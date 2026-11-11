// Headless (server) mode: the engine must start, tick scene + physics and shut
// down without creating a window or initializing the renderer.
#include "core/engine.h"
#include "scene/scene.h"
#include "renderer/renderer.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
  EngineSettings settings = {0};
  settings.window_name = "headless";
  settings.target_fps = 120;
  settings.headless = true;

  void *engine = 0;
  assert(engine_create_instance(&settings, &engine));
  assert(engine && engine_is_headless(engine));
  assert(engine_should_run(engine));

  // No window exists in this mode.
  i32 w = -1, h = -1;
  assert(!engine_get_window_size(engine, &w, &h));
  assert(!engine_is_pointer_locked(engine));
  engine_set_pointer_lock(engine, true);
  engine_set_vsync(engine, false);

  // GPU-only entry points must fail gracefully instead of crashing.
  assert(renderer_mesh_load_obj("missing.obj") == 0);
  assert(renderer_mesh_load_glb("missing.glb") == 0);
  assert(!renderer_screenshot("/tmp/never.png"));

  // Scene and physics still run: a dynamic box falls under gravity.
  void *obj = egn_object_create(egn_scene_root(), "falling");
  assert(obj);
  egn_object_set_position(obj, 0.0f, 10.0f, 0.0f);
  egn_object_add_rigidbody(obj, EGN_PHYSICS_MOTION_DYNAMIC, false, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f, true);
  egn_object_add_collider(obj, EGN_COLLIDER_BOX, 1.0f, 1.0f, 1.0f, 0.5f, 0, 0.0f, 0.0f, 0.0f, true);

  for (int i = 0; i < 60; i++)
    assert(engine_frame(engine));

  f32 x, y, z;
  egn_object_get_position(obj, &x, &y, &z);
  assert(y < 10.0f);
  assert(engine_get_elapsed_time(engine) > 0.0);

  engine_request_quit(engine);
  assert(!engine_should_run(engine));
  assert(!engine_frame(engine));

  engine_destroy_instance(engine);
  engine_destroy_instance(engine); // safe to call twice
  printf("headless_test OK (y=%.3f)\n", y);
  return 0;
}
