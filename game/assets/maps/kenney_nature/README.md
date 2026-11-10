# Kenney Nature Kit (CC0)

Demo map assets used by `game/water_demo/`.

- Source: [Kenney Nature Kit](https://kenney.nl/assets/nature-kit)
- Mirror used for the GLB files: [shorepine/kenney](https://github.com/shorepine/kenney/tree/main/3d/nature)
- License: Creative Commons Zero (CC0), see `LICENSE.txt`

The files were patched only in the glTF scene root: UniGLTF exports scene nodes
as children of an off-scene `tmpParent`, which `cgltf` rejects. The patch changes
`scenes[0].nodes` to the root node so the geometry, materials and transforms stay
identical. No textures are required; the kit uses per-material base colours.

Assets included:

- `ground_grass.glb`
- `ground_riverBend.glb`, `ground_riverEnd.glb`, `ground_riverStraight.glb`,
  `ground_riverTile.glb` (available for future river layouts)
- `tree_default.glb`, `tree_pineDefaultA.glb`
- `rock_largeA.glb`, `rock_smallA.glb`
- `plant_bush.glb`, `lily_large.glb`
- `campfire_logs.glb`
- `bridge_wood.glb`, `bridge_side_wood.glb`
- `cliff_waterfall_rock.glb`, `cliff_waterfallTop_rock.glb`
