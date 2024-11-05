#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include "utilities.h"
#include "config_serializer.h"

#define HOST_PORT 25567
#define BACKEND_PORT 25565
#define HOST_IP "0.0.0.0"
#define BACKEND_IP "0.0.0.0"
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

const int SEG_BITS = 0x7F;
const int CONTINUE_BIT = 0x80;

int server_socket = -1;
int client_socket = -1;


void handle_sigint(int sig) {
    printf("\nReceived SIGINT (Ctrl+C). Cleaning up...\n");
    
    if (client_socket != -1) {
        shutdown(client_socket, SHUT_RDWR);
        close(client_socket);
        printf("Closed client socket.\n");
    }

    if (server_socket != -1) {
        close(server_socket);
        printf("Closed server socket.\n");
    }

    exit(0);  
}

int createSocket() {
    int socket_id = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket_id < 0) {
        printf("We are fucked! Could not create a socket");
        return -1;
    }
    if (setsockopt(socket_id, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        printf("Could not set socket options");
        return -1;
    };
    return socket_id;
}

void bindSocket(int sockfd, char host_ip[4], int port) {

    char strip[16];
    string_to_ip(HOST_IP, strip);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = *host_ip;
    printf("host ip %s\n", strip);
    printf("host ip %d\n", (int) *host_ip);
    if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("Could not bind socket");
        close(sockfd);
        exit(-1);
    }
}


typedef struct {
    char* buffer;
    int size;
    int read_index;
    int write_index;
} buffer;

buffer createBuffer(int size) {
    return (buffer) {
        .buffer = malloc(size),
        .size = size,
        .read_index = 0,
        .write_index = 0
    };
}

int getBufferSize(buffer* buf) {
    return buf->write_index - buf->read_index;
}

void freeBuffer(buffer* buf) {
    free(buf->buffer);
}

char readByte(buffer* buf) {

    if (buf->read_index >= buf->write_index) {
        printf("Buffer overflow %d", buf->read_index);
        exit(-1);
    }
    char c = buf->buffer[buf->read_index];
    (*buf).read_index++;
    return c;
}

void readBytes(buffer* buf, char* buffer, int size) {
    if (buf->read_index + size > buf->write_index) {
        printf("Buffer overflow %d size: %d", buf->read_index, size);
        exit(-1);
    }
    memcpy(buffer, buf->buffer + buf->read_index, size);
    buf->read_index += size;
}

bool canRead(buffer* buf) {
    return buf->read_index < buf->write_index;
}

int tryReadVarInt(buffer* buf) {
    int value = 0;
    int shift = 0;
    int reader_index = buf->read_index;
    while (true) {
        if (canRead(buf) == false) {
            buf->read_index = reader_index;
            return -1;
        }
        char c = readByte(buf);
        value |= (c & SEG_BITS) << shift;
        if ((c & CONTINUE_BIT) == 0) {
            break;
        }
        shift += 7;
        if (shift > 32) {
            printf("VarInt overflow");
            exit(-1);
        }
    }
    return value;
}

void writeBytes(buffer* buf, char* buffer, int size) {
    if (buf->write_index + size > buf->size) {
        printf("Buffer overflow %d size: %d", buf->write_index, size);
        exit(-1);
    }
    memcpy(buf->buffer + buf->write_index, buffer, size);
    buf->write_index += size;
}

void writeBuffer(buffer* dst, buffer* src, int size) {
    int bytes_to_write = getBufferSize(src);
    bytes_to_write = MIN(bytes_to_write, dst->size - dst->write_index);
    bytes_to_write = MIN(bytes_to_write, size);
    writeBytes(dst, src->buffer + src->read_index, bytes_to_write);
    src->read_index += bytes_to_write;
    //dst->write_index += bytes_to_write;
}

void writeByte(buffer* buf, char c) {
    writeBytes(buf, &c, 1);
}

int readVarInt(buffer* buf) {
    int value = 0;
    int shift = 0;
    while (true) {
        char c = readByte(buf);
        value |= (c & SEG_BITS) << shift;
        if ((c & CONTINUE_BIT) == 0) {
            break;
        }
        shift += 7;
        if (shift > 32) {
            printf("VarInt overflow");
            exit(-1);
        }
    }
    return value;
}

void writeVarInt(buffer* buf, int value) {
    while (true)
    {
        if ((value & ~SEG_BITS) == 0) {
            writeByte(buf, value);
            break;
        }
        writeByte(buf, (value & SEG_BITS) | CONTINUE_BIT);
        value >>= 7;
    }
    
}

