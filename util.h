#ifndef _UTIL_H
#define _UTIL_H

#include <glob.h>
#include <stdio.h>
#include <stdlib.h>

static inline void freep(void *p) { free(*(void **)p); }
static inline void fclosep(FILE **p) { if (*p) fclose(*p); }
static inline void globfreep(glob_t *p) { globfree(p); }
#define _cleanup_(x) __attribute__((cleanup(x)))
#define _cleanup_free_ _cleanup_(freep)

#endif  /* _UTIL_H */
