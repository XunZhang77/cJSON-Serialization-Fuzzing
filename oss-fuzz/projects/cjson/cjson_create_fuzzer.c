#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_ARRAY_COUNT 64
#define MAX_STRING_LEN  32

static int read_u8(const uint8_t **data, size_t *size, uint8_t *out)
{
    if (*size < 1) {
        return 0;
    }
    *out = **data;
    (*data)++;
    (*size)--;
    return 1;
}

static int read_bytes(const uint8_t **data, size_t *size, uint8_t *out, size_t n)
{
    if (*size < n) {
        return 0;
    }
    memcpy(out, *data, n);
    (*data) += n;
    (*size) -= n;
    return 1;
}

static int read_i32(const uint8_t **data, size_t *size, int *out)
{
    union {
        uint32_t u32;
        int i32;
    } conv;

    conv.u32 = 0;

    if (*size >= sizeof(uint32_t)) {
        if (!read_bytes(data, size, (uint8_t *)&conv.u32, sizeof(uint32_t))) {
            return 0;
        }
    } else {
        size_t n = *size;
        if (n > 0) {
            memcpy(&conv.u32, *data, n);
            (*data) += n;
            (*size) -= n;
        }
    }

    *out = conv.i32;
    return 1;
}

static int read_float(const uint8_t **data, size_t *size, float *out)
{
    union {
        uint32_t u32;
        float f;
    } conv;

    conv.u32 = 0;

    if (*size >= sizeof(uint32_t)) {
        if (!read_bytes(data, size, (uint8_t *)&conv.u32, sizeof(uint32_t))) {
            return 0;
        }
    } else {
        size_t n = *size;
        if (n > 0) {
            memcpy(&conv.u32, *data, n);
            (*data) += n;
            (*size) -= n;
        }
    }

    *out = conv.f;
    return 1;
}

static int read_double(const uint8_t **data, size_t *size, double *out)
{
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

static char *read_string(const uint8_t **data, size_t *size)
{
    uint8_t raw_len = 0;
    size_t len = 0;
    char *s = NULL;

    if (!read_u8(data, size, &raw_len)) {
        return NULL;
    }

    len = (size_t)(raw_len % MAX_STRING_LEN);
    if (*size < len) {
        len = *size;
    }

    s = (char *)malloc(len + 1);
    if (s == NULL) {
        return NULL;
    }

    if (len > 0) {
        memcpy(s, *data, len);
        (*data) += len;
        (*size) -= len;
    }
    s[len] = '\0';

    /* Avoid embedded NUL truncation effects from the raw input. */
    for (size_t i = 0; i < len; ++i) {
        if (s[i] == '\0') {
            s[i] = 'A';
        }
    }

    return s;
}

static void exercise_created_array(cJSON *arr)
{
    char *printed = NULL;
    cJSON *parsed = NULL;

    if (arr == NULL) {
        return;
    }

    /* Touch traversal helpers too. */
    (void)cJSON_GetArraySize(arr);
    if (cJSON_GetArraySize(arr) > 0) {
        (void)cJSON_GetArrayItem(arr, 0);
        (void)cJSON_GetArrayItem(arr, cJSON_GetArraySize(arr) - 1);
    }

    printed = cJSON_PrintUnformatted(arr);
    if (printed != NULL) {
        parsed = cJSON_Parse(printed);
        if (parsed != NULL) {
            cJSON_Delete(parsed);
        }
        free(printed);
    }
}

static cJSON *fuzz_create_string_array(const uint8_t *data, size_t size)
{
    uint8_t raw_count = 0;
    int count = 0;
    char **strings = NULL;
    cJSON *result = NULL;

    if (!read_u8(&data, &size, &raw_count)) {
        return NULL;
    }

    count = (int)(raw_count % MAX_ARRAY_COUNT);

    /*
     * cJSON supports empty arrays for these helpers in current upstream,
     * so count==0 is a valid and useful case.
     */
    if (count == 0) {
        return cJSON_CreateStringArray((const char * const *)NULL, 0);
    }

    strings = (char **)calloc((size_t)count, sizeof(char *));
    if (strings == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        strings[i] = read_string(&data, &size);
        if (strings[i] == NULL) {
            strings[i] = (char *)malloc(1);
            if (strings[i] == NULL) {
                count = i;
                break;
            }
            strings[i][0] = '\0';
        }
    }

    result = cJSON_CreateStringArray((const char * const *)strings, count);

    for (int i = 0; i < count; ++i) {
        free(strings[i]);
    }
    free(strings);

    return result;
}

static cJSON *fuzz_create_double_array(const uint8_t *data, size_t size)
{
    uint8_t raw_count = 0;
    int count = 0;
    double *numbers = NULL;
    cJSON *result = NULL;

    if (!read_u8(&data, &size, &raw_count)) {
        return NULL;
    }

    count = (int)(raw_count % MAX_ARRAY_COUNT);

    if (count == 0) {
        return cJSON_CreateDoubleArray(NULL, 0);
    }

    numbers = (double *)calloc((size_t)count, sizeof(double));
    if (numbers == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        (void)read_double(&data, &size, &numbers[i]);
    }

    result = cJSON_CreateDoubleArray(numbers, count);
    free(numbers);
    return result;
}

static cJSON *fuzz_create_float_array(const uint8_t *data, size_t size)
{
    uint8_t raw_count = 0;
    int count = 0;
    float *numbers = NULL;
    cJSON *result = NULL;

    if (!read_u8(&data, &size, &raw_count)) {
        return NULL;
    }

    count = (int)(raw_count % MAX_ARRAY_COUNT);

    if (count == 0) {
        return cJSON_CreateFloatArray(NULL, 0);
    }

    numbers = (float *)calloc((size_t)count, sizeof(float));
    if (numbers == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        (void)read_float(&data, &size, &numbers[i]);
    }

    result = cJSON_CreateFloatArray(numbers, count);
    free(numbers);
    return result;
}

static cJSON *fuzz_create_int_array(const uint8_t *data, size_t size)
{
    uint8_t raw_count = 0;
    int count = 0;
    int *numbers = NULL;
    cJSON *result = NULL;

    if (!read_u8(&data, &size, &raw_count)) {
        return NULL;
    }

    count = (int)(raw_count % MAX_ARRAY_COUNT);

    if (count == 0) {
        return cJSON_CreateIntArray(NULL, 0);
    }

    numbers = (int *)calloc((size_t)count, sizeof(int));
    if (numbers == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        (void)read_i32(&data, &size, &numbers[i]);
    }

    result = cJSON_CreateIntArray(numbers, count);
    free(numbers);
    return result;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t selector = 0;
    cJSON *arr = NULL;

    if (data == NULL || size == 0) {
        return 0;
    }

    selector = *data++;
    size--;

    switch (selector % 4) {
        case 0:
            arr = fuzz_create_string_array(data, size);
            break;
        case 1:
            arr = fuzz_create_double_array(data, size);
            break;
        case 2:
            arr = fuzz_create_float_array(data, size);
            break;
        case 3:
        default:
            arr = fuzz_create_int_array(data, size);
            break;
    }

    exercise_created_array(arr);
    cJSON_Delete(arr);
    return 0;
}

#ifdef __cplusplus
}
#endif
