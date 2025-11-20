#include "UTF8.h"
#include "StringUtil.h"
#include "psapi.h"
#include "estring.h"
#include "scratchStack.h"
#include "wininclude.h"
#include "winbase.h"
#include "shlobj.h"
#include "stringCache.h"
#include "windowsx.h"
#include "share.h"
#include "memtrack.h"
#include <sys/stat.h>
#include "io.h"
#include "earray.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

WCHAR *UTF8_To_UTF16_malloc_dbg(const char *pIn MEM_DBG_PARMS)
{
	int iLen;
	WCHAR *pRetVal;
	int nChars;

	if (!pIn)
	{
		return NULL;
	}

	iLen = (int)strlen(pIn);
	pRetVal = _malloc_dbg(sizeof(WCHAR) * (iLen + 1), _NORMAL_BLOCK MEM_DBG_PARMS_CALL);
	
	if (!iLen)
	{
		*pRetVal = 0;
		return pRetVal;
	}

	nChars = UTF8ToWideStrConvert(pIn, pRetVal, iLen + 1);
	assert(nChars);

	return pRetVal;
}

WCHAR *UTF8_To_UTF16_DoubleTerminated_malloc_dbg(SA_PARAM_OP_STR const char *pIn MEM_DBG_PARMS)
{
	char **ppStrs = NULL;
	const char *pTemp = NULL;
	int iTotalLen = 2;
	WCHAR *pRetVal;
	WCHAR *pWriteHead;
	int i;

	if (!pIn)
	{
		return NULL;
	}

	pTemp = pIn;

	do
	{
		eaPush(&ppStrs, (char*)pTemp);
		iTotalLen += (int)strlen(pTemp) + 1;
		pTemp += strlen(pTemp) + 1;
	}
	while (*pTemp);

	pWriteHead = pRetVal = _malloc_dbg(sizeof(WCHAR) * (iTotalLen), _NORMAL_BLOCK MEM_DBG_PARMS_CALL);


	for (i = 0; i < eaSize(&ppStrs); i++)
	{
		int iCurLen = UTF8ToWideStrConvert(ppStrs[i], pWriteHead, iTotalLen) + 1;
		pWriteHead += iCurLen;
		iTotalLen -= iCurLen;
	}

	*pWriteHead = 0;
	eaDestroy(&ppStrs);

	return pRetVal;
}





typedef struct UTFStackAllocation
{
	WCHAR bHeapAllocation;
	WCHAR wideString[1];
} UTFStackAllocation;

//size of header, minus size of the stub wideString, plus sizeof string including null separator
#define UTFALLOCSIZE(iLen) ( sizeof(UTFStackAllocation) - sizeof(WCHAR) + sizeof(WCHAR) * (iLen + 1))

#define UTF8_To_UTF16_Stack(pIn, pOut) \
{ UTFStackAllocation *_pAllocation; \
if (!pIn) pOut = NULL; else { \
	int _iLen = (int)strlen(pIn) + 1; \
	if (_iLen > 8000) { _pAllocation = malloc(UTFALLOCSIZE(_iLen)); _pAllocation->bHeapAllocation = 1; } \
	else { _pAllocation = alloca(UTFALLOCSIZE(_iLen)); _pAllocation->bHeapAllocation = 0; } \
	pOut = _pAllocation->wideString; \
	if (_iLen == 1) { _pAllocation->wideString[0] = 0; } else \
	{ int _iChars = UTF8ToWideStrConvert(pIn, _pAllocation->wideString, _iLen); assert(_iChars); }}}

#define UTF8_To_UTF16_Stack_Verbose(pIn, pOut) \
{ UTFStackAllocation *_pAllocation; \
printf("Converting %s\n", pIn);	\
if (!pIn) pOut = NULL; else { \
	int _iLen = (int)strlen(pIn) + 1; \
	printf("Len: %d\n", _iLen); \
	printf("Allocation size: %d\n", UTFALLOCSIZE(_iLen)); \
	if (_iLen > 8000) { _pAllocation = malloc(UTFALLOCSIZE(_iLen)); _pAllocation->bHeapAllocation = 1; } \
	else { _pAllocation = alloca(UTFALLOCSIZE(_iLen)); _pAllocation->bHeapAllocation = 0; } \
	pOut = _pAllocation->wideString; \
	if (_iLen == 1) { _pAllocation->wideString[0] = 0; } else \
	{ int _iChars = UTF8ToWideStrConvert_Verbose(pIn, _pAllocation->wideString, _iLen); assert(_iChars); }}}


#define UTF8_To_UTF16_Stack_GetSize(pIn, pOut, outSize) \
{ UTFStackAllocation *_pAllocation; \
if (!pIn) pOut = NULL; else { \
	int _iLen = (int)strlen(pIn) + 1; \
	if (_iLen > 8000) { _pAllocation = malloc(UTFALLOCSIZE(_iLen)); _pAllocation->bHeapAllocation = 1; } \
	else { _pAllocation = alloca(UTFALLOCSIZE(_iLen)); _pAllocation->bHeapAllocation = 0; } \
	pOut = _pAllocation->wideString; \
	if (_iLen == 1) { _pAllocation->wideString[0] = 0; } else \
	{ outSize = UTF8ToWideStrConvert(pIn, _pAllocation->wideString, _iLen); assert(outSize); }}}

#define UTF8_With_Size_To_UTF16_Stack_GetSize(pIn, inLen, pOut, outSize) \
{ UTFStackAllocation *_pAllocation; \
if (!pIn) pOut = NULL; else { \
	if (inLen > 8000) { _pAllocation = malloc(UTFALLOCSIZE(inLen)); _pAllocation->bHeapAllocation = 1; } \
	else { _pAllocation = alloca(UTFALLOCSIZE(inLen)); _pAllocation->bHeapAllocation = 0; } \
	pOut = _pAllocation->wideString; \
	if (!inLen) { _pAllocation->wideString[0] = 0; } else \
	{ outSize = UTF8ToWideStrConvert(pIn, _pAllocation->wideString, inLen); assert(outSize); }}}


#define UTFStackFree(pBuff) { if (pBuff) { UTFStackAllocation *_pAllocation = (UTFStackAllocation*)(((char*)pBuff) - sizeof(WCHAR)); if (_pAllocation->bHeapAllocation) free(_pAllocation); }}



//the size does NOT include the null terminator. If the size is zero, it will check for it, but
//it's faster not to have to
void UTF16ToEstring_dbg(const WCHAR *pBuff, int iSize, char **ppOutEString MEM_DBG_PARMS)
{
	int iBytes;
	int iNewBuffBytes;

	if (!pBuff)
	{
		estrClear(ppOutEString);
		return;
	}

	if (iSize == 0)
	{
		iSize = (int)wcslen(pBuff);
	}

	iNewBuffBytes = (iSize * 3) + 1;

	estrReserveCapacity_dbg(ppOutEString, iNewBuffBytes MEM_DBG_PARMS_CALL);
	iBytes = WideToUTF8StrConvert(pBuff, *ppOutEString, iNewBuffBytes);
	estrForceSize_dbg(ppOutEString, iBytes MEM_DBG_PARMS_CALL);
}

//special version that does only things that can be done very early in startup
char *UTF16_to_UTF8_CommandLine(const S16 *pIn)
{
	size_t iInLen;
	size_t iOutLen;
	char *pOutBuf;
	int bytesWritten;

	if (!pIn)
	{
		return NULL;
	}

	iInLen = wcslen(pIn);
	iOutLen = (iInLen * 3) + 1;

	pOutBuf = malloc(iOutLen);
	bytesWritten = WideToUTF8StrConvert(pIn, pOutBuf, (int)iOutLen);

	return realloc(pOutBuf, bytesWritten + 1);
}


const char *allocAddString_UTF16ToUTF8(const WCHAR *pIn)
{
	char *pTemp = NULL;
	const char *pRetVal = NULL;
	estrStackCreate(&pTemp);
	UTF16ToEstring(pIn ? pIn : L"", 0, &pTemp);
	pRetVal = allocAddString(pTemp);
	estrDestroy(&pTemp);
	return pRetVal;
}

char *strdup_UTF16ToUTF8_dbg(const WCHAR *pIn MEM_DBG_PARMS)
{
	char *pTemp = NULL;
	char *pRetVal = NULL;
	estrStackCreate(&pTemp);
	UTF16ToEstring(pIn ? pIn : L"", 0, &pTemp);

	pRetVal = _malloc_dbg(estrLength(&pTemp) + 1, _NORMAL_BLOCK MEM_DBG_PARMS_CALL);
	memcpy(pRetVal, pTemp, estrLength(&pTemp) + 1);
	estrDestroy(&pTemp);
	return pRetVal;
}


