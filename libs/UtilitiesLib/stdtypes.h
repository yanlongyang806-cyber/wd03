#pragma once
GCC_SYSTEM

// This file is included into _every_ translation unit that is compiled.  Even if you think your change is superawesome and everyone should
// use it, please do not put it here.  If your change isn't big enough for its own header file, consider using StringUtil.h, mathutil.h,
// utils.h, osdependent.h, or some similar header file.  Note that memory-related stuff should go into memcheck.h instead of here; memcheck.h
// includes this.

#ifndef UNICODE
STATIC_ASSERT("Cryptic apps must have unicode enabled");
#endif

#ifdef __cplusplus
	#define C_DECLARATIONS_BEGIN	extern "C"{
	#define C_DECLARATIONS_END	}
#else
	#define C_DECLARATIONS_BEGIN
	#define C_DECLARATIONS_END
#endif

#if _PS3 || _XBOX
#define PLATFORM_CONSOLE 1
#endif

typedef struct PerfInfoStaticData	PerfInfoStaticData;
	
#define PERFINFO_TYPE PerfInfoStaticData

#include <stddef.h>
#include <stdarg.h>
#include <string.h>

#if PLATFORM_CONSOLE
#define DATACACHE_LINE_SIZE_BYTES	128
#else
#define DATACACHE_LINE_SIZE_BYTES	32
#endif

//headers to make our secure CRT stuff work somewhat transparently
#include <ctype.h>
#include <io.h>
#include <direct.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <process.h>

// Prevent conflicts with standard library functions that may be defined as macros
// Undefine these before including math.h to avoid conflicts with our own implementations
#ifdef round
#undef round
#endif
#ifdef log2
#undef log2
#endif


//#if !_XBOX
#ifdef _PREFAST_
#include <CodeAnalysis/sourceannotations.h>
#endif
//#endif

//#include <sal.h>

//#if !_XBOX
//#ifndef __Printf_format_string
//#define __Printf_format_string				[SA_FormatString(Style="printf")]
//#endif
//#else
//#define __Printf_format_string
//#endif

#if _XBOX
    #include <ppcintrinsics.h>

    #define __cntlzw(v) _CountLeadingZeros(v)
    #define __cntlzd(v) _CountLeadingZeros64(v)
#else

#if __cplusplus
	#ifndef assert
		#define assert(x) (void)0
	#endif
#endif

    #include <intrin.h>

    #pragma intrinsic(_BitScanReverse)

    __forceinline static int __cntlzw(__int32 v) {
        unsigned long i;
        if(_BitScanReverse(&i, v))
            return 32-(i+1);
        return 32;
    }

    #ifdef _M_X64
        #pragma intrinsic(_BitScanReverse64)
        __forceinline static int __cntlzd(__int64 v) {
            unsigned long i;
            if(_BitScanReverse64(&i, v))
                return 64-(i+1);
            return 64;
        }
    #else
        __forceinline static int __cntlzd(__int64 v) {
            if(v>>32)
                return __cntlzw((__int32)(v>>32));
            return __cntlzw((__int32)(v)) + 32;
        }
    #endif
#endif

// Static Analysis Macros
// Notation Key:
// PRE_ - incoming param value conforms to specified attributes
// POST_ - outgoing param value conforms to specified attributes
// PARAM_ - incoming and outgoing param value conforms to specified attributes
// RET_	- function return value conforms to attributes, and return value must be checked
// ORET_ - function return value conforms to attributes, and return value may or may not be checked

// NN - Non-Null Pointer To
// OP - Optional Pointer To (might be null, will be valid if non-null)
// P - Pointer To (make no statement about whether the pointer is valid or non-null)

// VALID - Valid data
// GOOD - Non-Null and Valid data
// FREE - Invalid data
// NULL - Null data
// STR - Null terminated string

#ifdef _PREFAST_

#define SA_PRE_GOOD					[SA_Pre(Null=SA_No, Valid=SA_Yes)]
#define SA_PRE_VALID				[SA_Pre(Valid=SA_Yes)] // maybe null

#define SA_PRE_NN_STR				[SA_Pre(Null=SA_No, Valid=SA_Yes, NullTerminated=SA_Yes)] [SA_Pre(Deref=1, Valid=SA_Yes)]
#define SA_PRE_OP_STR				[SA_Pre(Valid=SA_Yes, NullTerminated=SA_Yes)] [SA_Pre(Deref=1, Valid=SA_Yes)] // maybe null

#define SA_PRE_OP_VALID				[SA_Pre(Valid=SA_Yes)] [SA_Pre(Deref=1, Valid=SA_Yes)]
#define SA_PRE_OP_FREE				[SA_Pre(Valid=SA_Yes)] [SA_Pre(Deref=1, Valid=SA_No)]
#define SA_PRE_OP_NULL				[SA_Pre(Valid=SA_Yes)] [SA_Pre(Deref=1, Null=SA_Yes)]
#define SA_PRE_NN_VALID				[SA_Pre(Null=SA_No, Valid=SA_Yes)] [SA_Pre(Deref=1, Valid=SA_Yes)]
#define SA_PRE_NN_FREE				[SA_Pre(Null=SA_No, Valid=SA_Yes)] [SA_Pre(Deref=1, Valid=SA_No)]
#define SA_PRE_NN_NULL				[SA_Pre(Null=SA_No, Valid=SA_Yes)] [SA_Pre(Deref=1, Null=SA_Yes)]

#define SA_PRE_NN_NN_VALID			[SA_Pre(Null=SA_No, Valid=SA_Yes)] [SA_Pre(Deref=1, Null=SA_No, Valid=SA_Yes)] [SA_Pre(Deref=2, Valid=SA_Yes)]
#define SA_PRE_NN_OP_VALID			[SA_Pre(Null=SA_No, Valid=SA_Yes)] [SA_Pre(Deref=1, Valid=SA_Yes)] [SA_Pre(Deref=2, Valid=SA_Yes)]
#define SA_PRE_OP_OP_VALID			[SA_Pre(Valid=SA_Yes)] [SA_Pre(Deref=1, Valid=SA_Yes)] [SA_Pre(Deref=2, Valid=SA_Yes)]
#define SA_PRE_OP_OP_STR			[SA_Pre(Valid=SA_Yes)] [SA_Pre(Deref=1, Valid=SA_Yes, NullTerminated=SA_Yes)] [SA_Pre(Deref=2, Valid=SA_Yes)]

#define SA_PRE_NN_NN_STR			[SA_Pre(Null=SA_No, Valid=SA_Yes)] [SA_Pre(Deref=1, Null=SA_No, Valid=SA_Yes, NullTerminated=SA_Yes)] [SA_Pre(Deref=2, Valid=SA_Yes)]
#define SA_PRE_NN_OP_STR			[SA_Pre(Null=SA_No, Valid=SA_Yes)] [SA_Pre(Deref=1, Valid=SA_Yes, NullTerminated=SA_Yes)] [SA_Pre(Deref=2, Valid=SA_Yes)]
#define SA_PRE_NN_NN_NN_STR			[SA_Pre(Null=SA_No, Valid=SA_Yes)] [SA_Pre(Deref=1, Null=SA_No, Valid=SA_Yes)] [SA_Pre(Deref=2, Null=SA_No, Valid=SA_Yes, NullTerminated=SA_Yes)] [SA_Pre(Deref=3, Valid=SA_Yes)]

#define SA_PRE_NN_RELEMS(x)			[SA_Pre(Null=SA_No, Valid=SA_Yes, ValidElementsConst=x)]
#define SA_PRE_NN_RELEMS_VAR(x)		[SA_Pre(Null=SA_No, Valid=SA_Yes, ValidElements=#x)]

#define SA_PRE_NN_ELEMS(x)			[SA_Pre(Null=SA_No, Valid=SA_Yes, ValidElementsConst=x, WritableElementsConst=x)]
#define SA_PRE_NN_ELEMS_VAR(x)		[SA_Pre(Null=SA_No, Valid=SA_Yes, ValidElements=#x, WritableElements=#x)]

#define SA_PRE_NN_RBYTES(x)			[SA_Pre(Null=SA_No, Valid=SA_Yes, ValidBytesConst=x)]
#define SA_PRE_NN_RBYTES_VAR(x)		[SA_Pre(Null=SA_No, Valid=SA_Yes, ValidBytes=#x)]

#define SA_PRE_NN_BYTES(x)			[SA_Pre(Null=SA_No, Valid=SA_Yes, ValidBytesConst=x, WritableBytesConst=x)]
#define SA_PRE_NN_BYTES_VAR(x)		[SA_Pre(Null=SA_No, Valid=SA_Yes, ValidBytes=#x, WritableBytes=#x)]

#define SA_PRE_OP_RELEMS(x)			[SA_Pre(Valid=SA_Yes, ValidElementsConst=x)]
#define SA_PRE_OP_RELEMS_VAR(x)		[SA_Pre(Valid=SA_Yes, ValidElements=#x)]

#define SA_PRE_OP_ELEMS(x)			[SA_Pre(Valid=SA_Yes, ValidElementsConst=x, WritableElementsConst=x)]
#define SA_PRE_OP_ELEMS_VAR(x)		[SA_Pre(Valid=SA_Yes, ValidElements=#x, WritableElements=#x)]

#define SA_PRE_OP_RBYTES(x)			[SA_Pre(Valid=SA_Yes, ValidBytesConst=x)]
#define SA_PRE_OP_RBYTES_VAR(x)		[SA_Pre(Valid=SA_Yes, ValidBytes=#x)]

#define SA_PRE_OP_BYTES(x)			[SA_Pre(Valid=SA_Yes, ValidBytesConst=x, WritableBytesConst=x)]
#define SA_PRE_OP_BYTES_VAR(x)		[SA_Pre(Valid=SA_Yes, ValidBytes=#x, WritableBytes=#x)]

#define SA_POST_VALID				[SA_Post(Valid=SA_Yes)] // maybe null
#define SA_POST_FREE				[SA_Post(Valid=SA_No)] // freed pointer
#define SA_POST_NULL				[SA_Post(Null=SA_Yes)] // null pointer

#define SA_POST_OP_STR				[SA_Post(Valid=SA_Yes, NullTerminated=SA_Yes)] // maybe null
#define SA_POST_NN_STR				[SA_Post(Null=SA_No, Valid=SA_Yes, NullTerminated=SA_Yes)]

#define SA_POST_OP_VALID			[SA_Post(Valid=SA_Yes)] [SA_Post(Deref=1, Valid=SA_Yes)]
#define SA_POST_OP_FREE				[SA_Post(Valid=SA_Yes)] [SA_Post(Deref=1, Valid=SA_No)]
#define SA_POST_OP_NULL				[SA_Post(Valid=SA_Yes)] [SA_Post(Deref=1, Null=SA_Yes)]
#define SA_POST_NN_VALID			[SA_Post(Null=SA_No, Valid=SA_Yes)] [SA_Post(Deref=1, Valid=SA_Yes)]
#define SA_POST_NN_FREE				[SA_Post(Null=SA_No, Valid=SA_Yes)] [SA_Post(Deref=1, Valid=SA_No)]
#define SA_POST_NN_NULL				[SA_Post(Null=SA_No, Valid=SA_Yes)] [SA_Post(Deref=1, Null=SA_Yes)]
#define SA_POST_P_FREE				[SA_Post(Deref=1, Valid=SA_No)]
#define SA_POST_P_NULL				[SA_Post(Deref=1, Null=SA_Yes)]

