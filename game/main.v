module main

import egn {
	Camera,
	Color,
	Mesh,
	Model,
	Object,
	Projection,
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

// Scene demo: two cameras splitting the window.
//
// - Left camera: perspective, layers 1 + 2.
// - Right camera: orthographic, layers 1 + 3.
// - Cube and sphere are on layer 1, so both cameras see them.
// - Each camera also sees a sprite that belongs only to its layer.
//
// Controls: Q/Esc quits, F12 saves a screenshot.

fn main() {
	mut app := create(Settings{
		title:  'EGN - Cena / Split Screen'
		width:  1100
		height: 620
		x:      100
		y:      100
	}) or {
		eprintln('failed to start the engine: ${err}')
		exit(1)
	}
	defer {
		app.destroy()
	}

	log_info('Scene demo started')

	checker := app.load_texture('assets/textures/checker.png') or {
		log_error('could not load assets/textures/checker.png: ${err}')
		Texture{}
	}
	defer {
		app.release_texture(checker)
	}

	scene := app.scene()

	// Reuse the engine's default camera as the left camera.
	left_camera_object := scene.child_at(0) or {
		log_error('default camera object was not found')
		exit(1)
	}
	left_camera_object.set_name('Left Camera')
	left_camera_object.set_position(vec3(-2.2, 1.6, 5.0))
	left_camera_object.look_at(vec3(0, 0, 0))

	mut left_camera := left_camera_object.get_component[Camera]() or {
		log_error('default camera component was not found')
		exit(1)
	}
	left_camera.set_viewport(Viewport{ x: 0.0, y: 0.0, width: 0.5, height: 1.0 })
	left_camera.set_layer_mask(u32((1 << 1) | (1 << 2)))
	left_camera.set_clear_color(rgba(0.02, 0.04, 0.12, 1.0))
	left_camera.set_order(0)

	// Right camera: orthographic, same shared layer plus its own sprite layer.
	right_camera_object := scene.create_object('Right Camera')
	right_camera_object.set_position(vec3(0.0, 0.8, 6.0))
	right_camera_object.look_at(vec3(0, 0, 0))
	right_camera_object.add_component(Camera{
		projection:  .orthographic
		ortho_size:  6.0
		viewport:    Viewport{ x: 0.5, y: 0.0, width: 0.5, height: 1.0 }
		layer_mask:  u32((1 << 1) | (1 << 3))
		clear_color: rgba(0.08, 0.02, 0.10, 1.0)
		order:       0
	})

	// Shared layer 1: both cameras render these on their half of the window.
	// The cube is a child of a pivot to demonstrate hierarchy: the pivot's
	// transform is accumulated with the child's local transform.
	cube_pivot := scene.create_object('Cube Pivot')
	cube_pivot.set_position(vec3(-0.6, 0.0, 0))

	cube := cube_pivot.create_child_euler_deg('Cube', vec3(0, 0.3, 0), vec3(20, 0, 0), vec3(1, 1, 1))
	cube.set_layer(u32(1))
	cube.add_component(Model{
		mesh:  app.mesh_cube()
		color: rgb(1.0, 0.42, 0.12)
	})

	sphere := scene.create_child_euler_deg('Sphere', vec3(0.8, 0.2, 0), vec3(0, 0, 0), vec3(1, 1, 1))
	sphere.set_layer(u32(1))
	sphere.add_component(Model{
		mesh:  app.mesh_sphere()
		color: rgb(0.20, 0.65, 1.0)
	})

	// Layer 2: only the left camera sees this sprite.
	left_sprite := scene.create_child_euler_deg('Left Sprite', vec3(-1.9, -0.5, 0.5), vec3(0, 10, 0), vec3(1, 1, 1))
	left_sprite.set_layer(u32(2))
	left_sprite.add_component(Sprite{
		texture: checker
		size:    vec2(1.0, 1.0)
		color:   rgb(1, 1, 1)
	})

	// Layer 3: only the right camera sees this sprite.
	right_sprite := scene.create_child_euler_deg('Right Sprite', vec3(1.9, -0.5, 0.5), vec3(0, -10, 0), vec3(1, 1, 1))
	right_sprite.set_layer(u32(3))
	right_sprite.add_component(Sprite{
		texture: checker
		size:    vec2(1.0, 1.0)
		color:   rgb(1, 1, 1)
	})

	log_info('scene: root id ${scene.id()}, children ${scene.child_count()}')

	mut angle := f32(0)

	for app.should_run() {
		dt := f32(app.delta_time())
		angle += dt * 1.3

		cube_pivot.set_euler(vec3(0, angle * 0.35, 0))
		cube.set_euler(vec3(0.35, angle, 0))
		sphere.set_euler(vec3(0, -angle * 0.7, 0))

		app.frame()

		if app.key_pressed(.f12) {
			app.screenshot('screenshot.png')
			log_info('screenshot requested: screenshot.png')
		}
		if app.key_pressed(.q) || app.key_pressed(.escape) {
			app.quit()
		}
	}

	log_info('Scene demo finished')
}
