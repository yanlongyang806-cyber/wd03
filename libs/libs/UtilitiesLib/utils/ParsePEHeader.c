///////////////////////////////////////////////////////////////////////////////
//
// DebugDir.cpp 
//
// DebugDir example source code 
// 
// Author: Oleg Starodumov (www.debuginfo.com)
//
//

#if !PLATFORM_CONSOLE

///////////////////////////////////////////////////////////////////////////////
// Include files 
//

#include "ParsePEHeader.h"
#include "UTF8.h"

#include <windows.h>
#pragma pack( push, before_imagehlp, 8 )
#include <dbghelp.h>
#pragma pack( pop, before_imagehlp )

#include <tchar.h>
#include <stdio.h>
#include <limits.h>

///////////////////////////////////////////////////////////////////////////////
// Helper macros 
//
// Thanks to Matt Pietrek 
#define MakePtr( cast, ptr, addValue ) (cast)( (DWORD_PTR)(ptr) + (DWORD_PTR)(addValue))


///////////////////////////////////////////////////////////////////////////////
// CodeView debug information structures 
//

#define CV_SIGNATURE_NB10   '01BN'
#define CV_SIGNATURE_RSDS   'SDSR'

// CodeView header 
typedef struct CV_HEADER 
{
	DWORD CvSignature; // NBxx
	LONG  Offset;      // Always 0 for NB10
} CV_HEADER;

// CodeView NB10 debug information 
// (used when debug information is stored in a PDB 2.00 file) 
typedef struct CV_INFO_PDB20 
{
	CV_HEADER  Header; 
	DWORD      Signature;       // seconds since 01.01.1970
	DWORD      Age;             // an always-incrementing value 
	BYTE       PdbFileName[1];  // zero terminated string with the name of the PDB file 
} CV_INFO_PDB20;

// CodeView RSDS debug information 
// (used when debug information is stored in a PDB 7.00 file) 
typedef struct CV_INFO_PDB70 
{
	DWORD      CvSignature; 
	GUID       Signature;       // unique identifier 
	DWORD      Age;             // an always-incrementing value 
	BYTE       PdbFileName[1];  // zero terminated string with the name of the PDB file 
} CV_INFO_PDB70;


///////////////////////////////////////////////////////////////////////////////
// Function declarations 
//

static bool CheckDosHeader( PIMAGE_DOS_HEADER pDosHeader, const char **failureReason, int *failureErrorCode ); 
static bool CheckNtHeaders( PIMAGE_NT_HEADERS pNtHeaders, const char **failureReason, int *failureErrorCode ); 
static bool CheckSectionHeaders( PIMAGE_NT_HEADERS pNtHeaders, const char **failureReason, int *failureErrorCode ); 
static bool CheckDebugDirectory( PIMAGE_DEBUG_DIRECTORY pDebugDir, DWORD DebugDirSize, const char **failureReason, int *failureErrorCode ); 
static bool IsPE32Plus( PIMAGE_OPTIONAL_HEADER pOptionalHeader, bool *bPE32Plus ); 
static bool GetDebugDirectoryRVA( PIMAGE_OPTIONAL_HEADER pOptionalHeader, DWORD* DebugDirRva, DWORD* DebugDirSize );
static bool GetFileOffsetFromRVA( PIMAGE_NT_HEADERS pNtHeaders, DWORD Rva, DWORD* FileOffset ); 
static void DumpDebugDirectoryEntries( LPBYTE pImageBase, PIMAGE_DEBUG_DIRECTORY pDebugDir, DWORD DebugDirSize, PIMAGEHLP_MODULE64 pModuleInfo, const char **failureReason, int *failureErrorCode, bool DiskMode ); 
static int DumpDebugDirectoryEntry( LPBYTE pImageBase, PIMAGE_DEBUG_DIRECTORY pDebugDir, PIMAGEHLP_MODULE64 pModuleInfo, const char **failureReason, int *failureErrorCode, bool DiskMode ); 
static void DumpCodeViewDebugInfo( LPBYTE pDebugInfo, DWORD DebugInfoSize, PIMAGEHLP_MODULE64 pModuleInfo ); 

