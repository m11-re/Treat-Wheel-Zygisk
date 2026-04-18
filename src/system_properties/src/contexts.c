/* TODO: Format with proper coding style */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <async_safe/log.h>

#include "system_properties.h"

/* INFO: Context node mutex helpers */

static void ctx_mutex_init(context_node *n) {
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&n->mutex, &a);
  pthread_mutexattr_destroy(&a);
}

static bool context_node_check_access(context_node *n) {
  char fn[PROP_FILENAME_MAX];
  int len = async_safe_format_buffer(fn, sizeof(fn), "%s/%s", n->filename, n->context);
  return len > 0 && len < PROP_FILENAME_MAX && access(fn, R_OK) == 0;
}

void context_node_init(context_node *n, const char *ctx, const char *filename) {
  ctx_mutex_init(n);
  n->context = ctx;
  n->filename = filename;
  n->pa = NULL;
  n->no_access = false;
}

bool context_node_open(context_node *n, bool rw, bool *fsetxattr_failed) {
  pthread_mutex_lock(&n->mutex);

  if (n->pa) {
    pthread_mutex_unlock(&n->mutex);
    return true;
  }

  char fn[PROP_FILENAME_MAX];
  int len = async_safe_format_buffer(fn, sizeof(fn), "%s/%s", n->filename, n->context);
  if (len > 0 && len < PROP_FILENAME_MAX) {
    n->pa = rw ? prop_area_map_prop_area_rw(fn, n->context, fsetxattr_failed) : prop_area_map_prop_area(fn, NULL);
  }

  pthread_mutex_unlock(&n->mutex);
  return n->pa != NULL;
}

bool context_node_check_access_and_open(context_node *n) {
  if (!n->pa && !n->no_access) {
    if (!context_node_check_access(n) || !context_node_open(n, false, NULL)) {
      n->no_access = true;
    }
  }
  return n->pa != NULL;
}

void context_node_reset_access(context_node *n) {
  if (!context_node_check_access(n)) {
    context_node_unmap(n);
    n->no_access = true;
  } else {
    n->no_access = false;
  }
}

void context_node_unmap(context_node *n) {
  prop_area_unmap_prop_area(&n->pa);
}

/* INFO: Property info parser */

struct trie_cmp {
  const trie_node *node;
  const char *name;
  uint32_t len;
};

static int bin_find(uint32_t cnt, int (*cmp)(uint32_t, void *), void *cookie) {
  int lo = 0;
  int hi = (int)cnt - 1;

  while (hi >= lo) {
    int m = (hi + lo) / 2;
    int r = cmp((uint32_t)m, cookie);
    if (!r) return m;
    if (r < 0) lo = m + 1;
    else hi = m - 1;
  }

  return -1;
}

static const property_info_area_header *pia_hdr(const property_info_area *p) {
  return (const property_info_area_header *)p->data_base;
}

static const char *pia_str(const property_info_area *p, uint32_t off) {
  return off && off > pia_hdr(p)->size ? NULL : p->data_base + off;
}

static const uint32_t *pia_u32(const property_info_area *p, uint32_t off) {
  return off && off > pia_hdr(p)->size ? NULL : (const uint32_t *)(p->data_base + off);
}

static uint32_t pia_num_contexts(const property_info_area *p) {
  return pia_u32(p, pia_hdr(p)->contexts_offset)[0];
}

static const char *pia_context(const property_info_area *p, uint32_t i) {
  return p->data_base + pia_u32(p, pia_hdr(p)->contexts_offset + sizeof(uint32_t))[i];
}

static trie_node pia_trie(const property_info_area *p, uint32_t off) {
  trie_node ret;
  ret.pia = p;
  ret.node = (off && off <= pia_hdr(p)->size)
    ? (const trie_node_internal *)(p->data_base + off)
    : NULL;
  return ret;
}

static const property_entry *trie_ent(const trie_node *n) {
  return (const property_entry *)(n->pia->data_base + n->node->property_entry);
}

static trie_node trie_child(const trie_node *n, int i) {
  return pia_trie(n->pia, pia_u32(n->pia, n->node->child_nodes)[i]);
}

