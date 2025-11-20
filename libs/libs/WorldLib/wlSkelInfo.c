
#include "wlSkelInfo.h"

// UtilsLib
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "structInternals.h"
#include "StringCache.h"
#include "mathutil.h"


#include "dynAnimChart.h"
#include "dynAnimTrack.h"
#include "dynSkeleton.h"
#include "dynDraw.h"
#include "dynSeqData.h"
#include "wlCostume.h"



#include "autogen/wlSkelInfo_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

DictionaryHandle hSkelInfoDict;
DictionaryHandle hScaleInfoDict;
DictionaryHandle hBlendInfoDict;
DictionaryHandle hHeadshotInfoDict;

bool bLoadedSkelInfos = false;

static bool verifyScaleVec(Vec3 vScale, bool bMin)
{
	int i;
	bool bResult = true;
	for (i=0; i<3; ++i)
	{
		if (vScale[i] == 0.0f)
			vScale[i] = 1.0f;
		else if (vScale[i] < 0.0f)
		{
			bResult = false;
			vScale[i] = 1.0f;
		}
		if (bMin && vScale[i] > 1.0f)
		{
			vScale[i] = 1.0f;
			bResult = false;
		}
		else if (!bMin && vScale[i] < 1.0f)
		{
			vScale[i] = 1.0f;
			bResult = false;
		}
	}
	return bResult;
}


static bool verifySkelScaleGroupForSkelInfo(SkelScaleGroup* pGroup, SkelScaleInfo* pScaleInfo, SkelInfo* pSkelInfo)
{
	const DynBaseSkeleton *pBaseSkeleton;
	if (!(pBaseSkeleton = GET_REF(pSkelInfo->hBaseSkeleton)))
		return false;
	FOR_EACH_IN_EARRAY(pGroup->eaBone, SkelScaleBone, pBone)
	{
		// Verify bone name exists
		if (!dynBaseSkeletonFindNode(pBaseSkeleton, pBone->pcBone))
		{
			CharacterFileError(pSkelInfo->pcFileName, "SkelInfo %s: Unable to find bone %s referenced in ScaleInfo %s", pSkelInfo->pcSkelInfoName, pBone->pcBone, pScaleInfo->pcScaleInfoName);
		}


	}
	FOR_EACH_END;
	return true;
}

static bool verifySkelScaleGroup(SkelScaleGroup* pGroup, const char* pcFileName)
{
	FOR_EACH_IN_EARRAY(pGroup->eaBone, SkelScaleBone, pBone)
	{
		if (!pBone->bTranslation)
		{
			if (
				!verifyScaleVec(pBone->vLargeMax, false)
				|| !verifyScaleVec(pBone->vLargeMin, true)
				|| !verifyScaleVec(pBone->vSmallMax, false)
				|| !verifyScaleVec(pBone->vSmallMin, true)
				)
			{
				CharacterFileErrorAndInvalidate(pcFileName, "Bone %s: Scale must be positive!", pBone->pcBone);
				return false;
			}
		}
	}
	FOR_EACH_END;
	return true;
}
#define SCALE_TRANSLATION_VECTOR(vec, frac) { scaleVec3(vec, frac, vec); }

#define SCALE_SCALING_VECTOR(vec, frac) { Vec3 vTemp; subVec3(vec, onevec3, vTemp); scaleVec3(vTemp, frac, vTemp); addVec3(vTemp, onevec3, vec); }

