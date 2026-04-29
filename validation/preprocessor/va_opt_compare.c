#define OK1(X,...) __VA_OPT__(X =)
#define OK1(X,...) __VA_OPT__(X =)
#define OK2(X,...) #__VA_OPT__(X =)
#define OK2(X,...) #__VA_OPT__(X =)
#define BAD1(X,...) __VA_OPT__(X)
#define BAD1(X,...) __VA_OPT__(_)
#define BAD2(X,...) __VA_OPT__(,)
#define BAD2(X,...) ,
#define BAD3(X,...) __VA_OPT__(,)
#define BAD3(X,...) #__VA_OPT__(,)
/*
 * check-name: __VA_OPT__ comparison
 * check-command: sparse -E $file
 *
 * check-output-start


 * check-output-end
 *
 * check-error-start
preprocessor/va_opt_compare.c:6:9: warning: preprocessor token BAD1 redefined
preprocessor/va_opt_compare.c:5:9: this was the original definition
preprocessor/va_opt_compare.c:8:9: warning: preprocessor token BAD2 redefined
preprocessor/va_opt_compare.c:7:9: this was the original definition
preprocessor/va_opt_compare.c:10:9: warning: preprocessor token BAD3 redefined
preprocessor/va_opt_compare.c:9:9: this was the original definition
 * check-error-end
 */
