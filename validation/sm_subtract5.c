#include "check_debug.h"

int frob(int a, unsigned long b)
{

	if (a < 0 || a > 63)
		return;

	while (a > b) {
		a -= b;
		__smatch_implied(a);
	}

	return 0;
}

/*
 * check-name: smatch: subtract #5
 * check-command: ./smatch -I.. sm_subtract5.c
 *
 * check-output-start
sm_subtract5.c:11 frob() implied: a = '1-63'
 * check-output-end
 */
