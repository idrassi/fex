#include "fe.h"
#include "fex.h"

int main(void) {
    char arena[64 * 1024];
    fe_Context *ctx = fe_open(arena, sizeof(arena));
    if (!ctx) {
        return 1;
    }

    fex_init(ctx);
    fe_close(ctx);
    return 0;
}
