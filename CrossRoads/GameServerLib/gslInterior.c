/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "EntitySavedData.h"
#include "StringCache.h"
#include "LocalTransactionManager.h"
#include "entCritter.h"
#include "SavedPetCommon.h"
#include "interaction_common.h"
#include "GameAccountDataCommon.h"
#include "LoggedTransactions.h"
#include "gslPartition.h"
#include "gslSpawnPoint.h"
#include "WorldGrid.h"
#include "MapDescription.h"
#include "gslMechanics.h"
#include "Player.h"
#include "EntityLib.h"
#include "gslTransactions.h"
#include "gslEntity.h"
#include "chatCommonStructs.h"
#include "gslInterior.h"
#include "InteriorCommon.h"
#include "GameServerLib.h"
#include "gslInteractable.h"
#include "gslPartition.h"
#include "MicroTransactions.h"
#include "rand.h"
#include "gslMapTransfer.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

#include "AutoGen/MapDescription_h_ast.h"
#include "AutoGen/gslInterior_h_ast.h"
#include "AutoGen/InteriorCommon_h_ast.h"
#include "AutoGen/Player_h_ast.h"

// TODO_PARTITION: Interior system
static InteriorPartitionState **s_InteriorPartitionStates = NULL;

static InteriorPartitionState *
GetInteriorPartitionState(int iPartitionIdx)
{
	InteriorPartitionState *pInteriorState;
	pInteriorState = eaGet(&s_InteriorPartitionStates, iPartitionIdx);

	devassert(pInteriorState != NULL);
	if ( pInteriorState == NULL )
	{
		ErrorDetailsf("partition index = %d", iPartitionIdx);
		Errorf("gslInterior: Attempt to access an invalid partition.");
	}
	else
	{
		devassert(pInteriorState->iPartitionIdx == iPartitionIdx);
		if ( pInteriorState->iPartitionIdx != iPartitionIdx )
		{
			ErrorDetailsf("array index = %d, partition state index = %d", iPartitionIdx, pInteriorState->iPartitionIdx);
			Errorf("gslInterior: InteriorPartitionState has incorrect partition index.");
		}
	}
	return pInteriorState;
}

void
gslInterior_SetMapInteriorOptions(int iPartitionIdx)
{
	Entity *pOwnerEnt;
	Entity *pInteriorEnt;

	// only update options for an interior map
	if ( !InteriorCommon_IsCurrentMapInterior() )
	{
		return;
	}

	// only update options if owner is present
	pOwnerEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, partition_OwnerIDFromIdx(iPartitionIdx));
	if ( pOwnerEnt == NULL || entGetPartitionIdx(pOwnerEnt) != iPartitionIdx )
	{
		return;
	}

	pInteriorEnt = InteriorCommon_GetActiveInteriorOwner(pOwnerEnt, NULL);

	if ( pInteriorEnt != NULL )
	{
		InteriorPartitionState *pInteriorState = GetInteriorPartitionState(iPartitionIdx);
		
		if ( pInteriorState )
		{
			// free any previous interior data
			if ( pInteriorState->interiorData != NULL )
			{
				StructDestroy(parse_EntityInteriorData, pInteriorState->interiorData);
			}

			if ( ( pInteriorEnt->pSaved == NULL ) || ( pInteriorEnt->pSaved->interiorData == NULL ) )
			{
				pInteriorState->interiorData = StructCreate(parse_EntityInteriorData);
			}
			else
			{
				// copy interior data from current active pet
				pInteriorState->interiorData = StructClone(parse_EntityInteriorData, pInteriorEnt->pSaved->interiorData);
			}

			devassert(pInteriorState->interiorData != NULL);

			if ( !IS_HANDLE_ACTIVE(pInteriorState->interiorData->hInteriorDef) )
			{
				InteriorDef *interiorDef = InteriorCommon_GetCurrentInteriorDef(pInteriorEnt);
				if ( ( interiorDef != NULL ) && ( pInteriorState->noconstInteriorData != NULL ) )
				{
					SET_HANDLE_FROM_STRING("InteriorDef", interiorDef->name, pInteriorState->noconstInteriorData->hInteriorDef);
				}
			}
		}
	}
}

void
gslInterior_SetMapOwnerReturnMap(Entity *pOwnerEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pOwnerEnt);

	InteriorPartitionState *pInteriorState = GetInteriorPartitionState(iPartitionIdx);

	if ( pInteriorState )
	{
		// only set this the first time the owner enters the map
		if ( pInteriorState->ownerReturnMapName == NULL )
		{
			if ( ( pOwnerEnt->pSaved != NULL ) && ( pOwnerEnt->pSaved->lastStaticMap != NULL ) )
			{
				pInteriorState->ownerReturnMapName = pOwnerEnt->pSaved->lastStaticMap->mapDescription;
			}
		}
		// also set the last item assignment volume
		if ( pInteriorState->ownerLastItemAssignmentVolume == NULL )
		{
			pInteriorState->ownerLastItemAssignmentVolume = SAFE_MEMBER(pOwnerEnt->pPlayer, pchLastItemAssignmentVolume);
		}
	}
}

const char *
gslInterior_GetMapOwnerReturnMap(int iPartitionIdx)
{
	InteriorPartitionState *pInteriorState = GetInteriorPartitionState(iPartitionIdx);

	if ( pInteriorState )
	{
		return pInteriorState->ownerReturnMapName;
	}

	return NULL;
}

const char *
gslInterior_GetOwnerLastItemAssignmentVolume(int iPartitionIdx)
{
	InteriorPartitionState *pInteriorState = GetInteriorPartitionState(iPartitionIdx);

	if ( pInteriorState )
	{
		return pInteriorState->ownerLastItemAssignmentVolume;
	}

	return NULL;
}

