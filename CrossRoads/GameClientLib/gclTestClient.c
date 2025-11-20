#include "gclTestClient.h"
#include "gclScript.h"
#include "GameClientLib.h"
#include "gclBaseStates.h"
#include "gclPlayerControl.h"
#include "GlobalTypes.h"
#include "referencesystem.h"
#include "file.h"
#include "net/net.h"
#include "ticketnet.h"
#include "trivia.h"
#include "utilitiesLib.h"
#include "gclEntity.h"
#include "Character.h"
#include "entCritter.h"
#include "Character_target.h"
#include "ClientTargeting.h"
#include "wlInteraction.h"
#include "Player.h"
#include "Powers.h"
#include "PowerActivation.h"
#include "AttribModFragility.h"
#include "GlobalStateMachine.h"
#include "EntitySavedData.h"
#include "EntityIterator.h"
#include "MapDescription.h"
#include "testclient_comm.h"
#include "structNet.h"
#include "SavedPetCommon.h"
#include "contact_common.h"
#include "mission_common.h"
#include "inventoryCommon.h"
#include "rewardCommon.h"
#include "TestClientCommon.h"

#include "TestClientCommon_h_ast.h"
#include "Autogen/trivia_h_ast.h"
#include "../Common/AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

void gclTestClient_GetNearestTargets_internal(TestClientStateUpdate *pState, EntityRef **ppiEnts, Entity *pEnt, U32 target_type_req, U32 target_type_exc, int n, int depth);
void gclTestClient_GetNearestObjects_internal(TestClientStateUpdate *pState, char ***pppObjects, Entity *pEnt, int depth);
extern int clientTarget_FindAllValidTargets(Entity *e, PowerTarget *pPowerTarget, U32 target_type_req, U32 target_type_exc, ClientTargetDef ***pppTargetsOut, ClientTargetVisibleCheck fVisibleCheckFunc, ClientTargetSortDistCheck fSortDistFunc, ClientTargetSortFunc fSortFunc, bool bIncludeObjects, bool bTargetAllObjects);
extern int clientTarget_SortByDist(const ClientTargetDef **DefA, const ClientTargetDef **DefB);
extern void setTicketTracker(char *pTicketTracker);

extern int gbNoGraphics;
extern NetLink *gServerLink;
extern TriviaData **g_ppTrivia;
extern ParseTable parse_TicketData[];
#define TYPE_parse_TicketData TicketData
extern ParseTable parse_TicketRequestData[];
#define TYPE_parse_TicketRequestData TicketRequestData
extern ParseTable parse_TicketRequestResponse[];
#define TYPE_parse_TicketRequestResponse TicketRequestResponse
extern ParseTable parse_TicketRequestResponseList[];
#define TYPE_parse_TicketRequestResponseList TicketRequestResponseList
extern ParseTable parse_TicketRequestResponseWrapper[];
#define TYPE_parse_TicketRequestResponseWrapper TicketRequestResponseWrapper
extern DictionaryHandle g_ContactDictionary;

typedef struct TestClientTicketRequestInfo
{
	int iHandle;
	int iIterator;
	TicketRequestResponseList *pList;
} TestClientTicketRequestInfo;

TestClientTicketRequestInfo **gppTicketRequests = NULL;
int gTicketRequests = 0;
int giHeadshotDebugMode = 0;
int giDebugPacketDisconnect = 0;

TestClientStateUpdate gGCLTestClientState = {0};

