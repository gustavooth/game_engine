import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parseGLB } from '../engine/renderer/webgpu/loaders/glb.js';

// The loader only needs a matrix provider here; animation metadata is
// independent of the transforms used to traverse the model's scene graph.
const identity = () => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
const mat4 = { identity, translation: identity, fromQuat: identity, scaling: identity,
  multiply: identity };
const bytes = readFileSync(new URL('../game/assets/models/humanoid_idle.glb', import.meta.url));
const buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
const { data, image } = parseGLB(buffer, { mat4 });
assert.ok(image instanceof Blob);
assert.equal(image.type, 'image/png');
assert.ok(image.size > 0);
assert.equal(data.animations.length, 8);
assert.equal(data.animations[2].name, 'Human Armature|Idle');
assert.equal(data.animations[2].channels.length, 31);
assert.equal(data.animations[2].duration, 10);
assert.equal(data.animations[2].channels[0].node, 2);
