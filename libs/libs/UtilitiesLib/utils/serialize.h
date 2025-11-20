// serialize.h - provides simple binary serialization functions for storing structs
// these functions allow for a simple binary format: hierarchical, self describing,
// with traversal.  64 bytes overhead per struct

#ifndef __SERIALIZE_H
#define __SERIALIZE_H
#pragma once
GCC_SYSTEM

#include <stdio.h>
#include "endian.h"
#include "TextParserEnums.h"

///////////////////////////////////////// simple buffer files - just preloading entire file
// SimpleBuf functions just let you treat a file read to and from a large
// memory buffer as a normal file.  Actual file I/O happens on Open on a 
// Read file and Close on a Write file.  The semantics of all functions are
// the same as standard lib functions

typedef struct SimpleBuffer SimpleBuffer;
typedef struct SimpleBuffer *SimpleBufHandle;
typedef struct HogFile HogFile;
typedef enum enumBinaryReadFlags enumBinaryReadFlags;

SimpleBufHandle SimpleBufOpenWrite(const char* filename, int forcewrite, HogFile *hog_file, bool force_zero_timestamp, bool use_huge_initial_buffer);
SimpleBufHandle SimpleBufOpenRead(const char* filename, HogFile *hog_file);
void SimpleBufClose(SimpleBufHandle handle);
void SimpleBufCloseNoWriteIfEmpty(SimpleBufHandle handle);
int SimpleBufWrite(const void* data, int size, SimpleBufHandle handle);
void *SimpleBufReserve(int size, SimpleBufHandle handle);
int SimpleBufRead(void* data, int size, SimpleBufHandle handle);
int SimpleBufSeek(SimpleBufHandle handle, long offset, int origin);
int SimpleBufTell(SimpleBufHandle handle);
int SimpleBufAtEnd(const SimpleBufHandle handle);

// extra non file-io functions to avoid double mallocs
void *SimpleBufGetData(SimpleBufHandle handle,unsigned int *len);
void *SimpleBufGetDataCurrentPosition(SimpleBufHandle handle);
void *SimpleBufGetDataAndClose(SimpleBufHandle handle, SA_PRE_NN_FREE SA_POST_NN_VALID unsigned int *len); // Relinquishes ownership
SimpleBufHandle SimpleBufSetData(void *data,unsigned int len);
int SimpleBufGetSize(SimpleBufHandle handle);
const char *SimpleBufGetFilename(SimpleBufHandle handle);
HogFile *SimpleBufGetHogFile(SimpleBufHandle buf);

// endian-aware i/o - always assumes that file should be little-endian
__forceinline static int SimpleBufWriteU8(U8 val, SimpleBufHandle handle) { return SimpleBufWrite(&val, sizeof(U8), handle); }
__forceinline static int SimpleBufWriteU16(U16 val, SimpleBufHandle handle) { U16 x = endianSwapIfBig(U16, val); return SimpleBufWrite(&x, sizeof(U16), handle); }
__forceinline static int SimpleBufWriteU32(U32 val, SimpleBufHandle handle) { U32 x = endianSwapIfBig(U32, val); return SimpleBufWrite(&x, sizeof(U32), handle); }
__forceinline static int SimpleBufWriteTime32(__time32_t val, SimpleBufHandle handle) { U32 x = endianSwapIfBig(__time32_t, val); return SimpleBufWrite(&x, sizeof(__time32_t), handle); }
__forceinline static int SimpleBufWriteU64(U64 val, SimpleBufHandle handle) { U64 x = endianSwapIfBig(U64, val); return SimpleBufWrite(&x, sizeof(U64), handle); }
__forceinline static int SimpleBufWriteF32(F32 val, SimpleBufHandle handle) { F32 x = endianSwapIfBig(F32, val); return SimpleBufWrite(&x, sizeof(F32), handle); }
__forceinline static int SimpleBufWriteF64(F64 val, SimpleBufHandle handle) { F64 x = endianSwapIfBig(F64, val); return SimpleBufWrite(&x, sizeof(F64), handle); }

__forceinline static int SimpleBufReadU8(U8* val, SimpleBufHandle handle) { return SimpleBufRead(val, sizeof(U8), handle); }
__forceinline static int SimpleBufReadU16(U16* val, SimpleBufHandle handle) { U16 x; int ret = SimpleBufRead(&x, sizeof(U16), handle); *val = endianSwapIfBig(U16, x); return ret; }
__forceinline static int SimpleBufReadU32(U32* val, SimpleBufHandle handle) { U32 x; int ret = SimpleBufRead(&x, sizeof(U32), handle); *val = endianSwapIfBig(U32, x); return ret; }
__forceinline static int SimpleBufReadTime32(__time32_t* val, SimpleBufHandle handle) { __time32_t x; int ret = SimpleBufRead(&x, sizeof(__time32_t), handle); *val = endianSwapIfBig(__time32_t, x); return ret; }
__forceinline static int SimpleBufReadU64(U64* val, SimpleBufHandle handle) { U64 x; int ret = SimpleBufRead(&x, sizeof(U64), handle); *val = endianSwapIfBig(U64, x); return ret; }
__forceinline static int SimpleBufReadF32(F32* val, SimpleBufHandle handle) { F32 x; int ret = SimpleBufRead(&x, sizeof(F32), handle); *val = endianSwapIfBig(F32, x); return ret; }
__forceinline static int SimpleBufReadF64(F64* val, SimpleBufHandle handle) { F64 x; int ret = SimpleBufRead(&x, sizeof(F64), handle); *val = endianSwapIfBig(F64, x); return ret; }

