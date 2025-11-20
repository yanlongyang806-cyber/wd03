// serialize.c - provides simple binary serialization functions for storing structs
// these functions allow for a simple binary format: hierarchical, self describing,
// with traversal.  64 bytes overhead per struct

#include "serialize.h"
#include <stdio.h>
#include <stdlib.h>
#if !_PS3
#include <io.h>
#include "windefinclude.h"
#endif
#include "file.h"
#include "error.h"
#include "utils.h"
#include "estring.h"
#include "hoglib.h"
#include "TextParserEnums.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Unknown);); // Should be 0 bytes

////////////////////////////////////////////// simple buffer files

#define INITIAL_BUF_SIZE 100
#define HUGE_INITIAL_BUF_SIZE (200 * 1024 * 1024)
typedef struct SimpleBuffer {
	char* data;
	int filesize;	// logical size of file
	int memsize;	// allocated size
	int curposition;
	int writing;	// are we writing or reading?
	int forcewrite;
	HogFile *hog_file;
	bool force_zero_timestamp;
	enumBinaryFileHeaderFlags eHeaderFlags; //flags read from the buffer during SerializeReadOpen
	char filename[CRYPTIC_MAX_PATH];
} SimpleBuffer;

int SimpleBufFilenameComparator(const SimpleBuffer **pHandle1, const SimpleBuffer **pHandle2)
{
	const SimpleBuffer *buf1 = *pHandle1;
	const SimpleBuffer *buf2 = *pHandle2;
	intptr_t t;

	t = ((intptr_t)buf2->hog_file) - ((intptr_t)buf1->hog_file);
	if (t)
		return SIGN(t);

	return stricmp(buf1->filename, buf2->filename);
}

SimpleBufHandle SimpleBufOpenWrite(const char* filename, int forcewrite, HogFile *hog_file, bool force_zero_timestamp, bool use_huge_initial_buffer)
{
	SimpleBuffer* buf;
	buf = calloc(sizeof(SimpleBuffer), 1);
	buf->filesize = 0;
	buf->memsize = use_huge_initial_buffer ? HUGE_INITIAL_BUF_SIZE : INITIAL_BUF_SIZE;
	buf->data = malloc(buf->memsize);
	buf->writing = 1;
	buf->forcewrite = forcewrite;
	buf->hog_file = hog_file;
	buf->force_zero_timestamp = force_zero_timestamp;
	strcpy(buf->filename, filename);
	return (SimpleBufHandle)buf;
}

SimpleBufHandle SimpleBufOpenRead(const char* filename, HogFile *hog_file)
{
	SimpleBuffer* buf;
	//FILE *file;
	//int re;

	errorIsDuringDataLoadingInc(filename);
	buf = calloc(sizeof(SimpleBuffer), 1);
	buf->filesize = 0;
	buf->writing = 0;
	buf->hog_file = hog_file;
	strcpy(buf->filename, filename);

	if (hog_file)
	{
		HogFileIndex file_index = hogFileFind(hog_file, filename);
		if (file_index != HOG_INVALID_INDEX) {
			bool checksum_valid=true;
			if (buf->data = hogFileExtract(hog_file, file_index, &buf->filesize, &checksum_valid))
				assert(checksum_valid);
		}
	}
	else
	{
		bool checksum_valid;
		// one pass only for compatibility with zip files
		// if coming from a hogg, we'll also verify the integrity
		if (buf->data = fileAllocWithCRCCheck(filename, &buf->filesize, &checksum_valid))
			assert(checksum_valid);
	}

	if (!buf->data)
	{
		free(buf);
		errorIsDuringDataLoadingDec();
		return 0;
	}
	buf->memsize = buf->filesize;
	buf->curposition = 0;

	errorIsDuringDataLoadingDec();
	return (SimpleBufHandle)buf;
}

