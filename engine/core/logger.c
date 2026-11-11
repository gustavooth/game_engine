#include "logger.h"
#include "asserts.h"
#include "platform/platform.h"

// TODO: temporary
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define OUTPUT_MESSAGE_LENGTH 1024

typedef struct LoggerState
{

} LoggerState;

static LoggerState *state_ptr;

b8 logger_initialize(u64 *out_memory_requirement, void *state)
{
  if (!state)
  {
    *out_memory_requirement = sizeof(LoggerState);
    return false;
  }

  state_ptr = (LoggerState *)state;

  // TODO: create log file.

  EGNINFO("Core Logger initialized!");
  return true;
}

void logger_shutdown()
{
  // TODO: cleanup logging/write queued entries.

  state_ptr = 0;
}

void log_output(LogLevel level, const char *message, ...)
{
  const char *level_strings[6] = {"[FATAL]: ", "[ERROR]: ", "[WARN]:  ", "[INFO]:  ", "[DEBUG]: ", "[TRACE]: "};
  b8 is_error = level < LOG_LEVEL_WARN;

  char out_message[OUTPUT_MESSAGE_LENGTH];
  out_message[OUTPUT_MESSAGE_LENGTH-1] = '\0';

  va_list arg_ptr;
  va_start(arg_ptr, message);
  vsnprintf(out_message, OUTPUT_MESSAGE_LENGTH, message, arg_ptr);
  va_end(arg_ptr);

  // Room for the level prefix and the newline: the old sprintf() into a buffer
  // of the same size overflowed the stack on messages close to the limit.
  char out_message2[OUTPUT_MESSAGE_LENGTH + 16];
  snprintf(out_message2, sizeof(out_message2), "%s%s\n", level_strings[level], out_message);

  if (is_error)
    platform_console_write_error(out_message2, level);
  else
    platform_console_write(out_message2, level);
}

void report_assertion_failure(const char *expression, const char *message, const char *file, i32 line)
{
  log_output(LOG_LEVEL_FATAL, "Assertion Failure: %s, message: '%s', in file: %s, line: %d\n", expression, message, file, line);
}