static const property_entry *trie_prefix(const trie_node *n, int i) {
  return (const property_entry *)(n->pia->data_base + pia_u32(n->pia, n->node->prefix_entries)[i]);
}

static const property_entry *trie_exact(const trie_node *n, int i) {
  return (const property_entry *)(n->pia->data_base + pia_u32(n->pia, n->node->exact_match_entries)[i]);
}

static int trie_cmp_fn(uint32_t off, void *cookie) {
  struct trie_cmp *s = cookie;
  trie_node ch = trie_child(s->node, (int)off);

  int c = strncmp(pia_str(ch.pia, trie_ent(&ch)->name_offset), s->name, s->len);
  if (c == 0 && trie_ent(&ch)->namelen > s->len) return 1;
  return c;
}

static void check_prefix(const char *rest, const trie_node *n, uint32_t *ci, uint32_t *ti) {
  for (uint32_t i = 0; i < n->node->num_prefixes; ++i) {
    const property_entry *e = trie_prefix(n, (int)i);
    if (e->namelen <= strlen(rest) && !strncmp(pia_str(n->pia, e->name_offset), rest, e->namelen)) {
      if (e->context_index != ~0u) *ci = e->context_index;
      if (e->type_index != ~0u) *ti = e->type_index;
      return;
    }
  }
}

const property_info_area *property_info_area_file_area(const property_info_area_file *self) {
  static property_info_area pia;
  pia.data_base = (const char *)self->mmap_base;
  return &pia;
}

uint32_t property_info_area_file_num_contexts(const property_info_area_file *self) {
  return pia_num_contexts(property_info_area_file_area(self));
}

const char *property_info_area_file_context(const property_info_area_file *self, size_t i) {
  return pia_context(property_info_area_file_area(self), (uint32_t)i);
}

void property_info_area_file_get_property_info_indexes(const property_info_area_file *self, const char *name, uint32_t *ci, uint32_t *ti) {
  uint32_t rci = ~0u;
  uint32_t rti = ~0u;

  const property_info_area *pia = property_info_area_file_area(self);
  trie_node node = pia_trie(pia, pia_hdr(pia)->root_offset);
  const char *rest = name;

  while (1) {
    const char *sep = strchr(rest, '.');
    if (node.node->property_entry && trie_ent(&node)->context_index != ~0u) rci = trie_ent(&node)->context_index;
    if (node.node->property_entry && trie_ent(&node)->type_index != ~0u) rti = trie_ent(&node)->type_index;

    check_prefix(rest, &node, &rci, &rti);
    if (!sep) break;

    uint32_t slen = (uint32_t)(sep - rest);
    struct trie_cmp s = { &node, rest, slen };
    int idx = bin_find(node.node->num_child_nodes, trie_cmp_fn, &s);
    if (idx == -1) break;

    node = trie_child(&node, idx);
    rest = sep + 1;
  }

  for (uint32_t i = 0; i < node.node->num_exact_matches; ++i) {
    const property_entry *e = trie_exact(&node, (int)i);
    if (!strcmp(pia_str(pia, e->name_offset), rest)) {
      if (ci) *ci = e->context_index != ~0u ? e->context_index : rci;
      if (ti) *ti = e->type_index != ~0u ? e->type_index : rti;
      return;
    }
  }

  check_prefix(rest, &node, &rci, &rti);
  if (ci) *ci = rci;
  if (ti) *ti = rti;
}

void property_info_area_file_init(property_info_area_file *self) {
  self->mmap_base = NULL;
  self->mmap_size = 0;
}

