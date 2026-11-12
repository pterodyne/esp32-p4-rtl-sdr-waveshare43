/* Fine Offset WH2 433.92 MHz OOK/PWM decoder. GPL-3.0-or-later.
 * Protocol timing and frame layout from rtl_433 fineoffset.c (GPL-2.0-or-later).
 * This decoder is an independent implementation, without rtl_433 code. */
#include "weather_wh2.h"
#include <string.h>

static uint8_t wh2_crc(const uint8_t *data, size_t count)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < count; ++i) {
        crc ^= data[i];
        for (unsigned j = 0; j < 8; ++j)
            crc = (uint8_t)((crc << 1) ^ ((crc & 0x80) ? 0x31 : 0));
    }
    return crc;
}

bool weather_wh2_decode(const uint8_t bytes[6], weather_wh2_packet_t *packet)
{
    if (!bytes || !packet || bytes[0] != 0xff || (bytes[1] >> 4) != 4 ||
        wh2_crc(bytes + 1, 4) != bytes[5])
        return false;
    unsigned temp = ((unsigned)(bytes[2] & 15) << 8) | bytes[3];
    int temp_tenths = (temp & 0x800) ? -(int)(temp & 0x7ff) : (int)temp;
    if (temp_tenths < -500 || temp_tenths > 700 || bytes[4] > 100)
        return false; /* reject valid-CRC noise outside plausible WH2 readings */
    packet->id = (uint8_t)(((bytes[1] & 15) << 4) | (bytes[2] >> 4));
    packet->temperature_tenths_c = (int16_t)temp_tenths;
    packet->humidity = bytes[4];
    return true;
}

bool weather_wh2_stream_init(weather_wh2_stream_t *stream, uint32_t sample_rate)
{
    if (!stream || (sample_rate != 250000 && sample_rate != 2400000)) return false;
    memset(stream, 0, sizeof *stream);
    stream->sample_rate = sample_rate;
    stream->filter_size = sample_rate / 15625; /* 64 us moving power average */
    return true;
}

void weather_wh2_stream_reset(weather_wh2_stream_t *stream)
{
    if (!stream) return;
    uint32_t rate = stream->sample_rate;
    (void)weather_wh2_stream_init(stream, rate);
}

static unsigned samples_us(const weather_wh2_stream_t *stream, unsigned us)
{
    return (unsigned)(((uint64_t)stream->sample_rate * us) / 1000000);
}

size_t weather_wh2_stream_feed(weather_wh2_stream_t *stream, const uint8_t *cu8,
                               size_t byte_count, weather_wh2_packet_t *out,
                               size_t capacity)
{
    if (!stream || !stream->sample_rate || !cu8 || (byte_count & 1) ||
        (capacity && !out)) return 0;
    size_t emitted = 0;
    unsigned end_gap = samples_us(stream, 1200);
    unsigned short_min = samples_us(stream, 320);
    unsigned short_max = samples_us(stream, 750);
    unsigned long_min = samples_us(stream, 1250);
    unsigned long_max = samples_us(stream, 1800);
    for (size_t i = 0; i < byte_count; i += 2) {
        int iv = (int)cu8[i] - 128, qv = (int)cu8[i + 1] - 128;
        int dc_div = stream->sample_rate == 250000 ? 2048 : 32768;
        int lp_div = stream->sample_rate == 250000 ? 1 : 4;
        stream->dc_i += (iv * 256 - stream->dc_i) / dc_div;
        stream->dc_q += (qv * 256 - stream->dc_q) / dc_div;
        stream->lp_i += (iv * 256 - stream->dc_i - stream->lp_i) / lp_div;
        stream->lp_q += (qv * 256 - stream->dc_q - stream->lp_q) / lp_div;
        iv = stream->lp_i / 256;
        qv = stream->lp_q / 256;
        uint32_t power = (uint32_t)(iv * iv + qv * qv);
        stream->power_sum -= stream->power[stream->filter_pos];
        stream->power_sum += power;
        stream->power[stream->filter_pos] = power;
        if (++stream->filter_pos == stream->filter_size) stream->filter_pos = 0;
        if (stream->filter_count < stream->filter_size) ++stream->filter_count;
        if (stream->filter_count < stream->filter_size) continue;
        uint32_t average = stream->power_sum / stream->filter_size;
        if (!stream->level_ready) {
            stream->noise_power = average;
            stream->peak_power = average;
            stream->level_ready = true;
        }
        if (average > stream->peak_power) stream->peak_power = average;
        else if (stream->peak_power > stream->noise_power) {
            uint32_t decay = (stream->peak_power - stream->noise_power) / 8192 + 1;
            stream->peak_power -= decay;
        }
        if (!stream->in_pulse) {
            if (average < stream->noise_power) {
                uint32_t delta = (stream->noise_power - average) / 64 + 1;
                stream->noise_power -= delta;
            } else if (average < stream->noise_power * 2 + 100) {
                stream->noise_power += (average - stream->noise_power) / 256;
            }
        }
        if (stream->peak_power < stream->noise_power) stream->peak_power = stream->noise_power;
        uint32_t margin = (stream->peak_power - stream->noise_power) / 4;
        if (margin < (stream->sample_rate == 250000 ? 64u : 600u))
            margin = stream->sample_rate == 250000 ? 64u : 600u;
        bool on = average > stream->noise_power + margin;
        if (on) {
            stream->quiet_samples = 0;
            if (!stream->in_pulse) {
                stream->in_pulse = true;
                stream->pulse_samples = 0;
            }
            ++stream->pulse_samples;
        } else if (stream->in_pulse) {
            stream->in_pulse = false;
            unsigned n = stream->pulse_samples;
            int bit = (n >= short_min && n <= short_max) ? 1 :
                      (n >= long_min && n <= long_max) ? 0 : -1;
            if (bit < 0 || stream->bits == 48) {
                stream->bits = 0;
                memset(stream->frame, 0, sizeof stream->frame);
            } else {
                if (bit) stream->frame[stream->bits / 8] |=
                             (uint8_t)(1u << (7 - stream->bits % 8));
                ++stream->bits;
            }
            stream->quiet_samples = 1;
        } else if (stream->quiet_samples < end_gap + 1) {
            if (++stream->quiet_samples == end_gap && stream->bits) {
                if (stream->bits == 48 && emitted < capacity &&
                    weather_wh2_decode(stream->frame, &out[emitted])) ++emitted;
                stream->bits = 0;
                memset(stream->frame, 0, sizeof stream->frame);
            }
        }
    }
    return emitted;
}
