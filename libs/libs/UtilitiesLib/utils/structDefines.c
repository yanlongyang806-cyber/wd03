#include "structDefines.h"
#include "crypt.h"
#include "stashtable.h"
#include "earray.h"
#include "error.h"
#include "wininclude.h"
#include "StringCache.h"
#include "Message.h"
#include "file.h"
#include "strings_opt.h"
#include "GlobalTypes.h"
#include "mathutil.h"
#include "fastAtoi.h"
#include "StringUtil.h"

#undef DefineAddInternal

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems);); // Until this trickles correctly


//for our fast int lookup caches, we need to know, for each call to DefineAddInt, which staticDefines that have 
//fast int lookup caches reference it. So we need a stashtable containing lists of StaticDefines

typedef struct ListOfStaticDefines
{
	StaticDefine **ppDefines;
} ListOfStaticDefines;
static StashTable sListsOfStaticDefinesByDefineContextHandle = NULL;

// *********************************************************************************
//  Dynamic defines
// *********************************************************************************

// need this because free() is redefined in memcheck
static void Free(void* p) { free(p); }

static void StaticDefine_ContextIntAdded(DefineContext **ppContext, const char *pKey, int uiValue, bool bOverlapOK);
void DefineAddInternal(DefineContext* context, const char* key, const char* value);
static void RegisterDefineContextForStaticDefine(DefineContext **ppDefineContextHandle, StaticDefine *pStaticDefine);

struct DefineContext
{
	StashTable hash;
    DefineList *elts;
    int n_elts;
    int c_elts;
	struct DefineContext* higherlevel;
};

DefineContext* DefineCreate()
{
	DefineContext* result = malloc(sizeof(DefineContext));
	memset(result, 0, sizeof(DefineContext));
	result->hash = stashTableCreateWithStringKeys(100, StashDefault);
	return result;
}

DefineContext* DefineCreateFromList(DefineList defs[])
{
	DefineContext* result = DefineCreate();
	DefineAddList(result, defs);
	return result;
}

DefineContext* DefineCreateFromIntList(DefineIntList defs[])
{
	DefineContext* result = DefineCreate();
	DefineAddIntList(result, defs);
	return result;
}

void DefineAddList(DefineContext* lhs, DefineList defs[])
{
	DefineList* cur = defs;
	while (cur->key && cur->key[0])
	{
		DefineAddInternal(lhs, cur->key, cur->value);
		cur++;
	}
}

void DefineAddIntList(DefineContext* lhs, DefineIntList defs[])
{
	DefineIntList* cur = defs;
	while (cur->key && cur->key[0])
	{
		char buffer[20];
		sprintf(buffer, "%i", cur->value);
		DefineAddInternal(lhs, cur->key, buffer);
		cur++;
	}
}

void DefineDestroyByHandle(DefineContext** ppContext)
{
    int i;
	ListOfStaticDefines *pList = NULL;
	DefineContext *context = *ppContext;

	if (stashFindPointer(sListsOfStaticDefinesByDefineContextHandle, ppContext, &pList))
	{
		Parser_InvalidateParseTableCRCs();

		for (i = 0; i < eaSize(&pList->ppDefines); i++)
		{
			StaticDefine *pDefine = pList->ppDefines[i];

			StaticDefine_ClearFastIntLookupCache(pDefine);
		}
	}

    for(i = 0; i<context->n_elts; ++i)
    {
        DefineList *l = context->elts + i;
        if(l->key) 
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*l->key'"
            free((char*)l->key);
        if(l->value) 
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**l[4]'"
            free((char*)l->value);
    }
 	stashTableDestroy(context->hash);
	free(context->elts);
	free(context);
	*ppContext = NULL;

	if (pList)
	{
		Parser_InvalidateParseTableCRCs();

		for (i = 0; i < eaSize(&pList->ppDefines); i++)
		{
			StaticDefine *pDefine = pList->ppDefines[i];
			StaticDefine_ReEnableFastIntLookupCache(pDefine);
		}
	}
}

void DefineAddInternal(DefineContext* context, const char* key, const char* value) // strings copied
{
    DefineList *elt = dynArrayAddStruct(context->elts,context->n_elts,context->c_elts);
    elt->key   = _strdup(key);   
    elt->value = _strdup(value);
	stashAddPointer(context->hash, elt->key, elt->value, true);
}

void DefineAdd_AlreadyStrDuped(DefineContext* context, const char* key, const char* value) // strings copied
{
    DefineList *elt = dynArrayAddStruct(context->elts,context->n_elts,context->c_elts);
    elt->key   = key;   
    elt->value = value;
	stashAddPointer(context->hash, elt->key, elt->value, true);
}

void DefineAddByHandle(DefineContext** context, const char* key, const char* value, bool bEnumOverlapOK)
{
	//check if value is a string-ified integer, if it is, then call DefineAddInt instead so that 
	//AUTO_ENUMs get updated
	int iVal;

	if (StringToInt_Paranoid(value, &iVal))
	{
		DefineAddIntByHandle(context, key, iVal, bEnumOverlapOK);
	}
	else
	{
		DefineAddInternal(*context, key, value);
	}
}

