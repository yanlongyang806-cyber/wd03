#ifndef _REFERENCESYSTEM_H_
#define _REFERENCESYSTEM_H_
GCC_SYSTEM

#include "stashtable.h"

C_DECLARATIONS_BEGIN

typedef struct Packet Packet;
typedef struct RefDictIterator RefDictIterator;
typedef void **EArrayHandle;
typedef void (*ReferentDestructor)(void*);
typedef struct ResourceCache ResourceCache;
typedef struct PackedStructStream PackedStructStream;
typedef struct ResourceDictionary ResourceDictionary;

/********************************* System Definitions *********************************/

// set to 1 to make all reference checks much more careful, but slower
#define REFSYSTEM_DEBUG_MODE 0

//a handle that is active, but whose referent is absent.
#define REFERENT_SET_BUT_ABSENT ((void*)0x1ll)

// a pointer to a bad referent, to indicate an error 
#define REFERENT_INVALID ((void*)0xffffffffffffffffll)



// reference dictionaries can be referred to two ways:
// (1) By string (name) (slower)
// (2) By DictionaryHandle (faster)
// 
// All the Reference System functions are set up to take either type of argument, and automatically
// figure out how to treat it
typedef const void *DictionaryHandle;

// a define to make it super-obvious from looking at the function prototype that it takes a handle or string
//  Also needs to be const since names are const (but dictionaries are not)
typedef const void *DictionaryHandleOrName;

// a reference handle is what the outside world uses to get pointers to the data is has a reference to. It's basically
// a managed pointer, but might sometimes (during debug modes) be a little struct full of cookies or what
// have you
typedef void *ReferenceHandle;
typedef void *const ConstReferenceHandle;

// a pointer to "reference data" is a pointer to the actual reference itself, that is, the string that describes 
// the Referent, or the hierarchical structs that define the Referent
typedef void *ReferenceData;
typedef const void *ConstReferenceData;

// a "Referent" is the thing that is pointed to by a reference. It's just a pointer, but I've given it a type to make
// things as generally unambiguous as possible
typedef void *Referent;
typedef const void *ConstReferent;

// A "ReferentPrevious" is all the data needed to send binary diffs to clients for
// container subscriptions.
typedef struct ReferentPrevious {
	Referent	referent;
	Referent	referentPrevious;
	U32			version;
	U32			lock;
	U32			isQueuedForCopy : 1;
} ReferentPrevious;

// The "filename" of referenced objects that have no file (references created on the fly).
#define NO_FILE "__no_file__"

/* this macro is used inside a struct definition to define a reference to a particular type.
so instead of

Power *pMyPower;

you would do

REF_TO(Power) hMyPower;
*/
#define REF_TO(myType) union { myType * __pData_INTERNAL; ReferenceHandle __handle_INTERNAL; }

// A non-modifiable reference
#define CONST_REF_TO(myType) union { myType * const __pData_INTERNAL; ConstReferenceHandle __handle_INTERNAL; }

#define REF_HANDLEPTR(handleName)		(&(handleName).__handle_INTERNAL)

// initializes the reference system
void RefSystem_Init(void);


/********************************* Handle Operations *********************************/

// For all of these operations, there is a macro version that works on handles, and a function
// that avoids the type safety. Use the macros.

// This macro takes the handle and gives you back what it refers to. Always use this
// macro instead of manually dereferencing it
//  In debug mode, calls the slow verify function
//  In normal mode this is just a pointer lookup

#define REF_IS_SET_BUT_ABSENT(handleName) ((handleName).__pData_INTERNAL == REFERENT_SET_BUT_ABSENT)

#if REFSYSTEM_DEBUG_MODE
#define GET_REF(handleName) (RefSystem_ReferentFromHandle(&(handleName).__handle_INTERNAL) ? (handleName).__pData_INTERNAL : REF_IS_SET_BUT_ABSENT(handleName) ? NULL : (handleName).__pData_INTERNAL)
#else
#define GET_REF(handleName) (REF_IS_SET_BUT_ABSENT(handleName) ? NULL : (handleName).__pData_INTERNAL)
#endif

#define REF_IS_VALID(handleName) (RefSystem_IsReferentStringValid(REF_DICTNAME_FROM_HANDLE(handleName), REF_STRING_FROM_HANDLE(handleName)))

