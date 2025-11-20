/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameServerLib.h"
#include "gslSendToClient.h"
#include "gslCommandParse.h"
#include "file.h"
#include "strings_opt.h"
#include "gslTransactions.h"
#include "EntityMovementManager.h"
#include "EntityMovementTargetedRotation.h"
#include "Powers.h"
#include "PowerActivation.h"
#include "PowersMovement.h"
#include "wlTime.h"
#include "gslExtern.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "Entity.h"
#include "EntityBuild.h"
#include "memcheck.h"
#include "gslEntity.h"
#include "gslInteractionManager.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CombatDebug.h"
#include "ControlScheme.h"
#include "gslContact.h"
#include "gslControlScheme.h"
#include "gslCritter.h"
#include "gslEntity.h"
#include "gslPowerTransactions.h"
#include "gslUserExperience.h"
#include "CostumeCommon.h"
#include "Character.h"
#include "ResourceInfo.h"
#include "cutscene.h"
#include "EntDebugMenu.h"
#include "Team.h"
#include "Guild.h"
#include "logging.h"
#include "ResourceManager.h"
#include "CommandTranslation.h"
#include "sysutil.h"
#include "chatCommon.h"
#include "AutoTransDefs.h"
#include "Player.h"
#include "CombatConfig.h"
#include "gslMechanics.h"
#include "gslPartition.h"
#include "gslInteractable.h"
#include "mission_common.h"
#include "interaction_common.h"
#include "StringFormat.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "autogen/cmdparse_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "Autogen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/ServerLib_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#include "Autogen/Entity_h_ast.h"

int GetAccessLevelForGSLCmdParse(Entity *pEnt)
{
	if (isProductionEditMode() && !gbUseRealAccessLevelInUGC)
		return ACCESS_UGC;

	return entGetAccessLevel(pEnt);
}




static void ExpandCommandVars(const char *in_str,char **out_estr,Entity *client)
{
	const char	*s;
	char *var;
	int	cmdlen;

	if (!client)
	{
		estrAppend2(out_estr,in_str);
		return;
	}
	for(s=in_str;*s;s++)
	{
		if (*s != '$')
		{
			estrConcatChar(out_estr,*s);
			continue;
		}
		var = gslExternExpandCommandVar(s+1,&cmdlen,client);
		if (var)
		{
			estrAppend2(out_estr,var);
			s += cmdlen;
		}
		else
		{
			estrConcatChar(out_estr,*s);
		}
	}
}

int GameServerParsePublic(const char *cmd_str_orig, CmdContextFlag iFlags, Entity *clientEntity, char **ppRetString, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{

	CmdServerContext svr_context = {0};	
	char *expandedString = NULL;
	char *lineBuffer, *cmd_str, *cmd_str_start = NULL;
	char *pInternalRetString = NULL;
	char *pOrigCmdString = NULL;
	estrStackCreate(&pOrigCmdString);
	estrStackCreate(&expandedString);
	estrStackCreate(&cmd_str_start);
	estrStackCreate(&pInternalRetString);
	estrCopy2(&cmd_str_start,cmd_str_orig);
	cmd_str = cmd_str_start;


	while(cmd_str)
	{
		CmdContext		cmd_context = {0};
		int result;

		cmd_context.language = entGetLanguage(clientEntity);

		cmd_context.eHowCalled = eHow;
		cmd_context.pStructList = pStructs;

		if (ppRetString)
		{
			cmd_context.output_msg = ppRetString;
		}
		else
		{
			cmd_context.output_msg = &pInternalRetString;
		}

		lineBuffer = cmdReadNextLine(&cmd_str);

		estrClear(&expandedString);

		cmd_context.flags = iFlags;

		if (clientEntity && entGetPlayer(clientEntity))
		{
			ExpandCommandVars(lineBuffer,&expandedString,clientEntity);
			cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : GetAccessLevelForGSLCmdParse(clientEntity);
		}
		else
		{
			estrAppend2(&expandedString,lineBuffer);
			cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : ACCESS_INTERNAL;
		}


		svr_context.sourceStr = expandedString;
		svr_context.clientEntity = clientEntity;

		cmd_context.data = &svr_context;

		estrCopy2(&pOrigCmdString, expandedString);

		result = cmdParseAndExecute(&gGlobalCmdList,expandedString,&cmd_context);

	
		if (result && cmd_context.found_cmd->access_level > 0 && (cmd_context.flags & CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL) && clientEntity && entGetPlayer(clientEntity)
			&& cmd_context.found_cmd->access_level > cmdGetMinAccessLevelWhichForcesLogging())
		{
			if (iOverrideAccessLevel != -1)
			{
				entLog(LOG_COMMANDS, clientEntity, "AccessLevelGT0Command", "Player %s executed access level %d command string (Using override access level %d): %s",
					entGetLocalName(clientEntity), cmd_context.found_cmd->access_level, iOverrideAccessLevel, pOrigCmdString);
			}
			else
			{			
				entLog(LOG_COMMANDS, clientEntity, "AccessLevelGT0Command", "Player %s executed access level %d command string: %s",
					entGetLocalName(clientEntity), cmd_context.found_cmd->access_level, pOrigCmdString);
			}
		}
	

		if (clientEntity && eHow == CMD_CONTEXT_HOWCALLED_CHATWINDOW && cmd_context.cmd_unknown)
		{
			gslSendUnknownChatWindowCmd(clientEntity, cmd_str_orig);
		}
		else if (estrLength(cmd_context.output_msg))
		{
			if (clientEntity)
				gslSendPrintf(clientEntity,"%s\n",*cmd_context.output_msg);
			else 
				cmdPrintPrettyOutput(&cmd_context, printf);
		}

		// don't show csr commands on target client
		if (clientEntity && cmd_context.found_cmd && eHow != CMD_CONTEXT_HOWCALLED_CSR_COMMAND) {
			int cmd_accessLevel = cmd_context.found_cmd->access_level;
			if (cmd_accessLevel > 0) {
				ClientCmd_notifyRanAccessLevelCmd(clientEntity, cmd_context.found_cmd->name, cmd_accessLevel);
			}
		}
	}

	estrDestroy(&pOrigCmdString);
	estrDestroy(&expandedString);
	estrDestroy(&cmd_str_start);
	estrDestroy(&pInternalRetString);
	
	return 1;
}

int GameServerParseList(CmdList *cmdList, const char *cmd_str, CmdContextFlag iFlags, Entity *clientEntity, int iOverrideAccessLevel, bool *pbUnknownCommand, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	CmdContext		cmd_context = {0};
	CmdServerContext svr_context = {0};
	char *str = NULL;
	char *msg = NULL;
	int result = 0;
	ClientLink *client = NULL;
	char *pOrigCmdString = NULL;
	estrStackCreate(&pOrigCmdString);
	estrCopy2(&pOrigCmdString, cmd_str);

	estrStackCreateSize(&str,10000);

	cmd_context.flags = iFlags;
	cmd_context.eHowCalled = eHow;
	cmd_context.pStructList = pStructs;

	if (clientEntity && entGetPlayer(clientEntity))
	{
		ExpandCommandVars(cmd_str,&str,clientEntity);
		client = entGetClientLink(clientEntity);
		cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : GetAccessLevelForGSLCmdParse(clientEntity);
	}
	else
	{
		estrAppend2(&str,cmd_str);
		cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : ACCESS_INTERNAL; //JE: Had to change this for all of the chat commands (i.e. sginvite_long) to work  Is this the right thing to do?
	}

	svr_context.sourceStr = str;
	svr_context.clientEntity = clientEntity;

	cmd_context.data = &svr_context;

	InitCmdOutput(cmd_context,msg);

	result = cmdParseAndExecute(cmdList,str,&cmd_context);

	if (!result)
	{
		if (!cmd_context.found_cmd && !cmd_context.banned_cmd)
		{
			if (pbUnknownCommand)
			{
				*pbUnknownCommand = true;
			}
			Errorf("Internal server command (%s) not found",str);
		}
		else
		{
			if (!(cmd_context.found_cmd && cmd_context.found_cmd->flags & CMDF_IGNOREPARSEERRORS))
			{
				Errorf("Internal server command (%s) returned error %s",str,msg);
			}
		}
	}
	else
	{
		if (cmd_context.found_cmd->access_level > 0 && (cmd_context.flags & CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL)
			&& cmd_context.found_cmd->access_level > cmdGetMinAccessLevelWhichForcesLogging())
		{
			if (clientEntity && entGetPlayer(clientEntity))
			{
				entLog(LOG_COMMANDS, clientEntity, "AccessLevelGT0Command", "Player %s executed access level %d command string: %s",
					entGetLocalName(clientEntity), cmd_context.found_cmd->access_level, pOrigCmdString);
			}
		}
	}

	CleanupCmdOutput(cmd_context);
	estrDestroy(&str);
	estrDestroy(&pOrigCmdString);

	return result;
}

// This executes from the gateway command list, which users don't have access to and can only be executed on a gateway server
int GameServerParseGateway(const char *cmd_str, CmdContextFlag iFlags, Entity *clientEntity, int iOverrideAccessLevel, bool *pbUnknownCommand, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	return GameServerParseList(&gGatewayCmdList, cmd_str, iFlags, clientEntity, iOverrideAccessLevel, pbUnknownCommand, eHow, pStructs);
}

// This executes from the private command list, which users don't have access to
int GameServerParsePrivate(const char *cmd_str, CmdContextFlag iFlags, Entity *clientEntity, int iOverrideAccessLevel, bool *pbUnknownCommand, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	return GameServerParseList(&gPrivateCmdList, cmd_str, iFlags, clientEntity, iOverrideAccessLevel, pbUnknownCommand, eHow, pStructs);
}

int GameServerDefaultParse(const char *str, char **ppRetString, CmdContextFlag iCmdContextFlags, int iAccessLevelOverride, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	return GameServerParsePublic(str,iCmdContextFlags, NULL, ppRetString, iAccessLevelOverride, eHow, pStructs);
}

static void CmdPrint(char *string, Entity *ent)
{
	gslSendPrintf(ent,"%s",string);
}

static void CmdPrintToFile(char *string, void *userData)
{
	fprintf((FILE *)userData,"%s",string);
}


// Powers

//Command to change the global cooldown on the fly
AUTO_COMMAND ACMD_CATEGORY(Powers,Debug);
void PowersSetGlobalCooldown(F32 time)
{
	
	Entity* currEnt;
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		ClientCmd_PowersSetGlobalCooldownClient(currEnt,time);
	}
	EntityIteratorRelease(iter);
	g_CombatConfig.fCooldownGlobal = time;
}

