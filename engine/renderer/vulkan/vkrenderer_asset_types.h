#pragma once

#include "defines.h"
#include "platform/math.h"

#define EGN_MAX_JOINTS 128
#define EGN_ANIMATION_NAME_MAX 64

typedef enum EgnAnimationPath
{
  EGN_ANIMATION_PATH_TRANSLATION = 0,
  EGN_ANIMATION_PATH_ROTATION = 1,
  EGN_ANIMATION_PATH_SCALE = 2
} EgnAnimationPath;

typedef enum EgnAnimationInterpolation
{
  EGN_ANIMATION_INTERPOLATION_LINEAR = 0,
  EGN_ANIMATION_INTERPOLATION_STEP = 1,
  EGN_ANIMATION_INTERPOLATION_CUBICSPLINE = 2
} EgnAnimationInterpolation;

typedef struct EgnAnimationNode
{
  char name[EGN_ANIMATION_NAME_MAX];
  i32 parent; // -1 for a root node
  vec3 translation;
  quat rotation;
  vec3 scale;
} EgnAnimationNode;

typedef struct EgnSkin
{
  u32 joint_count;
  i32 *joints;        // node indices
  mat4 *inverse_bind; // one per joint
} EgnSkin;

typedef struct EgnAnimationChannel
{
  i32 node;
  i32 path;
  i32 interpolation;

  f32 *times;
  u32 time_count;

  f32 *values;
  u32 value_count; // components per key: 3 or 4
} EgnAnimationChannel;

typedef struct EgnAnimation
{
  char name[EGN_ANIMATION_NAME_MAX];
  f32 duration;
  u32 channel_count;
  EgnAnimationChannel *channels;
} EgnAnimation;

/**
 * CPU-side mesh data kept alongside the GPU mesh.
 *
 * positions/indices are the flattened collision representation. The node,
 * skin and animation arrays are only present for glTF/GLB assets.
 */
typedef struct EgnMeshAsset
{
  vec3 *positions;
  u32 position_count;
  u32 *indices;
  u32 index_count;

  u32 node_count;
  EgnAnimationNode *nodes;

  u32 skin_count;
  EgnSkin *skins;

  u32 animation_count;
  EgnAnimation *animations;

  b8 skinned;
  i32 skin_index; // skin used by the mesh vertices, or -1
} EgnMeshAsset;

void egn_mesh_asset_destroy(EgnMeshAsset *asset);
