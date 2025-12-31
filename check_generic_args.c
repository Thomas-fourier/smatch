#include "smatch.h"
#include "smatch_extra.h"
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int my_id;

struct func_arg {
    const char *fn;
    int arg_pos;
};

enum section {
    SEC_NORMAL,
    SEC_AFTER,
    SEC_DO,
    SEC_IGNORE,
};


#undef STRINGIFY
#define parse_error(...) do { \
    sm_warning("Parsing error: " __VA_ARGS__);\
    exit(1);\
} while (0)
#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)
#define stringify_macro(x) #x
#define varname_size 63
#define label " %" STRINGIFY(varname_size) "[a-zA-Z0-9_] "

////////////////////////////////////////////////////////////////////////////////
//              Representation of DSL
////////////////////////////////////////////////////////////////////////////////
static char **arg_cat;      // category of argument (sg, nents,...)
static int nb_arg_cat;
static char **func_name;    // function name
static int nb_func_name;
static int **arg_pos;       // For each function, for each arg, position of the
                            // arg (-1 is return value, -2 not present)

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


// This is only used for parsing
static char **sec_func;
static int nb_sec_func;

////////////////////////////////////////////////////////////////////////////////
//                  Representation of the DSL
////////////////////////////////////////////////////////////////////////////////
static char ***arg_name; // name of the arguments for each category
static char **arg_name_function;
static char **arg_name_location;
static int nb_arg_name;


static void push_array(void ***array, int *len, void *elt) {
    (*array)[*len] = elt;
    (*len)++;
    *array = realloc(*array, (*len + 1) * sizeof(**array));
    (*array)[*len] = NULL;
}

static void init_array(void ***list, int *len) {
    *list = malloc(sizeof(**list));
    (*list)[0] = NULL;
    *len = 0;
}

