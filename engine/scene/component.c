#include "scene_internal.h"
#include "physics/physics.h"
#include "core/logger.h"
#include "core/memo.h"

#include <string.h>
#include <math.h>

static b8 ensure_component_capacity(void)
{
  EgnScene *scene = egn_scene_get();

  if (scene->component_count < scene->component_capacity)
    return true;

  u32 new_capacity = scene->component_capacity ? scene->component_capacity * 2 : 128;
  EgnComponent **components = MEMOALLOC(sizeof(EgnComponent *) * new_capacity, MEMO_SCENE);
  if (!components)
    return false;

  if (scene->components)
  {
    MEMOCPY(components, scene->components, sizeof(EgnComponent *) * scene->component_count);
    MEMOFREE(scene->components);
  }

  scene->components = components;
  scene->component_capacity = new_capacity;
  return true;
}

EgnComponent *egn_component_from_handle(void *handle)
{
  if (!handle)
    return 0;

  EgnComponent *component = (EgnComponent *)handle;
  if (component->magic != EGN_COMPONENT_MAGIC || !component->alive)
    return 0;

  return component;
}

static EgnComponent *allocate_component(EgnObject *owner, i32 type)
{
  EgnScene *scene = egn_scene_get();

  if (!owner || !ensure_component_capacity())
    return 0;

  EgnComponent *component = MEMOZALLOC(sizeof(EgnComponent), MEMO_SCENE);
  if (!component)
    return 0;

  component->magic = EGN_COMPONENT_MAGIC;
  component->type = type;
  component->owner = owner;
  component->enabled = true;
  component->alive = true;

  component->next = owner->first_component;
  owner->first_component = component;

  scene->components[scene->component_count++] = component;
  return component;
}

static EgnCameraData *camera_data(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (!component || (component->type != EGN_COMPONENT_TYPE_CAMERA &&
                     component->type != EGN_COMPONENT_TYPE_CAMERA_2D &&
                     component->type != EGN_COMPONENT_TYPE_CANVAS))
    return 0;
  return &component->data.camera;
}

static EgnModelData *model_data(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (!component || component->type != EGN_COMPONENT_TYPE_MODEL)
    return 0;
  return &component->data.model;
}

static EgnSpriteData *sprite_data(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (!component || component->type != EGN_COMPONENT_TYPE_SPRITE)
    return 0;
  return &component->data.sprite;
}

static EgnAnimationData *animation_data(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (!component || component->type != EGN_COMPONENT_TYPE_ANIMATION)
    return 0;
  return &component->data.animation;
}

static EgnLightData *light_data(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (!component || component->type != EGN_COMPONENT_TYPE_LIGHT)
    return 0;
  return &component->data.light;
}

static EgnRigidbodyData *rigidbody_data(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (!component || component->type != EGN_COMPONENT_TYPE_RIGIDBODY)
    return 0;
  return &component->data.rigidbody;
}

static EgnParticleData *particles_data(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (!component || component->type != EGN_COMPONENT_TYPE_PARTICLES)
    return 0;
  return &component->data.particles;
}

static void mark_owner_collider_dirty(EgnComponent *component)
{
  if (!component || !component->owner)
    return;

  for (EgnComponent *other = component->owner->first_component; other; other = other->next)
  {
    if (other->alive && other->type == EGN_COMPONENT_TYPE_COLLIDER)
      other->data.collider.dirty = true;
  }
}

static EgnColliderData *collider_data(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (!component || component->type != EGN_COMPONENT_TYPE_COLLIDER)
    return 0;
  return &component->data.collider;
}

void *egn_object_add_camera(void *object_handle, i32 projection,
                            f32 fov, f32 ortho_size,
                            f32 near_clip, f32 far_clip,
                            f32 viewport_x, f32 viewport_y,
                            f32 viewport_width, f32 viewport_height,
                            u32 layer_mask,
                            f32 clear_r, f32 clear_g, f32 clear_b, f32 clear_a,
                            b8 clear_color, b8 clear_depth,
                            i32 order, b8 enabled)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  EgnComponent *component = allocate_component(object, EGN_COMPONENT_TYPE_CAMERA);
  if (!component)
    return 0;

  EgnCameraData *camera = &component->data.camera;
  camera->projection = projection == EGN_PROJECTION_ORTHOGRAPHIC
                           ? EGN_PROJECTION_ORTHOGRAPHIC
                           : EGN_PROJECTION_PERSPECTIVE;
  camera->fov_degrees = fov > 0.0f ? fov : 60.0f;
  camera->ortho_size = ortho_size > 0.0f ? ortho_size : 10.0f;
  camera->near_clip = near_clip > 0.0f ? near_clip : 0.1f;
  camera->far_clip = far_clip > camera->near_clip ? far_clip : 1000.0f;

  camera->viewport_x = EGNCLAMP(viewport_x, 0.0f, 1.0f);
  camera->viewport_y = EGNCLAMP(viewport_y, 0.0f, 1.0f);
  camera->viewport_width = EGNCLAMP(viewport_width, 0.0f, 1.0f - camera->viewport_x);
  camera->viewport_height = EGNCLAMP(viewport_height, 0.0f, 1.0f - camera->viewport_y);
  if (camera->viewport_width <= 0.0f)
    camera->viewport_width = 1.0f;
  if (camera->viewport_height <= 0.0f)
    camera->viewport_height = 1.0f;

  camera->layer_mask = layer_mask;
  camera->clear_r = clear_r;
  camera->clear_g = clear_g;
  camera->clear_b = clear_b;
  camera->clear_a = clear_a;
  camera->clear_color = clear_color;
  camera->clear_depth = clear_depth;
  camera->order = order;

  component->enabled = enabled;
  return component;
}

