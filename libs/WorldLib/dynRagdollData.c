
#include "dynRagdollData.h"

#include "fileutil.h"
#include "FolderCache.h"
#include "error.h"
#include "StringCache.h"
#include "quat.h"

#include "dynSeqData.h"
#include "dynFxManager.h"

#include "dynRagdollData_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););


AUTO_CMD_INT(dynDebugState.bDrawRagdollDataGfx, danimDrawRagdollDataGfx) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bDrawRagdollDataAnim, danimDrawRagdollDataAnim) ACMD_CATEGORY(dynAnimation);

//////////////////////////////////////////////////////////////////////////////////
//
// Ragdoll Data 
//
//////////////////////////////////////////////////////////////////////////////////
DictionaryHandle hRagdollDataDict;

bool dynRagdollDataVerify(DynRagdollData* pData)
{
	U32 uiNumRoots = 0;
	FOR_EACH_IN_EARRAY(pData->eaShapes, DynRagdollShape, pShape)
	{
		quatNormalize(pShape->qRotation);
		if (!pShape->pcParentBone)
		{
			if (++uiNumRoots > 1)
			{
				ErrorFilenamef(pData->pcFileName, "Too many shapes in the ragdoll data have no parent. All but one must have ParentBone specified.");
				return false;
			}
			++uiNumRoots;
		}
		else
		{
			bool bFoundBone = false;
			if (pShape->pcParentBone == pShape->pcBone)
			{
				ErrorFilenamef(pData->pcFileName, "Can't parent ragdoll bone %s to itself!", pShape->pcBone);
				return false;
			}
			FOR_EACH_IN_EARRAY(pData->eaShapes, DynRagdollShape, pOtherShape)
			{
				if (pOtherShape->pcBone == pShape->pcParentBone)
				{
					// Found a match
					bFoundBone = true;
					break;
				}
			}
			FOR_EACH_END;
			if (!bFoundBone)
			{
				ErrorFilenamef(pData->pcFileName, "Unable to find ParentBone %s. This bone needs to be a ragdoll bone in this same file.", pShape->pcParentBone);
				return false;
			}
		}
	}
	FOR_EACH_END;
	if (uiNumRoots == 0)
	{
		ErrorFilenamef(pData->pcFileName, "At least one shape in the ragdoll data must have no parent bone (be the root).");
		return false;
	}

	return true;
}

bool dynRagdollDataFixup(DynRagdollData* pData)
{
	{
		char cName[256];
		getFileNameNoExt(cName, pData->pcFileName);
		pData->pcName = allocAddString(cName);
	}
	FOR_EACH_IN_EARRAY(pData->eaShapes, DynRagdollShape, pShape)
	{
		quatNormalize(pShape->qRotation);
		
		pShape->iParentIndex = -1;
		pShape->iNumChildren = 0;
		FOR_EACH_IN_EARRAY(pData->eaShapes, DynRagdollShape, pOtherShape)
		{
			if (pOtherShape->pcBone == pShape->pcParentBone) {
				pShape->iParentIndex = ipOtherShapeIndex;
			}
			else if (pOtherShape->pcParentBone == pShape->pcBone) {
				pShape->iNumChildren++;
			}
		}
		FOR_EACH_END;

		if (pShape->fDensity <= 0.0f)
			pShape->fDensity = 1.0f;
	}
	FOR_EACH_END;

	// color the torso and corresponding limbs based on connectivity
	FOR_EACH_IN_EARRAY(pData->eaShapes, DynRagdollShape, pShape)
	{
		if (pShape->iParentIndex == -1)
		{
			pShape->bTorsoBone = true;
		}
		else if (1 < pShape->iNumChildren)
		{
			DynRagdollShape* pLooper = pShape;

			while (pLooper->iParentIndex != -1)
			{
				pLooper->bTorsoBone = true;
				pLooper = pData->eaShapes[pLooper->iParentIndex];
			}

			pLooper->bTorsoBone = true;
		}
		else
		{
			pShape->bTorsoBone = false;
		}
	}
	FOR_EACH_END;

	return true;
}

static void dynRagdollDataReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);


	if(!ParserReloadFileToDictionary(relpath,hRagdollDataDict))
	{
		CharacterFileError(relpath, "Error reloading DynRagdollData file: %s", relpath);
	}
	else
	{
		// nothing to do here
	}
	//costumeForceGlobalReload();
}

AUTO_FIXUPFUNC;
TextParserResult fixupRagdollData(DynRagdollData* pRagdollData, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynRagdollDataVerify(pRagdollData) || !dynRagdollDataFixup(pRagdollData))
				return PARSERESULT_INVALID; // remove this from the costume list
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynRagdollDataFixup(pRagdollData))
				return PARSERESULT_INVALID; // remove this from the costume list
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerRagdollDataDict(void)
{
	hRagdollDataDict = RefSystem_RegisterSelfDefiningDictionary("DynRagdollData", false, parse_DynRagdollData, true, false, NULL);
}

void dynRagdollDataLoadAll(void)
{
	loadstart_printf("Loading DynRagdollData...");

	// optional for outsource build
	ParserLoadFilesToDictionary("dyn/ragdoll", ".rag", "DynRagdollData.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hRagdollDataDict);

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/ragdoll/*.rag", dynRagdollDataReloadCallback);
	}

	loadend_printf("done (%d DynRagdollDatas)", RefSystem_GetDictionaryNumberOfReferents(hRagdollDataDict) );
}

#include "dynRagdollData_h_ast.c"