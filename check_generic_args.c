#include "smatch.h"
#include "smatch_extra.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int my_id;

struct func_arg {
    const char *fn;
    int arg_pos;
};

#define parse_error(...) sm_warning("Parsing error: " __VA_ARGS__)
#define label " %[a-zA-Z_] "

// Initially created
static char **arg_cat;      // category of argument (sg, nents,...)
static int nb_arg_cat;
static char **func_name;    // function name
static int nb_func_name;
static int **arg_pos;       // For each function, for each arg, position of the
                            // arg  (-1 is return value, -2 not present)


// Maintained and updated
static char ***arg_name; // name of the arguments for each category
static int nb_arg_name;


static void push_array(void ***array, int *len, void *elt) {
    (*array)[*len] = elt;
    (*len)++;
    *array = realloc(*array, (*len + 1) * sizeof(**array));
    (*array)[*len] = NULL;
}

static char *stringify(struct expression *expr) {
    char *res;
    if (expr->type == EXPR_DEREF && expr->member) {
        res = malloc(strlen(expr->member->name) + 2);
        sprintf(res, ".%s", expr->member->name);
    } else {
        res = expr_to_str(expr);
    }
    return res;
}

/* Tests if a string is in a list. If it is, write index with the index in the
list if found */
static bool is_expr_in_list(char *expr, char **list, int len, int *index)
{
    int i;
    for (i = 0; i < len; i++) {
        if (list[i] && strcmp(expr, list[i]) == 0) {
            *index = i;
            return true;
        }
    }
    return false;
}

/* Find string in a matrix of strings, if no
 *
 */
static void find_previous_arg_name(char *expr, int *this_arg_cat, int *index) {

    int i;
    for (i = 0; arg_name[i]; i++) {
        if (is_expr_in_list(expr, arg_name[i], nb_arg_cat, this_arg_cat)) {
            *index = i;
            return;
        }
    }

    // Case not found
    *this_arg_cat = -1;
    *index = i;
}

static void try_merge(int index, char **new_arg_name) {
    for (int i = 0; i < nb_arg_cat; i++) {
        if (arg_name[index][i] == NULL) {
            arg_name[index][i] = new_arg_name[i];
        } else if (new_arg_name[i]) {
            sm_warning("Possibly mixing arguments %s and %s",
                       new_arg_name[i], arg_name[index][i]);
        }
    }
}

static char *get_arg_from_call_expr(struct expression *expr, int arg_position) {
        struct expression *arg;
        if (arg_position == -1) {
            struct expression *parent = expr_get_parent_expr(expr);
            if (parent->type == EXPR_ASSIGNMENT && parent->left)
                arg = parent->left;
            else // Maybe warning as well
                return NULL;
        } else {
            arg = get_argument_from_call_expr(expr->args, arg_position);
        }

        return stringify(arg);
}

static void print_arg_name() {
    for (int i = 0; arg_cat[i]; i++) {
        printf("%s\t\t\t", arg_cat[i]);
        for (int j = 0; arg_name[j]; j++) {
            printf("%s\t\t\t", arg_name[j][i]);
        }
        printf("\n");
    }
}

static void print_arg_pos() {
    printf("\t\t");
    for (int j = 0; func_name[j]; j++) {
        printf("%s\t", func_name[j]);
    }
    printf("\n");

    for (int i = 0; arg_cat[i]; i++) {
        printf("%s\t\t", arg_cat[i]);
        for (int j = 0; func_name[j]; j++) {
            printf("%d\t\t\t", arg_pos[j][i]);
        }
        printf("\n");
    }
}


static void match_func(const char *fn_name, struct expression *expr, void *_fn_id)
{
    int index = -1;
    int fn_id = (int)(long) _fn_id;


    char **new_arg_name = NULL;

    for (int cur_arg_cat = 0; arg_cat[cur_arg_cat]; cur_arg_cat++) {
        if (arg_pos[fn_id][cur_arg_cat] == -2) continue;

        char *str_arg = get_arg_from_call_expr(expr, arg_pos[fn_id][cur_arg_cat]);
        if (!str_arg) continue;


        int prev_arg_cat, cur_index;
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
        } else if (new_arg_name) { // If the argument is known but not the
            // previous, then try to merge and do as if nothing happened
            try_merge(cur_index, new_arg_name);
            free(new_arg_name);
            new_arg_name = NULL;
        }

        if (prev_arg_cat != cur_arg_cat) {
            sm_warning("Possibly mixing %s and %s",
                       arg_cat[prev_arg_cat], arg_cat[cur_arg_cat]);
            free_string(str_arg);
            continue;
        }

        if (index == -1)
            index = cur_index;

        if (index != cur_index) {
            sm_warning("Possibly mixing arguments");
        }
    }

    if (new_arg_name)
        push_array((void ***)&arg_name, &nb_arg_name, new_arg_name);

    if (true)
        print_arg_name();

}

