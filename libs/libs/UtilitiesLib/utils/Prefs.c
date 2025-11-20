//
// Prefs.c
//

#include "Prefs.h"
#include "error.h"
#include "file.h"
#include "fileLoader.h"
#include "timing.h"
#include "utilitiesLib.h"
#include "StringCache.h"
#include "AutoGen/Prefs_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_STRUCT;
typedef struct PrefEntry {
	char *pcName;
	char *pcValue;
} PrefEntry;

// Non-auto-struct'd so we can have a redundant struct definition for backwards compatibility
typedef struct PrefSet {
	const char *pcFileName; // string pooled
	PrefEntry **eaPrefEntries;
	bool bChanged;
} PrefSet;

ParseTable parse_PrefEntryOneline[] =
{
	{ "Name",		TOK_STRUCTPARAM|TOK_STRING(PrefEntry, pcName, 0), NULL },
	{ "Value",		TOK_STRUCTPARAM|TOK_STRING(PrefEntry, pcValue, 0), NULL },
	{ "\n",			TOK_END },
	{ "", 0, 0 }
};
#define TYPE_parse_PrefEntryOneline PrefEntryOneline

ParseTable parse_PrefSet[] =
{
	{ "{",				TOK_IGNORE, 0 },
	{ "PrefEntry",		TOK_STRUCT(PrefSet, eaPrefEntries, parse_PrefEntryOneline) },
	{ "PrefEntries",	TOK_REDUNDANTNAME|TOK_STRUCT(PrefSet, eaPrefEntries, parse_PrefEntry) },
	{ "Filename",		TOK_IGNORE, 0 },
	{ "}",				TOK_IGNORE, 0 },
	{ "", 0, 0 }
};
#define TYPE_parse_PrefSet PrefSet

AUTO_RUN;
void initPrefSetTPIs(void)
{
	ParserSetTableInfoRecurse(parse_PrefSet, sizeof(PrefSet), "PrefSet", NULL, __FILE__, NULL, SETTABLEINFO_NAME_STATIC | SETTABLEINFO_ALLOW_CRC_CACHING);
};



//---------------------------------------------------------------------------------------------------
// Global Data
//---------------------------------------------------------------------------------------------------

static PrefSet **eaPrefSets = NULL;
static int iGamePrefSet = -1;
static bool bPrefSetsDirty;

//---------------------------------------------------------------------------------------------------
// Internal Functions
//---------------------------------------------------------------------------------------------------

static int PrefSetCreate(const char *pcFileName)
{
	PrefSet *pPrefSet= StructCreate(parse_PrefSet);
	int i;

	// Read the file
	ParserReadTextFile(pcFileName, parse_PrefSet, pPrefSet, 1);
	pPrefSet->pcFileName = allocAddFilename(pcFileName);

	// Make sure there are no NULL prefs
	for(i=eaSize(&pPrefSet->eaPrefEntries)-1; i>=0; --i) {
		if (pPrefSet->eaPrefEntries[i]->pcValue == NULL) {
			pPrefSet->eaPrefEntries[i]->pcValue = StructAllocString("");
		}
	}

	// Add to list of pref sets
	eaPush(&eaPrefSets, pPrefSet);
	return eaSize(&eaPrefSets) - 1;
}

static void PrefSetWrite(const char *pcFilename, char *pcText)
{
	FILE *f;
	PERFINFO_AUTO_START_FUNC();
	makeDirectoriesForFile(pcFilename);
	f = fopen(pcFilename, "wt");

	if (f)
	{
		fprintf(f, "%s", pcText);
		fclose(f);
	}

	estrDestroy(&pcText);
	PERFINFO_AUTO_STOP();
}

static int cmpPrefEntry(const PrefEntry **a, const PrefEntry **b)
{
	return stricmp((*a)->pcName, (*b)->pcName);
}