#define SA_POST_NN_NN_VALID			[SA_Post(Null=SA_No, Valid=SA_Yes)] [SA_Post(Deref=1, Null=SA_No, Valid=SA_Yes)] [SA_Post(Deref=2, Valid=SA_Yes)]
#define SA_POST_NN_OP_VALID			[SA_Post(Null=SA_No, Valid=SA_Yes)] [SA_Post(Deref=1, Valid=SA_Yes)] [SA_Post(Deref=2, Valid=SA_Yes)]
#define SA_POST_OP_OP_VALID			[SA_Post(Valid=SA_Yes)] [SA_Post(Deref=1, Valid=SA_Yes)] [SA_Post(Deref=2, Valid=SA_Yes)]
#define SA_POST_OP_OP_STR			[SA_Post(Valid=SA_Yes)] [SA_Post(Deref=1, Valid=SA_Yes, NullTerminated=SA_Yes)]

#define SA_POST_NN_NN_STR			[SA_Post(Null=SA_No, Valid=SA_Yes)] [SA_Post(Deref=1, Null=SA_No, Valid=SA_Yes, NullTerminated=SA_Yes)]
#define SA_POST_NN_OP_STR			[SA_Post(Null=SA_No, Valid=SA_Yes)] [SA_Post(Deref=1, Valid=SA_Yes, NullTerminated=SA_Yes)]
#define SA_POST_NN_NN_NN_STR		[SA_Post(Null=SA_No, Valid=SA_Yes)] [SA_Post(Deref=1, Null=SA_No, Valid=SA_Yes)] [SA_Post(Deref=2, Null=SA_No, Valid=SA_Yes, NullTerminated=SA_Yes)]

#define SA_POST_NN_RELEMS(x)		[SA_Post(Null=SA_No, Valid=SA_Yes, ValidElementsConst=x)]
#define SA_POST_NN_RELEMS_VAR(x)	[SA_Post(Null=SA_No, Valid=SA_Yes, ValidElements=#x)]

#define SA_POST_NN_ELEMS(x)			[SA_Post(Null=SA_No, Valid=SA_Yes, ValidElementsConst=x, WritableElementsConst=x)]
#define SA_POST_NN_ELEMS_VAR(x)		[SA_Post(Null=SA_No, Valid=SA_Yes, ValidElements=#x, WritableElements=#x)]

#define SA_POST_NN_RBYTES(x)		[SA_Post(Null=SA_No, Valid=SA_Yes, ValidBytesConst=x)]
#define SA_POST_NN_RBYTES_VAR(x)	[SA_Post(Null=SA_No, Valid=SA_Yes, ValidBytes=#x)]

#define SA_POST_NN_BYTES(x)			[SA_Post(Null=SA_No, Valid=SA_Yes, ValidBytesConst=x, WritableBytesConst=x)]
#define SA_POST_NN_BYTES_VAR(x)		[SA_Post(Null=SA_No, Valid=SA_Yes, ValidBytes=#x, WritableBytes=#x)]

#define SA_POST_OP_RELEMS(x)		[SA_Post(Valid=SA_Yes, ValidElementsConst=x)]
#define SA_POST_OP_RELEMS_VAR(x)	[SA_Post(Valid=SA_Yes, ValidElements=#x)]

#define SA_POST_OP_ELEMS(x)			[SA_Post(Valid=SA_Yes, ValidElementsConst=x, WritableElementsConst=x)]
#define SA_POST_OP_ELEMS_VAR(x)		[SA_Post(Valid=SA_Yes, ValidElements=#x, WritableElements=#x)]

#define	SA_POST_OP_RBYTES(x)		[SA_Post(Valid=SA_Yes, ValidBytesConst=x)]
#define	SA_POST_OP_RBYTES_VAR(x)	[SA_Post(Valid=SA_Yes, ValidBytes=#x)]

#define	SA_POST_OP_BYTES(x)			[SA_Post(Valid=SA_Yes, ValidBytesConst=x, WritableBytesConst=x)]
#define	SA_POST_OP_BYTES_VAR(x)		[SA_Post(Valid=SA_Yes, ValidBytes=#x, WritableBytes=#x)]

// SA_PARAM_* macros are convenience macros that combine identical pre and post conditions.
#define SA_PARAM_NN_STR				SA_PRE_NN_STR SA_POST_NN_STR
#define SA_PARAM_OP_STR				SA_PRE_OP_STR SA_POST_OP_STR

#define SA_PARAM_NN_VALID			SA_PRE_NN_VALID SA_POST_NN_VALID
#define SA_PARAM_OP_VALID			SA_PRE_OP_VALID SA_POST_OP_VALID

#define SA_PARAM_NN_NN_VALID		SA_PRE_NN_NN_VALID SA_POST_NN_NN_VALID
#define SA_PARAM_NN_OP_VALID		SA_PRE_NN_OP_VALID SA_POST_NN_OP_VALID
#define SA_PARAM_OP_OP_VALID		SA_PRE_OP_OP_VALID SA_POST_OP_OP_VALID
#define SA_PARAM_NN_NN_STR			SA_PRE_NN_NN_STR SA_POST_NN_NN_STR
#define SA_PARAM_NN_OP_STR			SA_PRE_NN_OP_STR SA_POST_NN_OP_STR
#define SA_PARAM_OP_OP_STR			SA_PRE_OP_OP_STR SA_POST_OP_OP_STR
#define SA_PARAM_NN_NN_NN_STR		SA_PRE_NN_NN_NN_STR SA_POST_NN_NN_NN_STR

// SA_ORET_* macros are for OPTIONAL return values - e.g. caller does not have to check them.
#define SA_ORET_VALID				[returnvalue:SA_Post(Valid=SA_Yes)] // maybe null

#define SA_RET_VALID				[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes)] // maybe null

#define SA_ORET_NN_STR				[returnvalue:SA_Post(Null=SA_No, Valid=SA_Yes, NullTerminated=SA_Yes)]
#define SA_ORET_OP_STR				[returnvalue:SA_Post(Valid=SA_Yes, NullTerminated=SA_Yes)] // maybe null
#define SA_ORET_OP_OP_STR			[returnvalue:SA_Post(Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes, NullTerminated=SA_Yes)]

#define SA_RET_NN_STR				[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes, NullTerminated=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes)]
#define SA_RET_OP_STR				[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes, NullTerminated=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes)] // maybe null
#define SA_RET_OP_OP_STR			[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes)][returnvalue:SA_Post(Deref=1, Valid=SA_Yes, NullTerminated=SA_Yes)]

#define SA_ORET_OP_VALID			[returnvalue:SA_Post(Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes)]
#define SA_ORET_NN_VALID			[returnvalue:SA_Post(Null=SA_No, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes)]
#define SA_ORET_NN_FREE				[returnvalue:SA_Post(Null=SA_No, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_No)]
#define SA_ORET_NN_NULL				[returnvalue:SA_Post(Null=SA_No, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Null=SA_Yes)]
#define SA_ORET_P_NULL				[returnvalue:SA_Post(Deref=1, Null=SA_Yes)]
#define SA_ORET_OP_OP_VALID			[returnvalue:SA_Post(Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=2, Valid=SA_Yes)]
#define SA_ORET_OP_OP_OP_VALID		[returnvalue:SA_Post(Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=2, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=3, Valid=SA_Yes)]

#define SA_ORET_NN_NN_VALID			[returnvalue:SA_Post(Null=SA_No, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Null=SA_No, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=2, Valid=SA_Yes)]

#define SA_RET_OP_VALID				[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes)]
#define SA_RET_NN_VALID				[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes)]
#define SA_RET_NN_FREE				[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_No)]
#define SA_RET_NN_NULL				[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Null=SA_Yes)]
#define SA_RET_OP_OP_VALID			[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=2, Valid=SA_Yes)]
#define SA_RET_OP_OP_OP_VALID		[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=2, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=3, Valid=SA_Yes)]

#define SA_RET_NN_NN_VALID			[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=1, Null=SA_No, Valid=SA_Yes)] [returnvalue:SA_Post(Deref=2, Valid=SA_Yes)]

#define SA_RET_NN_RELEMS(x)			[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes, ValidElementsConst=x)]
#define SA_RET_NN_RELEMS_VAR(x)		[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes, ValidElements=#x)]

#define SA_RET_NN_ELEMS(x)			[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes, ValidElementsConst=x, WritableElementsConst=x)]
#define SA_RET_NN_ELEMS_VAR(x)		[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes, ValidElements=#x, WritableElements=#x)]

#define SA_RET_NN_BYTES(x)			[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes, ValidBytesConst=x, WritableBytesConst=x)]
#define SA_RET_NN_BYTES_VAR(x)		[returnvalue:SA_Post(MustCheck=SA_Yes, Null=SA_No, Valid=SA_Yes, ValidBytes=#x, WritableBytes=#x)]

#define SA_RET_OP_RELEMS(x)			[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes, ValidElementsConst=x)]
#define SA_RET_OP_RELEMS_VAR(x)		[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes, ValidElements=#x)]

#define SA_RET_OP_ELEMS(x)			[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes, ValidElementsConst=x, WritableElementsConst=x)]
#define SA_RET_OP_ELEMS_VAR(x)		[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes, ValidElements=#x, WritableElements=#x)]

#define SA_RET_OP_BYTES(x)			[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes, ValidBytesConst=x, WritableBytesConst=x)]
#define SA_RET_OP_BYTES_VAR(x)		[returnvalue:SA_Post(MustCheck=SA_Yes, Valid=SA_Yes, ValidBytes=#x, WritableBytes=#x)]

#define FORMAT_STR					[SA_FormatString(Style="printf")]

// Note: *both* of these get hit during the preprocessor, once for /analyze, once for the compiler!
// Note: This is just for analysis, *not* the optimizer's __assume directive
#define ANALYSIS_ASSUME(expr)		(__analysis_assume(expr))

#else // ifdef _PREFAST_

#define SA_PRE_GOOD
#define SA_PRE_VALID

#define SA_PRE_NN_STR
#define SA_PRE_OP_STR

#define SA_PRE_OP_VALID
#define SA_PRE_OP_FREE
#define SA_PRE_OP_NULL
#define SA_PRE_NN_VALID
#define SA_PRE_NN_FREE
#define SA_PRE_NN_NULL

#define SA_PRE_NN_NN_VALID
#define SA_PRE_NN_OP_VALID
#define SA_PRE_OP_OP_VALID
#define SA_PRE_OP_OP_STR

#define SA_PRE_NN_NN_STR
#define SA_PRE_NN_OP_STR
#define SA_PRE_NN_NN_NN_STR

#define SA_PRE_NN_RELEMS(x)
#define SA_PRE_NN_RELEMS_VAR(x)

#define SA_PRE_NN_ELEMS(x)
#define SA_PRE_NN_ELEMS_VAR(x)

#define SA_PRE_NN_RBYTES(x)
#define SA_PRE_NN_RBYTES_VAR(x)

#define SA_PRE_NN_BYTES(x)
#define SA_PRE_NN_BYTES_VAR(x)

