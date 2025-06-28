/*
** Copyright (c) 2020 rxi
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

/* Fe Core Language */


#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <string.h>
#include "fe.h"

#define unused(x)     ( (void) (x) )
#define car(x)        ( (x)->car.o )
#define cdr(x)        ( (x)->cdr.o )
#define tag(x)        ( (x)->flags )
#define isnil(x)      ( (x) == &nil )
#define type(x) \
   ( FE_IS_FIXNUM(x)              ? FE_TNUMBER :          \
     FE_IS_BOOLEAN(x)             ? FE_TBOOLEAN :         \
     ((tag(x) & 1)                ? tag(x) >> 2 : FE_TPAIR) )
#define settype(x,t)  ( tag(x) = (t) << 2 | 1 )
#define number(x)     ( (x)->cdr.n )
#define prim(x)       ( (x)->cdr.c )
#define cfunc(x)      ( (x)->cdr.f )
#define nval(o)       fe_num_value(o)

#define GCMARKBIT     ( 0x2 )
#define GCSTACKSIZE   ( 1024 )

/* --- GC constants --- */
#define GC_GROWTH_FACTOR 2
#define GC_INITIAL_DIVISOR 4
#define GC_MIN_THRESHOLD 1024


enum {
 P_LET, P_SET, P_IF, P_FN, P_MAC, P_WHILE,
 P_RETURN, P_MODULE, P_EXPORT, P_IMPORT, P_GET,
 P_QUOTE, P_AND, P_OR, P_DO, P_CONS,
 P_CAR, P_CDR, P_SETCAR, P_SETCDR, P_LIST, P_NOT, P_IS, P_ATOM, P_PRINT, P_LT,
 P_LTE, P_ADD, P_SUB, P_MUL, P_DIV, P_MAX
};

static const char *primnames[] = {
  "let", "=", "if", "fn", "mac", "while", "return",
  "module", "export", "import", "get",
  "quote", "and", "or", "do", "cons",
  "car", "cdr", "setcar", "setcdr", "list", "not", "is", "atom", "print", "<",
  "<=", "+", "-", "*", "/"
};

static const char *typenames[] = {
  "pair", "free", "nil", "number", "symbol", "string",
  "func", "macro", "prim", "cfunc", "ptr",
  "boolean"
};

typedef union { fe_Object *o; fe_CFunc f; fe_Number n; char c;  char *s;} Value;

struct fe_Object {
  Value car, cdr;
  uint8_t flags;                 /* holds GC-mark + type tag   */
};

struct fe_Context {
  fe_Handlers handlers;
  fe_Object *gcstack[GCSTACKSIZE];
  int gcstack_idx;
  fe_Object *objects;
  int object_count;
  fe_Object *calllist;
  fe_Object *freelist;
  fe_Object *modulestack;
  fe_Object *symlist;
  fe_Object *t;
  int nextchr;
  /* --- GC fields --- */
  int live_count;          /* Objects surviving last GC */
  int allocs_since_gc;     /* Objects allocated since last GC */
  int gc_threshold;        /* Trigger next GC when allocs_since_gc exceeds this */
};

static fe_Object nil = {{ NULL }, { NULL }, (FE_TNIL << 2 | 1)};

static fe_Object *return_sym = NULL;
static fe_Object *frame_sym = NULL; /* Tag for new environment frames */
/* Symbols for static analysis */
static fe_Object *do_sym = NULL;
static fe_Object *let_sym = NULL;
static fe_Object *quote_sym = NULL;
static fe_Object *fn_sym = NULL;
static fe_Object *mac_sym = NULL;

static int list_has(fe_Object *list, fe_Object *item) {
  for (; !isnil(list); list = cdr(list)) {
    if (car(list) == item) { return 1; }
  }
  return 0;
}

/* Truth test that all new code should use                               */
static int fe_truthy(fe_Object *o) {
    return !FE_IS_FALSE(o) && !isnil(o);
} 

static void analyze(fe_Context *ctx, fe_Object *node, fe_Object *bound, fe_Object **free_vars) {
  /* Base case: atom. If it's a symbol not in our bound list, it's free. */
  if (type(node) != FE_TPAIR) {
    if (type(node) == FE_TSYMBOL && !list_has(bound, node) && !list_has(*free_vars, node)) {
      *free_vars = fe_cons(ctx, node, *free_vars);
    }
    return;
  }

  fe_Object *op = car(node);
  fe_Object *args = cdr(node);
  fe_Object *p;

  /* Special form handling */
  if (op == quote_sym) {
    return; /* Don't analyze contents of a quote */
  }

  if (op == do_sym) {
    fe_Object *local_bound = bound;
    int gc = fe_savegc(ctx);
    fe_pushgc(ctx, local_bound);

    for (p = args; !isnil(p); p = cdr(p)) {
      fe_Object *stmt = car(p);
      /* Check for (let var expr) */
      if (type(stmt) == FE_TPAIR && car(stmt) == let_sym) {
        fe_Object *let_args = cdr(stmt);
        fe_Object *var = car(let_args);
        fe_Object *expr = car(cdr(let_args));

        /* Analyze the expression in the current environment */
        analyze(ctx, expr, local_bound, free_vars);

        /* Add the new variable to the local bound list for subsequent statements */
        local_bound = fe_cons(ctx, var, local_bound);
        fe_restoregc(ctx, gc);
        fe_pushgc(ctx, local_bound);
      } else {
        analyze(ctx, stmt, local_bound, free_vars);
      }
    }
    fe_restoregc(ctx, gc);
    return;
  }
  
  if (op == fn_sym || op == mac_sym) {
    fe_Object *params = car(args);
    fe_Object *body = car(cdr(args));
    fe_Object *inner_free;
    int gc = fe_savegc(ctx);

    /* The inner function's bound variables start with its own parameters. */
    fe_Object *inner_bound = &nil;
    fe_pushgc(ctx, inner_bound);
    for (p = params; !isnil(p); p = cdr(p)) {
      inner_bound = fe_cons(ctx, car(p), inner_bound);
    }

    inner_free = &nil;
    fe_pushgc(ctx, inner_free);
    analyze(ctx, body, inner_bound, &inner_free);
    fe_restoregc(ctx, gc);

    /* The inner function's free variables must be resolved in the outer scope.
       We treat them as expressions to be analyzed in the outer 'bound' list. */
    fe_pushgc(ctx, inner_free);
    for (p = inner_free; !isnil(p); p = cdr(p)) {
      analyze(ctx, car(p), bound, free_vars);
    }
    fe_restoregc(ctx, gc);
    return;
  }

  /* Generic case: treat as a function call. Analyze the operator and all arguments. */
  analyze(ctx, op, bound, free_vars);
  for (p = args; !isnil(p); p = cdr(p)) {
    if (type(p) == FE_TPAIR) {
        analyze(ctx, car(p), bound, free_vars);
    } else { /* Dotted pair in arguments */
        analyze(ctx, p, bound, free_vars);
        break;
    }
  }
}