void gclTestClient_UpdateEntity(TestClientStateUpdate *pState, TestClientEntity *pEnt, Entity *pSource, int depth)
{
	Entity *pTarget = NULL;
	Entity *pPlayer = entActivePlayerPtr();
	EntityRef iTarget = 0;
	TestClientEntity *pTargetEnt = NULL;
	Quat rot = {0.0};
	int i;

	if(!pEnt || (pEnt->iUpdateDepth && pEnt->iUpdateDepth <= depth) || !pSource || depth > 4 || !pPlayer)
	{
		return;
	}

	pEnt->iUpdateDepth = depth;

	pEnt->iID = entGetContainerID(pSource);
	estrCopy2(&pEnt->pchName, entGetLocalName(pSource));
	pEnt->fHP = SAFE_MEMBER2(entGetChar(pSource), pattrBasic, fHitPoints);
	pEnt->fMaxHP = SAFE_MEMBER2(entGetChar(pSource), pattrBasic, fHitPointsMax);
	pEnt->iLevel = SAFE_MEMBER(entGetChar(pSource), iLevelCombat);

	for(i = 0; i < 4; ++i)
	{
		pEnt->fShields[i] = 0;
	}

	if(pSource->pChar)
	{
		FOR_EACH_IN_EARRAY(pSource->pChar->modArray.ppMods, AttribMod, pMod)
		{
			PowerDef *pDef = GET_REF(pMod->hPowerDef);
			int iSpaceCategory = StaticDefineIntGetInt(PowerCategoriesEnum, "Region_Space");
			int iShieldCategory = StaticDefineIntGetInt(PowerCategoriesEnum, "Shield");

			if(!pDef || iSpaceCategory == -1 || iShieldCategory == -1 || eaiFind(&pDef->piCategories, iSpaceCategory) == -1 || eaiFind(&pDef->piCategories, iShieldCategory) == -1)
			{
				continue;
			}

			pEnt->fShields[pMod->uiDefIdx] = SAFE_MEMBER(pMod->pFragility, fHealth) / MAX(1, SAFE_MEMBER(pMod->pFragility, fHealthMax));
		}
		FOR_EACH_END
	}

	entGetPos(pSource, pEnt->vPos);
	entGetRot(pSource, rot);
	quatToPYR(rot, pEnt->vPyr);
	
	pEnt->fDistance = distance3(pPlayer->pos_use_accessor, pEnt->vPos);

	pEnt->bDead = SAFE_MEMBER2(entGetChar(pSource), pattrBasic, fHitPoints) == 0.0f;
	pEnt->bCasting = SAFE_MEMBER(entGetChar(pSource), pPowActCurrent) ? true : false;
	pEnt->bHostile = critter_IsKOS(PARTITION_CLIENT, pPlayer, pSource);

	if(depth == 3)
	{
		pEnt->iTarget = 0;
		eaiClear(&pEnt->piNearbyFriends);
		eaiClear(&pEnt->piNearbyHostiles);
		return;
	}

	if(pSource->pChar)
	{
		pTarget = entGetClientTarget(pSource, "selected", &pEnt->iTarget);
		pTargetEnt = eaIndexedGetUsingInt(&pState->ppEnts, pEnt->iTarget);

		if(pTarget && !pTargetEnt)
		{
			pTargetEnt = StructCreate(parse_TestClientEntity);
			pTargetEnt->iRef = pEnt->iTarget;
			eaIndexedAdd(&pState->ppEnts, pTargetEnt);
		}
	}

	gclTestClient_UpdateEntity(pState, pTargetEnt, pTarget, depth+1);
	gclTestClient_GetNearestTargets_internal(pState, &pEnt->piNearbyFriends, pSource, kTargetType_Friend, kTargetType_Self, 20, depth);
	gclTestClient_GetNearestTargets_internal(pState, &pEnt->piNearbyHostiles, pSource, 0, kTargetType_Friend|kTargetType_Self, 20, depth);
	gclTestClient_GetNearestObjects_internal(pState, &pEnt->ppNearbyObjects, pSource, depth);
}

void gclTestClient_UpdateObject(TestClientStateUpdate *pState, TestClientObject *pObj, WorldInteractionNode *pNode)
{
	static U32 iDestructibleMask;
	static U32 iClickableMask;
	static U32 iDoorMask;
	static int first = 0;
	Entity *pPlayer = entActivePlayerPtr();
	Vec3 vPos = {0};

	if(!pObj || !pNode || !pPlayer)
	{
		return;
	}

	if(!first)
	{
		first = 1;
		iDestructibleMask = wlInteractionClassNameToBitMask("Destructible");
		iClickableMask = wlInteractionClassNameToBitMask("Clickable");
		iDoorMask = wlInteractionClassNameToBitMask("Door");
	}

	pObj->pchName = wlInteractionNodeGetDisplayName(pNode);

	entGetPos(pPlayer, vPos);
	pObj->fDistance = wlInterationNode_FindNearestPoint(vPos, pNode, pObj->vPos);

	pObj->bDestructible = wlInteractionClassMatchesMask(pNode, iDestructibleMask);
	pObj->bClickable = wlInteractionClassMatchesMask(pNode, iClickableMask);
	pObj->bDoor = wlInteractionClassMatchesMask(pNode, iDoorMask);
}

void gclTestClient_UpdateMission(TestClientStateUpdate *pState, TestClientMission *pMission, Mission *pSource)
{
	MissionDef *pMissionDef = mission_GetDef(pSource);

	if(pMissionDef)
	{
		pMission->iLevel = pMissionDef->levelDef.missionLevel;
		pMission->bNeedsReturn = pMissionDef->needsReturn;
	}

	pMission->bInProgress = pSource->state == MissionState_InProgress;
	pMission->bCompleted = pSource->state > MissionState_InProgress;
	pMission->bSucceeded = pSource->state == MissionState_Succeeded;

	FOR_EACH_IN_EARRAY_FORWARDS(pSource->children, Mission, pChild)
	{
		TestClientMission *pChildMission = StructCreate(parse_TestClientMission);

		pChildMission->bIsChild = true;
		pChildMission->pchName = pChild->missionNameOrig;
		estrCopy(&pChildMission->pchParent, pMission->bIsChild ? &pMission->pchParent : &pMission->pchIndexName);
		estrPrintf(&pChildMission->pchIndexName, "%s::%s", pChildMission->pchParent, pChildMission->pchName);
		eaIndexedAdd(&pState->ppMyMissions, pChildMission);
		eaPush(&pMission->ppChildren, StructAllocString(pChildMission->pchIndexName));
		gclTestClient_UpdateMission(pState, pChildMission, pChild);
	}
	FOR_EACH_END
}

