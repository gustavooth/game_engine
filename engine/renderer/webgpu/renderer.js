import { WasmMath } from "./wasm_math.js";
import { createCube, createSphere, createQuad, VERTEX_STRIDE } from "./meshes.js";
import { parseOBJ } from "./loaders/obj.js";
import { parseGLB } from "./loaders/glb.js";
const COMPONENT_CAMERA = 0;
const COMPONENT_MODEL = 1;
const COMPONENT_SPRITE = 2;
const COMPONENT_CAMERA_2D = 8;
const COMPONENT_CANVAS = 9;
const COMPONENT_ANIMATION = 3;
const COMPONENT_LIGHT = 4;
const COMPONENT_PARTICLES = 7;
const LIGHT_DIRECTIONAL = 0;
const LIGHT_POINT = 1;
const SHADOW_CASCADES = 4;
// Must match Globals in scene.wgsl/sky.wgsl: 108 floats before the point-light
// array, 8 lights * 2 vec4 = 64 floats, then inverse VP/atmosphere/fog/time and
// the shadow/sky vectors (cascade texel, shadow fade, zenith, horizon, ground,
// sun disc).
const GLOBALS_FLOATS = 232;
const SKY_LUT_WIDTH = 256;
const SKY_LUT_HEIGHT = 128;
const BLOOM_MAX_MIPS = 7;

// ---------------------------------------------------------------------------
// CPU evaluation of the atmosphere rendered by sky_lut.wgsl. A few samples per
// frame give lighting that matches the visible sky: the colour of the sun
// after crossing the atmosphere, the sky/ground ambient and the fog colour.
// Keep in sync with engine/renderer/atmosphere.c.
// ---------------------------------------------------------------------------
const ATMOSPHERE_PLANET_RADIUS = 6371;
const ATMOSPHERE_RADIUS = 6471;
const ATMOSPHERE_RAYLEIGH_HEIGHT = 8;
const ATMOSPHERE_MIE_HEIGHT = 1.2;
const BETA_RAYLEIGH = [0.0058, 0.0135, 0.0331];

function raySphere(origin, direction, radius) {
  const b = origin[0] * direction[0] + origin[1] * direction[1] + origin[2] * direction[2];
  const c = origin[0] * origin[0] + origin[1] * origin[1] + origin[2] * origin[2] - radius * radius;
  const discriminant = b * b - c;
  if (discriminant < 0) return null;
  const root = Math.sqrt(discriminant);
  return [-b - root, -b + root];
}

// Optical depth [Rayleigh, Mie] toward the sun, or null when the planet blocks it.
function lightOpticalDepth(position, sun, steps) {
  const hit = raySphere(position, sun, ATMOSPHERE_RADIUS);
  if (!hit || hit[1] <= 0) return [0, 0];
  const planet = raySphere(position, sun, ATMOSPHERE_PLANET_RADIUS - 1);
  if (planet && planet[0] > 0) return null;
  let rayleigh = 0, mie = 0, previous = 0;
  for (let i = 0; i < steps; i++) {
    const u = (i + 1) / steps;
    const next = hit[1] * u * u;
    const mid = 0.5 * (previous + next);
    const height = Math.max(0, Math.hypot(position[0] + sun[0] * mid,
      position[1] + sun[1] * mid, position[2] + sun[2] * mid) - ATMOSPHERE_PLANET_RADIUS);
    rayleigh += Math.exp(-height / ATMOSPHERE_RAYLEIGH_HEIGHT) * (next - previous);
    mie += Math.exp(-height / ATMOSPHERE_MIE_HEIGHT) * (next - previous);
    previous = next;
  }
  return [rayleigh, mie];
}

function atmosphereOrigin(params) {
  return [0, ATMOSPHERE_PLANET_RADIUS + 0.002 + Math.max(0, params.altitudeKm), 0];
}

function atmosphereMie(params) {
  return 0.021 * params.mie * (params.turbidity > 0 ? params.turbidity : 1);
}

function atmosphereSunTransmittance(params, steps) {
  const depth = lightOpticalDepth(atmosphereOrigin(params), params.sunDirection, steps);
  if (!depth) return [0, 0, 0];
  const betaMie = atmosphereMie(params);
  return BETA_RAYLEIGH.map((beta) =>
    Math.exp(-(beta * params.rayleigh * depth[0] + betaMie * 1.1 * depth[1])));
}

function atmosphereSkyRadiance(params, direction, steps, lightSteps) {
  const origin = atmosphereOrigin(params);
  const hit = raySphere(origin, direction, ATMOSPHERE_RADIUS);
  if (!hit || hit[1] <= 0) return [0, 0, 0];
  const tStart = Math.max(0, hit[0]);
  let tEnd = hit[1];
  const planet = raySphere(origin, direction, ATMOSPHERE_PLANET_RADIUS);
  if (planet && planet[0] > 0 && planet[0] < tEnd) tEnd = planet[0];

  const betaR = BETA_RAYLEIGH.map((beta) => beta * params.rayleigh);
  const betaM = atmosphereMie(params);
  const sun = params.sunDirection;
  const mu = direction[0] * sun[0] + direction[1] * sun[1] + direction[2] * sun[2];
  const phaseR = 3 / (16 * Math.PI) * (1 + mu * mu);
  const g = params.mieAnisotropy;
  const phaseM = 3 / (8 * Math.PI) * ((1 - g * g) * (1 + mu * mu)) /
    ((2 + g * g) * Math.pow(Math.max(1 + g * g - 2 * g * mu, 0.001), 1.5));

  const sumR = [0, 0, 0], sumM = [0, 0, 0];
  let opticalR = 0, opticalM = 0, previous = tStart;
  const span = tEnd - tStart;
  for (let i = 0; i < steps; i++) {
    const u = (i + 1) / steps;
    const next = tStart + span * u * u;
    const mid = 0.5 * (previous + next);
    const length = next - previous;
    previous = next;
    const position = [origin[0] + direction[0] * mid, origin[1] + direction[1] * mid,
      origin[2] + direction[2] * mid];
    const height = Math.max(0, Math.hypot(position[0], position[1], position[2]) - ATMOSPHERE_PLANET_RADIUS);
    const densityR = Math.exp(-height / ATMOSPHERE_RAYLEIGH_HEIGHT) * length;
    const densityM = Math.exp(-height / ATMOSPHERE_MIE_HEIGHT) * length;
    opticalR += densityR;
    opticalM += densityM;
    const light = lightOpticalDepth(position, sun, lightSteps);
    if (!light) continue;
    for (let c = 0; c < 3; c++) {
      const transmittance = Math.exp(-(betaR[c] * (opticalR + light[0]) + betaM * 1.1 * (opticalM + light[1])));
      sumR[c] += densityR * transmittance;
      sumM[c] += densityM * transmittance;
    }
  }
  return [0, 1, 2].map((c) => (sumR[c] * betaR[c] * phaseR + sumM[c] * betaM * phaseM) *
    params.sunColor[c] * params.sunIntensity);
}
const OBJECT_FLOATS = 44;

function shadowSizeForQuality(quality) {
  if (quality <= 0) return 1024;
  if (quality === 1) return 1536;
  if (quality >= 3) return 4096;
  return 2048;
}

function cascadeCountForSettings(settings) {
  const explicit = settings?.shadow_cascades ?? 0;
  if (explicit >= 1 && explicit <= SHADOW_CASCADES) return explicit;
  return [2, 3, 4, 4][settings?.shadow_quality ?? 2] ?? 4;
}