AUTO_COMMAND ACMD_CATEGORY(Powers,Debug);
void PowersDebugServer(S32 enabled)
{
	g_bPowersDebug = !!enabled;
}

AUTO_COMMAND ACMD_CATEGORY(Powers,Debug);
void PowersSelectDebugServer(S32 enabled)
{
	g_bPowersSelectDebug = !!enabled;
}
AUTO_COMMAND ACMD_CATEGORY(Powers,Debug);
void PowersErrorsServer(S32 enabled)
{
	g_bPowersErrors = !!enabled;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void PowersDebugToggleFlags(Entity* e, ACMD_NAMELIST(EPowerDebugFlagsEnum, STATICDEFINE) const char* flagname)
{
	combatdebug_SetDebugFlagByName(flagname);

	ClientCmd_PowersDebugToggleFlagsClient(e, flagname);
}

AUTO_COMMAND ACMD_CATEGORY(Powers,Debug);
void PowerDisable(ACMD_NAMELIST("PowerDef", REFDICTIONARY) char *name, S32 bDisable)
{
	PowerDef *pdef = powerdef_Find(name);
	if(pdef)
	{
		if(bDisable)
		{
			eaPushUnique(&g_ppchPowersDisabled,pdef->pchName);
		}
		else
		{
			eaFindAndRemoveFast(&g_ppchPowersDisabled,pdef->pchName);
		}
	}
}

AUTO_COMMAND ACMD_NAME("CombatDebugPerfEnable");
void powersCmdCombatDebugPerfEnable(Entity* e, S32 bEnable)
{
	combatdebug_PerfEnable(e,bEnable);
}

AUTO_COMMAND ACMD_NAME("CombatDebugPerfReset");
void powersCmdCombatDebugPerfReset(void)
{
	combatdebug_PerfReset();
}

// Force the player to exit combat immediately
AUTO_COMMAND;
void CombatForceExit(Entity* e)
{
	if (e->pChar)
	{
		e->pChar->uiTimeCombatExit = 0;
		e->pChar->uiTimeCombatVisualsExit = 0;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

// Server command to cancel power activations.  Called by the client when it wants to outright
//  cancel an activation.  Does not always succeed.  ID can not be 0.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void PowersActCancelServer(Entity *pEnt, U32 uiActID, const char* pchDefReplacement)
{
	Character *pchar = pEnt->pChar;
	U8 uchID = (U8)uiActID;
	PowerDef *pdefReplacement = powerdef_Find(pchDefReplacement);

	if(pchar && uchID)
	{
		if(pchar->pPowActQueued && !pchar->pPowActQueued->bCommit && pchar->pPowActQueued->uchID==uchID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pEnt, "Queued %d CancelPowers: %d\n",uchID,pmTimestamp(0));
			character_ActQueuedCancelReason(entGetPartitionIdx(pEnt),pchar,pdefReplacement,uchID,kAttribType_Null,false);
		}
		else if(pchar->pPowActOverflow && !pchar->pPowActOverflow->bCommit && pchar->pPowActOverflow->uchID==uchID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pEnt, "Overflow %d CancelPowers: %d\n",uchID,pmTimestamp(0));
			character_ActOverflowCancelReason(entGetPartitionIdx(pEnt),pchar,pdefReplacement,uchID,kAttribType_Null,false);
		}
		else
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pEnt, "UnknownActivation %d CancelPowers: %d\n",uchID,pmTimestamp(0));
		}
	}
}

// Server command to set Character class. 
AUTO_COMMAND ACMD_NAME("CharacterSetClass") ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Powers) ACMD_PRIVATE;
void powersCmdCharacterSetClass(Entity *pEnt, ACMD_SENTENCE cpchClass)
{
	Character *pchar = pEnt->pChar;
	CharacterClass *pclass = RefSystem_ReferentFromString(g_hCharacterClassDict,cpchClass);

	if(	pchar && pclass && 
		pEnt->pPlayer &&
		entity_PlayerCanBecomeClass(pEnt, pclass))
	{
		character_SetClass(pchar,cpchClass);
	}
}

// Makes a new build based on your current state
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Powers);
void buildCreate_force(Entity *pent) 
{
	entity_BuildCreate(pent);
}

// Sets your build to the specified index
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Powers);
void buildSet_force(Entity *pent, U32 uiBuild) 
{
	entity_BuildSetCurrent(pent, uiBuild, false);
}

// Makes a new build based on your current state
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void buildCreate(Entity *pent) 
{
	entity_BuildCreate(pent);
}

// Deletes a build
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Powers);
void buildDelete_force(Entity *pent, U32 uiBuild)
{
	entity_BuildDelete(pent, uiBuild, false);
}

// Deletes a build
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Powers);
void buildDelete(Entity *pent, U32 uiBuild)
{
	entity_BuildDelete(pent, uiBuild, true);
}

// Sets your build to the specified index
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void buildSet(Entity *pent, U32 uiBuild) 
{
	entity_BuildSetCurrent(pent, uiBuild, true);
}

// Names a build
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void buildName(Entity *pent, U32 uiBuild, ACMD_SENTENCE pchName) 
{
	entity_BuildSetName(pent, uiBuild, pchName);
}

// Sets the class of a build
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void buildClass(Entity *pent, U32 uiBuild, ACMD_SENTENCE pchClass) 
{
	entity_BuildSetClass(pent, uiBuild, pchClass);
}

