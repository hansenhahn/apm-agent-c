#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ucontext.h>
#include <execinfo.h>
#include <unistd.h>
#include <errno.h>

#include <trrlog/trrlog.h>
#include <trrapm/apm_internal.h>

#define CALL_STACK_MAX 32

static char curl_cmd[513] = {0};

static void apm_crash_backend_exec(void);

void apm_handle_exception_cb(int signo, siginfo_t* info, void* secret)
{
    void* callstack[CALL_STACK_MAX];
    int callstack_size;
    ucontext_t* uc = (ucontext_t*)secret;

    const char* signal = NULL;
    const char* sig_message = NULL;

    switch (signo) {
        case SIGSEGV:
            signal = "SIGSEGV";
            sig_message = "Segmentation Fault";
            break;
        case SIGABRT:
            signal = "SIGABRT";
            sig_message = "Abort";
            break;
    }

	callstack_size = backtrace(callstack, CALL_STACK_MAX);
	/* overwrite sigaction with caller's address */
#if __WORDSIZE == 64
	callstack[1] = (void*)uc->uc_mcontext.gregs[REG_RIP];
#else
	callstack[1] = (void *) uc->uc_mcontext.gregs[REG_EIP];
#endif

    char **messages = NULL;
    messages = backtrace_symbols(callstack, callstack_size);

    apm_catch_error(NULL, signal, sig_message, messages, callstack_size, 0);

    apm_end_capture_transaction_internal(FAILURE, NULL);
    apm_crash_backend_exec();
}

static void apm_crash_backend_exec(void)
{
    uint64_t val;
    trrlog(apm_facility, TRRLOG_DEBUG, "Enviando erro antes de morrer definitivamente.");

    //! temos um contexto altamente complicado aqui, onde o curl pode estar travado. vamos gerar o payload e jogar para
    //! o curl externo da máquina executar esta última chamada antes de morrer. se não for possível executar, paciência.
    char* payload = apm_create_payload(apm_get_current_transaction());
    if (!payload) {
        goto finally;
    }

    apm_config_t* config = apm_get_config();

    sprintf(curl_cmd, "gzip -c | curl -X POST %s/intake/v2/events "
                      "-H 'Content-Type: application/x-ndjson' "
                      "-H 'Content-Encoding: gzip' "
                      "-H 'Authorization: Bearer %s' "
                      "--data-binary @-", config->url, config->token);

    FILE *pipe = popen(curl_cmd, "w");
    if (!pipe) {
        trrlog(apm_facility, TRRLOG_DEBUG, "Erro ao abrir pipe");
        goto finally;
    }

    // Write your raw NDJSON payload (uncompressed)
    fprintf(pipe, "%s", payload);
    fflush(pipe); // Garante envio antes de fechar

    //! Vamos acreditar que foi .. o contexto de pids pode estar corrompido.
    fclose(pipe);

finally:
    free(payload);
}