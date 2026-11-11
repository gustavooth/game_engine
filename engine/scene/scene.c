#include "scene_internal.h"
#include "core/logger.h"
#include "core/memo.h"

#include <stdio.h>
#include <string.h>

static EgnScene scene;
static b8 scene_initialized;

EgnScene *egn_scene_get(void)
{
  return &scene;
}

static b8 ensure_object_capacity(void)
{
  if (scene.object_count < scene.object_capacity)
    return true;

  u32 new_capacity = scene.object_capacity ? scene.object_capacity * 2 : 64;
  EgnObject **objects = MEMOALLOC(sizeof(EgnObject *) * new_capacity, MEMO_SCENE);
  if (!objects)
    return false;

  if (scene.objects)
  {
    MEMOCPY(objects, scene.objects, sizeof(EgnObject *) * scene.object_count);
    MEMOFREE(scene.objects);
  }

  scene.objects = objects;
  scene.object_capacity = new_capacity;
  return true;
}

static EgnObject *allocate_object(void)
{
  if (!ensure_object_capacity())
    return 0;

  EgnObject *object = MEMOZALLOC(sizeof(EgnObject), MEMO_SCENE);
  if (!object)
    return 0;

  object->magic = EGN_OBJECT_MAGIC;
  object->id = scene.next_object_id++;
  object->rotation = quat_identity();
  object->scale = vec3_one();
  object->active = true;
  object->canvas_anchor = EGN_ANCHOR_CENTER;
  object->alive = true;
  object->world_dirty = true;
  object->world_matrix = mat4_identity();

  scene.objects[scene.object_count++] = object;
  return object;
}

static void set_name_internal(EgnObject *object, const char *name)
{
  if (!object)
    return;

  if (!name)
    name = "Object";

  snprintf(object->name, sizeof(object->name), "%s", name);
}

static void append_child(EgnObject *parent, EgnObject *child)
{
  child->parent = parent;
  child->prev_sibling = 0;
  child->next_sibling = 0;

  if (!parent->first_child)
  {
    parent->first_child = child;
    return;
  }

  EgnObject *last = parent->first_child;
  while (last->next_sibling)
    last = last->next_sibling;

  last->next_sibling = child;
  child->prev_sibling = last;
}

static void detach_from_parent(EgnObject *object)
{
  EgnObject *parent = object->parent;
  if (!parent)
    return;

  if (object->prev_sibling)
    object->prev_sibling->next_sibling = object->next_sibling;
  else if (parent->first_child == object)
    parent->first_child = object->next_sibling;

  if (object->next_sibling)
    object->next_sibling->prev_sibling = object->prev_sibling;

  object->parent = 0;
  object->prev_sibling = 0;
  object->next_sibling = 0;
}

static b8 is_descendant_of(EgnObject *candidate, EgnObject *ancestor)
{
  EgnObject *current = candidate;
  while (current)
  {
    if (current == ancestor)
      return true;
    current = current->parent;
  }
  return false;
}

EgnObject *egn_object_from_handle(void *handle)
{
  if (!handle)
    return 0;

  EgnObject *object = (EgnObject *)handle;
  if (object->magic != EGN_OBJECT_MAGIC || !object->alive)
    return 0;

  return object;
}

void egn_object_mark_world_dirty(EgnObject *object)
{
  // A dirty object always has a dirty subtree: a child is only cleaned by
  // computing its parents first, and new children start dirty. So the walk
  // can stop here instead of revisiting the whole subtree on every setter
  // call (objects driven by physics or scripts move every frame).
  if (!object || object->world_dirty)
    return;

  object->world_dirty = true;

  EgnObject *child = object->first_child;
  while (child)
  {
    egn_object_mark_world_dirty(child);
    child = child->next_sibling;
  }
}

mat4 egn_object_local_matrix(const EgnObject *object)
{
  mat4 translation = mat4_translation(object->position);
  mat4 rotation = quat_to_mat4(object->rotation);
  mat4 scale = mat4_scale(object->scale);
  return mat4_mul(translation, mat4_mul(rotation, scale));
}

