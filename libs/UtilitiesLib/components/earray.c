// earray.c - provides yet another type of expandable pArray
// these arrays are differentiated in that you generally declare them as
// MyStruct** earray; and access them like a normal pArray of pointers
// NOTE - EArrays are now threadsafe

#include "earray.h"
#include <string.h>

#include "wininclude.h"


#ifndef EXTERNAL_TEST
#include "MemoryMonitor.h"
#include "sysutil.h"
#include "memcheck.h"
#include "utils.h"
#endif

#include "strings_opt.h"
#include "timing.h"
#include "referencesystem.h"
#include "textparser.h"
#include "textparserUtils.h"
#include "objPath.h"
#include "sharedmemory.h"
#include "estring.h"

#include "structNet.h"

#include "mathutil.h"
#include "zutils.h"
#include "crypt.h"
#include "endian.h"
#include "earray_inline.h"

#include "earray_c_ast.h"
#include "qsortG.h"
#include "tokenstore_inline.h"

#include "structInternals.h"
#include "cmdparse.h"
#include "ThreadSafeMemoryPool.h"

#include "MemTrack.h"
#include "structInternals.h"
#include "fastAtoi.h"
#include "GlobalTypes.h"

static const char *spPoolReportingName = "PooledEarrays";

AUTO_RUN_ANON(memBudgetAddMapping("eaIndexedStashTable", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("PooledEarrays", BUDGET_GameSystems););

//earray memory functions should look like this:
//blockype = _NORMAL_BLOCK
//void* malloc_timed(size_t size,int blockType, const char *filename, int linenumber); b
//void *realloc_timed(void *p, size_t size,int blockType, const char *filename, int linenumber)
//void free_timed(void *p,int blockType); 


typedef void* (*EarrayMallocFunc)(size_t size,int blockType, const char *filename, int linenumber);
typedef void* (*EarrayReallocFunc)(void *p, size_t size,int blockType, const char *filename, int linenumber);
typedef void (*EarrayFreeFunc)(void *p,int blockType);

void* pEAMalloc_Assert(size_t size,int blockType, const char *filename, int linenumber)
{
	assertmsg(0, "Someone is trying to do an earray allocation before Earray_InitSystem is called, this is illegal");
	return NULL;
}

void* pEARealloc_Assert(void *p, size_t size,int blockType, const char *filename, int linenumber)
{
	assertmsg(0, "Someone is trying to do an earray reallocation before Earray_InitSystem is called, this is illegal");
	return NULL;
}

void pEAFree_Assert(void *p,int blockType)
{
	assertmsg(0, "Someone is trying to do an earray free before Earray_InitSystem is called, this is illegal");
}

static EarrayMallocFunc pEAMalloc_Internal = pEAMalloc_Assert;
static EarrayReallocFunc pEARealloc_Internal = pEARealloc_Assert;
static EarrayFreeFunc pEAFree_Internal = pEAFree_Assert;

#define pEAMalloc(size) pEAMalloc_Internal(size, _NORMAL_BLOCK, caller_fname,line)
#define pEARealloc(p, size) pEARealloc_Internal(p, size, _NORMAL_BLOCK, caller_fname,line)
#define pEAFree(p) pEAFree_Internal(p, _NORMAL_BLOCK)


//EAPOOLING_MINSIZE and EAPOOLING_MAXSIZE must be powers of 2, obviously. They are in bytes + header size,
//where header size is the largest header size between ea, ea32 and ea64. So presumably whatever pool holds 7-element ea64s holds
//14-element ea32s, etc.
#define EAPOOLING_MINSIZE 64
#define EAPOOLING_MINSIZE_SHIFT 6
STATIC_ASSERT((1 << EAPOOLING_MINSIZE_SHIFT) == EAPOOLING_MINSIZE)

#define EAPOOLING_MAXSIZE 2048
#define EAPOOLING_MAXSIZE_SHIFT 11
STATIC_ASSERT((1 << EAPOOLING_MAXSIZE_SHIFT) == EAPOOLING_MAXSIZE)

#define EAPOOLING_NUMPOOLS (EAPOOLING_MAXSIZE_SHIFT - EAPOOLING_MINSIZE_SHIFT + 1)

#define EAPOOLING_NOT_POOLED_USE_HEAP EAPOOLING_NUMPOOLS

#define EAPOOLING_POOLSIZE 256

static ThreadSafeMemoryPool *spEarrayPools[EAPOOLING_NUMPOOLS];


typedef struct EAPoolingBlock
{
	union
	{
		size_t iFiller;
		struct
		{
			U8 iPoolNum;
			int iMemTrackIndex;
		};
	};

	char data[1];
} EAPoolingBlock;
#define POOLING_BLOCK_HEADER_SIZE OFFSETOF(EAPoolingBlock, data)

static size_t  sPoolHeaderSize = 0;


static int EAPoolingGetPoolNum(size_t iRawSize)
{
	int iBit;
	
	if (iRawSize <= sPoolHeaderSize + EAPOOLING_MINSIZE)
	{
		return 0;
	}

	iRawSize -= (sPoolHeaderSize);
	iBit = highBitIndex((U32)iRawSize - 1) + 1;

	if (iBit > EAPOOLING_MAXSIZE_SHIFT)
	{
		return EAPOOLING_NOT_POOLED_USE_HEAP;
	}

	return iBit - EAPOOLING_MINSIZE_SHIFT;
}


//returns the size of data allocated for this pool index, not counting the internal pool header
static int EAPooling_GetDataBlockSize(int iPoolNum)
{
	return (U32)sPoolHeaderSize + (1 << ( iPoolNum + EAPOOLING_MINSIZE_SHIFT ));
}

/*
AUTO_RUN;
void EaPoolTest(void)
{
	int i;

	sPoolHeaderSize = MAX(EARRAY32_HEADER_SIZE, EARRAY64_HEADER_SIZE);
	sPoolHeaderSize = MAX(EARRAY_HEADER_SIZE, sPoolHeaderSize);

	printf("Pool header size: %d\n", sPoolHeaderSize);

	for (i = 0; i < EAPOOLING_NUMPOOLS; i++)
	{
		printf("Pool %d size: %d bytes\n", i, EAPooling_GetDataBlockSize(i));
	}

	for (i = 1 ; i < 5000; i++)
	{
		printf("%d bytes: pool %d\n", i, EAPoolingGetPoolNum(i));
	}
}*/

static int sPoolReportingSizes[EAPOOLING_NUMPOOLS] = {0};


void ReportMemPoolOperation(int iPoolNum, bool bAllocation)
{
	memTrackUpdateStatsByName(spPoolReportingName, sPoolReportingSizes[iPoolNum], bAllocation ? -sPoolReportingSizes[iPoolNum] : sPoolReportingSizes[iPoolNum], 0);
}

static void* EAPooling_Malloc(size_t size,int blockType, const char *filename, int linenumber)
{
	int iPoolNum = EAPoolingGetPoolNum(size);
	EAPoolingBlock *pBlock;
	if (iPoolNum == EAPOOLING_NOT_POOLED_USE_HEAP)
	{
		pBlock = malloc_timed(size + POOLING_BLOCK_HEADER_SIZE, _NORMAL_BLOCK, filename, linenumber);
	}
	else
	{
		ReportMemPoolOperation(iPoolNum, true);
		pBlock = threadSafeMemoryPoolAlloc(spEarrayPools[iPoolNum]);
		pBlock->iMemTrackIndex = memTrackUpdateStatsByName(filename, linenumber, sPoolReportingSizes[iPoolNum], 1);
	}

	pBlock->iPoolNum = iPoolNum;


	return pBlock->data;

}

static void EAPooling_Free(void *p,int blockType)
{
	EAPoolingBlock *pOldBlock = (EAPoolingBlock*)(((char*)p) - POOLING_BLOCK_HEADER_SIZE);
	int iOldPoolNum = pOldBlock->iPoolNum;
	
	assert(iOldPoolNum >= 0 && iOldPoolNum <= EAPOOLING_NUMPOOLS);

	if (iOldPoolNum == EAPOOLING_NOT_POOLED_USE_HEAP)
	{
		free_timed(pOldBlock, blockType);
	}
	else
	{
		ReportMemPoolOperation(iOldPoolNum, false);
		memTrackUpdateStatsBySlotIdx(pOldBlock->iMemTrackIndex, -sPoolReportingSizes[iOldPoolNum], -1);
		threadSafeMemoryPoolFree(spEarrayPools[iOldPoolNum], pOldBlock);
	}
}



static void *EAPooling_Realloc(void *p, size_t size,int blockType, const char *filename, int linenumber)
{
	EAPoolingBlock *pOldBlock = (EAPoolingBlock*)(((char*)p) - POOLING_BLOCK_HEADER_SIZE);
	EAPoolingBlock *pNewBlock;
	int iNewPoolNum = EAPoolingGetPoolNum(size);
	int iOldPoolNum;
	
	assert(pOldBlock->iPoolNum >= 0 && pOldBlock->iPoolNum <= EAPOOLING_NUMPOOLS);
	
	iOldPoolNum = (int)(pOldBlock->iPoolNum);

	if (iNewPoolNum == EAPOOLING_NOT_POOLED_USE_HEAP)
	{
		//want to end up with a malloc... so may need to do a realloc, depending on our old block num
		if (iOldPoolNum == EAPOOLING_NOT_POOLED_USE_HEAP)
		{
			pNewBlock = realloc_timed(pOldBlock, size + POOLING_BLOCK_HEADER_SIZE, blockType, filename, linenumber);
			return &pNewBlock->data;
		}

		pNewBlock = malloc_timed(size + POOLING_BLOCK_HEADER_SIZE, blockType, filename, linenumber);
		pNewBlock->iPoolNum = EAPOOLING_NOT_POOLED_USE_HEAP;
		memcpy(pNewBlock->data, pOldBlock->data, EAPooling_GetDataBlockSize(iOldPoolNum));

		ReportMemPoolOperation(iOldPoolNum, false);
		memTrackUpdateStatsBySlotIdx(pOldBlock->iMemTrackIndex, -sPoolReportingSizes[iOldPoolNum], -1);

		threadSafeMemoryPoolFree(spEarrayPools[iOldPoolNum], pOldBlock);
		return pNewBlock->data;
	}

	//same size block, not malloc, just return the block that already exists
	if (iOldPoolNum == iNewPoolNum)
	{
		return p;
	}

	pNewBlock = threadSafeMemoryPoolAlloc(spEarrayPools[iNewPoolNum]);
	pNewBlock->iPoolNum = iNewPoolNum;

	//shrinking... (might be from malloc down to pool)
	if (iOldPoolNum > iNewPoolNum)
	{
		memcpy(pNewBlock->data, pOldBlock->data, EAPooling_GetDataBlockSize(iNewPoolNum));
	}
	else
	{
		memcpy(pNewBlock->data, pOldBlock->data, EAPooling_GetDataBlockSize(iOldPoolNum));
	}

	ReportMemPoolOperation(iNewPoolNum, true);
	pNewBlock->iMemTrackIndex = memTrackUpdateStatsByName(filename, linenumber, sPoolReportingSizes[iNewPoolNum], 1);


	EAPooling_Free(p, blockType);
	return pNewBlock->data;


}


static void Earray_InitPooling(void)
{
	int i;

	sPoolHeaderSize = MAX(EARRAY32_HEADER_SIZE, EARRAY64_HEADER_SIZE);
	sPoolHeaderSize = MAX(EARRAY_HEADER_SIZE, sPoolHeaderSize);

	for (i = 0; i < EAPOOLING_NUMPOOLS; i++)
	{
		char poolName[128];
		sprintf(poolName, "Earrays_le%d", 1 << ( i + EAPOOLING_MINSIZE_SHIFT));
		sPoolReportingSizes[i] = POOLING_BLOCK_HEADER_SIZE + EAPooling_GetDataBlockSize(i);

		spEarrayPools[i] = aligned_calloc_dbg(sizeof(ThreadSafeMemoryPool), 64, spPoolReportingName, sPoolReportingSizes[i]);

#if _M_X64
		threadSafeMemoryPoolInitSize_dbg(spEarrayPools[i], TSMP_X64_RECOMMENDED_CHUNK_SIZE, 
			(POOLING_BLOCK_HEADER_SIZE + EAPooling_GetDataBlockSize(i)), 0, poolName, spPoolReportingName, sPoolReportingSizes[i]);
#else
		threadSafeMemoryPoolInit_dbg(spEarrayPools[i], EAPOOLING_POOLSIZE,
			(POOLING_BLOCK_HEADER_SIZE + EAPooling_GetDataBlockSize(i)), 0, poolName, spPoolReportingName, sPoolReportingSizes[i]);
#endif
		threadSafeMemoryPoolSetNoEarrays(spEarrayPools[i], true);

	}

	pEAMalloc_Internal = EAPooling_Malloc;
	pEARealloc_Internal = EAPooling_Realloc;
	pEAFree_Internal = EAPooling_Free;

}

//dummy AUTO_COMMAND to avoid error message
AUTO_COMMAND ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void PoolEArrays(void)
{
}

static bool DoEarrayPooling_default(void)
{
	if (IsThisObjectDB() && sizeof(void*) == 8)
	{
		return true;
	}

	return false;

}

void Earray_InitSystem(void)
{
	char usePoolsCmd[16] = "";
	bool bDoPooling = false;

	if (ParseCommandOutOfCommandLine("PoolEArrays", usePoolsCmd))
	{
		if (atoi(usePoolsCmd))
		{
			bDoPooling = true;
		}
	}
	else if (DoEarrayPooling_default())
	{
		bDoPooling = true;
	}

	if (bDoPooling)
	{
		Earray_InitPooling();
	}
	else
	{
		pEAMalloc_Internal = malloc_timed;
		pEARealloc_Internal = realloc_timed;
		pEAFree_Internal = free_timed;
	}
}

#if _PS3

#define _VALIDATE_RETURN_VOID( expr, errorcode )                               \
    {                                                                          \
        int _Expr_val=!!(expr);                                                \
        if ( !( _Expr_val ) )                                                  \
        {                                                                      \
            assert(!(#expr)); \
            return;                                                            \
        }                                                                      \
    }

#define _VALIDATE_RETURN( expr, errorcode, retexpr )                           \
    {                                                                          \
        int _Expr_val=!!(expr);                                                \
        if ( !( _Expr_val ) )                                                  \
        {                                                                      \
            assert(!(#expr)); \
            return ( retexpr );                                                \
        }                                                                      \
    }

#define __COMPARE(context, p1, p2) comp(context, p1, p2)
#define __SHORTSORT(lo, hi, width, comp, context) shortsort_s(lo, hi, width, comp, context);

static void  qsort_swap (
    char *a,
    char *b,
    size_t width
    )
{
    char tmp;

    if ( a != b )
        /* Do the qsort_swap one character at a time to avoid potential alignment
           problems. */
        while ( width-- ) {
            tmp = *a;
            *a++ = *b;
            *b++ = tmp;
        }
}

static void shortsort_s (
    char *lo,
    char *hi,
    size_t width,
    int ( *comp)(void *, const void *, const void *),
    void * context
    )
{
    char *p, *max;

    /* Note: in assertions below, i and j are alway inside original bound of
       array to sort. */

    while (hi > lo) {
        /* A[i] <= A[j] for i <= j, j > hi */
        max = lo;
        for (p = lo+width; p <= hi; p += width) {
            /* A[i] <= A[max] for lo <= i < p */
            if (__COMPARE(context, p, max) > 0) {
                max = p;
            }
            /* A[i] <= A[max] for lo <= i <= p */
        }

        /* A[i] <= A[max] for lo <= i <= hi */

        qsort_swap(max, hi, width);

        /* A[i] <= A[hi] for i <= hi, so A[i] <= A[j] for i <= j, j >= hi */

        hi -= width;

        /* A[i] <= A[j] for i <= j, j > hi, loop top condition established */
    }
    /* A[i] <= A[j] for i <= j, j > lo, which implies A[i] <= A[j] for i < j,
       so array is sorted */
}

#define CUTOFF 8            /* testing shows that this is good value */

/* Note: the theoretical number of stack entries required is
   no more than 1 + log2(num).  But we switch to insertion
   sort for CUTOFF elements or less, so we really only need
   1 + log2(num) - log2(CUTOFF) stack entries.  For a CUTOFF
   of 8, that means we need no more than 30 stack entries for
   32 bit platforms, and 62 for 64-bit platforms. */
#define STKSIZ (8*sizeof(void*) - 2)


void qsort_s (
    void *base,
    size_t num,
    size_t width,
    int (*comp)(void *, const void *, const void *),
    void *context
    )
{
    char *lo, *hi;              /* ends of sub-array currently sorting */
    char *mid;                  /* points to middle of subarray */
    char *loguy, *higuy;        /* traveling pointers for partition step */
    size_t size;                /* size of the sub-array */
    char *lostk[STKSIZ], *histk[STKSIZ];
    int stkptr;                 /* stack for saving sub-array to be processed */

    /* validation section */
    _VALIDATE_RETURN_VOID(base != NULL || num == 0, EINVAL);
    _VALIDATE_RETURN_VOID(width > 0, EINVAL);
    _VALIDATE_RETURN_VOID(comp != NULL, EINVAL);

    if (num < 2)
        return;                 /* nothing to do */

    stkptr = 0;                 /* initialize stack */

    lo = (char *)base;
    hi = (char *)base + width * (num-1);        /* initialize limits */

    /* this entry point is for pseudo-recursion calling: setting
       lo and hi and jumping to here is like recursion, but stkptr is
       preserved, locals aren't, so we preserve stuff on the stack */
recurse:

    size = (hi - lo) / width + 1;        /* number of el's to sort */

    /* below a certain size, it is faster to use a O(n^2) sorting method */
    if (size <= CUTOFF) {
        __SHORTSORT(lo, hi, width, comp, context);
    }
    else {
        /* First we pick a partitioning element.  The efficiency of the
           algorithm demands that we find one that is approximately the median
           of the values, but also that we select one fast.  We choose the
           median of the first, middle, and last elements, to avoid bad
           performance in the face of already sorted data, or data that is made
           up of multiple sorted runs appended together.  Testing shows that a
           median-of-three algorithm provides better performance than simply
           picking the middle element for the latter case. */

        mid = lo + (size / 2) * width;      /* find middle element */

        /* Sort the first, middle, last elements into order */
        if (__COMPARE(context, lo, mid) > 0) {
            qsort_swap(lo, mid, width);
        }
        if (__COMPARE(context, lo, hi) > 0) {
            qsort_swap(lo, hi, width);
        }
        if (__COMPARE(context, mid, hi) > 0) {
            qsort_swap(mid, hi, width);
        }

        /* We now wish to partition the array into three pieces, one consisting
           of elements <= partition element, one of elements equal to the
           partition element, and one of elements > than it.  This is done
           below; comments indicate conditions established at every step. */

        loguy = lo;
        higuy = hi;

        /* Note that higuy decreases and loguy increases on every iteration,
           so loop must terminate. */
        for (;;) {
            /* lo <= loguy < hi, lo < higuy <= hi,
               A[i] <= A[mid] for lo <= i <= loguy,
               A[i] > A[mid] for higuy <= i < hi,
               A[hi] >= A[mid] */

            /* The doubled loop is to avoid calling comp(mid,mid), since some
               existing comparison funcs don't work when passed the same
               value for both pointers. */

            if (mid > loguy) {
                do  {
                    loguy += width;
                } while (loguy < mid && __COMPARE(context, loguy, mid) <= 0);
            }
            if (mid <= loguy) {
                do  {
                    loguy += width;
                } while (loguy <= hi && __COMPARE(context, loguy, mid) <= 0);
            }

            /* lo < loguy <= hi+1, A[i] <= A[mid] for lo <= i < loguy,
               either loguy > hi or A[loguy] > A[mid] */

            do  {
                higuy -= width;
            } while (higuy > mid && __COMPARE(context, higuy, mid) > 0);

            /* lo <= higuy < hi, A[i] > A[mid] for higuy < i < hi,
               either higuy == lo or A[higuy] <= A[mid] */

            if (higuy < loguy)
                break;

            /* if loguy > hi or higuy == lo, then we would have exited, so
               A[loguy] > A[mid], A[higuy] <= A[mid],
               loguy <= hi, higuy > lo */

            qsort_swap(loguy, higuy, width);

            /* If the partition element was moved, follow it.  Only need
               to check for mid == higuy, since before the qsort_swap,
               A[loguy] > A[mid] implies loguy != mid. */

            if (mid == higuy)
                mid = loguy;

            /* A[loguy] <= A[mid], A[higuy] > A[mid]; so condition at top
               of loop is re-established */
        }

        /*     A[i] <= A[mid] for lo <= i < loguy,
               A[i] > A[mid] for higuy < i < hi,
               A[hi] >= A[mid]
               higuy < loguy
           implying:
               higuy == loguy-1
               or higuy == hi - 1, loguy == hi + 1, A[hi] == A[mid] */

        /* Find adjacent elements equal to the partition element.  The
           doubled loop is to avoid calling comp(mid,mid), since some
           existing comparison funcs don't work when passed the same value
           for both pointers. */

        higuy += width;
        if (mid < higuy) {
            do  {
                higuy -= width;
            } while (higuy > mid && __COMPARE(context, higuy, mid) == 0);
        }
        if (mid >= higuy) {
            do  {
                higuy -= width;
            } while (higuy > lo && __COMPARE(context, higuy, mid) == 0);
        }

        /* OK, now we have the following:
              higuy < loguy
              lo <= higuy <= hi
              A[i]  <= A[mid] for lo <= i <= higuy
              A[i]  == A[mid] for higuy < i < loguy
              A[i]  >  A[mid] for loguy <= i < hi
              A[hi] >= A[mid] */

        /* We've finished the partition, now we want to sort the subarrays
           [lo, higuy] and [loguy, hi].
           We do the smaller one first to minimize stack usage.
           We only sort arrays of length 2 or more.*/

        if ( higuy - lo >= hi - loguy ) {
            if (lo < higuy) {
                lostk[stkptr] = lo;
                histk[stkptr] = higuy;
                ++stkptr;
            }                           /* save big recursion for later */

            if (loguy < hi) {
                lo = loguy;
                goto recurse;           /* do small recursion */
            }
        }
        else {
            if (loguy < hi) {
                lostk[stkptr] = loguy;
                histk[stkptr] = hi;
                ++stkptr;               /* save big recursion for later */
            }

            if (lo < higuy) {
                hi = higuy;
                goto recurse;           /* do small recursion */
            }
        }
    }

    /* We have sorted the array, except for any pending sorts on the stack.
       Check if there are any, and do them. */

    --stkptr;
    if (stkptr >= 0) {
        lo = lostk[stkptr];
        hi = histk[stkptr];
        goto recurse;           /* pop subarray from stack */
    }
    else
        return;                 /* all subarrays done */
}

#undef __COMPARE
#undef __SHORTSORT

#define __COMPARE(context, p1, p2) (*compare)(context, p1, p2)

void * bsearch_s (
    const void *key,
    const void *base,
    size_t num,
    size_t width,
    int (*compare)(void *, const void *, const void *),
    void *context
    )
{
    char *lo = (char *)base;
    char *hi = (char *)base + (num - 1) * width;
    char *mid;
    size_t half;
    int result;

    /* validation section */
    _VALIDATE_RETURN(base != NULL || num == 0, EINVAL, NULL);
    _VALIDATE_RETURN(width > 0, EINVAL, NULL);
    _VALIDATE_RETURN(compare != NULL, EINVAL, NULL);

        /*
        We allow a NULL key here because it breaks some older code and because we do not dereference
        this ourselves so we can't be sure that it's a problem for the comparison function
        */

    while (lo <= hi)
    {
        if ((half = num / 2) != 0)
        {
            mid = lo + (num & 1 ? half : (half - 1)) * width;
            if (!(result = __COMPARE(context, key, mid)))
                return(mid);
            else if (result < 0)
            {
                hi = mid - width;
                num = num & 1 ? half : half-1;
            }
            else
            {
                lo = mid + width;
                num = half;
            }
        }
        else if (num)
            return (__COMPARE(context, key, lo) ? NULL : lo);
        else
            break;
    }

    return NULL;
}

#undef __COMPARE

#undef _VALIDATE_RETURN_VOID
#undef _VALIDATE_RETURN

#endif

/////////////////////////////////////////////////////// EArray (64-bit compatible) functions


static void eaSetOptions(EArray *pArray,void *indexedTable)
{
	pArray->tableandflags = (intptr_t)indexedTable;
}

int eags(EArrayHandle* handle)
{
	return eaSize(handle);
}

int eagsf(EArray32Handle* handle)
{
	return ea32Size(handle);
}

int eagsi(EArray32Handle* handle)
{
	return ea32Size(handle);
}

int eags32(EArray32Handle* handle)
{
	return ea32Size(handle);
}

extern int some_global_that_the_compiler_thinks_might_be_non_zero;

AUTO_RUN;
void eaEnsureDebuggingFunctionsGetLinked(void)
{
	if(some_global_that_the_compiler_thinks_might_be_non_zero)
	{
		eags(NULL);
		eagsf(NULL);
		eagsi(NULL);
		eags32(NULL);
	}
}

/////////////////////////////////////////////////////// Indexed EArray templates

// StashTable for our indexed EArray templates, which are just empty indexed
// EArrays for each different ParseTable.
static StashTable stIndexedTemplateTable = NULL;
static CRITICAL_SECTION gIndexedTemplateCritSection;

int eaIndexedEnableAfterTemplateCheck(cEArrayHandle *handle, ParseTable tpi[] MEM_DBG_PARMS);

// Clean up a template that goes with a ParseTable when we destroy the ParseTable.
void eaIndexedTemplateDestroy(ParseTable tpi[]) {
	cEArrayHandle handle;
	EnterCriticalSection(&gIndexedTemplateCritSection);
	if(stashRemovePointer(stIndexedTemplateTable, tpi, (void*)&handle))
	{
		EArray* pArray = EArrayFromHandle(handle);
		if (handle && pArray->size >= 0)
			pEAFree(pArray);
	}
	LeaveCriticalSection(&gIndexedTemplateCritSection);
}

// Make a new indexed EArray with the same ParseTable as a template.
void eaCreateIndexedFromTemplate(cEArrayHandle *templateHandle, cEArrayHandle *handle MEM_DBG_PARMS)
{
	EArray *pArray = EArrayFromHandle(*templateHandle);
	ParseTable *parseTable = pArray ? GetIndexedTable(pArray) : NULL;
	
	*handle = NULL;
	eaCreateInternal(handle MEM_DBG_PARMS_CALL);
	eaIndexedEnableAfterTemplateCheck(handle, parseTable MEM_DBG_PARMS_CALL);
}

void eaIndexedTemplateInitialize(void) {
	InitializeCriticalSection(&gIndexedTemplateCritSection);
	stIndexedTemplateTable = stashTableCreateAddressEx(1000, "eaIndexedStashTable", __LINE__);
}

// Determine if a given EArray is an indexed EArray template.
bool eaIsIndexedTemplate(ccEArrayHandle *handle)
{
	if(!stIndexedTemplateTable)
		eaIndexedTemplateInitialize();

	if(*handle) {

		// All templates are size 0.
		if(eaSize(handle) == 0)
		{
			EArray *pArray = EArrayFromHandle(*handle);
			ParseTable *parseTable = pArray ? GetIndexedTable(pArray) : NULL;

			// All templates have a parse table set.
			if(parseTable)
			{
				cEArrayHandle templateHandle = NULL;

				EnterCriticalSection(&gIndexedTemplateCritSection);
				// If both these are true we can dedicate some time to the table lookup to find it.
				if(stashFindPointer(stIndexedTemplateTable, parseTable, (void*)&templateHandle))
				{
					LeaveCriticalSection(&gIndexedTemplateCritSection);
					// And, if it actually equals the same thing...
					if(templateHandle == *handle)
					{
						// Then it's the template.
						return true;
					}
				}
				else
				{
					LeaveCriticalSection(&gIndexedTemplateCritSection);
				}
			}
		}
	}
	return false;
}

// Get a template for a given ParseTable or, if one doesn't exist already,
// create it. Then set the handle.
void eaIndexedGetTemplate(ParseTable *parseTable, cEArrayHandle *handle MEM_DBG_PARMS)
{
	if(!stIndexedTemplateTable)
		eaIndexedTemplateInitialize();

	EnterCriticalSection(&gIndexedTemplateCritSection);
	if(!stashFindPointerConst(stIndexedTemplateTable, parseTable, (void*)handle))
	{
		eaCreateInternal(handle MEM_DBG_PARMS_CALL);
		stashAddPointer(stIndexedTemplateTable, parseTable, *handle, true);

		eaIndexedEnableAfterTemplateCheck(handle, parseTable MEM_DBG_PARMS_CALL);
	}
	LeaveCriticalSection(&gIndexedTemplateCritSection);
}

/////////////////////////////////////////////////////// EArray functions

void eaCreateInternal(cEArrayHandle* handle MEM_DBG_PARMS)
{
	EArray* pArray;
	assertmsg(!(*handle), "Tried to create an earray that is either already created or has a corrupt pointer.");
	pArray = pEAMalloc(sizeof(EArray));
	pArray->count = 0;
	pArray->size = 1;
	eaSetOptions(pArray, NULL);
	*handle = HandleFromEArray(pArray);
}





void eaDestroy(cEArrayHandle* handle) // pEAFree list
{
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle))
		return;

	if(eaIsIndexedTemplate(handle))
	{
		// Only pretend to pEAFree indexed eArray templates.
		*handle = NULL;
		return;
	}

	if (pArray->size >= 0)
		pEAFree(pArray);
	// else it is allocated on the stack
	
	*handle = NULL;
}

void eaClearEx(EArrayHandle* handle, EArrayItemCallback destructor)
{
	int i;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle))
		return;

	// Do not change the order: This goes from last to first when clearing
	// so that an item can safely remove itself from the array on destruction
	for (i = pArray->count-1; i >= 0; --i)
	{
		if (pArray->structptrs[i])
		{
			if (destructor)
				destructor(pArray->structptrs[i]);
			else
				free(pArray->structptrs[i]);	

			pArray->structptrs[i] = NULL;
		}
	}

	pArray->count = 0;
}

void eaClearExFileLineEx(EArrayHandle* handle, EArrayItemFileLineCallback destructor, const char* file, int line)
{
	int i;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle))
		return;

	// Do not change the order: This goes from last to first when clearing
	// so that an item can safely remove itself from the array on destruction
	for (i = pArray->count-1; i >= 0; --i)
	{
		if (pArray->structptrs[i])
		{
			destructor(pArray->structptrs[i], file, line);

			pArray->structptrs[i] = NULL;
		}
	}

	pArray->count = 0;
}

