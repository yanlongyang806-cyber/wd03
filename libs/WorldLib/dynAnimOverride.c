#include "dynAnimOverride.h"


#include "GlobalTypes.h"
#include "ResourceManager.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "Error.h"

#include "dynSeqData.h"
#include "dynSequencer.h"

#include "dynAnimOverride_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

DictionaryHandle hAnimOverrideDict;


bool dynAnimOverrideInfoVerify(DynAnimOverrideList* pList)
{
	FOR_EACH_IN_EARRAY(pList->eaAnimOverride, DynAnimOverride, pOverride)
	{
		const char* pcBadBit = NULL;
		if (!GET_REF(pOverride->hMove))
		{
			AnimFileError(pList->pcFileName, "Can't find move %s", REF_STRING_FROM_HANDLE(pOverride->hMove));
			return false;
		}
		if (!dynBitFieldStaticSetFromStrings(&pOverride->bits, &pcBadBit))
		{
			AnimFileError(pList->pcFileName, "Invalid bit %s in DynAnimOverride %s bits", pcBadBit, pOverride->pcName);
			return false;
		}
		if (!pOverride->bits.uiNumBits)
		{
			AnimFileError(pList->pcFileName, "No valid bits in DynAnimOverride %s bits", pOverride->pcName);
			return false;
		}
	}
	FOR_EACH_END;
	return true;
}

bool dynAnimOverrideInfoFixup(DynAnimOverrideList* pList)
{
	{
		char cName[256];
		getFileNameNoExt(cName, pList->pcFileName);
		pList->pcName = allocAddString(cName);
	}
	FOR_EACH_IN_EARRAY(pList->eaAnimOverride, DynAnimOverride, pOverride)
	{
		const char* pcBadBit = NULL;
		if (!dynBitFieldStaticSetFromStrings(&pOverride->bits, &pcBadBit))
		{
			AnimFileError(pList->pcFileName, "Invalid bit %s in DynAnimOverride %s bits", pcBadBit, pOverride->pcName);
			return false;
		}

        pOverride->nBones = eaSize(&pOverride->eaBones);
	}
	FOR_EACH_END;
	return true;
}

static void dynAnimOverrideListReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);


	if(!ParserReloadFileToDictionary(relpath,hAnimOverrideDict))
	{
		AnimFileError(relpath, "Error reloading DynAnimOverrideList file: %s", relpath);
	}
	else
	{
		// nothing to do here
	}
	dynSequencerResetAll();
}

AUTO_FIXUPFUNC;
TextParserResult fixupAnimOverrideList(DynAnimOverrideList* pAnimOverrideInfo, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynAnimOverrideInfoVerify(pAnimOverrideInfo) || !dynAnimOverrideInfoFixup(pAnimOverrideInfo))
				return PARSERESULT_INVALID; // remove this from the costume list
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynAnimOverrideInfoFixup(pAnimOverrideInfo))
				return PARSERESULT_INVALID; // remove this from the costume list
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerAnimOverrideInfoDict(void)
{
	hAnimOverrideDict = RefSystem_RegisterSelfDefiningDictionary("DynAnimOverrideList", false, parse_DynAnimOverrideList, true, false, NULL);

	if (IsServer())
	{
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(hAnimOverrideDict, NULL, NULL, NULL, NULL, NULL);
		}
	}
}

void dynAnimOverrideListLoadAll(void)
{
	char *pSharedMemoryName = NULL;
	loadstart_printf("Loading DynAnimOverrideLists...");

	MakeSharedMemoryName("DynAnimOverrideList", &pSharedMemoryName);
	ParserLoadFilesToDictionary("dyn/animoverride", ".aover", "DynAnimOverride.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hAnimOverrideDict);
	estrDestroy(&pSharedMemoryName);

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/animoverride/*.aover", dynAnimOverrideListReloadCallback);
	}

	loadend_printf("done (%d DynAnimOverrideList)", RefSystem_GetDictionaryNumberOfReferents(hAnimOverrideDict) );
}

#include "dynAnimOverride_h_ast.c"