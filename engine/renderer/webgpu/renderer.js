import { WasmMath } from "./wasm_math.js";
import { createCube, createSphere, createQuad, VERTEX_STRIDE } from "./meshes.js";
import { sceneShader as sceneShaderSource } from "./shaders/scene.js";
import { shadowShader as shadowShaderSource } from "./shaders/shadow.js";
const COMPONENT_CAMERA = 0;
const COMPONENT_MODEL = 1;
const COMPONENT_LIGHT = 4;
const LIGHT_DIRECTIONAL = 0;
const LIGHT_POINT = 1;
const SHADOW_SIZE = 2048;
const GLOBALS_FLOATS = 116;
const OBJECT_FLOATS = 24;
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
  frameLayout;
  objectLayout;
  textureLayout;
  shadowFrameLayout;
  shadowPipelineLayout;
  mainPipelineLayout;
  solidTexture;
  meshes = /* @__PURE__ */ new Map();
  textures = /* @__PURE__ */ new Map();
  objectGpu = /* @__PURE__ */ new Map();
  cameraGpu = /* @__PURE__ */ new Map();
  depthTexture;
  depthView;
  shadowTexture;
  shadowView;
  shadowSampler;
  shadowGlobals;
  shadowBindGroup;
  scratch;
  ambient = [0.09, 0.1, 0.13, 1];
  stats = { objects: 0, drawCalls: 0, cameras: 0, shadowCasters: 0 };
  constructor(canvas, device, context, format, module) {
    this.canvas = canvas;
    this.device = device;
    this.context = context;
    this.format = format;
    this.module = module;
    this.scratch = module._malloc(256);
    this.math = new WasmMath(module);
    this.sceneModule = device.createShaderModule({ code: sceneShaderSource, label: "egn-scene-shader" });
    this.shadowModule = device.createShaderModule({ code: shadowShaderSource, label: "egn-shadow-shader" });
    this.frameLayout = device.createBindGroupLayout({
      label: "egn-frame-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth" } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "comparison" } }
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
    this.shadowFrameLayout = device.createBindGroupLayout({
      label: "egn-shadow-frame-layout",
      entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: "uniform" } }
      ]
    });
    this.mainPipelineLayout = device.createPipelineLayout({
      bindGroupLayouts: [this.frameLayout, this.objectLayout, this.textureLayout]
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
      fragment: { module: this.sceneModule, entryPoint: "fs_main", targets: [{ format }] },
      primitive: { topology: "triangle-list", cullMode: "none", frontFace: "ccw" },
      depthStencil: { format: "depth24plus", depthWriteEnabled: true, depthCompare: "less" }
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
        depthBias: 2,
        depthBiasSlopeScale: 2
      }
    });
    this.createShadowResources();
    this.registerBuiltinMeshes();
    this.createSolidTexture();
    this.resize();
  }
  createShadowResources() {
    const device = this.device;
    this.shadowTexture = device.createTexture({
      label: "egn-shadow-map",
      size: [SHADOW_SIZE, SHADOW_SIZE],
      format: "depth32float",
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
    });
    this.shadowView = this.shadowTexture.createView();
    this.shadowSampler = device.createSampler({
      compare: "less-equal",
      magFilter: "linear",
      minFilter: "linear",
      addressModeU: "clamp-to-edge",
      addressModeV: "clamp-to-edge"
    });
    this.shadowGlobals = device.createBuffer({
      label: "egn-shadow-globals",
      size: 64,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });
    this.shadowBindGroup = device.createBindGroup({
      layout: this.shadowFrameLayout,
      entries: [{ binding: 0, resource: { buffer: this.shadowGlobals } }]
    });
  }
  createSolidTexture() {
    const texture = this.device.createTexture({
      label: "egn-white-texture",
      size: [1, 1],
      format: "rgba8unorm-srgb",
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
    });
    this.device.queue.writeTexture({ texture }, new Uint8Array([255, 255, 255, 255]), { bytesPerRow: 4 }, [1, 1]);
    const sampler = this.device.createSampler({ magFilter: "linear", minFilter: "linear" });
    const view = texture.createView();
    this.solidTexture = {
      id: 0,
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
    this.textures.set(0, this.solidTexture);
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
      positions: data.positions
    });
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
      format: "rgba8unorm-srgb",
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
  resize() {
    const dpr = window.devicePixelRatio || 1;
    const width = Math.max(1, Math.floor(this.canvas.clientWidth * dpr));
    const height = Math.max(1, Math.floor(this.canvas.clientHeight * dpr));
    if (this.canvas.width === width && this.canvas.height === height && this.depthTexture) {
      return;
    }
    this.canvas.width = width;
    this.canvas.height = height;
    this.depthTexture?.destroy();
    this.depthTexture = this.device.createTexture({
      label: "egn-depth",
      size: [width, height],
      format: "depth24plus",
      usage: GPUTextureUsage.RENDER_ATTACHMENT
    });
    this.depthView = this.depthTexture.createView();
  }
  floats(ptr, offset, count) {
    return new Float32Array(this.module.HEAPF32.buffer, ptr + offset * 4, count).slice();
  }
  readScene() {
    const m = this.module;
    const models = [];
    const cameras = [];
    const lights = [];
    const scratch = this.scratch;
    const walk = (object) => {
      if (!object || !m._egn_object_is_active_in_hierarchy(object)) {
        return;
      }
      const layer = m._egn_object_get_layer(object);
      m._egn_object_get_world_matrix(object, scratch);
      const world = this.floats(scratch, 0, 16);
      const modelCount = m._egn_object_component_count(object, COMPONENT_MODEL);
      for (let i = 0; i < modelCount; i++) {
        const component = m._egn_object_get_component_at(object, COMPONENT_MODEL, i);
        if (!component || !m._egn_component_is_enabled(component)) continue;
        m._egn_model_get_color(component, scratch, scratch + 4, scratch + 8, scratch + 12);
        const color = this.floats(scratch, 0, 4);
        models.push({
          object,
          component,
          layer,
          world: world.slice(),
          mesh: m._egn_model_get_mesh(component),
          texture: m._egn_model_get_texture(component),
          color,
          castShadows: !!m._egn_model_get_cast_shadows(component)
        });
      }
      const cameraCount = m._egn_object_component_count(object, COMPONENT_CAMERA);
      for (let i = 0; i < cameraCount; i++) {
        const component = m._egn_object_get_component_at(object, COMPONENT_CAMERA, i);
        if (!component || !m._egn_component_is_enabled(component)) continue;
        m._egn_camera_get_viewport(component, scratch, scratch + 4, scratch + 8, scratch + 12);
        const viewport = this.floats(scratch, 0, 4);
        m._egn_camera_get_clear_color(component, scratch, scratch + 4, scratch + 8, scratch + 12);
        const clearColor = this.floats(scratch, 0, 4);
        const view = this.math.mat4.inverse(world);
        const aspect = Math.max(1e-4, viewport[2] * this.canvas.width / Math.max(1, viewport[3] * this.canvas.height));
        const near = m._egn_camera_get_near(component);
        const far = m._egn_camera_get_far(component);
        let projection;
        if (m._egn_camera_get_projection(component) === 1) {
          const halfHeight = m._egn_camera_get_ortho_size(component) * 0.5;
          const halfWidth = halfHeight * aspect;
          projection = this.math.mat4.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, near, far);
        } else {
          projection = this.math.mat4.perspective(m._egn_camera_get_fov(component) * Math.PI / 180, aspect, near, far);
        }
        cameras.push({
          object,
          component,
          layer,
          layerMask: m._egn_camera_get_layer_mask(component),
          world,
          view,
          projection,
          viewport,
          clearColor,
          order: m._egn_camera_get_order(component)
        });
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
      const childCount = m._egn_object_child_count(object);
      for (let i = 0; i < childCount; i++) {
        walk(m._egn_object_child_at(object, i));
      }
    };
    walk(m._egn_scene_root());
    cameras.sort((a, b) => a.order - b.order);
    return { models, cameras, lights };
  }
  objectBuffer(item) {
    let gpu = this.objectGpu.get(item.component);
    if (!gpu) {
      const buffer = this.device.createBuffer({
        label: `egn-object-${item.component}`,
        size: OBJECT_FLOATS * 4,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
      });
      gpu = {
        buffer,
        bindGroup: this.device.createBindGroup({
          layout: this.objectLayout,
          entries: [{ binding: 0, resource: { buffer } }]
        })
      };
      this.objectGpu.set(item.component, gpu);
    }
    const data = new Float32Array(OBJECT_FLOATS);
    data.set(item.world, 0);
    data.set(item.color, 16);
    data[20] = item.castShadows ? 1 : 0;
    this.device.queue.writeBuffer(gpu.buffer, 0, data);
    return gpu;
  }
  cameraFrame(camera, shadowViewProjection, sun, pointLights, shadowEnabled) {
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
            { binding: 2, resource: this.shadowSampler }
          ]
        })
      };
      this.cameraGpu.set(camera.component, gpu);
    }
    const data = new Float32Array(GLOBALS_FLOATS);
    const viewProjection = this.math.mat4.multiply(camera.projection, camera.view);
    data.set(viewProjection, 0);
    data.set([camera.world[12], camera.world[13], camera.world[14], 1], 16);
    data.set(this.ambient, 20);
    data.set([0, -1, 0, 0], 24);
    data.set([0, 0, 0, 0], 28);
    if (sun) {
      const direction = this.math.vec3.normalize([sun.world[8], sun.world[9], sun.world[10]]);
      data.set([direction[0], direction[1], direction[2], sun.intensity], 24);
      data.set([sun.color[0], sun.color[1], sun.color[2], 0.68], 28);
    }
    data.set(shadowViewProjection, 32);
    data.set([pointLights.length, shadowEnabled && !!sun?.castShadows ? 1 : 0, 1 / SHADOW_SIZE, 1 / SHADOW_SIZE], 48);
    for (let i = 0; i < pointLights.length && i < 8; i++) {
      const light = pointLights[i];
      data.set([light.world[12], light.world[13], light.world[14], light.range], 52 + i * 8);
      data.set([light.color[0], light.color[1], light.color[2], light.intensity], 56 + i * 8);
    }
    this.device.queue.writeBuffer(gpu.buffer, 0, data);
    return gpu;
  }
  computeShadow(camera, sun) {
    if (!sun || !sun.castShadows) {
      return this.math.mat4.identity();
    }
    const direction = this.math.vec3.normalize([sun.world[8], sun.world[9], sun.world[10]]);
    const center = [0, 0, 0];
    const lightPosition = [
      center[0] - direction[0] * 45,
      center[1] - direction[1] * 45,
      center[2] - direction[2] * 45
    ];
    let up = [0, 1, 0];
    if (Math.abs(this.math.vec3.dot(direction, up)) > 0.99) {
      up = [0, 0, 1];
    }
    const view = this.math.mat4.lookAt(lightPosition, center, up);
    const projection = this.math.mat4.ortho(-25, 25, -25, 25, 0.1, 100);
    return this.math.mat4.multiply(projection, view);
  }
  render() {
    this.resize();
    const scene = this.readScene();
    const sun = scene.lights.find((light) => light.type === LIGHT_DIRECTIONAL);
    const pointLights = scene.lights.filter((light) => light.type === LIGHT_POINT).slice(0, 8);
    const camera = scene.cameras[0];
    if (!camera || !this.depthView) {
      return;
    }
    const shadowViewProjection = this.computeShadow(camera, sun);
    const shadowEnabled = !!sun?.castShadows;
    this.device.queue.writeBuffer(this.shadowGlobals, 0, shadowViewProjection);
    this.stats.objects = scene.models.length;
    this.stats.cameras = scene.cameras.length;
    this.stats.drawCalls = 0;
    this.stats.shadowCasters = 0;
    for (const item of scene.models) {
      this.objectBuffer(item);
    }
    const encoder = this.device.createCommandEncoder({ label: "egn-frame" });
    if (shadowEnabled) {
      const pass = encoder.beginRenderPass({
        label: "egn-shadow-pass",
        colorAttachments: [],
        depthStencilAttachment: {
          view: this.shadowView,
          depthClearValue: 1,
          depthLoadOp: "clear",
          depthStoreOp: "store"
        }
      });
      pass.setPipeline(this.shadowPipeline);
      pass.setBindGroup(0, this.shadowBindGroup);
      for (const item of scene.models) {
        if (!item.castShadows) continue;
        const mesh = this.meshes.get(item.mesh);
        const objectGpu = this.objectGpu.get(item.component);
        if (!mesh || !objectGpu) continue;
        pass.setBindGroup(1, objectGpu.bindGroup);
        pass.setVertexBuffer(0, mesh.vertexBuffer);
        pass.setIndexBuffer(mesh.indexBuffer, "uint32");
        pass.drawIndexed(mesh.indexCount);
        this.stats.shadowCasters++;
      }
      pass.end();
    }
    const frameBindGroups = /* @__PURE__ */ new Map();
    for (const cam of scene.cameras) {
      frameBindGroups.set(cam, this.cameraFrame(cam, shadowViewProjection, sun, pointLights, shadowEnabled).bindGroup);
    }
    let first = true;
    for (const cam of scene.cameras) {
      const x = Math.floor(cam.viewport[0] * this.canvas.width);
      const y = Math.floor((1 - cam.viewport[1] - cam.viewport[3]) * this.canvas.height);
      const width = Math.max(1, Math.floor(cam.viewport[2] * this.canvas.width));
      const height = Math.max(1, Math.floor(cam.viewport[3] * this.canvas.height));
      const pass = encoder.beginRenderPass({
        label: "egn-main-pass",
        colorAttachments: [{
          view: this.context.getCurrentTexture().createView(),
          clearValue: { r: cam.clearColor[0], g: cam.clearColor[1], b: cam.clearColor[2], a: cam.clearColor[3] },
          loadOp: first ? "clear" : "load",
          storeOp: "store"
        }],
        depthStencilAttachment: {
          view: this.depthView,
          depthClearValue: 1,
          depthLoadOp: first ? "clear" : "load",
          depthStoreOp: "store"
        }
      });
      pass.setViewport(x, y, width, height, 0, 1);
      pass.setScissorRect(x, y, width, height);
      pass.setPipeline(this.mainPipeline);
      pass.setBindGroup(0, frameBindGroups.get(cam));
      for (const item of scene.models) {
        const layerVisible = item.layer < 32 && (cam.layerMask & 1 << item.layer) !== 0;
        if (!layerVisible) continue;
        const mesh = this.meshes.get(item.mesh);
        const objectGpu = this.objectGpu.get(item.component);
        if (!mesh || !objectGpu) continue;
        const texture = this.textures.get(item.texture) ?? this.solidTexture;
        pass.setBindGroup(1, objectGpu.bindGroup);
        pass.setBindGroup(2, texture.bindGroup);
        pass.setVertexBuffer(0, mesh.vertexBuffer);
        pass.setIndexBuffer(mesh.indexBuffer, "uint32");
        pass.drawIndexed(mesh.indexCount);
        this.stats.drawCalls++;
      }
      pass.end();
      first = false;
    }
    this.device.queue.submit([encoder.finish()]);
  }
  destroy() {
    this.depthTexture?.destroy();
    this.shadowTexture?.destroy();
    this.module._free(this.scratch);
    for (const mesh of this.meshes.values()) {
      mesh.vertexBuffer.destroy();
      mesh.indexBuffer.destroy();
    }
    for (const texture of this.textures.values()) {
      texture.view = void 0;
    }
    this.meshes.clear();
    this.textures.clear();
    this.objectGpu.clear();
    this.cameraGpu.clear();
  }
}
export {
  WebGPURenderer
};
