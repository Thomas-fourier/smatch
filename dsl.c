#include "dsl.h"
#include "smatch.h"
#include "smatch_extra.h"
#include <ctype.h>

static const char *filename;

/* Tests if a string is in a list. If it is, write index with the index in the
list if found */
bool is_expr_in_list(const char *expr, char **list, int len, int *index)
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

void push_array(void ***array, int *len, void *elt) {
    (*array)[*len] = elt;
    (*len)++;
    *array = realloc(*array, (*len + 1) * sizeof(**array));
    (*array)[*len] = NULL;
}

void init_array(void ***list, int *len) {
    *list = malloc(sizeof(**list));
    (*list)[0] = NULL;
    *len = 0;
}

static bool parse_decl(const char *line, struct dsl_representation *dsl)
{
    char buffer[varname_size + 1];
    const char *var_pos;
    int i;
    var_pos = strstr(line, "var");
    if (!var_pos)
        return false;

    for (;line < var_pos; line++) {
        if (!isspace(*line))
            return false;
    }
    line += 3;
    if (!isspace(*line))
        return false;

    do {
        while (isspace(*line))
            line++;
    
        sscanf(line - 1, label, buffer);

        if (is_expr_in_list(buffer, dsl->arg_cat, dsl->nb_arg_cat, &i))
            parse_error("Double declaration of %s", buffer);

        push_array((void ***)&dsl->arg_cat, &dsl->nb_arg_cat, alloc_string(buffer));

        for (i = 0; dsl->func_name[i]; i++) {
            dsl->arg_pos[i] = realloc(dsl->arg_pos[i], dsl->nb_arg_cat * sizeof(*dsl->arg_pos[i]));
            dsl->arg_pos[i][dsl->nb_arg_cat - 1] = -2;
        }
        line = strchr(line, ',');
    } while (line++);

    return true;
}

static bool add_to_arg_pos(char *expr, char *line, int pos,
                           struct dsl_representation *dsl)
{
    int index;

    if (strcmp("_", expr) == 0)
        return true;

    if (!is_expr_in_list(expr, dsl->arg_cat, dsl->nb_arg_cat, &index))
        parse_error("Argument %s not declared.", expr);

    if (dsl->arg_pos[dsl->nb_func_name - 1][index] != -2)
        parse_error("Argument %s used multiple times in %s", expr, line);

    dsl->arg_pos[dsl->nb_func_name - 1][index] = pos;
    return true;
}

static bool parse_call(char *line, struct dsl_representation *dsl)
{
    char buffer[varname_size + 1];
    char *current;
    char *last;
    int i;
    if (1 != sscanf(line, label, buffer))
        parse_error("Impossible to parse line %s", line);

    if (is_expr_in_list(buffer, dsl->func_name, dsl->nb_func_name, &i))
        parse_error("Function %s defined multiple times line: %s", buffer, line);

    current = strchr(line, '(');
    if (!current)
        parse_error("Line %s could not be parsed", line);

    push_array((void ***)&dsl->func_name, &dsl->nb_func_name,
               alloc_string(buffer));
    dsl->arg_pos = realloc(dsl->arg_pos,
                           dsl->nb_func_name * sizeof(*dsl->arg_pos));
    dsl->arg_pos[dsl->nb_func_name - 1] =
                malloc(dsl->nb_arg_cat * sizeof(**dsl->arg_pos));

    for (i = 0; i < dsl->nb_arg_cat; i++)
        dsl->arg_pos[dsl->nb_func_name - 1][i] = -2;

    while (isspace(current[1]))
        current++;

    // If there are no arguments
    if (current[1] == ')')
        return true;

    i = 0;
    do {
        current++;
        if (1 != sscanf(current, label, buffer))
            parse_error("Could not read variable in %s", current);

        if (!add_to_arg_pos(buffer, line, i, dsl))
            return false;
        i++;
        last = current;
    } while ((current = strchr(last, ',')));

    if (!strchr(last, ')'))
        parse_error("Parenthesis not closed %s", line);

    return true;
}

static bool parse_equal(char *line, struct dsl_representation *dsl)
{
    char *sep;
    char ret_val[varname_size + 1];
    if (!(sep = strchr(line, '=')))
        return false;

    if (!parse_call(sep + 1, dsl))
        parse_error("Weird line '%s'", line);

    else if (1 != sscanf(line, label, ret_val))
        parse_error("Could not parse affectation statement %s", line);

    add_to_arg_pos(ret_val, line, -1, dsl);

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


void init_dsl(struct dsl_representation *dsl)
{
    init_array((void ***)&dsl->arg_cat, &dsl->nb_arg_cat);
    init_array((void ***)&dsl->func_name, &dsl->nb_func_name);
    dsl->arg_pos = NULL;
    return;
}

void parse_file(const char *_filename, struct dsl_representation *res)
{
    filename = _filename;
    res->filename = alloc_string(filename);
    FILE *file = fopen(filename, "r");
    if (!file)
        parse_error("File %s could not be opened.", filename);

    char *line = NULL;
    size_t line_size;
    char *comment_start;
    init_dsl(res);

    while (-1 != getline(&line, &line_size, file)) {
        if ((comment_start = strstr(line, "//")))
            comment_start[0] = '\0';

        if (isempty(line)) continue;
        if (parse_decl(line, res)) continue;
        if (parse_equal(line, res)) continue;
        if (parse_call(line, res)) continue;
        parse_error("File %s: line %s could not be parsed", filename, line);
    }

    free(line);
    return;
}

void print_dsl_representation(FILE *out, const struct dsl_representation *dsl)
{
    fprintf(out, "%s\n", dsl->filename);
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

