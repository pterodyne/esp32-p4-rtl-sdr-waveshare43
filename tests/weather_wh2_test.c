#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "weather_wh2.h"

/* rtl_433_tests/fineoffset/fineoffset_wh2/01/gfile001.json:
 * {"model":"Fineoffset-WH2","id":209,"temperature_C":24.600,
 *  "humidity":33,"mic":"CRC"}. Fixture is not redistributed here. */
static const uint8_t fixture_packet[6] = {0xff, 0x4d, 0x10, 0xf6, 0x21, 0x38};

static uint8_t crc_test(const uint8_t *bytes, size_t count)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < count; i++) {
        crc ^= bytes[i];
        for (unsigned j = 0; j < 8; j++)
            crc = (uint8_t)((crc << 1) ^ ((crc & 0x80) ? 0x31 : 0));
    }
    return crc;
}

static void check_expected(const weather_wh2_packet_t *packet)
{
    assert(packet->id == 209);
    assert(packet->temperature_tenths_c == 246);
    assert(packet->humidity == 33);
}

static void test_packet_and_crc(void)
{
    weather_wh2_packet_t out = {0};
    assert(weather_wh2_decode(fixture_packet, &out));
    check_expected(&out);
    uint8_t corrupt[6];
    memcpy(corrupt, fixture_packet, sizeof corrupt);
    corrupt[5] ^= 1;
    assert(!weather_wh2_decode(corrupt, &out));
    corrupt[5] ^= 1;
    corrupt[0] = 0xfe;
    assert(!weather_wh2_decode(corrupt, &out));
    corrupt[0] = 0xff;
    corrupt[1] = 0x40;
    assert(!weather_wh2_decode(corrupt, &out));
    memcpy(corrupt, fixture_packet, sizeof corrupt);
    corrupt[4] = 101;
    corrupt[5] = crc_test(corrupt + 1, 4);
    assert(!weather_wh2_decode(corrupt, &out)); /* plausible CRC, impossible RH */
    memcpy(corrupt, fixture_packet, sizeof corrupt);
    corrupt[2] = 0x13;
    corrupt[3] = 0x84; /* 90 C outside supported WH2 range */
    corrupt[5] = crc_test(corrupt + 1, 4);
    assert(!weather_wh2_decode(corrupt, &out));
}

/* Independent synthetic 2.4 MS/s OOK PWM waveform, MSB first. */
static void emit_samples(weather_wh2_stream_t *stream, unsigned count, int on,
                         weather_wh2_packet_t *out, size_t *total)
{
    uint8_t iq[512];
    for (size_t i = 0; i < sizeof iq; i += 2) {
        iq[i] = on ? 230 : 128;
        iq[i + 1] = 128;
    }
    while (count) {
        unsigned n = count > 256 ? 256 : count;
        size_t got = weather_wh2_stream_feed(stream, iq, 2 * n, out, 1);
        assert(got <= 1 && *total + got <= 1);
        *total += got;
        count -= n;
    }
}

static void test_synthetic_waveform(void)
{
    weather_wh2_stream_t stream;
    weather_wh2_packet_t out = {0};
    size_t total = 0;
    assert(weather_wh2_stream_init(&stream, 2400000));
    emit_samples(&stream, 24000, 0, &out, &total);
    for (unsigned i = 0; i < 48; ++i) {
        unsigned bit = (fixture_packet[i / 8] >> (7 - i % 8)) & 1;
        emit_samples(&stream, bit ? 1200 : 3600, 1, &out, &total);
        emit_samples(&stream, 2400, 0, &out, &total);
    }
    emit_samples(&stream, 6000, 0, &out, &total);
    assert(total == 1);
    check_expected(&out);
    weather_wh2_stream_reset(&stream);
    total = 0;
    uint8_t corrupt[6];
    memcpy(corrupt, fixture_packet, sizeof corrupt);
    corrupt[5] ^= 1;
    for (unsigned i = 0; i < 48; ++i) {
        unsigned bit = (corrupt[i / 8] >> (7 - i % 8)) & 1;
        emit_samples(&stream, bit ? 1200 : 3600, 1, &out, &total);
        emit_samples(&stream, 2400, 0, &out, &total);
    }
    emit_samples(&stream, 6000, 0, &out, &total);
    assert(total == 0);
}

static void test_real_cu8_fixture(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file && "pass original gfile001.cu8 path");
    weather_wh2_stream_t stream;
    assert(weather_wh2_stream_init(&stream, 250000));
    weather_wh2_packet_t out[2];
    uint8_t iq[8192];
    size_t total = 0, n, chunk = 0;
    while ((n = fread(iq, 1, (chunk++ % 3 == 0) ? 2 : sizeof iq, file)) != 0) {
        assert((n & 1) == 0);
        size_t got = weather_wh2_stream_feed(&stream, iq, n, out, 2);
        for (size_t i = 0; i < got; ++i) check_expected(&out[i]);
        total += got;
    }
    fclose(file);
    assert(total == 1);
}

