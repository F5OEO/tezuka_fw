/* No-ALSA replacement for upstream beep.c: the lock-tone beeper is disabled. */
#include <stddef.h>
#include "beep.h"

void *loop_beep(void *arg)
{
    (void)arg;
    return NULL;
}
