#include "check_debug.h"

int frob(unsigned int a, int b)
{
	unsigned int ures;
	int sres;

	if (a > 10)
		return 0;
	if ((b < 0 && b != -5) || b > 12)
		return;
	__smatch_implied(a);
	__smatch_implied(b);
	__smatch_implied(a - b);
	__smatch_implied((int)(a - b));
	ures = a - b;
	sres = a - b;
	__smatch_implied(ures);
	__smatch_implied(sres);

	return 0;
}

/*
 * check-name: smatch: subtract #2
 * check-command: ./smatch -I.. sm_subtract2.c
 *
 * check-output-start
sm_subtract2.c:12 frob() implied: a = '0-10'
sm_subtract2.c:13 frob() implied: b = '(-5),0-12'
sm_subtract2.c:14 frob() implied: a - b = '0-15,4294967284-u32max'
sm_subtract2.c:15 frob() implied: (a - b) = '(-12)-15'
sm_subtract2.c:18 frob() implied: ures = '0-15,4294967284-u32max'
sm_subtract2.c:19 frob() implied: sres = '(-12)-15'
 * check-output-end
 */
