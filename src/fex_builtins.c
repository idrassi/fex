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
** - Map/object operations (makemap, mapget, mapset, etc.)
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

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} TextBuffer;

static int buf_reserve(TextBuffer *buf, size_t extra) {
    size_t needed;
    size_t new_cap;
    char *new_data;

    needed = buf->len + extra + 1;
    if (needed <= buf->cap) {
        return 1;
    }

    new_cap = (buf->cap > 0) ? buf->cap : 64;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    new_data = realloc(buf->data, new_cap);
    if (!new_data) {
        return 0;
    }
    buf->data = new_data;
    buf->cap = new_cap;
    return 1;
}

static int buf_append_mem(TextBuffer *buf, const char *data, size_t len) {
    if (!buf_reserve(buf, len)) {
        return 0;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 1;
}

static int buf_append_str(TextBuffer *buf, const char *str) {
    return buf_append_mem(buf, str, strlen(str));
}

static int buf_append_char(TextBuffer *buf, char chr) {
    if (!buf_reserve(buf, 1)) {
        return 0;
    }
    buf->data[buf->len++] = chr;
    buf->data[buf->len] = '\0';
    return 1;
}

static int buf_append_utf8(TextBuffer *buf, unsigned codepoint) {
    char bytes[3];
    if (codepoint <= 0x7F) {
        return buf_append_char(buf, (char)codepoint);
    }
    if (codepoint <= 0x7FF) {
        bytes[0] = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
        bytes[1] = (char)(0x80 | (codepoint & 0x3F));
        return buf_append_mem(buf, bytes, 2);
    }
    if (codepoint <= 0xFFFF) {
        bytes[0] = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
        bytes[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (codepoint & 0x3F));
        return buf_append_mem(buf, bytes, 3);
    }
    return 0;
}

static void buf_free(TextBuffer *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static char* string_to_cstr(fe_Context *ctx, fe_Object *str_obj, const char *func_name) {
    size_t len;
    char *buffer;
    char msg[128];

    if (fe_type(ctx, str_obj) != FE_TSTRING) {
        sprintf(msg, "%s: type mismatch", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    len = fe_strlen(ctx, str_obj);
    buffer = malloc(len + 1);
    if (!buffer) {
        sprintf(msg, "%s: out of memory", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    fe_tostring(ctx, str_obj, buffer, (int)(len + 1));
    return buffer;
}

static int is_path_separator_char(char chr) {
    return chr == '/' || chr == '\\';
}

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

    return fe_make_number(ctx, (fe_Number) fe_strlen(ctx, str));
}

static fe_Object* builtin_string_upper(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "upper");
    fe_Object *str = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "upper");

    size_t len = fe_strlen(ctx, str);
    if (len == 0) {
        return fe_string(ctx, "", 0);
    }
    
    if (len >= 1024) {
        fe_error(ctx, "upper: string too long");
        return fe_nil(ctx);
    }

    char buffer[1024];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    
    size_t i;
    for (i = 0; i < len; i++) {
        buffer[i] = toupper(buffer[i]);
    }

    return fe_string(ctx, buffer, len);
}

static fe_Object* builtin_string_lower(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "lower");
    fe_Object *str = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "lower");

    size_t len = fe_strlen(ctx, str);
    if (len == 0) {
        return fe_string(ctx, "", 0);
    }
    
    if (len >= 1024) {
        fe_error(ctx, "lower: string too long");
        return fe_nil(ctx);
    }

    char buffer[1024];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    
    size_t i;
    for (i = 0; i < len; i++) {
        buffer[i] = tolower(buffer[i]);
    }

    return fe_string(ctx, buffer, len);
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
    
    fe_Object *obj = fe_string(ctx, result, result_len);
    free(result);
    return obj;
}

static fe_Object* builtin_string_substring(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "substring");
    fe_Object *str = fe_nextarg(ctx, &args);
    fe_Object *start_obj = fe_nextarg(ctx, &args);
    fe_Object *end_obj = fe_nextarg(ctx, &args);
    
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "substring");
    
    size_t str_len = fe_strlen(ctx, str);
    int start = (int)fe_tonumber(ctx, start_obj);
    int end = fe_isnil(ctx, end_obj) ? (int)str_len : (int)fe_tonumber(ctx, end_obj);
    
    if (start < 0) start = 0;
    if (end > (int)str_len) end = (int)str_len;
    if (start >= end) return fe_string(ctx, "", 0);
    
    if (str_len >= 1024) {
        fe_error(ctx, "substring: string too long");
        return fe_nil(ctx);
    }
    
    char buffer[1024];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    
    int result_len = end - start;
    char result[1024];
    memcpy(result, buffer + start, result_len);
    result[result_len] = '\0';

    return fe_string(ctx, result, result_len);
}