void gclTestClientOncePerFrame(void)
{
	if(!gclGetLinkToTestClient()){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	{
		Packet *pkt = pktCreate(gclGetLinkToTestClient(), TO_TESTCLIENT_CMD_COMMAND);
		char *pCmd = NULL;
		char *pState = NULL;

		GSM_PutFullStateStackIntoEString(&pState);
		estrPrintf(&pCmd, "StateUpdate %s", pState);
		pktSendString(pkt, pCmd);
		pktSend(&pkt);
		estrDestroy(&pState);
		estrDestroy(&pCmd);
	}

	if(GSM_IsStateActive(GCL_GAMEPLAY))
	{
		TestClientStateUpdate *pCurrentState = NULL;
		StashTableIterator iter = {0};
		StashElement pElem = NULL;
		Entity *pPlayer = entActivePlayerPtr();
		Team *pTeam = team_GetTeam(pPlayer);
		Entity *pPet = NULL;
		TestClientEntity *pEnt;
		SavedMapDescription *pCurrentMap;

		if(!pPlayer)
		{
			PERFINFO_AUTO_STOP();
			return;
		}

		pCurrentState = &gGCLTestClientState; // Yes, I know, this is ugly. Whatever.
		StructReset(parse_TestClientStateUpdate, pCurrentState);
		assert(pCurrentState);

		eaIndexedEnable(&pCurrentState->ppEnts, parse_TestClientEntity);
		eaIndexedEnable(&pCurrentState->ppObjects, parse_TestClientObject);

		if(entGetContainerID(pPlayer) != pCurrentState->iID)
		{
			pCurrentState->iID = entGetContainerID(pPlayer);
		}

		pCurrentMap = entity_GetLastMap(pPlayer);
		if(pCurrentMap)
		{
			estrCopy2(&pCurrentState->pchMapName, pCurrentMap->mapDescription);
			pCurrentState->iInstanceIndex = pCurrentMap->mapInstanceIndex;
		}

		pCurrentState->bSTOSpaceshipMovement = gclPlayerControl_IsInSTOSpaceshipMovement();

		pCurrentState->iMyRef = entGetRef(pPlayer);
		
		pEnt = StructCreate(parse_TestClientEntity);
		pEnt->iRef = pCurrentState->iMyRef;
		eaIndexedAdd(&pCurrentState->ppEnts, pEnt);

		gclTestClient_UpdateEntity(pCurrentState, pEnt, pPlayer, 1);

		FOR_EACH_IN_EARRAY_FORWARDS(pPlayer->pChar->ppPowers, Power, pPower)
		{
			TestClientPower *pPow;
			PowerDef *pDef = GET_REF(pPower->hDef);
			PowerTarget *pTarget = NULL;
			int iBlockCategory = StaticDefineIntGetInt(PowerCategoriesEnum, "Block");
			int iSmashCategory = StaticDefineIntGetInt(PowerCategoriesEnum, "Heldobject");

			if(!pDef || pDef->eType >= kPowerType_Passive || (iBlockCategory > -1 && eaiFind(&pDef->piCategories, iBlockCategory) > -1) || (iSmashCategory > -1 && eaiFind(&pDef->piCategories, iSmashCategory) > -1))
			{
				continue;
			}

			pTarget = GET_REF(pDef->hTargetAffected);

			if(!pTarget)
			{
				continue;
			}

			pPow = StructCreate(parse_TestClientPower);

			pPow->iID = pPower->uiID;
			pPow->pchName = pDef->pchName;
			pPow->fRange = pDef->fRange;

			if(pTarget->bAllowFoe)
			{
				pPow->bAttack = true;
			}

			eaPush(&pCurrentState->ppPowers, pPow);
		}
		FOR_EACH_END

		// Team stuff
		if(pTeam)
		{
			pCurrentState->bIsTeamed = true;
			pCurrentState->iNumMembers = eaSize(&pTeam->eaMembers);
			pCurrentState->iNumRequests = eaSize(&pTeam->eaRequests);

			if(pPlayer->pTeam->eState == TeamState_Invitee)
			{
				pCurrentState->bIsInvited = true;
			}

			FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pMember)
			{
				Entity *pTeamEntity = GET_REF(pMember->hEnt);
				TestClientEntity *pTeamEnt = NULL;

				if(!pTeamEntity)
				{
					continue;
				}

				pTeamEnt = StructCreate(parse_TestClientEntity);
				pTeamEnt->iRef = ipMemberIndex+1;

				FOR_EACH_IN_EARRAY_FORWARDS(pCurrentState->ppEnts, TestClientEntity, pKnownEnt)
				{
					if(pKnownEnt->iID == entGetContainerID(pTeamEntity))
					{
						pTeamEnt->iRefIfTeamed = pKnownEnt->iRef;
						estrCopy2(&pKnownEnt->pchMapName, pMember->pcMapName);
						break;
					}
				}
				FOR_EACH_END

				eaIndexedAdd(&pCurrentState->ppEnts, pTeamEnt);

				if(!pTeamEnt->iRefIfTeamed)
				{
					gclTestClient_UpdateEntity(pCurrentState, pTeamEnt, pTeamEntity, 1);
				}
			}
			FOR_EACH_END

			FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaRequests, TeamMember, pMember)
			{
				Entity *pTeamEntity = GET_REF(pMember->hEnt);
				TestClientEntity *pTeamEnt = NULL;

				if(!pTeamEntity)
				{
					continue;
				}

				pTeamEnt = StructCreate(parse_TestClientEntity);
				pTeamEnt->iRef = ipMemberIndex+6;

				FOR_EACH_IN_EARRAY_FORWARDS(pCurrentState->ppEnts, TestClientEntity, pKnownEnt)
				{
					if(pKnownEnt->iID == entGetContainerID(pTeamEntity))
					{
						pTeamEnt->iRefIfTeamed = pKnownEnt->iRef;
						estrCopy2(&pKnownEnt->pchMapName, pMember->pcMapName);
						break;
					}
				}
				FOR_EACH_END

				eaIndexedAdd(&pCurrentState->ppEnts, pTeamEnt);

				if(!pTeamEnt->iRefIfTeamed)
				{
					gclTestClient_UpdateEntity(pCurrentState, pTeamEnt, pTeamEntity, 1);
				}
			}
			FOR_EACH_END
		}

		FOR_EACH_IN_EARRAY_FORWARDS(pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions, InteractOption, pOption)
		{
			TestClientInteract *pInteract = StructCreate(parse_TestClientInteract);

			pInteract->pchString = StructAllocString(pOption->pcInteractString);
			pInteract->iRef = pOption->entRef;
			eaPush(&pCurrentState->ppInteracts, pInteract);
		}
		FOR_EACH_END

		FOR_EACH_IN_EARRAY_FORWARDS(pPlayer->pPlayer->pInteractInfo->nearbyContacts, ContactInfo, pContactInfo)
		{
			TestClientContact *pContact = StructCreate(parse_TestClientContact);

			pContact->iRef = pContactInfo->entRef;
			pContact->bIsImportant = pContactInfo->currIndicator >= ContactIndicator_MissionAvailable;
			pContact->bHasMission = pContactInfo->currIndicator == ContactIndicator_MissionAvailable;
			pContact->bHasMissionComplete = (pContactInfo->currIndicator == ContactIndicator_MissionCompleted || pContactInfo->currIndicator == ContactIndicator_MissionCompletedRepeatable);
			eaPush(&pCurrentState->ppContacts, pContact);

			if(eaIndexedFindUsingInt(&pCurrentState->ppEnts, pContact->iRef) == -1)
			{
				TestClientEntity *pContactEnt = StructCreate(parse_TestClientEntity);

				pContactEnt->iRef = pContact->iRef;
				eaIndexedAdd(&pCurrentState->ppEnts, pContactEnt);
				gclTestClient_UpdateEntity(pCurrentState, pContactEnt, entFromEntityRefAnyPartition(pContact->iRef), 1);
			}
		}
		FOR_EACH_END

		if(pPlayer->pPlayer->pInteractInfo->pContactDialog)
		{
			pCurrentState->pContactDialog = StructCreate(parse_TestClientContactDialog);
			estrCopy(&pCurrentState->pContactDialog->pchText1, &pPlayer->pPlayer->pInteractInfo->pContactDialog->pchDialogText1);
			estrCopy(&pCurrentState->pContactDialog->pchText2, &pPlayer->pPlayer->pInteractInfo->pContactDialog->pchDialogText2);

			FOR_EACH_IN_EARRAY_FORWARDS(pPlayer->pPlayer->pInteractInfo->pContactDialog->eaOptions, ContactDialogOption, pOption)
			{
				TestClientContactDialogOption *pDialogOption = StructCreate(parse_TestClientContactDialogOption);

				pDialogOption->pchKey = StructAllocString(pOption->pchKey);
				estrCopy(&pDialogOption->pchName, &pOption->pchDisplayString);
				pDialogOption->bIsMission = pOption->eType == ContactIndicator_MissionAvailable;
				pDialogOption->bIsMissionComplete = (pOption->eType == ContactIndicator_MissionCompleted || 
					pOption->eType == ContactIndicator_MissionCompletedRepeatable);
				eaPush(&pCurrentState->pContactDialog->ppOptions, pDialogOption);
			}
			FOR_EACH_END

			FOR_EACH_IN_EARRAY_FORWARDS(pPlayer->pPlayer->pInteractInfo->pContactDialog->eaRewardBags, InventoryBag, pBag)
			{
				if(pBag->pRewardBagInfo && pBag->pRewardBagInfo->PickupType == kRewardPickupType_Choose)
				{
					FOR_EACH_IN_EARRAY_FORWARDS(pBag->ppIndexedInventorySlots, InventorySlot, pSlot)
					{
						pCurrentState->pContactDialog->bHasReward = true;
						eaPush(&pCurrentState->pContactDialog->rewardChoices, REF_STRING_FROM_HANDLE(pSlot->pItem->hItem));
					}
					FOR_EACH_END

					break;
				}
			}
			FOR_EACH_END
		}

		eaIndexedEnable(&pCurrentState->ppMyMissions, parse_TestClientMission);

		FOR_EACH_IN_EARRAY_FORWARDS(pPlayer->pPlayer->missionInfo->missions, Mission, pMission)
		{
			if(mission_GetDef(pMission) && mission_GetType(pMission) != MissionType_Perk)
			{
				TestClientMission *pNewMission = StructCreate(parse_TestClientMission);

				estrCopy2(&pNewMission->pchIndexName, pMission->missionNameOrig);
				pNewMission->pchName = pMission->missionNameOrig;
				eaIndexedAdd(&pCurrentState->ppMyMissions, pNewMission);
				gclTestClient_UpdateMission(pCurrentState, pNewMission, pMission);
			}
		}
		FOR_EACH_END

		if(0)
		{
			Packet *pkt = pktCreate(gclGetLinkToTestClient(), TO_TESTCLIENT_CMD_UPDATE);
			ParserSend(parse_TestClientStateUpdate, pkt, NULL, pCurrentState, 0, 0, 0, NULL);
			pktSend(&pkt);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void gclTestClient_GetNearestTargets_internal(TestClientStateUpdate *pState, EntityRef **ppiEnts, Entity *pEnt, U32 target_type_req, U32 target_type_exc, int n, int depth)
{
	static ClientTargetDef **ppTargets = NULL;
	int numTargets;
	int i;

	if(!pEnt || !ppiEnts)
	{
		return;
	}

	eaiClear(ppiEnts);
	numTargets = clientTarget_FindAllValidTargets(pEnt, NULL, target_type_req, target_type_exc, &ppTargets, NULL, NULL, clientTarget_SortByDist, false, false);

	if(numTargets > n)
	{
		numTargets = n;
	}

	for(i = 0; i < numTargets; ++i)
	{
		if(ppTargets[i]->entRef)
		{
			eaiPush(ppiEnts, ppTargets[i]->entRef);
		}
	}

	eaClear(&ppTargets);

	for(i = 0; i < numTargets; ++i)
	{
		TestClientEntity *pTCEnt = eaIndexedGetUsingInt(&pState->ppEnts, (*ppiEnts)[i]);

		if(!pTCEnt)
		{
			pTCEnt = StructCreate(parse_TestClientEntity);
			pTCEnt->iRef = (*ppiEnts)[i];
			eaIndexedAdd(&pState->ppEnts, pTCEnt);
		}

		gclTestClient_UpdateEntity(pState, pTCEnt, entFromEntityRefAnyPartition(pTCEnt->iRef), depth+1);
	}
}

static int gclTestClient_CompareObjects(const TestClientObject **pNodeA, const TestClientObject **pNodeB)
{
	F32 diff = (*pNodeA)->fDistance - (*pNodeB)->fDistance;
	return diff < 0 ? -1 : (diff > 0 ? 1 : 0);
}

void gclTestClient_GetNearestObjects_internal(TestClientStateUpdate *pState, char ***pppObjects, Entity *pEnt, int depth)
{
	static TestClientObject **ppUnsortedObjects = NULL;
	Entity *pPlayer = entActivePlayerPtr();
	EntityIterator* iter = NULL;
	Entity *currEnt = NULL;

	if(!pEnt || !pppObjects || depth > 3)
	{
		return;
	}

	eaClear(pppObjects);

	iter = entGetIteratorAllTypesAllPartitions(0, gbNoGraphics ? ENTITYFLAG_IGNORE : ENTITYFLAG_IGNORE|ENTITYFLAG_DONOTDRAW);

	while((currEnt = EntityIteratorGetNext(iter)))
	{
		WorldInteractionNode *pCreatorNode = NULL;
		TestClientObject *pObject = NULL;
		TestClientEntity *pEntity = NULL;

		if(!character_TargetMatchesType(PARTITION_CLIENT, pEnt->pChar, currEnt->pChar, kTargetType_Critter|kTargetType_Foe, 0))
		{
			continue;
		}

		if(!entIsSelectable(pPlayer->pChar, currEnt))
		{
			continue;
		}

		if(!(pCreatorNode = GET_REF(currEnt->hCreatorNode)))
		{
			continue;
		}

		pObject = eaIndexedGetUsingString(&pState->ppObjects, wlInteractionNodeGetKey(pCreatorNode));

		if(!pObject)
		{
			pObject = StructCreate(parse_TestClientObject);
			pObject->pchKey = StructAllocString(wlInteractionNodeGetKey(pCreatorNode));
			pObject->iRef = entGetRef(currEnt);
			eaIndexedAdd(&pState->ppObjects, pObject);
			gclTestClient_UpdateObject(pState, pObject, pCreatorNode);
		}

		pEntity = eaIndexedGetUsingInt(&pState->ppObjects, pObject->iRef);

		if(!pEntity)
		{
			pEntity = StructCreate(parse_TestClientEntity);
			pEntity->iRef = pObject->iRef;
			pEntity->pchKey = StructAllocString(wlInteractionNodeGetKey(pCreatorNode));
			eaIndexedAdd(&pState->ppEnts, pEntity);
			gclTestClient_UpdateEntity(pState, pEntity, currEnt, depth+1);
		}

		eaPush(&ppUnsortedObjects, pObject);
	}

	EntityIteratorRelease(iter);
	
	if(pEnt->pPlayer)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pEnt->pPlayer->InteractStatus.ppTargetableNodes, TargetableNode, pNode)
		{
			WorldInteractionNode *pWLNode = GET_REF(pNode->hNode);
			TestClientObject *pObject;

			if(!pWLNode)
			{
				continue;
			}

			pObject = eaIndexedGetUsingString(&pState->ppObjects, wlInteractionNodeGetKey(pWLNode));

			if(!pObject)
			{
				pObject = StructCreate(parse_TestClientObject);
				pObject->pchKey = StructAllocString(wlInteractionNodeGetKey(pWLNode));
				eaIndexedAdd(&pState->ppObjects, pObject);
				gclTestClient_UpdateObject(pState, pObject, pWLNode);
			}

			eaPush(&ppUnsortedObjects, pObject);
		}
		FOR_EACH_END
	}

	eaQSort(ppUnsortedObjects, gclTestClient_CompareObjects);

	FOR_EACH_IN_EARRAY_FORWARDS(ppUnsortedObjects, TestClientObject, pObject)
	{
		eaPush(pppObjects, StructAllocString(pObject->pchKey));
	}
	FOR_EACH_END

	eaClear(&ppUnsortedObjects);
}

