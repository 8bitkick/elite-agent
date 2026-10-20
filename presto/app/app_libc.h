// Included first in every file of the app build (-include): stderr is no stream here, so the
// simulator's debug lines (fprintf(stderr, ...): app_libc.c) need newlib's reentrancy structure
// no more than its streams.
#include <stdio.h>
#undef stderr
#define stderr ((FILE *) 2)        // (not a stream: fprintf takes it to nowhere)
