#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#include <direct.h>
#define TEST_PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_PATH_SEP '/'
#endif

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>

#include "fe.h"
#include "fex.h"

#define TEST_MEM_SIZE (1024 * 1024)
#ifndef FEX_TEST_PYTHON_EXECUTABLE
#define FEX_TEST_PYTHON_EXECUTABLE "python"
#endif
#ifdef _WIN32
#define TEST_SCRIPTS_DIR "..\\scripts"
#else
#define TEST_SCRIPTS_DIR "../scripts"
#endif

static int fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    return 1;
}

static int fail_status(const char *message, FexStatus status, const FexError *error) {
    char buffer[512];
    const char *detail = "<empty>";

    if (error && error->message[0] != '\0') {
        detail = error->message;
    }

    snprintf(buffer, sizeof(buffer), "%s (status=%d, message=%s)",
        message, (int)status, detail);
    return fail(buffer);
}

static int interrupt_once(fe_Context *ctx, void *udata) {
    int *calls = (int*)udata;
    (void)ctx;
    (*calls)++;
    return *calls >= 1;
}

typedef struct {
    jmp_buf env;
    char message[256];
} ReadTryScope;

typedef struct {
    const char *text;
    size_t offset;
} StringReader;

static ReadTryScope *g_read_try_scope = NULL;

static void read_try_error_handler(fe_Context *ctx, const char *err, fe_Object *cl) {
    (void)ctx;
    (void)cl;
    if (!g_read_try_scope) {
        abort();
    }
    snprintf(g_read_try_scope->message, sizeof(g_read_try_scope->message), "%s", err);
    longjmp(g_read_try_scope->env, 1);
}

static char read_from_string(fe_Context *ctx, void *udata) {
    StringReader *reader = (StringReader*)udata;
    (void)ctx;
    if (!reader->text[reader->offset]) {
        return '\0';
    }
    return reader->text[reader->offset++];
}

static fe_Object* make_number_list(fe_Context *ctx, int count) {
    fe_Object *nil_obj = fe_nil(ctx);
    fe_Object *head = nil_obj;
    fe_Object **tail = &head;
    int gc_save = fe_savegc(ctx);
    int i;

    for (i = 0; i < count; i++) {
        fe_Object *value = fe_make_number(ctx, (fe_Number)i);
        fe_pushgc(ctx, value);
        *tail = fe_cons(ctx, value, nil_obj);
        tail = fe_cdr_ptr(ctx, *tail);
        fe_restoregc(ctx, gc_save);
        if (!fe_isnil(ctx, head)) {
            fe_pushgc(ctx, head);
        }
    }

    fe_restoregc(ctx, gc_save);
    return head;
}

static fe_Object* make_string_key_map(fe_Context *ctx, int count) {
    fe_Object *map = fe_map(ctx);
    int gc_save = fe_savegc(ctx);
    int i;

    fe_pushgc(ctx, map);
    for (i = 0; i < count; i++) {
        char key_buffer[32];
        fe_Object *key;
        fe_Object *value;

        snprintf(key_buffer, sizeof(key_buffer), "key_%05d", i);
        key = fe_string(ctx, key_buffer, strlen(key_buffer));
        fe_pushgc(ctx, key);
        value = fe_make_number(ctx, (fe_Number)i);
        fe_pushgc(ctx, value);
        fe_map_set(ctx, map, key, value);
        fe_restoregc(ctx, gc_save);
        fe_pushgc(ctx, map);
    }

    fe_restoregc(ctx, gc_save);
    return map;
}

static char* make_large_parsejson_source(size_t item_count) {
    const char *prefix = "parsejson(\"[";
    const char *suffix = "]\");\n";
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t body_len;
    size_t total_len;
    char *source;
    size_t offset;
    size_t i;

    if (item_count == 0) {
        item_count = 1;
    }

    body_len = item_count * 2 - 1;
    total_len = prefix_len + body_len + suffix_len;
    source = (char*)malloc(total_len + 1);
    if (!source) {
        return NULL;
    }

    memcpy(source, prefix, prefix_len);
    offset = prefix_len;
    for (i = 0; i < item_count; i++) {
        source[offset++] = '0';
        if (i + 1 < item_count) {
            source[offset++] = ',';
        }
    }
    memcpy(source + offset, suffix, suffix_len);
    offset += suffix_len;
    source[offset] = '\0';
    return source;
}

static char* make_large_read_list_source(size_t item_count) {
    size_t total_len;
    char *source;
    size_t offset;
    size_t i;

    if (item_count == 0) {
        item_count = 1;
    }

    total_len = 2 + item_count * 2;
    source = (char*)malloc(total_len + 1);
    if (!source) {
        return NULL;
    }

    offset = 0;
    source[offset++] = '(';
    for (i = 0; i < item_count; i++) {
        source[offset++] = '0';
        if (i + 1 < item_count) {
            source[offset++] = ' ';
        }
    }
    source[offset++] = ')';
    source[offset] = '\0';
    return source;
}

static char* make_large_path_string(size_t length) {
    char *path = (char*)malloc(length + 1);
    size_t i;

    if (!path) {
        return NULL;
    }

    for (i = 0; i < length; i++) {
        path[i] = ((i + 1) % 17 == 0) ? '/' : 'a';
    }
    path[length] = '\0';
    return path;
}

static char* make_large_split_string(size_t item_count) {
    size_t length;
    char *text;
    size_t offset = 0;
    size_t i;

    if (item_count == 0) {
        item_count = 1;
    }

    length = item_count * 2 - 1;
    text = (char*)malloc(length + 1);
    if (!text) {
        return NULL;
    }

    for (i = 0; i < item_count; i++) {
        text[offset++] = 'a';
        if (i + 1 < item_count) {
            text[offset++] = ',';
        }
    }
    text[offset] = '\0';
    return text;
}

static char* make_large_string_literal_source(size_t literal_len) {
    const char *prefix = "\"";
    const char *suffix = "\";\n";
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    char *source = (char*)malloc(prefix_len + literal_len + suffix_len + 1);
    size_t i;

    if (!source) {
        return NULL;
    }

    memcpy(source, prefix, prefix_len);
    for (i = 0; i < literal_len; i++) {
        source[prefix_len + i] = 'a';
    }
    memcpy(source + prefix_len + literal_len, suffix, suffix_len);
    source[prefix_len + literal_len + suffix_len] = '\0';
    return source;
}

static fe_Object* nested_compile_ok_cfunc(fe_Context *ctx, fe_Object *args) {
    fe_Object *result = NULL;
    FexError error;
    FexStatus status;
    (void)args;

    status = fex_try_do_string(ctx, "41 + 1;", &result, &error);
    if (status != FEX_STATUS_OK) {
        fe_error(ctx, error.message[0] ? error.message : "nested compile failed");
        return fe_nil(ctx);
    }
    return result;
}

