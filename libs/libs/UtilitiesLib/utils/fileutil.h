#ifndef FILEUTIL_H
#define FILEUTIL_H
#pragma once
GCC_SYSTEM

#include "file.h"

#ifdef UNICODE
typedef struct _WIN32_FIND_DATAW WIN32_FIND_DATAW;
typedef WIN32_FIND_DATAW WIN32_FIND_DATA;
typedef struct _WIN32_FIND_DATAA WIN32_FIND_DATAA;
#else
typedef struct _WIN32_FIND_DATAA WIN32_FIND_DATAA;
typedef WIN32_FIND_DATAA WIN32_FIND_DATA;
#endif

char * fileGetcwd(char * _DstBuf, int _SizeInBytes);

// If true, assume that the cwd never changes.
// WARNING: Subtle, yet horrible bugs will result if you set this when it is not, in fact, true.
void fileSetFixedCwd(bool fixed);

typedef struct FolderNode FolderNode;
typedef FileScanAction (*FolderNodeOpEx)(const char *fullpath, FolderNode *node, void *pUserData_Proc, void *pUserData_Data);

extern int quickload;

// All callers of fileScanAllDataDirs() should instead call fileScanAllDataDirs2() for less overhead
void fileScanAllDataDirs(const char* dir, FileScanProcessor processor, void *pUserData);
// fileScanAllDataDirs2 has a lot less overhead when running off the FolderCache
void fileScanAllDataDirs2(const char* dir, FolderNodeOpEx processor, void *pUserData);

void fileScanAllDataDirsMultiRoot(const char* dir, FileScanProcessor processor, const char** roots, void *pUserData);
int IsFileNewerThanDir(const char* dir, const char* filemask, const char* persistfile);
	// same as old textparser.ParserIsPersistNewer.  This checks a directory tree
	// for files that are newer than persistfile.  You can mask extensions
	// using filemask.  

void fileCacheDirectories(const char* paths[], int numpaths);

// This function is probably far less robust as some of the others.
// It works for the case that it was needed for.
void fileScanFoldersToDepth(const char* absPath, int max_depth, FolderNodeOpEx processor, void *pUserData);

//returns the complete pathname in long format
char *makeLongPathName_safe(char * anyPath, char *outBuff, int buffsize); //accepts both UNC and traditional paths, both long and short
#define makeLongPathName(anyPath, outBuff) makeLongPathName_safe( anyPath, outBuff, ARRAY_SIZE(outBuff) )

//Checks to see if a file has been updated.
//Also updates the age that this was last checked
int fileHasBeenUpdated(const char * fname, int *age);

int fileCompare(const char *fname0, const char *fname1); // Returns 0 if equal
bool isWildcardMatch( const char* wildcard, const char* string, bool caseSensitive, bool onlyExactMatch );

// Safely zips a file without allocating any memory or calling any of our functions (for use in minidump writing)
void fileZip(const char *filename);
void fileGZip(const char *filename);
void fileGunZip(const char *filename);

void fileWaitForExclusiveAccess(const char *filename);
bool fileCanGetExclusiveAccess(const char *filename);

bool fileAttemptNetworkReconnect(const char *filename);

void requireDataVersion(int version);

//used to "back up" while writing a text file. Backs up to offset, then fills in chars, then backs up again
void fseek_and_set(FILE *pFile, size_t iOffset, char c);

typedef struct FileServerFile FileServerFile;

FileServerFile *fileServerClient_fopen(const char *fname, const char *how);
size_t fileServerClient_fread(void *buf, U64 size, FileServerFile *file_data);
int fileServerClient_fclose(FileServerFile *file_data);
S64 fileServerClient_ftell(FileServerFile *file_data);
S64 fileServerClient_fseek(FileServerFile *file_data, S64 dist, int whence);

void fileServerRun(void);
S32 fileServerFindFirstFile(void ** handleOut, const char* fileSpec, WIN32_FIND_DATAA* wfd);
S32 fileServerFindNextFile(void *handle, WIN32_FIND_DATAA* wfd);
S32 fileServerFindClose(void *handle);

// Gets the exact file name of the path, minus all folders (includes file extension)
char *fileGetFilename_s(const char *fFullPath, char *dest, size_t dest_size);
#define fileGetFilename(path, dest) fileGetFilename_s(path, dest, ARRAY_SIZE_CHECKED(dest))

// Splits up the path and the file extension (if any)
void fileSplitFilepathAndExt_s(const char *fullPath, char *fname, size_t fname_size, char *ext, size_t ext_size);
#define fileSplitFilepath(path, fname, ext) fileSplitFilepathAndExt_s(path, fname, ARRAY_SIZE_CHECKED(fname), ext, ARRAY_SIZE_CHECKED(ext))

// Return the base pointer of the filename component of the path, or NULL if there is no filename.
const char *fileGetFilenameSubstrPtr(const char * filenameOrFullPath);
// Remove any extension of the filename component of the path, including the extension separator,
// and return the pointer to the end of the string, for example to allow appending a new filename suffix, 
// separator and/or extension.
char *fileStripFileExtension(char * filenameOrFullPath);

//reads in a file that was written with "wbz", ie .gz, do not use this for anything else
void *fileAllocWBZ_dbg(const char *pFileName, int *pLen MEM_DBG_PARMS);
#define fileAllocWBZ(pFileName, pLen) fileAllocWBZ_dbg(pFileName, pLen MEM_DBG_PARMS_INIT)

//returns true for "gameserver_foo.exe" and "gameserverFD.exe". Ignores directory (if any), strips off X64, FD and _foo
bool FilenamesMatchFrankenbuildAware(const char *pName1, const char *pName2);

void humanBytes(S64 bytes, F32 *num, char **units, U32 *prec);

#endif
