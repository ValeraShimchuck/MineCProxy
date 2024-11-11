#include "channels.h"
#include "buffer.h"
#include "config_serializer.h"
#include "utilities.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <libdeflate.h>
#include <uuid/uuid.h>
#include <arpa/inet.h>
#include <unistd.h>

#define MAX_PING_RESPONSE_SIZE 20480
#define INITIAL_BUFFER_SIZE 1000000
#define READ_BUFFER_SIZE 1024
#define POLL_TIMEOUT 20000 // in milliseconds
#define MAX_SERVER_ADDRESS_LENGTH 16
#define MAX_PACKET_SIZE 10240000

int last_channel_id = -1;
int channels_max_size = -1;
int channels_size = -1;
channel_context *channels = NULL;
char *raw_ping_response = NULL;
int connection_relations_size = -1;
connection_relation *connection_relations = NULL;

int init_channels(int max_channels)
{
    channels_max_size = max_channels;
    channels_size = 0;

    channels = malloc(channels_max_size * sizeof(channel_context));
    if (channels == NULL)
    {
        fprintf(stderr, "Memory allocation for channels failed\n");
        return -1;
    }

    raw_ping_response = malloc(MAX_PING_RESPONSE_SIZE);
    if (raw_ping_response == NULL)
    {
        fprintf(stderr, "Memory allocation for raw_ping_response failed\n");
        free(channels);
        return -1;
    }
    create_raw_response(raw_ping_response);

    return 0;
}

int create_channel(int max_connections, int type)
{
    if (channels_size >= channels_max_size)
    {
        fprintf(stderr, "No more channels available\n");
        return -1;
    }

    last_channel_id++;
    channel_context *c = &channels[last_channel_id];
    c->max_connections = max_connections;
    c->current_connections = 0;
    c->connection_type = type;

    // Corrected sizeof to connection_context
    c->connections = malloc(max_connections * sizeof(connection_context));
    if (c->connections == NULL)
    {
        fprintf(stderr, "Memory allocation for channel connections failed\n");
        return -1;
    }

    for (int i = 0; i < max_connections; i++)
    {
        c->connections[i].fd = -1;
        c->connections[i].buf = NULL;
        c->connections[i].state = 0;
        c->connections[i].protocol_version = 0;
    }

    channels_size++;

    if (pipe(c->sync_pipes) == -1)
    {
        fprintf(stderr, "Failed to create pipe for channel %d\n", last_channel_id);
        free(c->connections);
        return -1;
    }

    if (fcntl(c->sync_pipes[0], F_SETFL, O_NONBLOCK) == -1 ||
        fcntl(c->sync_pipes[1], F_SETFL, O_NONBLOCK) == -1)
    {
        fprintf(stderr, "Failed to set non-blocking mode for pipes\n");
        close(c->sync_pipes[0]);
        close(c->sync_pipes[1]);
        free(c->connections);
        return -1;
    }

    if (connection_relations_size < 0)
    {
        connection_relations_size = max_connections;
        connection_relations = malloc(connection_relations_size * sizeof(connection_relation));
        if (connection_relations == NULL)
        {
            fprintf(stderr, "Memory allocation for connection_relations failed\n");
            close(c->sync_pipes[0]);
            close(c->sync_pipes[1]);
            free(c->connections);
            return -1;
        }
        for (int i = 0; i < connection_relations_size; i++)
        {
            pthread_mutex_init(&connection_relations[i].lock, NULL);
            connection_relations[i].client_fd = -1;
            connection_relations[i].backend_fd = -1;
        }
    }
    else
    {
        int old_size = connection_relations_size;
        connection_relations_size += max_connections;
        connection_relations = realloc(connection_relations, connection_relations_size * sizeof(connection_relation));
        if (connection_relations == NULL)
        {
            fprintf(stderr, "Memory allocation for connection_relations failed\n");
            close(c->sync_pipes[0]);
            close(c->sync_pipes[1]);
            free(c->connections);
            return -1;
        }
        for (int i = old_size; i < connection_relations_size; i++)
        {
            connection_relations[i].client_fd = -1;
            connection_relations[i].backend_fd = -1;
        }
    }

    return last_channel_id;
}

