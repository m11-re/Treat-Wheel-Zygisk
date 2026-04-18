/* system_properties.h - Android property system (all API levels) */
#ifndef SYSTEM_PROPERTIES_H
#define SYSTEM_PROPERTIES_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <time.h>
#include <api/system_properties.h>

#define PROP_TREE_FILE "/dev/__properties__/property_info"

#ifndef __BIONIC_ALIGN
#define __BIONIC_ALIGN(v, a) (((v) + (a) - 1) & ~((a) - 1))
#endif
#ifndef __predict_true
#define __predict_true(x) __builtin_expect(!!(x), 1)
#endif
#ifndef __predict_false
#define __predict_false(x) __builtin_expect(!!(x), 0)
#endif

/* ── prop_bt: trie node ──────────────────────────────────────── */
typedef struct prop_bt {
  uint32_t namelen;
  atomic_uint_least32_t prop;
  atomic_uint_least32_t left;
  atomic_uint_least32_t right;
  atomic_uint_least32_t children;
  char name[];
} prop_bt;

typedef struct prop_info {
  enum { k_long_flag = 1u << 16 };
  atomic_uint_least32_t serial;
  union {
    char value[PROP_VALUE_MAX];
    struct { char error_message[56]; uint32_t offset; } long_property;
  } value_un;
  char name[];
} prop_info;

typedef struct prop_area {
  uint32_t bytes_used;
  atomic_uint_least32_t serial;
  uint32_t magic;
  uint32_t version;
  uint32_t reserved[28];
  char data[];
} prop_area;

/* ── Property info parser types ──────────────────────────────── */
typedef struct { uint32_t name_offset, namelen, context_index, type_index; } property_entry;
typedef struct {
  uint32_t property_entry, num_child_nodes, child_nodes;
  uint32_t num_prefixes, prefix_entries;
  uint32_t num_exact_matches, exact_match_entries;
} trie_node_internal;
typedef struct {
  uint32_t current_version, minimum_supported_version, size;
  uint32_t contexts_offset, types_offset, root_offset;
} property_info_area_header;
typedef struct { const char *data_base; } property_info_area;
typedef struct { void *mmap_base; size_t mmap_size; } property_info_area_file;
typedef struct { const property_info_area *pia; const trie_node_internal *node; } trie_node;

/* ── Context node ────────────────────────────────────────────── */
typedef struct context_node {
  pthread_mutex_t mutex;
  const char *context;
  const char *filename;
  prop_area *pa;
  bool no_access;
} context_node;

/* ── Contexts vtable ─────────────────────────────────────────── */
struct contexts;
typedef struct contexts_ops {
  bool (*initialize)(struct contexts *self, bool writable, const char *filename, bool *fsetxattr_failed);
  prop_area *(*get_prop_area_for_name)(struct contexts *self, const char *name);
  prop_area *(*get_serial_prop_area)(struct contexts *self);
  const char *(*get_context_for_name)(struct contexts *self, const char *name);
  void (*for_each)(struct contexts *self, void (*fn)(const prop_info *, void *), void *cookie);
  bool (*compact)(struct contexts *self);
  bool (*compact_context)(struct contexts *self, const char *context, bool *found);
  void (*reset_access)(struct contexts *self);
  void (*free_and_unmap)(struct contexts *self);
  void (*for_each_context)(struct contexts *self, void (*fn)(prop_area *, const char *, void *), void *cookie);
} contexts_ops;

typedef struct contexts {
  const contexts_ops *ops;
  bool rw;
} contexts;
/* Alias for old API compat */
typedef contexts Contexts;

/* ── Contexts implementations ────────────────────────────────── */
typedef struct {
  contexts base;
  const char *filename;
  property_info_area_file piaf;
  context_node *nodes;
  size_t num_nodes;
  size_t nodes_mmap_size;
  prop_area *serial_pa;
} contexts_serialized;

typedef struct contexts_split contexts_split;
typedef struct prefix_node prefix_node;
typedef struct context_list_node context_list_node;

struct prefix_node {
  char *prefix;
  size_t prefix_len;
  context_list_node *ctx;
  prefix_node *next;
};

struct context_list_node {
  context_node node;
  context_list_node *next;
};

struct contexts_split {
  contexts base;
  prefix_node *prefixes;
  context_list_node *contexts;
  prop_area *serial_pa;
  const char *filename;
};

typedef struct {
  contexts base;
  prop_area *pa;
} contexts_pre_split;