///////////////////////////////////////////////////////////////////////////////
// main 
//

// FIXME: This function is way too aggressive about verification for its intended purpose.  In general, we'd prefer to get module information,
// even if the headers are damaged in some way, to getting nothing at all.
bool GetDebugInfo( const char *fileName, void *moduleBase, void *pModuleInfoVoid, const char **failureReason, int *failureErrorCode ) 
{
	PIMAGEHLP_MODULE64 pModuleInfo = pModuleInfoVoid;
	HANDLE hFile      = NULL; 
	HANDLE hFileMap   = NULL; 
	LPVOID lpFileMem  = 0; 
	LPVOID lpImageBase= 0; 
	
	PIMAGE_DOS_HEADER pDosHeader;
	PIMAGE_NT_HEADERS pNtHeaders;
	PIMAGE_DEBUG_DIRECTORY pDebugDir;

	DWORD DebugDirRva    = 0; 
	DWORD DebugDirSize   = 0; 
	DWORD DebugDirOffset = 0;

	bool DiskMode = false;  // If true, use file offsets instead of RVAs.
	
	// Validate parameters.
	if (!(fileName || moduleBase) || !pModuleInfo || *failureReason)
	{
		*failureReason = "null param";
		*failureErrorCode = 0;
		return false;
	}

	do // Process the file 
	{
		// Open the file and map it into memory 
		if (moduleBase)
			lpImageBase = moduleBase;
		else
		{
			DiskMode = true;

			hFile = CreateFile_UTF8( fileName, GENERIC_READ, FILE_SHARE_READ, NULL, 
			                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL ); 
	
			if( ( hFile == INVALID_HANDLE_VALUE ) || ( hFile == NULL ) ) 
			{
				*failureReason = "CreateFile";
				*failureErrorCode = GetLastError();
				break;
			}
	
			hFileMap = CreateFileMapping( hFile, NULL, PAGE_READONLY, 0, 0, NULL ); 
	
			if( hFileMap == NULL ) 
			{
				*failureReason = "CreateFileMapping";
				*failureErrorCode = GetLastError();
				break;
			}
		
			lpFileMem = MapViewOfFile( hFileMap, FILE_MAP_READ, 0, 0, 0 ); 
	
			if( lpFileMem == 0 ) 
			{
				*failureReason = "MapViewOfFile";
				*failureErrorCode = GetLastError();
				break;
			}

			lpImageBase = lpFileMem;
		}
		pDosHeader = (PIMAGE_DOS_HEADER)lpImageBase;

		// Is it a valid PE executable ? 
		if( !CheckDosHeader( pDosHeader, failureReason, failureErrorCode ) )
			break; 

		pNtHeaders = MakePtr( PIMAGE_NT_HEADERS, pDosHeader, pDosHeader->e_lfanew );
		if( !CheckNtHeaders( pNtHeaders, failureReason, failureErrorCode ) || !CheckSectionHeaders( pNtHeaders, failureReason, failureErrorCode ) ||
			!GetDebugDirectoryRVA( &pNtHeaders->OptionalHeader, &DebugDirRva, &DebugDirSize )) 
			break; 

		//printf("Preferred Load Address: %d\n", pNtHeaders->OptionalHeader.ImageBase);
		//pModuleInfo->BaseOfImage = pNtHeaders->OptionalHeader.ImageBase;
		if (!pModuleInfo->ImageSize)
			pModuleInfo->ImageSize = pNtHeaders->OptionalHeader.SizeOfImage;
		if (!pModuleInfo->TimeDateStamp)
			pModuleInfo->TimeDateStamp = pNtHeaders->FileHeader.TimeDateStamp;

		// Is the Debug directory present and valid?
		if( DebugDirRva == 0 ) 
		{
			*failureReason = "GetFileOffsetFromRVA:RVA";
			*failureErrorCode = DebugDirRva;
			break;
		}
		if( DebugDirSize == 0 )
		{
			*failureReason = "GetFileOffsetFromRVA:Size";
			*failureErrorCode = DebugDirSize;
			break;
		}

		// Find the debug directory.
		if ( DiskMode )
		{
			if( !GetFileOffsetFromRVA( pNtHeaders, DebugDirRva, &DebugDirOffset ) ) 
			{
				*failureReason = "GetFileOffsetFromRVA";
				*failureErrorCode = DebugDirRva;
				break;
			}
		}
		else
		{
			DebugDirOffset = DebugDirRva;
		}

		pDebugDir = MakePtr( PIMAGE_DEBUG_DIRECTORY, lpImageBase, DebugDirOffset ); 
		if( !CheckDebugDirectory( pDebugDir, DebugDirSize, failureReason, failureErrorCode ) ) 
			break; 

		// Display information about every entry in the debug directory 
		DumpDebugDirectoryEntries( (LPBYTE)lpImageBase, pDebugDir, DebugDirSize, pModuleInfo, failureReason, failureErrorCode, DiskMode ); 
	}
	while( 0 ); 

	// Cleanup 
	if( lpFileMem != 0 ) 
	{
		UnmapViewOfFile( lpFileMem );
	}

	if( hFileMap != NULL ) 
	{
		CloseHandle( hFileMap );
	}

	if( ( hFile != NULL ) && ( hFile != INVALID_HANDLE_VALUE ) ) 
	{
		CloseHandle( hFile );
	}

	// Complete 
	return true; 
}


