#include "check_debug.h"

#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)

struct inner {
	int a, b, c;
};

struct outer1 {
	int foo, bar, baz;
	struct inner member;
};

struct outer2 {
	struct outer1 mid;
	int x, y, z;
};

void frob(struct inner *in)
{
	void *p;
	void *q;

	p = &(struct outer2){ .x = 2 };
	q = &(&(struct outer2){ .x = 2 })->y;

	__smatch_implied(p);
	__smatch_implied(q);
}

/*
 * check-name: smatch: mtag #12
 * check-command: ./smatch -I.. sm_mtag12.c
 *
 * check-output-start
sm_mtag12.c:27 frob() implied: p = '4096-ptr_max'
sm_mtag12.c:28 frob() implied: q = '4096-ptr_max'
 * check-output-end
 */
