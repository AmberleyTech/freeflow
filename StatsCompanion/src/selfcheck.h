#ifndef FFS_SELFCHECK_H
#define FFS_SELFCHECK_H

/* Implemented in tests/tests.c and linked into the same binary so the
 * installed tool can self-verify on the user's machine. */

int ffs_run_tests(void);  /* --test: unit + integration suite, synthetic data */
int ffs_memcheck(void);   /* --memcheck: fail if peak RSS exceeds the budget */
int ffs_diskcheck(void);  /* --diskcheck: fail if the store exceeds budget */

#endif