///////////////////////////////////////////////////////////////////////////////
// Functions 
//

// 
// Check IMAGE_DOS_HEADER and determine whether the file is a PE executable 
// (according to the header contents) 
// 
// Return value: "true" if the header is valid and the file is a PE executable, 
//   "false" otherwise 
// 
static bool CheckDosHeader( PIMAGE_DOS_HEADER pDosHeader, const char **failureReason, int *failureErrorCode ) 
{
	// Check whether the header is valid and belongs to a PE executable 
	if( pDosHeader == 0 )
	{
		*failureReason = "CheckDosHeader:null";
		*failureErrorCode = 0;
		return false;
	}

	if( IsBadReadPtr( pDosHeader, sizeof(IMAGE_DOS_HEADER) ) )
	{
		// Invalid header
		*failureReason = "CheckDosHeader:Invalid header";
		*failureErrorCode = GetLastError();
		return false;
	}

	if( pDosHeader->e_magic != IMAGE_DOS_SIGNATURE )
	{
		// Not a PE executable 
		*failureReason = "CheckDosHeader:NotPeExe";
		*failureErrorCode = 0;
		return false;
	}

	return true; 
}

// 
// Check IMAGE_NT_HEADERS and determine whether the file is a PE executable 
// (according to the headers' contents) 
// 
// Return value: "true" if the headers are valid and the file is a PE executable, 
//   "false" otherwise 
// 
static bool CheckNtHeaders( PIMAGE_NT_HEADERS pNtHeaders, const char **failureReason, int *failureErrorCode ) 
{
	bool bPE32Plus = false;

	// Check the signature 
	if( pNtHeaders == 0 ) 
	{
		*failureReason = "CheckNtHeaders:null";
		*failureErrorCode = 0;
		return false;
	}
	if( IsBadReadPtr( pNtHeaders, sizeof(pNtHeaders->Signature) ) ) 
	{
		// Invalid header
		*failureReason = "CheckNtHeaders:InvalidHeader";
		*failureErrorCode = 0;
		return false;
	}

	if( pNtHeaders->Signature != IMAGE_NT_SIGNATURE ) 
	{
		// Not a PE executable
		*failureReason = "CheckNtHeaders:NotPe";
		*failureErrorCode = 0;
		return false;
	}

	// Check the file header 
	if( IsBadReadPtr( &pNtHeaders->FileHeader, sizeof(IMAGE_FILE_HEADER) ) )
	{
		// Invalid header
		*failureReason = "CheckNtHeaders:FileHeaderBadPtr";
		*failureErrorCode = GetLastError();
		return false;
	}

	if( IsBadReadPtr( &pNtHeaders->OptionalHeader, pNtHeaders->FileHeader.SizeOfOptionalHeader ) ) 
	{
		// Invalid size of the optional header 
		*failureReason = "CheckNtHeaders:OptHeaderBadPtr";
		*failureErrorCode = GetLastError();
		return false;
	}

	// Determine the format of the header 
		// If true, PE32+, otherwise PE32
	if( !IsPE32Plus( &pNtHeaders->OptionalHeader, &bPE32Plus ) ) 
	{
		// Probably invalid IMAGE_OPTIONAL_HEADER.Magic 
		*failureReason = "CheckNtHeaders:PE32PlusFailed";
		*failureErrorCode = 0;
		return false;
	}

	// Complete 
	return true; 
}

