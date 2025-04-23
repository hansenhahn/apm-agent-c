#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trrapm/apm.h>
#include <trrapm/apm_internal.h>
#include <trrapm/apm_rest.h>
#include <trrlog1/trrlog.h>

typedef enum {
    POST_INTAKE_EVENT,
    POST_INTAKE_METRICS,
    GET_AZURE_CLOUD_METADATA,
} apm_endpoint_type_t;

typedef struct {
    apm_endpoint_type_t type;
    const char* url;
    const char* operation;
} apm_facade_t;

static apm_facade_t facade[] = {
    { .type = POST_INTAKE_EVENT, .url = "/intake/v2/events", .operation = HTTP_POST },
    { .type = POST_INTAKE_METRICS, .url = "/intake/v2/metrics", .operation = HTTP_POST },
    { .type = GET_AZURE_CLOUD_METADATA, .url = "/metadata/instance/compute?api-version=2019-08-15", .operation = HTTP_GET },
};

static RESTResponse* apm_request(apm_endpoint_type_t type, const char* base_url, const char* payload, Headers* headers, int flags);

static RESTResponse* apm_request(apm_endpoint_type_t type, const char* base_url, const char* payload, Headers* headers, int flags)
{
    for (int i = 0; i < sizeof(facade) / sizeof(apm_facade_t); i++) {
        if (facade[i].type == type) {
            char* url = build_url(base_url, facade[i].url);
            RESTResponse* resp = request(facade[i].operation, url, payload, headers, flags);
            free(url);
            return resp;
        }
    }
    return NULL;
}

void apm_create_intake_event_request(char* payload)
{
    apm_config_t* config = apm_get_config();
    Headers* headers = headers_new();

    headers_add_bearer_authorization(headers, config->token);
    headers_add(headers, "Content-Type", "application/x-ndjson");

    RESTResponse* resp = apm_request(POST_INTAKE_EVENT, config->url, payload, headers, REQUEST_COMPRESS);
    if (!resp || resp->status != 202) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro de comunicação.");
    }

    headers_free(headers);
    rest_response_free(resp);
}

void apm_create_intake_metrics_request(char* payload)
{
    apm_config_t* config = apm_get_config();
    Headers* headers = headers_new();

    headers_add_bearer_authorization(headers, config->token);
    headers_add(headers, "Content-Type", "application/x-ndjson");

    RESTResponse* resp = apm_request(POST_INTAKE_METRICS, config->url, payload, headers, REQUEST_COMPRESS);
    if (!resp || resp->status != 202) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro de comunicação.");
    }

    headers_free(headers);
    rest_response_free(resp);
}

char* apm_create_azure_cloud_metadata_request(void)
{
    char* payload = NULL;
    const char* url = "http://169.254.169.254";
    Headers* headers = headers_new();

    headers_add(headers, "Metadata", "true");
    RESTResponse* resp = apm_request(GET_AZURE_CLOUD_METADATA, url, NULL, headers, REQUEST_NO_FLAGS);
    if (!resp || resp->status != 200) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro de comunicação.");
        goto catch;
    }

    payload = strdup(resp->response);
    goto finally;
catch:
finally:
    headers_free(headers);
    rest_response_free(resp);
    return payload;
}