void egn_object_get_world_matrix_internal(EgnObject *object, mat4 *out_matrix)
{
  if (!out_matrix)
    return;

  if (!object || !object->alive)
  {
    *out_matrix = mat4_identity();
    return;
  }

  if (!object->world_dirty)
  {
    *out_matrix = object->world_matrix;
    return;
  }

  mat4 local = egn_object_local_matrix(object);

  if (object->parent)
  {
    mat4 parent_world;
    egn_object_get_world_matrix_internal(object->parent, &parent_world);
    object->world_matrix = mat4_mul(parent_world, local);
  }
  else
  {
    object->world_matrix = local;
  }

  object->world_dirty = false;
  *out_matrix = object->world_matrix;
}

quat egn_object_get_world_rotation(EgnObject *object)
{
  if (!object)
    return quat_identity();

  quat rotation = object->rotation;

  EgnObject *parent = object->parent;
  while (parent)
  {
    rotation = quat_mul(parent->rotation, rotation);
    parent = parent->parent;
  }

  return quat_normalize(rotation);
}

vec3 egn_object_get_world_position(EgnObject *object)
{
  mat4 world;
  egn_object_get_world_matrix_internal(object, &world);
  return vec3_create(world.data[12], world.data[13], world.data[14]);
}

b8 egn_scene_initialize(void)
{
  if (scene_initialized)
    return true;

  MEMOZERO(&scene, sizeof(scene));
  scene.next_object_id = 1;

  scene.root = allocate_object();
  if (!scene.root)
  {
    egn_scene_shutdown();
    return false;
  }

  scene.root->id = 1;
  set_name_internal(scene.root, "Scene");
  egn_object_mark_world_dirty(scene.root);

  scene_initialized = true;
  EGNINFO("Scene initialized (root object id 1)");
  return true;
}

void *egn_scene_find_object_by_id(u32 id)
{
  if (!id) return 0;
  for (u32 i = 0; i < scene.object_count; ++i)
  {
    EgnObject *object = scene.objects[i];
    if (object && object->alive && object->id == id) return object;
  }
  return 0;
}

void egn_scene_shutdown(void)
{
  if (scene.objects)
  {
    for (u32 i = 0; i < scene.object_count; i++)
    {
      if (scene.objects[i])
        MEMOFREE(scene.objects[i]);
    }
    MEMOFREE(scene.objects);
  }

  if (scene.components)
  {
    for (u32 i = 0; i < scene.component_count; i++)
    {
      if (scene.components[i])
        MEMOFREE(scene.components[i]);
    }
    MEMOFREE(scene.components);
  }

  MEMOZERO(&scene, sizeof(scene));
  scene_initialized = false;
}

void *egn_scene_root(void)
{
  return scene.root;
}

b8 egn_object_is_valid(void *object)
{
  return egn_object_from_handle(object) != 0;
}

u32 egn_object_get_id(void *object)
{
  EgnObject *obj = egn_object_from_handle(object);
  return obj ? obj->id : 0;
}

void egn_object_set_name(void *object, const char *name)
{
  EgnObject *obj = egn_object_from_handle(object);
  if (obj)
    set_name_internal(obj, name);
}

const char *egn_object_get_name(void *object)
{
  EgnObject *obj = egn_object_from_handle(object);
  return obj ? obj->name : "";
}

void *egn_object_create(void *parent_handle, const char *name)
{
  EgnObject *parent = egn_object_from_handle(parent_handle);
  if (!parent)
    return 0;

  EgnObject *object = allocate_object();
  if (!object)
    return 0;

  set_name_internal(object, name);
  append_child(parent, object);
  egn_object_mark_world_dirty(object);

  return object;
}

