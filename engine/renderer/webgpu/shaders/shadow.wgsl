struct ShadowGlobals {
  lightViewProjection: mat4x4f,
};

struct ObjectUniforms {
  model: mat4x4f,
  color: vec4f,
  params: vec4f,
  inverseModel: mat4x4f,
};

@group(0) @binding(0) var<uniform> globals: ShadowGlobals;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;

@vertex
fn vs_main(@location(0) position: vec3f) -> @builtin(position) vec4f {
  return globals.lightViewProjection * object.model * vec4f(position, 1.0);
}
