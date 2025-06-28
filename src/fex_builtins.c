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

/*
** This module provides a comprehensive set of built-in functions
** to make FeX more practical for real-world programming tasks.
**
** Categories:
** - Mathematical functions (sin, cos, sqrt, etc.)
** - String manipulation (length, substring, split, etc.)
** - List operations (map, filter, reduce, etc.)
** - File I/O operations (readfile, writefile, etc.)
** - System operations (time, exit, etc.)
** - Type checking and conversion (typeof, tostring, etc.)
*/

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

#include "fex_builtins.h"
#include "sfc32.h"

/*
================================================================================
|                            MATHEMATICAL FUNCTIONS                            |
================================================================================
*/

static fe_Object* builtin_sqrt(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "sqrt");
    fe_Object *arg = fe_nextarg(ctx, &args);
    fe_Number n = fe_tonumber(ctx, arg);
    if (n < 0.0) {
        fe_error(ctx, "sqrt: negative argument");
        return fe_nil(ctx);
    }
    return fe_make_number(ctx, sqrt(n));
}

static fe_Object* builtin_sin(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "sin");
    fe_Object *arg = fe_nextarg(ctx, &args);
    fe_Number n = fe_tonumber(ctx, arg);
    return fe_make_number(ctx, sin(n));
}

static fe_Object* builtin_cos(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "cos");
    fe_Object *arg = fe_nextarg(ctx, &args);
    fe_Number n = fe_tonumber(ctx, arg);
    return fe_make_number(ctx, cos(n));
}

static fe_Object* builtin_tan(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "tan");
    fe_Object *arg = fe_nextarg(ctx, &args);
    fe_Number n = fe_tonumber(ctx, arg);
    return fe_make_number(ctx, tan(n));
}

static fe_Object* builtin_abs(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "abs");
    fe_Object *arg = fe_nextarg(ctx, &args);
    fe_Number n = fe_tonumber(ctx, arg);
    return fe_make_number(ctx, fabs(n));
}

static fe_Object* builtin_floor(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "floor");
    fe_Object *arg = fe_nextarg(ctx, &args);
    fe_Number n = fe_tonumber(ctx, arg);
    return fe_make_number(ctx, floor(n));
}

static fe_Object* builtin_ceil(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "ceil");
    fe_Object *arg = fe_nextarg(ctx, &args);
    fe_Number n = fe_tonumber(ctx, arg);
    return fe_make_number(ctx, ceil(n));
}

static fe_Object* builtin_round(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "round");
    fe_Object *arg = fe_nextarg(ctx, &args);
    fe_Number n = fe_tonumber(ctx, arg);
    return fe_make_number(ctx, round(n));
}

static fe_Object* builtin_min(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "min");
    
    fe_Object *first = fe_nextarg(ctx, &args);
    fe_Number result = fe_tonumber(ctx, first);
    
    while (!fe_isnil(ctx, args)) {
        fe_Object *arg = fe_nextarg(ctx, &args);
        fe_Number n = fe_tonumber(ctx, arg);
        if (n < result) result = n;
    }
    return fe_make_number(ctx, result);
}

static fe_Object* builtin_max(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "max");
    
    fe_Object *first = fe_nextarg(ctx, &args);
    fe_Number result = fe_tonumber(ctx, first);
    
    while (!fe_isnil(ctx, args)) {
        fe_Object *arg = fe_nextarg(ctx, &args);
        fe_Number n = fe_tonumber(ctx, arg);
        if (n > result) result = n;
    }
    return fe_make_number(ctx, result);
}

static fe_Object* builtin_pow(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "pow");
    fe_Object *base = fe_nextarg(ctx, &args);
    fe_Object *exp = fe_nextarg(ctx, &args);
    fe_Number b = fe_tonumber(ctx, base);
    fe_Number e = fe_tonumber(ctx, exp);
    return fe_make_number(ctx, pow(b, e));
}

static fe_Object* builtin_log(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "log");
    fe_Object *arg = fe_nextarg(ctx, &args);
    fe_Number n = fe_tonumber(ctx, arg);
    if (n <= 0.0) {
        fe_error(ctx, "log: argument must be positive");
        return fe_nil(ctx);
    }
    return fe_make_number(ctx, log(n));
}

static int seeded = 0;
static sfc32_state rng_state;

