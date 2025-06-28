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

#include "sfc32.h"

/* ---------- 32-bit mixing function for seeding -------- */
/**
 * @brief Takes a 32-bit integer and returns a well-mixed, pseudo-random 32-bit integer.
 *
 * This function is used to derive the initial state of the SFC32 generator from a single
 * 32-bit seed. It uses constants and operations inspired by the finalizer of the
 * MurmurHash3 algorithm to ensure a thorough mixing of the input bits.
 *
 * @param x A pointer to a 32-bit integer state, which is advanced on each call.
 * @return A pseudo-random 32-bit integer.
 */
static uint32_t seed_mix32(uint32_t *x) {
    /* Advance state with the 32-bit golden ratio conjugate. */
    uint32_t z = (*x += 0x9e3779b9);
    /* Mix bits using multiplication and xor-shifts. */
    z = (z ^ (z >> 16)) * 0x85ebca6b;
    z = (z ^ (z >> 13)) * 0xc2b2ae35;
    return z ^ (z >> 16);
}

/* ---------- Core SFC32 step (8 instructions, no multiply) ---------- */
uint32_t sfc32_next(sfc32_state *s) {
    uint32_t t = s->a + s->b + s->d++;
    s->a = s->b ^ (s->b >> 9);
    s->b = s->c + (s->c << 3);
    s->c = (s->c << 21) | (s->c >> 11);
    return s->c += t;
}

/* ---------- Friendly seeding helpers ---------- */
void sfc32_seed4(sfc32_state *s,
                 uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    int i;
    /* The counter (d) must be non-zero to guarantee full period. */
    if (d == 0) d = 1;
    s->a = a; s->b = b; s->c = c; s->d = d;

    /* Warm-up: 12 rounds recommended by the author. */
    for (i = 0; i < 12; ++i) {
        sfc32_next(s);
    }
}

/*  Seeds the SFC32 generator from a single 32-bit integer */
void sfc32_seed(sfc32_state *s, uint32_t seed)
{
    uint32_t x = seed;
    uint32_t a = seed_mix32(&x);
    uint32_t b = seed_mix32(&x);
    uint32_t c = seed_mix32(&x);

    /* Counter starts at 1 to avoid the all-zero state. */
    sfc32_seed4(s, a, b, c, 1u);
}
