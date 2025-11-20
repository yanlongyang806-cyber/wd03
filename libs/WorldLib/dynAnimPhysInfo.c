#include "dynAnimPhysInfo.h"

#include "fileutil.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "error.h"
#include "ResourceManager.h"
#include "wlState.h"
#include "quat.h"

#include "dynSeqData.h"
#include "dynSkeleton.h"

#include "dynAnimPhysInfo_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

//////////////////////////////////////////////////////////////////////////////////
//
// Simple Physics Info 
//
//////////////////////////////////////////////////////////////////////////////////
DictionaryHandle hBouncerInfoDict;


bool dynBouncerInfoVerify(DynBouncerGroupInfo* pInfo)
{
	FOR_EACH_IN_EARRAY(pInfo->eaBouncer, DynBouncerInfo, pBouncerInfo)
	{
		if (pBouncerInfo->fDampRate < 0.0f || pBouncerInfo->fDampRate > 10.0f)
		{
			AnimFileError(pInfo->pcFileName, "DampRate is %.2f, but it must be between 0.0 and 10.0", pBouncerInfo->fDampRate);
			return false;
		}
		if (pBouncerInfo->fMaxDist <= 0.0f)
		{
			AnimFileError(pInfo->pcFileName, "MaxDist must be specified, and must be greater than 0");
			return false;
		}
		if (vec4IsZero(pBouncerInfo->qRot))
		{
			unitQuat(pBouncerInfo->qRot);
		}
		else
			quatNormalize(pBouncerInfo->qRot);
	}
	FOR_EACH_END;
	return true;
}

bool dynBouncerInfoFixup(DynBouncerGroupInfo* pInfo)
{
	{
		char cName[256];
		getFileNameNoExt(cName, pInfo->pcFileName);
		pInfo->pcInfoName = allocAddString(cName);
	}
	return true;
}

static void dynBouncerInfoReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);


	if(!ParserReloadFileToDictionary(relpath,hBouncerInfoDict))
	{
		AnimFileError(relpath, "Error reloading DynBouncerGroupInfo file: %s", relpath);
	}
	else
	{
		// nothing to do here
	}
	// Reload skeletal bouncer updaters
	// JE: I think this call might not be needed now, had to reload bouncers specifically to get it to reload at the login screen below
	if (wl_state.force_costume_reload_func)
		wl_state.force_costume_reload_func();
	dynSkeletonResetBouncers();
}

AUTO_FIXUPFUNC;
TextParserResult fixupBouncerInfo(DynBouncerGroupInfo* pBouncerInfo, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynBouncerInfoVerify(pBouncerInfo) || !dynBouncerInfoFixup(pBouncerInfo))
				return PARSERESULT_INVALID; // remove this from the costume list
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynBouncerInfoFixup(pBouncerInfo))
				return PARSERESULT_INVALID; // remove this from the costume list
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerSimplePhysicsInfoDict(void)
{
	hBouncerInfoDict = RefSystem_RegisterSelfDefiningDictionary("DynBouncerGroup", false, parse_DynBouncerGroupInfo, true, false, NULL);

	if (IsServer())
	{
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(hBouncerInfoDict, NULL, NULL, NULL, NULL, NULL);
		}
	}
}

void dynBouncerInfoLoadAll(void)
{
	resLoadResourcesFromDisk(hBouncerInfoDict, "dyn/bouncer", ".bounce", "DynBouncer.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/bouncer/*.bounce", dynBouncerInfoReloadCallback);
	}
}

#include "dynAnimPhysInfo_h_ast.c"