static void PrefSetSave(int iSet)
{
	char *pcText = NULL;
	
	eaQSort(eaPrefSets[iSet]->eaPrefEntries, cmpPrefEntry);
	
	if (!ParserWriteText(&pcText, parse_PrefSet, eaPrefSets[iSet], 0, 0, 0))
	{
		Alertf("Unable to write preferences text for file '%s'", eaPrefSets[iSet]->pcFileName);
		estrDestroy(&pcText);
	}
	else
	{
		if (utilitiesLibShouldQuit())
		{
			PrefSetWrite(eaPrefSets[iSet]->pcFileName, pcText);
		} else {
			fileLoaderRequestAsyncExec(eaPrefSets[iSet]->pcFileName, FILE_LOW_PRIORITY, false, PrefSetWrite, pcText);
		}
	}

	eaPrefSets[iSet]->bChanged = false;
}

void PrefSetOncePerFrame(void)
{
	int i;

	if (!bPrefSetsDirty)
		return;

	for (i = eaSize(&eaPrefSets)-1; i >= 0; --i)
	{
		if (eaPrefSets[i]->bChanged)
			PrefSetSave(i);
	}

	bPrefSetsDirty = false;
}

static void PrefSetDirty(int iSet)
{
	eaPrefSets[iSet]->bChanged = true;
	bPrefSetsDirty = true;
}

//---------------------------------------------------------------------------------------------------
// General Access Functions
//---------------------------------------------------------------------------------------------------

int PrefSetGet(const char *pcFileName)
{
	int i;

	// Look for existing set
	for(i=eaSize(&eaPrefSets)-1; i>=0; --i) {
		if (stricmp(pcFileName, eaPrefSets[i]->pcFileName) == 0) {
			return i;
		}
	}

	// Create a new one
	return PrefSetCreate(pcFileName);
}

const char *PrefGetString(int iPrefSet, const char *pcPrefName, const char *pcDefault)
{
	PrefSet *pPrefSet = eaPrefSets[iPrefSet];
	int i;

	for(i=eaSize(&pPrefSet->eaPrefEntries)-1; i>=0; --i) {
		if (stricmp(pPrefSet->eaPrefEntries[i]->pcName, pcPrefName) == 0) {
			return pPrefSet->eaPrefEntries[i]->pcValue;
		}
	}

	return pcDefault;
}

int PrefGetInt(int iPrefSet, const char *pcPrefName, int iDefault)
{
	const char *pcText = PrefGetString(iPrefSet, pcPrefName, NULL);
	if (pcText) {
		return atoi(pcText);
	}
	return iDefault;
}

F32 PrefGetFloat(int iPrefSet, const char *pcPrefName, F32 fDefault)
{
	const char *pcText = PrefGetString(iPrefSet, pcPrefName, NULL);
	if (pcText) {
		return atof(pcText);
	}
	return fDefault;
}

int PrefGetPosition(int iPrefSet, const char *pcPrefName, F32 *pX, F32 *pY, F32 *pW, F32 *pH)
{
	const char *pcText = PrefGetString(iPrefSet, pcPrefName, NULL);
	char buf[260];
	char *pcStart = buf;
	char *pcEnd;

	if (!pcText) {
		return 0;
	}

	strcpy(buf, pcText);

	pcEnd = strchr(pcStart,' ');
	if (pcEnd) {
		*pcEnd = '\0';
	}
	if (*pcStart) {
		*pX = atof(pcStart);
	}
	if (!pcEnd) {
		return 0;
	}
	pcStart = pcEnd+1;

	pcEnd = strchr(pcStart,' ');
	if (pcEnd) {
		*pcEnd = '\0';
	}
	if (*pcStart) {
		*pY = atof(pcStart);
	}
	if (!pcEnd) {
		return 0;
	}
	pcStart = pcEnd+1;

	pcEnd = strchr(pcStart,' ');
	if (pcEnd) {
		*pcEnd = '\0';
	}
	if (*pcStart) {
		*pW = atof(pcStart);
	}
	if (!pcEnd) {
		return 0;
	}
	pcStart = pcEnd+1;

	pcEnd = strchr(pcStart,' ');
	if (pcEnd) {
		*pcEnd = '\0';
	}
	if (*pcStart) {
		*pH = atof(pcStart);
	}
	return 1;
}