static fe_Object* nested_compile_error_cfunc(fe_Context *ctx, fe_Object *args) {
    fe_Object *result = NULL;
    FexError error;
    FexStatus status;
    (void)args;

    status = fex_try_do_string(ctx, "let inner = ;", &result, &error);
    if (status != FEX_STATUS_COMPILE_ERROR ||
        strcmp(error.message, "Expect expression.") != 0) {
        fe_error(ctx, "nested compile error handling failed");
        return fe_nil(ctx);
    }
    return fe_make_number(ctx, 7);
}

static int write_large_test_file(const char *path, size_t size, unsigned char fill_byte) {
    FILE *file;
    unsigned char chunk[4096];
    size_t remaining = size;

    memset(chunk, fill_byte, sizeof(chunk));
    remove(path);
    file = fopen(path, "wb");
    if (!file) {
        return 0;
    }

    while (remaining > 0) {
        size_t chunk_size = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        if (fwrite(chunk, 1, chunk_size, file) != chunk_size) {
            fclose(file);
            remove(path);
            return 0;
        }
        remaining -= chunk_size;
    }

    fclose(file);
    return 1;
}

static int make_test_directory(const char *path) {
    remove(path);
#ifdef _WIN32
    _rmdir(path);
    return _mkdir(path) == 0;
#else
    rmdir(path);
    return mkdir(path, 0700) == 0;
#endif
}

