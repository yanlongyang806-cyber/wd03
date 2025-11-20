#pragma once
GCC_SYSTEM

#include <stdio.h>
#if !SPU
#include <time.h>
#endif
#include "stdtypes.h"
#include "memcheck.h"
#include "EString.h"

C_DECLARATIONS_BEGIN

// Stringize a token in the preprocessor
#define STRINGIZE(X)			STRINGIZE_HELPER(X)
#define STRINGIZE_HELPER(X)		#X

typedef struct _CHAR_INFO CHAR_INFO;
typedef struct _COORD COORD;
typedef void *HANDLE;


// Bitmasks for creating colors
typedef enum {
	COLOR_LEAVE=-1,
	COLOR_BLUE=1,
	COLOR_GREEN=2,
	COLOR_RED=4,
	COLOR_BRIGHT=8,
} ConsoleColor;

typedef struct StuffBuff
{
	int		idx;
	int		size;
	char	*buff;
	MEM_DBG_STRUCT_PARMS
} StuffBuff;

#if _PS3
    #if SPU
        #define PREFETCH( Address )
    #else
        #define PREFETCH( Address ) __dcbt(Address)
    #endif
#elif _XBOX
#define PREFETCH( Address ) __dcbt(0, (Address))
#else
#define PREFETCH( Address ) _mm_prefetch((char const *)(Address),_MM_HINT_T1)
#endif

char *changeFileExt_s(SA_PARAM_NN_STR const char *fname, SA_PARAM_NN_STR const char *new_extension,
					  SA_PARAM_NN_STR char *buf, size_t buf_size);
#define changeFileExt(fname,new_extension,buf) changeFileExt_s(fname, new_extension, SAFESTR(buf))
char *forwardSlashes(SA_PARAM_OP_STR char *path);
char *backSlashes(SA_PARAM_OP_STR char *path);
char *fixDoubleSlashes(SA_PARAM_NN_STR char *fn);
void estrFixFilename(char **estrFn);
void concatpath_s(SA_PARAM_NN_STR const char *s1, SA_PARAM_NN_STR const char *s2, SA_PARAM_NN_STR char *full, size_t full_size);
#define concatpath(s1,s2,full) concatpath_s(s1, s2, SAFESTR(full))
void makefullpath_s(SA_PARAM_NN_STR const char *dir, SA_PARAM_NN_STR char *full, size_t full_size);
#define makefullpath(dir,full) makefullpath_s(dir, SAFESTR(full))
void mkdirtree(SA_PARAM_NN_STR char *path); // this function modifies passed on string. mkdirtree_const is safe
void mkdirtree_const(SA_PARAM_NN_STR const char *path);
void rmdirtree(SA_PARAM_NN_STR const char *path);
void rmdirtreeEx(SA_PARAM_NN_STR const char* path, int forceRemove);
int isFullPath(SA_PARAM_NN_STR const char *dir);
int makeDirectories(SA_PARAM_NN_STR const char* dirPath);
int makeDirectoriesForFile(SA_PARAM_NN_STR const char* filePath);

void openURL(SA_PARAM_NN_STR const char *url);
void openCrypticWikiPage(SA_PARAM_NN_STR const char *page);

SA_ORET_OP_STR char *strtok_r(SA_PARAM_OP_STR char *target, SA_PARAM_NN_STR const char *delim, char **last); // Identical to strtok_s, use that instead?
SA_ORET_OP_STR char *strtok_paren_r(SA_PARAM_OP_STR char *target, SA_PARAM_NN_VALID const char *delim, char **last);
SA_ORET_OP_STR char *strtok_delims_r(SA_PARAM_OP_STR char *target,
								   SA_PARAM_NN_STR const char *startdelim, SA_PARAM_NN_STR const char *enddelim,
								   char **last);
SA_ORET_OP_STR char *strtok_nondestructive(SA_PARAM_OP_STR char *target, const char *delim, char **last, char *delim_context);

SA_ORET_OP_STR char* strsep2(SA_PARAM_NN_NN_STR char** str, SA_PARAM_NN_STR const char* delim, SA_PRE_VALID char* retrievedDelim);
SA_ORET_OP_STR char* strsep(SA_PARAM_NN_NN_STR char** str, SA_PARAM_NN_STR const char* delim);
SA_ORET_OP_STR char *strstri(SA_PARAM_NN_STR const char *s, SA_PARAM_NN_STR const char *srch);
#define strstri_safe(s, srch)		\
	__pragma(warning(suppress:6387))	\
	(((s) && (srch)) ? (strstri((s), (srch))) : NULL)
SA_ORET_OP_STR const char *strstriConst(SA_PARAM_NN_STR const char *s, SA_PARAM_NN_STR const char *srch);
#define tokenize_line(buf, args, next_line_ptr) tokenize_line_safe(buf, args, ARRAY_SIZE(args), next_line_ptr)
int tokenize_line_safe(char *buf,char *args[],int max_args, char **next_line_ptr);
#define tokenize_line_quoted(buf, args, next_line_ptr) tokenize_line_quoted_safe(buf, args, ARRAY_SIZE(args), next_line_ptr)
int tokenize_line_quoted_safe(SA_PARAM_OP_STR char *buf, SA_PARAM_NN_STR char *args[], int max_args, char **next_line_ptr);
int tokenize_line_quoted_safe_earray(SA_PARAM_OP_STR char *buf, char ***pppOutArray, char **next_line_ptr);
int tokenize_line_quoted_delim(SA_PARAM_NN_STR char *buf, SA_PARAM_NN_STR char *args[], int max_args, char **next_line_ptr, SA_PARAM_NN_STR const char *startdelim, SA_PARAM_NN_STR const char *enddelim);
char *strtok_quoted(SA_PARAM_OP_STR char *target, SA_PARAM_NN_STR const char *startdelim, SA_PARAM_NN_STR const char *enddelim);
char *strtok_quoted_r(SA_PARAM_OP_STR char *target, SA_PARAM_NN_STR const char *startdelim, SA_PARAM_NN_STR const char *enddelim, char **last);

SA_ORET_NN_STR char *getContainerValueStatic(SA_PARAM_OP_STR char *container, SA_PARAM_NN_STR char* fieldname);
int getContainerValue(SA_PARAM_OP_STR char *container, SA_PARAM_NN_STR char* fieldname, SA_PARAM_NN_STR char* result, int size);

#ifndef PLATFORM_CONSOLE
int system_detach(SA_PARAM_NN_STR const char* cmd, int minimized, int hidden);
int system_wait(SA_PARAM_NN_STR const char* cmd, int minimized, int hidden);


//used specifically when launching things that might have a newer FD version, ie, we try to launch
//gameserver.exe, but if gameserverFD.exe exists and is newer, launch it instead.
int system_detach_with_fulldebug_fixup(SA_PARAM_NN_STR const char* cmd, int minimized, int hidden);
#endif

//given an exe name, returns it with FD appended before .exe IF that exists and is newer
void fixupExeNameForFullDebugFixup(char **ppName);


//returns 1 if the process is running 0 otherwise. Win32 only.
int system_poke(int pid);

//returns 1 if the process is running and the executable is named <name> or name is NULL, 0 otherwise. Win32 only.
int system_pokename(int pid, const char *name);

