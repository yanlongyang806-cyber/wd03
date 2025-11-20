/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameClientLib.h"
#include "gclHandleMsg.h"
#include "EntityNet.h"
#include "WorldGrid.h"
#include "GameAccountDataCommon.h"
#include "gclEntityNet.h"
#include "gclCommandParse.h"
#include "GfxConsole.h"
#include "GfxDebug.h"
#include "GfxPrimitive.h"
#include "testclient_comm.h"
#include "gclLogin.h"
#include "gclBaseStates.h"
#include "GlobalStateMachine.h"
#include "gclLoading.h"
#include "BeaconDebug.h"
#include "gclDemo.h"
#include "EntityIterator.h"
#include "Character.h"
#include "gclMapState.h"
#include "EditLib.h"
#include "gclControlScheme.h"
#include "logging.h"
#include "ClientTargeting.h"
#include "gclDialogBox.h"
#include "appRegCache.h"
#include "gclEntity.h"
#include "CombatDebugViewer.h"
#include "MicroTransactions.h"
#include "gclScript.h"
#include "gclChat.h"
#include "gclNotify.h"
#include "StringFormat.h"

// For wleOpSendClientBinsHack and TimedCallback_Run
#include "WorldEditorOperations.h"
#include "TimedCallback.h"
#include "logincommon.h"
#include "Login2Common.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

static int HandleTestClientCommandResultsFromServer(Packet *pak)
{
	U32 iTestClientCmdID;
	NetLink *pLinkToTestClient = gclGetLinkToTestClient();

	while((iTestClientCmdID = pktGetU32(pak)))
	{
		bool bResult = pktGetBits(pak, 1);
		char *pCmd = NULL;
		char *pString;

		if (bResult)
		{
			pCmd = pktGetStringTemp(pak);
		}

		pString = pktGetStringTemp(pak);

		if (pLinkToTestClient)
		{
			Packet *pPacket = pktCreate(pLinkToTestClient, TO_TESTCLIENT_CMD_RESULT);
			pktSendU32(pPacket, iTestClientCmdID);
			pktSendBits(pPacket, 1, bResult);
			if (bResult)
			{
				pktSendString(pPacket, pCmd);
			}
			pktSendString(pPacket, pString);
			pktSend(&pPacket);
		}
	}

	return 1;
}




static void HandleGameMsgs(Packet *pak, Entity *ent, NetLink *link)
{
	while (!pktEnd(pak))
	{
		int lib = pktGetBits(pak,1);
		int command = pktGetBitsPack(pak,GAME_MSG_SENDBITS) | (lib * LIB_MSG_BIT);
		if (!(command & LIB_MSG_BIT))
		{
			GameClientHandlePktMsg(pak,command,ent);
		}
		else
		{
			gclHandleMsg(pak,command,ent);
		}
	}
}

__forceinline static void gclConsolePrintf(const char *msg)
{
	if(!gbNoGraphics)
	{
		conPrintf("%s", msg );
	}
	gclScript_QueueChat("Console", "!!GameServer", msg);
	SendCommandStringToTestClientf("PushChat Console 0 !!GameServer \"%s\"", msg);
}

int gclHandleMsg(Packet* pak, int cmd, Entity *ent)
{
	char	*msg = pktGetStringTemp(pak);
	U32 iFlags = 0;
	enumCmdContextHowCalled eHow;
	CmdParseStructList structList = {0};
	char *pErrorMessage = NULL;

	switch (cmd)
	{
	xcase GAMESERVERLIB_CMD_PUBLIC: // also CHATRELAY_CMD_PUBLIC
		cmdParseGetStructListFromPacket(pak, &structList, &pErrorMessage, false);
		if (!eaSize(&structList.ppEntries) && pErrorMessage)
		{
			assertmsgf(0, "Data corruption while receiving cmdparse structs: %s", pErrorMessage);
		}
		else
		{
			iFlags = pktGetBits(pak, 32);
			eHow = pktGetBits(pak, 32);
			GameClientParsePublic(msg, iFlags, ent, NULL, -1, eHow, &structList);
		}
		cmdClearStructList(&structList);

	xcase GAMESERVERLIB_CMD_PRIVATE: // also CHATRELAY_CMD_PRIVATE
		cmdParseGetStructListFromPacket(pak, &structList, &pErrorMessage, false);
		if (!eaSize(&structList.ppEntries) && pErrorMessage)
		{
			assertmsgf(0, "Data corruption while receiving cmdparse structs: %s", pErrorMessage);
		}
		else
		{
			iFlags = pktGetBits(pak, 32);
			eHow = pktGetBits(pak, 32);
			GameClientParsePrivate(msg, iFlags, ent, -1, eHow, &structList);
		}
		cmdClearStructList(&structList);
	xcase GAMESERVERLIB_CON_PRINTF:
		gclConsolePrintf(msg);
	xcase GAMESERVERLIB_CHATCMD_UNKNOWN:
		{
			char *cmdName = NULL;

			if (!ClientChat_AttemptChannelMessage(msg, &cmdName))
			{
				char *errmsg = NULL;
				FormatMessageKey(&errmsg, "CmdParse_UnknownCommand", STRFMT_STRING("Command", cmdName), STRFMT_END);
				gclConsolePrintf(errmsg);
				estrDestroy(&errmsg);
			}
			estrDestroy(&cmdName);
		}

	xdefault:
		Errorf("Invalid Server msg received: %d", cmd);
	}

	// If demo recording is on, record this message
	demo_RecordMessage(msg, iFlags, cmd, ent);

	return 1;
}