void SimpleBufClose(SimpleBufHandle buf)
{
	if (buf->writing && buf->filename[0])
	{
		if (buf->hog_file)
		{
			hogFileModifyUpdateNamed(buf->hog_file, buf->filename, buf->data, buf->filesize, buf->force_zero_timestamp ? 0 : _time32(NULL), NULL);
			buf->data = NULL;
		}
		else
		{
			FILE *file;
			char fullpath[MAX_PATH];

			// make it writable if it exists
			if (buf->forcewrite) _chmod(buf->filename, _S_IREAD | _S_IWRITE);

			// one pass only for compatibility with zip files
			fileLocateWriteBin(buf->filename, fullpath);
			makeDirectoriesForFile(fullpath);
			file = fileOpenBin(buf->filename, "wb");
			if (!file) 
			{
				printf("SimpleBufClose: couldn't write to %s\n", buf->filename);
			}
			else // opened file ok
			{
				fwrite(buf->data, sizeof(char), buf->filesize, file);
				fclose(file);
			}
		}
	}
	// don't do anything for a read

	SAFE_FREE(buf->data);
	free(buf);
}

void SimpleBufCloseNoWriteIfEmpty(SimpleBufHandle buf)
{
	if (buf->filesize)
	{
		SimpleBufClose(buf);
	}
	else
	{
		SAFE_FREE(buf->data);
		free(buf);
	}
}

void *SimpleBufGetData(SimpleBufHandle buf, U32 *len)
{
	*len = buf->filesize;
	return buf->data;
}

void *SimpleBufGetDataCurrentPosition(SimpleBufHandle buf)
{
	return buf->data + buf->curposition;
}

void *SimpleBufGetDataAndClose(SimpleBufHandle buf, unsigned int *len) // Relinquishes ownership
{
	void *ret = buf->data;

	*len = buf->filesize;
	buf->data = NULL;
	SimpleBufClose(buf);
	return ret;
}

SimpleBufHandle SimpleBufSetData(void *data,U32 len)
{
	SimpleBuffer* buf;

	buf = calloc(sizeof(SimpleBuffer),1);
	buf->data = data;
	buf->filesize = buf->memsize = len;
	return (SimpleBufHandle)buf;
}

int SimpleBufGetSize(SimpleBufHandle buf)
{
	return buf->filesize;
}

const char *SimpleBufGetFilename(SimpleBufHandle buf)
{
	return buf->filename;
}

HogFile *SimpleBufGetHogFile(SimpleBufHandle buf)
{
	return buf->hog_file;
}

void SimpleBufResize(SimpleBufHandle buf)
{
	if (buf->memsize < INITIAL_BUF_SIZE) buf->memsize = INITIAL_BUF_SIZE;
	else buf->memsize *= 2;
	buf->data = realloc(buf->data, buf->memsize);
}

int SimpleBufWrite(const void* data, int size, SimpleBufHandle buf)
{
	while (buf->curposition + size > buf->memsize) 
		SimpleBufResize(buf);

	if (buf->curposition + size > buf->filesize)
		buf->filesize = buf->curposition + size;
	memcpy(buf->data + buf->curposition, data, size);
	buf->curposition += size;
	return size;
}

void *SimpleBufReserve(int size, SimpleBufHandle buf)
{
	void *ret;

	while (buf->curposition + size > buf->memsize) 
		SimpleBufResize(buf);

	if (buf->curposition + size > buf->filesize)
		buf->filesize = buf->curposition + size;
	ret = buf->data + buf->curposition;
	buf->curposition += size;
	return ret;
}

int SimpleBufRead(void* data, int size, SimpleBufHandle buf)
{
	if (buf->curposition + size > buf->filesize)
		size = buf->filesize - buf->curposition;

	if (data)
		memcpy(data, buf->data + buf->curposition, size);
	buf->curposition += size;
	return size;
}

// Returns actual count, not actual size!
int SimpleBufFRead(void* data, int size, int count, SimpleBufHandle buf)
{
	int actual_data_size = size * count;
	int actual_count = count;
	if (buf->curposition + actual_data_size > buf->filesize)
	{
		ErrorDetailsf("Buffer in hogg %s, size %d count %d s*c %d avail %d", buf->filename, size, count, size * count, buf->filesize - buf->curposition);
		ErrorFilenamef(hogFileGetArchiveFileName(buf->hog_file), "Reading past end of file stored inside hogg.");
		actual_data_size = buf->filesize - buf->curposition;
		actual_count = actual_data_size / size;
		actual_data_size -= actual_data_size % size;
	}

	if (data)
		memcpy(data, buf->data + buf->curposition, actual_data_size);
	buf->curposition += actual_data_size;
	return actual_count;
}

