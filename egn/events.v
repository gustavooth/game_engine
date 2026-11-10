module egn

// Engine event system.
//
// EventContext is a 16-byte C union (see engine/core/events.h). V cannot
// express that union in a way that is safe across all V versions, so the SDK
// exposes it as raw bytes plus typed accessors.

pub enum EventCode {
	application_quit = 0x01
	key_pressed      = 0x02
	key_released     = 0x03
	button_pressed   = 0x04
	button_released  = 0x05
	mouse_moved      = 0x06
	mouse_wheel      = 0x07
	resized          = 0x08

	// Game code must use codes above 255.
	first_user_code = 0x100
}

pub struct EventContext {
pub mut:
	data [16]u8
}

// EventCallback mirrors `PFN_on_event` from engine/core/events.h.
// The function must be a plain (non-closure) function so it has a C-compatible
// address.
pub type EventCallback = fn (code u16, sender voidptr, data EventContext) bool

// i64_at reads the i64 stored at the given union slot (0 or 1).
@[inline]
pub fn (ctx EventContext) i64_at(slot int) i64 {
	mut value := i64(0)
	unsafe {
		bytes := &u8(&value)
		for i in 0 .. 8 {
			bytes[i] = ctx.data[slot * 8 + i]
		}
	}
	return value
}

// u64_at reads the u64 stored at the given union slot (0 or 1).
@[inline]
pub fn (ctx EventContext) u64_at(slot int) u64 {
	return u64(ctx.i64_at(slot))
}

// f64_at reads the f64 stored at the given union slot (0 or 1).
@[inline]
pub fn (ctx EventContext) f64_at(slot int) f64 {
	mut value := f64(0)
	unsafe {
		bytes := &u8(&value)
		for i in 0 .. 8 {
			bytes[i] = ctx.data[slot * 8 + i]
		}
	}
	return value
}

// u16_at reads the u16 stored at the given union slot (0..7).
@[inline]
pub fn (ctx EventContext) u16_at(slot int) u16 {
	mut value := u16(0)
	unsafe {
		bytes := &u8(&value)
		for i in 0 .. 2 {
			bytes[i] = ctx.data[slot * 2 + i]
		}
	}
	return value
}

// i32_at reads the i32 stored at the given union slot (0..3).
@[inline]
pub fn (ctx EventContext) i32_at(slot int) i32 {
	mut value := i32(0)
	unsafe {
		bytes := &u8(&value)
		for i in 0 .. 4 {
			bytes[i] = ctx.data[slot * 4 + i]
		}
	}
	return value
}

// u8_at reads the u8 stored at the given union slot (0..15).
@[inline]
pub fn (ctx EventContext) u8_at(slot int) u8 {
	return ctx.data[slot]
}
