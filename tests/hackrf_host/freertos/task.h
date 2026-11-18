#pragma once
#include <stddef.h>
static inline int xTaskCreatePinnedToCore(void(*f)(void*),const char*n,int s,void*a,int p,void*h,int c){(void)f;(void)n;(void)s;(void)a;(void)p;(void)h;(void)c;return 0;}
static inline void vTaskDelete(void *p){(void)p;}
static inline void vTaskDelay(int x){(void)x;}
