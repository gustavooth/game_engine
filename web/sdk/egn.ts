import { WebGPURenderer, type EgnWasm, type MeshData, type RendererStats } from '../../engine/renderer/webgpu/renderer';
import { parseOBJ } from './obj';
import { parseGLB } from './glb';

export type { EgnWasm, MeshData, RendererStats } from '../../engine/renderer/webgpu/renderer';
export type EgnModuleFactory = () => Promise<EgnWasm>;

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export enum Projection {
  Perspective = 0,
  Orthographic = 1,
}

export type LightKind = 'directional' | 'point';
export type MotionType = 'static' | 'kinematic' | 'dynamic';
export type ColliderShape = 'box' | 'sphere' | 'mesh';

export interface EngineOptions {
  canvas: HTMLCanvasElement;
  moduleFactory: EgnModuleFactory;
  powerPreference?: GPUPowerPreference;
}

export interface Vec3Like {
  x: number;
  y: number;
  z: number;
}

export type Vec3Tuple = [number, number, number];
export type ColorTuple = [number, number, number, number];

export interface ModelOptions {
  mesh: Mesh;
  texture?: Texture;
  color?: ColorTuple;
  castShadows?: boolean;
  enabled?: boolean;
}

export interface CameraOptions {
  projection?: Projection;
  fov?: number;
  orthoSize?: number;
  near?: number;
  far?: number;
  viewport?: [number, number, number, number];
  layerMask?: number;
  clearColor?: ColorTuple;
  clearColorEnabled?: boolean;
  clearDepthEnabled?: boolean;
  order?: number;
  enabled?: boolean;
}

export interface LightOptions {
  type?: LightKind;
  color?: [number, number, number];
  intensity?: number;
  range?: number;
  castShadows?: boolean;
  enabled?: boolean;
}

export interface RigidbodyOptions {
  motion?: MotionType;
  is2D?: boolean;
  gravityFactor?: number;
  mass?: number;
  friction?: number;
  restitution?: number;
  linearDamping?: number;
  angularDamping?: number;
  enabled?: boolean;
}

export interface ColliderOptions {
  shape?: ColliderShape;
  size?: Vec3Tuple;
  radius?: number;
  mesh?: Mesh;
  offset?: Vec3Tuple;
  enabled?: boolean;
}

export interface AnimationOptions {
  clip: number;
  speed?: number;
  loop?: boolean;
  playing?: boolean;
  enabled?: boolean;
}

function motionValue(motion: MotionType): number {
  return motion === 'static' ? 0 : motion === 'kinematic' ? 1 : 2;
}

function shapeValue(shape: ColliderShape): number {
  return shape === 'sphere' ? 1 : shape === 'mesh' ? 2 : 0;
}

function withString<T>(module: EgnWasm, text: string, fn: (ptr: number) => T): T {
  const encoded = encoder.encode(`${text}\0`);
  const ptr = module._malloc(encoded.length);
  module.HEAPU8.set(encoded, ptr);
  try {
    return fn(ptr);
  } finally {
    module._free(ptr);
  }
}

function readCString(module: EgnWasm, ptr: number): string {
  if (!ptr) return '';
  const heap = module.HEAPU8;
  let end = ptr;
  while (end < heap.length && heap[end] !== 0) end++;
  return decoder.decode(heap.subarray(ptr, end));
}

export class Mesh {
  constructor(public readonly id: number, public readonly data?: MeshData) {}
}

export class Texture {
  constructor(public readonly id: number) {}
}

export class Component {
  constructor(
    protected readonly engine: Engine,
    public readonly handle: number,
  ) {}

  get valid(): boolean {
    return this.handle !== 0 && !!this.engine.module._egn_component_is_valid(this.handle);
  }

  setEnabled(enabled: boolean): this {
    this.engine.module._egn_component_set_enabled(this.handle, enabled);
    return this;
  }

  destroy(): void {
    this.engine.module._egn_component_destroy(this.handle);
  }
}

export class ModelComponent extends Component {
  setColor(color: ColorTuple): this {
    this.engine.module._egn_model_set_color(this.handle, color[0], color[1], color[2], color[3]);
    return this;
  }

  setTexture(texture: Texture): this {
    this.engine.module._egn_model_set_texture(this.handle, texture.id);
    return this;
  }

  setMesh(mesh: Mesh): this {
    this.engine.module._egn_model_set_mesh(this.handle, mesh.id);
    return this;
  }