AUTO_COMMAND ACMD_NAME(TestClient_Target) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclTestClient_Target(EntityRef iRef)
{
	Entity *pEnt = entActivePlayerPtr();

	if(!pEnt)
	{
		return;
	}

	entity_SetTarget(pEnt, iRef);
}

AUTO_COMMAND ACMD_NAME(TestClient_TargetObject) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclTestClient_TargetObject(const char *pchKey)
{
	Entity *pEnt = entActivePlayerPtr();

	if(!pEnt)
	{
		return;
	}

	entity_SetTargetObject(pEnt, pchKey);
}

AUTO_COMMAND ACMD_NAME(TestClient_Assist) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclTestClient_Assist(EntityRef iRef)
{
	Entity *pEnt = entActivePlayerPtr();

	if(!pEnt)
	{
		return;
	}

	entity_AssistTarget(pEnt, iRef);
}

AUTO_COMMAND ACMD_NAME(TestClient_EnableTimedDisconnects) ACMD_CMDLINE ACMD_ACCESSLEVEL(9) ACMD_HIDE;
void gclTestClient_EnableTimedDisconnects(F32 delay)
{
	commTimedDisconnect(commDefault(), delay);
}

AUTO_COMMAND ACMD_NAME(TestClient_EnableRandomDisconnects) ACMD_CMDLINE ACMD_ACCESSLEVEL(9) ACMD_HIDE;
void gclTestClient_EnableRandomDisconnects(int chance)
{
	commRandomDisconnect(commDefault(), chance);
}

