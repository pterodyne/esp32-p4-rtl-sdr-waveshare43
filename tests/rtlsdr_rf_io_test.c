/* Host regression for short EEPROM transfers and failed Bias-T GPIO writes.
 * Include the vendor implementation to construct its private device struct. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../components/rtlsdr/blog/src/librtlsdr.c"

static int short_eeprom, fail_gpio_write, short_gpio_read, eeprom_reads, last_gpo;

int __wrap_libusb_control_transfer(libusb_device_handle *handle, uint8_t request_type,
                                   uint8_t request, uint16_t value, uint16_t index,
                                   unsigned char *data, uint16_t length, unsigned int timeout)
{
    (void)handle; (void)request; (void)index; (void)timeout;
    if ((request_type & LIBUSB_ENDPOINT_IN) && value == EEPROM_ADDR) {
        eeprom_reads++;
        if (short_eeprom && eeprom_reads == 8) return 0;
    }
    if ((request_type & LIBUSB_ENDPOINT_IN) && short_gpio_read && value == GPD) return 0;
    if (!(request_type & LIBUSB_ENDPOINT_IN) && fail_gpio_write && value == GPO) return 0;
    if (!(request_type & LIBUSB_ENDPOINT_IN) && value == GPO) last_gpo = data[0];
    if (request_type & LIBUSB_ENDPOINT_IN) memset(data, 0, length);
    return length;
}

int main(void)
{
    struct rtlsdr_dev dev = {0};
    dev.devh = (libusb_device_handle *)1;
    uint8_t eeprom[8] = {0};
    short_eeprom = 1;
    assert(rtlsdr_read_eeprom(&dev, eeprom, 0, sizeof eeprom) < 0);
    short_eeprom = 0;
    eeprom_reads = 0;
    assert(rtlsdr_read_eeprom(&dev, eeprom, 0, sizeof eeprom) >= 0);
    fail_gpio_write = 1;
    assert(rtlsdr_set_bias_tee(&dev, 1) < 0);
    fail_gpio_write = 0;
    assert(rtlsdr_set_bias_tee(&dev, 1) == 0);
    short_gpio_read = 1;
    assert(rtlsdr_set_bias_tee(&dev, 0) < 0);
    short_gpio_read = 0;
    dev.force_bt = 1;
    assert(rtlsdr_get_bias_tee_forced(&dev) == 1); /* UI must read the driver's latch */
    assert(rtlsdr_set_bias_tee(&dev, 0) == 0);
    assert(last_gpo & 1); /* driver's EEPROM force cannot be bypassed by UI */
    dev.force_bt = 0;
    assert(rtlsdr_get_bias_tee_forced(&dev) == 0);
    assert(rtlsdr_get_bias_tee_forced(NULL) < 0);
    puts("RTL-SDR RF I/O: short EEPROM and failed GPIO writes rejected");
    return 0;
}
