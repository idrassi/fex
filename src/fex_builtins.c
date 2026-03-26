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
#include <windows.h>
#include <io.h>
#include <wchar.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif

#include "fex_builtins.h"
#include "fex_internal.h"
#include "sfc32.h"

#define FEX_BUILTIN_ABORT_CHECK_INTERVAL 64u
#define FEX_FILE_IO_CHUNK_SIZE 16384u

typedef struct {
    fe_Context *ctx;
    char *data;
    size_t len;
    size_t cap;
} TextBuffer;

typedef struct {
    fe_Context *ctx;
    char **items;
    int count;
} CStringArray;

typedef struct {
    unsigned char *stdin_data;
    size_t stdin_len;
    char *cwd;
    CStringArray env;
    int use_env;
    int stdout_mode;
    int stderr_mode;
    size_t max_stdout;
    size_t max_stderr;
} ProcessOptions;

typedef struct {
    int exit_code;
    unsigned char *stdout_data;
    size_t stdout_len;
    unsigned char *stderr_data;
    size_t stderr_len;
    int stdout_captured;
    int stderr_captured;
} ProcessOutput;

typedef struct {
    char *path;
#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
#endif
} TempRedirectFile;

typedef struct {
#ifdef _WIN32
    HANDLE read_handle;
    HANDLE write_handle;
#else
    int read_fd;
    int write_fd;
#endif
} ProcessCapturePipe;

#ifdef _WIN32
typedef struct {
    HANDLE write_handle;
    const unsigned char *data;
    size_t len;
    int failed;
    DWORD error_code;
} ProcessInputWriter;
#endif

#define FEX_COMMAND_OUTPUT_MAX_BYTES (4u * 1024u * 1024u)
#define PROCESS_STREAM_CAPTURE 0
#define PROCESS_STREAM_INHERIT 1
#define PROCESS_STREAM_DISCARD 2

static void* builtin_alloc(fe_Context *ctx, size_t size) {
    (void)ctx;
    return malloc(size > 0 ? size : 1);
}

