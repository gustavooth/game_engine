#version 450

layout(location = 0) out vec2 v_uv;

void main()
{
  // Fullscreen triangle. Vertices: (0,0), (2,0), (0,2) in UV space.
  vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
  v_uv = uv;
  gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