// Sets the item in iInvBag, iSlot to ilItemID which came from iSrcBag, iSrcSlot
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void buildSetItem(Entity *pent, U32 uiBuild, int iInvBag, int iSlot, U64 ilItemID, int iSrcBag, int iSrcSlot)
{
	entity_BuildSetItem(pent, uiBuild, iInvBag, iSlot, ilItemID, iSrcBag, iSrcSlot);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void buildCopyFromCurrent(Entity *pent, U32 uiDestBuild, S32 bCopyClass, S32 bCopyCostume, S32 bCopyItems, S32 bCopyPowers)
{
	entity_BuildCopyCurrent(pent, uiDestBuild, bCopyClass, bCopyCostume, bCopyItems, bCopyPowers);
}

// Interaction Manager

AUTO_COMMAND ACMD_NAME("InteractionManager.Debug");
void InteractionManagerDebugServer(int i)
{
	g_bInteractionDebug = i;
}

// sets/displays the server time
AUTO_COMMAND ACMD_NAME(time, timeSet) ACMD_SERVERCMD;
void timeSetHandler(F32 newtime)
{
	Entity* currEnt;
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		ClientCmd_clientTimeSet(currEnt,newtime);
	}
	EntityIteratorRelease(iter);

	wlTimeSet(newtime);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(time);
void timeGetHandler(Entity *client)
{
	gslSendPrintf(client, "%f", wlTimeGet());
}

static bool g_disableClientPerfLog=false;
AUTO_CMD_INT(g_disableClientPerfLog, disableClientPerfLog) ACMD_CMDLINE;

// Logs performance info from the client
AUTO_COMMAND ACMD_PRIVATE ACMD_SERVERCMD ACMD_ACCESSLEVEL(0);
void clientPerfLog(Entity *client, const char *category, const char *message)
{
	if (g_disableClientPerfLog)
		return;
	entLog(LOG_CLIENT_PERF, client, category, "%s", message);
}

// Logs performance info from the client
AUTO_COMMAND ACMD_PRIVATE ACMD_SERVERCMD ACMD_ACCESSLEVEL(0);
void clientSystemSpecsLog(Entity *client, const char *message)
{
	UserExp_LogSystemInfo(client, message);
}

// sets/displays the server time scale
AUTO_COMMAND ACMD_NAME(timescale) ACMD_SERVERCMD;
void timeScaleSetHandler(F32 newtime)
{
	Entity* currEnt;
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		ClientCmd_clientTimeScaleSet(currEnt,newtime);
	}
	EntityIteratorRelease(iter);

	wlTimeSetScale(newtime);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(timescale);
void timeScaleGetHandler(Entity *client)
{
	gslSendPrintf(client, "%f", wlTimeGetScale());
}

void timeStepScaleSetHandler(F32 newtimeDebug, F32 newtimeGame)
{
	Entity* currEnt;
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		ClientCmd_clientTimeStepScaleSet(currEnt, newtimeDebug, newtimeGame);
	}
	EntityIteratorRelease(iter);

	wlTimeSetStepScaleDebug(newtimeDebug);
	wlTimeSetStepScaleGame(newtimeGame);
}

