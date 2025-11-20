/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalComm.h"
#include "GameServerLib.h"
#include "gslDoorTransition.h"
#include "gslExtern.h"
#include "gslHandleMsg.h"
#include "gslEntity.h"
#include "EntityLib.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "EntityNet.h"
#include "gslEventSend.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "dynNode.h"
#include "gslCommandParse.h"
#include "error.h"
#include "file.h"
#include "EditorServerMain.h"
#include "EntityMovementManager.h"
#include "EntityIterator.h"
#include "gslSendToClient.h"
#include "gslTransactions.h"
#include "testclient_comm.h"
#include "cmdparse.h"
#include "textparser.h"
#include "ServerLib.h"
#include "gslBugReport.h"
#include "sock.h"
#include "ResourceManager.h"
#include "inventoryCommon.h"
#include "gslSavedPet.h"
#include "Character.h"
#include "Character_tick.h"
#include "CostumeCommonEntity.h"
#include "cutscene.h"
#include "logging.h"
#include "netprivate.h" //so we can check pkt->error_occurred without function call overhead
#include "notifycommon.h"
#include "Player.h"
#include "gslInterior.h"
#include "Player_h_ast.h"
#include "XBoxStructs_h_ast.h"
#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "timedCallback.h"
#include "GamePermissionsCommon.h"
#include "GamePermissionTransactions.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "gslTriggerCondition.h"
#include "gslSavedPet.h"
#include "inventoryTransactions.h"
#include "gslUGC_cmd.h"
#include "Alerts.h"

bool gLoginAttempted = false;

void DEFAULT_LATELINK_SendClientGameSpecificLoginInfo(Entity *pEntity)
{
	//Do nothing
	//Placeholder for LATELINK calls
}

void gamePermissions_UpdateEntityNumerics(Entity *pEntity)
{
	PERFINFO_AUTO_START_FUNC();

	if(pEntity && pEntity->pPlayer && gamePermission_Enabled())
	{
		// check all numeric permissions and compare them to the character if they are changed run the transaction
		GameAccountData *pData = entity_GetGameAccount(pEntity);
		if(pData && GamePermissions_trh_UpdateNumerics(CONTAINER_NOCONST(Entity, pEntity), CONTAINER_NOCONST(GameAccountData, pData), false, false))
		{
			AutoTrans_GamePermissions_tr_UpdateNumerics(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pEntity->myContainerID, GLOBALTYPE_GAMEACCOUNTDATA, pData->iAccountID);
		}
	}

	PERFINFO_AUTO_STOP();
}


static void FixupBags_CB(TransactionReturnVal *pVal, void *userData)
{
	Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData);
	if(pEnt && pEnt->pPlayer)
	{
		HandlePlayerLogin_Success(pEnt, kLoginSuccess_InvBagFixup);
	}
}

void HandlePlayerLogin_EntityTasks(Entity *pEntity)
{
	PERFINFO_AUTO_START_FUNC();

	if (pEntity->pChar && !pEntity->pChar->bLoaded)
	{
		// If something went wrong with copying saved AttribMods, re-copy the saved mods from the PuppetEntity
		if (pEntity->pSaved->pPuppetMaster &&
			pEntity->pSaved->pPuppetMaster->uPuppetSwapVersion != pEntity->pSaved->pPuppetMaster->uSavedModsVersion)
		{
			gslTransformToPuppet_HandleLoadMods(pEntity);
		}
		character_LoadNonTransact(entGetPartitionIdx(pEntity), pEntity, false);
	}

	// cache numeric limits that are limited by GamePermissions
	gamePermissions_UpdateEntityNumerics(pEntity);

	gslExternPlayerLogin(pEntity);	
	gslPlayerEnteredMap(pEntity, true);	

	PERFINFO_AUTO_STOP();
}

