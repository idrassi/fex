/*
** SFC32 - Chris Doty-Humphrey's Small Fast Chaotic (SFC) PRNG
**
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

#ifndef SFC32_H_
#define SFC32_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t a, b, c, d;   /* internal state (128 bits plus 32-bit counter) */
} sfc32_state;

/* Seed with a single 32-bit value.
   Guarantees a != b != c, d starts at 1, and runs 12 warm-up rounds. */
void sfc32_seed(sfc32_state *s, uint32_t seed);

/* Seed directly with four 32-bit words (advanced use only). */
void sfc32_seed4(sfc32_state *s,
                 uint32_t a, uint32_t b, uint32_t c, uint32_t d);

/* Next 32 random bits (uniform on [0, 2^32 − 1]). */
uint32_t sfc32_next(sfc32_state *s);

#ifdef __cplusplus
}
#endif
#endif /* SFC32_H_ */