static void* builtin_realloc(fe_Context *ctx, void *ptr, size_t size) {
    (void)ctx;
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

static void builtin_free(fe_Context *ctx, void *ptr) {
    (void)ctx;
    free(ptr);
}

static void* tracked_builtin_alloc(fe_Context *ctx, size_t size) {
    return fe_ctx_tracked_alloc(ctx, size);
}

static void tracked_builtin_free(fe_Context *ctx, void *ptr) {
    fe_ctx_tracked_free(ctx, ptr);
}

#ifdef _WIN32
static wchar_t* utf8_to_wide_alloc(fe_Context *ctx, const char *text, const char *func_name) {
    int needed;
    wchar_t *wide;
    char msg[160];

    needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (needed <= 0) {
        snprintf(msg, sizeof(msg), "%s: invalid UTF-8 path", func_name);
        fe_error(ctx, msg);
        return NULL;
    }

    wide = (wchar_t*)builtin_alloc(ctx, (size_t)needed * sizeof(wchar_t));
    if (!wide) {
        snprintf(msg, sizeof(msg), "%s: out of memory", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, needed) <= 0) {
        builtin_free(ctx, wide);
        snprintf(msg, sizeof(msg), "%s: invalid UTF-8 path", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    return wide;
}

static char* wide_to_utf8_alloc(fe_Context *ctx, const wchar_t *text, const char *func_name) {
    int needed;
    char *utf8;
    char msg[160];

    needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) {
        snprintf(msg, sizeof(msg), "%s: could not convert wide string", func_name);
        fe_error(ctx, msg);
        return NULL;
    }

    utf8 = (char*)builtin_alloc(ctx, (size_t)needed);
    if (!utf8) {
        snprintf(msg, sizeof(msg), "%s: out of memory", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, needed, NULL, NULL) <= 0) {
        builtin_free(ctx, utf8);
        snprintf(msg, sizeof(msg), "%s: could not convert wide string", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    return utf8;
}

static FILE* fopen_utf8(fe_Context *ctx, const char *path, const wchar_t *mode,
                        const char *func_name) {
    wchar_t *wide_path = utf8_to_wide_alloc(ctx, path, func_name);
    FILE *file;

    if (!wide_path) {
        return NULL;
    }
    file = _wfopen(wide_path, mode);
    builtin_free(ctx, wide_path);
    return file;
}
#else
static FILE* fopen_utf8(fe_Context *ctx, const char *path, const char *mode,
                        const char *func_name) {
    (void)ctx;
    (void)func_name;
    return fopen(path, mode);
}
#endif

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

    if (buf->ctx) {
        new_data = (char*)builtin_realloc(buf->ctx, buf->data, new_cap);
    } else {
        new_data = (char*)realloc(buf->data, new_cap);
    }
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

static const char* builtin_poll_abort(fe_Context *ctx, size_t *countdown) {
    if (*countdown > 1) {
        (*countdown)--;
        return NULL;
    }

    *countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    return fe_poll_abort(ctx);
}

static int buf_append_mem_polling(fe_Context *ctx, TextBuffer *buf,
                                  const char *data, size_t len,
                                  size_t *poll_countdown,
                                  const char **abort_error) {
    size_t offset = 0;

    while (offset < len) {
        size_t chunk_size = len - offset;
        if (chunk_size > FEX_FILE_IO_CHUNK_SIZE) {
            chunk_size = FEX_FILE_IO_CHUNK_SIZE;
        }
        *abort_error = builtin_poll_abort(ctx, poll_countdown);
        if (*abort_error != NULL) {
            return 0;
        }
        if (!buf_append_mem(buf, data + offset, chunk_size)) {
            return 0;
        }
        offset += chunk_size;
    }

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
    if (buf->ctx) {
        builtin_free(buf->ctx, buf->data);
    } else {
        free(buf->data);
    }
    buf->ctx = NULL;
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static char* copy_cstr_ctx(fe_Context *ctx, const char *str) {
    size_t len;
    char *copy;

    len = strlen(str);
    if (ctx) {
        copy = (char*)builtin_alloc(ctx, len + 1);
    } else {
        copy = (char*)malloc(len + 1);
    }
    if (!copy) {
        return NULL;
    }
    memcpy(copy, str, len + 1);
    return copy;
}

static void free_cstring_array(CStringArray *array) {
    int i;

    if (!array->items) {
        array->count = 0;
        return;
    }

    for (i = 0; i < array->count; i++) {
        if (array->ctx) {
            builtin_free(array->ctx, array->items[i]);
        } else {
            free(array->items[i]);
        }
    }
    if (array->ctx) {
        builtin_free(array->ctx, array->items);
    } else {
        free(array->items);
    }
    array->items = NULL;
    array->count = 0;
    array->ctx = NULL;
}

static void free_cstring_items(fe_Context *ctx, char **items, int count) {
    int i;

    if (!items) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (ctx) {
            builtin_free(ctx, items[i]);
        } else {
            free(items[i]);
        }
    }
    if (ctx) {
        builtin_free(ctx, items);
    } else {
        free(items);
    }
}

static void free_process_options(fe_Context *ctx, ProcessOptions *options) {
    builtin_free(ctx, options->stdin_data);
    options->stdin_data = NULL;
    options->stdin_len = 0;
    builtin_free(ctx, options->cwd);
    options->cwd = NULL;
    free_cstring_array(&options->env);
    options->use_env = 0;
    options->stdout_mode = PROCESS_STREAM_CAPTURE;
    options->stderr_mode = PROCESS_STREAM_CAPTURE;
    options->max_stdout = FEX_COMMAND_OUTPUT_MAX_BYTES;
    options->max_stderr = FEX_COMMAND_OUTPUT_MAX_BYTES;
}

static void free_process_output(fe_Context *ctx, ProcessOutput *output) {
    builtin_free(ctx, output->stdout_data);
    output->stdout_data = NULL;
    output->stdout_len = 0;
    builtin_free(ctx, output->stderr_data);
    output->stderr_data = NULL;
    output->stderr_len = 0;
    output->stdout_captured = 0;
    output->stderr_captured = 0;
    output->exit_code = 0;
}

static void init_temp_redirect_file(TempRedirectFile *file) {
    file->path = NULL;
#ifdef _WIN32
    file->handle = INVALID_HANDLE_VALUE;
#else
    file->fd = -1;
#endif
}

static void close_temp_redirect_file(TempRedirectFile *file) {
#ifdef _WIN32
    if (file->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(file->handle);
        file->handle = INVALID_HANDLE_VALUE;
    }
#else
    if (file->fd >= 0) {
        close(file->fd);
        file->fd = -1;
    }
#endif
}

static void destroy_temp_redirect_file(TempRedirectFile *file) {
    close_temp_redirect_file(file);
    if (file->path) {
        remove(file->path);
        free(file->path);
        file->path = NULL;
    }
}

static void init_process_capture_pipe(ProcessCapturePipe *pipe) {
#ifdef _WIN32
    pipe->read_handle = INVALID_HANDLE_VALUE;
    pipe->write_handle = INVALID_HANDLE_VALUE;
#else
    pipe->read_fd = -1;
    pipe->write_fd = -1;
#endif
}

static void close_process_capture_pipe_read(ProcessCapturePipe *pipe) {
#ifdef _WIN32
    if (pipe->read_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe->read_handle);
        pipe->read_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (pipe->read_fd >= 0) {
        close(pipe->read_fd);
        pipe->read_fd = -1;
    }
#endif
}

static void close_process_capture_pipe_write(ProcessCapturePipe *pipe) {
#ifdef _WIN32
    if (pipe->write_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe->write_handle);
        pipe->write_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (pipe->write_fd >= 0) {
        close(pipe->write_fd);
        pipe->write_fd = -1;
    }
#endif
}

static void destroy_process_capture_pipe(ProcessCapturePipe *pipe) {
    close_process_capture_pipe_read(pipe);
    close_process_capture_pipe_write(pipe);
}

static int append_process_capture(TextBuffer *buf, const unsigned char *data,
                                  size_t len, size_t limit, int *overflow,
                                  fe_Context *ctx, const char *func_name) {
    size_t to_copy = len;
    char msg[160];

    if (*overflow) {
        return 1;
    }

    if (limit > 0 && buf->len >= limit) {
        *overflow = 1;
        return 1;
    }

    if (limit > 0 && to_copy > limit - buf->len) {
        to_copy = limit - buf->len;
        *overflow = 1;
    }

    if (to_copy > 0 && !buf_append_mem(buf, (const char*)data, to_copy)) {
        snprintf(msg, sizeof(msg), "%s: out of memory", func_name);
        fe_error(ctx, msg);
        return 0;
    }

    return 1;
}

static char* string_to_cstr(fe_Context *ctx, fe_Object *str_obj, const char *func_name) {
    size_t len;
    char *buffer;
    char msg[160];

    if (fe_type(ctx, str_obj) != FE_TSTRING) {
        snprintf(msg, sizeof(msg), "%s: type mismatch", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    len = fe_strlen(ctx, str_obj);
    if (len + 1 > (size_t)INT_MAX) {
        snprintf(msg, sizeof(msg),"%s: string too large", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    buffer = (char*)builtin_alloc(ctx, len + 1);
    if (!buffer) {
        snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    fe_tostring(ctx, str_obj, buffer, (int)(len + 1));
    if (len > 0 && memchr(buffer, '\0', len) != NULL) {
        builtin_free(ctx, buffer);
        snprintf(msg, sizeof(msg),"%s: strings containing NUL bytes are not allowed", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    return buffer;
}

static char* string_to_buffer(fe_Context *ctx, fe_Object *str_obj, const char *func_name, size_t *out_len) {
    size_t len;
    char *buffer;
    char msg[160];

    if (fe_type(ctx, str_obj) != FE_TSTRING) {
        snprintf(msg, sizeof(msg),"%s: type mismatch", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    len = fe_strlen(ctx, str_obj);
    if (len + 1 > (size_t)INT_MAX) {
        snprintf(msg, sizeof(msg),"%s: string too large", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    buffer = (char*)builtin_alloc(ctx, len + 1);
    if (!buffer) {
        snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    fe_tostring(ctx, str_obj, buffer, (int)(len + 1));
    if (out_len) {
        *out_len = len;
    }
    return buffer;
}

static unsigned char* bytes_to_buffer(fe_Context *ctx, fe_Object *bytes_obj, const char *func_name, size_t *out_len) {
    size_t len;
    unsigned char *buffer;
    char msg[128];

    if (fe_type(ctx, bytes_obj) != FE_TBYTES) {
        snprintf(msg, sizeof(msg),"%s: type mismatch", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    len = fe_byteslen(ctx, bytes_obj);
    buffer = (unsigned char*)builtin_alloc(ctx, (len > 0) ? len : 1);
    if (!buffer) {
        snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    if (len > 0) {
        fe_bytescopy(ctx, bytes_obj, 0, buffer, len);
    }
    *out_len = len;
    return buffer;
}

static fe_Object* string_to_bytes(fe_Context *ctx, fe_Object *str_obj, const char *func_name) {
    size_t len;
    char *buffer;
    fe_Object *result;

    if (fe_type(ctx, str_obj) != FE_TSTRING) {
        char msg[128];
        snprintf(msg, sizeof(msg),"%s: type mismatch", func_name);
        fe_error(ctx, msg);
        return fe_nil(ctx);
    }
    len = fe_strlen(ctx, str_obj);
    if (len > 0 && len + 1 > (size_t)INT_MAX) {
        fe_error(ctx, "tobytes: string too large");
        return fe_nil(ctx);
    }
    buffer = (char*)builtin_alloc(ctx, (len > 0) ? len + 1 : 1);
    if (!buffer) {
        fe_error(ctx, "tobytes: out of memory");
        return fe_nil(ctx);
    }
    if (len > 0) {
        fe_tostring(ctx, str_obj, buffer, (int)(len + 1));
    }
    result = fe_bytes(ctx, buffer, len);
    builtin_free(ctx, buffer);
    return result;
}

static int copy_string_to_fixed_buffer(fe_Context *ctx, fe_Object *str_obj,
                                       const char *func_name,
                                       char *buffer, size_t capacity,
                                       size_t *out_len) {
    size_t len;
    char msg[160];

    if (fe_type(ctx, str_obj) != FE_TSTRING) {
        snprintf(msg, sizeof(msg),"%s: type mismatch", func_name);
        fe_error(ctx, msg);
        return 0;
    }

    len = fe_strlen(ctx, str_obj);
    if (len + 1 > capacity) {
        snprintf(msg, sizeof(msg),"%s: string too long", func_name);
        fe_error(ctx, msg);
        return 0;
    }

    fe_tostring(ctx, str_obj, buffer, (int)capacity);
    if (out_len) {
        *out_len = len;
    }
    return 1;
}

static int bytes_contains_byte(const char *set, size_t set_len, unsigned char value) {
    size_t i;
    for (i = 0; i < set_len; i++) {
        if ((unsigned char)set[i] == value) {
            return 1;
        }
    }
    return 0;
}

static int bytes_contains_sequence(const char *haystack, size_t haystack_len,
                                   const char *needle, size_t needle_len) {
    size_t i;

    if (needle_len == 0) {
        return 1;
    }
    if (needle_len > haystack_len) {
        return 0;
    }

    for (i = 0; i + needle_len <= haystack_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_path_separator_char(char chr) {
    return chr == '/' || chr == '\\';
}

#ifndef _WIN32
static int decode_process_exit_code(int status) {
    if (status < 0) {
        return status;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return status;
}
#endif

static char* build_merged_command(fe_Context *ctx, const char *command, const char *func_name) {
    const char *prefix = "(";
    const char *suffix = ") 2>&1";
    size_t command_len = strlen(command);
    size_t total_len = strlen(prefix) + command_len + strlen(suffix);
    char *merged = (char*)builtin_alloc(ctx, total_len + 1);
    char msg[160];

    if (!merged) {
        snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
        fe_error(ctx, msg);
        return NULL;
    }

    memcpy(merged, prefix, strlen(prefix));
    memcpy(merged + strlen(prefix), command, command_len);
    memcpy(merged + strlen(prefix) + command_len, suffix, strlen(suffix) + 1);
    return merged;
}

static int cstring_array_push_copy(CStringArray *array, const char *value) {
    char **new_items;
    char *copy;

    copy = copy_cstr_ctx(array->ctx, value);
    if (!copy) {
        return 0;
    }

    if (array->ctx) {
        new_items = (char**)builtin_realloc(array->ctx, array->items,
                                            sizeof(*array->items) * (size_t)(array->count + 2));
    } else {
        new_items = (char**)realloc(array->items,
                                    sizeof(*array->items) * (size_t)(array->count + 2));
    }
    if (!new_items) {
        if (array->ctx) {
            builtin_free(array->ctx, copy);
        } else {
            free(copy);
        }
        return 0;
    }

    array->items = new_items;
    array->items[array->count++] = copy;
    array->items[array->count] = NULL;
    return 1;
}

static int build_shell_process_argv(fe_Context *ctx, const char *command,
                                    const char *func_name, CStringArray *argv) {
    char msg[160];
#ifdef _WIN32
    const char *shell = getenv("ComSpec");

    if (!shell || !*shell) {
        shell = "cmd.exe";
    }
    if (!cstring_array_push_copy(argv, shell) ||
        !cstring_array_push_copy(argv, "/d") ||
        !cstring_array_push_copy(argv, "/s") ||
        !cstring_array_push_copy(argv, "/c") ||
        !cstring_array_push_copy(argv, command)) {
        snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
        fe_error(ctx, msg);
        return 0;
    }
#else
    if (!cstring_array_push_copy(argv, "/bin/sh") ||
        !cstring_array_push_copy(argv, "-c") ||
        !cstring_array_push_copy(argv, command)) {
        snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
        fe_error(ctx, msg);
        return 0;
    }
#endif

    (void)ctx;
    return 1;
}

static int compare_cstring_items(const void *lhs, const void *rhs) {
    const char *left = *(const char* const*)lhs;
    const char *right = *(const char* const*)rhs;
    return strcmp(left, right);
}

static void cstring_array_sort(CStringArray *array) {
    if (array->count > 1) {
        qsort(array->items, (size_t)array->count, sizeof(*array->items),
              compare_cstring_items);
    }
}

static fe_Object* cstring_array_to_list(fe_Context *ctx, const CStringArray *array) {
    fe_Object **items;
    fe_Object *result;
    int i;
    int gc_save;
    size_t poll_countdown = 1;
    const char *abort_error;

    if (array->count == 0) {
        return fe_nil(ctx);
    }

    items = (fe_Object**)builtin_alloc(ctx, sizeof(*items) * (size_t)array->count);
    if (!items) {
        fe_error(ctx, "listdir: out of memory");
        return fe_nil(ctx);
    }

    gc_save = fe_savegc(ctx);
    for (i = 0; i < array->count; i++) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_restoregc(ctx, gc_save);
            builtin_free(ctx, items);
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        items[i] = fe_string(ctx, array->items[i], strlen(array->items[i]));
        fe_pushgc(ctx, items[i]);
    }

    result = fe_list(ctx, items, array->count);
    fe_restoregc(ctx, gc_save);
    builtin_free(ctx, items);
    return result;
}

static int path_exists_cstr(const char *path) {
#ifdef _WIN32
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    DWORD attrs = INVALID_FILE_ATTRIBUTES;
    wchar_t *wide_path;

    if (needed <= 0) {
        return 0;
    }
    wide_path = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
    if (!wide_path) {
        return 0;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, needed) > 0) {
        attrs = GetFileAttributesW(wide_path);
    }
    free(wide_path);
    return attrs != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

static int path_is_directory_cstr(const char *path) {
#ifdef _WIN32
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    DWORD attrs = INVALID_FILE_ATTRIBUTES;
    wchar_t *wide_path;

    if (needed <= 0) {
        return 0;
    }
    wide_path = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
    if (!wide_path) {
        return 0;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, needed) > 0) {
        attrs = GetFileAttributesW(wide_path);
    }
    free(wide_path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
#endif
}

static int create_directory_cstr(const char *path) {
#ifdef _WIN32
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide_path;
    int created = 0;

    if (needed <= 0) {
        return 0;
    }
    wide_path = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
    if (!wide_path) {
        return 0;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, needed) > 0 &&
        CreateDirectoryW(wide_path, NULL)) {
        created = 1;
    }
    if (created) {
        free(wide_path);
        return 1;
    }
    free(wide_path);
    if (GetLastError() == ERROR_ALREADY_EXISTS && path_is_directory_cstr(path)) {
        return 1;
    }
    return 0;
#else
    if (mkdir(path, 0777) == 0) {
        return 1;
    }
    if (errno == EEXIST && path_is_directory_cstr(path)) {
        return 1;
    }
    return 0;
#endif
}

static size_t path_root_length(const char *path) {
    size_t len = strlen(path);

#ifdef _WIN32
    if (len >= 2 && isalpha((unsigned char)path[0]) && path[1] == ':') {
        if (len >= 3 && is_path_separator_char(path[2])) {
            return 3;
        }
        return 2;
    }
#endif
    if (len > 0 && is_path_separator_char(path[0])) {
        return 1;
    }
    return 0;
}

static int ensure_directory_tree_cstr(char *path) {
    size_t len;
    size_t root_len;
    size_t i;

    len = strlen(path);
    root_len = path_root_length(path);
    while (len > root_len && is_path_separator_char(path[len - 1])) {
        path[len - 1] = '\0';
        len--;
    }

    if (len == 0) {
        return 0;
    }

    for (i = root_len; i < len; i++) {
        char saved;

        if (!is_path_separator_char(path[i])) {
            continue;
        }
        if (i == root_len) {
            continue;
        }

        saved = path[i];
        path[i] = '\0';
        if (path[0] != '\0' && !(i == 2 && path[1] == ':') &&
            !create_directory_cstr(path)) {
            path[i] = saved;
            return 0;
        }
        path[i] = saved;
    }

    return create_directory_cstr(path);
}

static char* current_working_directory_cstr(fe_Context *ctx) {
#ifdef _WIN32
    DWORD size = MAX_PATH;
    wchar_t *wide_buffer = NULL;

    for (;;) {
        wchar_t *new_buffer = (wchar_t*)builtin_realloc(ctx, wide_buffer, (size_t)size * sizeof(wchar_t));
        DWORD written;

        if (!new_buffer) {
            builtin_free(ctx, wide_buffer);
            return NULL;
        }
        wide_buffer = new_buffer;

        written = GetCurrentDirectoryW(size, wide_buffer);
        if (written == 0) {
            builtin_free(ctx, wide_buffer);
            return NULL;
        }
        if (written < size) {
            char *utf8 = wide_to_utf8_alloc(ctx, wide_buffer, "cwd");
            builtin_free(ctx, wide_buffer);
            return utf8;
        }
        size = written + 1;
    }
#else
    size_t size = 256;
    char *buffer = NULL;

    for (;;) {
        char *new_buffer = (char*)builtin_realloc(ctx, buffer, size);

        if (!new_buffer) {
            builtin_free(ctx, buffer);
            return NULL;
        }
        buffer = new_buffer;

        if (getcwd(buffer, size) != NULL) {
            return buffer;
        }
        if (errno != ERANGE) {
            builtin_free(ctx, buffer);
            return NULL;
        }
        size *= 2;
    }
#endif
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
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;

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
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
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
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "upper");

    size_t len = fe_strlen(ctx, str);
    if (len == 0) {
        return fe_string(ctx, "", 0);
    }

    char buffer[1024];
    if (!copy_string_to_fixed_buffer(ctx, str, "upper", buffer, sizeof(buffer), &len)) {
        return fe_nil(ctx);
    }

    size_t i;
    for (i = 0; i < len; i++) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        buffer[i] = (char)toupper((unsigned char)buffer[i]);
    }

    return fe_string(ctx, buffer, len);
}

static fe_Object* builtin_string_lower(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "lower");
    fe_Object *str = fe_nextarg(ctx, &args);
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "lower");

    size_t len = fe_strlen(ctx, str);
    if (len == 0) {
        return fe_string(ctx, "", 0);
    }

    char buffer[1024];
    if (!copy_string_to_fixed_buffer(ctx, str, "lower", buffer, sizeof(buffer), &len)) {
        return fe_nil(ctx);
    }

    size_t i;
    for (i = 0; i < len; i++) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        buffer[i] = (char)tolower((unsigned char)buffer[i]);
    }

    return fe_string(ctx, buffer, len);
}

static fe_Object* builtin_string_concat(fe_Context *ctx, fe_Object *args) {
    TextBuffer result;
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;

    result.ctx = NULL;
    result.data = NULL;
    result.len = 0;
    result.cap = 0;

    while (!fe_isnil(ctx, args)) {
        fe_Object *arg = fe_nextarg(ctx, &args);
        char stack_buffer[1024];
        char *buffer = stack_buffer;
        size_t len = 0;
        int dynamic_buffer = 0;

        if (fe_type(ctx, arg) == FE_TSTRING) {
            len = fe_strlen(ctx, arg);
            if (len + 1 > sizeof(stack_buffer)) {
                buffer = (char*)builtin_alloc(ctx, len + 1);
                if (!buffer) {
                    buf_free(&result);
                    fe_error(ctx, "concat: out of memory");
                    return fe_nil(ctx);
                }
                dynamic_buffer = 1;
            }
            fe_tostring(ctx, arg, buffer, (int)(len + 1));
        } else {
            len = (size_t)fe_tostring(ctx, arg, stack_buffer, sizeof(stack_buffer));
        }

        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            if (dynamic_buffer) {
                builtin_free(ctx, buffer);
            }
            buf_free(&result);
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }

        if (!buf_append_mem_polling(ctx, &result, buffer, len,
                                    &poll_countdown, &abort_error)) {
            if (dynamic_buffer) {
                builtin_free(ctx, buffer);
            }
            buf_free(&result);
            if (abort_error != NULL) {
                fe_error(ctx, abort_error);
            } else {
                fe_error(ctx, "concat: out of memory");
            }
            return fe_nil(ctx);
        }

        if (dynamic_buffer) {
            builtin_free(ctx, buffer);
        }
    }

    fe_Object *obj = fe_string(ctx, result.data ? result.data : "", result.len);
    buf_free(&result);
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

    char buffer[1024];
    if (!copy_string_to_fixed_buffer(ctx, str, "substring", buffer, sizeof(buffer), &str_len)) {
        return fe_nil(ctx);
    }

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
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;

    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "split");
    FEX_CHECK_TYPE(ctx, delim, FE_TSTRING, "split");

    size_t str_len = fe_strlen(ctx, str);
    size_t delim_len = fe_strlen(ctx, delim);

    char buffer[1024], delimiter[64];
    size_t start = 0;
    size_t i;
    if (!copy_string_to_fixed_buffer(ctx, str, "split", buffer, sizeof(buffer), &str_len)) {
        return fe_nil(ctx);
    }
    if (!copy_string_to_fixed_buffer(ctx, delim, "split", delimiter, sizeof(delimiter), &delim_len)) {
        return fe_nil(ctx);
    }

    fe_Object *result = fe_nil(ctx);
    fe_Object **tail = &result;

    if (delim_len == 0) {
        *tail = fe_cons(ctx, fe_string(ctx, buffer, str_len), fe_nil(ctx));
        return result;
    }

    while (start < str_len && bytes_contains_byte(delimiter, delim_len, (unsigned char)buffer[start])) {
        start++;
    }

    for (i = start; i <= str_len; i++) {
        int at_end = (i == str_len);
        int is_delim = !at_end && bytes_contains_byte(delimiter, delim_len, (unsigned char)buffer[i]);

        if (!at_end && !is_delim) {
            continue;
        }

        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }

        if (i > start) {
            *tail = fe_cons(ctx, fe_string(ctx, buffer + start, i - start), fe_nil(ctx));
            tail = fe_cdr_ptr(ctx, *tail);
        }

        while (i + 1 < str_len &&
               bytes_contains_byte(delimiter, delim_len, (unsigned char)buffer[i + 1])) {
            i++;
        }
        start = i + 1;
    }

    if (str_len > 0 && start < str_len &&
        !bytes_contains_byte(delimiter, delim_len, (unsigned char)buffer[str_len - 1])) {
        *tail = fe_cons(ctx, fe_string(ctx, buffer + start, str_len - start), fe_nil(ctx));
        tail = fe_cdr_ptr(ctx, *tail);
    }

    return result;
}

static fe_Object* builtin_string_trim(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "trim");
    fe_Object *str = fe_nextarg(ctx, &args);
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;
    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "trim");

    size_t str_len = fe_strlen(ctx, str);
    char buffer[1024];
    size_t start = 0;
    size_t end;
    if (!copy_string_to_fixed_buffer(ctx, str, "trim", buffer, sizeof(buffer), &str_len)) {
        return fe_nil(ctx);
    }

    if (str_len == 0) {
        return fe_string(ctx, "", 0);
    }

    /* Trim leading whitespace */
    while (start < str_len && isspace((unsigned char)buffer[start])) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        start++;
    }

    if (start == str_len) {
        return fe_string(ctx, "", 0);
    }

    /* Trim trailing whitespace */
    end = str_len;
    while (end > start && isspace((unsigned char)buffer[end - 1])) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        end--;
    }

    return fe_string(ctx, buffer + start, end - start);
}

static fe_Object* builtin_string_contains(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "contains");
    fe_Object *str = fe_nextarg(ctx, &args);
    fe_Object *substr = fe_nextarg(ctx, &args);

    FEX_CHECK_TYPE(ctx, str, FE_TSTRING, "contains");
    FEX_CHECK_TYPE(ctx, substr, FE_TSTRING, "contains");

    size_t str_len = fe_strlen(ctx, str);
    size_t substr_len = fe_strlen(ctx, substr);

    char buffer[1024], search[256];
    if (!copy_string_to_fixed_buffer(ctx, str, "contains", buffer, sizeof(buffer), &str_len)) {
        return fe_nil(ctx);
    }
    if (!copy_string_to_fixed_buffer(ctx, substr, "contains", search, sizeof(search), &substr_len)) {
        return fe_nil(ctx);
    }

    return fe_bool(ctx, bytes_contains_sequence(buffer, str_len, search, substr_len));
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
    size_t fill_len = 0;
    if (!copy_string_to_fixed_buffer(ctx, fill_obj, "makestring", fill_buffer, sizeof(fill_buffer), &fill_len)) {
        return fe_nil(ctx);
    }

    if (fill_len == 0) {
        fe_error(ctx, "makestring: fill character cannot be empty");
        return fe_nil(ctx);
    }

    char fill_char = fill_buffer[0];  /* Use first character only */

    fe_Object *obj = fe_string_raw(ctx, length, fill_char);

    return obj;
}

static fe_Object* builtin_make_bytes(fe_Context *ctx, fe_Object *args) {
    fe_Object *length_obj;
    fe_Object *fill_obj;
    fe_Number length_num;
    int length;
    int fill = 0;

    FEX_CHECK_ARGS(ctx, args, 1, "makebytes");
    length_obj = fe_nextarg(ctx, &args);
    fill_obj = fe_isnil(ctx, args) ? fe_nil(ctx) : fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, length_obj, FE_TNUMBER, "makebytes");
    if (!fe_isnil(ctx, fill_obj)) {
        FEX_CHECK_TYPE(ctx, fill_obj, FE_TNUMBER, "makebytes");
        fill = (int)fe_tonumber(ctx, fill_obj);
        if (fill < 0 || fill > 255) {
            fe_error(ctx, "makebytes: fill must be between 0 and 255");
            return fe_nil(ctx);
        }
    }

    length_num = fe_tonumber(ctx, length_obj);
    length = (int)length_num;
    if (length < 0) {
        fe_error(ctx, "makebytes: length must be non-negative");
        return fe_nil(ctx);
    }
    return fe_bytes_raw(ctx, (size_t)length, (unsigned char)fill);
}

static fe_Object* builtin_to_bytes(fe_Context *ctx, fe_Object *args) {
    fe_Object *obj;

    FEX_CHECK_ARGS(ctx, args, 1, "tobytes");
    obj = fe_nextarg(ctx, &args);
    if (fe_type(ctx, obj) == FE_TBYTES) {
        return obj;
    }
    if (fe_type(ctx, obj) == FE_TSTRING) {
        return string_to_bytes(ctx, obj, "tobytes");
    }
    fe_error(ctx, "tobytes: type mismatch");
    return fe_nil(ctx);
}

static fe_Object* builtin_bytes_length(fe_Context *ctx, fe_Object *args) {
    fe_Object *bytes_obj;

    FEX_CHECK_ARGS(ctx, args, 1, "byteslen");
    bytes_obj = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, bytes_obj, FE_TBYTES, "byteslen");
    return fe_make_number(ctx, (fe_Number)fe_byteslen(ctx, bytes_obj));
}

static fe_Object* builtin_byte_at(fe_Context *ctx, fe_Object *args) {
    fe_Object *bytes_obj;
    fe_Object *index_obj;
    int index;
    unsigned char value = 0;

    FEX_CHECK_ARGS(ctx, args, 2, "byteat");
    bytes_obj = fe_nextarg(ctx, &args);
    index_obj = fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, bytes_obj, FE_TBYTES, "byteat");
    FEX_CHECK_TYPE(ctx, index_obj, FE_TNUMBER, "byteat");

    index = (int)fe_tonumber(ctx, index_obj);
    if (index < 0 || (size_t)index >= fe_byteslen(ctx, bytes_obj)) {
        fe_error(ctx, "byteat: index out of range");
        return fe_nil(ctx);
    }
    fe_bytescopy(ctx, bytes_obj, (size_t)index, &value, 1);
    return fe_make_number(ctx, (fe_Number)value);
}

static fe_Object* builtin_bytes_slice(fe_Context *ctx, fe_Object *args) {
    fe_Object *bytes_obj;
    fe_Object *start_obj;
    fe_Object *end_obj;
    size_t len;
    int start;
    int end;
    size_t slice_len;
    unsigned char *buffer;
    fe_Object *result;

    FEX_CHECK_ARGS(ctx, args, 2, "byteslice");
    bytes_obj = fe_nextarg(ctx, &args);
    start_obj = fe_nextarg(ctx, &args);
    end_obj = fe_isnil(ctx, args) ? fe_nil(ctx) : fe_nextarg(ctx, &args);
    FEX_CHECK_TYPE(ctx, bytes_obj, FE_TBYTES, "byteslice");
    FEX_CHECK_TYPE(ctx, start_obj, FE_TNUMBER, "byteslice");

    len = fe_byteslen(ctx, bytes_obj);
    start = (int)fe_tonumber(ctx, start_obj);
    end = fe_isnil(ctx, end_obj) ? (int)len : (int)fe_tonumber(ctx, end_obj);

    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (end > (int)len) end = (int)len;
    if (start >= end) {
        return fe_bytes(ctx, "", 0);
    }

    slice_len = (size_t)(end - start);
    buffer = builtin_alloc(ctx, slice_len > 0 ? slice_len : 1);
    if (!buffer) {
        fe_error(ctx, "byteslice: out of memory");
        return fe_nil(ctx);
    }
    fe_bytescopy(ctx, bytes_obj, (size_t)start, buffer, slice_len);
    result = fe_bytes(ctx, buffer, slice_len);
    builtin_free(ctx, buffer);
    return result;
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
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error;
    FEX_CHECK_TYPE(ctx, list, FE_TPAIR, "contains");

    while (!fe_isnil(ctx, list)) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        count++;
        list = fe_cdr(ctx, list);
    }

    return fe_make_number(ctx, (fe_Number)count);
}

static fe_Object* builtin_list_nth(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "nth");
    fe_Object *list = fe_nextarg(ctx, &args);
    fe_Object *index_obj = fe_nextarg(ctx, &args);
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error;
    int index = (int)fe_tonumber(ctx, index_obj);
    int i;

    FEX_CHECK_TYPE(ctx, list, FE_TPAIR, "nth");

    for (i = 0; i < index && !fe_isnil(ctx, list); i++) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
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
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error;
    FEX_CHECK_TYPE(ctx, first, FE_TPAIR, "nth");
    if (fe_isnil(ctx, args)) return first;

    /* Build result list by copying first list and appending rest */
    fe_Object *result = fe_nil(ctx);
    fe_Object **tail = &result;

    /* Copy first list */
    fe_Object *current = first;
    while (!fe_isnil(ctx, current)) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
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
            abort_error = builtin_poll_abort(ctx, &poll_countdown);
            if (abort_error != NULL) {
                fe_error(ctx, abort_error);
                return fe_nil(ctx);
            }
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
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error;
    FEX_CHECK_TYPE(ctx, list, FE_TPAIR, "reverse");
    while (!fe_isnil(ctx, list)) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
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
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error;

    while (!fe_isnil(ctx, args)) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
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
    size_t poll_countdown;
} JsonParser;

typedef struct {
    fe_Context *ctx;
    size_t poll_countdown;
} JsonWriter;

static void json_skip_ws(JsonParser *parser) {
    const char *abort_error;

    while (*parser->current &&
           (*parser->current == ' ' || *parser->current == '\t' ||
            *parser->current == '\n' || *parser->current == '\r')) {
        abort_error = builtin_poll_abort(parser->ctx, &parser->poll_countdown);
        if (abort_error != NULL) {
            fe_error(parser->ctx, abort_error);
            return;
        }
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
    const char *abort_error;

    buf.ctx = NULL;
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

        abort_error = builtin_poll_abort(parser->ctx, &parser->poll_countdown);
        if (abort_error != NULL) {
            buf_free(&buf);
            fe_error(parser->ctx, abort_error);
            return fe_nil(parser->ctx);
        }
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
    const char *abort_error;

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
        abort_error = builtin_poll_abort(parser->ctx, &parser->poll_countdown);
        if (abort_error != NULL) {
            fe_error(parser->ctx, abort_error);
            return fe_nil(parser->ctx);
        }
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
    const char *abort_error;

    parser->current++;
    json_skip_ws(parser);
    if (*parser->current == '}') {
        parser->current++;
        return map;
    }

    for (;;) {
        fe_Object *key;
        fe_Object *value;

        abort_error = builtin_poll_abort(parser->ctx, &parser->poll_countdown);
        if (abort_error != NULL) {
            fe_error(parser->ctx, abort_error);
            return fe_nil(parser->ctx);
        }
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
    size_t poll_countdown = 1;
    const char *abort_error;

    while (!fe_isnil(ctx, obj)) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return 0;
        }
        if (fe_type(ctx, obj) != FE_TPAIR) {
            return 0;
        }
        obj = fe_cdr(ctx, obj);
    }
    return 1;
}

static int json_write_string(JsonWriter *writer, fe_Object *obj, TextBuffer *buf) {
    char *text;
    size_t text_len;
    size_t i;
    const char *abort_error;

    text = string_to_buffer(writer->ctx, obj, "tojson", &text_len);
    if (!text) {
        return 0;
    }
    if (!buf_append_char(buf, '"')) {
        builtin_free(writer->ctx, text);
        return 0;
    }
    for (i = 0; i < text_len; i++) {
        unsigned char chr = (unsigned char)text[i];
        abort_error = builtin_poll_abort(writer->ctx, &writer->poll_countdown);
        if (abort_error != NULL) {
            builtin_free(writer->ctx, text);
            fe_error(writer->ctx, abort_error);
            return 0;
        }
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
                    snprintf(hexbuf, sizeof(hexbuf),"\\u%04x", chr);
                    if (!buf_append_str(buf, hexbuf)) goto fail;
                } else if (!buf_append_char(buf, (char)chr)) {
                    goto fail;
                }
                break;
        }
    }
    builtin_free(writer->ctx, text);
    return buf_append_char(buf, '"');

fail:
    builtin_free(writer->ctx, text);
    return 0;
}