// 
// Lookup the section headers and check whether they are valid 
// 
// Return value: "true" if the headers are valid, "false" otherwise 
// 
static bool CheckSectionHeaders( PIMAGE_NT_HEADERS pNtHeaders, const char **failureReason, int *failureErrorCode ) 
{
	PIMAGE_SECTION_HEADER pSectionHeaders = IMAGE_FIRST_SECTION( pNtHeaders ); 

	if( pNtHeaders == 0 ) 
	{
		*failureReason = "CheckSectionHeaders:null";
		*failureErrorCode = 0;
		return false;
	}

	if( IsBadReadPtr( pSectionHeaders, pNtHeaders->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) ) ) 
	{
		// Invalid header 
		*failureReason = "CheckSectionHeaders:InvalidHeader";
		*failureErrorCode = GetLastError();
		return false;
	}

	return true; 
}

// 
// Checks whether the debug directory is valid 
// 
// Return value: "true" if the debug directory is valid, "false" if it is not 
// 
static bool CheckDebugDirectory( PIMAGE_DEBUG_DIRECTORY pDebugDir, DWORD DebugDirSize, const char **failureReason, int *failureErrorCode ) 
{
	if( ( pDebugDir == 0 ) || ( DebugDirSize == 0 ) ) 
	{
		*failureReason = "CheckDebugDirectory:null";
		*failureErrorCode = 0;
		return false;
	}

	if( IsBadReadPtr( pDebugDir, DebugDirSize ) ) 
	{
		// Invalid debug directory 
		*failureReason = "CheckDebugDirectory:InvalidDir";
		*failureErrorCode = GetLastError();
		return false;
	}

	if( DebugDirSize < sizeof(IMAGE_DEBUG_DIRECTORY) ) 
	{
		// Invalid size of the debug directory 
		*failureReason = "CheckDebugDirectory:InvalidSize";
		*failureErrorCode = DebugDirSize;
		return false;
	}

	return true; 
}

// 
// Check whether the specified IMAGE_OPTIONAL_HEADER belongs to 
// a PE32 or PE32+ file format 
// 
// Return value: "true" if succeeded (bPE32Plus contains "true" if the file 
//  format is PE32+, and "false" if the file format is PE32), 
//  "false" if failed 
// 
static bool IsPE32Plus( PIMAGE_OPTIONAL_HEADER pOptionalHeader, bool *bPE32Plus ) 
{
	// Note: The function does not check the header for validity. 
	// It assumes that the caller has performed all the necessary checks. 

	// IMAGE_OPTIONAL_HEADER.Magic field contains the value that allows 
	// to distinguish between PE32 and PE32+ formats 

	if( pOptionalHeader->Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ) 
	{
		// PE32 
		*bPE32Plus = false; 
	}
	else if( pOptionalHeader->Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ) 
	{
		// PE32+
		*bPE32Plus = true; 
	}
	else 
	{
		// Unknown value -> Report an error 
		*bPE32Plus = false; 
		return false; 
	}

	return true; 

}

