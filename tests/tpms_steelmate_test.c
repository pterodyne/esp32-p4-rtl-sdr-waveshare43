#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tpms_steelmate.h"

static void test_fixture_payload(void)
{
    /* Decoded values from rtl_433_tests/steelmate-TPMS/01/gfile001.json. */
    const uint8_t bytes[9] = {0, 0, 1, 0x1a, 0x4e, 83, 75, 133, 0x8c};
    tpms_steelmate_packet_t packet = {0};
    assert(tpms_steelmate_decode(bytes, &packet));
    assert(packet.id == 0x1a4e);
    assert(fabsf(packet.pressure_kpa - 259.375f) < 0.001f);
    assert(packet.temperature_c == 25);
    assert(packet.battery_mv == 2570);
    assert(packet.alarm == TPMS_STEELMATE_NO_ALARM);
}

static void test_checksum_preamble_and_alarms(void)
{
    uint8_t bytes[9] = {0, 0, 1, 0x1a, 0x4e, 83, 75, 133, 0x8c};
    tpms_steelmate_packet_t packet = {0};
    bytes[8] ^= 1;
    assert(!tpms_steelmate_decode(bytes, &packet));
    bytes[8] ^= 1;
    bytes[0] = 1;
    assert(!tpms_steelmate_decode(bytes, &packet));
    bytes[0] = 0;
    bytes[2] = 0;
    assert(!tpms_steelmate_decode(bytes, &packet));
    bytes[2] = 1;
    bytes[7] = 0xfe;
    bytes[8] = (uint8_t)(0x8c + 0xfe - 133);
    assert(tpms_steelmate_decode(bytes, &packet));
    assert(packet.alarm == TPMS_STEELMATE_SLOW_LEAK && packet.battery_mv == -1);
    bytes[7] = 0xff;
    bytes[8]++;
    assert(tpms_steelmate_decode(bytes, &packet));
    assert(packet.alarm == TPMS_STEELMATE_FAST_LEAK && packet.battery_mv == -1);
}

static void test_real_cu8_fixture(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file && "pass the downloaded rtl_433_tests CU8 fixture path");
    tpms_steelmate_stream_t stream;
    assert(tpms_steelmate_stream_init(&stream, 250000));
    uint8_t buf[8192];
    tpms_steelmate_packet_t out[4];
    size_t total = 0, n, chunk = 0;
    while ((n = fread(buf, 1, (chunk++ % 3 == 0) ? 2 : sizeof(buf), file)) != 0) {
        assert(!(n & 1));
        size_t got = tpms_steelmate_stream_feed(&stream, buf, n, out, 4);
        for (size_t k = 0; k < got; k++) {
            assert(out[k].id == 0x1a4e);
            assert(fabsf(out[k].pressure_kpa - 259.375f) < 0.001f);
            assert(out[k].temperature_c == 25);
            assert(out[k].battery_mv == 2570);
        }
        total += got;
    }
    fclose(file);
    assert(total == 3);
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
    tpms_steelmate_stream_t stream;
    assert(tpms_steelmate_stream_init(&stream, 2400000));
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
        tpms_steelmate_packet_t out[4];
        size_t found = tpms_steelmate_stream_feed(&stream, iq, 2 * count, out, 4);
        for (size_t i = 0; i < found; i++) {
            assert(out[i].id == 0x1a4e && out[i].temperature_c == 25);
            assert(fabsf(out[i].pressure_kpa - 259.375f) < 0.001f);
        }
        total += found;
    }
    free(source);
    assert(total >= 2 && total <= 3); /* 2/3 today: strict preamble rejects one */
}

#define PI_D 3.14159265358979323846

