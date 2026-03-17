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
#include <ctype.h>

#include "fe.h"
#include "fex.h"

#define MEMORY_POOL_SIZE (5 * 1024 * 1024)
#define REPL_BUFFER_SIZE 1024

static int exit_code_for_status(FexStatus status) {
  switch (status) {
    case FEX_STATUS_COMPILE_ERROR:
      return 65;
    case FEX_STATUS_RUNTIME_ERROR:
      return 70;
    case FEX_STATUS_IO_ERROR:
      return 74;
    default:
      return 1;
  }
}

static int builtin_mask_from_name(const char *name, FexBuiltinsConfig *out_mask) {
  if (strcmp(name, "math") == 0) {
    *out_mask = FEX_BUILTINS_MATH;
  } else if (strcmp(name, "string") == 0) {
    *out_mask = FEX_BUILTINS_STRING;
  } else if (strcmp(name, "list") == 0) {
    *out_mask = FEX_BUILTINS_LIST;
  } else if (strcmp(name, "io") == 0) {
    *out_mask = FEX_BUILTINS_IO;
  } else if (strcmp(name, "system") == 0) {
    *out_mask = FEX_BUILTINS_SYSTEM;
  } else if (strcmp(name, "type") == 0) {
    *out_mask = FEX_BUILTINS_TYPE;
  } else if (strcmp(name, "data") == 0) {
    *out_mask = FEX_BUILTINS_DATA;
  } else if (strcmp(name, "safe") == 0) {
    *out_mask = FEX_BUILTINS_SAFE;
  } else if (strcmp(name, "all") == 0) {
    *out_mask = FEX_BUILTINS_ALL;
  } else {
    return 0;
  }
  return 1;
}

static int add_builtin_spec(FexBuiltinsConfig *mask, const char *spec) {
  char *copy;
  char *token;
  size_t len;

  len = strlen(spec);
  copy = malloc(len + 1);
  if (!copy) {
    return 0;
  }
  memcpy(copy, spec, len + 1);

  token = copy;
  while (token != NULL) {
    FexBuiltinsConfig token_mask;
    char *start = token;
    char *end;
    char *next = strchr(token, ',');

    if (next != NULL) {
      *next = '\0';
    }

    while (*start != '\0' && isspace((unsigned char)*start)) {
      start++;
    }
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
      end--;
    }
    *end = '\0';

    if (*start == '\0' || !builtin_mask_from_name(start, &token_mask)) {
      free(copy);
      return 0;
    }
    *mask |= token_mask;
    token = (next != NULL) ? next + 1 : NULL;
  }

  free(copy);
  return 1;
}

static void run_repl(fe_Context *ctx) {
  char buffer[REPL_BUFFER_SIZE];
  FexError error;
  fe_Object *result;
  FexStatus status;

  printf("FeX v1.0 (Modern Syntax Layer for enhanced Fe code)\n");
  while (1) {
    printf("> ");
    if (!fgets(buffer, sizeof(buffer), stdin)) {
      printf("\n");
      break;
    }

    status = fex_try_do_string(ctx, buffer, &result, &error);
    if (status != FEX_STATUS_OK) {
      fex_print_error(stderr, &error);
      continue;
    }

    if (result) {
      fe_writefp(ctx, result, stdout);
      printf("\n");
    }
  }
}

static int run_file(fe_Context *ctx, const char *path) {
  FexError error;
  fe_Object *result;
  FexStatus status = fex_try_do_file(ctx, path, &result, &error);

  (void)result;
  if (status != FEX_STATUS_OK) {
    fex_print_error(stderr, &error);
    return exit_code_for_status(status);
  }
  return 0;
}

