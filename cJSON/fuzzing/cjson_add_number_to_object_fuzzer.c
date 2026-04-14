/* fuzzing/cjson_add_number_to_object_fuzzer.c */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../cJSON.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    cJSON *root = NULL;
    char *key = NULL;
    char *printed = NULL;
    double number = 0.0;
    size_t key_len;

    if (data == NULL || size < 2) {
        return 0;
    }

    /* Split input into:
       - first byte controls key length
       - next bytes provide key contents
       - remaining bytes contribute to the number */
    key_len = (size_t)(data[0] % 32);  /* cap key size */
    if (key_len > size - 1) {
        key_len = size - 1;
    }

    key = (char *)malloc(key_len + 1);
    if (key == NULL) {
        return 0;
    }

    memcpy(key, data + 1, key_len);
    key[key_len] = '\0';

    /* Make sure the key is a valid C string with no embedded NUL truncation surprises */
    for (size_t i = 0; i < key_len; i++) {
        if (key[i] == '\0') {
            key[i] = 'A';
        }
    }

    /* Derive a number from up to 8 bytes */
    {
        uint64_t bits = 0;
        size_t remaining = size - 1 - key_len;
        size_t n = remaining > sizeof(bits) ? sizeof(bits) : remaining;
        memcpy(&bits, data + 1 + key_len, n);
        number = (double)(int64_t)bits;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        free(key);
        return 0;
    }

    /* Target API */
    cJSON_AddNumberToObject(root, key, number);

    /* Exercise additional downstream logic */
    printed = cJSON_PrintUnformatted(root);
    if (printed != NULL) {
        free(printed);
    }

    cJSON_Delete(root);
    free(key);
    return 0;
}