// This takes some time to verify the integrity of the handle and all the internal stuff.
// In debug mode, this is called on every dereference, but CheckIntegrity uses it in
// normal mode to look for errors.
SA_RET_OP_VALID Referent RefSystem_ReferentFromHandle(SA_PRE_NN_NN_VALID ConstReferenceHandle *pHandle);


// Takes reference data describing a referent, and adds a reference to that referent.
// dictNameOrHandle is the name of the reference dictionary,
// pRefData is a pointer to the ReferenceData, and handleName is your handle.
#define SET_HANDLE_FROM_REFDATA(dictNameOrHandle, pRefData, handleName) RefSystem_SetHandleFromRefData(dictNameOrHandle, pRefData, &(handleName).__handle_INTERNAL)

// Function used by above macros. Do not call directly
bool RefSystem_SetHandleFromRefDataWithReason(DictionaryHandleOrName dictHandle, ConstReferenceData pRefData, ReferenceHandle *pHandle, const char* reason);
#define RefSystem_SetHandleFromRefData(dictHandle, pRefData, pHandle) RefSystem_SetHandleFromRefDataWithReason(dictHandle, pRefData, pHandle, __FUNCTION__)


// Takes an int describing a referent (for int key dictionaries), and adds a reference to that referent.
#define SET_HANDLE_FROM_INT(dictNameOrHandle, iRefData, handleName) RefSystem_SetHandleFromInt(dictNameOrHandle, iRefData, &(handleName).__handle_INTERNAL, __FUNCTION__)

// Function used by above macro. Do not call directly.
__forceinline static bool RefSystem_SetHandleFromInt(DictionaryHandleOrName dictHandle, int iRefData, ReferenceHandle *pHandle, const char* reason)
{
	return RefSystem_SetHandleFromRefDataWithReason(dictHandle, &iRefData, pHandle, reason);
}

// Takes an actual referent and adds a reference to it.
// dictNameOrHandle is the name of the reference dictionary,
// pReferent is a pointer to the Referent, and handleName is your handle.
#define SET_HANDLE_FROM_REFERENT(dictNameOrHandle, pReferent, handleName) RefSystem_SetHandleFromReferent(dictNameOrHandle, pReferent, &(handleName).__handle_INTERNAL, __FUNCTION__)

// Function used by above macros. Do not call directly
bool RefSystem_SetHandleFromReferent(DictionaryHandleOrName dictHandle, ConstReferent pReferent, ReferenceHandle *pHandle, const char* reason);


// Takes a string description of a referent, and adds a reference to that referent
#define SET_HANDLE_FROM_STRING(dictNameOrHandle, string, handleName) RefSystem_SetHandleFromString(dictNameOrHandle, string, &(handleName).__handle_INTERNAL)

bool RefSystem_SetHandleFromStringWithReason(DictionaryHandleOrName dictHandle, const char *pString, ReferenceHandle *pHandle, const char* reason);
#define RefSystem_SetHandleFromString(dictHandle, pString, pHandle) RefSystem_SetHandleFromStringWithReason(dictHandle, pString, pHandle, __FUNCTION__)


// sets DstHandle to point to the same thing SrcHandle is pointing to
// Removes the DstHandle (if active), then sets it to the SrcHandle (if active)
#define COPY_HANDLE(dstHandleName, srcHandleName) RefSystem_CopyHandleWithReason(&(dstHandleName).__handle_INTERNAL, &(srcHandleName).__handle_INTERNAL, __FUNCTION__);

bool RefSystem_CopyHandleWithReason(ReferenceHandle *pDstHandle, ConstReferenceHandle *pSrcHandle, const char* reason);
#define RefSystem_CopyHandle(pDstHandle, pSrcHandle) RefSystem_CopyHandleWithReason(pDstHandle, pSrcHandle, __FUNCTION__)


// This tells the reference system that a handle has moved from one place to another. Note that this does
// not read or write data to either newHandle or oldHandle. The assumption is that if you are relocating a struct
// that contains handles, you will allocate memory for the new struct, and use memcpy to copy all the struct data in 
// one fell swoop, which clones the actual data inside the reference handle, so that the reference system doesn't
// need to actually set the handle. However, the reference system DOES need to know where the handle now is.
#define MOVE_HANDLE(newHandle, oldHandle) RefSystem_MoveHandle(&(newHandle).__handle_INTERNAL, &(oldHandle).__handle_INTERNAL)

