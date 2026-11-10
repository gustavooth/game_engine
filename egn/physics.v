module egn

// PhysicsHit identifies a live scene object and, for rays, its distance from the origin.
pub struct PhysicsHit {
pub:
    object_id u32
    distance f32
}

// ray_cast returns all intersected objects, nearest first, after the physics update.
pub fn (e &Engine) ray_cast(origin Vec3, direction Vec3, distance f32) []PhysicsHit {
    count := C.egn_physics_ray_cast(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, distance, unsafe { nil }, 0)
    if count == 0 { return []PhysicsHit{} }
    mut raw := []C.EgnPhysicsHit{len: int(count)}
    actual := C.egn_physics_ray_cast(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, distance, raw.data, count)
    mut hits := []PhysicsHit{cap: int(count)}
    for i in 0 .. int(if actual < count { actual } else { count }) {
        hits << PhysicsHit{object_id: raw[i].object_id, distance: raw[i].distance}
    }
    return hits
}

pub fn (e &Engine) overlap_sphere(center Vec3, radius f32) []PhysicsHit {
    count := C.egn_physics_overlap_sphere(center.x, center.y, center.z, radius, unsafe { nil }, 0)
    if count == 0 { return []PhysicsHit{} }
    mut raw := []C.EgnPhysicsHit{len: int(count)}
    actual := C.egn_physics_overlap_sphere(center.x, center.y, center.z, radius, raw.data, count)
    mut hits := []PhysicsHit{cap: int(count)}
    for i in 0 .. int(if actual < count { actual } else { count }) {
        hits << PhysicsHit{object_id: raw[i].object_id, distance: raw[i].distance}
    }
    return hits
}

pub fn (e &Engine) overlap_box(center Vec3, size Vec3) []PhysicsHit {
    count := C.egn_physics_overlap_box(center.x, center.y, center.z, size.x, size.y, size.z, unsafe { nil }, 0)
    if count == 0 { return []PhysicsHit{} }
    mut raw := []C.EgnPhysicsHit{len: int(count)}
    actual := C.egn_physics_overlap_box(center.x, center.y, center.z, size.x, size.y, size.z, raw.data, count)
    mut hits := []PhysicsHit{cap: int(count)}
    for i in 0 .. int(if actual < count { actual } else { count }) {
        hits << PhysicsHit{object_id: raw[i].object_id, distance: raw[i].distance}
    }
    return hits
}

// camera_ray_cast casts through pixel coordinates measured from the window's top-left.
pub fn (e &Engine) camera_ray_cast(camera Camera, x f32, y f32, distance f32) []PhysicsHit {
    width, height := e.window_size()
    count := C.egn_camera_ray_cast(camera.handle, x, y, f32(width), f32(height), distance, unsafe { nil }, 0)
    if count == 0 { return []PhysicsHit{} }
    mut raw := []C.EgnPhysicsHit{len: int(count)}
    actual := C.egn_camera_ray_cast(camera.handle, x, y, f32(width), f32(height), distance, raw.data, count)
    mut hits := []PhysicsHit{cap: int(count)}
    for i in 0 .. int(if actual < count { actual } else { count }) {
        hits << PhysicsHit{object_id: raw[i].object_id, distance: raw[i].distance}
    }
    return hits
}

// mouse_ray_cast casts through the pointer, or the window center when captured.
pub fn (e &Engine) mouse_ray_cast(camera Camera, distance f32) []PhysicsHit {
    if e.pointer_locked() {
        w, h := e.window_size()
        return e.camera_ray_cast(camera, f32(w) * 0.5, f32(h) * 0.5, distance)
    }
    x, y := e.mouse_position()
    return e.camera_ray_cast(camera, f32(x), f32(y), distance)
}

// XY-plane queries use a thin 0.002-unit volume at plane_z.
pub fn (e &Engine) overlap_circle_2d(center Vec2, radius f32, plane_z f32) []PhysicsHit {
    count := C.egn_physics_overlap_circle_2d(center.x, center.y, plane_z, radius, unsafe { nil }, 0)
    if count == 0 { return []PhysicsHit{} }
    mut raw := []C.EgnPhysicsHit{len: int(count)}
    actual := C.egn_physics_overlap_circle_2d(center.x, center.y, plane_z, radius, raw.data, count)
    mut hits := []PhysicsHit{cap: int(count)}
    for i in 0 .. int(if actual < count { actual } else { count }) {
        hits << PhysicsHit{object_id: raw[i].object_id, distance: raw[i].distance}
    }
    return hits
}
pub fn (e &Engine) overlap_box_2d(center Vec2, size Vec2, plane_z f32) []PhysicsHit {
    count := C.egn_physics_overlap_box_2d(center.x, center.y, plane_z, size.x, size.y, unsafe { nil }, 0)
    if count == 0 { return []PhysicsHit{} }
    mut raw := []C.EgnPhysicsHit{len: int(count)}
    actual := C.egn_physics_overlap_box_2d(center.x, center.y, plane_z, size.x, size.y, raw.data, count)
    mut hits := []PhysicsHit{cap: int(count)}
    for i in 0 .. int(if actual < count { actual } else { count }) {
        hits << PhysicsHit{object_id: raw[i].object_id, distance: raw[i].distance}
    }
    return hits
}
