/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Message.h"
#include "Entity.h"
#include "File.h"
#include "cutscene_common.h"
#include "resourceManager.h"
#include "wlCurve.h"
#include "WorldVariable.h"

#include "cutscene_common_h_ast.h"

DictionaryHandle g_hCutsceneDict = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_STARTUP(Cutscenes);
void cutscene_Startup(void)
{
	//artificial cutscene task
}

static void cutscenedef_ValidatePos(CutsceneDef* pCutscene, CutscenePos* pPos)
{
	if(pPos->fHoldTime < 0 || pPos->fMoveTime < 0)
		ErrorFilenamef(pCutscene->filename, "Cutscene position in def %s has negative focus or pan time", pCutscene->name);

	if(vec3IsZero(pPos->vPos) && (!pPos->pchNamedPoint || !pPos->pchNamedPoint[0]))
		ErrorFilenamef(pCutscene->filename, "Cutscene position in def %s is (0, 0, 0); this is probably a mistake", pCutscene->name);

	if((!vec3IsZero(pPos->vPos)) && (pPos->pchNamedPoint && pPos->pchNamedPoint[0]))
		ErrorFilenamef(pCutscene->filename, "Cutscene position in def %s has both a pos and a pointname set", pCutscene->name);
}

static bool cutscenedef_ValidateEncounterOffset(CutsceneDef* pCutscene, CutsceneOffsetData *pOffsetData)
{
	if (pOffsetData->pchStaticEncName || pOffsetData->pchActorName){
		if (!pOffsetData->pchStaticEncName){
			ErrorFilenamef(pCutscene->filename, "Cutscene %s: A point has an Actor name, but not a Static Encounter name!", pCutscene->name);
			return false;
		}
		if (!pOffsetData->pchActorName){
			ErrorFilenamef(pCutscene->filename, "Cutscene %s: A point has an Static Encounter name, but not an Actor name!", pCutscene->name);
			return false;
		}
		if (!pCutscene->pcMapName){
			ErrorFilenamef(pCutscene->filename, "Cutscene %s: Cutscenes with Encounter Actor targets must have a map name.", pCutscene->name);
			return false;
		}
	}
	return true;
}

static void cutscenedef_Validate(CutsceneDef* pCutscene)
{
	int i, n;
	F32 camTime = 0;
	F32 targetTime = 0;

	if(pCutscene->pPathList)
	{
		//TODO: Add Paths Validation Here
		for (i=0; i<eaSize(&pCutscene->pPathList->ppPaths); i++){
			CutscenePath *pPath = pCutscene->pPathList->ppPaths[i];
			if(!cutscenedef_ValidateEncounterOffset(pCutscene, &pPath->common.main_offset))
				break;
			if(pPath->common.bTwoRelativePos) {
				if(!cutscenedef_ValidateEncounterOffset(pCutscene, &pPath->common.second_offset))
					break;
			}
		}
	}
	else
	{
		if((0 == eaSize(&pCutscene->ppCamPositions)) && (0 == eaSize(&pCutscene->ppCamTargets)))
			ErrorFilenamef(pCutscene->filename, "Cutscene %s has no camera positions or camera targets", pCutscene->name);

		// Validate each camera position and target
		n = eaSize(&pCutscene->ppCamPositions);
		for(i=0; i<n; i++)
		{
			CutscenePos* pPos = pCutscene->ppCamPositions[i];
			cutscenedef_ValidatePos(pCutscene, pPos);
			camTime += pPos->fHoldTime + pPos->fMoveTime;
		}
		n = eaSize(&pCutscene->ppCamTargets);
		for(i=0; i<n; i++)
		{
			CutscenePos* pPos = pCutscene->ppCamTargets[i];
			cutscenedef_ValidatePos(pCutscene, pPos);
			targetTime += pPos->fHoldTime + pPos->fMoveTime;
		}

		if(ABS(camTime - targetTime) > 1)
			ErrorFilenamef(pCutscene->filename, "Camera times and target times for cutscene def %s don't add up (%f and %f)", pCutscene->name, camTime, targetTime);
	}

	n = eaSize(&pCutscene->ppCutsceneEnts);
	if(n > 0 && pCutscene->bSinglePlayer)
		ErrorFilenamef(pCutscene->filename, "Cutscene def %s is a single-player cutscene, but has a list of cutscene entities", pCutscene->name);

	for(i=0; i<n; i++)
	{
		CutsceneEnt* pCutEnt = pCutscene->ppCutsceneEnts[i];
		if((!pCutEnt->actorName || !pCutEnt->actorName[0]) && (!pCutEnt->staticEncounterName || !pCutEnt->staticEncounterName[0]))
			ErrorFilenamef(pCutscene->filename, "Cutscene def %s has an entity with no actor name or encounter name.", pCutscene->name);
	}

	n = eaSize(&pCutscene->pUIGenList->ppUIGenPoints);
	for(i=0; i<n; i++)
	{
		CutsceneUIGenPoint* pPoint = pCutscene->pUIGenList->ppUIGenPoints[i];

		if(pPoint->actionType == CutsceneUIGenAction_MessageAndVariable || pPoint->actionType == CutsceneUIGenAction_VariableOnly)
			if(!pPoint->pcVariable || pPoint->pcVariable[0] == '\0')
				ErrorFilenamef(pCutscene->filename, "Cutscene has a UI Gen Track (%s) with a Key Frame to set a variable, but the Variable name is missing.", pCutscene->pUIGenList->common.pcName);

		if(pPoint->actionType == CutsceneUIGenAction_MessageAndVariable || pPoint->actionType == CutsceneUIGenAction_MessageOnly)
			if(!pPoint->pcMessage || pPoint->pcMessage[0] == '\0')
				ErrorFilenamef(pCutscene->filename, "Cutscene has a UI Gen Track (%s) with a Key Frame to send a message, but the Message name is missing.", pCutscene->pUIGenList->common.pcName);
	}
}