void DefineAddIntByHandle(DefineContext**context, const char* key, S32 uiValue, bool bEnumOverlapOK)
{
	if (context && *context)
	{
		char buf[20];
		const char *pStrDuppedKey = _strdup(key);

		StaticDefine_ContextIntAdded(context, pStrDuppedKey, uiValue, bEnumOverlapOK);

		sprintf(buf, "%u", uiValue);
		DefineAdd_AlreadyStrDuped(*context, pStrDuppedKey, _strdup(buf));
	}
}

void DefineSetHigherLevel(DefineContext* lower, DefineContext* higher)
{
	lower->higherlevel = higher;
}

static __forceinline DefineContext* DefineGetHigherLevel(DefineContext* context)
{
	return context->higherlevel;
}

void DefineGetKeysAndVals(DefineContext *context, char ***eakeys, char ***eavals) // Fills earrays with keys and values
{
	StashElement currElement = NULL;
	DefineContext* curcontext = context;
    int i;
    for(i = 0; i<curcontext->n_elts; ++i)
    {
        DefineList *l = &curcontext->elts[i];
        eaPush(eakeys, (char*)l->key);
        eaPush(eavals, (char*)l->value);
    }
}

// uses a hash table now
const char* DefineLookup(DefineContext* context, const char* key)
{
	DefineContext* curcontext = context;

	while (curcontext)
	{
		const char*str = stashFindPointerReturnPointer(curcontext->hash, key);
		if (str) 
            return str;
		curcontext = curcontext->higherlevel;
	}
	return NULL;
}

const char* DefineRevLookup(DefineContext* context, const char* value)
{
	DefineContext* curcontext = context;

	while (curcontext)
	{
        int i;
        for(i = 0; i<context->n_elts; ++i)
        {
            DefineList *l = context->elts + i;
			if (0==stricmp(value, l->value))
				return l->key;
		}
		curcontext = curcontext->higherlevel;
	}
	return NULL;
}


// Used for loading Define data from a file
typedef struct DefineKeys{
	const char **ppchNames;
	const char *pchFilename;
} DefineKeys;

// This gets modified at run time
ParseTable parse_DefineKeys[] =
{
	{ "DefineKeys", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(DefineKeys), 0, NULL, 0, NULL },
	{ "{",				TOK_START, 0 },
	{ NULL,				TOK_STRINGARRAY(DefineKeys, ppchNames), NULL },
	{ "Filename",		TOK_POOL_STRING | TOK_CURRENTFILE(DefineKeys, pchFilename), NULL},
	{ "}",				TOK_END, 0 },
	{ "", 0, 0 }
};
#define TYPE_parse_DefineKeys DefineKeys

AUTO_RUN;
void registerDefineKeys(void)
{
	ParserSetTableInfo(parse_DefineKeys,sizeof(DefineKeys),"DefineKeys",NULL,__FILE__, 0);
}

static void SetUpDefineKeysParse(const char *pchName)
{
	assertmsg(!parse_DefineKeys[2].name,"DefineLoadFromFile can't be called recursively or from multiple threads!");
	parse_DefineKeys[2].name = pchName;
}

int DefineHandle_LoadFromFile(DefineContext **context, const char *pchKeyName, const char *pchDescription, const char *pchDir, const char *pchFilename, const char *pchBinName, int iStartingValue)
{
	static DefineKeys names = {0};
	int i, s;

	loadstart_printf("Loading %s...", pchDescription);

	SetUpDefineKeysParse(pchKeyName);

	ParserLoadFiles(pchDir,pchFilename,pchBinName,PARSER_OPTIONALFLAG,parse_DefineKeys,&names);
	s = eaSize(&names.ppchNames);
	for(i=0;i<s;i++){
		if (!DefineLookup(*context, names.ppchNames[i]))
			DefineAddIntByHandle(context,names.ppchNames[i],i+iStartingValue, false);
		else
			ErrorFilenamef(names.pchFilename, "Duplicate %s %s %s", pchDescription, pchKeyName, names.ppchNames[i]);
	}
	StructDeInit(parse_DefineKeys, &names);
	parse_DefineKeys[2].name = NULL;

	loadend_printf(" done (%d %s).", s, pchDescription);
	return i + iStartingValue;
}

// *********************************************************************************
//  static defines
// *********************************************************************************

#define STATIC_DEFINE_SCRATCHPAD_SIZE 200

const char* StaticDefineLookup(StaticDefine* list, const char* key)
{
	char *pScratchPad; // may be needed to hold result value
	int curtype = DM_NOTYPE;
	StaticDefine* cur = list;
	STATIC_THREAD_ALLOC_SIZE(pScratchPad, STATIC_DEFINE_SCRATCHPAD_SIZE);

	if (!key) return NULL;


	while (1)
	{
		// look for key markers first
		if (cur->key == U32_TO_PTR(DM_END))
			return NULL; // failed lookup
		else if (cur->key == U32_TO_PTR(DM_INT))
			curtype = DM_INT;
		else if (cur->key == U32_TO_PTR(DM_STRING))
			curtype = DM_STRING;
		else if (cur->key == U32_TO_PTR(DM_DYNLIST))
		{
			// do a lookup in the dynamic list
			DefineContext* context = *(DefineContext**)cur->value;
			const char* result = DefineLookup(context, key);
			if (result) 
				return result;
		}
		else if (cur->key == U32_TO_PTR(DM_TAILLIST))
		{
			return StaticDefineLookup((StaticDefine *)cur->value,key);
		}
		else if (curtype == DM_NOTYPE)
		{
			assertmsg(0, "StaticDefine table found without a type marker at top, (or bad pointer?)");
		}
		else
		{
			// do a real lookup on key
			if (!stricmp(cur->key, key))
			{
				switch (curtype)
				{
				case DM_INT:
					_itoa_s((intptr_t)cur->value, pScratchPad, STATIC_DEFINE_SCRATCHPAD_SIZE, 10);
					return pScratchPad;
				case DM_STRING:
					return cur->value;
				}
				FatalErrorf("StaticDefineLookup: list doesn't have a correct type marker\n");
			}
		}
		// keep looking for keys
		cur++;
	}
}

