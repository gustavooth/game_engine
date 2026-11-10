#include "defines.h"

#ifdef EGNPLATFORM_WEB
#include "platform/math.h"

#include <emscripten/emscripten.h>
#include <string.h>

// The browser renderer consumes the engine's own C math library through these
// small wasm entry points instead of reimplementing matrix/vector math in JS.

EMSCRIPTEN_KEEPALIVE void egn_web_mat4_identity(f32 *out)
{
  mat4 value = mat4_identity();
  memcpy(out, value.data, sizeof(value.data));
}

EMSCRIPTEN_KEEPALIVE void egn_web_mat4_multiply(const f32 *a, const f32 *b, f32 *out)
{
  mat4 left;
  mat4 right;
  memcpy(left.data, a, sizeof(left.data));
  memcpy(right.data, b, sizeof(right.data));
  mat4 result = mat4_mul(left, right);
  memcpy(out, result.data, sizeof(result.data));
}

EMSCRIPTEN_KEEPALIVE void egn_web_mat4_inverse(const f32 *matrix, f32 *out)
{
  mat4 value;
  memcpy(value.data, matrix, sizeof(value.data));
  mat4 result = mat4_inverse(value);
  memcpy(out, result.data, sizeof(result.data));
}

EMSCRIPTEN_KEEPALIVE void egn_web_mat4_perspective(f32 fov_radians, f32 aspect,
                                                    f32 near_clip, f32 far_clip, f32 *out)
{
  mat4 result = mat4_perspective(fov_radians, aspect, near_clip, far_clip);
  memcpy(out, result.data, sizeof(result.data));
}

EMSCRIPTEN_KEEPALIVE void egn_web_mat4_orthographic(f32 left, f32 right,
                                                     f32 bottom, f32 top,
                                                     f32 near_clip, f32 far_clip, f32 *out)
{
  mat4 result = mat4_orthographic(left, right, bottom, top, near_clip, far_clip);
  memcpy(out, result.data, sizeof(result.data));
}

EMSCRIPTEN_KEEPALIVE void egn_web_mat4_look_at(const f32 *eye, const f32 *target,
                                                const f32 *up, f32 *out)
{
  mat4 result = mat4_look_at(vec3_create(eye[0], eye[1], eye[2]),
                             vec3_create(target[0], target[1], target[2]),
                             vec3_create(up[0], up[1], up[2]));
  memcpy(out, result.data, sizeof(result.data));
}

EMSCRIPTEN_KEEPALIVE void egn_web_mat4_translation(const f32 *translation, f32 *out)
{
  mat4 result = mat4_translation(vec3_create(translation[0], translation[1], translation[2]));
  memcpy(out, result.data, sizeof(result.data));
}

EMSCRIPTEN_KEEPALIVE void egn_web_mat4_scale(const f32 *scale, f32 *out)
{
  mat4 result = mat4_scale(vec3_create(scale[0], scale[1], scale[2]));
  memcpy(out, result.data, sizeof(result.data));
}

EMSCRIPTEN_KEEPALIVE void egn_web_mat4_from_quat(const f32 *q, f32 *out)
{
  quat value = (quat){q[0], q[1], q[2], q[3]};
  mat4 result = quat_to_mat4(value);
  memcpy(out, result.data, sizeof(result.data));
}

EMSCRIPTEN_KEEPALIVE void egn_web_vec3_normalize(const f32 *value, f32 *out)
{
  vec3 result = vec3_normalized(vec3_create(value[0], value[1], value[2]));
  out[0] = result.x;
  out[1] = result.y;
  out[2] = result.z;
}

EMSCRIPTEN_KEEPALIVE f32 egn_web_vec3_dot(const f32 *a, const f32 *b)
{
  return vec3_dot(vec3_create(a[0], a[1], a[2]), vec3_create(b[0], b[1], b[2]));
}

#endif
