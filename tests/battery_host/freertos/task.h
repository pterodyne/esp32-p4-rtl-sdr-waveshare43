void vTaskDelay(int);
void vTaskDelete(void*);
int xTaskCreate(void(*)(void*),const char*,int,void*,int,void*);
