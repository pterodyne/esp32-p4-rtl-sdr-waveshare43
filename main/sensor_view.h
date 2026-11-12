#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Foreground 433.92 MHz receiver; packet formats are named, not generic sensors. */
#define SENSOR_VIEW_ROWS 6
#define SENSOR_VIEW_LINE 56
typedef struct {
    bool streaming;
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
bool sensor_view_touch(int event, int x, int y); /* true only on EXIT release */
