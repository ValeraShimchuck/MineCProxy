#ifndef BUFFER_H
#define BUFFER_H
#include <stdbool.h>
#include "utilities.h"
typedef struct {
    char* buffer;
    int size;
    int read_index;
    int write_index;
} buffer;

buffer createBuffer(int size);
int getBufferSize(buffer* buf);
void freeBuffer(buffer* buf);
char readByte(buffer* buf);
void readBytes(buffer* buf, char* buffer, int size);
bool canRead(buffer* buf);
int tryReadVarInt(buffer* buf);
void writeBytes(buffer* buf, char* buffer, int size);
void writeBuffer(buffer* dst, buffer* src, int size);
void writeByte(buffer* buf, char c);
int readVarInt(buffer* buf);
void writeVarInt(buffer* buf, int value);
short readShort(buffer* buf);
long long readLong(buffer* buf);
void writeLong(buffer* buf, long long value);

#endif