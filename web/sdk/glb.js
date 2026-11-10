import { VERTEX_FLOATS } from "../../engine/renderer/webgpu/meshes.js";
let mat4 = null;
const COMPONENT_SIZE = {
  5120: 1,
  5121: 1,
  5122: 2,
  5123: 2,
  5125: 4,
  5126: 4
};
const TYPE_COMPONENTS = {
  SCALAR: 1,
  VEC2: 2,
  VEC3: 3,
  VEC4: 4,
  MAT2: 4,
  MAT3: 9,
  MAT4: 16
};
function readComponent(view, offset, componentType, normalized) {
  switch (componentType) {
    case 5120:
      return normalized ? Math.max(view.getInt8(offset) / 127, -1) : view.getInt8(offset);
    case 5121:
      return normalized ? view.getUint8(offset) / 255 : view.getUint8(offset);
    case 5122:
      return normalized ? Math.max(view.getInt16(offset, true) / 32767, -1) : view.getInt16(offset, true);
    case 5123:
      return normalized ? view.getUint16(offset, true) / 65535 : view.getUint16(offset, true);
    case 5125:
      return view.getUint32(offset, true);
    case 5126:
      return view.getFloat32(offset, true);
    default:
      throw new Error(`unsupported glTF component type ${componentType}`);
  }
}
function accessorData(json, bin, index) {
  const accessor = json.accessors[index];
  if (!accessor || accessor.bufferView === void 0) {
    throw new Error("glTF accessor without bufferView is not supported");
  }
  const bufferView = json.bufferViews[accessor.bufferView];
  if (!bufferView) throw new Error("invalid glTF bufferView");
  const components = TYPE_COMPONENTS[accessor.type];
  const componentSize = COMPONENT_SIZE[accessor.componentType];
  const stride = bufferView.byteStride ?? components * componentSize;
  const base = (bufferView.byteOffset ?? 0) + (accessor.byteOffset ?? 0);
  const output = accessor.componentType === 5125 ? new Uint32Array(accessor.count * components) : new Float32Array(accessor.count * components);
  for (let i = 0; i < accessor.count; i++) {
    for (let c = 0; c < components; c++) {
      output[i * components + c] = readComponent(
        bin,
        base + i * stride + c * componentSize,
        accessor.componentType,
        accessor.normalized ?? false
      );
    }
  }
  return output;
}
function nodeMatrix(node) {
  if (node.matrix) {
    return new Float32Array(node.matrix);
  }
  let result = new Float32Array(mat4.identity());
  if (node.translation) result = mat4.multiply(result, mat4.translation(node.translation));
  if (node.rotation) result = mat4.multiply(result, mat4.fromQuat(node.rotation));
  if (node.scale) result = mat4.multiply(result, mat4.scaling(node.scale));
  return result;
}
function transformPoint(m, x, y, z) {
  return [
    m[0] * x + m[4] * y + m[8] * z + m[12],
    m[1] * x + m[5] * y + m[9] * z + m[13],
    m[2] * x + m[6] * y + m[10] * z + m[14]
  ];
}
function transformDirection(m, x, y, z) {
  return [
    m[0] * x + m[4] * y + m[8] * z,
    m[1] * x + m[5] * y + m[9] * z,
    m[2] * x + m[6] * y + m[10] * z
  ];
}
function normalize(v) {
  const length = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / length, v[1] / length, v[2] / length];
}
function parseGLB(buffer, math) {
  mat4 = math.mat4;
  const view = new DataView(buffer);
  if (view.getUint32(0, true) !== 1179937895) {
    throw new Error("not a GLB file");
  }
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
    if (chunkType === 1313821514) {
      json = JSON.parse(new TextDecoder().decode(chunk).replace(/\0+$/g, ""));
    } else if (chunkType === 5130562) {
      bin = new DataView(buffer, offset + 8, chunkLength);
    }
    offset += 8 + chunkLength;
  }
  if (!json || !json.meshes?.length) throw new Error("GLB has no meshes");
  if (!bin) throw new Error("GLB has no binary chunk");
  const sceneIndex = json.scene ?? 0;
  const sceneNodes = json.scenes?.[sceneIndex]?.nodes ?? json.nodes.map((_, i) => i);
  const nodeWorlds = /* @__PURE__ */ new Map();
  const visit = (index, parent) => {
    const world = new Float32Array(mat4.multiply(parent, nodeMatrix(json.nodes[index] ?? {})));
    nodeWorlds.set(index, world);
    for (const child of json.nodes[index]?.children ?? []) {
      visit(child, world);
    }
  };
  for (const root of sceneNodes) {
    visit(root, new Float32Array(mat4.identity()));
  }
  const vertices = [];
  const indices = [];
  let image;
  for (const [nodeIndex, node] of json.nodes.entries()) {
    if (node.mesh === void 0) continue;
    const mesh = json.meshes[node.mesh];
    const world = nodeWorlds.get(nodeIndex) ?? new Float32Array(mat4.identity());
    for (const primitive of mesh.primitives) {
      if (primitive.attributes.POSITION === void 0) continue;
      const position = accessorData(json, bin, primitive.attributes.POSITION);
      const normal = primitive.attributes.NORMAL !== void 0 ? accessorData(json, bin, primitive.attributes.NORMAL) : void 0;
      const uv = primitive.attributes.TEXCOORD_0 !== void 0 ? accessorData(json, bin, primitive.attributes.TEXCOORD_0) : void 0;
      let indexData;
      if (primitive.indices !== void 0) {
        const rawIndices = accessorData(json, bin, primitive.indices);
        indexData = new Uint32Array(rawIndices.length);
        for (let i = 0; i < rawIndices.length; i++) indexData[i] = rawIndices[i];
      } else {
        indexData = new Uint32Array(position.length / 3);
        for (let i = 0; i < indexData.length; i++) indexData[i] = i;
      }
      const baseVertex = vertices.length / VERTEX_FLOATS;
      const transformedPositions = [];
      for (let i = 0; i < position.length / 3; i++) {
        const p = transformPoint(world, position[i * 3], position[i * 3 + 1], position[i * 3 + 2]);
        transformedPositions.push(p);
      }
      for (let i = 0; i < position.length / 3; i++) {
        const p = transformedPositions[i];
        let n;
        if (normal) {
          n = normalize(transformDirection(world, normal[i * 3], normal[i * 3 + 1], normal[i * 3 + 2]));
        } else {
          n = [0, 1, 0];
        }
        vertices.push(
          p[0],
          p[1],
          p[2],
          n[0],
          n[1],
          n[2],
          uv ? uv[i * 2] : 0,
          uv ? uv[i * 2 + 1] : 0
        );
      }
      for (let i = 0; i < indexData.length; i++) {
        indices.push(baseVertex + indexData[i]);
      }
      if (!image && primitive.material !== void 0) {
        const material = json.materials?.[primitive.material];
        const textureIndex = material?.pbrMetallicRoughness?.baseColorTexture?.index;
        if (textureIndex !== void 0) {
          const texture = json.textures?.[textureIndex];
          const imageIndex = texture?.source;
          const gltfImage = imageIndex !== void 0 ? json.images?.[imageIndex] : void 0;
          if (gltfImage?.bufferView !== void 0) {
            const imageView = json.bufferViews[gltfImage.bufferView];
            if (imageView) {
              const bytes = new Uint8Array(bin.buffer, bin.byteOffset + (imageView.byteOffset ?? 0), imageView.byteLength);
              const bytesCopy = bytes.slice();
              image = new Blob([bytesCopy.buffer], { type: gltfImage.mimeType ?? "image/png" });
            }
          }
        }
      }
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
      positions: collision
    },
    image
  };
}
export {
  parseGLB
};