void RefSystem_MoveHandle(ReferenceHandle *pNewHandle, ReferenceHandle *pOldHandle);


// use this macro to remove a reference. You MUST remove all handles you create.
#define REMOVE_HANDLE(handleName) RefSystem_RemoveHandleWithReason(&(handleName).__handle_INTERNAL, __FUNCTION__)

// Function used by above macros. Do not call directly
void RefSystem_RemoveHandleWithReason(ReferenceHandle *pHandle, const char* reason);
#define RefSystem_RemoveHandle(pHandle) RefSystem_RemoveHandleWithReason(pHandle, __FUNCTION__)


// Checks whether the given ReferenceHandle is "active" with the system. Which is basically equivalent to checking
// whether the given piece of memory is a Reference Handle
#define IS_HANDLE_ACTIVE(handleName) ((handleName).__handle_INTERNAL != NULL)

bool RefSystem_IsHandleActive(ConstReferenceHandle *pHandle);


// Takes a reference handle, and returns the ReferenceData for it
#define REF_DATA_FROM_HANDLE(handleName) ((handleName).__handle_INTERNAL == NULL ? NULL : RefSystem_RefDataFromHandle(&(handleName).__handle_INTERNAL))

ReferenceData RefSystem_RefDataFromHandle(ConstReferenceHandle *pHandle);


// Takes a reference handle, and returns the int key (ReferenceData) for it
#define REF_INT_FROM_HANDLE(handleName) ((handleName).__handle_INTERNAL == NULL ? 0 : RefSystem_RefIntKeyFromHandle(&(handleName).__handle_INTERNAL))

__forceinline static int RefSystem_RefIntKeyFromHandle(ConstReferenceHandle *pHandle)
{
	int *pData = (int *)RefSystem_RefDataFromHandle(pHandle);
	return SAFE_DEREF(pData);
}


// Takes a reference handle, and returns the string description of it
#define REF_STRING_FROM_HANDLE(handleName) ((handleName).__handle_INTERNAL == NULL ? NULL : RefSystem_StringFromHandle(&(handleName).__handle_INTERNAL))

//only use this if you know the result will be non-NULL... this is much slower in the NULL case otherise
#define REF_STRING_FROM_HANDLE_KNOWN_NONNULL(handleName) (RefSystem_StringFromHandle(&(handleName).__handle_INTERNAL))

const char *RefSystem_StringFromHandle(ConstReferenceHandle *pHandle);

// given a reference handle, append the given string to the passed in estring
void RefSystem_AppendReferenceString(char **ppEString, ConstReferenceHandle *pHandle);


// Takes a reference handle and returns the dictionary name the referent is in
#define REF_DICTNAME_FROM_HANDLE(handleName) ((handleName).__handle_INTERNAL == NULL ? NULL : RefSystem_DictionaryNameFromHandle(&(handleName).__handle_INTERNAL))

const char *RefSystem_DictionaryNameFromHandle(ConstReferenceHandle *pHandle);

#define REF_COMPARE_HANDLES(handle1, handle2) ((handle1).__handle_INTERNAL != (handle2).__handle_INTERNAL ? 0 : (REF_IS_SET_BUT_ABSENT(handle1) ? RefSystem_CompareHandles(&(handle1).__handle_INTERNAL, &(handle2).__handle_INTERNAL) : 1))

//returns true if they match, false otherwise
bool RefSystem_CompareHandles(ConstReferenceHandle *pHandle1, ConstReferenceHandle *pHandle2);

#define REF_PARSETABLE_FROM_HANDLE(handleName) (RefSystem_ParseTableFromHandle(&(handleName).__handle_INTERNAL))

// Given a handle, return the ParseTable associated with its dictionary.
ParseTable *RefSystem_ParseTableFromHandle(ConstReferenceHandle *pHandle);

#define REF_IS_REFERENT_SET_BY_SOURCE_FROM_HANDLE(handleName) (RefSystem_IsReferentSetBySource(&(handleName).__handle_INTERNAL))