DWORD GetEnvironmentVariable_UTF8_dbg(const char *pName, char **ppOutEString MEM_DBG_PARMS)
{
	WCHAR defaultBuff[1024];
	WCHAR *pWideName;
	DWORD iRetVal;

	UTF8_To_UTF16_Stack(pName, pWideName);
	
	iRetVal = GetEnvironmentVariable(pWideName, defaultBuff, ARRAY_SIZE(defaultBuff));

	if (iRetVal >= ARRAY_SIZE(defaultBuff))
	{
		WCHAR *pAllocedBuff = ScratchAlloc(iRetVal * sizeof(WCHAR));
		iRetVal = GetEnvironmentVariable(pWideName, pAllocedBuff, iRetVal);

		UTF16ToEstring_dbg(pAllocedBuff, iRetVal, ppOutEString MEM_DBG_PARMS_CALL);

		UTFStackFree(pWideName);
		ScratchFree(pAllocedBuff);
		return iRetVal;
	}
	
	UTF16ToEstring_dbg(defaultBuff, iRetVal, ppOutEString MEM_DBG_PARMS_CALL);
	UTFStackFree(pWideName);
	return iRetVal;
}


/*FormatMessage does this weird thing where if you pass in FORMAT_MESSAGE_ALLOCATE_BUFFER, it allocates the buffer
for you with LocalAlloc... we take advantage of that rather than dicking around with trying to guess a large enough size, etc*/
DWORD FormatMessage_UTF8_dbg(DWORD dwFlags,
  LPCVOID lpSource,
  DWORD dwMessageId,
  DWORD dwLanguageId,
  char **ppOutMessage MEM_DBG_PARMS,
  va_list *Arguments)
{
	WCHAR *pLocalBuffer = NULL;
	DWORD retVal;

	retVal = FormatMessage(dwFlags | FORMAT_MESSAGE_ALLOCATE_BUFFER, lpSource, dwMessageId, dwLanguageId, 
		(WCHAR*)(&pLocalBuffer), 0, Arguments);

	if (!retVal)
	{
		SAFE_FREE(pLocalBuffer);
		return 0;
	}

	UTF16ToEstring_dbg(pLocalBuffer, retVal, ppOutMessage MEM_DBG_PARMS_CALL);
	LocalFree(pLocalBuffer);

	return retVal;
}

UINT GetTempFileName_UTF8_dbg(const char *pPathName, const char *pPrefixString, UINT uUnique, char **ppOutTempFileName MEM_DBG_PARMS)
{
	UINT retVal;
	WCHAR wideBuff[MAX_PATH];
	WCHAR *pWidePathName = NULL;
	WCHAR *pWidePrefixString = NULL;


	UTF8_To_UTF16_Stack(pPathName, pWidePathName);
	UTF8_To_UTF16_Stack(pPrefixString, pWidePrefixString);


	retVal = GetTempFileName(pWidePathName, pWidePrefixString, uUnique, wideBuff);

	UTFStackFree(pWidePrefixString);
	UTFStackFree(pWidePathName);

	UTF16ToEstring_dbg(wideBuff, 0, ppOutTempFileName MEM_DBG_PARMS_CALL);

	return retVal;
}


int GetLocaleInfo_UTF8_dbg(LCID Locale, LCTYPE LCType, char **ppOutData MEM_DBG_PARMS)
{
	WCHAR wideBuff[1024];
	int iRetVal = GetLocaleInfo(Locale, LCType, wideBuff, ARRAY_SIZE(wideBuff));

	if (!iRetVal && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		int iNeededSize = GetLocaleInfo(Locale, LCType, NULL, 0);
		WCHAR *pTempWideBuff = ScratchAlloc((iNeededSize + 1) * sizeof(WCHAR));
		iRetVal = GetLocaleInfo(Locale, LCType, pTempWideBuff, iNeededSize + 1);
		if (!iRetVal)
		{
			ScratchFree(pTempWideBuff);
			return 0;
		}

		UTF16ToEstring_dbg(pTempWideBuff, 0, ppOutData MEM_DBG_PARMS_CALL);
		ScratchFree(pTempWideBuff);
		return iRetVal;
	}

	UTF16ToEstring_dbg(wideBuff, 0, ppOutData MEM_DBG_PARMS_CALL);

	return iRetVal;

}

int GetWindowText_UTF8_dbg(HWND hWnd, char **ppOutString MEM_DBG_PARMS)
{
	size_t iSize = GetWindowTextLength(hWnd);
	WCHAR *pWideBuff;

	if (!iSize)
	{
		estrCopy2(ppOutString, "");
		return 0;
	}

	pWideBuff = ScratchAlloc((iSize + 1) * sizeof(WCHAR));

	GetWindowText(hWnd, pWideBuff, (int)(iSize + 1));

	UTF16ToEstring_dbg(pWideBuff, (int)iSize, ppOutString MEM_DBG_PARMS_CALL);

	ScratchFree(pWideBuff);

	return estrLength(ppOutString);
}


void OutputDebugString_UTF8(const char *pBuf)
{
	WCHAR *pWideBuff;
	
	UTF8_To_UTF16_Stack(pBuf, pWideBuff);
	OutputDebugString(pWideBuff);
	UTFStackFree(pWideBuff);
}

HANDLE CreateMutex_UTF8(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, const char *pName)
{
	WCHAR *pWideName;
	HANDLE retVal;
	UTF8_To_UTF16_Stack(pName, pWideName);
	retVal = CreateMutex(lpMutexAttributes, bInitialOwner, pWideName);
	UTFStackFree(pWideName);
	return retVal;
}

HANDLE CreateSemaphore_UTF8(
    LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    LONG lInitialCount,
    LONG lMaximumCount,
    const char *pName)
{
	WCHAR *pWideName;
	HANDLE retVal;
	UTF8_To_UTF16_Stack(pName, pWideName);
	retVal = CreateSemaphore(lpSemaphoreAttributes, lInitialCount, lMaximumCount, pWideName);
	UTFStackFree(pWideName);
	return retVal;
}

void SetConsoleTitle_UTF8(const char *pTitle)
{
	WCHAR *pWideName;
	assert(pTitle);
	UTF8_To_UTF16_Stack(pTitle, pWideName);
	SetConsoleTitle(pWideName);
	UTFStackFree(pWideName);
}

HANDLE CreateFile_UTF8(
	const char *pFileName,
	DWORD dwDesiredAccess,
	DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes,
	HANDLE hTemplateFile)
{
	HANDLE retVal;
	WCHAR *pWideName;

	assert(pFileName);

	UTF8_To_UTF16_Stack(pFileName, pWideName);
	retVal = CreateFile(pWideName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition,
		dwFlagsAndAttributes, hTemplateFile);
	UTFStackFree(pWideName);
	return retVal;
}


BOOL WriteConsoleOutputCharacter_UTF8(
  HANDLE hConsoleOutput,
  char *pInStr,
  DWORD nLength,
  COORD dwWriteCoord,
  LPDWORD lpNumberOfCharsWritten)
{
	WCHAR *pWideStr;
	BOOL retVal;

	assert(pInStr);

	UTF8_To_UTF16_Stack(pInStr, pWideStr);
	retVal = WriteConsoleOutputCharacter(hConsoleOutput, pWideStr, nLength, dwWriteCoord, lpNumberOfCharsWritten);
	UTFStackFree(pWideStr);
	return retVal;
}



BOOL SetWindowText_UTF8(HWND hWnd, const char *pText)
{
	BOOL retVal;
	WCHAR *pWideStr;
	UTF8_To_UTF16_Stack(pText, pWideStr);
	retVal = SetWindowText(hWnd, pWideStr);
	UTFStackFree(pWideStr);
	return retVal;
}

HMODULE LoadLibrary_UTF8(const char *pName)
{
	HMODULE retVal;
	WCHAR *pWideStr;

	assert(pName);

	UTF8_To_UTF16_Stack(pName, pWideStr);
	retVal = LoadLibrary(pWideStr);
	UTFStackFree(pWideStr);
	return retVal;
}

BOOL DeleteFile_UTF8(const char *pName)
{
	BOOL retVal;
	WCHAR *pWideStr;

	assert(pName);

	UTF8_To_UTF16_Stack(pName, pWideStr);
	retVal = DeleteFile(pWideStr);
	UTFStackFree(pWideStr);
	return retVal;
}


HINSTANCE ShellExecute_UTF8(
  HWND hwnd,
  const char *pOperation,
  const char *pFile,
  const char *pParameters,
  const char *pDirectory,
  INT nShowCmd)
{
	HINSTANCE retVal;
	WCHAR *pWideOperation;
	WCHAR *pWideFile;
	WCHAR *pWideParameters;
	WCHAR *pWideDirectory;

	assert(pFile);

	UTF8_To_UTF16_Stack(pOperation, pWideOperation);
	UTF8_To_UTF16_Stack(pFile, pWideFile);
	UTF8_To_UTF16_Stack(pParameters, pWideParameters);
	UTF8_To_UTF16_Stack(pDirectory, pWideDirectory);

	retVal = ShellExecute(hwnd, pWideOperation, pWideFile, pWideParameters, pWideDirectory, nShowCmd);

	UTFStackFree(pWideDirectory);
	UTFStackFree(pWideParameters);
	UTFStackFree(pWideFile);
	UTFStackFree(pWideOperation);

	return retVal;
}