static fe_Object* builtin_random(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_NO_ARGS(ctx, args, "rand");
    if (!seeded) {
        sfc32_seed(&rng_state, (uint32_t)time(NULL));
        seeded = 1;
    }
    return fe_make_number(ctx, (fe_Number)sfc32_next(&rng_state) / (fe_Number)UINT32_MAX);
}

static fe_Object* builtin_seed_random(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "seedrand");
    fe_Object *seed_obj = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, seed_obj, FE_TNUMBER, "seedrand");
    fe_Number seed_num = fe_tonumber(ctx, seed_obj);
    
    uint32_t seed = (uint32_t)seed_num;
    sfc32_seed(&rng_state, seed);
    seeded = 1;
    
    return fe_nil(ctx);
}

static fe_Object* builtin_random_int(fe_Context *ctx, fe_Object *args) {
    if (!seeded) {
        sfc32_seed(&rng_state, (uint32_t)time(NULL));
        seeded = 1;
    }
    
    /* If no arguments, return full uint32_t value */
    if (fe_isnil(ctx, args)) {
        uint32_t random_val = sfc32_next(&rng_state);
        return fe_make_number(ctx, (fe_Number)random_val);
    }
    
    /* If argument provided, use it as maximum */
    FEX_CHECK_ARGS(ctx, args, 1, "randint");
    fe_Object *max_obj = fe_nextarg(ctx, &args);
    fe_Number max_num = fe_tonumber(ctx, max_obj);
    
    if (max_num <= 0) {
        fe_error(ctx, "randint: maximum must be positive");
        return fe_nil(ctx);
    }
    
    uint32_t max_val = (uint32_t)max_num;
    uint32_t random_val = sfc32_next(&rng_state) % max_val;
    
    return fe_make_number(ctx, (fe_Number)random_val);
}

static fe_Object* builtin_random_bytes(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "randbytes");
    fe_Object *count_obj = fe_nextarg(ctx, &args);
    fe_Number count_num = fe_tonumber(ctx, count_obj);
    
    if (count_num <= 0 || count_num > 1024) {
        fe_error(ctx, "randbytes: count must be between 1 and 1024");
        return fe_nil(ctx);
    }
    
    if (!seeded) {
        sfc32_seed(&rng_state, (uint32_t)time(NULL));
        seeded = 1;
    }
    
    int count = (int)count_num, i;
    fe_Object *result = fe_nil(ctx);
    fe_Object **tail = &result;
    
    for (i = 0; i < count; i++) {
        uint32_t random_val = sfc32_next(&rng_state);
        uint8_t byte_val = (uint8_t)(random_val & 0xFF);
        
        *tail = fe_cons(ctx, fe_make_number(ctx, (fe_Number)byte_val), fe_nil(ctx));
        tail = fe_cdr_ptr(ctx, *tail);
    }
    
    return result;
}

/*
================================================================================
|                            STRING FUNCTIONS                                  |
================================================================================
*/

static fe_Object* builtin_string_length(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "strlen");
    fe_Object *str = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "strlen");

    char buffer[1024];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    return fe_make_number(ctx, (fe_Number)strlen(buffer));
}

static fe_Object* builtin_string_upper(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "upper");
    fe_Object *str = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "upper");

    char buffer[1024];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    
    int i;
    for (i = 0; buffer[i]; i++) {
        buffer[i] = toupper(buffer[i]);
    }
    
    return fe_string(ctx, buffer);
}

static fe_Object* builtin_string_lower(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "lower");
    fe_Object *str = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "lower");

    char buffer[1024];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    
    int i;
    for (i = 0; buffer[i]; i++) {
        buffer[i] = tolower(buffer[i]);
    }
    
    return fe_string(ctx, buffer);
}

