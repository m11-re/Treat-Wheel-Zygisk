#include "system_properties.h"

#include <string.h>

void init_prop(void) {
  __system_properties_init();
}

void get_prop(const char *prop, char *output) {
  __system_property_get(prop, output);
}

void set_prop(const char *prop, const char *value) {
  struct prop_info *ps = (struct prop_info *)__system_property_find(prop);
  if (!ps) {
    __system_property_add(prop, strlen(prop), value, strlen(value));
    ps = (struct prop_info *)__system_property_find(prop);
  }

  __system_property_update(ps, value, (unsigned int)strlen(value));
}

void delete_prop(const char *prop) {
  __system_property_delete(prop, false);
}

void set_prop_if_diff(const char *prop, const char *value) {
  char current_value[PROP_VALUE_MAX];
  get_prop(prop, current_value);

  /* TODO: If it doesn't exist, we should NOT set it */
  if (current_value[0] == '\0') return;

  if (strcmp(current_value, value) != 0)
    set_prop(prop, value);
}

void set_prop_if_match(const char *prop, const char *value, const char *needle) {
  char current_value[PROP_VALUE_MAX];
  get_prop(prop, current_value);

  /* TODO: If it doesn't exist, we should NOT set it */
  if (current_value[0] == '\0') return;

  if (strcmp(current_value, needle) == 0)
    set_prop(prop, value);
}

void delete_prop_if_exist(const char *prop) {
  char current_value[PROP_VALUE_MAX];
  get_prop(prop, current_value);

  if (current_value[0] == '\0') return;

  delete_prop(prop);
}

bool check_prop_equal(const char *prop, const char *value) {
  char current_value[PROP_VALUE_MAX];
  get_prop(prop, current_value);

  return strcmp(current_value, value) == 0;
}

bool compact_props(void) {
  return __system_property_compact();
}

typedef struct {
  char value[PROP_VALUE_MAX];
  bool done;
} fix_serial_state;

static void fix_serial_read_cb(void *cookie, const char *name, const char *value, uint32_t serial) {
  (void)name;
  (void)serial;

  fix_serial_state *state = cookie;
  strlcpy(state->value, value, sizeof(state->value));
  state->done = true;
}

static void fix_serial_callback(const prop_info *pi, void *cookie) {
  (void)cookie;

  /* INFO: Only fix short ro. properties. */
  if (strncmp(pi->name, "ro.", 3) != 0) return;
  if (prop_info_is_long(pi)) return;

  fix_serial_state state = { 0 };
  __system_property_read_callback(pi, fix_serial_read_cb, &state);
  if (!state.done) return;

  __system_property_update((prop_info *)pi, state.value, (unsigned int)strlen(state.value));
}

void fix_serials(void) {
  __system_property_foreach(fix_serial_callback, NULL);
}