BOOL LookupPrivilegeValue_UTF8(
  const char *pSystemName,
  const char *pName,
  PLUID lpLuid)
{
	BOOL retVal;
	WCHAR *pWideSystemName;
	WCHAR *pWideName;


	UTF8_To_UTF16_Stack(pSystemName, pWideSystemName);
	UTF8_To_UTF16_Stack(pName, pWideName);

	assert(pName);

	retVal = LookupPrivilegeValue(pWideSystemName, pWideName, lpLuid);

	UTFStackFree(pWideName);
	UTFStackFree(pWideSystemName);

	return retVal;


}


BOOL WaitNamedPipe_UTF8(
  const char *pPipeName,
  DWORD nTimeOut
)
{
	BOOL retVal;
	WCHAR *pWideName;
	assert(pPipeName);
	UTF8_To_UTF16_Stack(pPipeName, pWideName);
	retVal = WaitNamedPipe(pWideName, nTimeOut);
	UTFStackFree(pWideName);

	return retVal;
}

HANDLE CreateNamedPipe_UTF8(
	const char *pName,
	DWORD dwOpenMode,
	DWORD dwPipeMode,
	DWORD nMaxInstances,
	DWORD nOutBufferSize,
	DWORD nInBufferSize,
	DWORD nDefaultTimeOut,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes
)
{
	HANDLE retVal;
	WCHAR *pWideName;
	assert(pName);
	UTF8_To_UTF16_Stack(pName, pWideName);
	retVal = CreateNamedPipe(pWideName, dwOpenMode, dwPipeMode, nMaxInstances, nOutBufferSize, nInBufferSize, 
		nDefaultTimeOut, lpSecurityAttributes);
	UTFStackFree(pWideName);
	return retVal;
}



LONG
RegOpenKeyEx_UTF8 (
    __in HKEY hKey,
    __in_opt const char *pSubKey,
    __in_opt DWORD ulOptions,
    __in REGSAM samDesired,
    __out PHKEY phkResult
    )
{
	LONG retVal;
	WCHAR *pWide = NULL;
	UTF8_To_UTF16_Stack(pSubKey, pWide);
	retVal = RegOpenKeyEx(hKey, pWide, ulOptions, samDesired, phkResult);
	UTFStackFree(pWide);
	return retVal;
}


LONG
RegCreateKeyEx_UTF8 (
    __in HKEY hKey,
    __in const char *pSubKey,
    __reserved DWORD Reserved,
    __in_opt char *pClass,
    __in DWORD dwOptions,
    __in REGSAM samDesired,
    __in_opt CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    __out PHKEY phkResult,
    __out_opt LPDWORD lpdwDisposition
    )
{
	LONG retVal;
	WCHAR *pWideSubKey = NULL;
	WCHAR *pWideClass = NULL;
	UTF8_To_UTF16_Stack(pSubKey, pWideSubKey);
	UTF8_To_UTF16_Stack(pClass, pWideClass);
	retVal = RegCreateKeyEx ( hKey, pWideSubKey,  Reserved,  pWideClass,  dwOptions,  samDesired,  lpSecurityAttributes, phkResult, lpdwDisposition);
	UTFStackFree(pWideClass);
	UTFStackFree(pWideSubKey);
	return retVal;
}



LONG
RegQueryValueEx_UTF8 (
    __in HKEY hKey,
    __in_opt const char *pValueName,
    __reserved LPDWORD lpReserved,
    __out_opt LPDWORD lpType,
    __out_bcount_part_opt(*lpcbData, *lpcbData) __out_data_source(REGISTRY) LPBYTE lpData,
    __inout_opt LPDWORD lpcbData
    )
{
	LONG retVal;
	WCHAR *pWide = NULL;
	UTF8_To_UTF16_Stack(pValueName, pWide);
	retVal = RegQueryValueEx(hKey, pWide, lpReserved, lpType, lpData, lpcbData);
	UTFStackFree(pWide);
	return retVal;

}

LONG
RegSetValueEx_UTF8 (
    __in HKEY hKey,
    __in_opt const char *pValueName,
    __reserved DWORD Reserved,
    __in DWORD dwType,
    __in_bcount_opt(cbData) CONST BYTE* lpData,
    __in DWORD cbData
    )
{
	LONG retVal;
	WCHAR *pWide = NULL;
	UTF8_To_UTF16_Stack(pValueName, pWide);
	retVal = RegSetValueEx(hKey, pWide, Reserved, dwType, lpData, cbData);
	UTFStackFree(pWide);
	return retVal;
}

LONG
RegDeleteValue_UTF8 (
    __in HKEY hKey,
    __in_opt const char *pValueName
    )
{
	LONG retVal;
	WCHAR *pWide = NULL;
	UTF8_To_UTF16_Stack(pValueName, pWide);
	retVal = RegDeleteValue(hKey, pWide);
	UTFStackFree(pWide);
	return retVal;
}

LONG
RegDeleteKey_UTF8 (
    __in HKEY hKey,
    __in const char *pSubKey
    )
{
	LONG retVal;
	WCHAR *pWide = NULL;
	UTF8_To_UTF16_Stack(pSubKey, pWide);
	retVal = RegDeleteKey(hKey, pWide);
	UTFStackFree(pWide);
	return retVal;

}


LONG
RegEnumValue_UTF8 (
    __in HKEY hKey,
    __in DWORD dwIndex,
    char *lpValueName,
    __inout LPDWORD lpcchValueName,
    __reserved LPDWORD lpReserved,
    __out_opt LPDWORD lpType,
    __out_bcount_part_opt(*lpcbData, *lpcbData) __out_data_source(REGISTRY) LPBYTE lpData,
    __inout_opt LPDWORD lpcbData
    )
{
	LONG retVal;
	WCHAR *pWideValueName = NULL;
	UTF8_To_UTF16_Stack(lpValueName, pWideValueName);
	retVal = RegEnumValue(hKey,dwIndex,
		pWideValueName, lpcchValueName, lpReserved, lpType,
		lpData, lpcbData);
	UTFStackFree(pWideValueName);
	return retVal;


}


LONG
RegEnumKeyEx_UTF8(
    __in HKEY hKey,
    __in DWORD dwIndex,
    char *lpName,
    __inout LPDWORD lpcchName,
    __reserved LPDWORD lpReserved,
    __out_ecount_part_opt(*lpcchClass,*lpcchClass + 1) LPWSTR lpClass,
    __inout_opt LPDWORD lpcchClass,
    __out_opt PFILETIME lpftLastWriteTime
    )
{
	LONG retVal;
	WCHAR *pWideOutBuf = (WCHAR*)malloc(sizeof(WCHAR) * (*lpcchName));

	retVal = RegEnumKeyEx(hKey,
		dwIndex,
		pWideOutBuf,
		lpcchName,
		lpReserved,
		lpClass,
		lpcchClass,
		lpftLastWriteTime);

	if (retVal == ERROR_SUCCESS)
	{
		*lpcchName = WideToUTF8StrConvert(pWideOutBuf, lpName, *lpcchName);
	}

	free(pWideOutBuf);

	return retVal;

	



}


BOOL
CreateProcess_UTF8(
    __in_opt    const char *pAppName,
    __in_opt	const char *pCommandLine,
    __in_opt    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    __in_opt    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    __in        BOOL bInheritHandles,
    __in        DWORD dwCreationFlags,
    __in_opt    LPVOID lpEnvironment,
    __in_opt    const char *pCurDirectory,
    __in        LPSTARTUPINFOW lpStartupInfo,
    __out       LPPROCESS_INFORMATION lpProcessInformation
    )
{
	LONG retVal;
	WCHAR *pWideAppName = NULL;
	WCHAR *pWideCommandLine = NULL;
	WCHAR *pWideDirectory = NULL;
	UTF8_To_UTF16_Stack(pAppName, pWideAppName);
	UTF8_To_UTF16_Stack(pCommandLine, pWideCommandLine);
	UTF8_To_UTF16_Stack(pCurDirectory, pWideDirectory);

	retVal = CreateProcess(pWideAppName, pWideCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles,
		dwCreationFlags, lpEnvironment, pWideDirectory, lpStartupInfo, lpProcessInformation);

	UTFStackFree(pWideDirectory);
	UTFStackFree(pWideCommandLine);
	UTFStackFree(pWideAppName);

	return retVal;


}