static fe_Object* builtin_string_concat(fe_Context *ctx, fe_Object *args) {
    /* Use dynamic allocation for large concatenations */
    size_t capacity = 1024;
    char *result = malloc(capacity);
    if (!result) {
        fe_error(ctx, "concat: out of memory");
        return fe_nil(ctx);
    }
    
    size_t result_len = 0;
    result[0] = '\0';
    
    while (!fe_isnil(ctx, args)) {
        fe_Object *arg = fe_nextarg(ctx, &args);
        char buffer[1024];
        fe_tostring(ctx, arg, buffer, sizeof(buffer));
        
        size_t buffer_len = strlen(buffer);
        
        /* Grow buffer if needed */
        if (result_len + buffer_len >= capacity) {
            capacity = (result_len + buffer_len + 1) * 2;
            char *new_result = realloc(result, capacity);
            if (!new_result) {
                free(result);
                fe_error(ctx, "concat: out of memory");
                return fe_nil(ctx);
            }
            result = new_result;
        }
        
        strcpy(result + result_len, buffer);
        result_len += buffer_len;
    }
    
    fe_Object *obj = fe_string(ctx, result);
    free(result);
    return obj;
}

static fe_Object* builtin_string_substring(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "substring");
    fe_Object *str = fe_nextarg(ctx, &args);
    fe_Object *start_obj = fe_nextarg(ctx, &args);
    fe_Object *end_obj = fe_nextarg(ctx, &args);
    
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "substring");
    
    char buffer[1024];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    
    int len = (int) strlen(buffer);
    int start = (int)fe_tonumber(ctx, start_obj);
    int end = fe_isnil(ctx, end_obj) ? len : (int)fe_tonumber(ctx, end_obj);
    
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return fe_string(ctx, "");
    
    char result[1024];
    int result_len = end - start;
    strncpy(result, buffer + start, result_len);
    result[result_len] = '\0';
    
    return fe_string(ctx, result);
}

static fe_Object* builtin_string_split(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "split");
    fe_Object *str = fe_nextarg(ctx, &args);
    fe_Object *delim = fe_nextarg(ctx, &args);
    
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "split");
    
    char buffer[1024], delimiter[64];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    fe_tostring(ctx, delim, delimiter, sizeof(delimiter));
    
    fe_Object *result = fe_nil(ctx);
    fe_Object **tail = &result;
    
    char *token = strtok(buffer, delimiter);
    while (token != NULL) {
        *tail = fe_cons(ctx, fe_string(ctx, token), fe_nil(ctx));
        tail = fe_cdr_ptr(ctx, *tail);
        token = strtok(NULL, delimiter);
    }
    
    return result;
}

static fe_Object* builtin_string_trim(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "trim");
    fe_Object *str = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "trim");
    
    char buffer[1024];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    
    /* Trim leading whitespace */
    char *start = buffer;
    while (isspace(*start)) start++;
    
    /* Trim trailing whitespace */
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    
    *(end + 1) = '\0';
    return fe_string(ctx, start);
}

static fe_Object* builtin_string_contains(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "contains");
    fe_Object *str = fe_nextarg(ctx, &args);
    fe_Object *substr = fe_nextarg(ctx, &args);
    
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "contains");
    FEX_CHECK_TYPE(ctx, substr, FE_TSTRING, "contains");
    
    char buffer[1024], search[256];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    fe_tostring(ctx, substr, search, sizeof(search));
    
    return fe_bool(ctx, strstr(buffer, search) != NULL);
}

/*
================================================================================
|                              LIST FUNCTIONS                                 |
================================================================================
*/

static fe_Object* builtin_list_length(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "length");
    fe_Object *list = fe_nextarg(ctx, &args);
    int count = 0;
    FEX_CHECK_TYPE(ctx, list, FE_TPAIR, "contains");
    
    while (!fe_isnil(ctx, list)) {
        count++;
        list = fe_cdr(ctx, list);
    }
    
    return fe_make_number(ctx, (fe_Number)count);
}

static fe_Object* builtin_list_nth(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "nth");
    fe_Object *list = fe_nextarg(ctx, &args);
    fe_Object *index_obj = fe_nextarg(ctx, &args);
    
    int index = (int)fe_tonumber(ctx, index_obj);
    int i;

    FEX_CHECK_TYPE(ctx, list, FE_TPAIR, "nth");

    for (i = 0; i < index && !fe_isnil(ctx, list); i++) {
        list = fe_cdr(ctx, list);
    }
    
    if (fe_isnil(ctx, list)) {
        return fe_nil(ctx);
    }
    
    return fe_car(ctx, list);
}

