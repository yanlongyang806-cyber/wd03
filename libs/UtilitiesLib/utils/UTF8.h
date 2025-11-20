#pragma once

//code for the big UTF8-UTF16 conversion. Checking it in right now without it actually being included anywhere just for purposes of
//not losing work

#ifdef __cplusplus
extern "C" {
#endif

#include "wininclude.h"
#include "scratchStack.h"
#include "memcheck.h"
#include "shlObj.h"


//takes a UTF8 string, mallocs and returns it converted to UTF16. If the input is NULL, will return NULL, so you
//should SAFE_FREE to free it. This is NOT particularly memory efficient, it assumes the worst case size, so should usually
//only be used for temporary buffers
SA_RET_OP_VALID WCHAR *UTF8_To_UTF16_malloc_dbg(SA_PARAM_OP_STR const char *pIn MEM_DBG_PARMS);
#define UTF8_To_UTF16_malloc(pIn) UTF8_To_UTF16_malloc_dbg(pIn MEM_DBG_PARMS_INIT)

//These are actually in StringUtil.c, this is what to call when you have a dest buffer already. Note that
//the return value is the number of characters written and does NOT include the null terminator
int UTF8ToWideStrConvert(SA_PARAM_NN_STR const unsigned char *str, SA_PARAM_OP_STR unsigned short *outBuffer, int outBufferMaxLength);
int WideToUTF8StrConvert(SA_PARAM_NN_STR const wchar_t* str, SA_PARAM_OP_STR char* outBuffer, int outBufferMaxLength);

//#define of the previous to keep the naming scheme consistent
#define UTF8_To_UTF16_Static UTF8ToWideStrConvert


void UTF16ToEstring_dbg(const WCHAR *pBuff, int iSize, char **ppOutEString MEM_DBG_PARMS);
#define UTF16ToEstring(pBuff, iSize, ppOutEString) UTF16ToEstring_dbg(pBuff, iSize, ppOutEString MEM_DBG_PARMS_INIT)


//starts with a UTF16 string, converts it to UTF8, then allocAddStrings it
const char *allocAddString_UTF16ToUTF8(const WCHAR *pIn);

char *strdup_UTF16ToUTF8_dbg(const WCHAR *pIn MEM_DBG_PARMS);
#define strdup_UTF16ToUTF8(pIn) strdup_UTF16ToUTF8_dbg(pIn MEM_DBG_PARMS_INIT)


//special version for use with windows functions which expect multiple strings to be rammed into a single string with 
//double /0 at the end.... ie, "foo\0bar\0wakka\0\0". MUST be double terminated.

SA_RET_OP_VALID WCHAR *UTF8_To_UTF16_DoubleTerminated_malloc_dbg(SA_PARAM_OP_STR const char *pIn MEM_DBG_PARMS);
#define UTF8_To_UTF16_DoubleTerminated_malloc(pIn) UTF8_To_UTF16_DoubleTerminated_malloc_dbg(pIn MEM_DBG_PARMS_INIT)

//special version that does only things that can be done very early in startup
char *UTF16_to_UTF8_CommandLine(const S16 *pIn);

#define UTF16_to_UTF8_Static WideToUTF8StrConvert


//here are all the actual function prototypes, stuck in a separate file so that they can be included from .cpp files and 
//in other weird ways
#include "utf8_prototypes.h"

#ifdef __cplusplus
}
#endif