static int cutscenedef_ValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, CutsceneDef *pCutscene, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
			cutscenedef_Validate(pCutscene);
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static void fixupCutsceneDefPoints0to1(CutscenePathPoint ***pppPointList, F32 *prevTime)
{
	int j;
	for ( j=0; j < eaSize(pppPointList); j++ ) {
		CutscenePathPoint *pPoint = (*pppPointList)[j];
		pPoint->common.time = pPoint->Ver0_moveTime + (*prevTime);
		pPoint->common.length = pPoint->Ver0_holdTime;
		(*prevTime) = pPoint->common.time + pPoint->common.length;

		pPoint->Ver0_usePntDoF = 0;
		pPoint->Ver0_moveTime = 0;
		pPoint->Ver0_holdTime = 0;
		zeroVec4(pPoint->Ver0_cameraFade);
		pPoint->Ver0_DoFBlur = 0;
		pPoint->Ver0_DoFDist = 0;
	}
}

static bool fixupCutsceneDef0to1(CutsceneDef *pCutsceneDef)
{
	int i, j;
	F32 prevCamPosTime = 0;
	F32 prevLookAtTime = 0;
	Vec4 prevFadeVal = {0,0,0,0};
	U8 prevDOFOn = 0;
	F32 prevDOFBlur = 0;
	F32 prevDOFDist = 0;

	if(!pCutsceneDef->pPathList)
		return true;

	pCutsceneDef->pcMapName = pCutsceneDef->pPathList->Ver0_map_name;
	pCutsceneDef->pPathList->Ver0_map_name = NULL;
	pCutsceneDef->fBlendRate = pCutsceneDef->pPathList->Ver0_blend_rate;
	pCutsceneDef->pPathList->Ver0_blend_rate = 0;

	for ( i=0; i < eaSize(&pCutsceneDef->pPathList->ppPaths); i++ ) {
		CutscenePath *pPath = pCutsceneDef->pPathList->ppPaths[i];

		switch (pPath->type) {
			case CutscenePathType_EasyPath:
			case CutscenePathType_Orbit:
			case CutscenePathType_LookAround:
			case CutscenePathType_ShadowEntity:
				if(eaSize(&pPath->ppPositions) != eaSize(&pPath->ppTargets)) {
					ErrorFilenamef(pCutsceneDef->filename, "CutsceneDef of version %d, path that should have a matching number of points does not.", pCutsceneDef->iVersion);
					return false;
				}
		}

		{
			F32 prevOtherTime = prevLookAtTime;
			U8 bDOFOn = pPath->Ver0_useDoF;
			for ( j=0; j < eaSize(&pPath->ppTargets); j++ ) {
				CutscenePathPoint *pCamTarget = pPath->ppTargets[j];
				F32 fDOFBlur = (pCamTarget->Ver0_usePntDoF ? pCamTarget->Ver0_DoFBlur : pPath->Ver0_DoFBlur);
				F32 fDOFDist = (pCamTarget->Ver0_usePntDoF ? pCamTarget->Ver0_DoFDist : pPath->Ver0_DoFDist);
				if(bDOFOn != prevDOFOn || fDOFBlur != prevDOFBlur || fDOFDist != prevDOFDist) {
					CutsceneDOFPoint *pNewPoint;
					if(!pCutsceneDef->pDOFList) {
						pCutsceneDef->pDOFList = StructCreate(parse_CutsceneDOFList);
						pCutsceneDef->pDOFList->common.pcName = StructAllocString("DOFList_Ver0");
					}

					pNewPoint = StructCreate(parse_CutsceneDOFPoint);
					pNewPoint->common.time = prevOtherTime;
					pNewPoint->bDOFIsOn = prevDOFOn;
					pNewPoint->fDOFBlur = prevDOFBlur;
					pNewPoint->fDOFDist = prevDOFDist;
					eaPush(&pCutsceneDef->pDOFList->ppDOFPoints, pNewPoint);

					pNewPoint = StructCreate(parse_CutsceneDOFPoint);
					pNewPoint->common.time = prevOtherTime + pCamTarget->Ver0_moveTime;
					pNewPoint->bDOFIsOn = bDOFOn;
					pNewPoint->fDOFBlur = fDOFBlur;
					pNewPoint->fDOFDist = fDOFDist;
					eaPush(&pCutsceneDef->pDOFList->ppDOFPoints, pNewPoint);

					prevDOFOn = bDOFOn;
					prevDOFBlur = fDOFBlur;
					prevDOFDist = fDOFDist;
				}

				if(!sameVec4(prevFadeVal, pCamTarget->Ver0_cameraFade)) {
					CutsceneFadePoint *pNewPoint;
					if(!pCutsceneDef->pFadeList) {
						pCutsceneDef->pFadeList = StructCreate(parse_CutsceneFadeList);
						pCutsceneDef->pFadeList->common.pcName = StructAllocString("FadeList_Ver0");
					}

					pNewPoint = StructCreate(parse_CutsceneFadePoint);
					pNewPoint->common.time = prevOtherTime;
					copyVec4(prevFadeVal, pNewPoint->vFadeValue);
					eaPush(&pCutsceneDef->pFadeList->ppFadePoints, pNewPoint);
					
					pNewPoint = StructCreate(parse_CutsceneFadePoint);
					pNewPoint->common.time = prevOtherTime + pCamTarget->Ver0_moveTime;
					copyVec4(pCamTarget->Ver0_cameraFade, pNewPoint->vFadeValue);
					eaPush(&pCutsceneDef->pFadeList->ppFadePoints, pNewPoint);

					copyVec4(pCamTarget->Ver0_cameraFade, prevFadeVal);
				}

				prevOtherTime += pCamTarget->Ver0_moveTime;
				prevOtherTime += pCamTarget->Ver0_holdTime;
			}
		}

		pPath->Ver0_useDoF = 0;
		pPath->Ver0_DoFBlur = 0;
		pPath->Ver0_DoFDist = 0;

		fixupCutsceneDefPoints0to1(&pPath->ppPositions, &prevCamPosTime);
		fixupCutsceneDefPoints0to1(&pPath->ppTargets, &prevLookAtTime);

		prevCamPosTime = prevLookAtTime = MAX(prevCamPosTime, prevLookAtTime);
	}

	return true;
}

