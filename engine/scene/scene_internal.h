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
  f32 metallic;
  f32 roughness;
  b8 cast_shadows;

  // Renderer-owned per-instance animation state and current bone slot.
  void *anim_state;
  u32 bone_offset;
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

typedef struct EgnAnimationData
{
  i32 clip;
  f32 time;
  f32 speed;
  b8 loop;
  b8 playing;
} EgnAnimationData;

typedef struct EgnLightData
{
  i32 light_type;
  f32 r;
  f32 g;
  f32 b;
  f32 intensity;
  f32 range;
  b8 cast_shadows;
} EgnLightData;

typedef struct EgnRigidbodyData
{
  i32 motion_type;
  b8 is_2d;
  f32 gravity_factor;
  f32 mass;
  f32 friction;
  f32 restitution;
  f32 linear_damping;
  f32 angular_damping;
} EgnRigidbodyData;

typedef struct EgnCharacterData
{
  f32 radius, height, gravity;
  f32 velocity_x, velocity_z, velocity_y, jump_speed, plane_z;
  b8 grounded;
  void *character;
} EgnCharacterData;

typedef struct EgnParticleData
{
  u32 max_particles;
  u32 texture;
  f32 spawn_rate;
  f32 lifetime;
  f32 speed;
  f32 speed_variation;
  f32 gravity;
  f32 drag;
  f32 spread;
  f32 radius;
  vec3 direction;
  f32 size_start;
  f32 size_end;
  f32 size_scale;
  f32 fade_in;
  f32 fade_out;
  vec4 color_start;
  vec4 color_end;
  u32 system;
  f32 burst;
} EgnParticleData;

typedef struct EgnColliderData
{
  i32 shape_type;
  f32 size_x;
  f32 size_y;
  f32 size_z;
  f32 radius;
  u32 mesh;
  f32 offset_x;
  f32 offset_y;
  f32 offset_z;

  // Physics-owned body state.
  u32 body_id;
  b8 body_created;
  b8 dirty;
} EgnColliderData;

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
    EgnAnimationData animation;
    EgnLightData light;
    EgnRigidbodyData rigidbody;
    EgnColliderData collider;
    EgnParticleData particles;
    EgnCharacterData character;
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
  i32 render_domain;
  i32 canvas_anchor;
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
