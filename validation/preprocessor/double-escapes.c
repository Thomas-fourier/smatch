#define A(x) x, x
#define B(x,y) x ## y, x ## y
static char *p[] = {A("\\")};
static char *q[] = {B("\\", )};
/*
 * check-name: double escapes
 * check-command: sparse $file
 */