void eaDestroyEx(EArrayHandle *handle,EArrayItemCallback destructor)
{
	eaClearEx(handle,destructor);
	eaDestroy(handle);
}

void eaDestroyExFileLineEx(EArrayHandle *handle, EArrayItemFileLineCallback destructor, const char* file, int line)
{
	eaClearExFileLineEx(handle, destructor, file, line);
	eaDestroy(handle);
}

void eaForEach(cEArrayHandle* handle, EArrayItemCallback callback)
{
	int i;
	EArray* pArray = EArrayFromHandle(*handle);

	if (!callback || !(*handle))
		return;

	for (i = 0; i < pArray->count; ++i)
	{
		if (pArray->structptrs[i])
		{
			callback(pArray->structptrs[i]);
		}
	}
}

void eaSetCapacity_dbg(cEArrayHandle* handle, int capacity MEM_DBG_PARMS) // grows or shrinks capacity, limits size if required
{
	EArray* pArray;
	int size = capacity>1? capacity: 1;

	if(eaIsIndexedTemplate(handle))
		eaCreateIndexedFromTemplate(handle, handle MEM_DBG_PARMS_CALL);

	if (*handle)
	{
		pArray = EArrayFromHandle(*handle);
		assert(pArray->size >= 0); // no stack arrays allowed
		pArray = pEARealloc(pArray, EARRAY_HEADER_SIZE + sizeof(void *)*size);
		if (pArray->count > capacity) 
			pArray->count = capacity;
	}
	else
	{
		pArray = pEAMalloc(EARRAY_HEADER_SIZE + sizeof(void *)*size);
		pArray->count = 0;
		eaSetOptions(pArray, NULL);
	}
	
	pArray->size = size;

	*handle = HandleFromEArray(pArray);
}

