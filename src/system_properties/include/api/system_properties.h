#ifndef API_SYSTEM_PROPERTIES_H
#define API_SYSTEM_PROPERTIES_H

#include <sys/system_properties.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef PROP_NAME_MAX
  #define PROP_NAME_MAX 32
#endif

#ifndef PROP_VALUE_MAX
  #define PROP_VALUE_MAX 92
#endif

#ifndef PROP_FILENAME
  #define PROP_FILENAME "/dev/__properties__"
#endif

#ifndef PROP_FILENAME_MAX
  #define PROP_FILENAME_MAX 1024
#endif

#ifndef PROP_SERVICE_NAME
  #define PROP_SERVICE_NAME "property_service"
#endif

struct prop_info;
struct prop_area;
struct Contexts;

int __system_properties_init(void);

struct Contexts *__system_property_get_contexts(void);

uint32_t __system_property_serial(const struct prop_info *pi);

int __system_property_update(struct prop_info *pi, const char *value, unsigned int len);

int __system_property_add(const char *name, unsigned int namelen, const char *value, unsigned int valuelen);

int __system_property_delete(const char *name, bool prune);

const char *__system_property_get_context(const char *name);

bool __system_property_compact(void);

bool __system_property_compact_context(const char *context);

#endif /* API_SYSTEM_PROPERTIES_H */
