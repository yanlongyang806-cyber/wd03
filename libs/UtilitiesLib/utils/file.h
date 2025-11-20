/***************************************************************************
 
 
 
 */

#ifndef _FILE_H
#define _FILE_H
#pragma once
GCC_SYSTEM

// Warning: Do not use the C runtime functions for getting times from files, such as stat(), findfirstfind(), and similar.  The times they return are unreliable.

#include <stdio.h>
#include <io.h>
#include "stdtypes.h"
#include "memcheck.h"

C_DECLARATIONS_BEGIN

#include "EString.h"
#include "piglib.h"

#define sfread fread
#define fileClose fclose

typedef struct FileWrapper FileWrapper;
#define FILE FileWrapper
#undef getc
#undef ferror
#undef fopen // defined in stdtypes.h to be "please include file.h"
#define getc(f) x_fgetc(f)
#define fseek(f, dist, whence) x_fseek(f, dist, whence)
#define fopen(name, how) x_fopen(name, how, __FILE__, __LINE__)
#define fclose(f) x_fclose(f)
#define ftell(f) x_ftell(f)
#define fread(buf, size, count, f) x_fread(buf, size, count, f)
#define fwrite(buf, size, count, f) x_fwrite(buf, size, count, f)
#define fgets(buf, size, f) x_fgets(buf, size, f)
#define vfprintf x_vfprintf
// #define fprintf x_fprintf
#define fprintf(fw, format, ...) x_fprintf(fw, FORMAT_STRING_CHECKED(format), __VA_ARGS__)
#define ferror x_ferror
#define fflush(f) x_fflush(f)
// #define fscanf x_fscanf
#define fgetc(f) x_fgetc(f)
#define fputc(c, f) x_fputc(c, f)
#define setvbuf(f, buf, mode, size) x_setvbuf(f, buf, mode, size)

#define FW_ALLOC_STACK_SIZE 4096

typedef enum {
	IO_CRT,
	IO_GZ,
	IO_PIG,
	IO_STUFFBUFF,
	IO_ESTRING,
	IO_NETFILE,
	IO_WINIO,
	IO_RAMCACHED,
} IOMode;

typedef enum {
	FileContentType_Binary = 0,				// Catch-all for files of unknown type
	FileContentType_Empty,					// Empty files
	FileContentType_Generic_Narrow_Text,	// Text in a common narrow byte format, such as ASCII, CP-1252, UTF-8, SJIS, EBCDIC, or similar
	FileContentType_Generic_Wide_Text,		// Wide text, such as UTF-16
	FileContentType_Generic_Image,			// Common image format
	FileContentType_Windows_Executable,		// Some type of Windows executable, that is not text-based
	FileContentType_Windows_Icon,			// Windows icon file
	FileContentType_Windows_Cursor,			// Windows cursor file
	FileContentType_Csv,					// Comma-separated values
	FileContentType_Hog,					// Hog file
} FileContentType;

#define FWB_NUM_BUFFERS 3
#define FW_BUFFER_SIZE (1024*16)

typedef struct FileWritingBuffer
{
	S64 iCurrentSeekInRealFile;
	char buffers[FWB_NUM_BUFFERS][FW_BUFFER_SIZE];

	int iNumFullBuffers;
	int iCurActiveBuffer;
	int iBytesWrittenInActiveBuffer;

} FileWritingBuffer;

typedef struct FileWrapper
{
	void	*fptr;
	IOMode	iomode;
	const char *nameptr;
	const char *caller_fname;
	U32	nameptr_needs_freeing:1;
	U32	error:1; // If set, there has been an I/O error of some sort.
	U32 line:30; // For estring files, the nameptr is the file it was allocated in, and this is the line number
	FileWritingBuffer *pWritingBuffer;
} FileWrapper;