int add_connection(int channel_id, int client_fd)
{
    if (channel_id < 0 || channel_id > last_channel_id)
    {
        fprintf(stderr, "Invalid channel id %d\n", channel_id);
        return -1;
    }

    channel_context *ch = &channels[channel_id];

    if (ch->current_connections >= ch->max_connections)
    {
        fprintf(stderr, "Channel %d is full. Max connections %d\n",
                channel_id, ch->max_connections);
        return -1;
    }

    int suitable_connection_id = -1;
    for (int i = 0; i < ch->max_connections; i++)
    {
        if (ch->connections[i].fd < 0)
        {
            suitable_connection_id = i;
            break;
        }
    }

    if (suitable_connection_id == -1)
    {
        fprintf(stderr, "No available slot in channel %d\n", channel_id);
        return -1;
    }

    ch->connections[suitable_connection_id].fd = client_fd;
    ch->connections[suitable_connection_id].buf = malloc(sizeof(buffer));
    connection_relation *cr = NULL;
    for (int i = 0; i < connection_relations_size; i++)
    {
        if (connection_relations[i].client_fd == -1)
        {
            cr = &connection_relations[i];
            break;
        }
    }
    if (cr == NULL)
    {
        fprintf(stderr, "No available slot in connection_relations\n");
        return -1;
    }

    cr->client_fd = client_fd;
    ch->connections[suitable_connection_id].relation = cr;
    if (ch->connections[suitable_connection_id].buf == NULL)
    {
        fprintf(stderr, "Memory allocation for client buffer failed\n");
        return -1;
    }

    *(ch->connections[suitable_connection_id].buf) = createBuffer(INITIAL_BUFFER_SIZE);
    ch->current_connections++;

    return 0;
}

int find_suitable_channel(int type)
{
    for (int i = 0; i < channels_size; i++)
    {
        if (channels[i].current_connections < channels[i].max_connections && channels[i].connection_type == type)
        {
            return i;
        }
    }
    return -1;
}

int wake_up_channel(int channel_id)
{
    if (channel_id < 0 || channel_id > last_channel_id)
    {
        fprintf(stderr, "Invalid channel id %d\n", channel_id);
        return -1;
    }

    channel_context *ch = &channels[channel_id];
    printf("Waking up channel %d\n", channel_id);

    ssize_t bytes_written = write(ch->sync_pipes[1], "wake_up", 7);
    if (bytes_written == -1)
    {
        fprintf(stderr, "Failed to write to sync pipe: %s\n", strerror(errno));
        return -1;
    }
    printf("successfully woke up channel %d\n", channel_id);
    return 0;
}

void disconnect_client(int channel_id, int fd)
{
    if (channel_id < 0 || channel_id > last_channel_id)
    {
        fprintf(stderr, "Invalid channel id %d\n", channel_id);
        return;
    }

    channel_context *ch = &channels[channel_id];

    for (int i = 0; i < ch->max_connections; i++)
    {
        connection_context *c = &ch->connections[i];
        if (c->fd == fd)
        {
            shutdown(fd, SHUT_RDWR);
            close(fd);

            if (c->buf != NULL)
            {
                freeBuffer(c->buf);
                free(c->buf);
                c->buf = NULL;
            }

            c->fd = -1;
            c->state = 0;
            c->protocol_version = 0;
            c->relation->client_fd = -1;
            shutdown(c->relation->backend_fd, SHUT_RDWR);
            close(c->relation->backend_fd);
            c->relation->backend_fd = -1;
            ch->current_connections--;
            break;
        }
    }
}