static int json_write_value(JsonWriter *writer, fe_Object *obj, TextBuffer *buf) {
    char number_buf[64];
    switch (fe_type(writer->ctx, obj)) {
        case FE_TNIL:
            return buf_append_str(buf, "null");
        case FE_TBOOLEAN:
            return buf_append_str(buf, (obj == fe_bool(writer->ctx, 1)) ? "true" : "false");
        case FE_TNUMBER:
            snprintf(number_buf, sizeof(number_buf),"%.15g", fe_tonumber(writer->ctx, obj));
            return buf_append_str(buf, number_buf);
        case FE_TSTRING:
            return json_write_string(writer, obj, buf);
        case FE_TMAP: {
            fe_Object *keys = fe_map_keys(writer->ctx, obj);
            int first = 1;
            if (!buf_append_char(buf, '{')) return 0;
            while (!fe_isnil(writer->ctx, keys)) {
                fe_Object *key = fe_car(writer->ctx, keys);
                const char *abort_error = builtin_poll_abort(writer->ctx, &writer->poll_countdown);
                if (abort_error != NULL) {
                    fe_error(writer->ctx, abort_error);
                    return 0;
                }
                if (!first && !buf_append_char(buf, ',')) return 0;
                if (!json_write_string(writer, key, buf)) return 0;
                if (!buf_append_char(buf, ':')) return 0;
                if (!json_write_value(writer, fe_map_get(writer->ctx, obj, key), buf)) return 0;
                first = 0;
                keys = fe_cdr(writer->ctx, keys);
            }
            return buf_append_char(buf, '}');
        }
        case FE_TPAIR: {
            if (!json_is_proper_list(writer->ctx, obj)) {
                fe_error(writer->ctx, "tojson: cannot serialize dotted pair");
                return 0;
            }
            if (!buf_append_char(buf, '[')) return 0;
            {
                int first = 1;
                fe_Object *list = obj;
                while (!fe_isnil(writer->ctx, list)) {
                    const char *abort_error = builtin_poll_abort(writer->ctx, &writer->poll_countdown);
                    if (abort_error != NULL) {
                        fe_error(writer->ctx, abort_error);
                        return 0;
                    }
                    if (!first && !buf_append_char(buf, ',')) return 0;
                    if (!json_write_value(writer, fe_car(writer->ctx, list), buf)) return 0;
                    first = 0;
                    list = fe_cdr(writer->ctx, list);
                }
            }
            return buf_append_char(buf, ']');
        }
        default:
            fe_error(writer->ctx, "tojson: unsupported value type");
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
    parser.poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    result = json_parse_value(&parser);
    json_skip_ws(&parser);
    if (*parser.current != '\0') {
        builtin_free(ctx, text);
        fe_error(ctx, "parsejson: trailing characters");
        return fe_nil(ctx);
    }
    builtin_free(ctx, text);
    return result;
}

static fe_Object* builtin_to_json(fe_Context *ctx, fe_Object *args) {
    TextBuffer buf;
    fe_Object *value;
    fe_Object *result;
    JsonWriter writer;

    FEX_CHECK_ARGS(ctx, args, 1, "tojson");
    value = fe_nextarg(ctx, &args);

    buf.ctx = NULL;
    buf.data = NULL;
    buf.len = 0;
    buf.cap = 0;
    writer.ctx = ctx;
    writer.poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    if (!json_write_value(&writer, value, &buf)) {
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
    size_t total_read;
    size_t poll_countdown = 1;
    const char *abort_error;
    char msg[160];

    file = fopen_utf8(ctx, filename,
#ifdef _WIN32
        L"rb",
#else
        "rb",
#endif
        func_name);
    if (!file) {
        snprintf(msg, sizeof(msg),"%s: could not open file", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        snprintf(msg, sizeof(msg),"%s: could not determine file size", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        snprintf(msg, sizeof(msg),"%s: could not determine file size", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    if ((size_t)size > max_size) {
        fclose(file);
        snprintf(msg, sizeof(msg),"%s: file too large", func_name);
        fe_error(ctx, msg);
        return NULL;
    }
    rewind(file);

    buffer = (char*)tracked_builtin_alloc(ctx, (size_t)size + 1);
    if (!buffer) {
        fclose(file);
        snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
        fe_ctx_memory_error(ctx, msg);
        return NULL;
    }

    total_read = 0;
    while (total_read < (size_t)size) {
        size_t chunk_size = (size_t)size - total_read;
        if (chunk_size > FEX_FILE_IO_CHUNK_SIZE) {
            chunk_size = FEX_FILE_IO_CHUNK_SIZE;
        }

        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            tracked_builtin_free(ctx, buffer);
            fclose(file);
            fe_error(ctx, abort_error);
            return NULL;
        }

        bytes_read = fread(buffer + total_read, 1, chunk_size, file);
        total_read += bytes_read;
        if (bytes_read < chunk_size) {
            if (ferror(file)) {
                tracked_builtin_free(ctx, buffer);
                fclose(file);
                snprintf(msg, sizeof(msg),"%s: error reading file", func_name);
                fe_error(ctx, msg);
                return NULL;
            }
            break;
        }
    }

    fclose(file);
    buffer[total_read] = '\0';
    if (out_size) {
        *out_size = total_read;
    }
    return buffer;
}

static fe_Object* builtin_path_join(fe_Context *ctx, fe_Object *args) {
    TextBuffer buf;
    int need_sep = 0;
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;

    FEX_CHECK_ARGS(ctx, args, 1, "pathjoin");
    buf.ctx = NULL;
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
            abort_error = builtin_poll_abort(ctx, &poll_countdown);
            if (abort_error != NULL) {
                builtin_free(ctx, part);
                buf_free(&buf);
                fe_error(ctx, abort_error);
                return fe_nil(ctx);
            }
            start++;
        }
        while (end > start && is_path_separator_char(part[end - 1])) {
            abort_error = builtin_poll_abort(ctx, &poll_countdown);
            if (abort_error != NULL) {
                builtin_free(ctx, part);
                buf_free(&buf);
                fe_error(ctx, abort_error);
                return fe_nil(ctx);
            }
            end--;
        }

        if (need_sep && start < end && buf.len > 0 && buf.data[buf.len - 1] != '/') {
            if (!buf_append_char(&buf, '/')) {
                builtin_free(ctx, part);
                buf_free(&buf);
                fe_error(ctx, "pathjoin: out of memory");
                return fe_nil(ctx);
            }
        }

        for (i = start; i < end; i++) {
            char chr = is_path_separator_char(part[i]) ? '/' : part[i];
            abort_error = builtin_poll_abort(ctx, &poll_countdown);
            if (abort_error != NULL) {
                builtin_free(ctx, part);
                buf_free(&buf);
                fe_error(ctx, abort_error);
                return fe_nil(ctx);
            }
            if (!buf_append_char(&buf, chr)) {
                builtin_free(ctx, part);
                buf_free(&buf);
                fe_error(ctx, "pathjoin: out of memory");
                return fe_nil(ctx);
            }
        }
        need_sep = buf.len > 0;
        builtin_free(ctx, part);
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
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;

    FEX_CHECK_ARGS(ctx, args, 1, "dirname");
    path_obj = fe_nextarg(ctx, &args);
    path = string_to_cstr(ctx, path_obj, "dirname");
    if (!path) return fe_nil(ctx);

    end = strlen(path);
    while (end > 1 && is_path_separator_char(path[end - 1])) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            builtin_free(ctx, path);
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        end--;
    }
    for (i = end; i > 0; i--) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            builtin_free(ctx, path);
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        if (is_path_separator_char(path[i - 1])) {
            while (i > 1 && is_path_separator_char(path[i - 2])) {
                abort_error = builtin_poll_abort(ctx, &poll_countdown);
                if (abort_error != NULL) {
                    builtin_free(ctx, path);
                    fe_error(ctx, abort_error);
                    return fe_nil(ctx);
                }
                i--;
            }
            if (i == 1) {
                result = fe_string(ctx, "/", 1);
            } else {
                result = fe_string(ctx, path, i - 1);
            }
            builtin_free(ctx, path);
            return result;
        }
    }

    builtin_free(ctx, path);
    return fe_string(ctx, ".", 1);
}

static fe_Object* builtin_basename(fe_Context *ctx, fe_Object *args) {
    fe_Object *path_obj;
    char *path;
    size_t end;
    size_t start;
    fe_Object *result;
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;

    FEX_CHECK_ARGS(ctx, args, 1, "basename");
    path_obj = fe_nextarg(ctx, &args);
    path = string_to_cstr(ctx, path_obj, "basename");
    if (!path) return fe_nil(ctx);

    end = strlen(path);
    while (end > 1 && is_path_separator_char(path[end - 1])) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            builtin_free(ctx, path);
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        end--;
    }
    start = end;
    while (start > 0 && !is_path_separator_char(path[start - 1])) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            builtin_free(ctx, path);
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        start--;
    }
    result = fe_string(ctx, path + start, end - start);
    builtin_free(ctx, path);
    return result;
}

