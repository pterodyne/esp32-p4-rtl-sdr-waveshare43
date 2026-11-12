#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "weather_wh2.h"
#include "tpms_steelmate.h"

typedef enum { SENSOR_WH2, SENSOR_TPMS } sensor_packet_type_t;
typedef struct {
    sensor_packet_type_t type;
    weather_wh2_packet_t weather;
    tpms_steelmate_packet_t tpms;
} sensor_packet_t;
typedef struct {
    weather_wh2_stream_t weather;
    tpms_steelmate_stream_t tpms;
} sensor_pipeline_t;

/* The caller owns one cursor on the existing CU8 ring; no USB calls here.
 * Reset after any dropped bytes, stream restart or retune, to reject split frames.
 * Output capacity bounds writes; both decoders advance even when output fills. */
bool sensor_pipeline_init(sensor_pipeline_t *pipeline, uint32_t sample_rate);
void sensor_pipeline_reset(sensor_pipeline_t *pipeline);
size_t sensor_pipeline_feed(sensor_pipeline_t *pipeline, const uint8_t *iq,
                            size_t bytes, sensor_packet_t *out, size_t capacity);
