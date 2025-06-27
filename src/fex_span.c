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

#define BUCKETS 8192                  /* open-addressing hash table         */
static FexSpan *table[BUCKETS];

#define hash_ptr(p) (((uintptr_t)((p)) >> 3) & (BUCKETS - 1))

void fex_record_span(const fe_Object *node, const char *src,
                     int sl,int sc,int el,int ec)
{
    unsigned h = hash_ptr(node);
    FexSpan *e = malloc(sizeof *e);
    *e = (FexSpan){ node, src, sl, sc, el, ec, table[h] };
    table[h] = e;
}

const FexSpan *fex_lookup_span(const fe_Object *node)
{
    FexSpan *e;
    for (e = table[hash_ptr(node)]; e; e = e->next)
        if (e->node == node) return e;
    return NULL;
}