int SimpleBufWriteU16Array(const U16 *data, int count, SimpleBufHandle handle)
{
	int size = count * sizeof(U16);
	U16 *dest = SimpleBufReserve(size, handle);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < count; ++i)
			dest[i] = endianSwapU16(data[i]);
	}
	else
	{
		memcpy(dest, data, size);
	}
	return size;
}

int SimpleBufWriteU32Array(const U32 *data, int count, SimpleBufHandle handle)
{
	int size = count * sizeof(U32);
	U32 *dest = SimpleBufReserve(size, handle);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < count; ++i)
			dest[i] = endianSwapU32(data[i]);
	}
	else
	{
		memcpy(dest, data, size);
	}
	return size;
}

int SimpleBufWriteU64Array(const U64 *data, int count, SimpleBufHandle handle)
{
	int size = count * sizeof(U64);
	U64 *dest = SimpleBufReserve(size, handle);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < count; ++i)
			dest[i] = endianSwapU64(data[i]);
	}
	else
	{
		memcpy(dest, data, size);
	}
	return size;
}

int SimpleBufWriteF32Array(const F32 *data, int count, SimpleBufHandle handle)
{
	int size = count * sizeof(F32);
	F32 *dest = SimpleBufReserve(size, handle);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < count; ++i)
			dest[i] = endianSwapF32(data[i]);
	}
	else
	{
		memcpy(dest, data, size);
	}
	return size;
}

int SimpleBufWriteF64Array(const F64 *data, int count, SimpleBufHandle handle)
{
	int size = count * sizeof(F64);
	F64 *dest = SimpleBufReserve(size, handle);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < count; ++i)
			dest[i] = endianSwapF64(data[i]);
	}
	else
	{
		memcpy(dest, data, size);
	}
	return size;
}

int SimpleBufWriteString(const char *str, SimpleBufHandle handle)
{
	if (str)
	{
		int size = (int)strlen(str) + 1;
		char *dest = SimpleBufReserve(size, handle);
		strcpy_s(dest, size, str);
		return size;
	}
	return SimpleBufWriteU8(0, handle);
}

int SimpleBufReadU16Array(U16 *data, int count, SimpleBufHandle handle)
{
	int size;
	count = SimpleBufFRead(data, sizeof(U16), count, handle);
	size = count * sizeof(U16);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < count; ++i)
			data[i] = endianSwapU16(data[i]);
	}
	return size;
}

int SimpleBufReadU32Array(U32 *data, int count, SimpleBufHandle handle)
{
	int size;
	count = SimpleBufFRead(data, sizeof(U32), count, handle);
	size = count * sizeof(U32);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < count; ++i)
			data[i] = endianSwapU32(data[i]);
	}
	return size;
}

int SimpleBufReadU64Array(U64 *data, int count, SimpleBufHandle handle)
{
	int size;
	count = SimpleBufFRead(data, sizeof(U64), count, handle);
	size = count * sizeof(U64);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < count; ++i)
			data[i] = endianSwapU64(data[i]);
	}
	return size;
}

int SimpleBufReadF32Array(F32 *data, int count, SimpleBufHandle handle)
{
	int size;
	count = SimpleBufFRead(data, sizeof(F32), count, handle);
	size = count * sizeof(F32);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < count; ++i)
			data[i] = endianSwapF32(data[i]);
	}
	return size;
}

int SimpleBufReadF64Array(F64 *data, int count, SimpleBufHandle handle)
{
	int size;
	count = SimpleBufFRead(data, sizeof(F64), count, handle);
	size = count * sizeof(F64);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < count; ++i)
			data[i] = endianSwapF64(data[i]);
	}
	return size;
}

int SimpleBufReadString(char **str, SimpleBufHandle buf)
{
	int size = 0;

	if (buf->curposition >= buf->filesize)
	{
		*str = NULL;
		return 0;
	}

	*str = buf->data + buf->curposition;
	do
	{
		++buf->curposition;
		++size;
	} while (buf->curposition < buf->filesize && (*str)[size-1]);

	if ((*str)[size-1])
	{
		// hit the end of the buffer without finding a null terminator
		*str = NULL;
		return 0;
	}

	return size;
}

