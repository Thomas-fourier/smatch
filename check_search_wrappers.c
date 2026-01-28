#include "smatch.h"
#include "smatch_extra.h"
#include "dsl.h"
#include "stringification.h"

static struct dsl_representation *apis;
char **func_args = NULL;
int nb_func_args = 0;
char *ret = NULL;
char *func_wrapped;
bool found = false;
char *wrapper_found = NULL;


static int id;

static void match_func_start(struct symbol *sym)
{
    if (__inline_fn)
        return;

    struct symbol *arg;
    FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
        if (!arg->ident) {
            continue;
        }

        nb_func_args++;
        func_args = realloc(func_args, sizeof(func_args) * nb_func_args);
        func_args[nb_func_args - 1] = arg->ident->name;

    } END_FOR_EACH_PTR(arg);
}

static void match_func_end(void) {
    if (__inline_fn)
        return;

    nb_func_args = 0;
    free(func_args);
    func_args = 0;
    free(ret);
    ret = 0;
    if (func_wrapped) {
        free(func_wrapped);
        func_wrapped = 0;
    }
    if (found && wrapper_found)
        sm_warning("Possible wrapper found %s", wrapper_found);
    found = false;
    free(wrapper_found);
    wrapper_found = NULL;
}

static bool interseting_function(char *fn)
{
    for (int i = 0; i < nb_generic_args_file; i++) {
        for (int j = 0; j < apis[i].nb_func_name; j++) {
            if (0 == strcmp(apis[i].func_name[j], fn))
                return true;
        }
    }
    return false;
}

static void add_possible_wrapper(char *wrapper)
{
    sm_warning("add possible wrapper %s", wrapper);
    if (found) {
        free(wrapper_found);
        wrapper_found = NULL;
    } else {
        found = true;
        wrapper_found = wrapper;
    }
    return;
}

static void match_func_call(struct expression *expr)
{
    if (__inline_fn)
        return;

    char *new_func_wrapped = expr_to_str(expr->fn);
    if (!interseting_function(new_func_wrapped)) {
        free(new_func_wrapped);
        return;
    }
    // Here we should probably do something about two functions in the same
    // function… let's see what happens
    // That was a valid concern indeed
    struct statement *parent = expr_get_parent_stmt(expr);
    if (parent->type == STMT_RETURN) {
        add_possible_wrapper(new_func_wrapped);
        return;
    }

    free(func_wrapped);
    func_wrapped = new_func_wrapped;

    int nb_args = expression_list_size(expr->args);
    int nb_common_arg = 0;
    for (int i = 0; i < nb_args; i++) {
        char *arg = get_arg_from_call_expr(expr, i);
        for (int j = 0; j < nb_func_args; j++) {
            if (strcmp(func_args[j], arg))
                nb_common_arg += 1;
        }
        free(arg);
    }
    if ((float)nb_common_arg / (float)nb_args > 0.45) {
        add_possible_wrapper(func_wrapped);
        return;
    }
    
    if (ret)
        free(ret);

    ret = get_arg_from_call_expr(expr, -1);
}

static void match_return(struct expression *expr)
{
    if (!ret)
        return;

    char *this_ret = expr_to_str(expr);
    if (!this_ret)
        return;

    if (strcmp(ret, this_ret) == 0)
        add_possible_wrapper(func_wrapped);

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
