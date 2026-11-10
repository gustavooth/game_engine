#version 450

struct Particle
{
  vec4 position_age;
  vec4 velocity_life;
};

layout(set = 0, binding = 0) readonly buffer ParticleBuffer
{
  Particle particles[];
} particle_buffer;

layout(push_constant) uniform PushConstants
{
  mat4 view_projection;
  vec4 camera_right; // xyz = camera right, w = size scale
  vec4 camera_up;    // xyz = camera up
  vec4 color_start;
  vec4 color_end;
  vec4 params;       // x = size start, y = size end, z = fade in, w = fade out
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main()
{
  uint particle_index = gl_InstanceIndex;
  Particle particle = particle_buffer.particles[particle_index];

  float age = particle.position_age.w;
  float life = particle.velocity_life.w;
  if (life <= 0.0 || age >= life)
  {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    v_uv = vec2(0.0);
    v_color = vec4(0.0);
    return;
  }

  const vec2 corners[6] = vec2[](
      vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
      vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
  vec2 corner = corners[gl_VertexIndex % 6u];

  float t = clamp(age / max(life, 0.0001), 0.0, 1.0);
  float size = mix(pc.params.x, pc.params.y, t) * pc.camera_right.w;
  vec3 world_position = particle.position_age.xyz +
                        pc.camera_right.xyz * (corner.x * size) +
                        pc.camera_up.xyz * (corner.y * size);

  gl_Position = pc.view_projection * vec4(world_position, 1.0);
  v_uv = corner * 0.5 + 0.5;

  float fade_in = clamp(t / max(pc.params.z, 0.0001), 0.0, 1.0);
  float fade_out = clamp((1.0 - t) / max(pc.params.w, 0.0001), 0.0, 1.0);
  float alpha = min(fade_in, fade_out);
  vec3 colour = mix(pc.color_start.rgb, pc.color_end.rgb, t);
  v_color = vec4(colour, mix(pc.color_start.a, pc.color_end.a, t) * alpha);
}