AUTO_COMMAND ACMD_NAME(TestClient_EnablePacketDisconnects) ACMD_CMDLINE ACMD_ACCESSLEVEL(9) ACMD_HIDE;
void gclTestClient_EnablePacketDisconnects(int packets)
{
	giDebugPacketDisconnect = packets;
}

AUTO_COMMAND ACMD_NAME(TestClient_EnableCorruption) ACMD_CMDLINE ACMD_ACCESSLEVEL(9) ACMD_HIDE;
void gclTestClient_EnableCorruption(int freq)
{
	linkSetCorruptionFrequency(gServerLink, freq);
}

AUTO_COMMAND ACMD_NAME(TestClient_SetTicketTracker) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
void gclTestClient_SetTicketTracker(char *pServer)
{
	setTicketTracker(pServer);
}

AUTO_COMMAND ACMD_NAME(TestClient_SendTicket) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
bool gclTestClient_SendTicket(char *pCategory, char *pSummary, ACMD_SENTENCE pDescription)
{
	Entity *pEnt = entActivePlayerPtr();
	TicketData *pTicketData = StructCreate(parse_TicketData);
	NOCONST(TriviaList) *ptList = calloc(1, sizeof(TriviaList));
	char *pUnescapedSummary = NULL;
	char *pAccountName = NULL;

	if(!pEnt)
	{
		return false;
	}

	pTicketData->pPlatformName = strdup(PLATFORM_NAME);
	pTicketData->pProductName = strdup(GetProductName());
	pTicketData->pVersionString = strdup(GetUsefulVersionString());

	pTicketData->pAccountName = strdup(entGetAccountOrLocalName(pEnt));
	pTicketData->pCharacterName = strdup(entGetLocalName(pEnt));

	pTicketData->pMainCategory = strdup("CBug.CategoryMain.GameSupport");
	pTicketData->pCategory = strdup(pCategory);
	pTicketData->pSummary = pSummary ? strdup(pSummary) : NULL;
	pTicketData->pUserDescription = pDescription ? strdup(pDescription) : NULL;

	pTicketData->iProductionMode = isProductionMode();
	ptList->triviaDatas = (NOCONST(TriviaData)**) g_ppTrivia;
	pTicketData->pTriviaList = (TriviaList*) ptList;
	pTicketData->iMergeID = 0;
	pTicketData->imagePath = NULL;

	pTicketData->eLanguage = entGetLanguage(pEnt);
	pTicketData->uIsInternal = 0;

	return ticketTrackerSendTicket(pTicketData);
}

