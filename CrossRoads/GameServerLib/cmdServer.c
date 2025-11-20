/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "cmdServer.h"
#include "CombatDebug.h"
#include "GameServerLib.h"
#include "ExpressionDebug.h"
#include "GameAccountDataCommon.h"
#include "gslControlScheme.h"
#include "gslTransactions.h"
#include "gslSendToClient.h"
#include "gslEntity.h"
#include "gslEncounter.h"
#include "aiLib.h"
#include "character_combat.h"
#include "Powers.h"
#include "PowersEnums_h_ast.h"
#include "PowerTree.h"
#include "Character.h"
#include "gslCritter.h"
#include "gslPartition.h"
#include "ControllerScriptingSupport.h"
#include "ControlScheme.h"
#include "gslOldEncounter.h"
#include "CharacterClass.h"
#include "qSortG.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "RegionRules.h"
#include "StringCache.h"
#include "GameStringFormat.h"
#include "Leaderboard.h"
#include "Leaderboard_h_ast.h"

#include "costumeCommon.h"
#include "AutoGen/costumeCommon_h_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

// For nemesis testing; can remove once Nemesis editor is up
#include "AutoGen/Entity_h_ast.h"

// Static variables for each of the nemesis lists
extern NemesisPowerSetList g_NemesisPowerSetList;
extern NemesisMinionPowerSetList g_NemesisMinionPowerSetList;
extern NemesisMinionCostumeSetList g_NemesisMinionCostumeSetList;

//Implements the game-specific parts of the gslCommandParse.h header file

AUTO_CMD_INT(g_bDebugPowerTree,Debug_PowerTree_Server);

AUTO_CMD_INT(g_bDebugStats,Debug_PowerStats);


// Replace any special strings (like $target) with the right thing
char *gslExternExpandCommandVar(const char *variable,int *cmdlen,Entity *client)
{
	return 0;
}

// Spawn a test critter
AUTO_COMMAND ACMD_NAME("TestCritter", "Spawn", "SpawnCritter");
void TestCritter(Entity *clientEntity, ACMD_NAMELIST("CritterDef", REFDICTIONARY) char* testStr)
{
	Entity * e;
	e = critter_Create(testStr, NULL, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(clientEntity), NULL, 1, 1, 0, 0, 0, 0, 0, 0, 0, NULL, NULL);
	if(e)
	{
		Vec3 vPos;
		entGetPos(clientEntity, vPos);
		entSetPos(e, vPos, 1, __FUNCTION__);
		entSetRot(e, unitquat, 1, __FUNCTION__);

		aiSetupTestCritter(e);
	}
	else
		gslSendPrintf(clientEntity, "Unable to create critter named \"%s\"", testStr);
}

AUTO_COMMAND ACMD_NAME("CritterSearch", "TestCritterSearch");
void TestCritterSearch(Entity *clientEntity, ACMD_SENTENCE substring )
{
	const char** eaCritterNames = NULL;
	RefDictIterator iter;
	CritterDef *pCritter = NULL;
	RefSystem_InitRefDictIterator("CritterDef", &iter);
	while (pCritter = RefSystem_GetNextReferentFromIterator(&iter))
	{
		bool bFound = true;

		//meaningless call to reset strTok
		char* pcSubStr = strTokWithSpacesAndPunctuation(NULL, NULL);

		while (pcSubStr = strTokWithSpacesAndPunctuation(substring, " "))
		{
			if (!strstri(pCritter->pchName, pcSubStr))
			{
				bFound = false;
				break;
			}
		}
		if (bFound)
			eaPush(&eaCritterNames, pCritter->pchName);
	}
	gslSendPrintf(clientEntity, "\n\n------------------------");
	gslSendPrintf(clientEntity, "%d Critter name matches found\n", eaSize(&eaCritterNames));
	gslSendPrintf(clientEntity, "------------------------\n\n");
	eaQSortG(eaCritterNames, strCmp);
	FOR_EACH_IN_EARRAY_FORWARDS(eaCritterNames, const char, pcCritterName)
		gslSendPrintf(clientEntity, "%s\n", pcCritterName);
	FOR_EACH_END;
	gslSendPrintf(clientEntity, "\n");
	eaDestroy(&eaCritterNames);
}

AUTO_COMMAND;
void TestCritterPos(Entity *clientEntity, ACMD_NAMELIST("CritterDef", REFDICTIONARY) char* testStr, Vec3 pos)
{
	Entity *e;
	e = critter_Create(testStr, NULL, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(clientEntity), NULL, 1, 1, 0, 0, 0, 0, 0, 0, 0, NULL, NULL);

	if(e)
	{
		entSetPos(e, pos, 1, __FUNCTION__);
		entSetRot(e, unitquat, 1, __FUNCTION__);

		aiSetupTestCritter(e);
	}
	else
	{
		gslSendPrintf(clientEntity, "Unable to create critter named \"%s\"", testStr);
	}
}