int eaCapacity(ccEArrayHandle* handle) // just returns current capacity
{
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle))
		return 0;
	return ABS(pArray->size);
}

size_t eaMemUsage(ccEArrayHandle* handle, bool bAbsoluteUsage) // get the amount of memory actually used (not counting slack allocated)
{
	EArray* pArray = EArrayFromHandle(*handle);

	if (!(*handle))
		return 0;

	if (bAbsoluteUsage)
		return ABS(pArray->size)*sizeof(pArray->structptrs[0]) + EARRAY_HEADER_SIZE;
	else
		return pArray->count*sizeof(pArray->structptrs[0]) + EARRAY_HEADER_SIZE;
}

static __forceinline void eaSetSize_dbg_inline(SA_PRE_NN_VALID SA_POST_NN_ELEMS_VAR(size) cEArrayHandle* handle, int size MEM_DBG_PARMS) // set the number of items in pArray (fills with zero)
{
	// this is a little confusing because Size = count within the pArray structure, Capacity = size
	EArray* pArray=NULL;

	if(eaIsIndexedTemplate(handle))
		eaCreateIndexedFromTemplate(handle, handle MEM_DBG_PARMS_CALL);

	if (*handle)
		pArray = EArrayFromHandle(*handle);

	// don't bother creating the array or "raising" its capacity if the new size is 0
	if (size)
	{
		// set the new capacity, creating the array if necessary
		if (!(*handle) || ABS(pArray->size) < size) 
		{
			eaSetCapacity_dbg(handle, size MEM_DBG_PARMS_CALL);
			pArray = EArrayFromHandle(*handle);
		}

		if (pArray->count < size) // increase size
		{
			memset(&pArray->structptrs[pArray->count], 0, sizeof(pArray->structptrs[0])*(size-pArray->count));
		}
	}

	if (pArray) // this would be NULL only if there was no array and the new size is also 0
		pArray->count = size;
}

void eaSetSize_dbg(cEArrayHandle* handle, int size MEM_DBG_PARMS) // set the number of items in pArray (fills with zero)
{
	eaSetSize_dbg_inline(handle,size MEM_DBG_PARMS_CALL);
}

int eaPush_dbg(cEArrayHandle* handle, const void* structptr MEM_DBG_PARMS) // add to the end of the list, returns the index it was added at (the new size - 1)
{
	EArray* pArray = *handle ? EArrayFromHandle(*handle) : NULL;

	if (pArray && GetIndexedTable(pArray))
	{
		int size = 0;

		if(eaIsIndexedTemplate(handle))
			eaCreateIndexedFromTemplate(handle, handle MEM_DBG_PARMS_CALL);

		if (*handle)
		{
			pArray = EArrayFromHandle(*handle);
			size = pArray->size;
		}

		if (!(*handle) || pArray->count == ABS(pArray->size)) 
		{
			eaSetCapacity_dbg(handle, max(4,ABS(size)*2) MEM_DBG_PARMS_CALL);
			pArray = EArrayFromHandle(*handle);
		}

		return eaIndexedAdd_dbg(handle,structptr MEM_DBG_PARMS_CALL);
	}
	else
	{
		int size = 0;

		if (*handle)
		{
			pArray = EArrayFromHandle(*handle);
			size = pArray->size;
		}

		if (!(*handle) || pArray->count == ABS(pArray->size)) 
		{
			eaSetCapacity_dbg(handle, max(4,ABS(size)*2) MEM_DBG_PARMS_CALL);
			pArray = EArrayFromHandle(*handle);
		}

		pArray->structptrs[pArray->count++] = (void*)structptr;
		return pArray->count-1;
	}
}

int eaPushUnique_dbg(cEArrayHandle* handle, const void* structptr MEM_DBG_PARMS) // add to the end of the list, returns the index it was added at (the new size)
{
	int idx = eaFind(handle, structptr);
	if (idx < 0)
		idx = seaPush(handle, structptr);
	return idx;
}

int eaPushEArray_dbg(cEArrayHandle* handle, ccEArrayHandle* src MEM_DBG_PARMS) // add to the end of the list, returns the index it was added at
{
	EArray *pArray=NULL;
	int start=0;

	if(eaIsIndexedTemplate(handle))
		eaCreateIndexedFromTemplate(handle, handle MEM_DBG_PARMS_CALL);

	if (*handle)
	{
		pArray = EArrayFromHandle(*handle);
		start = eaSize( handle );
	}

	if (src)
	{
		int srcCount = eaSize(src);
		if (pArray && GetIndexedTable(pArray))
		{
			int i;
			for (i = 0; i < srcCount; i++)
			{
				eaIndexedAdd_dbg(handle,(*src)[i] MEM_DBG_PARMS_CALL);
			}
		}
		else
		{
			eaSetSize_dbg_inline( handle, start + srcCount MEM_DBG_PARMS_CALL);
			CopyStructs(*(EArrayHandle*)handle + start, *src, srcCount);
		}
	}

	return start;
}

int	eaPushArray_dbg(cEArrayHandle* handle, const void * const *structptrs, int count MEM_DBG_PARMS)
{
	EArray *pArray=NULL;
	int start=0;

	if(eaIsIndexedTemplate(handle))
		eaCreateIndexedFromTemplate(handle, handle MEM_DBG_PARMS_CALL);

	if (*handle)
	{
		pArray = EArrayFromHandle(*handle);
		start = eaSize(handle);
	}

	if (structptrs)
	{
		if (pArray && GetIndexedTable(pArray))
		{
			int i;
			for (i = 0; i < count; i++)
			{
				eaIndexedAdd_dbg(handle,structptrs[i] MEM_DBG_PARMS_CALL);
			}
		}
		else
		{					
			eaSetSize_dbg_inline( handle, start + count MEM_DBG_PARMS_CALL);
			CopyStructs(*(EArrayHandle*)handle + start, structptrs, count);
		}
	}
	return start;
}

void* eaPop(cEArrayHandle* handle) // remove the last item from the list
{
	EArray* pArray = EArrayFromHandle(*handle);

	if (!(*handle) || !pArray->count)
		return NULL;
	
	pArray->count--;
	return pArray->structptrs[pArray->count];
}

void eaSet_dbg(cEArrayHandle* handle, const void* structptr, int i MEM_DBG_PARMS) // set i'th element (zero-based)
{
	EArray* pArray=NULL;

	if (i < 0)
		return;

	if(eaIsIndexedTemplate(handle))
		eaCreateIndexedFromTemplate(handle, handle MEM_DBG_PARMS_CALL);

	if (*handle)
	{
		pArray = EArrayFromHandle(*handle);
		assertmsg(!GetIndexedTable(pArray), "eaSet not supported for indexed arrays");
	}

	if (!(*handle) || ABS(pArray->size) <= i)
	{
		eaSetCapacity_dbg(handle, pow2(i+1) MEM_DBG_PARMS_CALL);
		pArray = EArrayFromHandle(*handle);
	}
	if (pArray->count <= i)
	{
		ZeroStructsForce(pArray->structptrs + pArray->count, i + 1 - pArray->count);
		pArray->count = i + 1;
	}
	pArray->structptrs[i] = (void*)structptr;
}

void* eaGetVoid(ccEArrayHandle* handle, U32 index)	// get i'th element (zero-based), NULL on error
{
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || index >= (U32)pArray->count)
		return NULL;
	return pArray->structptrs[index];
}

static void* twoNULLs[2];
#define PTR_ARRAY_OFFSET_TO_NULL(s) ((size_t)((intptr_t)twoNULLs + sizeof(void*) - 1 - (intptr_t)s) / sizeof(void*))

size_t eaGetIndexOrOffsetToNULL(ccEArrayHandle* handle, U32 index)	// get i'th element (zero-based), NULL on error
{
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || index >= (U32)pArray->count)
	{
		return PTR_ARRAY_OFFSET_TO_NULL(pArray->structptrs);
	}
	return index;
}

int eaInsert_dbg(cEArrayHandle* handle, const void* structptr, int i, bool bIndexed MEM_DBG_PARMS) // insert before i'th position, will not insert on error (i == -1, etc.)
{
	EArray* pArray=NULL;
	int iCurCount = 0;
	int iCurSize = 0;

	if (i < 0)
		return 0;

	if(eaIsIndexedTemplate(handle))
		eaCreateIndexedFromTemplate(handle, handle MEM_DBG_PARMS_CALL);

	if (*handle)
	{
		pArray = EArrayFromHandle(*handle);
		iCurCount = pArray->count;
		iCurSize = pArray->size;
	}

	if (iCurCount < i)
		return 0;
	if (!(*handle) || pArray->count == ABS(pArray->size))
	{
		eaSetCapacity_dbg(handle, max(4,ABS(iCurSize)*2) MEM_DBG_PARMS_CALL);
		pArray = EArrayFromHandle(*handle);
	}

	assertmsg(!GetIndexedTable(pArray) || bIndexed, "eaInsert not supported for indexed arrays, unless you call eaIndexedInsert");

	CopyStructsFromOffset(pArray->structptrs + i + 1, -1, pArray->count - i);
	
	pArray->count++;
	pArray->structptrs[i] = (void*)structptr;
	return 1;
}

int eaInsertEArray_dbg(cEArrayHandle* handle, ccEArrayHandle* src, int i MEM_DBG_PARMS) // insert before i'th position, will not insert on error (i == -1, etc.)
{
	EArray* pArray=NULL;
	EArray* pInsertArray;
	int iCurCount = 0;
	int iCurSize = 0;

	if (i < 0)
		return 0;
	if (!(*src))
		return 0;
	pInsertArray = EArrayFromHandle(*src);
	if (!pInsertArray->count)
		return 0;
	
	if(eaIsIndexedTemplate(handle))
		eaCreateIndexedFromTemplate(handle, handle MEM_DBG_PARMS_CALL);
	
	if (*handle)
	{
		pArray = EArrayFromHandle(*handle);
		iCurCount = pArray->count;
		iCurSize = pArray->size;
	}

	if (iCurCount < i)
		return 0;

	if (!(*handle) || pArray->count + pInsertArray->count > ABS(pArray->size))
	{
		eaSetCapacity_dbg(handle, ABS(iCurSize) + ABS(pInsertArray->size) MEM_DBG_PARMS_CALL);
		pArray = EArrayFromHandle(*handle);
	}
	
	CopyStructsFromOffset(pArray->structptrs + i + pInsertArray->count, -1 * pInsertArray->count, pArray->count - i);
	CopyStructs(pArray->structptrs + i, pInsertArray->structptrs, pInsertArray->count);
	
	pArray->count += pInsertArray->count;

	return 1;
}

int eaIndexedMerge_dbg(EArrayHandle *pppDestEarray, EArrayHandle *pppSrcEarray, eaMergeCallback mergeCB, void *pUserData MEM_DBG_PARMS)
{
	ParseTable *pSrcIndexedTable = eaIndexedGetTable(pppSrcEarray);
	ParseTable *pTPIToUse = eaIndexedGetTable(pppDestEarray);
	ParserCompareFieldFunction cmp;
	int iKeyColumn;

	int iSrcIndex = 0;
	int iDestIndex = 0;
	int iDestSize = eaSize(pppDestEarray);
	int iSrcSize = eaSize(pppSrcEarray);
	ParserSortData sortData;

	if(eaIsIndexedTemplate(pppDestEarray))
	{
		eaCreateIndexedFromTemplate(pppDestEarray, pppDestEarray MEM_DBG_PARMS_CALL);
		pTPIToUse = eaIndexedGetTable(pppDestEarray);
	}

	if (!pSrcIndexedTable || !pTPIToUse || pSrcIndexedTable != pTPIToUse)
	{
		return 0;
	}

	iKeyColumn = ParserGetTableKeyColumn(pTPIToUse);
	assert(iKeyColumn != -1);

	cmp = ParserGetCompareFunction(pTPIToUse,iKeyColumn);
	sortData.tpi = pTPIToUse;
	sortData.column = iKeyColumn;

	while (iSrcIndex < iSrcSize)
	{
		void *pSrcStruct = (*pppSrcEarray)[iSrcIndex];

		if (iDestIndex >= iDestSize)
		{
			seaIndexedInsert(pppDestEarray, pSrcStruct, iDestIndex);
			if (mergeCB)
			{
				mergeCB(pppDestEarray, iDestIndex, pUserData);
			}
			iSrcIndex++;
			iDestSize++;
			iDestIndex++;
		}
		else
		{
			void *pDestStruct = (*pppDestEarray)[iDestIndex];
			int iCompRet = cmp(&sortData, &pSrcStruct, &pDestStruct);
			if (iCompRet == 0)
			{
				StructDestroyVoid(pTPIToUse, pDestStruct);
				(*pppDestEarray)[iDestIndex] = pSrcStruct;
				if (mergeCB)
				{
					mergeCB(pppDestEarray, iDestIndex, pUserData);
				}
				iSrcIndex++;
				iDestIndex++;
			}
			else if (iCompRet < 0)
			{			
				seaIndexedInsert(pppDestEarray, pSrcStruct, iDestIndex);
				if (mergeCB)
				{
					mergeCB(pppDestEarray, iDestIndex, pUserData);
				}
				iSrcIndex++;
				iDestIndex++;
				iDestSize++;
			}
			else //iCompRet > 0
			{
				// Just leave it there, go to next one
				iDestIndex++;
			}
		}
	}

	eaClear(pppSrcEarray);
	return eaSize(pppDestEarray);

}


void* eaRemoveVoid(cEArrayHandle* handle, U32 index)
{
	void* structptr;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || index >= (U32)pArray->count) return NULL;
	structptr = pArray->structptrs[index];
	pArray->count--;
	if(index != (U32)pArray->count){
		CopyStructsFromOffset(pArray->structptrs + index, 1, pArray->count - index);
	}
	return structptr;
}

size_t eaGetIndexToRemovedOrOffsetToNULL(cEArrayHandle* handle, U32 index)
{
	void* structptr;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || index >= (U32)pArray->count){
		return PTR_ARRAY_OFFSET_TO_NULL(pArray->structptrs);
	}
	structptr = pArray->structptrs[index];
	pArray->count--;
	if(index != (U32)pArray->count){
		CopyStructsFromOffset(pArray->structptrs + index, 1, pArray->count - index);
		pArray->structptrs[pArray->count] = structptr;
	}
	return pArray->count;
}

void* eaRemoveFastVoid(cEArrayHandle* handle, U32 index) // remove the i'th element, and move the last element into this place DOES NOT KEEP ORDER
{	
	void* structptr;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || index >= (U32)pArray->count) return NULL;
	structptr = pArray->structptrs[index];

	assertmsg(!GetIndexedTable(pArray), "eaRemoveFast not supported for indexed arrays");

	pArray->structptrs[index] = pArray->structptrs[--pArray->count];
	
	return structptr;
}

size_t eaGetIndexToRemovedFastOrOffsetToNULL(cEArrayHandle* handle, U32 index)
{
	void* structptr;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || index >= (U32)pArray->count){
		return PTR_ARRAY_OFFSET_TO_NULL(pArray->structptrs);
	}
	structptr = pArray->structptrs[index];
	pArray->count--;
	if(index != (U32)pArray->count){
		SWAPP(pArray->structptrs[index], pArray->structptrs[pArray->count]);
	}
	return pArray->count;
}

void eaRemoveRange(cEArrayHandle* handle, int start, int count) // remove count elements, starting with start
{
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || pArray->count <= start || start < 0) return;

	if (start + count >= pArray->count)
	{
		pArray->count = start;
		return;
	}

	CopyStructsFromOffset(pArray->structptrs + start, count, pArray->count - start - count);

	pArray->count -= count;
}

void eaRemoveTail(cEArrayHandle* handle, int start) // remove trailing elements, starting with start
{
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || pArray->count <= start || start < 0) return;

	pArray->count = start;
}

void eaRemoveTailEx(cEArrayHandle* handle, int start, EArrayItemCallback destructor) // remove trailing elements, starting with start
{
	int i;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || pArray->count <= start || start < 0) return;

	for (i = pArray->count-1; i >= start; --i)
	{
		if (pArray->structptrs[i])
		{
			if (destructor)
				destructor(pArray->structptrs[i]);
			else
				free(pArray->structptrs[i]);	

			pArray->structptrs[i] = NULL;
		}
	}

	pArray->count = start;
}


