#include "defines.h"

#ifdef EGNPLATFORM_WEB
#include "core/logger.h"
#include "core/memo.h"
#include "physics/physics.h"
#include "platform/platform.h"
#include "renderer/renderer.h"
#include "scene/scene.h"

#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>

#define EGN_WEB_MAX_MESHES 256
#define EGN_WEB_MESH_CUSTOM_BASE 100

typedef struct WebMesh
{
  u32 handle;
  vec3 *positions;
  u32 position_count;
  u32 *indices;
  u32 index_count;
} WebMesh;

static b8 web_initialized;
static void *web_platform_state;
static void *web_logger_state;
static void *web_memo_state;
static WebMesh web_meshes[EGN_WEB_MAX_MESHES];

static void web_meshes_shutdown(void)
{
  for (u32 i = 0; i < EGN_WEB_MAX_MESHES; i++)
  {
    if (web_meshes[i].positions)
      MEMOFREE(web_meshes[i].positions);
    if (web_meshes[i].indices)
      MEMOFREE(web_meshes[i].indices);
    MEMOZERO(&web_meshes[i], sizeof(WebMesh));
  }
}

// The native renderer provides this through its mesh table. On the web the
// SDK registers collision geometry explicitly so Jolt mesh colliders keep
// working without any Vulkan renderer.
b8 renderer_mesh_get_collision(u32 mesh_handle, const vec3 **positions,
                               u32 *position_count, const u32 **indices,
                               u32 *index_count)
{
  if (mesh_handle == 0)
    return false;

  for (u32 i = 0; i < EGN_WEB_MAX_MESHES; i++)
  {
    WebMesh *mesh = &web_meshes[i];
    if (mesh->handle != mesh_handle || !mesh->positions)
      continue;

    if (positions)
      *positions = mesh->positions;
    if (position_count)
      *position_count = mesh->position_count;
    if (indices)
      *indices = mesh->indices;
    if (index_count)
      *index_count = mesh->index_count;
    return true;
  }
  return false;
}

EMSCRIPTEN_KEEPALIVE int egn_web_init(void)
{
  if (web_initialized)
    return 1;

  u64 required = 0;

  platform_initialize(&required, 0);
  web_platform_state = malloc(required > 0 ? required : 1);
  if (!platform_initialize(0, web_platform_state))
    return 0;

  logger_initialize(&required, 0);
  web_logger_state = malloc(required > 0 ? required : 1);
  if (!logger_initialize(0, web_logger_state))
    return 0;

  memo_initialize(&required, 0);
  web_memo_state = malloc(required > 0 ? required : 1);
  if (!memo_initialize(0, web_memo_state))
    return 0;

  if (!egn_scene_initialize())
  {
    EGNERROR("Web bridge failed to initialize the scene");
    return 0;
  }

  if (!egn_physics_initialize())
  {
    EGNERROR("Web bridge failed to initialize physics");
    return 0;
  }

  web_initialized = true;
  EGNINFO("EGN web bridge initialized");
  return 1;
}

EMSCRIPTEN_KEEPALIVE void egn_web_update(double delta_time)
{
  if (!web_initialized)
    return;

  egn_scene_update(delta_time);
  egn_physics_update(delta_time);
}

EMSCRIPTEN_KEEPALIVE void egn_web_shutdown(void)
{
  if (!web_initialized)
    return;

  egn_physics_shutdown();
  egn_scene_shutdown();
  web_meshes_shutdown();
  memo_shutdown();
  logger_shutdown();
  platform_shutdown();

  free(web_memo_state);
  free(web_logger_state);
  free(web_platform_state);
  web_memo_state = 0;
  web_logger_state = 0;
  web_platform_state = 0;
  web_initialized = false;
}

EMSCRIPTEN_KEEPALIVE u32 egn_web_mesh_cube(void)
{
  return 1;
}

EMSCRIPTEN_KEEPALIVE u32 egn_web_mesh_sphere(void)
{
  return 2;
}

EMSCRIPTEN_KEEPALIVE u32 egn_web_mesh_quad(void)
{
  return 3;
}

EMSCRIPTEN_KEEPALIVE u32 egn_web_mesh_custom_base(void)
{
  return EGN_WEB_MESH_CUSTOM_BASE;
}

EMSCRIPTEN_KEEPALIVE int egn_web_register_mesh(u32 handle, const f32 *positions,
                                                u32 position_count,
                                                const u32 *indices,
                                                u32 index_count)
{
  if (!web_initialized || handle < EGN_WEB_MESH_CUSTOM_BASE ||
      !positions || position_count == 0 || !indices || index_count < 3)
    return 0;

  WebMesh *slot = 0;
  for (u32 i = 0; i < EGN_WEB_MAX_MESHES; i++)
  {
    if (web_meshes[i].handle == handle)
    {
      slot = &web_meshes[i];
      break;
    }
    if (!slot && web_meshes[i].handle == 0)
      slot = &web_meshes[i];
  }

  if (!slot)
  {
    EGNERROR("Web mesh registry is full");
    return 0;
  }

  if (slot->positions)
    MEMOFREE(slot->positions);
  if (slot->indices)
    MEMOFREE(slot->indices);

  slot->positions = MEMOALLOC(sizeof(vec3) * position_count, MEMO_ASSET);
  slot->indices = MEMOALLOC(sizeof(u32) * index_count, MEMO_ASSET);
  if (!slot->positions || !slot->indices)
  {
    if (slot->positions) MEMOFREE(slot->positions);
    if (slot->indices) MEMOFREE(slot->indices);
    MEMOZERO(slot, sizeof(WebMesh));
    return 0;
  }

  MEMOCPY(slot->positions, positions, sizeof(vec3) * position_count);
  MEMOCPY(slot->indices, indices, sizeof(u32) * index_count);
  slot->handle = handle;
  slot->position_count = position_count;
  slot->index_count = index_count;
  return 1;
}

EMSCRIPTEN_KEEPALIVE void egn_web_unregister_mesh(u32 handle)
{
  for (u32 i = 0; i < EGN_WEB_MAX_MESHES; i++)
  {
    if (web_meshes[i].handle != handle)
      continue;
    if (web_meshes[i].positions)
      MEMOFREE(web_meshes[i].positions);
    if (web_meshes[i].indices)
      MEMOFREE(web_meshes[i].indices);
    MEMOZERO(&web_meshes[i], sizeof(WebMesh));
  }
}

#endif