// Given a handle, return whether the referent has been set by the source or is still waiting for data
bool RefSystem_IsReferentSetBySource(ConstReferenceHandle *pHandle);

/********************************* Referent Operations *********************************/

// These functions modify individual referents in a dictionary

// Tells the system that something new exists that might be referenced, in case there are already
// things loaded that are trying to point to it. This also actually "fills" the dictionary, if it's a
// self-defining dictionary
void RefSystem_AddReferentWithReason(DictionaryHandleOrName dictHandle, ConstReferenceData pData, Referent pReferent, const char* reason);
#define RefSystem_AddReferent(dictHandle, pData, pReferent) RefSystem_AddReferentWithReason(dictHandle, pData, pReferent, __FUNCTION__)

void RefSystem_MarkSetBySourceWithReason(DictionaryHandleOrName dictHandle, ConstReferenceData pRefData, const char* reason);
#define RefSystem_MarkSetBySource(dictHandle, pRefData) RefSystem_MarkSetBySourceWithReason(dictHandle, pRefData, __FUNCTION__)

__forceinline static void RefSystem_AddIntKeyReferentWithReason(DictionaryHandleOrName dictHandle, int iData, Referent pReferent, const char* reason)
{
	RefSystem_AddReferentWithReason(dictHandle, &iData, pReferent, reason);
}
#define RefSystem_AddIntKeyReferent(dictHandle, iData, pReferent) RefSystem_AddIntKeyReferentWithReason(dictHandle, iData, pReferent, __FUNCTION__)

// Removes a referent from the reference dictionary. All handles pointing to it will be set to NULL.
// If this referent was directly created by the reference system (because of network sync), it will be freed.
// In certain instances, you not only want handles pointing to this referent to become NULL, you actually
// want them to completely cease to be handles, that is, just return to being simple plain old NULL pointers. In
// that case, pass in true for bCompletelyRemoveHandlesToMe.
// Returns true if the referent was actually in a dictionary, otherwise false.
bool RefSystem_RemoveReferentWithReason(Referent pReferent, bool bCompletelyRemoveHandlesToMe, const char* reason);
#define RefSystem_RemoveReferent(pReferent, bCompletelyRemoveHandlesToMe) RefSystem_RemoveReferentWithReason(pReferent, bCompletelyRemoveHandlesToMe, __FUNCTION__)

// a referent has moved from one place in RAM to another. Update all handles pointing to it.
void RefSystem_MoveReferentWithReason(Referent pNewReferent, Referent pOldReferent, const char* reason);
#define RefSystem_MoveReferent(pNewReferent, pOldReferent) RefSystem_MoveReferentWithReason(pNewReferent, pOldReferent, __FUNCTION__)

// Given a string, returns the referent (if any) referred to by that string in the given dictionary
SA_ORET_OP_VALID Referent RefSystem_ReferentFromString(DictionaryHandleOrName dictHandle, const char *pString);

// Given a string, returns if the referent seems valid.  For loaded
// namespaces, equivalent to RefSystem_ReferentFromString returning
// not null or the string being empty, for unloaded namespaces assumed
// to be always true.
bool RefSystem_IsReferentStringValid(DictionaryHandleOrName dictHandle, const char *pString);

// Given a string, returns the ReferentPrevious referred to by that string in the given dictionary
ReferentPrevious* RefSystem_ReferentPreviousFromString(DictionaryHandleOrName dictHandle, const char *pString);

// Queue referent for backing up.
void RefSystem_QueueCopyReferentToPrevious(DictionaryHandleOrName dictHandle, const char *pString);

void RefSystem_CopyQueuedToPrevious(void);

// Clear any referent previous backup data
void RefSystem_ClearReferentPrevious(DictionaryHandleOrName dictHandle, const char *pString);

// Given a referent, return the string describing it
const char *RefSystem_StringFromReferent(Referent pReferent);

// counts the number of active handles pointing to a given referent
int RefSystem_GetReferenceCountForReferent(Referent pReferent);

// return if the given referent is in the system
bool RefSystem_DoesReferentExist(ConstReferent pReferent);

