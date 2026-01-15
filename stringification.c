#include "smatch.h"
#include "smatch_extra.h"
#include "stringification.h"


static char *stringify_call(struct expression *expr)
{
    int nb_args = ptr_list_size((struct ptr_list *)expr->args);
    char **str_args;
    int len = 0;
    char *res;
    char *p;
    int i;

    char *func = stringify(expr->fn);
    len = strlen(func) + 1;
    if (!nb_args) {
        res = realloc(func, len + 2);
        strcpy(res + len - 1, "()");
        return res;
    }

    str_args = malloc(nb_args * sizeof(*str_args));
    for (i = 0; i < nb_args; i++) {
        str_args[i] = stringify(get_argument_from_call_expr(expr->args,
                                                                  i));
        len += (2 + strlen(str_args[i]));
    }

    res = malloc(len);
    p = res;
    p += sprintf(p, "%s(", func);
    free_string(func);
    for (i = 0; i < nb_args; i++) {
        p += sprintf(p, i == nb_args - 1 ? "%s)" : "%s, ", str_args[i]);
        free_string(str_args[i]);
    }
    free(str_args);

    return res;
}

bool is_cast(struct expression *expr) {
    if (!expr)
        return false;

    switch (expr->type) {
        case EXPR_CAST:
        case EXPR_FORCE_CAST:
        case EXPR_IMPLIED_CAST:
            return true;
        default:
            return false;
    }
}

char *stringify(struct expression *expr)
{
    char *res;
    char *pp;
    sval_t constant;

    expr = strip_expr(expr);
    if (get_value(expr, &constant))
        return alloc_string(sval_to_str(constant));

    if (is_cast(expr))
        return stringify(expr->cast_expression);

    if (expr->type == EXPR_ASSIGNMENT)
        return stringify(expr->left);

    if (expr->type == EXPR_PREOP && (expr->op == '&' || expr->op == '*'))
        return stringify(expr->unop);

    if (expr->type == EXPR_CALL) {
        return stringify_call(expr);
    }

    if (expr->type == EXPR_DEREF && expr->member) {
        if (expr->deref->type == EXPR_DEREF) {
            char *parent = stringify(expr->deref);
            asprintf(&res, "%s->%s", parent, expr->member->name);
            free_string(parent);
        } else {
            asprintf(&res, "(%s)->%s", type_to_str(get_type(expr->deref)),
                     expr->member->name);
        }
    } else {
        res = expr_to_str(expr);

        pp = res;
        while (pp && (pp = strstr(pp, "++"))) {
            memmove(pp, pp+2, strlen(pp+2)+1);
        }
    }

    return res;
}
