#include "smatch.h"
#include "smatch_extra.h"
#include <glib.h>
#include "stringification.h"

 #define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define nb_max_pair 2000

GHashTable *function_calls = NULL;
FILE *out;


//////////////////////////////////////////////////////////////////////////
//                  Function call representation                        //
//////////////////////////////////////////////////////////////////////////

typedef float score;
struct fn_call {
    char *func;
    int nb_args;
    char **args;
    char *macro_name;
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
    int nb_real_args = 0;

    for (int i = 0; i < nb_args; i++) {
        arg_expr = get_argument_index(expr, i - 1);

        if (arg_expr) {
            str[i] = stringify(arg_expr);
            if (str[i])
                nb_real_args++;
        } else {
            str[i] = NULL;
        }
    }

    if (nb_real_args < 2) {
        for (int i = 0; i < nb_args; i++)
            free_string(str[i]);
        free(str);
        return NULL;
    }

    struct fn_call *res = malloc(sizeof(*res));
    res->nb_args = nb_args;
    res->args = str;
    res->func = stringify(expr->fn);
    res->macro_name = alloc_string(get_macro_name(expr->pos));
    if (res->macro_name)
        fprintf(stderr, "Macro %s\n", res->macro_name);
    return res;
}

static void free_call_list(struct fn_call_list *call_list) {
    struct fn_call *call;
    FOR_EACH_PTR(call_list, call) {
        for (int i = 0; i < call->nb_args; i++)
            free_string(call->args[i]);
        free(call->args);
        free(call->func);
        free(call->macro_name);
        free(call);
    } END_FOR_EACH_PTR(call);
    free_ptr_list(&call_list);
}

///////////////////////////////////////////////////////////////////////////
//                          Hook functions                               //
///////////////////////////////////////////////////////////////////////////

static void match_func_def(struct symbol *sm)
{
    if (__inline_fn || __inline_call)
        return;

    fprintf(out, "Defining %s with %d arguments in file s %s\n",
            sm->ident->name,
            ptr_list_size((struct ptr_list*)sm->ctype.base_type->arguments),
            get_filename());
}

static bool in_header()
{
    const char *filename = get_filename();
    int filename_name = strlen(filename);
    if (filename_name >= 2 && filename[filename_name - 1] == 'h' &&
        filename[filename_name - 2] == '.')
        return true;

    return false;
}

static void match_func(struct expression *expr)
{
    if (__inline_fn || __inline_call)
        return;

    if (in_header())
        return;

    char *fn = expr_to_str(expr->fn);
    if (!fn)
        return;

    fprintf(out, "Calling %s in %s:%s\n", fn, get_filename(), get_function());

    if (ptr_list_size((struct ptr_list *)expr->args) <= 1) {
        free(fn);
        return;
    }

    bool free_fn = false;
    struct fn_call *fn_rep = save_fn_call(expr);
    if (!fn_rep) {
        free(fn);
        return;
    }
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

static bool macro_correlation(struct fn_call_list *calls_i, int len_i,
                             struct fn_call_list *calls_j, int len_j)
{
    struct fn_call *i, *j;
    int nb_common_macro = 0;

    FOR_EACH_PTR(calls_i,i); {
        FOR_EACH_PTR(calls_j, j) {
            if (i->macro_name && j->macro_name &&
                strcmp(i->macro_name, j->macro_name) == 0)
                    nb_common_macro++;
        } END_FOR_EACH_PTR(j);
    } END_FOR_EACH_PTR(i);


    return ((float) nb_common_macro * 2) / (len_i + len_j) >= 0.5;
}

static score compute_correlation(struct fn_call_list *calls_i,
                                 struct fn_call_list *calls_j)
{
    struct fn_call *i, *j;
    int ind_i, ind_j;
    score cur_max;
    score avg_i = 0, avg_j = 0;
    int len_i, len_j;
    score **dists;

    len_i = ptr_list_size((struct ptr_list *)calls_i);
    len_j = ptr_list_size((struct ptr_list *)calls_j);

    if (macro_correlation(calls_i, len_i, calls_j, len_j))
        return 0;


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
        cur_max = 0;
        for (ind_j = 0; ind_j < len_j; ind_j++)
            cur_max = max(cur_max, dists[ind_i][ind_j]);
        avg_i += cur_max;
        ind_i++;
    } END_FOR_EACH_PTR(i);

    ind_j = 0;
    FOR_EACH_PTR(calls_j, j) {
        cur_max = 0;
        for (ind_i = 0; ind_i < len_i; ind_i++)
            cur_max = max(cur_max, dists[ind_i][ind_j]);
        avg_j += cur_max;
        ind_j++;
    } END_FOR_EACH_PTR(j);


    for (ind_i = 0; ind_i < len_i; ind_i++)
        free(dists[ind_i]);
    free(dists);


    return (avg_i / (score)len_i + avg_j / (score)len_j) / 2;
}

static void add_to_dist(char *fun_i, char *fun_j, score dist, score *distances,
                        char **func_pair)
{
    if (dist < distances[nb_max_pair - 1])
        return;

    int j = nb_max_pair - 1, i = 0;
    while (dist < distances[i] && i < nb_max_pair)
        i++;

    free(func_pair[j]);

    while (j >= i) {
        distances[j] = distances[j - 1];
        func_pair[j] = func_pair[j - 1];
        j--;
    }

    distances[i] = dist;

    // Order pairs of functions because h_map doesn't always return them in the
    // same order.
    if (strcmp(fun_i,fun_j) < 0) {
        char* temp;
        temp = fun_i;
        fun_i = fun_j;
        fun_j = temp;
    }

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
            if (dis > 0.5)
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
    if (!option_resemblance)
        return;

    out = stdout;
    function_calls = g_hash_table_new(g_str_hash, g_str_equal);

    add_hook(match_func_def, FUNC_DEF_HOOK);
    add_hook(match_func, FUNCTION_CALL_HOOK);
    add_hook(match_file_end, END_FILE_HOOK);
}
