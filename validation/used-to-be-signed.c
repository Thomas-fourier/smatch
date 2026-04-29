void error(void);
int check(void);

static void positive_tests(unsigned int val)
{
	unsigned int ret = check();

	if (ret < 0)
		error();

	if (0 > ret)
		error();

	if (ret >= 0)
		/* Do stuff */;

	if (0 <= ret)
		/* Do stuff */;
}

static void negative_tests(unsigned int val)
{
	if (val < 0 || val > 42)
		error();

	if (0 > val || 42 < val)
		error();

	if (val >= 0 && val < 42)
		/* Do stuff */;

	if (0 <= val && 42 > val)
		/* Do stuff */;
}

/*
 * check-name: used-to-be-signed
 *
 * check-error-start
used-to-be-signed.c:8:19: warning: unsigned value that used to be signed checked against zero?
used-to-be-signed.c:6:33: signed value source
used-to-be-signed.c:11:17: warning: unsigned value that used to be signed checked against zero?
used-to-be-signed.c:6:33: signed value source
used-to-be-signed.c:14:20: warning: unsigned value that used to be signed checked against zero?
used-to-be-signed.c:6:33: signed value source
used-to-be-signed.c:17:18: warning: unsigned value that used to be signed checked against zero?
used-to-be-signed.c:6:33: signed value source
 * check-error-end
 */