void egn_object_destroy(void *handle)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object || object == scene.root)
    return;

  EgnObject *child = object->first_child;
  while (child)
  {
    EgnObject *next = child->next_sibling;
    egn_object_destroy(child);
    child = next;
  }

  EgnComponent *component = object->first_component;
  while (component)
  {
    component->alive = false;
    component->enabled = false;
    component = component->next;
  }
  object->first_component = 0;

  detach_from_parent(object);
  object->alive = false;
  object->active = false;
  object->world_dirty = true;
}

void *egn_object_get_parent(void *object)
{
  EgnObject *obj = egn_object_from_handle(object);
  return (obj && obj->parent) ? obj->parent : 0;
}

void egn_object_set_parent(void *handle, void *parent_handle)
{
  EgnObject *object = egn_object_from_handle(handle);
  EgnObject *parent = egn_object_from_handle(parent_handle);

  if (!object || !parent || object == scene.root)
    return;

  if (object == parent || is_descendant_of(parent, object))
    return;

  if (object->parent == parent)
    return;

  detach_from_parent(object);
  append_child(parent, object);
  egn_object_mark_world_dirty(object);
}

u32 egn_object_child_count(void *handle)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object)
    return 0;

  u32 count = 0;
  EgnObject *child = object->first_child;
  while (child)
  {
    if (child->alive)
      count++;
    child = child->next_sibling;
  }

  return count;
}

void *egn_object_child_at(void *handle, u32 index)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object)
    return 0;

  u32 current = 0;
  EgnObject *child = object->first_child;
  while (child)
  {
    if (child->alive)
    {
      if (current == index)
        return child;
      current++;
    }
    child = child->next_sibling;
  }

  return 0;
}

void egn_object_get_world_matrix(void *object_handle, f32 *out_matrix16)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object || !out_matrix16)
    return;

  mat4 world;
  egn_object_get_world_matrix_internal(object, &world);
  for (u32 i = 0; i < 16; i++)
    out_matrix16[i] = world.data[i];
}

void egn_object_set_position(void *handle, f32 x, f32 y, f32 z)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object)
    return;

  object->position = vec3_create(x, y, z);
  egn_object_mark_world_dirty(object);
}

void egn_object_get_position(void *handle, f32 *x, f32 *y, f32 *z)
{
  EgnObject *object = egn_object_from_handle(handle);
  vec3 position = object ? object->position : vec3_zero();

  if (x)
    *x = position.x;
  if (y)
    *y = position.y;
  if (z)
    *z = position.z;
}

void egn_object_set_rotation_quat(void *handle, f32 x, f32 y, f32 z, f32 w)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object)
    return;

  quat rotation = (quat){x, y, z, w};
  if (quat_normal(rotation) <= EGN_FLOAT_EPSILON)
    rotation = quat_identity();

  object->rotation = quat_normalize(rotation);
  egn_object_mark_world_dirty(object);
}

void egn_object_get_rotation_quat(void *handle, f32 *x, f32 *y, f32 *z, f32 *w)
{
  EgnObject *object = egn_object_from_handle(handle);
  quat rotation = object ? object->rotation : quat_identity();

  if (x)
    *x = rotation.x;
  if (y)
    *y = rotation.y;
  if (z)
    *z = rotation.z;
  if (w)
    *w = rotation.w;
}

void egn_object_set_rotation_euler(void *handle, f32 x, f32 y, f32 z)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object)
    return;

  object->rotation = quat_from_euler_xyz(x, y, z);
  egn_object_mark_world_dirty(object);
}

void egn_object_rotate_euler(void *handle, f32 x, f32 y, f32 z)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object)
    return;

  quat delta = quat_from_euler_xyz(x, y, z);
  object->rotation = quat_normalize(quat_mul(object->rotation, delta));
  egn_object_mark_world_dirty(object);
}

