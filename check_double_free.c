#include "parse.h"
#include "smatch.h"
#include "smatch_extra.h"
#include "stringification.h"
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <unistd.h>

static int my_id;
static struct string_list* freed_expr;
static struct string_list* freed_expr_loc;
static bool in_error_path;


#define run_again_filename ".run_smatch_again"


////////////////////////////////////////////////////////////////////////////////
// 			Function definition
////////////////////////////////////////////////////////////////////////////////
DECLARE_PTR_LIST(string_list_list, struct string_list);
// Function name to parameters list
static GHashTable *function_parameters = NULL;
// Function name to arguments of call instances struct expression_list_list
static GHashTable *function_called = NULL;

static void __match_assign(char *left, char *right);

static void assign_list(char **args, struct string_list *params)
{
    int arg_index = 0;
    char *arg;
    FOR_EACH_PTR(params, arg) {
        if (!args[arg_index] || !arg)
            return;
        __match_assign(alloc_string(args[arg_index]), arg);
        arg_index++;
    } END_FOR_EACH_PTR(arg);

}

static void match_func_start(struct symbol *sm)
{
    in_error_path = false;

    if (!sm->ident)
        return;

    if (g_hash_table_lookup(function_parameters, sm->ident->name))
        return;

    char **arguments = NULL;
    int nb_func_args = 0;
    struct symbol *arg;
    FOR_EACH_PTR(sm->ctype.base_type->arguments, arg) {
        if (!arg->ident) {
            continue;
        }

        nb_func_args++;
        arguments = realloc(arguments, sizeof(*arguments) * nb_func_args);
        arguments[nb_func_args - 1] = arg->ident->name;

    } END_FOR_EACH_PTR(arg);

    nb_func_args++;
    arguments = realloc(arguments, sizeof(*arguments) * nb_func_args);
    arguments[nb_func_args - 1] = NULL;


    struct string_list_list *calls = g_hash_table_lookup(function_called, sm->ident->name);
    if (calls) {
        struct string_list *call;
        FOR_EACH_PTR(calls, call) {
            assign_list(arguments, call);
        } END_FOR_EACH_PTR(call);
    }

    g_hash_table_insert(function_parameters, sm->ident->name, arguments);
}


static struct string_list *call_to_string_list(struct expression *expr)
{
    struct string_list *res = NULL;
    struct expression *arg;
    char *arg_str;
    
    FOR_EACH_PTR(expr->args, arg) {
        arg_str = stringify(arg);
        add_ptr_list(&res, arg_str);
    } END_FOR_EACH_PTR(arg);

    return res;
}

static void match_func_call(struct expression *expr)
{
    struct string_list *call_expr;
    char *fn_name;

    if (in_error_path)
        return;
    
    fn_name = expr_to_str(expr->fn);
    if (!fn_name) {
        free(fn_name);
        return;
    }

    call_expr = call_to_string_list(expr);

    char **args = g_hash_table_lookup(function_parameters, fn_name);
    if (!args) {
        bool free_fn = false;
        struct string_list_list *calls = g_hash_table_lookup(function_called, fn_name);
        if (calls)
            free_fn = true;

        add_ptr_list(&calls, call_expr);
        g_hash_table_insert(function_called, fn_name, calls);

        if (free_fn)
            free(fn_name);
        return;
    }

    assign_list(args, call_expr);
}



////////////////////////////////////////////////////////////////////////////////
// 			Confusion
////////////////////////////////////////////////////////////////////////////////
static char **arg_confusion = NULL;
static int *confusion_parent = NULL;
static int nb_arg_confusion;

static void add_arg(char *new_arg)
{
    arg_confusion = realloc(arg_confusion,
                            sizeof(*arg_confusion) * (nb_arg_confusion+1));
    confusion_parent = realloc(confusion_parent,
                               sizeof(*confusion_parent) * (nb_arg_confusion+1));
    if (!(arg_confusion && confusion_parent))
	    exit(1);
    confusion_parent[nb_arg_confusion] = nb_arg_confusion;
    arg_confusion[nb_arg_confusion] = alloc_string(new_arg);
    nb_arg_confusion++;
}

