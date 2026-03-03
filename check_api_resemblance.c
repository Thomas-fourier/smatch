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

    fprintf(out, "Defining %s with %d arguments in file %s\n",
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
    if (__inline_fn)
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

static int max_nb_args(struct fn_call_list *calls)
{
    struct fn_call *i;
    int res = 0;

    FOR_EACH_PTR(calls, i) {
        res = max(res, i->nb_args);
    } END_FOR_EACH_PTR(i);
    return res;
}

#define swap(i,j) do {                                          \
    typeof(i) tmp = j;                                          \
    j = i;                                                      \
    i = tmp;                                                    \
} while (0)

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


static void print_common_args(struct fn_call_list *calls_i,
                              struct fn_call_list *calls_j)
{
    int nb_calls_i = ptr_list_size(calls_i);
    int nb_calls_j = ptr_list_size(calls_j);

    if (macro_correlation(calls_i, nb_calls_i, calls_j, nb_calls_j))
        return;

    if (nb_calls_i < nb_calls_j) {
        swap(calls_i, calls_j);
        swap(nb_calls_i, nb_calls_j);
    }

    struct fn_call *i, *j;
    int nb_args_i = max_nb_args(calls_i);
    int nb_args_j = max_nb_args(calls_j);
    int nb_func_same_arg;

    for (int arg_index_i = 0; arg_index_i < nb_args_i; arg_index_i ++) {
        for (int arg_index_j = 0; arg_index_j < nb_args_j; arg_index_j ++) {
            nb_func_same_arg = 0;
            FOR_EACH_PTR(calls_i, i) {
                FOR_EACH_PTR(calls_j, j) {
                    if (arg_index_i < i->nb_args && arg_index_j < j->nb_args &&
                        i->args[arg_index_i] && j->args[arg_index_j] &&
                        0 == strcmp(i->args[arg_index_i],
                                    j->args[arg_index_j])) {
                        nb_func_same_arg++;
                        goto loop_exit;
                    }
                } END_FOR_EACH_PTR(j);
loop_exit:
                NULL;
            } END_FOR_EACH_PTR(i);
            if (nb_func_same_arg)
                fprintf(out, "Same argument: %s.%d %s.%d %f\n",
                        calls_i->list[0]->func, arg_index_i,
                        calls_j->list[0]->func, arg_index_j,
                        (float)nb_func_same_arg /(float)nb_calls_i);
        }
    }

}

static void match_file_end()
{
    GHashTableIter i, j;
    char *fun_i, *fun_j;
    struct fn_call_list *calls_i, *calls_j;


    g_hash_table_iter_init(&i, function_calls);

    while (g_hash_table_iter_next(&i, (void **)&fun_i,
                                  (void **)&calls_i)) {
        g_hash_table_iter_remove(&i); // Remove to compute distance
        // between different functions
        g_hash_table_iter_init(&j, function_calls);
        while (g_hash_table_iter_next(&j, (void **)&fun_j,
                                      (void **)&calls_j)) {
            print_common_args(calls_i, calls_j);
        }

        free(fun_i);
        free_call_list(calls_i);
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