#define fileAllocWithRetries(fname, lenp, count) fileAllocWithRetries_dbg(fname, lenp, count MEM_DBG_PARMS_INIT)
#define fileAllocWithCRCCheck(fname, lenp, checksum_valid) fileAllocWithCRCCheck_dbg(fname,lenp, checksum_valid MEM_DBG_PARMS_INIT)
#define fileAlloc(fname,lenp) fileAlloc_dbg(fname,lenp MEM_DBG_PARMS_INIT)
#define fileAllocEx(fname,lenp,callback) fileAllocEx_dbg(fname,lenp,"rb",callback MEM_DBG_PARMS_INIT)
//#ifdef _XBOX
// used for allocating files on physical memory on the xbox
// necessary for some things, like sound - JW
#define fileAllocPhysical(fname,alignment,lenp) fileAllocPhysical_dbg(fname,alignment,lenp MEM_DBG_PARMS_INIT)
//#endif


#define fa_read(dst, src, bytes) memcpy(dst, src, bytes); (*(char**)&(src))+=bytes;
#define fa_read_s(dst, dst_size, src, bytes) memcpy_s((dst), (dst_size), (src), (bytes)); (*(char**)&(src))+=(bytes);
#define fa_read_elem(dst, src) fa_read_s((dst), sizeof(*(dst)), (src), sizeof(*(dst)))
#define fa_read_string(dst, src, bytes) memcpy_s(SAFESTR(dst), (src), (bytes)); (*(char**)&(src))+=(bytes);

typedef FileScanAction (*FileScanProcessor)(char* dir, struct _finddata32_t* data, void *pUserData);

typedef struct FileScanContext FileScanContext;
typedef FileScanAction (*FileScanProcessorEx)(FileScanContext* context);
struct FileScanContext{
	FileScanProcessorEx processor;
	char* dir;
	struct _finddata32_t* fileInfo;
	void* userData;
};
typedef int (*FileProcessCallback)(int bytes_processed, int total); // Return 0 to cancel reading/writing

typedef struct StuffBuff StuffBuff;

#if !PLATFORM_CONSOLE
void fileGetCrypticSettingsDir(char* dirOut, S32 dirSize);
void fileGetCrypticSettingsFilePath(char* pathOut, S32 pathSize, const char* fileName);
#endif

void fileLoadGameDataDirAndPiggs(void);
void filePrintDataDirs(void);
void fileDisableAutoDataDir(void);
void fileAutoDataDir(void);
void fileSpecialDir(const char *name, char *dest, size_t dest_size);
bool fileMakeSpecialDir(bool makeIt, const char *name, char *dest, size_t dest_size);
const char *fileTempDir(void);
const char *fileLogDir(void);
const char *fileCacheDir(void);
const char *fileDemoDir(void);
const char *fileSrcDir(void);
const char *fileCoreSrcDir(void);
const char *fileLocalDataDir(void);
const char *fileDataDir(void);
const char *fileBaseDir(void);

void fileSetAsyncFopen(bool bAsync);
int fileGetGimmeDataBranchNumber(void);
const char *fileGetGimmeDataBranchName(void);
char *fileRelativePath_s(SA_PARAM_NN_STR const char *fname, char *dest, size_t dest_size);
#define fileRelativePath(fname, dest) fileRelativePath_s(fname, SAFESTR(dest))
bool fileIsInDataDirs(SA_PARAM_NN_STR const char *fname);
char *fileExecutableDir(void);
char *fileCoreExecutableDir(void);
char *fileToolsBinDir(void);
char *fileToolsDir(void);
const char *fileCoreToolsBinDir(void);
const char *fileCrypticToolsBinDir(void);

bool fileIsFileServerPath(const char *path);

char *fileFixUpName(const char *src,char * tgt);

void fileUpdateHoggAfterWrite(SA_PARAM_NN_STR const char *relpath, SA_PARAM_OP_VALID void *data, U32 data_len); // data == NULL -> load from disk

int fileNewer(const char *refname, const char *testname);
int fileNewerOrEqual(const char *refname, const char *testname);
int fileNewerAbsolute(const char *refname,const char *testname);
__time32_t fileLastChangedEx(const char *refname, bool do_hog_reload);
#define fileLastChanged(refname) fileLastChangedEx(refname, true)
#define fileLastChanged_NoHogReload(refname) fileLastChangedEx(refname, false)
U32 fileLastChangedSS2000(const char *refname);
__time32_t fileLastChangedAltStat(const char *refname);
__time32_t fileLastChangedAbsolute(const char *refname);

