/* RX-only HackRF One protocol; see docs/HACKRF-RX.md for source attribution. */
#pragma once
#include "libusb.h"
#include <stdbool.h>
int hackrf_rx_stage(libusb_device_handle *h, bool lna, unsigned db);
void hackrf_rx_unsigned(unsigned char *data, unsigned n);
int hackrf_rx_rate(libusb_device_handle *h, uint32_t hz);
int hackrf_rx_tune(libusb_device_handle *h, uint32_t hz);
int hackrf_rx_safe(libusb_device_handle *h);
int hackrf_rx_clear_bias_options(libusb_device_handle *h, uint16_t api);
int hackrf_rx_gain(libusb_device_handle *h, unsigned lna, unsigned vga);
int hackrf_rx_mode(libusb_device_handle *h, int receive);
int hackrf_rx_bias(libusb_device_handle *h, int on);
