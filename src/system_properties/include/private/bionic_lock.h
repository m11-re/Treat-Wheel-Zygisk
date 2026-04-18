#ifndef PRIVATE_BIONIC_LOCK_H
#define PRIVATE_BIONIC_LOCK_H

#include <pthread.h>
#include <stdbool.h>

// Minimal stand-in for bionic's internal Lock used by the property implementation.
typedef struct Lock {
  pthread_mutex_t mutex;
} Lock;

static inline void Lock_init(Lock *lock, bool shared) {
  (void)shared;  // Shared vs non-shared is not distinguished in this minimal implementation.
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&lock->mutex, &attr);
  pthread_mutexattr_destroy(&attr);
}

static inline void Lock_lock(Lock *lock) {
  pthread_mutex_lock(&lock->mutex);
}

static inline void Lock_unlock(Lock *lock) {
  pthread_mutex_unlock(&lock->mutex);
}

static inline void Lock_destroy(Lock *lock) {
  pthread_mutex_destroy(&lock->mutex);
}

#endif /* PRIVATE_BIONIC_LOCK_H */
