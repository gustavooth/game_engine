#pragma once

// Fixed-width types come from the platform's <stdint.h> so they stay
// compatible with other compilers/languages (including the V SDK) that also
// use the C standard types.
#include <stdint.h>
#include <stdbool.h>

// Unsigned int types.
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Signed int types.
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

// Floating point types
typedef float f32;
typedef double f64;

// Boolean types
typedef _Bool b8;
typedef int32_t b32;

// Properly define static assertions.
#if defined(__clang__) || defined(__gcc__)
#define STATIC_ASSERT _Static_assert
#else
#define STATIC_ASSERT static_assert
#endif

// Platform detection
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#define EGNPLATFORM_WINDOWS 1
#ifndef _WIN64
#error "64-bit is required on Windows!"
#endif
#elif defined(__EMSCRIPTEN__)
#define EGNPLATFORM_WEB 1
#elif defined(__linux__) || defined(__gnu_linux__)
#define EGNPLATFORM_LINUX 1
#define EGNPLATFORM_WAYLAND
#else
#error "Unknown platform!"
#endif

#ifdef EGNEXPORT
// Exports
#ifdef _MSC_VER
#define EGNAPI __declspec(dllexport)
#else
#define EGNAPI __attribute__((visibility("default")))
#endif
#else
// Imports
#ifdef _MSC_VER
#define EGNAPI __declspec(dllimport)
#else
#define EGNAPI
#endif
#endif

#define EGNCLAMP(value, min, max) (((value) <= (min)) ? (min) : ((value) >= (max)) ? (max) : (value))
#define EGNMIN(a, b) (((a) < (b)) ? (a) : (b))
#define EGNMAX(a, b) (((a) > (b)) ? (a) : (b))

// Inlining
#ifdef _MSC_VER
#define EGNINLINE __forceinline
#define EGNNOINLINE __declspec(noinline)
#else
#define EGNINLINE static inline
#define EGNNOINLINE
#endif
