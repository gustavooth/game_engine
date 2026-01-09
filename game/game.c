#include <engine.h>

int main(int cnt, char **args)
{
  EngineSettings settings = {
    .window_name = "Minha engine",
    .window_width = 940,
    .window_height = 1000,
    .window_pos_x = 100,
    .window_pos_y = 100,
  };

  void* engine_instance;
  
  engine_create_instance(&settings, &engine_instance);
  
  return 0;
}
