/***************************************************************************



***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cmdparse.h"
#include "strings_opt.h"
#include "file.h"
#include "winutil.h"
#include "SimpleParser.h"
#include "StringCache.h"
#include "sysutil.h"
#include "logging.h"
#include "stringUtil.h"
#include "Message.h"
#include "StringFormat.h"
#include "ResourceManager.h"
#include "CommandTranslation.h"
#include "trivia.h"
#include "FolderCache.h"
#include "cmdparse_h_ast.h"
#include "commandline.h"
#include "net/net.h"
#include "structNet.h"
#include "earray.h"
#include "mathutil.h"
#include "CmdParseJson.h"
#include "globalEnums.h"
#include "ControllerScriptingSupport.h"

void *cmdParseAlloc(int iSize);
void cmdParseFree(void **ppBuffer);

bool gbLogAllAccessLevelCommands = false;

static __forceinline bool cmdContextHasStructList(CmdContext *pContext)
{
	return pContext->pStructList && eaSize(&pContext->pStructList->ppEntries);
}


LATELINK;
void ControllerAutoSetting_CmdWasCalled(const char *pCmdName, enumCmdContextHowCalled eHow);
void DEFAULT_LATELINK_ControllerAutoSetting_CmdWasCalled(const char *pCmdName, enumCmdContextHowCalled eHow)
{
}

LATELINK;
void AutoSetting_CmdWasCalled(const Cmd *pCmd, enumCmdContextHowCalled eHow, const char *pFullStr);
void DEFAULT_LATELINK_AutoSetting_CmdWasCalled(const Cmd *pCmd, enumCmdContextHowCalled eHow, const char *pFullStr)
{
}


LATELINK;
void GetObjInfoForCmdParseLogging(CmdContext *pContext, GlobalType *pOutType, ContainerID *pOutID, const char **ppOutObjName, const char **ppOutOwnerString, const char **ppProjSpecificString /*NOT AN ESTRING*/);

LATELINK;
void UseRealAccessLevelInUGC_ExtraStuff(int iSet);

bool gbUseRealAccessLevelInUGC = false;

void DEFAULT_LATELINK_UseRealAccessLevelInUGC_ExtraStuff(int iSet)
{
}

AUTO_COMMAND ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void UseRealAccessLevelInUGC(int iSet)
{
	gbUseRealAccessLevelInUGC = !!iSet;
	UseRealAccessLevelInUGC_ExtraStuff(iSet);
}





AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

//this is obviously hilariously unsafe but is only present on the server
bool gDontCheckAccessLevelForCommands = false;
AUTO_CMD_INT(gDontCheckAccessLevelForCommands, DontCheckAccessLevelForCommands) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_SERVERONLY;

bool gbLimitCommandLineCommands = false;

bool gbPrintCommandLine = true;
// Disables printing of the command line as it is parsed
AUTO_CMD_INT(gbPrintCommandLine, printCommandLine) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;



//table of commands which are "banned", that is, can not be executed
StashTable sBannedCommands = NULL;

void cmdSetBannedCommands(char ***pppBannedCommands)
{
	int i;

	if (sBannedCommands)
	{
		stashTableClear(sBannedCommands);
	}
	else
	{
		sBannedCommands =  stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	}

	for (i=0; i < eaSize(pppBannedCommands); i++)
	{
		stashAddPointer(sBannedCommands, (*pppBannedCommands)[i], NULL, true);
	}
}

AUTO_COMMAND ACMD_COMMANDLINE ACMD_NAME(BanCommand);
void cmdBanCommand(const char *pCommand)
{
	if (!sBannedCommands)
	{
		sBannedCommands =  stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	}

	stashAddPointer(sBannedCommands, pCommand, NULL, true);
}



void cmdParsePrintCommandLine(bool should_print)
{
	gbPrintCommandLine = should_print;
}

void MultiValWriteInt(MultiVal *val, void *dest, int size)
{
	assertmsg(val->type == MULTI_INT,"MultiValWriteInt called on wrong type");
	switch (size)
	{
		xcase sizeof(S8):	*((S8 *)dest)  = (S8)val->intval;
		xcase sizeof(S16):	*((S16 *)dest) = (S16)val->intval;
		xcase sizeof(S32):	*((S32 *)dest) = (S32)val->intval;
		xcase sizeof(S64):	*((S64 *)dest) = (S64)val->intval;
		xdefault: assertmsg(0,"MultiValWriteInt called with unsupported size!");
	}
}

void MultiValWriteFloat(MultiVal *val, void *dest, int size)
{
	assertmsg(val->type == MULTI_FLOAT,"MultiValWriteFloat called on wrong type");
	switch (size)
	{
		xcase sizeof(F32):	*((F32 *)dest) = (F32)val->floatval;
		xcase sizeof(F64):	*((F64 *)dest) = (F64)val->floatval;
		xdefault: assertmsg(0,"MultiValWriteFloat called with unsupported size!");
	}
}

void MultiValReadInt(MultiVal *val, void *dest, int size)
{
	assertmsg(val->type == MULTI_INT,"MultiValReadInt called on wrong type");
	switch (size)
	{
		xcase sizeof(S8):	val->intval = *((S8 *)dest) ;
		xcase sizeof(S16):	val->intval = *((S16 *)dest);
		xcase sizeof(S32):	val->intval = *((S32 *)dest);
		xcase sizeof(S64):	val->intval = *((S64 *)dest);
		xdefault: assertmsg(0,"MultiValReadInt called with unsupported size!");
	}
}

void MultiValReadFloat(MultiVal *val, void *dest, int size)
{
	assertmsg(val->type == MULTI_FLOAT,"MultiValReadFloat called on wrong type");
	switch (size)
	{
		xcase sizeof(F32):	val->floatval = *((F32 *)dest);
		xcase sizeof(F64):	val->floatval = *((F64 *)dest);
		xdefault: assertmsg(0,"MultiValReadFloat called with unsupported size!");
	}
}

// Debug: Check if we've been running non-accesslevel 0 (NALZ) commands
static bool g_hasRanNALZCmd=false;
static char *g_NALZCmd;
static int g_NALZAccessLevel;
static AccessLevelCmdCallback accessLevelCmdCallback=NULL;

void cmdParserSetAccessLevelCmdCallback(AccessLevelCmdCallback callback)
{
	accessLevelCmdCallback = callback;
}

const char *cmdParseHasRanAccessLevelCmd(bool resetFlag, AccessLevel *accessLevel)
{
	if (g_hasRanNALZCmd) {
		if (resetFlag)
			g_hasRanNALZCmd = false;
		if (accessLevel)
			*accessLevel = g_NALZAccessLevel;
		return g_NALZCmd;
	} else {
		return NULL;
	}
}

void cmdParseNotifyRanAccessLevelCmd(const char *cmdstr, AccessLevel accessLevel)
{
	//assert(cmdstr && stricmp(cmdstr, "(null)")!=0);
	g_hasRanNALZCmd = true;
	SAFE_FREE(g_NALZCmd);
	g_NALZCmd = strdup(cmdstr);
	g_NALZAccessLevel = accessLevel;
	if (accessLevelCmdCallback)
		accessLevelCmdCallback(cmdstr, accessLevel);
}

// This function doesn't handle every possible size correctly
void cmdPrintCommmandString(Cmd *cmd,char *buf,int buf_size)
{
	char	buf2[1000];
	int		j;
	void	*ptr;
	MultiVal temp={0};

	sprintf_s(SAFESTR2(buf), "%s ",cmd->name);
	for(j=0;j < cmd->iNumLogicalArgs ;j++)
	{
		buf2[0] = 0;
		ptr = cmd->data[j].ptr;
		temp.type = cmd->data[j].type;
		switch(cmd->data[j].type)
		{
		case MULTI_INT:
			MultiValReadInt(&temp,ptr,cmd->data[j].data_size);
			sprintf(buf2," %"FORM_LL"d",temp.intval);
		xcase MULTI_FLOAT:
			sprintf(buf2," %f",*((F32 *)ptr));
		xcase MULTI_STRING:
			sprintf(buf2," \"%s\"",(char *)ptr);
		xdefault:
			continue;
		}
		strcat_s(buf,buf_size,buf2);
	}
}

void cmdPrintUsage(Cmd *cmd, char *buf, int buf_size)
{
	char buf2[1000];
	int j;

	sprintf_s(SAFESTR2(buf), "Usage: %s",cmd->name);
	for(j=0; j < cmd->iNumLogicalArgs; j++)
	{
		buf2[0] = 0;
		switch(cmd->data[j].type)
		{
		case MULTI_INT:
			sprintf(buf2," <int:%s>", cmd->data[j].pArgName);
		xcase MULTI_FLOAT:
			sprintf(buf2," <float:%s>", cmd->data[j].pArgName);
		xcase MULTI_STRING:
			sprintf(buf2," <string:%s>", cmd->data[j].pArgName);
		xcase MULTI_VEC3:
			sprintf(buf2," <vec3:%s>", cmd->data[j].pArgName);
		xcase MULTI_VEC4:
			sprintf(buf2," <vec4:%s>", cmd->data[j].pArgName);
		xcase MULTI_QUAT:
			sprintf(buf2," <quat:%s>", cmd->data[j].pArgName);
		xcase MULTI_MAT4:
			sprintf(buf2," <mat4:%s>", cmd->data[j].pArgName);
		xcase MULTI_NP_POINTER:
			sprintf(buf2," <tpblock:%s>", cmd->data[j].pArgName);
		xdefault:
			continue;
		}
		strcat_s(buf,buf_size,buf2);
	}
	if (cmd->comment && cmd->comment[0])
	{
		buf2[0] = 0;
		sprintf(buf2, "\n%s", cmd->comment);
		strcat_s(buf, buf_size, buf2);
	}
}

#define MAXNAME 128

int cmdListAddToHashes(CmdList *cmdlist, Cmd *cmd)
{
	const char *name;

	if (!cmdlist->sCmdsByName)
	{
		cmdlist->sCmdsByName = stashTableCreateWithStringKeys(8, StashDefault); // All names should be static strings, no deep copies!
	}

	if (cmdlist->bInternalList)
	{
		name = cmd->name;
	}
	else
	{
		if (strchr(cmd->name, '_')) {
			// Need to make a new string
			name = allocAddString(stripUnderscores(cmd->name));
		} else {
			name = cmd->name;
		}
	}

	if (isDevelopmentMode())
	{
		assertmsgf(!stashFindPointer(cmdlist->sCmdsByName,name,NULL),
			"Duplicate command named %s found in Command List!",cmd->name);
	}
	stashAddPointer(cmdlist->sCmdsByName,name,cmd, true);
	return 1;
}

int cmdFilterAppendCategories(const char *filter, char ***eArrayOutput)
{
	char *tempFilter = NULL;
	char *cat;
	char *context = NULL;
	char context_char = 0;
	char *str;

	estrStackCreate(&tempFilter);
	estrCopy2(&tempFilter, filter);

	str = tempFilter;
	while (cat = strtok_nondestructive(str, " ", &context, &context_char))
	{
		char *temp = NULL;
		estrPrintf(&temp, " %s ", cat);
		eaPush(eArrayOutput, temp);

		str = NULL;
	}

	estrDestroy(&tempFilter);
	return 1;
}

Cmd *cmdListFind(CmdList *cmdlist, const char *fullname)
{
	Cmd		*cmd;

	if (!fullname)
		return 0;

 	if (!cmdlist->bInternalList)
 	{
		bool bFound;

		if (strchr(fullname, '_'))
		{
			char *pFixedName = NULL;
			estrStackCreate(&pFixedName);
			stripUnderscoresSafe(fullname, &pFixedName);
			bFound = stashFindPointer(cmdlist->sCmdsByName,pFixedName,&cmd);
			estrDestroy(&pFixedName);
		}
		else
		{
			bFound = stashFindPointer(cmdlist->sCmdsByName, fullname, &cmd);
		}

		if (!bFound)
		{
			return NULL;
		}
	}
	else
	{
		if(!stashFindPointer(cmdlist->sCmdsByName,fullname,&cmd)) 
		{
			return NULL;
		}
	}

	return cmd;
}

Cmd *cmdListFindWithContext(CmdList *cmdlist, const char *fullname, CmdContext *context)
{
	Cmd *pCmd = cmdListFind(cmdlist, fullname);

	if (pCmd)
	{
		if (pCmd->access_level > context->access_level)
		{
			return NULL;
		}

		if (context->flags & CMD_CONTEXT_FLAG_COMMAND_LINE_CMDS_ONLY
			&& !(pCmd->flags & CMDF_COMMANDLINE))
		{
			return NULL;
		}

		if (context->flags & CMD_CONTEXT_FLAG_NO_EARLY_COMMAND_LINE
			&& pCmd->flags & CMDF_EARLYCOMMANDLINE)
		{
			return NULL;
		}

		if(context->categories && (context->access_level < 9))
		{
			if(!pCmd->categories)
				return NULL;
			else
			{
				bool bMatchedOneCategory = false;
				EARRAY_FOREACH_BEGIN(context->categories, i);
				{
					char *category = context->categories[i];
					if(strstri(pCmd->categories, category))
					{
						bMatchedOneCategory = true;
						break;
					}
				}
				EARRAY_FOREACH_END;

				if(!bMatchedOneCategory)
					return NULL;
			}
		}
	}


	return pCmd;
}



Cmd *cmdListFindTranslated(CmdList *cmdlist, const char *fullname, Language eLang)
{
	Cmd *pCmd;
	if (eLang < 0 || eLang > ARRAY_SIZE_CHECKED(cmdlist->sCmdsByName_Translated))
		return NULL;
	if (stashFindPointer(cmdlist->sCmdsByName_Translated[eLang], stripUnderscores(fullname), &pCmd))
		return pCmd;
	return NULL;
}

int cmdGetAccessLevelFromFullString(CmdList *pList, const char *pCmdString)
{
	char *pTempString = NULL;
	Cmd *pCmd;

	estrStackCreate(&pTempString);
	estrCopy2(&pTempString, pCmdString);
	estrTruncateAtFirstOccurrence(&pTempString, ' ');

	pCmd = cmdListFind(pList, pTempString);
	estrDestroy(&pTempString);

	if (pCmd)
	{
		return pCmd->access_level;
	}
	
	// an unknown command should be considered very high access level, not
	// very low, if anything is actually trying to check it.
	return 9000;
}


bool cmdIsCommandIn(const char *pchCommandString, const char *pchCommandName)
{
	char *pchCmd = NULL, *pchCmdStart = NULL;
	bool bIsIn = false;
	size_t szLength;
	
	PERFINFO_AUTO_START_FUNC();

	szLength = strlen(pchCommandName);
	
	estrStackCreate(&pchCmdStart);
	estrCopy2(&pchCmdStart, pchCommandString);
	pchCmd = pchCmdStart;

	do
	{
		unsigned char *pchTmpCmd = cmdReadNextLine(&pchCmd);
		while (pchTmpCmd && characterIsSpace(*pchTmpCmd))
			pchTmpCmd++;
		if (pchTmpCmd && *pchTmpCmd)
		{
			// MJF Mar/2/2013 -- Skip the special punctuation that
			// implies parameters for a command name.  I know of +,
			// ++, -, and --.  This handles all of those.
			while(*pchTmpCmd == '+' || *pchTmpCmd == '-')
			{
				++pchTmpCmd;
			}

			if(strStartsWith(pchTmpCmd, pchCommandName))
			{
				char potentialCommandEnd = pchTmpCmd[szLength];
				if(characterIsSpace(potentialCommandEnd) || potentialCommandEnd == '\0')
				{
					// This is the asked about command, space delmited.
					bIsIn = true;
				}
			}
		}
	} while (pchCmd && *pchCmd && !bIsIn);
	estrDestroy(&pchCmdStart);

	PERFINFO_AUTO_STOP();

	return bIsIn;
}

void DEFAULT_LATELINK_GetObjInfoForCmdParseLogging(CmdContext *pContext, GlobalType *pOutType, ContainerID *pOutID, 
	const char **ppOutObjName /*NOT AN ESTRING*/, const char **ppOutOwnerString, const char **ppProjSpecificString /*NOT AN ESTRING*/)
{
	if (pContext->pAuthNameAndIP)
	{
		*ppOutOwnerString = pContext->pAuthNameAndIP;
	}
}

