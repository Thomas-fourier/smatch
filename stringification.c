#include "smatch.h"
#include "smatch_extra.h"
#include "stringification.h"


static char *stringify_call(struct expression *expr)
{
    int nb_args = ptr_list_size((struct ptr_list *)expr->args);
    char **str_args;
    int len = 0;
    char *res = 0;
    char *p;
    int i;

    char *func = stringify(expr->fn);
    if (!func)
        return NULL;

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
        if (!str_args[i]) {
            free(res);
            nb_args = i;
            res = 0;
            goto free_str_args;
        }
        len += (2 + strlen(str_args[i]));
    }

    res = malloc(len);
    p = res;
    p += sprintf(p, "%s(", func);
free_str_args:
    free_string(func);
    for (i = 0; i < nb_args; i++) {
        if (res)
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

    if (!expr)
        return NULL;

    expr = strip_expr(expr);
    if (get_value(expr, &constant))
        return alloc_string(sval_to_str(constant));

    if (is_cast(expr))
        return stringify(expr->cast_expression);

    switch (expr->type) {
        case EXPR_ASSIGNMENT:
            return stringify(expr->left);

        case EXPR_PREOP:
        case EXPR_POSTOP:
            // Array access
            if (expr->op == '*' && expr->unop->type == EXPR_BINOP &&
                expr->unop->op == '+') {
                char *array = stringify(expr->unop->left);
                asprintf(&res, "%s[index]", array);
                free(array);
                return res;
            }
            if (expr->op == '&' || expr->op == '*')
                return stringify(expr->unop);
        case EXPR_COMPARE:
        case EXPR_LOGICAL:
        case EXPR_BINOP:
        case EXPR_COMMA: {
            // Array access
            struct expression *array_expr;
            if ((array_expr = get_array_expr(expr))) {
                asprintf(&res, "%s[index]", stringify(array_expr));
                return res;
            }
            char *l = stringify(expr->left);
            if (!l)
                return NULL;
            char *r = stringify(expr->right);
            if (!r) {
                res = 0;
                goto free_l;
            }
            // Sort operands of commutative binop
            switch (expr->op) {
                case '+':
                case '*':
                case '&':
                case '|':
                case '^':
                case SPECIAL_EQUAL:
	            case SPECIAL_NOTEQUAL:
	            case SPECIAL_LOGICAL_AND:
	            case SPECIAL_LOGICAL_OR: {
                    if (strcmp(r, l) > 0) {
                        char *tmp = r;
                        r = l;
                        l = tmp;
                    }
                }
            }
            asprintf(&res, "(%s) %c (%s)", l, expr->op, r);
            free(r);
free_l:
            free(l);
            return res;
        }
        break;
        case EXPR_CALL:
            return stringify_call(expr);

        case EXPR_DEREF: {
            char *type = type_to_str(get_type(expr->deref));
            if (!type || !expr->member || strcmp(type, "struct ") == 0) {
                return NULL;
            }
            asprintf(&res, "(%s)->%s", type, expr->member->name);
            return res;
        }
    }

    res = expr_to_str(expr);

    pp = res;
    while (pp && (pp = strstr(pp, "++"))) {
        memmove(pp, pp+2, strlen(pp+2)+1);
    }

    return res;
}

char *get_arg_from_call_expr(struct expression *expr, int arg_position) {
    struct expression *arg;
    if (arg_position == -1) {
        struct expression *parent = expr_get_parent_expr(expr);
        if (parent && is_cast(parent))
            return get_arg_from_call_expr(parent, -1);
        if (parent && parent->type == EXPR_ASSIGNMENT && parent->left)
            arg = parent->left;
        else // Maybe warning as well
            return NULL;
    } else {
        arg = get_argument_from_call_expr(expr->args, arg_position);
    }

    return stringify(arg);
}
