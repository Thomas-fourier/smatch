#define A(__VA_OPT__)
#define B(X) __VA_OPT__(_)
#define C(X,...) __VA_OPT__(__VA_OPT__(_))
#define D(X,...) __VA_OPT__
#define E(X,...) __VA_OPT__(_
#define OK(X,...) __VA_OPT__()
#define OK2(X,...) __VA_OPT__(,(,,),)
#define F(X,...) __VA_OPT__(,(,,,)
#define OK3(X,...) __VA_OPT__(,(,,),))
#define G1(...) __VA_OPT__(##)
#define G2(...) __VA_OPT__(##,)
#define G3(...) __VA_OPT__(,##)
#define H(...) __VA_OPT__(#1)
#define OK4(X,...) __VA_OPT__(__VA_ARGS__,#X)
#define OK5(X,...) #__VA_OPT__(__VA_ARGS__,#X)
/*
 * check-name: __VA_OPT__ parsing
 * check-command: sparse -E $file
 *
 * check-output-start


 * check-output-end
 *
 * check-error-start
preprocessor/va_opt_parse.c:1:11: error: __VA_OPT__ can only appear in the expansion of a C99 variadic macro
preprocessor/va_opt_parse.c:2:14: error: __VA_OPT__ can only appear in the expansion of a C99 variadic macro
preprocessor/va_opt_parse.c:3:29: error: __VA_OPT__ may not appear in a __VA_OPT__
preprocessor/va_opt_parse.c:4:18: error: unterminated __VA_OPT__
preprocessor/va_opt_parse.c:5:18: error: unterminated __VA_OPT__
preprocessor/va_opt_parse.c:8:18: error: unterminated __VA_OPT__
preprocessor/va_opt_parse.c:10:28: error: '##' cannot appear at the ends of macro expansion
preprocessor/va_opt_parse.c:11:28: error: '##' cannot appear at the ends of macro expansion
preprocessor/va_opt_parse.c:12:29: error: '##' cannot appear at the ends of macro expansion
preprocessor/va_opt_parse.c:13:27: error: '#' is not followed by a macro parameter
 * check-error-end
 */