S32
gslInterior_GetMapOptionChoiceValue(int iPartitionIdx, const char *optionName)
{
	InteriorOptionDef *optionDef = InteriorCommon_FindOptionDefByName(optionName);
	S32 retval = 0;

	InteriorPartitionState *pInteriorState = GetInteriorPartitionState(iPartitionIdx);

	if ( pInteriorState )
	{
		if ( optionDef != NULL )
		{
			InteriorOption *pOption = NULL;
			InteriorOptionChoice *pChoice;

			if ( pInteriorState->interiorData != NULL )
			{
				pOption = eaIndexedGetUsingString(&pInteriorState->interiorData->options, optionName);
			}

			if ( pOption != NULL )
			{
				pChoice = GET_REF(pOption->hChoice);
			}
			else
			{
				pChoice = GET_REF(optionDef->hDefaultChoice);
			}

			if ( pChoice != NULL )
			{
				retval = pChoice->value;
			}
		}
	}
	return retval;
}

static void
SetOptionChoice_CB(TransactionReturnVal *pReturn, void *pData)
{
	EntityRef entRef = (EntityRef)(uintptr_t)pData;
	Entity *pEntity = entFromEntityRefAnyPartition(entRef);
	
	//Don't set the static data for the map if you're not the owner
	if(pEntity && gslInterior_IsCurrentMapPlayerCurrentInterior(pEntity))
	{
		gslInterior_SetMapInteriorOptions(entGetPartitionIdx(pEntity));
	}
}

//
// Sets the interior and an interior option on the players pet.
//
void
gslInterior_SetInteriorAndOption(Entity *pEnt, ContainerID petContainer, const char *interiorDefName, const char *optionName, const char *choiceName)
{
	PetDef *petDef;
	int i;
	int n;
	InteriorDef *interiorDef = NULL;
	const char *pooledInteriorDefName = allocAddString(interiorDefName);
	bool found = false;
	TransactionReturnVal *pReturn;
	Entity *pTargetEnt = NULL;
	ContactFlags requiredContact;
	bool sameInterior = false;

	if(petContainer != 0)
	{
		// find the pet we want to set in the player's owned containers
		n = eaSize( &pEnt->pSaved->ppOwnedContainers );
		for ( i = 0; i < n; i++ )
		{
			PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
			if(pPet && pPet->conID == petContainer )
			{
				Entity* tmpPetEnt = pPet ? SavedPet_GetEntity(entGetPartitionIdx(pEnt), pPet) : NULL;
				pTargetEnt = tmpPetEnt;
				break;
			}
		}
	}
	else
	{
		pTargetEnt = pEnt;
	}

	if ( pTargetEnt == NULL )
	{
		return;
	}

	if ( petContainer && pTargetEnt->myEntityType != GLOBALTYPE_ENTITYSAVEDPET )
	{
		return;
	}

	if ( pTargetEnt->pSaved->interiorData != NULL )
	{
		interiorDef = GET_REF(pTargetEnt->pSaved->interiorData->hInteriorDef);
		if ( ( interiorDef != NULL ) && ( interiorDef->name == pooledInteriorDefName ) )
		{
			// if we are already set to this interior, then do nothing
			sameInterior = true;
		}
	}

	if(!sameInterior)
	{
		requiredContact = InteriorConfig_RequiredContactType();
		if ( requiredContact != 0 )
		{
			if ( !interaction_IsPlayerNearContact(pEnt, requiredContact) )
			{
				// not near the required contact
				return;
			}
		}
		else
		{
			// If no contact is required, then just require a static map.
			if ( zmapInfoGetMapType(NULL) != ZMTYPE_STATIC )
			{
				return;
			}
		}
	}

	if(entGetType(pTargetEnt) == GLOBALTYPE_ENTITYSAVEDPET)
	{
		petDef = GET_REF(pTargetEnt->pCritter->petDef);
		if ( petDef == NULL )
		{
			return;
		}

		// make sure the named interior def is valid for this pet
		n = eaSize(&petDef->ppInteriorDefs);
		for ( i = 0; i < n; i++ )
		{
			interiorDef = GET_REF(petDef->ppInteriorDefs[i]->hInterior);
			if ( ( interiorDef != NULL ) && ( interiorDef->name == pooledInteriorDefName ) )
			{
				//check to see if the player has unlocked this interior
				found = InteriorCommon_IsInteriorUnlocked(pEnt, interiorDef);
				break;
			}
		}

		// interiorDef is not valid for this pet
		if ( found == false )
		{
			return;
		}
	}
	else
	{
		interiorDef = (InteriorDef*)RefSystem_ReferentFromString(g_hInteriorDefDict, pooledInteriorDefName);
		if(!interiorDef || !InteriorCommon_IsInteriorUnlocked(pEnt, interiorDef))
		{
			return;
		}
	}

	if ( ( optionName != NULL ) && ( optionName[0] != '\0' ) && ( choiceName != NULL ) && ( choiceName[0] != '\0' ) )
	{
		// setting an option
		InteriorOptionDef *optionDef = InteriorCommon_FindOptionDefByName(optionName);
		ItemChangeReason reason = {0};

		if ( ( interiorDef == NULL ) || ( optionDef == NULL ) )
		{
			return;
		}

		// not a valid option for this interior
		if ( !InteriorCommon_IsOptionAvailableForInterior(interiorDef, optionDef) )
		{
			return;
		}

		if ( !InteriorCommon_IsChoiceAvailableForOption(optionDef, choiceName) )
		{
			return;
		}

		inv_FillItemChangeReason(&reason, pEnt, "Interior:SetInterior", pTargetEnt->debugName);

		pReturn = LoggedTransactions_CreateManagedReturnValEnt("SetInteriorOptionChoice", pEnt, SetOptionChoice_CB, (void*)(intptr_t)entGetRef(pEnt));
		if ( sameInterior ) 
		{
			// only need to set the option.  The interior is already correct
			AutoTrans_gslInterior_tr_SetOptionChoice(pReturn, GLOBALTYPE_GAMESERVER, 
					GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, 
					GLOBALTYPE_ENTITYSAVEDPET, petContainer, 
					optionName, choiceName, &reason);
		}
		else
		{
			// setting both interior and option
			AutoTrans_gslInterior_tr_SetInteriorAndOption(pReturn, GLOBALTYPE_GAMESERVER, 
					GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, 
					GLOBALTYPE_ENTITYSAVEDPET, petContainer, 
					pooledInteriorDefName, 
					GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID, 
					optionName, choiceName, &reason);
		}
	}
	else if ( !sameInterior )
	{
		ItemChangeReason reason = {0};

		// setting just an interior

		pReturn = LoggedTransactions_CreateManagedReturnValEnt("SetInterior", pEnt, NULL, NULL);

		inv_FillItemChangeReason(&reason, pEnt, "Interior:SetInterior", pTargetEnt->debugName);

		AutoTrans_gslInterior_tr_SetInterior(pReturn, GLOBALTYPE_GAMESERVER, 
				GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, 
				GLOBALTYPE_ENTITYSAVEDPET, petContainer, 
				pooledInteriorDefName, &reason, 
				GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID);
	}
}