// Basic prototype for dynArray; use one of the below wrappers
#define basicDynArrayAdd(basep,struct_size,count,max_count,num_structs,kind,memdbg)																						\
	(((int)((count)+(num_structs))>(int)max_count) ?																													\
	dynArrayAdd ## kind ## _dbg((void**)&(basep),(struct_size),&(count),&(max_count),(num_structs) memdbg):																\
	((count+=(num_structs)),																																			\
	memset((char*)(basep)+(count-(num_structs))*(struct_size), 0, (struct_size)*(num_structs))))
#define basicDynArrayAdd_no_memset(basep,struct_size,count,max_count,num_structs,kind,memdbg)																			\
	(((int)((count)+(num_structs))>(int)max_count) ?																													\
	dynArrayAdd ## kind ## _dbg((void**)&(basep),(struct_size),&(count),&(max_count),(num_structs) memdbg):														\
	(count+=(num_structs),((char*)(basep)+(count-(num_structs))*(struct_size))))
#define basicDynArrayAddp(basep,count,max_count,ptr,kind,memdbg) dynArrayAddp ## kind ## _dbg(&basep,&count,&max_count,ptr memdbg)
#define basicDynArrayAddStruct(basep,count,max_count,kind,memdbg) basicDynArrayAdd(basep,sizeof(*(basep)),count,max_count,1,kind,memdbg)
#define basicDynArrayAddStruct_no_memset(basep,count,max_count,kind,memdbg) basicDynArrayAdd_no_memset(basep,sizeof(*(basep)),count,max_count,1,kind,memdbg)
#define basicDynArrayAddStructType(type,basep,count,max_count,kind,memdbg) ((type*)basicDynArrayAddStruct(basep,count,max_count,kind,memdbg))
#define basicDynArrayAddStructs(basep,count,max_count,num_structs,kind,memdbg) basicDynArrayAdd(basep,sizeof(*(basep)),count,max_count,num_structs,kind,memdbg)
#define basicDynArrayFit(basep,struct_size,max_count,idx_to_fit,kind,memdbg) dynArrayFit ## kind ## _dbg(basep,struct_size,max_count,idx_to_fit memdbg)
#define basicDynArrayFitStructs(basep,max_count,idx_to_fit,kind,memdbg) basicDynArrayFit((void**)basep,sizeof(**(basep)),max_count,idx_to_fit,kind,memdbg)
#define basicDynArrayReserveStructs(basep,max_count,reserve_max,kind,memdbg)																							\
	(reserve_max > max_count																																			\
	? dynArrayReserve ## kind ## _dbg((void**)&basep,sizeof(*(basep)),&max_count,reserve_max memdbg)																\
		:basep)

// Regular dynArray
#define dynArrayAdd(basep,struct_size,count,max_count,num_structs) basicDynArrayAdd(basep,struct_size,count,max_count,num_structs,Small,MEM_DBG_PARMS_INIT)
#define dynArrayAdd_no_memset(basep,struct_size,count,max_count,num_structs) basicDynArrayAdd_no_memset(basep,struct_size,count,max_count,num_structs,Small,MEM_DBG_PARMS_INIT)
#define dynArrayAddp(basep,count,max_count,ptr) basicDynArrayAddp(basep,count,max_count,ptr,Small,MEM_DBG_PARMS_INIT)
#define dynArrayAddStruct(basep,count,max_count) basicDynArrayAddStruct(basep,count,max_count,Small,MEM_DBG_PARMS_INIT)
#define dynArrayAddStruct_no_memset(basep,count,max_count) basicDynArrayAddStruct_no_memset(basep,count,max_count,Small,MEM_DBG_PARMS_INIT)
#define dynArrayAddStructType(type,basep,count,max_count) basicDynArrayAddStructType(type,basep,count,max_count,Small,MEM_DBG_PARMS_INIT)
#define dynArrayAddStructs(basep,count,max_count,num_structs) basicDynArrayAddStructs(basep,count,max_count,num_structs,Small,MEM_DBG_PARMS_INIT)
#define dynArrayFit(basep,struct_size,max_count,idx_to_fit) basicDynArrayFit(basep,struct_size,max_count,idx_to_fit,Small,MEM_DBG_PARMS_INIT)
#define dynArrayFitStructs(basep,max_count,idx_to_fit) basicDynArrayFitStructs(basep,max_count,idx_to_fit,Small,MEM_DBG_PARMS_INIT)
#define dynArrayReserveStructs(basep,max_count,reserve_max) basicDynArrayReserveStructs(basep,max_count,reserve_max,Small,MEM_DBG_PARMS_INIT)

// Pass-through memory accounting version of dynArray, for use in utility libraries
#define sdynArrayAdd(basep,struct_size,count,max_count,num_structs) basicDynArrayAdd(basep,struct_size,count,max_count,num_structs,Small,MEM_DBG_PARMS_CALL)
#define sdynArrayAdd_no_memset(basep,struct_size,count,max_count,num_structs) basicDynArrayAdd_no_memset(basep,struct_size,count,max_count,num_structs,Small,MEM_DBG_PARMS_CALL)
#define sdynArrayAddp(basep,count,max_count,ptr) basicDynArrayAddp(basep,count,max_count,ptr,Small,MEM_DBG_PARMS_CALL)
#define sdynArrayAddStruct(basep,count,max_count) basicDynArrayAddStruct(basep,count,max_count,Small,MEM_DBG_PARMS_CALL)
#define sdynArrayAddStruct_no_memset(basep,count,max_count) basicDynArrayAddStruct_no_memset(basep,count,max_count,Small,MEM_DBG_PARMS_CALL)
#define sdynArrayAddStructType(type,basep,count,max_count) basicDynArrayAddStructType(type,basep,count,max_count,Small,MEM_DBG_PARMS_CALL)
#define sdynArrayAddStructs(basep,count,max_count,num_structs) basicDynArrayAddStructs(basep,count,max_count,num_structs,Small,MEM_DBG_PARMS_CALL)
#define sdynArrayFit(basep,struct_size,max_count,idx_to_fit) basicDynArrayFit(basep,struct_size,max_count,idx_to_fit,Small,MEM_DBG_PARMS_CALL)
#define sdynArrayFitStructs(basep,max_count,idx_to_fit) basicDynArrayFitStructs(basep,max_count,idx_to_fit,Small,MEM_DBG_PARMS_CALL)
#define sdynArrayReserveStructs(basep,max_count,reserve_max) basicDynArrayReserveStructs(basep,max_count,reserve_max,Small,MEM_DBG_PARMS_CALL)

