#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fe.h"
#include "fex.h"

#define TEST_MEM_SIZE (1024 * 1024)

static int fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(void) {
    void *memory;
    fe_Context *ctx;
    fe_Object *result;
    FexError error;
    FexStatus status;

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