void *egn_object_add_camera_2d(void *object, f32 ortho_size, u32 layer_mask, i32 order, b8 enabled)
{
  EgnComponent *component = egn_object_add_camera(object, EGN_PROJECTION_ORTHOGRAPHIC,
      60, ortho_size, 0.1f, 1000, 0, 0, 1, 1, layer_mask,
      0, 0, 0, 0, false, true, order, enabled);
  if (component) component->type = EGN_COMPONENT_TYPE_CAMERA_2D;
  return component;
}
void *egn_object_add_canvas(void *object, u32 layer_mask, i32 order, b8 enabled)
{
  EgnComponent *component = egn_object_add_camera_2d(object, 10, layer_mask, order, enabled);
  if (component) component->type = EGN_COMPONENT_TYPE_CANVAS;
  return component;
}

void *egn_object_add_model(void *object_handle, u32 mesh, u32 texture,
                           f32 r, f32 g, f32 b, f32 a,
                           b8 cast_shadows, b8 enabled)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  EgnComponent *component = allocate_component(object, EGN_COMPONENT_TYPE_MODEL);
  if (!component)
    return 0;

  EgnModelData *model = &component->data.model;
  model->mesh = mesh;
  model->texture = texture;
  model->r = r;
  model->g = g;
  model->b = b;
  model->a = a;
  model->metallic = 0.0f;
  model->roughness = 1.0f;
  model->cast_shadows = cast_shadows;
  model->water = false;

  component->enabled = enabled;
  return component;
}

void *egn_object_add_rigidbody(void *object_handle, i32 motion_type, b8 is_2d,
                               f32 gravity_factor, f32 friction,
                               f32 restitution, f32 linear_damping,
                               f32 angular_damping, b8 enabled)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  EgnComponent *component = allocate_component(object, EGN_COMPONENT_TYPE_RIGIDBODY);
  if (!component)
    return 0;

  EgnRigidbodyData *rigidbody = &component->data.rigidbody;
  rigidbody->motion_type = (motion_type == EGN_PHYSICS_MOTION_STATIC ||
                            motion_type == EGN_PHYSICS_MOTION_KINEMATIC)
                               ? motion_type
                               : EGN_PHYSICS_MOTION_DYNAMIC;
  rigidbody->is_2d = is_2d;
  rigidbody->gravity_factor = gravity_factor;
  rigidbody->mass = 1.0f;
  rigidbody->friction = friction > 0.0f ? friction : 0.5f;
  rigidbody->restitution = restitution;
  rigidbody->linear_damping = linear_damping;
  rigidbody->angular_damping = angular_damping;

  component->enabled = enabled;
  return component;
}

void *egn_object_add_collider(void *object_handle, i32 shape_type,
                              f32 size_x, f32 size_y, f32 size_z,
                              f32 radius, u32 mesh,
                              f32 offset_x, f32 offset_y, f32 offset_z,
                              b8 enabled)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  EgnComponent *component = allocate_component(object, EGN_COMPONENT_TYPE_COLLIDER);
  if (!component)
    return 0;

  EgnColliderData *collider = &component->data.collider;
  collider->shape_type = (shape_type == EGN_COLLIDER_SPHERE ||
                          shape_type == EGN_COLLIDER_MESH ||
                          shape_type == EGN_COLLIDER_CAPSULE)
                             ? shape_type
                             : EGN_COLLIDER_BOX;
  collider->size_x = size_x > 0.0f ? size_x : 1.0f;
  collider->size_y = size_y > 0.0f ? size_y : 1.0f;
  collider->size_z = size_z > 0.0f ? size_z : 1.0f;
  collider->radius = radius > 0.0f ? radius : 0.5f;
  collider->mesh = mesh;
  collider->offset_x = offset_x;
  collider->offset_y = offset_y;
  collider->offset_z = offset_z;
  collider->body_id = 0;
  collider->body_created = false;
  collider->dirty = true;

  component->enabled = enabled;
  return component;
}

void *egn_object_add_character(void *object_handle, b8 is_2d, f32 radius,
                                f32 height, f32 gravity, b8 enabled)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object || !isfinite(radius) || !isfinite(height) || !isfinite(gravity) ||
      radius <= 0 || height <= 2 * radius || gravity < 0) return 0;
  EgnComponent *component = allocate_component(object, is_2d ? EGN_COMPONENT_TYPE_CHARACTER_2D : EGN_COMPONENT_TYPE_CHARACTER_3D);
  if (!component) return 0;
  component->data.character.radius = radius;
  component->data.character.height = height;
  component->data.character.gravity = gravity;
  component->enabled = enabled;
  return component;
}
static EgnCharacterData *character_data(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (!component || (component->type != EGN_COMPONENT_TYPE_CHARACTER_2D &&
                     component->type != EGN_COMPONENT_TYPE_CHARACTER_3D)) return 0;
  return &component->data.character;
}
void egn_character_set_velocity(void *handle, f32 x, f32 z)
{
  EgnCharacterData *c = character_data(handle);
  if (c && isfinite(x) && isfinite(z)) { c->velocity_x = x; c->velocity_z = z; }
}
void egn_character_jump(void *handle, f32 speed)
{
  EgnCharacterData *c = character_data(handle);
  if (c && c->grounded && isfinite(speed) && speed > 0) c->jump_speed = speed;
}
b8 egn_character_is_grounded(void *handle)
{
  EgnCharacterData *c = character_data(handle);
  return c && c->grounded;
}

