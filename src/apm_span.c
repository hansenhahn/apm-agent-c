#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <trrlog1/trrlog.h>

#include <trrmap/trrmap.h>
#include <trrapm/apm.h>
#include <trrapm/apm_internal.h>
#include <trrapm/cJSON.h>


apm_span_t* apm_new_span(void)
{
    struct timeval tv;
    apm_span_t* span = calloc(1, sizeof(apm_span_t));
    if (!span) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao alocar memória.");
        goto catch;
    }

    span->id = generate_id(SPAN_ID_LEN);
    if (!span->id) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao gerar id único para o span.");
        goto catch;
    }

    if (gettimeofday(&tv, NULL) != 0) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao obter horário da máquina.");
    }
    else {
        span->timestamp = MICROS(tv);
    }

    goto finally;
catch:
    apm_free_span(span);
    free(span);
    span = NULL;
finally:
    return span;
}

void apm_free_span(apm_span_t* span)
{
    if (span) {
        free(span->id);
        free(span->name);
        free(span->type);
        free(span->subtype);
        free(span->transaction_id);
        free(span->parent_id);
        free(span->trace_id);
        free(span->outcome);
        trrmap_free((void **)&span->context);
        Lfreelist(span->children);
    }
}

void apm_begin_capture_span_internal(const char* name, const char* type, const char* subtype)
{
    apm_span_t* new_span = NULL;
    apm_transaction_t* current_transaction = apm_get_current_transaction();
    if (!current_transaction || !current_transaction->id || !current_transaction->trace_id) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhuma transação foi iniciada. [%s:%d]", __FILE__, __LINE__);
        return;
    }

    trrlog(apm_facility, TRRLOG_DEBUG, "Criando span. [%s:%d]", __FILE__, __LINE__);

    new_span = apm_new_span();
    if (!new_span) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao criar span. [%s:%d]", __FILE__, __LINE__);
        return; 
    }

    new_span->name = dup_value_or_default(name, NULL);
    new_span->type = dup_value_or_default(type, "code.custom");
    new_span->subtype = dup_value_or_default(subtype, NULL);
    new_span->transaction_id = dup_value_or_default(current_transaction->id, NULL);
    new_span->trace_id = dup_value_or_default(current_transaction->trace_id, NULL);

    if (current_transaction->span_depth == 0) {
        new_span->parent_id = dup_value_or_default(current_transaction->id, NULL);
        if (current_transaction->children == NULL) {
            current_transaction->children = Lopen();
        }
        Linsert(current_transaction->children, (char*)new_span, sizeof(apm_span_t), 1);
        current_transaction->span_depth++;
    } else {
        //! Estou apenas com um span_depth
        apm_span_t* current_span = apm_get_pending_span((apm_span_t*)Lcurrent(current_transaction->children));
        new_span->parent_id = dup_value_or_default(current_span->id, NULL);
        if (current_span->children == NULL) {
            current_span->children = Lopen();
        }
        Linsert(current_span->children, (char*)new_span, sizeof(apm_span_t), 1);
    }

    // Liberar a memória alocada para new_span após a inserção
    free(new_span);
}

void apm_end_capture_span_internal(const char* outcome)
{
    struct timeval tv;
    apm_span_t* current_span = NULL;
    apm_transaction_t* current_transaction = apm_get_current_transaction();
    if (!current_transaction) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhuma transação foi iniciada.");
        return;
    }

    trrlog(apm_facility, TRRLOG_DEBUG, "Encerrando span [%s:%d]", __FILE__, __LINE__);

    current_span = apm_get_pending_span((apm_span_t*)Lcurrent(current_transaction->children));
    if (!current_span) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhum span foi iniciado.");
        return;
    }

    if (gettimeofday(&tv, NULL) != 0) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao obter horário da máquina.");
    }
    else {
        current_span->duration = (double)(MICROS(tv) - current_span->timestamp) / 1000.0f;
    }
    current_span->outcome = dup_value_or_default(outcome, FAILURE);

    trrlog(apm_facility, TRRLOG_DEBUG, "current_span->id = %s [%s:%d]", current_span->id, __FILE__, __LINE__);
    trrlog(apm_facility, TRRLOG_DEBUG, "current_span->name = %s [%s:%d]", current_span->name, __FILE__, __LINE__);

    if (strcmp(current_span->parent_id, current_span->transaction_id) == 0) {
        current_transaction->span_depth--;
    }
}

