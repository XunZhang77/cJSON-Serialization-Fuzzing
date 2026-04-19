#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;

    cJSON *root = cJSON_CreateObject();
    if (!root) return 0;

    for (size_t i = 0; i < size && i < 8; i++) {
        char key[4];
        key[0] = 'k';
        key[1] = '0' + (i % 10);
        key[2] = '\0';

        cJSON_AddNumberToObject(root, key, data[i]);
    }

    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < size && i < 4; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(data[i]));
    }
    cJSON_AddItemToObject(root, "arr", arr);

    size_t buf_size = 256;
    char *buffer = (char *)malloc(buf_size);
    if (!buffer) {
        cJSON_Delete(root);
        return 0;
    }

    int format = data[0] & 1;

    cJSON_PrintPreallocated(root, buffer, (int)buf_size, format);

    buffer[buf_size - 1] = '\0';
    cJSON *parsed = cJSON_Parse(buffer);
    if (parsed) {
        cJSON_Delete(parsed);
    }

    free(buffer);
    cJSON_Delete(root);

    return 0;
}

#ifdef __cplusplus
}
#endif