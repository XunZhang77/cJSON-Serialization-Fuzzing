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

static int read_u8_as_int(const uint8_t **data, size_t *size, int *out) {
    uint8_t b = 0;
    if (!read_u8(data, size, &b)) {
        return 0;
    }
    *out = (int)b;
    return 1;
}

static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *copy = (char *)malloc(n + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, n + 1);
    return copy;
}

static char *read_key(const uint8_t **data, size_t *size) {
    uint8_t raw_len = 0;
    if (!read_u8(data, size, &raw_len)) {
        return NULL;
    }

    size_t key_len = (size_t)(raw_len % 64);
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

    for (size_t i = 0; i < key_len; ++i) {
        if (key[i] == '\0') {
            key[i] = 'A';
        }
    }

    return key;
}

static void mutate_case(char *s, uint8_t mode) {
    if (s == NULL) {
        return;
    }

    for (size_t i = 0; s[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'a' && c <= 'z') {
            if (mode == 1 || (mode == 2 && (i % 2 == 0))) {
                s[i] = (char)(c - 'a' + 'A');
            }
        } else if (c >= 'A' && c <= 'Z') {
            if (mode == 3 || (mode == 2 && (i % 2 == 1))) {
                s[i] = (char)(c - 'A' + 'a');
            }
        }
    }
}

static void seed_object(cJSON *root) {
    cJSON *nested = NULL;
    cJSON *arr = NULL;
    cJSON *inner_arr = NULL;

    cJSON_AddNumberToObject(root, "alpha", 1.0);
    cJSON_AddNumberToObject(root, "Bravo", 2.0);
    cJSON_AddNumberToObject(root, "CHARLIE", 3.0);
    cJSON_AddStringToObject(root, "delta", "x");
    cJSON_AddBoolToObject(root, "Echo", 1);
    cJSON_AddNullToObject(root, "foxtrot");

    nested = cJSON_CreateObject();
    if (nested != NULL) {
        cJSON_AddStringToObject(nested, "innerKey", "value");
        cJSON_AddNumberToObject(nested, "InnerNum", 42.0);
        cJSON_AddBoolToObject(nested, "innerFlag", 1);

        inner_arr = cJSON_CreateArray();
        if (inner_arr != NULL) {
            cJSON_AddItemToArray(inner_arr, cJSON_CreateString("nestedA"));
            cJSON_AddItemToArray(inner_arr, cJSON_CreateNumber(99));
            cJSON_AddItemToObject(nested, "innerArray", inner_arr);
        }

        cJSON_AddItemToObject(root, "nestedObj", nested);
    }

    arr = cJSON_CreateArray();
    if (arr != NULL) {
        cJSON_AddItemToArray(arr, cJSON_CreateString("a"));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(7));
        cJSON_AddItemToArray(arr, cJSON_CreateBool(1));
        cJSON_AddItemToObject(root, "ArrayKey", arr);
    }
}

static void clear_root_object(cJSON *root) {
    while (root != NULL && root->child != NULL) {
        cJSON *detached = cJSON_DetachItemViaPointer(root, root->child);
        if (detached == NULL) {
            break;
        }
        cJSON_Delete(detached);
    }
}

static void rebuild_object(cJSON *root) {
    if (root == NULL) {
        return;
    }
    clear_root_object(root);
    seed_object(root);
}

static char *choose_known_root_key(uint8_t selector) {
    static const char *keys[] = {
        "alpha",
        "Bravo",
        "CHARLIE",
        "delta",
        "Echo",
        "foxtrot",
        "nestedObj",
        "ArrayKey"
    };
    return dup_cstr(keys[selector % (sizeof(keys) / sizeof(keys[0]))]);
}

static char *choose_known_nested_key(uint8_t selector) {
    static const char *keys[] = {
        "innerKey",
        "InnerNum",
        "innerFlag",
        "innerArray"
    };
    return dup_cstr(keys[selector % (sizeof(keys) / sizeof(keys[0]))]);
}

static char *choose_lookup_key(const uint8_t **data, size_t *size, int nested_scope) {
    uint8_t mode = 0;
    if (!read_u8(data, size, &mode)) {
        return NULL;
    }

    if ((mode & 3) != 0) {
        return read_key(data, size);
    }

    {
        uint8_t sel = 0;
        if (!read_u8(data, size, &sel)) {
            return NULL;
        }

        if (nested_scope) {
            return choose_known_nested_key(sel);
        }
        return choose_known_root_key(sel);
    }
}

static void maybe_rebuild(const uint8_t **data, size_t *size, cJSON *root) {
    uint8_t control = 0;
    if (!read_u8(data, size, &control)) {
        return;
    }

    if ((control & 7) == 0) {
        rebuild_object(root);
    }
}

