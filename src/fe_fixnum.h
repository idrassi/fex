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

#include <stdint.h>

/* Forward declaration */
typedef struct fe_Object fe_Object;
typedef double fe_Number;

/* 1 = immediate, rest of the word = signed integer shifted left by 1.  */
#define FE_IS_FIXNUM(x)    (((uintptr_t)(x) & 1u) != 0)
#define FE_FIXNUM(n)       ((fe_Object*)(((intptr_t)(n) << 1) | 1))
#define FE_UNBOX_FIXNUM(x) ((intptr_t)(x) >> 1)

/* Helper that works for *either* boxed double or immediate fixnum         */
static inline fe_Number fe_num_value(fe_Object *o)
{
    if (FE_IS_FIXNUM(o)) {
        return (fe_Number)FE_UNBOX_FIXNUM(o);
    } else {
        /* Access the cdr.n field - we know the structure layout */
        typedef union { fe_Object *o; void *f; fe_Number n; char c; } Value;
        struct fe_Object_internal { Value car, cdr; };
        return ((struct fe_Object_internal*)o)->cdr.n;
    }
}
