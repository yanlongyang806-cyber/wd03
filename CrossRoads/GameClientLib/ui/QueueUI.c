#include "Character.h"
#include "CharacterClass.h"
#include "chatCommonStructs.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "Estring.h"
#include "Expression.h"
#include "GameClientLib.h"
#include "GameStringFormat.h"
#include "gclControlScheme.h"
#include "gclEntity.h"
#include "Guild.h"
#include "InteractionUI.h"
#include "OfficerCommon.h"
#include "Player.h"
#include "queue_common.h"
#include "queue_common_structs.h"
#include "RegionRules.h"
#include "rewardCommon.h"
#include "StashTable.h"
#include "StringCache.h"
#include "Team.h"
#include "UIGen.h"
#include "WorldGrid.h"
#include "NotifyCommon.h"

#include "entenums_h_ast.h"
#include "queue_common_h_ast.h"
#include "queue_common_structs_h_ast.h"
#include "UIGen_h_ast.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "QueueUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("QueueDesc", BUDGET_UISystem););

#define queue_StateMeansInQueue(eState) ((eState) == PlayerQueueState_InQueue || \
										(eState) == PlayerQueueState_Offered || \
										(eState) == PlayerQueueState_Accepted || \
										(eState) == PlayerQueueState_Countdown || \
										(eState) == PlayerQueueState_WaitingForTeam)

AUTO_STRUCT;
typedef struct QueueDesc
{
	const char* pchQueueName;		AST(POOL_STRING, KEY)
	QueueDef *pDef;					AST(UNOWNED)
	const char* pchIcon;			AST(POOL_STRING)
	const char* pchCategoryIcon;	AST(POOL_STRING)
	const char* pchRequiredMission; AST(POOL_STRING)
	const char* pchRequiredActivity; AST(POOL_STRING)
	char* pchCategoryDisplayName;
	char* pchPrivateName;
	char* pchFormattedDisplayName;	AST(ESTRING)
	char* pchFormattedDescription;	AST(ESTRING)
	char* pchFormattedName;			AST(ESTRING)
	DisplayMessage displayNameMesg;	AST(STRUCT(parse_DisplayMessage))
	DisplayMessage descriptionMesg;	AST(STRUCT(parse_DisplayMessage))
	DisplayMessage cooldownDisplayNameMesg; AST(STRUCT(parse_DisplayMessage))
	DisplayMessage cooldownDescriptionMesg; AST(STRUCT(parse_DisplayMessage))
	QueueCategory eCategory;
	QueueReward eReward;
	QueueDifficulty eDifficulty;
	S32 iExpectedGameTime;
	QueueInstanceParams* pParams;	AST(UNOWNED)
	QueueCannotUseReason eCannotUseReason; AST(NAME(CannotUseReason))
	PlayerQueueState eQueueState;
	ControlSchemeRegionType eRegionFlags;
	U32 uCooldownTime;
	U32 uiInstanceID;
	S64 iMapKey;					AST(ADDNAMES(uiMapID))
	U32 uiOwnerID;
	U32 uAvgWaitTime;
	U32 uElapsedMapTime;			AST(NAME(ElapsedMapTime))
	S32 iPlayerCountMax;
	S32 iPlayerCountMin;
	S32* piGroupPlayerCount;
	S32 iTotalPlayerCount;
	S32 iSimpleStatus;
	REF_TO(AllegianceDef) hGroup1Allegiance;
	REF_TO(AllegianceDef) hGroup2Allegiance;
	bool bInQueue : 1;
	bool bBolster : 1;
	bool bAllGroupsSameAffiliation : 1;
	bool bOvertime : 1;
	bool bHasPassword : 1;
} QueueDesc;

AUTO_STRUCT;
typedef struct QueueMemberDesc
{
	const char* pchName;			AST(UNOWNED)
	const char* pchAllegiance;		AST(UNOWNED)
	REF_TO(CharacterClass) hClass;
	PlayerQueueState eState;
	U32 uiEntID;
	S32 iLevel;
	S32 iRank;
	S32 iGroupID;
	U32 uiTeamID;
	U32 uiLastUpdateFrame;
	U32 bIsHeader : 1;
	U32 bIsTeammate : 1;
	U32 bIsGuildmate : 1;
	U32 bIsFriend : 1;
	U32 bFirstTeamMember : 1;
	U32 bLastTeamMember : 1;
} QueueMemberDesc;

AUTO_STRUCT;
typedef struct QueueRulesData
{
	char* pchString;
	const char* pchMapName; AST(POOL_STRING)
	S32 iIndex;
	S32 iMinLevel;
	S32 iMaxLevel;
	S32 iBolsterLevel;
	bool bEmpty;
	bool bBolster;
} QueueRulesData;

AUTO_STRUCT;
typedef struct QueueGameSettingsData
{
	const char* pchDisplayName; AST(UNOWNED)
	char* pchName;
	S32 iValue;
	S32 iMinValue;
	S32 iMaxValue;
	S32 iRewardMinValue;
} QueueGameSettingsData;

AUTO_STRUCT;
typedef struct QueueVoteDataUI
{
	QueueVoteType eType;
	char* pchInitiatorName;
	char* pchTargetPlayerName;
	U32 uInitiatorID;
	U32 uTargetPlayerID;
	S32 iVoteCount;
	S32 iVotesRequired;
	S32 iGroupSize;
	bool bAlreadyVoted;
} QueueVoteDataUI;

typedef struct QueueInfoUI
{
	const char* pchPrivateQueue;
	U32 uPrivateInstance;
	ZoneMapType ePrivateMapType;
	S32 iReadyCount;
	U32 uLastUpdateMs;
} QueueInfoUI;

AUTO_STRUCT;
typedef struct QueuePenaltyDataUI
{
	const char* pchCategoryDisplayName; AST(UNOWNED)
	S32 eCategory; AST(NAME(Category))
	U32 uPenaltyTimeLeft; AST(NAME(PenaltyTimeLeft))
} QueuePenaltyDataUI;

static QueueMatch* s_pInviteMatch = NULL;
static NOCONST(QueueGameSetting)** s_eaSettings = NULL;
static QueueVoteDataUI* s_pVoteData = NULL;
static U32 s_uNextConcedeTime = 0;
static U32 s_uNextVoteKickTime = 0;
static NOCONST(QueueMemberPrefs) *s_pMatchPrefs = NULL;
static QueueDesc s_SelectedQueue;

AUTO_ENUM;
typedef enum QueueGetInstanceListFlag {
	// Include individual map instances
	kGetInstanceListFlag_Maps = 1,
	// Include the best ready queue
	kGetInstanceListFlag_BestReadyQueue = 2,
	// Include private games
	kGetInstanceListFlag_PrivateGames = 4,
	// Include public games
	kGetInstanceListFlag_PublicGames = 8,
	// Include owned instances
	kGetInstanceListFlag_OwnedInstances = 16,
	// Include very basic template information. This is for
	// backwards compatibility, prefer Templates instead.
	kGetInstanceListFlag_Templates = 32,
	// Include games with a password
	kGetInstanceListFlag_Password = 64,
	// Include games without a password
	kGetInstanceListFlag_NoPassword = 128,
	// Include unowned instances
	kGetInstanceListFlag_UnownedInstances = 256,
	// Exclude queues that you are in the queue for
	kGetInstanceListFlag_ExcludeQueued = 512,
	// Include the first instance information on Templates
	kGetInstanceListFlag_TemplatesIncludeInstance = 1024,
	// Include only games that are flagged as RequireSameGuild
	kGetInstanceListFlag_RequireSameGuild = 2048,

	// Include non-map specific instances (OwnedInstances | UnownedInstances)
	kGetInstanceListFlag_Instances = kGetInstanceListFlag_OwnedInstances | kGetInstanceListFlag_UnownedInstances,
} QueueGetInstanceListFlag;

AUTO_STARTUP(QueueUI,1) ASTRT_DEPS(Queues);
void queueUIInit(void)
{
	ui_GenInitStaticDefineVars(PlayerQueueStateEnum, "PlayerQueueState_");
	ui_GenInitStaticDefineVars(QueueCannotUseReasonEnum, "QueueCannotUseReason_");
	ui_GenInitStaticDefineVars(QueueCategoryEnum, "QueueCategory_");
	ui_GenInitStaticDefineVars(QueueDifficultyEnum, "QueueDifficulty_");
	ui_GenInitStaticDefineVars(QueueGetInstanceListFlagEnum, "QueueList_");
	ui_GenInitStaticDefineVars(QueueVoteTypeEnum, "QueueVoteType_");
	
}

AUTO_STARTUP(Queues) ASTRT_DEPS(QueueCategories, QueueCooldowns);
void ASTRT_Queues_Load(void)
{
	Queues_LoadConfig();
}

static const char* queue_StateToStr(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PlayerQueueInstance *pInstance)
{
	if(!pEnt || !pInstance)
		return("");
	if (pInstance->eQueueState == PlayerQueueState_Countdown)
		return entTranslateMessageKey(pEnt, "QueueUI.Countdown");
	if (pInstance->eQueueState == PlayerQueueState_Accepted)
		return entTranslateMessageKey(pEnt, "QueueUI.Accepted");
	if (pInstance->eQueueState == PlayerQueueState_Invited)
		return entTranslateMessageKey(pEnt, "QueueUI.Invited");
	if (pInstance->eQueueState == PlayerQueueState_Offered)
		return entTranslateMessageKey(pEnt, "QueueUI.Ready");
	else if (pInstance->eQueueState == PlayerQueueState_Delaying)
		return entTranslateMessageKey(pEnt, "QueueUI.Snoozing");
	else
		return("");
}

static void gclQueue_UpdateQueueInfoFromMapTypes(Entity *pEnt, QueueInfoUI** ppData, S32 eQueueMapType)
{
	static QueueInfoUI s_QueueData = {0};
	PlayerQueueInfo *pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	if (pQueueInfo)
	{
		if (!eQueueMapType || gGCLState.totalElapsedTimeMs != s_QueueData.uLastUpdateMs)
		{
			int i, j;
			ZeroStruct(&s_QueueData);
			for (i = eaSize(&pQueueInfo->eaQueues)-1; i >= 0; i--)
			{
				PlayerQueue* pQueue = pQueueInfo->eaQueues[i];
				QueueDef* pDef = GET_REF(pQueue->hDef);

				if(eQueueMapType && (!pDef || pDef->MapSettings.eMapType != eQueueMapType))
					continue;

				for (j = eaSize(&pQueue->eaInstances)-1; j >= 0; j--)
				{
					PlayerQueueInstance* pInstance = pQueue->eaInstances[j];

					if (pInstance->eQueueState == PlayerQueueState_Offered ||	
						pInstance->eQueueState == PlayerQueueState_Invited ||
						pInstance->eQueueState == PlayerQueueState_Accepted ||
						pInstance->eQueueState == PlayerQueueState_Countdown)
					{
						s_QueueData.iReadyCount++;
					}
					if (pInstance->pParams && pInstance->pParams->uiOwnerID > 0)
					{
						if (pInstance->eQueueState == PlayerQueueState_InQueue ||	
							pInstance->eQueueState == PlayerQueueState_Offered ||	
							pInstance->eQueueState == PlayerQueueState_Accepted ||
							pInstance->eQueueState == PlayerQueueState_Countdown)
						{
							
							s_QueueData.pchPrivateQueue = allocAddString(pQueue->pchQueueName);
							s_QueueData.uPrivateInstance = pInstance->uiID;
							s_QueueData.ePrivateMapType = SAFE_MEMBER(pDef, MapSettings.eMapType);
						}
					}
				}
			}
			if(!eQueueMapType)
				s_QueueData.uLastUpdateMs = gGCLState.totalElapsedTimeMs;
			else
				s_QueueData.uLastUpdateMs = 0;
		}
		if (ppData)
		{
			(*ppData) = &s_QueueData;
		}
	}
	else
	{
		if (ppData)
		{
			(*ppData) = NULL;
		}
	}
}

static void gclQueue_UpdateQueueInfo(Entity* pEnt, QueueInfoUI** ppData)
{
	gclQueue_UpdateQueueInfoFromMapTypes(pEnt,ppData,0);
}

static bool Queue_GetBestReadyQueueOfType(PlayerQueueInfo* pPlayerQueueInfo, 
										  ZoneMapType eType,
										  S32* piBestQueueIndex, 
										  S32* piBestInstanceIndex)
{
	int i, j, n = eaSize(&pPlayerQueueInfo->eaQueues);
	PlayerQueueState eBestState = PlayerQueueState_None;
	U32 uiBestTime = 0;
	for (i = 0; i < n; i++)
	{
		PlayerQueue* pQueue = pPlayerQueueInfo->eaQueues[i];
		QueueDef *pDef = GET_REF(pQueue->hDef);
		
		if (!pDef)
		{
			continue;
		}
		for (j = eaSize(&pQueue->eaInstances)-1; j >= 0; j--)
		{
			PlayerQueueInstance* pInstance = pQueue->eaInstances[j];

			if (!queue_InstanceShouldCheckOffers(pDef, pInstance->pParams) && 
				pInstance->eQueueState == PlayerQueueState_Accepted)
			{
				return false;
			}
			if ((eType == ZMTYPE_UNSPECIFIED || pDef->MapSettings.eMapType == eType) 
				&&	pInstance->eQueueState == PlayerQueueState_Offered 
				||	pInstance->eQueueState == PlayerQueueState_Delaying
				||	pInstance->eQueueState == PlayerQueueState_Invited
				||	pInstance->eQueueState == PlayerQueueState_Accepted
				||	pInstance->eQueueState == PlayerQueueState_Countdown)
			{
				if (eBestState == PlayerQueueState_None || 
					pInstance->eQueueState < eBestState || 
					pInstance->eQueueState == PlayerQueueState_Accepted ||
					pInstance->eQueueState == PlayerQueueState_Countdown || 
					(pInstance->eQueueState == eBestState && pInstance->uSecondsRemaining < uiBestTime))
				{
					eBestState = pInstance->eQueueState;
					uiBestTime = pInstance->uSecondsRemaining;
					(*piBestQueueIndex) = i;
					(*piBestInstanceIndex) = j;
					
					// These states are all mutually exclusive, and get the highest priority
					if (eBestState == PlayerQueueState_Countdown ||
						eBestState == PlayerQueueState_Invited ||	
						eBestState == PlayerQueueState_Accepted)
					{
						return true;
					}
				}
			}
		}
	}
	return (eBestState != PlayerQueueState_None);
}

SA_ORET_OP_VALID static PlayerQueueInstance* Queue_GetReadyQueueOfType(SA_PARAM_OP_VALID Entity *pEnt, 
																   ZoneMapType eType, 
																   QueueDef** ppDef)
{
	PlayerQueueInfo* pPlayerQueueInfo = (pEnt && pEnt->pPlayer) ? pEnt->pPlayer->pPlayerQueueInfo : NULL;
	if (pPlayerQueueInfo)
	{
		S32 iBestQueueIdx = -1;
		S32 iBestInstanceIdx = -1;

		if (Queue_GetBestReadyQueueOfType(pPlayerQueueInfo, eType, &iBestQueueIdx, &iBestInstanceIdx))
		{
			if (ppDef)
			{
				(*ppDef) = GET_REF(pPlayerQueueInfo->eaQueues[iBestQueueIdx]->hDef);
			}
			return pPlayerQueueInfo->eaQueues[iBestQueueIdx]->eaInstances[iBestInstanceIdx];
		}
	}
	return NULL;
}

static QueueIntArray* gclQueue_CreateInviteListFromMatch(SA_PARAM_NN_VALID QueueMatch* pMatch)
{
	QueueIntArray* pList = StructCreate(parse_QueueIntArray);
	S32 i, j;
	for (i = eaSize(&s_pInviteMatch->eaGroups)-1; i >= 0; i--)
	{
		QueueGroup* pGroup = s_pInviteMatch->eaGroups[i];
		for (j = eaSize(&pGroup->eaMembers)-1; j >= 0; j--)
		{
			ea32Push(&pList->piArray, pGroup->eaMembers[j]->uEntID);
		}
	}
	return pList;
}

static void gclQueue_GetGameSettings(SA_PARAM_NN_VALID UIGen* pGen, 
									 Entity* pEnt,
									 QueueDef* pDef, 
									 QueueCustomMapData* pMapType, 
									 QueueGameSetting** eaSettings,
									 S32 iLevelBandIndex)
{
	QueueGameSettingsData*** peaData = ui_GenGetManagedListSafe(pGen, QueueGameSettingsData);
	S32 i, j, iCount = 0;

	if (pEnt && pDef && pMapType)
	{
		for (i = 0; i < eaSize(&pDef->MapSettings.VarData.eaQueueData); i++)
		{
			QueueVariableData* pVarData = pDef->MapSettings.VarData.eaQueueData[i];
			if (!pVarData->pSettingData)
			{
				continue;
			}
			if (!pVarData->ppchCustomMapTypes || eaFindString(&pVarData->ppchCustomMapTypes, pMapType->pchName) >= 0)
			{
				QueueLevelBand* pLevelBand = eaGet(&pDef->eaLevelBands, iLevelBandIndex);
				QueueGameSettingsData* pData = eaGetStruct(peaData, parse_QueueGameSettingsData, iCount++);
				pData->iMaxValue = pVarData->pSettingData->iMaxValue;
				pData->iMinValue = pVarData->pSettingData->iMinValue;
				pData->iRewardMinValue = pVarData->pSettingData->iRewardMinValue;

				StructCopyString(&pData->pchName, pVarData->pchName);

				pData->pchDisplayName = entTranslateDisplayMessage(pEnt,pVarData->pSettingData->msgDisplayName);
			
				// Try to get the setting on the instance
				if (!queue_GetVariableOverrideValue(eaSettings,
													pVarData,
													&pData->iValue))
				{
					// If no setting is found, then try to find an appropriate default
					bool bFoundDefault = false;
					if (pVarData->eType == kQueueVariableType_WorldVariable)
					{
						if (pLevelBand)
						{
							for (j = eaSize(&pLevelBand->VarData.eaWorldVars)-1; j >= 0; j--)
							{
								WorldVariable* pWorldVar = pLevelBand->VarData.eaWorldVars[j];
								if (eaFind(&pVarData->ppchVarNames, pWorldVar->pcName) >= 0 &&
									pWorldVar->eType == WVAR_INT)
								{
									pData->iValue = pWorldVar->iIntVal;
									bFoundDefault = true;
									break;
								}
							}
						}
						if (!bFoundDefault)
						{
							for (j = eaSize(&pDef->MapSettings.VarData.eaWorldVars)-1; j >= 0; j--)
							{
								WorldVariable* pWorldVar = pDef->MapSettings.VarData.eaWorldVars[j];
								if (eaFind(&pVarData->ppchVarNames, pWorldVar->pcName) >= 0 &&
									pWorldVar->eType == WVAR_INT)
								{
									pData->iValue = pWorldVar->iIntVal;
									bFoundDefault = true;
									break;
								}
							}
						}
					}
					if (!bFoundDefault)
					{
						pData->iValue = pVarData->pSettingData->iMinValue;
					}
				}
			}
		}
	}
	while (eaSize(peaData) > iCount)
		StructDestroy(parse_QueueGameSettingsData, eaPop(peaData));

	ui_GenSetManagedListSafe(pGen,peaData, QueueGameSettingsData,true);
}


