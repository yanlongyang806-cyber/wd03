#include "earray.h"
#include "Expression.h"
#include "cmdparse.h"
#include "UIGen.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CombatEnums.h"
#include "EntityLib.h"
#include "EntityResolver.h"
#include "EntitySavedData.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "Team.h"
#include "TeamPetsCommonStructs.h"
#include "AutoTransDefs.h"
#include "gclEntity.h"
#include "MapDescription.h"
#include "mission_common.h"
#include "WorldGrid.h"
#include "Team_h_ast.h"
#include "gclUIGen.h"
#include "GameStringFormat.h"
#include "OfficerCommon.h"
#include "Player.h"
#include "entCritter.h"
#include "WorldGrid.h"
#include "gclFriendsIgnore.h"
#include "chatCommonStructs.h"
#include "rewardCommon.h"
#include "GlobalStateMachine.h"
#include "gclBaseStates.h"
#include "TeamUpCommon.h"
#include "dynFxManager.h"
#include "dynFxInterface.h"
#include "dynFxInfo.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/TeamUI_c_ast.h"
#include "AutoGen/dynFxInfo_h_ast.h"

extern StaticDefineInt CharClassTypesEnum[];

extern bool gbNoGraphics;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

///////////////////////////////////////////////////////////////////////////////////////////
// Expression Functions
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct TeamMemberData {
	Entity *pEnt;			AST(UNOWNED)
	char *pcName;
	const char *pcAccount;	AST(UNOWNED)
	char *pcLocation;		AST(ESTRING)
	U32 iLevel;
	U32 iCombatLevel;
	U32 iOfficerRank;
	const char *pcStatus;	AST(UNOWNED)
	const char *pcClassName;AST(UNOWNED)
	S32 iExpLevel;
	bool bSidekicked;
	bool bDead;
	bool bDisconnected;
	bool bNonLocal;
	bool bIsPet;
	int iAiState;
	int iAiStance;
	int iEntID;

	// Indicates if the team member is talking in voice chat
	bool bIsTalking;
} TeamMemberData;

AUTO_STRUCT;
typedef struct TeamUpGroupData
{
	U32 iGroupIndex;				AST(NAME(GroupIndex))

	U32 bMyGroup;				AST(NAME(MyGroup))
	U32 bFullGroup;				AST(NAME(FullGroup))
	U32 bEmptyGroup;			AST(NAME(EmptyGroup))
}TeamUpGroupData;

AUTO_STRUCT;
typedef struct AwayTeamMemberData {
	Entity*					pEnt;			AST(UNOWNED)
	Entity*					pOwnerEnt;		AST(UNOWNED)
	char*					pcName;
	char*					pcOwnerName;
	int						iONLen;
	bool					bIsPet;
	int						iClass;
	const char*				pcClassName;	AST(UNOWNED)
	bool					bPetPermission;
	bool					bIsReady;
	S32						iEntType;
	U32						iEntID;
	REF_TO(PlayerCostume)	hCostume; // This is only valid for CritterPets
} AwayTeamMemberData;

AUTO_STRUCT;
typedef struct TeamPetData {
	U32 iPetID;
	U32 iPetType;
} TeamPetData;

AUTO_STRUCT;
typedef struct AwayTeamData
{
	bool				bIsReady;
	bool				bChanged;
	bool				bAllInCircle; // Are all the players in the venture forth circle?
	S32					iTimer;
	S32					iPlayerCount;
	AwayTeamMembers*	pAwayTeamMembers;
} AwayTeamData;

static AwayTeamData* s_pAwayTeamData = NULL;
static TeamMemberMapInfoRequest** s_eaMapInfoRequest = NULL;
static bool s_bInCircle = false;
static dtFx s_partyCircleFx = 0;
static S64 s_timePCFxLastUpdated = 0;

INT_EARRAY eApprovedPetIDs = NULL;
INT_EARRAY eApprovedPetTypes = NULL;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetStatusMessageMaxLength);
S32 teamExpr_team_GetStatusMessageMaxLength(void)
{
	return TEAM_STATUS_MESSAGE_MAX_LENGTH;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_AmITeamLeader);
