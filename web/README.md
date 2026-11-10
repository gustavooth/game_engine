# EGN Web SDK (WASM + WebGPU)

This directory contains the browser build of EGN:

- `engine/renderer/webgpu/`: the WebGPU renderer and embedded WGSL shader modules;
- `web/sdk/`: the JavaScript ESM SDK (`egn.js`, `obj.js`, `glb.js`);
- `web/demo/`: the demo page and its JavaScript entry point;
- `engine/platform/web/`: the Emscripten platform and the C bridge exposed to
  JavaScript.

The C scene graph, component logic and Jolt physics are compiled to
WebAssembly. WebGPU is driven by the JavaScript renderer, which reads the wasm
scene through a small bridge. The math used by the renderer and loaders is
imported from the engine's C math library compiled into the wasm module.

## Build

```sh
make emsdk     # first time only
make web       # builds wasm, SDK and demo in web/dist
make run-web   # serves web/dist on http://localhost:8091
```

No Node.js, npm or bundler is required. `make web` copies the native ES modules
directly into `web/dist`. The first run also builds JoltC/Jolt for wasm
(`make jolt-web`).

## JavaScript SDK

`web/sdk/egn.js` is a native ES module. The SDK has no build step; the files in
`web/sdk` are the public sources.

```js
import createEgnModule from './engine.js';
import * as EGN from './web/sdk/egn.js';

const engine = await EGN.Engine.create({
  canvas: document.querySelector('canvas'),
  moduleFactory: createEgnModule,
});

const scene = engine.scene;
const object = scene.createObject('Cube');
object.setPosition(0, 1, 0);
object.addModel({ mesh: engine.meshCube(), color: [0.2, 0.7, 1, 1] });

engine.run(() => {
  object.setEuler(0, performance.now() * 0.0004, 0);
});
```

The SDK exports:

- `Engine`, `Scene`, `Object`, `Input`;
- `Mesh`, `Texture` and `loadOBJ` / `loadGLB` helpers;
- components: `ModelComponent`, `CameraComponent`, `LightComponent`,
  `RigidbodyComponent`, `ColliderComponent`, `AnimationComponent`;
- helpers and types for colors, vectors, colliders and rigidbodies.

The WebGPU renderer is available at `engine/renderer/webgpu/renderer.js`.

## Scope

The web renderer implements forward rendering with:

- directional sun with a 2D shadow map (PCF);
- up to 8 point lights;
- vertex-colour/texture models, primitives, OBJ and static GLB meshes;
- multiple cameras with viewport/layer masks;
- WASM-driven Jolt physics (box, sphere and mesh colliders);
- a JavaScript SDK and an FPS-style input controller.
- math operations imported from the wasm core (`engine/math/math.c`).

Skeletal animation is currently evaluated by the native Vulkan renderer. The
web renderer loads the GLB bind pose; a GPU skinning backend can share the
same `Animation` component data in a future iteration.