/* Synthetic FSK IQ for the 2.4 MS/s SDR path, not a fixture recording. */
static void test_synthetic_2400k_stream_and_reset(void)
{
    uint8_t bytes[9] = {0, 0, 1, 0x1a, 0x4e, 83, 75, 133, 0x8c};
    uint8_t iq[2 * (72 * 2 * 120 + 1200)];
    tpms_steelmate_stream_t stream;
    tpms_steelmate_packet_t out[2];
    assert(!tpms_steelmate_stream_init(&stream, 240000));
    assert(tpms_steelmate_stream_init(&stream, 2400000));
    for (int corrupt = 0; corrupt < 4; corrupt++) {
        double phase = 0;
        size_t samples = 0;
        if (corrupt == 1) bytes[8] ^= 1;
        if (corrupt == 2) bytes[8] ^= 1;
        for (unsigned bit = 0; bit < 72; bit++) {
            uint8_t raw = bit < 16 ? (corrupt == 2 ? 0x00 : corrupt == 3 && bit < 8 ? 0xfe : 0xff) :
                                     (uint8_t)~bytes[bit / 8];
            unsigned lsb_first = (raw >> (bit % 8)) & 1;
            for (unsigned chip = 0; chip < 2; chip++) {
                double freq = (lsb_first ^ chip) ? 11000. : -34000.;
                for (unsigned j = 0; j < 120; j++) {
                    iq[2 * samples] = (uint8_t)(128 + 70 * cos(phase));
                    iq[2 * samples + 1] = (uint8_t)(128 + 70 * sin(phase));
                    phase += 2 * PI_D * freq / 2400000.;
                    samples++;
                }
            }
        }
        for (unsigned j = 0; j < 1200; j++) {
            iq[2 * samples] = iq[2 * samples + 1] = 128;
            samples++;
        }
        size_t got = 0;
        for (size_t offset = 0; offset < 2 * samples;) {
            size_t n = offset % 3 == 0 ? 2 : 4096;
            if (n > 2 * samples - offset) n = 2 * samples - offset;
            got += tpms_steelmate_stream_feed(&stream, iq + offset, n, out, 2 - got);
            offset += n;
        }

        assert(got == (corrupt ? 0u : 1u)); /* even one corrupt preamble bit rejects */
        if (!corrupt) {
            assert(out[0].id == 0x1a4e && out[0].temperature_c == 25);
            tpms_steelmate_stream_reset(&stream);
            size_t split = samples & ~(size_t)1;
            assert(tpms_steelmate_stream_feed(&stream, iq, split, out, 2) == 0);
            tpms_steelmate_stream_reset(&stream); /* dropped ring span / retune */
            assert(tpms_steelmate_stream_feed(&stream, iq + split,
                                              2 * samples - split, out, 2) == 0);
        }
        tpms_steelmate_stream_reset(&stream);
    }
}

static void test_nonzero_idle_iq_floor(int frequency_offset_hz)
{
    const uint8_t bytes[9] = {0, 0, 1, 0x1a, 0x4e, 83, 75, 133, 0x8c};
    uint8_t iq[2 * (72 * 2 * 120 + 1200)];
    tpms_steelmate_stream_t stream;
    assert(tpms_steelmate_stream_init(&stream, 2400000));
    double phase = 0;
    size_t samples = 0;
    for (unsigned bit = 0; bit < 72; bit++) {
        uint8_t raw = bit < 16 ? 0xff : (uint8_t)~bytes[bit / 8];
        unsigned data = (raw >> (bit % 8)) & 1;
        for (unsigned chip = 0; chip < 2; chip++) {
            double freq = ((data ^ chip) ? 11000. : -34000.) + frequency_offset_hz;
            for (unsigned j = 0; j < 120; j++, samples++) {
                iq[2 * samples] = (uint8_t)(148 + 70 * cos(phase));
                iq[2 * samples + 1] = (uint8_t)(128 + 70 * sin(phase));
                phase += 2 * PI_D * freq / 2400000.;
            }
        }
    }
    for (unsigned j = 0; j < 1200; j++, samples++) {
        iq[2 * samples] = 148; /* idle 20 I counts: power 400 > old fixed cutoff */
        iq[2 * samples + 1] = 128;
    }
    tpms_steelmate_packet_t out[2];
    size_t found = tpms_steelmate_stream_feed(&stream, iq, 2 * samples, out, 2);
    assert(found == 1 && out[0].id == 0x1a4e);
}

int main(int argc, char **argv)
{
    test_fixture_payload();
    test_checksum_preamble_and_alarms();
    test_synthetic_2400k_stream_and_reset();
    test_nonzero_idle_iq_floor(0);
    test_nonzero_idle_iq_floor(-9000);
    if (argc > 1) {
        test_real_cu8_fixture(argv[1]);
        test_real_replayed_at_firmware_rate(argv[1]);
    }
    puts("Steelmate packet tests passed");
    return 0;
}