///////////////////////////////////////////////////////////////////////////////////////////
// Expression Functions
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_SelectQueue");
void exprQueueSelectQueue(SA_PARAM_OP_VALID QueueDesc *pDesc)
{
	if (pDesc)
		StructCopyAll(parse_QueueDesc, pDesc, &s_SelectedQueue);
	else if (s_SelectedQueue.pDef)
		StructReset(parse_QueueDesc, &s_SelectedQueue);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetSelectedQueue");
SA_RET_NN_VALID QueueDesc *exprQueueGetSelectedQueue(void)
{
	return &s_SelectedQueue;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GenGetSelectedQueue");
SA_RET_NN_VALID QueueDesc *exprQueueGenGetSelectedQueue(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetPointer(pGen, &s_SelectedQueue, parse_QueueDesc);
	return &s_SelectedQueue;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetMinPlayerCount);
S32 exprQueue_GetMinPlayerCount(const char* pchQueueDefName)
{
	QueueDef* pDef = queue_DefFromName(pchQueueDefName);
	if (pDef)
	{
		return queue_QueueGetMinPlayers(pDef, false);
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetGameSettings);
void exprQueue_GetGameSettings(SA_PARAM_NN_VALID UIGen* pGen)
{
	S32 iLevelBandIndex = -1;
	QueueDef* pDef = NULL;
	QueueCustomMapData* pMapType = NULL;
	QueueGameSetting** eaSettings = NULL;
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);

	if (pQueueData && pQueueData->uPrivateInstance > 0)
	{
		PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
		const char* pchQueue = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueue, uInstanceID, true);
		
		if (pInstance)
		{
			pDef = queue_DefFromName(pchQueue);
			iLevelBandIndex = SAFE_MEMBER2(pInstance, pParams, iLevelBandIndex);
			pMapType = queue_GetCustomMapTypeFromMapName(pDef, SAFE_MEMBER2(pInstance, pParams, pchMapName));
			eaSettings = SAFE_MEMBER(pInstance, eaSettings);
		}
	}
	gclQueue_GetGameSettings(pGen, pEnt, pDef, pMapType, eaSettings, iLevelBandIndex);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetGameSettingsForNewInstance);
void exprQueue_GetGameSettingsForNewInstance(SA_PARAM_NN_VALID UIGen* pGen, 
											 const char* pchQueueName, 
											 const char* pchMapName, 
											 S32 iLevelBandIndex)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	QueueCustomMapData* pMapType = queue_GetCustomMapTypeFromMapName(pDef, pchMapName);

	gclQueue_GetGameSettings(pGen, pEnt, pDef, pMapType, (QueueGameSetting**)s_eaSettings, iLevelBandIndex);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_ChangeGameSettingForNewInstance);
void exprQueue_ChangeGameSettingForNewInstance(const char* pchVarName, S32 iValue)
{
	NOCONST(QueueGameSetting)* pSetting = NULL;
	S32 i;
	for (i = eaSize(&s_eaSettings)-1; i >= 0; i--)
	{
		pSetting = s_eaSettings[i];
		if (stricmp(pSetting->pchQueueVarName, pchVarName)==0)
		{
			break;
		}
	}
	if (i < 0)
	{
		pSetting = StructCreateNoConst(parse_QueueGameSetting);
		pSetting->pchQueueVarName = StructAllocString(pchVarName);
		eaPush(&s_eaSettings, pSetting);
	}
	pSetting->iValue = iValue;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_ClearGameSettingsForNewInstance);
void exprQueue_ClearGameSettingsForNewInstance(void)
{
	eaDestroyStructNoConst(&s_eaSettings, parse_QueueGameSetting);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_IsPlayerInMaxAllowedQueues);
bool exprQueue_IsPlayerInMaxAllowedQueues(void)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerQueueInfo)
	{
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		S32 i, j, iCount = 0;
		for (i = eaSize(&pQueueInfo->eaQueues)-1; i >= 0; i--)
		{
			PlayerQueue* pQueue = pQueueInfo->eaQueues[i];
			for (j = eaSize(&pQueue->eaInstances)-1; j >= 0; j--)
			{
				PlayerQueueInstance* pInstance = pQueue->eaInstances[j];
				if (pInstance->eQueueState != PlayerQueueState_None)
				{
					if ((g_QueueConfig.iMaxQueueCount != 0 && ++iCount >= g_QueueConfig.iMaxQueueCount) || 
						pInstance->eQueueState == PlayerQueueState_Invited)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_Exists);
bool exprQueue_Exists(const char* pchQueueName, S32 iLevelBandIndex, S32 iMapIndex, bool bPrivate)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerQueueInfo && pDef)
	{
		bool bFound = false;
		QueueInstanceParams* pParams = queue_CreateInstanceParams(pEnt, 
																  pDef, 
																  NULL, 
																  NULL, 
																  iLevelBandIndex, 
																  iMapIndex, 
																  bPrivate);
		if (pParams)
		{
			PlayerQueueInfo *pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		
			if (queue_FindPlayerQueueInstance(pQueueInfo, pchQueueName, pParams, true))
			{
				bFound = true;
			}
			StructDestroy(parse_QueueInstanceParams, pParams);
		}
		return bFound;
	}
	return false;
}

static bool gclQueue_CreateEx(const char* pchQueueName, 
							const char* pchPrivateName,
							const char* pchPassword,
							S32 iLevelBandIndex, 
							S32 iMapIndex,
							bool bPrivate)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueDef* pDef = queue_DefFromName(pchQueueName);

	if(g_QueueConfig.bDisablePrivateQueues && bPrivate)
	{
		if(pEnt)
		{
			notify_NotifySend(pEnt, kNotifyType_GameplayAnnounce, TranslateMessageKey("QueueServer_PrivateDisabled"), NULL, NULL);
		}
		return false;
	}
	
	if (pDef && pEnt)
	{
		QueueIntArray* pInviteList = NULL;
		QueueGameSettings* pSettings = NULL;
		QueueInstanceParams* pParams = queue_CreateInstanceParams(pEnt, 
																  pDef, 
																  pchPrivateName, 
																  pchPassword, 
																  iLevelBandIndex, 
																  iMapIndex, 
																  bPrivate);
		if (pParams)
		{
			if (s_pInviteMatch)
			{
				pInviteList = gclQueue_CreateInviteListFromMatch(s_pInviteMatch);
				StructDestroySafe(parse_QueueMatch, &s_pInviteMatch);
			}
			if (eaSize(&s_eaSettings))
			{
				pSettings = StructCreate(parse_QueueGameSettings);
				pSettings->eaSettings = (QueueGameSetting**)s_eaSettings;
				s_eaSettings = NULL;
			}
			ServerCmd_queue_create(pchQueueName, pParams, pInviteList, pSettings);
			StructDestroy(parse_QueueIntArray, pInviteList);
			StructDestroy(parse_QueueGameSettings, pSettings);
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_CreateWithPassword);
bool exprQueue_CreateWithPassword(const char* pchQueueName, 
								  const char* pchPrivateName,
								  const char* pchPassword,
								  S32 iLevelBandIndex, 
								  S32 iMapIndex)
{
	return gclQueue_CreateEx(pchQueueName, pchPrivateName, pchPassword, iLevelBandIndex, iMapIndex, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_Create);
bool exprQueue_Create(const char* pchQueueName, S32 iLevelBandIndex, S32 iMapIndex, bool bPrivate)
{
	return gclQueue_CreateEx(pchQueueName, NULL, NULL, iLevelBandIndex, iMapIndex, bPrivate);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_CreateBestPrivateQueue);
bool exprQueue_CreateBestPrivateQueue(const char* pchQueueName, 
									  const char* pchPrivateName, 
									  const char* pchPassword, 
									  S32 iLevelBandIndex, 
									  S32 iMapIndex)
{
	Entity* pEnt = entActivePlayerPtr();
	PlayerQueueInfo* pPlayerQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	const char* pchBestQueueName = NULL;
	S32 iBestLevelBandIndex = 0, iBestMapIndex = 0, iBestLevelBandDiff = -1;

	if (pDef && pDef->MapRules.bChallengeMatch)
	{
		pchBestQueueName = pDef->pchName;
		iBestLevelBandIndex = iLevelBandIndex;
		iBestMapIndex = iMapIndex;
	}
	else if (pDef && pPlayerQueueInfo)
	{
		const char* pchMapName = queue_GetMapNameByIndex(pDef, iMapIndex);
		QueueLevelBand* pLevelBand = eaGet(&pDef->eaLevelBands, iLevelBandIndex);
		S32 i, iQueueLevel = 0;
		int iPlayerLevel = entity_GetSavedExpLevel(pEnt);
		const char* pchAffiliation = queue_EntGetQueueAffiliation(pEnt);

		if (pLevelBand)
		{
			iQueueLevel = (pLevelBand->iMinLevel + pLevelBand->iMaxLevel) / 2;
		}
		for (i = eaSize(&pPlayerQueueInfo->eaQueues)-1; i >= 0; i--) 
		{
			QueueDef* pPlayerQueueDef = GET_REF(pPlayerQueueInfo->eaQueues[i]->hDef);
			QueueLevelBand* pCheckLevelBand;
			S32 iCheckLevelBandIndex, iCheckMapIndex, iCheckQueueLevel, iLevelDiff;

			if (!pPlayerQueueDef || !pPlayerQueueDef->MapRules.bChallengeMatch)
			{
				continue;
			}
			iCheckLevelBandIndex = iQueueLevel ? queue_GetLevelBandIndexForLevel(pPlayerQueueDef, iQueueLevel) : 0;
			if (iCheckLevelBandIndex < 0)
			{
				continue;
			}
			iCheckMapIndex = pchMapName ? queue_GetMapIndexByName(pPlayerQueueDef, pchMapName) : 0;
			if (iCheckMapIndex < 0)
			{
				continue;
			}
			if (queue_EntCannotUseQueue(NULL, iPlayerLevel, pchAffiliation, pPlayerQueueDef, iCheckLevelBandIndex, false, true))
			{
				continue;
			}

			pCheckLevelBand = pPlayerQueueDef->eaLevelBands[iCheckLevelBandIndex];
			iCheckQueueLevel = (pCheckLevelBand->iMinLevel + pCheckLevelBand->iMaxLevel) / 2;
			iLevelDiff = ABS(iQueueLevel - iCheckQueueLevel);

			if (iBestLevelBandDiff < 0 || iLevelDiff < iBestLevelBandDiff)
			{
				pchBestQueueName = pPlayerQueueDef->pchName;
				iBestLevelBandIndex = iCheckLevelBandIndex;
				iBestMapIndex = iCheckMapIndex;
				iBestLevelBandDiff = iLevelDiff;
			}
		}
	}

	if (!pchBestQueueName)
	{
		pchBestQueueName = pchQueueName;
		iBestLevelBandIndex = iLevelBandIndex;
		iBestMapIndex = iMapIndex;
	}
	if (pchBestQueueName)
	{
		return gclQueue_CreateEx(pchBestQueueName, pchPrivateName, pchPassword, iBestLevelBandIndex, iBestMapIndex, true);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_InPowerhouse);
bool exprQueue_PlayerInPowerhouse()
{
	const char *pcCurrentMap = zmapInfoGetPublicName(NULL);
	ZoneMapInfo* pCurrZoneMap = worldGetZoneMapByPublicName(pcCurrentMap);

	//You can't transfer from a zone map that requires confirmation
	if(pCurrZoneMap && zmapInfoConfirmPurchasesOnExit(pCurrZoneMap))
	{
		return true;
	}
	return false;
}

static U32 gclQueue_TimeRemaining(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_STR const char* queueName, U32 uiInstanceID)
{
	Player* pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerQueueInstance* pInstance = NULL;
	
	if (pPlayer)
	{
		if (uiInstanceID > 0)
		{
			pInstance = queue_FindPlayerQueueInstanceByID(pPlayer->pPlayerQueueInfo, queueName, uiInstanceID, true);
		}
		else if (pEnt->pPlayer->pPlayerQueueInfo)
		{
			PlayerQueue* pQueue = eaIndexedGetUsingString(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues, queueName);
			pInstance = (pQueue && !pQueue->eCannotUseReason) ? eaGet(&pQueue->eaInstances,0) : NULL;
		}
	}
	
	if (pInstance)
	{
		return pInstance->uSecondsRemaining;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_TimeRemaining);
U32 exprQueue_TimeRemaining(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_STR const char* queueName)
{
	return gclQueue_TimeRemaining(pEnt, queueName, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_TimeRemainingForInstance);
U32 exprQueue_TimeRemainingForInstance(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_STR const char* queueName, U32 uiID)
{
	return gclQueue_TimeRemaining(pEnt, queueName, uiID);
}

static bool gclQueue_QueueStateIsType(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_STR const char* queueName, U32 uiID, PlayerQueueState queueState) 	
{	
	PlayerQueueInstance* pInstance = NULL;
	Player* pPlayer = pEnt ? pEnt->pPlayer : NULL;
	if(pPlayer)
	{
		if(uiID)
		{
			pInstance = queue_FindPlayerQueueInstanceByID(pPlayer->pPlayerQueueInfo, queueName, uiID, true);
		}
		else if (pPlayer->pPlayerQueueInfo)
		{
			PlayerQueue* pQueue = eaIndexedGetUsingString(&pPlayer->pPlayerQueueInfo->eaQueues, queueName);
			QueueDef* pQueueDef = SAFE_GET_REF(pQueue, hDef);
			if (pQueueDef && pQueue->eCannotUseReason == QueueCannotUseReason_None)
			{
				S32 i, iLevelBandIndex = queue_GetLevelBandIndexForLevel(pQueueDef, entity_GetSavedExpLevel(pEnt));
				for (i = eaSize(&pQueue->eaInstances)-1; i >= 0; i--)
				{
					PlayerQueueInstance* pCheckInstance = pQueue->eaInstances[i];
					if (pCheckInstance->pParams && 
						pCheckInstance->pParams->iLevelBandIndex == iLevelBandIndex)
					{
						pInstance = pCheckInstance;
						break;
					}
				}
			}
		}
		if (pInstance)
		{
			return pInstance->eQueueState == queueState;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_StateIsType);
bool exprQueue_QueueStateIsType(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_STR const char* queueName, U32 queueState)
{
	return gclQueue_QueueStateIsType(pEnt, queueName, 0, queueState);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_InstanceStateIsType);
bool exprQueue_QueueInstanceStateIsType(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_STR const char* queueName, U32 uiID, U32 queueState)
{
	return gclQueue_QueueStateIsType(pEnt, queueName, uiID, queueState);
}

static const char* gclQueue_GetMapDisplayName(const char* pchMapName)
{
	ZoneMapInfo* pZoneInfo = worldGetZoneMapByPublicName(pchMapName);
	Message* pMsg = zmapInfoGetDisplayNameMessagePtr(pZoneInfo);
	return pMsg ? TranslateMessagePtr(pMsg) : pchMapName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_ReadyQueueCount);
int exprQueue_QueueReadyQueueCount(SA_PARAM_OP_VALID Entity *pEnt)
{
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);
	return SAFE_MEMBER(pQueueData, iReadyCount);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_ReadyQueueCountByMapType);
int exprQueue_QueueReadyQueueCountWithMapType(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_STR const char *pchMapType)
{
	S32 eQueueMapType = 0;
	QueueInfoUI* pQueueData = NULL;

	if(pchMapType && *pchMapType)
		eQueueMapType  = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);

	if(eQueueMapType == 0 || eQueueMapType == -1 )
		eQueueMapType = 0;

	gclQueue_UpdateQueueInfoFromMapTypes(pEnt, &pQueueData, eQueueMapType);
	
	return SAFE_MEMBER(pQueueData, iReadyCount);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetQueueCount);
int exprQueue_GetQueueCount(SA_PARAM_OP_VALID Entity *pEnt)
{
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	if (pQueueInfo)
	{
		return eaSize(&pQueueInfo->eaQueues);
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetQueueCountByMapType);
int exprQueue_GetQueueCountByMapType(SA_PARAM_OP_VALID Entity *pEnt, S32 eQueueMapType)
{
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	S32 i, iCount = 0;

	if (pQueueInfo)
	{
		for (i = eaSize(&pQueueInfo->eaQueues)-1; i >= 0; i--)
		{
			PlayerQueue* pQueue = pQueueInfo->eaQueues[i];
			QueueDef* pDef = GET_REF(pQueue->hDef);
			if (pDef && pDef->MapSettings.eMapType == eQueueMapType)
			{
				iCount++;
			}
		}
	}
	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_IsInQueue);
int exprQueue_IsInQueue(SA_PARAM_OP_VALID Entity *pEnt)
{
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	if (pQueueInfo)
	{
		FOR_EACH_IN_EARRAY(pQueueInfo->eaQueues, PlayerQueue, pQueue)
		{
			FOR_EACH_IN_EARRAY(pQueue->eaInstances, PlayerQueueInstance, pInstance)
			{
				if (queue_StateMeansInQueue(pInstance->eQueueState))
					return true;
			}
			FOR_EACH_END
			
		}
		FOR_EACH_END
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_IsInNamedQueue);
int exprQueue_IsInNamedQueue(SA_PARAM_OP_VALID Entity *pEnt, const char* pchQueueDefName)
{
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	if (pQueueInfo)
	{
		FOR_EACH_IN_EARRAY(pQueueInfo->eaQueues, PlayerQueue, pQueue)
		{
			// The name may come in from a different unpooled string, so find it first.
			const char* pchPoolName = allocFindString(pchQueueDefName);
			
			if (pQueue->pchQueueName==pchPoolName)
			{
				FOR_EACH_IN_EARRAY(pQueue->eaInstances, PlayerQueueInstance, pInstance)
				{
					if (queue_StateMeansInQueue(pInstance->eQueueState))
						return true;
				}
				FOR_EACH_END
			}
		}
		FOR_EACH_END
	}
	return false;
}

// If the queue is not on the player, this will return QueueCannotUseReason_None
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_ReasonCannotUseNamedQueueIfKnown);
int exprQueue_ReasonCannotUseNamedQueueIfKnown(SA_PARAM_OP_VALID Entity *pEnt, const char* pchQueueDefName)
{
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	if (pQueueInfo)
	{
		FOR_EACH_IN_EARRAY(pQueueInfo->eaQueues, PlayerQueue, pQueue)
		{
			// The name may come in from a different unpooled string, so find it first.
			const char* pchPoolName = allocFindString(pchQueueDefName);
			
			if (pQueue->pchQueueName==pchPoolName)
			{
				return(pQueue->eCannotUseReason);
			}
		}
		FOR_EACH_END
	}
	return (QueueCannotUseReason_None);
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_PlayerIsInPrivateInstance);
bool exprQueue_PlayerIsInPrivateInstance(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);

	if (pEnt && pEnt->pPlayer && pQueueData && pQueueData->uPrivateInstance > 0)
	{
		const char* pchQueueName = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, true);

		return pInstance != NULL;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetPrivateQueueName);
const char* exprQueue_GetPrivateQueueName(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);
	return NULL_TO_EMPTY(SAFE_MEMBER(pQueueData, pchPrivateQueue));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetPrivateInstanceID);
U32 exprQueue_GetPrivateInstanceID(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);
	return SAFE_MEMBER(pQueueData, uPrivateInstance);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetPrivateMapType);
U32 exprQueue_GetPrivateMapType(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);
	return SAFE_MEMBER(pQueueData, ePrivateMapType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_IsPrivate);
bool exprQueue_IsPrivate(const char* pchQueueName, U32 uiInstanceID)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pchQueueName && pchQueueName[0] && uiInstanceID > 0)
	{
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uiInstanceID, true);

		return pInstance && pInstance->pParams && pInstance->pParams->uiOwnerID > 0;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetInstanceMapName);
const char* exprQueue_GetInstanceMapName(const char* pchQueueName, U32 uiInstance)
{
	Entity* pEnt = entActivePlayerPtr();
	
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerQueueInfo)
	{
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo,pchQueueName,uiInstance, true);
		if (SAFE_MEMBER2(pInstance, pParams, pchMapName))
		{
			return gclQueue_GetMapDisplayName(pInstance->pParams->pchMapName);
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetMapDisplayNameByIndex);
const char* exprQueue_GetMapDisplayNameByIndex(const char* pchQueueName, S32 iMapIndex)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	if (pDef)
	{
		const char* pchMapName = queue_GetMapNameByIndex(pDef, iMapIndex);
		return gclQueue_GetMapDisplayName(pchMapName);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_IsCreator);
bool exprQueue_IsCreator(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);

	if (pEnt && pEnt->pPlayer && pQueueData && pQueueData->uPrivateInstance > 0)
	{
		const char* pchQueueName = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, true);

		return pInstance && pInstance->pParams && pInstance->pParams->uiOwnerID == entGetContainerID(pEnt);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_CanStartGame);
bool exprQueue_CanStartGame(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);
	if (pEnt && pEnt->pPlayer && pQueueData && pQueueData->uPrivateInstance > 0)
	{
		bool bValidMatch = false;
		const char* pchQueueName = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		QueueDef* pDef = queue_DefFromName(pchQueueName);
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, true);

		if (pInstance && (pInstance->bNewMapLoading || pInstance->eQueueState != PlayerQueueState_InQueue))
		{
			return false;
		}

		// Make a dummy match and validate it.
		if (pDef && pInstance)
		{
			S32 iMemberIdx;
			QueueMatch QMatch = {0};
			queue_InitMatchGroups(pDef, &QMatch);
			for (iMemberIdx = eaSize(&pInstance->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				PlayerQueueMember* pMember = pInstance->eaMembers[iMemberIdx];
				queue_Match_AddMember(&QMatch, pMember->iGroupIndex, pMember->uiID, pMember->uiTeamID);
			}
			bValidMatch = queue_Match_Validate(&QMatch, pDef, pInstance->pParams, pInstance->bOvertime, false, false);
			StructDeInit(parse_QueueMatch, &QMatch);
		}
		if (bValidMatch && !queue_PlayerInstance_AllMembersInState(pInstance, PlayerQueueState_InQueue))
		{
			bValidMatch = false;
		}
		return bValidMatch;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_CanLeaveInstance);
bool exprQueue_CanLeaveInstance(const char* pchQueueName, U32 uInstanceID)
{
	Entity* pEnt = entActivePlayerPtr();
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	if (pQueueInfo && uInstanceID > 0)
	{
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, false);
		
		if (pInstance)
		{
			return queue_CanLeaveQueue(pInstance->pParams, pInstance->eQueueState);
		}
	}
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_IsCreatingNewMap);
bool exprQueue_IsCreatingNewMap(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);
	if (pEnt && pEnt->pPlayer && pQueueData && pQueueData->uPrivateInstance > 0)
	{
		const char* pchQueueName = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, true);

		return SAFE_MEMBER(pInstance, bNewMapLoading);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetMemberCount);
