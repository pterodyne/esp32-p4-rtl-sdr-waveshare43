#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Tunable foreground receiver; RF frequency is not decoder compatibility. */
#define SENSOR_BANDS 5
enum { SENSOR_EXIT = 1, SENSOR_BAND = 2 };
uint32_t sensor_view_band_hz(int band);
#define SENSOR_VIEW_ROWS 6
#define SENSOR_VIEW_LINE 56
typedef struct {
    bool streaming;
    int band; /* 433.92, 315, 345, 868.3, 915 MHz; not persisted */
    unsigned wh2, tpms;
    uint64_t dropped_bytes;
    int load_percent;
    int rows;
    char event[SENSOR_VIEW_ROWS][SENSOR_VIEW_LINE]; /* newest first */
} sensor_view_info_t;

enum { SENSOR_PRESS, SENSOR_MOVE, SENSOR_RELEASE };
void sensor_view_init(void);
void sensor_view_open(void);
void sensor_view_draw(const sensor_view_info_t *info);
int sensor_view_touch(int event, int x, int y); /* SENSOR_EXIT/BAND on release */
