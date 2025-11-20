#include "dynAnimNodeAuxTransform.h"
#include "dynAnimNodeAuxTransform_h_ast.h"

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

#define DYN_ANIM_NODE_AUX_TRANSFORM_USE_STASH_TABLE_SIZE 0

DictionaryHandle hDynAnimNodeAuxTransformListDict;

// +---------------+
// |               |
// | Load Routines |
// |               |
// +---------------+

static bool dynAnimNodeAuxTransformVerify(DynAnimNodeAuxTransformList *pList, DynAnimNodeAuxTransform *pData)
{
	bool bRet = true;
	//if (bad transform) {
	//	ErrorFilenamef(pList->pcFileName, "You've got a bad transform!\n");
	//	bRet = false;
	//}
	return bRet;
}

static bool dynAnimNodeAuxTransformListVerify(DynAnimNodeAuxTransformList *pList)
{
	bool bRet = true;

	FOR_EACH_IN_EARRAY(pList->eaTransforms, DynAnimNodeAuxTransform, pData) {
		S32 i;
		bRet &= dynAnimNodeAuxTransformVerify(pList, pData);
		for (i = ipDataIndex+1; i < eaSize(&pList->eaTransforms); i++) {
			if (pData->pcNode == pList->eaTransforms[i]->pcNode) {
				ErrorFilenamef(pList->pcFileName, "Found duplicate aux transform : %s!\n", pData->pcNode);
				bRet = false;
				break;
			}
		}
	} FOR_EACH_END;

	return bRet;
}

static bool dynAnimNodeAuxTransformListFixup(DynAnimNodeAuxTransformList *pList)
{
	char cName[256];
	S32 iRotationIndex;
	S32 iScaleIndex;
	S32 iBitFieldIndex;

	getFileNameNoExt(cName, pList->pcFileName);
	pList->pcName = allocAddString(cName);

	ParserFindColumn(parse_DynAnimNodeAuxTransform, "Rotation",				&iRotationIndex	);
	ParserFindColumn(parse_DynAnimNodeAuxTransform, "Scale",				&iScaleIndex	);
	ParserFindColumn(parse_DynAnimNodeAuxTransform, "bfParamsSpecified",	&iBitFieldIndex	);

	stashTableDestroySafe(&pList->stAuxTransforms);
	pList->stAuxTransforms = stashTableCreateWithStringKeys(32, StashDefault);
	FOR_EACH_IN_EARRAY(pList->eaTransforms, DynAnimNodeAuxTransform, pData)
	{
		if (!stashAddPointer(pList->stAuxTransforms, pData->pcNode, pData, false)) {
			assert(0); // duplicates should have been found during the verify pass
		}

		if (!TokenIsSpecified(parse_DynAnimNodeAuxTransform, iRotationIndex, pData, iBitFieldIndex)) {
			unitQuat(pData->qRot); // when the artist haven't set a rotation, set it to {0,0,0,-1}
		}

		if (!TokenIsSpecified(parse_DynAnimNodeAuxTransform, iScaleIndex, pData, iBitFieldIndex)) {
			unitVec3(pData->vScale); // when the artist haven't set a scale, set it to {1,1,1}
		}

		copyQuat(pData->qRot, pData->xForm.qRot);
		copyVec3(pData->vPos, pData->xForm.vPos);
		copyVec3(pData->vScale, pData->xForm.vScale);
	}
	FOR_EACH_END;

	return true;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDynAnimNodeAuxTransformList(DynAnimNodeAuxTransformList *pList, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynAnimNodeAuxTransformListVerify(pList) || !dynAnimNodeAuxTransformListFixup(pList))
				return PARSERESULT_INVALID; // remove entry
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynAnimNodeAuxTransformListFixup(pList))
				return PARSERESULT_INVALID; // remove entry
	}
	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerDynAnimNodeAuxTransformListDict(void)
{
	hDynAnimNodeAuxTransformListDict = RefSystem_RegisterSelfDefiningDictionary("DynAnimNodeAuxTransformList", false, parse_DynAnimNodeAuxTransformList, true, false, NULL);
}

static void dynAnimNodeAuxTransformListReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath)) {
		; // File was deleted, do we care here?
	}

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if(!ParserReloadFileToDictionary(relpath, hDynAnimNodeAuxTransformListDict)) {
		CharacterFileError(relpath, "Error reloading DynAnimNodeAuxTransformList file: %s", relpath);
	}
}

void dynAnimNodeAuxTransformLoadAll(void)
{
	loadstart_printf("Loading DynAnimNodeAuxTransformLists...");
	ParserLoadFilesToDictionary("dyn/AnimNodeAuxTransforms", ".auxform", "DynAnimNodeAuxTransforms.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hDynAnimNodeAuxTransformListDict);
	if(isDevelopmentMode()) {
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/AnimNodeAuxTransforms/*.auxform", dynAnimNodeAuxTransformListReloadCallback);
	}
	loadend_printf("done (%d DyAnimNodeAuxTransformLists)", RefSystem_GetDictionaryNumberOfReferents(hDynAnimNodeAuxTransformListDict));
}

// +---------+
// | Lookups |
// +---------+

const DynTransform *dynAnimNodeAuxTransform(const DynAnimNodeAuxTransformList *pList, const char *pcName)
{
	if (pList && pcName)
	{
		const char *pcPooledName = allocFindString(pcName);
		if (eaSize(&pList->eaTransforms) < DYN_ANIM_NODE_AUX_TRANSFORM_USE_STASH_TABLE_SIZE)
		{
			FOR_EACH_IN_EARRAY(pList->eaTransforms, DynAnimNodeAuxTransform, pData) {
				if (pData->pcNode == pcPooledName) {
					return &pData->xForm;
				}
			} FOR_EACH_END;
		}
		else
		{
			StashElement element;
			if (stashFindElementConst(pList->stAuxTransforms, pcPooledName, &element)) {
				DynAnimNodeAuxTransform *pData = stashElementGetPointer(element);
				return &pData->xForm;
			}
		}
	}

	return NULL;
}

// +--------------+
// | AST includes |
// +--------------+

#include "dynAnimNodeAuxTransform_h_ast.c"