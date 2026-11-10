module main

import math
import egn {
	Animation,
	Camera,
	Collider,
	Color,
	Light,
	Mesh,
	Model,
	MotionType,
	Object,
	Particles,
	Projection,
	Rigidbody,
	Settings,
	Sprite,
	Texture,
	Vec2,
	Vec3,
	Viewport,
	create,
	log_error,
	log_info,
	rgb,
	rgba,
	vec2,
	vec3,
}

// EGN feature demo.
//
// Controls:
//   WASD ........ walk
//   Space/Ctrl .. move up/down
//   Mouse ....... look (click to capture the pointer)
//   Esc ......... release the pointer
//   Q ........... quit
//   F1 .......... toggle vsync
//   F2 .......... cycle FPS cap (off / 60 / 120 / 144)
//   F12 ......... save a screenshot
//   LMB ......... (re)capture the pointer

fn main() {
	mut app := create(Settings{
		title:      'EGN - FPS / Physics / Particles'
		width:      1280
		height:     720
		x:          100
		y:          100
		vsync:      true
		target_fps: 0
	}) or {
		eprintln('failed to start the engine: ${err}')
		exit(1)
	}
	defer {
		app.destroy()
	}

	log_info('EGN feature demo started')

	ground_texture := app.load_texture('assets/textures/ground.png') or {
		log_error('could not load ground texture: ${err}')
		Texture{}
	}
	checker_texture := app.load_texture('assets/textures/checker.png') or {
		log_error('could not load checker texture: ${err}')
		Texture{}
	}
	particle_texture := app.load_texture('assets/textures/particle.png') or {
		log_error('could not load particle texture: ${err}')
		Texture{}
	}
	box_mesh := app.load_obj('assets/models/box.obj') or {
		log_error('could not load box.obj: ${err}')
		Mesh{}
	}
	fox_mesh := app.load_glb('assets/models/fox.glb') or {
		log_error('could not load fox.glb: ${err}')
		Mesh{}
	}

	scene := app.scene()
	scene.set_layer(u32(0))

	// Camera: reuse the default camera and drive it like an FPS controller.
	camera_object := scene.child_at(0) or {
		log_error('default camera object was not found')
		exit(1)
	}
	camera_object.set_name('FPS Camera')
	camera_object.set_position(vec3(0.0, 1.5, 5.5))
	camera_object.set_layer(u32(1))
	mut camera := camera_object.get_component[Camera]() or {
		log_error('default camera component was not found')
		exit(1)
	}
	camera.set_layer_mask(u32(0xFFFFFFFF))
	camera.set_clear_color(rgba(0.035, 0.055, 0.11, 1.0))
	camera.set_fov(65.0)
	camera.set_clip_planes(0.05, 300.0)

	// Ground: a wide, thin cube with a tiled-ish grid texture and a static
	// box collider.
	ground := scene.create_object('Ground')
	ground.set_position(vec3(0.0, -0.05, 0.0))
	ground.set_scale(vec3(20.0, 0.1, 20.0))
	ground.set_layer(u32(1))
	ground.add_component(Model{
		mesh:    app.mesh_cube()
		texture: ground_texture
		color:   rgb(1.0, 1.0, 1.0)
	})
	ground.add_component(Collider{
		shape: .box
		size:  vec3(1.0, 1.0, 1.0)
	})

	// Animated fox character (Khronos Fox sample, CC0 + CC-BY 4.0).
	fox := scene.create_object('Fox')
	fox.set_position(vec3(0.0, 0.03, 0.0))
	fox.set_euler_deg(vec3(0.0, 0.0, 0.0))
	fox.set_scale(vec3(0.02, 0.02, 0.02))
	fox.set_layer(u32(1))
	fox.add_component(Model{
		mesh:  fox_mesh
		color: rgb(1.0, 1.0, 1.0)
	})
	mut fox_animation := fox.add_component(Animation{
		clip:  1 // Walk
		speed: 1.3
		loop:  true
	})
	fox_animation.play()
	log_info('fox clips: 0="${fox_mesh.animation_name(0)}" (${fox_mesh.animation_duration(0):.2f}s), ' +
		'1="${fox_mesh.animation_name(1)}" (${fox_mesh.animation_duration(1):.2f}s), ' +
		'2="${fox_mesh.animation_name(2)}" (${fox_mesh.animation_duration(2):.2f}s)')

	// Camp fire: particles + warm point light.
	fire := scene.create_object('Camp Fire')
	fire.set_position(vec3(2.4, 0.25, -1.6))
	fire.set_layer(u32(1))
	mut fire_particles := fire.add_component(Particles{
		max_particles:   700
		texture:         particle_texture
		spawn_rate:      95.0
		lifetime:        1.4
		speed:           1.45
		speed_variation: 0.35
		gravity:         1.2
		drag:            0.35
		spread:          0.45
		radius:          0.16
		direction:       vec3(0.0, 1.0, 0.0)
		size_start:      0.36
		size_end:        0.03
		fade_in:         0.08
		fade_out:        0.35
		color_start:     rgba(1.0, 0.78, 0.25, 1.0)
		color_end:       rgba(0.9, 0.08, 0.02, 0.0)
	})
	fire_particles.emit(80)

	fire_light_object := scene.create_object('Fire Light')
	fire_light_object.set_position(vec3(2.4, 1.1, -1.6))
	fire_light_object.set_layer(u32(1))
	fire_light_object.add_component(Light{
		light_type: .point
		color:      rgb(1.0, 0.42, 0.08)
		intensity:  2.4
		range:      9.0
	})

	// Blue crystal with a cold point light and a small particle halo.
	crystal := scene.create_object('Crystal')
	crystal.set_position(vec3(-2.6, 0.75, -1.2))
	crystal.set_scale(vec3(0.7, 1.0, 0.7))
	crystal.set_layer(u32(1))
	crystal.add_component(Model{
		mesh:  app.mesh_sphere()
		color: rgb(0.18, 0.48, 1.0)
	})
	crystal.add_component(Collider{
		shape:  .sphere
		radius: 0.55
	})
	mut crystal_particles := crystal.add_component(Particles{
		max_particles:   260
		texture:         particle_texture
		spawn_rate:      38.0
		lifetime:        2.0
		speed:           0.65
		speed_variation: 0.5
		gravity:         -0.15
		drag:            0.7
		spread:          0.9
		radius:          0.55
		direction:       vec3(0.0, 1.0, 0.0)
		size_start:      0.16
		size_end:        0.01
		color_start:     rgba(0.35, 0.75, 1.0, 0.9)
		color_end:       rgba(0.1, 0.2, 1.0, 0.0)
	})
	crystal_particles.emit(30)

	crystal_light_object := scene.create_object('Crystal Light')
	crystal_light_object.set_position(vec3(-2.6, 1.3, -1.2))
	crystal_light_object.set_layer(u32(1))
	crystal_light_object.add_component(Light{
		light_type: .point
		color:      rgb(0.25, 0.5, 1.0)
		intensity:  1.6
		range:      7.0
	})

	// Physics props.
	mut prop_index := 0
	for prop_index < 5 {
		x := -1.2 + f32(prop_index) * 0.55
		height := 1.6 + f32(prop_index) * 1.1
		prop := scene.create_object('Falling Box ${prop_index}')
		prop.set_position(vec3(x, height, 1.6))
		prop.set_euler_deg(vec3(f32(prop_index) * 9.0, f32(prop_index) * 17.0, 0.0))
		prop.set_scale(vec3(0.5, 0.5, 0.5))
		prop.set_layer(u32(1))
		prop.add_component(Model{
			mesh:    app.mesh_cube()
			texture: checker_texture
			color:   rgb(1.0, 1.0, 1.0)
		})
		prop.add_component(Rigidbody{
			motion_type: .dynamic
			mass:        1.0
			friction:    0.55
		})
		prop.add_component(Collider{
			shape: .box
			size:  vec3(1.0, 1.0, 1.0)
		})
		prop_index++
	}

	sphere := scene.create_object('Physics Sphere')
	sphere.set_position(vec3(2.8, 2.2, 1.9))
	sphere.set_scale(vec3(0.45, 0.45, 0.45))
	sphere.set_layer(u32(1))
	sphere.add_component(Model{
		mesh:  app.mesh_sphere()
		color: rgb(0.95, 0.5, 0.12)
	})
	sphere.add_component(Rigidbody{
		motion_type: .dynamic
		mass:        2.0
		friction:    0.35
		restitution: 0.35
	})
	sphere.add_component(Collider{
		shape:  .sphere
		radius: 0.5
	})

	// 2D physics API smoke test: a body constrained to the XY plane. It
	// falls and rolls while the main demo remains a 3D scene.
	body_2d := scene.create_object('2D Physics Sphere')
	body_2d.set_position(vec3(1.2, 3.2, -2.6))
	body_2d.set_scale(vec3(0.35, 0.35, 0.35))
	body_2d.set_layer(u32(1))
	body_2d.add_component(Model{
		mesh:  app.mesh_sphere()
		color: rgb(0.55, 0.95, 0.35)
	})
	body_2d.add_component(Rigidbody{
		motion_type: .dynamic
		is_2d:       true
		mass:        1.0
	})
	body_2d.add_component(Collider{
		shape:  .sphere
		radius: 0.5
	})

	// Dynamic mesh collider: the whole multi-material OBJ is used as the
	// collision shape of a falling rigid body.
	obj_prop := scene.create_object('Dynamic Mesh Prop')
	obj_prop.set_position(vec3(-2.8, 1.9, 1.8))
	obj_prop.set_euler_deg(vec3(0.0, 25.0, 0.0))
	obj_prop.set_scale(vec3(0.6, 0.6, 0.6))
	obj_prop.set_layer(u32(1))
	obj_prop.add_component(Model{
		mesh:  box_mesh
		color: rgb(1.0, 1.0, 1.0)
	})
	obj_prop.add_component(Rigidbody{
		motion_type: .dynamic
		mass:        1.5
		friction:    0.6
	})
	obj_prop.add_component(Collider{
		shape: .mesh
		mesh:  box_mesh
	})

	// A non-physics multi-material prop for the OBJ loader.
	pedestal := scene.create_object('OBJ Pedestal')
	pedestal.set_position(vec3(3.6, 0.35, -2.4))
	pedestal.set_scale(vec3(0.7, 0.7, 0.7))
	pedestal.set_layer(u32(1))
	pedestal.add_component(Model{
		mesh:  box_mesh
		color: rgb(1.0, 1.0, 1.0)
	})

	log_info('demo scene: root id ${scene.id()}, children ${scene.child_count()}')

	// FPS controller state.
	mut yaw := f32(0.0)
	mut pitch := f32(-0.08)
	sensitivity := f32(0.0022)
	mut move_speed := f32(5.0)
	mut camera_position := vec3(0.0, 1.5, 5.5)
	fps_caps := [0, 60, 120, 144]
	mut fps_cap_index := 0
	mut want_lock := true

	for app.should_run() {
		dt := f32(app.delta_time())

		// Keep trying until the compositor grants the pointer lock (it needs
		// an input serial, which usually arrives with pointer-enter).
		if want_lock && !app.pointer_locked() {
			app.set_pointer_lock(true)
		}

		if app.key_pressed(.escape) {
			want_lock = false
			if app.pointer_locked() {
				app.set_pointer_lock(false)
			}
			log_info('pointer released - click to recapture')
		}
		if app.mouse_pressed(.left) && !app.pointer_locked() {
			want_lock = true
			app.set_pointer_lock(true)
		}
		if app.key_pressed(.q) {
			app.quit()
		}

		if app.key_pressed(.f1) {
			app.set_vsync(!app.vsync())
			log_info('vsync ${app.vsync()}')
		}
		if app.key_pressed(.f2) {
			fps_cap_index = (fps_cap_index + 1) % fps_caps.len
			cap := fps_caps[fps_cap_index]
			app.set_target_fps(cap)
			if cap == 0 {
				log_info('target fps: unlimited')
			} else {
				log_info('target fps: ${cap}')
			}
		}
		if app.key_pressed(.f12) {
			app.screenshot('screenshot.png')
			log_info('screenshot requested: screenshot.png')
		}

		if app.pointer_locked() {
			mx, my := app.mouse_delta()
			yaw -= mx * sensitivity
			pitch -= my * sensitivity
			pitch = if pitch < -1.45 {
				-1.45
			} else if pitch > 1.45 {
				1.45
			} else {
				pitch
			}
		}

		mut move := vec3(0.0, 0.0, 0.0)
		// Forward follows the full view direction (including pitch); strafing
		// remains horizontal so the camera can fly freely in 3D.
		cos_pitch := f32(math.cos(f64(pitch)))
		forward := vec3(
			-f32(math.sin(f64(yaw))) * cos_pitch,
			f32(math.sin(f64(pitch))),
			-f32(math.cos(f64(yaw))) * cos_pitch,
		)
		right := vec3(f32(math.cos(f64(yaw))), 0.0, -f32(math.sin(f64(yaw))))
		if app.key_down(.w) {
			move = move.add(forward)
		}
		if app.key_down(.s) {
			move = move.sub(forward)
		}
		if app.key_down(.d) {
			move = move.add(right)
		}
		if app.key_down(.a) {
			move = move.sub(right)
		}
		if app.key_down(.space) {
			move.y += 1.0
		}
		if app.key_down(.control) {
			move.y -= 1.0
		}
		if app.key_down(.shift) {
			move_speed = 9.0
		} else {
			move_speed = 5.0
		}
		if move.length() > 0.001 {
			camera_position = camera_position.add(move.normalized().scale(move_speed * dt))
		}
		camera_object.set_position(camera_position)
		camera_object.set_look_angles(pitch, yaw, 0.0)

		// Animate the camp fire's light intensity slightly.
		fire_light_object.set_position(vec3(2.4 + f32(math.sin(f64(f32(app.elapsed_time()) * 3.0))) * 0.08, 1.1,
			-1.6 + f32(math.cos(f64(f32(app.elapsed_time()) * 2.3))) * 0.08))

		app.frame()
	}

	log_info('EGN feature demo finished')
}
