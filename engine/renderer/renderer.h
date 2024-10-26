#include "defines.h"
#include "renderer_types.h"
#include "platform/window.h"

b8 renderer_initialize(Window *win);
void renderer_shutdown();

b8 renderer_draw_frame(RenderPacket packet);