//sets/displays the time step scale
AUTO_COMMAND ACMD_NAME(timeStepScaleDebug, timeStepScale) ACMD_SERVERCMD ACMD_ACCESSLEVEL(9);
void timeStepScaleDebug(F32 newtime)
{
	timeStepScaleSetHandler(newtime, wlTimeGetStepScaleGame());
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(timeStepScaleDebug);
void timeStepScaleGetHandler(Entity *client)
{
	gslSendPrintf(client, "%f", wlTimeGetStepScaleDebug());
}

// Toggles the time step scale between paused and not paused.
AUTO_COMMAND ACMD_SERVERCMD;
void timeStepPause(void) {
	static F32 lastscale;
	F32 scale = wlTimeGetStepScaleDebug();
	if (scale > 0.01) {
		timeStepScaleDebug(0.0000001);
	} else {
		timeStepScaleDebug(lastscale);
	}
	lastscale = scale;
}

// Print available commands
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(scmdlist) ACMD_HIDE;
void CommandServerCmdList(CmdContext *cmd_context, Entity *clientEntity)
{
	cmdPrintList(&gGlobalCmdList,cmd_context->access_level,NULL,0,CmdPrint,entGetLanguage(clientEntity),clientEntity);
}

// Print available commands containing <string>
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(scmds) ACMD_HIDE;
void CommandServerCmds(CmdContext *cmd_context, Entity *clientEntity, char *string)
{
	cmdPrintList(&gGlobalCmdList,cmd_context->access_level,string,0,CmdPrint,entGetLanguage(clientEntity),clientEntity);
}

// Print available commands containing <string>
AUTO_COMMAND ACMD_NAME(scmdsp);
void CommandServerCmdsPrivate(CmdContext *cmd_context, Entity *clientEntity, char *string)
{
	cmdPrintList(&gPrivateCmdList,cmd_context->access_level,string,0,CmdPrint,entGetLanguage(clientEntity),clientEntity);
}

// Export a list of all commands
AUTO_COMMAND;
void ExportSCmds(CmdContext *cmd_context)
{
	FILE *f = fopen("c:/scmdlist.txt","w");
	if (f)
	{	
		cmdPrintList(&gGlobalCmdList,cmd_context->access_level,NULL,1,CmdPrintToFile,cmd_context->language,f);
		fclose(f);
	}
}

AUTO_COMMAND;
void ExportSCmdsAsCSV(CmdContext *cmd_context, bool bShowHidden, S32 iMinAL, S32 iMaxAL)
{
	FILE *f = fopen("c:/scmdlist.csv","w");
	if (f) {

		fprintf(f, "AL,Name,Hide,Category,Comment\n");
		
		FOR_EACH_IN_STASHTABLE(gGlobalCmdList.sCmdsByName, Cmd, pCmd)
		{
			if (!(pCmd->flags & (CMDF_COMMANDLINEONLY | CMDF_COMMANDLINE | (bShowHidden ? 0 : CMDF_HIDEPRINT))) &&
				pCmd->access_level >= iMinAL && pCmd->access_level <= iMaxAL) 
			{
				
				fprintf(f, "%d,%s,%d,\"%s\",\"%s\"\n", pCmd->access_level, pCmd->name, (pCmd->flags & CMDF_HIDEPRINT) != 0, pCmd->categories, pCmd->comment);
			}
		}
		FOR_EACH_END;

		fclose(f);
	}
}

// After the time expires for logging out, this is called
void PostLogOutPlayer(Entity *pEnt, U32 iAuthTicket, LogoffType eType)
{
	if (eType != kLogoffType_MeetPartyInLobby &&
		eType != kLogoffType_Disconnect)
	{
		char pcChannelName[512];
		pcChannelName[0] = 0;
		if (team_IsWithTeam(pEnt)) {
			switch (pEnt->pTeam->eState) {
				case TeamState_Member:
					RemoteCommand_aslTeam_DoLogout(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, pEnt->myContainerID, false);
					break;
				case TeamState_Invitee:
					RemoteCommand_aslTeam_DeclineInvite(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, NULL, NULL, NULL);
					break;
				case TeamState_Requester:
					RemoteCommand_aslTeam_CancelRequest(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, NULL, NULL, NULL);
					break;
			}
			if (pEnt->pTeam->iInChat) {
				team_MakeTeamChannelNameFromID(pcChannelName, sizeof(pcChannelName), pEnt->pTeam->iInChat);
				RemoteCommand_ChatServerLeaveChannel(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, pcChannelName);
			}
		}

		// Force the players to meet with their team in the next login.
		if (pEnt->pTeam && pEnt->pTeam->iLastTeamIDForInitialMeeting)
		{
			pEnt->pTeam->iLastTeamIDForInitialMeeting = 0;
			entity_SetDirtyBit(pEnt, parse_PlayerTeam, pEnt->pTeam, true);
		}
	}
	
	ClientCmd_FinishLogOut(pEnt, iAuthTicket);
}

// Does pre-logout processing
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void CommandPreLogOutPlayer(Entity *pEnt, ControlScheme* pScheme)
{
	if ( pScheme ) //save control scheme with updated auto-adjusted fields
	{
		entity_SetSchemeDetails( pEnt, pScheme );
	}

	//Start the timed logout
	gslLogOutEntityNormal(pEnt);
}

// Does processing before the player goes back to the character select
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void CommandPreGoToCharacterSelectPlayer(Entity *pEnt, ControlScheme* pScheme)
{
	if ( pScheme ) //save control scheme with updated auto-adjusted fields
	{
		entity_SetSchemeDetails( pEnt, pScheme );
	}

	//Start the timed logout
	gslLogOutEntityGoToCharacterSelect(pEnt);
}

// Does processing before the player goes back to the lobby to meet with their team
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void CommandPreMeetTeamInLobby(Entity *pEnt, ControlScheme* pScheme)
{
	if ( pScheme ) //save control scheme with updated auto-adjusted fields
	{
		entity_SetSchemeDetails( pEnt, pScheme );
	}

	// Start the log out process
	gslLogOutMeetPartyInLobby(pEnt);
}


// Logs out a player
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void CommandLogOutPlayer(Entity *clientEntity)
{
	if (entGetClientLink(clientEntity))
	{
		gslSendForceLogout(entGetClientLink(clientEntity), "");
	}
	gslLogOutEntity(clientEntity, 0, 0);
}

AUTO_COMMAND;
void ForceLogout(char *containerTypeName, U32 containerID)
{
	Entity *ent = entFromContainerIDAnyPartition(NameToGlobalType(containerTypeName),containerID);
	if (ent)
	{
		CommandLogOutPlayer(ent);
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void ForceNoTimeout(Entity *e, int on)
{
	ClientLink *link = entGetClientLink(e);
	if (link)
	{
		if (on)
		{
			linkSetTimeout(link->netLink,0.0);
		}
		else if (!link->noTimeOut)
		{
			linkSetTimeout(link->netLink,CLIENT_LINK_TIMEOUT);
		}
	}
}


AUTO_COMMAND ACMD_PRIVATE;
void ServerDummyPrivate(ACMD_SENTENCE object)
{
	
}

void gslLoadCmdConfig(char *config_file)
{
	FILE	*file;
	char	buf[1000];

//	cmdServerStateInit();
	file = fileOpen(config_file, "rt");
	if(!file)
		file = fileOpen("svrconfig.txt","rt");
	if (!file)
		file = fileOpen("./svrconfig.txt","rt");
	if (!file)
		return;
	while(fgets(buf,sizeof(buf),file))
	{
		GameServerParsePublic(buf,0, NULL, NULL, -1, CMD_CONTEXT_HOWCALLED_CONFIGFILE, NULL);
/*		if (!context.found_cmd)
		{
			extern void parseArgs2(int argc,char **argv);
			char *args[100],*line2;
			int count;

			// HACK: &args[1] is used since parseArgs2 ignores the first
			//   parameter.
			count = tokenize_line_safe(buf, &args[1], ARRAY_SIZE(args)-1, &line2);
			parseArgs2(count+1, args);
		}*/
	}
	fileClose(file);
}

void SendCommandNamesToClientForAutoCompletion(Entity *pEntity)
{
	// TODO: Should probably change this into building a single string/list so it
	//  does not take so much bandwidth
	// We can safely call this on people without accesslevel, as it only sends down
	//  the same commands that show up in /cmdlist.  If we integrate auto-completion
	//  into the regular player text entry box, we should do this.
	if (GetAccessLevelForGSLCmdParse(pEntity) >= ACCESS_GM)
	{
		PERFINFO_AUTO_START_FUNC();
		
		FOR_EACH_IN_STASHTABLE(gGlobalCmdList.sCmdsByName, Cmd, pCmd)
		{
			if (pCmd->access_level <= GetAccessLevelForGSLCmdParse(pEntity) &&
				(GetAccessLevelForGSLCmdParse(pEntity) >= ACCESS_GM || !(pCmd->flags & CMDF_HIDEPRINT)))
			{
				ClientCmd_AddCommandNameToClientForAutoCompletion(pEntity, pCmd->name, pCmd->access_level);
			}
		}
		FOR_EACH_END;

		ClientCmd_ResetClientCommandArgsForAutoCompletion(pEntity);

		PERFINFO_AUTO_STOP();
	}
}

//called by the client to request auto-completion arg names for a given command
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_HIDE ACMD_SERVERCMD ACMD_IGNOREPARSEERRORS;
void RequestAutoCompletionArgNamesFromServer(Entity *pClientEntity, ACMD_SENTENCE pCommandName)
{
	Cmd *pCmd = cmdListFind(&gGlobalCmdList, pCommandName);
	int i;
	bool bHasANameList = false;
	NamesForAutoCompletion *pNames;


	if (!pCmd)
	{
		return;
	}

	if (pCmd->access_level > GetAccessLevelForGSLCmdParse(pClientEntity))
	{
		return;
	}
	
	for (i=0; i < CMDMAXARGS; i++)
	{
		if (pCmd->data[i].eNameListType != NAMELISTTYPE_NONE)
		{
			bHasANameList = true;
			break;
		}
	}

	if (!bHasANameList)
	{
		return;
	}

	pNames = StructCreate(parse_NamesForAutoCompletion);

	pNames->pCmdName = strdup(pCommandName);

	for (i=0; i < CMDMAXARGS; i++)
	{
		if (pCmd->data[i].eNameListType != NAMELISTTYPE_NONE)
		{
			NameList *pNameList;
			const char *pNameFromList;

			NamesForAutoCompletion_SingleArg *pNamesSingleArg = StructCreate(parse_NamesForAutoCompletion_SingleArg);
			pNamesSingleArg->iArgIndex = i;
			pNamesSingleArg->eNameListType = pCmd->data[i].eNameListType;
			eaPush(&pNames->ppNamesForArgs, pNamesSingleArg);

			pNameList = CreateTempNameListFromTypeAndData(pCmd->data[i].eNameListType, pCmd->data[i].ppNameListData);
			pNameList->erClientEntity = entGetRef(pClientEntity);

			if (pNameList)
			{
				while(pNameFromList = pNameList->pGetNextCB(pNameList, true))
				{
					eaPush(&pNamesSingleArg->ppNames, strdup(pNameFromList));
				}
				pNames->bUpdateClientDataPerRequest = pNameList->bUpdateClientDataPerRequest;
			}
			else
			{
				Errorf("Unable to create temporary name list\n");
			}
		}
	}


	ClientCmd_AddCommandArgsToClientForAutoCompletion(pClientEntity, pNames);

	StructDestroy(parse_NamesForAutoCompletion, pNames);

}

AUTO_COMMAND ACMD_NAME(entSendInterval);
void gslCmdEntSendInterval(F32 interval)
{
	gGSLState.entSend.seconds.interval = interval;
}

// Send a public command to the server, needed for commands in libraries
AUTO_COMMAND ACMD_NAME(ServerPublic,ServerCmd);
void ServerPublic(CmdContext *context, ACMD_NAMELIST(gGlobalCmdList, COMMANDLIST) Entity *pEntity, ACMD_SENTENCE cmd)
{
	GameServerParsePublic(cmd, 0, pEntity, NULL, -1, context->eHowCalled, NULL);
}

// Send a private command to the server
AUTO_COMMAND;
void ServerPrivate(CmdContext *context, ACMD_NAMELIST(gPrivateCmdList, COMMANDLIST) Entity *pEntity, ACMD_SENTENCE cmd)
{
	GameServerParsePrivate(cmd, 0, pEntity, -1, NULL, context->eHowCalled, NULL);
}


void gslSendPublicOrPrivateCommand(Entity *pEntity, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, bool bFast, CmdParseStructList *pStructs)
{
	if (bFast)
	{
		gslSendFastCommand(pEntity, iFlags, pCmd, eHow, bPrivate, pStructs);
	}
	else if (bPrivate)
	{
		gslSendPrivateCommand(pEntity, iFlags, pCmd, eHow, pStructs);
	}
	else
	{
		gslSendPublicCommand(pEntity, iFlags, pCmd, eHow, pStructs);
	}
}


AUTO_RUN;
void initGSLCommandParse(void)
{
	cmdSetServerToClientCB(gslSendPublicOrPrivateCommand);
}

AUTO_COMMAND ACMD_SERVERCMD;
void forceFullEntityUpdate()
{
	EntityIterator * iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE);
	Entity* currEnt;

	// Get the list of all entities on the server
	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		// Ask for a full update for each entity
		gslEntityForceFullSend(currEnt);
	}

	EntityIteratorRelease(iter);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void forceFullEntityUpdateForDemo(CmdContext *pContext, Entity *clientEntity)
{
	ClientLink *link = entGetClientLink(clientEntity);
	int curtime = timeSecondsSince2000();

	if (link)
	{
		if (curtime - link->demo_last_full_update_time > 10)
		{
			gslQueueFullUpdateForLink(link);
			link->demo_last_full_update_time = curtime;
			if(clientEntity && clientEntity->pChar)
			{
				character_ResetPowerActSeq(clientEntity->pChar);
				// Special slow send so reset data arrives after the full update
				ClientCmd_PowerActSeqResetSlow(clientEntity,clientEntity->pChar->uchPowerActSeqReset);
			}
		}
		else
		{
			estrPrintf(pContext->output_msg, "Ignoring call to forceFullEntityUpdateForDemo, called too often.");
		}
	}
}

// This allows a player to skip a cutscene.  This is only supported for single-player cutscenes such as zone flyovers.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME("SkipCutscene");
void gslSkipCutscene(Entity *e)
{
	if(e)
	{
		cutscene_PlayerSkipCutscene(e, false);
	}
}

// This allows a player to force skip a cutscene.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_NAME("ForceSkipCutscene");
void gslForceSkipCutscene(Entity *e)
{
	if(e)
	{
		cutscene_PlayerSkipCutscene(e, true);
	}
}

AUTO_COMMAND ACMD_NAME(sleepServer);
void gclCmdSleepServer(S32 milliseconds)
{
	if(milliseconds >= 0){
		printf("sleepServer: Sleeping for %dms...", milliseconds);
		Sleep(milliseconds);
		printf(" done!\n");
	}
}

AUTO_COMMAND ACMD_NAME(mmSendFullRotations) ACMD_SERVERONLY;
void mmCmdSendFullRotations(Entity* e, S32 enabled)
{
	mmClientSetSendFullRotations(	SAFE_MEMBER3(e, pPlayer, clientLink, movementClient),
									enabled);
}

// Sets the Player's movement throttle, clamped to [-0.25..1]
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Movement) ACMD_ACCESSLEVEL(0);
void MovementThrottleSet(Entity* e, F32 fThrottle)
{
	if(e && e->pPlayer)
	{
		e->pPlayer->fMovementThrottle = CLAMPF32(fThrottle,PLAYER_MIN_THROTTLE,PLAYER_MAX_THROTTLE);
		if(e->pChar)
			e->pChar->bUpdateFlightParams = true;
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}
}


AUTO_COMMAND;
void PrintDependencies(Entity* e, char *pDictionaryName, char *pReferentName)
{
	int i;
	ResourceInfoHolder *pHolder = StructCreate(parse_ResourceInfoHolder);
	if (resFindDependencies(pDictionaryName, pReferentName, pHolder))
	{	
		gslSendPrintf(e, "Dependencies for %s %s:\n", pDictionaryName, pReferentName);
		for (i = 0; i < eaSize(&pHolder->ppInfos); i++)
		{
			gslSendPrintf(e, "%s %s: %s\n", pHolder->ppInfos[i]->resourceDict, pHolder->ppInfos[i]->resourceName, 
				pHolder->ppInfos[i]->resourceLocation ? pHolder->ppInfos[i]->resourceLocation : "UNKNOWN");
		}		
		gslSendPrintf(e, "\n");
	}
	else
	{
		gslSendPrintf(e, "No dependencies, or invalid object\n");
	}	
	StructDestroy(parse_ResourceInfoHolder, pHolder);
}

AUTO_COMMAND ACMD_NAME(IAmAnArtist, Artist, ArtistMode);
void cmdArtist(S32 enabled)
{
	globCmdParsef("bcnNoLoad %d", enabled);
	globCmdParsef("noAI %d", enabled);
	globCmdParsef("encounterProcessing %d", !enabled);
	globCmdParsef("DisableCutscenes %d", enabled);
	globCmdParsef("aiCivilianDisable %d", enabled);
}

AUTO_COMMAND ACMD_NAME(sendPacketVerifyData);
void cmdSendPacketVerifyData(Entity* e, S32 enabled)
{
	if(SAFE_MEMBER2(e, pPlayer, clientLink))
	{
		e->pPlayer->clientLink->doSendPacketVerifyData = !!enabled;
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void Remote_RunCSRCommand(CSRCommandObject *pCSRObject)
{
	char *pOutputString = NULL;
	Entity *pOtherEnt = entFromContainerIDAnyPartition(pCSRObject->playerType, pCSRObject->playerID);
	if (!pOtherEnt)
	{
		objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Could not find player %s", pCSRObject->playerName);
		objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
			"RunCSRCommand", NULL, "Failed, could not find player %s", pCSRObject->playerName);
		return;
	}

	gslSetCSRListener(pOtherEnt, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->callerName, pCSRObject->callerAccount, 10, pCSRObject->callerAccessLevel);
	estrStackCreate(&pOutputString);
	GameServerParsePublic(pCSRObject->commandString, CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL, pOtherEnt, &pOutputString, pCSRObject->callerAccessLevel, CMD_CONTEXT_HOWCALLED_CSR_COMMAND, NULL);
	objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Executed command %s on player %s with result: %s", pCSRObject->commandString, pCSRObject->playerName, pOutputString);
	objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
		"RunCSRCommand", NULL, "Succeeded on player %s, result %s:", pCSRObject->playerName, pOutputString);
	estrDestroy(&pOutputString);
}

static void RunCSRCommand_ReturnToDBReturn(TransactionReturnVal *returnVal, CSRCommandObject *pCSRObject)
{
	switch(returnVal->eOutcome)
	{
		xcase TRANSACTION_OUTCOME_FAILURE:
			// Not sure how this could fail or how to handle this properly
			objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Ran CSR command on player %s, but couldn't transfer back to ObjectDB", pCSRObject->playerName);
			objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
				"RunCSRCommand", NULL, "Ran CSR command on player %s, but couldn't transfer back to ObjectDB", pCSRObject->playerName);

		xcase TRANSACTION_OUTCOME_SUCCESS:
			gslOfflineCSREntRemove(pCSRObject->playerType, pCSRObject->playerID);
	}
	StructDestroy(parse_CSRCommandObject, pCSRObject);
}

