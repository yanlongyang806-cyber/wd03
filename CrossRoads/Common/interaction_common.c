/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CommandQueue.h"
#include "Entity.h"
#include "EntityInteraction.h"
#include "Expression.h"
#include "gameaction_common.h"
#include "interaction_common.h"
#include "mission_common.h"
#include "Player.h"
#include "ResourceManager.h"
#include "rewardCommon.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "wlGroupPropertyStructs.h"
#include "WorldGrid.h"

#ifdef GAMESERVER
#include "gslSpawnPoint.h"
#endif
#ifdef GAMECLIENT
#include "GameClientLib.h"
#endif

#include "AutoGen/interaction_common_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Static Data Initialization
// ----------------------------------------------------------------------------------

DictionaryHandle g_InteractionDefDictionary = NULL;

const char *pcPooled_AmbientJob;
const char *pcPooled_CombatJob;
const char *pcPooled_Chair;
const char *pcPooled_Clickable;
const char *pcPooled_Contact;
const char *pcPooled_CraftingStation;
const char *pcPooled_Destructible;
const char *pcPooled_Door;
const char *pcPooled_FromDefinition;
const char *pcPooled_Gate;
const char *pcPooled_NamedObject;
const char *pcPooled_Throwable;
const char *pcPooled_TeamCorral;

static char **s_eaClassNames = NULL;

ExprContext *g_pInteractionContext = NULL;
ExprContext *g_pInteractionNonPlayerContext = NULL;


// ----------------------------------------------------------------------------------
// General Use Interaction Functions
// ----------------------------------------------------------------------------------

bool interaction_IsPlayerInteracting(SA_PARAM_NN_VALID Entity *pPlayerEnt)
{
	if (pPlayerEnt->pPlayer) {
		// Player is currently interacting with something ("interact" bar is ticking down)
		if(pPlayerEnt->pPlayer->InteractStatus.bInteracting) {
			return true;
		}
	}
	return false;
}

bool interaction_IsPlayerInteractTimerFinished(SA_PARAM_NN_VALID Entity *pPlayerEnt)
{
	if (pPlayerEnt->pPlayer) {
		// Player is currently interacting with something ("interact" bar is ticking down)
		if(pPlayerEnt->pPlayer->InteractStatus.bInteracting && pPlayerEnt->pPlayer->InteractStatus.fTimerInteract <= 0) {
			return true;
		}
	}
	return false;
}

bool interaction_IsPlayerInDialog(SA_PARAM_NN_VALID Entity* playerEnt)
{
	if (SAFE_MEMBER3(playerEnt, pPlayer, pInteractInfo, pContactDialog)) {
		return true;
	}
	return false;
}

bool interaction_IsPlayerInDialogAndTeamSpokesman(SA_PARAM_NN_VALID Entity* playerEnt)
{
	ContactDialog *pDialog = SAFE_MEMBER3(playerEnt, pPlayer, pInteractInfo, pContactDialog);
	return pDialog && pDialog->bIsTeamSpokesman;
}


bool interaction_IsPlayerNearContact(SA_PARAM_NN_VALID Entity *pPlayerEnt, ContactFlags eFlags)
{
	if (pPlayerEnt->pPlayer) {
		return ( pPlayerEnt->pPlayer->InteractStatus.eNearbyContactTypes & eFlags ) != 0;
	}

	return false;
}

bool interaction_CanInteractWithContact(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID ContactInfo* pInfo)
{
	Entity* pContactEnt = entFromEntityRefAnyPartition(pInfo->entRef);
	if (entity_VerifyInteractTarget(entGetPartitionIdx(pEnt), 
									pEnt, 
									pContactEnt, 
									NULL, 
									0, 
									NULL, 
									0.0f, 
									false, 
									NULL))
	{
		return true;
	}
	return false;
}

void interaction_GetNearbyInteractableContacts(Entity* pPlayerEnt, 
											   ContactFlags eFlags, 
											   ContactInfo*** peaContacts)
{
	int i;
	InteractInfo* pInteractInfo = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);

	if (!pInteractInfo)
		return;

#ifdef GAMECLIENT
	{
		ContactFlags eOldFlags, eNewFlags;
		U32 uCurrFrame = 0;
		frameLockedTimerGetTotalFrames(gGCLState.frameLockedTimer, &uCurrFrame);
		if (pInteractInfo->uLastVerifyInteractFrame != uCurrFrame)
		{
			eaClear(&pInteractInfo->eaInteractableContacts);
			pInteractInfo->uLastVerifyContactFlags = 0;
			pInteractInfo->uLastVerifyInteractFrame = uCurrFrame;
		}
		eOldFlags = pInteractInfo->uLastVerifyContactFlags;
		eNewFlags = eFlags & ~eOldFlags;
		if (eNewFlags)
		{
			// Set the new flags
			pInteractInfo->uLastVerifyContactFlags |= eNewFlags;

			// Update the list of interactable contacts
			for (i = 0; i < eaSize(&pInteractInfo->nearbyContacts); i++)
			{
				ContactInfo* pInfo = pInteractInfo->nearbyContacts[i];
				if ((pInfo->eFlags & eNewFlags) && 
					!(pInfo->eFlags & eOldFlags) &&
					interaction_CanInteractWithContact(pPlayerEnt, pInfo))
				{
					eaPush(&pInteractInfo->eaInteractableContacts, pInfo);
				}
			}
		}
		if (eFlags)
		{
			for (i = 0; i < eaSize(&pInteractInfo->eaInteractableContacts); i++)
			{
				ContactInfo* pInfo = pInteractInfo->eaInteractableContacts[i];
				if (pInfo->eFlags & eFlags)
				{
					eaPush(peaContacts, pInfo);
				}
			}
		}
	}
#else
	if (eFlags)
	{
		for (i = 0; i < eaSize(&pInteractInfo->nearbyContacts); i++)
		{
			ContactInfo* pInfo = pInteractInfo->nearbyContacts[i];
			if ((pInfo->eFlags & eFlags) && 
				interaction_CanInteractWithContact(pPlayerEnt, pInfo))
			{
				eaPush(peaContacts, pInfo);
			}
		}
	}
