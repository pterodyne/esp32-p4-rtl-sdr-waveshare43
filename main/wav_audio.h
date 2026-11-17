#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
/* Demodulated listening audio only: PCM signed LE16 mono, 48000 Hz. */
#define WAV_AUDIO_RATE 48000
/* Target stdio uses signed 32-bit long for fseek/ftell. Include the 44-byte
 * header and round payload down to a complete 16-bit sample, even on LP64 hosts. */
#define WAV_AUDIO_MAX_BYTES (((uint32_t)INT32_MAX - 44u) & ~1u)
typedef struct { FILE *file; uint32_t bytes; bool failed; } wav_writer_t;
typedef struct { FILE *file; uint32_t remaining; bool failed; } wav_reader_t;
bool wav_begin(wav_writer_t *w, FILE *f);
bool wav_append(wav_writer_t *w, const int16_t *pcm, size_t count);
bool wav_finish(wav_writer_t *w);
bool wav_open(wav_reader_t *r, FILE *f);
size_t wav_read(wav_reader_t *r, int16_t *pcm, size_t count);
bool wav_safe_name(const char *name);
/* UTC system-clock snapshot; outside 2024..2099 is explicitly unsynced. */
bool wav_record_name(char *out, size_t size, uint32_t hz, int bw, int64_t utc, unsigned collision);
bool wav_recording_name(const char *name);
