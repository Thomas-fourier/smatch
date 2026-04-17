#include "check_debug.h"

int frob(unsigned int a, int b)
{
	if (a != 0 && a != 20 && a != 40)
		return 0;
	if (b < 4 || b > 5)
		return 0;
	__smatch_implied(a);
	__smatch_implied(b);
	__smatch_implied(a + b);

	return 0;
}

/*
 * check-name: smatch: math #3
 * check-command: ./smatch -I.. sm_math3.c
 *
 * check-output-start
sm_math3.c:9 frob() implied: a = '0,20,40'
sm_math3.c:10 frob() implied: b = '4-5'
sm_math3.c:11 frob() implied: a + b = '4-5,24-25,44-45'
 * check-output-end
 */
