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
#include <wchar.h>
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

/* --- Recursion depth defaults --- */
#define FE_DEFAULT_MAX_EVAL_DEPTH  512
#define FE_DEFAULT_MAX_READ_DEPTH  512

/* --- GC constants --- */
#define GC_GROWTH_FACTOR 2
#define GC_INITIAL_DIVISOR 4
#define GC_MIN_THRESHOLD 1024
#define FE_IO_ABORT_CHECK_INTERVAL 64u


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
 P_LTE, P_GT, P_GTE, P_ADD, P_SUB, P_MUL, P_DIV, P_MAX
};

static const char *primnames[] = {
  "let", "=", "if", "fn", "mac", "while", "return",
  "module", "export", "import", "get", "put",
  "quote", "and", "or", "do", "cons",
  "car", "cdr", "setcar", "setcdr", "list", "not", "is", "atom", "print", "<",
  "<=", ">", ">=", "+", "-", "*", "/"
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
  int loaded_module_path_capacity;
  fe_Object **loaded_module_values;
  int loaded_module_count;
  int loaded_module_value_capacity;
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
  int current_eval_depth;
  int max_eval_depth;
  int current_read_depth;
  int max_read_depth;
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
  /* Per-context special symbols */
  fe_Object *return_sym;
  fe_Object *frame_sym;
  fe_Object *do_sym;
  fe_Object *let_sym;
  fe_Object *quote_sym;
  fe_Object *fn_sym;
  fe_Object *mac_sym;
  /* Per-context span state (opaque pointer to FexSpanState) */
  void *span_state;
  /* Per-context scratch temp allocations owned by fex.c */
  void *temp_allocs;
#ifdef FE_OPT_NO_MALLOC_STRINGS
  uint8_t *str_base;
  uint8_t *str_end;
  uint32_t str_freelist;   /* Offset of first free slab head */
#endif
};

static fe_Object nil = {{ NULL }, { NULL }, (FE_TNIL << 2 | 1)};