const char* StaticDefineIntRevLookup_Old(StaticDefineInt* list, int value)
{
	char valuestr[20];
	int curtype = DM_NOTYPE;
	StaticDefineInt* cur = list;

	sprintf(valuestr, "%i", value);
	while (1)
	{
		if (cur->key == U32_TO_PTR(DM_END))
			return NULL; // failed lookup
		else if (cur->key == U32_TO_PTR(DM_INT))
			curtype = DM_INT;
		else if (cur->key == U32_TO_PTR(DM_STRING))
			curtype = DM_STRING;
		else if (cur->key == U32_TO_PTR(DM_DYNLIST))
		{
			// do a lookup in the dynamic list
			DefineContext* context = *(DefineContext**)cur->value;
			const char *result = DefineRevLookup(context, valuestr);
			if(result) return result;
		}
		else if (cur->key == U32_TO_PTR(DM_TAILLIST))
		{
			return StaticDefineIntRevLookup_Old((StaticDefineInt *)cur->value,value);
		}
		else if (curtype == DM_STRING)
		{
			if (stricmp((char*)cur->value, valuestr) == 0)
				return cur->key;
		}
		else if (curtype == DM_INT)
		{
			if (cur->value == value)
				return cur->key;
		}
		cur++;
	}
}

const char* StaticDefineRevLookup(StaticDefine* list, const char* value)
{
	char valuestr[20];
	int curtype = DM_NOTYPE;
	StaticDefine* cur = list;

	while (1)
	{
		if (cur->key == U32_TO_PTR(DM_END))
			return NULL; // failed lookup
		else if (cur->key == U32_TO_PTR(DM_INT))
			curtype = DM_INT;
		else if (cur->key == U32_TO_PTR(DM_STRING))
			curtype = DM_STRING;
		else if (cur->key == U32_TO_PTR(DM_DYNLIST))
		{
			// do a lookup in the dynamic list
			DefineContext* context = *(DefineContext**)cur->value;
			const char *result = DefineRevLookup(context, value);
			if(result) return result;
		}
		else if (cur->key == U32_TO_PTR(DM_TAILLIST))
		{
			return StaticDefineRevLookup((StaticDefine *)cur->value,value);
		}
	
		else if (curtype == DM_STRING)
		{
			if (stricmp(cur->value, value) == 0)
				return cur->key;
		}
		else if (curtype == DM_INT)
		{
			sprintf(valuestr, "%i", (intptr_t)cur->value);
			if (stricmp(valuestr, value) == 0)
				return cur->key;
		}
		
		cur++;
		
	}
}

void StaticDefineIntAddTailList(StaticDefineInt *parent, StaticDefineInt *child)
{
	StaticDefineInt* cur = parent;

	while (1)
	{
		if (cur->key == U32_TO_PTR(DM_TAILLIST) && (StaticDefineInt *)cur->value != child)
		{
			assertmsg(0,"Attempted to add a Tail list to a StaticDefine list that already has one!");
		}
		if (cur->key == U32_TO_PTR(DM_END))
		{
			cur->key = U32_TO_PTR(DM_TAILLIST);
			cur->value = (intptr_t)child;
			return;
		}
		cur++;
	}
}
/*
int UpdateCrcFromDefineElement(StashElement e)
{
	const char* key = stashElementGetStringKey(e);
	char* value = (char*)stashElementGetPointer(e);
	cryptAdler32Update(key, (int)strlen(key));
	cryptAdler32Update(value, (int)strlen(value));
	return 1;
}*/

typedef struct KeyValPair
{
	const char *pKey;
	const char *pVal;
} KeyValPair;


int SortKeyValPairsByKey(const KeyValPair *pPair1, const KeyValPair *pPair2)
{
	return stricmp(pPair1->pKey, pPair2->pKey);
}


#include "ScratchStack.h"
#include "estring.h"

void UpdateCrcFromDefineList(DefineContext* defines)
{
	StashElement currElement = NULL;
	KeyValPair *pKeyValPairs = NULL;
	int i,count;
	static DefineContext* last_defines;
	static int last_count;
	static char *last_data;

	count = defines->n_elts;
	if (last_defines != defines || last_count != count)
	{
		pKeyValPairs = ScratchAlloc(count * sizeof(KeyValPair));
		for (i=0; i < count; ++i) 
		{
            DefineList *l = defines->elts + i;
			pKeyValPairs[i].pKey = l->key;
			pKeyValPairs[i].pVal = l->value;
		}

		qsort(pKeyValPairs,count,sizeof(KeyValPair),SortKeyValPairsByKey);

		estrClear(&last_data);
		for (i=0; i < count; i++)
		{
			estrAppend2(&last_data,pKeyValPairs[i].pKey);
			estrAppend2(&last_data,pKeyValPairs[i].pVal);
		}
		ScratchFree(pKeyValPairs);
		last_count = count;
		last_defines = defines;
	}
	cryptAdler32Update_IgnoreCase(last_data, estrLength(&last_data));
}

