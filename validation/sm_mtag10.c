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
	struct outer1 *p1;

	__smatch_implied(offsetof(struct outer2, mid.member));
	p1 = (void *)8UL;
	__smatch_implied(p1);
	__smatch_implied(&p1);
	__smatch_implied(&*p1);
	__smatch_implied(*&p1);
	__smatch_implied(&p1->bar);
}

/*
 * check-name: smatch: mtag #10
 * check-command: ./smatch -I.. sm_mtag10.c
 *
 * check-output-start
sm_mtag10.c:23 frob() implied: (&(0)->mid.member) = '12'
sm_mtag10.c:25 frob() implied: p1 = '8'
sm_mtag10.c:26 frob() implied: &p1 = '5095278607788875776'
sm_mtag10.c:27 frob() implied: &*p1 = '8'
sm_mtag10.c:28 frob() implied: p1 = '8'
sm_mtag10.c:29 frob() implied: &p1->bar = '12'
 * check-output-end
 */