#endif
}



void interaction_ClearPlayerInteractState(Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer) {
		// Clean up the player
		REMOVE_HANDLE(pPlayerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode);
		memset(&pPlayerEnt->pPlayer->InteractStatus.interactTarget,0,sizeof(InteractTarget));

		if (pPlayerEnt->pPlayer->InteractStatus.pEndInteractCommandQueue){
			CommandQueue_ExecuteAllCommands(pPlayerEnt->pPlayer->InteractStatus.pEndInteractCommandQueue);
		}

		// Clear the interact flags, so that interaction doesn't break on move, power, or damage.
		pPlayerEnt->pPlayer->InteractStatus.bInteractBreakOnDamage = false;
		pPlayerEnt->pPlayer->InteractStatus.bInteractBreakOnMove = false;
		pPlayerEnt->pPlayer->InteractStatus.bInteractBreakOnPower = false;
		zeroVec3(pPlayerEnt->pPlayer->InteractStatus.interactStartPos);
		pPlayerEnt->pPlayer->InteractStatus.fTimerInteract = pPlayerEnt->pPlayer->InteractStatus.fTimerInteractMax = 0;
		REMOVE_HANDLE(pPlayerEnt->pPlayer->InteractStatus.hInteractUseTimeMsg);

		// Make the next frame test interaction status again instead of waiting
		pPlayerEnt->pPlayer->InteractStatus.interactTargetCounter = -1;
		pPlayerEnt->pPlayer->InteractStatus.interactCheckCounter = -1;
	}
}


// ----------------------------------------------------------------------------------
// Access functions
// ----------------------------------------------------------------------------------


const char *interaction_GetEffectiveClass(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			return pDef->pEntry->pcInteractionClass;
		}
	}
	return pEntry->pcInteractionClass;
}


Expression *interaction_GetInteractCond(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->bOverrideInteract) {
				return pEntry->pInteractCond;
			} else {
				return pDefEntry->pInteractCond;
			}
		}
	} else {
		return pEntry->pInteractCond;
	}

	return NULL;
}


Expression *interaction_GetSuccessCond(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->bOverrideInteract) {
				return pEntry->pSuccessCond;
			} else {
				return pDefEntry->pSuccessCond;
			}
		}
	} else {
		return pEntry->pSuccessCond;
	}

	return NULL;
}

Expression *interaction_GetAttemptableCond(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->bOverrideInteract) {
				return pEntry->pAttemptableCond;
			} else {
				return pDefEntry->pAttemptableCond;
			}
		}
	} else {
		return pEntry->pAttemptableCond;
	}

	return NULL;
}



Expression *interaction_GetVisibleExpr(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->bOverrideVisibility) {
				return pEntry->pVisibleExpr;
			} else {
				return pDefEntry->pVisibleExpr;
			}
		}
	} else {
		return pEntry->pVisibleExpr;
	}

	return NULL;
}


const char *interaction_GetCategoryName(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->bOverrideCategoryPriority) {
				return pEntry->pcCategoryName;
			} else {
				return pDefEntry->pcCategoryName;
			}
		}
	} else {
		return pEntry->pcCategoryName;
	}

	return NULL;
}

int interaction_GetPriority(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return 0;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->bOverrideCategoryPriority) {
				return pEntry->iPriority;
			} else {
				return pDefEntry->iPriority;
			}
		}
	} else {
		return pEntry->iPriority;
	}

	return 0;
}

bool interaction_GetAutoExecute(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return false;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			return SAFE_MEMBER(pDefEntry, bAutoExecute);
		}
	} else {
		return pEntry->bAutoExecute;
	}

	return false;
}

WorldActionInteractionProperties *interaction_GetActionProperties(WorldInteractionPropertyEntry *pEntry)
{
	WorldActionInteractionProperties *pProps = NULL;

	if (!pEntry) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->pActionProperties) {
				pProps = pEntry->pActionProperties;
			} else {
				pProps = pDefEntry->pActionProperties;
			}
		}
	} else {
		pProps = pEntry->pActionProperties;
	}

	PERFINFO_AUTO_STOP();

	return pProps;
}

WorldAnimationInteractionProperties *interaction_GetAnimationProperties(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->pAnimationProperties) {
				return pEntry->pAnimationProperties;
			} else {
				return pDefEntry->pAnimationProperties;
			}
		}
	} else {
		return pEntry->pAnimationProperties;
	}

	return NULL;
}

WorldContactInteractionProperties *interaction_GetContactProperties(WorldInteractionPropertyEntry *pEntry)
{
	WorldContactInteractionProperties *pProps = NULL;

	if (!pEntry) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pDefEntry->pcInteractionClass == pcPooled_Contact) {
				if (pEntry->pContactProperties) {
					pProps = pEntry->pContactProperties;
				} else {
					pProps = pDefEntry->pContactProperties;
				}
			}
		}
	} else if (pEntry->pcInteractionClass == pcPooled_Contact) {
		pProps = pEntry->pContactProperties;
	}

	PERFINFO_AUTO_STOP();

	return pProps;
}

ContactDef *interaction_GetContactDef(WorldInteractionPropertyEntry *pEntry)
{
	WorldContactInteractionProperties *pContactProps = interaction_GetContactProperties(pEntry);
	if (pContactProps){
		return GET_REF(pContactProps->hContactDef);
	}
	return NULL;
}

WorldChairInteractionProperties *interaction_GetChairProperties(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_Chair) {
		return pEntry->pChairProperties;
	}

	return NULL;
}

WorldCraftingInteractionProperties *interaction_GetCraftingProperties(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pDefEntry->pcInteractionClass == pcPooled_CraftingStation) {
				if (pEntry->pCraftingProperties) {
					return pEntry->pCraftingProperties;
				} else {
					return pDefEntry->pCraftingProperties;
				}
			}
		}
	} else if (pEntry->pcInteractionClass == pcPooled_CraftingStation) {
		return pEntry->pCraftingProperties;
	}

	return NULL;
}