int SimpleBufSeek(SimpleBufHandle buf, long offset, int origin)
{
	if (origin == SEEK_END) buf->curposition = buf->filesize;
	else if (origin == SEEK_SET) buf->curposition = 0;
	// SEEK_CUR ok

	buf->curposition += offset;
	if (buf->curposition < 0)
		buf->curposition = 0;
	while (buf->curposition > buf->memsize)
		SimpleBufResize(buf);
	if (buf->curposition > buf->filesize)
		buf->filesize = buf->curposition;
	return 0;
}

int SimpleBufTell(SimpleBufHandle buf)
{
	return buf->curposition;
}

int SimpleBufAtEnd(const SimpleBufHandle buf)
{
	return buf->curposition == buf->filesize;
}



/////////////////////////////////////////////////////////////// internal util
static char cryptic_sig[] = "CrypticS";
#define cryptic_sig_len 8

int WritePascalString(SimpleBufHandle file, const char* str) // write a string to a file, with padding for DWORD lines, returns bytes written
{
	int wr1, wr2, wr3;
	int zero = 0;
	size_t lenLong;
	unsigned short len;
	int paddingreq;

	if (!str) str = "";	// ok to write an empty string
	lenLong = strlen(str);
	len = (unsigned short)lenLong;

	assertmsg(len == lenLong, "Can't bin strings longer than 64k! Data loss will occur.");
	paddingreq = (4 - (len + sizeof(unsigned short)) % 4) % 4;
	wr1 = SimpleBufWriteU16(len, file);
	wr2 = SimpleBufWrite((void *) str, sizeof(char) * len, file);
	wr3 = SimpleBufWrite(&zero, sizeof(char) * paddingreq, file);
	if (wr2 != len || wr3 != paddingreq) printf("WritePascalString: failed writing %s\n", str);
	return sizeof(unsigned short) + wr2 + wr3;
}

int ReadPascalString(SimpleBufHandle file, char* str, int strsize) // read a corresponding string, returns bytes read
{
	int re;
	unsigned short len;
	int paddingreq;

	str[0] = 0;
	re = SimpleBufReadU16(&len, file);
	if (!re) 
	{ 
		printf("ReadPascalString: couldn't read len\n"); 
		return re; 
	}
	paddingreq = (4 - (len + sizeof(unsigned short)) % 4) % 4;
	if (len >= strsize) 
	{ 
		printf("ReadPascalString: string read too long for size\n");
		SimpleBufSeek(file, paddingreq + len, SEEK_CUR);
		return sizeof(unsigned short) + paddingreq + len;
	}
	re = SimpleBufRead(str, sizeof(char) * len, file);
	str[len] = 0;
	if (re != len) { printf("ReadPascalString: couldn't read entire string\n"); return sizeof(unsigned short) + paddingreq + len; }
	SimpleBufSeek(file, paddingreq, SEEK_CUR);
	return sizeof(unsigned short) + paddingreq + len;
}

int ReadPascalStringIntoEString(SimpleBufHandle file, char **ppEString)
{
	int re;
	unsigned short len;
	int paddingreq;

	estrPrintf(ppEString, "");

	re = SimpleBufReadU16(&len, file);
	if (!re) 
	{ 
		printf("ReadPascalStringIntoEString: couldn't read len\n"); 
		return re; 
	}
	paddingreq = (4 - (len + sizeof(unsigned short)) % 4) % 4;

	estrSetSize(ppEString, len);

	re = SimpleBufRead(*ppEString, sizeof(char) * len, file);
	(*ppEString)[len] = 0;
	if (re != len) { printf("ReadPascalString: couldn't read entire string\n"); return sizeof(unsigned short) + paddingreq + len; }
	SimpleBufSeek(file, paddingreq, SEEK_CUR);
	return sizeof(unsigned short) + paddingreq + len;
}


static int SkipPascalString(SimpleBufHandle file) // returns bytes skipped
{
	int paddingreq;
	unsigned short len = 0;
	SimpleBufReadU16(&len, file);
	paddingreq = (4 - (len + sizeof(unsigned short)) % 4) % 4;
	SimpleBufSeek(file, len + paddingreq, SEEK_CUR);
	return sizeof(unsigned short) + len + paddingreq;
}

