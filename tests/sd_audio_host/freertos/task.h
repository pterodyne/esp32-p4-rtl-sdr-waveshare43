#pragma once
#include "FreeRTOS.h"
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *,unsigned,void *,int,void *,int);
void vTaskDelay(unsigned);