/* Special symbols are now stored per-context in fe_Context. */

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
  if (op == ctx->quote_sym) {
    return; /* Don't analyze contents of a quote */
  }

  if (op == ctx->do_sym) {
    fe_Object *local_bound = bound;
    int gc = fe_savegc(ctx);
    fe_pushgc(ctx, local_bound);

    for (p = args; !isnil(p); p = cdr(p)) {
      fe_Object *stmt = car(p);
      /* Check for (let var expr) */
      if (type(stmt) == FE_TPAIR && car(stmt) == ctx->let_sym) {
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

  if (op == ctx->fn_sym || op == ctx->mac_sym) {
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

void *fe_ctx_span_state(fe_Context *ctx) {
  return ctx->span_state;
}

void fe_ctx_set_span_state(fe_Context *ctx, void *state) {
  ctx->span_state = state;
}

void *fe_ctx_temp_allocs(fe_Context *ctx) {
  return ctx->temp_allocs;
}

void fe_ctx_set_temp_allocs(fe_Context *ctx, void *state) {
  ctx->temp_allocs = state;
}

void *fe_ctx_tracked_alloc(fe_Context *ctx, size_t size) {
  return tracked_alloc(ctx, size);
}

void *fe_ctx_tracked_realloc(fe_Context *ctx, void *ptr, size_t size) {
  return tracked_realloc(ctx, ptr, size);
}

void fe_ctx_tracked_free(fe_Context *ctx, void *ptr) {
  tracked_free(ctx, ptr);
}

void fe_ctx_memory_error(fe_Context *ctx, const char *fallback_msg) {
  memory_error(ctx, fallback_msg);
}

int fe_ctx_object_is_live(fe_Context *ctx, const fe_Object *obj) {
  if (!obj || FE_IS_FIXNUM(obj) || FE_IS_BOOLEAN(obj) || obj == &nil) {
    return 0;
  }
  if (obj < ctx->objects || obj >= ctx->objects + ctx->object_count) {
    return 0;
  }
  return type((fe_Object*)obj) != FE_TFREE;
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


void fe_set_eval_depth_limit(fe_Context *ctx, int max_depth) {
  ctx->max_eval_depth = max_depth;
}

int fe_get_eval_depth_limit(fe_Context *ctx) {
  return ctx->max_eval_depth;
}

void fe_set_read_depth_limit(fe_Context *ctx, int max_depth) {
  ctx->max_read_depth = max_depth;
}

int fe_get_read_depth_limit(fe_Context *ctx) {
  return ctx->max_read_depth;
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


static const char* poll_io_abort(fe_Context *ctx, size_t *countdown) {
  if (*countdown > 1) {
    (*countdown)--;
    return NULL;
  }
  *countdown = FE_IO_ABORT_CHECK_INTERVAL;
  return fe_poll_abort(ctx);
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

static int string_array_contains(fe_Context *ctx, char **items, int count, const char *value) {
  int i;
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;
  for (i = 0; i < count; i++) {
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
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

#ifdef _WIN32
static wchar_t* utf8_to_wide_tracked(fe_Context *ctx, const char *text) {
  int needed;
  wchar_t *wide;

  if (!text) return NULL;
  needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
  if (needed <= 0) return NULL;
  wide = tracked_alloc(ctx, (size_t)needed * sizeof(wchar_t));
  if (!wide) return NULL;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, needed) <= 0) {
    tracked_free(ctx, wide);
    return NULL;
  }
  return wide;
}

static char* wide_to_utf8_tracked(fe_Context *ctx, const wchar_t *text) {
  int needed;
  char *utf8;

  if (!text) return NULL;
  needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  if (needed <= 0) return NULL;
  utf8 = tracked_alloc(ctx, (size_t)needed);
  if (!utf8) return NULL;
  if (WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, needed, NULL, NULL) <= 0) {
    tracked_free(ctx, utf8);
    return NULL;
  }
  return utf8;
}
#endif

static int ensure_object_array_capacity(fe_Context *ctx, fe_Object ***items,
                                        int *capacity, int needed) {
  int new_capacity;
  fe_Object **new_items;
  if (needed <= *capacity && *items != NULL) return 1;
  new_capacity = (*capacity > 0) ? *capacity * 2 : 4;
  if (*items == NULL && *capacity >= needed) {
    new_capacity = *capacity;
  }
  while (new_capacity < needed) {
    new_capacity *= 2;
  }
  new_items = tracked_realloc(ctx, *items, sizeof(fe_Object*) * (size_t)new_capacity);
  if (!new_items) return 0;
  *items = new_items;
  *capacity = new_capacity;
  return 1;
}

static int module_cache_find_index(fe_Context *ctx, const char *path) {
  int i;
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;
  for (i = 0; i < ctx->loaded_module_count; i++) {
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
    if (strcmp(ctx->loaded_modules[i], path) == 0) return i;
  }
  return -1;
}

static fe_Object* module_cache_get(fe_Context *ctx, const char *path) {
  int index = module_cache_find_index(ctx, path);
  if (index < 0) return &nil;
  return ctx->loaded_module_values[index];
}

static int module_cache_push_owned(fe_Context *ctx, char *path, fe_Object *value) {
  int needed = ctx->loaded_module_count + 1;
  if (!ensure_string_array_capacity(ctx, &ctx->loaded_modules,
                                    &ctx->loaded_module_path_capacity, needed)) {
    return 0;
  }
  if (!ensure_object_array_capacity(ctx, &ctx->loaded_module_values,
                                    &ctx->loaded_module_value_capacity, needed)) {
    return 0;
  }
  ctx->loaded_modules[ctx->loaded_module_count] = path;
  ctx->loaded_module_values[ctx->loaded_module_count] = value;
  ctx->loaded_module_count++;
  return 1;
}

static void module_cache_clear(fe_Context *ctx) {
  string_array_clear(ctx, &ctx->loaded_modules,
                     &ctx->loaded_module_count, &ctx->loaded_module_path_capacity);
  tracked_free(ctx, ctx->loaded_module_values);
  ctx->loaded_module_values = NULL;
  ctx->loaded_module_value_capacity = 0;
}

static int is_path_separator(char chr) {
  return chr == '/' || chr == '\\';
}

static fe_Object* getbound(fe_Context *ctx, fe_Object *sym, fe_Object *env);

static void normalize_path_chars(fe_Context *ctx, char *path) {
  char *p;
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;
  if (!path) return;
  for (p = path; *p; p++) {
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
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
  normalize_path_chars(ctx, copy);
  return copy;
}

static char* normalize_existing_path(fe_Context *ctx, const char *path) {
#ifdef _WIN32
  wchar_t *wide_path;
  wchar_t *wide_resolved;
  DWORD needed;
#else
  char resolved[4096];
#endif
  char *copy;
#ifdef _WIN32
  wide_path = utf8_to_wide_tracked(ctx, path);
  if (!wide_path) return NULL;
  needed = GetFullPathNameW(wide_path, 0, NULL, NULL);
  if (needed == 0) {
    tracked_free(ctx, wide_path);
    return NULL;
  }
  wide_resolved = tracked_alloc(ctx, (size_t)needed * sizeof(wchar_t));
  if (!wide_resolved) {
    tracked_free(ctx, wide_path);
    return NULL;
  }
  if (GetFullPathNameW(wide_path, needed, wide_resolved, NULL) == 0) {
    tracked_free(ctx, wide_resolved);
    tracked_free(ctx, wide_path);
    return NULL;
  }
  copy = wide_to_utf8_tracked(ctx, wide_resolved);
  tracked_free(ctx, wide_resolved);
  tracked_free(ctx, wide_path);
  if (!copy) return NULL;
#else
  if (!realpath(path, resolved)) return NULL;
#endif
#ifndef _WIN32
  copy = dup_cstring(ctx, resolved);
  if (!copy) return NULL;
#endif
  normalize_path_chars(ctx, copy);
  return copy;
}

static char* path_dirname_copy(fe_Context *ctx, const char *path) {
  const char *last_sep = NULL;
  const char *p;
  size_t len;
  char *dir;
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;

  if (!path || !*path) return dup_cstring(ctx, ".");
  for (p = path; *p; p++) {
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
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

static int module_spec_uses_path_mode(const char *spec) {
  const char *p;
  if (!spec) return 0;
  for (p = spec; *p; p++) {
    if (is_path_separator(*p)) return 1;
  }
  return 0;
}

static int module_spec_is_relative(const char *spec) {
  if (!spec || !*spec) return 0;
  return spec[0] == '.';
}

static char* module_spec_to_lookup_name(fe_Context *ctx, const char *spec) {
  char *copy;
  char *p;
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;

  copy = dup_cstring(ctx, spec);
  if (!copy) return NULL;
  if (module_spec_uses_path_mode(spec)) {
    normalize_path_chars(ctx, copy);
    return copy;
  }

  for (p = copy; *p; p++) {
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      tracked_free(ctx, copy);
      fe_error(ctx, abort_msg);
    }
    if (*p == '.') *p = '/';
  }
  return copy;
}

static int split_module_spec_segments(fe_Context *ctx, const char *spec,
                                      char ***segments, int *count,
                                      int *capacity) {
  int path_mode = module_spec_uses_path_mode(spec);
  const char *segment_start = spec;
  const char *p;
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;

  *segments = NULL;
  *count = 0;
  *capacity = 0;

  if (!spec || !*spec) return 1;

  for (p = spec;; p++) {
    int at_end = (*p == '\0');
    int is_sep = 0;
    size_t len;

    if (!at_end) {
      const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
      if (abort_msg != NULL) {
        string_array_clear(ctx, segments, count, capacity);
        fe_error(ctx, abort_msg);
      }
      is_sep = path_mode ? is_path_separator(*p) : (*p == '.');
    }

    if (!at_end && !is_sep) {
      continue;
    }

    len = (size_t)(p - segment_start);
    if (len > 0) {
      if (!(path_mode && len == 1 && segment_start[0] == '.') &&
          !(path_mode && len == 2 && segment_start[0] == '.' &&
            segment_start[1] == '.')) {
        if (!string_array_push_copy(ctx, segments, count, capacity, segment_start)) {
          string_array_clear(ctx, segments, count, capacity);
          return 0;
        }
        (*segments)[*count - 1][len] = '\0';
      }
    }

    if (at_end) {
      break;
    }
    segment_start = p + 1;
  }

  return 1;
}

static char* join_module_segments(fe_Context *ctx, char **segments, int count,
                                  char sep) {
  size_t total = 1;
  char *joined;
  char *out;
  int i;

  for (i = 0; i < count; i++) {
    total += strlen(segments[i]);
    if (i + 1 < count) total++;
  }

  joined = tracked_alloc(ctx, total);
  if (!joined) return NULL;

  out = joined;
  for (i = 0; i < count; i++) {
    size_t len = strlen(segments[i]);
    memcpy(out, segments[i], len);
    out += len;
    if (i + 1 < count) *out++ = sep;
  }
  *out = '\0';
  return joined;
}

static fe_Object* lookup_global_module(fe_Context *ctx, const char *name) {
  fe_Object *value;
  if (!name || !*name) return &nil;
  value = cdr(getbound(ctx, fe_symbol(ctx, name), &nil));
  return type(value) == FE_TMAP ? value : &nil;
}

static fe_Object* lookup_module_chain(fe_Context *ctx, char **segments, int count) {
  fe_Object *current;
  int i;

  if (count <= 0) return &nil;
  current = lookup_global_module(ctx, segments[0]);
  if (type(current) != FE_TMAP) return &nil;

  for (i = 1; i < count; i++) {
    current = fe_map_get(ctx, current, fe_symbol(ctx, segments[i]));
    if (type(current) != FE_TMAP) return &nil;
  }

  return current;
}

static fe_Object* ensure_package_chain(fe_Context *ctx, char **segments, int count) {
  fe_Object *current;
  fe_Object *next;
  int i;

  if (count <= 0) return NULL;
  current = lookup_global_module(ctx, segments[0]);
  if (type(current) != FE_TMAP) {
    current = fe_map(ctx);
    fe_set(ctx, fe_symbol(ctx, segments[0]), current);
  }

  for (i = 1; i < count; i++) {
    fe_Object *segment_sym = fe_symbol(ctx, segments[i]);
    next = fe_map_get(ctx, current, segment_sym);
    if (fe_isnil(ctx, next)) {
      next = fe_map(ctx);
      fe_map_set(ctx, current, segment_sym, next);
    } else if (type(next) != FE_TMAP) {
      char error_buf[256];
      snprintf(error_buf, sizeof(error_buf), "package segment '%s' is not a module/map", segments[i]);
      fe_error(ctx, error_buf);
    }
    current = next;
  }

  return current;
}

static void bind_imported_module(fe_Context *ctx, const char *spec,
                                 char **segments, int count, int is_relative,
                                 fe_Object *module_obj) {
  if (count <= 0 || type(module_obj) != FE_TMAP) return;

  if (!is_relative && count > 1) {
    fe_Object *parent = ensure_package_chain(ctx, segments, count - 1);
    fe_map_set(ctx, parent, fe_symbol(ctx, segments[count - 1]), module_obj);
    return;
  }

  fe_set(ctx, fe_symbol(ctx, segments[count - 1]), module_obj);
  if (spec && strcmp(spec, segments[count - 1]) != 0 &&
      !module_spec_uses_path_mode(spec) && strchr(spec, '.') == NULL) {
    fe_set(ctx, fe_symbol(ctx, spec), module_obj);
  }
}

static int file_exists(const char *path) {
#ifdef _WIN32
  int exists = 0;
  int needed;
  wchar_t *wide_path;

  if (!path) return 0;
  needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if (needed <= 0) return 0;
  wide_path = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
  if (!wide_path) return 0;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, needed) > 0) {
    FILE *fp = _wfopen(wide_path, L"rb");
    if (fp) {
      fclose(fp);
      exists = 1;
    }
  }
  free(wide_path);
  return exists;
#else
  FILE *fp = fopen(path, "rb");
  if (!fp) return 0;
  fclose(fp);
  return 1;
#endif
}

static char* read_text_file(fe_Context *ctx, const char *path) {
  enum { SOURCE_BUFFER_TAIL_SLACK = 8 };
  FILE *file;
  long file_size;
  size_t bytes_read;
  char *buffer;

#ifdef _WIN32
  {
    wchar_t *wide_path = utf8_to_wide_tracked(ctx, path);
    if (!wide_path) return NULL;
    file = _wfopen(wide_path, L"rb");
    tracked_free(ctx, wide_path);
  }
#else
  file = fopen(path, "rb");
#endif
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
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;

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
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
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
  ctx->modulestack = &nil;
}

static fe_Object* do_file_common(fe_Context *ctx, const char *path,
                                 fe_Object *implicit_exports) {
  char *source;
  fe_Object *result;
  int pushed_module = 0;

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

  if (implicit_exports != NULL && type(implicit_exports) == FE_TMAP) {
    ctx->modulestack = fe_cons(ctx, implicit_exports, ctx->modulestack);
    pushed_module = 1;
  }

  result = fex_do_string_named(ctx, source, path);
  if (pushed_module) {
    ctx->modulestack = cdr(ctx->modulestack);
  }
  string_array_pop(ctx, ctx->source_buffers, &ctx->source_buffer_count);
  pop_source_dir(ctx);
  return result;
}

static fe_Object* fex_do_import_file(fe_Context *ctx, const char *path,
                                     fe_Object *implicit_exports) {
  return do_file_common(ctx, path, implicit_exports);
}

static fe_Object* try_import_file_runner(fe_Context *ctx, const void *path,
                                         const void *implicit_exports) {
  return fex_do_import_file(ctx, (const char*)path, (fe_Object*)implicit_exports);
}

fe_Object* fex_do_file(fe_Context *ctx, const char *path) {
  return do_file_common(ctx, path, NULL);
}


void fe_error(fe_Context *ctx, const char *msg) {
  fe_Object *cl = ctx->calllist;
  /* reset context state */
  ctx->calllist = &nil;
  ctx->eval_depth = 0;
  ctx->current_eval_depth = 0;
  ctx->current_read_depth = 0;
  if (ctx->timeout_ms > 0) {
    ctx->timeout_countdown = TIMEOUT_CHECK_INTERVAL;
  }
  ctx->interrupt_countdown = ctx->interrupt_interval;
  fex_compile_cleanup_ctx(ctx);
  fex_temp_cleanup_all(ctx);
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


static fe_Object* checktype(fe_Context *ctx, fe_Object *obj, int expected) {
  char buf[64];
  int actual_type = type(obj);

  /* Special case: allow fixnums when expecting numbers */
  if (expected == FE_TNUMBER && FE_IS_FIXNUM(obj)) {
    return obj;
  }

  if (actual_type != expected) {
    snprintf(buf, sizeof(buf), "expected %s, got %s",
            typenames[expected],
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
    /*  Iterative mark.  Uses an explicit stack for car branches so that
        deeply nested pair trees don't overflow the C call stack.  The
        cdr direction is followed iteratively in a loop. */

#define MARK_STACK_SIZE 256
    fe_Object *stack[MARK_STACK_SIZE];
    int sp = 0;

    goto enter;

pop:
    if (sp == 0) return;
    obj = stack[--sp];

enter:
    for (;;) {
        /* 0. Fast exits for objects we never allocate. */
        if (FE_IS_FIXNUM(obj) || FE_IS_BOOLEAN(obj) || isnil(obj)) goto pop;

        /* 1. Do not mark rogue pointers that don't belong to us. */
        if (obj < ctx->objects || obj >= ctx->objects + ctx->object_count)
            goto pop;

        /* 2. Already marked?  Done. */
        if (tag(obj) & GCMARKBIT) goto pop;
        tag(obj) |= GCMARKBIT;

        switch (type(obj)) {
        case FE_TPAIR:
            /* Push car onto explicit stack, iterate cdr */
            if (sp < MARK_STACK_SIZE) {
                stack[sp++] = car(obj);
            } else {
                /* Stack full: fall back to recursion for car */
                fe_mark(ctx, car(obj));
            }
            obj = cdr(obj);
            continue;               /* re-check fixnum / nil next round */

        case FE_TFUNC:   /* (prototype . body) where prototype=(free_vars) */
        case FE_TMACRO:  /* (prototype . body) where prototype=(free_vars) */
        case FE_TSYMBOL: /* (name-string . value)   */
            obj = cdr(obj);
            continue;

        case FE_TSTRING:
        case FE_TBYTES:
            goto pop;

        case FE_TPTR:
            if (ctx->handlers.mark) ctx->handlers.mark(ctx, obj);
            /* fall-through */
        case FE_TMAP: {
            fe_Map *map = mapdata(obj);
            int i;
            if (!map) goto pop;
            for (i = 0; i < map->capacity; i++) {
              if (map->states[i] == MAP_USED) {
                if (sp < MARK_STACK_SIZE) {
                    stack[sp++] = map->keys[i];
                } else {
                    fe_mark(ctx, map->keys[i]);
                }
                if (sp < MARK_STACK_SIZE) {
                    stack[sp++] = map->values[i];
                } else {
                    fe_mark(ctx, map->values[i]);
                }
              }
            }
            goto pop;
        }
        default:
            goto pop;
        }
    }
#undef MARK_STACK_SIZE
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
  for (i = 0; i < ctx->loaded_module_count; i++) {
    fe_mark(ctx, ctx->loaded_module_values[i]);
  }
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

  fex_span_prune(ctx);

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
static int is_return_obj(fe_Context *ctx, fe_Object *obj) {
  return type(obj) == FE_TPAIR && car(obj) == ctx->return_sym;
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
#if ULONG_MAX > 0xFFFFFFFFu
  unsigned long hash = 14695981039346656037UL;
  #define FNV_PRIME 1099511628211UL
#else
  unsigned long hash = 2166136261u;
  #define FNV_PRIME 16777619u
#endif
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;
#ifdef FE_OPT_NO_MALLOC_STRINGS
  size_t remaining = FE_STR_LEN(obj);
  uint32_t offset = obj->cdr.u32;
  while (offset != FE_SLAB_NULL && remaining > 0) {
    fe_Slab *slab = (fe_Slab*)(ctx->str_base + offset);
    size_t to_hash = (remaining > FE_SLAB_DATA_SIZE) ? FE_SLAB_DATA_SIZE : remaining;
    size_t i;
    for (i = 0; i < to_hash; i++) {
      const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
      if (abort_msg != NULL) {
        fe_error(ctx, abort_msg);
      }
      hash ^= (unsigned char)slab->data[i];
      hash *= FNV_PRIME;
    }
    remaining -= to_hash;
    offset = slab->next;
  }
#else
  const unsigned char *p = (const unsigned char*)FE_STR_DATA(ctx, obj);
  size_t i;
  size_t len = FE_STR_LEN(obj);
  for (i = 0; i < len; i++) {
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
    hash ^= p[i];
    hash *= FNV_PRIME;
  }
#endif
#undef FNV_PRIME
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
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;

  grown = map_alloc(ctx, capacity);
  if (!grown) {
    return 0;
  }

  for (i = 0; i < map->capacity; i++) {
    int found;
    int slot;
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
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
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;

  *found = 0;
  if (map->capacity <= 0) {
    return -1;
  }

  hash = hash_string_obj(ctx, key);
  index = (int)(hash % (unsigned long)map->capacity);

  for (steps = 0; steps < map->capacity; steps++) {
    int slot = (index + steps) % map->capacity;
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
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
  int gc_save;
  int i;
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;
  checktype(ctx, map_obj, FE_TMAP);
  gc_save = fe_savegc(ctx);
  fe_pushgc(ctx, map_obj);
  fe_pushgc(ctx, result);
  map = mapdata(map_obj);
  for (i = map->capacity - 1; i >= 0; i--) {
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
    if (map->states[i] == MAP_USED) {
      fe_restoregc(ctx, gc_save);
      fe_pushgc(ctx, map_obj);
      fe_pushgc(ctx, result);
      result = fe_cons(ctx, map->keys[i], result);
    }
  }
  fe_restoregc(ctx, gc_save);
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

static fe_Object* fe_symbol_from_string_obj(fe_Context *ctx, fe_Object *name_obj) {
  fe_Object *obj;
  int gc_save;

  checktype(ctx, name_obj, FE_TSTRING);
  gc_save = fe_savegc(ctx);
  fe_pushgc(ctx, name_obj);

  for (obj = ctx->symlist; !isnil(obj); obj = cdr(obj)) {
    if (equal(ctx, car(cdr(car(obj))), name_obj)) {
      fe_restoregc(ctx, gc_save);
      return car(obj);
    }
  }

  obj = object(ctx);
  settype(obj, FE_TSYMBOL);
  cdr(obj) = fe_cons(ctx, name_obj, &nil);
  ctx->symlist = fe_cons(ctx, obj, ctx->symlist);
  fe_restoregc(ctx, gc_save);
  return obj;
}


fe_Object* fe_symbol(fe_Context *ctx, const char *name) {
  fe_Object *obj;
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;
  /* try to find in symlist */
  for (obj = ctx->symlist; !isnil(obj); obj = cdr(obj)) {
    const char *abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
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

static void write_checked(fe_Context *ctx, fe_WriteFn fn, void *udata, char chr,
                          size_t *countdown) {
  const char *abort_msg = poll_io_abort(ctx, countdown);
  if (abort_msg != NULL) {
    fe_error(ctx, abort_msg);
  }
  fn(ctx, udata, chr);
}

static void writestr(fe_Context *ctx, fe_WriteFn fn, void *udata, const char *s,
                     size_t *countdown) {
  while (*s) {
    write_checked(ctx, fn, udata, *s++, countdown);
  }
}

static void write_hex_byte(fe_Context *ctx, fe_WriteFn fn, void *udata,
                           unsigned char byte, size_t *countdown) {
  static const char hexdigits[] = "0123456789abcdef";
  write_checked(ctx, fn, udata, hexdigits[(byte >> 4) & 0x0f], countdown);
  write_checked(ctx, fn, udata, hexdigits[byte & 0x0f], countdown);
}

void fe_write(fe_Context *ctx, fe_Object *obj, fe_WriteFn fn, void *udata, int qt) {
  char buf[32];
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;
  const char *abort_msg;

  switch (type(obj)) {
    case FE_TNIL:
      writestr(ctx, fn, udata, "nil", &poll_countdown);
      break;

    case FE_TBOOLEAN:
      writestr(ctx, fn, udata, (obj == FE_TRUE) ? "true" : "false", &poll_countdown);
      break;

    case FE_TNUMBER:
      if (FE_IS_FIXNUM(obj)) {
          snprintf(buf, sizeof(buf), "%" PRIdMAX, (intmax_t)FE_UNBOX_FIXNUM(obj));
      } else {
          snprintf(buf, sizeof(buf), "%.7g", number(obj));
      }
      writestr(ctx, fn, udata, buf, &poll_countdown);
      break;

    case FE_TPAIR:
      if (car(obj) == ctx->frame_sym) {
        writestr(ctx, fn, udata, "[env frame]", &poll_countdown);
        break;
      }

      write_checked(ctx, fn, udata, '(', &poll_countdown);
      for (;;) {
        abort_msg = poll_io_abort(ctx, &poll_countdown);
        if (abort_msg != NULL) {
          fe_error(ctx, abort_msg);
        }
        fe_write(ctx, car(obj), fn, udata, 1);
        obj = cdr(obj);
        if (type(obj) != FE_TPAIR) { break; }
        write_checked(ctx, fn, udata, ' ', &poll_countdown);
      }
      if (!isnil(obj)) {
        writestr(ctx, fn, udata, " . ", &poll_countdown);
        fe_write(ctx, obj, fn, udata, 1);
      }
      write_checked(ctx, fn, udata, ')', &poll_countdown);
      break;

    case FE_TSYMBOL:
      fe_write(ctx, car(cdr(obj)), fn, udata, 0);
      break;

    case FE_TSTRING:
      if (qt) write_checked(ctx, fn, udata, '"', &poll_countdown);
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
                      if (qt && c == '"') write_checked(ctx, fn, udata, '\\', &poll_countdown);
                      write_checked(ctx, fn, udata, c, &poll_countdown);
                  }
                  remaining -= to_write;
                  offset = slab->next;
              }
          }
      }
#else
      {
          const char *p = FE_STR_DATA(ctx, obj);
          size_t len = FE_STR_LEN(obj);
          size_t i;
          for (i = 0; i < len; i++) {
              if (qt && p[i] == '"') write_checked(ctx, fn, udata, '\\', &poll_countdown);
              write_checked(ctx, fn, udata, p[i], &poll_countdown);
          }
      }
#endif
      if (qt) write_checked(ctx, fn, udata, '"', &poll_countdown);
      break;

    case FE_TBYTES:
      writestr(ctx, fn, udata, "#bytes[", &poll_countdown);
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
                      if (!first) write_checked(ctx, fn, udata, ' ', &poll_countdown);
                      write_hex_byte(ctx, fn, udata, (unsigned char)slab->data[i], &poll_countdown);
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
              if (i > 0) write_checked(ctx, fn, udata, ' ', &poll_countdown);
              write_hex_byte(ctx, fn, udata, p[i], &poll_countdown);
          }
      }
#endif
      write_checked(ctx, fn, udata, ']', &poll_countdown);
      break;

    case FE_TMAP: {
      fe_Map *map = mapdata(obj);
      int i;
      int first = 1;
      write_checked(ctx, fn, udata, '{', &poll_countdown);
      if (map) {
        for (i = 0; i < map->capacity; i++) {
          abort_msg = poll_io_abort(ctx, &poll_countdown);
          if (abort_msg != NULL) {
            fe_error(ctx, abort_msg);
          }
          if (map->states[i] != MAP_USED) {
            continue;
          }
          if (!first) {
            writestr(ctx, fn, udata, ", ", &poll_countdown);
          }
          fe_write(ctx, map->keys[i], fn, udata, 1);
          writestr(ctx, fn, udata, ": ", &poll_countdown);
          fe_write(ctx, map->values[i], fn, udata, 1);
          first = 0;
        }
      }
      write_checked(ctx, fn, udata, '}', &poll_countdown);
      break;
    }

    default:
      snprintf(buf, sizeof(buf), "[%s %p]", typenames[type(obj)], (void*) obj);
      writestr(ctx, fn, udata, buf, &poll_countdown);
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
  unused(ctx);
  return FE_STR_LEN(obj);
}

int fe_string_contains_nul(fe_Context *ctx, fe_Object *obj)
{
#ifdef FE_OPT_NO_MALLOC_STRINGS
  size_t remaining = FE_STR_LEN(obj);
  uint32_t offset = obj->cdr.u32;

  while (offset != FE_SLAB_NULL && remaining > 0) {
    fe_Slab *slab = (fe_Slab*)(ctx->str_base + offset);
    size_t to_scan = (remaining > FE_SLAB_DATA_SIZE) ? FE_SLAB_DATA_SIZE : remaining;
    if (memchr(slab->data, '\0', to_scan) != NULL) {
      return 1;
    }
    remaining -= to_scan;
    offset = slab->next;
  }
  return 0;
#else
  unused(ctx);
  return FE_STR_LEN(obj) > 0 &&
         memchr(FE_STR_DATA(ctx, obj), '\0', FE_STR_LEN(obj)) != NULL;
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

static fe_Object* getbound(fe_Context *ctx, fe_Object *sym, fe_Object *env) {
  fe_Object *p;
  /* Check for new closure environment frame */
  if (type(env) == FE_TPAIR && car(env) == ctx->frame_sym) {
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
  cdr(getbound(ctx, sym, &nil)) = v;
}


static fe_Object rparen;

static fe_Object* read_(fe_Context *ctx, fe_ReadFn fn, void *udata) {
  const char *delimiter = " \n\t\r();";
  fe_Object *v, *res, **tail;
  fe_Number n;
  int chr, gc;
  char buf[64], *p;
  size_t poll_countdown = FE_IO_ABORT_CHECK_INTERVAL;
  const char *abort_msg;

  /* get next character */
  chr = ctx->nextchr ? ctx->nextchr : fn(ctx, udata);
  ctx->nextchr = '\0';

  /* skip whitespace */
  while (chr && strchr(" \n\t\r", chr)) {
    abort_msg = poll_io_abort(ctx, &poll_countdown);
    if (abort_msg != NULL) {
      fe_error(ctx, abort_msg);
    }
    chr = fn(ctx, udata);
  }

  switch (chr) {
    case '\0':
      return NULL;

    case ';':
      while (chr && chr != '\n') {
        abort_msg = poll_io_abort(ctx, &poll_countdown);
        if (abort_msg != NULL) {
          fe_error(ctx, abort_msg);
        }
        chr = fn(ctx, udata);
      }
      return read_(ctx, fn, udata);

    case ')':
      return &rparen;

    case '(':
      if (ctx->max_read_depth > 0 && ctx->current_read_depth >= ctx->max_read_depth) {
        fe_error(ctx, "read nesting depth limit exceeded");
      }
      ctx->current_read_depth++;
      res = &nil;
      tail = &res;
      gc = fe_savegc(ctx);
      fe_pushgc(ctx, res); /* to cause error on too-deep nesting */
      while ( (v = read_(ctx, fn, udata)) != &rparen ) {
        abort_msg = poll_io_abort(ctx, &poll_countdown);
        if (abort_msg != NULL) {
          fe_error(ctx, abort_msg);
        }
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
      ctx->current_read_depth--;
      return res;

    case '\'':
      if (ctx->max_read_depth > 0 && ctx->current_read_depth >= ctx->max_read_depth) {
        fe_error(ctx, "read nesting depth limit exceeded");
      }
      ctx->current_read_depth++;
      v = fe_read(ctx, fn, udata);
      if (!v) { fe_error(ctx, "stray '''"); }
      ctx->current_read_depth--;
      return fe_cons(ctx, fe_symbol(ctx, "quote"), fe_cons(ctx, v, &nil));

    case '"':
      {
#ifdef FE_OPT_NO_MALLOC_STRINGS
#define FE_MAX_LITERAL_SZ 256
        char s_buf[FE_MAX_LITERAL_SZ];
        size_t len = 0;
        chr = fn(ctx, udata);
        while (chr != '"') {
            abort_msg = poll_io_abort(ctx, &poll_countdown);
            if (abort_msg != NULL) {
                fe_error(ctx, abort_msg);
            }
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
          abort_msg = poll_io_abort(ctx, &poll_countdown);
          if (abort_msg != NULL) {
            tracked_free(ctx, s_buf);
            fe_error(ctx, abort_msg);
          }
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
        abort_msg = poll_io_abort(ctx, &poll_countdown);
        if (abort_msg != NULL) {
          fe_error(ctx, abort_msg);
        }
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
  int save = fe_savegc(ctx);
  while (!isnil(lst)) {
    fe_Object *value;

    fe_restoregc(ctx, save);
    fe_pushgc(ctx, res);
    fe_pushgc(ctx, lst);
    fe_pushgc(ctx, env);
    value = eval(ctx, fe_nextarg(ctx, &lst), env, NULL);

    fe_restoregc(ctx, save);
    fe_pushgc(ctx, res);
    fe_pushgc(ctx, lst);
    fe_pushgc(ctx, env);
    fe_pushgc(ctx, value);
    *tail = fe_cons(ctx, value, &nil);
    tail = &cdr(*tail);
  }
  fe_restoregc(ctx, save);
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
    if (is_return_obj(ctx, res)) { break; }
  }
  return res;
}


static fe_Object* argstoenv(fe_Context *ctx, fe_Object *prm, fe_Object *arg, fe_Object *env) {
  int save = fe_savegc(ctx);
  while (!isnil(prm)) {
    if (type(prm) != FE_TPAIR) {
      fe_Object *binding;
      fe_restoregc(ctx, save);
      fe_pushgc(ctx, prm);
      fe_pushgc(ctx, arg);
      fe_pushgc(ctx, env);
      binding = fe_cons(ctx, prm, arg);
      fe_restoregc(ctx, save);
      fe_pushgc(ctx, env);
      fe_pushgc(ctx, binding);
      env = fe_cons(ctx, binding, env);
      break;
    }
    {
      fe_Object *binding;
      fe_restoregc(ctx, save);
      fe_pushgc(ctx, prm);
      fe_pushgc(ctx, arg);
      fe_pushgc(ctx, env);
      binding = fe_cons(ctx, car(prm), fe_car(ctx, arg));
      fe_restoregc(ctx, save);
      fe_pushgc(ctx, prm);
      fe_pushgc(ctx, arg);
      fe_pushgc(ctx, env);
      fe_pushgc(ctx, binding);
      env = fe_cons(ctx, binding, env);
    }
    prm = cdr(prm);
    arg = fe_cdr(ctx, arg);
  }
  fe_restoregc(ctx, save);
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


static int path_contains_dotdot(const char *path) {
  const char *p = path;
  while (*p) {
    /* Match ".." at start, after a separator, or at end */
    if (p[0] == '.' && p[1] == '.') {
      /* Check that ".." is bounded by separators or string boundaries */
      int at_start = (p == path) || is_path_separator(p[-1]);
      int at_end = (p[2] == '\0') || is_path_separator(p[2]);
      if (at_start && at_end) return 1;
    }
    p++;
  }
  return 0;
}

static char* import_spec_to_cstring(fe_Context *ctx, fe_Object *spec_obj) {
  size_t len;
  char *module_spec;

  if (type(spec_obj) != FE_TSTRING && type(spec_obj) != FE_TSYMBOL) {
    checktype(ctx, spec_obj, FE_TSTRING);
  }

  len = fe_strlen(ctx, spec_obj);
  module_spec = tracked_alloc(ctx, len + 1);
  if (!module_spec) {
    memory_error(ctx, "out of memory (module spec)");
  }
  if (type(spec_obj) == FE_TSTRING && fe_string_contains_nul(ctx, spec_obj)) {
    tracked_free(ctx, module_spec);
    fe_error(ctx, "import: strings containing NUL bytes are not allowed");
  }
  fe_tostring(ctx, spec_obj, module_spec, (int)len + 1);
  if (len > 0 && memchr(module_spec, '\0', len) != NULL) {
    tracked_free(ctx, module_spec);
    fe_error(ctx, "import: strings containing NUL bytes are not allowed");
  }
  if (path_contains_dotdot(module_spec)) {
    tracked_free(ctx, module_spec);
    fe_error(ctx, "import: '..' path components are not allowed in import specifiers");
  }
  return module_spec;
}

static void append_module_search_error(char *error_buf, size_t error_buf_size,
                                       const char *module_spec,
                                       const char *searched_paths) {
  size_t error_len;
  size_t remaining;

  snprintf(error_buf, error_buf_size, "could not resolve module '%s'", module_spec);
  if (!searched_paths || !*searched_paths) return;

  error_len = strlen(error_buf);
  remaining = error_buf_size - error_len - 1;
  if (remaining > 0) {
    static const char searched_label[] = "\nsearched:\n";
    size_t label_len = sizeof(searched_label) - 1;
    size_t copy_len;
    if (label_len > remaining) label_len = remaining;
    memcpy(error_buf + error_len, searched_label, label_len);
    error_len += label_len;
    remaining = error_buf_size - error_len - 1;
    copy_len = strlen(searched_paths);
    if (copy_len > remaining) copy_len = remaining;
    memcpy(error_buf + error_len, searched_paths, copy_len);
    error_len += copy_len;
    error_buf[error_len] = '\0';
  }
}

static fe_Object* resolve_imported_module_value(fe_Context *ctx, const char *module_spec,
                                                char **segments, int segment_count,
                                                fe_Object *eval_result) {
  fe_Object *module_obj;

  if (eval_result != NULL && type(eval_result) == FE_TMAP) {
    return eval_result;
  }

  module_obj = lookup_module_chain(ctx, segments, segment_count);
  if (type(module_obj) == FE_TMAP) {
    return module_obj;
  }

  if (segment_count > 0) {
    module_obj = lookup_global_module(ctx, segments[segment_count - 1]);
    if (type(module_obj) == FE_TMAP) {
      return module_obj;
    }
  }

  module_obj = lookup_global_module(ctx, module_spec);
  if (type(module_obj) == FE_TMAP) {
    return module_obj;
  }

  return &nil;
}

static fe_Object* import_module_spec(fe_Context *ctx, char *module_spec,
                                     int optional) {
  char error_buf[2048];
  char *lookup_name = NULL;
  char *module_path;
  char *searched_paths = NULL;
  char **segments = NULL;
  int segment_count = 0;
  int segment_capacity = 0;
  int is_relative;
  fe_Object *result = NULL;
  fe_Object *module_obj = NULL;
  fe_Object *implicit_exports = NULL;
  FexError import_error;
  FexStatus import_status;
  int gc_save = fe_savegc(ctx);
  int i;

  lookup_name = module_spec_to_lookup_name(ctx, module_spec);
  if (!lookup_name) {
    tracked_free(ctx, module_spec);
    memory_error(ctx, "out of memory (module lookup)");
  }
  if (!split_module_spec_segments(ctx, module_spec, &segments, &segment_count,
                                  &segment_capacity)) {
    tracked_free(ctx, lookup_name);
    tracked_free(ctx, module_spec);
    memory_error(ctx, "out of memory (module segments)");
  }
  is_relative = module_spec_is_relative(module_spec);

  if (!is_relative && segment_count > 1) {
    for (i = 1; i < segment_count; i++) {
      char *prefix_spec = join_module_segments(ctx, segments, i, '/');
      if (!prefix_spec) {
        tracked_free(ctx, lookup_name);
        string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
        tracked_free(ctx, module_spec);
        memory_error(ctx, "out of memory (module prefix)");
      }
      (void)import_module_spec(ctx, prefix_spec, 1);
    }
  }

  module_path = resolve_module_file(ctx, lookup_name, optional ? NULL : &searched_paths);
  if (!module_path) {
    module_obj = resolve_imported_module_value(ctx, module_spec, segments,
                                               segment_count, NULL);
    if (type(module_obj) == FE_TMAP) {
      bind_imported_module(ctx, module_spec, segments, segment_count,
                           is_relative, module_obj);
      tracked_free(ctx, lookup_name);
      tracked_free(ctx, searched_paths);
      string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
      fe_restoregc(ctx, gc_save);
      tracked_free(ctx, module_spec);
      return module_obj;
    }

    tracked_free(ctx, lookup_name);
    if (optional) {
      tracked_free(ctx, searched_paths);
      string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
      fe_restoregc(ctx, gc_save);
      tracked_free(ctx, module_spec);
      return NULL;
    }

    if (searched_paths && ctx->alloc_failure_active) {
      tracked_free(ctx, searched_paths);
    }
    if (ctx->alloc_failure_active) {
      string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
      tracked_free(ctx, module_spec);
      memory_error(ctx, "out of memory (module path)");
    }
    append_module_search_error(error_buf, sizeof(error_buf), module_spec,
                               searched_paths);
    tracked_free(ctx, searched_paths);
    string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
    tracked_free(ctx, module_spec);
    fe_error(ctx, error_buf);
  }
  tracked_free(ctx, lookup_name);

  module_obj = module_cache_get(ctx, module_path);
  if (type(module_obj) == FE_TMAP) {
    tracked_free(ctx, module_path);
    bind_imported_module(ctx, module_spec, segments, segment_count,
                         is_relative, module_obj);
    string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
    fe_restoregc(ctx, gc_save);
    tracked_free(ctx, module_spec);
    return module_obj;
  }

  if (string_array_contains(ctx, ctx->loading_modules, ctx->loading_module_count, module_path)) {
    tracked_free(ctx, module_path);
    string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
    fe_restoregc(ctx, gc_save);
    snprintf(error_buf, sizeof(error_buf), "cyclic import detected for module '%s'", module_spec);
    tracked_free(ctx, module_spec);
    fe_error(ctx, error_buf);
  }

  if (!string_array_push_copy(ctx, &ctx->loading_modules, &ctx->loading_module_count,
                              &ctx->loading_module_capacity, module_path)) {
    tracked_free(ctx, module_path);
    string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
    fe_restoregc(ctx, gc_save);
    tracked_free(ctx, module_spec);
    memory_error(ctx, "out of memory (import state)");
  }

  implicit_exports = fe_map(ctx);
  fe_pushgc(ctx, implicit_exports);
  import_status = fex_try_run_internal(ctx, &result, &import_error,
                                       try_import_file_runner,
                                       module_path, implicit_exports,
                                       module_path, 0);
  if (import_status == FEX_STATUS_OK && result != NULL) {
    fe_pushgc(ctx, result);
  }

  if (import_status != FEX_STATUS_OK) {
    string_array_pop(ctx, ctx->loading_modules, &ctx->loading_module_count);
    tracked_free(ctx, module_path);
    string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
    fe_restoregc(ctx, gc_save);
    tracked_free(ctx, module_spec);
    if (fex_try_is_active()) {
      fex_try_raise_error(&import_error);
    }
    fe_error(ctx, import_error.message);
  }

  if (result == NULL) {
    string_array_pop(ctx, ctx->loading_modules, &ctx->loading_module_count);
    tracked_free(ctx, module_path);
    string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
    fe_restoregc(ctx, gc_save);
    tracked_free(ctx, module_spec);
    fe_error(ctx, "failed to import module");
  }

  string_array_pop(ctx, ctx->loading_modules, &ctx->loading_module_count);
  if (fe_map_count(ctx, implicit_exports) > 0) {
    module_obj = implicit_exports;
  } else {
    module_obj = resolve_imported_module_value(ctx, module_spec, segments,
                                               segment_count, NULL);
    if (type(module_obj) != FE_TMAP) {
      module_obj = implicit_exports;
    }
  }
  fe_pushgc(ctx, module_obj);

  if (type(module_obj) != FE_TMAP) {
    tracked_free(ctx, module_path);
    string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
    fe_restoregc(ctx, gc_save);
    snprintf(error_buf, sizeof(error_buf), "module '%s' did not produce a module value", module_spec);
    tracked_free(ctx, module_spec);
    fe_error(ctx, error_buf);
  }

  bind_imported_module(ctx, module_spec, segments, segment_count,
                       is_relative, module_obj);

  if (!module_cache_push_owned(ctx, module_path, module_obj)) {
    tracked_free(ctx, module_path);
    string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
    fe_restoregc(ctx, gc_save);
    tracked_free(ctx, module_spec);
    memory_error(ctx, "out of memory (module cache)");
  }
  module_path = NULL;
  tracked_free(ctx, module_path);
  string_array_clear(ctx, &segments, &segment_count, &segment_capacity);
  fe_restoregc(ctx, gc_save);
  tracked_free(ctx, module_spec);

  return module_obj;
}

static fe_Object* import_module(fe_Context *ctx, fe_Object *spec_obj) {
  char *module_spec = import_spec_to_cstring(ctx, spec_obj);
  return import_module_spec(ctx, module_spec, 0);
}


static fe_Object* eval(fe_Context *ctx, fe_Object *obj, fe_Object *env, fe_Object **newenv) {
  fe_Object *fn, *arg, *res;
  fe_Object cl, *va, *vb;
  int n, gc;
  int in_func_tail = 0;

tail_call:
  /* If we're in a function's tail position and the expression to evaluate
     is (return X), strip the redundant return and evaluate X directly.
     In tail position, return is implicit — the return sentinel would just
     be unwrapped at the end of eval anyway. */
  if (in_func_tail && type(obj) == FE_TPAIR && car(obj) == ctx->return_sym) {
    fe_Object *ret_args = cdr(obj);
    obj = isnil(ret_args) ? &nil : car(ret_args);
  }
  check_eval_budget(ctx);
  if (ctx->max_eval_depth > 0 && ctx->current_eval_depth >= ctx->max_eval_depth) {
    fe_error(ctx, "eval recursion depth limit exceeded");
  }
  ctx->current_eval_depth++;

  if (type(obj) == FE_TSYMBOL) { ctx->current_eval_depth--; return cdr(getbound(ctx, obj, env)); }
  if (type(obj) != FE_TPAIR) { ctx->current_eval_depth--; return obj; }

  /* Call stack frames are synthetic pair cells that live on the C stack. */
  tag(&cl) = 0;
  car(&cl) = obj, cdr(&cl) = ctx->calllist;
  ctx->calllist = &cl;

  gc = fe_savegc(ctx);
  fe_pushgc(ctx, env);
  fe_pushgc(ctx, obj);
  fn = eval(ctx, car(obj), env, NULL);
  arg = cdr(obj);
  res = &nil;

  switch (type(fn)) {
    case FE_TPRIM:
      switch (prim(fn)) {
        case P_MODULE: {
          /* form: (module "name" body) or (module symbol body) */
          fe_Object *name_obj = fe_nextarg(ctx, &arg);
          fe_Object *body = fe_nextarg(ctx, &arg);
          fe_Object *name_sym;

          if (type(name_obj) == FE_TSYMBOL) {
            name_sym = name_obj;
          } else if (type(name_obj) == FE_TSTRING) {
            if (fe_string_contains_nul(ctx, name_obj)) {
              fe_error(ctx, "module: name strings cannot contain NUL bytes");
            }
            name_sym = fe_symbol_from_string_obj(ctx, name_obj);
          } else {
            checktype(ctx, name_obj, FE_TSTRING);
            name_sym = &nil;
          }

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
          fe_set(ctx, name_sym, exports);
          res = exports;
          break;
        }
        case P_EXPORT: {
          /* form: (export (let name value)) */
          if (isnil(ctx->modulestack)) fe_error(ctx, "export outside of module");

          fe_Object *decl = fe_nextarg(ctx, &arg);
          fe_Object *name_sym = NULL;
          fe_Object *exports = fe_car(ctx, ctx->modulestack);

          if (type(decl) == FE_TSYMBOL) {
            name_sym = decl;
          } else if (type(decl) == FE_TPAIR && car(decl) == ctx->let_sym) {
            name_sym = fe_car(ctx, fe_cdr(ctx, decl));
          } else if (type(decl) == FE_TPAIR && car(decl) == ctx->do_sym) {
            fe_Object *exprs = cdr(decl);
            fe_Object *set_sym = fe_symbol(ctx, "=");

            if (type(exprs) == FE_TPAIR) {
              fe_Object *first = car(exprs);
              if (type(first) == FE_TPAIR && car(first) == set_sym) {
                name_sym = fe_car(ctx, fe_cdr(ctx, first));
              }
            }

            if (!name_sym) {
              fe_Object *tail_exprs = exprs;
              while (type(tail_exprs) == FE_TPAIR && !isnil(tail_exprs)) {
                if (isnil(cdr(tail_exprs))) {
                  name_sym = car(tail_exprs);
                  break;
                }
                tail_exprs = cdr(tail_exprs);
              }
            }
          }

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
            fe_error(ctx, "property access is only supported on maps/modules and pairs");
          }
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
            res = fe_cons(ctx, ctx->return_sym, va);
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
            if (type(*newenv) == FE_TPAIR && car(*newenv) == ctx->frame_sym) {
                fe_Object *locals = car(cdr(*newenv));
                fe_Object *upvals = cdr(cdr(*newenv));
                fe_Object *new_locals = fe_cons(ctx, binding, locals);
                new_frame_env = fe_cons(ctx, new_locals, upvals);
                new_frame_env = fe_cons(ctx, ctx->frame_sym, new_frame_env);
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
          cdr(getbound(ctx, va, env)) = evalarg();
          break;

        case P_IF:
          while (!isnil(arg)) {
            /* If this is the last element (else clause with no body),
               tail-call it instead of evaluating eagerly */
            if (isnil(cdr(arg))) {
              obj = fe_nextarg(ctx, &arg);
              fe_restoregc(ctx, gc);
              ctx->calllist = cdr(&cl);
              ctx->current_eval_depth--;
              goto tail_call;
            }
            va = evalarg();
            if (fe_truthy(va)) {
              /* Tail-call: evaluate the taken branch via trampoline */
              obj = fe_nextarg(ctx, &arg);
              fe_restoregc(ctx, gc);
              ctx->calllist = cdr(&cl);
              ctx->current_eval_depth--;
              goto tail_call;
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
              fe_restoregc(ctx, s);
              fe_pushgc(ctx, bound);
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
          fe_restoregc(ctx, s);
          fe_pushgc(ctx, free_vars);
          fe_pushgc(ctx, params);
          fe_pushgc(ctx, body);
          fe_pushgc(ctx, env);
          fe_pushgc(ctx, va);
          va = fe_cons(ctx, params, va);
          fe_restoregc(ctx, s);
          fe_pushgc(ctx, free_vars);
          fe_pushgc(ctx, params);
          fe_pushgc(ctx, body);
          fe_pushgc(ctx, env);
          fe_pushgc(ctx, va);
          va = fe_cons(ctx, free_vars, va);
          fe_restoregc(ctx, s);
          fe_pushgc(ctx, free_vars);
          fe_pushgc(ctx, params);
          fe_pushgc(ctx, body);
          fe_pushgc(ctx, env);
          fe_pushgc(ctx, va);
          va = fe_cons(ctx, env, va); /* Prepend definition env to the list */
          fe_restoregc(ctx, s);
          fe_pushgc(ctx, va);

          res = object(ctx);
          settype(res, prim(fn) == P_FN ? FE_TFUNC : FE_TMACRO);
          cdr(res) = va;
          break;
        }

        case P_WHILE: {
          fe_Object *cond_expr = fe_nextarg(ctx, &arg);
          n = fe_savegc(ctx);
          while (fe_truthy(eval(ctx, cond_expr, env, NULL))) {
            /* Inline body evaluation: eval all but last normally,
               then tail-call the last expression. */
            fe_Object *body_cur = arg;
            int wsave = fe_savegc(ctx);
            while (!isnil(body_cur)) {
              fe_Object *expr = fe_nextarg(ctx, &body_cur);
              if (isnil(body_cur)) {
                /* Last body expression: eval directly (not goto, since
                   we need to loop back for the condition check). */
                fe_restoregc(ctx, wsave);
                fe_pushgc(ctx, arg);
                fe_pushgc(ctx, env);
                res = eval(ctx, expr, env, &env);
                break;
              }
              fe_restoregc(ctx, wsave);
              fe_pushgc(ctx, body_cur);
              fe_pushgc(ctx, env);
              res = eval(ctx, expr, env, &env);
              if (is_return_obj(ctx, res)) { break; }
            }
            if (is_return_obj(ctx, res)) { break; }
            fe_restoregc(ctx, n);
          }
          break;
        }

        case P_QUOTE:
          res = fe_nextarg(ctx, &arg);
          break;

        case P_AND:
          while (!isnil(arg) && fe_truthy(res = evalarg()));
          break;

        case P_OR:
          while (!isnil(arg) && !fe_truthy(res = evalarg()));
          break;

        case P_DO: {
          /* Evaluate all but last expression, then tail-call the last */
          int save = fe_savegc(ctx);
          while (!isnil(arg)) {
            fe_Object *expr = fe_nextarg(ctx, &arg);
            if (isnil(arg)) {
              /* Last expression: tail-call */
              fe_restoregc(ctx, gc);
              ctx->calllist = cdr(&cl);
              ctx->current_eval_depth--;
              obj = expr;
              goto tail_call;
            }
            fe_restoregc(ctx, save);
            fe_pushgc(ctx, arg);
            fe_pushgc(ctx, env);
            res = eval(ctx, expr, env, &env);
            if (is_return_obj(ctx, res)) { break; }
          }
          break;
        }

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
        case P_GT: numcmpop(>); break;
        case P_GTE: numcmpop(>=); break;
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
        case P_DIV: {
          fe_Number x = nval(checknum(ctx, evalarg()));
          while (!isnil(arg)) {
            fe_Number d = nval(checknum(ctx, evalarg()));
            if (d == 0) { fe_error(ctx, "division by zero"); }
            x = x / d;
          }
          res = fe_make_number(ctx, x);
          break;
        }
      }
      break;

    case FE_TCFUNC:
      arg = evallist(ctx, arg, env);
      fe_pushgc(ctx, arg);
      res = cfunc(fn)(ctx, arg);
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
      fe_pushgc(ctx, arg);
      for (p = free_vars; !isnil(p); p = cdr(p)) {
        fe_Object *sym = car(p);
        fe_Object *binding = getbound(ctx, sym, def_env);
        fe_restoregc(ctx, s);
        fe_pushgc(ctx, upvals);
        fe_pushgc(ctx, def_env);
        fe_pushgc(ctx, arg);
        fe_pushgc(ctx, binding);
        upvals = fe_cons(ctx, binding, upvals);
      }
      fe_restoregc(ctx, s);

      fe_pushgc(ctx, upvals);
      fe_pushgc(ctx, arg);

      /* Create local environment and frame while rooting intermediates. */
      fe_Object *locals;
      fe_Object *frame;
      int frame_save = fe_savegc(ctx);
      fe_pushgc(ctx, upvals);
      fe_pushgc(ctx, arg);
      locals = argstoenv(ctx, params, arg, &nil);
      fe_pushgc(ctx, locals);
      frame = fe_cons(ctx, locals, upvals);
      fe_pushgc(ctx, frame);
      frame = fe_cons(ctx, ctx->frame_sym, frame);
      fe_restoregc(ctx, frame_save);

      /* Inline body evaluation with tail-call optimization:
         evaluate all but the last expression normally, then
         trampoline the last expression via goto tail_call. */
      {
        fe_Object *body_cur = body;
        int save = fe_savegc(ctx);
        res = &nil;
        while (!isnil(body_cur)) {
          fe_Object *expr = fe_nextarg(ctx, &body_cur);
          if (isnil(body_cur)) {
            /* Last body expression: tail-call via trampoline */
            fe_restoregc(ctx, gc);
            ctx->calllist = cdr(&cl);
            ctx->current_eval_depth--;
            obj = expr;
            env = frame;
            in_func_tail = 1;
            goto tail_call;
          }
          fe_restoregc(ctx, save);
          fe_pushgc(ctx, body_cur);
          fe_pushgc(ctx, frame);
          res = eval(ctx, expr, frame, &frame);
          if (is_return_obj(ctx, res)) { res = cdr(res); break; }
        }
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
      fe_pushgc(ctx, arg);
      for (p = free_vars; !isnil(p); p = cdr(p)) {
        fe_Object *sym = car(p);
        fe_Object *binding = getbound(ctx, sym, def_env);
        fe_restoregc(ctx, s);
        fe_pushgc(ctx, upvals);
        fe_pushgc(ctx, def_env);
        fe_pushgc(ctx, arg);
        fe_pushgc(ctx, binding);
        upvals = fe_cons(ctx, binding, upvals);
      }
      fe_restoregc(ctx, s);

      fe_Object *locals;
      fe_Object *frame;
      int frame_save = fe_savegc(ctx);
      fe_pushgc(ctx, upvals);
      fe_pushgc(ctx, arg);
      locals = argstoenv(ctx, params, arg, &nil);
      fe_pushgc(ctx, locals);
      frame = fe_cons(ctx, locals, upvals);
      fe_pushgc(ctx, frame);
      frame = fe_cons(ctx, ctx->frame_sym, frame);
      fe_restoregc(ctx, frame_save);

      {
        int save = fe_savegc(ctx);
        fe_pushgc(ctx, frame);
        *obj = *dolist(ctx, body, frame);
        fe_restoregc(ctx, save);
      }
      fe_restoregc(ctx, gc);
      ctx->calllist = cdr(&cl);
      ctx->current_eval_depth--;
      return eval(ctx, obj, env, NULL);
    }

    default:
      fe_error(ctx, "tried to call non-callable value");
  }

  fe_restoregc(ctx, gc);
  fe_pushgc(ctx, res);
  ctx->calllist = cdr(&cl);
  ctx->current_eval_depth--;
  /* Unwrap return sentinel when we're in a function's tail-call chain.
     The trampoline skips the normal FE_TFUNC return_obj unwrap, so we
     catch it here instead. */
  if (in_func_tail && is_return_obj(ctx, res)) {
    res = cdr(res);
  }
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

  /* init recursion depth limits */
  ctx->current_eval_depth = 0;
  ctx->max_eval_depth = FE_DEFAULT_MAX_EVAL_DEPTH;
  ctx->current_read_depth = 0;
  ctx->max_read_depth = FE_DEFAULT_MAX_READ_DEPTH;

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

  /* --- Initialize per-context symbols for closures and analysis --- */
  ctx->return_sym = fe_symbol(ctx, "return");
  ctx->frame_sym = fe_symbol(ctx, "[frame]");
  ctx->do_sym = fe_symbol(ctx, "do");
  ctx->let_sym = fe_symbol(ctx, "let");
  ctx->quote_sym = fe_symbol(ctx, "quote");
  ctx->fn_sym = fe_symbol(ctx, "fn");
  ctx->mac_sym = fe_symbol(ctx, "mac");

  return ctx;
}


void fe_close(fe_Context *ctx) {
  fex_temp_cleanup_all(ctx);
  fex_span_cleanup(ctx);
  string_array_clear(ctx, &ctx->import_paths, &ctx->import_path_count, &ctx->import_path_capacity);
  string_array_clear(ctx, &ctx->source_dirs, &ctx->source_dir_count, &ctx->source_dir_capacity);
  string_array_clear(ctx, &ctx->source_buffers, &ctx->source_buffer_count, &ctx->source_buffer_capacity);
  string_array_clear(ctx, &ctx->loading_modules, &ctx->loading_module_count, &ctx->loading_module_capacity);
  module_cache_clear(ctx);
  /* clear gcstack and symlist; makes all objects unreachable */
  ctx->gcstack_idx = 0;
  ctx->symlist = &nil;
  collectgarbage(ctx);
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
