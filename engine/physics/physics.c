#include "physics.h"
#include "core/logger.h"
#include "core/memo.h"
#include "platform/math.h"
#include "renderer/renderer.h"
#include "scene/scene_internal.h"

#include "joltc.h"

#include <stddef.h>
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
