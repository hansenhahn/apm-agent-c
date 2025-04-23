#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <curl/curl.h>

#include <trrlog1/trrlog.h>
#include <trrutil/ndtlist.h>
#include <trrapm/apm.h>
#include <trrapm/apm_internal.h>

#define APM_FACILITY_LABEL "APM"
int apm_facility = -1;

static apm_config_t default_config = {
    .bypass = 1
};
static apm_config_t* apm_config = &default_config;

void apm_init(apm_config_t* config)
{
    apm_config = config;
    apm_facility = trrlog_register(APM_FACILITY_LABEL);

    //! precisamos instalar os stubs
    apm_init_libtrrmanager_stubs();
    apm_init_libcurl_stubs();
    apm_init_libwsclient_stubs();
    apm_init_libtrrvm_stubs();
    apm_init_libtrrvmcomm_stubs();

    if (apm_config && !apm_config->bypass) {
    #ifdef APM_SPAWN_METRICS
        apm_init_metrics();
    #endif
        apm_init_flush();
    } else {
        trrlog(apm_facility, TRRLOG_DEBUG, "APM não habilitado [%s:%d]", __FILE__, __LINE__);
    }
}

void apm_destroy(void)
{
    if (apm_config && !apm_config->bypass) {
        apm_destroy_flush();
    #ifdef APM_SPAWN_METRICS
        apm_destroy_metrics();
    #endif
        trrlog(apm_facility, TRRLOG_DEBUG, "Finalizando APM [%s:%d]", __FILE__, __LINE__);
    }
}

void apm_flush(void)
{
    if (apm_config && !apm_config->bypass) {
        trrlog(apm_facility, TRRLOG_DEBUG, "Flushing transação [%s:%d]", __FILE__, __LINE__);
        apm_signal_flush();
    }
}

void apm_begin_capture_transaction(const char* name, const char* type, const char* trace_id, const char* parent_id)
{
    if (apm_config && !apm_config->bypass) {
        apm_begin_capture_transaction_internal(name, type, trace_id, parent_id);
    }
}

void apm_end_capture_transaction(const char* outcome, const char* result)
{
    if (apm_config && !apm_config->bypass) {
        apm_end_capture_transaction_internal(outcome, result);

        apm_add_to_flush_queue(apm_get_current_transaction(), sizeof(apm_transaction_t));
        apm_clear_current_transaction();
        apm_flush();
    }
}

void apm_begin_capture_span(const char* name, const char* type, const char* subtype)
{
    if (apm_config && !apm_config->bypass) {
        apm_begin_capture_span_internal(name, type, subtype);
    }
}

void apm_end_capture_span(const char* outcome)
{
    if (apm_config && !apm_config->bypass) {
        apm_end_capture_span_internal(outcome);
    }
}

void apm_catch_error(const char* culprit, const char* signal, const char* sig_message, const char** stacksym, size_t stack_size, bool handled)
{
    if (apm_config && !apm_config->bypass) {
        apm_catch_error_internal(culprit, signal, sig_message, stacksym, stack_size, handled);
    }
}

apm_config_t* apm_get_config(void)
{
    return apm_config;
}

void apm_get_traceparent_info(const char* traceparent, char** trace_id, char** parent_id)
{
    *trace_id = NULL;
    *parent_id = NULL;

    if (traceparent != NULL) {
        char trace_id_str[TRACE_ID_LEN+1] = {0};
        char parent_id_str[SPAN_ID_LEN+1] = {0};

        if (sscanf(traceparent, "00-%32[0-9a-fA-F]-%16[0-9a-fA-F]-01", trace_id_str, parent_id_str)) {
            *trace_id = dup_value_or_default(trace_id_str, NULL);
            *parent_id = dup_value_or_default(parent_id_str, NULL);
        }
    }
}
