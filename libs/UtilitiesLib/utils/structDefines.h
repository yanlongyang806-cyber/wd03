#ifndef STRUCTDEFINES_H
#define STRUCTDEFINES_H
#pragma once
GCC_SYSTEM

typedef struct Message Message;
typedef struct StashTableImp*			StashTable;
typedef enum Language Language;

//////////////////////////////////////////////////////// Define Contexts are used
//for dynamic extention of AUTO_ENUMS, ie through AEN_EXTEND_WITH_DYNLIST.

typedef struct DefineContext DefineContext;

//defineLists are a mostly obsolete way to get information into a Define Context
typedef struct DefineList {
	const char* key;
	const char* value;
} DefineList;
typedef struct DefineIntList {
	const char* key;
	int value;
} DefineIntList;

DefineContext* DefineCreate(void);
DefineContext* DefineCreateFromList(DefineList defs[]);
DefineContext* DefineCreateFromIntList(DefineIntList defs[]);

void DefineDestroyByHandle(DefineContext **context);
#define DefineDestroy(context) DefineDestroyByHandle(&context)

//adding by handle is key, because it properly updates the optimized StaticDefines.
void DefineAddByHandle(DefineContext **context, const char *key, const char *value, bool EnumOverlapsOK);
#define DefineAdd(context, key, value) DefineAddByHandle(&context, key, value, false)
#define DefineAdd_EnumOverlapsOK(context, key, value) DefineAddByHandle(&context, key, value, true)

void DefineAddIntByHandle(DefineContext **context, const char* key, S32 uiValue, bool bEnumOverlapsOK);
#define DefineAddInt(context, key, uiValue) DefineAddIntByHandle(&context, key, uiValue, false)
#define DefineAddInt_EnumOverlapOK(context, key, uiValue) DefineAddIntByHandle(&context, key, uiValue, true)


// Loads a data-defined enum from a file
// Returns the maximum value added (iStartingValue + number of things loaded)
int DefineHandle_LoadFromFile(DefineContext **context, const char *pchKeyName, const char *pchDescription, const char *pchDir, const char *pchFilename, const char *pchBinName, int iStartingValue);
#define DefineLoadFromFile(context, pchKeyName, pchDescription, pchDir, pchFilename, pchBinName, iStartingValue) DefineHandle_LoadFromFile(&context, pchKeyName, pchDescription, pchDir, pchFilename, pchBinName, iStartingValue)



///////////////////////////////////////////////////// StaticDefine
// New defines system for per-token defines.  Only uses a static definition.
// Lists with different types may be used interchangeably, but
// must be declared with DEFINE_Xxx type macros, as in:
//
//	StaticDefineInt mydefines = {
//		DEFINE_INT
//		{ "fFlag",	1 },
//		...
//		DEFINE_END
//	};

// private stuff..
#define DM_END 0
#define DM_INT 1
#define DM_STRING 2
#define DM_DYNLIST 3
#define DM_NOTYPE 4
#define DM_TAILLIST 5 

//If you're going to add to the DM int defines, move this max define so we can account for it.
#define DM_DEFINE_MAX 7

// use these to start and end a list
#define DEFINE_STRING	{ U32_TO_PTR(DM_STRING), 0 },
#define DEFINE_INT		{ U32_TO_PTR(DM_INT), 0 },
#define DEFINE_END		{ U32_TO_PTR(DM_END), 0 },

// use this to embed a dynamic define list in a StaticDefine or StaticDefineInt
#define DEFINE_EMBEDDYNAMIC(pDefineContext) { U32_TO_PTR(DM_DYNLIST), (const char*)(&pDefineContext) }, 
#define DEFINE_EMBEDDYNAMIC_INT(pDefineContext) { U32_TO_PTR(DM_DYNLIST), (intptr_t)(&pDefineContext) }, 

