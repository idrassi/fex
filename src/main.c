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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "fe.h"
#include "fex.h"

#define MEMORY_POOL_SIZE (5 * 1024 * 1024) /* 5MB */
#define REPL_BUFFER_SIZE 1024

static jmp_buf toplevel;
static fe_Context *g_ctx = NULL;

/* Custom error handler to jump back to the REPL loop */
static void on_error(fe_Context *ctx, const char *msg, fe_Object *cl) {
  (void)ctx; (void)cl;
  if (g_ctx) {
    fex_reset_import_state(g_ctx);
  }
  fprintf(stderr, "runtime error: %s\n", msg);
  /* We don't print the fe stack trace as it's not useful for the new syntax */
  longjmp(toplevel, 1);
}

static void run_repl() {
  char buffer[REPL_BUFFER_SIZE];
  printf("FeX v1.0 (Modern Syntax Layer for enhanced Fe code)\n");

  /* Set the error handler only for the REPL */
  fe_handlers(g_ctx)->error = on_error;

  while (1) {
    if (setjmp(toplevel) == 0) {
      printf("> ");
      if (!fgets(buffer, sizeof(buffer), stdin)) {
        printf("\n");
        break;
      }

      fe_Object* result = fex_do_string(g_ctx, buffer);

      if (result) { /* NULL indicates a compile error which is already printed */
        fe_writefp(g_ctx, result, stdout);
        printf("\n");
      }
    }
  }
}

static void run_file(const char* path) {
  FILE *file = fopen(path, "rb");
  fe_Object *result;
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }
  fclose(file);

  result = fex_do_file(g_ctx, path);

  if (result == NULL) {
      exit(65); /* Compilation error */
  }
}

static void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options] [file]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --spans       Enable detailed error reporting with source spans\n");
    fprintf(stderr, "  --builtins    Enable extended built-in functions\n");
    fprintf(stderr, "  --module-path PATH  Add a module search directory (may be repeated)\n");
    fprintf(stderr, "  -I PATH       Alias for --module-path\n");
    fprintf(stderr, "  --memory-pool-size SIZE  Set memory pool size in MB (default: 5MB)\n");
    fprintf(stderr, "  --help        Show this help message\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "If no file is provided, starts the interactive REPL.\n");
}

int main(int argc, char **argv) {
  int enable_spans = 0, enable_builtins = 0, i, module_path_count = 0;
  size_t memory_pool_size = MEMORY_POOL_SIZE;
  const char* filename = NULL;
  const char** module_paths = NULL;
  void* mem;
  FexConfig config;

  module_paths = malloc((size_t)((argc > 0) ? argc : 1) * sizeof(*module_paths));
  if (!module_paths) {
    fprintf(stderr, "Failed to allocate module path storage.\n");
    return 1;
  }
  
  /* Parse command line arguments */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--spans") == 0) {
      enable_spans = 1;
    } else if (strcmp(argv[i], "--builtins") == 0) {
      enable_builtins = 1;
    } else if (strcmp(argv[i], "--module-path") == 0 || strcmp(argv[i], "-I") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: %s requires a path argument\n", argv[i]);
        print_usage(argv[0]);
        free(module_paths);
        return 64;
      }
      module_paths[module_path_count++] = argv[++i];
    } else if (strcmp(argv[i], "--memory-pool-size") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --memory-pool-size requires a value in MB\n");
        print_usage(argv[0]);
        free(module_paths);
        return 64;
      }
      i++;
      char* endptr;
      long size_mb = strtol(argv[i], &endptr, 10);
      if (*endptr != '\0' || size_mb <= 0) {
        fprintf(stderr, "Error: Invalid memory pool size '%s'. Must be a positive integer in MB.\n", argv[i]);
        print_usage(argv[0]);
        free(module_paths);
        return 64;
      }
      memory_pool_size = (size_t)size_mb * 1024 * 1024;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      free(module_paths);
      return 0;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      free(module_paths);
      return 64;
    } else {
      if (filename != NULL) {
        fprintf(stderr, "Multiple input files specified.\n");
        print_usage(argv[0]);
        free(module_paths);
        return 64;
      }
      filename = argv[i];
    }
  }
  
  /* Allocate memory pool for the fe context */
  mem = malloc(memory_pool_size);
  if (!mem) {
    fprintf(stderr, "Failed to allocate %zu bytes for interpreter.\n", memory_pool_size);
    free(module_paths);
    return 1;
  }
  
  g_ctx = fe_open(mem, memory_pool_size);
  if (!g_ctx) {
    fprintf(stderr, "Failed to initialize interpreter context.\n");
    free(mem);
    free(module_paths);
    return 1;
  }

  /* Initialize our custom environment with conditional support */
  config = FEX_CONFIG_NONE;
  if (enable_spans) config |= FEX_CONFIG_ENABLE_SPANS;
  if (enable_builtins) config |= FEX_CONFIG_ENABLE_EXTENDED_BUILTINS;
  fex_init_with_config(g_ctx, config);
  for (i = 0; i < module_path_count; i++) {
    if (!fex_add_import_path(g_ctx, module_paths[i])) {
      fprintf(stderr, "Failed to add module path \"%s\".\n", module_paths[i]);
      free(module_paths);
      fe_close(g_ctx);
      free(mem);
      return 1;
    }
  }
  free(module_paths);
  
  if (filename == NULL) {
    run_repl();
  } else {
    run_file(filename);
  }

  fe_close(g_ctx);
  free(mem);
  return 0;
}
