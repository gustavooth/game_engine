#include "engine.h"
#include "logger.h"
#include "memo.h"
#include "events.h"
#include "inputs.h"
#include "clock.h"
#include "platform/platform.h"
#include "platform/window.h"
#include "renderer/renderer.h"

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

  char *memory_usage_str;
} EngineState;

static EngineState *engine_ptr;

b8 engine_on_event(u16 code, void *sender, EventContext data);
b8 engine_on_key(u16 code, void *sender, EventContext data);
b8 engine_on_resize(u16 code, void *sender, EventContext data);

EGNAPI b8 engine_create_instance(EngineSettings *settings, void **out_instance_ptr)
{
  if (engine_ptr)
  {
    EGNERROR("engine_create_instance: only one instance can be created");
    return false;
  }

  if (!out_instance_ptr)
  {
    EGNERROR("engine_create_instance: out_instance_ptr must be a valid pointer.")
    return false;
  }

  EngineState *instance = MEMOALLOC(sizeof(EngineState), MEMO_NOT_INITIALIZED);
  *out_instance_ptr = instance;
  engine_ptr = instance;

  linear_allocator_create(64 * 1024 * 1024, &instance->linear_allocator); // 64mb

  u64 required_memory;

  platform_initialize(&required_memory, 0);
  instance->platform_state = LIALLOC(&instance->linear_allocator, required_memory);
  if (!platform_initialize(0, instance->platform_state))
  {
    EGNERROR("engine_create_instance: failed to initialize platform state");
    return false;
  }
  clock_start(&engine_ptr->clock);

  logger_initialize(&required_memory, 0);
  instance->logger_state = LIALLOC(&instance->linear_allocator, required_memory);
  if (!logger_initialize(0, instance->logger_state))
  {
    EGNERROR("engine_create_instance: failed to initialize logger state");
    return false;
  }

  memo_initialize(&required_memory, 0);
  instance->memo_state = LIALLOC(&instance->linear_allocator, required_memory);
  if (!memo_initialize(0, instance->memo_state))
  {
    EGNERROR("engine_create_instance: failed to initialize memo state");
    return false;
  }

  memo_set_memory_usage_tag(MEMO_LINEAR_ALLOCATOR, instance->linear_allocator.total_size + memo_get_memory_usage_tag(MEMO_LINEAR_ALLOCATOR));
  memo_set_memory_usage_tag(MEMO_ENGINE_INSTANCE, sizeof(EngineState) + memo_get_memory_usage_tag(MEMO_ENGINE_INSTANCE));
  memo_set_allocations_count(2 + memo_get_allocations_count());

  events_initialize(&required_memory, 0);
  instance->events_state = LIALLOC(&instance->linear_allocator, required_memory);
  if (!events_initialize(0, instance->events_state))
  {
    EGNERROR("engine_create_instance: failed to initialize events state");
    return false;
  }

  inputs_initialize(&required_memory, 0);
  instance->inputs_state = LIALLOC(&instance->linear_allocator, required_memory);
  if (!inputs_initialize(0, instance->inputs_state))
  {
    EGNERROR("engine_create_instance: failed to initialize inputs state");
    return false;
  }

  if (!window_create(settings->window_name,
                     settings->window_pos_x, settings->window_pos_y, settings->window_width,
                     settings->window_height, &engine_ptr->window))
  {
    EGNERROR("engine_create_instance: failed to initialize window state");
    return false;
  }

  window_show(&engine_ptr->window);

  if (!renderer_initialize(&engine_ptr->window, settings->window_width, settings->window_height))
  {
    EGNERROR("engine_create_instance: failed to initialize renderer state");
    return false;
  }

  events_register(EVENT_CODE_APPLICATION_QUIT, 0, &engine_on_event);
  events_register(EVENT_CODE_KEY_PRESSED, 0, &engine_on_key);
  events_register(EVENT_CODE_KEY_RELEASED, 0, &engine_on_key);
  events_register(EVENT_CODE_RESIZED, 0, &engine_on_resize);

  engine_ptr->running = true;
  clock_update(&engine_ptr->clock);

  engine_ptr->memory_usage_str = linear_allocator_allocate(&engine_ptr->linear_allocator, 2048);
  memo_write_memory_usage_str(engine_ptr->memory_usage_str, 2048);

  EGNINFO("Game Engine has been initialized in %f seconds!", engine_ptr->clock.elapsed);
  EGNDEBUG(engine_ptr->memory_usage_str);

  clock_start(&engine_ptr->clock);
  f64 last_time = 0;

  i32 frames_count = 0;
  f64 time_count = 0;

  while (engine_ptr->running)
  {
    clock_update(&engine_ptr->clock);

    // Calculate delta time
    f64 current_time = engine_ptr->clock.elapsed;
    f64 delta = current_time - last_time;
    last_time = current_time;

    // Frames count
    frames_count++;
    time_count += delta;
    if (time_count >= 1.f)
    {
      EGNTRACE("FPS: %i", frames_count);
      frames_count = 0;
      time_count -= 1.f;
    }

    window_process_messages(&engine_ptr->window);

    inputs_update(delta);

    RenderPacket packet = {delta};
    renderer_draw_frame(packet);
  }

  return true;
}

EGNAPI void engine_shutdown()
{
  window_hide(&engine_ptr->window);
  window_destroy(&engine_ptr->window);

  platform_shutdown();
  inputs_shutdown();
  events_shutdown();
  memo_shutdown();
  logger_shutdown();

  EGNINFO("Press any key to QUIT...\n");
  getchar();
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
  u16 key = data.data.u16[0];
  if (EVENT_CODE_KEY_PRESSED)
  {
    if (key == KEY_ESCAPE)
    {
      EventContext ctx = {0};
      events_fire(EVENT_CODE_APPLICATION_QUIT, 0, ctx);
    }
  }

  return false;
}

b8 engine_on_resize(u16 code, void *sender, EventContext data)
{
  EGNDEBUG("Event EVENT_CODE_RESIZED received!")

  renderer_resize((u32)data.data.u16[0], (u32)data.data.u16[1]);

  return false;
}
