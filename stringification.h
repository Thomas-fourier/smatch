#include "smatch.h"

char *stringify(struct expression *expr);
bool is_cast(struct expression *expr);
char *get_arg_from_call_expr(struct expression *expr, int arg_position);