// tells the ref system that a referent was modified (this is optional, but is needed if you want
// to get RESEVENT_RESOURCE_MODIFIED callbacks)
void RefSystem_ReferentModifiedWithReason(Referent pReferent, const char* reason);
#define RefSystem_ReferentModified(pReferent) RefSystem_ReferentModifiedWithReason(pReferent, __FUNCTION__)

//given a referent string, returns true or false if the dictionary "knows about" that item. Then, if returns true,
//finds the pReferent and the number of active handles
//
//currently only works on "browsable" dictionaries, that is, dictionaries with tpis and string ref data
bool RefSystem_FindReferentAndCountHandles(DictionaryHandleOrName dictHandle, char *pRefData, void **ppStruct, int *piNumHandles);


/********************************* Dictionary Operations *********************************/

// Global operations on an entire dictionary

// Removes all referents from the dictionary, bCompletelyRemoveHandlesToMe is passed to RefSystem_RemoveReferent
void RefSystem_ClearDictionary(DictionaryHandleOrName dictHandle, bool bCompletelyRemoveHandlesToMe);

// Pass in a destructor to free the referents themselves
// DO NOT call this on a dictionary that is loaded using ParserLoadFilesToDictionary
void RefSystem_ClearDictionaryEx(DictionaryHandleOrName dictHandle, bool bCompletelyRemoveHandlesToMe, ReferentDestructor destructor);

// given a dictionary name, returns true if that dictionary exists, false otherwise
bool RefSystem_DoesDictionaryExist(DictionaryHandleOrName dictHandle);

// checks if a dictionary has string reference data
bool RefSystem_DoesDictionaryHaveStringRefData(DictionaryHandleOrName dictHandle);

// given a dictionary name, returns a handle to that dictionary, which makes execution of most
// reference system functions faster.
DictionaryHandle RefSystem_GetDictionaryHandleFromNameOrHandle(DictionaryHandleOrName pDictionaryName);

// given a dictionary handle (or name), return it as a name
const char *RefSystem_GetDictionaryNameFromNameOrHandle(DictionaryHandleOrName pDictionaryName);
const char *RefSystem_GetDictionaryNameFromHandle(DictionaryHandleOrName dictHandle);

// If the dictionary has a deprecated name, get it. Returns NULL if it doesn't have one
const char *RefSystem_GetDictionaryDeprecatedName(DictionaryHandleOrName pDictionaryName);

// Given a handle or name, return the text parser data associated with this object
ParseTable *RefSystem_GetDictionaryParseTable(DictionaryHandleOrName dictHandle);

// Given a handle/name, get the referent root size
int RefSystem_GetDictionaryReferentSize(DictionaryHandleOrName dictHandle);

// Returns number of referred to things in dictionary, if they exist or not
U32 RefSystem_GetDictionaryNumberOfReferentInfos(DictionaryHandleOrName dictHandle);

// Returns number of actually existing referents
U32 RefSystem_GetDictionaryNumberOfReferents(DictionaryHandleOrName dictHandle);

// Returns the bool bIgnoreNullReferences
bool RefSystem_GetDictionaryIgnoreNullReferences(DictionaryHandleOrName pDictionaryName);
void RefSystem_SetDictionaryIgnoreNullReferences(DictionaryHandleOrName pDictionaryName, bool b);

// for the resource system to set a back pointer on the reference dictionary it uses (avoids lots of hashtable lookups later)
void RefSystem_SetResourceDictOnRefDict(const char *refdict_name,ResourceDictionary *pResourceDict);
ResourceDictionary *RefSystem_GetResourceDictFromNameOrHandle(DictionaryHandleOrName dictHandle);

//Tells the reference system that all the referents that will be in a particular self-defining dictionary are already there,
//and no more will ever be added, nor will they be moved or renamed or anything (but handles will still be added/removed).
//
//This should be turned on with care, presumably only for certain server-side dictionaries in production mode. It
//potentially saves a LARGE chunk of memory because suddenly the handles no longer need to be smart pointers.
void RefSystem_LockDictionaryReferents(DictionaryHandleOrName pDictionaryName);



// The collection of callbacks that a reference dictionary needs to provide:

// should return REFERENT_INVALID if the reference is poorly formed, NULL if it is not pointing
// to anything
typedef Referent RefCallBack_DirectlyDecodeReference(ConstReferenceData pRefData);