int
MessageBox_UTF8(
    __in_opt HWND hWnd,
    __in_opt const char *pText,
    __in_opt const char *pCaption,
    __in UINT uType)
{
	int retVal;
	WCHAR *pWideText = NULL;
	WCHAR *pWideCaption = NULL;

	UTF8_To_UTF16_Stack(pText, pWideText);
	UTF8_To_UTF16_Stack(pCaption, pWideCaption);

	retVal = MessageBox(hWnd, pWideText, pWideCaption, uType);

	UTFStackFree(pWideCaption);
	UTFStackFree(pWideText);

	return retVal;
}


int GetCurrentDirectory_UTF8_dbg(char **ppOutDir MEM_DBG_PARMS)
{
	WCHAR sBuf[MAX_PATH + 10] = {0};
	int iSize = GetCurrentDirectory(MAX_PATH + 10, sBuf);

	if (!iSize)
	{
		return 0;
	}

	assert(iSize < MAX_PATH + 10);
	UTF16ToEstring_dbg(sBuf, iSize, ppOutDir MEM_DBG_PARMS_CALL);
	
	return estrLength(ppOutDir);
}

DWORD GetModuleFileNameEx_UTF8_dbg(_In_ HANDLE hProcess, 
	_In_opt_ HMODULE hModule, char **ppOutEString MEM_DBG_PARMS)
{
	WCHAR sBuf[MAX_PATH + 10];
	DWORD retVal = GetModuleFileNameEx(hProcess, hModule, SAFESTR(sBuf));

	if (retVal == 0)
	{
		return 0;
	}

	assert(retVal < ARRAY_SIZE(sBuf) - 1);

	UTF16ToEstring_dbg(sBuf, retVal, ppOutEString MEM_DBG_PARMS_CALL);
	
	return estrLength(ppOutEString);
}

DWORD GetModuleBaseName_UTF8_dbg(_In_ HANDLE hProcess, 
	_In_opt_ HMODULE hModule, char **ppOutEString MEM_DBG_PARMS)
{
	WCHAR sBuf[MAX_PATH + 10];
	DWORD retVal = GetModuleBaseName(hProcess, hModule, SAFESTR(sBuf));

	if (retVal == 0)
	{
		return 0;
	}

	assert(retVal < ARRAY_SIZE(sBuf) - 1);

	UTF16ToEstring_dbg(sBuf, retVal, ppOutEString MEM_DBG_PARMS_CALL);
	
	return estrLength(ppOutEString);
}

DWORD GetModuleFileName_UTF8_dbg(__in_opt HMODULE hModule, char **ppOutEString MEM_DBG_PARMS)
{
	WCHAR sBuf[MAX_PATH + 10];
	DWORD retVal = GetModuleFileName(hModule, SAFESTR(sBuf));

	if (retVal == 0)
	{
		return 0;
	}

	assert(retVal < ARRAY_SIZE(sBuf) - 1);

	UTF16ToEstring_dbg(sBuf, retVal, ppOutEString MEM_DBG_PARMS_CALL);
	
	return estrLength(ppOutEString);
}

BOOL GetVolumeInformation_UTF8_dbg(
    char *pRootPathName,
    char **ppOutVolumeName, //can be NULL
    __out_opt LPDWORD lpVolumeSerialNumber,
    __out_opt LPDWORD lpMaximumComponentLength,
    __out_opt LPDWORD lpFileSystemFlags,
    char **ppOutFileSystemName // can be NULL
	MEM_DBG_PARMS)
{
	WCHAR *pWideRootPathName = NULL;
	WCHAR volumeNameBuf[MAX_PATH + 10];
	WCHAR fileSystemNameBuf[MAX_PATH + 10];
	BOOL bRetVal;

	UTF8_To_UTF16_Stack(pRootPathName, pWideRootPathName);

	bRetVal = GetVolumeInformation(pWideRootPathName, volumeNameBuf, ARRAY_SIZE(volumeNameBuf),
		lpVolumeSerialNumber, lpMaximumComponentLength, lpFileSystemFlags,
		fileSystemNameBuf, ARRAY_SIZE(fileSystemNameBuf));

	if (!bRetVal)
	{
		UTFStackFree(pRootPathName);
		return 0;
	}

	if (ppOutVolumeName)
	{
		UTF16ToEstring_dbg(volumeNameBuf, 0, ppOutVolumeName MEM_DBG_PARMS_CALL);
	}

	if (ppOutFileSystemName)
	{
		UTF16ToEstring_dbg(fileSystemNameBuf, 0, ppOutFileSystemName MEM_DBG_PARMS_CALL);
	}


	UTFStackFree(pRootPathName);
	return bRetVal;
}

UINT GetSystemDirectory_UTF8_dbg(char **ppOutName MEM_DBG_PARMS)
{
	UINT retVal;
	WCHAR buf[MAX_PATH + 10];

	retVal = GetSystemDirectory(buf, ARRAY_SIZE(buf));

	if (!retVal)
	{
		return 0;
	}

	assert(retVal < ARRAY_SIZE(buf) - 1);

	UTF16ToEstring_dbg(buf, retVal, ppOutName MEM_DBG_PARMS_CALL);

	return estrLength(ppOutName);
}


__out_opt
HRSRC
FindResource_UTF8(
    __in_opt HMODULE hModule,
    __in     const char *pName,
    __in     const char *pType
    )
{
	WCHAR *pWideName = NULL;
	WCHAR *pWideType = NULL;
	HRSRC retVal;

	UTF8_To_UTF16_Stack(pName, pWideName);
	UTF8_To_UTF16_Stack(pType, pWideType);

	retVal = FindResource(hModule, pWideName, pWideType);

	UTFStackFree(pWideType);
	UTFStackFree(pWideName);

	return retVal;
}

UINT
GetDlgItemText_UTF8_dbg(
    __in HWND hDlg,
    __in int nIDDlgItem,
    char **ppOutString MEM_DBG_PARMS)
{
	UINT retVal;
	WCHAR startingBuf[256];
	
	size_t actualLen;
	WCHAR *pActualBuffer;

	retVal = GetDlgItemText(hDlg, nIDDlgItem, startingBuf, ARRAY_SIZE(startingBuf));

	if (retVal < ARRAY_SIZE(startingBuf) - 1)
	{
		UTF16ToEstring_dbg(startingBuf, retVal, ppOutString MEM_DBG_PARMS_CALL);
		return estrLength(ppOutString);
	}

	actualLen = GetWindowTextLength(GetDlgItem(hDlg, nIDDlgItem));
	pActualBuffer = malloc(sizeof(WCHAR) * (actualLen + 1));

	retVal = GetDlgItemText(hDlg, nIDDlgItem, pActualBuffer, (int)actualLen);

	assert(retVal <= actualLen);

	UTF16ToEstring_dbg(pActualBuffer, retVal, ppOutString MEM_DBG_PARMS_CALL);

	free(pActualBuffer);

	return estrLength(ppOutString);
}

DWORD
GetFileAttributes_UTF8(
    __in const char *pFileName
    )
{
	DWORD retVal;
	WCHAR *pWideFileName = NULL;

	UTF8_To_UTF16_Stack(pFileName, pWideFileName);
	
	retVal = GetFileAttributes(pWideFileName);

	UTFStackFree(pWideFileName);

	return retVal;
}

__out
HANDLE
FindFirstFileEx_UTF8(
    __in       const char *pFileName,
    __in       FINDEX_INFO_LEVELS fInfoLevelId,
    __out      LPVOID lpFindFileData,
    __in       FINDEX_SEARCH_OPS fSearchOp,
    __reserved LPVOID lpSearchFilter,
    __in       DWORD dwAdditionalFlags
    )
{
	HANDLE retVal;
	WCHAR *pWideFileName = NULL;

	UTF8_To_UTF16_Stack(pFileName, pWideFileName);
	
	retVal = FindFirstFileEx(pWideFileName, fInfoLevelId, lpFindFileData, fSearchOp, lpSearchFilter, dwAdditionalFlags);

	UTFStackFree(pWideFileName);

	return retVal;
}

UINT
GetDriveType_UTF8(
    __in_opt const char *pRootPathName
    )
{
	UINT retVal;
	WCHAR *pWideName = NULL;
	UTF8_To_UTF16_Stack(pRootPathName, pWideName);

	retVal = GetDriveType(pWideName);

	UTFStackFree(pWideName);

	return retVal;
}

DWORD
GetFileVersionInfoSize_UTF8(
        __in const char *pFileName, /* Filename of version stamped file */
        __out_opt LPDWORD lpdwHandle       /* Information for use by GetFileVersionInfo */
        )
{
	DWORD retVal;
	WCHAR *pWideFileName = NULL;
	UTF8_To_UTF16_Stack(pFileName, pWideFileName);

	retVal = GetFileVersionInfoSize(pWideFileName, lpdwHandle);

	UTFStackFree(pWideFileName);

	return retVal;
}