static void RunCSRCommand_MoveOnlineReturn(TransactionReturnVal *returnVal, CSRCommandObject *pCSRObject)
{
	switch(returnVal->eOutcome)
	{
		xcase TRANSACTION_OUTCOME_FAILURE:
			objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Can't run CSR command on player %s, couldn't be transferred to local map", pCSRObject->playerName);
			objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
				"RunCSRCommand", NULL, "Failed, player %s couldn't be transferred to local map", pCSRObject->playerName);

		xcase TRANSACTION_OUTCOME_SUCCESS:
			Remote_RunCSRCommand(pCSRObject);

			objRequestContainerMove(objCreateManagedReturnVal(RunCSRCommand_ReturnToDBReturn, pCSRObject),
				pCSRObject->playerType, pCSRObject->playerID, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);

			return; // so we don't free pCSRObject
	}
	StructDestroy(parse_CSRCommandObject, pCSRObject);
}

static void RunCSRCommand_GetLocationReturn(TransactionReturnVal *returnVal, CSRCommandObject *pCSRObject)
{
	ContainerRef *pLocation;
	switch(RemoteCommandCheck_ContainerGetOwner(returnVal, &pLocation))
	{
	case TRANSACTION_OUTCOME_FAILURE:		
		objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Could not find player %s", pCSRObject->playerName);
		objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
			"RunCSRCommand", NULL, "Failed, could not find player %s", pCSRObject->playerName);
		break;
	case TRANSACTION_OUTCOME_SUCCESS:
		if (pLocation->containerType == GLOBALTYPE_LOGINSERVER)
		{
			objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Can't run CSR command on player %s, currently logging in", pCSRObject->playerName);
			objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
				"RunCSRCommand", NULL, "Failed, player %s is currently logging in", pCSRObject->playerName);
		}
		else if (pLocation->containerType == GLOBALTYPE_GAMESERVER)
		{
			RemoteCommand_Remote_RunCSRCommand(pLocation->containerType, pLocation->containerID, pCSRObject);
		}
		else if (pLocation->containerType == GLOBALTYPE_OBJECTDB)
		{
			//we are moving the other player directly here from the object DB. This will break partitioning so we need
			//to specify a partition
			Entity *pCallingEnt = entFromContainerIDAnyPartition(pCSRObject->callerType, pCSRObject->callerID);
			if (pCallingEnt)
			{
				MapPartitionSummary summary = {0};
				int iCallerPartitionIdx = entGetPartitionIdx(pCallingEnt);
				StructInit(parse_MapPartitionSummary, &summary);
				StructCopy(parse_MapPartitionSummary, &(partition_FromIdx(iCallerPartitionIdx)->summary), &summary, 0, 0, 0);

				gslOfflineCSREntAdd(pCSRObject->playerType, pCSRObject->playerID);

				HereIsPartitionInfoForUpcomingMapTransfer(0, pCSRObject->playerID, summary.uPartitionID, &summary);

				objRequestContainerMove(objCreateManagedReturnVal(RunCSRCommand_MoveOnlineReturn, pCSRObject),
					pCSRObject->playerType, pCSRObject->playerID, GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID());

				StructDestroy(parse_ContainerRef, pLocation);
				StructDeInit(parse_MapPartitionSummary, &summary);
			}
			return; // so we don't free pCSRObject
		}
		else
		{
			objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Can't run CSR command on player %s, at unknown location %s[%d].", pCSRObject->playerName, GlobalTypeToName(pLocation->containerType), pLocation->containerID);		
			objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
				"RunCSRCommand", NULL, "Failed, player %s at unknown location %s[%d]", pCSRObject->playerName, GlobalTypeToName(pLocation->containerType), pLocation->containerID);
		}
		StructDestroy(parse_ContainerRef, pLocation);
		break;
	}
	StructDestroy(parse_CSRCommandObject, pCSRObject);
}


