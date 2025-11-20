/***************************************************************************



***************************************************************************/
#include "piglib.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include "utils.h"
#include "zutils.h"
#include "GlobalTypes.h"
#include "sysutil.h"

#include "strings_opt.h"
#include "../../3rdparty/zlib/zlib.h"
#include "network/crypt.h"
#include "timing.h"
#include "hoglib.h"
#include "fileutil2.h"
#include "endian.h"
#include "ScratchStack.h"
#include "earray.h"
#include "FolderCache.h"
#include "systemspecs.h"
#include "MemoryMonitor.h"
#include "MemAlloc.h"
#include "MemLog.h"
#include "trivia.h"
#include "UTF8.h"

extern MemLog hogmemlog;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

#if _PS3
int pig_debug=0;
#else
int pig_debug=0;
#endif

int pig_disabled=0;

int pig_compression=9;

void pigDisablePiggs(int disabled)
{
	pig_disabled = disabled ? 1 : 0;
}


void dumpMemoryToFile(void *mem, unsigned int count)
{
	char fn[MAX_PATH];
	static int callcount=0;
	int handle;

#if _PS3
	sprintf(fn, "/app_home/memory_%d.txt", callcount);
#elif _XBOX
	sprintf(fn, "devkit:\\memory_%d.txt", callcount);
#else
	sprintf(fn, "C:\\memory_%d.txt", callcount);
#endif
	callcount++;

	handle = open(fn, _O_RDWR | _O_BINARY | _O_CREAT | _O_TRUNC);
	if (handle != -1) {
		int written = _write(handle, mem, count);
		if (written == -1) {
			OutputDebugString(L"Error ");
			switch(errno)
			{
			xcase EBADF:
				OutputDebugString(L"Bad file descriptor!");
			xcase ENOSPC:
				OutputDebugString(L"No space left on device!");
			xcase EINVAL:
				OutputDebugString(L"Invalid parameter: buffer was NULL!");
			}
		} else {
			OutputDebugString(L"Wrote to ");
			OutputDebugString_UTF8(fn);
			OutputDebugString(L".\n");
		}

		close(handle);
	} else {
		OutputDebugString(L"Could not open ");
		OutputDebugString_UTF8(fn);
		OutputDebugString(L" for writing\n");
	}
}

#define UNZIP_BLOCK_SIZE	32768

