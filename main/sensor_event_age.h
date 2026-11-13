#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SENSOR_EVENT_TTL_US UINT64_C(60000000)

static inline bool sensor_event_expired(uint64_t seen_us, uint64_t now_us)
{
    return now_us < seen_us || now_us - seen_us >= SENSOR_EVENT_TTL_US;
}
