module egn

// Scene graph and component API.
//
// Every object has a local TRS transform, a layer (0..31), an active flag and
// a parent/children hierarchy. Children accumulate their parent transforms.
// The scene itself is the root object (id 1).

pub enum ComponentType {
	camera = 0
	model  = 1
	sprite = 2
}

// Object is a handle to an engine object (a scene node / GameObject).
pub struct Object {
	handle voidptr
}

// Camera renders the objects whose layer is present in layer_mask into its
// normalized viewport.
pub struct Camera {
mut:
	handle voidptr
pub mut:
	projection          Projection = .perspective
	fov                 f32        = 60.0
	ortho_size          f32        = 10.0
	near                f32        = 0.1
	far                 f32        = 1000.0
	viewport            Viewport   = Viewport{ x: 0.0, y: 0.0, width: 1.0, height: 1.0 }
	layer_mask          u32        = 0xFFFFFFFF
	clear_color         Color      = Color{ r: 0.0, g: 0.0, b: 0.1, a: 1.0 }
	clear_color_enabled bool       = true
	clear_depth_enabled bool       = true
	order               int
	enabled             bool = true
}

// Model renders a 3D mesh with a solid colour/texture.
pub struct Model {
mut:
	handle voidptr
pub mut:
	mesh    Mesh    = Mesh{}
	texture Texture = Texture{}
	color   Color   = Color{ r: 1.0, g: 1.0, b: 1.0, a: 1.0 }
	enabled bool    = true
}

// Sprite renders a textured quad in world units (XY plane, facing +Z).
pub struct Sprite {
mut:
	handle voidptr
pub mut:
	texture Texture = Texture{}
	size    Vec2    = Vec2{ x: 1.0, y: 1.0 }
	pivot   Vec2    = Vec2{ x: 0.5, y: 0.5 }
	color   Color   = Color{ r: 1.0, g: 1.0, b: 1.0, a: 1.0 }
	enabled bool    = true
}

fn projection_from_int(value i32) Projection {
	if value == 1 {
		return .orthographic
	}
	return .perspective
}

// Object methods ------------------------------------------------------------

pub fn (o Object) is_valid() bool {
	return o.handle != 0 && C.egn_object_is_valid(o.handle)
}

pub fn (o Object) id() u32 {
	return C.egn_object_get_id(o.handle)
}

pub fn (o Object) set_name(name string) {
	C.egn_object_set_name(o.handle, name.str)
}

pub fn (o Object) name() string {
	ptr := C.egn_object_get_name(o.handle)
	if ptr == 0 {
		return ''
	}
	return unsafe { cstring_to_vstring(ptr) }
}

pub fn (o Object) create_object(name string) Object {
	h := C.egn_object_create(o.handle, name.str)
	return Object{
		handle: h
	}
}

pub fn (o Object) create_child(name string) Object {
	return o.create_object(name)
}

pub fn (o Object) create_child_euler_deg(name string, position Vec3, rotation_euler_deg Vec3, scale Vec3) Object {
	child := o.create_object(name)
	child.set_position(position)
	child.set_euler_deg(rotation_euler_deg)
	child.set_scale(scale)
	return child
}

pub fn (o Object) destroy() {
	C.egn_object_destroy(o.handle)
}

pub fn (o Object) parent() ?Object {
	h := C.egn_object_get_parent(o.handle)
	if h == 0 {
		return none
	}
	return Object{
		handle: h
	}
}

pub fn (o Object) set_parent(parent Object) {
	C.egn_object_set_parent(o.handle, parent.handle)
}

pub fn (o Object) child_count() int {
	return int(C.egn_object_child_count(o.handle))
}

pub fn (o Object) child_at(index int) ?Object {
	if index < 0 {
		return none
	}
	h := C.egn_object_child_at(o.handle, u32(index))
	if h == 0 {
		return none
	}
	return Object{
		handle: h
	}
}

pub fn (o Object) set_position(position Vec3) {
	C.egn_object_set_position(o.handle, position.x, position.y, position.z)
}

pub fn (o Object) position() Vec3 {
	mut x := f32(0)
	mut y := f32(0)
	mut z := f32(0)
	C.egn_object_get_position(o.handle, &x, &y, &z)
	return vec3(x, y, z)
}

pub fn (o Object) set_rotation_quat(rotation Quat) {
	C.egn_object_set_rotation_quat(o.handle, rotation.x, rotation.y, rotation.z, rotation.w)
}