static fe_Object* builtin_exists(fe_Context *ctx, fe_Object *args) {
    fe_Object *path_obj;
    char *path;
    int exists;

    FEX_CHECK_ARGS(ctx, args, 1, "exists");
    path_obj = fe_nextarg(ctx, &args);
    path = string_to_cstr(ctx, path_obj, "exists");
    if (!path) {
        return fe_nil(ctx);
    }

    exists = path_exists_cstr(path);
    builtin_free(ctx, path);
    return fe_bool(ctx, exists);
}

static fe_Object* builtin_list_dir(fe_Context *ctx, fe_Object *args) {
    fe_Object *path_obj;
    char *path;
    CStringArray entries;
    fe_Object *result;
    size_t poll_countdown = 1;
    const char *abort_error;

    FEX_CHECK_ARGS(ctx, args, 1, "listdir");
    path_obj = fe_nextarg(ctx, &args);
    path = string_to_cstr(ctx, path_obj, "listdir");
    if (!path) {
        return fe_nil(ctx);
    }

        entries.ctx = ctx;
        entries.items = NULL;
        entries.count = 0;

    if (!path_is_directory_cstr(path)) {
        builtin_free(ctx, path);
        fe_error(ctx, "listdir: not a directory");
        return fe_nil(ctx);
    }

#ifdef _WIN32
    {
        TextBuffer pattern;
        WIN32_FIND_DATAW find_data;
        HANDLE find_handle;
        wchar_t *wide_pattern;

        pattern.ctx = NULL;
        pattern.data = NULL;
        pattern.len = 0;
        pattern.cap = 0;
        if (!buf_append_str(&pattern, path)) {
            builtin_free(ctx, path);
            fe_error(ctx, "listdir: out of memory");
            return fe_nil(ctx);
        }
        if (pattern.len == 0 || !is_path_separator_char(pattern.data[pattern.len - 1])) {
            if (!buf_append_char(&pattern, '\\')) {
                builtin_free(ctx, path);
                buf_free(&pattern);
                fe_error(ctx, "listdir: out of memory");
                return fe_nil(ctx);
            }
        }
        if (!buf_append_char(&pattern, '*')) {
            builtin_free(ctx, path);
            buf_free(&pattern);
            fe_error(ctx, "listdir: out of memory");
            return fe_nil(ctx);
        }

        wide_pattern = utf8_to_wide_alloc(ctx, pattern.data, "listdir");
        buf_free(&pattern);
        if (!wide_pattern) {
            builtin_free(ctx, path);
            return fe_nil(ctx);
        }
        find_handle = FindFirstFileW(wide_pattern, &find_data);
        builtin_free(ctx, wide_pattern);
        if (find_handle == INVALID_HANDLE_VALUE) {
            builtin_free(ctx, path);
            fe_error(ctx, "listdir: could not read directory");
            return fe_nil(ctx);
        }

        do {
            abort_error = builtin_poll_abort(ctx, &poll_countdown);
            if (abort_error != NULL) {
                FindClose(find_handle);
                builtin_free(ctx, path);
                free_cstring_array(&entries);
                fe_error(ctx, abort_error);
                return fe_nil(ctx);
            }
            if (wcscmp(find_data.cFileName, L".") == 0 ||
                wcscmp(find_data.cFileName, L"..") == 0) {
                continue;
            }
            {
                char *entry_name = wide_to_utf8_alloc(ctx, find_data.cFileName, "listdir");
                if (!entry_name) {
                    FindClose(find_handle);
                    builtin_free(ctx, path);
                    free_cstring_array(&entries);
                    return fe_nil(ctx);
                }
                if (!cstring_array_push_copy(&entries, entry_name)) {
                    builtin_free(ctx, entry_name);
                    FindClose(find_handle);
                    builtin_free(ctx, path);
                    free_cstring_array(&entries);
                    fe_error(ctx, "listdir: out of memory");
                    return fe_nil(ctx);
                }
                builtin_free(ctx, entry_name);
            }
        } while (FindNextFileW(find_handle, &find_data));

        FindClose(find_handle);
    }
#else
    {
        DIR *dir = opendir(path);
        struct dirent *entry;

        if (!dir) {
            builtin_free(ctx, path);
            fe_error(ctx, "listdir: could not read directory");
            return fe_nil(ctx);
        }

        while ((entry = readdir(dir)) != NULL) {
            abort_error = builtin_poll_abort(ctx, &poll_countdown);
            if (abort_error != NULL) {
                closedir(dir);
                builtin_free(ctx, path);
                free_cstring_array(&entries);
                fe_error(ctx, abort_error);
                return fe_nil(ctx);
            }
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (!cstring_array_push_copy(&entries, entry->d_name)) {
                closedir(dir);
                builtin_free(ctx, path);
                free_cstring_array(&entries);
                fe_error(ctx, "listdir: out of memory");
                return fe_nil(ctx);
            }
        }

        closedir(dir);
    }
#endif

    cstring_array_sort(&entries);
    result = cstring_array_to_list(ctx, &entries);
    free_cstring_array(&entries);
    builtin_free(ctx, path);
    return result;
}

static fe_Object* builtin_make_dir(fe_Context *ctx, fe_Object *args) {
    fe_Object *path_obj;
    char *path;
    int ok;

    FEX_CHECK_ARGS(ctx, args, 1, "mkdir");
    path_obj = fe_nextarg(ctx, &args);
    path = string_to_cstr(ctx, path_obj, "mkdir");
    if (!path) {
        return fe_nil(ctx);
    }

    ok = create_directory_cstr(path);
    builtin_free(ctx, path);
    if (!ok) {
        fe_error(ctx, "mkdir: could not create directory");
        return fe_nil(ctx);
    }
    return fe_bool(ctx, 1);
}

static fe_Object* builtin_make_dir_parents(fe_Context *ctx, fe_Object *args) {
    fe_Object *path_obj;
    char *path;
    int ok;

    FEX_CHECK_ARGS(ctx, args, 1, "mkdirp");
    path_obj = fe_nextarg(ctx, &args);
    path = string_to_cstr(ctx, path_obj, "mkdirp");
    if (!path) {
        return fe_nil(ctx);
    }

    ok = ensure_directory_tree_cstr(path);
    builtin_free(ctx, path);
    if (!ok) {
        fe_error(ctx, "mkdirp: could not create directory tree");
        return fe_nil(ctx);
    }
    return fe_bool(ctx, 1);
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
    builtin_free(ctx, filename);
    if (!buffer) return fe_nil(ctx);

    result = fe_string(ctx, buffer, bytes_read);
    tracked_builtin_free(ctx, buffer);
    return result;
}

static fe_Object* builtin_read_bytes(fe_Context *ctx, fe_Object *args) {
    fe_Object *filename_obj;
    char *filename;
    char *buffer;
    size_t bytes_read;
    fe_Object *result;

    FEX_CHECK_ARGS(ctx, args, 1, "readbytes");
    filename_obj = fe_nextarg(ctx, &args);
    filename = string_to_cstr(ctx, filename_obj, "readbytes");
    if (!filename) return fe_nil(ctx);

    buffer = read_file_dynamic(ctx, filename, 256 * 1024, &bytes_read, "readbytes");
    builtin_free(ctx, filename);
    if (!buffer) return fe_nil(ctx);

    result = fe_bytes(ctx, buffer, bytes_read);
    tracked_builtin_free(ctx, buffer);
    return result;
}

static fe_Object* builtin_write_file(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 2, "writefile");
    fe_Object *filename_obj = fe_nextarg(ctx, &args);
    fe_Object *content_obj = fe_nextarg(ctx, &args);
    char *filename = string_to_cstr(ctx, filename_obj, "writefile");
    char *content = string_to_cstr(ctx, content_obj, "writefile");
    size_t content_len;
    size_t written;
    size_t total_written;
    size_t chunk_size;
    size_t poll_countdown = 1;
    const char *abort_error;
    FILE *file;

    if (!filename || !content) {
        builtin_free(ctx, filename);
        builtin_free(ctx, content);
        return fe_nil(ctx);
    }

    content_len = strlen(content);
    file = fopen_utf8(ctx, filename,
#ifdef _WIN32
        L"wb",
#else
        "wb",
#endif
        "writefile");
    if (!file) {
        builtin_free(ctx, filename);
        builtin_free(ctx, content);
        fe_error(ctx, "writefile: could not open file for writing");
        return fe_nil(ctx);
    }

    total_written = 0;
    while (total_written < content_len) {
        chunk_size = content_len - total_written;
        if (chunk_size > FEX_FILE_IO_CHUNK_SIZE) {
            chunk_size = FEX_FILE_IO_CHUNK_SIZE;
        }
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fclose(file);
            builtin_free(ctx, filename);
            builtin_free(ctx, content);
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        written = fwrite(content + total_written, 1, chunk_size, file);
        total_written += written;
        if (written < chunk_size || ferror(file)) {
            fclose(file);
            builtin_free(ctx, filename);
            builtin_free(ctx, content);
            fe_error(ctx, "writefile: error writing file");
            return fe_nil(ctx);
        }
    }

    fclose(file);
    builtin_free(ctx, filename);
    builtin_free(ctx, content);
    return fe_make_number(ctx, (fe_Number)total_written);
}

static fe_Object* builtin_write_bytes(fe_Context *ctx, fe_Object *args) {
    fe_Object *filename_obj;
    fe_Object *content_obj;
    char *filename;
    unsigned char *content;
    size_t content_len;
    size_t written;
    size_t total_written;
    size_t chunk_size;
    size_t poll_countdown = 1;
    const char *abort_error;
    FILE *file;

    FEX_CHECK_ARGS(ctx, args, 2, "writebytes");
    filename_obj = fe_nextarg(ctx, &args);
    content_obj = fe_nextarg(ctx, &args);
    filename = string_to_cstr(ctx, filename_obj, "writebytes");
    if (!filename) return fe_nil(ctx);

    content = bytes_to_buffer(ctx, content_obj, "writebytes", &content_len);
    if (!content) {
        builtin_free(ctx, filename);
        return fe_nil(ctx);
    }

    file = fopen_utf8(ctx, filename,
#ifdef _WIN32
        L"wb",
#else
        "wb",
#endif
        "writebytes");
    if (!file) {
        builtin_free(ctx, filename);
        builtin_free(ctx, content);
        fe_error(ctx, "writebytes: could not open file for writing");
        return fe_nil(ctx);
    }

    total_written = 0;
    while (total_written < content_len) {
        chunk_size = content_len - total_written;
        if (chunk_size > FEX_FILE_IO_CHUNK_SIZE) {
            chunk_size = FEX_FILE_IO_CHUNK_SIZE;
        }
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fclose(file);
            builtin_free(ctx, filename);
            builtin_free(ctx, content);
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        written = fwrite(content + total_written, 1, chunk_size, file);
        total_written += written;
        if (written < chunk_size || ferror(file)) {
            fclose(file);
            builtin_free(ctx, filename);
            builtin_free(ctx, content);
            fe_error(ctx, "writebytes: error writing file");
            return fe_nil(ctx);
        }
    }

    fclose(file);
    builtin_free(ctx, filename);
    builtin_free(ctx, content);
    return fe_make_number(ctx, (fe_Number)total_written);
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
    builtin_free(ctx, filename);
    if (!buffer) return fe_nil(ctx);

    parser.ctx = ctx;
    parser.current = buffer;
    parser.poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    result = json_parse_value(&parser);
    json_skip_ws(&parser);
    if (*parser.current != '\0') {
        tracked_builtin_free(ctx, buffer);
        fe_error(ctx, "readjson: trailing characters");
        return fe_nil(ctx);
    }
    tracked_builtin_free(ctx, buffer);
    return result;
}

