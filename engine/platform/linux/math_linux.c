#include "platform/math.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

// Native Linux implementation. This is deliberately a separate backend from
// the wasm one: it uses libm's float entry points and the C library RNG.
static b8 linux_rng_seeded;

static void linux_seed_random(void)
{
  if (!linux_rng_seeded)
  {
    srand((u32)time(NULL));
    linux_rng_seeded = true;
  }
}

static f32 linux_sin(f32 x) { return sinf(x); }
static f32 linux_cos(f32 x) { return cosf(x); }
static f32 linux_tan(f32 x) { return tanf(x); }
static f32 linux_acos(f32 x) { return acosf(x); }
static f32 linux_sqrt(f32 x) { return sqrtf(x); }
static f32 linux_abs(f32 x) { return fabsf(x); }

static i32 linux_random(void)
{
  linux_seed_random();
  return rand();
}

static i32 linux_random_range(i32 min, i32 max)
{
  if (min > max)
  {
    i32 tmp = min;
    min = max;
    max = tmp;
  }
  linux_seed_random();
  i64 span = (i64)max - (i64)min + 1;
  return (i32)((i64)min + (i64)((u64)rand() % (u64)span));
}

static f32 linux_frandom(void)
{
  linux_seed_random();
  return (f32)rand() / (f32)RAND_MAX;
}

static f32 linux_frandom_range(f32 min, f32 max)
{
  if (min > max)
  {
    f32 tmp = min;
    min = max;
    max = tmp;
  }
  return min + (max - min) * linux_frandom();
}

static const EgnMathInterface linux_math_interface = {
    .name = EGN_MATH_PLATFORM_NAME,
    .sin = linux_sin,
    .cos = linux_cos,
    .tan = linux_tan,
    .acos = linux_acos,
    .sqrt = linux_sqrt,
    .abs = linux_abs,
    .random = linux_random,
    .random_range = linux_random_range,
    .frandom = linux_frandom,
    .frandom_range = linux_frandom_range,
};

const EgnMathInterface *egn_math_interface(void)
{
  return &linux_math_interface;
}

f32 egnsin(f32 x) { return linux_math_interface.sin(x); }
f32 egncos(f32 x) { return linux_math_interface.cos(x); }
f32 egntan(f32 x) { return linux_math_interface.tan(x); }
f32 egnacos(f32 x) { return linux_math_interface.acos(x); }
f32 egnsqrt(f32 x) { return linux_math_interface.sqrt(x); }
f32 egnabs(f32 x) { return linux_math_interface.abs(x); }
i32 egnrandom(void) { return linux_math_interface.random(); }
i32 egnrandom_in_range(i32 min, i32 max) { return linux_math_interface.random_range(min, max); }
f32 egnfrandom(void) { return linux_math_interface.frandom(); }
f32 egnfrandom_in_range(f32 min, f32 max) { return linux_math_interface.frandom_range(min, max); }
