
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"
#include "string.h"
#include <string.h>

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
    "dma_map_resource",
    "ib_dma_map_single",
    "ib_dma_map_page",
    "__skb_frag_dma_map",
    "__skb_frag_dma_map1",
    "fc_dma_map_single",
    "nfp_net_dma_map_rx",
    "hmm_dma_map_pfn",
    "vring_map_single",
    "virtqueue_dma_map_single_attrs",
};

static const char *dma_mapping_test_funcs[] = {
    "dma_mapping_error",
    "ib_dma_mapping_error",
    "fc_dma_mapping_error",
    "enic_dma_map_check",
    "vring_mapping_error",
    "virtqueue_dma_mapping_error",
};

static bool str_in_array(char *str, char *array[], int array_size) {
    for (int i = 0; i < array_size; i++) {
        if (strcmp(str, array[i]) == 0)
            return true;
    }

    return false;
}

static bool in_implementation() {
    return (str_in_array(get_function(),
                        (char **) dma_mapping_test_funcs,
                        ARRAY_SIZE(dma_mapping_test_funcs)) ||
            str_in_array(get_function(),
                         (char **) dma_mapping_functions,
                         ARRAY_SIZE(dma_mapping_functions))
            );
}

static void set_untested(struct expression *expr) {
    if (is_fake_var(expr))
        return;

    set_state_expr(my_id, expr, &untested_dma);

    if (last_dma_map){
        sm_warning("possible dma mapping not tested of %s",
                   last_dma_map);
        free_string(last_dma_map);
    }
    last_dma_map = expr_to_str(expr);


    // We remove the preops
    // TODO: check that they are preops
    char *pp;
    if (!(pp = strstr(last_dma_map, "++"))) {
        return;
    }
    char *tmp = malloc(strlen(last_dma_map));
    strcpy(tmp, last_dma_map);
    free_string(last_dma_map);
    last_dma_map = tmp;
    memmove(pp, pp+2, strlen(pp+2)+1);

    while ((pp = strstr(last_dma_map, "++"))) {
        memmove(pp, pp+2, strlen(pp+2)+1);
    }

}

static void match_dma_map(const char *fn, struct expression *expr, void *unused) {
    if (__inline_fn)
        return;

    if (in_implementation())
        return;

    struct expression *parent = expr_get_parent_expr(expr);

    set_state_expr(my_id, expr, &untested_dma);

    if (!parent || parent->type != EXPR_ASSIGNMENT){
        char *str_expr = expr_to_str(expr);
        sm_warning("Calling DMA_MAP but not assigned in %s\n", str_expr);
        free_string(str_expr);
        return;
    }

    set_untested(parent->left);
    untested_dma_count++;
}

static bool is_dma_untested(struct expression *arg, char arg_str[]) {
    return (expr_has_possible_state(my_id, arg, &untested_dma) ||
        (last_dma_map && arg_str && strcmp(last_dma_map, arg_str) == 0));
}

static void match_dma_error(const char *fn, struct expression *expr, void *unused) {
    char *arg_str;

    if (__inline_fn)
        return;

    if (in_implementation())
        return;

    struct expression *arg = get_argument_from_call_expr(expr->args, 1);

    if (strcmp(get_function(), "svm_is_valid_dma_mapping_addr") == 0 ||
        strcmp(get_function(), "gve_free_page") == 0)
        return;

    if (get_state_expr(my_id, arg) == &tested_dma) {
        arg_str = expr_to_str(arg);
        if (strcmp(arg_str, last_dma_map) == 0) {
            goto ok;
        }
        if (option_spammy)
            sm_warning("dma_mapping_error called on an already tested dma pointer.");
        return;
    }

    arg_str = expr_to_str(arg);
    if (!is_dma_untested(arg, arg_str)) {
        if (option_spammy)
            sm_warning("dma_mapping_error called with %s which is not dma'd", arg_str);
        free_string(arg_str);
        return;
    }


ok:
    set_state_expr(my_id, arg, &tested_dma);
    free_string(last_dma_map);
    free_string(arg_str);
    last_dma_map = NULL;
    untested_dma_count--;
}

static void match_assign(struct expression *expr) {
    if (is_fake_var_assign(expr))
        return;

    char *arg_str = expr_to_str(expr->right);
    if (is_dma_untested(expr->right, arg_str)) {
        set_untested(expr->left);
    }
    free_string(arg_str);
}

static void match_func_end(struct symbol *sym) {
    if (__inline_fn)
        return;

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


    if (!found_untested && untested_dma_count && (last_dma_map || option_spammy))
        sm_warning("%d untested dma_map call%s, including %s",
                   untested_dma_count, untested_dma_count > 1 ? "s" : "",
                   last_dma_map);

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

    for (int i = 0; i < ARRAY_SIZE(dma_mapping_test_funcs); i++)
        add_function_hook(dma_mapping_test_funcs[i], match_dma_error, NULL);

    for (int i = 0; i < ARRAY_SIZE(dma_mapping_functions); i++)
        add_function_hook(dma_mapping_functions[i], match_dma_map, NULL);

    add_hook(match_assign, ASSIGNMENT_HOOK);
    add_hook(match_func_end, END_FUNC_HOOK);
}