BOOL
GetFileVersionInfo_UTF8(
        __in                const char *pFileName, /* Filename of version stamped file */
        __reserved          DWORD dwHandle,          /* Information from GetFileVersionSize */
        __in                DWORD dwLen,             /* Length of buffer for info */
        __out_bcount(dwLen) LPVOID lpData            /* Buffer to place the data structure */
        )
{
	BOOL retVal;
	WCHAR *pWideFileName = NULL;
	UTF8_To_UTF16_Stack(pFileName, pWideFileName);

	retVal = GetFileVersionInfo(pWideFileName, dwHandle, dwLen,  lpData);

	UTFStackFree(pWideFileName);

	return retVal;
}

BOOL
VerQueryValue_UTF8(
        __in LPCVOID pBlock,
        __in const char *pSubBlock,
        __deref_out_xcount("buffer can be PWSTR or DWORD*") LPVOID * lplpBuffer,
        __out PUINT puLen
        )
{
	BOOL retVal;
	WCHAR *pWideSubBlock = NULL;
	UTF8_To_UTF16_Stack(pSubBlock, pWideSubBlock);

	retVal = VerQueryValue(pBlock, pWideSubBlock, lplpBuffer,  puLen);

	UTFStackFree(pWideSubBlock);

	return retVal;
}

HWND
FindWindow_UTF8(
    __in_opt const char *pClassName,
    __in_opt const char *pWindowName)
{
	WCHAR *pWideClassName = NULL;
	WCHAR *pWideWindowName = NULL;
	HWND retVal;

	UTF8_To_UTF16_Stack(pClassName, pWideClassName);
	UTF8_To_UTF16_Stack(pWindowName, pWideWindowName);

	retVal = FindWindow(pWideClassName, pWideWindowName);

	UTFStackFree(pWideWindowName);
	UTFStackFree(pWideClassName);

	return retVal;
}

__out_opt
HANDLE
CreateFileMapping_UTF8(
    __in     HANDLE hFile,
    __in_opt LPSECURITY_ATTRIBUTES lpFileMappingAttributes,
    __in     DWORD flProtect,
    __in     DWORD dwMaximumSizeHigh,
    __in     DWORD dwMaximumSizeLow,
    __in_opt const char *pName
    )
{

	WCHAR *pWideName = NULL;
	HANDLE retVal;

	UTF8_To_UTF16_Stack(pName, pWideName);

	retVal = CreateFileMapping(hFile, lpFileMappingAttributes, flProtect, 
		dwMaximumSizeHigh, dwMaximumSizeLow, pWideName);

	UTFStackFree(pWideName);

	return retVal;
}

__out
HANDLE
OpenFileMapping_UTF8(
    __in DWORD dwDesiredAccess,
    __in BOOL bInheritHandle,
    __in const char *pName
    )
{

	WCHAR *pWideName = NULL;
	HANDLE retVal;

	UTF8_To_UTF16_Stack(pName, pWideName);

	retVal =OpenFileMapping(dwDesiredAccess, bInheritHandle, pWideName);

	UTFStackFree(pWideName);

	return retVal;
}

BOOL
GetTextExtentPoint_UTF8(
    __in HDC hdc,
    __in_ecount(c) const char *pString,
    __in int c,
    __out LPSIZE lpsz
    )
{
	WCHAR *pWideString = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pString, pWideString);

	retVal = GetTextExtentPoint(hdc, pWideString, c, lpsz);

	UTFStackFree(pWideString);

	return retVal;
}


BOOL TextOut_UTF8( __in HDC hdc, 
	__in int x, 
	__in int y, 
	__in_ecount(c) const char *pString, __in int c)
{
	WCHAR *pWideString = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pString, pWideString);

	retVal = TextOut(hdc, x, y, pWideString, c);

	UTFStackFree(pWideString);

	return retVal;

}

BOOL
SetEnvironmentVariable_UTF8(
    __in     const char *pName,
    __in_opt const char *pValue
    )
{
	WCHAR *pWideName = NULL;
	WCHAR *pWideValue = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pName, pWideName);
	UTF8_To_UTF16_Stack(pValue, pWideValue);

	retVal = SetEnvironmentVariable(pWideName, pWideValue);

	UTFStackFree(pWideValue);
	UTFStackFree(pWideName);

	return retVal;
}

BOOL SHGetSpecialFolderPath_UTF8_dbg(
	HWND hwndOwner,
	char **ppOutPath,
	_In_   int csidl,
	_In_   BOOL fCreate MEM_DBG_PARMS
)
{
	WCHAR widePath[MAX_PATH];
	BOOL retVal = SHGetSpecialFolderPath(hwndOwner, widePath, csidl, fCreate);

	if (!retVal)
	{
		estrClear(ppOutPath);
		return false;
	}

	UTF16ToEstring_dbg(widePath, 0, ppOutPath MEM_DBG_PARMS_CALL);

	return retVal;
}


BOOL
GetDiskFreeSpaceEx_UTF8(
    __in_opt  const char *pDirName,
    __out_opt PULARGE_INTEGER lpFreeBytesAvailableToCaller,
    __out_opt PULARGE_INTEGER lpTotalNumberOfBytes,
    __out_opt PULARGE_INTEGER lpTotalNumberOfFreeBytes
    )
{
	WCHAR *pWideDirName = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pDirName, pWideDirName);

	retVal = GetDiskFreeSpaceEx(pWideDirName, lpFreeBytesAvailableToCaller, 
		lpTotalNumberOfBytes, lpTotalNumberOfFreeBytes);

	UTFStackFree(pWideDirName);

	return retVal;

}


BOOL
SetDlgItemText_UTF8(
    __in HWND hDlg,
    __in int nIDDlgItem,
    __in const char *pString)
{
	WCHAR *pWideString = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pString, pWideString);

	retVal = SetDlgItemText(hDlg, nIDDlgItem,
		pWideString);

	UTFStackFree(pWideString);

	return retVal;
}

BOOL
MoveFileEx_UTF8(
    __in     const char *pExistingFileName,
    __in_opt const char *pNewFileName,
    __in     DWORD    dwFlags
    )
{
	WCHAR *pWideExistingFileName = NULL;
	WCHAR *pWideNewFileName = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pExistingFileName, pWideExistingFileName);
	UTF8_To_UTF16_Stack(pNewFileName, pWideNewFileName);

	retVal = MoveFileEx(pWideExistingFileName, pWideNewFileName, dwFlags);

	UTFStackFree(pWideNewFileName);
	UTFStackFree(pWideExistingFileName);

	return retVal;


}

BOOL
CopyFile_UTF8(
    __in const char *pExistingFileName,
    __in const char *pNewFileName,
    __in BOOL bFailIfExists
    )
{
	WCHAR *pWideExistingFileName = NULL;
	WCHAR *pWideNewFileName = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pExistingFileName, pWideExistingFileName);
	UTF8_To_UTF16_Stack(pNewFileName, pWideNewFileName);

	retVal = CopyFile(pWideExistingFileName, pWideNewFileName, bFailIfExists);

	UTFStackFree(pWideNewFileName);
	UTFStackFree(pWideExistingFileName);

	return retVal;




}


BOOL
WriteConsole_UTF8(
    __in HANDLE hConsoleOutput,
    __in_ecount(nNumberOfCharsToWrite) CONST char *lpBuffer,
    __in DWORD nNumberOfCharsToWrite,
    __out_opt LPDWORD lpNumberOfCharsWritten,
    __reserved LPVOID lpReserved)
{
	WCHAR *pWideBuffer = NULL;
	BOOL retVal;
	int iSize = 0;

	UTF8_To_UTF16_Stack_GetSize(lpBuffer, pWideBuffer, iSize);

	retVal = WriteConsole(hConsoleOutput, pWideBuffer, iSize, lpNumberOfCharsWritten, lpReserved);

	UTFStackFree(pWideBuffer);

	return retVal;


}

BOOL
GetNamedPipeHandleState_UTF8_dbg(
    __in      HANDLE hNamedPipe,
    __out_opt LPDWORD lpState,
    __out_opt LPDWORD lpCurInstances,
    __out_opt LPDWORD lpMaxCollectionCount,
    __out_opt LPDWORD lpCollectDataTimeout,
    char **ppUserName MEM_DBG_PARMS
    )
{
	WCHAR wideUserName[1024];
	BOOL retVal = GetNamedPipeHandleState(hNamedPipe, lpState, lpCurInstances, lpMaxCollectionCount,lpCollectDataTimeout,
		SAFESTR(wideUserName));


	if (!retVal)
	{
		if (ppUserName)
		{
			estrClear(ppUserName);
		}
		return false;
	}

	if (ppUserName)
	{
		UTF16ToEstring_dbg(wideUserName, 0, ppUserName MEM_DBG_PARMS_CALL);
	}

	return retVal;

}