static bool is_cast(struct expression *expr) {
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

static char *stringify(struct expression *expr) {
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

    if (expr->type == EXPR_PREOP && expr->op == '(')
        return stringify(expr->unop);

    if (expr->type == EXPR_CALL) {
        return stringify_call(expr);
    }

    if (expr->type == EXPR_BINOP ||
        expr->type == EXPR_COMPARE ||
        expr->type == EXPR_LOGICAL) {
        char *l_str = stringify(expr->left);
        char *r_str = stringify(expr->right);
        asprintf(&res, "(%s) %s (%s)", l_str, show_special(expr->op), r_str);
        free_string(l_str);
        free_string(r_str);
        return res;
    }

    if (expr->type == EXPR_DEREF && expr->member) {
        if (expr->deref->type == EXPR_DEREF) {
            char *parent = stringify(expr->deref);
            if (!parent)
                return NULL;
            asprintf(&res, "%s->%s", parent, expr->member->name);
            free_string(parent);
        } else {
            asprintf(&res, "(%s)->%s", type_to_str(get_type(expr->deref)),
                     expr->member->name);
        }
        return res;
    }

    res = expr_to_str(expr);

    pp = res;
    while (pp && (pp = strstr(pp, "++"))) {
        memmove(pp, pp+2, strlen(pp+2)+1);
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
            for (; i > 0; i--)
                free (str_args[i-1]);
            free(str_args);
            return NULL;
        }
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


/* Tests if a string is in a list. If it is, write index with the index in the
list if found */
static bool is_expr_in_list(const char *expr, char **list, int len, int *index)
{
    int i;
    for (i = 0; i < len; i++) {
        if (list[i] && strcmp(expr, list[i]) == 0) {
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

static char *get_arg_from_call_expr(struct expression *expr, int arg_position) {
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

static char *search_param_in_expr(char *haystack, char *needle)
{
    char *n_start = strstr(haystack, needle);
    if (!n_start)
        return NULL;

    if (n_start > haystack && 
        *(n_start - 1) != '(' &&
        *(n_start - 1) != ')' &&
        *(n_start - 1) != ' ' &&
        *(n_start - 1) != ',' &&
        *(n_start - 1) != '[' &&
        *(n_start - 1) != ']')
        return NULL;

    int needle_len = strlen(needle);
    int haystack_len = strlen(haystack);

    if (n_start + needle_len < haystack + haystack_len &&
        *(n_start + needle_len) != ')' &&
        *(n_start + needle_len) != '(' &&
        *(n_start + needle_len) != ' ' &&
        *(n_start + needle_len) != ',' &&
        *(n_start + needle_len) != ']' &&
        *(n_start + needle_len) != '['
    )
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

static bool args_are_same(char **arg_name_1, char **arg_name_2)
{
    for (int i = 0; i < nb_arg_cat; i++) {
        if (arg_name_1[i] && arg_name_2[i]) {
            if (!two_args_are_same(arg_name_1[i], arg_name_2[i]))
                return false;
        }
    }
    return true;
}

static void print_arg_name(FILE *out) {
    fprintf(out, "\t\t\t\t");
    for (int i = 0; arg_cat[i]; i++)
        fprintf(out, "%16s\t", arg_cat[i]);

    fprintf(out, "\n");

    for (int i = 0; arg_name[i]; i++) {
        fprintf(out, "%24s\t", arg_name_function[i]);
        for (int j = 0; arg_cat[j]; j++) {
            if (arg_name[i][j])
                fprintf(out, "%16s\t", arg_name[i][j]);
        }
        fprintf(out, "\n");
    }
    fprintf(out, "\n\n");
}

static void print_arg_pos(FILE *out) {
    fprintf(out,"Arg positions:\t");
    for (int j = 0; arg_cat[j]; j++) 
        fprintf(out, "%16s", arg_cat[j]);
    fprintf(out, "\n");

        for (int i = 0; func_name[i]; i++) {
        fprintf(out, "%24s\t", func_name[i]);

        for (int j = 0; arg_cat[j]; j++)
            fprintf(out, "%d\t\t", arg_pos[i][j]);
        fprintf(out, "\n");
    }
    fprintf(out, "\n");
}


static bool similar_line_exists(char **new_arg_name, const char *fn_name)
{
    if (!arg_name_function)
        return false;
    for (int i = 0; i < nb_arg_name; i++) {
        if (0 == strcmp(fn_name, arg_name_function[i])) {
            if (args_are_same(new_arg_name, arg_name[i]))
                return true;
        }
    }
    return false;
}

// Assume that there is only one ternary pattern in the call
static char **split_array_if_ternary(char **new_arg_name)
{
    char **new_array = NULL;
    char *mark_index;

    for (int i = 0; i < nb_arg_cat; i++) {
        if (new_arg_name[i] && (mark_index = strstr(new_arg_name[i], "?"))) {
            if (new_array) {
                sm_warning( "Two ternary patterns on the same file");
                continue;
            }
            new_array = malloc(sizeof(*new_array) * nb_arg_cat);
            for (int j = 0; j < nb_arg_cat; j++) {
                if (j == i) {
                    char *column = strstr(new_arg_name[i], ":");
                    if (!column) {
                        sm_warning( "? without :");
                        continue;
                    }
                    *column = 0;
                    new_array[j] = alloc_string(mark_index + 1);
                    new_arg_name[j] = alloc_string(column + 1);
                } else {
                    new_array[j] = new_arg_name[j];
                }

            }
        }
    }
    return new_array;
}

static void push_line_or_free(char **new_arg_name, const char* fn_name) 
{
    // If the call is not the same as one already done, pass otherwise add
    if (!similar_line_exists(new_arg_name, fn_name)) {
        push_array((void ***)&arg_name, &nb_arg_name, new_arg_name);
        nb_arg_name--;
        push_array((void ***)&arg_name_function, &nb_arg_name, alloc_string(fn_name));
        nb_arg_name--;
        char *loc;
        asprintf(&loc, "%s:%d", get_filename(), get_lineno());
        push_array((void ***)&arg_name_location, &nb_arg_name,loc);
    } else {
        for (int i = 0; i < nb_arg_cat; i++)
            free(new_arg_name[i]);
        free(new_arg_name);
    }
}

static void match_func(const char *fn_name, struct expression *expr, void *_fn_id)
{
    if (is_fake_call(expr) || __inline_fn)
        return;

    // Ignore if we are in the implementation of a function
    if (is_expr_in_list(get_function(), func_name, nb_func_name, NULL))
        return;

    int fn_id = (int)(long) _fn_id;


    char **new_arg_name = calloc(nb_arg_cat, sizeof(*new_arg_name));

    for (int cur_arg_cat = 0; arg_cat[cur_arg_cat]; cur_arg_cat++) {
        if (arg_pos[fn_id][cur_arg_cat] == -2) continue;

        char *str_arg = get_arg_from_call_expr(expr, arg_pos[fn_id][cur_arg_cat]);
        if (!str_arg) continue;

        new_arg_name[cur_arg_cat] = str_arg;
    }

    char **other_array = split_array_if_ternary(new_arg_name);

    push_line_or_free(new_arg_name, fn_name);
    if (other_array)
        push_line_or_free(other_array, fn_name);
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

static bool exists_similar_call(bool *checked, int index)
{
    if (!arg_name_function)
        return true;
    if (!arg_name)
        return true;
    bool ret = false;
    for (int j = 0; j < nb_arg_name; j++) {
        if (j == index)
            continue;

        if (checked[j] && ret)
            continue;

        if (strcmp(arg_name_function[index], arg_name_function[j]) != 0 &&
            args_are_same(arg_name[index], arg_name[j])) {
                checked[j] = true;
                ret = true;
        }
    }
    return ret;
}

static bool all_funcs_are_same()
{
    if (0 == nb_arg_name)
        return false;

    char *fn_name = arg_name_function[0];
    for (int i = 0; i < nb_arg_name; i++) {
        if (strcmp(fn_name, arg_name_function[i]) != 0)
            return false;
    }
    return true;
}

static void match_file_end()
{
    if (false) print_arg_name(stderr);

    if (all_funcs_are_same()) {
        if (option_spammy)
            fprintf(sm_outfd,
                    "%s warn: Function %s is the only of the interface\n",
                    arg_name_location[0], arg_name_function[0]);
        return;
    }

    bool *checked = calloc(nb_arg_name, sizeof(*checked));

    for (int i = 0; i < nb_arg_name; i++) {
        if (checked[i])
            continue;
        if (exists_similar_call(checked, i))
            continue;

        fprintf(sm_outfd, "%s warn: Possible function not matched %s\n",
                   arg_name_location[i], arg_name_function[i]);

    }

    free(checked);
}

////////////////////////////////////////////////////////////////////////////////
//                                PARSER                                      //
////////////////////////////////////////////////////////////////////////////////



static bool parse_decl(char *line)
{
    char buffer[varname_size + 1];
    int i;
    if (1 != sscanf(line, "var "label, buffer))
        return false;

    if (is_expr_in_list(buffer, arg_cat, nb_arg_cat, &i))
        parse_error("Double declaration of %s", buffer);

    push_array((void ***)&arg_cat, &nb_arg_cat, alloc_string(buffer));

    for (int i = 0; func_name[i]; i++) {
        arg_pos[i] = realloc(arg_pos[i], nb_arg_cat * sizeof(*arg_pos[i]));
        arg_pos[i][nb_arg_cat - 1] = -2;
    }
    return true;
}

static bool add_to_arg_pos(char *expr, char *line, int pos, bool key)
{
    int index;

    if (strcmp("_", expr) == 0)
        return true;

    if (!is_expr_in_list(expr, arg_cat, nb_arg_cat, &index))
        parse_error("Argument %s not declared.", expr);

    if (arg_pos[nb_func_name - 1][index] != -2)
        parse_error("Argument %s used multiple times in %s", expr, line);

    arg_pos[nb_func_name - 1][index] = pos;
    return true;
}

static bool parse_call(char *line, enum section sec)
{
    char buffer[varname_size + 1];
    char *current;
    char *last;
    bool key = false;
    int i;
    if (1 != sscanf(line, label, buffer))
        parse_error("Impossible to parse line %s", line);

    if (sec == SEC_AFTER) {
        push_array((void ***)&sec_func, &nb_sec_func, alloc_string(buffer));
    }

    if (is_expr_in_list(buffer, func_name, nb_func_name, &i))
        parse_error("Function %s defined multiple times line: %s", buffer, line);

    current = strchr(line, '(');
    if (!current)
        parse_error("Line %s could not be parsed", line);

    push_array((void ***)&func_name, &nb_func_name, alloc_string(buffer));
    arg_pos = realloc(arg_pos, nb_func_name * sizeof(*arg_pos));
    arg_pos[nb_func_name - 1] = malloc(nb_arg_cat * sizeof(**arg_pos));

    for (i = 0; i < nb_arg_cat; i++)
        arg_pos[nb_func_name - 1][i] = -2;

    i = 0;
    do {
        current++;
        if (1 == sscanf(current, " key "label, buffer))
            key = true;
        else if (1 != sscanf(current, label, buffer))
            parse_error("Could not read variable in %s", current);

        if (!add_to_arg_pos(buffer, line, i, key))
            return false;
        i++;
        last = current;
        key = false;
    } while ((current = strchr(last, ',')));

    if (!strchr(last, ')'))
        parse_error("Parenthesis not closed %s", line);

    return true;
}

static bool parse_equal(char *line, enum section sec)
{
    char *sep;
    char ret_val[varname_size + 1];
    bool key = false;
    if (!(sep = strchr(line, '=')))
        return false;

    if (!parse_call(sep + 1, sec))
        parse_error("Weird line '%s'", line);

    if (1 == sscanf(line, " key " label, ret_val))
        key = true;
    else if (1 != sscanf(line, label, ret_val))
        parse_error("Could not parse affectation statement %s", line);

    add_to_arg_pos(ret_val, line, -1, key);

    return true;
}

bool isempty(const char *s)
{
    while (*s) {
        if (!isspace(*s))
            return false;
        s++;
    }
    return true;
}


bool is_label(char *line, enum section *sec) {
    char buffer[varname_size + 1];
    if (!strchr(line, ':')) {
        return false;
    }

    if (1 != sscanf(line, label":", buffer))
        return false;

    if (*sec == SEC_AFTER) {
        if (0 == strcmp(buffer, "do")) {
            *sec = SEC_DO;
            return true;
        } else {
            parse_error("do: section must be after after: section.");
        }
    }

    if (*sec == SEC_DO) {
        for (int i = 0; sec_func[i]; i++)
            free_string(sec_func[i]);
        free(sec_func);
        sec_func = NULL;
    }

    if (0 == strcmp(buffer, "after")) {
        sec_func = calloc(1, sizeof(*sec_func));
        nb_sec_func = 0;
        *sec = SEC_AFTER;
    } else if (0 == strcmp(buffer, "then")) {
        *sec = SEC_NORMAL;
    } else if (0 == strcmp(buffer, "ignore")) {
        *sec = SEC_IGNORE;
    }
    return true;
}

static bool parse_do_test(char *line)
{
    char *test_pos;
    test_pos = strstr(line, "test");

    if (test_pos)
        return true;

    return false;
}

static bool parse_ignore(char *line) {
    char buffer[varname_size + 1];
    if (1 == sscanf(line, label, buffer)) {
        return true;
    }

    return false;
}

static bool parse_file(const char *filename) {
    init_array((void ***)&arg_cat, &nb_arg_cat);
    init_array((void ***)&func_name, &nb_func_name);
    arg_pos = NULL;

    FILE *file = fopen(filename, "r");
    if (!file) {
        sm_warning("File %s could not be opened.", filename);
        return false;
    }
    char *line = NULL;
    size_t line_size;
    char *comment_start;
    enum section sec = SEC_NORMAL;

    while (-1 != getline(&line, &line_size, file)) {
        if ((comment_start = strstr(line, "//")))
            comment_start[0] = '\0';

        if (isempty(line)) continue;
        if (is_label(line, &sec)) continue;
        if (parse_decl(line)) continue;

        switch (sec) {
            case SEC_NORMAL:
            case SEC_AFTER:
                if (parse_equal(line, sec)) break;
                if (parse_call(line, sec)) break;
                break;
            case SEC_DO:
                parse_do_test(line);
                break;
            case SEC_IGNORE:
                parse_ignore(line);
        }

    }

    free(line);
    return true;
}

void manual_init()
{
    arg_cat = malloc(4 * sizeof(*arg_cat));
    arg_cat[0] = alloc_string("sg");
    arg_cat[1] = alloc_string("nents");
    arg_cat[2] = alloc_string("dma_nents");
    arg_cat[3] = NULL;
    nb_arg_cat = 3;

    func_name = malloc(3 * sizeof(*func_name));
    func_name[0] = alloc_string("dma_map_sg_attrs");
    func_name[1] = alloc_string("dma_sync_sg_for_device");
    func_name[2] = NULL;
    nb_func_name = 2;

    arg_pos = malloc(2 * sizeof(*arg_pos));
    arg_pos[0] = malloc(3 * sizeof(**arg_pos));
    arg_pos[1] = malloc(3 * sizeof(**arg_pos));

    // dma_map_sg_attrs
    arg_pos[0][0] = 1;
    arg_pos[0][1] = 2;
    arg_pos[0][2] = -1;

    // dma_unmap_sg_attrs
    arg_pos[1][0] = 1;
    arg_pos[1][1] = 2;
    arg_pos[1][2] = -2;

}


void check_generic_args(int id) {
    my_id = id;

    // manual_init();
    if (! option_generic_args_file || !parse_file(option_generic_args_file))
        return;
    if (false) print_arg_pos(stdout);

    init_array((void ***)&arg_name, &nb_arg_name);
    arg_name_function = calloc(1, sizeof(*arg_name_function));
    arg_name_location = calloc(1, sizeof(*arg_name_location));

    int i;
    for (i = 0; func_name[i]; i++)
        add_function_hook(func_name[i], &match_func, (void *)(long)i);


    add_hook(match_assign, ASSIGNMENT_HOOK);

    add_hook(match_file_end, END_FILE_HOOK);
}