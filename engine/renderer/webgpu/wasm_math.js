// Thin JS facade over the engine's C math library compiled to WebAssembly.
// Every matrix/vector operation used by the WebGPU renderer and asset loaders
// runs inside the wasm module; this file only marshals scratch buffers.

export class WasmMath {
  constructor(module) {
    this.module = module;
    this.scratch = module._malloc(128 * 4);

    const A = 0;     // mat4 input A
    const B = 16;    // mat4 input B
    const C = 32;    // mat4 output
    const V0 = 48;   // vec3 input 0
    const V1 = 51;   // vec3 input 1
    const V2 = 54;   // vec3 input 2
    const Q = 57;    // quat input

    const view = (offset, count) =>
      new Float32Array(module.HEAPF32.buffer, this.scratch + offset * 4, count);
    const write = (offset, values) => view(offset, values.length).set(values);
    const read = (offset, count) => view(offset, count).slice();
    const ptr = (offset) => this.scratch + offset * 4;

    this.mat4 = {
      identity: () => {
        module._egn_web_mat4_identity(ptr(C));
        return read(C, 16);
      },
      multiply: (a, b) => {
        write(A, a);
        write(B, b);
        module._egn_web_mat4_multiply(ptr(A), ptr(B), ptr(C));
        return read(C, 16);
      },
      inverse: (m) => {
        write(A, m);
        module._egn_web_mat4_inverse(ptr(A), ptr(C));
        return read(C, 16);
      },
      perspective: (fov, aspect, near, far) => {
        module._egn_web_mat4_perspective(fov, aspect, near, far, ptr(C));
        return read(C, 16);
      },
      ortho: (left, right, bottom, top, near, far) => {
        module._egn_web_mat4_orthographic(left, right, bottom, top, near, far, ptr(C));
        return read(C, 16);
      },
      lookAt: (eye, target, up) => {
        write(V0, eye);
        write(V1, target);
        write(V2, up);
        module._egn_web_mat4_look_at(ptr(V0), ptr(V1), ptr(V2), ptr(C));
        return read(C, 16);
      },
      translation: (v) => {
        write(V0, v);
        module._egn_web_mat4_translation(ptr(V0), ptr(C));
        return read(C, 16);
      },
      scaling: (v) => {
        write(V0, v);
        module._egn_web_mat4_scale(ptr(V0), ptr(C));
        return read(C, 16);
      },
      fromQuat: (q) => {
        write(Q, q);
        module._egn_web_mat4_from_quat(ptr(Q), ptr(C));
        return read(C, 16);
      },
    };

    this.vec3 = {
      normalize: (v) => {
        write(V0, v);
        module._egn_web_vec3_normalize(ptr(V0), ptr(V1));
        return read(V1, 3);
      },
      dot: (a, b) => {
        write(V0, a);
        write(V1, b);
        return module._egn_web_vec3_dot(ptr(V0), ptr(V1));
      },
    };
  }

  destroy() {
    if (this.scratch) {
      this.module._free(this.scratch);
      this.scratch = 0;
    }
  }
}