fe_Handlers* fe_handlers(fe_Context *ctx) {
  return &ctx->handlers;
}


void fe_error(fe_Context *ctx, const char *msg) {
  fe_Object *cl = ctx->calllist;
  /* reset context state */
  ctx->calllist = &nil;
  /* do error handler */
  if (ctx->handlers.error) { ctx->handlers.error(ctx, msg, cl); }
  /* error handler returned -- print error and traceback, exit */
  fprintf(stderr, "error: %s\n", msg);
  for (; !isnil(cl); cl = cdr(cl)) {
    char buf[64];
    fe_tostring(ctx, car(cl), buf, sizeof(buf));
    fprintf(stderr, "=> %s\n", buf);
  }
  exit(EXIT_FAILURE);
}


fe_Object* fe_nextarg(fe_Context *ctx, fe_Object **arg) {
  fe_Object *a = *arg;
  if (type(a) != FE_TPAIR) {
    if (isnil(a)) { fe_error(ctx, "too few arguments"); }
    fe_error(ctx, "dotted pair in argument list");
  }
  *arg = cdr(a);
  return car(a);
}


static fe_Object* checktype(fe_Context *ctx, fe_Object *obj, int type) {
  char buf[64];
  int actual_type = type(obj);
  
  /* Special case: allow fixnums when expecting numbers */
  if (type == FE_TNUMBER && FE_IS_FIXNUM(obj)) {
    return obj;
  }
  
  if (actual_type != type) {
    sprintf(buf, "expected %s, got %s", 
            typenames[type], 
            FE_IS_FIXNUM(obj) ? "number" : typenames[actual_type]);
    fe_error(ctx, buf);
  }
  return obj;
}

static fe_Object* checknum(fe_Context *ctx, fe_Object *obj)
{
    if (FE_IS_FIXNUM(obj)) return obj;           /* fine � immediate */
    return checktype(ctx, obj, FE_TNUMBER);      /* boxed double or error */
}

int fe_type(fe_Context *ctx, fe_Object *obj) {
  unused(ctx);
  return type(obj);
}

fe_Number fe_num_value(fe_Object *o)
{
    if (FE_IS_FIXNUM(o)) {
        return (fe_Number)FE_UNBOX_FIXNUM(o);
    } else {
        return (o)->cdr.n;
    }
}

fe_Object *fe_make_number(fe_Context *ctx, fe_Number v)
{
    /* 1. Is it an integer?  (Avoids FP rounding issues.)             */
    fe_Number iv = (fe_Number)(intptr_t)v;
    if (v == iv) {
        /* 2. Does it fit in the fixnum range for this word size?     */
        intptr_t i = (intptr_t)iv;
        intptr_t shr = i >> (8*sizeof(intptr_t)-2);   /* sign-extend  */
        if (shr == 0 || shr == -1) {                  /* fits */      
            return FE_FIXNUM(i);
        }
    }
    /* Fallback: allocate the boxed double. */
    return fe_number(ctx, v);
}


int fe_isnil(fe_Context *ctx, fe_Object *obj) {
  unused(ctx);
  return isnil(obj);
}


void fe_pushgc(fe_Context *ctx, fe_Object *obj) {
  /* Immediates never reach the GC root stack */
  if (FE_IS_FIXNUM(obj) || FE_IS_BOOLEAN(obj) || obj == &nil) {
    return;
  }
  
  if (ctx->gcstack_idx == GCSTACKSIZE) {
    fe_error(ctx, "gc stack overflow");
  }
  ctx->gcstack[ctx->gcstack_idx++] = obj;
}

void fe_restoregc(fe_Context *ctx, int idx) {
  ctx->gcstack_idx = idx;
}


int fe_savegc(fe_Context *ctx) {
  return ctx->gcstack_idx;
}