static bool piaf_load_path(property_info_area_file *self, const char *filename) {
  int fd = open(filename, O_CLOEXEC | O_NOFOLLOW | O_RDONLY);
  if (fd < 0) return false;

  struct stat st;
  if (fstat(fd, &st) < 0) {
    close(fd);
    return false;
  }

  if (st.st_uid || st.st_gid || (st.st_mode & (S_IWGRP | S_IWOTH)) || st.st_size < (off_t)sizeof(property_info_area_header)) {
    close(fd);
    return false;
  }

  self->mmap_size = (size_t)st.st_size;
  self->mmap_base = mmap(NULL, self->mmap_size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if (self->mmap_base == MAP_FAILED) {
    self->mmap_base = NULL;
    return false;
  }

  const property_info_area_header *h = pia_hdr(property_info_area_file_area(self));
  if (h->minimum_supported_version > 1 || h->size != self->mmap_size) {
    property_info_area_file_reset(self);
    return false;
  }

  return true;
}

bool property_info_area_file_load_default_path(property_info_area_file *self) {
  return piaf_load_path(self, PROP_TREE_FILE);
}

void property_info_area_file_reset(property_info_area_file *self) {
  if (self->mmap_size && self->mmap_base) munmap(self->mmap_base, self->mmap_size);
  self->mmap_base = NULL;
  self->mmap_size = 0;
}

/* INFO: Shared helper */

static bool map_serial(prop_area **out, const char *filename, bool rw, bool *rw_flag, bool *fsetxattr_failed) {
  char path[PROP_FILENAME_MAX];
  int len = async_safe_format_buffer(path, sizeof(path), "%s/properties_serial", filename);
  if (len <= 0 || len >= PROP_FILENAME_MAX) {
    *out = NULL;
    return false;
  }

  *out = rw
    ? prop_area_map_prop_area_rw(path, "u:object_r:properties_serial:s0", fsetxattr_failed)
    : prop_area_map_prop_area(path, rw_flag);
  return *out != NULL;
}

/* INFO: Serialized contexts (modern Android) */

static contexts_serialized *as_ser(contexts *b) { return (contexts_serialized *)b; }

static bool ser_init_nodes(contexts_serialized *s) {
  size_t n = property_info_area_file_num_contexts(&s->piaf);

  void *p = mmap(NULL, sizeof(context_node) * n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) return false;

  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, p, sizeof(context_node) * n, "System property context nodes");

  s->nodes = p;
  s->num_nodes = n;
  s->nodes_mmap_size = sizeof(context_node) * n;

  for (size_t i = 0; i < n; ++i) {
    context_node_init(&s->nodes[i], property_info_area_file_context(&s->piaf, i), s->filename);
  }

  return true;
}

static bool ser_initialize(contexts *b, bool writable, const char *filename, bool *ff) {
  contexts_serialized *s = as_ser(b);
  s->filename = filename;

  property_info_area_file_init(&s->piaf);
  if (!property_info_area_file_load_default_path(&s->piaf) || !ser_init_nodes(s)) {
    contexts_free_and_unmap(b);
    return false;
  }

  if (writable) {
    mkdir(s->filename, S_IRWXU | S_IXGRP | S_IXOTH);
    if (ff) *ff = false;

    for (size_t i = 0; i < s->num_nodes; ++i) {
      if (!context_node_open(&s->nodes[i], true, ff)) {
        contexts_free_and_unmap(b);
        return false;
      }
    }

    if (!map_serial(&s->serial_pa, s->filename, true, NULL, ff)) {
      contexts_free_and_unmap(b);
      return false;
    }
  } else {
    if (!map_serial(&s->serial_pa, s->filename, false, &b->rw, NULL)) {
      contexts_free_and_unmap(b);
      return false;
    }
  }

  return true;
}

static prop_area *ser_get_area(contexts *b, const char *name) {
  contexts_serialized *s = as_ser(b);
  uint32_t idx;

  property_info_area_file_get_property_info_indexes(&s->piaf, name, &idx, NULL);
  if (idx == ~0u || idx >= s->num_nodes) {
    async_safe_format_log(ANDROID_LOG_ERROR, "libc", "No context for \"%s\"", name);
    return NULL;
  }

  context_node *n = &s->nodes[idx];
  if (!n->pa) context_node_open(n, false, NULL);
  return n->pa;
}

static prop_area *ser_get_serial(contexts *b) { return as_ser(b)->serial_pa; }

static const char *ser_get_context(contexts *b, const char *name) {
  uint32_t ci;
  property_info_area_file_get_property_info_indexes(&as_ser(b)->piaf, name, &ci, NULL);
  return ci == ~0u ? NULL : pia_context(property_info_area_file_area(&as_ser(b)->piaf), ci);
}

