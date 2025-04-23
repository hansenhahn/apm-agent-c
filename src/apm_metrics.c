#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <trrlog/trrlog.h>
#include <trrutil/ndtlist.h>

#include <trrapm/apm.h>
#include <trrapm/apm_internal.h>
#include <trrapm/apm_rest.h>

static pthread_t threadh;
static pthread_mutex_t mutexh;
static pthread_cond_t condh;

static LIST transaction_queue;

static int __thread_init = 0;
static int __thread_ready = 0;
static int __thread_destroy = 0;

static void* apm_metrics_thread(void* arg);

void apm_init_metrics(void)
{
    if (__thread_init++ == 0) {
        if (pthread_mutex_init(&mutexh, NULL) != 0) {
            trrlog(apm_facility, TRRLOG_ERR, "Erro ao criar mutex interno [%s:%d]", __FILE__, __LINE__);
            goto except_clear_mutex;
        }

        if (pthread_cond_init(&condh, NULL) != 0) {
            trrlog(apm_facility, TRRLOG_ERR, "Erro ao criar sinalizador interno [%s:%d]", __FILE__, __LINE__);
            goto except_clear_cond;
        }

        if (pthread_create(&threadh, NULL, apm_metrics_thread, NULL) != 0) {
            trrlog(apm_facility, TRRLOG_ERR, "Erro ao criar thread interna [%s:%d]", __FILE__, __LINE__);
            goto except_clear_thread;
        }

        transaction_queue = Lopen();

        trrlog(apm_facility, TRRLOG_DEBUG, "Thread de métricas criada com sucesso [%s:%d]", __FILE__, __LINE__);
    }

    goto finally;
except_clear_thread:
except_clear_cond:
    pthread_cond_destroy(&condh);
except_clear_mutex:
    pthread_mutex_destroy(&mutexh);
    __thread_init--;
finally:
    return;
}

void apm_destroy_metrics(void)
{
    if (__thread_init-- > 0) {
        __thread_destroy = 1;

        apm_signal_metrics();
        pthread_join(threadh, NULL);
        __thread_destroy = 0;

        pthread_cond_destroy(&condh);
        pthread_mutex_destroy(&mutexh);
    }
}

static void* apm_metrics_thread(void* arg)
{
    apm_metadata_t* metadatat = apm_new_metadata();
    if (metadatat == NULL) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao criar estrutura de metadata");
        return NULL;
    }

    char* metadata = NULL;
    apm_dump_metadata(metadatat, &metadata);

    apm_stats_t* old_stats = apm_collect_metrics();

    while (1) {
        struct timespec tv;
        clock_gettime(CLOCK_REALTIME, &tv);
        tv.tv_sec += 10;

        apm_wait_metrics(&tv);

        if (__thread_destroy) {
            trrlog(apm_facility, TRRLOG_DEBUG, "Destruindo thread");
            break;
        }

        trrlog(apm_facility, TRRLOG_DEBUG, "Enviando métricas");

        char* payload = strdup(metadata);

        apm_stats_t* new_stats = apm_collect_metrics();

        trrlog(apm_facility, TRRLOG_DEBUG, "stats->system->cpu_usage=%f", new_stats->system->cpu_usage);
        trrlog(apm_facility, TRRLOG_DEBUG, "stats->system->cpu_total=%f", new_stats->system->cpu_total);
        trrlog(apm_facility, TRRLOG_DEBUG, "stats->process->utime=%f", new_stats->process->utime);
        trrlog(apm_facility, TRRLOG_DEBUG, "stats->process->stime=%f", new_stats->process->stime);
        trrlog(apm_facility, TRRLOG_DEBUG, "stats->process->vsize=%f", new_stats->process->vsize);
        trrlog(apm_facility, TRRLOG_DEBUG, "stats->process->rss=%f", new_stats->process->rss);

        apm_dump_metrics(new_stats, old_stats, &payload);

        trrlog(apm_facility, TRRLOG_DEBUG, "%s", payload);
        apm_create_intake_event_request(payload);

        apm_free_metrics(old_stats);
        old_stats = new_stats;

        free(payload);
    }

    free(metadata);
    apm_free_metrics(old_stats);

    return NULL;
}

apm_stats_t* apm_collect_metrics(void)
{
    struct timeval tv;
    apm_stats_t* stats = calloc(1, sizeof(apm_stats_t));

    gettimeofday(&tv, NULL);
    stats->timestamp = MICROS(tv);

    apm_lock_metrics();
    stats->system = apm_read_system_stats();
    stats->process = apm_read_process_stats();

    apm_unlock_metrics();

    return stats;
}

void apm_free_metrics(apm_stats_t* stats)
{
    apm_free_process_stats(stats->process);
    apm_free_system_stats(stats->system);
    free(stats);
}

void apm_dump_metrics(apm_stats_t* new, apm_stats_t* old, char** buffer)
{
    apm_process_stats_t currp = {
        .stime = new->process->stime,
        .utime = new->process->utime,
        .proc_total_time = new->process->proc_total_time - old->process->proc_total_time,
        .vsize = new->process->vsize,
        .rss = new->process->rss
    };

    apm_system_stats_t currs = {
        .cpu_total = new->system->cpu_total - old->system->cpu_total,
        .cpu_usage = new->system->cpu_usage - old->system->cpu_usage
    };

    apm_stats_t curr = {
        .process = &currp,
        .system = &currs,
        .timestamp = new->timestamp
    };

    //! vamos converter para json
    char* partial_buffer = apm_stats_to_json(&curr);

    *buffer = realloc(*buffer, strlen(*buffer) + strlen(partial_buffer) + 2);
    strcat(*buffer, partial_buffer);
    strcat(*buffer, "\n");

    free(partial_buffer);
}

void apm_lock_metrics(void)
{
    pthread_mutex_lock(&mutexh);
}

void apm_unlock_metrics(void)
{
    pthread_mutex_unlock(&mutexh);
}

void apm_wait_metrics(struct timespec* tv)
{
    apm_lock_metrics();
    while (!__thread_ready) {
        pthread_cond_timedwait(&condh, &mutexh, tv);
    }
    __thread_ready = 0;
    apm_unlock_metrics();
}

void apm_signal_metrics(void)
{
    apm_lock_metrics();
    __thread_ready = 1;
    pthread_cond_signal(&condh);
    apm_unlock_metrics();
}
