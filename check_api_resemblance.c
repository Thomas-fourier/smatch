#include "smatch.h"
#include "smatch_extra.h"
#include <glib.h>
#include <math.h>

 #define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define nb_max_pair 200

GHashTable *function_calls = NULL;
FILE *out;


//////////////////////////////////////////////////////////////////////
//                          Stringify                               //
//////////////////////////////////////////////////////////////////////

static bool is_cast(struct expression *expr)
{
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

static char *stringify_call(struct expression *expr);

static char *stringify(struct expression *expr)
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

//////////////////////////////////////////////////////////////////////////
//                  Function call representation                        //
//////////////////////////////////////////////////////////////////////////

typedef float score;
struct fn_call {
    char *func;
    int nb_args;
    char **args;
};

DECLARE_PTR_LIST(fn_call_list, struct fn_call);

static struct expression *get_argument_index(struct expression *expr, int arg_position) {
    if (arg_position == -1) {
        struct expression *parent = expr_get_parent_expr(expr);
        if (parent && is_cast(parent))
            return get_argument_index(parent, -1);
        if (parent && parent->type == EXPR_ASSIGNMENT && parent->left)
            return parent->left;
        else
            return NULL;
    } else {
        return get_argument_from_call_expr(expr->args, arg_position);
    }
}

static struct fn_call *save_fn_call(struct expression *expr) {
    int nb_args = ptr_list_size((struct ptr_list *)expr->args) + 1;
    char **str = malloc(sizeof(* str) * nb_args);
    struct expression *arg_expr;

    for (int i = 0; i < nb_args; i++) {
        arg_expr = get_argument_index(expr, i - 1);

        if (arg_expr)
            str[i] = stringify(arg_expr);
        else
            str[i] = NULL;
    }

    struct fn_call *res = malloc(sizeof(*res));
    res->nb_args = nb_args;
    res->args = str;
    res->func = stringify(expr->fn);
    return res;
}

static void free_call_list(struct fn_call_list *call_list) {
    struct fn_call *call;
    FOR_EACH_PTR(call_list, call) {
        for (int i = 0; i < call->nb_args; i++)
            free(call->args[i]);
        free(call->args);
        free(call->func);
        free(call);
    } END_FOR_EACH_PTR(call);
    free_ptr_list(&call_list);
}

///////////////////////////////////////////////////////////////////////////
//                          Hook functions                               //
///////////////////////////////////////////////////////////////////////////

static void match_func_def(struct symbol *sm)
{
    fprintf(out, "Defining %s in file %s\n", sm->ident->name, get_filename());
}

static void match_func(struct expression *expr)
{
    if (__inline_call)
        return;

    if (ptr_list_size((struct ptr_list *)expr->args) <= 1)
        return;

    bool free_fn = false;
    char *fn = expr_to_str(expr->fn);
    struct fn_call *fn_rep = save_fn_call(expr);
    // For each function, save all the calls to it as
    struct fn_call_list *tab = g_hash_table_lookup(function_calls, fn);
    if (tab)
        free_fn = true;

    add_ptr_list(&tab, fn_rep);

    g_hash_table_insert(function_calls, fn, tab);
    if (free_fn)
        free(fn);
}

///////////////////////////////////////////////////////////////////////////
//               Compute distance between functions                      //
///////////////////////////////////////////////////////////////////////////

static score compute_distance(struct fn_call *expr_1,
                              struct fn_call *expr_2)
{
    int nb_args_i = expr_1->nb_args, nb_args_j = expr_2->nb_args;
    char **args_i = expr_1->args;
    char **args_j = expr_2->args;

    int common_args = 0;

    for (int i = 0; i < nb_args_i; i++) {
        for (int j = 0; j < nb_args_j; j++) {
            if (args_i[i] && args_j[j] &&
                strcmp(args_i[i], args_j[j]) == 0) {
                common_args++;
                break;
            }
        }
    }

    return common_args;
}

static score compute_correlation(struct fn_call_list *calls_i,
                                 struct fn_call_list *calls_j)
{
    struct fn_call *i, *j;
    int ind_i, ind_j;
    score cur_min;
    score avg_i = 0, avg_j = 0;
    int len_i, len_j;
    score **dists;

    len_i = ptr_list_size((struct ptr_list *)calls_i);
    len_j = ptr_list_size((struct ptr_list *)calls_j);

    dists = malloc(sizeof(*dists) * len_i);
    for (ind_i = 0; ind_i < len_i; ind_i++)
        dists[ind_i] = malloc(sizeof(**dists) * len_j);

    ind_i = 0;
    FOR_EACH_PTR(calls_i, i) {
        ind_j = 0;
        FOR_EACH_PTR(calls_j, j) {
            dists[ind_i][ind_j] = compute_distance(i, j);
            ind_j++;
        } END_FOR_EACH_PTR(j);
        ind_i++;
    } END_FOR_EACH_PTR(i);

    ind_i = 0;
    FOR_EACH_PTR(calls_i,i); {
        cur_min = INFINITY;
        for (ind_j = 0; ind_j < len_j; ind_j++)
            cur_min = min((score)cur_min, (score)dists[ind_i][ind_j]);
        avg_i += cur_min;
        ind_i++;
    } END_FOR_EACH_PTR(i);

    ind_j = 0;
    FOR_EACH_PTR(calls_j, j) {
        cur_min = INT_MAX;
        for (ind_i = 0; ind_i < len_i; ind_i++) {
            cur_min = min((score)cur_min, (score)dists[ind_i][ind_j]);
        }
        avg_j += cur_min;
        ind_j++;
    } END_FOR_EACH_PTR(j);


    for (ind_i = 0; ind_i < len_i; ind_i++)
        free(dists[ind_i]);
    free(dists);


    return avg_i / (score)len_i + avg_j / (score)len_j;
}

static void add_to_dist(char *fun_i, char *fun_j, score dist, score *distances,
                        char **func_pair)
{
    if (dist > distances[nb_max_pair - 1])
        return;

    int j = nb_max_pair - 1, i = 0;
    while (dist > distances[i] && i < nb_max_pair)
        i++;

    free(func_pair[j]);

    while (j >= i) {
        distances[j] = distances[j - 1];
        func_pair[j] = func_pair[j - 1];
        j--;
    }

    distances[i] = dist;
    asprintf(&func_pair[i], "%s %s", fun_i, fun_j);
}

static void match_file_end()
{
    GHashTableIter i, j;
    char *fun_i, *fun_j;
    struct fn_call_list *calls_i, *calls_j;
    score distances[nb_max_pair];
    char *func_pair[nb_max_pair];

    memset(distances, 0xff, nb_max_pair * sizeof(*distances));
    memset(func_pair, 0x00, nb_max_pair * sizeof(*func_pair));

    g_hash_table_iter_init(&i, function_calls);

    while (g_hash_table_iter_next(&i, (void **)&fun_i,
                                  (void **)&calls_i)) {
        g_hash_table_iter_remove(&i); // Remove to compute distance
        // between different functions
        g_hash_table_iter_init(&j, function_calls);
        while (g_hash_table_iter_next(&j, (void **)&fun_j,
                                      (void **)&calls_j)) {
            score dis = compute_correlation(calls_i, calls_j);
            add_to_dist(fun_i, fun_j, dis, distances, func_pair);
        }

        free(fun_i);
        free_call_list(calls_i);
    }

    for (int i = 0; i < nb_max_pair && func_pair[i]; i++) {
        fprintf(out, "funct pair: %s %f\n", func_pair[i], distances[i]);
        free(func_pair[i]);
    }
}

void check_api_resemblance(int id)
{
    out = stderr;
    function_calls = g_hash_table_new(g_str_hash, g_str_equal);

    add_hook(match_func_def, FUNC_DEF_HOOK);
    add_hook(match_func, FUNCTION_CALL_HOOK);
    add_hook(match_file_end, END_FILE_HOOK);
}