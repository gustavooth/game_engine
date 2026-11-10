#include "vkrenderer_scene.h"
#include "shader/vkrenderer_shader.h"
#include "scene/scene_internal.h"
#include "core/logger.h"
#include "core/memo.h"

#define VKRENDERER_MAX_CAMERAS 64

typedef struct ScenePushConstants
{
  mat4 mvp;
  f32 normal[12]; // mat3 packed as three vec4 columns (48 bytes)
  vec4 tint;
} ScenePushConstants;

typedef char ScenePushConstants_size_check[(sizeof(ScenePushConstants) == 128) ? 1 : -1];

typedef struct SceneCameraInfo
{
  EgnObject *object;
  EgnCameraData *camera;
  i32 order;
  u32 sequence;
} SceneCameraInfo;

static f32 clampf(f32 value, f32 minimum, f32 maximum)
{
  if (value < minimum)
    return minimum;
  if (value > maximum)
    return maximum;
  return value;
}

static VkDescriptorSet resolve_texture(VkContext *ctx, u32 texture)
{
  if (texture > 0 && texture <= VKRENDERER_MAX_TEXTURES)
  {
    VkRendererTexture *slot = &ctx->textures[texture - 1];
    if (slot->used)
      return slot->descriptor_set;
  }

  return ctx->white_texture.descriptor_set;
}

static void pack_normal_matrix(mat4 model, f32 out[12])
{
  mat4 normal = mat4_transposed(mat4_inverse(model));

  out[0] = normal.data[0];
  out[1] = normal.data[1];
  out[2] = normal.data[2];
  out[3] = 0.0f;

  out[4] = normal.data[4];
  out[5] = normal.data[5];
  out[6] = normal.data[6];
  out[7] = 0.0f;

  out[8] = normal.data[8];
  out[9] = normal.data[9];
  out[10] = normal.data[10];
  out[11] = 0.0f;
}

static void draw_mesh(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                      VkRendererShader *shader, u32 mesh_handle, u32 texture_handle,
                      mat4 model, mat4 view_projection, vec4 tint,
                      VkDescriptorSet *last_texture)
{
  if (mesh_handle == 0 || mesh_handle > VKRENDERER_MAX_MESHES)
    return;

  VkRendererMesh *mesh = &ctx->meshes[mesh_handle - 1];
  if (!mesh->used)
    return;

  ScenePushConstants push = {0};
  push.mvp = mat4_mul(view_projection, model);
  pack_normal_matrix(model, push.normal);
  push.tint = tint;

  VkDescriptorSet texture = resolve_texture(ctx, texture_handle);
  if (texture != *last_texture)
  {
    vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            shader->pipeline.layout, 0, 1, &texture, 0, 0);
    *last_texture = texture;
  }

  vkCmdPushConstants(command_buffer->handle, shader->pipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(ScenePushConstants), &push);

  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(command_buffer->handle, 0, 1, &mesh->vertex_buffer, &offset);

  if (mesh->index_count > 0)
  {
    vkCmdBindIndexBuffer(command_buffer->handle, mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command_buffer->handle, mesh->index_count, 1, 0, 0, 0);
  }
  else
  {
    vkCmdDraw(command_buffer->handle, mesh->vertex_count, 1, 0, 0);
  }
}