int	eaFind_dbg(ccEArrayHandle* handle, const void* structptr)	// find the first element that matches structptr, returns -1 on error
{
	int i;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle)) return -1;
	for (i = 0; i < pArray->count; i++)
	{
		if (pArray->structptrs[i] == structptr) return i;
	}
	return -1;
}

int eaFindCmp(ccEArrayHandle* handle, const void* structptr, EArrayStructCompare compareFn)	 // like eaFind, but compares with a function
{
	int i;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle)) return -1;
	for (i = 0; i < pArray->count; i++)
	{
		if (compareFn(pArray->structptrs[i], structptr)) return i;
	}
	return -1;
}

int	eaFindAndRemove_dbg(cEArrayHandle* handle, const void* structptr)
{
	int	idx;

	if ((idx = eaFind(handle,structptr)) >= 0)
		eaRemove(handle,idx);
	return idx;
}

int	eaFindAndRemoveFast_dbg(cEArrayHandle* handle, const void* structptr)
{
	int	idx;

	if ((idx = eaFind(handle,structptr)) >= 0)
		eaRemoveFast(handle,idx);
	return idx;
}

void eaReverseEx(cEArrayHandle* handle, int start_idx, int end_idx)
{
	EArray* pArray = EArrayFromHandle(*handle);
	void **head, **tail;
	if (!(*handle)) return;

	assertmsg(!GetIndexedTable(pArray), "eaReverse not supported for indexed arrays");

	if (start_idx < 0)
		start_idx = 0;
	if (end_idx >= pArray->count)
		end_idx = pArray->count - 1;

	head = pArray->structptrs + start_idx;
	tail = pArray->structptrs + end_idx;

	// Start the index at the two "outer most" elements of the pArray.
	// Each interation through the loop, swap the two elements that are indexed,
	// then bring the indices one step closer to each other.
	for(; head < tail; head++, tail--){
		void* tempBuffer = *head;
		*head = *tail;
		*tail = tempBuffer;
	}
}

void eaReverse(cEArrayHandle* handle)
{
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle)) return;
	eaReverseEx(handle, 0, pArray->count - 1);
}

void eaCopy_dbg(cEArrayHandle* dest, ccEArrayHandle* src MEM_DBG_PARMS)
{
	int size = eaSize(src);

	if(eaIsIndexedTemplate(dest))
		eaCreateIndexedFromTemplate(dest, dest MEM_DBG_PARMS_CALL);

	if(size || *dest)
	{
		if (!*dest) eaCreateInternal(dest MEM_DBG_PARMS_CALL);
		eaSetSize_dbg_inline(dest, size MEM_DBG_PARMS_CALL);
	}

	if(size)
	{
		EArray *d, *s;
		d = EArrayFromHandle(*dest);
		s = EArrayFromHandle(*src);
		
		memcpy(d->structptrs, s->structptrs, sizeof(d->structptrs[0])*size);
	}
}

void eaCopyEx_dbg(EArrayHandle* src, EArrayHandle* dst, EArrayItemCopier copyFn, EArrayItemCallback destroyFn MEM_DBG_PARMS)
{
	int size = eaSize(src);
	int	i;

	if(eaIsIndexedTemplate(dst))
		eaCreateIndexedFromTemplate(dst, dst MEM_DBG_PARMS_CALL);

	if(size || *dst)
	{
		if (!(*dst)) eaCreateInternal(dst MEM_DBG_PARMS_CALL);
		eaClearEx(dst, destroyFn); // Could be optimized, but at least it doesn't leak memory
		eaSetSize_dbg_inline(dst,size MEM_DBG_PARMS_CALL);
	}

	if (size)
	{
		EArray	*src_array, *dst_array;
		src_array = EArrayFromHandle(*src);
		dst_array = EArrayFromHandle(*dst);

		for (i = 0; i < size; i++)
		{
			if (!dst_array->structptrs[i])
			{
				if (!copyFn)
					dst_array->structptrs[i] = src_array->structptrs[i];
				else
					dst_array->structptrs[i] = copyFn(src_array->structptrs[i]);
			}
		}
	}
}

void eaCopyStructs_dbg(ccEArrayHandle* src, EArrayHandle* dst, ParseTable *pTPI MEM_DBG_PARMS)
{
	int size = eaSize(src);
	int	i;

	if(eaIsIndexedTemplate(dst))
		eaCreateIndexedFromTemplate(dst, dst MEM_DBG_PARMS_CALL);

	if(size || *dst)
	{
		if (!(*dst)) eaCreateInternal(dst MEM_DBG_PARMS_CALL);

		while (eaSize(dst) > size)
			StructDestroyVoid(pTPI,eaPop(dst));

		eaSetSize_dbg_inline(dst, size MEM_DBG_PARMS_CALL);
	}

	if (size)
	{
		ParseTableInfo *info = ParserGetTableInfo(pTPI);
		EArray *src_array = EArrayFromHandle(*src);
		EArray *dst_array = EArrayFromHandle(*dst);
		
		for (i = 0; i < size; i++)
		{
			if (!src_array->structptrs[i])
			{
				StructDestroySafeVoid(pTPI,&dst_array->structptrs[i]);
			}
			else 
			{
				if (!dst_array->structptrs[i])
					dst_array->structptrs[i] = StructAlloc_dbg(pTPI, info MEM_DBG_PARMS_CALL);
				StructCopyAllVoid(pTPI, src_array->structptrs[i], dst_array->structptrs[i]);
			}
		}
	}
}

void eaPushStructs_dbg(EArrayHandle* dst, ccEArrayHandle* src, ParseTable *pTPI MEM_DBG_PARMS)
{
	EArray	*src_array, *dst_array;
	int		i;
	int		dst_count;

	if(eaIsIndexedTemplate(dst))
		eaCreateIndexedFromTemplate(dst, dst MEM_DBG_PARMS_CALL);

	if (!(*src)) return;
	if (!(*dst)) eaCreateInternal(dst MEM_DBG_PARMS_CALL);
	src_array = EArrayFromHandle(*src);
	dst_array = EArrayFromHandle(*dst);
	dst_count = dst_array->count;
	eaSetSize_dbg_inline(dst, src_array->count + dst_count MEM_DBG_PARMS_CALL);
	dst_array = EArrayFromHandle(*dst);

	for (i = 0; i < src_array->count; i++)
	{
		int dst_idx = i + dst_count;
		if (!src_array->structptrs[i])
			continue;
		dst_array->structptrs[dst_idx] = StructClone_dbg(pTPI, src_array->structptrs[i], NULL MEM_DBG_PARMS_CALL);

	}
}

void eaCopyEStrings_dbg(ccEArrayHandle* src, EArrayHandle *dst MEM_DBG_PARMS)
{
	int size = eaSize(src);
	int	i;

	assert(!eaIsIndexedTemplate(dst));

	if(size || *dst)
	{
		if (!(*dst)) eaCreateInternal(dst MEM_DBG_PARMS_CALL);
		eaSetSizeEString_dbg(dst, size MEM_DBG_PARMS_CALL);
	}

	if (size)
	{
		EArray *src_array = EArrayFromHandle(*src);
		EArray *dst_array = EArrayFromHandle(*dst);

		for (i = 0; i < size; i++)
		{
			if (!src_array->structptrs[i])
				continue;
			estrCopy2_dbg((char**) &dst_array->structptrs[i], src_array->structptrs[i] MEM_DBG_PARMS_CALL);
		}
	}
}

void eaCompress_dbg(EArrayHandle *dst, cEArrayHandle *src, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS)
{
	const EArray *src_array;
	EArray *dst_array;
	size_t len;

	if (!(*src)) {
		*dst=NULL;
		return;
	}

	len = eaMemUsage(src, false);
	src_array = EArrayFromHandle(*src);

	if (memAllocator)
	{	
		dst_array = memAllocator(customData, len);
	}
	else
	{
		dst_array = pEAMalloc(len);
	}

	eaSetOptions(dst_array, GetIndexedTable(src_array));

	memcpy(dst_array, src_array, len);
	dst_array->size = dst_array->count;
	*dst = HandleFromEArray(dst_array);	
}

EArrayHandle eaTemp(void* ptr)
{
	static void** ea = 0;
	if (!ea)
		eaCreate(&ea);
	eaSetSize(&ea, 1);
	ea[0] = ptr;
	return ea;
}

void eaSwap(cEArrayHandle* handle, int i, int j)
{
	void* temp;
	EArray* pArray = EArrayFromHandle(*handle);
	if (!(*handle) || pArray->count <= i || i < 0 || pArray->count <= j || j < 0)
		return;
	
	assertmsg(!GetIndexedTable(pArray), "eaSwap not supported for indexed arrays");

	temp = pArray->structptrs[i];
	pArray->structptrs[i] = pArray->structptrs[j];
	pArray->structptrs[j] = temp;

}

void eaMove(cEArrayHandle* handle, int dest, int src)
{
	void* temp;
	EArray* pArray = EArrayFromHandle(*handle);

	assertmsg(!GetIndexedTable(pArray), "eaMove not supported for indexed arrays");

	if (!(*handle) || pArray->count <= src || src < 0 || pArray->count <= dest || dest < 0) return;
	temp = pArray->structptrs[src];
	if(src < dest)
		CopyStructsFromOffset(pArray->structptrs + src, 1, dest - src);
	else
		CopyStructsFromOffset(pArray->structptrs + dest + 1, -1, src - dest);
	pArray->structptrs[dest] = temp;
}

void *eaRandChoice(ccEArrayHandle* handle)
{
	return eaGetVoid(handle, randInt(eaSize(handle)));
}

int eaRandIndex(ccEArrayHandle* handle)
{
	if(eaSize(handle)<1)
		return -1;
	return randInt(eaSize(handle));
}

void	eaRandomize(void ***pppArray)
{
	int iSize = eaSize(pppArray);
	int i;

	for (i = iSize - 1; i > 0; i--)
	{
		int j = randInt(i + 1);
		if (j != i)
		{
			void *pTemp = (*pppArray)[i];
			(*pppArray)[i] = (*pppArray)[j];
			(*pppArray)[j] = pTemp;
		}
	}
}







/////////////////////////////////////////////////////// EArray32 (int, f32) functions

void ea32Create_dbg(EArray32Handle* handle MEM_DBG_PARMS)
{
	EArray32* pArray;
	assertmsg(!(*handle), "Tried to create an earray that is either already created or has a corrupt pointer.");
	pArray = pEAMalloc(sizeof(EArray32));
	pArray->count = 0;
	pArray->size = 1;
	*handle = HandleFromEArray32(pArray);
}

void ea32Destroy(EArray32Handle* handle) // pEAFree list
{
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle))
		return;

	if(eaIsIndexedTemplate((cEArrayHandle*)handle))
	{
		// Only pretend to pEAFree indexed eArray templates.
		*handle = NULL;
		return;
	}

	if (pArray->size >= 0)
		pEAFree(pArray);
	// else it is allocated on the stack
	
	*handle = NULL;
}

void ea32SetCapacity_dbg(EArray32Handle* handle, int capacity MEM_DBG_PARMS) // grows or shrinks capacity, limits size if required
{
	EArray32* pArray;
	int size = capacity>1? capacity: 1;

	if (*handle)
	{
		// resize the array
		pArray = EArray32FromHandle(*handle);

		assert(pArray->size >= 0); // no stack arrays allowed
		pArray = pEARealloc(pArray, EARRAY32_HEADER_SIZE + sizeof(U32)*size);
		if (pArray->count > capacity) pArray->count = capacity;
	}
	else
	{
		// Auto-create array.
		pArray = pEAMalloc(EARRAY32_HEADER_SIZE + sizeof(U32)*size);
		pArray->count = 0;
	}

	pArray->size = size;
	*handle = HandleFromEArray32(pArray);
}

int ea32Capacity(ccEArray32Handle* handle) // just returns current capacity
{
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle))
		return 0;
	return ABS(pArray->size);
}

size_t ea32MemUsage(ccEArray32Handle* handle, bool bAbsoluteUsage) // get the amount of memory actually used (not counting slack allocated)
{
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle)) return 0;
	if (bAbsoluteUsage)	
		return ABS(pArray->size)*sizeof(pArray->values[0]) + EARRAY32_HEADER_SIZE;
	else
		return pArray->count*sizeof(pArray->values[0]) + EARRAY32_HEADER_SIZE;
}

void ea32SetSize_dbg(EArray32Handle* handle, int size MEM_DBG_PARMS) // set the number of items in pArray (fills with zero)
{
	// this is a little confusing because Size = count within the pArray structure, Capacity = size
	EArray32* pArray=NULL;

	if (*handle)
	{
		pArray = EArray32FromHandle(*handle);
	}

	// don't bother creating the array or "raising" its capacity if the new size is 0
	if (size)
	{
		// set the new capacity, creating the array if necessary
		if (!(*handle) || ABS(pArray->size) < size) 
		{
			ea32SetCapacity_dbg(handle, size MEM_DBG_PARMS_CALL);
			pArray = EArray32FromHandle(*handle);
		}
		if (pArray->count < size) // increase size
		{
			memset(&pArray->values[pArray->count], 0, sizeof(pArray->values[0])*(size-pArray->count));
		}
	}

	if (pArray) // this would be NULL only if there was no array and the new size is also 0
		pArray->count = size;
}

void ea32SetSizeFast_dbg(EArray32Handle* handle, int size MEM_DBG_PARMS) // set the number of items in pArray
{
	// this is a little confusing because Size = count within the pArray structure, Capacity = size
	EArray32* pArray=NULL;

	if (*handle)
	{
		pArray = EArray32FromHandle(*handle);
	}

	// don't bother creating the array or "raising" its capacity if the new size is 0
	if (size)
	{
		// set the new capacity, creating the array if necessary
		if (!(*handle) || ABS(pArray->size) < size) 
		{
			ea32SetCapacity_dbg(handle, size MEM_DBG_PARMS_CALL);
			pArray = EArray32FromHandle(*handle);
		}
	}

	if (pArray) // this would be NULL only if there was no array and the new size is also 0
		pArray->count = size;
}

int ea32Push_dbg(EArray32Handle* handle, U32 value MEM_DBG_PARMS) // add to the end of the list, returns the index it was added at (the new size)
{
	EArray32* pArray=NULL;
	int size = 0;

	if (*handle)
	{
		pArray = EArray32FromHandle(*handle);
		size = pArray->size;
	}

	if (!(*handle) || pArray->count == ABS(pArray->size))
	{
		ea32SetCapacity_dbg(handle, max(4,ABS(size)*2) MEM_DBG_PARMS_CALL);
	}
	pArray = EArray32FromHandle(*handle);
	pArray->values[pArray->count++] = value;
	return pArray->count-1;
}

int ea32PushUnique_dbg(EArray32Handle* handle, U32 value MEM_DBG_PARMS) // add to the end of the list, returns the index it was added at (the new size)
{
	int idx = ea32Find(handle, value);

	if (idx < 0)
		idx = ea32Push_dbg(handle, value MEM_DBG_PARMS_CALL);

	return idx;
}

int ea32PushArray_dbg(EArray32Handle* handle, ccEArray32Handle* src MEM_DBG_PARMS) // add to the end of the list, returns the index it was added at
{
	int count = 0;

	if (*handle)
	{
		count = ea32Size( handle );
	}

	if (src)
	{
		int srcCount = ea32Size(src);
		ea32SetSize_dbg(handle, count + srcCount MEM_DBG_PARMS_CALL);
		CopyStructs(*handle + count, *src, srcCount);
	}

	return count;
}

U32 ea32Pop(EArray32Handle* handle) // remove the last item from the list
{
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle) || !pArray->count)
		return 0;
	pArray->count--;
	return pArray->values[pArray->count];
}

void ea32Set_dbg(EArray32Handle* handle, U32 value, int i MEM_DBG_PARMS) // set i'th element (zero-based)
{
	EArray32* pArray=NULL;
	if (i < 0)
		return;

	if (*handle)
	{
		pArray = EArray32FromHandle(*handle);
	}

	if (!(*handle) || ABS(pArray->size) <= i)
	{
		ea32SetCapacity_dbg(handle, pow2(i+1) MEM_DBG_PARMS_CALL);
		pArray = EArray32FromHandle(*handle);
	}
	if (pArray->count <= i)
	{
		ZeroStructsForce(pArray->values + pArray->count, i + 1 - pArray->count);
		pArray->count = i + 1;
	}
	pArray->values[i] = value;
}

