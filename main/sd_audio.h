#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SD_AUDIO_FILES 64
#define SD_AUDIO_NAME 64
enum { SD_AUDIO_REFRESH=1, SD_AUDIO_RECORD, SD_AUDIO_PLAY, SD_AUDIO_STOP, SD_AUDIO_NEXT, SD_AUDIO_PREV, SD_AUDIO_DELETE };
enum { SD_AUDIO_IDLE, SD_AUDIO_RECORDING, SD_AUDIO_PLAYING };
typedef struct {
    int state, count, batch;
    bool more;
    bool record_requested; /* pending preparation OR actively recording; STOP cancels both */
    unsigned generation;
    char status[80];
    char name[SD_AUDIO_NAME];
    char files[SD_AUDIO_FILES][SD_AUDIO_NAME];
} sd_audio_snapshot_t;
bool sd_audio_init(void);
bool sd_audio_request(int command, const char *name);
void sd_audio_snapshot(sd_audio_snapshot_t *out);
/* Audio owner only; bounded copy, never filesystem/codec/queue wait. Captures demod PCM,
 * OR substitutes playback PCM (zero on underrun); never records its own playback. */
bool sd_audio_playing(void);
/* Page routing never changes the user's codec mute or volume. Transitions finalize
 * recording/cancel playback; recording is deliberately unavailable on this page. */
void sd_audio_playback_page(bool on);
bool sd_audio_output_exclusive(void);
void sd_audio_exchange(int16_t *pcm, size_t samples);
