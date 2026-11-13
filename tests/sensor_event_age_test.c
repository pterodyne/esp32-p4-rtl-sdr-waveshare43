#include <assert.h>
#include <stdio.h>
#include "sensor_event_age.h"

int main(void)
{
    assert(!sensor_event_expired(123456, 123457));
    assert(!sensor_event_expired(123456, 60123455));
    assert(sensor_event_expired(123456, 60123456));
    assert(sensor_event_expired(123456, 123455)); /* discontinuity: fail closed */
    puts("Sensor readings expire 60 seconds after reception");
    return 0;
}