void RunCSRCommand_GetIDReturn(TransactionReturnVal *returnVal, CSRCommandObject *pCSRObject)
{
	ContainerID returnID = 0;
	enumTransactionOutcome eOutcome = gslGetPlayerIDFromNameWithRestoreReturn(returnVal, &returnID);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && returnID)
	{
		Entity *pOtherEnt;
		pCSRObject->playerID = returnID;
		pOtherEnt = entFromContainerIDAnyPartition(pCSRObject->playerType, pCSRObject->playerID);
		if (pOtherEnt)
		{
			Remote_RunCSRCommand(pCSRObject);
		}
		else
		{
			RemoteCommand_ContainerGetOwner(objCreateManagedReturnVal(RunCSRCommand_GetLocationReturn, pCSRObject),
				pCSRObject->playerType, pCSRObject->playerID);
			return; // so we don't free pCSRObject
		}		
	}
	else
	{
		objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Could not find player %s", pCSRObject->playerName);
		objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
			"RunCSRCommand", NULL, "Failed, could not find player %s", pCSRObject->playerName);
	}
	StructDestroy(parse_CSRCommandObject, pCSRObject);
}

// Executes an arbitrary command through another character, with the CSR's access level
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_NAME(CSR) ACMD_CATEGORY(csr);
void RunCSRCommand(Entity *pCaller, const char *entName, ACMD_SENTENCE pCommandString)
{
	if (!entName || !strchr(entName, '@'))
	{
		gslSendPrintf(pCaller, "Can't call csr command on name that is not full character@accountname name.");
	}
	else
	{	
		CSRCommandObject *pCSRObject = StructCreate(parse_CSRCommandObject);
		pCSRObject->callerType = entGetType(pCaller);
		pCSRObject->callerID = entGetContainerID(pCaller);
		pCSRObject->callerAccessLevel = GetAccessLevelForGSLCmdParse(pCaller);
		pCSRObject->callerName = strdup(pCaller->debugName);
		if (pCaller->pPlayer)
		{
			pCSRObject->callerAccount = strdup(pCaller->pPlayer->privateAccountName);
		}
		pCSRObject->playerType = GLOBALTYPE_ENTITYPLAYER;
		pCSRObject->commandString = strdup(pCommandString);
		pCSRObject->playerName = strdup(entName);

		objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
			"RunCSRCommand", NULL, "Attempted CSR command of %s on player %s", pCSRObject->commandString, pCSRObject->playerName);

		gslGetPlayerIDFromNameWithRestore(entName, entGetVirtualShardID(pCaller), RunCSRCommand_GetIDReturn, pCSRObject);
	}
}

void RunCSRPetCommand_GetIDReturn(TransactionReturnVal *returnVal, CSRCommandObject *pCSRObject)
{
	ContainerID returnID = 0;
	enumTransactionOutcome eOutcome = gslGetPetIDFromNameReturn(returnVal, &returnID);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && returnID)
	{
		Entity *pOtherEnt;
		pCSRObject->playerID = returnID;
		pOtherEnt = entFromContainerIDAnyPartition(pCSRObject->playerType, pCSRObject->playerID);
		if (pOtherEnt)
		{
			Remote_RunCSRCommand(pCSRObject);
		}
		else // Do NOT request the container here, because it is guaranteed to fail the partition assert in gslInitializeEntity
		{
			objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Saved Pet %s is not available on the current game server", pCSRObject->playerName);
			objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
				"RunCSRPetCommand", NULL, "Failed, could not find Saved Pet %s", pCSRObject->playerName);
		}		
	}
	else
	{
		objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Could not find Saved Pet %s", pCSRObject->playerName);
		objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
			"RunCSRPetCommand", NULL, "Failed, could not find Saved Pet %s", pCSRObject->playerName);
	}
	StructDestroy(parse_CSRCommandObject, pCSRObject);
}

// Executes an arbitrary command through another character, with the CSR's access level
// Syntax is petname:character@account
// NOTE:  This works, but we aren't sure that we want people to use it yet since it may
// be unsafe to run certain commands on a pet/nemesis.  So I am leaving it at ACCESSLEVEL 9
// for now.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(csrpet) ACMD_CATEGORY(csr);
void RunCSRPetCommand(Entity *pCaller, const char *pchPetRef, ACMD_SENTENCE pCommandString)
{
	if (!pchPetRef || !strchr(pchPetRef, '@') || !strchr(pchPetRef, ':'))
	{
		gslSendPrintf(pCaller, "Can't call csrpet command on name that is not full petname:character@accountname name.");
	}
	else
	{	
		CSRCommandObject *pCSRObject = StructCreate(parse_CSRCommandObject);
		pCSRObject->callerType = entGetType(pCaller);
		pCSRObject->callerID = entGetContainerID(pCaller);
		pCSRObject->callerAccessLevel = GetAccessLevelForGSLCmdParse(pCaller);
		pCSRObject->callerName = strdup(pCaller->debugName);
		if (pCaller->pPlayer)
		{
			pCSRObject->callerAccount = strdup(pCaller->pPlayer->privateAccountName);
		}
		pCSRObject->playerType = GLOBALTYPE_ENTITYSAVEDPET;
		pCSRObject->commandString = strdup(pCommandString);
		pCSRObject->playerName = strdup(pchPetRef);

		objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
			"RunCSRCommand", NULL, "Attempted CSR command of %s on Saved Pet %s", pCSRObject->commandString, pCSRObject->playerName);

		gslGetPetIDFromName(pchPetRef, entGetVirtualShardID(pCaller), RunCSRPetCommand_GetIDReturn, pCSRObject);
	}
}

void cmdGetPetList_Return(TransactionReturnVal *returnVal, ContainerID *pEntID)
{
	enumTransactionOutcome eOutcome;
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);
	
	eOutcome = RemoteCommandCheck_dbPetListFromPlayerReference(returnVal, &estrBuffer);
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && estrBuffer && estrBuffer[0])
	{
		objPrintf(GLOBALTYPE_ENTITYPLAYER, *pEntID, "%s", estrBuffer);
	}
	estrDestroy(&estrBuffer);
	free(pEntID);
}

