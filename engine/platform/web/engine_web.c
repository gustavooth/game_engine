#include "core/engine.h"
#include "core/inputs.h"
#include "core/memo.h"
#include "platform/platform.h"
#include "platform/filemanager.h"
#include "renderer/renderer.h"
#include "text/font.h"
#include <emscripten/emscripten.h>
#include <string.h>

int egn_web_init(void);
void egn_web_update(double delta_time);
void egn_web_shutdown(void);

EM_ASYNC_JS(int, web_load_asset, (const char *path, int kind), {
  try {
    const renderer = globalThis.egnWebRenderer;
    const url = UTF8ToString(path);
    if (kind === 0) return await renderer.loadTexture(url);
    if (kind === 1) return (await renderer.loadOBJ(url)).id;
    const loaded = await renderer.loadGLB(url);
    renderer.meshes.get(loaded.id).defaultTexture = loaded.textureId ?? 0;
    return loaded.id;
  } catch (error) { console.error(error); return 0; }
});
EM_JS(int, web_upload_pixels, (const unsigned char *pixels, int width, int height), {
  const renderer = globalThis.egnWebRenderer;
  const id = renderer.nextTextureId++;
  renderer.registerTexturePixels(id, width, height, HEAPU8.subarray(pixels, pixels + width * height * 4));
  return id;
});
EM_JS(void, web_release_texture, (int id), {
  const renderer = globalThis.egnWebRenderer;
  const texture = renderer?.textures.get(id);
  if (texture && id !== 0) { texture.texture.destroy(); renderer.textures.delete(id); }
});
EM_JS(void, web_render_frame, (double dt, int shadows, int quality), {
  const renderer = globalThis.egnWebRenderer;
  renderer.graphicsSettings = { shadowsEnabled: !!shadows, shadowQuality: quality };
  renderer.render(dt);
});
EM_JS(void, web_dispose, (), { globalThis.egnWebDispose?.(); });
EM_JS(int, web_canvas_width, (), { return globalThis.egnWebRenderer.canvas.width; });
EM_JS(int, web_canvas_height, (), { return globalThis.egnWebRenderer.canvas.height; });
EM_JS(int, web_pointer_locked, (), { return document.pointerLockElement === globalThis.egnWebRenderer.canvas; });
EM_JS(void, web_pointer_request, (int locked), {
  globalThis.egnWantPointerLock = !!locked;
  if (!locked && document.pointerLockElement) document.exitPointerLock();
});
EM_JS(int, web_clip_count, (int id), { return globalThis.egnWebRenderer.meshes.get(id)?.data.animations?.length || 0; });
EM_JS(double, web_clip_duration, (int id, int index), { return globalThis.egnWebRenderer.meshes.get(id)?.data.animations?.[index]?.duration || 0; });
EM_JS(void, web_clip_name, (int id, int index, char *out, int capacity), {
  stringToUTF8(globalThis.egnWebRenderer.meshes.get(id)?.data.animations?.[index]?.name || "", out, capacity);
});
EM_JS(int, web_screenshot, (), {
  const canvas = globalThis.egnWebRenderer.canvas;
  const link = document.createElement('a'); link.download = 'screenshot.png';
  link.href = canvas.toDataURL('image/png'); link.click(); return 1;
});

typedef struct WebEngine { b8 running, vsync; i32 target_fps; f64 start, last, delta; } WebEngine;
static WebEngine engine;
static RendererGraphicsSettings graphics_settings = {true, RENDERER_QUALITY_MEDIUM};

RendererGraphicsSettings renderer_graphics_settings_default(void)
{
  RendererGraphicsSettings settings = {true, RENDERER_QUALITY_MEDIUM};
  return settings;
}
b8 renderer_set_graphics_settings(const RendererGraphicsSettings *settings)
{
  if (!settings || settings->shadow_quality < RENDERER_QUALITY_LOW ||
      settings->shadow_quality > RENDERER_QUALITY_HIGH) return false;
  graphics_settings = *settings;
  graphics_settings.shadows_enabled = !!settings->shadows_enabled;
  return true;
}
RendererGraphicsSettings renderer_get_graphics_settings(void) { return graphics_settings; }

