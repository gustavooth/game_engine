#pragma once
#include "defines.h"

typedef struct Clock
{
  f64 start_time;
  f64 elapsed;
} Clock;

/**
 * Starts the provided Clock. Resets elapsed time.
 * @param clock Clock to update.
 */
void clock_start(Clock *Clock);

/**
 * Stops the provided Clock. Does not reset elapsed time.
 * @param clock Clock to stop.
 */
void clock_stop(Clock *Clock);

/**
 * Updates the provided Clock. Should be called just before checking elapsed time.
 * Has no effect on non-started Clocks.
 * @param clock Clock to update.
 */
void clock_update(Clock *Clock);
