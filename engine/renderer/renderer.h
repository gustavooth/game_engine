#pragma once

#include "defines.h"
#include "containers/egnstring.h"
#include "renderer_types.h"
#include "platform/window.h"

// ---------------------------------------------------------------------------
// Renderer lifecycle (owned by the engine)
// ---------------------------------------------------------------------------

b8 renderer_initialize(Window *win, u32 width, u32 height, b8 vsync);
void renderer_shutdown();

void renderer_resize(u32 width, u32 height);

b8 renderer_draw_frame(RenderPacket packet);

void renderer_set_vsync(b8 vsync);

// Graphics settings are applied as one atomic batch. The game can change the
// whole structure at any time; the renderer compares it with the active
// configuration at the start of the next frame and recreates only the
// resources whose settings changed. This avoids partially recreated pipelines
// and keeps runtime quality changes predictable.
typedef enum RendererQuality
{
  RENDERER_QUALITY_LOW = 0,
  RENDERER_QUALITY_MEDIUM = 1,
  RENDERER_QUALITY_HIGH = 2,
  RENDERER_QUALITY_ULTRA = 3
} RendererQuality;

typedef enum RendererGraphicsPreset
{
  RENDERER_PRESET_CUSTOM = 0,
  RENDERER_PRESET_LOW = 1,
  RENDERER_PRESET_MEDIUM = 2,
  RENDERER_PRESET_HIGH = 3,
  RENDERER_PRESET_ULTRA = 4
} RendererGraphicsPreset;

typedef enum RendererToneMapper
{
  RENDERER_TONEMAP_REINHARD = 0,
  RENDERER_TONEMAP_ACES = 1,
  RENDERER_TONEMAP_AGX = 2
} RendererToneMapper;

typedef struct RendererGraphicsSettings
{
  // Informational label. Every field below is always used explicitly, so a
  // caller can start from a preset and override individual values.
  RendererGraphicsPreset preset;

  // HDR scene target and tone mapping.
  b8 hdr_enabled;
  b8 tone_mapping_enabled;
  RendererToneMapper tone_mapper;
  f32 exposure_compensation;

  // Automatic exposure (histogram on medium/high/ultra).
  b8 auto_exposure_enabled;
  RendererQuality auto_exposure_quality;
  f32 auto_exposure_min_ev;
  f32 auto_exposure_max_ev;
  f32 auto_exposure_speed;

  // Color grading. Temperatures are in the -1..1 range, tint too. Lift,
  // gamma and gain are RGB triples.
  b8 color_grading_enabled;
  f32 contrast;
  f32 saturation;
  f32 temperature;
  f32 tint;
  f32 lift_r;
  f32 lift_g;
  f32 lift_b;
  f32 gamma_r;
  f32 gamma_g;
  f32 gamma_b;
  f32 gain_r;
  f32 gain_g;
  f32 gain_b;
  f32 vignette;
  b8 lut_enabled;
  f32 lut_strength;
  u32 lut_texture; // renderer_lut_load() handle, 0 = identity

  // Bloom.
  b8 bloom_enabled;
  RendererQuality bloom_quality;
  f32 bloom_threshold;
  f32 bloom_intensity;
  f32 bloom_radius;

  // Directional shadows (CSM).
  b8 shadows_enabled;
  RendererQuality shadow_quality;
  u32 shadow_cascades; // 0 = automatic from quality
  f32 shadow_distance;
  f32 shadow_softness;

  // Physically based materials.
  b8 pbr_enabled;

  // Screen-space ambient occlusion.
  b8 ao_enabled;
  RendererQuality ao_quality;
  f32 ao_radius;
  f32 ao_intensity;

  // Screen-space global illumination.
  b8 gi_enabled;
  RendererQuality gi_quality;
  f32 gi_intensity;
  f32 gi_distance;

  // Atmospheric scattering, sky ambient and fog.
  b8 atmosphere_enabled;
  RendererQuality atmosphere_quality;
  f32 atmosphere_rayleigh;
  f32 atmosphere_mie;
  f32 atmosphere_turbidity;
  f32 atmosphere_mie_anisotropy;
  f32 atmosphere_sun_intensity;
  b8 fog_enabled;
  f32 fog_density;
  f32 fog_height_falloff;
} RendererGraphicsSettings;