void fe_mark(fe_Context *ctx, fe_Object *obj) {
    /*  Tail-recursive mark without �goto', and with a *fresh* check
        every time we follow cdr(obj).  That way, if the cdr turned
        out to be an immediate fixnum we bail before dereferencing it. */

    for (;;) {
        /* 0. Fast exits for objects we never allocate. */
        if (FE_IS_FIXNUM(obj) || FE_IS_BOOLEAN(obj) || isnil(obj)) return;

        /* 1. Do not mark rogue pointers that don't belong to us. */
        if (obj < ctx->objects || obj >= ctx->objects + ctx->object_count)
            return;

        /* 2. Already marked?  Done. */
        if (tag(obj) & GCMARKBIT) return;
        tag(obj) |= GCMARKBIT;

        switch (type(obj)) {
        case FE_TPAIR:
            /* mark car, then continue with cdr in the next loop
               iteration (this removes deep recursion) */
            fe_mark(ctx, car(obj));
            obj = cdr(obj);
            continue;               /* re-check fixnum / nil next round */

        case FE_TFUNC:   /* (prototype . body) where prototype=(free_vars) */
        case FE_TMACRO:  /* (prototype . body) where prototype=(free_vars) */
        case FE_TSYMBOL: /* (name-string . value)   */
        case FE_TSTRING: /* chunk list              */
            obj = cdr(obj);
            continue;

        case FE_TPTR:
            if (ctx->handlers.mark) ctx->handlers.mark(ctx, obj);
            /* fall-through */
        default:
            return;                 /* nothing more to traverse */
        }
    }
}


static void collectgarbage(fe_Context *ctx) {
  int i;
  int live = 0; /* Counter for live objects */
  /* mark */
  for (i = 0; i < ctx->gcstack_idx; i++) {
    if (!FE_IS_FIXNUM(ctx->gcstack[i]) && ctx->gcstack[i] != &nil) {
      fe_mark(ctx, ctx->gcstack[i]);
    }
  }
  fe_mark(ctx, ctx->modulestack);
  fe_mark(ctx, ctx->symlist);
  /* sweep and unmark */
  for (i = 0; i < ctx->object_count; i++) {
    fe_Object *obj = &ctx->objects[i];
    if (type(obj) == FE_TFREE) { continue; }
    if (~tag(obj) & GCMARKBIT) {
      if (type(obj)==FE_TSTRING && FE_STR_DATA(obj)) {
          free(FE_STR_DATA(obj));
      }
      if (type(obj) == FE_TPTR && ctx->handlers.gc) {
        ctx->handlers.gc(ctx, obj);
      }
      settype(obj, FE_TFREE);
      cdr(obj) = ctx->freelist;
      ctx->freelist = obj;
    } else {
      tag(obj) &= ~GCMARKBIT;
      live++; /* This object is alive */
    }
  }

  /* --- Update GC state and threshold --- */
  ctx->live_count = live;
  ctx->allocs_since_gc = 0;
  ctx->gc_threshold = ctx->live_count * GC_GROWTH_FACTOR;
  if (ctx->gc_threshold < GC_MIN_THRESHOLD) {
    ctx->gc_threshold = GC_MIN_THRESHOLD;
  }
}

/* -------------------------------------------------------------------------
 * Early-return helper
 * ---------------------------------------------------------------------- */
static int is_return_obj(fe_Object *obj) {
  return type(obj) == FE_TPAIR && car(obj) == return_sym;
}

/* --------------------------------------------------------------------- */

static int equal(fe_Object *a, fe_Object *b) {
  if (a == b) { return 1; }
  if (type(a) != type(b)) { return 0; }
  if (type(a) == FE_TNUMBER) { return nval(a) == nval(b); }
  if (type(a) == FE_TSTRING) {
    return FE_STR_LEN(a)==FE_STR_LEN(b) &&
           memcmp(FE_STR_DATA(a), FE_STR_DATA(b), FE_STR_LEN(a))==0;
  }
  return 0;
}


static int streq(fe_Object *obj, const char *str) {
  return strcmp(FE_STR_DATA(obj), str)==0;
}


static fe_Object* object(fe_Context *ctx) {
  fe_Object *obj;

  /* --- GC trigger logic --- */
  /* Trigger GC if the allocation count exceeds the threshold,
   * or as a fallback if the freelist is empty. */
  if (ctx->allocs_since_gc >= ctx->gc_threshold || isnil(ctx->freelist)) {
    collectgarbage(ctx);
    if (isnil(ctx->freelist)) { fe_error(ctx, "out of memory"); }
  }

  /* get object from freelist and push to the gcstack */
  obj = ctx->freelist;
  ctx->freelist = cdr(obj);
  
  /* Increment allocation counter and push to GC stack for protection */
  ctx->allocs_since_gc++;
  fe_pushgc(ctx, obj);

  return obj;
}


fe_Object* fe_cons(fe_Context *ctx, fe_Object *car, fe_Object *cdr) {
  fe_Object *obj = object(ctx);
    obj->flags = 0;               /* <- essential:  �I am a pair�        */
    car(obj)  = car;
    cdr(obj)  = cdr;
  return obj;
}


fe_Object* fe_bool(fe_Context *ctx, int b) {
  (void)ctx;
  return b ? FE_TRUE : FE_FALSE;
}

fe_Object* fe_nil(fe_Context *ctx) {
  (void)ctx;
  return &nil; /* Return the static nil object */
}

fe_Object* fe_number(fe_Context *ctx, fe_Number n) {
  fe_Object *obj = object(ctx);
  settype(obj, FE_TNUMBER);
  number(obj) = n;
  return obj;
}

#define GROW_STEP 64

static fe_Object* make_string_obj(fe_Context *ctx,
                                  const char   *src,
                                  size_t        len)
{
    fe_Object *o = object(ctx);
    settype(o, FE_TSTRING);

    char *buf = malloc(len+1);
    if (!buf) fe_error(ctx, "out of memory (string)");
    memcpy(buf, src, len);
    buf[len]='\0';

    car(o) = FE_FIXNUM((intptr_t)len);
    FE_STR_DATA(o) = buf;
    return o;
}

fe_Object* fe_string(fe_Context *ctx, const char *str)
{
    return make_string_obj(ctx, str, strlen(str));
}


