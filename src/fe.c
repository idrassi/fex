/*
** Copyright (c) 2020 rxi
** Copyright (c) 2025 Mounir IDRASSI <mounir.idrassi@amcrypto.jp>
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to
** deal in the Software without restriction, including without limitation the
** rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
** sell copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
** IN THE SOFTWARE.
*/

/* Fe Core Language */


#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#else
#include <sys/time.h>
#endif
#include <limits.h>
#include <string.h>
#include "fe.h"
#include "fex.h"
#include "fex_internal.h"

#define unused(x)     ( (void) (x) )
#define car(x)        ( (x)->car.o )
#define cdr(x)        ( (x)->cdr.o )
#define tag(x)        ( (x)->flags )
#define isnil(x)      ( (x) == &nil )
#define type(x) \
   ( FE_IS_FIXNUM(x)              ? FE_TNUMBER :          \
     FE_IS_BOOLEAN(x)             ? FE_TBOOLEAN :         \
     ((tag(x) & 1)                ? tag(x) >> 2 : FE_TPAIR) )
#define settype(x,t)  ( tag(x) = (t) << 2 | 1 )
#define number(x)     ( (x)->cdr.n )
#define prim(x)       ( (x)->cdr.c )
#define cfunc(x)      ( (x)->cdr.f )
#define nval(o)       fe_num_value(o)

#define GCMARKBIT     ( 0x2 )
#define GCSTACKSIZE   ( 1024 )

/* --- GC constants --- */
#define GC_GROWTH_FACTOR 2
#define GC_INITIAL_DIVISOR 4
#define GC_MIN_THRESHOLD 1024


#ifdef FE_OPT_NO_MALLOC_STRINGS
/* --- String Slab Allocator Constants --- */
#ifndef FE_STR_ARENA_RATIO
#define FE_STR_ARENA_RATIO 0.3
#endif
#define FE_SLAB_SIZE 64
#define FE_SLAB_DATA_SIZE (FE_SLAB_SIZE - sizeof(uint32_t))
#define FE_SLAB_NULL ((uint32_t)-1)

typedef struct {
    uint32_t next; /* Offset of next slab in chain, or in freelist */
    char data[FE_SLAB_DATA_SIZE];
} fe_Slab;
#endif


enum {
 P_LET, P_SET, P_IF, P_FN, P_MAC, P_WHILE,
 P_RETURN, P_MODULE, P_EXPORT, P_IMPORT, P_GET, P_PUT,
 P_QUOTE, P_AND, P_OR, P_DO, P_CONS,
 P_CAR, P_CDR, P_SETCAR, P_SETCDR, P_LIST, P_NOT, P_IS, P_ATOM, P_PRINT, P_LT,
 P_LTE, P_ADD, P_SUB, P_MUL, P_DIV, P_MAX
};

static const char *primnames[] = {
  "let", "=", "if", "fn", "mac", "while", "return",
  "module", "export", "import", "get", "put",
  "quote", "and", "or", "do", "cons",
  "car", "cdr", "setcar", "setcdr", "list", "not", "is", "atom", "print", "<",
  "<=", "+", "-", "*", "/"
};

static const char *typenames[] = {
  "pair", "free", "nil", "number", "symbol", "string", "bytes",
  "func", "macro", "prim", "cfunc", "ptr", "map",
  "boolean"
};

typedef struct {
  int count;
  int used;
  int capacity;
  fe_Object **keys;
  fe_Object **values;
  unsigned char *states;
} fe_Map;

typedef union {
  size_t size;
  void *ptr;
  double dbl;
  long double ldouble;
} fe_AllocHeader;

typedef union { fe_Object *o; fe_CFunc f; fe_Number n; char c; char *s; void *p; uint32_t u32; } Value;

struct fe_Object {
  Value car, cdr;
  uint8_t flags;                 /* holds GC-mark + type tag   */
};

struct fe_Context {
  fe_Handlers handlers;
  fe_Object *gcstack[GCSTACKSIZE];
  int gcstack_idx;
  fe_Object *objects;
  int object_count;
  fe_Object *calllist;
  fe_Object *freelist;
  fe_Object *modulestack;
  fe_Object *symlist;
  fe_Object *t;
  int nextchr;
  char **import_paths;
  int import_path_count;
  int import_path_capacity;
  char **source_dirs;
  int source_dir_count;
  int source_dir_capacity;
  char **source_buffers;
  int source_buffer_count;
  int source_buffer_capacity;
  char **loading_modules;
  int loading_module_count;
  int loading_module_capacity;
  char **loaded_modules;
  int loaded_module_count;
  int loaded_module_capacity;
  size_t step_limit;
  size_t steps_executed;
  uint64_t timeout_ms;
  uint64_t timeout_deadline_ms;
  size_t timeout_countdown;
  size_t interrupt_interval;
  size_t interrupt_countdown;
  fe_InterruptFn interrupt_handler;
  void *interrupt_udata;
  int eval_depth;
  /* --- GC fields --- */
  int live_count;          /* Objects surviving last GC */
  int allocs_since_gc;     /* Objects allocated since last GC */
  int gc_threshold;        /* Trigger next GC when allocs_since_gc exceeds this */
  size_t bytes_since_gc;   /* String bytes allocated since last GC */
  size_t byte_threshold;   /* Trigger next GC when bytes_since_gc exceeds this */
  size_t gc_runs;
  size_t object_allocations_total;
  size_t base_memory_bytes;
  size_t memory_limit;
  size_t memory_used;
  size_t peak_memory_used;
  int alloc_failure_active;
  int alloc_failure_is_limit;
#ifdef FE_OPT_NO_MALLOC_STRINGS
  uint8_t *str_base;
  uint8_t *str_end;
  uint32_t str_freelist;   /* Offset of first free slab head */
#endif
};

static fe_Object nil = {{ NULL }, { NULL }, (FE_TNIL << 2 | 1)};

static fe_Object *return_sym = NULL;
static fe_Object *frame_sym = NULL; /* Tag for new environment frames */
/* Symbols for static analysis */
static fe_Object *do_sym = NULL;
static fe_Object *let_sym = NULL;
static fe_Object *quote_sym = NULL;
static fe_Object *fn_sym = NULL;
static fe_Object *mac_sym = NULL;

#define MAP_EMPTY 0
#define MAP_USED 1
#define MAP_TOMBSTONE 2
#define TIMEOUT_CHECK_INTERVAL 64

static fe_Map* mapdata(fe_Object *obj);
static fe_Object* normalize_map_key(fe_Context *ctx, fe_Object *key);
static int map_find_slot(fe_Context *ctx, fe_Map *map, fe_Object *key, int *found);
static fe_Object* object(fe_Context *ctx);
static void tracked_free(fe_Context *ctx, void *ptr);

static int memory_would_exceed_limit(fe_Context *ctx, size_t extra) {
  if (ctx->memory_limit == 0) {
    return 0;
  }
  if (ctx->memory_used > ctx->memory_limit) {
    return 1;
  }
  return extra > ctx->memory_limit - ctx->memory_used;
}

static void memory_note_alloc(fe_Context *ctx, size_t size) {
  ctx->memory_used += size;
  if (ctx->memory_used > ctx->peak_memory_used) {
    ctx->peak_memory_used = ctx->memory_used;
  }
  ctx->alloc_failure_active = 0;
  ctx->alloc_failure_is_limit = 0;
}

static void memory_note_free(fe_Context *ctx, size_t size) {
  size_t floor = ctx->base_memory_bytes;
  if (ctx->memory_used >= floor + size) {
    ctx->memory_used -= size;
  } else {
    ctx->memory_used = floor;
  }
}

static void* tracked_alloc(fe_Context *ctx, size_t size) {
  fe_AllocHeader *header;
  size_t payload = (size > 0) ? size : 1;

  if (size > SIZE_MAX - sizeof(*header)) {
    ctx->alloc_failure_active = 1;
    ctx->alloc_failure_is_limit = 0;
    return NULL;
  }
  if (memory_would_exceed_limit(ctx, size)) {
    ctx->alloc_failure_active = 1;
    ctx->alloc_failure_is_limit = 1;
    return NULL;
  }

  header = malloc(sizeof(*header) + payload);
  if (!header) {
    ctx->alloc_failure_active = 1;
    ctx->alloc_failure_is_limit = 0;
    return NULL;
  }

  header->size = size;
  memory_note_alloc(ctx, size);
  return header + 1;
}

static void* tracked_realloc(fe_Context *ctx, void *ptr, size_t new_size) {
  fe_AllocHeader *header;
  fe_AllocHeader *grown;
  size_t payload;
  size_t old_size;

  if (!ptr) {
    return tracked_alloc(ctx, new_size);
  }
  if (new_size == 0) {
    tracked_free(ctx, ptr);
    return NULL;
  }

  header = ((fe_AllocHeader*)ptr) - 1;
  old_size = header->size;
  if (new_size > SIZE_MAX - sizeof(*header)) {
    ctx->alloc_failure_active = 1;
    ctx->alloc_failure_is_limit = 0;
    return NULL;
  }
  if (new_size > old_size && memory_would_exceed_limit(ctx, new_size - old_size)) {
    ctx->alloc_failure_active = 1;
    ctx->alloc_failure_is_limit = 1;
    return NULL;
  }

  payload = new_size;
  grown = realloc(header, sizeof(*header) + payload);
  if (!grown) {
    ctx->alloc_failure_active = 1;
    ctx->alloc_failure_is_limit = 0;
    return NULL;
  }

  grown->size = new_size;
  if (new_size > old_size) {
    memory_note_alloc(ctx, new_size - old_size);
  } else if (old_size > new_size) {
    memory_note_free(ctx, old_size - new_size);
  } else {
    ctx->alloc_failure_active = 0;
    ctx->alloc_failure_is_limit = 0;
  }
  return grown + 1;
}

static void tracked_free(fe_Context *ctx, void *ptr) {
  fe_AllocHeader *header;
  if (!ptr) {
    return;
  }
  header = ((fe_AllocHeader*)ptr) - 1;
  memory_note_free(ctx, header->size);
  free(header);
}

static void memory_error(fe_Context *ctx, const char *fallback_msg) {
  if (ctx->alloc_failure_is_limit) {
    fe_error(ctx, "memory limit exceeded");
  }
  fe_error(ctx, fallback_msg);
}

static int list_has(fe_Object *list, fe_Object *item) {
  for (; !isnil(list); list = cdr(list)) {
    if (car(list) == item) { return 1; }
  }
  return 0;
}

/* Truth test that all new code should use                               */
static int fe_truthy(fe_Object *o) {
    return !FE_IS_FALSE(o) && !isnil(o);
}

static fe_Map* mapdata(fe_Object *obj) {
  return (fe_Map*)obj->cdr.p;
}

static void analyze(fe_Context *ctx, fe_Object *node, fe_Object *bound, fe_Object **free_vars) {
  /* Base case: atom. If it's a symbol not in our bound list, it's free. */
  if (type(node) != FE_TPAIR) {
    if (type(node) == FE_TSYMBOL && !list_has(bound, node) && !list_has(*free_vars, node)) {
      *free_vars = fe_cons(ctx, node, *free_vars);
    }
    return;
  }

  fe_Object *op = car(node);
  fe_Object *args = cdr(node);
  fe_Object *p;

  /* Special form handling */
  if (op == quote_sym) {
    return; /* Don't analyze contents of a quote */
  }

  if (op == do_sym) {
    fe_Object *local_bound = bound;
    int gc = fe_savegc(ctx);
    fe_pushgc(ctx, local_bound);

    for (p = args; !isnil(p); p = cdr(p)) {
      fe_Object *stmt = car(p);
      /* Check for (let var expr) */
      if (type(stmt) == FE_TPAIR && car(stmt) == let_sym) {
        fe_Object *let_args = cdr(stmt);
        fe_Object *var = car(let_args);
        fe_Object *expr = car(cdr(let_args));

        /* Analyze the expression in the current environment */
        analyze(ctx, expr, local_bound, free_vars);

        /* Add the new variable to the local bound list for subsequent statements */
        local_bound = fe_cons(ctx, var, local_bound);
        fe_restoregc(ctx, gc);
        fe_pushgc(ctx, local_bound);
      } else {
        analyze(ctx, stmt, local_bound, free_vars);
      }
    }
    fe_restoregc(ctx, gc);
    return;
  }

  if (op == fn_sym || op == mac_sym) {
    fe_Object *params = car(args);
    fe_Object *body = car(cdr(args));
    fe_Object *inner_free;
    int gc = fe_savegc(ctx);

    /* The inner function's bound variables start with its own parameters. */
    fe_Object *inner_bound = &nil;
    fe_pushgc(ctx, inner_bound);
    for (p = params; !isnil(p); p = cdr(p)) {
      inner_bound = fe_cons(ctx, car(p), inner_bound);
    }

    inner_free = &nil;
    fe_pushgc(ctx, inner_free);
    analyze(ctx, body, inner_bound, &inner_free);
    fe_restoregc(ctx, gc);

    /* The inner function's free variables must be resolved in the outer scope.
       We treat them as expressions to be analyzed in the outer 'bound' list. */
    fe_pushgc(ctx, inner_free);
    for (p = inner_free; !isnil(p); p = cdr(p)) {
      analyze(ctx, car(p), bound, free_vars);
    }
    fe_restoregc(ctx, gc);
    return;
  }

  /* Generic case: treat as a function call. Analyze the operator and all arguments. */
  analyze(ctx, op, bound, free_vars);
  for (p = args; !isnil(p); p = cdr(p)) {
    if (type(p) == FE_TPAIR) {
        analyze(ctx, car(p), bound, free_vars);
    } else { /* Dotted pair in arguments */
        analyze(ctx, p, bound, free_vars);
        break;
    }
  }
}