/////////////////////////////////////////////////////////// writing functions
SimpleBufHandle SerializeWriteOpen(const char* filename, char* filetype, int build, 
	HogFile *hog_file, bool force_zero_timestamp, bool huge_initial_Size, enumBinaryFileHeaderFlags eHeaderFlagsToWrite)
{
	int wr;
	SimpleBufHandle result = SimpleBufOpenWrite(filename, 1, hog_file, force_zero_timestamp, huge_initial_Size);
	if (!result) 
	{
		printf("SerializeWriteOpen: failed to open %s\n", filename);
		return 0;
	}

	// signature
	SimpleBufWrite(cryptic_sig, cryptic_sig_len, result);
	wr = SimpleBufWriteU32((U32)build, result);
	if (!wr) 
	{ 
		printf("SerializeWriteOpen: failed writing build to file\n"); 
		SimpleBufClose(result); 
		return 0; 
	}
	wr = SimpleBufWriteU32((U32)eHeaderFlagsToWrite, result);
	if (!wr) 
	{ 
		printf("SerializeWriteOpen: failed writing header flags to file\n"); 
		SimpleBufClose(result); 
		return 0; 
	}

	WritePascalString(result, filetype);

	return result;
}

void SerializeClose(SimpleBufHandle sfile)
{
	SimpleBufClose(sfile);
}

int SerializeWriteStruct(SimpleBufHandle sfile, char* structname, int size, void* structptr)	// returns length written
{
	int sum;
	int wr;
	sum = WritePascalString(sfile, structname); 
	wr = SimpleBufWriteU32(size, sfile);
	if (!wr) printf("SerializeWriteStruct: failed writing size\n");
	wr = SimpleBufWrite(structptr, size, sfile);
	if (!wr) printf("SerializeWriteStruct: failed writing struct\n");
	return sum + sizeof(U32) + size;
}

int SerializeWriteHeader(SimpleBufHandle sfile, char* structname, int size, long* loc)		// returns length written
{
	int sum;
	int wr;
	sum = WritePascalString(sfile, structname);
	if (loc) *loc = SimpleBufTell(sfile);	// location for size later
	wr = SimpleBufWriteU32(size, sfile);
	if (!wr) printf("SerializeWriteHeader: failed writing size\n");
	return sum + sizeof(int);
}

int SerializeWriteData(SimpleBufHandle sfile, int size, void* dataptr)				// returns length written
{
	int wr = SimpleBufWrite(dataptr, size, sfile);
	if (!wr) printf("SerializeWriteData: failed writing data\n");
	return size;
}

int SerializePatchHeader(SimpleBufHandle sfile, int size, long loc)					// use loc returned by SerializeWriteHeader
{
	int wr;
	long cur = SimpleBufTell(sfile);
	SimpleBufSeek(sfile, loc, SEEK_SET);
	wr = SimpleBufWriteU32(size, sfile);
	if (!wr) printf("SerializePatchHeader: failed patching size\n");
	SimpleBufSeek(sfile, cur, SEEK_SET);
	return wr;
}