U32 PigExtractBytesInternal(HogFile *hog_file, FILE *file, int file_index, void *buf, U32 pos, U32 size, U64 fileoffset, U32 filesize, U32 pack_size)
{
	U32 numread, zipped=pack_size > 0;
	U32 t_unpack_size;
	S32 leftover = 0;
	U8 *zip_buf, *zip_buf_ptr;
	int ret;
	bool bAnyFailure = false;

	// Note: MUST be inside of some critical section stopping two reads or two seeks on the same file

	if (filesize==0) {
		memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal failed with zero filesize on \"%s\"", hog_file, file->nameptr);
		return 0;
	}

	g_file_stats.pig_reads++;

	if (!zipped)
	{
		if (0!=fseek(file, fileoffset + pos, SEEK_SET)) {
			memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal failed to seek to %"FORM_LL"u on \"%s\"", hog_file, fileoffset + pos, file->nameptr);
			return 0;
		}

		numread = (U32)fread(buf, 1, size, file);
		if (numread!=size) {
			memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal fread failed %u bytes at %"FORM_LL"u on \"%s\"", hog_file, size, fileoffset + pos, file->nameptr);
			return 0;
		}

		return numread;
	}

	g_file_stats.pig_unzips++;

	zip_buf = zip_buf_ptr = ScratchAlloc(pack_size);

	if (filesize == size && pos == 0)
	{
		// zipped file, read in the whole thing at once
		if (0!=fseek(file, fileoffset, SEEK_SET)) {
			memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal (zipped) failed to seek to %"FORM_LL"u on \"%s\"", hog_file, fileoffset, file->nameptr);
			ScratchFree(zip_buf);
			return 0;
		}

		numread = (U32)fread(zip_buf, 1, pack_size, file);
		if (numread!=pack_size)
		{
			// Error!
			memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal (zipped) failed to seek to %"FORM_LL"u on \"%s\"", hog_file, fileoffset, file->nameptr);
			ScratchFree(zip_buf);
			return 0;
		}

		t_unpack_size = filesize;

		ret = unzipData(buf,&t_unpack_size,zip_buf,pack_size);
		if (ret < 0)
		{
			bAnyFailure = true;
			if (ret == Z_DATA_ERROR)
			{
				// First, re-verify zlib
				t_unpack_size = filesize;
				ret = unzipData(buf,&t_unpack_size,zip_buf,pack_size);
				if (ret == 0)
				{
					int ret2 = memTestRange(buf, t_unpack_size);
					if (ret2)
					{
						// g_genericlog will have output of prints which contain more details on what failed the memory test
						FatalErrorf("Memory inconsistency #5 detected.  Most likely cause is bad RAM.");
						// If we wanted to continue, we could leak data, it is bad, allocate a new buffer and read it in
					}

					ret2 = memTestRange(zip_buf, pack_size);
					if (ret2)
					{
						// This should never happen, if the zip buffer is bad, it shouldn't succeed a second time!
						// g_genericlog will have output of prints which contain more details on what failed the memory test
						assertmsg(0, "Memory inconsistency #6 detected - something gone horribly wrong.");
					}

					// Worked the second time!  Either bad memory or something else, attach a debugger and debug
					if (system_specs.audioX64CheckSkipped)
					{
						// Known to be caused by the RealTek audio driver
						FatalErrorf("Fatal memory corruption detected.  This is caused by a bug in the RealTek audio driver for Windows XP 64-bit.  Use a different audio device or remove -ignoreX64check from your command line to avoid this crash.");
					} else {
						// Changed message to #2 to get a new ET entry after splitting out RealTek crashes
						assertmsg(0, "zlib returned an error once, and then succeeded the second time - something gone horribly wrong #2.");
					}
				}

				{
					// Possible disk/network or memory failure, try reading from disk again?
					U8 *zip_buf2;
#if _PS3
					fflush(file);
#elif _XBOX
					SIZE_T oldsize = XGetFileCacheSize();
					XSetFileCacheSize(0); // Try to flush the cache
					XSetFileCacheSize(oldsize);
#endif
					// Re-read and verify that it gets the same data, if not, probably a memory error!
					fseek(file, fileoffset + (zipped ? 0 : pos), SEEK_SET);
					zip_buf2 = ScratchAlloc(pack_size);
					numread = (U32)fread(zip_buf2, 1, pack_size, file);
					if (memcmp(zip_buf, zip_buf2, pack_size)!=0) {
						// The two reads were different.  RAM or Disk problem?
						fseek(file, fileoffset + (zipped ? 0 : pos), SEEK_SET);
						numread = (U32)fread(zip_buf, 1, pack_size, file);
						if (memcmp(zip_buf, zip_buf2, pack_size)!=0) {
							// *zip_buf was bad, reading into *zip_buf2 was something different (assumed good)
							// and *zip_buf again was different than *zip_buf2, so we assume it's the same
							// badness as the first time, probably bad memory.

							if (memTestRange(zip_buf, pack_size) || memTestRange(zip_buf2, pack_size))
							{
								// g_genericlog will have output of prints which contain more details on what failed the memory test
								FatalErrorf("Memory inconsistency #7 detected.  Most likely cause is bad RAM.");
							} else {
								FatalErrorf("Repeated disk reads returned different results.  Probably bad memory or disk failure.");
							}
						} else {
							// *zip_buf was bad, *zip_buf2 got something different (assumed good)
							// *zip_buf again got the same as *zip_buf2, so it's probably a disk
							// error, just recover.
							devassertmsg(0, "Probably disk read error");
						}
					} else {
						// Both got the same bad data, perhaps the file is bad
						hogShowErrorWithFile(hog_file, file_index, 1, "Error decompressing data.  Verifying Files in the launcher may fix this issue.", 0);
					}
					ScratchFree(zip_buf2);
				}
			} else {
				// Unknown zlib error
				hogShowErrorWithFile(hog_file, file_index, 1, "Unknown error decompressing data.  Verifying Files in the launcher may fix this issue.", 0);
			}
		}
	} else {
		// partial read of a zipped file
		U32 bytesLeft;
		U32 bytesIn = 0;
		U32 prevBytes = unzipStreamTell();
		U64 readPos = fileoffset;

		if (pos < prevBytes) {
			// a backwards seek has been issued. go back to the beginning and unzip again.
			unzipStreamInit();
			prevBytes = 0;
		}

        if (0!=fseek(file, fileoffset + unzipStreamConsumed(), SEEK_SET)) {
            memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal (zipped partial) failed to seek to %"FORM_LL"u on \"%s\"", hog_file, fileoffset, file->nameptr);
            ScratchFree(zip_buf);
            return 0;
        }

		// if we need to seek forward, uncompress and throw away as many bytes as needed.
		if (pos > prevBytes) {
			U32 pad = pos - prevBytes;
			U8 *unzip_buf = ScratchAlloc(pad);

			// skip bytes until we're at the desired position.
			bytesLeft = pad;
			while (bytesLeft) {
				size_t readSize = MIN(UNZIP_BLOCK_SIZE, pack_size);
				bytesIn = (U32)fread(zip_buf, 1, readSize, file);
				if (bytesIn != readSize)
				{
					memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal (zipped partial) failed to read %u bytes at %"FORM_LL"u on \"%s\"", hog_file, readSize, readPos, file->nameptr);
					bAnyFailure = true;
					break;
				}
				readPos += bytesIn;

				numread = bytesLeft;
				ret = unzipStream(unzip_buf, &numread, zip_buf, bytesIn, false);
				if (ret < 0 && ret != Z_BUF_ERROR) {
					memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal (zipped partial) decompress error %d at %"FORM_LL"u on \"%s\"", hog_file, ret, readPos, file->nameptr);
					bAnyFailure = true;
					break;
				}

				assert(bytesLeft >= numread);
				bytesLeft -= numread;
			}

			ScratchFree(unzip_buf);
			leftover = unzipStreamRemaining();
			zip_buf_ptr = zip_buf + bytesIn - leftover;
			bytesIn = leftover;
		}

		t_unpack_size = 0;

		// now read the requested data
		bytesLeft = size;
		while (bytesLeft) {
			// skip the fread if we still have bytes in the buffer that haven't been decompressed
			if (!bytesIn) {
				size_t readSize = MIN(UNZIP_BLOCK_SIZE, pack_size);
				bytesIn = (U32)fread(zip_buf, 1, readSize, file);
				if (bytesIn != readSize)
				{
					memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal (zipped partial) failed to read %u bytes at %"FORM_LL"u on \"%s\"", hog_file, readSize, readPos, file->nameptr);
					bAnyFailure = true;
					break;
				}
				zip_buf_ptr = zip_buf;
			}

			numread = bytesLeft;
			ret = unzipStream(buf, &numread, zip_buf_ptr, bytesIn, false);
			if (ret < 0 && ret != Z_BUF_ERROR) {
				memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal (zipped partial) decompress error %d at %"FORM_LL"u on \"%s\"", hog_file, ret, readPos, file->nameptr);
				bAnyFailure = true;
				break;
			}

			assert(bytesLeft >= numread);
			bytesLeft -= numread;
			buf = (U8*)buf + numread;
			t_unpack_size += numread;
			assert(t_unpack_size <= size);

			leftover = unzipStreamRemaining();
			zip_buf_ptr = zip_buf + bytesIn - leftover;
			bytesIn = leftover;
		}
	}

	ScratchFree(zip_buf);

	if (t_unpack_size != size || bAnyFailure)
	{
		memlog_printf(&hogmemlog, "0x%08p: PigExtractBytesInternal failed and returning 0 on \"%s\"", hog_file, file->nameptr);
		return 0;
	}

	return size;
}