fe_Handlers* fe_handlers(fe_Context *ctx) {
  return &ctx->handlers;
}


static uint64_t current_time_ms(void) {
#ifdef _WIN32
  return (uint64_t)GetTickCount64();
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000u + (uint64_t)(tv.tv_usec / 1000);
#endif
}


void fe_set_step_limit(fe_Context *ctx, size_t max_steps) {
  ctx->step_limit = max_steps;
}


size_t fe_get_step_limit(fe_Context *ctx) {
  return ctx->step_limit;
}


size_t fe_get_steps_executed(fe_Context *ctx) {
  return ctx->steps_executed;
}


void fe_set_memory_limit(fe_Context *ctx, size_t max_bytes) {
  ctx->memory_limit = max_bytes;
}


size_t fe_get_memory_limit(fe_Context *ctx) {
  return ctx->memory_limit;
}


size_t fe_get_memory_used(fe_Context *ctx) {
  return ctx->memory_used;
}


size_t fe_get_peak_memory_used(fe_Context *ctx) {
  return ctx->peak_memory_used;
}


void fe_get_stats(fe_Context *ctx, fe_Stats *out_stats) {
  if (!out_stats) return;
  memset(out_stats, 0, sizeof(*out_stats));
  out_stats->step_limit = ctx->step_limit;
  out_stats->steps_executed = ctx->steps_executed;
  out_stats->timeout_ms = ctx->timeout_ms;
  out_stats->memory_limit = ctx->memory_limit;
  out_stats->memory_used = ctx->memory_used;
  out_stats->peak_memory_used = ctx->peak_memory_used;
  out_stats->base_memory_bytes = ctx->base_memory_bytes;
  out_stats->object_capacity = (size_t)ctx->object_count;
  out_stats->live_objects = (size_t)((ctx->live_count > 0) ? ctx->live_count : 0);
  out_stats->object_allocations_total = ctx->object_allocations_total;
  out_stats->allocs_since_gc = (size_t)((ctx->allocs_since_gc > 0) ? ctx->allocs_since_gc : 0);
  out_stats->gc_runs = ctx->gc_runs;
}


void fe_set_timeout_ms(fe_Context *ctx, uint64_t timeout_ms) {
  ctx->timeout_ms = timeout_ms;
  if (timeout_ms > 0) {
    ctx->timeout_deadline_ms = current_time_ms() + timeout_ms;
    ctx->timeout_countdown = TIMEOUT_CHECK_INTERVAL;
  } else {
    ctx->timeout_deadline_ms = 0;
    ctx->timeout_countdown = 0;
  }
}


uint64_t fe_get_timeout_ms(fe_Context *ctx) {
  return ctx->timeout_ms;
}


void fe_set_interrupt_handler(fe_Context *ctx, fe_InterruptFn fn,
                              void *udata, size_t check_interval_steps) {
  ctx->interrupt_handler = fn;
  ctx->interrupt_udata = udata;
  ctx->interrupt_interval = (fn != NULL)
    ? (check_interval_steps > 0 ? check_interval_steps : 1024)
    : 0;
  ctx->interrupt_countdown = ctx->interrupt_interval;
}


static void begin_eval_run(fe_Context *ctx) {
  if (ctx->eval_depth == 0) {
    ctx->steps_executed = 0;
    if (ctx->timeout_ms > 0) {
      ctx->timeout_deadline_ms = current_time_ms() + ctx->timeout_ms;
      ctx->timeout_countdown = TIMEOUT_CHECK_INTERVAL;
    }
    ctx->interrupt_countdown = ctx->interrupt_interval;
  }
  ctx->eval_depth++;
}


static void end_eval_run(fe_Context *ctx) {
  if (ctx->eval_depth > 0) {
    ctx->eval_depth--;
  }
  if (ctx->eval_depth == 0) {
    if (ctx->timeout_ms > 0) {
      ctx->timeout_countdown = TIMEOUT_CHECK_INTERVAL;
    }
    ctx->interrupt_countdown = ctx->interrupt_interval;
  }
}


static const char* poll_eval_abort(fe_Context *ctx, int immediate) {
  if (ctx->timeout_ms > 0) {
    if (immediate || ctx->timeout_countdown <= 1) {
      ctx->timeout_countdown = TIMEOUT_CHECK_INTERVAL;
      if (current_time_ms() >= ctx->timeout_deadline_ms) {
        return "execution timeout exceeded";
      }
    } else {
      ctx->timeout_countdown--;
    }
  }
  if (ctx->interrupt_handler != NULL) {
    if (immediate || ctx->interrupt_countdown <= 1) {
      ctx->interrupt_countdown = ctx->interrupt_interval;
      if (ctx->interrupt_handler(ctx, ctx->interrupt_udata)) {
        return "execution interrupted";
      }
    } else {
      ctx->interrupt_countdown--;
    }
  }
  return NULL;
}


const char* fe_poll_abort(fe_Context *ctx) {
  return poll_eval_abort(ctx, 1);
}


static void check_eval_budget(fe_Context *ctx) {
  const char *abort_msg;

  ctx->steps_executed++;
  if (ctx->step_limit > 0 && ctx->steps_executed > ctx->step_limit) {
    fe_error(ctx, "execution step limit exceeded");
  }
  abort_msg = poll_eval_abort(ctx, 0);
  if (abort_msg != NULL) {
    fe_error(ctx, abort_msg);
  }
}


static char* dup_cstring(fe_Context *ctx, const char *src) {
  size_t len;
  char *copy;
  if (!src) return NULL;
  len = strlen(src) + 1;
  copy = tracked_alloc(ctx, len);
  if (!copy) return NULL;
  memcpy(copy, src, len);
  return copy;
}

static int ensure_string_array_capacity(fe_Context *ctx, char ***items, int *capacity, int needed) {
  int new_capacity;
  char **new_items;
  if (needed <= *capacity) return 1;
  new_capacity = (*capacity > 0) ? *capacity * 2 : 4;
  while (new_capacity < needed) {
    new_capacity *= 2;
  }
  new_items = tracked_realloc(ctx, *items, sizeof(char*) * (size_t)new_capacity);
  if (!new_items) return 0;
  *items = new_items;
  *capacity = new_capacity;
  return 1;
}

static int string_array_push_owned(fe_Context *ctx, char ***items, int *count, int *capacity, char *value) {
  if (!ensure_string_array_capacity(ctx, items, capacity, *count + 1)) return 0;
  (*items)[*count] = value;
  (*count)++;
  return 1;
}

static int string_array_push_copy(fe_Context *ctx, char ***items, int *count, int *capacity, const char *value) {
  char *copy = dup_cstring(ctx, value);
  if (!copy) return 0;
  if (!string_array_push_owned(ctx, items, count, capacity, copy)) {
    tracked_free(ctx, copy);
    return 0;
  }
  return 1;
}

static void string_array_clear(fe_Context *ctx, char ***items, int *count, int *capacity) {
  int i;
  for (i = 0; i < *count; i++) {
    tracked_free(ctx, (*items)[i]);
  }
  tracked_free(ctx, *items);
  *items = NULL;
  *count = 0;
  *capacity = 0;
}

static int string_array_contains(char **items, int count, const char *value) {
  int i;
  for (i = 0; i < count; i++) {
    if (strcmp(items[i], value) == 0) return 1;
  }
  return 0;
}

static void string_array_pop(fe_Context *ctx, char **items, int *count) {
  if (*count <= 0) return;
  (*count)--;
  tracked_free(ctx, items[*count]);
  items[*count] = NULL;
}

static int is_path_separator(char chr) {
  return chr == '/' || chr == '\\';
}

static void normalize_path_chars(char *path) {
  char *p;
  if (!path) return;
  for (p = path; *p; p++) {
    if (is_path_separator(*p)) {
      *p = '/';
#ifdef _WIN32
    } else if (*p >= 'A' && *p <= 'Z') {
      *p = (char)('a' + (*p - 'A'));
#endif
    }
  }
}

static char* dup_normalized_path(fe_Context *ctx, const char *path) {
  char *copy = dup_cstring(ctx, path);
  if (!copy) return NULL;
  normalize_path_chars(copy);
  return copy;
}

static char* normalize_existing_path(fe_Context *ctx, const char *path) {
#ifdef _WIN32
  char resolved[MAX_PATH];
#else
  char resolved[4096];
#endif
  char *copy;
#ifdef _WIN32
  if (!_fullpath(resolved, path, sizeof(resolved))) return NULL;
#else
  if (!realpath(path, resolved)) return NULL;
#endif
  copy = dup_cstring(ctx, resolved);
  if (!copy) return NULL;
  normalize_path_chars(copy);
  return copy;
}

static char* path_dirname_copy(fe_Context *ctx, const char *path) {
  const char *last_sep = NULL;
  const char *p;
  size_t len;
  char *dir;

  if (!path || !*path) return dup_cstring(ctx, ".");
  for (p = path; *p; p++) {
    if (is_path_separator(*p)) last_sep = p;
  }
  if (!last_sep) return dup_cstring(ctx, ".");

  len = (size_t)(last_sep - path);
  if (len == 0) len = 1;
  dir = tracked_alloc(ctx, len + 1);
  if (!dir) return NULL;
  memcpy(dir, path, len);
  dir[len] = '\0';
  return dir;
}

static char* join_path_suffix(fe_Context *ctx, const char *base,
                              const char *name, const char *suffix) {
  size_t base_len;
  size_t name_len;
  size_t needs_sep;
  size_t suffix_len;
  char *path;

  base_len = strlen(base);
  name_len = strlen(name);
  suffix_len = strlen(suffix);
  needs_sep = (base_len > 0 && !is_path_separator(base[base_len - 1])) ? 1 : 0;
  path = tracked_alloc(ctx, base_len + needs_sep + name_len + suffix_len + 1);
  if (!path) return NULL;
  memcpy(path, base, base_len);
  if (needs_sep) path[base_len++] = '/';
  memcpy(path + base_len, name, name_len);
  base_len += name_len;
  memcpy(path + base_len, suffix, suffix_len + 1);
  return path;
}

static char* join_module_file_path(fe_Context *ctx, const char *base,
                                   const char *module_name) {
  return join_path_suffix(ctx, base, module_name, ".fex");
}

static char* join_module_index_path(fe_Context *ctx, const char *base,
                                    const char *module_name) {
  return join_path_suffix(ctx, base, module_name, "/index.fex");
}

static int file_exists(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return 0;
  fclose(fp);
  return 1;
}