///////////////////////////////////////////////////////////////////// read functions
SimpleBufHandle SerializeReadOpen(const char* filename, char* filetype, int crc1, int crc2, enumBinaryReadFlags eReadFlags, HogFile *hog_file)
{
	SimpleBufHandle result;
	int readbuild = 0;
	char readsig[cryptic_sig_len+1];
	char readtype[MAX_FILETYPE_LEN];
	U32 iTempFlags;

	result = SimpleBufOpenRead(filename, hog_file);
	if (!result) 
	{ 
		verbose_printf("SerializeReadOpen: failed to open %s\n", filename); 
		return NULL; 
	}

	// signature
	readsig[0] = 0;
	SimpleBufRead(readsig, cryptic_sig_len, result);
	readsig[cryptic_sig_len] = 0;
	SimpleBufReadU32((U32*)&readbuild, result);
	SimpleBufReadU32(&iTempFlags, result);
	result->eHeaderFlags = iTempFlags;

	if (eReadFlags & BINARYREADFLAG_REQUIRE_NO_ERRORS_FLAG)
	{
		if (!(result->eHeaderFlags & BINARYHEADERFLAG_NO_DATA_ERRORS))
		{
			verbose_printf("SerializeReadOpen: got BINARYREADFLAG_REQUIRE_NO_ERRORS_FLAG but not BINARYHEADERFLAG_NO_DATA_ERRORS");
			SimpleBufClose(result);
			return 0;
		}
	}
	
	readtype[0] = 0;
	ReadPascalString(result, readtype, MAX_FILETYPE_LEN);
	if (strcmp(cryptic_sig, readsig) != 0 ||
		strcmp(readtype, filetype) != 0 ||
		(!(eReadFlags & BINARYREADFLAG_IGNORE_CRC) && (readbuild != crc1 && readbuild != crc2)))
	{
		if (strcmp(cryptic_sig, readsig) != 0) {
			verbose_printf("SerializeReadOpen: invalid signature on file %s (got:%s expected:%s)\n", filename, readsig, cryptic_sig);
		} else if (strcmp(readtype, filetype) != 0) {
			verbose_printf("SerializeReadOpen: wrong file type on %s (got:%s expected:%s)\n", filename, readtype, filetype);
		} else if (readbuild != crc1 && readbuild != crc2) {
			verbose_printf("SerializeReadOpen: wrong ParseTable CRC on %s (got:0x%X expected:0x%X)\n", filename, readbuild, crc1);
		} 
		if(!fileIsUsingDevData())
		{
			if (!(eReadFlags & BINARYREADFLAG_NONMATCHING_SIGNATURE_NON_FATAL))
			{
				FatalErrorf("SerializeReadOpen: invalid signature on file %s", filename);
			}
		}
		SimpleBufClose(result);
		return 0;
	} else if (readbuild != crc1 && readbuild != crc2) {
		verbose_printf("SerializeReadOpen: wrong ParseTable CRC on %s (got:0x%X expected:0x%X), but ignoring - loading anyway\n", filename, readbuild, crc1);
	}

	return result;
}

int SerializeNextStruct(SimpleBufHandle sfile, char* structname, int namesize, int* size) // loads name, size.  returns success
{
	long cur = SimpleBufTell(sfile);
	int re;
	int success = 1;

	ReadPascalString(sfile, structname, namesize);
	if (size)
	{
		re = SimpleBufReadU32((U32*)size, sfile);
		if (!re) { printf("SerializeNextStruct: failed reading size\n"); *size = 0; success = 0; }
	}
	SimpleBufSeek(sfile, cur, SEEK_SET);
	return success;
}

int SerializeSkipStruct(SimpleBufHandle sfile)			// returns length skipped
{
	int size;
	int re;
	int sum;

	sum = SkipPascalString(sfile);
	re = SimpleBufReadU32((U32*)&size, sfile);
	if (!re) { return sum; } // end of file
	SimpleBufSeek(sfile, size, SEEK_CUR);
	return sum + sizeof(int) + size;
}

int SerializeReadStruct(SimpleBufHandle sfile, char* structname, int size, void* structptr)	// returns length read
{
	int sum;
	int re;
	char readtype[MAX_STRUCTNAME_LEN];
	int readsize;

	// header
	sum = ReadPascalString(sfile, readtype, MAX_STRUCTNAME_LEN);
	re = SimpleBufReadU32((U32*)&readsize, sfile);
	if (!re) { printf("SerializeReadStruct: failed reading header\n"); return 0; }
	if (readsize != size || strcmp(structname, readtype) != 0)
	{
		printf("SerializeReadStruct: encountered unexpected structure, skipping\n");
		SimpleBufSeek(sfile, size, SEEK_CUR);
		return sum + size;
	}
	
	// data
	re = SimpleBufRead(structptr, sizeof(char) * size, sfile);
	if (re != size) { printf("SerializeReadStruct: failed reading data\n"); }
	return sum + sizeof(int) + size;
}

int SerializeReadHeader(SimpleBufHandle sfile, char* structname, int namesize, int* size) // loads name, size. returns success
{
	int re;

	// header
	if (!ReadPascalString(sfile, structname, namesize)) return 0;
	if (size)
	{
		re = SimpleBufReadU32((U32*)size, sfile);
		if (!re) { printf("SerializeReadHeader: failed reading size\n"); return 0; }
	}
	else SimpleBufSeek(sfile, sizeof(int), SEEK_CUR); // skip size
	return 1;
}

int SerializeReadData(SimpleBufHandle sfile, int size, void* dataptr)					// returns length read
{
	int re = SimpleBufRead(dataptr, sizeof(char) * size, sfile);
	if (re != size) printf("SerializeReadData: failed reading data\n");
	return re;
}