  setCastShadows(castShadows: boolean): this {
    this.engine.module._egn_model_set_cast_shadows(this.handle, castShadows);
    return this;
  }
}

export class CameraComponent extends Component {
  setFov(fov: number): this {
    this.engine.module._egn_camera_set_fov(this.handle, fov);
    return this;
  }

  setViewport(x: number, y: number, width: number, height: number): this {
    this.engine.module._egn_camera_set_viewport(this.handle, x, y, width, height);
    return this;
  }

  setLayerMask(mask: number): this {
    this.engine.module._egn_camera_set_layer_mask(this.handle, mask);
    return this;
  }

  setClearColor(color: ColorTuple): this {
    this.engine.module._egn_camera_set_clear_color(this.handle, color[0], color[1], color[2], color[3]);
    return this;
  }
}

export class LightComponent extends Component {
  setColor(color: [number, number, number]): this {
    this.engine.module._egn_light_set_color(this.handle, color[0], color[1], color[2]);
    return this;
  }

  setIntensity(intensity: number): this {
    this.engine.module._egn_light_set_intensity(this.handle, intensity);
    return this;
  }

  setRange(range: number): this {
    this.engine.module._egn_light_set_range(this.handle, range);
    return this;
  }
}

export class RigidbodyComponent extends Component {
  applyImpulse(x: number, y: number, z: number): void {
    this.engine.module._egn_rigidbody_apply_impulse(this.handle, x, y, z);
  }

  setLinearVelocity(x: number, y: number, z: number): void {
    this.engine.module._egn_rigidbody_set_linear_velocity(this.handle, x, y, z);
  }

  linearVelocity(): Vec3Tuple {
    const m = this.engine.module;
    const ptr = m._malloc(12);
    m._egn_rigidbody_get_linear_velocity(this.handle, ptr, ptr + 4, ptr + 8);
    const out: Vec3Tuple = [
      new Float32Array(m.HEAPF32.buffer, ptr, 1)[0],
      new Float32Array(m.HEAPF32.buffer, ptr + 4, 1)[0],
      new Float32Array(m.HEAPF32.buffer, ptr + 8, 1)[0],
    ];
    m._free(ptr);
    return out;
  }
}

export class ColliderComponent extends Component {}

export class AnimationComponent extends Component {
  play(): this {
    this.engine.module._egn_animation_set_playing(this.handle, true);
    return this;
  }

  pause(): this {
    this.engine.module._egn_animation_set_playing(this.handle, false);
    return this;
  }

  setClip(clip: number): this {
    this.engine.module._egn_animation_set_clip(this.handle, clip);
    return this;
  }

  setSpeed(speed: number): this {
    this.engine.module._egn_animation_set_speed(this.handle, speed);
    return this;
  }

  setLoop(loop: boolean): this {
    this.engine.module._egn_animation_set_loop(this.handle, loop);
    return this;
  }
}

export class Object {
  constructor(
    readonly engine: Engine,
    public readonly handle: number,
  ) {}

  get valid(): boolean {
    return this.handle !== 0 && !!this.engine.module._egn_object_is_valid(this.handle);
  }

  get name(): string {
    return readCString(this.engine.module, this.engine.module._egn_object_get_name(this.handle));
  }

  setName(name: string): this {
    withString(this.engine.module, name, (ptr) => this.engine.module._egn_object_set_name(this.handle, ptr));
    return this;
  }

  setPosition(x: number, y: number, z: number): this {
    this.engine.module._egn_object_set_position(this.handle, x, y, z);
    return this;
  }

  position(): Vec3Tuple {
    const m = this.engine.module;
    const ptr = m._malloc(12);
    m._egn_object_get_position(this.handle, ptr, ptr + 4, ptr + 8);
    const heap = new Float32Array(m.HEAPF32.buffer, ptr, 3);
    const out: Vec3Tuple = [heap[0], heap[1], heap[2]];
    m._free(ptr);
    return out;
  }

  setEuler(x: number, y: number, z: number): this {
    this.engine.module._egn_object_set_rotation_euler(this.handle, x, y, z);
    return this;
  }

  setEulerDeg(x: number, y: number, z: number): this {
    const d = Math.PI / 180;
    return this.setEuler(x * d, y * d, z * d);
  }

  setScale(x: number, y: number, z: number): this {
    this.engine.module._egn_object_set_scale(this.handle, x, y, z);
    return this;
  }