// Large version of the above, using size_t instead of int
#define bigDynArrayAdd(basep,struct_size,count,max_count,num_structs) basicDynArrayAdd(basep,struct_size,count,max_count,num_structs,Big,MEM_DBG_PARMS_INIT)
#define bigDynArrayAdd_no_memset(basep,struct_size,count,max_count,num_structs) basicDynArrayAdd_no_memset(basep,struct_size,count,max_count,num_structs,Big,MEM_DBG_PARMS_INIT)
#define bigDynArrayAddp(basep,count,max_count,ptr) basicDynArrayAddp(basep,count,max_count,ptr,Big,MEM_DBG_PARMS_INIT)
#define bigDynArrayAddStruct(basep,count,max_count) basicDynArrayAddStruct(basep,count,max_count,Big,MEM_DBG_PARMS_INIT)
#define bigDynArrayAddStruct_no_memset(basep,count,max_count) basicDynArrayAddStruct_no_memset(basep,count,max_count,Big,MEM_DBG_PARMS_INIT)
#define bigDynArrayAddStructType(type,basep,count,max_count) basicDynArrayAddStructType(type,basep,count,max_count,Big,MEM_DBG_PARMS_INIT)
#define bigDynArrayAddStructs(basep,count,max_count,num_structs) basicDynArrayAddStructs(basep,count,max_count,num_structs,Big,MEM_DBG_PARMS_INIT)
#define bigDynArrayFit(basep,struct_size,max_count,idx_to_fit) basicDynArrayFit(basep,struct_size,max_count,idx_to_fit,Big,MEM_DBG_PARMS_INIT)
#define bigDynArrayFitStructs(basep,max_count,idx_to_fit) basicDynArrayFitStructs(basep,max_count,idx_to_fit,Big,MEM_DBG_PARMS_INIT)
#define bigDynArrayReserveStructs(basep,max_count,reserve_max) basicDynArrayReserveStructs(basep,max_count,reserve_max,Big,MEM_DBG_PARMS_INIT)

// Large version of the above, using size_t instead of int
#define sbigDynArrayAdd(basep,struct_size,count,max_count,num_structs) basicDynArrayAdd(basep,struct_size,count,max_count,num_structs,Big,MEM_DBG_PARMS_CALL)
#define sbigDynArrayAdd_no_memset(basep,struct_size,count,max_count,num_structs) basicDynArrayAdd_no_memset(basep,struct_size,count,max_count,num_structs,Big,MEM_DBG_PARMS_CALL)
#define sbigDynArrayAddp(basep,count,max_count,ptr) basicDynArrayAddp(basep,count,max_count,ptr,Big,MEM_DBG_PARMS_CALL)
#define sbigDynArrayAddStruct(basep,count,max_count) basicDynArrayAddStruct(basep,count,max_count,Big,MEM_DBG_PARMS_CALL)
#define sbigDynArrayAddStruct_no_memset(basep,count,max_count) basicDynArrayAddStruct_no_memset(basep,count,max_count,Big,MEM_DBG_PARMS_CALL)
#define sbigDynArrayAddStructType(type,basep,count,max_count) basicDynArrayAddStructType(type,basep,count,max_count,Big,MEM_DBG_PARMS_CALL)
#define sbigDynArrayAddStructs(basep,count,max_count,num_structs) basicDynArrayAddStructs(basep,count,max_count,num_structs,Big,MEM_DBG_PARMS_CALL)
#define sbigDynArrayFit(basep,struct_size,max_count,idx_to_fit) basicDynArrayFit(basep,struct_size,max_count,idx_to_fit,Big,MEM_DBG_PARMS_CALL)
#define sbigDynArrayFitStructs(basep,max_count,idx_to_fit) basicDynArrayFitStructs(basep,max_count,idx_to_fit,Big,MEM_DBG_PARMS_CALL)
#define sbigDynArrayReserveStructs(basep,max_count,reserve_max) basicDynArrayReserveStructs(basep,max_count,reserve_max,Big,MEM_DBG_PARMS_CALL)

void *dynArrayAddSmall_dbg(SA_PARAM_NN_VALID void **basep,int struct_size,SA_PARAM_NN_VALID int *count,SA_PARAM_NN_VALID int *max_count,int num_structs MEM_DBG_PARMS);
void *dynArrayAddpSmall_dbg(SA_PARAM_NN_VALID void ***basep,SA_PARAM_NN_VALID int *count,SA_PARAM_NN_VALID int *max_count, SA_PARAM_OP_VALID void *ptr MEM_DBG_PARMS);
void *dynArrayFitSmall_dbg(SA_PARAM_NN_VALID void **basep,int struct_size,SA_PARAM_NN_VALID int *max_count,int idx_to_fit MEM_DBG_PARMS);
void *dynArrayReserveSmall_dbg(void **basep,int struct_size,int *max_count,int reserve_max MEM_DBG_PARMS);
void *dynArrayAddBig_dbg(SA_PARAM_NN_VALID void **basep,size_t struct_size,SA_PARAM_NN_VALID size_t *count,SA_PARAM_NN_VALID size_t *max_count,size_t num_structs MEM_DBG_PARMS);
void *dynArrayAddpBig_dbg(SA_PARAM_NN_VALID void ***basep,SA_PARAM_NN_VALID size_t *count,SA_PARAM_NN_VALID size_t *max_count, SA_PARAM_OP_VALID void *ptr MEM_DBG_PARMS);
void *dynArrayFitBig_dbg(SA_PARAM_NN_VALID void **basep,size_t struct_size,SA_PARAM_NN_VALID size_t *max_count,size_t idx_to_fit MEM_DBG_PARMS);
void *dynArrayReserveBig_dbg(void **basep,size_t struct_size,size_t *max_count,size_t reserve_max MEM_DBG_PARMS);

void newConsoleWindow(void);
void setConsoleTitle(const char *msg);
void setConsoleTitleWithPid(const char *msg);
#define consoleSetFGColor(fg) consoleSetColor((ConsoleColor)(fg), COLOR_LEAVE)
void consolePushColor(void);
void consolePopColor(void);
ConsoleColor consoleGetColorFG(void);
ConsoleColor consoleGetColorBG(void);
void consoleSetColor(ConsoleColor fg, ConsoleColor bg);
void consoleSetDefaultColor();
void consoleCapture(void);
CHAR_INFO *consoleGetCapturedData(void);
void consoleGetCapturedSize(SA_PRE_OP_FREE SA_POST_OP_VALID COORD* coord);
void consoleGetDims(SA_PRE_NN_FREE SA_POST_NN_VALID COORD* coord);
void consoleBringToTop(void);
void consoleUpSize(int minBufferWidth, int minBufferHeight);
void consoleSetSize(int bufferWidth, int bufferHeight, int windowHeight);
int consoleYesNo(void);
bool consoleIsCursorOnLeft(void);
void consoleSetToUnicodeFont(void);

int printPercentageBar(int filled, int total);
void backSpace(int num, bool clear);

// You can't make getFileName const!  The return value has gotta stay
// non-const, so the parameter must be non-const as well.
SA_ORET_NN_STR char *getFileName(SA_PARAM_NN_VALID char *fname);
SA_ORET_NN_STR char *getFileNameNoDir_s(SA_PARAM_NN_STR char *dest, size_t dest_size, SA_PARAM_NN_STR const char *filename);
#define getFileNameNoDir(dest, filename) getFileNameNoDir_s(SAFESTR(dest), filename)
SA_ORET_NN_STR char *getFileNameNoExt_s(SA_PARAM_NN_STR char *dest, size_t dest_size, SA_PARAM_NN_STR const char *filename);
#define getFileNameNoExt(dest, filename) getFileNameNoExt_s(SAFESTR(dest), filename)
SA_ORET_NN_STR char *getFileNameNoExtNoDirs_s(SA_PARAM_NN_STR char *dest, size_t dest_size, SA_PARAM_NN_STR const char *filename);
#define getFileNameNoExtNoDirs(dest, filename) getFileNameNoExtNoDirs_s(SAFESTR(dest), filename)
SA_ORET_NN_STR const char *getFileNameConst(SA_PARAM_NN_STR const char *fname);
SA_ORET_NN_STR char *getDirectoryName(SA_PARAM_NN_STR char *fullPath);
SA_ORET_NN_STR char *getDirString(SA_PARAM_NN_STR const char *path);
SA_ORET_NN_STR char *addFilePrefix(SA_PARAM_NN_STR const char* path, SA_PARAM_NN_STR const char* prefix);
SA_ORET_NN_STR char *filenameInFixedSizeBuffer(SA_PARAM_NN_STR const char *filename, int strwidth, char *buf, int buf_size, bool right_align);
SA_ORET_NN_STR const char *getUserName(void);
SA_ORET_NN_STR const char *getHostName(void);