void AddCmdArgDescriptions(char **ppOutEStr, Cmd *pCmd)
{
	int j;

	for(j=0;j<pCmd->iNumLogicalArgs;j++)
	{
		switch(pCmd->data[j].type)
		{
			case MULTI_INT:
				estrAppend2(ppOutEStr," <int>");
			xcase MULTI_FLOAT:
				estrAppend2(ppOutEStr," <float>");
			xcase MULTI_STRING:
				estrAppend2(ppOutEStr," <string>");
			xcase MULTI_VEC3:
				estrAppend2(ppOutEStr," <vec3>");
			xcase MULTI_VEC4:
				estrAppend2(ppOutEStr," <vec4>");
			xcase MULTI_QUAT:
				estrAppend2(ppOutEStr," <quat>");
			xcase MULTI_MAT4:
				estrAppend2(ppOutEStr," <mat4>");
			xcase MULTI_NP_POINTER:
				estrAppend2(ppOutEStr," <tpblock>");
		}

		if (isDevelopmentMode())
		{
			if (pCmd->data[j].pArgName && pCmd->data[j].pArgName[0])
			{
				estrConcatf(ppOutEStr, "(%s)", pCmd->data[j].pArgName);
			}
		}
	}
}

bool cmdCheckMultiValArgs(Cmd *cmd, MultiVal ***pppMultiValArgs)
{
	int i;

	if (cmd->iNumLogicalArgs != eaSize(pppMultiValArgs))
	{
		return false;
	}

	for (i=0; i < eaSize(pppMultiValArgs); i++)
	{
		if (cmd->data[i].type != MULTI_GET_TYPE((*pppMultiValArgs)[i]->type))
		{
			return false;
		}
	}

	return true;
}

bool cmdBanned(char *pCmdName)
{
	if (sBannedCommands && stashFindPointer(sBannedCommands, pCmdName, NULL))
	{
		return true;
	}

	return false;
}

__forceinline static void cmdParseSetUnknownCommand(CmdContext *cmd_context, const char *cmdname)
{
	cmd_context->cmd_unknown = true;
	if (!gbFolderCacheModeChosen || cmd_context->eHowCalled == CMD_CONTEXT_HOWCALLED_COMMANDLINE || 
		cmd_context->eHowCalled == CMD_CONTEXT_HOWCALLED_EARLY_COMMANDLINE || cmd_context->eHowCalled == CMD_CONTEXT_HOWCALLED_TRANSACTION)
	{
		estrPrintf(cmd_context->output_msg, "Unknown command \"%s\".", cmdname);
	} else {
		FormatMessageKeyDefault(cmd_context->output_msg, "CmdParse_UnknownCommand", "Unknown Command: {Command}",
			STRFMT_STRING("Command", cmdname), STRFMT_END);
	}
}

