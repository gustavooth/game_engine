module egn

// Value types and resources used by the scene/rendering API.

pub enum Projection {
	perspective  = 0
	orthographic = 1
}

// Viewport is a normalized rectangle (0..1) relative to the window. The
// origin is the top-left corner.
pub struct Viewport {
pub mut:
	x      f32
	y      f32
	width  f32
	height f32
}

// Color is an RGBA colour with components in the 0..1 range.
pub struct Color {
pub mut:
	r f32
	g f32
	b f32
	a f32
}

// rgb builds an opaque colour.
pub fn rgb(r f32, g f32, b f32) Color {
	return Color{
		r: r
		g: g
		b: b
		a: 1.0
	}
}

// rgba builds a colour with an explicit alpha.
pub fn rgba(r f32, g f32, b f32, a f32) Color {
	return Color{
		r: r
		g: g
		b: b
		a: a
	}
}

// with_alpha returns a copy of the colour with a different alpha.
pub fn (c Color) with_alpha(a f32) Color {
	return Color{
		r: c.r
		g: c.g
		b: c.b
		a: a
	}
}

// Texture is an image loaded by the engine.
pub struct Texture {
pub:
	id u32
}

// is_valid reports whether the texture handle can be drawn.
pub fn (t Texture) is_valid() bool {
	return t.id != 0
}

// Mesh is a GPU mesh. The engine owns the cube/sphere primitives; OBJ meshes
// will be added in a later stage.
pub struct Mesh {
pub:
	id u32
}

// is_valid reports whether the mesh handle can be drawn.
pub fn (m Mesh) is_valid() bool {
	return m.id != 0
}

// load_texture loads an image (PNG, JPG, BMP, ...) from disk.
pub fn (e &Engine) load_texture(path string) !Texture {
	id := C.renderer_texture_load(path.str)
	if id == 0 {
		return error('failed to load texture: ${path}')
	}
	return Texture{
		id: id
	}
}

// release_texture frees a texture loaded with load_texture.
pub fn (e &Engine) release_texture(texture Texture) {
	if texture.id != 0 {
		C.renderer_texture_release(texture.id)
	}
}

// mesh_cube returns the renderer-owned unit cube mesh.
pub fn (e &Engine) mesh_cube() Mesh {
	return Mesh{
		id: C.renderer_mesh_cube()
	}
}

// mesh_sphere returns the renderer-owned unit sphere mesh.
pub fn (e &Engine) mesh_sphere() Mesh {
	return Mesh{
		id: C.renderer_mesh_sphere()
	}
}

// screenshot requests a PNG of the next rendered frame.
pub fn (e &Engine) screenshot(path string) bool {
	return C.renderer_screenshot(path.str)
}
