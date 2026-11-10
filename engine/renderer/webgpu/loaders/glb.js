import { VERTEX_FLOATS } from "../meshes.js";

let mat4 = null;

const COMPONENT_SIZE = { 5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4 };
const TYPE_COMPONENTS = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT2: 4, MAT3: 9, MAT4: 16 };

function readComponent(view, offset, componentType, normalized) {
  switch (componentType) {
    case 5120: return normalized ? Math.max(view.getInt8(offset) / 127, -1) : view.getInt8(offset);
    case 5121: return normalized ? view.getUint8(offset) / 255 : view.getUint8(offset);
    case 5122: return normalized ? Math.max(view.getInt16(offset, true) / 32767, -1) : view.getInt16(offset, true);
    case 5123: return normalized ? view.getUint16(offset, true) / 65535 : view.getUint16(offset, true);
    case 5125: return view.getUint32(offset, true);
    case 5126: return view.getFloat32(offset, true);
    default: throw new Error(`unsupported glTF component type ${componentType}`);
  }
}

function accessorData(json, bin, index) {
  const accessor = json.accessors[index];
  if (!accessor || accessor.bufferView === undefined) {
    throw new Error("glTF accessor without bufferView is not supported");
  }
  const bufferView = json.bufferViews[accessor.bufferView];
  if (!bufferView) throw new Error("invalid glTF bufferView");
  const components = TYPE_COMPONENTS[accessor.type];
  const componentSize = COMPONENT_SIZE[accessor.componentType];
  const stride = bufferView.byteStride ?? components * componentSize;
  const base = (bufferView.byteOffset ?? 0) + (accessor.byteOffset ?? 0);
  const output = accessor.componentType === 5125
    ? new Uint32Array(accessor.count * components)
    : new Float32Array(accessor.count * components);
  for (let i = 0; i < accessor.count; i++) {
    for (let c = 0; c < components; c++) {
      output[i * components + c] = readComponent(
        bin,
        base + i * stride + c * componentSize,
        accessor.componentType,
        accessor.normalized ?? false,
      );
    }
  }
  return output;
}

function nodeRecord(node) {
  return {
    parent: -1,
    translation: node.translation ? Array.from(node.translation) : [0, 0, 0],
    rotation: node.rotation ? Array.from(node.rotation) : [0, 0, 0, 1],
    scale: node.scale ? Array.from(node.scale) : [1, 1, 1],
    matrix: node.matrix ? new Float32Array(node.matrix) : null,
  };
}

function localMatrix(node) {
  if (node.matrix) return new Float32Array(node.matrix);
  let result = new Float32Array(mat4.identity());
  result = mat4.multiply(result, mat4.translation(node.translation));
  result = mat4.multiply(result, mat4.fromQuat(node.rotation));
  result = mat4.multiply(result, mat4.scaling(node.scale));
  return result;
}

function transformPoint(m, x, y, z) {
  return [
    m[0] * x + m[4] * y + m[8] * z + m[12],
    m[1] * x + m[5] * y + m[9] * z + m[13],
    m[2] * x + m[6] * y + m[10] * z + m[14],
  ];
}

function transformDirection(m, x, y, z) {
  return [
    m[0] * x + m[4] * y + m[8] * z,
    m[1] * x + m[5] * y + m[9] * z,
    m[2] * x + m[6] * y + m[10] * z,
  ];
}

function normalize(v) {
  const length = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / length, v[1] / length, v[2] / length];
}

function readIndices(json, bin, primitive, vertexCount) {
  if (primitive.indices === undefined) {
    const indices = new Uint32Array(vertexCount);
    for (let i = 0; i < vertexCount; i++) indices[i] = i;
    return indices;
  }
  const raw = accessorData(json, bin, primitive.indices);
  const indices = new Uint32Array(raw.length);
  for (let i = 0; i < raw.length; i++) indices[i] = raw[i];
  return indices;
}

