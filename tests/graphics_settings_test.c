#include "renderer/renderer.h"
#include <assert.h>

int main(void)
{
  RendererGraphicsSettings defaults = renderer_graphics_settings_default();
  assert(defaults.shadows_enabled && defaults.shadow_quality == RENDERER_QUALITY_MEDIUM);
  RendererGraphicsSettings requested = {false, RENDERER_QUALITY_HIGH};
  assert(renderer_set_graphics_settings(&requested));
  RendererGraphicsSettings current = renderer_get_graphics_settings();
  assert(!current.shadows_enabled && current.shadow_quality == RENDERER_QUALITY_HIGH);
  requested.shadows_enabled = true;
  requested.shadow_quality = (RendererQuality)99;
  assert(!renderer_set_graphics_settings(&requested));
  assert(!renderer_set_graphics_settings(0));
  current = renderer_get_graphics_settings();
  assert(!current.shadows_enabled && current.shadow_quality == RENDERER_QUALITY_HIGH);
  assert(renderer_set_graphics_settings(&defaults));
  return 0;
}