void egn_object_look_at(void *handle, f32 target_x, f32 target_y, f32 target_z)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object)
    return;

  vec3 position = egn_object_get_world_position(object);
  vec3 target = vec3_create(target_x, target_y, target_z);
  vec3 forward = vec3_sub(target, position);
  if (vec3_length(forward) <= EGN_FLOAT_EPSILON)
    return;
  forward = vec3_normalized(forward);

  vec3 up = vec3_up();
  if (egnabs(vec3_dot(forward, up)) > 0.999f)
    up = vec3_back();

  vec3 right = vec3_cross(forward, up);
  if (vec3_length(right) <= EGN_FLOAT_EPSILON)
    right = vec3_right();
  right = vec3_normalized(right);

  vec3 corrected_up = vec3_cross(right, forward);

  mat4 rotation_matrix = mat4_identity();
  rotation_matrix.data[0] = right.x;
  rotation_matrix.data[1] = right.y;
  rotation_matrix.data[2] = right.z;

  rotation_matrix.data[4] = corrected_up.x;
  rotation_matrix.data[5] = corrected_up.y;
  rotation_matrix.data[6] = corrected_up.z;

  rotation_matrix.data[8] = -forward.x;
  rotation_matrix.data[9] = -forward.y;
  rotation_matrix.data[10] = -forward.z;

  quat world_rotation = quat_from_mat4(rotation_matrix);
  quat parent_rotation = object->parent ? egn_object_get_world_rotation(object->parent) : quat_identity();
  quat local_rotation = quat_normalize(quat_mul(quat_inverse(parent_rotation), world_rotation));

  object->rotation = local_rotation;
  egn_object_mark_world_dirty(object);
}

void egn_object_set_scale(void *handle, f32 x, f32 y, f32 z)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object)
    return;

  object->scale = vec3_create(x, y, z);
  egn_object_mark_world_dirty(object);
}

void egn_object_get_scale(void *handle, f32 *x, f32 *y, f32 *z)
{
  EgnObject *object = egn_object_from_handle(handle);
  vec3 scale = object ? object->scale : vec3_one();

  if (x)
    *x = scale.x;
  if (y)
    *y = scale.y;
  if (z)
    *z = scale.z;
}

void egn_object_set_layer(void *handle, u32 layer)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object)
    return;

  object->layer = layer < EGN_SCENE_MAX_LAYERS ? layer : EGN_SCENE_MAX_LAYERS - 1;
}

u32 egn_object_get_layer(void *handle)
{
  EgnObject *object = egn_object_from_handle(handle);
  return object ? object->layer : 0;
}

void egn_object_set_active(void *handle, b8 active)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (object)
    object->active = active;
}

b8 egn_object_is_active(void *handle)
{
  EgnObject *object = egn_object_from_handle(handle);
  return object ? object->active : false;
}

b8 egn_object_is_active_in_hierarchy(void *handle)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (!object)
    return false;

  EgnObject *current = object;
  while (current)
  {
    if (!current->alive || !current->active)
      return false;
    current = current->parent;
  }

  return true;
}

static void scene_update_object(EgnObject *object, f64 delta_time)
{
  if (!object || !object->alive || !object->active)
    return;

  for (EgnComponent *component = object->first_component; component; component = component->next)
  {
    if (!component->alive || !component->enabled ||
        component->type != EGN_COMPONENT_TYPE_ANIMATION)
      continue;

    EgnAnimationData *animation = &component->data.animation;
    if (animation->playing && animation->speed != 0.0f)
      animation->time += (f32)delta_time * animation->speed;
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    scene_update_object(child, delta_time);
}

void egn_scene_update(f64 delta_time)
{
  if (!scene_initialized || !scene.root)
    return;

  scene_update_object(scene.root, delta_time);
}

void egn_object_set_render_domain(void *handle, i32 domain)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (object && domain >= EGN_RENDER_3D && domain <= EGN_RENDER_CANVAS)
    object->render_domain = domain;
}
i32 egn_object_get_render_domain(void *handle)
{
  EgnObject *object = egn_object_from_handle(handle);
  return object ? object->render_domain : EGN_RENDER_3D;
}
void egn_object_set_canvas_anchor(void *handle, i32 anchor)
{
  EgnObject *object = egn_object_from_handle(handle);
  if (object && anchor >= EGN_ANCHOR_BOTTOM_LEFT && anchor <= EGN_ANCHOR_TOP_RIGHT)
    object->canvas_anchor = anchor;
}
i32 egn_object_get_canvas_anchor(void *handle)
{
  EgnObject *object = egn_object_from_handle(handle);
  return object ? object->canvas_anchor : EGN_ANCHOR_CENTER;
}