//must be the same as ExternalHashFunction from stashtable.h
typedef int RefCallBack_GetHashFromReferenceData(ConstReferenceData pRefData, int hashseed);

//must be the same as ExternalCompareFunction from stashtable.h
typedef int RefCallBack_CompareReferenceData(ConstReferenceData pRefData1, ConstReferenceData pRefData2);


// This function converts a reference data to a string, and returns a string (probably already in string pool) which is added to string pool
typedef const char *RefCallBack_ReferenceDataToString(ConstReferenceData pRefData);

// This function converts a string to a reference and returns the reference data, which can 
// be freed by calling RefCallBack_FreeReferenceData. Returns NULL if the string is badly formatted
typedef ReferenceData RefCallBack_StringToReferenceData(const char *pString);

// given a ReferenceData pointer is owned by someone else, makes a copy of it that can be owned
// by the reference system.
typedef ReferenceData RefCallBack_CopyReferenceData(ConstReferenceData pRefData);

// Frees the ReferenceData that was created by RefCallBack_CopyReferenceData or RefCallBack_StringToReferenceData
typedef void RefCallBack_FreeReferenceData(ReferenceData pRefData);

// when registering a dictionary, you can optionally pass in the ParseTable that the elements of that
// dictionary use. This is only required by the ref system for dictionaries that automatically sync
// client and server

// Registers a reference dictionary, given a name and all the necessary callback functions
DictionaryHandle RefSystem_RegisterDictionary_dbg(const char *pName,
	RefCallBack_DirectlyDecodeReference *pDecodeCB,
	RefCallBack_GetHashFromReferenceData *pGetHashCB,
	RefCallBack_CompareReferenceData *pCompareCB,
	RefCallBack_ReferenceDataToString *pRefDataToStringCB,
	RefCallBack_StringToReferenceData *pStringToRefDataCB,
	RefCallBack_CopyReferenceData *pCopyReferenceCB,
	RefCallBack_FreeReferenceData *pFreeReferenceCB,
	ParseTable *pParseTable, bool bRegisterParseTableName
	MEM_DBG_PARMS);

// Registers a reference dictionary which uses strings as reference data. This is how most reference dictionaries are,
// and is faster and more efficient than using opaque data as reference data.
DictionaryHandle RefSystem_RegisterDictionaryWithIntRefData_dbg(const char *pName,
	 RefCallBack_DirectlyDecodeReference *pDecodeCB,
	 ParseTable *pParseTable, bool bRegisterParseTableName
	 MEM_DBG_PARMS);
#define RefSystem_RegisterDictionaryWithIntRefData(pName, pDecodeCB, pParseTable, bRegisterParseTableName)\
	RefSystem_RegisterDictionaryWithIntRefData_dbg(pName, pDecodeCB, pParseTable, bRegisterParseTableName MEM_DBG_PARMS_INIT)

// Registers a reference dictionary which uses strings as reference data. This is how most reference dictionaries are,
// and is faster and more efficient than using opaque data as reference data.
DictionaryHandle RefSystem_RegisterDictionaryWithStringRefData_dbg(const char *pName,
	 RefCallBack_DirectlyDecodeReference *pDecodeCB,
	 bool bCaseSensitive, ParseTable *pParseTable, bool bRegisterParseTableName
	 MEM_DBG_PARMS);
#define RefSystem_RegisterDictionaryWithStringRefData(pName, pDecodeCB, bCaseSensitive, pParseTable, bRegisterParseTableName)\
	RefSystem_RegisterDictionaryWithStringRefData_dbg(pName, pDecodeCB, bCaseSensitive, pParseTable, bRegisterParseTableName MEM_DBG_PARMS_INIT)

// Registers a "self-defining" dictionary. See the wiki page for more info.
// If you set bRegisterParseTableName, it will also set up an alias for the parse table's name
// If you set bMaintainEArray to true, it will maintain an earray for you, useful for editing
// If you set pDeprecatedName, it will load data using the deprecated name in addition to the dictionary name
DictionaryHandle RefSystem_RegisterSelfDefiningDictionary_dbg(const char *pName, bool bCaseSensitive, ParseTable *pParseTable, 
	bool bRegisterParseTableName, bool bMaintainEArray, const char *pDeprecatedName
	MEM_DBG_PARMS);