WorldDestructibleInteractionProperties *interaction_GetDestructibleProperties(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pDefEntry->pcInteractionClass == pcPooled_Destructible) {
				if (pEntry->pDestructibleProperties) {
					return pEntry->pDestructibleProperties;
				} else {
					return pDefEntry->pDestructibleProperties;
				}
			}
		}
	} else if (pEntry->pcInteractionClass == pcPooled_Destructible) {
		return pEntry->pDestructibleProperties;
	}

	return NULL;
}

WorldDoorInteractionProperties *interaction_GetDoorProperties(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pDefEntry->pcInteractionClass == pcPooled_Door) {
				if (pEntry->pDoorProperties) {
					return pEntry->pDoorProperties;
				} else {
					return pDefEntry->pDoorProperties;
				}
			}
		}
	} else if (pEntry->pcInteractionClass == pcPooled_Door) {
		return pEntry->pDoorProperties;
	}

	return NULL;
}

WorldGateInteractionProperties *interaction_GetGateProperties(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_Gate) {
		return pEntry->pGateProperties;
	}

	return NULL;
}

WorldRewardInteractionProperties *interaction_GetRewardProperties(WorldInteractionPropertyEntry *pEntry)
{
	WorldRewardInteractionProperties *pProps = NULL;

	if (!pEntry) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->pRewardProperties) {
				pProps = pEntry->pRewardProperties;
			} else {
				pProps = pDefEntry->pRewardProperties;
			}
		}
	} else {
		pProps = pEntry->pRewardProperties;
	}

	PERFINFO_AUTO_STOP();
	return pProps;
}

WorldSoundInteractionProperties *interaction_GetSoundProperties(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->pSoundProperties) {
				return pEntry->pSoundProperties;
			} else {
				return pDefEntry->pSoundProperties;
			}
		}
	} else {
		return pEntry->pSoundProperties;
	}

	return NULL;
}

WorldTextInteractionProperties *interaction_GetTextProperties(WorldInteractionPropertyEntry *pEntry)
{
	WorldTextInteractionProperties *pProps = NULL;

	if (!pEntry) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->pTextProperties) {
				pProps = pEntry->pTextProperties;
			} else {
				pProps = pDefEntry->pTextProperties;
			}
		}
	} else {
		pProps = pEntry->pTextProperties;
	}

	PERFINFO_AUTO_STOP();
	return pProps;
}

WorldTimeInteractionProperties *interaction_GetTimeProperties(WorldInteractionPropertyEntry *pEntry)
{
	WorldTimeInteractionProperties *pProps = NULL;

	if (!pEntry) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->pTimeProperties) {
				pProps = pEntry->pTimeProperties;
			} else {
				pProps = pDefEntry->pTimeProperties;
			}
		}
	} else {
		pProps = pEntry->pTimeProperties;
	}

	PERFINFO_AUTO_STOP();
	return pProps;
}

WorldMotionInteractionProperties *interaction_GetMotionProperties(WorldInteractionPropertyEntry *pEntry)
{
	if (!pEntry) {
		return NULL;
	}
	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pEntry->pMotionProperties) {
				return pEntry->pMotionProperties;
			} else {
				return pDefEntry->pMotionProperties;
			}
		}
	} else {
		return pEntry->pMotionProperties;
	}

	return NULL;
}

bool interaction_EntryGetExclusive(WorldInteractionPropertyEntry *pEntry, bool bIsNode)
{
	// new exclusion specification
	if (pEntry->bExclusiveInteraction || pEntry->bUseExclusionFlag)
		return pEntry->bExclusiveInteraction;

	// old exclusion rules
	else if (bIsNode)
		return !(pEntry->pcInteractionClass == pcPooled_CraftingStation || pEntry->pcInteractionClass == pcPooled_Contact);
	else
		return false;
}

F32 interaction_GetCooldownTime(WorldInteractionPropertyEntry *pEntry)
{
	WorldTimeInteractionProperties *pTimeProps = interaction_GetTimeProperties(pEntry);
	F32 fCooldownTime = 0;
	if (pTimeProps) {
		switch(pTimeProps->eCooldownTime) {
			xcase WorldCooldownTime_None:	fCooldownTime = 0;
			xcase WorldCooldownTime_Short:	fCooldownTime = INTERACTION_COOLDOWN_SHORT;
			xcase WorldCooldownTime_Medium: fCooldownTime = INTERACTION_COOLDOWN_MEDIUM;
			xcase WorldCooldownTime_Long:	fCooldownTime = INTERACTION_COOLDOWN_LONG;
			xcase WorldCooldownTime_Custom: fCooldownTime = pTimeProps->fCustomCooldownTime;
			xdefault:	assertmsg(0, "Unsupported cooldown time value.");
		}
	}
	return fCooldownTime;
}

bool interaction_HasTag(WorldInteractionProperties *pProps, const char *pchPooledTag)
{
	return (eaFind(&pProps->eaInteractionTypeTag, pchPooledTag) >= 0);
}

