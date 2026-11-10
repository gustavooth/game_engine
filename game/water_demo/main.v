module main

import math
import egn

// Water map demo built from Kenney Nature Kit (CC0) modular tiles, plus a
// procedural water plane that exercises the animated water shading path.

fn load_mesh(app &egn.Engine, path string) egn.Mesh {
	return app.load_glb(path) or {
		eprintln('failed to load ${path}: ${err}')
		egn.Mesh{}
	}
}

fn create_water_mesh(app &egn.Engine) !egn.Mesh {
	positions := [
		f32(-1), 0, f32(-1),
		f32(1), 0, f32(-1),
		f32(1), 0, f32(1),
		f32(-1), 0, f32(1),
	]
	normals := [
		f32(0), 1, f32(0),
		f32(0), 1, f32(0),
		f32(0), 1, f32(0),
		f32(0), 1, f32(0),
	]
	uvs := [
		f32(0), 0,
		f32(1), 0,
		f32(1), 1,
		f32(0), 1,
	]
	indices := [u32(0), 1, 2, 0, 2, 3]
	return app.create_mesh(positions, normals, uvs, indices)
}

fn add_prop(scene egn.Object, name string, mesh egn.Mesh, position egn.Vec3,
	scale egn.Vec3) {
	if !mesh.is_valid() {
		return
	}
	object := scene.create_object(name)
	object.set_position(position)
	object.set_scale(scale)
	object.add_component(egn.Model{
		mesh: mesh
	})
}

