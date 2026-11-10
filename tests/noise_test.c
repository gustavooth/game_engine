#include "defines.h"
#include "math/math.h"

#include <math.h>
#include <stdio.h>

static int failures = 0;

static void expect_true(int condition, const char *message)
{
  if (!condition)
  {
    fprintf(stderr, "FAIL: %s\n", message);
    failures++;
  }
}

int main(void)
{
  // Determinism: the same seed and coordinate must always give the same value.
  f32 a = egn_perlin2(1337u, 12.25f, -4.75f);
  f32 b = egn_perlin2(1337u, 12.25f, -4.75f);
  expect_true(a == b, "perlin2 is deterministic");

  f32 c = egn_perlin2(1338u, 12.25f, -4.75f);
  expect_true(a != c, "different seeds produce different values");

  // Range checks over a grid.
  for (int y = -20; y <= 20; y++)
  {
    for (int x = -20; x <= 20; x++)
    {
      f32 n = egn_perlin2(42u, (f32)x * 0.37f, (f32)y * 0.41f);
      expect_true(n >= -1.0f && n <= 1.0f, "perlin2 stays in [-1, 1]");

      f32 f = egn_fbm2(42u, (f32)x * 0.11f, (f32)y * 0.13f, 5, 2.0f, 0.5f);
      expect_true(f >= -1.0f && f <= 1.0f, "fbm2 stays in [-1, 1]");

      f32 r = egn_ridged2(42u, (f32)x * 0.11f, (f32)y * 0.13f, 4, 2.0f, 0.5f);
      expect_true(r >= 0.0f && r <= 1.0f, "ridged2 stays in [0, 1]");
    }
  }

  expect_true(fabsf(egn_smoothstep(0.0f, 1.0f, -1.0f)) < 0.0001f,
              "smoothstep clamps below the lower edge");
  expect_true(fabsf(egn_smoothstep(0.0f, 1.0f, 2.0f) - 1.0f) < 0.0001f,
              "smoothstep clamps above the upper edge");

  if (failures == 0)
    printf("noise_test: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