fe_Object* fe_symbol(fe_Context *ctx, const char *name) {
  fe_Object *obj;
  /* try to find in symlist */
  for (obj = ctx->symlist; !isnil(obj); obj = cdr(obj)) {
    if (streq(car(cdr(car(obj))), name)) {
      return car(obj);
    }
  }
  /* create new object, push to symlist and return */
  obj = object(ctx);
  settype(obj, FE_TSYMBOL);
  cdr(obj) = fe_cons(ctx, fe_string(ctx, name), &nil);
  ctx->symlist = fe_cons(ctx, obj, ctx->symlist);
  return obj;
}


fe_Object* fe_cfunc(fe_Context *ctx, fe_CFunc fn) {
  fe_Object *obj = object(ctx);
  settype(obj, FE_TCFUNC);
  cfunc(obj) = fn;
  return obj;
}


fe_Object* fe_ptr(fe_Context *ctx, void *ptr) {
  fe_Object *obj = object(ctx);
  settype(obj, FE_TPTR);
  cdr(obj) = ptr;
  return obj;
}


fe_Object* fe_list(fe_Context *ctx, fe_Object **objs, int n) {
  fe_Object *res = &nil;
  while (n--) {
    res = fe_cons(ctx, objs[n], res);
  }
  return res;
}


fe_Object* fe_car(fe_Context *ctx, fe_Object *obj) {
  if (isnil(obj)) { return obj; }
  return car(checktype(ctx, obj, FE_TPAIR));
}


fe_Object* fe_cdr(fe_Context *ctx, fe_Object *obj) {
  if (isnil(obj)) { return obj; }
  return cdr(checktype(ctx, obj, FE_TPAIR));
}

fe_Object** fe_cdr_ptr(fe_Context *ctx, fe_Object *obj) {
  if (isnil(obj)) { fe_error(ctx, "cannot get cdr pointer of nil"); }
  return &cdr(checktype(ctx, obj, FE_TPAIR));
}

static void writestr(fe_Context *ctx, fe_WriteFn fn, void *udata, const char *s) {
  while (*s) { fn(ctx, udata, *s++); }
}

void fe_write(fe_Context *ctx, fe_Object *obj, fe_WriteFn fn, void *udata, int qt) {
  char buf[32];

  switch (type(obj)) {
    case FE_TNIL:
      writestr(ctx, fn, udata, "nil");
      break;

    case FE_TBOOLEAN:
      writestr(ctx, fn, udata, (obj == FE_TRUE) ? "true" : "false");
      break;

    case FE_TNUMBER:
      if (FE_IS_FIXNUM(obj)) {
          sprintf(buf, "%lld", (intmax_t)FE_UNBOX_FIXNUM(obj));
      } else {
          sprintf(buf, "%.7g", number(obj));
      }
      writestr(ctx, fn, udata, buf);
      break;

    case FE_TPAIR:
      if (car(obj) == frame_sym) {
        writestr(ctx, fn, udata, "[env frame]");
        break;
      }

      fn(ctx, udata, '(');
      for (;;) {
        fe_write(ctx, car(obj), fn, udata, 1);
        obj = cdr(obj);
        if (type(obj) != FE_TPAIR) { break; }
        fn(ctx, udata, ' ');
      }
      if (!isnil(obj)) {
        writestr(ctx, fn, udata, " . ");
        fe_write(ctx, obj, fn, udata, 1);
      }
      fn(ctx, udata, ')');
      break;

    case FE_TSYMBOL:
      fe_write(ctx, car(cdr(obj)), fn, udata, 0);
      break;

    case FE_TSTRING:
      if (qt) fn(ctx, udata, '"');
      const char *p = FE_STR_DATA(obj);
      while (*p) {
          if (qt && *p=='"') fn(ctx, udata, '\\');
          fn(ctx, udata, *p++);
      }
      if (qt) fn(ctx, udata, '"');
      break;

    default:
      sprintf(buf, "[%s %p]", typenames[type(obj)], (void*) obj);
      writestr(ctx, fn, udata, buf);
      break;
  }
}


static void writefp(fe_Context *ctx, void *udata, char chr) {
  unused(ctx);
  fputc(chr, udata);
}

void fe_writefp(fe_Context *ctx, fe_Object *obj, FILE *fp) {
  fe_write(ctx, obj, writefp, fp, 0);
}


typedef struct { char *p; int n; } CharPtrInt;

static void writebuf(fe_Context *ctx, void *udata, char chr) {
  CharPtrInt *x = udata;
  unused(ctx);
  if (x->n) { *x->p++ = chr; x->n--; }
}

int fe_tostring(fe_Context *ctx, fe_Object *obj, char *dst, int size) {
  CharPtrInt x;
  x.p = dst;
  x.n = size - 1;
  fe_write(ctx, obj, writebuf, &x, 0);
  *x.p = '\0';
  return size - x.n - 1;
}


fe_Number fe_tonumber(fe_Context *ctx, fe_Object *obj) {
    unused(ctx);
    return nval(obj);      /* works for both representations */
}


void* fe_toptr(fe_Context *ctx, fe_Object *obj) {
  return cdr(checktype(ctx, obj, FE_TPTR));
}