static fe_Object* builtin_list_append(fe_Context *ctx, fe_Object *args) {
    if (fe_isnil(ctx, args)) return fe_nil(ctx);
    
    fe_Object *first = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, first, FE_TPAIR, "nth");
    if (fe_isnil(ctx, args)) return first;
    
    /* Build result list by copying first list and appending rest */
    fe_Object *result = fe_nil(ctx);
    fe_Object **tail = &result;
    
    /* Copy first list */
    fe_Object *current = first;
    while (!fe_isnil(ctx, current)) {
        *tail = fe_cons(ctx, fe_car(ctx, current), fe_nil(ctx));
        tail = fe_cdr_ptr(ctx, *tail);
        current = fe_cdr(ctx, current);
    }
    
    /* Append remaining lists */
    while (!fe_isnil(ctx, args)) {
        fe_Object *list = fe_nextarg(ctx, &args);
        FEX_CHECK_TYPE(ctx, list, FE_TPAIR, "nth");
        current = list;
        while (!fe_isnil(ctx, current)) {
            *tail = fe_cons(ctx, fe_car(ctx, current), fe_nil(ctx));
            tail = fe_cdr_ptr(ctx, *tail);
            current = fe_cdr(ctx, current);
        }
    }
    
    return result;
}

static fe_Object* builtin_list_reverse(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "reverse");
    fe_Object *list = fe_nextarg(ctx, &args);
    fe_Object *result = fe_nil(ctx);
    FEX_CHECK_TYPE(ctx, list, FE_TPAIR, "reverse");
    while (!fe_isnil(ctx, list)) {
        result = fe_cons(ctx, fe_car(ctx, list), result);
        list = fe_cdr(ctx, list);
    }
    
    return result;
}

static fe_Object* builtin_map(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "map");
    fe_Object *func = fe_nextarg(ctx, &args);
    fe_Object *list = fe_nextarg(ctx, &args);
    
    fe_Object *result = fe_nil(ctx);
    fe_Object **tail = &result;
    
    while (!fe_isnil(ctx, list)) {
        fe_Object *item = fe_car(ctx, list);
        fe_Object *call_args = fe_cons(ctx, item, fe_nil(ctx));
        fe_Object *call_expr = fe_cons(ctx, func, call_args);
        fe_Object *mapped = fe_eval(ctx, call_expr);
        
        *tail = fe_cons(ctx, mapped, fe_nil(ctx));
        tail = fe_cdr_ptr(ctx, *tail);
        list = fe_cdr(ctx, list);
    }
    
    return result;
}

static fe_Object* builtin_filter(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "filter");
    fe_Object *predicate = fe_nextarg(ctx, &args);
    fe_Object *list = fe_nextarg(ctx, &args);
    
    fe_Object *result = fe_nil(ctx);
    fe_Object **tail = &result;
    
    while (!fe_isnil(ctx, list)) {
        fe_Object *item = fe_car(ctx, list);
        fe_Object *call_args = fe_cons(ctx, item, fe_nil(ctx));
        fe_Object *call_expr = fe_cons(ctx, predicate, call_args);
        fe_Object *test_result = fe_eval(ctx, call_expr);
        
        if (!fe_isnil(ctx, test_result) && test_result != fe_bool(ctx, 0)) {
            *tail = fe_cons(ctx, item, fe_nil(ctx));
            tail = fe_cdr_ptr(ctx, *tail);
        }
        list = fe_cdr(ctx, list);
    }
    
    return result;
}

static fe_Object* builtin_fold(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 3, "fold");
    fe_Object *func = fe_nextarg(ctx, &args);
    fe_Object *init = fe_nextarg(ctx, &args);
    fe_Object *list = fe_nextarg(ctx, &args);
    
    fe_Object *acc = init;
    
    while (!fe_isnil(ctx, list)) {
        fe_Object *item = fe_car(ctx, list);
        fe_Object *call_args = fe_cons(ctx, item, fe_cons(ctx, acc, fe_nil(ctx)));
        fe_Object *call_expr = fe_cons(ctx, func, call_args);
        acc = fe_eval(ctx, call_expr);
        list = fe_cdr(ctx, list);
    }
    
    return acc;
}

/*
================================================================================
|                               I/O FUNCTIONS                                 |
================================================================================
*/