//doesn't go through filewatcher, so presumably slower, but avoids a
//currently unsolved daylight savings time bug
U32 fileLastChangedSS2000AltStat(const char *refname);

typedef __time32_t (*FileTimestampFunction)(const char *refname);

// When the file was last saved. Hooks in to the assetmanager on the client
__time32_t fileLastSaved(const char *refname);
void fileRegisterLastSavedFunction(FileTimestampFunction func);

// Hooks for adding a method of getting the timestamp off of a file which does
// not exist (e.g. source files which are binned and not included in production
// mode).  These are called by fileLastChanged().
void fileRegisterLastChangedNonExistentFunction(FileTimestampFunction func);

char* fileFixName(char* src, char* tgt);

int is_pigged_path(const char *path);

SA_ORET_NN_STR char *fileLocateWrite_s(SA_PARAM_NN_STR const char *fname, SA_PRE_GOOD SA_POST_NN_STR char *dest, size_t dest_size);
#define fileLocateWrite(fname, dest) fileLocateWrite_s(fname, SAFESTR(dest))
#define fileLocatePhysical(fname, dest) fileLocateWrite(fname, dest)
SA_RET_OP_STR char *fileLocateRead_s(SA_PARAM_NN_STR const char *fname, SA_PRE_GOOD SA_POST_OP_STR char *dest, size_t dest_size);
#define fileLocateRead(fname, dest) fileLocateRead_s(fname, SAFESTR(dest))

// fileLocate*Bin is for finding local /bin paths, which treat the Core/ folder slightly differently (we read/write bins from/into our project folder only)
char *fileLocateWriteBin_s(SA_PARAM_NN_STR const char *fname, SA_PRE_GOOD SA_POST_NN_STR char *dest, size_t dest_size);
#define fileLocateWriteBin(fname, dest) fileLocateWriteBin_s(fname, SAFESTR(dest))
char *fileLocateReadBin_s(SA_PARAM_NN_STR const char *fname, SA_PRE_GOOD SA_POST_NN_STR char *dest, size_t dest_size);
#define fileLocateReadBin(fname, dest) fileLocateReadBin_s(fname, SAFESTR(dest))
FILE *fileOpenBin_dbg(const char *fname, const char *how, const char *caller_fname, int line);
#define fileOpenBin(fname, how) fileOpenBin_dbg(fname, how, __FILE__, __LINE__)

FILE *fileOpen_dbg(const char *fname, const char *how, const char *caller_fname, int line);
#define fileOpen(fname, how) fileOpen_dbg(fname, how, __FILE__, __LINE__)
FILE *fileOpenEx_dbg(FORMAT_STR const char *fnameFormat, const char *caller_fname, int line, const char *how, ...);
#define fileOpenEx(fname, how, ...) fileOpenEx_dbg(FORMAT_STRING_CHECKED(fname), __FILE__, __LINE__, how, __VA_ARGS__)
FILE *fileOpenTemp_dbg(const char *fname, const char *how, const char *caller_fname, int line);
#define fileOpenTemp(fname, how) fileOpenTemp_dbg(fname, how, __FILE__, __LINE__)
FILE *fileOpenStuffBuff_dbg(StuffBuff *sb, const char *caller_fname, int line);
#define fileOpenStuffBuff(fname) fileOpenStuffBuff_dbg(fname, __FILE__, __LINE__)
FILE *fileOpenEString_dbg(char **estr, const char *caller_fname, int line);
#define fileOpenEString(estr) fileOpenEString_dbg(estr, __FILE__, __LINE__)
FILE *fileOpenRAMCached_dbg(const char *fname, const char *how, const char *caller_fname, int line);
#define fileOpenRAMCached(fname, how) fileOpenRAMCached_dbg(fname, how, __FILE__, __LINE__)
FILE *fileOpenRAMCachedPreallocated_dbg(const char *buffer, size_t buffer_size, const char *caller_fname, int line);
#define fileOpenRAMCachedPreallocated(buffer, buffer_size) fileOpenRAMCachedPreallocated_dbg(buffer, buffer_size, __FILE__, __LINE__)
void fileRAMCachedFreeCache(FILE *f); // Must call this before writing to file

