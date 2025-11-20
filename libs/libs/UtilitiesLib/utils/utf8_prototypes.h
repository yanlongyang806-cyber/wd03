#pragma once

//this file contains just the function prototypes for the UTF8 wrapper files found in utf8.c. It's in a separate file so that
//it can be included directly from .cpp files and in other tricky ways where including causes massive compile failures.

#include "memcheck_minimal_defines.h"


//need an extra declaration here of estrDestroy so that the oddball files that call the functions here that return estrings can destroy them,
//but only if we don't have our magical PRE_ defines and so forth
#if !defined(SA_PRE_OP_OP_STR)
void estrDestroy( char** str);
int WideToUTF8StrConvert(const wchar_t* str, char* outBuffer, int outBufferMaxLength);
#endif



//A bunch of wrappers around windows functions that return UTF16. Returns UTF8 estrings instead
DWORD GetModuleFileNameEx_UTF8_dbg(_In_ HANDLE hProcess, 
	_In_opt_ HMODULE hModule, char **ppOutEString MEM_DBG_PARMS);
#define GetModuleFileNameEx_UTF8(hProcess, hModule, ppOutEString) GetModuleFileNameEx_UTF8_dbg(hProcess, hModule, ppOutEString MEM_DBG_PARMS_INIT)

DWORD GetModuleBaseName_UTF8_dbg(
  _In_ HANDLE hProcess,
  _In_opt_ HMODULE hModule,
  char **ppOutName MEM_DBG_PARMS);
#define GetModuleBaseName_UTF8(hProcess, hModle, ppOutName) GetModuleBaseName_UTF8_dbg(hProcess, hModle, ppOutName MEM_DBG_PARMS_INIT) 



DWORD GetModuleFileName_UTF8_dbg(__in_opt HMODULE hModule, char **ppOutString MEM_DBG_PARMS);
#define GetModuleFileName_UTF8(hModule, ppOutString) GetModuleFileName_UTF8_dbg(hModule, ppOutString MEM_DBG_PARMS_INIT)

DWORD GetEnvironmentVariable_UTF8_dbg(_In_opt_ const char *pName, char **ppOutEString MEM_DBG_PARMS);
#define GetEnvironmentVariable_UTF8(pName, ppOutEString) GetEnvironmentVariable_UTF8_dbg(pName, ppOutEString MEM_DBG_PARMS_INIT)

void OutputDebugString_UTF8(_In_opt_ const char *pBuf);

HANDLE CreateMutex_UTF8(_In_opt_ LPSECURITY_ATTRIBUTES lpMutexAttributes, _In_ BOOL bInitialOwner, _In_opt_ const char *pName);

__out_opt
HANDLE
OpenMutex_UTF8(
    __in DWORD dwDesiredAccess,
    __in BOOL bInheritHandle,
    __in const char *pName
    );

DWORD FormatMessage_UTF8_dbg(_In_ DWORD dwFlags,
  _In_opt_ LPCVOID lpSource,
  _In_ DWORD dwMessageId,
  _In_ DWORD dwLanguageId,
  char **ppOutMessage MEM_DBG_PARMS,
  _In_opt_ va_list *Arguments);
#define FormatMessage_UTF8(dwFlags, lpSource, dwMessageId, dwLanguageId, ppOutMessage, Arguments) \
	FormatMessage_UTF8_dbg(dwFlags, lpSource, dwMessageId, dwLanguageId, ppOutMessage MEM_DBG_PARMS_INIT, Arguments)

HANDLE CreateSemaphore_UTF8(
    _In_opt_ LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    _In_ LONG lInitialCount,
    _In_ LONG lMaximumCount,
    _In_opt_ const char *pName);

void SetConsoleTitle_UTF8(const char *pTitle);