int connect_to_backend(int channel_id, char *nickname, int size, uuid_t uuid, connection_relation *cr)
{
    int sockfd;
    struct sockaddr_in server_addr;
    char raw_read_buffer[READ_BUFFER_SIZE];

    buffer read_buffer = {
        .buffer = raw_read_buffer,
        .read_index = 0,
        .write_index = 0,
        .size = READ_BUFFER_SIZE};

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Could not open socket");
        exit(-1);
    }

    memset(&server_addr, 0, sizeof(server_addr)); // Clear the structure
    server_addr.sin_family = AF_INET;             // IPv4
    server_addr.sin_port = htons(25566);          // Server port
    //server_addr.sin_addr.s_addr = 0;
    if (inet_pton(AF_INET, "0.0.0.0", &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address or address not supported");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection to the server failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Connected to the server and handshaking him\n");
    buffer buf1 = createBuffer(3000);
    writeVarInt(&buf1, 0);
    writeVarInt(&buf1, 767);
    char *serverAddress = "0.0.0.0";
    writeVarInt(&buf1, strlen(serverAddress));
    writeBytes(&buf1, serverAddress, strlen(serverAddress));
    writeShort(&buf1, 25566);
    writeVarInt(&buf1, 2);

    buffer buf2 = createBuffer(3010);
    writeVarInt(&buf2, getBufferSize(&buf1));
    writeBuffer(&buf2, &buf1, getBufferSize(&buf1));
    if ( send(sockfd, buf2.buffer, getBufferSize(&buf2), 0) < 0) {
        perror("Failed to send data");
        close(sockfd);
        freeBuffer(&buf1);
        freeBuffer(&buf2);
        exit(EXIT_FAILURE);
    }

    buf1.write_index = 0;
    buf1.read_index = 0;

    buf2.write_index = 0;
    buf2.read_index = 0;

    
    printf("Sending login packet\n");

    writeVarInt(&buf1, 0);
    writeVarInt(&buf1, size);
    writeBytes(&buf1, nickname, size);
    writeBytes(&buf1, uuid, 16);

    
    writeVarInt(&buf2, getBufferSize(&buf1));
    writeBuffer(&buf2, &buf1, getBufferSize(&buf1));
    
    if ( send(sockfd, buf2.buffer, getBufferSize(&buf2), 0) < 0) {
        perror("Failed to send data2");
        close(sockfd);
        freeBuffer(&buf1);
        freeBuffer(&buf2);
        exit(EXIT_FAILURE);
    }
    
    buf1.write_index = 0;
    buf1.read_index = 0;

    buf2.write_index = 0;
    buf2.read_index = 0;

    buffer accumulator = createBuffer(4000);

    char rcv_buffer[READ_BUFFER_SIZE];
    ssize_t read_bytes;

    // Read data from client
    while ((read_bytes = recv(sockfd, rcv_buffer, sizeof(rcv_buffer), 0)) > 0)
    {   
        buffer read_buf = {
            .buffer = rcv_buffer,
            .size = (size_t)read_bytes,
            .read_index = 0,
            .write_index = (size_t)read_bytes};

        bool should_disconnect = false;

        while (1)
        {
            int packet_length = tryReadVarInt(&accumulator);
            if (packet_length == -1)
            {
                if (!canRead(&read_buf))
                {
                    break;
                }
                writeByte(&accumulator, readByte(&read_buf));
                continue;
            }

            // Ensure packet_length is within reasonable bounds
            if (packet_length <= 0 || packet_length > MAX_PACKET_SIZE)
            {
                fprintf(stderr, "Invalid packet length: %d\n", packet_length);
                should_disconnect = true;
                break;
            }

            writeBuffer(&accumulator, &read_buf, packet_length);
            if (packet_length > getBufferSize(&accumulator))
            {
                // Not enough data yet
                accumulator.read_index = 0;
                break;
            }
            //printArray(accumulator.buffer + accumulator.read_index, getBufferSize(&accumulator) - accumulator.read_index);


            int packet_id = readVarInt(&accumulator);
            //printArray(read_buf.buffer, 39);
            if (packet_id == 0x02) {
                writeVarInt(&buf2, 1);
                writeVarInt(&buf2, 3);
                int ret = send(sockfd, buf2.buffer, getBufferSize(&buf2), 0);
                if (ret < 0) {
                    printf("Error to send aknowledged packet %s\n", strerror(errno));
                    close(sockfd);
                    freeBuffer(&accumulator);
                    freeBuffer(&buf2);
                    freeBuffer(&buf1);
                    exit(EXIT_FAILURE);
                }
                freeBuffer(&accumulator);
                freeBuffer(&buf2);
                freeBuffer(&buf1);

                channel_context *ch = &channels[channel_id];

                connection_context *suitable_connection = NULL;
                for (int i = 0; i < ch->max_connections; i++)
                {
                    connection_context *c = &ch->connections[i];
                    if (c->fd < 0)
                    {
                        suitable_connection = c;
                        break;
                    }
                }
                if (suitable_connection == NULL)
                {
                    fprintf(stderr, "No available connections for channel %d\n", channel_id);
                    close(sockfd);
                    return -1;
                }

                suitable_connection->fd = sockfd;
                suitable_connection->buf = malloc(sizeof(buffer));
                (*suitable_connection->buf) = createBuffer(INITIAL_BUFFER_SIZE);
                suitable_connection->state = 3;
                suitable_connection->protocol_version = 767;
                suitable_connection->relation = cr;
                cr->backend_fd = sockfd;
                return sockfd;
            } else if (packet_id == 0x00) {
                int size = readVarInt(&accumulator);
                char reason[size];
                readBytes(&accumulator, reason, size);
                printf("Got disconnect packet reason: %s\n", reason);
            }

            
        }
        
    }
    if (read_bytes == 0) {
        printf("Server dissconnected us\n");
    } 
    if (read_bytes < 0) {
        printf("Error: %s\n", strerror(errno));
    }
    close(sockfd);
    return -1;
}