U32 ea32Get(ccEArray32Handle* handle, int i)	// get i'th element (zero-based), NULL on error
{
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle) || pArray->count <= i || i < 0)
		return 0;
	return pArray->values[i];
}

void ea32Insert_dbg(EArray32Handle* handle, U32 value, int i MEM_DBG_PARMS) // insert before i'th position, will not insert on error (i == -1, etc.)
{
	EArray32* pArray=NULL;
	int count = 0;
	int size = 0;

	if (i < 0)
		return;

	if (*handle)
	{
		pArray = EArray32FromHandle(*handle);
		count = pArray->count;
		size = pArray->size;
	}

	if (count < i)
		return;
	if (count == ABS(size))
	{
		ea32SetCapacity_dbg(handle, max(4,ABS(size)*2) MEM_DBG_PARMS_CALL);
		pArray = EArray32FromHandle(*handle);
	}
	CopyStructsFromOffset(pArray->values + i + 1, -1, pArray->count - i);
	pArray->count++;
	pArray->values[i] = value;
}

int ea32BInsert_dbg(EArray32Handle* handle, U32 value MEM_DBG_PARMS) // insert into the sorted position, returns the inserted index
{
	int i = 0;
	if (*handle)
		i = (int) bfind(&value, *handle, ea32Size(handle), sizeof(handle[0]), intCmp);
	ea32Insert_dbg(handle, value, i, caller_fname, line);
	return i;
}

int ea32BInsertUnique_dbg(EArray32Handle* handle, U32 value MEM_DBG_PARMS) // inserts a unique value into the sorted position, returns the inserted index (-1 if it already exists)
{
	int i = 0;
	if (*handle)
	{
		int size = ea32Size(handle);
		i = (int) bfind(&value, *handle, size, sizeof(handle[0]), intCmp);
		if (i < size && (*handle)[i] == value)
			return -1;
	}
	ea32Insert_dbg(handle, value, i, caller_fname, line);
	return i;
}

U32 ea32Remove(EArray32Handle* handle, int i) // remove the i'th element, NULL on error
{
	U32 value;
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle) || pArray->count <= i || i < 0) return 0;
	value = pArray->values[i];
	CopyStructsFromOffset(pArray->values + i, 1, pArray->count - i - 1);
	pArray->count--;
	return value;
}

U32 ea32RemoveFast(EArray32Handle* handle, int i) // remove the i'th element, and move the last element into this place DOES NOT KEEP ORDER
{	
	U32 value;
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle) || pArray->count <= i || i < 0) return 0;
	value = pArray->values[i];
	pArray->values[i] = pArray->values[--pArray->count];
	return value;
}

void ea32RemoveRange(EArray32Handle* handle, int start, int count) // remove count elements, starting with start
{
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle) || pArray->count <= start || start < 0) return;

	if (start + count >= pArray->count)
	{
		pArray->count = start;
		return;
	}

	CopyStructsFromOffset(pArray->values + start, count, pArray->count - start - count);

	pArray->count -= count;
}

int	ea32Find(ccEArray32Handle* handle, U32 value)	// find the first element that matches structptr, returns -1 on error
{
	int i;
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle)) return -1;
	for (i = 0; i < pArray->count; i++)
	{
		if (pArray->values[i] == value) return i;
	}
	return -1;
}

U32 ea32BFindAndRemove(EArray32Handle* handle, U32 value)
{
	int	idx, size;
	if (!(*handle)) return -1;
	size = ea32Size(handle);
	idx = (int) bfind(&value, *handle, size, sizeof(handle[0]), intCmp);
	if (idx < size && (*handle)[idx] == value)
		return ea32Remove(handle, idx);
	return -1;
}

int	ea32FindAndRemove(EArray32Handle* handle, U32 value)
{
	int	idx;

	if ((idx = ea32Find(handle,value)) >= 0)
		ea32Remove(handle,idx);
	return idx;
}

int	ea32FindAndRemoveFast(EArray32Handle* handle, U32 value)
{
	int	idx;

	if ((idx = ea32Find(handle,value)) >= 0)
		ea32RemoveFast(handle,idx);
	return idx;
}

void ea32Reverse(EArray32Handle* handle)
{
	EArray32* pArray = EArray32FromHandle(*handle);
	U32 *head, *tail;
	if (!(*handle)) return;

	head = pArray->values;
	tail = head + pArray->count - 1;
	
	// Start the index at the two "outer most" elements of the pArray.
	// Each interation through the loop, swap the two elements that are indexed,
	// then bring the indices one step closer to each other.
	for(; head < tail; head++, tail--)
	{
		U32 temp = *head;
		*head = *tail;
		*tail = temp;
	}
}

void ea32Copy_dbg(EArray32Handle* dest, ccEArray32Handle* src MEM_DBG_PARMS)
{
	int size;
	EArray32 *d, *s;

	size = ea32Size(src);
	if (size)
	{
		if (!*dest)
			ea32Create_dbg(dest MEM_DBG_PARMS_CALL);
		ea32SetSize_dbg(dest, size MEM_DBG_PARMS_CALL);
		d = EArray32FromHandle(*dest);
		s = EArray32FromHandle(*src);
		memcpy(d->values, s->values, sizeof(d->values[0])*size);
	}
	else
	{	
		if (*dest)
		{
			ea32SetSize(dest, 0);
		}
	}
}

void ea32Append_dbg(EArray32Handle* dest, ccEArray32Handle* src MEM_DBG_PARMS)
{
	int size, old_size;
	EArray32 *d, *s;

	size = ea32Size(src);
	if (size)
	{
		old_size = ea32Size(dest);
		if (!*dest)
			ea32Create_dbg(dest MEM_DBG_PARMS_CALL);
		ea32SetSize_dbg(dest, size+old_size MEM_DBG_PARMS_CALL);
		d = EArray32FromHandle(*dest);
		s = EArray32FromHandle(*src);
		memcpy(d->values+old_size, s->values, sizeof(d->values[0])*size);
	}
}

void ea32Prepend_dbg(EArray32Handle* dest, ccEArray32Handle* src MEM_DBG_PARMS)
{
	int size, old_size;
	EArray32 *d, *s;

	size = ea32Size(src);
	if (size)
	{
		old_size = ea32Size(dest);
		if (!*dest)
			ea32Create_dbg(dest MEM_DBG_PARMS_CALL);
		ea32SetSize_dbg(dest, size+old_size MEM_DBG_PARMS_CALL);
		d = EArray32FromHandle(*dest);
		s = EArray32FromHandle(*src);
		memcpy(d->values, d->values+old_size, sizeof(d->values[0])*old_size);
		memcpy(d->values, s->values, sizeof(d->values[0])*size);
	}
}

void ea32Compress_dbg(EArray32Handle *dst, ccEArray32Handle *src, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS)
{
	const EArray32 *src_array;
	EArray32 *dst_array;
	size_t len;

	if (!(*src)) {
		*dst=NULL;
		return;
	}
	if (memAllocator)
	{
		len = ea32MemUsage(src, false);
		src_array = EArray32FromHandle(*src);
		dst_array = memAllocator(customData, len);
		memcpy(dst_array, src_array, len);
		dst_array->size = dst_array->count;
		*dst = HandleFromEArray32(dst_array);
	}
	else
	{
		ea32Create_dbg(dst MEM_DBG_PARMS_CALL);
		ea32Copy_dbg(dst, src MEM_DBG_PARMS_CALL);
	}
}

EArray32Handle ea32Temp(U32 value)
{
	static U32* ei = 0;
	if (!ei)
		ea32Create(&ei);
	ea32SetSize(&ei, 1);
	ei[0] = value;
	return ei;
}

void ea32Swap(EArray32Handle* handle, int i, int j)
{
	U32 temp;
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle) || pArray->count <= i || i < 0 || pArray->count <= j || j < 0) return;
	temp = pArray->values[i];
	pArray->values[i] = pArray->values[j];
	pArray->values[j] = temp;
}

void ea32Move(EArray32Handle* handle, int dest, int src)
{
	U32 temp;
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle) || pArray->count <= src || src < 0 || pArray->count <= dest || dest < 0) return;
	temp = pArray->values[src];
	if(src < dest)
		CopyStructsFromOffset(pArray->values + src, 1, dest - src);
	else
		CopyStructsFromOffset(pArray->values + dest + 1, -1, src - dest);
	pArray->values[dest] = temp;
}

/////////////////////////////////////////////////// EArrayInt util

int eaiCompare(const int*const* array1, const int*const* array2)
{
	int len1, len2;
	int i;
	len1 = ea32Size(array1);
	len2 = ea32Size(array2);
	if (len1 != len2) {
		return len1 - len2;
	}
	if (len1 <= 0)
		return 0;
	for (i=0; i<len1-1 && (*array1)[i]==(*array2)[i]; i++)
		;
	return (*array1)[i]-(*array2)[i];
}

/////////////////////////////////////////////////// EArrayF32 util

int eafCompare(const F32*const* array1, const F32*const* array2)
{
	int len1, len2;
	int i;
	len1 = ea32Size(array1);
	len2 = ea32Size(array2);
	if (len1 != len2) {
		return len1 - len2;
	}
	if (len1 <= 0)
		return 0;
	for (i=0; i<len1-1 && (*array1)[i]==(*array2)[i]; i++)
		;
	return (*array1)[i]-(*array2)[i];
}

/////////////////////////////////////////////////// StringArray util

// returns index, or -1
int StringArrayFind(const char** pArray, const char* elem)
{
	int result = -1;
	int i, n = eaSize(&pArray);
	for (i = 0; i < n; i++)
	{
		if (!stricmp(pArray[i], elem))
		{
			result = i;
			break;
		}
	}
	return result;
}

// returns index, or -1, compares partial strings
int StringArrayNFind(const char** pArray, const char* elem)
{
	int result = -1;
	int i, n = eaSize(&pArray);

	for (i = 0; i < n; i++)
	{
		if(!strnicmp(pArray[i],elem,strlen(elem)))
		{
			result = i;
			break;
		}
	}
	return result;
}

// dumb implementation of this, just something for small arrays
int StringArrayIntersection(char*** result, char** lhs, char** rhs)
{
	int i, n;
	eaSetSize(result, 0);
	n = eaSize(&lhs);
	for (i = 0; i < n; i++)
	{
		if (StringArrayFind(rhs, lhs[i]) >= 0 &&
			!(StringArrayFind(*result, lhs[i]) >= 0))
		{
			eaPush(result, lhs[i]);
		}
	}
	return eaSize(result);
}

void StringArrayPrint(char* buf, int buflen, char** pArray)
{
#define PUSH(str) \
	{ if (buflen <= 1) return; \
	strncpyt(bufptr, str, buflen); \
	len = (int)strlen(bufptr); \
	buflen -= len; \
	bufptr += len; }

	int i, n, len;
	char* bufptr = buf; 
	*bufptr = '\0';
	PUSH("(")
	n = eaSize(&pArray);
	for (i = 0; i < n; i++)
	{
		if (i != 0)
			PUSH(", ")
		PUSH(pArray[i])
	}
	PUSH(")")
#undef PUSH
}

// Indexed EArray support (sorted earrays of parsetable-created objects



int eaIndexedEnableAfterTemplateCheck(cEArrayHandle *handle, ParseTable tpi[] MEM_DBG_PARMS)
{
	void *pOldTable;
	EArray* pArray;

	if (!(*handle)) eaCreateInternal(handle MEM_DBG_PARMS_CALL);	// Auto-create pArray.
	pArray = EArrayFromHandle(*handle);

	if (pOldTable = GetIndexedTable(pArray))
	{
		assertmsg(pOldTable == tpi,"Changing indexed EArray to another type is invalid (disable indexed first)");
		return 1;
	}

	if (eaSortUsingKeyVoid(handle,tpi))
	{
		eaSetOptions(pArray, tpi);
		return 1;
	}
	else
	{
		assertmsg(0, "Invalidly attempted to enable indexing on array");
	}

	return 0;
}

// Turn on indexed mode, which asserts on invalid actions
int eaIndexedEnable_dbg(cEArrayHandle *handle, ParseTable tpi[] MEM_DBG_PARMS)
{
	// Check to see if we should return a template handle instead of an actual EArray...

	if(*handle && !eaSize(handle) && !GetIndexedTable(EArrayFromHandle(*handle)) && !eaIsIndexedTemplate(handle))
	{
		// Something is here, but it's empty and not an indexed EArray.
		// Destroy it and just return a template!

		eaDestroy(handle);
		eaIndexedGetTemplate(tpi, handle MEM_DBG_PARMS_CALL);
		return 0;
	}

	if(!(*handle))
	{
		// Nothing there at all? return a template.

		eaIndexedGetTemplate(tpi, handle MEM_DBG_PARMS_CALL);
		return 0;
	}

	return eaIndexedEnableAfterTemplateCheck(handle, tpi MEM_DBG_PARMS_CALL);
}

// Turn off indexed mode
int eaIndexedDisable_dbg(cEArrayHandle *handle MEM_DBG_PARMS)
{
	EArray* pArray;
	if (!(*handle)) return 0;

	if(eaIsIndexedTemplate(handle))
		eaCreateIndexedFromTemplate(handle, handle MEM_DBG_PARMS_CALL);

	pArray = EArrayFromHandle(*handle);

	eaSetOptions(pArray, NULL);

	return 1;
}

int eaSortUsingKeyVoid(cEArrayHandle * earray, ParseTable tpi[])
{
	ParserSortData sortContext;
	int keyColumn;
	ParserCompareFieldFunction compareFunc = NULL;

	if (!tpi || (keyColumn = ParserGetTableKeyColumn(tpi)) < 0)
	{
		// No key
		return 0;
	}
	sortContext.tpi = tpi;
	sortContext.column = keyColumn;

	if (!(compareFunc = ParserGetCompareFunction(tpi,keyColumn)))
	{
		// Invalid key type
		return 0;
	}

	if (*earray && eaSize(earray))
		qsort_s((void **)(*earray), eaSize(earray), sizeof(void *), compareFunc,&sortContext);
	return 1;
}

typedef struct StableSortContext {
	ParserSortData sortContext;
	ParserCompareFieldFunction compareFunc;
} StableSortContext;

int StableSortComp(const void* rawItem1, const void* rawItem2, const void* rawContext)
{
	const StableSortContext* context = (const StableSortContext*)rawContext;
	return context->compareFunc(&context->sortContext, (const void**)rawItem1, (const void**)rawItem2);
}

int eaStableSortUsingColumnVoid(cEArrayHandle *earray, ParseTable tpi[], int col)
{
	StableSortContext ctx;

	if (!tpi || col >= ParserGetTableNumColumns(tpi) || col < 0)
	{	// No key
		return 0;
	}
	if (!(ctx.compareFunc = ParserGetCompareFunction(tpi,col)))
	{	// Invalid key type
		return 0;
	}

	ctx.sortContext.tpi = tpi;
	ctx.sortContext.column = col;

	//This needs to be replaced with Merge Sort!
	if (*earray && eaSize(earray))
		mergeSort((void **)(*earray),
					eaSize(earray),
					sizeof(void *),
					&ctx,
					StableSortComp);
	return 1;
}

void *eaIndexedGetTable(cEArrayHandle *handle)
{
	return eaIndexedGetTable_inline(handle);
}

S64 GetKeyValueForIndexedCompareCache(const void *pStruct, ParseTableInfo_IndexedCompareCache *pCompareCache)
{
	void *pOffsetPointer = (void*)(((U8*)pStruct) + pCompareCache->storeoffset);

	switch (pCompareCache->eCompareType)
	{
	case INDEXCOMPARETYPE_INT8:
		return *((U8*)pOffsetPointer);
	case INDEXCOMPARETYPE_INT16:
		return *((S16*)pOffsetPointer);
	case INDEXCOMPARETYPE_INT32:
		return *((S32*)pOffsetPointer);
	case INDEXCOMPARETYPE_INT64:
		return *((S64*)pOffsetPointer);
	case INDEXCOMPARETYPE_STRING_EMBEDDED:
	case INDEXCOMPARETYPE_STRUCT_EMBEDDED:
		return (S64)((intptr_t)pOffsetPointer);
	case INDEXCOMPARETYPE_STRING_POINTER:
	case INDEXCOMPARETYPE_STRUCT_POINTER:
		return (S64)((intptr_t)((*((void**)pOffsetPointer))));
	case INDEXCOMPARETYPE_REF:
		return ((S64)((intptr_t)(RefSystem_StringFromHandle(pOffsetPointer))));
	default:
		assert(0);
		return 0;
	}
}

