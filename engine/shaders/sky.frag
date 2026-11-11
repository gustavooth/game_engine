#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;
layout(location = 1) out vec4 out_normal;

// Sky-view LUT rendered by sky_lut.frag.
layout(set = 0, binding = 0) uniform sampler2D u_sky_lut;

layout(push_constant) uniform SkyConstants
{
  mat4 inverse_view_projection;
  vec4 sun_direction; // xyz = toward the sun
  vec4 sun_disc;      // rgb = radiance of the sun disc seen from the ground
  vec4 horizon;       // rgb = horizon radiance (matches the fog colour)
  vec4 ground;        // rgb = radiance below the horizon
} pc;

const float PI = 3.14159265;

vec3 sky_lut(vec3 direction)
{
  float elevation = asin(clamp(direction.y, 0.0, 1.0));
  vec2 uv = vec2(atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
                 sqrt(elevation / (0.5 * PI)));
  return textureLod(u_sky_lut, uv, 0.0).rgb;
}

void main()
{
  vec2 ndc = v_uv * 2.0 - 1.0;
  vec4 near_point = pc.inverse_view_projection * vec4(ndc, 0.0, 1.0);
  vec4 far_point = pc.inverse_view_projection * vec4(ndc, 1.0, 1.0);
  near_point /= near_point.w;
  far_point /= far_point.w;
  vec3 direction = normalize(far_point.xyz - near_point.xyz);
  vec3 sun = normalize(pc.sun_direction.xyz);

  // Night glow so the sky never collapses to pure black.
  vec3 colour = sky_lut(direction) + vec3(0.006, 0.009, 0.018);

  // Sun disc with a soft limb, hidden once it sinks below the horizon.
  float mu = dot(direction, sun);
  float disc = smoothstep(0.99990, 0.99996, mu);
  colour += pc.sun_disc.rgb * disc * smoothstep(-0.01, 0.01, direction.y);

  // Below the horizon there is no terrain, so fade into the haze the fog
  // converges to and then into the light bounced by the ground. Far geometry
  // dissolves into the same colour instead of ending in a hard line.
  float below = smoothstep(0.0, 0.35, -direction.y);
  vec3 lower = mix(pc.horizon.rgb, pc.ground.rgb, below);
  colour = mix(lower, colour, smoothstep(-0.03, 0.02, direction.y));

  out_colour = vec4(colour, 1.0);
  out_normal = vec4(0.0);
}
