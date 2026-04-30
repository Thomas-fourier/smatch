#include "check_debug.h"

#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	((type *)(__mptr - offsetof(type, member))); })

struct inner {
	int a, b, c;
};

struct outer1 {
	int x, y, z;
	struct inner member;
};

struct outer2 {
	struct inner member;
	int x, y, z;
};

int frob(struct inner *in, struct inner *in2)
{
	struct outer1 *p, *p2;
	struct outer1 *null = 0;

	if (!in)
		return 0;

	in2 = (void *)-12UL;

	__smatch_implied(&null->member);
	__smatch_implied(offsetof(struct outer1, member));
	p = container_of(in, struct outer1, member);
	__smatch_implied(in);
	__smatch_implied(p);

	p2 = container_of(in2, struct outer2, member);
	__smatch_implied(in2);
	__smatch_implied((int)(unsigned long)p2);

	return 0;
}

/*
 * check-name: smatch: subtract #6
 * check-command: ./smatch -I.. sm_subtract6.c
 *
 * check-output-start
sm_subtract6.c:33 frob() implied: &null->member = '12'
sm_subtract6.c:34 frob() implied: (&(0)->member) = '12'
sm_subtract6.c:36 frob() implied: in = '1-u64max'
sm_subtract6.c:37 frob() implied: p = '4096-ptr_max'
sm_subtract6.c:40 frob() implied: in2 = '18446744073709551604'
sm_subtract6.c:41 frob() implied: p2 = '(-12)'
 * check-output-end
 */
