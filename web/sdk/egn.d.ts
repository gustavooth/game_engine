/// <reference types="@webgpu/types" />
export interface MeshData {
	vertices: Float32Array;
	indices: Uint32Array;
	positions: Float32Array;
}
export type EgnWasm = {
	HEAPF32: Float32Array;
	HEAPU8: Uint8Array;
	_malloc(size: number): number;
	_free(ptr: number): void;
	[key: string]: any;
};
export interface TextureResource {
	id: number;
	view: GPUTextureView;
	sampler: GPUSampler;
	bindGroup: GPUBindGroup;
}
export interface RendererStats {
	objects: number;
	drawCalls: number;
	cameras: number;
	shadowCasters: number;
}
export declare class WebGPURenderer {
	readonly device: GPUDevice;
	readonly canvas: HTMLCanvasElement;
	readonly context: GPUCanvasContext;
	readonly format: GPUTextureFormat;
	private module;
	private sceneModule;
	private shadowModule;
	private mainPipeline;
	private shadowPipeline;
	private frameLayout;
	private objectLayout;
	private textureLayout;
	private shadowFrameLayout;
	private shadowPipelineLayout;
	private mainPipelineLayout;
	private solidTexture;
	private meshes;
	private textures;
	private objectGpu;
	private cameraGpu;
	private depthTexture?;
	private depthView?;
	private shadowTexture;
	private shadowView;
	private shadowSampler;
	private shadowGlobals;
	private shadowBindGroup;
	private scratch;
	ambient: [
		number,
		number,
		number,
		number
	];
	stats: RendererStats;
	constructor(canvas: HTMLCanvasElement, device: GPUDevice, context: GPUCanvasContext, format: GPUTextureFormat, module: EgnWasm);
	private createShadowResources;
	private createSolidTexture;
	private registerBuiltinMeshes;
	registerMesh(id: number, data: MeshData): void;
	registerTexture(id: number, url: string): Promise<TextureResource>;
	registerTextureFromBlob(id: number, blob: Blob): Promise<TextureResource>;
	private registerTextureBitmap;
	private resize;
	private floats;
	private readScene;
	private objectBuffer;
	private cameraFrame;
	private computeShadow;
	render(): void;
	destroy(): void;
}
export type EgnModuleFactory = () => Promise<EgnWasm>;
export declare enum Projection {
	Perspective = 0,
	Orthographic = 1
}
export type LightKind = "directional" | "point";
export type MotionType = "static" | "kinematic" | "dynamic";
export type ColliderShape = "box" | "sphere" | "mesh";
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
export type Vec3Tuple = [
	number,
	number,
	number
];
export type ColorTuple = [
	number,
	number,
	number,
	number
];
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
	viewport?: [
		number,
		number,
		number,
		number
	];
	layerMask?: number;
	clearColor?: ColorTuple;
	clearColorEnabled?: boolean;
	clearDepthEnabled?: boolean;
	order?: number;
	enabled?: boolean;
}
export interface LightOptions {
	type?: LightKind;
	color?: [
		number,
		number,
		number
	];
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
export declare class Mesh {
	readonly id: number;
	readonly data?: MeshData | undefined;
	constructor(id: number, data?: MeshData | undefined);
}
export declare class Texture {
	readonly id: number;
	constructor(id: number);
}
export declare class Component {
	protected readonly engine: Engine;
	readonly handle: number;
	constructor(engine: Engine, handle: number);
	get valid(): boolean;
	setEnabled(enabled: boolean): this;
	destroy(): void;
}
export declare class ModelComponent extends Component {
	setColor(color: ColorTuple): this;
	setTexture(texture: Texture): this;
	setMesh(mesh: Mesh): this;
	setCastShadows(castShadows: boolean): this;
}
export declare class CameraComponent extends Component {
	setFov(fov: number): this;
	setViewport(x: number, y: number, width: number, height: number): this;
	setLayerMask(mask: number): this;
	setClearColor(color: ColorTuple): this;
}
export declare class LightComponent extends Component {
	setColor(color: [
		number,
		number,
		number
	]): this;
	setIntensity(intensity: number): this;
	setRange(range: number): this;
}
export declare class RigidbodyComponent extends Component {
	applyImpulse(x: number, y: number, z: number): void;
	setLinearVelocity(x: number, y: number, z: number): void;
	linearVelocity(): Vec3Tuple;
}
export declare class ColliderComponent extends Component {
}
export declare class AnimationComponent extends Component {
	play(): this;
	pause(): this;
	setClip(clip: number): this;
	setSpeed(speed: number): this;
	setLoop(loop: boolean): this;
}
declare class Object$1 {
	readonly engine: Engine;
	readonly handle: number;
	constructor(engine: Engine, handle: number);
	get valid(): boolean;
	get name(): string;
	setName(name: string): this;
	setPosition(x: number, y: number, z: number): this;
	position(): Vec3Tuple;
	setEuler(x: number, y: number, z: number): this;
	setEulerDeg(x: number, y: number, z: number): this;
	setLookAngles(pitch: number, yaw: number, roll?: number): this;
	setScale(x: number, y: number, z: number): this;
	lookAt(x: number, y: number, z: number): this;
	setLayer(layer: number): this;
	get layer(): number;
	setActive(active: boolean): this;
	get active(): boolean;
	createChild(name?: string): Object$1;
	children(): Object$1[];
	find(predicate: (object: Object$1) => boolean): Object$1 | null;
	addModel(options: ModelOptions): ModelComponent;
	addCamera(options?: CameraOptions): CameraComponent;
	addLight(options?: LightOptions): LightComponent;
	addRigidbody(options?: RigidbodyOptions): RigidbodyComponent;
	addCollider(options?: ColliderOptions): ColliderComponent;
	addAnimation(options: AnimationOptions): AnimationComponent;
	destroy(): void;
}
export declare class Scene {
	readonly engine: Engine;
	readonly root: Object$1;
	constructor(engine: Engine);
	createObject(name?: string): Object$1;
	find(name: string): Object$1 | null;
	findCamera(): Object$1 | null;
	findDirectionalLight(): Object$1 | null;
}
export declare class Input {
	private readonly canvas;
	private down;
	private pressed;
	private released;
	private mouseDown;
	private mousePressed;
	private mouseReleased;
	private dx;
	private dy;
	pointerLocked: boolean;
	private listeners;
	constructor(canvas: HTMLCanvasElement);
	isDown(code: string): boolean;
	isPressed(code: string): boolean;
	isReleased(code: string): boolean;
	isMouseDown(button?: number): boolean;
	isMousePressed(button?: number): boolean;
	isMouseReleased(button?: number): boolean;
	mouseDelta(): [
		number,
		number
	];
	requestPointerLock(): void;
	exitPointerLock(): void;
	endFrame(): void;
	dispose(): void;
}
export declare class Engine {
	readonly module: EgnWasm;
	readonly scene: Scene;
	readonly input: Input;
	readonly renderer: WebGPURenderer;
	readonly canvas: HTMLCanvasElement;
	readonly device: GPUDevice;
	private nextMeshId;
	private nextTextureId;
	private raf;
	private lastTime;
	private constructor();
	static create(options: EngineOptions): Promise<Engine>;
	get stats(): RendererStats;
	setAmbient(r: number, g: number, b: number, intensity?: number): void;
	meshCube(): Mesh;
	meshSphere(): Mesh;
	meshQuad(): Mesh;
	createObject(name?: string, parent?: Object$1): Object$1;
	loadTexture(url: string): Promise<Texture>;
	loadGLB(url: string): Promise<{
		mesh: Mesh;
		texture?: Texture;
	}>;
	loadOBJ(url: string): Promise<Mesh>;
	registerMesh(data: MeshData, id?: number): Mesh;
	private registerCollisionMesh;
	tick(dt: number): void;
	run(update?: (dt: number) => void): void;
	stop(): void;
	destroy(): void;
}
/** Minimal Wavefront OBJ loader for static triangle meshes. Materials are
 * ignored; the caller assigns a texture/colour through the Model component. */
export declare function parseOBJ(source: string): MeshData;
export interface GLBResult {
	data: MeshData;
	image?: Blob;
}
export declare function parseGLB(buffer: ArrayBuffer): GLBResult;

export {
	Object$1 as Object,
};

export {};
