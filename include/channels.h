#ifndef CHANNELS_H
#define CHANNELS_H
#include <unistd.h>
#include "buffer.h"

typedef struct {
    int fd;
    int state;
    int protocol_version;
    buffer* buf;
} client_context;

typedef struct {
    int max_connections; // max available connections ReadOnly
    int current_connections; // read only if being used out of the channel
    client_context* connections;
    int sync_pipes[2];
} channel_context;


// returns 0 on success
int init_channels(int max_channels);
// returns chnnel id
int create_channel(int max_connections);

void start_channel(int channel_id);

// returns 0 on success
int wake_up_channel(int channel_id);

// returns 0 on success
int add_connection(int channel_id, int client_fd);

// seeks for a suitable channel. -1 if there is no suitable channel. Otherwise returns channel's id
int find_suitable_channel();
#endif // CHANNELS_H