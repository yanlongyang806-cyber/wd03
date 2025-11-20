#include "EString.h"
#include <stddef.h>
#include <stdlib.h>
#include <memory.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "StringUtil.h"
#include "utils.h"
#include "mathutil.h"
#include "net/net.h"
#include "timing.h"
#include "ScratchStack.h"
#include "earray.h"
#include "crypt.h"
#include "MemAlloc.h"
#include "utf8.h"

#include <wininclude.h>

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Unsorted);); // Should be no allocations in here.

#define isWhitespace(c) (((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r') ? 1 : 0)


extern int quick_vscprintf(const char *format,va_list args);

#if 0
	__forceinline static char* estrToStr(SA_PARAM_NN_VALID EString* str)
	{
		return str->str;
	}

	__forceinline static EString* estrFromStr(SA_PARAM_NN_STR char* str)
	{
		return (EString*)(str - EStrHeaderSize);
	}

	__forceinline static const EString* cestrFromStr(SA_PARAM_NN_STR const char* str)
	{
		return (const EString*)(str - EStrHeaderSize);
	}
#else
	#define estrToStr(estr)		((estr)->str)
	#define estrFromStr(str)	((EString*)((str) - EStrHeaderSize))
	#define cestrFromStr(str)	((const EString*)((str) - EStrHeaderSize))
#endif



// Returns the size of the entire EString object.
__forceinline static int estrObjSize(SA_PARAM_NN_VALID EString* str)
{
	return str->bufferLength + EStrHeaderSize + EStrTerminatorSize;
}

// Returns the size of the EString object that must be copied to move the object.
__forceinline static int estrObjDataSize(SA_PARAM_NN_VALID const EString* str)
{
	return str->stringLength + EStrHeaderSize + EStrTerminatorSize;
}


//---------------------------------------------------------------------------------
// EString: binary interface
//---------------------------------------------------------------------------------
void estrInsert_dbg(char** dst, unsigned int insertByteIndex, const char* buffer, unsigned int byteCount, const char *caller_fname, int line)
{
	EString* estr;
	int verifiedInsertByteIndex;
	U32 dstNewLen;

	if(!dst || !buffer)
		return;
	dstNewLen = byteCount;
	if(!*dst){
		verifiedInsertByteIndex = 0;
	}else{
		estr = estrFromStr(*dst);
		verifiedInsertByteIndex = MINMAX(insertByteIndex, 0, estr->stringLength);
		dstNewLen += estr->stringLength;
	}
	estrReserveCapacity_dbg(dst, dstNewLen, caller_fname, line);

	estr = estrFromStr(*dst);	// String might have been reallocated.
	memmove(estr->str + verifiedInsertByteIndex+byteCount, estr->str + verifiedInsertByteIndex, estr->stringLength + EStrTerminatorSize - verifiedInsertByteIndex);
	memcpy(estr->str + verifiedInsertByteIndex, buffer, byteCount);
	estr->stringLength += byteCount;
}

void estrRemove(char** dst, unsigned int removeByteIndex, unsigned int byteCount)
{
	EString* estr;
	int verifiedRemoveByteIndex;

	if(!dst || !*dst)
		return;

	PERFINFO_AUTO_START(__FUNCTION__,1);

	estr = estrFromStr(*dst);
	verifiedRemoveByteIndex = MINMAX(removeByteIndex, 0, estr->stringLength);

	// make sure the bytes to remove is valid. take a guess to the end of the string
	if( !verify( (verifiedRemoveByteIndex + byteCount) <= estr->stringLength ))
	{
		byteCount = estr->stringLength - verifiedRemoveByteIndex;
	}

	memmove(estr->str + verifiedRemoveByteIndex, estr->str + verifiedRemoveByteIndex + byteCount, estr->stringLength + EStrTerminatorSize - (verifiedRemoveByteIndex + byteCount));
	estr->stringLength -= byteCount;
	estrTerminateString(estr);

	PERFINFO_AUTO_STOP();
}

void estrConcat_dbg(char** dst, const char* src, unsigned int srcLength, const char *caller_fname, int line)
{
	PERFINFO_AUTO_START_L2(__FUNCTION__,1);
	estrConcat_dbg_inline(dst,src,srcLength,caller_fname,line);
	PERFINFO_AUTO_STOP_L2();
}

unsigned int estrLength(const char* const* str)
{
	if(!str || !*str)
		return 0;

	return (cestrFromStr(*str))->stringLength;
}

unsigned int estrAllocSize(const char* const * str)
{
	if(!str || !*str)
		return 0;

	return (cestrFromStr(*str))->bufferLength + EStrHeaderSize + EStrTerminatorSize;
}

//-------------------------------------------------------------------
// Constructor/Destructors
//-------------------------------------------------------------------
void estrCreate_dbg(char** str, const char *caller_fname, int line)
{
	// By default, allocate the string from the heap.
	// Specify a 0 size string to have estrCreate() use the default string size.
	// 0 for special_heap means use the main heap
	estrHeapCreate_dbg(str, 0, 0, caller_fname, line);
}

char *estrCreateFromStr_dbg(const char *str, const char *caller_fname, int line)
{
	char *eStr = NULL;
	estrCreate_dbg(&eStr, caller_fname, line);
	estrConcat_dbg_inline(&eStr,str,(int)strlen(str),caller_fname,line);
	return eStr;
}

char *estrStackCreateFromStr_dbg(const char *str, const char *caller_fname, int line)
{
	char *eStr = NULL;
	estrStackCreate_dbg(&eStr, MIN_STACK_ESTR, caller_fname, line);
	estrConcat_dbg_inline(&eStr,str,(int)strlen(str),caller_fname,line);
	return eStr;
}

void estrHeapCreate_dbg(char** str, unsigned int initSize, int special_heap, const char *caller_fname, int line)
{
	EString* estr;

	if(!str)
		return;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(0 == initSize)
		initSize = ESTR_DEFAULT_SIZE;

	estr = malloc_special_heap(EStrHeaderSize + EStrTerminatorSize + initSize, special_heap);
	// This needs to set each element of estr manually.
	memcpy(estr->header, "ESTR", 4);
	estr->bufferLength = initSize;
	estr->stringLength = 0;
	estr->estrType = ESTR_TYPE_HEAP;
	estr->str[0] = 0;
	*str = estrToStr(estr);
	
	PERFINFO_AUTO_STOP();
}

void estrStackCreate_dbg(char** str, unsigned int initSize, const char *caller_fname, int line)
{
	EString* estr;

	if(!str)
		return;

	estr = ScratchStackPerThreadAllocEx(EStrHeaderSize + EStrTerminatorSize + initSize, false, true, caller_fname, line);
	// This needs to set each element of estr manually.
	memcpy(estr->header, "ESTR", 4);
	estr->bufferLength = initSize;
	estr->stringLength = 0;
	estr->estrType = ESTR_TYPE_SCRATCH;
	estr->str[0] = 0;
	*str = estrToStr(estr);
}

void estrBufferCreate(char **str, char *pBuffer, unsigned int iBufferSize)
{
	EString* estr;
	S64 iBufLen;
	if (!str)
		return;
	

	estr = AlignPointerUpPow2(pBuffer, 4);
	// This needs to set each element of estr manually.
	memcpy(estr->header, "ESTR", 4);
	iBufLen = iBufferSize - ((char*)estr - pBuffer) - EStrHeaderSize - EStrTerminatorSize;
	assert(iBufLen > 0);
	estr->bufferLength = iBufLen;
	estr->stringLength = 0;
	estr->estrType = ESTR_TYPE_BUFFER;
	estr->str[0] = 0;
	*str = estrToStr(estr);
}


