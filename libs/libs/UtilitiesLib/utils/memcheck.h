// This file is included into _every_ translation unit that is compiled.  Even if you think your change is superawesome and everyone should
// use it, please do not put it here.  If your change isn't big enough for its own header file, consider using StringUtil.h, mathutil.h,
// utils.h, osdependent.h, or some similar header file.  If you absolutely must make every file include your change, put it into stdtypes.h instead
// of here, unless it's specifically memory-related.

#ifndef GCC_SYSTEM
#if _PS3
//YVS
#define GCC_SYSTEM _Pragma("GCC system_header")
GCC_SYSTEM
#else
#define GCC_SYSTEM 
#endif
#endif

#ifdef __cplusplus
	#define C_DECLARATIONS_BEGIN	extern "C"{
	#define C_DECLARATIONS_END	}
#else
	#define C_DECLARATIONS_BEGIN
	#define C_DECLARATIONS_END
#endif

#define __ALIGN(x) __declspec(align(x))
#define __STATIC_ASSERT(x) extern int (*__static_assert(void))[(x) ? 1 : -1];
#define __EMPTY
#define FORM_LL "I64"

#define __BIT(i) (((U64)1)<<(i))

#define IS_ALIGNED(x,a) (!((x)&((a)-1)))
#define ALIGNDN(x,a) ( ((x))&~((a)-1) )


#ifdef NO_MEMCHECK_H
// For Novodex C++ files
// Don't do any redefining

#else

#ifndef _MEMCHECK_H
#define _MEMCHECK_H
#pragma once

#define MEMCHECK_INCLUDED

#define memAlloc malloc
#define memFree free
#define	memRealloc	realloc

// For rand_s() in <stdlib.h>
#define _CRT_RAND_S

#include <stdlib.h>
#undef _malloc_dbg
#undef _calloc_dbg
#undef _realloc_dbg
#undef _free_dbg
#include <crtdbg.h>
#include <stdio.h>
#include <malloc.h>

#if _CRTDBG_MAP_ALLOC

// These are here to prevent people from using them as function pointers
#ifdef __cplusplus
#undef malloc
#undef calloc
#undef realloc
#undef free
#define malloc(size)		_malloc_dbg(size,_NORMAL_BLOCK,__FILE__,__LINE__)
#define calloc(num,size)	_calloc_dbg(num,size,_NORMAL_BLOCK,__FILE__,__LINE__)
#define realloc(data,size)	_realloc_dbg(data,size,_NORMAL_BLOCK,__FILE__,__LINE__)
#define free(data)			_free_dbg(data,_NORMAL_BLOCK)
#else
#undef malloc
#undef calloc
#undef realloc
#undef free
#define malloc mallocIsNotAFunction
#define calloc callocIsNotAFunction
#define realloc reallocIsNotAFunction
#define free freeIsNotAFunction
#define mallocIsNotAFunction(size)		_malloc_dbg(size,_NORMAL_BLOCK,__FILE__,__LINE__)
#define callocIsNotAFunction(num,size)	_calloc_dbg(num,size,_NORMAL_BLOCK,__FILE__,__LINE__)
#define reallocIsNotAFunction(data,size)	_realloc_dbg(data,size,_NORMAL_BLOCK,__FILE__,__LINE__)
#define freeIsNotAFunction(data)			_free_dbg(data,_NORMAL_BLOCK)
#endif

#define aligned_free free

#ifdef _FULLDEBUG
#define FD_MEM_DBG_PARMS , const char *caller_fname, int line
#define FD_MEM_DBG_PARMS_CALL , caller_fname, line
#define FD_MEM_DBG_PARMS_INIT , __FILE__, __LINE__
#else
#define FD_MEM_DBG_PARMS
#define FD_MEM_DBG_PARMS_CALL
#define FD_MEM_DBG_PARMS_INIT
#endif

#include "memcheck_minimal_defines.h"

