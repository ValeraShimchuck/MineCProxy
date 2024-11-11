#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>
#include "utilities.h"
#include <fcntl.h>
#include <errno.h>

void println(char* str, ...) {
    va_list args;
    va_start(args, str);
    
    vprintf(str, args);
    printf("\n");  // Add newline at the end
    
    va_end(args);
}

int is_fd_open(int fd) {
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
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

void string_to_ip(char* strIp, char* ipBuffer) {
    if (inet_pton(AF_INET, strIp, ipBuffer) != 1)  {
        printf("Could not convert ip %s", strIp);
        exit(-1);
    }
}

void ip_to_string(char* strIp, char* ipBuffer) {
    if (inet_ntop(AF_INET, ipBuffer, strIp, INET_ADDRSTRLEN) == NULL) {
        printf("Could not convert ip %s", ipBuffer);
        exit(-1);
    }
}

const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
char* base64_encode(const unsigned char* data, size_t input_length, size_t* output_length) {
    *output_length = 4 * ((input_length + 2) / 3);
    char* encoded_data = malloc(*output_length + 1);
    if (encoded_data == NULL) return NULL;
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        encoded_data[j++] = base64_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = base64_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = base64_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = base64_table[(triple >> 0 * 6) & 0x3F];
    }
    for (size_t i = 0; i < (3 - input_length % 3) % 3; i++)
        encoded_data[*output_length - 1 - i] = '=';

    // encoded_data[*output_length] = '\0';
    return encoded_data;
}