static bool fixupCutsceneDef1to2(CutsceneDef *pCutsceneDef)
{
	int i;
	if(!pCutsceneDef->pPathList)
		return true;
	for ( i=0; i < eaSize(&pCutsceneDef->pPathList->ppPaths); i++ ) {
		CutscenePath *pPath = pCutsceneDef->pPathList->ppPaths[i];
		if(pPath->Ver1_bFollowPlayer) {
			pPath->common.main_offset.offsetType = CutsceneOffsetType_Player;
		} else if(pPath->Ver1_bFollowContact) {
			pPath->common.main_offset.offsetType = CutsceneOffsetType_Contact;
		}
		pPath->Ver1_bFollowPlayer = false;
		pPath->Ver1_bFollowContact = false;
	}
	return true;
}

/// Fixup function for CutsceneDef
TextParserResult fixupCutsceneDef(CutsceneDef *pCutsceneDef, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ: 
	case FIXUPTYPE_POST_BIN_READ: 
		if(pCutsceneDef->iVersion < 1 && !fixupCutsceneDef0to1(pCutsceneDef)) {
			ErrorFilenamef(pCutsceneDef->filename, "CutsceneDef failed to convert from version 0 to 1.  Now in a corrupt state.");
			return PARSERESULT_INVALID;
		}
		if(pCutsceneDef->iVersion < 2 && !fixupCutsceneDef1to2(pCutsceneDef)) {
			ErrorFilenamef(pCutsceneDef->filename, "CutsceneDef failed to convert from version 1 to 2.  Now in a corrupt state.");
			return PARSERESULT_INVALID;
		}
		pCutsceneDef->iVersion = CUTSCENE_DEF_VERSION;
		break;
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void RegisterCutsceneDefDict(void)
{
	// Set up reference dictionaries
	g_hCutsceneDict = RefSystem_RegisterSelfDefiningDictionary("Cutscene",false, parse_CutsceneDef, true, true, NULL);

	resDictManageValidation(g_hCutsceneDict, cutscenedef_ValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hCutsceneDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hCutsceneDict, ".Name", NULL, ".Tags", NULL, NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hCutsceneDict, 8, false, resClientRequestSendReferentCommand);
	}
}

