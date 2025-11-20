#include "dynGroundReg.h"

#include "dynSeqData.h" //for the animation file error
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "mathutil.h"
#include "referencesystem.h"
#include "StringCache.h"

#include "dynGroundReg_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

//////////////////////////////////////////////////////////////////////////////////
//
// Ground Reg Data 
//
//////////////////////////////////////////////////////////////////////////////////

DictionaryHandle hGroundRegDataDict;

bool dynGroundRegDataVerify(DynGroundRegData *pGroundRegData)
{
	bool bRet = true;

	//check the static registration bones

	if (!pGroundRegData->pcHipsNode) {
		ErrorFilenamef(pGroundRegData->pcFileName, "Must specify a HipsNode\n");
		bRet = false;
	}
	else if (pGroundRegData->pcHipsNode == pGroundRegData->pcHeightFixupNode) {
		ErrorFilenamef(pGroundRegData->pcFileName, "Not allowed to use the same node for the HipsNode and the HeightFixupNode\n");
		bRet = false;
	}
	if (!pGroundRegData->pcHeightFixupNode) {
		ErrorFilenamef(pGroundRegData->pcFileName, "Must specify a HeightFixupNode\n");
		bRet = false;
	}

	// check the limb data

	if (eaSize(&pGroundRegData->eaLimbs))
	{
		// check defines for height above floor before corrections are made
		if (pGroundRegData->fFloorDeltaNear < 0.f) {
			ErrorFilenamef(pGroundRegData->pcFileName, "FloorDeltaNear must be greater than zero\n");
			bRet = false;
		}
		if (pGroundRegData->fFloorDeltaFar <= pGroundRegData->fFloorDeltaNear) {
			ErrorFilenamef(pGroundRegData->pcFileName, "FloorDeltaFar must be larger than FloorDeltaNear\n");
			bRet = false;
		}

		FOR_EACH_IN_EARRAY(pGroundRegData->eaLimbs, DynGroundRegDataLimb, pLimb)
		{
			//check the limb bones
			if (!pLimb->pcHeightFixupNode) {
				ErrorFilenamef(pGroundRegData->pcFileName, "Every limb must specify a HeightFixupNode (which can be the same as the EndEffectorNode)\n");
				bRet = false;
			}
			if (!pLimb->pcEndEffectorNode) {
				ErrorFilenamef(pGroundRegData->pcFileName, "Every limb must specify a EndEffectorNode (which can be the same as the HeightFixupNode)\n");
				bRet = false;
			}

			//check for uniqueness with the hips bone
			if (pLimb->pcHeightFixupNode == pGroundRegData->pcHipsNode ||
				pLimb->pcEndEffectorNode == pGroundRegData->pcHipsNode)
			{
				ErrorFilenamef(pGroundRegData->pcFileName, "Limbs are not allowed to use the HipsNode\n");
				bRet = false;
			}

			//check for uniqueness across limbs
			FOR_EACH_IN_EARRAY(pGroundRegData->eaLimbs, DynGroundRegDataLimb, pChkLimb)
			{
				if (ipLimbIndex < ipChkLimbIndex)
				{
					if (pLimb->pcHeightFixupNode == pChkLimb->pcHeightFixupNode ||
						pLimb->pcHeightFixupNode == pChkLimb->pcEndEffectorNode)
					{
						ErrorFilenamef(pGroundRegData->pcFileName, "Not allowed to use the same node across multiple limbs (found %s on multiple limbs)", pLimb->pcHeightFixupNode);
						bRet = false;
					}

					if (pLimb->pcEndEffectorNode != pLimb->pcHeightFixupNode &&
						(	pLimb->pcEndEffectorNode == pChkLimb->pcEndEffectorNode ||
						pLimb->pcEndEffectorNode == pChkLimb->pcHeightFixupNode))
					{
						ErrorFilenamef(pGroundRegData->pcFileName, "Not allowed to use the same node across multiple limbs (found %s on multiple limbs)", pLimb->pcEndEffectorNode);
						bRet = false;
					}
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}

	return bRet;
}

bool dynGroundRegDataFixup(DynGroundRegData* pGroundRegData)
{
	char cName[256];
	getFileNameNoExt(cName, pGroundRegData->pcFileName);
	pGroundRegData->pcName = allocAddString(cName);

	//make sure the hyper extension axis is normalize, this will be assumed to be true later on
	FOR_EACH_IN_EARRAY(pGroundRegData->eaLimbs, DynGroundRegDataLimb, pLimb)
	{
		if (normalVec3(pLimb->vHyperExtAxis) > FLT_EPSILON) {
			pLimb->bMinimizeHyperExtension = true;
		} else {
			pLimb->bMinimizeHyperExtension = false;
		}
	}
	FOR_EACH_END;

	return true;
}

static void dynGroundRegDataReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath)) {
		; // File was deleted, do we care here?
	}

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if(!ParserReloadFileToDictionary(relpath,hGroundRegDataDict)) {
		AnimFileError(relpath, "Error reloading DynGroundRegData file: %s", relpath);
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupGroundRegData(DynGroundRegData* pGroundRegData, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynGroundRegDataVerify(pGroundRegData) || !dynGroundRegDataFixup(pGroundRegData))
				return PARSERESULT_INVALID; // remove this
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynGroundRegDataFixup(pGroundRegData))
				return PARSERESULT_INVALID; // remove this
	}
	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerGroundRegDataDict(void)
{
	hGroundRegDataDict = RefSystem_RegisterSelfDefiningDictionary("DynGroundRegData", false, parse_DynGroundRegData, true, false, NULL);
}

void dynGroundRegDataLoadAll(void)
{
	loadstart_printf("Loading DynGroundRegData...");

	// optional for outsource build
	ParserLoadFilesToDictionary("dyn/groundreg", ".groundreg", "DynGroundRegData.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hGroundRegDataDict);

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/groundreg/*.groundreg", dynGroundRegDataReloadCallback);
	}

	loadend_printf("done (%d DynGroundRegDatas)", RefSystem_GetDictionaryNumberOfReferents(hGroundRegDataDict));
}

//////////////////////////////////////////////////////////////////////////////////
//
// Ground Reg 
//
//////////////////////////////////////////////////////////////////////////////////

bool dynGroundRegLimbIsSafe(DynGroundRegLimb *pLimb)
{
	return	pLimb->pEndEffectorNode													&&
			pLimb->pHeightFixupNode													&&
			pLimb->pEndEffectorNode->pParent == pLimb->pHeightFixupNode->pParent	&&
			pLimb->pEndEffectorNode->pParent										&&
			pLimb->pEndEffectorNode->pParent->pParent								&&
			pLimb->pEndEffectorNode->pParent->pParent->pParent;
}

#include "dynGroundReg_h_ast.c"