void interaction_FixupMessages(WorldInteractionPropertyEntry *pEntry, const char *pcScope, const char *pcBaseMessageKey, const char *pcSubKey, int iIndex)
{
	char buf1[1024];
	char buf2[1024];

	if (pEntry->pDestructibleProperties) {
		sprintf(buf1, "%s.InteractableProps.%s.%d.DestructibleName", pcBaseMessageKey, pcSubKey, 0);
		sprintf(buf2, "This is the display name for the destructible");
		langFixupMessage(pEntry->pDestructibleProperties->displayNameMsg.pEditorCopy, buf1, buf2, pcScope);
	}

	if (pEntry->pTextProperties) {
		sprintf(buf1, "%s.InteractableProps.%s.%d.UsabilityText", pcBaseMessageKey, pcSubKey, iIndex);
		sprintf(buf2, "This is prompt displayed in the HUD when the player can interact with this interactable but it has some usability requirements.");
		langFixupMessage(pEntry->pTextProperties->usabilityOptionText.pEditorCopy, buf1, buf2, pcScope);
		
		sprintf(buf1, "%s.InteractableProps.%s.%d.InteractText", pcBaseMessageKey, pcSubKey, iIndex);
		sprintf(buf2, "This is prompt displayed in the HUD when the player can interact with this interactable");
		langFixupMessage(pEntry->pTextProperties->interactOptionText.pEditorCopy, buf1, buf2, pcScope);

		sprintf(buf1, "%s.InteractableProps.%s.%d.DetailText", pcBaseMessageKey, pcSubKey, iIndex);
		sprintf(buf2, "This is a game-specific message used by the UI in any way it chooses");
		langFixupMessage(pEntry->pTextProperties->interactDetailText.pEditorCopy, buf1, buf2, pcScope);

		sprintf(buf1, "%s.InteractableProps.%s.%d.SuccessText", pcBaseMessageKey, pcSubKey, iIndex);
		sprintf(buf2, "This is prompt displayed when the player succeeds at interacting");
		langFixupMessage(pEntry->pTextProperties->successConsoleText.pEditorCopy, buf1, buf2, pcScope);

		sprintf(buf1, "%s.InteractableProps.%s.%d.FailureText", pcBaseMessageKey, pcSubKey, iIndex);
		sprintf(buf2, "This is prompt displayed when the player fails to interact");
		langFixupMessage(pEntry->pTextProperties->failureConsoleText.pEditorCopy, buf1, buf2, pcScope);
	}

	if (pEntry->pActionProperties) {
		int i;
		for(i=eaSize(&pEntry->pActionProperties->successActions.eaActions)-1; i>=0; --i) {
			WorldGameActionProperties *pAction = pEntry->pActionProperties->successActions.eaActions[i];
			sprintf(buf1, "%s.InteractableProps.%s.%d.GameAction", pcBaseMessageKey, pcSubKey, iIndex);
			gameaction_FixupMessages(pAction, pcScope, buf1, i, false);
		}
	}
	
	if (pEntry->pDoorProperties) {
		int i;
		for(i=eaSize(&pEntry->pDoorProperties->eaVariableDefs)-1; i>=0; --i) {
			WorldVariableDef *pVarDef = pEntry->pDoorProperties->eaVariableDefs[i];
			if ((pVarDef->eType == WVAR_MESSAGE) && pVarDef->pSpecificValue) {
				DisplayMessage *pDispMsg = &pVarDef->pSpecificValue->messageVal;
				if (pDispMsg && pDispMsg->pEditorCopy) {
					sprintf(buf1, "%s.InteractableProps.%s.%d.doorvars.%s.id%d", pcBaseMessageKey, pcSubKey, iIndex, pVarDef->pcName, i);
					sprintf(buf2, "This is the \"%s\" value for a MissionDef door override.", pVarDef->pcName);
					langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
				}
			}
		}
	}
}


void interaction_CleanProperties(WorldInteractionPropertyEntry *pEntry)
{
	int i;

	exprClean(pEntry->pInteractCond);
	exprClean(pEntry->pSuccessCond);
	exprClean(pEntry->pAttemptableCond);
	exprClean(pEntry->pVisibleExpr);
	if (pEntry->pActionProperties) {
		exprClean(pEntry->pActionProperties->pAttemptExpr);
		exprClean(pEntry->pActionProperties->pFailureExpr);
		exprClean(pEntry->pActionProperties->pInterruptExpr);
		exprClean(pEntry->pActionProperties->pNoLongerActiveExpr);
		exprClean(pEntry->pActionProperties->pSuccessExpr);
		gameactionblock_Clean(&pEntry->pActionProperties->successActions);
	}
	if (pEntry->pGateProperties) {
		exprClean(pEntry->pGateProperties->pCritterUseCond);
	}

	if (pEntry->pDoorProperties) {
		worldVariableDefCleanExpressions(&pEntry->pDoorProperties->doorDest);

		for(i=eaSize(&pEntry->pDoorProperties->eaVariableDefs)-1; i>=0; --i)
		{
			worldVariableDefCleanExpressions(pEntry->pDoorProperties->eaVariableDefs[i]);
		}
	}
}


// ----------------------------------------------------------------------------------
// Validation Logic
// ----------------------------------------------------------------------------------

ExprFuncTable* interactable_CreateExprFuncTable(void)
{
	static ExprFuncTable* s_interactableFuncTable = NULL;
	if (!s_interactableFuncTable) {
		s_interactableFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_interactableFuncTable, "clickable");
		exprContextAddFuncsToTableByTag(s_interactableFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_interactableFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_interactableFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_interactableFuncTable, "event_count");
		exprContextAddFuncsToTableByTag(s_interactableFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_interactableFuncTable, "player");
		exprContextAddFuncsToTableByTag(s_interactableFuncTable, "PTECharacter");
		exprContextAddFuncsToTableByTag(s_interactableFuncTable, "util");
	}
	return s_interactableFuncTable;
}


ExprFuncTable* interactable_CreateNonPlayerExprFuncTable(void)
{
	static ExprFuncTable* s_nonPlayerInteractableFuncTable = NULL;
	if (!s_nonPlayerInteractableFuncTable) {
		s_nonPlayerInteractableFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_nonPlayerInteractableFuncTable, "clickable");
		exprContextAddFuncsToTableByTag(s_nonPlayerInteractableFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_nonPlayerInteractableFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_nonPlayerInteractableFuncTable, "event_count");
		exprContextAddFuncsToTableByTag(s_nonPlayerInteractableFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_nonPlayerInteractableFuncTable, "util");
	}
	return s_nonPlayerInteractableFuncTable;
}


