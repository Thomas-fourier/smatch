#define B(X) 1
// don't screw unexpanded __VA_ARGS__ on prior __VA_OPT__
#define A(...) __VA_OPT__(1) A##__VA_ARGS__
A(B(_))
// tests for skipping __VA_OPT__ don't care if expanded __VA_ARGS__
// has been already consumed
#define C(...) [__VA_ARGS__ __VA_OPT__(1)]
C(_)
C()
// don't cannibalize unexpanded __VA_ARGS__ too early
#define E(X)
#define D(...) A##__VA_ARGS__ R __VA_OPT__(1)
D(E(_))
// check that parser clears seen_va_opt on failure exit
#define BAD(...) __VA_OPT__(,) #1
#define F(...) A##__VA_ARGS__ R __VA_OPT__(1)
F(E(_))
/*
 * check-name: __VA_ARGS__ cannibalization with __VA_OPT__
 * check-command: sparse -E $file
 *
 * check-output-start

1 AB(_)
[_ 1]
[]
AE(_) R
AE(_) R
 * check-output-end
 *
 * check-error-start
preprocessor/va_opt2.c:15:32: error: '#' is not followed by a macro parameter
 * check-error-end
 */
