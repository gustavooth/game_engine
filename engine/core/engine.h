#pragma once

#include "defines.h"

typedef struct EngineSettings
{
  const char *window_name;
  i32 window_width;
  i32 window_height;
  i32 window_pos_x;
  i32 window_pos_y;
} EngineSettings;

/**
 * @brief Creates the engine instance and initializes every subsystem
 * (platform, logger, memory, events, inputs, window and renderer).
 *
 * This function does not enter the game loop. The game owns the loop and
 * must call engine_frame() while engine_should_run() returns true.
 *
 * @param settings Engine/window configuration.
 * @param out_instance_ptr Receives the engine instance pointer.
 * @return true on success, false otherwise.
 */
EGNAPI b8 engine_create_instance(EngineSettings *settings, void **out_instance_ptr);

/**
 * @brief Runs one frame: processes OS/window messages, updates inputs and
 * renders the current frame.
 *
 * @param instance_ptr Engine instance returned by engine_create_instance().
 * @return true while the engine should keep running, false when it must stop.
 */
EGNAPI b8 engine_frame(void *instance_ptr);

/**
 * @brief Tells whether the engine is still running.
 *
 * @param instance_ptr Engine instance returned by engine_create_instance().
 * @return true if engine_frame() should be called again.
 */
EGNAPI b8 engine_should_run(void *instance_ptr);

/**
 * @brief Requests the engine/game to stop at the end of the current frame.
 * Fires EVENT_CODE_APPLICATION_QUIT so listeners can react.
 *
 * @param instance_ptr Engine instance returned by engine_create_instance().
 */
EGNAPI void engine_request_quit(void *instance_ptr);

/**
 * @brief Time elapsed between the two most recent frames, in seconds.
 */
EGNAPI f64 engine_get_delta_time(void *instance_ptr);

/**
 * @brief Time elapsed since the engine was created, in seconds.
 */
EGNAPI f64 engine_get_elapsed_time(void *instance_ptr);

/**
 * @brief Current window size in pixels.
 *
 * @param out_width Receives the width. Can be NULL.
 * @param out_height Receives the height. Can be NULL.
 * @return true on success, false if the window is not available.
 */
EGNAPI b8 engine_get_window_size(void *instance_ptr, i32 *out_width, i32 *out_height);

/**
 * @brief Shuts down every subsystem and releases the engine instance.
 * Safe to call more than once.
 *
 * @param instance_ptr Engine instance returned by engine_create_instance().
 */
EGNAPI void engine_destroy_instance(void *instance_ptr);
