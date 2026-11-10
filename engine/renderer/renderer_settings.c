#include "renderer/renderer_settings.h"

#include <math.h>
#include <string.h>

static b8 quality_valid(RendererQuality quality)
{
  return quality >= RENDERER_QUALITY_LOW && quality <= RENDERER_QUALITY_ULTRA;
}

static void set_atmosphere(RendererGraphicsSettings *settings, RendererQuality quality)
{
  settings->atmosphere_enabled = true;
  settings->atmosphere_quality = quality;
  settings->atmosphere_rayleigh = 1.0f;
  settings->atmosphere_mie = 0.0035f;
  settings->atmosphere_turbidity = 2.4f;
  settings->atmosphere_mie_anisotropy = 0.76f;
  settings->atmosphere_sun_intensity = 20.0f;
  settings->fog_enabled = true;
  settings->fog_density = 0.008f;
  settings->fog_height_falloff = 0.12f;

  if (quality == RENDERER_QUALITY_LOW)
  {
    settings->atmosphere_mie = 0.006f;
    settings->fog_enabled = false;
    settings->fog_density = 0.0f;
  }
  else if (quality == RENDERER_QUALITY_ULTRA)
  {
    settings->atmosphere_mie = 0.003f;
    settings->fog_density = 0.006f;
  }
}

static void set_shadows(RendererGraphicsSettings *settings, RendererQuality quality)
{
  settings->shadows_enabled = true;
  settings->shadow_quality = quality;
  settings->shadow_cascades = 0; // automatic from quality
  settings->shadow_distance = 120.0f;
  settings->shadow_softness = 1.0f;

  if (quality == RENDERER_QUALITY_LOW)
  {
    settings->shadow_cascades = 2;
    settings->shadow_distance = 55.0f;
    settings->shadow_softness = 0.6f;
  }
  else if (quality == RENDERER_QUALITY_MEDIUM)
  {
    settings->shadow_cascades = 3;
    settings->shadow_distance = 85.0f;
    settings->shadow_softness = 0.9f;
  }
  else if (quality == RENDERER_QUALITY_ULTRA)
  {
    settings->shadow_cascades = 4;
    settings->shadow_distance = 160.0f;
    settings->shadow_softness = 1.4f;
  }
}

static void set_post_effects(RendererGraphicsSettings *settings, RendererQuality quality)
{
  settings->bloom_enabled = quality >= RENDERER_QUALITY_MEDIUM;
  settings->bloom_quality = quality;
  settings->bloom_threshold = 1.1f;
  settings->bloom_intensity = 0.055f;
  settings->bloom_radius = 0.55f;

  if (quality == RENDERER_QUALITY_ULTRA)
  {
    settings->bloom_threshold = 1.0f;
    settings->bloom_intensity = 0.085f;
    settings->bloom_radius = 0.7f;
  }
  else if (quality == RENDERER_QUALITY_LOW)
  {
    settings->bloom_intensity = 0.0f;
  }
}

static void set_ambient_effects(RendererGraphicsSettings *settings, RendererQuality quality)
{
  settings->ao_enabled = quality >= RENDERER_QUALITY_MEDIUM;
  settings->ao_quality = quality;
  settings->ao_radius = quality == RENDERER_QUALITY_ULTRA ? 1.25f : 1.0f;
  settings->ao_intensity = quality == RENDERER_QUALITY_ULTRA ? 0.9f : 0.7f;

  settings->gi_enabled = quality >= RENDERER_QUALITY_MEDIUM;
  settings->gi_quality = quality;
  settings->gi_intensity = quality == RENDERER_QUALITY_ULTRA ? 0.8f : 0.55f;
  settings->gi_distance = quality == RENDERER_QUALITY_ULTRA ? 18.0f : 12.0f;
}