short readShort(buffer* buf) {
    short value = 0;
    readBytes(buf, &value, 2);
    return ntohs(value);
}

uint64_t htonll(uint64_t value) {
    return ((uint64_t) htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
}

uint64_t ntohll(uint64_t value) {
    return ((uint64_t) ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
}

long long readLong(buffer* buf) {
    printArray(buf->buffer, buf->write_index);
    long long value = 0;
    readBytes(buf, &value, 8);
    value = ntohll(value);
    return value;
}


void writeLong(struct buffer* buf, long long value) {
    value = htonll(value);
    writeBytes(buf, &value, 8);
}

int main() {
    struct config_data config = init_config();
    println("Starting server on %s:%d", config.host, config.port);
    int socket = createSocket();
    server_socket = socket;
    signal(SIGINT, handle_sigint);
    bindSocket(socket, config.host, config.port);
    listen(socket, 10);
    int protocol_version = 0;
    while (/* condition */ true)
    {
        int client_fd = accept(socket, 0, 0);
    client_socket = client_fd;
    buffer accamulated_buffer = createBuffer(100000);
    char rcv_buffer[1024] = {0};
    buffer read_buf = {
        .buffer = rcv_buffer,
        .size = 1024,
        .read_index = 0,
        .write_index = 1024
    };
    int state = 0;
    int read;
    while ((read = recv(client_fd, rcv_buffer, 1024, 0)) >= 0)
    {
        read_buf.read_index = 0;
        read_buf.write_index = read;
        while (true)
        {
            int read_index = accamulated_buffer.read_index;
            int packet_length = tryReadVarInt(&accamulated_buffer);
            if (packet_length == -1) {
                if (!canRead(&read_buf)) {
                    break;
                }
                writeByte(&accamulated_buffer, readByte(&read_buf));
                continue;
            }
            writeBuffer(&accamulated_buffer, &read_buf, packet_length);
            if (packet_length > getBufferSize(&accamulated_buffer)) {
                accamulated_buffer.read_index = read_index;
                break;
            }
            int packet_id = readVarInt(&accamulated_buffer);
            if (packet_id == 0 && state == 0) {
                protocol_version = readVarInt(&accamulated_buffer);
                int server_address_length = readVarInt(&accamulated_buffer);
                char server_address[server_address_length + 1];
                readBytes(&accamulated_buffer, server_address, server_address_length);
                server_address[server_address_length] = '\0';
                unsigned short server_port = readShort(&accamulated_buffer);
                int next_state = readVarInt(&accamulated_buffer);
                state = next_state;
            }
            else if (packet_id == 0 && state == 1) {
                char* PING_RESPONSE = create_response(protocol_version);
                buffer send_buffer1 = createBuffer(20000);
                buffer send_buffer2 = createBuffer(20000);
                writeVarInt(&send_buffer1, 0);
                writeVarInt(&send_buffer1, strlen(PING_RESPONSE));
                writeBytes(&send_buffer1, PING_RESPONSE, strlen(PING_RESPONSE));
                writeVarInt(&send_buffer2, getBufferSize(&send_buffer1));
                writeBuffer(&send_buffer2, &send_buffer1, getBufferSize(&send_buffer1));
                send(client_fd, send_buffer2.buffer, getBufferSize(&send_buffer2), 0);
                freeBuffer(&send_buffer2);
                freeBuffer(&send_buffer1);
                free(PING_RESPONSE);

            } else if (packet_id == 1 && state == 1) {
                long long payload = readLong(&accamulated_buffer);
                buffer send_buffer1 = createBuffer(20);
                writeVarInt(&send_buffer1, 1);
                writeLong(&send_buffer1, payload);
                buffer send_buffer2 = createBuffer(20);
                writeVarInt(&send_buffer2, getBufferSize(&send_buffer1));
                writeBuffer(&send_buffer2, &send_buffer1, getBufferSize(&send_buffer1));
                send(client_fd, send_buffer2.buffer, getBufferSize(&send_buffer2), 0);
                freeBuffer(&send_buffer2);
                freeBuffer(&send_buffer1);
                shutdown(client_fd, SHUT_RDWR);
                close(client_fd);

            }
            accamulated_buffer.read_index = 0;
            accamulated_buffer.write_index = 0;
        }
    }
    }
    if (client_socket != -1) {
        close(client_socket);
    }
    close(socket);
    return 0;
}