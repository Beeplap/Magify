#ifndef MG_ARENA_H
#define MG_ARENA_H

#include "mg_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mg_arena {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
} mg_arena_t;

mg_arena_t* mg_arena_create(size_t capacity);
void*       mg_arena_alloc(mg_arena_t *arena, size_t size);
void*       mg_arena_alloc_zero(mg_arena_t *arena, size_t size);
void        mg_arena_reset(mg_arena_t *arena);
void        mg_arena_destroy(mg_arena_t *arena);

// String conversions
wchar_t* mg_utf8_to_utf16(mg_arena_t *arena, const char *u8_str);
char*    mg_utf16_to_utf8(mg_arena_t *arena, const wchar_t *w_str);

#ifdef __cplusplus
}
#endif

#endif // MG_ARENA_H