#define SA_PRE_OP_RELEMS(x)
#define SA_PRE_OP_RELEMS_VAR(x)

#define SA_PRE_OP_ELEMS(x)
#define SA_PRE_OP_ELEMS_VAR(x)

#define SA_PRE_OP_RBYTES(x)
#define SA_PRE_OP_RBYTES_VAR(x)

#define SA_PRE_OP_BYTES(x)
#define SA_PRE_OP_BYTES_VAR(x)

#define SA_POST_VALID
#define SA_POST_FREE
#define SA_POST_NULL

#define SA_POST_OP_STR
#define SA_POST_NN_STR

#define SA_POST_OP_VALID
#define SA_POST_OP_FREE
#define SA_POST_OP_NULL
#define SA_POST_NN_VALID
#define SA_POST_NN_FREE
#define SA_POST_NN_NULL
#define SA_POST_P_FREE
#define SA_POST_P_NULL

#define SA_POST_NN_NN_VALID
#define SA_POST_NN_OP_VALID
#define SA_POST_OP_OP_VALID
#define SA_POST_OP_OP_STR

#define SA_POST_NN_NN_STR
#define SA_POST_NN_OP_STR
#define SA_POST_NN_NN_NN_STR

#define SA_POST_NN_RELEMS(x)
#define SA_POST_NN_RELEMS_VAR(x)

#define SA_POST_NN_ELEMS(x)
#define SA_POST_NN_ELEMS_VAR(x)

#define SA_POST_NN_RBYTES(x)
#define SA_POST_NN_RBYTES_VAR(x)

#define SA_POST_NN_BYTES(x)
#define SA_POST_NN_BYTES_VAR(x)

#define SA_POST_OP_RELEMS(x)
#define SA_POST_OP_RELEMS_VAR(x)

#define SA_POST_OP_ELEMS(x)
#define SA_POST_OP_ELEMS_VAR(x)

#define	SA_POST_OP_RBYTES(x)
#define	SA_POST_OP_RBYTES_VAR(x)

#define	SA_POST_OP_BYTES(x)
#define	SA_POST_OP_BYTES_VAR(x)

#define SA_PARAM_NN_STR
#define SA_PARAM_OP_STR

#define SA_PARAM_NN_VALID
#define SA_PARAM_OP_VALID

#define SA_PARAM_NN_NN_VALID
#define SA_PARAM_NN_OP_VALID
#define SA_PARAM_OP_OP_VALID
#define SA_PARAM_NN_NN_STR
#define SA_PARAM_NN_OP_STR
#define SA_PARAM_OP_OP_STR
#define SA_PARAM_NN_NN_NN_STR

#define SA_ORET_VALID

#define SA_RET_VALID

#define SA_ORET_NN_STR
#define SA_ORET_OP_STR
#define SA_ORET_OP_OP_STR

#define SA_RET_NN_STR
#define SA_RET_OP_STR
#define SA_RET_OP_OP_STR

#define SA_ORET_OP_VALID
#define SA_ORET_NN_VALID
#define SA_ORET_NN_FREE
#define SA_ORET_NN_NULL
#define SA_ORET_P_NULL
#define SA_ORET_OP_OP_VALID
#define SA_ORET_OP_OP_OP_VALID

#define SA_ORET_NN_NN_VALID

#define SA_RET_OP_VALID
#define SA_RET_NN_VALID
#define SA_RET_NN_FREE
#define SA_RET_NN_NULL
#define SA_RET_OP_OP_VALID
#define SA_RET_OP_OP_OP_VALID

#define SA_RET_NN_NN_VALID

#define SA_RET_NN_RELEMS(x)
#define SA_RET_NN_RELEMS_VAR(x)

#define SA_RET_NN_ELEMS(x)
#define SA_RET_NN_ELEMS_VAR(x)

#define SA_RET_NN_BYTES(x)
#define SA_RET_NN_BYTES_VAR(x)

#define SA_RET_OP_RELEMS(x)
#define SA_RET_OP_RELEMS_VAR(x)

#define SA_RET_OP_ELEMS(x)
#define SA_RET_OP_ELEMS_VAR(x)

#define SA_RET_OP_BYTES(x)
#define SA_RET_OP_BYTES_VAR(x)

#define FORMAT_STR

#define ANALYSIS_ASSUME(expr)		(0)

#endif // ifdef _PREFAST_


C_DECLARATIONS_BEGIN

#ifndef _WIN32_WINNT
#ifdef CLIENT
#define _WIN32_WINNT 0x0400
#else
#define _WIN32_WINNT 0x0501
#endif
#endif

#pragma warning (disable:4244)		/* disable bogus conversion warnings */
#pragma warning (disable:4305)		/* disable bogus conversion warnings */
#if _MSC_VER >= 1600
#ifdef _M_X64
#pragma warning (disable:4306)		// void *p = (void*)1; started happening with VS2010
#endif
#endif

#pragma warning (disable:4100) // Unreferenced formal parameter
#pragma warning (disable:4057) // differs in indirection to slightly different base types
#pragma warning (disable:4115) // named type definition in parentheses
#pragma warning (disable:4127) // conditional expression is constant (while (1))
#pragma warning (disable:4214) // nonstandard extension used : bit field types other than int (chars and enums)
#pragma warning (disable:4201) // nonstandard extension used : nameless struct/union
#pragma warning (disable:4055) // 'type cast' : from data pointer X to function pointer Y
#pragma warning (disable:4152) // nonstandard extension, function/data pointer conversion in expression
#pragma warning (disable:4706) // assignment within conditional expression
#pragma warning (disable:702)  // unreachable code (EXCEPTION_HANDLER_END mostly)
#pragma warning (disable:4204) // non-constant aggregate initializer
#pragma warning (disable:4221) // X: cannot be initialized using address of automatic variable Y (used in textparser stuff)
#pragma warning (disable:4130) // logical operation on address of string constant (assert("foo"==0))
#pragma warning (disable:4189) // X: local variable is initialized but not referenced (too many to fix)
#pragma warning (disable:4512) // X : assignment operator could not be generated (cryptopp, novadex)
#pragma warning (disable:4210) // nonstandard extension used : function given file scope
#pragma warning (disable:4709) // comma operator within array index expression (eaSize in arrays)
#pragma warning (disable:4324) // 'x' : structure was padded due to __declspec(align())  (WHY IS THIS A WARNING???? compiler is doing what we told it to)

// Found a bug, many false positives
#pragma warning (disable:4245) // conversion from X to Y, signed/unsigned mismatch

/******************************************************************************************
 *
 *	/analyze warnings
 *
 *****************************************************************************************/
#pragma warning (disable:6269)		//Disable possible incorrect order  *p++ vs (*p)++ or *(p++)
#pragma warning (disable:6255)		//Use _alloca_s instead of _alloc to avoid exception
#pragma warning (disable:6263)		//_alloca used in a loop, might exceed stack size

// This thinks eaiSize is returning a pointer for some reason.
#pragma warning (disable:6384)		//Dividing sizeof a pointer by another value
#pragma warning (disable:6011)		//Dereferencing NULL pointer - firing on lines like if (x && x->f)

//////////////////////////////////////////////////////////////////////////
// Warnings to be re-enabled when someone gets around to it
// TODO: These should be re-enabled

#pragma warning (disable:6385)		//Readable size is 1*0
#pragma warning (disable:6054)		// String 'argument 1' might not be zero-terminated (parsed strings through string functions can't be validated)

//////////////////////////////////////////////////////////////////////////

// Warnings enabled in W4 but not in W3 that we probably want anyway
#pragma warning (3:4131)		// Disallow traditional function declarations

//////////////////////////////////////////////////////////////////////////

#if UTILITIESLIB
#pragma warning (disable:6031)		//Return value ignored  (_chmod, _chdir, etc...)
#pragma warning (disable:6262)		//Too much stack space used
#endif 

#ifndef UNUSED
#define UNUSED(xx) (xx==xx)
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef CRYPTIC_MAX_PATH
#define CRYPTIC_MAX_PATH MAX_PATH
#endif

typedef unsigned char U8;
typedef signed char S8;
typedef volatile signed char VS8;
typedef volatile unsigned char VU8;

typedef unsigned short U16;
typedef short S16;
typedef volatile unsigned short VU16;
typedef volatile short VS16;

typedef int S32;
typedef unsigned int U32;
typedef volatile int VS32;
typedef volatile unsigned int VU32;

typedef unsigned __int64 U64;
typedef __int64 S64;
typedef volatile __int64 VS64;
typedef volatile unsigned __int64 VU64;

typedef float F32;
typedef volatile float VF32;

typedef U16 F16;
typedef VU16 VF16;

typedef U32 EntityRef;
typedef U8 DirtyBit;

#if PLATFORM_CONSOLE
typedef U32 Vec3_Packed; // 11:11:10 format
#else
// PC does't support DEC3N or HEND3 :(
typedef U64 Vec3_Packed; // 16:16:16:16 format
#endif

typedef volatile double VF64;
typedef double F64;

typedef void * UserData;
typedef void * PrivateData;

#define S32_MIN    (-2147483647i32 - 1) /* minimum signed 32 bit value */
#define S32_MAX      2147483647i32 /* maximum signed 32 bit value */
#define U32_MAX     0xffffffffui32 /* maximum unsigned 32 bit value */
#define U16_MAX		0xffffui16 /* maximum unsigned 32 bit value */
#define U8_MAX		0xffui8 /* maximum unsigned 32 bit value */

#define MAX_NAME_LEN 128
#define MAX_DESCRIPTION_LEN 2048

#ifndef NULL
#define NULL ((void *)0)
#endif

#define CONST_OPTIONAL_STRUCT(type) type * const
#define OPTIONAL_STRUCT(type) type * 
#define CONST_STRING_MODIFIABLE const char * const
#define STRING_MODIFIABLE char *
#define CONST_STRING_POOLED const char * const
#define STRING_POOLED const char *

#define EARRAY_OF(type) type **
#define INT_EARRAY int *
#define UINT_EARRAY unsigned int *
#define CONTAINERID_EARRAY ContainerID *
#define FLOAT_EARRAY float *
#define CONST_EARRAY_OF(type) type * const * const
#define CONST_CONTAINERID_EARRAY const ContainerID * const
#define CONST_INT_EARRAY const int * const 
#define CONST_UINT_EARRAY const unsigned int * const
#define CONST_FLOAT_EARRAY const float * const
#define CONST_STRING_EARRAY const char * const * const
#define STRING_EARRAY char **

#define DECONST(type, val) (*((type *)&val))

#define NOCONST_EVAL(type) type##_AutoGen_NoConst
#define NOCONST(type) NOCONST_EVAL(type)

// Use these as a safer way to cast between const and non-const containers, e.g.
// Item *pItem = CONTAINER_RECONST(Item, functionReturningNoconstItem())
// NOCONST(Item) *pItem = CONTAINER_NOCONST(Item, functionReturningItem())
//
// But, this will fail to compile:
// Item *pItem = CONTAINER_RECONST(Item, functionReturningNoconstPower())
#define CONTAINER_RECONST(Type, val) (0 && ((NOCONST(Type) *)NULL == val) ? 0 : (Type *)(val))
#define CONTAINER_RECONST2(Type, val) (0 && ((NOCONST(Type) **)NULL == val) ? 0 : (Type **)(val))
#define CONTAINER_NOCONST(Type, val) (0 && ((Type *)NULL == val) ? 0 : (NOCONST(Type) *)(val))
#define CONTAINER_NOCONST2(Type, val) (0 && ((Type **)NULL == val) ? 0 : (NOCONST(Type) **)(val))
#define CONTAINER_NOCONST3(Type, val) (0 && ((Type ***)NULL == val) ? 0 : (NOCONST(Type) ***)(val))