//
// Sets the interior and/or setting of the interior on the players pet (or themselves if petContainer is 0)
//
void 
gslInterior_SetInteriorAndSetting(Entity *pEnt, ContainerID petContainer, const char *interiorDefName, const char *settingName)
{
	PetDef *petDef;
	int i,n;
	InteriorDef *interiorDef = NULL;
	const char *pooledInteriorDefName = allocAddString(interiorDefName);
	bool found = false;
	TransactionReturnVal *pReturn;
	Entity *pTargetEnt = NULL;
	ContactFlags requiredContact;
	bool sameInterior = false;
	bool interiorNotSet = false;

	if(petContainer != 0)
	{
		// find the pet we want to set in the player's owned containers
		n = eaSize( &pEnt->pSaved->ppOwnedContainers );
		for ( i = 0; i < n; i++ )
		{
			PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
			if(pPet && pPet->conID == petContainer )
			{
				Entity* tmpPetEnt = pPet ? SavedPet_GetEntity(entGetPartitionIdx(pEnt), pPet) : NULL;
				pTargetEnt = tmpPetEnt;
				break;
			}
		}
	}
	else
	{
		pTargetEnt = pEnt;
	}

	if ( pTargetEnt == NULL )
	{
		return;
	}

	if ( petContainer && pTargetEnt->myEntityType != GLOBALTYPE_ENTITYSAVEDPET )
	{
		return;
	}

	if ( pTargetEnt->pSaved->interiorData != NULL )
	{
		interiorDef = GET_REF(pTargetEnt->pSaved->interiorData->hInteriorDef);
		if ( ( interiorDef != NULL ) && ( interiorDef->name == pooledInteriorDefName ) )
		{
			// if we are already set to this interior, then do nothing
			sameInterior = true;
		}
		else if(InteriorConfig_PersistAlternates())
		{
			bool bFound = false;
			FOR_EACH_IN_EARRAY(pTargetEnt->pSaved->interiorData->alternates, const InteriorData, pAlternateData)
			{
				InteriorDef *pAlternateDef = GET_REF(pAlternateData->hInteriorDef);
				if( pAlternateDef && pAlternateDef->name == pooledInteriorDefName )
				{
					bFound = true;
					break;
				}
			} FOR_EACH_END;

			if(!bFound)
				interiorNotSet = true;
		}
	}
	else
	{
		interiorNotSet = true;
	}

	if(!sameInterior)
	{
		requiredContact = InteriorConfig_RequiredContactType();
		if ( requiredContact != 0 )
		{
			if ( !interaction_IsPlayerNearContact(pEnt, requiredContact) )
			{
				// not near the required contact
				return;
			}
		}
		else
		{
			// If no contact is required, then just require a static map.
			if ( zmapInfoGetMapType(NULL) != ZMTYPE_STATIC )
			{
				return;
			}
		}
	}

	if(entGetType(pTargetEnt) == GLOBALTYPE_ENTITYSAVEDPET)
	{
		petDef = GET_REF(pTargetEnt->pCritter->petDef);
		if ( petDef == NULL )
		{
			return;
		}

		// make sure the named interior def is valid for this pet
		n = eaSize(&petDef->ppInteriorDefs);
		for ( i = 0; i < n; i++ )
		{
			interiorDef = GET_REF(petDef->ppInteriorDefs[i]->hInterior);
			if ( ( interiorDef != NULL ) && ( interiorDef->name == pooledInteriorDefName ) )
			{
				//check to see if the player has unlocked this interior
				found = InteriorCommon_IsInteriorUnlocked(pEnt, interiorDef);
				break;
			}
		}

		// interiorDef is not valid for this pet
		if ( found == false )
		{
			return;
		}

		if(interiorNotSet && ( settingName == NULL || settingName[0] == '\0'))
		{
			FOR_EACH_IN_REFDICT(g_hInteriorSettingDict, InteriorSetting, pSetting)
			{
				if( InteriorCommon_IsSettingAvailableForInterior(interiorDef, pSetting) 
					&& InteriorCommon_CanUseSetting(pEnt, pSetting) )
				{
					settingName = pSetting->pchName;
					break;
				}
			} FOR_EACH_END;
		}
	}
	else
	{
		interiorDef = (InteriorDef*)RefSystem_ReferentFromString(g_hInteriorDefDict, pooledInteriorDefName);
		if(!interiorDef || !InteriorCommon_IsInteriorUnlocked(pEnt, interiorDef))
		{
			return;
		}

		if(interiorNotSet && ( settingName == NULL || settingName[0] == '\0'))
		{
			FOR_EACH_IN_REFDICT(g_hInteriorSettingDict, InteriorSetting, pSetting)
			{
				if( InteriorCommon_IsSettingAvailableForInterior(interiorDef, pSetting) 
					&& InteriorCommon_CanUseSetting(pEnt, pSetting) )
				{
					settingName = pSetting->pchName;
					break;
				}
			} FOR_EACH_END;
		}
	}

	if ( ( settingName != NULL ) && ( settingName[0] != '\0' )  )
	{
		// setting an option
		InteriorSetting *setting = InteriorCommon_FindSettingByName(settingName);

		if ( ( interiorDef == NULL ) || ( setting == NULL ) )
		{
			return;
		}

		// not a valid setting for this interior
		if ( !InteriorCommon_IsSettingAvailableForInterior(interiorDef, setting) 
			|| InteriorCommon_IsSettingCurrent(pTargetEnt, setting)
			|| !InteriorCommon_CanUseSetting(pTargetEnt, setting))
		{
			return;
		}

		pReturn = LoggedTransactions_CreateManagedReturnValEnt("SetInteriorSetting", pEnt, SetOptionChoice_CB, (void*)(intptr_t)entGetRef(pEnt));
		if ( sameInterior ) 
		{
			// only need to set the setting.  The interior is already correct
			AutoTrans_gslInterior_tr_SetSetting(pReturn, GLOBALTYPE_GAMESERVER, 
				GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, 
				GLOBALTYPE_ENTITYSAVEDPET, petContainer, 
				settingName);
		}
		else
		{
			// setting both interior and setting
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pEnt, "Interior:SetInterior", pTargetEnt->debugName);

			AutoTrans_gslInterior_tr_SetInteriorAndSetting(pReturn, GLOBALTYPE_GAMESERVER, 
				GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, 
				GLOBALTYPE_ENTITYSAVEDPET, petContainer, 
				pooledInteriorDefName, 
				GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID, 
				settingName, &reason);
		}
	}
	else if ( !sameInterior )
	{
		// setting the interior and clearing the setting
		ItemChangeReason reason = {0};

		pReturn = LoggedTransactions_CreateManagedReturnValEnt("SetInteriorAndClearSetting", pEnt, NULL, NULL);

		inv_FillItemChangeReason(&reason, pEnt, "Interior:SetInterior", pTargetEnt->debugName);

		AutoTrans_gslInterior_tr_SetInteriorAndSetting(pReturn, GLOBALTYPE_GAMESERVER, 
			GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, 
			GLOBALTYPE_ENTITYSAVEDPET, petContainer, 
			pooledInteriorDefName, 
			GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID, 
			settingName, &reason);
	}
	else
	{

		pReturn = LoggedTransactions_CreateManagedReturnValEnt("ClearInteriorSetting", pEnt, NULL, NULL);

		AutoTrans_gslInterior_tr_ClearSetting(pReturn, GLOBALTYPE_GAMESERVER, 
			GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, 
			GLOBALTYPE_ENTITYSAVEDPET, petContainer);
	}
}

