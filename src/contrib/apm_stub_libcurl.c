#ifdef __BUILD_APM_WITH_LIBCURL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <curl/curl.h>

#include <trrlog1/trrlog.h>
#include <trrapm/apm.h>
#include <trrapm/apm_internal.h>

#define LIBCURL_SO "libcurl.so.4"

#define HTTP_GET "GET"
#define HTTP_POST "POST"
#define HTTP_PUT "PUT"
#define HTTP_HEAD "HEAD"

struct CURLset {
    char* url;
    char* method;
    struct curl_slist* header;
}; 

//! Não é thread-safe
#warning not thread-safe
static struct CURLset set = {0};

static void add_traceparent_header(CURL* curl);
CURLcode (*curl_easy_perform_s)(CURL*) = NULL;
CURLcode (*curl_easy_setopt_s)(CURL*, CURLoption, ...) = NULL;
void (*curl_easy_cleanup_s)(CURL*) = NULL;

static void add_traceparent_header(CURL* curl)
{
    char *trace_id = NULL;
    char *parent_id = NULL;
    char *traceparent = NULL;

    if (!curl) {
        return;
    }

    apm_transaction_t* current_transaction = apm_get_current_transaction();
    if (!current_transaction) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhuma transação foi iniciada.");
        return;
    }

    if (current_transaction->children) {
        apm_span_t* current_span = apm_get_pending_span((apm_span_t*)Lcurrent(current_transaction->children));
        if (!current_span || !current_span->trace_id || !current_span->id) {
            trrlog(apm_facility, TRRLOG_ERR, "Nenhuma span foi iniciado.");
            return;
        }
        trace_id = current_span->trace_id;
        parent_id = current_span->id;
    }
    else {
        if (!current_transaction->trace_id || !current_transaction->id) {
            trrlog(apm_facility, TRRLOG_ERR, "Nenhuma span foi iniciado.");
            return;
        }
        trace_id = current_transaction->trace_id;
        parent_id = current_transaction->id;
    }

    if (asprintf(&traceparent, "traceparent: 00-%s-%s-01", trace_id, parent_id)==-1) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao criar header \"traceparent\".");
        return;
    }

    struct curl_slist* tmp = curl_slist_append(set.header, traceparent);
    if (!tmp) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao adicionar header \"traceparent\".");
        free(traceparent);
        return;

    }
    
    if (curl_easy_setopt_s(curl, CURLOPT_HTTPHEADER, tmp) != CURLE_OK) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao configurar header na chamada curl.");
    }

    free(traceparent);
}

#undef curl_easy_setopt
CURLcode curl_easy_setopt(CURL* curl, CURLoption option, ...)
{
    //! Não há uma forma simples de obter a configuração do curl visto que este utiliza uma estrutura opaca. 
    //! Então mantemos um registro das alterações das principais opções.
    va_list arg;
    CURLcode result;
    long val;
    void* ptr = NULL;

    if(!curl) {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }

    va_start(arg, option);

    //! os argumentos do curl podem ser apenas de dois tipo, ou long ou um ponteiro.
    //! vamos tratar todas as opções do segundo caso como void*.
    if(option < CURLOPTTYPE_OBJECTPOINT) {
        val = va_arg(arg, long);
    }
    else {
        ptr = va_arg(arg, void*);
    }

    va_end(arg);

    apm_config_t* apm_config = apm_get_config();

    if (apm_config && !apm_config->bypass) {
        switch (option) {
            case CURLOPT_HTTPHEADER:
                //! shallow copy. não fazer free desta estrutura.
                set.header = (struct curl_slist*)ptr;
                break;
            case CURLOPT_POSTFIELDS:
                free(set.method);
                set.method = strdup(HTTP_POST);
                break;
            case CURLOPT_POST:
                free(set.method);
                set.method = strdup(val ? HTTP_POST : HTTP_GET);
                break;
            case CURLOPT_HTTPGET:
                if (val) {
                    free(set.method);
                    set.method = strdup(HTTP_GET);
                }
                break;
            case CURLOPT_UPLOAD:
                if (val) {
                    free(set.method);
                    set.method = strdup(HTTP_PUT);
                }
                break;
            case CURLOPT_NOBODY:
                if (val) {
                    free(set.method);
                    set.method = strdup(HTTP_HEAD);
                }
                break;
            case CURLOPT_CUSTOMREQUEST:
                if (ptr != NULL) {
                    free(set.method);
                    set.method = strdup((const char*)ptr);
                }
                break;
            case CURLOPT_URL:
                if (ptr != NULL) {
                    free(set.url);
                    set.url = strdup((const char*)ptr);
                }
                break;   
            default:
                break;
        }
    }

    //! backward compability. apenas a partir do curl 7.85.0, a função curl_vsetopt (que aceita va_list como
    //! argumento) deixa de ser static. sabemos de antemão que, apesar de variadic, a curl_easy_setopt olha apenas
    //! o primeiro argumento.
    result = (option < CURLOPTTYPE_OBJECTPOINT)
        ? curl_easy_setopt_s(curl, option, val)
        : curl_easy_setopt_s(curl, option, ptr);

    return result;
}