void HandlePlayerLogin_Success(Entity *pEntity, PlayerLoginWait eWaitSucceeded)
{
	if(!pEntity
		|| !pEntity->pPlayer
		|| !pEntity->pSaved)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

    //clear the SkippedSuccessOnLogin flag for puppets
    //This must happen before any of the early out returns except for the pointer null check one above.
    if(pEntity->pSaved->pPuppetMaster && ( eWaitSucceeded & kLoginSuccess_PuppetSwap ) )
        pEntity->pSaved->pPuppetMaster->bSkippedSuccessOnLogin = false;

	//Clear the flag that just succeeded
	if(eWaitSucceeded)
		pEntity->pPlayer->eLoginWaiting &= ~(eWaitSucceeded);

	if(pEntity->pSaved->uGameSpecificFixupVersion != (U32)gameSpecificFixup_Version())
	{
        if ( pEntity->pSaved->uGameSpecificFixupVersion > (U32)gameSpecificFixup_Version() )
        {
            if ( isProductionMode() )
            {
                TriggerAlert("ENTITY_HAS_FUTURE_FIXUP", 
                    STACK_SPRINTF("Player Entity %u has fixup version %u, which is higher than the game build(%d).  It is likely that the build was rolled back without rolling back the database.", 
                        entGetContainerID(pEntity), pEntity->pSaved->uGameSpecificFixupVersion, gameSpecificFixup_Version()),
                    ALERTLEVEL_CRITICAL, ALERTCATEGORY_PROGRAMMER, 0, 0, 0, 0, 0, NULL, 0);
            }
            else
            {
                ErrorDetailsf("Entity=%u, EntityFixupversion=%u, BuildFixupVersion=%d", entGetContainerID(pEntity), pEntity->pSaved->uGameSpecificFixupVersion, gameSpecificFixup_Version());
                Errorf("Player entity has fixup version which is higher than the game build.");
            }
        }

		pEntity->pPlayer->eLoginWaiting |= kLoginSuccess_Fixup;
		
		PERFINFO_AUTO_START("gameSpecificFixup",1);
		gameSpecificFixup(pEntity);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_STOP();
		return;
	}

	if(pEntity->pPlayer->eLoginWaiting || ( pEntity->pSaved->pPuppetMaster && pEntity->pSaved->pPuppetMaster->bSkippedSuccessOnLogin ))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if(inv_NeedsBagFixup(pEntity))
	{
		pEntity->pPlayer->eLoginWaiting |= kLoginSuccess_InvBagFixup;

		inv_FixupBags(pEntity, FixupBags_CB, (void*)(intptr_t)entGetRef(pEntity));

		PERFINFO_AUTO_STOP();
		return;
	}

	HandlePlayerLogin_EntityTasks(pEntity);

 	gslSendLoginSuccess(pEntity->pPlayer->clientLink);
	Entity_Login_ApplyRegionRules(pEntity);

	PERFINFO_AUTO_START("SendClientGameSpecificLoginInfo", 1);
	SendClientGameSpecificLoginInfo(pEntity);
	PERFINFO_AUTO_STOP();

	pEntity->pPlayer->iLastUGCAccountRequestTimestamp = 0;

	// Stash UGCAccount data
	if(entGetAccountID(pEntity))
	{
		if(entity_IsUGCCharacter(pEntity)) // we are on the same shard as the UGCDataManager, so we can get a copy of the data that is automatically synced
		{
			char idBuf[128];
			SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCACCOUNT), ContainerIDToString(entGetAccountID(pEntity), idBuf), pEntity->pPlayer->hUGCAccount);
		}
		else // we are on a different shard as the UGCDataManager. Inter-shard remote commands are used to provide this data.
			gslUGC_RequestAccountThrottled(pEntity);
	}

	if(bPetTransactionDebug)
		printf("%s: Handle Login Success\n", pEntity ? pEntity->debugName : "NULL");
		
	PERFINFO_AUTO_STOP();
}

void InformClientOfSuccessfulLoginCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	EntityRef iRef = (EntityRef)((intptr_t)userData);
	Entity *pEnt = entFromEntityRefAnyPartition(iRef);
	ClientLink *pLink;
	if (pEnt && (pLink = entGetClientLink(pEnt)))
	{
		if (pLink->netLink)
		{
			Packet *pak = pktCreate(pLink->netLink, TOCLIENT_LOGIN_SAFE_PERIOD_PASSED);
			pktSend(&pak);
		}
	}
}

