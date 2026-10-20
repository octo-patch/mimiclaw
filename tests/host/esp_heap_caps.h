#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
static inline void *heap_caps_calloc(size_t n, size_t size, int caps) { (void)caps; return calloc(n, size); }
static inline void *heap_caps_realloc(void *ptr, size_t size, int caps) { (void)caps; return realloc(ptr, size); }