static void collect_cameras(EgnObject *object, SceneCameraInfo *cameras,
                            u32 *camera_count, u32 *sequence)
{
  if (!object || !object->alive || !object->active)
    return;

  for (EgnComponent *component = object->first_component; component; component = component->next)
  {
    if (!component->alive || !component->enabled ||
        component->type != EGN_COMPONENT_TYPE_CAMERA)
      continue;

    if (*camera_count < VKRENDERER_MAX_CAMERAS)
    {
      SceneCameraInfo *info = &cameras[*camera_count];
      info->object = object;
      info->camera = &component->data.camera;
      info->order = component->data.camera.order;
      info->sequence = (*sequence)++;
      (*camera_count)++;
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    collect_cameras(child, cameras, camera_count, sequence);
}

static void sort_cameras(SceneCameraInfo *cameras, u32 camera_count)
{
  for (u32 i = 1; i < camera_count; i++)
  {
    SceneCameraInfo key = cameras[i];
    i32 j = (i32)i - 1;

    while (j >= 0 &&
           (cameras[j].order > key.order ||
            (cameras[j].order == key.order && cameras[j].sequence > key.sequence)))
    {
      cameras[j + 1] = cameras[j];
      j--;
    }

    cameras[j + 1] = key;
  }
}

static mat4 camera_view_matrix(EgnObject *object)
{
  mat4 world;
  egn_object_get_world_matrix_internal(object, &world);

  vec3 position = vec3_create(world.data[12], world.data[13], world.data[14]);

  vec3 right = vec3_create(world.data[0], world.data[1], world.data[2]);
  vec3 up = vec3_create(world.data[4], world.data[5], world.data[6]);
  vec3 back = vec3_create(world.data[8], world.data[9], world.data[10]);

  if (vec3_length(right) <= EGN_FLOAT_EPSILON)
    right = vec3_right();
  if (vec3_length(up) <= EGN_FLOAT_EPSILON)
    up = vec3_up();
  if (vec3_length(back) <= EGN_FLOAT_EPSILON)
    back = vec3_back();

  right = vec3_normalized(right);
  up = vec3_normalized(up);
  back = vec3_normalized(back);

  vec3 forward = vec3_mul_scalar(back, -1.0f);
  return mat4_look_at(position, vec3_add(position, forward), up);
}

static b8 camera_viewport_pixels(const EgnCameraData *camera, u32 framebuffer_width,
                                 u32 framebuffer_height, u32 *out_x, u32 *out_y,
                                 u32 *out_width, u32 *out_height)
{
  f32 x = clampf(camera->viewport_x, 0.0f, 1.0f);
  f32 y = clampf(camera->viewport_y, 0.0f, 1.0f);
  f32 width = clampf(camera->viewport_width, 0.0f, 1.0f - x);
  f32 height = clampf(camera->viewport_height, 0.0f, 1.0f - y);

  if (width <= 0.0f || height <= 0.0f)
    return false;

  u32 pixel_x = (u32)(x * (f32)framebuffer_width);
  u32 pixel_y = (u32)(y * (f32)framebuffer_height);
  u32 pixel_width = (u32)(width * (f32)framebuffer_width);
  u32 pixel_height = (u32)(height * (f32)framebuffer_height);

  if (pixel_x >= framebuffer_width)
    pixel_x = framebuffer_width - 1;
  if (pixel_y >= framebuffer_height)
    pixel_y = framebuffer_height - 1;

  if (pixel_width == 0)
    pixel_width = 1;
  if (pixel_height == 0)
    pixel_height = 1;

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

static void clear_camera_area(VkRendererCommandBuffer *command_buffer,
                              const EgnCameraData *camera,
                              u32 x, u32 y, u32 width, u32 height)
{
  VkClearAttachment attachments[2];
  MEMOZERO(attachments, sizeof(attachments));

  u32 attachment_count = 0;

  if (camera->clear_color)
  {
    VkClearAttachment *attachment = &attachments[attachment_count++];
    attachment->aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment->colorAttachment = 0;
    attachment->clearValue.color.float32[0] = camera->clear_r;
    attachment->clearValue.color.float32[1] = camera->clear_g;
    attachment->clearValue.color.float32[2] = camera->clear_b;
    attachment->clearValue.color.float32[3] = camera->clear_a;
  }

  if (camera->clear_depth)
  {
    VkClearAttachment *attachment = &attachments[attachment_count++];
    attachment->aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    attachment->colorAttachment = 0;
    attachment->clearValue.depthStencil.depth = 1.0f;
    attachment->clearValue.depthStencil.stencil = 0;
  }

  if (attachment_count == 0)
    return;

  VkClearRect clear_rect = {0};
  clear_rect.rect.offset.x = (i32)x;
  clear_rect.rect.offset.y = (i32)y;
  clear_rect.rect.extent.width = width;
  clear_rect.rect.extent.height = height;
  clear_rect.baseArrayLayer = 0;
  clear_rect.layerCount = 1;

  vkCmdClearAttachments(command_buffer->handle, attachment_count, attachments,
                        1, &clear_rect);
}

static void render_models(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                          EgnObject *object, const SceneCameraInfo *camera,
                          mat4 view_projection, VkDescriptorSet *last_texture)
{
  if (!object || !object->alive || !object->active)
    return;

  b8 layer_visible = object->layer < EGN_SCENE_MAX_LAYERS &&
                     (camera->camera->layer_mask & (1u << object->layer)) != 0;

  if (layer_visible)
  {
    mat4 world;
    b8 world_ready = false;

    for (EgnComponent *component = object->first_component; component; component = component->next)
    {
      if (!component->alive || !component->enabled ||
          component->type != EGN_COMPONENT_TYPE_MODEL)
        continue;

      if (!world_ready)
      {
        egn_object_get_world_matrix_internal(object, &world);
        world_ready = true;
      }

      EgnModelData *model = &component->data.model;
      vec4 tint = vec4_create(model->r, model->g, model->b, model->a);
      draw_mesh(ctx, command_buffer, &ctx->shader_3d, model->mesh, model->texture,
                world, view_projection, tint, last_texture);
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    render_models(ctx, command_buffer, child, camera, view_projection, last_texture);
}

static void render_sprites(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                           EgnObject *object, const SceneCameraInfo *camera,
                           mat4 view_projection, VkDescriptorSet *last_texture)
{
  if (!object || !object->alive || !object->active)
    return;

  b8 layer_visible = object->layer < EGN_SCENE_MAX_LAYERS &&
                     (camera->camera->layer_mask & (1u << object->layer)) != 0;

  if (layer_visible)
  {
    mat4 world;
    b8 world_ready = false;

    for (EgnComponent *component = object->first_component; component; component = component->next)
    {
      if (!component->alive || !component->enabled ||
          component->type != EGN_COMPONENT_TYPE_SPRITE)
        continue;

      if (!world_ready)
      {
        egn_object_get_world_matrix_internal(object, &world);
        world_ready = true;
      }

      EgnSpriteData *sprite = &component->data.sprite;
      if (sprite->width <= 0.0f || sprite->height <= 0.0f)
        continue;

      f32 offset_x = (0.5f - sprite->pivot_x) * sprite->width;
      f32 offset_y = (0.5f - sprite->pivot_y) * sprite->height;

      mat4 local = mat4_mul(
          mat4_translation(vec3_create(offset_x, offset_y, 0.0f)),
          mat4_scale(vec3_create(sprite->width, sprite->height, 1.0f)));
      mat4 model = mat4_mul(world, local);

      vec4 tint = vec4_create(sprite->r, sprite->g, sprite->b, sprite->a);
      draw_mesh(ctx, command_buffer, &ctx->shader_sprite, ctx->sprite_mesh,
                sprite->texture, model, view_projection, tint, last_texture);
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    render_sprites(ctx, command_buffer, child, camera, view_projection, last_texture);
}

void vkrenderer_scene_render(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  EgnScene *scene = egn_scene_get();
  if (!scene || !scene->root)
    return;

  SceneCameraInfo cameras[VKRENDERER_MAX_CAMERAS];
  MEMOZERO(cameras, sizeof(cameras));

  u32 camera_count = 0;
  u32 sequence = 0;
  collect_cameras(scene->root, cameras, &camera_count, &sequence);

  if (camera_count == 0)
    return;

  sort_cameras(cameras, camera_count);

  for (u32 i = 0; i < camera_count; i++)
  {
    SceneCameraInfo *camera = &cameras[i];
    if (!camera->object || !camera->camera)
      continue;

    u32 x, y, width, height;
    if (!camera_viewport_pixels(camera->camera, ctx->framebuffer_width,
                                ctx->framebuffer_height, &x, &y, &width, &height))
      continue;

    VkViewport viewport;
    viewport.x = (f32)x;
    viewport.y = (f32)(y + height);
    viewport.width = (f32)width;
    viewport.height = -(f32)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset.x = (i32)x;
    scissor.offset.y = (i32)y;
    scissor.extent.width = width;
    scissor.extent.height = height;

    vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);

    clear_camera_area(command_buffer, camera->camera, x, y, width, height);

    mat4 view = camera_view_matrix(camera->object);
    f32 aspect = (f32)width / (f32)height;

    mat4 projection;
    if (camera->camera->projection == EGN_PROJECTION_ORTHOGRAPHIC)
    {
      f32 half_height = camera->camera->ortho_size * 0.5f;
      f32 half_width = half_height * aspect;
      projection = mat4_orthographic(-half_width, half_width,
                                     -half_height, half_height,
                                     camera->camera->near_clip,
                                     camera->camera->far_clip);
    }
    else
    {
      projection = mat4_perspective(deg_to_rad(camera->camera->fov_degrees),
                                    aspect,
                                    camera->camera->near_clip,
                                    camera->camera->far_clip);
    }

    mat4 view_projection = mat4_mul(projection, view);

    vkrenderer_shader_bind(ctx, &ctx->shader_3d, command_buffer->handle);
    VkDescriptorSet last_texture = 0;
    render_models(ctx, command_buffer, scene->root, camera, view_projection, &last_texture);

    vkrenderer_shader_bind(ctx, &ctx->shader_sprite, command_buffer->handle);
    last_texture = 0;
    render_sprites(ctx, command_buffer, scene->root, camera, view_projection, &last_texture);
  }
}
