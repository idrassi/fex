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

static int interrupt_once(fe_Context *ctx, void *udata) {
    int *calls = (int*)udata;
    (void)ctx;
    (*calls)++;
    return *calls >= 1;
}

int main(void) {
    void *memory;
    fe_Context *ctx;
    fe_Object *map;
    fe_Object *name_key;
    fe_Object *name_value;
    fe_Object *result;
    FexError error;
    FexStatus status;
    char buffer[64];

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

    fe_set_step_limit(ctx, 64);
    status = fex_try_do_string(
        ctx,
        "let n = 0;\n"
        "while (true) { n = n + 1; }\n",
        &result,
        &error
    );
    if (status != FEX_STATUS_RUNTIME_ERROR ||
        strstr(error.message, "execution step limit exceeded") == NULL) {
        fe_close(ctx);
        free(memory);
        return fail("expected execution step limit error");
    }
    if (fe_get_steps_executed(ctx) <= 64) {
        fe_close(ctx);
        free(memory);
        return fail("expected step counter to advance past the configured limit");
    }
    fe_set_step_limit(ctx, 0);

    {
        int interrupt_calls = 0;
        fe_set_interrupt_handler(ctx, interrupt_once, &interrupt_calls, 8);
        status = fex_try_do_string(
            ctx,
            "let n = 0;\n"
            "while (n < 1000) { n = n + 1; }\n",
            &result,
            &error
        );
        fe_set_interrupt_handler(ctx, NULL, NULL, 0);
        if (status != FEX_STATUS_RUNTIME_ERROR ||
            strstr(error.message, "execution interrupted") == NULL) {
            fe_close(ctx);
            free(memory);
            return fail("expected interrupt handler to stop evaluation");
        }
        if (interrupt_calls < 1) {
            fe_close(ctx);
            free(memory);
            return fail("expected interrupt handler to run at least once");
        }
    }

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