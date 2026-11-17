#pragma once
#include "FreeRTOS.h"
typedef struct queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueReceive(QueueHandle_t,void *,unsigned);
int xQueueSend(QueueHandle_t,const void *,unsigned);
void vQueueDelete(QueueHandle_t);