static void remove_test_directory(const char *path) {
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

static int create_test_directory_files(const char *dir_path, int count) {
    int i;

    for (i = 0; i < count; i++) {
        char file_path[256];
        FILE *file;

        snprintf(file_path, sizeof(file_path), "%s%cfile_%04d.tmp",
            dir_path, TEST_PATH_SEP, i);
        file = fopen(file_path, "wb");
        if (!file) {
            return 0;
        }
        fclose(file);
    }

    return 1;
}

static void cleanup_test_directory_files(const char *dir_path, int count) {
    int i;

    for (i = 0; i < count; i++) {
        char file_path[256];
        snprintf(file_path, sizeof(file_path), "%s%cfile_%04d.tmp",
            dir_path, TEST_PATH_SEP, i);
        remove(file_path);
    }
    remove_test_directory(dir_path);
}

int main(void) {
    void *memory;
    void *plain_memory;
    fe_Context *ctx;
    fe_Context *plain_ctx;
    fe_Context *span_ctx;
    fe_Object *map;
    fe_Object *name_key;
    fe_Object *name_value;
    fe_Object *output;
    fe_Object *result;
    fe_Stats stats;
    FexError error;
    FexStatus status;
    char buffer[64];
    const char *budget_loop_source = "while (true) { }\n";
    void *span_memory;

    memory = malloc(TEST_MEM_SIZE);
    if (!memory) {
        return fail("failed to allocate memory");
    }

    ctx = fe_open(memory, TEST_MEM_SIZE);
    if (!ctx) {
        free(memory);
        return fail("failed to open interpreter");
    }

    fex_init_with_config(ctx, FEX_CONFIG_ENABLE_SPANS);
    fe_set(ctx, fe_symbol(ctx, "nested_compile_ok"), fe_cfunc(ctx, nested_compile_ok_cfunc));
    fe_set(ctx, fe_symbol(ctx, "nested_compile_error"), fe_cfunc(ctx, nested_compile_error_cfunc));

    plain_memory = malloc(TEST_MEM_SIZE);
    if (!plain_memory) {
        fe_close(ctx);
        free(memory);
        return fail("failed to allocate lazy span test memory");
    }
    plain_ctx = fe_open(plain_memory, TEST_MEM_SIZE);
    if (!plain_ctx) {
        fe_close(ctx);
        free(memory);
        free(plain_memory);
        return fail("failed to open lazy span test context");
    }
    if (fe_get_memory_used(plain_ctx) != TEST_MEM_SIZE) {
        fe_close(plain_ctx);
        fe_close(ctx);
        free(memory);
        free(plain_memory);
        return fail("expected spans to stay disabled without extra tracked allocations");
    }
    fex_init(plain_ctx);
    if (fe_get_memory_used(plain_ctx) != TEST_MEM_SIZE) {
        fe_close(plain_ctx);
        fe_close(ctx);
        free(memory);
        free(plain_memory);
        return fail("expected default initialization to keep spans lazily disabled");
    }
    fex_init_with_config(plain_ctx, FEX_CONFIG_ENABLE_SPANS);
    if (fe_get_memory_used(plain_ctx) <= TEST_MEM_SIZE) {
        fe_close(plain_ctx);
        fe_close(ctx);
        free(memory);
        free(plain_memory);
        return fail("expected enabling spans to allocate span state lazily");
    }
    fe_close(plain_ctx);
    free(plain_memory);

    {
        int gc_before = fe_savegc(ctx);
        status = fex_try_do_string(ctx, "40 + 2;", &result, &error);
        if (status != FEX_STATUS_OK) {
            fe_close(ctx);
            free(memory);
            return fail("expected successful evaluation");
        }
        if (fe_tonumber(ctx, result) != 42) {
            fe_close(ctx);
            free(memory);
            return fail("expected 40 + 2 to equal 42");
        }
        if (fe_savegc(ctx) != gc_before) {
            fe_close(ctx);
            free(memory);
            return fail("expected successful fex_try_do_string to restore the GC stack");
        }
    }

    {
        int gc_before = fe_savegc(ctx);
        fe_Object *compiled = NULL;

        status = fex_try_compile(ctx, "40 + 2;", "try-compile-root", &compiled, &error);
        if (status != FEX_STATUS_OK) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected successful fex_try_compile", status, &error);
        }
        if (fe_savegc(ctx) != gc_before + 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected successful fex_try_compile to keep the returned AST rooted");
        }
        status = fex_try_eval(ctx, compiled, &result, &error);
        if (status != FEX_STATUS_OK || fe_tonumber(ctx, result) != 42) {
            fe_restoregc(ctx, gc_before);
            fe_close(ctx);
            free(memory);
            return fail_status("expected rooted fex_try_compile result to evaluate successfully", status, &error);
        }
        if (fe_savegc(ctx) != gc_before + 1) {
            fe_restoregc(ctx, gc_before);
            fe_close(ctx);
            free(memory);
            return fail("expected fex_try_eval to preserve the caller-owned compile root");
        }
        fe_restoregc(ctx, gc_before);
    }

    {
        void *plain_try_memory = malloc(TEST_MEM_SIZE);
        fe_Context *plain_try_ctx;

        if (!plain_try_memory) {
            fe_close(ctx);
            free(memory);
            return fail("failed to allocate non-span runtime diagnostic test memory");
        }
        plain_try_ctx = fe_open(plain_try_memory, TEST_MEM_SIZE);
        if (!plain_try_ctx) {
            fe_close(ctx);
            free(memory);
            free(plain_try_memory);
            return fail("failed to open non-span runtime diagnostic test context");
        }
        fex_init(plain_try_ctx);
        status = fex_try_do_string_named(plain_try_ctx, "1 / 0;", "<expr>", &result, &error);
        if (status != FEX_STATUS_RUNTIME_ERROR) {
            fe_close(plain_try_ctx);
            fe_close(ctx);
            free(memory);
            free(plain_try_memory);
            return fail_status("expected named runtime diagnostics without spans", status, &error);
        }
        if (strcmp(error.source_name, "<expr>") != 0) {
            fe_close(plain_try_ctx);
            fe_close(ctx);
            free(memory);
            free(plain_try_memory);
            return fail("expected fex_try_do_string_named to preserve the caller source name without spans");
        }
        fe_close(plain_try_ctx);
        free(plain_try_memory);
    }

    map = fe_map(ctx);
    name_key = fe_symbol(ctx, "name");
    name_value = fe_string(ctx, "fex", 3);
    if (!fe_map_set(ctx, map, name_key, name_value)) {
        fe_close(ctx);
        free(memory);
        return fail("expected fe_map_set to succeed");
    }
    if (!fe_map_has(ctx, map, fe_string(ctx, "name", 4))) {
        fe_close(ctx);
        free(memory);
        return fail("expected fe_map_has to find a stored key");
    }
    result = fe_map_get(ctx, map, fe_symbol(ctx, "name"));
    if (fe_strlen(ctx, result) != 3) {
        fe_close(ctx);
        free(memory);
        return fail("expected fe_map_get to return the stored value");
    }

    status = fex_try_do_string(ctx, "let x = ;", &result, &error);
    if (status != FEX_STATUS_COMPILE_ERROR) {
        fe_close(ctx);
        free(memory);
        return fail("expected compile error");
    }
    if (strcmp(error.message, "Expect expression.") != 0 || error.line != 1) {
        fe_close(ctx);
        free(memory);
        return fail("compile error details were not captured");
    }

    status = fex_try_do_string(ctx, "nested_compile_ok();", &result, &error);
    if (status != FEX_STATUS_OK || fe_tonumber(ctx, result) != 42) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected nested same-thread compile to succeed", status, &error);
    }

    status = fex_try_do_string(ctx, "nested_compile_error();", &result, &error);
    if (status != FEX_STATUS_OK || fe_tonumber(ctx, result) != 7) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected nested same-thread compile errors to unwind cleanly", status, &error);
    }

    status = fex_try_do_string(ctx, "let p = 1 :: 2 :: nil;\np.foo;\n", &result, &error);
    if (status != FEX_STATUS_RUNTIME_ERROR) {
        fe_close(ctx);
        free(memory);
        return fail("expected runtime error");
    }
    if (strstr(error.message, "Only .head") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail("runtime error message was not captured");
    }
    if (error.frame_count < 1 || error.frames[0].line < 1) {
        fe_close(ctx);
        free(memory);
        return fail("runtime traceback was not captured");
    }

    status = fex_try_do_file(ctx, "missing-file-does-not-exist.fex", &result, &error);
    if (status != FEX_STATUS_IO_ERROR) {
        fe_close(ctx);
        free(memory);
        return fail("expected file I/O error");
    }

    if (fe_get_memory_used(ctx) < TEST_MEM_SIZE) {
        fe_close(ctx);
        free(memory);
        return fail("expected tracked memory usage to include the base arena");
    }
    if (fe_get_peak_memory_used(ctx) < fe_get_memory_used(ctx)) {
        fe_close(ctx);
        free(memory);
        return fail("expected peak memory usage to be at least current usage");
    }
    memset(&stats, 0, sizeof(stats));
    fe_get_stats(ctx, &stats);
    if (stats.base_memory_bytes != TEST_MEM_SIZE) {
        fe_close(ctx);
        free(memory);
        return fail("expected stats to report the configured base arena size");
    }
    if (stats.memory_used != fe_get_memory_used(ctx) ||
        stats.peak_memory_used != fe_get_peak_memory_used(ctx)) {
        fe_close(ctx);
        free(memory);
        return fail("expected stats memory fields to match the dedicated getters");
    }
    if (stats.object_capacity == 0 || stats.object_allocations_total == 0) {
        fe_close(ctx);
        free(memory);
        return fail("expected stats to expose object-capacity and allocation counters");
    }

    {
        static const unsigned char raw_bytes[3] = {0x41, 0x00, 0xff};
        unsigned char copied_bytes[3];
        fe_Object *bytes = fe_bytes(ctx, raw_bytes, sizeof(raw_bytes));
        if (fe_byteslen(ctx, bytes) != sizeof(raw_bytes)) {
            fe_close(ctx);
            free(memory);
            return fail("expected fe_byteslen to report the stored byte length");
        }
        memset(copied_bytes, 0, sizeof(copied_bytes));
        if (fe_bytescopy(ctx, bytes, 0, copied_bytes, sizeof(copied_bytes)) != sizeof(copied_bytes)) {
            fe_close(ctx);
            free(memory);
            return fail("expected fe_bytescopy to copy the requested range");
        }
        if (copied_bytes[0] != raw_bytes[0] || copied_bytes[1] != raw_bytes[1] || copied_bytes[2] != raw_bytes[2]) {
            fe_close(ctx);
            free(memory);
            return fail("expected fe_bytescopy to preserve raw byte values");
        }
    }

    fe_set_step_limit(ctx, 64);
    status = fex_try_do_string(
        ctx,
        budget_loop_source,
        &result,
        &error
    );
    if (status != FEX_STATUS_RUNTIME_ERROR ||
        strstr(error.message, "execution step limit exceeded") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected execution step limit error", status, &error);
    }
    if (fe_get_steps_executed(ctx) <= 64) {
        fe_close(ctx);
        free(memory);
        return fail("expected step counter to advance past the configured limit");
    }
    fe_get_stats(ctx, &stats);
    if (stats.step_limit != 64 || stats.steps_executed != fe_get_steps_executed(ctx)) {
        fe_close(ctx);
        free(memory);
        return fail("expected stats step counters to match the runtime state");
    }
    fe_set_step_limit(ctx, 0);

    fe_set_timeout_ms(ctx, 50);
    status = fex_try_do_string(
        ctx,
        budget_loop_source,
        &result,
        &error
    );
    if (status != FEX_STATUS_RUNTIME_ERROR ||
        strstr(error.message, "execution timeout exceeded") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected execution timeout error", status, &error);
    }
    fe_set_timeout_ms(ctx, 0);

    {
        int interrupt_calls = 0;
        fe_set_timeout_ms(ctx, 100);
        fe_set_interrupt_handler(ctx, interrupt_once, &interrupt_calls, 1);
        status = fex_try_do_string(
            ctx,
            budget_loop_source,
            &result,
            &error
        );
        fe_set_timeout_ms(ctx, 0);
        fe_set_interrupt_handler(ctx, NULL, NULL, 0);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "execution interrupted") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected interrupt handler to stop evaluation", status, &error);
        }
        if (interrupt_calls < 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected interrupt handler to run at least once");
        }
    }

    {
        size_t baseline_used = fe_get_memory_used(ctx);
        fe_set_memory_limit(ctx, baseline_used);
        if (fe_get_memory_limit(ctx) != baseline_used) {
            fe_close(ctx);
            free(memory);
            return fail("expected memory limit getter to report the configured value");
        }
        status = fex_try_do_string(
            ctx,
            "module(\"mem_limit_test\") {\n"
            "  export let x = 1;\n"
            "}\n",
            &result,
            &error
        );
        fe_set_memory_limit(ctx, 0);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "memory limit exceeded") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected memory limit error", status, &error);
        }
        if (fe_get_memory_limit(ctx) != 0) {
            fe_close(ctx);
            free(memory);
            return fail("expected resetting the memory limit to disable it");
        }
        if (fe_get_memory_used(ctx) > baseline_used) {
            fe_close(ctx);
            free(memory);
            return fail("expected tracked memory usage to stay within the configured limit");
        }
    }

    span_memory = malloc(TEST_MEM_SIZE);
    if (!span_memory) {
        fe_close(ctx);
        free(memory);
        return fail("failed to allocate span accounting test memory");
    }
    span_ctx = fe_open(span_memory, TEST_MEM_SIZE);
    if (!span_ctx) {
        fe_close(ctx);
        free(memory);
        free(span_memory);
        return fail("failed to open span accounting test context");
    }
    fex_init_with_config(span_ctx, FEX_CONFIG_ENABLE_SPANS);
    {
        size_t baseline_used = fe_get_memory_used(span_ctx);
        fe_Object *compiled = NULL;
        FexError span_error;
        FexStatus span_status = fex_try_compile(span_ctx, "1 + 2;\n", "span-usage", &compiled, &span_error);

        if (span_status != FEX_STATUS_OK) {
            fe_close(span_ctx);
            fe_close(ctx);
            free(memory);
            free(span_memory);
            return fail_status("expected span-enabled compile to succeed", span_status, &span_error);
        }
        if (fe_get_memory_used(span_ctx) <= baseline_used) {
            fe_close(span_ctx);
            fe_close(ctx);
            free(memory);
            free(span_memory);
            return fail("expected span tracking allocations to contribute to tracked memory usage");
        }
    }
    fe_close(span_ctx);
    free(span_memory);

    span_memory = malloc(TEST_MEM_SIZE);
    if (!span_memory) {
        fe_close(ctx);
        free(memory);
        return fail("failed to allocate span memory limit test memory");
    }
    span_ctx = fe_open(span_memory, TEST_MEM_SIZE);
    if (!span_ctx) {
        fe_close(ctx);
        free(memory);
        free(span_memory);
        return fail("failed to open span memory limit test context");
    }
    fex_init_with_config(span_ctx, FEX_CONFIG_ENABLE_SPANS);
    {
        size_t baseline_used = fe_get_memory_used(span_ctx);
        fe_Object *compiled = NULL;
        FexError span_error;
        FexStatus span_status;

        fe_set_memory_limit(span_ctx, baseline_used + 16);
        span_status = fex_try_compile(span_ctx, "1 + 2;\n", "span-limit", &compiled, &span_error);
        fe_set_memory_limit(span_ctx, 0);
        if (span_status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(span_error.message, "memory limit exceeded") == NULL) {
            fe_close(span_ctx);
            fe_close(ctx);
            free(memory);
            free(span_memory);
            return fail_status("expected span tracking allocations to honor the memory limit", span_status, &span_error);
        }
        if (fe_get_memory_used(span_ctx) != baseline_used) {
            fe_close(span_ctx);
            fe_close(ctx);
            free(memory);
            free(span_memory);
            return fail("expected span tracking allocation failures to clean up tracked memory");
        }
    }
    fe_close(span_ctx);
    free(span_memory);

    fex_init_with_builtins(ctx, FEX_CONFIG_ENABLE_SPANS,
        FEX_BUILTINS_STRING | FEX_BUILTINS_DATA);
    status = fex_try_do_string(
        ctx,
        "let cfg = makemap(\"env\", \"prod\");\n"
        "cfg.host = \"localhost\";\n"
        "mapget(cfg, \"host\");\n",
        &result, &error
    );
    if (status != FEX_STATUS_OK || fe_strlen(ctx, result) != 9) {
        fe_close(ctx);
        free(memory);
        return fail("expected map property assignment to succeed");
    }

    status = fex_try_do_string(
        ctx,
        "let q = substring(tojson(\"x\"), 0, 1);\n"
        "let raw = concat(\"{\", q, \"name\", q, \":\", q, \"fex\", q, \"}\");\n"
        "parsejson(raw).name;\n",
        &result,
        &error
    );
    if (status != FEX_STATUS_OK) {
        fe_close(ctx);
        free(memory);
        return fail("expected selective JSON helpers to succeed");
    }
    fe_tostring(ctx, result, buffer, sizeof(buffer));
    if (strcmp(buffer, "fex") != 0) {
        fe_close(ctx);
        free(memory);
        return fail("unexpected selective JSON helper result");
    }

    {
        char *parsejson_source = make_large_parsejson_source(100);
        int interrupt_calls = 0;

        if (!parsejson_source) {
            fe_close(ctx);
            free(memory);
            return fail("failed to allocate parsejson interrupt source");
        }

        fe_set_interrupt_handler(ctx, interrupt_once, &interrupt_calls, 1);
        status = fex_try_do_string(ctx, parsejson_source, &result, &error);
        free(parsejson_source);
        fe_set_interrupt_handler(ctx, NULL, NULL, 0);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "execution interrupted") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected parsejson to honor interrupt polling", status, &error);
        }
        if (interrupt_calls < 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected parsejson interrupt handler to run");
        }
    }

    {
        void *map_memory = malloc(TEST_MEM_SIZE);
        fe_Context *map_ctx;
        fe_Object *bigmap;
        fe_ErrorFn previous_error;
        ReadTryScope direct_try;
        int interrupt_calls = 0;

        if (!map_memory) {
            fe_close(ctx);
            free(memory);
            return fail("failed to allocate mapkeys interrupt context");
        }

        map_ctx = fe_open(map_memory, TEST_MEM_SIZE);
        if (!map_ctx) {
            free(map_memory);
            fe_close(ctx);
            free(memory);
            return fail("failed to open mapkeys interrupt context");
        }

        fex_init_with_config(map_ctx, FEX_CONFIG_ENABLE_SPANS);
        bigmap = make_string_key_map(map_ctx, 4096);
        previous_error = fe_handlers(map_ctx)->error;
        fe_handlers(map_ctx)->error = read_try_error_handler;
        g_read_try_scope = &direct_try;
        direct_try.message[0] = '\0';
        fe_set_interrupt_handler(map_ctx, interrupt_once, &interrupt_calls, 1);

        if (setjmp(direct_try.env) == 0) {
            (void)fe_map_keys(map_ctx, bigmap);
            g_read_try_scope = NULL;
            fe_handlers(map_ctx)->error = previous_error;
            fe_set_interrupt_handler(map_ctx, NULL, NULL, 0);
            fe_close(map_ctx);
            free(map_memory);
            fe_close(ctx);
            free(memory);
            return fail("expected fe_map_keys to honor interrupt polling");
        }

        g_read_try_scope = NULL;
        fe_handlers(map_ctx)->error = previous_error;
        fe_set_interrupt_handler(map_ctx, NULL, NULL, 0);
        if (strstr(direct_try.message, "execution interrupted") == NULL) {
            fe_close(map_ctx);
            free(map_memory);
            fe_close(ctx);
            free(memory);
            return fail("expected fe_map_keys interrupt message");
        }
        if (interrupt_calls < 1) {
            fe_close(map_ctx);
            free(map_memory);
            fe_close(ctx);
            free(memory);
            return fail("expected fe_map_keys interrupt handler to run");
        }
        fe_close(map_ctx);
        free(map_memory);
    }

    {
        fe_ErrorFn previous_error = fe_handlers(ctx)->error;
        ReadTryScope direct_try;
        int interrupt_calls = 0;

        fe_handlers(ctx)->error = read_try_error_handler;
        g_read_try_scope = &direct_try;
        direct_try.message[0] = '\0';
        fe_set_interrupt_handler(ctx, interrupt_once, &interrupt_calls, 1);

        if (setjmp(direct_try.env) == 0) {
            (void)fe_symbol(ctx, "sym_interrupt_target");
            g_read_try_scope = NULL;
            fe_handlers(ctx)->error = previous_error;
            fe_set_interrupt_handler(ctx, NULL, NULL, 0);
            fe_close(ctx);
            free(memory);
            return fail("expected fe_symbol to honor interrupt polling");
        }

        g_read_try_scope = NULL;
        fe_handlers(ctx)->error = previous_error;
        fe_set_interrupt_handler(ctx, NULL, NULL, 0);
        if (strstr(direct_try.message, "execution interrupted") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail("expected fe_symbol interrupt message");
        }
        if (interrupt_calls < 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected fe_symbol interrupt handler to run");
        }
    }

    {
        char *large_split = make_large_split_string(512);
        fe_Object *bigsplit;
        int gc_save = fe_savegc(ctx);
        int interrupt_calls = 0;

        if (!large_split) {
            fe_close(ctx);
            free(memory);
            return fail("failed to allocate split interrupt source");
        }

        bigsplit = fe_string(ctx, large_split, strlen(large_split));
        free(large_split);
        fe_pushgc(ctx, bigsplit);
        fe_set(ctx, fe_symbol(ctx, "bigsplit"), bigsplit);
        fe_restoregc(ctx, gc_save);

        fe_set_interrupt_handler(ctx, interrupt_once, &interrupt_calls, 1);
        status = fex_try_do_string(ctx, "split(bigsplit, \",\");\n", &result, &error);
        fe_set_interrupt_handler(ctx, NULL, NULL, 0);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "execution interrupted") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected split to honor interrupt polling", status, &error);
        }
        if (interrupt_calls < 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected split interrupt handler to run");
        }
    }

    fex_init_with_builtins(ctx, FEX_CONFIG_ENABLE_SPANS, FEX_BUILTINS_LIST);
    {
        fe_Object *biglist;
        int gc_save = fe_savegc(ctx);
        int interrupt_calls = 0;

        biglist = make_number_list(ctx, 4096);
        fe_pushgc(ctx, biglist);
        fe_set(ctx, fe_symbol(ctx, "biglist"), biglist);
        fe_restoregc(ctx, gc_save);

        fe_set_interrupt_handler(ctx, interrupt_once, &interrupt_calls, 1);
        status = fex_try_do_string(ctx, "length(biglist);\n", &result, &error);
        fe_set_interrupt_handler(ctx, NULL, NULL, 0);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "execution interrupted") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected list length to honor interrupt polling", status, &error);
        }
        if (interrupt_calls < 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected list length interrupt handler to run");
        }
    }

    {
        int interrupt_calls = 0;

        fex_init_with_builtins(ctx, FEX_CONFIG_ENABLE_SPANS, FEX_BUILTINS_TYPE);
        fe_set_interrupt_handler(ctx, interrupt_once, &interrupt_calls, 1);
        status = fex_try_do_string(ctx, "tostring(biglist);\n", &result, &error);
        fe_set_interrupt_handler(ctx, NULL, NULL, 0);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "execution interrupted") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected tostring to honor interrupt polling", status, &error);
        }
        if (interrupt_calls < 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected tostring interrupt handler to run");
        }
    }

    {
        void *read_memory = malloc(TEST_MEM_SIZE);
        fe_Context *read_ctx;
        char *read_source = make_large_read_list_source(4096);
        StringReader reader;
        ReadTryScope read_try;
        fe_ErrorFn previous_error;
        int interrupt_calls = 0;

        if (!read_memory || !read_source) {
            free(read_source);
            free(read_memory);
            fe_close(ctx);
            free(memory);
            return fail("failed to allocate fe_read interrupt fixtures");
        }

        read_ctx = fe_open(read_memory, TEST_MEM_SIZE);
        if (!read_ctx) {
            free(read_source);
            free(read_memory);
            fe_close(ctx);
            free(memory);
            return fail("failed to open fe_read interrupt context");
        }

        fex_init_with_config(read_ctx, FEX_CONFIG_ENABLE_SPANS);
        fe_set_interrupt_handler(read_ctx, interrupt_once, &interrupt_calls, 1);
        reader.text = read_source;
        reader.offset = 0;
        previous_error = fe_handlers(read_ctx)->error;
        fe_handlers(read_ctx)->error = read_try_error_handler;
        g_read_try_scope = &read_try;
        read_try.message[0] = '\0';

        if (setjmp(read_try.env) == 0) {
            (void)fe_read(read_ctx, read_from_string, &reader);
            g_read_try_scope = NULL;
            fe_handlers(read_ctx)->error = previous_error;
            fe_set_interrupt_handler(read_ctx, NULL, NULL, 0);
            fe_close(read_ctx);
            free(read_source);
            free(read_memory);
            fe_close(ctx);
            free(memory);
            return fail("expected fe_read to honor interrupt polling");
        }

        g_read_try_scope = NULL;
        fe_handlers(read_ctx)->error = previous_error;
        fe_set_interrupt_handler(read_ctx, NULL, NULL, 0);
        if (strstr(read_try.message, "execution interrupted") == NULL) {
            fe_close(read_ctx);
            free(read_source);
            free(read_memory);
            fe_close(ctx);
            free(memory);
            return fail("expected fe_read interrupt message");
        }
        if (interrupt_calls < 1) {
            fe_close(read_ctx);
            free(read_source);
            free(read_memory);
            fe_close(ctx);
            free(memory);
            return fail("expected fe_read interrupt handler to run");
        }
        fe_close(read_ctx);
        free(read_source);
        free(read_memory);
    }

    status = fex_try_do_string(ctx, "pathjoin(\"build\", \"fex\");", &result, &error);
    if (status != FEX_STATUS_RUNTIME_ERROR || strstr(error.message, "tried to call non-callable value") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail("expected pathjoin to stay unavailable without I/O builtins");
    }

    fex_init_with_builtins(ctx, FEX_CONFIG_ENABLE_SPANS, FEX_BUILTINS_IO);
    status = fex_try_do_string(ctx, "pathjoin(\"build\", \"fex\");", &result, &error);
    if (status != FEX_STATUS_OK) {
        fe_close(ctx);
        free(memory);
        return fail("expected I/O builtins to be addable later");
    }
    fe_tostring(ctx, result, buffer, sizeof(buffer));
    if (strcmp(buffer, "build/fex") != 0) {
        fe_close(ctx);
        free(memory);
        return fail("unexpected pathjoin result after enabling I/O builtins");
    }

    status = fex_try_do_string(ctx, "writefile(\"bad\\0path.txt\", \"x\");", &result, &error);
    if (status != FEX_STATUS_RUNTIME_ERROR ||
        strstr(error.message, "writefile: strings containing NUL bytes are not allowed") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected writefile to reject embedded NUL bytes in path strings", status, &error);
    }
    if (error.frame_count < 1 ||
        strstr(error.frames[0].expression, "bad\\0path.txt") == NULL ||
        strstr(error.frames[0].expression, "writefile") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail("expected structured runtime diagnostics to preserve embedded NUL literals");
    }

    status = fex_try_do_string(
        ctx,
        "tojson(parsejson(\"\\\"a\\\\u0000b\\\"\"));\n",
        &result,
        &error
    );
    if (status != FEX_STATUS_OK) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected tojson to serialize embedded NUL strings as JSON escapes", status, &error);
    }
    {
        char json_buffer[32];
        fe_tostring(ctx, result, json_buffer, sizeof(json_buffer));
        if (strcmp(json_buffer, "\"a\\u0000b\"") != 0) {
            fe_close(ctx);
            free(memory);
            return fail("unexpected JSON serialization for embedded NUL string");
        }
    }

    status = fex_try_do_string(ctx, "tonumber(\"42\\0junk\");", &result, &error);
    if (status != FEX_STATUS_RUNTIME_ERROR ||
        strstr(error.message, "tonumber: invalid number format") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected tonumber to reject embedded NUL trailing data", status, &error);
    }
    if (error.frame_count < 1 ||
        strstr(error.frames[0].expression, "42\\0junk") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail("expected tonumber diagnostic to preserve embedded NUL literals");
    }

    status = fex_try_do_string(ctx, "tonumber(\"\\\\0\");", &result, &error);
    if (status != FEX_STATUS_RUNTIME_ERROR ||
        strstr(error.message, "tonumber: invalid number format") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected tonumber to reject literal backslash-zero text", status, &error);
    }
    if (error.frame_count < 1 ||
        strstr(error.frames[0].expression, "\"\\\\0\"") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail("expected diagnostics to distinguish literal backslash sequences from embedded NUL bytes");
    }

    {
        char *heap_source = (char*)malloc(32);
        fe_Object *compiled = NULL;

        if (!heap_source) {
            fe_close(ctx);
            free(memory);
            return fail("failed to allocate transient compile source");
        }
        memcpy(heap_source, "tonumber(\"\\\\0\");\n", 18);
        status = fex_try_compile(ctx, heap_source, "transient-source", &compiled, &error);
        if (status != FEX_STATUS_OK) {
            free(heap_source);
            fe_close(ctx);
            free(memory);
            return fail_status("expected transient-source compile to succeed", status, &error);
        }
        memset(heap_source, 'x', 17);
        heap_source[17] = '\0';
        free(heap_source);

        status = fex_try_eval(ctx, compiled, &result, &error);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "tonumber: invalid number format") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected compiled transient-source diagnostics to stay valid", status, &error);
        }
        if (error.frame_count < 1 ||
            strstr(error.frames[0].expression, "\"\\\\0\"") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail("expected span excerpts to outlive transient compile buffers");
        }
    }

    status = fex_try_do_string(
        ctx,
        "let s = \"a\\\"b\";\n"
        "let bits = split(\"a\\0b,c\", \",\");\n"
        "list(strlen(s), contains(\"a\\0b\", \"\\0b\"), strlen(concat(\"a\\0b\", \"c\")), strlen(car(bits)), strlen(trim(\"\\t\\0 \\n\")));\n",
        &result,
        &error
    );
    if (status != FEX_STATUS_OK) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected escaped quotes and embedded NUL string builtins to work", status, &error);
    }
    if (fe_tonumber(ctx, fe_car(ctx, result)) != 3 ||
        fe_car(ctx, fe_cdr(ctx, result)) != FE_TRUE ||
        fe_tonumber(ctx, fe_car(ctx, fe_cdr(ctx, fe_cdr(ctx, result)))) != 4 ||
        fe_tonumber(ctx, fe_car(ctx, fe_cdr(ctx, fe_cdr(ctx, fe_cdr(ctx, result))))) != 3 ||
        fe_tonumber(ctx, fe_car(ctx, fe_cdr(ctx, fe_cdr(ctx, fe_cdr(ctx, fe_cdr(ctx, result)))))) != 1) {
        fe_close(ctx);
        free(memory);
        return fail("unexpected results from escaped quote / embedded NUL string tests");
    }

    span_memory = malloc(TEST_MEM_SIZE);
    if (!span_memory) {
        fe_close(ctx);
        free(memory);
        return fail("failed to allocate large literal memory limit test memory");
    }
    span_ctx = fe_open(span_memory, TEST_MEM_SIZE);
    if (!span_ctx) {
        fe_close(ctx);
        free(memory);
        free(span_memory);
        return fail("failed to open large literal memory limit test context");
    }
    fex_init_with_config(span_ctx, FEX_CONFIG_ENABLE_SPANS);
    {
        char *large_literal_source = make_large_string_literal_source(4096);
        size_t baseline_used = fe_get_memory_used(span_ctx);
        fe_Object *compiled = NULL;
        FexError span_error;
        FexStatus span_status;

        if (!large_literal_source) {
            fe_close(span_ctx);
            fe_close(ctx);
            free(memory);
            free(span_memory);
            return fail("failed to allocate large literal source");
        }

        fe_set_memory_limit(span_ctx, baseline_used + 512);
        span_status = fex_try_compile(span_ctx, large_literal_source, "literal-limit", &compiled, &span_error);
        fe_set_memory_limit(span_ctx, 0);
        free(large_literal_source);
        if (span_status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(span_error.message, "memory limit exceeded") == NULL) {
            fe_close(span_ctx);
            fe_close(ctx);
            free(memory);
            free(span_memory);
            return fail_status("expected large literal compilation to honor tracked memory limits", span_status, &span_error);
        }
    }
    fe_close(span_ctx);
    free(span_memory);

    span_memory = malloc(TEST_MEM_SIZE);
    if (!span_memory) {
        fe_close(ctx);
        free(memory);
        return fail("failed to allocate direct large literal memory limit test memory");
    }
    span_ctx = fe_open(span_memory, TEST_MEM_SIZE);
    if (!span_ctx) {
        fe_close(ctx);
        free(memory);
        free(span_memory);
        return fail("failed to open direct large literal memory limit test context");
    }
    fex_init_with_config(span_ctx, FEX_CONFIG_ENABLE_SPANS);
    {
        char *large_literal_source = make_large_string_literal_source(4096);
        size_t baseline_used = fe_get_memory_used(span_ctx);
        fe_ErrorFn previous_error = fe_handlers(span_ctx)->error;
        ReadTryScope direct_try;

        if (!large_literal_source) {
            fe_close(span_ctx);
            fe_close(ctx);
            free(memory);
            free(span_memory);
            return fail("failed to allocate direct large literal source");
        }

        fe_handlers(span_ctx)->error = read_try_error_handler;
        g_read_try_scope = &direct_try;
        direct_try.message[0] = '\0';
        fe_set_memory_limit(span_ctx, baseline_used + 512);

        if (setjmp(direct_try.env) == 0) {
            (void)fex_compile(span_ctx, large_literal_source);
            g_read_try_scope = NULL;
            fe_handlers(span_ctx)->error = previous_error;
            fe_set_memory_limit(span_ctx, 0);
            free(large_literal_source);
            fe_close(span_ctx);
            fe_close(ctx);
            free(memory);
            free(span_memory);
            return fail("expected direct compile failure to escape through the error handler");
        }

        g_read_try_scope = NULL;
        fe_handlers(span_ctx)->error = previous_error;
        fe_set_memory_limit(span_ctx, 0);
        free(large_literal_source);
        if (strstr(direct_try.message, "memory limit exceeded") == NULL) {
            fe_close(span_ctx);
            fe_close(ctx);
            free(memory);
            free(span_memory);
            return fail("expected direct compile memory limit message");
        }
        if (fe_get_memory_used(span_ctx) != baseline_used) {
            fe_close(span_ctx);
            fe_close(ctx);
            free(memory);
            free(span_memory);
            return fail("expected direct compile temp allocations to be cleaned after error escape");
        }
    }
    fe_close(span_ctx);
    free(span_memory);

    {
        char *large_path = make_large_path_string(256 * 1024);
        fe_Object *bigpath;
        int gc_save = fe_savegc(ctx);
        int interrupt_calls = 0;

        if (!large_path) {
            fe_close(ctx);
            free(memory);
            return fail("failed to allocate pathjoin interrupt source");
        }

        bigpath = fe_string(ctx, large_path, strlen(large_path));
        free(large_path);
        fe_pushgc(ctx, bigpath);
        fe_set(ctx, fe_symbol(ctx, "bigpath"), bigpath);
        fe_restoregc(ctx, gc_save);

        fe_set_interrupt_handler(ctx, interrupt_once, &interrupt_calls, 1);
        status = fex_try_do_string(ctx, "pathjoin(bigpath);\n", &result, &error);
        fe_set_interrupt_handler(ctx, NULL, NULL, 0);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "execution interrupted") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected pathjoin to honor interrupt polling", status, &error);
        }
        if (interrupt_calls < 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected pathjoin interrupt handler to run");
        }
    }

    {
        const char *readbytes_path = "fex_try_api_abort_read.bin";
        char readbytes_source[256];
        int interrupt_calls = 0;

        if (!write_large_test_file(readbytes_path, 200 * 1024, 0x5a)) {
            fe_close(ctx);
            free(memory);
            return fail("failed to create readbytes interrupt fixture");
        }

        snprintf(readbytes_source, sizeof(readbytes_source),
            "readbytes(\"%s\");\n", readbytes_path);
        fe_set_interrupt_handler(ctx, interrupt_once, &interrupt_calls, 1);
        status = fex_try_do_string(ctx, readbytes_source, &result, &error);
        fe_set_interrupt_handler(ctx, NULL, NULL, 0);
        remove(readbytes_path);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "execution interrupted") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected readbytes to honor interrupt polling", status, &error);
        }
        if (interrupt_calls < 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected readbytes interrupt handler to run");
        }
    }

    {
        const char *listdir_path = "fex_try_api_abort_dir";
        char listdir_source[256];
        int interrupt_calls = 0;

        if (!make_test_directory(listdir_path)) {
            fe_close(ctx);
            free(memory);
            return fail("failed to create listdir interrupt fixture directory");
        }
        if (!create_test_directory_files(listdir_path, 256)) {
            cleanup_test_directory_files(listdir_path, 256);
            fe_close(ctx);
            free(memory);
            return fail("failed to populate listdir interrupt fixture directory");
        }

        snprintf(listdir_source, sizeof(listdir_source),
            "listdir(\"%s\");\n", listdir_path);
        fe_set_interrupt_handler(ctx, interrupt_once, &interrupt_calls, 1);
        status = fex_try_do_string(ctx, listdir_source, &result, &error);
        fe_set_interrupt_handler(ctx, NULL, NULL, 0);
        cleanup_test_directory_files(listdir_path, 256);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "execution interrupted") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail_status("expected listdir to honor interrupt polling", status, &error);
        }
        if (interrupt_calls < 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected listdir interrupt handler to run");
        }
    }

    fex_init_with_builtins(ctx, FEX_CONFIG_ENABLE_SPANS, FEX_BUILTINS_SYSTEM);
    status = fex_try_do_string(
        ctx,
#ifdef _WIN32
        "let proc = runcommand(\"powershell -NoProfile -EncodedCommand WwBDAG8AbgBzAG8AbABlAF0AOgA6AE8AcABlAG4AUwB0AGEAbgBkAGEAcgBkAE8AdQB0AHAAdQB0ACgAKQAuAFcAcgBpAHQAZQAoAFsAYgB5AHQAZQBbAF0AXQAoADEAMQAxACwAMQAxADcALAAxADEANgApACwAMAAsADMAKQA7AFsAQwBvAG4AcwBvAGwAZQBdADoAOgBPAHAAZQBuAFMAdABhAG4AZABhAHIAZABFAHIAcgBvAHIAKAApAC4AVwByAGkAdABlACgAWwBiAHkAdABlAFsAXQBdACgAMQAwADEALAAxADEANAAsADEAMQA0ACkALAAwACwAMwApADsAZQB4AGkAdAAgADMA\");\n"
#else
        "let proc = runcommand(\"sh -c 'printf out; printf err >&2; exit 3'\");\n"
#endif
        "proc;\n",
        &result,
        &error
    );
    if (status != FEX_STATUS_OK) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected runcommand to execute successfully", status, &error);
    }
    if (fe_type(ctx, result) != FE_TMAP) {
        fe_close(ctx);
        free(memory);
        return fail("expected runcommand to return a map");
    }
    if (fe_tonumber(ctx, fe_map_get(ctx, result, fe_symbol(ctx, "code"))) != 3) {
        fe_close(ctx);
        free(memory);
        return fail("expected runcommand to preserve the exit code");
    }
    if (fe_map_get(ctx, result, fe_symbol(ctx, "ok")) != FE_FALSE) {
        fe_close(ctx);
        free(memory);
        return fail("expected runcommand ok to be false for a non-zero exit");
    }
    output = fe_map_get(ctx, result, fe_symbol(ctx, "output"));
    if (fe_type(ctx, output) != FE_TBYTES || fe_byteslen(ctx, output) != 6) {
        fe_close(ctx);
        free(memory);
        return fail("expected runcommand output to be six captured bytes");
    }
    {
        unsigned char captured[6];
        memset(captured, 0, sizeof(captured));
        if (fe_bytescopy(ctx, output, 0, captured, sizeof(captured)) != sizeof(captured)) {
            fe_close(ctx);
            free(memory);
            return fail("expected runcommand output to be readable as bytes");
        }
        if (memcmp(captured, "outerr", sizeof(captured)) != 0) {
            fe_close(ctx);
            free(memory);
            return fail("expected runcommand to merge stdout and stderr output");
        }
    }

    fex_init_with_builtins(ctx, FEX_CONFIG_ENABLE_SPANS,
        FEX_BUILTINS_SYSTEM | FEX_BUILTINS_DATA);
    status = fex_try_do_string(
        ctx,
        "let proc = runprocess(\"" FEX_TEST_PYTHON_EXECUTABLE "\", [\"-c\", \"import os, pathlib, sys; data = sys.stdin.buffer.read(); sys.stdout.buffer.write(data.upper()); sys.stderr.buffer.write(os.getenv('FEX_TEST', 'missing').encode('ascii')); sys.stderr.buffer.write(b'@'); sys.stderr.buffer.write(pathlib.Path().resolve().name.encode('ascii')); raise SystemExit(5)\"], "
            "makemap(\"stdin\", tobytes(\"abc\"), \"cwd\", \"" TEST_SCRIPTS_DIR "\", "
            "\"env\", makemap(\"FEX_TEST\", \"env\")));\n"
        "proc;\n",
        &result,
        &error
    );
    if (status != FEX_STATUS_OK) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected runprocess to execute successfully", status, &error);
    }
    if (fe_type(ctx, result) != FE_TMAP) {
        fe_close(ctx);
        free(memory);
        return fail("expected runprocess to return a map");
    }
    if (fe_tonumber(ctx, fe_map_get(ctx, result, fe_symbol(ctx, "code"))) != 5) {
        fe_close(ctx);
        free(memory);
        return fail("expected runprocess to preserve the exit code");
    }
    if (fe_map_get(ctx, result, fe_symbol(ctx, "ok")) != FE_FALSE) {
        fe_close(ctx);
        free(memory);
        return fail("expected runprocess ok to be false for a non-zero exit");
    }
    output = fe_map_get(ctx, result, fe_symbol(ctx, "stdout"));
    if (fe_type(ctx, output) != FE_TBYTES || fe_byteslen(ctx, output) != 3) {
        fe_close(ctx);
        free(memory);
        return fail("expected runprocess stdout to be captured as bytes");
    }
    {
        unsigned char captured_stdout[3];
        memset(captured_stdout, 0, sizeof(captured_stdout));
        if (fe_bytescopy(ctx, output, 0, captured_stdout, sizeof(captured_stdout)) != sizeof(captured_stdout)) {
            fe_close(ctx);
            free(memory);
            return fail("expected runprocess stdout bytes to be readable");
        }
        if (memcmp(captured_stdout, "ABC", sizeof(captured_stdout)) != 0) {
            fe_close(ctx);
            free(memory);
            return fail("expected runprocess stdout to reflect redirected stdin");
        }
    }
    output = fe_map_get(ctx, result, fe_symbol(ctx, "stderr"));
    if (fe_type(ctx, output) != FE_TBYTES || fe_byteslen(ctx, output) != 11) {
        fe_close(ctx);
        free(memory);
        return fail("expected runprocess stderr to be captured as bytes");
    }
    {
        unsigned char captured_stderr[11];
        memset(captured_stderr, 0, sizeof(captured_stderr));
        if (fe_bytescopy(ctx, output, 0, captured_stderr, sizeof(captured_stderr)) != sizeof(captured_stderr)) {
            fe_close(ctx);
            free(memory);
            return fail("expected runprocess stderr bytes to be readable");
        }
        if (memcmp(captured_stderr, "env@scripts", sizeof(captured_stderr)) != 0) {
            fe_close(ctx);
            free(memory);
            return fail("expected runprocess stderr to reflect env and cwd overrides");
        }
    }

    status = fex_try_do_string(
        ctx,
        "let proc = runprocess(\"" FEX_TEST_PYTHON_EXECUTABLE "\", [\"-c\", \"import sys; sys.stdout.write('inherit-out\\\\n'); sys.stderr.write('discard-err\\\\n')\"], "
            "makemap(\"stdout\", \"inherit\", \"stderr\", \"discard\"));\n"
        "proc;\n",
        &result,
        &error
    );
    if (status != FEX_STATUS_OK) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected runprocess stream modes to execute successfully", status, &error);
    }
    output = fe_map_get(ctx, result, fe_symbol(ctx, "stdout"));
    if (!fe_isnil(ctx, output)) {
        fe_close(ctx);
        free(memory);
        return fail("expected inherited stdout to return nil in the result map");
    }
    output = fe_map_get(ctx, result, fe_symbol(ctx, "stderr"));
    if (!fe_isnil(ctx, output)) {
        fe_close(ctx);
        free(memory);
        return fail("expected discarded stderr to return nil in the result map");
    }

    status = fex_try_do_string(
        ctx,
        "runprocess(\"" FEX_TEST_PYTHON_EXECUTABLE "\", [\"-c\", \"import sys; sys.stdout.write('abcdef')\"], "
            "makemap(\"max_stdout\", 4));\n",
        &result,
        &error
    );
    if (status != FEX_STATUS_RUNTIME_ERROR ||
        strstr(error.message, "runprocess stdout: file too large") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail_status("expected runprocess capture limit error", status, &error);
    }

    status = fex_try_do_string(ctx, "41 + 1;", &result, &error);
    if (status != FEX_STATUS_OK || fe_tonumber(ctx, result) != 42) {
        fe_close(ctx);
        free(memory);
        return fail("context did not recover after handled errors");
    }

    fe_close(ctx);
    free(memory);
    return 0;
}