pub fn (o Object) rotation() Quat {
	mut x := f32(0)
	mut y := f32(0)
	mut z := f32(0)
	mut w := f32(1)
	C.egn_object_get_rotation_quat(o.handle, &x, &y, &z, &w)
	return quat(x, y, z, w)
}

// set_euler sets the rotation from Euler angles in radians (X * Y * Z).
pub fn (o Object) set_euler(euler Vec3) {
	C.egn_object_set_rotation_euler(o.handle, euler.x, euler.y, euler.z)
}

// set_euler_deg sets the rotation from Euler angles in degrees.
pub fn (o Object) set_euler_deg(euler_deg Vec3) {
	radians := vec3(deg_to_rad(euler_deg.x), deg_to_rad(euler_deg.y), deg_to_rad(euler_deg.z))
	C.egn_object_set_rotation_euler(o.handle, radians.x, radians.y, radians.z)
}

// rotate_euler applies an additional local Euler rotation in radians.
pub fn (o Object) rotate_euler(euler Vec3) {
	C.egn_object_rotate_euler(o.handle, euler.x, euler.y, euler.z)
}

// rotate_euler_deg applies an additional local Euler rotation in degrees.
pub fn (o Object) rotate_euler_deg(euler_deg Vec3) {
	radians := vec3(deg_to_rad(euler_deg.x), deg_to_rad(euler_deg.y), deg_to_rad(euler_deg.z))
	C.egn_object_rotate_euler(o.handle, radians.x, radians.y, radians.z)
}

// look_at orients the object so its local -Z axis points at the world target.
pub fn (o Object) look_at(target Vec3) {
	C.egn_object_look_at(o.handle, target.x, target.y, target.z)
}

pub fn (o Object) set_scale(scale Vec3) {
	C.egn_object_set_scale(o.handle, scale.x, scale.y, scale.z)
}

pub fn (o Object) scale() Vec3 {
	mut x := f32(0)
	mut y := f32(0)
	mut z := f32(0)
	C.egn_object_get_scale(o.handle, &x, &y, &z)
	return vec3(x, y, z)
}

pub fn (o Object) set_layer(layer u32) {
	C.egn_object_set_layer(o.handle, layer)
}

pub fn (o Object) layer() u32 {
	return C.egn_object_get_layer(o.handle)
}

pub fn (o Object) set_active(active bool) {
	C.egn_object_set_active(o.handle, active)
}

pub fn (o Object) active() bool {
	return C.egn_object_is_active(o.handle)
}

pub fn (o Object) active_in_hierarchy() bool {
	return C.egn_object_is_active_in_hierarchy(o.handle)
}

// add_component attaches a Camera, Model or Sprite component and returns a
// handle populated with the component state.
pub fn (o Object) add_component[T](component T) T {
	$if T is Camera {
		h := C.egn_object_add_camera(o.handle, i32(component.projection), component.fov, component.ortho_size,
			component.near, component.far, component.viewport.x, component.viewport.y,
			component.viewport.width, component.viewport.height, component.layer_mask,
			component.clear_color.r, component.clear_color.g, component.clear_color.b, component.clear_color.a,
			component.clear_color_enabled, component.clear_depth_enabled, i32(component.order), component.enabled)
		mut result := component
		result.handle = h
		return result
	} $else $if T is Model {
		h := C.egn_object_add_model(o.handle, component.mesh.id, component.texture.id,
			component.color.r, component.color.g, component.color.b, component.color.a, component.enabled)
		mut result := component
		result.handle = h
		return result
	} $else $if T is Sprite {
		h := C.egn_object_add_sprite(o.handle, component.texture.id, component.size.x, component.size.y,
			component.pivot.x, component.pivot.y, component.color.r, component.color.g,
			component.color.b, component.color.a, component.enabled)
		mut result := component
		result.handle = h
		return result
	} $else {
		compile_error('add_component: unsupported component type')
	}
}