RendererGraphicsSettings renderer_graphics_settings_preset(RendererGraphicsPreset preset)
{
  RendererGraphicsSettings settings;
  memset(&settings, 0, sizeof(settings));

  settings.preset = preset;
  settings.hdr_enabled = true;
  settings.tone_mapping_enabled = true;
  settings.tone_mapper = RENDERER_TONEMAP_ACES;
  settings.exposure_compensation = 0.0f;

  settings.auto_exposure_enabled = true;
  settings.auto_exposure_quality = RENDERER_QUALITY_HIGH;
  settings.auto_exposure_min_ev = -6.0f;
  settings.auto_exposure_max_ev = 6.0f;
  settings.auto_exposure_speed = 1.5f;

  settings.color_grading_enabled = true;
  settings.contrast = 1.0f;
  settings.saturation = 1.0f;
  settings.temperature = 0.0f;
  settings.tint = 0.0f;
  settings.lift_r = 0.0f;
  settings.lift_g = 0.0f;
  settings.lift_b = 0.0f;
  settings.gamma_r = 1.0f;
  settings.gamma_g = 1.0f;
  settings.gamma_b = 1.0f;
  settings.gain_r = 1.0f;
  settings.gain_g = 1.0f;
  settings.gain_b = 1.0f;
  settings.vignette = 0.15f;
  settings.lut_enabled = false;
  settings.lut_strength = 1.0f;
  settings.lut_texture = 0;

  set_shadows(&settings, RENDERER_QUALITY_HIGH);
  set_post_effects(&settings, RENDERER_QUALITY_HIGH);
  set_ambient_effects(&settings, RENDERER_QUALITY_HIGH);
  set_atmosphere(&settings, RENDERER_QUALITY_HIGH);
  settings.pbr_enabled = true;

  switch (preset)
  {
  case RENDERER_PRESET_LOW:
    settings.hdr_enabled = false;
    settings.tone_mapper = RENDERER_TONEMAP_REINHARD;
    settings.auto_exposure_enabled = false;
    settings.auto_exposure_quality = RENDERER_QUALITY_LOW;
    settings.auto_exposure_min_ev = -4.0f;
    settings.auto_exposure_max_ev = 4.0f;
    settings.auto_exposure_speed = 1.0f;
    settings.vignette = 0.0f;
    set_shadows(&settings, RENDERER_QUALITY_LOW);
    set_post_effects(&settings, RENDERER_QUALITY_LOW);
    set_ambient_effects(&settings, RENDERER_QUALITY_LOW);
    set_atmosphere(&settings, RENDERER_QUALITY_LOW);
    break;

  case RENDERER_PRESET_MEDIUM:
    settings.auto_exposure_quality = RENDERER_QUALITY_MEDIUM;
    settings.auto_exposure_min_ev = -5.0f;
    settings.auto_exposure_max_ev = 5.0f;
    settings.auto_exposure_speed = 1.2f;
    set_shadows(&settings, RENDERER_QUALITY_MEDIUM);
    set_post_effects(&settings, RENDERER_QUALITY_MEDIUM);
    set_ambient_effects(&settings, RENDERER_QUALITY_MEDIUM);
    set_atmosphere(&settings, RENDERER_QUALITY_MEDIUM);
    break;

  case RENDERER_PRESET_ULTRA:
    settings.auto_exposure_quality = RENDERER_QUALITY_ULTRA;
    settings.auto_exposure_min_ev = -7.0f;
    settings.auto_exposure_max_ev = 7.0f;
    settings.auto_exposure_speed = 1.8f;
    settings.vignette = 0.18f;
    set_shadows(&settings, RENDERER_QUALITY_ULTRA);
    set_post_effects(&settings, RENDERER_QUALITY_ULTRA);
    set_ambient_effects(&settings, RENDERER_QUALITY_ULTRA);
    set_atmosphere(&settings, RENDERER_QUALITY_ULTRA);
    break;

  case RENDERER_PRESET_HIGH:
  case RENDERER_PRESET_CUSTOM:
  default:
    break;
  }

  return settings;
}

RendererGraphicsSettings renderer_graphics_settings_default(void)
{
  return renderer_graphics_settings_preset(RENDERER_PRESET_HIGH);
}

u32 renderer_graphics_settings_cascades(const RendererGraphicsSettings *settings)
{
  if (!settings)
    return 1;

  if (settings->shadow_cascades >= 1 && settings->shadow_cascades <= 4)
    return settings->shadow_cascades;

  switch (settings->shadow_quality)
  {
  case RENDERER_QUALITY_LOW:
    return 2;
  case RENDERER_QUALITY_MEDIUM:
    return 3;
  case RENDERER_QUALITY_HIGH:
  case RENDERER_QUALITY_ULTRA:
  default:
    return 4;
  }
}

u32 renderer_graphics_settings_shadow_size(const RendererGraphicsSettings *settings)
{
  if (!settings)
    return 2048;

  switch (settings->shadow_quality)
  {
  case RENDERER_QUALITY_LOW:
    return 1024;
  case RENDERER_QUALITY_MEDIUM:
    return 1536;
  case RENDERER_QUALITY_ULTRA:
    return 4096;
  case RENDERER_QUALITY_HIGH:
  default:
    return 2048;
  }
}

b8 renderer_graphics_settings_valid(const RendererGraphicsSettings *settings)
{
  if (!settings)
    return false;

  if (settings->preset < RENDERER_PRESET_CUSTOM || settings->preset > RENDERER_PRESET_ULTRA)
    return false;
  if (!quality_valid(settings->auto_exposure_quality) ||
      !quality_valid(settings->bloom_quality) ||
      !quality_valid(settings->shadow_quality) ||
      !quality_valid(settings->ao_quality) ||
      !quality_valid(settings->gi_quality) ||
      !quality_valid(settings->atmosphere_quality))
    return false;
  if (settings->tone_mapper < RENDERER_TONEMAP_REINHARD ||
      settings->tone_mapper > RENDERER_TONEMAP_AGX)
    return false;
  if (settings->shadow_cascades > 4)
    return false;

  if (settings->auto_exposure_min_ev > settings->auto_exposure_max_ev)
    return false;
  if (settings->auto_exposure_speed < 0.0f)
    return false;
  if (settings->shadow_distance <= 0.0f)
    return false;
  if (settings->shadow_softness < 0.0f)
    return false;
  if (settings->bloom_threshold < 0.0f || settings->bloom_intensity < 0.0f ||
      settings->bloom_radius < 0.0f)
    return false;
  if (settings->ao_radius <= 0.0f || settings->ao_intensity < 0.0f)
    return false;
  if (settings->gi_intensity < 0.0f || settings->gi_distance <= 0.0f)
    return false;
  if (settings->fog_density < 0.0f || settings->fog_height_falloff < 0.0f)
    return false;
  if (settings->lut_strength < 0.0f)
    return false;

  if (!isfinite(settings->exposure_compensation) ||
      !isfinite(settings->atmosphere_rayleigh) ||
      !isfinite(settings->atmosphere_mie) ||
      !isfinite(settings->atmosphere_turbidity))
    return false;

  return true;
}

b8 renderer_graphics_settings_equal(const RendererGraphicsSettings *a,
                                    const RendererGraphicsSettings *b)
{
  if (a == b)
    return true;
  if (!a || !b)
    return false;

  RendererGraphicsSettings left;
  RendererGraphicsSettings right;
  memset(&left, 0, sizeof(left));
  memset(&right, 0, sizeof(right));
  memcpy(&left, a, sizeof(left));
  memcpy(&right, b, sizeof(right));
  return memcmp(&left, &right, sizeof(left)) == 0;
}