void *egn_object_add_particles(void *object_handle, u32 max_particles, u32 texture,
                               f32 spawn_rate, f32 lifetime, f32 speed,
                               f32 speed_variation, f32 gravity, f32 drag,
                               f32 spread, f32 radius,
                               f32 dir_x, f32 dir_y, f32 dir_z,
                               f32 size_start, f32 size_end, f32 size_scale,
                               f32 fade_in, f32 fade_out,
                               f32 r0, f32 g0, f32 b0, f32 a0,
                               f32 r1, f32 g1, f32 b1, f32 a1,
                               b8 enabled)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  EgnComponent *component = allocate_component(object, EGN_COMPONENT_TYPE_PARTICLES);
  if (!component)
    return 0;

  EgnParticleData *particles = &component->data.particles;
  particles->max_particles = max_particles > 0 ? max_particles : 256;
  if (particles->max_particles > 65536)
    particles->max_particles = 65536;
  particles->texture = texture;
  particles->spawn_rate = spawn_rate;
  particles->lifetime = lifetime > 0.0f ? lifetime : 1.0f;
  particles->speed = speed;
  particles->speed_variation = speed_variation;
  particles->gravity = gravity;
  particles->drag = drag;
  particles->spread = spread;
  particles->radius = radius;
  particles->direction = vec3_create(dir_x, dir_y, dir_z);
  if (vec3_length(particles->direction) <= EGN_FLOAT_EPSILON)
    particles->direction = vec3_up();
  else
    particles->direction = vec3_normalized(particles->direction);
  particles->size_start = size_start;
  particles->size_end = size_end;
  particles->size_scale = size_scale > 0.0f ? size_scale : 1.0f;
  particles->fade_in = fade_in > 0.0f ? fade_in : 0.1f;
  particles->fade_out = fade_out > 0.0f ? fade_out : 0.1f;
  particles->color_start = vec4_create(r0, g0, b0, a0);
  particles->color_end = vec4_create(r1, g1, b1, a1);
  particles->system = 0;
  particles->burst = 0.0f;

  component->enabled = enabled;
  return component;
}

void *egn_object_add_sprite(void *object_handle, u32 texture,
                            f32 width, f32 height,
                            f32 pivot_x, f32 pivot_y,
                            f32 r, f32 g, f32 b, f32 a, b8 enabled)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  EgnComponent *component = allocate_component(object, EGN_COMPONENT_TYPE_SPRITE);
  if (!component)
    return 0;

  EgnSpriteData *sprite = &component->data.sprite;
  sprite->texture = texture;
  sprite->width = width > 0.0f ? width : 1.0f;
  sprite->height = height > 0.0f ? height : 1.0f;
  sprite->pivot_x = EGNCLAMP(pivot_x, 0.0f, 1.0f);
  sprite->pivot_y = EGNCLAMP(pivot_y, 0.0f, 1.0f);
  sprite->r = r;
  sprite->g = g;
  sprite->b = b;
  sprite->a = a;

  component->enabled = enabled;
  return component;
}

void *egn_object_add_animation(void *object_handle, i32 clip, f32 speed,
                               b8 loop, b8 playing, b8 enabled)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  EgnComponent *component = allocate_component(object, EGN_COMPONENT_TYPE_ANIMATION);
  if (!component)
    return 0;

  EgnAnimationData *animation = &component->data.animation;
  animation->clip = clip;
  animation->time = 0.0f;
  animation->speed = speed;
  animation->loop = loop;
  animation->playing = playing;

  component->enabled = enabled;
  return component;
}

void *egn_object_add_light(void *object_handle, i32 light_type,
                           f32 r, f32 g, f32 b, f32 intensity,
                           f32 range, b8 cast_shadows, b8 enabled)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  EgnComponent *component = allocate_component(object, EGN_COMPONENT_TYPE_LIGHT);
  if (!component)
    return 0;

  EgnLightData *light = &component->data.light;
  light->light_type = light_type == EGN_LIGHT_DIRECTIONAL ? EGN_LIGHT_DIRECTIONAL : EGN_LIGHT_POINT;
  light->r = r;
  light->g = g;
  light->b = b;
  light->intensity = intensity;
  light->range = range > 0.0f ? range : 10.0f;
  light->cast_shadows = cast_shadows;

  component->enabled = enabled;
  return component;
}

void *egn_object_get_component(void *object_handle, i32 type)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  EgnComponent *component = object->first_component;
  while (component)
  {
    if (component->alive && component->type == type)
      return component;
    component = component->next;
  }

  return 0;
}

void *egn_object_get_component_at(void *object_handle, i32 type, u32 index)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  u32 current = 0;
  EgnComponent *component = object->first_component;
  while (component)
  {
    if (component->alive && component->type == type)
    {
      if (current == index)
        return component;
      current++;
    }
    component = component->next;
  }

  return 0;
}

u32 egn_object_component_count(void *object_handle, i32 type)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object)
    return 0;

  u32 count = 0;
  EgnComponent *component = object->first_component;
  while (component)
  {
    if (component->alive && component->type == type)
      count++;
    component = component->next;
  }

  return count;
}

void egn_component_destroy(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (!component)
    return;

  EgnObject *owner = component->owner;
  if (owner)
  {
    EgnComponent *previous = 0;
    EgnComponent *current = owner->first_component;
    while (current)
    {
      if (current == component)
      {
        if (previous)
          previous->next = current->next;
        else
          owner->first_component = current->next;
        break;
      }
      previous = current;
      current = current->next;
    }
  }

  component->alive = false;
  component->enabled = false;
}

b8 egn_component_is_valid(void *component)
{
  return egn_component_from_handle(component) != 0;
}

i32 egn_component_get_type(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  return component ? component->type : -1;
}

void egn_component_set_enabled(void *handle, b8 enabled)
{
  EgnComponent *component = egn_component_from_handle(handle);
  if (component)
    component->enabled = enabled;
}

b8 egn_component_is_enabled(void *handle)
{
  EgnComponent *component = egn_component_from_handle(handle);
  return component ? component->enabled : false;
}

// Camera --------------------------------------------------------------------

i32 egn_camera_get_projection(void *camera)
{
  EgnCameraData *data = camera_data(camera);
  return data ? data->projection : EGN_PROJECTION_PERSPECTIVE;
}