static char* read_text_file(fe_Context *ctx, const char *path) {
  enum { SOURCE_BUFFER_TAIL_SLACK = 8 };
  FILE *file;
  long file_size;
  size_t bytes_read;
  char *buffer;

  file = fopen(path, "rb");
  if (!file) return NULL;
  if (fseek(file, 0L, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  file_size = ftell(file);
  if (file_size < 0) {
    fclose(file);
    return NULL;
  }
  rewind(file);

  /* Keep a few trailing NUL bytes so lexer/parser lookahead never steps
   * into uninitialized heap memory at the end of file-backed sources. */
  buffer = tracked_alloc(ctx, (size_t)file_size + SOURCE_BUFFER_TAIL_SLACK);
  if (!buffer) {
    fclose(file);
    return NULL;
  }
  bytes_read = fread(buffer, 1, (size_t)file_size, file);
  if (bytes_read != (size_t)file_size && ferror(file)) {
    tracked_free(ctx, buffer);
    fclose(file);
    return NULL;
  }
  memset(buffer + bytes_read, 0, SOURCE_BUFFER_TAIL_SLACK);
  fclose(file);
  return buffer;
}

static int push_source_dir_from_file(fe_Context *ctx, const char *path) {
  char *normalized_path = normalize_existing_path(ctx, path);
  char *dir = path_dirname_copy(ctx, normalized_path ? normalized_path : path);
  tracked_free(ctx, normalized_path);
  if (!dir) return 0;
  if (!string_array_push_owned(ctx, &ctx->source_dirs, &ctx->source_dir_count,
                               &ctx->source_dir_capacity, dir)) {
    tracked_free(ctx, dir);
    return 0;
  }
  return 1;
}

static void pop_source_dir(fe_Context *ctx) {
  string_array_pop(ctx, ctx->source_dirs, &ctx->source_dir_count);
}

static char* format_search_paths(fe_Context *ctx, char **items, int count) {
  size_t total = 1;
  size_t prefix_len = 4;
  int i;
  char *buf;
  char *p;

  if (count <= 0) return dup_cstring(ctx, "");
  for (i = 0; i < count; i++) {
    total += prefix_len + strlen(items[i]) + 1;
  }

  buf = tracked_alloc(ctx, total);
  if (!buf) return NULL;

  p = buf;
  for (i = 0; i < count; i++) {
    size_t item_len = strlen(items[i]);
    memcpy(p, "  - ", prefix_len);
    p += prefix_len;
    memcpy(p, items[i], item_len);
    p += item_len;
    *p++ = '\n';
  }
  *p = '\0';
  return buf;
}

static int try_resolve_module_candidate(fe_Context *ctx, const char *candidate,
                                        char ***searched, int *searched_count,
                                        int *searched_capacity,
                                        char **resolved_path) {
  char *normalized;

  if (searched &&
      !string_array_push_copy(ctx, searched, searched_count,
                              searched_capacity, candidate)) {
    return -1;
  }
  if (!file_exists(candidate)) {
    return 0;
  }

  normalized = normalize_existing_path(ctx, candidate);
  if (normalized) {
    *resolved_path = normalized;
    return 1;
  }
  if (ctx->alloc_failure_active) {
    return -1;
  }

  normalized = dup_normalized_path(ctx, candidate);
  if (!normalized) return -1;
  *resolved_path = normalized;
  return 1;
}

static int try_resolve_module_under_base(fe_Context *ctx, const char *base,
                                         const char *module_name,
                                         char ***searched, int *searched_count,
                                         int *searched_capacity,
                                         char **resolved_path) {
  char *candidate;
  int status;

  candidate = join_module_file_path(ctx, base, module_name);
  if (!candidate) return -1;
  status = try_resolve_module_candidate(ctx, candidate, searched,
                                        searched_count, searched_capacity,
                                        resolved_path);
  tracked_free(ctx, candidate);
  if (status != 0) return status;

  candidate = join_module_index_path(ctx, base, module_name);
  if (!candidate) return -1;
  status = try_resolve_module_candidate(ctx, candidate, searched,
                                        searched_count, searched_capacity,
                                        resolved_path);
  tracked_free(ctx, candidate);
  return status;
}

static char* resolve_module_file(fe_Context *ctx, const char *module_name,
                                 char **searched_paths) {
  int i;
  int status;
  char *resolved = NULL;
  char **searched = NULL;
  int searched_count = 0;
  int searched_capacity = 0;

  if (searched_paths) *searched_paths = NULL;

  if (ctx->source_dir_count > 0) {
    status = try_resolve_module_under_base(ctx,
                                           ctx->source_dirs[ctx->source_dir_count - 1],
                                           module_name,
                                           searched_paths ? &searched : NULL,
                                           &searched_count, &searched_capacity,
                                           &resolved);
    if (status > 0) goto resolved;
    if (status < 0) goto done;
  }

  for (i = 0; i < ctx->import_path_count; i++) {
    status = try_resolve_module_under_base(ctx, ctx->import_paths[i], module_name,
                                           searched_paths ? &searched : NULL,
                                           &searched_count, &searched_capacity,
                                           &resolved);
    if (status > 0) goto resolved;
    if (status < 0) goto done;
  }

  status = try_resolve_module_under_base(ctx, ".", module_name,
                                         searched_paths ? &searched : NULL,
                                         &searched_count, &searched_capacity,
                                         &resolved);
  if (status > 0) goto resolved;

done:
  if (searched_paths) {
    *searched_paths = format_search_paths(ctx, searched, searched_count);
  }
  string_array_clear(ctx, &searched, &searched_count, &searched_capacity);
  return resolved;

resolved:
  string_array_clear(ctx, &searched, &searched_count, &searched_capacity);
  return resolved;
}

void fex_clear_import_paths(fe_Context *ctx) {
  string_array_clear(ctx, &ctx->import_paths, &ctx->import_path_count, &ctx->import_path_capacity);
}

int fex_add_import_path(fe_Context *ctx, const char *path) {
  char *normalized;
  if (!path || !*path) return 0;
  normalized = normalize_existing_path(ctx, path);
  if (!normalized) {
    normalized = dup_normalized_path(ctx, path);
    if (!normalized) return 0;
  }
  if (!string_array_push_owned(ctx, &ctx->import_paths, &ctx->import_path_count,
                               &ctx->import_path_capacity, normalized)) {
    tracked_free(ctx, normalized);
    return 0;
  }
  return 1;
}

void fex_reset_import_state(fe_Context *ctx) {
  while (ctx->source_buffer_count > 0) {
    string_array_pop(ctx, ctx->source_buffers, &ctx->source_buffer_count);
  }
  while (ctx->source_dir_count > 0) {
    pop_source_dir(ctx);
  }
  while (ctx->loading_module_count > 0) {
    string_array_pop(ctx, ctx->loading_modules, &ctx->loading_module_count);
  }
}

fe_Object* fex_do_file(fe_Context *ctx, const char *path) {
  char *source;
  fe_Object *result;

  if (!push_source_dir_from_file(ctx, path)) {
    memory_error(ctx, "out of memory (source path)");
  }

  source = read_text_file(ctx, path);
  if (!source) {
    pop_source_dir(ctx);
    if (ctx->alloc_failure_active) {
      memory_error(ctx, "out of memory (source file)");
    }
    if (fex_try_is_active()) {
      fex_try_raise(FEX_STATUS_IO_ERROR, path, 0, 0, "could not open input file");
    }
    return NULL;
  }
  if (!string_array_push_owned(ctx, &ctx->source_buffers, &ctx->source_buffer_count,
                               &ctx->source_buffer_capacity, source)) {
    pop_source_dir(ctx);
    tracked_free(ctx, source);
    memory_error(ctx, "out of memory (source path)");
  }

  result = fex_do_string_named(ctx, source, path);
  string_array_pop(ctx, ctx->source_buffers, &ctx->source_buffer_count);
  pop_source_dir(ctx);
  return result;
}


void fe_error(fe_Context *ctx, const char *msg) {
  fe_Object *cl = ctx->calllist;
  /* reset context state */
  ctx->calllist = &nil;
  ctx->eval_depth = 0;
  if (ctx->timeout_ms > 0) {
    ctx->timeout_countdown = TIMEOUT_CHECK_INTERVAL;
  }
  ctx->interrupt_countdown = ctx->interrupt_interval;
  /* do error handler */
  if (ctx->handlers.error) { ctx->handlers.error(ctx, msg, cl); }
  /* error handler returned -- print error and traceback, exit */
  fprintf(stderr, "error: %s\n", msg);
  for (; !isnil(cl); cl = cdr(cl)) {
    char buf[64];
    fe_tostring(ctx, car(cl), buf, sizeof(buf));
    fprintf(stderr, "=> %s\n", buf);
  }
  exit(EXIT_FAILURE);
}


fe_Object* fe_nextarg(fe_Context *ctx, fe_Object **arg) {
  fe_Object *a = *arg;
  if (type(a) != FE_TPAIR) {
    if (isnil(a)) { fe_error(ctx, "too few arguments"); }
    fe_error(ctx, "dotted pair in argument list");
  }
  *arg = cdr(a);
  return car(a);
}


static fe_Object* checktype(fe_Context *ctx, fe_Object *obj, int type) {
  char buf[64];
  int actual_type = type(obj);

  /* Special case: allow fixnums when expecting numbers */
  if (type == FE_TNUMBER && FE_IS_FIXNUM(obj)) {
    return obj;
  }

  if (actual_type != type) {
    sprintf(buf, "expected %s, got %s",
            typenames[type],
            FE_IS_FIXNUM(obj) ? "number" : typenames[actual_type]);
    fe_error(ctx, buf);
  }
  return obj;
}

static fe_Object* checknum(fe_Context *ctx, fe_Object *obj)
{
    if (FE_IS_FIXNUM(obj)) return obj;           /* fine - immediate */
    return checktype(ctx, obj, FE_TNUMBER);      /* boxed double or error */
}

int fe_type(fe_Context *ctx, fe_Object *obj) {
  unused(ctx);
  return type(obj);
}

fe_Number fe_num_value(fe_Object *o)
{
    if (FE_IS_FIXNUM(o)) {
        return (fe_Number)FE_UNBOX_FIXNUM(o);
    } else {
        return (o)->cdr.n;
    }
}

fe_Object *fe_make_number(fe_Context *ctx, fe_Number v)
{
    /* 1. Is it an integer?  (Avoids FP rounding issues.)             */
    fe_Number iv = (fe_Number)(intptr_t)v;
    if (v == iv) {
        /* 2. Does it fit in the fixnum range for this word size?     */
        intptr_t i = (intptr_t)iv;
        intptr_t shr = i >> (8*sizeof(intptr_t)-2);   /* sign-extend  */
        if (shr == 0 || shr == -1) {                  /* fits */
            return FE_FIXNUM(i);
        }
    }
    /* Fallback: allocate the boxed double. */
    return fe_number(ctx, v);
}


int fe_isnil(fe_Context *ctx, fe_Object *obj) {
  unused(ctx);
  return isnil(obj);
}


void fe_pushgc(fe_Context *ctx, fe_Object *obj) {
  /* Immediates never reach the GC root stack */
  if (FE_IS_FIXNUM(obj) || FE_IS_BOOLEAN(obj) || obj == &nil) {
    return;
  }

  if (ctx->gcstack_idx == GCSTACKSIZE) {
    fe_error(ctx, "gc stack overflow");
  }
  ctx->gcstack[ctx->gcstack_idx++] = obj;
}

void fe_restoregc(fe_Context *ctx, int idx) {
  ctx->gcstack_idx = idx;
}


int fe_savegc(fe_Context *ctx) {
  return ctx->gcstack_idx;
}


void fe_mark(fe_Context *ctx, fe_Object *obj) {
    /*  Tail-recursive mark without goto, and with a fresh check
        every time we follow cdr(obj).  That way, if the cdr turned
        out to be an immediate fixnum we bail before dereferencing it. */

    for (;;) {
        /* 0. Fast exits for objects we never allocate. */
        if (FE_IS_FIXNUM(obj) || FE_IS_BOOLEAN(obj) || isnil(obj)) return;

        /* 1. Do not mark rogue pointers that don't belong to us. */
        if (obj < ctx->objects || obj >= ctx->objects + ctx->object_count)
            return;

        /* 2. Already marked?  Done. */
        if (tag(obj) & GCMARKBIT) return;
        tag(obj) |= GCMARKBIT;

        switch (type(obj)) {
        case FE_TPAIR:
            /* mark car, then continue with cdr in the next loop
               iteration (this removes deep recursion) */
            fe_mark(ctx, car(obj));
            obj = cdr(obj);
            continue;               /* re-check fixnum / nil next round */

        case FE_TFUNC:   /* (prototype . body) where prototype=(free_vars) */
        case FE_TMACRO:  /* (prototype . body) where prototype=(free_vars) */
        case FE_TSYMBOL: /* (name-string . value)   */
            obj = cdr(obj);
            continue;

#ifdef FE_OPT_NO_MALLOC_STRINGS
        case FE_TSTRING: /* String object holds offset; no further marking from here */
        case FE_TBYTES:
            return;
#else
        case FE_TSTRING: /* String object cdr holds a pointer we don't trace */
        case FE_TBYTES:
            return;
#endif

        case FE_TPTR:
            if (ctx->handlers.mark) ctx->handlers.mark(ctx, obj);
            /* fall-through */
        case FE_TMAP: {
            fe_Map *map = mapdata(obj);
            int i;
            if (!map) return;
            for (i = 0; i < map->capacity; i++) {
              if (map->states[i] == MAP_USED) {
                fe_mark(ctx, map->keys[i]);
                fe_mark(ctx, map->values[i]);
              }
            }
            return;
        }
        default:
            return;                 /* nothing more to traverse */
        }
    }
}


#ifdef FE_OPT_NO_MALLOC_STRINGS
static void str_slab_free(fe_Context *ctx, uint32_t offset) {
    if (offset == FE_SLAB_NULL) return;
    fe_Slab *slab = (fe_Slab*)(ctx->str_base + offset);
    slab->next = ctx->str_freelist;
    ctx->str_freelist = offset;
}

static void str_free_chain(fe_Context *ctx, uint32_t offset) {
    while (offset != FE_SLAB_NULL) {
        fe_Slab *slab = (fe_Slab*)(ctx->str_base + offset);
        uint32_t next_offset = slab->next;
        str_slab_free(ctx, offset);
        offset = next_offset;
    }
}
#endif


static void collectgarbage(fe_Context *ctx) {
  int i;
  int live = 0; /* Counter for live objects */
  /* mark */
  for (i = 0; i < ctx->gcstack_idx; i++) {
    if (!FE_IS_FIXNUM(ctx->gcstack[i]) && ctx->gcstack[i] != &nil) {
      fe_mark(ctx, ctx->gcstack[i]);
    }
  }
  fe_mark(ctx, ctx->modulestack);
  fe_mark(ctx, ctx->symlist);
  /* sweep and unmark */
  for (i = 0; i < ctx->object_count; i++) {
    fe_Object *obj = &ctx->objects[i];
    if (type(obj) == FE_TFREE) { continue; }
    if (~tag(obj) & GCMARKBIT) {
#ifdef FE_OPT_NO_MALLOC_STRINGS
      if (type(obj) == FE_TSTRING || type(obj) == FE_TBYTES) {
        str_free_chain(ctx, obj->cdr.u32);
      }
#else
      if ((type(obj)==FE_TSTRING || type(obj)==FE_TBYTES) && FE_STR_DATA(ctx, obj)) {
          tracked_free(ctx, FE_STR_DATA(ctx, obj));
      }
#endif
      if (type(obj) == FE_TMAP && mapdata(obj)) {
        fe_Map *map = mapdata(obj);
        tracked_free(ctx, map->keys);
        tracked_free(ctx, map->values);
        tracked_free(ctx, map->states);
        tracked_free(ctx, map);
      }
      if (type(obj) == FE_TPTR && ctx->handlers.gc) {
        ctx->handlers.gc(ctx, obj);
      }
      settype(obj, FE_TFREE);
      cdr(obj) = ctx->freelist;
      ctx->freelist = obj;
    } else {
      tag(obj) &= ~GCMARKBIT;
      live++; /* This object is alive */
    }
  }

  /* --- Update GC state and threshold --- */
  ctx->live_count = live;
  ctx->allocs_since_gc = 0;
  ctx->bytes_since_gc = 0;
  ctx->gc_threshold = ctx->live_count * GC_GROWTH_FACTOR;
  if (ctx->gc_threshold < GC_MIN_THRESHOLD) {
    ctx->gc_threshold = GC_MIN_THRESHOLD;
  }
  ctx->gc_runs++;
}

/* -------------------------------------------------------------------------
 * Early-return helper
 * ---------------------------------------------------------------------- */
static int is_return_obj(fe_Object *obj) {
  return type(obj) == FE_TPAIR && car(obj) == return_sym;
}

/* --------------------------------------------------------------------- */

#ifdef FE_OPT_NO_MALLOC_STRINGS
static int equal_slab(fe_Context *ctx, fe_Object *a, fe_Object *b) {
    size_t len = FE_STR_LEN(a);
    /* length is pre-checked by caller */
    if (len == 0) return 1;

    uint32_t offset_a = a->cdr.u32;
    uint32_t offset_b = b->cdr.u32;
    size_t remaining = len;

    while (remaining > 0) {
        fe_Slab *slab_a = (fe_Slab*)(ctx->str_base + offset_a);
        fe_Slab *slab_b = (fe_Slab*)(ctx->str_base + offset_b);
        size_t to_cmp = (remaining > FE_SLAB_DATA_SIZE) ? FE_SLAB_DATA_SIZE : remaining;

        if (memcmp(slab_a->data, slab_b->data, to_cmp) != 0) {
            return 0;
        }

        remaining -= to_cmp;
        if (remaining > 0) {
            offset_a = slab_a->next;
            offset_b = slab_b->next;
        }
    }
    return 1;
}
#endif

static int equal(fe_Context *ctx, fe_Object *a, fe_Object *b) {
  if (a == b) { return 1; }
  if (type(a) != type(b)) { return 0; }
  if (type(a) == FE_TNUMBER) { return nval(a) == nval(b); }
  if (type(a) == FE_TSTRING || type(a) == FE_TBYTES) {
    if (FE_STR_LEN(a) != FE_STR_LEN(b)) return 0;
#ifdef FE_OPT_NO_MALLOC_STRINGS
    return equal_slab(ctx, a, b);
#else
    return memcmp(FE_STR_DATA(ctx, a), FE_STR_DATA(ctx, b), FE_STR_LEN(a))==0;
#endif
  }
  return 0;
}

#ifdef FE_OPT_NO_MALLOC_STRINGS
static int streq_slab(fe_Context *ctx, fe_Object *obj, const char *str) {
    size_t len = FE_STR_LEN(obj);
    if (strlen(str) != len) return 0;
    if (len == 0) return 1;

    uint32_t offset = obj->cdr.u32;
    size_t remaining = len;

    while(offset != FE_SLAB_NULL && remaining > 0) {
        fe_Slab *slab = (fe_Slab*)(ctx->str_base + offset);
        size_t to_cmp = (remaining > FE_SLAB_DATA_SIZE) ? FE_SLAB_DATA_SIZE : remaining;
        if (memcmp(slab->data, str, to_cmp) != 0) {
            return 0;
        }
        str += to_cmp;
        remaining -= to_cmp;
        offset = slab->next;
    }
    return remaining == 0;
}
#endif

static int streq(fe_Context *ctx, fe_Object *obj, const char *str) {
#ifdef FE_OPT_NO_MALLOC_STRINGS
  return streq_slab(ctx, obj, str);
#else
  return strcmp(FE_STR_DATA(ctx, obj), str)==0;
#endif
}

int fe_symbol_name_eq(fe_Context *ctx, fe_Object *sym, const char *str) {
  (void)ctx;
  if (type(sym) != FE_TSYMBOL) { return 0; }
  return streq(ctx, car(cdr(sym)), str);
}

static fe_Object* normalize_map_key(fe_Context *ctx, fe_Object *key) {
  if (type(key) == FE_TSYMBOL) {
    return car(cdr(key));
  }
  if (type(key) == FE_TSTRING) {
    return key;
  }
  fe_error(ctx, "map keys must be strings or symbols");
  return &nil;
}

static unsigned long hash_string_obj(fe_Context *ctx, fe_Object *obj) {
  unsigned long hash = 2166136261u;
#ifdef FE_OPT_NO_MALLOC_STRINGS
  size_t remaining = FE_STR_LEN(obj);
  uint32_t offset = obj->cdr.u32;
  while (offset != FE_SLAB_NULL && remaining > 0) {
    fe_Slab *slab = (fe_Slab*)(ctx->str_base + offset);
    size_t to_hash = (remaining > FE_SLAB_DATA_SIZE) ? FE_SLAB_DATA_SIZE : remaining;
    size_t i;
    for (i = 0; i < to_hash; i++) {
      hash ^= (unsigned char)slab->data[i];
      hash *= 16777619u;
    }
    remaining -= to_hash;
    offset = slab->next;
  }
#else
  const unsigned char *p = (const unsigned char*)FE_STR_DATA(ctx, obj);
  size_t i;
  size_t len = FE_STR_LEN(obj);
  for (i = 0; i < len; i++) {
    hash ^= p[i];
    hash *= 16777619u;
  }
#endif
  return hash;
}

static fe_Map* map_alloc(fe_Context *ctx, int capacity) {
  fe_Map *map;
  map = tracked_alloc(ctx, sizeof(*map));
  if (!map) {
    return NULL;
  }
  map->count = 0;
  map->used = 0;
  map->capacity = capacity;
  map->keys = tracked_alloc(ctx, sizeof(fe_Object*) * (size_t)capacity);
  map->values = tracked_alloc(ctx, sizeof(fe_Object*) * (size_t)capacity);
  map->states = tracked_alloc(ctx, sizeof(unsigned char) * (size_t)capacity);
  if (!map->keys || !map->values || !map->states) {
    tracked_free(ctx, map->keys);
    tracked_free(ctx, map->values);
    tracked_free(ctx, map->states);
    tracked_free(ctx, map);
    return NULL;
  }
  memset(map->keys, 0, sizeof(fe_Object*) * capacity);
  memset(map->values, 0, sizeof(fe_Object*) * capacity);
  memset(map->states, 0, sizeof(unsigned char) * capacity);
  return map;
}

static int map_resize(fe_Context *ctx, fe_Map *map, int capacity) {
  fe_Map *grown;
  int i;

  grown = map_alloc(ctx, capacity);
  if (!grown) {
    return 0;
  }

  for (i = 0; i < map->capacity; i++) {
    int found;
    int slot;
    if (map->states[i] != MAP_USED) {
      continue;
    }
    slot = map_find_slot(ctx, grown, map->keys[i], &found);
    if (slot < 0) {
      tracked_free(ctx, grown->keys);
      tracked_free(ctx, grown->values);
      tracked_free(ctx, grown->states);
      tracked_free(ctx, grown);
      return 0;
    }
    grown->states[slot] = MAP_USED;
    grown->keys[slot] = map->keys[i];
    grown->values[slot] = map->values[i];
    grown->count++;
    grown->used++;
  }

  tracked_free(ctx, map->keys);
  tracked_free(ctx, map->values);
  tracked_free(ctx, map->states);
  map->keys = grown->keys;
  map->values = grown->values;
  map->states = grown->states;
  map->count = grown->count;
  map->used = grown->used;
  map->capacity = grown->capacity;
  tracked_free(ctx, grown);
  return 1;
}

static int map_ensure_capacity(fe_Context *ctx, fe_Map *map) {
  int min_capacity = 8;
  int target = map->capacity;
  if (target < min_capacity) {
    target = min_capacity;
  }
  if ((map->used + 1) * 4 < target * 3) {
    return 1;
  }
  while ((map->used + 1) * 4 >= target * 3) {
    target *= 2;
  }
  return map_resize(ctx, map, target);
}

static int map_find_slot(fe_Context *ctx, fe_Map *map, fe_Object *key, int *found) {
  unsigned long hash;
  int index;
  int first_tombstone = -1;
  int steps;

  *found = 0;
  if (map->capacity <= 0) {
    return -1;
  }

  hash = hash_string_obj(ctx, key);
  index = (int)(hash % (unsigned long)map->capacity);

  for (steps = 0; steps < map->capacity; steps++) {
    int slot = (index + steps) % map->capacity;
    if (map->states[slot] == MAP_EMPTY) {
      return (first_tombstone >= 0) ? first_tombstone : slot;
    }
    if (map->states[slot] == MAP_TOMBSTONE) {
      if (first_tombstone < 0) {
        first_tombstone = slot;
      }
      continue;
    }
    if (equal(ctx, map->keys[slot], key)) {
      *found = 1;
      return slot;
    }
  }

  return first_tombstone;
}

fe_Object* fe_map(fe_Context *ctx) {
  fe_Object *obj = object(ctx);
  fe_Map *map = map_alloc(ctx, 8);
  if (!map) {
    memory_error(ctx, "out of memory (map)");
  }
  settype(obj, FE_TMAP);
  car(obj) = &nil;
  obj->cdr.p = map;
  return obj;
}

int fe_map_set(fe_Context *ctx, fe_Object *map_obj, fe_Object *key, fe_Object *value) {
  fe_Map *map;
  int found;
  int slot;

  checktype(ctx, map_obj, FE_TMAP);
  key = normalize_map_key(ctx, key);
  if (type(key) != FE_TSTRING) {
    return 0;
  }

  map = mapdata(map_obj);
  if (!map_ensure_capacity(ctx, map)) {
    memory_error(ctx, "out of memory (map)");
  }

  slot = map_find_slot(ctx, map, key, &found);
  if (slot < 0) {
    memory_error(ctx, "out of memory (map)");
  }
  if (!found) {
    if (map->states[slot] == MAP_EMPTY) {
      map->used++;
    }
    map->states[slot] = MAP_USED;
    map->keys[slot] = key;
    map->count++;
  }
  map->values[slot] = value;
  return 1;
}

int fe_map_has(fe_Context *ctx, fe_Object *map_obj, fe_Object *key) {
  fe_Map *map;
  int found;
  checktype(ctx, map_obj, FE_TMAP);
  key = normalize_map_key(ctx, key);
  if (type(key) != FE_TSTRING) {
    return 0;
  }
  map = mapdata(map_obj);
  map_find_slot(ctx, map, key, &found);
  return found;
}

fe_Object* fe_map_get(fe_Context *ctx, fe_Object *map_obj, fe_Object *key) {
  fe_Map *map;
  int found;
  int slot;
  checktype(ctx, map_obj, FE_TMAP);
  key = normalize_map_key(ctx, key);
  if (type(key) != FE_TSTRING) {
    return &nil;
  }
  map = mapdata(map_obj);
  slot = map_find_slot(ctx, map, key, &found);
  if (!found || slot < 0) {
    return &nil;
  }
  return map->values[slot];
}

int fe_map_delete(fe_Context *ctx, fe_Object *map_obj, fe_Object *key) {
  fe_Map *map;
  int found;
  int slot;
  checktype(ctx, map_obj, FE_TMAP);
  key = normalize_map_key(ctx, key);
  if (type(key) != FE_TSTRING) {
    return 0;
  }
  map = mapdata(map_obj);
  slot = map_find_slot(ctx, map, key, &found);
  if (!found || slot < 0) {
    return 0;
  }
  map->states[slot] = MAP_TOMBSTONE;
  map->keys[slot] = NULL;
  map->values[slot] = NULL;
  map->count--;
  return 1;
}

int fe_map_count(fe_Context *ctx, fe_Object *map_obj) {
  fe_Map *map;
  checktype(ctx, map_obj, FE_TMAP);
  map = mapdata(map_obj);
  return map->count;
}

fe_Object* fe_map_keys(fe_Context *ctx, fe_Object *map_obj) {
  fe_Map *map;
  fe_Object *result = &nil;
  int i;
  checktype(ctx, map_obj, FE_TMAP);
  map = mapdata(map_obj);
  for (i = map->capacity - 1; i >= 0; i--) {
    if (map->states[i] == MAP_USED) {
      result = fe_cons(ctx, map->keys[i], result);
    }
  }
  return result;
}


static fe_Object* object(fe_Context *ctx) {
  fe_Object *obj;

  /* --- GC trigger logic --- */
  /* Trigger GC if object count or byte count exceeds the threshold,
   * or as a fallback if the freelist is empty. */
  if (ctx->allocs_since_gc >= ctx->gc_threshold ||
      ctx->bytes_since_gc >= ctx->byte_threshold ||
      isnil(ctx->freelist)) {
    collectgarbage(ctx);
    if (isnil(ctx->freelist)) { fe_error(ctx, "out of memory"); }
  }

  /* get object from freelist and push to the gcstack */
  obj = ctx->freelist;
  ctx->freelist = cdr(obj);

  /* Increment allocation counter and push to GC stack for protection */
  ctx->allocs_since_gc++;
  ctx->object_allocations_total++;
  fe_pushgc(ctx, obj);

  return obj;
}


fe_Object* fe_cons(fe_Context *ctx, fe_Object *car, fe_Object *cdr) {
  fe_Object *obj = object(ctx);
    obj->flags = 0;               /* essential: mark the object as a pair */
    car(obj)  = car;
    cdr(obj)  = cdr;
  return obj;
}


fe_Object* fe_bool(fe_Context *ctx, int b) {
  (void)ctx;
  return b ? FE_TRUE : FE_FALSE;
}

fe_Object* fe_nil(fe_Context *ctx) {
  (void)ctx;
  return &nil; /* Return the static nil object */
}

fe_Object* fe_number(fe_Context *ctx, fe_Number n) {
  fe_Object *obj = object(ctx);
  settype(obj, FE_TNUMBER);
  number(obj) = n;
  return obj;
}

#define GROW_STEP 64

#ifdef FE_OPT_NO_MALLOC_STRINGS
static uint32_t str_slab_alloc(fe_Context *ctx) {
    if (ctx->str_freelist == FE_SLAB_NULL) {
        /* Before failing, try to collect garbage to free up slabs */
        collectgarbage(ctx);
        if (ctx->str_freelist == FE_SLAB_NULL) {
            fe_error(ctx, "out of memory (string slab)");
        }
    }
    uint32_t offset = ctx->str_freelist;
    fe_Slab *slab = (fe_Slab*)(ctx->str_base + offset);
    ctx->str_freelist = slab->next;
    return offset;
}

static uint32_t str_alloc(fe_Context *ctx, const char *src, size_t len, char fill_char) {
    if (len == 0) {
        return FE_SLAB_NULL;
    }

    uint32_t head_offset = str_slab_alloc(ctx);
    fe_Slab *head_slab = (fe_Slab*)(ctx->str_base + head_offset);

    fe_Slab *current_slab = head_slab;

    const char *p = src;
    size_t remaining = len;
    size_t bytes_allocated = FE_SLAB_SIZE;

    while (remaining > 0) {
        size_t to_copy = (remaining > FE_SLAB_DATA_SIZE) ? FE_SLAB_DATA_SIZE : remaining;
        if (p)
        {
          memcpy(current_slab->data, p, to_copy);
          p += to_copy;
        }
        else
        {
          memset(current_slab->data, fill_char, to_copy); /* Fill with fill_char if p is NULL */
        }
        remaining -= to_copy;

        if (remaining > 0) {
            uint32_t next_offset = str_slab_alloc(ctx);
            bytes_allocated += FE_SLAB_SIZE;
            current_slab->next = next_offset;
            current_slab = (fe_Slab*)(ctx->str_base + next_offset);
        } else {
            current_slab->next = FE_SLAB_NULL;
        }
    }

    ctx->bytes_since_gc += bytes_allocated;
    return head_offset;
}
#endif

static fe_Object* make_data_obj(fe_Context *ctx,
                                int           type_tag,
                                const char   *src,
                                size_t        len,
                                char fill_char)
{
    fe_Object *o = object(ctx);
    settype(o, type_tag);
    car(o) = FE_FIXNUM((intptr_t)len);

#ifdef FE_OPT_NO_MALLOC_STRINGS
    o->cdr.u32 = str_alloc(ctx, src, len, fill_char);
#else
    char *buf = tracked_alloc(ctx, len + 1);
    if (!buf) memory_error(ctx, "out of memory (string)");
    ctx->bytes_since_gc += len + 1;
    if (src)
    {
      memcpy(buf, src, len);
    }
    else
    {
      memset(buf, fill_char, len); /* Fill with fill_char if src is NULL */
    }
    buf[len]='\0';
    o->cdr.s = buf;
#endif
    return o;
}

fe_Object* fe_string(fe_Context *ctx, const char *str, size_t len)
{
    return make_data_obj(ctx, FE_TSTRING, str, len, 0);
}

fe_Object* fe_string_raw(fe_Context *ctx, size_t len, char fill_char)
{
    return make_data_obj(ctx, FE_TSTRING, NULL, len, fill_char);
}

fe_Object* fe_bytes(fe_Context *ctx, const void *data, size_t len)
{
    return make_data_obj(ctx, FE_TBYTES, (const char*)data, len, 0);
}

fe_Object* fe_bytes_raw(fe_Context *ctx, size_t len, unsigned char fill_byte)
{
    return make_data_obj(ctx, FE_TBYTES, NULL, len, (char)fill_byte);
}


fe_Object* fe_symbol(fe_Context *ctx, const char *name) {
  fe_Object *obj;
  /* try to find in symlist */
  for (obj = ctx->symlist; !isnil(obj); obj = cdr(obj)) {
    if (streq(ctx, car(cdr(car(obj))), name)) {
      return car(obj);
    }
  }
  /* create new object, push to symlist and return */
  obj = object(ctx);
  settype(obj, FE_TSYMBOL);
  cdr(obj) = fe_cons(ctx, fe_string(ctx, name, strlen(name)), &nil);
  ctx->symlist = fe_cons(ctx, obj, ctx->symlist);
  return obj;
}


fe_Object* fe_cfunc(fe_Context *ctx, fe_CFunc fn) {
  fe_Object *obj = object(ctx);
  settype(obj, FE_TCFUNC);
  cfunc(obj) = fn;
  return obj;
}


fe_Object* fe_ptr(fe_Context *ctx, void *ptr) {
  fe_Object *obj = object(ctx);
  settype(obj, FE_TPTR);
  cdr(obj) = ptr;
  return obj;
}


fe_Object* fe_list(fe_Context *ctx, fe_Object **objs, int n) {
  fe_Object *res = &nil;
  while (n--) {
    res = fe_cons(ctx, objs[n], res);
  }
  return res;
}


fe_Object* fe_car(fe_Context *ctx, fe_Object *obj) {
  if (isnil(obj)) { return obj; }
  return car(checktype(ctx, obj, FE_TPAIR));
}


fe_Object* fe_cdr(fe_Context *ctx, fe_Object *obj) {
  if (isnil(obj)) { return obj; }
  return cdr(checktype(ctx, obj, FE_TPAIR));
}

fe_Object** fe_cdr_ptr(fe_Context *ctx, fe_Object *obj) {
  if (isnil(obj)) { fe_error(ctx, "cannot get cdr pointer of nil"); }
  return &cdr(checktype(ctx, obj, FE_TPAIR));
}

static void writestr(fe_Context *ctx, fe_WriteFn fn, void *udata, const char *s) {
  while (*s) { fn(ctx, udata, *s++); }
}

static void write_hex_byte(fe_Context *ctx, fe_WriteFn fn, void *udata, unsigned char byte) {
  static const char hexdigits[] = "0123456789abcdef";
  fn(ctx, udata, hexdigits[(byte >> 4) & 0x0f]);
  fn(ctx, udata, hexdigits[byte & 0x0f]);
}

void fe_write(fe_Context *ctx, fe_Object *obj, fe_WriteFn fn, void *udata, int qt) {
  char buf[32];

  switch (type(obj)) {
    case FE_TNIL:
      writestr(ctx, fn, udata, "nil");
      break;

    case FE_TBOOLEAN:
      writestr(ctx, fn, udata, (obj == FE_TRUE) ? "true" : "false");
      break;

    case FE_TNUMBER:
      if (FE_IS_FIXNUM(obj)) {
          sprintf(buf, "%lld", (intmax_t)FE_UNBOX_FIXNUM(obj));
      } else {
          sprintf(buf, "%.7g", number(obj));
      }
      writestr(ctx, fn, udata, buf);
      break;

    case FE_TPAIR:
      if (car(obj) == frame_sym) {
        writestr(ctx, fn, udata, "[env frame]");
        break;
      }

      fn(ctx, udata, '(');
      for (;;) {
        fe_write(ctx, car(obj), fn, udata, 1);
        obj = cdr(obj);
        if (type(obj) != FE_TPAIR) { break; }
        fn(ctx, udata, ' ');
      }
      if (!isnil(obj)) {
        writestr(ctx, fn, udata, " . ");
        fe_write(ctx, obj, fn, udata, 1);
      }
      fn(ctx, udata, ')');
      break;

    case FE_TSYMBOL:
      fe_write(ctx, car(cdr(obj)), fn, udata, 0);
      break;

    case FE_TSTRING:
      if (qt) fn(ctx, udata, '"');
#ifdef FE_OPT_NO_MALLOC_STRINGS
      {
          size_t len = FE_STR_LEN(obj);
          if (len > 0) {
              uint32_t offset = obj->cdr.u32;
              size_t remaining = len;
              while (offset != FE_SLAB_NULL && remaining > 0) {
                  fe_Slab *slab = (fe_Slab*)(ctx->str_base + offset);
                  size_t to_write = (remaining > FE_SLAB_DATA_SIZE) ? FE_SLAB_DATA_SIZE : remaining;
                  size_t i;
                  for (i = 0; i < to_write; i++) {
                      char c = slab->data[i];
                      if (qt && c == '"') fn(ctx, udata, '\\');
                      fn(ctx, udata, c);
                  }
                  remaining -= to_write;
                  offset = slab->next;
              }
          }
      }
#else
      {
          const char *p = FE_STR_DATA(ctx, obj);
          while (*p) {
              if (qt && *p=='"') fn(ctx, udata, '\\');
              fn(ctx, udata, *p++);
          }
      }
#endif
      if (qt) fn(ctx, udata, '"');
      break;

    case FE_TBYTES:
      writestr(ctx, fn, udata, "#bytes[");
#ifdef FE_OPT_NO_MALLOC_STRINGS
      {
          size_t len = FE_STR_LEN(obj);
          if (len > 0) {
              uint32_t offset = obj->cdr.u32;
              size_t remaining = len;
              int first = 1;
              while (offset != FE_SLAB_NULL && remaining > 0) {
                  fe_Slab *slab = (fe_Slab*)(ctx->str_base + offset);
                  size_t to_write = (remaining > FE_SLAB_DATA_SIZE) ? FE_SLAB_DATA_SIZE : remaining;
                  size_t i;
                  for (i = 0; i < to_write; i++) {
                      if (!first) fn(ctx, udata, ' ');
                      write_hex_byte(ctx, fn, udata, (unsigned char)slab->data[i]);
                      first = 0;
                  }
                  remaining -= to_write;
                  offset = slab->next;
              }
          }
      }
#else
      {
          size_t len = FE_STR_LEN(obj);
          size_t i;
          const unsigned char *p = (const unsigned char*)FE_STR_DATA(ctx, obj);
          for (i = 0; i < len; i++) {
              if (i > 0) fn(ctx, udata, ' ');
              write_hex_byte(ctx, fn, udata, p[i]);
          }
      }
#endif
      fn(ctx, udata, ']');
      break;

    case FE_TMAP: {
      fe_Map *map = mapdata(obj);
      int i;
      int first = 1;
      fn(ctx, udata, '{');
      if (map) {
        for (i = 0; i < map->capacity; i++) {
          if (map->states[i] != MAP_USED) {
            continue;
          }
          if (!first) {
            writestr(ctx, fn, udata, ", ");
          }
          fe_write(ctx, map->keys[i], fn, udata, 1);
          writestr(ctx, fn, udata, ": ");
          fe_write(ctx, map->values[i], fn, udata, 1);
          first = 0;
        }
      }
      fn(ctx, udata, '}');
      break;
    }

    default:
      sprintf(buf, "[%s %p]", typenames[type(obj)], (void*) obj);
      writestr(ctx, fn, udata, buf);
      break;
  }
}


static void writefp(fe_Context *ctx, void *udata, char chr) {
  unused(ctx);
  fputc(chr, udata);
}

void fe_writefp(fe_Context *ctx, fe_Object *obj, FILE *fp) {
  fe_write(ctx, obj, writefp, fp, 0);
}


typedef struct { char *p; int n; } CharPtrInt;

static void writebuf(fe_Context *ctx, void *udata, char chr) {
  CharPtrInt *x = udata;
  unused(ctx);
  if (x->n) { *x->p++ = chr; x->n--; }
}

int fe_tostring(fe_Context *ctx, fe_Object *obj, char *dst, int size) {
  CharPtrInt x;
  x.p = dst;
  x.n = size - 1;
  fe_write(ctx, obj, writebuf, &x, 0);
  *x.p = '\0';
  return size - x.n - 1;
}

size_t fe_strlen(fe_Context *ctx, fe_Object *obj)
{
  /* caller must ensure obj is a string */
#ifdef FE_OPT_NO_MALLOC_STRINGS
  unused(ctx);
  return FE_STR_LEN(obj);
#else
  return strlen(FE_STR_DATA(ctx, obj));
#endif
}

size_t fe_byteslen(fe_Context *ctx, fe_Object *obj)
{
  unused(ctx);
  return FE_STR_LEN(checktype(ctx, obj, FE_TBYTES));
}

size_t fe_bytescopy(fe_Context *ctx, fe_Object *obj, size_t offset, void *dst, size_t size)
{
  size_t len;
  unsigned char *out;

  obj = checktype(ctx, obj, FE_TBYTES);
  len = FE_STR_LEN(obj);
  if (offset >= len || size == 0) {
    return 0;
  }
  if (size > len - offset) {
    size = len - offset;
  }
  out = (unsigned char*)dst;

#ifdef FE_OPT_NO_MALLOC_STRINGS
  {
    uint32_t slab_offset = obj->cdr.u32;
    size_t remaining_offset = offset;
    size_t remaining = size;

    while (slab_offset != FE_SLAB_NULL && remaining > 0) {
      fe_Slab *slab = (fe_Slab*)(ctx->str_base + slab_offset);
      size_t slab_start = 0;
      size_t available;
      size_t to_copy;

      if (remaining_offset >= FE_SLAB_DATA_SIZE) {
        remaining_offset -= FE_SLAB_DATA_SIZE;
        slab_offset = slab->next;
        continue;
      }
      slab_start = remaining_offset;
      available = FE_SLAB_DATA_SIZE - slab_start;
      if (available > remaining) {
        available = remaining;
      }
      to_copy = available;
      memcpy(out, slab->data + slab_start, to_copy);
      out += to_copy;
      remaining -= to_copy;
      remaining_offset = 0;
      slab_offset = slab->next;
    }
  }
#else
  memcpy(out, FE_STR_DATA(ctx, obj) + offset, size);
#endif
  return size;
}

fe_Number fe_tonumber(fe_Context *ctx, fe_Object *obj) {
    unused(ctx);
    return nval(obj);      /* works for both representations */
}


void* fe_toptr(fe_Context *ctx, fe_Object *obj) {
  return cdr(checktype(ctx, obj, FE_TPTR));
}

static int find_assoc_binding(fe_Object *sym, fe_Object *env, fe_Object **binding) {
  fe_Object *p;
  *binding = NULL;
  if (type(env) != FE_TPAIR) { return 0; }
  for (p = env; !isnil(p); p = cdr(p)) {
    fe_Object *entry;
    if (type(p) != FE_TPAIR) { return 0; }
    entry = car(p);
    if (type(entry) != FE_TPAIR) { return 0; }
    if (car(entry) == sym) {
      *binding = entry;
      return 1;
    }
  }
  return 1;
}

static fe_Object* getbound(fe_Object *sym, fe_Object *env) {
  fe_Object *p;
  /* Check for new closure environment frame */
  if (type(env) == FE_TPAIR && car(env) == frame_sym) {
    fe_Object *locals = car(cdr(env));
    fe_Object *upvals = cdr(cdr(env));
    /* search locals */
    for (p = locals; !isnil(p); p = cdr(p)) {
      fe_Object *x = car(p);
      if (car(x) == sym) { return x; }
    }
    /* search upvalues */
    for (p = upvals; !isnil(p); p = cdr(p)) {
      fe_Object *x = car(p);
      if (car(x) == sym) { return x; }
    }
    /* not found in frame, fall through to globals */
  } else {
    /* try to find in old-style association list environment */
    for (; !isnil(env); env = cdr(env)) {
      fe_Object *x = car(env);
      if (car(x) == sym) { return x; }
    }
  }
  /* return global */
  return cdr(sym);
}


void fe_set(fe_Context *ctx, fe_Object *sym, fe_Object *v) {
  unused(ctx);
  cdr(getbound(sym, &nil)) = v;
}


static fe_Object rparen;

static fe_Object* read_(fe_Context *ctx, fe_ReadFn fn, void *udata) {
  const char *delimiter = " \n\t\r();";
  fe_Object *v, *res, **tail;
  fe_Number n;
  int chr, gc;
  char buf[64], *p;

  /* get next character */
  chr = ctx->nextchr ? ctx->nextchr : fn(ctx, udata);
  ctx->nextchr = '\0';

  /* skip whitespace */
  while (chr && strchr(" \n\t\r", chr)) {
    chr = fn(ctx, udata);
  }

  switch (chr) {
    case '\0':
      return NULL;

    case ';':
      while (chr && chr != '\n') { chr = fn(ctx, udata); }
      return read_(ctx, fn, udata);

    case ')':
      return &rparen;

    case '(':
      res = &nil;
      tail = &res;
      gc = fe_savegc(ctx);
      fe_pushgc(ctx, res); /* to cause error on too-deep nesting */
      while ( (v = read_(ctx, fn, udata)) != &rparen ) {
        if (v == NULL) { fe_error(ctx, "unclosed list"); }
        if (type(v) == FE_TSYMBOL && streq(ctx, car(cdr(v)), ".")) {
          /* dotted pair */
          *tail = fe_read(ctx, fn, udata);
        } else {
          /* proper pair */
          *tail = fe_cons(ctx, v, &nil);
          tail = &cdr(*tail);
        }
        fe_restoregc(ctx, gc);
        fe_pushgc(ctx, res);
      }
      return res;

    case '\'':
      v = fe_read(ctx, fn, udata);
      if (!v) { fe_error(ctx, "stray '''"); }
      return fe_cons(ctx, fe_symbol(ctx, "quote"), fe_cons(ctx, v, &nil));

    case '"':
      {
#ifdef FE_OPT_NO_MALLOC_STRINGS
#define FE_MAX_LITERAL_SZ 256
        char s_buf[FE_MAX_LITERAL_SZ];
        size_t len = 0;
        chr = fn(ctx, udata);
        while (chr != '"') {
            if (chr == '\0') fe_error(ctx, "unclosed string");
            if (chr == '\\') {
                chr = fn(ctx, udata);
                if (chr == 'n') chr = '\n';
                else if (chr == 'r') chr = '\r';
                else if (chr == 't') chr = '\t';
            }
            if (len >= FE_MAX_LITERAL_SZ - 1) {
                fe_error(ctx, "string literal too long");
            }
            s_buf[len++] = chr;
            chr = fn(ctx, udata);
        }
        return make_data_obj(ctx, FE_TSTRING, s_buf, len, 0);
#else
        size_t cap = GROW_STEP, len = 0;
        char *s_buf = tracked_alloc(ctx, cap);
        if (!s_buf) memory_error(ctx, "out of memory (string)");

        chr = fn(ctx, udata);
        while (chr!='"') {
          if (chr=='\0') { tracked_free(ctx, s_buf); fe_error(ctx, "unclosed string"); }
          if (chr=='\\') {
              chr = fn(ctx, udata);
              if (chr=='n') chr='\n';
              else if (chr=='r') chr='\r';
              else if (chr=='t') chr='\t';
          }
          if (len+1>=cap) {
            char *grown;
            cap += GROW_STEP;
            grown = tracked_realloc(ctx, s_buf, cap);
            if (!grown) {
              tracked_free(ctx, s_buf);
              memory_error(ctx, "out of memory (string)");
            }
            s_buf = grown;
          }
          s_buf[len++]=chr;
          chr = fn(ctx, udata);
        }
        fe_Object* str_obj = make_data_obj(ctx, FE_TSTRING, s_buf, len, 0);
        tracked_free(ctx, s_buf);
        return str_obj;
#endif
      }

    default:
      p = buf;
      do {
        if (p == buf + sizeof(buf) - 1) { fe_error(ctx, "symbol too long"); }
        *p++ = chr;
        chr = fn(ctx, udata);
      } while (chr && !strchr(delimiter, chr));
      *p = '\0';
      ctx->nextchr = chr;
      n = strtod(buf, &p);  /* try to read as number */
      if (p != buf && strchr(delimiter, *p)) {
        return fe_make_number(ctx, n);
      }
      if (!strcmp(buf, "nil"))   { return &nil;  }
      if (!strcmp(buf, "true"))  { return FE_TRUE;  }
      if (!strcmp(buf, "false")) { return FE_FALSE; }
      return fe_symbol(ctx, buf);
  }
}


fe_Object* fe_read(fe_Context *ctx, fe_ReadFn fn, void *udata) {
  fe_Object* obj = read_(ctx, fn, udata);
  if (obj == &rparen) { fe_error(ctx, "stray ')'"); }
  return obj;
}


static char readfp(fe_Context *ctx, void *udata) {
  int chr;
  unused(ctx);
  return (chr = fgetc(udata)) == EOF ? '\0' : chr;
}

fe_Object* fe_readfp(fe_Context *ctx, FILE *fp) {
  return fe_read(ctx, readfp, fp);
}


static fe_Object* eval(fe_Context *ctx, fe_Object *obj, fe_Object *env, fe_Object **newenv);

static fe_Object* evallist(fe_Context *ctx, fe_Object *lst, fe_Object *env) {
  fe_Object *res = &nil;
  fe_Object **tail = &res;
  while (!isnil(lst)) {
    *tail = fe_cons(ctx, eval(ctx, fe_nextarg(ctx, &lst), env, NULL), &nil);
    tail = &cdr(*tail);
  }
  return res;
}


static fe_Object* dolist(fe_Context *ctx, fe_Object *lst, fe_Object *env) {
  fe_Object *res = &nil;
  int save = fe_savegc(ctx);
  while (!isnil(lst)) {
    fe_restoregc(ctx, save);
    fe_pushgc(ctx, lst);
    fe_pushgc(ctx, env);
    res = eval(ctx, fe_nextarg(ctx, &lst), env, &env);
    if (is_return_obj(res)) { break; }
  }
  return res;
}


static fe_Object* argstoenv(fe_Context *ctx, fe_Object *prm, fe_Object *arg, fe_Object *env) {
  while (!isnil(prm)) {
    if (type(prm) != FE_TPAIR) {
      env = fe_cons(ctx, fe_cons(ctx, prm, arg), env);
      break;
    }
    env = fe_cons(ctx, fe_cons(ctx, car(prm), fe_car(ctx, arg)), env);
    prm = cdr(prm);
    arg = fe_cdr(ctx, arg);
  }
  return env;
}


#define evalarg() eval(ctx, fe_nextarg(ctx, &arg), env, NULL)

#define arithop(op) {                                     \
    fe_Number x = nval(checknum(ctx, evalarg()));         \
    while (!isnil(arg)) {                                 \
        x = x op nval(checknum(ctx, evalarg()));          \
    }                                                     \
    res = fe_make_number(ctx, x);                         \
}

#define numcmpop(op) {                                    \
    va  = checknum(ctx, evalarg());                       \
    vb  = checknum(ctx, evalarg());                       \
    res = fe_bool(ctx, nval(va) op nval(vb));             \
}