  lookAt(x: number, y: number, z: number): this {
    this.engine.module._egn_object_look_at(this.handle, x, y, z);
    return this;
  }

  setLayer(layer: number): this {
    this.engine.module._egn_object_set_layer(this.handle, layer);
    return this;
  }

  get layer(): number {
    return this.engine.module._egn_object_get_layer(this.handle);
  }

  setActive(active: boolean): this {
    this.engine.module._egn_object_set_active(this.handle, active);
    return this;
  }

  get active(): boolean {
    return !!this.engine.module._egn_object_is_active_in_hierarchy(this.handle);
  }

  createChild(name = 'Object'): Object {
    return this.engine.createObject(name, this);
  }

  children(): Object[] {
    const m = this.engine.module;
    const count = m._egn_object_child_count(this.handle);
    const children: Object[] = [];
    for (let i = 0; i < count; i++) {
      children.push(new Object(this.engine, m._egn_object_child_at(this.handle, i)));
    }
    return children;
  }

  find(predicate: (object: Object) => boolean): Object | null {
    if (predicate(this)) return this;
    for (const child of this.children()) {
      const found = child.find(predicate);
      if (found) return found;
    }
    return null;
  }

  addModel(options: ModelOptions): ModelComponent {
    const c = options.color ?? [1, 1, 1, 1];
    const handle = this.engine.module._egn_object_add_model(
      this.handle,
      options.mesh.id,
      options.texture?.id ?? 0,
      c[0], c[1], c[2], c[3],
      options.castShadows ?? true,
      options.enabled ?? true,
    );
    return new ModelComponent(this.engine, handle);
  }

  addCamera(options: CameraOptions = {}): CameraComponent {
    const viewport = options.viewport ?? [0, 0, 1, 1];
    const clear = options.clearColor ?? [0.02, 0.03, 0.07, 1];
    const handle = this.engine.module._egn_object_add_camera(
      this.handle,
      options.projection ?? Projection.Perspective,
      options.fov ?? 60,
      options.orthoSize ?? 10,
      options.near ?? 0.1,
      options.far ?? 1000,
      viewport[0], viewport[1], viewport[2], viewport[3],
      options.layerMask ?? 0xffffffff,
      clear[0], clear[1], clear[2], clear[3],
      options.clearColorEnabled ?? true,
      options.clearDepthEnabled ?? true,
      options.order ?? 0,
      options.enabled ?? true,
    );
    return new CameraComponent(this.engine, handle);
  }

  addLight(options: LightOptions = {}): LightComponent {
    const color = options.color ?? [1, 1, 1];
    const handle = this.engine.module._egn_object_add_light(
      this.handle,
      options.type === 'point' ? 1 : 0,
      color[0], color[1], color[2],
      options.intensity ?? 1,
      options.range ?? 10,
      options.castShadows ?? false,
      options.enabled ?? true,
    );
    return new LightComponent(this.engine, handle);
  }

  addRigidbody(options: RigidbodyOptions = {}): RigidbodyComponent {
    const m = this.engine.module;
    const handle = m._egn_object_add_rigidbody(
      this.handle,
      motionValue(options.motion ?? 'dynamic'),
      options.is2D ?? false,
      options.gravityFactor ?? 1,
      options.friction ?? 0.5,
      options.restitution ?? 0,
      options.linearDamping ?? 0,
      options.angularDamping ?? 0,
      options.enabled ?? true,
    );
    m._egn_rigidbody_set_mass(handle, options.mass ?? 1);
    return new RigidbodyComponent(this.engine, handle);
  }

  addCollider(options: ColliderOptions = {}): ColliderComponent {
    const size = options.size ?? [1, 1, 1];
    const offset = options.offset ?? [0, 0, 0];
    const handle = this.engine.module._egn_object_add_collider(
      this.handle,
      shapeValue(options.shape ?? 'box'),
      size[0], size[1], size[2],
      options.radius ?? 0.5,
      options.mesh?.id ?? 0,
      offset[0], offset[1], offset[2],
      options.enabled ?? true,
    );
    return new ColliderComponent(this.engine, handle);
  }

  addAnimation(options: AnimationOptions): AnimationComponent {
    const handle = this.engine.module._egn_object_add_animation(
      this.handle,
      options.clip,
      options.speed ?? 1,
      options.loop ?? true,
      options.playing ?? true,
      options.enabled ?? true,
    );
    return new AnimationComponent(this.engine, handle);
  }