__out_opt
HANDLE
OpenMutex_UTF8(
    __in DWORD dwDesiredAccess,
    __in BOOL bInheritHandle,
    __in const char *pName
    )
{
	WCHAR *pWideBuffer = NULL;
	HANDLE retVal;
	int iSize = 0;

	UTF8_To_UTF16_Stack(pName, pWideBuffer);

	retVal = OpenMutex(dwDesiredAccess, bInheritHandle, pWideBuffer);

	UTFStackFree(pWideBuffer);

	return retVal;
}



BOOL
RemoveDirectory_UTF8(
    __in const char *pPathName
    )
{
	WCHAR *pWideBuffer = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pPathName, pWideBuffer);
	retVal = RemoveDirectory(pWideBuffer);
	UTFStackFree(pWideBuffer);

	return retVal;
}

BOOL
SetCurrentDirectory_UTF8(
    __in const char *pPathName
    )
{
	WCHAR *pWideBuffer = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pPathName, pWideBuffer);
	retVal = SetCurrentDirectory(pWideBuffer);
	UTFStackFree(pWideBuffer);

	return retVal;
}

void GetEnvironmentStrings_UTF8_dbg(
    char **ppOut MEM_DBG_PARMS
    )
{
	WCHAR *pWideStrings = GetEnvironmentStrings();
	UTF16ToEstring_dbg(pWideStrings, 0, ppOut MEM_DBG_PARMS_CALL);
	FreeEnvironmentStrings(pWideStrings);
}

#define STARTING_ENV_SIZE 2048

DWORD
ExpandEnvironmentStrings_UTF8_dbg(
    __in const char *pSrc,
    char **ppDst MEM_DBG_PARMS
    )
{
	WCHAR *pWideSource = NULL;
	WCHAR *pWideDest = malloc(sizeof(WCHAR) * STARTING_ENV_SIZE);
	int iCharsWritten;

	UTF8_To_UTF16_Stack(pSrc, pWideSource);

	iCharsWritten = ExpandEnvironmentStrings(pWideSource, pWideDest, STARTING_ENV_SIZE);

	if (!iCharsWritten)
	{
		free(pWideDest);
		UTFStackFree(pWideSource);
		return 0;
	}

	if (iCharsWritten >= STARTING_ENV_SIZE)
	{
		free(pWideDest);
		pWideDest = malloc(sizeof(WCHAR) * iCharsWritten);

		iCharsWritten = ExpandEnvironmentStrings(pWideSource, pWideDest, iCharsWritten);
	}

	UTF16ToEstring_dbg(pWideDest, iCharsWritten, ppDst MEM_DBG_PARMS_CALL);
	free(pWideDest);
	UTFStackFree(pWideSource);

	return estrLength(ppDst);
}

HANDLE
CreateEvent_UTF8(
    __in_opt LPSECURITY_ATTRIBUTES lpEventAttributes,
    __in     BOOL bManualReset,
    __in     BOOL bInitialState,
    __in_opt const char *lpName
    )
{
	WCHAR *pWideBuffer = NULL;
	HANDLE retVal;

	UTF8_To_UTF16_Stack(lpName, pWideBuffer);
	retVal = CreateEvent(lpEventAttributes, bManualReset, bInitialState, pWideBuffer);
	UTFStackFree(pWideBuffer);

	return retVal;

}

HANDLE
LoadImage_UTF8(
    __in_opt HINSTANCE hInst,
    __in const char *name,
    __in UINT type,
    __in int cx,
    __in int cy,
    __in UINT fuLoad)
{
	WCHAR *pWideBuffer = NULL;
	HANDLE retVal;

	UTF8_To_UTF16_Stack(name, pWideBuffer);
	retVal = LoadImage(hInst, pWideBuffer, type, cx, cy, fuLoad);
	UTFStackFree(pWideBuffer);

	return retVal;
}

HWND
CreateWindow_UTF8(
    __in_opt const char * lpClassName,
    __in_opt const char * lpWindowName,
    __in DWORD dwStyle,
    __in int X,
    __in int Y,
    __in int nWidth,
    __in int nHeight,
    __in_opt HWND hWndParent,
    __in_opt HMENU hMenu,
    __in_opt HINSTANCE hInstance,
    __in_opt LPVOID lpParam)
{
	WCHAR *pWideClassName = NULL;
	WCHAR *pWideWindowName = NULL;
	HWND retVal;

	UTF8_To_UTF16_Stack(lpClassName, pWideClassName);
	UTF8_To_UTF16_Stack(lpWindowName, pWideWindowName);

	retVal = CreateWindow(pWideClassName, pWideWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent,
		hMenu, hInstance, lpParam);

	UTFStackFree(pWideWindowName);
	UTFStackFree(pWideClassName);

	return retVal;
}

BOOL
MoveFile_UTF8(
    __in const char *pExistingFileName,
    __in const char *pNewFileName
    )
{
	WCHAR *pWideExistingFileName = NULL;
	WCHAR *pWideNewFileName = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pExistingFileName, pWideExistingFileName);
	UTF8_To_UTF16_Stack(pNewFileName, pWideNewFileName);

	retVal = MoveFile(pWideExistingFileName, pWideNewFileName);

	UTFStackFree(pWideNewFileName);
	UTFStackFree(pWideExistingFileName);

	return retVal;
}


__out_opt
HANDLE
OpenEvent_UTF8(
    __in DWORD dwDesiredAccess,
    __in BOOL bInheritHandle,
    __in const char *pName
    )
{
	WCHAR *pWideBuffer = NULL;
	HANDLE retVal;

	UTF8_To_UTF16_Stack(pName, pWideBuffer);
	retVal = OpenEvent(dwDesiredAccess, bInheritHandle, pWideBuffer);
	UTFStackFree(pWideBuffer);

	return retVal;
}

int
GetClipboardFormatName_UTF8_dbg(
    __in UINT format,
    char **ppOut MEM_DBG_PARMS)
{
	WCHAR temp[1024];
	int iRetVal = GetClipboardFormatName(format, temp, 1024);
	if (!iRetVal)
	{
		return 0;
	}

	//looks like this always has to be short, from the examples, so 1024 should be inf more than needed
	assert(iRetVal < 1024);

	UTF16ToEstring_dbg(temp, iRetVal, ppOut MEM_DBG_PARMS_CALL);

	return estrLength(ppOut);
}

BOOL
EnumDisplayDevices_UTF8(
    __in_opt const char *pDevice,
    __in DWORD iDevNum,
    __inout PDISPLAY_DEVICEW lpDisplayDevice,
    __in DWORD dwFlags)
{
	WCHAR *pWideBuffer = NULL;
	BOOL retVal;

	UTF8_To_UTF16_Stack(pDevice, pWideBuffer);
	retVal = EnumDisplayDevices(pWideBuffer, iDevNum, lpDisplayDevice, dwFlags);
	UTFStackFree(pWideBuffer);

	return retVal;


}

DWORD
GetProcessImageFileName_UTF8_dbg(
    __in HANDLE hProcess,
    char **ppOutStr MEM_DBG_PARMS)
{
	DWORD retVal;
	WCHAR wideBuf[MAX_PATH * 2];

	retVal = GetProcessImageFileName(hProcess, wideBuf, ARRAY_SIZE(wideBuf));

	if (!retVal)
	{
		return 0;
	}

	assert(retVal < ARRAY_SIZE(wideBuf));

	UTF16ToEstring_dbg(wideBuf, retVal, ppOutStr MEM_DBG_PARMS_CALL);

	return estrLength(ppOutStr);
}

BOOL SHGetPathFromIDList_UTF8_dbg(__in PCIDLIST_ABSOLUTE pidl, char **ppOutPath MEM_DBG_PARMS)
{
	WCHAR wideBuf[MAX_PATH];
	BOOL bRetVal;

	bRetVal = SHGetPathFromIDList(pidl, wideBuf);

	if (!bRetVal)
	{
		return false;
	}

	UTF16ToEstring_dbg(wideBuf, 0, ppOutPath MEM_DBG_PARMS_CALL);

	return true;
}

 BOOL  ExtTextOut_UTF8( __in HDC hdc, __in int x, __in int y, __in UINT options, 
	 __in_opt CONST RECT * lprect, 
	 __in_ecount_opt(c) char *pStr, __in UINT c, __in_ecount_opt(c) CONST INT * lpDx)
 {
	WCHAR *pWideBuffer = NULL;
	BOOL retVal;
	UINT iS16Size = 0;

	UTF8_With_Size_To_UTF16_Stack_GetSize(pStr, c, pWideBuffer, iS16Size);

	retVal = ExtTextOut(hdc, x, y, options, 
		lprect, 
	 pWideBuffer, iS16Size, lpDx);
	UTFStackFree(pWideBuffer);

	return retVal;
 }

  int ComboBox_AddString_UTF8(HWND hwndCtl, const char *pStr)
  {
	WCHAR *pWideBuffer = NULL;
	int retVal;

	UTF8_To_UTF16_Stack(pStr, pWideBuffer);
	retVal = ComboBox_AddString(hwndCtl, pWideBuffer);
	UTFStackFree(pWideBuffer);

	return retVal;


  }