static fe_Object* getbound(fe_Object *sym, fe_Object *env) {
  fe_Object *p;
  /* Check for new closure environment frame */
  if (type(env) == FE_TPAIR && car(env) == frame_sym) {
    fe_Object *locals = car(cdr(env));
    fe_Object *upvals = cdr(cdr(env));
    /* search locals */
    for (p = locals; !isnil(p); p = cdr(p)) {
      fe_Object *x = car(p);
      if (car(x) == sym) { return x; }
    }
    /* search upvalues */
    for (p = upvals; !isnil(p); p = cdr(p)) {
      fe_Object *x = car(p);
      if (car(x) == sym) { return x; }
    }
    /* not found in frame, fall through to globals */
  } else {
    /* try to find in old-style association list environment */
    for (; !isnil(env); env = cdr(env)) {
      fe_Object *x = car(env);
      if (car(x) == sym) { return x; }
    }
  }
  /* return global */
  return cdr(sym);
}


void fe_set(fe_Context *ctx, fe_Object *sym, fe_Object *v) {
  unused(ctx);
  cdr(getbound(sym, &nil)) = v;
}


static fe_Object rparen;

static fe_Object* read_(fe_Context *ctx, fe_ReadFn fn, void *udata) {
  const char *delimiter = " \n\t\r();";
  fe_Object *v, *res, **tail;
  fe_Number n;
  int chr, gc;
  char buf[64], *p;

  /* get next character */
  chr = ctx->nextchr ? ctx->nextchr : fn(ctx, udata);
  ctx->nextchr = '\0';

  /* skip whitespace */
  while (chr && strchr(" \n\t\r", chr)) {
    chr = fn(ctx, udata);
  }

  switch (chr) {
    case '\0':
      return NULL;

    case ';':
      while (chr && chr != '\n') { chr = fn(ctx, udata); }
      return read_(ctx, fn, udata);

    case ')':
      return &rparen;

    case '(':
      res = &nil;
      tail = &res;
      gc = fe_savegc(ctx);
      fe_pushgc(ctx, res); /* to cause error on too-deep nesting */
      while ( (v = read_(ctx, fn, udata)) != &rparen ) {
        if (v == NULL) { fe_error(ctx, "unclosed list"); }
        if (type(v) == FE_TSYMBOL && streq(car(cdr(v)), ".")) {
          /* dotted pair */
          *tail = fe_read(ctx, fn, udata);
        } else {
          /* proper pair */
          *tail = fe_cons(ctx, v, &nil);
          tail = &cdr(*tail);
        }
        fe_restoregc(ctx, gc);
        fe_pushgc(ctx, res);
      }
      return res;

    case '\'':
      v = fe_read(ctx, fn, udata);
      if (!v) { fe_error(ctx, "stray '''"); }
      return fe_cons(ctx, fe_symbol(ctx, "quote"), fe_cons(ctx, v, &nil));

    case '"':
      {
        size_t cap = GROW_STEP, len = 0;
        char *buf = malloc(cap);
        if (!buf) fe_error(ctx, "out of memory (string)");

        chr = fn(ctx, udata);
        while (chr!='"') {
          if (chr=='\0') fe_error(ctx, "unclosed string");
          if (chr=='\\') {
              chr = fn(ctx, udata);
              if (chr=='n') chr='\n';
              else if (chr=='r') chr='\r';
              else if (chr=='t') chr='\t';
          }
          if (len+1>=cap) { cap+=GROW_STEP; buf=realloc(buf,cap); }
          buf[len++]=chr;
          chr = fn(ctx, udata);
        }
        return make_string_obj(ctx, buf, len); /* duplicates & keeps */
      }

    default:
      p = buf;
      do {
        if (p == buf + sizeof(buf) - 1) { fe_error(ctx, "symbol too long"); }
        *p++ = chr;
        chr = fn(ctx, udata);
      } while (chr && !strchr(delimiter, chr));
      *p = '\0';
      ctx->nextchr = chr;
      n = strtod(buf, &p);  /* try to read as number */
      if (p != buf && strchr(delimiter, *p)) {
        return fe_make_number(ctx, n); 
      }
      if (!strcmp(buf, "nil"))   { return &nil;  }
      if (!strcmp(buf, "true"))  { return FE_TRUE;  }
      if (!strcmp(buf, "false")) { return FE_FALSE; }
      return fe_symbol(ctx, buf);
  }
}


fe_Object* fe_read(fe_Context *ctx, fe_ReadFn fn, void *udata) {
  fe_Object* obj = read_(ctx, fn, udata);
  if (obj == &rparen) { fe_error(ctx, "stray ')'"); }
  return obj;
}


static char readfp(fe_Context *ctx, void *udata) {
  int chr;
  unused(ctx);
  return (chr = fgetc(udata)) == EOF ? '\0' : chr;
}

fe_Object* fe_readfp(fe_Context *ctx, FILE *fp) {
  return fe_read(ctx, readfp, fp);
}


static fe_Object* eval(fe_Context *ctx, fe_Object *obj, fe_Object *env, fe_Object **newenv);

static fe_Object* evallist(fe_Context *ctx, fe_Object *lst, fe_Object *env) {
  fe_Object *res = &nil;
  fe_Object **tail = &res;
  while (!isnil(lst)) {
    *tail = fe_cons(ctx, eval(ctx, fe_nextarg(ctx, &lst), env, NULL), &nil);
    tail = &cdr(*tail);
  }
  return res;
}


static fe_Object* dolist(fe_Context *ctx, fe_Object *lst, fe_Object *env) {
  fe_Object *res = &nil;
  int save = fe_savegc(ctx);
  while (!isnil(lst)) {
    fe_restoregc(ctx, save);
    fe_pushgc(ctx, lst);
    fe_pushgc(ctx, env);
    res = eval(ctx, fe_nextarg(ctx, &lst), env, &env);
    if (is_return_obj(res)) { break; }
  }
  return res;
}