//These are the HOT NEW FAST ways to do lookups, this is what you should almost always be calling:
//Note that they come in identicaly StaticDefine_ and StaticDefineInt_ versions just so you don't have to waste
//time doing unnecessary casting
int StaticDefine_FastStringToInt(StaticDefine *pDefine, const char *pStr, int iDefault);
const char *StaticDefine_FastIntToString(StaticDefine *pDefine, int iInt);
static __forceinline int StaticDefineInt_FastStringToInt(StaticDefineInt *pDefine, const char *pStr, int iDefault) { return StaticDefine_FastStringToInt((StaticDefine*)pDefine, pStr, iDefault); }
static __forceinline const char *StaticDefineInt_FastIntToString(StaticDefineInt *pDefine, int iInt) { return StaticDefine_FastIntToString((StaticDefine*)pDefine, iInt);}

//Some old function names redirected to the fast new lookup types
#define StaticDefineIntRevLookup StaticDefineInt_FastIntToString
#define StaticDefineIntGetIntDefault StaticDefineInt_FastStringToInt
__forceinline static int StaticDefineIntGetInt(StaticDefineInt* list, const char* key)
{
	return StaticDefineInt_FastStringToInt(list, key, -1);
}

__forceinline static const char * StaticDefineIntRevLookupNonNull(StaticDefineInt* list, int value)
{
	const char *pResult = StaticDefineInt_FastIntToString(list, value);
	return NULL_TO_EMPTY(pResult);
}

//a very useful function which gets all the name value pairs out of a StaticDefineInt (including all the ones in define contexts, etc).
//This is NOT very fast, and is not intended to be used in performance-intensive code
void DefineFillAllKeysAndValues(StaticDefineInt *enumTable, const char ***keys, S32 **values);

// Fills retVals with values contained in keyList. Returns the number of values parsed.
int StaticDefineIntGetIntsFromKeyListEx(StaticDefineInt* list, const char* keyList, const char* keyListDelims, int** retVals, char** failEString);
#define StaticDefineIntGetIntsFromKeyList(list, keyList, retVals, failEString) StaticDefineIntGetIntsFromKeyListEx(list, keyList, " ,\t\r\n", retVals, failEString) 

//retursn the min and max INT in a particular StaticDefineInt, again, NOT performance-intensive
void DefineGetMinAndMaxInt(StaticDefineInt *enumTable, int *piOutMin, int *piOutMax);

//global names-to-StaticDefine and back
StaticDefineInt *FindNamedStaticDefine(const char *pName);
const char *FindStaticDefineName(StaticDefineInt *list);

//retursn true if either pDefine1 is a prefix or pDefine2 or vice versa (not necessarily because
//one is DM_TAILLISTed onto the end of the other, but just because all the names/values are identical
bool OneStaticDefineIsPrefixOfOther(StaticDefineInt *pDefine1, StaticDefineInt *pDefine2);

//turn on optimization for a StaticDefine, useful for hand-built ones (not needed if you call RegisterNamedStaticDefine)
void StaticDefine_PossiblyAddFastIntLookupCache(StaticDefine *pDefine);
static __forceinline void StaticDefineInt_PossiblyAddFastIntLookupCache(StaticDefineInt *pDefine) { StaticDefine_PossiblyAddFastIntLookupCache((StaticDefine*)pDefine); }

//if you want to basically do what AEN_EXTEND_WITH_DYNLIST does, but do it at runtime, presumably to avoid crazy linking issues,
//first put AEN_PAD on your StaticDefine (which sticks extra padding into it so that it can have stuff added to it), then call this:
void StaticDefineInt_ExtendWithDefineContext(StaticDefineInt *pStaticDefine, DefineContext **pDefineContext);

//use these before and after reloading a define context staticefienthat a StaticDefine uses
void StaticDefine_ClearFastIntLookupCache(StaticDefine *pDefine);
void StaticDefine_ReEnableFastIntLookupCache(StaticDefine *pDefine);
static __forceinline void StaticDefineInt_ClearFastIntLookupCache(StaticDefineInt *pDefine) { StaticDefine_ClearFastIntLookupCache((StaticDefine*)pDefine); }
static __forceinline void StaticDefineInt_ReEnableFastIntLookupCache(StaticDefineInt *pDefine) { StaticDefine_ReEnableFastIntLookupCache((StaticDefine*)pDefine); }


