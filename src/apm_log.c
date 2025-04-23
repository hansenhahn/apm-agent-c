#include <pthread.h>
#include <trrlog1/trrlog.h>

//! precisamos reescrever a trrlog easy para adicionarmos mutex, já que o apm lança uma thread para envio dos dados

static pthread_mutex_t mutexh;

static trrlog_t* g_log_handle = NULL;
static int g_initialized = 0;
static int g_pthread_aware = 0;

static void trrlog_lock(void);
static void trrlog_unlock(void);

int trrlog_init(int p_thread_aware)
{
    if (g_initialized++ == 0) {
        if (p_thread_aware) {
            g_pthread_aware = 1;
            if (pthread_mutex_init(&mutexh, NULL) != 0) {
                return -__LINE__;
            }
        }

        g_log_handle = trrlog_new(p_thread_aware);
        if (!g_log_handle) {
            return -__LINE__;
        }
    }
    return 0;
}

int trrlog_config(const char* p_str)
{
    return trrlog_parse_config(g_log_handle, p_str);
}

int trrlog(int p_facility, int p_level, const char* p_msg, ...)
{
    va_list params;
    int ret;

    va_start(params, p_msg);
    trrlog_lock();
    ret = trrlog_vlog(g_log_handle, p_facility, p_level, p_msg, params);
    trrlog_unlock();
    va_end(params);

    return ret;
}

int trrlog_register(const char* p_name)
{
    return trrlog_register_facility(g_log_handle, p_name);
}

int trrlog_finish(void)
{
    if (--g_initialized == 0) {
        trrlog_delete(g_log_handle);
        if (g_pthread_aware) {
            pthread_mutex_destroy(&mutexh);
        }
    }
    return 0;
}

int trrlogv(int facility, int level, const char* msg, va_list p_args)
{
    trrlog_lock();
    int ret = trrlog_vlog(g_log_handle, facility, level, msg, p_args);
    trrlog_unlock();
    return ret;
}

void trrlog_reopen(void)
{
    trrlog_reopen_files(g_log_handle);
}

void trrlog_label(const char* p)
{
    trrlog_set_label(g_log_handle, p);
}

int trrlog_config_from_file(const char* p_file)
{
    return trrlog_parse_config_from_file(g_log_handle, p_file);
}

static void trrlog_lock(void)
{
    if (g_pthread_aware) {
        pthread_mutex_lock(&mutexh);
    }
}

static void trrlog_unlock(void)
{
    if (g_pthread_aware) {
        pthread_mutex_unlock(&mutexh);
    }
}