// get_component returns the first component of the requested type.
pub fn (o Object) get_component[T]() ?T {
	$if T is Camera {
		h := C.egn_object_get_component(o.handle, i32(ComponentType.camera))
		if h == 0 {
			return none
		}
		mut vx := f32(0)
		mut vy := f32(0)
		mut vw := f32(1)
		mut vh := f32(1)
		C.egn_camera_get_viewport(h, &vx, &vy, &vw, &vh)
		mut cr := f32(0)
		mut cg := f32(0)
		mut cb := f32(0)
		mut ca := f32(1)
		C.egn_camera_get_clear_color(h, &cr, &cg, &cb, &ca)
		mut result := T{}
		result.handle = h
		result.projection = projection_from_int(C.egn_camera_get_projection(h))
		result.fov = C.egn_camera_get_fov(h)
		result.ortho_size = C.egn_camera_get_ortho_size(h)
		result.near = C.egn_camera_get_near(h)
		result.far = C.egn_camera_get_far(h)
		result.viewport = Viewport{
			x:      vx
			y:      vy
			width:  vw
			height: vh
		}
		result.layer_mask = C.egn_camera_get_layer_mask(h)
		result.clear_color = Color{
			r: cr
			g: cg
			b: cb
			a: ca
		}
		result.clear_color_enabled = C.egn_camera_get_clear_color_enabled(h)
		result.clear_depth_enabled = C.egn_camera_get_clear_depth_enabled(h)
		result.order = int(C.egn_camera_get_order(h))
		result.enabled = C.egn_component_is_enabled(h)
		return result
	} $else $if T is Model {
		h := C.egn_object_get_component(o.handle, i32(ComponentType.model))
		if h == 0 {
			return none
		}
		mut cr := f32(1)
		mut cg := f32(1)
		mut cb := f32(1)
		mut ca := f32(1)
		C.egn_model_get_color(h, &cr, &cg, &cb, &ca)
		mut result := T{}
		result.handle = h
		result.mesh = Mesh{
			id: C.egn_model_get_mesh(h)
		}
		result.texture = Texture{
			id: C.egn_model_get_texture(h)
		}
		result.color = Color{
			r: cr
			g: cg
			b: cb
			a: ca
		}
		result.enabled = C.egn_component_is_enabled(h)
		return result
	} $else $if T is Sprite {
		h := C.egn_object_get_component(o.handle, i32(ComponentType.sprite))
		if h == 0 {
			return none
		}
		mut w := f32(1)
		mut hgt := f32(1)
		C.egn_sprite_get_size(h, &w, &hgt)
		mut px := f32(0.5)
		mut py := f32(0.5)
		C.egn_sprite_get_pivot(h, &px, &py)
		mut cr := f32(1)
		mut cg := f32(1)
		mut cb := f32(1)
		mut ca := f32(1)
		C.egn_sprite_get_color(h, &cr, &cg, &cb, &ca)
		mut result := T{}
		result.handle = h
		result.texture = Texture{
			id: C.egn_sprite_get_texture(h)
		}
		result.size = vec2(w, hgt)
		result.pivot = vec2(px, py)
		result.color = Color{
			r: cr
			g: cg
			b: cb
			a: ca
		}
		result.enabled = C.egn_component_is_enabled(h)
		return result
	} $else {
		compile_error('get_component: unsupported component type')
	}
}

// Camera component methods --------------------------------------------------

pub fn (c Camera) is_valid() bool {
	return c.handle != 0 && C.egn_component_is_valid(c.handle)
}

pub fn (mut c Camera) set_enabled(enabled bool) {
	C.egn_component_set_enabled(c.handle, enabled)
	c.enabled = enabled
}

pub fn (mut c Camera) set_projection(projection Projection) {
	C.egn_camera_set_projection(c.handle, i32(projection))
	c.projection = projection
}

pub fn (mut c Camera) set_fov(fov f32) {
	C.egn_camera_set_fov(c.handle, fov)
	c.fov = fov
}

pub fn (mut c Camera) set_ortho_size(ortho_size f32) {
	C.egn_camera_set_ortho_size(c.handle, ortho_size)
	c.ortho_size = ortho_size
}

pub fn (mut c Camera) set_clip_planes(near f32, far f32) {
	C.egn_camera_set_clip_planes(c.handle, near, far)
	c.near = near
	c.far = far
}

pub fn (mut c Camera) set_viewport(viewport Viewport) {
	C.egn_camera_set_viewport(c.handle, viewport.x, viewport.y, viewport.width, viewport.height)
	c.viewport = viewport
}

pub fn (mut c Camera) set_layer_mask(layer_mask u32) {
	C.egn_camera_set_layer_mask(c.handle, layer_mask)
	c.layer_mask = layer_mask
}

pub fn (mut c Camera) set_clear_color(color Color) {
	C.egn_camera_set_clear_color(c.handle, color.r, color.g, color.b, color.a)
	c.clear_color = color
}

pub fn (mut c Camera) set_clear_color_enabled(enabled bool) {
	C.egn_camera_set_clear_color_enabled(c.handle, enabled)
	c.clear_color_enabled = enabled
}

pub fn (mut c Camera) set_clear_depth_enabled(enabled bool) {
	C.egn_camera_set_clear_depth_enabled(c.handle, enabled)
	c.clear_depth_enabled = enabled
}

