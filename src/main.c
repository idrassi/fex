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

#define MEMORY_POOL_SIZE (1024 * 1024) /* 1MB */
#define REPL_BUFFER_SIZE 1024

static jmp_buf toplevel;
static fe_Context *g_ctx = NULL;

/* Custom error handler to jump back to the REPL loop */
static void on_error(fe_Context *ctx, const char *msg, fe_Object *cl) {
  (void)ctx; (void)cl;
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

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }
    
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

static void run_file(const char* path) {
  char* source = read_file(path);
  fe_Object *result = fex_do_string(g_ctx, source);
  free(source);

  /* fe_error will exit on its own if an error occurs outside the REPL */
  if (result == NULL) {
      exit(65); /* Compilation error */
  }
}

static void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options] [file]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --spans    Enable detailed error reporting with source spans\n");
    fprintf(stderr, "  --help     Show this help message\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "If no file is provided, starts the interactive REPL.\n");
}

int main(int argc, char **argv) {
  int enable_spans = 0, i;
  const char* filename = NULL;
  
  /* Parse command line arguments */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--spans") == 0) {
      enable_spans = 1;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return 64;
    } else {
      if (filename != NULL) {
        fprintf(stderr, "Multiple input files specified.\n");
        print_usage(argv[0]);
        return 64;
      }
      filename = argv[i];
    }
  }
  
  /* Allocate memory pool for the fe context */
  void* mem = malloc(MEMORY_POOL_SIZE);
  if (!mem) {
    fprintf(stderr, "Failed to allocate memory for interpreter.\n");
    return 1;
  }
  
  g_ctx = fe_open(mem, MEMORY_POOL_SIZE);

  /* Initialize our custom environment with conditional span support */
  FexConfig config = enable_spans ? FEX_CONFIG_ENABLE_SPANS : FEX_CONFIG_NONE;
  fex_init_with_config(g_ctx, config);
  
  if (filename == NULL) {
    run_repl();
  } else {
    run_file(filename);
  }

  fe_close(g_ctx);
  free(mem);
  return 0;
}