// 
// Returns (in [out] parameters) the RVA and size of the debug directory, 
// using the information in IMAGE_OPTIONAL_HEADER.DebugDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]
// 
// Return value: "true" if succeeded, "false" if failed
// 
static bool GetDebugDirectoryRVA( PIMAGE_OPTIONAL_HEADER pOptionalHeader, DWORD* DebugDirRva, DWORD* DebugDirSize ) 
{
	bool bPE32Plus = false; 

	// Check parameters 
	if( pOptionalHeader == 0 ) 
	{
		assert( 0 ); 
		return false; 
	}

	// Determine the format of the PE executable 

	if( !IsPE32Plus( pOptionalHeader, &bPE32Plus ) ) 
	{
		// Probably invalid IMAGE_OPTIONAL_HEADER.Magic
		return false; 
	}

	// Obtain the debug directory RVA and size 
	if( bPE32Plus ) 
	{
		PIMAGE_OPTIONAL_HEADER64 pOptionalHeader64 = (PIMAGE_OPTIONAL_HEADER64)pOptionalHeader; 

		*DebugDirRva = pOptionalHeader64->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress; 
		*DebugDirSize = pOptionalHeader64->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size; 
	}
	else 
	{
		PIMAGE_OPTIONAL_HEADER32 pOptionalHeader32 = (PIMAGE_OPTIONAL_HEADER32)pOptionalHeader; 

		*DebugDirRva = pOptionalHeader32->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress; 
		*DebugDirSize = pOptionalHeader32->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size; 
	}

	if( ( *DebugDirRva == 0 ) && ( *DebugDirSize == 0 ) ) 
	{
		// No debug directory in the executable -> no debug information 
		return true; 
	}
	else if( ( *DebugDirRva == 0 ) || ( *DebugDirSize == 0 ) )
	{
		// Inconsistent data in the data directory 
		return false; 
	}

	// Complete 
	return true; 
}

// 
// The function walks through the section headers, finds out the section 
// the given RVA belongs to, and uses the section header to determine 
// the file offset that corresponds to the given RVA 
// 
// Return value: "true" if succeeded, "false" if failed 
// 
static bool GetFileOffsetFromRVA( PIMAGE_NT_HEADERS pNtHeaders, DWORD Rva, DWORD* FileOffset ) 
{
	bool bFound = false; 
	PIMAGE_SECTION_HEADER pSectionHeader;
	INT Diff;
	int i;

	// Check parameters 
	if( pNtHeaders == 0 ) 
	{
		assert( 0 ); 
		return false; 
	}

	// Look up the section the RVA belongs to 
	pSectionHeader = IMAGE_FIRST_SECTION( pNtHeaders );

	for( i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++, pSectionHeader++ ) 
	{
		DWORD SectionSize = pSectionHeader->Misc.VirtualSize; 

		if( SectionSize == 0 ) // compensate for Watcom linker strangeness, according to Matt Pietrek 
			SectionSize = pSectionHeader->SizeOfRawData; 

		if( ( Rva >= pSectionHeader->VirtualAddress ) && 
		    ( Rva < pSectionHeader->VirtualAddress + SectionSize ) ) 
		{
			// Yes, the RVA belongs to this section 
			bFound = true; 
			break; 
		}
	}

	if( !bFound ) 
	{
		// Section not found 
		return false; 
	}

	// Look up the file offset using the section header 
	Diff = (INT)( pSectionHeader->VirtualAddress - pSectionHeader->PointerToRawData ); 

	*FileOffset = Rva - Diff; 

	// Complete 
	return true; 
}

// ----------------------------------------------
// Dump functions

