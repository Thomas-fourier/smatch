#include "smatch.h"
#include "smatch_extra.h"
#include "stringification.h"
#include "dsl.h"
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int my_id;
static struct calls_rep *all_calls;

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
        }}
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

static bool args_are_same(char **arg_name_1, char **arg_name_2,
                          struct calls_rep *calls)
{
    for (int i = 0; i < calls->dsl.nb_arg_cat; i++) {
        if (arg_name_1[i] && arg_name_2[i]) {
            if (!two_args_are_same(arg_name_1[i], arg_name_2[i]))
                return false;
        }
    }
    return true;
}

static void print_arg_name(FILE *out, struct calls_rep *calls)
{
    if (!calls->nb_arg_name)
        return;

    fprintf(out, "%s\n", calls->dsl.filename);

    fprintf(out, "\t\t\t\t");
    for (int i = 0; calls->dsl.arg_cat[i]; i++)
        fprintf(out, "%16s\t", calls->dsl.arg_cat[i]);

    fprintf(out, "\n");

    for (int i = 0; calls->arg_name[i]; i++) {
        fprintf(out, "%24s\t", calls->arg_name_function[i]);
        for (int j = 0; calls->dsl.arg_cat[j]; j++) {
            if (calls->arg_name[i][j])
                fprintf(out, "%16s\t", calls->arg_name[i][j]);
            else
                fprintf(out, "\t\t\t");
        }
        fprintf(out, "\n");
    }
    fprintf(out, "\n\n");
}

static void print_arg_pos(FILE *out, const struct dsl_representation *dsl)
{
    fprintf(out,"Arg positions:\t");
    for (int j = 0; dsl->arg_cat[j]; j++) 
        fprintf(out, "%16s", dsl->arg_cat[j]);
    fprintf(out, "\n");

        for (int i = 0; dsl->func_name[i]; i++) {
        fprintf(out, "%24s\t", dsl->func_name[i]);

        for (int j = 0; dsl->arg_cat[j]; j++)
            fprintf(out, "%d\t\t", dsl->arg_pos[i][j]);
        fprintf(out, "\n");
    }
    fprintf(out, "\n");
}

// Assume that there is only one ternary pattern in the call
static char **split_array_if_ternary(char **new_arg_name,
                                     struct calls_rep *calls)
{
    char **new_array = NULL;
    char *mark_index;
    char *old_entry;

    for (int i = 0; i < calls->dsl.nb_arg_cat; i++) {
        if (new_arg_name[i] && (mark_index = strstr(new_arg_name[i], "?"))) {
            if (new_array) {
                sm_warning( "Two ternary patterns on the same file");
                continue;
            }
            new_array = malloc(sizeof(*new_array) * calls->dsl.nb_arg_cat);
            for (int j = 0; j < calls->dsl.nb_arg_cat; j++) {
                if (j == i) {
                    char *column = strstr(new_arg_name[i], ":");
                    if (!column) {
                        sm_warning( "? without :");
                        continue;
                    }
                    old_entry = new_arg_name[j];
                    *column = '\0';
                    new_array[j] = alloc_string(mark_index + 1);
                    new_arg_name[j] = alloc_string(column + 1);
                    free(old_entry);
                } else {
                    new_array[j] = alloc_string(new_arg_name[j]);
                }

            }
        }
    }
    return new_array;
}

static void push_line_or_free(char **new_arg_name, const char* fn_name,
                              struct calls_rep *calls) 
{
    // If the call is not the same as one already done, pass otherwise add
    push_array((void ***)&calls->arg_name, &calls->nb_arg_name, new_arg_name);
    calls->nb_arg_name--;
    push_array((void ***)&calls->arg_name_function, &calls->nb_arg_name, alloc_string(fn_name));
    calls->nb_arg_name--;
    char *loc;
    asprintf(&loc, "%s:%d", get_filename(), get_lineno());
    push_array((void ***)&calls->arg_name_location, &calls->nb_arg_name,loc);
}