// Fills retVals with values contained in keyList. Returns the number of values parsed.
int StaticDefineIntGetIntsFromKeyListEx(StaticDefineInt* list, const char* keyList, const char* keyListDelims, int** retVals, char** failEString)
{
	int numParsed = 0;
	if (list && keyList && keyList[0])
	{
		char* context;
		char* start;
		char* copyList;
		strdup_alloca(copyList, keyList);
		start = strtok_r(copyList, keyListDelims, &context);
		do
		{
			if (start)
			{
				int iRes = StaticDefineInt_FastStringToInt(list, start, INT_MIN);
				if (iRes != INT_MIN)
				{
					if (retVals)
					{
						eaiPush(retVals, iRes);
					}
					numParsed++;
				}
				else if (failEString)
				{
					if (EMPTY_TO_NULL(*failEString))
						estrConcatf(failEString, ", ");
					else
						estrConcatf(failEString, "StaticDefine %s: ", NULL_TO_EMPTY(FindStaticDefineName(list)));
					estrConcatf(failEString,  "Key '%s' not found", start);
				}
			}
		} while (start = strtok_r(NULL, keyListDelims, &context));
	}
	return numParsed;
}

void DefineFillAllKeysAndValues(StaticDefineInt *enumTable, const char ***keys, S32 **values)
{
	S32 i;
	for (i = 1; enumTable[i].key != U32_TO_PTR(DM_END); i++)
	{
		if (enumTable[i].key == U32_TO_PTR(DM_DYNLIST))
		{
			// read the dynamic list, add in order of value
			DefineContext* context = *(DefineContext**)enumTable[i].value;
			char **ppchKeys = NULL;
			char **ppchVals = NULL;

			if (context)
			{

				DefineGetKeysAndVals(context,&ppchKeys,&ppchVals);
				while (eaSize(&ppchKeys))
				{
					int j, minidx = 0, minval = atoi64(ppchVals[0]);
					for (j=1; j<eaSize(&ppchKeys); j++)
					{
						int ival = atoi64(ppchVals[j]);
						if(ival<minval)
						{
							minval = ival;
							minidx = j;
						}
					}
					if (keys)
						eaPush(keys,ppchKeys[minidx]);
					if (values)
						eaiPush(values,minval);
					eaRemoveFast(&ppchKeys,minidx);
					eaRemoveFast(&ppchVals,minidx);
				}
				eaDestroy(&ppchKeys);
				eaDestroy(&ppchVals);
			}
			continue;
		}
		else if (enumTable[i].key == U32_TO_PTR(DM_TAILLIST))
		{
			enumTable = (StaticDefineInt *)enumTable[i].value;
			i = 0;
			continue;
		}
		if (keys)
			eaPush(keys, enumTable[i].key);
		if (values)
			eaiPush(values, enumTable[i].value);
	}
}

void DefineGetMinAndMaxInt(StaticDefineInt *enumTable, int *piOutMin, int *piOutMax)
{
	S32 *pInts = NULL;
	int i;

	DefineFillAllKeysAndValues(enumTable, NULL, &pInts);

	if (ea32Size(&pInts))
	{
		*piOutMin = pInts[0];
		*piOutMax = pInts[0];

		for (i = 1; i < ea32Size(&pInts); i++)
		{
			if (pInts[i] < *piOutMin)
			{
				*piOutMin = pInts[i];
			}

			if (pInts[i] > *piOutMax)
			{
				*piOutMax = pInts[i];
			}
		}
	}
	else
	{
		*piOutMin = *piOutMax = 0;
	}

	ea32Destroy(&pInts);

}



StashTable sStaticDefinesByName = NULL;
static StashTable sStaticDefineNamesByValue = NULL;


void RegisterNamedStaticDefine(StaticDefineInt *list, const char *pName)
{
	if (!sStaticDefinesByName)
	{
		sStaticDefinesByName = stashTableCreateWithStringKeys(32, StashDefault);
	}
	
	if (stashFindPointer(sStaticDefinesByName, pName, NULL))
	{
		assertmsgf(0, "Duplicate enums named %s", pName);
	}

	stashAddPointer(sStaticDefinesByName, pName, list, false);

	RegisterNamedDefineForSchema(list, pName);
	StaticDefineInt_PossiblyAddFastIntLookupCache(list);
}

void RegisterNamedDefineForSchema(StaticDefineInt *list, const char *pName)
{
	if (!sStaticDefineNamesByValue)
	{
		sStaticDefineNamesByValue = stashTableCreateAddress(32);
	}

	if (GetAppGlobalType() == GLOBALTYPE_CLIENT && !g_disallow_static_strings){
		stashAddPointer(sStaticDefineNamesByValue, list, allocAddStaticString(pName), false);
	} else {
		stashAddPointer(sStaticDefineNamesByValue, list, allocAddString(pName), false);
	}
}

StaticDefineInt *FindNamedStaticDefine(const char *pName)
{
	StaticDefineInt *pList = NULL;

	if (sStaticDefinesByName)
	{
		stashFindPointer(sStaticDefinesByName, pName, &pList);
	}

	return pList;
}

