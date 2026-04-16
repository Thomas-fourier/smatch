#include "check_debug.h"

int frob(int x, int y, int z)
{
	if (x < -10 || x > 10)
		return 0;
	if (z < -4 || z > -2)
		return 0;

	if (y) {} // force a state

	__smatch_implied(x);
	__smatch_implied(x / y);
	__smatch_real_absolute(x / y);
	__smatch_implied(y / x);
	__smatch_real_absolute(y / x);
	__smatch_implied(0 / x);
	__smatch_implied(0 / y);
	if (x > 4)
		__smatch_implied(x / z);

	return 0;
}


/*
 * check-name: smatch: divide
 * check-command: ./smatch -I.. sm_divide.c
 *
 * check-output-start
sm_divide.c:12 frob() implied: x = '(-10)-10'
sm_divide.c:13 frob() implied: x / y = ''
sm_divide.c:14 frob() real absolute: x / y = '(-10)-10'
sm_divide.c:15 frob() implied: y / x = ''
sm_divide.c:16 frob() real absolute: y / x = 's32min-s32max'
sm_divide.c:17 frob() implied: 0 / x = '0'
sm_divide.c:18 frob() implied: 0 / y = '0'
sm_divide.c:20 frob() implied: x / z = '(-5)-(-1)'
 * check-output-end
 */
