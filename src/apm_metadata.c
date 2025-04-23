#include <gnu/libc-version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <trrlog1/trrlog.h>
#include <unistd.h>

#include <trrapm/apm.h>
#include <trrapm/apm_internal.h>
#include <trrapm/cJSON.h>

apm_metadata_t* apm_new_metadata(void)
{
    apm_metadata_t* metadata = calloc(1, sizeof(apm_metadata_t));

    metadata->service = apm_new_service();
    metadata->process = apm_new_process();
    metadata->system = apm_new_system();
    metadata->cloud = apm_new_cloud();

    return metadata;
}

apm_service_t* apm_new_service(void)
{
    apm_service_t* service = calloc(1, sizeof(apm_service_t));
    apm_config_t* config = apm_get_config();

    service->name = strdup(config->name);
    service->environment = strdup(config->environment);

    service->agent_name = strdup("terra-apm-agent-c");
    service->agent_version = strdup("1.0.0");
    service->agent_activation_method = strdup("unknown");

    service->runtime_name = strdup("glibc");
    service->runtime_version = strdup(gnu_get_libc_version());

    service->language_name = strdup("C");
#ifdef __STDC_VERSION__
#if (__STDC_VERSION__ == 202000)
    service->language_version = strdup("23");
#elif (__STDC_VERSION__ == 201710L)
    service->language_version = strdup("17");
#elif (__STDC_VERSION__ == 201112L)
    service->language_version = strdup("11");
#elif (__STDC_VERSION__ == 199901L)
    service->language_version = strdup("99");
#endif
#else
    service->language_version = strdup("90");
#endif
    return service;
}

apm_process_t* apm_new_process(void)
{
    apm_process_t* process = calloc(1, sizeof(apm_process_t));

    process->pid = getpid();
    process->ppid = getppid();

    return process;
}

apm_system_t* apm_new_system(void)
{
    apm_system_t* system = calloc(1, sizeof(apm_system_t));

    struct utsname sysinfo;
    uname(&sysinfo);

    //! parsear /proc/self/cgroups para verificar se estamos rodando em um container (não é o caso, vamos só deixar vazio);
    system->container_id = strdup("");
    system->architecture = strdup(sysinfo.machine);
    system->platform = strdup(sysinfo.sysname);
    getfqdn(&system->detected_hostname);

    return system;
}

apm_cloud_t* apm_new_cloud(void)
{
    return apm_get_azure_cloud_metadata();
}

void apm_free_metadata(apm_metadata_t* metadata)
{
    apm_free_service(metadata->service);
    apm_free_process(metadata->process);
    apm_free_system(metadata->system);
    apm_free_cloud(metadata->cloud);

    free(metadata);
}

void apm_free_service(apm_service_t* service)
{
    free(service->name);
    free(service->environment);
    free(service->version);
    free(service->language_name);
    free(service->language_version);
    free(service->runtime_name);
    free(service->runtime_version);
    free(service->agent_name);
    free(service->agent_version);
    free(service->agent_activation_method);
    free(service);
}

void apm_free_process(apm_process_t* process)
{
    free(process);
}

void apm_free_system(apm_system_t* system)
{
    free(system->architecture);
    free(system->container_id);
    free(system->detected_hostname);
    free(system->platform);
    free(system);
}

void apm_free_cloud(apm_cloud_t* cloud)
{
    free(cloud->account_id);
    free(cloud->instance_id);
    free(cloud->instance_name);
    free(cloud->project_name);
    free(cloud->availability_zone);
    free(cloud->machine_type);
    free(cloud->provider);
    free(cloud->region);
    free(cloud);
}

void apm_dump_metadata(apm_metadata_t* metadata, char** buffer)
{
    //! vamos converter para json
    char* partial_buffer = apm_metadata_to_json(metadata);

    if (*buffer == NULL) {
        *buffer = calloc(1, strlen(partial_buffer) + 2);
    } else {
        *buffer = realloc(*buffer, strlen(*buffer) + strlen(partial_buffer) + 2);
    }

    strcat(*buffer, partial_buffer);
    strcat(*buffer, "\n");

    apm_free_metadata(metadata);
    free(partial_buffer);
}

