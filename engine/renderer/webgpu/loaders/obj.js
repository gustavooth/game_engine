import { VERTEX_FLOATS } from "../meshes.js";
function parseIndex(raw, count) {
  if (!raw) return -1;
  const value = Number.parseInt(raw, 10);
  if (Number.isNaN(value)) return -1;
  return value < 0 ? count + value : value - 1;
}
function cornerIndex(token, positionCount, uvCount, normalCount) {
  const parts = token.split("/");
  return {
    p: parseIndex(parts[0] ?? "", positionCount),
    t: parseIndex(parts[1] ?? "", uvCount),
    n: parseIndex(parts[2] ?? "", normalCount)
  };
}
function sub(a, b) {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}
function cross(a, b) {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
function normalize(v) {
  const length = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / length, v[1] / length, v[2] / length];
}
function parseOBJ(source) {
  const positions = [];
  const uvs = [];
  const normals = [];
  const vertices = [];
  for (const rawLine of source.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#")) continue;
    const parts = line.split(/\s+/);
    const kind = parts[0];
    if (kind === "v") {
      positions.push([Number(parts[1]), Number(parts[2]), Number(parts[3])]);
    } else if (kind === "vt") {
      uvs.push([Number(parts[1]), Number(parts[2])]);
    } else if (kind === "vn") {
      normals.push([Number(parts[1]), Number(parts[2]), Number(parts[3])]);
    } else if (kind === "f") {
      const corners = parts.slice(1).map((token) => cornerIndex(token, positions.length, uvs.length, normals.length));
      if (corners.length < 3) continue;
      const a = positions[corners[0].p] ?? [0, 0, 0];
      const b = positions[corners[1].p] ?? [0, 0, 0];
      const c = positions[corners[2].p] ?? [0, 0, 0];
      const faceNormal = normalize(cross(sub(b, a), sub(c, a)));
      for (let i = 1; i + 1 < corners.length; i++) {
        for (const corner of [corners[0], corners[i], corners[i + 1]]) {
          const p = positions[corner.p] ?? [0, 0, 0];
          const uv = uvs[corner.t] ?? [0, 0];
          const n = corner.n >= 0 ? normals[corner.n] ?? faceNormal : faceNormal;
          vertices.push(p[0], p[1], p[2], n[0], n[1], n[2], uv[0], uv[1]);
        }
      }
    }
  }
  if (vertices.length === 0) {
    throw new Error("OBJ has no triangles");
  }
  const interleaved = new Float32Array(vertices);
  const count = interleaved.length / VERTEX_FLOATS;
  const collisionPositions = new Float32Array(count * 3);
  for (let i = 0, p = 0; i < interleaved.length; i += VERTEX_FLOATS, p += 3) {
    collisionPositions[p] = interleaved[i];
    collisionPositions[p + 1] = interleaved[i + 1];
    collisionPositions[p + 2] = interleaved[i + 2];
  }
  const indices = new Uint32Array(count);
  for (let i = 0; i < count; i++) indices[i] = i;
  return { vertices: interleaved, indices, positions: collisionPositions };
}
export {
  parseOBJ
};
