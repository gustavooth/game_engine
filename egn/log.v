module egn

// Logging helpers.
//
// Messages go through the engine logger, so engine and game output share the
// same format, level filtering and output stream.

pub enum LogLevel {
	fatal = 0
	error = 1
	warn  = 2
	info  = 3
	debug = 4
	trace = 5
}

// log writes a message using the engine logger.
pub fn log(level LogLevel, message string) {
	unsafe {
		C.log_output(int(level), c'%s', message.str)
	}
}

// log_fatal logs a fatal message.
pub fn log_fatal(message string) {
	log(.fatal, message)
}

// log_error logs an error message.
pub fn log_error(message string) {
	log(.error, message)
}

// log_warn logs a warning message.
pub fn log_warn(message string) {
	log(.warn, message)
}

// log_info logs an informational message.
pub fn log_info(message string) {
	log(.info, message)
}

// log_debug logs a debug message (compiled out in release builds of the engine).
pub fn log_debug(message string) {
	log(.debug, message)
}

// log_trace logs a trace message (compiled out in release builds of the engine).
pub fn log_trace(message string) {
	log(.trace, message)
}