static void ser_for_each(contexts *b, void (*fn)(const prop_info *, void *), void *c) {
  contexts_serialized *s = as_ser(b);
  for (size_t i = 0; i < s->num_nodes; ++i) {
    if (context_node_check_access_and_open(&s->nodes[i])) {
      prop_area_foreach(s->nodes[i].pa, fn, c);
    }
  }
}

static void ser_for_each_ctx(contexts *b, void (*fn)(prop_area *, const char *, void *), void *c) {
  contexts_serialized *s = as_ser(b);
  for (size_t i = 0; i < s->num_nodes; ++i) {
    if (context_node_check_access_and_open(&s->nodes[i])) {
      fn(s->nodes[i].pa, s->nodes[i].context, c);
    }
  }
}

static bool ser_compact(contexts *b) {
  contexts_serialized *s = as_ser(b);
  for (size_t i = 0; i < s->num_nodes; ++i) {
    if (!context_node_check_access_and_open(&s->nodes[i]) || !prop_area_compact(s->nodes[i].pa)) {
      return false;
    }
  }
  return !s->serial_pa || prop_area_compact(s->serial_pa);
}

static bool ser_compact_ctx(contexts *b, const char *ctx, bool *found) {
  contexts_serialized *s = as_ser(b);
  *found = false;

  for (size_t i = 0; i < s->num_nodes; ++i) {
    if (!strcmp(s->nodes[i].context, ctx)) {
      *found = true;
      if (!context_node_check_access_and_open(&s->nodes[i]) || !prop_area_compact(s->nodes[i].pa)) {
        return false;
      }
    }
  }

  if (!*found && s->serial_pa && !strcmp(ctx, "u:object_r:properties_serial:s0")) {
    *found = true;
    return prop_area_compact(s->serial_pa);
  }

  return *found;
}

static void ser_reset(contexts *b) {
  contexts_serialized *s = as_ser(b);
  for (size_t i = 0; i < s->num_nodes; ++i) {
    context_node_reset_access(&s->nodes[i]);
  }
}

static void ser_free(contexts *b) {
  contexts_serialized *s = as_ser(b);
  property_info_area_file_reset(&s->piaf);

  if (s->nodes) {
    for (size_t i = 0; i < s->num_nodes; ++i) {
      context_node_unmap(&s->nodes[i]);
    }
    munmap(s->nodes, s->nodes_mmap_size);
    s->nodes = NULL;
  }

  prop_area_unmap_prop_area(&s->serial_pa);
}

static const contexts_ops ser_ops = {
  ser_initialize, ser_get_area, ser_get_serial, ser_get_context,
  ser_for_each, ser_compact, ser_compact_ctx, ser_reset, ser_free, ser_for_each_ctx,
};

void contexts_serialized_init(contexts_serialized *s) {
  memset(s, 0, sizeof(*s));
  s->base.ops = &ser_ops;
}

/* INFO: Split contexts (mid-era Android) */

static contexts_split *as_split(contexts *b) { return (contexts_split *)b; }

static context_list_node *cln_new(context_list_node *next, const char *ctx, const char *fn) {
  context_list_node *n = calloc(1, sizeof(*n));
  if (!n) return NULL;

  char *dup = strdup(ctx);
  if (!dup) { free(n); return NULL; }

  context_node_init(&n->node, dup, fn);
  n->next = next;
  return n;
}

static context_list_node *cln_find(context_list_node *l, const char *ctx) {
  for (; l; l = l->next) {
    if (!strcmp(l->node.context, ctx)) return l;
  }
  return NULL;
}

static void pn_add(prefix_node **list, const char *prefix, context_list_node *ctx) {
  size_t plen = strlen(prefix);
  prefix_node **p = list;
  while (*p && ((*p)->prefix_len >= plen || (*p)->prefix[0] == '*')) p = &(*p)->next;

  prefix_node *n = calloc(1, sizeof(*n));
  if (!n) return;

  n->prefix = strdup(prefix);
  if (!n->prefix) { free(n); return; }

  n->prefix_len = strlen(n->prefix);
  n->ctx = ctx;
  n->next = *p;
  *p = n;
}

