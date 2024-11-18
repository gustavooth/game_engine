#include "defines.h"
#include "renderer_types.h"
#include "platform/window.h"

b8 renderer_initialize(Window *win, u32 width, u32 height);
void renderer_shutdown();

void renderer_resize(u32 width, u32 height);

b8 renderer_draw_frame(RenderPacket packet);
