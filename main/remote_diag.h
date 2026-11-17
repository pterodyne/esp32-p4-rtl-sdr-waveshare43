#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef enum { REMOTE_NONE, REMOTE_STATUS, REMOTE_TUNE, REMOTE_GAIN, REMOTE_GAIN_AGC, REMOTE_ERROR } remote_kind_t;
typedef struct { remote_kind_t kind; uint32_t value; } remote_command_t;
typedef struct { char line[64]; size_t used; bool discard; } remote_parser_t;
bool remote_parse_byte(remote_parser_t *p, unsigned char ch, remote_command_t *out);
