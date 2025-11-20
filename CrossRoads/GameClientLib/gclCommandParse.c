/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameAccountDataCommon.h"
#include "CombatDebug.h"
#include "GameClientLib.h"
#include "gclSendToServer.h"
#include "gclCommandParse.h"
#include "gclControlScheme.h"
#include "gclUtils.h"
#include "GfxConsole.h"
#include "GfxDebug.h"
#include "GfxMaterials.h"
#include "strings_opt.h"
#include "EntityIterator.h"
#include "EntityMovementFlight.h"
#include "EntityMovementProjectile.h"
#include "Character.h"
#include "Character_target.h"
#include "CombatConfig.h"
#include "Powers.h"
#include "Powers_h_ast.h"
#include "PowerActivation.h"
#include "PowerAnimFx.h"
#include "PowersMovement.h"
#include "RegionRules.h"
#include "wlTime.h"
#include "WorldColl.h"
#include "EditLib.h"
#include "EditorManager.h"
#include "testclient_comm.h"
#include "gclEntity.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "WorldGrid.h"
#include "sysutil.h"
#include "gclLogin.h"
#include "gclDemo.h"
#include "logging.h"
#include "CommandTranslation.h"
#include "GimmeDLLWrapper.h"
#include "ControllerScriptingSupport.h"
#include "FolderCache.h"
#include "gclNotify.h"
#include "Player.h"
#include "Prefs.h"
#include "EntitySavedData.h"
#include "MapDescription.h"
#include "gclPatchStreaming.h"
#include "StringUtil.h"
#include "EntityMovementTactical.h"
#include "GameEvent.h"
#include "GameEventDebugger.h"
#include "sock.h"
#include "AutoGen/GameClientLib_h_ast.h"
#include "cmdParse_h_ast.h"
#include "gclChatAutoComplete.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static int	staticCmdTimeStamp = 0;

int access_override = 0;

extern int g_force_sockbsd;



AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void SetTestingMode(int iSet)
{
	gbGclTestingMode = !!iSet;
	if (iSet)
	{
		access_override = ACCESS_DEBUG;
	}
}

void GameClientAccessOverride(int level)
{
	access_override = level;
}

char *encrypedKeyIssuedTo=NULL;


int EncryptedKeyedAccessLevel(void)
{
#if 0
FIXME2 newnet do we need blowfish?

	static bool inited=false;
	static int value=0;
	if (!inited) {
		const char *keyfile = "./devrel.key";
		if (fileExists(keyfile)) {
			U32 key[4] = {0x220CAE0A, 0xE5E0B992, 0xBC7A3D03, 0x6D83E9E7};
			int datalen;
			char *data = fileAlloc(keyfile, NULL);
			char *args[4];
			int numargs;
			BLOWFISH_CTX blowfish_ctx;
			cryptBlowfishInit(&blowfish_ctx,(U8*)key,sizeof(key));
			datalen = *((U32*)data);
			cryptBlowfishDecrypt(&blowfish_ctx,data+4, ((datalen+7)&~7));
			numargs = tokenize_line_safe(data+4, args, ARRAY_SIZE(args), NULL);
			if (numargs==3 && stricmp(args[0], "CRYPTIC")==0)
			{
				encrypedKeyIssuedTo = strdup(args[2]);
				value = atoi(args[1]);
			}
		}
		inited = true;
	}
	return value;
#else
	return 0;
#endif
}


int GameClientAccessLevel(Entity *p, GameClientAccessLevelFlags eFlags)
{
	if (!p)
	{
		p = entActivePlayerPtr();
	}

	// When playing demos, allow all sorts of performance commands, etc
	if (demo_playingBack())
		return ACCESS_DEBUG;
	if (isDevelopmentMode() && !p)
		return ACCESS_DEBUG;

	//in UGC map editing mode, access level for the entity is always 2
	if (p && isProductionEditMode() && !gbUseRealAccessLevelInUGC && !(eFlags & IGNORE_UGC_MODIFICATIONS))
		return ACCESS_UGC;

	if (EncryptedKeyedAccessLevel() && (!p || EncryptedKeyedAccessLevel() > entGetAccessLevel(p)))
		return EncryptedKeyedAccessLevel();
	if (p)
		return entGetAccessLevel(p);
	return access_override;
}


void gclCmdSetTimeStamp(int timeStamp)
{
	staticCmdTimeStamp = timeStamp;
}


int gclCmdGetTimeStamp(void)
{
	return staticCmdTimeStamp;
}


int GameClientParseActive(const char *str, char **ppRetString, CmdContextFlag iCmdContextFlags, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	return GameClientParsePublic(str, iCmdContextFlags, entActivePlayerPtr(), ppRetString, iOverrideAccessLevel, eHow, pStructs);
}


// Any commands available via some keyboard binding can be invoked directly
// by passing in a string to this function.  Profile trickle rules apply.
// Returns 1 if all commands from the string were parsed successfully.
int GameClientParsePublic(const char *sourceStrOrig, CmdContextFlag iFlags, Entity *ent, char **ppRetString, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	char *lineBuffer, *sourceStr, *sourceStrStart = NULL;
	char *pInternalRetString = NULL;
		
	int parseResult;
	KeyBindProfile *curProfile;
	int iRet = 0;

	CmdPrintfFunc printFunc;

	switch (eHow)
	{
	case CMD_CONTEXT_HOWCALLED_DDCONSOLE:
		printFunc = printf;
		break;
	default:
		printFunc = conPrintf;
		break;
	}

	estrStackCreate(&pInternalRetString);
	estrStackCreate(&sourceStrStart);
	estrCopy2(&sourceStrStart,sourceStrOrig);
	sourceStr = sourceStrStart;

	while(sourceStr)
	{
		int iCntQuote=1;
		bool bHandled = false;
		CmdContext cmd_context = {0};
		cmd_context.language = (ent && ent->pPlayer) ? entGetLanguage(ent) : locGetLanguage(getCurrentLocale());

		cmd_context.eHowCalled = eHow;

		if (ppRetString)
		{
			cmd_context.output_msg = ppRetString;
		}
		else
		{
			cmd_context.output_msg = &pInternalRetString;
		}

		lineBuffer = cmdReadNextLine(&sourceStr);

		if (!keybind_GetProfileCount()) // At startup
		{
			cmd_context.flags = iFlags;
			cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : GameClientAccessLevel(ent, 0);
			cmd_context.pStructList = pStructs;
			parseResult = cmdParseAndExecute(&gGlobalCmdList,lineBuffer,&cmd_context);
			iRet = iRet || parseResult;
		}
		else
		{
			KeyBindProfileIterator iter;
			keybind_NewProfileIterator(&iter);
			while (curProfile = keybind_ProfileIteratorNext(&iter))
			{
				cmdContextReset(&cmd_context);
				if (!curProfile->pCmdList)
				{
					// This must be a keybind-only profile. Let it go through.
					continue;
				}
				cmd_context.eHowCalled = eHow;
				cmd_context.flags = iFlags;
				cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : GameClientAccessLevel(ent, 0);
				cmd_context.pStructList = pStructs;
				parseResult = cmdParseAndExecute(curProfile->pCmdList,lineBuffer,&cmd_context);
				iRet = iRet || parseResult;
				// If the command has been handled, break...
				if (parseResult) {
					// Command OK!
					cmdPrintPrettyOutput(&cmd_context, printFunc);
					bHandled = true;
					break;
				}
				// Else command was not handled
				if (cmd_context.found_cmd) {
					// Found the command, but a syntax error must have occurred!
					cmdPrintPrettyOutput(&cmd_context, printFunc);
					bHandled = true;
					break;
				}

				// If the command cannot be trickle down, break...
				if (!curProfile->bTrickleCommands && SAFE_DEREF(lineBuffer) != '-')
					break;
			}

			if (!bHandled) {
				if (gclServerIsConnected() && !(cmd_context.outFlags & CTXTOUTFLAG_NO_CLIENT_TO_SERVER_PROPOGATION)) {
					gclSendPublicCommand(iFlags, lineBuffer, eHow, pStructs);
				} 
				else
				{
					if (!(cmd_context.outFlags & CTXTOUTFLAG_NO_OUTPUT_ON_UNHANDLED))
					{
						cmdPrintPrettyOutput(&cmd_context, printFunc);
					}
				}				
			}
		}
	}

	estrDestroy(&pInternalRetString);
	estrDestroy(&sourceStrStart);

	return iRet;
}

// Runs a private command on the client
AUTO_COMMAND;
void PrivateCommand(CmdContext *context, ACMD_SENTENCE cmd)
{
	GameClientParsePrivate(cmd, 0, entActivePlayerPtr(), -1, context->eHowCalled, NULL);
}


// Like cmdParse, but does a private command
int GameClientParsePrivate(const char *str, CmdContextFlag iFlags, Entity *ent, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	char *msg = NULL;
	CmdContext cmd_context = {0};
	int parseResult;
	InitCmdOutput(cmd_context,msg);

	cmd_context.flags = iFlags;
	cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : GameClientAccessLevel(ent, 0);
	cmd_context.eHowCalled = eHow;
	cmd_context.pStructList = pStructs;
	parseResult = cmdParseAndExecute(&gPrivateCmdList,str,&cmd_context);

	if (!parseResult)
	{
		if (!cmd_context.found_cmd)
		{
			Errorf("Internal game command (%s) not found",str);
		}
		else
		{
			Errorf("Internal game command (%s) returned error %s",str,msg);
		}
	}

	CleanupCmdOutput(cmd_context);

	// If parseResult is null, do something useful, like tell in debug mode what went wrong
	return parseResult;
}

Packet *gpPacketForTestClientCommands = NULL;

