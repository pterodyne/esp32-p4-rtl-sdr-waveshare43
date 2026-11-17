/* Host tests for the bounded UART protocol. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "remote_diag.h"
static remote_command_t feed(remote_parser_t *p, const char *s) {
    remote_command_t c = {0}, last = {0};
    while (*s) if (remote_parse_byte(p, (unsigned char)*s++, &c)) last = c;
    return last;
}
int main(void) {
    remote_parser_t p = {0};
    assert(feed(&p, "status\r\n").kind == REMOTE_STATUS);
    assert(feed(&p, "tune 7100000\n").value == 7100000);
    assert(feed(&p, "tune 7100000\n").kind == REMOTE_TUNE);
    const char *bad[] = {"tune -1\n", "tune 42949672960\n", "tune 99999\n", "tune 1766000001\n", "tune 7100000 junk\n", "bias on\n", "tune \n"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) assert(feed(&p, bad[i]).kind == REMOTE_ERROR);
    char huge[200]; memset(huge, 'x', sizeof huge); huge[198] = '\n'; huge[199] = 0;
    assert(feed(&p, huge).kind == REMOTE_ERROR);
    assert(feed(&p, "status\n").kind == REMOTE_STATUS);
    assert(feed(&p, "tune 710\0010000\n").kind == REMOTE_ERROR);
    assert(feed(&p, "gain agc\n").kind == REMOTE_GAIN_AGC);
    assert(feed(&p, "gain 120\n").kind == REMOTE_GAIN);
    assert(feed(&p, "gain 501\n").kind == REMOTE_ERROR);
    puts("remote parser bounds/recovery/gain OK");
}
