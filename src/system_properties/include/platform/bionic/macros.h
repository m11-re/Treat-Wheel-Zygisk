#ifndef PLATFORM_BIONIC_MACROS_H
#define PLATFORM_BIONIC_MACROS_H

#ifndef __predict_true
  #define __predict_true(exp) __builtin_expect((exp) != 0, 1)
#endif

#ifndef __predict_false
  #define __predict_false(exp) __builtin_expect((exp) != 0, 0)
#endif

#ifndef __BIONIC_ALIGN
  #define __BIONIC_ALIGN(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#endif

#endif /* PLATFORM_BIONIC_MACROS_H */