#define MACHINE_ID_MAX_LEN 256
SA_ORET_NN_STR const char *getMachineID(void);

//given a filename, returns a pointer to the last dot that has
//no slashes or backslashes after it, NULL if none
char *FindExtensionFromFilename(char *pFileName);


void initStuffBuff_dbg(SA_PRE_NN_FREE SA_POST_NN_VALID StuffBuff *sb, int size MEM_DBG_PARMS);
#define initStuffBuff(sb, size) initStuffBuff_dbg(sb, size MEM_DBG_PARMS_INIT)
void resizeStuffBuff(SA_PARAM_NN_VALID StuffBuff *sb, int size );
void clearStuffBuff(SA_PARAM_NN_VALID StuffBuff *sb);
void addStringToStuffBuff_fv(SA_PARAM_NN_VALID StuffBuff *sb,const char *fmt,va_list args);
void addStringToStuffBuff(SA_PARAM_NN_VALID StuffBuff *sb, FORMAT_STR const char *fmt, ... );
#define addStringToStuffBuff(sb, fmt, ...) addStringToStuffBuff(sb, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void addIntToStuffBuff(SA_PARAM_NN_VALID StuffBuff *sb, int i);
void addSingleStringToStuffBuff(SA_PARAM_NN_VALID StuffBuff *sb, SA_PARAM_NN_STR const char *s);
void addBinaryDataToStuffBuff(SA_PARAM_NN_VALID StuffBuff *sb, SA_PRE_NN_RELEMS_VAR(len) const char *data, int len);
void freeStuffBuff(SA_PRE_NN_VALID SA_POST_P_FREE StuffBuff * sb );

int firstBits( int val );
bool simpleMatchExact(SA_PARAM_NN_STR const char *exp, SA_PARAM_NN_STR const char *tomatch); // This version of simpleMatch doesn't do the weird prefix matching thing, so "the" does not match "then"
bool simpleMatchExactSensitiveFast(SA_PARAM_NN_STR const char *exp, SA_PARAM_NN_STR const char *tomatch); // Like simpleMatchExact(), but without folding and faster.
bool simpleMatch(SA_PARAM_NN_STR const char *exp, SA_PARAM_NN_STR const char *tomatch); // Does a simple wildcard match (only 1 '*' is supported).  simpleMatch assumes that exp is a prefix, so simpleMatch("the", "then") is true
bool simpleMatchSensitive(SA_PARAM_NN_STR const char *exp, SA_PARAM_NN_STR const char *tomatch); // Like simpleMatch(), but without folding.
SA_ORET_NN_STR char *getLastMatch(); // Returns the portion that matched in the last call to simpleMatch
bool matchExact(SA_PARAM_NN_STR const char *exp, SA_PARAM_NN_STR const char *tomatch);
bool matchExactSensitive(SA_PARAM_NN_STR const char *exp, SA_PARAM_NN_STR const char *tomatch);  // Like matchExact, but without folding.
bool match(SA_PARAM_NN_STR const char *exp, SA_PARAM_NN_STR const char *tomatch); // Supports multiple wildcards, but slow on any length strings
void OutputDebugStringv(FORMAT_STR const char *fmt, va_list va); // Outputs a string to the debug console (useful for remote debugging, and outputing stuff we don't want users to see)
void OutputDebugStringf(FORMAT_STR const char *fmt, ... ); // Outputs a string to the debug console (useful for remote debugging, and outputing stuff we don't want users to see)
#define OutputDebugStringf(fmt, ...) OutputDebugStringf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

//characters which get escaped by escapeDataStatic adn its ilk
#define ESCAPABLE_CHARACTERS "\\\n\r\t\"'%$"

#define escapeString(instring,outstring) escapeString_s(instring,SAFESTR(outstring));
size_t escapeString_s(SA_PARAM_NN_STR const char *instring, SA_PRE_NN_ELEMS_VAR(outlen) SA_POST_OP_VALID char * outstring, size_t outlen);
size_t escapeDataStatic(SA_PRE_NN_RELEMS_VAR(inlen) const char *instring, size_t inlen, SA_PRE_NN_ELEMS_VAR(outlen) SA_POST_OP_VALID char * outstring, size_t outlen, bool stopOnNull);
SA_ORET_NN_STR const char *escapeString_unsafe(SA_PARAM_NN_VALID const char *instring); // Behavior of the old escapeString. NOT THREADSAFE

#define unescapeString(instring,outstring) unescapeString_s(instring,SAFESTR(outstring));
size_t unescapeString_s(SA_PARAM_NN_STR const char *instring, SA_PRE_NN_ELEMS_VAR(outlen) SA_POST_OP_VALID char * outstring, size_t outlen);
size_t unescapeDataStatic(SA_PRE_NN_RELEMS_VAR(inlen) const char *instring, size_t inlen, SA_PRE_NN_ELEMS_VAR(outlen) SA_POST_OP_VALID char * outstring, size_t outlen, bool stopOnNull);
SA_ORET_NN_STR const char *unescapeString_unsafe(SA_PARAM_NN_STR const char *instring); // Behavior of the old escapeString. NOT THREADSAFE

int strIsAlphaNumeric(SA_PARAM_OP_STR const unsigned char* str);
int strIsAlpha(SA_PARAM_OP_STR const unsigned char* str);
int strIsNumeric(SA_PARAM_OP_STR const unsigned char* str);
#define strcatf(concatStr, format, ...) strcatf_s(concatStr, ARRAY_SIZE_CHECKED(concatStr), FORMAT_STRING_CHECKED(format), __VA_ARGS__)
void strcatf_s(char* concatStr, size_t concatStr_size, FORMAT_STR const char* format, ...);

//note: these two functions are case insensitive
int strEndsWith(SA_PARAM_OP_STR const char* str, SA_PARAM_OP_STR const char* ending);
int strStartsWith(SA_PARAM_OP_STR const char* str, SA_PARAM_OP_STR const char* start);
int strStartsWithIgnoreUnderscores(const char* str, const char* start);
int strEndsWithAny(SA_PARAM_OP_STR const char* str, SA_PARAM_OP_STR const char** endings);
int strStartsWithAny(SA_PARAM_OP_STR const char* str, SA_PARAM_OP_STR const char** starts);
int strEndsWithAnyStatic(SA_PARAM_OP_STR const char* str, SA_PARAM_OP_STR const char** endings);
int strStartsWithAnyStatic(SA_PARAM_OP_STR const char* str, SA_PARAM_OP_STR const char** starts);