void HandleCommandRequestFromTestClient(const char *pString, U32 iTestClientCmdID, NetLink *pLinkToTestClient)
{
	CmdContext cmd_context = {0};
	char *msg = NULL;
	cmd_context.access_level = ACCESS_DEBUG;
	cmd_context.flags |= CMD_CONTEXT_FLAG_TREAT_PRINTVARS_AS_SUCCESS;

	InitCmdOutput(cmd_context, msg);
	
	if(cmdParseAndExecute(&gGlobalCmdList, pString, &cmd_context))
	{
		//The command has been executed locally...send back our result immediately
		Packet *pkt = pktCreate(pLinkToTestClient, TO_TESTCLIENT_CMD_RESULT);
		pktSendU32(pkt, iTestClientCmdID);
		pktSendBits(pkt, 1, 1);
		pktSendString(pkt, cmd_context.found_cmd->name);
		pktSendString(pkt, msg);
		pktSend(&pkt);

		CleanupCmdOutput(cmd_context);
		return;
	}
	else if(cmd_context.found_cmd)
	{
		Packet *pkt = pktCreate(pLinkToTestClient, TO_TESTCLIENT_CMD_RESULT);
		pktSendU32(pkt, iTestClientCmdID);
		pktSendBits(pkt, 1, 0);
		pktSendString(pkt, msg);
		pktSend(&pkt);

		CleanupCmdOutput(cmd_context);
		return;
	}

	CleanupCmdOutput(cmd_context);

	if (!gpPacketForTestClientCommands)
	{
		gpPacketForTestClientCommands = pktCreateTemp(pLinkToTestClient);
	}
	
	pktSendU32(gpPacketForTestClientCommands, iTestClientCmdID);
	pktSendString(gpPacketForTestClientCommands, pString);
}




// earray of server commands
static StringArray serverCommands;


void gclAddServerCommand(char * scmd)
{
	if (gclIsServerCommand(scmd)) //don't leak
		return;
	eaPush(&serverCommands,strdup(scmd));
}


int gclIsServerCommand(char *str)
{
	int i = 0, num = eaSize(&serverCommands);

	for (;i < num; ++i)
	{
		if ( !stricmp(str, serverCommands[i]) )
			return 1;
	}

	return 0;
}

int gclCompleteCommand(char *str, char *out, int searchID, int searchBackwards)
{
/*	 static int num_cmds = 0,
		 num_client_cmds = 0,
		 num_control_cmds = 0,
		 num_svr_cmds = 0;

	 int i,
		cmd_offset = 0,
		server_cmd_offset = 0,
		client_cmd_offset = 0,
		control_cmd_offset = 0,
		total_cmds = 0;

	//static int server_searchID = 0;
	char searchStr[256] = {0}, foundStr[256] = {0};
	MRUList *consolemru = conGetMRUList();
	int num_mru_commands = 0;
	int effi;
	char uniquemrucommands[32][1024];

	if (num_cmds == 0)
	{
		num_cmds = cmdCount(game_cmds);
	}

	if ( !str || str[0] == 0 )
	{
		out[0] = 0;
		return 0;
	}

	for (i=consolemru->count-1; i>=0; i--)
	{
		char cmd[1024];
		bool good = true;
		int j;
		Strncpyt(cmd, consolemru->values[i]);
		if (strchr(cmd, ' '))
			*strchr(cmd, ' ')=0;
		for (j=0; j<num_mru_commands; j++) {
			if (stricmp(uniquemrucommands[j], cmd)==0) {
				good = false;
			}
		}
		if (good) {
			Strcpy(uniquemrucommands[num_mru_commands], cmd);
			num_mru_commands++;
		}
	}

	if ( !num_svr_cmds )
		num_svr_cmds = eaSize(&serverCommands);

	if ( !num_client_cmds )
	{
		if ( client_control_cmds )
		{
			i = 0;

			for (;;++i )
			{
				if ( client_control_cmds[i].name )
					continue;
				break;
			}
		}
			num_client_cmds = i;
	}

	if ( !num_control_cmds )
	{
		if ( control_cmds )
		{
			i = 0;
			for (;;++i )
			{
				if ( control_cmds[i].name )
					continue;
				break;
			}
		}
		num_control_cmds = i;
	}

	// figure out command offsets
	cmd_offset = num_mru_commands,
	server_cmd_offset = cmd_offset + num_cmds,
	client_cmd_offset = server_cmd_offset + num_svr_cmds,
	control_cmd_offset = client_cmd_offset + num_client_cmds,
	total_cmds = control_cmd_offset + num_control_cmds;


		Strcpy( searchStr, stripUnderscores(str) );

		i = searchBackwards ? searchID - 1 : searchID + 1;

		//for ( ; i < num_cmds; ++i )
		while ( i != searchID )
		{
			if ( i >=1 && i < cmd_offset + 1) {
				effi = i - 1;
				Strcpy( foundStr, uniquemrucommands[effi]);
				if ( strStartsWith(foundStr, searchStr) )
				{
					strcpy(out, foundStr);
					return i;
				}
			}
			else if ( i >= cmd_offset + 1 && i < server_cmd_offset + 1 )
			{
				effi = i - cmd_offset - 1;
				Strcpy( foundStr, stripUnderscores(game_cmds[effi].name) );

				if ( !(game_cmds[effi].flags & CMDF_HIDEPRINT) &&
					game_cmds[effi].access_level <= GameClientAccessLevel() && strStartsWith(foundStr, searchStr) )
				{
					//strcpy(out, game_cmds[i].name);
					strcpy(out, foundStr);
					return i;
				}
			}
			else if ( i >= server_cmd_offset + 1 && i < client_cmd_offset + 1 )
			{
				effi = i - server_cmd_offset - 1;
				Strcpy( foundStr, stripUnderscores(serverCommands[effi]) );

				if ( strStartsWith(foundStr, searchStr) )
				{
					//strcpy(out, serverCommands[i - num_cmds]);
					strcpy(out, foundStr);
					return i;
				}
			}
			else if ( i >= client_cmd_offset + 1 && i < control_cmd_offset + 1 )
			{
				effi = i - client_cmd_offset - 1;
				Strcpy( foundStr, stripUnderscores(client_control_cmds[effi].name) );

				if ( !(client_control_cmds[effi].flags & CMDF_HIDEPRINT) &&
					client_control_cmds[effi].access_level <= GameClientAccessLevel() &&
					strStartsWith(foundStr, searchStr) )
				{
					//strcpy(out, game_cmds[i].name);
					strcpy(out, foundStr);
					return i;
				}
			}
			else if ( i >= control_cmd_offset + 1 && i < total_cmds + 1 )
			{
				effi = i - control_cmd_offset - 1;
				Strcpy( foundStr, stripUnderscores(control_cmds[effi].name) );

				if ( !(control_cmds[effi].flags & CMDF_HIDEPRINT) &&
					control_cmds[effi].access_level <= GameClientAccessLevel() &&
					strStartsWith(foundStr, searchStr) )
				{
					//strcpy(out, game_cmds[i].name);
					strcpy(out, foundStr);
					return i;
				}
			}

			searchBackwards ? --i : ++i;

			if ( i >= total_cmds + 1 ) i = 0;
			else if ( i < 0 ) i = total_cmds;
		}

	if ( searchID && searchID != -1 )
	{
		if ( searchID < num_mru_commands + 1 && searchID - 1 > 0 )
		{
			int idx = searchID - 1;
			strcpy(out, idx >= 0 ? uniquemrucommands[idx] : "");
		}
		if ( searchID < num_mru_commands + num_cmds + 1 )
		{
			int idx = searchID - num_mru_commands - 1;
			strcpy(out, idx >= 0 ? game_cmds[idx].name : "");
		}
		else if ( searchID < num_mru_commands + num_cmds + num_svr_cmds + 1 )
		{
			int idx = searchID - num_mru_commands - num_cmds - 1;
			strcpy(out, idx >= 0 ? serverCommands[idx] : "");
		}
		else
			out[0] = 0;
	}
	else
		out[0] = 0;*/
	return 0; //fix this later
}



// Public Commands

// Powers

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void powersCmdCharacterInitData(ClientCharacterInitData *pdata)
{
	Entity* e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		int i;
		int iPartitionIdx;
		Character *pchar = e->pChar;
		
		// Set existing toggles to inactive and destroy the PowerActivation earray
		for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
		{
			Power *ppow = character_FindPowerByIDComplete(pchar,pchar->ppPowerActToggle[i]->ref.uiID);
			if(ppow)
			{
				power_SetActive(ppow,false);
			}
			if(pchar->ppPowerActToggle[i]==pchar->pPowActFinished)
			{
				pchar->pPowActFinished = NULL;
			}
			poweract_Destroy(pchar->ppPowerActToggle[i]);
		}
		eaDestroy(&pchar->ppPowerActToggle);

		// Make a new PowerActivation from each PowerActivationState
		for(i=eaSize(&pdata->ppActivationStateToggle)-1; i>=0; i--)
		{
			PowerActivationState *pState = pdata->ppActivationStateToggle[i];
			Power *ppow = character_FindPowerByIDComplete(pchar,pState->ref.uiID);
			if(ppow)
			{
				// Little more complicated for toggles
				PowerActivation *pact = poweract_Create();
				poweract_SetPower(pact,ppow);
				pact->uchID = pState->uchID;
				pact->uiPeriod = pState->uiPeriod;
				pact->fTimerActivate = pState->fTimerActivate;
				pact->fTimeCharged = pState->fTimeCharged;
				pact->fTimeActivating = pState->fTimeActivating;
				eaPush(&pchar->ppPowerActToggle,pact);

				// Ensure it's flagged as active
				power_SetActive(ppow,true);

				// TODO(JW): Toggles: I'm sure there's way more I need to do here eventually
			}
		}

		// Apply PowerRechargeState
		iPartitionIdx = entGetPartitionIdx(e);
		character_RechargeStateApply(iPartitionIdx,pchar,&pdata->rechargeState);

		// Rebuild CooldownTimers
		eaDestroyStruct(&pchar->ppCooldownTimers, parse_CooldownTimer);
		for(i=eaSize(&pdata->ppCooldownTimers)-1; i>=0; i--)
		{
			character_CategorySetCooldown(iPartitionIdx,pchar,pdata->ppCooldownTimers[i]->iPowerCategory,pdata->ppCooldownTimers[i]->fCooldown);
		}
	}
}

//Command to change the global cooldown on the fly
AUTO_COMMAND ACMD_PRIVATE ACMD_CATEGORY(Powers,Debug) ACMD_CLIENTCMD;
void PowersSetGlobalCooldownClient(F32 time)
{
	g_CombatConfig.fCooldownGlobal = time;
}