/* ── System properties ───────────────────────────────────────── */
typedef union {
  contexts_serialized serialized;
  contexts_split split;
  contexts_pre_split pre_split;
} contexts_storage;

typedef struct system_properties {
  contexts_storage contexts_data;
  contexts *ctx;
  bool initialized;
  char property_filename[PROP_FILENAME_MAX];
} system_properties;

/* ── Inline dispatch ─────────────────────────────────────────── */
static inline bool contexts_initialize(contexts *s, bool w, const char *f, bool *x) { return s->ops->initialize(s, w, f, x); }
static inline prop_area *contexts_get_prop_area_for_name(contexts *s, const char *n) { return s->ops->get_prop_area_for_name(s, n); }
static inline prop_area *contexts_get_serial_prop_area(contexts *s) { return s->ops->get_serial_prop_area(s); }
static inline const char *contexts_get_context_for_name(contexts *s, const char *n) { return s->ops->get_context_for_name(s, n); }
static inline void contexts_for_each(contexts *s, void (*f)(const prop_info *, void *), void *c) { s->ops->for_each(s, f, c); }
static inline bool contexts_compact(contexts *s) { return s->ops->compact(s); }
static inline bool contexts_compact_context(contexts *s, const char *c, bool *f) { return s->ops->compact_context(s, c, f); }
static inline void contexts_reset_access(contexts *s) { s->ops->reset_access(s); }
static inline void contexts_free_and_unmap(contexts *s) { s->ops->free_and_unmap(s); }
static inline void contexts_for_each_context(contexts *s, void (*f)(prop_area *, const char *, void *), void *c) { s->ops->for_each_context(s, f, c); }

/* ── Prop info helpers ───────────────────────────────────────── */
static inline bool prop_info_is_long(const prop_info *pi) {
  return (atomic_load_explicit((atomic_uint_least32_t *)&pi->serial, memory_order_relaxed) & k_long_flag) != 0;
}
static inline const char *prop_info_long_value(const prop_info *pi) {
  return (const char *)pi + pi->value_un.long_property.offset;
}
static inline atomic_uint_least32_t *prop_area_get_serial(prop_area *pa) { return &pa->serial; }
static inline char *prop_area_dirty_backup_area(prop_area *pa) { return pa->data + sizeof(prop_bt); }

/* ── Context init ────────────────────────────────────────────── */
void contexts_serialized_init(contexts_serialized *self);
void contexts_split_init(contexts_split *self);
void contexts_pre_split_init(contexts_pre_split *self);

/* ── Prop area API ───────────────────────────────────────────── */
void prop_info_init(prop_info *pi, const char *name, uint32_t namelen, const char *value, uint32_t valuelen);
void prop_info_init_long(prop_info *pi, const char *name, uint32_t namelen, uint32_t offset);

prop_area *prop_area_map_prop_area_rw(const char *filename, const char *context, bool *fsetxattr_failed);
prop_area *prop_area_map_prop_area(const char *filename, bool *is_rw);
void prop_area_unmap_prop_area(prop_area **pa);
const prop_info *prop_area_find(prop_area *pa, const char *name);
bool prop_area_add(prop_area *pa, const char *name, unsigned int namelen, const char *value, unsigned int valuelen);
bool prop_area_remove(prop_area *pa, const char *name, bool prune);
bool prop_area_compact(prop_area *pa);
bool prop_area_foreach(prop_area *pa, void (*fn)(const prop_info *, void *), void *cookie);

/* ── Context node API ────────────────────────────────────────── */
void context_node_init(context_node *n, const char *ctx, const char *filename);
bool context_node_open(context_node *n, bool rw, bool *fsetxattr_failed);
bool context_node_check_access_and_open(context_node *n);
void context_node_reset_access(context_node *n);
void context_node_unmap(context_node *n);

/* ── Property info parser API ────────────────────────────────── */
void property_info_area_file_init(property_info_area_file *self);
bool property_info_area_file_load_default_path(property_info_area_file *self);
void property_info_area_file_reset(property_info_area_file *self);
const property_info_area *property_info_area_file_area(const property_info_area_file *self);
uint32_t property_info_area_file_num_contexts(const property_info_area_file *self);
const char *property_info_area_file_context(const property_info_area_file *self, size_t index);
void property_info_area_file_get_property_info_indexes(const property_info_area_file *self, const char *name, uint32_t *ci, uint32_t *ti);

#endif /* SYSTEM_PROPERTIES_H */
