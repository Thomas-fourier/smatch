#include "smatch.h"
#include <string.h>

#define FN_LEN 256

static int my_id;
static bool must_check_fn;

STATE(to_check);
STATE(checked);

static void match_func_def() {
    if (__inline_fn)
        return;

    must_check_fn = false;
}

static bool is_to_check(struct expression *expr) {
    if (expr_has_possible_state(my_id, expr, &to_check))
        return true;

    sval_t val;
    if (get_value(expr, &val) && (0 <= -val.value && -val.value <= 139))
        return true;

    if (expr->type == EXPR_CALL && expr->fn) {
        char fn[FN_LEN];
        if (expr->fn->type == EXPR_IDENTIFIER) {
            strncpy(fn, expr->fn->symbol_name->name, FN_LEN-1);
        } else if (expr->fn->type == EXPR_DEREF && expr->fn->ident) {
            char *p = fn;
            p += snprintf(p, FN_LEN, "(%s)", type_to_str(get_type(expr->fn->deref)));
            p += snprintf(p, FN_LEN - (p-fn), ".");
            p += snprintf(p, FN_LEN - (p-fn), "%s", expr->fn->ident->name);
        }
    }

    return false;
}

static void match_conditionnal(struct expression *expr){
    if (expr_has_possible_state(my_id, expr, &to_check)) {
        set_state_expr(my_id, expr, &checked);
    }
}

static void match_assign(struct expression *expr) {
    if (__inline_fn)
        return;

    if (is_to_check(expr->right)) {
        set_state_expr(my_id, expr->left, &to_check);
    }
}

static void match_return(struct expression *expr) {
    if (__inline_fn)
        return;
    
    if (is_to_check(expr)) {
        must_check_fn = true;
    }
}

static void match_func_end() {
    if (__inline_fn)
        return;

    if (must_check_fn) {
        // add to database
    }
    must_check_fn = false;
}

void check_error_code_check(int id) {
    my_id = id;
    
    add_hook(match_func_def, FUNC_DEF_HOOK);

    add_hook(match_conditionnal, CONDITION_HOOK);
    add_hook(match_assign, ASSIGNMENT_HOOK);
    add_hook(match_return, RETURN_HOOK);


    add_hook(match_func_end, END_FUNC_HOOK);

}