function readAnimations(json, bin) {
  const animations = [];
  for (const animation of json.animations ?? []) {
    const channels = [];
    let duration = 0;
    for (const channel of animation.channels ?? []) {
      if (channel.target?.node === undefined || channel.sampler === undefined) continue;
      const path = channel.target.path;
      if (path !== "translation" && path !== "rotation" && path !== "scale") continue;
      const sampler = animation.samplers?.[channel.sampler];
      if (!sampler || sampler.input === undefined || sampler.output === undefined) continue;
      const times = accessorData(json, bin, sampler.input);
      const values = accessorData(json, bin, sampler.output);
      const components = path === "rotation" ? 4 : 3;
      channels.push({
        node: channel.target.node,
        path,
        interpolation: sampler.interpolation ?? "LINEAR",
        times,
        values,
        components,
      });
      const last = times[times.length - 1] ?? 0;
      if (last > duration) duration = last;
    }
    animations.push({ name: animation.name ?? `Animation ${animations.length}`, duration, channels });
  }
  return animations;
}

function textureBlob(json, bin, textureInfo) {
  if (!textureInfo || textureInfo.index === undefined) return undefined;
  const texture = json.textures?.[textureInfo.index];
  const imageIndex = texture?.source;
  const gltfImage = imageIndex !== undefined ? json.images?.[imageIndex] : undefined;
  if (gltfImage?.bufferView === undefined) return undefined;
  const imageView = json.bufferViews[gltfImage.bufferView];
  if (!imageView) return undefined;
  const bytes = new Uint8Array(bin.buffer, bin.byteOffset + (imageView.byteOffset ?? 0), imageView.byteLength).slice();
  return new Blob([bytes.buffer], { type: gltfImage.mimeType ?? "image/png" });
}

function readMaterial(json, bin, primitive) {
  const material = primitive.material !== undefined ? json.materials?.[primitive.material] : undefined;
  const pbr = material?.pbrMetallicRoughness;
  const emissive = material?.emissiveFactor ?? [0, 0, 0];
  return {
    images: {
      baseColor: textureBlob(json, bin, pbr?.baseColorTexture),
      metallicRoughness: textureBlob(json, bin, pbr?.metallicRoughnessTexture),
      normal: textureBlob(json, bin, material?.normalTexture),
      occlusion: textureBlob(json, bin, material?.occlusionTexture),
      emissive: textureBlob(json, bin, material?.emissiveTexture),
    },
    metallic: pbr?.metallicFactor ?? 1,
    roughness: pbr?.roughnessFactor ?? 1,
    metallicRoughnessTexture: !!pbr?.metallicRoughnessTexture,
    normalTexture: !!material?.normalTexture,
    occlusionTexture: !!material?.occlusionTexture,
    emissiveTexture: !!material?.emissiveTexture,
    emissive: [emissive[0], emissive[1], emissive[2], 1],
    baseColorFactor: pbr?.baseColorFactor ?? null,
  };
}