U32 PigSetExtractBytes(PigFileDescriptor *desc, void *buf, U32 pos, U32 size) // Extracts size bytes into a pre-allocated buffer
{
	U32 numread;
	HogFile *hog_file = desc->parent_hog;
	U32 data_size;

	data_size = hogFileGetFileSize(hog_file, desc->file_index);

	assert(!(pos >= data_size));

	if (pos + size > data_size) {
		size = data_size - pos;
	}

//JE: removed because fileCompare does this, which may be called from any gimme function, such as in GetTex, also new animReadTrackFile does this
//	if (pos < desc->header_data_size && pos + size > desc->header_data_size) {
//		assert(!"Warning!  Reading data from a file with a cached header, but reading over the border!");
//	}
	if (pos + size <= desc->header_data_size) {
		U32 header_data_size;
		const U8 *header_data;
		// We can read from the pre-cached header!
		header_data = hogFileGetHeaderData(hog_file, desc->file_index, &header_data_size);
		assert(header_data);
		assert(header_data_size == desc->header_data_size);
		memcpy(buf, ((U8*)header_data) + pos, size);
		return size;
	}

	numread=hogFileExtractBytes(hog_file, desc->file_index, buf, pos, size);
	if (numread != size)
	{
		char	err_buf[1000];

		errorIsDuringDataLoadingInc(desc->debug_name);
		// Corrupt pig files bucket uniquely on file name only for non-game clients
		if (GetAppGlobalType() == GLOBALTYPE_CLIENT)
		{
			sprintf(err_buf,"Corrupt pig file detected");
			triviaPrintf("ValidationError:AppendFilename", "%d", 1);
		}
		else
			sprintf(err_buf,"Corrupt pig file detected\nRead failed for file: %s\n",desc->debug_name);
		hogShowErrorWithFile(hog_file, desc->file_index, 0, err_buf, 0);
		errorIsDuringDataLoadingDec();
		return 0;
	}
	return numread;
}

