#include "check_debug.h"

int frob(int x, int y, int z)
{
	if (x < -10 || x > 10)
		return 0;
	if (z < -2 || !z || z > 2)
		return 0;

	if (y) {} // force a state

	__smatch_implied(x * y);
	__smatch_real_absolute(x * y);
	__smatch_implied(y * x);
	__smatch_implied(0 * x);
	__smatch_implied(0 * y);
	__smatch_implied(x * z);
	__smatch_implied(y * z);

	return 0;
}

/*
 * check-name: smatch: multiplication
 * check-command: ./smatch -I.. sm_multiply.c
 *
 * check-output-start
sm_multiply.c:12 frob() implied: x * y = ''
sm_multiply.c:13 frob() real absolute: x * y = 's32min-s32max'
sm_multiply.c:14 frob() implied: y * x = ''
sm_multiply.c:15 frob() implied: 0 * x = '0'
sm_multiply.c:16 frob() implied: 0 * y = '0'
sm_multiply.c:17 frob() implied: x * z = '(-20)-20'
sm_multiply.c:18 frob() implied: y * z = ''
 * check-output-end
 */
