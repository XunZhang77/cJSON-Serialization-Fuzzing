#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!data || size == 0) return 0;

    char *input = (char *)malloc(size + 1);
    if (!input) return 0;

    memcpy(input, data, size);
    input[size] = '\0';

    cJSON *root = cJSON_Parse(input);
    free(input);

    if (!root) return 0;

    int recurse = (size > 0) ? (data[0] & 1) : 0;

    cJSON *dup = cJSON_Duplicate(root, recurse);
    if (dup) {
        char *out = cJSON_PrintUnformatted(dup);
        free(out);

        if (size > 1 && (data[1] & 1)) {
            cJSON *dup2 = cJSON_Duplicate(dup, 1);
            cJSON_Delete(dup2);
        }

        cJSON_Delete(dup);
    }

    cJSON_Delete(root);
    return 0;
}
#ifdef __cplusplus
}
#endif