S32 exprQueue_GetMemberCount(bool bOnlyActiveMembers)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);
	if (pEnt && pEnt->pPlayer && pQueueData && pQueueData->uPrivateInstance > 0)
	{
		S32 iCount = 0;
		const char* pchQueueName = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, true);

		if (pInstance)
		{
			if (!bOnlyActiveMembers)
			{
				iCount = eaSize(&pInstance->eaMembers);
			}
			else
			{
				S32 iMemberIdx;
				for (iMemberIdx = eaSize(&pInstance->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
				{
					PlayerQueueMember* pMember = pInstance->eaMembers[iMemberIdx];
					switch (pMember->eState)
					{
						xcase PlayerQueueState_None:
						acase PlayerQueueState_Invited:
						acase PlayerQueueState_Limbo:
						acase PlayerQueueState_Exiting:
						{
							continue;
						}
					}
					iCount++;
				}
			}
		}
		return iCount;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetPrivateChatChannelName);
const char* exprQueue_GetPrivateChatChannelName(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);

	if (pEnt && pEnt->pPlayer && pQueueData && pQueueData->uPrivateInstance > 0)
	{
		static char* estrChannelName = NULL;
		const char* pchQueueName = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, true);
		U32 uOrigOwnerID = SAFE_MEMBER(pInstance, uiOrigOwnerID);

		queue_GetPrivateChatChannelName(&estrChannelName, uOrigOwnerID, uInstanceID);
		return estrChannelName;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetFailedAllRequirementsReason);
S32 exprQueue_GetFailedAllRequirementsReason(S32 eMapType)
{
	Entity* pEnt = entActivePlayerPtr();
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	if (pQueueInfo)
	{
		S32 i;
		for (i = eaSize(&pQueueInfo->eaFailsAllReqs)-1; i >= 0; i--)
		{
			QueueFailRequirementsData* pData = pQueueInfo->eaFailsAllReqs[i];
			if (pData->eMapType == eMapType)
			{
				if (pData->eReason != QueueCannotUseReason_None)
				{
					return pData->eReason;
				}
				break;
			}
		}
	}
	return QueueCannotUseReason_None;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetWorldVariableString);
