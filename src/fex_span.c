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

#include "fex_span.h"

#include <stdlib.h>
#include <string.h>

#define BUCKETS 8192

typedef struct FexSourceName {
    char *text;
    struct FexSourceName *next;
} FexSourceName;

static FexSpan *table[BUCKETS];
static FexSourceName *source_names = NULL;
static int spans_enabled = 0;

#define hash_ptr(p) (((uintptr_t)(p) >> 3) & (BUCKETS - 1))

static const char *intern_source_name(const char *source_name) {
    FexSourceName *entry;
    char *copy;
    size_t len;

    if (!source_name || !*source_name) {
        return "<string>";
    }

    for (entry = source_names; entry; entry = entry->next) {
        if (strcmp(entry->text, source_name) == 0) {
            return entry->text;
        }
    }

    len = strlen(source_name);
    copy = malloc(len + 1);
    if (!copy) {
        return "<string>";
    }
    memcpy(copy, source_name, len + 1);

    entry = malloc(sizeof(*entry));
    if (!entry) {
        free(copy);
        return "<string>";
    }

    entry->text = copy;
    entry->next = source_names;
    source_names = entry;
    return entry->text;
}

void fex_span_set_enabled(int enabled) {
    spans_enabled = enabled;
}

int fex_span_is_enabled(void) {
    return spans_enabled;
}

void fex_record_span(const fe_Object *node, const char *src,
                     const char *source_name,
                     int sl, int sc, int el, int ec) {
    unsigned h;
    FexSpan *entry;

    if (!spans_enabled) {
        return;
    }

    h = hash_ptr(node);
    entry = malloc(sizeof(*entry));
    if (!entry) {
        return;
    }

    entry->node = node;
    entry->source = src;
    entry->source_name = intern_source_name(source_name);
    entry->start_line = sl;
    entry->start_col = sc;
    entry->end_line = el;
    entry->end_col = ec;
    entry->next = table[h];
    table[h] = entry;
}

const FexSpan *fex_lookup_span(const fe_Object *node) {
    FexSpan *entry;

    if (!spans_enabled) {
        return NULL;
    }

    for (entry = table[hash_ptr(node)]; entry; entry = entry->next) {
        if (entry->node == node) {
            return entry;
        }
    }
    return NULL;
}
