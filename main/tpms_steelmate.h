#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Steelmate TP-S15 family only. Caller supplies CU8 I/Q from the existing SDR ring.
 * Recording fixture: https://github.com/merbanan/rtl_433_tests/tree/master/tests/steelmate-TPMS/01
 * At 2.4 MS/s tune the receiver near 433.92 MHz; no tuner/USB control occurs here. */
typedef struct {
    uint16_t id;
    float pressure_kpa;
    int temperature_c;
    int battery_mv; /* -1 when alarm is present */
    enum { TPMS_STEELMATE_NO_ALARM, TPMS_STEELMATE_SLOW_LEAK,
           TPMS_STEELMATE_FAST_LEAK } alarm;
} tpms_steelmate_packet_t;

/* Nine translated bytes: 00 00 01 IDhi IDlo pressure temp+50 battery checksum.
 * Checksum is the byte sum of positions 2..7 modulo 256. */
bool tpms_steelmate_decode(const uint8_t bytes[9], tpms_steelmate_packet_t *packet);

#define TPMS_STEELMATE_BURST_SAMPLES 1200
typedef struct {
    uint32_t sample_rate, decimation, decim_count;
    int32_t dc_i, dc_q; /* Q16 slow IQ DC estimator: tuner offset is not a burst */
    int prev_i, prev_q;
    bool prev_valid, in_burst;
    float cross, dot, power;
    unsigned quiet, count;
    float freq[TPMS_STEELMATE_BURST_SAMPLES];
} tpms_steelmate_stream_t;

/* Supports 250 kS/s (legacy rtl_433 fixture) and 2.4 MS/s (existing SDR).
 * Even-length CU8 chunks only. A burst emits after trailing quiet samples; output
 * capacity bounds writes. Feed at least 18 decimated bins of {128,128}
 * silence (36 complex samples at 250 kS/s, 432 at 2.4 MS/s) to flush.
 * Reset after a lost SDR-ring span or a retune (never join across a gap). */
bool tpms_steelmate_stream_init(tpms_steelmate_stream_t *stream, uint32_t sample_rate);
void tpms_steelmate_stream_reset(tpms_steelmate_stream_t *stream);
size_t tpms_steelmate_stream_feed(tpms_steelmate_stream_t *stream, const uint8_t *cu8,
                                  size_t byte_count, tpms_steelmate_packet_t *out,
                                  size_t capacity);
