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
#include "fex_internal.h"

#include <stdlib.h>
#include <string.h>

#define BUCKETS 8192

typedef struct FexSourceName {
    char *text;
    struct FexSourceName *next;
} FexSourceName;

typedef struct {
    FexSpan *buckets[BUCKETS];
    FexSourceName *source_names;
    int enabled;
} FexSpanState;

#define hash_ptr(p) (((uintptr_t)(p) >> 3) & (BUCKETS - 1))

static FexSpanState *get_state(fe_Context *ctx) {
    return (FexSpanState *)fe_ctx_span_state(ctx);
}

static void *span_alloc(fe_Context *ctx, size_t size) {
    return fe_ctx_tracked_alloc(ctx, size);
}

static void span_free(fe_Context *ctx, void *ptr) {
    fe_ctx_tracked_free(ctx, ptr);
}

static const char *intern_source_name(fe_Context *ctx, FexSpanState *state, const char *source_name) {
    FexSourceName *entry;
    char *copy;
    size_t len;

    if (!source_name || !*source_name) {
        return "<string>";
    }

    for (entry = state->source_names; entry; entry = entry->next) {
        if (strcmp(entry->text, source_name) == 0) {
            return entry->text;
        }
    }

    len = strlen(source_name);
    copy = span_alloc(ctx, len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, source_name, len + 1);

    entry = span_alloc(ctx, sizeof(*entry));
    if (!entry) {
        span_free(ctx, copy);
        return NULL;
    }

    entry->text = copy;
    entry->next = state->source_names;
    state->source_names = entry;
    return entry->text;
}

static char *copy_span_excerpt(fe_Context *ctx, const char *start, const char *end) {
    char *copy;
    size_t len;

    if (!start || !end || end < start) {
        return NULL;
    }

    len = (size_t)(end - start);
    copy = span_alloc(ctx, len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

void fex_span_init(fe_Context *ctx) {
    FexSpanState *state = span_alloc(ctx, sizeof(*state));
    if (state) {
        memset(state, 0, sizeof(*state));
        fe_ctx_set_span_state(ctx, state);
    }
}

void fex_span_cleanup(fe_Context *ctx) {
    FexSpanState *state = get_state(ctx);
    unsigned i;
    FexSourceName *name_entry;

    if (!state) return;

    /* Free all span entries */
    for (i = 0; i < BUCKETS; i++) {
        FexSpan *entry = state->buckets[i];
        while (entry) {
            FexSpan *next = entry->next;
            if (entry->source) {
                span_free(ctx, (void*)entry->source);
            }
            span_free(ctx, entry);
            entry = next;
        }
    }

    /* Free all interned source names */
    name_entry = state->source_names;
    while (name_entry) {
        FexSourceName *next = name_entry->next;
        span_free(ctx, name_entry->text);
        span_free(ctx, name_entry);
        name_entry = next;
    }

    span_free(ctx, state);
    fe_ctx_set_span_state(ctx, NULL);
}

void fex_span_prune(fe_Context *ctx) {
    FexSpanState *state = get_state(ctx);
    unsigned i;

    if (!state) return;

    for (i = 0; i < BUCKETS; i++) {
        FexSpan **link = &state->buckets[i];
        while (*link) {
            FexSpan *entry = *link;
            if (!fe_ctx_object_is_live(ctx, entry->node)) {
                *link = entry->next;
                if (entry->source) {
                    span_free(ctx, (void*)entry->source);
                }
                span_free(ctx, entry);
            } else {
                link = &entry->next;
            }
        }
    }
}

void fex_span_set_enabled(fe_Context *ctx, int enabled) {
    FexSpanState *state = get_state(ctx);
    if (enabled && !state) {
        fex_span_init(ctx);
        state = get_state(ctx);
        if (!state) {
            fe_ctx_memory_error(ctx, "out of memory (span state)");
            return;
        }
    }
    if (state) {
        state->enabled = enabled;
    }
}

int fex_span_is_enabled(fe_Context *ctx) {
    FexSpanState *state = get_state(ctx);
    return state ? state->enabled : 0;
}

void fex_record_span(fe_Context *ctx, const fe_Object *node, const char *src,
                     const char *source_name,
                     int sl, int sc, int el, int ec,
                     const char *start, const char *end) {
    FexSpanState *state = get_state(ctx);
    unsigned h;
    FexSpan *entry;
    const char *stable_source_name;
    char *excerpt = NULL;
    size_t excerpt_len = 0;

    if (!state || !state->enabled) {
        return;
    }
    (void)src;

    if (start && end && end >= start) {
        excerpt_len = (size_t)(end - start);
        excerpt = copy_span_excerpt(ctx, start, end);
        if (!excerpt) {
            fe_ctx_memory_error(ctx, "out of memory (span excerpt)");
            return;
        }
    }

    h = hash_ptr(node);
    entry = span_alloc(ctx, sizeof(*entry));
    if (!entry) {
        if (excerpt) {
            span_free(ctx, excerpt);
        }
        fe_ctx_memory_error(ctx, "out of memory (span entry)");
        return;
    }

    stable_source_name = intern_source_name(ctx, state, source_name);
    if (!stable_source_name) {
        if (excerpt) {
            span_free(ctx, excerpt);
        }
        span_free(ctx, entry);
        fe_ctx_memory_error(ctx, "out of memory (span source name)");
        return;
    }

    entry->node = node;
    entry->source = excerpt;
    entry->source_name = stable_source_name;
    entry->start = excerpt;
    entry->end = excerpt ? excerpt + excerpt_len : NULL;
    entry->start_line = sl;
    entry->start_col = sc;
    entry->end_line = el;
    entry->end_col = ec;
    entry->next = state->buckets[h];
    state->buckets[h] = entry;
}

const FexSpan *fex_lookup_span(fe_Context *ctx, const fe_Object *node) {
    FexSpanState *state = get_state(ctx);
    FexSpan *entry;

    if (!state || !state->enabled) {
        return NULL;
    }

    for (entry = state->buckets[hash_ptr(node)]; entry; entry = entry->next) {
        if (entry->node == node) {
            return entry;
        }
    }
    return NULL;
}