HANDLE CreateFile_UTF8(
	_In_ const char *pFileName,
	_In_ DWORD dwDesiredAccess,
	_In_ DWORD dwShareMode,
	_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	_In_ DWORD dwCreationDisposition,
	_In_ DWORD dwFlagsAndAttributes,
	_In_opt_ HANDLE hTemplateFile);

UINT GetTempFileName_UTF8_dbg(_In_ const char *pPathName, 
	_In_ const char *pPrefixStirng, 
	_In_ UINT uUnique, 
	char **ppOutTempFileName MEM_DBG_PARMS);
#define GetTempFileName_UTF8(pPathName, pPrefixString, uUnique, ppOutTempFileName) GetTempFileName_UTF8_dbg(pPathName, pPrefixString, uUnique, ppOutTempFileName MEM_DBG_PARMS_INIT)



BOOL WriteConsoleOutputCharacter_UTF8(
  _In_ HANDLE hConsoleOutput,
  _In_ char *pInStr,
  _In_ DWORD nLength,
  _In_ COORD dwWriteCoord,
  _Out_ LPDWORD lpNumberOfCharsWritten);

int GetLocaleInfo_UTF8_dbg(
	_In_ LCID Locale,
	_In_ LCTYPE LCType, char **ppOutData MEM_DBG_PARMS);
#define GetLocaleInfo_UTF8(Locale, LCType, ppOutData) GetLocaleInfo_UTF8_dbg(Locale, LCType, ppOutData MEM_DBG_PARMS_INIT)



BOOL SetWindowText_UTF8(_In_ HWND hWnd, _In_opt_ const char *pText);

int GetWindowText_UTF8_dbg(_In_ HWND hWnd, char **ppOutString MEM_DBG_PARMS);
#define GetWindowText_UTF8(hWnd, ppOutString) GetWindowText_UTF8_dbg(hWnd, ppOutString MEM_DBG_PARMS_INIT)
//note this line from WindowsX.h:
//#define ComboBox_GetText(hwndCtl, lpch, cchMax) GetWindowText((hwndCtl), (lpch), (cchMax))
#define ComboBox_GetText_UTF8(hWnd, ppOutString) GetWindowText_UTF8(hWnd, ppOutString)
//similarly
#define Edit_SetText_UTF8(hWnd, pText) SetWindowText_UTF8(hWnd, pText)
#define ComboBox_SetText_UTF8(hWnd, pText) SetWindowText_UTF8(hWnd, pText)
#define Edit_GetText_UTF8(hWnd, ppText) GetWindowText_UTF8(hWnd, ppText)

#define GetWindowText_UTF8_FixedSize(hWnd, pOutBuf, iSize) { if (pOutBuf) {\
	char *_pTemp = NULL; estrStackCreate(&_pTemp); GetWindowText_UTF8(hWnd, &_pTemp); strcpy_s(pOutBuf, iSize, _pTemp); estrDestroy(&_pTemp); }}

HMODULE LoadLibrary_UTF8(_In_ const char *pName);

BOOL DeleteFile_UTF8(_In_ const char *pName);

HINSTANCE ShellExecute_UTF8(
	_In_opt_ HWND hwnd,
	_In_opt_ const char *pOperation,
	_In_  const char *pFile,
	_In_opt_ const char *pParameters,
	_In_opt_ const char *pDirectory,
	_In_ INT nShowCmd
);

BOOL LookupPrivilegeValue_UTF8(
	_In_opt_ const char *pSystemName,
	_In_ const char *pName,
	_Out_ PLUID lpLuid
);

BOOL WaitNamedPipe_UTF8(
  _In_ const char *pPipeName,
  _In_ DWORD nTimeOut
);

HANDLE CreateNamedPipe_UTF8(
	_In_ const char *pName,
	_In_ DWORD dwOpenMode,
	_In_ DWORD dwPipeMode,
	_In_ DWORD nMaxInstances,
	_In_ DWORD nOutBufferSize,
	_In_ DWORD nInBufferSize,
	_In_ DWORD nDefaultTimeOut,
	_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes
);