// case sensitive
int strEndsWithSensitive(SA_PARAM_NN_STR const char* str, SA_PARAM_NN_STR const char* ending);

#if !SPU
#define printDate(date, buf) printDate_s(date, SAFESTR(buf))
SA_ORET_NN_STR char *printDate_s(__time32_t date, SA_PARAM_NN_STR char *buf, size_t buf_size);
void printDateEstr_dbg(__time32_t date, char **buf, SA_PARAM_NN_STR const char *caller_fname, int line);
#define printDateEstr(date, buf) printDateEstr_dbg(date, buf, __FILE__, __LINE__)
#endif

#define Strncpyt(dest, src) strcpy(dest, src)
SA_ORET_NN_STR char *strncpyt(SA_PARAM_NN_STR char *buf, SA_PARAM_NN_STR const char *s, int n); // Does a strncpy, null terminates, silently truncates
SA_ORET_NN_STR char *strcpy_unsafe(SA_PARAM_NN_STR char *dst, SA_PARAM_NN_STR const char *src);

#define Vsprintf(dst, fmt, ap) vsprintf_s(dst, ARRAY_SIZE_CHECKED(dst), fmt, ap)
#define Vsnprintf(dst, count, fmt, ap) vsprintf_s(dst, ARRAY_SIZE_CHECKED(dst), count, fmt, ap)
SA_ORET_NN_STR char *strInsert( SA_PARAM_NN_STR char * dest, SA_PARAM_NN_STR const char * insert_ptr, SA_PARAM_NN_STR const char *insert ); // returns static ptr to string with 'insert' inserted at the insert_ptr of dest
SA_ORET_NN_STR char *strchrInsert( SA_PARAM_NN_STR char * dest, SA_PARAM_NN_STR const char * insert, int character ); // inserts 'insert' at every character location
SA_ORET_NN_STR char *strstrInsert( SA_PARAM_NN_STR const char * src, SA_PARAM_NN_STR const char * find, SA_PARAM_NN_STR const char * replace );	// find-and-replace within a string
void strchrReplace( SA_PARAM_NN_STR char *dest, int find, int replace );// replace 'find' characters with 'replace' characters in string
int strchrCount( const char *str, int find ); // Count the number of 'find' characters in string
#define strstriReplace(src, find, replace) strstriReplace_s(SAFESTR(src), find, replace)
int strstriReplace_s(SA_PARAM_NN_STR char *src, size_t src_size, SA_PARAM_NN_STR const char *find, SA_PARAM_NN_STR const char *replace); // replace "find" string with "replace" string in src (case insensitive), returns 1 if found, replaces only once occurrence per call
void removeLeadingAndFollowingSpaces(SA_PARAM_NN_STR char * str);
SA_ORET_NN_STR char *incrementName(SA_PARAM_NN_STR unsigned char *name, unsigned int maxlen);

#if !_PS3
void disableWow64Redirection(void);	// IMPORTANT: this screws up winsock
void enableWow64Redirection(void);
#endif

#if !SPU
__time32_t statTimeToUTC(__time32_t statTime);
__time32_t statTimeFromUTC(__time32_t utcTime);
__time32_t gimmeTimeToUTC(__time32_t gimmeTime);
__time32_t gimmeTimeFromUTC(__time32_t utcTime);
int createShortcut(SA_PARAM_NN_STR char *file, SA_PARAM_NN_STR char *out, int icon, SA_PARAM_OP_STR char *working_dir, SA_PARAM_OP_STR char *args, SA_PARAM_OP_STR char *desc);
int spawnProcess(SA_PARAM_NN_STR char *cmdLine, int mode); // spawn a process. mode should be either _P_WAIT (for synchronous) or _P_NOWAIT (for asynchronous).  returns either the return value of the process (synchronous), or the process handle (asynchronous)
#endif

#define printUnit(buf, val) printUnit_s(SAFESTR(buf), val)
SA_ORET_NN_STR char *printUnit_s(SA_PARAM_NN_STR char *buf, size_t buf_size, S64 val);
SA_ORET_NN_STR char *printUnitDecimal_s(char *buf, size_t buf_size, S64 val);
#define printTimeUnit(buf, val) printTimeUnit_s(SAFESTR(buf), val)
SA_ORET_NN_STR char *printTimeUnit_s(SA_PARAM_NN_STR char *buf, size_t buf_size, U32 val);
void printfColor_dbg(int color, FORMAT_STR const char *fmt, ...);
#define printfColor(color, fmt, ...) printfColor_dbg(color, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

SA_ORET_NN_STR char* getCommaSeparatedInt(S64 x);

SA_ORET_NN_STR char *itoa_with_grouping(int i, SA_PRE_GOOD SA_POST_NN_STR char *buf, int radix, int groupsize, int decimalplaces, char sep, char dp, int leftzeropad);

void swap(SA_PARAM_NN_VALID void *a, SA_PARAM_NN_VALID void *b, size_t width);
#define SWAP32(a,b) {int tempIntForSwap=(a);(a)=(b);(b)=tempIntForSwap;} // a swap for 32-bit or smaller integer variables
#define SWAP64(a,b) {S64 tempS64ForSwap=(a);(a)=(b);(b)=tempS64ForSwap;} // a swap for 64-bit or smaller integer variables
#define SWAPF32(a,b) {float tempFloatForSwap=(a);(a)=(b);(b)=tempFloatForSwap;} // a swap for 32-bit floating point variables
#define SWAPP(a,b) {void*tempPointerForSwap=(a);(a)=(b);(b)=tempPointerForSwap;} // a swap for pointers
#define SWAPVEC2(a,b) { SWAPF32((a)[0],(b)[0]); SWAPF32((a)[1],(b)[1]); }
#define SWAPVEC3(a,b) { SWAPF32((a)[0],(b)[0]); SWAPF32((a)[1],(b)[1]); SWAPF32((a)[2],(b)[2]); }
#define SWAPVEC4(a,b) { SWAPF32((a)[0],(b)[0]); SWAPF32((a)[1],(b)[1]); SWAPF32((a)[2],(b)[2]); SWAPF32((a)[3],(b)[3]); }
#define SWAPIVEC2(a,b) { SWAP32((a)[0],(b)[0]); SWAP32((a)[1],(b)[1]); }
#define SWAPIVEC3(a,b) { SWAP32((a)[0],(b)[0]); SWAP32((a)[1],(b)[1]); SWAP32((a)[2],(b)[2]); }
#define SWAPIVEC4(a,b) { SWAP32((a)[0],(b)[0]); SWAP32((a)[1],(b)[1]); SWAP32((a)[2],(b)[2]); SWAP32((a)[3],(b)[3]); }

typedef struct Packet Packet;
typedef struct StashTableImp *StashTable;
StashTable receiveHashTable(SA_PARAM_NN_VALID Packet * pak, int mode);
void sendHashTable(SA_PARAM_NN_VALID Packet * pak, SA_PARAM_NN_VALID StashTable table);

#if !PLATFORM_CONSOLE
#pragma comment (lib, "ws2_32.lib") //	for getHostName()
#endif

#define CLAMP(var, min, max) \
	((var) < (min)? (min) : ((var) > (max)? (max) : (var)))

