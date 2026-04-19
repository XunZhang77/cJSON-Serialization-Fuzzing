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

    uint8_t flags = data[0];
    const char *json = (const char *)(data + 1);
    size_t json_len = size - 1;

    char *buf = (char *)malloc(json_len + 1);
    if (!buf) return 0;

    memcpy(buf, json, json_len);
    buf[json_len] = '\0';

    cJSON *root = cJSON_Parse(buf);

    if (root) {
        if (flags & 1) {
            char *printed = cJSON_Print(root);
            if (printed) free(printed);
        }

        if (flags & 2) {
            char *printed = cJSON_PrintUnformatted(root);
            if (printed) free(printed);
        }

        if (flags & 4) {
            cJSON_Minify(buf);
            cJSON *r2 = cJSON_Parse(buf);
            if (r2) cJSON_Delete(r2);
        }

        cJSON_Delete(root);
    }

    free(buf);
    return 0;
}

#ifdef __cplusplus
}
#endif