LONG
RegOpenKeyEx_UTF8 (
    __in HKEY hKey,
    __in_opt const char *pSubKey,
    __in_opt DWORD ulOptions,
    __in REGSAM samDesired,
    __out PHKEY phkResult
    );

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
    );

LONG
RegQueryValueEx_UTF8 (
    __in HKEY hKey,
    __in_opt const char *pValueName,
    __reserved LPDWORD lpReserved,
    __out_opt LPDWORD lpType,
    __out_bcount_part_opt(*lpcbData, *lpcbData) __out_data_source(REGISTRY) LPBYTE lpData,
    __inout_opt LPDWORD lpcbData
    );

LONG
RegSetValueEx_UTF8 (
    __in HKEY hKey,
    __in_opt const char *pValueName,
    __reserved DWORD Reserved,
    __in DWORD dwType,
    __in_bcount_opt(cbData) CONST BYTE* lpData,
    __in DWORD cbData
    );

LONG
RegDeleteValue_UTF8 (
    __in HKEY hKey,
    __in_opt const char *pValueName
    );

LONG
RegDeleteKey_UTF8 (
    __in HKEY hKey,
    __in const char *pSubKey
    );


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
    );


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
    );

//the wide version of createProcess can modify pCommandLine, but not in a way
//that actually matters, so we hide that in our wrapper
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
    );


int
MessageBox_UTF8(
    __in_opt HWND hWnd,
    __in_opt const char *pText,
    __in_opt const char *pCaption,
    __in UINT uType);

int GetCurrentDirectory_UTF8_dbg(char **ppOutDir MEM_DBG_PARMS);
#define GetCurrentDirectory_UTF8(ppOutDir) GetCurrentDirectory_UTF8_dbg(ppOutDir MEM_DBG_PARMS_INIT)


BOOL GetVolumeInformation_UTF8_dbg(
    char *pRootPathName,
    char **ppOutVolumeName, //can be NULL
    __out_opt LPDWORD lpVolumeSerialNumber,
    __out_opt LPDWORD lpMaximumComponentLength,
    __out_opt LPDWORD lpFileSystemFlags,
    char **ppOutFileSystemName // can be NULL
	MEM_DBG_PARMS);
#define GetVolumeInformation_UTF8(lpRootPathName, ppOutVolumeName, lpVolumeSerialNumber, lpMaximumComponentLength, lpFileSystemFlags, ppOutFileSystemName) GetVolumeInformation_UTF8_dbg(lpRootPathName, ppOutVolumeName, lpVolumeSerialNumber, lpMaximumComponentLength, lpFileSystemFlags, ppOutFileSystemName MEM_DBG_PARMS_INIT)

UINT GetSystemDirectory_UTF8_dbg(char **ppOutName MEM_DBG_PARMS);
#define GetSystemDirectory_UTF8(ppOutName) GetSystemDirectory_UTF8_dbg(ppOutName MEM_DBG_PARMS_INIT)

__out_opt
HRSRC
FindResource_UTF8(
    __in_opt HMODULE hModule,
    __in     const char *pName,
    __in     const char *pType
    );

UINT
GetDlgItemText_UTF8_dbg(
    __in HWND hDlg,
    __in int nIDDlgItem,
    char **ppOutString MEM_DBG_PARMS);
#define GetDlgItemText_UTF8(hDlg, niDDlgItem, ppOutString) GetDlgItemText_UTF8_dbg(hDlg, niDDlgItem, ppOutString MEM_DBG_PARMS_INIT)


DWORD
GetFileAttributes_UTF8(
    __in const char *pFileName
    );

__out
HANDLE
FindFirstFileEx_UTF8(
    __in       const char *pFileName,
    __in       FINDEX_INFO_LEVELS fInfoLevelId,
    __out      LPVOID lpFindFileData,
    __in       FINDEX_SEARCH_OPS fSearchOp,
    __reserved LPVOID lpSearchFilter,
    __in       DWORD dwAdditionalFlags
    );

