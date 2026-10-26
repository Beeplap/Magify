#include "mg_arena.h"
#include <stdlib.h>
#include <string.h>

mg_arena_t* mg_arena_create(size_t capacity) {
    if (capacity == 0) {
        capacity = 1024 * 1024 * 4; // default 4MB scratch arena
    }
    mg_arena_t *arena = (mg_arena_t*)malloc(sizeof(mg_arena_t));
    if (!arena) return NULL;

    arena->buffer = (uint8_t*)malloc(capacity);
    if (!arena->buffer) {
        free(arena);
        return NULL;
    }
    arena->capacity = capacity;
    arena->offset = 0;
    return arena;
}

void* mg_arena_alloc(mg_arena_t *arena, size_t size) {
    if (!arena) return NULL;
    // Align to 8-byte boundary
    size_t aligned_size = (size + 7) & ~((size_t)7);
    if (arena->offset + aligned_size > arena->capacity) {
        return NULL; // Out of arena space
    }
    void *ptr = arena->buffer + arena->offset;
    arena->offset += aligned_size;
    return ptr;
}

void* mg_arena_alloc_zero(mg_arena_t *arena, size_t size) {
    void *ptr = mg_arena_alloc(arena, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void mg_arena_reset(mg_arena_t *arena) {
    if (arena) {
        arena->offset = 0;
    }
}

void mg_arena_destroy(mg_arena_t *arena) {
    if (arena) {
        if (arena->buffer) {
            free(arena->buffer);
            arena->buffer = NULL;
        }
        free(arena);
    }
}

wchar_t* mg_utf8_to_utf16(mg_arena_t *arena, const char *u8_str) {
    if (!u8_str) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, u8_str, -1, NULL, 0);
    if (len <= 0) return NULL;

    wchar_t *w_buf = (wchar_t*)mg_arena_alloc(arena, (size_t)len * sizeof(wchar_t));
    if (!w_buf) return NULL;

    MultiByteToWideChar(CP_UTF8, 0, u8_str, -1, w_buf, len);
    return w_buf;
}

char* mg_utf16_to_utf8(mg_arena_t *arena, const wchar_t *w_str) {
    if (!w_str) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, w_str, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;

    char *u8_buf = (char*)mg_arena_alloc(arena, (size_t)len);
    if (!u8_buf) return NULL;

    WideCharToMultiByte(CP_UTF8, 0, w_str, -1, u8_buf, len, NULL, NULL);
    return u8_buf;
}
