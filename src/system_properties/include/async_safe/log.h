#ifndef ASYNC_SAFE_LOG_H
#define ASYNC_SAFE_LOG_H

#include <android/log.h>
#include <stdarg.h>
#include <stdio.h>

// Lightweight stand-ins for bionic async_safe logging helpers.
static inline int async_safe_format_buffer(char *buffer, size_t buffer_size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int written = vsnprintf(buffer, buffer_size, fmt, ap);
  va_end(ap);
  return written;
}

static inline void async_safe_format_log(int pri, const char *tag, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  __android_log_vprint(pri, tag, fmt, ap);
  va_end(ap);
}

#endif /* ASYNC_SAFE_LOG_H */
