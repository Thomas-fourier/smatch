#define A(X,Y,...) __VA_ARGS__,Y,X
#define B(X,Y...) Y
#define C(...) __VA_OPT__(1 #__VA_ARGS__) #__VA_OPT__(1 __VA_ARGS__)
/*
 * check-name: -dM handling of varargs
 * check-command: sparse -E -dM $file | tail -3
 *
 * check-output-start
#define A(X,Y,...) __VA_ARGS__,Y,X
#define B(X,Y...) Y
#define C(...) __VA_OPT__(1 #__VA_ARGS__) #__VA_OPT__(1 __VA_ARGS__)
 * check-output-end
 */
