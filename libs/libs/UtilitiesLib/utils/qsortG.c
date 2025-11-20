/***********************************************************************

 NAME
      iqsort() - improved quick sort

 SYNOPSIS
      #include <qsort.h>

      void iqsort(
           void *base,
           size_t nel,
           size_t size,
           int (*compar)(const void *, const void *)
      );

 DESCRIPTION
      iqsort() is the implementation of a robust quicksort algorithm
      as described in part ALGORITHM. It sorts a table of data in place.

           base        Pointer to the element at the base of the table.

           nel         Number of elements in the table.

           size        Size of each element in the table.

           compar      Name of the comparison function, which is called with
                       two arguments that point to the elements being
                       compared.  The function passed as compar must return
                       an integer less than, equal to, or greater than zero,
                       according to whether its first argument is to be
                       considered less than, equal to, or greater than the
                       second.  strcmp() uses this same return convention
                       (see strcmp(3C)).

 NOTES
      The pointer to the base of the table should be of type pointer-to-
      element, and cast to type pointer-to-void.

 ALGORITHM

      Jon L. Bentley & M. Douglas McIlroy,

      Engineering a Sort Function,
      Software-Practice and Experience, 23:11 (Nov. 1993) 1249-1265

      Summary:

      We recount the history of a new qsort function for a C library.
      Our function is clearer, faster and more robust than existing sorts.
   
***********************************************************************/

#include <qsortG.h>

#ifdef TESTOUT
#define TEST_OUT(X) X
#else
#define TEST_OUT(X)
#endif

#ifndef min
#define min(a,b)                 ((a)>(b) ? (b) : (a))
#endif

typedef long GWORD; // Renamed to not conlict with windef.h's incorrect definition of a "WORD"
#define W sizeof(GWORD)   /* must be a power of 2 */
#define SWAPINIT(a, es) swaptype =  \
    (((a - (char*)0) | es) % W) ? 2 : (es > W) ? 1 : 0  

/* 
   The strange formula to check data size and alignment works even 
   on Cray computers, where plausible code such as 
   ((long) a| es) % sizeof(long) fails because the least significant
   part of a byte address occupies the most significant part of a long.
*/

#define exch(a, b, t) (t = a, a = b, b = t)
#define swap(a, b)                                \
   swaptype != 0 ? swapfunc(a, b, es, swaptype) : \
   (void) exch(*(GWORD*)(a), *(GWORD*)(b), t)

#define vecswap(a, b, n) if (n > 0) swapfunc(a, b, n, swaptype)

#define PVINIT(pv, pm)                      \
   if (swaptype != 0) pv = a, swap(pv, pm); \
   else pv = (char*)&v, v = *(GWORD*)pm

/*--------------------------------------------------------------------*/

static void swapfunc(char *a, char *b, size_t n, int swaptype)
{
   if (swaptype <= 1) {
      GWORD t;
      for ( ; n > 0; a += W, b += W, n -= W)
         exch(*(GWORD*)(a), *(GWORD*)(b), t);
   } else {
      char t;
      for ( ; n > 0; a += 1, b += 1, n -= 1)
         exch(*a, *b, t);
   }
}

static char *med3(char *a, char *b, char *c, 
		  int (*cmp)(const void *, const void *))
{
   return cmp(a, b) < 0 ?
	 (cmp(b, c) <= 0 ? b : cmp(a, c) < 0 ? c : a)
       : (cmp(b, c) >= 0 ? b : cmp(a, c) > 0 ? c : a);
}

/*--------------------------------------------------------------------*/

void qsortG_checked(
           void *base,
           size_t n,
		   size_t recursion_check,
           size_t es,
           int (*cmp)(const void *, const void *)
		   )
{
	char   *a, *pa, *pb, *pc, *pd, *pl, *pm, *pn, *pv;
#ifdef DEBUG_INTERMEDIATE_VALUES
	char *pa0, *pb0, *pc0, *pd0, *pn2, *pm01, *pm02;
	size_t s2, s3, s4, s5;
#else
#	define pn2 pn
#	define s2 s
#	define s3 s
#	define s4 s
#	define s5 s
#endif
	int    r, swaptype;
	GWORD   t, v;
	size_t s;

	a = (char *) base;
	SWAPINIT( a, es );
	TEST_OUT(printf("swaptype %d\n", swaptype));
	if (n < 7) {     /* Insertion sort on smallest arrays */
		TEST_OUT(printf("Insertion Sort\n"));
		for (pm = a + es; pm < a + n*es; pm += es)
			for (pl = pm; pl > a && cmp(pl-es, pl) > 0; pl -= es)
				swap(pl, pl-es);
		return;
	}

	assert(recursion_check>0); // Infinite recursion!  Probably bad comparator, returned V < V when &V==&V or something...

	pm = a + (n/2)*es;    /* Small arrays, middle element */
#ifdef DEBUG_INTERMEDIATE_VALUES
	pm01 = pm;
#endif
	if (n > 7) {
		pl = a;
		pn = a + (n-1)*es;
		if (n > 40) {     /* Big arrays, pseudomedian of 9 */
			s = (n/8)*es;
			pl = med3(pl, pl+s, pl+2*s, cmp);
			pm = med3(pm-s, pm, pm+s, cmp);
#ifdef DEBUG_INTERMEDIATE_VALUES
			pm02 = pm;
#endif		
			pn = med3(pn-2*s, pn-s, pn, cmp);
		} 
		pm = med3(pl, pm, pn, cmp);  /* Mid-size, med of 3 */
	}
	PVINIT(pv, pm);       /* pv points to partition value */
	pa = pb = a;
	pc = pd = a + (n-1)*es;
#ifdef DEBUG_INTERMEDIATE_VALUES
	// Save values for debugging
	pa0 = pa; pb0 = pb; pc0=pc; pd0 = pd;
#endif
	for (;;) {
		while (pb <= pc && (r = cmp(pb, pv)) <= 0) {
			if (r == 0) {
				swap(pa, pb);
				pa += es;
			}
			pb += es;
		}
		while (pc >= pb && (r = cmp(pc, pv)) >= 0) {
			if (r == 0) {
				swap(pc, pd);
				pd -= es;
			}
			pc -= es;
		}
		if (pb > pc)
			break;
		swap(pb, pc);
		pb += es;
		pc -= es;
	}
	pn2 = a + n*es;
	s5 = min(pa-a,  pb-pa   ); vecswap(a,  pb-s5, s5);
	s2 = min((size_t)(pd-pc), pn2-pd-es); vecswap(pb, pn2-s2, s2);
	if ((s3 = pb-pa) > es)
	{
		//assert(!(a==base && s3/es==n)); // Infinite recursion!  Probably bad comparator, returned V < V when &V==&V
		  // Actually, this seems to sometimes fire in normal operation?
		qsortG_checked(a, s3/es, recursion_check-1, es, cmp);
	}
	// TODO: Turn this into tail-recursion for better Xbox performance
	if ((s4 = pd-pc) > es)
		qsortG_checked(pn2-s4, s4/es, recursion_check-1, es, cmp);
}

