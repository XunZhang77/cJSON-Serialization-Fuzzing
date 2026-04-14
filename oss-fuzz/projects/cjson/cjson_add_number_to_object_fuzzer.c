#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

static int read_u8(const uint8_t **data, size_t *size, uint8_t *out) {
    if (*size < 1) {
        return 0;
    }
    *out = **data;
    (*data)++;
    (*size)--;
    return 1;
}

static int read_bytes(const uint8_t **data, size_t *size, uint8_t *out, size_t n) {
    if (*size < n) {
        return 0;
    }
    memcpy(out, *data, n);
    (*data) += n;
    (*size) -= n;
    return 1;
}

static char *read_key(const uint8_t **data, size_t *size) {
    uint8_t raw_len = 0;
    if (!read_u8(data, size, &raw_len)) {
        return NULL;
    }

    /* Keep keys bounded for speed and memory stability. */
    size_t key_len = (size_t)(raw_len % 32);

    if (*size < key_len) {
        key_len = *size;
    }

    char *key = (char *)malloc(key_len + 1);
    if (key == NULL) {
        return NULL;
    }

    if (key_len > 0) {
        memcpy(key, *data, key_len);
        (*data) += key_len;
        (*size) -= key_len;
    }
    key[key_len] = '\0';

    /* Replace embedded NULs with visible characters. */
    for (size_t i = 0; i < key_len; ++i) {
        if (key[i] == '\0') {
            key[i] = 'A';
        }
    }

    return key;
}

static int read_double(const uint8_t **data, size_t *size, double *out) {
    union {
        uint64_t u64;
        double d;
    } conv;

    conv.u64 = 0;

    if (*size >= sizeof(uint64_t)) {
        if (!read_bytes(data, size, (uint8_t *)&conv.u64, sizeof(uint64_t))) {
            return 0;
        }
    } else {
        /*
         * If fewer than 8 bytes remain, consume what we have and leave the rest 0.
         * This still gives deterministic behavior for short inputs.
         */
        size_t n = *size;
        if (n > 0) {
            memcpy(&conv.u64, *data, n);
            (*data) += n;
            (*size) -= n;
        }
    }

    *out = conv.d;
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (data == NULL || size == 0) {
        return 0;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return 0;
    }

    /*
     * First byte controls how many add operations we attempt.
     * Bound the count to keep runs fast and deterministic.
     */
    uint8_t raw_ops = *data++;
    size--;

    size_t op_count = (size_t)(raw_ops % 16);

    for (size_t i = 0; i < op_count && size > 0; ++i) {
        char *key = read_key(&data, &size);
        if (key == NULL) {
            break;
        }

        double number = 0.0;
        (void)read_double(&data, &size, &number);

        /*
         * Core target API.
         * Duplicate keys are allowed by construction and may expose
         * different internal object behaviors.
         */
        cJSON_AddNumberToObject(root, key, number);

        free(key);
    }

    /*
     * Exercise printing after the object has accumulated state.
     * This can expose bugs in number formatting and object traversal.
     */
    char *printed = cJSON_PrintUnformatted(root);
    if (printed != NULL) {
        /*
         * Optional round-trip parse to further leverage structured states
         * produced by the add-number operations.
         */
        cJSON *parsed = cJSON_Parse(printed);
        if (parsed != NULL) {
            cJSON_Delete(parsed);
        }
        free(printed);
    }

    cJSON_Delete(root);
    return 0;
}

#ifdef __cplusplus
}
#endif
