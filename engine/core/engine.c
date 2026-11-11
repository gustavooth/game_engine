#include "engine.h"
#include "logger.h"
#include "memo.h"
#include "events.h"
#include "inputs.h"
#include "clock.h"
#include "platform/platform.h"
#include "platform/window.h"
#include "renderer/renderer.h"
#include "scene/scene.h"
#include "physics/physics.h"

#include "core/egnstring.h"
#include "memory/linear_allocator.h"

#include <stdio.h>

#include "platform/filemanager.h"

typedef struct EngineState
{
  LinearAllocator linear_allocator;

  void *logger_state;
  void *platform_state;
  void *memo_state;
  void *events_state;
  void *inputs_state;

  Window window;
  Clock clock;
  b8 running;

  f64 delta_time;
  f64 last_time;

  i32 frames_count;
  f64 time_count;

  char *memory_usage_str;

  b8 platform_ready;
  b8 logger_ready;
  b8 memo_ready;
  b8 events_ready;
  b8 inputs_ready;
  b8 window_ready;
  b8 renderer_ready;
  b8 scene_ready;
  b8 physics_ready;

  b8 vsync;
  i32 target_fps;
  b8 headless;
} EngineState;

static EngineState *engine_ptr;

b8 engine_on_event(u16 code, void *sender, EventContext data);
b8 engine_on_key(u16 code, void *sender, EventContext data);
b8 engine_on_resize(u16 code, void *sender, EventContext data);

// Shuts down only the subsystems that were fully initialized, in reverse order.
static void engine_cleanup(EngineState *instance)
{
  if (!instance)
    return;

  if (instance->physics_ready)
    egn_physics_shutdown();

  if (instance->scene_ready)
    egn_scene_shutdown();

  if (instance->renderer_ready)
    renderer_shutdown();

  if (instance->window_ready)
  {
    window_hide(&instance->window);
    window_destroy(&instance->window);
  }

  if (instance->inputs_ready)
    inputs_shutdown();

  if (instance->events_ready)
    events_shutdown();

  if (instance->memo_ready)
    memo_shutdown();

  if (instance->logger_ready)
    logger_shutdown();

  if (engine_ptr == instance)
    engine_ptr = 0;

  b8 platform_ready = instance->platform_ready;
  MEMOFREE(instance);

  if (platform_ready)
    platform_shutdown();
}