F32 cutscene_GetPathEndTimeUnsafe(CutscenePath *pPath)
{
	int size;
	F32 retVal = -1;
	
	size = eaSize(&pPath->ppPositions);
	if(size > 0) {
		CutscenePathPoint *pPoint = pPath->ppPositions[size-1];
		MAX1(retVal, pPoint->common.time + pPoint->common.length);
	}

	size = eaSize(&pPath->ppTargets);
	if(size > 0) {
		CutscenePathPoint *pPoint = pPath->ppTargets[size-1];
		MAX1(retVal, pPoint->common.time + pPoint->common.length);
	}

	return retVal;
}

F32 cutscene_GetPathLength(CutscenePathList *pPathList, int idx, bool bIncludeHold)
{
	int i;
	F32 prevTime = 0;
	F32 posEnd = 0, posHolds = 0, posDiff = 0;
	F32 lookEnd = 0, lookHolds = 0, lookDiff = 0;

	assert( idx >= 0 && idx < eaSize(&pPathList->ppPaths) );

	if(idx > 0) {
		F32 newTime;
		for (i = idx-1; i >= 0; i--) {
			newTime = cutscene_GetPathEndTimeUnsafe(pPathList->ppPaths[i]);
			if(newTime >= 0) {
				prevTime = newTime;
				break;
			}
		}
	}

	for ( i=0; i < eaSize(&pPathList->ppPaths[idx]->ppPositions); i++ ) {
		CutscenePathPoint *pPoint = pPathList->ppPaths[idx]->ppPositions[i];
		MAX1(posEnd, pPoint->common.time + pPoint->common.length);
		posHolds += pPoint->common.length;
	}

	for ( i=0; i < eaSize(&pPathList->ppPaths[idx]->ppTargets); i++ ) {
		CutscenePathPoint *pPoint = pPathList->ppPaths[idx]->ppTargets[i];
		MAX1(lookEnd, pPoint->common.time + pPoint->common.length);
		lookHolds += pPoint->common.length;
	}

	posDiff = posEnd-prevTime;
	if(!bIncludeHold)
		posDiff -= posHolds;

	lookDiff = lookEnd-prevTime;
	if(!bIncludeHold)
		lookDiff -= lookHolds;

	return MAX(posDiff, lookDiff);
}

