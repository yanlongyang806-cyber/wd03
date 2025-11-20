/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CharacterClass.h"
#include "Character_combat.h"
#include "Entity.h"
#include "estring.h"
#include "Expression.h"
#include "gslEncounter.h"
#include "gslInteractable.h"
#include "gslInteractionManager.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslVolume.h"
#include "interaction_common.h"
#include "mission_common.h"
#include "player.h"
#include "powers.h"
#include "StringCache.h"
#include "WorldGrid.h"

// ----------------------------------------------------------------------------------
// Expression functions
// ----------------------------------------------------------------------------------

S32 interactable_ExprValidate(WorldScope *pScope, const char *pcClickableName, ACMD_EXPR_ERRSTRING estrErrString)
{
	if(!pcClickableName || !pcClickableName[0]) {
		estrPrintf(estrErrString, "Passing in empty string for clickable name");
		return 0;
	}

	if (strnicmp(pcClickableName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0) {
		estrPrintf(estrErrString, "Cannot reference temporary interactable name: %s", pcClickableName);
		return 0;
	}

	if (!interactable_InteractableExists(pScope, pcClickableName)) {
		estrPrintf(estrErrString, "Clickable %s does not exist in scope.", pcClickableName);
		return 0;
	}

	return 1;
}


// This function hides an interactable so that it can't be seen or interacted with
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(HideClickable);
ExprFuncReturnVal exprFuncHideClickable(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcClickableName, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	pScope = exprContextGetScope(pContext);
	if (!interactable_ExprValidate(pScope, pcClickableName, estrErrString)) {
		return ExprFuncReturnError;
	}

	// Call to interaction code
	if (!interactable_HideInteractableByName(iPartitionIdx, pScope, pcClickableName, 0, estrErrString)) {
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// This function hides all interactables in a logical group so that it can't be seen or interacted with
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(HideClickableGroup);
ExprFuncReturnVal exprFuncHideClickableGroup(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcGroupName, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	if (pcGroupName && (strnicmp(pcGroupName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0)) {
		estrPrintf(estrErrString, "Cannot reference temporary group name: %s", pcGroupName);
		return ExprFuncReturnError;
	}

	// Call to interaction code
	pScope = exprContextGetScope(pContext);
	if (!interactable_HideInteractableGroup(iPartitionIdx, pScope, pcGroupName, 0, estrErrString)) {
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// This function unhides an interactable
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(UnhideClickable);
ExprFuncReturnVal exprFuncUnhideClickable(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcClickableName, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	pScope = exprContextGetScope(pContext);
	if (!interactable_ExprValidate(pScope, pcClickableName, estrErrString)) {
		return ExprFuncReturnError;
	}

	// Call to interaction code
	if (!interactable_ShowInteractableByName(iPartitionIdx, pScope, pcClickableName, 1, estrErrString)) {
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// This function unhides all interactables in a group
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(UnhideClickableGroup);
ExprFuncReturnVal exprFuncUnhideClickableGroup(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcGroupName, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	if (pcGroupName && (strnicmp(pcGroupName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0)) {
		estrPrintf(estrErrString, "Cannot reference temporary group name: %s", pcGroupName);
		return ExprFuncReturnError;
	}

	// Call to interaction code
	pScope = exprContextGetScope(pContext);
	if (!interactable_ShowInteractableGroup(iPartitionIdx, pScope, pcGroupName, 1, estrErrString)) {
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// This function resets an interactable to its map-start state
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(ResetClickable);
ExprFuncReturnVal exprFuncResetClickable(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcClickableName, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	pScope = exprContextGetScope(pContext);
	if (!interactable_ExprValidate(pScope, pcClickableName, estrErrString)) {
		return ExprFuncReturnError;
	}

	// Call to interaction code
	if (!interactable_ResetInteractableByName(iPartitionIdx, pScope, pcClickableName, 1, estrErrString)) {
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// This function changes which geometry is visible for a given interactable
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(ClickableSetVisibleChild);
ExprFuncReturnVal exprFuncClickableSetVisibleChild(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcClickableName, U32 child, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	if (pcClickableName && (strnicmp(pcClickableName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0)) {
		estrPrintf(estrErrString, "Cannot reference temporary interactable name: %s", pcClickableName);
		return ExprFuncReturnError;
	}

	// Call to interaction code
	pScope = exprContextGetScope(pContext);
	if (!interactable_SetVisibleChildByName(iPartitionIdx, pScope, pcClickableName, child, estrErrString)) {
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// This function returns which geometry is visible for a given interactable
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(ClickableGetVisibleChild);
ExprFuncReturnVal exprFuncClickableGetVisibleChild(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piResult, const char *pcClickableName, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	if (pcClickableName && (strnicmp(pcClickableName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0)) {
		estrPrintf(estrErrString, "Cannot reference temporary interactable name: %s", pcClickableName);
		return ExprFuncReturnError;
	}

	// Call to interaction code
	pScope = exprContextGetScope(pContext);
	if (!interactable_GetVisibleChild(iPartitionIdx, pScope, pcClickableName, piResult, estrErrString)) {
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Returns TRUE if any interactable with the given name is currently Active
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ClickableIsActive);
ExprFuncReturnVal exprFuncClickableIsActive(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piResult, const char *pcClickableName, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	pScope = exprContextGetScope(pContext);
	if (!interactable_ExprValidate(pScope, pcClickableName, estrErrString)) {
		return ExprFuncReturnError;
	}

	// Call to interaction code
	*piResult = interactable_IsActiveByName(iPartitionIdx, pScope, pcClickableName);
	return ExprFuncReturnFinished;
}


// Returns TRUE if any interactable with the given name is currently visible
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ClickableIsVisible);
ExprFuncReturnVal exprFuncClickableIsVisible(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piResult, const char *pcClickableName, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	pScope = exprContextGetScope(pContext);
	if (!interactable_ExprValidate(pScope, pcClickableName, estrErrString)) {
		return ExprFuncReturnError;
	}

	// Call to interaction code
	*piResult = !interactable_IsHiddenByName(iPartitionIdx, pScope, pcClickableName);
	return ExprFuncReturnFinished;
}


// Returns TRUE if any interactable with the given name is currently being used
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ClickableInUse);
ExprFuncReturnVal exprFuncClickableInUse(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piResult, const char *pcClickableName, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	pScope = exprContextGetScope(pContext);
	if (!interactable_ExprValidate(pScope, pcClickableName, estrErrString)) {
		return ExprFuncReturnError;
	}

	// Call to interaction code
	*piResult = interactable_IsInUseByName(iPartitionIdx, pScope, pcClickableName);
	return ExprFuncReturnFinished;
}


// Returns TRUE if the interactable is not hidden, in use, on cooldown, etc.
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ClickableIsUsable);
ExprFuncReturnVal exprFuncClickableIsUsable(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piResult, const char *pcClickableName, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;

	pScope = exprContextGetScope(pContext);
	if (!interactable_ExprValidate(pScope, pcClickableName, estrErrString)) {
		return ExprFuncReturnError;
	}

	// Call to interaction code
	*piResult = interactable_IsUsableByName(iPartitionIdx, pScope, pcClickableName);
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
void encountereval_LoadVerify_ActivatePower(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcPowerName, const char *pcStrength, ACMD_EXPR_ERRSTRING errEstr)
{
	if (!ActivatePowerClassFromStrength(pcStrength)) {
		estrPrintf(errEstr, "Unknown strength used in ActivatePower(%s, %s). Valid are Small, Medium, and Large.", pcPowerName, pcStrength);
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal encountereval_LoadVerify_ActivatePowerLevel(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcPowerName, const char *pcStrength, int iLevel, ACMD_EXPR_ERRSTRING errEstr)
{
	if (!ActivatePowerClassFromStrength(pcStrength)) {
		estrPrintf(errEstr, "Unknown strength used in ActivatePower(%s, %s). Valid are Small, Medium, and Large.", pcPowerName, pcStrength);
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
void encountereval_LoadVerify_ActivatePowerAtPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcPowerName, const char *pcStrength, ACMD_EXPR_LOC_MAT4_IN mPoint, ACMD_EXPR_ERRSTRING errEstr)
{
	if (!ActivatePowerClassFromStrength(pcStrength)) {
		estrPrintf(errEstr, "Unknown strength used in ActivatePowerAtPoint(%s, %s). Valid are Small, Medium, and Large.", pcPowerName, pcStrength);
	}
}


// Activate a power from a clickable targeted at the clickable's location
AUTO_EXPR_FUNC(clickable) ACMD_NAME(ActivatePowerPointBlank) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePower);
void exprFuncActivatePowerPointBlank(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_ERRSTRING errEstr)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		WorldInteractionEntry *pInteractionEntry = exprContextGetVarPointerUnsafe(pContext, "ClickableTracker");
		if (pInteractionEntry) {
			int iLevel = mechanics_GetMapLevel(iPartitionIdx);
			location_ApplyPowerDef(pInteractionEntry->base_entry.bounds.world_matrix[3], iPartitionIdx, pPowerDef, 0, pInteractionEntry->base_entry.bounds.world_matrix[3], NULL, NULL, ActivatePowerClassFromStrength(pcStrength), iLevel, 0);
		} else {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "No clickable in ActivatePowerPointBlank expression context");
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerPointBlank: couldn't find power %s", pcPowerName);
	}
}


AUTO_EXPR_FUNC(clickable) ACMD_NAME(ActivatePowerPointBlankLevel) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerLevel);
ExprFuncReturnVal exprFuncActivatePowerPointBlankLevel(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, int iLevel, ACMD_EXPR_ERRSTRING errEstr)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		WorldInteractionEntry *pInteractionEntry = exprContextGetVarPointerUnsafe(pContext, "ClickableTracker");
		if (pInteractionEntry) {
			location_ApplyPowerDef(pInteractionEntry->base_entry.bounds.world_matrix[3], iPartitionIdx, pPowerDef, 0, pInteractionEntry->base_entry.bounds.world_matrix[3], NULL, NULL, ActivatePowerClassFromStrength(pcStrength), iLevel, 0);
		} else {
			estrPrintf(errEstr, "No clickable in ActivatePowerPointBlank expression context");
			return ExprFuncReturnError;
		}
	} else {
		estrPrintf(errEstr, "ActivatePowerPointBlank: couldn't find power %s", pcPowerName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Activate a power from a clickable to the player using the clickable
AUTO_EXPR_FUNC(clickable) ACMD_NAME(ActivatePowerAtPlayers) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePower);
void exprFuncActivatePowerPointAtPlayers(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_ERRSTRING errEstr)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		Entity *pInteractor = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
		WorldInteractionEntry *pInteractionEntry = exprContextGetVarPointerUnsafe(pContext, "ClickableTracker");
		if (pInteractionEntry && pInteractor) {
			EntityRef entRef;
			int iLevel;
			ANALYSIS_ASSUME(pInteractor != NULL);
			entRef = entGetRef(pInteractor);
			iLevel = mechanics_GetMapLevel(iPartitionIdx);
			location_ApplyPowerDef(pInteractionEntry->base_entry.bounds.world_matrix[3], iPartitionIdx, pPowerDef, entRef, NULL, NULL, NULL, ActivatePowerClassFromStrength(pcStrength), iLevel, 0);
		} else if(!pInteractionEntry) {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "No clickable in ActivatePowerAtPlayers expression context");
		} else {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "No interacting player in ActivatePowerAtPlayers expression context");
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerAtPlayers: couldn't find power %s", pcPowerName);
	}
}


// Activate a power from a clickable to a point
AUTO_EXPR_FUNC(clickable) ACMD_NAME(ActivatePowerAtPoint) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerAtPoint);
void exprFuncActivatePowerAtPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_LOC_MAT4_IN mPoint, ACMD_EXPR_ERRSTRING errEstr)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		WorldInteractionEntry *pInteractionEntry = exprContextGetVarPointerUnsafe(pContext, "ClickableTracker");
		if (pInteractionEntry) {
			int iLevel = mechanics_GetMapLevel(iPartitionIdx);
			location_ApplyPowerDef(pInteractionEntry->base_entry.bounds.world_matrix[3], iPartitionIdx, pPowerDef, 0, mPoint[3], NULL, NULL, ActivatePowerClassFromStrength(pcStrength), iLevel, 0);
		} else if(!pInteractionEntry) {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "No clickable in ActivatePowerAtPoint expression context");
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerAtPoint: couldn't find power %s", pcPowerName);
	}
}


// Set the gate state for a named interactable
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SetGateOpen);
ExprFuncReturnVal exprFuncSetGateOpen(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcGateName, bool bOpen, ACMD_EXPR_ERRSTRING estrErrString)
{
	GameInteractable *pGateInteractable;
	WorldScope *pScope;

	if (pcGateName && (strnicmp(pcGateName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0)) {
		estrPrintf(estrErrString, "Cannot reference temporary interactable name: %s", pcGateName);
		return ExprFuncReturnError;
	}

	// Call to interaction code
	pScope = exprContextGetScope(pContext);
	pGateInteractable = interactable_GetByName(pcGateName, pScope);
	if (pGateInteractable) {
		interactable_ChangeGateOpenState(iPartitionIdx, pGateInteractable, NULL, bOpen);
	} else if (estrErrString) {
		estrPrintf(estrErrString, "Gate '%s' : no such interactable", pcGateName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Get the gate state for a named interactable
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(IsGateOpen);
ExprFuncReturnVal exprFuncIsGateOpen(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pResult, const char *pcGateName, ACMD_EXPR_ERRSTRING estrErrString)
{
	GameInteractable *pGateInteractable;
	WorldScope *pScope;

	if (pcGateName && (strnicmp(pcGateName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0)) {
		estrPrintf(estrErrString, "Cannot reference temporary interactable name: %s", pcGateName);
		return ExprFuncReturnError;
	}

	// Call to interaction code
	pScope = exprContextGetScope(pContext);
	pGateInteractable = interactable_GetByName(pcGateName, pScope);
	if (pGateInteractable) {
		*pResult = interactable_IsGateOpen(iPartitionIdx, pGateInteractable);
	} else if (estrErrString) {
		*pResult = false;
		estrPrintf(estrErrString, "Gate '%s' : no such interactable", pcGateName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Triggers the normal operation of an interactable as if the player had clicked on it
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ActivateInteractable);
ExprFuncReturnVal exprFuncActivateInteractable(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_NN_VALID Entity * pPlayerEnt, const char * pcInteractable, int iIndex, ACMD_EXPR_ERRSTRING estrErrString)
{
	GameInteractable *pInteractable;
	WorldScope *pScope;

	if (pcInteractable && (strnicmp(pcInteractable, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0)) {
		estrPrintf(estrErrString, "Cannot reference temporary interactable name: %s", pcInteractable);
		return ExprFuncReturnError;
	}

	// Call to interaction code
	pScope = exprContextGetScope(pContext);
	pInteractable = interactable_GetByName(pcInteractable, pScope);
	if (pInteractable)
	{
		WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, iIndex);
		im_Interact(iPartitionIdx, pInteractable, NULL, NULL, pEntry, iIndex, pPlayerEnt);
	} else if (estrErrString) {
		estrPrintf(estrErrString, "ActivateInteractable '%s' : no such interactable", pcInteractable);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(clickable) ACMD_NAME(UGCGetDetailSelectionInt);
ExprFuncReturnVal exprFuncUGCGetDetailSelectionInt(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piRet, int iDefault)
{
	GameInteractable *pInteractable = exprContextGetVarPointerUnsafePooled(pContext, g_InteractableExprVarName);
	*piRet = iDefault;
	if (pInteractable) {
		MapVariable *pMapVar;
		pMapVar = mapvariable_GetByName(iPartitionIdx, pInteractable->pcName);

		if (pMapVar && pMapVar->pDef->eType == WVAR_INT) {
			*piRet = pMapVar->pVariable->iIntVal;
		}
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(clickable) ACMD_NAME(HasRecentlyCompletedClickable, HasRecentlyCompletedInteractWithObject);
ExprFuncReturnVal exprFuncHasRecentlyCompletedInteractWithObject(ExprContext *pContext, ACMD_EXPR_INT_OUT piRet, const char *pchObjectName)
{
	Entity *pPlayerEnt = (Entity *)exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	const char* pchObjectNamePooled = NULL;

	(*piRet) = false;
	
	if (pchObjectName && pchObjectName[0])
	{
		pchObjectNamePooled = allocFindString(pchObjectName);
	}
	if (pPlayerEnt && pPlayerEnt->pPlayer && pchObjectNamePooled)
	{
		InteractInfo *pInfo = pPlayerEnt->pPlayer->pInteractInfo;
		if (pInfo && pInfo->recentlyCompletedInteracts)
		{
			int i;
			for (i = eaSize(&pInfo->recentlyCompletedInteracts)-1; i >= 0; i--)
			{
				InteractionInfo* pInteract = pInfo->recentlyCompletedInteracts[i];
				if (pInteract->pchInteractableName == pchObjectNamePooled)
				{
					(*piRet) = true;
				}
			}
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(clickable) ACMD_NAME(HasRecentlyCompletedInteractWithVolume);
ExprFuncReturnVal exprFuncHasRecentlyCompletedInteractWithVolume(ExprContext *pContext, ACMD_EXPR_INT_OUT piRet, const char *pchVolumeName)
{
	Entity *pPlayerEnt = (Entity *)exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	const char* pchVolumeNamePooled = NULL;

	(*piRet) = false;
	
	if (pchVolumeName && pchVolumeName[0])
	{
		pchVolumeNamePooled = allocFindString(pchVolumeName);
	}
	if (pPlayerEnt && pPlayerEnt->pPlayer && pchVolumeNamePooled)
	{
		InteractInfo *pInfo = pPlayerEnt->pPlayer->pInteractInfo;
		if (pInfo && pInfo->recentlyCompletedInteracts)
		{
			int i;
			for (i = eaSize(&pInfo->recentlyCompletedInteracts)-1; i >= 0; i--)
			{
				InteractionInfo* pInteract = pInfo->recentlyCompletedInteracts[i];
				if (pInteract->pchVolumeName == pchVolumeNamePooled)
				{
					(*piRet) = true;
				}
			}
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(clickable) ACMD_NAME(HasRecentlyCompletedInteractWithEntityInEncounter);
ExprFuncReturnVal exprFuncHasRecentlyCompletedInteractWithEntityInEncounter(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piRet, const char *pchEncounterName)
{
	Entity *pPlayerEnt = (Entity *)exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	const char* pchEncNamePooled = NULL;

	(*piRet) = false;
	
	if (pchEncounterName && pchEncounterName[0])
	{
		pchEncNamePooled = allocFindString(pchEncounterName);
	}
	if (pPlayerEnt && pPlayerEnt->pPlayer && pchEncNamePooled)
	{
		InteractInfo *pInfo = pPlayerEnt->pPlayer->pInteractInfo;
		if (pInfo && pInfo->recentlyCompletedInteracts)
		{
			int i;
			for (i = eaSize(&pInfo->recentlyCompletedInteracts)-1; i >= 0; i--)
			{
				InteractionInfo* pInteract = pInfo->recentlyCompletedInteracts[i];
				if (pInteract->erTarget)
				{
					Entity* pEnt = entFromEntityRef(iPartitionIdx, pInteract->erTarget);
					if (pEnt && pEnt->pCritter && pEnt->pCritter->encounterData.pGameEncounter)
					{
						if (pEnt->pCritter->encounterData.pGameEncounter->pcName == pchEncNamePooled)
						{
							(*piRet) = true;
						}
					}
				}
			}
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action, clickable) ACMD_NAME(HasRecentlyCompletedClickableSelf, HasRecentlyCompletedInteractSelf);
ExprFuncReturnVal exprFuncHasRecentlyCompletedClickableSelf(ExprContext *pContext, ACMD_EXPR_INT_OUT piRet)
{
	GameInteractable *pInteractable = (GameInteractable *)exprContextGetVarPointerUnsafePooled(pContext, g_InteractableExprVarName);
	
	(*piRet) = false;

	if (pInteractable)
	{
		return exprFuncHasRecentlyCompletedInteractWithObject(pContext, piRet, pInteractable->pcName);
	}
	else
	{
		GameNamedVolume* pVolume = (GameNamedVolume *)exprContextGetVarPointerUnsafePooled(pContext, g_pchVolumeVarName);
		if (pVolume)
		{
			return exprFuncHasRecentlyCompletedInteractWithVolume(pContext, piRet, pVolume->pcName);
		}
		else
		{
			Entity *pPlayerEnt = (Entity *)exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
			EntityRef erTarget = 0;
			Entity* pEntTarget = exprContextGetSelfPtr(pContext);
			
			if (pEntTarget && pPlayerEnt && pEntTarget != pPlayerEnt)
			{
				ANALYSIS_ASSUME(pEntTarget != NULL);
				erTarget = entGetRef(pEntTarget);

				if (pPlayerEnt && pPlayerEnt->pPlayer && erTarget)
				{
					InteractInfo *pInfo = pPlayerEnt->pPlayer->pInteractInfo;
					if (pInfo && pInfo->recentlyCompletedInteracts)
					{
						int i;
						for (i = eaSize(&pInfo->recentlyCompletedInteracts)-1; i >= 0; i--)
						{
							InteractionInfo* pInteract = pInfo->recentlyCompletedInteracts[i];
							if (pInteract->erTarget == erTarget)
							{
								(*piRet) = true;
							}
						}
					}
				}
			}
			else
			{
				Errorf("HasRecentlyCompletedInteractSelf: Couldn't find a valid interactable, volume, or entity on the context");
				return ExprFuncReturnError;
			}
		}
	}
	return ExprFuncReturnFinished;
}

#include "EntityIterator.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

// This function hides an interactable so that it can't be seen or interacted with.
// Upon further reflection, this approach is flawed.  I will be taking this function back out.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendFXMessageToNamed);
ExprFuncReturnVal exprFuncSendFXMessageToNamed(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcClickableName, const char *pchMessage, ACMD_EXPR_ERRSTRING estrErrString)
{
	WorldScope *pScope;
	GameInteractable * pInteractable;

	pScope = exprContextGetScope(pContext);
	if (!interactable_ExprValidate(pScope, pcClickableName, estrErrString)) {
		return ExprFuncReturnError;
	}

	//
	pInteractable = interactable_GetByName(pcClickableName, pScope);
	if (pInteractable)
	{
		EntityIterator * iter = entGetIteratorSingleType(iPartitionIdx,0,0, GLOBALTYPE_ENTITYPLAYER);
		Entity *pCurrEnt;

		while(pCurrEnt = EntityIteratorGetNext(iter))
		{
			ClientCmd_SendFXMessageToNamed(pCurrEnt,pInteractable->pcNodeName,pchMessage);
		}

		EntityIteratorRelease( iter );
	}
	else if (estrErrString)
	{
		estrPrintf(estrErrString, "SendFXMessageToNamed - '%s' : no such interactable", pcClickableName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}