void estrDestroy(char** str)
{
	EString* estr;

	if(!str || !*str)
		return;

	estr = estrFromStr(*str);
	switch (estr->estrType)
	{
		xcase ESTR_TYPE_HEAP:
			PERFINFO_AUTO_START_FUNC();
			free(estr);
			PERFINFO_AUTO_STOP();
		xcase ESTR_TYPE_ALLOCA:
			break; //do nothing
		xcase ESTR_TYPE_SCRATCH:
			ScratchFree(estr);
		xcase ESTR_TYPE_BUFFER:
			break; //do nothing
		xdefault:
			if (assertIsDevelopmentMode())
				devassertmsg( memcmp(estr->header,ESTR_HEADER, 4) == 0, "Trying to estrDestroy a non-estring pointer.");
			else
				ErrorfForceCallstack("Trying to estrDestroy a non-estring pointer.");
	}
	*str = NULL;
}

void estrClear(char** str)
{
	EString* estr;

	if(!str || !*str)
		return;

	estr = estrFromStr(*str);
	estr->stringLength = 0;
	estrTerminateString(estr);
}

void estrClearTo(char** str, char c)
{
	EString* estr;
	U32 i;

	if(!str || !*str)
		return;

	estr = estrFromStr(*str);
	for (i = 0; i < estr->stringLength; i++) (*str)[i] = c;
	estrTerminateString(estr);
}

void estrAppend_dbg(char** dst, const char** src, const char *caller_fname, int line)
{
	const EString* estrSrc;

	if(!dst || !src || !*src)
		return;
	if(!*dst)
		estrCreate_dbg(dst, caller_fname, line);

	estrSrc = cestrFromStr(*src);
	
	estrConcat_dbg(dst, estrSrc->str, estrSrc->stringLength, caller_fname, line);
}

void estrAppend2_dbg(char** dst, const char* src, const char *caller_fname, int line)
{
	estrAppend2_dbg_inline(dst,src,caller_fname,line);
}

unsigned int estrPrintf_dbg(char** str, const char *caller_fname, int line, const char* format, ...)
{
	unsigned int count;
	VA_START(args, format);
	estrClear(str);
	count = estrConcatfv_dbg(str, caller_fname, line, format, args);
	VA_END();
	return count;
}

unsigned int estrPrintfUnsafe_dbg(char** str, const char *caller_fname, int line, const char* format, ...)
{
	unsigned int count;
	VA_START(args, format);
	estrClear(str);
	count = estrConcatfv_dbg(str, caller_fname, line, format, args);
	VA_END();
	return count;
}

unsigned int estrConcatf_dbg(char** str, const char *caller_fname, int line, const char* format, ...)
{
	unsigned int count;
	VA_START(args, format);
	count = estrConcatfv_dbg(str, caller_fname, line, format, args);
	VA_END();
	return count;
}

unsigned int estrConcatfv_dbg(char** str, const char *caller_fname, int line, const char* format, va_list args)
{
	EString* estr;
	int printCount;

	if(!str)
		return 0;
	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	if(!*str)
		estrCreate_dbg(str, caller_fname, line);

	estr = estrFromStr(*str);

	// Try to print the string.
	printCount = quick_vsnprintf(estr->str + estr->stringLength, estr->bufferLength - estr->stringLength,
		_TRUNCATE, format, args);
	if(printCount >= 0) {
		// Good!  It fit.
	} else {
		
		printCount = quick_vscprintf((char*)format, args);
		estrReserveCapacity_dbg(str, estr->stringLength + printCount + 1, caller_fname, line);
		estr = estrFromStr(*str);

		printCount = quick_vsnprintf(estr->str + estr->stringLength, estr->bufferLength - estr->stringLength,
			_TRUNCATE, format, args);
		
		assert(printCount >= 0);
	}

	estr->stringLength += printCount;

	estrTerminateString(estr);

	PERFINFO_AUTO_STOP_L2();
	return printCount;
}

void estrConcatString_dbg(char** str, const char* appendme, unsigned int width, const char *caller_fname, int line)
{
	EString* estr;
	unsigned int requiredLength;
	char *s;
	int widthleft;

	if(!str)
		return;
	PERFINFO_AUTO_START(__FUNCTION__,1);
	if(!*str)
		estrCreate_dbg(str, caller_fname, line);

	estr = estrFromStr(*str);
	requiredLength = estr->stringLength + width + EStrTerminatorSize;
	if (requiredLength > estr->bufferLength) {
		estrReserveCapacity_dbg(str, requiredLength, caller_fname, line);
		estr = estrFromStr(*str);
	}
	// Append the string
	for (s=*str+estr->stringLength, widthleft=width; *appendme && widthleft; *s++=*appendme++, widthleft--);
	// Pad if less than the width
	for (;widthleft; *s++=' ', widthleft--);
	// Increment stored length
	estr->stringLength += width;
	// Check to see if anything didn't fit in the width
	if (*appendme) {
		// There's more stuff to append
		estrConcatString_dbg(str, appendme, (int)strlen(appendme), caller_fname, line);
		PERFINFO_AUTO_STOP();
		return;
	}
	// Null terminate
	estrTerminateString(estr);
	assert(estr->stringLength <= estr->bufferLength);
	PERFINFO_AUTO_STOP();
}

void estrConcatChar_dbg(char** str, char appendme, int count, const char *caller_fname, int line)
{
	EString* estr;

	if(!str || count <= 0)
		return;
	PERFINFO_AUTO_START_L2(__FUNCTION__,1);
	if(!*str)
		estrCreate_dbg(str, caller_fname, line);

	estr = estrFromStr(*str);
	if (estr->stringLength + count > estr->bufferLength) {
		estrReserveCapacity_dbg(str, estr->stringLength + count, caller_fname, line);
		estr = estrFromStr(*str);
	}
	// Append the char, advance the length
	while (count)
	{
		*(*str+estr->stringLength++)=appendme;
		--count;
	}
	// Null terminate
	estrTerminateString(estr);
	PERFINFO_AUTO_STOP_L2();
}



void estrCopy_dbg(char** dst, const char** src, const char *caller_fname, int line)
{
	if (src == dst)
		return; // No work if source and dest are the same
	estrClear(dst);
	estrAppend_dbg(dst, src, caller_fname, line);
}

void estrCopy2_dbg(char** dst, const char* src, const char *caller_fname, int line)
{
	if (!dst || src == *dst)
		return; // No work if source and dest are the same
	estrClear(dst);
	estrAppend2_dbg_inline(dst, src, caller_fname, line);
}

unsigned int estrReserveCapacity_dbg(char** str, unsigned int reserveSize, const char *caller_fname, int line)
{
	int index;
	unsigned int newObjSize;
	unsigned int newBufferSize;
	EString* estr;

	if(!str)
		return 0;
	if(!*str){
		estr = NULL;
	}else{
		estr = estrFromStr(*str);

		if(estr->bufferLength >= reserveSize + EStrTerminatorSize){
			return estr->bufferLength;
		}
	}

	// If the capacity is already larger than the specified reserve size,
	// the operation is already complete.

	// Attempt to make the actual allocaiton size a power of 2
	index = highBitIndex(reserveSize + ESTR_SHRINK_AMOUNT) + 1;
	if(32 <= index)
	{
		newObjSize = 0xffffffff;	
		// I hope this never happens.  =)
		// Allocating 4gb of memory is always a bad idea.
	}
	else
	{
		newObjSize = (1 << index);
	}

	newBufferSize = newObjSize - ESTR_SHRINK_AMOUNT;

	if (!estr)
	{
		estrHeapCreate_dbg(str, newBufferSize, 0, caller_fname, line);
		estr = estrFromStr(*str);
	}
	else if(estr->estrType == ESTR_TYPE_HEAP)
	{
		PERFINFO_AUTO_START_FUNC();
		estr = srealloc(estr,newObjSize);
		PERFINFO_AUTO_STOP();
	}
	else if (estr->estrType == ESTR_TYPE_SCRATCH && ScratchPerThreadReAllocInPlaceIfPossible(estr, newObjSize))
	{



	}
	else
	{
		EString *newMemory;	
		PERFINFO_AUTO_START_FUNC();
		newMemory = smalloc(newObjSize);
		PERFINFO_AUTO_STOP();
		memcpy(newMemory,estr,estr->bufferLength + EStrHeaderSize + EStrTerminatorSize);
		if (estr->estrType == ESTR_TYPE_SCRATCH)
		{
			ScratchFree(estr);
		}
		estr = newMemory;
		estr->estrType = ESTR_TYPE_HEAP;
	}

	estr->bufferLength = newBufferSize;
	*str = estrToStr(estr);
	return newBufferSize;
}

