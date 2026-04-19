#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// Find a reasonable split point for two JSON inputs
static size_t find_split(const uint8_t *data, size_t size) {
    for (size_t i = 1; i < size - 1; i++) {
        if (data[i] == '|' || data[i] == '\n') {
            return i;
        }
    }
    return size / 2;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (data == NULL || size < 2) {
        return 0;
    }

    // --- Step 1: split input into two JSON strings ---
    size_t split = find_split(data, size);

    if (split == 0 || split >= size - 1) {
        return 0;
    }

    size_t len1 = split;
    size_t len2 = size - split - 1;

    const uint8_t *part1 = data;
    const uint8_t *part2 = data + split + 1;

    // --- Step 2: allocate buffers ---
    char *buf1 = (char *)malloc(len1 + 1);
    char *buf2 = (char *)malloc(len2 + 1);
    if (!buf1 || !buf2) {
        free(buf1);
        free(buf2);
        return 0;
    }

    memcpy(buf1, part1, len1);
    buf1[len1] = '\0';

    memcpy(buf2, part2, len2);
    buf2[len2] = '\0';

    // --- Step 3: parse both JSON objects ---
    cJSON *obj1 = cJSON_Parse(buf1);
    cJSON *obj2 = cJSON_Parse(buf2);

    // --- Step 4: core target — compare ---
    if (obj1 && obj2) {
        // strict comparison
        cJSON_Compare(obj1, obj2, 1);

        // non-strict comparison (different code path)
        cJSON_Compare(obj1, obj2, 0);
    }

    // --- Step 5: cleanup ---
    if (obj1) cJSON_Delete(obj1);
    if (obj2) cJSON_Delete(obj2);

    free(buf1);
    free(buf2);

    return 0;
}

#ifdef __cplusplus
}
#endif