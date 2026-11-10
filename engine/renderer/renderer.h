#pragma once

#include "defines.h"
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

// ---------------------------------------------------------------------------
// Resource API used by the scene components and the V SDK
// ---------------------------------------------------------------------------

// Loads an image (PNG, JPG, BMP, ...) through stb_image. Returns 0 on failure.
EGNAPI u32 renderer_texture_load(const char *path);

// Releases a texture previously returned by renderer_texture_load().
EGNAPI void renderer_texture_release(u32 texture);

// Handles for the renderer-owned primitive meshes.
EGNAPI u32 renderer_mesh_cube(void);
EGNAPI u32 renderer_mesh_sphere(void);

// Loads a static/skinned mesh from disk. Returns 0 on failure.
EGNAPI u32 renderer_mesh_load_obj(const char *path);
EGNAPI u32 renderer_mesh_load_glb(const char *path);

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
