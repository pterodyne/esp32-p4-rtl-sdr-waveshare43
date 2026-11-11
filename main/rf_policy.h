#pragma once
#include <stdbool.h>

/* V4/V4L have a built-in HF upconverter rather than a wired direct-ADC path.
 * Unknown device identity is treated as unsupported instead of risking a silent receiver. */
bool rf_direct_sampling_supported(int tuner, const char *manufacturer, const char *product);