static fe_Object* builtin_string_split(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "split");
    fe_Object *str = fe_nextarg(ctx, &args);
    fe_Object *delim = fe_nextarg(ctx, &args);
    
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "split");
    FEX_CHECK_TYPE(ctx, delim, FE_TSTRING, "split");
    
    size_t str_len = fe_strlen(ctx, str);
    size_t delim_len = fe_strlen(ctx, delim);
    
    if (str_len >= 1024 || delim_len >= 64) {
        fe_error(ctx, "split: string too long");
        return fe_nil(ctx);
    }
    
    char buffer[1024], delimiter[64];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    fe_tostring(ctx, delim, delimiter, sizeof(delimiter));
    
    fe_Object *result = fe_nil(ctx);
    fe_Object **tail = &result;
    
    char *token = strtok(buffer, delimiter);
    while (token != NULL) {
        *tail = fe_cons(ctx, fe_string(ctx, token, strlen(token)), fe_nil(ctx));
        tail = fe_cdr_ptr(ctx, *tail);
        token = strtok(NULL, delimiter);
    }
    
    return result;
}

static fe_Object* builtin_string_trim(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "trim");
    fe_Object *str = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "trim");
    
    size_t str_len = fe_strlen(ctx, str);
    if (str_len >= 1024) {
        fe_error(ctx, "trim: string too long");
        return fe_nil(ctx);
    }
    
    char buffer[1024];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    
    /* Trim leading whitespace */
    char *start = buffer;
    while (isspace(*start)) start++;
    
    /* Trim trailing whitespace */
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    
    *(end + 1) = '\0';
    size_t trimmed_len = (size_t)(end - start + 1);
    return fe_string(ctx, start, trimmed_len);
}

static fe_Object* builtin_string_contains(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "contains");
    fe_Object *str = fe_nextarg(ctx, &args);
    fe_Object *substr = fe_nextarg(ctx, &args);
    
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "contains");
    FEX_CHECK_TYPE(ctx, substr, FE_TSTRING, "contains");
    
    size_t str_len = fe_strlen(ctx, str);
    size_t substr_len = fe_strlen(ctx, substr);
    
    if (str_len >= 1024 || substr_len >= 256) {
        fe_error(ctx, "contains: string too long");
        return fe_nil(ctx);
    }
    
    char buffer[1024], search[256];
    fe_tostring(ctx, str, buffer, sizeof(buffer));
    fe_tostring(ctx, substr, search, sizeof(search));
    
    return fe_bool(ctx, strstr(buffer, search) != NULL);
}

static fe_Object* builtin_make_string(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "makestring");
    fe_Object *length_obj = fe_nextarg(ctx, &args);
    fe_Object *fill_obj = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, length_obj, FE_TNUMBER, "makestring");
    FEX_CHECK_TYPE(ctx, fill_obj, FE_TSTRING, "makestring");
    fe_Number length_num = fe_tonumber(ctx, length_obj);
    
    int length = (int)length_num;
    
    if (length == 0) {
        return fe_string(ctx, "", 0);
    }
    
    char fill_buffer[64];
    fe_tostring(ctx, fill_obj, fill_buffer, sizeof(fill_buffer));
    
    if (strlen(fill_buffer) == 0) {
        fe_error(ctx, "makestring: fill character cannot be empty");
        return fe_nil(ctx);
    }
    
    char fill_char = fill_buffer[0];  /* Use first character only */
    
    fe_Object *obj = fe_string_raw(ctx, length, fill_char);
    
    return obj;
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
|                              DATA FUNCTIONS                                 |
================================================================================
*/

static fe_Object* builtin_make_map(fe_Context *ctx, fe_Object *args) {
    fe_Object *map = fe_map(ctx);

    while (!fe_isnil(ctx, args)) {
        fe_Object *key = fe_nextarg(ctx, &args);
        fe_Object *value;
        if (fe_isnil(ctx, args)) {
            fe_error(ctx, "makemap: expected an even number of arguments");
            return fe_nil(ctx);
        }
        value = fe_nextarg(ctx, &args);
        fe_map_set(ctx, map, key, value);
    }

    return map;
}

