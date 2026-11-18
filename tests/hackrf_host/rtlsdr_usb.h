#pragma once
#include <stdbool.h>
#include <stdint.h>
#define ESP_OK 0
int rtlsdr_usb_start(void);
bool rtlsdr_usb_wait_new_device(uint32_t ms);
