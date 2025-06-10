
#include "smatch.h"
#include "smatch_extra.h"
#include "string.h"

static int my_id;

STATE(untested_dma);
STATE(tested_dma);

int untested_dma_count = 0;

// Sometimes, the state tracking is wrong, for instance in a[i]
// If two expressions are the same, we assume it is ok.
char *last_dma_map = NULL;

static void match_dma_map(struct expression *expr) {
    struct expression *parent = expr_get_parent_expr(expr);

    set_state_expr(my_id, expr, &untested_dma);

    if (parent && parent->type != EXPR_ASSIGNMENT)
        return;
    
    set_state_expr(my_id, parent->left, &untested_dma);

    if (last_dma_map)
        free_string(last_dma_map);
    last_dma_map = expr_to_str(parent->left);
    
    untested_dma_count++;
}

static void match_dma_error(struct expression *expr) {
    struct expression *arg = get_argument_from_call_expr(expr->args, 1);

    //printf("dma_mapping_test %s\n", expr_to_str(expr));

    if (!expr_has_possible_state(my_id, arg, &untested_dma)) {
        if (last_dma_map && strcmp(last_dma_map, expr_to_str(arg)) == 0) {
            // If the last dma_map was the same as this one, it is ok
            goto ok;
        }
        sm_warning("dma_mapping_error called with %s which is not dma'd", expr_to_str(arg));
        return;
    }

ok:
    set_state_expr(my_id, arg, &tested_dma);
    free_string(last_dma_map);
    last_dma_map = NULL;
    untested_dma_count--;
}

static void match_call(struct expression *expr) {
    if (expr->type != EXPR_CALL || !expr->fn)
        return;

    if (!get_function())
        return;

    // If it is the implementation of dma_map or dma_mapping_error, ignore
    if (strstr(get_function(), "dma_map") ||
        strstr(get_function(), "dma_mapping_error")) {
        return;
    }

    char *fn_name = expr_to_str(expr->fn);
    if (!fn_name)
        return;

    if (strstr(fn_name, "dma_map_sg") ||
        strstr(fn_name, "rdma"))
        goto free;

    if (strstr(fn_name, "dma_mapping_error")) {
        match_dma_error(expr);
        goto free;
    }

    if (strstr(fn_name, "dma_map")) {
        match_dma_map(expr);
        goto free;
    }

 free:
    free_string(fn_name);
}



static void match_func_end(struct symbol *sym) {
    if (__inline_fn) {
        return;
    }

    if (untested_dma_count && !strstr(get_function(), "dma_map")) {
        sm_warning("Function %s has %d untested dma_map calls", get_function(), untested_dma_count);
    }
    untested_dma_count = 0;
}


void check_dma(int id)
{
	my_id = id;
    if (option_project != PROJ_KERNEL) {
        return;
    }

    // TODO: ignore map_sg
    // TODO: ignore rdma

    add_hook(match_call, FUNCTION_CALL_HOOK);
    add_hook(match_func_end, END_FUNC_HOOK);
}
