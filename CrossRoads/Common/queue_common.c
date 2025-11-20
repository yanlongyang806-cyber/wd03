/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "queue_common.h"
#include "queue_common_structs.h"

#include "AutoTransDefs.h"
#include "character.h"
#include "CharacterClass.h"
#include "chatCommon.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntityIterator.h"
#include "OfficerCommon.h"
#include "Expression.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GlobalTypes.h"
#include "Guild.h"
#include "mission_common.h"
#include "objPath.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "rand.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "TextFilter.h"
#include "tokenstore.h"
#include "utilitiesLib.h"
#include "WorldVariable.h"
#include "../StaticWorld/ZoneMap.h"
#include "ResourceSystem_Internal.h"

#include "PvPGameCommon.h"
#include "GameEvent.h"

#include "queue_smartGroup.h"


#if defined(GAMESERVER) || defined(APPSERVER)
#include "ChoiceTable.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#endif

#if defined(GAMESERVER)
#include "gslQueue.h"
#endif

#include "queue_common_h_ast.h"
#include "queue_common_structs_h_ast.h"

#include "PvPGameCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define QUEUES_BASE_DIR "defs/queues"
#define QUEUES_EXTENSION "queue"

#define QUEUE_PRIVATE_NAME_MIN_LENGTH 3
#define QUEUE_PRIVATE_NAME_MAX_LENGTH 32

QueueCategories s_QueueCategories = {0};
static QueueCooldownDefs s_QueueCooldowns = {0};
extern DefineContext* g_pQueueCategories;
extern DefineContext* g_pQueueRewards;
extern DefineContext* g_pQueueDifficulty;
QueueConfig g_QueueConfig;
DictionaryHandle g_hQueueDefDict;
static int s_hPlayerVar;

ExprContext *queue_GetContext(Entity *pEnt)
{
	static ExprContext *s_pQueueContext = NULL;

	if(!s_pQueueContext)
	{
		ExprFuncTable* stTable;

		s_pQueueContext = exprContextCreate();
		stTable = exprContextCreateFunctionTable();

		exprContextAddFuncsToTableByTag(stTable,"entity");
		exprContextAddFuncsToTableByTag(stTable,"entityutil");
		exprContextAddFuncsToTableByTag(stTable,"player");
		exprContextAddFuncsToTableByTag(stTable,"util");
		exprContextAddFuncsToTableByTag(stTable,"gameutil");
		exprContextAddFuncsToTableByTag(stTable,"CEFuncsCharacter");

		exprContextSetFuncTable(s_pQueueContext, stTable);

		exprContextSetAllowRuntimePartition(s_pQueueContext);
		exprContextSetAllowRuntimeSelfPtr(s_pQueueContext);

		assert(g_PlayerVarName != NULL);
	}
	
	exprContextSetPointerVarPooledCached(s_pQueueContext,g_PlayerVarName,pEnt,parse_Entity,true,true,&s_hPlayerVar);

	if (pEnt) {
		exprContextSetSelfPtr(s_pQueueContext, pEnt);
		exprContextSetPartition(s_pQueueContext, entGetPartitionIdx(pEnt));
	} else {
		exprContextClearSelfPtrAndPartition(s_pQueueContext);
	}
	return s_pQueueContext;
}

static WorldVariable* queue_GetOverrideWorldVariable(SA_PARAM_NN_VALID QueueInstance* pInstance,
													 SA_PARAM_NN_VALID QueueLevelBand* pLevelBand,
													 SA_PARAM_NN_VALID QueueDef* pDef,
													 SA_PARAM_NN_VALID WorldVariable* pWorldVariable,
													 const char* pchMapName,
													 WorldVariable*** peaCopyVarsOut,
													 bool *bSettingsQualify)
{
	WorldVariable* pRetVar;
	if (!queue_IsVariableSupportedOnMap(pDef, pLevelBand, pWorldVariable->pcName, pchMapName))
	{
		return NULL;
	}
	pRetVar = StructClone(parse_WorldVariable, pWorldVariable);
	if (pRetVar)
	{
		S32 iOverrideValue = -1;
		const char* pchVarName = pWorldVariable->pcName;
		S32 iLevelBandIndex = SAFE_MEMBER(pInstance->pParams, iLevelBandIndex);
		QueueVariableData* pData = queue_FindVariableDataEx(pDef, iLevelBandIndex, pchVarName, pchMapName, true);
		
		if (pData && pWorldVariable->eType == WVAR_INT)
		{
			if (queue_GetVariableOverrideValue((QueueGameSetting**)pInstance->eaSettings,
											   pData, 
											   &iOverrideValue))
			{
				pRetVar->iIntVal = iOverrideValue;
			}
			else if (pData && pData->pSettingData)
			{
				S32 iMin = pData->pSettingData->iMinValue;
				S32 iMax = pData->pSettingData->iMaxValue;
				pRetVar->iIntVal = CLAMP(pRetVar->iIntVal, iMin, iMax);
			}

			if (pData->pSettingData->iRewardMinValue > 0 &&
				pData->pSettingData->iRewardMinValue > pRetVar->iIntVal)
			{
				*bSettingsQualify = false;
			}
		}
		if (pData && pData->pchStringValue && pWorldVariable->eType == WVAR_STRING)
		{
			StructCopyString(&pRetVar->pcStringVal, pData->pchStringValue);
		}
		if (pData && peaCopyVarsOut)
		{
			S32 i;
			for (i = 0; i < eaSize(&pData->ppchCopyVarNames); i++)
			{
				const char* pchCopyVarName = pData->ppchCopyVarNames[i];
				WorldVariable* pCopyVar = StructCreate(parse_WorldVariable);
				pCopyVar->pcName = allocAddString(pchCopyVarName);
				pCopyVar->eType = pWorldVariable->eType;
				pCopyVar->iIntVal = pRetVar->iIntVal;
				StructCopyString(&pCopyVar->pcStringVal, pRetVar->pcStringVal);
				eaPush(peaCopyVarsOut, pCopyVar);
			}
		}
	}
	return pRetVar;
}

static void queue_AddWorldVariablesToList(WorldVariable** eaVarList,
										  SA_PARAM_NN_VALID QueueInstance* pInstance,
										  SA_PARAM_NN_VALID QueueLevelBand* pLevelBand,
										  SA_PARAM_NN_VALID QueueDef* pDef,
										  const char* pchMapName,
										  WorldVariable*** peaVarsOut,
										  bool *bSettingsQualify)
{
	WorldVariable** eaCopyVars = NULL;
	WorldVariable* pVar;
	S32 iVarIdx, iCopyVarIdx;

	for (iVarIdx = eaSize(&eaVarList)-1; iVarIdx >= 0; iVarIdx--)
	{
		WorldVariable* pWorldVariable = eaVarList[iVarIdx];

		pVar = queue_GetOverrideWorldVariable(pInstance, pLevelBand, pDef, pWorldVariable, pchMapName, &eaCopyVars, bSettingsQualify);
		if (pVar && !eaIndexedAdd(peaVarsOut, pVar))
		{
			WorldVariable* pBaseVar = eaIndexedGetUsingString(peaVarsOut, pVar->pcName);
			StructCopyAll(parse_WorldVariable, pVar, pBaseVar);
			StructDestroySafe(parse_WorldVariable, &pVar);
		}
		for (iCopyVarIdx = eaSize(&eaCopyVars)-1; iCopyVarIdx >= 0; iCopyVarIdx--)
		{
			WorldVariable* pCopyVar = eaCopyVars[iCopyVarIdx];
			if (!eaIndexedAdd(peaVarsOut, pCopyVar))
			{
				WorldVariable* pBaseVar = eaIndexedGetUsingString(peaVarsOut, pCopyVar->pcName);
				StructCopyAll(parse_WorldVariable, pCopyVar, pBaseVar);
				StructDestroySafe(parse_WorldVariable, &pCopyVar);
			}
		}
		eaClear(&eaCopyVars);
	}
	eaDestroy(&eaCopyVars);
}

static void queue_FillGameInfoFromVariables(QueueGameInfo* pGameInfo,
											QueueVariableData** eaVarData,
											SA_PARAM_NN_VALID QueueInstance* pInstance,
											const char* pchMapName)
{
	S32 iVarIdx;
	for (iVarIdx = eaSize(&eaVarData)-1; iVarIdx >= 0; iVarIdx--)
	{
		QueueVariableData* pVarData = eaVarData[iVarIdx];
		if (pVarData->eType == kQueueVariableType_GameInfo)
		{
			S32 iValue;
			if (queue_GetVariableOverrideValue((QueueGameSetting**)pInstance->eaSettings, pVarData, &iValue))
			{
				int iColumn, iIndex;
				ParseTable* pTable;
				void* pStructPtr;
				char pchPath[512];

				sprintf(pchPath, ".%s", pVarData->pchFieldName);
				if (objPathResolveField(pchPath, parse_QueueGameInfo, pGameInfo, &pTable, &iColumn, &pStructPtr, &iIndex, 0))
				{
					TokenStoreSetInt(pTable, iColumn, pStructPtr, iIndex, iValue, NULL, NULL);
				}
			}
		}
	}
}


int queue_FindPvPGameType(QueueDef* pDef, QueueInstance *pInstance, QueueMap* pMap)
{
	int eGameType=0;	//Return val
	int eDefaultGameType=0;	// This used to be set up in aslQueue_MMR_FillMatchMaking before the map was created. It's kind of awkward and we're not using MMR now.
	
	//Decide what game type to play
	if(!SAFE_MEMBER2(pInstance, pParams, eGameType) || !eDefaultGameType)
	{
		S32 iIndex = queue_GetMapIndexByName(pDef,pMap->pchMapName);
		if(iIndex >= 0 && iIndex < eaSize(&pDef->QueueMaps.eaCustomMapTypes))
		{
			S32 iCount = ea32Size(&pDef->QueueMaps.eaCustomMapTypes[iIndex]->puiPVPGameModes);
			eGameType = iCount ? pDef->QueueMaps.eaCustomMapTypes[iIndex]->puiPVPGameModes[randomIntRange(0,iCount-1)] : kPVPGameType_None;
		}
	}
	else if(!eDefaultGameType)
	{
		eGameType = SAFE_MEMBER2(pInstance, pParams, eGameType);
	}

	if(eGameType == kPVPGameType_None)
	{
		eGameType = pDef->MapRules.QGameRules.publicRules.eGameType;
	}

	return(eGameType);
}


void queue_FillGameInfo(QueueGameInfo* pGameInfo, QueueDef* pDef, QueueInstance* pInstance, QueueMap* pMap)
{
	S32 iLevelBandIndex = SAFE_MEMBER(pInstance->pParams, iLevelBandIndex);
	QueueLevelBand* pLevelBand = eaGet(&pDef->eaLevelBands, iLevelBandIndex);


	if (pGameInfo!=NULL)
	{
		// Fill in the GameInfo struct
		pGameInfo->pchQueueDef = pDef->pchName;
		pGameInfo->iLevelBandIndex = iLevelBandIndex;
		pGameInfo->iBolsterLevel = SAFE_MEMBER(pLevelBand, iBolsterLevel);
		pGameInfo->eBolsterType = queue_GetBolsterType(pDef, pLevelBand);
		pGameInfo->ePvPGameType = queue_FindPvPGameType(pDef, pInstance, pMap);

		if ((pDef->Settings.bEnableLeaverPenalty && !queue_IsPrivateMatch(pInstance->pParams)) || pInstance->bEnableLeaverPenalty)
		{
			pGameInfo->uLeaverPenaltyDuration = queue_GetLeaverPenaltyDuration(pDef);
		}
		// Fill GameInfo override data from game settings
		if (pLevelBand)
		{
			queue_FillGameInfoFromVariables(pGameInfo, pLevelBand->VarData.eaQueueData, pInstance, pMap->pchMapName);
		}
		queue_FillGameInfoFromVariables(pGameInfo, pDef->MapSettings.VarData.eaQueueData, pInstance, pMap->pchMapName);
	}
}


