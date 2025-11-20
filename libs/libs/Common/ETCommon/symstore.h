#ifndef SYMSTORE_H
#define SYMSTORE_H

#define SYMSERVLOOKUP_MEM_HARDCAP 2048 // in MB, cap value at which point SymServLookup.exe is forcibly restarted by Error Tracker
#define SYMSERVLOOKUP_MEM_SOFTCAP 1024 // in MB, cap value at which point SymServLookup.exe tries to free up space by removing cached modules
#define MILLION 1048576.0

// Forwards
typedef struct CallStack CallStack;
typedef struct StackTraceLine StackTraceLine;
typedef struct NOCONST(StackTraceLine) NOCONST(StackTraceLine);
typedef struct NetLink NetLink;

typedef struct SymServStatusStruct
{
	NetLink *link;
	char *hashString;
} SymServStatusStruct;


typedef void (*SymStoreStatusCallback) (int statuscount, void *data);
typedef void (*SymStoreModuleFailureCallback) (void *data, char *module, char *guid);

void setSymbolLogFile(const char *file);
const char * getSymbolLogFile(void);

bool InitCOMAndDbgHelp(void);
bool InitSymbolServer(void);
bool symstoreLookupStackTrace(CallStack *pSrc, NOCONST(StackTraceLine) ***pDst, SymStoreStatusCallback cbStatusUpdate, void *data, const char *extraErrorLogData, SymStoreModuleFailureCallback cbFailureReport);

void symStore_TrimCache(void);

#endif
