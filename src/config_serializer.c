#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "include/config_serializer.h"

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

    encoded_data[*output_length] = '\0';
    return encoded_data;
}

char* favicon_get() {
    const char* filepath = "./favicons/fv.png";
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        perror("Could not open file");
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    unsigned char* buffer = malloc(file_size);
    if (!buffer) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }
    fread(buffer, 1, file_size, file);
    fclose(file);

    size_t output_length;
    char* encoded_data = base64_encode(buffer, file_size, &output_length);

    free(buffer);
    return encoded_data;
}

char* create_response() {
    char* favicon_data = favicon_get();
    char* formatted_response = malloc(20480);
    if (formatted_response == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    if (favicon_data != NULL) {
        size_t max_data_length = 20480 - 256;
        favicon_data[max_data_length - 1] = '\0';

        snprintf(formatted_response, 20480,
                 "{\"version\":{\"name\":\"1.8 - 1.21.x\",\"protocol\":767},"
                 "\"players\":{\"max\":100,\"online\":0,\"sample\":[]},"
                 "\"description\":{\"text\":\"Minecraft Reverse Proxy\"},"
                 "\"favicon\":\"data:image/png;base64,%s\","
                 "\"enforcesSecureChat\":false}", 
                 favicon_data);
    } else {
        snprintf(formatted_response, 20480,
                 "{\"version\":{\"name\":\"1.8 - 1.21.x\",\"protocol\":767},"
                 "\"players\":{\"max\":100,\"online\":0,\"sample\":[]},"
                 "\"description\":{\"text\":\"Minecraft Reverse Proxy\"},"
                 "\"favicon\":\"\","
                 "\"enforcesSecureChat\":false}");
    }

    return formatted_response;
}