static void print_usage(const char *program_name) {
  fprintf(stderr, "Usage: %s [options] [file]\n", program_name);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  --spans       Enable detailed error reporting with source spans\n");
  fprintf(stderr, "  --builtins    Enable all extended built-in functions\n");
  fprintf(stderr, "  --builtin NAME  Enable a builtin category or preset (may repeat, comma-separated)\n");
  fprintf(stderr, "                Categories: math, string, list, io, system, type, data\n");
  fprintf(stderr, "                Presets: safe, all\n");
  fprintf(stderr, "  --module-path PATH  Add a module search directory (may be repeated)\n");
  fprintf(stderr, "  -I PATH       Alias for --module-path\n");
  fprintf(stderr, "  --max-steps N  Abort evaluation after approximately N eval steps (0 disables)\n");
  fprintf(stderr, "  --memory-pool-size SIZE  Set memory pool size in MB (default: 5MB)\n");
  fprintf(stderr, "  --help        Show this help message\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "If no file is provided, starts the interactive REPL.\n");
}

int main(int argc, char **argv) {
  int enable_spans = 0, i, module_path_count = 0;
  int exit_code = 0;
  size_t memory_pool_size = MEMORY_POOL_SIZE;
  size_t max_steps = 0;
  const char *filename = NULL;
  const char **module_paths = NULL;
  void *mem;
  fe_Context *ctx;
  FexConfig config;
  FexBuiltinsConfig builtins = FEX_BUILTINS_NONE;

  module_paths = malloc((size_t)((argc > 0) ? argc : 1) * sizeof(*module_paths));
  if (!module_paths) {
    fprintf(stderr, "Failed to allocate module path storage.\n");
    return 1;
  }

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--spans") == 0) {
      enable_spans = 1;
    } else if (strcmp(argv[i], "--builtins") == 0) {
      builtins |= FEX_BUILTINS_ALL;
    } else if (strcmp(argv[i], "--builtin") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --builtin requires a category or preset name\n");
        print_usage(argv[0]);
        free(module_paths);
        return 64;
      }
      i++;
      if (!add_builtin_spec(&builtins, argv[i])) {
        fprintf(stderr, "Error: Unknown builtin category or preset '%s'\n", argv[i]);
        print_usage(argv[0]);
        free(module_paths);
        return 64;
      }
    } else if (strcmp(argv[i], "--module-path") == 0 || strcmp(argv[i], "-I") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: %s requires a path argument\n", argv[i]);
        print_usage(argv[0]);
        free(module_paths);
        return 64;
      }
      module_paths[module_path_count++] = argv[++i];
    } else if (strcmp(argv[i], "--max-steps") == 0) {
      char *endptr;
      unsigned long long parsed_steps;
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --max-steps requires an integer value\n");
        print_usage(argv[0]);
        free(module_paths);
        return 64;
      }
      i++;
      parsed_steps = strtoull(argv[i], &endptr, 10);
      if (*endptr != '\0' || (size_t)parsed_steps != parsed_steps) {
        fprintf(stderr, "Error: Invalid max step count '%s'. Must be a non-negative integer.\n", argv[i]);
        print_usage(argv[0]);
        free(module_paths);
        return 64;
      }
      max_steps = (size_t)parsed_steps;
    } else if (strcmp(argv[i], "--memory-pool-size") == 0) {
      char *endptr;
      long size_mb;
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --memory-pool-size requires a value in MB\n");
        print_usage(argv[0]);
        free(module_paths);
        return 64;
      }
      i++;
      size_mb = strtol(argv[i], &endptr, 10);
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

  mem = malloc(memory_pool_size);
  if (!mem) {
    fprintf(stderr, "Failed to allocate %zu bytes for interpreter.\n", memory_pool_size);
    free(module_paths);
    return 1;
  }

  ctx = fe_open(mem, memory_pool_size);
  if (!ctx) {
    fprintf(stderr, "Failed to initialize interpreter context.\n");
    free(mem);
    free(module_paths);
    return 1;
  }

  config = FEX_CONFIG_NONE;
  if (enable_spans) {
    config |= FEX_CONFIG_ENABLE_SPANS;
  }
  fex_init_with_builtins(ctx, config, builtins);
  fe_set_step_limit(ctx, max_steps);

  for (i = 0; i < module_path_count; i++) {
    if (!fex_add_import_path(ctx, module_paths[i])) {
      fprintf(stderr, "Failed to add module path \"%s\".\n", module_paths[i]);
      free(module_paths);
      fe_close(ctx);
      free(mem);
      return 1;
    }
  }
  free(module_paths);

  if (filename == NULL) {
    run_repl(ctx);
  } else {
    exit_code = run_file(ctx, filename);
  }

  fe_close(ctx);
  free(mem);
  return exit_code;
}