void start_channel_to_backend(int channel_id)
{
    printf("Starting backend channel %d\n", channel_id);
    if (channel_id < 0 || channel_id > last_channel_id)
    {
        fprintf(stderr, "Invalid channel id %d\n", channel_id);
        return;
    }

    channel_context *ch = &channels[channel_id];
    int total_fds = ch->max_connections + 1;

    struct pollfd *pfds = malloc(total_fds * sizeof(struct pollfd));
    if (pfds == NULL)
    {
        fprintf(stderr, "Memory allocation for pollfd array failed\n");
        return;
    }

    pfds[ch->max_connections].fd = ch->sync_pipes[0];
    pfds[ch->max_connections].events = POLLIN;

    while (1)
    {
        for (int i = 0; i < ch->max_connections; i++)
        {
            connection_context *c = &ch->connections[i];

            if (c->fd < 0 || c->relation->backend_fd < 0)
            {
                pfds[i].fd = -1;
                pfds[i].events = 0;
            }
            else
            {
                pfds[i].fd = c->fd;
                pfds[i].events = POLLIN;
            }
            pfds[i].revents = 0; // Clear revents before polling
        }
        pfds[ch->max_connections].revents = 0; // Clear revents for sync pipe

        int poll_res = poll(pfds, total_fds, POLL_TIMEOUT);
        if (poll_res == -1)
        {
            if (errno == EINTR)
            {
                continue; // Interrupted by signal, retry poll
            }
            fprintf(stderr, "Poll failed: %s\n", strerror(errno));
            break;
        }

        // Check sync pipe for wake-up signals
        if (pfds[ch->max_connections].revents & POLLIN)
        {
            char rcv_buffer[READ_BUFFER_SIZE];
            ssize_t read_bytes;
            while ((read_bytes = read(pfds[ch->max_connections].fd, rcv_buffer, sizeof(rcv_buffer))) > 0)
            {
                // Process wake-up messages if necessary
            }
            if (read_bytes == -1 && errno != EWOULDBLOCK && errno != EAGAIN)
            {
                fprintf(stderr, "Pipe read error: %s\n", strerror(errno));
                break;
            }
        }

        for (int i = 0; i < ch->max_connections; i++)
        {
            if (pfds[i].fd < 0)
            {
                continue;
            }

            if (pfds[i].revents & POLLIN)
            {
                connection_context *c = &ch->connections[i];
                int client_fd = c->fd;
                buffer *buf = c->buf;

                char rcv_buffer[READ_BUFFER_SIZE];
                ssize_t read_bytes;

                // Read data from client
                while ((read_bytes = recv(client_fd, rcv_buffer, sizeof(rcv_buffer), 0)) > 0)
                {
                    buffer read_buf = {
                        .buffer = rcv_buffer,
                        .size = (size_t)read_bytes,
                        .read_index = 0,
                        .write_index = (size_t)read_bytes};

                    bool should_disconnect = false;

                    while (1)
                    {
                        int packet_length = tryReadVarInt(buf);
                        if (packet_length == -1)
                        {
                            if (!canRead(&read_buf))
                            {
                                break;
                            }
                            writeByte(buf, readByte(&read_buf));
                            continue;
                        }

                        // Ensure packet_length is within reasonable bounds
                        if (packet_length <= 0 || packet_length > MAX_PACKET_SIZE)
                        {
                            fprintf(stderr, "Invalid packet length: %d\n", packet_length);
                            should_disconnect = true;
                            break;
                        }
                        int data_to_read = MIN(packet_length - getBufferSize(buf), getBufferSize(&read_buf));
                        
                        writeBuffer(buf, &read_buf, data_to_read);
                        if (getBufferSize(buf) > packet_length)  {
                            printf("Packet length: %d\n", packet_length);
                            printf("Data to read: %d\n", data_to_read);
                            printf("Buffer size: %d\n", getBufferSize(buf));
                            printf("Buffer index: %d\n", buf->read_index);
                            printf("Read buffer index: %d\n", read_buf.read_index);
                        }
                        if (packet_length > getBufferSize(buf))
                        {
                            // Not enough data yet
                            buf->read_index = 0;
                            break;
                        }

                        int reader_index = buf->read_index;
                        int packet_id = readVarInt(buf);
                        buf->read_index = reader_index;
                        //int state = c->state;
                        connection_relation *cr = c->relation;
                        int backend_fd = cr->backend_fd;
                        int client_fd = cr->client_fd;


                        buffer send_buf = createBuffer(3 + packet_length);
                        writeVarInt(&send_buf, packet_length);
                        writeBuffer(&send_buf, buf, packet_length);
                        
                        pthread_mutex_lock(&cr->lock);
                        int bytes_sent = send(client_fd, send_buf.buffer, getBufferSize(&send_buf), 0);
                        pthread_mutex_unlock(&cr->lock);
                        if (bytes_sent < 0)
                        {
                            fprintf(stderr, "Failed to send data: %s\n", strerror(errno));
                            freeBuffer(&send_buf);
                            break;
                        }
                        freeBuffer(&send_buf);

                        if (getBufferSize(buf) > 0) {
                            printf("leftover %d\n", getBufferSize(buf));
                            exit(-1);
                        }
                        buf->read_index = 0;
                        buf->write_index = 0;
                        // TODO continue
                    }

                    if (read_bytes == -1 && errno != EWOULDBLOCK && errno != EAGAIN)
                    {
                    fprintf(stderr, "Client read error: %s\n", strerror(errno));
                    break;
                    }

                }
                
            }
        }
    }
}

