static void match_func(const char *fn_name, struct expression *expr, void *_calls)
{
    struct calls_rep *calls = _calls;
    if (is_fake_call(expr) || __inline_fn)
        return;

    // Ignore if we are in the implementation of a function
    if (is_expr_in_list(get_function(), calls->dsl.func_name, calls->dsl.nb_func_name, NULL))
        return;

    int fn_id;
    for (fn_id = 0;
         strcmp(fn_name, calls->dsl.func_name[fn_id]) &&
                fn_id < calls->dsl.nb_func_name;
         fn_id++) {}


    char **new_arg_name = calloc(calls->dsl.nb_arg_cat, sizeof(*new_arg_name));

    for (int cur_arg_cat = 0; calls->dsl.arg_cat[cur_arg_cat]; cur_arg_cat++) {
        if (calls->dsl.arg_pos[fn_id][cur_arg_cat] == -2) continue;

        char *str_arg = get_arg_from_call_expr(expr, calls->dsl.arg_pos[fn_id][cur_arg_cat]);
        if (!str_arg) continue;

        new_arg_name[cur_arg_cat] = str_arg;
    }

    char **other_array = split_array_if_ternary(new_arg_name, calls);

    push_line_or_free(new_arg_name, fn_name, calls);
    if (other_array)
        push_line_or_free(other_array, fn_name, calls);
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

    // If one of the side is constant
    sval_t TMP;
    if (get_value(expr->right, &TMP)) {
        constant_assign(left_str, TMP.value);
        goto free_left;
    }

    char *right_str = stringify(expr->right);
    if (!right_str)
        goto free_left;


    if (str_is_constant(right_str)) {
        int right_int;
        if (1 != sscanf(right_str, "%d", &right_int)) {
            sm_error("%s scan as int failed", right_str);
            goto free_right;
        }
        constant_assign(left_str, right_int);
        goto free_right;
    }

    union_(right_str, left_str);

free_right:
    free_string(right_str);
free_left:
    free_string(left_str);
    return;
}

static bool exists_similar_call(bool *checked, int index,
                                struct calls_rep *calls)
{
    if (!calls->arg_name_function)
        return true;
    if (!calls->arg_name)
        return true;
    bool ret = false;
    for (int j = 0; j < calls->nb_arg_name; j++) {
        if (j == index)
            continue;

        if (checked[j] && ret)
            continue;

        if (strcmp(calls->arg_name_function[index], calls->arg_name_function[j]) != 0 &&
            args_are_same(calls->arg_name[index], calls->arg_name[j], calls)) {
                checked[j] = true;
                ret = true;
        }
    }
    return ret;
}

static bool all_funcs_are_same(struct calls_rep *calls)
{
    if (0 == calls->nb_arg_name)
        return false;

    char *fn_name = calls->arg_name_function[0];
    for (int i = 0; i < calls->nb_arg_name; i++) {
        if (strcmp(fn_name, calls->arg_name_function[i]) != 0)
            return false;
    }
    return true;
}

static void match_file_end_calls(struct calls_rep *calls)
{
    if (false) print_arg_name(stderr, calls);

    if (all_funcs_are_same(calls)) {
        if (option_spammy)
            fprintf(sm_outfd,
                    "%s warn: Function %s is the only of the interface\n",
                    calls->arg_name_location[0], calls->arg_name_function[0]);
        return;
    }

    bool *checked = calloc(calls->nb_arg_name, sizeof(*checked));

    for (int i = 0; i < calls->nb_arg_name; i++) {
        if (checked[i])
            continue;
        if (exists_similar_call(checked, i, calls))
            continue;

        fprintf(sm_outfd, "%s warn: Possible function not matched %s\n",
                   calls->arg_name_location[i], calls->arg_name_function[i]);

    }

    free(checked);
}

static void match_file_end() {
    for (int i = 0; i < nb_generic_args_file; i++)
        match_file_end_calls(&all_calls[i]);
}

static void init_call_rep(int i) {
    char *filename = option_generic_args_file[i];
    struct calls_rep *calls = &all_calls[i];
    parse_file(filename, (struct dsl_representation *)&calls->dsl);

    if (false) print_arg_pos(stdout, &calls->dsl);
    
    init_array((void ***)&calls->arg_name, &calls->nb_arg_name);
    calls->arg_name_function = calloc(1, sizeof(*calls->arg_name_function));
    calls->arg_name_location = calloc(1, sizeof(*calls->arg_name_location));

    for (i = 0; calls->dsl.func_name[i]; i++)
        add_function_hook(calls->dsl.func_name[i], &match_func, (void *)calls);

    
}

void check_generic_args(int id) {
    my_id = id;

    if (!option_check_api)
        return;

    if ((!option_generic_args_file))
        return;

    all_calls = malloc(sizeof(*all_calls) * nb_generic_args_file);

    for (int i = 0; i < nb_generic_args_file; i++)
        init_call_rep(i);


    add_hook(match_assign, ASSIGNMENT_HOOK);
    add_hook(match_file_end, END_FILE_HOOK);
}