const char *FindStaticDefineName(StaticDefineInt *list)
{
	char *pName = NULL;

	if (!list)
	{
		return NULL;
	}

	if (sStaticDefineNamesByValue)
	{
		stashFindPointer(sStaticDefineNamesByValue, list, &pName);
	}

	return pName;
}

// Needing a message for each value in a StaticDefineInt is a common pattern.

Message *StaticDefineGetMessage(StaticDefineInt *pDefine, S32 iValue)
{
	const char *pchDefineName = FindStaticDefineName(pDefine);
	const char *pchKey = StaticDefineIntRevLookup_Old(pDefine, iValue);
	Message *pMessage = NULL;
	if (pchDefineName && pchKey)
	{
		char achMessageKey[2048];
		sprintf(achMessageKey, "StaticDefine_%s_%s", pchDefineName, pchKey);
		pMessage = RefSystem_ReferentFromString(gMessageDict, achMessageKey);
	}
	return pMessage;
}

const char *StaticDefineVerifyMessages(StaticDefineInt *pDefine)
{
	const char *pchDefineName = FindStaticDefineName(pDefine);
	const char **eaKeys = NULL;
	S32 i;
	if (!pchDefineName)
		return "StaticDefine does not have a name.";

	DefineFillAllKeysAndValues(pDefine, &eaKeys, NULL);
	for (i = 0; i < eaSize(&eaKeys); i++)
	{
		const char *pchKey = eaKeys[i];
		char achMessageKey[2048];
		sprintf(achMessageKey, "StaticDefine_%s_%s", pchDefineName, pchKey);
		if (!RefSystem_ReferentFromString(gMessageDict, achMessageKey))
		{
			eaDestroy(&eaKeys);
			return allocAddString(achMessageKey);
		}
	}

	eaDestroy(&eaKeys);
	return NULL;
}

// Dump a .ms file for a static define.
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void StaticDefineDumpMessages(const char *pchName)
{
#ifndef MESSAGE_IS_TINY
	StaticDefineInt *pDefine = FindNamedStaticDefine(pchName);
	if (pDefine)
	{
		char achPath[MAX_PATH];

		FILE *pFile;
		sprintf(achPath, "messages/StaticDefines/%s.ms", pchName);
		fileLocateWrite(achPath, achPath);
		makeDirectoriesForFile(achPath);
		pFile = fopen(achPath, "wb");

		if (pFile)
		{
			const char **eaKeys = NULL;
			char achScope[2048];
			S32 i;

			sprintf(achScope, "StaticDefine/%s", pchName);
			DefineFillAllKeysAndValues(pDefine, &eaKeys, NULL);

			for (i = 0; i < eaSize(&eaKeys); i++)
			{
				const char *pchKey = eaKeys[i];
				char achDescription[2048];
				char achMessageKey[2048];
				char *pch;
				Message m = {0};
				sprintf(achMessageKey, "StaticDefine_%s_%s", pchName, pchKey);
				sprintf(achDescription, "Human-readable name for %s, a kind of %s", pchKey, pchName);
				m.pcDefaultString = (char *)pchKey;
				m.pcDescription = achDescription;
				m.pcScope = achScope;
				m.pcMessageKey = achMessageKey;
				
				pch = NULL;
				ParserWriteText(&pch, parse_Message, &m, 0, 0, 0);
				fprintf(pFile, "Message %s", pch);
				estrDestroy(&pch);
			}

			eaDestroy(&eaKeys);
			fclose(pFile);
		}
		else
		{
#if _PS3
            Errorf("Unable to open file %s: errno=%d.", achPath, errno);
#else
			char achError[1024];
			strerror_s(SAFESTR(achError), errno);
			Errorf("Unable to open file %s: %s.", achPath, achError);
#endif
		}

	}
	else
		Errorf("Unable to find StaticDefine %s.", pchName);
#endif // MESSAGE_IS_TINY
}

const char *StaticDefineLangGetTranslatedMessage(Language lang, StaticDefineInt *pDefine, S32 iValue)
{
	Message *msg = StaticDefineGetMessage(pDefine, iValue);
	char const *res = NULL;
	if(!msg)
		return NULL;
	res = langTranslateMessage(lang, msg); 
	return res;
}

const char *StaticDefineGetTranslatedMessage(StaticDefineInt *pDefine, S32 iValue)
{
	return StaticDefineLangGetTranslatedMessage(locGetLanguage(getCurrentLocale()), pDefine, iValue);
}

bool OneStaticDefineIsPrefixOfOther(StaticDefineInt *pDefine1, StaticDefineInt *pDefine2)
{
	char **ppKeys1 = NULL;
	int *pVals1 = NULL;

	char **ppKeys2 = NULL;
	int *pVals2 = NULL;

	bool bMatch = true;

	int iSize;

	int i;

	DefineFillAllKeysAndValues(pDefine1, &ppKeys1, &pVals1);
	DefineFillAllKeysAndValues(pDefine2, &ppKeys2, &pVals2);

	iSize = MIN(eaSize(&ppKeys1), eaSize(&ppKeys2));

	for (i=0; i < iSize; i++)
	{
		assert(pVals1 && pVals2 && ppKeys1 && ppKeys1[i] && ppKeys2 && ppKeys2[i]);

		if (pVals1[i] != pVals2[i])
		{
			bMatch = false;
			break;
		}

		if (stricmp(ppKeys1[i], ppKeys2[i]) != 0)
		{
			bMatch = false;
			break;
		}
	}

	ea32Destroy(&pVals1);
	ea32Destroy(&pVals2);
	eaDestroy(&ppKeys1);
	eaDestroy(&ppKeys2);


	return bMatch;
}