static fe_Object* builtin_write_json(fe_Context *ctx, fe_Object *args) {
    fe_Object *filename_obj;
    fe_Object *value;
    char *filename;
    TextBuffer buf;
    FILE *file;
    size_t written;
    size_t total_written;
    size_t chunk_size;
    size_t poll_countdown = 1;
    const char *abort_error;
    JsonWriter writer;

    FEX_CHECK_ARGS(ctx, args, 2, "writejson");
    filename_obj = fe_nextarg(ctx, &args);
    value = fe_nextarg(ctx, &args);

    filename = string_to_cstr(ctx, filename_obj, "writejson");
    if (!filename) return fe_nil(ctx);

    buf.ctx = NULL;
    buf.data = NULL;
    buf.len = 0;
    buf.cap = 0;
    writer.ctx = ctx;
    writer.poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    if (!json_write_value(&writer, value, &buf)) {
        builtin_free(ctx, filename);
        buf_free(&buf);
        return fe_nil(ctx);
    }

    file = fopen_utf8(ctx, filename,
#ifdef _WIN32
        L"wb",
#else
        "wb",
#endif
        "writejson");
    if (!file) {
        builtin_free(ctx, filename);
        buf_free(&buf);
        fe_error(ctx, "writejson: could not open file for writing");
        return fe_nil(ctx);
    }

    total_written = 0;
    while (total_written < buf.len) {
        chunk_size = buf.len - total_written;
        if (chunk_size > FEX_FILE_IO_CHUNK_SIZE) {
            chunk_size = FEX_FILE_IO_CHUNK_SIZE;
        }
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fclose(file);
            builtin_free(ctx, filename);
            buf_free(&buf);
            fe_error(ctx, abort_error);
            return fe_nil(ctx);
        }
        written = fwrite(buf.data + total_written, 1, chunk_size, file);
        total_written += written;
        if (written < chunk_size || ferror(file)) {
            fclose(file);
            builtin_free(ctx, filename);
            buf_free(&buf);
            fe_error(ctx, "writejson: error writing file");
            return fe_nil(ctx);
        }
    }

    fclose(file);
    builtin_free(ctx, filename);
    buf_free(&buf);
    return fe_make_number(ctx, (fe_Number)total_written);
}

static int count_list_length(fe_Context *ctx, fe_Object *list,
                             const char *func_name, const char *label) {
    int count = 0;
    fe_Object *node = list;
    char msg[160];
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error;

    while (!fe_isnil(ctx, node)) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return -1;
        }
        if (fe_type(ctx, node) != FE_TPAIR) {
            snprintf(msg, sizeof(msg),"%s: %s must be a list", func_name, label);
            fe_error(ctx, msg);
            return -1;
        }
        count++;
        node = fe_cdr(ctx, node);
    }

    return count;
}

static fe_Object* map_get_named_value(fe_Context *ctx, fe_Object *map,
                                      const char *name, int *present) {
    fe_Object *key = fe_symbol(ctx, name);

    if (fe_map_has(ctx, map, key)) {
        *present = 1;
        return fe_map_get(ctx, map, key);
    }

    *present = 0;
    return fe_nil(ctx);
}

static int object_to_input_buffer(fe_Context *ctx, fe_Object *obj,
                                  const char *func_name,
                                  unsigned char **out_data,
                                  size_t *out_len) {
    size_t len;
    char *string_buffer;

    if (fe_isnil(ctx, obj)) {
        *out_data = NULL;
        *out_len = 0;
        return 1;
    }

    if (fe_type(ctx, obj) == FE_TBYTES) {
        *out_data = bytes_to_buffer(ctx, obj, func_name, out_len);
        return *out_data != NULL;
    }

    if (fe_type(ctx, obj) == FE_TSTRING) {
        len = fe_strlen(ctx, obj);
        string_buffer = string_to_cstr(ctx, obj, func_name);
        if (!string_buffer) {
            return 0;
        }
        *out_data = (unsigned char*)string_buffer;
        *out_len = len;
        return 1;
    }

    fe_error(ctx, "runprocess: stdin must be a string, bytes, or nil");
    return 0;
}

static int collect_process_argv(fe_Context *ctx, fe_Object *exe_obj,
                                fe_Object *args_obj, const char *func_name,
                                CStringArray *argv_out) {
    int extra_count;
    fe_Object *node;
    int index;
    char **items;
    char msg[160];
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error;

    extra_count = count_list_length(ctx, args_obj, func_name, "args");
    if (extra_count < 0) {
        return 0;
    }

    items = (char**)builtin_alloc(ctx, sizeof(char*) * ((size_t)extra_count + 2));
    if (!items) {
        snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
        fe_error(ctx, msg);
        return 0;
    }
    memset(items, 0, sizeof(char*) * ((size_t)extra_count + 2));

    items[0] = string_to_cstr(ctx, exe_obj, func_name);
    if (!items[0]) {
        builtin_free(ctx, items);
        return 0;
    }

    node = args_obj;
    index = 1;
    while (!fe_isnil(ctx, node)) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            free_cstring_items(ctx, items, index);
            fe_error(ctx, abort_error);
            return 0;
        }
        fe_Object *value = fe_car(ctx, node);
        if (fe_type(ctx, value) != FE_TSTRING) {
            free_cstring_items(ctx, items, index);
            fe_error(ctx, "runprocess: args must contain only strings");
            return 0;
        }
        items[index] = string_to_cstr(ctx, value, func_name);
        if (!items[index]) {
            free_cstring_items(ctx, items, index);
            return 0;
        }
        index++;
        node = fe_cdr(ctx, node);
    }

    argv_out->ctx = ctx;
    argv_out->items = items;
    argv_out->count = index;
    return 1;
}

static int collect_process_env(fe_Context *ctx, fe_Object *env_obj,
                               const char *func_name, CStringArray *env_out) {
    fe_Object *keys;
    int count;
    fe_Object *node;
    int index;
    char **items;
    char msg[160];
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error;

    keys = fe_map_keys(ctx, env_obj);
    count = count_list_length(ctx, keys, func_name, "env keys");
    if (count < 0) {
        return 0;
    }

    items = (char**)builtin_alloc(ctx, sizeof(char*) * ((size_t)count + 1));
    if (!items) {
        snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
        fe_error(ctx, msg);
        return 0;
    }
    memset(items, 0, sizeof(char*) * ((size_t)count + 1));

    node = keys;
    index = 0;
    while (!fe_isnil(ctx, node)) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            free_cstring_items(ctx, items, index);
            fe_error(ctx, abort_error);
            return 0;
        }
        fe_Object *key_obj = fe_car(ctx, node);
        fe_Object *value_obj = fe_map_get(ctx, env_obj, key_obj);
        char *key;
        char *value;
        size_t key_len;
        size_t value_len;

        if (fe_type(ctx, value_obj) != FE_TSTRING) {
            free_cstring_items(ctx, items, index);
            fe_error(ctx, "runprocess: env values must be strings");
            return 0;
        }

        key = string_to_cstr(ctx, key_obj, func_name);
        if (!key) {
            free_cstring_items(ctx, items, index);
            return 0;
        }
        value = string_to_cstr(ctx, value_obj, func_name);
        if (!value) {
            builtin_free(ctx, key);
            free_cstring_items(ctx, items, index);
            return 0;
        }

        key_len = strlen(key);
        value_len = strlen(value);
        items[index] = (char*)builtin_alloc(ctx, key_len + value_len + 2);
        if (!items[index]) {
            builtin_free(ctx, key);
            builtin_free(ctx, value);
            free_cstring_items(ctx, items, index);
            snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
            fe_error(ctx, msg);
            return 0;
        }
        memcpy(items[index], key, key_len);
        items[index][key_len] = '=';
        memcpy(items[index] + key_len + 1, value, value_len + 1);
        builtin_free(ctx, key);
        builtin_free(ctx, value);

        index++;
        node = fe_cdr(ctx, node);
    }

    env_out->ctx = ctx;
    env_out->items = items;
    env_out->count = count;
    return 1;
}

static int parse_process_stream_mode(fe_Context *ctx, fe_Object *value,
                                     const char *func_name,
                                     const char *option_name,
                                     int *out_mode) {
    char *mode;
    int result = 1;

    if (fe_isnil(ctx, value)) {
        *out_mode = PROCESS_STREAM_CAPTURE;
        return 1;
    }
    if (fe_type(ctx, value) != FE_TSTRING) {
        char msg[160];
        snprintf(msg, sizeof(msg),"%s: %s must be a string or nil", func_name, option_name);
        fe_error(ctx, msg);
        return 0;
    }

    mode = string_to_cstr(ctx, value, func_name);
    if (!mode) {
        return 0;
    }

    if (strcmp(mode, "capture") == 0) {
        *out_mode = PROCESS_STREAM_CAPTURE;
    } else if (strcmp(mode, "inherit") == 0) {
        *out_mode = PROCESS_STREAM_INHERIT;
    } else if (strcmp(mode, "discard") == 0) {
        *out_mode = PROCESS_STREAM_DISCARD;
    } else {
        char msg[192];
        snprintf(msg, sizeof(msg),"%s: %s must be 'capture', 'inherit', 'discard', or nil",
                func_name, option_name);
        fe_error(ctx, msg);
        result = 0;
    }

    builtin_free(ctx, mode);
    return result;
}

static int parse_process_size_limit(fe_Context *ctx, fe_Object *value,
                                    const char *func_name,
                                    const char *option_name,
                                    size_t *out_limit) {
    fe_Number number_value;

    if (fe_isnil(ctx, value)) {
        *out_limit = FEX_COMMAND_OUTPUT_MAX_BYTES;
        return 1;
    }

    if (fe_type(ctx, value) != FE_TNUMBER) {
        char msg[160];
        snprintf(msg, sizeof(msg),"%s: %s must be a non-negative integer or nil",
                func_name, option_name);
        fe_error(ctx, msg);
        return 0;
    }

    number_value = fe_tonumber(ctx, value);
    if (number_value < 0 || number_value != floor(number_value)) {
        char msg[160];
        snprintf(msg, sizeof(msg),"%s: %s must be a non-negative integer or nil",
                func_name, option_name);
        fe_error(ctx, msg);
        return 0;
    }

    if ((double)number_value > (double)((size_t)-1)) {
        char msg[160];
        snprintf(msg, sizeof(msg),"%s: %s is too large", func_name, option_name);
        fe_error(ctx, msg);
        return 0;
    }

    *out_limit = (size_t)number_value;
    return 1;
}

static int parse_process_options(fe_Context *ctx, fe_Object *opts_obj,
                                 const char *func_name,
                                 ProcessOptions *options) {
    int present;
    fe_Object *value;

    memset(options, 0, sizeof(*options));
    options->stdout_mode = PROCESS_STREAM_CAPTURE;
    options->stderr_mode = PROCESS_STREAM_CAPTURE;
    options->max_stdout = FEX_COMMAND_OUTPUT_MAX_BYTES;
    options->max_stderr = FEX_COMMAND_OUTPUT_MAX_BYTES;

    if (fe_isnil(ctx, opts_obj)) {
        return 1;
    }

    if (fe_type(ctx, opts_obj) != FE_TMAP) {
        fe_error(ctx, "runprocess: options must be a map");
        return 0;
    }

    value = map_get_named_value(ctx, opts_obj, "stdin", &present);
    if (present &&
        !object_to_input_buffer(ctx, value, func_name,
                                &options->stdin_data, &options->stdin_len)) {
        return 0;
    }

    value = map_get_named_value(ctx, opts_obj, "cwd", &present);
    if (present && !fe_isnil(ctx, value)) {
        if (fe_type(ctx, value) != FE_TSTRING) {
            fe_error(ctx, "runprocess: cwd must be a string");
            return 0;
        }
        options->cwd = string_to_cstr(ctx, value, func_name);
        if (!options->cwd) {
            return 0;
        }
    }

    value = map_get_named_value(ctx, opts_obj, "env", &present);
    if (present && !fe_isnil(ctx, value)) {
        if (fe_type(ctx, value) != FE_TMAP) {
            fe_error(ctx, "runprocess: env must be a map");
            return 0;
        }
        if (!collect_process_env(ctx, value, func_name, &options->env)) {
            return 0;
        }
        options->use_env = 1;
    }

    value = map_get_named_value(ctx, opts_obj, "stdout", &present);
    if (present &&
        !parse_process_stream_mode(ctx, value, func_name, "stdout",
                                   &options->stdout_mode)) {
        return 0;
    }

    value = map_get_named_value(ctx, opts_obj, "stderr", &present);
    if (present &&
        !parse_process_stream_mode(ctx, value, func_name, "stderr",
                                   &options->stderr_mode)) {
        return 0;
    }

    value = map_get_named_value(ctx, opts_obj, "max_stdout", &present);
    if (present &&
        !parse_process_size_limit(ctx, value, func_name, "max_stdout",
                                  &options->max_stdout)) {
        return 0;
    }

    value = map_get_named_value(ctx, opts_obj, "max_stderr", &present);
    if (present &&
        !parse_process_size_limit(ctx, value, func_name, "max_stderr",
                                  &options->max_stderr)) {
        return 0;
    }

    return 1;
}

static int open_null_redirect_file(fe_Context *ctx, TempRedirectFile *file,
                                   int writable, const char *func_name) {
    char msg[160];

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    file->handle = CreateFileA(
        "NUL",
        writable ? GENERIC_WRITE : GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file->handle == INVALID_HANDLE_VALUE) {
        snprintf(msg, sizeof(msg),"%s: could not open null device", func_name);
        fe_error(ctx, msg);
        return 0;
    }
#else
    file->fd = open("/dev/null", writable ? O_WRONLY : O_RDONLY);
    if (file->fd < 0) {
        snprintf(msg, sizeof(msg),"%s: could not open null device", func_name);
        fe_error(ctx, msg);
        return 0;
    }
#endif

    return 1;
}

static int create_process_capture_pipe(fe_Context *ctx, ProcessCapturePipe *capture_pipe,
                                       const char *func_name) {
    char msg[160];

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&capture_pipe->read_handle, &capture_pipe->write_handle, &sa, 0)) {
        snprintf(msg, sizeof(msg),"%s: could not create capture pipe", func_name);
        fe_error(ctx, msg);
        return 0;
    }
    if (!SetHandleInformation(capture_pipe->read_handle, HANDLE_FLAG_INHERIT, 0)) {
        snprintf(msg, sizeof(msg),"%s: could not configure capture pipe", func_name);
        fe_error(ctx, msg);
        return 0;
    }
#else
    int fds[2];
    int flags;

    if (pipe(fds) != 0) {
        snprintf(msg, sizeof(msg),"%s: could not create capture pipe", func_name);
        fe_error(ctx, msg);
        return 0;
    }
    flags = fcntl(fds[0], F_GETFD, 0);
    if (flags >= 0) {
        fcntl(fds[0], F_SETFD, flags | FD_CLOEXEC);
    }
    flags = fcntl(fds[1], F_GETFD, 0);
    if (flags >= 0) {
        fcntl(fds[1], F_SETFD, flags | FD_CLOEXEC);
    }

    capture_pipe->read_fd = fds[0];
    capture_pipe->write_fd = fds[1];
    flags = fcntl(capture_pipe->read_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(capture_pipe->read_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        snprintf(msg, sizeof(msg),"%s: could not configure capture pipe", func_name);
        fe_error(ctx, msg);
        return 0;
    }
#endif

    return 1;
}