static fe_Object* builtin_read_file(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "readfile");
    fe_Object *filename_obj = fe_nextarg(ctx, &args);
    
    char filename[1024];
    fe_tostring(ctx, filename_obj, filename, sizeof(filename));
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fe_error(ctx, "readfile: could not open file");
        return fe_nil(ctx);
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    /* size validation and error handling */
    if (size < 0) {
        fclose(file);
        fe_error(ctx, "readfile: could not determine file size");
        return fe_nil(ctx);
    }
    
    if (size > 8 * 1024) { /* 8KB limit */
        fclose(file);
        fe_error(ctx, "readfile: file too large (max 8KB)");
        return fe_nil(ctx);
    }
    
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(file);
        fe_error(ctx, "readfile: out of memory");
        return fe_nil(ctx);
    }
    
    size_t bytes_read = fread(buffer, 1, size, file);
    /* Check for read errors */
    if (ferror(file)) {
        free(buffer);
        fclose(file);
        fe_error(ctx, "readfile: error reading file");
        return fe_nil(ctx);
    }
    
    buffer[bytes_read] = '\0';
    fclose(file);
    
    fe_Object *result = fe_string(ctx, buffer);
    free(buffer);
    
    return result;
}

static fe_Object* builtin_write_file(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "writefile");
    fe_Object *filename_obj = fe_nextarg(ctx, &args);
    fe_Object *content_obj = fe_nextarg(ctx, &args);
    
    char filename[1024];
    char content[4096];
    fe_tostring(ctx, filename_obj, filename, sizeof(filename));
    fe_tostring(ctx, content_obj, content, sizeof(content));
    
    FILE *file = fopen(filename, "wb");
    if (!file) {
        fe_error(ctx, "writefile: could not open file for writing");
        return fe_nil(ctx);
    }
    
    size_t written = fwrite(content, 1, strlen(content), file);
    fclose(file);
    
    return fe_make_number(ctx, (fe_Number)written);
}

/*
================================================================================
|                             SYSTEM FUNCTIONS                                |
================================================================================
*/

static fe_Object* builtin_time(fe_Context *ctx, fe_Object *args) {
    (void)args; /* Unused */
    time_t current_time = time(NULL);
    return fe_make_number(ctx, (fe_Number)current_time);
}

static fe_Object* builtin_exit(fe_Context *ctx, fe_Object *args) {
    int code = 0;
    if (!fe_isnil(ctx, args)) {
        fe_Object *code_obj = fe_nextarg(ctx, &args);
        code = (int)fe_tonumber(ctx, code_obj);
    }
    exit(code);
    return fe_nil(ctx); /* Never reached */
}

static fe_Object* builtin_system(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "system");
    fe_Object *command_obj = fe_nextarg(ctx, &args);
    
    char command[1024];
    fe_tostring(ctx, command_obj, command, sizeof(command));
    
    int result = system(command);
    return fe_make_number(ctx, (fe_Number)result);
}

/*
================================================================================
|                             TYPE FUNCTIONS                                  |
================================================================================
*/

static fe_Object* builtin_type_of(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "typeof");
    fe_Object *obj = fe_nextarg(ctx, &args);
    int type = fe_type(ctx, obj);
    
    const char *type_name;
    switch (type) {
        case FE_TNIL: type_name = "nil"; break;
        case FE_TNUMBER: type_name = "number"; break;
        case FE_TSTRING: type_name = "string"; break;
        case FE_TSYMBOL: type_name = "symbol"; break;
        case FE_TPAIR: type_name = "pair"; break;
        case FE_TFUNC: type_name = "function"; break;
        case FE_TMACRO: type_name = "macro"; break;
        case FE_TCFUNC: type_name = "cfunction"; break;
        case FE_TPTR: type_name = "pointer"; break;
        case FE_TBOOLEAN: type_name = "boolean"; break;
        default: type_name = "unknown"; break;
    }
    
    return fe_string(ctx, type_name);
}

static fe_Object* builtin_to_string(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "tostring");
    fe_Object *obj = fe_nextarg(ctx, &args);
    
    char buffer[1024];
    fe_tostring(ctx, obj, buffer, sizeof(buffer));
    
    return fe_string(ctx, buffer);
}

