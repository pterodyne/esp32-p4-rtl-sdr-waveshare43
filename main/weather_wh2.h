#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fine Offset WH2 only (0xff preamble, 48 bits). No tuner or USB control.
 * temperature_tenths_c is signed magnitude in tenths of a degree Celsius. */
typedef struct {
    uint8_t id;
    int16_t temperature_tenths_c;
    uint8_t humidity;
} weather_wh2_packet_t;

/* Includes 0xff preamble and CRC-8 byte (poly 0x31, initial 0). */
bool weather_wh2_decode(const uint8_t bytes[6], weather_wh2_packet_t *packet);

#define WEATHER_WH2_FILTER_MAX 160
typedef struct {
    uint32_t sample_rate;
    uint32_t power[WEATHER_WH2_FILTER_MAX];
    uint32_t power_sum;
    uint32_t noise_power, peak_power; /* learned after IQ channel filtering */
    int32_t dc_i, dc_q, lp_i, lp_q; /* Q8: remove RTL DC and isolate the tuned center */
    unsigned filter_size, filter_pos, filter_count;
    unsigned pulse_samples, quiet_samples, bits;
    uint8_t frame[6];
    bool in_pulse;
    bool level_ready;
} weather_wh2_stream_t;

/* Accepts interleaved unsigned 8-bit I/Q at 250 kS/s or 2.4 MS/s.
 * byte_count must be even; no partial IQ sample is retained. Output capacity
 * bounds writes. Reset after lost IQ samples or a retune. A packet emits after
 * a trailing quiet interval (>1.2 ms), including across arbitrary CU8 chunks. */
bool weather_wh2_stream_init(weather_wh2_stream_t *stream, uint32_t sample_rate);
void weather_wh2_stream_reset(weather_wh2_stream_t *stream);
size_t weather_wh2_stream_feed(weather_wh2_stream_t *stream, const uint8_t *cu8,
                               size_t byte_count, weather_wh2_packet_t *out,
                               size_t capacity);
