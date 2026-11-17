#pragma once
/* Diagnostic command/readback records, not proof of RF reception or switch position. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t tuner_hz, lo_hz; /* actual driver computation, not measured */
    int pll;                 /* -1 not checked, 0 unlocked, 1 locked */
    bool gpio5_attempted;
    int gpio5_rc, gpio5_level; /* requested level; never a measured voltage */
} hf_diag_tuner_t;

typedef struct {
    bool valid;
    uint32_t requested_hz;
    int center_rc;           /* actual rtlsdr_set_center_freq return */
    hf_diag_tuner_t tuner;
} hf_diag_tune_t;

typedef struct {
    bool identity_read, exact_v4;
    char manufacturer[32], product[32];
    hf_diag_tune_t hf, vhf, latest;
} hf_diag_state_t;

void hf_diag_identity(hf_diag_state_t *d, const char *manufacturer, const char *product, bool read_ok);
void hf_diag_tune(hf_diag_state_t *d, uint32_t requested_hz, int center_rc, hf_diag_tuner_t tuner);
/* Nine short, portrait-safe lines shown only in RF INPUT > HF DIAG. */
const char *hf_diag_line(const hf_diag_state_t *d, int index, char *out, size_t n);
