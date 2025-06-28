/*
** Copyright (c) 2020 rxi
** Copyright (c) 2025 Mounir IDRASSI <mounir.idrassi@amcrypto.jp>
**
** This library is free software; you can redistribute it and/or modify it
** under the terms of the MIT license. See `fe.c` for details.
*/

#ifndef FE_H
#define FE_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

/* --- Compile-time Options --- */
/* By default, define to enable slab-allocated strings, eliminating malloc for string data. */
#define FE_OPT_NO_MALLOC_STRINGS

#define FE_VERSION "1.0"

typedef double fe_Number;
typedef struct fe_Object fe_Object;
typedef struct fe_Context fe_Context;
typedef fe_Object* (*fe_CFunc)(fe_Context *ctx, fe_Object *args);
typedef void (*fe_ErrorFn)(fe_Context *ctx, const char *err, fe_Object *cl);
typedef void (*fe_WriteFn)(fe_Context *ctx, void *udata, char chr);
typedef char (*fe_ReadFn)(fe_Context *ctx, void *udata);
typedef struct { fe_ErrorFn error; fe_CFunc mark, gc; } fe_Handlers;

/**********************************
 *  Immediate representations
 **********************************/
/* Fixnum:  ...xxxxxxx1   (LSB = 1)  */
#define FE_IS_FIXNUM(x)    (((uintptr_t)(x) & 1u) != 0)
#define FE_FIXNUM(n)       ((fe_Object*)(((intptr_t)(n) << 1) | 1))
#define FE_UNBOX_FIXNUM(x) ((intptr_t)(x) >> 1)
#define fe_fixnum(n)       FE_FIXNUM(n)          /* public alias – convenience */

/* String data access. Under FE_OPT_NO_MALLOC_STRINGS, string data is in a separate
 * arena and requires the context to resolve an offset to a pointer. */
#ifndef FE_OPT_NO_MALLOC_STRINGS
#define FE_STR_DATA(ctx, o)  ((o)->cdr.s)
#endif

#define FE_STR_LEN(obj)    (FE_UNBOX_FIXNUM((obj)->car.o))
#define FE_IS_STRING(o)    (!FE_IS_FIXNUM(o) && ((o)->flags>>2)==FE_TSTRING)


/* Boolean: ...xxx0010 -> false, ...xxx0110 -> true  */
#define FE_FALSE           ((fe_Object*)0x02)
#define FE_TRUE            ((fe_Object*)0x06)
#define FE_IS_BOOLEAN(o)   (((uintptr_t)(o) & 0x03u) == 0x02u)
#define FE_IS_FALSE(o)     ((o) == FE_FALSE)
#define FE_IS_TRUE(o)      ((o) == FE_TRUE)

/* Helper that works for *either* boxed double or immediate fixnum         */
fe_Number fe_num_value(fe_Object *o);

enum {
  FE_TPAIR, FE_TFREE, FE_TNIL, FE_TNUMBER, FE_TSYMBOL, FE_TSTRING,
  FE_TFUNC, FE_TMACRO, FE_TPRIM, FE_TCFUNC, FE_TPTR,
  FE_TBOOLEAN
};

fe_Context* fe_open(void *ptr, int size);
void fe_close(fe_Context *ctx);
fe_Handlers* fe_handlers(fe_Context *ctx);
void fe_error(fe_Context *ctx, const char *msg);
fe_Object* fe_nextarg(fe_Context *ctx, fe_Object **arg);
int fe_type(fe_Context *ctx, fe_Object *obj);
int fe_isnil(fe_Context *ctx, fe_Object *obj);
void fe_pushgc(fe_Context *ctx, fe_Object *obj);
void fe_restoregc(fe_Context *ctx, int idx);
int fe_savegc(fe_Context *ctx);
void fe_mark(fe_Context *ctx, fe_Object *obj);
fe_Object* fe_cons(fe_Context *ctx, fe_Object *car, fe_Object *cdr);
fe_Object* fe_bool(fe_Context *ctx, int b);
fe_Object* fe_nil(fe_Context *ctx);
fe_Object* fe_number(fe_Context *ctx, fe_Number n);
fe_Object *fe_make_number(fe_Context *ctx, fe_Number v); /* automatic fixnum or boxed double */
fe_Object* fe_string(fe_Context *ctx, const char *str);
fe_Object* fe_symbol(fe_Context *ctx, const char *name);
fe_Object* fe_cfunc(fe_Context *ctx, fe_CFunc fn);
fe_Object* fe_ptr(fe_Context *ctx, void *ptr);
fe_Object* fe_list(fe_Context *ctx, fe_Object **objs, int n);
fe_Object* fe_car(fe_Context *ctx, fe_Object *obj);
fe_Object* fe_cdr(fe_Context *ctx, fe_Object *obj);
fe_Object** fe_cdr_ptr(fe_Context *ctx, fe_Object *obj);
void fe_write(fe_Context *ctx, fe_Object *obj, fe_WriteFn fn, void *udata, int qt); /* qt: print with string quotes on/off */
void fe_writefp(fe_Context *ctx, fe_Object *obj, FILE *fp);
int fe_tostring(fe_Context *ctx, fe_Object *obj, char *dst, int size);
fe_Number fe_tonumber(fe_Context *ctx, fe_Object *obj);
void* fe_toptr(fe_Context *ctx, fe_Object *obj);
void fe_set(fe_Context *ctx, fe_Object *sym, fe_Object *v);
fe_Object* fe_read(fe_Context *ctx, fe_ReadFn fn, void *udata);
fe_Object* fe_readfp(fe_Context *ctx, FILE *fp);
fe_Object* fe_eval(fe_Context *ctx, fe_Object *obj);

#endif