static void delete_from_root_object(const uint8_t **data, size_t *size, cJSON *root) {
    char *lookup_key = NULL;
    uint8_t case_mode = 0;
    uint8_t which_api = 0;

    lookup_key = choose_lookup_key(data, size, 0);
    if (lookup_key == NULL) {
        return;
    }
    if (!read_u8(data, size, &case_mode)) {
        free(lookup_key);
        return;
    }
    if (!read_u8(data, size, &which_api)) {
        free(lookup_key);
        return;
    }

    mutate_case(lookup_key, (uint8_t)(case_mode % 4));

    if ((which_api & 1) == 0) {
        cJSON_DeleteItemFromObject(root, lookup_key);
    } else {
        cJSON_DeleteItemFromObjectCaseSensitive(root, lookup_key);
    }

    free(lookup_key);
}

static void delete_from_nested_object(const uint8_t **data, size_t *size, cJSON *root) {
    cJSON *nested = cJSON_GetObjectItem(root, "nestedObj");
    char *lookup_key = NULL;
    uint8_t case_mode = 0;
    uint8_t which_api = 0;

    if (!cJSON_IsObject(nested)) {
        return;
    }

    lookup_key = choose_lookup_key(data, size, 1);
    if (lookup_key == NULL) {
        return;
    }
    if (!read_u8(data, size, &case_mode)) {
        free(lookup_key);
        return;
    }
    if (!read_u8(data, size, &which_api)) {
        free(lookup_key);
        return;
    }

    mutate_case(lookup_key, (uint8_t)(case_mode % 4));

    if ((which_api & 1) == 0) {
        cJSON_DeleteItemFromObject(nested, lookup_key);
    } else {
        cJSON_DeleteItemFromObjectCaseSensitive(nested, lookup_key);
    }

    free(lookup_key);
}

static void delete_from_root_array(const uint8_t **data, size_t *size, cJSON *root) {
    cJSON *arr = cJSON_GetObjectItem(root, "ArrayKey");
    int index = 0;

    if (!cJSON_IsArray(arr)) {
        return;
    }
    if (!read_u8_as_int(data, size, &index)) {
        return;
    }

    index = (index % 8) - 2;
    cJSON_DeleteItemFromArray(arr, index);
}

static void delete_from_nested_array(const uint8_t **data, size_t *size, cJSON *root) {
    cJSON *nested = cJSON_GetObjectItem(root, "nestedObj");
    cJSON *inner_arr = NULL;
    int index = 0;

    if (!cJSON_IsObject(nested)) {
        return;
    }

    inner_arr = cJSON_GetObjectItem(nested, "innerArray");
    if (!cJSON_IsArray(inner_arr)) {
        return;
    }
    if (!read_u8_as_int(data, size, &index)) {
        return;
    }

    index = (index % 8) - 2;
    cJSON_DeleteItemFromArray(inner_arr, index);
}

static void detach_then_delete_from_root(const uint8_t **data, size_t *size, cJSON *root) {
    char *lookup_key = NULL;
    uint8_t case_mode = 0;
    uint8_t which_api = 0;
    cJSON *detached = NULL;

    lookup_key = choose_lookup_key(data, size, 0);
    if (lookup_key == NULL) {
        return;
    }
    if (!read_u8(data, size, &case_mode)) {
        free(lookup_key);
        return;
    }
    if (!read_u8(data, size, &which_api)) {
        free(lookup_key);
        return;
    }

    mutate_case(lookup_key, (uint8_t)(case_mode % 4));

    if ((which_api & 1) == 0) {
        detached = cJSON_DetachItemFromObject(root, lookup_key);
    } else {
        detached = cJSON_DetachItemFromObjectCaseSensitive(root, lookup_key);
    }

    cJSON_Delete(detached);
    free(lookup_key);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (data == NULL || size == 0) {
        return 0;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return 0;
    }

    seed_object(root);

    {
        uint8_t raw_ops = 0;
        if (!read_u8(&data, &size, &raw_ops)) {
            cJSON_Delete(root);
            return 0;
        }

        {
            size_t op_count = (size_t)(raw_ops % 48);

            for (size_t i = 0; i < op_count && size > 0; ++i) {
                uint8_t op_kind = 0;
                if (!read_u8(&data, &size, &op_kind)) {
                    break;
                }

                switch (op_kind % 5) {
                    case 0:
                        delete_from_root_object(&data, &size, root);
                        break;

                    case 1:
                        delete_from_nested_object(&data, &size, root);
                        break;

                    case 2:
                        delete_from_root_array(&data, &size, root);
                        break;

                    case 3:
                        delete_from_nested_array(&data, &size, root);
                        break;

                    case 4:
                        detach_then_delete_from_root(&data, &size, root);
                        break;

                    default:
                        break;
                }

                maybe_rebuild(&data, &size, root);
            }
        }
    }

    {
        char *printed = cJSON_PrintUnformatted(root);
        if (printed != NULL) {
            cJSON *parsed = cJSON_Parse(printed);
            if (parsed != NULL) {
                cJSON_Delete(parsed);
            }
            free(printed);
        }
    }

    cJSON_Delete(root);
    return 0;
}

#ifdef __cplusplus
}
#endif