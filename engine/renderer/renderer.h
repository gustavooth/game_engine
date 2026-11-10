#pragma once

#include "defines.h"
#include "renderer_types.h"
#include "platform/window.h"

// ---------------------------------------------------------------------------
// Renderer lifecycle (owned by the engine)
// ---------------------------------------------------------------------------

b8 renderer_initialize(Window *win, u32 width, u32 height);
void renderer_shutdown();

void renderer_resize(u32 width, u32 height);

b8 renderer_draw_frame(RenderPacket packet);

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

// Requests a PNG screenshot of the next rendered frame.
EGNAPI b8 renderer_screenshot(const char *path);