b8 engine_create_instance(EngineSettings *settings, void **out)
{
  if (!settings || !out || engine.running || !egn_web_init()) return false;
  engine = (WebEngine){.running = true, .vsync = settings->vsync, .target_fps = settings->target_fps};
  engine.start = engine.last = platform_get_absolute_time();
  *out = &engine;
  return true;
}
b8 engine_frame(void *instance)
{
  if (instance != &engine || !engine.running) return false;
  f64 now = platform_get_absolute_time();
  engine.delta = now - engine.last; engine.last = now;
  if (engine.delta > 0.1) engine.delta = 0.1;
  egn_web_update(engine.delta);
  web_render_frame(engine.delta, graphics_settings.shadows_enabled, graphics_settings.shadow_quality);
  inputs_update(engine.delta);
  emscripten_sleep(engine.target_fps > 0 ? (u32)(1000 / engine.target_fps) : 1);
  return engine.running;
}
b8 engine_should_run(void *instance) { return instance == &engine && engine.running; }
void engine_request_quit(void *instance) { if (instance == &engine) engine.running = false; }
void engine_destroy_instance(void *instance) { if (instance == &engine) { engine.running = false; web_dispose(); egn_web_shutdown(); } }
f64 engine_get_delta_time(void *instance) { return instance == &engine ? engine.delta : 0; }
f64 engine_get_elapsed_time(void *instance) { return instance == &engine ? platform_get_absolute_time() - engine.start : 0; }
b8 engine_get_window_size(void *instance, i32 *width, i32 *height) { if (instance != &engine || !width || !height) return false; *width = web_canvas_width(); *height = web_canvas_height(); return true; }
void engine_set_vsync(void *instance, b8 vsync) { if (instance == &engine) engine.vsync = vsync; }
b8 engine_get_vsync(void *instance) { return instance == &engine && engine.vsync; }
void engine_set_target_fps(void *instance, i32 fps) { if (instance == &engine) engine.target_fps = fps; }
i32 engine_get_target_fps(void *instance) { return instance == &engine ? engine.target_fps : 0; }
void engine_set_pointer_lock(void *instance, b8 locked) { if (instance == &engine) web_pointer_request(locked); }
b8 engine_is_pointer_locked(void *instance) { return instance == &engine && web_pointer_locked(); }

u32 renderer_texture_load(const char *path) { return path ? web_load_asset(path, 0) : 0; }
void renderer_texture_release(u32 texture) { web_release_texture(texture); }
u32 renderer_mesh_cube(void) { return 1; }
u32 renderer_mesh_sphere(void) { return 2; }
u32 renderer_mesh_load_obj(const char *path) { return path ? web_load_asset(path, 1) : 0; }
u32 renderer_mesh_load_glb(const char *path) { return path ? web_load_asset(path, 2) : 0; }
void renderer_mesh_release(u32 mesh) { (void)mesh; }
u32 renderer_mesh_animation_count(u32 mesh) { return web_clip_count(mesh); }
const char *renderer_mesh_animation_name(u32 mesh, u32 index) { static char name[256]; web_clip_name(mesh, index, name, sizeof(name)); return name; }
f32 renderer_mesh_animation_duration(u32 mesh, u32 index) { return (f32)web_clip_duration(mesh, index); }
b8 renderer_screenshot(const char *path) { (void)path; return web_screenshot(); }
u32 renderer_text_texture_load(const char *path, const EgnString *text, f32 size, u32 *width, u32 *height)
{
  u8 *font = 0; u64 length = 0;
  if (!filemanager_read_all(path, &font, &length)) return 0;
  u8 *pixels = length <= 0xffffffffu ? egn_font_rasterize(font, (u32)length, text, size, width, height) : 0;
  MEMOFREE(font);
  if (!pixels) return 0;
  u32 texture = web_upload_pixels(pixels, *width, *height);
  egn_font_pixels_free(pixels);
  return texture;
}