int
GetClassName_UTF8_dbg(
    __in HWND hWnd,
    char **ppOutString MEM_DBG_PARMS
    )
{
	WCHAR wideBuf[256];
	int iCount = GetClassName(hWnd, wideBuf, ARRAY_SIZE(wideBuf));
	if (!iCount)
	{
		return 0;
	}
	assert(iCount < ARRAY_SIZE(wideBuf));
	
	UTF16ToEstring_dbg(wideBuf, 0, ppOutString MEM_DBG_PARMS_CALL);

	return true;
}

 int ListBox_GetText_UTF8_dbg(HWND hwndCtl, int ind, char **ppOutStr MEM_DBG_PARMS)
 {
	 int iLen = ListBox_GetTextLen(hwndCtl, ind);
	 WCHAR *pWideBuf;

	 estrClear(ppOutStr);

	 if (!iLen)
	 {
		 return 0;
	 }

	 if (iLen < 1000)
	 {
		 pWideBuf = alloca((iLen + 1) * sizeof(WCHAR));
		 assert(ListBox_GetText(hwndCtl, ind, pWideBuf));
		 UTF16ToEstring_dbg(pWideBuf, iLen, ppOutStr MEM_DBG_PARMS_CALL);
		 return estrLength(ppOutStr);
	 }
	 else
	 {
		 pWideBuf = malloc((iLen + 1) * sizeof(WCHAR));
		 assert(ListBox_GetText(hwndCtl, ind, pWideBuf));
		 UTF16ToEstring_dbg(pWideBuf, iLen, ppOutStr MEM_DBG_PARMS_CALL);
		 free(pWideBuf);
		 return estrLength(ppOutStr);
	 }
 }


 int ListBox_AddString_UTF8(HWND hDlg, const char *pStr)
 {
	WCHAR *pWideBuffer = NULL;
	int retVal;

	UTF8_To_UTF16_Stack(pStr, pWideBuffer);
	retVal = ListBox_AddString(hDlg, pWideBuffer);
	UTFStackFree(pWideBuffer);

	return retVal;
 }

  int ListBox_SelectString_UTF8(HWND hWndClt, int iIndexStart, const char *pStr)
 {

	 WCHAR *pWideBuffer = NULL;
	int retVal;

	UTF8_To_UTF16_Stack(pStr, pWideBuffer);
	retVal = ListBox_SelectString( hWndClt,  iIndexStart, pWideBuffer);
	UTFStackFree(pWideBuffer);

	return retVal;
 }


 int ComboBox_SelectString_UTF8(
  HWND hwndCtl,
  int indexStart,
  const char *pFind
  )
{

    WCHAR *pWideBuffer = NULL;
	int retVal;

	UTF8_To_UTF16_Stack(pFind, pWideBuffer);
	retVal = ComboBox_SelectString( hwndCtl,  indexStart, pWideBuffer);
	UTFStackFree(pWideBuffer);

	return retVal;
 }


void WideFindDataToUTF8(WIN32_FIND_DATAW *pWide, WIN32_FIND_DATAA *pNarrow)
{
	pNarrow->dwFileAttributes = pWide->dwFileAttributes;
	pNarrow->ftCreationTime = pWide->ftCreationTime;
	pNarrow->ftLastAccessTime = pWide->ftLastAccessTime;
	pNarrow->ftLastWriteTime = pWide->ftLastWriteTime;
	pNarrow->nFileSizeHigh = pWide->nFileSizeHigh;
	pNarrow->nFileSizeLow = pWide->nFileSizeLow;
	pNarrow->dwReserved0 = pWide->dwReserved0;
	pNarrow->dwReserved1 = pWide->dwReserved1;

	WideToUTF8StrConvert(pWide->cFileName, SAFESTR(pNarrow->cFileName));
	WideToUTF8StrConvert(pWide->cAlternateFileName, SAFESTR(pNarrow->cAlternateFileName));
}

  __out
HANDLE
FindFirstFile_UTF8(
    __in  const  char *pFileName,
    __out LPWIN32_FIND_DATAA lpFindFileData
    )
{
	WIN32_FIND_DATAW wideFindData = {0};
	WCHAR *pWideFileName = NULL;
	HANDLE retVal;
	
	UTF8_To_UTF16_Stack(pFileName, pWideFileName);
	retVal = FindFirstFile(pWideFileName, &wideFindData);
	UTFStackFree(pWideFileName);

	WideFindDataToUTF8(&wideFindData, lpFindFileData);

	return retVal;

  }

BOOL
FindNextFile_UTF8(
    __in  HANDLE hFindFile,
    __out LPWIN32_FIND_DATAA lpFindFileData
    )
{
	WIN32_FIND_DATAW wideFindData = {0};
	BOOL retVal = FindNextFile(hFindFile, &wideFindData);

	WideFindDataToUTF8(&wideFindData, lpFindFileData);

	return retVal;
}

FILE *_wfsopen_UTF8( 
   _In_z_ const char *filename,
   _In_z_ const char *mode,
   _In_ int shflag 
)
{
	WCHAR *pWideFileName = NULL;
	WCHAR *pWideMode = NULL;
	FILE *pRetVal;

	UTF8_To_UTF16_Stack(filename, pWideFileName);
	UTF8_To_UTF16_Stack(mode, pWideMode);

	pRetVal = _wfsopen(pWideFileName, pWideMode, shflag);

	UTFStackFree(pWideMode);
	UTFStackFree(pWideFileName);

	return pRetVal;
}




LRESULT SendMessage_AddString_UTF8(HWND hDlg, const char *pStr)
{
	WCHAR *pWideBuffer = NULL;
	LRESULT retVal;

	UTF8_To_UTF16_Stack(pStr, pWideBuffer);

	retVal = SendMessage(hDlg, CB_ADDSTRING, 0, (LPARAM)pWideBuffer);

	UTFStackFree(pWideBuffer);

	return retVal;
}


LRESULT SendMessage_SelectString_UTF8(HWND hDlg, const char *pStr)
{
	WCHAR *pWideBuffer = NULL;
	LRESULT retVal;

	UTF8_To_UTF16_Stack(pStr, pWideBuffer);

	retVal = SendMessage(hDlg, CB_SELECTSTRING, 0, (LPARAM)pWideBuffer);

	UTFStackFree(pWideBuffer);

	return retVal;
}


LRESULT SendMessage_ReplaceSel_UTF8(HWND hDlg, const char *pStr)
{
	WCHAR *pWideBuffer = NULL;
	LRESULT retVal;

	UTF8_To_UTF16_Stack(pStr, pWideBuffer);

	retVal = SendMessage(hDlg, EM_REPLACESEL, 0, (LPARAM)pWideBuffer);

	UTFStackFree(pWideBuffer);

	return retVal;
}


int __unlink_UTF8(_In_z_ const char *name)
{
	int iRetVal;
	WCHAR *pWideBuffer = NULL;
	UTF8_To_UTF16_Stack(name, pWideBuffer);
	iRetVal = _wunlink(pWideBuffer);
	UTFStackFree(pWideBuffer);

	return iRetVal;
}

int __chmod_UTF8(_In_z_ const char *name, int mode)
{
	int iRetVal;
	WCHAR *pWideBuffer = NULL;
	UTF8_To_UTF16_Stack(name, pWideBuffer);
	iRetVal = _wchmod(pWideBuffer, mode);
	UTFStackFree(pWideBuffer);

	return iRetVal;
}

int remove_UTF8(_In_z_ const char *path)
{
	int iRetVal;
	WCHAR *pWideBuffer = NULL;
	UTF8_To_UTF16_Stack(path, pWideBuffer);
	iRetVal = _wremove(pWideBuffer);
	UTFStackFree(pWideBuffer);

	return iRetVal;

}

 int system_UTF8(__in const char *pStr)
 {

	int iRetVal;
	WCHAR *pWideBuffer = NULL;
	UTF8_To_UTF16_Stack(pStr, pWideBuffer);
	iRetVal = _wsystem(pWideBuffer);
	UTFStackFree(pWideBuffer);

	return iRetVal;



 }

int open_cryptic(_In_z_ const char* filename, int oflag)
{
	int retVal;
	WCHAR *pWideBuffer = NULL;
	UTF8_To_UTF16_Stack(filename, pWideBuffer);
	_wsopen_s(&retVal, pWideBuffer, oflag, _SH_DENYNO, _S_IREAD | _S_IWRITE);
	UTFStackFree(pWideBuffer);
	return retVal;
}

int chdir_UTF8(_In_z_ const char *path)
{
	int iRetVal;
	WCHAR *pWideBuffer = NULL;
	UTF8_To_UTF16_Stack(path, pWideBuffer);
	iRetVal = _wchdir(pWideBuffer);
	UTFStackFree(pWideBuffer);

	return iRetVal;

}