void egn_camera_set_projection(void *camera, i32 projection)
{
  EgnCameraData *data = camera_data(camera);
  if (!data)
    return;
  data->projection = projection == EGN_PROJECTION_ORTHOGRAPHIC
                         ? EGN_PROJECTION_ORTHOGRAPHIC
                         : EGN_PROJECTION_PERSPECTIVE;
}

f32 egn_camera_get_fov(void *camera)
{
  EgnCameraData *data = camera_data(camera);
  return data ? data->fov_degrees : 60.0f;
}

void egn_camera_set_fov(void *camera, f32 fov)
{
  EgnCameraData *data = camera_data(camera);
  if (data && fov > 0.0f)
    data->fov_degrees = fov;
}

f32 egn_camera_get_ortho_size(void *camera)
{
  EgnCameraData *data = camera_data(camera);
  return data ? data->ortho_size : 10.0f;
}

void egn_camera_set_ortho_size(void *camera, f32 ortho_size)
{
  EgnCameraData *data = camera_data(camera);
  if (data && ortho_size > 0.0f)
    data->ortho_size = ortho_size;
}

f32 egn_camera_get_near(void *camera)
{
  EgnCameraData *data = camera_data(camera);
  return data ? data->near_clip : 0.1f;
}

f32 egn_camera_get_far(void *camera)
{
  EgnCameraData *data = camera_data(camera);
  return data ? data->far_clip : 1000.0f;
}

void egn_camera_set_clip_planes(void *camera, f32 near_clip, f32 far_clip)
{
  EgnCameraData *data = camera_data(camera);
  if (!data)
    return;

  f32 near_value = near_clip > 0.0f ? near_clip : 0.1f;
  f32 far_value = far_clip > near_value ? far_clip : 1000.0f;
  data->near_clip = near_value;
  data->far_clip = far_value;
}

void egn_camera_get_viewport(void *camera, f32 *x, f32 *y, f32 *w, f32 *h)
{
  EgnCameraData *data = camera_data(camera);
  f32 vx = data ? data->viewport_x : 0.0f;
  f32 vy = data ? data->viewport_y : 0.0f;
  f32 vw = data ? data->viewport_width : 1.0f;
  f32 vh = data ? data->viewport_height : 1.0f;

  if (x)
    *x = vx;
  if (y)
    *y = vy;
  if (w)
    *w = vw;
  if (h)
    *h = vh;
}

void egn_camera_set_viewport(void *camera, f32 x, f32 y, f32 w, f32 h)
{
  EgnCameraData *data = camera_data(camera);
  if (!data)
    return;

  data->viewport_x = EGNCLAMP(x, 0.0f, 1.0f);
  data->viewport_y = EGNCLAMP(y, 0.0f, 1.0f);
  data->viewport_width = EGNCLAMP(w, 0.0f, 1.0f - data->viewport_x);
  data->viewport_height = EGNCLAMP(h, 0.0f, 1.0f - data->viewport_y);
  if (data->viewport_width <= 0.0f)
    data->viewport_width = 1.0f;
  if (data->viewport_height <= 0.0f)
    data->viewport_height = 1.0f;
}

u32 egn_camera_get_layer_mask(void *camera)
{
  EgnCameraData *data = camera_data(camera);
  return data ? data->layer_mask : 0;
}

void egn_camera_set_layer_mask(void *camera, u32 layer_mask)
{
  EgnCameraData *data = camera_data(camera);
  if (data)
    data->layer_mask = layer_mask;
}

void egn_camera_get_clear_color(void *camera, f32 *r, f32 *g, f32 *b, f32 *a)
{
  EgnCameraData *data = camera_data(camera);
  if (r)
    *r = data ? data->clear_r : 0.0f;
  if (g)
    *g = data ? data->clear_g : 0.0f;
  if (b)
    *b = data ? data->clear_b : 0.1f;
  if (a)
    *a = data ? data->clear_a : 1.0f;
}

void egn_camera_set_clear_color(void *camera, f32 r, f32 g, f32 b, f32 a)
{
  EgnCameraData *data = camera_data(camera);
  if (!data)
    return;
  data->clear_r = r;
  data->clear_g = g;
  data->clear_b = b;
  data->clear_a = a;
}

b8 egn_camera_get_clear_color_enabled(void *camera)
{
  EgnCameraData *data = camera_data(camera);
  return data ? data->clear_color : false;
}

void egn_camera_set_clear_color_enabled(void *camera, b8 enabled)
{
  EgnCameraData *data = camera_data(camera);
  if (data)
    data->clear_color = enabled;
}

b8 egn_camera_get_clear_depth_enabled(void *camera)
{
  EgnCameraData *data = camera_data(camera);
  return data ? data->clear_depth : false;
}

void egn_camera_set_clear_depth_enabled(void *camera, b8 enabled)
{
  EgnCameraData *data = camera_data(camera);
  if (data)
    data->clear_depth = enabled;
}

i32 egn_camera_get_order(void *camera)
{
  EgnCameraData *data = camera_data(camera);
  return data ? data->order : 0;
}

void egn_camera_set_order(void *camera, i32 order)
{
  EgnCameraData *data = camera_data(camera);
  if (data)
    data->order = order;
}

// Model ---------------------------------------------------------------------

b8 egn_model_set_pbr(void *model, f32 metallic, f32 roughness)
{
  EgnModelData *data = model_data(model);
  /* This form rejects NaN as well as values outside the valid range. */
  if (!data || !(metallic >= 0.0f && metallic <= 1.0f) ||
      !(roughness >= 0.0f && roughness <= 1.0f))
    return false;
  data->metallic = metallic;
  data->roughness = roughness;
  return true;
}

