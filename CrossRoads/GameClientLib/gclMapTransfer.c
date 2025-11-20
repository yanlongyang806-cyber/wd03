/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Expression.h"
#include "globalstatemachine.h"
#include "WorldGrid.h"
#include "Estring.h"

#include "GraphicsLib.h"
#include "UIGen.h"
#include "gclBaseStates.h"
#include "MapDescription.h"
#include "GameStringFormat.h"
#include "gclEntity.h"
#include "EntitySavedData.h"
#include "MapDescription.h"
#include "MicroTransactions.h"
#include "Team.h"
#include "Guild.h"
#include "chatCommonStructs.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "StringCache.h"
#include "GameAccountData\GameAccountData.h"
#include "MapTransferCommon.h"
#include "GameClientLib.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("MapDescription", BUDGET_GameSystems););

// Handles the GCL_GAMEPLAY_CHOOSING_GAMESERVER state, and talks to gslMapTransfer.c

static PossibleMapChoices s_MapChoices;
static bool s_DidMapChoice;



// Forward Declarations
void gclMapTransferCancel(void);
bool gclExprMapTransferChoose(SA_PARAM_NN_VALID PossibleMapChoice *pChoice);


static void gclGameplayChoosingGameServer_Enter(void)
{
	// Choose the first map by default.
	s_DidMapChoice = false;
	if (devassertmsg(eaSize(&s_MapChoices.ppChoices), "Server sent empty map choice list (or in the wrong state)!"))
	{
		
		// FIXME: Looking for this gen is a nasty hack.
		if (gbNoGraphics || !ui_GenFind("MapTransfer_Root", kUIGenStateNone))
		{
			ServerCmd_gslMapTransferChooseAddress(s_MapChoices.ppChoices[0]);
			s_DidMapChoice = true;
		}
		else
		{
			Entity *pEntity = entActivePlayerPtr();
			ZoneMapInfo *pZminfo;
			bool bShowChoice = false;
			if(pEntity)
				bShowChoice = pEntity->pPlayer->pUI->pLooseUI->bShowMapChoice;
			FOR_EACH_IN_EARRAY(s_MapChoices.ppChoices, PossibleMapChoice, pChoice)
				pZminfo = worldGetZoneMapByPublicName(pChoice->baseMapDescription.mapDescription);
				if(stricmp(zmapInfoGetPublicName(NULL), zmapInfoGetPublicName(pZminfo))==0)
				{
					bShowChoice = true;
					break;
				}
			FOR_EACH_END
			if(!bShowChoice)
			{
				ServerCmd_gslMapTransferChooseAddress(s_MapChoices.ppChoices[0]);
				s_DidMapChoice = true;
			}
		}
	}
	else
	{
		GSM_SwitchToState_Complex(GCL_GAMEPLAY);
	}
}

static void gclGameplayChoosingGameServer_Leave(void)
{
	StructDeInit(parse_PossibleMapChoices, &s_MapChoices);
}




AUTO_COMMAND ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD;
void gclDisplayMapChoice(PossibleMapChoices *pChoices)
{
	if (GSM_IsStateActive(GCL_GAMEPLAY)){
		StructCopyAll(parse_PossibleMapChoices, pChoices, &s_MapChoices);
		eaQSort(s_MapChoices.ppChoices, TransferCommon_PossibleMapChoiceSort);
		GSM_SwitchToState_Complex(GCL_GAMEPLAY "/" GCL_GAMEPLAY_CHOOSING_GAMESERVER);
	} else {
		// Player might be loading or something
		StructCopyAll(parse_PossibleMapChoices, pChoices, &s_MapChoices);
		eaQSort(s_MapChoices.ppChoices, TransferCommon_PossibleMapChoiceSort);
		if (!eaSize(&s_MapChoices.ppChoices) || !gclExprMapTransferChoose(s_MapChoices.ppChoices[0])){
			gclMapTransferCancel();
		}
		StructDeInit(parse_PossibleMapChoices, &s_MapChoices);
	}
}

// Get a list of valid game servers to transfer to.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetMapTransferList");
void gclExprGenGetMapTransferList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &s_MapChoices.ppChoices, parse_PossibleMapChoice);
}

// Transfer to the given map, if possible.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MapTransferChoose");
bool gclExprMapTransferChoose(SA_PARAM_NN_VALID PossibleMapChoice *pChoice)
{
	if (eaFind(&s_MapChoices.ppChoices, pChoice) >= 0 && !pChoice->bNotALegalChoice)
	{
		ServerCmd_gslMapTransferChooseAddress(pChoice);
		return true;
	}
	else
		return false;
}