void HandlePlayerLogin_EarlyEntityTasks(Entity *pEnt)
{
	GameAccountDataExtract *pExtract;

	PERFINFO_AUTO_START_FUNC();
	
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	// Cache the region
	gslCacheEntRegion(pEnt,pExtract);

	// Check if needs puppet swap
	Entity_PuppetCheck(pEnt);

	// Check on pets
	Entity_PetCheck(pEnt);

	// Inform the interior system that a player is entering the map
	gslInterior_PlayerEntering(pEnt);

	PERFINFO_AUTO_STOP();
}

void HandlePlayerLogin(Packet *pak, ClientLink *clientLink)
{
	EntityIterator *iter;
	Entity *pEnt;
	char *name;
	int cookie;
	int needsFileUpdates;
	int noTimeout;
	int locale;
	ContainerID iEntContainerID;

	NOCONST(CrypticXnAddr) xnAddr;
	U64 xuid = 0;
	
	gLoginAttempted = true;

	if(clientLink->clientLoggedIn || clientLink->disconnected)
	{
		// This is a bad client.
		log_printf(LOG_LOGIN, "Client %s sent login packet on already logged in or disconnected link\n", gslGetAccountNameForLink(clientLink));
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	name = pktGetStringTemp(pak);
	cookie = pktGetBitsPack(pak,4);
	iEntContainerID = GetContainerIDFromPacket(pak);
	needsFileUpdates = pktGetBits(pak,1) && isDevelopmentMode();
	noTimeout = pktGetBits(pak,1);
	locale = pktGetU32(pak);
	clientLink->clientLangID = locGetLanguage(locale);

	if(pktGetBits(pak, 1)){
		clientLink->clientInfoString = pktMallocString(pak);
	}

	// Do we have XBOX Network information?
	if(pktGetBits(pak, 1))
	{
		// XUID
		xuid = pktGetBits64(pak, 64);		
		// XNADDR
		pktGetBytes(pak, sizeof(xnAddr.abEnet), &xnAddr.abEnet);
		pktGetBytes(pak, sizeof(xnAddr.abOnline), &xnAddr.abOnline);
		xnAddr.ina = pktGetU32(pak);
		xnAddr.inaOnline = pktGetU32(pak);
		xnAddr.wPortOnline = pktGetBits(pak, 16);
	}

	clientLink->iHighestActiveEntityDuringSendLastFrame = -1;

	iter = entGetIteratorSingleTypeAllPartitions(ENTITYFLAG_PLAYER_DISCONNECTED | ENTITYFLAG_PLAYER_LOGGING_IN, 0, GLOBALTYPE_ENTITYPLAYER);

	// Loop to find the player
	while (pEnt = EntityIteratorGetNext(iter))
	{
		if (pEnt->pPlayer && !pEnt->pPlayer->clientLink && pEnt->pPlayer->loginCookie == cookie && entGetContainerID(pEnt) == iEntContainerID)
		{
			// Found the player.

			HandlePlayerLogin_EarlyEntityTasks(pEnt);
			
			// Mark player as logged in
			gslPlayerLoggedIn(clientLink, pEnt, noTimeout, locale);
			entGetPlayer(pEnt)->needsFileUpdates = needsFileUpdates;

			//I'm waiting on my game account data still before login is considered successful
			if(entGetAccountID(pEnt) &&		//So the continuous builder won't fail to login
				pEnt->pPlayer->pPlayerAccountData && 
				!GET_REF(pEnt->pPlayer->pPlayerAccountData->hData))
			{
				pEnt->pPlayer->eLoginWaiting |= kLoginSuccess_GameAccount;
			}

			// If puppet swap may be necessary, skip the successful login
			// This will later be checked in Entity_PuppetMasterTick()
			// If you change the internals of this if statement, do the same in Entity_PuppetMasterTick()
			if(		!pEnt->pSaved 
				||	!pEnt->pSaved->pPuppetMaster 
				||	(pEnt->pSaved->pPuppetMaster->bPuppetCheckPassed && !entCheckFlag(pEnt,ENTITYFLAG_PUPPETPROGRESS))
				||  !Entity_PuppetRegionValidate(pEnt))
			
			{
				HandlePlayerLogin_Success(pEnt, kLoginSuccess_PuppetSwap);

				entClearCodeFlagBits(pEnt,ENTITYFLAG_PLAYER_DISCONNECTED);
				entClearCodeFlagBits(pEnt,ENTITYFLAG_PLAYER_LOGGING_IN);
			}
			else
			{
				pEnt->pSaved->pPuppetMaster->bSkippedSuccessOnLogin = true;
			}

			if (xuid > 0)
			{
				// Update the XBOX specific data
				AutoTrans_gslXBox_trSetXBoxSpecificData(NULL, GLOBALTYPE_GAMESERVER, entGetType(pEnt), 
					entGetContainerID(pEnt), (CrypticXnAddr *)&xnAddr, xuid);
			}
			else
			{
				if (pEnt->pPlayer->pXBoxSpecificData != NULL)
				{
					// Delete all XBOX specific data
					AutoTrans_gslXBox_trDeleteXBoxSpecificData(NULL, GLOBALTYPE_GAMESERVER, entGetType(pEnt),
						entGetContainerID(pEnt));
				}
			}
			
			break;
		}
	}
	EntityIteratorRelease(iter);

	if (!pEnt)
	{
		gslSendLoginFailure(clientLink,"Can't find corresponding entity!");
		// Failed to log in
	}
	else
	{
		//if the client remains logged in for half a second, then we decide that the transfer was "successful", and tell the client
		//to clear their "I am in the middle of a transfer, things might be bad" flag
		TimedCallback_Run(InformClientOfSuccessfulLoginCB, (void*)((intptr_t)((entGetRef(pEnt)))), 0.5f);
	}

	PERFINFO_AUTO_STOP();
}

void HandleDoneLoading_Entity(Entity *pEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	// Make visible, but only if not in stasis.
	if(pEnt->pPlayer->iStasis < timeSecondsSince2000()) {
		gslEntitySetInvisibleTransient(pEnt, 0);
	}
	entClearCodeFlagBits(pEnt,ENTITYFLAG_IGNORE);
	mmDisabledHandleDestroy(&pEnt->mm.mdhIgnored);
			
	if(pEnt->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		//When done loading a character, tick the character completely one time.  This should
		// reapply passives and toggles when zoning or logging in.
		//FIXME:  Ticking the player's character after loading may have unintended consequences.
		// If there are any issues in any of the tick phases occuring when a player zones or
		// logs in, then this is likely the problem.  Come talk to Jered or Ben H if there are
		// any issues resulting from this lovely little HACK.
		character_TickPhaseOne(iPartitionIdx, pEnt->pChar, gConf.combatUpdateTimer, pExtract);
		character_TickPhaseTwo(iPartitionIdx, pEnt->pChar, gConf.combatUpdateTimer, pExtract);
		character_TickPhaseThree(iPartitionIdx, pEnt->pChar, gConf.combatUpdateTimer, pExtract);
				
		character_SendClientInitData(pEnt->pChar);
	}

	if (pEnt->pPlayer) 
	{
		cutscene_PlayerDoneLoading(pEnt);

		gslEntityPlaySpawnTransitionSequence(pEnt, false);

		eventsend_PlayerSpawnIn(pEnt);

		if (gConf.bDisplayFreeCostumeChangeAfterLoad)
		{
			// Check for free costume change.
			if(costumeEntity_GetFreeChangeTokens(NULL, pEnt) > 0)
			{
				notify_NotifySend(pEnt, kNotifyType_FreeCostumeChange, TranslateMessageKeySafe("InvalidCostume_FreeCostumeChange"), NULL, NULL);
			}
		}
	}

	// Send initial FX conditions.
	triggercondition_InitClientTriggerConditions(pEnt);
}

void HandleReadyForGeneralUpdates(ClientLink *clientLink)
{
	clientLink->readyForGeneralUpdates = true;
}

void HandleDoneLoading(Packet *pak, ClientLink *clientLink)
{
	Entity *pEnt;
	int i;

	// Make sure that the clientLink is flagged as ready for general updates
	HandleReadyForGeneralUpdates(clientLink);

	for (i = 0; i < eaiSize(&clientLink->localEntities); i++)
	{
		pEnt = entFromEntityRefAnyPartition(clientLink->localEntities[i]);
		if (pEnt)
		{
			HandleDoneLoading_Entity(pEnt);
		}
		else
		{
			Errorf("Client sent \"done loading\" msg but his entity is gone, so I'm disconnecting him.");
			linkFlushAndClose(&clientLink->netLink, "Received \"done loading\" but my entity is gone.");
			break;
		}
	}
}


static int HandleGameMsgs(Packet *pak, Entity *ent, NetLink *link)
{
	if (!ent || !ent->pPlayer || !ent->pPlayer->clientLink)
	{
		return 1;
	}
	assert(ent->pPlayer->clientLink->netLink == link);

	//pktAlignBitsArray(pak);
	//if we change the sending to use a bits array, uncomment this align

	while (!pktEnd(pak))
	{
		int lib = pktGetBits(pak,1);
		int command = pktGetBitsPack(pak,GAME_MSG_SENDBITS) | (lib * LIB_MSG_BIT);
		if (!(command & LIB_MSG_BIT))
		{
			GameServerHandlePktMsg(pak,command,ent);
		}
		else
		{
			if(!gslHandleMsg(pak,command,ent))
			{
				return 0;
			}
		}

	}
	return 1;
}

int gslHandleRefDictDataRequests(Packet *pPack, Entity *pEnt)
{
	ClientLink *pClientLink = (pEnt)->pPlayer->clientLink;

	return resServerProcessClientRequests(pPack, pClientLink->pResourceCache);
}

void gslHandleTestClientCommandRequests(Packet *pPack, Entity *pEnt)
{
	U32 iTestClientCmdID;
	char *msg = NULL;
	char *pchCommand;
	ClientLink *pClientLink = (pEnt)->pPlayer->clientLink;

	while((iTestClientCmdID = pktGetU32(pPack)))
	{
		CmdContext context = {0};
		bool bResult;

		if(!pClientLink->pPendingTestClientCommandsPacket)
		{
			pClientLink->pPendingTestClientCommandsPacket = pktCreateTemp(entGetNetLink(pEnt));
		}

		pktSendU32(pClientLink->pPendingTestClientCommandsPacket, iTestClientCmdID);

		InitCmdOutput(context, msg);
		context.access_level = entGetAccessLevel(pEnt);

		pchCommand = pktGetStringTemp(pPack);
		bResult = cmdCheckSyntax(&gGlobalCmdList, pchCommand, &context);

		if(!bResult)
		{
			pktSendBits(pClientLink->pPendingTestClientCommandsPacket, 1, 0);
			pktSendString(pClientLink->pPendingTestClientCommandsPacket, msg);
			CleanupCmdOutput(context);
			continue;
		}

		CleanupCmdOutput(context);
		bResult = GameServerParsePublic(pchCommand, 0, pEnt, &msg, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);

		if(bResult)
		{
			pktSendBits(pClientLink->pPendingTestClientCommandsPacket, 1, 1);
			pktSendString(pClientLink->pPendingTestClientCommandsPacket, context.found_cmd->name);
			pktSendString(pClientLink->pPendingTestClientCommandsPacket, msg);
		}
		else
		{
			pktSendBits(pClientLink->pPendingTestClientCommandsPacket, 1, 0);
			pktSendString(pClientLink->pPendingTestClientCommandsPacket, msg);
		}

		estrDestroy(&msg);
	}
}






int gslHandleMsg(Packet* pak, int cmd, Entity *ent)
{
	S32 ret = 1;
	U32 packetIndexStart;
	
	if (!ent->pPlayer->clientLink)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	packetIndexStart = pktGetIndex(pak);
	
	switch (cmd)
	{
	xcase GAMECLIENTLIB_CMD_PUBLIC:
		PERFINFO_AUTO_START("GAMECLIENTLIB_CMD_PUBLIC", 1);
		{
			char *pErrorString = NULL;
			CmdParseStructList structList = {0};
			cmdParseGetStructListFromPacket(pak, &structList, &pErrorString, true);
			if (!eaSize(&structList.ppEntries) && pErrorString)
			{
				NetLink *pLink = pktLink(pak);

				Errorf("Data corruption in link %s: %s",
					pLink ? linkDebugName(pLink) : "(none)", pErrorString);
				pktSetErrorOccurred(pak, "Data corruption detected. See errorf.");
				ret = 0;
			}
			else
			{
				char *pString = pktGetStringTemp(pak);
				U32 iFlags = pktGetBits(pak, 32);
				enumCmdContextHowCalled eHow = pktGetBits(pak, 32);
				GameServerParsePublic(pString, iFlags | CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL, ent, NULL, -1, eHow, &structList);
			}
			cmdClearStructList(&structList);
		}
		PERFINFO_AUTO_STOP();
	xcase GAMECLIENTLIB_CMD_PRIVATE:
		PERFINFO_AUTO_START("GAMECLIENTLIB_CMD_PRIVATE", 1);
			{
				char *pErrorString = NULL;
				CmdParseStructList structList = {0};
				cmdParseGetStructListFromPacket(pak, &structList, &pErrorString, true);
				if (!eaSize(&structList.ppEntries) && pErrorString)
				{
					NetLink *pLink = pktLink(pak);

					Errorf("Data corruption in link %s: %s",
						pLink ? linkDebugName(pLink) : "(none)", pErrorString);
					pktSetErrorOccurred(pak, "Data corruption detected. See errorf.");
					ret = 0;
				}
				else
				{			
					char *pString = pktGetStringTemp(pak);
					U32 iFlags = pktGetBits(pak, 32);
					enumCmdContextHowCalled eHow = pktGetBits(pak, 32);

					bool bUnknownCommand = false;

					GameServerParsePrivate(pString, iFlags | CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL, ent, -1, &bUnknownCommand, eHow, &structList);

					if (bUnknownCommand)
					{
						Errorf("POSSIBLE ASSERT: Server received unrecognized private command %s. You probably don't have the required accesslevel.", pString);
						pktSetErrorOccurred(pak, "Unrecognized private server command. See errorf.");
					}
				}
				cmdClearStructList(&structList);
			}
		PERFINFO_AUTO_STOP();
	xcase GAMECLIENTLIB_SEND_PHYSICS:
		PERFINFO_AUTO_START("GAMECLIENTLIB_SEND_PHYSICS", 1);
		{
			mmReceiveFromClient(ent->pPlayer->clientLink->movementClient, pak);
		}
		PERFINFO_AUTO_STOP();
	xcase GAMECLIENTLIB_SEND_REFDICT_DATA_REQUESTS:
		PERFINFO_AUTO_START("GAMECLIENTLIB_SEND_REFDICT_DATA_REQUESTS", 1);
		{
			ret = gslHandleRefDictDataRequests(pak, ent);
		}
		PERFINFO_AUTO_STOP();

	xcase GAMECLIENTLIB_SEND_TESTCLIENT_COMMAND_REQUESTS:
		PERFINFO_AUTO_START("GAMECLIENTLIB_SEND_TESTCLIENT_COMMAND_REQUESTS", 1);
		gslHandleTestClientCommandRequests(pak, ent);
		PERFINFO_AUTO_STOP();
		
	xcase GAMECLIENTLIB_SEND_SCREENSHOT:
		PERFINFO_AUTO_START("GAMECLIENTLIB_SEND_SCREENSHOT", 1);
		{
			gslHandleBugOrTicket(pak, ent);
		}
		PERFINFO_AUTO_STOP();

	xdefault:
		PERFINFO_AUTO_START("bad cmd", 1);
		Errorf("Invalid Client msg received: %d", cmd);
		pktSetErrorOccurred(pak, "Unknown client msg received. See errorf.");
		ret = 0;
		PERFINFO_AUTO_STOP();
	}

	if(SAFE_MEMBER2(ent->pPlayer, clientLink, movementClient)){
		mmClientRecordPacketSizeFromClient(	ent->pPlayer->clientLink->movementClient,
											cmd == GAMECLIENTLIB_SEND_PHYSICS,
											pktGetIndex(pak) - packetIndexStart);
	}

	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}


void gslHandleInput(Packet* pak, int cmd, NetLink* link, ClientLink *clientLink)
{
	if (clientLink->disconnected)
	{
		// Already disconnected
		return;
	}

	switch (cmd)
	{
	xcase TOSERVER_GAME_MSG:
		PERFINFO_AUTO_START("TOSERVER_GAME_MSG", 1);
		{
			EntityRef ref = entReceiveRef(pak);
			if (gslLinkOwnsRef(clientLink, ref))
				HandleGameMsgs(pak,entFromEntityRefAnyPartition(ref),link);
			else
				log_printf(LOG_CLIENTSERVERCOMM, "TOSERVER_GAME_MSG : Client %s tried to send entity command for an entity it does not own.", gslGetAccountNameForLink(clientLink));
		}
		PERFINFO_AUTO_STOP();
	xcase TOSERVER_GAME_LOGIN:
		PERFINFO_AUTO_START("TOSERVER_GAME_LOGIN", 1);
		{
			HandlePlayerLogin(pak,clientLink);
		}
		PERFINFO_AUTO_STOP();
	xcase TOSERVER_DONE_LOADING:
		PERFINFO_AUTO_START("TOSERVER_DONE_LOADING", 1);
		{
			HandleDoneLoading(pak,clientLink);
		}
		PERFINFO_AUTO_STOP();
	xcase TOSERVER_READY_FOR_GENERAL_UPDATES:
		PERFINFO_AUTO_START("TOSERVER_READY_FOR_GENERAL_UPDATES", 1);
		{
			HandleReadyForGeneralUpdates(clientLink);
		}
		PERFINFO_AUTO_STOP();
	xcase TOSERVER_REQ_UPDATE:
		PERFINFO_AUTO_START("TOSERVER_REQ_UPDATE", 1);
		{
			int full_update = pktGetBits(pak, 1);
			gslSendWorldUpdate(clientLink,full_update, true);
		}
		PERFINFO_AUTO_STOP();
	xcase TOSERVER_LOCKED_UPDATE:
		PERFINFO_AUTO_START("TOSERVER_LOCKED_UPDATE", 1);
		{
			if (clientLink->accessLevel >= ACCESS_DEBUG)
				worldReceiveLockedUpdate(pak, link);
			else
				log_printf(LOG_CLIENTSERVERCOMM, "TOSERVER_LOCKED_UPDATE : Client %s tried to send editor commands without the appropriate access level.", gslGetAccountNameForLink(clientLink));
		}
		PERFINFO_AUTO_STOP();
	xcase TOSERVER_EDIT_MSG:
		PERFINFO_AUTO_START("TOSERVER_EDIT_MSG", 1);
		{
			if (clientLink->accessLevel >= ACCESS_DEBUG || isProductionEditMode())
				editorHandleGameMsg(pak, link, clientLink->pResourceCache);
			else
				log_printf(LOG_CLIENTSERVERCOMM, "TOSERVER_EDIT_MSG : Client %s tried to send editor commands without the appropriate access level.", gslGetAccountNameForLink(clientLink));
		}
		PERFINFO_AUTO_STOP();
	xcase TOSERVER_MOVEMENT_CLIENT:
		PERFINFO_AUTO_START("TOSERVER_MOVEMENT_CLIENT", 1);
		{
			mmClientReceiveFromClient(clientLink->movementClient, pak);
		}
		PERFINFO_AUTO_STOP();
	xcase TOSERVER_CLIENT_PATCHING_WORLD:
		PERFINFO_AUTO_START("TOSERVER_CLIENT_PATCHING_WORLD", 1);
		{
			clientLink->uClientPatchTm = timeSecondsSince2000();
		}
		PERFINFO_AUTO_STOP();
	xdefault:
		PERFINFO_AUTO_START("default", 1);
		{		
			GameServerHandlePktInput(pak,cmd);
		}
		PERFINFO_AUTO_STOP();
	}	

	if (pak->error_occurred)
	{
		gslHandleClientPacketCorruption(clientLink, "Packet corruption occurred");
	}
}
