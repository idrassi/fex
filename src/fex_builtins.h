/*
** Extended Built-in Functions for FeX Programming Language
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

#ifndef FEX_BUILTINS_H
#define FEX_BUILTINS_H

#include "fe.h"

/*
 * Extended built-in functions for FeX programming language.
 * These functions provide practical functionality for real-world programming.
 * 
 * To enable these builtins, call fex_init_extended_builtins() after fex_init().
 */

/*
 * Configuration flags for extended builtins
 */
typedef enum {
    FEX_BUILTINS_NONE = 0,
    FEX_BUILTINS_MATH = 1 << 0,        /* Mathematical functions */
    FEX_BUILTINS_STRING = 1 << 1,      /* String manipulation */
    FEX_BUILTINS_LIST = 1 << 2,        /* List operations */
    FEX_BUILTINS_IO = 1 << 3,          /* File I/O operations */
    FEX_BUILTINS_SYSTEM = 1 << 4,      /* System operations */
    FEX_BUILTINS_TYPE = 1 << 5,        /* Type checking and conversion */
    FEX_BUILTINS_ALL = 0x3F            /* All builtins */
} FexBuiltinsConfig;

/* Error handling utilities */
#define FEX_CHECK_ARGS(ctx, args, min_count, func_name) \
    do { \
        int count = 0; \
        fe_Object *temp = args; \
        while (!fe_isnil(ctx, temp)) { count++; temp = fe_cdr(ctx, temp); } \
        if (count < min_count) { \
            fe_error(ctx, func_name ": insufficient arguments"); \
            return fe_nil(ctx); \
        } \
    } while(0)

#define FEX_CHECK_NO_ARGS(ctx, args, func_name) \
    do { \
        if (!fe_isnil(ctx, args)) { \
            fe_error(ctx, func_name ": no arguments expected"); \
            return fe_nil(ctx); \
        } \
    } while(0)

#define FEX_CHECK_TYPE(ctx, obj, expected_type, func_name) \
    do { \
        if (fe_type(ctx, obj) != expected_type) { \
            fe_error(ctx, func_name ": type mismatch"); \
            return fe_nil(ctx); \
        } \
    } while(0)

/*
 * Initialize extended built-in functions with specified configuration.
 * Call this after fex_init() to register additional built-ins.
 */
void fex_init_extended_builtins(fe_Context *ctx, FexBuiltinsConfig config);

/*
 * Initialize all extended built-in functions.
 * Convenience function equivalent to fex_init_extended_builtins(ctx, FEX_BUILTINS_ALL).
 */
void fex_init_all_builtins(fe_Context *ctx);

#endif