//This will map a name to a list and that list to the name... used for when you create hand-built StaticDefines and want them
//globally registered and named the way AUTO_ENUMs are
void RegisterNamedStaticDefine(StaticDefineInt *list, const char *pName);
//This will map a list to a name. (for define lists hanging off ParseTables loaded from Schemas)
void RegisterNamedDefineForSchema(StaticDefineInt *list, const char *pName);

//used internally by TextParser while calculating parse table CRCs
void UpdateCrcFromDefineList(DefineContext* defines);

//reports the memory usage of the fast lookup cache for a StaticDefine
size_t StaticDefine_GetFastIntLookupCacheMemoryUsage(StaticDefine *pDefine);

/*pass in one or two StaticDefineInts. This assumes that both are defined as flags, ie
FOO_A = 1 << 1,
FOO_B = 1 << 2

etc. Verifies that all values are powers of 2, and that no two values overlap between the two.

Only exception is that a field named _LASTPLUSONE can be one greater than a power of two, so you can do this:
AUTO_ENUM;
typedef enum fooEnum
{
	FOO_A = 1 << 1,
	FOO_B = 1 << 2,

	FOO_LASTPLUSONE
} fooEnum;

AUTO_ENUM;
typedef enum barEnum
{
	BAR_A = (FOO_LASTPLUSONE - 1) << 1,
	BAR_B = (FOO_LASTPLUSONE - 1) << 2,
} barEnum;
*/
void VerifyFlagEnums(StaticDefineInt *pDefine1, StaticDefineInt *pDefine2);

//the global stashtable of named StaticDefines
extern StashTable sStaticDefinesByName;

//////////////////////////////// stuff relating to Messages and StaticDefines

// Returns NULL if the verification succeeded, or the missing key if one was found.
SA_ORET_OP_STR const char *StaticDefineVerifyMessages(StaticDefineInt *pDefine);
SA_ORET_OP_VALID Message *StaticDefineGetMessage(StaticDefineInt *pDefine, S32 iValue);
SA_ORET_OP_STR const char *StaticDefineGetTranslatedMessage(StaticDefineInt *pDefine, S32 iValue);
SA_ORET_OP_STR const char *StaticDefineLangGetTranslatedMessage(Language lang, StaticDefineInt *pDefine, S32 iValue);

// These are the basic OLD AND SLOW ways to go key-to-value or value-to-key, if both key and value are strings,
//still occasionally used
const char* StaticDefineLookup(StaticDefine* list, const char* key);
const char* StaticDefineRevLookup(StaticDefine* list, const char* value);


//rarely used stuff for DefineContexts
void DefineAddList(DefineContext* lhs, DefineList defs[]);
void DefineAddIntList(DefineContext* lhs, DefineIntList defs[]);
const char* DefineLookup(DefineContext* context, const char* key);
void DefineSetHigherLevel(DefineContext* lower, DefineContext* higher); // lookups on lower will default to higher if not found
void DefineGetKeysAndVals(DefineContext *context, char ***eakeys, char ***eavals); // Fills earrays with keys and values

//do these before and after reloading a define context
void DefineClearAllListsAndReset(DefineContext **context);
void DefineReCacheAllLists(DefineContext **context);

//obsolete old way to define a StaticDefine directly from a define context
#define STATIC_DEFINE_WRAPPER(StaticDefineName, pDefineContext) \
	StaticDefine StaticDefineName[] = { \
		DEFINE_EMBEDDYNAMIC(pDefineContext) \
		DEFINE_END \
	}

//obsolete way to append one StaticDefine to another
void StaticDefineIntAddTailList(StaticDefineInt *parent, StaticDefineInt *list);

// note, defines that are not found will silently be ignored and not added to the list.
void StaticDefineIntParseStringForInts(	SA_PARAM_NN_VALID StaticDefineInt* list, 
										SA_PARAM_NN_VALID const char *pchStr, 
										SA_PARAM_NN_VALID S32 **piTagsOut, 
										SA_PARAM_OP_STR const char *pToks);


#endif	// STRUCTDEFINES_H
