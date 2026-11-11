#include <assert.h>
#include <stdio.h>
#include "ui_audio_policy.h"

int main(void)
{
    assert(ui_should_log_rds(0, false, false, true));
    assert(ui_should_log_rds(0, false, true, true)); /* muting speaker must not suppress RDS */
    assert(!ui_should_log_rds(4, false, true, true));
    assert(!ui_should_log_rds(0, true, true, true));
    assert(!ui_should_log_rds(0, false, false, false));
    puts("UI audio: mute does not suppress RDS log updates");
    return 0;
}
