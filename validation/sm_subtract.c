#include "check_debug.h"

int frob(int a, int b, int c, int d, int e, int f)
{
	if (a < -10 || a > 10)
		return 0;
	if (b) {} // force a state

	if (d < -2 || !d || d > 2)
		return 0;
	if (e < 2 || e > 5)
		return 0;
	if (f < -5 || f > -2)
		return 0;

	__smatch_note("test: unknown");
	__smatch_implied(a - b);
	__smatch_implied(b - d);
	__smatch_implied(b - a);
	__smatch_note("test: absolute unknown");
	__smatch_real_absolute(a - b);
	__smatch_real_absolute(a - c);
	__smatch_note("test: zero tests");
	__smatch_implied(a - 0);
	__smatch_note("test: 0 - (-10)-10 = (-10)-10");
	__smatch_implied(0 - a);
	__smatch_note("test: 0 - 2-5 = (-5)-(-2)");
	__smatch_implied(0 - e);
	__smatch_note("test: 0 - (-5)-(-2) = 2-5");
	__smatch_implied(0 - f);
	__smatch_note("test: unknown");
	__smatch_implied(0 - b);
	__smatch_implied(0 - c);
	__smatch_note("test: (-10)-10 - (-2)-2 = (-12)-12");
	__smatch_implied(a - d);
	__smatch_note("test: (-10)-10 - 2-5 = (-15)-8");
	__smatch_implied(a - e);
	__smatch_note("test: (-2)-2 - 2-5 = (-7)-0");
	__smatch_implied(d - e);
	__smatch_note("test: (-5)-(-2) - 2-5 = (-10)-(-4)");
	__smatch_implied(f - e);
	__smatch_note("test: (-5)-(-2) - (-10)-10 = (-15)-8");
	__smatch_implied(f - a);
	__smatch_note("test: (-5)-(-2) - (-2)-(-1),1-2 = (-7)-0");
	__smatch_implied(f - d);
	__smatch_note("test: 2-5 - (-5)-(-2) = 4-10");
	__smatch_implied(e - f);
	__smatch_note("test: (-2)-(-1),1-2 - 2 = (-4)-(-3),(-1)-0");
	__smatch_implied(d - 2);

	return 0;
}

/*
 * check-name: smatch: subtract
 * check-command: ./smatch -I.. sm_subtract.c
 *
 * check-output-start
sm_subtract.c:16 frob() test: unknown
sm_subtract.c:17 frob() implied: a - b = ''
sm_subtract.c:18 frob() implied: b - d = ''
sm_subtract.c:19 frob() implied: b - a = ''
sm_subtract.c:20 frob() test: absolute unknown
sm_subtract.c:21 frob() real absolute: a - b = 's32min-s32max'
sm_subtract.c:22 frob() real absolute: a - c = 's32min-s32max'
sm_subtract.c:23 frob() test: zero tests
sm_subtract.c:24 frob() implied: a - 0 = '(-10)-10'
sm_subtract.c:25 frob() test: 0 - (-10)-10 = (-10)-10
sm_subtract.c:26 frob() implied: 0 - a = '(-10)-10'
sm_subtract.c:27 frob() test: 0 - 2-5 = (-5)-(-2)
sm_subtract.c:28 frob() implied: 0 - e = '(-5)-(-2)'
sm_subtract.c:29 frob() test: 0 - (-5)-(-2) = 2-5
sm_subtract.c:30 frob() implied: 0 - f = '2-5'
sm_subtract.c:31 frob() test: unknown
sm_subtract.c:32 frob() implied: 0 - b = ''
sm_subtract.c:33 frob() implied: 0 - c = ''
sm_subtract.c:34 frob() test: (-10)-10 - (-2)-2 = (-12)-12
sm_subtract.c:35 frob() implied: a - d = '(-12)-12'
sm_subtract.c:36 frob() test: (-10)-10 - 2-5 = (-15)-8
sm_subtract.c:37 frob() implied: a - e = '(-15)-8'
sm_subtract.c:38 frob() test: (-2)-2 - 2-5 = (-7)-0
sm_subtract.c:39 frob() implied: d - e = '(-7)-0'
sm_subtract.c:40 frob() test: (-5)-(-2) - 2-5 = (-10)-(-4)
sm_subtract.c:41 frob() implied: f - e = '(-10)-(-4)'
sm_subtract.c:42 frob() test: (-5)-(-2) - (-10)-10 = (-15)-8
sm_subtract.c:43 frob() implied: f - a = '(-15)-8'
sm_subtract.c:44 frob() test: (-5)-(-2) - (-2)-(-1),1-2 = (-7)-0
sm_subtract.c:45 frob() implied: f - d = '(-7)-0'
sm_subtract.c:46 frob() test: 2-5 - (-5)-(-2) = 4-10
sm_subtract.c:47 frob() implied: e - f = '4-10'
sm_subtract.c:48 frob() test: (-2)-(-1),1-2 - 2 = (-4)-(-3),(-1)-0
sm_subtract.c:49 frob() implied: d - 2 = '(-4)-(-3),(-1)-0'
 * check-output-end
 */