Cmd *cmdReadInternal(CmdList *cmd_list, const char *cmdstr,CmdContext* cmd_context, bool syntax_check,
	bool bUseMultivalArgs, MultiVal ***pppMultiValArgs)
{
	static char *startdelim_quoted = " \n,\t",
			*enddelim_quoted = " \n,\t",
			*startdelim_sentence = " \n",
			*enddelim_sentence = "\n";
	char	*argv[128],*s, *tokbuf, *cmdname;
	int		j=0,argc=0,toggle=0,idx=1,k,invert=0,subarg=0, i;
	void	*ptr;
	F32		*fptr;
	Cmd		*cmd;
	char    *buf;

	int access_level;
	size_t iCmdStringLength;

	PERFINFO_AUTO_START_FUNC();




	if (cmd_context)
	{
		cmd_context->op = CMD_OPERATION_NONE;
		access_level = cmd_context->access_level;
		cmd_context->commandString = cmdstr;
	}
	else
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	if (gDontCheckAccessLevelForCommands)
	{
		access_level = cmd_context->access_level = 12;
	}

	iCmdStringLength = strlen(cmdstr);
    buf = cmdParseAlloc((int)(iCmdStringLength + 1));
	memcpy(buf, cmdstr, iCmdStringLength + 1);

	s = strtok_quoted_r(buf,startdelim_quoted, enddelim_quoted,&tokbuf);
	if (!s)
	{
		cmdParseFree(&buf);
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}
	cmdname = s;

	//ABW commands ending in % currently crash in cmdTranslated
	if (strchr(cmdname, '%'))
	{
		cmdParseFree(&buf);
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}

	if (!cmd_list->bInternalList)
	{	
		if (cmdname[0] == '+' || cmdname[0] == '-')
		{
			if (cmdname[1] == '+')
				toggle = 1;

			argv[argc++] = (cmdname[0] == '+') ? "1" : "0";
			j++;
			cmdname += 1 + toggle;
		}
		else if(cmdname[0] == '&')
		{
			cmd_context->op = CMD_OPERATION_AND;
			cmdname += 1;
		}
		else if(cmdname[0] == '|')
		{
			cmd_context->op = CMD_OPERATION_OR;
			cmdname += 1;
		}
	}



	cmd = cmdListFind(cmd_list,cmdname);
	if (!cmd)
		cmd = cmdListFindTranslated(cmd_list, cmdname, cmd_context->language);


	if (!cmd || cmd->access_level > access_level)
	{
		cmdParseSetUnknownCommand(cmd_context, cmdname);
		cmdParseFree(&buf);
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}

	if (cmdBanned(cmdname))
	{
		cmd_context->banned_cmd = 1;
		estrPrintf(cmd_context->output_msg,"Banned command %s",cmdname);
		cmdParseFree(&buf);
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}

	if (cmd_context->flags & CMD_CONTEXT_FLAG_NO_EARLY_COMMAND_LINE)
	{
		if (cmd->flags & CMDF_EARLYCOMMANDLINE)
		{
			estrPrintf(cmd_context->output_msg,"Not re-running early command line cmd %s",cmdname);
			cmdParseFree(&buf);
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
	}


	if (cmd_context->flags & CMD_CONTEXT_FLAG_COMMAND_LINE_CMDS_ONLY)
	{
		if (!(cmd->flags & CMDF_COMMANDLINE))
		{
			Errorf("Trying to execute non-command-line command <<%s>> on the command line", cmdname);
			estrPrintf(cmd_context->output_msg,"Trying to execute non-command-line command <<%s>> on the command line", cmdname);
			cmdParseFree(&buf);
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
	}

	// Don't let access level 0 commandline commands be run through other methods (eg. chat window, tilde window)
	if (cmd_context->access_level == 0 && cmd->flags & CMDF_COMMANDLINEONLY)
	{
		if (cmd_context->eHowCalled != CMD_CONTEXT_HOWCALLED_COMMANDLINE && cmd_context->eHowCalled != CMD_CONTEXT_HOWCALLED_EARLY_COMMANDLINE)
		{
			cmdParseSetUnknownCommand(cmd_context, cmdname);
			cmdParseFree(&buf);
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
	}

	if (!cmd_list->bInternalList && cmd->access_level > 0) {
		cmdParseNotifyRanAccessLevelCmd(cmdstr, cmd->access_level);
	}
	
	if (cmd->flags & CMDF_AUTOSETTING_NONCONTROLLER)
	{
		if (!(cmd_context->eHowCalled > CMD_CONTEXT_HOWCALLED_FIRST_AUTOSETTING
			&& cmd_context->eHowCalled < CMD_CONTEXT_HOWCALLED_LAST_AUTOSETTING))
		{
			AutoSetting_CmdWasCalled(cmd, cmd_context->eHowCalled, cmdstr);
			cmdParseFree(&buf);
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
	}

	cmd_context->found_cmd = cmd;
	cmd_context->return_val.type = cmd->return_type.type;
	

	if (bUseMultivalArgs)
	{
		if (!cmdCheckMultiValArgs(cmd, pppMultiValArgs))
		{
			cmd_context->good_args = false;

			cmdParseFree(&buf);
			PERFINFO_AUTO_STOP_FUNC();
			return cmd;		
		}
		else
		{
			cmd_context->good_args = true;
			for (i=0; i < eaSize(pppMultiValArgs); i++)
			{
				memcpy(&cmd_context->args[i], (*pppMultiValArgs)[i], sizeof(MultiVal));
			}
		}
	}
	else
	{
		for(;;)
		{
			if (j > cmd->iNumReadArgs)
			{
				cmd_context->good_args = false;
				break;
			}
			if (j < cmd->iNumReadArgs && cmd->data[argc].flags & CMDAF_SENTENCE)
			{
				int len;
				if (cmd_context->multi_line)
				{
					//optimization... in the case where we are reading the entire rest of the line, calling strtok_delims_r is
					//wasteful. It does two things: (1) eat up all the spaces at the beginning of the token (still useful), and
					//(2) search through the entire rest of the string looking for the NULL-terminator. This is wasteful
					//because we have already calculated the entire length of the line (not to mention that the way its done
					//in strtok_delims_r is to call strcspn even when then delims are "", which is much than just calling strlen).
					//So, we replace this:
					//  s=strtok_delims_r(0,startdelim_sentence,"",&tokbuf);
					//
					//with this:
					
					s = tokbuf + strspn(tokbuf, startdelim_sentence);
					tokbuf = buf + iCmdStringLength;
					
					//here's the version I ran temporarily to make sure the optimized version always resulted in the
					//same output
					/*
					char *tokbuf_copy = tokbuf;
					char *s2;
					
					s=strtok_delims_r(0,startdelim_sentence,"",&tokbuf);

					s2 = tokbuf_copy + strspn(tokbuf_copy, startdelim_sentence);
					tokbuf_copy = buf + iCmdStringLength;

					assert(s2 == s && tokbuf_copy == tokbuf);
					*/
					


				}
				else
				{			
					s=strtok_delims_r(0,startdelim_sentence,enddelim_sentence,&tokbuf);
				}
				if (s && s[0] == '"')
				{
					len = (int)strlen(s);
					while (s[len - 1] == ' ')
					{
						// ignore trailing whitespace
						len--;
					}
					if (s[len - 1] == '"')
					{
						// chop off the outer quotes
						s[len-1] = '\0';
						s++;
					}
				}
			}
			else if ((j < cmd->iNumReadArgs && cmd->data[argc].flags & CMDAF_TEXTPARSER) && !cmdContextHasStructList(cmd_context))
			{
				char *tempstr;
				s = tokbuf;
				tempstr = strstr(s,"&>");
				if (!tempstr)
				{
					s = NULL;
				}
				else
				{
					tempstr++;tempstr++;
					if (*tempstr == 0)
					{
						tokbuf = NULL;
					}
					else
					{
						*tempstr = 0;
						tokbuf = tempstr+1;
					}
				}
			}
			else
			{
				s=strtok_quoted_r(0,startdelim_quoted,enddelim_quoted,&tokbuf);
			}
			
			if (!s)
				break;
			if (!cmd_list->bInternalList && strcmp(s,"=") == 0)
				continue;
			argv[j++] = s;
			subarg++;

			if (cmd->data[argc].type == MULTI_MAT4)
			{
				if (subarg >= 12)
				{
					subarg = 0;
					argc++;
				}
			}
			else if (cmd->data[argc].type == MULTI_VEC3)
			{
				if (subarg >= 3)
				{
					subarg = 0;
					argc++;
				}
			}
			else if (cmd->data[argc].type == MULTI_VEC4 || cmd->data[argc].type == MULTI_QUAT)
			{
				if (subarg >= 4)
				{
					subarg = 0;
					argc++;
				}
			}
			else
			{
				subarg = 0;
				argc++;
			}
		}

		//special case... on the command line, anything which expects one integer arg and is called with zero args gets an
		//implicit arg of "1"
		if (j == 0 && cmd->iNumReadArgs == 1 
			&& (cmd_context->eHowCalled == CMD_CONTEXT_HOWCALLED_COMMANDLINE || cmd_context->eHowCalled == CMD_CONTEXT_HOWCALLED_EARLY_COMMANDLINE))
		{
			argv[j++] = "1";
		}

		if (j != cmd->iNumReadArgs)
		{
			cmd_context->good_args = false;
			if (j == 0 && (cmd->flags & CMDF_PRINTVARS))
			{
				if (cmd->data[0].ptr && cmd->data[0].type != MULTI_NP_POINTER)
				{
					char printBuffer[10000];
					char *pFirstSpace;
					// Print out the parameter, if it's the right kind of command
					cmdPrintCommmandString(cmd,SAFESTR(printBuffer));
					

					if ((cmd_context->flags & CMD_CONTEXT_FLAG_TREAT_PRINTVARS_AS_SUCCESS) 
						&& (pFirstSpace = strchr(printBuffer, ' ')) )
					{
						estrCopy2(cmd_context->output_msg,pFirstSpace);
					}
					else
					{
						estrCopy2(cmd_context->output_msg,printBuffer);
					}

					if (cmd_context->flags & CMD_CONTEXT_FLAG_TREAT_PRINTVARS_AS_SUCCESS)
					{
						cmd_context->good_args = true;
					}

				}
			}
			else if (cmd_context->output_msg && cmd->name[0] && !cmd->error_handler)
			{
				const char *pchMessage = TranslateMessageKeyDefault("CmdParse_IncorrectUsage", "\"{Command}\" takes {ArgCount} argument(s).");
				estrClear(cmd_context->output_msg);
				strfmt_FromArgs(cmd_context->output_msg, pchMessage,
					STRFMT_STRING("Command", cmd->name),
					STRFMT_INT("ArgCount", cmd->iNumReadArgs),
					//STRFMT_INT("GivenCount", argc - 1), // argc is not the given count, it's just the number parsed (e.g. always == cmd->num_args+1 if you pass too many)
					STRFMT_STRING("Comment", cmd->comment),
					STRFMT_END);
				estrAppend2(cmd_context->output_msg,"\n");
				estrAppend2(cmd_context->output_msg,cmdname);

				AddCmdArgDescriptions(cmd_context->output_msg, cmd);



			}		
			if (cmd->error_handler && !syntax_check)
			{
				// If there's a function pointer handler, run it now
				cmd->error_handler(cmd,cmd_context);
			}
			cmdParseFree(&buf);
			PERFINFO_AUTO_STOP_FUNC();
			return cmd;		
		}
		else
		{
			cmd_context->good_args = true;
		}

		if (syntax_check)
		{
			cmdParseFree(&buf);
			PERFINFO_AUTO_STOP_FUNC();
			return cmd;
		}
		for(j=0,idx=0;idx<cmd->iNumReadArgs;j++)
		{
			ptr = cmd->data[j].ptr;
			cmd_context->args[j].type = cmd->data[j].type;
			switch(cmd->data[j].type)
			{
				case MULTI_INT:
					if (toggle)
					{
						if (ptr) {
							MultiValReadInt(&cmd_context->args[j],ptr,cmd->data[j].data_size);

							cmd_context->args[j].intval++;

							if (cmd->data[j].max_value == 0)
							{
								if (cmd_context->args[j].intval > 1)
								{
									cmd_context->args[j].intval = 0;
								}
							}
							else if (cmd_context->args[j].intval > cmd->data[j].max_value)
							{
								cmd_context->args[j].intval = 0;
							}

							MultiValWriteInt(&cmd_context->args[j],ptr,cmd->data[j].data_size);
						}
					}
					else
					{
						char* str = argv[idx];
						S64 value;
						
						if(!cmd_list->bInternalList && str[0] == '!'){
							str++;
							invert = 1;
						}
							
						if(strStartsWith(str, "0x")){
							str += 2;
							value = 0;
							while(str[0]){
								char c = tolower(str[0]);
								value <<= 4;
								if(c >= '0' && c <= '9'){
									value += c - '0';
								}
								else if(c >= 'a' && c <= 'f'){
									value += 10 + c - 'a';
								}
								else{
									value = 0;
									break;
								}
								str++;
							}
						}else{
							value = atoi64(str);
						}

						if(invert)
							value = ~value;
							
						cmd_context->args[j].intval = value;

						if (ptr)
						{					
							MultiValWriteInt(&cmd_context->args[j],ptr,cmd->data[j].data_size);
						}
					}
					idx++;
				xcase MULTI_FLOAT:
					if (toggle)
					{
						if (ptr) {
							MultiValReadFloat(&cmd_context->args[j],ptr,cmd->data[j].data_size);

							cmd_context->args[j].floatval += 1.0;

							if (cmd->data[j].max_value == 0)
							{
								if (cmd_context->args[j].floatval > 1.0)
								{
									cmd_context->args[j].floatval = 0.0;
								}
							}
							else if (cmd_context->args[j].floatval > cmd->data[j].max_value)
							{
								cmd_context->args[j].floatval = 0;
							}
							MultiValWriteFloat(&cmd_context->args[j],ptr,cmd->data[j].data_size);
						}
					}
					else
					{
						char* str = argv[idx];
						F64 value;

						if(!cmd_list->bInternalList && str[0] == '!'){
							str++;
							invert = 1;
						}

						value = opt_atof(str);

						cmd_context->args[j].floatval = value;

						if (ptr)
						{					
							MultiValWriteFloat(&cmd_context->args[j],ptr,cmd->data[j].data_size);
						}
					}
					idx++;
				xcase MULTI_STRING:
					{
						char *str = (char *)ptr;
						if (!str)
						{
							if (!(cmd->data[j].flags & CMDAF_ESCAPEDSTRING) && !(cmd_context->flags & CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED))
							{
                                int iLen = (int)strlen(argv[idx]);
								int iAllocSize = MAX(iLen + 1, cmd->data[j].data_size);
								str = cmdParseAlloc(iAllocSize);
								memcpy(str, argv[idx], iLen + 1);
							}
							else
							{
                                int iLen = (int)strlen(argv[idx]);
								int iAllocSize = MAX(iLen + 1, cmd->data[j].data_size);
								size_t iWritten;

								str = cmdParseAlloc(iAllocSize);

								iWritten = unescapeDataStatic(argv[idx], iLen, str, iAllocSize, true);
								str[iWritten] = 0;
							}
						}
						else
						{
							if (cmd->data[j].data_size == MAGIC_CMDPARSE_STRING_SIZE_ESTRING)
							{
								//cmd->data[j].ptr should already be a **
								estrCopy2((char**)cmd->data[j].ptr, argv[idx]);
							}
							else
							{
								strncpy_s(str,cmd->data[j].data_size, argv[idx], _TRUNCATE);
								str[cmd->data[j].data_size-1] = 0;
							}

						}
						

						cmd_context->args[j].str = str;
						idx++;
					}
				xcase MULTI_NP_POINTER:			
					{	
						void	*struct_ptr;
						char *argtemp = argv[idx];

						if (!ptr) // we need a TextParser def
						{
							idx++;
							break;
						}
						
						while (IS_WHITESPACE(*argtemp)) 
						{ 
							argtemp++; 
						} 

						if (stricmp(argtemp, "<& __NULL__ &>") == 0)
						{
							cmd_context->args[j].ptr = NULL;
						}
						else
						{
							if (cmdContextHasStructList(cmd_context))
							{
								CmdParseStructListEntry *pEntry;

								if (!strStartsWith(argtemp, "STRUCT("))
								{
									cmd_context->good_args = false;
									cmd_context->args[j].ptr = NULL;
									if (cmd_context->output_msg)
									{
										estrPrintf(cmd_context->output_msg, "Bad STRUCT( syntax");
									}
								}
								else
								{
									int iStructIndex = atoi(argtemp + 7);
									if (iStructIndex < 0 || iStructIndex >= eaSize(&cmd_context->pStructList->ppEntries) || 
										!(pEntry = cmd_context->pStructList->ppEntries[iStructIndex])
										|| !pEntry->pTPI
										|| pEntry->pTPI != cmd->data[j].ptr)
									{
										cmd_context->good_args = false;
										cmd_context->args[j].ptr = NULL;
										if (cmd_context->output_msg)
										{
											estrPrintf(cmd_context->output_msg, "Missing struct in STRUCT(foo)");
										}
									}
									else
									{
										cmd_context->args[j].ptr = pEntry->pStruct;
									}
								}
							}
							else
							{
								char *pTempComment = NULL;
								estrStackCreate(&pTempComment);

								estrPrintf(&pTempComment, "Parsing %s while cmdparsing cmd %s",
									ParserGetTableName(cmd->data[j].ptr), cmd->name);

								struct_ptr = StructCreateVoid(cmd->data[j].ptr);
							

								if (cmd_context->flags & CMD_CONTEXT_FLAG_IGNORE_UNKNOWN_FIELDS)
									ParserReadTextEscapedWithComment(&argtemp,cmd->data[j].ptr,struct_ptr, PARSER_PARSECURRENTFILE | PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE, pTempComment);
								else
									ParserReadTextEscapedWithComment(&argtemp,cmd->data[j].ptr,struct_ptr, PARSER_PARSECURRENTFILE, pTempComment);
						
								cmd_context->args[j].ptr = struct_ptr;
								estrDestroy(&pTempComment);
							}
						}
						idx++;
						
					}
				xcase MULTI_VEC3:
					{	
						if (stricmp(argv[idx], "N") == 0)
						{
							idx+= 3;

							SAFE_FREE(cmd_context->args[j].ptr_noconst);
							break;
						}



						fptr = ptr;
						if (!fptr)
						{
							fptr = _alloca(sizeof(F32)*3);
						}
						for(k=0;k<3;k++)
							fptr[k] = (F32)opt_atof(argv[idx++]);
						cmd_context->args[j].ptr = fptr;
					}
				xcase MULTI_VEC4:
					{			
						if (stricmp(argv[idx], "N") == 0)
						{
							idx+= 4;

							SAFE_FREE(cmd_context->args[j].ptr_noconst);
							break;
						}

						fptr = ptr;
						if (!fptr)
						{
							fptr = _alloca(sizeof(F32)*4);
						}
						for(k=0;k<4;k++)
							fptr[k] = (F32)opt_atof(argv[idx++]);
						cmd_context->args[j].ptr = fptr;
					}
				xcase MULTI_QUAT:
					{				
						fptr = ptr;
						if (!fptr)
						{
							fptr = _alloca(sizeof(F32)*4);
						}
						for(k=0;k<4;k++)
							fptr[k] = (F32)opt_atof(argv[idx++]);
						cmd_context->args[j].ptr = fptr;
					}
				xcase MULTI_MAT4:
					{				
						fptr = ptr;
						if (!fptr)
						{
							fptr = _alloca(sizeof(F32)*12 ); 
						}
						for(k=0;k<3;k++)
							fptr[k+9] = (F32)opt_atof(argv[idx++]);
						for(k=0;k<9;k++)
							fptr[k] = (F32)opt_atof(argv[idx++]);
						cmd_context->args[j].ptr = fptr;
					}
			}
		}
	}

	if (((cmd_context->flags & CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL) || gbLogAllAccessLevelCommands)
		&& cmd->access_level > 0
		&& cmd->access_level > cmdGetMinAccessLevelWhichForcesLogging())
	{
		cmd_context->slowReturnInfo.bForceLogReturn = true;
	}

	if (!cmd_context->good_args)
	{
		if (cmd->error_handler && !syntax_check)
		{
			// If there's a function pointer handler, run it now
			cmd->error_handler(cmd,cmd_context);
		}

		cmdCleanup(cmd,cmd_context);

		cmdParseFree(&buf);
		PERFINFO_AUTO_STOP_FUNC();
		return cmd;		
	}

	if (cmd->handler)
	{
		PERFINFO_AUTO_START_STATIC(cmd->name, &cmd->perfInfo, 1);
		// If this table has a global handler, run it now
		cmd->handler(cmd,cmd_context);
		PERFINFO_AUTO_STOP_CHECKED(cmd->name);
	}

	if (cmd->flags & CMDF_CONTROLLERAUTOSETTING)
	{
		if (!(cmd_context->eHowCalled > CMD_CONTEXT_HOWCALLED_FIRST_AUTOSETTING
			&& cmd_context->eHowCalled < CMD_CONTEXT_HOWCALLED_LAST_AUTOSETTING))
		{
			ControllerAutoSetting_CmdWasCalled(cmd->name, cmd_context->eHowCalled);
		}
	}
	
	if (((cmd_context->flags & CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL) || gbLogAllAccessLevelCommands)
		&& cmd->access_level > 0
		&& cmd->access_level > cmdGetMinAccessLevelWhichForcesLogging())
	{
		GlobalType eTypeToLog = GLOBALTYPE_NONE;
		ContainerID eIDToLog = 0;
		char *pObjName = NULL;
		char *pRetString = NULL;
		char *pOwnerString = NULL;
		char *pProjSpecificString = NULL;
		
		char *pMultiValArgsString = NULL;

		//only log multival arguments if gbLogAllAccessLevelCommands is set (mainly to avoid logging enormous structs,
		//we may want to change this at some point if it turns out we're missing key information)
		if (gbLogAllAccessLevelCommands && bUseMultivalArgs)
		{
			estrStackCreate(&pMultiValArgsString);

			FOR_EACH_IN_EARRAY_FORWARDS((*pppMultiValArgs), MultiVal, pMV)
			{
				estrConcatChar(&pMultiValArgsString, ' ');
				MultiValToEString(pMV, &pMultiValArgsString);
			}
			FOR_EACH_END;
		}

		GetObjInfoForCmdParseLogging(cmd_context, &eTypeToLog, &eIDToLog, &pObjName, &pOwnerString, &pProjSpecificString);
		estrStackCreate(&pRetString);

		if (cmd_context->slowReturnInfo.bDoingSlowReturn)
		{
			estrPrintf(&pRetString, "Slow return. ID: %d:%d:%u",
				cmd_context->slowReturnInfo.iClientID, 
				cmd_context->slowReturnInfo.iCommandRequestID, cmd_context->slowReturnInfo.iMCPID);
		}
		else
		{
			MultiValToEString(&cmd_context->return_val, &pRetString);
		}
		objLog(LOG_COMMANDS, eTypeToLog, eIDToLog, 0, pObjName, NULL, pOwnerString, "AccessLevelCommand", pProjSpecificString, "Access Level %d command run (%s). Cmd string: \"%s%s\" Result String: \"%s\"",
			cmd->access_level, GetContextHowString(cmd_context), 
			cmdstr, pMultiValArgsString ? pMultiValArgsString : "", pRetString);
		estrDestroy(&pRetString);
		estrDestroy(&pMultiValArgsString);

	}

	//when we are using multival args, the multivals in cmd context are just shallow copies of the ones that were
	//passed in, so make sure we don't try to free them
	if (bUseMultivalArgs)
	{
		memset(cmd_context->args, 0, sizeof(cmd_context->args));
	}

	cmdCleanup(cmd,cmd_context);

	cmdParseFree(&buf);
	PERFINFO_AUTO_STOP_FUNC();
	return cmd;
}

Cmd *cmdMultiValRead(Cmd *cmd, MultiVal ** args, CmdContext* cmd_context, bool syntax_check)
{
	int		j,argc=0,toggle=0,idx=1,invert=0;
	void	*ptr;
	int access_level;
	CmdHandler handler = NULL;

	if (cmd_context)
	{
		cmd_context->op = CMD_OPERATION_NONE;
		access_level = cmd_context->access_level;
	}
	else
	{
		return NULL;
	}

	if (!cmd || cmd->access_level > access_level)
	{
		if (cmd_context)
		{
			cmd_context->cmd_unknown = true;
			if (!gbFolderCacheModeChosen) {
				estrPrintf(cmd_context->output_msg, "Unknown command \"%s\".", cmd->name);
			} else {
				FormatMessageKey(cmd_context->output_msg, "CmdParse_UnknownCommand", STRFMT_STRING("Command", cmd->name), STRFMT_END);
			}
		}
		return 0;
	}

	cmd_context->found_cmd = cmd;
	cmd_context->return_val.type = cmd->return_type.type;
	handler = cmd->handler;

	argc = eaSize(&args);

	if (argc != cmd->iNumLogicalArgs)
	{
		cmd_context->good_args = false;
		if (argc == 0 && (cmd->flags & CMDF_PRINTVARS))
		{
			if (cmd->data[0].ptr && cmd->data[0].type != MULTI_NP_POINTER)
			{
				char printBuffer[10000];
				char *pFirstSpace;
				// Print out the parameter, if it's the right kind of command
				cmdPrintCommmandString(cmd,SAFESTR(printBuffer));


				if ((cmd_context->flags & CMD_CONTEXT_FLAG_TREAT_PRINTVARS_AS_SUCCESS) 
					&& (pFirstSpace = strchr(printBuffer, ' ')) )
				{
					estrCopy2(cmd_context->output_msg,pFirstSpace);
				}
				else
				{
					estrCopy2(cmd_context->output_msg,printBuffer);
				}

				if (cmd_context->flags & CMD_CONTEXT_FLAG_TREAT_PRINTVARS_AS_SUCCESS)
				{
					cmd_context->good_args = true;
				}
			}
		}
		else if (cmd_context->output_msg && cmd->name[0] && !cmd->error_handler)
		{
			const char *pchMessage = TranslateMessageKeyDefault("CmdParse_IncorrectUsage", "\"{Command}\" takes {ArgCount} argument(s), not {GivenCount}.");
			estrClear(cmd_context->output_msg);
			strfmt_FromArgs(cmd_context->output_msg, pchMessage,
				STRFMT_STRING("Command", cmd->name),
				STRFMT_INT("ArgCount", cmd->iNumLogicalArgs),
				STRFMT_INT("GivenCount", argc - 1),
				STRFMT_STRING("Comment", cmd->comment),
				STRFMT_END);
			estrAppend2(cmd_context->output_msg,"\n");
			estrAppend2(cmd_context->output_msg,cmd->name);

			AddCmdArgDescriptions(cmd_context->output_msg, cmd);

		}		
		if (cmd->error_handler && !syntax_check)
		{
			// If there's a function pointer handler, run it now
			cmd->error_handler(cmd,cmd_context);
		}
		return cmd;		
	}
	else
	{
		for (j = 0; j < argc; j++)
		{
			if (args[j]->type != cmd->data[j].type)
			{
				if (cmd_context->output_msg && cmd->name[0] && !cmd->error_handler)
				{
					const char *pchMessage = TranslateMessageKeyDefault("CmdParse_IncorrectUsage", "\"{Command}\" takes {ArgCount} argument(s), not {GivenCount}.");
					estrClear(cmd_context->output_msg);
					strfmt_FromArgs(cmd_context->output_msg, pchMessage,
						STRFMT_STRING("Command", cmd->name),
						STRFMT_INT("ArgCount", cmd->iNumLogicalArgs),
						STRFMT_INT("GivenCount", argc - 1),
						STRFMT_STRING("Comment", cmd->comment),
						STRFMT_END);

					estrAppend2(cmd_context->output_msg,"\n");
					estrAppend2(cmd_context->output_msg,cmd->name);
					
					AddCmdArgDescriptions(cmd_context->output_msg, cmd);


				}
				if (cmd->error_handler && !syntax_check)
				{
					// If there's a function pointer handler, run it now
					cmd->error_handler(cmd,cmd_context);
				}
				return cmd;		
			}
		}
		cmd_context->good_args = true;
	}

	if (syntax_check)
	{
		return cmd;
	}
	for(j=0;j<argc;j++)
	{
		ptr = cmd->data[j].ptr;
		cmd_context->args[j].type = cmd->data[j].type;

		switch(cmd->data[j].type)
		{
		case MULTI_INT:
			cmd_context->args[j].intval = args[j]->intval;
		xcase MULTI_FLOAT:
			cmd_context->args[j].floatval = args[j]->floatval;
		xcase MULTI_STRING:			
		case MULTI_NP_POINTER:			
		case MULTI_VEC3:
		case MULTI_VEC4:
		case MULTI_QUAT:
		case MULTI_MAT4:
			cmd_context->args[j].ptr = args[j]->ptr;
		}
	}
	if (handler)
	{
		// If this table has a global handler, run it now
		handler(cmd,cmd_context);
	}

	return cmd;
}



// Clear out anything allocated by cmdRead
void cmdCleanup(Cmd *cmd, CmdContext* cmd_context) 
{
	int j;
	void *ptr;
	if (!cmd)
		return;

	for(j=0;j<cmd->iNumLogicalArgs;j++)
	{
		ptr = (void *)cmd_context->args[j].ptr;
		switch(cmd->data[j].type)
		{
		// These are allocated with alloca for now
		xcase MULTI_NP_POINTER:
			{	
				if (!ptr || !cmd_context || !cmd->data[j].ptr || !(cmd->data[j].flags & CMDAF_ALLOCATED) || cmdContextHasStructList(cmd_context))
				{					
					break;
				}

				StructDestroyVoid(cmd->data[j].ptr,ptr);
			}
		xcase MULTI_STRING:
			{	
				// Only free the string if we weren't given one originally
				if (!ptr || !cmd_context || cmd->data[j].ptr || !(cmd->data[j].flags & CMDAF_ALLOCATED))
				{					
					break;
				}
				

				cmdParseFree((void**)(&cmd_context->args[j].str));
				cmd_context->args[j].str = NULL;
			}
		}
	}
	cmd_context->commandString = NULL;
}

// Returns 1 if the command was executed. 0 if there was an error. context->found_cmd will
// be true if a command was found, and just failed to execute.
int cmdParseAndExecute(CmdList *cmdlist, const char* str, CmdContext *cmd_context)
{
	
	cmd_context->found_cmd = NULL;
	cmd_context->good_args = false;

	cmdReadInternal(cmdlist,str,cmd_context,0, false, NULL);

	if (!cmd_context->found_cmd || !cmd_context->good_args)
	{	
		return 0;
	}

	return 1;
	
}


int cmdExecuteWithMultiVals(CmdList *cmdlist, const char *cmdName, CmdContext *cmd_context, MultiVal ***pppArgs)
{

	cmd_context->found_cmd = NULL;
	cmd_context->good_args = false;

	cmdReadInternal(cmdlist,cmdName,cmd_context,0, true, pppArgs);

	if (!cmd_context->found_cmd || !cmd_context->good_args)
	{	
		return 0;
	}

	return 1;




}


// Does most of what cmdParseAndExecute does, but just checks to see if it is valid
int cmdCheckSyntax(CmdList *cmdlist, const char* str, CmdContext *cmd_context )
{
	cmd_context->found_cmd = NULL;
	cmd_context->good_args = false;

	cmdReadInternal(cmdlist,str,cmd_context,1, false, NULL); //only do a syntax check

	if (!cmd_context->found_cmd || !cmd_context->good_args)
	{	
		return 0;
	}

	return 1;

}

// Executes a command, by directly passing through MultiVals
int cmdDirectExecute(Cmd *cmd, CmdContext *cmd_context, MultiVal **args)
{

	if (!cmd_context)
	{		
		return 0;
	}
	if (!cmd)
	{
		cmd_context->return_val.type = 0;
		cmd_context->return_val.intval = 0;
		cmd_context->found_cmd = NULL;
		return 0;
	}

	cmd_context->found_cmd = cmd;
	cmd_context->good_args = false;

	cmdMultiValRead(cmd,args,cmd_context,0);

	if (!cmd_context->found_cmd || !cmd_context->good_args)
	{	
		return 0;
	}

	return 1;
}


void cmdInit(Cmd *cmd)
{
	int j, count;
	count = 0;

	cmd->original_access_level = cmd->access_level;

	for(j=0;cmd->data[j].type && j<ARRAY_SIZE(cmd->data);j++)
	{
		cmd->iNumLogicalArgs++;

		if (cmd->data[j].type == MULTI_STRING)
		{
			assertmsg(cmd->data[j].data_size || !cmd->data[j].ptr,"Strings with global variables must have size!");
		}
		else if (cmd->data[j].type == MULTI_NP_POINTER)
		{
			assertmsg(cmd->data[j].flags & CMDAF_TEXTPARSER,"Pointers must be declared as TextParser");
			assertmsg(cmd->data[j].ptr,"Pointers must provide a StructInfo pointer");
		}
		if (cmd->data[j].type == MULTI_MAT4)
			count += 12;
		else if (cmd->data[j].type == MULTI_VEC3)
			count += 3;
		else if (cmd->data[j].type == MULTI_VEC4)
			count += 4;
		else if (cmd->data[j].type == MULTI_QUAT)
			count += 4;
		else
			count++;
	}
	cmd->iNumReadArgs = count;
}

static int defaultCmdParseFunc(const char *cmdOrig, char **ppReturnString, CmdContextFlag iCmdContextFlags, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	char *pReturnStringInternal = NULL;
	char *lineBuffer, *cmd, *cmdStart = NULL;
	int iResult = 1;

	CmdContext cmd_context = {0};
	cmd_context.flags |= iCmdContextFlags;
	cmd_context.eHowCalled = eHow;
	cmd_context.pStructList = pStructs;

	if (ppReturnString)
	{
		cmd_context.output_msg = ppReturnString;
	}
	else
	{
		estrStackCreate(&pReturnStringInternal);
		cmd_context.output_msg = &pReturnStringInternal;
	}

	cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : 10;

	estrStackCreate(&cmdStart);
	estrCopy2(&cmdStart,cmdOrig);
	cmd = cmdStart;

	while (cmd)
	{
		estrClear(cmd_context.output_msg);
		lineBuffer = cmdReadNextLine(&cmd);
		iResult &= cmdParseAndExecute(&gGlobalCmdList, lineBuffer, &cmd_context);
	}

	estrDestroy(&pReturnStringInternal);
	estrDestroy(&cmdStart);
	return iResult;
}

CmdParseFunc globCmdParseAndReturn = defaultCmdParseFunc;
void cmdSetGlobalCmdParseFunc(CmdParseFunc func)
{
	globCmdParseAndReturn = func;
}

#undef globCmdParsef
int globCmdParsef(const char *fmt, ...)
{
	int returnVal;
	va_list va;
	char *commandstr = NULL;
	estrStackCreate(&commandstr);

	va_start( va, fmt );
	estrConcatfv(&commandstr,fmt,va);
	va_end( va );

	returnVal = globCmdParse(commandstr);

	estrDestroy(&commandstr);
	return returnVal;
}


CmdList gGlobalCmdList = {0};
CmdList gEarlyCmdList = {0};
CmdList gPrivateCmdList = {0};
CmdList gGatewayCmdList = {0};
CmdList gNotThisProductList = {0};

void cmdAddSingleCmdToList(CmdList *cmdList, Cmd *cmd)
{
	int i;
	char *pProductName;

	if (cmd->pProductNames && (pProductName = GetProductName_IfSet()))
	{
		char **ppProducts = NULL;
		int iFoundIndex;
		DivideString(cmd->pProductNames, ",", &ppProducts, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
		iFoundIndex = eaFindString(&ppProducts, pProductName);
		eaDestroyEx(&ppProducts, NULL);

		if (iFoundIndex == -1)
		{
			return;
		}
	}

	cmdInit(cmd);
	cmdListAddToHashes(cmdList,cmd);

	if (cmdList == &gGlobalCmdList && cmd->access_level == 0)
	{
		// 	if (!stricmp(cmd->comment, "No comment provided"))
		// 	{
		// 		printf("WARNING: No comment for public visible command: %s", cmd->name);
		// 	}
		for (i = 0; i < cmd->iNumLogicalArgs; i++)
		{
			if (cmd->data[i].type == MULTI_NP_POINTER)
			{
				Errorf("Public access level 0 comand takes a tp block: %s", cmd->name);
			}
		}
	}
}

void cmdAddCmdArrayToList(CmdList *cmdList, Cmd cmds[])
{
	int i;
	for (i = 0; cmds[i].name; i++)
	{
		cmdAddSingleCmdToList(cmdList,&cmds[i]);
	}
}

void cmdAddAutoSettingGlobalType(Cmd *pCmd, GlobalType eType)
{
	ea32Push(&pCmd->pAutoSettingGlobalTypes, (U32)eType);
}

// Executes each line in a file as a slash command
AUTO_COMMAND ACMD_NAME(exec) ACMD_CMDLINE ACMD_APPSPECIFICACCESSLEVEL(GLOBALTYPE_CLIENT, 0); // handler for "exec" or /exec  MUST be access level 9 (at least on server)
void cmdParseExecFile(Cmd *cmd, CmdContext *context, const char *filename)
{
	char *ret=NULL;
	int lenp;
	char *data = fileAlloc(filename, &lenp);
	char *str;
	char *last;
	bool found_cmd=true;
	bool good_args=true;
	if (!data) {
		if(!strEndsWith(filename, ".txt")){
			char buffer[MAX_PATH];
			sprintf(buffer, "%s.txt", filename);
			data = fileAlloc(buffer, &lenp);
		}
		if(!data){
			estrConcatf(context->output_msg, "Failed to load %s", filename);
			return;
		}
	}
	
	for(str = strtok_r(data, "\r\n", &last);
		str;
		str = strtok_r(NULL, "\r\n", &last))
	{
		const char* cmd_str = removeLeadingWhiteSpaces(str);
		
		if(	cmd_str[0] == '#'
			||
			cmd_str[0] == ';'
			||
			cmd_str[0] == '/' &&
			cmd_str[1] == '/')
		{
			continue;
		}
		if (!globCmdParseAndReturn(cmd_str, &ret, 0, -1, context->eHowCalled, NULL))
		{
			estrConcatf(context->output_msg, "%s\n", ret);
			good_args = false;
			cmd = NULL;
		}
	}

	estrDestroy(&ret);

	if (!found_cmd)
	{
		context->found_cmd = NULL;
	}
	context->found_cmd = cmd;
	context->good_args = good_args;
	context->outFlags |= CTXTOUTFLAG_NO_CLIENT_TO_SERVER_PROPOGATION | CTXTOUTFLAG_NO_OUTPUT_ON_UNHANDLED;
	fileFree(data);
}

void cmdContextReset(CmdContext *context)
{
	char **outputEstr = context->output_msg;
	ZeroStruct(context);
	context->output_msg = outputEstr;
	estrClear(context->output_msg);
	context->commandString = NULL;
}


/*
// The old one, for reference, since some features are still missing in the new one
int gclCompleteCommand(char *str, char *out, int searchID, int searchBackwards)
{
	static int num_cmds = sizeof(game_cmds)/sizeof(Cmd) - 1, 
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
		out[0] = 0;
	return 0;
}
*/


void cmdPrintList(CmdList *cmdList,int access_level,char *match_str_src,
			   int print_header,CmdPrintCB printCB, Language language, void *userData)
{
	char	buf[2000];

	char	commentBuffer[2000];
	char*	comment;
	char*	str;
	char	delim;
	int		firstLine;
	size_t	maxNameLen = 1;
	char	nameFormat[100];
	char	commentFormat[100];
	char	match_str[1024];

	if( match_str_src )
		strcpy(match_str, stripUnderscores(match_str_src));
	else
		match_str[0] = '\0';

	if (print_header) {
		char *header = NULL;
		estrStackCreate(&header);
		FormatMessageKey(&header, "CmdParse_LevelCommands", STRFMT_INT("AccessLevel", access_level), STRFMT_END);
		sprintf( buf, "\n%s\n", header);
		printCB(header,userData);
		estrDestroy(&header);
	}

	FOR_EACH_IN_STASHTABLE(cmdList->sCmdsByName, Cmd, cmd)
	{
		maxNameLen = max(maxNameLen, strlen(cmd->name));
	}
	FOR_EACH_END;

	if( access_level > 0 )
		sprintf(nameFormat, "%%d %%-%d.%ds", maxNameLen, maxNameLen);
	else
		sprintf(nameFormat, "%%-%d.%ds", maxNameLen+10, maxNameLen+10);
	sprintf(commentFormat, "%%s%%-%d.%ds", maxNameLen + 3, maxNameLen + 3);

	FOR_EACH_IN_STASHTABLE(cmdList->sCmdsByName, Cmd, cmd)
	{
		const char * cmdname;

		cmdname = CmdTranslatedName(cmd->name, language);

		if (cmd->access_level > access_level || !cmd->comment || (match_str && !strstri(stripUnderscores(cmdname),match_str)))
			continue;
		if( cmd->flags & CMDF_HIDEPRINT && (cmd->access_level == access_level))
			continue;
		if( cmd->flags & CMDF_COMMANDLINEONLY && access_level == 0)
			continue;

		if( access_level > 0 )
			sprintf(buf,FORMAT_OK(nameFormat),cmd->access_level,cmdname);
		else
			sprintf(buf,FORMAT_OK(nameFormat),cmdname);

		strcat(buf," ");

		if( stricmp(cmd->name, cmdname) == 0 )
		{
			assert(strlen(cmd->comment) < 2000);
			strcpy(commentBuffer, cmd->comment ? cmd->comment : "");
		}
		else
		{
			strcpy(commentBuffer, CmdTranslatedHelp(cmd, language));
		}

		comment = commentBuffer;
		firstLine = 1;
		for(str = strsep2(&comment, "\n", &delim); str; str = strsep2(&comment, "\n", &delim)){
			if(firstLine)
				firstLine = 0;
			else
				sprintf(buf, FORMAT_OK(commentFormat), buf, "");
				strcat(buf, str);
			strcat(buf, "\n");
			if(delim)
				comment[-1] = delim;
		}
		printCB(buf,userData);
	}
	FOR_EACH_END;
}
#define MDASH (-106)

#pragma warning(disable:6385)

void cmdParseCommandLine_internal(int argc_in,char **argv_in, bool bUseEarlyList)
{
	int	i;
	char* buf = NULL;
	CmdContext context = {0};
	bool bFailed;
	bool bOKIfDoesntExist = false;
	int argc;
	char **argv;
	char **ppAllocedArgv = NULL;

	if (!bUseEarlyList && GetCommandLineFromFile())
	{
		int iMaxArgs;
		int iNumArgs;
		const char *pTemp = GetCommandLineFromFile();

		iMaxArgs = 1;

		while (*pTemp)
		{
			if (*pTemp == ' ')
			{
				iMaxArgs++;
			}

			pTemp++;
		}
		
		ppAllocedArgv = calloc((iMaxArgs + argc_in) * sizeof(char*), 1);
		iNumArgs = tokenize_line_quoted_safe(GetCommandLineFromFile(),ppAllocedArgv + argc_in, iMaxArgs ,NULL);

		memcpy(ppAllocedArgv, argv_in, sizeof(char*) * argc_in);

		argv = ppAllocedArgv;
		argc = argc_in + iNumArgs;
	}
	else
	{
		argc = argc_in;
		argv = argv_in;
	}
	


	context.access_level = 10;

	context.eHowCalled = bUseEarlyList ? CMD_CONTEXT_HOWCALLED_EARLY_COMMANDLINE : CMD_CONTEXT_HOWCALLED_COMMANDLINE;

	if (gbLimitCommandLineCommands)
	{	
		context.flags |= CMD_CONTEXT_FLAG_COMMAND_LINE_CMDS_ONLY;
	}
	
	estrStackCreate(&buf);

	for (i=0; i<argc; i++)
	{
		if (i)
			estrConcatStatic(&buf, " ");
		if (i>=2 && isdigit(argv[i][0]) && (stricmp(argv[i-1], "-AuthTicketNew")==0 || stricmp(argv[i-2], "-AuthTicketNew")==0))
		{
			// Remove the two parameters to -AuthTicketNew so that ErrorTracker's trivia can be grouped in a useful manner, people can look in the dump if they need the whole string
			estrAppend2(&buf, "<removed>");
		} else {
			estrAppend2(&buf, argv[i]);
		}
	}
	triviaPrintf("CommandLine", "%s", buf);
	estrClear(&buf);

	for(i=bUseEarlyList ? 0 : 1;i<argc;)
	{
		char *ret=NULL;
		if (!argv[i][0]) {
			i++;
			continue;
		}
		if (argv[i][0] != '-' && argv[i][0] != '+' && argv[i][0] != MDASH)
		{
			if (!bUseEarlyList)
			{
#if !PLATFORM_CONSOLE
				pushErrorDialogCallback(NULL, NULL); // Make sure the client immediately displays the error!
#endif
				Alertf("Unrecognized command \"%s\" (did you mean \"-%s\"?)", argv[i], argv[i]);
#if !PLATFORM_CONSOLE
				popErrorDialogCallback();
#endif
			}

			i++;
			continue;
		}

		//dash all by itself is a separator, skip it
		if (argv[i][0] == '-' && argv[i][1] == 0)
		{
			i++;
			continue;
		}

		//commands that start with "-?foo" mean "execute foo, but don't complain if it doesn't exist
		if (argv[i][1] == '?' && argv[i][2] != ' ' && argv[i][2] != 0)
		{
			estrCopy2(&buf, &argv[i][2]);
			bOKIfDoesntExist = true;
		}
		else
		{
			estrCopy2(&buf,&argv[i][1]);
			bOKIfDoesntExist = false;
		}

		

		for(i=i+1;i<argc;i++)
		{
			if (!argv[i][0] || argv[i][0] == '-' || argv[i][0] == '+' || argv[i][0] == MDASH)
				break;
			estrConcat(&buf, " ", 1);
			if (strchr(argv[i],' ') || strchr(argv[i],','))
			{
				if(!strStartsWith(argv[i], "\""))
				{
					estrConcat(&buf,"\"", 1);
				}
				estrConcat(&buf,argv[i], (U32)strlen(argv[i]));
				if(!strEndsWith(argv[i], "\""))
				{
					estrConcat(&buf,"\"", 1);
				}
			}
			else
			{
				estrConcat(&buf,argv[i], (U32)strlen(argv[i]));
			}
		}

		if (!bUseEarlyList && gbPrintCommandLine)
		{
			printf(" %s\n",buf);
		}


		cmdCheckSyntax(&gEarlyCmdList, buf, &context);
		bFailed = false;

		if (context.found_cmd)
		{
			if (bUseEarlyList)
			{
				
				if (!cmdParseAndExecute(&gEarlyCmdList, buf, &context))
				{
					bFailed = true;
				}
			}
		}
		else
		{	
			if (!bUseEarlyList)
			{
				if (!globCmdParseAndReturnWithFlags(buf, &ret, (CMD_CONTEXT_FLAG_NO_EARLY_COMMAND_LINE) | (gbLimitCommandLineCommands ? CMD_CONTEXT_FLAG_COMMAND_LINE_CMDS_ONLY : 0), bUseEarlyList ? CMD_CONTEXT_HOWCALLED_EARLY_COMMANDLINE : CMD_CONTEXT_HOWCALLED_COMMANDLINE))
				{
					bFailed = true;
				}
			}
		}

		if (bFailed && !bOKIfDoesntExist)
		{
			if (!bUseEarlyList)
			{
#if !PLATFORM_CONSOLE
				pushErrorDialogCallback(NULL, NULL); // Make sure the client immediately displays the error!
#endif
				if (ret)
				{
					Alertf("Unrecognized command: %s\n%s", buf, ret);
				} else {
					Alertf("Unrecognized command: %s", buf);
				}
#if !PLATFORM_CONSOLE
				popErrorDialogCallback();
#endif
			}
		}
		estrDestroy(&ret);
	}

	estrDestroy(&buf);
	SAFE_FREE(ppAllocedArgv);
}


AUTO_RUN_EARLY;
void DoEarlyCommandLineParsing(void)
{
	char *pDupCmdLine = NULL;
	char **ppArgs; //NOT an earray.
	int iMaxArgs;
	int iNumArgs;

	loadCmdLineIntoEString("./cmdline.txt", &pDupCmdLine);
	estrConcatf(&pDupCmdLine, " ");
	estrConcatf(&pDupCmdLine, "%s", GetCommandLineWithoutExecutable());

	iMaxArgs = estrCountChars(&pDupCmdLine, ' ') + 1;

	ppArgs = calloc(iMaxArgs * sizeof(char*), 1);

	iNumArgs = tokenize_line_quoted_safe(pDupCmdLine,ppArgs, iMaxArgs ,NULL);

	cmdParseCommandLine_internal(iNumArgs, ppArgs, true);

	estrDestroy(&pDupCmdLine);
	free(ppArgs);
}

static char* cmdReadNextSeparator(unsigned char *cmd_str)
{
	unsigned char *pch;
	int iCntQuote=0;
	const char *pchQuote=cmd_str;

	if (!cmd_str)
	{
		return NULL;
	}

	pch=strstr(cmd_str, "$$");
	while(pch!=NULL)
	{
		// Check to see if this is inside a quoted string.
		// This is a little naive; it just counts the quotes up to
		//   this point. If there's an odd number, we're in a quoted
		//   section.
		while(pchQuote && (pchQuote=strchr(pchQuote,'"'))!=NULL && pchQuote<(char*)pch)
		{
			pchQuote++;
			iCntQuote++;
		}

		// Inside a quote, look for the next one
		if(iCntQuote&1)
		{
			pch=strstr(pch+2, "$$");
		}
		else // not inside a quote - done
			break;
	}
	
	return pch;
}

char *cmdReadNextLine(char **sourceStr)
{
	char* cmd = *sourceStr;
	char* sep;
	
	if(!cmd)
	{
		return NULL;
	}
	
	sep = cmdReadNextSeparator(cmd);

	if(!sep)
	{
		*sourceStr = NULL;
	}
	else
	{
		sep[0] = '\0';
		sep += 2;
		while (characterIsSpace(*sep)) sep++;
		if (!*sep)
			*sourceStr = NULL;
		else
			*sourceStr = sep;
	}

	return cmd;
}

S32 cmdReadNextLineConst(const char **sourceStr, char** estrCmdOut, const char** cmdOut)
{
	const char* cmd = *sourceStr;
	char* sep;
	
	if(!cmd)
	{
		return 0;
	}
	
	sep = cmdReadNextSeparator((char*)cmd);

	if(!sep)
	{
		*sourceStr = NULL;
		*cmdOut = cmd;
	}
	else
	{
		estrClear(estrCmdOut);
		estrConcat(estrCmdOut, cmd, sep - cmd);
		*cmdOut = *estrCmdOut;
		sep += 2;
		while (characterIsSpace(*sep)) sep++;
		if (!*sep)
			*sourceStr = NULL;
		else
			*sourceStr = sep;
	}

	return 1;
}

NameList *pGlobCmdListNames = NULL;
NameList *pAddedCmdNames = NULL;
NameList *pAllCmdNamesForAutoComplete = NULL;
NameList *pMRUCmdNameList = NULL;

static Cmd *cmdGetCmdAndArgCountFromPartialCommandString(char *pCommandString, char **ppCommandName, int *piArgCount) {
	char fullCommandString[1024];	
	char *pArgs;
	char *strtokcontext = 0;

	*piArgCount = 0;

 	strcpy(fullCommandString, pCommandString);

	if(strrchr(fullCommandString, ' ')-fullCommandString==(int)strlen(fullCommandString)-1)
		(*piArgCount)++;  // Last character is a space - give "next" arg

	*ppCommandName = strtok_s(fullCommandString, " ", &strtokcontext);

	while ((pArgs = strtok_s(NULL, " ", &strtokcontext)))
	{
		(*piArgCount)++;
	}
	

	if (*piArgCount < 1)
	{
		return NULL;
	}

	return cmdListFind(&gGlobalCmdList, *ppCommandName);
}

enumNameListType cmdGetNameListTypeFromPartialCommandString(char *pCommandString)
{
	char *pCommandName = NULL;
	int iArgCount = 0;
	Cmd *pCmd;

	pCmd = cmdGetCmdAndArgCountFromPartialCommandString(pCommandString, &pCommandName, &iArgCount);

	if (!pCmd)
	{
		if (gpGetExtraNameListTypeForArgAutoCompletionCB && pCommandName)
		{
			return gpGetExtraNameListTypeForArgAutoCompletionCB(pCommandName, iArgCount - 1);
		}

		return NAMELISTTYPE_NONE;
	}

	if (iArgCount > CMDMAXARGS)
	{
		return NAMELISTTYPE_NONE;
	}

	return pCmd->data[iArgCount-1].eNameListType;
}

char **ppCommandsWithAutoCompleteDisabled = NULL;

//requested by Brent H, who is sick of the huge amount of time it takes for commands like giveItem to do their autocompletion on 
//STO. Run this with any command name, and that command will never do autocomplete thereafeter.
AUTO_COMMAND ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CLIENTONLY;
void DisableAutoCompleteForCommand(char *pCommandName)
{
	eaPush(&ppCommandsWithAutoCompleteDisabled, strdup(pCommandName));
}


static bool cmdHasAutoCompleteDisabled(char *pCommandName)
{
	if (eaFindString(&ppCommandsWithAutoCompleteDisabled, pCommandName) == -1)
	{
		return false;
	}

	return true;
}


NameList *cmdGetNameListFromPartialCommandString(char *pCommandString)
{
	char *pCommandName = NULL;
	int iArgCount = 0;
	Cmd *pCmd;
	NameList* nl;

	PERFINFO_AUTO_START_FUNC();

	pCmd = cmdGetCmdAndArgCountFromPartialCommandString(pCommandString, &pCommandName, &iArgCount);

	if (cmdHasAutoCompleteDisabled(pCommandName))
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	if (!pCmd)
	{
		if (gpGetExtraNameListForArgAutoCompletionCB && pCommandName)
		{
			PERFINFO_AUTO_STOP();
			return gpGetExtraNameListForArgAutoCompletionCB(pCommandName, iArgCount - 1);
		}

		PERFINFO_AUTO_STOP();
		return NULL;
	}

	if (iArgCount > CMDMAXARGS)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	nl = CreateTempNameListFromTypeAndData(pCmd->data[iArgCount-1].eNameListType, pCmd->data[iArgCount-1].ppNameListData);

	PERFINFO_AUTO_STOP();
	
	return nl;
}

static cmdSendCmdServerToClientCB *spServerToClientCB = NULL;
static cmdSendCmdClientToServerCB *spClientToServerCB = NULL;


void cmdSendCmdServerToClient(Entity *pEntity, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, bool bFast, CmdParseStructList *pStructs)
{
	assert(spServerToClientCB);
	spServerToClientCB(pEntity, pCmd, bPrivate, iFlags, eHow, bFast, pStructs);
}

void cmdSendCmdClientToServer(const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	assert(spClientToServerCB);
	spClientToServerCB(pCmd, bPrivate, iFlags, eHow, pStructs);
}

void cmdSetServerToClientCB(cmdSendCmdServerToClientCB *pCB)
{
	spServerToClientCB = pCB;
}
void cmdSetClientToServerCB(cmdSendCmdClientToServerCB *pCB)
{
	spClientToServerCB = pCB;
}

static cmdSendCmdGenericServerToClientCB *spServerToClientGenericCB = NULL;
static cmdSendCmdGenericToServerCB *spToServerGenericCB = NULL;

void cmdSetGenericServerToClientCB(cmdSendCmdGenericServerToClientCB *pCB)
{
	spServerToClientGenericCB = pCB;
}
void cmdSetGenericToServerCB(cmdSendCmdGenericToServerCB *pCB)
{
	spToServerGenericCB = pCB;
}
void cmdSendCmdGenericServerToClient(U32 uID, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	assert(spServerToClientGenericCB);
	spServerToClientGenericCB(uID, pCmd, bPrivate, iFlags, eHow, pStructs);
}
void cmdSendCmdGenericToServer(GlobalType eServerType, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	assert(spToServerGenericCB);
	spToServerGenericCB(eServerType, pCmd, bPrivate, iFlags, eHow, pStructs);
}

typedef struct OncePerFrameCmd {
	char *cmdStr;
	int count;
	S64 start;
	int iAccessLevel;
} OncePerFrameCmd;

static OncePerFrameCmd **sppOncePerFrameCommands = NULL;

void freeOncePerFrameCmd(OncePerFrameCmd *cmd)
{
	free(cmd->cmdStr);
	free(cmd);
}

void cmdParseOncePerFrame(void)
{
	int i;
	int iSize = eaSize(&sppOncePerFrameCommands);
	PERFINFO_AUTO_START_FUNC();
	for (i = iSize-1; i >=0 ; i--)
	{
		OncePerFrameCmd *cmd = sppOncePerFrameCommands[i];

		if(ABS_TIME < cmd->start)
			continue;

		globCmdParseAndReturnWithFlagsAndOverrideAccessLevel(cmd->cmdStr, NULL, 0, cmd->iAccessLevel, CMD_CONTEXT_HOWCALLED_ONCEPERFRAME);
		if(cmd->count>0)
			cmd->count--;

		if(!cmd->count)
		{
			eaRemoveFast(&sppOncePerFrameCommands, i);
			freeOncePerFrameCmd(cmd);
		}
	}
	PERFINFO_AUTO_STOP();
}

//this command is used to bootstrap super-escaped commands on the command line.
AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void SuperEsc(char *pCommandName, char *pSuperEscapedArg, CmdContext *pContext)
{
	char *pUnescapedArg = NULL;
	char *pNewString = NULL;

	estrStackCreate(&pUnescapedArg);
	estrStackCreate(&pNewString);

	if (!estrSuperUnescapeString(&pUnescapedArg, pSuperEscapedArg))
	{
		Errorf("Invalid superescaped string %s in command %s", pSuperEscapedArg, pCommandName);
	}
	else
	{

		estrPrintf(&pNewString, "%s \"", pCommandName);
		estrAppendEscaped(&pNewString, pUnescapedArg);
		estrConcatf(&pNewString, "\"");

		globCmdParseAndReturnWithFlagsAndOverrideAccessLevel(pNewString, NULL, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED | pContext->flags, pContext->access_level, pContext->eHowCalled);
	}

	estrDestroy(&pUnescapedArg);
	estrDestroy(&pNewString);
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void EarlySuperEsc(char *pCommandName, char *pSuperEscapedArg, CmdContext *pContext)
{
	char *pUnescapedArg = NULL;
	char *pNewString = NULL;

	estrStackCreate(&pUnescapedArg);
	estrStackCreate(&pNewString);

	if (!estrSuperUnescapeString(&pUnescapedArg, pSuperEscapedArg))
	{
		Errorf("Invalid superescaped string %s in command %s", pSuperEscapedArg, pCommandName);
	}
	else
	{

		estrPrintf(&pNewString, "%s \"", pCommandName);
		estrAppendEscaped(&pNewString, pUnescapedArg);
		estrConcatf(&pNewString, "\"");

		globCmdParseAndReturnWithFlagsAndOverrideAccessLevel(pNewString, NULL, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED | pContext->flags, pContext->access_level, pContext->eHowCalled);
	}

	estrDestroy(&pUnescapedArg);
	estrDestroy(&pNewString);
}

AUTO_COMMAND;
void oncePerFrameClear(void)
{
	eaClearEx(&sppOncePerFrameCommands, freeOncePerFrameCmd);
}

AUTO_COMMAND;
void oncePerFrame(CmdContext *pContext, ACMD_SENTENCE cmdStr ACMD_NAMELIST(pAllCmdNamesForAutoComplete))
{
	OncePerFrameCmd *cmd = calloc(1, sizeof(OncePerFrameCmd));
	cmd->cmdStr = strdup(cmdStr);
	cmd->count = -1;
	cmd->start = ABS_TIME;
	cmd->iAccessLevel = pContext->access_level;
	eaPush(&sppOncePerFrameCommands, cmd);
}

AUTO_COMMAND;
void oncePerFrameCount(CmdContext *pContext, int count, ACMD_SENTENCE cmdStr ACMD_NAMELIST(pAllCmdNamesForAutoComplete))
{
	OncePerFrameCmd *cmd = calloc(1, sizeof(OncePerFrameCmd));
	cmd->cmdStr = strdup(cmdStr);
	cmd->count = count;
	cmd->start = ABS_TIME;
	cmd->iAccessLevel = pContext->access_level;
	eaPush(&sppOncePerFrameCommands, cmd);
}

AUTO_COMMAND;
void delayedCmd(CmdContext *pContext, F32 sec, ACMD_SENTENCE cmdStr ACMD_NAMELIST(pAllCmdNamesForAutoComplete))
{
	OncePerFrameCmd *cmd = calloc(1, sizeof(OncePerFrameCmd));

	cmd->cmdStr = strdup(cmdStr);
	cmd->start = ABS_TIME + SEC_TO_ABS_TIME(sec);
	cmd->count = 1;

	cmd->iAccessLevel = pContext->access_level;
	eaPush(&sppOncePerFrameCommands, cmd);
}

AUTO_RUN;
void initNameListsForAutoComplete(void)
{
	pGlobCmdListNames = CreateNameList_CmdList(&gGlobalCmdList, 9);
	pAddedCmdNames = CreateNameList_AccessLevelBucket(9);
	pAllCmdNamesForAutoComplete = CreateNameList_MultiList();
	pMRUCmdNameList = CreateNameList_MRUList("cmdparse", 64, 128);

	NameList_MultiList_AddList(pAllCmdNamesForAutoComplete, pMRUCmdNameList); // MUST be first
	NameList_MultiList_AddList(pAllCmdNamesForAutoComplete, pGlobCmdListNames);
	NameList_MultiList_AddList(pAllCmdNamesForAutoComplete, pAddedCmdNames);
}

cmdGetExtraNameListForArgAutoCompletionCB *gpGetExtraNameListForArgAutoCompletionCB = NULL;
cmdGetExtraNameListTypeForArgAutoCompletionCB *gpGetExtraNameListTypeForArgAutoCompletionCB = NULL;

int SortCmdsByOriginAndName(const Cmd **ppCmd1, const Cmd **ppCmd2)
{
	int iResult = stricmp((*ppCmd1)->origin, (*ppCmd2)->origin);

	if (iResult == 0)
	{
		iResult = stricmp((*ppCmd1)->name, (*ppCmd2)->name);
	}
	else
	{
		bool b1IsLib = strEndsWith((*ppCmd1)->origin, "Lib");
		bool b2IsLib = strEndsWith((*ppCmd2)->origin, "Lib");

		if (b1IsLib ^ b2IsLib)
		{
			return b1IsLib;
		}
	}


	return iResult;
}

LATELINK;
void help(char *pCommandName);

void DEFAULT_LATELINK_help(char *pCommandName)
{
#if !PLATFORM_CONSOLE
	newConsoleWindow();

	if (strcmp(pCommandName, "1") == 0)
	{
		int i, j;

		Cmd **ppCmds = NULL;
		const char *pLastOrigin = NULL;



		printf("All command line commands for %s\n----------------------------------\n", 
			getExecutableName());

		FOR_EACH_IN_STASHTABLE(gGlobalCmdList.sCmdsByName, Cmd, pCmd)
		{
			if ((pCmd->flags & CMDF_COMMANDLINE)
				&& !(pCmd->flags & CMDF_HIDEPRINT))
			{
				eaPush(&ppCmds, pCmd);
			}	
		}
		FOR_EACH_END;

		FOR_EACH_IN_STASHTABLE(gEarlyCmdList.sCmdsByName, Cmd, pCmd)
		{
			if (!(pCmd->flags & CMDF_HIDEPRINT))
			{
				eaPush(&ppCmds, pCmd);
			}
			
		}
		FOR_EACH_END;

		eaQSort(ppCmds, SortCmdsByOriginAndName);


		for (i=0; i < eaSize(&ppCmds); i++)
		{

			int iLen;
			int iNumSpaces;


			Cmd *pCmd = ppCmds[i];

			if (pCmd->origin != pLastOrigin)
			{
				printf("\n---------- Commands defined in %s -----------\n", pCmd->origin);
				pLastOrigin = pCmd->origin;
			}

			printf("-%s", pCmd->name);
			iLen = (int)strlen(pCmd->name);
			iNumSpaces = 30 - iLen;
			while (iNumSpaces <= 0)
			{
				iNumSpaces += 10;
			}

			for (j=0; j < iNumSpaces; j++)
			{
				printf(" ");
			}


			printf("%s\n", pCmd->comment);		
			
		}

	}
	else
	{
		Cmd *pCmd = cmdListFind(&gGlobalCmdList, pCommandName);
		if (!pCmd)
		{
			pCmd = cmdListFind(&gEarlyCmdList, pCommandName);
		}

		if (!pCmd || pCmd->flags & CMDF_HIDEPRINT || !(pCmd->flags & CMDF_COMMANDLINE))
		{
			printf("No help available for command %s. It does not exist, or is hidden,\nor is not allowed on the command line\n",
				pCommandName);
		}
		else
		{
			printf("Full help for command %s\n-----\n%s\n----\n",
				pCmd->name, pCmd->comment);
			printf("Access level: %d\nOrigin module: %s\nCategories: %s\n",
				pCmd->access_level, pCmd->origin, pCmd->categories);
			if (pCmd->handler)
			{
				int i;

				printf("This command DOES have a handler function, and expects %d arguments:\n",
					pCmd->iNumLogicalArgs);

				for (i=0; i < pCmd->iNumLogicalArgs; i++)
				{
					DataDesc *pArg = &pCmd->data[i];

					printf("Arg %d: %s (%s)\n", i, pArg->pArgName, MultiValTypeToReadableString(pArg->type));
				}
			}
			else
			{
				printf("This command does NOT have a handler function. It directly\nmodifies a variable of type %s\n",
					MultiValTypeToReadableString(pCmd->data[0].type));
			}
			
			if (pCmd->error_handler)
			{
				printf("This command has an error handling function to deal\nwith the case where it is called with 0 args\n");
			}
		}
	}

	exit(0);

#endif
}

//the command line -help menu
AUTO_COMMAND ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void help_cmd(char *pCommandName)
{
	// Just call the latelink so apps can provide their own help output if wanted.
	help(pCommandName);
}

int cmdParseForServerMonitor(CommandServingFlags eFlags, char *pCommand, int iAccessLevel, char **ppRetString, int iClientID, int iCommandRequestID, U32 iMCPID, SlowCmdReturnCallbackFunc *pSlowReturnCB, void *pSlowReturnUserData, const char *pAuthNameAndIP, bool *pbOutReturnIsSlow)
{


	CmdContext		cmd_context = {0};
	char *msg = NULL;
	int result = 0;
	bool bReturnIsSlow = false;

	cmd_context.flags |= CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL;
	cmd_context.eHowCalled = CMD_CONTEXT_HOWCALLED_SERVER_MONITORING;
	cmd_context.slowReturnInfo.iClientID = iClientID;
	cmd_context.slowReturnInfo.iCommandRequestID = iCommandRequestID;
	cmd_context.slowReturnInfo.iMCPID = iMCPID;
	cmd_context.slowReturnInfo.pSlowReturnCB = pSlowReturnCB;
	cmd_context.slowReturnInfo.pUserData = pSlowReturnUserData;
	cmd_context.slowReturnInfo.eFlags = eFlags;
	cmd_context.pAuthNameAndIP = pAuthNameAndIP;


	cmd_context.access_level = iAccessLevel;

	InitCmdOutput(cmd_context,msg);

	if (eFlags & CMDSRV_JSONRPC)
	{
		result = CmdParseJsonRPC(pCommand, &cmd_context);
	}
	else
	{
		result = cmdParseAndExecute(&gGlobalCmdList,pCommand,&cmd_context);
	}

	if (cmd_context.slowReturnInfo.bDoingSlowReturn)
	{
		assertmsg(pbOutReturnIsSlow, "Someone is trying to do a slow return for a command which should have no return at all");
		*pbOutReturnIsSlow = true;
	}
	else
	{
		if (ppRetString)
		{
			if (eFlags & CMDSRV_JSONRPC)
			{
				estrCopy(ppRetString, &msg);
			}
			else
			{
				if (result)
				{
					if (estrLength(&msg))
					{
						estrPrintf(ppRetString, "Command \"%s\" on server %s[%u] completed successfully. Return string:<br>%s",
							pCommand, GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID_ForCmdParse(), msg);
					}
					else
					{
						estrPrintf(ppRetString, "Command \"%s\" on server %s[%u] completed successfully. (No return string)",
							pCommand, GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID_ForCmdParse());
					}
				}
				else
				{
					estrPrintf(ppRetString, "Command \"%s\" on server %s[%u] FAILED",
						pCommand, GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID_ForCmdParse());
				}
			}
		}
	}

	CleanupCmdOutput(cmd_context);

	return result;
	
}


int cmdParseForClusterController(char *pCommand, int iAccessLevel, char **ppRetString, bool *pbReturnIsSlow, int iClientID, int iCommandRequestID, U32 iMCPID, SlowCmdReturnCallbackFunc *pSlowReturnCB, void *pSlowReturnUserData, const char *pAuthNameAndIP)
{
	CmdContext		cmd_context = {0};
	char *msg = NULL;
	int result = 0;
	bool bReturnIsSlow = false;

	cmd_context.flags |= CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL;
	cmd_context.eHowCalled = CMD_CONTEXT_HOWCALLED_CLUSTERCONTROLLER;
	cmd_context.slowReturnInfo.iClientID = iClientID;
	cmd_context.slowReturnInfo.iCommandRequestID = iCommandRequestID;
	cmd_context.slowReturnInfo.iMCPID = iMCPID;
	cmd_context.slowReturnInfo.pSlowReturnCB = pSlowReturnCB;
	cmd_context.slowReturnInfo.pUserData = pSlowReturnUserData;
	cmd_context.pAuthNameAndIP = pAuthNameAndIP;


	cmd_context.access_level = iAccessLevel;

	InitCmdOutput(cmd_context,msg);

	result = cmdParseAndExecute(&gGlobalCmdList,pCommand,&cmd_context);

	if (cmd_context.slowReturnInfo.bDoingSlowReturn)
	{
		assertmsg(pbReturnIsSlow, "Someone is trying to do a slow return for a command which should have no return at all");
		*pbReturnIsSlow = true;
	}
	else
	{
		if (ppRetString)
		{
			if (result)
			{
				if (estrLength(&msg))
				{
					estrPrintf(ppRetString, "Command \"%s\" on %s completed successfully. Return string:<br>%s",
						pCommand, GlobalTypeToName(GetAppGlobalType()), msg);
				}
				else
				{
					estrPrintf(ppRetString, "Command \"%s\" on %s completed successfully. (No return string)",
						pCommand, GlobalTypeToName(GetAppGlobalType()));
				}
			}
			else
			{
				estrPrintf(ppRetString, "Command \"%s\" on %s FAILED",
					pCommand, GlobalTypeToName(GetAppGlobalType()));
			}
		}
	}

	CleanupCmdOutput(cmd_context);

	return result;
}


void DoSlowCmdReturn(int iRetVal, const char *pRetString, CmdSlowReturnForServerMonitorInfo *pSlowInfo)
{
	char *pFullRetString = NULL;

	if (pSlowInfo->bForceLogReturn)
	{
		log_printf(LOG_COMMANDS, "Got return for slow command %d:%d:%u. Return value: %d. Return string: \"%s\"",
			pSlowInfo->iClientID, pSlowInfo->iCommandRequestID, pSlowInfo->iMCPID, 
			iRetVal, pRetString ? pRetString : "");
	}

	if (pSlowInfo->eHowCalled == CMD_CONTEXT_HOWCALLED_JSONRPC)
	{
		DoSlowCmdReturn_JsonRPC(pRetString, NULL, NULL, iRetVal, pSlowInfo);
	}
	else
	{
		estrStackCreate(&pFullRetString);

		if (iRetVal)
		{
			if (pSlowInfo->eHowCalled == CMD_CONTEXT_HOWCALLED_XMLRPC)
			{
				if (pRetString && pRetString[0])
				{
					if (strStartsWith(pRetString, "<?xml"))
					{
						estrPrintf(&pFullRetString, "%s", pRetString);
					}
					else
					{
						estrPrintf(&pFullRetString, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
							"<methodResponse><params><param><value><string>%s</string></value></param></params></methodResponse>",
							pRetString);
					}
				}
				else
				{
					estrPrintf(&pFullRetString, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
						"<methodResponse><params><param><value></value></param></params></methodResponse>");
				}
			}
			else
			{
				if (pRetString && pRetString[0])
				{
					estrPrintf(&pFullRetString, "Command on server %s[%u] completed successfully. Return string:<br><pre>%s</pre>",
						GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID_ForCmdParse(), pRetString);
				}
				else
				{
					estrPrintf(&pFullRetString, "Command on server %s[%u] completed successfully. (No return string)",
						GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID_ForCmdParse());
				}
			}
		}
		else
		{
			if (pSlowInfo->eHowCalled == CMD_CONTEXT_HOWCALLED_XMLRPC)
			{
				if (pRetString && pRetString[0])
				{
					if (strStartsWith(pRetString, "<?xml"))
					{
						estrPrintf(&pFullRetString, "%s", pRetString);
					}
					else
					{
						estrPrintf(&pFullRetString, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
							"<methodResponse><fault><value><struct>"
							"<member><name>faultCode</name><value><int>6</int></value></member>"
							"<member><name>faultString</name><value><string>%s</string></value></member>"
							"</struct></value></fault></methodResponse>",
							pRetString);
					}
				}
				else
				{
					estrPrintf(&pFullRetString, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
						"<methodResponse><fault><value><struct>"
						"<member><name>faultCode</name><value><int>6</int></value></member>"
						"<member><name>faultString</name><value><string>Could not complete slow remote command.</string></value></member>"
						"</struct></value></fault></methodResponse>");
				}
			}
			else
			{
				if (pRetString && pRetString[0])
				{
					estrPrintf(&pFullRetString, "Command on server %s[%u] FAILED. Return string:<br><pre>%s</pre>",
						GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), pRetString);
				}
				else
				{
					estrPrintf(&pFullRetString, "Command on server %s[%u] FAILED. (No return string)",
						GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID());
				}	
			}
		}

		if (pSlowInfo->pSlowReturnCB)
		{
			pSlowInfo->pSlowReturnCB(pSlowInfo->iMCPID, pSlowInfo->iCommandRequestID, pSlowInfo->iClientID, pSlowInfo->eFlags, pFullRetString, pSlowInfo->pUserData);
		}
		else
		{
			printf("NO SLOW RETURN CB: %s", pFullRetString);
		}

		estrDestroy(&pFullRetString);
	}
}


void DoSlowCmdReturn_Int(S64 iRetVal, CmdSlowReturnForServerMonitorInfo *pSlowInfo)
{
	if (pSlowInfo->eHowCalled == CMD_CONTEXT_HOWCALLED_JSONRPC)
	{
		DoSlowCmdReturn_JsonRPC(NULL, NULL, NULL, iRetVal, pSlowInfo);
	}
	else
	{
		char temp[32];
		sprintf(temp, "%I64d", iRetVal);
		DoSlowCmdReturn(1, temp, pSlowInfo);
	}
}

void DoSlowCmdReturn_Struct(ParseTable *pTPI, void *pStruct, CmdSlowReturnForServerMonitorInfo *pSlowInfo)
{
	assertmsgf(pSlowInfo->eHowCalled != CMD_CONTEXT_HOWCALLED_XMLRPC, "Slow return struct doesn't support xmlrpc, talk to Alex");

	if (pSlowInfo->eHowCalled == CMD_CONTEXT_HOWCALLED_JSONRPC)
	{
		DoSlowCmdReturn_JsonRPC(NULL, pTPI, pStruct, 0, pSlowInfo);
	}
	else
	{
		char *pTemp = NULL;
		estrStackCreate(&pTemp);
		ParserWriteTextEscaped(&pTemp, pTPI, pStruct, 0, 0, 0);
		DoSlowCmdReturn(1, pTemp, pSlowInfo);
		estrDestroy(&pTemp);
	}
}


int ParseCommandOutOfCommandLine_WithPrefix(char *pCommandName, char *pValString, int iValStringSize, char *pPrefix)
{
	const char *pCmdLine = GetCommandLineWithoutExecutable();
	char tempStr[256];
	char *pCmdOccurrence;
	char *pBegin, *pEnd;
	size_t cmdLen;
	char nextChar;

	sprintf(tempStr, "%s%s", pPrefix, pCommandName);
	cmdLen = strlen(tempStr);

	pCmdOccurrence = strstri(pCmdLine, tempStr);

	nextChar = pCmdOccurrence ? *(pCmdOccurrence + cmdLen) : 0;

	if (!pCmdOccurrence || nextChar && !IS_WHITESPACE(nextChar))
	{
		return false;
	}

	pBegin = pCmdOccurrence + cmdLen;
	while (*pBegin && IS_WHITESPACE(*pBegin))
	{
		pBegin++;
	}

	if (*pBegin == 0 || *pBegin == '-' || *pBegin == '+' || *pBegin == '?')
	{
		pValString[0] = '1';
		pValString[1] = '\0';
		return true;
	}

	pEnd = pBegin;
	while (*pEnd && !IS_WHITESPACE(*pEnd))
	{
		pEnd++;
	}

	if (pEnd - pBegin > iValStringSize - 1)
	{
		assert(0);
	}

	memcpy(pValString, pBegin, pEnd - pBegin);
	pValString[pEnd - pBegin] = 0;
	return true;
}


int ParseCommandOutOfCommandLineEx(char *pCommandName, char *pValString, int iValStringSize)
{
	if (ParseCommandOutOfCommandLine_WithPrefix(pCommandName, pValString, iValStringSize, "-"))
	{
		return true;
	}

	if (ParseCommandOutOfCommandLine_WithPrefix(pCommandName, pValString, iValStringSize, "-?"))
	{
		return true;
	}

	return false;
}



const char *GetContextHowString(CmdContext *pContext)
{
	return StaticDefineIntRevLookup(enumCmdContextHowCalledEnum, pContext->eHowCalled);
}

// "pretty prints" the command output and sends it to the given printfFunc.
// This will prefix commands that return numeric/bool results with "Cmd <commandname>: ".  
// Commands that return strings or structs are untouched.  
void cmdPrintPrettyOutput(CmdContext *pContext, CmdPrintfFunc printfFunc) {
	if (printfFunc && pContext) {
		if (estrLength(pContext->output_msg)) {
			MultiValType retType = pContext->found_cmd ? pContext->found_cmd->return_type.type : MULTI_NONE;
			switch (retType) {
				case MULTI_INT:
				case MULTI_FLOAT:
				case MULTI_VEC3:
				case MULTI_VEC4:
				case MULTI_QUAT:
					printfFunc("Cmd %s: %s\n", pContext->found_cmd->name, *pContext->output_msg);
					break;
				default:
					printfFunc("%s\n", *pContext->output_msg);
					break;
			}
		}
	}
}

int globCmdParseAndStripWhitespace(enumCmdContextHowCalled eHow, const char *str)
{
	char *copy;
	char *cursor;
	strdup_alloca(copy, str);
	cursor = copy + strlen(copy) - 1;
	while (*copy && IS_WHITESPACE(*copy))
		*copy++;
	while (cursor >= copy && IS_WHITESPACE(*cursor))
	{
		*cursor = '\0';
		cursor--;
	}
	if (*copy)
		return globCmdParseSpecifyHow(copy, eHow);
	else
		return 0;
}

int globCmdParsefAndStripWhitespace(enumCmdContextHowCalled eHow, const char *fmt, ...)
{
	int returnVal;
	va_list va;
	char *commandstr = NULL;
	estrStackCreate(&commandstr);

	va_start( va, fmt );
	estrConcatfv(&commandstr,fmt,va);
	va_end( va );

	returnVal = globCmdParseAndStripWhitespace(eHow, commandstr);

	estrDestroy(&commandstr);
	return returnVal;
}

int DEFAULT_LATELINK_cmdGetMinAccessLevelWhichForcesLogging(void)
{
	return 0;
}


void cmdGetPresumedCommandsFromCommandLine(char *pInCommandLine, char ***pppOutEarray)
{
	char **ppTokens = NULL;
	char *pCurCommand = NULL;
	int i;

	DivideString(pInCommandLine, " ", &ppTokens, 0);

	for (i = 0; i < eaSize(&ppTokens); i++)
	{
		char *pCurToken = ppTokens[i];

		if (!pCurToken || !pCurToken[0])
		{
			continue;
		}
			


		if (pCurToken[0] == '-' || pCurToken[0] == '+' || pCurToken[0] == MDASH)
		{
			if (estrLength(&pCurCommand))
			{
				eaPush(pppOutEarray, strdup(pCurCommand));
				estrDestroy(&pCurCommand);
			}

			if (pCurToken[1] == 0)
			{
				continue;
			}

			estrCopy2(&pCurCommand, pCurToken);

			while (pCurCommand[0] == '-' || pCurCommand[0] == '+' || pCurCommand[0] == '?' || pCurCommand[0] == MDASH)
			{
				estrRemove(&pCurCommand, 0, 1);
			}
		}
		else
		{
			estrConcatf(&pCurCommand, " %s", pCurToken);
		}
	}

	if (estrLength(&pCurCommand))
	{
		eaPush(pppOutEarray, strdup(pCurCommand));
		estrDestroy(&pCurCommand);
	}

	eaDestroyEx(&ppTokens, NULL);
}

void cmdParsePutStructListIntoPacket(Packet *pPkt, CmdParseStructList *pList, char *pComment)
{
	int iNumStructs = pList ? eaSize(&pList->ppEntries) : 0;
	int i;

	pktSendBits(pPkt, 32, iNumStructs);

	if (!iNumStructs)
	{
		return;
	}

	pktSendString(pPkt, pComment ? pComment : "(No comment specified for cmdParsePutStructListIntoPacket)");

	for (i = 0; i < iNumStructs; i++)
	{
		pktSendString(pPkt, ParserGetTableName(pList->ppEntries[i]->pTPI));
		if (pList->ppEntries[i]->pStruct)
		{
			pktSendBits(pPkt, 1, 1);
			ParserSendStruct(pList->ppEntries[i]->pTPI, pPkt, pList->ppEntries[i]->pStruct);
		}
		else
		{
			pktSendBits(pPkt, 1, 0);
		}
	}

}

void cmdClearStructList(CmdParseStructList *pList)
{
	if (!pList)
	{
		return;
	}

	FOR_EACH_IN_EARRAY(pList->ppEntries, CmdParseStructListEntry, pEntry)
	{
		if (pEntry->pStruct)
		{
			StructDestroyVoid(pEntry->pTPI, pEntry->pStruct);
		}
	}
	FOR_EACH_END;

	eaDestroyEx(&pList->ppEntries, NULL);
}

void cmdParseGetStructListFromPacket(Packet *pPkt, CmdParseStructList *pList, char **ppErrorString, bool bSourceIsUntrustworthy)
{
	int iNumStructs = pktGetBits(pPkt, 32);
	int i;
	CmdParseStructListEntry *pEntry;

	if (!iNumStructs)
	{
		return;
	}

	TextParser_SetGlobalStructCreationComment(pktGetStringTemp(pPkt));
	
	for (i = 0; i < iNumStructs; i++)
	{
		char *pStructName = pktGetStringTemp(pPkt);
		ParseTable *pTPI = ParserGetTableFromStructName(pStructName);

		if (!pTPI)
		{
			estrPrintf(ppErrorString, "Unknown TPI %s", pStructName);
			cmdClearStructList(pList);
			return;
		}

		pEntry = calloc(sizeof(CmdParseStructListEntry), 1);
		pEntry->pTPI = pTPI;
		eaPush(&pList->ppEntries, pEntry);

		if (pktGetBits(pPkt, 1))
		{	
			pEntry->pStruct = StructCreateVoid(pTPI);

			//
			if (!ParserRecv(pTPI, pPkt, pEntry->pStruct, RECVDIFF_FLAG_GET_GLOBAL_CREATION_COMMENT | (bSourceIsUntrustworthy ? RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE : 0)))
			{
				estrPrintf(ppErrorString, "Failed ParserRecv for %s", pStructName);
				cmdClearStructList(pList);
				return;
			}
		}

		
	}
}


void cmdDestroyUnownedStructList(CmdParseStructList *pList)
{
	eaDestroyEx(&pList->ppEntries, NULL);
}

void cmdAddToUnownedStructList(CmdParseStructList *pList, ParseTable *pTPI, void *pStruct)
{
	CmdParseStructListEntry *pEntry = malloc(sizeof(CmdParseStructListEntry));

	pEntry->pTPI = pTPI;
	pEntry->pStruct = pStruct;
	eaPush(&pList->ppEntries, pEntry);
}

AUTO_COMMAND ACMD_NAME(BranchOnAutoInt) ACMD_CLIENTCMD ACMD_HIDE ACMD_ACCESSLEVEL(0);
void cmdBranchOnAutoInt(const char* pchAutoIntName, const char* cmdIfTrue, const char* cmdIfFalse)
{
	Cmd* pCmd = cmdListFind(&gGlobalCmdList,pchAutoIntName);
	if (pCmd && pCmd->data && (pCmd->data[0].type == MULTI_INT || pCmd->data[0].type == MULTI_FLOAT))
	{
		bool bEvaluatedVar = false;
		MultiVal temp = {0};
		switch (pCmd->data[0].type)
		{
			case MULTI_INT:
				{
					temp.type = pCmd->data[0].type;
					MultiValReadInt(&temp,pCmd->data[0].ptr,pCmd->data[0].data_size);
					bEvaluatedVar = !!MultiValGetInt(&temp, NULL);
				}break;
			case MULTI_FLOAT:
				{
					bEvaluatedVar = !!*((F32 *)pCmd->data[0].ptr);
				}break;
			default:
				Errorf("BranchOnAutoInt called with non-int/float var %s.", pchAutoIntName);
		}
		if (bEvaluatedVar)
			globCmdParse(cmdIfTrue);
		else
			globCmdParse(cmdIfFalse);
	}
	else
	{
		Errorf("BranchOnAutoInt called with unrecognized auto_int var %s.", pchAutoIntName);
	}
}

void cmdUpdateAccessLevelsFromCommaSeparatedString(CmdList *pList, char *pListName, char *pString, int iMaxToSet)
{
	char **ppLines = NULL;
	int iLineNum;

	printf("\nAbout to try to apply override access levels to list %s\nInput string is: %s\n", pListName, pString);

	FOR_EACH_IN_STASHTABLE(pList->sCmdsByName, Cmd, pCmd)
	{
		if (pCmd->access_level != pCmd->original_access_level)
		{
			printf("Command %s had been overridden from %d to %d, restoring it to original value (may override it right back in a moment)\n",
				pCmd->name, pCmd->original_access_level, pCmd->access_level);
			pCmd->access_level = pCmd->original_access_level;
		}
		
	}
	FOR_EACH_END;

	DivideString(pString, ",", &ppLines, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	for (iLineNum = 0; iLineNum < eaSize(&ppLines); iLineNum++)
	{
		char *pCurLine = ppLines[iLineNum];
		char **ppWords = NULL;
		int iAccessLevelToSet;

		DivideString(pCurLine, " ", &ppWords, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

		if (eaSize(&ppWords) != 2)
		{
			printf("Error: line \"%s\" is not properly formatted (should be \"doSomeCmd n\")\n", pCurLine);

		}
		else if (!StringToInt_Paranoid(ppWords[1], &iAccessLevelToSet))
		{
			printf("Error: Unrecognized presumed integer %s\n", ppWords[1]);

		}
		else if (!(iAccessLevelToSet >= 0 && iAccessLevelToSet <= iMaxToSet))
		{
			printf("Error: Access level %d to set is out of range (should be 0 <= n <= %d)\n", iAccessLevelToSet, iMaxToSet);
		}
		else
		{
			Cmd *pCmd = cmdListFind(pList, ppWords[0]);
			if (!pCmd)
			{
				printf("Did not find command %s, likely it was in the other list (global vs. private)\n", ppWords[0]);
			}
			else if (pCmd->access_level > iMaxToSet)
			{
				printf("Error: Can't set access level of %s, it's access level %d already which is greater than our max settable value %d\n", ppWords[0],
					pCmd->access_level, iMaxToSet);
			}
			else
			{
				printf("Successfully setting %s's AL to %d\n", ppWords[0], iAccessLevelToSet);
				pCmd->access_level = iAccessLevelToSet;
			}
		}
		

		eaDestroyEx(&ppWords, NULL);		
	}
	eaDestroyEx(&ppLines, NULL);	
}

#define MIN_CMDPARSEBLOCK_SIZE_BITS 4 //sizes always rounded up to 1 << 4, ie 16
#define MAX_CMDPARSEBLOCK_SIZE_BITS 25 //sizes > 32 megs, on the off chance they somehow show up, get normal heap allocations

#define CMDPARSEBLOCK_NUM_BLOCK_SIZES (MAX_CMDPARSEBLOCK_SIZE_BITS - MIN_CMDPARSEBLOCK_SIZE_BITS + 1)

static SLIST_HEADER sCmdParseBlockListHeaders[CMDPARSEBLOCK_NUM_BLOCK_SIZES] = {0};

#define CMDPARSEALLOCHEADER_MAGICNUMBER 0xfdcb

typedef struct CmdParseAllocHeader
{
	SLIST_ENTRY ItemEntry;
	int iMagicNumber;
	int iListIndex;
} CmdParseAllocHeader;


AUTO_RUN_FIRST;
void cmdParseAllocInit(void)
{
	int i;

	for (i = 0; i < CMDPARSEBLOCK_NUM_BLOCK_SIZES; i++)
	{
		InitializeSListHead(&sCmdParseBlockListHeaders[i]);
	}
}


void *cmdParseAlloc(int iSize)
{

	int iHighBitIndex;
	int iActualListIndex;
	CmdParseAllocHeader *pInternalHeader;

	if (iSize <= (1 << MIN_CMDPARSEBLOCK_SIZE_BITS))
	{
		iHighBitIndex = MIN_CMDPARSEBLOCK_SIZE_BITS;
	}
	else
	{
		iHighBitIndex = highBitIndex(iSize - 1) + 1; //4 = 16 bytes, etc

		if (iHighBitIndex > MAX_CMDPARSEBLOCK_SIZE_BITS)
		{
			pInternalHeader = (CmdParseAllocHeader*)malloc(iSize + sizeof(CmdParseAllocHeader));
			pInternalHeader->iListIndex = CMDPARSEBLOCK_NUM_BLOCK_SIZES;
			pInternalHeader->iMagicNumber = CMDPARSEALLOCHEADER_MAGICNUMBER;
			return ((char*)pInternalHeader) + sizeof(CmdParseAllocHeader);
		}
	}


	

	if (iHighBitIndex < MIN_CMDPARSEBLOCK_SIZE_BITS)
	{
		iHighBitIndex = MIN_CMDPARSEBLOCK_SIZE_BITS;
	}

	iActualListIndex = iHighBitIndex - MIN_CMDPARSEBLOCK_SIZE_BITS;

	pInternalHeader =(CmdParseAllocHeader*)InterlockedPopEntrySList(&sCmdParseBlockListHeaders[iActualListIndex]);
	if (!pInternalHeader)
	{
        pInternalHeader = (CmdParseAllocHeader*)aligned_malloc_dbg((size_t)(1 << iHighBitIndex) + sizeof(CmdParseAllocHeader), 16, __FILE__, __LINE__);
		pInternalHeader->iListIndex = iActualListIndex;
		pInternalHeader->iMagicNumber = CMDPARSEALLOCHEADER_MAGICNUMBER;
	}
	return ((char*)pInternalHeader) + sizeof(CmdParseAllocHeader);
}

void cmdParseFree(void **ppBuffer)
{
	if (!ppBuffer || !(*ppBuffer))
	{
		return;
	}
	else
	{
		CmdParseAllocHeader *pInternalHeader = (CmdParseAllocHeader*)(((char*)(*ppBuffer)) - sizeof(CmdParseAllocHeader));
		assert(pInternalHeader->iMagicNumber == CMDPARSEALLOCHEADER_MAGICNUMBER);
		if (pInternalHeader->iListIndex == CMDPARSEBLOCK_NUM_BLOCK_SIZES)
		{
			free(pInternalHeader);
		}
		else
		{
			assert(pInternalHeader->iListIndex >= 0 && pInternalHeader->iListIndex < CMDPARSEBLOCK_NUM_BLOCK_SIZES);
			InterlockedPushEntrySList(&sCmdParseBlockListHeaders[pInternalHeader->iListIndex], (void*)pInternalHeader);
		}

		*ppBuffer = NULL;
	}
}
/*
AUTO_RUN_SECOND;
void CmdParseAllocTest(void)
{
	int i;

	for (i = 1; i < 50; i++)
	{
		void *pTemp = cmdParseAlloc(i);
		cmdParseFree(pTemp);
	}
}*/

AUTO_COMMAND;
void CmdParseAllocPrintReport(void)
{
	int i;
	char *pSize1 = NULL;
	char *pSize2 = NULL;
	S64 iTotal = 0;

	for (i = 0; i < CMDPARSEBLOCK_NUM_BLOCK_SIZES; i++)
	{
		int iSize = (1 << (i + MIN_CMDPARSEBLOCK_SIZE_BITS));
		int iNumBlocks = QueryDepthSList(&sCmdParseBlockListHeaders[i]);
		iTotal += iSize * iNumBlocks;

		estrMakePrettyBytesString(&pSize1, iSize);
		estrMakePrettyBytesString(&pSize2, iSize * iNumBlocks);

		printf("Have %d blocks of size %s, totalling %s\n", iNumBlocks, pSize1, pSize2);
	}

	estrMakePrettyBytesString(&pSize1, iTotal);

	printf("Total allocated: %s\n", pSize1);
}

void RemoveCmdsFromOtherProductsFromStashTable(StashTable sCommandTable, const char *pProductName, StashTable sWhereToPutThem)
{
	StashTableIterator stashIterator;
	StashElement element;

	stashGetIterator(sCommandTable, &stashIterator);

	while (stashGetNextElement(&stashIterator, &element))
	{
		Cmd *pCmd = stashElementGetPointer(element);
		char **ppProducts = NULL;
		int iFoundIndex;

		if (!pCmd->pProductNames)
		{
			continue;
		}

		DivideString(pCmd->pProductNames, ",", &ppProducts, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
		iFoundIndex = eaFindString(&ppProducts, pProductName);

		eaDestroyEx(&ppProducts, NULL);

		if (iFoundIndex == -1)
		{
			stashRemovePointer(sCommandTable, stashElementGetStringKey(element), NULL);

			if (sWhereToPutThem)
			{
				stashAddPointer(sWhereToPutThem, pCmd->name, pCmd, false);
			}
		}
	}
}

void RemoveCmdsFromOtherProducts(CmdList *pCmdList, const char *pProductName)
{
	int iLanguage;

	if (!pCmdList->sCmdsNotInThisProduct)
	{
		pCmdList->sCmdsNotInThisProduct = stashTableCreateWithStringKeys(8, StashDefault); // All names should be static strings, no deep copies!
	}

	RemoveCmdsFromOtherProductsFromStashTable(pCmdList->sCmdsByName, pProductName, pCmdList->sCmdsNotInThisProduct);

	for (iLanguage = 0; iLanguage < LANGUAGE_MAX; iLanguage++)
	{
		if (pCmdList->sCmdsByName_Translated[iLanguage])
		{
			RemoveCmdsFromOtherProductsFromStashTable(pCmdList->sCmdsByName_Translated[iLanguage], pProductName, NULL);
		}
	}
}

//the product name has now been set... go through all command lists and remove all commands that should not exist for this project
void cmdParseHandleProductNameSet(const char *pProductName)
{
	RemoveCmdsFromOtherProducts(&gGlobalCmdList, pProductName);
	RemoveCmdsFromOtherProducts(&gPrivateCmdList, pProductName);
}

StashTable sCommandsForCSVExport = NULL;

static void AddCommandsFromStashTableForCSVExport(StashTable sTable, bool bClient, bool bServer, bool bPrivate, bool bEarly, bool bInThisProduct)
{
	FOR_EACH_IN_STASHTABLE(sTable, Cmd, pCmd)
	{
		int i = 0;

		CommandForCSV *pCommandForCSV = StructCreate(parse_CommandForCSV);
		pCommandForCSV->pName = pCmd->name;
		pCommandForCSV->iAccessLevel = pCmd->access_level;

		
		if ((pCmd->flags & (CMDF_CONTROLLERAUTOSETTING | CMDF_AUTOSETTING_NONCONTROLLER)) && !pCmd->pSourceFileName[0])
		{
			pCommandForCSV->pSourceFile = "AUTO_SETTING";
		}
		else
		{
			pCommandForCSV->pSourceFile = pCmd->pSourceFileName;
		}
		pCommandForCSV->iLineNum = pCmd->iSourceLineNum;
		pCommandForCSV->bExistsOnClient = bClient;
		pCommandForCSV->bExistsOnServer = bServer;
		pCommandForCSV->bHidden = !!(pCmd->flags & CMDF_HIDEPRINT);
		pCommandForCSV->bPrivate = bPrivate;
		pCommandForCSV->bCommandLineOnly = !!(pCmd->flags & CMDF_COMMANDLINEONLY);
		pCommandForCSV->bEarlyCommandLine = bEarly;
		pCommandForCSV->bExistsInCurrentProduct = bInThisProduct;
		pCommandForCSV->pProductString = pCmd->pProductNames;
		pCommandForCSV->pComment = pCmd->comment;

		while (stashFindPointer(sCommandsForCSVExport, pCommandForCSV->pName, NULL))
		{
			char temp[256];
			i++;
			sprintf(temp, "%s__NAMEOVERLAP_%d", pCmd->name, i);
			pCommandForCSV->pName = allocAddString(temp);
		}

		stashAddPointer(sCommandsForCSVExport, pCommandForCSV->pName, pCommandForCSV, true);
	}
	FOR_EACH_END;
}

static void AddCommandsFromListForCSVExport(CmdList *pCmdList, bool bClient, bool bServer, bool bPrivate, bool bEarly)
{
	if (pCmdList->sCmdsByName)
	{
		AddCommandsFromStashTableForCSVExport(pCmdList->sCmdsByName, bClient, bServer, bPrivate, bEarly, true);
	}

	

	if (pCmdList->sCmdsNotInThisProduct)
	{
		AddCommandsFromStashTableForCSVExport(pCmdList->sCmdsNotInThisProduct, bClient, bServer, bPrivate, bEarly, false);
	}


}

AUTO_COMMAND;
void MakeCommandsForCSV(void)
{
	bool bClient = (GetAppGlobalType() == GLOBALTYPE_CLIENT);
	bool bServer = (GetAppGlobalType() == GLOBALTYPE_GAMESERVER);

	if (sCommandsForCSVExport)
	{
		return;
	}

	sCommandsForCSVExport = stashTableCreateWithStringKeys(16, StashDefault);
	resRegisterDictionaryForStashTable("CommandsForCSV", RESCATEGORY_SYSTEM, 0, sCommandsForCSVExport, parse_CommandForCSV);


	AddCommandsFromListForCSVExport(&gGlobalCmdList, 
		bClient, bServer, false, false);
	AddCommandsFromListForCSVExport(&gPrivateCmdList, 
		bClient, bServer, true, false);
	AddCommandsFromListForCSVExport(&gEarlyCmdList, 
		bClient, bServer, false, true);

}

static void WriteStringForCSV(FILE *pOutFile, const char *pStr)
{
	static char *pTemp = NULL;
	estrClear(&pTemp);

	estrAppendEscaped(&pTemp, pStr ? pStr : "");

	fprintf(pOutFile, "\"%s\"", pTemp);
}


void ExportCommandsForCSV_Internal(void)
{
	FILE *pOutFile; 
	char fileName[CRYPTIC_MAX_PATH];

	if (!sCommandsForCSVExport)
	{
		return;
	}

	sprintf(fileName, "%s/Commands_%s.csv",
		fileLocalDataDir(), GlobalTypeToName(GetAppGlobalType()));

	mkdirtree_const(fileName);
	pOutFile = fopen(fileName, "wt");

	if (!pOutFile)
	{
		Errorf("Can't open %s for writing\n", fileName);
		return;
	}

	fprintf(pOutFile, "Name, AccessLevel, SourceFile, LineNum, ExistsOnClient, ExistsOnServer, ClientChatAutoComplete, Hidden, Private, ExistsInCurrentProduct, CommandLineOnly, EarlyCommandLine, ProductString, Comment\n");

	FOR_EACH_IN_STASHTABLE(sCommandsForCSVExport, CommandForCSV, pCommand)
	{
		WriteStringForCSV(pOutFile, pCommand->pName);
		fprintf(pOutFile, ", %d, ", pCommand->iAccessLevel);
		WriteStringForCSV(pOutFile, pCommand->pSourceFile);
		
		fprintf(pOutFile, ", %d, %d, %d, %d, %d, %d, %d, %d, %d, ",
			pCommand->iLineNum, pCommand->bExistsOnClient, pCommand->bExistsOnServer, pCommand->bChatAutoComplete,
			pCommand->bHidden, pCommand->bPrivate, pCommand->bExistsInCurrentProduct,
			pCommand->bCommandLineOnly, pCommand->bEarlyCommandLine);
		WriteStringForCSV(pOutFile, pCommand->pProductString);
		fprintf(pOutFile, ", ");
		WriteStringForCSV(pOutFile, pCommand->pComment);
		fprintf(pOutFile, "\n");
	}
	FOR_EACH_END;

	fclose(pOutFile);
}

bool DEFAULT_LATELINK_ExportCSVCommandFile_Special(void)
{
	return false;
}

AUTO_COMMAND;
void ExportCSVCommandFile(void)
{
	if (ExportCSVCommandFile_Special())
	{
		return;
	}

	MakeCommandsForCSV();
	ExportCommandsForCSV_Internal();
	ControllerScript_Succeeded();
}

static char *CmdGetSummary(Cmd *pCmd, bool bPrivate, bool bEarlyCommandLine)
{
	static char *spRetVal = NULL;

	estrPrintf(&spRetVal, "Command %s, AL(%d)\nIn code: %s(%d)\n",
		pCmd->name, pCmd->access_level, pCmd->pSourceFileName, pCmd->iSourceLineNum);
	if (bPrivate)
	{
		estrConcatf(&spRetVal, "PRIVATE ");
	}
	if (bEarlyCommandLine)
	{
		estrConcatf(&spRetVal, "EARLY_CMD_LINE ");
	}
	if (pCmd->flags & CMDF_HIDEPRINT)
	{
		estrConcatf(&spRetVal, "HIDDEN ");
	}
	if (pCmd->flags & CMDF_COMMANDLINEONLY)
	{
		estrConcatf(&spRetVal, "CMD_LINE_ONLY ");
	}
	if (pCmd->flags & (CMDF_CONTROLLERAUTOSETTING | CMDF_AUTOSETTING_NONCONTROLLER))
	{
		estrConcatf(&spRetVal, "CMD_LINE_ONLY ");
	}
	estrConcatf(&spRetVal, "\n");

	if (pCmd->pProductNames)
	{
		estrConcatf(&spRetVal, "Products: %s\n", pCmd->pProductNames);
	}

	return spRetVal;
}



char *cmdGetInfo_Internal(char *pCommandName)
{
	Cmd *pCmd = cmdListFind(&gGlobalCmdList, pCommandName);
	if (pCmd)
	{
		return CmdGetSummary(pCmd, false, false);
	}

	pCmd = cmdListFind(&gPrivateCmdList, pCommandName);
	if (pCmd)
	{
		return CmdGetSummary(pCmd, true, false);		
	}

	pCmd = cmdListFind(&gEarlyCmdList, pCommandName);
	if (pCmd)
	{
		return CmdGetSummary(pCmd, false, true);
	}
	
	return NULL;
}


void DEFAULT_LATELINK_CmdPrintInfoInternal(void *pCommandName_in, void *pContext_in)
{
	char *pCommandName = (char*)pCommandName_in;
	CmdContext *pContext = (CmdContext*)pContext_in;

	char *pInfo = cmdGetInfo_Internal(pCommandName);
	if (pInfo)
	{
		printf("%s\n", pInfo);
	}
	else
	{
		printf("Command %s is unknown\n", pCommandName);
	}
}

AUTO_COMMAND;
void CmdPrintInfo(char *pCommandName, CmdContext *pContext)
{
	CmdPrintInfoInternal(pCommandName, pContext);
}


bool cmdExistsButIsAccessLevelOverridden(CmdList *cmd_list, char *pFullCommand /*including args*/, int iAccessLevel, Language language)
{
	char *pCmdName = NULL;
	Cmd *cmd;

	estrStackCreate(&pCmdName);
	estrCopy2(&pCmdName, pFullCommand);
	estrTruncateAtFirstOccurrence(&pCmdName, ' ');

	cmd = cmdListFind(cmd_list,pCmdName);
	if (!cmd)
		cmd = cmdListFindTranslated(cmd_list, pCmdName, language);

	estrDestroy(&pCmdName);

	if (!cmd)
	{
		return false;
	}

	if (cmd->access_level == cmd->original_access_level)
	{
		return false;
	}

	if (cmd->original_access_level <= iAccessLevel)
	{
		return true;
	}

	return false;
}


int cmdParseGetIntDefaultValueForArg(DataDesc *pArg)
{
	if (!(pArg->flags & CMDAF_HAS_DEFAULT))
	{
		return 0;
	}

	return (int)((intptr_t)(pArg->ppNameListData));
}

//returns "" if there isn't one
char *cmdParseGetStringDefaultValueForArg(DataDesc *pArg)
{
	char *pRetVal;
	if (!(pArg->flags & CMDAF_HAS_DEFAULT))
	{
		return "";
	}

	pRetVal = (char*)(pArg->ppNameListData);
	if (!pRetVal)
	{
		return "";
	}
	return pRetVal;
}




static void DivideStringByDoubleHashes(char ***pppOutStrings, char *pInString)
{
	char *pTemp = NULL;
	char *pFound;

	while ((pFound = strstr(pInString, "##")))
	{
		estrConcat(&pTemp, pInString, pFound - pInString);
		estrTrimLeadingAndTrailingWhitespace(&pTemp);
		eaPush(pppOutStrings, strdup(pTemp));
		estrDestroy(&pTemp);

		pInString = pFound + 2;
	}


	estrCopy2(&pTemp, pInString);
	estrTrimLeadingAndTrailingWhitespace(&pTemp);
	eaPush(pppOutStrings, strdup(pTemp));
	estrDestroy(&pTemp);
}

static int DoToggling(char *pInString, int iSize)
{
	static StashTable sTogglingTable = NULL;
	int iFound;

	if (!sTogglingTable)
	{
		sTogglingTable = stashTableCreateWithStringKeys(4, StashDeepCopyKeys_NeverRelease);
	}

	if (stashFindInt(sTogglingTable, pInString, &iFound))
	{
		stashAddInt(sTogglingTable, pInString, iFound + 1, true);
		return iFound % iSize;
	}
	else
	{
		stashAddInt(sTogglingTable, pInString, 1, true);
		return 0;
	}
}



/*
this command is used for little "scripted" commands/macros that are presumably attacked to keyboard aliases. Its syntax is this:

multiCommand "switchCommand ## choice1 # action1a # action1B ## choice2 # action2 ## ... ## choiceN # actionN ## default # defaultActiona # defaultActionb"

It first executes switchCommand and gets the result out, then basically does a C-style switch statement, looking for a string
in choice1 or choice2 or whatever that matches the result of switchCommand. If it finds one, it executes one or more commands,
with the individual commands separated by #. If it doesn't, it executes one or more defaultActions. (Default is optional).

Special case: if the switch command is "toggle", then the choices are ignored (but must still be present), and it it just switches between them each
time the identical command is executed
*/

//special command for making macros that have conditional behaviors... talk to Alex W
AUTO_COMMAND;
void multiCommand(CmdContext *pContext, ACMD_SENTENCE pCommandString)
{
	static char **ppStrings = NULL;
	static char *pRetVal = NULL;
	int iRetVal;
	int i;
	bool bDoToggle = false;
	int iToggleCase = 0;

	estrDestroy(&pRetVal);
	eaDestroyEx(&ppStrings, NULL);

	DivideStringByDoubleHashes(&ppStrings, pCommandString);

	if (eaSize(&ppStrings) < 3)
	{
		printf("multiCommand syntax syntax incorrect... need at least 3 commands separated by ##");
		return;
	}

	if (stricmp_safe(ppStrings[0], "toggle") == 0)
	{
		bDoToggle = true;
		printf("Going to do toggle\n");
		iToggleCase = DoToggling(pCommandString, eaSize(&ppStrings) - 1) + 1;
	}
	else
	{
		printf("For multiCommand, going to execute %s\n", ppStrings[0]);
		iRetVal = globCmdParseAndReturnWithFlagsAndOverrideAccessLevel(ppStrings[0], &pRetVal, 0, pContext->access_level, CMD_CONTEXT_HOWCALLED_MULTICOMMAND);
		if (!iRetVal && !estrLength(&pRetVal))
		{
			printf("Execution failed with no return string\n");
			return;
		}
		else
		{
			printf("Succeeded... returned: %s\n", pRetVal);

			estrTrimLeadingAndTrailingWhitespace(&pRetVal);

			//special case... executing some simple commands returns the command name along
			//with the result... ie, executing WorldEditorEnableTransSnap returns 
			//"WorldEditorEnableTransSnap 0" instead of "0". Remove that.
			if (strStartsWith(pRetVal, ppStrings[0]))
			{
				estrRemove(&pRetVal, 0, (int)strlen(ppStrings[0]));
				estrTrimLeadingAndTrailingWhitespace(&pRetVal);
				printf("Trimmed return value to: %s\n", pRetVal);
			}
		}
	}

	for (i = 1; i < eaSize(&ppStrings); i++)
	{
		char *pCaseString = ppStrings[i];
		static char **ppSubStrings = NULL;
		bool bMatched = false;
		bool bDefault = false;

		eaDestroyEx(&ppSubStrings, NULL);
		DivideString(pCaseString, "#", &ppSubStrings, DIVIDESTRING_STANDARD);

		if (eaSize(&ppSubStrings) < 2)
		{
			printf("Bad multicommand syntax in case %s\n", pCaseString);
			return;
		}

		if (bDoToggle)
		{
			if (i == iToggleCase)
			{
				bMatched = true;
				printf("Toggling happened: decided we should executed %s\n", ppSubStrings[1]);
			}
		}
		else
		{

			if (stricmp_safe(ppSubStrings[0], "default") == 0)
			{
				if (i == eaSize(&ppStrings) - 1)
				{
					printf("Bad multicommand syntax... default must come last\n");
					return;
				}
		
				printf("Matched a default... going to execute commands, starting with %s\n", ppSubStrings[1]);
				bDefault = true;
			}
			else if (StringsMatchRespectingNumbers(pRetVal, ppSubStrings[0]))
			{
				printf("Matched %s... going to execute commands starting with %s\n", ppSubStrings[0], ppSubStrings[1]);
				bMatched = true;
			}
		}

		if (bMatched || bDefault)
		{
			for (i = 1; i < eaSize(&ppSubStrings); i++)
			{
				printf("Executing: %s\n", ppSubStrings[i]);
				estrClear(&pRetVal);		
				globCmdParseAndReturnWithFlagsAndOverrideAccessLevel(ppSubStrings[i], &pRetVal, 0, pContext->access_level, CMD_CONTEXT_HOWCALLED_MULTICOMMAND);
		
				printf("Return value: %s\n", pRetVal);
			}
			return;
		}
	}

	printf("No default case, matched nothing, doing nothing\n");
}


#include "autogen/cmdparse_h_ast.c"
