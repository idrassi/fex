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
 * Compiles a string of source code in the modern syntax into an
 * evaluatable fe_Object.
 */
fe_Object* fex_compile(fe_Context *ctx, const char *source);

/*
 * A convenience function that compiles and then evaluates a string
 * of source code.
 */
fe_Object* fex_do_string(fe_Context *ctx, const char *source);

/*
 * Reads, compiles, and evaluates a source file. Relative imports from that
 * file resolve against the file's directory.
 */
fe_Object* fex_do_file(fe_Context *ctx, const char *path);

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

#endif
