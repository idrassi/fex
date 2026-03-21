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

#ifndef FEX_SPAN_H
#define FEX_SPAN_H

#include "fe.h"

typedef struct FexSpan {
    const fe_Object *node;
    /* Owned excerpt buffer when source text is available; NULL otherwise. */
    const char *source;
    const char *source_name;
    /* Slice into `source` when an excerpt is available; NULL otherwise. */
    const char *start;
    const char *end;
    int start_line, start_col;
    int end_line, end_col;
    struct FexSpan *next;
} FexSpan;

void fex_record_span(fe_Context *ctx, const fe_Object *node,
                     const char *src,
                     const char *source_name,
                     int sline, int scol, int eline, int ecol,
                     const char *start, const char *end);

const FexSpan *fex_lookup_span(fe_Context *ctx, const fe_Object *node);

void fex_span_set_enabled(fe_Context *ctx, int enabled);
int fex_span_is_enabled(fe_Context *ctx);

#endif
