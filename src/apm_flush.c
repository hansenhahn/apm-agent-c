#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
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

static char* metadata = NULL;

/**
 * @brief Background thread that sends completed APM transactions to the server.
 *
 * This function runs in a dedicated thread and is responsible for dequeuing
 * finalized transactions, serializing them to JSON, and sending them to the APM
 * server endpoint. Its primary goal is to decouple network operations from the
 * main application flow, avoiding latency penalties during instrumentation.
 *
 * Once a transaction is complete, it is pushed into a queue. This thread monitors
 * that queue and processes transactions asynchronously, ensuring that the main
 * application thread remains responsive.
 *
 * The following operations are handled:
 * - Retrieves transactions from the internal queue.
 * - Serializes the transaction and its spans into JSON format.
 * - Sends the JSON payload over the network using libcurl.
 * - Logs any failures or diagnostics for monitoring purposes.
 *
 * @param arg Unused parameter. Present for thread API compatibility.
 * @return Always returns NULL.
 *
 * @note This thread is usually started during APM initialization and runs
 *       for the lifetime of the application.
 *
 * @warning This function is not intended to be called directly. It should only
 *          be used as a thread entry point. Transactions must be safely
 *          transferred to the queue before being handled here.
 */
static void* apm_flush_thread(void* arg);

void apm_init_flush(void)
{
    if (__thread_init++ == 0) {
        apm_metadata_t* metadatat = apm_new_metadata();
        if (metadatat == NULL) {
            trrlog(apm_facility, TRRLOG_ERR, "Erro ao criar estrutura de metadata");
            return NULL;
        }

        apm_dump_metadata(metadatat, &metadata);

        if (pthread_mutex_init(&mutexh, NULL) != 0) {
            trrlog(apm_facility, TRRLOG_ERR, "erro ao criar mutex interno [%s:%d]", __FILE__, __LINE__);
            goto except_clear_mutex;
        }

        if (pthread_cond_init(&condh, NULL) != 0) {
            trrlog(apm_facility, TRRLOG_ERR, "erro ao criar sinalizador interno [%s:%d]", __FILE__, __LINE__);
            goto except_clear_cond;
        }

        if (pthread_create(&threadh, NULL, apm_flush_thread, NULL) != 0) {
            trrlog(apm_facility, TRRLOG_ERR, "erro ao criar thread interna [%s:%d]", __FILE__, __LINE__);
            goto except_clear_thread;
        }

        transaction_queue = Lopen();

        trrlog(apm_facility, TRRLOG_DEBUG, "thread de envio criada com sucesso [%s:%d]", __FILE__, __LINE__);
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

void apm_destroy_flush(void)
{
    if (__thread_init-- > 0) {
        __thread_destroy = 1;

        apm_signal_flush();
        pthread_join(threadh, NULL);
        __thread_destroy = 0;

        pthread_cond_destroy(&condh);
        pthread_mutex_destroy(&mutexh);
    }
}

static void* apm_flush_thread(void* arg)
{
    while (1) {
        apm_wait_flush();

        if (__thread_destroy) {
            trrlog(apm_facility, TRRLOG_DEBUG, "Destruindo thread");
            break;
        }

        apm_lock_flush();
        Lwalk(transaction_queue, LARGHOME);
        apm_transaction_t* transaction = (apm_transaction_t*)Lcurrent(transaction_queue);
        apm_unlock_flush();

        apm_flush_transaction_internal(transaction);

        apm_lock_flush();
        //! Caso o POST para o APM demore demais, a thread principal pode adicionar uma nova transação na queue.
        //! Neste caso, a posição corrente acaba apontando para esta nova transação. Assim precisamos forçar o retorno para
        //! a posição 0 da queue.
        Lwalk(transaction_queue, LARGHOME);
        Lfree(transaction_queue);
        apm_unlock_flush();
    }

    //free(metadata);

    return NULL;
}

int apm_check_flush_constraints(apm_transaction_t* transaction, apm_config_t* config)
{
    if (config->constraints.flush_if_error && (strcmp(transaction->outcome, FAILURE)==0)) {
        return 1;
    }

    if (transaction->duration > config->constraints.flush_if_min_duration) {
        return 1;
    }

    return 0;
}

char* apm_create_payload(apm_transaction_t* transaction)
{
    if (!transaction) {
        trrlog(apm_facility, TRRLOG_ERR, "Nenhuma transação foi informada.");
        return NULL;
    }

    char* payload = dup_value_or_default(metadata, "");

    if (transaction->error) {
        apm_dump_error(transaction->error, &payload);
    }

    //! vamos correr todos os spans filhos da transação
    if (transaction->children) {
        apm_dump_span(transaction->children, &payload, &transaction->span_count);
    }

    apm_dump_transaction(transaction, &payload);
    return payload;
}

void apm_flush_transaction_internal(apm_transaction_t* transaction)
{
    apm_config_t* config = apm_get_config();    
    char* payload = apm_create_payload(transaction);

    trrlog(apm_facility, TRRLOG_DEBUG, "Enviando informações para o transaction->id = %s [%s:%d]", transaction->id, __FILE__, __LINE__);

    if (apm_check_flush_constraints(transaction, config)) {
        apm_create_intake_event_request(payload);
    }
    else {
        trrlog(apm_facility, TRRLOG_DEBUG, "Transação descartada");
    }

    apm_free_transaction(transaction);

    free(payload);
}

void apm_lock_flush(void)
{
    pthread_mutex_lock(&mutexh);
}

void apm_unlock_flush(void)
{
    pthread_mutex_unlock(&mutexh);
}

void apm_signal_flush(void)
{
    apm_lock_flush();
    __thread_ready = 1;
    pthread_cond_signal(&condh);
    apm_unlock_flush();
}

void apm_wait_flush(void)
{
    apm_lock_flush();
    while(!__thread_ready){
        pthread_cond_wait(&condh, &mutexh);
    }
    __thread_ready = 0;
    apm_unlock_flush();
}

void apm_add_to_flush_queue(void* data, size_t len)
{
    if (data && len) {
        apm_lock_flush();
        Linsert(transaction_queue, (char*)data, len, 1);
        apm_unlock_flush();
    }
}
