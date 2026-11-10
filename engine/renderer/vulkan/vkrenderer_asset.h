#pragma once

#include "vkrenderer_types.h"

// Asset loaders. Each returns a 1-based mesh handle, or 0 on failure.
EGNAPI u32 renderer_mesh_load_obj(const char *path);
EGNAPI u32 renderer_mesh_load_glb(const char *path);

// Frees a CPU mesh asset (positions/indices, skeleton and animations).
void egn_mesh_asset_destroy(EgnMeshAsset *asset);
