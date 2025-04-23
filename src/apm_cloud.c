#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trrlog1/trrlog.h>

#include <trrapm/apm.h>
#include <trrapm/apm_internal.h>
#include <trrapm/cJSON.h>

apm_cloud_t* apm_get_azure_cloud_metadata(void)
{
    char *payload = NULL;
    apm_cloud_t* cloud = calloc(1, sizeof(apm_cloud_t));
    if (cloud == NULL) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao alocar estrutura.");
        goto finally;
    }

    payload = apm_create_azure_cloud_metadata_request();
    if (payload == NULL) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao ler informações da Azure.");
        goto catch;
    }

    cJSON* root = cJSON_Parse(payload);
    if (root == NULL) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao ler json.");
        goto catch;
    }

    cloud->account_id = get_json_string_or_empty(root, "subscriptionId");
    cloud->instance_id = get_json_string_or_empty(root, "vmId");
    cloud->instance_name = get_json_string_or_empty(root, "name");
    cloud->project_name = get_json_string_or_empty(root, "resourceGroupName");
    cloud->availability_zone = get_json_string_or_empty(root, "zone");
    cloud->machine_type = get_json_string_or_empty(root, "vmSize");
    cloud->provider = strdup("azure");
    cloud->region = get_json_string_or_empty(root, "location");

    cJSON_Delete(root);
    goto finally;
catch :
    //! implementar fallback
    cloud->provider = strdup("azure");
finally :
    free(payload);
    return cloud;
}

apm_cloud_t* apm_get_gcp_cloud_metadata(void)
{
    apm_cloud_t* cloud = calloc(1, sizeof(apm_cloud_t));
    return cloud;
}

apm_cloud_t* apm_get_aws_cloud_metadata(void)
{
    apm_cloud_t* cloud = calloc(1, sizeof(apm_cloud_t));
    return cloud;
}