static fe_Object* builtin_map_set(fe_Context *ctx, fe_Object *args) {
    fe_Object *map;
    fe_Object *key;
    fe_Object *value;

    FEX_CHECK_ARGS(ctx, args, 3, "mapset");
    map = fe_nextarg(ctx, &args);
    key = fe_nextarg(ctx, &args);
    value = fe_nextarg(ctx, &args);
    fe_map_set(ctx, map, key, value);
    return value;
}

static fe_Object* builtin_map_get(fe_Context *ctx, fe_Object *args) {
    fe_Object *map;
    fe_Object *key;
    fe_Object *default_value = fe_nil(ctx);

    FEX_CHECK_ARGS(ctx, args, 2, "mapget");
    map = fe_nextarg(ctx, &args);
    key = fe_nextarg(ctx, &args);
    if (!fe_isnil(ctx, args)) {
        default_value = fe_nextarg(ctx, &args);
    }

    if (!fe_map_has(ctx, map, key)) {
        return default_value;
    }
    return fe_map_get(ctx, map, key);
}

static fe_Object* builtin_map_has(fe_Context *ctx, fe_Object *args) {
    fe_Object *map;
    fe_Object *key;

    FEX_CHECK_ARGS(ctx, args, 2, "maphas");
    map = fe_nextarg(ctx, &args);
    key = fe_nextarg(ctx, &args);
    return fe_bool(ctx, fe_map_has(ctx, map, key));
}

static fe_Object* builtin_map_delete(fe_Context *ctx, fe_Object *args) {
    fe_Object *map;
    fe_Object *key;

    FEX_CHECK_ARGS(ctx, args, 2, "mapdelete");
    map = fe_nextarg(ctx, &args);
    key = fe_nextarg(ctx, &args);
    return fe_bool(ctx, fe_map_delete(ctx, map, key));
}

static fe_Object* builtin_map_keys(fe_Context *ctx, fe_Object *args) {
    fe_Object *map;

    FEX_CHECK_ARGS(ctx, args, 1, "mapkeys");
    map = fe_nextarg(ctx, &args);
    return fe_map_keys(ctx, map);
}

static fe_Object* builtin_map_count(fe_Context *ctx, fe_Object *args) {
    fe_Object *map;

    FEX_CHECK_ARGS(ctx, args, 1, "mapcount");
    map = fe_nextarg(ctx, &args);
    return fe_make_number(ctx, (fe_Number)fe_map_count(ctx, map));
}

/*
================================================================================
|                              JSON FUNCTIONS                                 |
================================================================================
*/

typedef struct {
    fe_Context *ctx;
    const char *current;
} JsonParser;

static void json_skip_ws(JsonParser *parser) {
    while (*parser->current &&
           (*parser->current == ' ' || *parser->current == '\t' ||
            *parser->current == '\n' || *parser->current == '\r')) {
        parser->current++;
    }
}

static int json_hex_value(char chr) {
    if (chr >= '0' && chr <= '9') return chr - '0';
    if (chr >= 'a' && chr <= 'f') return 10 + (chr - 'a');
    if (chr >= 'A' && chr <= 'F') return 10 + (chr - 'A');
    return -1;
}

static fe_Object* json_parse_value(JsonParser *parser);