int gclHandleMsgFromReplay(char* msg, U32 iFlags, int cmd, Entity* ent)
{
	switch (cmd)
	{
	xcase GAMESERVERLIB_CMD_PUBLIC:
		GameClientParsePublic(msg, iFlags, ent, NULL, -1, CMD_CONTEXT_HOWCALLED_REPLAY, NULL);
	xcase GAMESERVERLIB_CMD_PRIVATE:
		GameClientParsePrivate(msg, iFlags, ent, -1, CMD_CONTEXT_HOWCALLED_REPLAY, NULL);
	xcase GAMESERVERLIB_CON_PRINTF:
		conPrintf("%s", msg );
	xdefault:
		Errorf("Invalid Server msg received: %d", cmd);
	}

	return 1;
}

// Processes data sent from the server, in case the client needs to do immediate processing,
//  or wants to buffer some data for later without the server overwriting it
static void ProcessHandledGeneralUpdate(void)
{
	Entity *pEnt;
	EntityIterator *pIter = entGetIteratorAllTypesAllPartitions(0, 0);
	while(pEnt = EntityIteratorGetNext(pIter))
	{
		if(pEnt->pChar)
		{
			if(pEnt->pChar->dirtyID != pEnt->pChar->dirtyClientMatchID)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
				character_ResetPowersArray(entGetPartitionIdx(pEnt), pEnt->pChar, pExtract);
				character_updateTacticalRequirements(pEnt->pChar);
				pEnt->pChar->dirtyClientMatchID = pEnt->pChar->dirtyID;
			}
			if(pEnt->pChar->combatTrackerNetList.id)
			{
				character_CombatTrackerBuffer(pEnt->pChar);
			}
		}
	}
	EntityIteratorRelease(pIter);
}

static void gclHandleGeneralUpdate(Packet *pak)
{
	START_BIT_COUNT(pak, "gclHandleEntityUpdate");
		if (!gclHandleEntityUpdate(pak))
		{
			STOP_BIT_COUNT(pak);
			return;
		}
	STOP_BIT_COUNT(pak);

	START_BIT_COUNT(pak, "resClientProcessServerUpdates");
		if (pktGetBits(pak, 1) &&
			!resClientProcessServerUpdates(pak))
		{
			STOP_BIT_COUNT(pak);
			return;
		}
	STOP_BIT_COUNT(pak);

	START_BIT_COUNT(pak, "HandleTestClientCommandResultsFromServer");
		if (pktGetBits(pak, 1) &&
			!HandleTestClientCommandResultsFromServer(pak))
		{
			STOP_BIT_COUNT(pak);
			return;
		}
	STOP_BIT_COUNT(pak);

	START_BIT_COUNT(pak, "mapState_ClientReceiveMapStateFromPacket");
	mapState_ClientReceiveMapStateFromPacket(pak);
	STOP_BIT_COUNT(pak);

	ProcessHandledGeneralUpdate();
	schemes_HandleUpdate();
	gclNotify_HandleUpdate();
	clientTarget_HandleServerChange();
	clientTarget_HandleServerFocusChange();
}

static void handleLoginSafePeriodPassed(Packet *pPak)
{
	//off chance this could be NULL in some crazy logout race condition
	if (gclLoginGetChosenCharacterName())
	{
        regDelete(GetLoginBeganKeyName(gclLoginGetChosenCharacterName()));
	}

}