void interaction_ValidatePropertyEntry(WorldInteractionPropertyEntry *pEntry, WorldScope *pScope, const char *pcFilename, const char *pcObjectType, const char *pcObjectName)
{
	WorldAnimationInteractionProperties *pAnimationProps = interaction_GetAnimationProperties(pEntry);
	WorldActionInteractionProperties *pActionProps = interaction_GetActionProperties(pEntry);
	WorldContactInteractionProperties *pContactProps = interaction_GetContactProperties(pEntry);
	WorldDestructibleInteractionProperties *pDestructibleProps = interaction_GetDestructibleProperties(pEntry);
	WorldDoorInteractionProperties *pDoorProps = interaction_GetDoorProperties(pEntry);
	WorldRewardInteractionProperties *pRewardProps = interaction_GetRewardProperties(pEntry);
	WorldTextInteractionProperties *pTextProps = interaction_GetTextProperties(pEntry);
	const char *pcClass = interaction_GetEffectiveClass(pEntry);
	const char *pcCategoryName = interaction_GetCategoryName(pEntry);
	int i;

	if (!pcClass) {
		ErrorFilenamef(pcFilename, "%s '%s' has no interaction class", pcObjectType, pcObjectName);
	} else {
		if (!s_eaClassNames) {
			wlInteractionGetClassNames(&s_eaClassNames);
		}
		for(i=eaSize(&s_eaClassNames)-1; i>=0; --i) {
			if (stricmp(s_eaClassNames[i], pcClass) == 0) {
				break;
			}
		}
		if (i<0) {
			ErrorFilenamef(pcFilename, "%s '%s' has invalid interaction class '%s'", pcObjectType, pcObjectName, pcClass);
		}
	}

	// Check the category name
	if (pcCategoryName) {
		for(i=eaSize(&g_eaOptionalActionCategoryDefs)-1; i>=0; --i) {
			if (g_eaOptionalActionCategoryDefs[i]->pcName == pcCategoryName) { // Can do this since both are pooled
				break;
			}
		}
		if (i < 0) {
			ErrorFilenamef(pcFilename, "%s '%s' refers to unknown optional action category '%s'.", pcObjectType, pcObjectName, pcCategoryName);
		}
	}

	if(pAnimationProps) {
		if (!GET_REF(pAnimationProps->hInteractAnim) && REF_STRING_FROM_HANDLE(pAnimationProps->hInteractAnim)) {
			ErrorFilenamef(pcFilename, "%s '%s' refers to a non-existent animation '%s'", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pAnimationProps->hInteractAnim));
		}
	}

	if (pActionProps) {
		// Call to game action code to validate the actions
		if (!gameaction_ValidateActions(&pActionProps->successActions.eaActions, zmapGetName(NULL), NULL, NULL, true, pcFilename)) {
			ErrorFilenamef(pcFilename, "%s '%s' reported validation failures on its game actions", pcObjectType, pcObjectName);
		}
		if (stricmp(pcObjectType, "Volume") != 0)
		{
			for(i=eaSize(&pActionProps->successActions.eaActions)-1; i>=0; --i) {
				WorldGameActionProperties *pAction = pActionProps->successActions.eaActions[i];
				if (pAction->eActionType == WorldGameActionType_Warp && nullStr(SAFE_MEMBER2(pAction->pWarpProperties, warpDest.pSpecificValue, pcZoneMap)) &&
					(stricmp_safe(SAFE_MEMBER2(pAction->pWarpProperties, warpDest.pSpecificValue, pcStringVal), "MissionReturn") != 0)) {
					ErrorFilenamef(pcFilename, "%s '%s' has a game action set up to warp a player within a map.  Interactions that warp players should be set up as Door type and use the door properties in order to ensure proper AI behavior.", pcObjectType, pcObjectName);
				}
			}
		}
	}

	if (pContactProps) {
		if (!GET_REF(pContactProps->hContactDef)) {
			if (REF_STRING_FROM_HANDLE(pContactProps->hContactDef)) {
				ErrorFilenamef(pcFilename, "%s '%s' is of Contact type, but references unknown Contact (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pContactProps->hContactDef));
			} else {
				ErrorFilenamef(pcFilename, "%s '%s' is of Contact type, but no valid Contact is specified.", pcObjectType, pcObjectName);
			}
		}
	}

	if (pDestructibleProps) {
		if (!GET_REF(pDestructibleProps->hCritterDef)) {
			if (REF_STRING_FROM_HANDLE(pDestructibleProps->hCritterDef)) {
				ErrorFilenamef(pcFilename, "%s '%s' is of Destructible type, but references unknown Critter Def (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pDestructibleProps->hCritterDef));
			} else {
				ErrorFilenamef(pcFilename, "%s '%s' is of Destructible type, but no valid Critter Def is specified.", pcObjectType, pcObjectName);
			}
		}
		if (IS_HANDLE_ACTIVE(pDestructibleProps->hCritterOverrideDef) && !GET_REF(pDestructibleProps->hCritterOverrideDef)) {
			ErrorFilenamef(pcFilename, "%s '%s' is of Destructible type, but references unknown critter override def (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pDestructibleProps->hCritterOverrideDef));
		}

		if (IS_HANDLE_ACTIVE(pDestructibleProps->hOnDeathPowerDef) && !GET_REF(pDestructibleProps->hOnDeathPowerDef)) {
			ErrorFilenamef(pcFilename, "%s '%s' is of Destructible type, but references unknown on death Power def (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pDestructibleProps->hOnDeathPowerDef));
		}

		if (IS_HANDLE_ACTIVE(pDestructibleProps->displayNameMsg.hMessage) && !GET_REF(pDestructibleProps->displayNameMsg.hMessage)) {
			ErrorFilenamef(pcFilename, "%s '%s' references unknown message (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pDestructibleProps->displayNameMsg.hMessage));
		}

		// Should we detect destructibles of respawn time zero on static maps?
		// A simple test fails since the designers sometimes do explicit spawning.
	}

	if (pDoorProps) {
		if(pDoorProps->eDoorType == WorldDoorType_QueuedInstance) {
			if (!GET_REF(pDoorProps->hQueueDef)) {
				if (REF_STRING_FROM_HANDLE(pDoorProps->hQueueDef)) {
					ErrorFilenamef(pcFilename, "%s '%s' is a Door of queue type, but references unknown Queue Def (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pDoorProps->hQueueDef));
				} else {
					ErrorFilenamef(pcFilename, "%s '%s' is a Door of queue type, but no valid Queue Def is specified.", pcObjectType, pcObjectName);
				}
			}

			if (pDoorProps->doorDest.eDefaultType == WVARDEF_SPECIFY_DEFAULT && pDoorProps->doorDest.pSpecificValue) {
				ErrorFilenamef(pcFilename, "%s '%s' is a Door of queue type, but has SpecificValue data, which is not used by queue type Doors.", pcObjectType, pcObjectName);
			}
		} else if(pDoorProps->eDoorType != WorldDoorType_Keyed && pDoorProps->eDoorType != WorldDoorType_JoinTeammate){
			if (pDoorProps->doorDest.eType != WVAR_MAP_POINT) {
				ErrorFilenamef(pcFilename, "%s: %s -- Door destination is not a MAP_POINT.  This is an internal editor error.", pcObjectType, pcObjectName);
			} else {
				char buffer[ 256 ];
				sprintf( buffer, "%s '%s'", pcObjectType, pcObjectName);
				worldVariableValidateDef(&pDoorProps->doorDest, &pDoorProps->doorDest, pcObjectName, pcFilename);
			}
			
			if (pDoorProps->doorDest.eDefaultType == WVARDEF_SPECIFY_DEFAULT && pDoorProps->doorDest.pSpecificValue) {
				char* pcZoneMap = pDoorProps->doorDest.pSpecificValue->pcZoneMap;
				bool bIsMissionReturn = (stricmp_safe(pDoorProps->doorDest.pSpecificValue->pcStringVal, "MissionReturn") == 0);
				char* pcSpawnPoint = pDoorProps->doorDest.pSpecificValue->pcStringVal;
				
				ZoneMapInfo *pZoneMap = NULL;
				char buf[1024];

				// A door must have either a map or a target unless it is of type NONE
				if (!pcZoneMap && !pcSpawnPoint && pDoorProps->eDoorType != WorldDoorType_None) {
					ErrorFilenamef(pcFilename, "%s '%s' is of Door type, but does not have either a map name or spawn target name defined.", pcObjectType, pcObjectName);
				}
				// TomY TODO this doesn't validate UGC maps correctly
				if (pcZoneMap && !resHasNamespace(pcZoneMap)) {
					pZoneMap = worldGetZoneMapByPublicName(pcZoneMap);
					if (!pZoneMap) {
						ErrorFilenamef(pcFilename, "%s '%s' references non-existent map name '%s'.", pcObjectType, pcObjectName, pcZoneMap);
					}
				}

				// If no map name, but does have a target, then validate spawn point on current map
#ifdef GAMESERVER
				if (!pcZoneMap && pcSpawnPoint && 
					!spawnpoint_GetByNameForSpawning(pcSpawnPoint, pScope) &&
					(stricmp("MissionReturn", pcSpawnPoint) != 0) &&
					(stricmp("StartSpawn", pcSpawnPoint) != 0)) {
					ErrorFilenamef(pcFilename, "%s '%s' references non-existent spawn point (or logial group with spawn points) '%s' on the current map.", pcObjectType, pcObjectName, pcSpawnPoint);
				}
#endif
				if (pcSpawnPoint && (strnicmp(pcSpawnPoint, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0)) {
					ErrorFilenamef(pcFilename, "%s '%s' references temporary named spawn point (or logical group) '%s'.  You need to give the spawn point a real name.", pcObjectType, pcObjectName, pcSpawnPoint);
				}

				// If this is a per-player door, it must use a Map Type of OWNED
				if (pDoorProps->bPerPlayer || pDoorProps->bSinglePlayer) {
					if (!pcZoneMap || zmapInfoGetMapType(worldGetZoneMapByPublicName(pcZoneMap)) != ZMTYPE_OWNED){
						ErrorFilenamef(pcFilename, "%s '%s' is a per-player door, but it doesn't reference a map of type OWNED (references %s).", pcObjectType, pcObjectName, pcSpawnPoint);
					}
				}
				// An OWNED map must use per-player doors
				if (pcZoneMap && zmapInfoGetMapType(worldGetZoneMapByPublicName(pcZoneMap)) == ZMTYPE_OWNED){
					if (!pDoorProps->bPerPlayer && !pDoorProps->bSinglePlayer) {
						ErrorFilenamef(pcFilename, "%s '%s' reference an OWNED map (%s), but is not a per-player door.", pcObjectType, pcObjectName, pcZoneMap);
					}
				}

				if (pZoneMap) {
					sprintf(buf, "%s '%s' (using map variable)", pcObjectName, pcObjectType);
					zmapInfoValidateVariableDefs(pZoneMap, pDoorProps->eaVariableDefs, buf, pcFilename);
				} else if (eaSize(&pDoorProps->eaVariableDefs) && !pcZoneMap && !bIsMissionReturn) {
					ErrorFilenamef(pcFilename, "%s '%s' is attempting to set variables on the door, but they are ignored because the door does not leave the map.", pcObjectType, pcObjectName);
				}
			}
		}
		if (pDoorProps->bCustomMotion){
			ErrorFilenamef(pcFilename, "%s '%s' has custom motion properies; these are no longer supported and must be converted to use the generic interactable motion properties.", pcObjectType, pcObjectName);
		}
	}

	if (pRewardProps) {
		if (!GET_REF(pRewardProps->hRewardTable)) {
			if (IS_HANDLE_ACTIVE(pRewardProps->hRewardTable))
				ErrorFilenamef(pcFilename, "%s '%s' has reward data with invalid reward table \"%s\" specified.", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pRewardProps->hRewardTable));
			else
				ErrorFilenamef(pcFilename, "%s '%s' has reward data but no valid reward table is specified.", pcObjectType, pcObjectName);
		} else {
			RewardTable *pTable = GET_REF(pRewardProps->hRewardTable);

			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_Direct, RewardContextType_Clickable)) {
				ErrorFilenamef(pcFilename, "%s %s references non-numeric Direct reward table %s.  Use GiveItem actions instead.", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pRewardProps->hRewardTable));
			}

			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_Choose, RewardContextType_Clickable)) {
				ErrorFilenamef(pcFilename, "%s %s references non-numeric Choose reward table %s.", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pRewardProps->hRewardTable));
			}
		}
	}

	if (pTextProps) {
		if (IS_HANDLE_ACTIVE(pTextProps->usabilityOptionText.hMessage) && !GET_REF(pTextProps->usabilityOptionText.hMessage)) {
			ErrorFilenamef(pcFilename, "%s '%s' references unknown message (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pTextProps->usabilityOptionText.hMessage));
		}

		if (IS_HANDLE_ACTIVE(pTextProps->interactOptionText.hMessage) && !GET_REF(pTextProps->interactOptionText.hMessage)) {
			ErrorFilenamef(pcFilename, "%s '%s' references unknown message (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pTextProps->interactOptionText.hMessage));
		}

		if (IS_HANDLE_ACTIVE(pTextProps->interactDetailText.hMessage) && !GET_REF(pTextProps->interactDetailText.hMessage)) {
			ErrorFilenamef(pcFilename, "%s '%s' references unknown message (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pTextProps->interactDetailText.hMessage));
		}

		if (IS_HANDLE_ACTIVE(pTextProps->successConsoleText.hMessage) && !GET_REF(pTextProps->successConsoleText.hMessage)) {
			ErrorFilenamef(pcFilename, "%s '%s' references unknown message (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pTextProps->successConsoleText.hMessage));
		}
		if (IS_HANDLE_ACTIVE(pTextProps->failureConsoleText.hMessage) && !GET_REF(pTextProps->failureConsoleText.hMessage)) {
			ErrorFilenamef(pcFilename, "%s '%s' references unknown message (%s)", pcObjectType, pcObjectName, REF_STRING_FROM_HANDLE(pTextProps->failureConsoleText.hMessage));
		}
	}
}


void interaction_InitPropertyEntry(WorldInteractionPropertyEntry *pEntry, ExprContext *pContext, const char *pcFilename, const char *pcObjectType, const char *pcObjectName, bool bEntSpecificVisibility)
{
	// This function is called on every entry to generate its expressions
	// It should only look at the current entry and ignore any "FromDefinition" behaviors
	// The fields on the definition are handled in separate calls to this function
	WorldActionInteractionProperties *pActionProps = pEntry->pActionProperties;
	WorldDoorInteractionProperties *pDoorProps = interaction_GetDoorProperties(pEntry);
	WorldGateInteractionProperties *pGateProps = pEntry->pGateProperties;
	Expression *pVisibleExpr = pEntry->pVisibleExpr;
	Expression *pInteractCond = pEntry->pInteractCond;
	Expression *pSuccessCond = pEntry->pSuccessCond;
	Expression *pAttemptableCond = pEntry->pAttemptableCond;

	if (pVisibleExpr) {
		// Note that "visible_expr" uses the world layer expression context, not the interactable one
		if (bEntSpecificVisibility) {
			if (!exprGenerate(pVisibleExpr, pContext)) {
				ErrorFilenamef(pcFilename, "%s '%s' has an invalid Visible Expression.", pcObjectType, pcObjectName);
			}
		} else {
			if (!exprGenerate(pVisibleExpr, g_pInteractionNonPlayerContext)) {
				ErrorFilenamef(pcFilename, "%s '%s' has an invalid Visible Expression.", pcObjectType, pcObjectName);
			}
		}
	}

	if (pInteractCond) {
		if (!exprGenerate(pInteractCond, pContext)) {
			ErrorFilenamef(pcFilename, "%s '%s' has an invalid Interact Expression.", pcObjectType, pcObjectName);
		}
	}

	if (pSuccessCond) {
		if (!exprGenerate(pSuccessCond, pContext)) {
			ErrorFilenamef(pcFilename, "%s '%s' has an invalid Success Expression.", pcObjectType, pcObjectName);
		}
	}

	if (pAttemptableCond) {
		if (!exprGenerate(pAttemptableCond, pContext)) {
			ErrorFilenamef(pcFilename, "%s '%s' has an invalid Usable Expression.", pcObjectType, pcObjectName);
		}
	}

	if (pGateProps) {
		if (pGateProps->pCritterUseCond) {
			if (!exprGenerate(pGateProps->pCritterUseCond, pContext)) {
				ErrorFilenamef(pcFilename, "%s '%s' has an invalid gate Critter Use Condition expression.", pcObjectType, pcObjectName);
			}
		}
	}

	if (pActionProps) {
		if (pActionProps->pAttemptExpr) {
			if (!exprGenerate(pActionProps->pAttemptExpr, pContext)) {
				ErrorFilenamef(pcFilename, "%s '%s' has an invalid action Attempt Expression.", pcObjectType, pcObjectName);
			}
		}
		if (pActionProps->pSuccessExpr) {
			if (!exprGenerate(pActionProps->pSuccessExpr, pContext)) {
				ErrorFilenamef(pcFilename, "%s '%s' has an invalid action Success Expression.", pcObjectType, pcObjectName);
			}
		}
		if (pActionProps->pFailureExpr) {
			if (!exprGenerate(pActionProps->pFailureExpr, pContext)) {
				ErrorFilenamef(pcFilename, "%s '%s' has an invalid action Fail Expression.", pcObjectType, pcObjectName);
			}
		}
		if (pActionProps->pInterruptExpr) {
			if (!exprGenerate(pActionProps->pInterruptExpr, pContext)) {
				ErrorFilenamef(pcFilename, "%s '%s' has an invalid action Interrupt Expression.", pcObjectType, pcObjectName);
			}
		}
		if (pActionProps->pNoLongerActiveExpr) {
			if (!exprGenerate(pActionProps->pNoLongerActiveExpr, g_pInteractionNonPlayerContext)) {
				ErrorFilenamef(pcFilename, "%s '%s' has an invalid action No Longer Active Expression.", pcObjectType, pcObjectName);
			}
		}
		if (pActionProps->pCooldownExpr) {
			if (!exprGenerate(pActionProps->pCooldownExpr, g_pInteractionNonPlayerContext)) {
				ErrorFilenamef(pcFilename, "%s '%s' has an invalid action Cooldown Expression.", pcObjectType, pcObjectName);
			}
		}
		gameaction_GenerateActions(&pActionProps->successActions.eaActions, NULL, pcFilename);
		gameaction_GenerateActions(&pActionProps->failureActions.eaActions, NULL, pcFilename);
	}

	if (pDoorProps) {
		int i;
		if(pDoorProps->eDoorType != WorldDoorType_Keyed){
			worldVariableDefGenerateExpressions(&pDoorProps->doorDest, pcObjectName, pcFilename);

			if(pDoorProps->eaVariableDefs) {
				for(i=0; i<eaSize(&pDoorProps->eaVariableDefs); i++)
				{
					worldVariableDefGenerateExpressions(pDoorProps->eaVariableDefs[i], pcObjectName, pcFilename);
				}
			}
		}
	}
}


bool interactiondef_Validate(InteractionDef *pDef)
{
	ExprContext* exprContext = g_pInteractionContext;

	if(!pDef) {
		return false;
	}

	if(IsServer() && pDef->pEntry)
	{
		interaction_InitPropertyEntry(pDef->pEntry, exprContext, pDef->pcFilename, "InteractionDef", pDef->pcName, false);
	}

	// Make sure the interaction type jives with the class
	if (pDef->pEntry && pDef->pEntry->pcInteractionClass == pcPooled_Gate && pDef->eType != InteractionDefType_Node)
	{
		ErrorFilenamef(pDef->pcFilename, "Interaction def '%s' can only apply gate properties to node interactions.", pDef->pcName);
		return false;
	}

	return true;
}


// ----------------------------------------------------------------------------------
// Dictionary Management
// ----------------------------------------------------------------------------------

void interactiondef_LoadDefs(void)
{
	static int bLoadedOnce = false;

	if (IsServer() && !bLoadedOnce) {
		bLoadedOnce = true;
		resLoadResourcesFromDisk(g_InteractionDefDictionary, "defs/interactiondef", ".interactiondef", NULL, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
	}
}


static int interactionDefResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, InteractionDef *pDef, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename((char**)&pDef->pcFilename, "defs/interactiondef", pDef->pcScope, pDef->pcName, "interactiondef");
		return VALIDATE_HANDLED;	

		xcase RESVALIDATE_POST_BINNING:
			interactiondef_Validate(pDef);
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


AUTO_RUN;
int RegisterInteractionDefDictionary(void)
{
	g_InteractionDefDictionary = RefSystem_RegisterSelfDefiningDictionary("InteractionDef", false, parse_InteractionDef, true, true, NULL);

	resDictManageValidation(g_InteractionDefDictionary, interactionDefResValidateCB);

	if (IsServer()) {
		resDictProvideMissingResources(g_InteractionDefDictionary);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_InteractionDefDictionary, ".name", ".scope", NULL, NULL, NULL);
		}
	} else if (IsClient()) {
		resDictRequestMissingResources(g_InteractionDefDictionary, 8, false, resClientRequestSendReferentCommand);
	}
	resDictProvideMissingRequiresEditMode(g_InteractionDefDictionary);

	return 1;
}


AUTO_RUN;
void interaction_InitCommon(void)
{
	// Initialize compare strings
	pcPooled_Chair = allocAddString("Chair");
	pcPooled_Clickable = allocAddString("Clickable");
	pcPooled_Contact = allocAddString("Contact");
	pcPooled_CraftingStation = allocAddString("CraftingStation");
	pcPooled_Destructible = allocAddString("Destructible");
	pcPooled_Door = allocAddString("Door");
	pcPooled_FromDefinition = allocAddString("FromDefinition");
	pcPooled_Gate = allocAddString("Gate");
	pcPooled_NamedObject = allocAddString("NamedObject");
	pcPooled_Throwable = allocAddString("Throwable");
	pcPooled_AmbientJob = allocAddString("Ambientjob");
	pcPooled_CombatJob = allocAddString("CombatJob");
	pcPooled_TeamCorral = allocAddString("TeamCorral");

	// Set up the interaction expression context
	g_pInteractionContext = exprContextCreate();
	exprContextSetFuncTable(g_pInteractionContext, interactable_CreateExprFuncTable());
	exprContextSetAllowRuntimePartition(g_pInteractionContext);
	exprContextSetAllowRuntimeSelfPtr(g_pInteractionContext);

	g_pInteractionNonPlayerContext = exprContextCreate();
	exprContextSetFuncTable(g_pInteractionNonPlayerContext, interactable_CreateNonPlayerExprFuncTable());
	exprContextSetAllowRuntimePartition(g_pInteractionNonPlayerContext);
	// Not allowing Self pointer
}

static bool interaction_GetAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
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
		return true;
	}
	return false;
}