// Returns the default settings (the high preset).
EGNAPI RendererGraphicsSettings renderer_graphics_settings_default(void);

// Returns a fully populated preset. The preset field is set to the requested
// preset so callers can label/tweak the result before applying it.
EGNAPI RendererGraphicsSettings renderer_graphics_settings_preset(RendererGraphicsPreset preset);

// Applies the whole structure as one atomic batch on the next frame. Returns
// false when the structure is null or contains an invalid enum/range.
EGNAPI b8 renderer_set_graphics_settings(const RendererGraphicsSettings *settings);

// Returns the most recently requested settings. Use
// renderer_get_active_graphics_settings() to inspect what is currently
// rendering (it can lag by one frame).
EGNAPI RendererGraphicsSettings renderer_get_graphics_settings(void);
EGNAPI RendererGraphicsSettings renderer_get_active_graphics_settings(void);

// Loads a .cube 3D LUT and returns a texture handle usable by lut_texture.
// The handle is released with renderer_texture_release().
EGNAPI u32 renderer_lut_load(const char *path);

// ---------------------------------------------------------------------------
// Resource API used by the scene components and the V SDK
// ---------------------------------------------------------------------------

// Loads an image (PNG, JPG, BMP, ...) through stb_image. Returns 0 on failure.
EGNAPI u32 renderer_texture_load(const char *path);

// Rasterizes text as a texture, to be displayed by a Sprite in any domain.
EGNAPI u32 renderer_text_texture_load(const char *font_path, const EgnString *text,
                                      f32 pixel_height, u32 *width, u32 *height);

// Releases a texture previously returned by renderer_texture_load().
EGNAPI void renderer_texture_release(u32 texture);

// Changes the sampler address mode of an already loaded texture. Repeat is
// useful for tiling water/ocean surfaces; the default is clamp-to-edge.
EGNAPI b8 renderer_texture_set_repeat(u32 texture, b8 repeat);

// Handles for the renderer-owned primitive meshes.
EGNAPI u32 renderer_mesh_cube(void);
EGNAPI u32 renderer_mesh_sphere(void);

// Creates a static mesh from tightly packed CPU arrays. positions/normals are
// vec3 (3 floats per vertex), uvs are vec2 and indices are u32. The arrays are
// copied into GPU buffers; the caller keeps ownership. Returns a 1-based mesh
// handle, or 0 on failure.
EGNAPI u32 renderer_mesh_create_static(const f32 *positions, const f32 *normals,
                                      const f32 *uvs, u32 vertex_count,
                                      const u32 *indices, u32 index_count);

// Loads a static/skinned mesh from disk. Returns 0 on failure.
EGNAPI u32 renderer_mesh_load_obj(const char *path);
EGNAPI u32 renderer_mesh_load_glb(const char *path);

// Releases a mesh handle returned by load_obj/load_glb. Renderer-owned
// primitives (mesh_cube/mesh_sphere) are ignored.
EGNAPI void renderer_mesh_release(u32 mesh);

// Returns the CPU collision representation of a mesh. The pointers stay
// valid while the mesh handle exists.
EGNAPI b8 renderer_mesh_get_collision(u32 mesh, const vec3 **positions,
                                      u32 *position_count, const u32 **indices,
                                      u32 *index_count);

// Animation metadata for skinned GLB meshes.
EGNAPI u32 renderer_mesh_animation_count(u32 mesh);
EGNAPI const char *renderer_mesh_animation_name(u32 mesh, u32 index);
EGNAPI f32 renderer_mesh_animation_duration(u32 mesh, u32 index);

// Requests a PNG screenshot of the next rendered frame.
EGNAPI b8 renderer_screenshot(const char *path);
