#include "platform/math.h"

#include <math.h>
#include <time.h>

// WebAssembly implementation. Uses double-precision libm from the wasm
// runtime and a deterministic xorshift RNG so games behave consistently
// across browser runs.
static u32 web_rng_state;
static b8 web_rng_seeded;

static u32 web_next_random(void)
{
  if (!web_rng_seeded)
  {
    web_rng_state = (u32)time(NULL) ^ 0x9E3779B9u;
    if (web_rng_state == 0)
      web_rng_state = 1;
    web_rng_seeded = true;
  }

  u32 x = web_rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  web_rng_state = x;
  return x;
}

static f32 web_sin(f32 x) { return (f32)sin((f64)x); }
static f32 web_cos(f32 x) { return (f32)cos((f64)x); }
static f32 web_tan(f32 x) { return (f32)tan((f64)x); }
static f32 web_acos(f32 x) { return (f32)acos((f64)x); }
static f32 web_sqrt(f32 x) { return (f32)sqrt((f64)x); }
static f32 web_abs(f32 x) { return (f32)fabs((f64)x); }

static i32 web_random(void)
{
  return (i32)(web_next_random() & 0x7FFFFFFFu);
}

static i32 web_random_range(i32 min, i32 max)
{
  if (min > max)
  {
    i32 tmp = min;
    min = max;
    max = tmp;
  }
  u64 span = (u64)((i64)max - (i64)min + 1);
  return (i32)((i64)min + (i64)(web_next_random() % span));
}

static f32 web_frandom(void)
{
  return (f32)(web_next_random() & 0xFFFFFFu) / 16777216.0f;
}

static f32 web_frandom_range(f32 min, f32 max)
{
  if (min > max)
  {
    f32 tmp = min;
    min = max;
    max = tmp;
  }
  return min + (max - min) * web_frandom();
}

static const EgnMathInterface web_math_interface = {
    .name = EGN_MATH_PLATFORM_NAME,
    .sin = web_sin,
    .cos = web_cos,
    .tan = web_tan,
    .acos = web_acos,
    .sqrt = web_sqrt,
    .abs = web_abs,
    .random = web_random,
    .random_range = web_random_range,
    .frandom = web_frandom,
    .frandom_range = web_frandom_range,
};

const EgnMathInterface *egn_math_interface(void)
{
  return &web_math_interface;
}

f32 egnsin(f32 x) { return web_math_interface.sin(x); }
f32 egncos(f32 x) { return web_math_interface.cos(x); }
f32 egntan(f32 x) { return web_math_interface.tan(x); }
f32 egnacos(f32 x) { return web_math_interface.acos(x); }
f32 egnsqrt(f32 x) { return web_math_interface.sqrt(x); }
f32 egnabs(f32 x) { return web_math_interface.abs(x); }
i32 egnrandom(void) { return web_math_interface.random(); }
i32 egnrandom_in_range(i32 min, i32 max) { return web_math_interface.random_range(min, max); }
f32 egnfrandom(void) { return web_math_interface.frandom(); }
f32 egnfrandom_in_range(f32 min, f32 max) { return web_math_interface.frandom_range(min, max); }