void PrefStoreString(int iPrefSet, const char *pcPrefName, const char *pcValue)
{
	PrefSet *pPrefSet = eaPrefSets[iPrefSet];
	PrefEntry *pEntry;
	int i;

	if (!pcValue) {
		PrefClear(iPrefSet, pcPrefName);
		return;
	}

	// Overwrite previous value
	for(i=eaSize(&pPrefSet->eaPrefEntries)-1; i>=0; --i) {
		if (stricmp(pPrefSet->eaPrefEntries[i]->pcName, pcPrefName) == 0) {
			if (strcmp(pPrefSet->eaPrefEntries[i]->pcValue, pcValue) != 0) {
				StructFreeString(pPrefSet->eaPrefEntries[i]->pcValue);
				pPrefSet->eaPrefEntries[i]->pcValue = StructAllocString(pcValue);

				PrefSetDirty(iPrefSet);
			}
			return;
		}
	}

	// Add new value
	pEntry = (PrefEntry*)StructCreate(parse_PrefEntry);
	pEntry->pcName = StructAllocString(pcPrefName);
	pEntry->pcValue = StructAllocString(pcValue);
	eaPush(&pPrefSet->eaPrefEntries, pEntry);

	PrefSetDirty(iPrefSet);
}

void PrefStoreInt(int iPrefSet, const char *pcPrefName, int iValue)
{
	char buf[260];
	sprintf(buf, "%d", iValue);
	PrefStoreString(iPrefSet, pcPrefName, buf);
}

void PrefStoreFloat(int iPrefSet, const char *pcPrefName, F32 fValue)
{
	char buf[260];
	sprintf(buf, "%f", fValue);
	PrefStoreString(iPrefSet, pcPrefName, buf);
}

void PrefStorePosition(int iPrefSet, const char *pcPrefName, F32 x, F32 y, F32 w, F32 h)
{
	char buf[260];
	sprintf(buf, "%f %f %f %f", x, y, w, h);
	PrefStoreString(iPrefSet, pcPrefName, buf);
}

int PrefGetStruct(int iPrefSet, const char *pcPrefName, ParseTable *pParseTable, void *pStruct)
{
	char keyBuf[260];
	const char *pcValue;
	int i;
	int ret = 1;

	FORALL_PARSETABLE(pParseTable, i)
	{
		assert(pParseTable[i].name);
		if (pParseTable[i].type & TOK_REDUNDANTNAME) {
			continue; // don't allow TOK_REDUNDANTNAME
		}
		if (!pParseTable[i].name[0]) {
			continue;
		}
		sprintf(keyBuf, "%s.%s", pcPrefName, pParseTable[i].name);
		pcValue = PrefGetString(iPrefSet, keyBuf, NULL);
		if (pcValue)
		{
			TokenFromSimpleString(pParseTable, i, pStruct, pcValue);
		} else {
			TokenFromSimpleString(pParseTable, i, pStruct, "");
			ret = 0;
		}
	}
	return ret;
}

void PrefStoreStruct(int iPrefSet, const char *pcPrefName, ParseTable *pParseTable, void *pStruct)
{
	char keyBuf[260];
	char valueBuf[10240];
	int i;

	FORALL_PARSETABLE(pParseTable, i)
	{
		if (pParseTable[i].type & TOK_REDUNDANTNAME) {
			continue; // don't allow TOK_REDUNDANTNAME
		}
		if (pParseTable[i].name && pParseTable[i].name[0] && TokenToSimpleString(pParseTable, i, pStruct, SAFESTR(valueBuf), 0)) {
			sprintf(keyBuf, "%s.%s", pcPrefName, pParseTable[i].name);
			PrefStoreString(iPrefSet, keyBuf, valueBuf);
		}
	}
}

bool PrefIsSet(int iPrefSet, const char *pcPrefName)
{
	PrefSet *pPrefSet = eaPrefSets[iPrefSet];
	int i;

	for(i=eaSize(&pPrefSet->eaPrefEntries)-1; i>=0; --i) {
		if (stricmp(pPrefSet->eaPrefEntries[i]->pcName, pcPrefName) == 0) {
			return true;
		}
	}

	return false;
}