// Check if this map transfer is cancellable.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MapTransferCanCancel");
bool gclExprMapTransferCanCancel(void)
{
	return !(s_MapChoices.eFlags & MAPSEARCH_NOCANCELLING);
}

// Format a possible map choice for display - keys available are Map.Name, Map.NumPlayers,
// Map.NumTeamMembers, Map.NumGuildMembers, Map.NumFriends, and Map.Index.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FormatPossibleMapChoice");
const char *gclExprFormatPossibleMapChoice(SA_PARAM_NN_VALID PossibleMapChoice *pChoice, const char *pchMessageKey)
{
	static unsigned char *s_pchFormatted;
	ZoneMapInfo *zminfo = worldGetZoneMapByPublicName(pChoice->baseMapDescription.mapDescription);
	Message *pMessage = zminfo ? zmapInfoGetDisplayNameMessagePtr(zminfo) : NULL;
	const char *pchName = pMessage ? langTranslateMessage(langGetCurrent(), pMessage) : pChoice->baseMapDescription.mapDescription;
	estrClear(&s_pchFormatted);
	FormatGameMessageKey(&s_pchFormatted, pchMessageKey,
		STRFMT_STRING("Map.Name", pchName),
		STRFMT_INT("Map.NumPlayers", pChoice->iNumPlayers),
		STRFMT_INT("Map.NumTeamMembers", pChoice->iNumTeamMembersThere),
		STRFMT_INT("Map.NumGuildMembers", pChoice->iNumGuildMembersThere),
		STRFMT_INT("Map.NumFriends", pChoice->iNumFriendsThere),
		STRFMT_INT("Map.Index", pChoice->baseMapDescription.mapInstanceIndex),
		STRFMT_END);
	return s_pchFormatted ? s_pchFormatted : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SecondsUntilCanChangeInstance");
U32 gclExprSecondsUntilCanChangeInstance(void)
{
	if( gConf.uSecondsBetweenChangeInstance > 0 )
	{
		Entity *pEntity = entActivePlayerPtr();
		if( pEntity && pEntity->pSaved )
		{
			if(gConf.uSecondsBetweenChangeInstance + pEntity->pSaved->timeEnteredMap > timeServerSecondsSince2000())
			{
				return (gConf.uSecondsBetweenChangeInstance + pEntity->pSaved->timeEnteredMap - timeServerSecondsSince2000());
			}
		}
	}
	return 0;
}

// Is the map the client entity on a static map (i.e. instance change allowed)?
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CanChangeInstance");
bool gclExprCanChangeInstance(void)
{
	bool bAllowed = gclGetInstanceSwitchingAllowed();
	if( bAllowed && gConf.uSecondsBetweenChangeInstance > 0 )
	{
		Entity *pEntity = entActivePlayerPtr();
		if( pEntity && pEntity->pSaved && pEntity->pSaved->timeEnteredMap )
		{
			bAllowed &= (gConf.uSecondsBetweenChangeInstance + pEntity->pSaved->timeEnteredMap <= timeServerSecondsSince2000());
		}
	}
	return bAllowed;
}

// Cancel a map transfer.
AUTO_COMMAND ACMD_HIDE ACMD_NAME("MapTransferCancel") ACMD_ACCESSLEVEL(0);
void gclMapTransferCancel(void)
{
	// Cancelling is done by sending back a map that is not valid.
	PossibleMapChoice Choice = {0};
	Choice.baseMapDescription.eMapType = ZMTYPE_UNSPECIFIED;
	Choice.baseMapDescription.mapInstanceIndex = -1;
	Choice.bNotALegalChoice = true;
	sprintf(Choice.notLegalChoiceReason, "Set due to user cancel");
	Choice.baseMapDescription.mapDescription = allocAddString("InvalidMapName");
	ServerCmd_gslMapTransferChooseAddress(&Choice);

	if (GSM_IsStateActive(GCL_GAMEPLAY_CHOOSING_GAMESERVER))
	{
		GSM_SwitchToState_Complex(GCL_GAMEPLAY_CHOOSING_GAMESERVER "/..");
	}
}

static PlayerMapMoveClient *s_pClientConfirm = NULL;
static bool s_bClientConfirm_MTUpdated = false;

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void RequestMapMoveConfirm(PlayerMapMoveClient *pClientConfirm)
{
	StructDestroySafe(parse_PlayerMapMoveClient, &s_pClientConfirm);
	s_pClientConfirm = StructClone(parse_PlayerMapMoveClient, pClientConfirm);
	s_bClientConfirm_MTUpdated = false;

	if(!s_pClientConfirm)
		return;

	if(s_pClientConfirm->eType == kPlayerMapMove_PowerPurchase)
	{
		UIGen *pGen = ui_GenFind("Core_MapMoveConfirm", kUIGenTypeNone);
		if(pGen)
		{
			ui_GenSendMessage(pGen,"Show");
		}
	}
	else if(s_pClientConfirm->eType == kPlayerMapMove_Permission)
	{
		UIGen *pGen = ui_GenFind("Permission_MapMoveConfirm", kUIGenTypeNone);
		if(pGen)
		{
			ui_GenSendMessage(pGen,"Show");
		}
	}
	else if(s_pClientConfirm->eType == kPlayerMapMove_Warp)
	{
		UIGen *pGen = ui_GenFind("Warp_MapMoveConfirm", kUIGenTypeNone);
		if(pGen)
		{
			ui_GenSendMessage(pGen,"Show");
		}
	}
}

AUTO_EXPR_FUNC(UIGen);
const char *gclMapMoveConfirm_MapName(void)
{
	if(s_pClientConfirm)
		return NULL_TO_EMPTY(TranslateMessageRef(s_pClientConfirm->hDisplayName));

	return "";
}

AUTO_EXPR_FUNC(UIGen);
int gclMapMoveConfirm_TimeLeft(void)
{
	if(s_pClientConfirm && s_pClientConfirm->uiTimeToConfirm)
	{
		if(s_pClientConfirm->uiTimeStart + s_pClientConfirm->uiTimeToConfirm > timeServerSecondsSince2000())
			return s_pClientConfirm->uiTimeStart + s_pClientConfirm->uiTimeToConfirm - timeServerSecondsSince2000();
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen);
const char* gclMapMoveConfirm_PlayerInitiator(void)
{
	if(s_pClientConfirm && s_pClientConfirm->pchRequestingEnt)
	{
		return NULL_TO_EMPTY(s_pClientConfirm->pchRequestingEnt);
	}
	
	return "";
}


const MicroTransactionRef **gclMapMoveConfirm_Microtransactions(void)
{
	if(s_pClientConfirm)
	{
		return s_pClientConfirm->ppMTRefs;
	}

	return NULL;
}

void gclMapMoveConfirm_Microtransaction_Notify(MicroTransactionDef *pDef, bool bSuccess)
{
	UIGen *pGen = NULL;
	bool bFound = false;
	if(!s_pClientConfirm 
		|| s_pClientConfirm->eType != kPlayerMapMove_Permission)
		return;

	FOR_EACH_IN_EARRAY(s_pClientConfirm->ppMTRefs, MicroTransactionRef, pRef)
	{
		MicroTransactionDef *pPossibleDef = GET_REF(pRef->hMTDef);
		if(pPossibleDef == pDef)
		{
			bFound = true;
			break;
		}
	} FOR_EACH_END;

	if(!bFound)
		return;

	pGen = ui_GenFind("Permission_MapMoveConfirm", kUIGenTypeNone);

	if(pGen)
	{
		if(bSuccess)
		{
			ui_GenSendMessage(pGen,"Success");
		}
		else
		{	
			ui_GenSendMessage(pGen,"Failure");
		}
	}
}

// Should we always show the map transfer dialog
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AlwaysShowMapTransfer");
bool gclExprAlwaysShowMapTransfer(void)
{
	Entity *pEntity = entActivePlayerPtr();

	if(pEntity && pEntity->pPlayer && pEntity->pPlayer->pUI && pEntity->pPlayer->pUI->pLooseUI)
	{
		return pEntity->pPlayer->pUI->pLooseUI->bShowMapChoice;
	}

	return false;
}

// Set the always show map tranfer UI preference
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SetAlwaysShowMapTransfer");
void gclExprSetAlwaysShowMapTransfer(bool bVal)
{
	ServerCmd_SetAlwaysShowMapTransfer(bVal);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("DidMapTransfer");
bool gclExprDidMapTransfer(void)
{
	return s_DidMapChoice;
}

AUTO_RUN;
void gclMapTransfer_AutoRegister(void)
{
	GSM_AddGlobalState(GCL_GAMEPLAY_CHOOSING_GAMESERVER);
	GSM_AddGlobalStateCallbacks(GCL_GAMEPLAY_CHOOSING_GAMESERVER, gclGameplayChoosingGameServer_Enter, NULL, NULL, gclGameplayChoosingGameServer_Leave);
}