static bool skelScaleGroupMoveIncludesToBones(SkelScaleGroup* pGroup, SkelScaleInfo* pScaleInfo, U32 uiDepth)
{
	if (uiDepth > 20)
	{
		CharacterFileErrorAndInvalidate(pScaleInfo->pcFileName, "Exceeded max Group Include depth of 20, check for cycles in include graph!");
		return false;
	}
	FOR_EACH_IN_EARRAY(pGroup->eaGroup, SkelScaleGroupInclude, pInclude)
		// Find corresponding group
		if (pInclude->fFraction <= 0.0f || pInclude->fFraction > 1.0f)
		{
			CharacterFileErrorAndInvalidate(pScaleInfo->pcFileName, "Got Group Include %s Fraction not between 0 and 1: %.2f in ScaleGroup %s", pInclude->pcGroup, pInclude->fFraction, pGroup->pcGroupName);
			return false;
		}
		FOR_EACH_IN_EARRAY(pScaleInfo->eaScaleGroup, SkelScaleGroup, pOtherGroup)
			if (pOtherGroup->pcGroupName == pInclude->pcGroup)
			{
				// Make sure that the bones are grouped up in the othergroup
				if (eaSize(&pOtherGroup->eaGroup) > 0)
				{
					if (!skelScaleGroupMoveIncludesToBones(pOtherGroup, pScaleInfo, uiDepth+1))
						return false;
				}
				FOR_EACH_IN_EARRAY(pOtherGroup->eaBone, SkelScaleBone, pBone)
					SkelScaleBone* pNewBone = StructClone(parse_SkelScaleBone, pBone);
					if (pNewBone)
					{
						if (pNewBone->bTranslation)
						{
							SCALE_TRANSLATION_VECTOR(pNewBone->vLargeMax, pInclude->fFraction);
							SCALE_TRANSLATION_VECTOR(pNewBone->vLargeMin, pInclude->fFraction);
							SCALE_TRANSLATION_VECTOR(pNewBone->vSmallMax, pInclude->fFraction);
							SCALE_TRANSLATION_VECTOR(pNewBone->vSmallMin, pInclude->fFraction);
						}
						else
						{
							SCALE_SCALING_VECTOR(pNewBone->vLargeMax, pInclude->fFraction);
							SCALE_SCALING_VECTOR(pNewBone->vLargeMin, pInclude->fFraction);
							SCALE_SCALING_VECTOR(pNewBone->vSmallMax, pInclude->fFraction);
							SCALE_SCALING_VECTOR(pNewBone->vSmallMin, pInclude->fFraction);
						}
						eaPush(&pGroup->eaBone, pNewBone);
					}
				FOR_EACH_END;
				break;
			}
		FOR_EACH_END;
	FOR_EACH_END;
	eaDestroyStruct(&pGroup->eaGroup, parse_SkelScaleGroupInclude);

	return true;
}

static bool verifySkelScaleInfo(SkelScaleInfo* pScaleInfo)
{
	{
		char cName[256];
		getFileNameNoExt(cName, pScaleInfo->pcFileName);
		pScaleInfo->pcScaleInfoName = allocAddString(cName);
	}
	// first, convert all scale group includes into new bones
	FOR_EACH_IN_EARRAY(pScaleInfo->eaScaleGroup, SkelScaleGroup, pGroup)
		skelScaleGroupMoveIncludesToBones(pGroup, pScaleInfo, 0);
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pScaleInfo->eaScaleGroup, SkelScaleGroup, pGroup)
		if (!verifySkelScaleGroup(pGroup, pScaleInfo->pcFileName))
			return false;
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS(pScaleInfo->eaScaleAnimTrack, SkelScaleAnimTrack, pSkelScaleAnimTrack)
	{
		DynAnimTrackHeader* pAnimTrackHeader = dynAnimTrackHeaderFind(pSkelScaleAnimTrack->pcScaleAnimFile);
		if (pAnimTrackHeader)
		{
			pSkelScaleAnimTrack->pScaleTrackHeader = pAnimTrackHeader;
			dynAnimTrackHeaderIncrementPermanentRefCount(pSkelScaleAnimTrack->pScaleTrackHeader);
			if (!pSkelScaleAnimTrack->pScaleTrackHeader->bLoaded)
				dynAnimTrackHeaderForceLoadTrack(pSkelScaleAnimTrack->pScaleTrackHeader);
		}
		else
		{
			CharacterFileErrorAndInvalidate(pScaleInfo->pcFileName, "Unable to find ScaleAnimTrack %s", pSkelScaleAnimTrack->pcScaleAnimFile);
			return false;
		}
	}
	FOR_EACH_END;
	return true;
}

static bool verifyRunAndGunBones(SkelBlendInfo *pBlendInfo)
{
	bool foundPrimary = false;
	bool bRet = true;

	FOR_EACH_IN_EARRAY(pBlendInfo->eaRunAndGunBone, SkelBlendRunAndGunBone, pBone) {
		if (!pBone->bSecondary) {
			if (foundPrimary) {
				CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "Only one run'n'gun bone can be made primary by NOT applying the secondary tag");
				bRet = false;
			} else {
				foundPrimary = true;
			}
		}
		if (360.0 < pBone->fLimitAngle || pBone->fLimitAngle < 0) {
			CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "Run'n'gun bone limit angles must be between 0 and 360");
			bRet = false;
		}
		FOR_EACH_IN_EARRAY(pBlendInfo->eaRunAndGunBone, SkelBlendRunAndGunBone, pChkBone) {
			if (pBone != pChkBone && pBone->fLimitAngle == pChkBone->fLimitAngle) {
				CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "Run'n'gun bone limit angles must be unique");
				bRet = false;
			}
		} FOR_EACH_END;
	} FOR_EACH_END;

	if (!foundPrimary) {
		CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "One run'n'gun bone must be made primary by NOT applying the secondary tag");
		bRet = false;
	}

	return bRet;
}