#define SEQ_NEXT(var,min,max) (((var) + 1) >= (max) ? (min) : ((var) + 1))
#define SEQ_PREV(var,min,max) ((var) <= (min) ? ((max) - 1) : ((var) - 1))

// ------------------------------------------------------------
// ranges

#define INRANGE( var, min, max ) ((var) >= min && (var) < max)
#define INRANGE0(var, max) INRANGE(var, 0, max)
#define AINRANGE( var, array ) INRANGE0(var, ARRAY_SIZE(array) )
#define EAINRANGE( index, eavar ) (INRANGE0( index, eaSize( &(eavar))))
#define EAINTINRANGE( index, eaintvar ) (INRANGE0( index, eaiSize(&(eaintvar))))
#define AGET(var, array) (AINRANGE(var,array)?array[var]:0)

// ------------------------------------------------------------
// bit ops

#define POW_OF_2(var) (!((var) & ((var) - 1)))
__forceinline static int get_num_bits_set( int v )
{
    int c; // c accumulates the total bits set in v
    for (c = 0; v; c++)
    {
        v &= v - 1; // clear the least significant bit set
    }    
	return c;
}

#define stableSort mergeSort

// A merge sort implementation that runs O(nlogn).

void mergeSort(SA_PARAM_NN_VALID void *base, size_t num, size_t width, const void* context,
			   int (__cdecl *comp)(const void* item1, const void* item2, const void* context));

//------------------------------------------------------------
//  committer aaron is an idiot.
//----------------------------------------------------------
void bubbleSort(SA_PARAM_NN_VALID void *base, size_t num, size_t width, const void* context,
				int (__cdecl *comp)(const void* item1, const void* item2, const void* context));

// returns location where this key should be inserted into array (can be equal to n)
size_t bfind(SA_PARAM_NN_VALID const void* key, SA_PRE_OP_RELEMS_VAR(num) SA_POST_OP_VALID const void* base, size_t num, size_t width, int (__cdecl *compare)(const void*, const void*));
size_t bfind_s(SA_PARAM_NN_VALID const void* key, SA_PRE_OP_RELEMS_VAR(num) SA_POST_OP_VALID const void* base, size_t num, size_t width, int (__cdecl *compare)(void *, const void*, const void*), void * context);

#define strdup_alloca(dst,src) {const char* srcCopy=src; size_t len = strlen(srcCopy)+1; dst = (char *)_alloca(len);strcpy_s(dst,len,srcCopy);}
// return an allocated copy of a filled-in format string
SA_RET_OP_STR char *strdupf_dbg(const char *caller_fname, int line, FORMAT_STR const char *fmt, ...);
#define strdupf(fmt, ...) strdupf_dbg(__FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)


//tokenizes a string, but returning punctuation characters in a list, with spaces as whitespace. So, if ',' 
//is punctuation, then repeated calls to tokenize "this,, ,is    fun," will return "this" "," "," "," "is" "fun" ","
//max token length = 32
//
//pPunctuationString can contain more than one piece of punctuation, ie, ",;!"
//
//call with NULL pInputString to reset
SA_ORET_OP_VALID char *strTokWithSpacesAndPunctuation(const char *pInputString, const char *pPunctuationString);
C_DECLARATIONS_END

// Flag helpers

// If eFlagBitsToCheck is 0, we matched them
#define FlagsMatchAny(eSourceFlagBits,eFlagBitsToCheck) (!(eFlagBitsToCheck) || ((eSourceFlagBits) & (eFlagBitsToCheck)) != 0)

// If eFlagBitsToCheck is 0, we matched them
#define FlagsMatchAll(eSourceFlagBits,eFlagBitsToCheck) (!(eFlagBitsToCheck) || ((eSourceFlagBits) & (eFlagBitsToCheck)) == (eFlagBitsToCheck))

// Not quite the same as the inverse of Any, because if eFlagBitsToCheck is 0, this also returns true
#define FlagsMatchNone(eSourceFlagBits,eFlagBitsToCheck) (((eSourceFlagBits) & (eFlagBitsToCheck)) == 0)

__forceinline static F32 fillF32FromStr(const char *str, F32 def)
{
	int argc;
	F32 ret = def;
	char *args[10];
	char *s = NULL;

	if (!str)
		return def;

	estrStackCreate(&s);
	estrCopy2(&s, str);
	argc = tokenize_line(s, args, NULL);
	if (argc > 0)
		ret = atof(args[0]);
	estrDestroy(&s);
	return ret;
}

__forceinline static void fillVec3sFromStr(const char *str, Vec3 v[], int vec_count)
{
	int i, j, argc;
	char *args[10];
	char *s = NULL;

	if (!str)
	{
		for (i = 0; i < vec_count; ++i)
		{
			v[i][0] = 0;
			v[i][1] = 0;
			v[i][2] = 0;
		}
		return;
	}

	estrStackCreate(&s);
	estrCopy2(&s, str);
	argc = tokenize_line(s, args, NULL);
	if (argc == 1)
	{
		F32 val;
		val = atof(args[0]);
		for (i = 0; i < vec_count; ++i)
		{
			v[i][0] = val;
			v[i][1] = val;
			v[i][2] = val;
		}
	}
	else
	{
		for (i = 0, j = 0; i < vec_count; ++i)
		{
			if (j < argc)
				v[i][0] = atof(args[j++]);
			else
				v[i][0] = 0;

			if (j < argc)
				v[i][1] = atof(args[j++]);
			else
				v[i][1] = 0;

			if (j < argc)
				v[i][2] = atof(args[j++]);
			else
				v[i][2] = 0;
		}
	}
	estrDestroy(&s);
}


__forceinline static int inline_stricmp(const char *a,const char *b)
{
	int	t;
	if(a == b)
		return 0;

	for(;*a && *b;a++,b++)
	{
		if (*a != *b)
		{
			t = tolower(*a) - tolower(*b);
			if (t)
				return t;
		}
	}
	return *a - *b;
}

__forceinline static S32 isSlash(char c){
	return c == '/' || c == '\\';
}

__forceinline static S32 isNullOrSlash(char c){
	return !c || isSlash(c);
}


typedef enum
{
	DIVIDESTRING_POSTPROCESS_NONE = 0,

	//any or all of the following can always be applied
	DIVIDESTRING_POSTPROCESS_FORWARDSLASHES							= 1 << 0,
	DIVIDESTRING_POSTPROCESS_BACKSLASHES							= 1 << 1,
	DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE						= 1 << 2,
	
	//applied after STRIP_WHITESPACE, so they are useful together
	DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS				= 1 << 3,

	//case insensitive, don't push something into the list that is already in the list
	DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE							= 1 << 4,

	//treat \r\n as a single seperator, not as two. (Generally harmless, doesn't make assumptions.)
	DIVIDESTRING_POSTPROCESS_WINDOWSNEWLINES						= 1 << 5,

	//gives you the expected return from <<"hi, there" , foo , bar>>. Implicity also strips whitespace. Currently
	//no escaping at all
	DIVIDESTRING_RESPECT_SIMPLE_QUOTES								= 1 << 6,


	//only one of the following will be applied.
	//If any of these 4 are specified, the strings will be AllocAdded. Otherwise they will be strdup'd.
	DIVIDESTRING_POSTPROCESS_ALLOCADD								= 1 << 16,
	DIVIDESTRING_POSTPROCESS_ALLOCADD_CASESENSITIVE					= 1 << 17,

	DIVIDESTRING_POSTPROCESS_ESTRINGS								= 1 << 18,



} enumDivideStringPostProcessFlags;