static fe_Object* builtin_to_number(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "tonumber");
    fe_Object *obj = fe_nextarg(ctx, &args);
    
    if (fe_type(ctx, obj) == FE_TNUMBER) {
        return obj;
    }
    
    if (fe_type(ctx, obj) == FE_TSTRING) {
        char buffer[1024];
        fe_tostring(ctx, obj, buffer, sizeof(buffer));
        
        char *endptr;
        double value = strtod(buffer, &endptr);
        
        if (*endptr != '\0') {
            fe_error(ctx, "tonumber: invalid number format");
            return fe_nil(ctx);
        }
        
        return fe_make_number(ctx, value);
    }
    
    fe_error(ctx, "tonumber: cannot convert to number");
    return fe_nil(ctx);
}

static fe_Object* builtin_is_nil(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "isnil");
    fe_Object *obj = fe_nextarg(ctx, &args);
    return fe_bool(ctx, fe_isnil(ctx, obj));
}

static fe_Object* builtin_is_number(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "isnumber");
    fe_Object *obj = fe_nextarg(ctx, &args);
    return fe_bool(ctx, fe_type(ctx, obj) == FE_TNUMBER || FE_IS_FIXNUM(obj));
}

static fe_Object* builtin_is_string(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "isstring");
    fe_Object *obj = fe_nextarg(ctx, &args);
    return fe_bool(ctx, fe_type(ctx, obj) == FE_TSTRING);
}

static fe_Object* builtin_is_list(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "islist");
    fe_Object *obj = fe_nextarg(ctx, &args);
    return fe_bool(ctx, fe_type(ctx, obj) == FE_TPAIR || fe_isnil(ctx, obj));
}

/*
================================================================================
|                          REGISTRATION FUNCTIONS                             |
================================================================================
*/

static void register_math_functions(fe_Context *ctx) {
    int gc_save = fe_savegc(ctx);
    fe_set(ctx, fe_symbol(ctx, "sqrt"), fe_cfunc(ctx, builtin_sqrt));
    fe_set(ctx, fe_symbol(ctx, "sin"), fe_cfunc(ctx, builtin_sin));
    fe_set(ctx, fe_symbol(ctx, "cos"), fe_cfunc(ctx, builtin_cos));
    fe_set(ctx, fe_symbol(ctx, "tan"), fe_cfunc(ctx, builtin_tan));
    fe_set(ctx, fe_symbol(ctx, "abs"), fe_cfunc(ctx, builtin_abs));
    fe_set(ctx, fe_symbol(ctx, "floor"), fe_cfunc(ctx, builtin_floor));
    fe_set(ctx, fe_symbol(ctx, "ceil"), fe_cfunc(ctx, builtin_ceil));
    fe_set(ctx, fe_symbol(ctx, "round"), fe_cfunc(ctx, builtin_round));
    fe_set(ctx, fe_symbol(ctx, "min"), fe_cfunc(ctx, builtin_min));
    fe_set(ctx, fe_symbol(ctx, "max"), fe_cfunc(ctx, builtin_max));
    fe_set(ctx, fe_symbol(ctx, "pow"), fe_cfunc(ctx, builtin_pow));
    fe_set(ctx, fe_symbol(ctx, "log"), fe_cfunc(ctx, builtin_log));
    fe_set(ctx, fe_symbol(ctx, "rand"), fe_cfunc(ctx, builtin_random));
    fe_set(ctx, fe_symbol(ctx, "seedrand"), fe_cfunc(ctx, builtin_seed_random));
    fe_set(ctx, fe_symbol(ctx, "randint"), fe_cfunc(ctx, builtin_random_int));
    fe_set(ctx, fe_symbol(ctx, "randbytes"), fe_cfunc(ctx, builtin_random_bytes));

    
    fe_restoregc(ctx, gc_save);
}

static void register_string_functions(fe_Context *ctx) {
    int gc_save = fe_savegc(ctx);
    
    fe_set(ctx, fe_symbol(ctx, "strlen"), fe_cfunc(ctx, builtin_string_length));
    fe_set(ctx, fe_symbol(ctx, "upper"), fe_cfunc(ctx, builtin_string_upper));
    fe_set(ctx, fe_symbol(ctx, "lower"), fe_cfunc(ctx, builtin_string_lower));
    fe_set(ctx, fe_symbol(ctx, "concat"), fe_cfunc(ctx, builtin_string_concat));
    fe_set(ctx, fe_symbol(ctx, "substring"), fe_cfunc(ctx, builtin_string_substring));
    fe_set(ctx, fe_symbol(ctx, "split"), fe_cfunc(ctx, builtin_string_split));
    fe_set(ctx, fe_symbol(ctx, "trim"), fe_cfunc(ctx, builtin_string_trim));
    fe_set(ctx, fe_symbol(ctx, "contains"), fe_cfunc(ctx, builtin_string_contains));
    
    fe_restoregc(ctx, gc_save);
}