// These are meant to be used on AST_FORCE_CONST structs. They are just wrappers for the CONTAINER macros.
#define STRUCT_RECONST(Type, val) CONTAINER_RECONST(Type, val)
#define STRUCT_RECONST2(Type, val) CONTAINER_RECONST2(Type, val)
#define STRUCT_NOCONST(Type, val) CONTAINER_NOCONST(Type, val)
#define STRUCT_NOCONST2(Type, val) CONTAINER_NOCONST2(Type, val)
#define STRUCT_NOCONST3(Type, val) CONTAINER_NOCONST3(Type, val)

// Use this instead of __declspec(thread)
// The above remark was originally unexplained, but I assume it's because TLS sections did not work in dynamically-loaded
// DLLs prior to Windows Vista.  It may be safe to use __declspec(thread) if you provide fallback code paths for pre-Vista.
#define STATIC_THREAD_ALLOC_TYPE(var,type) STATIC_THREAD_ALLOC_TYPE_SIZE(var,type,sizeof(*var))

// Like above, but for an array of characters
#define STATIC_THREAD_ALLOC_SIZE(var,size) STATIC_THREAD_ALLOC_TYPE_SIZE(var,char*,size)

// Like above, but for a generic void pointer
#define STATIC_THREAD_ALLOC(var) STATIC_THREAD_ALLOC_TYPE(var,void *)

typedef unsigned long DWORD;
typedef void *LPVOID;

#if !defined(_KERNEL32_)
#define WINBASEAPI DECLSPEC_IMPORT
#else
#define WINBASEAPI
#endif

// for the following, from sysutil.c:
int CrypticTlsAlloc(void);
void *CrypticTlsGetValue(int dwTlsIndex);
int CrypticTlsSetValue(int dwTlsIndex, void *lpTlsValue);
int CrypticGetLastError(void);

#define TLS_OUT_OF_INDEXES ((DWORD)0xFFFFFFFF)  // from winbase.h