  destroy(): void {
    this.engine.module._egn_object_destroy(this.handle);
  }
}

export class Scene {
  readonly root: Object;

  constructor(readonly engine: Engine) {
    this.root = new Object(engine, engine.module._egn_scene_root());
  }

  createObject(name = 'Object'): Object {
    return this.engine.createObject(name, this.root);
  }

  find(name: string): Object | null {
    return this.root.find((object) => object.name === name);
  }

  findCamera(): Object | null {
    return this.root.find((object) => !!this.engine.module._egn_object_get_component(object.handle, 0));
  }

  findDirectionalLight(): Object | null {
    return this.root.find((object) => {
      const handle = this.engine.module._egn_object_get_component(object.handle, 4);
      return !!handle && this.engine.module._egn_light_get_type(handle) === 0;
    });
  }
}

export class Input {
  private down = new Set<string>();
  private pressed = new Set<string>();
  private released = new Set<string>();
  private mouseDown = new Set<number>();
  private mousePressed = new Set<number>();
  private mouseReleased = new Set<number>();
  private dx = 0;
  private dy = 0;
  pointerLocked = false;
  private listeners: Array<[EventTarget, string, EventListener]> = [];

  constructor(private readonly canvas: HTMLCanvasElement) {
    const add = (target: EventTarget, type: string, listener: EventListener) => {
      target.addEventListener(type, listener);
      this.listeners.push([target, type, listener]);
    };

    add(window, 'keydown', (event) => {
      const e = event as KeyboardEvent;
      if (!e.repeat && !this.down.has(e.code)) {
        this.pressed.add(e.code);
      }
      this.down.add(e.code);
    });
    add(window, 'keyup', (event) => {
      const e = event as KeyboardEvent;
      this.down.delete(e.code);
      this.released.add(e.code);
    });
    add(window, 'blur', () => {
      this.down.clear();
      this.mouseDown.clear();
    });
    add(canvas, 'pointerdown', (event) => {
      const e = event as PointerEvent;
      if (!this.mouseDown.has(e.button)) this.mousePressed.add(e.button);
      this.mouseDown.add(e.button);
    });
    add(window, 'pointerup', (event) => {
      const e = event as PointerEvent;
      this.mouseDown.delete(e.button);
      this.mouseReleased.add(e.button);
    });
    add(window, 'pointermove', (event) => {
      const e = event as PointerEvent;
      if (!this.pointerLocked) return;
      this.dx += e.movementX;
      this.dy += e.movementY;
    });
    add(document, 'pointerlockchange', () => {
      this.pointerLocked = document.pointerLockElement === canvas;
      if (!this.pointerLocked) {
        this.dx = 0;
        this.dy = 0;
      }
    });
    add(canvas, 'contextmenu', (event) => event.preventDefault());
  }

  isDown(code: string): boolean {
    return this.down.has(code);
  }

  isPressed(code: string): boolean {
    return this.pressed.has(code);
  }

  isReleased(code: string): boolean {
    return this.released.has(code);
  }

  isMouseDown(button = 0): boolean {
    return this.mouseDown.has(button);
  }

  isMousePressed(button = 0): boolean {
    return this.mousePressed.has(button);
  }

  isMouseReleased(button = 0): boolean {
    return this.mouseReleased.has(button);
  }

  mouseDelta(): [number, number] {
    return [this.dx, this.dy];
  }

  requestPointerLock(): void {
    void this.canvas.requestPointerLock();
  }

  exitPointerLock(): void {
    document.exitPointerLock();
  }

  endFrame(): void {
    this.pressed.clear();
    this.released.clear();
    this.mousePressed.clear();
    this.mouseReleased.clear();
    this.dx = 0;
    this.dy = 0;
  }

  dispose(): void {
    for (const [target, type, listener] of this.listeners) {
      target.removeEventListener(type, listener);
    }
    this.listeners = [];
  }
}

export class Engine {
  readonly scene: Scene;
  readonly input: Input;
  readonly renderer: WebGPURenderer;
  readonly canvas: HTMLCanvasElement;
  readonly device: GPUDevice;

  private nextMeshId = 100;
  private nextTextureId = 1;
  private raf = 0;
  private lastTime = 0;

  private constructor(
    readonly module: EgnWasm,
    canvas: HTMLCanvasElement,
    device: GPUDevice,
    renderer: WebGPURenderer,
  ) {
    this.canvas = canvas;
    this.device = device;
    this.renderer = renderer;
    this.input = new Input(canvas);
    this.scene = new Scene(this);
  }