// ---------------------------------------------------------------------------
// Canvas hit testing
// ---------------------------------------------------------------------------

static f32 canvas_clampf(f32 value, f32 min_value, f32 max_value)
{
  if (value < min_value)
    return min_value;
  if (value > max_value)
    return max_value;
  return value;
}

typedef struct CanvasCameraQuery
{
  EgnComponent *best;
  b8 has_best;
  i32 order;
  u32 sequence;
} CanvasCameraQuery;

static void canvas_find_camera_recursive(EgnObject *object, CanvasCameraQuery *query,
                                         u32 *sequence)
{
  if (!object || !object->alive || !object->active)
    return;

  for (EgnComponent *component = object->first_component; component;
       component = component->next)
  {
    if (!component->alive || !component->enabled ||
        component->type != EGN_COMPONENT_TYPE_CANVAS)
      continue;

    u32 current_sequence = (*sequence)++;
    i32 order = component->data.camera.order;
    if (!query->has_best || order > query->order ||
        (order == query->order && current_sequence > query->sequence))
    {
      query->best = component;
      query->has_best = true;
      query->order = order;
      query->sequence = current_sequence;
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    canvas_find_camera_recursive(child, query, sequence);
}

static EgnComponent *canvas_top_camera(void)
{
  EgnScene *scene_state = egn_scene_get();
  if (!scene_state || !scene_state->root)
    return 0;

  CanvasCameraQuery query = {0};
  u32 sequence = 0;
  canvas_find_camera_recursive(scene_state->root, &query, &sequence);
  return query.best;
}

static b8 canvas_viewport_pixels(const EgnCameraData *camera,
                                 f32 framebuffer_width, f32 framebuffer_height,
                                 f32 *out_x, f32 *out_y,
                                 f32 *out_width, f32 *out_height)
{
  if (!camera || !out_x || !out_y || !out_width || !out_height)
    return false;
  if (framebuffer_width <= 0.0f || framebuffer_height <= 0.0f)
    return false;

  f32 x = canvas_clampf(camera->viewport_x, 0.0f, 1.0f);
  f32 y = canvas_clampf(camera->viewport_y, 0.0f, 1.0f);
  f32 width = canvas_clampf(camera->viewport_width, 0.0f, 1.0f - x);
  f32 height = canvas_clampf(camera->viewport_height, 0.0f, 1.0f - y);

  if (width <= 0.0f || height <= 0.0f)
    return false;

  f32 pixel_x = x * framebuffer_width;
  f32 pixel_y = y * framebuffer_height;
  f32 pixel_width = width * framebuffer_width;
  f32 pixel_height = height * framebuffer_height;

  if (pixel_x >= framebuffer_width)
    pixel_x = framebuffer_width - 1.0f;
  if (pixel_y >= framebuffer_height)
    pixel_y = framebuffer_height - 1.0f;

  if (pixel_width < 1.0f)
    pixel_width = 1.0f;
  if (pixel_height < 1.0f)
    pixel_height = 1.0f;

  if (pixel_x + pixel_width > framebuffer_width)
    pixel_width = framebuffer_width - pixel_x;
  if (pixel_y + pixel_height > framebuffer_height)
    pixel_height = framebuffer_height - pixel_y;

  *out_x = pixel_x;
  *out_y = pixel_y;
  *out_width = pixel_width;
  *out_height = pixel_height;
  return true;
}

static void canvas_anchor_offset(const EgnObject *object, f32 viewport_width,
                                 f32 viewport_height, f32 *out_x, f32 *out_y)
{
  i32 anchor = object ? object->canvas_anchor : EGN_ANCHOR_CENTER;
  *out_x = (f32)((anchor % 3) - 1) * viewport_width * 0.5f;
  *out_y = (f32)((anchor / 3) - 1) * viewport_height * 0.5f;
}

static void canvas_transform_point(const mat4 *world, f32 local_x, f32 local_y,
                                   f32 anchor_x, f32 anchor_y,
                                   f32 viewport_x, f32 viewport_y,
                                   f32 viewport_width, f32 viewport_height,
                                   f32 *out_x, f32 *out_y)
{
  const f32 *m = world->data;
  f32 canvas_x = m[0] * local_x + m[4] * local_y + m[12] + anchor_x;
  f32 canvas_y = m[1] * local_x + m[5] * local_y + m[13] + anchor_y;

  // The canvas projection maps the viewport centre to (0, 0) and +Y upwards,
  // while screen coordinates start at the top-left corner and grow downwards.
  *out_x = viewport_x + (canvas_x + viewport_width * 0.5f);
  *out_y = viewport_y + (viewport_height * 0.5f - canvas_y);
}

static void canvas_sprite_quad(const EgnObject *object, const EgnSpriteData *sprite,
                               const mat4 *world,
                               f32 viewport_x, f32 viewport_y,
                               f32 viewport_width, f32 viewport_height,
                               f32 out_x[4], f32 out_y[4])
{
  f32 anchor_x = 0.0f;
  f32 anchor_y = 0.0f;
  canvas_anchor_offset(object, viewport_width, viewport_height, &anchor_x, &anchor_y);

  f32 center_x = (0.5f - sprite->pivot_x) * sprite->width;
  f32 center_y = (0.5f - sprite->pivot_y) * sprite->height;
  f32 half_width = sprite->width * 0.5f;
  f32 half_height = sprite->height * 0.5f;

  f32 local_x[4] = {
      center_x - half_width,
      center_x + half_width,
      center_x + half_width,
      center_x - half_width,
  };
  f32 local_y[4] = {
      center_y - half_height,
      center_y - half_height,
      center_y + half_height,
      center_y + half_height,
  };

  for (i32 i = 0; i < 4; i++)
  {
    canvas_transform_point(world, local_x[i], local_y[i], anchor_x, anchor_y,
                           viewport_x, viewport_y, viewport_width, viewport_height,
                           &out_x[i], &out_y[i]);
  }
}

static b8 canvas_point_in_quad(f32 point_x, f32 point_y,
                               const f32 quad_x[4], const f32 quad_y[4])
{
  b8 has_negative = false;
  b8 has_positive = false;

  for (i32 i = 0; i < 4; i++)
  {
    i32 next = (i + 1) & 3;
    f32 cross = (quad_x[next] - quad_x[i]) * (point_y - quad_y[i]) -
                (quad_y[next] - quad_y[i]) * (point_x - quad_x[i]);
    if (cross < -0.001f)
      has_negative = true;
    if (cross > 0.001f)
      has_positive = true;
  }

  return !(has_negative && has_positive);
}

static void canvas_pick_recursive(EgnObject *object, const EgnCameraData *camera,
                                  f32 viewport_x, f32 viewport_y,
                                  f32 viewport_width, f32 viewport_height,
                                  f32 point_x, f32 point_y, u32 *hit)
{
  if (!object || !object->alive || !object->active)
    return;

  b8 layer_visible = object->render_domain == EGN_RENDER_CANVAS &&
                     object->layer < EGN_SCENE_MAX_LAYERS &&
                     (camera->layer_mask & (1u << object->layer)) != 0;

  if (layer_visible)
  {
    mat4 world;
    b8 world_ready = false;

    for (EgnComponent *component = object->first_component; component;
         component = component->next)
    {
      if (!component->alive || !component->enabled ||
          component->type != EGN_COMPONENT_TYPE_SPRITE)
        continue;

      EgnSpriteData *sprite = &component->data.sprite;
      if (sprite->width <= 0.0f || sprite->height <= 0.0f)
        continue;

      if (!world_ready)
      {
        egn_object_get_world_matrix_internal(object, &world);
        world_ready = true;
      }

      f32 quad_x[4];
      f32 quad_y[4];
      canvas_sprite_quad(object, sprite, &world, viewport_x, viewport_y,
                         viewport_width, viewport_height, quad_x, quad_y);

      if (canvas_point_in_quad(point_x, point_y, quad_x, quad_y))
        *hit = object->id;
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    canvas_pick_recursive(child, camera, viewport_x, viewport_y,
                          viewport_width, viewport_height, point_x, point_y, hit);
}

EGNAPI u32 egn_canvas_pick(f32 screen_x, f32 screen_y,
                           f32 framebuffer_width, f32 framebuffer_height)
{
  EgnComponent *camera_component = canvas_top_camera();
  if (!camera_component)
    return 0;

  f32 viewport_x = 0.0f;
  f32 viewport_y = 0.0f;
  f32 viewport_width = 0.0f;
  f32 viewport_height = 0.0f;
  if (!canvas_viewport_pixels(&camera_component->data.camera,
                              framebuffer_width, framebuffer_height,
                              &viewport_x, &viewport_y,
                              &viewport_width, &viewport_height))
    return 0;

  EgnScene *scene_state = egn_scene_get();
  if (!scene_state || !scene_state->root)
    return 0;

  u32 hit = 0;
  canvas_pick_recursive(scene_state->root, &camera_component->data.camera,
                        viewport_x, viewport_y, viewport_width, viewport_height,
                        screen_x, screen_y, &hit);
  return hit;
}

EGNAPI b8 egn_object_canvas_rect(void *object_handle,
                                 f32 framebuffer_width, f32 framebuffer_height,
                                 f32 *out_x, f32 *out_y,
                                 f32 *out_width, f32 *out_height)
{
  EgnObject *object = egn_object_from_handle(object_handle);
  if (!object || !out_x || !out_y || !out_width || !out_height)
    return false;
  if (object->render_domain != EGN_RENDER_CANVAS)
    return false;

  EgnComponent *camera_component = canvas_top_camera();
  if (!camera_component)
    return false;

  f32 viewport_x = 0.0f;
  f32 viewport_y = 0.0f;
  f32 viewport_width = 0.0f;
  f32 viewport_height = 0.0f;
  if (!canvas_viewport_pixels(&camera_component->data.camera,
                              framebuffer_width, framebuffer_height,
                              &viewport_x, &viewport_y,
                              &viewport_width, &viewport_height))
    return false;

  mat4 world;
  egn_object_get_world_matrix_internal(object, &world);

  b8 has_rect = false;
  f32 min_x = 0.0f;
  f32 min_y = 0.0f;
  f32 max_x = 0.0f;
  f32 max_y = 0.0f;

  for (EgnComponent *component = object->first_component; component;
       component = component->next)
  {
    if (!component->alive || !component->enabled ||
        component->type != EGN_COMPONENT_TYPE_SPRITE)
      continue;

    EgnSpriteData *sprite = &component->data.sprite;
    if (sprite->width <= 0.0f || sprite->height <= 0.0f)
      continue;

    f32 quad_x[4];
    f32 quad_y[4];
    canvas_sprite_quad(object, sprite, &world, viewport_x, viewport_y,
                       viewport_width, viewport_height, quad_x, quad_y);

    for (i32 i = 0; i < 4; i++)
    {
      if (!has_rect)
      {
        min_x = max_x = quad_x[i];
        min_y = max_y = quad_y[i];
        has_rect = true;
      }
      else
      {
        if (quad_x[i] < min_x)
          min_x = quad_x[i];
        if (quad_x[i] > max_x)
          max_x = quad_x[i];
        if (quad_y[i] < min_y)
          min_y = quad_y[i];
        if (quad_y[i] > max_y)
          max_y = quad_y[i];
      }
    }
  }

  if (!has_rect)
    return false;

  *out_x = min_x;
  *out_y = min_y;
  *out_width = max_x - min_x;
  *out_height = max_y - min_y;
  return true;
}