static int create_process_input_pipe(fe_Context *ctx, ProcessCapturePipe *capture_pipe,
                                     const char *func_name) {
    char msg[160];

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&capture_pipe->read_handle, &capture_pipe->write_handle, &sa, 0)) {
        snprintf(msg, sizeof(msg),"%s: could not create stdin pipe", func_name);
        fe_error(ctx, msg);
        return 0;
    }
    if (!SetHandleInformation(capture_pipe->write_handle, HANDLE_FLAG_INHERIT, 0)) {
        snprintf(msg, sizeof(msg),"%s: could not configure stdin pipe", func_name);
        fe_error(ctx, msg);
        return 0;
    }
#else
    int fds[2];
    int flags;

    if (pipe(fds) != 0) {
        snprintf(msg, sizeof(msg),"%s: could not create stdin pipe", func_name);
        fe_error(ctx, msg);
        return 0;
    }
    flags = fcntl(fds[0], F_GETFD, 0);
    if (flags >= 0) {
        fcntl(fds[0], F_SETFD, flags | FD_CLOEXEC);
    }
    flags = fcntl(fds[1], F_GETFD, 0);
    if (flags >= 0) {
        fcntl(fds[1], F_SETFD, flags | FD_CLOEXEC);
    }

    capture_pipe->read_fd = fds[0];
    capture_pipe->write_fd = fds[1];
    flags = fcntl(capture_pipe->write_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(capture_pipe->write_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        snprintf(msg, sizeof(msg),"%s: could not configure stdin pipe", func_name);
        fe_error(ctx, msg);
        return 0;
    }
#endif

    return 1;
}

#ifdef _WIN32
static HANDLE duplicate_inheritable_handle(HANDLE handle) {
    HANDLE copy = INVALID_HANDLE_VALUE;

    if (handle == NULL || handle == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }
    if (!DuplicateHandle(GetCurrentProcess(), handle,
                         GetCurrentProcess(), &copy,
                         0, TRUE, DUPLICATE_SAME_ACCESS)) {
        return INVALID_HANDLE_VALUE;
    }
    return copy;
}

static DWORD WINAPI process_input_writer_thread(void *param) {
    ProcessInputWriter *writer = (ProcessInputWriter*)param;
    size_t offset = 0;

    while (offset < writer->len) {
        DWORD written = 0;
        DWORD chunk = (DWORD)((writer->len - offset > 0x7fffffffU)
            ? 0x7fffffffU
            : (DWORD)(writer->len - offset));

        if (!WriteFile(writer->write_handle, writer->data + offset,
                       chunk, &written, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE) {
                writer->failed = 0;
                writer->error_code = 0;
                CloseHandle(writer->write_handle);
                writer->write_handle = INVALID_HANDLE_VALUE;
                return 0;
            }
            writer->failed = 1;
            writer->error_code = err;
            CloseHandle(writer->write_handle);
            writer->write_handle = INVALID_HANDLE_VALUE;
            return 1;
        }

        if (written == 0) {
            writer->failed = 1;
            writer->error_code = ERROR_WRITE_FAULT;
            CloseHandle(writer->write_handle);
            writer->write_handle = INVALID_HANDLE_VALUE;
            return 1;
        }

        offset += (size_t)written;
    }

    CloseHandle(writer->write_handle);
    writer->write_handle = INVALID_HANDLE_VALUE;
    writer->failed = 0;
    writer->error_code = 0;
    return 0;
}

static void terminate_windows_process(HANDLE *process_handle) {
    if (*process_handle != NULL && *process_handle != INVALID_HANDLE_VALUE) {
        TerminateProcess(*process_handle, 1);
        WaitForSingleObject(*process_handle, INFINITE);
        CloseHandle(*process_handle);
        *process_handle = NULL;
    }
}
#endif

#ifdef _WIN32
static int drain_windows_capture_pipe(fe_Context *ctx, ProcessCapturePipe *pipe,
                                      TextBuffer *buf, size_t limit,
                                      int *overflow, const char *func_name,
                                      int process_finished,
                                      int *pipe_open, int *made_progress) {
    unsigned char chunk[4096];
    char msg[160];

    while (*pipe_open) {
        DWORD available = 0;

        if (!PeekNamedPipe(pipe->read_handle, NULL, 0, NULL, &available, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE) {
                close_process_capture_pipe_read(pipe);
                *pipe_open = 0;
                return 1;
            }
            snprintf(msg, sizeof(msg),"%s: could not read capture pipe", func_name);
            fe_error(ctx, msg);
            return 0;
        }

        if (available == 0) {
            if (process_finished) {
                close_process_capture_pipe_read(pipe);
                *pipe_open = 0;
            }
            return 1;
        }

        for (;;) {
            DWORD to_read = (DWORD)((sizeof(chunk) < available) ? sizeof(chunk) : available);
            DWORD bytes_read = 0;

            if (!ReadFile(pipe->read_handle, chunk, to_read, &bytes_read, NULL)) {
                DWORD err = GetLastError();
                if (err == ERROR_BROKEN_PIPE) {
                    close_process_capture_pipe_read(pipe);
                    *pipe_open = 0;
                    return 1;
                }
                snprintf(msg, sizeof(msg),"%s: could not read capture pipe", func_name);
                fe_error(ctx, msg);
                return 0;
            }

            if (bytes_read == 0) {
                close_process_capture_pipe_read(pipe);
                *pipe_open = 0;
                return 1;
            }

            *made_progress = 1;
            if (!append_process_capture(buf, chunk, (size_t)bytes_read, limit,
                                        overflow, ctx, func_name)) {
                return 0;
            }

            if (bytes_read >= available) {
                break;
            }
            available -= bytes_read;
        }
    }

    return 1;
}
#else
static void terminate_posix_process(pid_t *pid) {
    int wait_status = 0;

    if (*pid <= 0) {
        return;
    }

    kill(*pid, SIGKILL);
    for (;;) {
        if (waitpid(*pid, &wait_status, 0) >= 0) {
            break;
        }
        if (errno != EINTR) {
            break;
        }
    }
    *pid = -1;
}

static int write_posix_input_pipe(fe_Context *ctx, ProcessCapturePipe *pipe,
                                  const unsigned char *data, size_t len,
                                  size_t *offset, const char *func_name,
                                  int *pipe_open, int *made_progress) {
    char msg[160];

    while (*pipe_open && *offset < len) {
        ssize_t bytes_written = write(pipe->write_fd, data + *offset, len - *offset);

        if (bytes_written > 0) {
            *offset += (size_t)bytes_written;
            *made_progress = 1;
            continue;
        }

        if (bytes_written < 0 && errno == EINTR) {
            continue;
        }
        if (bytes_written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 1;
        }
        if (bytes_written < 0 && errno == EPIPE) {
            close_process_capture_pipe_write(pipe);
            *pipe_open = 0;
            return 1;
        }

        snprintf(msg, sizeof(msg),"%s: could not write stdin", func_name);
        fe_error(ctx, msg);
        return 0;
    }

    if (*pipe_open && *offset >= len) {
        close_process_capture_pipe_write(pipe);
        *pipe_open = 0;
    }

    (void)ctx;
    return 1;
}

static int drain_posix_capture_pipe(fe_Context *ctx, ProcessCapturePipe *pipe,
                                    TextBuffer *buf, size_t limit,
                                    int *overflow, const char *func_name,
                                    int *pipe_open, int *made_progress) {
    unsigned char chunk[4096];
    char msg[160];

    while (*pipe_open) {
        ssize_t bytes_read = read(pipe->read_fd, chunk, sizeof(chunk));

        if (bytes_read > 0) {
            *made_progress = 1;
            if (!append_process_capture(buf, chunk, (size_t)bytes_read, limit,
                                        overflow, ctx, func_name)) {
                return 0;
            }
            continue;
        }

        if (bytes_read == 0) {
            close_process_capture_pipe_read(pipe);
            *pipe_open = 0;
            return 1;
        }

        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }

        snprintf(msg, sizeof(msg),"%s: could not read capture pipe", func_name);
        fe_error(ctx, msg);
        return 0;
    }

    return 1;
}
#endif

#ifdef _WIN32
static int buf_append_repeat(fe_Context *ctx, TextBuffer *buf, char chr,
                             size_t count, size_t *poll_countdown,
                             const char **abort_error) {
    size_t i;

    for (i = 0; i < count; i++) {
        *abort_error = builtin_poll_abort(ctx, poll_countdown);
        if (*abort_error != NULL) {
            return 0;
        }
        if (!buf_append_char(buf, chr)) {
            return 0;
        }
    }
    return 1;
}

static int needs_windows_quotes(fe_Context *ctx, const char *arg,
                                size_t *poll_countdown,
                                const char **abort_error) {
    const char *p = arg;

    if (*arg == '\0') {
        return 1;
    }

    while (*p) {
        *abort_error = builtin_poll_abort(ctx, poll_countdown);
        if (*abort_error != NULL) {
            return -1;
        }
        if (*p == ' ' || *p == '\t' || *p == '"') {
            return 1;
        }
        p++;
    }

    return 0;
}

static int append_windows_command_arg(fe_Context *ctx, TextBuffer *buf,
                                      const char *arg,
                                      size_t *poll_countdown,
                                      const char **abort_error) {
    const char *p = arg;
    size_t backslashes = 0;
    int quote_mode = needs_windows_quotes(ctx, arg, poll_countdown, abort_error);

    if (quote_mode < 0) {
        return 0;
    }
    if (!quote_mode) {
        return buf_append_mem_polling(ctx, buf, arg, strlen(arg),
                                      poll_countdown, abort_error);
    }

    if (!buf_append_char(buf, '"')) {
        return 0;
    }

    while (*p) {
        *abort_error = builtin_poll_abort(ctx, poll_countdown);
        if (*abort_error != NULL) {
            return 0;
        }
        if (*p == '\\') {
            backslashes++;
        } else if (*p == '"') {
            if (!buf_append_repeat(ctx, buf, '\\', backslashes * 2 + 1,
                                   poll_countdown, abort_error) ||
                !buf_append_char(buf, '"')) {
                return 0;
            }
            backslashes = 0;
        } else {
            if (!buf_append_repeat(ctx, buf, '\\', backslashes,
                                   poll_countdown, abort_error) ||
                !buf_append_char(buf, *p)) {
                return 0;
            }
            backslashes = 0;
        }
        p++;
    }

    if (!buf_append_repeat(ctx, buf, '\\', backslashes * 2,
                           poll_countdown, abort_error) ||
        !buf_append_char(buf, '"')) {
        return 0;
    }

    return 1;
}

static char* build_windows_command_line(fe_Context *ctx, CStringArray *argv,
                                        const char *func_name) {
    TextBuffer buf;
    int i;
    char msg[160];
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;

    buf.ctx = NULL;
    buf.data = NULL;
    buf.len = 0;
    buf.cap = 0;

    for (i = 0; i < argv->count; i++) {
        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            buf_free(&buf);
            fe_error(ctx, abort_error);
            return NULL;
        }
        if (i > 0 && !buf_append_char(&buf, ' ')) {
            buf_free(&buf);
            snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
            fe_error(ctx, msg);
            return NULL;
        }
        if (!append_windows_command_arg(ctx, &buf, argv->items[i],
                                        &poll_countdown, &abort_error)) {
            buf_free(&buf);
            if (abort_error != NULL) {
                fe_error(ctx, abort_error);
            } else {
                snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
                fe_error(ctx, msg);
            }
            return NULL;
        }
    }

    if (!buf.data) {
        buf.data = copy_cstr_ctx(ctx, "");
        if (!buf.data) {
            snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
            fe_error(ctx, msg);
            return NULL;
        }
    }

    (void)ctx;
    return buf.data;
}

static wchar_t* build_windows_environment_block(fe_Context *ctx, CStringArray *env,
                                                const char *func_name) {
    size_t total_wchars = 1;
    size_t offset = 0;
    int i;
    wchar_t *block;
    char msg[160];
    size_t poll_countdown = FEX_BUILTIN_ABORT_CHECK_INTERVAL;
    const char *abort_error = NULL;

    for (i = 0; i < env->count; i++) {
        int needed;

        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
            return NULL;
        }

        needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     env->items[i], -1, NULL, 0);
        if (needed <= 0) {
            snprintf(msg, sizeof(msg), "%s: invalid UTF-8 in env", func_name);
            fe_error(ctx, msg);
            return NULL;
        }
        total_wchars += (size_t)needed;
    }

    block = (wchar_t*)builtin_alloc(ctx, total_wchars * sizeof(wchar_t));
    if (!block) {
        snprintf(msg, sizeof(msg),"%s: out of memory", func_name);
        fe_error(ctx, msg);
        return NULL;
    }

    for (i = 0; i < env->count; i++) {
        int needed;

        abort_error = builtin_poll_abort(ctx, &poll_countdown);
        if (abort_error != NULL) {
            builtin_free(ctx, block);
            fe_error(ctx, abort_error);
            return NULL;
        }

        needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     env->items[i], -1, block + offset,
                                     (int)(total_wchars - offset));
        if (needed <= 0) {
            builtin_free(ctx, block);
            snprintf(msg, sizeof(msg), "%s: invalid UTF-8 in env", func_name);
            fe_error(ctx, msg);
            return NULL;
        }
        offset += (size_t)needed;
    }
    block[offset] = L'\0';
    return block;
}
#else
extern char **environ;
#endif