EGNAPI b8 engine_create_instance(EngineSettings *settings, void **out_instance_ptr)
{
  if (engine_ptr)
  {
    EGNERROR("engine_create_instance: only one instance can be created");
    return false;
  }

  if (!settings)
  {
    EGNERROR("engine_create_instance: settings must be a valid pointer.");
    return false;
  }

  if (!out_instance_ptr)
  {
    EGNERROR("engine_create_instance: out_instance_ptr must be a valid pointer.")
    return false;
  }

  EngineState *instance = MEMOZALLOC(sizeof(EngineState), MEMO_NOT_INITIALIZED);
  if (!instance) { *out_instance_ptr = 0; return false; }
  *out_instance_ptr = instance;
  engine_ptr = instance;

  linear_allocator_create(64 * 1024 * 1024, &instance->linear_allocator); // 64mb

  u64 required_memory;

  platform_initialize(&required_memory, 0);
  instance->platform_state = LIALLOC(&instance->linear_allocator, required_memory);
  if (!platform_initialize(0, instance->platform_state))
  {
    EGNERROR("engine_create_instance: failed to initialize platform state");
    engine_cleanup(instance);
    return false;
  }
  instance->platform_ready = true;
  clock_start(&engine_ptr->clock);

  logger_initialize(&required_memory, 0);
  instance->logger_state = LIALLOC(&instance->linear_allocator, required_memory);
  if (!logger_initialize(0, instance->logger_state))
  {
    EGNERROR("engine_create_instance: failed to initialize logger state");
    engine_cleanup(instance);
    return false;
  }
  instance->logger_ready = true;

  memo_initialize(&required_memory, 0);
  instance->memo_state = LIALLOC(&instance->linear_allocator, required_memory);
  if (!memo_initialize(0, instance->memo_state))
  {
    EGNERROR("engine_create_instance: failed to initialize memo state");
    engine_cleanup(instance);
    return false;
  }
  instance->memo_ready = true;

  memo_set_memory_usage_tag(MEMO_LINEAR_ALLOCATOR, instance->linear_allocator.total_size + memo_get_memory_usage_tag(MEMO_LINEAR_ALLOCATOR));
  memo_set_memory_usage_tag(MEMO_ENGINE_INSTANCE, sizeof(EngineState) + memo_get_memory_usage_tag(MEMO_ENGINE_INSTANCE));
  memo_set_allocations_count(2 + memo_get_allocations_count());

  events_initialize(&required_memory, 0);
  instance->events_state = LIALLOC(&instance->linear_allocator, required_memory);
  if (!events_initialize(0, instance->events_state))
  {
    EGNERROR("engine_create_instance: failed to initialize events state");
    engine_cleanup(instance);
    return false;
  }
  instance->events_ready = true;

  inputs_initialize(&required_memory, 0);
  instance->inputs_state = LIALLOC(&instance->linear_allocator, required_memory);
  if (!inputs_initialize(0, instance->inputs_state))
  {
    EGNERROR("engine_create_instance: failed to initialize inputs state");
    engine_cleanup(instance);
    return false;
  }
  instance->inputs_ready = true;

  engine_ptr->headless = settings->headless;
  engine_ptr->vsync = settings->vsync;
  engine_ptr->target_fps = settings->target_fps;

  if (settings->headless)
  {
    EGNINFO("Engine running in headless mode: no window, no renderer.");
  }
  else
  {
    if (!window_create(settings->window_name,
                       settings->window_pos_x, settings->window_pos_y, settings->window_width,
                       settings->window_height, &engine_ptr->window))
    {
      EGNERROR("engine_create_instance: failed to initialize window state");
      engine_cleanup(instance);
      return false;
    }
    instance->window_ready = true;

    window_show(&engine_ptr->window);

    if (!renderer_initialize(&engine_ptr->window, settings->window_width, settings->window_height, settings->vsync))
    {
      EGNERROR("engine_create_instance: failed to initialize renderer state");
      engine_cleanup(instance);
      return false;
    }
    instance->renderer_ready = true;
  }

  if (!egn_scene_initialize())
  {
    EGNERROR("engine_create_instance: failed to initialize scene state");
    engine_cleanup(instance);
    return false;
  }
  instance->scene_ready = true;

  if (!egn_physics_initialize())
  {
    EGNERROR("engine_create_instance: failed to initialize physics state");
    engine_cleanup(instance);
    return false;
  }
  instance->physics_ready = true;

  events_register(EVENT_CODE_APPLICATION_QUIT, 0, &engine_on_event);
  events_register(EVENT_CODE_KEY_PRESSED, 0, &engine_on_key);
  events_register(EVENT_CODE_KEY_RELEASED, 0, &engine_on_key);
  events_register(EVENT_CODE_RESIZED, 0, &engine_on_resize);

  engine_ptr->running = true;
  clock_update(&engine_ptr->clock);

  engine_ptr->memory_usage_str = linear_allocator_allocate(&engine_ptr->linear_allocator, 2048);
  memo_write_memory_usage_str(engine_ptr->memory_usage_str, 2048);

  EGNINFO("Game Engine has been initialized in %f seconds!", engine_ptr->clock.elapsed);
  EGNDEBUG("%s", engine_ptr->memory_usage_str);

  // Restart the clock so delta time starts counting from the first frame.
  clock_start(&engine_ptr->clock);
  engine_ptr->last_time = 0;
  engine_ptr->delta_time = 0;
  engine_ptr->frames_count = 0;
  engine_ptr->time_count = 0;

  return true;
}

EGNAPI b8 engine_frame(void *instance_ptr)
{
  if (!engine_ptr || instance_ptr != engine_ptr || !engine_ptr->running)
    return false;

  f64 frame_start = platform_get_absolute_time();

  clock_update(&engine_ptr->clock);

  // Calculate delta time
  f64 current_time = engine_ptr->clock.elapsed;
  f64 delta = current_time - engine_ptr->last_time;
  engine_ptr->last_time = current_time;
  engine_ptr->delta_time = delta;

  // Frames count
  engine_ptr->frames_count++;
  engine_ptr->time_count += delta;
  if (engine_ptr->time_count >= 1.0)
  {
    EGNTRACE("FPS: %i", engine_ptr->frames_count);
    engine_ptr->frames_count = 0;
    engine_ptr->time_count -= 1.0;
  }

  inputs_update(delta);
  if (engine_ptr->window_ready)
    window_process_messages(&engine_ptr->window);

  egn_scene_update(delta);
  egn_physics_update(delta);

  if (engine_ptr->renderer_ready)
  {
    RenderPacket packet = {delta};
    renderer_draw_frame(packet);
  }

  if (engine_ptr->target_fps > 0)
  {
    f64 target = 1.0 / (f64)engine_ptr->target_fps;
    f64 elapsed = platform_get_absolute_time() - frame_start;
    if (elapsed < target)
      platform_sleep_us((u64)((target - elapsed) * 1000000.0));
  }

  return engine_ptr->running;
}

