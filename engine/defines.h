#pragma once

// Unsigned int types.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

// Signed int types.
typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;

// Floating point types
typedef float f32;
typedef double f64;

// Boolean types
typedef _Bool b8;
typedef int b32;

#define true 1
#define false 0

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

#define EGNCLAMP(value, min, max) (value <= min) ? min : (value >= max) ? max \
                                                                        : value;

// Inlining
#ifdef _MSC_VER
#define EGNINLINE __forceinline
#define EGNNOINLINE __declspec(noinline)
#else
#define EGNINLINE static inline
#define EGNNOINLINE
#endif