void curl_easy_cleanup(CURL* curl)
{
    apm_config_t* apm_config = apm_get_config();
    if (!(apm_config && !apm_config->bypass)) {
        //! set.header é um shallow copy do curl->set.header. curl_easy_cleanup já dá o free desta estrutura.
        set.header = NULL;
        free(set.method);
        set.method = NULL;
        free(set.url);
        set.url = NULL;
    }
    return curl_easy_cleanup_s(curl);
}

CURLcode curl_easy_perform(CURL* curl)
{
    CURLcode ret;
    char* full_url = NULL;
    apm_config_t* apm_config = apm_get_config();
    if (!(apm_config && !apm_config->bypass)) {
        goto catch;
    }

    if (asprintf(&full_url, "%s %s", set.method, set.url)==-1 || !full_url) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao alocar memória.");
        goto catch;
    }

    apm_begin_capture_span((const char*)full_url, "external", "http");
    //! adiciona traceparent header para tracing distribuído.
    add_traceparent_header(curl);

    //curl_easy_setopt_s(curl, CURLOPT_VERBOSE, 1);
    ret = curl_easy_perform_s(curl);

    const char *outcome = SUCCESS;
    long http_code = 0;
    long remote_port = 0;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code) != CURLE_OK) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao adquirir \"HTTP code\".");
    }
    if (curl_easy_getinfo(curl, CURLINFO_PRIMARY_PORT, &remote_port) != CURLE_OK) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao adquirir \"remote port\".");
    }

    if (ret != CURLE_OK || http_code >= 400) {
        outcome = FAILURE;
    }

    if (ret == CURLE_OK) {
        double code = (double)http_code;
        apm_add_int_to_span_context(&code, "http", "status_code", NULL);
    }

    apm_add_str_to_span_context(set.url, "service", "target", "name", NULL);
    apm_add_str_to_span_context("http", "service", "target", "type", NULL);

    double port = (double)remote_port;
    apm_add_str_to_span_context(set.url, "destination", "service", "name", NULL);
    apm_add_str_to_span_context(set.url, "destination", "service", "resource", NULL);
    apm_add_str_to_span_context("external", "destination", "service", "type", NULL);
    apm_add_str_to_span_context(set.url, "destination", "address", NULL);
    apm_add_int_to_span_context(&port, "destination", "port", NULL);

    apm_add_str_to_span_context(set.url, "http", "url", NULL);
    apm_add_str_to_span_context(set.method, "http", "method", NULL);

    apm_end_capture_span(outcome);
    
    goto finally;
catch:
    //! em caso de erro, ainda tentamos chamar o curl.
    ret = curl_easy_perform_s(curl);
finally:
    free(full_url);
    return ret;
}

void apm_init_libcurl_stubs(void)
{
    void* handle = dlopen(LIBCURL_SO, RTLD_LOCAL | RTLD_LAZY);
    if (!handle) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao executar dlopen("LIBCURL_SO"): %s", dlerror());
        return;
    }

    curl_easy_perform_s = dlsym(handle, "curl_easy_perform");
    if (!curl_easy_perform_s) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao executar dlsym(\"curl_easy_perform\"): %s", dlerror());
        return;
    }

    curl_easy_setopt_s = dlsym(handle, "curl_easy_setopt");
    if (!curl_easy_setopt_s) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao executar dlsym(\"curl_easy_setopt\"): %s", dlerror());
        return;
    }

    curl_easy_cleanup_s = dlsym(handle, "curl_easy_cleanup");
    if (!curl_easy_cleanup_s) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao executar dlsym(\"curl_easy_cleanup\"): %s", dlerror());
        return;
    }
}
#else
#warning Building without instrumented libcurl
#include <stdio.h>
void (*curl_easy_perform_s)(void) = NULL;
void (*curl_easy_setopt_s)(void) = NULL;
void (*curl_easy_cleanup_s)(void) = NULL;
void apm_init_libcurl_stubs(void){}

#endif
