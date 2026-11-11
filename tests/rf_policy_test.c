#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "rtl-sdr.h"
#include "rf_policy.h"

int main(void)
{
    /* Blog V4 and V4L route HF through a mixer, not ADC direct sampling. */
    assert(!rf_direct_sampling_supported(RTLSDR_TUNER_R828D, "RTLSDRBlog", "Blog V4"));
    assert(!rf_direct_sampling_supported(RTLSDR_TUNER_R820T, "RTLSDRBlog", "Blog V4L"));
    assert(!rf_direct_sampling_supported(RTLSDR_TUNER_R828D, "generic", "other"));
    assert(rf_direct_sampling_supported(RTLSDR_TUNER_R820T, "RTLSDRBlog", "Blog V3"));
    assert(rf_direct_sampling_supported(RTLSDR_TUNER_R820T, "generic", "unknown"));
    assert(!rf_direct_sampling_supported(RTLSDR_TUNER_R820T, NULL, NULL)); /* unknown hardware: fail closed */

    puts("RF policy: V4/V4L direct-sampling blocked");
}
