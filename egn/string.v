module egn

// EgnString contains valid UTF-8 without embedded NUL bytes.
pub struct EgnString {
    value string
}

pub fn new_egn_string(value string) !EgnString {
    if !C.egn_string_utf8_valid(value.str, u64(value.len)) { return error('invalid UTF-8 or embedded NUL') }
    return EgnString{value: value.clone()}
}

pub fn (s EgnString) str() string { return s.value }