typedef struct PigSet {
	int num_pigs;
	HogFile **eaPigs;
} PigSet;

PigSet pig_set={0,0};

// Windows resources to load as .hogg files for the pig set.
int *resource_pigs = NULL;

bool PigSetInited(void)
{
	return pig_set.num_pigs!=0;
}

static HogFileCreateFlags pigset_file_open_flags = HOG_DEFAULT;

static bool force_use_mutex=false;
// Forces using mutices for accessing hoggs (safe)
AUTO_CMD_INT(force_use_mutex, force_use_mutex) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;

AUTO_RUN_LATE;
void setupDefaultPigsetFlags(void)
{
	if (!force_use_mutex)
	{
		if (isProductionMode())
		{
			// This *should* be safe on all server types, but just doing GameServer for now, because it's the only one
			// that seems likely to be affected by the problem this would avoid (a crashed GameServer holding onto a mutex
			// stalling all other GameServers).
			if (GetAppGlobalType() == GLOBALTYPE_GAMESERVER || GetAppGlobalType() == GLOBALTYPE_TESTCLIENT)
			{
				pigset_file_open_flags = HOG_READONLY|HOG_NO_MUTEX|HOG_SHARED_MEMORY|HOG_NO_ACCESS_BY_NAME;
			}
		}
		if (GetAppGlobalType() == GLOBALTYPE_CLIENT)
		{
#if PLATFORM_CONSOLE
			// Always do read-only on Xbox/PS3 for decreased memory usage, but don't use shared memory!
			// We CANNOT afford have these handles open in non-read-only mode on the Xbox/PS3 for memory reasons
			pigset_file_open_flags = HOG_READONLY|HOG_NO_MUTEX|HOG_NO_ACCESS_BY_NAME|HOG_MUST_BE_READONLY;
#else
			if (isProductionMode() && !isPatchStreamingOn())
			{
				pigset_file_open_flags = HOG_READONLY|HOG_NO_MUTEX|HOG_SHARED_MEMORY|HOG_NO_ACCESS_BY_NAME|HOG_MUST_BE_READONLY;
			}
#endif
		}
	}
}