static fe_Object* import_module(fe_Context *ctx, fe_Object *sym) {
  char module_name[128];
  char error_buf[2048];
  char *module_path;
  char *searched_paths = NULL;
  fe_Object *result;
  size_t error_len;

  checktype(ctx, sym, FE_TSYMBOL);
  fe_tostring(ctx, sym, module_name, sizeof(module_name));
  result = cdr(getbound(sym, &nil));
  if (type(result) == FE_TMAP) {
    return result;
  }

  module_path = resolve_module_file(ctx, module_name, &searched_paths);
  if (!module_path) {
    if (searched_paths && ctx->alloc_failure_active) {
      tracked_free(ctx, searched_paths);
    }
    if (ctx->alloc_failure_active) {
      memory_error(ctx, "out of memory (module path)");
    }
    sprintf(error_buf, "could not resolve module '%s'", module_name);
    if (searched_paths && *searched_paths) {
      static const char searched_label[] = "\nsearched:\n";
      size_t remaining;
      error_len = strlen(error_buf);
      remaining = sizeof(error_buf) - error_len - 1;
      if (remaining > 0) {
        size_t label_len = sizeof(searched_label) - 1;
        size_t copy_len;
        if (label_len > remaining) label_len = remaining;
        memcpy(error_buf + error_len, searched_label, label_len);
        error_len += label_len;
        remaining = sizeof(error_buf) - error_len - 1;
        copy_len = strlen(searched_paths);
        if (copy_len > remaining) copy_len = remaining;
        memcpy(error_buf + error_len, searched_paths, copy_len);
        error_len += copy_len;
        error_buf[error_len] = '\0';
      }
    }
    tracked_free(ctx, searched_paths);
    fe_error(ctx, error_buf);
  }

  if (string_array_contains(ctx->loaded_modules, ctx->loaded_module_count, module_path)) {
    tracked_free(ctx, module_path);
    return cdr(getbound(sym, &nil));
  }

  if (string_array_contains(ctx->loading_modules, ctx->loading_module_count, module_path)) {
    tracked_free(ctx, module_path);
    sprintf(error_buf, "cyclic import detected for module '%s'", module_name);
    fe_error(ctx, error_buf);
  }

  if (!string_array_push_copy(ctx, &ctx->loading_modules, &ctx->loading_module_count,
                              &ctx->loading_module_capacity, module_path)) {
    tracked_free(ctx, module_path);
    memory_error(ctx, "out of memory (import state)");
  }

  result = fex_do_file(ctx, module_path);

  if (result == NULL) {
    string_array_pop(ctx, ctx->loading_modules, &ctx->loading_module_count);
    tracked_free(ctx, module_path);
    fe_error(ctx, "failed to import module");
  }

  string_array_pop(ctx, ctx->loading_modules, &ctx->loading_module_count);
  if (!string_array_contains(ctx->loaded_modules, ctx->loaded_module_count, module_path)) {
    if (!string_array_push_owned(ctx, &ctx->loaded_modules, &ctx->loaded_module_count,
                                 &ctx->loaded_module_capacity, module_path)) {
      tracked_free(ctx, module_path);
      memory_error(ctx, "out of memory (module cache)");
    }
    module_path = NULL;
  }
  tracked_free(ctx, module_path);

  return cdr(getbound(sym, &nil));
}


