#include <engine.h>

int main(int cnt, char **args)
{
  EngineSettings settings = {
    .window_name = "Minha engine",
    .window_width = 1280,
    .window_height = 720,
    .window_pos_x = 100,
    .window_pos_y = 100,
  };

  engine_initialize(&settings);
  
  return 0;
}
