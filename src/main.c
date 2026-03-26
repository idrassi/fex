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
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#define FEX_ISATTY _isatty
#define FEX_FILENO _fileno
#else
#include <unistd.h>
#define FEX_ISATTY isatty
#define FEX_FILENO fileno
#endif

#include "fe.h"
#include "fex.h"

#define MEMORY_POOL_SIZE (5 * 1024 * 1024)
#define REPL_BUFFER_SIZE 1024

#ifdef _WIN32
static char *utf8_from_wide_arg(const wchar_t *src) {
  int needed;
  char *dst;

  needed = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
  if (needed <= 0) {
    return NULL;
  }
  dst = (char*)malloc((size_t)needed);
  if (!dst) {
    return NULL;
  }
  if (WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, needed, NULL, NULL) <= 0) {
    free(dst);
    return NULL;
  }
  return dst;
}

static void free_utf8_argv(int argc, char **argv) {
  int i;
  if (!argv) {
    return;
  }
  for (i = 0; i < argc; i++) {
    free(argv[i]);
  }
  free(argv);
}

static int fex_main_utf8(int argc, char **argv);

static int windows_main_utf8_from_command_line(void) {
  LPWSTR *wargv;
  char **argv;
  int argc = 0;
  int i;
  int result;

  wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!wargv) {
    fprintf(stderr, "Failed to read Unicode command-line arguments.\n");
    return 1;
  }

  argv = (char**)calloc((size_t)((argc > 0) ? argc : 1), sizeof(*argv));
  if (!argv) {
    fprintf(stderr, "Failed to allocate UTF-8 argv storage.\n");
    LocalFree(wargv);
    return 1;
  }

  for (i = 0; i < argc; i++) {
    argv[i] = utf8_from_wide_arg(wargv[i]);
    if (!argv[i]) {
      fprintf(stderr, "Failed to convert command-line arguments to UTF-8.\n");
      LocalFree(wargv);
      free_utf8_argv(argc, argv);
      return 1;
    }
  }

  LocalFree(wargv);
  result = fex_main_utf8(argc, argv);
  free_utf8_argv(argc, argv);
  return result;
}
#endif

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

static int append_source_fragment(char **buffer, size_t *len, size_t *cap,
                                  const char *fragment, size_t fragment_len) {
  size_t needed;
  size_t new_cap;
  char *new_buffer;

  if (fragment_len == 0) {
    return 1;
  }

  needed = *len + fragment_len + 1;
  if (needed > *cap) {
    new_cap = (*cap > 0) ? *cap : 128;
    while (new_cap < needed) {
      new_cap *= 2;
    }
    new_buffer = realloc(*buffer, new_cap);
    if (!new_buffer) {
      return 0;
    }
    *buffer = new_buffer;
    *cap = new_cap;
  }

  memcpy(*buffer + *len, fragment, fragment_len);
  *len += fragment_len;
  (*buffer)[*len] = '\0';
  return 1;
}

static int read_stream_source(FILE *stream, char **out_source) {
  char chunk[4096];
  char *buffer = NULL;
  size_t len = 0;
  size_t cap = 0;
  size_t bytes_read;

  while ((bytes_read = fread(chunk, 1, sizeof(chunk), stream)) > 0) {
    if (!append_source_fragment(&buffer, &len, &cap, chunk, bytes_read)) {
      free(buffer);
      return 0;
    }
  }

  if (ferror(stream)) {
    free(buffer);
    return 0;
  }

  if (buffer == NULL) {
    buffer = malloc(1);
    if (!buffer) {
      return 0;
    }
    buffer[0] = '\0';
  }

  *out_source = buffer;
  return 1;
}

static int stdin_is_interactive(void) {
  return FEX_ISATTY(FEX_FILENO(stdin));
}

static void print_version(void) {
  printf("FeX %s\n", FE_VERSION);
}

