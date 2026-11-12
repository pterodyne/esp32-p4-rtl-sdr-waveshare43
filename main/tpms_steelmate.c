#include "tpms_steelmate.h"
#include <math.h>
#include <string.h>

bool tpms_steelmate_decode(const uint8_t bytes[9], tpms_steelmate_packet_t *packet)
{
    if (!bytes || !packet || bytes[0] || bytes[1] || bytes[2] != 1) return false;
    unsigned sum = 0;
    for (unsigned i = 2; i < 8; i++) sum += bytes[i];
    if ((uint8_t)sum != bytes[8]) return false;
    packet->id = (uint16_t)((bytes[3] << 8) | bytes[4]);
    packet->pressure_kpa = bytes[5] * 3.125f;
    packet->temperature_c = bytes[6] - 50;
    packet->battery_mv = bytes[7] >= 0xfe ? -1 : 3900 - bytes[7] * 10;
    packet->alarm = bytes[7] == 0xff ? TPMS_STEELMATE_FAST_LEAK :
                    bytes[7] == 0xfe ? TPMS_STEELMATE_SLOW_LEAK : TPMS_STEELMATE_NO_ALARM;
    return true;
}

bool tpms_steelmate_stream_init(tpms_steelmate_stream_t *s, uint32_t rate)
{
    if (!s || (rate != 250000 && rate != 2400000)) return false;
    memset(s, 0, sizeof(*s));
    s->sample_rate = rate;
    s->decimation = rate == 250000 ? 2 : 24;
    return true;
}

void tpms_steelmate_stream_reset(tpms_steelmate_stream_t *s)
{
    if (!s) return;
    uint32_t rate = s->sample_rate;
    if (rate == 250000 || rate == 2400000) (void)tpms_steelmate_stream_init(s, rate);
}

static uint8_t reflect8(uint8_t x)
{
    x = (uint8_t)((x >> 4) | (x << 4));
    x = (uint8_t)(((x & 0xcc) >> 2) | ((x & 0x33) << 2));
    return (uint8_t)(((x & 0xaa) >> 1) | ((x & 0x55) << 1));
}

/* Each 50 us FSK chip supplies one Manchester half-bit. Search the burst's
 * chip phase, bit polarity and line polarity, but require preamble AND MIC. */