function transformVec4(m, v) {
  const x = v[0], y = v[1], z = v[2], w = v[3];
  return [
    m[0] * x + m[4] * y + m[8] * z + m[12] * w,
    m[1] * x + m[5] * y + m[9] * z + m[13] * w,
    m[2] * x + m[6] * y + m[10] * z + m[14] * w,
    m[3] * x + m[7] * y + m[11] * z + m[15] * w,
  ];
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function slerp(a, b, t) {
  let x1 = b[0], y1 = b[1], z1 = b[2], w1 = b[3];
  let cos = a[0] * x1 + a[1] * y1 + a[2] * z1 + a[3] * w1;
  if (cos < 0) {
    cos = -cos;
    x1 = -x1; y1 = -y1; z1 = -z1; w1 = -w1;
  }
  if (cos > 0.9995) {
    return normalizeQuat([lerp(a[0], x1, t), lerp(a[1], y1, t), lerp(a[2], z1, t), lerp(a[3], w1, t)]);
  }
  const angle = Math.acos(Math.min(1, Math.max(-1, cos)));
  const sin = Math.sin(angle);
  const wa = Math.sin((1 - t) * angle) / sin;
  const wb = Math.sin(t * angle) / sin;
  return [
    a[0] * wa + x1 * wb,
    a[1] * wa + y1 * wb,
    a[2] * wa + z1 * wb,
    a[3] * wa + w1 * wb,
  ];
}

function normalizeQuat(q) {
  const length = Math.hypot(q[0], q[1], q[2], q[3]) || 1;
  return [q[0] / length, q[1] / length, q[2] / length, q[3] / length];
}

function sampleChannel(channel, time) {
  const times = channel.times;
  const components = channel.components;
  let key = 0;
  if (times.length === 0) return null;
  if (time >= times[times.length - 1]) {
    key = times.length - 1;
  } else if (time > times[0]) {
    for (let i = 0; i + 1 < times.length; i++) {
      if (time < times[i + 1]) {
        key = i;
        break;
      }
    }
  }
  const t = key + 1 < times.length && times[key + 1] > times[key]
    ? Math.min(1, Math.max(0, (time - times[key]) / (times[key + 1] - times[key])))
    : 0;
  const keyValue = (index) => {
    const base = channel.interpolation === "CUBICSPLINE" ? (index * 3 + 1) * components : index * components;
    return base;
  };
  let i0 = keyValue(key);
  let i1 = channel.interpolation === "STEP" || key + 1 >= times.length ? i0 : keyValue(key + 1);
  if (i0 + components > channel.values.length) i0 = 0;
  if (i1 + components > channel.values.length) i1 = i0;
  const v0 = Array.from(channel.values.slice(i0, i0 + components));
  const v1 = Array.from(channel.values.slice(i1, i1 + components));
  if (channel.path === "rotation") {
    const q0 = normalizeQuat(v0);
    if (channel.interpolation === "STEP" || i0 === i1) return q0;
    return slerp(q0, v1, t);
  }
  if (channel.interpolation === "STEP" || i0 === i1) return v0;
  return v0.map((value, index) => lerp(value, v1[index], t));
}

class WebGPURenderer {
  device;
  canvas;
  context;
  format;
  module;
  sceneModule;
  shadowModule;
  mainPipeline;
  shadowPipeline;
  particlePipeline;
  postPipeline;
  postModule;
  skyPipeline;
  skyModule;
  effectsPipeline;
  effectsModule;
  bloomModule;
  exposureModule;
  effectsLayout;
  effectsBindGroup;
  effectsUniformBuffer;
  effectsSampler;
  effectsDepthSampler;
  effectsTexture;
  effectsView;
  bloomPipeline;
  bloomLayout;
  bloomSampler;
  exposureBuffer;
  exposurePipeline;
  exposureLayout;
  exposureBindGroup;
  exposureUniformBuffer;
  hdrTexture;
  hdrView;
  normalTexture;
  normalView;
  postLayout;
  postBindGroup;
  postUniformBuffer;
  postSampler;
  frameLayout;
  objectLayout;
  textureLayout;
  materialLayout;
  materialSampler;
  luts = new Map();
  nextLutHandle = 1000;
  currentLutHandle = 0;
  defaultLutTexture;
  defaultLutView;
  whiteView;
  flatNormalView;
  blackView;
  flatNormalTexture;
  blackTexture;
  shadowFrameLayout;
  shadowPipelineLayout;
  mainPipelineLayout;
  solidTexture;
  meshes = /* @__PURE__ */ new Map();
  textures = /* @__PURE__ */ new Map();
  objectGpu = /* @__PURE__ */ new Map();
  cameraGpu = /* @__PURE__ */ new Map();
  particleGpu = /* @__PURE__ */ new Map();
  animatedGpu = new Map();
  depthTexture;
  depthView;
  shadowTexture;
  shadowView;
  shadowViews = [];
  shadowSampler;
  shadowGlobals = [];
  shadowBindGroups = [];
  shadowSize = 2048;
  elapsedTime = 0;
  scratch;
  nextMeshId = 100;
  nextTextureId = 1;
  ambient = [0.09, 0.1, 0.13, 1];
  stats = { objects: 0, drawCalls: 0, cameras: 0, shadowCasters: 0 };
  constructor(canvas, device, context, format, module, shaders) {
    this.canvas = canvas;
    this.device = device;
    this.context = context;
    this.format = format;
    this.module = module;
    this.scratch = module._malloc(256);
    this.math = new WasmMath(module);
    this.sceneModule = device.createShaderModule({ code: shaders.scene, label: "egn-scene-shader" });
    this.shadowModule = device.createShaderModule({ code: shaders.shadow, label: "egn-shadow-shader" });
    const spriteModule = device.createShaderModule({ code: shaders.sprite, label: "egn-sprite-shader" });
    const particleModule = device.createShaderModule({ code: shaders.particle, label: "egn-particle-shader" });
    this.postModule = device.createShaderModule({ code: shaders.post, label: "egn-post-shader" });
    this.skyModule = device.createShaderModule({ code: shaders.sky, label: "egn-sky-shader" });
    this.effectsModule = device.createShaderModule({ code: shaders.effects, label: "egn-effects-shader" });
    this.bloomModule = device.createShaderModule({ code: shaders.bloom, label: "egn-bloom-shader" });
    this.exposureModule = device.createShaderModule({ code: shaders.exposure, label: "egn-exposure-shader" });
    this.skyLutModule = device.createShaderModule({ code: shaders.skyLut, label: "egn-sky-lut-shader" });
    const hdrFormat = "rgba16float";
    const normalFormat = "rgba16float";
    this.frameLayout = device.createBindGroupLayout({
      label: "egn-frame-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth", viewDimension: "2d-array" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "comparison" } },
        // Sky-view LUT: the sky pass draws it and the PBR reflections sample it.
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } }
      ]
    });
    this.objectLayout = device.createBindGroupLayout({
      label: "egn-object-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } }
      ]
    });
    this.textureLayout = device.createBindGroupLayout({
      label: "egn-texture-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } }
      ]
    });
    this.materialLayout = device.createBindGroupLayout({
      label: "egn-material-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } }
      ]
    });
    this.materialSampler = device.createSampler({
      label: "egn-material-sampler",
      magFilter: "linear",
      minFilter: "linear",
      addressModeU: "repeat",
      addressModeV: "repeat",
      mipmapFilter: "linear"
    });
    this.shadowFrameLayout = device.createBindGroupLayout({
      label: "egn-shadow-frame-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: "uniform" } }
      ]
    });
    this.mainPipelineLayout = device.createPipelineLayout({
      bindGroupLayouts: [this.frameLayout, this.objectLayout, this.textureLayout, this.materialLayout]
    });
    this.shadowPipelineLayout = device.createPipelineLayout({
      bindGroupLayouts: [this.shadowFrameLayout, this.objectLayout]
    });
    const vertexBuffers = [{
      arrayStride: VERTEX_STRIDE,
      attributes: [
        { shaderLocation: 0, offset: 0, format: "float32x3" },
        { shaderLocation: 1, offset: 12, format: "float32x3" },
        { shaderLocation: 2, offset: 24, format: "float32x2" }
      ]
    }];
    this.mainPipeline = device.createRenderPipeline({
      label: "egn-main-pipeline",
      layout: this.mainPipelineLayout,
      vertex: { module: this.sceneModule, entryPoint: "vs_main", buffers: vertexBuffers },
      fragment: { module: this.sceneModule, entryPoint: "fs_main",
        targets: [{ format: hdrFormat }, { format: normalFormat }] },
      primitive: { topology: "triangle-list", cullMode: "none", frontFace: "ccw" },
      depthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "less" }
    });
    this.spritePipeline = device.createRenderPipeline({
      label: "egn-sprite-pipeline",
      layout: this.mainPipelineLayout,
      vertex: { module: spriteModule, entryPoint: "vs_main", buffers: vertexBuffers },
      fragment: { module: spriteModule, entryPoint: "fs_main", targets: [
        { format: hdrFormat,
          blend: { color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha" },
                   alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha" } } },
        { format: normalFormat }
      ] },
      primitive: { topology: "triangle-list", cullMode: "none" },
      depthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "less-equal" }
    });
    this.shadowPipeline = device.createRenderPipeline({
      label: "egn-shadow-pipeline",
      layout: this.shadowPipelineLayout,
      vertex: { module: this.shadowModule, entryPoint: "vs_main", buffers: vertexBuffers },
      primitive: { topology: "triangle-list", cullMode: "none", frontFace: "ccw" },
      depthStencil: {
        format: "depth32float",
        depthWriteEnabled: true,
        depthCompare: "less",
        // Slope-scaled bias in the rasterizer removes acne without the large
        // constant bias in the shader that detached shadows from casters.
        depthBias: 2,
        depthBiasSlopeScale: 2.0
      }
    });
    this.particlePipeline = device.createRenderPipeline({
      label: "egn-particle-pipeline",
      layout: device.createPipelineLayout({
        bindGroupLayouts: [this.frameLayout, this.textureLayout]
      }),
      vertex: {
        module: particleModule,
        entryPoint: "vs_main",
        buffers: [{
          arrayStride: 9 * 4,
          attributes: [
            { shaderLocation: 0, offset: 0, format: "float32x3" },
            { shaderLocation: 1, offset: 12, format: "float32x2" },
            { shaderLocation: 2, offset: 20, format: "float32x4" }
          ]
        }]
      },
      fragment: {
        module: particleModule,
        entryPoint: "fs_main",
        targets: [{
          format: hdrFormat,
          blend: {
            color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
            alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" }
          }
        }, { format: normalFormat }]
      },
      primitive: { topology: "triangle-list", cullMode: "none" },
      depthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "less" }
    });

    this.postSampler = device.createSampler({
      label: "egn-post-sampler",
      magFilter: "linear",
      minFilter: "linear",
      addressModeU: "clamp-to-edge",
      addressModeV: "clamp-to-edge"
    });
    this.postUniformBuffer = device.createBuffer({
      label: "egn-post-uniforms",
      size: 8 * 16,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });
    this.postLayout = device.createBindGroupLayout({
      label: "egn-post-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 5, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
        { binding: 6, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float", viewDimension: "3d" } },
        // Scene normal/albedo and depth, for the depth-aware AO/GI upsample.
        { binding: 7, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 8, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth" } }
      ]
    });
    this.postPipeline = device.createRenderPipeline({
      label: "egn-post-pipeline",
      layout: device.createPipelineLayout({ bindGroupLayouts: [this.postLayout] }),
      vertex: { module: this.postModule, entryPoint: "vs_main" },
      fragment: { module: this.postModule, entryPoint: "fs_main", targets: [{ format }] },
      primitive: { topology: "triangle-list", cullMode: "none" }
    });
    this.skyPipeline = device.createRenderPipeline({
      label: "egn-sky-pipeline",
      layout: device.createPipelineLayout({ bindGroupLayouts: [this.frameLayout] }),
      vertex: { module: this.skyModule, entryPoint: "vs_main" },
      fragment: { module: this.skyModule, entryPoint: "fs_main",
        targets: [{ format: hdrFormat }, { format: normalFormat }] },
      primitive: { topology: "triangle-list", cullMode: "none" },
      depthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "always" }
    });
    this.effectsLayout = device.createBindGroupLayout({
      label: "egn-effects-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth" } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "non-filtering" } },
        { binding: 5, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } }
      ]
    });
    this.effectsSampler = device.createSampler({ magFilter: "linear", minFilter: "linear" });
    this.effectsDepthSampler = device.createSampler({ magFilter: "nearest", minFilter: "nearest" });
    this.effectsUniformBuffer = device.createBuffer({
      label: "egn-effects-uniforms",
      size: 176,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });
    this.effectsPipeline = device.createRenderPipeline({
      label: "egn-effects-pipeline",
      layout: device.createPipelineLayout({ bindGroupLayouts: [this.effectsLayout] }),
      vertex: { module: this.effectsModule, entryPoint: "vs_main" },
      fragment: { module: this.effectsModule, entryPoint: "fs_main",
        targets: [{ format: "rgba16float" }] },
      primitive: { topology: "triangle-list", cullMode: "none" }
    });
    this.bloomLayout = device.createBindGroupLayout({
      label: "egn-bloom-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } }
      ]
    });
    this.bloomSampler = device.createSampler({ magFilter: "linear", minFilter: "linear" });
    const bloomPipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [this.bloomLayout] });
    // Mip-chain bloom: the downsample pipeline overwrites a mip, the upsample
    // pipeline adds the blurred smaller mip onto the larger one.
    this.bloomPipeline = device.createRenderPipeline({
      label: "egn-bloom-down-pipeline",
      layout: bloomPipelineLayout,
      vertex: { module: this.bloomModule, entryPoint: "vs_main" },
      fragment: { module: this.bloomModule, entryPoint: "fs_main",
        targets: [{ format: "rgba16float" }] },
      primitive: { topology: "triangle-list", cullMode: "none" }
    });
    this.bloomUpPipeline = device.createRenderPipeline({
      label: "egn-bloom-up-pipeline",
      layout: bloomPipelineLayout,
      vertex: { module: this.bloomModule, entryPoint: "vs_main" },
      fragment: { module: this.bloomModule, entryPoint: "fs_main",
        targets: [{ format: "rgba16float",
          blend: { color: { srcFactor: "one", dstFactor: "one" },
                   alpha: { srcFactor: "one", dstFactor: "one" } } }] },
      primitive: { topology: "triangle-list", cullMode: "none" }
    });
    this.exposureLayout = device.createBindGroupLayout({
      label: "egn-exposure-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } },
        { binding: 1, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "float" } },
        { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } }
      ]
    });
    this.exposureBuffer = device.createBuffer({
      label: "egn-exposure-buffer",
      size: 16,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST
    });
    this.device.queue.writeBuffer(this.exposureBuffer, 0, new Float32Array([1, 0.18, 1, 0]));
    this.exposureUniformBuffer = device.createBuffer({
      label: "egn-exposure-uniforms",
      size: 32,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });
    this.exposurePipeline = device.createComputePipeline({
      label: "egn-exposure-pipeline",
      layout: device.createPipelineLayout({ bindGroupLayouts: [this.exposureLayout] }),
      compute: { module: this.exposureModule, entryPoint: "main" }
    });

    // Sky-view LUT (see sky_lut.wgsl): re-rendered only when the sun or the
    // atmosphere settings change.
    this.skyLutTexture = device.createTexture({
      label: "egn-sky-lut",
      size: [SKY_LUT_WIDTH, SKY_LUT_HEIGHT],
      format: "rgba16float",
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
    });
    this.skyLutView = this.skyLutTexture.createView();
    // Azimuth wraps around, elevation clamps at the horizon and the zenith.
    this.skyLutSampler = device.createSampler({
      magFilter: "linear", minFilter: "linear",
      addressModeU: "repeat", addressModeV: "clamp-to-edge"
    });
    this.skyLutUniformBuffer = device.createBuffer({
      label: "egn-sky-lut-uniforms",
      size: 64,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });
    const skyLutLayout = device.createBindGroupLayout({
      label: "egn-sky-lut-layout",
      entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } }]
    });
    this.skyLutBindGroup = device.createBindGroup({
      layout: skyLutLayout,
      entries: [{ binding: 0, resource: { buffer: this.skyLutUniformBuffer } }]
    });
    this.skyLutPipeline = device.createRenderPipeline({
      label: "egn-sky-lut-pipeline",
      layout: device.createPipelineLayout({ bindGroupLayouts: [skyLutLayout] }),
      vertex: { module: this.skyLutModule, entryPoint: "vs_main" },
      fragment: { module: this.skyLutModule, entryPoint: "fs_main",
        targets: [{ format: "rgba16float" }] },
      primitive: { topology: "triangle-list", cullMode: "none" }
    });
    this.skyLutKey = "";

    this.createShadowResources();
    this.registerBuiltinMeshes();
    this.createSolidTexture();
    this.resize();
  }
  createShadowResources() {
    const device = this.device;
    this.shadowTexture?.destroy();
    for (const buffer of this.shadowGlobals) buffer.destroy();
    this.shadowViews = [];
    this.shadowGlobals = [];
    this.shadowBindGroups = [];

    const size = this.shadowSize;
    this.shadowTexture = device.createTexture({
      label: "egn-shadow-map-array",
      size: [size, size, SHADOW_CASCADES],
      format: "depth32float",
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
    });
    this.shadowView = this.shadowTexture.createView({ dimension: "2d-array" });
    if (!this.shadowSampler) {
      this.shadowSampler = device.createSampler({
        compare: "less-equal",
        magFilter: "linear",
        minFilter: "linear",
        addressModeU: "clamp-to-edge",
        addressModeV: "clamp-to-edge"
      });
    }
    for (let cascade = 0; cascade < SHADOW_CASCADES; cascade++) {
      this.shadowViews.push(this.shadowTexture.createView({
        dimension: "2d",
        baseArrayLayer: cascade,
        arrayLayerCount: 1
      }));
      const buffer = device.createBuffer({
        label: `egn-shadow-globals-${cascade}`,
        size: 64,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
      });
      this.shadowGlobals.push(buffer);
      this.shadowBindGroups.push(device.createBindGroup({
        layout: this.shadowFrameLayout,
        entries: [{ binding: 0, resource: { buffer } }]
      }));
    }
    for (const gpu of this.cameraGpu.values()) gpu.buffer.destroy();
    this.cameraGpu.clear();
  }
  createSolidTexture() {
    const create = (label, rgba) => {
      const texture = this.device.createTexture({
        label,
        size: [1, 1],
        format: "rgba8unorm",
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
      });
      this.device.queue.writeTexture({ texture }, new Uint8Array(rgba), { bytesPerRow: 4 }, [1, 1]);
      return { texture, view: texture.createView() };
    };
    const white = create("egn-white-texture", [255, 255, 255, 255]);
    const flatNormal = create("egn-flat-normal-texture", [128, 128, 255, 255]);
    const black = create("egn-black-texture", [0, 0, 0, 255]);
    const sampler = this.device.createSampler({ magFilter: "linear", minFilter: "linear" });
    this.whiteView = white.view;
    this.flatNormalView = flatNormal.view;
    this.blackView = black.view;
    this.flatNormalTexture = flatNormal.texture;
    this.blackTexture = black.texture;
    this.solidTexture = {
      id: 0,
      texture: white.texture,
      view: white.view,
      sampler,
      bindGroup: this.device.createBindGroup({
        layout: this.textureLayout,
        entries: [
          { binding: 0, resource: white.view },
          { binding: 1, resource: sampler }
        ]
      })
    };
    this.textures.set(0, this.solidTexture);
    this.defaultMaterialBindGroup = this.device.createBindGroup({
      layout: this.materialLayout,
      entries: [
        { binding: 0, resource: white.view },
        { binding: 1, resource: flatNormal.view },
        { binding: 2, resource: white.view },
        { binding: 3, resource: black.view },
        { binding: 4, resource: this.materialSampler }
      ]
    });

    const lutTexture = this.device.createTexture({
      label: "egn-identity-lut",
      size: [2, 2, 2],
      dimension: "3d",
      format: "rgba8unorm",
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
    });
    const lutData = new Uint8Array(2 * 2 * 2 * 4);
    let cursor = 0;
    for (let b = 0; b < 2; b++) {
      for (let g = 0; g < 2; g++) {
        for (let r = 0; r < 2; r++) {
          lutData[cursor++] = r * 255;
          lutData[cursor++] = g * 255;
          lutData[cursor++] = b * 255;
          lutData[cursor++] = 255;
        }
      }
    }
    this.device.queue.writeTexture({ texture: lutTexture }, lutData,
      { bytesPerRow: 2 * 4, rowsPerImage: 2 }, [2, 2, 2]);
    this.defaultLutTexture = lutTexture;
    this.defaultLutView = lutTexture.createView();
  }
  registerBuiltinMeshes() {
    this.registerMesh(1, createCube());
    this.registerMesh(2, createSphere(32, 16));
    this.registerMesh(3, createQuad());
  }
  registerMesh(id, data) {
    if (this.meshes.has(id)) {
      return;
    }
    const device = this.device;
    const vertexBuffer = device.createBuffer({
      label: `egn-mesh-${id}-vertices`,
      size: Math.max(16, data.vertices.byteLength),
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST
    });
    device.queue.writeBuffer(vertexBuffer, 0, data.vertices);
    const indexBuffer = device.createBuffer({
      label: `egn-mesh-${id}-indices`,
      size: Math.max(16, data.indices.byteLength),
      usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST
    });
    device.queue.writeBuffer(indexBuffer, 0, data.indices);
    this.meshes.set(id, {
      id,
      vertexBuffer,
      indexBuffer,
      indexCount: data.indices.length,
      positions: data.positions,
      data,
      baseVertices: new Float32Array(data.vertices),
      animated: !!(data.skin && data.animations?.length),
    });
    if (id >= 100) this.registerCollisionMesh(id, data);
  }

  materialBindGroup(mesh) {
    if (!mesh) return null;
    if (mesh.materialBindGroup) return mesh.materialBindGroup;
    const ids = mesh.materialTextures ?? {};
    const view = (id, fallback) => this.textures.get(id)?.view ?? fallback;
    mesh.materialBindGroup = this.device.createBindGroup({
      layout: this.materialLayout,
      entries: [
        { binding: 0, resource: view(ids.metallicRoughness, this.whiteView) },
        { binding: 1, resource: view(ids.normal, this.flatNormalView) },
        { binding: 2, resource: view(ids.occlusion, this.whiteView) },
        { binding: 3, resource: view(ids.emissive, this.blackView) },
        { binding: 4, resource: this.materialSampler }
      ]
    });
    return mesh.materialBindGroup;
  }

  registerCollisionMesh(id, data) {
    if (!data?.positions?.length) return;
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

  async loadTexture(url) {
    const id = this.nextTextureId++;
    await this.registerTexture(id, url);
    return id;
  }

  async loadOBJ(url) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`failed to load OBJ '${url}': ${response.status}`);
    const data = parseOBJ(await response.text());
    const id = this.nextMeshId++;
    this.registerMesh(id, data);
    return { id, data };
  }

  async loadGLB(url) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`failed to load GLB '${url}': ${response.status}`);
    const parsed = parseGLB(await response.arrayBuffer(), this.math);
    const id = this.nextMeshId++;
    this.registerMesh(id, parsed.data);
    const mesh = this.meshes.get(id);
    const material = parsed.material ?? {};
    const images = material.images ?? {};
    const textureIds = {};
    for (const [key, blob] of Object.entries(images)) {
      if (!blob) continue;
      const textureId = this.nextTextureId++;
      await this.registerTextureFromBlob(textureId, blob);
      textureIds[key] = textureId;
    }
    mesh.defaultTexture = textureIds.baseColor ?? 0;
    mesh.materialTextures = {
      metallicRoughness: textureIds.metallicRoughness ?? 0,
      normal: textureIds.normal ?? 0,
      occlusion: textureIds.occlusion ?? 0,
      emissive: textureIds.emissive ?? 0
    };
    let flags = 0;
    if (material.metallicRoughnessTexture) flags |= 1;
    if (material.normalTexture) flags |= 2;
    if (material.occlusionTexture) flags |= 4;
    if (material.emissiveTexture) flags |= 8;
    if (material.water) flags |= 16;
    mesh.material = {
      metallic: material.metallic ?? 1,
      roughness: material.roughness ?? 1,
      emissive: material.emissive ?? [0, 0, 0, 1],
      water: !!material.water,
      flags
    };
    return { id, data: parsed.data, textureId: mesh.defaultTexture };
  }

  async loadLUT(url) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`failed to load LUT '${url}': ${response.status}`);
    const text = await response.text();
    let size = 0;
    const values = [];
    for (const rawLine of text.split(/\r?\n/)) {
      const line = rawLine.trim();
      if (!line || line.startsWith("#"))
        continue;
      if (line.startsWith("LUT_3D_SIZE")) {
        size = Number.parseInt(line.split(/\s+/)[1] ?? "0", 10);
        continue;
      }
      if (line.startsWith("TITLE") || line.startsWith("DOMAIN") ||
          line.startsWith("LUT_1D"))
        continue;
      const parts = line.split(/\s+/).map(Number);
      if (parts.length >= 3 && parts.every((value) => Number.isFinite(value))) {
        values.push(parts[0], parts[1], parts[2]);
      }
    }

    if (size < 2 || size > 256 || values.length < size * size * size * 3) {
      throw new Error(`invalid .cube LUT '${url}'`);
    }

    const data = new Uint8Array(size * size * size * 4);
    for (let i = 0; i < size * size * size; i++) {
      data[i * 4 + 0] = Math.round(Math.max(0, Math.min(1, values[i * 3 + 0])) * 255);
      data[i * 4 + 1] = Math.round(Math.max(0, Math.min(1, values[i * 3 + 1])) * 255);
      data[i * 4 + 2] = Math.round(Math.max(0, Math.min(1, values[i * 3 + 2])) * 255);
      data[i * 4 + 3] = 255;
    }

    const texture = this.device.createTexture({
      label: `egn-lut-${this.nextLutHandle}`,
      size: [size, size, size],
      dimension: "3d",
      format: "rgba8unorm",
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
    });
    this.device.queue.writeTexture({ texture }, data,
      { bytesPerRow: size * 4, rowsPerImage: size }, [size, size, size]);
    const handle = this.nextLutHandle++;
    this.luts.set(handle, { texture, view: texture.createView() });
    return handle;
  }

  registerMeshData(data) {
    const id = this.nextMeshId++;
    this.registerMesh(id, data);
    return id;
  }
  async registerTexture(id, url) {
    if (this.textures.has(id)) {
      return this.textures.get(id);
    }
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`failed to load texture '${url}': ${response.status}`);
    }
    return this.registerTextureFromBlob(id, await response.blob());
  }
  registerTexturePixels(id, width, height, pixels) {
    const texture = this.device.createTexture({ label: `egn-text-${id}`, size: [width, height],
      format: "rgba8unorm", usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    this.device.queue.writeTexture({ texture }, pixels, { bytesPerRow: width * 4 }, [width, height]);
    const view = texture.createView();
    const sampler = this.device.createSampler({ magFilter: "linear", minFilter: "linear" });
    const resource = { id, texture, view, sampler, bindGroup: this.device.createBindGroup({
      layout: this.textureLayout, entries: [{ binding: 0, resource: view }, { binding: 1, resource: sampler }]
    }) };
    this.textures.set(id, resource);
    return resource;
  }
  async registerTextureFromBlob(id, blob) {
    if (this.textures.has(id)) {
      return this.textures.get(id);
    }
    const bitmap = await createImageBitmap(blob, { colorSpaceConversion: "none" });
    try {
      return this.registerTextureBitmap(id, bitmap);
    } finally {
      bitmap.close();
    }
  }
  registerTextureBitmap(id, bitmap) {
    const texture = this.device.createTexture({
      label: `egn-texture-${id}`,
      size: [bitmap.width, bitmap.height],
      format: "rgba8unorm",
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT
    });
    this.device.queue.copyExternalImageToTexture({ source: bitmap }, { texture }, [bitmap.width, bitmap.height]);
    const view = texture.createView();
    const sampler = this.device.createSampler({
      magFilter: "linear",
      minFilter: "linear",
      mipmapFilter: "linear",
      addressModeU: "repeat",
      addressModeV: "repeat"
    });
    const resource = {
      id,
      texture,
      view,
      sampler,
      bindGroup: this.device.createBindGroup({
        layout: this.textureLayout,
        entries: [
          { binding: 0, resource: view },
          { binding: 1, resource: sampler }
        ]
      })
    };
    this.textures.set(id, resource);
    return resource;
  }
  lutViewForHandle(handle) {
    return this.luts.get(handle)?.view ?? this.defaultLutView;
  }
  createPostBindGroup() {
    this.postBindGroup = this.device.createBindGroup({
      layout: this.postLayout,
      entries: [
        { binding: 0, resource: this.hdrView },
        { binding: 1, resource: this.postSampler },
        { binding: 2, resource: { buffer: this.postUniformBuffer } },
        { binding: 3, resource: this.effectsView },
        { binding: 4, resource: this.bloomMips[0].view },
        { binding: 5, resource: { buffer: this.exposureBuffer } },
        { binding: 6, resource: this.lutViewForHandle(this.currentLutHandle) },
        { binding: 7, resource: this.normalView },
        { binding: 8, resource: this.depthView }
      ]
    });
  }
  resize() {
    const dpr = window.devicePixelRatio || 1;
    const width = Math.max(1, Math.floor(this.canvas.clientWidth * dpr));
    const height = Math.max(1, Math.floor(this.canvas.clientHeight * dpr));
    if (this.canvas.width === width && this.canvas.height === height && this.depthTexture && this.hdrTexture) {
      return;
    }
    this.canvas.width = width;
    this.canvas.height = height;
    this.depthTexture?.destroy();
    this.hdrTexture?.destroy();
    this.normalTexture?.destroy();
    this.depthTexture = this.device.createTexture({
      label: "egn-depth",
      size: [width, height],
      format: "depth32float",
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
    });
    this.depthView = this.depthTexture.createView();
    this.hdrTexture = this.device.createTexture({
      label: "egn-hdr-colour",
      size: [width, height],
      format: "rgba16float",
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
    });
    this.hdrView = this.hdrTexture.createView();
    this.normalTexture = this.device.createTexture({
      label: "egn-normal",
      size: [width, height],
      format: "rgba16float",
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
    });
    this.normalView = this.normalTexture.createView();
    this.effectsTexture?.destroy();
    const halfSize = [Math.max(1, Math.floor(width / 2)), Math.max(1, Math.floor(height / 2))];
    this.effectsTexture = this.device.createTexture({
      label: "egn-effects",
      size: halfSize,
      format: "rgba16float",
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
    });
    this.effectsView = this.effectsTexture.createView();
    this.createBloomChain(halfSize[0], halfSize[1]);
    this.createPostBindGroup();
    this.effectsBindGroup = this.device.createBindGroup({
      layout: this.effectsLayout,
      entries: [
        { binding: 0, resource: this.hdrView },
        { binding: 1, resource: this.normalView },
        { binding: 2, resource: this.depthView },
        { binding: 3, resource: this.effectsSampler },
        { binding: 4, resource: this.effectsDepthSampler },
        { binding: 5, resource: { buffer: this.effectsUniformBuffer } }
      ]
    });
    this.exposureBindGroup = this.device.createBindGroup({
      layout: this.exposureLayout,
      entries: [
        { binding: 0, resource: { buffer: this.exposureBuffer } },
        { binding: 1, resource: this.hdrView },
        { binding: 2, resource: { buffer: this.exposureUniformBuffer } }
      ]
    });
  }
  createBloomChain(width, height) {
    for (const mip of this.bloomMips ?? []) {
      mip.texture.destroy();
      mip.downBuffer.destroy();
      mip.upBuffer.destroy();
    }
    this.bloomMips = [];
    let w = width, h = height;
    for (let i = 0; i < BLOOM_MAX_MIPS; i++) {
      // Stop once a mip gets too small to add anything useful.
      if (i > 0 && (w < 4 || h < 4)) break;
      const texture = this.device.createTexture({
        label: `egn-bloom-mip-${i}`,
        size: [w, h],
        format: "rgba16float",
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
      });
      const createBuffer = (label) => this.device.createBuffer({
        label, size: 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
      });
      this.bloomMips.push({ texture, view: texture.createView(), width: w, height: h,
        downBuffer: createBuffer(`egn-bloom-down-${i}`), upBuffer: createBuffer(`egn-bloom-up-${i}`) });
      w = Math.max(1, Math.floor(w / 2));
      h = Math.max(1, Math.floor(h / 2));
    }
    const bind = (view, buffer) => this.device.createBindGroup({
      layout: this.bloomLayout,
      entries: [
        { binding: 0, resource: view },
        { binding: 1, resource: this.bloomSampler },
        { binding: 2, resource: { buffer } },
        { binding: 3, resource: { buffer: this.exposureBuffer } }
      ]
    });
    for (let i = 0; i < this.bloomMips.length; i++) {
      const mip = this.bloomMips[i];
      // Downsample into mip i reads the scene (i = 0) or mip i - 1.
      mip.downBindGroup = bind(i === 0 ? this.hdrView : this.bloomMips[i - 1].view, mip.downBuffer);
      // Upsample into mip i reads mip i + 1.
      mip.upBindGroup = i + 1 < this.bloomMips.length
        ? bind(this.bloomMips[i + 1].view, mip.upBuffer) : null;
    }
  }
  bloomMipCount(settings) {
    const wanted = [4, 5, 6, BLOOM_MAX_MIPS][settings?.bloom_quality ?? 2] ?? 6;
    return Math.min(wanted, this.bloomMips?.length ?? 0);
  }
  // Derives the sun colour, sky/ground ambient and fog colour from the same
  // atmosphere model the sky renders, so the lighting always matches the
  // visible sky. Also re-renders the sky LUT when its inputs change.
  updateAtmosphere(encoder, sun, camera) {
    const settings = this.graphicsSettings ?? {};
    const enabled = settings.atmosphere_enabled ?? true;
    const night = [0.006, 0.009, 0.018];
    const lighting = {
      sunDirection: [0, -1, 0], sunIntensity: 0, sunColor: [0, 0, 0],
      zenith: night.slice(), horizon: night.slice(), ground: [0, 0, 0], fog: night.slice()
    };
    if (sun) {
      lighting.sunDirection = this.math.vec3.normalize([sun.world[8], sun.world[9], sun.world[10]]);
      lighting.sunIntensity = sun.intensity;
      lighting.sunColor = [sun.color[0], sun.color[1], sun.color[2]];
    }
    if (!enabled) {
      const ambient = this.ambient.slice(0, 3).map((value) => value * 2.2);
      lighting.zenith = ambient;
      lighting.horizon = ambient;
      lighting.ground = ambient.map((value) => value * 0.5);
      lighting.fog = ambient;
      this.atmosphereLighting = lighting;
      return lighting;
    }

    const altitudeKm = Math.max(0, Math.trunc((camera?.world[13] ?? 0) * 0.02) * 0.05);
    const rawSun = lighting.sunColor.map((value) => value * lighting.sunIntensity);
    const params = {
      sunDirection: lighting.sunDirection,
      sunColor: rawSun,
      rayleigh: settings.atmosphere_rayleigh ?? 1,
      mie: settings.atmosphere_mie ?? 0.0035,
      turbidity: settings.atmosphere_turbidity ?? 2.4,
      mieAnisotropy: settings.atmosphere_mie_anisotropy ?? 0.76,
      sunIntensity: settings.atmosphere_sun_intensity ?? 20,
      altitudeKm
    };
    const steps = [8, 12, 16, 24][settings.atmosphere_quality ?? 2] ?? 16;
    const lightSteps = [2, 3, 4, 6][settings.atmosphere_quality ?? 2] ?? 4;
    const lutData = new Float32Array([
      ...params.sunDirection, altitudeKm,
      ...rawSun, 0,
      params.rayleigh, params.mie, params.turbidity, params.mieAnisotropy,
      params.sunIntensity, steps, lightSteps, 0
    ]);

    // Everything below only depends on these inputs, so it is cached.
    const key = Array.from(lutData).join(",");
    if (key !== this.skyLutKey) {
      this.skyLutKey = key;
      this.device.queue.writeBuffer(this.skyLutUniformBuffer, 0, lutData);
      const pass = encoder.beginRenderPass({
        label: "egn-sky-lut-pass",
        colorAttachments: [{ view: this.skyLutView, loadOp: "clear", storeOp: "store",
          clearValue: { r: 0, g: 0, b: 0, a: 1 } }]
      });
      pass.setPipeline(this.skyLutPipeline);
      pass.setBindGroup(0, this.skyLutBindGroup);
      pass.draw(3);
      pass.end();

      const cached = { sunColor: lighting.sunColor.slice(), zenith: night.slice(), horizon: night.slice() };
      if (sun && lighting.sunIntensity > 0) {
        const transmittance = atmosphereSunTransmittance(params, 12);
        // Fade the direct light out as the sun sinks below the horizon.
        const above = Math.max(0, Math.min(1, params.sunDirection[1] * 12 + 0.6));
        cached.sunColor = lighting.sunColor.map((value, c) => value * transmittance[c] * above);
        const zenith = atmosphereSkyRadiance(params, [0, 1, 0], 10, 4);
        // Average the horizon ring so the ambient does not depend on the view.
        const ring = [0, 0, 0];
        for (let i = 0; i < 6; i++) {
          const angle = i * Math.PI / 3;
          const direction = this.math.vec3.normalize([Math.cos(angle), 0.12, Math.sin(angle)]);
          const radiance = atmosphereSkyRadiance(params, direction, 10, 4);
          for (let c = 0; c < 3; c++) ring[c] += radiance[c] / 6;
        }
        cached.zenith = night.map((value, c) => value + zenith[c]);
        cached.horizon = night.map((value, c) => value + ring[c]);
      }
      this.skyLightingCache = cached;
    }

    const cached = this.skyLightingCache;
    lighting.sunColor = cached.sunColor;
    lighting.zenith = cached.zenith;
    lighting.horizon = cached.horizon;
    lighting.fog = cached.horizon;
    // Light bounced by the terrain: a neutral soil albedo lit by sun and sky.
    // The zenith dominates the sky irradiance (the bright horizon ring covers
    // little solid angle and arrives at grazing angles).
    const groundAlbedo = [0.22, 0.20, 0.17];
    const sunUp = Math.max(0, lighting.sunDirection[1]) * lighting.sunIntensity;
    lighting.ground = groundAlbedo.map((albedo, c) => albedo *
      (cached.sunColor[c] * sunUp + cached.horizon[c] * 0.35 + cached.zenith[c] * 0.65));
    this.atmosphereLighting = lighting;
    return lighting;
  }
  floats(ptr, offset, count) {
    return new Float32Array(this.module.HEAPF32.buffer, ptr + offset * 4, count).slice();
  }
  readScene() {
    const m = this.module;
    const models = [];
    const cameras = [];
    const sprites = [];
    const lights = [];
    const particles = [];
    const scratch = this.scratch;
    const walk = (object) => {
      if (!object || !m._egn_object_is_active_in_hierarchy(object)) {
        return;
      }
      const layer = m._egn_object_get_layer(object);
      const domain = m._egn_object_get_render_domain(object);
      const anchor = m._egn_object_get_canvas_anchor(object);
      m._egn_object_get_world_matrix(object, scratch);
      const world = this.floats(scratch, 0, 16);
      const animationHandle = m._egn_object_get_component(object, COMPONENT_ANIMATION);
      let animation = null;
      if (animationHandle && m._egn_component_is_enabled(animationHandle)) {
        animation = {
          clip: m._egn_animation_get_clip(animationHandle),
          time: m._egn_animation_get_time(animationHandle),
          speed: m._egn_animation_get_speed(animationHandle),
          loop: !!m._egn_animation_get_loop(animationHandle),
          playing: !!m._egn_animation_get_playing(animationHandle),
        };
      }
      const modelCount = m._egn_object_component_count(object, COMPONENT_MODEL);
      for (let i = 0; i < modelCount; i++) {
        const component = m._egn_object_get_component_at(object, COMPONENT_MODEL, i);
        if (!component || !m._egn_component_is_enabled(component)) continue;
        m._egn_model_get_color(component, scratch, scratch + 4, scratch + 8, scratch + 12);
        const color = this.floats(scratch, 0, 4);
        m._egn_model_get_pbr(component, scratch, scratch + 4);
        const [metallic, roughness] = this.floats(scratch, 0, 2);
        models.push({
          object,
          component,
          layer,
          domain,
          world: world.slice(),
          mesh: m._egn_model_get_mesh(component),
          texture: m._egn_model_get_texture(component),
          color,
          metallic,
          roughness,
          water: !!m._egn_model_get_water(component),
          castShadows: !!m._egn_model_get_cast_shadows(component),
          animation,
        });
      }
      const spriteCount = m._egn_object_component_count(object, COMPONENT_SPRITE);
      for (let i = 0; i < spriteCount; i++) {
        const component = m._egn_object_get_component_at(object, COMPONENT_SPRITE, i);
        if (!component || !m._egn_component_is_enabled(component)) continue;
        m._egn_sprite_get_size(component, scratch, scratch + 4);
        const [width, height] = this.floats(scratch, 0, 2);
        m._egn_sprite_get_pivot(component, scratch, scratch + 4);
        const [pivotX, pivotY] = this.floats(scratch, 0, 2);
        m._egn_sprite_get_color(component, scratch, scratch + 4, scratch + 8, scratch + 12);
        const color = this.floats(scratch, 0, 4);
        let local = this.math.mat4.translation([(0.5 - pivotX) * width, (0.5 - pivotY) * height, 0]);
        local = this.math.mat4.multiply(local, this.math.mat4.scaling([width, height, 1]));
        sprites.push({ component, layer, domain, anchor, width, height, world: this.math.mat4.multiply(world, local),
          texture: m._egn_sprite_get_texture(component), color, castShadows: false });
      }
      for (const cameraType of [COMPONENT_CAMERA, COMPONENT_CAMERA_2D, COMPONENT_CANVAS]) {
      const cameraCount = m._egn_object_component_count(object, cameraType);
      for (let i = 0; i < cameraCount; i++) {
        const component = m._egn_object_get_component_at(object, cameraType, i);
        if (!component || !m._egn_component_is_enabled(component)) continue;
        m._egn_camera_get_viewport(component, scratch, scratch + 4, scratch + 8, scratch + 12);
        const viewport = this.floats(scratch, 0, 4);
        m._egn_camera_get_clear_color(component, scratch, scratch + 4, scratch + 8, scratch + 12);
        const clearColor = this.floats(scratch, 0, 4);
        const view = cameraType === COMPONENT_CANVAS ? this.math.mat4.identity() : this.math.mat4.inverse(world);
        const aspect = Math.max(1e-4, viewport[2] * this.canvas.width / Math.max(1, viewport[3] * this.canvas.height));
        const near = m._egn_camera_get_near(component);
        const far = m._egn_camera_get_far(component);
        let projection;
        if (cameraType !== COMPONENT_CAMERA || m._egn_camera_get_projection(component) === 1) {
          const halfHeight = cameraType === COMPONENT_CANVAS ? viewport[3] * this.canvas.height * 0.5 : m._egn_camera_get_ortho_size(component) * 0.5;
          const halfWidth = halfHeight * aspect;
          projection = this.math.mat4.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, cameraType === COMPONENT_CAMERA ? near : -1000, cameraType === COMPONENT_CAMERA ? far : 1000);
        } else {
          projection = this.math.mat4.perspective(m._egn_camera_get_fov(component) * Math.PI / 180, aspect, near, far);
        }
        cameras.push({
          object,
          component,
          layer,
          domain: cameraType === COMPONENT_CANVAS ? 2 : cameraType === COMPONENT_CAMERA_2D ? 1 : 0,
          clearDepth: !!m._egn_camera_get_clear_depth_enabled(component),
          clearColorEnabled: !!m._egn_camera_get_clear_color_enabled(component),
          layerMask: m._egn_camera_get_layer_mask(component),
          world,
          view,
          projection,
          viewport,
          clearColor,
          aspect,
          near,
          far,
          fov: m._egn_camera_get_fov(component) * Math.PI / 180,
          order: m._egn_camera_get_order(component)
        });
      }
      }
      const lightCount = m._egn_object_component_count(object, COMPONENT_LIGHT);
      for (let i = 0; i < lightCount; i++) {
        const component = m._egn_object_get_component_at(object, COMPONENT_LIGHT, i);
        if (!component || !m._egn_component_is_enabled(component)) continue;
        m._egn_light_get_color(component, scratch, scratch + 4, scratch + 8);
        const color = this.floats(scratch, 0, 3);
        lights.push({
          object,
          component,
          type: m._egn_light_get_type(component),
          world,
          color,
          intensity: m._egn_light_get_intensity(component),
          range: m._egn_light_get_range(component),
          castShadows: !!m._egn_light_get_cast_shadows(component)
        });
      }
      const particleCount = m._egn_object_component_count(object, COMPONENT_PARTICLES);
      for (let i = 0; i < particleCount; i++) {
        const component = m._egn_object_get_component_at(object, COMPONENT_PARTICLES, i);
        if (!component || !m._egn_component_is_enabled(component)) continue;
        m._egn_particles_get_direction(component, scratch, scratch + 4, scratch + 8);
        const direction = this.floats(scratch, 0, 3);
        m._egn_particles_get_size(component, scratch, scratch + 4);
        const sizes = this.floats(scratch, 0, 2);
        m._egn_particles_get_color_start(component, scratch, scratch + 4, scratch + 8, scratch + 12);
        const colorStart = this.floats(scratch, 0, 4);
        m._egn_particles_get_color_end(component, scratch, scratch + 4, scratch + 8, scratch + 12);
        const colorEnd = this.floats(scratch, 0, 4);
        m._egn_particles_get_fade(component, scratch, scratch + 4);
        const fade = this.floats(scratch, 0, 2);
        particles.push({
          object,
          component,
          layer,
          world: world.slice(),
          texture: m._egn_particles_get_texture(component),
          max: m._egn_particles_get_max(component),
          spawnRate: m._egn_particles_get_spawn_rate(component),
          lifetime: m._egn_particles_get_lifetime(component),
          speed: m._egn_particles_get_speed(component),
          speedVariation: m._egn_particles_get_speed_variation(component),
          gravity: m._egn_particles_get_gravity(component),
          drag: m._egn_particles_get_drag(component),
          spread: m._egn_particles_get_spread(component),
          radius: m._egn_particles_get_radius(component),
          direction,
          sizeStart: sizes[0],
          sizeEnd: sizes[1],
          sizeScale: m._egn_particles_get_size_scale(component),
          fadeIn: fade[0],
          fadeOut: fade[1],
          colorStart,
          colorEnd,
        });
      }

      const childCount = m._egn_object_child_count(object);
      for (let i = 0; i < childCount; i++) {
        walk(m._egn_object_child_at(object, i));
      }
    };
    walk(m._egn_scene_root());
    cameras.sort((a, b) => a.order - b.order);
    return { models, sprites, cameras, lights, particles };
  }
  updateMeshAnimation(mesh, item, vertexBuffer) {
    if (!mesh.animated || !mesh.data.skin || !mesh.data.animations || !item.animation) {
      return;
    }
    const clip = mesh.data.animations[item.animation.clip];
    if (!clip || clip.duration <= 0) return;

    let time = item.animation.time;
    if (item.animation.loop) {
      time = ((time % clip.duration) + clip.duration) % clip.duration;
    } else {
      time = Math.min(clip.duration, Math.max(0, time));
    }

    const nodes = mesh.data.nodes;
    const count = nodes.length;
    const translations = new Array(count);
    const rotations = new Array(count);
    const scales = new Array(count);
    for (let i = 0; i < count; i++) {
      translations[i] = nodes[i].translation.slice();
      rotations[i] = nodes[i].rotation.slice();
      scales[i] = nodes[i].scale.slice();
    }
    for (const channel of clip.channels) {
      if (channel.node < 0 || channel.node >= count) continue;
      const value = sampleChannel(channel, time);
      if (!value) continue;
      if (channel.path === "translation") translations[channel.node] = value;
      else if (channel.path === "rotation") rotations[channel.node] = value;
      else if (channel.path === "scale") scales[channel.node] = value;
    }

    const worlds = new Array(count);
    const computeWorld = (index) => {
      if (worlds[index]) return worlds[index];
      let local = this.math.mat4.translation(translations[index]);
      local = this.math.mat4.multiply(local, this.math.mat4.fromQuat(rotations[index]));
      local = this.math.mat4.multiply(local, this.math.mat4.scaling(scales[index]));
      const parent = nodes[index].parent;
      const world = parent >= 0 ? this.math.mat4.multiply(computeWorld(parent), local) : local;
      worlds[index] = world;
      return world;
    };
    for (let i = 0; i < count; i++) computeWorld(i);

    const skin = mesh.data.skin;
    const inverseBind = skin.inverseBind;
    const boneCount = skin.joints.length;
    const bones = new Float32Array(boneCount * 16);
    for (let joint = 0; joint < boneCount; joint++) {
      const node = skin.joints[joint];
      const world = node >= 0 && node < count ? worlds[node] : this.math.mat4.identity();
      const inverse = inverseBind.slice(joint * 16, joint * 16 + 16);
      bones.set(this.math.mat4.multiply(world, inverse), joint * 16);
    }

    const base = mesh.baseVertices;
    const vertexCount = base.length / 8;
    const joints = mesh.data.skinData.joints;
    const weights = mesh.data.skinData.weights;
    const output = new Float32Array(base.length);
    for (let vertex = 0; vertex < vertexCount; vertex++) {
      let px = 0, py = 0, pz = 0;
      let nx = 0, ny = 0, nz = 0;
      const bx = base[vertex * 8], by = base[vertex * 8 + 1], bz = base[vertex * 8 + 2];
      const bnx = base[vertex * 8 + 3], bny = base[vertex * 8 + 4], bnz = base[vertex * 8 + 5];
      for (let k = 0; k < 4; k++) {
        const weight = weights[vertex * 4 + k];
        if (weight === 0) continue;
        const joint = joints[vertex * 4 + k];
        if (joint >= boneCount) continue;
        const offset = joint * 16;
        px += weight * (bones[offset] * bx + bones[offset + 4] * by + bones[offset + 8] * bz + bones[offset + 12]);
        py += weight * (bones[offset + 1] * bx + bones[offset + 5] * by + bones[offset + 9] * bz + bones[offset + 13]);
        pz += weight * (bones[offset + 2] * bx + bones[offset + 6] * by + bones[offset + 10] * bz + bones[offset + 14]);
        nx += weight * (bones[offset] * bnx + bones[offset + 4] * bny + bones[offset + 8] * bnz);
        ny += weight * (bones[offset + 1] * bnx + bones[offset + 5] * bny + bones[offset + 9] * bnz);
        nz += weight * (bones[offset + 2] * bnx + bones[offset + 6] * bny + bones[offset + 10] * bnz);
      }
      const normalLength = Math.hypot(nx, ny, nz) || 1;
      output[vertex * 8] = px;
      output[vertex * 8 + 1] = py;
      output[vertex * 8 + 2] = pz;
      output[vertex * 8 + 3] = nx / normalLength;
      output[vertex * 8 + 4] = ny / normalLength;
      output[vertex * 8 + 5] = nz / normalLength;
      output[vertex * 8 + 6] = base[vertex * 8 + 6];
      output[vertex * 8 + 7] = base[vertex * 8 + 7];
    }
    this.device.queue.writeBuffer(vertexBuffer, 0, output);
  }

  particleState(item) {
    let state = this.particleGpu.get(item.component);
    if (!state || state.max !== item.max) {
      if (state?.buffer) state.buffer.destroy();
      const max = Math.max(1, item.max);
      state = {
        max,
        buffer: this.device.createBuffer({
          label: `egn-particles-${item.component}`,
          size: Math.max(16, max * 6 * 9 * 4),
          usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
        }),
        positions: new Float32Array(max * 3),
        velocities: new Float32Array(max * 3),
        ages: new Float32Array(max),
        lives: new Float32Array(max),
        seeded: false,
      };
      this.particleGpu.set(item.component, state);
    }
    return state;
  }

  simulateParticles(item, dt) {
    const state = this.particleState(item);
    const max = state.max;
    const target = Math.min(max, Math.floor(Math.max(0, item.spawnRate) * Math.max(0.001, item.lifetime)));
    const world = item.world;
    const local = item.direction;
    let direction = [
      world[0] * local[0] + world[4] * local[1] + world[8] * local[2],
      world[1] * local[0] + world[5] * local[1] + world[9] * local[2],
      world[2] * local[0] + world[6] * local[1] + world[10] * local[2],
    ];
    const dl = Math.hypot(direction[0], direction[1], direction[2]) || 1;
    direction = [direction[0] / dl, direction[1] / dl, direction[2] / dl];
    const emitter = [world[12], world[13], world[14]];
    const step = Math.min(0.1, Math.max(0, dt));

    const random = () => Math.random() * 2 - 1;
    for (let i = 0; i < max; i++) {
      if (i >= target) {
        state.lives[i] = 0;
        continue;
      }
      let age = state.ages[i];
      const life = state.lives[i];
      if (life <= 0 || age >= life) {
        const offset = [random(), random(), random()];
        const ol = Math.hypot(offset[0], offset[1], offset[2]) || 1;
        const radius = (Math.random()) * item.radius;
        state.positions[i * 3] = emitter[0] + offset[0] / ol * radius;
        state.positions[i * 3 + 1] = emitter[1] + offset[1] / ol * radius;
        state.positions[i * 3 + 2] = emitter[2] + offset[2] / ol * radius;
        const spread = item.spread * 0.85;
        let dx = direction[0] + random() * spread;
        let dy = direction[1] + random() * spread;
        let dz = direction[2] + random() * spread;
        const length = Math.hypot(dx, dy, dz) || 1;
        dx /= length; dy /= length; dz /= length;
        const speed = item.speed * (1 + (Math.random() * 2 - 1) * item.speedVariation);
        state.velocities[i * 3] = dx * speed;
        state.velocities[i * 3 + 1] = dy * speed;
        state.velocities[i * 3 + 2] = dz * speed;
        state.ages[i] = 0;
        state.lives[i] = item.lifetime * (0.85 + Math.random() * 0.3);
        state.seeded = true;
        continue;
      }
      age += step;
      let vx = state.velocities[i * 3];
      let vy = state.velocities[i * 3 + 1] - item.gravity * step;
      let vz = state.velocities[i * 3 + 2];
      const damping = Math.max(1 - item.drag * step, 0);
      vx *= damping; vy *= damping; vz *= damping;
      state.velocities[i * 3] = vx;
      state.velocities[i * 3 + 1] = vy;
      state.velocities[i * 3 + 2] = vz;
      state.positions[i * 3] += vx * step;
      state.positions[i * 3 + 1] += vy * step;
      state.positions[i * 3 + 2] += vz * step;
      state.ages[i] = age;
    }
  }

  buildParticleVertices(item, camera) {
    const state = this.particleState(item);
    const vertices = new Float32Array(state.max * 6 * 9);
    const right = this.math.vec3.normalize([camera.view[0], camera.view[4], camera.view[8]]);
    const up = this.math.vec3.normalize([camera.view[1], camera.view[5], camera.view[9]]);
    const corners = [[-1, -1], [1, -1], [1, 1], [-1, -1], [1, 1], [-1, 1]];
    let count = 0;
    for (let i = 0; i < state.max; i++) {
      const life = state.lives[i];
      const age = state.ages[i];
      if (life <= 0 || age >= life) continue;
      const t = Math.min(1, Math.max(0, age / life));
      const fadeIn = item.fadeIn > 0 ? Math.min(1, t / item.fadeIn) : 1;
      const fadeOut = item.fadeOut > 0 ? Math.min(1, (1 - t) / item.fadeOut) : 1;
      const alpha = Math.min(fadeIn, fadeOut);
      const size = (item.sizeStart + (item.sizeEnd - item.sizeStart) * t) * item.sizeScale;
      const r = item.colorStart[0] + (item.colorEnd[0] - item.colorStart[0]) * t;
      const g = item.colorStart[1] + (item.colorEnd[1] - item.colorStart[1]) * t;
      const b = item.colorStart[2] + (item.colorEnd[2] - item.colorStart[2]) * t;
      const a = (item.colorStart[3] + (item.colorEnd[3] - item.colorStart[3]) * t) * alpha;
      const px = state.positions[i * 3], py = state.positions[i * 3 + 1], pz = state.positions[i * 3 + 2];
      for (let c = 0; c < 6; c++) {
        const [cx, cy] = corners[c];
        const offset = (count * 6 + c) * 9;
        vertices[offset] = px + right[0] * cx * size + up[0] * cy * size;
        vertices[offset + 1] = py + right[1] * cx * size + up[1] * cy * size;
        vertices[offset + 2] = pz + right[2] * cx * size + up[2] * cy * size;
        vertices[offset + 3] = (cx + 1) * 0.5;
        vertices[offset + 4] = (cy + 1) * 0.5;
        vertices[offset + 5] = r;
        vertices[offset + 6] = g;
        vertices[offset + 7] = b;
        vertices[offset + 8] = a;
      }
      count++;
    }
    if (count > 0) {
      this.device.queue.writeBuffer(state.buffer, 0, vertices.subarray(0, count * 6 * 9));
    }
    return count;
  }

  objectBuffer(item, mesh = null) {
    let gpu = this.objectGpu.get(item.component);
    if (!gpu) {
      const buffer = this.device.createBuffer({
        label: `egn-object-${item.component}`,
        size: OBJECT_FLOATS * 4,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
      });
      gpu = {
        buffer,
        data: new Float32Array(OBJECT_FLOATS),
        previousWorld: null,
        bindGroup: this.device.createBindGroup({
          layout: this.objectLayout,
          entries: [{ binding: 0, resource: { buffer } }]
        })
      };
      this.objectGpu.set(item.component, gpu);
    }
    const data = gpu.data;
    let worldChanged = !gpu.previousWorld;
    if (!worldChanged) {
      for (let i = 0; i < 16; i++) {
        if (gpu.previousWorld[i] !== item.world[i]) { worldChanged = true; break; }
      }
    }
    if (worldChanged) {
      data.set(item.world, 0);
      data.set(this.math.mat4.inverse(item.world), 24);
      if (!gpu.previousWorld) gpu.previousWorld = new Float32Array(16);
      gpu.previousWorld.set(item.world);
    }
    data.set(item.color, 16);
    const material = mesh?.material;
    data[20] = item.castShadows ? 1 : 0;
    data[21] = material?.metallic ?? item.metallic ?? 0;
    data[22] = material?.roughness ?? item.roughness ?? 1;
    let materialFlags = material?.flags ?? 0;
    if (item.water) materialFlags |= 16;
    data[23] = materialFlags;
    data[40] = material?.emissive?.[0] ?? 0;
    data[41] = material?.emissive?.[1] ?? 0;
    data[42] = material?.emissive?.[2] ?? 0;
    data[43] = material?.emissive?.[3] ?? 1;
    this.device.queue.writeBuffer(gpu.buffer, 0, data);
    return gpu;
  }
  cameraFrame(camera, cascades, sun, pointLights, shadowEnabled) {
    let gpu = this.cameraGpu.get(camera.component);
    if (!gpu) {
      const buffer = this.device.createBuffer({
        label: `egn-frame-${camera.component}`,
        size: GLOBALS_FLOATS * 4,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
      });
      gpu = {
        buffer,
        bindGroup: this.device.createBindGroup({
          layout: this.frameLayout,
          entries: [
            { binding: 0, resource: { buffer } },
            { binding: 1, resource: this.shadowView },
            { binding: 2, resource: this.shadowSampler },
            { binding: 3, resource: this.skyLutView },
            { binding: 4, resource: this.skyLutSampler }
          ]
        })
      };
      this.cameraGpu.set(camera.component, gpu);
    }
    const settings = this.graphicsSettings ?? {};
    const data = new Float32Array(GLOBALS_FLOATS);
    const viewProjection = this.math.mat4.multiply(camera.projection, camera.view);
    data.set(viewProjection, 0);
    data.set([camera.world[12], camera.world[13], camera.world[14], 1], 16);
    data.set([0, -1, 0, 0], 24);
    data.set([0, 0, 0, 0], 28);
    // Sun, sky and fog colours come from updateAtmosphere(), which derives
    // them from the atmosphere model so they match the rendered sky.
    const lighting = this.atmosphereLighting;
    const atmosphereEnabled = settings.atmosphere_enabled ?? true;
    data.set([...lighting.sunDirection, lighting.sunIntensity], 24);
    // w = shadow strength: shadows fully block the sun; the sky/ground
    // ambient is what keeps them from being black.
    data.set([...lighting.sunColor, 1], 28);
    data.set([
      lighting.horizon[0] * 0.35 + lighting.zenith[0] * 0.65,
      lighting.horizon[1] * 0.35 + lighting.zenith[1] * 0.65,
      lighting.horizon[2] * 0.35 + lighting.zenith[2] * 0.65, 1
    ], 20);
    for (let cascade = 0; cascade < SHADOW_CASCADES; cascade++) {
      const matrix = cascades.matrices[cascade] ?? this.math.mat4.identity();
      data.set(matrix, 32 + cascade * 16);
    }
    data.set(cascades.splits, 96);
    data.set([shadowEnabled ? 1 : 0, cascades.count, 1 / this.shadowSize,
      settings.shadow_softness ?? 1], 100);
    data.set([pointLights.length, shadowEnabled ? 1 : 0,
      (settings.pbr_enabled ?? true) ? 1 : 0, 0], 104);
    for (let i = 0; i < pointLights.length && i < 8; i++) {
      const light = pointLights[i];
      data.set([light.world[12], light.world[13], light.world[14], light.range], 108 + i * 8);
      data.set([light.color[0], light.color[1], light.color[2], light.intensity], 112 + i * 8);
    }
    data.set(this.math.mat4.inverse(viewProjection), 172);
    const steps = [6, 10, 16, 24][settings.atmosphere_quality ?? 2] ?? 16;
    const lightSteps = [1, 2, 3, 4][settings.atmosphere_quality ?? 2] ?? 3;
    data.set([settings.atmosphere_rayleigh ?? 1, settings.atmosphere_mie ?? 0.0035,
      settings.atmosphere_turbidity ?? 2.4, settings.atmosphere_mie_anisotropy ?? 0.76], 188);
    data.set([settings.atmosphere_sun_intensity ?? 20, steps, lightSteps,
      (settings.atmosphere_enabled ?? true) ? 1 : 0], 192);
    data.set([settings.fog_density ?? 0.008, settings.fog_height_falloff ?? 0.12,
      (settings.fog_enabled ?? true) ? 1 : 0, atmosphereEnabled ? 1 : 0], 196);
    data.set([...lighting.fog, 0], 200);
    data.set([this.elapsedTime, 0, 0, 0], 204);
    data.set(cascades.texels, 208);
    data.set([cascades.far, cascades.far * 0.12, 0.12, 0], 212);
    data.set([...lighting.zenith, 1], 216);
    data.set([...lighting.horizon, 1], 220);
    data.set([...lighting.ground, 1], 224);
    // The disc is the sun seen through the atmosphere, far brighter than any
    // surface so bloom and exposure react to it.
    const disc = lighting.sunIntensity * 80;
    data.set([lighting.sunColor[0] * disc, lighting.sunColor[1] * disc,
      lighting.sunColor[2] * disc, 0], 228);
    this.device.queue.writeBuffer(gpu.buffer, 0, data);
    return gpu;
  }
  computeShadowCascades(camera, sun) {
    const count = cascadeCountForSettings(this.graphicsSettings);
    const result = {
      matrices: [
        this.math.mat4.identity(), this.math.mat4.identity(),
        this.math.mat4.identity(), this.math.mat4.identity()
      ],
      splits: [0, 0, 0, 0],
      texels: [0, 0, 0, 0],
      far: 0,
      count: 0
    };
    if (!sun || !sun.castShadows || !camera) {
      return result;
    }

    const settings = this.graphicsSettings ?? {};
    const vec3 = this.math.vec3;
    const direction = vec3.normalize([sun.world[8], sun.world[9], sun.world[10]]);
    const near = Math.max(0.05, camera.near ?? 0.1);
    let far = Math.min(camera.far ?? 300, settings.shadow_distance ?? 120);
    if (far <= near + 0.1) far = near + 0.1;

    // Practical split scheme. A higher lambda concentrates resolution close
    // to the camera, where shadow detail is actually visible.
    const splits = [];
    const lambda = 0.82;
    for (let i = 0; i < count; i++) {
      const p = (i + 1) / count;
      const logarithmic = near * Math.pow(far / near, p);
      const linear = near + (far - near) * p;
      splits.push(Math.min(far, lambda * logarithmic + (1 - lambda) * linear));
    }

    // Light basis. It only depends on the sun direction, so the shadow texels
    // keep a fixed orientation in the world while the camera moves.
    const referenceUp = Math.abs(direction[1]) > 0.99 ? [0, 0, 1] : [0, 1, 0];
    const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
    const lightRight = vec3.normalize(cross(referenceUp, direction));
    const lightUp = cross(direction, lightRight);

    const world = camera.world;
    const cameraPosition = [world[12], world[13], world[14]];
    const cameraForward = vec3.normalize([-world[8], -world[9], -world[10]]);
    const tanY = Math.tan((camera.fov ?? Math.PI / 3) * 0.5);
    const tanX = tanY * (camera.aspect ?? 1);
    const k = tanX * tanX + tanY * tanY;

    // Distance kept between the cascade volume and the light so tall casters
    // outside the camera frustum still reach the shadow map.
    const pullback = 150;

    for (let cascade = 0; cascade < count; cascade++) {
      const n = cascade === 0 ? near : splits[cascade - 1];
      const f = splits[cascade];

      // Minimal bounding sphere of the frustum slice. Its radius does not
      // change when the camera rotates, which (together with the texel
      // snapping below) removes the shimmering of the shadow edges.
      let centerDistance, radius;
      if (k >= (f - n) / (f + n)) {
        centerDistance = f;
        radius = f * Math.sqrt(k);
      } else {
        centerDistance = 0.5 * (f + n) * (1 + k);
        radius = 0.5 * Math.sqrt((f - n) * (f - n) + 2 * (f * f + n * n) * k + (f + n) * (f + n) * k * k);
      }
      radius = Math.max(0.5, Math.ceil(radius * 16) / 16);

      let center = [
        cameraPosition[0] + cameraForward[0] * centerDistance,
        cameraPosition[1] + cameraForward[1] * centerDistance,
        cameraPosition[2] + cameraForward[2] * centerDistance
      ];

      // Snap the volume to whole shadow texels in light space.
      const texel = radius * 2 / this.shadowSize;
      const alongRight = Math.floor(vec3.dot(center, lightRight) / texel) * texel;
      const alongUp = Math.floor(vec3.dot(center, lightUp) / texel) * texel;
      const alongSun = vec3.dot(center, direction);
      center = [0, 1, 2].map((i) => lightRight[i] * alongRight + lightUp[i] * alongUp + direction[i] * alongSun);

      const lightPosition = center.map((value, i) => value + direction[i] * (radius + pullback));
      const lightView = this.math.mat4.lookAt(lightPosition, center, lightUp);
      const lightProjection = this.math.mat4.ortho(-radius, radius, -radius, radius, 0, radius * 2 + pullback);
      const matrix = this.math.mat4.multiply(lightProjection, lightView);
      result.matrices[cascade] = matrix;
      result.texels[cascade] = texel;
      this.device.queue.writeBuffer(this.shadowGlobals[cascade], 0, new Float32Array(matrix));
    }

    for (let cascade = count; cascade < SHADOW_CASCADES; cascade++) {
      splits.push(splits[count - 1]);
    }
    result.splits = splits.slice(0, SHADOW_CASCADES);
    result.far = far;
    result.count = count;
    return result;
  }
  render(dt = 1 / 60) {
    this.elapsedTime += dt;
    this.resize();
    const scene = this.readScene();
    // Reclaim buffers when components disappear or become inactive.
    const prune = (cache, items) => {
      const live = new Set(items.map((item) => item.component));
      for (const [key, value] of cache) {
        if (!live.has(key)) {
          value.buffer.destroy();
          cache.delete(key);
        }
      }
    };
    const spriteInstances = scene.sprites.flatMap((sprite) => scene.cameras
      .filter((camera) => camera.domain === 2 && sprite.domain === 2)
      .map((camera) => ({ component: `${sprite.component}:${camera.component}` })));
    prune(this.objectGpu, [...scene.models, ...scene.sprites, ...spriteInstances]);
    prune(this.cameraGpu, scene.cameras);
    prune(this.particleGpu, scene.particles);
    prune(this.animatedGpu, scene.models.filter((item) => item.animation && this.meshes.get(item.mesh)?.animated));
    for (const emitter of scene.particles) {
      this.simulateParticles(emitter, dt);
    }
    const sun = scene.lights.find((light) => light.type === LIGHT_DIRECTIONAL);
    const pointLights = scene.lights.filter((light) => light.type === LIGHT_POINT).slice(0, 8);
    const camera = scene.cameras.find((camera) => camera.domain === 0) ?? scene.cameras[0];
    if (!camera || !this.depthView) {
      return;
    }
    const cascades = this.computeShadowCascades(camera, sun);
    const shadowEnabled = (this.graphicsSettings?.shadows_enabled ?? true) &&
      !!sun?.castShadows && cascades.count > 0;
    this.stats.objects = scene.models.length;
    this.stats.cameras = scene.cameras.length;
    this.stats.drawCalls = 0;
    this.stats.shadowCasters = 0;
    for (const item of scene.models) {
      const mesh = this.meshes.get(item.mesh);
      if (mesh?.animated && item.animation) {
        let animation = this.animatedGpu.get(item.component);
        if (animation && animation.mesh !== item.mesh) {
          animation.buffer.destroy();
          this.animatedGpu.delete(item.component);
          animation = null;
        }
        if (!animation) {
          animation = { mesh: item.mesh, buffer: this.device.createBuffer({
            label: `egn-animated-${item.component}`,
            size: Math.max(16, mesh.baseVertices.byteLength),
            usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST
          }) };
          this.device.queue.writeBuffer(animation.buffer, 0, mesh.baseVertices);
          this.animatedGpu.set(item.component, animation);
        }
        this.updateMeshAnimation(mesh, item, animation.buffer);
      }
      this.objectBuffer(item, mesh);
    }
    for (const sprite of scene.sprites) this.objectBuffer(sprite);
    const encoder = this.device.createCommandEncoder({ label: "egn-frame" });
    if (shadowEnabled) {
      for (let cascade = 0; cascade < cascades.count; cascade++) {
        const pass = encoder.beginRenderPass({
          label: `egn-shadow-pass-${cascade}`,
          colorAttachments: [],
          depthStencilAttachment: {
            view: this.shadowViews[cascade],
            depthClearValue: 1,
            depthLoadOp: "clear",
            depthStoreOp: "store"
          }
        });
        pass.setPipeline(this.shadowPipeline);
        pass.setBindGroup(0, this.shadowBindGroups[cascade]);
        for (const item of scene.models) {
          if (!item.castShadows || item.domain !== 0) continue;
          const mesh = this.meshes.get(item.mesh);
          const objectGpu = this.objectGpu.get(item.component);
          if (!mesh || !objectGpu) continue;
          pass.setBindGroup(1, objectGpu.bindGroup);
          pass.setVertexBuffer(0, this.animatedGpu.get(item.component)?.buffer ?? mesh.vertexBuffer);
          pass.setIndexBuffer(mesh.indexBuffer, "uint32");
          pass.drawIndexed(mesh.indexCount);
          this.stats.shadowCasters++;
        }
        pass.end();
      }
    }
    this.updateAtmosphere(encoder, sun, camera);
    const frameBindGroups = /* @__PURE__ */ new Map();
    for (const cam of scene.cameras) {
      frameBindGroups.set(cam, this.cameraFrame(cam, cascades, sun, pointLights, shadowEnabled).bindGroup);
    }
    let first = true;
    for (const cam of scene.cameras) {
      const x = Math.floor(cam.viewport[0] * this.canvas.width);
      const y = Math.floor((1 - cam.viewport[1] - cam.viewport[3]) * this.canvas.height);
      const width = Math.max(1, Math.floor(cam.viewport[2] * this.canvas.width));
      const height = Math.max(1, Math.floor(cam.viewport[3] * this.canvas.height));
      const pass = encoder.beginRenderPass({
        label: "egn-main-pass",
        colorAttachments: [
          {
            view: this.hdrView,
            clearValue: { r: cam.clearColor[0], g: cam.clearColor[1], b: cam.clearColor[2], a: cam.clearColor[3] },
            loadOp: first || cam.clearColorEnabled ? "clear" : "load",
            storeOp: "store"
          },
          {
            view: this.normalView,
            clearValue: { r: 0, g: 0, b: 0, a: 0 },
            loadOp: first ? "clear" : "load",
            storeOp: "store"
          }
        ],
        depthStencilAttachment: {
          view: this.depthView,
          depthClearValue: 1,
          depthLoadOp: first || cam.clearDepth ? "clear" : "load",
          depthStoreOp: "store"
        }
      });
      pass.setViewport(x, y, width, height, 0, 1);
      pass.setScissorRect(x, y, width, height);
      if (cam.domain === 0 && (this.graphicsSettings?.atmosphere_enabled ?? true)) {
        pass.setPipeline(this.skyPipeline);
        pass.setBindGroup(0, frameBindGroups.get(cam));
        pass.draw(3);
      }
      pass.setPipeline(this.mainPipeline);
      pass.setBindGroup(0, frameBindGroups.get(cam));
      for (const item of scene.models) {
        if (cam.domain !== 0 || item.domain !== 0) continue;
        const layerVisible = item.layer < 32 && (cam.layerMask & 1 << item.layer) !== 0;
        if (!layerVisible) continue;
        const mesh = this.meshes.get(item.mesh);
        const objectGpu = this.objectGpu.get(item.component);
        if (!mesh || !objectGpu) continue;
        const texture = this.textures.get(item.texture || mesh.defaultTexture) ?? this.solidTexture;
        pass.setBindGroup(1, objectGpu.bindGroup);
        pass.setBindGroup(2, texture.bindGroup);
        pass.setBindGroup(3, this.materialBindGroup(mesh));
        pass.setVertexBuffer(0, this.animatedGpu.get(item.component)?.buffer ?? mesh.vertexBuffer);
        pass.setIndexBuffer(mesh.indexBuffer, "uint32");
        pass.drawIndexed(mesh.indexCount);
        this.stats.drawCalls++;
      }

      for (const emitter of scene.particles) {
        if (cam.domain !== 0) continue;
        const layerVisible = emitter.layer < 32 && (cam.layerMask & (1 << emitter.layer)) !== 0;
        if (!layerVisible) continue;
        const count = this.buildParticleVertices(emitter, cam);
        if (count === 0) continue;
        const state = this.particleGpu.get(emitter.component);
        const texture = this.textures.get(emitter.texture) ?? this.solidTexture;
        pass.setPipeline(this.particlePipeline);
        pass.setBindGroup(0, frameBindGroups.get(cam));
        pass.setBindGroup(1, texture.bindGroup);
        pass.setVertexBuffer(0, state.buffer);
        pass.draw(count * 6);
        this.stats.drawCalls++;
      }

      {
        pass.setPipeline(this.spritePipeline);
        pass.setBindGroup(0, frameBindGroups.get(cam));
        for (const sprite of scene.sprites) {
          if (sprite.domain !== cam.domain || sprite.layer >= 32 || !(cam.layerMask & (1 << sprite.layer))) continue;
          let item = sprite;
          if (cam.domain === 2) {
            const ax = (sprite.anchor % 3 - 1) * width * 0.5;
            const ay = (Math.floor(sprite.anchor / 3) - 1) * height * 0.5;
            item = { ...sprite, component: `${sprite.component}:${cam.component}`, world: this.math.mat4.multiply(this.math.mat4.translation([ax, ay, 0]), sprite.world) };
          }
          const gpu = this.objectBuffer(item);
          const mesh = this.meshes.get(3);
          const texture = this.textures.get(sprite.texture) ?? this.solidTexture;
          pass.setBindGroup(1, gpu.bindGroup);
          pass.setBindGroup(2, texture.bindGroup);
          pass.setBindGroup(3, this.defaultMaterialBindGroup);
          pass.setVertexBuffer(0, mesh.vertexBuffer);
          pass.setIndexBuffer(mesh.indexBuffer, "uint32");
          pass.drawIndexed(mesh.indexCount);
          this.stats.drawCalls++;
        }
      }
      pass.end();
      first = false;
    }

    const settings = this.graphicsSettings ?? {};
    if (camera && (settings.ao_enabled || settings.gi_enabled)) {
      const viewProjection = this.math.mat4.multiply(camera.projection, camera.view);
      const quality = Math.max(settings.ao_quality ?? 2, settings.gi_quality ?? 2);
      const samples = [8, 12, 16, 24][quality] ?? 16;
      const effectsData = new Float32Array(44);
      effectsData.set(this.math.mat4.inverse(viewProjection), 0);
      effectsData.set(viewProjection, 16);
      effectsData.set([camera.world[12], camera.world[13], camera.world[14], 1], 32);
      effectsData.set([settings.ao_radius ?? 1, settings.ao_intensity ?? 0.7,
        settings.gi_intensity ?? 0.55, settings.gi_distance ?? 12], 36);
      effectsData.set([samples, 1 / Math.tan((camera.fov ?? Math.PI / 3) * 0.5), camera.aspect ?? 1,
        (settings.ao_enabled ? 1 : 0) | (settings.gi_enabled ? 2 : 0)], 40);
      this.device.queue.writeBuffer(this.effectsUniformBuffer, 0, effectsData);

      const effectsPass = encoder.beginRenderPass({
        label: "egn-effects-pass",
        colorAttachments: [{
          view: this.effectsView,
          clearValue: { r: 0, g: 0, b: 0, a: 1 },
          loadOp: "clear",
          storeOp: "store"
        }]
      });
      effectsPass.setPipeline(this.effectsPipeline);
      effectsPass.setBindGroup(0, this.effectsBindGroup);
      effectsPass.draw(3);
      effectsPass.end();
    }

    const bloomMips = this.bloomMipCount(settings);
    if (settings.bloom_enabled && bloomMips > 0) {
      const manualExposure = Math.pow(2, settings.exposure_compensation ?? 0);
      const upRadius = 0.6 + (settings.bloom_radius ?? 0.55);
      const bloomPass = (target, pipeline, bindGroup, load) => {
        const pass = encoder.beginRenderPass({
          label: "egn-bloom",
          colorAttachments: [{ view: target.view, loadOp: load ? "load" : "clear", storeOp: "store",
            clearValue: { r: 0, g: 0, b: 0, a: 1 } }]
        });
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bindGroup);
        pass.draw(3);
        pass.end();
      };

      // Downsample chain: scene -> mip 0 (thresholded) -> mip 1 -> ...
      for (let i = 0; i < bloomMips; i++) {
        const mip = this.bloomMips[i];
        const sourceWidth = i === 0 ? this.canvas.width : this.bloomMips[i - 1].width;
        const sourceHeight = i === 0 ? this.canvas.height : this.bloomMips[i - 1].height;
        this.device.queue.writeBuffer(mip.downBuffer, 0, new Float32Array([
          1 / sourceWidth, 1 / sourceHeight, i === 0 ? 0 : 1, 0,
          settings.bloom_threshold ?? 1.1, manualExposure, settings.auto_exposure_enabled ? 1 : 0, 0
        ]));
        bloomPass(mip, this.bloomPipeline, mip.downBindGroup, false);
      }
      // Upsample chain: each mip is blurred and added onto the next larger
      // one, so mip 0 ends up holding the sum of every blur radius.
      for (let i = bloomMips - 2; i >= 0; i--) {
        const mip = this.bloomMips[i];
        const source = this.bloomMips[i + 1];
        this.device.queue.writeBuffer(mip.upBuffer, 0, new Float32Array([
          1 / source.width, 1 / source.height, 2, upRadius, 0, 0, 0, 0
        ]));
        bloomPass(mip, this.bloomUpPipeline, mip.upBindGroup, true);
      }
    }

    if (settings.auto_exposure_enabled) {
      const quality = settings.auto_exposure_quality ?? 2;
      const grid = [64, 96, 128, 192][quality] ?? 128;
      const exposureData = new Float32Array([
        dt, settings.auto_exposure_speed ?? 1.5,
        settings.auto_exposure_min_ev ?? -6,
        settings.auto_exposure_max_ev ?? 6,
        grid, 0, 1, 0
      ]);
      this.device.queue.writeBuffer(this.exposureUniformBuffer, 0, exposureData);
      const computePass = encoder.beginComputePass({ label: "egn-exposure" });
      computePass.setPipeline(this.exposurePipeline);
      computePass.setBindGroup(0, this.exposureBindGroup);
      computePass.dispatchWorkgroups(1);
      computePass.end();
    }

    this.updatePostUniforms();
    const postPass = encoder.beginRenderPass({
      label: "egn-post-pass",
      colorAttachments: [{
        view: this.context.getCurrentTexture().createView(),
        clearValue: { r: 0, g: 0, b: 0, a: 1 },
        loadOp: "clear",
        storeOp: "store"
      }]
    });
    postPass.setPipeline(this.postPipeline);
    postPass.setBindGroup(0, this.postBindGroup);
    postPass.draw(3, 1, 0, 0);
    postPass.end();

    this.device.queue.submit([encoder.finish()]);
  }
  applyGraphicsSettings(settings) {
    this.graphicsSettings = settings;
    const size = shadowSizeForQuality(settings?.shadow_quality ?? 2);
    if (size !== this.shadowSize) {
      this.shadowSize = size;
      this.createShadowResources();
    }
    const lut = settings?.lut_enabled ? (settings.lut_texture ?? 0) : 0;
    if (lut !== this.currentLutHandle) {
      this.currentLutHandle = lut;
      if (this.hdrView) this.createPostBindGroup();
    }
  }
  updatePostUniforms() {
    const s = this.graphicsSettings ?? {};
    const grading = !!s.color_grading_enabled;
    const data = new Float32Array(32);
    data[0] = Math.pow(2, s.exposure_compensation ?? 0);
    data[1] = s.tone_mapper ?? 1;
    data[2] = s.tone_mapping_enabled ?? true ? 1 : 0;
    data[3] = s.hdr_enabled ?? true ? 1 : 0;
    data[4] = grading ? (s.contrast ?? 1) : 1;
    data[5] = grading ? (s.saturation ?? 1) : 1;
    data[6] = grading ? (s.temperature ?? 0) : 0;
    data[7] = grading ? (s.tint ?? 0) : 0;
    data[8] = grading ? (s.lift_r ?? 0) : 0;
    data[9] = grading ? (s.lift_g ?? 0) : 0;
    data[10] = grading ? (s.lift_b ?? 0) : 0;
    data[11] = grading ? (s.vignette ?? 0) : 0;
    data[12] = grading ? (s.gamma_r ?? 1) : 1;
    data[13] = grading ? (s.gamma_g ?? 1) : 1;
    data[14] = grading ? (s.gamma_b ?? 1) : 1;
    data[15] = s.lut_strength ?? 1;
    data[16] = grading ? (s.gain_r ?? 1) : 1;
    data[17] = grading ? (s.gain_g ?? 1) : 1;
    data[18] = grading ? (s.gain_b ?? 1) : 1;
    data[19] = s.lut_enabled ? 1 : 0;
    data[20] = s.ao_enabled ? 1 : 0;
    data[21] = s.gi_enabled ? 1 : 0;
    data[22] = s.gi_intensity ?? 0.55;
    data[23] = s.ao_intensity ?? 0.7;
    // The chain sums one blur per mip; normalise so the intensity setting
    // keeps its meaning whatever the quality level.
    const bloomMips = this.bloomMipCount(s);
    data[24] = s.bloom_enabled && bloomMips > 0 ? 1 : 0;
    data[25] = (s.bloom_intensity ?? 0.055) * (bloomMips > 0 ? 4 / bloomMips : 0);
    data[26] = s.bloom_threshold ?? 1.1;
    data[27] = s.bloom_radius ?? 0.55;
    data[28] = s.auto_exposure_enabled ? 1 : 0;
    data[29] = Math.pow(2, s.exposure_compensation ?? 0);
    data[30] = 0;
    data[31] = 0;
    this.device.queue.writeBuffer(this.postUniformBuffer, 0, data);
  }
  destroy() {
    this.depthTexture?.destroy();
    this.hdrTexture?.destroy();
    this.normalTexture?.destroy();
    this.effectsTexture?.destroy();
    for (const mip of this.bloomMips ?? []) {
      mip.texture.destroy();
      mip.downBuffer.destroy();
      mip.upBuffer.destroy();
    }
    this.skyLutTexture?.destroy();
    this.skyLutUniformBuffer?.destroy();
    this.flatNormalTexture?.destroy();
    this.blackTexture?.destroy();
    this.postUniformBuffer?.destroy();
    this.exposureUniformBuffer?.destroy();
    this.exposureBuffer?.destroy();
    this.shadowTexture?.destroy();
    for (const buffer of this.shadowGlobals) buffer.destroy();
    if (this.scratch) this.module._free(this.scratch);
    this.scratch = 0;
    this.math.destroy();
    for (const mesh of this.meshes.values()) {
      mesh.vertexBuffer.destroy();
      mesh.indexBuffer.destroy();
    }
    for (const texture of this.textures.values()) texture.texture.destroy();
    for (const cache of [this.objectGpu, this.cameraGpu, this.particleGpu, this.animatedGpu]) {
      for (const resource of cache.values()) resource.buffer.destroy();
      cache.clear();
    }
    this.meshes.clear();
    this.textures.clear();
  }
}
export {
  WebGPURenderer
};
