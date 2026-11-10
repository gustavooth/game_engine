module main

import math
import egn {
	Animation,
	Camera,
	Canvas,
	GraphicsPreset,
	CharacterController,
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
	TextTexture,
	Vec2,
	Vec3,
	Viewport,
	create,
	graphics_preset,
	graphics_settings,
	log_error,
	log_info,
	new_egn_string,
	set_graphics_settings,
	rgb,
	rgba,
	vec2,
	vec3,
}

struct FpsGlyph {
    object Object
    sprite Sprite
}

struct FpsCounter {
    glyphs []TextTexture
    lines []FpsGlyph
}

fn create_fps_counter(app &egn.Engine, scene Object) !FpsCounter {
    mut glyphs := []TextTexture{cap: 11}
    for glyph_text in ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ' FPS'] {
        text := new_egn_string(glyph_text)!
        glyph := app.load_text('assets/fonts/DejaVuSansMono.ttf', text, 18) or {
            for loaded in glyphs { app.release_texture(loaded.texture) }
            return err
        }
        glyphs << glyph
    }
    mut lines := []FpsGlyph{cap: 10}
    for layer in 0 .. 2 {
        for _ in 0 .. 5 {
            object := scene.create_object('FPS counter')
            object.set_render_domain(.canvas)
            object.set_canvas_anchor(.top_left)
            sprite := object.add_component(Sprite{
                texture: glyphs[0].texture
                size: vec2(f32(glyphs[0].width), f32(glyphs[0].height))
                pivot: vec2(0, 1)
                color: if layer == 0 { rgba(0, 0, 0, 0.9) } else { rgba(1, 1, 1, 1) }
            })
            lines << FpsGlyph{object: object, sprite: sprite}
        }
    }
    return FpsCounter{glyphs: glyphs, lines: lines}
}

fn (counter FpsCounter) show(value int) {
    mut number := if value > 9999 { 9999 } else if value < 0 { 0 } else { value }
    mut digits := [4]int{}
    mut digit_count := 0
    for {
        digits[digit_count] = number % 10
        digit_count++
        number /= 10
        if number == 0 { break }
    }
    for layer in 0 .. 2 {
        mut x := f32(12 + if layer == 0 { 1 } else { 0 })
        y := f32(-12 - if layer == 0 { 1 } else { 0 })
        for i in 0 .. 5 {
            item := counter.lines[layer * 5 + i]
            if i > digit_count {
                item.object.set_active(false)
                continue
            }
            index := if i == digit_count { 10 } else { digits[digit_count - 1 - i] }
            glyph := counter.glyphs[index]
            item.object.set_active(true)
            item.object.set_position(vec3(x, y, 0))
            mut sprite := item.sprite
            sprite.set_texture(glyph.texture)
            sprite.set_size(vec2(f32(glyph.width), f32(glyph.height)))
            x += f32(glyph.width)
        }
    }
}

// EGN feature demo.
//
// Controls:
//   WASD ........ walk (W/S along the view direction, A/D strafe)
//   Space ....... jump
//   Mouse ....... look (click to capture the pointer)
//   Esc ......... quit
//   F1 .......... toggle vsync
//   F2 .......... cycle FPS cap (off / 60 / 120 / 144)
//   F3 .......... cycle graphics preset (low/medium/high/ultra)
//   F4 .......... toggle HDR
//   F5 .......... toggle shadows
//   F6 .......... toggle ambient occlusion
//   F7 .......... toggle global illumination
//   F8 .......... toggle atmosphere
//   F9 .......... toggle bloom
//   F10 ......... toggle auto exposure
//   F11 ......... toggle color grading
//   L ........... toggle warm 3D LUT
//   F12 ......... save a screenshot
//   LMB ......... (re)capture the pointer