EGNAPI b8 engine_should_run(void *instance_ptr)
{
  if (!engine_ptr || instance_ptr != engine_ptr)
    return false;

  return engine_ptr->running;
}

EGNAPI void engine_request_quit(void *instance_ptr)
{
  if (!engine_ptr || (instance_ptr && instance_ptr != engine_ptr))
    return;

  EventContext ctx = {0};
  events_fire(EVENT_CODE_APPLICATION_QUIT, 0, ctx);

  // Guarantee the loop stops even if no listener handles the event.
  engine_ptr->running = false;
}

EGNAPI f64 engine_get_delta_time(void *instance_ptr)
{
  if (!engine_ptr || instance_ptr != engine_ptr)
    return 0;

  return engine_ptr->delta_time;
}

EGNAPI f64 engine_get_elapsed_time(void *instance_ptr)
{
  if (!engine_ptr || instance_ptr != engine_ptr)
    return 0;

  return engine_ptr->clock.elapsed;
}

EGNAPI b8 engine_get_window_size(void *instance_ptr, i32 *out_width, i32 *out_height)
{
  if (!engine_ptr || instance_ptr != engine_ptr || !engine_ptr->window_ready)
    return false;

  u32 width = 0;
  u32 height = 0;
  if (!window_get_size(&engine_ptr->window, &width, &height))
    return false;

  if (out_width)
    *out_width = (i32)width;
  if (out_height)
    *out_height = (i32)height;

  return true;
}

EGNAPI void engine_destroy_instance(void *instance_ptr)
{
  if (!engine_ptr)
    return;

  if (instance_ptr && instance_ptr != engine_ptr)
  {
    EGNERROR("engine_destroy_instance: invalid instance pointer");
    return;
  }

  engine_cleanup(engine_ptr);
}

b8 engine_on_event(u16 code, void *sender, EventContext data)
{
  switch (code)
  {
  case EVENT_CODE_APPLICATION_QUIT:
  {
    engine_ptr->running = false;
    return true;
  }
  }

  return false;
}

b8 engine_on_key(u16 code, void *sender, EventContext data)
{
  // The engine does not consume keys: games own the input policy. In
  // particular Esc releases the pointer lock in the demo and Q quits.
  (void)code;
  (void)sender;
  (void)data;
  return false;
}

b8 engine_on_resize(u16 code, void *sender, EventContext data)
{
  EGNDEBUG("Event EVENT_CODE_RESIZED received!")

  if (!engine_ptr || !engine_ptr->renderer_ready)
    return false;

  renderer_resize((u32)data.data.u16[0], (u32)data.data.u16[1]);

  return false;
}

EGNAPI void engine_set_vsync(void *instance_ptr, b8 vsync)
{
  if (!engine_ptr || (instance_ptr && instance_ptr != engine_ptr))
    return;

  engine_ptr->vsync = vsync;
  if (engine_ptr->renderer_ready)
    renderer_set_vsync(vsync);
}

EGNAPI b8 engine_get_vsync(void *instance_ptr)
{
  if (!engine_ptr || instance_ptr != engine_ptr)
    return false;
  return engine_ptr->vsync;
}

EGNAPI void engine_set_target_fps(void *instance_ptr, i32 target_fps)
{
  if (!engine_ptr || (instance_ptr && instance_ptr != engine_ptr))
    return;
  engine_ptr->target_fps = target_fps < 0 ? 0 : target_fps;
}

EGNAPI i32 engine_get_target_fps(void *instance_ptr)
{
  if (!engine_ptr || instance_ptr != engine_ptr)
    return 0;
  return engine_ptr->target_fps;
}

EGNAPI void engine_set_pointer_lock(void *instance_ptr, b8 locked)
{
  if (!engine_ptr || (instance_ptr && instance_ptr != engine_ptr))
    return;
  if (!engine_ptr->window_ready)
    return;
  window_set_pointer_lock(&engine_ptr->window, locked);
}

EGNAPI b8 engine_is_pointer_locked(void *instance_ptr)
{
  if (!engine_ptr || instance_ptr != engine_ptr)
    return false;
  if (!engine_ptr->window_ready)
    return false;
  return window_is_pointer_locked(&engine_ptr->window);
}

EGNAPI b8 engine_is_headless(void *instance_ptr)
{
  if (!engine_ptr || instance_ptr != engine_ptr)
    return false;
  return engine_ptr->headless;
}
