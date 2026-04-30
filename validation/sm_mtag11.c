#include "check_debug.h"

#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)

struct inner {
	int a, b, c;
};

struct mid {
	int foo, bar, baz;
	struct inner member;
};

struct outer2 {
	struct mid mid;
	int x, y, z;
};

void frob(struct mid *mid)
{
	struct outer2 out2;

	__smatch_implied(&mid->bar);
	__smatch_implied(&out2);
	__smatch_implied(&out2.x);
	__smatch_implied(&out2.mid.bar);
	__smatch_implied(&out2.x - &out2.mid.bar);
	__smatch_implied((unsigned long)&out2.x - (unsigned long)&out2.mid.bar);
}

/*
 * check-name: smatch: mtag #11
 * check-command: ./smatch -I.. sm_mtag11.c
 *
 * check-output-start
sm_mtag11.c:23 frob() implied: &mid->bar = '4096-ptr_max'
sm_mtag11.c:24 frob() implied: &out2 = '1496362378149650432'
sm_mtag11.c:25 frob() implied: &out2.x = '1496362378149650456'
sm_mtag11.c:26 frob() implied: &out2.mid.bar = '1496362378149650436'
sm_mtag11.c:27 frob() implied: &out2.x - &out2.mid.bar = '5'
sm_mtag11.c:28 frob() implied: &out2.x - &out2.mid.bar = '20'
 * check-output-end
 */
