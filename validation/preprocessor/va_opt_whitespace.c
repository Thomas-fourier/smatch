#define A(X,...) [__VA_OPT__( X)][ __VA_OPT__(X)]
A(1,_)
/*
 * check-name: __VA_OPT__ whitespace
 * check-command: sparse -E $file
 *
 * check-output-start

[1][ 1]
 * check-output-end
 *
 * check-error-start
 * check-error-end
 */