void egn_model_get_pbr(void *model, f32 *metallic, f32 *roughness)
{
  EgnModelData *data = model_data(model);
  if (metallic) *metallic = data ? data->metallic : 0.0f;
  if (roughness) *roughness = data ? data->roughness : 1.0f;
}

b8 egn_model_set_water(void *model, b8 water)
{
  EgnModelData *data = model_data(model);
  if (!data)
    return false;
  data->water = !!water;
  return true;
}

b8 egn_model_get_water(void *model)
{
  EgnModelData *data = model_data(model);
  return data ? data->water : false;
}

u32 egn_model_get_mesh(void *model)
{
  EgnModelData *data = model_data(model);
  return data ? data->mesh : 0;
}

void egn_model_set_mesh(void *model, u32 mesh)
{
  EgnModelData *data = model_data(model);
  if (data)
    data->mesh = mesh;
}

u32 egn_model_get_texture(void *model)
{
  EgnModelData *data = model_data(model);
  return data ? data->texture : 0;
}

void egn_model_set_texture(void *model, u32 texture)
{
  EgnModelData *data = model_data(model);
  if (data)
    data->texture = texture;
}

void egn_model_get_color(void *model, f32 *r, f32 *g, f32 *b, f32 *a)
{
  EgnModelData *data = model_data(model);
  if (r)
    *r = data ? data->r : 1.0f;
  if (g)
    *g = data ? data->g : 1.0f;
  if (b)
    *b = data ? data->b : 1.0f;
  if (a)
    *a = data ? data->a : 1.0f;
}

void egn_model_set_color(void *model, f32 r, f32 g, f32 b, f32 a)
{
  EgnModelData *data = model_data(model);
  if (!data)
    return;
  data->r = r;
  data->g = g;
  data->b = b;
  data->a = a;
}

b8 egn_model_get_cast_shadows(void *model)
{
  EgnModelData *data = model_data(model);
  return data ? data->cast_shadows : false;
}

void egn_model_set_cast_shadows(void *model, b8 cast_shadows)
{
  EgnModelData *data = model_data(model);
  if (data)
    data->cast_shadows = cast_shadows;
}

// Sprite --------------------------------------------------------------------

u32 egn_sprite_get_texture(void *sprite)
{
  EgnSpriteData *data = sprite_data(sprite);
  return data ? data->texture : 0;
}

void egn_sprite_set_texture(void *sprite, u32 texture)
{
  EgnSpriteData *data = sprite_data(sprite);
  if (data)
    data->texture = texture;
}

void egn_sprite_get_size(void *sprite, f32 *width, f32 *height)
{
  EgnSpriteData *data = sprite_data(sprite);
  if (width)
    *width = data ? data->width : 1.0f;
  if (height)
    *height = data ? data->height : 1.0f;
}

void egn_sprite_set_size(void *sprite, f32 width, f32 height)
{
  EgnSpriteData *data = sprite_data(sprite);
  if (!data)
    return;
  if (width > 0.0f)
    data->width = width;
  if (height > 0.0f)
    data->height = height;
}

void egn_sprite_get_pivot(void *sprite, f32 *x, f32 *y)
{
  EgnSpriteData *data = sprite_data(sprite);
  if (x)
    *x = data ? data->pivot_x : 0.5f;
  if (y)
    *y = data ? data->pivot_y : 0.5f;
}

void egn_sprite_set_pivot(void *sprite, f32 x, f32 y)
{
  EgnSpriteData *data = sprite_data(sprite);
  if (!data)
    return;
  data->pivot_x = EGNCLAMP(x, 0.0f, 1.0f);
  data->pivot_y = EGNCLAMP(y, 0.0f, 1.0f);
}

void egn_sprite_get_color(void *sprite, f32 *r, f32 *g, f32 *b, f32 *a)
{
  EgnSpriteData *data = sprite_data(sprite);
  if (r)
    *r = data ? data->r : 1.0f;
  if (g)
    *g = data ? data->g : 1.0f;
  if (b)
    *b = data ? data->b : 1.0f;
  if (a)
    *a = data ? data->a : 1.0f;
}

void egn_sprite_set_color(void *sprite, f32 r, f32 g, f32 b, f32 a)
{
  EgnSpriteData *data = sprite_data(sprite);
  if (!data)
    return;
  data->r = r;
  data->g = g;
  data->b = b;
  data->a = a;
}

// Animation -----------------------------------------------------------------

i32 egn_animation_get_clip(void *animation)
{
  EgnAnimationData *data = animation_data(animation);
  return data ? data->clip : -1;
}

void egn_animation_set_clip(void *animation, i32 clip)
{
  EgnAnimationData *data = animation_data(animation);
  if (data)
    data->clip = clip;
}

f32 egn_animation_get_time(void *animation)
{
  EgnAnimationData *data = animation_data(animation);
  return data ? data->time : 0.0f;
}

void egn_animation_set_time(void *animation, f32 time)
{
  EgnAnimationData *data = animation_data(animation);
  if (data)
    data->time = time;
}

f32 egn_animation_get_speed(void *animation)
{
  EgnAnimationData *data = animation_data(animation);
  return data ? data->speed : 0.0f;
}

void egn_animation_set_speed(void *animation, f32 speed)
{
  EgnAnimationData *data = animation_data(animation);
  if (data)
    data->speed = speed;
}

b8 egn_animation_get_loop(void *animation)
{
  EgnAnimationData *data = animation_data(animation);
  return data ? data->loop : false;
}

void egn_animation_set_loop(void *animation, b8 loop)
{
  EgnAnimationData *data = animation_data(animation);
  if (data)
    data->loop = loop;
}

b8 egn_animation_get_playing(void *animation)
{
  EgnAnimationData *data = animation_data(animation);
  return data ? data->playing : false;
}

