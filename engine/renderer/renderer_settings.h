#pragma once

#include "renderer/renderer.h"

// Shared graphics-settings helpers used by every renderer backend.

RendererGraphicsSettings renderer_graphics_settings_default(void);
RendererGraphicsSettings renderer_graphics_settings_preset(RendererGraphicsPreset preset);

// Validates enums and the ranges that would otherwise produce broken GPU
// resources. Callers should reject the whole batch when this returns false.
b8 renderer_graphics_settings_valid(const RendererGraphicsSettings *settings);

// Compares two settings structures field by field (padding safe). Used to
// decide whether a runtime batch needs to recreate anything.
b8 renderer_graphics_settings_equal(const RendererGraphicsSettings *a,
                                    const RendererGraphicsSettings *b);

// Returns the effective number of cascades (resolves 0 = automatic).
u32 renderer_graphics_settings_cascades(const RendererGraphicsSettings *settings);

// Shadow map resolution for the current quality.
u32 renderer_graphics_settings_shadow_size(const RendererGraphicsSettings *settings);