unsigned int estrGetCapacity(const char** str)
{
	const EString* estr;
	if(!str || !*str)
		return 0;

	estr = cestrFromStr(*str);
	return estr->bufferLength;
}

unsigned int estrSetSize_dbg(char** str, unsigned int size, const char *caller_fname, int line)
{
	EString* estr;
	char*	 pStr;

	if(!str)
		return 0;

	estrReserveCapacity_dbg(str, size, caller_fname, line);
	estr = estrFromStr(*str);
	if(size < estr->stringLength)
	{
		estr->stringLength = size;
		pStr = estr->str;
		pStr[size] = 0;
		return size;
	}
	memset(estr->str + estr->stringLength, 0, size - estr->stringLength + EStrTerminatorSize);
	estr->stringLength = size;
	return size;
}

unsigned int estrForceSize_dbg(char** str, unsigned int size, const char *caller_fname, int line)
{
	EString* estr;
	char*	pStr;

	if(!str)
		return 0;

	estrReserveCapacity_dbg(str, size, caller_fname, line);
	estr = estrFromStr(*str);
	if(size < estr->stringLength)
	{
		estr->stringLength = size;
		pStr = estr->str;
		pStr[size] = 0;

		return size;
	}
	estr->stringLength = size;
	memset(estr->str + estr->stringLength, 0, EStrTerminatorSize);	
	return size;
}

unsigned int estrAppendEscaped_dbg(char **str, const char *src, const char *caller_fname, int line)
{
	if (!str || !src)
	{
		return 0;
	}
	return estrAppendEscapedCount_dbg(str,src,(unsigned int)strlen(src), false, caller_fname, line);
}

unsigned int estrAppendEscapedf_dbg(char** str, const char *caller_fname, int line, const char* format, ...)
{
	char *temp = NULL;
	unsigned int count;
	estrStackCreate(&temp);
	VA_START(args, format);
	count = estrConcatfv_dbg(&temp, caller_fname, line, format, args);
	VA_END();

	count = estrAppendEscapedCount_dbg(str, temp, count, false, caller_fname, line);
	estrDestroy(&temp);
	return count;
}


unsigned int estrAppendEscapedCount_dbg(char **str, const char *src, unsigned int inlen, bool bIgnoreNULL, const char *caller_fname, int line)
{
	EString* estr;
	size_t newlen;
	size_t written;
	if (!str || !src)
	{
		return 0;
	}

	newlen = inlen * 2 + 1;

	if(*str){
		estr = estrFromStr(*str);
		newlen += estr->stringLength;
	}

	estrReserveCapacity_dbg(str, (unsigned int)newlen, caller_fname, line);

	estr = estrFromStr(*str);

	written = escapeDataStatic(src,inlen,estr->str + estr->stringLength,newlen - estr->stringLength,!bIgnoreNULL);

	estr->stringLength = estr->stringLength + (int)written;
	memset(estr->str + estr->stringLength, 0, EStrTerminatorSize);
	return (unsigned int)written;
}

unsigned int estrAppendUnescaped_dbg(char **str, const char *src, const char *caller_fname, int line)
{
	return estrAppendUnescapedCount_dbg(str,src,(unsigned int)strlen(src), caller_fname, line);
}


unsigned int estrAppendUnescapedCount_dbg(char **str, const char *src, unsigned int inlen, const char *caller_fname, int line)
{
	EString* estr;
	size_t newlen;
	size_t written;
	if (!str || !src)
	{
		return 0;
	}
	newlen = inlen + 1;

	if(*str)
	{
		estr = estrFromStr(*str);
		newlen += estr->stringLength;
	}

	estrReserveCapacity_dbg(str,(unsigned int)newlen, caller_fname, line);

	estr = estrFromStr(*str);

	written = unescapeDataStatic(src,inlen,estr->str + estr->stringLength,newlen - estr->stringLength,true);

	estr->stringLength = estr->stringLength + (int)written;
	memset(estr->str + estr->stringLength, 0, EStrTerminatorSize);
	return (unsigned int)written;
}

static char *estrPreAlloc_dbg(char **dst, int srcLength, const char *caller_fname, int line)
{
	char	*target;
	EString	*estrDst;
	U32		dstNewLen;

	if(!dst)
		return 0;
	dstNewLen = srcLength;
	if(*dst){
		estrDst = estrFromStr(*dst);
		dstNewLen += estrDst->stringLength;
	}

	estrReserveCapacity_dbg(dst, dstNewLen, caller_fname, line);
	estrDst = estrFromStr(*dst);
	target = (*dst) + estrDst->stringLength;
	estrDst->stringLength += srcLength;
	estrTerminateString(estrDst);
	return target;
}

//packet-related things
void estrAppendFromPacket_dbg(char **str, Packet *pPak, const char *caller_fname, int line)
{
	char	*s = pktGetStringTemp(pPak);
	estrAppend2_dbg_inline(str, s, caller_fname, line);
}

void estrCopyFromPacket_dbg(char **str, Packet *pPak, const char *caller_fname, int line)
{
	estrClear(str);
	estrAppendFromPacket_dbg(str,pPak, caller_fname, line);
}

//packet-related things
void estrAppendFromPacketNonEmpty_dbg(char **str, Packet *pPak, const char *caller_fname, int line)
{
	char	*s = pktGetStringTemp(pPak);
	if (s && s[0])
	{
		estrAppend2_dbg_inline(str, s, caller_fname, line);
	}
}

void estrCopyFromPacketNonEmpty_dbg(char **str, Packet *pPak, const char *caller_fname, int line)
{
	estrClear(str);
	estrAppendFromPacketNonEmpty_dbg(str,pPak, caller_fname, line);
	if (!estrLength(str))
	{
		estrDestroy(str);
	}
}