static void VerifyPowersOfTwo(char ***pppKeys, U32 **ppVals, const char *pName)
{
	int iSize = eaSize(pppKeys);
	int i;
	U32 iMax = 0;
	U32 iMaxPlusOne = 0;
	for (i=0; i < iSize; i++)
	{
		char *pKey = (*pppKeys)[i];
		U32 iVal = (*ppVals)[i];

		if (stricmp(pKey, "LASTPLUSONE") == 0)
		{
			assertmsgf(iMaxPlusOne == 0, "Enum %s has two LASTPLUSONE fields. This is not legal", pName);
			assertmsgf(isPower2(iVal - 1), "Field LASTPLUSONE in enum %s does not have a value that is a power of two plus one. Instead, it has %u",
				pName, iVal);
			iMaxPlusOne = iVal;
		}
		else
		{
			assertmsgf(isPower2(iVal), "Field %s in enum %s does not have a value which is a power of two. Instead it has %u",
				pKey, pName, iVal);
			if (iVal > iMax)
			{
				iMax = iVal;
			}
		}
	}

	if (iMaxPlusOne)
	{
		assertmsgf(iMaxPlusOne = iMax + 1, "Enum %s has a LASTPLUSONE field that is not, in fact, the max value plus one. It's %u, max is %u",
			pName, iMaxPlusOne, iMax);
	}
}

static void VerifyNoOverlap(char ***pppKeys1, int **ppVals1, char ***pppKeys2, int **ppVals2, const char *pName1, const char *pName2)
{
	int iSize = eaSize(pppKeys1);
	int i;

	for (i=0; i < iSize; i++)
	{
		char *pKey = (*pppKeys1)[i];
		U32 iVal = (*ppVals1)[i];
		int iIndex = ea32Find(ppVals2, iVal);

		assertmsgf(iIndex < 0, "Static defines %s and %s should have disjoint values, but share value %u (named %s and %s)",
			pName1, pName2, iVal, pKey, (*pppKeys2)[iIndex]);
	}
}


void VerifyFlagEnums(StaticDefineInt *pDefine1, StaticDefineInt *pDefine2)
{
	char **ppKeys1 = NULL;
	U32 *pVals1 = NULL;

	char **ppKeys2 = NULL;
	U32 *pVals2 = NULL;

	const char *pName1 = NULL;
	const char *pName2 = NULL;


	if (pDefine2 && !pDefine1)
	{
		pDefine1 = pDefine2;
		pDefine2 = NULL;
	}

	pName1 = FindStaticDefineName(pDefine1);

	DefineFillAllKeysAndValues(pDefine1, &ppKeys1, &pVals1);
	
	VerifyPowersOfTwo(&ppKeys1, &pVals1, pName1);

	if (pDefine2)
	{

		pName2 = FindStaticDefineName(pDefine2);

		DefineFillAllKeysAndValues(pDefine2, &ppKeys2, &pVals2);

		VerifyPowersOfTwo(&ppKeys2, &pVals2, pName2);

		VerifyNoOverlap(&ppKeys1, &pVals1, &ppKeys2, &pVals2, pName1, pName2);
	}
}


//there are three values that can not legally be the int in a staticDefine: 0, because it can't
//be the key of a stashtable, -2, because it can't be a value in a stash table (don't ask why)
//and INT_MIN, because it's used as the default return value all over the place

#define STANDS_FOR_ZERO (INT_MIN+1)
#define STANDS_FOR_NEGATIVE_2 (INT_MIN+2)

int FastLookup_ActualIntValueToInternal(int iIn)
{
	if (iIn == STANDS_FOR_ZERO || iIn == STANDS_FOR_NEGATIVE_2)
	{
		AssertOrAlert("BAD_STATICDEFINE_KEY", 
			"%d and %d can not be STATICDEFINE keys, they are needed as magic values for various reasons",
			STANDS_FOR_ZERO, STANDS_FOR_NEGATIVE_2);
		return iIn;
	}

	if (iIn == 0)
	{
		return STANDS_FOR_ZERO;
	}

	if (iIn == -2)
	{
		return STANDS_FOR_NEGATIVE_2;
	}

	return iIn;
}

int FastLookup_InternalValueToActualValue(int iIn)
{
	if (iIn == STANDS_FOR_ZERO)
	{
		return 0;
	}

	if (iIn == STANDS_FOR_NEGATIVE_2)
	{
		return -2;
	}

	return iIn;
}

//because 0 can not be the key for a stashtable, we use INT_MIN to represent 0


typedef struct
{
	StashTable sStringsByInt;
	StashTable sIntsByString;
} StructDefineIntCache;

void StaticDefine_RegisterAllDefineContexts(StaticDefine *pDefine, StaticDefine *pParentDefine);

