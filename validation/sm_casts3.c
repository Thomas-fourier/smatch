#include "check_debug.h"

int frob(int var_int)
{
	signed char var_s8;
	signed char var2_s8;
	unsigned char var_u8;
	short var_s16;
	unsigned short var_u16;
	int var2_s32;
	unsigned int var_u32;
	long long var_s64;
	unsigned long long var_u64;

	if (var_int < -10 || var_int > 10)
		return 0;
	var_u32 = var_int;
	var2_s32 = var_u32;
	var_s8 = var_int;
	var2_s8 = var_u32;
	var_u8 = var_int;
	var_s16 = var_int;
	var_u16 = var_int;
	var_s64 = var_int;
	var_u64 = var_int;

	__smatch_implied(var_int);
	__smatch_implied(var_u32);
	__smatch_implied(var2_s32);
	__smatch_implied(var_s8);
	__smatch_implied(var2_s8);
	__smatch_implied(var_u8);
	__smatch_implied(var_s16);
	__smatch_implied(var_s16);
	__smatch_implied(var_u64);
	__smatch_implied(var_s64);

	return 0;
}


/*
 * check-name: smatch: casts #3
 * check-command: ./smatch -I.. sm_casts3.c
 *
 * check-output-start
sm_casts3.c:27 frob() implied: var_int = '(-10)-10'
sm_casts3.c:28 frob() implied: var_u32 = '0-10,4294967286-u32max'
sm_casts3.c:29 frob() implied: var2_s32 = '(-10)-10'
sm_casts3.c:30 frob() implied: var_s8 = '(-10)-10'
sm_casts3.c:31 frob() implied: var2_s8 = '(-10)-10'
sm_casts3.c:32 frob() implied: var_u8 = '0-10,246-255'
sm_casts3.c:33 frob() implied: var_s16 = '(-10)-10'
sm_casts3.c:34 frob() implied: var_s16 = '(-10)-10'
sm_casts3.c:35 frob() implied: var_u64 = '0-10,18446744073709551606-u64max'
sm_casts3.c:36 frob() implied: var_s64 = '(-10)-10'
 * check-output-end
 */