#define PACKET_CASE(cmdName, packet)	xcase cmdName: PERFINFO_AUTO_START(#cmdName, 1); START_BIT_COUNT(packet, #cmdName);
#define PACKET_CASE_END(packet) STOP_BIT_COUNT(packet); PERFINFO_AUTO_STOP()

void gclHandlePacketFromGameServer(Packet* pak, int cmd, NetLink* link, void *user_data)
{	
	U32 packetIndexStart = pktGetIndex(pak);;
	
	PERFINFO_AUTO_START_FUNC();

	packetIndexStart = pktGetIndex(pak);

	switch (cmd)
	{
		PACKET_CASE(TOCLIENT_GAME_MSG, pak);
		{
			EntityRef ref = entReceiveRef(pak);
			HandleGameMsgs(pak,entFromEntityRefAnyPartition(ref),link);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_GAME_LOGOUT, pak);
		{
			const char *msg = pktGetStringTemp(pak);
			msg = TranslateMessageKeySafe(msg);
			
			log_printf(LOG_CLIENTSERVERCOMM, "Client told to log out. Reason: %s", msg);
			notify_NotifySend(NULL, kNotifyType_ForcedDisconnect, msg, NULL, NULL);

			GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOGIN);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_WORLD_UPDATE, pak);
		{
			if (!gGCLState.bGotWorldUpdate)
			{
				gGCLState.bGotWorldUpdate = true;
			}
			
			if(!gGCLState.bSkipWorldUpdate)
			{
				worldReceiveUpdate(pak, demo_RecordMapName);
			}
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_WORLD_PERIODIC_UPDATE, pak)
		{
			worldReceivePeriodicUpdate(pak);
		}
		PACKET_CASE_END(pak);
		
		PACKET_CASE(CHATRELAY_CMD_PUBLIC, pak)
		{
			HandleGameMsgs(pak, NULL, link);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(CHATRELAY_CMD_PRIVATE, pak)
		{
			HandleGameMsgs(pak, NULL, link);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_WORLD_REPLY, pak);
		{
#ifndef NO_EDITORS
			editLibHandleServerReply(pak);
#endif
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_GENERAL_UPDATE, pak);
		{
			if (!gGCLState.bGotGeneralUpdate)
			{			
				gGCLState.bGotGeneralUpdate = true;
			}
			gclHandleGeneralUpdate(pak);
		}
		PACKET_CASE_END(pak);
		

		PACKET_CASE(TOCLIENT_GAME_SERVER_ADDRESS, pak);
		{
			HandleReturnedServerAddress(pak);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_GAME_START_TRANSFER, pak);
		{
			HandleStartTransfer(pak);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_GAME_CONNECT_SUCCESS, pak);
		{
			HandleServerConnectSuccess(pak);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_GAME_CONNECT_FAILURE, pak);
		{
			HandleServerConnectFailure(pak);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_GAME_TRANSFER_FAILED, pak);
		if (!GSM_IsStateActive(GCL_LOGIN_FAILED))
		{
			gclLoginFail(pktGetStringTemp(pak));
		}
		PACKET_CASE_END(pak);	

		PACKET_CASE(TOCLIENT_MICROTRANSACTION_CATEGORY, pak);
		{
			MicroTrans_SetShardCategory(pktGetU32(pak));
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_DEBUG_BEACONSTUFF, pak);
		{
			#if !PLATFORM_CONSOLE
				beaconHandleDebugMsg(pak);
			#endif
		}
		PACKET_CASE_END(pak);
		
		PACKET_CASE(TOCLIENT_MOVEMENT_CLIENT, pak);
		{
			mmClientReceiveFromServer(pak);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_LOGIN_SAFE_PERIOD_PASSED, pak)
		{
			handleLoginSafePeriodPassed(pak);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_ENTITY_DEBUG, pak)
		{
			combatdebug_HandlePacket(pak);
		}
		PACKET_CASE_END(pak);

		xdefault:
		{
			START_BIT_COUNT(pak, "recv:GAME_COMMAND");
			GameClientHandlePktInput(pak,cmd);
			STOP_BIT_COUNT(pak);
		}
	}
	
	mmClientRecordPacketSizeFromServer(	cmd == TOCLIENT_GENERAL_UPDATE,
										pktGetIndex(pak) - packetIndexStart);

	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////