#define smalloc(x) _malloc_dbg((x),_NORMAL_BLOCK,caller_fname,line)
#define scalloc(x,y) _calloc_dbg(x,y,_NORMAL_BLOCK,caller_fname,line)
#define srealloc(x,y) _realloc_dbg(x,y,_NORMAL_BLOCK,caller_fname,line)
#define stmalloc(x,struct_ptr) _malloc_dbg((x),_NORMAL_BLOCK,(struct_ptr)->caller_fname,(struct_ptr)->line)
#define stcalloc(x,y,struct_ptr) _calloc_dbg(x,y,_NORMAL_BLOCK,(struct_ptr)->caller_fname,(struct_ptr)->line)
#define strealloc(x,y,struct_ptr) _realloc_dbg(x,y,_NORMAL_BLOCK,(struct_ptr)->caller_fname,(struct_ptr)->line)
#include <string.h>
#undef strdup
#undef _strdup
#undef strndup
#define strdup(a) strdup_dbg(a, __FILE__, __LINE__) // So that memory allocations are tracked
#define _strdup(a) strdup_dbg(a, __FILE__, __LINE__) // So that memory allocations are tracked
#define strndup(a,n) strndup_dbg(a, n, __FILE__, __LINE__)

#define strdup_special(a, heap) strdup_special_dbg(a, heap, __FILE__, __LINE__)
#define strndup_special(a, n, heap) strndup_special_dbg(a, n, heap, __FILE__, __LINE__)

#define _malloc_dbg		malloc_timed
#define _calloc_dbg		calloc_timed
#define _realloc_dbg	realloc_timed
#define _free_dbg		free_timed

#else
#define MEM_DBG_PARMS 
#define MEM_DBG_PARMS_VOID void
#define MEM_DBG_PARMS_CALL 
#define MEM_DBG_PARMS_CALL_VOID 
#define MEM_DBG_PARMS_INIT 
#define MEM_DBG_PARMS_INIT_VOID 
#define MEM_DBG_STRUCT_PARMS 
#define MEM_DBG_STRUCT_PARMS_INIT(struct_ptr) 
#define MEM_DBG_STRUCT_PARMS_CALL(struct_ptr) 
#define MEM_DBG_STRUCT_PARMS_CALL_VOID(struct_ptr) 
#define smalloc(x) malloc(x)
#define scalloc(x,y) calloc(x,y)
#define srealloc(x,y) realloc(x,y)
#define stmalloc(x,struct_ptr) malloc(x)
#define stcalloc(x,y,struct_ptr) calloc(x,y)
#define strealloc(x,y,struct_ptr) realloc(x,y)
#endif


C_DECLARATIONS_BEGIN

void *aligned_malloc_dbg(size_t size, int align_bytes, const char *filename, int linenumber);
void *aligned_calloc_dbg(size_t size, int align_bytes, const char *filename, int linenumber);

C_DECLARATIONS_END

#define aligned_malloc(size,alignment) aligned_malloc_dbg(size,alignment,__FILE__,__LINE__)
#define aligned_calloc(size,count,alignment) aligned_calloc_dbg((size)*(count),alignment,__FILE__,__LINE__)


#include "stdtypes.h"

C_DECLARATIONS_BEGIN

SA_RET_OP_STR char *strdup_dbg(SA_PARAM_OP_STR const char *s MEM_DBG_PARMS);
SA_RET_OP_STR char *strndup_dbg(SA_PARAM_OP_STR const char *s, size_t n MEM_DBG_PARMS);
SA_RET_OP_STR char *strdup_special_dbg(SA_PARAM_OP_STR const char *s, int specialHeap MEM_DBG_PARMS);
SA_RET_OP_STR char *strndup_special_dbg(SA_PARAM_OP_STR const char *s, size_t n, int specialHeap MEM_DBG_PARMS);
void memCheckInit(void);

// Dumps to file and console.
void memCheckDumpAllocsFile(const char *filename);

// Dumps to file only.
void memCheckDumpAllocsFileOnly(const char *filename);

void memCheckDumpAllocs(void);

//this returns true on success, false otehrwise
SA_RET_VALID int heapValidateAllReturn(void);

//asserts on failure. Use this if you're inserting stuff around and trying to 
//track down errors
void assertHeapValidateAll(void);


SA_RET_VALID int heapValidateAllPeriodic(int period);

int memTrackValidateHeap(void);


//including this here as memcheck.h is universally included, and I couldn't figure out a better way
//to get all the .c and .cpp files to compile
#include "CRTSafetyWrapper.h"



C_DECLARATIONS_END

#endif // _MEMCHECK_H

#endif // NO_MEMCHECK_H