void StaticDefine_PossiblyAddFastIntLookupCache(StaticDefine *pDefine)
{
	StructDefineIntCache *pCache;
	char **ppKeys = NULL;
	S32 *pValues = NULL;
	int i;

	if (pDefine->key != U32_TO_PTR(DM_INT))
	{
		return;
	}

	Parser_InvalidateParseTableCRCs();

	if (pDefine->value)
	{
		pCache = (StructDefineIntCache*)pDefine->value;
		stashTableDestroySafe(&pCache->sIntsByString);
		stashTableDestroySafe(&pCache->sStringsByInt);
	}
	else
	{
		pCache = (StructDefineIntCache*)calloc(sizeof(StructDefineIntCache), 1);
		pDefine->value = (char*)pCache;
	}

	pCache->sStringsByInt = stashTableCreateInt(4);
	pCache->sIntsByString = stashTableCreateWithStringKeys(4, StashDefault);

	DefineFillAllKeysAndValues((StaticDefineInt*)pDefine, &ppKeys, &pValues);

	for (i = 0; i < eaSize(&ppKeys); i++)
	{
		int iValueToUse = FastLookup_ActualIntValueToInternal(pValues[i]);

		stashIntAddPointer(pCache->sStringsByInt, iValueToUse, ppKeys[i], false);
		stashAddInt(pCache->sIntsByString, ppKeys[i], iValueToUse, false);
	}

	eaDestroy(&ppKeys);
	ea32Destroy(&pValues);

	StaticDefine_RegisterAllDefineContexts(pDefine, pDefine);
}


void StaticDefine_ClearFastIntLookupCache(StaticDefine *pDefine)
{
	if (pDefine->key != U32_TO_PTR(DM_INT))
	{
		return;
	}

	Parser_InvalidateParseTableCRCs();

	if (pDefine->value)
	{
		StructDefineIntCache *pCache = (StructDefineIntCache*)pDefine->value;
		stashTableDestroySafe(&pCache->sIntsByString);
		stashTableDestroySafe(&pCache->sStringsByInt);
		free(pCache);
		pDefine->value = NULL;
	}
}

void StaticDefine_ReEnableFastIntLookupCache(StaticDefine *pDefine)
{
	StructDefineIntCache *pCache;
	char **ppKeys = NULL;
	S32 *pValues = NULL;
	int i;

	if (pDefine->key != U32_TO_PTR(DM_INT))
	{
		return;
	}

	Parser_InvalidateParseTableCRCs();


	pCache = (StructDefineIntCache*)calloc(sizeof(StructDefineIntCache), 1);
	pDefine->value = (char*)pCache;
	
	pCache->sStringsByInt = stashTableCreateInt(4);
	pCache->sIntsByString = stashTableCreateWithStringKeys(4, StashDefault);

	DefineFillAllKeysAndValues((StaticDefineInt*)pDefine, &ppKeys, &pValues);

	for (i = 0; i < eaSize(&ppKeys); i++)
	{
		int iValueToUse = FastLookup_ActualIntValueToInternal(pValues[i]);

		stashIntAddPointer(pCache->sStringsByInt, iValueToUse, ppKeys[i], false);
		stashAddInt(pCache->sIntsByString, ppKeys[i], iValueToUse, false);
	}

	eaDestroy(&ppKeys);
	ea32Destroy(&pValues);
}

void StaticDefineInt_ExtendWithDefineContext(StaticDefineInt *pStaticDefine, DefineContext **ppDefineContext)
{
	int i;

	StaticDefineInt_ClearFastIntLookupCache(pStaticDefine);

	for (i = 0; pStaticDefine[i].key != U32_TO_PTR(DM_END); i++);

	assertmsgf(pStaticDefine[i+1].key == U32_TO_PTR(DM_END), "Trying to extend static define %s which didn't have AEN_PAD",
		FindStaticDefineName(pStaticDefine));

	pStaticDefine[i].key = U32_TO_PTR(DM_DYNLIST);
	pStaticDefine[i].value = (intptr_t)(ppDefineContext);

	RegisterDefineContextForStaticDefine(ppDefineContext, (StaticDefine*)pStaticDefine);
	StaticDefineInt_ReEnableFastIntLookupCache(pStaticDefine);
}


int StaticDefine_FastStringToInt(StaticDefine *pDefine, const char *pStr, int iDefault)
{
	const char *pRes;

	if (pDefine->key == U32_TO_PTR(DM_INT) && pDefine->value)
	{
		int iFound;
		StructDefineIntCache *pCache = (StructDefineIntCache *)pDefine->value;
		if (stashFindInt(pCache->sIntsByString, pStr, &iFound))
		{
			return FastLookup_InternalValueToActualValue(iFound);
		}
	}

	pRes = StaticDefineLookup(pDefine, pStr);
	if (pRes)
	{
		return atoi(pRes);
	}

	return iDefault;
}

const char *StaticDefine_FastIntToString(StaticDefine *pDefine, int iInt)
{
	if (pDefine->key == U32_TO_PTR(DM_INT) && pDefine->value)
	{
		char *pFound;
		StructDefineIntCache *pCache = (StructDefineIntCache *)pDefine->value;
		if (stashIntFindPointer(pCache->sStringsByInt, FastLookup_ActualIntValueToInternal(iInt), &pFound))
		{
			return pFound;
		}
	}

	return StaticDefineIntRevLookup_Old((StaticDefineInt*)pDefine, iInt);
}


size_t StaticDefine_GetFastIntLookupCacheMemoryUsage(StaticDefine *pDefine)
{
	if (pDefine->key == U32_TO_PTR(DM_INT) && pDefine->value)
	{
		StructDefineIntCache *pCache = (StructDefineIntCache *)pDefine->value;

		return sizeof(StructDefineIntCache) + stashGetMemoryUsage(pCache->sIntsByString) + stashGetMemoryUsage(pCache->sStringsByInt);
	}

	return 0;
}








