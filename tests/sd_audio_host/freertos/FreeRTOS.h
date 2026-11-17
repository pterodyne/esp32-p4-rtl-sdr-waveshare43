#pragma once
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define taskENTER_CRITICAL(p) pthread_mutex_lock(p)
#define taskEXIT_CRITICAL(p) pthread_mutex_unlock(p)
#define pdMS_TO_TICKS(n) (n)
#define pdTRUE 1
#define pdPASS 1