#define DIVIDESTRING_STANDARD 	(DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS)


//NON-destructively divide a string into its substrings, put the substrings into an earray, 
//then optionally forward or backslash them 
void DivideString(const char *pInString, char *pSeparators, char ***pppOutStrings, enumDivideStringPostProcessFlags ePostProcessFlags);

//NON-destructively finds all consecutive collections of alphanumeric characters (alphabet, 0-9, _), puts them
//in an earray
#define ExtractAlphaNumTokensFromString(pInString, pppOutStrings) ExtractAlphaNumTokensFromStringEx(pInString, pppOutStrings, NULL)

//pExtraLegalIdentChars is an optional string of additional characters that are "legal" in the identifiers (for instance, @ and . to 
//allow parsing of email addresses)
void ExtractAlphaNumTokensFromStringEx(char *pInString, char ***pppOutStrings, char *pExtraLegalIdentChars);



char *loadCmdline(char *cmdFname,char *buf,int bufsize); // for loading cmdline.txt
void loadCmdLineIntoEString(char *cmdFname, char **ppEString);

//calls GetCommandLine() (a system func), then returns just the args, not the executable name
const char *GetCommandLineWithoutExecutable(void);

//given an earray of strings, finds the longest shared prefix they have, puts it in an EString
void FindSharedPrefix(char **ppOutEString, char **ppInStrings);

//support for the ASSERT_COMPLETES macro
typedef void (*voidVoidFunc)(void);
int AssertCompletes_BeginTiming(int iNumSecs, char *pCommand, voidVoidFunc callback, char *pFile, int iLineNum);
void AssertCompletes_StopTiming(int iSlotNum);
void AssertCompletes_InitSystem(void);

