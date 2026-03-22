/*
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

#ifndef FEX_H
#define FEX_H

#include "fe.h"
#include "fex_builtins.h"

/*
 * Configuration flags for FeX
 */
typedef enum {
    FEX_CONFIG_NONE = 0,
    FEX_CONFIG_ENABLE_SPANS = 1 << 0,
    FEX_CONFIG_ENABLE_EXTENDED_BUILTINS = 1 << 1,
} FexConfig;

typedef enum {
    FEX_STATUS_OK = 0,
    FEX_STATUS_COMPILE_ERROR,
    FEX_STATUS_RUNTIME_ERROR,
    FEX_STATUS_IO_ERROR
} FexStatus;

#define FEX_ERROR_MESSAGE_MAX 256
#define FEX_ERROR_SOURCE_NAME_MAX 260
#define FEX_ERROR_EXPRESSION_MAX 128
#define FEX_ERROR_TRACE_MAX 8

typedef struct {
    char source_name[FEX_ERROR_SOURCE_NAME_MAX];
    int line;
    int column;
    char expression[FEX_ERROR_EXPRESSION_MAX];
} FexErrorFrame;

typedef struct {
    FexStatus status;
    char message[FEX_ERROR_MESSAGE_MAX];
    char source_name[FEX_ERROR_SOURCE_NAME_MAX];
    int line;
    int column;
    int frame_count;
    FexErrorFrame frames[FEX_ERROR_TRACE_MAX];
} FexError;

/*
 * Threading: The fex_try_* APIs use a thread-local error scope internally.
 * Each thread may use its own fe_Context with fex_try_* concurrently,
 * provided the platform supports thread-local storage (_Thread_local or
 * __thread).  See doc/capi.md "Threading and Re-entrancy" for details.
 */

/*
 * Initializes the FeX environment, registering custom built-in
 * functions like 'print'. Must be called after fe_open().
 */
void fex_init(fe_Context *ctx);

/*
 * Initializes the FeX environment with configuration options.
 */
void fex_init_with_config(fe_Context *ctx, FexConfig config);

/*
 * Initializes the FeX environment with configuration options and a selective
 * extended-builtin mask. If `builtins` is `FEX_BUILTINS_NONE`, the legacy
 * `FEX_CONFIG_ENABLE_EXTENDED_BUILTINS` flag still enables all builtins.
 */
void fex_init_with_builtins(fe_Context *ctx, FexConfig config,
                            FexBuiltinsConfig builtins);

/* 
 * Compiles a string of source code in the modern syntax into an
 * evaluatable fe_Object.
 */
fe_Object* fex_compile(fe_Context *ctx, const char *source);

/*
 * Compiles source and reports structured errors instead of printing and
 * continuing. `source_name` is used in diagnostics; pass NULL for "<string>".
 */
FexStatus fex_try_compile(fe_Context *ctx, const char *source,
                          const char *source_name, fe_Object **out_code,
                          FexError *out_error);

/*
 * A convenience function that compiles and then evaluates a string
 * of source code.
 */
fe_Object* fex_do_string(fe_Context *ctx, const char *source);

/*
 * Evaluates a precompiled AST and reports runtime errors without terminating
 * the process.
 */
FexStatus fex_try_eval(fe_Context *ctx, fe_Object *obj, fe_Object **out_result,
                       FexError *out_error);

/*
 * Compiles and evaluates a string of source code without terminating the
 * process on compile/runtime errors.
 */
FexStatus fex_try_do_string(fe_Context *ctx, const char *source,
                            fe_Object **out_result, FexError *out_error);

/*
 * Like `fex_try_do_string`, but preserves a caller-supplied source name in
 * diagnostics.
 */
FexStatus fex_try_do_string_named(fe_Context *ctx, const char *source,
                                  const char *source_name,
                                  fe_Object **out_result, FexError *out_error);

/*
 * Reads, compiles, and evaluates a source file. Relative imports from that
 * file resolve against the file's directory.
 */
fe_Object* fex_do_file(fe_Context *ctx, const char *path);

/*
 * Reads, compiles, and evaluates a source file without terminating the
 * process on I/O, compile, or runtime errors.
 */
FexStatus fex_try_do_file(fe_Context *ctx, const char *path,
                          fe_Object **out_result, FexError *out_error);

/*
 * Adds an import search path for file-based `import` resolution.
 * Returns non-zero on success.
 */
int fex_add_import_path(fe_Context *ctx, const char *path);

/*
 * Clears all configured import search paths.
 */
void fex_clear_import_paths(fe_Context *ctx);

/*
 * Clears transient import execution state after a recoverable error.
 * This leaves configured import paths and loaded-module cache intact.
 */
void fex_reset_import_state(fe_Context *ctx);

/*
 * Initializes an error object to the empty/success state.
 */
void fex_error_clear(FexError *error);

/*
 * Prints a structured error in a human-readable format.
 */
void fex_print_error(FILE *fp, const FexError *error);

#endif
