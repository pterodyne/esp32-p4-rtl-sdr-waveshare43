#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
static inline void *heap_caps_calloc(size_t n,size_t s,int c){(void)c;return calloc(n,s);}
