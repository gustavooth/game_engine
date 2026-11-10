#include "scene/scene.h"
#include "physics/physics.h"
#include "core/memo.h"
#include <assert.h>
#include <math.h>
int main(void) {
  void *bootstrap = MEMOZALLOC(32, MEMO_NOT_INITIALIZED);
  assert(bootstrap); MEMOFREE(bootstrap);
  assert(egn_scene_initialize());
  assert(egn_object_child_count(egn_scene_root()) == 0);
  assert(egn_physics_initialize());
  void *object = egn_object_create(egn_scene_root(), "box");
  egn_object_set_position(object, 0, 0, -5);
  assert(egn_object_add_collider(object, EGN_COLLIDER_BOX, 2, 2, 2, 0, 0, 0, 0, 0, true));
  egn_physics_update(1.0 / 60.0);
  EgnPhysicsHit hit[2];
  void *cameraObject = egn_object_create(egn_scene_root(), "camera");
  void *camera = egn_object_add_camera(cameraObject, EGN_PROJECTION_PERSPECTIVE,
      60, 10, 0.1f, 100, 0, 0, 1, 1, 0xffffffffu, 0, 0, 0, 1, true, true, 0, true);
  assert(camera);
  void *camera2 = egn_object_add_camera_2d(cameraObject, 12, 0x1234u, 1, true);
  void *canvas = egn_object_add_canvas(cameraObject, 0x5678u, 2, true);
  assert(camera2 && canvas);
  assert(egn_component_get_type(camera2) == EGN_COMPONENT_TYPE_CAMERA_2D);
  assert(egn_component_get_type(canvas) == EGN_COMPONENT_TYPE_CANVAS);
  assert(egn_camera_get_layer_mask(camera2) == 0x1234u);
  egn_object_set_render_domain(object, EGN_RENDER_CANVAS);
  egn_object_set_canvas_anchor(object, EGN_ANCHOR_TOP_RIGHT);
  assert(egn_object_get_render_domain(object) == EGN_RENDER_CANVAS);
  assert(egn_object_get_canvas_anchor(object) == EGN_ANCHOR_TOP_RIGHT);
  assert(egn_camera_ray_cast(camera, 400, 300, 800, 600, 10, hit, 2) == 1);
  assert(egn_camera_ray_cast(camera, 0, 0, 800, 600, 10, hit, 2) == 0);
  assert(egn_physics_ray_cast(0,0,0,0,0,-1,10,hit,2) == 1);
  assert(hit[0].object_id == egn_object_get_id(object));
  assert(fabsf(hit[0].distance - 4) < 0.1);
  assert(egn_physics_overlap_sphere(0,0,-5,0.5,hit,2) == 1);
  assert(egn_physics_overlap_sphere(0,0,0,0.5,hit,2) == 0);
  assert(egn_physics_ray_cast(0,0,0,0,0,-1,10,0,0) == 1);
  assert(egn_physics_overlap_box(0,0,-5,1,1,1,hit,2) == 1);
  assert(egn_physics_overlap_circle_2d(0,0,-5,0.5,hit,2) == 1);
  assert(egn_physics_overlap_box_2d(0,0,-5,1,1,hit,2) == 1);
  assert(egn_physics_overlap_circle_2d(0,0,0,0.5,hit,2) == 0);
  assert(egn_physics_overlap_box(0,0,0,1,1,1,hit,2) == 0);
  assert(egn_scene_find_object_by_id(hit[0].object_id) == object);
  u32 id = egn_object_get_id(object);
  egn_object_destroy(object);
  assert(!egn_scene_find_object_by_id(id));
  egn_physics_shutdown(); egn_scene_shutdown();
  return 0;
}
