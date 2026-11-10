#include "physics.h"
#include "core/logger.h"
#include "core/memo.h"
#include "math/math.h"
#include "renderer/renderer.h"
#include "scene/scene_internal.h"

#include "joltc.h"

#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define EGN_PHYSICS_OBJECT_LAYER_NON_MOVING 0
#define EGN_PHYSICS_OBJECT_LAYER_MOVING 1
#define EGN_PHYSICS_FIXED_STEP (1.0 / 60.0)

typedef struct PhysicsState
{
  b8 initialized;
  JPH_JobSystem *job_system;
  JPH_PhysicsSystem *system;
  JPH_BodyInterface *body_interface;
  JPH_ObjectLayerPairFilter *object_layer_pair_filter;
  JPH_BroadPhaseLayerInterface *broad_phase_layer_interface;
  JPH_ObjectVsBroadPhaseLayerFilter *object_vs_broadphase_filter;
  f64 accumulator;
} PhysicsState;

static PhysicsState physics;

static void physics_trace(const char *message)
{
  (void)message;
}

// ---------------------------------------------------------------------------
// Scene helpers
// ---------------------------------------------------------------------------

static EgnComponent *find_component(EgnObject *object, i32 type)
{
  if (!object)
    return 0;

  for (EgnComponent *component = object->first_component; component; component = component->next)
  {
    if (component->alive && component->enabled && component->type == type)
      return component;
  }
  return 0;
}


static vec3 matrix_scale(mat4 matrix)
{
  vec3 c0 = vec3_create(matrix.data[0], matrix.data[1], matrix.data[2]);
  vec3 c1 = vec3_create(matrix.data[4], matrix.data[5], matrix.data[6]);
  vec3 c2 = vec3_create(matrix.data[8], matrix.data[9], matrix.data[10]);
  return vec3_create(vec3_length(c0), vec3_length(c1), vec3_length(c2));
}

static vec3 matrix_transform_point(mat4 matrix, vec3 point)
{
  vec4 p = vec4_from_vec3(point, 1.0f);
  return vec3_create(matrix.data[0] * p.x + matrix.data[4] * p.y + matrix.data[8] * p.z + matrix.data[12],
                     matrix.data[1] * p.x + matrix.data[5] * p.y + matrix.data[9] * p.z + matrix.data[13],
                     matrix.data[2] * p.x + matrix.data[6] * p.y + matrix.data[10] * p.z + matrix.data[14]);
}

static void world_to_local(EgnObject *object, vec3 world_position, quat world_rotation,
                           vec3 *out_position, quat *out_rotation)
{
  if (!object || !object->parent)
  {
    *out_position = world_position;
    *out_rotation = world_rotation;
    return;
  }

  mat4 parent_world;
  egn_object_get_world_matrix_internal(object->parent, &parent_world);
  mat4 inverse = mat4_inverse(parent_world);

  *out_position = matrix_transform_point(inverse, world_position);
  quat parent_rotation = quat_from_mat4(parent_world);
  *out_rotation = quat_normalize(quat_mul(quat_inverse(parent_rotation), world_rotation));
}

// ---------------------------------------------------------------------------
// Jolt body creation
// ---------------------------------------------------------------------------

