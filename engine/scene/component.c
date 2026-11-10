#include "scene_internal.h"
#include "core/logger.h"
#include "core/memo.h"

#include <string.h>

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
  if (!component || component->type != EGN_COMPONENT_TYPE_CAMERA)
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

void *egn_object_add_model(void *object_handle, u32 mesh, u32 texture,
                           f32 r, f32 g, f32 b, f32 a, b8 enabled)
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