void queue_WorldVariablesFromDef(QueueDef* pDef, 
								 QueueInstance* pInstance,
								 const char* pchMapName,
								 WorldVariable*** peaVars)
{

	// WOLF[8Mar13] This code is all rather suspect. We are attempting to pass info onto the gameServer via MapVars. Unfortunately, the
	//  system for MapVars is only designed to modify existing MapVars on Maps, so even though we set vars up here, unless a designer
	//  specified one in the .Zone file they will not be appear on the map and will generate an error via gslWorldVariableValidateEx in
	//  mapvariable_PartitionLoad in gslMapVariable.c.  This methodology, though it can be made to work,
	//  is prone to many errors as the designers don't always know they need to create placeholder vars on their map.
	//
	// We could move these to be hardcoded variables, but that's sort of skeevy.	
	// In reality we should probably do the following as cleanup/rework.
	//
	// FOR "QueueDef" [DONE!! 15Mar13]
	//   gslQueue_InitGameData needs to be modified in gslQueue.c The calling function should do a remote command to the queue server at
	// startup time. The queue server will need to find the correct info based on the server ID/partition and package up the QueueInfo
	// and remote command back to the server. No need for a mapVar at all. [DONE!! 15Mar13]
	//
	// FOR "MapLevelOverrideHack"
	//   It's unclear how this is actually supposed to work. There are multiple places where map level can be set. This particular
	// variable is used explicitly in mechanics_GetMapLevel, and is referenced by at least a handful of missions for STO. We need
	// to figure out how MapLevel, OverrideMapLevel and this variable interact and just do something more sane.
	//
	// FOR "SettingsQualifyForRewards"
	//    This is a new feature as of Feb2013 for STO. It should not be set up using a MapVar but could possible be done as a explicit GameServer
	// expression call that directly gets the QueueInfo from the partition, does whatever calculation it needs, and returns the value.
	//
	// PLEASE do not propagate this MapVar pattern any further. It is too subtle for general use, causes errors on maps that don't even need
	// the vars, and is just all out BAD practice. MapVars should be for designer-accessible things that they can then override
	
	S32 iLevelBandIndex = SAFE_MEMBER(pInstance->pParams, iLevelBandIndex);
	QueueLevelBand* pLevelBand = eaGet(&pDef->eaLevelBands, iLevelBandIndex);
	bool bSettingsQualify = true;

	// Override Map Level
	// TODO: Move this into the GameInfo structure
	{
		S32 iMapLevel = SAFE_MEMBER(pLevelBand, iOverrideMapLevel);
		if (!iMapLevel)
		{
			iMapLevel = pDef->MapSettings.iOverrideMapLevel;
		}
		if (iMapLevel)
		{
			WorldVariable *pVar = StructCreate(parse_WorldVariable);
			pVar->pcName = allocAddString("MapLevelOverrideHack");
			pVar->eType = WVAR_INT;
			pVar->iIntVal = iMapLevel; 
			eaPush(peaVars, pVar);
		}
	}

	// Custom World Variables
	if (pLevelBand)
	{
		queue_AddWorldVariablesToList(pLevelBand->VarData.eaWorldVars, 
									  pInstance, 
									  pLevelBand, 
									  pDef, 
									  pchMapName,
									  peaVars,
									  &bSettingsQualify);
		queue_AddWorldVariablesToList(pDef->MapSettings.VarData.eaWorldVars, 
									  pInstance, 
									  pLevelBand, 
									  pDef, 
									  pchMapName,
									  peaVars,
									  &bSettingsQualify);
	}

	// Map settings qualify for rewards
	{
		WorldVariable *pVar = StructCreate(parse_WorldVariable);
		pVar->pcName = allocAddString("SettingsQualifyForRewards");
		pVar->eType = WVAR_INT;
		pVar->iIntVal = bSettingsQualify;
		eaPush(peaVars, pVar);
	}
}

bool queue_CheckInstanceParamsValid(Entity* pEnt, QueueDef* pDef, QueueInstanceParams* pParams)
{
	if (pParams)
	{
		S32 i, j;

		if (!eaSize(&pDef->eaLevelBands) && pParams->iLevelBandIndex != 0)
			return false;
		if (eaSize(&pDef->eaLevelBands) && !eaGet(&pDef->eaLevelBands,pParams->iLevelBandIndex))
			return false;
		if ((!pDef->Settings.bRandomMap && !pParams->pchMapName) || (pDef->Settings.bRandomMap && pParams->pchMapName))
			return false;
		if (pParams->uiOwnerID > 0 && pEnt && pParams->uiOwnerID != entGetContainerID(pEnt))
			return false;
		if (!pParams->uiOwnerID && (pParams->pchPrivateName || pParams->pchPassword))
			return false;
		if (pParams->uiOwnerID && pDef->Requirements.bRequireAnyGuild && !guild_IsMember(pEnt))
			return false;
		if (!pParams->uiGuildID && pDef->Requirements.bRequireSameGuild)
			return false;
		if (pParams->uiGuildID && !pDef->Requirements.bRequireSameGuild)
			return false;
		if (pParams->pchPassword && !pParams->pchPrivateName)
			return false;
		if (pDef->MapRules.bChallengeMatch && !pParams->uiOwnerID)
			return false;

		if (pParams->pchMapName)
		{
			for (i = eaSize(&pDef->QueueMaps.ppchMapNames) - 1; i >= 0; i--)
			{
				if (!stricmp(pDef->QueueMaps.ppchMapNames[i], pParams->pchMapName))
					break;
			}
			if (i < 0)
			{
				for (i = eaSize(&pDef->QueueMaps.eaCustomMapTypes) - 1; i >= 0; i--)
				{
					for (j = eaSize(&pDef->QueueMaps.eaCustomMapTypes[i]->ppchMaps) - 1; i >= 0; i--)
					{
						if (!stricmp(pDef->QueueMaps.eaCustomMapTypes[i]->ppchMaps[j], pParams->pchMapName))
							break;
					}
					if (j >= 0)
						break;
				}
				if (i < 0)
					return false;
			}
		}

		return true;
	}
	return false;
}

QueueInstanceParams* queue_CreateInstanceParams(Entity* pEnt, 
												QueueDef* pDef, 
												const char* pchPrivateName,
												const char* pchPassword,
												S32 iLevelBandIndex, 
												S32 iMapIndex, 
												bool bPrivate)
{
	NOCONST(QueueInstanceParams)* pParams = StructCreateNoConst(parse_QueueInstanceParams);
	pParams->iLevelBandIndex = iLevelBandIndex;
	pParams->pchMapName = allocAddString(queue_GetMapNameByIndex(pDef, iMapIndex));
	pParams->uiOwnerID = bPrivate ? entGetContainerID(pEnt) : 0;
	pParams->uiGuildID = pDef->Requirements.bRequireSameGuild && guild_IsMember(pEnt) ? guild_GetGuildID(pEnt) : 0;
	pParams->pchPrivateName = StructAllocString(pchPrivateName);
	pParams->pchPassword = StructAllocString(pchPassword);
	
	if (!queue_CheckInstanceParamsValid(pEnt, pDef, (QueueInstanceParams*)pParams))
	{
		StructDestroyNoConst(parse_QueueInstanceParams, pParams);
		return NULL;
	}
	
	if(iMapIndex < eaSize(&pDef->QueueMaps.eaCustomMapTypes))
	{
		S32 iCount = ea32Size(&pDef->QueueMaps.eaCustomMapTypes[iMapIndex]->puiPVPGameModes);
		pParams->eGameType = iCount ? pDef->QueueMaps.eaCustomMapTypes[iMapIndex]->puiPVPGameModes[randomIntRange(0,iCount-1)] : kPVPGameType_None;
	}
	return (QueueInstanceParams*)pParams;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pA, ".Ilevelbandindex, .Pchmapname, .Uiownerid, .Pchprivatename, .Pchpassword")
ATR_LOCKS(pB, ".Ilevelbandindex, .Pchmapname, .Uiownerid, .Pchprivatename, .Pchpassword");
bool queue_trh_SameParams(ATH_ARG NOCONST(QueueInstanceParams)* pA, ATH_ARG QueueInstanceParams* pB)
{
	if (ISNULL(pA) || ISNULL(pB))
		return false;
	if (pA->iLevelBandIndex != pB->iLevelBandIndex)
		return false;
	if (pA->pchMapName != pB->pchMapName)
		return false;
	if (pA->uiOwnerID != pB->uiOwnerID)
		return false;
	if (stricmp(pA->pchPrivateName, pB->pchPrivateName)!=0)
		return false;
	if (stricmp(pA->pchPassword, pB->pchPassword)!=0)
		return false;

	return true;
}

#define queue_SameParams(pA, pB) queue_trh_SameParams(CONTAINER_NOCONST(QueueInstanceParams, (pA)), (pB))

PlayerQueueInstance* queue_FindPlayerQueueInstance(SA_PARAM_OP_VALID PlayerQueueInfo *pQueueInfo, 
												   SA_PARAM_OP_STR const char* pchQueueName, 
												   QueueInstanceParams* pParams,
												   bool bValidateCanUseQueue)
{
	if (pQueueInfo && pParams)
	{
		PlayerQueue* pQueue = eaIndexedGetUsingString(&pQueueInfo->eaQueues, pchQueueName);

		if (pQueue && pQueue->eCannotUseReason == QueueCannotUseReason_None)
		{
			S32 i;
			for (i=eaSize(&pQueue->eaInstances)-1; i>=0; i--)
			{
				PlayerQueueInstance* pInstance = pQueue->eaInstances[i];

				if (queue_SameParams(pInstance->pParams, pParams))
				{
					return pInstance;
				}
			}
		}
	}
	return NULL;
}

PlayerQueueInstance* queue_FindPlayerQueueInstanceByID(SA_PARAM_OP_VALID PlayerQueueInfo *pQueueInfo, 
													   SA_PARAM_OP_STR const char* pchQueueName, 
													   U32 uiInstanceID,
													   bool bValidateCanUseQueue)
{
	if (pQueueInfo && uiInstanceID > 0)
	{
		PlayerQueue* pQueue = eaIndexedGetUsingString(&pQueueInfo->eaQueues, pchQueueName);

		if (pQueue && pQueue->eCannotUseReason == QueueCannotUseReason_None)
		{
			return eaIndexedGetUsingInt(&pQueue->eaInstances, uiInstanceID);
		}
	}
	return NULL;
}

// Since the members are ordered by when they entered the queue, find them sequentially
AUTO_TRANS_HELPER
ATR_LOCKS(pInstance, ".Eaunorderedmembers[]");
NOCONST(QueueMember) *queue_trh_FindPlayerInInstance(ATH_ARG NOCONST(QueueInstance) *pInstance, ContainerID iEntID)
{
	if (NONNULL(pInstance))
	{
		NOCONST(QueueMember) *pMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, iEntID);
		return(pMember);
	}
	return NULL;
}

// Find a player by container ID in a queue
AUTO_TRANS_HELPER
ATR_LOCKS(pQueue, ".Eainstances");
NOCONST(QueueMember) *queue_trh_FindPlayer(ATH_ARG NOCONST(QueueInfo) *pQueue, U32 iEntID, 
										   NOCONST(QueueInstance)** ppInstance)
{
	S32 i;
	for(i = eaSize(&pQueue->eaInstances)-1; i >= 0; i--)
	{
		NOCONST(QueueInstance)* pInstance = pQueue->eaInstances[i];
		if (NONNULL(pInstance))
		{
			NOCONST(QueueMember)* pMember = queue_trh_FindPlayerInInstance(pInstance, iEntID);
			if (NONNULL(pMember))
			{
				if (NONNULL(ppInstance))
				{
					(*ppInstance) = pInstance;
				}
				return pMember;
			}
		}
	}
	return NULL;
}

// Find an instance in a queue by its parameters
AUTO_TRANS_HELPER
ATR_LOCKS(pQueue, ".Eainstances");
NOCONST(QueueInstance) *queue_trh_FindInstance(ATH_ARG NOCONST(QueueInfo) *pQueue, QueueInstanceParams* pParams)
{
	S32 i;
	for(i = eaSize(&pQueue->eaInstances)-1; i >= 0; i--)
	{
		NOCONST(QueueInstance)* pInstance = pQueue->eaInstances[i];
		if(NONNULL(pInstance) && queue_trh_SameParams(pInstance->pParams, pParams))
		{
			return pInstance;
		}
	}
	return NULL;
}

static bool queue_IsPrivateMapActive(QueueMapState eMapState, QueueDef* pDef)
{
	if (eMapState == kQueueMapState_Open ||
		(pDef->MapSettings.eMapType == ZMTYPE_QUEUED_PVE &&
				(	eMapState == kQueueMapState_LaunchPending ||
					eMapState == kQueueMapState_LaunchCountdown ||
					eMapState == kQueueMapState_Launched ||
					eMapState == kQueueMapState_Active
				)
		 )
		)
	{
		return true;
	}
	return false;
}

QueueMap* queue_FindActiveMapForPrivateInstance(QueueInstance* pInstance, QueueDef* pDef)
{
	if (pInstance && queue_IsPrivateMatch(pInstance->pParams) && pDef)
	{
		S32 iMapIdx;
		for (iMapIdx = eaSize(&pInstance->eaMaps)-1; iMapIdx >= 0; iMapIdx--)
		{
			QueueMap* pMap = pInstance->eaMaps[iMapIdx];

			if (queue_IsPrivateMapActive(pMap->eMapState, pDef))
			{
				return pMap;
			}
		}
	}
	return NULL;
}

PlayerQueueMap* queue_FindActiveMapForPrivatePlayerInstance(PlayerQueueInstance* pInstance, QueueDef* pDef)
{
	if (pInstance && queue_IsPrivateMatch(pInstance->pParams) && pDef)
	{
		S32 iMapIdx;
		for (iMapIdx = eaSize(&pInstance->eaPlayerQueueMaps)-1; iMapIdx >= 0; iMapIdx--)
		{
			PlayerQueueMap* pMap = pInstance->eaPlayerQueueMaps[iMapIdx];

			if (queue_IsPrivateMapActive(pMap->eMapState, pDef))
			{
				return pMap;
			}
		}
	}
	return NULL;
}

S32 queue_ValidateEx(QueueDef *pDef, bool bQueueGen);