static bool parse_burst(const tpms_steelmate_stream_t *s, tpms_steelmate_packet_t *out)
{
    const float bins_per_chip = 0.00005f * s->sample_rate / s->decimation;
    if (s->count < (unsigned)(140.f * bins_per_chip)) return false;
    float lo = 125000.f, hi = -125000.f;
    for (unsigned j = 0; j < s->count; j++) {
        float f = s->freq[j];
        /* Reject discriminator spikes at low-power burst edges. Tuner must be
         * within ~40 kHz of the signal for this narrow decoder. */
        if (f < -40000.f || f > 40000.f) continue;
        if (f < lo) lo = f;
        if (f > hi) hi = f;
    }
    if (hi - lo < 3000.f) return false;
    for (unsigned step = 0; step <= 24; step++) {
        float chip = bins_per_chip * (.96f + .005f * step);
        for (unsigned start = 1; start < (unsigned)(chip * 44.f); start++) {
            if (start < (unsigned)(32.f * chip + 2.f)) continue;
            for (int invert = 0; invert < 2; invert++) {
                uint8_t b[9] = {0};
                bool valid = true;
                unsigned manchester_errors = 0;
                /* Payload begins at bit 16. Recover *all* preceding 16 bits
                 * with the same phase/polarity; a sync-high subset is insufficient. */
                for (unsigned bit = 0; bit < 16; bit++) {
                    float offset = (2.f * bit - 32.f) * chip;
                    unsigned a = (unsigned)(start + offset + .5f * chip + .5f);
                    unsigned z = (unsigned)(start + offset + 1.5f * chip + .5f);
                    if (a < 1 || z + 1 >= s->count) { valid = false; break; }
                    float first = s->freq[a - 1] + s->freq[a] + s->freq[a + 1];
                    float second = s->freq[z - 1] + s->freq[z] + s->freq[z + 1];
                    if (((first > second) ^ invert) == 0) { valid = false; break; }
                }
                if (!valid) continue;
                for (unsigned bit = 16; bit < 72; bit++) {
                    unsigned k = bit - 16;
                    unsigned a = (unsigned)(start + (2.f * k + .5f) * chip + .5f);
                    unsigned z = (unsigned)(start + (2.f * k + 1.5f) * chip + .5f);
                    if (z + 1 >= s->count) { valid = false; break; }
                    float first = s->freq[a - 1] + s->freq[a] + s->freq[a + 1];
                    float second = s->freq[z - 1] + s->freq[z] + s->freq[z + 1];
                    if ((first > (lo + hi) * 1.5f) == (second > (lo + hi) * 1.5f))
                        manchester_errors++;
                    b[bit / 8] = (uint8_t)((b[bit / 8] << 1) | ((first > second) ^ invert));
                    if (bit == 23 && b[2] != 0x7f) {
                        /* Both polarities are tried; only one matches 0x01
                         * after inversion and bit reflection. */
                        valid = false; break;
                    }
                }
                if (!valid || manchester_errors > 3) continue;
                /* Anchor polarity against the burst start as well as the
                 * payload phase: a half-chip slip can turn an all-zero prefix
                 * into apparent ones without changing the payload checksum. */
                unsigned sync_highs = 0;
                for (unsigned bit = 2; bit < 11; bit++) {
                    unsigned a = (unsigned)((2.f * bit + .5f) * chip + .5f);
                    unsigned z = (unsigned)((2.f * bit + 1.5f) * chip + .5f);
                    if (z + 1 >= s->count) break;
                    float f1 = s->freq[a - 1] + s->freq[a] + s->freq[a + 1];
                    float f2 = s->freq[z - 1] + s->freq[z] + s->freq[z + 1];
                    if ((f1 > f2) ^ invert) sync_highs++;
                }
                if (sync_highs < 7) continue;
                for (unsigned j = 2; j < 9; j++) b[j] = reflect8((uint8_t)~b[j]);
                if (tpms_steelmate_decode(b, out)) return true;
            }
        }
    }
    return false;
}

size_t tpms_steelmate_stream_feed(tpms_steelmate_stream_t *s, const uint8_t *cu8,
                                  size_t byte_count, tpms_steelmate_packet_t *out,
                                  size_t capacity)
{
    if (!s || !s->decimation || (!cu8 && byte_count) || (byte_count & 1) ||
        (!out && capacity)) return 0;
    size_t emitted = 0;
    for (size_t k = 0; k < byte_count; k += 2) {
        int i = (int)cu8[k] - 128, q = (int)cu8[k + 1] - 128;
        s->dc_i += (i * 65536 - s->dc_i) / (s->sample_rate == 250000 ? 4096 : 32768);
        s->dc_q += (q * 65536 - s->dc_q) / (s->sample_rate == 250000 ? 4096 : 32768);
        i -= s->dc_i / 65536;
        q -= s->dc_q / 65536;
        s->power += (float)(i * i + q * q);
        if (s->prev_valid) {
            s->cross += (float)(s->prev_i * q - s->prev_q * i);
            s->dot += (float)(s->prev_i * i + s->prev_q * q);
        }
        s->prev_i = i; s->prev_q = q; s->prev_valid = true;
        if (++s->decim_count != s->decimation) continue;
        float power = s->power / s->decimation;
        float freq = atan2f(s->cross, s->dot) * (s->sample_rate / 6.28318530718f);
        s->decim_count = 0; s->cross = s->dot = s->power = 0;
        if (power > 350.f) {
            s->in_burst = true;
            s->quiet = 0;
            if (s->count < TPMS_STEELMATE_BURST_SAMPLES) s->freq[s->count++] = freq;
            else { s->in_burst = false; s->count = 0; }
        } else if (s->in_burst && ++s->quiet >= 18) {
            if (emitted < capacity && parse_burst(s, &out[emitted])) emitted++;
            s->in_burst = false; s->count = s->quiet = 0;
        }
    }
    return emitted;
}