extern const char **eaGameDataDirs;

void PigSetIncludeResource(int id)
{
	eaiPush(&resource_pigs, id);
}

int PigSetInit(void) { // Loads all of the Pig files from a given directory
	const char *folder;
	char filespec[CRYPTIC_MAX_PATH];
    struct _finddata32i64_t c_file;
	intptr_t hFile;
	char fn[CRYPTIC_MAX_PATH];
	int pig_index=0;
	int data_dir_index;
	
	if(pig_disabled){
		return 0;
	}

	folder = fileDataDir(); // Call this first, because if fileDataDir is not defined, it calls PigSetInit, and we want the inner one to finish first, set num_pigs, and cause the outer one to exit

	if (pig_set.num_pigs!=0) {
		return 0; // Already inited
	}
	
	assert(folder);

	pig_set.num_pigs=-1; // Set this to -1 so that if we get a recursive call to PigSetInit (and we will, because we call fopen()), then we will just return immediately

	if (pig_set.eaPigs) {
		eaClearEx(&pig_set.eaPigs, NULL);
	}

	// Note: overrides are intentionally first in the list
	for (data_dir_index=0; data_dir_index<eaSize(&eaGameDataDirs); data_dir_index++)
	{
		folder = eaGameDataDirs[data_dir_index];

        STR_COMBINE_SS(filespec, folder, "/piggs/*.hogg");

#if _XBOX
		backSlashes(filespec);
#endif
		if( (hFile = findfirst32i64_SAFE( filespec, &c_file )) == -1L )
		{
			if (data_dir_index == eaSize(&eaGameDataDirs)-1 && eaSize(&pig_set.eaPigs)==0)
			{
				// Only try the current directory if all folders had no piggs
#if _PS3
				strcpy(filespec, "/app_home/piggs/*.hogg");
				folder = "/app_home";
#elif _XBOX
				strcpy(filespec, "game:/piggs/*.hogg");
				backSlashes(filespec);
				folder = ".";
#else
				strcpy(filespec, "./piggs/*.hogg");
				folder = ".";
#endif
				if( (hFile = findfirst32i64_SAFE( filespec, &c_file )) == -1L ) {
					continue; // No pigs anywhere!
				}
			} else {
				// Try the next folder
				continue;
			}
		}
		{
			// read them
			int i;
			int orig_index = pig_index;
			int store_index;
			do {
				HogFile *hogfile;
				HogFileCreateFlags flags = pigset_file_open_flags;
				int err_code=0;
				if (!strEndsWith(c_file.name, ".hogg"))
					continue;
				if (FolderCacheIsIgnored(c_file.name))
					continue;
				STR_COMBINE_SSS(fn, folder, "/piggs/", c_file.name);
				if (strEndsWith(fn, "ugc.hogg") || strEndsWith(fn, "dynamic.hogg")) // Allow dynamic patching just for these two files
					flags = HOG_DEFAULT | HOG_NOCREATE;
				hogfile = hogFileRead(fn, NULL, PIGERR_ASSERT, &err_code, flags);
				if (hogfile) {

                    if (pig_debug)
                        printf("%s\n", fn);

					eaPush(&pig_set.eaPigs, hogfile);
					pig_index++;
				}
				assert(pig_index == eaSize(&pig_set.eaPigs));
			} while (findnext32i64_SAFE( hFile, &c_file ) == 0 );
			_findclose(hFile);
			// Sort recent bunch so that any with "core" in the name are last
			store_index = pig_index-1;
			for (i=pig_index-2; i>=orig_index; i--) {
				if (strstri(hogFileGetArchiveFileName(pig_set.eaPigs[i]), "core")) {
					eaMove(&pig_set.eaPigs, store_index, i);
					store_index--;
				}
			}
		}
	}

	// Load any extra hog files from resources.
	EARRAY_INT_CONST_FOREACH_BEGIN(resource_pigs, i, n);
	{
		int id = resource_pigs[i];
		int err_code=0;
		HogFile *hogfile = hogFileReadFromResource(id, PIGERR_ASSERT, &err_code, HOG_DEFAULT);
		if (hogfile)
		{
			eaPush(&pig_set.eaPigs, hogfile);
			pig_index++;
		}
	}
	EARRAY_FOREACH_END;

	assert(pig_index == eaSize(&pig_set.eaPigs));
	if (pig_debug)
		printf("Read %d pigs\n", pig_index);
	if (pig_index == 0)
		pig_set.num_pigs = -1;
	else
		pig_set.num_pigs = pig_index;
	return 0;
}