//
// Sets the interior on the players pet (or themselves if petContainer is 0)
//
void
gslInterior_SetInterior(Entity *pEnt, ContainerID petContainer, const char *interiorDefName)
{
	gslInterior_SetInteriorAndSetting(pEnt, petContainer, interiorDefName, NULL);
}

void 
gslInterior_ClearData(Entity *pEnt, ContainerID petContainer)
{
	Entity *pTargetEnt = NULL;

	if(petContainer != 0)
	{
		// find the pet we want to set in the player's owned containers
		int i,n = eaSize( &pEnt->pSaved->ppOwnedContainers );
		for ( i = 0; i < n; i++ )
		{
			PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
			if(pPet && pPet->conID == petContainer )
			{
				Entity* tmpPetEnt = pPet ? SavedPet_GetEntity(entGetPartitionIdx(pEnt), pPet) : NULL;
				pTargetEnt = tmpPetEnt;
				break;
			}
		}
	}
	else
	{
		pTargetEnt = pEnt;
	}

	if ( pTargetEnt == NULL )
	{
		return;
	}

	if ( petContainer && pTargetEnt->myEntityType != GLOBALTYPE_ENTITYSAVEDPET )
	{
		return;
	}

	if(SAFE_MEMBER2(pTargetEnt, pSaved, interiorData))
	{
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(SetOptionChoice_CB, (void*)(intptr_t)entGetRef(pEnt));
		AutoTrans_gslInterior_tr_ClearData(pReturn, GLOBALTYPE_GAMESERVER, entGetType(pTargetEnt), entGetContainerID(pTargetEnt));
	}
}

//
// Move player to the interior of the given pet
//
void
gslInterior_MoveToInterior(Entity *playerEnt, Entity *pOwner)
{
	InteriorDef *interiorDef;

	if ( !InteriorCommon_CanMoveToInterior(playerEnt) )
	{
		return;
	}

	interiorDef = InteriorCommon_GetCurrentInteriorDef(pOwner);

	if ( interiorDef != NULL )
	{
		spawnpoint_MovePlayerToMapAndSpawn(playerEnt, interiorDef->mapName, interiorDef->spawnPointName, NULL, 0, 0, 0, 0, NULL, NULL, NULL,0, 0);
	}
}