// Gets a list of the Names and IDs of all pets belonging to the specified player
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_NAME(getpetlist) ACMD_CATEGORY(csr);
void cmdGetPetList(Entity *pCaller, const char *entName)
{
	if (!entName || !strchr(entName, '@'))
	{
		gslSendPrintf(pCaller, "Can't call getpetlist command on name that is not full character@accountname name.");
	}
	else
	{
		ContainerID *pEntID = malloc(sizeof(ContainerID));
		*pEntID = entGetContainerID(pCaller);
		RemoteCommand_dbPetListFromPlayerReference(objCreateManagedReturnVal(cmdGetPetList_Return, pEntID),
			GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, entName, entGetVirtualShardID(pCaller));
	}
}

static void RunCSRPetCallback_GetIDReturn(TransactionReturnVal *returnVal, CSRCommandObject *pCSRObject)
{
	ContainerID returnID = 0;
	enumTransactionOutcome eOutcome = gslGetPetIDFromNameReturn(returnVal, &returnID);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && returnID)
	{
		pCSRObject->cbPetFunc(pCSRObject->playerType, returnID);
	}
	else
	{
		objPrintf(pCSRObject->callerType, pCSRObject->callerID, "Could not find Saved Pet %s", pCSRObject->playerName);
		objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
			"RunCSRPetCallback", NULL, "Failed, could not find Saved Pet %s", pCSRObject->playerName);
	}
	StructDestroy(parse_CSRCommandObject, pCSRObject);
}

static void RunCSRPetCallback(Entity *pCaller, const char* pchPetRef, CSRPetCallback cbFunc)
{
	if (!pchPetRef || !strchr(pchPetRef, '@') || !strchr(pchPetRef, ':'))
	{
		gslSendPrintf(pCaller, "Can't call csrpet command on name that is not full petname:character@accountname name.");
	}
	else
	{	
		CSRCommandObject *pCSRObject = StructCreate(parse_CSRCommandObject);
		pCSRObject->callerType = entGetType(pCaller);
		pCSRObject->callerID = entGetContainerID(pCaller);
		pCSRObject->callerAccessLevel = GetAccessLevelForGSLCmdParse(pCaller);
		pCSRObject->callerName = strdup(pCaller->debugName);
		if (pCaller->pPlayer)
		{
			pCSRObject->callerAccount = strdup(pCaller->pPlayer->privateAccountName);
		}
		pCSRObject->playerType = GLOBALTYPE_ENTITYSAVEDPET;
		pCSRObject->cbPetFunc = cbFunc;
		pCSRObject->playerName = strdup(pchPetRef);

		objLog(LOG_CSR, pCSRObject->callerType, pCSRObject->callerID, pCSRObject->playerID, pCSRObject->callerName, NULL, pCSRObject->callerAccount, 
			"RunCSRPetCallback", NULL, "Attempted CSR pet callback on Saved Pet %s", pCSRObject->playerName);

		gslGetPetIDFromName(pchPetRef, entGetVirtualShardID(pCaller), RunCSRPetCallback_GetIDReturn, pCSRObject);
	}
}

static void BadPetName_CB(GlobalType ePetType, ContainerID uiPetID)
{
	gslEntity_BadName(ePetType, uiPetID, GLOBALTYPE_NONE, 0);
}

// Flags a player's pet or Nemesis as having an invalid name
// Gives the pet a default name and a free name change
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_NAME(BadPetName) ACMD_CATEGORY(csr);
void BadPetName(Entity *pCaller, const char *pchPetRef)
{
	RunCSRPetCallback(pCaller, pchPetRef, BadPetName_CB);
}

// Flags a player's pet or Nemesis as having a bad Costume
// Changes the pet's costume to the default and gives them a free costume change
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_NAME(BadPetCostume) ACMD_CATEGORY(csr);
void BadPetCostume(Entity *pCaller, const char *pchPetRef)
{
	//TODO: Make this work with RunCSRPetCallback instead
	RunCSRPetCommand(pCaller, pchPetRef, "badcostume");
}

// Reset recent interaction list
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void ResetRecentAutoExecuteInteractions(Entity *pCaller)
{
	if (pCaller && pCaller->pPlayer)
	{
		interactable_ClearPlayerInteractableTrackingData(pCaller);
	}
}

//Clears the list of recently clicked clickables
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void ResetRecentClickableList(Entity *pCaller)
{
	if (pCaller && pCaller->pPlayer)
	{
		interactable_ClearPlayerRecentClickableData(pCaller);
	}
}

//List all recently clicked clickables for the current player
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void ListRecentClickables(Entity *pCaller)
{
	if (pCaller && pCaller->pPlayer && pCaller->pPlayer->pInteractInfo)
	{
		int i;
		for(i = eaSize(&pCaller->pPlayer->pInteractInfo->recentlyCompletedInteracts)-1; i >= 0; i--)
		{
			InteractionInfo *pInfo = pCaller->pPlayer->pInteractInfo->recentlyCompletedInteracts[i];
			if(pInfo)
			{
				if (pInfo->pchInteractableName && pInfo->pchInteractableName[0])
					gslSendPrintf(pCaller, "Interactable: %s\n", pInfo->pchInteractableName);
				else if (pInfo->pchVolumeName && pInfo->pchVolumeName[0])
					gslSendPrintf(pCaller, "Volume: %s\n", pInfo->pchVolumeName);
				else if (pInfo->erTarget)
					gslSendPrintf(pCaller, "Entity: %d\n", pInfo->erTarget);
			}
		}
	}
}

void DEFAULT_LATELINK_GetObjInfoForCmdParseLogging(CmdContext *pContext, GlobalType *pOutType, ContainerID *pOutID, 
	const char **ppOutObjName /*NOT AN ESTRING*/, const char **ppOutOwnerString, const char **ppProjSpecificString /*NOT AN ESTRING*/);

void OVERRIDE_LATELINK_GetObjInfoForCmdParseLogging(CmdContext *pContext, GlobalType *pOutType, ContainerID *pOutID, 
	const char **ppOutObjName /*NOT AN ESTRING*/, const char **ppOutOwnerString, const char **ppProjSpecificString /*NOT AN ESTRING*/)
{
	if (pContext->svr_context && pContext->svr_context->clientEntity)
	{
		
		if (pContext->svr_context->clientEntity->pPlayer)
		{
			*ppOutOwnerString = pContext->svr_context->clientEntity->pPlayer->privateAccountName;
		}

		*pOutType = entGetType(pContext->svr_context->clientEntity);
		*pOutID = entGetContainerID(pContext->svr_context->clientEntity);
		*ppOutObjName = pContext->svr_context->clientEntity->debugName;
		*ppProjSpecificString = entity_GetProjSpecificLogString(pContext->svr_context->clientEntity);
	}
	else
	{
		DEFAULT_LATELINK_GetObjInfoForCmdParseLogging(pContext, pOutType, pOutID, ppOutObjName, ppOutOwnerString, ppProjSpecificString);
	}
}

// Dump server messages not in the Except list.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Debug);
void CmdParseDumpServerMessages(Entity *pEnt, const char *pchPrefix, CommandNameList *pExcept, const char *pchPath)
{
	CmdParseDumpMessages(pchPrefix, pExcept, NULL, pchPath);
	ClientCmd_CmdParseDumpServerMessagesSucceeded(pEnt);
}


