#pragma once

#include "defines.h"
#include "math/math.h"
#include "scene/scene.h"

#define EGN_SCENE_NAME_MAX 64
#define EGN_SCENE_MAX_LAYERS 32

#define EGN_OBJECT_MAGIC 0x4F424A54u
#define EGN_COMPONENT_MAGIC 0x434F4D50u

typedef struct EgnCameraData
{
  i32 projection;

  f32 fov_degrees;
  f32 ortho_size;
  f32 near_clip;
  f32 far_clip;

  f32 viewport_x;
  f32 viewport_y;
  f32 viewport_width;
  f32 viewport_height;

  u32 layer_mask;

  f32 clear_r;
  f32 clear_g;
  f32 clear_b;
  f32 clear_a;
  b8 clear_color;
  b8 clear_depth;

  i32 order;
} EgnCameraData;

typedef struct EgnModelData
{
  u32 mesh;
  u32 texture;
  f32 r;
  f32 g;
  f32 b;
  f32 a;
} EgnModelData;

typedef struct EgnSpriteData
{
  u32 texture;
  f32 width;
  f32 height;
  f32 pivot_x;
  f32 pivot_y;
  f32 r;
  f32 g;
  f32 b;
  f32 a;
} EgnSpriteData;

typedef struct EgnComponent
{
  u32 magic;
  i32 type;
  struct EgnObject *owner;
  b8 enabled;
  b8 alive;
  union
  {
    EgnCameraData camera;
    EgnModelData model;
    EgnSpriteData sprite;
  } data;
  struct EgnComponent *next;
} EgnComponent;

typedef struct EgnObject
{
  u32 magic;
  u32 id;
  char name[EGN_SCENE_NAME_MAX];

  struct EgnObject *parent;
  struct EgnObject *first_child;
  struct EgnObject *next_sibling;
  struct EgnObject *prev_sibling;

  vec3 position;
  quat rotation;
  vec3 scale;

  u32 layer;
  b8 active;
  b8 alive;

  b8 world_dirty;
  mat4 world_matrix;

  EgnComponent *first_component;
} EgnObject;

typedef struct EgnScene
{
  EgnObject *root;

  EgnObject **objects;
  u32 object_count;
  u32 object_capacity;

  EgnComponent **components;
  u32 component_count;
  u32 component_capacity;

  u32 next_object_id;
} EgnScene;

EgnScene *egn_scene_get(void);

// Handle validation. Returns NULL for NULL/destroyed/invalid handles.
EgnObject *egn_object_from_handle(void *handle);
EgnComponent *egn_component_from_handle(void *handle);

// Transform helpers used by the renderer.
mat4 egn_object_local_matrix(const EgnObject *object);
void egn_object_get_world_matrix_internal(EgnObject *object, mat4 *out_matrix);
quat egn_object_get_world_rotation(EgnObject *object);
vec3 egn_object_get_world_position(EgnObject *object);
void egn_object_mark_world_dirty(EgnObject *object);