//
// Move player to the interior of the active puppet of the type that has interiors
//
void
gslInterior_MoveToActiveInterior(Entity *playerEnt, const char* pchSetName)
{
	Entity *pOwner = InteriorCommon_GetActiveInteriorOwner(playerEnt, RefSystem_ReferentFromString(g_hCharacterClassCategorySetDict, pchSetName));

	if ( pOwner != NULL )
	{
		gslInterior_MoveToInterior(playerEnt, pOwner);
	}
}

//
// Return true if the current map is the player's current interior
//
static bool
gslInterior_IsCurrentMapPlayerCurrentInteriorInternal(Entity *playerEnt, CharClassCategorySet *pSet)
{
	Entity *pOwner = InteriorCommon_GetActiveInteriorOwner(playerEnt, pSet);

	if ( pOwner != NULL )
	{
		InteriorDef *interiorDef;
		const char *currentMapName = zmapInfoGetPublicName(NULL);
		ZoneMapType mapType = zmapInfoGetMapType(NULL);

		interiorDef = InteriorCommon_GetCurrentInteriorDef(pOwner);
		
		if ( ( interiorDef != NULL ) && ( mapType == ZMTYPE_OWNED ) && ( currentMapName == interiorDef->mapName ) )
		{
			Entity *ownerEnt = partition_GetPlayerMapOwner(entGetPartitionIdx(playerEnt));

			if ( ( ownerEnt != NULL ) && ( ownerEnt->myContainerID == playerEnt->myContainerID ) )
			{
				return true;
			}
		}
	}

	return false;
}

bool
gslInterior_IsCurrentMapPlayerCurrentInterior(Entity *playerEnt)
{
	CharClassCategorySet *pSet;
	RefDictIterator iter;
	RefSystem_InitRefDictIterator(g_hCharacterClassCategorySetDict, &iter);
	while (pSet = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if (gslInterior_IsCurrentMapPlayerCurrentInteriorInternal(playerEnt, pSet))
		{
			return true;
		}
	}
	return false;
}

//
// This is run on the server of the invitee.
//
bool
gslInterior_InviteeInvite(ContainerID inviteeID, InteriorInvite *inviteIn, U32 inviterAccountID)
{
	InteriorInvite *invite;
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, inviteeID);

	if ( ( pEnt == NULL ) || ( pEnt->pPlayer == NULL ) || ( zmapInfoGetMapType(NULL) != ZMTYPE_STATIC ) )
	{
		// player must exist and can only be invited from a static map
		return false;
	}

	if (!entIsWhitelistedEx(pEnt, inviteIn->ownerID, inviterAccountID, kPlayerWhitelistFlags_Invites))
	{
		return false;
	}

	// check ignore list
	if ( ( pEnt->pPlayer != NULL ) && ( pEnt->pPlayer->pUI != NULL ) && 
		( pEnt->pPlayer->pUI->pChatState != NULL ) && ( pEnt->pPlayer->pUI->pChatState->eaIgnores != NULL )	)
	{
		int i;
		int n;
		n = eaSize(&pEnt->pPlayer->pUI->pChatState->eaIgnores);
		for ( i = 0; i < n; i++ )
		{
			if ( pEnt->pPlayer->pUI->pChatState->eaIgnores[i]->accountID == inviterAccountID )
			{
				return false;
			}
		}
	}
	invite = StructClone(parse_InteriorInvite, inviteIn);

	eaPush(&pEnt->pPlayer->interiorInvites, invite);

	return true;
}

//
// This is run on the server of the player doing the inviting.
// It requires that the player be in their own interior.
//
void
gslInterior_Invite(Entity *playerEnt, ContainerID subjectID)
{
	InteriorInvite *invite;
	Entity *pOwner;
	InteriorDef *interiorDef;

	if ( ! gslInterior_IsCurrentMapPlayerCurrentInterior(playerEnt) )
	{
		// can't invite if not already in interior
		return;
	}

	pOwner = InteriorCommon_GetActiveInteriorOwner(playerEnt, NULL);

	if ( pOwner == NULL )
	{
		return;
	}

	interiorDef = InteriorCommon_GetCurrentInteriorDef(pOwner);
	if ( interiorDef == NULL )
	{
		return;
	}

	invite = StructCreate(parse_InteriorInvite);
	invite->mapName = interiorDef->mapName;
	invite->spawnPointName = interiorDef->spawnPointName;
	invite->ownerID = playerEnt->myContainerID;
	invite->ownerDisplayName = StructAllocString(playerEnt->pSaved->savedName);
	if(playerEnt != pOwner)
		invite->shipDisplayName = StructAllocString(pOwner->pSaved->savedName);

	RemoteCommand_gslInterior_InviteeInvite(GLOBALTYPE_ENTITYPLAYER, subjectID, subjectID, invite, playerEnt->pPlayer->accountID);

	StructDestroy(parse_InteriorInvite, invite);
}

void
InviteByName_CB(Entity *pEnt, ContainerID subjectID, U32 uiAccountID, U32 uiLoginServerID, void *userData)
{
	gslInterior_Invite(pEnt, subjectID);
}

void
gslInterior_InviteByName(Entity *playerEnt, const char *inviteeName)
{
	gslPlayerResolveHandle(playerEnt, inviteeName, InviteByName_CB, NULL, NULL);
}