static int run_process_native(fe_Context *ctx, CStringArray *argv,
                              ProcessOptions *options, ProcessOutput *output,
                              const char *func_name,
                              const char **out_error) {
    ProcessCapturePipe stdin_pipe;
    TempRedirectFile stdout_redirect;
    TempRedirectFile stderr_redirect;
    ProcessCapturePipe stdout_pipe;
    ProcessCapturePipe stderr_pipe;
    TextBuffer stdout_buf;
    TextBuffer stderr_buf;
    int stdout_overflow = 0;
    int stderr_overflow = 0;
    int ok = 0;
    const char *abort_error = NULL;
    const char *deferred_error = NULL;
#ifdef _WIN32
    HANDLE stdin_thread = NULL;
    HANDLE child_process = NULL;
    ProcessInputWriter stdin_writer;
#else
    pid_t child_pid = -1;
    size_t stdin_offset = 0;
#endif

    init_process_capture_pipe(&stdin_pipe);
    init_temp_redirect_file(&stdout_redirect);
    init_temp_redirect_file(&stderr_redirect);
    init_process_capture_pipe(&stdout_pipe);
    init_process_capture_pipe(&stderr_pipe);
    stdout_buf.ctx = NULL;
    stdout_buf.data = NULL;
    stdout_buf.len = 0;
    stdout_buf.cap = 0;
    stderr_buf.ctx = NULL;
    stderr_buf.data = NULL;
    stderr_buf.len = 0;
    stderr_buf.cap = 0;
    memset(output, 0, sizeof(*output));
    if (out_error != NULL) {
        *out_error = NULL;
    }
    output->stdout_captured = (options->stdout_mode == PROCESS_STREAM_CAPTURE);
    output->stderr_captured = (options->stderr_mode == PROCESS_STREAM_CAPTURE);
#ifdef _WIN32
    memset(&stdin_writer, 0, sizeof(stdin_writer));
    stdin_writer.write_handle = INVALID_HANDLE_VALUE;
#endif

    if (!create_process_input_pipe(ctx, &stdin_pipe, func_name)) {
        goto cleanup;
    }

    if (options->stdout_mode == PROCESS_STREAM_CAPTURE) {
        if (!create_process_capture_pipe(ctx, &stdout_pipe, func_name)) {
            goto cleanup;
        }
    } else if (options->stdout_mode == PROCESS_STREAM_DISCARD) {
        if (!open_null_redirect_file(ctx, &stdout_redirect, 1, func_name)) {
            goto cleanup;
        }
    }

    if (options->stderr_mode == PROCESS_STREAM_CAPTURE) {
        if (!create_process_capture_pipe(ctx, &stderr_pipe, func_name)) {
            goto cleanup;
        }
    } else if (options->stderr_mode == PROCESS_STREAM_DISCARD) {
        if (!open_null_redirect_file(ctx, &stderr_redirect, 1, func_name)) {
            goto cleanup;
        }
    }

#ifdef _WIN32
    {
        STARTUPINFOW startup_info;
        PROCESS_INFORMATION process_info;
        char *command_line = NULL;
        wchar_t *command_line_wide = NULL;
        wchar_t *environment = NULL;
        wchar_t *cwd_wide = NULL;
        DWORD creation_flags = 0;
        DWORD wait_result;
        DWORD raw_exit_code = 0;

        ZeroMemory(&startup_info, sizeof(startup_info));
        ZeroMemory(&process_info, sizeof(process_info));

        command_line = build_windows_command_line(ctx, argv, func_name);
        if (!command_line) {
            goto cleanup;
        }
        command_line_wide = utf8_to_wide_alloc(ctx, command_line, func_name);
        builtin_free(ctx, command_line);
        command_line = NULL;
        if (!command_line_wide) {
            goto cleanup;
        }
        if (options->use_env) {
            environment = build_windows_environment_block(ctx, &options->env, func_name);
            if (!environment) {
                builtin_free(ctx, command_line_wide);
                goto cleanup;
            }
            creation_flags |= CREATE_UNICODE_ENVIRONMENT;
        }
        if (options->cwd) {
            cwd_wide = utf8_to_wide_alloc(ctx, options->cwd, func_name);
            if (!cwd_wide) {
                builtin_free(ctx, command_line_wide);
                builtin_free(ctx, environment);
                goto cleanup;
            }
        }

        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESTDHANDLES;
        startup_info.hStdInput = stdin_pipe.read_handle;
        if (options->stdout_mode == PROCESS_STREAM_INHERIT) {
            stdout_redirect.handle = duplicate_inheritable_handle(GetStdHandle(STD_OUTPUT_HANDLE));
            if (stdout_redirect.handle == INVALID_HANDLE_VALUE) {
                builtin_free(ctx, command_line_wide);
                builtin_free(ctx, environment);
                builtin_free(ctx, cwd_wide);
                fe_error(ctx, "runprocess: could not inherit stdout");
                goto cleanup;
            }
            startup_info.hStdOutput = stdout_redirect.handle;
        } else if (options->stdout_mode == PROCESS_STREAM_CAPTURE) {
            startup_info.hStdOutput = stdout_pipe.write_handle;
        } else {
            startup_info.hStdOutput = stdout_redirect.handle;
        }
        if (options->stderr_mode == PROCESS_STREAM_INHERIT) {
            stderr_redirect.handle = duplicate_inheritable_handle(GetStdHandle(STD_ERROR_HANDLE));
            if (stderr_redirect.handle == INVALID_HANDLE_VALUE) {
                builtin_free(ctx, command_line_wide);
                builtin_free(ctx, environment);
                builtin_free(ctx, cwd_wide);
                fe_error(ctx, "runprocess: could not inherit stderr");
                goto cleanup;
            }
            startup_info.hStdError = stderr_redirect.handle;
        } else if (options->stderr_mode == PROCESS_STREAM_CAPTURE) {
            startup_info.hStdError = stderr_pipe.write_handle;
        } else {
            startup_info.hStdError = stderr_redirect.handle;
        }

        if (!CreateProcessW(NULL, command_line_wide, NULL, NULL, TRUE, creation_flags,
                            environment, cwd_wide, &startup_info, &process_info)) {
            builtin_free(ctx, command_line_wide);
            builtin_free(ctx, environment);
            builtin_free(ctx, cwd_wide);
            fe_error(ctx, "runprocess: could not start process");
            goto cleanup;
        }

        builtin_free(ctx, command_line_wide);
        builtin_free(ctx, environment);
        builtin_free(ctx, cwd_wide);
        CloseHandle(process_info.hThread);
        child_process = process_info.hProcess;
        close_process_capture_pipe_read(&stdin_pipe);
        close_temp_redirect_file(&stdout_redirect);
        close_temp_redirect_file(&stderr_redirect);
        close_process_capture_pipe_write(&stdout_pipe);
        close_process_capture_pipe_write(&stderr_pipe);

        if (options->stdin_len > 0) {
            stdin_writer.write_handle = stdin_pipe.write_handle;
            stdin_writer.data = options->stdin_data ? options->stdin_data : (const unsigned char*)"";
            stdin_writer.len = options->stdin_len;
            stdin_thread = CreateThread(NULL, 0, process_input_writer_thread,
                                        &stdin_writer, 0, NULL);
            if (stdin_thread == NULL) {
                fe_error(ctx, "runprocess: could not start stdin writer");
                goto cleanup;
            }
            stdin_pipe.write_handle = INVALID_HANDLE_VALUE;
        } else {
            close_process_capture_pipe_write(&stdin_pipe);
        }

        if (options->stdout_mode != PROCESS_STREAM_CAPTURE &&
            options->stderr_mode != PROCESS_STREAM_CAPTURE) {
            for (;;) {
                abort_error = fe_poll_abort(ctx);
                if (abort_error != NULL) {
                    goto cleanup;
                }
                wait_result = WaitForSingleObject(child_process, 10);
                if (wait_result == WAIT_OBJECT_0) {
                    break;
                }
                if (wait_result != WAIT_TIMEOUT) {
                    fe_error(ctx, "runprocess: could not wait for process");
                    goto cleanup;
                }
            }
            if (!GetExitCodeProcess(child_process, &raw_exit_code)) {
                fe_error(ctx, "runprocess: could not wait for process");
                goto cleanup;
            }
            CloseHandle(child_process);
            child_process = NULL;
            output->exit_code = (int)raw_exit_code;
        } else {
            int process_finished = 0;
            int stdout_open = (options->stdout_mode == PROCESS_STREAM_CAPTURE);
            int stderr_open = (options->stderr_mode == PROCESS_STREAM_CAPTURE);

            while (!process_finished || stdout_open || stderr_open) {
                int made_progress = 0;

                abort_error = fe_poll_abort(ctx);
                if (abort_error != NULL) {
                    goto cleanup;
                }
                if (stdout_open &&
                    !drain_windows_capture_pipe(ctx, &stdout_pipe, &stdout_buf,
                                                options->max_stdout, &stdout_overflow,
                                                func_name, process_finished,
                                                &stdout_open, &made_progress)) {
                    goto cleanup;
                }
                if (stderr_open &&
                    !drain_windows_capture_pipe(ctx, &stderr_pipe, &stderr_buf,
                                                options->max_stderr, &stderr_overflow,
                                                func_name, process_finished,
                                                &stderr_open, &made_progress)) {
                    goto cleanup;
                }

                if (!process_finished) {
                    wait_result = WaitForSingleObject(child_process,
                                                     made_progress ? 0 : 10);
                    if (wait_result == WAIT_OBJECT_0) {
                        if (!GetExitCodeProcess(child_process, &raw_exit_code)) {
                            fe_error(ctx, "runprocess: could not wait for process");
                            goto cleanup;
                        }
                        output->exit_code = (int)raw_exit_code;
                        CloseHandle(child_process);
                        child_process = NULL;
                        process_finished = 1;
                    } else if (wait_result != WAIT_TIMEOUT) {
                        fe_error(ctx, "runprocess: could not wait for process");
                        goto cleanup;
                    }
                }
            }
        }

        if (stdin_thread != NULL) {
            for (;;) {
                abort_error = fe_poll_abort(ctx);
                if (abort_error != NULL) {
                    goto cleanup;
                }
                wait_result = WaitForSingleObject(stdin_thread, 10);
                if (wait_result == WAIT_OBJECT_0) {
                    break;
                }
                if (wait_result != WAIT_TIMEOUT) {
                    fe_error(ctx, "runprocess: could not write stdin");
                    goto cleanup;
                }
            }
            CloseHandle(stdin_thread);
            stdin_thread = NULL;
            if (stdin_writer.failed) {
                fe_error(ctx, "runprocess: could not write stdin");
                goto cleanup;
            }
        }
    }
#else
    {
        pid_t pid;
        int status = 0;
        pid_t waited;

        pid = fork();
        if (pid < 0) {
            fe_error(ctx, "runprocess: could not start process");
            goto cleanup;
        }

        if (pid == 0) {
            if (options->cwd && chdir(options->cwd) != 0) {
                _exit(127);
            }
            if (dup2(stdin_pipe.read_fd, 0) < 0) {
                _exit(127);
            }

            close_process_capture_pipe_read(&stdin_pipe);
            close_process_capture_pipe_write(&stdin_pipe);
            if (options->stdout_mode == PROCESS_STREAM_CAPTURE) {
                close_process_capture_pipe_read(&stdout_pipe);
                if (dup2(stdout_pipe.write_fd, 1) < 0) {
                    _exit(127);
                }
                close_process_capture_pipe_write(&stdout_pipe);
            } else if (options->stdout_mode == PROCESS_STREAM_DISCARD) {
                if (dup2(stdout_redirect.fd, 1) < 0) {
                    _exit(127);
                }
                close_temp_redirect_file(&stdout_redirect);
            }
            if (options->stderr_mode == PROCESS_STREAM_CAPTURE) {
                close_process_capture_pipe_read(&stderr_pipe);
                if (dup2(stderr_pipe.write_fd, 2) < 0) {
                    _exit(127);
                }
                close_process_capture_pipe_write(&stderr_pipe);
            } else if (options->stderr_mode == PROCESS_STREAM_DISCARD) {
                if (dup2(stderr_redirect.fd, 2) < 0) {
                    _exit(127);
                }
                close_temp_redirect_file(&stderr_redirect);
            }

            if (options->use_env) {
                environ = options->env.items;
            }
            execvp(argv->items[0], argv->items);
            _exit(127);
        }

        child_pid = pid;
        close_process_capture_pipe_read(&stdin_pipe);
        close_temp_redirect_file(&stdout_redirect);
        close_temp_redirect_file(&stderr_redirect);
        close_process_capture_pipe_write(&stdout_pipe);
        close_process_capture_pipe_write(&stderr_pipe);

        if (options->stdout_mode != PROCESS_STREAM_CAPTURE &&
            options->stderr_mode != PROCESS_STREAM_CAPTURE &&
            options->stdin_len == 0) {
            struct timeval sleep_timeout;

            close_process_capture_pipe_write(&stdin_pipe);
            for (;;) {
                abort_error = fe_poll_abort(ctx);
                if (abort_error != NULL) {
                    goto cleanup;
                }
                waited = waitpid(child_pid, &status, WNOHANG);
                if (waited == child_pid) {
                    output->exit_code = decode_process_exit_code(status);
                    child_pid = -1;
                    break;
                }
                if (waited < 0) {
                    fe_error(ctx, "runprocess: could not wait for process");
                    goto cleanup;
                }
                sleep_timeout.tv_sec = 0;
                sleep_timeout.tv_usec = 10000;
                select(0, NULL, NULL, NULL, &sleep_timeout);
            }
        } else {
            int process_finished = 0;
            int stdin_open = 1;
            int stdout_open = (options->stdout_mode == PROCESS_STREAM_CAPTURE);
            int stderr_open = (options->stderr_mode == PROCESS_STREAM_CAPTURE);

            if (options->stdin_len == 0) {
                close_process_capture_pipe_write(&stdin_pipe);
                stdin_open = 0;
            }

            while (!process_finished || stdin_open || stdout_open || stderr_open) {
                int made_progress = 0;

                abort_error = fe_poll_abort(ctx);
                if (abort_error != NULL) {
                    goto cleanup;
                }
                if (!process_finished) {
                    waited = waitpid(child_pid, &status, WNOHANG);
                    if (waited == child_pid) {
                        output->exit_code = decode_process_exit_code(status);
                        child_pid = -1;
                        process_finished = 1;
                    } else if (waited < 0) {
                        fe_error(ctx, "runprocess: could not wait for process");
                        goto cleanup;
                    }
                }

                if (stdin_open || stdout_open || stderr_open) {
                    fd_set readfds;
                    fd_set writefds;
                    struct timeval timeout;
                    int maxfd = -1;
                    int select_result;

                    FD_ZERO(&readfds);
                    FD_ZERO(&writefds);
                    if (stdin_open) {
                        FD_SET(stdin_pipe.write_fd, &writefds);
                        if (stdin_pipe.write_fd > maxfd) {
                            maxfd = stdin_pipe.write_fd;
                        }
                    }
                    if (stdout_open) {
                        FD_SET(stdout_pipe.read_fd, &readfds);
                        if (stdout_pipe.read_fd > maxfd) {
                            maxfd = stdout_pipe.read_fd;
                        }
                    }
                    if (stderr_open) {
                        FD_SET(stderr_pipe.read_fd, &readfds);
                        if (stderr_pipe.read_fd > maxfd) {
                            maxfd = stderr_pipe.read_fd;
                        }
                    }

                    timeout.tv_sec = 0;
                    timeout.tv_usec = process_finished ? 0 : 10000;
                    select_result = select(maxfd + 1, &readfds, &writefds, NULL, &timeout);
                    if (select_result < 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        fe_error(ctx, "runprocess: could not poll process pipes");
                        goto cleanup;
                    }

                    if (stdin_open &&
                        FD_ISSET(stdin_pipe.write_fd, &writefds) &&
                        !write_posix_input_pipe(ctx, &stdin_pipe,
                                                options->stdin_data ? options->stdin_data : (const unsigned char*)"",
                                                options->stdin_len, &stdin_offset,
                                                func_name, &stdin_open, &made_progress)) {
                        goto cleanup;
                    }
                    if (stdout_open &&
                        (process_finished || FD_ISSET(stdout_pipe.read_fd, &readfds)) &&
                        !drain_posix_capture_pipe(ctx, &stdout_pipe, &stdout_buf,
                                                  options->max_stdout, &stdout_overflow,
                                                  func_name, &stdout_open,
                                                  &made_progress)) {
                        goto cleanup;
                    }
                    if (stderr_open &&
                        (process_finished || FD_ISSET(stderr_pipe.read_fd, &readfds)) &&
                        !drain_posix_capture_pipe(ctx, &stderr_pipe, &stderr_buf,
                                                  options->max_stderr, &stderr_overflow,
                                                  func_name, &stderr_open,
                                                  &made_progress)) {
                        goto cleanup;
                    }
                } else if (!process_finished) {
                    struct timeval sleep_timeout;

                    sleep_timeout.tv_sec = 0;
                    sleep_timeout.tv_usec = 10000;
                    select(0, NULL, NULL, NULL, &sleep_timeout);
                }
            }
        }
    }
#endif

    if (stdout_overflow) {
        deferred_error = "runprocess stdout: file too large";
        goto cleanup;
    }
    if (stderr_overflow) {
        deferred_error = "runprocess stderr: file too large";
        goto cleanup;
    }

    output->stdout_data = (unsigned char*)stdout_buf.data;
    output->stdout_len = stdout_buf.len;
    stdout_buf.data = NULL;
    stdout_buf.len = 0;
    stdout_buf.cap = 0;
    output->stderr_data = (unsigned char*)stderr_buf.data;
    output->stderr_len = stderr_buf.len;
    stderr_buf.data = NULL;
    stderr_buf.len = 0;
    stderr_buf.cap = 0;

    ok = 1;

cleanup:
#ifdef _WIN32
    if (child_process != NULL) {
        terminate_windows_process(&child_process);
    }
    if (stdin_thread != NULL) {
        WaitForSingleObject(stdin_thread, INFINITE);
        CloseHandle(stdin_thread);
        stdin_thread = NULL;
    }
#else
    if (child_pid > 0) {
        terminate_posix_process(&child_pid);
    }
#endif
    destroy_process_capture_pipe(&stdin_pipe);
    destroy_temp_redirect_file(&stdout_redirect);
    destroy_temp_redirect_file(&stderr_redirect);
    destroy_process_capture_pipe(&stdout_pipe);
    destroy_process_capture_pipe(&stderr_pipe);
    buf_free(&stdout_buf);
    buf_free(&stderr_buf);
    if (!ok) {
        free_process_output(ctx, output);
        if (out_error != NULL) {
            *out_error = (deferred_error != NULL) ? deferred_error : abort_error;
            return 0;
        }
        if (deferred_error != NULL) {
            fe_error(ctx, deferred_error);
        }
        if (abort_error != NULL) {
            fe_error(ctx, abort_error);
        }
    }
    return ok;
}