#define RefSystem_RegisterSelfDefiningDictionary(pName, bCaseSensitive, pParseTable, bRegisterParseTableName, bMaintainEArray, pDeprecatedName) RefSystem_RegisterSelfDefiningDictionary_dbg(pName, bCaseSensitive, pParseTable, bRegisterParseTableName, bMaintainEArray, pDeprecatedName MEM_DBG_PARMS_INIT)

/********************************* Reference Dictionary Iterators *********************************/

// A reference dictionary iterator, as you might think, allows you to iterate through all the references in a
// dictionary. You can iterate by referent, by reference data, or by reference-datas-with-no-referents. You can
// theoretically mix and match those methods but it will do nothing useful.

typedef struct RefDictIterator
{
	StashTableIterator stashIterator;
} RefDictIterator;

void RefSystem_InitRefDictIterator(DictionaryHandleOrName dictHandle, RefDictIterator *pIterator);
Referent RefSystem_GetNextReferentFromIterator(RefDictIterator *pIterator);
void RefSystem_GetNextReferentAndRefDataFromIterator(RefDictIterator *pIterator, Referent *pOutReferent, ReferenceData *pOutReferenceData);
ReferenceData RefSystem_GetNextReferenceDataFromIterator(RefDictIterator *pIterator);
ReferenceData RefSystem_GetNextReferenceDataWithNULLReferentFromIterator(RefDictIterator *pIterator);

// Iterate a reference dictionary
#define FOR_EACH_IN_REFDICT(hDict, typ, p) { RefDictIterator i##p##Iter; typ *p;	RefSystem_InitRefDictIterator(hDict, &i##p##Iter); while ((p = RefSystem_GetNextReferentFromIterator(&i##p##Iter)))	{
#define FOR_EACH_END } } 

/********************************* Misc *********************************/

//loads the default starting sizes for ref system stash tables from data files. Should be done
//early in startup
void RefSystem_LoadStashTableSizesFromDataFiles(void);


// Simple dictionaries

// This macro adds a "simple pointer reference", that is, an essentially nameless reference, from a handle to a pointer. This is useful
// when you want to have pointers which automatically get NULLed when the things the point to go away, without needing to be able to move things or
// have handles that are NULL-but-still-active. It saves you having to construct an entire reference dictionary to deal with super-simple references
#define ADD_SIMPLE_POINTER_REFERENCE(handleName, pReferent) SET_HANDLE_FROM_REFDATA("nullDictionary", (pReferent), handleName)

//this registers another dictionary that is identical to "nullDictionary". Do this if you want to add user callbacks to nullDictionary
DictionaryHandle RefSystem_RegisterNullDictionary_dbg(const char *pName MEM_DBG_PARMS);
#define RefSystem_RegisterNullDictionary(pName) RefSystem_RegisterNullDictionary_dbg(pName MEM_DBG_PARMS_INIT)

// Testing

// checks that all links in the ref dictionary, and between handles and stash tables and so forth,
// are all correct
void RefSystem_CheckIntegrity();

// Used by reference test harness
typedef struct RTHObject RTHObject;
void RTH_Test(void);

// set to 1 or 0 to turn on or off testharness code
#define REFSYSTEM_TEST_HARNESS 0

// set to 1 for the test harness to use strings as references, 0 for opaque data
#define TEST_HARNESS_STRINGVERSION 1

// Object paths

//  Does a root path lookup, for the object path system
int RefSystem_RootPathLookup(const char *name, const char *key, ParseTable** table, void** structptr, int* column);

// how many "browsable" dictionaries there are. For the code which browses ref dictionaries from the MCP
int RefSystem_GetNumDictionariesForBrowsing(void);

// returns the dictionary name of the nth browsable dictionary
const char *RefSystem_GetNthDictionaryForBrowsingName(int i);

bool RefSystem_DictionaryCanBeBrowsed(DictionaryHandleOrName dictHandle);

// handy function to check if there's at least one handle pointing to a NULL referent in a given
// dictionary
bool RefSystem_DoesDictHaveAnyNULLHandles(DictionaryHandleOrName dictHandle);

