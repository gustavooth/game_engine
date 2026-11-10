module egn

// Scene graph and component API.
//
// Every object has a local TRS transform, a layer (0..31), an active flag and
// a parent/children hierarchy. Children accumulate their parent transforms.
// The scene itself is the root object (id 1).

pub enum ComponentType {
	camera    = 0
	model     = 1
	sprite    = 2
	animation = 3
	light     = 4
	rigidbody = 5
	collider  = 6
	particles = 7
	camera_2d = 8
	canvas = 9
}

pub enum RenderDomain {
    world_3d = 0
    world_2d = 1
    canvas = 2
}

pub enum CanvasAnchor {
    bottom_left = 0
    bottom = 1
    bottom_right = 2
    left = 3
    center = 4
    right = 5
    top_left = 6
    top = 7
    top_right = 8
}

pub struct Camera2D {
mut:
    handle voidptr
pub mut:
    ortho_size f32 = 10
    layer_mask u32 = 0xffffffff
    order int
    enabled bool = true
}

pub struct Canvas {
mut:
    handle voidptr
pub mut:
    layer_mask u32 = 0xffffffff
    order int
    enabled bool = true
}

// CanvasBounds is the screen-space rectangle (pixels, top-left origin) covered
// by the sprites of a canvas object.
pub struct CanvasBounds {
pub mut:
    x      f32
    y      f32
    width  f32
    height f32
}

pub struct CharacterController {
mut:
    handle voidptr
pub mut:
    is_2d bool
    radius f32 = 0.3
    height f32 = 1.8
    gravity f32 = 9.81
    enabled bool = true
}

pub type CharacterController3D = CharacterController

pub struct CharacterController2D {
mut:
    handle voidptr
pub mut:
    radius f32 = 0.3
    height f32 = 1.8
    gravity f32 = 9.81
    enabled bool = true
}
pub fn (c CharacterController2D) set_velocity(x f32) { C.egn_character_set_velocity(c.handle, x, 0) }
pub fn (c CharacterController2D) jump(speed f32) { C.egn_character_jump(c.handle, speed) }
pub fn (c CharacterController2D) grounded() bool { return C.egn_character_is_grounded(c.handle) }

// set_velocity controls horizontal world-space movement. For 2D, z is ignored.
pub fn (c CharacterController) set_velocity(x f32, z f32) {
    C.egn_character_set_velocity(c.handle, x, z)
}
pub fn (c CharacterController) jump(speed f32) { C.egn_character_jump(c.handle, speed) }
pub fn (c CharacterController) grounded() bool { return C.egn_character_is_grounded(c.handle) }

pub enum LightType {
	directional = 0
	point       = 1
}

pub enum MotionType {
	static    = 0
	kinematic = 1
	dynamic   = 2
}

