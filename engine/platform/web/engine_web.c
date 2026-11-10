#include "core/engine.h"
#include "core/inputs.h"
#include "core/memo.h"
#include "platform/platform.h"
#include "platform/filemanager.h"
#include "renderer/renderer.h"
#include "renderer/renderer_settings.h"
#include "text/font.h"
#include <emscripten/emscripten.h>
#include <stdio.h>
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
EM_JS(void, web_apply_graphics_settings, (const char *json), {
  const renderer = globalThis.egnWebRenderer;
  if (renderer && renderer.applyGraphicsSettings) renderer.applyGraphicsSettings(JSON.parse(UTF8ToString(json)));
});
EM_JS(void, web_render_frame, (double dt), {
  const renderer = globalThis.egnWebRenderer;
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
static RendererGraphicsSettings graphics_settings;
static RendererGraphicsSettings active_graphics_settings;
static b8 graphics_settings_initialized;
static b8 graphics_settings_dirty;

static void web_graphics_settings_json(char *out, u64 capacity)
{
  const RendererGraphicsSettings *s = &graphics_settings;
  snprintf(out, (size_t)capacity,
           "{\"preset\":%d,\"hdr_enabled\":%d,\"tone_mapping_enabled\":%d,"
           "\"tone_mapper\":%d,\"exposure_compensation\":%.9g,"
           "\"auto_exposure_enabled\":%d,\"auto_exposure_quality\":%d,"
           "\"auto_exposure_min_ev\":%.9g,\"auto_exposure_max_ev\":%.9g,"
           "\"auto_exposure_speed\":%.9g,"
           "\"color_grading_enabled\":%d,\"contrast\":%.9g,\"saturation\":%.9g,"
           "\"temperature\":%.9g,\"tint\":%.9g,"
           "\"lift_r\":%.9g,\"lift_g\":%.9g,\"lift_b\":%.9g,"
           "\"gamma_r\":%.9g,\"gamma_g\":%.9g,\"gamma_b\":%.9g,"
           "\"gain_r\":%.9g,\"gain_g\":%.9g,\"gain_b\":%.9g,"
           "\"vignette\":%.9g,\"lut_enabled\":%d,\"lut_strength\":%.9g,"
           "\"bloom_enabled\":%d,\"bloom_quality\":%d,\"bloom_threshold\":%.9g,"
           "\"bloom_intensity\":%.9g,\"bloom_radius\":%.9g,"
           "\"shadows_enabled\":%d,\"shadow_quality\":%d,\"shadow_cascades\":%u,"
           "\"shadow_distance\":%.9g,\"shadow_softness\":%.9g,"
           "\"pbr_enabled\":%d,\"ao_enabled\":%d,\"ao_quality\":%d,"
           "\"ao_radius\":%.9g,\"ao_intensity\":%.9g,"
           "\"gi_enabled\":%d,\"gi_quality\":%d,\"gi_intensity\":%.9g,"
           "\"gi_distance\":%.9g,\"atmosphere_enabled\":%d,"
           "\"atmosphere_quality\":%d,\"atmosphere_rayleigh\":%.9g,"
           "\"atmosphere_mie\":%.9g,\"atmosphere_turbidity\":%.9g,"
           "\"atmosphere_mie_anisotropy\":%.9g,"
           "\"atmosphere_sun_intensity\":%.9g,\"fog_enabled\":%d,"
           "\"fog_density\":%.9g,\"fog_height_falloff\":%.9g}",
           (int)s->preset, s->hdr_enabled, s->tone_mapping_enabled,
           (int)s->tone_mapper, s->exposure_compensation,
           s->auto_exposure_enabled, (int)s->auto_exposure_quality,
           s->auto_exposure_min_ev, s->auto_exposure_max_ev,
           s->auto_exposure_speed,
           s->color_grading_enabled, s->contrast, s->saturation,
           s->temperature, s->tint,
           s->lift_r, s->lift_g, s->lift_b,
           s->gamma_r, s->gamma_g, s->gamma_b,
           s->gain_r, s->gain_g, s->gain_b,
           s->vignette, s->lut_enabled, s->lut_strength,
           s->bloom_enabled, (int)s->bloom_quality, s->bloom_threshold,
           s->bloom_intensity, s->bloom_radius,
           s->shadows_enabled, (int)s->shadow_quality, s->shadow_cascades,
           s->shadow_distance, s->shadow_softness,
           s->pbr_enabled, s->ao_enabled, (int)s->ao_quality,
           s->ao_radius, s->ao_intensity,
           s->gi_enabled, (int)s->gi_quality, s->gi_intensity,
           s->gi_distance, s->atmosphere_enabled,
           (int)s->atmosphere_quality, s->atmosphere_rayleigh,
           s->atmosphere_mie, s->atmosphere_turbidity,
           s->atmosphere_mie_anisotropy,
           s->atmosphere_sun_intensity, s->fog_enabled,
           s->fog_density, s->fog_height_falloff);
}

b8 renderer_set_graphics_settings(const RendererGraphicsSettings *settings)
{
  if (!renderer_graphics_settings_valid(settings))
    return false;

  graphics_settings = *settings;
  graphics_settings.shadows_enabled = !!settings->shadows_enabled;
  graphics_settings.hdr_enabled = !!settings->hdr_enabled;
  graphics_settings.auto_exposure_enabled = !!settings->auto_exposure_enabled;
  graphics_settings.tone_mapping_enabled = !!settings->tone_mapping_enabled;
  graphics_settings.color_grading_enabled = !!settings->color_grading_enabled;
  graphics_settings.bloom_enabled = !!settings->bloom_enabled;
  graphics_settings.pbr_enabled = !!settings->pbr_enabled;
  graphics_settings.ao_enabled = !!settings->ao_enabled;
  graphics_settings.gi_enabled = !!settings->gi_enabled;
  graphics_settings.atmosphere_enabled = !!settings->atmosphere_enabled;
  graphics_settings.fog_enabled = !!settings->fog_enabled;
  graphics_settings.lut_enabled = !!settings->lut_enabled;

  if (!graphics_settings_initialized)
  {
    active_graphics_settings = graphics_settings;
    graphics_settings_initialized = true;
  }

  graphics_settings_dirty = true;
  return true;
}

RendererGraphicsSettings renderer_get_graphics_settings(void)
{
  if (!graphics_settings_initialized)
  {
    graphics_settings = renderer_graphics_settings_default();
    active_graphics_settings = graphics_settings;
    graphics_settings_initialized = true;
    graphics_settings_dirty = true;
  }
  return graphics_settings;
}

RendererGraphicsSettings renderer_get_active_graphics_settings(void)
{
  if (!graphics_settings_initialized)
    return renderer_get_graphics_settings();
  return active_graphics_settings;
}

u32 renderer_lut_load(const char *path)
{
  (void)path;
  return 0;
}

b8 engine_create_instance(EngineSettings *settings, void **out)
{
  if (!settings || !out || engine.running || !egn_web_init()) return false;
  engine = (WebEngine){.running = true, .vsync = settings->vsync, .target_fps = settings->target_fps};
  engine.start = engine.last = platform_get_absolute_time();
  renderer_get_graphics_settings();
  active_graphics_settings = graphics_settings;
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
  if (graphics_settings_dirty)
  {
    static char settings_json[2048];
    web_graphics_settings_json(settings_json, sizeof(settings_json));
    web_apply_graphics_settings(settings_json);
    active_graphics_settings = graphics_settings;
    graphics_settings_dirty = false;
  }
  web_render_frame(engine.delta);
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
