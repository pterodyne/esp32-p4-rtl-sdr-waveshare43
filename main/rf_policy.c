#include <string.h>
#include "rtl-sdr.h"
#include "rf_policy.h"

bool rf_direct_sampling_supported(int tuner, const char *manufacturer, const char *product)
{
    if (!manufacturer || !product || !manufacturer[0] || !product[0]) return false;
    if (tuner == RTLSDR_TUNER_R828D) return false;
    if (!strncmp(product, "Blog V4", 7)) return false;
    return true;
}
