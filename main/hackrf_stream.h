#pragma once
#include "hackrf_rx.h"
typedef void (*hackrf_sink_t)(unsigned char *,uint32_t,void *);
/* Run on the single RX owner task. Returns only after every submitted transfer drains. */
int hackrf_stream_run(libusb_device_handle *h,hackrf_sink_t sink,void *ctx);
void hackrf_stream_stop(void);
/* Clear stop before starting the owner task, never inside it (avoids start/stop race). */
void hackrf_stream_prepare(void);
