# EGN Web SDK (WASM + WebGPU)

This directory contains the browser build of EGN:

- `engine/renderer/webgpu/`: the WebGPU renderer and WGSL shaders;
- `web/sdk/`: the TypeScript SDK, its generated JavaScript bundle (`egn.js`)
  and declarations (`egn.d.ts`);
- `web/demo/`: the demo page and its TypeScript entry point;
- `engine/platform/web/`: the Emscripten platform and the C bridge exposed to
  JavaScript.

The C scene graph, component logic and Jolt physics are compiled to
WebAssembly. WebGPU is driven by the TypeScript renderer, which reads the
wasm scene through a small, typed bridge.

## Build

```sh
make emsdk     # first time only
make web       # builds wasm, SDK and demo in web/dist
make web-run   # serves web/dist on http://localhost:8091
```

`make web` requires Node.js/npm and the Emscripten SDK. The first run also
builds JoltC/Jolt for wasm (`make jolt-web`).

## JavaScript SDK

```js
import createEgnModule from './engine.js';
import { Engine } from './egn.js';

const engine = await Engine.create({
  canvas: document.querySelector('canvas'),
  moduleFactory: createEgnModule,
});

const scene = engine.scene;
const object = scene.createObject('Cube');
object.setPosition(0, 1, 0);
object.addModel({ mesh: engine.meshCube(), color: [0.2, 0.7, 1, 1] });

engine.run((dt) => {
  object.setEuler(0, performance.now() * 0.0004, 0);
});
```

The generated `egn.js` is an ES module. `egn.d.ts` contains the public
TypeScript API, and the source of truth is `web/sdk/egn.ts`.

## TypeScript SDK

Import the source directly in a Vite/esbuild project:

```ts
import createEgnModule from './engine.js';
import { Engine, Projection } from './sdk/egn';
```

The SDK exports:

- `Engine`, `Scene`, `Object`, `Input`;
- `Mesh`, `Texture` and `loadOBJ` / `loadGLB` helpers;
- components: `ModelComponent`, `CameraComponent`, `LightComponent`,
  `RigidbodyComponent`, `ColliderComponent`, `AnimationComponent`;
- helpers and types for colors, vectors, colliders and rigidbodies.

The WebGPU renderer is available at `engine/renderer/webgpu/renderer.ts`.

## Scope

The web renderer implements forward rendering with:

- directional sun with a 2D shadow map (PCF);
- up to 8 point lights;
- vertex-colour/texture models, primitives, OBJ and static GLB meshes;
- multiple cameras with viewport/layer masks;
- WASM-driven Jolt physics (box, sphere and mesh colliders);
- a typed JS/TS SDK and an FPS-style input controller.

Skeletal animation is currently evaluated by the native Vulkan renderer. The
web renderer loads the GLB bind pose; a GPU skinning backend can share the
same `Animation` component data in a future iteration.
