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

// Set the whole structure before engine creation or
// between frames; changes take effect together on the next frame.
typedef enum RendererQuality
{
  RENDERER_QUALITY_LOW = 0,
  RENDERER_QUALITY_MEDIUM = 1,
  RENDERER_QUALITY_HIGH = 2
} RendererQuality;

typedef struct RendererGraphicsSettings
{
  b8 shadows_enabled;
  RendererQuality shadow_quality;
} RendererGraphicsSettings;

EGNAPI RendererGraphicsSettings renderer_graphics_settings_default(void);
EGNAPI b8 renderer_set_graphics_settings(const RendererGraphicsSettings *settings);
EGNAPI RendererGraphicsSettings renderer_get_graphics_settings(void);

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