static fe_Object* json_parse_string(JsonParser *parser) {
    TextBuffer buf;
    fe_Object *result;
    int hi, lo, hi2, lo2;

    buf.data = NULL;
    buf.len = 0;
    buf.cap = 0;

    if (*parser->current != '"') {
        fe_error(parser->ctx, "parsejson: expected string");
        return fe_nil(parser->ctx);
    }
    parser->current++;

    while (*parser->current && *parser->current != '"') {
        unsigned codepoint;
        char chr = *parser->current++;
        if (chr == '\\') {
            chr = *parser->current++;
            switch (chr) {
                case '"': if (!buf_append_char(&buf, '"')) goto oom; break;
                case '\\': if (!buf_append_char(&buf, '\\')) goto oom; break;
                case '/': if (!buf_append_char(&buf, '/')) goto oom; break;
                case 'b': if (!buf_append_char(&buf, '\b')) goto oom; break;
                case 'f': if (!buf_append_char(&buf, '\f')) goto oom; break;
                case 'n': if (!buf_append_char(&buf, '\n')) goto oom; break;
                case 'r': if (!buf_append_char(&buf, '\r')) goto oom; break;
                case 't': if (!buf_append_char(&buf, '\t')) goto oom; break;
                case 'u':
                    hi = json_hex_value(parser->current[0]);
                    lo = json_hex_value(parser->current[1]);
                    hi2 = json_hex_value(parser->current[2]);
                    lo2 = json_hex_value(parser->current[3]);
                    if (hi < 0 || lo < 0 || hi2 < 0 || lo2 < 0) {
                        buf_free(&buf);
                        fe_error(parser->ctx, "parsejson: invalid unicode escape");
                        return fe_nil(parser->ctx);
                    }
                    codepoint = (unsigned)((hi << 12) | (lo << 8) | (hi2 << 4) | lo2);
                    parser->current += 4;
                    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                        buf_free(&buf);
                        fe_error(parser->ctx, "parsejson: surrogate pairs are not supported");
                        return fe_nil(parser->ctx);
                    }
                    if (!buf_append_utf8(&buf, codepoint)) goto oom;
                    break;
                default:
                    buf_free(&buf);
                    fe_error(parser->ctx, "parsejson: invalid escape sequence");
                    return fe_nil(parser->ctx);
            }
        } else {
            if ((unsigned char)chr < 0x20) {
                buf_free(&buf);
                fe_error(parser->ctx, "parsejson: unescaped control character");
                return fe_nil(parser->ctx);
            }
            if (!buf_append_char(&buf, chr)) goto oom;
        }
    }

    if (*parser->current != '"') {
        buf_free(&buf);
        fe_error(parser->ctx, "parsejson: unterminated string");
        return fe_nil(parser->ctx);
    }
    parser->current++;
    result = fe_string(parser->ctx, buf.data ? buf.data : "", buf.len);
    buf_free(&buf);
    return result;

oom:
    buf_free(&buf);
    fe_error(parser->ctx, "parsejson: out of memory");
    return fe_nil(parser->ctx);
}

static fe_Object* json_parse_number(JsonParser *parser) {
    char *endptr;
    double value = strtod(parser->current, &endptr);
    if (endptr == parser->current) {
        fe_error(parser->ctx, "parsejson: invalid number");
        return fe_nil(parser->ctx);
    }
    parser->current = endptr;
    return fe_make_number(parser->ctx, value);
}

static fe_Object* json_parse_array(JsonParser *parser) {
    fe_Object *result;
    fe_Object **tail;

    parser->current++;
    json_skip_ws(parser);
    result = fe_nil(parser->ctx);
    tail = &result;

    if (*parser->current == ']') {
        parser->current++;
        return result;
    }

    for (;;) {
        fe_Object *item = json_parse_value(parser);
        *tail = fe_cons(parser->ctx, item, fe_nil(parser->ctx));
        tail = fe_cdr_ptr(parser->ctx, *tail);
        json_skip_ws(parser);
        if (*parser->current == ']') {
            parser->current++;
            return result;
        }
        if (*parser->current != ',') {
            fe_error(parser->ctx, "parsejson: expected ',' or ']'");
            return fe_nil(parser->ctx);
        }
        parser->current++;
        json_skip_ws(parser);
    }
}

static fe_Object* json_parse_object(JsonParser *parser) {
    fe_Object *map = fe_map(parser->ctx);

    parser->current++;
    json_skip_ws(parser);
    if (*parser->current == '}') {
        parser->current++;
        return map;
    }

    for (;;) {
        fe_Object *key;
        fe_Object *value;

        if (*parser->current != '"') {
            fe_error(parser->ctx, "parsejson: expected object key string");
            return fe_nil(parser->ctx);
        }
        key = json_parse_string(parser);
        json_skip_ws(parser);
        if (*parser->current != ':') {
            fe_error(parser->ctx, "parsejson: expected ':' after object key");
            return fe_nil(parser->ctx);
        }
        parser->current++;
        json_skip_ws(parser);
        value = json_parse_value(parser);
        fe_map_set(parser->ctx, map, key, value);
        json_skip_ws(parser);
        if (*parser->current == '}') {
            parser->current++;
            return map;
        }
        if (*parser->current != ',') {
            fe_error(parser->ctx, "parsejson: expected ',' or '}'");
            return fe_nil(parser->ctx);
        }
        parser->current++;
        json_skip_ws(parser);
    }
}