void gclTestClient_TicketRequestCallback(void *userData, const char *ticketResponse)
{
	TicketRequestResponseWrapper *pWrapper = StructCreate(parse_TicketRequestResponseWrapper);
	TicketRequestResponseList *pList = StructCreate(parse_TicketRequestResponseList);
	TestClientTicketRequestInfo *pInfo = (TestClientTicketRequestInfo *)userData;
	char *pCmd = NULL;
	char *pParsedStruct = NULL;

	if(!pInfo)
	{
		return;
	}

	if(!ticketResponse || !*ticketResponse)
	{
		pInfo->iHandle = 0;
		return;
	}

	if(!ParserReadText(ticketResponse, parse_TicketRequestResponseWrapper, pWrapper, 0))
	{
		pInfo->iHandle = 0;
		return;
	}

	if(!pWrapper->pListString || !pWrapper->pTPIString)
	{
		StructDestroy(parse_TicketRequestResponseWrapper, pWrapper);
		pInfo->iHandle = 0;
		return;
	}

	if(!ParserReadTextSafe(pWrapper->pListString, pWrapper->pTPIString, pWrapper->uCRC, parse_TicketRequestResponseList, pList, 0))
	{
		StructDestroy(parse_TicketRequestResponseWrapper, pWrapper);
		pInfo->iHandle = 0;
		return;
	}

	pInfo->pList = pList;
	pInfo->iIterator = 0;

	StructDestroy(parse_TicketRequestResponseWrapper, pWrapper);
}