//comma-or-newline-separated string
AUTO_COMMAND_REMOTE;
void SetGSLBannedCommands(char *pCommands)
{
	char **ppCommands = NULL;

	DivideString(pCommands, ",\n", &ppCommands, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS	| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

	cmdSetBannedCommands(&ppCommands);

	eaDestroyEx(&ppCommands, NULL);

}

int OVERRIDE_LATELINK_cmdGetMinAccessLevelWhichForcesLogging(void)
{
	if (isProductionEditMode())
	{
		return ACCESS_UGC;
	}

	return 0;
}

//comma-separated list of GS commands whose AL should be changed to something else, ie "doSomething 7, doThisOtherThing 0"
static char *spAccessLevelOverrides = NULL;
AUTO_CMD_ESTRING(spAccessLevelOverrides, AccessLevelOverrides) ACMD_AUTO_SETTING(Misc, GAMESERVER) ACMD_CALLBACK(AccessLevelOverrideChanged);


void AccessLevelOverrideChanged(CMDARGS)
{
	cmdUpdateAccessLevelsFromCommaSeparatedString(&gGlobalCmdList, "Global", spAccessLevelOverrides ? spAccessLevelOverrides : "", cmd_context->access_level);
	cmdUpdateAccessLevelsFromCommaSeparatedString(&gPrivateCmdList, "Private", spAccessLevelOverrides ? spAccessLevelOverrides : "", cmd_context->access_level);
}

// This dumps all replacements (i.e. {Var}) of the messages in message dictionary, as well
// as which strings use the replacement.
AUTO_COMMAND ACMD_NAME(ExportMessageReplacements) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void CmdParseExportMessageReplacements(Entity *pEnt)
{
	// Hacky inline auto structs, since this function is a one off thing (specifically for the
	// purpose of determining the meanings of variable replacements) and really needs to be
	// self-contained to this function, I don't really see much of a point to make them full
	// AUTO_STRUCT and import the auto generated files.
	//
	// This exists strictly to be able to use ParserWriteText on the structs.
	typedef struct MessageReplacement
	{
		char *pchKey;
		Message **eaMessages;
	} MessageReplacement;
	typedef struct MessageReplacements
	{
		MessageReplacement **eaReplacements;
	} MessageReplacements;
	extern ParseTable parse_Message[];
	ParseTable parse_MessageReplacement[] =
	{
		{ "MessageReplacement",		TOK_IGNORE , sizeof(MessageReplacement), 0, NULL, 0, NULL },
		{ "{",						TOK_START, 0 },
		{ "Key",					TOK_STRUCTPARAM | TOK_STRING(MessageReplacement, pchKey, 0), NULL },
		{ "Message",				TOK_STRUCT(MessageReplacement, eaMessages, parse_Message) },
		{ "}",						TOK_END, 0 },
		{ "", 0, 0 }
	};
	ParseTable parse_MessageReplacements[] =
	{
		{ "MessageReplacements",	TOK_IGNORE , sizeof(MessageReplacements), 0, NULL, 0, NULL },
		{ "{",						TOK_START, 0 },
		{ "Replacement",			TOK_NO_INDEX | TOK_STRUCT(MessageReplacements, eaReplacements, parse_MessageReplacement) },
		{ "}",						TOK_END, 0 },
		{ "", 0, 0 }
	};

	MessageReplacements Replacements = {0};
	char *estrResult = NULL;
	const char **eapchStarts = NULL;
	S32 i, j;

	estrCreate(&estrResult);

	FOR_EACH_IN_REFDICT("Message", Message, pMessage);
	{
		const char *pc;
		if (!pMessage || !pMessage->pcDefaultString || !strchr(pMessage->pcDefaultString, STRFMT_TOKEN_START))
			continue;

		for (pc = pMessage->pcDefaultString; *pc; pc++)
		{
			const char *pcStart = eaSize(&eapchStarts) > 0 ? eaTail(&eapchStarts) : NULL;
			if (*pc == STRFMT_TOKEN_START)
				eaPush(&eapchStarts, pc);
			else if (pcStart && *pc == STRFMT_TOKEN_END)
			{
				if (*pcStart == STRFMT_TOKEN_START)
				{
					MessageReplacement *pReplacement = NULL;
					estrTrimLeadingAndTrailingWhitespace(&estrResult);
					for (j = eaSize(&Replacements.eaReplacements) - 1; j >= 0; j--)
						if (stricmp(Replacements.eaReplacements[j]->pchKey, estrResult) <= 0)
							break;
					if (j < 0 || stricmp(estrResult, Replacements.eaReplacements[j]->pchKey) != 0)
					{
						pReplacement = calloc(1, sizeof(MessageReplacement));
						pReplacement->pchKey = StructAllocString(estrResult);
						eaIndexedEnable(&pReplacement->eaMessages, parse_Message);
						eaInsert(&Replacements.eaReplacements, pReplacement, j + 1);
					}
					else
						pReplacement = Replacements.eaReplacements[j];
					eaIndexedAdd(&pReplacement->eaMessages, pMessage);
				}
				estrClear(&estrResult);
				eaPop(&eapchStarts);
			}
			else if (pcStart && *pcStart == STRFMT_TOKEN_START && *pc == STRFMT_TOKEN_CHOICE)
			{
				estrClear(&estrResult);
				eapchStarts[eaSize(&eapchStarts) - 1] = pc;
			}
			else if (pcStart && *pcStart == STRFMT_TOKEN_START)
				estrConcatChar(&estrResult, *pc);
		}
		eaClearFast(&eapchStarts);
		estrClear(&estrResult);
	}
	FOR_EACH_END;

	ParserWriteText(&estrResult, parse_MessageReplacements, &Replacements, 0, 0, 0);
	ClientCmd_cmdExportMessageReplacements(pEnt, estrResult);
	estrDestroy(&estrResult);

	eaDestroy(&eapchStarts);
	for (i = eaSize(&Replacements.eaReplacements) - 1; i >= 0; i--)
	{
		eaDestroy(&Replacements.eaReplacements[i]->eaMessages);
		StructFreeString(Replacements.eaReplacements[i]->pchKey);
		free(Replacements.eaReplacements[i]);
	}
	eaDestroy(&Replacements.eaReplacements);
}

AUTO_COMMAND ACMD_PRIVATE ACMD_SERVERCMD;
void SendMeCommandsForCSV(Entity *pEntity)
{
	CommandForCSVList *pList = StructCreate(parse_CommandForCSVList);
	MakeCommandsForCSV();

	FOR_EACH_IN_STASHTABLE(sCommandsForCSVExport, CommandForCSV, pCommand)
	{
		eaPush(&pList->ppCommands, pCommand);
	}
	FOR_EACH_END;

	ClientCmd_HereAreServerCommandsForCSV(pEntity, pList);

	eaDestroy(&pList->ppCommands);
	StructDestroy(parse_CommandForCSVList, pList);
}

AUTO_COMMAND ACMD_PRIVATE ACMD_SERVERCMD;
void VerifyCommandForGclAutoComplete(char *pCommandName, int iAccessLevel)
{
	Cmd *pCmd = cmdListFind(&gGlobalCmdList, pCommandName);
	if (pCmd)
	{
		if (pCmd->access_level != iAccessLevel)
		{
			Errorf("ChatAutoComplete.def thinks that command %s has access level %d, actual access level is %d\n",
				pCommandName, iAccessLevel, pCmd->access_level);
		}

		if (pCmd->flags & (CMDF_HIDEPRINT | CMDF_EARLYCOMMANDLINE | CMDF_COMMANDLINEONLY))
		{
			Errorf("ChatAutoComplete.def contains command %s, but it's hidden, or command line only",
				pCommandName);
		}

		return;
	}

	pCmd = cmdListFind(&gPrivateCmdList, pCommandName);
	if (pCmd)
	{
		Errorf("ChatAutoComplete.def contains command %s, but it's private",
			pCommandName);
		return;
	}

	pCmd = cmdListFind(&gEarlyCmdList, pCommandName);
	if (pCmd)
	{
		Errorf("ChatAutoComplete.def contains command %s, but it's earlyCommandLine",
			pCommandName);
		return;
	}

	Errorf("ChatAutoComplete.def contains command %s, but it doesn't seem to exist on the client or the server",
		pCommandName);
}


AUTO_COMMAND ACMD_PRIVATE ACMD_SERVERCMD;
void GetCmdPrintInfoForClient(Entity *pEntity, char *pCommandName, bool bDoPrintsInGfxConsole)
{
	char *pOutString = NULL;
	char *pInfo =  cmdGetInfo_Internal(pCommandName);
	if (pInfo)
	{
		estrPrintf(&pOutString, "%s is on SERVER\n%s\n", pCommandName, pInfo);
	}
	else
	{
		estrPrintf(&pOutString, "%s does not exist on client or server\n", pCommandName);
	}

	ClientCmd_HereIsInfoAboutCommand(pEntity, pOutString, bDoPrintsInGfxConsole);

	estrDestroy(&pOutString);
}