bool teamExpr_team_AmITeamLeader()
{
	Entity* pPlayer = entActivePlayerPtr();
	return team_IsTeamLeader(pPlayer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsMyPet);
bool teamExpr_IsMyPet(SA_PARAM_OP_VALID Entity *pTeamMate)
{
	Entity* pPlayer = entActivePlayerPtr();
	return (pTeamMate && pPlayer && pTeamMate->erOwner == entGetRef(pPlayer));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetTimerValue);
S32 teamExpr_GetTimerValue(void)
{
	return s_pAwayTeamData ? s_pAwayTeamData->iTimer : -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_SetAwayTeamDataUpdated);
void teamExpr_SetAwayTeamDataUpdated(void)
{
	if ( s_pAwayTeamData )
	{
		s_pAwayTeamData->bChanged = false;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_HasAwayTeamDataChanged);
bool teamExpr_HasAwayTeamDataChanged(void)
{
	return s_pAwayTeamData ? s_pAwayTeamData->bChanged : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetAwayTeamPlayerSize);
S32 teamExpr_GetAwayTeamPlayerSize(void)
{
	return s_pAwayTeamData ? s_pAwayTeamData->iPlayerCount : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsAwayTeamFull);
bool teamExpr_IsAwayTeamFull(void)
{
	AwayTeamMembers *pAwayTeamMembers = SAFE_MEMBER(s_pAwayTeamData, pAwayTeamMembers);
	return pAwayTeamMembers && eaSize(&pAwayTeamMembers->eaMembers) >= pAwayTeamMembers->iMaxTeamSize;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_AwayTeamAmIReady);
bool teamExpr_AwayTeamAmIReady(void)
{
	return s_pAwayTeamData ? s_pAwayTeamData->bIsReady : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_AwayTeamAmIInThePartyCircle);
bool teamExpr_AwayTeamAmIInThePartyCircle(void)
{
	return s_bInCircle;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_AwayTeamAreAllInThePartyCircle);
bool teamExpr_AwayTeamAreAllInThePartyCircle(void)
{
	return s_pAwayTeamData ? s_pAwayTeamData->bAllInCircle : false;
}

static void gclTeam_CreateAwayTeamData(Entity *pEnt, S32 iTimer, AwayTeamMembers* pAwayTeamMembers, bool bAllInCircle)
{
	if ( s_pAwayTeamData==NULL )
	{
		s_pAwayTeamData = StructCreate( parse_AwayTeamData );
	}

	s_pAwayTeamData->iTimer = iTimer;
	s_pAwayTeamData->bChanged = true;
	s_pAwayTeamData->bIsReady = false;
	s_pAwayTeamData->iPlayerCount = 0;
	s_pAwayTeamData->bAllInCircle = bAllInCircle;

	StructDestroySafe( parse_AwayTeamMembers, &s_pAwayTeamData->pAwayTeamMembers );

	if ( pAwayTeamMembers )
	{
		S32 i, iAwayTeamSize = eaSize( &pAwayTeamMembers->eaMembers );

		for ( i = 0; i < iAwayTeamSize; i++ )
		{
			//count the number of players
			if ( pAwayTeamMembers->eaMembers[i]->eEntType == GLOBALTYPE_ENTITYPLAYER )
				s_pAwayTeamData->iPlayerCount++;

			//see if the server has deemed that the player has opted-in
			if (	pAwayTeamMembers->eaMembers[i]->eEntType == GLOBALTYPE_ENTITYPLAYER
				&&	pAwayTeamMembers->eaMembers[i]->iEntID == entGetContainerID( pEnt ) )
			{
				s_pAwayTeamData->bIsReady = pAwayTeamMembers->eaMembers[i]->bIsReady;
			}
		}

		s_pAwayTeamData->pAwayTeamMembers = StructClone( parse_AwayTeamMembers, pAwayTeamMembers );
	}
}

static bool s_bShowAwayTeamPicker = false;
static bool s_bShowMapTransferChoice = false;
static bool s_bShowTeamCorralWindow = false;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_ShouldAwayTeamPickerShow);
bool team_ShouldAwayTeamPickerShow()
{
	return s_bShowAwayTeamPicker;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_ShouldMapTransferChoiceShow);
bool team_ShouldMapTransferChoiceShow()
{
	return s_bShowMapTransferChoice;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_ShouldTeamCorralWindowShow);
bool team_ShouldTeamCorralWindowShow()
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(entActivePlayerPtr());
	if (pInfo)
	{
		if(!pInfo->bHasTeamCorral && s_partyCircleFx && timeMsecsSince2000() - s_timePCFxLastUpdated > 1500)
		{
			dtFxKill(s_partyCircleFx);
			s_partyCircleFx = 0;
		}
		return pInfo->bHasTeamCorral;
	}

	return s_bShowTeamCorralWindow;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void teamShowAwayTeamPicker(Entity *pEnt, S32 iTimer, AwayTeamMembers* pAwayTeamMembers, bool bAllInCircle)
{
	if (!pEnt || !pEnt->pPlayer)
	{
		return;
	}

	if(!gbNoGraphics)
	{
		s_bShowAwayTeamPicker = true;
		s_bShowMapTransferChoice = false;
	}

	gclTeam_CreateAwayTeamData(pEnt, iTimer, pAwayTeamMembers, bAllInCircle);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void team_SetEntInCircle(Entity *pEnt, bool bInCircle)
{
	s_bInCircle = bInCircle;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void team_hidePartyCircle(Entity *pEnt)
{
	if(s_partyCircleFx)
	{
		dtFxKill(s_partyCircleFx);
		s_partyCircleFx = 0;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void team_showPartyCircle(Entity *pEnt, Vec3 v3Pos, F32 fRadius, const char *pchPartyCircleFx)
{
	DynFxManager *pManager = dynFxGetGlobalFxManager(v3Pos);

	team_hidePartyCircle(pEnt);

	if (pManager)
	{
		DynParamBlock *pFxParams;
		DynDefineParam *pParam;
		static Vec3 vecScale;
		dtNode node = dtNodeCreate();

		dtNodeSetPos(node, v3Pos);

		if (fRadius > 0.0f)
		{
			pFxParams = dynParamBlockCreate();

			// Scale
			pParam = StructAlloc(parse_DynDefineParam);
			pParam->pcParamName = allocAddString("RadiusParam");
			setVec3(vecScale, fRadius, fRadius, fRadius);
			MultiValSetVec3(&pParam->mvVal, &vecScale);
			eaPush(&pFxParams->eaDefineParams, pParam);

			s_partyCircleFx = dtAddFx(pManager->guid, 
				(pchPartyCircleFx && pchPartyCircleFx[0] ? pchPartyCircleFx : "Fx_Placeholder_Splat_Circle_Self"),
				pFxParams, 0, node, 
				1.f, 0, NULL, eDynFxSource_UI, NULL, NULL);
			s_timePCFxLastUpdated = timeMsecsSince2000();
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void teamShowTeamCorralWindow(Entity *pEnt, S32 iTimer, bool bAllInCircle)
{
	if (!gbNoGraphics)
	{
		s_bShowTeamCorralWindow = true;
	}

	gclTeam_CreateAwayTeamData(pEnt, iTimer, NULL, bAllInCircle);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void teamUpdateTeamCorralData(Entity *pEnt, S32 iTimer, bool bAllInCircle)
{
	gclTeam_CreateAwayTeamData(pEnt, iTimer, NULL, bAllInCircle);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void teamHideTeamCorralWindow()
{
	s_bShowTeamCorralWindow = false;

	StructDestroySafe( parse_AwayTeamData, &s_pAwayTeamData );
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void teamShowMapTransferChoice(Entity *pEnt, S32 iTimer, AwayTeamMembers* pAwayTeamMembers, bool bAllInCircle)
{
	if (!pEnt || !pEnt->pPlayer)
	{
		return;
	}

	if(!gbNoGraphics )
	{
		s_bShowAwayTeamPicker = false;
		s_bShowMapTransferChoice = true;
	}

	gclTeam_CreateAwayTeamData(pEnt, iTimer, pAwayTeamMembers, bAllInCircle);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD;
void teamHideMapTransferChoice( Entity *pEnt )
{
	s_bShowAwayTeamPicker = false;
	s_bShowMapTransferChoice = false;

	StructDestroySafe( parse_AwayTeamData, &s_pAwayTeamData );
}


static bool StubTeamMemberData_Fill(TeamMemberData **ppData, StubTeamMember *pStubTeamMember)
{
	TeamMemberData *d = (*ppData);

	if(!pStubTeamMember || !d)
	{
		return false;
	}

	d->pEnt = NULL;
	if ( d->pcName )
	{
		StructFreeString( d->pcName );
	}
	d->iEntID = pStubTeamMember->iEntID;
	d->pcName = StructAllocString(pStubTeamMember->pcName);
	d->pcAccount = StructAllocString(pStubTeamMember->pcAccountHandle);
	d->pcClassName = pStubTeamMember->pchClassName;

	d->bDisconnected = true;
	
	d->iLevel = 0;
	d->iCombatLevel = 0;
	d->bNonLocal = false;
	d->bDead = false;
	d->bIsPet = false;
	d->bIsTalking = false;
	return true;
}


static bool TeamMemberData_Fill(TeamMemberData **ppData, Entity *e, bool bNonLocal, bool bIsTalking)
{
	Entity *pLocalEnt = entActivePlayerPtr();
	TeamMemberData *d = (*ppData);
	Team* pTeam = team_GetTeam(pLocalEnt);
	TeamMember *pMember = team_FindMember(pTeam, e);
	const char *pcName;

	if (!d)
		return false;

	if (!e) {
		if (d->pEnt || d->pcStatus || d->pcName || d->pcAccount || d->pcLocation)
			StructReset(parse_TeamMemberData, d);
		return false;
	}

	d->pEnt = e;
	if (e->pChar)
		e->pChar->pEntParent = e;

	pcName = entGetLocalName(e);
	if (pcName) {
		ANALYSIS_ASSUME(pcName);
		if ((!d->pcName) || strcmp(pcName, d->pcName)) {
			StructCopyString( &d->pcName, pcName );
		}
	}
	else {
		if (d->pcName) {
			StructCopyString(&d->pcName, NULL); // this frees the string
		}
	}
	d->pcAccount = e->pPlayer ? e->pPlayer->publicAccountName : "";

	if (pMember) {
		Entity *pMemberEnt = GET_REF(pMember->hEnt);
		const char* pchMapDisplayName = gclRequestMapDisplayName(pMember->pcMapMsgKey);

		if (!pchMapDisplayName || !pchMapDisplayName[0]) {
			pchMapDisplayName = langTranslateMessageKey(entGetLanguage(e), "TeamStatus_UnknownMap");
		}
		if (pMember->iMapInstanceNumber) {
			if (d->pcLocation) {
				estrClear(&d->pcLocation);
			}
			entFormatGameMessageKey(e, &d->pcLocation, "TeamStatus_MapNameWithNumber",
				STRFMT_STRING("MapName", pchMapDisplayName),
				STRFMT_INT("Number", pMember->iMapInstanceNumber),
				STRFMT_END);
		} else {
			estrPrintf(&d->pcLocation, "%s", pchMapDisplayName);
		}

		d->iLevel = entity_GetSavedExpLevel(pMemberEnt);
		if (!pMember->bSidekicked && !bNonLocal) {
			d->iCombatLevel = e->pChar ? e->pChar->iLevelCombat : d->iLevel;
		} else if (pMember->bSidekicked) {
			TeamMember* pChampion = team_FindChampion(pTeam);
			d->iCombatLevel = pChampion ? pChampion->iExpLevel : d->iLevel;
		} else {
			d->iCombatLevel = d->iLevel;
		}

		d->iOfficerRank = pMember->iOfficerRank;
		d->pcStatus = pMember->pcStatus;
		d->pcClassName = pMember->pchClassName;
	}

	d->bNonLocal = bNonLocal;
	d->bDead = entCheckFlag(e, ENTITYFLAG_DEAD);
	d->bDisconnected = entCheckFlag(e, ENTITYFLAG_PLAYER_DISCONNECTED);
	d->bIsPet = false;
	d->iEntID = e->myContainerID;
	d->bIsTalking = bIsTalking;
	return true;
}

static void team_GetMemberStateAndStance(SA_PARAM_NN_VALID TeamMemberData *pMemberData, SA_PARAM_OP_VALID PlayerPetInfo *pPetInfo)
{
	const char *state;
	const char *stance;

	if (pPetInfo)
	{
		PetStanceInfo *pStanceInfo = eaGet(&pPetInfo->eaStances, PetStanceType_PRECOMBAT);

		state = pPetInfo->curPetState;
		stance = (pStanceInfo) ? pStanceInfo->curStance : "";
	}
	else
	{
		stance = state = "";
	}


	if (!strnicmp(state,"Setmytarget",11))
	{
		pMemberData->iAiState = 1;
	}
	else if (!strnicmp(state,"Follow",6))
	{
		pMemberData->iAiState = 4;
	}
	else if (!strnicmp(state,"Hold",8))
	{
		pMemberData->iAiState = 5;
	}

	if (!strnicmp(stance,"aggressive",10))
	{
		pMemberData->iAiStance = 2;
	}
	else if (!strnicmp(stance,"defensive",9))
	{
		pMemberData->iAiStance = 3;
	}
	else if (!strnicmp(stance,"avoid",5))
	{
		pMemberData->iAiStance = 1;
	}
}

static S32 team_GetMembers(SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pPlayer, int includePets, bool bExcludePlayer)
{
	static Entity** s_eaTeamEnts = NULL;
	static TeamMember** s_eaTeamMembers = NULL;
	static StubTeamMember** s_eaStubTeamMembers = NULL;
	TeamMemberData*** peaData = pGen ? ui_GenGetManagedListSafe(pGen, TeamMemberData) : NULL;
	int i, j;
	int iCount = 0;

	eaClearFast(&s_eaTeamEnts);
	eaClearFast(&s_eaTeamMembers);
	eaClearFast(&s_eaStubTeamMembers);
	iCount = team_GetTeamListSelfFirst(pPlayer, &s_eaTeamEnts, &s_eaTeamMembers, !bExcludePlayer, includePets);


	if (pGen)
	{
		for (i = 0; i < iCount; i++)
		{
			Entity* pEnt = s_eaTeamEnts[i];
			TeamMember* pMember = s_eaTeamMembers[i];
			TeamMemberData* pMemberData = eaGetStruct(peaData, parse_TeamMemberData, i);
			bool bNonLocal = !entGetRef(pEnt);
			bool bTeamMemberTalking = pMember ? team_IsTeamMemberTalking(pMember) : false;

			TeamMemberData_Fill(&pMemberData, pEnt, bNonLocal, bTeamMemberTalking);
			pMemberData->bIsPet = (entGetType(pEnt) == GLOBALTYPE_ENTITYSAVEDPET || entGetType(pEnt) == GLOBALTYPE_ENTITYCRITTER);
			pMemberData->iAiState = 0;
			pMemberData->iAiStance = 0;

			if (pMember)
			{
				pMemberData->bSidekicked = pMember->bSidekicked;
				pMemberData->iExpLevel = pMember->iExpLevel;
			}
			else
			{
				pMemberData->bSidekicked = false;
				pMemberData->iExpLevel = 0;
			}

			if (pMemberData->bIsPet && pEnt->pSaved && pEnt->pSaved->conOwner.containerID == entGetContainerID(pPlayer))
			{
				for (j = eaSize(&pPlayer->pPlayer->petInfo)-1; j >= 0; j--)
				{
					if (pPlayer->pPlayer->petInfo[j]->iPetRef == pEnt->myRef)
					{
						break;
					}
				}
				if (j >= 0)
				{
					team_GetMemberStateAndStance(pMemberData, pPlayer->pPlayer->petInfo[j]);
				}
			}
		}

		if (gConf.bManageTeamDisconnecteds)
		{
			int iStubCount=0;
			
			int iDisconnectedCount = team_GetTeamStubMembers(pPlayer, &s_eaStubTeamMembers);
			for (iStubCount=0;iStubCount<iDisconnectedCount;iStubCount++)
			{
				StubTeamMember* pStubMember = s_eaStubTeamMembers[iStubCount];
				TeamMemberData* pMemberData = eaGetStruct(peaData, parse_TeamMemberData, iCount+iStubCount);
				
				StubTeamMemberData_Fill(&pMemberData, pStubMember);
			}

			iCount+=iDisconnectedCount;
		}
	}

	if (pGen)
	{
		eaSetSizeStruct(peaData, parse_TeamMemberData, iCount);
		ui_GenSetManagedListSafe(pGen, peaData, TeamMemberData, true);
	}
	return iCount;
}

static bool TeamUpMemberData_Fill(TeamMemberData **ppData, Entity *e, bool bNonLocal, bool bIsTalking)
{
	Entity *pLocalEnt = entActivePlayerPtr();
	TeamMemberData *d = (*ppData);

	if(!e || !d) {
		return false;
	}

	d->pEnt = e;
	if (e->pChar)
		e->pChar->pEntParent = e;
	if ( d->pcName )
		StructFreeString( d->pcName );
	d->pcName = StructAllocString(entGetLocalName(e));
	d->pcAccount = SAFE_MEMBER(e,pPlayer) ? e->pPlayer->publicAccountName : "";
	d->iLevel = entity_GetSavedExpLevel(e);
	d->iCombatLevel = e->pChar ? e->pChar->iLevelCombat : d->iLevel;
	d->bNonLocal = bNonLocal;
	d->bDead = entCheckFlag(e, ENTITYFLAG_DEAD);
	d->bIsPet = false;
	d->iEntID = e->myContainerID;
	d->bIsTalking = bIsTalking;
	return true;
}

S32 TeamUp_GetGroups(UIGen *pGen, Entity *pPlayer, bool bIncludePlayer, bool bAddEmptyGroup)
{
	TeamUpGroupData*** peaData = pGen ? ui_GenGetManagedListSafe(pGen, TeamUpGroupData) : NULL;
	int i,iCount = 0;
	int iActual = 0;

	if(pPlayer && pPlayer->pTeamUpRequest && pPlayer->pTeamUpRequest->eState != kTeamUpState_Invite)
	{	
		

		iCount = eaSize(&pPlayer->pTeamUpRequest->ppGroups);

		for(i=0;i<iCount;i++)
		{
			TeamUpGroup *pGroup = pPlayer->pTeamUpRequest->ppGroups[i];
			if(bIncludePlayer || (U32)pGroup->iGroupIndex != pPlayer->pTeamUpRequest->iGroupIndex)
			{
				TeamUpGroupData* pMemberData = eaGetStruct(peaData, parse_TeamUpGroupData, iActual);
				

				pMemberData->bFullGroup = eaSize(&pGroup->ppMembers) >= gConf.iMaxTeamUpGroupSize;
				pMemberData->iGroupIndex = pGroup->iGroupIndex;

				pMemberData->bMyGroup = (U32)pGroup->iGroupIndex == pPlayer->pTeamUpRequest->iGroupIndex;
				pMemberData->bEmptyGroup = false;

				iActual++;
			}	
		}

		if(bAddEmptyGroup)
		{
			TeamUpGroupData* pMemberData = eaGetStruct(peaData, parse_TeamUpGroupData, iActual);

			pMemberData->bFullGroup = false;
			pMemberData->iGroupIndex = -2; //so Not to confuse with gathering information about a players current group

			pMemberData->bMyGroup = false;
			pMemberData->bEmptyGroup = true;
			iActual++;
		}
	}

	if (pGen)
	{
		eaSetSizeStruct(peaData, parse_TeamUpGroupData, iActual);
		ui_GenSetManagedListSafe(pGen, peaData, TeamUpGroupData, true);
	}

	return iCount;
}


S32 TeamUp_GetMembers(UIGen *pGen, Entity *pPlayer, int iGroupIdx, int includePets, bool bExcludePlayer)
{
	static Entity** s_eaTeamEnts = NULL;
	static TeamUpMember** s_eaTeamMembers = NULL;
	TeamMemberData*** peaData = pGen ? ui_GenGetManagedListSafe(pGen, TeamMemberData) : NULL;
	int i;
	int iCount = 0;

	eaClearFast(&s_eaTeamEnts);
	eaClearFast(&s_eaTeamMembers);
	iCount = TeamUp_GetTeamListSelfFirst(pPlayer, &s_eaTeamEnts, &s_eaTeamMembers, iGroupIdx, !bExcludePlayer, includePets);

	if (pGen)
	{
		for (i = 0; i < iCount; i++)
		{
			Entity* pEnt = s_eaTeamEnts[i];
			TeamUpMember* pMember = s_eaTeamMembers[i];
			TeamMemberData* pMemberData = eaGetStruct(peaData, parse_TeamMemberData, i);
			bool bNonLocal = !entGetRef(pEnt);
			bool bTeamMemberTalking = false; //pMember ? team_IsTeamMemberTalking(pMember) : false;

			TeamUpMemberData_Fill(&pMemberData, pEnt, bNonLocal, bTeamMemberTalking);
			pMemberData->bIsPet = (entGetType(pEnt) == GLOBALTYPE_ENTITYSAVEDPET || entGetType(pEnt) == GLOBALTYPE_ENTITYCRITTER);
			pMemberData->iAiState = 0;
			pMemberData->iAiStance = 0;

			/*
			if (pMemberData->bIsPet && pEnt->pSaved && pEnt->pSaved->conOwner.containerID == entGetContainerID(pPlayer))
			{
				for (j = eaSize(&pPlayer->pPlayer->petInfo)-1; j >= 0; j--)
				{
					if (pPlayer->pPlayer->petInfo[j]->iPetRef == pEnt->myRef)
					{
						break;
					}
				}
				if (j >= 0)
				{
					//team_GetMemberStateAndStance(pMemberData, pPlayer->pPlayer->petInfo[j]);
				}
			}
			*/
		}
	}

	if (pGen)
	{
		eaSetSizeStruct(peaData, parse_TeamMemberData, iCount);
		ui_GenSetManagedListSafe(pGen, peaData, TeamMemberData, true);
	}

	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetMemberCount);
S32 teamExpr_GetMemberCount(SA_PARAM_OP_VALID Entity *pPlayer, int includePets)
{
	return team_GetMembers(NULL,pPlayer,includePets,true);
}

//useLastName and MaxLen no longer do anything
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetMemberListEx);
void teamExpr_GetMemberListEx(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pPlayer, int includePets, int useLastName, int MaxLen)
{
	team_GetMembers(pGen,pPlayer,includePets,true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetMemberList);
void teamExpr_GetMemberList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pPlayer)
{
	team_GetMembers(pGen,pPlayer,0,true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetMemberListFull);
void teamExpr_GetMemberListFull(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pPlayer)
{
	team_GetMembers(pGen,pPlayer,0,false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_FindMemberIndexFromRef);
S32 teamExpr_FindMemberIndexFromRef(SA_PARAM_NN_VALID UIGen *pGen, U32 erEnt)
{
	TeamMemberData ***peaData = ui_GenGetManagedListSafe(pGen, TeamMemberData);
	S32 i;

	for ( i = eaSize(peaData)-1; i >= 0; i-- )
	{
		TeamMemberData* pMember = (*peaData)[i];

		if ( pMember && entGetRef(pMember->pEnt) == erEnt )
		{
			return i;
		}
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetOnMapMembers);
void team_GetOnMapMembers(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pPlayer)
{
	TeamMemberData*** peaData = ui_GenGetManagedListSafe(pGen, TeamMemberData);
    EntityRef *refs = NULL;
	Team *team = team_GetTeam(pPlayer);
    int i,n,c = 0;

    if(!team)
    {
        ui_GenSetList(pGen,NULL,parse_TeamMemberData);
        return;
    }

    team_GetOnMapEntRefs(PARTITION_CLIENT, &refs,team);

    n = ea32Size(&refs);
    for(i = 0; i < n; ++i)
    {
        Entity *e = entFromEntityRefAnyPartition(refs[i]);
		if ( e )
		{
			TeamMemberData* pMemberData = eaGetStruct(peaData, parse_TeamMemberData, c++);
			TeamMemberData_Fill(&pMemberData, e, false, false);
		}
    }

	while (eaSize(peaData) > c)
		StructDestroy(parse_TeamMemberData, eaPop(peaData));

    ui_GenSetManagedListSafe(pGen,peaData, TeamMemberData,true);
}

/*AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UpdatePetList);
void UpdatePetList()											// this updates the list of the player's pets with the team.  It's meant to be called
{																// once every 5 seconds or so
	Entity* pPlayer = entActivePlayerPtr();
	if (pPlayer && pPlayer->pTeam)
	{
		ServerCmd_gslTeam_cmd_UpdateListOfPets();
	}
}*/

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_OnSameMap);
bool teamExpr_OnSameMap(SA_PARAM_NN_VALID Entity *pTeamMate)
{
	Entity *pEnt = entActivePlayerPtr();
	SavedMapDescription *pPlayerMap = entity_GetLastMap(pEnt);
	SavedMapDescription *pTeamMateMap = entity_GetLastMap(pTeamMate);
	if (pPlayerMap && pTeamMateMap) {
		if (!stricmp(pPlayerMap->mapDescription, pTeamMateMap->mapDescription)) {
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_InviteEnt);
void teamExpr_InviteEnt(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		ServerCmd_Team_InviteByID(pEnt->myContainerID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_InviteRef);
void teamExpr_InviteRef(EntityRef iEntRef)
{
	Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
	if (pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		ServerCmd_Team_InviteByID(pEnt->myContainerID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_InviteID);
void teamExpr_InviteID(ContainerID iContainerID)
{
	ServerCmd_Team_InviteByID(iContainerID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_RequestEnt);
void teamExpr_RequestEnt(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		ServerCmd_Team_RequestByID(pEnt->myContainerID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_RequestRef);
void teamExpr_RequestRef(EntityRef iEntRef)
{
	Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
	if (pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		ServerCmd_Team_RequestByID(pEnt->myContainerID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_RequestID);
void teamExpr_RequestID(ContainerID iContainerID)
{
	ServerCmd_Team_RequestByID(iContainerID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_KickEnt);
void teamExpr_KickEnt(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		ServerCmd_Team_KickByID(pEnt->myContainerID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_KickRef);
void teamExpr_KickRef(EntityRef iEntRef)
{
	Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
	if (pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		ServerCmd_Team_KickByID(pEnt->myContainerID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_KickID);
void teamExpr_KickID(ContainerID iContainerID)
{
	ServerCmd_Team_KickByID(iContainerID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_PromoteEnt);
void teamExpr_PromoteEnt(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		ServerCmd_Team_PromoteByID(pEnt->myContainerID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_PromoteRef);
void teamExpr_PromoteRef(EntityRef iEntRef)
{
	Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
	if (pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		ServerCmd_Team_PromoteByID(pEnt->myContainerID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_PromoteID);
void teamExpr_PromoteID(ContainerID iContainerID)
{
	ServerCmd_Team_PromoteByID(iContainerID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_SetChampionEnt);
void teamExpr_SetChampionEnt(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		ServerCmd_Team_SetChampionByID(pEnt->myContainerID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_SetChampionRef);
void teamExpr_SetChampionRef(EntityRef iEntRef)
{
	Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
	if (pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		ServerCmd_Team_SetChampionByID(pEnt->myContainerID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_SetChampionID);
void teamExpr_SetChampionID(ContainerID iContainerID)
{
	ServerCmd_Team_SetChampionByID(iContainerID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_SetSidekicking);
void teamExpr_SetSidekicking(bool bSidekicking)
{
	ServerCmd_Team_Sidekicking(bSidekicking);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_ToggleSidekicking);
void teamExpr_ToggleSidekicking(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);
	if (pTeam) {
		TeamMember *pMember = team_FindMember(pTeam, pPlayer);
		if (pMember) {
			ServerCmd_Team_Sidekicking(!pMember->bSidekicked);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsSidekicking);
bool teamExpr_IsSidekicking(void)
{
	Entity* pPlayer = entActivePlayerPtr();
	Team* pTeam = team_GetTeam(pPlayer);
	TeamMember* pMember = team_FindMember(pTeam, pPlayer);
	if (pMember) {
		return pMember->bSidekicked;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_EntIsSidekicking);
bool teamExpr_EntIsSidekicking(SA_PARAM_OP_VALID Entity* pEnt)
{
	Entity* pPlayer = entActivePlayerPtr();
	if (team_OnSameTeam(pPlayer, pEnt)) {
		Team* pTeam = team_GetTeam(pPlayer);
		TeamMember* pMember = team_FindMember(pTeam, pEnt);
		if (pMember) {
			return pMember->bSidekicked;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_EntGetLevelExp);
S32 teamExpr_EntGetLevelExp(SA_PARAM_OP_VALID Entity* pEnt)
{
	Entity* pPlayer = entActivePlayerPtr();
	if (pPlayer && pPlayer == pEnt) {
		return entity_GetSavedExpLevel(pEnt);
	}
	if (team_OnSameTeam(pPlayer,pEnt)) {
		Team* pTeam = team_GetTeam(pPlayer);
		TeamMember* pMember = team_FindMember(pTeam, pEnt);
		if (pMember) {
			return pMember->iExpLevel;
		}
	}
	if (pEnt && pEnt->pChar) {
		return pEnt->pChar->iLevelCombat;
	}
	return 0;
}



/////////////////////////////////////////////////////////////////////////////////////////////
//  Can Invite

// Root function that has all pertinent restrictions. We can take either a TargetEnt, a ChatPlayerStruct or a ContainerID
//    The Entity takes precedence, followed by the ChatPlayerStruct. We only use the TargetID for minimal checking if the other two do not exist.
// This defaults to permissive rather than restrictive. We rely on the eventual server commands to deal with unknown cases.
static bool teamCanInviteBase(Entity *pPlayer, Entity *pTargetEnt, ChatPlayerStruct *pChatPlayerStruct, ContainerID iTargetID)
{
	Team* pTeam = team_GetTeam(pPlayer);		// May be NULL. We may be inviting when soloing in order to form a team
	int iTargetLevel=-1;
	ContainerID iTargetTeamID=0;
	TeamMode eTargetLFGMode=TeamMode_Open;		// Team Mode of Target for Request, LFG Mode of Target for Invite

	if (pPlayer==NULL || pPlayer->pChar==NULL)
	{
		return(false);
	}
	
	if (pTeam!=NULL && pPlayer->pTeam->bMapLocal)
	{
		// There was a team and it's MapLocal
		return(false);
	}

	if (pTargetEnt!=NULL)
	{
		// A couple of checks that we can only do on entities.
		
		// If we are an Entity make sure it is a player. For ChatPlayerStruct it is assumed that those HAVE to be players.
		if (pTargetEnt->myEntityType != GLOBALTYPE_ENTITYPLAYER)
		{
			return(false);
		}

		//The ChatPlayerStructure has allegience but not faction. If we eventually want, we can add faction to the struct.
		//  For now, at worst, we will allow the UI to display the option, and rely on the actual Invite on the server to deal with the restriction.

		if (!team_TeamOnCompatibleFaction(pPlayer, pTargetEnt))
		{
			return(false);
		}

		// Set up the level, teamid, lfgmode

		if (pTargetEnt->pChar!=NULL)
		{
			iTargetLevel = pPlayer->pChar->iLevelCombat;
		}

		if (pTargetEnt->pTeam!=NULL)
		{
			iTargetTeamID = pTargetEnt->pTeam->iTeamID;
		}

		if (pTargetEnt->pPlayer!=NULL)
		{
			eTargetLFGMode=pTargetEnt->pPlayer->eLFGMode;
		}

		iTargetID = pTargetEnt->myContainerID;
	}
	else if (pChatPlayerStruct!=NULL)
	{
		// ChatPlayer specific check
		
		if (!ChatIsPlayerInfoSameShard(&pChatPlayerStruct->pPlayerInfo))
		{
			// Do not allow inviting people from other shards
			return(false);
		}

		// Set up the level, teamid, lfgmode

		iTargetID = pChatPlayerStruct->pPlayerInfo.onlineCharacterID;
		iTargetLevel = pChatPlayerStruct->pPlayerInfo.iPlayerLevel;
		iTargetTeamID = pChatPlayerStruct->pPlayerInfo.iPlayerTeam;
		eTargetLFGMode = pChatPlayerStruct->pPlayerInfo.eLFGMode;
	}
	else
	{
		// Coming in from possibly a non-local team member or guild member. At least make sure they're not already in our team or disconnected list
		if (iTargetID!=0 && pTeam!=NULL && (team_FindMemberID(pTeam, iTargetID)!=NULL || team_FindDisconnectedStubMemberID(pTeam, iTargetID)!=NULL))
		{
			return(false);
		}
	}

	// Generic checks

	// Can't invite self
	if (iTargetID==pPlayer->myContainerID)
	{
		return(false);
	}

	// See if the target is already associated with a team
	if (iTargetTeamID!=0)
	{
		return(false);
	}

	// Target LFG Mode
	if (eTargetLFGMode != TeamMode_RequestOnly && eTargetLFGMode != TeamMode_Open)
	{
		return(false);
	}

	// Level checks
	if (pPlayer->pChar->iLevelCombat < gConf.iMinimumTeamLevel || (iTargetLevel >=0 && iTargetLevel < gConf.iMinimumTeamLevel))
	{
		return(false);
	}

	return(true);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CanInviteEnt);
bool teamExpr_CanInviteEnt(SA_PARAM_NN_VALID Entity *pTeamMate)
{
	Entity *pPlayer = entActivePlayerPtr();

	return(teamCanInviteBase(pPlayer, pTeamMate, NULL, 0));
}
		

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CanInviteRef);
bool teamExpr_CanInviteRef(EntityRef iEntRef)
{
	Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
	return pEnt ? teamExpr_CanInviteEnt(pEnt) : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CanInviteID);
bool teamExpr_CanInviteID(ContainerID iContainerID)
{
	Entity *pPlayer = entActivePlayerPtr();
	Entity *pInviteEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iContainerID);
	ChatPlayerStruct *pChatPlayerStruct = FindChatPlayerByPlayerID(iContainerID);

	return(teamCanInviteBase(pPlayer,pInviteEnt,pChatPlayerStruct,iContainerID));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CanInviteHandle);
bool teamExpr_CanInviteHandle(const char *pchHandle)
{
	ContainerID iEntID = 0;
	ResolveKnownEntityID(entActivePlayerPtr(), pchHandle, NULL, NULL, &iEntID);
	return iEntID ? teamExpr_CanInviteID(iEntID) : false;
}

// Do the best we can with an Id OR a Handle. Try the ID stuff first since it is more efficient.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CanInviteIdOrHandle);
bool teamExpr_CanInviteIdOrHandle(ContainerID iContainerID, const char *pchHandle)
{
	Entity *pPlayer = entActivePlayerPtr();
	
	// Try the container first
	if (iContainerID!=0)
	{
		Entity *pInviteEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iContainerID);
		ChatPlayerStruct *pChatPlayerStruct = FindChatPlayerByPlayerID(iContainerID);

		// If we can get anything from the ID use that.
		if (pInviteEnt!=NULL || pChatPlayerStruct!=NULL)
		{
			return(teamCanInviteBase(pPlayer,pInviteEnt,pChatPlayerStruct,iContainerID));
		}
	}

	// No luck with the ID. Try to resolve the handle
	{
		ContainerID iEntID = 0;
		ResolveKnownEntityID(entActivePlayerPtr(), pchHandle, NULL, NULL, &iEntID);
		if (iEntID!=0)
		{
			// Do the ID invite
			return(teamExpr_CanInviteID(iContainerID));
		}
	}

	// No luck with the handle either. Just do the most basic ID check

	return(teamCanInviteBase(pPlayer,NULL,NULL,iContainerID));
}

/////////////////////////////////////////////////////////////////////////////////////////////
//  Can Request

// Root function that has all pertinent restrictions. We can take either a TargetEnt or a ContainerID that we can search in the ChatPlayerStructs. The Entity takes precedence
// This defaults to permissive rather than restrictive. We rely on the eventual server commands to deal with unknown cases.
static bool teamCanRequestBase(Entity *pPlayer, Entity *pTargetEnt, ChatPlayerStruct *pChatPlayerStruct, ContainerID iTargetID)
{
	int iTargetLevel=-1;
	TeamMode eTargetTeamMode=TeamMode_Open;		// Team Mode of Target for Request, LFG Mode of Target for Invite

	if (pPlayer==NULL || pPlayer->pChar==NULL)
	{
		return(false);
	}
	
	if (team_IsWithTeam(pPlayer))
	{
		// Player already on (or associated with) a team. Can't request to join another
		return(false);
	}

	if (pTargetEnt!=NULL)
	{
		// A few of checks that we can only do on entities.
		
		// If we are an Entity make sure it is a player. For ChatPlayerStruct it is assumed that those HAVE to be players.
		if (pTargetEnt->myEntityType != GLOBALTYPE_ENTITYPLAYER)
		{
			return(false);
		}

		//The ChatPlayerStructure has allegience but not faction. If we eventually want, we can add faction to the struct.
		//  For now, at worst, we will allow the UI to display the option, and rely on the actual Request on the server to deal with the restriction.

		if (!team_TeamOnCompatibleFaction(pPlayer, pTargetEnt))
		{
			return(false);
		}

		// For ChatPlayerStruct we can't get the target team from just the ID. But we can do the check for entities
		
		if (!team_IsMember(pTargetEnt) || pTargetEnt->pTeam->bMapLocal)
		{
			// No player, they are not a member of a team, or the team is MapLocal
			return(false);
		}
		
		// Set up the level, lfgmode

		if (pTargetEnt->pChar!=NULL)
		{
			iTargetLevel = pPlayer->pChar->iLevelCombat;
		}

		// We would like to check the eMode of the team the Target is on to see if it allows joins.
		//   The hTeam on the pTeam may not exist, though. Be permissive
//		{
//			Team* pTeam = team_GetTeam(pPlayer);
//			eTargetTeamMode=pTeam->eMode;
//		}

		iTargetID = pTargetEnt->myContainerID;
	}
	else if (pChatPlayerStruct!=NULL)
	{
		// ChatPlayer specific check
		
		if (!ChatIsPlayerInfoSameShard(&pChatPlayerStruct->pPlayerInfo))
		{
			// Do not allow requesting to people from other shards
			return(false);
		}

		if (pChatPlayerStruct->pPlayerInfo.iPlayerTeam==0)
		{
			// Target is not on a team. No request
			return(false);
		}

		// Set up the level, lfgmode
		
		iTargetID = pChatPlayerStruct->pPlayerInfo.onlineCharacterID;
		iTargetLevel = pChatPlayerStruct->pPlayerInfo.iPlayerLevel;
		eTargetTeamMode = pChatPlayerStruct->pPlayerInfo.eTeamMode;
	}
	else
	{
		// Can't do much here. For invites, we know the team that we're on and can do additional checks. For requesting, we can't know what the target team
		// is so can't check.
		// For requests we want to be a little more exclusive than invites. If we can't confirm that we're requesing to get onto a team, then don't allow the request.
		return(false);
	}

	// Generic checks
	
	// Can't request self
	if (iTargetID==pPlayer->myContainerID)
	{
		return(false);
	}

	// Target Team Mode
	if (eTargetTeamMode != TeamMode_RequestOnly && eTargetTeamMode != TeamMode_Open)
	{
		return(false);
	}

	// Level checks
	if (pPlayer->pChar->iLevelCombat < gConf.iMinimumTeamLevel || (iTargetLevel >=0 && iTargetLevel < gConf.iMinimumTeamLevel))
	{
		return(false);
	}

	return(true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CanRequestEnt);
bool teamExpr_CanRequestEnt(SA_PARAM_NN_VALID Entity *pEnt)
{
	Entity *pPlayer = entActivePlayerPtr();
	return(teamCanRequestBase(pPlayer, pEnt, NULL, 0));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CanRequestRef);
bool teamExpr_CanRequestRef(EntityRef iEntRef)
{
	Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
	return pEnt ? teamExpr_CanRequestEnt(pEnt) : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CanRequestID);
bool teamExpr_CanRequestID(ContainerID iContainerID)
{
	Entity *pRequestEnt = entFromContainerIDAnyPartition( GLOBALTYPE_ENTITYPLAYER, iContainerID );
	Entity *pPlayer = entActiveOrSelectedPlayer();
	ChatPlayerStruct *pChatPlayerStruct = FindChatPlayerByPlayerID(iContainerID);

	return(teamCanRequestBase(pPlayer,pRequestEnt,pChatPlayerStruct,iContainerID));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CanRequestHandle);
bool teamExpr_CanRequestHandle(const char *pchHandle)
{
	ContainerID iEntID = 0;
	ResolveKnownEntityID(entActivePlayerPtr(), pchHandle, NULL, NULL, &iEntID);
	return iEntID ? teamExpr_CanRequestID(iEntID) : false;
}

// Do the best we can with an Id OR a Handle. Try the ID stuff first since it is more efficient.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CanRequestIdOrHandle);
bool teamExpr_CanRequestIdOrHandle(ContainerID iContainerID, const char *pchHandle)
{
	Entity *pPlayer = entActivePlayerPtr();
	
	// Try the container first
	if (iContainerID!=0)
	{
		Entity *pRequestEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iContainerID);
		ChatPlayerStruct *pChatPlayerStruct = FindChatPlayerByPlayerID(iContainerID);

		// If we can get anything from the ID use that.
		if (pRequestEnt!=NULL || pChatPlayerStruct!=NULL)
		{
			return(teamCanRequestBase(pPlayer,pRequestEnt,pChatPlayerStruct,iContainerID));
		}
	}

	// No luck with the ID. Try to resolve the handle
	{
		ContainerID iEntID = 0;
		ResolveKnownEntityID(entActivePlayerPtr(), pchHandle, NULL, NULL, &iEntID);
		if (iEntID!=0)
		{
			// Do the ID Request
			return(teamExpr_CanRequestID(iContainerID));
		}
	}

	// No luck with the handle either. Just do the most basic ID check

	return(teamCanRequestBase(pPlayer,NULL,NULL,iContainerID));
}

/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsMember);
bool teamExpr_IsMember(SA_PARAM_OP_VALID Entity *pEnt)
{
	return team_IsMember(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsInvitee);
bool teamExpr_IsInvitee(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (team_IsInvitee(pEnt))
	{
		Team *t = GET_REF(pEnt->pTeam->hTeam);
		if (t)
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsRequester);
bool teamExpr_IsRequester(SA_PARAM_OP_VALID Entity *pEnt)
{
	return team_IsRequester(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsModeNew);
bool teamExpr_IsModeNew(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	if (pPlayer) {
		Team *pTeam = team_GetTeam(pPlayer);
		if (pTeam) {
			return pTeam->eMode == TeamMode_Prompt;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsModeOpen);
bool teamExpr_IsModeOpen(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	if (pPlayer) {
		Team *pTeam = team_GetTeam(pPlayer);
		if (pTeam) {
			return pTeam->eMode == TeamMode_Open;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsModeClosed);
bool teamExpr_IsModeClosed(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	if (pPlayer) {
		Team *pTeam = team_GetTeam(pPlayer);
		if (pTeam) {
			return pTeam->eMode == TeamMode_Closed;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsModeRequest);
bool teamExpr_IsModeRequest(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	if (pPlayer) {
		Team *pTeam = team_GetTeam(pPlayer);
		if (pTeam) {
			return pTeam->eMode == TeamMode_RequestOnly;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetTopRequestType);
U32 teamExpr_GetTopRequestType(void)
{
	return GLOBALTYPE_ENTITYPLAYER;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetTopRequestID);
U32 teamExpr_GetTopRequestID(void)
{
	Entity *pPlayer = entActivePlayerPtr();

	Team *pTeam = team_GetTeam(pPlayer);
	if (pTeam && eaSize(&pTeam->eaRequests))
	{
		return pTeam->eaRequests[0]->iEntID;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetTopRequestName);
const char *teamExpr_GetTopRequestName(void)
{
	Entity *pPlayer = entActivePlayerPtr();

	Team *pTeam = team_GetTeam(pPlayer);
	if (pTeam && eaSize(&pTeam->eaRequests)) {
		return pTeam->eaRequests[0]->pcName;
	}

    return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetTopRequestHandle);
const char *teamExpr_GetTopRequestHandle(void)
{
	Entity *pPlayer = entActivePlayerPtr();

	Team *pTeam = team_GetTeam(pPlayer);
	if (pTeam && eaSize(&pTeam->eaRequests)) {
		return pTeam->eaRequests[0]->pcAccountHandle;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsOnMyTeamEnt);
bool teamExpr_IsOnMyTeamEnt(SA_PARAM_OP_VALID Entity *pTeamMate)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	return team_IsPlayerOrPetOnTeam(pPlayerEnt, pTeamMate);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsOnMyTeamRef);
bool teamExpr_IsOnMyTeamRef(EntityRef iEntRef)
{
	Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
	Entity *pPlayerEnt = entActivePlayerPtr();
	return team_IsPlayerOrPetOnTeam(pPlayerEnt, pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsOnMyTeamID);
bool teamExpr_IsOnMyTeamID(ContainerID iContainerID)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iContainerID);
	Entity *pPlayerEnt = entActivePlayerPtr();
	return pEnt ? team_OnSameTeam(pPlayerEnt, pEnt) : team_OnSameTeamID(pPlayerEnt, iContainerID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsOnMyTeamHandle);
bool teamExpr_IsOnMyTeamHandle(const char *pchHandle)
{
	Entity *pEnt = entActivePlayerPtr();
	U32 iEntID = 0;
	ResolveKnownEntityID(entActivePlayerPtr(), pchHandle, NULL, NULL, &iEntID);
	return iEntID ? team_OnSameTeamID(pEnt, iEntID) : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsOnMyTeamDisconnectedAccountAndName);
bool teamExpr_IsOnMyTeamDisconnectedAccountAndName(const char *pcAccount, const char *pcName)
{
	Entity *pEnt = entActivePlayerPtr();
	Team* pTeam = team_GetTeam(pEnt);
	const char * pcAccountNoAt = pcAccount;
	if (pcAccountNoAt[0]=='@')
	{
		// Skip the '@'
		pcAccountNoAt++;
	}
	return (team_FindDisconnectedStubMemberAccountAndName(pTeam, pcAccountNoAt, pcName)!=NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsTeamSpokesman);
bool teamExpr_IsTeamSpokesman(SA_PARAM_OP_VALID Entity *pEnt)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	return pPlayerEnt && pEnt && team_IsTeamSpokesmanBySelfTeam(pPlayerEnt, pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsTeamSpokesmanByHandle);
bool teamExpr_IsTeamSpokesmanByHandle(const char *pchHandle)
{
	return teamExpr_IsTeamSpokesman(ResolveKnownEntity(entActivePlayerPtr(), pchHandle));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsTeamLeader);
bool teamExpr_IsTeamLeader(SA_PARAM_OP_VALID Entity *pEnt)
{
	Entity *pPlayerEnt;
	Team *pTeam;

	pPlayerEnt = entActivePlayerPtr();

	pTeam = pPlayerEnt ? team_GetTeam(pPlayerEnt) : NULL;

	// Assume only players can be leaders, otherwise, pets will also show as leaders
	if (pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER && pTeam && pTeam->pLeader)
	{
		return pTeam->pLeader && entGetContainerID(pEnt) == pTeam->pLeader->iEntID;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsTeamChampionID);
bool teamExpr_IsTeamChampionID(ContainerID iEntID)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	return team_IsTeamChampionID(iEntID, team_GetTeam(pPlayerEnt));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsTeamChampion);
bool teamExpr_IsTeamChampion(SA_PARAM_NN_VALID Entity *pEnt)
{
	return teamExpr_IsTeamChampionID(entGetContainerID(pEnt));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsTeamChampionHandle);
bool teamExpr_IsTeamChampionHandle(const char *pchHandle)
{
	U32 iEntID = 0;
	ResolveKnownEntityID(entActivePlayerPtr(), pchHandle, NULL, NULL, &iEntID);
	return iEntID ? teamExpr_IsTeamChampionID(iEntID) : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("team_IsInteracting");
bool teamExpr_IsInteracting( SA_PARAM_NN_VALID Entity* pEnt )
{
	if ( team_IsInvitee(pEnt) || team_IsRequester( pEnt ) )
	{
		return true;
	}

	return false;
}

static Entity* team_AwayTeamGetPetOwner( AwayTeamMember* pAwayTeamMember,
										 AwayTeamMember** pOwnerMemberOut,
										 Entity** pPetEntOut )
{
	S32 i, iAwayTeamSize = eaSize(&s_pAwayTeamData->pAwayTeamMembers->eaMembers);

	for ( i = 0; i < iAwayTeamSize; i++ )
	{
		AwayTeamMember* pCurrMember = s_pAwayTeamData->pAwayTeamMembers->eaMembers[i];

		if ( pCurrMember->eEntType != GLOBALTYPE_ENTITYPLAYER )
			break;

		if (pAwayTeamMember->eEntType == GLOBALTYPE_ENTITYCRITTER)
		{
			if (pAwayTeamMember->iEntID == pCurrMember->iEntID)
			{
				if ( pOwnerMemberOut )
					(*pOwnerMemberOut) = pCurrMember;

				return entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,pCurrMember->iEntID);
			}
		}
		else
		{
			Entity* pPlayerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,pCurrMember->iEntID);
			Entity* pPetEnt = entity_GetSubEntity(PARTITION_CLIENT, pPlayerEnt,pAwayTeamMember->eEntType,pAwayTeamMember->iEntID);

			if ( pPetEnt )
			{
				if ( pPetEntOut )
					(*pPetEntOut) = pPetEnt;

				if ( pOwnerMemberOut )
					(*pOwnerMemberOut) = pCurrMember;

				return pPlayerEnt;
			}
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetAwayTeamList);
S32 teamExpr_GetAwayTeamList(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pPlayer, bool bShowPlayersOnly)
{
	AwayTeamMemberData ***peaData = ui_GenGetManagedListSafe(pGen, AwayTeamMemberData);
	S32 iCount = 0;

	if (	s_pAwayTeamData==NULL
		||	s_pAwayTeamData->pAwayTeamMembers==NULL )
	{
		ui_GenSetManagedListSafe(pGen, NULL, AwayTeamMemberData, true);
		return 0;
	}

	if (pPlayer)
	{
		S32 i, iAwayTeamSize = eaSize(&s_pAwayTeamData->pAwayTeamMembers->eaMembers);
		for ( i = 0; i < iAwayTeamSize; i++ )
		{
			AwayTeamMember* pAwayTeamMember = s_pAwayTeamData->pAwayTeamMembers->eaMembers[i];
			AwayTeamMember* pOwnerMember = NULL;
			Entity *pOwner = NULL;
			Entity *pMember = NULL;

			if ( pAwayTeamMember->eEntType != GLOBALTYPE_ENTITYPLAYER )
			{
				if (bShowPlayersOnly)
				{
					continue;
				}
				pOwner = team_AwayTeamGetPetOwner(pAwayTeamMember, &pOwnerMember, &pMember);
			}
			else
			{
				pMember = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,pAwayTeamMember->iEntID);
			}

			if ( pAwayTeamMember->eEntType == GLOBALTYPE_ENTITYPLAYER || (pOwner && pOwnerMember) )
			{
				Entity *pGroundEnt;
				CharacterClass* pClass;
				PetDef *pPetDef = NULL;
				CritterPetRelationship* pCritterRel = NULL;
				AwayTeamMemberData* pAwayTeamMemberData = eaGetStruct( peaData, parse_AwayTeamMemberData, iCount++ );

				if ( pAwayTeamMemberData->pcName )
					StructFreeString( pAwayTeamMemberData->pcName );
				if ( pAwayTeamMemberData->pcOwnerName )
					StructFreeString( pAwayTeamMemberData->pcOwnerName );

				pAwayTeamMemberData->pcName = NULL;
				pAwayTeamMemberData->pcOwnerName = NULL;

				if ( pAwayTeamMember->eEntType != GLOBALTYPE_ENTITYCRITTER )
				{
					if ( pMember )
					{
						pAwayTeamMemberData->pcName = StructAllocString(entGetLocalName(pMember));
					}
				}
				else
				{
					U32 uiCritterID = pAwayTeamMember->uiCritterPetID;
					if (pCritterRel = Entity_FindSavedCritterByID(pPlayer, uiCritterID))
					{
						pPetDef = GET_REF(pCritterRel->hPetDef);
					}
					else
					{
						pPetDef = Entity_FindAllowedCritterPetDefByID(pPlayer, uiCritterID);
					}
					if (pPetDef)
					{
						const char* pchName = langTranslateMessageRef(entGetLanguage(pPlayer),pPetDef->displayNameMsg.hMessage);
						pAwayTeamMemberData->pcName = StructAllocString(pchName);
					}
				}

				pAwayTeamMemberData->pEnt = pMember;
				pAwayTeamMemberData->pOwnerEnt = pOwner;

				pAwayTeamMemberData->iEntType = pAwayTeamMember->eEntType;
				pAwayTeamMemberData->iEntID = pAwayTeamMember->iEntID;

				pAwayTeamMemberData->bIsPet = ( pAwayTeamMember->eEntType != GLOBALTYPE_ENTITYPLAYER );

				pAwayTeamMemberData->iClass = CharClassTypes_None;
				pAwayTeamMemberData->pcClassName = NULL;

				if (pMember)
				{
					if (pAwayTeamMemberData->bIsPet)
					{
						pGroundEnt = pMember;
					}
					else
					{
						pGroundEnt = entity_GetPuppetEntityByType(pMember, "Ground", NULL, false, true);
					}
					pClass = pGroundEnt && pGroundEnt->pChar ? GET_REF(pGroundEnt->pChar->hClass) : NULL;
					if (pClass)
					{
						pAwayTeamMemberData->iClass = pClass->eType;
						pAwayTeamMemberData->pcClassName = pClass->pchName;
					}
					REMOVE_HANDLE(pAwayTeamMemberData->hCostume);
				}
				else
				{
					if (pCritterRel)
					{
						COPY_HANDLE(pAwayTeamMemberData->hCostume, pCritterRel->hCostume);
					}
					else if (pPetDef)
					{
						CritterDef* pCritterDef = GET_REF(pPetDef->hCritterDef);
						CritterCostume* pCostume = pCritterDef ? eaGet(&pCritterDef->ppCostume, 0) : NULL;
						if (pCostume)
						{
							COPY_HANDLE(pAwayTeamMemberData->hCostume, pCostume->hCostumeRef);
						}
						else
						{
							REMOVE_HANDLE(pAwayTeamMemberData->hCostume);
						}
					}
					else
					{
						REMOVE_HANDLE(pAwayTeamMemberData->hCostume);
					}
				}

				if ( pAwayTeamMemberData->bIsPet )
				{
					ANALYSIS_ASSUME(pOwner != NULL); // relatively sure that since this is a pet, pOwner must be set.
					pAwayTeamMemberData->pcOwnerName = StructAllocString(entGetLocalName(pOwner));
					pAwayTeamMemberData->iONLen = pAwayTeamMemberData->pcOwnerName ? (int)strlen(pAwayTeamMemberData->pcOwnerName) : 0;

					pAwayTeamMemberData->bPetPermission = true;

					pAwayTeamMemberData->bIsReady = pOwnerMember->bIsReady;
				}
				else
				{
					pAwayTeamMemberData->bIsReady = pAwayTeamMember->bIsReady;
				}
			}
		}
	}

	while (eaSize(peaData) > iCount)
		StructDestroy(parse_AwayTeamMemberData, eaPop(peaData));

	ui_GenSetManagedListSafe(pGen, peaData, AwayTeamMemberData, true);
	return iCount;
}

//TODO: remove - use something more generic
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_CountAwayTeamPetsAvailable);
S32 teamExpr_CountAwayTeamPetsAvailable(SA_PARAM_OP_VALID Entity *pPlayer,
										SA_PARAM_OP_VALID AwayTeamMemberData* pAwayTeamMember )
{
	S32 i, iCount = 0;

	if ( s_pAwayTeamData==NULL || s_pAwayTeamData->pAwayTeamMembers==NULL )
		return 0;

	if (pPlayer && pAwayTeamMember && entity_GetSubEntity(PARTITION_CLIENT, pPlayer,pAwayTeamMember->iEntType,pAwayTeamMember->iEntID))
	{
		S32 iOwnedPetsSize = eaSize(&pPlayer->pSaved->ppOwnedContainers);

		for ( i = 0; i < iOwnedPetsSize; i++ )
		{
			PetRelationship* pPet = pPlayer->pSaved->ppOwnedContainers[i];
			Entity* pPetEnt = GET_REF(pPet->hPetRef);
			CharClassTypes PetClass = GetCharacterClassEnum( pPetEnt );

			if (pPetEnt==NULL)
				continue;

			if(SavedPet_IsPetAPuppet(pPlayer,pPet))
				continue;

			if ( pPetEnt->pChar && eaSize(&pPetEnt->pChar->ppTraining)>0 )
				continue;

			iCount++;
		}
	}

	if(pPlayer && pPlayer->pSaved)
	{
		for(i=0;i<eaSize(&pPlayer->pSaved->ppAllowedCritterPets);i++)
		{
			iCount++;
		}
	}

	return iCount;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsAwayTeamLeader);
bool teamExpr_IsAwayTeamLeader( SA_PARAM_OP_VALID Entity* pEntity )
{
	return pEntity && s_pAwayTeamData && team_IsAwayTeamLeader(pEntity,s_pAwayTeamData->pAwayTeamMembers);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetAwayTeamPetOwnerList);
S32 teamExpr_GetAwayTeamPetOwnerList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pPlayer, bool bCountOnly)
{
	AwayTeamMemberData ***peaData = ui_GenGetManagedListSafe(pGen, AwayTeamMemberData);
	S32 iCount = 0;

	if (	s_pAwayTeamData==NULL
		||	s_pAwayTeamData->pAwayTeamMembers==NULL
		||	s_pAwayTeamData->pAwayTeamMembers->eaMembers==NULL )
	{
		if ( !bCountOnly )
			ui_GenSetManagedListSafe(pGen, NULL, AwayTeamMemberData, true);

		return 0;
	}

	if ( pPlayer && team_IsTeamLeader( pPlayer ) )
	{
		S32 i, iAwayTeamSize = eaSize(&s_pAwayTeamData->pAwayTeamMembers->eaMembers);

		for ( i = 0; i < iAwayTeamSize; i++ )
		{
			if ( s_pAwayTeamData->pAwayTeamMembers->eaMembers[i]->eEntType == GLOBALTYPE_ENTITYPLAYER )
			{
				if ( !bCountOnly )
				{
					Entity *pGroundEnt;
					CharacterClass* pClass;
					AwayTeamMemberData* pMember = eaGetStruct( peaData, parse_AwayTeamMemberData, iCount++ );
					pMember->bPetPermission = false;
					pMember->bIsPet = false;
					pMember->bIsReady = false;
					pMember->iONLen = 0;
					pMember->iEntType = s_pAwayTeamData->pAwayTeamMembers->eaMembers[i]->eEntType;
					pMember->iEntID = s_pAwayTeamData->pAwayTeamMembers->eaMembers[i]->iEntID;
					pMember->pEnt = entFromContainerIDAnyPartition( pMember->iEntType, pMember->iEntID );
					pMember->pOwnerEnt = NULL;
					pGroundEnt = entity_GetPuppetEntityByType(pMember->pEnt, "Ground", NULL, false, true);
					pClass = pGroundEnt && pGroundEnt->pChar ? GET_REF(pGroundEnt->pChar->hClass) : NULL;
					if (pClass)
					{
						pMember->iClass = pClass->eType;
						pMember->pcClassName = pClass->pchName;
					}
					else
					{
						pMember->iClass = CharClassTypes_None;
						pMember->pcClassName = NULL;
					}

					if ( pMember->pcName )
						StructFreeString( pMember->pcName );
					if ( pMember->pcOwnerName )
						StructFreeString( pMember->pcOwnerName );

					if ( pMember->pEnt )
					{
						pMember->pcName = StructAllocString(entGetLocalName(pMember->pEnt));
					}
					else
					{
						pMember->pcName = NULL;
					}
					pMember->pcOwnerName = NULL;
				}
				else
				{
					iCount++;
				}
			}
		}
	}

	if ( !bCountOnly )
	{
		while (eaSize(peaData) > iCount)
			StructDestroy(parse_AwayTeamMemberData, eaPop(peaData));

		ui_GenSetManagedListSafe(pGen, peaData, AwayTeamMemberData, true);
	}
	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_ChangeAwayTeamPet);
void teamExpr_ChangeAwayTeamPet(SA_PARAM_OP_VALID Entity* pPlayerEnt, S32 iPetType, U32 uiPetID, U32 uiCritterID, S32 iSlot)
{
	if ( pPlayerEnt )
	{
		if ( iPetType == GLOBALTYPE_ENTITYCRITTER )
		{
			uiPetID = entGetContainerID(pPlayerEnt);
		}
		else if ( iPetType != GLOBALTYPE_ENTITYSAVEDPET || !entity_GetSubEntity(PARTITION_CLIENT,pPlayerEnt,iPetType,uiPetID) )
		{
			return;
		}

		ServerCmd_gslTeam_ChangeAwayTeamPet( iPetType, uiPetID, uiCritterID, iSlot );
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_ChangeAwayTeamPetOwnership);
void teamExpr_ChangeAwayTeamPetOwnership( SA_PARAM_OP_VALID AwayTeamMemberData* pOwner, S32 iSlot )
{
	Entity* pEntity = entActivePlayerPtr();

	if ( pEntity==NULL || pOwner==NULL )
		return;

	if ( s_pAwayTeamData==NULL )
		return;

	if ( !team_IsAwayTeamLeader( pEntity, s_pAwayTeamData->pAwayTeamMembers ) )
		return;

	ServerCmd_gslTeam_ChangeAwayTeamPetOwnership( pOwner->iEntType, pOwner->iEntID, iSlot );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_RemoveAwayTeamMember);
void teamExpr_RemoveAwayTeamMember(SA_PARAM_OP_VALID AwayTeamMemberData* pAwayTeamMember)
{
	Entity* pEntity = entActivePlayerPtr();

	if ( pEntity==NULL || pAwayTeamMember==NULL )
		return;

	if ( s_pAwayTeamData==NULL )
		return;

	if ( pEntity->myContainerID == pAwayTeamMember->iEntID )
		return;

	if ( pAwayTeamMember->iEntType == GLOBALTYPE_ENTITYPLAYER ) //cannot remove players
		return;

	if ( eaSize(&s_pAwayTeamData->pAwayTeamMembers->eaMembers) <= 1 )
		return;

	if ( !team_IsAwayTeamLeader( pEntity, s_pAwayTeamData->pAwayTeamMembers ) )
		return;

	ServerCmd_gslTeam_RemoveAwayTeamMember( pAwayTeamMember->iEntType, pAwayTeamMember->iEntID );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_AddAwayTeamMember);
void teamExpr_AddAwayTeamMember()
{
	Entity* pEntity = entActivePlayerPtr();
	AwayTeamMembers *pMembers = SAFE_MEMBER(s_pAwayTeamData, pAwayTeamMembers);

	if (!pEntity || !pMembers || eaSize(&pMembers->eaMembers) >= pMembers->iMaxTeamSize || !team_IsAwayTeamLeader(pEntity, pMembers))
		return;

	ServerCmd_gslTeam_AddAwayTeamMember();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_MapTransferOptIn);
void teamExpr_MapTransferOptIn()
{
	if ( s_pAwayTeamData == NULL )
		return;

	ServerCmd_gslTeam_cmd_MapTransferOptIn();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_MapTransferOptOut);
void teamExpr_MapTransferOptOut()
{
	// Players cannot opt out of team transfer in NNO
	if (!gConf.bEnableNNOTeamWarp)
	{
		ServerCmd_gslTeam_cmd_MapTransferOptOut();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Other Functionality
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_RUN;
void team_InitUI(void)
{
	ui_GenInitStaticDefineVars(TeamModeEnum, "TeamMode_");
}

///////////////////////////////////////////////////////////////////////////////////////////
// Team Settings
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetMode);
const char *TeamExpr_GetMode(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);

	if (!pTeam) {
		return 0;
	}

	return StaticDefineGetTranslatedMessage(TeamModeEnum, pTeam->eMode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetDefaultMode);
const char *TeamExpr_GetDefaultMode(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	const char *pchResult = NULL;

	if ( pPlayer && pPlayer->pTeam )
		pchResult = StaticDefineGetTranslatedMessage(TeamModeEnum,pPlayer->pTeam->eMode);

	return NULL_TO_EMPTY(pchResult);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetTeamInviter);
const char *TeamExpr_GetTeamInviter(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	const char *pchResult = NULL;

	if (pPlayer && pPlayer->pTeam)
		pchResult = pPlayer->pTeam->pcInviterName;

	return NULL_TO_EMPTY(pchResult);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetTeamInviterHandle);
const char *TeamExpr_GetTeamInviterHandle(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	const char *pchResult = NULL;

	if (pPlayer && pPlayer->pTeam)
		pchResult = pPlayer->pTeam->pcInviterHandle;

	return NULL_TO_EMPTY(pchResult);
}

AUTO_STRUCT;
typedef struct LootModeStrPair
{
    LootMode eMode;
	const char *pchName;		AST(UNOWNED)
    const char *pchStr;			AST(UNOWNED)
	char *pchDesc;		AST(ESTRING)
} LootModeStrPair;

static LootModeStrPair **s_ppLootModes = NULL;

static void teamExpr_GetLootModesInternal(void)
{
	int i;
	if (!s_ppLootModes)
	{
		for (i = LootMode_RoundRobin; i < LootMode_Count; ++i)
		{
			LootModeStrPair *p;
			char *descMsgKey = NULL;

				p = StructCreate(parse_LootModeStrPair);
				p->eMode = i;
				p->pchName = StaticDefineIntRevLookup(LootModeEnum, i);
				p->pchStr = StaticDefineGetTranslatedMessage(LootModeEnum,i);
				estrPrintf(&descMsgKey, "TeamLoot_%sTooltip", StaticDefineIntRevLookup(LootModeEnum,i));
				FormatGameMessageKey(&p->pchDesc, descMsgKey, STRFMT_END);
				eaPush(&s_ppLootModes, p);

			estrDestroy(&descMsgKey);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetStatusMessage);
SA_RET_OP_STR const char * teamExpr_GetStatusMessage(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);

	if (pTeam && pTeam->pchStatusMessage)
	{
		return pTeam->pchStatusMessage;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetLootMode);
S32 teamExpr_GetLootMode(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);
	int i;

	if (!pTeam)
		return -1;

	teamExpr_GetLootModesInternal();

	for (i = 0; i < eaSize(&s_ppLootModes); i++)
	{
		if (s_ppLootModes[i]->eMode == pTeam->loot_mode)
			return s_ppLootModes[i]->eMode;
	}

	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetLootModeStr);
const char *teamExpr_GetLootModeStr(void)
{
	LootMode eMode = teamExpr_GetLootMode();
	if (eMode >= 0) {
		return StaticDefineGetTranslatedMessage(LootModeEnum, eMode);
	} else {
		return "";
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetDefaultLootMode);
S32 teamExpr_GetDefaultLootMode(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	int i;

	if (!pPlayer || !pPlayer->pTeam)
		return -1;

	teamExpr_GetLootModesInternal();

	for (i = 0; i < eaSize(&s_ppLootModes); i++)
	{
		if (s_ppLootModes[i]->eMode == pPlayer->pTeam->eLootMode)
			return s_ppLootModes[i]->eMode;
	}

	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetDefaultLootModeStr);
const char *teamExpr_GetDefaultLootModeStr(void)
{
	LootMode eMode = teamExpr_GetDefaultLootMode();
	if (eMode >= 0) {
		return StaticDefineGetTranslatedMessage(LootModeEnum, eMode);
	} else {
		return "";
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetSize);
int teamExpr_GetSize(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);
	return(team_NumPresentMembers(pTeam));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetLocalEntity);
SA_RET_OP_VALID Entity *teamExpr_GetLocalEntity(int iMemberNum)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);
	TeamMember *pMember = pTeam ? eaGet(&pTeam->eaMembers, iMemberNum) : NULL;
	return pMember ? entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pMember->iEntID) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetTeammateName);
const char *teamExpr_GetTeammateName(int iTeamIndex)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);
	if (pTeam && iTeamIndex >= 0 && iTeamIndex < eaSize(&pTeam->eaMembers)) {
		return pTeam->eaMembers[iTeamIndex]->pcName;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_GetLootModes);
void teamExpr_GenGetLootModes(SA_PARAM_NN_VALID UIGen *gen)
{
	teamExpr_GetLootModesInternal();
	ui_GenSetList(gen, &s_ppLootModes, parse_LootModeStrPair);
}

AUTO_STRUCT;
typedef struct ItemQualityStrPair
{
    ItemQuality quality;
	const char *pcName;
    const char *str;
} ItemQualityStrPair;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetLootQualities");
void GetLootQualities(SA_PARAM_NN_VALID UIGen *gen)
{
    static ItemQualityStrPair **qualities = NULL;
    int i;
    if(!qualities)
    {
        for(i = 0; i < eaSize(&g_ItemQualities.ppQualities); ++i)
        {
			if (!(g_ItemQualities.ppQualities[i]->flags & kItemQualityFlag_HideFromUILists))
            {
				ItemQualityStrPair *p = StructCreate(parse_ItemQualityStrPair);
				p->quality = i;
				p->pcName = StaticDefineIntRevLookup(ItemQualityEnum, i);
				p->str = StaticDefineGetTranslatedMessage(ItemQualityEnum,i);
				eaPush(&qualities,p);
			}
        }
    }

    ui_GenSetList(gen, &qualities, parse_ItemQualityStrPair);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetLootQualitiesMinMax");
void GetLootQualitiesMinMax(SA_PARAM_NN_VALID UIGen *gen, int min, int max)
{
	static ItemQualityStrPair **qualities = NULL;
	int i;
	if(!qualities)
	{
		for(i = min; i < eaSize(&g_ItemQualities.ppQualities) && i <= max; ++i)
		{
			if (!(g_ItemQualities.ppQualities[i]->flags & kItemQualityFlag_HideFromUILists))
			{
				ItemQualityStrPair *p = StructCreate(parse_ItemQualityStrPair);
				p->quality = i;
				p->pcName = StaticDefineIntRevLookup(ItemQualityEnum, i);
				p->str = StaticDefineGetTranslatedMessage(ItemQualityEnum,i);
				eaPush(&qualities,p);
			}
		}
	}

	ui_GenSetList(gen, &qualities, parse_ItemQualityStrPair);
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TeamLoot_GetQualityStr");
const char* TeamLoot_GetQualityStr(void)
{
    Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);

	if(pTeam) {
		return StaticDefineGetTranslatedMessage(ItemQualityEnum, StaticDefineIntGetInt(ItemQualityEnum, pTeam->loot_mode_quality));
	} else {
		return "";
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Team_GetDefaultLootQualityStr");
const char* TeamExpr_GetDefaultLootQualityStr(void)
{
    Entity *pPlayer = entActivePlayerPtr();

	if(pPlayer && pPlayer->pTeam)
		return pPlayer->pTeam->eLootQuality;

	return "INVALID";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(team_IsOpenInstancingEnabled);
bool teamExpr_IsOpenInstancingEnabled(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);
	return pTeam ? pTeam->eMode == TeamMode_Open : false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(team_GetDefaultOpenInstancingModeForOthers);
bool TeamExpr_GetDefaultOpenInstancingModeForOthers( void )
{
	Entity* pEnt = entActivePlayerPtr();

	if ( pEnt && pEnt->pTeam && pEnt->pPlayer)
	{
		return pEnt->pPlayer->eLFGMode == TeamMode_Open;
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(team_GetDefaultOpenInstancingModeForSelf);
const char* TeamExpr_GetDefaultOpenInstancingModeForSelf( void )
{
	Entity* pEnt = entActivePlayerPtr();

	if ( pEnt && pEnt->pPlayer)
	{
		return StaticDefineIntRevLookup( TeamModeEnum, pEnt->pPlayer->eLFGMode );
	}
	return "";
}

static TeamInfoFromServer *gs_TeamInfo = NULL;
static char **gs_eaPowersCategories = NULL;
static U32 gs_uTimeSinceLastUpdate = 0;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME(RecieveTeamInfo) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void team_RecieveTeamInfo(Entity *pClientEntity, TeamInfoFromServer *pTeamInfo)
{
	if (gs_TeamInfo) StructDestroy(parse_TeamInfoFromServer, gs_TeamInfo);
	if (pTeamInfo)
	{
		gs_TeamInfo = StructClone(parse_TeamInfoFromServer, pTeamInfo);
	}
	else
	{
		gs_TeamInfo = NULL;
	}
	gs_uTimeSinceLastUpdate = timeSecondsSince2000();
}

const TeamInfoFromServer *team_RequestTeamInfoFromServer(const char *pPowersCategory)
{
	int i;

	for (i = eaSize(&gs_eaPowersCategories)-1; i >= 0; --i)
	{
		if (!stricmp(pPowersCategory, gs_eaPowersCategories[i])) break;
	}
	if (i < 0)
	{
		eaPush(&gs_eaPowersCategories, StructAllocString(pPowersCategory));
	}

	if (gs_uTimeSinceLastUpdate + 3 <= timeSecondsSince2000())
	{
		TeamInfoRequest temp;

		temp.eaPowersCategories = gs_eaPowersCategories;
		ServerCmd_RequestTeamInfo(&temp);
		gs_uTimeSinceLastUpdate = timeSecondsSince2000();
	}

	return gs_TeamInfo;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(team_IsMasterLooter);
bool TeamExpr_IsMasterLooter( Entity *pEnt )
{
	if ( pEnt && pEnt->pTeam)
	{
		Team *pTeam = GET_REF(pEnt->pTeam->hTeam);
		if (pTeam)
		{
			return pTeam->pLeader ? pTeam->pLeader->iEntID == pEnt->myContainerID : false;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(Team_GetTeamXPMultiplier);
F32 TeamExpr_GetTeamXPMultiplier(ExprContext *pContext, U32 uiTeamSize)
{
	if (uiTeamSize < ARRAY_SIZE(g_RewardConfig.TeamMods.TeamMods))
	{
		return g_RewardConfig.TeamMods.TeamMods[uiTeamSize];
	}
	return -1.0f;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(Team_GetAutoSidekick);
bool TeamExpr_GetAutoSidekick(ExprContext *pContext)
{
	Entity* pEnt = entActivePlayerPtr();
	return SAFE_MEMBER4(pEnt, pPlayer, pUI, pLooseUI, bAutoSidekick);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(Team_SetAutoSidekick);
void TeamExpr_SetAutoSidekick(ExprContext *pContext, bool bOn)
{
	Entity* pEnt = entActivePlayerPtr();
	if (SAFE_MEMBER3(pEnt, pPlayer, pUI, pLooseUI))
	{
		pEnt->pPlayer->pUI->pLooseUI->bAutoSidekick = bOn;
	}
}

#include "AutoGen/TeamUI_c_ast.c"
