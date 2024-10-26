#include "engine.h"
#include "logger.h"
#include "memo.h"
#include "events.h"
#include "inputs.h"
#include "clock.h"
#include "platform/platform.h"
#include "platform/window.h"
#include "renderer/renderer.h"

#include <stdio.h>

typedef struct engine_state
{
  Window window;
  Clock clock;
  b8 running;
} engine_state;

static engine_state engine;

#define INIT(func, msg) \
  if (!func)            \
  {                     \
    EGNERROR(msg);      \
    return false;       \
  }

b8 application_on_event(u16 code, void *sender, EventContext data)
{
  switch (code)
  {
  case EVENT_CODE_APPLICATION_QUIT:
  {
    engine.running = false;
    return true;
  }
  }

  return true;
}

b8 application_on_key(u16 code, void *sender, EventContext data)
{
  u16 key = data.data.u16[0];
  if (EVENT_CODE_KEY_PRESSED)
  {
    if (key == KEY_ESCAPE)
    {
      EventContext ctx;
      event_fire(EVENT_CODE_APPLICATION_QUIT, 0, ctx);
    }
  }

  return true;
}

EGNAPI b8 engine_initialize(EngineSettings *settings)
{
  INIT(logger_initialize(), "failed to initialize logger!");

  INIT(platform_initialize(), "failed to initialize platform!");
  clock_start(&engine.clock);

  INIT(memo_initialize(), "failed to initialize rexmemory!");
  INIT(event_initialize(), "failed to initialize events!");
  INIT(input_initialize(), "failed to initialize inputs!");

  INIT(window_create(settings->window_name,
                     settings->window_pos_x, settings->window_pos_y, settings->window_width, settings->window_height, &engine.window),
       "failed to create window!");

  window_show(&engine.window);

  INIT(renderer_initialize(&engine.window), "failed to initialize renderer!")

  event_register(EVENT_CODE_APPLICATION_QUIT, 0, &application_on_event);
  event_register(EVENT_CODE_KEY_PRESSED, 0, &application_on_key);
  event_register(EVENT_CODE_KEY_RELEASED, 0, &application_on_key);

  engine.running = true;
  clock_update(&engine.clock);
  EGNINFO("Game Engine has been initialized in %f seconds!", engine.clock.elapsed);

  clock_start(&engine.clock);
  f64 last_time = 0;

  i32 frames_count = 0;
  f64 time_count = 0;

  while (engine.running)
  {
    clock_update(&engine.clock);

    // Calculate delta time
    f64 current_time = engine.clock.elapsed;
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

    window_process_messages(&engine.window);

    input_update(delta);

    // TODO: render
  }

  return true;
}

EGNAPI void engine_shutdown()
{
  window_hide(&engine.window);
  window_destroy(&engine.window);

  platform_shutdown();
  input_shutdown();
  event_shutdown();
  memo_shutdown();
  logger_shutdown();

  EGNINFO("Press any key to QUIT...\n");
  getchar();
}