void egn_animation_set_playing(void *animation, b8 playing)
{
  EgnAnimationData *data = animation_data(animation);
  if (data)
    data->playing = playing;
}

// Light ---------------------------------------------------------------------

i32 egn_light_get_type(void *light)
{
  EgnLightData *data = light_data(light);
  return data ? data->light_type : EGN_LIGHT_POINT;
}

void egn_light_set_type(void *light, i32 light_type)
{
  EgnLightData *data = light_data(light);
  if (data)
    data->light_type = light_type == EGN_LIGHT_DIRECTIONAL ? EGN_LIGHT_DIRECTIONAL : EGN_LIGHT_POINT;
}

void egn_light_get_color(void *light, f32 *r, f32 *g, f32 *b)
{
  EgnLightData *data = light_data(light);
  if (r)
    *r = data ? data->r : 1.0f;
  if (g)
    *g = data ? data->g : 1.0f;
  if (b)
    *b = data ? data->b : 1.0f;
}

void egn_light_set_color(void *light, f32 r, f32 g, f32 b)
{
  EgnLightData *data = light_data(light);
  if (!data)
    return;
  data->r = r;
  data->g = g;
  data->b = b;
}

f32 egn_light_get_intensity(void *light)
{
  EgnLightData *data = light_data(light);
  return data ? data->intensity : 1.0f;
}

void egn_light_set_intensity(void *light, f32 intensity)
{
  EgnLightData *data = light_data(light);
  if (data)
    data->intensity = intensity;
}

f32 egn_light_get_range(void *light)
{
  EgnLightData *data = light_data(light);
  return data ? data->range : 10.0f;
}

void egn_light_set_range(void *light, f32 range)
{
  EgnLightData *data = light_data(light);
  if (data && range > 0.0f)
    data->range = range;
}

b8 egn_light_get_cast_shadows(void *light)
{
  EgnLightData *data = light_data(light);
  return data ? data->cast_shadows : false;
}

void egn_light_set_cast_shadows(void *light, b8 cast_shadows)
{
  EgnLightData *data = light_data(light);
  if (data)
    data->cast_shadows = cast_shadows;
}

// Rigidbody -----------------------------------------------------------------

i32 egn_rigidbody_get_motion_type(void *rigidbody)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  return data ? data->motion_type : EGN_PHYSICS_MOTION_STATIC;
}

void egn_rigidbody_set_motion_type(void *rigidbody, i32 motion_type)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  if (data)
  {
    data->motion_type = (motion_type == EGN_PHYSICS_MOTION_STATIC ||
                         motion_type == EGN_PHYSICS_MOTION_KINEMATIC)
                            ? motion_type
                            : EGN_PHYSICS_MOTION_DYNAMIC;
    mark_owner_collider_dirty(rigidbody_data(rigidbody) ? egn_component_from_handle(rigidbody) : 0);
  }
}

b8 egn_rigidbody_is_2d(void *rigidbody)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  return data ? data->is_2d : false;
}

void egn_rigidbody_set_2d(void *rigidbody, b8 is_2d)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  if (data)
  {
    data->is_2d = is_2d;
    mark_owner_collider_dirty(egn_component_from_handle(rigidbody));
  }
}

f32 egn_rigidbody_get_gravity_factor(void *rigidbody)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  return data ? data->gravity_factor : 1.0f;
}

void egn_rigidbody_set_gravity_factor(void *rigidbody, f32 factor)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  if (data)
  {
    data->gravity_factor = factor;
    mark_owner_collider_dirty(egn_component_from_handle(rigidbody));
  }
}

f32 egn_rigidbody_get_mass(void *rigidbody)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  return data ? data->mass : 1.0f;
}

void egn_rigidbody_set_mass(void *rigidbody, f32 mass)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  if (data)
  {
    data->mass = mass > 0.0f ? mass : 1.0f;
    mark_owner_collider_dirty(egn_component_from_handle(rigidbody));
  }
}

f32 egn_rigidbody_get_friction(void *rigidbody)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  return data ? data->friction : 0.5f;
}

void egn_rigidbody_set_friction(void *rigidbody, f32 friction)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  if (data)
  {
    data->friction = friction;
    mark_owner_collider_dirty(egn_component_from_handle(rigidbody));
  }
}

f32 egn_rigidbody_get_restitution(void *rigidbody)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  return data ? data->restitution : 0.0f;
}

void egn_rigidbody_set_restitution(void *rigidbody, f32 restitution)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  if (data)
  {
    data->restitution = restitution;
    mark_owner_collider_dirty(egn_component_from_handle(rigidbody));
  }
}

f32 egn_rigidbody_get_linear_damping(void *rigidbody)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  return data ? data->linear_damping : 0.0f;
}

void egn_rigidbody_set_linear_damping(void *rigidbody, f32 damping)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  if (data)
  {
    data->linear_damping = damping;
    mark_owner_collider_dirty(egn_component_from_handle(rigidbody));
  }
}

f32 egn_rigidbody_get_angular_damping(void *rigidbody)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  return data ? data->angular_damping : 0.0f;
}

void egn_rigidbody_set_angular_damping(void *rigidbody, f32 damping)
{
  EgnRigidbodyData *data = rigidbody_data(rigidbody);
  if (data)
  {
    data->angular_damping = damping;
    mark_owner_collider_dirty(egn_component_from_handle(rigidbody));
  }
}

void egn_rigidbody_apply_impulse(void *rigidbody, f32 x, f32 y, f32 z)
{
  egn_physics_apply_impulse(rigidbody, x, y, z);
}

void egn_rigidbody_get_linear_velocity(void *rigidbody, f32 *x, f32 *y, f32 *z)
{
  if (!egn_physics_get_linear_velocity(rigidbody, x, y, z))
  {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    if (z) *z = 0.0f;
  }
}