#define PUT_PIGFILE(pig_num, file_num) (((((pig_num)+1) & 0xff) << 24) | (file_num & 0xffffff))
#define GET_PIGNUM(index) (((index) >> 24)-1)
#define GET_FILENUM(index) ((index) & 0xffffff)

PigFileDescriptor PigSetGetFileInfo(int pig_index, int file_index, const char *debug_relpath)
{
	PigFileDescriptor ret;
	HogFile *handle = pig_set.eaPigs[pig_index];
	U32 header_data_size;

	assert(pig_index != -1);
	assert(file_index != -1);

	assert( pig_index >= 0 && pig_index < pig_set.num_pigs );
	assert( file_index >= 0 && file_index < (int)hogFileGetNumFiles(handle));
	ret.debug_name = hogFileGetFileName(handle, file_index);
	if (!ret.debug_name || hogFileIsSpecialFile(handle, file_index))
	{
		memset(&ret, 0, sizeof(ret));
		return ret;
	}
	//ret.header_data = 
	hogFileGetHeaderData(handle, file_index, &header_data_size);
	ret.header_data_size = header_data_size;
	ret.release_hog_on_close = 0;
	ret.parent_hog = handle;
	ret.file_index = file_index;
	ret.size = hogFileGetFileSize(handle, file_index);
	return ret;
}

// Caller *must* destroy the hoghandle
PigFileDescriptor PigSetGetFileInfoFake(const char *hogname, const char *file_name)
{
	PigFileDescriptor ret = {0};
	int file_index;
	HogFile *handle = hogFileRead(hogname, NULL, PIGERR_ASSERT, NULL, HOG_READONLY|HOG_NOCREATE);
	U32 header_data_size;
	if (!handle)
		return ret;
	file_index = hogFileFind(handle, file_name);
	if (file_index == HOG_INVALID_INDEX)
	{
		hogFileDestroy(handle, true);
		return ret;
	}

	ret.debug_name = hogFileGetFileName(handle, file_index);
	if (!ret.debug_name || hogFileIsSpecialFile(handle, file_index))
	{
		hogFileDestroy(handle, true);
		memset(&ret, 0, sizeof(ret));
		return ret;
	}

	//ret.header_data = 
	hogFileGetHeaderData(handle, file_index, &header_data_size);
	ret.header_data_size = header_data_size;
	ret.release_hog_on_close = 1;
	ret.parent_hog = handle;
	ret.file_index = file_index;
	ret.size = hogFileGetFileSize(handle, file_index);
	return ret;
}


void *extractFromFS(const char *name, U32 *count) {
	U32 total;
	char *mem;
	FILE * file;

	file = fopen(name,"rb~"); // Does NOT allow reading from pigs
	if (!file)
		return NULL;

	fseek(file, 0, SEEK_END);
	total = (long)ftell(file);
	assert(total != -1);
	mem = malloc(total+1);
	fseek(file, 0, SEEK_SET);
	fread(mem,total,1,file);
	fclose(file);
	mem[total] = 0;
	if (count)
		*count = total;
	return mem;
}