int ComapreWithCompareCache_KeyToStruct(ParseTableInfo_IndexedCompareCache *pCompareCache, S64 iKeyValue, void *pStruct)
{
	S64 iOtherKey = GetKeyValueForIndexedCompareCache(pStruct, pCompareCache);
	int iCmp;

	switch (pCompareCache->eCompareType)
	{
	case INDEXCOMPARETYPE_INT8:
	case INDEXCOMPARETYPE_INT16:
	case INDEXCOMPARETYPE_INT32:
	case INDEXCOMPARETYPE_INT64:
		if (iKeyValue < iOtherKey)
		{
			return -1;
		}
		if (iKeyValue > iOtherKey)
		{
			return 1;
		}
		return 0;

	case INDEXCOMPARETYPE_STRING_EMBEDDED:
	case INDEXCOMPARETYPE_STRING_POINTER:
	case INDEXCOMPARETYPE_REF:
		iCmp = stricmp(iKeyValue ? (char*)((intptr_t)iKeyValue) : "", iOtherKey ?  (char*)((intptr_t)iOtherKey) : "");
		if (iCmp < 0)
		{
			return -1;
		}
		if (iCmp == 0)
		{
			return 0;
		}
			
		return 1;
		

	case INDEXCOMPARETYPE_STRUCT_EMBEDDED:
	case INDEXCOMPARETYPE_STRUCT_POINTER:
		if (iKeyValue && iOtherKey)
		{
			iCmp = StructCompare(pCompareCache->pTPIForStructComparing, (void*)((intptr_t)iKeyValue), (void*)((intptr_t)iOtherKey), 0, 0, 0);
			if (iCmp < 0)
			{
				return -1;
			}
			if (iCmp == 0)
			{
				return 0;
			}
				
			return 1;
		}
			
		if (iKeyValue)
		{
			return 1;
		}

		if (iOtherKey)
		{
			return -1;
		}
	}

		
	return 0;
	

}

//returns the index of the matching item, and sets pbFound to true if the precise value was found, otherwise returns
//the index before which the item should be inserted
int eaBFindWithCompareCache(EArray *pArray, ParseTableInfo_IndexedCompareCache *pCompareCache, S64 iKeyValue, bool *pbFound)
{
	int iCmp = 0;
	int iMin, iMax;
	int iMidPoint = -1;
	
	if (pArray->count == 0)
	{
		return 0;
	}

	iMin = 0;
	iMax = pArray->count -1;

	while (iMin < iMax - 1)
	{
		iMidPoint = (iMin + iMax) / 2;
		iCmp = ComapreWithCompareCache_KeyToStruct(pCompareCache, iKeyValue, pArray->structptrs[iMidPoint]);

		switch (iCmp)
		{
		case -1:
			iMax = iMidPoint - 1;
			break;

		case 0:
			*pbFound = true;
			return iMidPoint;

		case 1:
			iMin = iMidPoint + 1;
		}
	}

	//we now have either 1 or 2 elements left, and no comparisons ever done with either one
	
	//don't redo the most recent comparison if we don't have to
	if (iMin != iMidPoint)
	{
		iCmp = ComapreWithCompareCache_KeyToStruct(pCompareCache, iKeyValue, pArray->structptrs[iMin]);
	}

	switch (iCmp)
	{
	case -1:
		return iMin;
	case 0:
		*pbFound = true;
		return iMin;	
	}

	if (iMax == iMin)
	{
		return iMin + 1;
	}

	//if we get here, then we were left with 2 elements, and just have to do a compare with iMax
	iCmp = ComapreWithCompareCache_KeyToStruct(pCompareCache, iKeyValue, pArray->structptrs[iMax]);

	switch (iCmp)
	{
	case -1:
		return iMax;
	case 0:
		*pbFound = true;
		return iMax;	
	}

	return iMax + 1;
}

int eaIndexedAdd_dbg(cEArrayHandle * handle, const void *substructure MEM_DBG_PARMS)
{
	ParseTable *tpi;
	ParserSortData sortContext;
	ParserCompareFieldFunction compareFunc = NULL;
	EArray* pArray;
	int index;
	ParseTableInfo *pTableInfo;

	PERFINFO_AUTO_START_L2("eaIndexedAdd",1);

	if(eaIsIndexedTemplate(handle))
		eaCreateIndexedFromTemplate(handle, handle MEM_DBG_PARMS_CALL);

	if (!(*handle)) 
	{
		assertmsgf(0,"eaIndexedAdd must be called on an Indexed earray");
	}
	pArray = EArrayFromHandle(*handle);

	assertmsg(tpi = GetIndexedTable(pArray), "eaIndexedAdd must be called on an Indexed earray");

	pTableInfo = ParserGetTableInfo(tpi);

	if (pTableInfo && pTableInfo->IndexedCompareCache.eCompareType)
	{
		bool bAlreadyInList = false;
		S64 iKeyValue = GetKeyValueForIndexedCompareCache(substructure, &pTableInfo->IndexedCompareCache);
		int iInsertIndex = eaBFindWithCompareCache(pArray, &pTableInfo->IndexedCompareCache, iKeyValue, &bAlreadyInList);

		if (bAlreadyInList)
		{
			PERFINFO_AUTO_STOP_L2();
			return 0; // something with this key is already there
		}
		else
		{
			int result = eaInsert_dbg(handle,substructure,iInsertIndex,true MEM_DBG_PARMS_CALL);
			PERFINFO_AUTO_STOP_L2();
			return result;
		}
	}
	else
	{
		sortContext.tpi = tpi;
		sortContext.column = ParserGetTableKeyColumn(tpi);

		if (!(compareFunc = ParserGetCompareFunction(tpi,sortContext.column)))
		{
			assertmsgf(0,"Invalid Type for Key %s in structure %s", tpi[sortContext.column].name, ParserGetTableName(tpi));
			return 0;
		}

/*
		//temporary snippet to check for any escape-requiring characters in any key
		{
			char *pTemp = NULL;
			estrStackCreate(&pTemp);
			objGetKeyEString(tpi, substructure, &pTemp);
			if (strpbrk(pTemp, ESCAPABLE_CHARACTERS))
			{
				AssertOrAlert("BAD_KEY_FOR_INDEXED_ADD", "Trying to add a %s with key %s to an indexed earray. This key contains escapable characters, this is very bad",
					ParserGetTableName(tpi), pTemp);
			}

			estrDestroy(&pTemp);
		}*/

		index = (int)eaBFind_s(*handle, compareFunc, &sortContext, substructure);
	
		if (index != eaSize(handle) && compareFunc(&sortContext, &substructure, &((*handle)[index])) == 0)
		{
			PERFINFO_AUTO_STOP_L2();
			return 0; // something with this key is already there
		}
		else
		{
			int result = eaInsert_dbg(handle,substructure,index,true MEM_DBG_PARMS_CALL);
			PERFINFO_AUTO_STOP_L2();
			return result;
		}
	}
}

int eaIndexedFind(cEArrayHandle * handle, const void *substructure)
{
	ParseTable *tpi;
	ParserSortData sortContext;
	ParserCompareFieldFunction compareFunc = NULL;
	EArray* pArray;
	int index;
	ParseTableInfo *pTableInfo;

	PERFINFO_AUTO_START_L2("eaIndexedFind",1);

	if (!(*handle)) 
	{
		assertmsgf(0,"eaIndexedFind must be called on an Indexed earray");
	}
	pArray = EArrayFromHandle(*handle);

	assertmsg(tpi = GetIndexedTable(pArray), "eaIndexedFind must be called on an Indexed earray");

	pTableInfo = ParserGetTableInfo(tpi);

	if (pTableInfo && pTableInfo->IndexedCompareCache.eCompareType)
	{
		bool bAlreadyInList = false;
		S64 iKeyValue = GetKeyValueForIndexedCompareCache(substructure, &pTableInfo->IndexedCompareCache);
		int iIndex = eaBFindWithCompareCache(pArray, &pTableInfo->IndexedCompareCache, iKeyValue, &bAlreadyInList);

		if (bAlreadyInList)
		{
			PERFINFO_AUTO_STOP_L2();
			return iIndex; // something with this key is already there
		}
		else
		{
			PERFINFO_AUTO_STOP_L2();
			return -1;
		}
	}
	else
	{

		sortContext.tpi = tpi;
		sortContext.column = ParserGetTableKeyColumn(tpi);

		if (!(compareFunc = ParserGetCompareFunction(tpi,sortContext.column)))
		{
			assertmsgf(0,"Invalid Type for Key %s in structure %s", tpi[sortContext.column].name, ParserGetTableName(tpi));
			return 0;
		}

		index = (int)eaBFind_s(*handle, compareFunc, &sortContext, substructure);

		if (index != eaSize(handle) && compareFunc(&sortContext, &substructure, &((*handle)[index])) == 0)
		{
			PERFINFO_AUTO_STOP_L2();
			return index;
		}
		else
		{
			PERFINFO_AUTO_STOP_L2();
			return -1;
		}
	}
}

// These are slow for now
int eaIndexedFindUsingInt(ccEArrayHandle* handle, S64 key)
{
	int i;
	void *foundStruct;
	ParseTable *tpi;

	ParserSearchIntFunction compareFunc = NULL;
	EArray* pArray;
	ParseTableInfo *pTableInfo;

	if (!eaSize(handle))
	{
		return -1;
	}
	PERFINFO_AUTO_START_L2("eaIndexedFindUsingInt",1);

	pArray = EArrayFromHandle(*handle);

	assertmsg(tpi = GetIndexedTable(pArray), "eaIndexedFind must be called on an Indexed earray");


	pTableInfo = ParserGetTableInfo(tpi);

	if (pTableInfo && pTableInfo->IndexedCompareCache.eCompareType)
	{
		bool bAlreadyInList = false;
		int iIndex;

		if (INDEXCOMPARETYPE_IS_SET_AND_NOT_INT(pTableInfo->IndexedCompareCache.eCompareType))
		{
			//special case... you can do an indexedGetUsingInt of a string field, it just converts the int to string. This is equivalent
			//to using IntSearchStringField

			if (INDEXCOMPARETYPE_IS_STRING(pTableInfo->IndexedCompareCache.eCompareType))
			{
				char temp[32];
				sprintf(temp, "%I64d", key);

				iIndex = eaBFindWithCompareCache(pArray, &pTableInfo->IndexedCompareCache, (intptr_t)(temp), &bAlreadyInList);

				if (bAlreadyInList)
				{
					PERFINFO_AUTO_STOP_L2();
					return iIndex; 
				}
				else
				{
					PERFINFO_AUTO_STOP_L2();
					return -1;
				}
			}


			assertmsgf(0,"Invalid Type for Key %s in structure %s", tpi[ParserGetTableKeyColumn(tpi)].name, ParserGetTableName(tpi));
		}

		// Hack to make sure 8-bit is the unsigned representation, 16-bit and 32-bit are signed representations
		if (pTableInfo->IndexedCompareCache.eCompareType == INDEXCOMPARETYPE_INT8 && key < 0)
			key = (S64)(U8)(S8) key;
		else if (pTableInfo->IndexedCompareCache.eCompareType == INDEXCOMPARETYPE_INT16 && key >= ((U16) 1<<15))
			key = (S64)(S16)(U16) key;
		else if (pTableInfo->IndexedCompareCache.eCompareType == INDEXCOMPARETYPE_INT32 && key >= ((U32) 1<<31))
			key = (S64)(S32)(U32) key;
		
		iIndex = eaBFindWithCompareCache(pArray, &pTableInfo->IndexedCompareCache, key, &bAlreadyInList);

		if (bAlreadyInList)
		{
			PERFINFO_AUTO_STOP_L2();
			return iIndex; 
		}
		else
		{
			PERFINFO_AUTO_STOP_L2();
			return -1;
		}
	}
	else
	{
		ParserSortData sortContext;

		sortContext.tpi = tpi;
		sortContext.column = ParserGetTableKeyColumn(tpi);


		if (!(compareFunc = ParserGetIntSearchFunction(tpi,sortContext.column)))
		{
			assertmsgf(0,"Invalid Type for Key %s in structure %s", tpi[sortContext.column].name, ParserGetTableName(tpi));
		}



		if (foundStruct = bsearch_s(&key,*handle,eaSize(handle),sizeof(void *),compareFunc,&sortContext))
		{
			i = (int)((char *)foundStruct - (char *)*handle) / sizeof(void *);
			PERFINFO_AUTO_STOP_L2();
			return i; // something with this key is already there
		}
		PERFINFO_AUTO_STOP_L2();
		return -1;
	}
}

int eaIndexedFindUsingString(ccEArrayHandle* handle, const char* key)
{
	int i;
	void *foundStruct;
	ParseTable *tpi;

	EArray* pArray;
	ParseTableInfo *pTableInfo;

	ParserSearchStringFunction compareFunc = NULL;
	ParserSortData sortContext;

	if (!eaSize(handle))
	{
		return -1;
	}
	PERFINFO_AUTO_START_L2("eaIndexedFindUsingString",1);

	pArray = EArrayFromHandle(*handle);

	assertmsg(tpi = GetIndexedTable(pArray), "eaIndexedFind must be called on an Indexed earray");



	sortContext.tpi = tpi;
	sortContext.column = ParserGetTableKeyColumn(tpi);

	//special case... if we are searching an int field with a subtable, and our string is an enum for it, convert it to int and do a
	//(much faster) int search
	if (TYPE_INFO(tpi[sortContext.column].type).interpretfield(tpi, sortContext.column, SubtableField) == StaticDefineList)
	{
		if (tpi[sortContext.column].subtable)
		{
			if (TOK_GET_TYPE(tpi[sortContext.column].type) == TOK_INT_X)
			{
				int iDecode = StaticDefine_FastStringToInt(tpi[sortContext.column].subtable, key, INT_MIN);
				if (iDecode != INT_MIN)
				{
					int iRetVal = eaIndexedFindUsingInt(handle, iDecode);
					PERFINFO_AUTO_STOP_L2();
					return iRetVal;
				}
			}
		}
	}


	pTableInfo = ParserGetTableInfo(tpi);

	if (pTableInfo && pTableInfo->IndexedCompareCache.eCompareType)
	{
		bool bAlreadyInList = false;
		int iIndex;

		switch (pTableInfo->IndexedCompareCache.eCompareType)
		{
		case INDEXCOMPARETYPE_INT8:
		case INDEXCOMPARETYPE_INT16:
		case INDEXCOMPARETYPE_INT32:
		case INDEXCOMPARETYPE_INT64:
			//convert the string to an int, hope for the best. This seems like a dumb case, but the old eaIndex
			//code supported it, so I'll try to do it as close to the same way as possible
			iIndex = eaBFindWithCompareCache(pArray, &pTableInfo->IndexedCompareCache, atoi64(key), &bAlreadyInList);
			break;

		case INDEXCOMPARETYPE_STRING_EMBEDDED:
		case INDEXCOMPARETYPE_STRING_POINTER:
		case INDEXCOMPARETYPE_REF:
			iIndex = eaBFindWithCompareCache(pArray, &pTableInfo->IndexedCompareCache, (intptr_t)key, &bAlreadyInList);
			break;

		case INDEXCOMPARETYPE_STRUCT_EMBEDDED:
		case INDEXCOMPARETYPE_STRUCT_POINTER:
			//very bizarre case where you are doing eaIndexedFindUsingString to do a lookup in something that
			//is indexed by a struct. I relaly hope no one is doing this.. infact, I'm just going to jump to the old code
			//and let the old code handle it
			ONCE(Errorf("Someone is doing an indexedGetUsingString on struct-keyed array %s. Let Alex W know that you saw this",
				ParserGetTableName(pTableInfo->IndexedCompareCache.pTPIForStructComparing)));

			goto DoItTheOldWay;

		default:
			assertmsgf(0, "Unknown IndexCompareType... ParseTableInfo must have gotten corrupted");

		}

		if (bAlreadyInList)
		{
			PERFINFO_AUTO_STOP_L2();
			return iIndex; 
		}
		else
		{
			PERFINFO_AUTO_STOP_L2();
			return -1;
		}
	}
	else
	{
		DoItTheOldWay:

		if (!(compareFunc = ParserGetStringSearchFunction(tpi,sortContext.column)))
		{
			assertmsgf(0,"Invalid Type for Key %s in structure %s", tpi[sortContext.column].name, ParserGetTableName(tpi));
		}

		if (foundStruct = bsearch_s(key,*handle,eaSize(handle),sizeof(void *),compareFunc,&sortContext))
		{
			i = (int)((char *)foundStruct - (char *)*handle) / sizeof(void *);
			PERFINFO_AUTO_STOP_L2();
			return i; // something with this key is already there
		}
		PERFINFO_AUTO_STOP_L2();
		return -1;
	}
}

