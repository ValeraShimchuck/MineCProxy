#ifndef CONFIG_SERIALIZER_H
#define CONFIG_SERIALIZER_H
#include <stdlib.h>
#define MAX_SERVERS 10
#define IP_SIZE 4

char* favicon_get();
void create_raw_response(char* resoponse);
int apply_response(char* raw_response, char* destination, int protocol_version);

struct config_data init_config();
struct server_data {
    char ip[IP_SIZE];
    short port;
    char name[32];
};

struct config_data {
    short port;
    char host[IP_SIZE];
    int max_players;
    char *motd;
    bool online_mode;
    struct server_data servers[10];
    int server_count;
};
#endif