static fe_Object* argstoenv(fe_Context *ctx, fe_Object *prm, fe_Object *arg, fe_Object *env) {
  while (!isnil(prm)) {
    if (type(prm) != FE_TPAIR) {
      env = fe_cons(ctx, fe_cons(ctx, prm, arg), env);
      break;
    }
    env = fe_cons(ctx, fe_cons(ctx, car(prm), fe_car(ctx, arg)), env);
    prm = cdr(prm);
    arg = fe_cdr(ctx, arg);
  }
  return env;
}


#define evalarg() eval(ctx, fe_nextarg(ctx, &arg), env, NULL)

#define arithop(op) {                                     \
    fe_Number x = nval(checknum(ctx, evalarg()));         \
    while (!isnil(arg)) {                                 \
        x = x op nval(checknum(ctx, evalarg()));          \
    }                                                     \
    res = fe_make_number(ctx, x);                         \
}

#define numcmpop(op) {                                    \
    va  = checknum(ctx, evalarg());                       \
    vb  = checknum(ctx, evalarg());                       \
    res = fe_bool(ctx, nval(va) op nval(vb));             \
}


static fe_Object* eval(fe_Context *ctx, fe_Object *obj, fe_Object *env, fe_Object **newenv) {
  fe_Object *fn, *arg, *res;
  fe_Object cl, *va, *vb;
  int n, gc;

  if (type(obj) == FE_TSYMBOL) { return cdr(getbound(obj, env)); }
  if (type(obj) != FE_TPAIR) { return obj; }

  car(&cl) = obj, cdr(&cl) = ctx->calllist;
  ctx->calllist = &cl;

  gc = fe_savegc(ctx);
  fn = eval(ctx, car(obj), env, NULL);
  arg = cdr(obj);
  res = &nil;

  switch (type(fn)) {
    case FE_TPRIM:
      switch (prim(fn)) {
        case P_MODULE: {
          /* form: (module "name" body) */
          fe_Object *name_obj = evalarg();
          fe_Object *body = fe_nextarg(ctx, &arg);
          char name_buf[128];

          /* Create and push module's export table */
          fe_Object *exports = &nil;
          fe_pushgc(ctx, exports);
          ctx->modulestack = fe_cons(ctx, exports, ctx->modulestack);

          /* Evaluate module body */
          eval(ctx, body, env, &env);

          /* Pop module from stack and retrieve final exports table */
          exports = car(ctx->modulestack);
          ctx->modulestack = cdr(ctx->modulestack);

          /* Register module in global environment */
          checktype(ctx, name_obj, FE_TSTRING);
          fe_tostring(ctx, name_obj, name_buf, sizeof(name_buf));
          fe_set(ctx, fe_symbol(ctx, name_buf), exports);
          res = exports;
          break;
        }
        case P_EXPORT: {
          /* form: (export (let name value)) */
          if (isnil(ctx->modulestack)) fe_error(ctx, "export outside of module");

          fe_Object *decl = fe_nextarg(ctx, &arg);
          fe_Object *name_sym = fe_car(ctx, fe_cdr(ctx, decl));
          checktype(ctx, name_sym, FE_TSYMBOL);

          /* Evaluate declaration to bind it and get value */
          res = eval(ctx, decl, env, &env);

          /* Add to current module's export table */
          fe_Object *binding = fe_cons(ctx, name_sym, res);
          fe_Object *exports = fe_car(ctx, ctx->modulestack);
          exports = fe_cons(ctx, binding, exports);
          car(ctx->modulestack) = exports;
          break;
        }
        case P_IMPORT:
          /* form: (import name) - no-op for now */
          res = &nil;
          break;
        case P_GET: {
          /* form: (get object property) */
          va = evalarg(); /* The module object (or any table) */
          vb = fe_nextarg(ctx, &arg); /* The property symbol (not evaluated) */
          checktype(ctx, vb, FE_TSYMBOL);
          res = cdr(getbound(vb, va)); /* Re-use getbound for assoc list lookup */
          break;
        }
        case P_RETURN: {
            /* evaluate argument, defaulting to nil */
            va  = isnil(arg) ? &nil : evalarg();
            /* (__return__ . value)  � single pair keeps GC simple */
            res = fe_cons(ctx, return_sym, va);
            break;
        }
        case P_LET: {
          fe_Object *sym = checktype(ctx, fe_nextarg(ctx, &arg), FE_TSYMBOL);
          fe_Object *val_expr = fe_nextarg(ctx, &arg);
          fe_Object *val;

          if (newenv) {
            /* Implement `letrec` semantics for local bindings to allow recursion.
               A new binding is created with a nil placeholder, the value is evaluated
               in this new environment, and then the binding is updated. */
            fe_Object *binding = fe_cons(ctx, sym, &nil);

            fe_Object *new_frame_env;
            if (type(*newenv) == FE_TPAIR && car(*newenv) == frame_sym) {
                fe_Object *locals = car(cdr(*newenv));
                fe_Object *upvals = cdr(cdr(*newenv));
                fe_Object *new_locals = fe_cons(ctx, binding, locals);
                new_frame_env = fe_cons(ctx, new_locals, upvals);
                new_frame_env = fe_cons(ctx, frame_sym, new_frame_env);
            } else {
                new_frame_env = fe_cons(ctx, binding, *newenv);
            }
            *newenv = new_frame_env;

            val = eval(ctx, val_expr, *newenv, NULL);
            cdr(binding) = val;
          } else {
            /* This case handles top-level 'let' in the REPL.
               We'll make it a global binding. This simple approach lacks letrec
               semantics for the REPL, but is better than doing nothing. */
            val = eval(ctx, val_expr, env, NULL);
            fe_set(ctx, sym, val);
          }
          /* A `let` expression should evaluate to the assigned value. */
          res = val;
          break;
        }

        case P_SET:
          va = checktype(ctx, fe_nextarg(ctx, &arg), FE_TSYMBOL);
          cdr(getbound(va, env)) = evalarg();
          break;

        case P_IF:
          while (!isnil(arg)) {
            va = evalarg();
            if (fe_truthy(va)) {
              res = isnil(arg) ? va : evalarg();
              break;
            }
            if (isnil(arg)) { break; }
            arg = cdr(arg);
          }
          break;

        case P_FN: case P_MAC: {
          fe_Object *params = fe_nextarg(ctx, &arg);
          fe_Object *body = fe_car(ctx, arg);

          int s = fe_savegc(ctx);
          fe_Object *bound = &nil;
          fe_Object *p;
          fe_Object *free_vars;
          fe_pushgc(ctx, bound);
          for (p = params; !isnil(p); p = cdr(p)) {
              bound = fe_cons(ctx, car(p), bound);
          }

          free_vars = &nil;
          fe_pushgc(ctx, free_vars);
          analyze(ctx, body, bound, &free_vars);
          fe_restoregc(ctx, s);
          
          fe_pushgc(ctx, free_vars);
          fe_pushgc(ctx, params);
          fe_pushgc(ctx, body);
          fe_pushgc(ctx, env); /* Protect the definition-time environment */

          va = fe_cons(ctx, body, &nil);
          va = fe_cons(ctx, params, va);
          va = fe_cons(ctx, free_vars, va);
          va = fe_cons(ctx, env, va); /* Prepend definition env to the list */
          
          res = object(ctx);
          settype(res, prim(fn) == P_FN ? FE_TFUNC : FE_TMACRO);
          cdr(res) = va;
          break;
        }

        case P_WHILE:
          va = fe_nextarg(ctx, &arg);
          n = fe_savegc(ctx);
          while (fe_truthy(eval(ctx, va, env, NULL))) {
            dolist(ctx, arg, env);
            fe_restoregc(ctx, n);
          }
          break;

        case P_QUOTE:
          res = fe_nextarg(ctx, &arg);
          break;

        case P_AND:
          while (!isnil(arg) && fe_truthy(res = evalarg()));
          break;

        case P_OR:
          while (!isnil(arg) && !fe_truthy(res = evalarg()));
          break;

        case P_DO:
          res = dolist(ctx, arg, env);
          break;

        case P_CONS:
          va = evalarg();
          res = fe_cons(ctx, va, evalarg());
          break;

        case P_CAR:
          res = fe_car(ctx, evalarg());
          break;

        case P_CDR:
          res = fe_cdr(ctx, evalarg());
          break;

        case P_SETCAR:
          va = checktype(ctx, evalarg(), FE_TPAIR);
          car(va) = evalarg();
          break;

        case P_SETCDR:
          va = checktype(ctx, evalarg(), FE_TPAIR);
          cdr(va) = evalarg();
          break;

        case P_LIST:
          res = evallist(ctx, arg, env);
          break;

        case P_NOT:
          res = fe_bool(ctx, !fe_truthy(evalarg()));
          break;

        case P_IS:
          va = evalarg();
          res = fe_bool(ctx, equal(va, evalarg()));
          break;

        case P_ATOM:
          res = fe_bool(ctx, fe_type(ctx, evalarg()) != FE_TPAIR);
          break;

        case P_PRINT:
          while (!isnil(arg)) {
            fe_writefp(ctx, evalarg(), stdout);
            if (!isnil(arg)) { printf(" "); }
          }
          printf("\n");
          break;

        case P_LT: numcmpop(<); break;
        case P_LTE: numcmpop(<=); break;
        case P_ADD: arithop(+); break;
        case P_SUB:
          /* --------  subtraction / unary minus -------- */
          if (isnil(arg)) {                 /* (-)  →  0  (Scheme behaviour) */
              res = fe_make_number(ctx, 0);
          } else {
              /* first operand */
              fe_Number x = nval(checknum(ctx, evalarg()));

              if (isnil(arg)) {             /* unary: (- x) → -x */
                  res = fe_make_number(ctx, -x);
              } else {                      /* n-ary: (- x y z …) */
                  while (!isnil(arg)) {
                      x -= nval(checknum(ctx, evalarg()));
                  }
                  res = fe_make_number(ctx, x);
              }
          }
          break;
        case P_MUL: arithop(*); break;
        case P_DIV: arithop(/); break;
      }
      break;

    case FE_TCFUNC:
      res = cfunc(fn)(ctx, evallist(ctx, arg, env));
      break;

    case FE_TFUNC: {
      arg = evallist(ctx, arg, env);

      fe_Object *fn_guts = cdr(fn);         /* (def_env free_vars params . body) */
      fe_Object *def_env = car(fn_guts);
      fn_guts = cdr(fn_guts);
      fe_Object *free_vars = car(fn_guts);
      fe_Object *params_body = cdr(fn_guts);
      fe_Object *params = car(params_body);
      fe_Object *body = cdr(params_body);
      fe_Object *p;

      /* Create upvalue list (the closure's captured environment) */
      fe_Object *upvals = &nil;
      int s = fe_savegc(ctx);
      fe_pushgc(ctx, upvals);
      fe_pushgc(ctx, def_env);
      for (p = free_vars; !isnil(p); p = cdr(p)) {
        fe_Object *sym = car(p);
        fe_Object *binding = getbound(sym, def_env); /* Use the captured definition env */
        upvals = fe_cons(ctx, binding, upvals);
      }
      fe_restoregc(ctx, s);

      fe_pushgc(ctx, upvals);
      fe_pushgc(ctx, arg);

      /* Create local environment */
      fe_Object *locals = argstoenv(ctx, params, arg, &nil);

      /* Create new frame: (frame_sym . (locals . upvals)) */
      fe_Object *frame = fe_cons(ctx, locals, upvals);
      frame = fe_cons(ctx, frame_sym, frame);

      res = dolist(ctx, body, frame);
      if (is_return_obj(res)) {
          res = cdr(res);
      }
      break;
    }

    case FE_TMACRO: {
      fe_Object *fn_guts = cdr(fn);         /* (def_env free_vars params . body) */
      fe_Object *def_env = car(fn_guts);
      fn_guts = cdr(fn_guts);
      fe_Object *free_vars = car(fn_guts);
      fe_Object *params_body = cdr(fn_guts);
      fe_Object *params = car(params_body);
      fe_Object *body = cdr(params_body);
      fe_Object *p;

      fe_Object *upvals = &nil;
      int s = fe_savegc(ctx);
      fe_pushgc(ctx, upvals);
      fe_pushgc(ctx, def_env);
      for (p = free_vars; !isnil(p); p = cdr(p)) {
        fe_Object *sym = car(p);
        fe_Object *binding = getbound(sym, def_env);
        upvals = fe_cons(ctx, binding, upvals);
      }
      fe_restoregc(ctx, s);
      
      fe_Object *locals = argstoenv(ctx, params, arg, &nil);
      fe_Object *frame = fe_cons(ctx, locals, upvals);
      frame = fe_cons(ctx, frame_sym, frame);
      
      *obj = *dolist(ctx, body, frame);
      fe_restoregc(ctx, gc);
      ctx->calllist = cdr(&cl);
      return eval(ctx, obj, env, NULL);
    }

    default:
      fe_error(ctx, "tried to call non-callable value");
  }

  fe_restoregc(ctx, gc);
  fe_pushgc(ctx, res);
  ctx->calllist = cdr(&cl);
  return res;
}