static prefix_node *pn_find(prefix_node *l, const char *name) {
  for (; l; l = l->next) {
    if (l->prefix[0] == '*' || !strncmp(l->prefix, name, l->prefix_len)) return l;
  }
  return NULL;
}

static bool split_load(contexts_split *s, const char *filename) {
  FILE *f = fopen(filename, "re");
  if (!f) return false;

  char *buf = NULL;
  size_t blen;

  while (getline(&buf, &blen, f) > 0) {
    char *p = buf;
    while (isspace(*p)) p++;
    if (*p == '#' || !*p) continue;

    /* INFO: Parse two tokens */
    char *t1 = p;
    while (*p && !isspace(*p)) p++;
    if (!*p) continue;
    *p++ = '\0';

    while (isspace(*p)) p++;
    if (!*p) continue;

    char *t2 = p;
    while (*p && !isspace(*p)) p++;
    *p = '\0';

    char *prefix = strdup(t1);
    char *ctx = strdup(t2);
    if (!prefix || !ctx) { free(prefix); free(ctx); continue; }

    if (!strncmp(prefix, "ctl.", 4)) { free(prefix); free(ctx); continue; }

    context_list_node *old = cln_find(s->contexts, ctx);
    if (!old) {
      old = cln_new(s->contexts, ctx, s->filename);
      s->contexts = old;
    }

    pn_add(&s->prefixes, prefix, old);
    free(prefix);
    free(ctx);
  }

  free(buf);
  fclose(f);
  return true;
}

static bool split_initialize(contexts *b, bool writable, const char *filename, bool *ff) {
  contexts_split *s = as_split(b);
  s->filename = filename;

  if (!split_load(s, "/property_contexts")) {
    if (access("/system/etc/selinux/plat_property_contexts", R_OK) != -1) {
      split_load(s, "/system/etc/selinux/plat_property_contexts");
      split_load(s, access("/vendor/etc/selinux/vendor_property_contexts", R_OK) != -1
        ? "/vendor/etc/selinux/vendor_property_contexts"
        : "/vendor/etc/selinux/nonplat_property_contexts");
    } else {
      if (!split_load(s, "/plat_property_contexts")) return false;
      split_load(s, access("/vendor_property_contexts", R_OK) != -1
        ? "/vendor_property_contexts"
        : "/nonplat_property_contexts");
    }
  }

  if (writable) {
    mkdir(s->filename, S_IRWXU | S_IXGRP | S_IXOTH);
    if (ff) *ff = false;

    for (context_list_node *l = s->contexts; l; l = l->next) {
      if (!context_node_open(&l->node, true, ff)) {
        contexts_free_and_unmap(b);
        return false;
      }
    }

    if (!map_serial(&s->serial_pa, s->filename, true, NULL, ff)) {
      contexts_free_and_unmap(b);
      return false;
    }
  } else {
    if (!map_serial(&s->serial_pa, s->filename, false, &b->rw, NULL)) {
      contexts_free_and_unmap(b);
      return false;
    }
  }

  return true;
}

static prop_area *split_get_area(contexts *b, const char *name) {
  prefix_node *e = pn_find(as_split(b)->prefixes, name);
  if (!e) return NULL;

  context_node *n = &e->ctx->node;
  if (!n->pa) context_node_open(n, false, NULL);
  return n->pa;
}

static prop_area *split_get_serial(contexts *b) { return as_split(b)->serial_pa; }

static const char *split_get_context(contexts *b, const char *name) {
  prefix_node *e = pn_find(as_split(b)->prefixes, name);
  return e ? e->ctx->node.context : NULL;
}

static void split_for_each(contexts *b, void (*fn)(const prop_info *, void *), void *c) {
  for (context_list_node *l = as_split(b)->contexts; l; l = l->next) {
    if (context_node_check_access_and_open(&l->node)) {
      prop_area_foreach(l->node.pa, fn, c);
    }
  }
}

static void split_for_each_ctx(contexts *b, void (*fn)(prop_area *, const char *, void *), void *c) {
  for (context_list_node *l = as_split(b)->contexts; l; l = l->next) {
    if (context_node_check_access_and_open(&l->node)) {
      fn(l->node.pa, l->node.context, c);
    }
  }
}