void PigSetDestroy(void) {
	int i;
	for (i=0; i<pig_set.num_pigs; i++) {
		hogFileDestroy(pig_set.eaPigs[i], true);
	}
	eaDestroyEx(&pig_set.eaPigs, NULL);
	pig_set.num_pigs=0;
}

int PigSetGetNumPigs(void) {
	PigSetInit();
	return pig_set.num_pigs;
}

int PigSetGetNumPigsNoLoad(void) {
	return pig_set.num_pigs;
}


void PigSetCompression(unsigned int level)
{
	if (level > 9) level = 9;
	pig_compression = level;
}

HogFile *PigSetGetHogFile(int index) {
	if (pig_set.num_pigs==0)
		PigSetInit();
	assert(index<pig_set.num_pigs);
	return pig_set.eaPigs[index];
}

bool doNotCompressMySize(NewPigEntry * entry)
{
	return (entry->size == 0 || ((double)entry->pack_size / (double)entry->size) > 0.9);
}

bool pigShouldBeUncompressed(const char *ext)
{
	static const char *no_compress_extensions[] = {".fsb", ".fev", ".bcn", ".hogg", ".hog", ".mset", ".bik"};
	int i;

	if (!ext)
		return false;
	for (i=0; i<ARRAY_SIZE(no_compress_extensions); i++) 
		if (stricmp(ext, no_compress_extensions[i])==0)
			return true;
	return false;
}

bool doNotCompressMyExt(NewPigEntry * entry)
{
	const char * fname = entry->fname;
	const char *ext = strrchr(fname, '.');
	return pigShouldBeUncompressed(ext);
}

void pigChecksumData(	const U8* data,
						U32 size,
						U32 checksumOut[4])
{
	cryptMD5Update(data,size);
	cryptMD5Final(checksumOut);
}

void pigChecksumAndPackEntryEx(NewPigEntry *entry, int special_heap)
{
	U8*		unpack_data=0;
	S32		dataIsNewlyZipped = 0;

	if (!entry->pack_size)
	{
		unpack_data = entry->data;
		if (entry->size) {
			if (!entry->checksum[0])
			{
				pigChecksumData(unpack_data,entry->size,entry->checksum);
			}
		} else {
			ZeroStruct(&entry->checksum);
		}
		if (entry->dont_pack || doNotCompressMyExt(entry))
			return;
		PERFINFO_AUTO_START("pigChecksumAndPackEntry::zipDataEx", 1);
		entry->data = zipDataEx(unpack_data, entry->size, &entry->pack_size, pig_compression, true, special_heap);
		PERFINFO_AUTO_STOP();
		dataIsNewlyZipped = 1;
	} else {
		U32 unpack_size = entry->size;
		if(!entry->no_devassert)
			devassert(entry->checksum[0]); // Otherwise performance problem - should have grabbed checksum before it was zipped!
		if (!entry->checksum[0])
		{
			PERFINFO_AUTO_START("PERFORMANCE PROBLEM:hog unzip just for CRC", 1);
			unpack_data = ScratchAlloc(entry->size);
			unzipDataEx(unpack_data, &unpack_size, entry->data, entry->pack_size, true);
			pigChecksumData(unpack_data, entry->size, entry->checksum);
			ScratchFree(unpack_data);
			unpack_data = NULL;
			PERFINFO_AUTO_STOP();
		}
	}
	if(	!entry->size ||
		(	!entry->must_pack &&
			(	doNotCompressMySize(entry) ||
				doNotCompressMyExt(entry))
			)
		)
	{
		S32 dataIsNewlyUnzipped = 0;
		
		if(	!unpack_data &&
			!dataIsNewlyZipped)
		{
			int ret;
			unpack_data = malloc_special_heap(entry->size, special_heap);
			g_file_stats.pig_unzips++;
			ret = unzipData(unpack_data,&entry->size,entry->data,entry->pack_size);
			assert(ret==0);
			dataIsNewlyUnzipped = 1;
		}
		if(	entry->free_callback &&
			!dataIsNewlyZipped)
		{
			entry->free_callback(entry->data);
			entry->free_callback = NULL;
			entry->data = NULL;
		}
		else
		{
			SAFE_FREE(entry->data);
		}
		if(dataIsNewlyUnzipped)
		{
			entry->free_callback = NULL;
		}
		entry->data = unpack_data;
		entry->pack_size = 0;
		entry->dont_pack = 1; // So another call to this does not try to zip again
	}
	else
	{
		if (entry->free_callback && unpack_data)
		{
			entry->free_callback(unpack_data);
			unpack_data = NULL;
			entry->free_callback = NULL; //Is now compressed data to be normally freed
		}
		else
		{
			SAFE_FREE(unpack_data);
		}
	}
		
}