char* apm_metadata_to_json(apm_metadata_t* metadata)
{
    //! vamos converter para json
    cJSON* json = cJSON_CreateObject();
    cJSON* fld_metadata = cJSON_AddObjectToObject(json, "metadata");

    //! Service metadata
    cJSON* fld_service = cJSON_AddObjectToObject(fld_metadata, "service");
    apm_service_t* service = metadata->service;
    cJSON_AddStringToObject(fld_service, "name", service->name);
    cJSON_AddStringToObject(fld_service, "environment", service->environment);
    cJSON_AddStringToObject(fld_service, "version", service->version);
    cJSON* fld_agent = cJSON_AddObjectToObject(fld_service, "agent");
    cJSON_AddStringToObject(fld_agent, "name", service->agent_name);
    cJSON_AddStringToObject(fld_agent, "version", service->agent_version);
    cJSON_AddStringToObject(fld_agent, "activation_method", service->agent_activation_method);
    cJSON* fld_lang = cJSON_AddObjectToObject(fld_service, "language");
    cJSON_AddStringToObject(fld_lang, "name", service->language_name);
    cJSON_AddStringToObject(fld_lang, "version", service->language_version);
    cJSON* fld_runtime = cJSON_AddObjectToObject(fld_service, "runtime");
    cJSON_AddStringToObject(fld_runtime, "name", service->runtime_name);
    cJSON_AddStringToObject(fld_runtime, "version", service->runtime_version);

    //! Process metadata
    cJSON* fld_process = cJSON_AddObjectToObject(fld_metadata, "process");
    apm_process_t* process = metadata->process;
    cJSON_AddNumberToObject(fld_process, "pid", process->pid);
    cJSON_AddNumberToObject(fld_process, "ppid", process->ppid);
    cJSON_AddStringToObject(fld_process, "title", NULL);

    //! System metadata
    cJSON* fld_system = cJSON_AddObjectToObject(fld_metadata, "system");
    apm_system_t* system = metadata->system;
    cJSON_AddStringToObject(fld_system, "detected_hostname", system->detected_hostname);
    cJSON_AddStringToObject(fld_system, "architecture", system->architecture);
    cJSON_AddStringToObject(fld_system, "platform", system->platform);
    if (system->container_id && system->container_id[0] != '\0') {
        cJSON* fld_container = cJSON_AddObjectToObject(fld_system, "container");
        cJSON_AddStringToObject(fld_container, "id", system->container_id);
    }

    //! Cloud metadata
    cJSON* fld_cloud = cJSON_AddObjectToObject(fld_metadata, "cloud");
    apm_cloud_t* cloud = metadata->cloud;
    cJSON_AddStringToObject(fld_cloud, "provider", cloud->provider);
    cJSON_AddStringToObject(fld_cloud, "region", cloud->region);
    if (cloud->availability_zone && cloud->availability_zone[0] != '\0') {
        cJSON_AddStringToObject(fld_cloud, "region", cloud->availability_zone);
    }
    cJSON* fld_account = cJSON_AddObjectToObject(fld_cloud, "account");
    cJSON_AddStringToObject(fld_account, "id", cloud->account_id);
    cJSON* fld_instance = cJSON_AddObjectToObject(fld_cloud, "instance");
    cJSON_AddStringToObject(fld_instance, "id", cloud->instance_id);
    cJSON_AddStringToObject(fld_instance, "name", cloud->instance_name);
    cJSON* fld_project = cJSON_AddObjectToObject(fld_cloud, "project");
    cJSON_AddStringToObject(fld_project, "name", cloud->project_name);
    cJSON* fld_machine = cJSON_AddObjectToObject(fld_cloud, "machine");
    cJSON_AddStringToObject(fld_machine, "type", cloud->machine_type);

    char* payload = cJSON_PrintUnformatted(json);

    cJSON_Delete(json);

    return payload;
}