void fileFree(void *mem);
int printPercentage(int bytes_read, int total); // Sample FileProcessCallback
SA_RET_OP_VALID void *fileAllocEx_dbg(const char *fname,int *lenp, const char* mode, FileProcessCallback callback, const char *caller_fname, int line);
SA_RET_OP_VALID void *fileAlloc_dbg(const char *fname,int *lenp, const char *caller_fname, int line);
void *fileAllocWithCRCCheck_dbg(const char *fname,int *lenp, bool *checksum_valid, const char *caller_fname, int line);
SA_ORET_OP_VALID void *fileAllocWithRetries_dbg(const char *fname,int *lenp, int count, const char *caller_fname, int line);
SA_ORET_OP_VALID void *fileAllocPhysical_dbg(const char *fname,int align,int *lenp, const char *caller_fname, int line);
int fileRenameToBak(const char *fname); // returns 0 on success
int fileForceRemove(const char *fname); // removes read-only files, returns -1 on failure
int fileMoveToRecycleBin(const char *filename); // removes a file and moves it to the recycle bin; returns 0 on success
int fileCopy(const char *src, const char *dst); // Copy a file (with timestamps); returns 0 upon success
int fileMove(const char *src, const char *dst);
int fileMakeLocalBackup(const char *_fname, int time_to_keep); // Make an incremental backup
bool fileExponentialBackup( char *fn, U32 intervalSeconds, U32 doublingPeriod); // make a numbered backup where the time between backups doubles at intervals. 
// the doubling period specifies how often to double the interval. given a doubling of N, after X*N backups the interval I is I_start*2^X
void fileOpenWithEditor(const char *localfname);
const char* fileDetectDiffProgram(const char *fname1, const char *fname2);
int fileLaunchDiffProgram(const char *fname1, const char *fname2); // returns 0 on failure
FileContentType fileGuessFileContentType(const char *fname);
bool fileGzip(const char *fname); // return false and calls Errorf() on failure
		

//hacky version of fileAlloc which copies the file to c:\ before loading it so that it will just fail rather
//than crashing while reading off of N as N goes down
void *fileAlloc_NetworkDriveSafe(const char *fname, int *lenp, int iFailureTime);


