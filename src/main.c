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
#include "buffer.h"
#include "channels.h"

#define HOST_PORT 25567
#define BACKEND_PORT 25565
#define HOST_IP "0.0.0.0"
#define BACKEND_IP "0.0.0.0"



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

int main() {
    struct config_data config = init_config();
    println("Starting server on %s:%d", config.host, config.port);
    int socket = createSocket();
    server_socket = socket;
    char* raw_response = malloc(20480);
    create_raw_response(raw_response);
    signal(SIGINT, handle_sigint);
    bindSocket(socket, config.host, config.port);
    listen(socket, 10);
    init_channels(1);
    create_channel(100);
    pthread_t thread_id;
    printf("Creating thread\n");
    pthread_create(&thread_id, NULL, (void *) start_channel, (void *) {0});
    while (true ) {
        int client_fd = accept(socket, 0, 0);
        printf("Accepted connection %d\n", client_fd);
        add_connection(0, client_fd);
        wake_up_channel(0);
    }
    //int protocol_version = 0;
    //while (/* condition */ true)
    //{
    //    int client_fd = accept(socket, 0, 0);
    //client_socket = client_fd;
    //buffer accamulated_buffer = createBuffer(100000);
    //char rcv_buffer[1024] = {0};
    //buffer read_buf = {
    //    .buffer = rcv_buffer,
    //    .size = 1024,
    //    .read_index = 0,
    //    .write_index = 1024
    //};
    //int state = 0;
    //int read;
    //while ((read = recv(client_fd, rcv_buffer, 1024, 0)) >= 0)
    //{
    //    read_buf.read_index = 0;
    //    read_buf.write_index = read;
    //    while (true)
    //    {
    //        int read_index = accamulated_buffer.read_index;
    //        int packet_length = tryReadVarInt(&accamulated_buffer);
    //        if (packet_length == -1) {
    //            if (!canRead(&read_buf)) {
    //                break;
    //            }
    //            writeByte(&accamulated_buffer, readByte(&read_buf));
    //            continue;
    //        }
    //        writeBuffer(&accamulated_buffer, &read_buf, packet_length);
    //        if (packet_length > getBufferSize(&accamulated_buffer)) {
    //            accamulated_buffer.read_index = read_index;
    //            break;
    //        }
    //        int packet_id = readVarInt(&accamulated_buffer);
    //        if (packet_id == 0 && state == 0) {
    //            protocol_version = readVarInt(&accamulated_buffer);
    //            int server_address_length = readVarInt(&accamulated_buffer);
    //            char server_address[server_address_length + 1];
    //            readBytes(&accamulated_buffer, server_address, server_address_length);
    //            server_address[server_address_length] = '\0';
    //            unsigned short server_port = readShort(&accamulated_buffer);
    //            int next_state = readVarInt(&accamulated_buffer);
    //            state = next_state;
    //        }
    //        else if (packet_id == 0 && state == 1) {
    //            char* PING_RESPONSE = malloc(20480);
    //            int ping_response_length = apply_response(raw_response, PING_RESPONSE, protocol_version);
    //            buffer send_buffer1 = createBuffer(20000);
    //            buffer send_buffer2 = createBuffer(20000);
    //            writeVarInt(&send_buffer1, 0);
    //            writeVarInt(&send_buffer1, ping_response_length);
    //            writeBytes(&send_buffer1, PING_RESPONSE, ping_response_length);
    //            writeVarInt(&send_buffer2, getBufferSize(&send_buffer1));
    //            writeBuffer(&send_buffer2, &send_buffer1, getBufferSize(&send_buffer1));
    //            send(client_fd, send_buffer2.buffer, getBufferSize(&send_buffer2), 0);
    //            freeBuffer(&send_buffer2);
    //            freeBuffer(&send_buffer1);
                //free(PING_RESPONSE);
//
    //        } else if (packet_id == 1 && state == 1) {
    //            long long payload = readLong(&accamulated_buffer);
    //            buffer send_buffer1 = createBuffer(20);
    //            writeVarInt(&send_buffer1, 1);
    //            writeLong(&send_buffer1, payload);
    //            buffer send_buffer2 = createBuffer(20);
    //            writeVarInt(&send_buffer2, getBufferSize(&send_buffer1));
    //            writeBuffer(&send_buffer2, &send_buffer1, getBufferSize(&send_buffer1));
    //            send(client_fd, send_buffer2.buffer, getBufferSize(&send_buffer2), 0);
    //            freeBuffer(&send_buffer2);
    //            freeBuffer(&send_buffer1);
    //            shutdown(client_fd, SHUT_RDWR);
                //close(client_fd);
//
    //        }
    //        accamulated_buffer.read_index = 0;
    //        accamulated_buffer.write_index = 0;
    //    }
    //}
    //}
    //if (client_socket != -1) {
    //    close(client_socket);
    //}
    close(socket);
    return 0;
}