void apm_add_str_to_span_context(char* value, ...)
{
    apm_span_t* current_span = NULL;
    apm_transaction_t* current_transaction = apm_get_current_transaction();
    if (!current_transaction) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhuma transação foi iniciada.");
        return;
    }

    current_span = apm_get_pending_span((apm_span_t*)Lcurrent(current_transaction->children));
    if (!current_span) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhum span foi iniciado.");
        return;
    }    

    if (!current_span->context) {
        current_span->context = trrmap_create_default();
    }

    va_list args;
    va_start(args, value);
    trrmap_vinsert(current_span->context, TRRMAP_VALUE_STR, value, args);
    va_end(args);
}

void apm_add_int_to_span_context(double* value, ...)
{
    apm_span_t* current_span = NULL;
    apm_transaction_t* current_transaction = apm_get_current_transaction();
    if (!current_transaction) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhuma transação foi iniciada.");
        return;
    }

    current_span = apm_get_pending_span((apm_span_t*)Lcurrent(current_transaction->children));
    if (!current_span) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhum span foi iniciado.");
        return;
    }    

    if (!current_span->context) {
        current_span->context = trrmap_create_default();
    }

    va_list args;
    va_start(args, value);
    trrmap_vinsert(current_span->context, TRRMAP_VALUE_NUMBER, value, args);
    va_end(args);
}

apm_span_t* apm_get_pending_span(apm_span_t* span)
{
    if (!span) {
        return NULL;
    }

    if (span->children) {
        apm_span_t* child_span = NULL;
        child_span = apm_get_pending_span((apm_span_t*)Lcurrent(span->children));
        if (child_span) {
            return child_span;
        }
    }

    return span->outcome == NULL ? span : NULL;
}

void apm_dump_span(LIST spanlist, char** buffer, int* span_count)
{
    if (!buffer) {
        return;
    }

    Lwalk(spanlist, LARGHOME);
    do {
        apm_span_t* current_span = (apm_span_t*)Lcurrent(spanlist);
        if (!current_span) {
            continue;
        }

        if (current_span->children) {
            apm_dump_span(current_span->children, buffer, span_count);
        }

        //! vamos converter para json
        char* partial_buffer = apm_span_to_json(current_span);
        apm_free_span(current_span);
        
        if (partial_buffer) {
            char *tmp = realloc(*buffer, strlen(*buffer) + strlen(partial_buffer) + 2);
            if (tmp) {
                *buffer = tmp;
                strcat(*buffer, partial_buffer);
                strcat(*buffer, "\n");
            }

            if (*span_count) {
                *span_count += 1;
            }

            free(partial_buffer);
        }
    } while (!Lwalk(spanlist, 1));
}

char* apm_span_to_json(apm_span_t* span)
{
    char* payload = NULL;
    //! vamos converter para json
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        goto catch;
    }

    cJSON* fld_span = cJSON_AddObjectToObject(json, "span");
    cJSON_AddStringToObject(fld_span, "id", span->id);
    cJSON_AddStringToObject(fld_span, "transaction_id", span->transaction_id);
    cJSON_AddStringToObject(fld_span, "trace_id", span->trace_id);
    cJSON_AddStringToObject(fld_span, "parent_id", span->parent_id);
    cJSON_AddStringToObject(fld_span, "name", span->name);
    cJSON_AddStringToObject(fld_span, "type", span->type);
    cJSON_AddStringToObject(fld_span, "subtype", span->subtype);
    cJSON_AddNumberToObject(fld_span, "timestamp", span->timestamp);
    cJSON_AddNumberToObject(fld_span, "duration", span->duration);
    cJSON_AddStringToObject(fld_span, "outcome", span->outcome);

    char* context_str = trrmap_serialize_json(span->context);
    if (context_str) {
        cJSON* fld_context = cJSON_Parse(context_str);
        if (fld_context) {
            cJSON_AddItemToObject(fld_span, "context", fld_context);
        }
        else {
            trrlog(apm_facility, TRRLOG_ERR, "Erro ao fazer o parser do contexto do span.");
        }

        free(context_str);
    }

    payload = cJSON_PrintUnformatted(json);
    goto finally;
catch:
    cJSON_Delete(json);
finally:
    return payload;
}