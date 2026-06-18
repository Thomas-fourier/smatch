#include "smatch.h"

static int id;

void match_stmt(struct statement *stmt)
{
    char *macro_name = get_macro_name(stmt->pos);
    if (macro_name && (strcmp(macro_name, "mb") == 0 ||
                       strcmp(macro_name, "wmb") == 0 ||
                       strcmp(macro_name, "rmb") == 0 ||
                       strcmp(macro_name, "smp_mb") == 0 ||
                       strcmp(macro_name, "smp_wmb") == 0 ||
                       strcmp(macro_name, "smp_rmb") == 0 ||
                       strcmp(macro_name, "dma_mb") == 0 ||
                       strcmp(macro_name, "dma_wmb") == 0 ||
                       strcmp(macro_name, "dma_rmb") == 0)) {
        fprintf(stderr, "Barrier %s at%s:%d\n", macro_name,
                get_filename(), get_lineno());
    }
}

void match_call_expr(struct expression *expr)
{
    char *fn_name = expr_to_str(expr->fn);
    fprintf(stderr, "Funciton call %s at%s:%d\n", fn_name,
                get_filename(), get_lineno());
    free(fn_name);
}

void check_barriers(int _id)
{
    id = _id;

    add_hook(&match_stmt, STMT_HOOK);
    add_hook(&match_call_expr, FUNCTION_CALL_HOOK);
}