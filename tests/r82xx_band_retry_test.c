/* Exercise the real tuner path; only USB/I2C-facing callbacks are simulated. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../components/rtlsdr/blog/src/tuner_r82xx.c"

static int gpio_fail, gpio_calls, gpio_on;
static int pll_locked = 1;
static const char *model = "Blog V4";
int rtlsdr_check_dongle_model(void *dev, char *manufacturer, char *product)
{
    (void)dev;
    return !strcmp(manufacturer, "RTLSDRBlog") && !strcmp(product, model);
}
int rtlsdr_set_bias_tee_gpio(void *dev, int gpio, int on)
{
    (void)dev;
    gpio_on = on;
    assert(gpio == 5);
    gpio_calls++;
    return gpio_fail ? -1 : 0;
}
int rtlsdr_i2c_write_fn(void *dev, uint8_t addr, uint8_t *buf, int len)
{
    (void)dev; (void)addr; (void)buf;
    return len;
}
int rtlsdr_i2c_read_fn(void *dev, uint8_t addr, uint8_t *buf, int len)
{
    (void)dev; (void)addr;
    memset(buf, 0, len);
    if (len > 2 && pll_locked) buf[2] = 0x02; /* bit-reversed PLL lock */
    if (len > 4) buf[4] = 0x08; /* bit-reversed VCO fine tune 1 */
    return len;
}
static void retry_hf_switch(const char *product)
{
    struct r82xx_config cfg = {.rafael_chip = CHIP_R828D, .xtal = 28800000,
                               .max_i2c_msg_len = 8};
    struct r82xx_priv priv = {.cfg = &cfg, .int_freq = 3570000, .input = UHF};
    model = product;
    gpio_calls = 0;
    gpio_fail = 1;
    assert(r82xx_set_freq(&priv, 10000000) < 0);
    assert(gpio_calls == 1);
    assert(priv.input != HF); /* failed transition must not be cached */
    gpio_fail = 0;
    assert(r82xx_set_freq(&priv, 10000000) == 0);
    assert(gpio_calls == 2); /* must retry physical upconverter switch */
    assert(priv.input == HF);
    assert(r82xx_set_freq(&priv, 15000000) == 0);
    assert(gpio_calls == 2); /* successful same-band tunes need no switch */
    gpio_fail = 1;
    assert(r82xx_set_freq(&priv, 915000000) < 0);
    assert(priv.input != HF && priv.input != UHF);
    gpio_fail = 0;
    int calls = gpio_calls;
    assert(r82xx_set_freq(&priv, 10000000) == 0);
    assert(gpio_calls == calls + 1); /* even returning to old band must reapply */
    assert(priv.input == HF);
}
/* Decode the actual programmed PLL, rather than substituting a fake PLL. */
static double programmed_lo(struct r82xx_priv *priv)
{
    unsigned r14 = r82xx_read_cache_reg(priv, 0x14);
    unsigned nint = 4 * (r14 & 0x3f) + (r14 >> 6) + 13;
    unsigned sdm = r82xx_read_cache_reg(priv, 0x15) |
                   (r82xx_read_cache_reg(priv, 0x16) << 8);
    unsigned div = 2u << (r82xx_read_cache_reg(priv, 0x10) >> 5);
    return 2.0 * priv->cfg->xtal * (nint + sdm / 65536.0) / div;
}
static int boundary(const char *product)
{
    const uint32_t freqs[] = {28799999, 28800000, 28800001, 28820000, 28799999};
    struct r82xx_config cfg = {.rafael_chip = CHIP_R828D, .xtal = 28800000,
                               .max_i2c_msg_len = 8};
    struct r82xx_priv priv = {.cfg = &cfg, .int_freq = 3570000, .input = 0xff};
    int failures = 0;
    model = product;
    gpio_fail = 0;
    for (unsigned i = 0; i < sizeof(freqs)/sizeof(freqs[0]); ++i) {
        int hf = freqs[i] < 28800000;
        int v4 = !strcmp(product, "Blog V4");
        unsigned expected_band = hf ? HF : (v4 ? VHF : UHF);
        double expected_lo = freqs[i] + priv.int_freq + (hf ? 28800000 : 0);
        assert(r82xx_set_freq(&priv, freqs[i]) == 0);
        assert(priv.has_lock); /* rc alone is insufficient in this driver */
        double delta = programmed_lo(&priv) - expected_lo;
        assert(delta > -2000 && delta < 2000); /* PLL quantization */
        unsigned r05 = r82xx_read_cache_reg(&priv, 0x05);
        unsigned r06 = r82xx_read_cache_reg(&priv, 0x06);
        int ok = priv.input == expected_band && gpio_on == !hf &&
                 (r05 & 0x60) == (v4 ? (hf ? 0x20 : 0x60) : (hf ? 0x60 : 0)) &&
                 (!v4 || (r06 & 0x08) == (hf ? 0x08 : 0));
        printf("%s %u Hz: LO %.0f input %u GPIO5 %d: %s\n",
               product, freqs[i], programmed_lo(&priv), priv.input, gpio_on,
               ok ? "PASS" : "FAIL");
        failures += !ok;
    }
    return failures;
}
int main(void)
{
    retry_hf_switch("Blog V4");
    retry_hf_switch("Blog V4L");
    puts("R82xx: failed HF input/GPIO transitions retry; successful transitions cached");
    int failures = boundary("Blog V4") + boundary("Blog V4L");
    /* Characterize the existing unlocked-PLL success hole, without changing it. */
    struct r82xx_config cfg = {.rafael_chip = CHIP_R828D, .xtal = 28800000,
                               .max_i2c_msg_len = 8};
    struct r82xx_priv priv = {.cfg = &cfg, .int_freq = 3570000, .input = 0xff};
    pll_locked = 0;
    gpio_calls = 0;
    int rc = r82xx_set_freq(&priv, 28800000);
    assert(rc == 0 && !priv.has_lock && gpio_calls == 0);
    puts("Known limitation: unlocked PLL returns 0; boundary tests explicitly require lock");
    assert(priv.diag_pll == 0 && !priv.diag_gpio5_attempted);
    pll_locked = 1;
    gpio_fail = 1;
    assert(r82xx_set_freq(&priv, 10000000) < 0);
    assert(priv.diag_tuner_hz == 38800000 && priv.diag_lo_hz == 42370000);
    assert(priv.diag_pll == 1 && priv.diag_gpio5_attempted);
    assert(priv.diag_gpio5_level == 0 && priv.diag_gpio5_rc < 0);
    gpio_fail = 0;
    assert(r82xx_set_freq(&priv, 10000000) == 0);
    assert(priv.diag_gpio5_attempted && priv.diag_gpio5_rc == 0);
    assert(r82xx_set_freq(&priv, 11000000) == 0);
    assert(!priv.diag_gpio5_attempted); /* cached same-band tune is NOT a new switch write */
    return failures ? 1 : 0;
}
