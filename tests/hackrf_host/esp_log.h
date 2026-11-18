#pragma once
#include <stdio.h>
#define ESP_LOGI(tag,...) ((void)(tag), (void)printf(__VA_ARGS__))
#define ESP_LOGW(tag,...) ((void)(tag), (void)printf(__VA_ARGS__))
#define ESP_LOGE(tag,...) ((void)(tag), (void)printf(__VA_ARGS__))