bool pigShouldCacheHeaderData(const char *ext)
{
	// Note: the client currently unloads all precached headers at startup after loading textures
	//   if something else needs this, that logic will need to be changed (FolderCacheReleaseHogHeaderData)
	static const char *scan_header_extensions[] = {
		".co2", // ObjectDB containers in some format
		".wtex", // Textures
		};
	int i;
	if (!ext)
		return false;
	for (i=0; i<ARRAY_SIZE(scan_header_extensions); i++) 
		if (stricmp(ext, scan_header_extensions[i])==0)
			return true;
	return false;
}

U8 *pigGetHeaderData(NewPigEntry *entry, U32 *size)
{
	static U8 *header_mem=0;
	static int header_size=0;
	char bigendian_extensions[] = ".bgeo";
	char *ext = strrchr(entry->fname, '.');

	if (ext && pigShouldCacheHeaderData(ext) && entry->size >= 4)
	{
		S32 numbytes,num_numbytes = sizeof(numbytes);
		bool zipped = entry->pack_size > 0;

		ANALYSIS_ASSUME(ext);
		if (zipped) {
			// This function can only be called from a single thread if provided pre-zipped data
			static DWORD threadid;
			if (!threadid)
				threadid = GetCurrentThreadId();
			else
				assert(threadid == GetCurrentThreadId());
		}

		if (strEndsWith(entry->fname, ".lm2")) {
			numbytes = 29; // fixed header size
		} else {
			if (zipped) {
				int ret = unzipData((U8*)&numbytes,&num_numbytes,entry->data,entry->pack_size);
				g_file_stats.pig_unzips++;
				assert(ret==0);
			} else
				numbytes = *(U32*)entry->data;
			if (strstri(bigendian_extensions, ext)) {
				// Writing a big endian file on a little endian system
				numbytes = endianSwapIfNotBig(U32, numbytes);
			} else {
				numbytes = endianSwapIfBig(U32, numbytes); // If we're on a big endian system, the serialized data is little endian
			}
		}
		if (numbytes == 0)
			return NULL;
		if (numbytes < 0 || numbytes > 1024*1024*2 || numbytes > entry->size) {
			Errorf("Header size too big on file %s!  Most likely corrupt data.", entry->fname);
			return NULL;
		}
		if (stricmp(ext, ".anm")==0 || stricmp(ext, ".geo")==0 || stricmp(ext, ".geolm")==0 || stricmp(ext, ".bgeo")==0 || stricmp(ext, ".lgeo")==0)
			numbytes+=4; // Cache 4 more bytes than an anm file tells it to
		dynArrayFitStructs(&header_mem, &header_size, numbytes);
		if (zipped) {
			int ret = unzipData(header_mem,&numbytes,entry->data,entry->pack_size);
			g_file_stats.pig_unzips++;
			assert(ret==0);
		} else
			memcpy(header_mem,entry->data,numbytes);
		*size = numbytes;
		return header_mem;
	}
	else
		return NULL;
}


void PigSetAdd(HogFile *hog_file)
{
	eaPush(&pig_set.eaPigs, hog_file);
	pig_set.num_pigs++;
}