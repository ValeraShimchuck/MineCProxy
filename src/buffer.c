#include "buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>


const int SEG_BITS = 0x7F;
const int CONTINUE_BIT = 0x80;

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
    readBytes(buf, (char*) &value, 2);
    return ntohs(value);
}

uint64_t htonll(uint64_t value) {
    return ((uint64_t) htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
}

uint64_t ntohll(uint64_t value) {
    return ((uint64_t) ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
}


long long readLong(buffer* buf) {
    long long value = 0;
    readBytes(buf, &value, 8);
    value = ntohll(value);
    return value;
}


void writeLong(buffer* buf, long long value) {
    value = htonll(value);
    writeBytes(buf, (char*) &value, 8);
}