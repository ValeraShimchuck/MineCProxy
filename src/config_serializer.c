#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include "config_serializer.h"
#include "utilities.h"
#include <string.h>


char* favicon_get() {
    const char* filepath = "./favicons/fv.png";
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        perror("Could not open file");
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    unsigned char* buffer = malloc(file_size);
    if (!buffer) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }
    fread(buffer, 1, file_size, file);
    fclose(file);

    size_t output_length;
    char* encoded_data = base64_encode(buffer, file_size, &output_length);

    free(buffer);
    return encoded_data;
}



void create_raw_response(char* resoponse) {
    char* favicon_data = favicon_get();
    if (favicon_data != NULL) {
        size_t max_data_length = 20480 - 256;
        favicon_data[max_data_length - 1] = '\0';
        snprintf(resoponse, 20480,
                 "{\"version\":{\"name\":\"1.8 - 1.21.x\",\"protocol\":%s},"
                 "\"players\":{\"max\":100,\"online\":0,\"sample\":[]},"
                 "\"description\":{\"text\":\"Minecraft Reverse Proxy\"},"
                 "\"favicon\":\"data:image/png;base64,%s\","
                 "\"enforcesSecureChat\":false}", 
                 "%d", favicon_data);
                

    } 
    free(favicon_data);
}
int apply_response(char* raw_response, char* destination, int protocol_version) {
    return snprintf(destination, 20480, raw_response, protocol_version);
}

//void create_response(char* resoponse, int protocol_version) {
//    char* favicon_data = favicon_get();
//    println(favicon_data);
//    char* formatted_response = malloc(20480);
//    if (formatted_response == NULL) {
//        fprintf(stderr, "Memory allocation failed\n");
//        return NULL;
//    }
//
//    if (favicon_data != NULL) {
//        size_t max_data_length = 20480 - 256;
//        favicon_data[max_data_length - 1] = '\0';
//
//        snprintf(resoponse, 20480,
//                 "{\"version\":{\"name\":\"1.8 - 1.21.x\",\"protocol\":%d},"
//                 "\"players\":{\"max\":100,\"online\":0,\"sample\":[]},"
//                 "\"description\":{\"text\":\"Minecraft Reverse Proxy\"},"
//                 "\"favicon\":\"data:image/png;base64,%s\","
//                 "\"enforcesSecureChat\":false}", 
//                 protocol_version, favicon_data);
//    } else {
//        snprintf(formatted_response, 20480,
//                 "{\"version\":{\"name\":\"1.8 - 1.21.x\",\"protocol\":%d},"
//                 "\"players\":{\"max\":100,\"online\":0,\"sample\":[]},"
//                 "\"description\":{\"text\":\"Minecraft Reverse Proxy\"},"
//                 "\"favicon\":\"\","
//                 "\"enforcesSecureChat\":false}", protocol_version);
//    }
//
//    return formatted_response;
//}

struct config_data config = {
    .port = 25565,
    .host = {127, 0, 0, 1},  // Default localhost in binary form
    .max_players = 100,
    .motd = "Minecraft Reverse Proxy",
    .online_mode = false,
    .server_count = 0
};


bool is_config_valid(const struct config_data *cfg) {
    if (cfg->port <= 0 || cfg->port > 65535) return false;
    if (cfg->max_players < 1 || cfg->max_players > 1000) return false;
    if (cfg->motd == NULL || strlen(cfg->motd) == 0) return false;
    for (int i = 0; i < cfg->server_count; ++i) {
        if (cfg->servers[i].port <= 0 || cfg->servers[i].port > 65535) return false;
    }
    return true;
}

void write_default_config() {
    FILE *fptr = fopen("config.yml", "w");
    if (fptr) {
        fprintf(fptr, "port: %d\n", config.port);
        fprintf(fptr, "host: 127.0.0.1\n");
        fprintf(fptr, "max_players: %d\n", config.max_players);
        fprintf(fptr, "motd: \"%s\"\n", config.motd);
        fprintf(fptr, "online_mode: %s\n", config.online_mode ? "true" : "false");
        fprintf(fptr, "servers:\n");
        for (int i = 0; i < config.server_count; ++i) {
            char ip_str[INET_ADDRSTRLEN];
            ip_to_string(config.servers[i].ip, ip_str);
            fprintf(fptr, "  - \"%s:%d\" %s\n", ip_str, config.servers[i].port, config.servers[i].name);
        }
        fclose(fptr);
    }
}

void parse_line(char *line) {
    char key[50], value[50];
    if (sscanf(line, "%49[^:]: %49[^\n]", key, value) == 2) {
        if (strcmp(key, "port") == 0) config.port = (short)atoi(value);
        else if (strcmp(key, "host") == 0) string_to_ip(value, config.host);
        else if (strcmp(key, "max_players") == 0) config.max_players = atoi(value);
        else if (strcmp(key, "motd") == 0) {
            config.motd = strdup(value + 1);
            config.motd[strlen(config.motd) - 1] = '\0';
        } else if (strcmp(key, "online_mode") == 0) config.online_mode = (strcmp(value, "true") == 0);
    } else if (sscanf(line, " - \"%49[^\"]\" %49s", value, key) == 2) {
        if (config.server_count < MAX_SERVERS) {
            char *ip_port = strtok(value, ":");
            char *port_str = strtok(NULL, ":");
            string_to_ip(ip_port, config.servers[config.server_count].ip);
            config.servers[config.server_count].port = (short)atoi(port_str);
            strncpy(config.servers[config.server_count].name, key, sizeof(config.servers[config.server_count].name) - 1);
            config.server_count++;
        }
    }
}

struct config_data init_config() {
    FILE *fptr = fopen("config.yml", "r");
    if (fptr) {
        char line[128];
        while (fgets(line, sizeof(line), fptr)) {
            parse_line(line);
        }
        fclose(fptr);
    } else {
        write_default_config();
    }

    if (!is_config_valid(&config)) {
        printf("Invalid config found. Using default values.\n");
        config = (struct config_data){
            .port = 25565,
            .host = {127, 0, 0, 1},
            .max_players = 100,
            .motd = "Minecraft Reverse Proxy",
            .online_mode = false,
            .server_count = 0
        };
        write_default_config();
    }

    return config;
}