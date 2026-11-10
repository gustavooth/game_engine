#include "math.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

static b8 rand_seeded = false;

static void egn_seed_random(void)
{
  if (!rand_seeded)
  {
    srand((u32)time(NULL));
    rand_seeded = true;
  }
}

/**
 * Note that these are here in order to prevent having to import the
 * entire <math.h> everywhere.
 */
f32 egnsin(f32 x)
{
  return sinf(x);
}
f32 egncos(f32 x)
{
  return cosf(x);
}
f32 egntan(f32 x)
{
  return tanf(x);
}
f32 egnacos(f32 x)
{
  return acosf(x);
}
f32 egnsqrt(f32 x)
{
  return sqrtf(x);
}
f32 egnabs(f32 x)
{
  return fabsf(x);
}
i32 egnrandom()
{
  egn_seed_random();
  return rand();
}
i32 egnrandom_in_range(i32 min, i32 max)
{
  if (min > max)
  {
    i32 tmp = min;
    min = max;
    max = tmp;
  }

  egn_seed_random();

  i64 span = (i64)max - (i64)min + 1;
  i64 value = (i64)min + (i64)((u64)rand() % (u64)span);
  return (i32)value;
}
f32 egnfrandom()
{
  egn_seed_random();
  return (f32)rand() / (f32)RAND_MAX;
}
f32 egnfrandom_in_range(f32 min, f32 max)
{
  if (min > max)
  {
    f32 tmp = min;
    min = max;
    max = tmp;
  }

  return min + (max - min) * egnfrandom();
}