#include "defines.h"

typedef struct EngineSettings
{
  const char *window_name;
  i32 window_width;
  i32 window_height;
  i32 window_pos_x;
  i32 window_pos_y;
} EngineSettings;

EGNAPI b8 engine_initialize(EngineSettings *settings);
EGNAPI void engine_shutdown();
