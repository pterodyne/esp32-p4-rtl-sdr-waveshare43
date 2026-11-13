#include <assert.h>
#include <stdio.h>
#include "settings_save_due.h"

int main(void)
{
    /* Entering an indefinitely long foreground mode cannot postpone a
     * pending receiver-settings save, even inside the normal debounce. */
    assert(settings_save_due(4000000, 1000000, true));
    assert(!settings_save_due(4000000, 1000000, false));
    assert(settings_save_due(4000000, 4000000, false));
    assert(!settings_save_due(0, 1000000, true));
    puts("Foreground entry flushes pending receiver settings before retuning");
    return 0;
}