static void
MoveToInvitedInterior(Entity *pEnt, InteriorInvite *invite)
{
	spawnpoint_MovePlayerToMapAndSpawn(pEnt, invite->mapName, invite->spawnPointName, NULL, GLOBALTYPE_ENTITYPLAYER, invite->ownerID, 0, 0, NULL, NULL, NULL,TRANSFERFLAG_NO_NEW_OWNED_MAP, 0);
}

void
gslInterior_AcceptInvite(Entity *pEnt)
{
	if ( eaSize(&pEnt->pPlayer->interiorInvites) > 0 && InteriorCommon_CanMoveToInterior(pEnt) )
	{
		InteriorInvite *invite = pEnt->pPlayer->interiorInvites[0];

		// remove the first element
		eaRemove(&pEnt->pPlayer->interiorInvites, 0);

		MoveToInvitedInterior(pEnt, invite);

		StructDestroy(parse_InteriorInvite, invite);

		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

void
gslInterior_DeclineInvite(Entity *pEnt)
{
	if ( eaSize(&pEnt->pPlayer->interiorInvites) > 0 )
	{
		InteriorInvite *invite = pEnt->pPlayer->interiorInvites[0];

		// remove the first element
		eaRemove(&pEnt->pPlayer->interiorInvites, 0);

		StructDestroy(parse_InteriorInvite, invite);

		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

void
gslInterior_ExpelGuest(Entity *playerEnt, EntityRef guestRef)
{
	Entity *guest;
	if ( ! gslInterior_IsCurrentMapPlayerCurrentInterior(playerEnt) )
	{
		// can't expel if not already in interior
		return;
	}

	guest = entFromEntityRef(entGetPartitionIdx(playerEnt), guestRef);
	if ( guest != NULL )
	{
		LeaveMap(guest);
	}
}

void
gslInterior_SetOptionChoice(Entity *playerEnt, ContainerID petID, const char *optionName, InteriorOptionChoice *pChoice)
{
	Entity *pTargetEnt = petID ? InteriorCommon_GetPetByID(playerEnt, petID) : playerEnt;
	InteriorDef *interiorDef;
	InteriorOptionDef *optionDef;
	TransactionReturnVal *pReturn;
	ItemChangeReason reason = {0};

	if ( ( playerEnt == NULL ) || ( pTargetEnt == NULL ) || ( pTargetEnt->pSaved == NULL ) || 
		( optionName == NULL ) || ( optionName[0] == '\0' ) || ( pChoice == NULL ) )
	{
		return;
	}

	optionDef = InteriorCommon_FindOptionDefByName(optionName);
	interiorDef = InteriorCommon_GetCurrentInteriorDef(pTargetEnt);

	if ( ( interiorDef == NULL ) || ( optionDef == NULL ) )
	{
		return;
	}

	// not a valid option for this interior
	if ( !InteriorCommon_IsOptionAvailableForInterior(interiorDef, optionDef) )
	{
		return;
	}

	pReturn = objCreateManagedReturnVal(SetOptionChoice_CB, (void*)(intptr_t)entGetRef(playerEnt));

	inv_FillItemChangeReason(&reason, playerEnt, "Interior:SetOption", pTargetEnt->debugName);

	AutoTrans_gslInterior_tr_SetOptionChoice(pReturn, GLOBALTYPE_GAMESERVER, 
			GLOBALTYPE_ENTITYPLAYER, playerEnt->myContainerID, 
			GLOBALTYPE_ENTITYSAVEDPET, petID, 
			optionDef->name, pChoice->name, &reason);
}

bool
gslInterior_InteriorOptionChoiceActiveByValue(Entity *pEnt, const char *optionName, S32 value)
{
	InteriorDef *interiorDef;
	InteriorOptionDef *optionDef;
	InteriorOptionDefRef *optionDefRef;
	int i;
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	InteriorPartitionState *pInteriorState = GetInteriorPartitionState(iPartitionIdx);

	if ( pInteriorState && pInteriorState->interiorData )
	{

		// only valid for owner of current map, which must be an interior
		if ( !gslInterior_IsCurrentMapPlayerCurrentInterior(pEnt) )
		{
			return false;
		}

		interiorDef = GET_REF(pInteriorState->interiorData->hInteriorDef);

		if ( interiorDef == NULL )
		{
			return false;
		}

		optionDefRef = eaIndexedGetUsingString(&interiorDef->optionRefs, optionName);

		if ( optionDefRef == NULL )
		{
			return false;
		}

		optionDef = GET_REF(optionDefRef->hOptionDef);
		if ( optionDef == NULL )
		{
			return false;
		}

		// find the choice with the specified value
		for ( i = eaSize(&optionDef->choiceRefs) - 1; i >= 0; i-- )
		{
			InteriorOptionChoice *pChoice;

			pChoice = GET_REF(optionDef->choiceRefs[i]->hChoice);

			if ( ( pChoice != NULL ) && ( pChoice->value == value ) )
			{
				return InteriorCommon_IsChoiceActive(pEnt, pChoice);
			}
		}

	}
	return false;
}

void
gslInterior_InteriorOptionChoiceSetByValue(Entity *pEnt, const char *optionName, S32 value)
{
	InteriorDef *interiorDef;
	InteriorOptionDef *optionDef;
	InteriorOptionDefRef *optionDefRef;
	Entity *pOwner;
	int i;

	int iPartitionIdx = entGetPartitionIdx(pEnt);

	InteriorPartitionState *pInteriorState = GetInteriorPartitionState(iPartitionIdx);

	if ( pInteriorState && pInteriorState->interiorData)
	{
		// only valid for owner of current map, which must be an interior
		if ( !gslInterior_IsCurrentMapPlayerCurrentInterior(pEnt) )
		{
			return;
		}

		interiorDef = GET_REF(pInteriorState->interiorData->hInteriorDef);

		if ( interiorDef == NULL )
		{
			return;
		}

		optionDefRef = eaIndexedGetUsingString(&interiorDef->optionRefs, optionName);

		if ( optionDefRef == NULL )
		{
			return;
		}

		optionDef = GET_REF(optionDefRef->hOptionDef);

		if ( optionDef == NULL )
		{
			return;
		}

		pOwner = InteriorCommon_GetActiveInteriorOwner(pEnt, NULL);

		// find the choice with the specified value
		for ( i = eaSize(&optionDef->choiceRefs) - 1; i >= 0; i-- )
		{
			InteriorOptionChoice *pChoice;

			pChoice = GET_REF(optionDef->choiceRefs[i]->hChoice);

			if ( ( pChoice != NULL ) && ( pChoice->value == value ) )
			{
				if ( InteriorCommon_IsChoiceActive(pEnt, pChoice) )
				{
					gslInterior_SetOptionChoice(pEnt, 
						pOwner == pEnt ? 0 : pOwner->myContainerID, 
						optionName, 
						pChoice);
				}
			}
		}
	}
	return;
}

static void FreePurchase_CB(TransactionReturnVal *pVal, void *userData)
{
	EntityRef eRef = (EntityRef)(intptr_t)(userData);
	Entity *pEnt = entFromEntityRefAnyPartition(eRef);

	if(pEnt 
		&& pEnt->pPlayer 
		&& pEnt->pPlayer->pMicroTransInfo)
	//	&& pDef 
	//	&& pDef->bOnePerCharacter)
	{
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		entity_SetDirtyBit(pEnt, parse_PlayerMTInfo, pEnt->pPlayer->pMicroTransInfo, false);
	}
}

void
gslInterior_UseFreePurchase(Entity *pEnt, const char *pchSetting)
{
	GameAccountDataExtract *pExtract = NULL;
	InteriorSetting *pSetting = InteriorCommon_FindSettingByName(pchSetting);
	InteriorSettingMTRef *pSettingMTRef = InteriorCommon_FindSettingMTRefByName(pchSetting);
	MicroTransactionDef *pDef = NULL;
	const GameAccountData *pData = entity_GetGameAccount(pEnt);
	
	if( !pEnt || !pSetting || !pSettingMTRef)
		return;

	pDef = GET_REF(pSettingMTRef->hMTDef);

	if(!pDef)
		return;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	if(!InteriorCommon_EntHasFreePurchase(pEnt, pExtract))
	{
		return;
	}

	if(microtrans_HasPurchased(pData,pDef))
		return;

	{
		U32 *eaPets = NULL;
		TransactionReturnVal *pVal = objCreateManagedReturnVal(FreePurchase_CB, (void*)(intptr_t)entGetRef(pEnt));
		ItemChangeReason reason = {0};
		
		if (microtrans_GrantsUniqueItem(pDef, NULL))
		{
			ea32Create(&eaPets);
			Entity_GetPetIDList(pEnt, &eaPets);
		}

		inv_FillItemChangeReason(&reason, pEnt, "Interior:UseFreePurchase", NULL);

		AutoTrans_gslInterior_tr_UseFreePurchase(pVal, GLOBALTYPE_GAMESERVER, 
			entGetType(pEnt), entGetContainerID(pEnt),
			GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
			GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt),
			pDef, pDef->pchName, &reason);

		ea32Destroy(&eaPets);
	}
}

static void
gslInterior_CreateFakeInteriorData(Entity *pEnt, InteriorPartitionState *pState)
{
	const char *currentMapName = zmapInfoGetPublicName(NULL);
	ZoneMapType mapType = zmapInfoGetMapType(NULL);
	InteriorDef *pCorrectDef = NULL;

	if(mapType != ZMTYPE_OWNED)
		return;

	FOR_EACH_IN_REFDICT(g_hInteriorDefDict, InteriorDef, pDef)
	{
		if(pDef->name == currentMapName)
		{
			pCorrectDef = pDef;
			break;
		}
	} FOR_EACH_END;

	if(pCorrectDef)
	{
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "Interior:CreateFakeInteriorData", NULL);

		AutoTrans_gslInterior_tr_SetInterior(NULL, GLOBALTYPE_GAMESERVER, 
			entGetType(pEnt), entGetContainerID(pEnt), 
			GLOBALTYPE_ENTITYSAVEDPET, 0, 
			pCorrectDef->name, &reason, 
			GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt));

		StructDestroyNoConstSafe(parse_EntityInteriorData, &pState->noconstInteriorData);
		pState->noconstInteriorData = StructCreateNoConst(parse_EntityInteriorData);
		SET_HANDLE_FROM_STRING(g_hInteriorDefDict, pCorrectDef->name, pState->noconstInteriorData->hInteriorDef);
	}
}

static void
SetOptionChoiceHelper(NOCONST(EntityInteriorData) *pInteriorData, InteriorOptionDef *pOptionDef, InteriorOptionChoice *pChoice)
{
	bool bFound = false;

	FOR_EACH_IN_EARRAY(pInteriorData->options, NOCONST(InteriorOption), pOption)
	{
		if(!pOption || !GET_REF(pOption->hOption))
			continue;

		if(GET_REF(pOption->hOption) != pOptionDef)
			continue;

		bFound = true;
		REMOVE_HANDLE(pOption->hChoice);
		SET_HANDLE_FROM_STRING(g_hInteriorOptionChoiceDict, pChoice->name, pOption->hChoice);
	} FOR_EACH_END;

	if(!bFound)
	{
		NOCONST(InteriorOption) *pOption = StructCreateNoConst(parse_InteriorOption);
		SET_HANDLE_FROM_STRING(g_hInteriorOptionDefDict, pOptionDef->name, pOption->hOption);
		SET_HANDLE_FROM_STRING(g_hInteriorOptionChoiceDict, pChoice->name, pOption->hChoice);
		eaPush(&pInteriorData->options, pOption);
	}
}

void
gslInterior_Randomize(Entity *pEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	InteriorPartitionState *pInteriorState = GetInteriorPartitionState(iPartitionIdx);

	if ( pInteriorState )
	{
		InteriorDef *pInterior = NULL;
		// only valid for owner of current map, which must be an interior
		if ( !gslInterior_IsCurrentMapPlayerCurrentInterior(pEnt) )
		{
			gslInterior_CreateFakeInteriorData(pEnt, pInteriorState);
			if(!pInteriorState->interiorData )
				return;
		}

		if(!pInteriorState->interiorData )
		{
			gslInterior_CreateFakeInteriorData(pEnt, pInteriorState);
			if(!pInteriorState->interiorData )
				return;
		}

		pInterior = GET_REF(pInteriorState->interiorData->hInteriorDef);

		if(!pInterior)
			return;

		//Go thru all the options, choose a random choice for each option, set it on the local setting

		FOR_EACH_IN_EARRAY(pInterior->optionRefs, InteriorOptionDefRef, pOptionRef)
		{
			InteriorOptionDef *pOptionDef = GET_REF(pOptionRef->hOptionDef);
			int optionSize = 0;
			int iChosenChoice = 0;

			if(!pOptionDef)
				continue;

			optionSize = eaSize(&pOptionDef->choiceRefs);

			iChosenChoice = ((int)(randomPositiveF32() * optionSize));
			if(devassert(iChosenChoice < optionSize && iChosenChoice >= 0))
			{
				InteriorOptionChoice *pChoice = GET_REF(pOptionDef->choiceRefs[iChosenChoice]->hChoice);
				if(!pChoice)
					continue;

				SetOptionChoiceHelper(pInteriorState->noconstInteriorData, pOptionDef, pChoice);
			}
		} FOR_EACH_END;
	}
}

void
gslInterior_PlayerEntering(Entity *pEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	if ( InteriorCommon_IsCurrentMapInterior() && ( pEnt->myContainerID == partition_OwnerIDFromIdx(iPartitionIdx) ) )
	{
		InteriorPartitionState *pInteriorState = GetInteriorPartitionState(iPartitionIdx);

		if ( pInteriorState )
		{
			pInteriorState->ownerEntering = true;
		}
	}
}

void
gslInterior_Tick(void)
{
	int iPartitionIdx;

	for ( iPartitionIdx = 0; iPartitionIdx < eaSize(&s_InteriorPartitionStates); iPartitionIdx++ )
	{
		InteriorPartitionState *pInteriorState = eaGet(&s_InteriorPartitionStates, iPartitionIdx);

		if ( pInteriorState && pInteriorState->ownerEntering && ( partition_OwnerIDFromIdx(iPartitionIdx) != 0 ) )
		{
			Entity *pOwnerEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, partition_OwnerIDFromIdx(iPartitionIdx));

			if ( ( pOwnerEnt == NULL ) || ( pOwnerEnt->pSaved == NULL ) || entGetPartitionIdx(pOwnerEnt) != iPartitionIdx)
			{
				// owner is gone and never fully logged in or is now in a different partition
				pInteriorState->ownerEntering = false;
			}
			else if ( pOwnerEnt->pSaved->bValidatedOwnedContainers )
			{
				// owner's pets have arrived, so set interior state
				gslInterior_SetMapInteriorOptions(iPartitionIdx);
				gslInterior_SetMapOwnerReturnMap(pOwnerEnt);

				pInteriorState->ownerEntering = false;
			}
			else
			{
				// owner's pets have not arrived yet
			}
		}
	}
}

