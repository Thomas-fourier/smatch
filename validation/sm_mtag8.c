#include "check_debug.h"

int frob(void)
{
	return 0;
}

typedef int (fn_p)(void);
void func(void)
{
	struct outer2 outer = {};
	void *p = frob;
	fn_p *fn = frob;
	int array[20];
	int *array_p1 = array;
	int *array_p2 = &array;
	char *str = "123";

	__smatch_implied(&frob);
	__smatch_implied(frob);
	__smatch_implied(p);
	__smatch_implied(&p);
	__smatch_implied(fn);
	__smatch_implied(&fn);
	__smatch_implied(array);
	__smatch_implied(array_p1);
	__smatch_implied(array_p2);
	__smatch_implied("123");
	__smatch_implied(&"123");
	__smatch_implied(str);
}

/*
 * check-name: smatch: mtag #8
 * check-command: ./smatch -I.. sm_mtag8.c
 *
 * check-output-start
sm_mtag8.c:19 func() implied: &frob = '2203940198711365632'
sm_mtag8.c:20 func() implied: frob = '2203940198711365632'
sm_mtag8.c:21 func() implied: p = '2203940198711365632'
sm_mtag8.c:22 func() implied: &p = '736213126887456768'
sm_mtag8.c:23 func() implied: fn = '2203940198711365632'
sm_mtag8.c:24 func() implied: &fn = '7495960107394805760'
sm_mtag8.c:25 func() implied: array = '14964629169045504'
sm_mtag8.c:26 func() implied: array_p1 = '14964629169045504'
sm_mtag8.c:27 func() implied: array_p2 = '14964629169045504'
sm_mtag8.c:28 func() implied: "123" = '8211539974290595840'
sm_mtag8.c:29 func() implied: &"123" = '8211539974290595840'
sm_mtag8.c:30 func() implied: str = '8211539974290595840'
 * check-output-end
 */