bool eaIndexedPushUsingStringIfPossible_dbg(cEArrayHandle* handle, const char *key, const void *substructure MEM_DBG_PARMS)
{
	ParseTable *tpi;
	const char *pObjKey;
	int iKeyColumn;
	EArray* pArray = EArrayFromHandle(*handle);


	if (!key || !key[0])
	{
		return false;
	}

	if (eaIndexedGetUsingString(handle, key))
	{
		return false;
	}



	assertmsg(tpi = GetIndexedTable(pArray), "eaIndexedPushUsingStringIfPossible must be called on an Indexed earray");
	iKeyColumn = ParserGetTableKeyColumn(tpi);

    if (TOK_GET_TYPE(tpi[iKeyColumn].type) == TOK_REFERENCE_X)
    {
        pObjKey = TokenStoreGetRefString_inline(tpi, &tpi[iKeyColumn], iKeyColumn, substructure, 0, NULL);
    }
    else
    {
	    pObjKey = TokenStoreGetString_inline(tpi, &tpi[iKeyColumn], iKeyColumn, substructure, 0, NULL);
    }

	if (stricmp(pObjKey, key) != 0)
	{
		devassertmsgf(stricmp(pObjKey, key) == 0, "eaIndexedPushUsingStringIfPossible called with non-matching keys %s and %s for TPI %s",
			pObjKey, key, ParserGetTableName(tpi));
		return false;
	}

	eaIndexedAdd_dbg(handle, substructure MEM_DBG_PARMS_CALL);

	return true;
}


bool eaIndexedPushUsingIntIfPossible_dbg(cEArrayHandle* handle, S64 key, const void *substructure MEM_DBG_PARMS)
{
	ParseTable *tpi;
	int iKeyColumn;
	EArray* pArray = EArrayFromHandle(*handle);
	S64 iObjKey;


	if (!key)
	{
		return false;
	}

	if (eaIndexedGetUsingInt(handle, key))
	{
		return false;
	}

	assertmsg(tpi = GetIndexedTable(pArray), "eaIndexedPushUsingStringIfPossible must be called on an Indexed earray");
	iKeyColumn = ParserGetTableKeyColumn(tpi);

	iObjKey = TokenStoreGetIntAuto(tpi, iKeyColumn, substructure, 0, NULL);


	if (iObjKey != key)
	{
		devassertmsgf(iObjKey == key, "eaIndexedPushUsingIntIfPossible called with non-matching keys %"FORM_LL"d and %"FORM_LL"d for TPI %s",
			iObjKey, key, ParserGetTableName(tpi));
		return false;
	}

	eaIndexedAdd_dbg(handle, substructure MEM_DBG_PARMS_CALL);

	return true;
}



void *eaIndexedGetUsingInt(ccEArrayHandle* handle, S64 key)
{
	int index = eaIndexedFindUsingInt(handle, key);
	if (index < 0) 
	{
		return NULL;
	}
	return eaGetVoid(handle,index);
}

void *eaIndexedGetUsingString(ccEArrayHandle* handle, const char* key)
{
	int index = eaIndexedFindUsingString(handle, key);
	if (index < 0) 
	{
		return NULL;
	}
	return eaGetVoid(handle,index);
}

void *eaIndexedRemoveUsingInt(cEArrayHandle* handle, S64 key)
{
	int index = eaIndexedFindUsingInt(handle, key);
	if (index < 0) 
	{
		return NULL;
	}
	return eaRemoveVoid(handle,index);
}

void *eaIndexedRemoveUsingString(cEArrayHandle* handle, const char* key)
{
	int index = eaIndexedFindUsingString(handle, key);
	if (index < 0) 
	{
		return NULL;
	}
	return eaRemoveVoid(handle,index);
}



void ea32PushUIntsFromString_dbg(EArray32Handle *handle, char *pString MEM_DBG_PARMS)
{
	U32 iCurVal = 0;
	bool bFoundADigit = false;

	while (1)
	{
		if (!(*pString))
		{
			if (bFoundADigit)
			{
				ea32Push_dbg(handle, iCurVal MEM_DBG_PARMS_CALL);
			}

			return;
		}

		if (isdigit(*pString))
		{
			iCurVal *= 10;
			iCurVal += (*pString) - '0';
			bFoundADigit = true;
		}
		else
		{
			if (bFoundADigit)
			{
				ea32Push_dbg(handle, iCurVal MEM_DBG_PARMS_CALL);
			}

			bFoundADigit = false;
			iCurVal = 0;
		}

		pString++;
	}
}

//calls structDestroy on each element of the earray with the given TPI, then clears the earray
void eaClearStructVoid(cEArrayHandle* handle, ParseTable *pTPI)
{
	EArray* pArray = EArrayFromHandle(*handle);
	int i;

	if (!(*handle)) return;

	for (i = pArray->count-1; i >= 0; --i)
	{
		if (pArray->structptrs[i])
		{
			StructDestroyVoid(pTPI, pArray->structptrs[i]);
			pArray->structptrs[i] = NULL;
		}

	}

	pArray->count = 0;
}

//calls StructDestroy on each element of the earray with the given TPI, then destroys the earray
void eaDestroyStructVoid(cEArrayHandle* handle, ParseTable *pTPI)
{
	eaClearStructVoid(handle, pTPI);

	eaDestroy(handle);
}

void eaSetSizeStruct_dbg(EArrayHandle* handle, ParseTable *pTPI, int size MEM_DBG_PARMS)
{
	assertmsg(!eaIndexedGetTable_inline(handle), "eaSetSizeStruct not supported for indexed arrays");
	size = max(0, size);
	while (eaSize(handle) > size)
		StructDestroyVoid(pTPI, eaPop(handle));
	while (eaSize(handle) < size)
		eaPush_dbg(handle, StructCreate_dbg(pTPI, NULL MEM_DBG_PARMS_CALL) MEM_DBG_PARMS_CALL);
}

void *eaGetStruct_dbg(EArrayHandle* handle, ParseTable *pTPI, int index MEM_DBG_PARMS)
{
	devassert(index >= 0);
	index = max(0, index);
	while (eaSize(handle) <= index)
		eaPush_dbg(handle, StructCreate_dbg(pTPI, NULL MEM_DBG_PARMS_CALL) MEM_DBG_PARMS_CALL);
	return (*handle)[index];
}

//calls estrDestroy on each element of the earray
void eaClearEString(cEArrayHandle* handle)
{
	EArray* pArray = EArrayFromHandle(*handle);
	int i;

	if (!(*handle)) return;

	for (i = pArray->count-1; i >= 0; --i)
	{
		if (pArray->structptrs[i])
		{
			estrDestroy((char**) &(pArray->structptrs[i]));
		}

	}

	pArray->count = 0;
}

//calls estrDestroy on each element of the earray, then destroys the earray
void eaDestroyEString(EArrayHandle* handle)
{
	eaClearEString(handle);

	eaDestroy(handle);
}


void eaSetSizeEString_dbg(EArrayHandle* handle, int size MEM_DBG_PARMS)
{
	size = max(0, size);
	while (eaSize(handle) > size)
	{
		char* estr = (char*)eaPop(handle);
		estrDestroy(&estr);
	}
	while (eaSize(handle) < size)
	{
		char* estr = NULL;
		estrCreate_dbg(&estr MEM_DBG_PARMS_CALL);
		eaPush_dbg(handle, estr MEM_DBG_PARMS_CALL);
	}
}



void ea64Create_dbg(cEArray64Handle* handle MEM_DBG_PARMS)
{
	EArray64* pArray;
	assertmsg(!(*handle), "Tried to create an earray that is either already created or has a corrupt pointer.");
	pArray = pEAMalloc(sizeof(EArray64));
	pArray->count = 0;
	pArray->size = 1;
	*handle = HandleFromEArray64(pArray);
}

void ea64Destroy(EArray64Handle* handle)
{
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle))
		return;
	pEAFree(pArray);
	*handle = NULL;
}

void ea64SetSize_dbg(EArray64Handle* handle, int size MEM_DBG_PARMS)
{
	// this is a little confusing because Size = count within the pArray structure, Capacity = size
	EArray64* pArray=NULL;
	if (*handle)
		pArray = EArray64FromHandle(*handle);

	// don't bother creating the array or "raising" its capacity if the new size is 0
	if (size)
	{
		// set the new capacity, creating the array if necessary
		if (!(*handle) || ABS(pArray->size) < size) 
		{
			ea64SetCapacity_dbg(handle, size MEM_DBG_PARMS_CALL);
			pArray = EArray64FromHandle(*handle);
		}
		if (pArray->count < size) // increase size
		{
			memset(&pArray->values[pArray->count], 0, sizeof(pArray->values[0])*(size-pArray->count));
		}
	}

	if (pArray) // this would be NULL only if there was no array and the new size is also 0
		pArray->count = size;
}

void ea64SetCapacity_dbg(EArray64Handle* handle, int capacity MEM_DBG_PARMS)
{
	EArray64* pArray=NULL;
	int size=capacity>1? capacity: 1;

	if (*handle)
	{
		pArray = EArray64FromHandle(*handle);
		assert(pArray->size >= 0); // no stack arrays allowed
		pArray = pEARealloc(pArray, EARRAY64_HEADER_SIZE + sizeof(U64)*size);
		if (pArray->count > capacity) pArray->count = capacity;
	}
	else
	{
		pArray = pEAMalloc(EARRAY64_HEADER_SIZE + sizeof(U64)*size);
		pArray->count = 0;
	}
	
	pArray->size = size;
	*handle = HandleFromEArray64(pArray);
}

int ea64Capacity(ccEArray64Handle* handle)
{
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle))
		return 0;
	return ABS(pArray->size);
}

size_t ea64MemUsage(ccEArray64Handle* handle, bool bAbsoluteUsage)
{
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle))
		return 0;
	if (bAbsoluteUsage)
		return ABS(pArray->size)*sizeof(pArray->values[0]) + EARRAY64_HEADER_SIZE;
	else
		return pArray->count*sizeof(pArray->values[0]) + EARRAY64_HEADER_SIZE;
}

void ea64Compress_dbg(EArray64Handle *dst, ccEArray64Handle *src, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS)
{
	const EArray64 *src_array;
	EArray64 *dst_array;
	size_t len;

	if (!(*src)) {
		*dst=NULL;
		return;
	}
	if (memAllocator)
	{
		len = ea64MemUsage(src, false);
		src_array = EArray64FromHandle(*src);
		dst_array = memAllocator(customData, len);
		memcpy(dst_array, src_array, len);
		dst_array->size = dst_array->count;
		*dst = HandleFromEArray64(dst_array);
	}
	else
	{
		ea64Create_dbg(dst MEM_DBG_PARMS_CALL);
		ea64Copy_dbg(dst, src MEM_DBG_PARMS_CALL);
	}
}

int	ea64Push_dbg(EArray64Handle* handle, U64 value MEM_DBG_PARMS)
{
	EArray64* pArray=NULL;
	int size = 0;

	if (*handle)
	{
		pArray = EArray64FromHandle(*handle);
		size = pArray->size;
	}

	if (!(*handle) || pArray->count == ABS(pArray->size))
		ea64SetCapacity_dbg(handle, max(4,ABS(size)*2) MEM_DBG_PARMS_CALL);
	pArray = EArray64FromHandle(*handle);
	pArray->values[pArray->count++] = value;
	return pArray->count-1;
}

int	ea64PushUnique_dbg(EArray64Handle* handle, U64 value MEM_DBG_PARMS)
{
	int idx = ea64Find(handle, value);

	if (idx < 0)
		idx = ea64Push_dbg(handle, value MEM_DBG_PARMS_CALL);

	return idx;
}

int	ea64PushArray_dbg(EArray64Handle* handle, ccEArray64Handle* src MEM_DBG_PARMS)
{
	int count=0;

	if (*handle)
	{
		count = ea64Size( handle );
	}

	if (src)
	{
		int srcCount = ea64Size(src);
		ea64SetSize_dbg(handle, count + srcCount MEM_DBG_PARMS_CALL);
		CopyStructs(*handle + count, *src, srcCount);
	}

	return count;
}

U64	ea64Pop(EArray64Handle* handle)
{
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle) || !pArray->count)
		return 0;
	pArray->count--;
	return pArray->values[pArray->count];
}

U64 ea64Tail(EArray64Handle* handle)
{
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle) || !pArray->count)
		return 0;
	return pArray->values[pArray->count-1];
}

U32 ea32Tail(EArray32Handle* handle)
{
	EArray32* pArray = EArray32FromHandle(*handle);
	if (!(*handle) || !pArray->count)
		return 0;
	return pArray->values[pArray->count-1];
}

void ea64Reverse(EArray64Handle* handle)
{
	EArray64* pArray = EArray64FromHandle(*handle);
	U64 *head, *tail;
	if (!(*handle))
		return;

	head = pArray->values;
	tail = head + pArray->count - 1;
	
	// Start the index at the two "outer most" elements of the pArray.
	// Each interation through the loop, swap the two elements that are indexed,
	// then bring the indices one step closer to each other.
	for(; head < tail; head++, tail--)
	{
		U64 temp = *head;
		*head = *tail;
		*tail = temp;
	}
}

void ea64Clear(EArray64Handle* handle)
{
	if (*handle)
	{
		EArray64* pArray = EArray64FromHandle(*handle);
		pArray->count = 0;
	}
}

void ea64Copy_dbg(EArray64Handle* dest, ccEArray64Handle* src MEM_DBG_PARMS)
{
	int size;
	EArray64 *d, *s;

	if (!*dest)
		ea64Create_dbg(dest MEM_DBG_PARMS_CALL);
	size = ea64Size(src);
	ea64SetSize_dbg(dest, size MEM_DBG_PARMS_CALL);
	d = EArray64FromHandle(*dest);
	s = EArray64FromHandle(*src);
	memcpy(d->values, s->values, sizeof(d->values[0])*size);
}

void ea64Set_dbg(EArray64Handle* handle, U64 value, int i MEM_DBG_PARMS)
{
	EArray64* pArray=NULL;
	if (i < 0)
		return;

	if (*handle)
	{
		pArray = EArray64FromHandle(*handle);
	}

	if (!(*handle) || ABS(pArray->size) <= i)
	{
		ea64SetCapacity_dbg(handle, pow2(i+1) MEM_DBG_PARMS_CALL);
		pArray = EArray64FromHandle(*handle);
	}
	if (pArray->count <= i)
	{
		ZeroStructsForce(pArray->values + pArray->count, i + 1 - pArray->count);
		pArray->count = i + 1;
	}
	pArray->values[i] = value;
}

U64	ea64Get(ccEArray64Handle* handle, int i)
{
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle) || pArray->count <= i || i < 0)
		return 0;
	return pArray->values[i];
}

void ea64Insert_dbg(EArray64Handle* handle, U64 value, int i MEM_DBG_PARMS)
{
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle) || pArray->count < i || i < 0)
		return;
	if (pArray->count == ABS(pArray->size))
		ea64SetCapacity_dbg(handle, ABS(pArray->size)*2 MEM_DBG_PARMS_CALL);
	pArray = EArray64FromHandle(*handle);
	CopyStructsFromOffset(pArray->values + i + 1, -1, pArray->count - i);
	pArray->count++;
	pArray->values[i] = value;
}

U64	ea64Remove(EArray64Handle* handle, int i)
{
	U64 value;
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle) || pArray->count <= i || i < 0)
		return 0;
	value = pArray->values[i];
	CopyStructsFromOffset(pArray->values + i, 1, pArray->count - i - 1);
	pArray->count--;
	return value;
}