fe_Object* fe_eval(fe_Context *ctx, fe_Object *obj) {
  return eval(ctx, obj, &nil, NULL);
}


fe_Context* fe_open(void *ptr, int size) {
  int i, save;
  fe_Context *ctx;

  /* init context struct */
  ctx = ptr;
  memset(ctx, 0, sizeof(fe_Context));
  ptr = (char*) ptr + sizeof(fe_Context);
  size -= sizeof(fe_Context);

  /* init objects memory region */
  ctx->objects = (fe_Object*) ptr;
  ctx->object_count = size / sizeof(fe_Object);

    /* --- Initialize new GC state --- */
  ctx->live_count = 0;
  ctx->allocs_since_gc = 0;
  ctx->gc_threshold = (ctx->object_count / GC_INITIAL_DIVISOR);
  if (ctx->gc_threshold < GC_MIN_THRESHOLD) {
    ctx->gc_threshold = GC_MIN_THRESHOLD;
  }

  /* init lists */
  ctx->calllist = &nil;
  ctx->freelist = &nil;
  ctx->modulestack = &nil;
  ctx->symlist = &nil;

  /* populate freelist */
  for (i = 0; i < ctx->object_count; i++) {
    fe_Object *obj = &ctx->objects[i];
    settype(obj, FE_TFREE);
    cdr(obj) = ctx->freelist;
    ctx->freelist = obj;
  }

  /* init objects */
  ctx->t = fe_symbol(ctx, "t");
  fe_set(ctx, ctx->t, ctx->t);

  /* register built in primitives */
  save = fe_savegc(ctx);
  for (i = 0; i < P_MAX; i++) {
    fe_Object *v = object(ctx);
    settype(v, FE_TPRIM);
    prim(v) = i;
    fe_set(ctx, fe_symbol(ctx, primnames[i]), v);
    fe_restoregc(ctx, save);
  }

  /* --- Initialize symbols for closures and analysis --- */
  return_sym = fe_symbol(ctx, "return");
  frame_sym = fe_symbol(ctx, "[frame]");
  do_sym = fe_symbol(ctx, "do");
  let_sym = fe_symbol(ctx, "let");
  quote_sym = fe_symbol(ctx, "quote");
  fn_sym = fe_symbol(ctx, "fn");
  mac_sym = fe_symbol(ctx, "mac");

  return ctx;
}


