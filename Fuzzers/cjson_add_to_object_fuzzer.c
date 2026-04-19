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

static char *read_bounded_string(const uint8_t **data, size_t *size, size_t max_len) {
    uint8_t raw_len = 0;
    size_t len = 0;
    char *out = NULL;

    if (!read_u8(data, size, &raw_len)) {
        return NULL;
    }

    len = (size_t)(raw_len % (max_len + 1));
    if (*size < len) {
        len = *size;
    }

    out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }

    if (len > 0) {
        memcpy(out, *data, len);
        (*data) += len;
        (*size) -= len;
    }
    out[len] = '\0';

    for (size_t i = 0; i < len; ++i) {
        if (out[i] == '\0') {
            out[i] = 'A';
        }
    }

    return out;
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
    cJSON *root = NULL;
    cJSON *shared_value = NULL;
    cJSON *shared_array_item = NULL;
    cJSON *containers[8] = {0};
    uint8_t raw_ops = 0;
    size_t op_count = 0;
    size_t container_count = 1;

    if (data == NULL || size < 1) {
        return 0;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return 0;
    }

    containers[0] = root;

    shared_value = cJSON_CreateString("shared");
    shared_array_item = cJSON_CreateNumber(1337);
    if ((shared_value == NULL) || (shared_array_item == NULL)) {
        cJSON_Delete(shared_value);
        cJSON_Delete(shared_array_item);
        cJSON_Delete(root);
        return 0;
    }

    raw_ops = *data++;
    size--;
    op_count = (size_t)(raw_ops % 32);

    for (size_t i = 0; i < op_count && size > 0; ++i) {
        uint8_t opcode = 0;
        uint8_t target_sel = 0;
        uint8_t fail_mode = 0;
        char *key_storage = NULL;
        const char *key = NULL;
        char *payload = NULL;
        double number = 0.0;
        cJSON *obj = NULL;
        cJSON *arr = NULL;
        cJSON *item = NULL;
        cJSON *tmp = NULL;
        cJSON *detached = NULL;
        cJSON *target = root;

        if (!read_u8(&data, &size, &opcode)) {
            break;
        }
        if (!read_u8(&data, &size, &target_sel)) {
            break;
        }
        if (!read_u8(&data, &size, &fail_mode)) {
            break;
        }

        if (container_count > 0) {
            target = containers[target_sel % container_count];
        }

        key_storage = read_bounded_string(&data, &size, 24);
        key = key_storage;

        if ((fail_mode & 0x3) == 0) {
            key = NULL;
        }
        if (((fail_mode >> 2) & 0x3) == 0) {
            target = NULL;
        }

        switch (opcode % 13) {
            case 0:
                (void)read_double(&data, &size, &number);
                (void)cJSON_AddNumberToObject(target, key, number);
                break;

            case 1:
                payload = read_bounded_string(&data, &size, 48);
                if (payload != NULL) {
                    (void)cJSON_AddStringToObject(target, key, payload);
                    free(payload);
                }
                break;

            case 2:
                payload = read_bounded_string(&data, &size, 48);
                if (payload != NULL) {
                    (void)cJSON_AddRawToObject(target, key, payload);
                    free(payload);
                }
                break;

            case 3:
                (void)cJSON_AddTrueToObject(target, key);
                break;

            case 4:
                (void)cJSON_AddFalseToObject(target, key);
                break;

            case 5:
                (void)cJSON_AddNullToObject(target, key);
                break;

            case 6:
                (void)cJSON_AddBoolToObject(target, key, (opcode & 1) ? 1 : 0);
                break;

            case 7:
                obj = cJSON_AddObjectToObject(target, key);
                if ((obj != NULL) && (container_count < 8)) {
                    containers[container_count++] = obj;
                }
                break;

            case 8:
                arr = cJSON_AddArrayToObject(target, key);
                if (arr != NULL) {
                    (void)cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)opcode));
                    (void)cJSON_AddItemReferenceToArray(arr, shared_array_item);
                }
                break;

            case 9:
                (void)cJSON_AddItemReferenceToObject(target, key, shared_value);
                break;

            case 10:
                payload = read_bounded_string(&data, &size, 48);
                if (payload != NULL) {
                    item = cJSON_CreateRaw(payload);
                    if (item != NULL) {
                        (void)cJSON_AddItemToObject(target, key, item);
                    }
                    free(payload);
                }
                break;

            case 11:
                item = cJSON_CreateNumber(7.0);
                if (item != NULL) {
                    (void)cJSON_AddItemToObjectCS(target, key, item);
                }
                break;

            case 12:
                tmp = cJSON_CreateNumber(1.0);
                if (tmp != NULL) {
                    if (cJSON_AddItemToObject(root, "oldkey", tmp)) {
                        detached = cJSON_DetachItemFromObject(root, "oldkey");
                        if (detached != NULL) {
                            (void)cJSON_AddItemToObject(target ? target : root,
                                                        key ? key : "newkey",
                                                        detached);
                        }
                    } else {
                        cJSON_Delete(tmp);
                    }
                }
                break;

            default:
                break;
        }

        if ((fail_mode & 0x80) == 0) {
            (void)cJSON_AddItemReferenceToArray(NULL, shared_array_item);
        }

        free(key_storage);
    }

    cJSON_Delete(shared_value);
    cJSON_Delete(shared_array_item);
    cJSON_Delete(root);
    return 0;
}

#ifdef __cplusplus
}
#endif