static fe_Object* json_parse_value(JsonParser *parser) {
    json_skip_ws(parser);
    switch (*parser->current) {
        case '"': return json_parse_string(parser);
        case '{': return json_parse_object(parser);
        case '[': return json_parse_array(parser);
        case 't':
            if (strncmp(parser->current, "true", 4) == 0) {
                parser->current += 4;
                return fe_bool(parser->ctx, 1);
            }
            break;
        case 'f':
            if (strncmp(parser->current, "false", 5) == 0) {
                parser->current += 5;
                return fe_bool(parser->ctx, 0);
            }
            break;
        case 'n':
            if (strncmp(parser->current, "null", 4) == 0) {
                parser->current += 4;
                return fe_nil(parser->ctx);
            }
            break;
        default:
            if (*parser->current == '-' || (*parser->current >= '0' && *parser->current <= '9')) {
                return json_parse_number(parser);
            }
            break;
    }
    fe_error(parser->ctx, "parsejson: invalid value");
    return fe_nil(parser->ctx);
}

static int json_is_proper_list(fe_Context *ctx, fe_Object *obj) {
    while (!fe_isnil(ctx, obj)) {
        if (fe_type(ctx, obj) != FE_TPAIR) {
            return 0;
        }
        obj = fe_cdr(ctx, obj);
    }
    return 1;
}

static int json_write_string(fe_Context *ctx, fe_Object *obj, TextBuffer *buf) {
    char *text;
    size_t i;

    text = string_to_cstr(ctx, obj, "tojson");
    if (!text) {
        return 0;
    }
    if (!buf_append_char(buf, '"')) {
        free(text);
        return 0;
    }
    for (i = 0; text[i] != '\0'; i++) {
        unsigned char chr = (unsigned char)text[i];
        switch (chr) {
            case '"': if (!buf_append_str(buf, "\\\"")) goto fail; break;
            case '\\': if (!buf_append_str(buf, "\\\\")) goto fail; break;
            case '\b': if (!buf_append_str(buf, "\\b")) goto fail; break;
            case '\f': if (!buf_append_str(buf, "\\f")) goto fail; break;
            case '\n': if (!buf_append_str(buf, "\\n")) goto fail; break;
            case '\r': if (!buf_append_str(buf, "\\r")) goto fail; break;
            case '\t': if (!buf_append_str(buf, "\\t")) goto fail; break;
            default:
                if (chr < 0x20) {
                    char hexbuf[7];
                    sprintf(hexbuf, "\\u%04x", chr);
                    if (!buf_append_str(buf, hexbuf)) goto fail;
                } else if (!buf_append_char(buf, (char)chr)) {
                    goto fail;
                }
                break;
        }
    }
    free(text);
    return buf_append_char(buf, '"');

fail:
    free(text);
    return 0;
}

static int json_write_value(fe_Context *ctx, fe_Object *obj, TextBuffer *buf) {
    char number_buf[64];
    switch (fe_type(ctx, obj)) {
        case FE_TNIL:
            return buf_append_str(buf, "null");
        case FE_TBOOLEAN:
            return buf_append_str(buf, (obj == fe_bool(ctx, 1)) ? "true" : "false");
        case FE_TNUMBER:
            sprintf(number_buf, "%.15g", fe_tonumber(ctx, obj));
            return buf_append_str(buf, number_buf);
        case FE_TSTRING:
            return json_write_string(ctx, obj, buf);
        case FE_TMAP: {
            fe_Object *keys = fe_map_keys(ctx, obj);
            int first = 1;
            if (!buf_append_char(buf, '{')) return 0;
            while (!fe_isnil(ctx, keys)) {
                fe_Object *key = fe_car(ctx, keys);
                if (!first && !buf_append_char(buf, ',')) return 0;
                if (!json_write_string(ctx, key, buf)) return 0;
                if (!buf_append_char(buf, ':')) return 0;
                if (!json_write_value(ctx, fe_map_get(ctx, obj, key), buf)) return 0;
                first = 0;
                keys = fe_cdr(ctx, keys);
            }
            return buf_append_char(buf, '}');
        }
        case FE_TPAIR: {
            if (!json_is_proper_list(ctx, obj)) {
                fe_error(ctx, "tojson: cannot serialize dotted pair");
                return 0;
            }
            if (!buf_append_char(buf, '[')) return 0;
            {
                int first = 1;
                fe_Object *list = obj;
                while (!fe_isnil(ctx, list)) {
                    if (!first && !buf_append_char(buf, ',')) return 0;
                    if (!json_write_value(ctx, fe_car(ctx, list), buf)) return 0;
                    first = 0;
                    list = fe_cdr(ctx, list);
                }
            }
            return buf_append_char(buf, ']');
        }
        default:
            fe_error(ctx, "tojson: unsupported value type");
            return 0;
    }
}