static JPH_Shape *create_collider_shape(EgnObject *object, EgnColliderData *collider)
{
  mat4 world;
  egn_object_get_world_matrix_internal(object, &world);
  vec3 scale = matrix_scale(world);
  if (scale.x < 0.0001f) scale.x = 1.0f;
  if (scale.y < 0.0001f) scale.y = 1.0f;
  if (scale.z < 0.0001f) scale.z = 1.0f;

  JPH_Shape *shape = 0;

  if (collider->shape_type == EGN_COLLIDER_SPHERE)
  {
    f32 radius = collider->radius * (scale.x > scale.y ? scale.x : scale.y);
    if (radius < 0.01f)
      radius = 0.01f;
    shape = (JPH_Shape *)JPH_SphereShape_Create(radius);
  }
  else if (collider->shape_type == EGN_COLLIDER_CAPSULE)
  {
    // size_y is the total height, including both hemispheres.
    f32 radial_scale = scale.x > scale.z ? scale.x : scale.z;
    f32 radius = collider->radius * radial_scale;
    if (radius < 0.01f) radius = 0.01f;
    f32 half_cylinder = collider->size_y * scale.y * 0.5f - radius;
    if (half_cylinder < 0.001f) half_cylinder = 0.001f;
    shape = (JPH_Shape *)JPH_CapsuleShape_Create(half_cylinder, radius);
  }
  else if (collider->shape_type == EGN_COLLIDER_MESH)
  {
    const vec3 *positions = 0;
    const u32 *indices = 0;
    u32 position_count = 0;
    u32 index_count = 0;

    if (collider->mesh == 0 ||
        !renderer_mesh_get_collision(collider->mesh, &positions, &position_count,
                                     &indices, &index_count) ||
        position_count == 0 || index_count < 3)
    {
      EGNWARN("PHYSICS mesh collider has no valid mesh; falling back to a box");
      JPH_Vec3 half_extents = {collider->size_x * 0.5f * scale.x,
                               collider->size_y * 0.5f * scale.y,
                               collider->size_z * 0.5f * scale.z};
      shape = (JPH_Shape *)JPH_BoxShape_Create(&half_extents, JPH_DEFAULT_CONVEX_RADIUS);
    }
    else
    {
      JPH_Vec3 *vertices = MEMOALLOC(sizeof(JPH_Vec3) * position_count, MEMO_SCENE);
      u32 triangle_count = index_count / 3;
      JPH_IndexedTriangle *triangles = MEMOALLOC(sizeof(JPH_IndexedTriangle) * triangle_count, MEMO_SCENE);
      if (!vertices || !triangles)
      {
        if (vertices) MEMOFREE(vertices);
        if (triangles) MEMOFREE(triangles);
        return 0;
      }

      for (u32 i = 0; i < position_count; i++)
      {
        vertices[i].x = positions[i].x * scale.x;
        vertices[i].y = positions[i].y * scale.y;
        vertices[i].z = positions[i].z * scale.z;
      }

      for (u32 i = 0; i < triangle_count; i++)
      {
        triangles[i].i1 = indices[i * 3 + 0];
        triangles[i].i2 = indices[i * 3 + 1];
        triangles[i].i3 = indices[i * 3 + 2];
        triangles[i].materialIndex = 0;
        triangles[i].userData = 0;
      }

      JPH_MeshShapeSettings *settings = JPH_MeshShapeSettings_Create2(vertices, position_count,
                                                                      triangles, triangle_count);
      if (settings)
      {
        JPH_MeshShapeSettings_Sanitize(settings);
        shape = (JPH_Shape *)JPH_MeshShapeSettings_CreateShape(settings);
        JPH_ShapeSettings_Destroy((JPH_ShapeSettings *)settings);
      }

      MEMOFREE(vertices);
      MEMOFREE(triangles);
    }
  }
  else
  {
    JPH_Vec3 half_extents = {collider->size_x * 0.5f * scale.x,
                             collider->size_y * 0.5f * scale.y,
                             collider->size_z * 0.5f * scale.z};
    if (half_extents.x < 0.005f) half_extents.x = 0.005f;
    if (half_extents.y < 0.005f) half_extents.y = 0.005f;
    if (half_extents.z < 0.005f) half_extents.z = 0.005f;
    shape = (JPH_Shape *)JPH_BoxShape_Create(&half_extents, JPH_DEFAULT_CONVEX_RADIUS);
  }

  if (!shape)
    return 0;

  if (collider->offset_x != 0.0f || collider->offset_y != 0.0f || collider->offset_z != 0.0f)
  {
    JPH_Vec3 offset = {collider->offset_x, collider->offset_y, collider->offset_z};
    JPH_Shape *offset_shape = (JPH_Shape *)JPH_RotatedTranslatedShape_Create(&offset, 0, shape);
    if (offset_shape)
    {
      // The offset wrapper keeps a reference to the base shape.
      JPH_Shape_Destroy(shape);
      shape = offset_shape;
    }
  }

  return shape;
}

