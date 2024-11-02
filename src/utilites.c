#include <stdarg.h>
#include <stdio.h>
#include "include/utilities.h"

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