UINT
GetDriveType_UTF8(
    __in_opt const char *pRootPathName
    );

DWORD
GetFileVersionInfoSize_UTF8(
        __in const char *pFileName, /* Filename of version stamped file */
        __out_opt LPDWORD lpdwHandle       /* Information for use by GetFileVersionInfo */
        );


BOOL
GetFileVersionInfo_UTF8(
        __in                const char *pFileName, /* Filename of version stamped file */
        __reserved          DWORD dwHandle,          /* Information from GetFileVersionSize */
        __in                DWORD dwLen,             /* Length of buffer for info */
        __out_bcount(dwLen) LPVOID lpData            /* Buffer to place the data structure */
        );

BOOL
VerQueryValue_UTF8(
        __in LPCVOID pBlock,
        __in const char *pSubBlock,
        __deref_out_xcount("buffer can be PWSTR or DWORD*") LPVOID * lplpBuffer,
        __out PUINT puLen
        );


HWND
FindWindow_UTF8(
    __in_opt const char *pClassName,
    __in_opt const char *pWindowName);


__out_opt
HANDLE
CreateFileMapping_UTF8(
    __in     HANDLE hFile,
    __in_opt LPSECURITY_ATTRIBUTES lpFileMappingAttributes,
    __in     DWORD flProtect,
    __in     DWORD dwMaximumSizeHigh,
    __in     DWORD dwMaximumSizeLow,
    __in_opt const char *pName
    );

__out
HANDLE
OpenFileMapping_UTF8(
    __in DWORD dwDesiredAccess,
    __in BOOL bInheritHandle,
    __in const char *pName
    );

BOOL
GetTextExtentPoint_UTF8(
    __in HDC hdc,
    __in_ecount(c) const char *pString,
    __in int c,
    __out LPSIZE lpsz
    );

BOOL TextOut_UTF8( __in HDC hdc, 
	__in int x, 
	__in int y, 
	__in_ecount(c) const char *pString, __in int c);

BOOL
SetEnvironmentVariable_UTF8(
    __in     const char *pName,
    __in_opt const char *pValue
    );

BOOL SHGetSpecialFolderPath_UTF8_dbg(
	HWND hwndOwner,
	char **ppOutPath,
	_In_   int csidl,
	_In_   BOOL fCreate MEM_DBG_PARMS
);
#define SHGetSpecialFolderPath_UTF8(hwndOwner,ppOutPath,csidl, fCreate) SHGetSpecialFolderPath_UTF8_dbg(hwndOwner,ppOutPath,csidl, fCreate MEM_DBG_PARMS_INIT) 

BOOL
GetDiskFreeSpaceEx_UTF8(
    __in_opt  const char *pDirName,
    __out_opt PULARGE_INTEGER lpFreeBytesAvailableToCaller,
    __out_opt PULARGE_INTEGER lpTotalNumberOfBytes,
    __out_opt PULARGE_INTEGER lpTotalNumberOfFreeBytes
    );

BOOL
SetDlgItemText_UTF8(
    __in HWND hDlg,
    __in int nIDDlgItem,
    __in const char *pString);

BOOL
MoveFileEx_UTF8(
    __in     const char *pExistingFileName,
    __in_opt const char *pNewFileName,
    __in     DWORD    dwFlags
    );

BOOL
CopyFile_UTF8(
    __in const char *pExistingFileName,
    __in const char *pNewFileName,
    __in BOOL bFailIfExists
    );

BOOL
WriteConsole_UTF8(
    __in HANDLE hConsoleOutput,
    __in_ecount(nNumberOfCharsToWrite) CONST char *lpBuffer,
    __in DWORD nNumberOfCharsToWrite,
    __out_opt LPDWORD lpNumberOfCharsWritten,
    __reserved LPVOID lpReserved);

