#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>
#include <sys/time.h>

#include <trrlog1/trrlog.h>
#include <trrmap/trrmap.h>
#include <trrapm/apm_internal.h>

#define CALL_STACK_MAX 32

apm_error_t* apm_new_error(void)
{
    struct timeval tv;
    apm_error_t* error = calloc(1, sizeof(apm_error_t));
    if (!error) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao alocar memória.");
        goto catch;
    }    

    error->id = generate_id(ERROR_ID_LEN);
    if (!error->id) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao gerar id único para o error.");
        goto catch;
    }

    if (gettimeofday(&tv, NULL) != 0) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao obter horário da máquina.");
    }
    else {
        error->timestamp = MICROS(tv);
    }

    error->exception.stacktrace = trrmap_create_default();

    list_t* list = trrmap_list_create_default();
    trrmap_insert(error->exception.stacktrace, TRRMAP_VALUE_LIST, list, "stacktrace", NULL);

    goto finally;
catch:
    apm_free_error(error);
    free(error);
    error = NULL;
finally:
    return error;
}

void apm_free_error(apm_error_t* error)
{
    if (error) {
        free(error->id);
        free(error->transaction_id);
        free(error->trace_id);
        free(error->parent_id);
        free(error->culprit);
        free(error->exception.type);
        free(error->exception.message);
        trrmap_free((void**)&error->exception.stacktrace);
    }
}

void apm_catch_error_internal(const char* culprit, const char* signal, const char* sig_message, const char** stacksym, size_t stack_size, bool handled)
{
    void* callstack[CALL_STACK_MAX] = {0};
    int stack_idx = 1;
    apm_error_t* new_error = NULL;
    apm_transaction_t* current_transaction = apm_get_current_transaction();
    if (!current_transaction) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhuma transação foi iniciada.");
        return;
    }

    trrlog(apm_facility, TRRLOG_DEBUG, "Capturando erro [%s:%d]", __FILE__, __LINE__);

    new_error = apm_new_error();
    new_error->exception.handled = handled;
    new_error->exception.type = dup_value_or_default(signal, "???");
    new_error->exception.message = dup_value_or_default(sig_message, "???");
    new_error->transaction_id = dup_value_or_default(current_transaction->id, NULL);
    new_error->trace_id = dup_value_or_default(current_transaction->trace_id, NULL);

    if (!stacksym) {
        stack_size = backtrace(callstack, CALL_STACK_MAX);
        stacksym = backtrace_symbols(callstack, stack_size);
        stack_idx++;
    }

    //! Vamos olhar a partir do índice 2, pois o índice 1 é esta função, e o indíce 0 é o próprio backtrace
    for (int i=stack_idx; i < stack_size; i++) {
        char* binary = NULL;
        char* function = NULL;
        char* location = NULL;
        char* lineno = NULL;
        trrlog(apm_facility, TRRLOG_DEBUG, "#%d %s", i-1, stacksym[i]);
        get_function_location_from_stack(stacksym[i], &binary, &function, &location, &lineno);

        apm_add_to_stacktrace(new_error, binary, location, function, 0);

        if (i==stack_idx) {
            new_error->culprit = dup_value_or_default(culprit, binary);
        }

        free(binary);
        free(function);
        free(location);
        free(lineno);
    }

    if (current_transaction->span_depth == 0) {
        new_error->parent_id = dup_value_or_default(current_transaction->id, NULL);
    } else {
        //! Estou apenas com um span_depth
        apm_span_t* current_span = apm_get_pending_span((apm_span_t*)Lcurrent(current_transaction->children));
        new_error->parent_id = dup_value_or_default(current_span->id, NULL);
    }

    if (!current_transaction->error) {
        current_transaction->error = Lopen();
    }
    Linsert(current_transaction->error, (char*)new_error, sizeof(apm_error_t), 1);
    free(new_error);
}

void apm_add_to_stacktrace(apm_error_t* error, const char* binary, const char* filename, const char* function, int lineno)
{
    hashmap_t* stacktrace = trrmap_create_default();
    if (!stacktrace) {
        return;
    }

    if (function) {
        trrmap_insert(stacktrace, TRRMAP_VALUE_STR, (void*)function, "function", NULL);
    }

    if (filename && filename[0] != '?') {
        trrmap_insert(stacktrace, TRRMAP_VALUE_STR, (void*)filename, "filename", NULL);
    }
    else if (binary) {
        trrmap_insert(stacktrace, TRRMAP_VALUE_STR, (void*)binary, "filename", NULL);
    }
    else {
        trrmap_insert(stacktrace, TRRMAP_VALUE_STR, "unknown", "filename", NULL);
    }

    list_t* list = NULL;
    if (trrmap_search(error->exception.stacktrace, (void**)&list, "stacktrace", NULL)) {
        trrmap_list_append_map(list, stacktrace);
    }
}

void apm_dump_error(LIST errorlist, char** buffer)
{
    if (!buffer) {
        return;
    }

    Lwalk(errorlist, LARGHOME);
    do {
        apm_error_t* error = (apm_error_t*)Lcurrent(errorlist);
        if (!error) {
            continue;
        }

        //! vamos converter para json
        char* partial_buffer = apm_error_to_json(error);
        apm_free_error(error);
        
        if (partial_buffer) {
            char *tmp = realloc(*buffer, strlen(*buffer) + strlen(partial_buffer) + 2);
            if (tmp) {
                *buffer = tmp;
                strcat(*buffer, partial_buffer);
                strcat(*buffer, "\n");
            }
            free(partial_buffer);
        }
    } while (!Lwalk(errorlist, 1));
}

char* apm_error_to_json(apm_error_t* error)
{
    //! vamos converter para json
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        goto catch;
    }

    cJSON* fld_error = cJSON_AddObjectToObject(json, "error");
    cJSON_AddStringToObject(fld_error, "id", error->id);
    cJSON_AddStringToObject(fld_error, "trace_id", error->trace_id);
    cJSON_AddStringToObject(fld_error, "transaction_id", error->transaction_id);
    cJSON_AddStringToObject(fld_error, "parent_id", error->parent_id);
    cJSON_AddNumberToObject(fld_error, "timestamp", error->timestamp);
    cJSON_AddStringToObject(fld_error, "culprit", error->culprit);

    cJSON* fld_exception = cJSON_AddObjectToObject(fld_error, "exception");
    cJSON_AddStringToObject(fld_exception, "message", error->exception.message);
    cJSON_AddStringToObject(fld_exception, "type", error->exception.type);
    cJSON_AddBoolToObject(fld_exception, "handled", error->exception.handled);

    char* stacktrace_str = trrmap_serialize_json(error->exception.stacktrace);
    if (stacktrace_str) {
        cJSON* fld_stacktrace = cJSON_Parse(stacktrace_str);
        if (fld_stacktrace) {
            cJSON_AddItemToObject(fld_exception, "stacktrace", cJSON_GetObjectItem(fld_stacktrace, "stacktrace"));
        }
        else {
            trrlog(apm_facility, TRRLOG_ERR, "Erro ao fazer o parser do stacktrace de erro.");
        }

        free(stacktrace_str);
    }

    char* payload = cJSON_PrintUnformatted(json);
    goto finally;
catch:
    cJSON_Delete(json);
finally:
    return payload;
}