void
gslInterior_MapLoad(void)
{
}

void
gslInterior_MapUnload(void)
{
}

void
gslInterior_MapValidate(void)
{
}

InteriorPartitionState *
gslInterior_CreatePartitionState(int iPartitionIdx)
{
	InteriorPartitionState *pInteriorState = StructCreate(parse_InteriorPartitionState);
	if ( pInteriorState != NULL )
	{
		pInteriorState->iPartitionIdx = iPartitionIdx;
	}

	return pInteriorState;
}

void
gslInterior_PartitionLoad(int iPartitionIdx)
{
	InteriorPartitionState *pInteriorState = eaGet(&s_InteriorPartitionStates, iPartitionIdx);

	PERFINFO_AUTO_START_FUNC();

	// Create state if it doesn't exist
	if ( pInteriorState == NULL )
	{
		pInteriorState = gslInterior_CreatePartitionState(iPartitionIdx);
		eaSet(&s_InteriorPartitionStates, pInteriorState, iPartitionIdx);
	}

	PERFINFO_AUTO_STOP();
}

void
gslInterior_PartitionUnload(int iPartitionIdx)
{
	InteriorPartitionState *pInteriorState = GetInteriorPartitionState(iPartitionIdx);

	// should always be called with an existing partition
	devassert(pInteriorState != NULL);

	if ( pInteriorState != NULL )
	{
		StructDestroy(parse_InteriorPartitionState, pInteriorState);
		eaSet(&s_InteriorPartitionStates, NULL, iPartitionIdx);
	}
	else
	{
		ErrorDetailsf("partition index = %d", iPartitionIdx);
		Errorf("gslInterior: Unloading a partition that doesn't exist");
	}
}

#include "gslInterior_h_ast.c"
