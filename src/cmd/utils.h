#ifndef CMD_UTILS_H
#define CMD_UTILS_H

#include <stdio.h>

void init_prop(void);

void get_prop(const char *prop, char *output);

void set_prop(const char *prop, const char *value);

void delete_prop(const char *prop);

void set_prop_if_diff(const char *prop, const char *value);

void set_prop_if_match(const char *prop, const char *value, const char *needle);

void delete_prop_if_exist(const char *prop);

bool check_prop_equal(const char *prop, const char *value);

bool compact_props(void);

void fix_serials(void);

#endif /* CMD_UTILS_H */
