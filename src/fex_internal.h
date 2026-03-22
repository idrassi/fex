#ifndef FEX_INTERNAL_H
#define FEX_INTERNAL_H

#include "fex.h"

/* Thread-local storage specifier for compiler portability */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  #define FEX_THREAD_LOCAL _Thread_local
#elif defined(_MSC_VER)
  #define FEX_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
  #define FEX_THREAD_LOCAL __thread
#else
  #define FEX_THREAD_LOCAL /* no thread-local support; single-threaded fallback */
#endif

fe_Object* fex_compile_named(fe_Context *ctx, const char *source,
                             const char *source_name);
fe_Object* fex_do_string_named(fe_Context *ctx, const char *source,
                               const char *source_name);
void fex_compile_cleanup_ctx(fe_Context *ctx);
int fex_try_is_active(void);
void fex_try_raise(FexStatus status, const char *source_name,
                   int line, int column, const char *message);
void fex_try_raise_error(const FexError *error);
FexStatus fex_try_run_internal(fe_Context *ctx, fe_Object **out_result,
                               FexError *out_error,
                               fe_Object *(*fn)(fe_Context *ctx, const void *a, const void *b),
                               const void *arg_a, const void *arg_b,
                               const char *source_name,
                               int preserve_result_root);

/* Per-context span state accessors (implemented in fe.c) */
void *fe_ctx_span_state(fe_Context *ctx);
void fe_ctx_set_span_state(fe_Context *ctx, void *state);
void *fe_ctx_temp_allocs(fe_Context *ctx);
void fe_ctx_set_temp_allocs(fe_Context *ctx, void *state);
void *fe_ctx_tracked_alloc(fe_Context *ctx, size_t size);
void *fe_ctx_tracked_realloc(fe_Context *ctx, void *ptr, size_t size);
void fe_ctx_tracked_free(fe_Context *ctx, void *ptr);
void fe_ctx_memory_error(fe_Context *ctx, const char *fallback_msg);
int fe_ctx_object_is_live(fe_Context *ctx, const fe_Object *obj);

/* Per-context span lifecycle (implemented in fex_span.c) */
void fex_span_init(fe_Context *ctx);
void fex_span_cleanup(fe_Context *ctx);
void fex_span_prune(fe_Context *ctx);
void fex_temp_cleanup_all(fe_Context *ctx);

#endif
