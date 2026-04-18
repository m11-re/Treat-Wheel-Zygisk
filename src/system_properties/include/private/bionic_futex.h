#ifndef PRIVATE_BIONIC_FUTEX_H
#define PRIVATE_BIONIC_FUTEX_H

#include <linux/futex.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static inline int __futex_wake(volatile void *futex, int count) {
  return (int)syscall(__NR_futex, futex, FUTEX_WAKE, count, NULL, NULL, 0);
}

static inline int __futex_wait(volatile void *futex, uint32_t expected,
const struct timespec *relative_timeout) {
  return (int)syscall(__NR_futex, futex, FUTEX_WAIT, expected, relative_timeout, NULL, 0);
}

#endif /* PRIVATE_BIONIC_FUTEX_H */
