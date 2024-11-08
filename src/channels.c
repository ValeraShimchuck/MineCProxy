#include "channels.h"
#include <stdio.h>
#include <stdlib.h>
#include "buffer.h"
#include <sys/poll.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include "config_serializer.h"


int last_channel_id = -1;
int channels_max_size = -1;
int channels_size = -1;
channel_context* channels = NULL;
char* raw_ping_response = NULL;


int init_channels(int max_channels) {
    channels_max_size = max_channels;
    channels_size = 0;
    channels = malloc(channels_max_size * sizeof(channel_context));
    if (channels == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return -1;
    }
    raw_ping_response = malloc(20480);
    create_raw_response(raw_ping_response);
    return 0;
}


int create_channel(int max_connections) {
    if (channels_size == channels_max_size) {
        fprintf(stderr, "No more channels available\n");
        return -1;
    }
    last_channel_id++;
    channel_context *c = &channels[last_channel_id];
    c->max_connections = max_connections;
    c->current_connections = 0;
    c->connections = malloc(max_connections * sizeof(channel_context));
    for (int i = 0; i < max_connections; i++) {
        c->connections[i] = (client_context) {
            .fd = -1,
             .buf = NULL
        };
    }
    channels_size++;
    pipe(c->sync_pipes);
    fcntl(c->sync_pipes[0], F_SETFL, O_NONBLOCK);
    fcntl(c->sync_pipes[1], F_SETFL, O_NONBLOCK);



    return last_channel_id;
}


int add_connection(int channel_id, int client_fd) {
    if (channel_id > last_channel_id) {
        fprintf(stderr, "Invalid channel id %d\n", channel_id);
        return -1;
    }
    if (channels[channel_id].current_connections >= channels[channel_id].max_connections) {
        fprintf(stderr, "Channel %d is full. Max connections %d. Current connections %d\n", channel_id, channels[channel_id].max_connections, channels[channel_id].current_connections);
        return -2;
    }
    int suitable_connection_id = -1;
    for (int i = 0; i < channels[channel_id].max_connections; i++) {
        client_context c = channels[channel_id].connections[i];
        if (c.fd < 0) {
            suitable_connection_id = i;
            break;
        }
    }
    if (suitable_connection_id == -1) {
        fprintf(stderr, "Channel %d is full\n", channel_id);
        return -2;
    }
    channel_context* ch = &channels[channel_id];
    ch->connections[suitable_connection_id] = (client_context) {
        .fd = client_fd,
        .buf = malloc(sizeof(buffer))
    };
    buffer* buf = ch->connections[suitable_connection_id].buf;
    *buf = createBuffer(100000);
    channels[channel_id].current_connections++;
    return 0;
}

int find_suitable_channel() {
    for (int i = 0; i < channels_size; i++) {
        if (channels[i].current_connections < channels[i].max_connections) {
            return i;
        }
    }
    return -1;
}

int wake_up_channel(int channel_id) {
    if (channel_id > last_channel_id) {
        fprintf(stderr, "Invalid channel id %d\n", channel_id);
        return -1;
    }
    channel_context* ch = &channels[channel_id];
    printf("Waking up channel %d\n", channel_id);
    write(ch->sync_pipes[1], "wake_up", 8);
    return 0;
}

void disconnect_client(int channel_id, int fd) {
    if (channel_id > last_channel_id) {
        fprintf(stderr, "Invalid channel id %d\n", channel_id);
        return;
    }
    for (int i = 0; i < channels[channel_id].max_connections; i++) {
        if (channels[channel_id].connections[i].fd == fd) {
            shutdown(fd, SHUT_RDWR);
            close(fd);
            client_context* c = &channels[channel_id].connections[i];
            if (c->buf != NULL) {
                freeBuffer(c->buf);
                c->buf = NULL;
                c->state = 0;
                c->protocol_version = 0;
            }
            channels[channel_id].connections[i] = (client_context) {
                .fd = -1,
                .buf = NULL
            };
            channels[channel_id].current_connections--;
            break;
        }
    }
}