BOOL
RemoveDirectory_UTF8(
    __in const char *pPathName
    );

BOOL
SetCurrentDirectory_UTF8(
    __in const char *pPathName
    );


BOOL
GetNamedPipeHandleState_UTF8_dbg(
    __in      HANDLE hNamedPipe,
    __out_opt LPDWORD lpState,
    __out_opt LPDWORD lpCurInstances,
    __out_opt LPDWORD lpMaxCollectionCount,
    __out_opt LPDWORD lpCollectDataTimeout,
    char **ppUserName MEM_DBG_PARMS
    );
#define GetNamedPipeHandleState_UTF8(hNamedPipe, lpState, lpCurInstances, lpMaxCollectionCount, lpCollectDataTimeout, ppUserName) GetNamedPipeHandleState_UTF8_dbg(hNamedPipe, lpState, lpCurInstances, lpMaxCollectionCount, lpCollectDataTimeout, ppUserName MEM_DBG_PARMS_INIT)

DWORD
ExpandEnvironmentStrings_UTF8_dbg(
    __in const char *pSrc,
    char **ppDst MEM_DBG_PARMS
    );

#define ExpandEnvironmentStrings_UTF8(pSrc, ppDst) ExpandEnvironmentStrings_UTF8_dbg(pSrc, ppDst MEM_DBG_PARMS_INIT)


HANDLE
CreateEvent_UTF8(
    __in_opt LPSECURITY_ATTRIBUTES lpEventAttributes,
    __in     BOOL bManualReset,
    __in     BOOL bInitialState,
    __in_opt const char *lpName
    );


void GetEnvironmentStrings_UTF8_dbg(
    char **ppOut MEM_DBG_PARMS
    );

#define GetEnvironmentStrings_UTF8(ppOut) GetEnvironmentStrings_UTF8_dbg(ppOut MEM_DBG_PARMS_INIT)


HANDLE
LoadImage_UTF8(
    __in_opt HINSTANCE hInst,
    __in const char *name,
    __in UINT type,
    __in int cx,
    __in int cy,
    __in UINT fuLoad);


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
    __in_opt LPVOID lpParam);


BOOL
MoveFile_UTF8(
    __in const char *pExistingFileName,
    __in const char *pNewFileName
    );


__out_opt
HANDLE
OpenEvent_UTF8(
    __in DWORD dwDesiredAccess,
    __in BOOL bInheritHandle,
    __in const char *pName
    );

int
GetClipboardFormatName_UTF8_dbg(
    __in UINT format,
    char **ppOut MEM_DBG_PARMS);

#define GetClipboardFormatName_UTF8(format, ppOut) GetClipboardFormatName_UTF8_dbg(format, ppOut MEM_DBG_PARMS_INIT)

BOOL
EnumDisplayDevices_UTF8(
    __in_opt const char *pDevice,
    __in DWORD iDevNum,
    __inout PDISPLAY_DEVICEW lpDisplayDevice,
    __in DWORD dwFlags);

UINT
GetDriveType_UTF8(
    __in_opt const char *pRootPathName
    );


DWORD
GetProcessImageFileName_UTF8_dbg(
    __in HANDLE hProcess,
    char **ppOutStr MEM_DBG_PARMS);
#define GetProcessImageFileName_UTF8(hProcess, ppOutStr) GetProcessImageFileName_UTF8_dbg(hProcess, ppOutStr MEM_DBG_PARMS_INIT)

BOOL SHGetPathFromIDList_UTF8_dbg(__in PCIDLIST_ABSOLUTE pidl, char **ppOutPath MEM_DBG_PARMS);
#define SHGetPathFromIDList_UTF8(pidl, ppOutPath) SHGetPathFromIDList_UTF8_dbg(pidl, ppOutPath MEM_DBG_PARMS_INIT)

 BOOL  ExtTextOut_UTF8( __in HDC hdc, __in int x, __in int y, __in UINT options, 
	 __in_opt CONST RECT * lprect, 
	 __in_ecount_opt(c) char *pStr, __in UINT c, __in_ecount_opt(c) CONST INT * lpDx);

