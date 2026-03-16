#define A(X,Y,...) __VA_ARGS__,Y,X
#define B(X,Y...) Y
/*
 * check-name: -dM handling of varargs
 * check-command: sparse -E -dM $file | tail -2
 *
 * check-output-start
#define A(X,Y,...) __VA_ARGS__,Y,X
#define B(X,Y...) Y
 * check-output-end
 */