AUTO_COMMAND;
void TestCritterGrid(Entity *clientEntity, ACMD_NAMELIST("CritterDef", REFDICTIONARY) char* testStr, int iSide, F32 fDistBetween)
{
	int iHalfSide = iSide / 2;
	Vec3 vCenterPos;
	int x, z;
	entGetPos(clientEntity, vCenterPos);

	for (x=-iHalfSide; x<=iHalfSide; ++x)
	{
		for (z=-iHalfSide; z<=iHalfSide; ++z)
		{
			Entity *e;
			Vec3 pos;
			setVec3(pos, fDistBetween * x, 0.0f, fDistBetween * z);
			addVec3(vCenterPos, pos, pos);


			e = critter_Create(testStr, NULL, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(clientEntity), NULL, 1, 1, 0, 0, 0, 0, 0, 0, 0, NULL, NULL);

			if(e)
			{
				entSetPos(e, pos, 1, __FUNCTION__);
				entSetRot(e, unitquat, 1, __FUNCTION__);

				aiSetupTestCritter(e);
			}
			else
			{
				gslSendPrintf(clientEntity, "Unable to create critter named \"%s\"", testStr);
			}
		}
	}
}

AUTO_COMMAND;
void TestCritterFSM(Entity *clientEntity, ACMD_NAMELIST("CritterDef", REFDICTIONARY) char *critter, ACMD_NAMELIST("FSM", REFDICTIONARY) char *fsm)
{
	Entity *e;
	Vec3 pos;
	e = critter_Create(critter, NULL, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(clientEntity), fsm, 1, 1, 0, 0, 0, 0, 0, 0, 0, NULL, NULL);

	if(e)
	{
		entGetPos(clientEntity, pos);
		entSetPos(e, pos, 1, __FUNCTION__);
		entSetRot(e, unitquat, 1, __FUNCTION__);

		aiSetupTestCritter(e);
	}
	else
	{
		gslSendPrintf(clientEntity, "Unable to create critter named \"%s\"", critter);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void TestCritterLevel(Entity *clientEntity, int level, ACMD_NAMELIST("CritterDef", REFDICTIONARY) char* testStr)
{
	Entity * e;
	e = critter_Create( testStr, NULL, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(clientEntity), NULL, level, 1, 0, 0, 0, 0, 0, 0, 0, NULL, NULL);
	if(e)
	{
		Vec3 vPos;
		entGetPos(clientEntity, vPos);
		entSetPos(e, vPos, 1, __FUNCTION__);
		entSetRot(e, unitquat, 1, __FUNCTION__);

		aiSetupTestCritter(e);
	}
	else
		gslSendPrintf(clientEntity, "Unable to create critter named \"%s\"", testStr);
}

static EntityRef cbCheckRef;

void ContinuousBuilderTestFFCritterCallback(TimedCallback* callback, F32 timeSinceLastCallback, UserData* userdata)
{
	Entity* checkBE = entFromEntityRefAnyPartition(cbCheckRef);

	if(!checkBE || !entIsAlive(checkBE))
		ControllerScript_Succeeded();
}

AUTO_COMMAND;
void ContinuousBuilderTestBurialCavesCritter()
{
	Entity * e = NULL;
	int i;
	int iPartitionIdx = -1;

	// Find a valid partition
	for(i=partition_GetCurNumPartitionsCeiling()-1; i>=0; --i) {
		if (partition_ExistsByIdx(i)) {
			iPartitionIdx = i;
			break;
		}
	}
	if (i >= 0) {
		e = critter_Create("Officer", NULL, GLOBALTYPE_ENTITYCRITTER, iPartitionIdx, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, NULL, NULL);
	}
	if(e)
	{
		Vec3 vPos = {70, 210, 24};
		entSetPos(e, vPos, 1, __FUNCTION__);
		entSetRot(e, unitquat, 1, __FUNCTION__);
		cbCheckRef = entGetRef(e);
		TimedCallback_Add(ContinuousBuilderTestFFCritterCallback, NULL, 5);
		aiDisableSleep(true);
		encounter_disableSleeping(true);
	}
	else
		ControllerScript_Failed("Unable to create critter named \"Officer\" for combat test");
}

// Saves your entity to disk
AUTO_COMMAND;
void EntSave(Entity *clientEntity)
{
	gslSendEntityToDatabase(clientEntity,true);
}

AUTO_COMMAND ACMD_PRIVATE;
void ExamplePrivateCommand(ACMD_SENTENCE object)
{
	printf("%s\n",object);
}

AUTO_COMMAND ACMD_NAME(ed) ACMD_NAME(exprDebug);
void expressionDebug(Entity* e, ACMD_SENTENCE cmd)
{
	char* resultStr = exprDebug(cmd, NULL, NULL);

	gslSendPrintf(e, "%s", resultStr);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(7);
void combatDebugEntServer(Entity *e, EntityRef erTarget)
{
	PlayerDebug *debug = entGetPlayerDebug(e, true);
	if(debug)
	{
		debug->erCombatDebug = erTarget;
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}

	g_erCombatDebugEntRef = erTarget;
 }

// Cause a power to be activated at your location
AUTO_COMMAND ACMD_NAME(power_exec_svr_loc);
void locApplyPower(Entity *e, const char *powerName)
{
	Vec3 vecSrc;
	PowerDef *ppowdef = powerdef_Find(powerName);
	if(ppowdef)
	{
		entGetPos(e, vecSrc);
		location_ApplyPowerDef(vecSrc,entGetPartitionIdx(e),ppowdef,0,vecSrc,NULL,NULL,NULL,1,0);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void power_requestinfo(Entity *e, U32 uiPowerID)
{
	if(e && e->pChar)
	{
		Power *ppow = character_FindPowerByIDComplete(e->pChar,uiPowerID);

		if(ppow)
		{
			ClientCmd_PowerSetRechargeClient(e,uiPowerID,ppow->fTimeRecharge);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void showServerFPS(Entity* playerEnt, bool show)
{
	PlayerDebug* debug = entGetPlayerDebug(playerEnt, show);

	if (debug)
	{
		debug->showServerFPS = show;
		entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);
	}
}

// "ReturnToContact" for a crime or scheme.  Harmless if the mission isn't complete.
// JAMES TODO: If noRewardHack is 1, gives no rewards for this mission.
/*
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
bool TurnInCrime(Entity* playerEnt, char* missionName, bool noRewardHack)
{
	if(playerEnt && missionName && playerEnt->pPlayer)
	{
		MissionInfo* missionInfo = mission_GetInfoFromPlayer(playerEnt);
		MissionDef* missionDef = missiondef_DefFromRefString(missionName);
		Mission* mission = NULL;

		if(!playerEnt->pPlayer->ePlayerKind == PlayerKind_Villain)
		{
			Errorf("TurnInCrime %s: Only villains can do this!", missionName);
			return false;
		}

		if (missionDef && missionInfo && (mission = mission_GetMissionFromDef(missionInfo, missionDef)))
		{
			contact_SingleMissionRewardInteractBegin(playerEnt, "VillainMissions", missionName, noRewardHack);
		}
	}

	return false;
}
*/

// Prints information about the current map
AUTO_COMMAND ACMD_SERVERCMD;
void PrintMapInfo(Entity* playerEnt)
{
	int iPartitionIdx = entGetPartitionIdx(playerEnt);
	int instanceNumber = partition_PublicInstanceIndexFromIdx(iPartitionIdx);
	const char *mapName = gGSLState.gameServerDescription.baseMapDescription.mapDescription;
	gslSendPrintf(playerEnt, "%s - %d", mapName, instanceNumber);
}


// Send the client a list of all player power trees.
// If necessary this can be filtered even more later.
AUTO_COMMAND ACMD_NAME("PowerTree_SendListToClient") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_HIDE;
void powertree_SendListToClient(Entity *pEnt, bool bOnlyIfCanBuy)
{
	RefDictIterator iter;
	PowerTreeDef *pTree = NULL;

	RefSystem_InitRefDictIterator("PowerTreeDef", &iter);
	while (pTree = RefSystem_GetNextReferentFromIterator(&iter))
	{
		CharacterClass *pClass = GET_REF(pTree->hClass);
		if ((!pClass || pClass->bPlayerClass) && (!bOnlyIfCanBuy || character_CanBuyPowerTree(entGetPartitionIdx(pEnt), pEnt->pChar, pTree)))
			ClientCmd_gclGenAddPowerTreeRef(pEnt, pTree->pchName);
	}
}

/*AUTO_COMMAND ACMD_NAME("PetStore_SendListToClient") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(PetStore);
void petStore_SendListToClient(Entity *pEnt)
{
	RefDictIterator iter;
	PetDef *pPet = NULL;
	RefSystem_InitRefDictIterator("PetDef", &iter);
	while (pPet = RefSystem_GetNextReferentFromIterator(&iter))
	{
		//ClientCmd_gclGenAddPetRef(pEnt, pPet->pchPetName);
	}
	}*/


void gslTrimPersistedPositions(PlayerUI *pUI, U32 uiTime)
{
	const S32 iMaxRememberedPositions = 100;
	while (eaSize(&pUI->eaStoredPositions) > iMaxRememberedPositions)
	{
		S32 i;
		S32 iMinIndex = 0;
		U32 uiMinTime = uiTime + 1;
		for (i = 0; i < eaSize(&pUI->eaStoredPositions); i++)
		{
			UIPersistedPosition *pStored = pUI->eaStoredPositions[i];
			if (pStored->uiTime < uiMinTime)
			{
				iMinIndex = i;
				uiMinTime = pStored->uiTime;
			}
		}
		StructDestroy(parse_UIPersistedPosition, eaRemove(&pUI->eaStoredPositions, iMinIndex));
	}
}

void gslTrimPersistedLists(PlayerUI *pUI, U32 uiTime)
{
	const S32 iMaxRememberedLists = 100;
	while (eaSize(&pUI->eaStoredLists) > iMaxRememberedLists)
	{
		S32 i;
		S32 iMinIndex = 0;
		U32 uiMinTime = uiTime + 1;
		for (i = 0; i < eaSize(&pUI->eaStoredLists); i++)
		{
			UIPersistedList *pStored = pUI->eaStoredLists[i];
			if (pStored->uiTime < uiMinTime)
			{
				iMinIndex = i;
				uiMinTime = pStored->uiTime;
			}
		}
		StructDestroy(parse_UIPersistedList, eaRemove(&pUI->eaStoredLists, iMinIndex));
	}
}

void gslTrimPersistedWindows(PlayerUI *pUI, U32 uiTime)
{
	PlayerLooseUI *pLooseUI = pUI->pLooseUI;
	const S32 c_iStoredValueMax = 50;
	// old code had some of these that were NULL, this check might not be need but is here just in case
	if(pLooseUI)
	{
		while (eaSize(&pLooseUI->eaPersistedWindows) > c_iStoredValueMax)
		{
			S32 i;
			S32 iMinIndex = 0;
			U32 uiMinTime = uiTime + 1;
			for (i = 0; i < eaSize(&pLooseUI->eaPersistedWindows); i++)
			{
				UIPersistedWindow *pWindows = pLooseUI->eaPersistedWindows[i];
				if (pWindows->uiTime < uiMinTime)
				{
					iMinIndex = i;
					uiMinTime = pWindows->uiTime;
				}
			}
			StructDestroy(parse_UIPersistedWindow, eaRemove(&pLooseUI->eaPersistedWindows, iMinIndex));
		}
	}
	else
	{
		Errorf("NULL pLooseUI in gslTrimPersistedWindows, if this error happens alot add more details to it.");
	}
}

void gslTrimPlayerUIPairs(PlayerUI *pUI, U32 uiTime)
{
	const S32 c_iStoredValueMax = 50;
	while (eaSize(&pUI->eaPairs) > c_iStoredValueMax)
	{
		S32 i;
		S32 iMinIndex = 0;
		U32 uiMinTime = uiTime + 1;
		for (i = 0; i < eaSize(&pUI->eaPairs); i++)
		{
			PlayerUIPair *pPair = pUI->eaPairs[i];
			if (pPair->uiTime < uiMinTime)
			{
				iMinIndex = i;
				uiMinTime = pPair->uiTime;
			}
		}
		StructDestroy(parse_PlayerUIPair, eaRemove(&pUI->eaPairs, iMinIndex));
	}
}

static void gslFixPlayerUI(Entity* pEnt)
{
	PlayerUI* pUI = SAFE_MEMBER2(pEnt, pPlayer, pUI);

	// Fixup control schemes
	if (pUI && pUI->pSchemes)
	{
		entity_FixupControlSchemes(pEnt);
		if (!Entity_IsValidControlSchemeForCurrentRegionEx(pEnt, pUI->pSchemes->pchCurrent, NULL))
		{
			entity_SetValidControlSchemeForRegion(pEnt, NULL);
		}
		else
		{
			entity_UpdateForCurrentControlScheme(pEnt);
		}
	}

	// Fixup key binds
	gslKeyBinds_Fixup(pEnt);
}

// Update UI position/size data.
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslCmdUpdatePlayerUI(Entity *pEnt, PlayerUI *pNewUI)
{
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	if (pUI && pNewUI)
	{
		U32 uiTime = timeSecondsSince2000();
		eaClearStruct(&pUI->eaStoredPositions, parse_UIPersistedPosition);
		StructCopyFields(parse_PlayerUI, pNewUI, pUI, 0, TOK_USEROPTIONBIT_2);
		gslTrimPersistedPositions(pUI, uiTime);
		gslTrimPersistedLists(pUI, uiTime);
		gslTrimPersistedWindows(pUI, uiTime);
		gslTrimPlayerUIPairs(pUI, uiTime);
		gslFixPlayerUI(pEnt);
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		ClientCmd_ui_GenLayersReset(pEnt);
	}
}

// Update UI position/size data.
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslCmdUpdateStoredUIPosition(Entity *pEnt, UIPersistedPosition *pPosition)
{
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	if (pUI && pPosition && pPosition->pchName && *pPosition->pchName)
	{
		UIPersistedPosition *pStored = eaIndexedGetUsingString(&pUI->eaStoredPositions, pPosition->pchName);
		U32 uiTime = timeSecondsSince2000();

		if (pStored)
		{
			// If only priority has changed, don't update the time. This
			// halves the amount of data that needs to be saved during normal
			// play, when the player isn't going into rearrange mode and is just
			// changing window priority.
			if (nearf(pPosition->fPercentX, pStored->fPercentX)
				&& nearf(pPosition->fPercentY, pStored->fPercentY)
				&& nearf(pPosition->iX, pStored->iX)
				&& nearf(pPosition->iY, pStored->iY)
				&& nearf(pPosition->fWidth, pStored->fWidth)
				&& nearf(pPosition->fHeight, pStored->fHeight)
				&& pPosition->eOffsetFrom == pStored->eOffsetFrom)
				uiTime = pStored->uiTime;

			StructCopyAll(parse_UIPersistedPosition, pPosition, pStored);
		}
		else
		{
			pStored = StructClone(parse_UIPersistedPosition, pPosition);
			eaPush(&pUI->eaStoredPositions, pStored);
		}
		pStored->uiTime = uiTime;
		gslTrimPersistedPositions(pUI, timeSecondsSince2000());

		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// Update UI position/size data.
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslCmdUpdateStoredUIList(Entity *pEnt, UIPersistedList *pList)
{
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	const S32 iMaxRememberedLists = 100;
	if (pUI && pList && pList->pchName && *pList->pchName)
	{
		UIPersistedList *pStored = eaIndexedGetUsingString(&pUI->eaStoredLists, pList->pchName);
		U32 uiTime = timeSecondsSince2000();
		if (pStored)
			StructCopyAll(parse_UIPersistedList, pList, pStored);
		else
		{
			pStored = StructClone(parse_UIPersistedList, pList);
			eaPush(&pUI->eaStoredLists, pStored);
		}

		pStored->uiTime = uiTime;

		gslTrimPersistedLists(pUI, timeSecondsSince2000());
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// Forget all saved UI positions/sizes
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("UIForgetPositions");
void gslCmdForgetStoredUIPositions(Entity *pEnt)
{
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	if (pUI)
	{
		eaClearStruct(&pUI->eaStoredPositions, parse_UIPersistedPosition);
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// Forget all saved UI positions/sizes
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslCmdForgetStoredUIPosition(Entity *pEnt, const char *pchName)
{
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	if (pUI)
	{
		S32 i = eaIndexedFindUsingString(&pUI->eaStoredPositions, pchName);
		if (i >= 0)
		{
			UIPersistedPosition *pPosition = pUI->eaStoredPositions[i];
			eaRemove(&pUI->eaStoredPositions, i);
			StructDestroySafe(parse_UIPersistedPosition, &pPosition);
		}
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void AutoLoot(Entity *pEnt, bool bEnabled)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->bEnableAutoLoot != (U32)!!bEnabled)
	{
		pEnt->pPlayer->bEnableAutoLoot = !!bEnabled;
	}
}

// AutoDescDetailTooltip <detail>: Sets the autodescription detail on tooltips
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void AutoDescDetailTooltip(Entity *pEnt, ACMD_NAMELIST(AutoDescDetailEnum, STATICDEFINE) const char *detail)
{
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	if (pUI)
	{
		AutoDescDetail eDetail = StaticDefineIntGetInt(AutoDescDetailEnum,detail);
		pUI->ePowerTooltipDetail = CLAMP(eDetail,kAutoDescDetail_Minimum,kAutoDescDetail_Maximum);
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// AutoDescDetailInspect <detail>: Sets the autodescription detail on inspect
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void AutoDescDetailInspect(Entity *pEnt, ACMD_NAMELIST(AutoDescDetailEnum, STATICDEFINE) const char *detail)
{
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	if (pUI)
	{
		AutoDescDetail eDetail = StaticDefineIntGetInt(AutoDescDetailEnum,detail);
		pUI->ePowerInspectDetail = CLAMP(eDetail,kAutoDescDetail_Minimum,kAutoDescDetail_Maximum);
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

static EntityKeyBinds* gslKeyBinds_GetEntBindsForProfile(EntityKeyBinds*** peaBinds,
														 const char* pchProfile,
														 bool bCreateIfNotFound)
{
	S32 i;
	for (i = eaSize(peaBinds)-1; i >= 0; i--)
	{
		EntityKeyBinds* pEntBinds = (*peaBinds)[i];
		if (stricmp(pEntBinds->pchProfile, pchProfile)==0)
		{
			return pEntBinds;
		}
	}
	if (bCreateIfNotFound)
	{
		EntityKeyBinds* pEntBinds = StructCreate(parse_EntityKeyBinds);
		pEntBinds->pchProfile = StructAllocString(pchProfile);
		eaPush(peaBinds, pEntBinds);
		return pEntBinds;
	}
	return NULL;
}

// If keybinds are stored per profile, then the profile is only valid if it's loaded by a control scheme
static bool gslKeyBinds_IsProfileValid(Entity* pEnt, const char* pchProfile)
{
	if (!gConf.bStoreKeybindsPerProfile)
	{
		return !pchProfile;
	}
	else
	{
		ControlSchemes* pSchemes = SAFE_MEMBER3(pEnt, pPlayer, pUI, pSchemes);
		if (pSchemes)
		{
			S32 i;
			for (i = eaSize(&pSchemes->eaSchemes)-1; i >= 0; i--)
			{
				ControlScheme* pScheme = pSchemes->eaSchemes[i];
				if (stricmp(pScheme->pchKeyProfileToLoad, pchProfile)==0)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void gslKeyBinds_Fixup(Entity* pEnt)
{
	PlayerUI* pUI = SAFE_MEMBER2(pEnt, pPlayer, pUI);
	if (pUI && pUI->pSchemes)
	{
		EntityKeyBinds** eaFixupBinds = NULL;
		S32 i, j, k, c;
		for (i = eaSize(&pUI->eaBindProfiles)-1; i >= 0; i--)
		{
			EntityKeyBinds* pBinds = pUI->eaBindProfiles[i];

			if (!pBinds->pchProfile && gConf.bStoreKeybindsPerProfile)
			{
				for (j = 0; j < eaSize(&pBinds->eaBinds); j++)
				{
					EntityKeyBind* pBind = pBinds->eaBinds[j];
					for (k = eaSize(&pUI->pSchemes->eaSchemeRegions)-1; k >= 0; k--)
					{
						ControlSchemeRegion* pSchemeRegion = pUI->pSchemes->eaSchemeRegions[k];
						const char* pchProfile = NULL;

						if (pBind->eSchemeRegions && (pSchemeRegion->eType & pBind->eSchemeRegions)==0)
							continue;

						if (!pSchemeRegion->pchScheme)
							continue;
						
						for (c = eaSize(&pUI->pSchemes->eaSchemes)-1; c >= 0; c--)
						{
							ControlScheme* pScheme = pUI->pSchemes->eaSchemes[c];
							if (stricmp(pScheme->pchName, pSchemeRegion->pchScheme)==0)
							{
								pchProfile = pScheme->pchKeyProfileToLoad;
								break;
							}
						}

						if (pchProfile)
						{
							EntityKeyBind* pCopyBind;
							EntityKeyBinds* pFixupBinds;
							pFixupBinds = gslKeyBinds_GetEntBindsForProfile(&eaFixupBinds, pchProfile, true);
							pCopyBind = StructClone(parse_EntityKeyBind, pBind);
							pCopyBind->eSchemeRegions = 0;
							eaPush(&pFixupBinds->eaBinds, pCopyBind);
						}
					}
				}
				StructDestroy(parse_EntityKeyBinds, eaRemove(&pUI->eaBindProfiles, i));
			}
			else if (pBinds->pchProfile && !gslKeyBinds_IsProfileValid(pEnt, pBinds->pchProfile))
			{
				StructDestroy(parse_EntityKeyBinds, eaRemove(&pUI->eaBindProfiles, i));
			}
		}
		for (i = eaSize(&eaFixupBinds)-1; i >= 0; i--)
		{
			for (j = eaSize(&pUI->eaBindProfiles)-1; j >= 0; j--)
			{
				if (stricmp(pUI->eaBindProfiles[j]->pchProfile, eaFixupBinds[i]->pchProfile)==0)
					break;
			}
			if (j < 0)
			{
				eaPush(&pUI->eaBindProfiles, eaRemove(&eaFixupBinds, i));
			}
		}
		eaDestroyStruct(&eaFixupBinds, parse_EntityKeyBinds);
	}
}

static void gslKeyBinds_SetBindsForProfile(Entity* pEnt, EntityKeyBinds *pBinds, const char* pchProfile)
{
	const S32 c_iStoredBindsMax = 200;
	PlayerUI* pUI = SAFE_MEMBER2(pEnt, pPlayer, pUI);
	if (pUI)
	{
		EntityKeyBinds* pEntBinds;
		pEntBinds = gslKeyBinds_GetEntBindsForProfile(&pUI->eaBindProfiles, pchProfile, true);
	
		while (eaSize(&pBinds->eaBinds) > c_iStoredBindsMax)
			StructDestroy(parse_EntityKeyBind, eaPop(&pBinds->eaBinds));
		
		if (eaSize(&pBinds->eaBinds))
			eaCopyStructs(&pBinds->eaBinds, &pEntBinds->eaBinds, parse_EntityKeyBind);
		else
			eaDestroyStruct(&pEntBinds->eaBinds, parse_EntityKeyBind);
		
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// Update the player's list of stored keybinds.
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslKeyBindSet(Entity* pEnt, EntityKeyBinds* pBinds)
{
	const char* pchProfile = gConf.bStoreKeybindsPerProfile ? pBinds->pchProfile : NULL;
	if (pBinds && gslKeyBinds_IsProfileValid(pEnt, pchProfile))
	{
		gslKeyBinds_Fixup(pEnt);
		gslKeyBinds_SetBindsForProfile(pEnt, pBinds, pchProfile);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslSetPlayerUIValueString(Entity *pEnt, const char *pchKey, const char *pchValue)
{
	if (pEnt->pPlayer && pEnt->pPlayer->pUI && pchKey && *pchKey)
	{
		PlayerUI *pUI = pEnt->pPlayer->pUI;
		PlayerUIPair *pPair = eaIndexedGetUsingString(&pUI->eaPairs, pchKey);
		U32 uiTime = timeSecondsSince2000();
		if (pPair)
		{
			SAFE_FREE(pPair->pchValue);
			pPair->pchValue = StructAllocString(pchValue);
		}
		else
		{
			pPair = StructCreate(parse_PlayerUIPair);
			pPair->pchKey = StructAllocString(pchKey);
			pPair->pchValue = StructAllocString(pchValue);
			eaPush(&pUI->eaPairs, pPair);
		}

		pPair->uiTime = uiTime;

		gslTrimPlayerUIPairs(pUI, uiTime);

		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslSetMapRegionScale(Entity *pEnt, WorldRegionType eType, F32 fScale)
{
	PlayerUI *pUI = pEnt->pPlayer->pUI;
	if (pUI = SAFE_MEMBER2(pEnt, pPlayer, pUI))
	{
		PlayerUIMapRegionScale *pRegionScale = eaIndexedGetUsingInt(&pUI->eaRegionScales, eType);
		if (pRegionScale)
		{
			pRegionScale->fScale = fScale;
		}
		else
		{
			pRegionScale = StructCreate(parse_PlayerUIMapRegionScale);
			pRegionScale->eType = eType;
			pRegionScale->fScale = fScale;
			eaPush(&pUI->eaRegionScales, pRegionScale);
		}
		
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslSetPlayerPersistedWindow(Entity *pEnt, UIPersistedWindow *pPersistedWindow)
{
	if (SAFE_MEMBER3(pEnt, pPlayer, pUI, pLooseUI) && pPersistedWindow && pPersistedWindow->pchName && *pPersistedWindow->pchName)
	{
		PlayerUI *pUI = pEnt->pPlayer->pUI;
		PlayerLooseUI *pLooseUI = pUI->pLooseUI;
		UIPersistedWindow *pWindows = eaIndexedGetUsingString(&pLooseUI->eaPersistedWindows, pPersistedWindow->pchName);
		U32 uiTime = timeSecondsSince2000();
		if (pWindows)
		{
			// ignore unchanged values
			if (!memcmp(pWindows->bfWindows, pPersistedWindow->bfWindows, ARRAY_SIZE(pPersistedWindow->bfWindows)))
			{
				return;
			}

			eaFindAndRemove(&pLooseUI->eaPersistedWindows, pWindows);
			SAFE_FREE(pWindows);
		}
		pWindows = StructClone(parse_UIPersistedWindow, pPersistedWindow);
		eaPush(&pLooseUI->eaPersistedWindows, pWindows);
		pWindows->uiTime = uiTime;

		gslTrimPersistedWindows(pUI, uiTime);

		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslResetPetTraySlots(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		PlayerLooseUI *pLooseUI = pEnt->pPlayer->pUI->pLooseUI;
		if (pLooseUI->eaPetCommandOrder)
		{
			eaClearStruct(&pLooseUI->eaPetCommandOrder, parse_PlayerPetPersistedOrder);

			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslSetPetTraySlot(Entity *pEnt, PlayerPetPersistedOrder *pTray)
{
	PlayerPetPersistedOrder *pUpdate = NULL;
	S32 iOldSlot = -1, iOldIndex = -1;
	if (!pTray || pTray->iSlot >= MAX_PLAYER_PET_PERSISTED_ORDER || !pTray->pchCommand || !*pTray->pchCommand)
		return;

	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		PlayerLooseUI *pLooseUI = pEnt->pPlayer->pUI->pLooseUI;

		if (!pLooseUI->eaPetCommandOrder)
		{
			eaCreate(&pLooseUI->eaPetCommandOrder);
			eaIndexedEnable(&pLooseUI->eaPetCommandOrder, parse_PlayerPetPersistedOrder);
		}

		for (iOldIndex = eaSize(&pLooseUI->eaPetCommandOrder) - 1; iOldIndex >= 0; iOldIndex--)
		{
			if (pLooseUI->eaPetCommandOrder[iOldIndex]->pchCommand == pTray->pchCommand)
			{
				iOldSlot = pLooseUI->eaPetCommandOrder[iOldIndex]->iSlot;
				break;
			}
		}

		if (pTray->iSlot < 0)
		{
			pUpdate = eaRemove(&pLooseUI->eaPetCommandOrder, iOldSlot);
			StructDestroy(parse_PlayerPetPersistedOrder, pUpdate);
			pUpdate = NULL;
		}
		else if (iOldSlot != pTray->iSlot)
		{
			pUpdate = eaIndexedGetUsingInt(&pLooseUI->eaPetCommandOrder, pTray->iSlot);
			if (!pUpdate)
			{
				pUpdate = StructCreate(parse_PlayerPetPersistedOrder);
				pUpdate->iSlot = pTray->iSlot;
				eaPush(&pLooseUI->eaPetCommandOrder, pUpdate);
			}
			else if (iOldSlot >= 0)
			{
				pUpdate->iSlot = iOldSlot;
				StructCopyAll(parse_PlayerPetPersistedOrder, pUpdate, pLooseUI->eaPetCommandOrder[iOldIndex]);
			}
		}

		if (pUpdate)
			StructCopyAll(parse_PlayerPetPersistedOrder, pTray, pUpdate);

		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_NAME(played) ACMD_ACCESSLEVEL(0);
void player_Played(CmdContext *pContext, Entity *pEnt)
{
	if(pEnt && pEnt->pSaved && pEnt->pPlayer)
	{
		char *estr = NULL;
		entFormatGameMessageKey(pEnt, &estr, "DateTime_ElapsedTime", STRFMT_TIMER("Time", (U32)pEnt->pPlayer->fTotalPlayTime), STRFMT_END);
		entFormatGameMessageKey(pEnt, pContext->output_msg, "Cmd_Played", STRFMT_ENTITY(pEnt), STRFMT_STRING("Time",estr), STRFMT_END);
		estrDestroy(&estr);
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0);
void player_SetPlayedTime(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		ClientCmd_gclUIGen_SetCurrentPlayTime(pEnt, entGetContainerID(pEnt), pEnt->pPlayer->iLastPlayedTime, pEnt->pPlayer->fTotalPlayTime);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
// This command sets the bAutoJoinTeamVoiceChat flag in player
void gslCmdSetAutoJoinTeamVoiceChatFlag(Entity *pEnt, bool bAutoJoinTeamVoiceChat)
{
	if (pEnt == NULL || pEnt->pPlayer == NULL)
	{
		return;
	}

	AutoTrans_trSetAutoJoinTeamVoiceChatFlag(NULL, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), bAutoJoinTeamVoiceChat);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
// This command resets the voice chat options
void gslCmdResetVoiceChatOptions(Entity *pEnt)
{
	if (pEnt == NULL || pEnt->pPlayer == NULL)
	{
		return;
	}

	AutoTrans_trSetAutoJoinTeamVoiceChatFlag(NULL, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), 1);
}

AUTO_COMMAND ACMD_NAME(Whitelist_Invites) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0);
void gslCmdSetInviteWhitelist(Entity *pEnt, bool bEnabled)
{
	if (!pEnt || !pEnt->pPlayer)
	{
		return;
	}
	if(bEnabled) {
		pEnt->pPlayer->eWhitelistFlags |= kPlayerWhitelistFlags_Invites;
	} else {
		pEnt->pPlayer->eWhitelistFlags &= ~kPlayerWhitelistFlags_Invites;
	}
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}

static void LeaderboardRequestCB(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PRE_NN_VALID SA_POST_FREE LeaderboardPageCB *pData)
{
	LeaderboardPage *pPage;

	if(RemoteCommandCheck_leaderboard_GetLeaderboardPage(pReturn,&pPage) == TRANSACTION_OUTCOME_FAILURE)
	{
		//Display some error message?
	}
	else
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pData->entRequester);
		//Call client function

		if(pEnt && pPage)
			ClientCmd_setLeaderboardPage(pEnt,pPage);

		if(pPage)
			StructDestroy(parse_LeaderboardPage,pPage);
	}

	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_NAME("LeaderboardPageRequest");
void gslLeaderboardPageRequest(Entity *pClientEntity, LeaderboardPageRequest *pRequest)
{
	LeaderboardPageCB *pData = malloc(sizeof(LeaderboardPageCB));

	pData->entRequester = pClientEntity->myRef;

	RemoteCommand_leaderboard_GetLeaderboardPage(objCreateManagedReturnVal(LeaderboardRequestCB,pData),GLOBALTYPE_LEADERBOARDSERVER,0,pRequest);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslRequestMapDisplayName(const char* pchMapNameMsgKey)
{
	static MessageRef** s_eaRefs = NULL;
	S32 i;

	if (!pchMapNameMsgKey || !pchMapNameMsgKey[0])
	{
		return;
	}
	for (i = eaSize(&s_eaRefs)-1; i >= 0; i--)
	{
		const char* pchRefString = REF_STRING_FROM_HANDLE(s_eaRefs[i]->hMessage);
		if (stricmp(pchRefString, pchMapNameMsgKey) == 0)
		{
			break;
		}
	}
	if (i < 0)
	{
		MessageRef* pRef = StructCreate(parse_MessageRef);
		SET_HANDLE_FROM_STRING("Message", pchMapNameMsgKey, pRef->hMessage);
		eaPush(&s_eaRefs, pRef);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(Macro) ACMD_PRIVATE;
void gslPlayerSetMacro(Entity* pEnt, U32 uMacroID, const char* pchMacro, const char* pchDesc, const char* pchIcon)
{
	if (pEnt && pEnt->pPlayer && entity_IsMacroValid(pchMacro, pchDesc, pchIcon))
	{
		if (uMacroID)
		{
			S32 iIdx = entity_FindMacroByID(pEnt, uMacroID);
			if (iIdx >= 0)
			{
				PlayerMacro* pMacro = pEnt->pPlayer->pUI->eaMacros[iIdx];
				StructCopyString(&pMacro->pchMacro, pchMacro);
				StructCopyString(&pMacro->pchDescription, pchDesc);
				pMacro->pchIcon = allocAddString(pchIcon);
				
				// Set dirty bits
				entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
				entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
			}
		}
		else
		{
			S32 iIdx = entity_FindMacro(pEnt, pchMacro, pchDesc, pchIcon);
			if (iIdx < 0)
			{
				gslEntity_CreateMacro(pEnt, pchMacro, pchDesc, pchIcon);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(MacroRemove) ACMD_PRIVATE;
void gslPlayerRemoveMacro(Entity* pEnt, U32 uMacroID)
{
	if (pEnt && pEnt->pPlayer)
	{
		gslEntity_DestroyMacro(pEnt, uMacroID);
	}
}