pub enum ColliderShape {
	box    = 0
	sphere = 1
	mesh   = 2
	capsule = 3
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

// Camera3D is the explicit name for the existing 3D Camera component.
pub type Camera3D = Camera

// Model renders a 3D mesh with a solid colour/texture.
pub struct Model {
mut:
	handle voidptr
pub mut:
	mesh         Mesh    = Mesh{}
	texture      Texture = Texture{}
	color        Color   = Color{ r: 1.0, g: 1.0, b: 1.0, a: 1.0 }
	metallic     f32
	roughness    f32 = 1.0
	cast_shadows bool    = true
	water        bool
	enabled      bool    = true
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

// Animation plays a clip from the skinned mesh referenced by the Model
// component on the same object.
pub struct Animation {
mut:
	handle voidptr
pub mut:
	clip    i32
	time    f32
	speed   f32  = 1.0
	loop    bool = true
	playing bool = true
	enabled bool = true
}

// Rigidbody adds Jolt physics simulation to an object. Attach a Collider to
// the same object to define its shape.
pub struct Rigidbody {
mut:
	handle voidptr
pub mut:
	motion_type     MotionType = .dynamic
	is_2d           bool
	gravity_factor  f32 = 1.0
	mass            f32 = 1.0
	friction        f32 = 0.5
	restitution     f32
	linear_damping  f32
	angular_damping f32
	enabled         bool = true
}

// Collider defines the shape used by a Rigidbody (or a static collider when
// no Rigidbody is present). For capsules, size.y is the total height and
// radius is the radius of each hemispherical end.
pub struct Collider {
mut:
	handle voidptr
pub mut:
	shape   ColliderShape = .box
	size    Vec3          = Vec3{ x: 1.0, y: 1.0, z: 1.0 }
	radius  f32           = 0.5
	mesh    Mesh          = Mesh{}
	offset  Vec3
	enabled bool = true
}

// Particles is a GPU-simulated billboard particle emitter.
pub struct Particles {
mut:
	handle voidptr
pub mut:
	max_particles   u32     = 512
	texture         Texture = Texture{}
	spawn_rate      f32     = 64.0
	lifetime        f32     = 2.0
	speed           f32     = 2.0
	speed_variation f32     = 0.3
	gravity         f32     = 1.0
	drag            f32     = 0.2
	spread          f32     = 0.35
	radius          f32     = 0.1
	direction       Vec3    = Vec3{ x: 0.0, y: 1.0, z: 0.0 }
	size_start      f32     = 0.3
	size_end        f32     = 0.05
	size_scale      f32     = 1.0
	fade_in         f32     = 0.1
	fade_out        f32     = 0.35
	color_start     Color   = Color{ r: 1.0, g: 0.75, b: 0.25, a: 1.0 }
	color_end       Color   = Color{ r: 0.8, g: 0.1, b: 0.05, a: 0.0 }
	enabled         bool    = true
}

// Light is either a directional sun or a point light.
pub struct Light {
mut:
	handle voidptr
pub mut:
	light_type   LightType = .point
	color        Color     = Color{ r: 1.0, g: 1.0, b: 1.0, a: 1.0 }
	intensity    f32       = 1.0
	range        f32       = 10.0
	cast_shadows bool
	enabled      bool = true
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

pub fn (o Object) set_render_domain(domain RenderDomain) {
    C.egn_object_set_render_domain(o.handle, i32(domain))
}
pub fn (o Object) set_canvas_anchor(anchor CanvasAnchor) {
    C.egn_object_set_canvas_anchor(o.handle, i32(anchor))
}

// canvas_bounds returns the screen-space AABB of this object's sprites. The
// framebuffer size can be obtained from Engine.window_size().
pub fn (o Object) canvas_bounds(framebuffer_width f32, framebuffer_height f32) ?CanvasBounds {
    mut x := f32(0)
    mut y := f32(0)
    mut width := f32(0)
    mut height := f32(0)
    if !C.egn_object_canvas_rect(o.handle, framebuffer_width, framebuffer_height, &x, &y, &width, &height) {
        return none
    }
    return CanvasBounds{x: x, y: y, width: width, height: height}
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

// set_look_angles orienta uma camera livre a partir de pitch/yaw/roll em
// radianos. A ordem e yaw * pitch * roll, a mesma de uma camera FPS.
pub fn (o Object) set_look_angles(pitch f32, yaw f32, roll f32) {
	qy := quat_axis_angle(vec3(0, 1, 0), yaw)
	qx := quat_axis_angle(vec3(1, 0, 0), pitch)
	qz := quat_axis_angle(vec3(0, 0, 1), roll)
	o.set_rotation_quat(qy.mul(qx).mul(qz))
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
	} $else $if T is Camera2D {
        h := C.egn_object_add_camera_2d(o.handle, component.ortho_size, component.layer_mask, i32(component.order), component.enabled)
        mut result := component
        result.handle = h
        return result
    } $else $if T is Canvas {
        h := C.egn_object_add_canvas(o.handle, component.layer_mask, i32(component.order), component.enabled)
        mut result := component
        result.handle = h
        return result
	} $else $if T is CharacterController2D {
        h := C.egn_object_add_character(o.handle, true, component.radius, component.height, component.gravity, component.enabled)
        mut result := component
        result.handle = h
        return result
	} $else $if T is CharacterController {
        h := C.egn_object_add_character(o.handle, component.is_2d, component.radius, component.height, component.gravity, component.enabled)
        mut result := component
        result.handle = h
        return result
	} $else $if T is Model {
		h := C.egn_object_add_model(o.handle, component.mesh.id, component.texture.id,
			component.color.r, component.color.g, component.color.b, component.color.a,
			component.cast_shadows, component.enabled)
		mut result := component
		result.handle = h
		if !C.egn_model_set_pbr(h, component.metallic, component.roughness) {
			C.egn_model_get_pbr(h, &result.metallic, &result.roughness)
		}
		C.egn_model_set_water(h, component.water)
		return result
	} $else $if T is Sprite {
		h := C.egn_object_add_sprite(o.handle, component.texture.id, component.size.x, component.size.y,
			component.pivot.x, component.pivot.y, component.color.r, component.color.g,
			component.color.b, component.color.a, component.enabled)
		mut result := component
		result.handle = h
		return result
	} $else $if T is Animation {
		h := C.egn_object_add_animation(o.handle, component.clip, component.speed,
			component.loop, component.playing, component.enabled)
		mut result := component
		result.handle = h
		return result
	} $else $if T is Light {
		h := C.egn_object_add_light(o.handle, i32(component.light_type),
			component.color.r, component.color.g, component.color.b,
			component.intensity, component.range, component.cast_shadows, component.enabled)
		mut result := component
		result.handle = h
		return result
	} $else $if T is Rigidbody {
		h := C.egn_object_add_rigidbody(o.handle, i32(component.motion_type), component.is_2d,
			component.gravity_factor, component.friction, component.restitution,
			component.linear_damping, component.angular_damping, component.enabled)
		C.egn_rigidbody_set_mass(h, component.mass)
		mut result := component
		result.handle = h
		return result
	} $else $if T is Collider {
		h := C.egn_object_add_collider(o.handle, i32(component.shape),
			component.size.x, component.size.y, component.size.z, component.radius,
			component.mesh.id, component.offset.x, component.offset.y, component.offset.z,
			component.enabled)
		mut result := component
		result.handle = h
		return result
	} $else $if T is Particles {
		h := C.egn_object_add_particles(o.handle, component.max_particles, component.texture.id,
			component.spawn_rate, component.lifetime, component.speed, component.speed_variation,
			component.gravity, component.drag, component.spread, component.radius,
			component.direction.x, component.direction.y, component.direction.z,
			component.size_start, component.size_end, component.size_scale,
			component.fade_in, component.fade_out,
			component.color_start.r, component.color_start.g, component.color_start.b, component.color_start.a,
			component.color_end.r, component.color_end.g, component.color_end.b, component.color_end.a,
			component.enabled)
		mut result := component
		result.handle = h
		return result
	} $else {
		compile_error('add_component: unsupported component type')
	}
	return component
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
	} $else $if T is Camera2D {
        h := C.egn_object_get_component(o.handle, i32(ComponentType.camera_2d))
        if h == 0 { return none }
        mut result := T{}
        result.handle = h
        result.ortho_size = C.egn_camera_get_ortho_size(h)
        result.layer_mask = C.egn_camera_get_layer_mask(h)
        result.order = int(C.egn_camera_get_order(h))
        result.enabled = C.egn_component_is_enabled(h)
        return result
    } $else $if T is Canvas {
        h := C.egn_object_get_component(o.handle, i32(ComponentType.canvas))
        if h == 0 { return none }
        mut result := T{}
        result.handle = h
        result.layer_mask = C.egn_camera_get_layer_mask(h)
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
		result.cast_shadows = C.egn_model_get_cast_shadows(h)
		C.egn_model_get_pbr(h, &result.metallic, &result.roughness)
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
	} $else $if T is Animation {
		h := C.egn_object_get_component(o.handle, i32(ComponentType.animation))
		if h == 0 {
			return none
		}
		mut result := T{}
		result.handle = h
		result.clip = C.egn_animation_get_clip(h)
		result.time = C.egn_animation_get_time(h)
		result.speed = C.egn_animation_get_speed(h)
		result.loop = C.egn_animation_get_loop(h)
		result.playing = C.egn_animation_get_playing(h)
		result.enabled = C.egn_component_is_enabled(h)
		return result
	} $else $if T is Light {
		h := C.egn_object_get_component(o.handle, i32(ComponentType.light))
		if h == 0 {
			return none
		}
		mut cr := f32(1)
		mut cg := f32(1)
		mut cb := f32(1)
		C.egn_light_get_color(h, &cr, &cg, &cb)
		mut result := T{}
		result.handle = h
		result.light_type = if C.egn_light_get_type(h) == 1 { .point } else { .directional }
		result.color = Color{ r: cr, g: cg, b: cb, a: 1.0 }
		result.intensity = C.egn_light_get_intensity(h)
		result.range = C.egn_light_get_range(h)
		result.cast_shadows = C.egn_light_get_cast_shadows(h)
		result.enabled = C.egn_component_is_enabled(h)
		return result
	} $else $if T is Rigidbody {
		h := C.egn_object_get_component(o.handle, i32(ComponentType.rigidbody))
		if h == 0 {
			return none
		}
		mut result := T{}
		result.handle = h
		result.motion_type = match C.egn_rigidbody_get_motion_type(h) {
			0 { .static }
			1 { .kinematic }
			else { .dynamic }
		}
		result.is_2d = C.egn_rigidbody_is_2d(h)
		result.gravity_factor = C.egn_rigidbody_get_gravity_factor(h)
		result.mass = C.egn_rigidbody_get_mass(h)
		result.friction = C.egn_rigidbody_get_friction(h)
		result.restitution = C.egn_rigidbody_get_restitution(h)
		result.linear_damping = C.egn_rigidbody_get_linear_damping(h)
		result.angular_damping = C.egn_rigidbody_get_angular_damping(h)
		result.enabled = C.egn_component_is_enabled(h)
		return result
	} $else $if T is Collider {
		h := C.egn_object_get_component(o.handle, i32(ComponentType.collider))
		if h == 0 {
			return none
		}
		mut sx := f32(1)
		mut sy := f32(1)
		mut sz := f32(1)
		C.egn_collider_get_size(h, &sx, &sy, &sz)
		mut ox := f32(0)
		mut oy := f32(0)
		mut oz := f32(0)
		C.egn_collider_get_offset(h, &ox, &oy, &oz)
		mut result := T{}
		result.handle = h
		result.shape = match C.egn_collider_get_shape(h) {
			1 { .sphere }
			2 { .mesh }
			else { .box }
		}
		result.size = vec3(sx, sy, sz)
		result.radius = C.egn_collider_get_radius(h)
		result.mesh = Mesh{
			id: C.egn_collider_get_mesh(h)
		}
		result.offset = vec3(ox, oy, oz)
		result.enabled = C.egn_component_is_enabled(h)
		return result
	} $else $if T is Particles {
		h := C.egn_object_get_component(o.handle, i32(ComponentType.particles))
		if h == 0 {
			return none
		}
		mut result := T{}
		result.handle = h
		result.max_particles = C.egn_particles_get_max(h)
		result.texture = Texture{
			id: C.egn_particles_get_texture(h)
		}
		result.spawn_rate = C.egn_particles_get_spawn_rate(h)
		result.lifetime = C.egn_particles_get_lifetime(h)
		result.speed = C.egn_particles_get_speed(h)
		result.gravity = C.egn_particles_get_gravity(h)
		result.drag = C.egn_particles_get_drag(h)
		result.spread = C.egn_particles_get_spread(h)
		mut ss := f32(1)
		mut se := f32(1)
		C.egn_particles_get_size(h, &ss, &se)
		result.size_start = ss
		result.size_end = se
		mut r0 := f32(1)
		mut g0 := f32(1)
		mut b0 := f32(1)
		mut a0 := f32(1)
		C.egn_particles_get_color_start(h, &r0, &g0, &b0, &a0)
		result.color_start = Color{ r: r0, g: g0, b: b0, a: a0 }
		mut r1 := f32(1)
		mut g1 := f32(1)
		mut b1 := f32(1)
		mut a1 := f32(0)
		C.egn_particles_get_color_end(h, &r1, &g1, &b1, &a1)
		result.color_end = Color{ r: r1, g: g1, b: b1, a: a1 }
		result.enabled = C.egn_component_is_enabled(h)
		return result
	} $else {
		compile_error('get_component: unsupported component type')
	}
	return none
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

// Stores both PBR factors atomically; each must be between 0 and 1.
pub fn (mut m Model) set_pbr(metallic f32, roughness f32) bool {
	if !C.egn_model_set_pbr(m.handle, metallic, roughness) {
		return false
	}
	m.metallic = metallic
	m.roughness = roughness
	return true
}

pub fn (mut m Model) set_cast_shadows(cast_shadows bool) {
	C.egn_model_set_cast_shadows(m.handle, cast_shadows)
	m.cast_shadows = cast_shadows
}

// set_water enables the animated water shading path for this model.
pub fn (mut m Model) set_water(water bool) {
	C.egn_model_set_water(m.handle, water)
	m.water = water
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
	if !C.egn_model_set_pbr(m.handle, m.metallic, m.roughness) {
		C.egn_model_get_pbr(m.handle, &m.metallic, &m.roughness)
	}
	C.egn_model_set_cast_shadows(m.handle, m.cast_shadows)
	C.egn_model_set_water(m.handle, m.water)
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

// Animation component methods -------------------------------------------------

pub fn (a Animation) is_valid() bool {
	return a.handle != 0 && C.egn_component_is_valid(a.handle)
}

pub fn (mut a Animation) set_enabled(enabled bool) {
	C.egn_component_set_enabled(a.handle, enabled)
	a.enabled = enabled
}

pub fn (mut a Animation) set_clip(clip i32) {
	C.egn_animation_set_clip(a.handle, clip)
	a.clip = clip
}

pub fn (mut a Animation) set_time(time f32) {
	C.egn_animation_set_time(a.handle, time)
	a.time = time
}

pub fn (mut a Animation) set_speed(speed f32) {
	C.egn_animation_set_speed(a.handle, speed)
	a.speed = speed
}

pub fn (mut a Animation) set_loop(loop bool) {
	C.egn_animation_set_loop(a.handle, loop)
	a.loop = loop
}

pub fn (mut a Animation) set_playing(playing bool) {
	C.egn_animation_set_playing(a.handle, playing)
	a.playing = playing
}

pub fn (mut a Animation) play() {
	a.set_playing(true)
}

pub fn (mut a Animation) pause() {
	a.set_playing(false)
}

pub fn (mut a Animation) destroy() {
	C.egn_component_destroy(a.handle)
	a.handle = voidptr(0)
}

// apply pushes every field to the engine. Call it after changing fields
// directly; the set_* methods already push their field automatically.
pub fn (mut a Animation) apply() {
	C.egn_animation_set_clip(a.handle, a.clip)
	C.egn_animation_set_time(a.handle, a.time)
	C.egn_animation_set_speed(a.handle, a.speed)
	C.egn_animation_set_loop(a.handle, a.loop)
	C.egn_animation_set_playing(a.handle, a.playing)
	C.egn_component_set_enabled(a.handle, a.enabled)
}

// Light component methods -----------------------------------------------------

pub fn (l Light) is_valid() bool {
	return l.handle != 0 && C.egn_component_is_valid(l.handle)
}

pub fn (mut l Light) set_enabled(enabled bool) {
	C.egn_component_set_enabled(l.handle, enabled)
	l.enabled = enabled
}

pub fn (mut l Light) set_type(light_type LightType) {
	C.egn_light_set_type(l.handle, i32(light_type))
	l.light_type = light_type
}

pub fn (mut l Light) set_color(color Color) {
	C.egn_light_set_color(l.handle, color.r, color.g, color.b)
	l.color = color
}

pub fn (mut l Light) set_intensity(intensity f32) {
	C.egn_light_set_intensity(l.handle, intensity)
	l.intensity = intensity
}

pub fn (mut l Light) set_range(range f32) {
	C.egn_light_set_range(l.handle, range)
	l.range = range
}

pub fn (mut l Light) set_cast_shadows(cast_shadows bool) {
	C.egn_light_set_cast_shadows(l.handle, cast_shadows)
	l.cast_shadows = cast_shadows
}

pub fn (mut l Light) destroy() {
	C.egn_component_destroy(l.handle)
	l.handle = voidptr(0)
}

pub fn (mut l Light) apply() {
	C.egn_light_set_type(l.handle, i32(l.light_type))
	C.egn_light_set_color(l.handle, l.color.r, l.color.g, l.color.b)
	C.egn_light_set_intensity(l.handle, l.intensity)
	C.egn_light_set_range(l.handle, l.range)
	C.egn_light_set_cast_shadows(l.handle, l.cast_shadows)
	C.egn_component_set_enabled(l.handle, l.enabled)
}

// Rigidbody component methods -------------------------------------------------

pub fn (r Rigidbody) is_valid() bool {
	return r.handle != 0 && C.egn_component_is_valid(r.handle)
}

pub fn (mut r Rigidbody) set_enabled(enabled bool) {
	C.egn_component_set_enabled(r.handle, enabled)
	r.enabled = enabled
}

pub fn (mut r Rigidbody) apply_impulse(impulse Vec3) {
	C.egn_rigidbody_apply_impulse(r.handle, impulse.x, impulse.y, impulse.z)
}

pub fn (r Rigidbody) linear_velocity() Vec3 {
	mut x := f32(0)
	mut y := f32(0)
	mut z := f32(0)
	C.egn_rigidbody_get_linear_velocity(r.handle, &x, &y, &z)
	return vec3(x, y, z)
}

pub fn (mut r Rigidbody) set_linear_velocity(velocity Vec3) {
	C.egn_rigidbody_set_linear_velocity(r.handle, velocity.x, velocity.y, velocity.z)
}

pub fn (r Rigidbody) angular_velocity() Vec3 {
	mut x := f32(0)
	mut y := f32(0)
	mut z := f32(0)
	C.egn_rigidbody_get_angular_velocity(r.handle, &x, &y, &z)
	return vec3(x, y, z)
}

pub fn (mut r Rigidbody) set_angular_velocity(velocity Vec3) {
	C.egn_rigidbody_set_angular_velocity(r.handle, velocity.x, velocity.y, velocity.z)
}

pub fn (mut r Rigidbody) destroy() {
	C.egn_component_destroy(r.handle)
	r.handle = voidptr(0)
}

pub fn (mut r Rigidbody) apply() {
	C.egn_rigidbody_set_motion_type(r.handle, i32(r.motion_type))
	C.egn_rigidbody_set_2d(r.handle, r.is_2d)
	C.egn_rigidbody_set_gravity_factor(r.handle, r.gravity_factor)
	C.egn_rigidbody_set_mass(r.handle, r.mass)
	C.egn_rigidbody_set_friction(r.handle, r.friction)
	C.egn_rigidbody_set_restitution(r.handle, r.restitution)
	C.egn_rigidbody_set_linear_damping(r.handle, r.linear_damping)
	C.egn_rigidbody_set_angular_damping(r.handle, r.angular_damping)
	C.egn_component_set_enabled(r.handle, r.enabled)
}

// Collider component methods --------------------------------------------------

pub fn (c Collider) is_valid() bool {
	return c.handle != 0 && C.egn_component_is_valid(c.handle)
}

pub fn (mut c Collider) set_enabled(enabled bool) {
	C.egn_component_set_enabled(c.handle, enabled)
	c.enabled = enabled
}

pub fn (mut c Collider) destroy() {
	C.egn_component_destroy(c.handle)
	c.handle = voidptr(0)
}

pub fn (mut c Collider) apply() {
	C.egn_collider_set_shape(c.handle, i32(c.shape))
	C.egn_collider_set_size(c.handle, c.size.x, c.size.y, c.size.z)
	C.egn_collider_set_radius(c.handle, c.radius)
	C.egn_collider_set_mesh(c.handle, c.mesh.id)
	C.egn_collider_set_offset(c.handle, c.offset.x, c.offset.y, c.offset.z)
	C.egn_component_set_enabled(c.handle, c.enabled)
}

// Particles component methods -------------------------------------------------

pub fn (p Particles) is_valid() bool {
	return p.handle != 0 && C.egn_component_is_valid(p.handle)
}

pub fn (mut p Particles) set_enabled(enabled bool) {
	C.egn_component_set_enabled(p.handle, enabled)
	p.enabled = enabled
}

pub fn (mut p Particles) set_spawn_rate(rate f32) {
	C.egn_particles_set_spawn_rate(p.handle, rate)
	p.spawn_rate = rate
}

pub fn (mut p Particles) set_lifetime(lifetime f32) {
	C.egn_particles_set_lifetime(p.handle, lifetime)
	p.lifetime = lifetime
}

pub fn (mut p Particles) set_speed(speed f32) {
	C.egn_particles_set_speed(p.handle, speed)
	p.speed = speed
}

pub fn (mut p Particles) set_spread(spread f32) {
	C.egn_particles_set_spread(p.handle, spread)
	p.spread = spread
}

pub fn (mut p Particles) set_gravity(gravity f32) {
	C.egn_particles_set_gravity(p.handle, gravity)
	p.gravity = gravity
}

pub fn (mut p Particles) set_drag(drag f32) {
	C.egn_particles_set_drag(p.handle, drag)
	p.drag = drag
}

pub fn (mut p Particles) set_direction(direction Vec3) {
	C.egn_particles_set_direction(p.handle, direction.x, direction.y, direction.z)
	p.direction = direction
}

pub fn (mut p Particles) set_size(start f32, end f32) {
	C.egn_particles_set_size(p.handle, start, end)
	p.size_start = start
	p.size_end = end
}

pub fn (mut p Particles) set_texture(texture Texture) {
	C.egn_particles_set_texture(p.handle, texture.id)
	p.texture = texture
}

pub fn (mut p Particles) set_color_start(color Color) {
	C.egn_particles_set_color_start(p.handle, color.r, color.g, color.b, color.a)
	p.color_start = color
}

pub fn (mut p Particles) set_color_end(color Color) {
	C.egn_particles_set_color_end(p.handle, color.r, color.g, color.b, color.a)
	p.color_end = color
}

pub fn (mut p Particles) emit(count u32) {
	C.egn_particles_emit(p.handle, count)
}

pub fn (mut p Particles) destroy() {
	C.egn_component_destroy(p.handle)
	p.handle = voidptr(0)
}

pub fn (mut p Particles) apply() {
	C.egn_particles_set_max(p.handle, p.max_particles)
	C.egn_particles_set_texture(p.handle, p.texture.id)
	C.egn_particles_set_spawn_rate(p.handle, p.spawn_rate)
	C.egn_particles_set_lifetime(p.handle, p.lifetime)
	C.egn_particles_set_speed(p.handle, p.speed)
	C.egn_particles_set_gravity(p.handle, p.gravity)
	C.egn_particles_set_drag(p.handle, p.drag)
	C.egn_particles_set_spread(p.handle, p.spread)
	C.egn_particles_set_direction(p.handle, p.direction.x, p.direction.y, p.direction.z)
	C.egn_particles_set_size(p.handle, p.size_start, p.size_end)
	C.egn_particles_set_color_start(p.handle, p.color_start.r, p.color_start.g, p.color_start.b, p.color_start.a)
	C.egn_particles_set_color_end(p.handle, p.color_end.r, p.color_end.g, p.color_end.b, p.color_end.a)
	C.egn_component_set_enabled(p.handle, p.enabled)
}

// Set camera viewports in normalized window coordinates.
pub fn (c Camera2D) set_viewport(viewport Viewport) {
    C.egn_camera_set_viewport(c.handle, viewport.x, viewport.y, viewport.width, viewport.height)
}
pub fn (c Canvas) set_viewport(viewport Viewport) {
    C.egn_camera_set_viewport(c.handle, viewport.x, viewport.y, viewport.width, viewport.height)
}
pub fn (c Camera2D) set_layer_mask(mask u32) { C.egn_camera_set_layer_mask(c.handle, mask) }
pub fn (c Canvas) set_layer_mask(mask u32) { C.egn_camera_set_layer_mask(c.handle, mask) }
