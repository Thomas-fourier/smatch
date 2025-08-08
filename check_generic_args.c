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

// Initially created
static char **arg_cat;      // category of argument (sg, nents,...)
static int nb_arg_cat;
static char **func_name;    // function name
static int nb_func_name;
static int **arg_pos;       // For each function, for each arg, position of the
                            // arg (-1 is return value, -2 not present)
static int *key_arg;        // key_arg[i] is the type of argument which is the
                            // key
static char **ignore_funcs;
static int nb_ignore_funcs;

static int **to_test;       // List of things to test:
                            // [after fn_id, test var_id, with fn_id, or with…]
                            // if with func is -1, then with
static int nb_to_test;

static char **var_to_test;
static int nb_var_to_test;
static struct statement *parent_if;
static int var_to_test_type;
static int *test_func;
static int *test_from_line; // All the lines concerned with the test

static char **all_vars_to_test_in_func;
static char nb_all_vars_to_test_in_func;

// This is only used for parsing
static char **sec_func;
static int nb_sec_func;

// Maintained and updated
static char ***arg_name; // name of the arguments for each category
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

static bool comp_list(int *array1, int* array2)
{
    int i;
    for (i = 0; array1[i] != -1 && array2[i] != -1; i++) {
        if (array1[i] != array2[i])
            return false;
    }
    return array1[i] == array2[i];
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

static char *stringify(struct expression *expr) {
    char *res;
    char *pp;

    while (is_cast(expr) || expr->type == EXPR_ASSIGNMENT) {
        if (expr->type == EXPR_ASSIGNMENT)
            expr = expr->left;
        else
            expr = expr->cast_expression;
    }

    if (expr->type == EXPR_DEREF && expr->member) {
        res = malloc(strlen(expr->member->name) + 1);
        sprintf(res, "%s", expr->member->name);
    } else {
        res = expr_to_str(expr);

        pp = res;
        while (pp && (pp = strstr(pp, "++"))) {
            memmove(pp, pp+2, strlen(pp+2)+1);
        }
    }

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

/* Find string in a matrix of strings, if no
 *
 */
static void find_previous_arg_name(char *expr, int *this_arg_cat, int *index)
{
    int i;
    for (i = 0; arg_name[i]; i++) {
        if (arg_name[i][*this_arg_cat] &&
            strcmp(arg_name[i][*this_arg_cat], expr) == 0) {
            *index = i;
            return;
        }
    }

    for (i = 0; arg_name[i]; i++) {
        if (is_expr_in_list(expr, arg_name[i], nb_arg_cat, this_arg_cat)) {
            *index = i;
            return;
        }
    }

    // Case not found
    *this_arg_cat = -1;
    *index = -1;
}

static bool try_merge(int index, char **new_arg_name, int fn_id) {
    if (new_arg_name[key_arg[fn_id]] && arg_name[index][key_arg[fn_id]] &&
        strcmp(new_arg_name[key_arg[fn_id]],
               arg_name[index][key_arg[fn_id]]) != 0)
        return false;


    for (int i = 0; i < nb_arg_cat; i++) {
        if (arg_name[index][i] == NULL) {
            arg_name[index][i] = new_arg_name[i];
        } else if (new_arg_name[i]) {
            sm_warning("Possibly mixing arguments %s and %s",
                       new_arg_name[i], arg_name[index][i]);
        }
    }
    return true;
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

static void print_arg_name(FILE *out) {
    for (int i = 0; arg_cat[i]; i++)
        fprintf(out, "%16s\t", arg_cat[i]);

    fprintf(out, "\n");

    for (int i = 0; arg_name[i]; i++) {
        for (int j = 0; arg_cat[j]; j++) {
            fprintf(out, "%16s\t", arg_name[i][j]);
        }
        fprintf(out, "\n");
    }
    fprintf(out, "\n\n");
}

static void print_arg_pos(FILE *out) {
    fprintf(out,"Arg positions:\t\t\t");
    for (int j = 0; arg_cat[j]; j++) {
        fprintf(out, "%s", arg_cat[j]);
        for (int i = 0; i < (16 - strlen(arg_cat[j])); i++)
            putc(' ', stdout);
    }
    fprintf(out, "\n");

    for (int i = 0; func_name[i]; i++) {
        fprintf(out, "%24s\t", func_name[i]);

        for (int j = 0; arg_cat[j]; j++) {
            if (key_arg[i] == j) 
                fprintf(out, "\033[91m");
            fprintf(out, "%d\t\t", arg_pos[i][j]);
            if (key_arg[i] == j) 
                fprintf(out, "\033[0m");
        }
        fprintf(out, "\n");
    }
    fprintf(out, "\n");

    fprintf(out, "Ignore functions:\n");
    for (int i = 0; ignore_funcs[i]; i++)
        fprintf(out, "%s ", ignore_funcs[i]);
    fprintf(out, "\n\n");

    fprintf(out, "Variables to test:\n");
    for (int i = 0; to_test[i]; i++) {
        if (to_test[i][2] != -1) {
            fprintf(out, "func: %s, var: %s, test functions: ",
                   func_name[to_test[i][0]], arg_cat[to_test[i][1]]);
                for (int j = 2; to_test[i][j] != -1; j++)
                    fprintf(out, "%s ", func_name[to_test[i][j]]);
            putc('\n', stdout);
        } else {
            fprintf(out, "func: %s, var: %s\n", func_name[to_test[i][0]],
                   arg_cat[to_test[i][1]]);
        }
    }
    fprintf(out, "\n");
}

static void free_var_to_test()
{
    if (!all_vars_to_test_in_func)
        init_array((void *)&all_vars_to_test_in_func, (void *)&nb_all_vars_to_test_in_func);
    for (int i = 0; var_to_test[i]; i++)
        push_array((void *)&all_vars_to_test_in_func,
                   (void *)&nb_all_vars_to_test_in_func, var_to_test[i]);

    free(var_to_test);
    free(test_from_line);
    var_to_test = NULL;
    parent_if = NULL;
}

static bool is_expr_to_test(char *expr) {
    if (!expr)
        return false;

    if (!var_to_test)
        return false;

    for (int i = 0; var_to_test[i]; i++) {
        if (strcmp(expr, var_to_test[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_valid_testing_fn(int fn_id, int *test_func)
{
    for (int i = 0; test_func[i] != -1; i++) {
        if (test_func[i] == fn_id)
            return true;
    }
    return false;
}

static void is_requirement(int fn_id, struct expression *expr)
{
    char *test;
    if (!var_to_test || !is_valid_testing_fn(fn_id, test_func))
        goto exit;

    if ((test = get_arg_from_call_expr(expr,
                                       arg_pos[fn_id][var_to_test_type]))) {
        if (is_expr_to_test(test)) {
                free_var_to_test();
                free_string(test);
                return;
        }
        
        free_string(test);
    }
exit:
     for (int i = 0; to_test[i]; i++) {
        if (to_test[i][2] == fn_id) {
            if (all_vars_to_test_in_func &&
                (test = get_arg_from_call_expr(expr, arg_pos[fn_id][to_test[i][1]])) &&
                (is_expr_in_list(test, all_vars_to_test_in_func, nb_all_vars_to_test_in_func, NULL)))
                    return;

            sm_warning("Testing function %s is not run on a variable to test.",
                       func_name[fn_id]);
            return;
        }
    }
}

static void warn_if_var_to_test() {
    if (var_to_test) {
        for (int i = 0; test_from_line[i]; i++)
            sm_warning_line(test_from_line[i],
                            "Possibly not testing %s", var_to_test[0]);
        free_var_to_test();
    }
}

static struct statement *get_parent_if(struct expression *expr, bool member)
{
    struct expression *parent_expr = NULL;
    while ((parent_expr = expr_get_parent_expr(expr)) && parent_expr != expr)
        expr = parent_expr;

    struct statement *stmt = expr_get_parent_stmt(expr);
    while (stmt && stmt->parent) {
        if (stmt->parent->type == STMT_IF &&
            ((member && stmt == stmt->parent->if_true) ||
             (!member && stmt == stmt->parent->if_false))) {
            return stmt->parent;
        }
        stmt = stmt->parent;
    }
    return NULL;
}

static bool other_member_if(int fn_id, struct expression *expr)
{
    if (!var_to_test || !parent_if)
        return false;

    for (int i = 0; to_test[i]; i++) {
        if (to_test[i][0] != fn_id)
            continue;

        char *new_var_to_test = get_arg_from_call_expr(expr, arg_pos[fn_id][to_test[i][1]]);
        if (strcmp(var_to_test[0], new_var_to_test) != 0) {
            free_string(new_var_to_test);
            continue;
        }
        free_string(new_var_to_test);

        if (!comp_list(test_func, &(to_test[i][2])))
            continue;

        if (parent_if != get_parent_if(expr, false))
            continue;

        return true;
    }

    return false;
}

static void add_test_requirements(int fn_id, struct expression *expr)
{
    char *str_arg;
    if (other_member_if(fn_id, expr)) {
        int i;
        for (i = 0; test_from_line[i]; i++) {}
        test_from_line[i] = get_lineno();
        test_from_line = realloc(test_from_line, (i + 1) * sizeof(*test_from_line));
        test_from_line[i + 1] = 0;
        return;
    }

    warn_if_var_to_test();

    for (int i = 0; to_test[i]; i++) {
        if (to_test[i][0] != fn_id)
            continue;

        if (var_to_test) {
            sm_warning(
                "Testing two variables from the same func is not currently supported"
            );
            free_var_to_test();
        }

        str_arg = get_arg_from_call_expr(expr, arg_pos[fn_id][to_test[i][1]]);
        if (!str_arg) {
            sm_warning("Not assigning %s, cannot be tested", arg_cat[to_test[i][1]]);
            continue;
        }

        init_array((void ***)&var_to_test, &nb_var_to_test);
        push_array((void ***)&var_to_test, &nb_var_to_test, str_arg);


        var_to_test_type = to_test[i][1];
        test_func = &(to_test[i][2]);
        test_from_line = malloc(2 * sizeof(*test_from_line));
        test_from_line[0] = get_lineno();
        test_from_line[1] = 0;

        parent_if = get_parent_if(expr, true);
    }
}


static int find_arg_name(int fn_id, struct expression *expr) {
    if (key_arg[fn_id] == -1)
        return -1;

    char *key_param = get_arg_from_call_expr(expr, arg_pos[fn_id][key_arg[fn_id]]);

    int prev_arg_cat = key_arg[fn_id], index;
    find_previous_arg_name(key_param, &prev_arg_cat, &index);
    free_string(key_param);

    if (prev_arg_cat == key_arg[fn_id])
        return index;

    return -1;

}


static void match_func(const char *fn_name, struct expression *expr, void *_fn_id)
{
    if (is_expr_in_list(get_function(), func_name, nb_func_name, NULL) ||
        is_expr_in_list(get_function(), ignore_funcs, nb_ignore_funcs, NULL))
        return;

    if (__inline_fn)
        return;

    int fn_id = (int)(long) _fn_id;
    int index = find_arg_name(fn_id, expr);


    char **new_arg_name = NULL;

    for (int cur_arg_cat = 0; arg_cat[cur_arg_cat]; cur_arg_cat++) {
        if (arg_pos[fn_id][cur_arg_cat] == -2) continue;

        char *str_arg = get_arg_from_call_expr(expr, arg_pos[fn_id][cur_arg_cat]);
        if (!str_arg) continue;

        if (index != -1 && arg_name[index][cur_arg_cat] && 
            strcmp(arg_name[index][cur_arg_cat], str_arg) == 0) {
            free_string(str_arg);
            continue;
        }

        if (index != -1 && !arg_name[index][cur_arg_cat]) {
            arg_name[index][cur_arg_cat] = str_arg;
            continue;
        }

        int prev_arg_cat = cur_arg_cat, cur_index;
        find_previous_arg_name(str_arg, &prev_arg_cat, &cur_index);

        if (prev_arg_cat == -1) {
            // Its ok, check that it is the case for all and append
            if (index == -1) {
                if (!new_arg_name)
                    new_arg_name = calloc(nb_arg_cat, sizeof(new_arg_name[0]));
                new_arg_name[cur_arg_cat] = str_arg;
                continue;
            }

            arg_name[index][cur_arg_cat] = str_arg;
            continue;
        } else if (new_arg_name) {
            // If the argument is known but not the
            // previous, then try to merge and do as if nothing happened
            if (try_merge(cur_index, new_arg_name, fn_id)) {
                free(new_arg_name);
                new_arg_name = NULL;
            }
        }

        if (prev_arg_cat != cur_arg_cat) {
            sm_warning("Possibly mixing %s and %s",
                       arg_cat[prev_arg_cat], arg_cat[cur_arg_cat]);
            free_string(str_arg);
            continue;
        }

        if (index == -1)
            index = cur_index;
        else if (index != cur_index)
            sm_warning("Possibly mixing arguments %s and %s",
                       arg_name[cur_index][prev_arg_cat],
                       arg_name[index][cur_arg_cat]);
    }

    is_requirement(fn_id, expr);
    add_test_requirements(fn_id, expr);

    if (new_arg_name)
        push_array((void ***)&arg_name, &nb_arg_name, new_arg_name);

    if (false)
        print_arg_name(stdout);

}

static void match_assign(struct expression *expr)
{
    if (is_fake_var_assign(expr))
        return;

    if (__inline_fn)
        return;

    if (!var_to_test)
        return;

    char *right_str = stringify(expr->right);
    for (int i = 0; var_to_test[i]; i++) {
        if (strcmp(var_to_test[i], right_str) == 0) {
            push_array((void ***)&var_to_test, &nb_var_to_test,
                       stringify(expr->left));
            return;
        }
    }

    free_string(right_str);
}

static void match_func_end() {
    if (__inline_fn)
        return;

    warn_if_var_to_test();

    if (!all_vars_to_test_in_func)
        return;

    for (int i = 0; all_vars_to_test_in_func[i]; i++)
        free_string(all_vars_to_test_in_func[i]);

    free(all_vars_to_test_in_func);
    all_vars_to_test_in_func = NULL;
}

static void match_test(struct expression *expr) {
    if (__inline_fn)
        return;

    if (var_to_test && test_func[0] != -1)
        return;

    char *str_expr = stringify(expr);
    if (is_expr_to_test(str_expr)) {
        free_var_to_test();
    }
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
    if (key)
        key_arg[nb_func_name - 1] = index;
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
    key_arg = realloc(key_arg, nb_func_name * sizeof(nb_func_name));
    key_arg[nb_func_name - 1] = -1;

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

static int *parse_testing_functions(char *test_func, int *len)
{
    int *test_func_id = malloc(sizeof(*test_func_id));
    int nb_test_func_id = 1;
    char buf[varname_size + 1];

    if (!test_func) {
        *len = 0;
        test_func_id[0] = -1;
        return test_func_id;
    }

    for (;;) {
        sscanf(test_func, label, buf);
        test_func += strlen(buf);
        test_func_id = realloc(test_func_id,
                                (1 + nb_test_func_id) * sizeof(*test_func_id));
        if (!is_expr_in_list(buf, func_name, nb_func_name,
                                &test_func_id[nb_test_func_id - 1]))
            parse_error("Function %s not declared", buf);
        test_func = strstr(test_func, "or");
        if (!test_func)
            break;
        test_func += 3;
        nb_test_func_id++;
    }
    test_func_id[nb_test_func_id] = -1;

    *len = nb_test_func_id;
    return test_func_id;
}

static void add_test(char *var, char *test_func) {
    int var_id;
    int *test_func_id;
    int nb_test_func_id;

    if (!is_expr_in_list(var, arg_cat, nb_arg_cat, &var_id))
        parse_error("Variable %s not declared.", var);

    test_func_id = parse_testing_functions(test_func, &nb_test_func_id);

    for (int i = 0; sec_func[i]; i++) {
        int *line = malloc((3 + nb_test_func_id) * sizeof(*line));
        if (!is_expr_in_list(sec_func[i], func_name, nb_func_name, &line[0]))
            parse_error("Unexpected error.");

        line[1] = var_id;
        for (int j = 0; j <= nb_test_func_id; j++)
            line[2 + j] = test_func_id[j];

        push_array((void ***)&to_test, &nb_to_test, line);
    }
}

static bool parse_do_test(char *line) {
    char var_test[varname_size + 1];

    char *test_pos, *with_pos;
    test_pos = strstr(line, "test");
    with_pos = strstr(line, "with");

    if (test_pos && with_pos) {
        with_pos[0] = '\0';
        sscanf(test_pos + 5, label, var_test);
        add_test(var_test, with_pos + 5);
        return true;
    } else if (test_pos) {
        sscanf(test_pos + 5, label, var_test);
        add_test(var_test, NULL);
        return true;
    }

    return false;
}

static bool parse_ignore(char *line) {
    char buffer[varname_size + 1];
    if (1 == sscanf(line, label, buffer)) {
        push_array((void ***)&ignore_funcs, &nb_ignore_funcs, alloc_string(buffer));
        return true;
    }

    return false;
}

static bool parse_file(const char *filename) {
    init_array((void ***)&arg_cat, &nb_arg_cat);
    init_array((void ***)&func_name, &nb_func_name);
    init_array((void ***)&to_test, &nb_to_test);
    ignore_funcs = calloc(1, sizeof(*ignore_funcs));
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
    if (!parse_file(option_generic_args_file))
        return;
    if (false) print_arg_pos(stdout);

    arg_name = malloc(sizeof(*arg_name));
    arg_name[0] = NULL;
    nb_arg_name = 0;
    int i;
    for (i = 0; func_name[i]; i++)
        add_function_hook(func_name[i], match_func, (void *)(long)i);


    add_hook(match_assign, ASSIGNMENT_HOOK);
    add_hook(match_func_end, END_FUNC_HOOK);
    add_hook(match_test, CONDITION_HOOK);
}