AUTO_COMMAND ACMD_CATEGORY(Powers,Debug);
void PowersDebugClient(S32 enabled)
{
	g_bPowersDebug = !!enabled;
}

AUTO_COMMAND ACMD_CATEGORY(Powers,Debug);
void PowersSelectDebugClient(S32 enabled)
{
	g_bPowersSelectDebug = !!enabled;
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CATEGORY(Powers,Debug) ACMD_CLIENTCMD;
void PowersDebugToggleFlagsClient(const char* flagname)
{
	combatdebug_SetDebugFlagByName(flagname);
}

AUTO_COMMAND ACMD_CATEGORY(Powers,Debug);
void PowersErrorsClient(S32 enabled)
{
	g_bPowersErrors = !!enabled;
}

// Toggles prediction of maintained powers
AUTO_COMMAND ACMD_NAME("PowersClientPredictMaintained") ACMD_CLIENTONLY;
void powersCmdClientPredictMaintained(S32 on)
{
	g_CombatConfig.bClientPredictMaintained = !!on;
}

static bool s_bClientPredictKnocks = true;

// Toggles prediction of knock effects
AUTO_COMMAND ACMD_NAME("Powers.ClientPredictKnocks");
void powersCmdClientPredictKnocks(bool bEnabled)
{
	s_bClientPredictKnocks = !!bEnabled;
}

// Client command to note the cost paid for an activation on the server.  Called by the server
//  when it needs to inform the client of paid costs.
AUTO_COMMAND ACMD_NAME("Powers.SetCostPaidServer") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CLIENTCMD;
void powersCmdSetCostPaidServer(U32 uiID, F32 fPaid)
{
	Entity* e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		Character *p = e->pChar;
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "SetCostPaidServer %d: %f\n",uiID,fPaid);
		if(p->pPowActCurrent && p->pPowActCurrent->uchID==uiID)
		{
			p->pPowActCurrent->fCostPaidServer = fPaid;
		}
		else if(p->pPowActQueued && p->pPowActQueued->uchID==uiID)
		{
			p->pPowActQueued->fCostPaidServer = fPaid;
		}
	}
}

// Client command to note the secondary cost paid for an activation on the server.  Called by the server
//  when it needs to inform the client of paid costs.
AUTO_COMMAND ACMD_NAME("Powers.SetCostPaidServerSecondary") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CLIENTCMD;
void powersCmdSetCostPaidServerSecondary(U32 uiID, F32 fPaid)
{
	Entity* e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		Character *p = e->pChar;
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "SetCostPaidServerSecondary %d: %f\n",uiID,fPaid);
		if(p->pPowActCurrent && p->pPowActCurrent->uchID==uiID)
		{
			p->pPowActCurrent->fCostPaidServerSecondary = fPaid;
		}
		else if(p->pPowActQueued && p->pPowActQueued->uchID==uiID)
		{
			p->pPowActQueued->fCostPaidServerSecondary = fPaid;
		}
	}
}

// Client command to interrupt a power activation.  Called by the server
//  when it needs to inform the client of an interrupt event.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CLIENTCMDFAST;
void PowersInterruptPower(U32 uiID, F32 fCharged, U32 uiTimestamp)
{
	Entity* e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		Character *p = e->pChar;
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Interrupt %d: Charge %f, Time %d\n",uiID,fCharged,uiTimestamp);
		if(p->pPowActCurrent && p->pPowActCurrent->uchID==uiID)
		{
			p->pPowActCurrent->fTimeCharged=fCharged;
			if(p->eChargeMode==kChargeMode_Current) 
			{
				p->pPowActCurrent->uiTimestampActivate = uiTimestamp;
				character_ActChargeToActivate(entGetPartitionIdx(e),p,p->pPowActCurrent,NULL);
			}
			else if(p->eChargeMode==kChargeMode_CurrentMaintain)
			{
				p->eChargeMode=kChargeMode_None;
			}
		}
		else if(p->pPowActQueued && p->pPowActQueued->uchID==uiID)
		{
			p->pPowActQueued->fTimeCharged=fCharged;
			if(p->eChargeMode==kChargeMode_Queued) 
			{
				p->pPowActQueued->uiTimestampActivate = uiTimestamp;
				character_ActChargeToActivate(entGetPartitionIdx(e),p,p->pPowActQueued,NULL);
			}
			else if(p->eChargeMode==kChargeMode_QueuedMaintain)
			{
				p->eChargeMode = kChargeMode_None;
			}
		}
	}
}

// Client command to cancel power activations.  Called by the server
//  when it damn well feels like it.  If the id is 0, it cancels all
//  the client's activations.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersCancelPowers(U32 uiID, bool bForceCurrent, bool bRecharge, U32 uiIDPower)
{
	Entity* e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		Character *p = e->pChar;
		int iPartitionIdx = entGetPartitionIdx(e);
		U32 uiTime = pmTimestamp(0);
		if(!uiID)
		{
			// Cancel them all
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "CancelPowers All: %d\n",uiTime);
			character_ActAllCancel(iPartitionIdx,p,bForceCurrent);
		}
		else
		{
			bool bFoundAct = false;

			if(p->pPowActCurrent && p->pPowActCurrent->uchID==uiID)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Current %d CancelPowers: %d\n",uiID,uiTime);
				character_ActCurrentCancel(iPartitionIdx,p,bForceCurrent,bRecharge);
				bFoundAct = true;
			}
			else if(p->pPowActQueued && p->pPowActQueued->uchID==uiID)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Queued %d CancelPowers: %d\n",uiID,uiTime);
				character_ActQueuedCancel(iPartitionIdx,p,NULL,0);
				bFoundAct = true;
			}
			else if(p->pPowActOverflow && p->pPowActOverflow->uchID==uiID)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Overflow %d CancelPowers: %d\n",uiID,uiTime);
				character_ActOverflowCancel(iPartitionIdx,p,NULL,0);
				bFoundAct = true;
			}
			else
			{
				// May have been an already completed toggle, check for that case (and make sure uiIDPower matches)
				int i;
				for(i=eaSize(&p->ppPowerActToggle)-1; i>=0; i--)
				{
					PowerActivation *pact = p->ppPowerActToggle[i];
					if(pact->uchID==uiID && pact->ref.uiID==uiIDPower)
					{
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Toggle %d CancelPowers: %d\n",uiID,uiTime);
						character_DeactivateToggle(iPartitionIdx,p,pact,NULL,uiTime,false);
						bFoundAct = true;
						break;
					}
				}

				if(i<0)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "UnknownActivation %d CancelPowers: %d\n",uiID,uiTime);
				}
			}

			// if we couldn't find the power and this was to recharge the power- try to find the power and apply the recharge
			if (bRecharge && !bFoundAct && uiIDPower)
			{
				Power *ppow = character_FindPowerByIDComplete(p,uiIDPower);
				if(ppow)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_RECHARGE, e, 
										"PowersCancelPowers cmd: Could not find activation- Recharging power.\n");
					power_SetRechargeDefault(iPartitionIdx, p, ppow, true);
					power_SetCooldownDefault(iPartitionIdx, p, ppow);
				}
			}
		}

		if(uiIDPower && !bRecharge)
		{
			Power *ppow = character_FindPowerByIDComplete(p,uiIDPower);
			
			if(ppow)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_RECHARGE, e, "PowersCancelPowers cmd: Clearing recharge for power.\n");
				power_SetRecharge(iPartitionIdx, p, ppow, 0);
			}
		}
	}
}

// Client command to reset PowerActivation Seq number
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowerActSeqReset(U8 uchReset)
{
	Entity* e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		e->pChar->uchPowerActSeqReset = uchReset;
	}
}

// Client command to reset PowerActivation Seq number using the normal non-fast command send
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void PowerActSeqResetSlow(U8 uchReset)
{
	Entity* e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		e->pChar->uchPowerActSeqReset = uchReset;
	}
}

// Client command to deactivate toggle powers.  Called by the server
//  when it damn well feels like it.  If the id is 0, it cancels all
//  the client's activations.
AUTO_COMMAND ACMD_NAME("Powers.DeactivateTogglePower") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CLIENTCMD;
void powersCmdDeactivateTogglePower(U32 uiID, int bRecharge)
{
	Entity* e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		Character *pchar = e->pChar;
		int iPartitionIdx = entGetPartitionIdx(e);
		if(!uiID)
		{
			character_DeactivateToggles(iPartitionIdx,pchar,pmTimestamp(0),bRecharge,false);
		}
		else
		{
			int i;
			PowerActivation *pact = NULL;
			for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
			{
				if(pchar->ppPowerActToggle[i]->uchID==uiID)
				{
					pact = pchar->ppPowerActToggle[i];
					break;
				}
			}

			if(pact)
			{
				character_DeactivateToggle(iPartitionIdx,pchar,pact,NULL,pmTimestamp(0),bRecharge);
			}
		}
	}
}