function parseGLB(buffer, math) {
  mat4 = math.mat4;
  const view = new DataView(buffer);
  if (view.getUint32(0, true) !== 0x46546c67) throw new Error("not a GLB file");
  const version = view.getUint32(4, true);
  if (version !== 2) throw new Error(`unsupported GLB version ${version}`);

  let offset = 12;
  let json;
  let bin;
  const length = view.getUint32(8, true);
  while (offset < length) {
    const chunkLength = view.getUint32(offset, true);
    const chunkType = view.getUint32(offset + 4, true);
    const chunk = new Uint8Array(buffer, offset + 8, chunkLength);
    if (chunkType === 0x4e4f534a) {
      json = JSON.parse(new TextDecoder().decode(chunk).replace(/\0+$/g, ""));
    } else if (chunkType === 0x004e4942) {
      bin = new DataView(buffer, offset + 8, chunkLength);
    }
    offset += 8 + chunkLength;
  }
  if (!json || !json.meshes?.length) throw new Error("GLB has no meshes");
  if (!bin) throw new Error("GLB has no binary chunk");

  const nodes = json.nodes.map(nodeRecord);
  for (let i = 0; i < json.nodes.length; i++) {
    for (const child of json.nodes[i].children ?? []) {
      if (nodes[child]) nodes[child].parent = i;
    }
  }

  const sceneIndex = json.scene ?? 0;
  const sceneNodes = json.scenes?.[sceneIndex]?.nodes ?? json.nodes.map((_, i) => i);
  const nodeWorlds = new Map();
  const visit = (index, parent) => {
    const world = new Float32Array(mat4.multiply(parent, localMatrix(nodes[index])));
    nodeWorlds.set(index, world);
    for (const child of json.nodes[index]?.children ?? []) visit(child, world);
  };
  for (const root of sceneNodes) visit(root, new Float32Array(mat4.identity()));

  const vertices = [];
  const indices = [];
  const skinJoints = [];
  const skinWeights = [];
  let materialInfo;
  let skin = null;

  for (let nodeIndex = 0; nodeIndex < json.nodes.length; nodeIndex++) {
    const node = json.nodes[nodeIndex];
    if (node.mesh === undefined) continue;
    const gltfMesh = json.meshes[node.mesh];
    const nodeWorld = nodeWorlds.get(nodeIndex) ?? new Float32Array(mat4.identity());
    const skinned = node.skin !== undefined && json.skins?.[node.skin] !== undefined;

    if (skinned && !skin) {
      const gltfSkin = json.skins[node.skin];
      const jointCount = gltfSkin.joints.length;
      const inverseBind = gltfSkin.inverseBindMatrices !== undefined
        ? accessorData(json, bin, gltfSkin.inverseBindMatrices)
        : new Float32Array(jointCount * 16);
      if (gltfSkin.inverseBindMatrices === undefined) {
        for (let i = 0; i < jointCount; i++) {
          inverseBind.set(mat4.identity(), i * 16);
        }
      }
      skin = {
        joints: gltfSkin.joints.slice(),
        inverseBind: new Float32Array(inverseBind),
      };
    }

    for (const primitive of gltfMesh.primitives) {
      if (primitive.attributes.POSITION === undefined) continue;
      const position = accessorData(json, bin, primitive.attributes.POSITION);
      const normal = primitive.attributes.NORMAL !== undefined
        ? accessorData(json, bin, primitive.attributes.NORMAL) : undefined;
      const uv = primitive.attributes.TEXCOORD_0 !== undefined
        ? accessorData(json, bin, primitive.attributes.TEXCOORD_0) : undefined;
      const joints = primitive.attributes.JOINTS_0 !== undefined
        ? accessorData(json, bin, primitive.attributes.JOINTS_0) : undefined;
      const weights = primitive.attributes.WEIGHTS_0 !== undefined
        ? accessorData(json, bin, primitive.attributes.WEIGHTS_0) : undefined;
      const indexData = readIndices(json, bin, primitive, position.length / 3);
      const baseVertex = vertices.length / VERTEX_FLOATS;

      for (let i = 0; i < position.length / 3; i++) {
        let p = [position[i * 3], position[i * 3 + 1], position[i * 3 + 2]];
        let n = normal ? [normal[i * 3], normal[i * 3 + 1], normal[i * 3 + 2]] : [0, 1, 0];
        if (!skinned) {
          p = transformPoint(nodeWorld, p[0], p[1], p[2]);
          n = normalize(transformDirection(nodeWorld, n[0], n[1], n[2]));
        }
        vertices.push(p[0], p[1], p[2], n[0], n[1], n[2],
          uv ? uv[i * 2] : 0, uv ? uv[i * 2 + 1] : 0);

        if (skinned && joints && weights) {
          skinJoints.push(joints[i * 4], joints[i * 4 + 1], joints[i * 4 + 2], joints[i * 4 + 3]);
          const w0 = weights[i * 4], w1 = weights[i * 4 + 1],
            w2 = weights[i * 4 + 2], w3 = weights[i * 4 + 3];
          const sum = w0 + w1 + w2 + w3 || 1;
          skinWeights.push(w0 / sum, w1 / sum, w2 / sum, w3 / sum);
        } else {
          skinJoints.push(0, 0, 0, 0);
          skinWeights.push(1, 0, 0, 0);
        }
      }

      for (const index of indexData) indices.push(baseVertex + index);
      if (!materialInfo && primitive.material !== undefined) materialInfo = readMaterial(json, bin, primitive);
    }
  }

  if (vertices.length === 0) throw new Error("GLB has no triangle geometry");

  const interleaved = new Float32Array(vertices);
  const collision = new Float32Array(interleaved.length / VERTEX_FLOATS * 3);
  for (let i = 0, p = 0; i < interleaved.length; i += VERTEX_FLOATS, p += 3) {
    collision[p] = interleaved[i];
    collision[p + 1] = interleaved[i + 1];
    collision[p + 2] = interleaved[i + 2];
  }

  return {
    data: {
      vertices: interleaved,
      indices: new Uint32Array(indices),
      positions: collision,
      nodes,
      skin,
      skinData: {
        joints: new Uint16Array(skinJoints),
        weights: new Float32Array(skinWeights),
      },
      animations: readAnimations(json, bin),
    },
    material: materialInfo,
  };
}

export { parseGLB };