static b8 create_body_for_collider(EgnObject *object, EgnColliderData *collider)
{
  JPH_Shape *shape = create_collider_shape(object, collider);
  if (!shape)
  {
    EGNERROR("PHYSICS failed to create collider shape");
    return false;
  }

  EgnComponent *rigidbody_component = find_component(object, EGN_COMPONENT_TYPE_RIGIDBODY);
  EgnRigidbodyData *rigidbody = rigidbody_component ? &rigidbody_component->data.rigidbody : 0;
  i32 motion = rigidbody ? rigidbody->motion_type : EGN_PHYSICS_MOTION_STATIC;
  b8 is_2d = rigidbody ? rigidbody->is_2d : false;

  mat4 world;
  egn_object_get_world_matrix_internal(object, &world);
  vec3 position = vec3_create(world.data[12], world.data[13], world.data[14]);
  quat rotation = quat_from_mat4(world);

  JPH_RVec3 jolt_position = {(double)position.x, (double)position.y, (double)position.z};
  JPH_Quat jolt_rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
  JPH_MotionType jolt_motion = JPH_MotionType_Dynamic;
  if (motion == EGN_PHYSICS_MOTION_STATIC)
    jolt_motion = JPH_MotionType_Static;
  else if (motion == EGN_PHYSICS_MOTION_KINEMATIC)
    jolt_motion = JPH_MotionType_Kinematic;

  JPH_ObjectLayer layer = jolt_motion == JPH_MotionType_Static
                              ? EGN_PHYSICS_OBJECT_LAYER_NON_MOVING
                              : EGN_PHYSICS_OBJECT_LAYER_MOVING;

  JPH_BodyCreationSettings *settings = JPH_BodyCreationSettings_Create3(
      shape, &jolt_position, &jolt_rotation, jolt_motion, layer);
  if (!settings)
  {
    JPH_Shape_Destroy(shape);
    return false;
  }

  if (is_2d)
    JPH_BodyCreationSettings_SetAllowedDOFs(settings, JPH_AllowedDOFs_Plane2D);

  if (rigidbody)
  {
    JPH_BodyCreationSettings_SetFriction(settings, rigidbody->friction);
    JPH_BodyCreationSettings_SetRestitution(settings, rigidbody->restitution);
    JPH_BodyCreationSettings_SetLinearDamping(settings, rigidbody->linear_damping);
    JPH_BodyCreationSettings_SetAngularDamping(settings, rigidbody->angular_damping);
    JPH_BodyCreationSettings_SetGravityFactor(settings, rigidbody->gravity_factor);
  }

  if (jolt_motion == JPH_MotionType_Dynamic && rigidbody && rigidbody->mass > 0.0f)
  {
    JPH_MassProperties mass_properties;
    JPH_Shape_GetMassProperties(shape, &mass_properties);
    JPH_MassProperties_ScaleToMass(&mass_properties, rigidbody->mass);
    JPH_BodyCreationSettings_SetMassPropertiesOverride(settings, &mass_properties);
  }

  JPH_BodyID id = JPH_BodyInterface_CreateAndAddBody(physics.body_interface, settings,
                                                     JPH_Activation_Activate);
  JPH_BodyCreationSettings_Destroy(settings);
  JPH_Shape_Destroy(shape);

  if (id == 0xFFFFFFFFu || id == 0)
  {
    EGNERROR("PHYSICS failed to create body for '%s'", object->name);
    collider->body_created = false;
    return false;
  }

  collider->body_id = id;
  collider->body_created = true;
  collider->dirty = false;
  return true;
}

