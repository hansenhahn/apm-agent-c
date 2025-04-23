/*==============================================================================
SGTEC - Terra
Copyright (C) 2024 Terra Networks Brasil S.A.
--------------------------------------------------------------------------------
 DESCRIPTION:  Biblioteca de comunicação com uma API REST-like bem simples.
 AUTHORS:      (RWPS) Roger W P Silva (roger.silva@cwi.com.br)

-Date--------Version-Who---What-------------------------------------------------
 08.mar.2024 1.0.0   RWPS  Criou esse arquivo com SGTEC Boot 0.0.2
==============================================================================*/
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trrlog1/trrlog.h>
#include <trrutil3/base64.h>
#include <zlib.h>

#include <trrapm/apm.h>
#include <trrapm/apm_rest.h>

// Buffer para a payload de resposta
typedef struct {
    char* data; //!< Ponteiro que armazena os dados
    size_t size; //!< Quantidade de caracteres dos dados
} Payload;

// Estrutura que guarda a lista de cabeçalhos
struct headers {
    struct curl_slist* list;
};

static char* gzip_compress(const char* input, size_t input_size, size_t* output_size);

extern CURLcode (*curl_easy_perform_s)(CURL*);
extern CURLcode (*curl_easy_setopt_s)(CURL*, CURLoption, ...);
extern void (*curl_easy_cleanup_s)(CURL*);

#undef curl_easy_perform
#undef curl_easy_setopt
#undef curl_easy_cleanup
#define curl_easy_perform(handle) CURLcode ccode; if (curl_easy_perform_s != NULL) ccode = curl_easy_perform_s(handle); else ccode = curl_easy_perform(handle);
#define curl_easy_setopt(handle,opt,param) if (curl_easy_setopt_s != NULL) curl_easy_setopt_s(handle,opt,param); else curl_easy_setopt(handle,opt,param);
#define curl_easy_cleanup(handle) if (curl_easy_cleanup_s != NULL) curl_easy_cleanup_s(handle); else curl_easy_cleanup(handle);

// Cria uma lista de cabeçalhos vazia
Headers* headers_new()
{
    Headers* self = malloc(sizeof *self);
    if (!self) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha de alocação ao criar lista de cabeçalhos vazia! [%s:%d]", __FILE__, __LINE__);
        return NULL;
    } else {
        self->list = NULL;
        return self;
    }
}

// Libera a memória de uma lista de cabeçalhos
void headers_free(Headers* self)
{
    if (!self) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha ao liberar uma lista de cabeçalhos nula");
    } else {
        curl_slist_free_all(self->list);
        free(self);
    }
}

// Callback que recebe os dados da resposta e escreve eles no buffer
static size_t request_callback(void* ptr, size_t size, size_t nmemb, void* data)
{
    int realsize = size * nmemb;
    Payload* mem = data;

    mem->data = realloc(mem->data, mem->size + realsize + 1);
    if (mem->data) {
        memcpy(mem->data + mem->size, ptr, realsize);
        mem->size += realsize;
        mem->data[mem->size] = 0;
    }
    return realsize;
}

// Adiciona o cabeçalho de autorização "Authorization: Basic base64encode(username:password)".
bool headers_add_basic_authorization(Headers* self, const char* username, const char* password)
{
    bool success = true;
    char* input = NULL;
    char* output = NULL;
    char* basic = NULL;
    trrlog(apm_facility, TRRLOG_DEBUG, "Adicionando cabeçalho de autorização básica");

    if (!self) {
        trrlog(apm_facility, TRRLOG_ERR, "A lista de cabeçalhos não pode ser nula");
        success = false;
        goto catch;
    }

    // Escreve o username:password em memória
    int input_len = asprintf(&input, "%s:%s", username, password);
    if (!input_len) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha ao concatenar as credenciais!");
        goto catch;
    }

    // Codifica o username:password em memória
    int output_len = 4 * (input_len / 3 + 1);
    output = calloc(1, 1 + output_len);
    if (!output) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha ao alocar o buffer para as credenciais!");
        goto catch;
    } else if (encode64(input, input_len, output, &output_len)) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha ao codificar as credenciais em base64!");
        goto catch;
    }

    // Completa o valor do cabeçalho em memória
    if (!asprintf(&basic, "Basic %s", output)) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha ao construir as credenciais!");
        goto catch;
    }

    // Adiciona o cabeçalho à lista
    if (!headers_add(self, "Authorization", basic)) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha ao adicionar o cabeçalho à lista existente");
        goto catch;
    }

    trrlog(apm_facility, TRRLOG_DEBUG, "Cabeçalho de autorização básica adicionado com sucesso");
    goto finally;

    catch : trrlog(apm_facility, TRRLOG_ERR, "Falha ao adicionar o cabeçalho de autorização básica!");
    success = false;

