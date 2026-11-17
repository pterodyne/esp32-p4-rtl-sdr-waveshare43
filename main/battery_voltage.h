#pragma once
#include <stdbool.h>
/* Waveshare schematic p1: BAT -> R12 200k -> GPIO20 -> R15 100k -> GND.
 * Plausibility limits reject absent/shorted/saturated inputs, not cell presence. */
static inline int battery_from_adc_mv(int mv, bool calibrated) {
    return calibrated && mv >= 833 && mv <= 1450 ? mv * 3 : -1;
}
/* Rough loaded 1S 18650 voltage bands; NOT numerical SOC/charging state. */
static inline int battery_level(int mv) {
    return mv < 0 ? -1 : mv < 3600 ? 0 : mv <= 3900 ? 1 : 2;
}
void battery_voltage_start(void);
int battery_voltage_get(void);