F32 cutscene_GetPathStartTime(CutscenePathList* pPathList, CutscenePath *pEndPath)
{
	int i, n;
	F32 retVal = 0;
	n = eaSize(&pPathList->ppPaths);
	for( i=0; i < n; i++ ) {
		F32 time;
		CutscenePath *pPath = pPathList->ppPaths[i];
		if(pPath == pEndPath)
			return retVal;
		time = cutscene_GetPathEndTimeUnsafe(pPath);
		MAX1(retVal, time);
	}
	return -1;
}

F32 cutscene_GetPathEndTime(CutscenePathList* pPathList, CutscenePath *pEndPath)
{
	int i, n;
	F32 retVal = 0;
	n = eaSize(&pPathList->ppPaths);
	for( i=0; i < n; i++ ) {
		F32 time;
		CutscenePath *pPath = pPathList->ppPaths[i];
		time = cutscene_GetPathEndTimeUnsafe(pPath);
		MAX1(retVal, time);
		if(pPath == pEndPath)
			return retVal;
	}
	return -1;
}

static F32 cutscene_GetPathListLength(CutscenePathList* pPathList, bool bIncludeHold)
{
	int i, n;
	F32 retVal = 0;
	n = eaSize(&pPathList->ppPaths);
	for( i=0; i < n; i++ ) {
		CutscenePath *pPath = pPathList->ppPaths[i];
		F32 time = cutscene_GetPathLength(pPathList, i, bIncludeHold);
		retVal += time;
	}
	return retVal;
}

static F32 cutscene_IncrementElapsedTimeAndClampForPathList(CutscenePathList* pPathList, F32 prevElapsedTime, F32 timestep)
{
	int i, n;
	F32 newElapsedTime = prevElapsedTime + timestep;
	F32 timeVal = 0;
	n = eaSize(&pPathList->ppPaths);
	for( i=0; i < n; i++ ) {
		CutscenePath *pPath = pPathList->ppPaths[i];
		F32 time = cutscene_GetPathLength(pPathList, i, /*bIncludeHold=*/true);
		timeVal += time;
		if(prevElapsedTime < timeVal)
			return MIN(newElapsedTime, timeVal);
	}
	return newElapsedTime;
}

static F32 cutscene_GetCGTLength(void* pCGT_In)
{
	int size;
	F32 retVal = 0;
	CutsceneDummyTrack* pCGT = (CutsceneDummyTrack*)pCGT_In;
	if(!pCGT)
		return 0;
	size = eaSize(&pCGT->ppGenPnts);
	if(size > 0) {
		CutsceneCommonPointData *pGenPnt = pCGT->ppGenPnts[size-1];
		MAX1(retVal, pGenPnt->time + pGenPnt->length);
	}
	return retVal;
}

static F32 cutscene_GetCGTListLength(void **pCGT_In)
{
	int i;
	F32 retVal = 0;
	for ( i=0; i < eaSize(&pCGT_In); i++ ) {
		F32 newVal = cutscene_GetCGTLength(pCGT_In[i]);
		MAX1(retVal, newVal);
	}
	return retVal;
}

static F32 cutscene_GetAllCGTsLength(CutsceneDef* pCutscene)
{
	F32 retVal =0 ;
	F32 time;

	//CutsceneEffectsAndEvents
	//Add a call to get the length in seconds
	#define cutCheckLength(pList) {time = cutscene_GetCGTLength(pList); MAX1(retVal, time);}
	#define cutCheckListLength(pList) {time = cutscene_GetCGTListLength(pList); MAX1(retVal, time);}
	cutCheckLength(pCutscene->pFadeList);
	cutCheckLength(pCutscene->pDOFList);
	cutCheckLength(pCutscene->pFOVList);
	cutCheckLength(pCutscene->pShakeList);
	cutCheckListLength(pCutscene->ppObjectLists);
	cutCheckListLength(pCutscene->ppEntityLists);
	cutCheckListLength(pCutscene->ppTexLists);
	cutCheckListLength(pCutscene->ppSoundLists);
	cutCheckListLength(pCutscene->ppSubtitleLists);
	return retVal;
}