AUTO_COMMAND ACMD_NAME(TestClient_SearchTicketCategory) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
int gclTestClient_SearchTicketCategory(const char *pCategory, const char *pKeyword, bool bDumpResults)
{
	TicketRequestData *pRequest = StructCreate(parse_TicketRequestData);
	TestClientTicketRequestInfo *pInfo = NULL;

	if(!bDumpResults)
	{
		pInfo = calloc(1, sizeof(TestClientTicketRequestInfo));
		pInfo->iHandle = ++gTicketRequests;

		eaPush(&gppTicketRequests, pInfo);
	}

	pRequest->pMainCategory = strdup("CBug.CategoryMain.GameSupport");
	pRequest->pCategory = strdup(pCategory);
	pRequest->pKeyword = strdup(pKeyword);

	ticketTrackerSendLabelRequest(pRequest, gclTestClient_TicketRequestCallback, (void *)pInfo);

	if(pInfo && pInfo->iHandle == 0)
	{
		eaPop(&gppTicketRequests);
		free(pInfo);
		pInfo = NULL;
	}

	return pInfo ? pInfo->iHandle : 0;
}

AUTO_COMMAND ACMD_NAME(TestClient_TicketResponseNext) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
int gclTestClient_TicketResponseNext(int iHandle)
{	
	TestClientTicketRequestInfo *pInfo = NULL;
	int iIndex = 0;

	FOR_EACH_IN_EARRAY(gppTicketRequests, TestClientTicketRequestInfo, p)
	{
		if(p->iHandle == iHandle)
		{
			pInfo = p;
			iIndex = ipIndex;
			break;
		}
	}
	FOR_EACH_END

	if(!pInfo)
	{
		return -1;
	}

	++pInfo->iIterator;

	if(pInfo->iIterator > eaSize(&pInfo->pList->ppTickets))
	{
		pInfo->iIterator = 0;
		return -1;
	}

	return pInfo->iIterator;
}