static fe_Object* builtin_parse_json(fe_Context *ctx, fe_Object *args) {
    JsonParser parser;
    fe_Object *input;
    char *text;
    fe_Object *result;

    FEX_CHECK_ARGS(ctx, args, 1, "parsejson");
    input = fe_nextarg(ctx, &args);
    text = string_to_cstr(ctx, input, "parsejson");
    if (!text) {
        return fe_nil(ctx);
    }

    parser.ctx = ctx;
    parser.current = text;
    result = json_parse_value(&parser);
    json_skip_ws(&parser);
    if (*parser.current != '\0') {
        free(text);
        fe_error(ctx, "parsejson: trailing characters");
        return fe_nil(ctx);
    }
    free(text);
    return result;
}

static fe_Object* builtin_to_json(fe_Context *ctx, fe_Object *args) {
    TextBuffer buf;
    fe_Object *value;
    fe_Object *result;

    FEX_CHECK_ARGS(ctx, args, 1, "tojson");
    value = fe_nextarg(ctx, &args);

    buf.data = NULL;
    buf.len = 0;
    buf.cap = 0;
    if (!json_write_value(ctx, value, &buf)) {
        buf_free(&buf);
        return fe_nil(ctx);
    }

    result = fe_string(ctx, buf.data ? buf.data : "", buf.len);
    buf_free(&buf);
    return result;
}

/*
================================================================================
|                               I/O FUNCTIONS                                 |
================================================================================
*/