pub fn (mut c Camera) set_order(order int) {
	C.egn_camera_set_order(c.handle, i32(order))
	c.order = order
}

pub fn (mut c Camera) destroy() {
	C.egn_component_destroy(c.handle)
	c.handle = voidptr(0)
}

// apply pushes every field to the engine. Call it after changing fields
// directly; the set_* methods already push their field automatically.
pub fn (mut c Camera) apply() {
	C.egn_camera_set_projection(c.handle, i32(c.projection))
	C.egn_camera_set_fov(c.handle, c.fov)
	C.egn_camera_set_ortho_size(c.handle, c.ortho_size)
	C.egn_camera_set_clip_planes(c.handle, c.near, c.far)
	C.egn_camera_set_viewport(c.handle, c.viewport.x, c.viewport.y, c.viewport.width, c.viewport.height)
	C.egn_camera_set_layer_mask(c.handle, c.layer_mask)
	C.egn_camera_set_clear_color(c.handle, c.clear_color.r, c.clear_color.g, c.clear_color.b, c.clear_color.a)
	C.egn_camera_set_clear_color_enabled(c.handle, c.clear_color_enabled)
	C.egn_camera_set_clear_depth_enabled(c.handle, c.clear_depth_enabled)
	C.egn_camera_set_order(c.handle, i32(c.order))
	C.egn_component_set_enabled(c.handle, c.enabled)
}

// Model component methods ---------------------------------------------------

pub fn (m Model) is_valid() bool {
	return m.handle != 0 && C.egn_component_is_valid(m.handle)
}

pub fn (mut m Model) set_enabled(enabled bool) {
	C.egn_component_set_enabled(m.handle, enabled)
	m.enabled = enabled
}

pub fn (mut m Model) set_mesh(mesh Mesh) {
	C.egn_model_set_mesh(m.handle, mesh.id)
	m.mesh = mesh
}

pub fn (mut m Model) set_texture(texture Texture) {
	C.egn_model_set_texture(m.handle, texture.id)
	m.texture = texture
}

pub fn (mut m Model) set_color(color Color) {
	C.egn_model_set_color(m.handle, color.r, color.g, color.b, color.a)
	m.color = color
}

pub fn (mut m Model) destroy() {
	C.egn_component_destroy(m.handle)
	m.handle = voidptr(0)
}

// apply pushes every field to the engine. Call it after changing fields
// directly; the set_* methods already push their field automatically.
pub fn (mut m Model) apply() {
	C.egn_model_set_mesh(m.handle, m.mesh.id)
	C.egn_model_set_texture(m.handle, m.texture.id)
	C.egn_model_set_color(m.handle, m.color.r, m.color.g, m.color.b, m.color.a)
	C.egn_component_set_enabled(m.handle, m.enabled)
}

// Sprite component methods --------------------------------------------------

pub fn (s Sprite) is_valid() bool {
	return s.handle != 0 && C.egn_component_is_valid(s.handle)
}

pub fn (mut s Sprite) set_enabled(enabled bool) {
	C.egn_component_set_enabled(s.handle, enabled)
	s.enabled = enabled
}

pub fn (mut s Sprite) set_texture(texture Texture) {
	C.egn_sprite_set_texture(s.handle, texture.id)
	s.texture = texture
}

pub fn (mut s Sprite) set_size(size Vec2) {
	C.egn_sprite_set_size(s.handle, size.x, size.y)
	s.size = size
}

pub fn (mut s Sprite) set_pivot(pivot Vec2) {
	C.egn_sprite_set_pivot(s.handle, pivot.x, pivot.y)
	s.pivot = pivot
}

pub fn (mut s Sprite) set_color(color Color) {
	C.egn_sprite_set_color(s.handle, color.r, color.g, color.b, color.a)
	s.color = color
}

pub fn (mut s Sprite) destroy() {
	C.egn_component_destroy(s.handle)
	s.handle = voidptr(0)
}

// apply pushes every field to the engine. Call it after changing fields
// directly; the set_* methods already push their field automatically.
pub fn (mut s Sprite) apply() {
	C.egn_sprite_set_texture(s.handle, s.texture.id)
	C.egn_sprite_set_size(s.handle, s.size.x, s.size.y)
	C.egn_sprite_set_pivot(s.handle, s.pivot.x, s.pivot.y)
	C.egn_sprite_set_color(s.handle, s.color.r, s.color.g, s.color.b, s.color.a)
	C.egn_component_set_enabled(s.handle, s.enabled)
}