// KLUDGE ALERT! Setting this to true tells the reference system that you may have nulled out some 
// handles with a memset before removing them. This is useful during one weird interaction between
// shared memory and reloading. DO NOT CALL THIS UNLESS YOU ARE SUPER-CERTAIN YOU HAVE A GOOD
// REASON TO!
void SetRefSystemAllowsNulledActiveHandles(bool bAllow);

// Hides the warning about setting a reference to unknown dictionaries - useful in tools which only read partial sets of data
void SetRefSystemSuppressUnknownDicitonaryWarning_All(bool bSuppress);

//like the above, takes a comma-seperated list of names
void SetRefSystemSuppressUnknownDicitonaryWarning_CertainDictionaries(char *pDictNames);

//used for helpful error-reporting... given a dictionary (with string handles) and a 
//refData, returns the n names of non-NULL referents that are most similar to the 
//input handle (using levenshtein distance)
void RefSystem_GetSimilarNames_dbg(DictionaryHandleOrName dictHandle,
	const char *pInName, int iNumToReturn, const char ***pppOutList
	MEM_DBG_PARMS);
#define RefSystem_GetSimilarNames(dictHandle, pInName, iNumToReturn, pppOutList) RefSystem_GetSimilarNames_dbg(dictHandle, pInName, iNumToReturn, pppOutList MEM_DBG_PARMS_INIT)

// Reload a file, log and print a message about it. Useful for FolderCache callbacks.
void RefSystem_ReloadFile(const char *pchWhere, S32 iWhen, const char *pchWhat, DictionaryHandle pWhich);

//for all major stash tables, resize them down to a reasonable size (ie, we're done loading things, if one of our stash tables is way larger than necessary, shrink it back down)
void RefSystem_ResizeAllStashTables(void);

// MACRO SYNONYMS THAT ARE NAMED CONSISTENTLY:
#define REF_HANDLE_SET_FROM_REFDATA(dictNameOrHandle, pRefData, handleName) SET_HANDLE_FROM_REFDATA(dictNameOrHandle, pRefData, handleName)
#define REF_HANDLE_SET_FROM_INT(dictNameOrHandle, iRefData, handleName) SET_HANDLE_FROM_INT(dictNameOrHandle, iRefData, handleName)
#define REF_HANDLE_SET_FROM_REFERENT(dictNameOrHandle, pReferent, handleName) SET_HANDLE_FROM_REFERENT(dictNameOrHandle, pReferent, handleName)
#define REF_HANDLE_SET_FROM_STRING(dictNameOrHandle, string, handleName) SET_HANDLE_FROM_STRING(dictNameOrHandle, string, handleName)

#define REF_HANDLE_COPY(dstHandle, srcHandle) COPY_HANDLE(dstHandle, srcHandle)
#define REF_HANDLE_MOVE(newHandle, oldHandle) MOVE_HANDLE(newHandle, oldHandle)
#define REF_HANDLE_REMOVE(handle) REMOVE_HANDLE(handle)
#define REF_HANDLE_IS_ACTIVE(handle) IS_HANDLE_ACTIVE(handle)

#define REF_HANDLE_GET_REF_DATA(handle) REF_DATA_FROM_HANDLE(handle)
#define REF_HANDLE_GET_INT(handle) REF_INT_FROM_HANDLE(handle)
#define REF_HANDLE_GET_STRING(handle) REF_STRING_FROM_HANDLE(handle)
#define REF_HANDLE_GET_STRING_KNOWN_NONNULL(handle) REF_STRING_FROM_HANDLE_KNOWN_NONNULL(handle)
#define REF_HANDLE_GET_DICTNAME(handle) REF_DICTNAME_FROM_HANDLE(handle)
#define REF_HANDLE_COMPARE(handleA, handleB) REF_COMPARE_HANDLES(handleA, handleB)
#define REF_HANDLE_GET_PARSETABLE(handle) REF_PARSETABLE_FROM_HANDLE(handle)

#define REF_HANDLE_ADD_SIMPLE_POINTER_REF(handle, pReferent) ADD_SIMPLE_POINTER_REFERENCE(handle, pReferent)

C_DECLARATIONS_END

#endif