static int find_index(int i)
{
    if (i < 0 || i > nb_arg_confusion) {
        sm_error("Wrong argument in find_index");
        exit(1);
    }
    if (confusion_parent[i] == i)
        return i;
    else
        return find_index(confusion_parent[i]);
}

static int find(char *some_arg)
{
    for (int i = 0; i < nb_arg_confusion; i++) {
        if (strcmp(some_arg, arg_confusion[i]) == 0) {
            int ret = find_index(i);
            return ret;
        }
    }
    add_arg(some_arg);
    return nb_arg_confusion - 1;
}

static void union_(char *some_arg, char *other_arg)
{
    int some_parent = find(some_arg);
    int other_parent = find(other_arg);

    confusion_parent[other_parent] = some_parent;
}


////////////////////////////////////////////////////////////////////////////////
//                         Constant assignment
////////////////////////////////////////////////////////////////////////////////
static int* argument_cst = NULL;    // index of the element in confusion_list
static int* const_value = NULL; // constant value corresponding
static int nb_argument = 0;

static void constant_assign(char *left, int right)
{
    nb_argument++;
    argument_cst = realloc(argument_cst,
                           (nb_argument * sizeof(*argument_cst)));
    const_value = realloc(const_value,
                           (nb_argument * sizeof(*const_value)));
    
    argument_cst[nb_argument - 1] = find(left);
    const_value[nb_argument - 1] = right;
}

static int find_const(char *var, char *cst)
{
    int int_cst;
    int var_find = find(var);

    if (1 != sscanf(cst, "%d", &int_cst)) {
        sm_error("Expression %s flagged as constant but scanf failed", cst);
        return -1;
    }

    for (int j = 0; j < nb_argument; j++) {
        if (const_value[j] == int_cst) {
            if (var_find == find_index(argument_cst[j]))
            return true;
        }
    }
    return false;
}

static bool is_separator(char c)
{
    return c == '(' ||
           c == ')' ||
           c == ' ' ||
           c == ',' ||
           c == '[' ||
           c == ']';
}

static char *search_param_in_expr(char *haystack, char *needle)
{
    char *n_start = strstr(haystack, needle);
    if (!n_start)
        return NULL;

    if (n_start > haystack && !is_separator(*(n_start - 1)))
        return NULL;

    int needle_len = strlen(needle);
    int haystack_len = strlen(haystack);

    if (n_start + needle_len < haystack + haystack_len &&
        !is_separator(*(n_start + needle_len)))
        return NULL;

    return n_start;
}

static bool strs_are_same_with_sub(char *char1, char *substr_start_1,
                                   int substr_1_idx,
                                   char *char2, char *substr_start_2,
                                   int substr_2_idx)
{
    // Check that what comes before the affected string is same
    while (substr_start_1 && substr_start_2) {
        while (char1 < substr_start_1 && char2 < substr_start_2) {
            if (*char1 != *char2)
                return false;
            char2++; char1++;
        }
        // check that what comes after is same
        char1 = substr_start_1 + strlen(arg_confusion[substr_1_idx]);
        char2 = substr_start_2 + strlen(arg_confusion[substr_2_idx]);

        // Next occurrence of the substrs 
        substr_start_1 = search_param_in_expr(char1,
                                              arg_confusion[substr_1_idx]);
        substr_start_2 = search_param_in_expr(char2, 
                                              arg_confusion[substr_2_idx]);
    }

    while (*char1 && *char2) {
        if (*char1 != *char2)
            return false;
        char1++; char2++;
    }

    return *char1 == *char2;
}

static bool str_with_some_subs_are_same(char *char1, char *substr_start_1, 
                                        int substr_1_idx, char *char2)
{
    char *substr_start_2;

    for (int j = 0; j < nb_arg_confusion; j++) {
        if (find_index(substr_1_idx) == find_index(j)) {
            if ((substr_start_2 = search_param_in_expr(char2, arg_confusion[j]))) {
                if (strs_are_same_with_sub(char1, substr_start_1, substr_1_idx,
                                       char2, substr_start_2, j))
                    return true;
            }
        }
    }
    return false;
}