void egn_rigidbody_set_linear_velocity(void *rigidbody, f32 x, f32 y, f32 z)
{
  egn_physics_set_linear_velocity(rigidbody, x, y, z);
}

void egn_rigidbody_get_angular_velocity(void *rigidbody, f32 *x, f32 *y, f32 *z)
{
  if (!egn_physics_get_angular_velocity(rigidbody, x, y, z))
  {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    if (z) *z = 0.0f;
  }
}

void egn_rigidbody_set_angular_velocity(void *rigidbody, f32 x, f32 y, f32 z)
{
  egn_physics_set_angular_velocity(rigidbody, x, y, z);
}

// Collider ------------------------------------------------------------------

i32 egn_collider_get_shape(void *collider)
{
  EgnColliderData *data = collider_data(collider);
  return data ? data->shape_type : EGN_COLLIDER_BOX;
}

void egn_collider_set_shape(void *collider, i32 shape_type)
{
  EgnColliderData *data = collider_data(collider);
  if (!data)
    return;
  data->shape_type = (shape_type == EGN_COLLIDER_SPHERE ||
                      shape_type == EGN_COLLIDER_MESH ||
                          shape_type == EGN_COLLIDER_CAPSULE)
                         ? shape_type
                         : EGN_COLLIDER_BOX;
  data->dirty = true;
}

void egn_collider_get_size(void *collider, f32 *x, f32 *y, f32 *z)
{
  EgnColliderData *data = collider_data(collider);
  if (x) *x = data ? data->size_x : 1.0f;
  if (y) *y = data ? data->size_y : 1.0f;
  if (z) *z = data ? data->size_z : 1.0f;
}

void egn_collider_set_size(void *collider, f32 x, f32 y, f32 z)
{
  EgnColliderData *data = collider_data(collider);
  if (!data)
    return;
  data->size_x = x > 0.0f ? x : 1.0f;
  data->size_y = y > 0.0f ? y : 1.0f;
  data->size_z = z > 0.0f ? z : 1.0f;
  data->dirty = true;
}

f32 egn_collider_get_radius(void *collider)
{
  EgnColliderData *data = collider_data(collider);
  return data ? data->radius : 0.5f;
}

void egn_collider_set_radius(void *collider, f32 radius)
{
  EgnColliderData *data = collider_data(collider);
  if (!data)
    return;
  data->radius = radius > 0.0f ? radius : 0.5f;
  data->dirty = true;
}

u32 egn_collider_get_mesh(void *collider)
{
  EgnColliderData *data = collider_data(collider);
  return data ? data->mesh : 0;
}

void egn_collider_set_mesh(void *collider, u32 mesh)
{
  EgnColliderData *data = collider_data(collider);
  if (!data)
    return;
  data->mesh = mesh;
  data->dirty = true;
}

void egn_collider_get_offset(void *collider, f32 *x, f32 *y, f32 *z)
{
  EgnColliderData *data = collider_data(collider);
  if (x) *x = data ? data->offset_x : 0.0f;
  if (y) *y = data ? data->offset_y : 0.0f;
  if (z) *z = data ? data->offset_z : 0.0f;
}

void egn_collider_set_offset(void *collider, f32 x, f32 y, f32 z)
{
  EgnColliderData *data = collider_data(collider);
  if (!data)
    return;
  data->offset_x = x;
  data->offset_y = y;
  data->offset_z = z;
  data->dirty = true;
}

// Particles -----------------------------------------------------------------

u32 egn_particles_get_max(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->max_particles : 0;
}

void egn_particles_set_max(void *particles, u32 max_particles)
{
  EgnParticleData *data = particles_data(particles);
  if (!data)
    return;
  if (max_particles > 65536)
    max_particles = 65536;
  if (max_particles > 0)
    data->max_particles = max_particles;
}

u32 egn_particles_get_texture(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->texture : 0;
}

void egn_particles_set_texture(void *particles, u32 texture)
{
  EgnParticleData *data = particles_data(particles);
  if (data)
    data->texture = texture;
}

f32 egn_particles_get_spawn_rate(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->spawn_rate : 0.0f;
}

void egn_particles_set_spawn_rate(void *particles, f32 spawn_rate)
{
  EgnParticleData *data = particles_data(particles);
  if (data)
    data->spawn_rate = spawn_rate;
}

f32 egn_particles_get_lifetime(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->lifetime : 1.0f;
}

void egn_particles_set_lifetime(void *particles, f32 lifetime)
{
  EgnParticleData *data = particles_data(particles);
  if (data && lifetime > 0.0f)
    data->lifetime = lifetime;
}

f32 egn_particles_get_speed(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->speed : 0.0f;
}

void egn_particles_set_speed(void *particles, f32 speed)
{
  EgnParticleData *data = particles_data(particles);
  if (data)
    data->speed = speed;
}

f32 egn_particles_get_speed_variation(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->speed_variation : 0.0f;
}

f32 egn_particles_get_radius(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->radius : 0.0f;
}

void egn_particles_get_direction(void *particles, f32 *x, f32 *y, f32 *z)
{
  EgnParticleData *data = particles_data(particles);
  vec3 direction = data ? data->direction : vec3_up();
  if (x) *x = direction.x;
  if (y) *y = direction.y;
  if (z) *z = direction.z;
}

f32 egn_particles_get_size_scale(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->size_scale : 1.0f;
}

void egn_particles_get_fade(void *particles, f32 *fade_in, f32 *fade_out)
{
  EgnParticleData *data = particles_data(particles);
  if (fade_in) *fade_in = data ? data->fade_in : 0.1f;
  if (fade_out) *fade_out = data ? data->fade_out : 0.1f;
}

f32 egn_particles_get_gravity(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->gravity : 0.0f;
}

void egn_particles_set_gravity(void *particles, f32 gravity)
{
  EgnParticleData *data = particles_data(particles);
  if (data)
    data->gravity = gravity;
}

