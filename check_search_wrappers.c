#include "smatch.h"
#include "smatch_extra.h"
#include "dsl.h"
#include "stringification.h"

static struct dsl_representation *apis;
char **func_args = NULL;
int nb_func_args = 0;
char *ret = NULL;
char *ret_func_wrapped;
int nb_api_call = 0;
char *wrapper_found = NULL;
char **wrapper_parameters = NULL;
int nb_wrapper_parameters;
int function_start;


static int id;

static void match_func_start(struct symbol *sym)
{
    if (__inline_fn)
        return;

    nb_api_call = 0;

    struct symbol *arg;
    FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
        if (!arg->ident) {
            continue;
        }

        nb_func_args++;
        func_args = realloc(func_args, sizeof(func_args) * nb_func_args);
        func_args[nb_func_args - 1] = arg->ident->name;

    } END_FOR_EACH_PTR(arg);

    function_start = get_lineno();
}

#define line_length 100
static char *str_of_wrapper_params(char *fn, char **params, int nb_params)
{
    static char res[line_length];
    int offset = 0;

    if (params[0])
        offset += snprintf(res + offset, line_length - offset, "%s = ",
                           params[0]);

    offset += snprintf(res + offset, line_length - offset, "%s(", fn);
    
    for (int i = 1; i <= nb_params; i++)
        if (params[i])
            offset += snprintf(res + offset, line_length - offset, "%s, ",
                               params[i]);
        else
            offset += snprintf(res + offset, line_length - offset, "_, ");

    if (nb_params >= 1)
        offset -= 2;

    res[offset] = ')';
    res[offset + 1] = '\0';

    return res;
}

static void match_func_end(void) {
    if (__inline_fn)
        return;

    nb_func_args = 0;
    free(func_args);
    func_args = 0;
    free(ret);
    ret = 0;
    if (wrapper_found && get_lineno() - function_start < 30)
        sm_warning_line(function_start, "Possible wrapper found %s %s",
                        wrapper_found,
                        str_of_wrapper_params(get_function(),
                                              wrapper_parameters,
                                              nb_wrapper_parameters));
    if (ret_func_wrapped != wrapper_found)
        free(ret_func_wrapped);
    ret_func_wrapped = 0;
    free(wrapper_found);
    wrapper_found = NULL;
}

static bool interseting_function(char *fn, int *api, int *func)
{
    for (int i = 0; i < nb_generic_args_file; i++) {
        for (int j = 0; j < apis[i].nb_func_name; j++) {
            if (0 == strcmp(apis[i].func_name[j], fn)) {
                *api = i;
                *func = j;
                return true;
            }
        }
    }
    return false;
}

static void add_possible_wrapper(char *wrapper, char **args, int nb_args)
{
    if (wrapper_found) {
        free(wrapper);
        free(args);
        return;
    }
    free(wrapper_found);
    free(wrapper_parameters);
    wrapper_found = wrapper;
    wrapper_parameters = args;
    nb_wrapper_parameters = nb_args;
}

static inline char *get_arg_type_from_call(int callee_index, int api, int func)
{
    for (int i = 0; i < apis[api].nb_arg_cat; i++)
        if (apis[api].arg_pos[func][i] == callee_index)
            return apis[api].arg_cat[i];
    
    return NULL;
}

static void match_func_call(struct expression *expr)
{
    int api;
    int api_func;
    if (__inline_fn || (nb_api_call >= 2))
        return;

    // If we are in an implementation, ignore and continue
    if (get_function() && 
        interseting_function(get_function(), &api, &api_func))
        return;

    char *func_wrapped = expr_to_str(expr->fn);
    if (!func_wrapped)
        return;

    if (!interseting_function(func_wrapped, &api, &api_func)) {
        free(func_wrapped);
        return;
    }

    nb_api_call++;

    struct statement *parent = expr_get_parent_stmt(expr);
    int nb_args = expression_list_size(expr->args);
    char **arguments = calloc(nb_func_args + 1, sizeof(*arguments));
    int nb_common_arg = 0;
    for (int i = 0; i < nb_args; i++) {
        char *arg = get_arg_from_call_expr(expr, i);
        if (!arg)
            continue;
        for (int j = 0; j < nb_func_args; j++) {
            if (0 == strcmp(func_args[j], arg)) {
                arguments[j + 1] = get_arg_type_from_call(i, api, api_func);
                nb_common_arg += 1;
                break;
            }
        }
        free(arg);
    }
    if (parent && parent->type == STMT_RETURN)
        arguments[0] = get_arg_type_from_call(-1, api, api_func);
    if ((float)nb_common_arg / (float)nb_args > 0.45 ||
        (parent && parent->type == STMT_RETURN)) {
        add_possible_wrapper(func_wrapped, arguments, nb_func_args);
    }

    free(ret);

    if ((ret = get_arg_from_call_expr(expr, -1))) {
        if (ret_func_wrapped != wrapper_found)
            free(ret_func_wrapped);
        ret_func_wrapped = func_wrapped;
        if (!wrapper_parameters) {
            wrapper_parameters = arguments;
            nb_wrapper_parameters = nb_func_args;
        }
    }
}

static void match_return(struct expression *expr)
{
    if (!ret)
        return;

    if (__inline_fn || (nb_api_call > 2))
        return;

    char *this_ret = expr_to_str(expr);
    if (!this_ret)
        return;

    if (strcmp(ret, this_ret) == 0) {
        int api, api_func;
        if (!wrapper_parameters) {
            wrapper_parameters = calloc(1, sizeof(*wrapper_parameters));
            nb_wrapper_parameters = 1;
        }

        if (!wrapper_found)
            wrapper_found = ret_func_wrapped;

        if (interseting_function(wrapper_found, &api, &api_func))
            wrapper_parameters[0] = get_arg_type_from_call(-1, api, api_func);

        ret_func_wrapped = NULL;
    }

    free(this_ret);
}

void check_search_wrappers(int _id)
{
    if (!option_wrappers)
        return;

    if (!option_generic_args_file) {
        sm_warning("No spec given to find wrappers");
        return;
    }

    apis = malloc(sizeof(*apis) * nb_generic_args_file);
    for (int i = 0; i < nb_generic_args_file; i++)
        parse_file(option_generic_args_file[i], &apis[i]);

    id = _id;

    add_hook(match_func_start, FUNC_DEF_HOOK);
    add_hook(match_func_end, END_FUNC_HOOK);
    add_hook(match_func_call, FUNCTION_CALL_HOOK);
    add_hook(match_return, RETURN_HOOK);
}