static bool split_compact(contexts *b) {
  bool ret = true;
  for (context_list_node *l = as_split(b)->contexts; l; l = l->next) {
    if (!context_node_check_access_and_open(&l->node) || !prop_area_compact(l->node.pa)) {
      ret = false;
    }
  }
  return ret && (!as_split(b)->serial_pa || prop_area_compact(as_split(b)->serial_pa));
}

static bool split_compact_ctx(contexts *b, const char *ctx, bool *found) {
  *found = false;
  bool ret = true;

  for (context_list_node *l = as_split(b)->contexts; l; l = l->next) {
    if (!strcmp(l->node.context, ctx)) {
      *found = true;
      if (!context_node_check_access_and_open(&l->node) || !prop_area_compact(l->node.pa)) {
        ret = false;
      }
    }
  }

  if (!*found && as_split(b)->serial_pa && !strcmp(ctx, "u:object_r:properties_serial:s0")) {
    *found = true;
    ret = prop_area_compact(as_split(b)->serial_pa);
  }

  return *found && ret;
}

static void split_reset(contexts *b) {
  for (context_list_node *l = as_split(b)->contexts; l; l = l->next) {
    context_node_reset_access(&l->node);
  }
}

static void split_free(contexts *b) {
  contexts_split *s = as_split(b);

  while (s->prefixes) {
    prefix_node *n = s->prefixes;
    s->prefixes = n->next;
    free(n->prefix);
    free(n);
  }

  while (s->contexts) {
    context_list_node *n = s->contexts;
    s->contexts = n->next;

    /* INFO: Inline context_node_destroy */
    context_node_unmap(&n->node);
    pthread_mutex_destroy(&n->node.mutex);

    free((char *)n->node.context);
    free(n);
  }

  prop_area_unmap_prop_area(&s->serial_pa);
}

static const contexts_ops split_ops = {
  split_initialize, split_get_area, split_get_serial, split_get_context,
  split_for_each, split_compact, split_compact_ctx, split_reset, split_free, split_for_each_ctx,
};

void contexts_split_init(contexts_split *s) {
  memset(s, 0, sizeof(*s));
  s->base.ops = &split_ops;
}

/* INFO: Pre-split contexts (old Android) */

static contexts_pre_split *as_pre(contexts *b) { return (contexts_pre_split *)b; }

static bool pre_initialize(contexts *b, bool w, const char *fn, bool *ff) {
  (void)w;
  (void)ff;
  contexts_pre_split *s = as_pre(b);
  s->pa = prop_area_map_prop_area(fn, &b->rw);
  return s->pa != NULL;
}

static prop_area *pre_get_area(contexts *b, const char *n) { (void)n; return as_pre(b)->pa; }
static prop_area *pre_get_serial(contexts *b) { return as_pre(b)->pa; }

static const char *pre_get_context(contexts *b, const char *n) {
  (void)b;
  (void)n;
  return "u:object_r:properties_device:s0";
}

static void pre_for_each(contexts *b, void (*fn)(const prop_info *, void *), void *c) {
  prop_area_foreach(as_pre(b)->pa, fn, c);
}

static void pre_for_each_ctx(contexts *b, void (*fn)(prop_area *, const char *, void *), void *c) {
  fn(as_pre(b)->pa, pre_get_context(b, NULL), c);
}

static bool pre_compact(contexts *b) { return prop_area_compact(as_pre(b)->pa); }

static bool pre_compact_ctx(contexts *b, const char *ctx, bool *found) {
  *found = ctx && !strcmp(ctx, pre_get_context(b, ""));
  return *found ? prop_area_compact(as_pre(b)->pa) : false;
}

static void pre_reset(contexts *b) { (void)b; }
static void pre_free(contexts *b) { prop_area_unmap_prop_area(&as_pre(b)->pa); }

static const contexts_ops pre_ops = {
  pre_initialize, pre_get_area, pre_get_serial, pre_get_context,
  pre_for_each, pre_compact, pre_compact_ctx, pre_reset, pre_free, pre_for_each_ctx,
};

void contexts_pre_split_init(contexts_pre_split *s) {
  memset(s, 0, sizeof(*s));
  s->base.ops = &pre_ops;
}
