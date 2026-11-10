#include "scene/scene.h"
#include "physics/physics.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
  assert(egn_scene_initialize() && egn_physics_initialize());
  void *ground = egn_object_create(egn_scene_root(), "ground");
  egn_object_set_position(ground,0,-0.5,0);
  assert(egn_object_add_collider(ground,EGN_COLLIDER_BOX,20,1,20,0,0,0,0,0,true));
  void *player=egn_object_create(egn_scene_root(),"player");
  egn_object_set_position(player,0,3,0);
  void *character=egn_object_add_character(player,false,0.3,1.8,9.81,true);
  assert(character);
  for (int i=0;i<240;i++) egn_physics_update(1.0/60);
  float x,y,z; egn_object_get_position(player,&x,&y,&z);
  printf("position %f %f %f grounded %d\n",x,y,z,egn_character_is_grounded(character));
  assert(egn_character_is_grounded(character));
  assert(y > 0.85 && y < 1.1);
  egn_character_jump(character,5);
  for (int i=0;i<10;i++) egn_physics_update(1.0/60);
  egn_object_get_position(player,&x,&y,&z);
  assert(y > 1.1);
  void *player2 = egn_object_create(egn_scene_root(), "player2d");
  egn_object_set_position(player2, 0, 3, 2);
  void *character2 = egn_object_add_character(player2, true, 0.3, 1.8, 9.81, true);
  assert(character2);
  egn_character_set_velocity(character2, 1, 100);
  for (int i = 0; i < 90; ++i) egn_physics_update(1.0 / 60);
  egn_object_get_position(player2, &x, &y, &z);
  assert(x > 1 && z == 2 && egn_character_is_grounded(character2));
  egn_physics_shutdown(); egn_scene_shutdown();
}