bool estrReplaceOccurrences_dbg(bool bCaseInsensitive, char **str, const char* pWhatToFind, const char *pWhatToReplaceItWith, const char *caller_fname, int line)
{
	int iSourceLength = estrLength(str);
	int iWhatToFindLength = (int)strlen(pWhatToFind);
	int iWhatToReplaceItWithLength = pWhatToReplaceItWith ? (int)strlen(pWhatToReplaceItWith) : 0;
	int iIndexToStartLooking = 0;
	bool bFoundAtLeastOne = false;

	if (!iSourceLength)
	{
		return false;
	}


	assertmsg(iWhatToFindLength, "Can't do replace occurrences with zero-length pWhatToFind");
	assert(str && *str && pWhatToFind);

	while (1)
	{
		char *pOccurrence;
		int iOccurrenceOffset;

		//can't use pOccurrence after a call to estrSetSize, use offset instead
		if (bCaseInsensitive)
		{
			pOccurrence = strstri((*str) + iIndexToStartLooking, pWhatToFind);
		}
		else
		{
			pOccurrence = strstr((*str) + iIndexToStartLooking, pWhatToFind);
		}
		
		if (!pOccurrence)
		{
			return bFoundAtLeastOne;
		}

		bFoundAtLeastOne = true;

		iOccurrenceOffset = pOccurrence - *str;

		if (iWhatToReplaceItWithLength > iWhatToFindLength)
		{
			estrSetSize_dbg(str, iSourceLength + iWhatToReplaceItWithLength - iWhatToFindLength, caller_fname, line);
		}

		memmove(*str + iOccurrenceOffset + iWhatToReplaceItWithLength, *str + iOccurrenceOffset + iWhatToFindLength, 
			iSourceLength - iOccurrenceOffset - iWhatToFindLength);
		memcpy(*str + iOccurrenceOffset, pWhatToReplaceItWith, iWhatToReplaceItWithLength);

		if (iWhatToReplaceItWithLength < iWhatToFindLength)
		{
			estrSetSize_dbg(str, iSourceLength + iWhatToReplaceItWithLength - iWhatToFindLength, caller_fname, line);
		}

		iSourceLength = iSourceLength + iWhatToReplaceItWithLength - iWhatToFindLength;

		iIndexToStartLooking = iOccurrenceOffset + iWhatToReplaceItWithLength;
	}
}

int estrResolveOnOffParenMacro_dbg(char **str, const char* pMacroName, bool bOn, const char *caller_fname, int line)
{
	int iMacroLength = (int)strlen(pMacroName);
	int iCurStartingOffset = 0;
	char *pNextOccurrence;
	int iRetCount = 0;

	if (!pMacroName)
	{
		return iRetCount;
	}

	if (!*str)
	{
		return iRetCount;
	}

	if (!iMacroLength)
	{
		return iRetCount;
	}

	if (!estrLength(str))
	{
		return iRetCount;
	}

	while ((pNextOccurrence = strstri((*str) + iCurStartingOffset, pMacroName)))
	{
		int iParenDepth = 1;
		int iOpenParenIndex;
		int iCloseParenIndex = 0;
		int i;
		int iThisOccurrenceIndex = pNextOccurrence - *str;

		if (pNextOccurrence[iMacroLength] != '(')
		{
			iCurStartingOffset = iThisOccurrenceIndex + 1;
			continue;
		}

		i = iMacroLength + 1;
		iOpenParenIndex = iThisOccurrenceIndex + iMacroLength;

		while (1)
		{
			if (pNextOccurrence[i] == 0)
			{
				//ran out of string in the middle of our macro.
				iCurStartingOffset = iThisOccurrenceIndex + 1;
				break;
			}

			if (pNextOccurrence[i] == '(')
			{
				iParenDepth++;
			}
			else if (pNextOccurrence[i] == ')')
			{
				iParenDepth--;
				if (iParenDepth == 0)
				{
					iCloseParenIndex = iThisOccurrenceIndex + i;
					break;
				}
			}

			i++;
		}

		if (!iCloseParenIndex)
		{
			continue;
		}

		iRetCount++;
		if (bOn)
		{
			estrRemove(str, iCloseParenIndex, 1);
			estrRemove(str, iThisOccurrenceIndex, iOpenParenIndex - iThisOccurrenceIndex + 1);
		}
		else
		{
			estrRemove(str, iThisOccurrenceIndex, iCloseParenIndex - iThisOccurrenceIndex + 1);
		}
	}

	return iRetCount;
}



void estrReplaceMultipleChars(char **dst, const char *pCharString, char replacementChar)
{
	int i;

	if (!(*dst))
	{
		return;
	}

	for (i=estrLength(dst)-1; i >=0; i--)
	{
		if (strchr(pCharString, (*dst)[i]))
		{
			(*dst)[i] = replacementChar;
		}
	}
}

void estrFixupNewLinesForWindows_dbg(char **str, const char *caller_fname, int line)
{
	int iSize = estrLength(str);
	int i;
	if (!str || !*str)
		return;

	for (i = iSize - 1; i >= 0; i--)
	{
		if ((*str)[i] == '\n' && (i == 0 || (*str)[i-1] != '\r'))
		{
			estrInsert_dbg(str, i, "\r", 1, caller_fname, line);
		}
	}
}

#define IS_WHITESPACE_EX(c) ((IS_WHITESPACE(c) || pExtraWhitespaceChars && strchr(pExtraWhitespaceChars, (c))))


int estrTrimLeadingAndTrailingWhitespaceEx(char **str, const char *pExtraWhitespaceChars)
{
	int iSize = estrLength(str);
	int iTrailCount = 0, iLeadCount = 0;
	int iIndex;

	if (iSize == 0 || !str || !*str)
	{
		return 0;
	}

	iIndex = iSize - 1;

	while (iIndex >= 0 && IS_WHITESPACE_EX((*str)[iIndex]))
	{
		iTrailCount++;
		iIndex--;
	}

	iSize -= iTrailCount;
	estrSetSize(str, iSize);

	if (iSize == 0)
	{
		return iTrailCount + iLeadCount;
	}

	iIndex = 0;

	while (iIndex < iSize && IS_WHITESPACE_EX((*str)[iIndex]))
	{
		iLeadCount++;
		iIndex++;
	}

	if (iLeadCount)
	{
		estrRemove(str, 0, iLeadCount);
	}

	return iTrailCount + iLeadCount;
}



void estrCopyWithHTMLEscaping(char **str, const char* src, bool escape_newline)
{
	estrClear(str);

	if (!src)
	{
		return;
	}

	//special case... a single space gets turned into nbsp
	if (strcmp(src, " ") == 0)
	{
		estrCopy2(str, "&nbsp;");
		return;
	}

	while (*src)
	{
		switch (*src)
		{
		case '&':
			estrConcatf(str, "&amp;");
			break;
		case '<':
			estrConcatf(str, "&lt;");
			break;
		case '>':
			estrConcatf(str, "&gt;");
			break;
		case '\'':
			estrConcatf(str, "&#39;");
			break;
		case '"':
			estrConcatf(str, "&quot;");
			break;
		case '\r':
			if (!escape_newline)
				estrConcatChar(str, *src);
			break;
		case '\n':
			if (escape_newline)
			{
				estrConcatf(str, "<br />");
				break;
			}
			// Fall-through:
		default:
			{
				U8 iVal = (U8) *src;
				int bytes = UTF8GetCodepointLength(src);
				if (bytes > 1)
				{
					U32 codepoint = UTF8ToCodepoint(src);
					estrConcatf(str, "&#%d;", codepoint);
					src += bytes-1;
				}
				else if ((0x08 < iVal && iVal < 0x0E) || (0x1F < iVal && iVal < 0x7F))
				{
					// Valid non-control ASCII character (or whitespace control character)
					estrConcatChar(str, *src);
				}
				else
				{
					// Other values are invalid control characters, write them out as an escaped value
					estrConcatf(str, "&amp;#0x%02x;", iVal);
				}
				break;
			}
		}

		src++;
	}
}

void estrCopyWithHTMLEscapingSafe(char **str, const char* src, bool escape_newline)
{
	estrCopyWithHTMLEscaping(str, src, escape_newline);
	if ((*str) == NULL)
		estrCopy2(str, "");
}

