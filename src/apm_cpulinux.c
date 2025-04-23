#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

#include <trrapm/apm_internal.h>
#include <trrlog1/trrlog.h>

#define SYS_STATS "/proc/stat"
#define PROC_STATS "/proc/self/stat"

apm_system_stats_t* apm_read_system_stats(void)
{
    FILE* fd = NULL;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;

    fd = fopen(SYS_STATS, "r");
    if (fd == NULL) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao ler %s", SYS_STATS);
        return NULL;
    }

    fscanf(fd, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
        &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(fd);

    apm_system_stats_t* stats = calloc(1, sizeof(apm_system_stats_t));

    stats->cpu_total = (double)(user + nice + system + idle + iowait + irq + softirq + steal);
    stats->cpu_usage = stats->cpu_total - (double)(idle + iowait);

    return stats;
}

apm_process_stats_t* apm_read_process_stats(void)
{
    FILE* fd = NULL;
    fd = fopen(PROC_STATS, "r");
    if (fd == NULL) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao ler %s", PROC_STATS);
        return NULL;
    }

    unsigned long long utime, stime, vsize;
    long rss;

    fscanf(fd,
        "%*d (%*[^)]) %*c "
        "%*d %*d %*d %*d %*d "
        "%*u %*u %*u %*u %*u "
        "%llu %llu " // 14: utime, 15: stime
        "%*d %*d %*d %*d %*d %*d"
        "%*llu %llu %ld", // 23: vsize, 24: rss
        &utime, &stime, &vsize, &rss);
    fclose(fd);

    apm_process_stats_t* stats = calloc(1, sizeof(apm_process_stats_t));
    stats->utime = (double)utime;
    stats->stime = (double)stime;
    stats->proc_total_time = (double)(utime + stime);
    stats->vsize = (double)vsize;
    stats->rss = (double)rss;

    return stats;
}

void apm_free_system_stats(apm_system_stats_t* stats)
{
    free(stats);
}

void apm_free_process_stats(apm_process_stats_t* stats)
{
    free(stats);
}

char* apm_stats_to_json(apm_stats_t* stats)
{
    //! vamos converter para json

    cJSON* json = cJSON_CreateObject();
    cJSON* fld_metricset = cJSON_AddObjectToObject(json, "metricset");

    cJSON_AddNumberToObject(fld_metricset, "timestamp", stats->timestamp);
    cJSON* fld_samples = cJSON_AddObjectToObject(fld_metricset, "samples");

    cJSON* fld_stat1 = cJSON_AddObjectToObject(fld_samples, "system.cpu.total.norm.pct");
    if (stats->system->cpu_total == 0) {
        cJSON_AddNumberToObject(fld_stat1, "value", 0);
    } else {
        cJSON_AddNumberToObject(fld_stat1, "value", stats->system->cpu_usage / stats->system->cpu_total);
    }
    cJSON_AddStringToObject(fld_stat1, "type", "gauge");

    cJSON* fld_stat2 = cJSON_AddObjectToObject(fld_samples, "system.process.cpu.total.norm.pct");
    if (stats->system->cpu_total == 0) {
        cJSON_AddNumberToObject(fld_stat2, "value", 0);
    } else {
        cJSON_AddNumberToObject(fld_stat2, "value", stats->process->proc_total_time / stats->system->cpu_total);
    }
    cJSON_AddStringToObject(fld_stat2, "type", "gauge");

    cJSON* fld_stat3 = cJSON_AddObjectToObject(fld_samples, "system.process.memory.size");
    cJSON_AddNumberToObject(fld_stat3, "value", stats->process->vsize);
    cJSON_AddStringToObject(fld_stat3, "type", "gauge");

    long page_size = sysconf(_SC_PAGE_SIZE);
    cJSON* fld_stat4 = cJSON_AddObjectToObject(fld_samples, "system.process.memory.rss.bytes");
    cJSON_AddNumberToObject(fld_stat4, "value", stats->process->rss * page_size);
    cJSON_AddStringToObject(fld_stat4, "type", "gauge");

    char* payload = cJSON_PrintUnformatted(json);

    cJSON_Delete(json);

    return payload;
}
