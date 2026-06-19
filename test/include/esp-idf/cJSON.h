/*!
 * @file
 * @brief cJSON stubs for test/simulation builds.
 *
 * Minimal subset of the cJSON API used by ha_discovery_manager.cpp.
 */

#ifndef CJSON_STUB_H
#define CJSON_STUB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    cJSON_Invalid = 0,
    cJSON_False,
    cJSON_True,
    cJSON_Number,
    cJSON_String,
    cJSON_Array,
    cJSON_Object,
    cJSON_Raw
} cJSON_Type;

typedef struct cJSON {
    struct cJSON* next;
    struct cJSON* prev;
    struct cJSON* child;
    cJSON_Type type;
    char* valuestring;
    int valueint;
    double valuedouble;
    char* string;
} cJSON;

static inline cJSON* cJSON_Parse(const char* str) {
    if (!str) return NULL;
    cJSON* node = (cJSON*)malloc(sizeof(cJSON));
    if (!node) return NULL;
    node->type = cJSON_Object;
    node->valuestring = NULL;
    node->string = NULL;
    node->child = NULL;
    node->next = NULL;
    node->prev = NULL;
    node->valueint = 0;
    node->valuedouble = 0;
    return node;
}

static inline void cJSON_Delete(cJSON* item) {
    if (item) free(item);
}

static inline cJSON* cJSON_GetObjectItemCaseSensitive(cJSON* const obj, const char* string) {
    (void)obj; (void)string;
    return NULL;
}

static inline int cJSON_IsString(cJSON* const item) {
    return item && item->type == cJSON_String;
}

static inline int cJSON_IsNumber(cJSON* const item) {
    return item && item->type == cJSON_Number;
}

#ifdef __cplusplus
}
#endif

#endif /* CJSON_STUB_H */