static bool verifySkelBlendInfo(SkelBlendInfo* pBlendInfo)
{
	bool bFoundOverlay = false;
	U32 iSeqCount = 0, iSubOverlayCount = 0;
	{
		char cName[256];
		getFileNameNoExt(cName, pBlendInfo->pcFileName);
		pBlendInfo->pcBlendInfoName = allocAddString(cName);
	}
	if (!pBlendInfo->pcMainSequencer)
		CharacterFileError(pBlendInfo->pcFileName, "Blend info must have a MainSequencer specified!");

	if (!gConf.bNewAnimationSystem)
	{
		if (!dynSeqDataCollectionFromName(pBlendInfo->pcMainSequencer))
			CharacterFileError(pBlendInfo->pcFileName, "Can't find MainSequencer %s!", pBlendInfo->pcMainSequencer);
	}
	else
	{
		DynAnimChartRunTime* pDefaultChart = GET_REF(pBlendInfo->hDefaultChart);
		DynAnimChartRunTime* pMountedChart = GET_REF(pBlendInfo->hMountedChart);

		if (!pDefaultChart)
		{
			CharacterFileError(pBlendInfo->pcFileName, "Blend info must specify a DefaultChart");
			return false;
		}
		else if(!gConf.bUseMovementGraphs &&
				!GET_REF(pDefaultChart->hMovementSet))
		{
			CharacterFileError(pBlendInfo->pcFileName, "Default chart %s must specify a MovementSet", pDefaultChart->pcName);
			return false;
		}

		if (!gConf.bUseMovementGraphs	&&
			pMountedChart				&&
			!GET_REF(pMountedChart->hMovementSet))
		{
			CharacterFileError(pBlendInfo->pcFileName, "Mounted chart %s must specify a MovementSet", pMountedChart->pcName);
			return false;
		}
	}

	pBlendInfo->stBoneSequencerInfo = stashTableCreateWithStringKeys(8, StashDefault);
	pBlendInfo->stBoneOverlayInfo   = stashTableCreateWithStringKeys(8, StashDefault);

	FOR_EACH_IN_EARRAY_FORWARDS(pBlendInfo->eaSequencer, SkelBlendSeqInfo, pSeqInfo)
	{
		if (!pSeqInfo->pcSeqName || (!gConf.bNewAnimationSystem && !dynSeqDataCollectionFromName(pSeqInfo->pcSeqName)))
		{
			CharacterFileError(pBlendInfo->pcFileName, "Can't find Sequencer %s!", pSeqInfo->pcSeqName);
		}
		if (pSeqInfo->bSubOverlay && pSeqInfo->bOverlay)
		{
			CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "Sequencer %s cannot be both a sub-overlay and overlay, choose a single type", pSeqInfo->pcSeqName);
			return false;
		}
		if (pSeqInfo->bOverlay)
		{
			if (pSeqInfo->bIgnoreMasterOverlay)
			{
				CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "The master overlay can't ignore itself.");
				return false;
			}
			if (bFoundOverlay)
			{
				CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "Only one Overlay sequencer per blend info is allowed.");
				return false;
			}
			bFoundOverlay = true;
		}
		if (!GET_REF(pSeqInfo->hChart) && (pSeqInfo->bSubOverlay || (gConf.bNewAnimationSystem && pSeqInfo->bOverlay)))
		{
			//Alertf("Warning: Can't find chart %s for sub-overlay sequencer %s, will use default chart",
			//printf("Warning: Can't find chart %s for sub-overlay sequencer %s, will use default chart",
			verbose_printf("WARNING: Can't find chart %s for sub-overlay sequencer %s, will use default chart",
				REF_HANDLE_GET_STRING(pSeqInfo->hChart),
				pSeqInfo->pcSeqName
				);
			//return false;
		}
		FOR_EACH_IN_EARRAY_FORWARDS(pSeqInfo->eaBoneName, const char, pcBoneName)
		{
			if (!pSeqInfo->bOverlay && !pSeqInfo->bSubOverlay)
			{
				if (!stashAddInt(pBlendInfo->stBoneSequencerInfo, pcBoneName, ipSeqInfoIndex+1, false))
				{
					CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "Bone %s found in two non-overlay sequencers. This is not allowed.", pcBoneName);
					return false;
				}
			}

			if (pSeqInfo->bSubOverlay)
			{
				if (!stashAddInt(pBlendInfo->stBoneOverlayInfo, pcBoneName, ipSeqInfoIndex+1, false))
				{
					CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "Bone %s found in two sub-overlay sequencers. This is not allowed.", pcBoneName);
					return false;
				}
			}

			if (pSeqInfo->bOverlay)
			{
				CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "Bone %s found in master overlay. This is not allowed.", pcBoneName);
				return false;
			}
		}
		FOR_EACH_END;
		if (!pSeqInfo->bSubOverlay && !pSeqInfo->bOverlay) iSeqCount++;
		if (pSeqInfo->bSubOverlay) iSubOverlayCount++;
	}
	FOR_EACH_END;

	if (iSeqCount > 4)
	{
		CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "Can't have more than 4 sequencers in a blend info file!");
		return false;
	}

	if (iSubOverlayCount > 4)
	{
		CharacterFileErrorAndInvalidate(pBlendInfo->pcFileName, "Can't have more than 4 sub-overlay sequencers in a blend info file!");
		return false;
	}

	return true;
}

