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

EGNAPI b8 engine_create_instance(EngineSettings *settings, void **out_instance_ptr);
EGNAPI void engine_destroy_instance(void *instance_ptr);
