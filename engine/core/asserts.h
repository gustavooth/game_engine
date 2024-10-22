#pragma once

#include "defines.h"

// Disable assertions by commenting out the below line.
#define EGNASSERTIONS_ENABLED

#ifdef EGNASSERTIONS_ENABLED
#if _MSC_VER
#include <intrin.h>
#define debugBreak() __debugbreak()
#else
#define debugBreak() __builtin_trap()
#endif

EGNAPI void report_assertion_failure(const char *expression, const char *message, const char *file, i32 line);

#define EGNASSERT(expr)                                        \
  {                                                            \
    if (expr)                                                  \
    {                                                          \
    }                                                          \
    else                                                       \
    {                                                          \
      report_assertion_failure(#expr, "", __FILE__, __LINE__); \
      debugBreak();                                            \
    }                                                          \
  }

#define EGNASSERT_MSG(expr, message)                                \
  {                                                                 \
    if (expr)                                                       \
    {                                                               \
    }                                                               \
    else                                                            \
    {                                                               \
      report_assertion_failure(#expr, message, __FILE__, __LINE__); \
      debugBreak();                                                 \
    }                                                               \
  }

#ifdef _DEBUG
#define EGNASSERT_DEBUG(expr)                                  \
  {                                                            \
    if (expr)                                                  \
    {                                                          \
    }                                                          \
    else                                                       \
    {                                                          \
      report_assertion_failure(#expr, "", __FILE__, __LINE__); \
      debugBreak();                                            \
    }                                                          \
  }
#else
#define EGNASSERT_DEBUG(expr) // Does nothing at all
#endif

#else
#define EGNASSERT(expr)              // Does nothing at all
#define EGNASSERT_MSG(expr, message) // Does nothing at all
#define EGNASSERT_DEBUG(expr)        // Does nothing at all
#endif
