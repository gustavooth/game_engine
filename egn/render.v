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

// TextTexture is a rasterized UTF-8 string and its dimensions in pixels.
pub struct TextTexture {
pub:
    texture Texture
    width u32
    height u32
}

// load_text rasterizes a trusted TTF/OTF font with kerning and newlines.
// Render the returned texture using a Sprite; release it with release_texture.
pub fn (e &Engine) load_text(font_path string, text EgnString, pixel_height f32) !TextTexture {
    mut utf8 := C.EgnString{}
    C.egn_string_init(&utf8)
    if !C.egn_string_from_utf8(&utf8, text.value.str, u64(text.value.len)) { return error('failed to copy EgnString') }
    defer { C.egn_string_destroy(&utf8) }
    mut width := u32(0)
    mut height := u32(0)
    id := C.renderer_text_texture_load(font_path.str, &utf8, pixel_height, &width, &height)
    if id == 0 { return error('failed to rasterize text or load font') }
    return TextTexture{texture: Texture{id: id}, width: width, height: height}
}

// release_texture frees a texture loaded with load_texture.
pub fn (e &Engine) release_texture(texture Texture) {
	if texture.id != 0 {
		C.renderer_texture_release(texture.id)
	}
}

// release_mesh frees a mesh loaded with load_obj/load_glb. Renderer-owned
// primitives (mesh_cube/mesh_sphere) are ignored by the engine.
pub fn (e &Engine) release_mesh(mesh Mesh) {
	if mesh.id != 0 {
		C.renderer_mesh_release(mesh.id)
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

// create_mesh uploads a static triangle mesh. positions and normals contain
// three floats per vertex, uvs two, and indices three per triangle. The mesh
// is owned by the renderer; release it with release_mesh.
pub fn (e &Engine) create_mesh(positions []f32, normals []f32, uvs []f32, indices []u32) !Mesh {
	if positions.len == 0 || indices.len == 0 {
		return error('create_mesh: empty mesh')
	}
	if positions.len % 3 != 0 || normals.len != positions.len || uvs.len * 3 != positions.len * 2 {
		return error('create_mesh: mismatched vertex attributes')
	}
	if indices.len % 3 != 0 {
		return error('create_mesh: index count must be a multiple of 3')
	}
	vertex_count := u32(positions.len / 3)
	id := C.renderer_mesh_create_static(positions.data, normals.data, uvs.data,
		vertex_count, indices.data, u32(indices.len))
	if id == 0 {
		return error('create_mesh: renderer rejected the mesh')
	}
	return Mesh{
		id: id
	}
}

// load_obj loads a Wavefront OBJ (with its MTL materials/textures).
pub fn (e &Engine) load_obj(path string) !Mesh {
	id := C.renderer_mesh_load_obj(path.str)
	if id == 0 {
		return error('failed to load OBJ: ${path}')
	}
	return Mesh{
		id: id
	}
}

// load_glb loads a binary glTF (.glb) mesh, including skeleton/animations.
pub fn (e &Engine) load_glb(path string) !Mesh {
	id := C.renderer_mesh_load_glb(path.str)
	if id == 0 {
		return error('failed to load GLB: ${path}')
	}
	return Mesh{
		id: id
	}
}

// screenshot requests a PNG of the next rendered frame.
pub fn (e &Engine) screenshot(path string) bool {
	return C.renderer_screenshot(path.str)
}

// animation_count returns the number of clips in a GLB mesh (0 for OBJ and
// primitives).
pub fn (m Mesh) animation_count() u32 {
	if m.id == 0 {
		return 0
	}
	return C.renderer_mesh_animation_count(m.id)
}

// animation_name returns the name of a GLB clip, or an empty string.
pub fn (m Mesh) animation_name(index u32) string {
	ptr := C.renderer_mesh_animation_name(m.id, index)
	if ptr == 0 {
		return ''
	}
	return unsafe { cstring_to_vstring(ptr) }
}

// animation_duration returns the duration of a GLB clip in seconds.
pub fn (m Mesh) animation_duration(index u32) f32 {
	if m.id == 0 {
		return 0.0
	}
	return C.renderer_mesh_animation_duration(m.id, index)
}
