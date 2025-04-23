#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <trrlog1/trrlog.h>
#include <unistd.h>

#include <trrapm/cJSON.h>

#define PROC_MAPS "/proc/self/maps"

extern int apm_facility;

const char* stackline_pattern = "^([^\\(]+)\\(([^\\+]*)\\+?0?x?([0-9a-fA-F]*)\\) \\[0x([0-9a-fA-F]+)\\]$";

typedef struct {
    char binary[512];
    char function[256];
    unsigned long offset;
    void* address;
} apm_stack_entry_t;

static int parse_stack_line(const char* line, apm_stack_entry_t* entry);
static int get_function_location(const char* exe_path, void* addr, char** function, char** location, char** lineno);

static unsigned long  get_base_address_of_library(const char* lib_name) {
    FILE* maps = fopen(PROC_MAPS, "r");
    if (!maps) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao abrir %s", PROC_MAPS);
        return 0;
    }

    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (lib_name[0] == '.') {
            lib_name++;
        }
        if (strstr(line, lib_name)) {
            unsigned long base;
            if (sscanf(line, "%lx-%*lx", &base) == 1) {
                fclose(maps);
                return base;
            }
        }
    }

    fclose(maps);
    return 0;
}

static int parse_stack_line(const char* line, apm_stack_entry_t* entry)
{
    regex_t regex;
    regmatch_t matches[5];
    char tmp[32] = {0};

    if (regcomp(&regex, stackline_pattern, REG_EXTENDED) != 0) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao compilar regex");
        return -1;
    }

    if (regexec(&regex, line, 5, matches, 0) != 0) {
        regfree(&regex);
        return -1;
    }

    int len = matches[1].rm_eo - matches[1].rm_so;
    strncpy(entry->binary, line+matches[1].rm_so, len);
    //! remover os comentários após o primeiro espaço
    char* args = strchr(entry->binary, ' ');
    if (args) {
        *args = '\0';
    }

    len = matches[2].rm_eo - matches[2].rm_so;
    strncpy(entry->function, line+matches[2].rm_so, len);

    len = matches[3].rm_eo - matches[3].rm_so;
    strncpy(tmp, line+matches[3].rm_so, len);
    entry->offset = strtoul(tmp, NULL, 16);

    len = matches[4].rm_eo - matches[4].rm_so;
    strncpy(tmp, line+matches[4].rm_so, len);
    entry->address = (void*)strtoull(tmp, NULL, 16);

    regfree(&regex);
    return 0;
}

static int get_function_location(const char* exe_path, void* addr, char** function, char** location, char** lineno)
{
    int ret = -1;
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "addr2line -e %s -f -p %p", exe_path, addr);

    FILE *fd = popen(cmd, "r");
    if (!fd) {
        return ret;
    }

    *function = NULL;
    *location = NULL;
    *lineno = NULL;

    char output[1024];
    char* function_str = NULL;
    char* location_str = NULL;
    char* lineno_str = NULL;
    if (fgets(output, sizeof(output), fd) != NULL) {
        char* at = strstr(output, " at ");
        if (at) {
            *at = '\0'; // Split string
            asprintf(&function_str, "%s", output);
            asprintf(&location_str, "%s", at + 4);
            location_str[strcspn(location_str, "\n")] = 0;

            //! xxxx.c:1234 (discriminator N)
            char* lineno_tmp = strchr(location_str, ':');
            if (lineno_tmp) {
                //! separamos em xxxx.c e 1234 (discriminator N)
                *lineno_tmp++ = '\0';
                lineno_str = lineno_tmp;
                lineno_tmp = strchr(location_str, ' ');
                if (lineno_tmp) {
                    //! ignoramos o que vier depois do número
                    *lineno_tmp = '\0';
                }
            }
            *function = strdup(function_str);
            *location = strdup(location_str);
            *lineno = strdup(lineno_str);
            ret = 0;
        }
    }

    free(function_str);
    free(location_str);

    fclose(fd);
    return ret;
}


int get_function_location_from_stack(const char* stack_line, char **binary, char** function, char** location, char** lineno)
{
    apm_stack_entry_t entry = {0};

    if (parse_stack_line(stack_line, &entry) != 0) {
        trrlog(apm_facility, TRRLOG_ERR, "Erro ao interpretar stackline %s", stack_line);
        return -1;
    }

    *binary = strdup(entry.binary);

    if (strstr(entry.binary, ".so")) {
        unsigned long base_address = get_base_address_of_library(entry.binary);
        get_function_location(entry.binary, (void*)((unsigned long)entry.address-base_address), function, location, lineno);
    }
    else {
        get_function_location(entry.binary, entry.address, function, location, lineno);
    }

    return 0;

}

int getfqdn(char** fqdn)
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        trrlog(apm_facility, TRRLOG_ERR, "Não foi possível ler o hostname.");
        return 1;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, NULL, &hints, &res) != 0) {
        trrlog(apm_facility, TRRLOG_ERR, "Não foi possível ler addrinfo para o hostname %s.", hostname);
        return 1;
    }

    char buff[NI_MAXHOST];
    if (getnameinfo(res->ai_addr, res->ai_addrlen, buff, sizeof(buff), NULL, 0, NI_NAMEREQD) != 0) {
        trrlog(apm_facility, TRRLOG_ERR, "Não foi possível ler nameinfo para o hostname %s.", hostname);
        freeaddrinfo(res);
        return 1;
    }

    *fqdn = strdup(buff);
    return 0;
}

char* build_url(const char* base_url, const char* method)
{
    trrlog(apm_facility, TRRLOG_DEBUG, "Construindo URL do endpoint...");
    char* url = NULL;

    if (!base_url || !method) {
        trrlog(apm_facility, TRRLOG_ERR, "URL incompleta!");
    } else {
        if (asprintf(&url, "%s%s", base_url, method)==-1) {
            trrlog(apm_facility, TRRLOG_ERR, "Erro ao construir URL.");
        }
        else {
            trrlog(apm_facility, TRRLOG_DEBUG, "URL construída: %s", url);
        }
    }

    return url;
}

char* get_json_string_or_empty(cJSON* root, const char* key)
{
    if (!root || !key) {
        return NULL;
    }

    cJSON* field = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(field) && (field->valuestring)) {
        return strdup(field->valuestring);
    } else {
        return strdup("");
    }
}

char* dup_value_or_default(const char* value, const char* def)
{
    if (value) {
        return strdup(value);
    }
    else if (def) {
        return strdup(def);
    }
    else {
        return strdup("");
    }
}

char* generate_id(int size)
{
    char* id = NULL;

    id = calloc(1, size + 1);
    if (id) {
        for (int i = 0; i < size; i += 2) {
            snprintf(&id[i], 3, "%02x", rand() % 255);
        }
    }

    return id;
}