void interaction_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	InteractionDef *pInteractionDef;
	ResourceIterator rI;

	*ppcType = strdup("InteractionDef");

	resInitIterator(g_InteractionDefDictionary, &rI);
	while (resIteratorGetNext(&rI, NULL, &pInteractionDef))
	{
		bool bResourceHasAudio = false;

		if (pInteractionDef->pEntry &&
			pInteractionDef->pEntry->pSoundProperties)
		{
			bResourceHasAudio |= interaction_GetAudioAssets_HandleString(pInteractionDef->pEntry->pSoundProperties->pchAttemptSound,	peaStrings);
			bResourceHasAudio |= interaction_GetAudioAssets_HandleString(pInteractionDef->pEntry->pSoundProperties->pchFailureSound,	peaStrings);
			bResourceHasAudio |= interaction_GetAudioAssets_HandleString(pInteractionDef->pEntry->pSoundProperties->pchInterruptSound,	peaStrings);
			bResourceHasAudio |= interaction_GetAudioAssets_HandleString(pInteractionDef->pEntry->pSoundProperties->pchSuccessSound,	peaStrings);

			bResourceHasAudio |= interaction_GetAudioAssets_HandleString(pInteractionDef->pEntry->pSoundProperties->pchMovementReturnEndSound,	peaStrings);
			bResourceHasAudio |= interaction_GetAudioAssets_HandleString(pInteractionDef->pEntry->pSoundProperties->pchMovementReturnStartSound,peaStrings);
			bResourceHasAudio |= interaction_GetAudioAssets_HandleString(pInteractionDef->pEntry->pSoundProperties->pchMovementTransEndSound,	peaStrings);
			bResourceHasAudio |= interaction_GetAudioAssets_HandleString(pInteractionDef->pEntry->pSoundProperties->pchMovementTransStartSound,	peaStrings);
		}

		*puiNumData = *puiNumData + 1;
		if (bResourceHasAudio) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	resFreeIterator(&rI);
}

#include "AutoGen/interaction_common_h_ast.c"