static void print_runtime_stats(FILE *fp, fe_Context *ctx) {
  fe_Stats stats;

  fe_get_stats(ctx, &stats);
  fprintf(fp, "runtime stats:\n");
  fprintf(fp, "  steps_executed: %llu\n", (unsigned long long)stats.steps_executed);
  fprintf(fp, "  step_limit: %llu\n", (unsigned long long)stats.step_limit);
  fprintf(fp, "  timeout_ms: %" PRIu64 "\n", stats.timeout_ms);
  fprintf(fp, "  memory_used: %llu\n", (unsigned long long)stats.memory_used);
  fprintf(fp, "  peak_memory_used: %llu\n", (unsigned long long)stats.peak_memory_used);
  fprintf(fp, "  memory_limit: %llu\n", (unsigned long long)stats.memory_limit);
  fprintf(fp, "  base_memory_bytes: %llu\n", (unsigned long long)stats.base_memory_bytes);
  fprintf(fp, "  object_capacity: %llu\n", (unsigned long long)stats.object_capacity);
  fprintf(fp, "  live_objects: %llu\n", (unsigned long long)stats.live_objects);
  fprintf(fp, "  object_allocations_total: %llu\n", (unsigned long long)stats.object_allocations_total);
  fprintf(fp, "  allocs_since_gc: %llu\n", (unsigned long long)stats.allocs_since_gc);
  fprintf(fp, "  gc_runs: %llu\n", (unsigned long long)stats.gc_runs);
}

/* --- JSON output helpers --- */

static void json_write_escaped_string(FILE *fp, const char *s) {
  fputc('"', fp);
  for (; *s; s++) {
    switch (*s) {
      case '"':  fprintf(fp, "\\\""); break;
      case '\\': fprintf(fp, "\\\\"); break;
      case '\n': fprintf(fp, "\\n"); break;
      case '\r': fprintf(fp, "\\r"); break;
      case '\t': fprintf(fp, "\\t"); break;
      default:
        if ((unsigned char)*s < 0x20) {
          fprintf(fp, "\\u%04x", (unsigned char)*s);
        } else {
          fputc(*s, fp);
        }
    }
  }
  fputc('"', fp);
}

static const char* status_string(FexStatus status) {
  switch (status) {
    case FEX_STATUS_OK:            return "ok";
    case FEX_STATUS_COMPILE_ERROR: return "compile_error";
    case FEX_STATUS_RUNTIME_ERROR: return "runtime_error";
    case FEX_STATUS_IO_ERROR:      return "io_error";
    default:                       return "error";
  }
}

static void print_json_stats(FILE *fp, fe_Context *ctx) {
  fe_Stats stats;

  fe_get_stats(ctx, &stats);
  fprintf(fp, "{\"steps_executed\":%llu", (unsigned long long)stats.steps_executed);
  fprintf(fp, ",\"step_limit\":%llu", (unsigned long long)stats.step_limit);
  fprintf(fp, ",\"timeout_ms\":%" PRIu64, stats.timeout_ms);
  fprintf(fp, ",\"memory_used\":%llu", (unsigned long long)stats.memory_used);
  fprintf(fp, ",\"peak_memory_used\":%llu", (unsigned long long)stats.peak_memory_used);
  fprintf(fp, ",\"memory_limit\":%llu", (unsigned long long)stats.memory_limit);
  fprintf(fp, ",\"base_memory_bytes\":%llu", (unsigned long long)stats.base_memory_bytes);
  fprintf(fp, ",\"object_capacity\":%llu", (unsigned long long)stats.object_capacity);
  fprintf(fp, ",\"live_objects\":%llu", (unsigned long long)stats.live_objects);
  fprintf(fp, ",\"object_allocations_total\":%llu", (unsigned long long)stats.object_allocations_total);
  fprintf(fp, ",\"allocs_since_gc\":%llu", (unsigned long long)stats.allocs_since_gc);
  fprintf(fp, ",\"gc_runs\":%llu}", (unsigned long long)stats.gc_runs);
}

