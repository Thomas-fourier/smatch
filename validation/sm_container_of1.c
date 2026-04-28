#include "check_debug.h"

#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	((type *)(__mptr - offsetof(type, member))); })

struct inner {
	int a, b, c;
};

struct mid {
	int foo, bar, baz;
	struct inner member;
};

struct outer2 {
	int x, y, z;
	struct mid mid;
};

int frob(struct inner *in)
{
	struct mid *p1;
	struct outer2 *p2;

	p1 = container_of(in, struct mid, member);
	p2 = container_of(in, struct outer2, mid.member);

	in->a = 100;
	p1->foo = 999;
	p2->x = 42;

	return 0;
}

void func(void)
{
	struct outer2 outer = {};
	struct outer2 two = {};
	struct inner *in = &two.mid.member;

	__smatch_implied(outer.x);
	__smatch_implied(outer.mid.foo);
	frob(&outer.mid.member);
	__smatch_implied(outer.x);
	__smatch_implied(outer.mid.foo);

	__smatch_implied(in->a);
	frob(in);
	__smatch_implied(two.x);
	__smatch_implied(in->a);
	__smatch_implied(two.mid.member.a);
}

/*
 * check-name: smatch: container_of #1
 * check-command: ./smatch -I.. sm_container_of1.c
 *
 * check-output-start
sm_container_of1.c:44 func() implied: outer.x = '0'
sm_container_of1.c:45 func() implied: outer.mid.foo = '0'
sm_container_of1.c:47 func() implied: outer.x = '42'
sm_container_of1.c:48 func() implied: outer.mid.foo = '999'
sm_container_of1.c:50 func() implied: in->a = '0'
sm_container_of1.c:52 func() implied: two.x = '42'
sm_container_of1.c:53 func() implied: in->a = '100'
sm_container_of1.c:54 func() implied: two.mid.member.a = '100'
 * check-output-end
 */
