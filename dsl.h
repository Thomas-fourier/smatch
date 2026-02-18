#include "smatch.h"

////////////////////////////////////////////////////////////////////////////////
//              Representation of DSL
////////////////////////////////////////////////////////////////////////////////
struct dsl_representation {
    char **arg_cat;     // category of argument (sg, nents,...)
    int nb_arg_cat;
    char **func_name;   // function name
    int nb_func_name;
    int **arg_pos;      // For each function, for each arg, position of the
                        // arg (-1 is return value, -2 not present)
    char *filename;     // name of the file of the DSL
};


#define parse_error(format, ...) do { \
    sm_warning("Parsing error in %s: " format, filename, __VA_ARGS__);\
    exit(1);\
} while (0)
#define stringify_macro(x) #x
#define varname_size 63
#define label " %" STRINGIFY(varname_size) "[a-zA-Z0-9_] "

bool is_expr_in_list(const char *expr, char **list, int len, int *index);
void push_array(void ***array, int *len, void *elt);
void init_array(void ***list, int *len);
void parse_file(const char *filename, struct dsl_representation *dsl);


////////////////////////////////////////////////////////////////////////////////
//                  Representation of the function calls
////////////////////////////////////////////////////////////////////////////////
struct calls_rep {
    const struct dsl_representation dsl;
    char ***arg_name; // name of the arguments for each category
    char **arg_name_function;
    char **arg_name_location;
    int nb_arg_name;
};