void qsortG(
			void *base,
			size_t n,
			size_t es,
			int (*cmp)(const void *, const void *)
			)
{
	qsortG_checked(base, n, n*2, es, cmp);
}


static char *med3_s(char *a, char *b, char *c,
				  int (*cmp)(void *, const void *, const void *), void * context)
{
	return cmp(context, a, b) < 0 ?
		(cmp(context, b, c) < 0 ? b : cmp(context, a, c) < 0 ? c : a)
		: (cmp(context, b, c) > 0 ? b : cmp(context, a, c) > 0 ? c : a);
}


extern void qsortG_s( void *base,
						  size_t n,
						  size_t es,					
						  int (*cmp)(void *, const void *, const void *),
						  void * context)
{
	char   *a, *pa, *pb, *pc, *pd, *pl, *pm, *pn, *pv;
	int    r, swaptype;
	GWORD   t, v;
	size_t s;

	a = (char *) base;
	SWAPINIT( a, es );
	TEST_OUT(printf("swaptype %d\n", swaptype));
	if (n < 7) {     /* Insertion sort on smallest arrays */
		TEST_OUT(printf("Insertion Sort\n"));
		for (pm = a + es; pm < a + n*es; pm += es)
			for (pl = pm; pl > a && cmp(context, pl-es, pl) > 0; pl -= es)
				swap(pl, pl-es);
		return;
	}
	pm = a + (n/2)*es;    /* Small arrays, middle element */
	if (n > 7) {
		pl = a;
		pn = a + (n-1)*es;
		if (n > 40) {     /* Big arrays, pseudomedian of 9 */
			s = (n/8)*es;
			pl = med3_s(pl, pl+s, pl+2*s, cmp, context);
			pm = med3_s(pm-s, pm, pm+s, cmp, context);
			pn = med3_s(pn-2*s, pn-s, pn, cmp, context);
		} 
		pm = med3_s(pl, pm, pn, cmp, context);  /* Mid-size, med of 3 */
	}
	PVINIT(pv, pm);       /* pv points to partition value */
	pa = pb = a;
	pc = pd = a + (n-1)*es;
	for (;;) {
		while (pb <= pc && (r = cmp(context, pb, pv)) <= 0) {
			if (r == 0) { swap(pa, pb); pa += es; }
			pb += es;
		}
		while (pc >= pb && (r = cmp(context, pc, pv)) >= 0) {
			if (r == 0) { swap(pc, pd); pd -= es; }
			pc -= es;
		}
		if (pb > pc) break;
		swap(pb, pc);
		pb += es;
		pc -= es;
	}
	pn = a + n*es;
	s = min(pa-a,  pb-pa   ); vecswap(a,  pb-s, s);
	s = min((size_t)(pd-pc), pn-pd-es); vecswap(pb, pn-s, s);
	if ((s = pb-pa) > es) qsortG_s(a,    s/es, es, cmp, context);
	if ((s = pd-pc) > es) qsortG_s(pn-s, s/es, es, cmp, context);

}


//////////////////////////////////////////////////////////////////////////
// common compare functions

int strCmp(const char *const *s1, const char *const *s2)
{
	return stricmp(*s1, *s2);
}

int ptrCmp(const void **pptr1, const void **pptr2)
{
	if (*pptr1 < *pptr2)
		return -1;
	return *pptr1 > *pptr2;
}

int intCmp(const int *a, const int *b)
{
	return ( *a - *b );
}

int cmpU32(const U32 *i, const U32 *j)
{
	return *i > *j ? 1 : *i < *j ? -1 : 0;
}

int cmpF32(const F32 *i, const F32 *j)
{
	return *i > *j ? 1 : *i < *j ? -1 : 0;
}

int cmpU64(const U64 *i, const U64 *j)
{
	return *i > *j ? 1 : *i < *j ? -1 : 0;
}

