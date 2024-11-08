#ifndef UTILITIES_H
#define UTILITIES_H

#include <stdarg.h> // Потрібно для підтримки varargs
#include <stddef.h>
#include <stdlib.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))


void println(char* str, ...);
void printArray(char* array, int size);
void string_to_ip(char* strIp, char* ipBuffer);
void ip_to_string(char* strIp, char* ipBuffer);
char* base64_encode(const unsigned char* data, size_t input_length, size_t* output_length);

#endif // UTILITIES_H