void QueueGen_HandleTable(QueueDef *pBaseDef, ChoiceTable *pTable)
{
	bool bAddToDict = true;
	ChoiceEntry **ppEntries = NULL;

	int n,nTotal = 0;

	nTotal = choice_TotalEntriesEx(pTable,&ppEntries);

	for(n=0;n<nTotal;n++)
	{
		QueueDef *pNewDef = StructClone(parse_QueueDef,pBaseDef);
		ChoiceEntry *pEntry = ppEntries[n];
		int j;
		bool bNameModified = false;
		ChoiceTable *pNextTable = NULL;
		
		for(j=0;j<eaSize(&pTable->eaDefs);j++)
		{
			bool bQueueWorldVar = true;

			if(pEntry->eaValues[j]->eType == CVT_Choice)
			{
				pNextTable = GET_REF(pEntry->eaValues[j]->hChoiceTable);
				continue;
			}

			//Entry, not a choice, change the queue def
			if(stricmp(pTable->eaDefs[j]->pchName,"Queue_Name") == 0)
			{
				char *estrName = NULL;

				estrCreate(&estrName);
				estrPrintf(&estrName,"%s_%s",pNewDef->pchName,pEntry->eaValues[j]->value.pcStringVal);

				pNewDef->pchName = allocAddString(estrName);

				estrDestroy(&estrName);

				bQueueWorldVar = false;
				bNameModified = true;
			}
			else if(stricmp(pTable->eaDefs[j]->pchName,"Queue_DisplayName") == 0)
			{
				SET_HANDLE_FROM_STRING(gMessageDict,REF_HANDLE_GET_STRING(pEntry->eaValues[j]->value.messageVal.hMessage),pNewDef->displayNameMesg.hMessage);
				bQueueWorldVar = false;
			}
			else if(stricmp(pTable->eaDefs[j]->pchName,"Queue_DisplayDesc") == 0)
			{
				SET_HANDLE_FROM_STRING(gMessageDict,REF_HANDLE_GET_STRING(pEntry->eaValues[j]->value.messageVal.hMessage),pNewDef->descriptionMesg.hMessage);
				bQueueWorldVar = false;
			}
			else if(stricmp(pTable->eaDefs[j]->pchName,"Queue_StartSpawn") == 0)
			{
				if(eaSize(&pNewDef->eaGroupDefs) == 1)
				{
					StructFreeString((char*)pNewDef->eaGroupDefs[0]->pchSpawnTargetName);
					pNewDef->eaGroupDefs[0]->pchSpawnTargetName = StructAllocString(pEntry->eaValues[j]->value.pcStringVal);
				}
				else
				{
					int iGroup;
					char *pchGroupString = NULL;

					estrCreate(&pchGroupString);

					for(iGroup=0;iGroup<eaSize(&pNewDef->eaGroupDefs);iGroup++)
					{
						estrClear(&pchGroupString);
						estrPrintf(&pchGroupString,"%s_%d",pEntry->eaValues[j]->value.pcStringVal,iGroup);

						StructFreeString((char*)pNewDef->eaGroupDefs[iGroup]->pchSpawnTargetName);
						pNewDef->eaGroupDefs[iGroup]->pchSpawnTargetName = StructAllocString(pEntry->eaValues[j]->value.pcStringVal);
					}

					estrDestroy(&pchGroupString);
				}
			}
			else if(stricmp(pTable->eaDefs[j]->pchName,"Queue_MapName") == 0)
			{
				int iMapData;
				const char *pchMapName = pEntry->eaValues[j]->value.pcStringVal;
				bool bMapFound = false;

				for(iMapData=0;iMapData<eaSize(&pNewDef->QueueMaps.eaCustomMapTypes) && bMapFound == false;iMapData++)
				{
					int iMap;

					for(iMap=0;iMap<eaSize(&pNewDef->QueueMaps.eaCustomMapTypes[iMapData]->ppchMaps);iMapData++)
					{
						if(strcmp(pNewDef->QueueMaps.eaCustomMapTypes[iMapData]->ppchMaps[iMap],pchMapName)==0)
						{
							QueueCustomMapData *pNewData = StructCreate(parse_QueueCustomMapData);
							QueueCustomMapData *pOldData = pNewDef->QueueMaps.eaCustomMapTypes[iMapData];

							eaPush(&pNewData->ppchMaps,pchMapName);
							pNewData->pchIcon = StructAllocString(pOldData->pchIcon);
							SET_HANDLE_FROM_STRING(gMessageDict,REF_HANDLE_GET_STRING(pOldData->msgDisplayName.hMessage),pNewData->msgDisplayName.hMessage);
							ea32Copy(&pNewData->puiPVPGameModes,&pOldData->puiPVPGameModes);

							eaClearStruct(&pNewDef->QueueMaps.eaCustomMapTypes,parse_QueueCustomMapData);
							eaPush(&pNewDef->QueueMaps.eaCustomMapTypes,pNewData);

							bMapFound = true;
							break;
						}
					}
				}

				for(iMapData = 0;iMapData<eaSize(&pNewDef->QueueMaps.ppchMapNames) && bMapFound == false;iMapData++)
				{
					if(strcmp(pNewDef->QueueMaps.ppchMapNames[iMapData],pchMapName)==0)
					{
						eaDestroy(&pNewDef->QueueMaps.ppchMapNames);
						eaPush(&pNewDef->QueueMaps.ppchMapNames,StructAllocString(pchMapName));
						bMapFound = true;
						break;
					}
				}

				if(!bMapFound)
				{
					QueueCustomMapData *pNewData = StructCreate(parse_QueueCustomMapData);

					eaPush(&pNewData->ppchMaps,pchMapName);

					eaDestroy(&pNewDef->QueueMaps.ppchMapNames);
					eaClearStruct(&pNewDef->QueueMaps.eaCustomMapTypes,parse_QueueCustomMapData);
					eaPush(&pNewDef->QueueMaps.eaCustomMapTypes,pNewData);
				}
			}

			if(bQueueWorldVar)
			{
				WorldVariable *pVar = StructClone(parse_WorldVariable,&pEntry->eaValues[j]->value);

				pVar->pcName = allocAddString(pTable->eaDefs[j]->pchName);
				eaPush(&pNewDef->MapSettings.VarData.eaWorldVars,pVar);
			}
		}

		if(!bNameModified)
		{
			char *estrName = NULL;

			estrCreate(&estrName);
			estrPrintf(&estrName,"%s_%d",pNewDef->pchName,n);

			pNewDef->pchName = allocAddString(estrName);

			estrDestroy(&estrName);
		}

		if(pNextTable)
		{
			QueueGen_HandleTable(pNewDef,pNextTable);
			StructDestroy(parse_QueueDef,pNewDef);
		}
		else
		{
			if(queue_ValidateEx(pNewDef,1))
			{
				pNewDef->pchFilename = NULL; //File names are pooled strings

				resEditSetWorkingCopy(resGetDictionary(g_hQueueDefDict), pNewDef->pchName, pNewDef);
			}
		}
	}
}

void Queue_GenLoad(void)
{
	char *binFile = "QueueGen.bin";
	QueueDefGenContainer pDefs = {0};
	int i;

#ifdef APPSERVER
	if (isDevelopmentMode() && !gbMakeBinsAndExit)
		binFile = "DevAppQueueGen.bin";
#endif

	ParserLoadFiles("defs/queues/",".queueGen",binFile,PARSER_OPTIONALFLAG,parse_QueueDefGenContainer,&pDefs);

	resEditStartDictionaryModification(g_hQueueDefDict);

	for(i=0;i<eaSize(&pDefs.ppDefs);i++)
	{
		ChoiceTable *pTable = GET_REF(pDefs.ppDefs[i]->hChoiceTable);
		QueueDef *pBaseDef = NULL;
		char *estrName;
		
		if(IS_HANDLE_ACTIVE(pDefs.ppDefs[i]->hBaseDef))
		{
			if(!GET_REF(pDefs.ppDefs[i]->hBaseDef))
			{
				ErrorFilenamef(pDefs.ppDefs[i]->sBaseDef.pchFilename,"Queue Gen References non existent gen: %s",REF_STRING_FROM_HANDLE(pDefs.ppDefs[i]->hBaseDef));
				continue;
			}
			pBaseDef = StructClone(parse_QueueDef,GET_REF(pDefs.ppDefs[i]->hBaseDef));
		}
		else {
			pBaseDef = StructClone(parse_QueueDef,&pDefs.ppDefs[i]->sBaseDef);
		}
		

		if(pDefs.ppDefs[i]->bMarkAsPublic)
			pBaseDef->Settings.bPublic = true;
		
		if(pDefs.ppDefs[i]->eCategoryChange)
			pBaseDef->eCategory = pDefs.ppDefs[i]->eCategoryChange;

		estrCreate(&estrName);
		estrPrintf(&estrName,"_%s",pBaseDef->pchName);

		pBaseDef->pchName = allocAddString(estrName);

		estrDestroy(&estrName);

		QueueGen_HandleTable(pBaseDef,pTable);
	}

	resEditCommitAllModifications(g_hQueueDefDict, true);
}

// Just the data reload. Don't do any QueueServer work here as it may get called from the login server or UGC Search manager
void Queues_ReloadQueues(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Queues...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionaryWithFlags(pchRelPath,g_hQueueDefDict,PARSER_OPTIONALFLAG);

	loadend_printf(" done (%d queues)", RefSystem_GetDictionaryNumberOfReferents(g_hQueueDefDict));
}