int SimpleBufWriteU16Array(const U16 *data, int count, SimpleBufHandle handle);
int SimpleBufWriteU32Array(const U32 *data, int count, SimpleBufHandle handle);
int SimpleBufWriteU64Array(const U64 *data, int count, SimpleBufHandle handle);
int SimpleBufWriteF32Array(const F32 *data, int count, SimpleBufHandle handle);
int SimpleBufWriteF64Array(const F64 *data, int count, SimpleBufHandle handle);
int SimpleBufWriteString(const char *str, SimpleBufHandle handle);

int SimpleBufReadU16Array(U16 *data, int count, SimpleBufHandle handle);
int SimpleBufReadU32Array(U32 *data, int count, SimpleBufHandle handle);
int SimpleBufReadU64Array(U64 *data, int count, SimpleBufHandle handle);
int SimpleBufReadF32Array(F32 *data, int count, SimpleBufHandle handle);
int SimpleBufReadF64Array(F64 *data, int count, SimpleBufHandle handle);
int SimpleBufReadString(char **str, SimpleBufHandle handle);

int SimpleBufFilenameComparator(const SimpleBuffer **pHandle1, const SimpleBuffer **pHandle2);

////////////////////////////////////// structured files
// Serialize functions encapsulate a structured binary data file.
// Data is stored in a series of named structure segments.  Segments
// may be included in each other heirarchically.  Each Struct is
// a Header followed by Data of any length.  Data length is recorded
// in the Header.

#define MAX_FILETYPE_LEN	4096
#define MAX_STRUCTNAME_LEN	4096

// read or write
void SerializeClose(SimpleBufHandle file);

// pascal strings
int WritePascalString(SimpleBufHandle file, const char* str); // write a string to a file, with padding for DWORD lines, returns bytes written
int ReadPascalString(SimpleBufHandle file, char* str, int strsize); // read a corresponding string, returns bytes read
int ReadPascalStringIntoEString(SimpleBufHandle file, char **ppEString); // read a corresponding string, returns bytes read

// write functions
SimpleBufHandle SerializeWriteOpen(const char* filename, char* filetype, int build, HogFile *hog_file, bool force_zero_timestamp, bool huge_initial_size,
	enumBinaryFileHeaderFlags eHeaderFlagsToWrite);
int SerializeWriteStruct(SimpleBufHandle sfile, char* structname, int size, void* structptr);	// returns length written
int SerializeWriteHeader(SimpleBufHandle sfile, char* structname, int size, long* loc);		// returns length written
int SerializeWriteData(SimpleBufHandle sfile, int size, void* dataptr);				// returns length written
int SerializePatchHeader(SimpleBufHandle sfile, int size, long loc);					// use loc returned by SerializeWriteHeader

// read functions
SimpleBufHandle SerializeReadOpen(const char* filename, char* filetype, int crc1, int crc2, enumBinaryReadFlags eReadFlags, HogFile *hog_file);
int SerializeNextStruct(SimpleBufHandle sfile, char* structname, int namesize, int* size); // loads name, size.  returns success
int SerializeSkipStruct(SimpleBufHandle sfile);			// returns length skipped
int SerializeReadStruct(SimpleBufHandle sfile, char* structname, int size, void* structptr);	// returns length read
int SerializeReadHeader(SimpleBufHandle sfile, char* structname, int namesize, int* size); // loads name, size. returns success
int SerializeReadData(SimpleBufHandle sfile, int size, void* dataptr);					// returns length read

int ReadPascalStringLen(SimpleBufHandle file,unsigned short * len);
int ReadPascalStringOfLength(SimpleBufHandle file, char* str, int len);

__inline int ReadPascalStringLen(SimpleBufHandle file,unsigned short * len)
{
	int re;

	re = SimpleBufReadU16(len, file);
	if (!re) 
	{ 
		printf("ReadStringLen: couldn't read len\n"); 
		return re; 
	}

	return re;
}

// this function assumes the length has already been read.  This is to facilitate fast action
__inline int ReadPascalStringOfLength(SimpleBufHandle file, char* str, int len)
{
	int re;
	int paddingreq;

	str[0] = 0;
	paddingreq = (4 - (len + sizeof(unsigned short)) % 4) % 4;

	re = SimpleBufRead(str, sizeof(char) * len, file);
	str[len] = 0;
	if (re != len)
	{
		printf("ReadPascalStringOfLength: couldn't read entire string\n");
		return re;
	}
	SimpleBufSeek(file, paddingreq, SEEK_CUR);

	return len+paddingreq;
}

#endif // __SERIALIZE_H