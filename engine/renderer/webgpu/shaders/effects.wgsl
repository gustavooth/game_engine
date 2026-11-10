struct EffectsParams {
  inverseViewProjection: mat4x4f,
  viewProjection: mat4x4f,
  cameraPosition: vec4f,
  params: vec4f,
  params2: vec4f,
};

@group(0) @binding(0) var sceneColour: texture_2d<f32>;
@group(0) @binding(1) var sceneNormal: texture_2d<f32>;
@group(0) @binding(2) var sceneDepth: texture_depth_2d;
@group(0) @binding(3) var colourSampler: sampler;
@group(0) @binding(4) var depthSampler: sampler;
@group(0) @binding(5) var<uniform> pc: EffectsParams;

fn hash12(p: vec2f) -> f32 {
  let p3 = fract(vec3f(p.x, p.y, p.x) * 0.1031);
  let p3b = p3 + vec3f(dot(p3, vec3f(p3.y, p3.z, p3.x) + 33.33));
  return fract((p3b.x + p3b.y) * p3b.z);
}

fn loadDepth(uv: vec2f) -> f32 {
  let dims = textureDimensions(sceneDepth, 0);
  let coord = vec2i(clamp(uv * vec2f(dims), vec2f(0.0), vec2f(dims - vec2u(1u))));
  return textureLoad(sceneDepth, coord, 0);
}

fn reconstructWorld(uv: vec2f, depth: f32) -> vec3f {
  let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let world = pc.inverseViewProjection * vec4f(ndc, depth, 1.0);
  return world.xyz / world.w;
}

struct VertexOutput {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> VertexOutput {
  var output: VertexOutput;
  let uv = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
  output.position = vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
  output.uv = uv;
  return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
  let uv = vec2f(input.uv.x, 1.0 - input.uv.y);
  let depth = loadDepth(uv);
  let normal = textureSampleLevel(sceneNormal, colourSampler, uv, 0.0).xyz;
  if (depth >= 0.9999 || dot(normal, normal) < 0.01) {
    return vec4f(0.0, 0.0, 0.0, 1.0);
  }

  let worldPosition = reconstructWorld(uv, depth);
  let cameraPosition = pc.cameraPosition.xyz;
  let radius = pc.params.x;
  let samples = i32(pc.params2.x);
  let giDistance = pc.params.w;
  let flags = i32(pc.params2.w);
  let aoEnabled = (flags & 1) != 0;
  let giEnabled = (flags & 2) != 0;

  var ao = 0.0;
  var aoWeight = 0.0;
  var gi = vec3f(0.0);
  var giWeight = 0.0;

  for (var i = 0; i < 32; i = i + 1) {
    if (i >= samples) {
      break;
    }

    let angle = hash12(vec2f(input.position.x, input.position.y) + vec2f(f32(i) * 17.0, 0.0)) * 6.2831853;
    let z = hash12(vec2f(input.position.y, input.position.x) + vec2f(f32(i) * 31.0, 0.0));
    let randomDirection = normalize(vec3f(cos(angle) * sqrt(max(1.0 - z * z, 0.0)),
                                          sin(angle) * sqrt(max(1.0 - z * z, 0.0)), z));
    let tangent = normalize(randomDirection - normal * dot(randomDirection, normal));
    let bitangent = cross(normal, tangent);
    let sampleDirection = normalize(tangent * randomDirection.x +
                                    bitangent * randomDirection.y + normal * z);

    let scale = hash12(vec2f(input.position.x, input.position.y) + vec2f(0.0, f32(i) * 7.0));
    let samplePosition = worldPosition + sampleDirection * radius * (0.2 + 0.8 * scale);
    let clip = pc.viewProjection * vec4f(samplePosition, 1.0);
    let ndc = clip.xyz / max(clip.w, 0.0001);
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 ||
        ndc.z < 0.0 || ndc.z > 1.0) {
      continue;
    }

    let sampleUv = vec2f(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    let sampleDepth = loadDepth(sampleUv);
    if (sampleDepth >= 0.9999) {
      continue;
    }

    let scenePosition = reconstructWorld(sampleUv, sampleDepth);
    let sampleDistance = length(scenePosition - cameraPosition);
    let pointDistance = length(samplePosition - cameraPosition);
    let bias = radius * 0.08;
    let weight = max(dot(normal, sampleDirection), 0.0);

    if (aoEnabled) {
      ao += select(0.0, weight, pointDistance > sampleDistance + bias);
      aoWeight += weight;
    }

    if (giEnabled) {
      let distanceToSurface = length(scenePosition - worldPosition);
      if (distanceToSurface < giDistance) {
        let falloff = 1.0 - clamp(distanceToSurface / giDistance, 0.0, 1.0);
        gi += textureSampleLevel(sceneColour, colourSampler, sampleUv, 0.0).rgb * weight * falloff;
        giWeight += weight * falloff;
      }
    }
  }

  let occlusion = select(1.0, 1.0 - ao / max(aoWeight, 0.0001), aoEnabled);
  var indirect = vec3f(0.0);
  if (giEnabled && giWeight > 0.0001) {
    indirect = gi / giWeight * 0.35;
  }
  return vec4f(indirect, occlusion);
}
