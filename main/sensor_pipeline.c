#include "sensor_pipeline.h"

bool sensor_pipeline_init(sensor_pipeline_t *pipeline, uint32_t sample_rate)
{
    return pipeline && weather_wh2_stream_init(&pipeline->weather, sample_rate) &&
           tpms_steelmate_stream_init(&pipeline->tpms, sample_rate);
}

void sensor_pipeline_reset(sensor_pipeline_t *pipeline)
{
    if (!pipeline) return;
    weather_wh2_stream_reset(&pipeline->weather);
    tpms_steelmate_stream_reset(&pipeline->tpms);
}

size_t sensor_pipeline_feed(sensor_pipeline_t *pipeline, const uint8_t *iq,
                            size_t bytes, sensor_packet_t *out, size_t capacity)
{
    if (!pipeline || !iq || (bytes & 1) || (capacity && !out)) return 0;
    weather_wh2_packet_t wh2[4];
    tpms_steelmate_packet_t tpms[4];
    size_t nwh = weather_wh2_stream_feed(&pipeline->weather, iq, bytes, wh2, 4);
    size_t ntp = tpms_steelmate_stream_feed(&pipeline->tpms, iq, bytes, tpms, 4);
    size_t emitted = 0;
    for (size_t i = 0; i < nwh && emitted < capacity; i++) {
        out[emitted].type = SENSOR_WH2;
        out[emitted++].weather = wh2[i];
    }
    for (size_t i = 0; i < ntp && emitted < capacity; i++) {
        out[emitted].type = SENSOR_TPMS;
        out[emitted++].tpms = tpms[i];
    }
    return emitted;
}
