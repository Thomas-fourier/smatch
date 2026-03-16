#define A(X,Y,...) __VA_ARGS__,Y,X
/*
 * check-name: -dM handling of varargs
 * check-command: sparse -E -dM $file | tail -1
 *
 * check-output-start
#define A(X,Y,...) __VA_ARGS__,Y,X
 * check-output-end
 */