void start_channel(int channel_id) {
    printf("Starting channel %d\n", channel_id);
    channel_context* ch = &channels[channel_id];
    struct pollfd pfds[ch->max_connections + 1];
    pfds[ch->max_connections].fd = ch->sync_pipes[0];
    pfds[ch->max_connections].events = POLLIN;
    while (channels != NULL) {
        for (int i = 0; i < ch->max_connections; i++) {
            if (ch->connections[i].fd < 0) {
                pfds[i].fd = -1;
                pfds[i].events = 0;
                continue;
            }
            pfds[i].fd = ch->connections[i].fd;
            pfds[i].events = POLLIN;
        }
        int poll_res = poll(pfds, ch->max_connections + 1, 20000); // TODO switch to epoll_wait
        printf("hearbeat\n");
        if (poll_res == -1) {
            fprintf(stderr, "Poll failed\n");
            return;
        }
        char rcv_buffer[1024] = {0};
        buffer read_buf = {
            .buffer = rcv_buffer,
            .size = 1024,
            .read_index = 0,
            .write_index = 1024
        };
        if (pfds[ch->max_connections].revents & POLLIN) {
            int read_bytes;
            while ((read_bytes = read(pfds[ch->max_connections].fd, rcv_buffer, 1024)) > 0) { }
            if (read_bytes < 0) {
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                fprintf(stderr, "Pipe read error %d\n", errno);
                // TODO think about channel shutdown
                return;
                }
            }
        }
        for (int i = 0; i < ch->max_connections; i++) {
            if (pfds[i].fd < 0) {
                continue;
            }
            if (pfds[i].revents & POLLIN) {
                int read;
                client_context* c = &ch->connections[i];
                int client_fd = c->fd;
                buffer* buf = c->buf;
                while ((read = recv(client_fd, rcv_buffer, read_buf.size, MSG_DONTWAIT)) >= 0) {
                    if (read < 0) {
                        if (!(errno == EWOULDBLOCK || errno == EAGAIN)) {
                            fprintf(stderr, "Socket read error %d\n", errno);
                            disconnect_client(channel_id, client_fd);
                        } 
                        break;
                    }
                    //if (read ) 
                    read_buf.read_index = 0;
                    read_buf.write_index = read;
                    while (true) {
                        int read_index = buf->read_index;
                        int packet_length = tryReadVarInt(buf);
                        if (packet_length == -1) {
                            if (!canRead(&read_buf)) {
                                break;
                            }
                            writeByte(buf, readByte(&read_buf));
                            continue;
                        }
                        writeBuffer(buf, &read_buf, packet_length);
                        if (packet_length > getBufferSize(buf)) {
                            buf->read_index = read_index;
                            break;
                        }
                        int packet_id = readVarInt(buf);
                        int state = c->state;
                        if (packet_id == 0 && state == 0) {
                            c->protocol_version = readVarInt(buf);
                            int server_address_length = readVarInt(buf);
                            char server_address[server_address_length + 1];
                            readBytes(buf, server_address, server_address_length);
                            server_address[server_address_length] = '\0';
                            unsigned short server_port = readShort(buf);
                            int next_state = readVarInt(buf);
                            c->state = next_state;
                        } else if (packet_id == 0 && state == 1) {
                            char* PING_RESPONSE = malloc(20480);
                            int ping_response_length = apply_response(raw_ping_response, PING_RESPONSE, c->protocol_version);
                            buffer send_buffer1 = createBuffer(20000);
                            buffer send_buffer2 = createBuffer(20000);
                            writeVarInt(&send_buffer1, 0);
                            writeVarInt(&send_buffer1, ping_response_length);
                            writeBytes(&send_buffer1, PING_RESPONSE, ping_response_length);
                            writeVarInt(&send_buffer2, getBufferSize(&send_buffer1));
                            writeBuffer(&send_buffer2, &send_buffer1, getBufferSize(&send_buffer1));
                            send(client_fd, send_buffer2.buffer, getBufferSize(&send_buffer2), 0);
                            freeBuffer(&send_buffer2);
                            freeBuffer(&send_buffer1);
                            free(PING_RESPONSE);
                        } else if (packet_id == 1 && state == 1) {
                            long long payload = readLong(buf);
                            buffer send_buffer1 = createBuffer(20);
                            writeVarInt(&send_buffer1, 1);
                            writeLong(&send_buffer1, payload);
                            buffer send_buffer2 = createBuffer(20);
                            writeVarInt(&send_buffer2, getBufferSize(&send_buffer1));
                            writeBuffer(&send_buffer2, &send_buffer1, getBufferSize(&send_buffer1));
                            send(client_fd, send_buffer2.buffer, getBufferSize(&send_buffer2), 0);
                            freeBuffer(&send_buffer2);
                            freeBuffer(&send_buffer1);
                            disconnect_client(channel_id, client_fd);
                            break;
                        }
                        buf->read_index = 0;
                        buf->write_index = 0;
                    }

                }
            }
        }

    }
}