#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;
layout(location = 1) out vec4 out_normal;

layout(push_constant) uniform SkyConstants
{
  mat4 inverse_view_projection;
  vec4 camera_position;
  vec4 sun_direction;
  vec4 sun_color;
  vec4 atmosphere; // rayleigh, mie, turbidity, mie anisotropy
  vec4 params;     // sun intensity, steps, light steps, unused
} pc;

const float PI = 3.14159265;
const float PLANET_RADIUS = 6371.0;
const float ATMOSPHERE_RADIUS = 6471.0;

vec2 ray_sphere(vec3 origin, vec3 direction, float radius)
{
  float b = dot(origin, direction);
  float c = dot(origin, origin) - radius * radius;
  float discriminant = b * b - c;
  if (discriminant < 0.0)
    return vec2(-1.0);
  float root = sqrt(discriminant);
  return vec2(-b - root, -b + root);
}

float hash12(vec2 p)
{
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

vec3 atmosphere(vec3 ray_direction)
{
  vec3 planet_center = vec3(0.0, -PLANET_RADIUS, 0.0);
  vec3 origin = pc.camera_position.xyz - planet_center;
  vec2 atmosphere_hit = ray_sphere(origin, ray_direction, ATMOSPHERE_RADIUS);
  if (atmosphere_hit.y <= 0.0)
    return vec3(0.0);

  float t_start = max(atmosphere_hit.x, 0.0);
  float t_end = atmosphere_hit.y;

  vec2 planet_hit = ray_sphere(origin, ray_direction, PLANET_RADIUS);
  if (planet_hit.x > 0.0 && planet_hit.x < t_end)
    t_end = planet_hit.x;

  int steps = int(max(pc.params.y, 2.0));
  int light_steps = int(max(pc.params.z, 1.0));
  float step_size = (t_end - t_start) / float(steps);

  vec3 beta_rayleigh = vec3(0.0058, 0.0135, 0.0331) * pc.atmosphere.x;
  float beta_mie = 0.021 * pc.atmosphere.y;

  float mu = dot(ray_direction, pc.sun_direction.xyz);
  float phase_rayleigh = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
  float g = pc.atmosphere.w;
  float phase_mie = 3.0 / (8.0 * PI) *
      ((1.0 - g * g) * (1.0 + mu * mu)) /
      ((2.0 + g * g) * pow(max(1.0 + g * g - 2.0 * g * mu, 0.001), 1.5));

  vec3 accumulated_rayleigh = vec3(0.0);
  vec3 accumulated_mie = vec3(0.0);
  float optical_rayleigh = 0.0;
  float optical_mie = 0.0;

  for (int i = 0; i < 32; i++)
  {
    if (i >= steps)
      break;

    float distance_along_ray = t_start + (float(i) + 0.5) * step_size;
    vec3 position = origin + ray_direction * distance_along_ray;
    float height = max(length(position) - PLANET_RADIUS, 0.0);
    float density = exp(-height / 8.0);
    optical_rayleigh += density * step_size;
    optical_mie += density * step_size;

    float light_optical_rayleigh = 0.0;
    float light_optical_mie = 0.0;
    float light_step = 12.0 / float(light_steps);
    for (int j = 0; j < 8; j++)
    {
      if (j >= light_steps)
        break;
      vec3 light_position = position + pc.sun_direction.xyz * (float(j) + 0.5) * light_step;
      float light_height = max(length(light_position) - PLANET_RADIUS, 0.0);
      float light_density = exp(-light_height / 8.0);
      light_optical_rayleigh += light_density * light_step;
      light_optical_mie += light_density * light_step;
    }

    vec3 transmittance = exp(-(beta_rayleigh * (optical_rayleigh + light_optical_rayleigh) +
                               beta_mie * 1.1 * (optical_mie + light_optical_mie)));
    accumulated_rayleigh += density * transmittance * step_size;
    accumulated_mie += density * transmittance * step_size;
  }

  vec3 scattering = accumulated_rayleigh * beta_rayleigh * phase_rayleigh +
                    accumulated_mie * beta_mie * phase_mie;
  vec3 colour = scattering * pc.sun_color.rgb * pc.params.x;
  colour *= mix(0.35, 1.0, max(pc.sun_direction.y, 0.0) * 0.5 + 0.5);

  float sun_dot = max(dot(ray_direction, pc.sun_direction.xyz), 0.0);
  colour += pc.sun_color.rgb * pow(sun_dot, 900.0) * pc.params.x * 2.0;
  colour += pc.sun_color.rgb * pow(sun_dot, 12.0) * 0.04 *
            pc.atmosphere.y * 400.0;

  // Below the horizon, fade into a haze that roughly matches the fog so the
  // finite ground plane does not end in a hard dark line.
  float day = clamp(pc.sun_direction.y * 3.0 + 0.35, 0.0, 1.0);
  vec3 haze = vec3(0.20, 0.28, 0.36) * (0.35 + 0.65 * day) +
              pc.sun_color.rgb * 0.05;
  float horizon = smoothstep(-0.16, 0.04, ray_direction.y);
  colour = mix(haze, colour, horizon);
  return colour;
}

void main()
{
  vec2 ndc = v_uv * 2.0 - 1.0;
  vec4 near_point = pc.inverse_view_projection * vec4(ndc, 0.0, 1.0);
  vec4 far_point = pc.inverse_view_projection * vec4(ndc, 1.0, 1.0);
  near_point /= near_point.w;
  far_point /= far_point.w;
  vec3 ray_direction = normalize(far_point.xyz - near_point.xyz);

  out_colour = vec4(atmosphere(ray_direction), 1.0);
  out_normal = vec4(0.0);
}
