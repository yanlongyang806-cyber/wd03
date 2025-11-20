#pragma once
GCC_SYSTEM
//this used to be the shader preprocessor code written by Jimb. Alex has ripped it out and turned
//it into generic preprocessing code so that textparser can use it

typedef struct FileEntry FileEntry;
typedef FileEntry** FileList;

#define MAX_INCLUDE_DEPTH 5

//resets generic preprocessing
void genericPreProcReset(void);


//hashes all the defines, returns the hash value
U32 genericPreProcHashDefines(void);

//update the cryptAlder32 with the hashes of all the #defines
//(assumes that it is being called in the middle of an ongoing hashing
//process
void genericPreProcHashDefines_ongoing(void);


//get the number of defines, get at any define
int genericPreProcGetNumDefines(void);
char *genericPreProcGetNthDefine(int n);

typedef enum PreProcFlags
{
	PreProc_Default = 0,
	PreProc_UseCRCs = 1<<0, // Use CRCs instead of timestamps when adding to the filelist
} PreProcFlags;

//processes all includes in a string (returns the number of include directives found
int genericPreProcIncludes(char **data, const char *path, const char *sourcefilename, FileList *file_list, PreProcFlags flags);

//processes all macros in a string
int genericPreProcMacros_dbg(char **data MEM_DBG_PARMS);
#define genericPreProcMacros(data) genericPreProcMacros_dbg(data MEM_DBG_PARMS_INIT)

//process all ifdefs in a string
void genericPreProcIfDefs(char *data, char **strtokcontext, int ignore, const char *filename);

void genericPreProcAddDefine(const char *pDefine);
char *genericPreProcGetDefinesString(void);

void genericPreProcSetCommentMarkers(char c0, char c1);


void genericPreProcEnterCriticalSection(void);
void genericPreProcLeaveCriticalSection(void);