// 
// Walk through each entry in the debug directory and display information about it 
// 
static void DumpDebugDirectoryEntries( LPBYTE pImageBase, PIMAGE_DEBUG_DIRECTORY pDebugDir, DWORD DebugDirSize, PIMAGEHLP_MODULE64 pModuleInfo,
	const char **failureReason, int *failureErrorCode, bool DiskMode ) 
{
	int i;
	int best = 0;
	char *bestFailureReason = NULL;
	int bestFailureErrorCode = 0;

	// Determine the number of entries in the debug directory 
	int NumEntries = DebugDirSize / sizeof(IMAGE_DEBUG_DIRECTORY); 
	
	// Scan each entry.
	// Return after the first entry that has all the data we need; otherwise return with the error info for the best match.
	for( i = 0; i < NumEntries; i++, pDebugDir++ )
	{
		char *tempFailureReason;
		int tempFailureErrorCode;

		// Try to get debug
		int result = DumpDebugDirectoryEntry( pImageBase, pDebugDir, pModuleInfo, &tempFailureReason, &tempFailureErrorCode, DiskMode );
		if ( !result )
			return;

		// Save the error information.
		if ( result > best )
		{
			best = result;
			bestFailureReason = tempFailureReason;
			bestFailureErrorCode = tempFailureErrorCode;
		}
	}

	// Failed to find debug info: Save error information.
	*failureReason = bestFailureReason;
	*failureErrorCode = bestFailureErrorCode;
}

// 
// Display information about debug directory entry 
// 
// Return how far we got, or zero for total success.
static int DumpDebugDirectoryEntry( LPBYTE pImageBase, PIMAGE_DEBUG_DIRECTORY pDebugDir, PIMAGEHLP_MODULE64 pModuleInfo, const char **failureReason, int *failureErrorCode, bool DiskMode ) 
{
	LPBYTE pDebugInfo;
	DWORD CvSignature;
	CV_INFO_PDB70* pCvInfo;

	// Display information about the entry 
	if (pDebugDir->Type != IMAGE_DEBUG_TYPE_CODEVIEW )
	{
		*failureReason = "DumpDebugDirectoryEntry:Type";
		*failureErrorCode = 0;
		return 1;
	}

	if (DiskMode)
		pDebugInfo = pImageBase + pDebugDir->PointerToRawData; 
	else
		pDebugInfo = pImageBase + pDebugDir->AddressOfRawData; 
	CvSignature = *(DWORD*) pDebugInfo;

	if ( !pDebugInfo )
	{
		*failureReason = "DumpDebugDirectoryEntry:DebugInfo";
		*failureErrorCode = 0;
		return 2;
	}
	if ( IsBadReadPtr( pDebugInfo, pDebugDir->SizeOfData ) )
	{
		*failureReason = "DumpDebugDirectoryEntry:BadReadPtr";
		*failureErrorCode = GetLastError();
		return 3;
	}
	if ( pDebugDir->SizeOfData < sizeof(DWORD) )
	{
		*failureReason = "DumpDebugDirectoryEntry:SizeOfData";
		*failureErrorCode = 0;
		return 4;
	}
		
	if ( CvSignature != CV_SIGNATURE_RSDS )
	{
		*failureReason = "DumpDebugDirectoryEntry:Signature";
		*failureErrorCode = 0;
		return 5;
	}

	// RSDS -> PDB 7.00 
	pCvInfo = (CV_INFO_PDB70*)pDebugInfo; 

	if ( IsBadReadPtr( pDebugInfo, sizeof(CV_INFO_PDB70)) )
	{
		*failureReason = "DumpDebugDirectoryEntry:BadPdbGuid";
		*failureErrorCode = GetLastError();
		return 6;
	}
	if ( IsBadStringPtrA( (CHAR*)pCvInfo->PdbFileName, 256 ) )
	{
		*failureReason = "DumpDebugDirectoryEntry:BadPdbFilename";
		*failureErrorCode = GetLastError();
		return 7;
	}

	// Save the debug information.
	pModuleInfo->TimeDateStamp = pDebugDir->TimeDateStamp;
	pModuleInfo->PdbSig70 = pCvInfo->Signature;
	pModuleInfo->PdbAge = pCvInfo->Age;
	strcpy(pModuleInfo->LoadedPdbName, (char *)pCvInfo->PdbFileName);

	return 0;
}

#endif // if !PLATFORM_CONSOLE