AUTO_COMMAND ACMD_NAME(TestClient_TicketResponseClose) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
void gclTestClient_TicketResponseClose(int iHandle)
{
	FOR_EACH_IN_EARRAY(gppTicketRequests, TestClientTicketRequestInfo, pInfo)
	{
		if(pInfo->iHandle == iHandle)
		{
			eaRemove(&gppTicketRequests, ipInfoIndex);
			StructDestroy(parse_TicketRequestResponseList, pInfo->pList);
			free(pInfo);
			break;
		}
	}
	FOR_EACH_END
}

AUTO_COMMAND ACMD_NAME(TestClient_TicketResponseReset) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
void gclTestClient_TicketResponseReset(void)
{
	FOR_EACH_IN_EARRAY(gppTicketRequests, TestClientTicketRequestInfo, pInfo)
	{
		eaRemove(&gppTicketRequests, ipInfoIndex);
		StructDestroy(parse_TicketRequestResponseList, pInfo->pList);
		free(pInfo);
	}
	FOR_EACH_END
}

AUTO_COMMAND ACMD_NAME(TestClient_TicketCategory) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
char *gclTestClient_TicketCategory(int iHandle, int iTicket)
{
	FOR_EACH_IN_EARRAY(gppTicketRequests, TestClientTicketRequestInfo, pInfo)
	{
		if(pInfo->iHandle == iHandle)
		{
			return pInfo->pList->ppTickets[iTicket-1]->pCategory;
		}
	}
	FOR_EACH_END

	return "INVALID TICKET";
}

AUTO_COMMAND ACMD_NAME(TestClient_TicketSummary) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
char *gclTestClient_TicketSummary(int iHandle, int iTicket)
{
	FOR_EACH_IN_EARRAY(gppTicketRequests, TestClientTicketRequestInfo, pInfo)
	{
		if(pInfo->iHandle == iHandle)
		{
			return pInfo->pList->ppTickets[iTicket-1]->pSummary;
		}
	}
	FOR_EACH_END

	return "INVALID TICKET";
}

AUTO_COMMAND ACMD_NAME(TestClient_TicketDescription) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
char *gclTestClient_TicketDescription(int iHandle, int iTicket)
{
	FOR_EACH_IN_EARRAY(gppTicketRequests, TestClientTicketRequestInfo, pInfo)
	{
		if(pInfo->iHandle == iHandle)
		{
			return pInfo->pList->ppTickets[iTicket-1]->pDescription;
		}
	}
	FOR_EACH_END

	return "INVALID TICKET";
}

AUTO_COMMAND ACMD_NAME(HeadshotDebugMode) ACMD_COMMANDLINE;
int gclTestClient_HeadshotDebugMode(int active)
{
	giHeadshotDebugMode = active;
	return giHeadshotDebugMode;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME(TestClient_Receive);
void gclTestClient_Receive(ContainerID iSenderID, char *sender, char *cmd)
{
	gclScript_QueueChat("Direct", sender, cmd);
	SendCommandStringToTestClientf("PushChat Direct %u \"%s\" \"%s\"", iSenderID, sender, cmd);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME(TestClient_RemoteExecute);
void gclTestClient_RemoteExecute(char *cmd)
{
	gclScript_QueueChat("Direct", "!!None", cmd);
	SendCommandStringToTestClientf("PushChat Direct 0 None \"%s\"", cmd);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME(TestClient_ChangeOwner);
void gclTestClient_ChangeOwner(ContainerID iOwner)
{
	SendCommandStringToTestClientf("OwnerID %u", iOwner);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME(TestClient_ResetSelf);
void gclTestClient_ResetSelf(void)
{
	//SendCommandStringToTestClient("Push_Reset");
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME(TestClient_KillSelf);
void gclTestClient_KillSelf(void)
{
	SendCommandStringToTestClient("Exit");
}

AUTO_COMMAND ACMD_NAME("TestClientContactRespond") ACMD_ACCESSLEVEL(1) ACMD_HIDE;
void gclTestClient_ContactRespond(const char *pchKey, const char *singleRewardChoice)
{
	static ContactRewardChoices rewardChoices = {0};
	if(singleRewardChoice && singleRewardChoice[0])
	{
		eaPush(&rewardChoices.ppItemNames, StructAllocString(singleRewardChoice));
	}

	ServerCmd_ContactResponse(pchKey, &rewardChoices, 0);

	eaClearEx(&rewardChoices.ppItemNames, StructFreeString);
}

AUTO_COMMAND ACMD_NAME("TestClientAwayTeamOptIn") ACMD_ACCESSLEVEL(1) ACMD_HIDE;
void gclTestClient_AwayTeamOptIn(void)
{
	ServerCmd_gslTeam_cmd_MapTransferOptIn();
}