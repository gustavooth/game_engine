const VERTEX_FLOATS = 8;
const VERTEX_STRIDE = VERTEX_FLOATS * 4;
function mesh(vertices, indices) {
  const v = new Float32Array(vertices);
  const positions = new Float32Array(v.length / VERTEX_FLOATS * 3);
  for (let i = 0, p = 0; i < v.length; i += VERTEX_FLOATS, p += 3) {
    positions[p + 0] = v[i + 0];
    positions[p + 1] = v[i + 1];
    positions[p + 2] = v[i + 2];
  }
  return { vertices: v, indices: new Uint32Array(indices), positions };
}
function createCube() {
  const vertices = [];
  const indices = [];
  const faces = [
    [[0, 0, 1], [[-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]]],
    [[0, 0, -1], [[1, -1, -1], [-1, -1, -1], [-1, 1, -1], [1, 1, -1]]],
    [[1, 0, 0], [[1, -1, 1], [1, -1, -1], [1, 1, -1], [1, 1, 1]]],
    [[-1, 0, 0], [[-1, -1, -1], [-1, -1, 1], [-1, 1, 1], [-1, 1, -1]]],
    [[0, 1, 0], [[-1, 1, 1], [1, 1, 1], [1, 1, -1], [-1, 1, -1]]],
    [[0, -1, 0], [[-1, -1, -1], [1, -1, -1], [1, -1, 1], [-1, -1, 1]]]
  ];
  const uvs = [[0, 1], [1, 1], [1, 0], [0, 0]];
  for (const [normal, corners] of faces) {
    const base = vertices.length / VERTEX_FLOATS;
    for (let i = 0; i < 4; i++) {
      vertices.push(
        corners[i][0] * 0.5,
        corners[i][1] * 0.5,
        corners[i][2] * 0.5,
        normal[0],
        normal[1],
        normal[2],
        uvs[i][0],
        uvs[i][1]
      );
    }
    indices.push(base, base + 1, base + 2, base + 2, base + 3, base);
  }
  return mesh(vertices, indices);
}
function createSphere(sectors = 32, stacks = 16) {
  const vertices = [];
  const indices = [];
  for (let stack = 0; stack <= stacks; stack++) {
    const v = stack / stacks;
    const phi = v * Math.PI;
    const y = Math.cos(phi) * 0.5;
    const radius = Math.sin(phi) * 0.5;
    for (let sector = 0; sector <= sectors; sector++) {
      const u = sector / sectors;
      const theta = u * Math.PI * 2;
      const x = Math.cos(theta) * radius;
      const z = Math.sin(theta) * radius;
      const nx = x * 2;
      const ny = y * 2;
      const nz = z * 2;
      vertices.push(x, y, z, nx, ny, nz, u, 1 - v);
    }
  }
  for (let stack = 0; stack < stacks; stack++) {
    for (let sector = 0; sector < sectors; sector++) {
      const a = stack * (sectors + 1) + sector;
      const b = a + sectors + 1;
      indices.push(a, b, a + 1, a + 1, b, b + 1);
    }
  }
  return mesh(vertices, indices);
}
function createQuad() {
  const vertices = [
    -0.5,
    -0.5,
    0,
    0,
    0,
    1,
    0,
    1,
    0.5,
    -0.5,
    0,
    0,
    0,
    1,
    1,
    1,
    0.5,
    0.5,
    0,
    0,
    0,
    1,
    1,
    0,
    -0.5,
    0.5,
    0,
    0,
    0,
    1,
    0,
    0
  ];
  return mesh(vertices, [0, 1, 2, 2, 3, 0]);
}
export {
  VERTEX_FLOATS,
  VERTEX_STRIDE,
  createCube,
  createQuad,
  createSphere
};