intptr_t fileSizeEx(const char *fname, bool do_hog_reload);
#define fileSize(fname) fileSizeEx(fname, true)
S64 fileSize64(const char *fname); // Slow!  Does not use FileWatcher.
int fileExistsEx(const char *fname, bool do_hog_reload);
#define fileExists(fname) fileExistsEx(fname, true)
bool fileNeedsPatching(const char *fname);
bool wfileExists(const wchar_t *fname);
int dirExists(const char *dirname);
int dirExistsMultiRoot(const char* dirname, const char** roots);
int fileIsReadOnly(const char *fname);
bool fileIsReadOnlyDoNotTrustFolderCache(const char *relpath);
int fileIsUsingDevData(void);
int filePathCompare(const char* path1, const char* path2);
int filePathBeginsWith(const char* fullPath, const char* prefix);
U64 fileGetFreeDiskSpace(const char* fullPath);
void fileAllPathsAbsolute(bool newvalue);
int fileIsAbsolutePath(const char *path);
int fileIsAbsolutePathInternal(const char *path);
const char** fileGetNameSpaceNameList();
void fileLoadAllUserNamespaces(int ignored);
int fileGetNameSpacePath_s(SA_PARAM_NN_STR const char *path, SA_PRE_VALID SA_POST_OP_STR char *ns, size_t ns_size, SA_PRE_VALID SA_POST_OP_STR char *relpath, size_t relpath_size);
#define fileGetNameSpacePath(path, ns, relpath) fileGetNameSpacePath_s(path, SAFESTR(ns), SAFESTR(relpath))
#define fileIsNameSpacePath(path) fileGetNameSpacePath_s(path, NULL, 0, NULL, 0)
#define fileIsProductionEditAllowed(path) (isProductionEditMode() && fileIsNameSpacePath(path))
char *fileNameSpacePath_s(SA_PARAM_NN_STR const char *path, SA_PARAM_NN_STR const char *ns, char *out, size_t out_size);
#define fileNameSpacePath(path, ns, out) fileNameSpacePath_s(path, ns, SAFESTR(out))
bool editorsManuallyDisabled(); // Returns 1 if the editors have been manually disabled
void disableEditors(bool disable); // Should be called at startup time/early-commandline parsing time
bool showDevUI(void); // Returns 1 if we should show the development mode UI, on by default in development mode
bool showDevUIProd(void); // Returns 1 if we should show the development mode UI, even in production mode - generally use the above
void setDefaultProductionMode(bool enable); // Set the default production mode status, used when there are no hoggs and it has not been forced.
int isProductionEditAvailable(void); // Returns 1 if this project supports production editing, 0 otherwise
int isProductionEditMode(void); // Returns 1 if we're in production edit mode
void setProductionEditMode(bool enable); // Call on the client to dynamically switch it
bool isPatchStreamingOn(void);
#define areEditorsPossible() ((isDevelopmentMode()|| isProductionEditAvailable()) && !editorsManuallyDisabled()) // Can editors ever be enabled. Must be valid to call at AUTO_RUN time
#define areEditorsAllowed() ((isDevelopmentMode()|| isProductionEditMode()) && !editorsManuallyDisabled()) // Are editors enabled RIGHT NOW. Must be valid to call at AUTO_RUN time
int hasServerDir_NotASecurityCheck(void); // Returns 0 if the server directory does not exist, 1 if it does.  EASILY FAKED!! ONLY USE TO PREVENT EDITOR CRASHES!!
void fileFreeOldZippedBuffers(void); // Frees buffers on handles that haven't been accessed in a long time
void fileFreeZippedBuffer(FileWrapper *fw); // Frees a buffer, if there is one, on a zipped pigged file handle

void fileDisableWinIO(int disable); // Disables use of Windows ReadFile/CreateFile instead of CRT for binary files

FileWrapper *x_fopen(const char *name, const char *how, const char *caller_fname, int line);
int x_fclose(FileWrapper *fw);
S64 x_fseek(FileWrapper *fw, S64 dist, int whence);
int x_getc(FileWrapper *fw);

S64 x_ftell_internal(FileWrapper *fw);
__forceinline static S64 x_ftell(FileWrapper *fw)
{
	if(fw->iomode == IO_ESTRING)
		return estrLength((char **)fw->fptr);
	else
		return x_ftell_internal(fw);
}

size_t x_fread(void *buf,size_t size1,size_t size2,FileWrapper *fw);

size_t x_fwrite_internal(const void *buf,size_t size1,size_t size2,FileWrapper *fw);
__forceinline static size_t x_fwrite(const void *buf, size_t size1, size_t size2, FileWrapper *fw)
{
	if(fw->iomode == IO_ESTRING)
	{
		estrConcat_dbg((char **)fw->fptr,(const char*)buf,(int)(size1*size2), fw->caller_fname, fw->line);
		return size1*size2;
	}
	else
		return x_fwrite_internal(buf, size1, size2, fw);
}

char *x_fgets(char *buf,int len,FileWrapper *fw);
void x_fflush(FileWrapper *fw);
int x_vfprintf(FileWrapper *fw, FORMAT_STR const char *format, va_list va);
int x_fprintf (FileWrapper *fw, FORMAT_STR const char *format, ...);
int x_ferror(FileWrapper* fw);
// i'm assuming some files were using fscanf and fopen and weren't including file.h
// so this never got commented out
// int x_fscanf(FileWrapper* fw, FORMAT_STR const char* format, ...);
int x_fgetc(FileWrapper* fw);
int x_fputc(int c,FileWrapper* fw);
int x_setvbuf(FileWrapper* fw, char *buffer, int mode, size_t size);
FileWrapper *fileWrap(void *real_file_pointer);

