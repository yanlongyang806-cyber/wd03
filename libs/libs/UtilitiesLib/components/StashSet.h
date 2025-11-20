#pragma once
GCC_SYSTEM

C_DECLARATIONS_BEGIN

//----------------------------------
// A StashSet is a hash set specifically for strings (if you want Int or Address types, use an eSet)
// optimized for the StringCache (and, presumably, only ever used for the StringCache)
//----------------------------------
typedef struct StashSetImp*			StashSet;
typedef const struct StashSetImp*			cStashSet;
typedef void (*Destructor)(void* value);

typedef struct
{
	StashSet		pSet;
	U32				uiIndex;
} StashSetIterator;

// -------------------------
// Set management and info
// -------------------------

StashSet			stashSetCreate(U32 uiInitialSize, const char *caller_fname, int line);

void				stashSetDestroy(StashSet pSet);
__forceinline static void stashSetDestroySafe(StashSet* ppSet){if(*ppSet){stashSetDestroy(*ppSet);*ppSet=NULL;}}
void				stashSetDestroyEx(StashSet pSet, Destructor keyDstr);
void				stashSetClear(StashSet pSet);
void				stashSetClearEx(StashSet pSet, Destructor keyDstr);

int					stashSetVerifyStringKeys(StashSet pSet); // Checks that no keys have been corrupted (returns 0 on failure)
bool				stashSetValidateValid(cStashSet pSet);

// set info 
U32					stashSetGetValidElementCount(cStashSet set);
U32					stashSetGetOccupiedSlots(cStashSet set);
U32					stashSetCountElements(cStashSet set);
U32					stashSetGetMaxSize(cStashSet set);
U32					stashSetGetResizeSize(cStashSet set);
size_t				stashSetGetMemoryUsage(cStashSet set);
void				stashSetSetThreadSafeLookups(StashSet set, bool bSet);
void				stashSetSetCantResize(StashSet set, bool bSet);
void				stashSetSetGrowFast(StashSet set, bool bSet);

// Iterators
void				stashSetGetIterator(StashSet pSet, StashSetIterator* pIter);
bool				stashSetGetNextElement(StashSetIterator* pIter, const char** ppElem);

// -----------
// String Keys
// -----------
bool				stashSetFind(StashSet set, const char* pKey, const char** ppValue);
bool				stashSetAdd(StashSet set, const char* pKey, bool bOverwriteIfFound, const char **ppKeyOut);
bool				stashSetRemove(StashSet set, const char* pKey, const char ** ppValue);

C_DECLARATIONS_END
