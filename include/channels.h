#ifndef CHANNELS_H
#define CHANNELS_H
#include <unistd.h>
#include "buffer.h"
#include <uuid/uuid.h>
#include <pthread.h>


typedef struct {
    int client_fd;
    int backend_fd;
    pthread_mutex_t  lock;
} connection_relation;

typedef struct {
    int fd;
    int state;
    int protocol_version;
    buffer* buf;
    connection_relation* relation;
    int nicklength;
    char* nick;
    uuid_t uuid;
} connection_context;



typedef struct {
    int connection_type; // 0 for client, 1 for backend
    int max_connections; // max available connections ReadOnly
    int current_connections; // read only if being used out of the channel
    connection_context* connections;
    int sync_pipes[2];
} channel_context;


// returns 0 on success
int init_channels(int max_channels);
// returns chnnel id
int create_channel(int max_connections, int type);

void start_channel_to_client(int channel_id);
void start_channel_to_backend(int channel_id);

// returns 0 on success
int wake_up_channel(int channel_id);

// returns 0 on success
int add_connection(int channel_id, int client_fd);

// seeks for a suitable channel. -1 if there is no suitable channel. Otherwise returns channel's id
int find_suitable_channel(int type);
#endif // CHANNELS_H