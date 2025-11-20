#include "dynCriticalNodes.h"

#include "dynSeqData.h" //for the animation file error
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "mathutil.h"
#include "referencesystem.h"
#include "StringCache.h"

#include "dynCriticalNodes_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

//////////////////////////////////////////////////////////////////////////////////
//
// Critical Nodes List
//
//////////////////////////////////////////////////////////////////////////////////

DictionaryHandle hCriticalNodeListDict;

/*
bool dynCriticalNodeListVerify(DynCriticalNodeList *pCriticalNodeList)
{
	bool bRet = true;

	if (false) {
		ErrorFilenamef(pCriticalNodeList->pcFileName, "whatever the error was!\n");
		bRet = false;
	}

	return bRet;
}
*/

bool dynCriticalNodeListFixup(DynCriticalNodeList *pCriticalNodeList)
{
	char cName[256];
	getFileNameNoExt(cName, pCriticalNodeList->pcFileName);
	pCriticalNodeList->pcName = allocAddString(cName);
	return true;
}

static void dynCriticalNodeListReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath)) {
		; // File was deleted, do we care here?
	}

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if(!ParserReloadFileToDictionary(relpath,hCriticalNodeListDict)) {
		AnimFileError(relpath, "Error reloading DynCriticalNodeList file: %s", relpath);
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupCriticalNodeList(DynCriticalNodeList* pCriticalNodeList, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (//!dynCriticalNodesListVerify(pGroundRegData) ||
				!dynCriticalNodeListFixup(pCriticalNodeList))
			{
				return PARSERESULT_INVALID; // remove this
			}
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynCriticalNodeListFixup(pCriticalNodeList))
			{
				return PARSERESULT_INVALID; // remove this
			}
	}
	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerCriticalNodeListDict(void)
{
	hCriticalNodeListDict = RefSystem_RegisterSelfDefiningDictionary("DynCriticalNodeList", false, parse_DynCriticalNodeList, true, false, NULL);
}

void dynCriticalNodeListLoadAll(void)
{
	loadstart_printf("Loading DynCriticalNodeLists...");

	// optional for outsource build
	ParserLoadFilesToDictionary("dyn/criticalnodes", ".criticalnodes", "DynCriticalNodeList.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hCriticalNodeListDict);

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/criticalnodes/*.criticalnodes", dynCriticalNodeListReloadCallback);
	}

	loadend_printf("done (%d DynCriticalNodeLists)", RefSystem_GetDictionaryNumberOfReferents(hCriticalNodeListDict));
}

#include "dynCriticalNodes_h_ast.c"