static bool str_is_constant(char *str)
{
    for (; *str; str++) {
        if (!isdigit(*str))
            return false;
    }
    return true;
}

static bool two_args_are_same(char *char1, char *char2)
{
    bool char1_cst = str_is_constant(char1);
    bool char2_cst = str_is_constant(char2);
    if (char1_cst && char2_cst)
        return strcmp(char1, char2) == 0;
    char *substr_start_1;

    if (char1_cst)
        return find_const(char2, char1);
    else if (char2_cst)
        return find_const(char1, char2);

    if (find(char1) == find(char2))
        return true;

    for (int i = 0; i < nb_arg_confusion; i++) {
        if ((substr_start_1 = search_param_in_expr(char1, arg_confusion[i])) &&
            strcmp(char1, arg_confusion[i]) != 0) {
            if (str_with_some_subs_are_same(char1, substr_start_1, i, char2))
                return true;
        }
    }
    return false;
}


static void __match_assign(char *left, char *right)
{
    if (str_is_constant(right)) {
        int right_int;
        if (1 != sscanf(right, "%d", &right_int)) {
            sm_error("%s scan as int failed", right);
            goto free_right;
        }
        constant_assign(left, right_int);
        goto free_right;
    }

    if (strlen(left) <= 2 || strlen(right) <= 2) {
        goto free_right;
    }

    if (strchr(left, '$') || strchr(right, '$')) {
        goto free_right;
    }

    union_(right, left);

free_right:
    free_string(right);
    free_string(left);
    return;
}

static void match_assign(struct expression *expr)
{
    if (is_fake_var_assign(expr) || is_fake_assigned_call(expr) ||
        is_fake_call(expr->right))
        return;

    if (__inline_fn)
        return;


    char *left_str = stringify(expr->left);
    if (!left_str)
        return;

    char *right_str = stringify(expr->right);
    if (!right_str) {
        free(left_str);
        return;
    }

    __match_assign(left_str, right_str);
}

static void match_free_func(const char *_, struct expression* expr, void *__arg)
{
    int arg_index = PTR_INT(__arg);
    char *freed = get_arg_from_call_expr(expr, arg_index);
    char *loc;
    char *old_free;

    if (!freed)
        return;

    asprintf(&loc, "%s:%d", get_filename(), get_lineno());

    for (int i = 0; i < ptr_list_size(freed_expr); i++) {
        old_free = ptr_list_nth_entry((struct ptr_list *)freed_expr, i);
        if (old_free && two_args_are_same(freed, old_free)) {
            sm_warning("Double free of %s at %s", freed,
                       (char *)ptr_list_nth_entry((struct ptr_list *)freed_expr_loc,
                                                  i));
            return;
        }
    }

    add_ptr_list(&freed_expr, freed);
    add_ptr_list(&freed_expr_loc, loc);
}

static void register_funcs_from_file(void)
{
	struct token *token;
	const char *func;
	int arg;

	token = get_tokens_file("kernel.frees_argument");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		arg = atoi(token->number);
		add_function_hook(func, &match_free_func, INT_PTR(arg));
		token = token->next;
	}
	clear_token_alloc();
}

void match_label(struct statement *stmt)
{
    if (stmt->type == STMT_LABEL)
        in_error_path = true;
}


void check_double_free(int id)
{
    my_id = id;

    register_funcs_from_file();

    function_parameters = g_hash_table_new(g_str_hash, g_str_equal);
    function_called = g_hash_table_new(g_str_hash, g_str_equal);

    add_hook(match_func_start, FUNC_DEF_HOOK);
    add_hook(match_func_call, FUNCTION_CALL_HOOK);
    add_hook(match_label, STMT_HOOK);
    add_hook(match_assign, ASSIGNMENT_HOOK);
}