void estrCopyWithURIUnescaping(char **str, const char* src)
{
	char *head = (char*)src;
	estrClear(str);

	if (!head)
		return;

	while (head[0])
	{
		switch(*head)
		{
		case '+': estrConcatChar(str, ' '); break;
		case '%': 
			{
				if (head[1] && head[2])
				{
					char c = head[0];
					char buf[3] = {head[1],head[2],'\0'};
					int con = strtol(buf, NULL, 16);
					switch (con)
					{
					case 0:
					xcase 0x21: c = '!'; 
					xcase 0x22: c = '"';
					xcase 0x23: c = '#';
					xcase 0x24: c = '$';
					xcase 0x25: c = '%';
					xcase 0x26: c = '&';
					xcase 0x27: c = '\'';
					xcase 0x28: c = '(';
					xcase 0x29: c = ')';
					xcase 0x2A: c = '*';
					xcase 0x2B: c = '+';
					xcase 0x2C: c = ',';
					xcase 0x2D: c = '-';
					xcase 0x2E: c = '.';
					xcase 0x2F: c = '/';

					xcase 0x3A: c = ':';
					xcase 0x3B: c = ';';
					xcase 0x3C: c = '<';
					xcase 0x3D: c = '=';
					xcase 0x3E: c = '>';
					xcase 0x3F: c = '?';
					xcase 0x40: c = '@';

					xcase 0x5B: c = '[';
					xcase 0x5C: c = '\\';
					xcase 0x5D: c = ']';
					xcase 0x5E: c = '^';
					xcase 0x5F: c = '_';
					xcase 0x60: c = '`';

					xcase 0x7B: c = '{';
					xcase 0x7C: c = '|';
					xcase 0x7D: c = '}';
					xcase 0x7E: c = '~';
					default:
						c = 0;
					}
					if (c)
					{
						estrConcatChar(str, c);
						head += 2;
						break;
					}
				}
			}
		default: estrConcatChar(str, *head);
		}
		head++;
	}
}

void estrCopyWithURIEscaping(char **str, const char* src)
{
	char *head = (char*)src;
	estrClear(str);

	if (!head)
		return;

	while (*head)
	{
		if (isalnum(*head))
		{
			estrConcatChar(str, *head);
		}
		else if (*head == ' ')
		{
			estrConcatChar(str, '+');
		}
		else
		{
			int top = (*head) >> 4;
			int bottom = (*head) & 0xf;

			estrConcatChar(str, '%');

			if (top > 9)
			{
				estrConcatChar(str, 'A' + top - 10);
			}
			else
			{
				estrConcatChar(str, '0' + top);
			}
			
			if (bottom > 9)
			{
				estrConcatChar(str, 'A' + bottom - 10);
			}
			else
			{
				estrConcatChar(str, '0' + bottom);
			}
		}

		head++;
	}
}



void estrCopyWithHTMLUnescaping(char **str, const char* src)
{
	static char *map[][2] = {
		{ "&amp;",  "&", },
		{ "&nbsp;", " ", },
		{ "&lt;",   "<", },
		{ "&gt;",   ">", },
		{ "&quot;", "\"", },
		{ "&apos;", "'", }, // yes, this one is an XHTML standard and not HTML
	};

	static int iNumEntries = sizeof(map) / sizeof (map[0]);
	char *pchCur = (char *) src;

	estrClear(str);

	if (!pchCur)
	{
		return;
	}

	for ( ; *pchCur; pchCur++) {
		int i;
		bool unescaped=false;

		if (*pchCur == '&') {
			// Check for HTML escapes
			if (*(pchCur+1) == '#') {
				char *pchEnd = strchr(pchCur+2, ';');

				if (pchEnd) {
					wchar_t character[2];
					char converted[16];
					char *pchDigit;
					int val = 0;

					for (pchDigit = pchCur + 2; *pchDigit && isdigit(*pchDigit); pchDigit++) {
						val *= 10;
						val += *pchDigit - '0';
					}

					character[0] = val;
					//character = 0x9580; // some Asian character that's useful for testing
					character[1] = 0;

                    WideToUTF8StrConvert(character, converted, sizeof(converted));

                    estrAppend2(str, converted);
					unescaped = true;

					pchCur = pchEnd;
				}
			} else {
				for (i=0; i < iNumEntries; i++) {
					if (strStartsWith(pchCur, map[i][0])) {
						estrAppend2(str, map[i][1]);
						pchCur += strlen(map[i][0]) - 1;
						unescaped = true;
						break;
					}
				}
			}
		}
		
		if (!unescaped) {
			estrConcatChar(str, *pchCur);
		}
	}
}

void estrCopyWithHTMLUnescapingSafe(char **str, const char* src)
{
	estrCopyWithHTMLUnescaping(str, src);
	if ((*str) == NULL)
		estrCopy2(str, "");
}

typedef struct
{
	char theChar;
	char *pString;
} SuperEscapeCharName;

SuperEscapeCharName sSuperEscapeCharNames[] =
{
	{ '_', "under", },
	{ '-', "dash", },
	{ '"', "doublequote", },
	{ '\'', "singlequote" },
	{ '\\', "bslash" },
	{ '/', "fslash" },
	{ '.', "period" },
	{ ',', "comma" },
	{ '?', "questmark" },
	{ '(', "lparens" },
	{ ')', "rparens" },
	{ '[', "lbracket" },
	{ ']', "rbracket" },
	{ '{', "lbrace" },
	{ '}', "rbrace" },
	{ '=', "equals" },
	{ '+', "plus" },
	{ '!', "exclpoint" },
	{ '#', "hash" },
	{ '@', "at" },
	{ '$', "dollar" },
	{ '^', "caret" },
	{ '~', "tilde" },
	{ '`', "backsinglequote" },
	{ '%', "percent" },
	{ '&', "ampersand" },
	{ '*', "asterisk" },
	{ '|', "vertbar" },
	{ ':', "colon" },
	{ ';', "semicolon" },
	{ '<', "lessthan" },
	{ '>', "greaterthan" },
};



void estrSuperEscapeString_shorter_dbg(char **ppOutString, const char* pInString, const char *caller_fname, int line)
{
	const U8 *pReadHead = pInString;
	estrDestroy(ppOutString);

	if (!pReadHead)
	{
		return;
	}

	while (*pReadHead)
	{
		if (isalnum(*pReadHead))
		{
			if (*pReadHead == 'Q')
			{
				estrConcatChar_dbg(ppOutString, 'Q', 1, caller_fname, line);
				estrConcatChar_dbg(ppOutString, 'Q', 1, caller_fname, line);
			}
			else
			{
				estrConcatChar_dbg(ppOutString, *pReadHead, 1, caller_fname, line);
			}
		}
		else if (*pReadHead == ' ')
		{
			estrConcatChar_dbg(ppOutString, '_', 1, caller_fname, line);
		}
		else
		{
			estrConcatf_dbg(ppOutString, caller_fname, line, "Q%03u", (U32)(*pReadHead));			
		}

		pReadHead++;
	}
}