void PrefClear(int iPrefSet, const char *pcPrefName)
{
	PrefSet *pPrefSet = eaPrefSets[iPrefSet];
	int i;

	for(i=eaSize(&pPrefSet->eaPrefEntries)-1; i>=0; --i) {
		if (stricmp(pPrefSet->eaPrefEntries[i]->pcName, pcPrefName) == 0) {
			eaRemove(&pPrefSet->eaPrefEntries, i);
			PrefSetDirty(iPrefSet);
			return;
		}
	}
}


//---------------------------------------------------------------------------------------------------
// Game Access Functions
//---------------------------------------------------------------------------------------------------

int GamePrefGetPrefSet(void)
{
	if (iGamePrefSet < 0) {
		// Initialize the pref set
		char buf[260];
		// Xbox writes to the same folder over a share, so use a different file name
#if _XBOX
		sprintf(buf, "%s/GamePrefsXBOX.pref", fileLocalDataDir());
#elif _PS3
		sprintf(buf, "%s/GamePrefsPS3.pref", fileLocalDataDir());
#else
		sprintf(buf, "%s/GamePrefs.pref", fileLocalDataDir());
#endif
		iGamePrefSet = PrefSetCreate(buf);
	}
	return iGamePrefSet;
}

const char *GamePrefGetString(const char *pcPrefName, const char *pcDefault)
{
	return PrefGetString(GamePrefGetPrefSet(), pcPrefName, pcDefault);
}

int GamePrefGetInt(const char *pcPrefName, int iDefault)
{
	return PrefGetInt(GamePrefGetPrefSet(), pcPrefName, iDefault);
}

F32 GamePrefGetFloat(const char *pcPrefName, F32 fDefault)
{
	return PrefGetFloat(GamePrefGetPrefSet(), pcPrefName, fDefault);
}

int GamePrefGetPosition(const char *pcPrefName, F32 *pX, F32 *pY, F32 *pW, F32 *pH)
{
	return PrefGetPosition(GamePrefGetPrefSet(), pcPrefName, pX, pY, pW, pH);
}

void GamePrefStoreString(const char *pcPrefName, const char *pcValue)
{
	PrefStoreString(GamePrefGetPrefSet(), pcPrefName, pcValue);
}

void GamePrefStoreInt(const char *pcPrefName, int iValue)
{
	PrefStoreInt(GamePrefGetPrefSet(), pcPrefName, iValue);
}

void GamePrefStoreFloat(const char *pcPrefName, F32 fValue)
{
	PrefStoreFloat(GamePrefGetPrefSet(), pcPrefName, fValue);
}

void GamePrefStorePosition(const char *pcPrefName, F32 x, F32 y, F32 w, F32 h)
{
	PrefStorePosition(GamePrefGetPrefSet(), pcPrefName, x, y, w, h);
}

int GamePrefGetStruct(const char *pcPrefName, ParseTable *pParseTable, void *pStruct)
{
	return PrefGetStruct(GamePrefGetPrefSet(), pcPrefName, pParseTable, pStruct);
}

void GamePrefStoreStruct(const char *pcPrefName, ParseTable *pParseTable, void *pStruct)
{
	int iPrefSet = GamePrefGetPrefSet();
	PrefStoreStruct(iPrefSet, pcPrefName, pParseTable, pStruct);
	if (utilitiesLibShouldQuit() && eaPrefSets[iPrefSet]->bChanged)
		PrefSetSave(iPrefSet); // Need to call save here, as the caller might be just about to call exit()
}

bool GamePrefIsSet(const char *pcPrefName)
{
	return PrefIsSet(GamePrefGetPrefSet(), pcPrefName);
}

void GamePrefClear(const char *pcPrefName)
{
	PrefClear(GamePrefGetPrefSet(), pcPrefName);
}


//
// Include the auto-generated code so it gets compiled
//
#include "AutoGen/Prefs_c_ast.c"