U64	ea64RemoveFast(EArray64Handle* handle, int i)
{
	U64 value;
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle) || pArray->count <= i || i < 0)
		return 0;
	value = pArray->values[i];
	pArray->values[i] = pArray->values[--pArray->count];
	return value;
}

int	ea64Find(ccEArray64Handle* handle, U64 value)
{
	int i;
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle))
		return -1;
	for (i = 0; i < pArray->count; i++)
	{
		if (pArray->values[i] == value)
			return i;
	}
	return -1;
}

int	ea64FindAndRemove(EArray64Handle* handle, U64 value)
{
	int	idx;
	if ((idx = ea64Find(handle,value)) >= 0)
		ea64Remove(handle,idx);
	return idx;
}

void ea64Swap(EArray64Handle* handle, int i, int j)
{
	U64 temp;
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle) || pArray->count <= i || i < 0 || pArray->count <= j || j < 0)
		return;
	temp = pArray->values[i];
	pArray->values[i] = pArray->values[j];
	pArray->values[j] = temp;
}

void ea64Move(EArray64Handle* handle, int dest, int src)
{
	U64 temp;
	EArray64* pArray = EArray64FromHandle(*handle);
	if (!(*handle) || pArray->count <= src || src < 0 || pArray->count <= dest || dest < 0)
		return;
	temp = pArray->values[src];
	if(src < dest)
		CopyStructsFromOffset(pArray->values + src, 1, dest - src);
	else
		CopyStructsFromOffset(pArray->values + dest + 1, -1, src - dest);
	pArray->values[dest] = temp;
}

EArray64Handle ea64Temp(U64 value)
{
	static U64* ei = 0;
	if (!ei)
		ea64Create(&ei);
	ea64SetSize(&ei, 1);
	ei[0] = value;
	return ei;
}

void ea64PushUIntsFromString_dbg(EArray64Handle *handle, char *pString MEM_DBG_PARMS)
{
	U64 iCurVal = 0;
	bool bFoundADigit = false;

	while (1)
	{
		if (!(*pString))
		{
			if (bFoundADigit)
			{
				ea64Push_dbg(handle, iCurVal MEM_DBG_PARMS_CALL);
			}

			return;
		}

		if (isdigit(*pString))
		{
			iCurVal *= 10;
			iCurVal += (*pString) - '0';
			bFoundADigit = true;
		}
		else
		{
			if (bFoundADigit)
			{
				ea64Push_dbg(handle, iCurVal MEM_DBG_PARMS_CALL);
			}

			bFoundADigit = false;
			iCurVal = 0;
		}

		pString++;
	}
}

int eaFindString(CONST_STRING_EARRAY * eaArray, const char* stringToFind)
{
	if(eaArray && stringToFind)
	{
		int i, n = eaSize(eaArray);

		for(i=0; i<n; i++)
		{
			if(0 == stricmp((*eaArray)[i], stringToFind))
				return i;
		}
	}
	return -1;
}

void eaRemoveDuplicateStrings(char *** eaArray) //for an array of MALLOC'd strings, removes 2nd+ copies of any (case insensitive)
	//duplicate strings
{
	int i;
	int j;

	for (i=0; i < eaSize(eaArray) - 1; i++)
	{
		j = i+1;

		while (j < eaSize(eaArray))
		{
			if (stricmp((*eaArray)[i], (*eaArray)[j]) == 0)
			{
				free((*eaArray)[j]);
				eaRemove(eaArray, j);
			}
			else
			{
				j++;
			}
		}
	}
}


void eaRemoveDuplicateEStrings(char *** eaArray) //for an array of MALLOC'd strings, removes 2nd+ copies of any (case insensitive)
	//duplicate strings
{
	int i;
	int j;

	for (i=0; i < eaSize(eaArray) - 1; i++)
	{
		j = i+1;

		while (j < eaSize(eaArray))
		{
			if (stricmp((*eaArray)[i], (*eaArray)[j]) == 0)
			{
				estrDestroy(&(*eaArray)[j]);
				eaRemove(eaArray, j);
			}
			else
			{
				j++;
			}
		}
	}
}


void eaDiffAddrEx_dbg(cEArrayHandle *left, EAAddrFunc left_func, cEArrayHandle *right, EAAddrFunc right_func, cEArrayHandle *result MEM_DBG_PARMS)
{
	int i;
	static StashTable eaDiffAddrTable = 0;

	if(!eaDiffAddrTable)
	{
		eaDiffAddrTable = stashTableCreateAddressEx(20 MEM_DBG_PARMS_CALL);
	}

	stashTableClear(eaDiffAddrTable);

	for(i=0; i<eaSize(right); i++)
	{
		const void *obj = (*right)[i];
		const void *addr = right_func ? right_func(obj) : obj;
		stashAddressAddInt(eaDiffAddrTable, addr, 1, 1);
	}

	for(i=0; i<eaSize(left); i++)
	{
		const void *obj = (*left)[i];
		const void *addr = left_func ? left_func(obj) : obj;
		if(!stashAddressFindInt(eaDiffAddrTable, addr, NULL))
		{
			eaPush_dbg(result, obj MEM_DBG_PARMS_CALL);
		}
	}
}

void eaIntersectAddrEx_dbg(cEArrayHandle *left, EAAddrFunc left_func, cEArrayHandle *right, EAAddrFunc right_func, cEArrayHandle *result MEM_DBG_PARMS)
{
	int i;
	static StashTable eaIntersectAddrTable = 0;

	if(!eaIntersectAddrTable)
	{
		eaIntersectAddrTable = stashTableCreateAddressEx(20 MEM_DBG_PARMS_CALL);
	}

	stashTableClear(eaIntersectAddrTable);

	for(i=0; i<eaSize(right); i++)
	{
		const void *obj = (*right)[i];
		const void *addr = right_func ? right_func(obj) : obj;
		stashAddressAddInt(eaIntersectAddrTable, addr, 1, 1);
	}

	for(i=0; i<eaSize(left); i++)
	{
		const void *obj = (*left)[i];
		const void *addr = left_func ? left_func(obj) : obj;
		if(stashAddressFindInt(eaIntersectAddrTable, addr, NULL))
		{
			eaPush_dbg(result, obj MEM_DBG_PARMS_CALL);
		}
	}
}

int eafSortComparator(const float *f1, const float *f2)
{
	if (*f1 < *f2)
	{
		return -1;
	}
	else if (*f1 > *f2)
	{
		return 1;
	}

	return 0;
}

int eai64SortComparator(const U64 *int1, const U64 *int2)
{
	if (*int1 < *int2)
	{
		return -1;
	}
	else if (*int1 > *int2)
	{
		return 1;
	}

	return 0;
}


void ea32ToZipString_dbg(EArray32Handle *handle, SA_PARAM_OP_VALID char **ppOutEString, bool bTrustworthy MEM_DBG_PARMS)
{
	int iZipSize;
	void *pZippedBuffer;

	if (!bTrustworthy && ea32Size(handle) > MAX_UNTRUSTWORTHY_ARRAY_SIZE)
	{
		Errorf("ea32ToZipString trying to be applied to an array so big that it will be viewed as untrustworthy when decompressed");
	}

	if (ea32Size(handle) == 0)
	{
		estrPrintf_dbg(ppOutEString MEM_DBG_PARMS_CALL, "0");
		return;
	}

	assert(ppOutEString);

#if PLATFORM_CONSOLE
	{
		int i;

		for (i=0; i < ea32Size(handle); i++)
		{
			xbEndianSwapU32((*handle)[i]);
		}
	}
#endif


	pZippedBuffer = zipDataEx_dbg(*handle, ea32Size(handle) * 4, &iZipSize, 9, false, 0 MEM_DBG_PARMS_CALL);

#if PLATFORM_CONSOLE
	{
		int i;

		for (i=0; i < ea32Size(handle); i++)
		{
			xbEndianSwapU32((*handle)[i]);
		}
	}
#endif

	estrPrintf_dbg(ppOutEString MEM_DBG_PARMS_CALL, "%d ", ea32Size(handle));

	estrBase64Encode(ppOutEString, pZippedBuffer, iZipSize);

	free(pZippedBuffer);
}

bool ea32FromZipString_dbg(EArray32Handle *handle, char *pInString, bool bTrustworthy MEM_DBG_PARMS)
{
	int iCount;
	char *pSpace;

	if (!pInString || !pInString[0])
	{
		return false;
	}

	iCount = atoi(pInString);
	
	if (iCount < 0 || (!bTrustworthy && iCount > MAX_UNTRUSTWORTHY_ARRAY_SIZE))
	{
		return false;
	}

	pSpace = strchr(pInString, ' ');

	if (iCount == 0 || !pSpace)
	{
		if (pInString[0] == '0' && pInString[1] == 0)
		{
			ea32SetSize(handle, 0);
			return true;
		}

		return false;
	}
	else
	{
		int iOutSize = iCount * 4;
		
		int iLen = (int)strlen(pSpace + 1);

		void *pZippedBuffer = pEAMalloc(iLen * 2 + 1);

		int iZipSize = decodeBase64String(pSpace + 1, iLen, pZippedBuffer, iLen * 2 + 1);

		ea32SetSize_dbg(handle, iCount MEM_DBG_PARMS_CALL);

		unzipData((char*)(*handle), &iOutSize, pZippedBuffer, iZipSize);

		free(pZippedBuffer);

#if PLATFORM_CONSOLE
	{
		int i;

		for (i=0; i < iCount; i++)
		{
			xbEndianSwapU32((*handle)[i]);
		}
	}
#endif

		return (iOutSize == iCount * 4);
	}
}



void ea32Randomize(int **ppArray)
{
	int iSize = ea32Size(ppArray);
	int i;
	int iOther;
	
	U32 iTemp;


	if (iSize < 2)
	{
		return;
	}

	for (i=0; i < iSize; i++)
	{
		iOther = randInt(iSize);
		if (iOther != i)
		{
			iTemp = (*ppArray)[i];
			(*ppArray)[i] = (*ppArray)[iOther];
			(*ppArray)[iOther] = iTemp;
		}
	}
}


bool ea32SortedFindIntOrPlace(EArray32Handle *handle, U32 iVal, int *pOutPlace)
{
	EArray32* pArray;
	int iFirst, iLast, iMid;

	if (!*handle)
	{
		*pOutPlace = 0;
		return false;
	}


	pArray = EArray32FromHandle(*handle);


	if (pArray->count == 0)
	{
		*pOutPlace = 0;
		return false;
	}

	if (iVal < pArray->values[0])
	{
		*pOutPlace = 0;
		return false;
	}

	if (iVal > pArray->values[pArray->count - 1])
	{
		*pOutPlace = pArray->count;
		return false;
	}

	if (pArray->count == 2)
	{
		if (iVal == pArray->values[0])
		{
			*pOutPlace = 0;
			return true;
		}
		if (iVal == pArray->values[1])
		{
			*pOutPlace = 1;
			return true;
		}
		*pOutPlace = 1;
		return false;
	}

	iFirst = 0;
	iLast = pArray->count - 1;

	while (1)
	{
		iMid = (iFirst + iLast)/2;

		if (pArray->values[iMid] == iVal)
		{
			*pOutPlace = iMid;
			return true;
		}

		if (iVal > pArray->values[iMid])
		{
			if (iMid + 1 == iLast)
			{
				*pOutPlace = iLast;
				
				if (iVal == pArray->values[iLast])
				{
					return true;
				}

				return false;
			}
			
			iFirst = iMid;
		}
		else
		{
			if (iFirst + 1 == iMid)
			{
				if (iVal == pArray->values[iFirst])
				{
					*pOutPlace = iFirst;
					return true;
				}

				*pOutPlace = iMid;
				return false;
			}

			iLast = iMid;
		}
	}
}

	
AUTO_STRUCT;
typedef struct EarrayToStringWrapperStruct_Dummy
{
	int x;
} EarrayToStringWrapperStruct_Dummy;

AUTO_STRUCT;
typedef struct EarrayToStringWrapperStruct
{
	EarrayToStringWrapperStruct_Dummy **ppWrapperElements;
} EarrayToStringWrapperStruct;



void eaStructArrayToString_dbg(EArrayHandle *handle, ParseTable *pTPI, char **ppOutString  MEM_DBG_PARMS)
{
	EarrayToStringWrapperStruct tempStruct = {0};
	assert(stricmp(parse_EarrayToStringWrapperStruct[2].name, "WrapperElements") == 0);
	parse_EarrayToStringWrapperStruct[2].subtable = pTPI;
	parse_EarrayToStringWrapperStruct[2].param = ParserGetTableSize(pTPI);

	estrClear(ppOutString);
	tempStruct.ppWrapperElements = (EarrayToStringWrapperStruct_Dummy**)*handle;
	ParserWriteText_dbg(ppOutString, parse_EarrayToStringWrapperStruct, &tempStruct, 0, 0, 0 MEM_DBG_PARMS_CALL);  
}

void eaStructArrayFromString_dbg(EArrayHandle *handle, ParseTable *pTPI, char *pInString MEM_DBG_PARMS)
{
	EarrayToStringWrapperStruct tempStruct = {0};
	assert(stricmp(parse_EarrayToStringWrapperStruct[2].name, "WrapperElements") == 0);
	parse_EarrayToStringWrapperStruct[2].subtable = pTPI;
	parse_EarrayToStringWrapperStruct[2].param = ParserGetTableSize(pTPI);

	ParserReadText(pInString, parse_EarrayToStringWrapperStruct, &tempStruct, 0);
	eaCopy_dbg(handle, &tempStruct.ppWrapperElements MEM_DBG_PARMS_CALL);
	eaDestroy(&tempStruct.ppWrapperElements);
}

void StringArrayJoin(const char** array, const char* join, char** result)
{
	int size = eaSize(&array);
	int i;
	if (size)
	{
		estrAppend2(result, array[0]);
		for (i = 1; i < size; i++)
		{
			estrAppend2(result, join);
			estrAppend2(result, array[i]);
		}
	}
}

void eaRemoveSequentialDuplicates(void ***peaArray)
{
	int it;
	for( it = eaSize(peaArray) - 2; it >= 0; --it ) {
		if( (*peaArray)[ it ] == (*peaArray)[ it + 1 ]) {
			eaRemove( peaArray, it + 1 );
		}
	}
}

void eaRemoveDuplicates(EArrayHandle* handle, DuplicationCompareCB pCB, ParseTable *pTPI)
{
	int *pIndicesToRemove = NULL;
	int i, j;

	for (i=0; i < eaSize(handle) - 1; i++)
	{
		for (j = i + 1; j < eaSize(handle); j++)
		{
			if (pCB(eaGet(handle, i), eaGet(handle, j)))
			{
				ea32Push(&pIndicesToRemove, i);
				break;
			}
		}
	}

	for (i = ea32Size(&pIndicesToRemove) - 1; i >= 0; i--)
	{
		int iIndexToRemove = pIndicesToRemove[i];
		if (pTPI)
		{
			StructDestroyVoid(pTPI, eaGet(handle, iIndexToRemove));
		}
		else
		{
			free(eaGet(handle, iIndexToRemove));
		}

		eaRemove(handle, iIndexToRemove);
	}
}
		
void eaOverrideAndRemoveDuplicates(EArrayHandle* handle, DuplicationCompareCB pCB, ParseTable *pTPI)
{
	int *pIndicesToRemove = NULL;
	int i, j;

	for (i=0; i < eaSize(handle) - 1; i++)
	{
		if (ea32Find(&pIndicesToRemove, i) != -1)
		{
			continue;
		}

		for (j = i + 1; j < eaSize(handle); j++)
		{
			if (pCB(eaGet(handle, i), eaGet(handle, j)))
			{
				StructOverride(pTPI, eaGet(handle, i), eaGet(handle, j), 0, false, false);
				ea32Push(&pIndicesToRemove, j);
			}
		}
	}

	ea32QSort(pIndicesToRemove, cmpU32);

	for (i = ea32Size(&pIndicesToRemove) - 1; i >= 0; i--)
	{
		int iIndexToRemove = pIndicesToRemove[i];
		if (pTPI)
		{
			StructDestroyVoid(pTPI, eaGet(handle, iIndexToRemove));
		}
		else
		{
			free(eaGet(handle, iIndexToRemove));
		}

		eaRemove(handle, iIndexToRemove);
	}

	ea32Destroy(&pIndicesToRemove);
}

#include "earray_c_ast.c"
