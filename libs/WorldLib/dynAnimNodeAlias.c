#include "dynAnimNodeAlias.h"
#include "dynAnimNodeAlias_h_ast.h"

#include "dynNodeInline.h"
#include "dynSeqData.h"
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "mathutil.h"
#include "Quat.h"
#include "StringCache.h"
#include "timing.h"
#include "utils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

#define DYN_ANIM_NODE_ALIAS_USE_STASH_TABLE_SIZE 0

DictionaryHandle hDynAnimNodeAliasListDict;

// +---------------+
// |               |
// | Load Routines |
// |               |
// +---------------+

static bool dynAnimNodeAliasVerify(DynAnimNodeAliasList *pList, DynAnimNodeAlias* pData)
{
	bool bRet = true;
	if (!pData->pcAlias) {
		ErrorFilenamef(pList->pcFileName, "Missing alias node name!\n");
		bRet = false;
	}
	if (!pData->pcTarget) {
		ErrorFilenamef(pList->pcFileName, "Missing target node name!\n");
		bRet = false;
	}
	return bRet;
}

static bool dynAnimNodeAliasListVerify(DynAnimNodeAliasList *pList)
{
	bool bRet = true;
	FOR_EACH_IN_EARRAY(pList->eaFxAlias, DynAnimNodeAlias, pData) {
		S32 i;
		bRet &= dynAnimNodeAliasVerify(pList, pData);
		for (i = ipDataIndex+1; i < eaSize(&pList->eaFxAlias); i++) {
			if (pData->pcAlias == pList->eaFxAlias[i]->pcAlias) {
				ErrorFilenamef(pList->pcFileName, "Found duplicate alias: %s!\n",pData->pcAlias);
				bRet = false;
				break;
			}
		}
	} FOR_EACH_END;
	return bRet;
}

static bool dynAnimNodeAliasListFixup(DynAnimNodeAliasList *pList)
{
	char cName[256];
	getFileNameNoExt(cName, pList->pcFileName);
	pList->pcName = allocAddString(cName);

	stashTableDestroySafe(&pList->stFxAlias);
	pList->stFxAlias = stashTableCreateWithStringKeys(32, StashDefault);
	FOR_EACH_IN_EARRAY(pList->eaFxAlias, DynAnimNodeAlias, pData)
	{
		if (!stashAddPointer(pList->stFxAlias, pData->pcAlias, pData, false)) {
			assert(0); // duplicates should have been found during the verify pass
		}
	}
	FOR_EACH_END;

	return true;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDynAnimNodeAliasList(DynAnimNodeAliasList *pList, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynAnimNodeAliasListVerify(pList) || !dynAnimNodeAliasListFixup(pList))
				return PARSERESULT_INVALID; // remove entry
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynAnimNodeAliasListFixup(pList))
				return PARSERESULT_INVALID; // remove entry
	}
	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerDynAnimNodeAliasListDict(void)
{
	hDynAnimNodeAliasListDict = RefSystem_RegisterSelfDefiningDictionary("DynAnimNodeAliasList", false, parse_DynAnimNodeAliasList, true, false, NULL);
}

static void dynAnimNodeAliasListReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath)) {
		; // File was deleted, do we care here?
	}

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if(!ParserReloadFileToDictionary(relpath, hDynAnimNodeAliasListDict)) {
		CharacterFileError(relpath, "Error reloading DynAnimNodeAliasList file: %s", relpath);
	}
}

void dynAnimNodeAliasLoadAll(void)
{
	loadstart_printf("Loading DynAnimNodeAliasLists...");
	ParserLoadFilesToDictionary("dyn/AnimNodeAliases", ".ana", "DynAnimNodeAlias.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hDynAnimNodeAliasListDict);
	if(isDevelopmentMode()) {
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/AnimNodeAliases/*.ana", dynAnimNodeAliasListReloadCallback);
	}
	loadend_printf("done (%d DyAnimNodeAliasLists)", RefSystem_GetDictionaryNumberOfReferents(hDynAnimNodeAliasListDict));
}

// +---------+
// | Lookups |
// +---------+

const char *dynAnimNodeFxAlias(const DynAnimNodeAliasList *pList, const char *pcName)
{
	if (pList && pcName)
	{
		const char *pcPooledName = allocFindString(pcName);
		if (eaSize(&pList->eaFxAlias) < DYN_ANIM_NODE_ALIAS_USE_STASH_TABLE_SIZE)
		{
			FOR_EACH_IN_EARRAY(pList->eaFxAlias, DynAnimNodeAlias, pData) {
				if (pData->pcAlias == pcPooledName) {
					return pData->pcTarget;
				}
			} FOR_EACH_END;
		}
		else
		{
			StashElement element;
			if (stashFindElementConst(pList->stFxAlias, pcPooledName, &element)) {
				DynAnimNodeAlias *pData = stashElementGetPointer(element);
				return pData->pcTarget;
			}
		}
	}
	return pcName;
}

const char *dynAnimNodeFxDefaultAlias(const DynAnimNodeAliasList *pList)
{
	return SAFE_MEMBER(pList,pcFxDefaultNode);
}

// +--------------+
// | AST includes |
// +--------------+

#include "dynAnimNodeAlias_h_ast.c"