//this macro can be wrapped around a line of code you are going to execute, and will cause an assert if your line of code
//doesn't complete within a certain number of seconds
#define ASSERT_COMPLETES(numSecs, command) \
{ \
	int iAssertCompletesLocalSlotNum = AssertCompletes_BeginTiming(numSecs, #command, NULL, __FILE__, __LINE__); \
	command; \
	AssertCompletes_StopTiming(iAssertCompletesLocalSlotNum); \
}
#define ASSERT_COMPLETES_CALLBACK(numSecs, callback, command) \
{ \
	int iAssertCompletesLocalSlotNum = AssertCompletes_BeginTiming(numSecs, #command, callback, __FILE__, __LINE__); \
	command; \
	AssertCompletes_StopTiming(iAssertCompletesLocalSlotNum); \
}


#if !PLATFORM_CONSOLE

typedef struct QueryableProcessHandle QueryableProcessHandle;

//returns non-zero if it successfully started the process
//
//NOTE: only one queryable process at a time can have a filenameForOuptut. Aside from that, multiple can be run
//at once and are thread safe etc.

//if you specify a pointer for pOutPID, then it launches the cmd line directly, rather than launching it via cmd.exe, which is
//usually more stable
QueryableProcessHandle *StartQueryableProcessEx(const char *pCmdLine, const char *pWorkingDir, bool bRunInMyConsole, bool minimized, bool hidden,
	char *pFileNameForOutput, int *pOutPID);


#define StartQueryableProcess(pCmdLine, pWorkingDir,  bRunInMyConsole, minimized, hidden, pFileNameForOutput) StartQueryableProcessEx(pCmdLine, pWorkingDir,  bRunInMyConsole, minimized, hidden, pFileNameForOutput, NULL)
QueryableProcessHandle *StartQueryableProcess_WithFullDebugFixup(const char  *pCmdLine, const char *pWorkingDir, bool bRunInMyConsole, bool minimized, bool hidden,
	char *pFileNameForOutput);

bool QueryableProcessComplete(QueryableProcessHandle **ppHandle, int *pReturnVal);
void KillQueryableProcess(QueryableProcessHandle **ppHandle);


//uses the above functions to provide an improved version of system() with a failure timeout. Also won't
//cause the parent process to crash if the child process does
#undef system
#define system(a) x_system(a)

int x_system(const char *pCmdLine);

//returns -1 on timeout
int system_w_timeout(char *pCmdLine, char *pWorkingDir, U32 iFailureTime);
int system_w_workingDir( char* cmd,  char* pWorkingDir);


// Run a subprocess and capture the output
int system_w_output(char *cmd, char **outstr);

// Run a subprocess with a given string on the input
int system_w_input(char *cmd, char *input, size_t intput_len, bool detach, bool hidden);

int system_w_output_and_timeout(char *cmd, char **outstr, int iTimeout);

// Create a pipe buffer.
int pipe_buffer_create(const char *buffer, size_t buffer_size);

// Read from a pipe buffer.
size_t pipe_buffer_read(char *buffer, size_t buffer_size, int pipe);

// Close a pipe buffer.
void pipe_buffer_cleanup(int pipe);

#endif

// hex string conversions
unsigned char* hexStrToBinStr(const char* hexString, int strLength);
char* binStrToHexStr(const unsigned char* binArray, int arrayLength);

#define memdup(src, byte_count) memdup_dbg(src, byte_count, __FILE__, __LINE__)
SA_RET_NN_BYTES_VAR(byte_count) __forceinline static void *memdup_dbg(const void *src, size_t byte_count, const char *caller_fname, int line)
{
	void *ret;
	ret = smalloc(byte_count);
	assert(ret);
	ANALYSIS_ASSUME(ret);
	memcpy_s(ret, byte_count, src, byte_count);
	return ret;
}

// Like strstr(), but the string to be searched is not null-terminated.
char *memstr(const char *haystack, const char *needle, size_t haystack_size);

bool memIsZero(const void *ptr, U32 num_bytes);
#define StructIsZero(ptr) memIsZero((ptr), sizeof(*(ptr)))

//macros and functions used to cram a float into a intptr_t and get it back out. Used by structparser and textparser
//to make default values work for embedded floats
intptr_t GET_INTPTR_FROM_FLOAT(float f);

//in order to make this work smoothly, if the default value is an int between -1024 and 1024, just use it and 
//assume it can't be a float
#define GET_FLOAT_FROM_INTPTR(n) (((n) >= -1024 && (n) <= 1024) ? (n) : (*((float*)(&(n)))))

void freeWrapper(void *data);

// Finds the next permutation in lexogrpahical order, e.g. 1,2,3 -> 1,3,2 -> 2,1,3 -> 2,3,1 -> 3,1,2 -> 3,2,1
bool nextPermutation(int *list, int N) ;
// Finds the i'th permutation. Assumes that indices range from 0 to iSize-1. Automatically fills pList.
void indexPermutation(int iIndex, int *pList, int iSize);


#define ASSERT_CALLED_IN_SINGLE_THREAD \
	static DWORD threadid=0; \
	int single_thread_dummy = (int)((threadid==0)?(threadid=GetCurrentThreadId()):(assertmsg(threadid==GetCurrentThreadId(), "Function should only be called in one thread")))


//calling this function loads in 4 megabytes of ip directory data, which then hangs around forever
char *ipToCountryName(U32 iIP);

// Global movement log, for logging global things to any active movement logs.

extern U32 globMovementLogIsEnabled;

typedef void (*GlobalMovementLogFunc)(FORMAT_STR const char* format, va_list va);
typedef void (*GlobalMovementLogCameraFunc)(const char* tags, const Mat4 mat);
typedef void (*GlobalMovementLogSegmentFunc)(const char* tags, const Vec3 p0, const Vec3 p1, U32 argb);

void	globMovementLogSetFuncs(GlobalMovementLogFunc logFunc,
								GlobalMovementLogCameraFunc logCameraFunc,
								GlobalMovementLogSegmentFunc logSegmentFunc);

void	wrapped_globMovementLog(FORMAT_STR const char* format,
								...);

void	wrapped_globMovementLogCamera(	const char* tags,
										const Mat4 mat);
									
void	wrapped_globMovementLogSegment(	const char* tags,
										const Vec3 p0,
										const Vec3 p1,
										U32 argb);

#define globMovementLog(format, ...)				(globMovementLogIsEnabled?wrapped_globMovementLog(FORMAT_STRING_CHECKED(format), ##__VA_ARGS__),0:0)
#define globMovementLogCamera(tags, mat)			(globMovementLogIsEnabled?wrapped_globMovementLogCamera(tags, mat),0:0)
#define globMovementLogSegment(tags, p0, p1, argb)	(globMovementLogIsEnabled?wrapped_globMovementLogSegment(tags, p0, p1, argb),0:0)

// GenericLogReceiver, simple way to create a callback object to pass around to receiver logging.

typedef enum GenericLogReceiverMsgType {
	GLR_MSG_LOG_TEXT,
	GLR_MSG_LOG_SEGMENTS,
} GenericLogReceiverMsgType;

typedef struct GenericLogReceiverMsg {
	GenericLogReceiverMsgType	msgType;
	void*						userPointer;

	union {
		struct {
			const char*			format;
			va_list				va;
		} logText;

		struct {
			const char*			tags;
			U32					argb;
			const Vec3*			segments;
			U32					count;
		} logSegments;
	};
} GenericLogReceiverMsg;

typedef void (*GenericLogReceiverMsgHandler)(const GenericLogReceiverMsg* msg);
typedef struct GenericLogReceiver GenericLogReceiver;

void glrCreate(	GenericLogReceiver** rOut,
				GenericLogReceiverMsgHandler msgHandler,
				void* userPointer);

void glrDestroy(GenericLogReceiver** rInOut);

void wrapped_glrLog(GenericLogReceiver* r,
					FORMAT_STR const char* format,
					...);
#define glrLog(r, format, ...)\
			((r)?wrapped_glrLog(r, format, __VA_ARGS__),0:0)

void wrapped_glrLogSegment(	GenericLogReceiver* r,
							const char* tags,
							U32 argb,
							const Vec3 pos1,
							const Vec3 pos2);
#define glrLogSegment(r, tags, argb, pos1, pos2)\
			((r)?wrapped_glrLogSegment(r, tags, argb, pos1, pos2),0:0)

void wrapped_glrLogSegmentOffset(	GenericLogReceiver* r,
									const char* tags,
									U32 argb,
									const Vec3 pos,
									const Vec3 offset);
#define glrLogSegmentOffset(r, tags, argb, pos, offset)\
			((r)?wrapped_glrLogSegmentOffset(r, tags, argb, pos, offset),0:0)

// PointerCounter: a simple way to count lots of things and get the n most common.

typedef struct PointerCounter PointerCounter;
typedef struct 
{
	const void *pPtr;
	int iCount;
} PointerCounterResult;

typedef struct PointerCounter
{
	PointerCounterResult **ppResults;
} PointerCounter;

PointerCounter *PointerCounter_Create(void);
void PointerCounter_AddSome(PointerCounter *pCounter, const void *pPtr, int iCount);
void PointerCounter_GetMostCommon(PointerCounter *pCounter, int iNumToReturn, PointerCounterResult ***pppResults);
void PointerCounter_Destroy(PointerCounter **ppCounter);


//convert U32s to 8-character checksummed strings, using an unambiguous 34-character alphabet consisting of uppercase letters and
//digits, with 0 and 1 equivalent to O and I. 
#define ID_STRING_LENGTH 8
#define ID_STRING_BUFFER_LENGTH (ID_STRING_LENGTH + 1)

void IDString_IntToString(U32 iInt, char outString[ID_STRING_BUFFER_LENGTH], U8 iExtraHash);

//returns true if string is valid
bool IDString_StringToInt(const char *pString, U32 *pOutInt, U8 iExtraHash);



//this gadget is to solve a problem that may be unique to the controller, where the controller realizes that something has gone wrong
//with some server by doing checks of "I haven't heard from X for Y seconds", but we don't want this to be accidentally hit as an 
//edge case after a controller stall, or anything of that sort. So we want to be able to make sure that the condition has been
//true for a certain length of time and a certain number of controller frames before we trigger.
typedef struct ConditionChecker ConditionChecker;

ConditionChecker *ConditionChecker_Create(int iSecondsConditionsMustBeTrueFor, int iServerFramesConditionMustBeTrueFor);

void ConditionChecker_BetweenConditionUpdates(ConditionChecker *pChecker);
bool ConditionChecker_CheckCondition(ConditionChecker *pChecker, const char *pKey /*POOL_STRING*/);
void ConditionChecker_Destroy(ConditionChecker **ppChecker);

//takes a bunch of two-way relationships of string-named objects and does a (not-algorithmically-optimized)
//graph-connection grouping on them so that any two things that end up in the same list have some 
//linkage
//
//If anyone wants to use this for something performance intensive, there are some fairly easy improvements,
//particularly removing unneeded string frees and dups, and caching the most recent group lookup. Also, when
//joining two groups together, should always destroy and add the smaller of the two, etc.
typedef struct RelationshipGrouper RelationshipGrouper;

AUTO_STRUCT;
typedef struct RelationshipGroup
{
	int iIndex; //unique non-zero integer, no guarantees as to ordering etc
	char **ppNames;
} RelationshipGroup;

RelationshipGrouper *RelationshipGrouper_Create(void);
void RelationshipGrouper_AddRelationship(RelationshipGrouper *pGrouper, const char *pName1, const char *pName2);

bool RelationshipGrouper_AreTwoItemsRelated(RelationshipGrouper *pGrouper, const char *pName1, const char *pName2);

//when you are done, call eaDestroy, NOT eaDestroyStruct
void RelationshipGrouper_GenerateGroups(RelationshipGrouper *pGrouper, RelationshipGroup ***pppGroups);
void RelationshipGrouper_Destroy(RelationshipGrouper *pGrouper);

typedef void (*diskSpaceCheckCallBack)(U64 uFreeBytes, void *userdata);

//begins a periodic callback which checks free disk space and alerts if it is below a certain percent of free space
//
//pass in 0 for driveLetter to get the drive that the executable is on
void BeginPeriodicFreeDiskSpaceCheck(char cDriveLetter, int iSecondsBetweenChecks, int iSecondsBetweenAlerts, int iMinFreePercent, U64 iMinFreeAbsolute,
	diskSpaceCheckCallBack pCallback, void *pUserdata);

//returns an HTML string that approximates the current console contents
void ConsoleCaptureHTML(char **ppOutString);