#include "renderer/renderer.h"
#include "renderer/renderer_settings.h"

#include <assert.h>
#include <string.h>

int main(void)
{
  RendererGraphicsSettings defaults = renderer_graphics_settings_default();
  assert(defaults.preset == RENDERER_PRESET_HIGH);
  assert(defaults.hdr_enabled && defaults.tone_mapping_enabled);
  assert(defaults.shadows_enabled && defaults.shadow_quality == RENDERER_QUALITY_HIGH);
  assert(defaults.ao_enabled && defaults.gi_enabled && defaults.atmosphere_enabled);
  assert(defaults.bloom_enabled && defaults.auto_exposure_enabled);
  assert(renderer_graphics_settings_cascades(&defaults) == 4);
  assert(renderer_graphics_settings_shadow_size(&defaults) == 2048);

  RendererGraphicsSettings low = renderer_graphics_settings_preset(RENDERER_PRESET_LOW);
  assert(low.preset == RENDERER_PRESET_LOW);
  assert(!low.hdr_enabled && !low.bloom_enabled && !low.ao_enabled && !low.gi_enabled);
  assert(renderer_graphics_settings_cascades(&low) == 2);
  assert(renderer_graphics_settings_shadow_size(&low) == 1024);

  RendererGraphicsSettings ultra = renderer_graphics_settings_preset(RENDERER_PRESET_ULTRA);
  assert(ultra.preset == RENDERER_PRESET_ULTRA);
  assert(ultra.hdr_enabled && ultra.bloom_enabled && ultra.ao_enabled && ultra.gi_enabled);
  assert(renderer_graphics_settings_cascades(&ultra) == 4);
  assert(renderer_graphics_settings_shadow_size(&ultra) == 4096);

  RendererGraphicsSettings requested = renderer_graphics_settings_preset(RENDERER_PRESET_HIGH);
  requested.shadows_enabled = false;
  requested.shadow_quality = RENDERER_QUALITY_ULTRA;
  requested.bloom_intensity = 0.2f;
  requested.shadow_cascades = 2;
  assert(renderer_set_graphics_settings(&requested));

  RendererGraphicsSettings current = renderer_get_graphics_settings();
  assert(!current.shadows_enabled);
  assert(current.shadow_quality == RENDERER_QUALITY_ULTRA);
  assert(current.bloom_intensity == 0.2f);
  assert(current.shadow_cascades == 2);
  assert(renderer_graphics_settings_equal(&current, &requested));

  RendererGraphicsSettings active = renderer_get_active_graphics_settings();
  assert(renderer_graphics_settings_equal(&active, &requested));

  requested.shadows_enabled = true;
  requested.shadow_quality = (RendererQuality)99;
  assert(!renderer_set_graphics_settings(&requested));
  assert(!renderer_set_graphics_settings(0));

  RendererGraphicsSettings invalid_cascades = renderer_graphics_settings_preset(RENDERER_PRESET_HIGH);
  invalid_cascades.shadow_cascades = 9;
  assert(!renderer_set_graphics_settings(&invalid_cascades));

  current = renderer_get_graphics_settings();
  assert(!current.shadows_enabled && current.shadow_quality == RENDERER_QUALITY_ULTRA);
  assert(renderer_set_graphics_settings(&defaults));

  // A copy with zeroed padding must compare equal to the original.
  RendererGraphicsSettings copy;
  memset(&copy, 0, sizeof(copy));
  memcpy(&copy, &defaults, sizeof(copy));
  assert(renderer_graphics_settings_equal(&copy, &defaults));

  return 0;
}
