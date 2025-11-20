#ifndef _QSORTG_H
#define _QSORTG_H

extern void qsortG(void *base, size_t nmemb, size_t size,
           int (*compare)(const void *, const void *));
extern void qsortG_s(void *base, size_t nmemb, size_t size,
				   int (*compare)(void *, const void *, const void *), void * context);

// common compare functions
int strCmp(const char *const *s1, const char *const *s2);
int ptrCmp(const void **pptr1, const void **pptr2);
int intCmp(const int *a, const int *b);
int cmpU32(const U32 *i, const U32 *j);
int cmpF32(const F32 *i, const F32 *j);
int cmpU64(const U64 *i, const U64 *j);

#endif