fn main() {
	mut graphics := egn.graphics_preset(.ultra)
	graphics.exposure_compensation = -0.5
	graphics.saturation = 1.15
	graphics.contrast = 1.08
	graphics.fog_density = 0.005
	mut app := egn.create(egn.Settings{
		title:      'EGN - Water Map (Kenney CC0)'
		width:      1600
		height:     900
		vsync:      false
		target_fps: 0
		graphics:   graphics
	}) or {
		eprintln('failed to start water demo: ${err}')
		exit(1)
	}
	defer {
		app.destroy()
	}

	scene := app.scene()

	ground_mesh := load_mesh(&app, 'assets/maps/kenney_nature/ground_grass.glb')
	tree_mesh := load_mesh(&app, 'assets/maps/kenney_nature/tree_default.glb')
	pine_mesh := load_mesh(&app, 'assets/maps/kenney_nature/tree_pineDefaultA.glb')
	rock_mesh := load_mesh(&app, 'assets/maps/kenney_nature/rock_largeA.glb')
	small_rock_mesh := load_mesh(&app, 'assets/maps/kenney_nature/rock_smallA.glb')
	bush_mesh := load_mesh(&app, 'assets/maps/kenney_nature/plant_bush.glb')
	lily_mesh := load_mesh(&app, 'assets/maps/kenney_nature/lily_large.glb')
	campfire_mesh := load_mesh(&app, 'assets/maps/kenney_nature/campfire_logs.glb')

	// 14x14 grass tiles at 6x scale form an 84x84 shore around the lake.
	for zi in -7 .. 7 {
		for xi in -7 .. 7 {
			add_prop(scene, 'Shore ${xi}:${zi}', ground_mesh,
				egn.vec3(f32(xi) * 6.0, 0.30, f32(zi) * 6.0),
				egn.vec3(6.0, 6.0, 6.0))
		}
	}

	// A small grass island in the lake.
	for zi in -1 .. 2 {
		for xi in -1 .. 2 {
			add_prop(scene, 'Island tile', ground_mesh,
				egn.vec3(f32(xi) * 3.0, 0.35, f32(zi) * 3.0),
				egn.vec3(3.0, 3.0, 3.0))
		}
	}
	add_prop(scene, 'Island tree', tree_mesh, egn.vec3(0.0, 0.24, 0.0), egn.vec3(2.6, 3.2, 2.6))
	add_prop(scene, 'Campfire', campfire_mesh, egn.vec3(2.2, 0.27, -1.2), egn.vec3(2.4, 2.4, 2.4))

	water_mesh := create_water_mesh(&app) or {
		eprintln('failed to create water mesh: ${err}')
		egn.Mesh{}
	}
	water := scene.create_object('Lake')
	water.set_position(egn.vec3(0, 0.08, 0))
	water.set_scale(egn.vec3(21, 1, 21))
	mut water_model := water.add_component(egn.Model{
		mesh:  water_mesh
		color: egn.rgba(0.03, 0.22, 0.55, 1.0)
		cast_shadows: false
		water: true
	})
	water_model.set_water(true)

	// Trees and rocks around the shoreline.
	trees := [
		egn.vec3(-32, 0.24, -26), egn.vec3(-26, 0.24, -32),
		egn.vec3(-14, 0.24, -34), egn.vec3(8, 0.24, -34),
		egn.vec3(24, 0.24, -30), egn.vec3(34, 0.24, -18),
		egn.vec3(36, 0.24, 2), egn.vec3(32, 0.24, 22),
		egn.vec3(20, 0.24, 34), egn.vec3(4, 0.24, 36),
		egn.vec3(-14, 0.24, 34), egn.vec3(-30, 0.24, 26),
		egn.vec3(-36, 0.24, 8), egn.vec3(-34, 0.24, -8),
	]
	for i, pos in trees {
		mesh := if i % 3 == 0 { pine_mesh } else { tree_mesh }
		add_prop(scene, 'Tree ${i}', mesh, pos, egn.vec3(4.0, 5.0, 4.0))
	}

	rocks := [
		egn.vec3(-20, 0.24, -22), egn.vec3(-8, 0.24, -26),
		egn.vec3(16, 0.24, -24), egn.vec3(28, 0.24, -10),
		egn.vec3(30, 0.24, 14), egn.vec3(12, 0.24, 28),
		egn.vec3(-8, 0.24, 30), egn.vec3(-24, 0.24, 18),
		egn.vec3(-28, 0.24, -4), egn.vec3(-12, 0.24, -8),
		egn.vec3(10, 0.24, -12), egn.vec3(18, 0.24, 8),
	]
	for i, pos in rocks {
		mesh := if i % 2 == 0 { rock_mesh } else { small_rock_mesh }
		scale := if i % 2 == 0 { egn.vec3(4.0, 4.0, 4.0) } else { egn.vec3(2.5, 2.5, 2.5) }
		add_prop(scene, 'Rock ${i}', mesh, pos, scale)
	}

	for i in 0 .. 18 {
		angle := f32(i) * 0.37
		radius := 5.0 + f32(i % 4) * 2.2
		pos := egn.vec3(f32(math.cos(f64(angle)) * f64(radius)), 0.10,
			f32(math.sin(f64(angle)) * f64(radius)))
		add_prop(scene, 'Lily ${i}', lily_mesh, pos, egn.vec3(1.6, 1.6, 1.6))
	}

	for i in 0 .. 12 {
		angle := f32(i) * 0.53 + 0.4
		radius := 10.0 + f32(i % 3) * 2.5
		pos := egn.vec3(f32(math.cos(f64(angle)) * f64(radius)), 0.10,
			f32(math.sin(f64(angle)) * f64(radius)))
		add_prop(scene, 'Bush ${i}', bush_mesh, pos, egn.vec3(1.8, 1.8, 1.8))
	}


	// A few PBR test objects on the island.
	gold := scene.create_object('Gold sphere')
	gold.set_position(egn.vec3(-2.0, 1.0, 1.5))
	gold.set_scale(egn.vec3(0.7, 0.7, 0.7))
	mut gold_model := gold.add_component(egn.Model{
		mesh:  app.mesh_sphere()
		color: egn.rgb(1.0, 0.78, 0.32)
	})
	gold_model.set_pbr(1.0, 0.18)

	ceramic := scene.create_object('Ceramic sphere')
	ceramic.set_position(egn.vec3(2.0, 0.9, 2.0))
	ceramic.set_scale(egn.vec3(0.6, 0.6, 0.6))
	mut ceramic_model := ceramic.add_component(egn.Model{
		mesh:  app.mesh_sphere()
		color: egn.rgb(0.72, 0.82, 0.92)
	})
	ceramic_model.set_pbr(0.0, 0.42)

	cube := scene.create_object('PBR cube')
	cube.set_position(egn.vec3(-1.0, 0.9, -2.0))
	cube.set_euler_deg(egn.vec3(0, 35, 0))
	cube.set_scale(egn.vec3(0.8, 0.8, 0.8))
	mut cube_model := cube.add_component(egn.Model{
		mesh:  app.mesh_cube()
		color: egn.rgb(0.82, 0.28, 0.16)
	})
	cube_model.set_pbr(0.0, 0.55)

	// Warm lantern light near the pier.
	lamp := scene.create_object('Pier lantern')
	lamp.set_position(egn.vec3(2.2, 1.2, -1.2))
	lamp.add_component(egn.Light{
		light_type: .point
		color:      egn.rgb(1.0, 0.55, 0.18)
		intensity:  2.0
		range:      8.0
	})

	sun := scene.create_object('Sun')
	sun.set_position(egn.vec3(0, 25, 0))
	sun.set_euler_deg(egn.vec3(-52, -32, 0))
	sun.add_component(egn.Light{
		light_type:   .directional
		color:        egn.rgb(1.0, 0.96, 0.86)
		intensity:    1.25
		cast_shadows: true
	})

	camera_object := scene.create_object('Orbit camera')
	camera_object.add_component(egn.Camera{
		fov:         52.0
		near:        0.1
		far:         500.0
		clear_color: egn.rgba(0.02, 0.03, 0.06, 1.0)
	})
	camera_object.set_position(egn.vec3(30, 12, 30))

	warm_lut := app.load_lut('assets/luts/warm.cube') or {
		eprintln('failed to load warm LUT: ${err}')
		egn.Texture{}
	}

	mut preset_index := 3
	presets := [egn.GraphicsPreset.low, .medium, .high, .ultra]
	mut lut_enabled := false
	for app.should_run() {
		elapsed := f32(app.elapsed_time())
		angle := f32(math.sin(f64(elapsed) * 0.12)) * 0.5
		camera_object.set_position(egn.vec3(
			f32(math.sin(f64(angle))) * 24.0,
			9.5 + f32(math.sin(f64(elapsed) * 0.25)) * 1.0,
			f32(math.cos(f64(angle))) * 24.0))
		camera_object.look_at(egn.vec3(0, 1.0, 0))

		if app.key_pressed(.escape) { app.quit() }
		if app.key_pressed(.f3) {
			preset_index = (preset_index + 1) % presets.len
			graphics = egn.graphics_preset(presets[preset_index])
			egn.set_graphics_settings(graphics)
			eprintln('preset changed')
		}
		if app.key_pressed(.f4) {
			mut g := egn.graphics_settings()
			g.hdr_enabled = !g.hdr_enabled
			egn.set_graphics_settings(g)
		}
		if app.key_pressed(.f5) {
			mut g := egn.graphics_settings()
			g.shadows_enabled = !g.shadows_enabled
			egn.set_graphics_settings(g)
		}
		if app.key_pressed(.f6) {
			mut g := egn.graphics_settings()
			g.ao_enabled = !g.ao_enabled
			egn.set_graphics_settings(g)
		}
		if app.key_pressed(.f7) {
			mut g := egn.graphics_settings()
			g.gi_enabled = !g.gi_enabled
			egn.set_graphics_settings(g)
		}
		if app.key_pressed(.f8) {
			mut g := egn.graphics_settings()
			g.atmosphere_enabled = !g.atmosphere_enabled
			egn.set_graphics_settings(g)
		}
		if app.key_pressed(.f9) {
			mut g := egn.graphics_settings()
			g.bloom_enabled = !g.bloom_enabled
			egn.set_graphics_settings(g)
		}
		if app.key_pressed(.f10) {
			mut g := egn.graphics_settings()
			g.auto_exposure_enabled = !g.auto_exposure_enabled
			egn.set_graphics_settings(g)
		}
		if app.key_pressed(.f11) {
			mut g := egn.graphics_settings()
			g.color_grading_enabled = !g.color_grading_enabled
			egn.set_graphics_settings(g)
		}
		if app.key_pressed(.l) && warm_lut.is_valid() {
			lut_enabled = !lut_enabled
			mut g := egn.graphics_settings()
			g.lut_enabled = lut_enabled
			g.lut_texture = warm_lut.id
			egn.set_graphics_settings(g)
			eprintln('warm LUT: ${lut_enabled}')
		}
		if app.key_pressed(.f12) {
			app.screenshot('water_demo.png')
			eprintln('screenshot requested: water_demo.png')
		}

		app.frame()
	}
}