int system_UTF8(__in const char *pStr);

int print_UTF8(__in const char *pStr);


int
GetClassName_UTF8_dbg(
    __in HWND hWnd,
    char **ppOutString MEM_DBG_PARMS
    );
#define GetClassName_UTF8(hWnd, ppOutString) GetClassName_UTF8_dbg(hWnd, ppOutString MEM_DBG_PARMS_INIT)


 int ListBox_GetText_UTF8_dbg(HWND hwndCtl, int ind, char **ppOutStr MEM_DBG_PARMS);
#define ListBox_GetText_UTF8(hwndCtl, ind, ppOutStr) ListBox_GetText_UTF8_dbg(hwndCtl, ind, ppOutStr MEM_DBG_PARMS_INIT)
 
 int ComboBox_AddString_UTF8(HWND hwndCtl, const char *pStr);

 int ListBox_SelectString_UTF8(HWND hWndClt, int iIndexStart, const char *pStr);

 int ComboBox_SelectString_UTF8(
  HWND hwndCtl,
  int indexStart,
  const char *pFind
);

 //in most cases, use fwFindFirstFile instead of calling this
__out
HANDLE
FindFirstFile_UTF8(
    __in  const  char *pFileName,
    __out LPWIN32_FIND_DATAA lpFindFileData
    );

BOOL
FindNextFile_UTF8(
    __in  HANDLE hFindFile,
    __out LPWIN32_FIND_DATAA lpFindFileData
    );

void WideFindDataToUTF8(WIN32_FIND_DATAW *pWide, WIN32_FIND_DATAA *pNarrow);


FILE *_wfsopen_UTF8( 
   _In_z_ const char *filename,
   _In_z_ const char *mode,
   _In_ int shflag 
);

_Check_return_wat_ errno_t __cdecl _wsopen_s_UTF8(_Out_ int * _FileHandle, 
	_In_z_ const char * _Filename, _In_ int _OpenFlag, _In_ int _ShareFlag, _In_ int _PermissionFlag);


/*this isn't a wrapper around a function. Rather, it's a wrapper around 
		lResult = SendMessage(hDlg, CB_ADDSTRING, 0, pStr);
*/
LRESULT SendMessage_AddString_UTF8(HWND hDlg, const char *pStr);


/*this isn't a wrapper around a function. Rather, it's a wrapper around 
		lResult = SendMessage(hDlg, CB_SELECTSTRING, 0, pStr);
*/
LRESULT SendMessage_SelectString_UTF8(HWND hDlg, const char *pStr);

LRESULT SendMessage_ReplaceSel_UTF8(HWND hDlg, const char *pStr);

int ListBox_AddString_UTF8(HWND hDlg, const char *pStr);

//Static_SetText is just #defined to SetWindowText, so we do the same
#define Static_SetText_UTF8 SetWindowText_UTF8

intptr_t _wfindfirst32i64_UTF8(const char* filename, struct _finddata32i64_t * pfd);
int _wfindnext32i64_UTF8(intptr_t filehandle, struct _finddata32i64_t * pfd);
intptr_t _wfindfirst32_UTF8(const char* filename, struct _finddata32_t * pfd);
int _wfindnext32_UTF8(intptr_t filehandle, struct _finddata32_t * pfd);
intptr_t _wfindfirst_UTF8(const char* filename, struct _finddata_t * pfd);
int _wfindnext_UTF8(intptr_t filehandle, struct _finddata_t * pfd);


LSTATUS
RegOpenKey_UTF8(
    __in HKEY hKey,
    __in_opt const char *pSubKey,
    __out PHKEY phkResult
    );