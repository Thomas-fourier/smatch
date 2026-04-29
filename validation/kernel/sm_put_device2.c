#include <linux/container_of.h>

#include "../../check_debug.h"

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

int frob(struct inner *in);
int frob(struct inner *in)
{
	struct outer1 *p1;
	struct outer2 *p2;

	p1 = container_of(in, struct outer1, member);
	p2 = container_of(p1, struct outer2, mid);

	p1->foo = 999;
	p2->x = 42;

	return 0;
}

void func(void);
void func(void)
{
	struct outer2 outer = {};

	__smatch_implied(outer.x);
	__smatch_implied(outer.mid.foo);
	frob(&outer.mid.member);
	__smatch_implied(outer.x);
	__smatch_implied(outer.mid.foo);
}

/*
 * check-name: smatch: put_device() #2
 * check-command: validation/kernel/build.sh sm_put_device2.c
 *
 * check-output-start
sm_put_device2.c:39 func() implied: outer.x = '0'
sm_put_device2.c:40 func() implied: outer.mid.foo = '0'
sm_put_device2.c:42 func() implied: outer.x = '42'
sm_put_device2.c:43 func() implied: outer.mid.foo = '999'
 * check-output-end
 */
