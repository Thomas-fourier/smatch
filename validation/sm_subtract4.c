#include "check_debug.h"

int frob(int a, unsigned long b)
{
	if (a < 0)
		return 0;
	if (b >= a)
		return 0;

	__smatch_implied(a);
	__smatch_implied(b);
	__smatch_implied(a - b);

	return 0;
}

/*
 * check-name: smatch: subtract #4
 * check-command: ./smatch -I.. sm_subtract4.c
 *
 * check-output-start
sm_subtract4.c:10 frob() implied: a = '1-s32max'
sm_subtract4.c:11 frob() implied: b = '0-2147483646'
sm_subtract4.c:12 frob() implied: a - b = '1-s32max'
 * check-output-end
 */
