#include "scene/scene.h"
#include <assert.h>
#include <math.h>

int main(void)
{
  assert(egn_scene_initialize());
  void *object = egn_object_create(egn_scene_root(), "material");
  void *model = egn_object_add_model(object, 0, 0, 1, 1, 1, 1, true, true);
  assert(model);
  float metallic, roughness;
  egn_model_get_pbr(model, &metallic, &roughness);
  assert(metallic == 0 && roughness == 1);
  assert(egn_model_set_pbr(model, 0.75f, 0.25f));
  assert(!egn_model_set_pbr(model, NAN, 0.5f));
  assert(!egn_model_set_pbr(model, 0.5f, 2.0f));
  egn_model_get_pbr(model, &metallic, &roughness);
  assert(metallic == 0.75f && roughness == 0.25f);
  assert(!egn_model_set_pbr(0, 0, 1));

  assert(!egn_model_get_water(model));
  assert(egn_model_set_water(model, true));
  assert(egn_model_get_water(model));
  assert(egn_model_set_water(model, false));
  assert(!egn_model_get_water(model));
  assert(!egn_model_set_water(0, true));
  assert(!egn_model_get_water(0));

  egn_scene_shutdown();
  return 0;
}