FILE *fileGetStdout(void);
FILE *fileGetStderr(void);
void *fileRealPointer(FileWrapper *fw);
void *fileDupHandle(FileWrapper *fw);

void *fileLockRealPointer(FileWrapper *fw); // This implements support for Pig files, and must be followed by an unlock when done
void fileUnlockRealPointer(FileWrapper *fw);

int fileTruncate(FileWrapper *fw, U64 newsize); // Returns 0 on success

int fileGetSize(FILE* file);
S64 fileGetSize64(FILE* file);
int fileSetTimestamp(const char* fullFilename, U32 timestamp);
void fileSetTimestampSS2000(const char* filename, U32 timestamp);

//sets created, modified, accessed times. 0 means leave unchanged
void fileSetModificationTimesSS2000(const char* file, U32 iCreationTime, U32 iModifiedTime, U32 iAccessedTime);


int fileSetLogging(int on);

const char * const * fileGetGameDataDirs(void);

// Adds an extra data dir to the list. Must be called from EARLYCOMMANDLINE
void fileAddNameSpace( char * path, char * path_name);
const char *fileGetNameSpaceDir( const char * path);

#if _XBOX
void initXboxDrives(void);
void debugXboxRestart(const char * command);
#endif

void filePushDiskAccessAllowedInMainThread(bool bDiskAccessAllowed);
void filePopDiskAccessAllowedInMainThread(void);

bool fileDiskAccessAllowedInMainThread(bool bAllowed);
void fileDiskAccessCheck(void); // Called in functions that are going to access the disk in order to trap disk-access stalls

extern bool g_xbox_production_hack;
extern bool g_xbox_local_hoggs_hack;

#define _findfirst32i64	DO_NOT_USE
#define _findnext32i64	DO_NOT_USE
#define _findfirst32	DO_NOT_USE
#define _findnext32		DO_NOT_USE

#undef _findfirst
#undef _findnext
#define _findfirst		DO_NOT_USE
#define _findnext		DO_NOT_USE

// TODO: Try to replace the below functions with functions based on fwFindFirstFile().

intptr_t __cdecl findfirst32i64_SAFE(const char* filename, struct _finddata32i64_t * pfd);
int __cdecl findnext32i64_SAFE(intptr_t filehandle, struct _finddata32i64_t * pfd);

intptr_t __cdecl findfirst32_SAFE(const char* filename, struct _finddata32_t * pfd);
int __cdecl findnext32_SAFE(intptr_t filehandle, struct _finddata32_t * pfd);

intptr_t __cdecl findfirst_SAFE(const char* filename, struct _finddata_t * pfd);
int __cdecl findnext_SAFE(intptr_t filehandle, struct _finddata_t * pfd);

AUTO_STRUCT;
typedef struct FileStats 
{
	int fread_count;
	int fwrite_count;
	int fopen_count;
	int pig_reads;
	int pig_unzips;
	int fileloader_queues;
} FileStats;
extern ParseTable parse_FileStats[];
#define TYPE_parse_FileStats FileStats
extern FileStats g_file_stats;
void fileGetStats(FileStats *file_stats);
void fileGetStatsAndClear(FileStats *file_stats);

bool fgetEString(char **string, FILE *file);

bool fileLoadedDataDirs(void);


int isDevelopmentModeInternal(void); // Returns 1 if we're in development mode, 0 if in production
int isProductionModeInternal(void); // Returns 0 if we're in development mode, 1 if in production

extern int gbCachedIsProductionMode;

static __forceinline int isProductionMode(void)
{
	if (gbCachedIsProductionMode != -1)
	{
		return gbCachedIsProductionMode;
	}

	return isProductionModeInternal();
}

static __forceinline int isDevelopmentMode(void)
{
	if (gbCachedIsProductionMode != -1)
	{
		return !gbCachedIsProductionMode;
	}

	return isDevelopmentModeInternal();
}

C_DECLARATIONS_END

#endif
