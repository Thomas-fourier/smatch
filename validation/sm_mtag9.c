#include "check_debug.h"

int frob(void)
{
	return 0;
}

typedef int (fn_p)(void);
void func(int idx)
{
	int array[20];
	int *array_p1 = array;
	int *array_p2 = &array;

	if (idx < 0 || idx > 3)
		return;

	__smatch_implied(array);
	__smatch_implied(&array[1]);
	__smatch_implied(&array[idx]); // FIXME <-- not ideal
	__smatch_implied((unsigned long)&array[1] - (unsigned long)&array[0]);
	__smatch_implied((unsigned long)&array[idx] - (unsigned long)&array[0]);
	__smatch_implied(array_p1);
	__smatch_implied(array_p2);
}

/*
 * check-name: smatch: mtag #9
 * check-command: ./smatch -I.. sm_mtag9.c
 *
 * check-output-start
sm_mtag9.c:18 func() implied: array = '3848759869566418944'
sm_mtag9.c:19 func() implied: &array[1] = '3848759869566418948'
sm_mtag9.c:20 func() implied: &array[idx] = '4096-ptr_max'
sm_mtag9.c:21 func() implied: &array[1] - &array[0] = '4'
sm_mtag9.c:22 func() implied: &array[idx] - &array[0] = ''
sm_mtag9.c:23 func() implied: array_p1 = '3848759869566418944'
sm_mtag9.c:24 func() implied: array_p2 = '3848759869566418944'
 * check-output-end
 */
