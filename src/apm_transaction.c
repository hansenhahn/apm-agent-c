#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <trrlog1/trrlog.h>

#include <trrapm/apm.h>
#include <trrapm/apm_internal.h>
#include <trrapm/cJSON.h>

static apm_transaction_t* current_transaction = NULL;

apm_transaction_t* apm_new_transaction(const char* trace_id)
{
    struct timeval tv;
    apm_transaction_t* transaction = calloc(1, sizeof(apm_transaction_t));
    if (!transaction) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao alocar memória.");
        goto catch;
    }    

    transaction->id = generate_id(TRANSACTION_ID_LEN);
    if (!transaction->id) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao gerar id único para a transação.");
        goto catch;
    }

    transaction->trace_id = (!trace_id) ? generate_id(TRACE_ID_LEN) : strdup(trace_id);
    if (!transaction->trace_id) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao gerar trace id para a transação.");
        goto catch;
    }    

    if (gettimeofday(&tv, NULL) != 0) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao obter horário da máquina.");
    }
    else {
        transaction->timestamp = MICROS(tv);
    }

    goto finally;
catch:
    apm_free_transaction(transaction);
    free(transaction);
    transaction = NULL;
finally:
    return transaction;
}

void apm_free_transaction(apm_transaction_t* transaction)
{
    if (transaction) {
        free(transaction->id);
        free(transaction->name);
        free(transaction->type);
        free(transaction->trace_id);
        free(transaction->parent_id);
        free(transaction->outcome);
        free(transaction->result);
        Lfreelist(transaction->children);
        Lfreelist(transaction->error);
    }
}

void apm_begin_capture_transaction_internal(const char* name, const char* type, const char* trace_id, const char* parent_id)
{
    trrlog(apm_facility, TRRLOG_DEBUG, "Criando transação [%s:%d]", __FILE__, __LINE__);

    current_transaction = apm_new_transaction(trace_id);
    if (!current_transaction) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao criar transação. [%s:%d]", __FILE__, __LINE__);
        return;
    }

    current_transaction->name = dup_value_or_default(name, NULL);
    current_transaction->type = dup_value_or_default(type, NULL);
    current_transaction->parent_id = dup_value_or_default(parent_id, NULL);
}

void apm_end_capture_transaction_internal(const char* outcome, const char* result)
{
    struct timeval tv;
    if (!current_transaction) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhuma transação foi iniciada.");
        return;
    }

    trrlog(apm_facility, TRRLOG_DEBUG, "Finalizando transação [%s:%d]", __FILE__, __LINE__);

    gettimeofday(&tv, NULL);
    current_transaction->outcome = dup_value_or_default(outcome, NULL);
    current_transaction->result = dup_value_or_default(result, NULL);
    current_transaction->duration = (double)(MICROS(tv) - current_transaction->timestamp) / 1000.0f;

    trrlog(apm_facility, TRRLOG_DEBUG, "current_transaction->id = %s [%s:%d]", current_transaction->id, __FILE__, __LINE__);
    trrlog(apm_facility, TRRLOG_DEBUG, "current_transaction->name = %s [%s:%d]", current_transaction->name, __FILE__, __LINE__);
}

apm_transaction_t* apm_get_current_transaction(void)
{
    return current_transaction;
}

void apm_clear_current_transaction(void)
{
    free(current_transaction);
    current_transaction = NULL;
}

void apm_dump_transaction(apm_transaction_t* transaction, char** buffer)
{
    if (!buffer || !transaction) {
        return;
    }

    //! vamos converter para json
    char* partial_buffer = apm_transaction_to_json(transaction);
    if (partial_buffer) {
        char *tmp = realloc(*buffer, strlen(*buffer) + strlen(partial_buffer) + 2);
        if (tmp) {
            *buffer = tmp;
            strcat(*buffer, partial_buffer);
            strcat(*buffer, "\n");
        }

        free(partial_buffer);
    }
}

char* apm_transaction_to_json(apm_transaction_t* transaction)
{
    char* payload = NULL;
    //! vamos converter para json
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        goto catch;
    }

    cJSON* fld_transaction = cJSON_AddObjectToObject(json, "transaction");
    cJSON_AddStringToObject(fld_transaction, "id", transaction->id);
    cJSON_AddStringToObject(fld_transaction, "trace_id", transaction->trace_id);
    if (transaction->parent_id && transaction->parent_id[0]) {
        cJSON_AddStringToObject(fld_transaction, "parent_id", transaction->parent_id);
    }

    cJSON_AddStringToObject(fld_transaction, "name", transaction->name);
    cJSON_AddStringToObject(fld_transaction, "type", transaction->type);
    cJSON_AddNumberToObject(fld_transaction, "timestamp", transaction->timestamp);
    cJSON_AddNumberToObject(fld_transaction, "duration", transaction->duration);
    cJSON_AddStringToObject(fld_transaction, "result", transaction->result);
    cJSON_AddStringToObject(fld_transaction, "outcome", transaction->outcome);

    cJSON* fld_span_count = cJSON_AddObjectToObject(fld_transaction, "span_count");
    cJSON_AddNumberToObject(fld_span_count, "started", transaction->span_count);
    cJSON_AddNumberToObject(fld_span_count, "dropped", transaction->span_dropped);

    payload = cJSON_PrintUnformatted(json);
    goto finally;
catch:
    cJSON_Delete(json);
finally:
    return payload;
}