finally:
    free(basic);
    free(output);
    free(input);
    return success;
}

// Adiciona o cabeçalho de autorização "Authorization: Basic base64encode(username:password)".
bool headers_add_bearer_authorization(Headers* self, const char* token)
{
    bool success = true;
    char* bearer = NULL;
    trrlog(apm_facility, TRRLOG_DEBUG, "Adicionando cabeçalho de autorização bearer");

    if (!self) {
        trrlog(apm_facility, TRRLOG_ERR, "A lista de cabeçalhos não pode ser nula");
        success = false;
        goto catch;
    }

    // Completa o valor do cabeçalho em memória
    if (!asprintf(&bearer, "Bearer %s", token)) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha ao construir as credenciais!");
        goto catch;
    }

    // Adiciona o cabeçalho à lista
    if (!headers_add(self, "Authorization", bearer)) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha ao adicionar o cabeçalho à lista existente");
        goto catch;
    }

    trrlog(apm_facility, TRRLOG_DEBUG, "Cabeçalho de autorização bearer adicionado com sucesso");
    goto finally;

    catch : trrlog(apm_facility, TRRLOG_ERR, "Falha ao adicionar o cabeçalho de autorização bearer!");
    success = false;

finally:
    free(bearer);
    return success;
}

// Adiciona um cabeçalho genérico do tipo chave:valor à uma lista encadeada
bool headers_add(Headers* self, const char* name, const char* value)
{
    trrlog(apm_facility, TRRLOG_DEBUG, "Adicionando cabeçalho: \"%s: %s\"", name, value);
    bool success = true;

    // Formata o cabeçalho em memória
    char* header = NULL;
    if (!asprintf(&header, "%s: %s", name, value)) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha na construção do cabeçalho!");
        goto catch;
    }

    // Adiciona o cabeçalho à lista
    struct curl_slist* tmp = curl_slist_append(self->list, header);
    if (!tmp) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha na chamada da libcurl!");
        goto catch;
    }

    self->list = tmp;
    trrlog(apm_facility, TRRLOG_DEBUG, "Cabeçalho adicionado com sucesso");
    goto finally;

    catch : trrlog(apm_facility, TRRLOG_ERR, "Falha ao adicionar o cabeçalho!");
    success = false;

finally:
    free(header);
    return success;
}