static void print_json_result(FILE *fp, int exit_code, const FexError *error,
                               fe_Context *ctx, int include_stats) {
  int i;

  fprintf(fp, "{\"status\":");
  if (error != NULL && error->status != FEX_STATUS_OK) {
    json_write_escaped_string(fp, status_string(error->status));
  } else {
    fprintf(fp, "\"ok\"");
  }
  fprintf(fp, ",\"exit_code\":%d", exit_code);

  if (error != NULL && error->status != FEX_STATUS_OK) {
    fprintf(fp, ",\"error\":{\"message\":");
    json_write_escaped_string(fp, error->message[0] ? error->message : "unknown error");
    if (error->source_name[0]) {
      fprintf(fp, ",\"source\":");
      json_write_escaped_string(fp, error->source_name);
    }
    if (error->line > 0) {
      fprintf(fp, ",\"line\":%d,\"column\":%d",
              error->line, error->column > 0 ? error->column : 1);
    }
    if (error->frame_count > 0) {
      fprintf(fp, ",\"trace\":[");
      for (i = 0; i < error->frame_count; i++) {
        const FexErrorFrame *frame = &error->frames[i];
        if (i > 0) fputc(',', fp);
        fputc('{', fp);
        if (frame->source_name[0]) {
          fprintf(fp, "\"source\":");
          json_write_escaped_string(fp, frame->source_name);
          if (frame->line > 0) {
            fprintf(fp, ",\"line\":%d,\"column\":%d",
                    frame->line, frame->column > 0 ? frame->column : 1);
          }
        }
        if (frame->expression[0]) {
          if (frame->source_name[0]) fputc(',', fp);
          fprintf(fp, "\"expression\":");
          json_write_escaped_string(fp, frame->expression);
        }
        fputc('}', fp);
      }
      fputc(']', fp);
    }
    fputc('}', fp);
  }

  if (include_stats) {
    fprintf(fp, ",\"stats\":");
    print_json_stats(fp, ctx);
  }

  fprintf(fp, "}\n");
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

static int run_file(fe_Context *ctx, const char *path, int json_output,
                    FexError *out_error) {
  FexError error;
  fe_Object *result;
  FexStatus status = fex_try_do_file(ctx, path, &result, &error);

  (void)result;
  if (status != FEX_STATUS_OK) {
    if (out_error) *out_error = error;
    if (!json_output) fex_print_error(stderr, &error);
    return exit_code_for_status(status);
  }
  return 0;
}

static int run_source(fe_Context *ctx, const char *source, const char *source_name,
                      int json_output, FexError *out_error) {
  FexError error;
  fe_Object *result = NULL;
  FexStatus status;

  if (source == NULL || source[0] == '\0') {
    return 0;
  }

  status = fex_try_do_string_named(ctx, source, source_name, &result, &error);
  (void)result;
  if (status != FEX_STATUS_OK) {
    if (out_error) *out_error = error;
    if (!json_output) fex_print_error(stderr, &error);
    return exit_code_for_status(status);
  }

  return 0;
}

static void print_usage(const char *program_name) {
  fprintf(stderr, "Usage: %s [options] [file|-]\n", program_name);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -e CODE       Evaluate inline source (may repeat)\n");
  fprintf(stderr, "  --spans       Enable detailed error reporting with source spans\n");
  fprintf(stderr, "  --builtins    Enable all extended built-in functions\n");
  fprintf(stderr, "  --builtin NAME  Enable a builtin category or preset (may repeat, comma-separated)\n");
  fprintf(stderr, "                Categories: math, string, list, io, system, type, data\n");
  fprintf(stderr, "                Presets: safe, all\n");
  fprintf(stderr, "  --module-path PATH  Add a module search directory (may be repeated)\n");
  fprintf(stderr, "  -I PATH       Alias for --module-path\n");
  fprintf(stderr, "  --max-steps N  Abort evaluation after approximately N eval steps (0 disables)\n");
  fprintf(stderr, "  --timeout-ms N  Abort evaluation after roughly N milliseconds (0 disables)\n");
  fprintf(stderr, "  --max-memory N  Abort when tracked context memory exceeds N bytes (0 disables)\n");
  fprintf(stderr, "  --max-eval-depth N  Limit eval recursion depth (0 disables, default: 512)\n");
  fprintf(stderr, "  --max-read-depth N  Limit read nesting depth (0 disables, default: 512)\n");
  fprintf(stderr, "  --json-output Emit structured JSON diagnostics to stderr instead of text\n");
  fprintf(stderr, "  --stats       Print runtime stats to stderr after non-REPL execution\n");
  fprintf(stderr, "  --memory-pool-size SIZE  Set memory pool size in MB (default: 5MB)\n");
  fprintf(stderr, "  --version, -V  Show version information\n");
  fprintf(stderr, "  --help        Show this help message\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Use '-' to read source from stdin. If no file or -e input is provided,\n");
  fprintf(stderr, "the CLI reads stdin when piped input is present; otherwise it starts the REPL.\n");
}

static int fex_main_utf8(int argc, char **argv) {
  int enable_spans = 0, i, module_path_count = 0;
  int read_stdin = 0;
  int end_of_options = 0;
  int show_stats = 0;
  int json_output = 0;
  int exit_code = 0;
  int stdin_interactive;
  size_t memory_pool_size = MEMORY_POOL_SIZE;
  size_t max_steps = 0;
  size_t max_memory = 0;
  uint64_t timeout_ms = 0;
  int max_eval_depth = -1;  /* -1 = use default */
  int max_read_depth = -1;
  const char *filename = NULL;
  char *eval_source = NULL;
  size_t eval_source_len = 0;
  size_t eval_source_cap = 0;
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
    if (!end_of_options && strcmp(argv[i], "--") == 0) {
      end_of_options = 1;
    } else if (!end_of_options && strcmp(argv[i], "-e") == 0) {
      if (filename != NULL || read_stdin) {
        fprintf(stderr, "Multiple input sources specified.\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: -e requires a source string\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      if (eval_source != NULL &&
          !append_source_fragment(&eval_source, &eval_source_len,
                                  &eval_source_cap, "\n", 1)) {
        fprintf(stderr, "Failed to allocate inline source buffer.\n");
        free(eval_source);
        free(module_paths);
        return 1;
      }
      i++;
      if (!append_source_fragment(&eval_source, &eval_source_len,
                                  &eval_source_cap, argv[i], strlen(argv[i]))) {
        fprintf(stderr, "Failed to allocate inline source buffer.\n");
        free(eval_source);
        free(module_paths);
        return 1;
      }
    } else if (!end_of_options && strcmp(argv[i], "--spans") == 0) {
      enable_spans = 1;
    } else if (!end_of_options && strcmp(argv[i], "--builtins") == 0) {
      builtins |= FEX_BUILTINS_ALL;
    } else if (!end_of_options && strcmp(argv[i], "--builtin") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --builtin requires a category or preset name\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      i++;
      if (!add_builtin_spec(&builtins, argv[i])) {
        fprintf(stderr, "Error: Unknown builtin category or preset '%s'\n", argv[i]);
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
    } else if (!end_of_options &&
               (strcmp(argv[i], "--module-path") == 0 || strcmp(argv[i], "-I") == 0)) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: %s requires a path argument\n", argv[i]);
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      module_paths[module_path_count++] = argv[++i];
    } else if (!end_of_options && strcmp(argv[i], "--max-steps") == 0) {
      char *endptr;
      unsigned long long parsed_steps;
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --max-steps requires an integer value\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      i++;
      parsed_steps = strtoull(argv[i], &endptr, 10);
      if (*endptr != '\0' || (size_t)parsed_steps != parsed_steps) {
        fprintf(stderr, "Error: Invalid max step count '%s'. Must be a non-negative integer.\n", argv[i]);
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      max_steps = (size_t)parsed_steps;
    } else if (!end_of_options && strcmp(argv[i], "--timeout-ms") == 0) {
      char *endptr;
      unsigned long long parsed_timeout_ms;
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --timeout-ms requires an integer value\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      i++;
      parsed_timeout_ms = strtoull(argv[i], &endptr, 10);
      if (*endptr != '\0' || (uint64_t)parsed_timeout_ms != parsed_timeout_ms) {
        fprintf(stderr, "Error: Invalid timeout '%s'. Must be a non-negative integer in milliseconds.\n", argv[i]);
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      timeout_ms = (uint64_t)parsed_timeout_ms;
    } else if (!end_of_options && strcmp(argv[i], "--max-memory") == 0) {
      char *endptr;
      unsigned long long parsed_max_memory;
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --max-memory requires an integer value\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      i++;
      parsed_max_memory = strtoull(argv[i], &endptr, 10);
      if (*endptr != '\0' || (size_t)parsed_max_memory != parsed_max_memory) {
        fprintf(stderr, "Error: Invalid memory limit '%s'. Must be a non-negative integer in bytes.\n", argv[i]);
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      max_memory = (size_t)parsed_max_memory;
    } else if (!end_of_options && strcmp(argv[i], "--max-eval-depth") == 0) {
      char *endptr;
      long parsed_depth;
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --max-eval-depth requires an integer value\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      i++;
      parsed_depth = strtol(argv[i], &endptr, 10);
      if (*endptr != '\0' || parsed_depth < 0 || parsed_depth > INT_MAX) {
        fprintf(stderr, "Error: Invalid eval depth limit '%s'. Must be a non-negative integer.\n", argv[i]);
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      max_eval_depth = (int)parsed_depth;
    } else if (!end_of_options && strcmp(argv[i], "--max-read-depth") == 0) {
      char *endptr;
      long parsed_depth;
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --max-read-depth requires an integer value\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      i++;
      parsed_depth = strtol(argv[i], &endptr, 10);
      if (*endptr != '\0' || parsed_depth < 0 || parsed_depth > INT_MAX) {
        fprintf(stderr, "Error: Invalid read depth limit '%s'. Must be a non-negative integer.\n", argv[i]);
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      max_read_depth = (int)parsed_depth;
    } else if (!end_of_options && strcmp(argv[i], "--json-output") == 0) {
      json_output = 1;
    } else if (!end_of_options && strcmp(argv[i], "--stats") == 0) {
      show_stats = 1;
    } else if (!end_of_options && strcmp(argv[i], "--memory-pool-size") == 0) {
      char *endptr;
      long size_mb;
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --memory-pool-size requires a value in MB\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      i++;
      size_mb = strtol(argv[i], &endptr, 10);
      if (*endptr != '\0' || size_mb <= 0) {
        fprintf(stderr, "Error: Invalid memory pool size '%s'. Must be a positive integer in MB.\n", argv[i]);
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      memory_pool_size = (size_t)size_mb * 1024 * 1024;
    } else if (!end_of_options &&
               (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0)) {
      print_version();
      free(eval_source);
      free(module_paths);
      return 0;
    } else if (!end_of_options &&
               (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)) {
      print_usage(argv[0]);
      free(eval_source);
      free(module_paths);
      return 0;
    } else if (!end_of_options && argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      free(eval_source);
      free(module_paths);
      return 64;
    } else if (strcmp(argv[i], "-") == 0) {
      if (filename != NULL || eval_source != NULL || read_stdin) {
        fprintf(stderr, "Multiple input sources specified.\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      read_stdin = 1;
    } else {
      if (filename != NULL || eval_source != NULL || read_stdin) {
        fprintf(stderr, "Multiple input sources specified.\n");
        print_usage(argv[0]);
        free(eval_source);
        free(module_paths);
        return 64;
      }
      filename = argv[i];
    }
  }

  mem = malloc(memory_pool_size);
  if (!mem) {
    fprintf(stderr, "Failed to allocate %llu bytes for interpreter.\n",
            (unsigned long long)memory_pool_size);
    free(eval_source);
    free(module_paths);
    return 1;
  }

  ctx = fe_open(mem, memory_pool_size);
  if (!ctx) {
    fprintf(stderr, "Failed to initialize interpreter context.\n");
    free(mem);
    free(eval_source);
    free(module_paths);
    return 1;
  }

  config = FEX_CONFIG_NONE;
  if (enable_spans) {
    config |= FEX_CONFIG_ENABLE_SPANS;
  }
  fex_init_with_builtins(ctx, config, builtins);
  fe_set_step_limit(ctx, max_steps);
  fe_set_memory_limit(ctx, max_memory);
  fe_set_timeout_ms(ctx, timeout_ms);
  if (max_eval_depth >= 0) fe_set_eval_depth_limit(ctx, max_eval_depth);
  if (max_read_depth >= 0) fe_set_read_depth_limit(ctx, max_read_depth);

  for (i = 0; i < module_path_count; i++) {
    if (!fex_add_import_path(ctx, module_paths[i])) {
      fprintf(stderr, "Failed to add module path \"%s\".\n", module_paths[i]);
      free(eval_source);
      free(module_paths);
      fe_close(ctx);
      free(mem);
      return 1;
    }
  }
  free(module_paths);

  stdin_interactive = stdin_is_interactive();
  {
    FexError run_error;
    int has_error = 0;
    int is_batch;
    fex_error_clear(&run_error);

    if (eval_source != NULL) {
      exit_code = run_source(ctx, eval_source, "<expr>", json_output, &run_error);
      has_error = (exit_code != 0);
    } else if (filename != NULL) {
      exit_code = run_file(ctx, filename, json_output, &run_error);
      has_error = (exit_code != 0);
    } else if (read_stdin || !stdin_interactive) {
      char *stdin_source = NULL;
      if (!read_stream_source(stdin, &stdin_source)) {
        if (!json_output) {
          fprintf(stderr, "I/O error: could not read stdin\n");
        }
        exit_code = 74;
        run_error.status = FEX_STATUS_IO_ERROR;
        snprintf(run_error.message, sizeof(run_error.message),
                 "could not read stdin");
        has_error = 1;
      } else {
        exit_code = run_source(ctx, stdin_source, "<stdin>", json_output, &run_error);
        has_error = (exit_code != 0);
        free(stdin_source);
      }
    } else {
      run_repl(ctx);
    }

    is_batch = (eval_source != NULL || filename != NULL || read_stdin || !stdin_interactive);

    if (json_output && is_batch) {
      print_json_result(stderr, exit_code, has_error ? &run_error : NULL,
                        ctx, show_stats);
    } else if (show_stats && is_batch) {
      print_runtime_stats(stderr, ctx);
    }
  }

  free(eval_source);
  fe_close(ctx);
  free(mem);
  return exit_code;
}

#ifdef _WIN32
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  return windows_main_utf8_from_command_line();
}
#else
int main(int argc, char **argv) {
  return fex_main_utf8(argc, argv);
}
#endif