const char* exprQueue_GetMapVariableString(SA_PARAM_OP_VALID QueueDef *pDef, const char* pchVariable)
{
	if (pDef && pchVariable && pchVariable[0])
	{
		int i; 
		for (i = 0; i < eaSize(&pDef->MapSettings.VarData.eaWorldVars); i++)
		{
			WorldVariable *pWorldVar = pDef->MapSettings.VarData.eaWorldVars[i];
			if (pWorldVar->eType == WVAR_STRING 
				&& stricmp(pWorldVar->pcName, pchVariable) == 0)
			{
				return pWorldVar->pcStringVal;
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetWorldVariableMessage);
const char* exprQueue_GetMapVariableMessage(QueueDef *pDef, const char* pchVariable)
{
	if (pDef && pchVariable && pchVariable[0])
	{
		int i; 
		for (i = 0; i < eaSize(&pDef->MapSettings.VarData.eaWorldVars); i++)
		{
			WorldVariable *pWorldVar = pDef->MapSettings.VarData.eaWorldVars[i];
			if (pWorldVar->eType == WVAR_MESSAGE 
				&& stricmp(pWorldVar->pcName, pchVariable) == 0)
			{
				return TranslateDisplayMessage(pWorldVar->messageVal);
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetWorldVariableInt);
int exprQueue_GetMapVariableInt(SA_PARAM_OP_VALID QueueDef *pDef, const char* pchVariable)
{
	if (pDef && pchVariable && pchVariable[0])
	{
		int i; 
		for (i = 0; i < eaSize(&pDef->MapSettings.VarData.eaWorldVars); i++)
		{
			WorldVariable *pWorldVar = pDef->MapSettings.VarData.eaWorldVars[i];
			if (pWorldVar->eType == WVAR_INT
				&& stricmp(pWorldVar->pcName, pchVariable) == 0)
			{
				return pWorldVar->iIntVal;
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetWorldVariableFloat);
float exprQueue_GetMapVariableFloat(SA_PARAM_OP_VALID QueueDef *pDef, const char* pchVariable)
{
	if (pDef && pchVariable && pchVariable[0])
	{
		int i; 
		for (i = 0; i < eaSize(&pDef->MapSettings.VarData.eaWorldVars); i++)
		{
			WorldVariable *pWorldVar = pDef->MapSettings.VarData.eaWorldVars[i];
			if (pWorldVar->eType == WVAR_FLOAT
				&& stricmp(pWorldVar->pcName, pchVariable) == 0)
			{
				return pWorldVar->fFloatVal;
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetWorldVariableCritterDef);
SA_RET_OP_VALID CritterDef* exprQueue_GetWorldVariableCritterDef(SA_PARAM_OP_VALID QueueDef *pDef, const char* pchVariable)
{
	if (pDef && pchVariable && pchVariable[0])
	{
		int i; 
		for (i = 0; i < eaSize(&pDef->MapSettings.VarData.eaWorldVars); i++)
		{
			WorldVariable *pWorldVar = pDef->MapSettings.VarData.eaWorldVars[i];
			if (pWorldVar->eType == WVAR_CRITTER_DEF
				&& stricmp(pWorldVar->pcName, pchVariable) == 0)
			{
				return GET_REF(pWorldVar->hCritterDef);
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CritterDef_GetLogicalName);
const char* exprCritterDef_GetLogicalName(SA_PARAM_OP_VALID CritterDef* pDef)
{
	return SAFE_MEMBER(pDef, pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CritterDef_GetDisplayName);
const char* exprCritterDef_GetDisplayName(SA_PARAM_OP_VALID CritterDef* pDef)
{
	return pDef ? TranslateDisplayMessage(pDef->displayNameMsg) : "";
}

///////////////////////////////////////////////////////////////////////////////////////////
// Gen expressions
///////////////////////////////////////////////////////////////////////////////////////////

static S32 gclQueue_GetTotalPlayerCount(S32* piGroupPlayerCount)
{
	S32 i, iPlayerCount = 0;
	for (i = ea32Size(&piGroupPlayerCount)-1; i >= 0; i--)
	{
		iPlayerCount += piGroupPlayerCount[i];
	}
	return iPlayerCount;
}

static void gclQueue_FillDesc(SA_PARAM_NN_VALID Entity* pEnt,
							  SA_PARAM_NN_VALID QueueDesc* pDesc, 
							  SA_PARAM_NN_VALID QueueDef* pDef, 
							  SA_PARAM_NN_VALID PlayerQueue* pQueue, 
							  SA_PARAM_OP_VALID PlayerQueueInstance* pInstance, 
							  SA_PARAM_OP_VALID PlayerQueueMap* pMap,
							  ControlSchemeRegionType eRegionFlags)
{	
	S32 i;
	QueueGroupDef* pGroup0Def;
	QueueGroupDef* pGroup1Def;
	QueueCategoryData* pCategory = queue_GetCategoryData(pDef->eCategory);
	QueueCooldownDef* pCooldownDef = queue_CooldownDefFromName(pDef->pchCooldownDef);
	COPY_HANDLE(pDesc->descriptionMesg.hMessage, pDef->descriptionMesg.hMessage);
	COPY_HANDLE(pDesc->displayNameMesg.hMessage, pDef->displayNameMesg.hMessage);
	pDesc->pDef = pDef;
	pDesc->pchQueueName = allocAddString(pQueue->pchQueueName);
	pDesc->pchIcon = allocAddString(pDef->pchIcon);
	pDesc->pchRequiredMission = allocAddString(REF_STRING_FROM_HANDLE(pDef->Requirements.hMissionRequired));
	pDesc->pchRequiredActivity = allocAddString(pDef->Requirements.pchRequiredActivity);
	pDesc->eCategory = pDef->eCategory;
	pDesc->eReward = pDef->eReward;
	pDesc->eDifficulty = pDef->eDifficulty;
	pDesc->iExpectedGameTime = pDef->iExpectedGameTime;
	pDesc->iPlayerCountMax = queue_QueueGetMaxPlayers(pDef, SAFE_MEMBER(pInstance, bOvertime));
	pDesc->iPlayerCountMin = queue_QueueGetMinPlayersEx(pDef, SAFE_MEMBER(pInstance, bOvertime), false, queue_PlayerInstanceIsPrivate(pInstance));
	pDesc->bAllGroupsSameAffiliation = true;
	pDesc->bOvertime = false;
	pDesc->bHasPassword = SAFE_MEMBER(pInstance, bHasPassword);
	pDesc->eRegionFlags = eRegionFlags;
	pDesc->eCannotUseReason = (QueueCannotUseReason)pQueue->eCannotUseReason;

	estrClear(&pDesc->pchFormattedDisplayName);
	estrClear(&pDesc->pchFormattedDescription);
	FormatGameDisplayMessage(&pDesc->pchFormattedDisplayName, &pDesc->displayNameMesg, STRFMT_PLAYER(pEnt));
	FormatGameDisplayMessage(&pDesc->pchFormattedDescription, &pDesc->descriptionMesg, STRFMT_PLAYER(pEnt));

	if (pInstance && queue_StateMeansInQueue(pInstance->eQueueState))
		pDesc->bInQueue = true;
	else if (!pQueue)
		pDesc->bInQueue = false;
	else
	{
		pDesc->bInQueue = false;

		if (!pInstance)
		{
			for (i = eaSize(&pQueue->eaInstances) - 1; i >= 0; i--)
			{
				if (queue_StateMeansInQueue(pQueue->eaInstances[i]->eQueueState))
				{
					pDesc->bInQueue = true;
					break;
				}
			}
		}
	}

	pDesc->iSimpleStatus = pDesc->bInQueue ? 1 : pDesc->eCannotUseReason ? -1 : 0;

	// Set cooldown data
	if (pCooldownDef)
	{
		pDesc->uCooldownTime = pCooldownDef->uCooldownTime;
		COPY_HANDLE(pDesc->cooldownDisplayNameMesg.hMessage, pCooldownDef->msgDisplayName.hMessage);
		COPY_HANDLE(pDesc->cooldownDescriptionMesg.hMessage, pCooldownDef->msgDescription.hMessage);
	}
	else
	{
		pDesc->uCooldownTime = 0;
		REMOVE_HANDLE(pDesc->cooldownDisplayNameMesg.hMessage);
		REMOVE_HANDLE(pDesc->cooldownDescriptionMesg.hMessage);
	}
	// Set category data
	if (pCategory)
	{
		pDesc->pchCategoryIcon = allocAddString(pCategory->pchIconName);
		StructCopyString(&pDesc->pchCategoryDisplayName, TranslateDisplayMessage(pCategory->msgDisplayName));
	}
	else
	{
		pDesc->pchCategoryIcon = NULL;
		StructFreeStringSafe(&pDesc->pchCategoryDisplayName);
	}

	if (pGroup0Def = eaGet(&pDef->eaGroupDefs,0))
	{
		SET_HANDLE_FROM_STRING("Allegiance", pGroup0Def->pchAffiliation, pDesc->hGroup1Allegiance);
	}
	else
	{
		REMOVE_HANDLE(pDesc->hGroup1Allegiance);
	}
	if (pGroup1Def = eaGet(&pDef->eaGroupDefs,1))
	{
		SET_HANDLE_FROM_STRING("Allegiance", pGroup1Def->pchAffiliation, pDesc->hGroup2Allegiance);
	}
	else
	{
		REMOVE_HANDLE(pDesc->hGroup2Allegiance);
	}

	if (pInstance && pInstance->pParams)
	{
		QueueLevelBand* pLevelBand = eaGet(&pDef->eaLevelBands,pInstance->pParams->iLevelBandIndex);
		pDesc->uiInstanceID = pInstance->uiID;
		pDesc->uiOwnerID = pInstance->pParams ? pInstance->pParams->uiOwnerID : 0;
		pDesc->eQueueState = pInstance->eQueueState;
		pDesc->pParams = pInstance->pParams;
		pDesc->bBolster = queue_GetBolsterType(pDef, pLevelBand) != kBolsterType_None;
		pDesc->bOvertime = pInstance->bOvertime;
		pDesc->uAvgWaitTime = pInstance->uAverageWaitTime;
		if (!pDesc->pchPrivateName && pInstance->pParams->pchPrivateName && stricmp(pDesc->pchPrivateName, pInstance->pParams->pchPrivateName))
			StructCopyString(&pDesc->pchPrivateName, pInstance->pParams->pchPrivateName);

		for (i = eaSize(&pDef->eaGroupDefs)-2; i >= 0; i--)
		{
			QueueGroupDef* pGroupDef = pDef->eaGroupDefs[i];
			QueueGroupDef* pNextGroupDef = pDef->eaGroupDefs[i+1];
			if (stricmp(pGroupDef->pchAffiliation,pNextGroupDef->pchAffiliation)!=0)
			{
				pDesc->bAllGroupsSameAffiliation = false;
				break;
			}
		}

		if (pMap)
		{
			U32 uCurrentServerTime = timeServerSecondsSince2000();
			ea32Copy(&pDesc->piGroupPlayerCount,&pMap->piGroupPlayerCounts);
			pDesc->iTotalPlayerCount = gclQueue_GetTotalPlayerCount(pDesc->piGroupPlayerCount);
			pDesc->iMapKey = pMap->iKey;

			if (pMap->uMapLaunchTime && uCurrentServerTime > pMap->uMapLaunchTime)
			{
				pDesc->uElapsedMapTime = uCurrentServerTime - pMap->uMapLaunchTime;
			}
			else
			{
				pDesc->uElapsedMapTime = 0;
			}
		}
		else
		{
			ea32Copy(&pDesc->piGroupPlayerCount,&pInstance->piGroupPlayerCounts);
			pDesc->iTotalPlayerCount = gclQueue_GetTotalPlayerCount(pDesc->piGroupPlayerCount);
			pDesc->iMapKey = 0;
			pDesc->uElapsedMapTime = 0;
		}
	}
	else
	{
		pDesc->uiInstanceID = 0;
		pDesc->iMapKey = 0;
		pDesc->uAvgWaitTime = 0;
		pDesc->uElapsedMapTime = 0;
		pDesc->uiOwnerID = 0;
		pDesc->iTotalPlayerCount = 0;
		pDesc->eQueueState = PlayerQueueState_None;
		pDesc->pParams = NULL;
		pDesc->bBolster = false;
		pDesc->bOvertime = false;
		StructFreeStringSafe(&pDesc->pchPrivateName);
		ea32Clear(&pDesc->piGroupPlayerCount);
	}

	{
		const char *pchFormat = TranslateMessageKeyDefault("QueueUI_Formatted_Name_Format", "{PrivateName ? {PrivateName} {DisplayName?({DisplayName})} | {DisplayName} }");
		estrClear(&pDesc->pchFormattedName);
		FormatGameString(&pDesc->pchFormattedName, pchFormat, STRFMT_STRING("PrivateName", NULL_TO_EMPTY(pDesc->pchPrivateName)), STRFMT_STRING("DisplayName", NULL_TO_EMPTY(pDesc->pchFormattedDisplayName)));
	}
}

static ControlSchemeRegionType gclPlayerQueueMapMatchesRegionFlags(const char* pchMapName)
{
	ZoneMapInfo* pZoneInfo = worldGetZoneMapByPublicName(pchMapName);
	RegionRules* pRules = getRegionRulesFromZoneMap(pZoneInfo);

	if (pRules)
	{
		return pRules->eSchemeRegionType;
	}
	return 0;
}

static ControlSchemeRegionType gclPlayerQueueGetRegionFlags(QueueDef* pDef, 
															PlayerQueueInstance* pInstance)
{
	ControlSchemeRegionType eRegionFlags = 0;
	if (pDef)
	{
		if (SAFE_MEMBER2(pInstance, pParams, pchMapName))
		{
			const char* pchMapName = pInstance->pParams->pchMapName;
			eRegionFlags |= gclPlayerQueueMapMatchesRegionFlags(pchMapName);
		}
		else
		{
			S32 i, iMapCount = queue_GetMapIndexByName(pDef, NULL);
			for (i = iMapCount-1; i >= 0; i--)
			{
				const char* pchMapName = queue_GetMapNameByIndex(pDef, i);
				eRegionFlags |= gclPlayerQueueMapMatchesRegionFlags(pchMapName);
			}
		}
	}
	return eRegionFlags;
}

static void gclQueue_GetInstanceList(SA_PARAM_NN_VALID UIGen *pGen,
									 SA_PARAM_OP_VALID Entity *pEntity,
									 ACMD_EXPR_ENUM(ZoneMapType) const char* pchMapType,
									 const char** ppchFilterByName,
									 const char** ppchFilterByQueueName,
									 const char** ppchFilterByQueueDesc,
									 U32 eSchemeRegionTypeFlags,
									 U32 eOptions,
									 S32 *piFilterByReward,
									 S32 *piFilterByDifficulty,
									 S32 *piFilterByFullPlayerCount
									 )
{
	QueueDesc ***peaQueues = ui_GenGetManagedListSafe(pGen, QueueDesc);
	PlayerQueueInfo *pPlayerQueueInfo = (pEntity && pEntity->pPlayer) ? pEntity->pPlayer->pPlayerQueueInfo : NULL;
	ZoneMapType eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);
	int iCount = 0, iFilter, iNumPrivateFilter = eaSize(&ppchFilterByName);

	if(pPlayerQueueInfo && eType >= ZMTYPE_UNSPECIFIED && eSchemeRegionTypeFlags != kControlSchemeRegionType_None
		// At least one of PublicGames or PrivateGames should be included
		&& (eOptions & (kGetInstanceListFlag_PublicGames | kGetInstanceListFlag_PrivateGames))
		// At least one of OwnedInstances, UnownedInstances, Instances, Templates, or SimpleTemplates should be included
		&& (eOptions & (kGetInstanceListFlag_Maps | kGetInstanceListFlag_Instances | kGetInstanceListFlag_Templates))
		// At least one of Password or NoPassword should be included
		&& (eOptions & (kGetInstanceListFlag_Password | kGetInstanceListFlag_NoPassword))
		)
	{
		int i, n = eaSize(&pPlayerQueueInfo->eaQueues);
		PlayerQueueInstance* pReadyInstance = NULL;

		if (!(eOptions & kGetInstanceListFlag_BestReadyQueue))
			pReadyInstance = Queue_GetReadyQueueOfType(pEntity, eType, NULL);

		for (i = 0; i < n; i++)
		{
			QueueDesc* pDesc;
			PlayerQueue* pQueue = pPlayerQueueInfo->eaQueues[i];
			QueueDef* pDef = GET_REF(pQueue->hDef);
			ControlSchemeRegionType eRegionFlags;
			int j, s = eaSize(&pQueue->eaInstances);

			if (!pDef || (eType != ZMTYPE_UNSPECIFIED && eType != pDef->MapSettings.eMapType))
				continue;

			if (eaSize(&ppchFilterByQueueName))
			{
				const char *pchQueueName = TranslateDisplayMessage(pDef->displayNameMesg);
				if (!pchQueueName || !*pchQueueName)
					continue;
				for (iFilter = eaSize(&ppchFilterByQueueName) - 1; iFilter >= 0; iFilter--)
				{
					if (!strstri(pchQueueName, ppchFilterByQueueName[iFilter]))
						break;
				}
				if (iFilter >= 0)
					continue;
			}

			if (eaSize(&ppchFilterByQueueDesc))
			{
				const char *pchQueueDesc = TranslateDisplayMessage(pDef->descriptionMesg);
				if (!pchQueueDesc || !*pchQueueDesc)
					continue;
				for (iFilter = eaSize(&ppchFilterByQueueDesc) - 1; iFilter >= 0; iFilter--)
				{
					if (!strstri(pchQueueDesc, ppchFilterByQueueDesc[iFilter]))
						break;
				}
				if (iFilter >= 0)
					continue;
			}

			if (eaiSize(&piFilterByReward))
			{
				if (eaiFind(&piFilterByReward, pDef->eReward) < 0)
					continue;
			}

			if (eaiSize(&piFilterByDifficulty))
			{
				if (eaiFind(&piFilterByDifficulty, pDef->eDifficulty) < 0)
					continue;
			}

			if (eaiSize(&piFilterByFullPlayerCount))
			{
				if (eaiFind(&piFilterByFullPlayerCount, queue_QueueGetMaxPlayers(pDef, false)) < 0)
					continue;
			}

			if (eOptions & kGetInstanceListFlag_RequireSameGuild)
			{
				if (!pDef->Requirements.bRequireSameGuild)
					continue;
			}

			if (eOptions & kGetInstanceListFlag_Templates)
			{
				PlayerQueueInstance *pIncludeInstance = NULL;

				if (eOptions & kGetInstanceListFlag_ExcludeQueued)
				{	
					// exclude any instance that we are queued for
					bool bInQueue = false;
					FOR_EACH_IN_EARRAY(pQueue->eaInstances, PlayerQueueInstance, pInstance)
					{
						if (pInstance->eQueueState != PlayerQueueState_None)
						{
							bInQueue = true;
							break;
						}
					}
					FOR_EACH_END
 
					if (bInQueue)
						continue;
				}

				eRegionFlags = gclPlayerQueueGetRegionFlags(pDef, NULL);
				pDesc = eaGetStruct(peaQueues, parse_QueueDesc, iCount++);
				
				if (eOptions & kGetInstanceListFlag_TemplatesIncludeInstance)
				{
					// include instance info for the first instance we find
					FOR_EACH_IN_EARRAY(pQueue->eaInstances, PlayerQueueInstance, pInstance)
					{
						if (pInstance->eQueueState != PlayerQueueState_None)
						{
							pIncludeInstance = pInstance;
							break;
						}
					}
					FOR_EACH_END

					if (!pIncludeInstance)
					{
						pIncludeInstance = eaGet(&pQueue->eaInstances, 0);
					}
				}
				
				gclQueue_FillDesc(pEntity, pDesc, pDef, pQueue, pIncludeInstance, NULL, eRegionFlags);
			}
			
			if (eOptions & kGetInstanceListFlag_ExcludeQueued)
				continue;


			for (j = 0; j < s; j++)
			{
				PlayerQueueInstance* pInstance = pQueue->eaInstances[j];
				bool bInclude = false;
				if (pReadyInstance == pInstance)
					continue;

				if (pInstance->bHasPassword)
				{
					if (!(kGetInstanceListFlag_Password & eOptions))
						continue;
				}
				else
				{
					if (!(kGetInstanceListFlag_NoPassword & eOptions))
						continue;
				}

				eRegionFlags = gclPlayerQueueGetRegionFlags(pDef, pInstance);
				if (!(eRegionFlags & eSchemeRegionTypeFlags))
					continue;

				if (SAFE_MEMBER(pInstance->pParams, uiOwnerID) > 0)
				{
					if (!(kGetInstanceListFlag_PrivateGames & eOptions))
						continue;
					if (iNumPrivateFilter > 0)
					{
						const char *pchPrivateName = pInstance->pParams->pchPrivateName;
						for (iFilter = iNumPrivateFilter - 1; iFilter >= 0; iFilter--)
						{
							if (!strstri(pchPrivateName, ppchFilterByName[iFilter]))
								break;
						}
						if (iFilter >= 0)
							continue;
					}
				}
				else
				{
					if (!(kGetInstanceListFlag_PublicGames & eOptions))
						continue;
				}

				if (SAFE_MEMBER(pInstance->pParams, uiOwnerID) > 0)
				{
					if ((eOptions & kGetInstanceListFlag_OwnedInstances))
						bInclude = true;
				}
				else
				{
					if ((eOptions & kGetInstanceListFlag_UnownedInstances))
						bInclude = true;
				}

				if (eOptions & kGetInstanceListFlag_Maps)
				{
					int k, m = eaSize(&pInstance->eaPlayerQueueMaps);
					for (k = 0; k < m; k++)
					{
						pDesc = eaGetStruct(peaQueues, parse_QueueDesc, iCount++);
						gclQueue_FillDesc(pEntity, pDesc, pDef, pQueue, pInstance, pInstance->eaPlayerQueueMaps[k], eRegionFlags);
						bInclude = false;	// don't list it twice
					}
				}

				if (bInclude)
				{
					pDesc = eaGetStruct(peaQueues, parse_QueueDesc, iCount++);
					gclQueue_FillDesc(pEntity, pDesc, pDef, pQueue, pInstance, NULL, eRegionFlags);
				}
			}
		}
	}

	eaSetSizeStruct(peaQueues, parse_QueueDesc, iCount);
	ui_GenSetManagedListSafe(pGen, peaQueues, QueueDesc, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueList");
void exprGetQueueList(SA_PARAM_NN_VALID UIGen *pGen, 
					  SA_PARAM_OP_VALID Entity *pEntity, 
					  ACMD_EXPR_ENUM(ZoneMapType) const char* pchMapType,
					  U32 eSchemeRegionTypeFlags)
{
	// This probably would actually be more worthwhile with Templates, but
	// this is maintained for backwards compatibility.
	gclQueue_GetInstanceList(pGen,
							 pEntity,
							 pchMapType,
							 NULL,
							 NULL,
							 NULL,
							 eSchemeRegionTypeFlags,
							 kGetInstanceListFlag_Templates | kGetInstanceListFlag_PublicGames
							 | kGetInstanceListFlag_PrivateGames | kGetInstanceListFlag_BestReadyQueue
							 | kGetInstanceListFlag_Password | kGetInstanceListFlag_NoPassword,
							 NULL, NULL, NULL
							);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueInstanceList");
void exprGetQueueInstanceList(SA_PARAM_NN_VALID UIGen *pGen,
							  SA_PARAM_OP_VALID Entity *pEntity,
							  ACMD_EXPR_ENUM(ZoneMapType) const char* pchMapType,
							  U32 eSchemeRegionTypeFlags,
							  bool bShowIndividualMaps,
							  bool bHideBestReadyQueue)
{
	gclQueue_GetInstanceList(pGen,
							 pEntity,
							 pchMapType,
							 NULL,
							 NULL,
							 NULL,
							 eSchemeRegionTypeFlags,
							   (bShowIndividualMaps ? kGetInstanceListFlag_Maps : kGetInstanceListFlag_Instances)
							 | (bHideBestReadyQueue ? 0 : kGetInstanceListFlag_BestReadyQueue)
							 | kGetInstanceListFlag_PublicGames
							 | kGetInstanceListFlag_Password | kGetInstanceListFlag_NoPassword,
							 NULL, NULL, NULL
							);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPrivateQueueInstanceList");
void exprGetPrivateQueueInstanceList(SA_PARAM_NN_VALID UIGen *pGen,
									 SA_PARAM_OP_VALID Entity *pEntity,
									 ACMD_EXPR_ENUM(ZoneMapType) const char* pchMapType,
									 const char* pchFilterByName,
									 U32 eSchemeRegionTypeFlags,
									 bool bShowIndividualMaps,
									 bool bHideBestReadyQueue)
{
	static const char **s_ppchFilterByName = NULL;

	if (pchFilterByName && *pchFilterByName)
	{
		if (eaSize(&s_ppchFilterByName) != 1)
			eaSetSize(&s_ppchFilterByName, 1);
		s_ppchFilterByName[0] = pchFilterByName;
	}
	else
	{
		eaClearFast(&s_ppchFilterByName);
	}

	gclQueue_GetInstanceList(pGen,
							 pEntity,
							 pchMapType,
							 s_ppchFilterByName,
							 NULL,
							 NULL,
							 eSchemeRegionTypeFlags,
							   (bShowIndividualMaps ? kGetInstanceListFlag_Maps : kGetInstanceListFlag_Instances)
							 | (bHideBestReadyQueue ? 0 : kGetInstanceListFlag_BestReadyQueue)
							 | kGetInstanceListFlag_PrivateGames
							 | kGetInstanceListFlag_Password | kGetInstanceListFlag_NoPassword,
							 NULL, NULL, NULL
							);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetQueueInstances");
void exprGenGetQueueInstances(SA_PARAM_NN_VALID UIGen *pGen,
							  ACMD_EXPR_ENUM(ZoneMapType) const char* pchMapType,
							  const char* pchFilter,
							  /*ControlSchemeRegionType*/ U32 eSchemeRegionTypeFlags,
							  /*QueueGetInstanceListFlag*/ U32 eOptions)
{
	static const char **s_ppchFilterByName = NULL;
	static const char **s_ppchFilterByQueueName = NULL;
	static const char **s_ppchFilterByQueueDesc = NULL;
	static S32 *s_piFilterByReward = NULL;
	static S32 *s_piFilterByDifficulty = NULL;
	static S32 *s_piFilterByFullPlayerCount = NULL;

	char *pchBuffer = NULL;
	strdup_alloca(pchBuffer, pchFilter);

	eaClearFast(&s_ppchFilterByName);
	eaClearFast(&s_ppchFilterByQueueName);
	eaClearFast(&s_ppchFilterByQueueDesc);
	eaiClearFast(&s_piFilterByReward);
	eaiClearFast(&s_piFilterByDifficulty);
	eaiClearFast(&s_piFilterByFullPlayerCount);

	while (*pchBuffer)
	{
		char *pchStart;

		while (*pchBuffer && isspace((unsigned char)*pchBuffer))
			pchBuffer++;

		pchStart = pchBuffer;
		while (*pchBuffer && !isspace((unsigned char)*pchBuffer) && *pchBuffer != ':')
			pchBuffer++;

		if (*pchBuffer == ':')
		{
			const char ***peaFilter = NULL;
			StaticDefineInt *pDefine = NULL;
			S32 **peaiFilter = NULL;
			*pchBuffer++ = '\0';

			// check tag
			if (!stricmp(pchStart, "name"))
				peaFilter = &s_ppchFilterByName;
			else if (!stricmp(pchStart, "queue"))
				peaFilter = &s_ppchFilterByQueueName;
			else if (!stricmp(pchStart, "desc"))
				peaFilter = &s_ppchFilterByQueueDesc;
			else if (!stricmp(pchStart, "reward"))
				peaiFilter = &s_piFilterByReward, pDefine = QueueRewardEnum;
			else if (!stricmp(pchStart, "difficulty"))
				peaiFilter = &s_piFilterByDifficulty, pDefine = QueueDifficultyEnum;
			else if (!stricmp(pchStart, "maxplayers"))
				peaiFilter = &s_piFilterByFullPlayerCount;

			if (*pchBuffer == '\"')
			{
				pchStart = ++pchBuffer;
				while (*pchBuffer && *pchBuffer != '\"')
					pchBuffer++;
				if (*pchBuffer == '\"')
					*pchBuffer++ = '\0';
			}
			else
			{
				pchStart = pchBuffer;
				while (*pchBuffer && !isspace((unsigned char)*pchBuffer))
					pchBuffer++;
				if (*pchBuffer)
					*pchBuffer++ = '\0';
			}

			if (peaFilter && *pchStart)
			{
				// Add search token
				eaPush(peaFilter, pchStart);
			}
			else if (peaiFilter && *pchStart)
			{
				S32 iValue = pDefine ? StaticDefineIntGetInt(pDefine, pchStart) : -1;
				if (iValue == -1)
					iValue = atoi(pchStart);
				eaiPush(peaiFilter, iValue);
			}
		}
	}

	gclQueue_GetInstanceList(pGen,
							 entActivePlayerPtr(),
							 pchMapType,
							 s_ppchFilterByName,
							 s_ppchFilterByQueueName,
							 s_ppchFilterByQueueDesc,
							 eSchemeRegionTypeFlags,
							 eOptions,
							 s_piFilterByReward, s_piFilterByDifficulty, s_piFilterByFullPlayerCount
							 );
}


static int SortQueueMembersByGroup(const QueueMemberDesc **ppMemberA, const QueueMemberDesc **ppMemberB)
{
	const QueueMemberDesc* pA = (*ppMemberA);
	const QueueMemberDesc* pB = (*ppMemberB);

	if (pA->iGroupID != pB->iGroupID)
	{
		return pA->iGroupID - pB->iGroupID;
	}
	if (pA->uiTeamID != pB->uiTeamID)
	{
		return pA->uiTeamID - pB->uiTeamID;
	}
	return stricmp(pA->pchName,pB->pchName);
}

static void gclQueueMemberList_FillGroup(SA_PARAM_NN_VALID Entity* pEntity, 
										 QueueMemberDesc ***peaMembers, 
										 SA_PARAM_NN_VALID QueueDef* pDef, 
										 S32 iCurGroup, 
										 bool bShowGroupHeaders,
										 bool bShowEmptySlots,
										 S32* piIndex)
{
	QueueGroupDef* pGroupDef = eaGet(&pDef->eaGroupDefs, iCurGroup);
	S32 iCurGroupSize = 0;

	if (pGroupDef)
	{
		AllegianceDef* pAllegianceDef = RefSystem_ReferentFromString("Allegiance", pGroupDef->pchAffiliation);
		if (bShowGroupHeaders)
		{
			QueueMemberDesc* pHeader = StructCreate(parse_QueueMemberDesc);
			pHeader->pchName = entTranslateDisplayMessage(pEntity, pGroupDef->DisplayName);
			pHeader->pchAllegiance = pAllegianceDef ? entTranslateDisplayMessage(pEntity, pAllegianceDef->displayNameMsg) : NULL;
			pHeader->bIsHeader = true;
			pHeader->iGroupID = iCurGroup;
			eaInsert(peaMembers, pHeader, (*piIndex));
			(*piIndex)++;
		}

		while ((*piIndex) < eaSize(peaMembers) && (*peaMembers)[*piIndex]->iGroupID == iCurGroup)
		{
			(*piIndex)++;
			iCurGroupSize++;
		}

		if (bShowEmptySlots)
		{
			while (iCurGroupSize < pGroupDef->iMax)
			{
				QueueMemberDesc* pEmpty = StructCreate(parse_QueueMemberDesc);
				pEmpty->bIsHeader = false;
				pEmpty->iGroupID = iCurGroup;
				eaInsert(peaMembers, pEmpty, (*piIndex));
				(*piIndex)++;
				iCurGroupSize++;
			}
		}
	}
}

static void gclQueueMemberList_GroupMembers(Entity* pEntity, 
											QueueMemberDesc ***peaMembers, 
											const char* pchQueueName, 
											S32 iGroupIndex,
											bool bShowGroupHeaders,
											bool bShowEmptySlots)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	S32 i, iIndex = 0;

	if (pDef==NULL || pEntity==NULL)
		return;

	for (i = 0; i < eaSize(&pDef->eaGroupDefs); i++)
	{
		if (iGroupIndex < 0 || i == iGroupIndex)
		{
			gclQueueMemberList_FillGroup(pEntity, peaMembers, pDef, i, bShowGroupHeaders, bShowEmptySlots, &iIndex);
		}
	}
}

static void gclGetQueueMemberListFillTeamInfo(QueueMemberDesc ***peaMembers)
{
	S32 i, iSize = eaSize(peaMembers);
	U32 uCurrentTeamID = 0;
	for (i = 0; i <= iSize; i++)
	{
		QueueMemberDesc* pMember = eaGet(peaMembers, i);
		if (uCurrentTeamID != SAFE_MEMBER(pMember, uiTeamID))
		{
			if (pMember) 
			{
				pMember->bFirstTeamMember = true;
			}
			if (i > 0)
			{
				(*peaMembers)[i-1]->bLastTeamMember = true;
			}
		}
		uCurrentTeamID = SAFE_MEMBER(pMember, uiTeamID);
	}
}

static S32 gclGetQueueInstanceMemberList(SA_PARAM_NN_VALID UIGen *pGen, 
										 SA_PARAM_OP_VALID Entity *pEntity, 
										 const char* pchQueueName, 
										 U32 uiInstanceID, 
										 S32 iGroupIndex,
										 bool bShowGroupHeaders,
										 bool bShowEmptySlots)
{
	QueueMemberDesc ***peaMembers = ui_GenGetManagedListSafe(pGen, QueueMemberDesc);
	PlayerQueueInfo* pQueueInfo = pEntity && pEntity->pPlayer ? pEntity->pPlayer->pPlayerQueueInfo : NULL;
	PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uiInstanceID, true);
	int c = 0;

	if (pEntity && pInstance)
	{
		PlayerQueueMember **peaQueueMembers = NULL;
		int i, iMemberCount = eaSize(&pInstance->eaMembers);
		int iCount = 0;
		S64 iMapKey = 0;
		bool bFound = false;

		// There was a sort of pInstance->eaMembers here. This is very bad as this is a struct inside of an entity and will cause the client and game server to be out of sync

		for (i = 0; i < iMemberCount; i++)
		{
			PlayerQueueMember* pMember = pInstance->eaMembers[i];
			bool bSelf = pMember->uiID == entGetContainerID(pEntity);

			if (bFound && iMapKey != pMember->iMapKey)
			{
				break;
			}

			if (bSelf)
			{
				bFound = true;
			}

			if (iMapKey != pMember->iMapKey)
			{
				iMapKey = pMember->iMapKey;
				eaClear(&peaQueueMembers);
				iCount = 0;
			}

			eaPush(&peaQueueMembers, pMember);
			iCount++;
		}

		for (i = 0; i < iCount; i++)
		{
			PlayerQueueMember* pMember = peaQueueMembers[i];
			bool bSelf = pMember->uiID == entGetContainerID(pEntity);
			Entity* pMemberEnt = bSelf ? pEntity : GET_REF(pMember->hEntity);

			if ((iGroupIndex < 0 || pMember->iGroupIndex == iGroupIndex))
			{
				QueueMemberDesc* pDesc = eaGetStruct(peaMembers, parse_QueueMemberDesc, c++);

				pDesc->uiEntID = pMember->uiID;
				pDesc->uiTeamID = pMember->uiTeamID;
				pDesc->iLevel = pMember->iLevel;
				pDesc->iRank = pMember->iRank;

				if (pMemberEnt && pMemberEnt->pChar)
				{
					pDesc->pchName = entGetLocalName(pMemberEnt);
					pDesc->pchAllegiance = queue_EntGetQueueAffiliation(pMemberEnt);
					COPY_HANDLE(pDesc->hClass, pMemberEnt->pChar->hClass);

					if (bSelf)
					{
						pDesc->iLevel = LevelFromLevelingNumeric(item_GetLevelingNumeric(pMemberEnt));
						pDesc->iRank = Officer_GetRank(pMemberEnt);
					}
				}
				
				pDesc->iGroupID = pMember->iGroupIndex;
				pDesc->eState = pMember->eState;
				pDesc->bIsHeader = false;
				pDesc->bFirstTeamMember = false;
				pDesc->bLastTeamMember = false;
			}
		}

		eaClear(&peaQueueMembers);
	}

	eaSetSizeStruct(peaMembers, parse_QueueMemberDesc, c);

	if (c > 1)
	{
		eaQSort(*peaMembers, SortQueueMembersByGroup);
	}
	gclGetQueueMemberListFillTeamInfo(peaMembers);
	gclQueueMemberList_GroupMembers(pEntity, 
									peaMembers, 
									pchQueueName, 
									iGroupIndex, 
									bShowGroupHeaders, 
									bShowEmptySlots); 

	ui_GenSetManagedListSafe(pGen, peaMembers, QueueMemberDesc, true);
	return c;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPrivateQueueMemberList");
S32 exprGetPrivateQueueMemberList(SA_PARAM_NN_VALID UIGen *pGen, 
								  SA_PARAM_OP_VALID Entity *pEntity, 
								  S32 iGroupIndex, 
								  bool bShowGroupHeaders,
								  bool bShowEmptySlots)
{
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEntity, &pQueueData);
	if (pQueueData && pQueueData->uPrivateInstance > 0)
	{
		const char* pchQueueName = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		return gclGetQueueInstanceMemberList(pGen, 
											 pEntity, 
											 pchQueueName, 
											 uInstanceID, 
											 iGroupIndex, 
											 bShowGroupHeaders,
											 bShowEmptySlots);
	}
	ui_GenSetListSafe(pGen, NULL, QueueMemberDesc);
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPrivateQueueMemberCount");
S32 exprGetPrivateQueueMemberCount(SA_PARAM_OP_VALID Entity *pEntity, S32 iGroupIndex)
{
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEntity, &pQueueData);
	if (pEntity && pQueueData && pQueueData->uPrivateInstance > 0)
	{
		PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEntity, pPlayer, pPlayerQueueInfo);
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pQueueData->pchPrivateQueue, pQueueData->uPrivateInstance, true);
		if (pInstance)
		{
			int i, iMemberCount = eaSize(&pInstance->eaMembers);
			S32 iCount = iMemberCount;
			for (i = 0; iGroupIndex >= 0 && i < iMemberCount; i++)
			{
				if (pInstance->eaMembers[i]->iGroupIndex != iGroupIndex)
					iCount--;
			}
			return iCount;
		}
	}
	return 0;
}

static int SortQueueMembersByName(const QueueMemberDesc **ppMemberA, const QueueMemberDesc **ppMemberB)
{
	return stricmp((*ppMemberA)->pchName, (*ppMemberB)->pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueInviteList");
void exprGetQueueInviteList(SA_PARAM_NN_VALID UIGen *pGen, 
							bool bShowTeam, bool bShowGuild, bool bShowFriends, bool bShowLocal, bool bReset)
{
	Entity* pEntity = entActivePlayerPtr();
	QueueMemberDesc ***peaMembers = ui_GenGetManagedListSafe(pGen, QueueMemberDesc);
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEntity, pPlayer, pPlayerQueueInfo);
	S32 i, j, iCount = 0;
	U32 uiThisFrame;

	frameLockedTimerGetTotalFrames(gGCLState.frameLockedTimer, &uiThisFrame);
	eaClear(peaMembers);

	if (pEntity && pEntity->pPlayer && pQueueInfo)
	{
		static StashTable s_stInviteHash = NULL;

		if (s_stInviteHash==NULL)
		{
			s_stInviteHash = stashTableCreateInt(20);
		}
		else
		{
			stashTableClear(s_stInviteHash);
		}
		if (bShowFriends)
		{
			ChatState* pChatState = pEntity->pPlayer->pUI->pChatState;
			
			for (i = 0; i < eaSize(&pChatState->eaFriends); i++)
			{
				QueueMemberDesc* pDesc = NULL;
				ChatPlayerStruct* pFriend = pChatState->eaFriends[i];
				if (pFriend->pPlayerInfo.onlineCharacterID == 0)
					continue;
				if (pFriend->online_status & USERSTATUS_HIDDEN)
				{
					stashIntAddPointer(s_stInviteHash, pFriend->pPlayerInfo.onlineCharacterID, NULL, true);
				}
				else if (!stashIntFindPointer(s_stInviteHash, pFriend->pPlayerInfo.onlineCharacterID, &pDesc))
				{
					pDesc = eaGetStruct(peaMembers, parse_QueueMemberDesc, iCount++);
					pDesc->bIsGuildmate = pDesc->bIsTeammate = false;
					stashIntAddPointer(s_stInviteHash, pFriend->pPlayerInfo.onlineCharacterID, pDesc, false);
				}
				if (!pDesc)
					continue;
				pDesc->bIsFriend = true;
				if (pDesc->uiLastUpdateFrame != uiThisFrame)
				{
					pDesc->pchName = pFriend->pPlayerInfo.onlinePlayerName;
					pDesc->pchAllegiance = pFriend->pPlayerInfo.onlinePlayerAllegiance;
					SET_HANDLE_FROM_STRING("CharacterClass", pFriend->pPlayerInfo.pchClassName, pDesc->hClass);
					pDesc->uiEntID = pFriend->pPlayerInfo.onlineCharacterID;
					pDesc->iLevel = pFriend->pPlayerInfo.iPlayerLevel;
					pDesc->iRank = pFriend->pPlayerInfo.iPlayerRank;
					pDesc->uiTeamID = (U32)pFriend->pPlayerInfo.iPlayerTeam;
					pDesc->uiLastUpdateFrame = uiThisFrame;
				}
			}
		}
		if (bShowGuild)
		{
			Guild* pGuild = guild_GetGuild(pEntity);
			if (pGuild && pEntity->pPlayer->pGuild->eState == GuildState_Member)
			{
				for (i = 0; i < eaSize(&pGuild->eaMembers); i++)
				{
					QueueMemberDesc* pDesc;
					GuildMember* pMember = pGuild->eaMembers[i];
					if (!pMember->bOnline || pMember->iEntID == entGetContainerID(pEntity))
						continue;
					if (!stashIntFindPointer(s_stInviteHash, pMember->iEntID, &pDesc))
					{
						pDesc = eaGetStruct(peaMembers, parse_QueueMemberDesc, iCount++);
						pDesc->bIsFriend = pDesc->bIsTeammate = false;
						stashIntAddPointer(s_stInviteHash, pMember->iEntID, pDesc, false);
					}
					if (!pDesc)
						continue;
					pDesc->bIsGuildmate = true;
					if (pDesc->uiLastUpdateFrame != uiThisFrame)
					{
						pDesc->pchName = pMember->pcName;
						SET_HANDLE_FROM_STRING("CharacterClass", pMember->pchClassName, pDesc->hClass);
						pDesc->uiEntID = pMember->iEntID;
						pDesc->iLevel = pMember->iLevel;
						pDesc->iRank = pMember->iOfficerRank;
						pDesc->pchAllegiance = queue_EntGetQueueAffiliation(pEntity);
						pDesc->uiTeamID = 0;
						pDesc->uiLastUpdateFrame = uiThisFrame;
					}
				}
			}
		}
		if (bShowTeam)
		{
			Team* pTeam = team_GetTeam(pEntity);
			if (pTeam)
			{
				for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
				{
					QueueMemberDesc* pDesc;
					TeamMember* pMember = pTeam->eaMembers[i];
					if (pMember->iEntID == entGetContainerID(pEntity))
						continue;
					if (!stashIntFindPointer(s_stInviteHash, pMember->iEntID, &pDesc))
					{
						pDesc = eaGetStruct(peaMembers, parse_QueueMemberDesc, iCount++);
						pDesc->bIsFriend = pDesc->bIsGuildmate = false;
						stashIntAddPointer(s_stInviteHash, pMember->iEntID, pDesc, false);
					}
					if (!pDesc)
						continue;
					pDesc->bIsTeammate = true;
					pDesc->uiTeamID = pTeam->iContainerID;
					if (pDesc->uiLastUpdateFrame != uiThisFrame)
					{
						AllegianceDef* pAllegiance;
						Entity* pMemberEnt = GET_REF(pMember->hEnt);
						pDesc->pchName = pMember->pcName;
						SET_HANDLE_FROM_STRING("CharacterClass", pMember->pchClassName, pDesc->hClass);
						pDesc->uiEntID = pMember->iEntID;
						pDesc->pchAllegiance = queue_EntGetQueueAffiliation(pEntity);
						pDesc->iLevel = pMember->iExpLevel;
						pAllegiance = pMemberEnt ? allegiance_GetOfficerPreference(GET_REF(pMemberEnt->hAllegiance), GET_REF(pMemberEnt->hSubAllegiance)) : NULL;
						// Guess at rank
						if (!Officer_GetRankAndGradeFromLevel(pDesc->iLevel, pAllegiance, &pDesc->iRank, NULL))
						{
							pDesc->iRank = 0;
						}
						pDesc->uiLastUpdateFrame = uiThisFrame;
					}
				}
			}
		}
		if (bShowLocal)
		{
			EntityIterator* pIter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			Entity* pEnt;
			while (pEnt = EntityIteratorGetNext(pIter))
			{
				QueueMemberDesc* pDesc;
				if (entGetRef(pEnt) == entGetRef(pEntity))
					continue;
				if (!pEnt->pChar || !pEnt->pPlayer)
					continue;
				if (!entIsWhitelisted(pEnt, pEntity, kPlayerWhitelistFlags_PvPInvites))
					continue;
				if (!stashIntFindPointer(s_stInviteHash, entGetContainerID(pEnt), &pDesc))
				{
					pDesc = eaGetStruct(peaMembers, parse_QueueMemberDesc, iCount++);
					pDesc->bIsFriend = pDesc->bIsGuildmate = pDesc->bIsTeammate = false;
					stashIntAddPointer(s_stInviteHash, entGetContainerID(pEnt), pDesc, false);
				}
				if (!pDesc)
					continue;
				if (pDesc->uiLastUpdateFrame != uiThisFrame)
				{
					AllegianceDef* pAllegiance;
					pDesc->pchName = entGetLocalName(pEnt);
					COPY_HANDLE(pDesc->hClass, pEnt->pChar->hClass);
					pDesc->uiEntID = entGetContainerID(pEnt);
					pDesc->iLevel = pEnt->pChar->iLevelCombat;
					pAllegiance = allegiance_GetOfficerPreference(GET_REF(pEnt->hAllegiance), GET_REF(pEnt->hSubAllegiance));
					//Guess at rank
					if (!Officer_GetRankAndGradeFromLevel(pDesc->iLevel, pAllegiance, &pDesc->iRank, NULL))
					{
						pDesc->iRank = 0;
					}
					pDesc->pchAllegiance = queue_EntGetQueueAffiliation(pEnt);
					pDesc->uiTeamID = team_GetTeamID(pEnt);
					pDesc->uiLastUpdateFrame = uiThisFrame;
				}
			}
			EntityIteratorRelease(pIter);
		}
		if (s_pInviteMatch)
		{
			for (i = eaSize(&s_pInviteMatch->eaGroups)-1; i >= 0; i--)
			{
				QueueGroup* pGroup = s_pInviteMatch->eaGroups[i];
				for (j = eaSize(&pGroup->eaMembers)-1; j >= 0; j--)
				{
					U32 uiMemberID = pGroup->eaMembers[j]->uEntID;

					if (!stashIntFindPointer(s_stInviteHash, uiMemberID, NULL))
					{
						queue_Match_RemoveMemberFromGroup(s_pInviteMatch, pGroup->iGroupIndex, uiMemberID);
					}
				}
			}
		}
	}

	while (eaSize(peaMembers) > iCount)
	{
		StructDestroy(parse_QueueMemberDesc, eaPop(peaMembers));
	}
	if (eaSize(peaMembers) > 1)
	{
		eaQSort(*peaMembers,SortQueueMembersByName);
	}
	ui_GenSetManagedListSafe(pGen, peaMembers, QueueMemberDesc, true);
}

// Only used for on-stack temporary groups
static bool queue_PlayerQueueGroupCache(SA_PARAM_OP_VALID PlayerQueueInstance* pInstance, 
										SA_PARAM_NN_VALID QueueDef* pQueueDef, 
										SA_PARAM_NN_VALID QueueMatch* pMatch,
										U32 uiEntID)
{
	bool bCanAddMember = true;

	if (s_pInviteMatch)
	{
		StructCopyAll(parse_QueueMatch, s_pInviteMatch, pMatch);
	}
	else
	{
		queue_InitMatchGroups(pQueueDef, pMatch);
	}
	if (pInstance)
	{
		S32 iMemberIdx, iMemberSize = eaSize(&pInstance->eaMembers);
		for (iMemberIdx = 0; iMemberIdx < iMemberSize; iMemberIdx++)
		{
			PlayerQueueMember* pMember = pInstance->eaMembers[iMemberIdx];
			if (NONNULL(pMember) && pMember->iGroupIndex >= 0)
			{
				queue_Match_AddMember(pMatch, pMember->iGroupIndex, pMember->uiID, pMember->uiTeamID);
				if (pMember->uiID == uiEntID)
				{
					bCanAddMember = false;
				}
			}
		}
	}
	return bCanAddMember;
}

static bool gclQueue_CanInvitePlayer(SA_PARAM_NN_VALID Entity* pEntity, 
									 SA_PARAM_NN_VALID QueueMemberDesc* pDesc,
									 const char* pchQueueName,
									 U32 uInstanceID,
									 S32 iLevelBandIndex)
{
	S32 i, iGroupID = -1;
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEntity, pPlayer, pPlayerQueueInfo);
	PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, true);
	NOCONST(QueueInstanceParams) QParams = {0};

	if (pDef)
	{
		bool bIgnoreLevelRestrictions;
		QueueInstanceParams* pParams;
		if (pInstance)
		{
			for (i = eaSize(&pInstance->eaMembers)-1; i >= 0; i--)
			{
				if (pInstance->eaMembers[i]->uiID == pDesc->uiEntID)
				{
					return false;
				}
			}
			pParams = pInstance->pParams;
		}
		else
		{
			QParams.iLevelBandIndex = iLevelBandIndex;
			pParams = (QueueInstanceParams*)&QParams;
		}

		bIgnoreLevelRestrictions = SAFE_MEMBER(pInstance, bIgnoreLevelRestrictions);
		if (!queue_EntCannotUseQueueInstance(NULL, 
											 pDesc->iLevel, 
											 pDesc->pchAllegiance, 
											 pParams, 
											 pDef,
											 bIgnoreLevelRestrictions,
											 true))
		{
			bool bSuccess = true;
			QueueMatch QMatch = {0};
			bSuccess = queue_PlayerQueueGroupCache(pInstance, pDef, &QMatch, pDesc->uiEntID);
			if (bSuccess)
			{
				iGroupID = queue_GetBestGroupIndexForPlayer(pDesc->pchAllegiance, 
															pDesc->uiTeamID, 
															-1, 
															false, 
															pDef, 
															&QMatch);
			}
			StructDeInit(parse_QueueMatch, &QMatch);
			if (!bSuccess)
			{
				return false;
			}
		}
	}
	return iGroupID >= 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QueueCanInvitePlayerToNewInstance");
bool exprQueueCanInvitePlayerToNewInstance(SA_PARAM_OP_VALID QueueMemberDesc* pDesc, 
										   const char* pchQueueName, 
										   S32 iLevelBandIndex)
{
	Entity* pEntity = entActivePlayerPtr();
	if (pDesc && pEntity)
	{
		return gclQueue_CanInvitePlayer(pEntity, pDesc, pchQueueName, 0, iLevelBandIndex);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QueueCanInvitePlayer");
bool exprQueueCanInvitePlayer(SA_PARAM_OP_VALID QueueMemberDesc* pDesc)
{
	Entity* pEntity = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEntity, &pQueueData);
	if (pDesc && pQueueData && pQueueData->uPrivateInstance > 0)
	{
		const char* pchQueueName = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		return gclQueue_CanInvitePlayer(pEntity, pDesc, pchQueueName, uInstanceID, 0);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QueueAddInvite");
bool exprQueueAddInvite(const char* pchQueueName, SA_PARAM_OP_VALID QueueMemberDesc* pDesc)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	
	if (pDesc && pDef)
	{
		S32 iGroupID;
		ANALYSIS_ASSUME(pDef);
		ANALYSIS_ASSUME(pDesc);
		if (s_pInviteMatch==NULL)
		{
			s_pInviteMatch = StructCreate(parse_QueueMatch);
			queue_InitMatchGroups(pDef, s_pInviteMatch);
		}
		else
		{
			S32 i;
			for (i = eaSize(&s_pInviteMatch->eaGroups)-1; i >= 0; i--)
			{
				QueueGroup* pGroup = s_pInviteMatch->eaGroups[i];

				if (queue_Match_FindMemberInGroup(pGroup, pDesc->uiEntID) >= 0)
				{
					return false;
				}
			}
		}
		iGroupID = queue_GetBestGroupIndexForPlayer(pDesc->pchAllegiance,
													pDesc->uiTeamID, 
													-1, 
													false, 
													pDef, 
													s_pInviteMatch);
		if (queue_Match_AddMember(s_pInviteMatch, iGroupID, pDesc->uiEntID, pDesc->uiTeamID))
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QueueRemoveInvite");
bool exprQueueRemoveInvite(SA_PARAM_OP_VALID QueueMemberDesc* pDesc)
{
	if (pDesc && s_pInviteMatch)
	{
		return queue_Match_RemoveMember(s_pInviteMatch, pDesc->uiEntID);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QueueClearInvites");
void exprQueueClearInvites(void)
{
	StructDestroySafe(parse_QueueMatch, &s_pInviteMatch);
	s_pInviteMatch = NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QueueHasInvites");
bool exprQueueHasInvites(void)
{
	return s_pInviteMatch ? s_pInviteMatch->iMatchSize > 0 : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QueueIsPlayerInvited");
bool exprQueueIsPlayerInvited(U32 uEntID)
{
	if (queue_Match_FindMember(s_pInviteMatch, uEntID, NULL) >= 0)
	{
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QueueSendInviteList");
void exprQueueSendInviteList(bool bInviteTeam)
{
	if (s_pInviteMatch)
	{
		if (s_pInviteMatch->iMatchSize > 0)
		{
			QueueIntArray* pList = gclQueue_CreateInviteListFromMatch(s_pInviteMatch);
			ServerCmd_queue_invitelist(pList, bInviteTeam);
			StructDestroy(parse_QueueIntArray, pList);
		}
		StructDestroySafe(parse_QueueMatch, &s_pInviteMatch);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QueueIsPrivateQueueFull");
bool exprQueueIsPrivateQueueFull(void)
{
	Entity* pEntity = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEntity, &pQueueData);
	if (pQueueData && pQueueData->uPrivateInstance > 0)
	{
		const char* pchQueueName = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		QueueDef* pDef = queue_DefFromName(pchQueueName);
		PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEntity, pPlayer, pPlayerQueueInfo);
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, true);

		if (pDef && pInstance)
		{
			QueueMatch QMatch = {0};
			U32 iMatchSize;
			queue_PlayerQueueGroupCache(pInstance, pDef, &QMatch, 0);
			iMatchSize = QMatch.iMatchSize;
			StructDeInit(parse_QueueMatch, &QMatch);
			
			if (iMatchSize == queue_QueueGetMaxPlayers(pDef, false))
			{
				return true;
			}
		}
	}
	return false;
}

// sets the UIGen's model list of all the Queues that the given player is currently queued of the given map type
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillQueuedQueuesList");
void exprFillQueuedQueuesList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char *pchMapType)
{
	QueueDesc ***peaQueues = ui_GenGetManagedListSafe(pGen, QueueDesc);
	PlayerQueueInfo *pPlayerQueueInfo = SAFE_MEMBER2(pEntity, pPlayer, pPlayerQueueInfo);
	int iCount = 0;

	if(pPlayerQueueInfo)
	{
		ZoneMapType eType = ZMTYPE_UNSPECIFIED;
		int i;
		
		if(pchMapType && *pchMapType)
			eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);

		for(i=0;i<eaSize(&pPlayerQueueInfo->eaQueues);i++)
		{
			PlayerQueue *pQueue = pPlayerQueueInfo->eaQueues[i];
			QueueDef *pDef = GET_REF(pQueue->hDef);
			int c;

			if(pDef == NULL)
				continue;

			if(eType == ZMTYPE_UNSPECIFIED || pDef->MapSettings.eMapType == eType)
			{
				for(c=0;c<eaSize(&pQueue->eaInstances);c++)
				{
					if(pQueue->eaInstances[c]->eQueueState == PlayerQueueState_InQueue)
					{
						QueueDesc *pDesc = eaGetStruct(peaQueues,parse_QueueDesc,iCount++);
						gclQueue_FillDesc(pEntity, pDesc, pDef, pQueue, eaGet(&pQueue->eaInstances,c), NULL, 0);
						break;
					}
				}
			}
		}
	}

	while (eaSize(peaQueues) > iCount)
		StructDestroy(parse_QueueDesc, eaPop(peaQueues));

	ui_GenSetManagedListSafe(pGen, peaQueues, QueueDesc, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillQueueList");
void exprFillQueueList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	QueueDesc ***peaQueues = ui_GenGetManagedListSafe(pGen, QueueDesc);
	PlayerQueueInfo *pPlayerQueueInfo = SAFE_MEMBER2(pEntity, pPlayer, pPlayerQueueInfo);
	int c = 0;

	if(pPlayerQueueInfo)
	{
		int i, n = eaSize(&pPlayerQueueInfo->eaQueues);
		
		for (i=0; i<n; i++)
		{
			PlayerQueue *pQueue = pPlayerQueueInfo->eaQueues[i];
			QueueDef *pDef = GET_REF(pQueue->hDef);

			if (pDef==NULL)
				continue;

			if(pDef->MapSettings.eMapType == ZMTYPE_PVP)
			{
				QueueDesc *pDesc = eaGetStruct(peaQueues,parse_QueueDesc,c++);
				gclQueue_FillDesc(pEntity, pDesc, pDef, pQueue, eaGet(&pQueue->eaInstances,0), NULL, 0);
			}
		}
	}

	while (eaSize(peaQueues) > c)
		StructDestroy(parse_QueueDesc, eaPop(peaQueues));

	ui_GenSetManagedListSafe(pGen, peaQueues, QueueDesc, true);
}

static void FillPVEQueueListFiltered(QueueDesc ***peaDescs, SA_PARAM_OP_VALID Entity *pEntity, const char *pchRequire, const char *pchExclude)
{
	PlayerQueueInfo *pPlayerQueueInfo = SAFE_MEMBER2(pEntity, pPlayer, pPlayerQueueInfo);
	int iDescCount = 0;

	if(pPlayerQueueInfo)
	{
		int i, j, n = eaSize(&pPlayerQueueInfo->eaQueues);

		for(i=0; i<n; i++)
		{
			PlayerQueue *pQueue = pPlayerQueueInfo->eaQueues[i];
			QueueDef *pDef = GET_REF(pQueue->hDef);

			if (pDef==NULL 
				|| pDef->MapSettings.eMapType != ZMTYPE_QUEUED_PVE
				|| (pchExclude && pchExclude[0] && strstri(pchExclude, StaticDefineIntRevLookup(QueueCategoryEnum, pDef->eCategory)))
				|| (pchRequire && pchRequire[0] && !strstri(pchRequire, StaticDefineIntRevLookup(QueueCategoryEnum, pDef->eCategory))))
				continue;

			if(!eaSize(&pQueue->eaInstances) 
				|| pDef->Settings.bPublic 
				|| gclInteract_FindRecentQueueInteract(pEntity, pDef->pchName))
			{
				QueueDesc *pDesc = eaGetStruct(peaDescs, parse_QueueDesc, iDescCount++);
				gclQueue_FillDesc(pEntity, pDesc, pDef, pQueue, eaGet(&pQueue->eaInstances,0), NULL, 0);
			}
			else
			{
				for(j = eaSize(&pQueue->eaInstances)-1; j >= 0; j--)
				{
					PlayerQueueInstance *pInstance = eaGet(&pQueue->eaInstances,j);
					if(pInstance->eQueueState != PlayerQueueState_None)
					{
						QueueDesc *pDesc = eaGetStruct(peaDescs, parse_QueueDesc, iDescCount++);
						gclQueue_FillDesc(pEntity, pDesc, pDef, pQueue, pInstance, NULL, 0);
					}
				}
			}
		}
	}

	if (peaDescs)
		eaSetSizeStruct(peaDescs, parse_QueueDesc, iDescCount);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetDataFromPVEQueue");
void exprGenSetDataFromPVEQueue(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char *pchRequire, const char *pchExclude, int iIndex)
{
	QueueDesc ***peaQueues = (QueueDesc***)ui_GenGetManagedList(pGen, parse_QueueDesc);
	FillPVEQueueListFiltered(peaQueues, pEntity, pchRequire, pchExclude);
	if (eaSize(peaQueues))
	{
		iIndex %= eaSize(peaQueues);
		if (iIndex < 0)
			iIndex += eaSize(peaQueues);		
		ui_GenSetPointer(pGen, (*peaQueues)[iIndex], parse_QueueDesc);
	}
	else
	{
		ui_GenSetPointer(pGen, NULL, parse_QueueDesc);
	}
	ui_GenSetManagedListSafe(pGen, peaQueues, QueueDesc, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillPVEQueueListFiltered");
void exprFillPVEQueueListFiltered(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char *pchRequire, const char *pchExclude)
{
	QueueDesc ***peaQueues = (QueueDesc ***)ui_GenGetManagedList(pGen, parse_QueueDesc);
	FillPVEQueueListFiltered(peaQueues, pEntity, pchRequire, pchExclude);
	ui_GenSetManagedListSafe(pGen, peaQueues, QueueDesc, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillPVEQueueList");
void exprFillPVEQueueList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	QueueDesc ***peaQueues = (QueueDesc ***)ui_GenGetManagedList(pGen, parse_QueueDesc);
	FillPVEQueueListFiltered(peaQueues, pEntity, NULL, NULL);
	ui_GenSetManagedListSafe(pGen, peaQueues, QueueDesc, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillReadyQueueList");
void exprFillReadyQueueList(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, int iMax)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	PlayerQueueInfo *pPlayerQueueInfo = SAFE_MEMBER2(pEntity, pPlayer, pPlayerQueueInfo);
	static QueueDesc** s_queueGenList = NULL;
	int c = 0;

	if(pPlayerQueueInfo)
	{
		int i, j, s, n = eaSize(&pPlayerQueueInfo->eaQueues);

		for (i = 0; i < n; i++)
		{
			PlayerQueue *pQueue = pPlayerQueueInfo->eaQueues[i];
			QueueDef *pDef = GET_REF(pQueue->hDef);

			if (pDef==NULL)
				continue;

			s = eaSize(&pQueue->eaInstances);
			for (j=0; j<s; j++)
			{
				PlayerQueueInstance *pInstance = pQueue->eaInstances[j];

				if (pInstance->eQueueState == PlayerQueueState_Offered)
				{
					QueueDesc *pDesc = eaGetStruct(&s_queueGenList,parse_QueueDesc,c++);
					gclQueue_FillDesc(pEntity, pDesc, pDef, pQueue, pInstance, NULL, 0);

					// Limit number of entries
					if (eaSize(&s_queueGenList) >= iMax)
					{
						break;
					}
				}
			}
		}
	}

	while (eaSize(&s_queueGenList) > c)
		StructDestroy(parse_QueueDesc, eaPop(&s_queueGenList));

	ui_GenSetManagedListSafe(pGen, &s_queueGenList, QueueDesc, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetDisplayName);
const char* exprQueue_GetDisplayName(const char* pchQueueName)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	if (pDef)
	{
		return TranslateDisplayMessage(pDef->displayNameMesg);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueDisplayName");
const char* exprGetQueueGetQueueDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID QueueDesc* pDesc)
{
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (pDesc)
		FormatGameDisplayMessage(&estrBuffer, &pDesc->displayNameMesg, STRFMT_END);

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetDescription);
const char* exprQueue_GetDescription(const char* pchQueueName)
{
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	if (pDef)
	{
		return TranslateDisplayMessage(pDef->descriptionMesg);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueDescription");
const char* exprGetQueueGetQueueDescription(ExprContext *pContext, SA_PARAM_OP_VALID QueueDesc* pDesc)
{
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (pDesc)
		FormatGameDisplayMessage(&estrBuffer, &pDesc->descriptionMesg, STRFMT_END);

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QueueGetRequiredClasses");
const char* exprQueueGetRequiredClasses(SA_PARAM_OP_VALID QueueDesc* pDesc, const char* pchClassFormatMsgKey)
{
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (pDesc)
	{
		QueueDef* pDef = queue_DefFromName(pDesc->pchQueueName);
		if (pDef)
		{
			const char** ppchClassNames = NULL;
			int i, iSize;

			for (i = 0; i < eaiSize(&pDef->Requirements.piClassCategoriesRequired); i++)
			{
				S32 eCategory = pDef->Requirements.piClassCategoriesRequired[i];
				const char* pchClassCategory = StaticDefineGetTranslatedMessage(CharClassCategoryEnum, eCategory);
				if (EMPTY_TO_NULL(pchClassCategory))
				{
					eaPush(&ppchClassNames, pchClassCategory);
				}
			}
			for (i = 0; i < eaSize(&pDef->Requirements.ppClassesRequired); i++)
			{
				CharacterClass* pClass = GET_REF(pDef->Requirements.ppClassesRequired[i]->hClass);
				if (pClass)
				{
					const char* pchClassName = TranslateDisplayMessage(pClass->msgDisplayName);
					if (EMPTY_TO_NULL(pchClassName))
					{
						eaPush(&ppchClassNames, pchClassName);
					}
				}
			}
			iSize = eaSize(&ppchClassNames);
			for (i = 0; i < iSize; i++)
			{
				FormatGameMessageKey(&estrBuffer, pchClassFormatMsgKey, 
					STRFMT_STRING("ClassName", ppchClassNames[i]), 
					STRFMT_INT("First", i == 0),
					STRFMT_INT("Last", i == iSize-1),
					STRFMT_END);
			}
			
			eaDestroy(&ppchClassNames);
		}	
	}

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueGroupDisplayName");
const char* exprGetQueueGroupDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID QueueDef* pDef, int iGroupIdx)
{
	QueueGroupDef* pGroupDef = pDef ? eaGet(&pDef->eaGroupDefs, iGroupIdx) : NULL;
	return pGroupDef ? TranslateDisplayMessage(pGroupDef->DisplayName) : "";
}



// Given a RowData of a QueueDesc, return if it is PvP or not
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsQueuePvP");
bool exprGetQueueIsPvP(ExprContext *pContext, SA_PARAM_OP_VALID QueueDesc* pDesc)
{
	QueueDef* pQueueDef = pDesc ? queue_DefFromName(pDesc->pchQueueName) : NULL;

	if (pQueueDef==NULL)
		return false;

	return(pQueueDef->MapRules.QGameRules.publicRules.eGameType!=kPVPGameType_None);
}


// This difers from playercountdesc in that it Always returns "Total" "Group1" and "Group2" it is up to the uigen to determine which it wants
//   PlayerCountDesc is problematic in that it only can determine the group counts if affiliation is different. Which is only if someone is in the queue.
//   I don't want to change Star Trek's behaviour in case they somehow depend on that.
#define QUEUE_COUNTS_FORMATS 3
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueMemberCountsDesc");
const char* exprGetQueueMemberCountsDesc(ExprContext *pContext, SA_PARAM_OP_VALID QueueDesc* pDesc, 
										const char* pchDescFormat, const char* pchGroupCountFormat)
{
	QueueDef* pDef = pDesc ? queue_DefFromName(pDesc->pchQueueName) : NULL;
	char* estrResult = NULL;
	char* estrGroup[QUEUE_COUNTS_FORMATS];
	char* result = NULL;
	int i;
	S32 iPlayerCount;

	if (pDef==NULL)
		return "";

	for (i = 0; i < QUEUE_COUNTS_FORMATS; i++)
	{
		estrGroup[i] = NULL;
		estrStackCreate(&estrGroup[i]);
	}

	estrStackCreate(&estrResult);

	iPlayerCount = pDesc->iTotalPlayerCount;

	FormatGameMessageKey(&estrGroup[0], pchGroupCountFormat,
		STRFMT_INT("CurPlayers", iPlayerCount),
		STRFMT_INT("MinPlayers", pDesc->iPlayerCountMin),
		STRFMT_INT("MaxPlayers", pDesc->iPlayerCountMax),
		STRFMT_INT("MeetsReqs", iPlayerCount >= pDesc->iPlayerCountMin),
		STRFMT_END);

	// Get Group1/Group2 info
	for (i = 0; i < 2; i++)
	{
		QueueGroupDef* pGroupDef = eaGet(&pDef->eaGroupDefs, i);
			
		if (pGroupDef)
		{
			bool bOvertime = pDesc->bOvertime;
			int iPlayerCountMin = queue_GetGroupMinSize(pGroupDef, bOvertime);
			int iPlayerCountMax = queue_GetGroupMaxSize(pGroupDef, bOvertime);
			iPlayerCount = ea32Get(&pDesc->piGroupPlayerCount,i);
			FormatGameMessageKey(&estrGroup[i+1], pchGroupCountFormat,	// NOTE +1
				STRFMT_INT("CurPlayers", iPlayerCount),
				STRFMT_INT("MinPlayers", iPlayerCountMin),
				STRFMT_INT("MaxPlayers", iPlayerCountMax),
				STRFMT_INT("MeetsReqs", iPlayerCount >= iPlayerCountMin),
				STRFMT_END);
		}
	}
	
	FormatGameMessageKey(&estrResult, pchDescFormat,
		STRFMT_STRING("Total", estrGroup[0]),
		STRFMT_STRING("Group1", estrGroup[1]),
		STRFMT_STRING("Group2", estrGroup[2]),
		STRFMT_END);

	if (strlen(estrResult))
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
	}
	estrDestroy(&estrResult);
	for (i = 0; i < QUEUE_COUNTS_FORMATS; i++)
	{
		estrDestroy(&estrGroup[i]);
	}
	return NULL_TO_EMPTY(result);
}



#define QUEUE_GROUP_FORMAT_COUNT 2
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueuePlayerCountDesc");
const char* exprGetQueuePlayerCountDesc(ExprContext *pContext, SA_PARAM_OP_VALID QueueDesc* pDesc, 
										const char* pchDescFormat, const char* pchGroupCountFormat)
{
	QueueDef* pDef = pDesc ? queue_DefFromName(pDesc->pchQueueName) : NULL;
	char* estrResult = NULL;
	char* estrGroup[QUEUE_GROUP_FORMAT_COUNT];
	char* result = NULL;
	int i;

	if (pDef==NULL)
		return "";

	for (i = 0; i < QUEUE_GROUP_FORMAT_COUNT; i++)
	{
		estrGroup[i] = NULL;
	}

	estrStackCreate(&estrResult);
	if (pDesc->bAllGroupsSameAffiliation)
	{
		S32 iPlayerCount = pDesc->iTotalPlayerCount;

		estrStackCreate(&estrGroup[0]);
		FormatGameMessageKey(&estrGroup[0], pchGroupCountFormat,
			STRFMT_INT("CurPlayers", iPlayerCount),
			STRFMT_INT("MinPlayers", pDesc->iPlayerCountMin),
			STRFMT_INT("MaxPlayers", pDesc->iPlayerCountMax),
			STRFMT_INT("MeetsReqs", iPlayerCount >= pDesc->iPlayerCountMin),
			STRFMT_END);
		FormatGameMessageKey(&estrResult, pchDescFormat,
			STRFMT_STRING("All", estrGroup[0]),
			STRFMT_STRING("Group1", ""),
			STRFMT_STRING("Group2", ""),
			STRFMT_END);
	}
	else
	{
		for (i = 0; i < QUEUE_GROUP_FORMAT_COUNT; i++)
		{
			QueueGroupDef* pGroupDef = eaGet(&pDef->eaGroupDefs, i);
			
			if (pGroupDef)
			{
				bool bOvertime = pDesc->bOvertime;
				int iPlayerCount = ea32Get(&pDesc->piGroupPlayerCount,i);
				int iPlayerCountMin = queue_GetGroupMinSize(pGroupDef, bOvertime);
				int iPlayerCountMax = queue_GetGroupMaxSize(pGroupDef, bOvertime);
				estrStackCreate(&estrGroup[i]);
				FormatGameMessageKey(&estrGroup[i], pchGroupCountFormat,
					STRFMT_INT("CurPlayers", iPlayerCount),
					STRFMT_INT("MinPlayers", iPlayerCountMin),
					STRFMT_INT("MaxPlayers", iPlayerCountMax),
					STRFMT_INT("MeetsReqs", iPlayerCount >= iPlayerCountMin),
					STRFMT_END);
			}
		}
		FormatGameMessageKey(&estrResult, pchDescFormat,
			STRFMT_STRING("All", ""),
			STRFMT_STRING("Group1", estrGroup[0]),
			STRFMT_STRING("Group2", estrGroup[1]),
			STRFMT_END);
	}

	if (strlen(estrResult))
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
	}
	estrDestroy(&estrResult);
	for (i = 0; i < 2; i++)
	{
		estrDestroy(&estrGroup[i]);
	}
	return NULL_TO_EMPTY(result);
}

static void gclQueueUpdateMapNames(const char* pchQueueName, U32 uiInstanceID, QueueRulesData*** peaMapData)
{
	Entity* pEntity = entActivePlayerPtr();
	static U32 s_uiLastFrame = 0;
	static QueueRulesData** s_eaMapData = NULL;
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEntity, pPlayer, pPlayerQueueInfo);
	PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uiInstanceID, true);
	S32 i, iSize = 0;
	U32 uiThisFrame;

	frameLockedTimerGetTotalFrames(gGCLState.frameLockedTimer, &uiThisFrame);

	if (pDef && peaMapData)
	{
		if (uiThisFrame != s_uiLastFrame)
		{
			const char* pchMapName = SAFE_MEMBER2(pInstance, pParams, pchMapName); 
			if (pchMapName)
			{
				QueueRulesData* pData = eaGetStruct(&s_eaMapData, parse_QueueRulesData, iSize++);
				pData->pchMapName = allocAddString(pchMapName);
				pData->iIndex = queue_GetMapIndexByName(pDef, pchMapName);
				StructCopyString(&pData->pchString, gclQueue_GetMapDisplayName(pData->pchMapName));
			}
			else
			{
				S32 iMapCount = queue_GetMapIndexByName(pDef, NULL);
				for (i = 0; i < iMapCount; i++)
				{
					QueueRulesData* pData = eaGetStruct(&s_eaMapData, parse_QueueRulesData, iSize++);
					pData->pchMapName = allocAddString(queue_GetMapNameByIndex(pDef, i));
					pData->iIndex = i;
					StructCopyString(&pData->pchString, gclQueue_GetMapDisplayName(pData->pchMapName));
				}
			}
			while (eaSize(&s_eaMapData) > iSize)
			{
				StructDestroy(parse_QueueRulesData, eaPop(&s_eaMapData));
			}
			s_uiLastFrame = uiThisFrame;
		}
		(*peaMapData) = s_eaMapData;
	}
}

static void gclQueue_FillLevelBandRulesData(QueueRulesData* pData,
											QueueLevelBand* pLevelBand, 
											S32 iIndex, 
											bool bBolster)
{
	pData->iBolsterLevel = pLevelBand->iBolsterLevel;
	pData->iMinLevel = pLevelBand->iMinLevel;
	pData->iMaxLevel = pLevelBand->iMaxLevel;
	pData->bBolster = bBolster;
	pData->iIndex = iIndex;
}

static int gclQueueLevelBands_SortByBolsterLevel(const QueueRulesData** ppRulesA, const QueueRulesData** ppRulesB)
{
	QueueRulesData* pA = (QueueRulesData*)(*ppRulesA);
	QueueRulesData* pB = (QueueRulesData*)(*ppRulesB);

	return pA->iBolsterLevel - pB->iBolsterLevel;
}

static int gclQueueGetLevelBands(Entity* pEntity, QueueDef* pDef, bool bBolster, QueueRulesData*** peaLevelBands)
{
	S32 i, iSize = 0;
	S32 iEntLevel = LevelFromLevelingNumeric(item_GetLevelingNumeric(pEntity));
	for (i = 0; i < eaSize(&pDef->eaLevelBands); i++)
	{
		QueueLevelBand* pLevelBand = pDef->eaLevelBands[i];
		bool bIsBolsterLevelBand = queue_GetBolsterType(pDef, pLevelBand) != kBolsterType_None;
		
		if ((bBolster && !bIsBolsterLevelBand) || (!bBolster && bIsBolsterLevelBand))
			continue;

		if (	(pLevelBand->iMinLevel == 0 || pLevelBand->iMinLevel <= iEntLevel)
			&&	(pLevelBand->iMaxLevel == 0 || pLevelBand->iMaxLevel >= iEntLevel))
		{
			if (peaLevelBands)
			{
				QueueRulesData* pData = eaGetStruct(peaLevelBands, parse_QueueRulesData, iSize++);
				gclQueue_FillLevelBandRulesData(pData, pLevelBand, i, bBolster);
			}
			else
			{
				iSize++;
			}
		}
	}
	return iSize;
}

static void gclQueueUpdateLevelBands(const char* pchQueueName, 
									 U32 uiInstanceID, 
									 bool bBolster,
									 QueueRulesData*** peaLevelBands)
{
	Entity* pEntity = entActivePlayerPtr();
	static QueueRulesData** s_eaLevelBands = NULL;
	static U32 s_uiLastFrame = 0;
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	PlayerQueueInfo* pQueueInfo = SAFE_MEMBER2(pEntity, pPlayer, pPlayerQueueInfo);
	PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uiInstanceID, true);
	S32 iSize = 0;
	U32 uiThisFrame;

	frameLockedTimerGetTotalFrames(gGCLState.frameLockedTimer, &uiThisFrame);

	if (pDef && peaLevelBands)
	{
		if (uiThisFrame != s_uiLastFrame)
		{
			if (pInstance && pInstance->pParams)
			{
				S32 iLevelBandIndex = pInstance->pParams->iLevelBandIndex;
				QueueLevelBand* pLevelBand = eaGet(&pDef->eaLevelBands,iLevelBandIndex);
				if (pLevelBand)
				{
					QueueRulesData* pData = eaGetStruct(&s_eaLevelBands, parse_QueueRulesData, iSize++);
					gclQueue_FillLevelBandRulesData(pData, pLevelBand, iLevelBandIndex, bBolster);
				}
			}
			else
			{
				iSize = gclQueueGetLevelBands(pEntity, pDef, bBolster, &s_eaLevelBands);
			}
			while (eaSize(&s_eaLevelBands) > iSize)
			{
				StructDestroy(parse_QueueRulesData, eaPop(&s_eaLevelBands));
			}
			if (bBolster)
			{
				eaQSort(s_eaLevelBands, gclQueueLevelBands_SortByBolsterLevel);
			}
			s_uiLastFrame = uiThisFrame;
		}
		(*peaLevelBands) = s_eaLevelBands;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueMapNames");
void exprGetQueueGetQueueMapNames(SA_PARAM_NN_VALID UIGen* pGen, const char* pchQueueName, U32 uiInstanceID)
{
	S32 i, iSize = 0;
	QueueRulesData** eaData = NULL;
	QueueRulesData*** peaData = ui_GenGetManagedListSafe(pGen, QueueRulesData);
	gclQueueUpdateMapNames(pchQueueName, uiInstanceID, &eaData);

	for (i = 0; i < eaSize(&eaData); i++)
	{
		QueueRulesData* pCurrData = eaData[i];
		QueueRulesData* pData = eaGetStruct(peaData, parse_QueueRulesData, iSize++);
		StructCopyString(&pData->pchString, pCurrData->pchString);
		pData->pchMapName = allocAddString(pCurrData->pchMapName);
		pData->iIndex = pCurrData->iIndex;
		pData->bEmpty = false;
	}
	while (eaSize(peaData) > iSize)
	{
		StructDestroy(parse_QueueRulesData, eaPop(peaData));
	}
	ui_GenSetManagedListSafe(pGen, peaData, QueueRulesData, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueMapNamesCount");
S32 exprGetQueueGetQueueMapNamesCount(const char* pchQueueName, U32 uiInstanceID)
{
	QueueRulesData** eaMapData = NULL;
	gclQueueUpdateMapNames(pchQueueName, uiInstanceID, &eaMapData);
	return eaSize(&eaMapData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueMapNameByIndex");
const char* exprGetQueueGetQueueMapNameByIndex(const char* pchQueueName, U32 uiInstanceID, S32 iIndex)
{
	QueueRulesData* pData;
	QueueRulesData** eaMapData = NULL;
	gclQueueUpdateMapNames(pchQueueName, uiInstanceID, &eaMapData);
	
	if (pData = eaGet(&eaMapData, iIndex))
	{
		return pData->pchString;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueLevelBands");
void exprGetQueueGetQueueLevelBands(SA_PARAM_NN_VALID UIGen* pGen, 
									const char* pchQueueName, 
									U32 uiInstanceID, 
									bool bBolster)
{
	S32 i, iSize = 0;
	QueueRulesData** eaData = NULL;
	QueueRulesData*** peaData = ui_GenGetManagedListSafe(pGen, QueueRulesData);
	gclQueueUpdateLevelBands(pchQueueName, uiInstanceID, bBolster, &eaData);

	for (i = 0; i < eaSize(&eaData); i++)
	{
		QueueRulesData* pCurrData = eaData[i];
		QueueRulesData* pData = eaGetStruct(peaData, parse_QueueRulesData, iSize++);
		pData->iIndex = pCurrData->iIndex;
		pData->iBolsterLevel = pCurrData->iBolsterLevel;
		pData->iMinLevel = pCurrData->iMinLevel;
		pData->iMaxLevel = pCurrData->iMaxLevel;
		pData->bEmpty = false;
		pData->bBolster = pCurrData->bBolster;
	}
	if (iSize == 0)
	{
		QueueRulesData* pData = eaGetStruct(peaData, parse_QueueRulesData, iSize++);
		pData->iIndex = -1;
		pData->bEmpty = true;
	}
	while (eaSize(peaData) > iSize)
	{
		StructDestroy(parse_QueueRulesData, eaPop(peaData));
	}
	ui_GenSetManagedListSafe(pGen, peaData, QueueRulesData, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueLevelBandsCount");
S32 exprGetQueueGetQueueLevelBandsCount(const char* pchQueueName, bool bBolster)
{
	Entity* pEntity = entActivePlayerPtr();
	QueueDef* pDef = queue_DefFromName(pchQueueName);
	S32 iCount = 0;

	if (pDef && pEntity)
	{
		iCount = gclQueueGetLevelBands(pEntity, pDef, bBolster, NULL);
	}
	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetQueueLevelBandByIndex");
S32 exprGetQueueGetQueueLevelBandByIndex(SA_PARAM_NN_VALID UIGen* pGen, const char* pchQueueName, U32 uiInstanceID, bool bBolster, S32 iIndex)
{
	QueueRulesData* pData;
	QueueRulesData** eaLevelBands = NULL;
	
	gclQueueUpdateLevelBands(pchQueueName, uiInstanceID, bBolster, &eaLevelBands);
	pData = eaGet(&eaLevelBands, iIndex);

	ui_GenSetPointer(pGen, pData, parse_QueueRulesData);

	return pData ? pData->iIndex : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(queue_GetReadyQueue);
SA_RET_OP_VALID PlayerQueueInstance* exprQueue_GetReadyQueue(SA_PARAM_OP_VALID Entity *pEnt, const char *pchMapType)
{
	if(pEnt)
	{
		ZoneMapType eType = ZMTYPE_UNSPECIFIED;
		if(pchMapType && *pchMapType)
			eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);

		return Queue_GetReadyQueueOfType(pEnt, eType, NULL);
	}
	return(NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyDesc");
const char* exprGetQueueDesc(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchMapType)
{
	static char *estrBuffer = NULL;
	QueueDef *pQueueDef = NULL;
	ZoneMapType eType = ZMTYPE_UNSPECIFIED;

	if(pchMapType && *pchMapType)
		eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);

	Queue_GetReadyQueueOfType(pEntity, eType, &pQueueDef);

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (pQueueDef)
		FormatGameDisplayMessage(&estrBuffer, &pQueueDef->descriptionMesg, STRFMT_PLAYER(pEntity), STRFMT_ENTITY(pEntity), STRFMT_END);

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyDisplayName");
const char* exprGetQueueDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchMapType)
{
	static char *estrBuffer = NULL;
	QueueDef *pQueueDef = NULL;
	ZoneMapType eType = ZMTYPE_UNSPECIFIED;

	if(pchMapType && *pchMapType)
		eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);

	Queue_GetReadyQueueOfType(pEntity, eType, &pQueueDef);

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (pQueueDef)
		FormatGameDisplayMessage(&estrBuffer, &pQueueDef->displayNameMesg, STRFMT_END);

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyName");
const char* exprGetQueueName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchMapType)
{
	QueueDef *pQueueDef = NULL;
	ZoneMapType eType = ZMTYPE_UNSPECIFIED;

	if(pchMapType && *pchMapType)
		eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);
	Queue_GetReadyQueueOfType(pEntity, eType, &pQueueDef);

	return pQueueDef ? pQueueDef->pchName : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyGameType");
U32 exprGetGameType(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchMapType)
{
	ZoneMapType eType = ZMTYPE_UNSPECIFIED;
	PlayerQueueInstance *pInstance = NULL;

	if(pchMapType && *pchMapType)
		eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);

	pInstance = Queue_GetReadyQueueOfType(pEntity,eType,NULL);

	if(pInstance)
		return pInstance->pParams->eGameType;
	else
		return 0;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyGameTypeString");
const char* exprGetGameTypeString(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchMapType)
{
	ZoneMapType eType = ZMTYPE_UNSPECIFIED;
	PlayerQueueInstance *pInstance = NULL;

	if(pchMapType && *pchMapType)
		eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);

	pInstance = Queue_GetReadyQueueOfType(pEntity,eType,NULL);

	if(pInstance)
		return StaticDefineIntRevLookup(PVPGameTypeEnum,pInstance->pParams->eGameType);
	else
		return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyInstance");
S32 exprGetQueueInstance(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchMapType)
{
	ZoneMapType eType = ZMTYPE_UNSPECIFIED;
	PlayerQueueInstance *pInstance = NULL;

	if(pchMapType && *pchMapType)
		eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);

	pInstance = Queue_GetReadyQueueOfType(pEntity,eType,NULL);

	return pInstance ? pInstance->uiID : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_ReadyQueueGetInviterName");
const char* exprReadyQueueGetInviterName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	PlayerQueueInstance* pInstance = Queue_GetReadyQueueOfType(pEntity, ZMTYPE_UNSPECIFIED, NULL);
	if (pInstance && pInstance->eQueueState == PlayerQueueState_Invited)
	{
		PlayerQueueMember* pMember = eaGet(&pInstance->eaMembers, 0);
		Entity* pOwner = pMember ? GET_REF(pMember->hEntity) : NULL;
		if (pOwner)
		{
			return entGetLocalName(pOwner);
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_ReadyQueueGetMapType");
S32 exprReadyQueueGetMapType(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	QueueDef *pQueueDef = NULL;
	Queue_GetReadyQueueOfType(pEntity, ZMTYPE_UNSPECIFIED, &pQueueDef);
	
	if (pQueueDef)
	{
		return pQueueDef->MapSettings.eMapType;
	}
	return ZMTYPE_UNSPECIFIED;
}

static const char* gclQueue_GetStatusText(ExprContext *pContext, Entity* pEntity, PlayerQueueInstance* pInstance)
{
	char* estrBuffer = NULL;
	char* pchStatus = NULL;

	estrStackCreate(&estrBuffer);

	if (pInstance && pEntity)
	{
		estrPrintf(&estrBuffer, "%s - (%d)", queue_StateToStr(pEntity, pInstance), pInstance->uSecondsRemaining);
	}
	
	if (strlen(estrBuffer))
	{
		pchStatus = exprContextAllocScratchMemory(pContext, strlen(estrBuffer) + 1);
		memcpy(pchStatus, estrBuffer, strlen(estrBuffer) + 1);
	}

	estrDestroy(&estrBuffer);

	return NULL_TO_EMPTY(pchStatus);	
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetStatusText");
const char* exprGetQueueStatusText(ExprContext *pContext, const char* pchQueueName, U32 uiInstanceID)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerQueueInfo)
	{
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueueInstance *pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uiInstanceID, true);
		return gclQueue_GetStatusText(pContext, pEnt, pInstance);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyStatusText");
const char* exprGetReadyQueueStatusText(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchMapType)
{
	ZoneMapType eType = ZMTYPE_UNSPECIFIED;

	if(pchMapType && *pchMapType)
		eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);

	return gclQueue_GetStatusText(pContext, pEntity, Queue_GetReadyQueueOfType(pEntity, eType, NULL));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyTimeLeftPercent");
F32 exprGetReadyTimeLeftPercent(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchMapType)
{
	QueueDef *pQueueDef = NULL;
	PlayerQueueInstance* pInstance = Queue_GetReadyQueueOfType(pEntity, ZMTYPE_UNSPECIFIED, &pQueueDef);
	
	if (pInstance && pQueueDef)
	{
		return 
			(pInstance->uTimelimit > 0) ? 
			((F32)pInstance->uSecondsRemaining / (F32)pInstance->uTimelimit) :
			(pInstance->uSecondsRemaining);
	}
	return 0.f;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyQueueMemberList");
void exprGetReadyQueueMemberList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity )
{
	QueueDef *pQueueDef = NULL;
	PlayerQueueInstance* pInstance = Queue_GetReadyQueueOfType(pEntity, ZMTYPE_UNSPECIFIED, &pQueueDef);

	if (pQueueDef && pInstance)
	{
		gclGetQueueInstanceMemberList(	pGen, 
										pEntity, 
										pQueueDef->pchName, 
										pInstance->uiID, 
										pInstance->iGroupIndex, 
										false,
										true);
	}
	else
	{
		ui_GenSetManagedListSafe(pGen, NULL, QueueMemberDesc, true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyQueueHasAccepted");
bool exprGetReadyQueueHasAccepted(Entity *pEntity)
{
	PlayerQueueInstance* pInstance = Queue_GetReadyQueueOfType(pEntity, ZMTYPE_UNSPECIFIED, NULL);

	return pInstance ? (pInstance->eQueueState == PlayerQueueState_Accepted || 
						pInstance->eQueueState == PlayerQueueState_Countdown || 
						pInstance->eQueueState == PlayerQueueState_InMap || 
						pInstance->eQueueState == PlayerQueueState_WaitingForTeam) : false;
}

// returns true if the ready queue has the maximum number of players alloted for each group
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetReadyQueueIsAtMax");
bool exprGetReadyQueueIsAtMax(Entity *pEntity)
{
	QueueDef* pDef = NULL;
	PlayerQueueInstance* pInstance = Queue_GetReadyQueueOfType(pEntity, ZMTYPE_UNSPECIFIED, &pDef);

	if (pDef == NULL || pEntity == NULL)
		return false;

	FOR_EACH_IN_EARRAY(pDef->eaGroupDefs, QueueGroupDef, pGroupDef)
	{
		S32 iCurGroupSize = 0;

		FOR_EACH_IN_EARRAY(pInstance->eaMembers, PlayerQueueMember, pMember)
		{
			if (pMember->iGroupIndex == FOR_EACH_IDX(-, pGroupDef))
			{
				iCurGroupSize++; 	
			}
		}
		FOR_EACH_END
				
		if (iCurGroupSize < pGroupDef->iMax)
		{
			return false;
		}
	}
	FOR_EACH_END

	return true;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_CanJoinActiveMap");
bool exprCanJoinActiveMap(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);

	if (pEnt && pEnt->pPlayer && pQueueData && pQueueData->uPrivateInstance > 0)
	{
		const char* pchQueueName = pQueueData->pchPrivateQueue;
		U32 uInstanceID = pQueueData->uPrivateInstance;
		PlayerQueueInfo* pQueueInfo = pEnt->pPlayer->pPlayerQueueInfo;
		PlayerQueueInstance* pInstance = queue_FindPlayerQueueInstanceByID(pQueueInfo, pchQueueName, uInstanceID, true);

		if (pInstance)
		{
			QueueDef* pDef = queue_DefFromName(pchQueueName);
			PlayerQueueMap* pMap = queue_FindActiveMapForPrivatePlayerInstance(pInstance, pDef);
			if (pMap && pMap->iKey != pInstance->iOfferedMapKey)
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetCooldownTimeleft");
U32 exprQueueGetCooldownTimeleft(const char* pchQueueDefName)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueDef* pDef = queue_DefFromName(pchQueueDefName);
	if (pEnt && pEnt->pPlayer && pDef && pDef->pchCooldownDef)
	{
		QueueCooldownDef* pCooldownDef = queue_CooldownDefFromName(pDef->pchCooldownDef);
		if (pCooldownDef)
		{
			PlayerQueueCooldown* pPlayerCooldown = eaIndexedGetUsingString(&pEnt->pPlayer->eaQueueCooldowns, pCooldownDef->pchName);
			if (pPlayerCooldown)
			{
				U32 uCurrentTime = timeServerSecondsSince2000();
				U32 uCooldownExpireTime = pPlayerCooldown->uStartTime + pCooldownDef->uCooldownTime;
				if (uCurrentTime < uCooldownExpireTime)
				{
					return uCooldownExpireTime - uCurrentTime;
				}
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_SetVoteData","queue_SetConcedeData");
bool exprSetConcedeData(SA_PARAM_NN_VALID UIGen* pGen)
{
	if (s_pVoteData)
	{
		ui_GenSetPointer(pGen, &s_pVoteData, parse_QueueVoteDataUI);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_NextConcedeTimeRemaining");
U32 exprNextConcedeTimeRemaining(void)
{
	U32 uCurrentTime = timeSecondsSince2000();
	if (uCurrentTime >= s_uNextConcedeTime)
	{
		return 0;
	}
	return s_uNextConcedeTime - uCurrentTime;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_NextVoteKickTimeRemaining");
U32 exprNextVoteKickTimeRemaining(void)
{
	U32 uCurrentTime = timeSecondsSince2000();
	if (uCurrentTime >= s_uNextVoteKickTime)
	{
		return 0;
	}
	return s_uNextVoteKickTime - uCurrentTime;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetLeaverPenaltyTimeLeft");
U32 exprGetLeaverPenaltyTimeLeft(S32 eQueueCategory)
{
	Entity* pEnt = entActivePlayerPtr();
	PlayerQueuePenaltyData* pPenaltyData = SAFE_MEMBER3(pEnt, pPlayer, pPlayerQueueInfo, pPenaltyData);
	U32 uCurrentTime = timeServerSecondsSince2000();
	U32 uPenaltyEndTime = 0;
	
	if (pPenaltyData)
	{
		if (eQueueCategory >= 0)
		{
			QueuePenaltyCategoryData* pCategoryData = eaIndexedGetUsingInt(&pPenaltyData->eaCategories, eQueueCategory);
			if (pCategoryData)
			{
				uPenaltyEndTime = pCategoryData->uPenaltyEndTime;
			}
		}
		else
		{
			int i;
			for (i = eaSize(&pPenaltyData->eaCategories)-1; i >= 0; i--)
			{
				QueuePenaltyCategoryData* pCategoryData = pPenaltyData->eaCategories[i];
				U32 uCurrEndTime = pCategoryData->uPenaltyEndTime;
				
				// Find the penalty time that will end soonest
				if (!uPenaltyEndTime || uCurrEndTime < uPenaltyEndTime)
				{
					uPenaltyEndTime = uCurrEndTime;
				}
			}
		}
	}
	if (uCurrentTime >= uPenaltyEndTime)
	{
		return 0;
	}
	return uPenaltyEndTime - uCurrentTime;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetLeaverPenaltyCategoryList");
void exprGetLeaverPenaltyCategoryList(SA_PARAM_NN_VALID UIGen* pGen)
{
	Entity* pEnt = entActivePlayerPtr();
	QueuePenaltyDataUI*** peaData = ui_GenGetManagedListSafe(pGen, QueuePenaltyDataUI);
	PlayerQueuePenaltyData* pPenaltyData = SAFE_MEMBER3(pEnt, pPlayer, pPlayerQueueInfo, pPenaltyData);
	int iCount = 0;

	if (pPenaltyData)
	{
		U32 uCurrentTime = timeSecondsSince2000();
		int i;
		for (i = 0; i < eaSize(&pPenaltyData->eaCategories); i++)
		{
			QueuePenaltyCategoryData* pPenaltyCategoryData = pPenaltyData->eaCategories[i];
			QueuePenaltyDataUI* pData = eaGetStruct(peaData, parse_QueuePenaltyDataUI, iCount++);
			QueueCategoryData* pCategory = queue_GetCategoryData((QueueCategory)pPenaltyCategoryData->eCategory);
			U32 uPenaltyEndTime = pPenaltyCategoryData->uPenaltyEndTime;
			U32 uPenaltyTimeLeft = 0;

			if (uCurrentTime < uPenaltyEndTime)
			{
				uPenaltyTimeLeft = uPenaltyEndTime - uCurrentTime;
			}
			pData->pchCategoryDisplayName = pCategory ? TranslateDisplayMessage(pCategory->msgDisplayName) : NULL;
			pData->eCategory = pCategory->eCategory;
			pData->uPenaltyTimeLeft = uPenaltyTimeLeft;
		}
	}

	eaSetSizeStruct(peaData, parse_QueuePenaltyDataUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, QueuePenaltyDataUI, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetLeaverPenaltyDurationForCurrentMap");
U32 exprGetLeaverPenaltyDurationForCurrentMap(void)
{
	Entity* pEnt = entActivePlayerPtr();
	return SAFE_MEMBER3(pEnt, pPlayer, pPlayerQueueInfo, uLeaverPenaltyDuration);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Queue_ResetPrefs) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclQueue_ResetPrefs()
{
	if(s_pMatchPrefs)
		StructDestroy(parse_QueueMemberPrefs,(QueueMemberPrefs*)s_pMatchPrefs);

	s_pMatchPrefs = (NOCONST(QueueMemberPrefs)*) StructCreate(parse_QueueMemberPrefs);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Queue_PrefsRemoveGameType) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclQueue_PrefRemoveGameType(const char *pchPreferedGameType)
{
	S32 sGameType = StaticDefineIntGetInt(PVPGameTypeEnum,pchPreferedGameType);

	if(!s_pMatchPrefs)
		gclQueue_ResetPrefs();

	ea32FindAndRemove(&s_pMatchPrefs->ePreferredGameTypes,sGameType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Queue_PrefsAddGameType) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclQueue_PrefAddGameType(const char *pchPreferedGameType)
{
	S32 sGameType = StaticDefineIntGetInt(PVPGameTypeEnum,pchPreferedGameType);

	if(!s_pMatchPrefs)
		gclQueue_ResetPrefs();

	ea32PushUnique(&s_pMatchPrefs->ePreferredGameTypes,sGameType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Queue_SetPrefsGameTypes) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclQueue_SetPrefGameTypes(const char * pchPreferedGameTypes, int bOnlyIfNotAlreadySet)
{
	char **ppchGameTypes = NULL;
	int i;

	if(bOnlyIfNotAlreadySet && s_pMatchPrefs && ea32Size(&s_pMatchPrefs->ePreferredGameTypes) != 0)
		return;

	if(!s_pMatchPrefs)
		gclQueue_ResetPrefs();

	DivideString(pchPreferedGameTypes,",",&ppchGameTypes, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	ea32Clear(&s_pMatchPrefs->ePreferredGameTypes);

	for(i=0;i<eaSize(&ppchGameTypes);i++)
	{
		S32 sGameType = StaticDefineIntGetInt(PVPGameTypeEnum,ppchGameTypes[i]);

		if(sGameType != -1)
			ea32Push(&s_pMatchPrefs->ePreferredGameTypes,sGameType);
	}

	eaDestroyEx(&ppchGameTypes, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Queue_PrefsCheckGameType) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Queue);
int gclQueue_GetPrefsGameType(const char *pchPreferedGameType)
{
	S32 sGameType = StaticDefineIntGetInt(PVPGameTypeEnum,pchPreferedGameType);

	if(!s_pMatchPrefs)
		gclQueue_ResetPrefs();

	return ea32Find(&s_pMatchPrefs->ePreferredGameTypes,sGameType) != -1;
}

AUTO_COMMAND ACMD_NAME(Queue_JoinQueueWithPrefs) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Queue);
void gclQueue_JoinQueueWithPrefs(const char *pchQueueName)
{
	if(!s_pMatchPrefs)
		gclQueue_ResetPrefs();

	ServerCmd_Queue_JoinWithPrefs(pchQueueName,(QueueMemberPrefs*)s_pMatchPrefs);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Queue_JoinCurrentModelList);
void gclQueue_JoinCurrentModelList(UIGen *pGen)
{
	QueueDesc ***peaQueues = (QueueDesc ***)ui_GenGetManagedList(pGen, parse_QueueDesc);
	int i;

	if(peaQueues && eaSize(peaQueues))
	{
		for(i=0;i<eaSize(peaQueues);i++)
			ServerCmd_Queue_Join((*peaQueues)[i]->pchQueueName);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Queue_TeamJoinCurrentModelList);
void gclQueue_TeamJoinCurrentModelList(UIGen *pGen)
{
	QueueDesc ***peaQueues = (QueueDesc ***)ui_GenGetManagedList(pGen, parse_QueueDesc);
	int i;

	if(peaQueues && eaSize(peaQueues))
	{
		for(i=0;i<eaSize(peaQueues);i++)
			ServerCmd_Queue_TeamJoin((*peaQueues)[i]->pchQueueName);
	}
}


//------------------------------------------------------------------
// Queue Instantiation info. (When the player is involved with a queue instance/map)
//    The instantiation info includes the queueDef name, so we could get info from it if we wanted
//------------------------------------------------------------------


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_CanPlayerReturnToQueueMap");
bool exprCanPlayerReturnToQueueMap(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInstantiationInfo* pQueueInstantiationInfo = SAFE_MEMBER3(pEnt, pPlayer, pPlayerQueueInfo, pQueueInstantiationInfo);
	if (pQueueInstantiationInfo!=NULL)
	{
		if (pQueueInstantiationInfo->iMapKey && !pQueueInstantiationInfo->bOnCorrectMapPartition)
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_InstantiationGroupIndex");
int exprQueueInstantiationGroupIndex(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInstantiationInfo* pQueueInstantiationInfo = SAFE_MEMBER3(pEnt, pPlayer, pPlayerQueueInfo, pQueueInstantiationInfo);
	if (pQueueInstantiationInfo!=NULL)
	{
		return(pQueueInstantiationInfo->iGroupIndex);
	}
	return(-1);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_InstantiationGroupAffiliation");
const char *exprQueueInstantiationGroupAffiliation(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInstantiationInfo* pQueueInstantiationInfo = SAFE_MEMBER3(pEnt, pPlayer, pPlayerQueueInfo, pQueueInstantiationInfo);
	if (pQueueInstantiationInfo!=NULL)
	{
		QueueDef* pDef = queue_DefFromName(pQueueInstantiationInfo->pchQueueDef);
		if (pDef!=NULL)
		{
			int iIndex = pQueueInstantiationInfo->iGroupIndex;
			// The QueueDef Groups should be 1-to-1 with the match groups.

			if (iIndex>=0 && iIndex<eaSize(&(pDef->eaGroupDefs)))
			{
				return(pDef->eaGroupDefs[iIndex]->pchAffiliation);
			}
		}
	}
	return("");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_InstantiationDifficulty");
int exprQueueInstantiationDifficulty(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInstantiationInfo* pQueueInstantiationInfo = SAFE_MEMBER3(pEnt, pPlayer, pPlayerQueueInfo, pQueueInstantiationInfo);
	if (pQueueInstantiationInfo!=NULL)
	{
		QueueDef* pDef = queue_DefFromName(pQueueInstantiationInfo->pchQueueDef);
		if (pDef!=NULL)
		{
			return(pDef->eDifficulty);
		}
	}
	return(0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_InstantiationCategory");
int exprQueueInstantiationCategory(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInstantiationInfo* pQueueInstantiationInfo = SAFE_MEMBER3(pEnt, pPlayer, pPlayerQueueInfo, pQueueInstantiationInfo);
	if (pQueueInstantiationInfo!=NULL)
	{
		QueueDef* pDef = queue_DefFromName(pQueueInstantiationInfo->pchQueueDef);
		if (pDef!=NULL)
		{
			return(pDef->eCategory);
		}
	}
	return(0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_InstantiationPvPGameType");
int exprQueueInstantiationPvpGameType(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInstantiationInfo* pQueueInstantiationInfo = SAFE_MEMBER3(pEnt, pPlayer, pPlayerQueueInfo, pQueueInstantiationInfo);
	if (pQueueInstantiationInfo!=NULL)
	{
		QueueDef* pQueueDef = queue_DefFromName(pQueueInstantiationInfo->pchQueueDef);
		if (pQueueDef!=NULL)
		{
			return(pQueueDef->MapRules.QGameRules.publicRules.eGameType!=kPVPGameType_None);
		}
	}
	return(kPVPGameType_None);
}


//------------------------------------------------------------------
// COMMANDS
//------------------------------------------------------------------

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void QueueUI_ShowQueuedInstance(ZoneMapType eMapType)
{
	UIGen *pQueueUIGen;
	const char* pchGenName = g_QueueConfig.pchQueueGen;

	if (g_QueueConfig.pchPvEQueueGen && eMapType == ZMTYPE_QUEUED_PVE)
	{
		pchGenName = g_QueueConfig.pchPvEQueueGen;
	}
	if (pQueueUIGen = ui_GenFind(pchGenName, kUIGenTypeNone))
	{
		ui_GenSendMessage(pQueueUIGen, "Toggle");
		if (!g_QueueConfig.bProvideIndividualMapInfo)
		{	
			ui_GenSendMessage(pQueueUIGen, "InstancesTab");
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void QueueUI_ReceiveVoteData(U32 uEntID, U32 uTargetEntID, S32 eVoteType, S32 iVoteCount, S32 iGroupSize, bool bVoted)
{
	if (uEntID > 0)
	{
		F32 fVoteRatio = 0.0f;
		S32 iVotesRequired = 0;

		if (!s_pVoteData)
		{
			Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uEntID);
			Entity* pTargetEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uTargetEntID);
			s_pVoteData = StructCreate(parse_QueueVoteDataUI);
			s_pVoteData->uInitiatorID = uEntID;
			s_pVoteData->uTargetPlayerID = uTargetEntID;
			s_pVoteData->eType = (QueueVoteType)eVoteType;
			if (pEnt)
			{
				s_pVoteData->pchInitiatorName = StructAllocString(entGetLocalName(pEnt));
			}
			if (pTargetEnt)
			{
				s_pVoteData->pchTargetPlayerName = StructAllocString(entGetLocalName(pTargetEnt));
			}
		}
		if (!s_pVoteData->bAlreadyVoted && bVoted)
		{
			s_pVoteData->bAlreadyVoted = bVoted;
		}
		s_pVoteData->iVoteCount = iVoteCount;

		switch (s_pVoteData->eType)
		{
			xcase kQueueVoteType_Concede:
			{
				fVoteRatio = g_QueueConfig.fConcedeVoteRatio;
				iVotesRequired = ceilf(iGroupSize*fVoteRatio);
			}
			xcase kQueueVoteType_VoteKick:
			{
				fVoteRatio = g_QueueConfig.fKickVoteRatio;
				iVotesRequired = ceilf((iGroupSize-1)*fVoteRatio);
			}
		}
		s_pVoteData->iVotesRequired = iVotesRequired;
		s_pVoteData->iGroupSize = iGroupSize;
	}
	else
	{
		switch (s_pVoteData->eType)
		{
			xcase kQueueVoteType_Concede:
			{
				s_uNextConcedeTime = timeSecondsSince2000() + g_QueueConfig.uConcedeRetryTime;
			}
			xcase kQueueVoteType_VoteKick:
			{
				s_uNextVoteKickTime = timeSecondsSince2000() + g_QueueConfig.uVoteKickRetryTime;
			}
		}
		StructDestroySafe(parse_QueueVoteDataUI, &s_pVoteData);
	}
}

static int SortPlayerCounts(const UIGenVarTypeGlob **ppLeft, const UIGenVarTypeGlob **ppRight)
{
	return ppLeft[0]->iInt - ppRight[0]->iInt;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_GetMaxPlayerCounts");
void queueExpr_GetDifficulties(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char *pchMapType)
{
	UIGenVarTypeGlob ***peaGlob = ui_GenGetManagedListSafe(pGen, UIGenVarTypeGlob);
	PlayerQueueInfo *pPlayerQueueInfo = (pEntity && pEntity->pPlayer) ? pEntity->pPlayer->pPlayerQueueInfo : NULL;
	ZoneMapType eType = StaticDefineIntGetInt(ZoneMapTypeEnum, pchMapType);
	S32 i, j, iSize = 0;

	if (pPlayerQueueInfo && eType >= ZMTYPE_UNSPECIFIED && eaSize(&pPlayerQueueInfo->eaQueues))
	{
		for (i = 0; i < eaSize(&pPlayerQueueInfo->eaQueues); i++)
		{
			PlayerQueue* pQueue = pPlayerQueueInfo->eaQueues[i];
			QueueDef* pDef = GET_REF(pQueue->hDef);
			S32 iCount = pDef ? queue_QueueGetMaxPlayers(pDef, false) : -1;
			if (pDef && iCount > 0 && (eType == ZMTYPE_UNSPECIFIED || eType == pDef->MapSettings.eMapType))
			{
				for (j = iSize - 1; j >= 0; j--)
					if ((*peaGlob)[j]->iInt == iCount)
						break;
				if (j < 0)
				{
					UIGenVarTypeGlob *pGlob = eaGetStruct(peaGlob, parse_UIGenVarTypeGlob, iSize++);
					pGlob->iInt = iCount;
				}
			}
		}
	}

	eaSetSizeStruct(peaGlob, parse_UIGenVarTypeGlob, iSize);
	eaQSort(*peaGlob, SortPlayerCounts);
	ui_GenSetManagedListSafe(pGen, peaGlob, UIGenVarTypeGlob, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("queue_PrivateQueueHasValidMapLevel");
bool queueExpr_PrivateQueueHasValidMapLevel(void)
{
	Entity* pEnt = entActivePlayerPtr();
	QueueInfoUI* pQueueData = NULL;
	PlayerQueueInfo *pQueueInfo = SAFE_MEMBER2(pEnt, pPlayer, pPlayerQueueInfo);
	gclQueue_UpdateQueueInfo(pEnt, &pQueueData);
	if (pQueueInfo && pQueueData && pQueueData->uPrivateInstance)
	{
		PlayerQueue *pPrivateQueue = eaIndexedGetUsingString(&pQueueInfo->eaQueues, pQueueData->pchPrivateQueue);
		if (pPrivateQueue)
		{
			PlayerQueueInstance *pInstance = eaIndexedGetUsingInt(&pPrivateQueue->eaInstances, pQueueData->uPrivateInstance);
			if (pInstance)
			{
				return queue_PrivateQueueLevelCheck(pEnt, pInstance);
			}
		}
	}
	return false;
}

#include "QueueUI_c_ast.c"
