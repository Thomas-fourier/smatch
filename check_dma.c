
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"
#include "string.h"

static int my_id;

STATE(untested_dma);
STATE(tested_dma);

int untested_dma_count = 0;

// Sometimes, the state tracking is wrong, for instance in a[i]
// If two expressions are the same as strings, assume, they are
// actually the same.
char *last_dma_map = NULL;

static const char *dma_mapping_functions[] = {
    "dma_map_single",
    "dma_map_single_attrs",
    "dma_map_page",
    "dma_map_page_attrs",
    "dma_map_area",
};

static void match_dma_map(const char *fn, struct expression *expr, void *unused) {
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

static void match_dma_error(const char *fn, struct expression *expr, void *unused) {
    struct expression *arg = get_argument_from_call_expr(expr->args, 1);

    if (get_state_expr(my_id, arg) == &tested_dma) {
        sm_warning("dma_mapping_error called on an already tested dma pointer.");
    }

    if (!(expr_has_possible_state(my_id, arg, &untested_dma) ||
        (last_dma_map && strcmp(last_dma_map, expr_to_str(arg)) == 0))) {
        sm_warning("dma_mapping_error called with %s which is not dma'd", expr_to_str(arg));
        return;
    }

    set_state_expr(my_id, arg, &tested_dma);
    free_string(last_dma_map);
    last_dma_map = NULL;
    untested_dma_count--;
}



static void match_func_end(struct symbol *sym) {
    if (__inline_fn) {
        return;
    }

    struct stree *stree;
	struct sm_state *tmp;
    bool found_untested = false;

	stree = __get_cur_stree();
	FOR_EACH_MY_SM(my_id, stree, tmp) {
		if (slist_has_state(tmp->possible, &untested_dma)) {
            sm_warning("possible dma mapping not tested of pointer %s", tmp->name);
            found_untested = true;
        }
	} END_FOR_EACH_SM(tmp);


    if (!found_untested && untested_dma_count && !strstr(get_function(), "dma_map")) {
        sm_warning("Function %s has %d untested dma_map calls", get_function(), untested_dma_count);
    }
    untested_dma_count = 0;

    free_string(last_dma_map);
    last_dma_map = NULL;

}


void check_dma(int id)
{
	my_id = id;
    if (option_project != PROJ_KERNEL) {
        return;
    }

    add_function_hook("dma_mapping_error", match_dma_error, NULL);

    for (int i = 0; i < ARRAY_SIZE(dma_mapping_functions); i++)
        add_function_hook(dma_mapping_functions[i], match_dma_map, NULL);

    add_hook(match_func_end, END_FUNC_HOOK);
}
