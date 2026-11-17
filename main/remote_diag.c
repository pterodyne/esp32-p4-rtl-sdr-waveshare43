#include "remote_diag.h"
#include <string.h>
bool remote_parse_byte(remote_parser_t *p, unsigned char ch, remote_command_t *out)
{
    if (ch == '\r' || ch == '\n') {
        if (!p->used && !p->discard) return false;
        p->line[p->used] = 0;
        out->kind = REMOTE_ERROR;
        out->value = 0;
        if (!p->discard && !strcmp(p->line, "status")) out->kind = REMOTE_STATUS;
        if (!p->discard && !strncmp(p->line, "tune ", 5)) {
            uint64_t n = 0;
            size_t i = 5;
            for (; p->line[i] >= '0' && p->line[i] <= '9'; i++) {
                n = n * 10 + (unsigned)(p->line[i] - '0');
                if (n > UINT32_MAX) break;
            }
            if (i > 5 && !p->line[i] && n >= 500000 && n <= 1766000000) {
                out->kind = REMOTE_TUNE;
                out->value = (uint32_t)n;
            }
        }
        if (!p->discard && !strcmp(p->line, "gain agc")) out->kind = REMOTE_GAIN_AGC;
        if (!p->discard && !strncmp(p->line, "gain ", 5)) {
            unsigned n = 0; size_t i = 5;
            for (; p->line[i] >= '0' && p->line[i] <= '9'; i++) {
                n = n * 10 + (unsigned)(p->line[i] - '0');
                if (n > 500) break;
            }
            if (i > 5 && !p->line[i] && n <= 500) {
                out->kind = REMOTE_GAIN; out->value = n;
            }
        }
        p->used = 0;
        p->discard = false;
        return true;
    }
    if (ch < 32 || ch > 126 || p->used == sizeof p->line - 1) p->discard = true;
    if (!p->discard) p->line[p->used++] = (char)ch;
    return false;
}