static bool parse_decl(char *line)
{
    char buffer[64];
    int i;
    if (1 != sscanf(line, "var "label, buffer))
        return false;

    if (is_expr_in_list(buffer, arg_cat, nb_arg_cat, &i)) {
        parse_error("Double declaration of %s", buffer);
        return true;
    }

    push_array((void ***)&arg_cat, &nb_arg_cat, alloc_string(buffer));

    for (int i = 0; func_name[i]; i++) {
        arg_pos[i] = realloc(arg_pos[i], nb_arg_cat * sizeof(*arg_pos[i]));
        arg_pos[i][nb_arg_cat - 1] = -2;
    }
    return true;
}

static bool add_to_arg_pos(char *expr, char *line, int pos)
{
    int index;

    if (strcmp("_", expr) == 0)
        return true;

    if (!is_expr_in_list(expr, arg_cat, nb_arg_cat, &index)) {
        parse_error("Argument %s not declared.", expr);
        return false;
    }
    if (arg_pos[nb_func_name - 1][index] != -2) {
        parse_error("Argument %s used multiple time in %s", expr, line);
        return false;
    }
    arg_pos[nb_func_name - 1][index] = pos;
    return true;
}

static bool parse_call(char *line)
{
    char buffer[64];
    char *current;
    char *last;
    int i;
    if (1 != sscanf(line, label, buffer)) {
        parse_error("Impossible to parse line %s", line);
        return false;
    }

    if (is_expr_in_list(buffer, func_name, nb_func_name, &i)) {
        parse_error("Function %s defined multiple times", buffer);
        return false;
    }

    push_array((void ***)&func_name, &nb_func_name, alloc_string(buffer));
    arg_pos = realloc(arg_pos, nb_func_name * sizeof(*arg_pos));
    arg_pos[nb_func_name - 1] = malloc(nb_arg_cat * sizeof(**arg_pos));

    for (i = 0; i < nb_arg_cat; i++)
        arg_pos[nb_func_name - 1][i] = -2;


    current = strchr(line, '(');
    if (!current)
        parse_error("Line %s could not be parsed", line);
    i = 0;
    do {
        current++;
        if (1 != sscanf(current, label, buffer))
            parse_error("Could not read variable in %s", current);
        if (!add_to_arg_pos(buffer, line, i))
            return false;
        i++;
        last = current;
    } while ((current = strchr(last, ',')));

    if (!strchr(last, ')'))
        parse_error("Parenthesis not closed %s", line);

    return true;
}

static bool parse_equal(char *line)
{
    char *sep;
    char ret_val[64];
    if (!(sep = strchr(line, '=')))
        return false;

    if (!parse_call(sep + 1)) {
        parse_error("Weird line '%s'", line);
        return false;
    }
    sscanf(line, label, ret_val);
    add_to_arg_pos(ret_val, line, -1);

    return true;
}

static void init_array(void ***list, int *len) {
    *list = malloc(sizeof(**list));
    (*list)[0] = NULL;
    *len = 0;
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


static void parse_file(const char *filename) {
    init_array((void ***)&arg_cat, &nb_arg_cat);
    init_array((void ***)&func_name, &nb_func_name);
    arg_pos = NULL;

    FILE *file = fopen(filename, "r");
    if (!file) {
        parse_error("File %s could not be opened.", filename);
        return;
    }
    char *line = NULL;
    size_t line_size;
    while (-1 != getline(&line, &line_size, file)) {
        if (isempty(line)) continue;
        if (parse_decl(line)) continue;
        if (parse_equal(line)) continue;
        if (parse_call(line)) continue;
    }
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
    parse_file(option_generic_args_file);
    print_arg_pos();

    arg_name = malloc(sizeof(*arg_name));
    arg_name[0] = NULL;
    nb_arg_name = 0;
    int i;
    for (i = 0; func_name[i]; i++) {
        add_function_hook(func_name[i], match_func, (void *)(long)i);
    }
}