static void destroy_collider_body(EgnColliderData *collider)
{
  if (!collider->body_created || !physics.body_interface)
    return;

  JPH_BodyInterface_RemoveAndDestroyBody(physics.body_interface,
                                         (JPH_BodyID)collider->body_id);
  collider->body_created = false;
  collider->body_id = 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

b8 egn_physics_initialize(void)
{
  MEMOZERO(&physics, sizeof(physics));

  if (!JPH_Init())
  {
    EGNERROR("PHYSICS JPH_Init failed");
    return false;
  }

  JPH_SetTraceHandler(physics_trace);

  physics.job_system = JPH_JobSystemThreadPool_Create(0);
  if (!physics.job_system)
  {
    EGNERROR("PHYSICS failed to create the job system");
    JPH_Shutdown();
    return false;
  }

  physics.object_layer_pair_filter = JPH_ObjectLayerPairFilterTable_Create(2);
  JPH_ObjectLayerPairFilterTable_EnableCollision(physics.object_layer_pair_filter,
                                                 EGN_PHYSICS_OBJECT_LAYER_NON_MOVING,
                                                 EGN_PHYSICS_OBJECT_LAYER_MOVING);
  JPH_ObjectLayerPairFilterTable_EnableCollision(physics.object_layer_pair_filter,
                                                 EGN_PHYSICS_OBJECT_LAYER_MOVING,
                                                 EGN_PHYSICS_OBJECT_LAYER_NON_MOVING);
  JPH_ObjectLayerPairFilterTable_EnableCollision(physics.object_layer_pair_filter,
                                                 EGN_PHYSICS_OBJECT_LAYER_MOVING,
                                                 EGN_PHYSICS_OBJECT_LAYER_MOVING);

  physics.broad_phase_layer_interface = JPH_BroadPhaseLayerInterfaceTable_Create(2, 2);
  JPH_BroadPhaseLayerInterfaceTable_MapObjectToBroadPhaseLayer(
      physics.broad_phase_layer_interface, EGN_PHYSICS_OBJECT_LAYER_NON_MOVING, 0);
  JPH_BroadPhaseLayerInterfaceTable_MapObjectToBroadPhaseLayer(
      physics.broad_phase_layer_interface, EGN_PHYSICS_OBJECT_LAYER_MOVING, 1);

  physics.object_vs_broadphase_filter = JPH_ObjectVsBroadPhaseLayerFilterTable_Create(
      physics.broad_phase_layer_interface, 2, physics.object_layer_pair_filter, 2);

  JPH_PhysicsSystemSettings settings = {0};
  settings.maxBodies = 8192;
  settings.numBodyMutexes = 0;
  settings.maxBodyPairs = 16384;
  settings.maxContactConstraints = 8192;
  settings.broadPhaseLayerInterface = physics.broad_phase_layer_interface;
  settings.objectLayerPairFilter = physics.object_layer_pair_filter;
  settings.objectVsBroadPhaseLayerFilter = physics.object_vs_broadphase_filter;
  physics.system = JPH_PhysicsSystem_Create(&settings);
  if (!physics.system)
  {
    EGNERROR("PHYSICS failed to create the physics system");
    JPH_JobSystem_Destroy(physics.job_system);
    JPH_Shutdown();
    return false;
  }

  physics.body_interface = JPH_PhysicsSystem_GetBodyInterface(physics.system);
  physics.accumulator = 0.0;
  physics.initialized = true;

  JPH_Vec3 gravity = {0.0f, -9.81f, 0.0f};
  JPH_PhysicsSystem_SetGravity(physics.system, &gravity);
  JPH_PhysicsSystem_OptimizeBroadPhase(physics.system);

  EGNINFO("PHYSICS Jolt initialized");
  return true;
}

static void destroy_character(EgnCharacterData *data)
{
  if (data->character) JPH_CharacterBase_Destroy((JPH_CharacterBase *)data->character);
  data->character = 0;
  data->grounded = false;
  data->velocity_y = 0;
}

static b8 create_character(EgnComponent *component)
{
  EgnCharacterData *data = &component->data.character;
  JPH_Shape *shape = (JPH_Shape *)JPH_CapsuleShape_Create(data->height * 0.5f - data->radius, data->radius);
  if (!shape) return false;
  JPH_CharacterVirtualSettings settings = {0};
  JPH_CharacterVirtualSettings_Init(&settings);
  settings.base.shape = shape;
  settings.base.supportingVolume.normal = (JPH_Vec3){0, 1, 0};
  settings.base.supportingVolume.distance = -data->height * 0.5f + data->radius;
  vec3 position = egn_object_get_world_position(component->owner);
  data->plane_z = position.z;
  JPH_RVec3 p = {position.x, position.y, position.z};
  JPH_Quat rotation = {0, 0, 0, 1};
  data->character = JPH_CharacterVirtual_Create(&settings, &p, &rotation, component->owner->id, physics.system);
  JPH_Shape_Destroy(shape);
  return data->character != 0;
}

static void update_character(EgnComponent *component, f32 delta)
{
  EgnCharacterData *data = &component->data.character;
  JPH_CharacterVirtual *character = data->character;
  if (!character) return;
  if (data->jump_speed > 0 && data->grounded) {
    data->velocity_y = data->jump_speed;
    data->grounded = false;
  } else if (data->grounded && data->velocity_y < 0) {
    data->velocity_y = 0;
  }
  data->jump_speed = 0;
  data->velocity_y -= data->gravity * delta;
  JPH_Vec3 velocity = {data->velocity_x, data->velocity_y,
                       component->type == EGN_COMPONENT_TYPE_CHARACTER_2D ? 0 : data->velocity_z};
  JPH_CharacterVirtual_SetLinearVelocity(character, &velocity);
  JPH_CharacterVirtual_Update(character, delta, EGN_PHYSICS_OBJECT_LAYER_MOVING, physics.system, 0, 0);
  data->grounded = JPH_CharacterBase_IsSupported((JPH_CharacterBase *)character);
  if (data->grounded && data->velocity_y < 0) data->velocity_y = 0;
  JPH_RVec3 position;
  JPH_CharacterVirtual_GetPosition(character, &position);
  if (component->type == EGN_COMPONENT_TYPE_CHARACTER_2D) {
    position.z = data->plane_z;
    JPH_CharacterVirtual_SetPosition(character, &position);
  }
  vec3 local_position; quat local_rotation;
  world_to_local(component->owner, vec3_create(position.x, position.y, position.z),
                 egn_object_get_world_rotation(component->owner), &local_position, &local_rotation);
  component->owner->position = local_position;
  egn_object_mark_world_dirty(component->owner);
}

void egn_physics_shutdown(void)
{
  if (!physics.initialized)
    return;

  EgnScene *scene = egn_scene_get();
  if (scene)
  {
    for (u32 i = 0; i < scene->component_count; i++)
    {
      EgnComponent *component = scene->components[i];
      if (component && component->type == EGN_COMPONENT_TYPE_COLLIDER)
        destroy_collider_body(&component->data.collider);
      else if (component && (component->type == EGN_COMPONENT_TYPE_CHARACTER_2D || component->type == EGN_COMPONENT_TYPE_CHARACTER_3D))
        destroy_character(&component->data.character);
    }
  }

  JPH_PhysicsSystem_Destroy(physics.system);
  JPH_JobSystem_Destroy(physics.job_system);
  JPH_Shutdown();
  MEMOZERO(&physics, sizeof(physics));
}

static void sync_body_to_object(EgnObject *object, EgnColliderData *collider)
{
  JPH_RVec3 position;
  JPH_Quat rotation;
  JPH_BodyInterface_GetPositionAndRotation(physics.body_interface,
                                           (JPH_BodyID)collider->body_id,
                                           &position, &rotation);

  vec3 world_position = vec3_create((f32)position.x, (f32)position.y, (f32)position.z);
  quat world_rotation = (quat){(f32)rotation.x, (f32)rotation.y, (f32)rotation.z, (f32)rotation.w};

  vec3 local_position;
  quat local_rotation;
  world_to_local(object, world_position, world_rotation, &local_position, &local_rotation);

  object->position = local_position;
  object->rotation = quat_normalize(local_rotation);
  egn_object_mark_world_dirty(object);
}

static void sync_object_to_body(EgnObject *object, EgnColliderData *collider)
{
  mat4 world;
  egn_object_get_world_matrix_internal(object, &world);
  vec3 position = vec3_create(world.data[12], world.data[13], world.data[14]);
  quat rotation = quat_from_mat4(world);

  JPH_RVec3 jolt_position = {(double)position.x, (double)position.y, (double)position.z};
  JPH_Quat jolt_rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
  JPH_BodyInterface_SetPositionAndRotation(physics.body_interface,
                                           (JPH_BodyID)collider->body_id,
                                           &jolt_position, &jolt_rotation,
                                           JPH_Activation_Activate);
}

void egn_physics_update(f64 delta_time)
{
  if (!physics.initialized)
    return;

  EgnScene *scene = egn_scene_get();
  if (!scene)
    return;

  // Rebuild/create bodies whose components appeared or whose collider
  // configuration changed since the last frame.
  for (u32 i = 0; i < scene->component_count; i++)
  {
    EgnComponent *component = scene->components[i];
    if (!component || component->type != EGN_COMPONENT_TYPE_COLLIDER)
      continue;

    EgnColliderData *collider = &component->data.collider;
    if (!component->alive)
    {
      destroy_collider_body(collider);
      continue;
    }

    EgnObject *object = component->owner;
    if (!object || !object->alive || !object->active || !component->enabled)
      continue;

    if (collider->body_created && collider->dirty)
      destroy_collider_body(collider);
    if (!collider->body_created)
      create_body_for_collider(object, collider);
  }

  for (u32 i = 0; i < scene->component_count; ++i)
  {
    EgnComponent *component = scene->components[i];
    if (!component || (component->type != EGN_COMPONENT_TYPE_CHARACTER_2D && component->type != EGN_COMPONENT_TYPE_CHARACTER_3D)) continue;
    if (!component->alive || !component->enabled || !component->owner || !egn_object_is_active_in_hierarchy(component->owner))
      destroy_character(&component->data.character);
    else if (!component->data.character.character)
      create_character(component);
  }

  if (delta_time > 0.25)
    delta_time = 0.25;
  physics.accumulator += delta_time;

  u32 steps = 0;
  while (physics.accumulator >= EGN_PHYSICS_FIXED_STEP && steps < 6)
  {
    // Keep kinematic bodies in sync with their animated transforms.
    for (u32 i = 0; i < scene->component_count; i++)
    {
      EgnComponent *component = scene->components[i];
      if (!component || !component->alive || component->type != EGN_COMPONENT_TYPE_COLLIDER)
        continue;

      EgnColliderData *collider = &component->data.collider;
      if (!collider->body_created || !component->enabled || !component->owner || !component->owner->alive)
        continue;

      EgnComponent *rigidbody_component = find_component(component->owner, EGN_COMPONENT_TYPE_RIGIDBODY);
      if (rigidbody_component && rigidbody_component->data.rigidbody.motion_type == EGN_PHYSICS_MOTION_KINEMATIC)
        sync_object_to_body(component->owner, collider);
    }

    JPH_PhysicsSystem_Update(physics.system, (float)EGN_PHYSICS_FIXED_STEP, 1, physics.job_system);
    for (u32 i = 0; i < scene->component_count; ++i)
    {
      EgnComponent *component = scene->components[i];
      if (component && component->alive && component->enabled && component->owner &&
          egn_object_is_active_in_hierarchy(component->owner) &&
          (component->type == EGN_COMPONENT_TYPE_CHARACTER_2D || component->type == EGN_COMPONENT_TYPE_CHARACTER_3D))
        update_character(component, (f32)EGN_PHYSICS_FIXED_STEP);
    }
    physics.accumulator -= EGN_PHYSICS_FIXED_STEP;
    steps++;
  }

  // Push dynamic bodies back into the scene graph.
  for (u32 i = 0; i < scene->component_count; i++)
  {
    EgnComponent *component = scene->components[i];
    if (!component || !component->alive || !component->enabled ||
        component->type != EGN_COMPONENT_TYPE_COLLIDER)
      continue;

    EgnColliderData *collider = &component->data.collider;
    EgnObject *object = component->owner;
    if (!collider->body_created || !object || !object->alive || !object->active)
      continue;

    EgnComponent *rigidbody_component = find_component(object, EGN_COMPONENT_TYPE_RIGIDBODY);
    if (rigidbody_component && rigidbody_component->data.rigidbody.motion_type == EGN_PHYSICS_MOTION_DYNAMIC)
      sync_body_to_object(object, collider);
  }
}

// ---------------------------------------------------------------------------
// Rigidbody accessors used by component.c
// ---------------------------------------------------------------------------

static EgnColliderData *rigidbody_collider(void *rigidbody_handle)
{
  EgnComponent *component = egn_component_from_handle(rigidbody_handle);
  if (!component || component->type != EGN_COMPONENT_TYPE_RIGIDBODY || !component->owner)
    return 0;

  EgnComponent *collider_component = find_component(component->owner, EGN_COMPONENT_TYPE_COLLIDER);
  if (!collider_component || !collider_component->data.collider.body_created)
    return 0;

  return &collider_component->data.collider;
}

void egn_physics_apply_impulse(void *rigidbody, f32 x, f32 y, f32 z)
{
  if (!physics.initialized)
    return;
  EgnColliderData *collider = rigidbody_collider(rigidbody);
  if (!collider)
    return;

  JPH_Vec3 impulse = {x, y, z};
  JPH_BodyInterface_AddImpulse(physics.body_interface, (JPH_BodyID)collider->body_id, &impulse);
}

b8 egn_physics_get_linear_velocity(void *rigidbody, f32 *x, f32 *y, f32 *z)
{
  EgnColliderData *collider = rigidbody_collider(rigidbody);
  if (!collider)
    return false;

  JPH_Vec3 velocity = {0};
  JPH_BodyInterface_GetLinearVelocity(physics.body_interface, (JPH_BodyID)collider->body_id, &velocity);
  if (x) *x = velocity.x;
  if (y) *y = velocity.y;
  if (z) *z = velocity.z;
  return true;
}

void egn_physics_set_linear_velocity(void *rigidbody, f32 x, f32 y, f32 z)
{
  EgnColliderData *collider = rigidbody_collider(rigidbody);
  if (!collider)
    return;

  JPH_Vec3 velocity = {x, y, z};
  JPH_BodyInterface_SetLinearVelocity(physics.body_interface, (JPH_BodyID)collider->body_id, &velocity);
  JPH_BodyInterface_ActivateBody(physics.body_interface, (JPH_BodyID)collider->body_id);
}

b8 egn_physics_get_angular_velocity(void *rigidbody, f32 *x, f32 *y, f32 *z)
{
  EgnColliderData *collider = rigidbody_collider(rigidbody);
  if (!collider)
    return false;

  JPH_Vec3 velocity = {0};
  JPH_BodyInterface_GetAngularVelocity(physics.body_interface, (JPH_BodyID)collider->body_id, &velocity);
  if (x) *x = velocity.x;
  if (y) *y = velocity.y;
  if (z) *z = velocity.z;
  return true;
}

void egn_physics_set_angular_velocity(void *rigidbody, f32 x, f32 y, f32 z)
{
  EgnColliderData *collider = rigidbody_collider(rigidbody);
  if (!collider)
    return;

  JPH_Vec3 velocity = {x, y, z};
  JPH_BodyInterface_SetAngularVelocity(physics.body_interface, (JPH_BodyID)collider->body_id, &velocity);
  JPH_BodyInterface_ActivateBody(physics.body_interface, (JPH_BodyID)collider->body_id);
}

// Query results use stable scene IDs rather than pointers. Jolt can report
// multiple sub-shapes for one body; retain the closest result per object.
typedef struct EgnQueryCollector {
  EgnPhysicsHit *hits;
  u32 count;
  f32 distance;
} EgnQueryCollector;

static void query_add(EgnQueryCollector *query, JPH_BodyID body, f32 fraction)
{
  EgnScene *scene = egn_scene_get();
  for (u32 i = 0; i < scene->component_count; ++i)
  {
    EgnComponent *c = scene->components[i];
    if (!c || !c->alive || !c->enabled || c->type != EGN_COMPONENT_TYPE_COLLIDER ||
        !c->owner || !c->owner->alive || !egn_object_is_active_in_hierarchy(c->owner) ||
        !c->data.collider.body_created || c->data.collider.body_id != body)
      continue;
    u32 id = c->owner->id;
    f32 distance = fraction * query->distance;
    for (u32 j = 0; j < query->count; ++j)
      if (query->hits[j].object_id == id)
      {
        if (distance < query->hits[j].distance) query->hits[j].distance = distance;
        return;
      }
    query->hits[query->count++] = (EgnPhysicsHit){id, distance};
    return;
  }
}
static void ray_result(void *context, const JPH_RayCastResult *hit)
{
  query_add((EgnQueryCollector *)context, hit->bodyID, hit->fraction);
}
static void overlap_result(void *context, const JPH_CollideShapeResult *hit)
{
  query_add((EgnQueryCollector *)context, hit->bodyID2, 0);
}
static int hit_compare(const void *a, const void *b)
{
  f32 x = ((const EgnPhysicsHit *)a)->distance, y = ((const EgnPhysicsHit *)b)->distance;
  return (x > y) - (x < y);
}
static u32 query_finish(EgnQueryCollector *query, EgnPhysicsHit *out, u32 capacity)
{
  qsort(query->hits, query->count, sizeof(*query->hits), hit_compare);
  u32 copied = query->count < capacity ? query->count : capacity;
  if (out && copied) memcpy(out, query->hits, copied * sizeof(*out));
  u32 count = query->count;
  MEMOFREE(query->hits);
  return count;
}
static b8 query_begin(EgnQueryCollector *query)
{
  if (!physics.initialized || !egn_scene_get()->object_count) return false;
  query->hits = MEMOALLOC((u64)egn_scene_get()->object_count * sizeof(*query->hits), MEMO_SCENE);
  query->count = 0;
  return query->hits != 0;
}

u32 egn_physics_ray_cast(f32 x, f32 y, f32 z, f32 dx, f32 dy, f32 dz,
                         f32 distance, EgnPhysicsHit *out, u32 capacity)
{
  vec3 direction = vec3_create(dx, dy, dz);
  f32 length = vec3_length(direction);
  if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(length) || !isfinite(distance) || length <= 0 || distance <= 0 || (capacity && !out)) return 0;
  EgnQueryCollector collector = {0};
  if (!query_begin(&collector)) return 0;
  collector.distance = distance;
  JPH_RVec3 origin = {x, y, z};
  JPH_Vec3 delta = {dx / length * distance, dy / length * distance, dz / length * distance};
  JPH_RayCastSettings settings = {0};
  settings.treatConvexAsSolid = true;
  JPH_NarrowPhaseQuery_CastRay3(JPH_PhysicsSystem_GetNarrowPhaseQuery(physics.system),
                               &origin, &delta, &settings, JPH_CollisionCollectorType_AllHitSorted,
                               ray_result, &collector, 0, 0, 0, 0);
  return query_finish(&collector, out, capacity);
}