  static async create(options: EngineOptions): Promise<Engine> {
    if (!navigator.gpu) {
      throw new Error('WebGPU is not available in this browser');
    }

    const module = await options.moduleFactory();
    if (!module._egn_web_init()) {
      throw new Error('failed to initialize the EGN wasm core');
    }

    // Some Linux/ANGLE configurations reject the high-performance request
    // before the GPU process has fully warmed up. Fall through to the default
    // and low-power adapters instead of failing the whole application.
    const preference = options.powerPreference ?? 'high-performance';
    let adapter = await navigator.gpu.requestAdapter({ powerPreference: preference });
    if (!adapter && preference !== 'low-power') {
      adapter = await navigator.gpu.requestAdapter({ powerPreference: 'low-power' });
    }
    if (!adapter) {
      adapter = await navigator.gpu.requestAdapter();
    }
    if (!adapter) {
      module._egn_web_shutdown();
      throw new Error('no suitable WebGPU adapter found');
    }

    const device = await adapter.requestDevice();
    const context = options.canvas.getContext('webgpu');
    if (!context) {
      module._egn_web_shutdown();
      throw new Error('could not create a WebGPU canvas context');
    }

    const format = navigator.gpu.getPreferredCanvasFormat();
    context.configure({ device, format, alphaMode: 'opaque' });
    const renderer = new WebGPURenderer(options.canvas, device, context, format, module);

    return new Engine(module, options.canvas, device, renderer);
  }

  get stats(): RendererStats {
    return this.renderer.stats;
  }

  setAmbient(r: number, g: number, b: number, intensity = 1): void {
    this.renderer.ambient = [r, g, b, intensity];
  }

  meshCube(): Mesh {
    return new Mesh(1);
  }

  meshSphere(): Mesh {
    return new Mesh(2);
  }

  meshQuad(): Mesh {
    return new Mesh(3);
  }

  createObject(name = 'Object', parent?: Object): Object {
    const parentHandle = parent?.handle ?? this.scene.root.handle;
    const handle = withString(this.module, name, (ptr) => this.module._egn_object_create(parentHandle, ptr));
    return new Object(this, handle);
  }

  async loadTexture(url: string): Promise<Texture> {
    const id = this.nextTextureId++;
    await this.renderer.registerTexture(id, url);
    return new Texture(id);
  }

  async loadGLB(url: string): Promise<{ mesh: Mesh; texture?: Texture }> {
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`failed to load GLB '${url}': ${response.status}`);
    }
    const parsed = parseGLB(await response.arrayBuffer());
    const id = this.nextMeshId++;
    this.renderer.registerMesh(id, parsed.data);
    this.registerCollisionMesh(id, parsed.data);

    let texture: Texture | undefined;
    if (parsed.image) {
      const textureId = this.nextTextureId++;
      await this.renderer.registerTextureFromBlob(textureId, parsed.image);
      texture = new Texture(textureId);
    }
    return { mesh: new Mesh(id, parsed.data), texture };
  }

  async loadOBJ(url: string): Promise<Mesh> {
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

  registerMesh(data: MeshData, id = this.nextMeshId++): Mesh {
    this.renderer.registerMesh(id, data);
    this.registerCollisionMesh(id, data);
    return new Mesh(id, data);
  }

  private registerCollisionMesh(id: number, data: MeshData): void {
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

  tick(dt: number): void {
    this.module._egn_web_update(dt);
    this.renderer.render();
    this.input.endFrame();
  }

  run(update?: (dt: number) => void): void {
    this.stop();
    this.lastTime = performance.now();
    const loop = (time: number) => {
      const dt = Math.min(0.1, (time - this.lastTime) / 1000);
      this.lastTime = time;
      update?.(dt);
      this.tick(dt);
      this.raf = requestAnimationFrame(loop);
    };
    this.raf = requestAnimationFrame(loop);
  }

  stop(): void {
    if (this.raf) {
      cancelAnimationFrame(this.raf);
      this.raf = 0;
    }
  }

  destroy(): void {
    this.stop();
    this.input.dispose();
    this.renderer.destroy();
    this.module._egn_web_shutdown();
  }
}

// Convenience re-export so applications can use the SDK from one import.
export { WebGPURenderer } from '../../engine/renderer/webgpu/renderer';