F32 cutscene_GetLength(CutsceneDef* pCutscene, bool bIncludeHold)
{
	int i, n;
	F32 tempLength = 0;
	F32 retVal = 0;

	if(pCutscene->pPathList) {
		F32 CamPathTime = cutscene_GetPathListLength(pCutscene->pPathList, bIncludeHold);
		F32 OtherPathTime = cutscene_GetAllCGTsLength(pCutscene);
		return MAX(CamPathTime, OtherPathTime);
	}

	n = eaSize(&pCutscene->ppCamPositions);
	for(i=0; i < n; i++)
	{
		CutscenePos* pPos = pCutscene->ppCamPositions[i];
		if(bIncludeHold)
			tempLength += pPos->fHoldTime;
		tempLength += pPos->fMoveTime;
	}
	retVal = tempLength;

	n = eaSize(&pCutscene->ppCamTargets);
	tempLength = 0;
	for(i=0; i < n; i++)
	{
		CutscenePos* pPos = pCutscene->ppCamTargets[i];
		if(bIncludeHold)
			tempLength += pPos->fHoldTime;
		tempLength += pPos->fMoveTime;
	}

	retVal = MAX(retVal, tempLength);

	return retVal;
}

F32 cutscene_IncrementElapsedTimeAndClamp(CutsceneDef* pCutscene, F32 prevElapsedTime, F32 timestep)
{
	int i, n;
	F32 tempLength = 0;
	F32 newElapsedTime = 0;
	F32 maxElapsedTime = 0;

	if(pCutscene->pPathList) {
		newElapsedTime = cutscene_IncrementElapsedTimeAndClampForPathList(pCutscene->pPathList, prevElapsedTime, timestep);

		return MIN(newElapsedTime, cutscene_GetLength(pCutscene, /*bIncludeHold=*/true));
	}

	newElapsedTime = prevElapsedTime + timestep;

	n = eaSize(&pCutscene->ppCamPositions);
	for(i=0; i < n; i++)
	{
		CutscenePos* pPos = pCutscene->ppCamPositions[i];
		tempLength += pPos->fHoldTime; // including hold times
		tempLength += pPos->fMoveTime;
	}

	maxElapsedTime = tempLength;

	n = eaSize(&pCutscene->ppCamTargets);
	tempLength = 0;
	for(i=0; i < n; i++)
	{
		CutscenePos* pPos = pCutscene->ppCamTargets[i];
		tempLength += pPos->fHoldTime; // including hold times
		tempLength += pPos->fMoveTime;
	}

	maxElapsedTime = MAX(maxElapsedTime, tempLength);

	return MIN(maxElapsedTime, newElapsedTime);
}

CutsceneDef* cutscene_GetDefByName( const char* pchName )
{
	return RefSystem_ReferentFromString(g_hCutsceneDict, pchName);
}

static bool cutscene_GetAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
{
	if (pcAddString)
	{
		bool bDup = false;
		FOR_EACH_IN_EARRAY(*peaStrings, const char, pcHasString) {
			if (strcmpi(pcHasString, pcAddString) == 0) {
				bDup = true;
			}
		} FOR_EACH_END;
		if (!bDup) {
			eaPush(peaStrings, strdup(pcAddString));
		}
		return false;
	}
	return true;
}

void cutscene_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	CutsceneDef *pCutsceneDef;
	ResourceIterator rI;

	*ppcType = strdup("CutsceneDef");

	resInitIterator(g_hCutsceneDict, &rI);
	while (resIteratorGetNext(&rI, NULL, &pCutsceneDef))
	{
		bool bResourceHasAudio = false;

		bResourceHasAudio |= cutscene_GetAudioAssets_HandleString(pCutsceneDef->pchCutsceneSound, peaStrings);

		FOR_EACH_IN_EARRAY(pCutsceneDef->ppSoundLists, CutsceneSoundList, pSoundList) {
			FOR_EACH_IN_EARRAY(pSoundList->ppSoundPoints, CutsceneSoundPoint, pSoundPoint)
			{
				//only adding the variable here, which should make all paths with that variable valid..
				bResourceHasAudio |= cutscene_GetAudioAssets_HandleString(pSoundPoint->pcSoundVariable, peaStrings);
			}
			FOR_EACH_END;
		} FOR_EACH_END;

		*puiNumData = *puiNumData + 1;
		if (bResourceHasAudio) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	resFreeIterator(&rI);
}

#include "cutscene_common_h_ast.c"