static void RegisterDefineContextForStaticDefine(DefineContext **ppDefineContextHandle, StaticDefine *pStaticDefine)
{
	ListOfStaticDefines *pList;

	if (!sListsOfStaticDefinesByDefineContextHandle)
	{
		sListsOfStaticDefinesByDefineContextHandle = stashTableCreateAddress(4);
	}

	if (!stashFindPointer(sListsOfStaticDefinesByDefineContextHandle, ppDefineContextHandle, &pList))
	{
		pList = calloc(sizeof(ListOfStaticDefines), 1);
		stashAddPointer(sListsOfStaticDefinesByDefineContextHandle, ppDefineContextHandle, pList, false);
	}

	if(eaFind(&pList->ppDefines, pStaticDefine) != -1)
	{
		return;
	}

	eaPush(&pList->ppDefines, pStaticDefine);
}

static void StaticDefine_ContextIntAdded(DefineContext **ppContext, const char *pKey, int uiValue, bool bOverlapOK)
{
	ListOfStaticDefines *pList;
	int i;
	int iInternalValue = FastLookup_ActualIntValueToInternal(uiValue);

	if (!stashFindPointer(sListsOfStaticDefinesByDefineContextHandle, ppContext, &pList))
	{
		return;
	}

	Parser_InvalidateParseTableCRCs();

	for (i = 0; i < eaSize(&pList->ppDefines); i++)
	{
		StaticDefine *pDefine = pList->ppDefines[i];
		StructDefineIntCache *pCache = (StructDefineIntCache *)pDefine->value;
		const char *pOtherString;
		int iOtherInt;

		if (!pCache)
		{
			//in the middle of reloading, the cache is temporarily gone
			continue;
		}

		iOtherInt = StaticDefine_FastStringToInt(pDefine, pKey, INT_MIN);
		if (iOtherInt != INT_MIN && !bOverlapOK)
		{
			Errorf("Static define %s having dynamic value %s(%d) added. %s is already present, with value %d",
				FindStaticDefineName((StaticDefineInt*)pDefine), pKey, uiValue, pKey, iOtherInt);
		}

		pOtherString = StaticDefine_FastIntToString(pDefine, uiValue);

		if (pOtherString && !bOverlapOK)
		{
			Errorf("Static define %s having dynamic value %s(%d) added. %d is already present, with value %s",
				FindStaticDefineName((StaticDefineInt*)pDefine), pKey, uiValue, uiValue, pOtherString);
		}

		

		stashIntAddPointer(pCache->sStringsByInt, iInternalValue, (char*)pKey, false);
		stashAddInt(pCache->sIntsByString, pKey, iInternalValue, false);
	}
}


void StaticDefine_RegisterAllDefineContexts(StaticDefine *pDefine, StaticDefine *pParentDefine)
{
	StaticDefine *cur = pDefine;

	while (1)
	{
		// look for key markers first
		if (cur->key == U32_TO_PTR(DM_END))
			return; 
		else if (cur->key == U32_TO_PTR(DM_DYNLIST))
		{
			RegisterDefineContextForStaticDefine((DefineContext**)(cur->value), pParentDefine);
		}
		else if (cur->key == U32_TO_PTR(DM_TAILLIST))
		{
			StaticDefine_RegisterAllDefineContexts((StaticDefine *)(cur->value),pParentDefine);
			return;
		}
		// keep looking for keys
		cur++;
	}
}


void DefineClearAllListsAndReset(DefineContext **ppContext)
{
	ListOfStaticDefines *pList;
	int i;

	if (stashFindPointer(sListsOfStaticDefinesByDefineContextHandle, ppContext, &pList))
	{
		Parser_InvalidateParseTableCRCs();

		for (i = 0; i < eaSize(&pList->ppDefines); i++)
		{
			StaticDefine *pDefine = pList->ppDefines[i];

			StaticDefine_ClearFastIntLookupCache(pDefine);
		}
	}

	DefineDestroy(*ppContext);
	*ppContext = DefineCreate();
}

void DefineReCacheAllLists(DefineContext **ppContext)
{
	ListOfStaticDefines *pList;
	int i;

	if (!stashFindPointer(sListsOfStaticDefinesByDefineContextHandle, ppContext, &pList))
	{
		return;
	}

	Parser_InvalidateParseTableCRCs();

	for (i = 0; i < eaSize(&pList->ppDefines); i++)
	{
		StaticDefine *pDefine = pList->ppDefines[i];
		StaticDefine_ReEnableFastIntLookupCache(pDefine);
	}
}


void StaticDefineIntParseStringForInts(StaticDefineInt* list, const char *pchStr, S32 **piTagsOut, const char *pToks)
{
	static const char *s_pDefaultToks = " \r\n\t,|%";
	char* pchParseBuffer, *pchToken, *pchContext = NULL;

	if (!pToks)
		pToks = s_pDefaultToks;

	strdup_alloca(pchParseBuffer, pchStr);
	if ((pchToken = strtok_r(pchParseBuffer, pToks, &pchContext)) != NULL)
	{
		do {
			int tag = StaticDefineIntGetInt(list,pchToken);
			if (tag != -1)
			{
				eaiPush(piTagsOut, tag);
			}
		} while ((pchToken = strtok_r(NULL, pToks, &pchContext)) != NULL);
	}
}