static fe_Object* eval(fe_Context *ctx, fe_Object *obj, fe_Object *env, fe_Object **newenv) {
  fe_Object *fn, *arg, *res;
  fe_Object cl, *va, *vb;
  int n, gc;

  check_eval_budget(ctx);
  if (type(obj) == FE_TSYMBOL) { return cdr(getbound(obj, env)); }
  if (type(obj) != FE_TPAIR) { return obj; }

  /* Call stack frames are synthetic pair cells that live on the C stack. */
  tag(&cl) = 0;
  car(&cl) = obj, cdr(&cl) = ctx->calllist;
  ctx->calllist = &cl;

  gc = fe_savegc(ctx);
  fn = eval(ctx, car(obj), env, NULL);
  arg = cdr(obj);
  res = &nil;

  switch (type(fn)) {
    case FE_TPRIM:
      switch (prim(fn)) {
        case P_MODULE: {
          /* form: (module "name" body) */
          fe_Object *name_obj = evalarg();
          fe_Object *body = fe_nextarg(ctx, &arg);
          char name_buf[128];

          /* Create and push module's export table */
          fe_Object *exports = fe_map(ctx);
          fe_pushgc(ctx, exports);
          ctx->modulestack = fe_cons(ctx, exports, ctx->modulestack);

          /* Evaluate module body */
          eval(ctx, body, env, &env);

          /* Pop module from stack and retrieve final exports table */
          exports = car(ctx->modulestack);
          ctx->modulestack = cdr(ctx->modulestack);

          /* Register module in global environment */
          checktype(ctx, name_obj, FE_TSTRING);
          fe_tostring(ctx, name_obj, name_buf, sizeof(name_buf));
          fe_set(ctx, fe_symbol(ctx, name_buf), exports);
          res = exports;
          break;
        }
        case P_EXPORT: {
          /* form: (export (let name value)) */
          if (isnil(ctx->modulestack)) fe_error(ctx, "export outside of module");

          fe_Object *decl = fe_nextarg(ctx, &arg);
          fe_Object *name_sym = fe_car(ctx, fe_cdr(ctx, decl));
          fe_Object *exports = fe_car(ctx, ctx->modulestack);
          checktype(ctx, name_sym, FE_TSYMBOL);

          /* Evaluate declaration to bind it and get value */
          res = eval(ctx, decl, env, &env);

          /* Add to current module's export table */
          fe_map_set(ctx, exports, name_sym, res);
          break;
        }
        case P_IMPORT:
          /* form: (import name) */
          vb = fe_nextarg(ctx, &arg); /* module symbol, not evaluated */
          res = import_module(ctx, vb);
          break;
        case P_GET: {
          /* form: (get object property) */
          va = evalarg(); /* The module object (or any table) */
          vb = fe_nextarg(ctx, &arg); /* The property symbol (not evaluated) */
          {
            fe_Object *binding = NULL;
            int is_table;

            checktype(ctx, vb, FE_TSYMBOL);
            if (type(va) == FE_TMAP) {
              res = fe_map_get(ctx, va, vb);
              break;
            }
            is_table = find_assoc_binding(vb, va, &binding);
            if (binding != NULL) {
              res = cdr(binding);
              break;
            }
            if (!is_table && type(va)==FE_TPAIR) {
              if (fe_symbol_name_eq(ctx, vb, "head") ||
                  fe_symbol_name_eq(ctx, vb, "first"))
                  { res = car(va); break; }
              if (fe_symbol_name_eq(ctx, vb, "tail") ||
                  fe_symbol_name_eq(ctx, vb, "rest"))
                  { res = cdr(va); break; }
              fe_error(ctx, "Only .head, .first, .tail, and .rest are valid on pairs");
            }
            if (is_table) {
              res = &nil;
              break;
            }
          }
          res = cdr(getbound(vb, va)); /* fallback: Re-use getbound for assoc list lookup */
          break;
        }
        case P_PUT:
          /* form: (put object property value) */
          va = evalarg();
          vb = fe_nextarg(ctx, &arg);
          res = evalarg();
          fe_map_set(ctx, va, vb, res);
          break;
        case P_RETURN: {
            /* evaluate argument, defaulting to nil */
            va  = isnil(arg) ? &nil : evalarg();
            /* (__return__ . value) - single pair keeps GC simple */
            res = fe_cons(ctx, return_sym, va);
            break;
        }
        case P_LET: {
          fe_Object *sym = checktype(ctx, fe_nextarg(ctx, &arg), FE_TSYMBOL);
          fe_Object *val_expr = fe_nextarg(ctx, &arg);
          fe_Object *val;

          if (newenv) {
            /* Implement `letrec` semantics for local bindings to allow recursion.
               A new binding is created with a nil placeholder, the value is evaluated
               in this new environment, and then the binding is updated. */
            fe_Object *binding = fe_cons(ctx, sym, &nil);

            fe_Object *new_frame_env;
            if (type(*newenv) == FE_TPAIR && car(*newenv) == frame_sym) {
                fe_Object *locals = car(cdr(*newenv));
                fe_Object *upvals = cdr(cdr(*newenv));
                fe_Object *new_locals = fe_cons(ctx, binding, locals);
                new_frame_env = fe_cons(ctx, new_locals, upvals);
                new_frame_env = fe_cons(ctx, frame_sym, new_frame_env);
            } else {
                new_frame_env = fe_cons(ctx, binding, *newenv);
            }
            *newenv = new_frame_env;

            val = eval(ctx, val_expr, *newenv, NULL);
            cdr(binding) = val;
          } else {
            /* This case handles top-level 'let' in the REPL.
               We'll make it a global binding. This simple approach lacks letrec
               semantics for the REPL, but is better than doing nothing. */
            val = eval(ctx, val_expr, env, NULL);
            fe_set(ctx, sym, val);
          }
          /* A `let` expression should evaluate to the assigned value. */
          res = val;
          break;
        }

        case P_SET:
          va = checktype(ctx, fe_nextarg(ctx, &arg), FE_TSYMBOL);
          cdr(getbound(va, env)) = evalarg();
          break;

        case P_IF:
          while (!isnil(arg)) {
            va = evalarg();
            if (fe_truthy(va)) {
              res = isnil(arg) ? va : evalarg();
              break;
            }
            if (isnil(arg)) { break; }
            arg = cdr(arg);
          }
          break;

        case P_FN: case P_MAC: {
          fe_Object *params = fe_nextarg(ctx, &arg);
          fe_Object *body = fe_car(ctx, arg);

          int s = fe_savegc(ctx);
          fe_Object *bound = &nil;
          fe_Object *p;
          fe_Object *free_vars;
          fe_pushgc(ctx, bound);
          for (p = params; !isnil(p); p = cdr(p)) {
              bound = fe_cons(ctx, car(p), bound);
          }

          free_vars = &nil;
          fe_pushgc(ctx, free_vars);
          analyze(ctx, body, bound, &free_vars);
          fe_restoregc(ctx, s);

          fe_pushgc(ctx, free_vars);
          fe_pushgc(ctx, params);
          fe_pushgc(ctx, body);
          fe_pushgc(ctx, env); /* Protect the definition-time environment */

          va = fe_cons(ctx, body, &nil);
          va = fe_cons(ctx, params, va);
          va = fe_cons(ctx, free_vars, va);
          va = fe_cons(ctx, env, va); /* Prepend definition env to the list */

          res = object(ctx);
          settype(res, prim(fn) == P_FN ? FE_TFUNC : FE_TMACRO);
          cdr(res) = va;
          break;
        }

        case P_WHILE:
          va = fe_nextarg(ctx, &arg);
          n = fe_savegc(ctx);
          while (fe_truthy(eval(ctx, va, env, NULL))) {
            dolist(ctx, arg, env);
            fe_restoregc(ctx, n);
          }
          break;

        case P_QUOTE:
          res = fe_nextarg(ctx, &arg);
          break;

        case P_AND:
          while (!isnil(arg) && fe_truthy(res = evalarg()));
          break;

        case P_OR:
          while (!isnil(arg) && !fe_truthy(res = evalarg()));
          break;

        case P_DO:
          res = dolist(ctx, arg, env);
          break;

        case P_CONS:
          va = evalarg();
          res = fe_cons(ctx, va, evalarg());
          break;

        case P_CAR:
          res = fe_car(ctx, evalarg());
          break;

        case P_CDR:
          res = fe_cdr(ctx, evalarg());
          break;

        case P_SETCAR:
          va = checktype(ctx, evalarg(), FE_TPAIR);
          car(va) = evalarg();
          break;

        case P_SETCDR:
          va = checktype(ctx, evalarg(), FE_TPAIR);
          cdr(va) = evalarg();
          break;

        case P_LIST:
          res = evallist(ctx, arg, env);
          break;

        case P_NOT:
          res = fe_bool(ctx, !fe_truthy(evalarg()));
          break;

        case P_IS:
          va = evalarg();
          res = fe_bool(ctx, equal(ctx, va, evalarg()));
          break;

        case P_ATOM:
          res = fe_bool(ctx, fe_type(ctx, evalarg()) != FE_TPAIR);
          break;

        case P_PRINT:
          while (!isnil(arg)) {
            fe_writefp(ctx, evalarg(), stdout);
            if (!isnil(arg)) { printf(" "); }
          }
          printf("\n");
          break;

        case P_LT: numcmpop(<); break;
        case P_LTE: numcmpop(<=); break;
        case P_ADD: arithop(+); break;
        case P_SUB:
          /* --------  subtraction / unary minus -------- */
          if (isnil(arg)) {                 /* (-) -> 0 (Scheme behavior) */
              res = fe_make_number(ctx, 0);
          } else {
              /* first operand */
              fe_Number x = nval(checknum(ctx, evalarg()));

              if (isnil(arg)) {             /* unary: (- x) -> -x */
                  res = fe_make_number(ctx, -x);
              } else {                      /* n-ary: (- x y z ...) */
                  while (!isnil(arg)) {
                      x -= nval(checknum(ctx, evalarg()));
                  }
                  res = fe_make_number(ctx, x);
              }
          }
          break;
        case P_MUL: arithop(*); break;
        case P_DIV: arithop(/); break;
      }
      break;

    case FE_TCFUNC:
      res = cfunc(fn)(ctx, evallist(ctx, arg, env));
      break;

    case FE_TFUNC: {
      arg = evallist(ctx, arg, env);

      fe_Object *fn_guts = cdr(fn);         /* (def_env free_vars params . body) */
      fe_Object *def_env = car(fn_guts);
      fn_guts = cdr(fn_guts);
      fe_Object *free_vars = car(fn_guts);
      fe_Object *params_body = cdr(fn_guts);
      fe_Object *params = car(params_body);
      fe_Object *body = cdr(params_body);
      fe_Object *p;

      /* Create upvalue list (the closure's captured environment) */
      fe_Object *upvals = &nil;
      int s = fe_savegc(ctx);
      fe_pushgc(ctx, upvals);
      fe_pushgc(ctx, def_env);
      for (p = free_vars; !isnil(p); p = cdr(p)) {
        fe_Object *sym = car(p);
        fe_Object *binding = getbound(sym, def_env); /* Use the captured definition env */
        upvals = fe_cons(ctx, binding, upvals);
      }
      fe_restoregc(ctx, s);

      fe_pushgc(ctx, upvals);
      fe_pushgc(ctx, arg);

      /* Create local environment */
      fe_Object *locals = argstoenv(ctx, params, arg, &nil);

      /* Create new frame: (frame_sym . (locals . upvals)) */
      fe_Object *frame = fe_cons(ctx, locals, upvals);
      frame = fe_cons(ctx, frame_sym, frame);

      res = dolist(ctx, body, frame);
      if (is_return_obj(res)) {
          res = cdr(res);
      }
      break;
    }

    case FE_TMACRO: {
      fe_Object *fn_guts = cdr(fn);         /* (def_env free_vars params . body) */
      fe_Object *def_env = car(fn_guts);
      fn_guts = cdr(fn_guts);
      fe_Object *free_vars = car(fn_guts);
      fe_Object *params_body = cdr(fn_guts);
      fe_Object *params = car(params_body);
      fe_Object *body = cdr(params_body);
      fe_Object *p;

      fe_Object *upvals = &nil;
      int s = fe_savegc(ctx);
      fe_pushgc(ctx, upvals);
      fe_pushgc(ctx, def_env);
      for (p = free_vars; !isnil(p); p = cdr(p)) {
        fe_Object *sym = car(p);
        fe_Object *binding = getbound(sym, def_env);
        upvals = fe_cons(ctx, binding, upvals);
      }
      fe_restoregc(ctx, s);

      fe_Object *locals = argstoenv(ctx, params, arg, &nil);
      fe_Object *frame = fe_cons(ctx, locals, upvals);
      frame = fe_cons(ctx, frame_sym, frame);

      *obj = *dolist(ctx, body, frame);
      fe_restoregc(ctx, gc);
      ctx->calllist = cdr(&cl);
      return eval(ctx, obj, env, NULL);
    }

    default:
      fe_error(ctx, "tried to call non-callable value");
  }

  fe_restoregc(ctx, gc);
  fe_pushgc(ctx, res);
  ctx->calllist = cdr(&cl);
  return res;
}