f32 egn_particles_get_drag(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->drag : 0.0f;
}

void egn_particles_set_drag(void *particles, f32 drag)
{
  EgnParticleData *data = particles_data(particles);
  if (data)
    data->drag = drag;
}

f32 egn_particles_get_spread(void *particles)
{
  EgnParticleData *data = particles_data(particles);
  return data ? data->spread : 0.0f;
}

void egn_particles_set_spread(void *particles, f32 spread)
{
  EgnParticleData *data = particles_data(particles);
  if (data)
    data->spread = spread;
}

void egn_particles_set_direction(void *particles, f32 x, f32 y, f32 z)
{
  EgnParticleData *data = particles_data(particles);
  if (!data)
    return;
  data->direction = vec3_create(x, y, z);
  if (vec3_length(data->direction) <= EGN_FLOAT_EPSILON)
    data->direction = vec3_up();
  else
    data->direction = vec3_normalized(data->direction);
}

void egn_particles_get_size(void *particles, f32 *start, f32 *end)
{
  EgnParticleData *data = particles_data(particles);
  if (start) *start = data ? data->size_start : 1.0f;
  if (end) *end = data ? data->size_end : 1.0f;
}

void egn_particles_set_size(void *particles, f32 start, f32 end)
{
  EgnParticleData *data = particles_data(particles);
  if (!data)
    return;
  data->size_start = start;
  data->size_end = end;
}

void egn_particles_get_color_start(void *particles, f32 *r, f32 *g, f32 *b, f32 *a)
{
  EgnParticleData *data = particles_data(particles);
  vec4 color = data ? data->color_start : vec4_one();
  if (r) *r = color.x;
  if (g) *g = color.y;
  if (b) *b = color.z;
  if (a) *a = color.w;
}

void egn_particles_set_color_start(void *particles, f32 r, f32 g, f32 b, f32 a)
{
  EgnParticleData *data = particles_data(particles);
  if (data)
    data->color_start = vec4_create(r, g, b, a);
}

void egn_particles_get_color_end(void *particles, f32 *r, f32 *g, f32 *b, f32 *a)
{
  EgnParticleData *data = particles_data(particles);
  vec4 color = data ? data->color_end : vec4_one();
  if (r) *r = color.x;
  if (g) *g = color.y;
  if (b) *b = color.z;
  if (a) *a = color.w;
}

void egn_particles_set_color_end(void *particles, f32 r, f32 g, f32 b, f32 a)
{
  EgnParticleData *data = particles_data(particles);
  if (data)
    data->color_end = vec4_create(r, g, b, a);
}

void egn_particles_emit(void *particles, u32 count)
{
  EgnParticleData *data = particles_data(particles);
  if (data)
    data->burst += (f32)count;
}


u32 egn_camera_ray_cast(void *handle, f32 screen_x, f32 screen_y,
                        f32 width, f32 height, f32 distance,
                        EgnPhysicsHit *out, u32 capacity)
{
  EgnComponent *component = egn_component_from_handle(handle);
  EgnCameraData *camera = camera_data(handle);
  if (!component || !camera || !component->enabled || !component->owner ||
      !egn_object_is_active_in_hierarchy(component->owner) ||
      !isfinite(width) || !isfinite(height) || width <= 0 || height <= 0 ||
      !isfinite(screen_x) || !isfinite(screen_y)) return 0;
  f32 viewport_x = camera->viewport_x * width;
  f32 viewport_y = (1.0f - camera->viewport_y - camera->viewport_height) * height;
  f32 viewport_width = camera->viewport_width * width;
  f32 viewport_height = camera->viewport_height * height;
  if (viewport_width <= 0 || viewport_height <= 0 || screen_x < viewport_x ||
      screen_x >= viewport_x + viewport_width || screen_y < viewport_y ||
      screen_y >= viewport_y + viewport_height) return 0;
  f32 nx = 2.0f * (screen_x - viewport_x) / viewport_width - 1.0f;
  f32 ny = 1.0f - 2.0f * (screen_y - viewport_y) / viewport_height;
  f32 aspect = viewport_width / viewport_height;
  vec3 origin = vec3_zero();
  vec3 direction = vec3_create(0, 0, -1);
  if (camera->projection == EGN_PROJECTION_ORTHOGRAPHIC)
  {
    origin.x = nx * camera->ortho_size * aspect * 0.5f;
    origin.y = ny * camera->ortho_size * 0.5f;
    origin.z = -camera->near_clip;
  }
  else
  {
    f32 half = tanf(deg_to_rad(camera->fov_degrees) * 0.5f);
    direction.x = nx * aspect * half;
    direction.y = ny * half;
  }
  mat4 world;
  egn_object_get_world_matrix_internal(component->owner, &world);
  vec3 world_origin = vec3_create(
    world.data[0] * origin.x + world.data[4] * origin.y + world.data[8] * origin.z + world.data[12],
    world.data[1] * origin.x + world.data[5] * origin.y + world.data[9] * origin.z + world.data[13],
    world.data[2] * origin.x + world.data[6] * origin.y + world.data[10] * origin.z + world.data[14]);
  quat rotation = egn_object_get_world_rotation(component->owner);
  mat4 orientation = quat_to_mat4(rotation);
  vec3 world_direction = vec3_create(
    orientation.data[0] * direction.x + orientation.data[4] * direction.y + orientation.data[8] * direction.z,
    orientation.data[1] * direction.x + orientation.data[5] * direction.y + orientation.data[9] * direction.z,
    orientation.data[2] * direction.x + orientation.data[6] * direction.y + orientation.data[10] * direction.z);
  return egn_physics_ray_cast(world_origin.x, world_origin.y, world_origin.z,
                              world_direction.x, world_direction.y, world_direction.z,
                              distance, out, capacity);
}