int mkdir_UTF8(_In_z_ const char *path)
{
	int iRetVal;
	WCHAR *pWideBuffer = NULL;
	UTF8_To_UTF16_Stack(path, pWideBuffer);
	iRetVal = _wmkdir(pWideBuffer);
	UTFStackFree(pWideBuffer);

	return iRetVal;

}

int rmdir_UTF8(_In_z_ const char *path)
{
	int iRetVal;
	WCHAR *pWideBuffer = NULL;
	UTF8_To_UTF16_Stack(path, pWideBuffer);
	iRetVal = _wrmdir(pWideBuffer);
	UTFStackFree(pWideBuffer);

	return iRetVal;

}


int rename_UTF8(_In_z_ const char *oldname, _In_z_ const char *newname)
{
	int iRetVal;
	WCHAR *pWideOldName = NULL;
	WCHAR *pWideNewName = NULL;
	UTF8_To_UTF16_Stack(oldname, pWideOldName);
	UTF8_To_UTF16_Stack(newname, pWideNewName);
	iRetVal = _wrename(pWideOldName, pWideNewName);
	UTFStackFree(pWideNewName);
	UTFStackFree(pWideOldName);

	return iRetVal;



}

_Check_return_wat_ errno_t __cdecl _wsopen_s_UTF8(_Out_ int * _FileHandle, 
	_In_z_ const char * _Filename, _In_ int _OpenFlag, _In_ int _ShareFlag, _In_ int _PermissionFlag)
{
	errno_t iRetVal;
	WCHAR *pWideBuffer = NULL;
	UTF8_To_UTF16_Stack(_Filename, pWideBuffer);
	iRetVal = _wsopen_s(_FileHandle, pWideBuffer, _OpenFlag, _ShareFlag, _PermissionFlag);
	UTFStackFree(pWideBuffer);

	return iRetVal;
}

int print_UTF8(__in const char *pStr)
{
	int iRetVal;
	WCHAR *pWideBuffer = NULL;
	UTF8_To_UTF16_Stack(pStr, pWideBuffer);
	iRetVal = wprintf(L"%s", pWideBuffer);
	UTFStackFree(pWideBuffer);	

	return iRetVal;
}

char *_fullpath_UTF8_dbg(char *pOutFullPath, const char *pInPath, int iOutSize MEM_DBG_PARMS)
{
	WCHAR *pWideInPath = NULL;
	char *pRetVal;
	UTF8_To_UTF16_Stack(pInPath, pWideInPath);
	assert(pWideInPath);

	if (pOutFullPath)
	{
		WCHAR *pOutWideBuf = malloc(iOutSize * sizeof(S16));
		WCHAR *pWideRetVal = _wfullpath(pOutWideBuf, pWideInPath, iOutSize);
		int iNumWideChars = (int)wcslen(pOutWideBuf);
		int iTempSize = iNumWideChars * 3 + 1;
		char *pTemp = malloc(iTempSize);
		int iOutLen;

		WideToUTF8StrConvert(pOutWideBuf, pTemp, iTempSize);

		iOutLen = (int)strlen(pTemp);

		if (iOutLen < iOutSize)
		{
			memcpy(pOutFullPath, pTemp, iOutLen + 1);
		}
		else
		{
			memcpy(pOutFullPath, pTemp, iOutSize - 1);
			pOutFullPath[iOutSize - 1] = 0;
		}

		free(pTemp);

		SAFE_FREE(pOutWideBuf);
		UTFStackFree(pWideInPath);
		return pOutFullPath;


	}
	else
	{
		WCHAR *pWideRetVal = _wfullpath(NULL, pWideInPath, 0);
		pRetVal = strdup_UTF16ToUTF8(pWideRetVal);

		crt_free(pWideRetVal);
		
		UTFStackFree(pWideInPath);
		return pRetVal;

	}
}



int _wstat64_UTF8(_In_z_ const char * _Name, _Out_ struct _stat64 * _Stat)
{	
	int iRetVal;
	WCHAR *pWideName = NULL;
	UTF8_To_UTF16_Stack(_Name, pWideName);
	iRetVal = _wstat64(pWideName, _Stat);
	
	UTFStackFree(pWideName);

	return iRetVal;
}

void wfinddata32i64_ToNarrow(struct _wfinddata32i64_t *pWide, struct _finddata32i64_t *pNarrow)
{
	pNarrow->attrib = pWide->attrib;
	pNarrow->time_create = pWide->time_create;
	pNarrow->time_access = pWide->time_access;
	pNarrow->time_write = pWide->time_write;
	pNarrow->size = pWide->size;
	WideToUTF8StrConvert(pWide->name, pNarrow->name, ARRAY_SIZE(pNarrow->name));
}

intptr_t _wfindfirst32i64_UTF8(const char* filename, struct _finddata32i64_t * pfd)
{
	intptr_t iRetVal;
	WCHAR *pWideName = NULL;
	struct _wfinddata32i64_t wideFileInfo = {0};
	UTF8_To_UTF16_Stack(filename, pWideName);

	assert(pWideName);

	iRetVal = _wfindfirst32i64(pWideName, &wideFileInfo);
	UTFStackFree(pWideName);

	wfinddata32i64_ToNarrow(&wideFileInfo, pfd);

	return iRetVal;
}

int _wfindnext32i64_UTF8(intptr_t filehandle, struct _finddata32i64_t * pfd)
{
	int iRetVal;
	struct _wfinddata32i64_t wideFileInfo = {0};
	iRetVal = _wfindnext32i64(filehandle, &wideFileInfo);
	wfinddata32i64_ToNarrow(&wideFileInfo, pfd);

	return iRetVal;
}




void wfinddata32_ToNarrow(struct _wfinddata32_t *pWide, struct _finddata32_t *pNarrow)
{
	pNarrow->attrib = pWide->attrib;
	pNarrow->time_create = pWide->time_create;
	pNarrow->time_access = pWide->time_access;
	pNarrow->time_write = pWide->time_write;
	pNarrow->size = pWide->size;
	WideToUTF8StrConvert(pWide->name, pNarrow->name, ARRAY_SIZE(pNarrow->name));
}

intptr_t _wfindfirst32_UTF8(const char* filename, struct _finddata32_t * pfd)
{
	intptr_t iRetVal;
	WCHAR *pWideName = NULL;
	struct _wfinddata32_t wideFileInfo = {0};
	UTF8_To_UTF16_Stack(filename, pWideName);

	assert(pWideName);

	iRetVal = _wfindfirst32(pWideName, &wideFileInfo);
	UTFStackFree(pWideName);

	wfinddata32_ToNarrow(&wideFileInfo, pfd);

	return iRetVal;
}

int _wfindnext32_UTF8(intptr_t filehandle, struct _finddata32_t * pfd)
{
	int iRetVal;
	struct _wfinddata32_t wideFileInfo = {0};
	iRetVal = _wfindnext32(filehandle, &wideFileInfo);
	wfinddata32_ToNarrow(&wideFileInfo, pfd);

	return iRetVal;
}




void wfinddata_ToNarrow(struct _wfinddata_t *pWide, struct _finddata_t *pNarrow)
{
	pNarrow->attrib = pWide->attrib;
	pNarrow->time_create = pWide->time_create;
	pNarrow->time_access = pWide->time_access;
	pNarrow->time_write = pWide->time_write;
	pNarrow->size = pWide->size;
	WideToUTF8StrConvert(pWide->name, pNarrow->name, ARRAY_SIZE(pNarrow->name));
}

intptr_t _wfindfirst_UTF8(const char* filename, struct _finddata_t * pfd)
{
	intptr_t iRetVal;
	WCHAR *pWideName = NULL;
	struct _wfinddata_t wideFileInfo = {0};
	UTF8_To_UTF16_Stack(filename, pWideName);

	assert(pWideName);

	iRetVal = _wfindfirst(pWideName, &wideFileInfo);
	UTFStackFree(pWideName);

	wfinddata_ToNarrow(&wideFileInfo, pfd);

	return iRetVal;
}

int _wfindnext_UTF8(intptr_t filehandle, struct _finddata_t * pfd)
{
	int iRetVal;
	struct _wfinddata_t wideFileInfo = {0};
	iRetVal = _wfindnext(filehandle, &wideFileInfo);
	wfinddata_ToNarrow(&wideFileInfo, pfd);

	return iRetVal;
}

LSTATUS
RegOpenKey_UTF8(
    __in HKEY hKey,
    __in_opt const char *pSubKey,
    __out PHKEY phkResult
    )
{
	LSTATUS retVal;
	WCHAR *pWideSubKey = NULL;
	UTF8_To_UTF16_Stack(pSubKey, pWideSubKey);
	retVal = RegOpenKey(hKey, pWideSubKey, phkResult);
	UTFStackFree(pWideSubKey);	

	return retVal;

}