void fe_close(fe_Context *ctx) {
  /* clear gcstack and symlist; makes all objects unreachable */
  ctx->gcstack_idx = 0;
  ctx->symlist = &nil;
  collectgarbage(ctx);
}


#ifdef FE_STANDALONE

#include <setjmp.h>

static jmp_buf toplevel;
static char buf[64000];

static void onerror(fe_Context *ctx, const char *msg, fe_Object *cl) {
  unused(ctx), unused(cl);
  fprintf(stderr, "error: %s\n", msg);
  longjmp(toplevel, -1);
}


int main(int argc, char **argv) {
  int gc;
  fe_Object *obj;
  FILE *volatile fp = stdin;
  fe_Context *ctx = fe_open(buf, sizeof(buf));

  /* init input file */
  if (argc > 1) {
    fp = fopen(argv[1], "rb");
    if (!fp) { fe_error(ctx, "could not open input file"); }
  }

  if (fp == stdin) { fe_handlers(ctx)->error = onerror; }
  gc = fe_savegc(ctx);
  setjmp(toplevel);

  /* re(p)l */
  for (;;) {
    fe_restoregc(ctx, gc);
    if (fp == stdin) { printf("> "); }
    if (!(obj = fe_readfp(ctx, fp))) { break; }
    obj = fe_eval(ctx, obj);
    if (fp == stdin) { fe_writefp(ctx, obj, stdout); printf("\n"); }
  }

  return EXIT_SUCCESS;
}

#endif