static void register_list_functions(fe_Context *ctx) {
    int gc_save = fe_savegc(ctx);
    
    fe_set(ctx, fe_symbol(ctx, "length"), fe_cfunc(ctx, builtin_list_length));
    fe_set(ctx, fe_symbol(ctx, "nth"), fe_cfunc(ctx, builtin_list_nth));
    fe_set(ctx, fe_symbol(ctx, "append"), fe_cfunc(ctx, builtin_list_append));
    fe_set(ctx, fe_symbol(ctx, "reverse"), fe_cfunc(ctx, builtin_list_reverse));
    fe_set(ctx, fe_symbol(ctx, "map"), fe_cfunc(ctx, builtin_map));
    fe_set(ctx, fe_symbol(ctx, "filter"), fe_cfunc(ctx, builtin_filter));
    fe_set(ctx, fe_symbol(ctx, "fold"), fe_cfunc(ctx, builtin_fold));
    
    fe_restoregc(ctx, gc_save);
}

static void register_io_functions(fe_Context *ctx) {
    int gc_save = fe_savegc(ctx);
    
    fe_set(ctx, fe_symbol(ctx, "readfile"), fe_cfunc(ctx, builtin_read_file));
    fe_set(ctx, fe_symbol(ctx, "writefile"), fe_cfunc(ctx, builtin_write_file));
    
    fe_restoregc(ctx, gc_save);
}

static void register_system_functions(fe_Context *ctx) {
    int gc_save = fe_savegc(ctx);
    
    fe_set(ctx, fe_symbol(ctx, "time"), fe_cfunc(ctx, builtin_time));
    fe_set(ctx, fe_symbol(ctx, "exit"), fe_cfunc(ctx, builtin_exit));
    fe_set(ctx, fe_symbol(ctx, "system"), fe_cfunc(ctx, builtin_system));
    
    fe_restoregc(ctx, gc_save);
}

static void register_type_functions(fe_Context *ctx) {
    int gc_save = fe_savegc(ctx);
    
    fe_set(ctx, fe_symbol(ctx, "typeof"), fe_cfunc(ctx, builtin_type_of));
    fe_set(ctx, fe_symbol(ctx, "tostring"), fe_cfunc(ctx, builtin_to_string));
    fe_set(ctx, fe_symbol(ctx, "tonumber"), fe_cfunc(ctx, builtin_to_number));
    fe_set(ctx, fe_symbol(ctx, "isnil"), fe_cfunc(ctx, builtin_is_nil));
    fe_set(ctx, fe_symbol(ctx, "isnumber"), fe_cfunc(ctx, builtin_is_number));
    fe_set(ctx, fe_symbol(ctx, "isstring"), fe_cfunc(ctx, builtin_is_string));
    fe_set(ctx, fe_symbol(ctx, "islist"), fe_cfunc(ctx, builtin_is_list));
    
    fe_restoregc(ctx, gc_save);
}

/*
================================================================================
|                              PUBLIC API                                     |
================================================================================
*/

void fex_init_extended_builtins(fe_Context *ctx, FexBuiltinsConfig config) {
    if (config & FEX_BUILTINS_MATH) {
        register_math_functions(ctx);
    }
    
    if (config & FEX_BUILTINS_STRING) {
        register_string_functions(ctx);
    }
    
    if (config & FEX_BUILTINS_LIST) {
        register_list_functions(ctx);
    }
    
    if (config & FEX_BUILTINS_IO) {
        register_io_functions(ctx);
    }
    
    if (config & FEX_BUILTINS_SYSTEM) {
        register_system_functions(ctx);
    }
    
    if (config & FEX_BUILTINS_TYPE) {
        register_type_functions(ctx);
    }
}

void fex_init_all_builtins(fe_Context *ctx) {
    fex_init_extended_builtins(ctx, FEX_BUILTINS_ALL);
}
