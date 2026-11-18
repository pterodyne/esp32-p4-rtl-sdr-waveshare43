#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_free(p) free(p)
#define heap_caps_malloc(n,c) malloc(n)