static void test_real_at_lower_gain_and_with_dc_offset(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    uint8_t iq[8192];
    for (int variation = 0; variation < 2; variation++) {
        rewind(file);
        weather_wh2_stream_t stream;
        assert(weather_wh2_stream_init(&stream, 250000));
        size_t total = 0, n;
        while ((n = fread(iq, 1, sizeof iq, file)) != 0) {
            for (size_t j = 0; j < n; j++) {
                int value = variation == 0 ? 128 + ((int)iq[j] - 128) * 35 / 100 :
                            (int)iq[j] + ((j & 1) ? 0 : 30);
                iq[j] = value < 0 ? 0 : value > 255 ? 255 : (uint8_t)value;
            }
            weather_wh2_packet_t p[4];
            size_t got = weather_wh2_stream_feed(&stream, iq, n, p, 4);
            for (size_t k = 0; k < got; k++) check_expected(&p[k]);
            total += got;
        }
        assert(total == 1); /* AGC level and DC offsets cannot erase a valid packet */
    }
    fclose(file);
}

static void test_real_replayed_at_firmware_rate(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long bytes = ftell(file);
    assert(bytes > 0 && !(bytes & 1));
    rewind(file);
    uint8_t *source = malloc((size_t)bytes);
    assert(source);
    assert(fread(source, 1, (size_t)bytes, file) == (size_t)bytes);
    fclose(file);
    weather_wh2_stream_t stream;
    assert(weather_wh2_stream_init(&stream, 2400000));
    uint8_t iq[8192];
    size_t total = 0, samples = (size_t)bytes / 2 * 48 / 5;
    for (size_t start = 0; start < samples; start += sizeof iq / 2) {
        size_t count = samples - start;
        if (count > sizeof iq / 2) count = sizeof iq / 2;
        for (size_t i = 0; i < count; i++) {
            size_t src = (start + i) * 5 / 48; /* 250 kS/s -> 2.4 MS/s */
            iq[2 * i] = source[2 * src];
            iq[2 * i + 1] = source[2 * src + 1];
        }
        weather_wh2_packet_t packet[4];
        size_t got = weather_wh2_stream_feed(&stream, iq, count * 2, packet, 4);
        for (size_t i = 0; i < got; i++) check_expected(&packet[i]);
        total += got;
    }
    free(source);
    assert(total == 1); /* replayed recording, not a live V4 capture */
}

static void test_adjacent_carrier_rejection(void)
{
    weather_wh2_stream_t stream;
    assert(weather_wh2_stream_init(&stream, 2400000));
    size_t total = 0, samples = 0;
    uint8_t iq[512];
    for (int pulse = -1; pulse <= 48; pulse++) {
        unsigned bit = pulse < 0 || pulse == 48 ? 0 :
                       (fixture_packet[pulse / 8] >> (7 - pulse % 8)) & 1;
        unsigned on_count = pulse == -1 || pulse == 48 ? 0 : bit ? 1200 : 3600;
        unsigned off_count = pulse == -1 ? 24000 : pulse == 48 ? 6000 : 2400;
        for (int section = 0; section < 2; section++) {
            unsigned count = section ? off_count : on_count;
            while (count) {
                unsigned n = count > 256 ? 256 : count;
                static const int ci[3] = {60, -30, -30}, cq[3] = {0, 52, -52};
                for (unsigned k = 0; k < n; k++, samples++) {
                    double phase = (6.283185307179586 / 120) * (samples % 120); /* desired 20 kHz carrier */
                    int vi = 128 + ci[samples % 3] + (section ? 0 : (int)(65 * cos(phase)));
                    int vq = 128 + cq[samples % 3] + (section ? 0 : (int)(65 * sin(phase)));
                    iq[2 * k] = (uint8_t)vi;
                    iq[2 * k + 1] = (uint8_t)vq;
                }
                weather_wh2_packet_t p[2];
                size_t got = weather_wh2_stream_feed(&stream, iq, 2 * n, p, 2);
                for (size_t k = 0; k < got; k++) check_expected(&p[k]);
                total += got;
                count -= n;
            }
        }
    }
    assert(total == 1); /* constant 800 kHz carrier must not mask 433.92 MHz OOK */
}

int main(int argc, char **argv)
{
    test_packet_and_crc();
    test_synthetic_waveform();
    test_adjacent_carrier_rejection();
    if (argc > 1) {
        test_real_cu8_fixture(argv[1]);
        test_real_at_lower_gain_and_with_dc_offset(argv[1]);
        test_real_replayed_at_firmware_rate(argv[1]);
    }
    puts("WH2 packet, CRC, streaming tests passed");
    return 0;
}