void estrSuperEscapeString_dbg(char **ppOutString, const char* pInString, const char *caller_fname, int line)
{
	const U8 *pReadHead = pInString;
	estrDestroy(ppOutString);

	if (!pReadHead)
	{
		return;
	}

	while (*pReadHead)
	{
		if (isalnum(*pReadHead))
		{
			if (*pReadHead == 'Q')
			{
				estrConcatChar_dbg(ppOutString, 'Q', 1, caller_fname, line);
				estrConcatChar_dbg(ppOutString, 'Q', 1, caller_fname, line);
			}
			else
			{
				estrConcatChar_dbg(ppOutString, *pReadHead, 1, caller_fname, line);
			}
		}
		else if (*pReadHead == ' ')
		{
			estrConcatChar_dbg(ppOutString, '_', 1, caller_fname, line);
		}
		else
		{
			int i;
			bool bFound = false;

			for (i=0; i < ARRAY_SIZE(sSuperEscapeCharNames); i++)
			{
				if (*pReadHead == sSuperEscapeCharNames[i].theChar)
				{
					estrConcatf_dbg(ppOutString, caller_fname, line, "Q%s", sSuperEscapeCharNames[i].pString);
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				estrConcatf_dbg(ppOutString, caller_fname, line, "Q%03u", (U32)(*pReadHead));
			}
		}

		pReadHead++;
	}
}


bool estrSuperUnescapeString_dbg(char **ppOutString, const char* pInString, const char *caller_fname, int line)
{
	const char *pReadHead = pInString;
	estrDestroy(ppOutString);

	if (!pReadHead)
	{
		return true;
	}

	while (*pReadHead)
	{
		if (*pReadHead == '_')
		{
			estrConcatChar_dbg(ppOutString, ' ', 1, caller_fname, line);
			pReadHead++;
		}
		else if (*pReadHead != 'Q')
		{
			estrConcatChar_dbg(ppOutString, *pReadHead, 1, caller_fname, line);
			pReadHead++;
		}
		else
		{
			pReadHead++;
			if (!*pReadHead)
			{
				return false;
			}

			if (*pReadHead == 'Q')
			{
				estrConcatChar_dbg(ppOutString, 'Q', 1, caller_fname, line);
				pReadHead++;
			}
			else if (isdigit(*pReadHead))
			{
				if (!isdigit(*(pReadHead+1)) || !isdigit(*(pReadHead+2)))
				{
					return false;
				}
				estrConcatChar_dbg(ppOutString, (char)( ((*pReadHead) - '0') * 100 + ((*(pReadHead + 1)) - '0') * 10 + ((*(pReadHead + 2)) - '0')), 1, caller_fname, line);
				pReadHead++;
				pReadHead++;
				pReadHead++;
			}
			else
			{
				int i;

				for (i=0; i < ARRAY_SIZE(sSuperEscapeCharNames); i++)
				{
					if (strStartsWith(pReadHead, sSuperEscapeCharNames[i].pString))
					{
						estrConcatChar_dbg(ppOutString, sSuperEscapeCharNames[i].theChar, 1, caller_fname, line);
						pReadHead += strlen(sSuperEscapeCharNames[i].pString);
						break;
					}
				}
				
				if (i == ARRAY_SIZE(sSuperEscapeCharNames))
				{
					return false;
				}
			}
		}
	}

	return true;
}

//escape a string following the rules for JSON string literals (same rules as C literals too)
void estrCopyWithJSONEscaping(char **dst, const char *src)
{
	estrClear(dst);
	if (!src)
		return;
	if (!*src)
		estrCopy2(dst, "");
	while (*src)
	{
		switch (*src)
		{
		case '"':
			estrConcatf(dst, "\\\"");
		xcase '\r':
			estrConcatf(dst, "\\r");
		xcase '\n':
			estrConcatf(dst, "\\n");
		xcase '\t':
			estrConcatf(dst, "\\t");
		xcase '\\':
			estrConcatf(dst, "\\\\");
		xdefault:
			{
				U8 iVal = (U8) *src;
				int bytes = UTF8GetCodepointLength(src);
				// Codepoints U+0000 to U+007F are one-byte ASCII characters
				if (bytes > 1)
				{
					U32 codepoint = UTF8ToCodepoint(src);
					//estrConcatf(str, "&#%d;", codepoint);
					if (codepoint < (U32) 0x10000)
					{
						// Codepoints U+0080 to U+FFFF are represented straight
						estrConcatf(dst, "\\u%04x", codepoint);
					}
					else if (codepoint < (U32) 0x110000)
					{
						U16 lead_surrogate, trail_surrogate;
						codepoint -= 0x10000;
						lead_surrogate = 0xD800 + ((U16) (codepoint >> 10));
						trail_surrogate = 0xDC00 + ((U16) (codepoint & 0x3FF));
						estrConcatf(dst, "\\u%04x\\u%04x", lead_surrogate, trail_surrogate);
					}
					// Higher codepoints are invalid and cannot be encoded
					src += bytes-1;
				}
				else if ((0x08 < iVal && iVal < 0x0E) || (0x1F < iVal && iVal < 0x7F))
				{
					// Valid non-control ASCII character (or whitespace control character)
					estrConcatChar(dst, *src);
				}
				// Other values are invalid control characters, skip them
				break;
			}
		}
		src++;
	}
}

void estrCopyValidXMLOnly(char **dst, const char *src)
{
	EString* estr;
	unsigned int srcLength;
	int iDstIdx = 0;

	if(!dst || !src)
		return;
	srcLength = (unsigned int)strlen(src);
	if (*dst)
	{
		estr = estrFromStr_sekret(*dst);
		if(estr->bufferLength < srcLength + EStrTerminatorSize)
		{
			estrReserveCapacity(dst, srcLength);
			estr = estrFromStr_sekret(*dst);
		}
	}
	else
	{
		estrReserveCapacity(dst, srcLength);
		estr = estrFromStr_sekret(*dst);
	}
	estr->stringLength = 0;

	while (*src)
	{
		unsigned char val = (unsigned char) *src;
		// Stupid check - only excludes invalid ASCII control characters
		// Doesn't actually validate multi-byte UTF characters
		if (val == 0x9 || val == 0xa || val == 0xd || val >= 0x20)
		{
			estr->str[iDstIdx++] = *src;
			estr->stringLength++;
		}
		src++;
	}
	estrTerminateString(estr);
}


//truncates an eString at the first occurrence of a given character
void estrTruncateAtFirstOccurrence(char **ppString, char c)
{
	char *pTemp;

	if (!ppString || !(*ppString))
	{
		return;
	}

	pTemp = strchr(*ppString, c);

	if (pTemp)
	{
		estrSetSize(ppString, pTemp - *ppString);
	}
}



void estrTruncateAtFirstWhitespace(char **ppString)
{
	int i;
	int iLen = estrLength(ppString);


	if (!ppString || !(*ppString))
	{
		return;
	}


	for (i=0; i < iLen; i++)
	{
		if (isWhitespace((*ppString)[i]))
		{
			estrSetSize(ppString, i);
			return;
		}
	}
}


void estrTruncateAtLastOccurrence(char **ppString, char c)
{
	char *pTemp;

	if (!ppString || !(*ppString))
	{
		return;
	}

	pTemp = strrchr(*ppString, c);

	if (pTemp)
	{
		estrSetSize(ppString, pTemp - *ppString);
	}
}

void estrTruncateAtNthOccurrence(char **ppString, char c, int n)
{
	int i;
	char *pStr = *ppString;
	if (!pStr)
		return;

	for (i=0; i < n; i++)
	{
		pStr = strchr(pStr, c);
		if (!pStr)
		{
			return;
		}

		pStr++;
	}

	estrSetSize(ppString, pStr - 1 - *ppString);
}


//counts the occurrences of character c in the string
int estrCountChars(char **ppString, char c)
{
	int iCount = 0;
	int i;
	int iSize;

	iSize = estrLength(ppString);

	if (!ppString || !*ppString)
	{
		return 0;
	}

	for (i=0; i < iSize; i++)
	{
		if ((*ppString)[i] == c)
		{
			iCount++;
		}
	}

	return iCount;
}


void estrMakeAllAlphaNumAndUnderscores(char **ppString)
{
	int iLen = estrLength(ppString);
	int i;

	if (!ppString || !*ppString)
	{
		return;
	}

	for (i=0; i < iLen; i++)
	{
		if (!isalnum((*ppString)[i]))
		{
			(*ppString)[i] = '_';
		}
	}
}

void estrTrimLeadingAndTrailingUnderscores(char **ppString)
{
	int iLeadingCount = 0;
	int iCount = 0;
	int iLength;

	while ((*ppString)[iCount] == '_')
	{
		iCount++;
	}

	if (iCount)
	{
		estrRemove(ppString, 0, iCount);
	}

	iLength = estrLength(ppString);
	iCount = 0;
	while (iCount < iLength && (*ppString)[iLength - iCount - 1] == '_')
	{
		iCount++;
	}

	if (iCount)
	{
		estrRemove(ppString, iLength - iCount, iCount);
	}
}


void estrMakeAllAlphaNumAndUnderscoresEx(char **ppString, char *pOKExtraChars)
{
	int iLen = estrLength(ppString);
	int i;

	if (!ppString || !*ppString)
	{
		return;
	}

	for (i=0; i < iLen; i++)
	{
		if (!isalnum((*ppString)[i]) && !strchr(pOKExtraChars, (*ppString)[i]))
		{
			(*ppString)[i] = '_';
		}
	}
}

void estrRemoveUpToFirstOccurrence(char **ppString, char c)
{
	char *pFound;

	if (!*ppString)
		return;
		
	pFound = strchr(*ppString, c);

	if (!pFound)
	{
		estrSetSize(ppString, 0);
		return;
	}

	estrRemove(ppString, 0, (pFound - *ppString) + 1);
}

void estrRemoveUpToLastOccurrence(char **ppString, char c)
{
	char *pFound;

	if (!*ppString)
		return;

	pFound = strrchr(*ppString, c);

	if (!pFound)
	{
		return;
	}

	estrRemove(ppString, 0, (pFound - *ppString) + 1);
}





void estrWordWrap_dbg(char **ppOutString, const char* pInString, int iCharsPerLine, const char *caller_fname, int line)
{
	int iCurLineCount = 0;
	const char *pLastBreakPoint = NULL;
	const char *pReadHead = pInString;

	if (!pReadHead)
	{
		return;
	}

	while (*pReadHead)
	{
		estrConcatChar_dbg(ppOutString, *pReadHead, 1, caller_fname, line);

		if (isWhitespace(*pReadHead))
		{
			pLastBreakPoint = pReadHead;
		}

		pReadHead++;
		iCurLineCount++;

		if (iCurLineCount >= iCharsPerLine)
		{
			if (pLastBreakPoint)
			{
				int iExtraCharsWritten = pReadHead - pLastBreakPoint;
				estrSetSize_dbg(ppOutString, estrLength(ppOutString) - iExtraCharsWritten, caller_fname, line);
				estrConcatString_dbg(ppOutString, "\n\r", 2, caller_fname, line);
				pReadHead = pLastBreakPoint + 1;
			}
			else
			{
				estrConcatString_dbg(ppOutString, "\n\r", 2, caller_fname, line);
			}

			iCurLineCount = 0;
			pLastBreakPoint = NULL;
		}
	}
}




void estrMakePrettyBytesString_dbg(char **ppOutString, U64 iNumBytes, const char *caller_fname, int line)
{
	if (iNumBytes < 1024)
	{
		estrPrintf_dbg(ppOutString, caller_fname, line, "%d bytes", (int)iNumBytes);
	}
	else if (iNumBytes < 1024 * 1024)
	{
		estrPrintf_dbg(ppOutString, caller_fname, line, "%.2f KB", ((float)iNumBytes) / 1024.0f );
	}
	else if (iNumBytes < 1024 * 1024 * 1024)
	{
		estrPrintf_dbg(ppOutString, caller_fname, line, "%.2f MB", ((float)iNumBytes) / (float)(1024 * 1024));
	}
	else
	{
		estrPrintf_dbg(ppOutString, caller_fname, line, "%.2f GB", ((float)iNumBytes) / (float)(1024 * 1024 * 1024));
	}
}


/*

AUTO_RUN_LATE;
void testSuperEscaping(void)
{
	char test1[1000];
	char *pTest2 = NULL;
	char *pTest3 = NULL;
	int i;

	for (i=0; i < 1000; i++)
	{
		test1[i] = (char)(i % 256);
		if (test1[i] == 0)
		{
			test1[i] = 1;
		}
	}

	test1[999] = 0;



	estrSuperEscapeString(&pTest2, test1);
	estrSuperUnescapeString(&pTest3, pTest2);

	assert(strcmp(test1, pTest3) == 0);


}

*/


/*
AUTO_RUN_LATE;
int testStack(void)
{
	char *str1, *str2, *str3;
	estrStackCreate(&str1,1000);
	estrStackCreate(&str2,1000);
	estrStackCreate(&str3,1000);

	estrDestroy(&str2);
	estrDestroy(&str1);
	estrDestroy(&str3);
	return 1;
}*/


char *estrDup_dbg(const char *pInStr, const char *pFileName, int iLineNum)
{
	char *pOutEstr = NULL;

	if (!pInStr)
	{
		return NULL;
	}

	estrCopy2_dbg(&pOutEstr, pInStr, pFileName, iLineNum);
	return pOutEstr;
}


char *estrDupAndTrim_dbg(const char *pInStr, const char *caller_fname, int line)
{
	char *pOutEString = estrDup_dbg(pInStr, caller_fname, line);
	estrTrimLeadingAndTrailingWhitespace(&pOutEString);
	return pOutEString;
}

// Duplicate a estring only if it's non-empty.
// This is like estrDup(), except that it folds "" to NULL.
char *estrDupIfNonempty(const char *pInStr)
{
	if (!pInStr || !*pInStr)
	{
		return NULL;
	}

	return estrDup(pInStr);
}

char *strDupFromEString(const char **ppString)
{
	int iLen;
	char *pOutBuf;

	if (!(*ppString))
	{
		return NULL;
	}

	iLen = estrLength(ppString);

	PERFINFO_AUTO_START_FUNC();
	pOutBuf = malloc(iLen + 1);
	PERFINFO_AUTO_STOP();

	if (iLen)
	{
		memcpy(pOutBuf, *ppString, iLen);
	}

	pOutBuf[iLen] = 0;
	return pOutBuf;
}

char *estrDupFromEString(const char **ppString)
{
	char *pOutEstr = NULL;

	if (!ppString || !*ppString)
	{
		return NULL;
	}

	estrCopy(&pOutEstr, ppString);
	return pOutEstr;
}

#define IS_NEWLINE(c) ((c) == '\n' || (c) == '\r')

void estrAddPrefixToLines_dbg(char **ppOutString, const char *pPrefix, const char *caller_fname, int line)
{
	int *pOffsets = NULL;
	int i;
	int iLen = estrLength(ppOutString);
	int iPrefixLen = (int)strlen(pPrefix);

	if (!(*ppOutString))
	{
		return;
	}

	ea32Push(&pOffsets, 0);

	for (i=1; i < iLen; i++)
	{
		if (IS_NEWLINE((*ppOutString)[i-1]) && !IS_NEWLINE((*ppOutString)[i]))
		{
			ea32Push(&pOffsets, i);
		}
	}

	for (i=ea32Size(&pOffsets) - 1; i >=0; i--)
	{
		estrInsert_dbg(ppOutString, pOffsets[i], pPrefix, iPrefixLen, caller_fname, line);
	}

	ea32Destroy(&pOffsets);
}

// Encodes contents of pBuffer into a base64 encoded string and appends it to ppOutString
void estrBase64Encode(char **ppOutString, const void *pBuffer, int iBufSize)
{
	int iSize;
	int iStartingSize = estrLength(ppOutString);
	int iRequiredSize = (iBufSize + 2) / 3 * 4 + 1;
	estrSetSize(ppOutString, iRequiredSize + iStartingSize);
	iSize = encodeBase64String(pBuffer, iBufSize, (*ppOutString) + iStartingSize, iRequiredSize);
	estrSetSize(ppOutString, iSize + iStartingSize);
}

// Decodes the base64 string in the buffer and appends the result to ppOutString
void estrBase64Decode(char **ppOutString, const void *pBuffer, int iBufSize)
{
	int iSize;
	char *decodeBuffer = alloca(iBufSize+1);
	iSize = decodeBase64String(pBuffer, iBufSize, decodeBuffer, iBufSize+1);
	if (iSize)
	{
		estrSetSize(ppOutString, iSize);
		memcpy(*ppOutString, decodeBuffer, iSize);
	}
	else
		Errorf("Base64 Decode buffer was too small");
}



void estrReplaceRangeWithString_dbg(char **ppString, int idx, int count, const char *pOtherString, const char *caller_fname, int line)
{
	int len = (int)strlen(pOtherString);
	estrRemove(ppString, idx, count);
	estrInsert_dbg(ppString, idx, pOtherString, len, caller_fname, line);
}

void estrRemoveCmdLineStyleArgIfPresent_dbg(char **ppString, const char *pCommand, const char *caller_fname, int line)
{
	int iCmdLen = (int)strlen(pCommand);
	int iSrcStrLen = (int)estrLength(ppString);
	char *pTemp = NULL;
	char *pFound;
	char *pNextDash;

	if (!ppString || !(*ppString))
	{
		return;
	}

	if (strEndsWith(*ppString, pCommand))
	{
		estrRemove(ppString, iSrcStrLen - iCmdLen, iCmdLen);
		return;
	}
	
	estrStackCreate(&pTemp);
	estrPrintf(&pTemp, "%s ", pCommand);

	pFound = strstri(*ppString, pTemp);

	if (!pFound)
	{
		estrDestroy(&pTemp);
		return;
	}

	pNextDash = strchr(pFound + 1, '-');

	if (pNextDash)
	{
		estrRemove(ppString, (pFound - *ppString), pNextDash - pFound);
	}
	else
	{
		estrRemove(ppString, (pFound - *ppString), iSrcStrLen - (pFound - *ppString));
	}

	estrDestroy(&pTemp);
}


void estrConcatSeparatedStringEarray_dbg(char **ppString, char ***pppEarray, const char *pSeparator, const char *caller_fname, int line)
{
	int i;

	for (i=0; i < eaSize(pppEarray); i++)
	{
		estrConcatf_dbg(ppString, caller_fname, line, "%s%s", i == 0 ? "" : pSeparator, (*pppEarray)[i]);
	}
}

void estrGetDirAndFileName(const char *pInString, char **ppOutDirName, char **ppOutFileName)
{
	int i;
	bool bFoundSlash = false;
	int iSrcLen = (int)strlen(pInString);

	for (i = iSrcLen - 1; i >= 0; i--)
	{
		if (pInString[i] == '/' || pInString[i] == '\\')
		{
			bFoundSlash = true;
			break;
		}
	}

	if (!bFoundSlash)
	{
		if (ppOutDirName)
		{
			estrCopy2(ppOutDirName, "");
		}

		if (ppOutFileName)
		{
			estrCopy2(ppOutFileName, pInString);
		}
		
		return;
	}

	if (ppOutDirName)
	{
		estrSetSize(ppOutDirName, i);
		memcpy(*ppOutDirName, pInString, i);
	}

	if (ppOutFileName)
	{
		estrSetSize(ppOutFileName, iSrcLen - i - 1);
		memcpy(*ppOutFileName, pInString + i + 1, iSrcLen - i - 1);
	}
}

void estrGetDirAndFileNameAndExtension(const char *pInString, char **ppOutDirName, char **ppOutFileName, char **ppOutExt)
{
	char *pDirName = NULL;
	char *pFullFileName = NULL;
	char *pLastDot;

	estrGetDirAndFileName(pInString, &pDirName, &pFullFileName);
	
	if (ppOutDirName)
	{
		estrCopy(ppOutDirName, &pDirName);
	}

	if (pFullFileName && (((pLastDot = strrchr(pFullFileName, '.')))))
	{
		if (ppOutFileName)
		{
			estrConcat(ppOutFileName, pFullFileName, pLastDot - pFullFileName);
		}

		if (ppOutExt)
		{
			estrConcat(ppOutExt, pLastDot + 1, estrLength(&pFullFileName) - (pLastDot - pFullFileName) - 1);
		}
	}
	else
	{
		if (ppOutFileName)
		{
			estrCopy(ppOutDirName, &pFullFileName);
		}
	}

	estrDestroy(&pDirName);
	estrDestroy(&pFullFileName);
}
		
		

#define MAX_COLUMNS_FOR_TAB_FIXUP 256

//obviously very inefficient to throw out ppWords and recalculate it twice. But doing otherwise
//would require an earray of earrays of strings. Madness!

void estrFixupTabsIntoSpacesAcrossMultipleLines_dbg(char **ppString, int iPadding, const char *caller_fname, int line)
{
	char **ppLines = NULL;
	int iMaxLengths[MAX_COLUMNS_FOR_TAB_FIXUP] = {0};
	int i, j;
	int iNumLines;
	DivideString(*ppString, "\n", &ppLines, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	iNumLines = eaSize(&ppLines);

	if (!iNumLines)
	{
		eaDestroyEx(&ppLines, NULL);
		return;
	}

	for (i=0; i < iNumLines; i++)
	{
		char **ppWords = NULL;
		int iNumWords;
		DivideString(ppLines[i], "\t", &ppWords, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
		iNumWords = eaSize(&ppWords);

		assert(iNumWords <= MAX_COLUMNS_FOR_TAB_FIXUP);

		if (iNumWords > 1)
		{
			for (j=0; j < iNumWords; j++)
			{
				int iLen = (int)strlen(ppWords[j]);
				if (iLen > iMaxLengths[j])
				{
					iMaxLengths[j] = iLen;
				}
			}
		}

		eaDestroyEx(&ppWords, NULL);
	}

	estrDestroy(ppString);

	for (i=0; i < iNumLines; i++)
	{
		char **ppWords = NULL;
		int iNumWords;
		
		if (i > 0)
		{
			estrConcatf(ppString, "\n");
		}

		DivideString(ppLines[i], "\t", &ppWords, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
		iNumWords = eaSize(&ppWords);

		if (iNumWords > 1)
		{
			estrConcatf(ppString, "%s", ppWords[0]);

			for (j=1; j < iNumWords; j++)
			{
				estrConcatCharCount(ppString, ' ', iMaxLengths[j-1] - (int)strlen(ppWords[j-1]) + iPadding);
				estrConcatf(ppString, "%s", ppWords[j]);
			}
		}
		else if (iNumWords == 1)
		{
			estrConcatf(ppString, "%s", ppWords[0]);
		}


		eaDestroyEx(&ppWords, NULL);
	}
	

	eaDestroyEx(&ppLines, NULL);
}


unsigned int estrInsertf_dbg(char** str, int iInsertByteIndex, const char *caller_fname, int line, const char* format, ...)
{
	char *pTemp = NULL;
	U32 iRetVal;
	estrStackCreate(&pTemp);
	estrGetVarArgs(&pTemp, format);

	estrInsert_dbg(str, iInsertByteIndex, pTemp, estrLength(&pTemp), caller_fname, line);

	iRetVal = estrLength(&pTemp);
	estrDestroy(&pTemp);
	return iRetVal;
}



void estrGetEnvironmentVariable_dbg(char **ppString, const char *pEnvVarName, const char *caller_fname, int line)
{
	GetEnvironmentVariable_UTF8_dbg(pEnvVarName, ppString, caller_fname, line);
}

void estrTokenize_dbg(char ***estrDest, const char *tokens, const char *src, SA_PARAM_NN_STR const char *caller_fname, int line)
{
	char *context = NULL;
	char *estrTokenBuffer = estrDup_dbg(src, caller_fname, line);
	char *pTok = strtok_s(estrTokenBuffer, tokens, &context);
	while(pTok != NULL)
	{
		eaPush(estrDest, estrDup(pTok));
		pTok = strtok_s(NULL, tokens, &context);
	}
	estrDestroy(&estrTokenBuffer);
}