fe_Object* fe_eval(fe_Context *ctx, fe_Object *obj) {
  fe_Object *res;
  begin_eval_run(ctx);
  res = eval(ctx, obj, &nil, NULL);
  end_eval_run(ctx);
  return res;
}


fe_Context* fe_open(void *ptr, size_t size) {
  int i, save;
  fe_Context *ctx;
  size_t object_capacity;
  size_t total_mem = size;

  /* init context struct */
  if (total_mem < sizeof(fe_Context)) return NULL;
  ctx = ptr;
  memset(ctx, 0, sizeof(fe_Context));
  ctx->base_memory_bytes = total_mem;
  ctx->memory_limit = 0;
  ctx->memory_used = total_mem;
  ctx->peak_memory_used = total_mem;
  ctx->alloc_failure_active = 0;
  ctx->alloc_failure_is_limit = 0;

  void* arenas_ptr = (char*) ptr + sizeof(fe_Context);
  size_t arenas_sz = total_mem - sizeof(fe_Context);

#ifdef FE_OPT_NO_MALLOC_STRINGS
  /* Partition memory: [Object Arena][String Arena] */
  size_t str_arena_sz = (size_t)(arenas_sz * FE_STR_ARENA_RATIO);
  str_arena_sz -= str_arena_sz % sizeof(void*); /* Align */
  size_t obj_arena_sz = arenas_sz - str_arena_sz;

  object_capacity = obj_arena_sz / sizeof(fe_Object);
  if (object_capacity > (size_t)INT_MAX) return NULL;
  ctx->object_count = (int)object_capacity;
  ctx->objects = (fe_Object*)arenas_ptr;

  ctx->str_base = (uint8_t*)ctx->objects + ctx->object_count * sizeof(fe_Object);
  ctx->str_end = ctx->str_base + str_arena_sz;
  ctx->str_freelist = FE_SLAB_NULL;

  /* Populate string slab freelist */
  uint8_t *slab_ptr = ctx->str_base;
  while (slab_ptr + FE_SLAB_SIZE <= ctx->str_end) {
      fe_Slab *slab = (fe_Slab*)slab_ptr;
      slab->next = ctx->str_freelist;
      ctx->str_freelist = (uint32_t)(slab_ptr - ctx->str_base);
      slab_ptr += FE_SLAB_SIZE;
  }
#else
  /* init objects memory region */
  ctx->objects = (fe_Object*) arenas_ptr;
  object_capacity = arenas_sz / sizeof(fe_Object);
  if (object_capacity > (size_t)INT_MAX) return NULL;
  ctx->object_count = (int)object_capacity;
#endif

  /* --- Initialize new GC state --- */
  ctx->live_count = 0;
  ctx->allocs_since_gc = 0;
  ctx->gc_threshold = (ctx->object_count / GC_INITIAL_DIVISOR);
  if (ctx->gc_threshold < GC_MIN_THRESHOLD) {
    ctx->gc_threshold = GC_MIN_THRESHOLD;
  }
  ctx->bytes_since_gc = 0;
  ctx->byte_threshold = (size_t)ctx->object_count * sizeof(fe_Object) / 3;
  ctx->gc_runs = 0;
  ctx->object_allocations_total = 0;

  /* init lists */
  ctx->calllist = &nil;
  ctx->freelist = &nil;
  ctx->modulestack = &nil;
  ctx->symlist = &nil;

  /* populate freelist */
  for (i = 0; i < ctx->object_count; i++) {
    fe_Object *obj = &ctx->objects[i];
    settype(obj, FE_TFREE);
    cdr(obj) = ctx->freelist;
    ctx->freelist = obj;
  }

  /* init objects */
  ctx->t = fe_symbol(ctx, "t");
  fe_set(ctx, ctx->t, ctx->t);

  /* register built in primitives */
  save = fe_savegc(ctx);
  for (i = 0; i < P_MAX; i++) {
    fe_Object *v = object(ctx);
    settype(v, FE_TPRIM);
    prim(v) = i;
    fe_set(ctx, fe_symbol(ctx, primnames[i]), v);
    fe_restoregc(ctx, save);
  }

  /* --- Initialize symbols for closures and analysis --- */
  return_sym = fe_symbol(ctx, "return");
  frame_sym = fe_symbol(ctx, "[frame]");
  do_sym = fe_symbol(ctx, "do");
  let_sym = fe_symbol(ctx, "let");
  quote_sym = fe_symbol(ctx, "quote");
  fn_sym = fe_symbol(ctx, "fn");
  mac_sym = fe_symbol(ctx, "mac");

  return ctx;
}