static u32 overlap_shape(JPH_Shape *shape, f32 x, f32 y, f32 z, b8 rotate_to_xy,
                         EgnPhysicsHit *out, u32 capacity)
{
  if (!shape) return 0;
  EgnQueryCollector collector = {0};
  if (!query_begin(&collector)) { JPH_Shape_Destroy(shape); return 0; }
  JPH_RVec3 origin = {x, y, z}, base = {0, 0, 0};
  JPH_RMatrix4x4 transform;
  if (rotate_to_xy) {
    JPH_Quat rotation = {0.70710678f, 0, 0, 0.70710678f};
    JPH_RMatrix4x4_RotationTranslation(&transform, &rotation, &origin);
  } else JPH_RMatrix4x4_Translation(&transform, &origin);
  JPH_Vec3 scale = {1, 1, 1};
  JPH_CollideShapeSettings settings;
  JPH_CollideShapeSettings_Init(&settings);
  JPH_NarrowPhaseQuery_CollideShape2(JPH_PhysicsSystem_GetNarrowPhaseQuery(physics.system),
                                    shape, &scale, &transform, &settings, &base,
                                    JPH_CollisionCollectorType_AllHit, overlap_result,
                                    &collector, 0, 0, 0, 0);
  JPH_Shape_Destroy(shape);
  return query_finish(&collector, out, capacity);
}

