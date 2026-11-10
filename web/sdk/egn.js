import { WebGPURenderer } from "../../engine/renderer/webgpu/renderer.js";
import { parseOBJ } from "./obj.js";
import { parseGLB } from "./glb.js";
const encoder = new TextEncoder();
const decoder = new TextDecoder();
var Projection = /* @__PURE__ */ ((Projection2) => {
  Projection2[Projection2["Perspective"] = 0] = "Perspective";
  Projection2[Projection2["Orthographic"] = 1] = "Orthographic";
  return Projection2;
})(Projection || {});
function motionValue(motion) {
  return motion === "static" ? 0 : motion === "kinematic" ? 1 : 2;
}
function shapeValue(shape) {
  return shape === "sphere" ? 1 : shape === "mesh" ? 2 : 0;
}
function withString(module, text, fn) {
  const encoded = encoder.encode(`${text}\0`);
  const ptr = module._malloc(encoded.length);
  module.HEAPU8.set(encoded, ptr);
  try {
    return fn(ptr);
  } finally {
    module._free(ptr);
  }
}
function readCString(module, ptr) {
  if (!ptr) return "";
  const heap = module.HEAPU8;
  let end = ptr;
  while (end < heap.length && heap[end] !== 0) end++;
  return decoder.decode(heap.subarray(ptr, end));
}
class Mesh {
  constructor(id, data) {
    this.id = id;
    this.data = data;
  }
}
class Texture {
  constructor(id) {
    this.id = id;
  }
}
class Component {
  constructor(engine, handle) {
    this.engine = engine;
    this.handle = handle;
  }
  get valid() {
    return this.handle !== 0 && !!this.engine.module._egn_component_is_valid(this.handle);
  }
  setEnabled(enabled) {
    this.engine.module._egn_component_set_enabled(this.handle, enabled);
    return this;
  }
  destroy() {
    this.engine.module._egn_component_destroy(this.handle);
  }
}
class ModelComponent extends Component {
  setColor(color) {
    this.engine.module._egn_model_set_color(this.handle, color[0], color[1], color[2], color[3]);
    return this;
  }
  setTexture(texture) {
    this.engine.module._egn_model_set_texture(this.handle, texture.id);
    return this;
  }
  setMesh(mesh) {
    this.engine.module._egn_model_set_mesh(this.handle, mesh.id);
    return this;
  }
  setCastShadows(castShadows) {
    this.engine.module._egn_model_set_cast_shadows(this.handle, castShadows);
    return this;
  }
}
class CameraComponent extends Component {
  setFov(fov) {
    this.engine.module._egn_camera_set_fov(this.handle, fov);
    return this;
  }
  setViewport(x, y, width, height) {
    this.engine.module._egn_camera_set_viewport(this.handle, x, y, width, height);
    return this;
  }
  setLayerMask(mask) {
    this.engine.module._egn_camera_set_layer_mask(this.handle, mask);
    return this;
  }
  setClearColor(color) {
    this.engine.module._egn_camera_set_clear_color(this.handle, color[0], color[1], color[2], color[3]);
    return this;
  }
}
class LightComponent extends Component {
  setColor(color) {
    this.engine.module._egn_light_set_color(this.handle, color[0], color[1], color[2]);
    return this;
  }
  setIntensity(intensity) {
    this.engine.module._egn_light_set_intensity(this.handle, intensity);
    return this;
  }
  setRange(range) {
    this.engine.module._egn_light_set_range(this.handle, range);
    return this;
  }
}
class RigidbodyComponent extends Component {
  applyImpulse(x, y, z) {
    this.engine.module._egn_rigidbody_apply_impulse(this.handle, x, y, z);
  }
  setLinearVelocity(x, y, z) {
    this.engine.module._egn_rigidbody_set_linear_velocity(this.handle, x, y, z);
  }
  linearVelocity() {
    const m = this.engine.module;
    const ptr = m._malloc(12);
    m._egn_rigidbody_get_linear_velocity(this.handle, ptr, ptr + 4, ptr + 8);
    const out = [
      new Float32Array(m.HEAPF32.buffer, ptr, 1)[0],
      new Float32Array(m.HEAPF32.buffer, ptr + 4, 1)[0],
      new Float32Array(m.HEAPF32.buffer, ptr + 8, 1)[0]
    ];
    m._free(ptr);
    return out;
  }
}
class ColliderComponent extends Component {
}
class AnimationComponent extends Component {
  play() {
    this.engine.module._egn_animation_set_playing(this.handle, true);
    return this;
  }
  pause() {
    this.engine.module._egn_animation_set_playing(this.handle, false);
    return this;
  }
  setClip(clip) {
    this.engine.module._egn_animation_set_clip(this.handle, clip);
    return this;
  }
  setSpeed(speed) {
    this.engine.module._egn_animation_set_speed(this.handle, speed);
    return this;
  }
  setLoop(loop) {
    this.engine.module._egn_animation_set_loop(this.handle, loop);
    return this;
  }
}
class Object {
  constructor(engine, handle) {
    this.engine = engine;
    this.handle = handle;
  }
  get valid() {
    return this.handle !== 0 && !!this.engine.module._egn_object_is_valid(this.handle);
  }
  get name() {
    return readCString(this.engine.module, this.engine.module._egn_object_get_name(this.handle));
  }
  setName(name) {
    withString(this.engine.module, name, (ptr) => this.engine.module._egn_object_set_name(this.handle, ptr));
    return this;
  }
  setPosition(x, y, z) {
    this.engine.module._egn_object_set_position(this.handle, x, y, z);
    return this;
  }
  position() {
    const m = this.engine.module;
    const ptr = m._malloc(12);
    m._egn_object_get_position(this.handle, ptr, ptr + 4, ptr + 8);
    const heap = new Float32Array(m.HEAPF32.buffer, ptr, 3);
    const out = [heap[0], heap[1], heap[2]];
    m._free(ptr);
    return out;
  }
  setEuler(x, y, z) {
    this.engine.module._egn_object_set_rotation_euler(this.handle, x, y, z);
    return this;
  }
  setEulerDeg(x, y, z) {
    const d = Math.PI / 180;
    return this.setEuler(x * d, y * d, z * d);
  }
  // setLookAngles orienta uma camera livre a partir de pitch/yaw/roll em
  // radianos (ordem yaw * pitch * roll, igual ao SDK V).
  setLookAngles(pitch, yaw, roll = 0) {
    const halfPitch = pitch * 0.5;
    const halfYaw = yaw * 0.5;
    const halfRoll = roll * 0.5;
    const sx = Math.sin(halfPitch);
    const cx = Math.cos(halfPitch);
    const sy = Math.sin(halfYaw);
    const cy = Math.cos(halfYaw);
    const sz = Math.sin(halfRoll);
    const cz = Math.cos(halfRoll);
    const x1 = cy * sx;
    const y1 = sy * cx;
    const z1 = -sy * sx;
    const w1 = cy * cx;
    const x = x1 * cz + y1 * sz;
    const y = -x1 * sz + y1 * cz;
    const z = w1 * sz + z1 * cz;
    const w = w1 * cz - z1 * sz;
    this.engine.module._egn_object_set_rotation_quat(this.handle, x, y, z, w);
    return this;
  }
  setScale(x, y, z) {
    this.engine.module._egn_object_set_scale(this.handle, x, y, z);
    return this;
  }
  lookAt(x, y, z) {
    this.engine.module._egn_object_look_at(this.handle, x, y, z);
    return this;
  }
  setLayer(layer) {
    this.engine.module._egn_object_set_layer(this.handle, layer);
    return this;
  }
  get layer() {
    return this.engine.module._egn_object_get_layer(this.handle);
  }
  setActive(active) {
    this.engine.module._egn_object_set_active(this.handle, active);
    return this;
  }
  get active() {
    return !!this.engine.module._egn_object_is_active_in_hierarchy(this.handle);
  }
  createChild(name = "Object") {
    return this.engine.createObject(name, this);
  }
  children() {
    const m = this.engine.module;
    const count = m._egn_object_child_count(this.handle);
    const children = [];
    for (let i = 0; i < count; i++) {
      children.push(new Object(this.engine, m._egn_object_child_at(this.handle, i)));
    }
    return children;
  }
  find(predicate) {
    if (predicate(this)) return this;
    for (const child of this.children()) {
      const found = child.find(predicate);
      if (found) return found;
    }
    return null;
  }
  addModel(options) {
    const c = options.color ?? [1, 1, 1, 1];
    const handle = this.engine.module._egn_object_add_model(
      this.handle,
      options.mesh.id,
      options.texture?.id ?? 0,
      c[0],
      c[1],
      c[2],
      c[3],
      options.castShadows ?? true,
      options.enabled ?? true
    );
    return new ModelComponent(this.engine, handle);
  }
  addCamera(options = {}) {
    const viewport = options.viewport ?? [0, 0, 1, 1];
    const clear = options.clearColor ?? [0.02, 0.03, 0.07, 1];
    const handle = this.engine.module._egn_object_add_camera(
      this.handle,
      options.projection ?? 0 /* Perspective */,
      options.fov ?? 60,
      options.orthoSize ?? 10,
      options.near ?? 0.1,
      options.far ?? 1e3,
      viewport[0],
      viewport[1],
      viewport[2],
      viewport[3],
      options.layerMask ?? 4294967295,
      clear[0],
      clear[1],
      clear[2],
      clear[3],
      options.clearColorEnabled ?? true,
      options.clearDepthEnabled ?? true,
      options.order ?? 0,
      options.enabled ?? true
    );
    return new CameraComponent(this.engine, handle);
  }
  addLight(options = {}) {
    const color = options.color ?? [1, 1, 1];
    const handle = this.engine.module._egn_object_add_light(
      this.handle,
      options.type === "point" ? 1 : 0,
      color[0],
      color[1],
      color[2],
      options.intensity ?? 1,
      options.range ?? 10,
      options.castShadows ?? false,
      options.enabled ?? true
    );
    return new LightComponent(this.engine, handle);
  }
  addRigidbody(options = {}) {
    const m = this.engine.module;
    const handle = m._egn_object_add_rigidbody(
      this.handle,
      motionValue(options.motion ?? "dynamic"),
      options.is2D ?? false,
      options.gravityFactor ?? 1,
      options.friction ?? 0.5,
      options.restitution ?? 0,
      options.linearDamping ?? 0,
      options.angularDamping ?? 0,
      options.enabled ?? true
    );
    m._egn_rigidbody_set_mass(handle, options.mass ?? 1);
    return new RigidbodyComponent(this.engine, handle);
  }
  addCollider(options = {}) {
    const size = options.size ?? [1, 1, 1];
    const offset = options.offset ?? [0, 0, 0];
    const handle = this.engine.module._egn_object_add_collider(
      this.handle,
      shapeValue(options.shape ?? "box"),
      size[0],
      size[1],
      size[2],
      options.radius ?? 0.5,
      options.mesh?.id ?? 0,
      offset[0],
      offset[1],
      offset[2],
      options.enabled ?? true
    );
    return new ColliderComponent(this.engine, handle);
  }
  addAnimation(options) {
    const handle = this.engine.module._egn_object_add_animation(
      this.handle,
      options.clip,
      options.speed ?? 1,
      options.loop ?? true,
      options.playing ?? true,
      options.enabled ?? true
    );
    return new AnimationComponent(this.engine, handle);
  }
  destroy() {
    this.engine.module._egn_object_destroy(this.handle);
  }
}
class Scene {
  constructor(engine) {
    this.engine = engine;
    this.root = new Object(engine, engine.module._egn_scene_root());
  }
  root;
  createObject(name = "Object") {
    return this.engine.createObject(name, this.root);
  }
  find(name) {
    return this.root.find((object) => object.name === name);
  }
  findCamera() {
    return this.root.find((object) => !!this.engine.module._egn_object_get_component(object.handle, 0));
  }
  findDirectionalLight() {
    return this.root.find((object) => {
      const handle = this.engine.module._egn_object_get_component(object.handle, 4);
      return !!handle && this.engine.module._egn_light_get_type(handle) === 0;
    });
  }
}
class Input {
  constructor(canvas) {
    this.canvas = canvas;
    const add = (target, type, listener) => {
      target.addEventListener(type, listener);
      this.listeners.push([target, type, listener]);
    };
    add(window, "keydown", (event) => {
      const e = event;
      if (!e.repeat && !this.down.has(e.code)) {
        this.pressed.add(e.code);
      }
      this.down.add(e.code);
    });
    add(window, "keyup", (event) => {
      const e = event;
      this.down.delete(e.code);
      this.released.add(e.code);
    });
    add(window, "blur", () => {
      this.down.clear();
      this.mouseDown.clear();
    });
    add(canvas, "pointerdown", (event) => {
      const e = event;
      if (!this.mouseDown.has(e.button)) this.mousePressed.add(e.button);
      this.mouseDown.add(e.button);
    });
    add(window, "pointerup", (event) => {
      const e = event;
      this.mouseDown.delete(e.button);
      this.mouseReleased.add(e.button);
    });
    add(window, "pointermove", (event) => {
      const e = event;
      if (!this.pointerLocked) return;
      this.dx += e.movementX;
      this.dy += e.movementY;
    });
    add(document, "pointerlockchange", () => {
      this.pointerLocked = document.pointerLockElement === canvas;
      if (!this.pointerLocked) {
        this.dx = 0;
        this.dy = 0;
      }
    });
    add(canvas, "contextmenu", (event) => event.preventDefault());
  }
  down = /* @__PURE__ */ new Set();
  pressed = /* @__PURE__ */ new Set();
  released = /* @__PURE__ */ new Set();
  mouseDown = /* @__PURE__ */ new Set();
  mousePressed = /* @__PURE__ */ new Set();
  mouseReleased = /* @__PURE__ */ new Set();
  dx = 0;
  dy = 0;
  pointerLocked = false;
  listeners = [];
  isDown(code) {
    return this.down.has(code);
  }
  isPressed(code) {
    return this.pressed.has(code);
  }
  isReleased(code) {
    return this.released.has(code);
  }
  isMouseDown(button = 0) {
    return this.mouseDown.has(button);
  }
  isMousePressed(button = 0) {
    return this.mousePressed.has(button);
  }
  isMouseReleased(button = 0) {
    return this.mouseReleased.has(button);
  }
  mouseDelta() {
    return [this.dx, this.dy];
  }
  requestPointerLock() {
    void this.canvas.requestPointerLock();
  }
  exitPointerLock() {
    document.exitPointerLock();
  }
  endFrame() {
    this.pressed.clear();
    this.released.clear();
    this.mousePressed.clear();
    this.mouseReleased.clear();
    this.dx = 0;
    this.dy = 0;
  }
  dispose() {
    for (const [target, type, listener] of this.listeners) {
      target.removeEventListener(type, listener);
    }
    this.listeners = [];
  }
}
class Engine {
  constructor(module, canvas, device, renderer) {
    this.module = module;
    this.canvas = canvas;
    this.device = device;
    this.renderer = renderer;
    this.input = new Input(canvas);
    this.scene = new Scene(this);
  }
  scene;
  input;
  renderer;
  canvas;
  device;
  nextMeshId = 100;
  nextTextureId = 1;
  raf = 0;
  lastTime = 0;
  static async create(options) {
    const webgpuHelp = "WebGPU não está disponível. Verifique chrome://gpu e, no Linux, " +
      "inicie o Chromium com --enable-unsafe-webgpu --enable-features=Vulkan " +
      "--use-angle=vulkan --ignore-gpu-blocklist. " +
      "Também é necessário um contexto seguro (localhost/HTTPS).";
    if (!navigator.gpu) {
      throw new Error(webgpuHelp);
    }
    const module = await options.moduleFactory();
    if (!module._egn_web_init()) {
      throw new Error("failed to initialize the EGN wasm core");
    }
    const preference = options.powerPreference ?? "high-performance";
    const attempts = [
      { powerPreference: preference },
      { powerPreference: "low-power" },
      {},
      { forceFallbackAdapter: true },
      { featureLevel: "compatibility" },
    ];
    let adapter = null;
    for (const request of attempts) {
      try {
        adapter = await navigator.gpu.requestAdapter(request);
      } catch {
        adapter = null;
      }
      if (adapter) break;
    }
    if (!adapter) {
      module._egn_web_shutdown();
      throw new Error(webgpuHelp);
    }
    const device = await adapter.requestDevice();
    const context = options.canvas.getContext("webgpu");
    if (!context) {
      module._egn_web_shutdown();
      throw new Error("could not create a WebGPU canvas context");
    }
    const format = navigator.gpu.getPreferredCanvasFormat();
    context.configure({ device, format, alphaMode: "opaque" });
    const renderer = new WebGPURenderer(options.canvas, device, context, format, module);
    return new Engine(module, options.canvas, device, renderer);
  }
  get stats() {
    return this.renderer.stats;
  }
  setAmbient(r, g, b, intensity = 1) {
    this.renderer.ambient = [r, g, b, intensity];
  }
  meshCube() {
    return new Mesh(1);
  }
  meshSphere() {
    return new Mesh(2);
  }
  meshQuad() {
    return new Mesh(3);
  }
  createObject(name = "Object", parent) {
    const parentHandle = parent?.handle ?? this.scene.root.handle;
    const handle = withString(this.module, name, (ptr) => this.module._egn_object_create(parentHandle, ptr));
    return new Object(this, handle);
  }
  async loadTexture(url) {
    const id = this.nextTextureId++;
    await this.renderer.registerTexture(id, url);
    return new Texture(id);
  }
  async loadGLB(url) {
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`failed to load GLB '${url}': ${response.status}`);
    }
    const parsed = parseGLB(await response.arrayBuffer(), this.renderer.math);
    const id = this.nextMeshId++;
    this.renderer.registerMesh(id, parsed.data);
    this.registerCollisionMesh(id, parsed.data);
    let texture;
    if (parsed.image) {
      const textureId = this.nextTextureId++;
      await this.renderer.registerTextureFromBlob(textureId, parsed.image);
      texture = new Texture(textureId);
    }
    return { mesh: new Mesh(id, parsed.data), texture };
  }
  async loadOBJ(url) {
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`failed to load OBJ '${url}': ${response.status}`);
    }
    const data = parseOBJ(await response.text());
    const id = this.nextMeshId++;
    this.renderer.registerMesh(id, data);
    this.registerCollisionMesh(id, data);
    return new Mesh(id, data);
  }
  registerMesh(data, id = this.nextMeshId++) {
    this.renderer.registerMesh(id, data);
    this.registerCollisionMesh(id, data);
    return new Mesh(id, data);
  }
  registerCollisionMesh(id, data) {
    const positions = data.positions;
    const indices = new Uint32Array(positions.length / 3);
    for (let i = 0; i < indices.length; i++) indices[i] = i;
    const posPtr = this.module._malloc(positions.byteLength);
    const idxPtr = this.module._malloc(indices.byteLength);
    this.module.HEAPF32.set(positions, posPtr / 4);
    this.module.HEAPU8.set(new Uint8Array(indices.buffer), idxPtr);
    this.module._egn_web_register_mesh(id, posPtr, positions.length / 3, idxPtr, indices.length);
    this.module._free(posPtr);
    this.module._free(idxPtr);
  }
  tick(dt) {
    this.module._egn_web_update(dt);
    this.renderer.render();
    this.input.endFrame();
  }
  run(update) {
    this.stop();
    this.lastTime = performance.now();
    const loop = (time) => {
      const dt = Math.min(0.1, (time - this.lastTime) / 1e3);
      this.lastTime = time;
      update?.(dt);
      this.tick(dt);
      this.raf = requestAnimationFrame(loop);
    };
    this.raf = requestAnimationFrame(loop);
  }
  stop() {
    if (this.raf) {
      cancelAnimationFrame(this.raf);
      this.raf = 0;
    }
  }
  destroy() {
    this.stop();
    this.input.dispose();
    this.renderer.destroy();
    this.module._egn_web_shutdown();
  }
}
import { WebGPURenderer as WebGPURenderer2 } from "../../engine/renderer/webgpu/renderer.js";
export {
  AnimationComponent,
  CameraComponent,
  ColliderComponent,
  Component,
  Engine,
  Input,
  LightComponent,
  Mesh,
  ModelComponent,
  Object,
  Projection,
  RigidbodyComponent,
  Scene,
  Texture,
  WebGPURenderer2 as WebGPURenderer
};