// Client command to stop maintaining a maintained power.  Called by the server
//  when it damn well feels like it, which is generally when it deactivates
//  a maintained power for a reason the client wouldn't necessarily predict.
AUTO_COMMAND ACMD_NAME("Powers.DeactivateMaintainedPower") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CLIENTCMD;
void powersCmdDeactivateMaintainedPower(U32 uiID)
{
	Entity* e = entActivePlayerPtr();
	if(e
		&& e->pChar
		&& e->pChar->pPowActCurrent
		&& e->pChar->pPowActCurrent->uchID==uiID
		&& e->pChar->eChargeMode==kChargeMode_CurrentMaintain)
	{
		e->pChar->eChargeMode = kChargeMode_None;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersPredictDisableTacticalInCombat(U32 uiTimestamp)
{
	Entity * pEnt = entActivePlayerPtr();
	if (pEnt)
	{
		// Disable tactical movement in combat
		mrTacticalNotifyPowersStart(pEnt->mm.mrTactical, 
			TACTICAL_COMBATDISABLE_UID, 
			combatconfig_GetTacticalDisableFlagsForCombat(), 
			uiTimestamp);
	}
}

// Client command to start a KnockBack, KnockUp or Push as predicted by the server
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersPredictKnock(EntityRef erTarget, S32 attrib, Vec3 vecDir, F32 fMagnitude, U32 uiTime, bool bInstantFacePlant, bool bProneAtEnd, F32 timer, bool bIgnoreTravelTime)
{
	Entity *e = entFromEntityRefAnyPartition(erTarget);
	if(s_bClientPredictKnocks && e)
	{
		if(attrib==kAttribType_KnockBack && vecDir)
		{
			pmKnockBackStart(e,vecDir,fMagnitude,uiTime, bInstantFacePlant, bProneAtEnd, timer, bIgnoreTravelTime);
		}
		else if(attrib==kAttribType_KnockUp)
		{
			pmKnockUpStart(e,fMagnitude,uiTime, bInstantFacePlant, bProneAtEnd, timer, bIgnoreTravelTime);
		}
		else if(attrib==kAttribType_Repel && vecDir)
		{
			pmPushStart(e,vecDir,fMagnitude,uiTime);
		}
		
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersPredictConstantForce(EntityRef erTarget, U32 id, U32 uiStartTime, U32 uiStopTime, Vec3 vForce)
{
	Entity *e = entFromEntityRefAnyPartition(erTarget);
	if(s_bClientPredictKnocks && e)
	{
		pmConstantForceStart(e, id, uiStartTime, uiStopTime, vForce);
		
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersPredictConstantForceWithRepeller(EntityRef erTarget, U32 id, U32 uiStartTime, U32 uiStopTime, EntityRef erRepeller, F32 fYawOffset, F32 fMagnitude)
{
	Entity *e = entFromEntityRefAnyPartition(erTarget);
	if(s_bClientPredictKnocks && e)
	{
		pmConstantForceStartWithRepeller(e, id, uiStartTime, uiStopTime, erRepeller, fYawOffset, fMagnitude);

	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersPredictConstantForceStop(EntityRef erTarget, U32 id, U32 uiStopTime)
{
	Entity *e = entFromEntityRefAnyPartition(erTarget);
	if(s_bClientPredictKnocks && e)
	{
		pmConstantForceStop(e, id, uiStopTime);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersPredictRoot(EntityRef erTarget, U32 uiTime)
{
	Entity *e = entFromEntityRefAnyPartition(erTarget);
	if(s_bClientPredictKnocks && e && e->pChar)
	{
		character_GenericRoot(e->pChar, true, uiTime);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersPredictRootStop(EntityRef erTarget, U32 uiTime)
{
	Entity *e = entFromEntityRefAnyPartition(erTarget);
	if(s_bClientPredictKnocks && e && e->pChar)
	{
		character_GenericRoot(e->pChar, false, uiTime);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersPredictHold(EntityRef erTarget, U32 uiTime)
{
	Entity *e = entFromEntityRefAnyPartition(erTarget);
	if(s_bClientPredictKnocks && e && e->pChar)
	{
		character_GenericHold(e->pChar, true, uiTime);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersPredictHoldStop(EntityRef erTarget, U32 uiTime)
{
	Entity *e = entFromEntityRefAnyPartition(erTarget);
	if(s_bClientPredictKnocks && e && e->pChar)
	{
		character_GenericHold(e->pChar, false, uiTime);
	}
}

// Targeting

SA_RET_OP_VALID Entity *FindEntByLocalName(SA_PARAM_NN_STR const char *pchName)
{
	Entity *pentTarget = NULL;
	EntityIterator *piter;

	piter = entGetIteratorAllTypesAllPartitions(0, gbNoGraphics ? ENTITYFLAG_IGNORE : ENTITYFLAG_IGNORE|ENTITYFLAG_DONOTDRAW);
	while((pentTarget = EntityIteratorGetNext(piter)))
	{
		const char *pchTargetName = entGetLocalName(pentTarget);
		if(pchTargetName && !stricmp(pchName,pchTargetName))
		{
			break;
		}
	}
	EntityIteratorRelease(piter);
	
	return pentTarget;
}

// Target <name>: Targets the Entity with the matching name
AUTO_COMMAND ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0);
void Target(ACMD_SENTENCE name)
{
	if(name && *name)
	{
		Entity *pentLocal = entActivePlayerPtr();
		Entity *pentTarget = FindEntByLocalName(name);

		if(pentTarget && pentTarget!=pentLocal)
		{
			entity_SetTarget(pentLocal,entGetRef(pentTarget));
		}
	}
}

// Assist <name>: Assists the Entity with the matching name.  If no name is given, assists your current target.
AUTO_COMMAND ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0);
void Assist(ACMD_SENTENCE name)
{
	Entity *pentLocal = entActivePlayerPtr();
	Entity *pentAssist = NULL;

	if(name && *name)
	{
		pentAssist = FindEntByLocalName(name);
	}
	else if(pentLocal && pentLocal->pChar && pentLocal->pChar->currentTargetRef)
	{
		pentAssist = entFromEntityRefAnyPartition(pentLocal->pChar->currentTargetRef);
	}

	if(pentAssist && pentAssist!=pentLocal)
	{
		entity_AssistTarget(pentLocal,entGetRef(pentAssist));
	}
}

// AssistTarget: Assists your current target
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(Assist);
void AssistTarget(void)
{
	Assist(NULL);
}

// Focus <name>: Sets the Entity with the matching name as the focus target. If no name is given, focuses your current target.
AUTO_COMMAND ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0);
void Focus(ACMD_SENTENCE name)
{
	Entity *pentLocal = entActivePlayerPtr();
	Entity *pentFocus = NULL;

	if(name && *name)
	{
		pentFocus = FindEntByLocalName(name);
	}
	else if(pentLocal && pentLocal->pChar && pentLocal->pChar->currentTargetRef)
	{
		pentFocus = entFromEntityRefAnyPartition(pentLocal->pChar->currentTargetRef);
	}

	if(pentFocus)
	{
		entity_SetFocusTarget(pentLocal,entGetRef(pentFocus));
	}
}

// Physics

// Create a test object at y feet above self
//AUTO_COMMAND;
//void CreateTestObject(char* modelName, F32 yOffset, S32 count){
//	Entity *e = entActivePlayerPtr();
//	if(e){
//		while(count-- > 0){
//			Vec3 pos = {0};
//			
//			entGetPos(e, pos);
//			pos[1] += yOffset;
//			
//			if(count){
//				pos[0] += qfrand() * 20.0f;
//				pos[2] += qfrand() * 20.0f;
//			}
//			
//			wcCreateTestObject(modelName, pos, qfrand() + 1.0f);
//		}
//	}
//}
//
//AUTO_COMMAND;
//void CreateTestObjectAt(char* modelName, const Vec3 pos){
//	wcCreateTestObject(modelName, pos, 1.0f);
//}
//
//AUTO_COMMAND;
//void CreateTestKinematic(char* modelName, F32 yOffset)
//{
//	Entity *e = entActivePlayerPtr();
//	if(e){
//		Vec3 pos = {0};
//
//		entGetPos(e, pos);
//		pos[1] += yOffset;
//
//		wcCreateTestKinematic(modelName, pos);
//	}
//}

// Destroy all test objects
//AUTO_COMMAND;
//void DestroyTestObjects(void)
//{
//	wcDestroyAllTestObjects();
//}

// Free camera mode
AUTO_CMD_INT(gGCLState.bUseFreeCamera, freecam) ACMD_CALLBACK(CommandFreeCameraCB) ACMD_ACCESSLEVEL(4);
void CommandFreeCameraCB(void)
{
	if (gGCLState.pPrimaryDevice->activecamera == &gGCLState.pPrimaryDevice->cutscenecamera || gGCLState.pPrimaryDevice->activecamera == &gGCLState.pPrimaryDevice->contactcamera)
		return;

	if (gGCLState.bUseFreeCamera)
		gclSetFreeCameraActive();
	else
		gclSetGameCameraActive();
}

AUTO_CMD_INT(gGCLState.bUseStationaryCamera,stationarycam) ACMD_CALLBACK(CommandStationaryCameraCB);

void CommandStationaryCameraCB(void)
{
	static GfxCameraControllerFunc sGameCamFunc = NULL;
	static GfxCameraControllerFunc sFreeCamFunc = NULL;

	if (gGCLState.bUseStationaryCamera)
	{
		sGameCamFunc = gGCLState.pPrimaryDevice->gamecamera.camera_func;
		sFreeCamFunc = gGCLState.pPrimaryDevice->freecamera.camera_func;
		gGCLState.pPrimaryDevice->gamecamera.camera_func = NULL;
		gGCLState.pPrimaryDevice->freecamera.camera_func = NULL;
	}
	else if (sGameCamFunc && sFreeCamFunc)
	{
		gGCLState.pPrimaryDevice->gamecamera.camera_func = sGameCamFunc;
		gGCLState.pPrimaryDevice->freecamera.camera_func = sFreeCamFunc;
	}
}

// Sets editor mode
#ifndef NO_EDITORS
static void emCommitEditModeChange(int enabled)
{
	Entity *pEnt = entActivePlayerPtr();
	if (!areEditorsAllowed())
	{
		return;
	}
	if (enabled)
		gclPatchStreamingFastMode();
	if ((pEnt && pEnt->pPlayer) || enabled != 1) // Need edit mode 2 if you don't have an ent
	{		
		ServerCmd_ForceNoTimeout(!!enabled);
		emSetEditorMode(!!enabled);
		gclSetEditorCameraActive(!!enabled);
		gfxDebugClearAccessLevelCmdWarnings(); // Reset the flag, so that editMode 0 doesn't trigger a warning
		if (enabled)
		{
			if (isProductionEditMode() && enabled == 1)
			{
				resClientRequestEditingLogin(pEnt->pPlayer->privateAccountName, true);
			}
			else if (isDevelopmentMode() && enabled == 1)
			{
				resClientRequestEditingLogin(gimmeDLLQueryUserName(), true);
				resSubscribeToAllInfoIndicesOnce();
			}

			gfxNoErrorOnNonPreloadedInternal(true); // Materials were preloaded when the map was loaded, but it's okay to add new ones

			if (!FolderCacheUpdatesLikelyWorking())
			{
				Errorf("Filesystem updates not working.  DO NOT EDIT FILES.  Checkouts will not work correctly, and you may lose data.  Please restart the client/server and if the problem persists, restart your computer.");
			}
		}
	}
	g_ui_State.bInEditor = !!enabled;
}

static void emCommitEditModeChangeCB(void *data)
{
	emCommitEditModeChange((intptr_t)data);
}

AUTO_COMMAND ACMD_NAME(editmode);
void CommandEditMode(int enabled)
{
	emQueueFunctionCallEx(emCommitEditModeChangeCB, (void*)(intptr_t)enabled, 0);
}

static void CommandEditorToggleUICB(void *data)
{
	emToggleEditorUI();
}

AUTO_COMMAND ACMD_NAME(editorToggleUI);
void CommandEditorToggleUI()
{
	emQueueFunctionCallEx(CommandEditorToggleUICB, NULL, 0);
}

#endif

// Private commands

// ran by the server to change the client's time
AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD;
void clientTimeSet(F32 newTime)
{
	wlTimeSet(newTime);
}

// ran by the server to change the client's timeScale
AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD;
void clientTimeScaleSet(F32 newTime)
{
	wlTimeSetScale(newTime);
}

// ran by the server to change the client's timeStepScale
AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void clientTimeStepScaleSet(F32 newTime, F32 newTimeGame)
{
	if(newTime>=0)
		wlTimeSetStepScaleDebug(newTime);

	if(newTimeGame>=0)
		wlTimeSetStepScaleGame(newTimeGame);
}


// Set the time
AUTO_COMMAND ACMD_NAME(time);
void clientTimeWrapper(F32 newTime)
{
	if (gclServerIsConnected()) {
		// connected to server - do synchronized time update
		ServerCmd_time(newTime);
	} else {
		// Local, just update locally
		clientTimeSet(newTime);
	}
}

// Set the time scale
AUTO_COMMAND ACMD_NAME(timescale);
void clientTimeScaleWrapper(F32 newTime)
{
	if (gclServerIsConnected()) {
		// connected to server - do synchronized time update
		ServerCmd_timescale(newTime);
	} else {
		// Local, just update locally
		clientTimeScaleSet(newTime);
	}
}

// Set the time step scale
AUTO_COMMAND ACMD_NAME(timeStepScale);
void clientTimeStepScaleDebugWrapper(F32 newTime)
{
	if (gclServerIsConnected()) {
		// connected to server - do synchronized time update
		ServerCmd_timeStepScaleDebug(newTime);
	} else {
		// Local, just update locally
		clientTimeStepScaleSet(newTime, -1); // -1 means leave game timestep alone
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(time);
void timeGetHandler(void)
{
	conPrintf("%f", wlTimeGet());
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(timescale);
void timeScaleGetHandler(void)
{
	conPrintf("%f", wlTimeGetScale());
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(timeStepScale);
void timeStepScaleDebugGetHandler(void)
{
	conPrintf("%f", wlTimeGetStepScaleDebug());
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void clientConPrint(const char *pMessage)
{
	conPrintf("%s", pMessage);
}

void clientConPrintfWrapper(GlobalType type, ContainerID id, const char *pMessage)
{
	conPrintf("%s", pMessage);
}

void clientBroadcastMessageWrapper(GlobalType type, ContainerID id, const char *pTitle, const char *pMessage)
{
	gclNotifyReceive(kNotifyType_ServerBroadcast, pMessage, NULL, NULL);
}

AUTO_RUN;
void setPrintCB(void)
{
	setObjPrintCB(clientConPrintfWrapper);
	setObjBroadcastMessageCB(clientBroadcastMessageWrapper);
}

// Global value for location
static Vec3 vLocation;

AUTO_COMMAND ACMD_NAME(locvec) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
Vec3* getLocationVec(void)
{
	Entity* e = entActivePlayerPtr();
	if(e) entGetPos(e, vLocation);
	else zeroVec3(vLocation);
	return &vLocation;
}

// Global value for location
AUTO_COMMAND ACMD_NAME(loc) ACMD_ACCESSLEVEL(0);
void getLocation(void)
{
	Entity* e = entActivePlayerPtr();

	if(e)
	{
		entGetPos(e, vLocation);
	}
	else
	{
		zeroVec3(vLocation);
	}

	conPrintf("%.0f %.0f %.0f", vecX(vLocation), vecY(vLocation), vecZ(vLocation));
}

AUTO_COMMAND;
void PlayTestFile(CmdContext *context, ACMD_SENTENCE fileName)
{
	if (isDevelopmentMode())
	{
		const char* dirList[] = {
			"N:/PlaytestCmds",
			"PlaytestCmds",
			NULL,
		};
		S32 i;
		char fileNameCopy[1024];
		
		strcpy(fileNameCopy, fileName);
		
		if(!strEndsWith(fileNameCopy, ".txt")){
			strcat(fileNameCopy, ".txt");
		}
		
		for(i = 0; dirList[i]; i++){
			char buffer[1024];

			sprintf(buffer, "%s/%s", dirList[i], fileNameCopy);
			if(fileExists(buffer)){
				conPrintf("Execing file: %s\n", buffer);
				sprintf(buffer, "exec %s/%s", dirList[i], fileNameCopy);
				GameClientParseActive(buffer, NULL, 0, -1, CMD_CONTEXT_HOWCALLED_PLAYTEST_FILE, NULL);
				break;
			}else{
				
			}
		}
		
		if(!dirList[i]){
			conPrintf("Can't find file: %s\n", fileName);
		}
	}
}

AUTO_COMMAND;
void PlayTest(CmdContext *context)
{
	PlayTestFile(context, GetProductName());
}

// Startup command line options

// Which username to use when logging in
AUTO_CMD_STRING(gGCLState.loginName,username) ACMD_CMDLINE;

// Which password to use when logging in
AUTO_CMD_STRING(gGCLState.loginPassword,password) ACMD_CMDLINE;

// Which character name to attempt to log in
AUTO_CMD_STRING(gGCLState.loginCharacterName,charactername) ACMD_CMDLINE;

// Which loginserver ip/name to connect to
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_NAME(server);
void AddLoginServerName(char *pName)
{
	eaPush(&gGCLState.ppLoginServerNames, strdup(pName));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_NAME(LoginServerShardIPAndPort);
void AddLoginServerShardIPAndPort(const char *shardName, const char *ipString, int port)
{
    LoginServerAddress *loginServerAddress = StructCreate(parse_LoginServerAddress);
    loginServerAddress->shardName = allocAddString(shardName);
    loginServerAddress->portNum = port;
    loginServerAddress->machineNameOrAddress = StructAllocString(ipString);

    eaPush(&gGCLState.eaLoginServers, loginServerAddress);
}

// Does not do pre-loading of level data behind a loading screen, nor waits for a button to be pressed to start a level
AUTO_CMD_INT(gGCLState.bSkipPreload, skipPreload) ACMD_CMDLINE;

// Does not load editor data from the server in development mode, until you hit ctrl-e
AUTO_CMD_INT(gGCLState.bPreLoadEditData, preLoadEditData) ACMD_CMDLINE;

// Skips the "press any key to continue" prompt after loading
AUTO_CMD_INT(gGCLState.bSkipPressAnyKey, skipPressAnyKey) ACMD_CMDLINE;

// Forces the "press any key to continue" prompt, which defaults on, but which gets turned off by
//quicklogin
AUTO_CMD_INT(gGCLState.bForcePromptToStartLevel, forcePrompt) ACMD_CMDLINE;

//disable the folder cache after loading
AUTO_CMD_INT(gGCLState.bDisableFolderCacheAfterLoad, disableFolderCacheAfterLoad) ACMD_CMDLINE;

// Disables in-game error messages and instead uses the Win32 error popups
//  for easier debugging.
AUTO_CMD_INT(gGCLState.bDoNotQueueErrors, doNotQueueErrors) ACMD_CMDLINE;

// Disables timeout when connecting to server
AUTO_CMD_INT(gGCLState.bNoTimeout, NoTimeout) ACMD_CMDLINE;

// Disables the XBOX title menu and XUID logins
AUTO_CMD_INT(gGCLState.bDisableXBoxTitleMenu, DisableXBoxTitleMenu) ACMD_CMDLINE;

// Enables the patch mode for XBOX client (default mode is game mode)
AUTO_CMD_INT(gGCLState.bXBoxPatchModeEnabled, EnableXBoxPatchMode) ACMD_CMDLINE;

// Allows the use of shared memory even in production mode (for test clients)
AUTO_CMD_INT(gGCLState.bAllowSharedMemory, allowSharedMemory) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

// -nothread (1) should disable the thread
// -nothread 0 should enable the thread (default behavior)
// -renderthread (1) should enable the thread even if numCPUs() == 1
// -renderthread 0 should disable the thread
// Command-line argument: Run non-threaded
AUTO_CMD_INT(gGCLState.bNoThread,nothread) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

// Command-line argument: Force the render thread on/off
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;
void renderThread(int on)
{
	if (on)
		gGCLState.bForceThread = 1;
	else {
		gGCLState.bNoThread = 1;
		gGCLState.bForceThread = 0;
	}
}

AUTO_COMMAND ACMD_NAME(notifyRanAccessLevelCmd) ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD;
void notifyRanAccessLevelCmd(const char *cmdstr, int accesseLevel)
{
	cmdParseNotifyRanAccessLevelCmd(cmdstr, accesseLevel);
}

int gclLoadCmdConfig(void)
{
	FILE	*file;
	char	buf[1000];

	//gameStateInit();

	file = fileOpen("config.txt","rt");
	if (!file)
		file = fileOpen("./config.txt","rt");
	if (!file)
		return 0;
	while(fgets(buf,sizeof(buf),file))
	{
		globCmdParse(buf);
	}
	fclose(file);
	return 1;
}

int gclLoadLoginConfig(void)
{
	const char *tmp = GamePrefGetString("Login.Username", NULL);
	if(tmp)
		strcpy_trunc(gGCLState.loginName, tmp);
	tmp = GamePrefGetString("Login.Character", NULL);
	if(tmp)
		strcpy_trunc(gGCLState.loginCharacterName, tmp);
	gGCLState.bSaveLoginUsername = GamePrefGetInt("Login.SaveUsername", 0);
	gGCLState.eLoginType = GamePrefGetInt("Login.LoginType", 0);
	gGCLState.eDefaultPlayerType = GamePrefGetInt("Login.DefaultPlayerType", 0);
	if(!(gGCLState.eDefaultPlayerType >= -1 && gGCLState.eDefaultPlayerType <= kPlayerType_LAST))
		gGCLState.eDefaultPlayerType = 0;
	return 1;
}

int gclSaveLoginConfig(void)
{
	if(gGCLState.bSaveLoginUsername)
	{
		if (gGCLState.pwAccountName[0])
			GamePrefStoreString("Login.Username", gGCLState.pwAccountName);
		else
			GamePrefStoreString("Login.Username", gGCLState.loginName);
		GamePrefStoreInt("Login.LoginType", gGCLState.eLoginType);
	}
	GamePrefStoreString("Login.Character", gGCLState.loginCharacterName);
	GamePrefStoreInt("Login.SaveUsername", gGCLState.bSaveLoginUsername?1:0);
	if(gGCLState.eDefaultPlayerType >= -1 && gGCLState.eDefaultPlayerType <= kPlayerType_LAST)
		GamePrefStoreInt("Login.DefaultPlayerType", gGCLState.eDefaultPlayerType);
	return 1;
}

//this function is called remotely from the server to add server-only command names to auto-completion
AUTO_COMMAND ACMD_HIDE ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD;
void AddCommandNameToClientForAutoCompletion(char *pCmdName, int iAccessLevel)
{
	//first, check if we already have it
	if (cmdListFind(&gGlobalCmdList,pCmdName))	
	{
		return;
	}

	//otherwise, add it to the bucket
	NameList_AccessLevelBucket_AddName(pAddedCmdNames, pCmdName, iAccessLevel);
}


#define NUM_CACHED_COMMANDS_FOR_ARG_AUTO_COMPLETION 32

typedef struct
{
	char *pCommandName;
	NameList *pNameLists[CMDMAXARGS];
	enumNameListType eNameListTypes[CMDMAXARGS];
	U32 uLastRequestTime;
	bool bUpdateClientDataPerRequest;
} CachedArgNamesForAutoCompletion;

CachedArgNamesForAutoCompletion gArgNameCacheForAutoCompletion[NUM_CACHED_COMMANDS_FOR_ARG_AUTO_COMPLETION] = {0};

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void ResetClientCommandArgsForAutoCompletion(void)
{
	ARRAY_FOREACH_BEGIN(gArgNameCacheForAutoCompletion, i);
	{
		SAFE_FREE(gArgNameCacheForAutoCompletion[i].pCommandName);
		ARRAY_FOREACH_BEGIN(gArgNameCacheForAutoCompletion[i].pNameLists, j);
		{
			if (gArgNameCacheForAutoCompletion[i].pNameLists[j])
			{
				FreeNameList(gArgNameCacheForAutoCompletion[i].pNameLists[j]);
				gArgNameCacheForAutoCompletion[i].eNameListTypes[j] = NAMELISTTYPE_NONE;
			}
		}
		ARRAY_FOREACH_END;
	}
	ARRAY_FOREACH_END;

	ZeroArray(gArgNameCacheForAutoCompletion);

	//somewhat arbitrarily, this is a good place to do all of the checking of the commands in the 
	//client command-auto-completion list
	if (isDevelopmentMode())
	{
		ONCE(gclChatAutoComplete_VerifyCommands());
	}
}

//command called by the server to send autocompletion names for a given command to the client
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void EnableDevModeKeybinds(bool enable)
{
	if (enable)
		keybind_PushProfileName("DevelopmentKeyBinds");
	else
		keybind_PopProfileName("DevelopmentKeyBinds");


}

//command called by the server to send autocompletion names for a given command to the client
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void AddCommandArgsToClientForAutoCompletion(NamesForAutoCompletion *pNames)
{
	//check if this command is already in the cache. If it is, promote it to newest
	int i, j, k;
	bool bCacheNotFull = false;

	for (i=0; i < NUM_CACHED_COMMANDS_FOR_ARG_AUTO_COMPLETION; i++)
	{
		if (!gArgNameCacheForAutoCompletion[i].pCommandName)
		{
			bCacheNotFull = true;
			break;
		}

		if (strcmp(pNames->pCmdName, gArgNameCacheForAutoCompletion[i].pCommandName) == 0)
		{
			CachedArgNamesForAutoCompletion tempCache;

			if (pNames->bUpdateClientDataPerRequest)
			{
				//the data can be updated more than once, copy over the existing data
				for (j=0; j < CMDMAXARGS; j++)
				{
					if (gArgNameCacheForAutoCompletion[i].pNameLists[j])
					{
						FreeNameList(gArgNameCacheForAutoCompletion[i].pNameLists[j]);
						gArgNameCacheForAutoCompletion[i].pNameLists[j] = NULL;
						gArgNameCacheForAutoCompletion[i].eNameListTypes[j] = NAMELISTTYPE_NONE;
					}
				}
				for (j=0; j < eaSize(&pNames->ppNamesForArgs); j++)
				{
					int iIndex = pNames->ppNamesForArgs[j]->iArgIndex;
					assert(iIndex>= 0 && iIndex < CMDMAXARGS && gArgNameCacheForAutoCompletion[i].pNameLists[iIndex] == NULL);
					gArgNameCacheForAutoCompletion[i].pNameLists[iIndex] = CreateNameList_Bucket();
					gArgNameCacheForAutoCompletion[i].eNameListTypes[iIndex] = pNames->ppNamesForArgs[j]->eNameListType;
					for (k=0; k < eaSize(&pNames->ppNamesForArgs[j]->ppNames); k++)
					{
						NameList_Bucket_AddName(gArgNameCacheForAutoCompletion[i].pNameLists[iIndex], pNames->ppNamesForArgs[j]->ppNames[k]);
					}
				}
				return;
			}
			if (i == 0)
			{
				//command is already in the cache, already newest
				return;
			}

			memcpy(&tempCache, &gArgNameCacheForAutoCompletion[i], sizeof(CachedArgNamesForAutoCompletion));
			memmove(&gArgNameCacheForAutoCompletion[1], &gArgNameCacheForAutoCompletion[0], sizeof(CachedArgNamesForAutoCompletion) * i);
			memcpy(&gArgNameCacheForAutoCompletion[0], &tempCache, sizeof(CachedArgNamesForAutoCompletion));

			return;
		}
	}

	//the command is not in our cache. We will insert it. First, dump the oldest thing in the cache (if full), and move
	//everything up one
	if (!bCacheNotFull)
	{
		free(gArgNameCacheForAutoCompletion[NUM_CACHED_COMMANDS_FOR_ARG_AUTO_COMPLETION-1].pCommandName);
		for (i=0; i < CMDMAXARGS; i++)
		{
			if (gArgNameCacheForAutoCompletion[NUM_CACHED_COMMANDS_FOR_ARG_AUTO_COMPLETION-1].pNameLists[i])
			{
				FreeNameList(gArgNameCacheForAutoCompletion[NUM_CACHED_COMMANDS_FOR_ARG_AUTO_COMPLETION-1].pNameLists[i]);
				gArgNameCacheForAutoCompletion[NUM_CACHED_COMMANDS_FOR_ARG_AUTO_COMPLETION-1].eNameListTypes[i] = NAMELISTTYPE_NONE;
			}
		}
	}

	CopyStructsFromOffset(&gArgNameCacheForAutoCompletion[1], -1, ARRAY_SIZE(gArgNameCacheForAutoCompletion) - 1);
	ZeroStruct(&gArgNameCacheForAutoCompletion[0]);

	gArgNameCacheForAutoCompletion[0].pCommandName = strdup(pNames->pCmdName);

	for (i=0; i < eaSize(&pNames->ppNamesForArgs); i++)
	{
		int iIndex = pNames->ppNamesForArgs[i]->iArgIndex;
		assert( iIndex>= 0 && iIndex < CMDMAXARGS && gArgNameCacheForAutoCompletion[0].pNameLists[iIndex] == NULL);

		gArgNameCacheForAutoCompletion[0].pNameLists[iIndex] = CreateNameList_Bucket();
		gArgNameCacheForAutoCompletion[0].eNameListTypes[iIndex] = pNames->ppNamesForArgs[i]->eNameListType;

		for (j=0; j < eaSize(&pNames->ppNamesForArgs[i]->ppNames); j++)
		{
			NameList_Bucket_AddName(gArgNameCacheForAutoCompletion[0].pNameLists[iIndex], pNames->ppNamesForArgs[i]->ppNames[j]);
		}
	}
	gArgNameCacheForAutoCompletion[0].bUpdateClientDataPerRequest = pNames->bUpdateClientDataPerRequest;
}

enumNameListType gclCmdParseGetExtraNameListTypeForArgAutoCompletion(char *pCommandName, int iArgNum) {
	int i;
	char *pCommandNameToUse = stripUnderscores(pCommandName);

	if (iArgNum >= CMDMAXARGS) {
		return NAMELISTTYPE_NONE;
	}

	for (i=0; i < NUM_CACHED_COMMANDS_FOR_ARG_AUTO_COMPLETION; i++)
	{
		if (!gArgNameCacheForAutoCompletion[i].pCommandName)
		{
			break;
		}

		if (strcmp(pCommandNameToUse, gArgNameCacheForAutoCompletion[i].pCommandName) == 0)
		{
			if (gArgNameCacheForAutoCompletion[i].bUpdateClientDataPerRequest)
			{
				U32 uCurrentTime = timeSecondsSince2000();
				if (gArgNameCacheForAutoCompletion[i].uLastRequestTime != uCurrentTime)
				{
					ServerCmd_RequestAutoCompletionArgNamesFromServer(pCommandNameToUse);
					gArgNameCacheForAutoCompletion[i].uLastRequestTime = uCurrentTime;
				}
			}
			return gArgNameCacheForAutoCompletion[i].eNameListTypes[iArgNum];
		}
	}

	//server is not in our cache... request it
	ServerCmd_RequestAutoCompletionArgNamesFromServer(pCommandNameToUse);
	return NAMELISTTYPE_NONE;
}

NameList *gclCmdParseGetExtraNameListForArgAutoCompletion(char *pCommandName, int iArgNum)
{
	int i;
	char *pCommandNameToUse = stripUnderscores(pCommandName);

	if (iArgNum >= CMDMAXARGS) {
		return NULL;
	}

	for (i=0; i < NUM_CACHED_COMMANDS_FOR_ARG_AUTO_COMPLETION; i++)
	{
		if (!gArgNameCacheForAutoCompletion[i].pCommandName)
		{
			break;
		}

		if (strcmp(pCommandNameToUse, gArgNameCacheForAutoCompletion[i].pCommandName) == 0)
		{
			if (gArgNameCacheForAutoCompletion[i].bUpdateClientDataPerRequest)
			{
				U32 uCurrentTime = timeSecondsSince2000();
				if (gArgNameCacheForAutoCompletion[i].uLastRequestTime != uCurrentTime)
				{
					ServerCmd_RequestAutoCompletionArgNamesFromServer(pCommandNameToUse);
					gArgNameCacheForAutoCompletion[i].uLastRequestTime = uCurrentTime;
				}
			}
			return gArgNameCacheForAutoCompletion[i].pNameLists[iArgNum];
		}
	}

	//server is not in our cache... request it
	ServerCmd_RequestAutoCompletionArgNamesFromServer(pCommandNameToUse);
	return NULL;
}

void gclSendPublicOrPrivateCommand(const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	if (bPrivate)
	{
		gclSendPrivateCommand(iFlags, pCmd, eHow, pStructs);
	}
	else
	{
		gclSendPublicCommand(iFlags, pCmd, eHow, pStructs);
	}
}

void gclSendGenericCommand(GlobalType eServerType, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	switch (eServerType)
	{
	case GLOBALTYPE_CHATRELAY:
		gclSendChatRelayCommand(iFlags, pCmd, bPrivate, eHow, pStructs);
	}
}

AUTO_RUN;
void initGCLCommandParse(void)
{
	gpGetExtraNameListForArgAutoCompletionCB = gclCmdParseGetExtraNameListForArgAutoCompletion;
	gpGetExtraNameListTypeForArgAutoCompletionCB = gclCmdParseGetExtraNameListTypeForArgAutoCompletion;
	cmdSetClientToServerCB(gclSendPublicOrPrivateCommand);
	cmdSetGenericToServerCB(gclSendGenericCommand);
}

AUTO_COMMAND ACMD_NAME(mmDebug);
void mmCmdSetDebugging(Entity* e, const char* target, S32 enabled)
{
	Entity* eTarget = entGetClientTarget(e, target, NULL);

	mmSetDebugging(SAFE_MEMBER(eTarget, mm.movement), enabled);
}

AUTO_COMMAND ACMD_NAME(mmLogPrint);
void mmCmdLogPrint(Entity* e, const char* target, ACMD_SENTENCE text)
{
	Entity* eTarget = entGetClientTarget(e, target, NULL);

	mmLog(SAFE_MEMBER(eTarget, mm.movement), NULL, "%s", text);
}

AUTO_COMMAND ACMD_NAME(mmLogWriteFiles);
void mmCmdLogWriteFiles(Entity* e, const char* target, S32 enabled)
{
	Entity* eTarget = entGetClientTarget(e, target, NULL);

	mmSetWriteLogFiles(SAFE_MEMBER(eTarget, mm.movement), enabled);
}

AUTO_COMMAND ACMD_NAME("mmAddRequester");
void mmCmdAddRequester(Entity* e, const char* target, const char* name)
{
	Entity* eTarget = entGetClientTarget(e, target, NULL);

	mmRequesterCreateBasicByName(SAFE_MEMBER(eTarget, mm.movement), NULL, name);
}

AUTO_COMMAND ACMD_NAME(mmDestroyRequester);
void mmCmdDestroyRequester(Entity* e, const char* target, const char* name)
{
	Entity*				eTarget = entGetClientTarget(e, target, NULL);
	MovementRequester*	mr;
	
	if(mmRequesterGetByNameFG(SAFE_MEMBER(eTarget, mm.movement), name, &mr)){
		mrDestroy(&mr);
	}
}

AUTO_COMMAND ACMD_NAME("mmTestPredictedKnockback");
void mmCmdTestPredictedKnockback(	Entity* e,
									const char* target,
									const Vec3 vel,
									S32 instantFacePlant,
									S32 proneAtEnd,
									F32 timer,
									S32 ignoreTravelTime)
{
	Entity*				eTarget = entGetClientTarget(e, target, NULL);
	MovementRequester*	mr;
	U32					startProcessCount = mmGetProcessCountAfterMillisecondsFG(0);
	
	if(eTarget){
		if(mmRequesterCreateBasicByName(eTarget->mm.movement, &mr, "ProjectileMovement")){
			mrProjectileStartWithVelocity(mr, e, vel, startProcessCount, instantFacePlant, proneAtEnd, timer, ignoreTravelTime);
		}
		
		globCmdParsef(	"ec %d mmTestPredictedKnockback %f %f %f %d %d %d %f %d",
						entGetRef(eTarget),
						vecParamsXYZ(vel),
						startProcessCount,
						instantFacePlant,
						proneAtEnd,
						timer,
						ignoreTravelTime);
	}
}

AUTO_COMMAND ACMD_NAME("mmTestMispredictedKnockback");
void mmCmdTestMispredictedKnockback(Entity* e,
									const char* target,
									const Vec3 vel,
									S32 instantFacePlant,
									S32 proneAtEnd,
									F32 timer,
									S32 ignoreTravelTime)
{
	Entity*				eTarget = entGetClientTarget(e, target, NULL);
	MovementRequester*	mr;
	U32					startProcessCount = mmGetProcessCountAfterMillisecondsFG(0);
	
	if(eTarget){
		if(mmRequesterCreateBasicByName(eTarget->mm.movement, &mr, "ProjectileMovement")){
			mrProjectileStartWithVelocity(mr, e, vel, startProcessCount, instantFacePlant, proneAtEnd, timer, ignoreTravelTime);
		}
	}
}

AUTO_COMMAND ACMD_NAME("mmTestPredictedKnockbackWithTarget");
void mmCmdTestPredictedKnockbackWithTarget(	Entity* e,
											const char* target,
											const Vec3 targetPos,
											S32 instantFacePlant,
											S32 proneAtEnd,
											F32 timer,
											S32 ignoreTravelTime)
{
	Entity*				eTarget = entGetClientTarget(e, target, NULL);
	MovementRequester*	mr;
	U32					startProcessCount = mmGetProcessCountAfterMillisecondsFG(0);
	
	if(eTarget){
		if(mmRequesterCreateBasicByName(eTarget->mm.movement, &mr, "ProjectileMovement")){
			mrProjectileStartWithTarget(mr,
										e,
										targetPos,
										startProcessCount,
										false,
										mrFlightGetEnabled(eTarget->mm.mrFlight),
										instantFacePlant,
										proneAtEnd,
										timer,
										ignoreTravelTime);
		}
		
		globCmdParsef(	"ec %d mmTestPredictedKnockbackWithTarget %f %f %f %d %d %d %f %d",
						entGetRef(eTarget),
						vecParamsXYZ(targetPos),
						startProcessCount,
						instantFacePlant,
						proneAtEnd,
						timer,
						ignoreTravelTime);
	}
}

#include "EntityMovementTest.h"

AUTO_COMMAND ACMD_NAME(mmTestThing);
void mmCmdTestThing(Entity* e, const char* target)
{
	Entity*				eTarget = entGetClientTarget(e, target, NULL);
	MovementRequester*	mr;
	
	if(	eTarget &&
		mmRequesterGetByNameFG(eTarget->mm.movement, "TestMovement", &mr))
	{
		U32 timeToStart = mmGetProcessCountAfterMillisecondsFG(0);
		
		ANALYSIS_ASSUME(eTarget);
		mmTestSetDoTest(mr, timeToStart);
		
		globCmdParsef("ec %d mmTestThing %d", entGetRef(eTarget), timeToStart);
	}
}

AUTO_COMMAND ACMD_NAME(sleepClient);
void gclCmdSleepClient(S32 milliseconds)
{
	if(milliseconds >= 0){
		printf("sleepClient: Sleeping for %dms...", milliseconds);
		Sleep(milliseconds);
		printf(" done!\n");
	}
}

char *gclGetDebugPosStringInternal(Entity *e)
{
	GfxCameraController *camera = gfxGetActiveCameraController();
	const char *mapName;
	const SavedMapDescription* pLastMap;
	const char *mapVariables;
	static char *buf;
	Vec3 tempPYR;
	
	PERFINFO_AUTO_START_FUNC();

	assert(camera);

	mapName = zmapInfoGetPublicName(NULL);
	pLastMap = entity_GetLastMap(e);
	if (pLastMap) {
		mapVariables = pLastMap->mapVariables;
	} else {
		mapVariables = NULL;
	}
	
	if (e)
	{
		Vec3 bePos;

		if( nullStr( mapVariables )) {
			estrPrintf( &buf, "SetDebugPos" );
		} else {
			estrPrintf( &buf, "SetDebugPosEx" );
		}

		estrConcatf(&buf, " \"%s\"", mapName);

		entGetPos(e, bePos);
		estrConcatf(&buf, " %f %f %f", vecParamsXYZ(bePos));

		copyVec3(camera->campyr, tempPYR);
		tempPYR[1] = addAngle(tempPYR[1], PI);
		estrConcatf(&buf, " %f %f %f", tempPYR[0], tempPYR[1], tempPYR[2]);
	}
	else
	{
		if( nullStr( mapVariables )) {
			estrPrintf(&buf, "SetDebugCamPos");
		} else {
			estrPrintf(&buf, "SetDebugCamPosEx");
		}
		
		estrConcatf(&buf, " \"%s\"", mapName);

		estrConcatf(&buf, " %f %f %f", camera->last_camera_matrix[3][0], camera->last_camera_matrix[3][1], camera->last_camera_matrix[3][2]);

		scaleVec3(camera->campyr, 180/PI, tempPYR);
//		tempPYR[1] = addAngle(tempPYR[1], PI);
		estrConcatf(&buf, " %f %f %f", tempPYR[0], tempPYR[1], tempPYR[2]);
	}

	// Append variables
	if( !nullStr( mapVariables )) {
		estrConcatStatic( &buf, " \"" );
		estrAppendEscaped( &buf, mapVariables );
		estrConcatStatic( &buf, "\"" );
	}
	
	PERFINFO_AUTO_STOP();
	return buf;
}

char *gclGetDebugPosString(void)
{
	return gclGetDebugPosStringInternal(entActivePlayerPtr());
}

AUTO_COMMAND ACMD_ACCESSLEVEL(5);
void CopyDebugPos(Entity *pEnt)
{
	winCopyToClipboard(gclGetDebugPosStringInternal(pEnt));
}

AUTO_COMMAND ACMD_NAME(SetDebugPosEx) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(5);
void gclSetRequestedDebugPosEx(Entity *pEnt, char *mapName, Vec3 vPos, Vec3 vRot, char* rawMapVariables)
{
	char* mapVariables = NULL;
	vRot[0] = vRot[2] = 0.0; // strip out pitch and roll for now
	
	estrAppendUnescaped( &mapVariables, rawMapVariables);
	
	if (pEnt)
	{
		ServerCmd_MapMoveDebug(mapName, vPos, vRot, mapVariables);
	}
	else
	{
		gclInitQuickLoginWithPos(1, "", mapName, vPos, vRot, mapVariables);
	}
	
	estrDestroy( &mapVariables );
}

AUTO_COMMAND ACMD_NAME(SetDebugPos) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(5);
void gclSetRequestedDebugPos(Entity *pEnt, char *mapName, Vec3 vPos, Vec3 vRot)
{
	gclSetRequestedDebugPosEx(pEnt, mapName, vPos, vRot, "");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(5);
void CopyDebugCamPos(void)
{
	winCopyToClipboard(gclGetDebugPosStringInternal(NULL));
}

AUTO_COMMAND ACMD_NAME(SetDebugCamPosEx);
void gclSetRequestedDebugCamPosEx(Entity *pEnt, char *mapName, Vec3 vPos, Vec3 vRot, ACMD_SENTENCE mapVariables)
{
	if (pEnt)
	{
		Quat tempRot;
		PYRToQuat(vRot,tempRot);
//		ServerCmd_MapMoveStatic(mapName);
//		Ignoring map variables -- since you can't change maps		
	}
	else
	{
//		gclInitQuickLogin(1, "", mapName);
	}

	globCmdParse("freecam 1");
	globCmdParsef("setcampos %f %f %f", vPos[0], vPos[1], vPos[2]);
	globCmdParsef("setcampyr %f %f %f", vRot[0], vRot[1], vRot[2]);
}

AUTO_COMMAND ACMD_NAME(SetDebugCamPos);
void gclSetRequestedDebugCamPos(Entity *pEnt, char *mapName, Vec3 vPos, Vec3 vRot)
{
	gclSetRequestedDebugCamPosEx(pEnt, mapName, vPos, vRot, "");
}

AUTO_COMMAND;
void PrintGlobalInfos(Entity* e, char *pDictionaryName)
{
	int i;
	ResourceDictionaryInfo *dictInfo = resDictGetInfo(pDictionaryName);
	if (dictInfo && eaSize(&dictInfo->ppInfos))
	{
		conPrintf("Objects for %s:\n", pDictionaryName);
		for (i = 0; i < eaSize(&dictInfo->ppInfos); i++)
		{
			ResourceInfo *objInfo = dictInfo->ppInfos[i];
			conPrintf("%s %s %s %s %s\n", objInfo->resourceName, objInfo->resourceDisplayName, objInfo->resourceLocation, objInfo->resourceScope, objInfo->resourceScope);
		}
		conPrintf("\n");
	}	
	else
	{
		conPrintf("No Objects\n");
	}	
}

AUTO_COMMAND ACMD_NAME("wcoInvalidate");
void cmdInvalidateWCO(U64 wcoU64){
	WorldCollObject* wco = (void*)(uintptr_t)wcoU64;
	
	if(wcIsValidObject(wco)){
		wcoInvalidate(wco);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(4);
void GodModeClient(int iSet)
{
	if (iSet)
	{
		keybind_PushProfileName("GodMode");
	}
	else
	{
		keybind_PopProfileName("GodMode");	
	}
}

// used by special permission from Jimb
const char *fileDataDir(void);

// Generate client and server command message lists.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void CmdParseGenerateMessages(void)
{
#ifndef MESSAGE_IS_TINY
	if (gclServerIsConnected())
	{
		CommandNameList List = {NULL};
		char achPath[MAX_PATH];
		sprintf(achPath, "%s/messages/commands", fileDataDir());
		CmdParseDumpMessages("Client", NULL, &List, achPath);
		ServerCmd_CmdParseDumpServerMessages("Server", &List, achPath);
		eaDestroy(&List.eaNames);
	} 
	else
	{
		Errorf("You need to be connected to a server to generate command messages.");
	}
#endif // MESSAGE_IS_TINY
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_PRIVATE ACMD_CLIENTCMD;
void CmdParseDumpServerMessagesSucceeded(void)
{
	ControllerScript_Succeeded();
}


//used for testing what happens when a command is spammed repeatedly
AUTO_COMMAND ACMD_CATEGORY(Debug);
void SpamCommand(CmdContext *pContext, int iTimes, ACMD_SENTENCE pCommand)
{
	int i;

	for (i= 0 ; i < iTimes; i++)
	{
		globCmdParseSpecifyHow(pCommand, pContext->eHowCalled);
	}
}

static char *g_proxy_host = NULL;
static U32 g_proxy_port = 0;
AUTO_COMMAND ACMD_CMDLINE ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;
void SetProxy(const char *host, U32 port)
{
	// This must be done early
	g_force_sockbsd = 1;
	
	g_proxy_host = strdup(host);
	g_proxy_port = port;

	systemSpecsTriviaPrintf("Proxy", "%s:%d", host, port);
}

AUTO_RUN;
void SetProxyAutoRun(void)
{
	if(g_proxy_host)
	{	
		commSetProxy(commDefault(), g_proxy_host, g_proxy_port);
	}
}

char *gclGetProxyHost(void)
{
	return g_proxy_host;
}

void gclDisableProxy(void)
{
	SAFE_FREE(g_proxy_host);
	g_proxy_port = 0;
	systemSpecsTriviaPrintf("Proxy", "");
	commSetProxy(commDefault(), NULL, 0);
}

AUTO_COMMAND ACMD_NAME(proxy) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cmdPrintProxy(void)
{
	if(g_proxy_host)
		conPrintf("%s:%d\n", g_proxy_host, g_proxy_port);
	else
		conPrintf("No proxy enabled\n");
}

AUTO_COMMAND ACMD_NAME(DebugSendEvent) ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_HIDE;
void cmdDebugSendEvent(GameEvent *pGameEvent, GameEventParticipants *pGameEventParticipants)
{
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);

	gameevent_WriteEvent(pGameEvent, &estrBuffer);
	printf("%s", estrBuffer);

	estrDestroy(&estrBuffer);

	gameeventdebug_SendEvent(pGameEvent, pGameEventParticipants);
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void cmdExportMessageReplacements(ACMD_SENTENCE pchReplacements)
{
	FILE *pFile = fopen("C:\\MessageReplacements.txt", "wb");
	pchReplacements = NULL_TO_EMPTY(pchReplacements);
	if (pFile)
	{
		fwrite(pchReplacements, 1, strlen(pchReplacements), pFile);
		fclose(pFile);
		conPrintf("Wrote C:\\MessageReplacements.txt\n");
	}
}


bool OVERRIDE_LATELINK_ExportCSVCommandFile_Special(void)
{
	MakeCommandsForCSV();
	ServerCmd_SendMeCommandsForCSV();

	return true;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void HereAreServerCommandsForCSV(CommandForCSVList *pList)
{
	NameList *pAutoCompleteCmds;
	const char *pCmdName;

	FOR_EACH_IN_EARRAY(pList->ppCommands, CommandForCSV, pCommand)
	{
		CommandForCSV *pExistingCommand;

		if (stashFindPointer(sCommandsForCSVExport, pCommand->pName, &pExistingCommand))
		{
			pExistingCommand->bExistsOnServer = true;
		}
		else
		{
			pCommand = StructClone(parse_CommandForCSV, pCommand);
			stashAddPointer(sCommandsForCSVExport, pCommand->pName, pCommand, true);
		}
	}
	FOR_EACH_END;

	pAutoCompleteCmds = gclChatGetAllNamesNameList();
	if (pAutoCompleteCmds)
	{
		pAutoCompleteCmds->pResetCB(pAutoCompleteCmds);

		while ((pCmdName = pAutoCompleteCmds->pGetNextCB(pAutoCompleteCmds, false)))
		{
			CommandForCSV *pExistingCommand;

			if (stashFindPointer(sCommandsForCSVExport, pCmdName, &pExistingCommand))
			{
				pExistingCommand->bChatAutoComplete = true;
			}
		}
	}

	ExportCommandsForCSV_Internal();
}

void OVERRIDE_LATELINK_CmdPrintInfoInternal(void *pCommandName_in, void *pContext_in)
{
	char *pCommandName = (char*)pCommandName_in;
	CmdContext *pContext = (CmdContext*)pContext_in;
		
	char *pInfo = cmdGetInfo_Internal(pCommandName);
	if (pInfo)
	{
		printf("%s is on CLIENT\n", pCommandName);
		printf("%s\n", pInfo);

		if (pContext->eHowCalled == CMD_CONTEXT_HOWCALLED_TILDE_WINDOW)
		{
			conPrintf("%s is on CLIENT\n", pCommandName);
			conPrintf("%s\n", pInfo);
		}
	}
	else
	{
		ServerCmd_GetCmdPrintInfoForClient(pCommandName, pContext->eHowCalled == CMD_CONTEXT_HOWCALLED_TILDE_WINDOW);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void HereIsInfoAboutCommand(char *pInfo, bool bDoPrintsInGfxConsole)
{
	printf("%s\n", pInfo);
	if (bDoPrintsInGfxConsole)
	{
		conPrintf("%s\n", pInfo);
	}
}