static bool verifySkelHeadshotInfo(SkelHeadshotInfo* pHeadshotInfo)
{
	{
		char cName[256];
		getFileNameNoExt(cName, pHeadshotInfo->pcFileName);
		pHeadshotInfo->pcHeadshotInfoName = allocAddString(cName);
	}
	return true;
}


static bool verifySkelInfo(SkelInfo* pSkelInfo, TextParserState *pTextParserState)
{
	SkelScaleInfo* pScaleInfo = GET_REF(pSkelInfo->hScaleInfo);
	const DynBaseSkeleton *pBaseSkeleton;
	if (!(pBaseSkeleton = GET_REF(pSkelInfo->hBaseSkeleton)))
	{
		CharacterFileErrorAndInvalidate(pSkelInfo->pcFileName, "SkelInfo %s: Unable to find skeleton %s", pSkelInfo->pcSkelInfoName, REF_STRING_FROM_HANDLE(pSkelInfo->hBaseSkeleton));
		return false;
	}

	if (pScaleInfo)
	{
		FOR_EACH_IN_EARRAY(pScaleInfo->eaScaleGroup, SkelScaleGroup, pGroup)
			if (!verifySkelScaleGroupForSkelInfo(pGroup, pScaleInfo, pSkelInfo))
			{
				// nothing for now
			}
		FOR_EACH_END;
	}

	if (pSkelInfo->bodySockInfo.pcBodySockGeo)
	{
		if ( pSkelInfo->bodySockInfo.vBodySockMin[0] >= pSkelInfo->bodySockInfo.vBodySockMax[0] || pSkelInfo->bodySockInfo.vBodySockMin[1] >= pSkelInfo->bodySockInfo.vBodySockMax[1] || pSkelInfo->bodySockInfo.vBodySockMin[2] >= pSkelInfo->bodySockInfo.vBodySockMax[2])
		{
			CharacterFileErrorAndInvalidate(pSkelInfo->pcFileName, "Skel_Info %s BodySockMax must be greater than BodySockMin in all 3 dimensions", pSkelInfo->pcSkelInfoName);
			return false;
		}
	}

	if (gConf.bNewAnimationSystem)
	{
		if (!GET_REF(pSkelInfo->hBlendInfo))
		{
			DynAnimChartRunTime *pDefaultChart = GET_REF(pSkelInfo->hDefaultChart);
			DynAnimChartRunTime *pMountedChart = GET_REF(pSkelInfo->hMountedChart);

			if (!pDefaultChart)
			{
				CharacterFileErrorAndInvalidate(pSkelInfo->pcFileName, "Skel_Info %s does not set a blendinfo or default chart", pSkelInfo->pcSkelInfoName);
				return false;
			}
			else if(!gConf.bUseMovementGraphs &&
					!GET_REF(pDefaultChart->hMovementSet))
			{
				CharacterFileErrorAndInvalidate(pSkelInfo->pcFileName, "Skel_Info %s default chart %s does not have a movement set", pSkelInfo->pcSkelInfoName, pDefaultChart->pcName);
				return false;
			}

			if (!gConf.bUseMovementGraphs	&&
				pMountedChart				&&
				!GET_REF(pMountedChart->hMovementSet))
			{
				CharacterFileErrorAndInvalidate(pSkelInfo->pcFileName, "Skel_Info %s mounted chart %s does not have a movement set", pSkelInfo->pcSkelInfoName, pMountedChart->pcName);
				return false;
			}
		}
	}


	
	if (pTextParserState && pTextParserState->parselist)
		FileListInsert(&pTextParserState->parselist, pBaseSkeleton->pcFileName, 0);
	return true;
}

static bool skelInfoFixup(SkelInfo* pSkelInfo)
{
	if (pSkelInfo->bodySockInfo.pcBodySockGeo)
	{
		if (!dynAnimTrackHeaderFind(pSkelInfo->bodySockInfo.pcBodySockPose))
		{
			CharacterFileErrorAndInvalidate(pSkelInfo->pcFileName, "Unable to find animation .atrk %s", pSkelInfo->bodySockInfo.pcBodySockPose);
			return false;
		}
	}
	return true;
}