void start_channel_to_client(int channel_id)
{
    printf("Starting channel %d\n", channel_id);

    if (channel_id < 0 || channel_id > last_channel_id)
    {
        fprintf(stderr, "Invalid channel id %d\n", channel_id);
        return;
    }

    channel_context *ch = &channels[channel_id];
    int total_fds = ch->max_connections + 1;

    // Allocate pollfd array dynamically to avoid stack overflow
    struct pollfd *pfds = malloc(total_fds * sizeof(struct pollfd));
    if (pfds == NULL)
    {
        fprintf(stderr, "Memory allocation for pollfd array failed\n");
        return;
    }

    pfds[ch->max_connections].fd = ch->sync_pipes[0];
    pfds[ch->max_connections].events = POLLIN;

    while (1)
    {
        // Set up pollfd structures
        for (int i = 0; i < ch->max_connections; i++)
        {
            connection_context *c = &ch->connections[i];

            if (c->fd < 0)
            {
                pfds[i].fd = -1;
                pfds[i].events = 0;
            }
            else
            {
                pfds[i].fd = c->fd;
                pfds[i].events = POLLIN;
            }
            pfds[i].revents = 0; // Clear revents before polling
        }
        pfds[ch->max_connections].revents = 0; // Clear revents for sync pipe

        int poll_res = poll(pfds, total_fds, POLL_TIMEOUT);
        if (poll_res == -1)
        {
            if (errno == EINTR)
            {
                continue; // Interrupted by signal, retry poll
            }
            fprintf(stderr, "Poll failed: %s\n", strerror(errno));
            break;
        }
        // Check sync pipe for wake-up signals
        if (pfds[ch->max_connections].revents & POLLIN)
        {
            char rcv_buffer[READ_BUFFER_SIZE];
            ssize_t read_bytes;
            while ((read_bytes = read(pfds[ch->max_connections].fd, rcv_buffer, sizeof(rcv_buffer))) > 0)
            {
                // Process wake-up messages if necessary
            }
            if (read_bytes == -1 && errno != EWOULDBLOCK && errno != EAGAIN)
            {
                fprintf(stderr, "Pipe read error: %s\n", strerror(errno));
                break;
            }
        }
        
        // Process client connections
        for (int i = 0; i < ch->max_connections; i++)
        {
            if (pfds[i].fd < 0)
            {
                continue;
            }

            if (pfds[i].revents & POLLIN)
            {
                connection_context *c = &ch->connections[i];
                int client_fd = c->fd;
                buffer *buf = c->buf;

                char rcv_buffer[READ_BUFFER_SIZE];
                ssize_t read_bytes;

                // Read data from client
                while ((read_bytes = recv(client_fd, rcv_buffer, sizeof(rcv_buffer), 0)) > 0)
                {
                    
                    buffer read_buf = {
                        .buffer = rcv_buffer,
                        .size = (size_t)read_bytes,
                        .read_index = 0,
                        .write_index = (size_t)read_bytes};

                    bool should_disconnect = false;
                    while (1)
                    {
                        int packet_length = tryReadVarInt(buf);
                        if (packet_length == -1)
                        {
                            if (!canRead(&read_buf))
                            {
                                break;
                            }
                            writeByte(buf, readByte(&read_buf));
                            continue;
                        }

                        // Ensure packet_length is within reasonable bounds
                        if (packet_length <= 0 || packet_length > MAX_PACKET_SIZE)
                        {
                            fprintf(stderr, "Invalid packet length: %d\n", packet_length);
                            should_disconnect = true;
                            break;
                        }
                        
                        
                        int data_to_read = MIN(packet_length - getBufferSize(buf), getBufferSize(&read_buf));
                        writeBuffer(buf, &read_buf, data_to_read);
                        if (packet_length > getBufferSize(buf))
                        {
                            // Not enough data yet
                            buf->read_index = 0;
                            break;
                        }
                        int buf_reader_index = buf->read_index;
                        int packet_id = readVarInt(buf);
                        int state = c->state;
                        if (packet_id == 0 && state == 0)
                        {
                            // Handle handshake
                            c->protocol_version = readVarInt(buf);
                            int server_address_length = readVarInt(buf);

                            // Validate server_address_length
                            if (server_address_length <= 0 || server_address_length > MAX_SERVER_ADDRESS_LENGTH)
                            {
                                fprintf(stderr, "Invalid server address length: %d\n", server_address_length);
                                should_disconnect = true;
                                break;
                            }

                            char *server_address = malloc(server_address_length + 1);
                            if (server_address == NULL)
                            {
                                fprintf(stderr, "Memory allocation for server address failed\n");
                                should_disconnect = true;
                                break;
                            }

                            readBytes(buf, server_address, server_address_length);
                            server_address[server_address_length] = '\0';

                            unsigned short server_port = readShort(buf);
                            int next_state = readVarInt(buf);
                            c->state = next_state;

                            free(server_address);
                        }
                        else if (packet_id == 0 && state == 1)
                        {
                            // Handle ping request
                            char *PING_RESPONSE = malloc(MAX_PING_RESPONSE_SIZE);
                            if (PING_RESPONSE == NULL)
                            {
                                fprintf(stderr, "Memory allocation for PING_RESPONSE failed\n");
                                should_disconnect = true;
                                break;
                            }

                            int ping_response_length = apply_response(raw_ping_response, PING_RESPONSE, c->protocol_version);

                            buffer send_buffer1 = createBuffer(ping_response_length + 10);
                            writeVarInt(&send_buffer1, 0);
                            writeVarInt(&send_buffer1, ping_response_length);
                            writeBytes(&send_buffer1, PING_RESPONSE, ping_response_length);

                            buffer send_buffer2 = createBuffer(getBufferSize(&send_buffer1) + 10);
                            writeVarInt(&send_buffer2, getBufferSize(&send_buffer1));
                            writeBuffer(&send_buffer2, &send_buffer1, getBufferSize(&send_buffer1));

                            printf("send response: %d\n", getBufferSize(&send_buffer2));
                            ssize_t bytes_sent = send(client_fd, send_buffer2.buffer, getBufferSize(&send_buffer2), 0);
                            if (bytes_sent == -1)
                            {
                                fprintf(stderr, "Failed to send ping response: %s\n", strerror(errno));
                                should_disconnect = true;
                            }

                            freeBuffer(&send_buffer2);
                            freeBuffer(&send_buffer1);
                            free(PING_RESPONSE);
                        }
                        else if (packet_id == 1 && state == 1)
                        {
                            // Handle ping payload
                            long long payload = readLong(buf);

                            buffer send_buffer1 = createBuffer(20);
                            writeVarInt(&send_buffer1, 1);
                            writeLong(&send_buffer1, payload);

                            buffer send_buffer2 = createBuffer(getBufferSize(&send_buffer1) + 10);
                            writeVarInt(&send_buffer2, getBufferSize(&send_buffer1));
                            writeBuffer(&send_buffer2, &send_buffer1, getBufferSize(&send_buffer1));

                            ssize_t bytes_sent = send(client_fd, send_buffer2.buffer, getBufferSize(&send_buffer2), 0);
                            if (bytes_sent == -1)
                            {
                                fprintf(stderr, "Failed to send pong response: %s\n", strerror(errno));
                            }

                            freeBuffer(&send_buffer2);
                            freeBuffer(&send_buffer1);

                            should_disconnect = true;
                        }
                        else if (packet_id == 0 && state == 2)
                        {
                            int nameSize = readVarInt(buf);
                            char *name = malloc(nameSize);
                            readBytes(buf, name, nameSize);
                            uuid_t uuid;
                            c->nicklength = nameSize;
                            c->nick = name;
                            readBytes(buf, &uuid, 16);
                            memcpy(c->uuid, uuid, 16);
                            buffer send_buffer1 = createBuffer(50);
                            writeVarInt(&send_buffer1, 2);
                            writeBytes(&send_buffer1, &uuid, 16);
                            writeVarInt(&send_buffer1, nameSize);
                            writeBytes(&send_buffer1, name, nameSize);
                            writeVarInt(&send_buffer1, 0);
                            writeByte(&send_buffer1, 1);

                            buffer send_buffer2 = createBuffer(getBufferSize(&send_buffer1) + 10);
                            writeVarInt(&send_buffer2, getBufferSize(&send_buffer1));
                            writeBuffer(&send_buffer2, &send_buffer1, getBufferSize(&send_buffer1));

                            
                            ssize_t bytes_sent = send(client_fd, send_buffer2.buffer, getBufferSize(&send_buffer2), 0);
                            if (bytes_sent == -1)
                            {
                                fprintf(stderr, "Failed to send login success response: %s\n", strerror(errno));
                                should_disconnect = true;
                            }

                            freeBuffer(&send_buffer2);
                            freeBuffer(&send_buffer1);
                        }
                        else if (packet_id == 3 && state == 2)
                        {
                            // connect to backend
                        
                            
                            c->state = 3;
                            int backend_channel = find_suitable_channel(1);
                            int backend_fd = connect_to_backend(backend_channel, c->nick, c->nicklength, c->uuid, c->relation);
                            printf("backend_fd: %d\n", backend_fd);
                            wake_up_channel(backend_channel);
                        }
                        else
                        {
                            //fprintf(stderr, "Unhandled packet id %d in state %d\n", packet_id, state);

                            // should_disconnect = true;
                            // break;

                            buffer send_buf = createBuffer(packet_length + 3);
                            buf->read_index = buf_reader_index;
                            writeVarInt(&send_buf, packet_length);
                            writeBuffer(&send_buf, buf, packet_length);
                            
                            printf("Packet ID from client is heading towards server: %d\n", packet_id);
                            pthread_mutex_lock(&c->relation->lock);
                            ssize_t bytes_sent = send(c->relation->backend_fd, send_buf.buffer, getBufferSize(&send_buf), 0);
                            pthread_mutex_unlock(&c->relation->lock);
                            if (bytes_sent < 0)
                            {
                                printf("Failed to send packet: %s\n", strerror(errno));
                                should_disconnect = true;
                            } else if(bytes_sent == 0) {
                                printf("Zero bytes is sent\n");
                            }
                            freeBuffer(&send_buf);
                        }

                        // Reset buffer for next packet
                        buf->read_index = 0;
                        buf->write_index = 0;

                        if (should_disconnect)
                        {
                            break;
                        }
                    }

                    if (should_disconnect)
                    {
                        disconnect_client(channel_id, client_fd);
                        break; // Exit the recv loop for this client
                    }
                }

                if (read_bytes == -1)
                {
                    if (errno != EWOULDBLOCK && errno != EAGAIN)
                    {
                        fprintf(stderr, "Socket read error: %s\n", strerror(errno));
                        disconnect_client(channel_id, client_fd);
                    }
                }
                else if (read_bytes == 0)
                {
                    // Client disconnected
                    printf("Client %d disconnected from channel %d\n", client_fd, channel_id);
                    disconnect_client(channel_id, client_fd);
                }
            }
        }
    }

    // Clean up
    free(pfds);
}
