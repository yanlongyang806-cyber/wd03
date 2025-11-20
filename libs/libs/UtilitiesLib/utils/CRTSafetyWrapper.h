#pragma once

//this header is intended to protect from people calling CRT functions that are not unicode-safe. Basically I attempted to 
//put in #defines so that calling any CRT function will either implicitly call a UTF8-wrapped version of the wide version of
//that function (ie, chdir) or will cause a compile error.

#ifdef __cplusplus
extern "C" {
#endif
int mkdir_UTF8(_In_z_ const char *dirname);
int remove_UTF8(_In_z_ const char *path);
int rename_UTF8(_In_z_ const char *oldname, _In_z_ const char *newname);
int rmdir_UTF8(_In_z_ const char *path);
int chdir_UTF8(_In_z_ const char *path);
char *_fullpath_UTF8_dbg(char *pOutFullPath, const char *pInPath, int iOutSize MEM_DBG_PARMS);
#ifdef __cplusplus
}
#endif

#define MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION This will not compile at all 1 2 3 4


//because all cryptic apps must now be unicode, we can't allow anyone to call the narrow versiosn of CRT file functions
#define _fsopen MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define fsopen MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _exec MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _execl MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define execl MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _execle MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define execle MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _execlp MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define execlp MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _execlpe MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define execlpe MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _execv MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define execv MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _execve MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define execve MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _execvp MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define execvp MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define fdopen MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _fdopen MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _find MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _fullpath_dbg MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _makepath MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _makepath_s MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define mkdir mkdir_UTF8

#define _mkdir MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define mktemp MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _mktemp MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _mktemp_s MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION

#define remove remove_UTF8
#define rename rename_UTF8
#define rmdir rmdir_UTF8
#define chdir chdir_UTF8
#define _rmdir rmdir_UTF8
#define _chdir chdir_UTF8
#define fullpath_UTF8(pOutFullPath, pInPath, iOutSize) _fullpath_UTF8_dbg(pOutFullPath, pInPath, iOutSize MEM_DBG_PARMS_INIT)

#define _searchenv MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _searchenv_s MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define sopen MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _sopen MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _sopen_s MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _spawn MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define spawnl MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _spawnl MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define spawnle MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _spawnle MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define spawnlp MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _spawnlp MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define spawnlpe MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _spawnlpe MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define spawnv MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _spawnv MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define spawnve MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _spawnve MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define spawnvp MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _spawnvp MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define spawnvpe MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _spawnvpe MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _splitpath MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _splitpath_s MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define tmpnam_s MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _tempnam_dbg MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define _unlink USE_UNLINK_MACRO_IN_STDTYPES
#define _getcwd_dbg MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
#define getcwd_dbg MUST_USE_WIDE_VERSIONS_OF_CRT_FUNCTION
