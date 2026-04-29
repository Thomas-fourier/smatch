_Static_assert(__builtin_strlen("a") == 1, "A");	// CIE, true
_Static_assert(__builtin_strlen("a") == 2, "B");	// CIE, false
char *s = "b";
_Static_assert(__builtin_strlen(s) == 1, "C");		// not a CIE
/*
 * check-name: builtin-strlen
 * check-command: sparse $file
 *
 * check-error-start
builtin-strlen.c:2:38: error: static assertion failed: "B"
builtin-strlen.c:4:36: error: bad constant expression
 * check-error-end
 */