// Actual TLS implementation for above
#define STATIC_THREAD_ALLOC_TYPE_SIZE(var,type,size)									\
	static int tls_##var##_index = 0;	/* Deliberately exposed to the outer scope */	\
	do {																				\
		ATOMIC_INIT_BEGIN;																\
		tls_##var##_index = CrypticTlsAlloc();											\
		ATOMIC_INIT_END;																\
		assert(tls_##var##_index != TLS_OUT_OF_INDEXES);								\
		var = (type)CrypticTlsGetValue(tls_##var##_index);								\
		if (!var)																		\
		{																				\
			int tls_success;															\
			assert(CrypticGetLastError() == 0);											\
			var = (type)calloc(size, 1);												\
			tls_success = CrypticTlsSetValue(tls_##var##_index, var);					\
			assert(tls_success);														\
		}																				\
	} while (0)

#define xcase									break;case
#define acase									case
#define xdefault								break;default

// Make a statement inside a #define 

#define STATEMENT(x)						do{x}while(0)

// Standard for loop.

#define FORCED_FOREACH_BEGIN_SEMICOLON		void adjsfljadslfjasdlfjjfalsjfasdjf(void)
#define FORCED_FOREACH_END_SEMICOLON		(0)

#define FOR_BEGIN_FROM(i, a, b)				{S32 i;for(i = (a); i < (b); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define FOR_BEGIN(i, a)						{S32 i;for(i = 0; i < (a); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define FOR_BEGIN_STEP(i, a, s)				{S32 i;for(i = 0; i < (a); i += (s)){FORCED_FOREACH_BEGIN_SEMICOLON
#define FOR_SIZE_BEGIN_FROM(i, a, s, f)		{S32 i;S32 s = a;for(i = f; i < (s); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define FOR_SIZE_BEGIN(i, a, s)				{S32 i;S32 s = a;for(i = 0; i < (s); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define FOR_REVERSE_BEGIN_STEP(i, a, s)		{S32 i;for(i = (a); i >= 0; i -= (s)){FORCED_FOREACH_BEGIN_SEMICOLON
#define FOR_REVERSE_BEGIN(i, a)				{S32 i;for(i = (a); i >= 0; i--){FORCED_FOREACH_BEGIN_SEMICOLON
#define FOR_REVERSE_BEGIN_TO(i, a, b)		{S32 i;for(i = (a); i >= b; i--){FORCED_FOREACH_BEGIN_SEMICOLON
#define FOR_END								}}FORCED_FOREACH_END_SEMICOLON

// Iterator for standard arrays.

#define ARRAY_FOREACH_BEGIN_FROM(a, i, f)	{S32 i;ASSERT_IS_ARRAY(a);for(i = (f); i < ARRAY_SIZE(a); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define ARRAY_FOREACH_BEGIN(a, i)			{S32 i;ASSERT_IS_ARRAY(a);for(i = 0; i < ARRAY_SIZE(a); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define ARRAY_FOREACH_BEGIN_STEP(a, i, s)	{S32 i;ASSERT_IS_ARRAY(a);for(i = 0; i < ARRAY_SIZE(a); i += (s)){FORCED_FOREACH_BEGIN_SEMICOLON
#define ARRAY_FOREACH_END					}}FORCED_FOREACH_END_SEMICOLON

// Iterators for EArrays.

#define EARRAY_FOREACH_BEGIN_FROM(a, i, f)				{S32 i;for(i = (f); i < eaSize(&a); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define EARRAY_FOREACH_BEGIN(a, i)						{S32 i;for(i = 0; i < eaSize(&a); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define EARRAY_FOREACH_REVERSE_BEGIN(a, i)				{S32 i;for(i = eaSize(&a) - 1; i >= 0; i--){FORCED_FOREACH_BEGIN_SEMICOLON
#define EARRAY_CONST_FOREACH_BEGIN_FROM(a, i, s, f)		{S32 i;S32 s = eaSize(&a);for(i = f; i < (s); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define EARRAY_CONST_FOREACH_BEGIN(a, i, s)				{S32 i;S32 s = eaSize(&a);for(i = 0; i < (s); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define EARRAY_CONST_FOREACH_REVERSE_BEGIN(a, i, s)		{S32 i;S32 s = eaSize(&a);for(i = s - 1; i >= 0; i--){FORCED_FOREACH_BEGIN_SEMICOLON
#define EARRAY_INT_CONST_FOREACH_BEGIN_FROM(a, i, s, f)	{S32 i;S32 s = eaiSize(&a);for(i = f; i < (s); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define EARRAY_INT_CONST_FOREACH_BEGIN(a, i, s)			{S32 i;S32 s = eaiSize(&a);for(i = 0; i < (s); i++){FORCED_FOREACH_BEGIN_SEMICOLON
#define EARRAY_INT_FOREACH_REVERSE_BEGIN(a, i)			{S32 i;for(i = eaiSize(&a) - 1; i >= 0; i--){FORCED_FOREACH_BEGIN_SEMICOLON
#define EARRAY_FOREACH_END								}}FORCED_FOREACH_END_SEMICOLON

#define IS_ARRAY(a)								((char*)(a) == (char*)&(a))
#define ASSERT_IS_ARRAY(a)						assertmsg(IS_ARRAY(a), #a" is not an array!")
#define ARRAY_SIZE(n)							(sizeof(n) / sizeof((n)[0]))
// ARRAY_SIZE_CHECKED causes a compile-time error if you pass it a char * instead of a char array
//  but has a false negative if it's a char[4] or char[8].
#define ARRAY_SIZE_CHECKED(n)					(sizeof(n) / ((sizeof(n)==0 || sizeof(n)==4 || sizeof(n)==8)?0:sizeof((n)[0])))
#define SAFESTR(str)							str, ARRAY_SIZE_CHECKED(str)
#define SAFESTR2(str)							str, str##_size
#define STRBUF_REMAIN(buffer, pos)				(ARRAY_SIZE_CHECKED(buffer) - ((pos) - (buffer)))
#define TYPE_ARRAY_SIZE(typename, n)			ARRAY_SIZE(((typename*)0)->n)
#define SIZEOF2(typename, n)					(sizeof(((typename*)0)->n))
#define SIZEOF2_DEREF(typename, n)				(sizeof(*((typename*)0)->n))
#define SIZEOF2_DEREF2(typename, n)				(sizeof(**((typename*)0)->n))
#define OFFSETOF(typename, n)					(uintptr_t)&(((typename*)(0x0))->n)
#define FREE_SIZE(obj,size)						free_timed_size((void*)(obj), _NORMAL_BLOCK, size)
#define FREE_SIZE_KEYED(obj,size,key)			(memFreeEnable((void*)(obj),(key)),free_timed_size((void*)(obj), _NORMAL_BLOCK, size))
#define SAFE_FREE(obj)							((obj)?(free((void*)(obj))),((obj) = NULL),1:0)
#define SAFE_FREE_KEYED(obj,key)				((obj)?memFreeEnable((void*)(obj),(key)),(free((void*)(obj))),((obj) = NULL),1:0)
#define SAFE_FREE_STRUCT(obj)					((obj)?(freeStruct(obj)),((obj) = NULL),1:0)
#define SAFE_FREE_STRUCT_KEYED(obj,key)			((obj)?memFreeEnable((void*)(obj),(key)),(freeStruct(obj)),((obj) = NULL),1:0)
#define SAFE_FREE_SIZE(obj, size)				((obj)?(FREE_SIZE((obj),(size))),((obj) = NULL),1:0)
#define SAFE_FREE_SIZE_KEYED(obj,size,key)		((obj)?(FREE_SIZE_KEYED((obj),(size),(key))),((obj) = NULL),1:0)
#ifdef __cplusplus
	#define SAFE_DELETE(obj)					((obj)?(delete (obj)),((obj) = NULL),1:0)
	#define SAFE_DELETE_ARRAY(obj)				((obj)?(delete [] (obj)),((obj) = NULL),1:0)
#endif

// Format string arguments marked with FORMAT_STRING_CHECKED() are required to be string literals.  This prevents
// accidentally passing regular strings as a format strings.  To pass format strings as pointers, wrap the argument
// in FORMAT_OK().
#define FORMAT_STRING_CHECKED(str)				("" str)
#define FORMAT_OK(str)							,str

#define SAFE_DEREF(p)							((p)?*(p):0)
#define SAFE_ASSIGN(p, v)						((p)?*(p) = (v):0)

#define SAFE_MEMBER(p, c1)						((p)?(p)->c1:0)
#define SAFE_MEMBER2(p, c1, c2)					(((p)&&(p)->c1)?(p)->c1->c2:0)
#define SAFE_MEMBER3(p, c1, c2, c3)				(((p)&&(p)->c1&&(p)->c1->c2)?(p)->c1->c2->c3:0)
#define SAFE_MEMBER4(p, c1, c2, c3, c4)			(((p)&&(p)->c1&&(p)->c1->c2&&(p)->c1->c2->c3)?(p)->c1->c2->c3->c4:0)

#define SAFE_GET_REF(p, c1)						((p)?GET_REF((p)->c1):0)
#define SAFE_GET_REF2(p, c1, c2)				(((p)&&(p)->c1)?GET_REF((p)->c1->c2):0)
#define SAFE_GET_REF3(p, c1, c2, c3)			(((p)&&(p)->c1&&(p)->c1->c2)?GET_REF((p)->c1->c2->c3):0)
#define SAFE_GET_REF4(p, c1, c2, c3, c4)		(((p)&&(p)->c1&&(p)->c1->c2&&(p)->c1->c2->c3)?GET_REF((p)->c1->c2->c3->c4):0)

#define OFFSET_PTR(p, bytes)					((void*)((U8*)(p) + (bytes)))

#define SAFE_MEMBER_ADDR(p, c1)					((p)?&(p)->c1:NULL)
#define SAFE_MEMBER_ADDR2(p, c1, c2)			(((p)&&(p)->c1)?&(p)->c1->c2:NULL)
#define SAFE_MEMBER_ADDR3(p, c1, c2, c3)		(((p)&&(p)->c1&&(p)->c1->c2)?&(p)->c1->c2->c3:NULL)
#define SAFE_MEMBER_ADDR4(p, c1, c2, c3, c4)	(((p)&&(p)->c1&&(p)->c1->c2&&(p)->c1->c2->c3)?&(p)->c1->c2->c3->c4:NULL)

#define ASSERT_TRUE_AND_RESET(x)				assert(x), (x) = 0
#define ASSERT_FALSE_AND_SET(x)					assert(!(x)), (x) = 1

#define ASSERT_TRUE_AND_RESET_BITS(x, b)		assert(((x) & (b)) == (b)), (x) &= ~(b)
#define ASSERT_FALSE_AND_SET_BITS(x, b)			assert(!((x) & (b))), (x) |= (b)

#define TRUE_THEN_RESET(x)						((x)?(((x)=0),1):0)
#define TRUE_THEN_RESET_BIT(x, b)				(((x)&(b))?(((x)&=~(b)),1):0)
#define TRUE_THEN_DECREMENT(x)					((x)?(((x)--),1):0)
#define FALSE_THEN_SET(x)						((x)?0:(((x)=1),1))
#define FALSE_THEN_SET_BIT(x, b)				(((x) & (b))?0:(((x)|=(b)),1))
#define FALSE_THEN_SET_BITS(x, b)				(((x) & (b))==(b)?0:(((x)|=(b)),1))

#define FIRST_IF_SET(a, b)						((a) ? (a) : (b))
#define NULL_TO_EMPTY(s)						FIRST_IF_SET(s, "")
#define EMPTY_TO_NULL(s)						(((s) && *(s)) ? (s) : NULL)

// XBox special memory functions
//#define NO_XMEM
#if defined(_XBOX) && !defined(NO_XMEM) && !defined(NO_MEMCHECK_H)
#	ifdef __cplusplus
		C_DECLARATIONS_END
#		include <string>
		C_DECLARATIONS_BEGIN
#	endif
	extern void *XMemCpy(void *dest, const void *src, unsigned long count);
	extern void *XMemSet(void *dest, int c, unsigned long count);

#	ifdef ASSERT_ON_BADPARAMS
		extern int isWriteCombined(void *ptr);
#		define ASSERT_IS_CACHED(ptr) (assertmsg(!isWriteCombined(ptr), "Write-combined pointer passed to function requiring a cacheable address"))
#		define ASSERT_IS_NOT_OVERLAPPED(dst, src, count) (assertmsg((((char*)(void*)(dst))+(count) < (char*)(void*)(src)) || (((char*)(void*)(src)) + (count) < (char*)(void*)(dst)), "Overlapping pointers passed to memcpy, use memmove instead"))
#	else
#		define ASSERT_IS_CACHED(ptr) 0
#		define ASSERT_IS_NOT_OVERLAPPED(dst, src, count) 0
#	endif

// Rules: Always use XMemSet, unless to write combined memory
	__forceinline static void *memset_writeCombined(void * __restrict dest, int c, int count) {
#		ifdef memset
#			undef memset
#		endif
		return memset(dest, c, count);
	}
#	define memset(dest, c, count) (ASSERT_IS_CACHED(dest),XMemSet((dest), (c), (count)))

// Rules: Use _intrinsic if size of data copied is known at compile time and < 100bytes
// Otherwise use _fast
#	define memcpy_fast(dest, src, count) (ASSERT_IS_NOT_OVERLAPPED((dest), (src), (count)),ASSERT_IS_CACHED((dest)),XMemCpy((dest), (src), (count)))
#	define memcpy_intrinsic(dest, src, count) memcpy(dest, src, count)
#	define memcpy_writeCombined(dest, src, count) XMemCpyStreaming_WriteCombined(dest, src, count)

#else

#	ifdef _XBOX
#		define memset_writeCombined(dest, c, count) memset_intrinsic(dest, c, count)
#	endif
// #define memset(dest, c, count) memset(dest, c, count)
#	define memcpy_fast(dest, src, count) memcpy(dest, src, count)
#	define memcpy_intrinsic(dest, src, count) memcpy(dest, src, count)
#	define memcpy_writeCombined(dest, src, count) memcpy_intrinsic(dest, src, count)

#endif

// Handy macros for atomic initialization of global data.

#define ATOMIC_INIT_DONE_BIT (1<<31)

S32 atomicInitIsFirst(volatile S32* init);
void atomicInitSetDone(S32* init);
		
#define ATOMIC_INIT_BEGIN	{																	\
		static S32 atomicInitS32;																\
		if(	!(atomicInitS32 & ATOMIC_INIT_DONE_BIT) &&											\
			atomicInitIsFirst(&atomicInitS32)){													\
			void xcsdlkfjalsdjfweiojf_ASDF_D_FSADF_AF_CV_X_VC_DS_ADS_FAD_F(void)
		
#define ATOMIC_INIT_END																			\
		atomicInitSetDone(&atomicInitS32);}}((void)0)

// Handy macros for zeroing memory.

#define ZeroStructForce(ptr)		memset((ptr), 0, sizeof(*(ptr)))
#define ZeroStruct(ptr)				memset((ptr), 0, (ptr)?sizeof(*(ptr)):0)
#define ZeroStructsForce(ptr,count)	memset((ptr), 0, sizeof(*(ptr)) * (count))
#define ZeroStructs(ptr,count)		memset((ptr), 0, (ptr)?sizeof(*(ptr)) * (count):0)
#define ZeroArray(x)				ASSERT_IS_ARRAY(x),memset((x), 0, sizeof(x))

#define CopyArray(dest, src)		ASSERT_IS_ARRAY(src),memmove((dest),(src),sizeof(src))

// Copy one struct.

#define CopyStruct(dest,src)						memmove((dest),(src),sizeof(*(dest)))
#define CopyStructFromOffset(dest,offset)			memmove((dest),(dest)+(offset),sizeof(*(dest)))
#define DupStruct(src)								((src)?memmove(calloc(1,sizeof(*(src))),(src),sizeof(*(src))):NULL)

// Copy "count" # of structs.

#define CopyStructs(dest,src,count)					memmove((dest),(src),sizeof((dest)[0]) * (count))
#define CopyStructsFromOffset(dest,offset,count)	memmove((dest),(dest)+(offset),sizeof((dest)[0]) * (count))
#define DupStructs(src,count)						((count && src)?memmove(calloc((count), sizeof((src)[0])),(src),sizeof((src)[0]) * (count)):NULL)

// Compare structs.

#define CompareStructs(a,b,count)	((void)(1/(S32)(sizeof(*(a))==sizeof(*(b)))),memcmp((a),(b),sizeof(*(a))*(count)))
#define CompareStruct(a,b)			CompareStructs((a),(b),1)

// structs heap operations with validation.

#define callocStruct(type)							((type *)calloc(1, sizeof(type)))
#define callocStructs(type, count)					((type *)calloc(count, sizeof(type)))
#define scallocStruct(type)							((type *)scalloc(1, sizeof(type)))

#define callocStructKeyed(ptr,type,keyInOut)		((void)(1/(S32)(sizeof(*(ptr))==sizeof(type))),((ptr) = (type*)calloc(1, sizeof(*(ptr)))),memFreeDisable((void*)(ptr),keyInOut),(ptr))
#define callocStructsKeyed(ptr,type,count,keyInOut)	((void)(1/(S32)(sizeof(*(ptr))==sizeof(type))),((ptr) = (type*)calloc(count, sizeof(*(ptr)))),memFreeDisable((void*)(ptr),keyInOut),(ptr))
#define freeStruct(ptr)								((void)(1/(S32)(sizeof(*(ptr))>sizeof(void*))),FREE_SIZE((ptr), sizeof(*(ptr))))

// Macro for creating a bitmask.  Example: BIT_RANGE(3, 10) = 0x7f8 (binary: 111 1111 1000).
//																	  bit 10-^  bit 3-^

#define BIT(bitIndex)		((U32)1 << (bitIndex))
#define BIT_RANGE(lo,hi)	(((((U32)1 << ((hi) - (lo))) - 1) << ((lo) + 1)) | BIT((lo)))

#define BIT64(bitIndex)		((U64)1 << (bitIndex))
#define BIT_RANGE64(lo,hi)	(((((U64)1 << ((hi) - (lo))) - 1) << ((lo) + 1)) | BIT64((lo)))

// va_start/end wrapper.

#define VA_START(va, format)	{va_list va, *__vaTemp__ = &va;va_start(va, format)
#define VA_END()				va_end(*__vaTemp__);}

// cast ints to and from pointers safely
#define U32_TO_PTR(x)	((void*)(uintptr_t)(U32)(x))
#define	S32_TO_PTR(x)	U32_TO_PTR(x)	// don't sign-extend
#define PTR_TO_U32(x)	((U32)(uintptr_t)(x))
#define PTR_TO_S32(x)	((S32)PTR_TO_U32(x))

#define PTR_TO_INT(x)	((intptr_t)(x))
#define PTR_TO_UINT(x)	((uintptr_t)(x))

#define UINT_TO_PTR(x) ((void*)(uintptr_t)(x))

// a bit more clunky..
#define PTR_ASSIGNFROM_F32(ptr,fl)	(*((F32*)(&ptr)) = (fl))
#define PTR_TO_F32(x)	*((F32*)(&(x)))
#define F32_TO_PTR(x)   *((void**)(&(x)))

typedef F64		DVec2[2];
typedef F64		DVec3[3];
typedef F64		DVec4[4];
typedef S32		IVec2[2];
typedef S32		IVec3[3];
typedef S32		IVec4[4];
typedef F32		Vec2[2];
typedef F32		Vec3[3];
typedef F32		Vec4[4];
typedef F32		__ALIGN(16) Vec4_aligned[4];
typedef Vec3 	Mat3[3];
typedef Vec3 	Mat4[4];
typedef Vec4 	Mat34[3];
typedef Vec4 	Mat44[4];
typedef DVec3 	DMat3[3];
typedef DVec3 	DMat4[4];
typedef DVec4 	DMat34[3];
typedef DVec4 	DMat44[4];
typedef IVec3 	IMat3[3];
typedef Vec3*	Mat3Ptr;
typedef Vec3*	Mat4Ptr;
typedef Vec3 const* Mat4ConstPtr;
typedef Vec4*	Mat44Ptr;
typedef F32		Quat[4];

// A Color object that can be addressed in several ways.
typedef union Color
{
	// Allows direct access to individual components.
	struct{
		U8 r;
		U8 g;
		U8 b;
		U8 a;
	};

	// Allows color component indexing like an array.
	U8 rgba[4];

	U8 rgb[3];

	// Not endian safe, only use for equality checking (use union assignment for fast assignment)
	U32 integer_for_equality_only; 
} Color;


// from structDefines.h
// structs to hold defines
typedef struct StaticDefine {
	const char* key;
	const char* value;
} StaticDefine;

typedef struct StaticDefineInt {
	const char* key;
	intptr_t value;
} StaticDefineInt;

// textparser.h
typedef U64 StructTypeField;
typedef U32 StructFormatField;

typedef struct ParseTable
{
	const char* name;
	StructTypeField type;
	size_t storeoffset;
	intptr_t param;			// default to ints, but pointers must fit here
	void* subtable;
	StructFormatField format;
	char *formatString_UseAccessor;
	//size_t namelen_NOT_REFERENCED_YET;
} ParseTable;



// A transform with the scale kept separate, for speed reasons
// No longer needed, but it might come back, so typedef it for now
#define SkinningMat4 Mat34

/*
typedef struct SkinningMat4
{
	Mat4 mat;
	Vec3 scale;
} SkinningMat4;
*/

// General macros
#define BITS_PER_BYTE		8
#define SECONDS_PER_MINUTE	60
#define MINUTES_PER_HOUR	60
#define SECONDS_PER_HOUR	(SECONDS_PER_MINUTE * MINUTES_PER_HOUR)
#define SECONDS_PER_DAY		(SECONDS_PER_HOUR * 24)
#define PI					3.1415926535897932384626433832795f
#define TWOPI				6.28318530717958647692
#define HALFPI				1.57079632679489661923
#define QUARTERPI			0.78539816339744830962
#define ONEOVERPI			0.31830988618379067154
#define ONEOVERTWOPI		0.15915494309189533577
#define SQRT2				1.41421356237309504880
#define SQRT3				1.73205080756887729353


#ifndef ABS
#define ABS(a)		(((a)<0) ? (-(a)) : (a))
#endif
#define ABS_UNS_DIFF(a, b)	((a > b)? (a - b) : (b - a))

#define SQR(a)		((a)*(a))
#define CUBE(a)		((a)*(a)*(a))
#define QUINT(a)	((a)*(a)*(a)*(a)*(a))
#ifndef RAD
#define RAD(a) 	(((a)*PI)*(1.0f/180.0f))		/* convert degrees to radians */
#endif
#ifndef DEG
#define DEG(a)	(((a)*180.0f)*(1.0f/PI))		/* convert radians to degrees */
#endif

#define CLAMP(var, min, max) \
	((var) < (min)? (min) : ((var) > (max)? (max) : (var)))

#define IN_CLOSED_INTERVAL_SAFE(a,x,b) ((a) < (b) ? ((x) >= (a) && (x) <= (b)) : ((x) >= (b) && (x) <= (a))) // a <= x <= b
#define IN_OPEN_INTERVAL_SAFE(a,x,b) ((a) < (b) ? ((x) > (a) && (x) < (b)) : ((x) > (b) && (x) < (a))) // a < x < b

#ifndef MIN
#define MIN(a,b)	(((a)<(b)) ? (a) : (b))
#define MAX(a,b)	(((a)>(b)) ? (a) : (b))
#endif
#define MIN1(a,b)	{if ((b)<(a)) (a) = (b);}
#define MAX1(a,b)	{if ((b)>(a)) (a) = (b);}
#define MINMAX1(a,b,c)	{if ((b)>(a)) (a) = (b);else if((c)<(a)) (a) = (c);}
#define SIGN(a)		(((a)>=0)  ?  1  : -1)
#define MINMAX(a,min,max) ((a) < (min) ? (min) : (a) > (max) ? (max) : (a))

// evaluates to true/false
#define ARE_FLAGS_SET(a,b)	(((a)&(b))==(b))

// evaluates to 0 or non-zero
#define CHECK_FLAG(a,b)	((a)&(b))


#if _XBOX
#define CLAMPF(x,a,b) __fself((x)-(a), __fself((b)-(x), (x), (b)), (a))
#define MINF(a,b)	(__fself((a)-(b), (b), (a)))
#define MAXF(a,b)	(__fself((b)-(a), (b), (a)))
#define MIN1F(a,b)	((a)=__fself((a)-(b), (b), (a)))
#define MAX1F(a,b)	((a)=__fself((b)-(a), (b), (a)))
#else
#define CLAMPF(var,min,max) ((var) < (float)(min)? (float)(min) : ((var) > (float)(max)? (float)(max) : (float)(var)))
#define MINF(a,b)	MIN(a,b)
#define MAXF(a,b)	MAX(a,b)
#define MIN1F(a,b)	MIN1(a,b)
#define MAX1F(a,b)	MAX1(a,b)
#endif

#define AVOID_DIV_0(x) ((x)?(x):1.f)

//JE: Changed this to a function, since where it was used, there was lots of dereferencing
//    going on in the parameters... sped it up by ~20% according to VTune
__forceinline static F32 CLAMPF32(F32 var, F32 min, F32 max) {
	return MAXF( min, MINF( var, max ) );
}

#define FINITE(x) ((x) >= -FLT_MAX && (x) <= FLT_MAX)

__forceinline static int log2(int val) {
    int notexact = (-((val-1) & val)) >> 31; // 0 if exact power of 2, -1 otherwise
    return 31-__cntlzw(val) - notexact;
}

 // Returns the number rounded up to a power of two
__forceinline static int pow2(int val) {
    return __BIT(log2(val));
}

#define SETB(mem,bitnum) ((mem)[(bitnum) >> 5] |= (1 << ((bitnum) & 31)))
#define CLRB(mem,bitnum) ((mem)[(bitnum) >> 5] &= ~(1 << ((bitnum) & 31)))
#define TOGB(mem,bitnum) ((mem)[(bitnum) >> 5] ^= (1 << ((bitnum) & 31)))
#define TSTB(mem,bitnum) (!!((mem)[(bitnum) >> 5] & (1 << ((bitnum) & 31))))

#define SCANF_CODE_FOR_INT(x) (sizeof(x) == 8 ? " %"FORM_LL"d " : " %d ")
#define SCANF_CODE_FOR_FLOAT(x) (sizeof(x) == 8 ? " %lf " : " %f ")
#define PRINTF_CODE_FOR_INT(x) FORMAT_OK(SCANF_CODE_FOR_INT(x))
#define PRINTF_CODE_FOR_FLOAT(x) FORMAT_OK(SCANF_CODE_FOR_FLOAT(x))
#define LOC_PRINTF_STR "(%.2f, %.2f, %.2f)"
#define ENT_PRINTF_STR "%s (ref: %d/%d)"

#define entPrintfParams(ent) entGetLocalName(ent), entGetRef(ent), entGetRefIndex(ent)

#ifndef __cplusplus
	#ifndef bool
		typedef S8 bool;
		#define true 1
		#define false 0
	#endif
#endif

int quick_sprintf(char *buffer, size_t buf_size, FORMAT_STR const char *format, ...);
int quick_snprintf(char *buffer, size_t buf_size, size_t maxlen, FORMAT_STR const char *format, ...);
int quick_vsprintf(char *buffer, size_t buf_size, FORMAT_STR const char *format, va_list argptr);
int quick_vsnprintf(char *buffer, size_t buf_size, size_t maxlen, FORMAT_STR const char *format, va_list argptr);

//a version of quick_sprintf that returns the buf rather than the num bytes, used for the STACK_SPRINTF macro
char *quick_sprintf_returnbuf(char *buffer, size_t buf_size, FORMAT_STR const char *format, ...);

//!!!DO NOT use this in a loop!!!
//a macro which sprintfs into a 1024 byte stack buffer and returns it. Useful if you have 
//MyFunc(char *pString) and wish you had MyFuncf(char *pString, ...). You can call
//MyFunc(STACK_SPRINTF(...))
void *checkForStackSprintfOverflow(void *pBuf, const char *pFileName, int iLineNum);
#define STACK_SPRINTF(format, ...) (quick_sprintf_returnbuf(checkForStackSprintfOverflow(_alloca(1024), __FILE__, __LINE__), 1024, FORMAT_STRING_CHECKED(format), __VA_ARGS__))


// Our own sprintf
#define sprintf_s(dst_dstlen, fmt, ...) quick_sprintf(dst_dstlen, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define snprintf_s(dst, len, fmt, ...) quick_sprintf(dst, len, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define sprintf(dst, fmt, ...) quick_sprintf(dst, ARRAY_SIZE_CHECKED(dst), FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define snprintf(buffer, maxlen, fmt, ...) quick_snprintf(buffer, ARRAY_SIZE_CHECKED(buffer), maxlen, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define vsnprintf(buffer, maxlen, fmt, ...) quick_vsnprintf(buffer, ARRAY_SIZE_CHECKED(buffer), maxlen, fmt, __VA_ARGS__)
#define vsprintf(buffer, format, argptr) quick_vsprintf(buffer, ARRAY_SIZE_CHECKED(buffer), format, argptr)
#define vsprintf_s(buffer, bufferSize, format, argptr) quick_vsprintf(buffer, bufferSize, format, argptr)
#define _vsnprintf(buffer, maxlen, format, argptr) quick_vsnprintf(buffer, ARRAY_SIZE_CHECKED(buffer), maxlen, format, argptr)

// Use library's printf
/*#define sprintf(dst, fmt, ...) sprintf_s(dst, ARRAY_SIZE_CHECKED(dst), fmt, __VA_ARGS__)
#define snprintf(buffer, maxlen, format, ...) snprintf_s(buffer, ARRAY_SIZE_CHECKED(buffer), maxlen, format, __VA_ARGS__)
#define vsnprintf(buffer, maxlen, format, ...) vsnprntf_s(buffer, ARRAY_SIZE_CHECKED(buffer), maxlen, format, __VA_ARGS__)
#define vsprintf(buffer, format, argptr) vsprintf_s(buffer, ARRAY_SIZE_CHECKED(buffer), format, argptr)*/


int printf_timed(FORMAT_STR const char *format, ...);
int _vscprintf_timed(FORMAT_STR const char *format, va_list argptr);
int sprintf_timed(char *buffer, size_t sizeOfBuffer, FORMAT_STR const char *format, ...);
int vsprintf_timed(char *buffer, size_t sizeOfBuffer, FORMAT_STR const char *format, va_list argptr);
int vprintf_timed(FORMAT_STR const char *format, va_list argptr);

#define printf(format, ...) printf_timed(FORMAT_STRING_CHECKED(format), __VA_ARGS__)
#define _vscprintf _vscprintf_timed
#define vprintf vprintf_timed

SA_RET_NN_BYTES_VAR(size) void* malloc_timed(size_t size, int blockType, const char *filename, int linenumber);
// SA_RET_NN_BYTES_VAR(size*num)
	void* calloc_timed(size_t num, size_t size, int blockType, const char *filename, int linenumber);
SA_RET_NN_BYTES_VAR(newSize) void* realloc_timed(void *p, size_t newSize, int blockType, const char *filename, int linenumber);
void free_timed(SA_PRE_OP_VALID SA_POST_OP_FREE void* p, int blockType);
void free_timed_size(SA_PRE_OP_VALID SA_POST_OP_FREE void* p, int blockType, size_t size);

void memFreeDisable(void* p, U32* keyInOut);
void memFreeEnable(void* p, U32 key);

typedef void (*OutOfMemoryCallback)(void);
OutOfMemoryCallback setOutOfMemoryCallback(OutOfMemoryCallback callback);

// physical memory functions
#if _XBOX
    void* physicalmalloc_timed(size_t size, int address, int alignment, int protect, SA_PRE_OP_STR const char *name, SA_PRE_NN_STR const char *filename, int linenumber);
    void physicalfree_timed(void *userData, SA_PRE_OP_STR const char *name);
    #define physicalmalloc(size, alignment, name) physicalmalloc_timed(size, MAXULONG_PTR, alignment, PAGE_READWRITE, name, __FILE__,__LINE__) 
    #define physicalfree(userData, name) physicalfree_timed(userData, name) // For some reason I need a space here...
    #define physicalptr(userData) (userData)
#else
    #define physicalmalloc(size, alignment, name) malloc(size)
    #define physicalfree(userData, name) free(userData)
    #define physicalptr(userData) (userData)
#endif
#define ALIGNUP(x,a) (((x)+((a)-1))&~((a)-1))
#define ALIGNUPNPT(x, a) ( (a)*( ((x)+(a)-1) / (a) ) )

#undef _malloc_dbg
#undef _calloc_dbg
#undef _realloc_dbg
#undef _free_dbg

#define _malloc_dbg		malloc_timed
#define _calloc_dbg		calloc_timed
#define _realloc_dbg	realloc_timed
#define _free_dbg		free_timed

// Optimized standard library and string functions
char *opt_strupr(char * string);
int opt_strnicmp(const char *first, const char *last, size_t count);
int opt_stricmp(const char * first, const char *second);

int utils_stricmp(const char * dst, const char * src);
int utils_strnicmp(const char *first, const char *last, size_t count);

#	define strupr(a) opt_strupr(a)
int __ascii_strnicmp(const char * dst,const char * src,size_t count);
#	define strnicmp(a, b, c) __ascii_strnicmp(a, b, c)
#	define stricmp(a, b) utils_stricmp(a, b)

#	define __ascii_toupper(c)      ( (((c) >= 'a') && ((c) <= 'z')) ? ((c) - 'a' + 'A') : (c) )
#	define toupper(c) __ascii_toupper(c)
#	define __ascii_tolower(c)      ( (((c) >= 'A') && ((c) <= 'Z')) ? ((c) - 'A' + 'a') : (c) )
#	define tolower(c) __ascii_tolower(c)

_CRTIMP extern const unsigned short *_pctype;
#ifdef isalnum
#undef isalnum
#endif
#ifdef isalpha
#undef isalpha
#endif
#ifdef isdigit
#undef isdigit
#endif
#ifdef isupper
#undef isupper
#endif
#ifdef islower
#undef islower
#endif
#define isalnum(c) (_pctype[c] & (_ALPHA|_DIGIT))
#define isalpha(c) (_pctype[c] & (_ALPHA))
#define isdigit(c) (_pctype[c] & (_DIGIT))
#define isupper(c) (_pctype[c] & (_UPPER))
#define islower(c) (_pctype[c] & (_LOWER))

#define isalnumorunderscore(c) (isalnum(c) || (c) == '_')

errno_t __cdecl fast_strcpy_s(char *_Dst, size_t _SizeInBytes, const char *_Src);
#define strcpy_s fast_strcpy_s

// Overrides of insecure string functions
#define strcpy_trunc(dst, src) strncpy_s(dst, ARRAY_SIZE_CHECKED(dst), src, _TRUNCATE)
#define strncpy_trunc(dst, src, count) strncpy_s(dst, ARRAY_SIZE_CHECKED(dst), src, ((count) < ARRAY_SIZE_CHECKED(dst) - 1 ? (count) : ARRAY_SIZE_CHECKED(dst) - 1))

#define strcpy(dst, src) strcpy_s(dst, ARRAY_SIZE_CHECKED(dst), src)
#define strncpy(dst, src, count) strncpy_s(dst, ARRAY_SIZE_CHECKED(dst), src, count)
#define strcat(dst, src) strcat_s(dst, ARRAY_SIZE_CHECKED(dst), src)
#define strncat(dst, src, count) strncat_s(dst, ARRAY_SIZE_CHECKED(dst), src, count)
#define strcat_trunc(dst, src) strncat_s(dst, ARRAY_SIZE_CHECKED(dst), src, _TRUNCATE)
#define strcat_s_trunc(dst, dst_size, src) strncat_s(dst, dst_size, src, _TRUNCATE)

#define sscanf(buf, fmt, ...) sscanf_s(buf, fmt, ##__VA_ARGS__)
#define swscanf(buf, fmt, ...) swscanf_s(buf, fmt, ##__VA_ARGS__)
#define gets(buf) gets_s(buf, ARRAY_SIZE_CHECKED(buf))
#define _strupr(str) _strupr_s(str, ARRAY_SIZE_CHECKED(str))

#define wcstombs(dst, src, count) wcstombs_s(NULL, dst, ARRAY_SIZE_CHECKED(dst), src, count)

#define _strdate(str) _strdate_s(str, ARRAY_SIZE_CHECKED(str))
#define _strtime(str) _strtime_s(str, ARRAY_SIZE_CHECKED(str))

#define itoa(value, str, radix) _itoa_s(value, str, ARRAY_SIZE_CHECKED(str), radix)
#define _itoa(value, str, radix) _itoa_s(value, str, ARRAY_SIZE_CHECKED(str), radix)
#define _ltoa(value, str, radix) _ltoa_s(value, str, ARRAY_SIZE_CHECKED(str), radix)

#define Strcpy(dst, src) strcpy_s(dst, ARRAY_SIZE_CHECKED(dst), src)
#define Strcat(dst, src) strcat_s(dst, ARRAY_SIZE_CHECKED(dst), src)

// Fills a string with a character repeated a certain number of times.
__forceinline static void strfill_dbg(char * string, int max_length, int length, int char_value)
{
	int i = length < max_length - 1 ? length : max_length - 1;
	memset(string, char_value, i);
	string[i] = '\0';
}

#define strfill(string, length, char_value)	strfill_dbg((string), ARRAY_SIZE_CHECKED((string)), (length), (char_value))

int open_cryptic(_In_z_ const char* filename, int oflag);
#	define open open_cryptic
#	define close _close
#	define strlwr _strlwr
#	define strcmpi _strcmpi
#	define getpid _getpid
#	define wcsicmp _wcsicmp

#	define unlink __unlink_UTF8
#	define chmod __chmod_UTF8
int __unlink_UTF8(_In_z_ const char *name);
int __chmod_UTF8(_In_z_ const char *name, int mode);

extern int getchFast(void);
extern int kbhitFast(void);
#define _getch getchFast
#define _kbhit kbhitFast
#define getch getchFast
#define kbhit kbhitFast


#define fopen include_file_h_for_fopen
#define system(a) include_utils_h_for_system

#define stat stat_is_broken_use_stat64
#define __stat stat_is_broken_use_stat64
#define stat32 stat32_is_broken_use_stat64
#define _stat32 stat32_is_broken_use_stat64
#define stat64i32 stat64i32_is_broken_use_stat64
#define _stat64i32 stat64i32_is_broken_use_stat64
#define _stat64(path,buff) _wstat64_UTF8(path,buff)

#if !defined(_CRTDBG_MAP_ALLOC) || !_CRTDBG_MAP_ALLOC
#pragma message("_DEBUG is defined but not _CRTDBG_MAP_ALLOC=1, memory allocations/frees will fail\r\n")
"You must define _CRTDBG_MAP_ALLOC=1 in project settings, as well as Force Include memcheck.h";
#endif

#if !defined(MEMCHECK_INCLUDED) && !defined(NO_MEMCHECK_OK)
#pragma message("Not force including memcheck.h.  Please setup your project to reference the Cryptic GeneralSettings.vsprops Property sheet\r\n")
"Not force including memcheck.h.  Please setup your project to reference the Cryptic GeneralSettings.vsprops Property sheet"
#endif

typedef int SlowRemoteCommandID;

#define EXPECT_SEMICOLON typedef int happyLongDummyMeaninglessNameThatMeansNothingLoopDeLoop

#ifndef __cplusplus

//#defines to make AUTO_COMMAND, AUTO_ENUM and AUTO_STRUCT work. They are special tokens looked for by StructParser

#define MAGIC_CMDPARSE_STRING_SIZE_ESTRING -1


#define AUTO_COMMAND EXPECT_SEMICOLON
#define AUTO_EXPR_FUNC(...) EXPECT_SEMICOLON
#define AUTO_EXPR_FUNC_STATIC_CHECK EXPECT_SEMICOLON
#define AUTO_COMMAND_REMOTE EXPECT_SEMICOLON
#define AUTO_COMMAND_REMOTE_SLOW(x)
#define AUTO_COMMAND_QUEUED(...)
#define ACMD_CALLBACK(x)
#define ACMD_OWNABLE(type) type**
#define ACMD_ACCESSLEVEL(x)
#define ACMD_APPSPECIFICACCESSLEVEL(globaltype,x)
#define ACMD_CLIENTCMD
#define ACMD_CLIENTCMDFAST
#define ACMD_SERVERCMD
#define ACMD_GENERICCLIENTCMD
#define ACMD_GENERICSERVERCMD
#define ACMD_IGNOREPARSEERRORS 
#define ACMD_EARLYCOMMANDLINE
#define ACMD_NAME(...)
#define ACMD_SENTENCE char*
#define ACMD_CATEGORY(...)
#define ACMD_CONTROLLER_AUTO_SETTING(x)
#define ACMD_AUTO_SETTING(x, ...)
#define ACMD_LIST(...)
#define ACMD_HIDE
#define ACMD_I_AM_THE_ERROR_FUNCTION_FOR(x)
#define ACMD_POINTER
#define ACMD_FORCETYPE(x)
#define ACMD_IFDEF(x)
#define ACMD_MAXVALUE(x)
#define ACMD_PRIVATE
#define ACMD_SERVERONLY
#define ACMD_NOTESTCLIENT
#define ACMD_INTERSHARD
#define ACMD_CLIENTONLY
#define ACMD_GLOBAL
#define ACMD_TESTCLIENT
#define ACMD_NAMELIST(...)
#define ACMD_IGNORE
#define ACMD_MULTIPLE_RECIPIENTS
#define ACMD_NONSTATICINTERNALCMD
#define ACMD_FORCEWRITECURFILE
#define ACMD_STATIC_RETURN 
#define ACMD_COMMANDLINE //these two things mean the same, to be both consisent with EARLYCOMMANDLINE and abbreviation-y
#define ACMD_CMDLINE
#define ACMD_CMDLINEORPUBLIC
#define ACMD_PACKETERRORCALLBACK
#define ACMD_PRODUCTS(...)
#define ACMD_CACHE_AUTOCOMPLETE
#define ACMD_DEFAULT(...)
#define ACMD_DEF(...)
#define ACMD_ALLOW_JSONRPC

#define NON_CONTAINER


// namelist types
#define STATICDEFINE
#define REFDICTIONARY
#define RESOURCEDICTIONARY
#define RESOURCEINFO
#define STASHTABLE
#define COMMANDLIST

#define AUTO_CMD_INT(varName, commandName) extern void AutoGen_RegisterAutoCmd_##commandName(void *pVarAddress, int iSize);\
int AutoGen_AutoRun_RegisterAutoCmd_##commandName(void)\
{\
AutoGen_RegisterAutoCmd_##commandName(&(varName), sizeof(varName));\
return 0;\
};\
STATIC_ASSERT(sizeof(varName) == sizeof(U8) || sizeof(varName) == sizeof(U32) || sizeof(varName) == sizeof(U64));

#define AUTO_CMD_FLOAT(varName, commandName) extern void AutoGen_RegisterAutoCmd_##commandName(float *pVarAddress, int iSize);\
int AutoGen_AutoRun_RegisterAutoCmd_##commandName(void)\
{\
AutoGen_RegisterAutoCmd_##commandName(&(varName), sizeof(varName));\
return 0;\
};\
STATIC_ASSERT(sizeof(varName) == sizeof(F32));

#define AUTO_CMD_STRING(varName, commandName) extern void AutoGen_RegisterAutoCmd_##commandName(void *pVarAddress, int iSize);\
int AutoGen_AutoRun_RegisterAutoCmd_##commandName(void)\
{\
AutoGen_RegisterAutoCmd_##commandName((varName), sizeof(varName));\
return 0;\
};\
STATIC_ASSERT(sizeof(varName) != 4 && sizeof(varName) != 8);


#define AUTO_CMD_ESTRING(varName, commandName) extern void AutoGen_RegisterAutoCmd_##commandName(void *pVarAddress, int iSize);\
int AutoGen_AutoRun_RegisterAutoCmd_##commandName(void)\
{\
char *pTemp;\
	if (varName) { pTemp = (char*)varName; varName = NULL; estrCopy2((char**)&varName, pTemp); };\
AutoGen_RegisterAutoCmd_##commandName((&varName), MAGIC_CMDPARSE_STRING_SIZE_ESTRING);\
return 0;\
};

#define AUTO_CMD_SENTENCE(varName, commandName) extern void AutoGen_RegisterAutoCmd_##commandName(void *pVarAddress, int iSize);\
int AutoGen_AutoRun_RegisterAutoCmd_##commandName(void)\
{\
AutoGen_RegisterAutoCmd_##commandName((varName), sizeof(varName));\
return 0;\
};\
STATIC_ASSERT(sizeof(varName) != 4 && sizeof(varName) != 8);

#define AUTO_CMD_ESENTENCE(varName, commandName) extern void AutoGen_RegisterAutoCmd_##commandName(void *pVarAddress, int iSize);\
int AutoGen_AutoRun_RegisterAutoCmd_##commandName(void)\
{\
AutoGen_RegisterAutoCmd_##commandName((&varName), MAGIC_CMDPARSE_STRING_SIZE_ESTRING);\
return 0;\
};

#endif //__cplusplus

#define AUTO_ENUM EXPECT_SEMICOLON
#define ENAMES(x)
#define EIGNORE
#define AEN_NO_PREFIX_STRIPPING
#define AEN_APPEND_TO(x)
#define AEN_WIKI(x)
#define AEN_EXTEND_WITH_DYNLIST(x)
#define AEN_APPEND_OTHER_TO_ME(x)
#define AEN_PAD

#define AUTO_STRUCT EXPECT_SEMICOLON
#define AST(...)
#define AST_NOT(...)
#define AST_FOR_ALL(...)
#define NO_AST
#define AST_IGNORE(...)
#define AST_CREATION_COMMENT_FIELD(...)
#define AST_IGNORE_STRUCTPARAM(...)
#define AST_IGNORE_STRUCT(...)
#define AST_START
#define AST_STOP
#define AST_STARTTOK(...)
#define AST_FORMATSTRING(...)
#define AST_ENDTOK(...)
#define AST_COMMAND(...)
#define AST_FIXUPFUNC(...)
#define AST_STRIP_UNDERSCORES
#define AST_NONCONST_PREFIXSUFFIX(...)
#define WIKI(...)
#define AST_MACRO(...)
#define AST_PREFIX(...)
#define AST_SUFFIX(...)
#define AUTO_TRANSACTION EXPECT_SEMICOLON
#define AST_NO_UNRECOGNIZED
#define AST_SINGLETHREADED_MEMPOOL
#define AST_THREADSAFE_MEMPOOL
#define AST_NOMEMTRACKING
#define AST_CONTAINER
#define AST_FORCE_CONST
#define AST_RUNTIME_MODIFIED

//note that this does NOT save the original case in the actual parsetable, as that would be too weird and inconsistent.
//Rather it stashes them off in the ParseTableInfo, where you can get them with ParserGetOriginalCaseFieldNames
#define AST_SAVE_ORIGINAL_CASE_OF_FIELD_NAMES

#define ATR_ARGS char **pestrATRSuccess, char **pestrATRFail
#define ATR_RECURSE pestrATRSuccess, pestrATRFail
#define ATR_PASS_ARGS pestrATRSuccess, pestrATRFail
#define ATR_EMPTY_ARGS NULL, NULL
#define ATR_RESULT_SUCCESS pestrATRSuccess
#define ATR_RESULT_FAIL pestrATRFail
#define ATR_GLOBAL_SYMBOL(filename, x) x
#define ATR_MAKE_APPEND
#define ATR_MAKE_DEFERRED
#define ATR_ALLOW_FULL_LOCK
#define ATR_LOCKS(argName, lockString)

#define AST_NO_PREFIX_STRIP
#define AST_CONTAINER
#define AST_FORCE_USE_ACTUAL_FIELD_NAME
#define AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT

#define AUTO_TP_FUNC_OPT EXPECT_SEMICOLON

#define AUTO_RUN_ANON(...) EXPECT_SEMICOLON

#define AUTO_STARTUP(...) EXPECT_SEMICOLON
#define ASTRT_DEPS(...)
#define ASTRT_CANCELLEDBY(...)

#define AUTO_TRANS_HELPER EXPECT_SEMICOLON
#define AUTO_TRANS_HELPER_SIMPLE EXPECT_SEMICOLON
#define ATH_ARG

#define NAME(...)
#define ADDNAMES(...)
#define REDUNDANTNAME
#define STRUCTPARAM
#define DEFAULT(...)
#define SUBTABLE(...)
#define STRUCT(...)
#define FORMAT_IP
#define FORMAT_KBYTES
#define FORMAT_FRIENDLYDATE
#define FORMAT_FRIENDLYSS2000
#define FORMAT_PERCENT
#define FORMAT_HSV
#define FORMAT_HSV_OFFSET
#define FORMAT_TEXTURE
#define FORMAT_COLOR
#define FORMAT_LVWIDTH(...)
#define MINBITS(...)
#define PRECISION(...)
#define FILENAME
#define TIMESTAMP
#define LINENUM
#define FLAGS
#define BOOLFLAG
#define USEDFIELD(...)
#define RAW
#define POINTER(...)
#define VEC2
#define VEC3
#define RG
#define RGBA
#define INDEX(...)
#define AUTO_INDEX(...)
#define EMBEDDED_FLAT
#define REDUNDANT_STRUCT(...)
#define USERFLAG(...)
#define REFDICT(...)
#define RESOURCEDICT(...)
#define CLIENT_ONLY
#define SERVER_ONLY
#define NO_TRANSACT
#define SOMETIMES_TRANSACT
#define POOL_STRING
#define SIMPLE_INHERITANCE
#define PERSIST
#define SUBSCRIBE
#define EDIT_ONLY
#define STRUCT_NORECURSE
#define KEY
#define ESTRING
#define INDEX_DEFINE
#define CURRENTFILE
#define POOL_STRING_DB
#define ALWAYS_ALLOC
#define LATEBIND
#define FORCE_CONTAINER
#define WIKILINK
#define REQUIRED
#define NON_NULL_REF
#define VITAL_REF
#define NO_WRITE
#define NO_NETSEND
#define NO_INDEX
#define POLYPARENTTYPE
#define POLYCHILDTYPE(...)
#define UNOWNED
#define SELF_ONLY
#define SELF_AND_TEAM_ONLY
#define NO_TEXT_SAVE
#define BLOCK_EARRAY

#define LATELINK EXPECT_SEMICOLON


#ifndef __cplusplus

#define AUTO_RUN EXPECT_SEMICOLON
#define AUTO_RUN_EARLY EXPECT_SEMICOLON
#define AUTO_RUN_LATE EXPECT_SEMICOLON
#define AUTO_RUN_FIRST EXPECT_SEMICOLON
#define AUTO_RUN_SECOND EXPECT_SEMICOLON
#define AUTO_FIXUPFUNC EXPECT_SEMICOLON
#define AUTO_RUN_POSTINTERNAL EXPECT_SEMICOLON

//AUTO_RUN_FILE is for DEBUG ONLY!!!!!!!!!!
#define AUTO_RUN_FILE EXPECT_SEMICOLON
//AUTO_RUN_FILE is for DEBUG ONLY!!!!!!!!!!!

// This is called to run the auto runs. This is slightly less magical, but simpler
void do_auto_runs(void);
void do_auto_runs_file(void);
#define DO_AUTO_RUNS do_auto_runs();
#define DO_AUTO_RUNS_FILE do_auto_runs_file();




//hooks into test harness. Talk to Ben.
#define AUTO_TEST(...) EXPECT_SEMICOLON
#define AUTO_TEST_CHILD(...) EXPECT_SEMICOLON
#define AUTO_TEST_GROUP(...) EXPECT_SEMICOLON
#define AUTO_TEST_BLOCK(...) EXPECT_SEMICOLON

//for Martin to hook in debug code if ent ptrs get corrupt
#define CHECK_ENTITY_PTR_VALIDITY(pEnt)


#endif //__cplusplus

C_DECLARATIONS_END

#include "superassert.h"

// not the clearest message, -1 sized array. Also, doesn't work in function scope.
#define STATIC_ASSERT(COND) typedef char __STATIC_ASSERT__[(COND)?1:-1];
#define STATIC_INFUNC_ASSERT(COND) { typedef char __STATIC_ASSERT__[(COND)?1:-1]; }
#define STATIC_ASSERT_MESSAGE(COND, msg) STATIC_ASSERT(COND)

#define TYPEOF_PARSETABLE(pti) TYPE_ ## pti

//do this only the first time you get to it
#define ONCE(stuffToDo) {ATOMIC_INIT_BEGIN; { stuffToDo; } ATOMIC_INIT_END;}

//NOT THREADSAFE
#define THROTTLE(secs, stuffToDo) { static int _siLastTime = 0; if (timeSecondsSince2000() - _siLastTime > secs) { _siLastTime = timeSecondsSince2000(); { stuffToDo; } } }