// Realiza uma requisição HTTP à API REST do parceiro
RESTResponse* request(const char* op, const char* url, const char* payload, Headers* headers, int flags)
{
    trrlog(apm_facility, TRRLOG_DEBUG, "Enviando requisição HTTP %s para %s", op, url);
    CURL* curl;
    Payload from_server = { 0 };
    char* compressed_payload = NULL;
    size_t compressed_payload_size = 0;

    // Inicializa a libcurl
    curl = curl_easy_init();
    if (!curl) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha na inicialização da libcurl!");
        return NULL;
    }

    if (payload && (flags & REQUEST_COMPRESS)) {
        trrlog(apm_facility, TRRLOG_DEBUG, "Conteúdo será comprimido");
        compressed_payload = gzip_compress(payload, strlen(payload), &compressed_payload_size);
        headers_add(headers, "Content-Encoding", "gzip");
    }

    // Adiciona os cabeçalhos
    trrlog(apm_facility, TRRLOG_DEBUG, ">>>>>>>>>>>> headers");
    if (headers) {
        struct curl_slist* header = headers->list;
        char const* sensitive = "Authorization:";
        while (header) {
            if (!strncmp(header->data, sensitive, strlen(sensitive))) {
                trrlog(apm_facility, TRRLOG_DEBUG, "%s *****", sensitive);
            } else {
                trrlog(apm_facility, TRRLOG_DEBUG, "%s", header->data);
            }
            header = header->next;
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers->list);
    } else {
        trrlog(apm_facility, TRRLOG_DEBUG, "(sem cabeçalhos)");
    }

    // Seleciona o método e decide se adiciona a payload
    if (!strcmp(op, HTTP_POST)) {
        curl_easy_setopt(curl, CURLOPT_POST, 1);
    } else if (!strcmp(op, HTTP_PUT)) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, HTTP_PUT);
    } else if (!strcmp(op, HTTP_DELETE)) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, HTTP_DELETE);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
        payload = NULL;
    } else if (!strcmp(op, HTTP_GET)) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
        payload = NULL;
    }

    // Loga e adiciona a payload, caso necessário
    trrlog(apm_facility, TRRLOG_DEBUG, ">>>>>>>>>>>> body");
    if (compressed_payload) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, compressed_payload);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)compressed_payload_size);
        trrlog(apm_facility, TRRLOG_DEBUG, "(payload comprimido)");
    } else if (payload) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        trrlog(apm_facility, TRRLOG_DEBUG, "%s", payload);
    } else {
        trrlog(apm_facility, TRRLOG_DEBUG, "(sem corpo)");
    }
    trrlog(apm_facility, TRRLOG_DEBUG, "============");

    // Restante das configurações
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, request_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&from_server);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300);
    //curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // Descomente para imprimir informações na stderr
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    //curl_easy_setopt(curl, CURLOPT_STDERR, stderr);

    // Realiza a requisição
    curl_easy_perform(curl);

    //! não precisamos mais do payload comprimido
    free(compressed_payload);

    // Recupera o resultado
    long http_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    trrlog(apm_facility, TRRLOG_DEBUG, "%s", from_server.data);
    trrlog(apm_facility, TRRLOG_DEBUG, "<<<<<<<<<<<< resposta HTTP %ld", http_code);

    trrlog(apm_facility, TRRLOG_DEBUG, "<<curlcode %s", curl_easy_strerror(ccode));

    // Loga o valor de retorno da chamada à libcurl
    long code;
    curl_easy_getinfo(curl, CURLINFO_OS_ERRNO, &code);
    trrlog(apm_facility, TRRLOG_DEBUG, "Retorno da chamada curl: %ld", code);

    curl_easy_cleanup(curl);

    // Copia as informações retornadas para a estrutura apropriada
    RESTResponse* result = calloc(1, sizeof *result);
    if (!result) {
        trrlog(apm_facility, TRRLOG_ERR, "Falha de alocação!");
        free(from_server.data);
        return NULL;
    } else if (code == CURLE_OPERATION_TIMEDOUT) {
        trrlog(apm_facility, TRRLOG_ERR, "Timeout na requisição!");
        result->status = 1;
    } else {
        result->status = http_code;
        result->response = from_server.data;
    }
    return result;
}

// Destrói a estrutura de resposta da API REST
void rest_response_free(RESTResponse* result)
{
    if (result != NULL) {
        free(result->response);
        free(result);
    }
}

// Function to compress data using zlib
char* gzip_compress(const char* input, size_t input_size, size_t* output_size)
{
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    if (deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return NULL;
    }

    size_t max_compressed_size = compressBound(input_size);
    char* compressed_data = (char*)malloc(max_compressed_size);

    if (!compressed_data) {
        deflateEnd(&strm);
        return NULL;
    }

    strm.next_in = (Bytef*)input;
    strm.avail_in = input_size;
    strm.next_out = (Bytef*)compressed_data;
    strm.avail_out = max_compressed_size;

    if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
        free(compressed_data);
        deflateEnd(&strm);
        return NULL;
    }

    *output_size = max_compressed_size - strm.avail_out;
    deflateEnd(&strm);
    return compressed_data;
}