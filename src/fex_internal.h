#ifndef FEX_INTERNAL_H
#define FEX_INTERNAL_H

#include "fex.h"

fe_Object* fex_compile_named(fe_Context *ctx, const char *source,
                             const char *source_name);
fe_Object* fex_do_string_named(fe_Context *ctx, const char *source,
                               const char *source_name);
int fex_try_is_active(void);
void fex_try_raise(FexStatus status, const char *source_name,
                   int line, int column, const char *message);

#endif