u32 egn_physics_overlap_sphere(f32 x, f32 y, f32 z, f32 radius,
                               EgnPhysicsHit *out, u32 capacity)
{
  if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(radius) || radius <= 0 || (capacity && !out)) return 0;
  return overlap_shape((JPH_Shape *)JPH_SphereShape_Create(radius), x, y, z, false, out, capacity);
}

u32 egn_physics_overlap_box(f32 x, f32 y, f32 z, f32 sx, f32 sy, f32 sz,
                            EgnPhysicsHit *out, u32 capacity)
{
  if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(sx) || !isfinite(sy) || !isfinite(sz) || sx <= 0 || sy <= 0 || sz <= 0 || (capacity && !out)) return 0;
  JPH_Vec3 half = {sx * 0.5f, sy * 0.5f, sz * 0.5f};
  return overlap_shape((JPH_Shape *)JPH_BoxShape_Create(&half, 0), x, y, z, false, out, capacity);
}

u32 egn_physics_overlap_circle_2d(f32 x, f32 y, f32 z, f32 radius, EgnPhysicsHit *out, u32 capacity)
{
  if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(radius) || radius <= 0 || (capacity && !out)) return 0;
  return overlap_shape((JPH_Shape *)JPH_CylinderShape_Create(0.001f, radius), x, y, z, true, out, capacity);
}
u32 egn_physics_overlap_box_2d(f32 x, f32 y, f32 z, f32 width, f32 height, EgnPhysicsHit *out, u32 capacity)
{
  return egn_physics_overlap_box(x, y, z, width, height, 0.002f, out, capacity);
}