fn main() {
	mut graphics := graphics_preset(.ultra)
	mut app := create(Settings{
		title:      'EGN - FPS / Physics / Particles'
		width:      1280
		height:     720
		x:          100
		y:          100
		vsync:      false
		target_fps: 0
		graphics:   graphics
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
	warm_lut := app.load_lut('assets/luts/warm.cube') or {
		log_error('could not load warm LUT: ${err}')
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

	// The demo creates its own camera and sun; the engine scene starts empty.
	player := scene.create_object('Player')
	player.set_position(vec3(0.0, 0.9, 5.5))
	controller := player.add_component(CharacterController{})
	camera_object := player.create_object('FPS Camera')
	camera_object.set_position(vec3(0.0, 0.6, 0.0))
	camera_object.set_layer(u32(1))
	camera := camera_object.add_component(Camera{
		fov: 65.0
		near: 0.05
		far: 300.0
		clear_color: rgba(0.035, 0.055, 0.11, 1.0)
	})
	sun := scene.create_object('Sun')
	sun.set_position(vec3(0.0, 10.0, 0.0))
	sun.set_euler_deg(vec3(-50.0, -30.0, 0.0))
	sun.add_component(Light{
		light_type: .directional
		color: rgb(1.0, 0.95, 0.85)
		intensity: 1.0
		cast_shadows: true
	})

	// Canvas in screen pixels; the default center anchor keeps the crosshair
	// centered even when the window is resized.
	crosshair_canvas := scene.create_object('Crosshair Canvas')
	crosshair_canvas.add_component(Canvas{ order: 1000 })
	for i in 0 .. 4 {
		line := scene.create_object('Crosshair ${i}')
		line.set_render_domain(.canvas)
		line.add_component(Sprite{
			size: if i == 0 { vec2(18, 4) } else if i == 1 { vec2(4, 18) } else if i == 2 { vec2(14, 2) } else { vec2(2, 14) }
			color: if i < 2 { rgba(0, 0, 0, 0.85) } else { rgba(1, 1, 1, 0.95) }
		})
	}

	fps_counter := create_fps_counter(&app, scene) or {
		log_error('could not create FPS counter: ${err}')
		return
	}
	defer {
		for glyph in fps_counter.glyphs { app.release_texture(glyph.texture) }
	}
	fps_counter.show(0)

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

	// A separate humanoid with a looping skeletal idle animation, useful for
	// checking skinning alongside the fox's walking animation.
	humanoid_mesh := app.load_glb('assets/models/humanoid_idle.glb') or {
		log_error('could not load humanoid_idle.glb: ${err}')
		Mesh{}
	}
	if humanoid_mesh.is_valid() {
		humanoid := scene.create_object('Humanoid Idle')
		humanoid.set_position(vec3(1.6, 0.02, 2.7))
		humanoid.set_euler_deg(vec3(0, 0, 0))
		humanoid.set_scale(vec3(0.32, 0.32, 0.32))
		humanoid.set_layer(u32(1))
		humanoid.add_component(Model{ mesh: humanoid_mesh })
		mut idle := humanoid.add_component(Animation{ clip: 2, speed: 1, loop: true })
		idle.play()
		log_info('humanoid idle: "${humanoid_mesh.animation_name(2)}" (${humanoid_mesh.animation_duration(2):.2f}s)')
	}

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
	fps_caps := [0, 60, 120, 144]
	mut fps_cap_index := 0
	mut held_id := u32(0)
	mut fps_sample_start := app.elapsed_time()
	mut fps_frame_count := 0
	mut want_lock := true

	mut preset_index := 3
	presets := [GraphicsPreset.low, .medium, .high, .ultra]
	for app.should_run() {

		// Keep trying until the compositor grants the pointer lock (it needs
		// an input serial, which usually arrives with pointer-enter).
		if want_lock && !app.pointer_locked() {
			app.set_pointer_lock(true)
		}

		if app.key_pressed(.escape) {
			app.quit()
		}
		if app.mouse_pressed(.left) && !app.pointer_locked() {
			want_lock = true
			app.set_pointer_lock(true)
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
		if app.key_pressed(.f3) {
			preset_index = (preset_index + 1) % presets.len
			graphics = graphics_preset(presets[preset_index])
			set_graphics_settings(graphics)
			log_info('graphics preset changed')
		}
		if app.key_pressed(.f4) {
			mut g := graphics_settings()
			g.hdr_enabled = !g.hdr_enabled
			set_graphics_settings(g)
			log_info('HDR: ${g.hdr_enabled}')
		}
		if app.key_pressed(.f5) {
			mut g := graphics_settings()
			g.shadows_enabled = !g.shadows_enabled
			set_graphics_settings(g)
			log_info('shadows: ${g.shadows_enabled}')
		}
		if app.key_pressed(.f6) {
			mut g := graphics_settings()
			g.ao_enabled = !g.ao_enabled
			set_graphics_settings(g)
			log_info('ambient occlusion: ${g.ao_enabled}')
		}
		if app.key_pressed(.f7) {
			mut g := graphics_settings()
			g.gi_enabled = !g.gi_enabled
			set_graphics_settings(g)
			log_info('global illumination: ${g.gi_enabled}')
		}
		if app.key_pressed(.f8) {
			mut g := graphics_settings()
			g.atmosphere_enabled = !g.atmosphere_enabled
			set_graphics_settings(g)
			log_info('atmosphere: ${g.atmosphere_enabled}')
		}
		if app.key_pressed(.f9) {
			mut g := graphics_settings()
			g.bloom_enabled = !g.bloom_enabled
			set_graphics_settings(g)
			log_info('bloom: ${g.bloom_enabled}')
		}
		if app.key_pressed(.f10) {
			mut g := graphics_settings()
			g.auto_exposure_enabled = !g.auto_exposure_enabled
			set_graphics_settings(g)
			log_info('auto exposure: ${g.auto_exposure_enabled}')
		}
		if app.key_pressed(.f11) {
			mut g := graphics_settings()
			g.color_grading_enabled = !g.color_grading_enabled
			set_graphics_settings(g)
			log_info('color grading: ${g.color_grading_enabled}')
		}
		if app.key_pressed(.l) && warm_lut.is_valid() {
			mut g := graphics_settings()
			g.lut_enabled = !g.lut_enabled
			g.lut_texture = warm_lut.id
			set_graphics_settings(g)
			log_info('warm LUT: ${g.lut_enabled}')
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
		// Ground movement follows yaw; look pitch does not affect walking.
		forward := vec3(-f32(math.sin(f64(yaw))), 0, -f32(math.cos(f64(yaw))))
		right := vec3(f32(math.cos(f64(yaw))), 0, -f32(math.sin(f64(yaw))))
		if app.key_down(.w) { move = move.add(forward) }
		if app.key_down(.s) { move = move.sub(forward) }
		if app.key_down(.d) { move = move.add(right) }
		if app.key_down(.a) { move = move.sub(right) }
		if app.key_pressed(.space) { controller.jump(5.0) }
		if app.key_down(.shift) { move_speed = 9.0 } else { move_speed = 5.0 }
		if move.length() > 0.001 { move = move.normalized().scale(move_speed) }
		controller.set_velocity(move.x, move.z)
		camera_object.set_look_angles(pitch, yaw, 0.0)
        look := vec3(-f32(math.sin(f64(yaw))) * f32(math.cos(f64(pitch))),
            f32(math.sin(f64(pitch))), -f32(math.cos(f64(yaw))) * f32(math.cos(f64(pitch))))
        if app.pointer_locked() && app.mouse_pressed(.left) {
            if held_id != 0 {
                held_id = 0
            } else {
                hits := app.mouse_ray_cast(camera, 4.0)
                if hits.len > 0 {
                    if target := app.object_by_id(hits[0].object_id) {
                        if rb := target.get_component[Rigidbody]() {
                            if rb.motion_type == .dynamic { held_id = target.id() }
                        }
                    }
                }
            }
        }
        if held_id != 0 {
            if target := app.object_by_id(held_id) {
                if mut rb := target.get_component[Rigidbody]() {
                    if app.mouse_pressed(.right) {
                        rb.apply_impulse(look.scale(8.0))
                        held_id = 0
                    } else {
                        eye := player.position().add(vec3(0, 0.6, 0))
                        delta := eye.add(look.scale(2.0)).sub(target.position())
                        rb.set_linear_velocity(delta.scale(8.0))
                    }
                } else { held_id = 0 }
            } else { held_id = 0 }
        }


		// Animate the camp fire's light intensity slightly.
		fire_light_object.set_position(vec3(2.4 + f32(math.sin(f64(f32(app.elapsed_time()) * 3.0))) * 0.08, 1.1,
			-1.6 + f32(math.cos(f64(f32(app.elapsed_time()) * 2.3))) * 0.08))

		app.frame()
		fps_frame_count++
		elapsed := app.elapsed_time() - fps_sample_start
		if elapsed >= 0.5 {
			fps_counter.show(int(f64(fps_frame_count) / elapsed + 0.5))
			fps_frame_count = 0
			fps_sample_start = app.elapsed_time()
		}
	}

	log_info('EGN feature demo finished')
}