static void skelInfoReloadCallback(const char *relpath, int when)
{

	loadstart_printf("Reloading SkelInfos...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	if(!ParserReloadFileToDictionary(relpath,hSkelInfoDict))
	{
		ErrorFilenamef(relpath, "Error reloading skelInfo file: %s", relpath);
	}

	loadend_printf("done");
}

static void scaleInfoReloadCallback(const char *relpath, int when)
{

	loadstart_printf("Reloading ScaleInfos...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	if(!ParserReloadFileToDictionary(relpath,hScaleInfoDict))
	{
		ErrorFilenamef(relpath, "Error reloading .scale file: %s", relpath);
	}

	loadend_printf("done");
}

static void blendInfoReloadCallback(const char *relpath, int when)
{

	loadstart_printf("Reloading BlendInfos...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	if(!ParserReloadFileToDictionary(relpath,hBlendInfoDict))
	{
		ErrorFilenamef(relpath, "Error reloading .blend file: %s", relpath);
	}

	loadend_printf("done");
}

static void headshotInfoReloadCallback(const char *relpath, int when)
{

	loadstart_printf("Reloading HeadShotInfos...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	if(!ParserReloadFileToDictionary(relpath,hHeadshotInfoDict))
	{
		ErrorFilenamef(relpath, "Error reloading .headshot file: %s", relpath);
	}

	loadend_printf("done");
}

static void wlSkelInfoReloadAllUsingScaleInfo(const char* pcScaleInfo)
{
	RefDictIterator iterator;
	SkelInfo* pSkelInfo;

	RefSystem_InitRefDictIterator(hSkelInfoDict, &iterator);
	while ((pSkelInfo = RefSystem_GetNextReferentFromIterator(&iterator)))
	{
		if (IS_HANDLE_ACTIVE(pSkelInfo->hScaleInfo) && stricmp(REF_STRING_FROM_HANDLE(pSkelInfo->hScaleInfo), pcScaleInfo)==0)
		{
			// Note 10/29/2007: Costumes are not file based any more so if you really need
			//                  a change, you need to ask entities to regenerate their costumes
			//wlCostumeReloadAllUsingSkelInfo(pSkelInfo, RESEVENT_RESOURCE_MODIFIED);
		}
	}
}

static int skelInfoValidate(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	SkelInfo *pSkelInfo = pResource;
	switch (eType)
	{	
		xcase RESVALIDATE_POST_BINNING:
			if (pSkelInfo->bodySockInfo.pcBodySockPose)
			{
				if (!dynAnimTrackHeaderFind(pSkelInfo->bodySockInfo.pcBodySockPose))
				{
					CharacterFileErrorAndInvalidate(pSkelInfo->pcFileName, "Unable to find animation .atrk %s", pSkelInfo->bodySockInfo.pcBodySockPose);
				}
			}

			return VALIDATE_HANDLED;
	}


	return VALIDATE_NOT_HANDLED;
}

AUTO_FIXUPFUNC;
TextParserResult fixupSkelInfo(SkelInfo* pSkelInfo, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			if (!verifySkelInfo(pSkelInfo, (TextParserState*)pExtraData))
			{
				return PARSERESULT_INVALID;
			}
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupSkelScaleInfo(SkelScaleInfo* pScaleInfo, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			if (!verifySkelScaleInfo(pScaleInfo))
			{
				return PARSERESULT_INVALID;
			}
		}
		xcase FIXUPTYPE_POST_BIN_READ:
		{
			FOR_EACH_IN_EARRAY(pScaleInfo->eaScaleAnimTrack, SkelScaleAnimTrack, pScaleAnimTrack)
			{
				pScaleAnimTrack->pScaleTrackHeader = dynAnimTrackHeaderFind(pScaleAnimTrack->pcScaleAnimFile);
				if (pScaleAnimTrack->pScaleTrackHeader)
				{
					dynAnimTrackHeaderIncrementPermanentRefCount(pScaleAnimTrack->pScaleTrackHeader);
					if (!pScaleAnimTrack->pScaleTrackHeader->bLoaded)
						dynAnimTrackHeaderForceLoadTrack(pScaleAnimTrack->pScaleTrackHeader);
				}
			}
			FOR_EACH_END;
		}
		xcase FIXUPTYPE_POST_RELOAD:
		{
			wlSkelInfoReloadAllUsingScaleInfo(pScaleInfo->pcScaleInfoName);
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupSkelBlendInfo(SkelBlendInfo* pBlendInfo, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_BIN_READ:
		case FIXUPTYPE_POST_TEXT_READ:
		{
			if (!verifySkelBlendInfo(pBlendInfo))
			{
				return PARSERESULT_INVALID;
			}
			if (eaSize(&pBlendInfo->eaRunAndGunBone) && !verifyRunAndGunBones(pBlendInfo))
			{
				return PARSERESULT_INVALID;
			}
		}
		/*
		xcase FIXUPTYPE_POST_RELOAD:
		{
			wlSkelInfoReloadAllUsingBlendInfo(pBlendInfo->pcBlendInfoName);
		}
		*/
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupSkelHeadshotInfo(SkelHeadshotInfo* pHeadshotInfo, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_BIN_READ:
		case FIXUPTYPE_POST_TEXT_READ:
		{
			if (!verifySkelHeadshotInfo(pHeadshotInfo))
			{
				return PARSERESULT_INVALID;
			}
		}
		/*
		xcase FIXUPTYPE_POST_RELOAD:
		{
			wlSkelInfoReloadAllUsingHeadshotInfo(pHeadshotInfo->pcHeadshotInfoName);
		}
		*/
	}

	return PARSERESULT_SUCCESS;
}



static void wlSkelInfoReloadCallback(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED || eType == RESEVENT_RESOURCE_REMOVED)
	{
		// Note 10/29/2007: Costumes in the dictionary are not file based any more
		//                  This may need to instead ask entities to regenerate their costumes
		// Make sure to tell costumes to reload, which should tell sequencers to reload
		//wlCostumeReloadAllUsingSkelInfo((const SkelInfo*)pReferent, eType);
	}
}

AUTO_RUN;
void registerSkelInfoDictionary(void)
{
	hSkelInfoDict = RefSystem_RegisterSelfDefiningDictionary("SkelInfo", false, parse_SkelInfo, true, true, NULL);
	resDictManageValidation(hSkelInfoDict, skelInfoValidate);
	hScaleInfoDict = RefSystem_RegisterSelfDefiningDictionary("ScaleInfo", false, parse_SkelScaleInfo, true, false, NULL);
	hBlendInfoDict = RefSystem_RegisterSelfDefiningDictionary("BlendInfo", false, parse_SkelBlendInfo, true, false, NULL);
	hHeadshotInfoDict = RefSystem_RegisterSelfDefiningDictionary("HeadshotInfo", false, parse_SkelHeadshotInfo, true, false, NULL);
}

void wlSkelInfoLoadAll()
{
	loadstart_printf("Loading SkelInfos...");

	ParserLoadFilesToDictionary("defs/skel_infos", ".scale", "ScaleInfos.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, hScaleInfoDict);
	ParserLoadFilesToDictionary("defs/skel_infos", ".blend", "BlendInfos.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, hBlendInfoDict);
	ParserLoadFilesToDictionary("defs/skel_infos", ".headshot", "HeadshotInfos.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, hHeadshotInfoDict);
	/*	
	ParserLoadFilesSharedToDictionary("SM_SkelInfo", "defs/skel_infos", ".skif", "SkelInfos.bin", PARSER_BINS_ARE_SHARED, hSkelInfoDict);
	*/
	// Do not share (until some shared memory bugs are resolved)
	ParserLoadFilesToDictionary("defs/skel_infos", ".skif", "SkelInfos.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, hSkelInfoDict);


	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/skel_infos/*.skif", skelInfoReloadCallback);
		resDictRegisterEventCallback(hSkelInfoDict, wlSkelInfoReloadCallback, NULL);

		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/skel_infos/*.scale", scaleInfoReloadCallback);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/skel_infos/*.blend", blendInfoReloadCallback);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/skel_infos/*.headshot", headshotInfoReloadCallback);
	}
	bLoadedSkelInfos = true;

	loadend_printf("done (%d ScaleInfos) (%d SkelInfos) (%d BlendInfos) (%d HeadshotInfos)", RefSystem_GetDictionaryNumberOfReferents(hScaleInfoDict), RefSystem_GetDictionaryNumberOfReferents(hSkelInfoDict), RefSystem_GetDictionaryNumberOfReferents(hBlendInfoDict), RefSystem_GetDictionaryNumberOfReferents(hHeadshotInfoDict));
}

bool wlSkelInfoFindSeqTypeRank(const SkelInfo* pSkelInfo, const char* pcSeqType, U32* uiRank)
{
	U32 uiNumSeqTypes = eaSize(&pSkelInfo->eapcSeqType);
	U32 uiSeqTypeIndex;
	for (uiSeqTypeIndex=0; uiSeqTypeIndex<uiNumSeqTypes; ++uiSeqTypeIndex)
	{
		if ( pcSeqType == pSkelInfo->eapcSeqType[uiSeqTypeIndex] )
		{
			if ( uiRank )
				*uiRank = uiSeqTypeIndex;
			return true;
		}
	}

	return false;

}


F32 zeroCenterInterpF32(F32 fInterpValue, const F32 fMin, const F32 fMax, const F32 fCenterValue)
{
	if (fInterpValue == 0.0f)
		return fCenterValue;
	else if (fInterpValue < 0.0f)
		return interpF32(fInterpValue + 1.0f, fMin, fCenterValue);
	else // > 0.0
		return interpF32(fInterpValue, fCenterValue, fMax);
}

static void wlSkelScaleGroupGetBoneScales(const SkelScaleGroup* pGroup, ScaleValue* pScaleValue, F32 fInterpValue, StashTable stBoneScaleTable)
{
	FOR_EACH_IN_EARRAY(pGroup->eaBone, SkelScaleBone, pScaleBone)
	{
		Vec3 vMin, vMax, vResult;
		CalcBoneScale* pCalcBoneScale;
		int i;
		interpVec3(fInterpValue, pScaleBone->vSmallMin, pScaleBone->vLargeMin, vMin);
		interpVec3(fInterpValue, pScaleBone->vSmallMax, pScaleBone->vLargeMax, vMax);
		for (i=0; i<3; ++i)
			vResult[i] = zeroCenterInterpF32(pScaleValue->vScaleInputs[i], vMin[i], vMax[i], pScaleBone->bTranslation?0.0f:1.0f);
		// Look up bone in bone stash  table, store the result
		if (stashFindPointer(stBoneScaleTable, pScaleBone->pcBone, &pCalcBoneScale))
		{
			// Found it already
			if (pScaleBone->bTranslation)
			{
				addVec3(pCalcBoneScale->vTrans, vResult, pCalcBoneScale->vTrans);
			}
			else
			{
				if (pScaleBone->bUniversal)
					mulVecVec3(pCalcBoneScale->vUniversalScale, vResult, pCalcBoneScale->vUniversalScale);
				else
					mulVecVec3(pCalcBoneScale->vScale, vResult, pCalcBoneScale->vScale);
			}
		}
		else
		{
			pCalcBoneScale = malloc(sizeof(CalcBoneScale));
			if (pScaleBone->bTranslation)
			{
				copyVec3(onevec3, pCalcBoneScale->vScale);
				copyVec3(onevec3, pCalcBoneScale->vUniversalScale);

				copyVec3(vResult, pCalcBoneScale->vTrans);
			}
			else
			{
				zeroVec3(pCalcBoneScale->vTrans);
				if (pScaleBone->bUniversal)
				{
					copyVec3(vResult, pCalcBoneScale->vUniversalScale);
					copyVec3(onevec3, pCalcBoneScale->vScale);
				}
				else
				{
					copyVec3(vResult, pCalcBoneScale->vScale);
					copyVec3(onevec3, pCalcBoneScale->vUniversalScale);
				}
			}
			if (pScaleBone->bUniversal && eaSize(&pScaleBone->eapcCounterScaleBones) > 0)
			{
				FOR_EACH_IN_EARRAY(pScaleBone->eapcCounterScaleBones, const char, pcCounterScaleBone)
				{
					// Look up the excluded bone, and multiply the counter scale by the inverse
					CalcBoneScale* pCounterScale;
					Vec3 vInv;
					int j;
					for (j=0; j<3; ++j)
						vInv[j] = 1.0f / pCalcBoneScale->vUniversalScale[j];
					if (!stashFindPointer(stBoneScaleTable, pcCounterScaleBone, &pCounterScale))
					{
						pCounterScale = malloc(sizeof(CalcBoneScale));
						copyVec3(onevec3, pCounterScale->vScale);
						copyVec3(onevec3, pCounterScale->vUniversalScale);
						zeroVec3(pCounterScale->vTrans);
						pCounterScale->pcBone = pcCounterScaleBone;
						stashAddPointer(stBoneScaleTable, pCounterScale->pcBone, pCounterScale, true);
					}
					mulVecVec3(pCounterScale->vUniversalScale, vInv, pCounterScale->vUniversalScale);
				}
				FOR_EACH_END;
			}
			pCalcBoneScale->pcBone = pScaleBone->pcBone;
			stashAddPointer(stBoneScaleTable, pCalcBoneScale->pcBone, pCalcBoneScale, true);
		}
	}
	FOR_EACH_END;
}

bool wlSkelInfoGetBoneScales(const SkelInfo* pSkelInfo, ScaleValue* pScaleValue, F32 fBodyType, StashTable stBoneScaleTable)
{
	F32 fInterpValue = NEGNORMALIZE(fBodyType);
	SkelScaleInfo* pScaleInfo = GET_REF(pSkelInfo->hScaleInfo);
	if (pScaleInfo)
	{
		FOR_EACH_IN_EARRAY(pScaleInfo->eaScaleGroup, SkelScaleGroup, pGroup)
			if (pGroup->pcGroupName == pScaleValue->pcScaleGroup) // We have a match!
				wlSkelScaleGroupGetBoneScales(pGroup, pScaleValue, fInterpValue, stBoneScaleTable);
		FOR_EACH_END;
	}
	return true;
}

// Open the named fx file in your favorite editor.
AUTO_COMMAND ACMD_CATEGORY(costume);
void skelInfoEdit(char* skelinfo_name ACMD_NAMELIST("SkelInfo", REFDICTIONARY))
{
	const SkelInfo* pSkelInfo = RefSystem_ReferentFromString(hSkelInfoDict, skelinfo_name);
	if (pSkelInfo)
	{
		char cFileNameBuf[512];
		if (fileLocateWrite(pSkelInfo->pcFileName, cFileNameBuf))
		{
			fileOpenWithEditor(cFileNameBuf);
		}
		else
		{
			Errorf("Could not find file %s for editing", pSkelInfo->pcFileName);
		}
	}
	else
	{
		Errorf("Could not find skel info %s for editing", skelinfo_name);
	}
}

void findCameraPosFromHeadShotFrame(const HeadShotFrame* pFrame, const DynSkeleton* pSkeleton, F32 fFOV, F32 fAspectRatio, Vec3 vResult)
{
	Vec3 vMax, vMin;
	Mat3 mCameraRot;
	Vec3 vNormalDir;
	Mat3 mInv;

	copyVec3(pFrame->vCameraDirection, vNormalDir);
	normalVec3(vNormalDir);

	// For this to really be correct, it should be in perspective-space.  I'm not going to fix that right now [RMARR- 8/30/11]
	copyVec3(vNormalDir, mCameraRot[2]);
	crossVec3(upvec, mCameraRot[2], mCameraRot[0]);
	crossVec3(mCameraRot[2], mCameraRot[0], mCameraRot[1]);

	normalVec3(mCameraRot[0]);
	normalVec3(mCameraRot[1]);
	invertMat3(mCameraRot, mInv);

	vMax[0] = vMax[1] = vMax[2] = -FLT_MAX;
	vMin[0] = vMin[1] = vMin[2] = FLT_MAX;
	FOR_EACH_IN_EARRAY(pFrame->eaFrameBone, HeadShotFrameBone, pFrameBone)
	{
		const DynNode* pNode = dynNodeFindByNameConst(pSkeleton->pRoot, pFrameBone->pcBoneName, false);
		if (pNode)
		{
			Vec3 vNodePos, vTransformedPos, vNodeScale;
			F32 fScaledRadius;

			dynNodeGetWorldSpacePos(pNode, vNodePos);
			dynNodeGetWorldSpaceScale(pNode, vNodeScale);

			if (pSkeleton->pDrawSkel)
				fScaledRadius = MAX(pFrameBone->fRadius, dynDrawSkeletonGetNodeRadius(pSkeleton->pDrawSkel, pNode->pcTag));
			else
				fScaledRadius = pFrameBone->fRadius;

			fScaledRadius *= vec3MaxComponent(vNodeScale);

			mulVecMat3(vNodePos, mInv, vTransformedPos);

			MAX1(vMax[0], vTransformedPos[0] + fScaledRadius);
			MAX1(vMax[1], vTransformedPos[1] + fScaledRadius);
			MAX1(vMax[2], vTransformedPos[2]);
			MIN1(vMin[0], vTransformedPos[0] - fScaledRadius);
			MIN1(vMin[1], vTransformedPos[1] - fScaledRadius);
			MIN1(vMin[2], vTransformedPos[2]);
		}
		else
		{
			WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
			SkelInfo* pSkelInfo = pCostume?GET_REF(pCostume->hSkelInfo):NULL;
			Errorf("Can't find bone %s in skeleton %s, as requested by headshotframe %s!", pFrameBone->pcBoneName, pSkelInfo?pSkelInfo->pcSkelInfoName:"Unknown", pFrame->pcFrameName);
			zeroVec3(vResult);
			return;
		}
	}
	FOR_EACH_END;

	// Add Margins
	vMin[0] -= pFrame->vMargins[0]; // LEFT
	vMax[0] += pFrame->vMargins[1]; // RIGHT
	vMax[1] += pFrame->vMargins[2]; // TOP
	vMin[1] -= pFrame->vMargins[3]; // BOTTOM

	// Now find the mid point of that bounding box
	{
		Vec2 vBoxSize;
		Vec3 vMid;
		Vec2 vDist;
		F32 fDist;
		F32 fTanFOV = tanf(RAD(fFOV) * 0.5f);

		addVec2(vMax, vMin, vMid);
		scaleVec2(vMid, 0.5f, vMid);
		vMid[2] = 0.0f;
		subVec2(vMax, vMin, vBoxSize);

		// First calculate distance for the case where the vertical is the constraint.
		vDist[1] = (vBoxSize[1] * 0.5f) / fTanFOV;

		// Now horizontal. We need to use the aspect ratio for this.
		vDist[0] = fAspectRatio * (vBoxSize[0] * 0.5f) / fTanFOV;

		fDist = MAX(vDist[0], vDist[1]);

		vMid[2] = -fDist+vMin[2];
		mulVecMat3(vMid, mCameraRot, vResult);
	}
}

#include "wlSkelInfo_h_ast.c"
