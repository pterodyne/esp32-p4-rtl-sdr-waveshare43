#include <assert.h>
#include <stdio.h>
#include "audio_output_policy.h"

int main(void)
{
    const int saved_volume = 5;
    assert(!audio_output_should_mute(saved_volume, false));
    assert(audio_output_should_mute(saved_volume, true));
    assert(!audio_output_should_mute(saved_volume, false)); /* unmute restores same level */
    assert(audio_output_should_mute(0, false)); /* VOL 0 stays silent */
    assert(audio_output_should_mute(0, true));
    /* The audio task may already be applying a prior volume request when the
     * UI queues MUTE. Taking the old request must not erase the new one. */
    audio_output_requests_t req = AUDIO_OUTPUT_REQUESTS_INIT;
    audio_output_set_volume(&req, saved_volume);
    assert(audio_output_take_volume(&req) == saved_volume);
    audio_output_set_mute(&req, true);
    assert(audio_output_take_volume(&req) == saved_volume);
    assert(audio_output_is_muted(&req));
    audio_output_set_mute(&req, false);
    assert(audio_output_take_volume(&req) == saved_volume);
    assert(!audio_output_is_muted(&req));
    puts("Audio output: mute is independent of the saved volume");
    return 0;
}