void fe_close(fe_Context *ctx) {
  /* clear gcstack and symlist; makes all objects unreachable */
  ctx->gcstack_idx = 0;
  ctx->symlist = &nil;
  collectgarbage(ctx);
  string_array_clear(ctx, &ctx->import_paths, &ctx->import_path_count, &ctx->import_path_capacity);
  string_array_clear(ctx, &ctx->source_dirs, &ctx->source_dir_count, &ctx->source_dir_capacity);
  string_array_clear(ctx, &ctx->source_buffers, &ctx->source_buffer_count, &ctx->source_buffer_capacity);
  string_array_clear(ctx, &ctx->loading_modules, &ctx->loading_module_count, &ctx->loading_module_capacity);
  string_array_clear(ctx, &ctx->loaded_modules, &ctx->loaded_module_count, &ctx->loaded_module_capacity);
}


#ifdef FE_STANDALONE

#include <setjmp.h>

static jmp_buf toplevel;
static char buf[64000];

static void onerror(fe_Context *ctx, const char *msg, fe_Object *cl) {
  unused(ctx), unused(cl);
  fprintf(stderr, "error: %s\n", msg);
  longjmp(toplevel, -1);
}


int main(int argc, char **argv) {
  int gc;
  fe_Object *obj;
  FILE *volatile fp = stdin;
  fe_Context *ctx = fe_open(buf, sizeof(buf));

  /* init input file */
  if (argc > 1) {
    fp = fopen(argv[1], "rb");
    if (!fp) { fe_error(ctx, "could not open input file"); }
  }

  if (fp == stdin) { fe_handlers(ctx)->error = onerror; }
  gc = fe_savegc(ctx);
  setjmp(toplevel);

  /* re(p)l */
  for (;;) {
    fe_restoregc(ctx, gc);
    if (fp == stdin) { printf("> "); }
    if (!(obj = fe_readfp(ctx, fp))) { break; }
    obj = fe_eval(ctx, obj);
    if (fp == stdin) { fe_writefp(ctx, obj, stdout); printf("\n"); }
  }

  return EXIT_SUCCESS;
}

#endif
