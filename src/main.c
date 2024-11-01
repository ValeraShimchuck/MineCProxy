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

#define HOST_PORT 25567
#define BACKEND_PORT 25565
#define HOST_IP "0.0.0.0"
#define BACKEND_IP "0.0.0.0"
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

const char* PING_RESPONSE = "{\"version\":{\"name\":\"you are loh\",\"protocol\": 767},"
        "\"players\":{\"max\":100,\"online\":0,\"sample\":[]},"
        "\"description\":{\"text\":\"Minecraft Reverse Proxy\"},"
        "\"favicon\":\"data:image/png;base64,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\","
        "\"enforcesSecureChat\":false}";

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


void convertIp(char* strIp, char* ipBuffer) {
    if (inet_pton(AF_INET, HOST_IP, ipBuffer) != 1)  {
        printf("Could not convert ip %s", strIp);
        exit(-1);
    }
}

void bindSocket(int sockfd, char* strIp, int port) {
    char host_ip[4];
    //memset(host_ip, 0, 4);
    convertIp(strIp, host_ip);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = *host_ip;
    printf("host ip %s\n", strIp);
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

void println(char* str, ...) {
    va_list args;
    va_start(args, str);
    
    vprintf(str, args);
    printf("\n");  // Add newline at the end
    
    va_end(args);
}

void printArray(char* array, int size) {
    printf("[");
    for (int i = 0; i < size; i++) {
        printf("%02x ", (unsigned char) array[i]);
        if ((i + 1) % 32 == 0) {
            printf("\n");
        }
    }
    printf("]\n");
    printf("\n");
}

int main() {
    int socket = createSocket();
    server_socket = socket;
    signal(SIGINT, handle_sigint);
    bindSocket(socket, HOST_IP, HOST_PORT);
    listen(socket, 10);
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
                int protocol_version = readVarInt(&accamulated_buffer);
                int server_address_length = readVarInt(&accamulated_buffer);
                char server_address[server_address_length + 1];
                readBytes(&accamulated_buffer, server_address, server_address_length);
                server_address[server_address_length] = '\0';
                unsigned short server_port = readShort(&accamulated_buffer);
                int next_state = readVarInt(&accamulated_buffer);
                state = next_state;
            }
            else if (packet_id == 0 && state == 1) {
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