void Queues_Load(resCallback_HandleEvent pReloadCB, FolderCacheCallback pFolderReloadCB)
{
	char *binFile = "Queues.bin";
#ifdef APPSERVER
	if (isDevelopmentMode() && !gbMakeBinsAndExit)
		binFile = "DevAppQueues.bin";
#endif

	if(!IsClient())
	{
		resLoadResourcesFromDisk(g_hQueueDefDict, "defs/queues/", ".queue", binFile, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

		Queue_GenLoad();
		// Reload callbacks
		if(isDevelopmentMode())
		{
			if(entIsServer() && pReloadCB)
			{
				resDictRegisterEventCallback(g_hQueueDefDict, pReloadCB, NULL);
			}
			if(pFolderReloadCB)
				FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/queues/*.queue", pFolderReloadCB);
		}
	}
}

static void Queues_ValidateConfig(void)
{
	int i;
	for (i = eaiSize(&g_QueueConfig.peAllowQueuingOnQueueMaps)-1; i >= 0; i--)
	{
		ZoneMapType eMapType = g_QueueConfig.peAllowQueuingOnQueueMaps[i];
		if (!queue_IsQueueMap(eMapType))
		{
			Errorf("Queue config includes a non-queue map %s", StaticDefineIntRevLookup(ZoneMapTypeEnum, eMapType));
		}
	}
}

void Queues_LoadConfig(void)
{
	StructReset(parse_QueueConfig, &g_QueueConfig);

	// Load the config file
	ParserLoadFiles(NULL, 
		"defs/config/QueueConfig.def", 
		"QueueConfig.bin", 
		PARSER_OPTIONALFLAG,
		parse_QueueConfig, 
		&g_QueueConfig);

	// Do validation
	if (isDevelopmentMode() && IsGameServerBasedType())
	{
		Queues_ValidateConfig();
	}

#if defined(GAMESERVER)
	// Generate the GearRating expression. Only on the GameServer because this gets loaded on AppServers which won't have the context.
	{
		ExprContext *pContext = queue_GetContext(NULL);
		if (g_QueueConfig.pGearRatingCalcExpr)
		{
			exprGenerate(g_QueueConfig.pGearRatingCalcExpr, pContext);
		}
	}
#endif	
}


static void Queues_LoadCategoriesInternal(const char *pchPath, S32 iWhen)
{
	S32 i, iSize;
	StructReset(parse_QueueCategories, &s_QueueCategories);

	if (g_pQueueCategories)
	{
		DefineDestroy(g_pQueueCategories);
	}
	g_pQueueCategories = DefineCreate();

	loadstart_printf("QueueDef Categories... ");

	ParserLoadFiles(NULL, 
		"defs/config/QueueCategories.def", 
		"QueueCategories.bin", 
		PARSER_OPTIONALFLAG, 
		parse_QueueCategories,
		&s_QueueCategories);

	iSize = eaSize(&s_QueueCategories.eaData);
	for (i = 0; i < iSize; i++)
	{
		QueueCategoryData* pData = s_QueueCategories.eaData[i];
		pData->eCategory = i+1;
		DefineAddInt(g_pQueueCategories, pData->pchName, pData->eCategory);
	}

	loadend_printf(" done (%d Categories).", iSize);
}

static void Queues_LoadRewardsInternal(const char *pchPath, S32 iWhen)
{
	const char *pchMessage;
	S32 iSize;
	if (g_pQueueRewards)
		DefineDestroy(g_pQueueRewards);
	g_pQueueRewards = DefineCreate();
	loadstart_printf("QueueDef Rewards... ");
	iSize = DefineLoadFromFile(g_pQueueRewards, "Reward", "Rewards", NULL, "defs/config/QueueRewards.def", "QueueRewards.bin", 1);
	loadend_printf(" done (%d Rewards).", iSize - 1);
	if (IsClient() || IsGameServerBasedType())
	{
		if (pchMessage = StaticDefineVerifyMessages(QueueRewardEnum))
			ErrorFilenamef("defs/config/QueueRewards.def", "Not all QueueReward messages were found: %s", pchMessage);
	}
}

static void Queues_LoadDifficultyInternal(const char *pchPath, S32 iWhen)
{
	const char *pchMessage;
	S32 iSize;
	if (g_pQueueDifficulty)
		DefineDestroy(g_pQueueDifficulty);
	g_pQueueDifficulty = DefineCreate();
	loadstart_printf("QueueDef Difficulties... ");
	iSize = DefineLoadFromFile(g_pQueueDifficulty, "Difficulty", "Difficulties", NULL, "defs/config/QueueDifficulty.def", "QueueDifficulty.bin", 1);
	loadend_printf(" done (%d Difficulties).", iSize - 1);
	if (IsClient() || IsGameServerBasedType())
	{
		if (pchMessage = StaticDefineVerifyMessages(QueueDifficultyEnum))
			ErrorFilenamef("defs/config/QueueDifficulty.def", "Not all QueueDifficulty messages were found: %s", pchMessage);
	}
}

AUTO_STARTUP(QueueCategories);
void Queues_LoadCategories(void)
{
	Queues_LoadCategoriesInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/QueueCategories.def", Queues_LoadCategoriesInternal);

	Queues_LoadRewardsInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/QueueRewards.def", Queues_LoadRewardsInternal);

	Queues_LoadDifficultyInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/QueueDifficulty.def", Queues_LoadDifficultyInternal);

}

static void Queues_LoadCooldownsInternal(const char *pchPath, S32 iWhen)
{
	StructReset(parse_QueueCooldownDefs, &s_QueueCooldowns);

	loadstart_printf("QueueDef Cooldowns... ");

	ParserLoadFiles(NULL, 
		"defs/config/QueueCooldowns.def", 
		"QueueCooldowns.bin", 
		PARSER_OPTIONALFLAG, 
		parse_QueueCooldownDefs,
		&s_QueueCooldowns);

	loadend_printf(" done (%d CooldownDefs).", eaSize(&s_QueueCooldowns.eaCooldowns));
}

AUTO_STARTUP(QueueCooldowns);
void Queues_LoadCooldowns(void)
{
	Queues_LoadCooldownsInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/QueueCooldowns.def", Queues_LoadCooldownsInternal);
}

QueueCooldownDef* queue_CooldownDefFromName(const char* pchName)
{
	if (pchName && pchName[0])
	{
		return eaIndexedGetUsingString(&s_QueueCooldowns.eaCooldowns, pchName);
	}
	return NULL;
}

void queue_GetCooldownNames(const char*** pppchCooldownNames)
{
	int i, iSize = eaSize(&s_QueueCooldowns.eaCooldowns);
	for (i = 0; i < iSize; i++)
	{
		eaPush(pppchCooldownNames, s_QueueCooldowns.eaCooldowns[i]->pchName);
	}
}

static void queue_Generate(QueueDef *pDef)
{
	S32 i;

	ExprContext *pContext = queue_GetContext(NULL);

	if(pDef->Requirements.pRequires)
	{
		exprGenerate(pDef->Requirements.pRequires, pContext);
	}

	for (i = eaSize(&pDef->eaGroupDefs)-1; i >= 0; i--)
	{
		QueueGroupDef *pGroupDef = pDef->eaGroupDefs[i];
		if (pGroupDef->pRequires)
		{
			exprGenerate(pGroupDef->pRequires,pContext);
		}
	}

	for (i = eaSize(&pDef->eaRewardTables)-1; i >= 0; i--)
	{
		QueueRewardTable *pRewardTable = pDef->eaRewardTables[i];
		if (pRewardTable->pExprRewardCondition)
		{
			exprGenerate(pRewardTable->pExprRewardCondition, pContext);
		}
	}
}

S32 queue_ValidateEx(QueueDef *pDef,bool bQueueGen)
{
	S32 bValid = true;
	S32 i;
	S32 iMinSizeReq = 0, iPrivateMinSizeReq = 0;
	S32 iMinTimedSizeReq = 0, iPrivateMinTimedSizeReq = 0;
	const char *pchTempFileName;
	bool bHasPrivateGroupData = false;

	if (!bQueueGen && !resIsValidName(pDef->pchName))
	{
		ErrorFilenamef(pDef->pchFilename, "Queue name is illegal: '%s'", pDef->pchName);
		bValid = false;
	}

	if (!bQueueGen && !resIsValidScope(pDef->pchScope))
	{
		ErrorFilenamef(pDef->pchFilename, "Queue scope is illegal: '%s'", pDef->pchScope);
		bValid = false;
	}

	pchTempFileName = pDef->pchFilename;
	if (!bQueueGen && resFixPooledFilename(&pchTempFileName, QUEUES_BASE_DIR, pDef->pchScope, pDef->pchName, QUEUES_EXTENSION)) {
		if (IsServer()) {
			ErrorFilenamef(pDef->pchFilename, "Queue filename does not match name '%s' scope '%s'", pDef->pchName, pDef->pchScope);
			bValid = false;
		}
	}

	for (i = 0; i < eaSize(&pDef->eaGroupDefs); i++)
	{
		QueueGroupDef *pGroupDef = eaGet(&pDef->eaGroupDefs, i);
		S32 iMaxTimed = pGroupDef->iMaxTimed ? pGroupDef->iMaxTimed : pGroupDef->iMax;
		if(!pGroupDef || pGroupDef->iMin > pGroupDef->iMax || pGroupDef->iMinTimed > iMaxTimed)
		{
			ErrorFilenamef(pDef->pchFilename, "QueueDef [%s] has invalid group def in index [%d]", pDef->pchName, i);
			bValid = false;
		}
		iMinSizeReq += pGroupDef->iMin;
		iMinTimedSizeReq += pGroupDef->iMinTimed;

		// private queue overrides
		if(pGroupDef->bUseGroupPrivateSettings)
		{
			bHasPrivateGroupData = true;
			if(!pGroupDef || pGroupDef->iPrivateMinGroupSize > pGroupDef->iMax || pGroupDef->iPrivateOverTimeSize > iMaxTimed)
			{
				ErrorFilenamef(pDef->pchFilename, "QueueDef [%s] has invalid group (private data) def in index [%d]", pDef->pchName, i);
				bValid = false;
			}
			iPrivateMinSizeReq += pGroupDef->iPrivateMinGroupSize;
			iPrivateMinTimedSizeReq += pGroupDef->iPrivateMinGroupSize;
		}

	}

	if (iMinSizeReq <= 0 && !pDef->Limitations.iMinMembersAllGroups)
	{
		ErrorFilenamef(pDef->pchFilename,"QueueDef [%s] specifies an invalid minimum [%d] for the map",
			pDef->pchName,
			iMinSizeReq);
		bValid = false;
	}
	else if (pDef->Limitations.iMaxTimeToWait && iMinTimedSizeReq <= 0)
	{
		ErrorFilenamef(pDef->pchFilename,"QueueDef [%s] specifies a max time to wait but specifies an invalid minimum [%d] for the map",
			pDef->pchName,
			iMinTimedSizeReq);
		bValid = false;
	}

	if(bHasPrivateGroupData)
	{
		if(iPrivateMinSizeReq <= 0 && (!pDef->Limitations.iMinMembersAllGroups || !pDef->Limitations.iPrivateMinMembersAllGroups))
		{
			ErrorFilenamef(pDef->pchFilename,"QueueDef [%s] specifies an invalid private minimum [%d] for the map",
				pDef->pchName,
				iPrivateMinSizeReq);
			bValid = false;
		}
		else if (pDef->Limitations.iMaxTimeToWait && iPrivateMinTimedSizeReq <= 0)
		{
			ErrorFilenamef(pDef->pchFilename,"QueueDef [%s] specifies a max time to wait but specifies an invalid private minimum [%d] for the map",
				pDef->pchName,
				iPrivateMinTimedSizeReq);
			bValid = false;
		}
	}

	if (pDef->Settings.bAlwaysCreate)
	{
		if (!pDef->Settings.bPublic)
		{
			ErrorFilenamef(pDef->pchFilename,"QueueDef [%s] is flagged as both non-public and 'AlwaysCreate', which isn't allowed",
				pDef->pchName);
			bValid = false;
		}
		if (pDef->Requirements.bRequireSameGuild)
		{
			ErrorFilenamef(pDef->pchFilename,"QueueDef [%s] is flagged as both 'RequireSameGuild' and 'AlwaysCreate', which isn't allowed",
				pDef->pchName);
			bValid = false;
		}
	}
	if (pDef->MapRules.bChallengeMatch)
	{
		if (pDef->Settings.bAlwaysCreate)
		{
			ErrorFilenamef(pDef->pchFilename, 
				"QueueDef [%s] is flagged as both 'ChallengeMatch' and 'AlwaysCreate', which isn't allowed", 
				pDef->pchName);
			bValid = false;
		}
	}
	if (g_QueueConfig.bStayInQueueOnMapLeave || pDef->Settings.bStayInQueueOnMapLeave)
	{
		if (pDef->Settings.uOverridePenaltyDuration || pDef->Settings.iLeaverPenaltyMinGroupMemberCount)
		{
			ErrorFilenamef(pDef->pchFilename, 
				"QueueDef [%s] is flagged as 'StayInQueueOnMapLeave' and specifies leaver penalty data, which will have no effect", 
				pDef->pchName);
		}
		if (pDef->Settings.uPlayerLimboTimeoutOverride)
		{
			ErrorFilenamef(pDef->pchFilename, 
				"QueueDef [%s] is flagged as 'StayInQueueOnMapLeave' and specifies a player disconnect timeout, which will have no effect", 
				pDef->pchName);
		}
	}

	for (i = 0; i < eaSize(&pDef->eaLevelBands); i++)
	{
		QueueLevelBand* pLevelBand = pDef->eaLevelBands[i];

		if(pLevelBand->iMinLevel < 0 || (pLevelBand->iMaxLevel != 0 && pLevelBand->iMaxLevel < pLevelBand->iMinLevel))
		{
			ErrorFilenamef(pDef->pchFilename,"QueueDef %s has invalid minimum and maximum levels: %d and %d for level band %d", pDef->pchName, pLevelBand->iMinLevel, pLevelBand->iMaxLevel, i);
			bValid = false;
		}
		
		if (queue_GetBolsterType(pDef, pLevelBand) != kBolsterType_None &&
			pLevelBand->iBolsterLevel &&
			(pLevelBand->iBolsterLevel < pLevelBand->iMinLevel || 
			 (pLevelBand->iMaxLevel && pLevelBand->iBolsterLevel > pLevelBand->iMaxLevel)))
		{
			ErrorFilenamef(pDef->pchFilename,"QueueDef %s has a bolster level (%d) that extends outside of the level band range (%d-%d).", 
				pDef->pchName, pLevelBand->iBolsterLevel, pLevelBand->iMinLevel, pLevelBand->iMaxLevel);
			bValid = false;
		}
		if (i > 0)
		{
			QueueLevelBand* pLastBand = pDef->eaLevelBands[i-1];

			if (pLevelBand->iMinLevel < pLastBand->iMinLevel || pLevelBand->iMaxLevel < pLastBand->iMinLevel)
			{
				ErrorFilenamef(pDef->pchFilename,"QueueDef %s has invalid minimum and maximum levels: LevelBand(%d) min/max levels are less than LevelBand(%d)", pDef->pchName, i, i-1);
				bValid = false;
			}
		}
	}

	if (!REF_STRING_FROM_HANDLE(pDef->QueueMaps.hMapChoiceTable) && eaSize(&pDef->QueueMaps.eaCustomMapTypes)==0 && eaSize(&pDef->QueueMaps.ppchMapNames)==0)
	{
		ErrorFilenamef(pDef->pchFilename,"QueueDef %s has no maps. Please specify at least one MapName.", pDef->pchName);
		bValid = false;
	}
	else
	{
#if defined(GAMESERVER)
		// Validate ppchMapNames to make sure they are the right type. Only valid on a game server since things like the loginServer don't have the maps loaded.
		//  We should validate the hMapChoiceTable as well?
	
		int iMapData=0;
	
		for(iMapData = 0; iMapData<eaSize(&pDef->QueueMaps.ppchMapNames); iMapData++)
		{
			ZoneMapType mapType = zmapInfoGetMapTypeByName(pDef->QueueMaps.ppchMapNames[iMapData]);

			if (!(mapType==ZMTYPE_PVP || mapType==ZMTYPE_QUEUED_PVE))
			{
				ErrorFilenamef(pDef->pchFilename,"QueueDef %s specifies a map %s that is not Queueable.", pDef->pchName,pDef->QueueMaps.ppchMapNames[iMapData]);
				bValid = false;
			}
		}
#endif		
	}
	

	for(i = 0; i < eaSize(&pDef->ppTrackedEvents); ++i)
	{
		QueueTrackedEvent *pTrackedEvent = pDef->ppTrackedEvents[i];
		GameEvent* pEvent = NULL;

		if(!pTrackedEvent->pchMapValue)
		{
			ErrorFilenamef(pDef->pchFilename,"QueueDef %s tracked event %d doesn't have a map value.", pDef->pchName, i);
			bValid = false;
			continue;
		}

		if(pTrackedEvent->pchEventString)
		{
			pEvent = gameevent_EventFromString(pTrackedEvent->pchEventString);

			if(!pEvent)
			{
				ErrorFilenamef(pDef->pchFilename,"QueueDef %s tracked event %d event string %s can't create event.", pDef->pchName, i, pTrackedEvent->pchEventString);
				bValid = false;
				continue;
			}
			else
			{
				// destroy valid event
				StructDestroy(parse_GameEvent, pEvent);
			}
		}
		else
		{
			ErrorFilenamef(pDef->pchFilename,"QueueDef %s tracked event %d doesn't have an event string.", pDef->pchName, i);
			bValid = false;
		}
	}


#ifdef GAMESERVER
	if (pDef->pchCooldownDef)
	{
		QueueCooldownDef* pCooldown = eaIndexedGetUsingString(&s_QueueCooldowns.eaCooldowns, pDef->pchCooldownDef);
		if (!pCooldown)
		{
			ErrorFilenamef(pDef->pchFilename,"QueueDef %s references non-existent QueueCooldownDef %s.", pDef->pchName, pDef->pchCooldownDef);
			bValid = false;
		}
	}
#endif

	//Missions aren't loaded on the app server
#if !APPSERVER
	if(REF_STRING_FROM_HANDLE(pDef->Requirements.hMissionRequired) && !GET_REF(pDef->Requirements.hMissionRequired))
	{
		ErrorFilenamef(pDef->pchFilename, "QueueDef %s has a mission requirement %s but it is invalid", pDef->pchName, REF_STRING_FROM_HANDLE(pDef->Requirements.hMissionRequired));
		bValid = false;
	}
#endif

#ifdef GAMESERVER
	for (i = eaSize(&pDef->Requirements.ppClassesRequired)-1; i >= 0; i--)
	{
		CharacterClassRef* pClassRef = pDef->Requirements.ppClassesRequired[i];
		CharacterClass* pClass = GET_REF(pClassRef->hClass);

		if (!pClass)
		{
			ErrorFilenamef(pDef->pchFilename, "QueueDef %s references a non-existent character class %s", pDef->pchName, REF_STRING_FROM_HANDLE(pClassRef->hClass));
			bValid = false;
		}
		else
		{
			S32 j;
			for (j = eaiSize(&pDef->Requirements.piClassCategoriesRequired)-1; j >= 0; j--)
			{
				if (pClass->eCategory == pDef->Requirements.piClassCategoriesRequired[j])
				{
					ErrorFilenamef(pDef->pchFilename, "QueueDef %s has a required class that has a category that matches the required class category %s", pDef->pchName, StaticDefineIntRevLookup(CharClassCategoryEnum, pClass->eCategory));
					bValid = false;
					break;
				}
			}
		}
	}
#endif

	for (i = eaSize(&pDef->eaRewardTables)-1; i >= 0; i--)
	{
		QueueRewardTable *pRewardTable = pDef->eaRewardTables[i];
		if (pRewardTable->pExprRewardCondition && pRewardTable->eRewardCondition != kQueueRewardTableCondition_UseExpression)
		{
			ErrorFilenamef(pDef->pchFilename, "QueueDef %s has a Condition Expression but is not Reward Condition type UseExpression.", pDef->pchName);
			bValid = false;
			break;
		}
		else if (pRewardTable->eRewardCondition == kQueueRewardTableCondition_UseExpression && !pRewardTable->pExprRewardCondition) 
		{
			ErrorFilenamef(pDef->pchFilename, "QueueDef %s is set to UseExpression, but no Condition Expression defined.", pDef->pchName);
			bValid = false;
			break;
		}
	}

	return bValid;
}

S32 queue_Validate(QueueDef *pDef)
{
	return queue_ValidateEx(pDef,0);
}

static int queuedef_ValidateCB(enumResourceValidateType eType, 
								const char *pDictName, 
								const char *pResourceName, 
								QueueDef *pQueueDef, 
								U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_POST_BINNING:
		{
			queue_Validate(pQueueDef);
			return VALIDATE_HANDLED;
		}
		xcase RESVALIDATE_POST_TEXT_READING:
		{
#if GAMESERVER || GAMECLIENT
			queue_Generate(pQueueDef);
#endif
			return VALIDATE_HANDLED;
		}
		xcase RESVALIDATE_FIX_FILENAME:
		{
			resFixPooledFilename(&pQueueDef->pchFilename, 
								 QUEUES_BASE_DIR, 
								 pQueueDef->pchScope, 
								 pQueueDef->pchName, 
								 QUEUES_EXTENSION);
			return VALIDATE_HANDLED;
		}
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN_LATE;
int queue_RegisterContainer(void)
{
	objRegisterNativeSchema(GLOBALTYPE_QUEUEINFO, parse_QueueInfo, NULL, NULL, NULL, NULL, NULL);

	return 1;
}

AUTO_RUN;
void QueueDefDictionary_Register(void)
{
	g_hQueueDefDict = RefSystem_RegisterSelfDefiningDictionary("QueueDef", false, parse_QueueDef, true, true, NULL);
	resDictManageValidation(g_hQueueDefDict, queuedef_ValidateCB);

	resDictSetDisplayName(g_hQueueDefDict, "Queue", "Queues", RESCATEGORY_DESIGN);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hQueueDefDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hQueueDefDict, ".displayNameMesg.Message", ".Scope", NULL, NULL, NULL);
		}
	}
	else
	{
		// Client loading from the server
		resDictRequestMissingResources(g_hQueueDefDict, 2, false, resClientRequestSendReferentCommand);
	}
	//resDictProvideMissingRequiresEditMode(g_hQueueDefDict);
}

//Gets the queueDef from the dictionary
QueueDef* queue_DefFromName(const char *pchName)
{
	return RefSystem_ReferentFromString(g_hQueueDefDict, pchName);
}

bool queue_IsQueueMap(ZoneMapType eMapType)
{
	switch (eMapType)
	{
	case ZMTYPE_PVP:
	case ZMTYPE_QUEUED_PVE:
		return true;
	}
	return false;
}

QueueCategoryData* queue_GetCategoryData(QueueCategory eCategory)
{
	S32 i;
	for (i = eaSize(&s_QueueCategories.eaData)-1; i >= 0; i--)
	{
		QueueCategoryData* pData = s_QueueCategories.eaData[i];
		if (pData->eCategory == eCategory)
		{
			return pData;
		}
	}
	return NULL;
}

U32 queue_QueueGetMaxPlayers(QueueDef* pDef, bool bOvertime)
{
	S32 i, n;
	U32 uiMaxPlayers = 0;

	// Count up how many players can be in each group
	n = eaSize(&pDef->eaGroupDefs);	
	for (i=0; i<n; i++)
	{
		uiMaxPlayers += queue_GetGroupMaxSize(pDef->eaGroupDefs[i], bOvertime);
	}

	return uiMaxPlayers;
}

U32 queue_QueueGetMinPlayersEx(QueueDef* pDef, bool bOvertime, bool bAutoBalance, bool bIsPrivate)
{
	S32 i, n;
	U32 uiMinPlayers = 0;

	// Count up how many players must be in each group
	n = eaSize(&pDef->eaGroupDefs);	
	for (i=0; i<n; i++)
	{
		uiMinPlayers += queue_GetGroupMinSizeEx(pDef->eaGroupDefs[i], bOvertime, bAutoBalance, bIsPrivate);
	}
	if (pDef->Limitations.iMinMembersAllGroups)
	{
		if(bIsPrivate && pDef->Limitations.bUsePrivateSettings)
		{
			MAX1(uiMinPlayers, pDef->Limitations.iPrivateMinMembersAllGroups);
		}
		else
		{
			MAX1(uiMinPlayers, pDef->Limitations.iMinMembersAllGroups);
		}
		
	}
	return uiMinPlayers;
}

//Convenience function to init the groups of a queue match
void queue_InitMatchGroups(SA_PARAM_NN_VALID QueueDef *pQueueDef, SA_PARAM_NN_VALID QueueMatch *pMatch)
{
	S32 iGroupIndex, iGroupSize = eaSize(&pQueueDef->eaGroupDefs);

	for (iGroupIndex = 0; iGroupIndex < iGroupSize; iGroupIndex++)
	{
		QueueGroup *pGroup = StructCreate(parse_QueueGroup);
		pGroup->iGroupIndex = iGroupIndex;
		pGroup->iGroupSize = 0;
		pGroup->pGroupDef = StructClone(parse_QueueGroupDef, pQueueDef->eaGroupDefs[iGroupIndex]);
		eaPush(&pMatch->eaGroups,pGroup);
	}
}

S32 queue_Match_FindMemberInGroup(SA_PARAM_OP_VALID QueueGroup* pGroup, U32 uMemberID)
{
	if (pGroup)
	{
		S32 iMemberIdx;
		for (iMemberIdx = eaSize(&pGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			QueueMatchMember* pMember = pGroup->eaMembers[iMemberIdx];
			if (pMember->uEntID == uMemberID)
			{
				return iMemberIdx;
			}
		}
	}
	return -1;
}

S32 queue_Match_FindMember(SA_PARAM_OP_VALID QueueMatch* pMatch, U32 uMemberID, S32* piGroupIndex)
{
	if (pMatch)
	{
		S32 iGroupIdx;
		for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
			S32 iMemberIdx = queue_Match_FindMemberInGroup(pGroup, uMemberID);
			if (iMemberIdx >= 0)
			{
				if (piGroupIndex)
				{
					(*piGroupIndex) = iGroupIdx;
				}
				return iMemberIdx;
			}
		}
	}
	return -1;
}

S32 queue_Match_FindGroupByTeam(SA_PARAM_OP_VALID QueueMatch* pMatch, U32 uTeamID)
{
	if (pMatch)
	{
		S32 iGroupIdx, iMemberIdx;
		for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
			for (iMemberIdx = eaSize(&pGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				QueueMatchMember* pMember = pGroup->eaMembers[iMemberIdx];
				if (pMember->uTeamID == uTeamID)
				{
					return iGroupIdx;
				}
			}
		}
	}
	return -1;
}

AUTO_TRANS_HELPER_SIMPLE;
S32 queue_Match_FindGroupByIndex(SA_PARAM_OP_VALID QueueMatch* pMatch, S32 iGroupIndex)
{
	if (pMatch)
	{
		S32 iGroupIdx;
		for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
			if (pGroup->iGroupIndex == iGroupIndex)
			{
				return iGroupIdx;
			}
		}
	}
	return -1;
}

QueueMatchMember* queue_Match_AddMemberToGroup(SA_PARAM_NN_VALID QueueMatch* pMatch, SA_PARAM_OP_VALID QueueGroup* pGroup, U32 uEntID, U32 uTeamID,
											      S32 iGroupRole, S32 iGroupClass)
{
	if (pGroup && uEntID > 0)
	{
		QueueMatchMember* pMatchMember = StructCreate(parse_QueueMatchMember);
		pMatchMember->uEntID = uEntID;
		pMatchMember->uTeamID = uTeamID;
		pMatchMember->iGroupRole = iGroupRole;
		pMatchMember->iGroupClass = iGroupClass;
		eaPush(&pGroup->eaMembers, pMatchMember);
		ea32PushUnique(&pGroup->puiInMapTeamIDs, uTeamID);	// I think this is only for Auto-Balance. Used to be an odd case in group_trh_GroupCache
		
		pGroup->iGroupSize++;
		pMatch->iMatchSize++;
		return pMatchMember;
	}
	return NULL;
}

QueueMatchMember* queue_Match_AddMember(SA_PARAM_NN_VALID QueueMatch* pMatch, S32 iGroupIndex, U32 uEntID, U32 uTeamID)
{
	QueueGroup *pGroup = eaGet(&pMatch->eaGroups, queue_Match_FindGroupByIndex(pMatch, iGroupIndex));
	return queue_Match_AddMemberToGroup(pMatch, pGroup, uEntID, uTeamID, 0, 0);  // Manual adding, not tracking role/class. What a mess.
}

bool queue_Match_RemoveMemberFromGroup(SA_PARAM_NN_VALID QueueMatch* pMatch, S32 iGroupIndex, U32 uMemberID)
{
	QueueGroup *pGroup = eaGet(&pMatch->eaGroups, queue_Match_FindGroupByIndex(pMatch, iGroupIndex));
	S32 iMemberIdx = queue_Match_FindMemberInGroup(pGroup, uMemberID);
	if (iMemberIdx >= 0)
	{
		StructDestroy(parse_QueueMatchMember, eaRemove(&pGroup->eaMembers, iMemberIdx));
		pGroup->iGroupSize--;
		pMatch->iMatchSize--;
		return true;
	}
	return false;
}

bool queue_Match_RemoveMember(SA_PARAM_NN_VALID QueueMatch *pMatch, U32 uMemberID)
{
	S32 i;
	for (i = eaSize(&pMatch->eaGroups)-1; i >= 0; i--)
	{
		QueueGroup *pGroup = pMatch->eaGroups[i];
		if (queue_Match_RemoveMemberFromGroup(pMatch, pGroup->iGroupIndex, uMemberID))
		{
			return true;
		}
	}
	return false;
}

// Fill the pMatch with the groups/members that have a valid group index
AUTO_TRANS_HELPER
ATR_LOCKS(pInstance, ".Eaunorderedmembers")
ATR_LOCKS(pMap, ".Imapkey");
void queue_trh_GroupCache(ATH_ARG NOCONST(QueueInstance) *pInstance, 
						  ATH_ARG NOCONST(QueueMap) *pMap,
						  PlayerQueueState eIncludeState,
						  PlayerQueueState eExcludeState,
						  SA_PARAM_NN_VALID QueueDef *pQueueDef, 
						  SA_PARAM_NN_VALID QueueMatch *pMatch)
{
	S32 iMemberIdx, iMemberSize = eaSize(&pInstance->eaUnorderedMembers);

	queue_InitMatchGroups(pQueueDef, pMatch);
		
	for (iMemberIdx = 0; iMemberIdx < iMemberSize; iMemberIdx++)
	{
		NOCONST(QueueMember)* pMember = pInstance->eaUnorderedMembers[iMemberIdx];
		QueueMatchMember* pMatchMember;
		QueueGroup* pGroup = NULL;

		if (ISNULL(pMember))
		{
			continue;
		}
		if (NONNULL(pMap) && pMember->iMapKey != pMap->iMapKey)
		{
			continue;
		}
		if (pMember->iGroupIndex >= 0)
		{
			S32 iGroupIndex = queue_Match_FindGroupByIndex(pMatch, pMember->iGroupIndex);
			pGroup = eaGet(&pMatch->eaGroups, iGroupIndex);
		}
		if (pMember->eState == PlayerQueueState_Limbo)
		{
			if (pGroup)
			{
				pGroup->iLimboCount++;
			}
		}
		if (eExcludeState != PlayerQueueState_None && pMember->eState == eExcludeState)
		{
			continue;
		}
		if (eIncludeState != PlayerQueueState_None && pMember->eState != eIncludeState)
		{
			if (pGroup)
			{
				pMatch->iMatchSize++;
				pGroup->iGroupSize++;
				ea32PushUnique(&pGroup->puiInMapTeamIDs, pMember->iTeamID);
			}
			continue;
		}
		if (pGroup)
		{
			pMatchMember = queue_Match_AddMemberToGroup(pMatch, pGroup, pMember->iEntID, pMember->iTeamID,
														pMember->iGroupRole, pMember->iGroupClass);
		}
	}
}


S32 queue_Match_GetAverageWaitTime(QueueInstance* pInstance, QueueMatch* pMatch)
{
	U32 uCurrentTime = timeSecondsSince2000();
	S32 iGroupIdx, iMemberIdx;
	S32 iMemberCount = 0;
	U32 uAvgWait = 0;

	if (pInstance && pMatch)
	{
		for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
			for (iMemberIdx = eaSize(&pGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				QueueMatchMember* pMatchMember = pGroup->eaMembers[iMemberIdx];
				QueueMember* pQueueMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, pMatchMember->uEntID);

				if (!pQueueMember || pQueueMember->iQueueEnteredTime > uCurrentTime)
				{
					continue;
				}
				uAvgWait += uCurrentTime - pQueueMember->iQueueEnteredTime;
				iMemberCount++;
			}
		}
	}
	if (iMemberCount)
	{
		uAvgWait /= iMemberCount;
	}
	return uAvgWait;
}

bool queue_Match_Validate(SA_PARAM_NN_VALID QueueMatch* pMatch,
						  SA_PARAM_NN_VALID QueueDef* pDef,
						  SA_PARAM_OP_VALID QueueInstanceParams* pParams,
						  bool bOvertime, bool bAutoBalance, bool bIgnoreMin)
{
	if (!bIgnoreMin && pMatch->iMatchSize < pDef->Limitations.iMinMembersAllGroups)
	{
		return false;
	}
	else
	{
		S32 iGroupIdx;
		for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			QueueGroup *pGroup = pMatch->eaGroups[iGroupIdx];
			QueueGroupDef *pGroupDef = pGroup->pGroupDef;
			S32 iMax = queue_GetGroupMaxSize(pGroupDef, bOvertime);
			
			if (pGroup->iGroupSize > iMax)
			{
				return false;
			}

			if (!bIgnoreMin)
			{
				S32 iMin = queue_GetGroupMinSizeEx(pGroupDef, bOvertime, bAutoBalance, (pParams->uiOwnerID != 0));
				if (pGroup->iGroupSize < iMin)
				{
					return false;
				}
			}
		}
	}
	return true;
}

//All we need to know about the member at the moment is their current group, affiliation and team
S32 queue_GetBestGroupIndexForPlayer(const char* pchAffiliation, 
									 U32 uTeamID, 
									 S32 iCurrentGroupIndex,
									 bool bOvertime,
									 SA_PARAM_NN_VALID QueueDef *pQueueDef, 
									 SA_PARAM_NN_VALID QueueMatch *pMatch)
{
	if (pMatch->iMatchSize < queue_QueueGetMaxPlayers(pQueueDef, bOvertime))
	{
		S32 iGroupIndex, iMemberIdx, iResultIndex = -1;
		S32 iSmallestGroupSize = -1;
		const char* pchPooledAffiliation = allocAddString(pchAffiliation);
		for(iGroupIndex = eaSize(&pMatch->eaGroups)-1; iGroupIndex >= 0; iGroupIndex--)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIndex];
			QueueGroupDef* pGroupDef = pGroup->pGroupDef;
			S32 iMax = queue_GetGroupMaxSize(pGroupDef, bOvertime);
			S32 iGroupSize = pGroup->iGroupSize;

			if (iCurrentGroupIndex == pGroup->iGroupIndex)
			{
				iGroupSize--; 
			}

			if (iGroupSize >= iMax)
				continue;

			if (pGroup->pGroupDef->pchAffiliation && pGroup->pGroupDef->pchAffiliation[0] &&	
				stricmp(pchPooledAffiliation,pGroup->pGroupDef->pchAffiliation)!=0)
				continue;

			if (uTeamID > 0)
			{
				for (iMemberIdx = eaSize(&pGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
				{
					if (uTeamID == pGroup->eaMembers[iMemberIdx]->uTeamID)
					{
						break;
					}
				}
				if (iMemberIdx >= 0)
				{
					return pGroup->iGroupIndex;
				}
			}

			if (iSmallestGroupSize < 0 || iGroupSize < iSmallestGroupSize)
			{
				iResultIndex = pGroup->iGroupIndex;
				iSmallestGroupSize = iGroupSize;
			}
		}
		if (iResultIndex >= 0)
		{
			return iResultIndex;
		}
	}
	return -1;
}

bool queue_IsValidGroupIndexForPlayer(S32 iGroupID, 
									  const char* pchAffiliation, 
									  bool bOvertime,
									  SA_PARAM_NN_VALID QueueDef *pQueueDef, 
									  SA_PARAM_NN_VALID QueueMatch *pMatch)
{
	if (pMatch->iMatchSize < queue_QueueGetMaxPlayers(pQueueDef, false))
	{
		QueueGroup* pGroup = eaGet(&pMatch->eaGroups, queue_Match_FindGroupByIndex(pMatch, iGroupID));
		if (pGroup)
		{
			QueueGroupDef* pGroupDef = pGroup->pGroupDef;
			S32 iMax = queue_GetGroupMaxSize(pGroupDef, bOvertime);
			const char* pchPooledAffiliation = allocAddString(pchAffiliation);
			if (pGroup->iGroupSize < iMax &&	
				(pGroup->pGroupDef->pchAffiliation==NULL ||	
				stricmp(pchPooledAffiliation,pGroup->pGroupDef->pchAffiliation)==0))
			{
				return true;
			}
		}
	}
	return false;
}

S32 queue_GetGroupMinSizeEx(QueueGroupDef* pDef, bool bOvertime, bool bAutoBalance, bool bIsPrivate)
{
	if (pDef)
	{
		if (bAutoBalance)
		{
			return pDef->iAutoBalanceMin ? pDef->iAutoBalanceMin : pDef->iMinTimed;
		}

		if(bIsPrivate && pDef->bUseGroupPrivateSettings)
		{
			return bOvertime ? pDef->iPrivateOverTimeSize : pDef->iPrivateMinGroupSize;
		}
		return bOvertime ? pDef->iMinTimed : pDef->iMin;
	}
	return 0;
}

S32 queue_GetGroupMaxSize(QueueGroupDef* pDef, bool bOvertime)
{
	if (pDef)
	{
		return bOvertime && pDef->iMaxTimed ? pDef->iMaxTimed : pDef->iMax;
	}
	return 0;
}

BolsterType queue_GetBolsterType(QueueDef* pDef, QueueLevelBand* pLevelBand)
{
	if (pLevelBand && pLevelBand->eBolsterType != kBolsterType_None)
	{
		return pLevelBand->eBolsterType;
	}
	return pDef ? pDef->MapSettings.eBolsterType : kBolsterType_None;
}

bool queue_Instance_AllMembersInState(QueueInstance* pInstance, PlayerQueueState eState)
{
	S32 i;
	if (!pInstance) return false;
	for (i = eaSize(&pInstance->eaUnorderedMembers)-1; i >= 0; i--)
	{
		if (pInstance->eaUnorderedMembers[i]->eState != eState)
			return false;
	}
	return true;
}

bool queue_PlayerInstance_AllMembersInState(PlayerQueueInstance* pInstance, PlayerQueueState eState)
{
	S32 i;
	if (!pInstance) return false;
	for (i = eaSize(&pInstance->eaMembers)-1; i >= 0; i--)
	{
		if (pInstance->eaMembers[i]->eState != eState)
			return false;
	}
	return true;
}

// Get the leaver penalty duration for a given QueueDef
U32 queue_GetLeaverPenaltyDuration(QueueDef* pDef)
{
	if (pDef)
	{
		QueueCategoryData* pCategoryData;
		if (pDef->Settings.uOverridePenaltyDuration)
		{
			return pDef->Settings.uOverridePenaltyDuration;
		}
		if (pCategoryData = queue_GetCategoryData(pDef->eCategory))
		{
			if (pCategoryData->uPenaltyDuration)
			{
				return pCategoryData->uPenaltyDuration;
			}
		}
	}
	return g_QueueConfig.uLeaverPenaltyDuration;
}

static QueueCannotUseReason queue_EntCanUseQueue_CheckGroups(Entity* pEnt, 
															 const char* pchEntAffiliation, 
															 QueueDef* pDef)
{
	const char* pchPooledAffiliation = allocAddString(pchEntAffiliation);
	S32 i;
	bool bMadeAffiliationButFailedRequirement=false;
	
	for (i = eaSize(&pDef->eaGroupDefs)-1; i >= 0; i--)
	{
		QueueGroupDef* pGroupDef = pDef->eaGroupDefs[i];

		//the ent can use this queue if one of the groups has a matching affiliation to the ent's
		//  Note that we allow affiliated Members to queue for GroupDefs that have no affiliation.
		if (!pGroupDef->pchAffiliation || stricmp(pchPooledAffiliation,pGroupDef->pchAffiliation)==0) 
		{
			if (pEnt && pGroupDef->pRequires)
			{
				MultiVal mVal;
				ExprContext *pContext = queue_GetContext(pEnt);
				exprEvaluate(pGroupDef->pRequires, pContext, &mVal);
				if (!MultiValGetInt(&mVal,NULL))
				{
					bMadeAffiliationButFailedRequirement=true;;
				}
				else
				{
					// Matched affiliation and requirements
					return QueueCannotUseReason_None;
				}
			}
			else
			{
				// Made affiliation. No requirements to be met (or we have no Ent)
				return QueueCannotUseReason_None;
			}
		}
	}

	if (bMadeAffiliationButFailedRequirement)
	{
		return(QueueCannotUseReason_GroupRequirements);
	}
	else
	{
		// We failed all affiliation checks. All groups must have had affiliations specified.
		if (pchPooledAffiliation==NULL || pchPooledAffiliation[0]==0)
		{
			// We have no affiliation and needed one.
			return(QueueCannotUseReason_AffiliationRequired);
		}
		else
		{
			// We have an affiliation and it did not match anything
			return(QueueCannotUseReason_InvalidAffiliation);
		}
	}
}

static bool queue_EntCannotUseQueue_CheckLeaverPenalty(Entity* pEnt, QueueDef* pDef)
{
	U32 uPenaltyEndTime = 0;
	U32 uCurrentTime = timeServerSecondsSince2000();
	PlayerQueuePenaltyData* pPenaltyData = SAFE_MEMBER3(pEnt, pPlayer, pPlayerQueueInfo, pPenaltyData);
	
	if (pDef && pPenaltyData)
	{
		QueuePenaltyCategoryData* pCategoryData = eaIndexedGetUsingInt(&pPenaltyData->eaCategories, pDef->eCategory);
		if (pCategoryData)
		{
			uPenaltyEndTime = pCategoryData->uPenaltyEndTime;
		}
	}
	if (!uPenaltyEndTime)
	{
		return false;
	}
	return uCurrentTime < uPenaltyEndTime;
}

//Checks to see if the given entity can use the given queue def, returns a message key to display if they cannot
QueueCannotUseReason queue_EntCannotUseQueue(Entity* pEnt, 
											 S32 iEntLevel, 
											 const char* pchEntAffiliation, 
											 QueueDef* pDef,
											 int iLevelBand,
											 S32 bIgnoreLevelRestrictions,
											 bool bIgnoreRequiresExpr)
{
	if (pDef)
	{
		QueueCannotUseReason eGroupReason;

		if (!bIgnoreLevelRestrictions)
		{
			if(iLevelBand >= 0)
			{
				QueueLevelBand* pLevelBand = eaGet(&pDef->eaLevelBands,iLevelBand);
				if (pLevelBand)
				{
					//check instance-specific requirements
					if (pLevelBand->iMinLevel != 0 && pLevelBand->iMinLevel > iEntLevel)
						return QueueCannotUseReason_LevelTooLow;
					if (pLevelBand->iMaxLevel != 0 && pLevelBand->iMaxLevel < iEntLevel)
						return QueueCannotUseReason_LevelTooHigh;
				}
			}
			else
			{
				QueueLevelBand* pMinLevelBand = eaGet(&pDef->eaLevelBands,0);
				QueueLevelBand* pMaxLevelBand = eaGet(&pDef->eaLevelBands,eaSize(&pDef->eaLevelBands)-1);
				if (pMinLevelBand && pMaxLevelBand)
				{
					//check to see if the player can use any queue instance
					if (pMinLevelBand->iMinLevel != 0 && pMinLevelBand->iMinLevel > iEntLevel)
						return QueueCannotUseReason_LevelTooLow;
					if (pMaxLevelBand->iMaxLevel != 0 && pMaxLevelBand->iMaxLevel < iEntLevel)
						return QueueCannotUseReason_LevelTooHigh;
				}
			}
		}

		if (!bIgnoreRequiresExpr && pDef->Requirements.pRequires)
		{
			if(!pEnt)
			{
				return QueueCannotUseReason_Requirement;
			}
			else
			{
				MultiVal mVal;
				ExprContext *pContext = queue_GetContext(pEnt);
				exprEvaluate(pDef->Requirements.pRequires, pContext, &mVal);
				if (!MultiValGetInt(&mVal,NULL))
				{
					return QueueCannotUseReason_Requirement;
				}
			}
		}
		
		//check group requirements
		if (eGroupReason = queue_EntCanUseQueue_CheckGroups(pEnt, pchEntAffiliation, pDef))
		{
			return eGroupReason;
		}
		if (queue_EntCannotUseQueue_CheckLeaverPenalty(pEnt, pDef))
		{
			return QueueCannotUseReason_LeaverPenalty;
		}
		return QueueCannotUseReason_None;
	}
	return QueueCannotUseReason_Other;
}

//Checks to see if the given entity can use the given queue instance, returns a message key to display if they cannot
QueueCannotUseReason queue_EntCannotUseQueueInstance(Entity* pEnt, 
													 S32 iEntLevel, 
													 const char* pchEntAffiliation, 
													 QueueInstanceParams* pParams, 
													 QueueDef* pDef,
													 bool bIgnoreLevelRestrictions,
													 bool bIgnoreRequiresExpr)
{
	if (pDef && pParams)
	{
		S32 iLevelBandIndex = bIgnoreLevelRestrictions ? -1 : pParams->iLevelBandIndex;
		return queue_EntCannotUseQueue(pEnt, iEntLevel, pchEntAffiliation, pDef, iLevelBandIndex, bIgnoreLevelRestrictions, bIgnoreRequiresExpr);
	}
	return QueueCannotUseReason_Other;
}

bool queue_InstanceShouldCheckOffers(QueueDef* pDef, QueueInstanceParams* pParams)
{
	if (!SAFE_MEMBER(pDef, MapSettings.bCheckOffersBeforeMapLaunch) || 
		SAFE_MEMBER(pParams, uiOwnerID))
	{
		return false;
	}
	return true;
}

// Find the best level band index for a QueueDef given a player level
S32 queue_GetLevelBandIndexForLevel(QueueDef* pDef, S32 iLevel)
{
	if (pDef && iLevel > 0)
	{
		S32 i;
		for (i = eaSize(&pDef->eaLevelBands)-1; i >= 0; i--)
		{
			QueueLevelBand* pLevelBand = pDef->eaLevelBands[i];
			if (iLevel >= pLevelBand->iMinLevel &&
				(!pLevelBand->iMaxLevel || iLevel <= pLevelBand->iMaxLevel))
			{
				return i;
			}
		}
	}
	return -1;
}

// Finds a world variable on the QueueDef at the specified level band that can be modified in private games
QueueVariableData* queue_FindVariableDataEx(QueueDef* pDef, 
											S32 iLevelBandIndex, 
											const char* pchFindVar, 
											const char* pchFindMap,
											bool bByWorldVar)
{
	QueueCustomMapData* pMapType = queue_GetCustomMapTypeFromMapName(pDef, pchFindMap);
	const char* pchFindVarPooled = NULL;
	if (bByWorldVar)
		pchFindVarPooled = allocFindString(pchFindVar);

	if (pDef && pchFindVar && (!bByWorldVar || pchFindVarPooled))
	{
		S32 i;
		QueueLevelBand* pLevelBand = eaGet(&pDef->eaLevelBands, iLevelBandIndex);
		if (pLevelBand)
		{
			for (i = eaSize(&pLevelBand->VarData.eaQueueData)-1; i >= 0; i--)
			{
				QueueVariableData* pQueueVarData = pLevelBand->VarData.eaQueueData[i];
				
				if (bByWorldVar && pQueueVarData->eType != kQueueVariableType_WorldVariable)
					continue;

				if (bByWorldVar)
				{
					if (eaFind(&pQueueVarData->ppchVarNames, pchFindVarPooled) >= 0)
					{
						if (!pMapType || !pQueueVarData->ppchCustomMapTypes ||
							eaFindString(&pQueueVarData->ppchCustomMapTypes, pMapType->pchName) >= 0)
						{
							return pQueueVarData;
						}
					}
				}
				else
				{
					if (stricmp(pQueueVarData->pchName, pchFindVar)==0)
						return pQueueVarData;
				}
			}
		}
		for (i = eaSize(&pDef->MapSettings.VarData.eaQueueData)-1; i >= 0; i--)
		{
			QueueVariableData* pQueueVarData = pDef->MapSettings.VarData.eaQueueData[i];

			if (bByWorldVar && pQueueVarData->eType != kQueueVariableType_WorldVariable)
				continue;
			
			if (bByWorldVar)
			{
				if (eaFind(&pQueueVarData->ppchVarNames, pchFindVarPooled) >= 0)
				{
					if (!pMapType || !pQueueVarData->ppchCustomMapTypes || 
						eaFindString(&pQueueVarData->ppchCustomMapTypes, pMapType->pchName) >= 0)
					{
						return pQueueVarData;
					}
				}
			}
			else
			{
				if (stricmp(pQueueVarData->pchName, pchFindVar)==0)
					return pQueueVarData;
			}
		}
	}
	return NULL;
}

static bool queue_IsVariableSupportedOnMap_Internal(QueueVariableData** eaData, 
													QueueCustomMapData* pMapType,
													const char* pchVarNamePooled)
{
	bool bFound = false;
	S32 iDataIdx;
	for (iDataIdx = eaSize(&eaData)-1; iDataIdx >= 0; iDataIdx--)
	{
		QueueVariableData* pData = eaData[iDataIdx];
		if (eaFind(&pData->ppchVarNames, pchVarNamePooled) >= 0)
		{
			if (!pData->ppchCustomMapTypes || eaFindString(&pData->ppchCustomMapTypes, pMapType->pchName) >= 0)
			{
				return true;
			}
			bFound = true;
		}
	}
	return !bFound;
}

bool queue_IsVariableSupportedOnMap(QueueDef* pDef, QueueLevelBand* pLevelBand, const char* pchVarName, const char* pchMapName)
{
	QueueCustomMapData* pMapType = queue_GetCustomMapTypeFromMapName(pDef, pchMapName);
	if (pMapType)
	{
		pchVarName = allocFindString(pchVarName);
		if (pLevelBand)
		{
			if (queue_IsVariableSupportedOnMap_Internal(pLevelBand->VarData.eaQueueData, pMapType, pchVarName))
			{
				return true;
			}
		}
		if (queue_IsVariableSupportedOnMap_Internal(pDef->MapSettings.VarData.eaQueueData, pMapType, pchVarName))
		{
			return true;
		}
		return false;
	}
	return true;
}

S64 queue_GetMapKey(ContainerID uMapID, U32 uPartitionID)
{
	return ((S64)uMapID) | ((S64)uPartitionID<<32);
}

U32 queue_GetMapIDFromMapKey(S64 iMapKey)
{
	return (U32)(iMapKey);
}

U32 queue_GetPartitionIDFromMapKey(S64 iMapKey)
{
	return (U32)(iMapKey>>32);
}

QueueCustomMapData* queue_GetCustomMapTypeFromMapName(QueueDef* pDef, const char* pchMapName)
{
	if (pDef && pchMapName && pchMapName[0])
	{
		S32 iTypeIdx;
		for (iTypeIdx = eaSize(&pDef->QueueMaps.eaCustomMapTypes)-1; iTypeIdx >= 0; iTypeIdx--)
		{
			QueueCustomMapData* pMapType = pDef->QueueMaps.eaCustomMapTypes[iTypeIdx];
			if (eaFindString(&pMapType->ppchMaps, pchMapName) >= 0)
			{
				return pMapType;
			}
		}
	}
	return NULL;
}

const char* queue_GetMapNameByIndex(QueueDef* pDef, S32 iMapIndex)
{
	if (pDef && iMapIndex >= 0)
	{
		S32 iSize = eaSize(&pDef->QueueMaps.ppchMapNames);
		if (iMapIndex < iSize)
		{
			return eaGet(&pDef->QueueMaps.ppchMapNames, iMapIndex);
		}
		else
		{
			S32 iTypeIdx, iOffsetIdx = iMapIndex - iSize;
			for (iTypeIdx = 0; iTypeIdx < eaSize(&pDef->QueueMaps.eaCustomMapTypes); iTypeIdx++)
			{
				QueueCustomMapData* pMapType = pDef->QueueMaps.eaCustomMapTypes[iTypeIdx];
				iSize = eaSize(&pMapType->ppchMaps);
				if (iOffsetIdx < iSize)
				{
					return eaGet(&pMapType->ppchMaps, iOffsetIdx);
				}
				iOffsetIdx -= iSize;
			}
		}
	}
	return NULL;
}

S32 queue_GetMapIndexByName(QueueDef* pDef, const char* pchMapName)
{
	S32 iOffsetIdx = -1;
	if (pDef)
	{
		S32 iMapIdx = eaFindString(&pDef->QueueMaps.ppchMapNames, pchMapName);
		if (iMapIdx >= 0)
		{
			return iMapIdx;
		}
		else
		{
			S32 iTypeIdx;
			iOffsetIdx = eaSize(&pDef->QueueMaps.ppchMapNames);
			for (iTypeIdx = 0; iTypeIdx < eaSize(&pDef->QueueMaps.eaCustomMapTypes); iTypeIdx++)
			{
				QueueCustomMapData* pMapType = pDef->QueueMaps.eaCustomMapTypes[iTypeIdx];
				iMapIdx = eaFindString(&pMapType->ppchMaps, pchMapName);
				if (iMapIdx >= 0)
				{
					return iMapIdx + iOffsetIdx;
				}
				iOffsetIdx += eaSize(&pMapType->ppchMaps);
			}
		}
		if (pchMapName)
		{
			return -1;
		}
	}
	return iOffsetIdx;
}


// -1 if we have no effective time limit. Due to legacy champions stuff we need to honor time limits of zero. Though it
//  is unclear how this would actually function.
int queue_GetJoinTimeLimit(QueueDef* pDef)
{
	if(pDef)
	{
		if(g_QueueConfig.bQueueJoinTimeIsExact || pDef->Limitations.uJoinTimeLimit != 0)
		{
			// Uint to int conversion. 
			if (pDef->Limitations.uJoinTimeLimit > 0x7fffffff)
			{
				return(0x7fffffff);
			}
			else
			{
				return((int)(pDef->Limitations.uJoinTimeLimit));
			}
		}
	}
	return(-1);
}

//bool Queue_InfiniteJoinTime(QueueDef* pDef)
//{
//	if(pDef)
//	{
;//		if(!g_QueueConfig.bQueueJoinTimeIsExact && pDef->Limitations.uJoinTimeLimit == 0)
//		{
//			return true;
//		}
//	}
//
//	return false;
//}

bool queue_MapJoinTimeLimitIsOkay(QueueMap* pMap, QueueDef* pDef)
{
	U32 uCurrentTime = timeSecondsSince2000();
	int iJoinTimeLimit = queue_GetJoinTimeLimit(pDef);
		
	if (iJoinTimeLimit<0 ||
		pMap->iMapLaunchTime == 0 ||
		uCurrentTime < pMap->iMapLaunchTime + iJoinTimeLimit)
	{
		return true;
	}
	return false;
}

// Test the map state and the join time limit to see if the map is accepting new joins
bool queue_MapAcceptingNewMembers(QueueMap *pMap, QueueDef *pDef)
{
	if (pMap->eMapState == kQueueMapState_Open)
	{
		return true;
	}
	if (!queue_MapJoinTimeLimitIsOkay(pMap, pDef))
	{
		return false;
	}

	if (pMap->eMapState == kQueueMapState_LaunchCountdown ||
		pMap->eMapState == kQueueMapState_LaunchPending ||	
		pMap->eMapState == kQueueMapState_Launched ||	
		pMap->eMapState == kQueueMapState_Active)
	{
		return true;
	}
	return false;
}



bool queue_IsMapFull(QueueMap* pMap, 
					 QueueInstance* pInstance, 
					 QueueDef* pDef, 
					 S32 iTeamSize, 
					 const char* pchAffiliation, 
					 bool bOvertime)
{
	bool bMapFull = false;
	if (pMap && pInstance && pDef)
	{
		QueueMatch QMatch = {0};
		S32 iGroupIdx;
		queue_GroupCacheWithState(pInstance, pDef, pMap, PlayerQueueState_InMap, PlayerQueueState_None, &QMatch);

		if (pDef->Settings.bSplitTeams)
		{
			S32 iFreeSlots = 0;
			for (iGroupIdx = eaSize(&QMatch.eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
			{
				QueueGroup *pGroup = QMatch.eaGroups[iGroupIdx];
				QueueGroupDef *pGroupDef = pGroup->pGroupDef;
				if (pGroupDef)
				{
					const char* pchGroupAffiliation = pGroupDef->pchAffiliation;
					S32 iMaxGroupSize, iCurGroupSize;

					if (pchGroupAffiliation && stricmp(pchAffiliation,pchGroupAffiliation) != 0)
						continue;

					iCurGroupSize = eaSize(&pGroup->eaMembers);
					iMaxGroupSize = queue_GetGroupMaxSize(pGroupDef, bOvertime);
					iFreeSlots += MAX(iMaxGroupSize - iCurGroupSize, 0);
				}
			}
			if (iTeamSize > iFreeSlots)
			{
				bMapFull = true;
			}
		}
		else
		{
			for (iGroupIdx = eaSize(&QMatch.eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
			{
				QueueGroup *pGroup = QMatch.eaGroups[iGroupIdx];
				QueueGroupDef *pGroupDef = pGroup->pGroupDef;
				if (pGroupDef)
				{
					const char* pchGroupAffiliation = pGroupDef->pchAffiliation;
					S32 iMaxGroupSize, iCurGroupSize;

					if (pchGroupAffiliation && stricmp(pchAffiliation,pchGroupAffiliation) != 0)
						continue;
			
					iCurGroupSize = eaSize(&pGroup->eaMembers);
					iMaxGroupSize = queue_GetGroupMaxSize(pGroupDef, bOvertime);
					if (iCurGroupSize + iTeamSize <= iMaxGroupSize)
					{
						break;
					}
				}
			}
			if (iGroupIdx < 0)
			{
				bMapFull = true;
			}
		}
		StructDeInit(parse_QueueMatch, &QMatch);
	}
	return bMapFull;
}

bool queue_IsPlayerQueueMapFull(PlayerQueueMap* pMap, 
								QueueDef* pDef, 
								S32 iTeamSize, 
								const char* pchAffiliation, 
								bool bOvertime)
{
	bool bMapFull = false;
	if (pMap && pDef)
	{
		S32 iGroupIdx;
		if (pDef->Settings.bSplitTeams)
		{
			S32 iFreeSlots = 0;
			for (iGroupIdx = eaiSize(&pMap->piGroupPlayerCounts)-1; iGroupIdx >= 0; iGroupIdx--)
			{
				QueueGroupDef* pGroupDef = eaGet(&pDef->eaGroupDefs, iGroupIdx);
				if (pGroupDef)
				{
					const char* pchGroupAffiliation = pGroupDef->pchAffiliation;
					S32 iMaxGroupSize, iCurGroupSize;

					if (pchGroupAffiliation && stricmp(pchAffiliation,pchGroupAffiliation) != 0)
						continue;

					iCurGroupSize = pMap->piGroupPlayerCounts[iGroupIdx];
					iMaxGroupSize = queue_GetGroupMaxSize(pGroupDef, bOvertime);
					iFreeSlots += MAX(iMaxGroupSize - iCurGroupSize, 0);
				}
			}
			if (iTeamSize > iFreeSlots)
			{
				bMapFull = true;
			}
		}
		else
		{
			for (iGroupIdx = eaiSize(&pMap->piGroupPlayerCounts)-1; iGroupIdx >= 0; iGroupIdx--)
			{
				QueueGroupDef* pGroupDef = eaGet(&pDef->eaGroupDefs, iGroupIdx);
				if (pGroupDef)
				{
					const char* pchGroupAffiliation = pGroupDef->pchAffiliation;
					S32 iMaxGroupSize, iCurGroupSize;

					if (pchGroupAffiliation && stricmp(pchAffiliation,pchGroupAffiliation) != 0)
						continue;
			
					iCurGroupSize = pMap->piGroupPlayerCounts[iGroupIdx];
					iMaxGroupSize = queue_GetGroupMaxSize(pGroupDef, bOvertime);
					if (iCurGroupSize + iTeamSize <= iMaxGroupSize)
					{
						break;
					}
				}
			}
			if (iGroupIdx < 0)
			{
				bMapFull = true;
			}
		}
	}
	return bMapFull;
}

bool queue_GetVariableOverrideValue(const QueueGameSetting** eaSettings,
									QueueVariableData* pQueueVarData,
									S32* piValue)
{
	if (pQueueVarData && pQueueVarData->pSettingData)
	{
		S32 iSettingIdx;
		for (iSettingIdx = eaSize(&eaSettings)-1; iSettingIdx >= 0; iSettingIdx--)
		{
			const QueueGameSetting* pSetting = eaSettings[iSettingIdx];

			if (stricmp(pSetting->pchQueueVarName, pQueueVarData->pchName) != 0)
			{
				continue;
			}

			(*piValue) = MAX(pSetting->iValue, pQueueVarData->pSettingData->iMinValue);
			if (pQueueVarData->pSettingData->iMaxValue)
			{
				MIN1((*piValue), pQueueVarData->pSettingData->iMaxValue);
			}
			return true;
		}
	}
	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pParams, ".Uiownerid");
bool queue_trh_IsPrivateMatch(ATH_ARG NOCONST(QueueInstanceParams)* pParams)
{
	return NONNULL(pParams) && pParams->uiOwnerID > 0;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pParams, ".Uiownerid");
bool queue_trh_CanLeaveQueue(ATH_ARG NOCONST(QueueInstanceParams)* pParams, PlayerQueueState eMemberState)
{
	if (eMemberState == PlayerQueueState_InMap)
	{
		return false;
	}
	if (!queue_trh_IsPrivateMatch(pParams))
	{
		if (eMemberState == PlayerQueueState_Accepted ||
			eMemberState == PlayerQueueState_Countdown)
		{
			return false;
		}
	}
	return true;
}

QueuePrivateNameInvalidReason queue_GetPrivateNameInvalidReason(const char* pchPrivateName)
{
	if (pchPrivateName && pchPrivateName[0])
	{
		S32 iNameLength = (S32)strlen(pchPrivateName);
		if (iNameLength < QUEUE_PRIVATE_NAME_MIN_LENGTH)
		{
			return kQueuePrivateNameInvalidReason_MinLength;
		}
		if (iNameLength > QUEUE_PRIVATE_NAME_MAX_LENGTH)
		{
			return kQueuePrivateNameInvalidReason_MaxLength;
		}
		if (IsAnyProfane(pchPrivateName))
		{
			return kQueuePrivateNameInvalidReason_Profanity;
		}
	}
	return kQueuePrivateNameInvalidReason_None;
}

U32 queue_GetOriginalOwnerID(SA_PARAM_OP_VALID QueueInstance* pInstance)
{
	if (pInstance)
	{
		if (pInstance->uOrigOwnerID)
		{
			return pInstance->uOrigOwnerID;
		}
		return SAFE_MEMBER(pInstance->pParams, uiOwnerID);
	}
	return 0;
}

void queue_GetPrivateChatChannelName(char** pestrChannel, U32 uOwnerID, U32 uInstanceID)
{
	estrPrintf(pestrChannel, "%s%d%d", QUEUE_CHANNEL_PREFIX, uOwnerID, uInstanceID);
}

void queue_SendResultMessageEx(U32 iEntID, 
							   U32 iTargetEntID, 
							   const char *pcQueueName, 
							   const char *pcMessageKey,
							   const char *pchCallerFunction)
{
#if defined(GAMESERVER) || defined(APPSERVER)
	if (!iEntID)
	{
		ErrorDetailsf("Called from function %s", pchCallerFunction);
		Errorf("queue_SendResultMessage: Recipient EntID is zero");
	}
	else
	{
		RemoteCommand_gslQueue_ResultMessage(GLOBALTYPE_ENTITYPLAYER, 
										 iEntID, 
										 iEntID, 
										 iTargetEntID,
										 pcQueueName,
										 pcMessageKey);
	}
#endif
}

S32 queue_GetNewMapIndex(SA_PARAM_OP_VALID QueueInstance *pInstance, U32 uiMapCreateRequestID)
{
	S32 i;

	if(pInstance)
	{
		for(i = 0;i < eaSize(&pInstance->eaNewMaps); ++i)
		{
			if(pInstance->eaNewMaps[i]->uiMapCreateRequestID == uiMapCreateRequestID)
			{
				return i;
			}
		}
	}

	return -1;
}

QueueMap *queue_GetNewMap(SA_PARAM_OP_VALID QueueInstance *pInstance, U32 uiMapCreateRequestID)
{
	S32 index = queue_GetNewMapIndex(pInstance, uiMapCreateRequestID);

	if(index >=0 && index < eaSize(&pInstance->eaNewMaps))
	{
		return pInstance->eaNewMaps[index];
	}
	
	return NULL;
}

// safely destroy the new map
void queue_DestroyNewMap(SA_PARAM_OP_VALID QueueInstance *pInstance, U32 uiMapCreateRequestID)
{
	S32 index = queue_GetNewMapIndex(pInstance, uiMapCreateRequestID);
	if(index >=0 && index < eaSize(&pInstance->eaNewMaps))
	{
		QueueMap *pNewMap = eaRemove(&pInstance->eaNewMaps, index);
		if(pNewMap)
		{
			StructDestroy(parse_QueueMap, pNewMap);
		}
	}
}

bool queue_InstanceIsPrivate(SA_PARAM_OP_VALID QueueInstance *pInstance)
{
	if(pInstance && pInstance->pParams && pInstance->pParams->uiOwnerID != 0)	
	{
		return true;
	}

	return false;
}

bool queue_PlayerInstanceIsPrivate(SA_PARAM_OP_VALID PlayerQueueInstance *pInstance)
{
	if(pInstance && pInstance->pParams && pInstance->pParams->uiOwnerID != 0)	
	{
		return true;
	}

	return false;
}

U32 queue_GetMaxTimeToWait(SA_PARAM_OP_VALID QueueDef *pQueueDef, SA_PARAM_OP_VALID QueueInstance *pInstance)
{
	if(pQueueDef)
	{
		if(queue_InstanceIsPrivate(pInstance) && pQueueDef->Limitations.bUsePrivateSettings)
		{
			return pQueueDef->Limitations.iPrivateMaxWaitTime;
		}

		return pQueueDef->Limitations.iMaxTimeToWait;
	}

	return 0;
}

S32 queue_GetSettingMapLevel(PlayerQueueInstance *pInstance)
{
	// find map level
	S32 i, iLevel = -1;
	if(pInstance)
	{
		for(i = 0; i < eaSize(&pInstance->eaSettings); ++i)
		{
			if(pInstance->eaSettings[i]->pchQueueVarName && strstri(pInstance->eaSettings[i]->pchQueueVarName, "maplevel"))
			{
				iLevel = pInstance->eaSettings[i]->iValue;
				break;
			}
		}
	}

	return iLevel;
}

bool queue_PrivateQueueLevelCheck(Entity *pEntity, PlayerQueueInstance *pInstance)
{
	if(pEntity && pInstance)
	{
		if(g_QueueConfig.bEnablePrivateQueueLevelLimit)
		{
			S32 iLevel = queue_GetSettingMapLevel(pInstance);
			S32 i;
			// override in place, check player
			if(iLevel >= 0)
			{
				for(i = 0; i < eaSize(&pInstance->eaMembers); ++i)
				{
					S32 iLevelDif = pInstance->eaMembers[i]->iLevel - iLevel;
					if(iLevelDif > g_QueueConfig.iPrivateQueueLevelLimit)
					{
						return false;
					}
				}
			}
		}

		return true;
	}

	return false;
}

bool queue_EntPrivateQueueLevelCheck(Entity *pEntity, PlayerQueueInstance *pInstance)
{
	if(pEntity && pInstance)
	{
		S32 iLevel = queue_GetSettingMapLevel(pInstance);
		// override in place, check player
		if(iLevel >= 0)
		{
			S32 iLevelDif = entity_GetSavedExpLevel(pEntity) - iLevel;
			if(iLevelDif > g_QueueConfig.iPrivateQueueLevelLimit)
			{
				return false;
			}
		}

		return true;
	}
	return false;
}


const char* queue_EntGetQueueAffiliation(Entity *pEntity)
{
	const char* pchAffiliation = NULL;
	
	if (pEntity!=NULL)
	{
		if (g_QueueConfig.bUseGuildAllegianceForAffiliation)
		{
			Guild *pGuild = guild_GetGuild(pEntity);
			if (pGuild!=NULL)
			{
				return (pGuild->pcAllegiance);  // A pooled string
			}
		}
		else
		{
			// Standard old-style of using the entity's allegiance.
			return(REF_STRING_FROM_HANDLE(pEntity->hAllegiance));
		}
	}
	return(NULL);
}


void queue_EntFillJoinCriteria(Entity *pEntity,	QueueMemberJoinCriteria* pMemberJoinCriteria)
{
	if (pEntity!=NULL && pMemberJoinCriteria!=NULL)
	{
		pMemberJoinCriteria->pchAffiliation = allocAddString(queue_EntGetQueueAffiliation(pEntity));
		pMemberJoinCriteria->iLevel = entity_GetSavedExpLevel(pEntity);
		pMemberJoinCriteria->iRank = Officer_GetRank(pEntity);
		pMemberJoinCriteria->iGroupRole = 0;
		pMemberJoinCriteria->iGroupClass = 0;

		if (gConf.bQueueSmartGroupNNO)
		{
			NNO_queue_EntFillJoinCriteria(pEntity, pMemberJoinCriteria);
		}
	}
}


AUTO_STARTUP(Queues);
void queue_DummyLoadFunctionForNonGameServers(void)
{

}

#include "autogen/queue_common_h_ast.c"
