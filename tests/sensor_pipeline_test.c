#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "sensor_pipeline.h"

static void run_fixture(sensor_pipeline_t *pipe, const char *path,
                        sensor_packet_type_t type, int expected)
{
    FILE *f = fopen(path, "rb"); assert(f);
    uint8_t iq[8192];
    int total = 0;
    size_t n;
    while ((n = fread(iq, 1, sizeof iq, f)) != 0) {
        sensor_packet_t packets[6];
        size_t found = sensor_pipeline_feed(pipe, iq, n, packets, 6);
        for (size_t j = 0; j < found; j++) {
            assert(packets[j].type == type);
            if (type == SENSOR_WH2) {
                assert(packets[j].weather.id == 209);
                assert(packets[j].weather.temperature_tenths_c == 246);
                assert(packets[j].weather.humidity == 33);
            } else {
                assert(packets[j].tpms.id == 0x1a4e);
                assert(packets[j].tpms.pressure_kpa == 259.375f);
                assert(packets[j].tpms.temperature_c == 25);
            }
            total++;
        }
    }
    fclose(f);
    assert(total == expected);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    sensor_pipeline_t pipe;
    assert(!sensor_pipeline_init(&pipe, 1000000));
    assert(sensor_pipeline_init(&pipe, 250000));
    run_fixture(&pipe, argv[1], SENSOR_WH2, 1);
    sensor_pipeline_reset(&pipe); /* retune / IQ loss: never join protocol frames */
    run_fixture(&pipe, argv[2], SENSOR_TPMS, 3);
    sensor_pipeline_reset(&pipe);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    uint8_t first[65536], second[65536];
    assert(fread(first, 1, sizeof first, f) == sizeof first);
    assert(fread(second, 1, sizeof second, f) == sizeof second);
    fclose(f);
    sensor_packet_t packets[6];
    assert(sensor_pipeline_feed(&pipe, first, sizeof first, packets, 6) == 0);
    sensor_pipeline_reset(&pipe); /* an overrun bisecting the WH2 frame */
    assert(sensor_pipeline_feed(&pipe, second, sizeof second, packets, 6) == 0);
    puts("Single IQ feed: WH2 1, Steelmate 3; gaps do not join packets");
    return 0;
}
