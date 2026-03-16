#define A(1)
#define B(__VA_ARGS__)
#define C(X,Y,X)
/*
 * check-name: macro arguments validation
 * check-command: sparse -E $file
 *
 * check-output-start


 * check-output-end
 *
 * check-error-start
preprocessor/bad-args.c:1:11: error: "1" may not appear in macro parameter list
preprocessor/bad-args.c:2:11: error: __VA_ARGS__ can only appear in the expansion of a C99 variadic macro
preprocessor/bad-args.c:3:15: error: duplicate macro parameter "X"
 * check-error-end
 */
