# EGN Web SDK (WASM + WebGPU)

This directory contains the browser build of EGN:

- `engine/renderer/webgpu/`: the WebGPU renderer, WGSL shader modules and
  engine-side asset parsers (`loaders/obj.js`, `loaders/glb.js`);
- `web/sdk/`: the thin JavaScript ESM bridge (`egn.js` only);
- `web/demo/`: the demo page and its JavaScript entry point;
- `engine/platform/web/`: the Emscripten platform and the C bridge exposed to
  JavaScript.

The C scene graph, component logic and Jolt physics are compiled to
WebAssembly. WebGPU is driven by the JavaScript renderer, which reads the wasm
scene through a small bridge. The SDK only exposes the engine interface and
component/asset helpers; OBJ/GLB parsing lives with the engine renderer, not in
`web/sdk`. The math used by the renderer and loaders is
imported from the engine's C math library compiled into the wasm module.

## Build

```sh
make emsdk     # first time only
make naga      # installs wgpu/naga for WGSL validation
make web       # builds wasm, SDK and demo in output/web
make run-web   # opens output/web/index.html with Chromium WebGPU flags
```

No Node.js, npm or bundler is required. Web shaders are authored in WGSL and
validated/canonicalized by **naga** (from the wgpu project) during `make web`.
WebGPU consumes WGSL directly; native Vulkan continues to use GLSL -> SPIR-V.
`make web` copies the native ES modules directly into `output/web`. The first run also builds JoltC/Jolt for wasm
(`make jolt-web`). `make run-web` only opens the generated HTML file (no HTTP
server); it launches Chromium with file-access and WebGPU flags. Override
`WEB_BROWSER=/path/to/chrome` if needed.

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
  `RigidbodyComponent`, `ColliderComponent`, `AnimationComponent`,
  `ParticlesComponent`;
- helpers and types for colors, vectors, colliders and rigidbodies.

The WebGPU renderer is available at `engine/renderer/webgpu/renderer.js`.

## Scope

The web renderer implements forward rendering with:

- directional sun with a 2D shadow map (PCF);
- up to 8 point lights;
- vertex-colour/texture models, primitives, OBJ and skinned GLB meshes;
- skeletal animation clips evaluated from the engine `Animation` component;
- CPU-emitted, camera-facing particle billboards with lifetime, gravity,
  drag, spread, colour/size over life and texture masks;
- multiple cameras with viewport/layer masks;
- WASM-driven Jolt physics (box, sphere and mesh colliders);
- a thin JavaScript SDK and an FPS-style input controller;
- math operations imported from the wasm core (`engine/math/math.c`).