static char* read_file_dynamic(fe_Context *ctx, const char *filename, size_t max_size, size_t *out_size, const char *func_name) {
    FILE *file;
    long size;
    char *buffer;
    size_t bytes_read;
    char msg[160];

    file = fopen(filename, "rb");
    if (!file) {
        sprintf(msg, "%s: could not open file", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        sprintf(msg, "%s: could not determine file size", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        sprintf(msg, "%s: could not determine file size", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    if ((size_t)size > max_size) {
        fclose(file);
        sprintf(msg, "%s: file too large", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    rewind(file);

    buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        sprintf(msg, "%s: out of memory", func_name);
        fe_error(ctx, msg);
        return NULL;
    }

    bytes_read = fread(buffer, 1, (size_t)size, file);
    if (ferror(file)) {
        free(buffer);
        fclose(file);
        sprintf(msg, "%s: error reading file", func_name);
        fe_error(ctx, msg);
        return NULL;
    }

    fclose(file);
    buffer[bytes_read] = '\0';
    if (out_size) {
        *out_size = bytes_read;
    }
    return buffer;
}

static fe_Object* builtin_path_join(fe_Context *ctx, fe_Object *args) {
    TextBuffer buf;
    int need_sep = 0;

    FEX_CHECK_ARGS(ctx, args, 1, "pathjoin");
    buf.data = NULL;
    buf.len = 0;
    buf.cap = 0;

    while (!fe_isnil(ctx, args)) {
        fe_Object *part_obj = fe_nextarg(ctx, &args);
        char *part = string_to_cstr(ctx, part_obj, "pathjoin");
        size_t i;
        size_t start = 0;
        size_t end;

        if (!part) {
            buf_free(&buf);
            return fe_nil(ctx);
        }

        end = strlen(part);
        while (start < end && is_path_separator_char(part[start]) && buf.len > 0) {
            start++;
        }
        while (end > start && is_path_separator_char(part[end - 1])) {
            end--;
        }

        if (need_sep && start < end && buf.len > 0 && buf.data[buf.len - 1] != '/') {
            if (!buf_append_char(&buf, '/')) {
                free(part);
                buf_free(&buf);
                fe_error(ctx, "pathjoin: out of memory");
                return fe_nil(ctx);
            }
        }

        for (i = start; i < end; i++) {
            char chr = is_path_separator_char(part[i]) ? '/' : part[i];
            if (!buf_append_char(&buf, chr)) {
                free(part);
                buf_free(&buf);
                fe_error(ctx, "pathjoin: out of memory");
                return fe_nil(ctx);
            }
        }
        need_sep = buf.len > 0;
        free(part);
    }

    if (buf.len == 0 && !buf_append_char(&buf, '.')) {
        buf_free(&buf);
        fe_error(ctx, "pathjoin: out of memory");
        return fe_nil(ctx);
    }

    {
        fe_Object *result = fe_string(ctx, buf.data, buf.len);
        buf_free(&buf);
        return result;
    }
}

static fe_Object* builtin_dirname(fe_Context *ctx, fe_Object *args) {
    fe_Object *path_obj;
    char *path;
    size_t end;
    size_t i;
    fe_Object *result;

    FEX_CHECK_ARGS(ctx, args, 1, "dirname");
    path_obj = fe_nextarg(ctx, &args);
    path = string_to_cstr(ctx, path_obj, "dirname");
    if (!path) return fe_nil(ctx);

    end = strlen(path);
    while (end > 1 && is_path_separator_char(path[end - 1])) {
        end--;
    }
    for (i = end; i > 0; i--) {
        if (is_path_separator_char(path[i - 1])) {
            while (i > 1 && is_path_separator_char(path[i - 2])) {
                i--;
            }
            if (i == 1) {
                result = fe_string(ctx, "/", 1);
            } else {
                result = fe_string(ctx, path, i - 1);
            }
            free(path);
            return result;
        }
    }

    free(path);
    return fe_string(ctx, ".", 1);
}

static fe_Object* builtin_basename(fe_Context *ctx, fe_Object *args) {
    fe_Object *path_obj;
    char *path;
    size_t end;
    size_t start;
    fe_Object *result;

    FEX_CHECK_ARGS(ctx, args, 1, "basename");
    path_obj = fe_nextarg(ctx, &args);
    path = string_to_cstr(ctx, path_obj, "basename");
    if (!path) return fe_nil(ctx);

    end = strlen(path);
    while (end > 1 && is_path_separator_char(path[end - 1])) {
        end--;
    }
    start = end;
    while (start > 0 && !is_path_separator_char(path[start - 1])) {
        start--;
    }
    result = fe_string(ctx, path + start, end - start);
    free(path);
    return result;
}

static fe_Object* builtin_read_file(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "readfile");
    fe_Object *filename_obj = fe_nextarg(ctx, &args);
    char *filename = string_to_cstr(ctx, filename_obj, "readfile");
    char *buffer;
    size_t bytes_read;
    fe_Object *result;

    if (!filename) {
        return fe_nil(ctx);
    }

    buffer = read_file_dynamic(ctx, filename, 256 * 1024, &bytes_read, "readfile");
    free(filename);
    if (!buffer) return fe_nil(ctx);

    result = fe_string(ctx, buffer, bytes_read);
    free(buffer);
    return result;
}

static fe_Object* builtin_write_file(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "writefile");
    fe_Object *filename_obj = fe_nextarg(ctx, &args);
    fe_Object *content_obj = fe_nextarg(ctx, &args);
    char *filename = string_to_cstr(ctx, filename_obj, "writefile");
    char *content = string_to_cstr(ctx, content_obj, "writefile");
    size_t content_len;
    FILE *file;
    size_t written;

    if (!filename || !content) {
        free(filename);
        free(content);
        return fe_nil(ctx);
    }

    content_len = strlen(content);
    file = fopen(filename, "wb");
    if (!file) {
        free(filename);
        free(content);
        fe_error(ctx, "writefile: could not open file for writing");
        return fe_nil(ctx);
    }

    written = fwrite(content, 1, content_len, file);
    if (written != content_len || ferror(file)) {
        fclose(file);
        free(filename);
        free(content);
        fe_error(ctx, "writefile: error writing file");
        return fe_nil(ctx);
    }

    fclose(file);
    free(filename);
    free(content);
    return fe_make_number(ctx, (fe_Number)written);
}

static fe_Object* builtin_read_json(fe_Context *ctx, fe_Object *args) {
    fe_Object *filename_obj;
    char *filename;
    char *buffer;
    JsonParser parser;
    fe_Object *result;

    FEX_CHECK_ARGS(ctx, args, 1, "readjson");
    filename_obj = fe_nextarg(ctx, &args);
    filename = string_to_cstr(ctx, filename_obj, "readjson");
    if (!filename) return fe_nil(ctx);

    buffer = read_file_dynamic(ctx, filename, 256 * 1024, NULL, "readjson");
    free(filename);
    if (!buffer) return fe_nil(ctx);

    parser.ctx = ctx;
    parser.current = buffer;
    result = json_parse_value(&parser);
    json_skip_ws(&parser);
    if (*parser.current != '\0') {
        free(buffer);
        fe_error(ctx, "readjson: trailing characters");
        return fe_nil(ctx);
    }
    free(buffer);
    return result;
}

static fe_Object* builtin_write_json(fe_Context *ctx, fe_Object *args) {
    fe_Object *filename_obj;
    fe_Object *value;
    char *filename;
    TextBuffer buf;
    FILE *file;
    size_t written;

    FEX_CHECK_ARGS(ctx, args, 2, "writejson");
    filename_obj = fe_nextarg(ctx, &args);
    value = fe_nextarg(ctx, &args);

    filename = string_to_cstr(ctx, filename_obj, "writejson");
    if (!filename) return fe_nil(ctx);

    buf.data = NULL;
    buf.len = 0;
    buf.cap = 0;
    if (!json_write_value(ctx, value, &buf)) {
        free(filename);
        buf_free(&buf);
        return fe_nil(ctx);
    }

    file = fopen(filename, "wb");
    if (!file) {
        free(filename);
        buf_free(&buf);
        fe_error(ctx, "writejson: could not open file for writing");
        return fe_nil(ctx);
    }

    written = fwrite(buf.data, 1, buf.len, file);
    if (written != buf.len || ferror(file)) {
        fclose(file);
        free(filename);
        buf_free(&buf);
        fe_error(ctx, "writejson: error writing file");
        return fe_nil(ctx);
    }

    fclose(file);
    free(filename);
    buf_free(&buf);
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
    FEX_CHECK_TYPE(ctx, command_obj, FE_TSTRING, "system");
    
    size_t command_len = fe_strlen(ctx, command_obj);
    if (command_len >= 1024) {
        fe_error(ctx, "system: command too long");
        return fe_nil(ctx);
    }
    
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
        case FE_TMAP: type_name = "map"; break;
        case FE_TBOOLEAN: type_name = "boolean"; break;
        default: type_name = "unknown"; break;
    }
    
    return fe_string(ctx, type_name, strlen(type_name));
}

static fe_Object* builtin_to_string(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "tostring");
    fe_Object *obj = fe_nextarg(ctx, &args);
    
    char buffer[1024];
    int len = fe_tostring(ctx, obj, buffer, sizeof(buffer));
    
    return fe_string(ctx, buffer, len);
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

static fe_Object* builtin_is_map(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "ismap");
    return fe_bool(ctx, fe_type(ctx, fe_nextarg(ctx, &args)) == FE_TMAP);
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
    fe_set(ctx, fe_symbol(ctx, "makestring"), fe_cfunc(ctx, builtin_make_string));
    
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
    
    fe_set(ctx, fe_symbol(ctx, "pathjoin"), fe_cfunc(ctx, builtin_path_join));
    fe_set(ctx, fe_symbol(ctx, "dirname"), fe_cfunc(ctx, builtin_dirname));
    fe_set(ctx, fe_symbol(ctx, "basename"), fe_cfunc(ctx, builtin_basename));
    fe_set(ctx, fe_symbol(ctx, "readfile"), fe_cfunc(ctx, builtin_read_file));
    fe_set(ctx, fe_symbol(ctx, "writefile"), fe_cfunc(ctx, builtin_write_file));
    fe_set(ctx, fe_symbol(ctx, "readjson"), fe_cfunc(ctx, builtin_read_json));
    fe_set(ctx, fe_symbol(ctx, "writejson"), fe_cfunc(ctx, builtin_write_json));
    
    fe_restoregc(ctx, gc_save);
}

static void register_data_functions(fe_Context *ctx) {
    int gc_save = fe_savegc(ctx);

    fe_set(ctx, fe_symbol(ctx, "makemap"), fe_cfunc(ctx, builtin_make_map));
    fe_set(ctx, fe_symbol(ctx, "mapset"), fe_cfunc(ctx, builtin_map_set));
    fe_set(ctx, fe_symbol(ctx, "mapget"), fe_cfunc(ctx, builtin_map_get));
    fe_set(ctx, fe_symbol(ctx, "maphas"), fe_cfunc(ctx, builtin_map_has));
    fe_set(ctx, fe_symbol(ctx, "mapdelete"), fe_cfunc(ctx, builtin_map_delete));
    fe_set(ctx, fe_symbol(ctx, "mapkeys"), fe_cfunc(ctx, builtin_map_keys));
    fe_set(ctx, fe_symbol(ctx, "mapcount"), fe_cfunc(ctx, builtin_map_count));
    fe_set(ctx, fe_symbol(ctx, "parsejson"), fe_cfunc(ctx, builtin_parse_json));
    fe_set(ctx, fe_symbol(ctx, "tojson"), fe_cfunc(ctx, builtin_to_json));

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
    fe_set(ctx, fe_symbol(ctx, "ismap"), fe_cfunc(ctx, builtin_is_map));

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

    if (config & FEX_BUILTINS_DATA) {
        register_data_functions(ctx);
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
