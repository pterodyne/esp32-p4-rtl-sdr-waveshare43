#include <assert.h>
#include <stdio.h>
#include "audio_save_pending.h"

int main(void)
{
    audio_save_pending_t p = {0};
    assert(!audio_save_pending_due(&p, 1));
    audio_save_pending_mark(&p);
    assert(audio_save_pending_due(&p, 1)); /* first change commits immediately */
    audio_save_pending_result(&p, false, 100);
    assert(!audio_save_pending_due(&p, 100));
    assert(!audio_save_pending_due(&p, 5000099));
    assert(audio_save_pending_due(&p, 5000100)); /* retry without another tap */
    audio_save_pending_mark(&p); /* a newer value supersedes the failed one */
    assert(audio_save_pending_due(&p, 101));
    audio_save_pending_result(&p, true, 102);
    assert(!audio_save_pending_due(&p, 5000100));
    puts("Failed audio commits retry; new values supersede pending failures");
    return 0;
}