static fe_Object* build_process_result(fe_Context *ctx,
                                       const ProcessOutput *output) {
    fe_Object *result = fe_map(ctx);

    fe_map_set(ctx, result, fe_symbol(ctx, "code"),
               fe_make_number(ctx, (fe_Number)output->exit_code));
    fe_map_set(ctx, result, fe_symbol(ctx, "ok"),
               fe_bool(ctx, output->exit_code == 0));
    if (output->stdout_captured) {
        const unsigned char *stdout_data = output->stdout_data
            ? output->stdout_data
            : (const unsigned char*)"";
        fe_map_set(ctx, result, fe_symbol(ctx, "stdout"),
                   fe_bytes(ctx, stdout_data,
                            output->stdout_len));
    } else {
        fe_map_set(ctx, result, fe_symbol(ctx, "stdout"),
                   fe_nil(ctx));
    }
    if (output->stderr_captured) {
        const unsigned char *stderr_data = output->stderr_data
            ? output->stderr_data
            : (const unsigned char*)"";
        fe_map_set(ctx, result, fe_symbol(ctx, "stderr"),
                   fe_bytes(ctx, stderr_data,
                            output->stderr_len));
    } else {
        fe_map_set(ctx, result, fe_symbol(ctx, "stderr"),
                   fe_nil(ctx));
    }
    return result;
}

/*
================================================================================
|                             SYSTEM FUNCTIONS                                |
================================================================================
*/

static fe_Object* builtin_cwd(fe_Context *ctx, fe_Object *args) {
    char *path;
    fe_Object *result;

    FEX_CHECK_NO_ARGS(ctx, args, "cwd");
    path = current_working_directory_cstr(ctx);
    if (!path) {
        fe_error(ctx, "cwd: could not determine current directory");
        return fe_nil(ctx);
    }

    result = fe_string(ctx, path, strlen(path));
    builtin_free(ctx, path);
    return result;
}

static fe_Object* builtin_chdir(fe_Context *ctx, fe_Object *args) {
    fe_Object *path_obj;
    char *path;
    int ok;

    FEX_CHECK_ARGS(ctx, args, 1, "chdir");
    path_obj = fe_nextarg(ctx, &args);
    path = string_to_cstr(ctx, path_obj, "chdir");
    if (!path) {
        return fe_nil(ctx);
    }

#ifdef _WIN32
    {
        wchar_t *wide_path = utf8_to_wide_alloc(ctx, path, "chdir");
        if (!wide_path) {
            builtin_free(ctx, path);
            return fe_nil(ctx);
        }
        ok = SetCurrentDirectoryW(wide_path) != 0;
        builtin_free(ctx, wide_path);
    }
#else
    ok = chdir(path) == 0;
#endif
    builtin_free(ctx, path);

    if (!ok) {
        fe_error(ctx, "chdir: could not change directory");
        return fe_nil(ctx);
    }
    return fe_bool(ctx, 1);
}

static fe_Object* builtin_get_env(fe_Context *ctx, fe_Object *args) {
    fe_Object *name_obj;
    char *name;
    const char *value;
    fe_Object *result;

    FEX_CHECK_ARGS(ctx, args, 1, "getenv");
    name_obj = fe_nextarg(ctx, &args);
    name = string_to_cstr(ctx, name_obj, "getenv");
    if (!name) {
        return fe_nil(ctx);
    }

    value = getenv(name);
    builtin_free(ctx, name);
    if (!value) {
        return fe_nil(ctx);
    }

    result = fe_string(ctx, value, strlen(value));
    return result;
}

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
    fe_Object *command_obj;
    char *command;
    CStringArray argv;
    ProcessOptions options;
    ProcessOutput output;
    fe_Object *result;
    const char *process_error = NULL;

    memset(&argv, 0, sizeof(argv));
    argv.ctx = ctx;
    memset(&options, 0, sizeof(options));
    options.env.ctx = ctx;
    memset(&output, 0, sizeof(output));

    FEX_CHECK_ARGS(ctx, args, 1, "system");
    command_obj = fe_nextarg(ctx, &args);
    command = string_to_cstr(ctx, command_obj, "system");

    if (!command) {
        return fe_nil(ctx);
    }

    if (!build_shell_process_argv(ctx, command, "system", &argv)) {
        builtin_free(ctx, command);
        free_cstring_array(&argv);
        return fe_nil(ctx);
    }
    builtin_free(ctx, command);
    options.stdout_mode = PROCESS_STREAM_INHERIT;
    options.stderr_mode = PROCESS_STREAM_INHERIT;
    if (!run_process_native(ctx, &argv, &options, &output, "system",
                            &process_error)) {
        free_cstring_array(&argv);
        free_process_output(ctx, &output);
        if (process_error != NULL) {
            fe_error(ctx, process_error);
        }
        return fe_nil(ctx);
    }

    result = fe_make_number(ctx, (fe_Number)output.exit_code);
    free_cstring_array(&argv);
    free_process_output(ctx, &output);
    return result;
}

static fe_Object* builtin_run_command(fe_Context *ctx, fe_Object *args) {
    fe_Object *command_obj;
    char *command;
    char *merged_command;
    CStringArray argv;
    ProcessOptions options;
    ProcessOutput output;
    fe_Object *result;
    const char *process_error = NULL;

    memset(&argv, 0, sizeof(argv));
    argv.ctx = ctx;
    memset(&options, 0, sizeof(options));
    options.env.ctx = ctx;
    memset(&output, 0, sizeof(output));

    FEX_CHECK_ARGS(ctx, args, 1, "runcommand");
    command_obj = fe_nextarg(ctx, &args);
    command = string_to_cstr(ctx, command_obj, "runcommand");
    if (!command) {
        return fe_nil(ctx);
    }

    merged_command = build_merged_command(ctx, command, "runcommand");
    builtin_free(ctx, command);
    if (!merged_command) {
        return fe_nil(ctx);
    }

    if (!build_shell_process_argv(ctx, merged_command, "runcommand", &argv)) {
        builtin_free(ctx, merged_command);
        free_cstring_array(&argv);
        return fe_nil(ctx);
    }
    builtin_free(ctx, merged_command);
    options.stdout_mode = PROCESS_STREAM_CAPTURE;
    options.stderr_mode = PROCESS_STREAM_DISCARD;
    options.max_stdout = FEX_COMMAND_OUTPUT_MAX_BYTES;
    if (!run_process_native(ctx, &argv, &options, &output, "runcommand",
                            &process_error)) {
        free_cstring_array(&argv);
        free_process_output(ctx, &output);
        if (process_error != NULL) {
            fe_error(ctx, process_error);
        }
        return fe_nil(ctx);
    }

    result = fe_map(ctx);
    fe_map_set(ctx, result, fe_symbol(ctx, "code"),
               fe_make_number(ctx, (fe_Number)output.exit_code));
    fe_map_set(ctx, result, fe_symbol(ctx, "ok"), fe_bool(ctx, output.exit_code == 0));
    fe_map_set(ctx, result, fe_symbol(ctx, "output"),
               fe_bytes(ctx,
                        output.stdout_data ? output.stdout_data : (const unsigned char*)"",
                        output.stdout_len));
    free_cstring_array(&argv);
    free_process_output(ctx, &output);
    return result;
}

static fe_Object* builtin_run_process(fe_Context *ctx, fe_Object *args) {
    fe_Object *exe_obj;
    fe_Object *argv_obj = fe_nil(ctx);
    fe_Object *opts_obj = fe_nil(ctx);
    CStringArray argv;
    ProcessOptions options;
    ProcessOutput output;
    fe_Object *result;
    const char *process_error = NULL;

    memset(&argv, 0, sizeof(argv));
    argv.ctx = ctx;
    memset(&options, 0, sizeof(options));
    options.env.ctx = ctx;
    memset(&output, 0, sizeof(output));

    FEX_CHECK_ARGS(ctx, args, 1, "runprocess");
    exe_obj = fe_nextarg(ctx, &args);
    if (!fe_isnil(ctx, args)) {
        argv_obj = fe_nextarg(ctx, &args);
    }
    if (!fe_isnil(ctx, args)) {
        opts_obj = fe_nextarg(ctx, &args);
    }
    if (!fe_isnil(ctx, args)) {
        fe_error(ctx, "runprocess: too many arguments");
        return fe_nil(ctx);
    }

    if (!collect_process_argv(ctx, exe_obj, argv_obj, "runprocess", &argv) ||
        !parse_process_options(ctx, opts_obj, "runprocess", &options) ||
        !run_process_native(ctx, &argv, &options, &output, "runprocess",
                            &process_error)) {
        free_cstring_array(&argv);
        free_process_options(ctx, &options);
        free_process_output(ctx, &output);
        if (process_error != NULL) {
            fe_error(ctx, process_error);
        }
        return fe_nil(ctx);
    }

    result = build_process_result(ctx, &output);
    free_cstring_array(&argv);
    free_process_options(ctx, &options);
    free_process_output(ctx, &output);
    return result;
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
        case FE_TBYTES: type_name = "bytes"; break;
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
        size_t len;
        char *buffer = string_to_buffer(ctx, obj, "tonumber", &len);
        char *endptr;
        double value;

        if (!buffer) {
            return fe_nil(ctx);
        }
        if (len > 0 && memchr(buffer, '\0', len) != NULL) {
            builtin_free(ctx, buffer);
            fe_error(ctx, "tonumber: invalid number format");
            return fe_nil(ctx);
        }

        value = strtod(buffer, &endptr);

        if (endptr != buffer + len) {
            builtin_free(ctx, buffer);
            fe_error(ctx, "tonumber: invalid number format");
            return fe_nil(ctx);
        }

        builtin_free(ctx, buffer);
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

static fe_Object* builtin_is_bytes(fe_Context *ctx, fe_Object *args) {
    FEX_CHECK_ARGS(ctx, args, 1, "isbytes");
    return fe_bool(ctx, fe_type(ctx, fe_nextarg(ctx, &args)) == FE_TBYTES);
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
    fe_set(ctx, fe_symbol(ctx, "exists"), fe_cfunc(ctx, builtin_exists));
    fe_set(ctx, fe_symbol(ctx, "listdir"), fe_cfunc(ctx, builtin_list_dir));
    fe_set(ctx, fe_symbol(ctx, "mkdir"), fe_cfunc(ctx, builtin_make_dir));
    fe_set(ctx, fe_symbol(ctx, "mkdirp"), fe_cfunc(ctx, builtin_make_dir_parents));
    fe_set(ctx, fe_symbol(ctx, "readfile"), fe_cfunc(ctx, builtin_read_file));
    fe_set(ctx, fe_symbol(ctx, "readbytes"), fe_cfunc(ctx, builtin_read_bytes));
    fe_set(ctx, fe_symbol(ctx, "writefile"), fe_cfunc(ctx, builtin_write_file));
    fe_set(ctx, fe_symbol(ctx, "writebytes"), fe_cfunc(ctx, builtin_write_bytes));
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
    fe_set(ctx, fe_symbol(ctx, "makebytes"), fe_cfunc(ctx, builtin_make_bytes));
    fe_set(ctx, fe_symbol(ctx, "tobytes"), fe_cfunc(ctx, builtin_to_bytes));
    fe_set(ctx, fe_symbol(ctx, "byteslen"), fe_cfunc(ctx, builtin_bytes_length));
    fe_set(ctx, fe_symbol(ctx, "byteat"), fe_cfunc(ctx, builtin_byte_at));
    fe_set(ctx, fe_symbol(ctx, "byteslice"), fe_cfunc(ctx, builtin_bytes_slice));
    fe_set(ctx, fe_symbol(ctx, "parsejson"), fe_cfunc(ctx, builtin_parse_json));
    fe_set(ctx, fe_symbol(ctx, "tojson"), fe_cfunc(ctx, builtin_to_json));

    fe_restoregc(ctx, gc_save);
}

static void register_system_functions(fe_Context *ctx) {
    int gc_save = fe_savegc(ctx);

    fe_set(ctx, fe_symbol(ctx, "cwd"), fe_cfunc(ctx, builtin_cwd));
    fe_set(ctx, fe_symbol(ctx, "chdir"), fe_cfunc(ctx, builtin_chdir));
    fe_set(ctx, fe_symbol(ctx, "getenv"), fe_cfunc(ctx, builtin_get_env));
    fe_set(ctx, fe_symbol(ctx, "time"), fe_cfunc(ctx, builtin_time));
    fe_set(ctx, fe_symbol(ctx, "exit"), fe_cfunc(ctx, builtin_exit));
    fe_set(ctx, fe_symbol(ctx, "system"), fe_cfunc(ctx, builtin_system));
    fe_set(ctx, fe_symbol(ctx, "runcommand"), fe_cfunc(ctx, builtin_run_command));
    fe_set(ctx, fe_symbol(ctx, "runprocess"), fe_cfunc(ctx, builtin_run_process));

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
    fe_set(ctx, fe_symbol(ctx, "isbytes"), fe_cfunc(ctx, builtin_is_bytes));
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
