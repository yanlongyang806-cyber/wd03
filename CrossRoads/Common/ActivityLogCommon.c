/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ActivityLogCommon.h"
#include "GameStringFormat.h"
#include "Guild.h"

#include "AutoGen/ActivityLogEnums_h_ast.h"
#include "AutoGen/ActivityLogCommon_h_ast.h"
#include "AutoGen/CombatEnums_h_ast.h"

#include "error.h"

static ActivityLogConfig s_ActivityLogConfig;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//
// Load the activity log config file
//
AUTO_STARTUP(AS_ActivityLogConfig) ASTRT_DEPS(AS_ActivityLogEntryTypes, AS_CharacterClassTypes);
void
ActivityLog_LoadConfig(void)
{
	loadstart_printf("Loading ActivityLogConfigs...");
	StructInit(parse_ActivityLogConfig, &s_ActivityLogConfig);

	ParserLoadFiles(NULL, "defs/config/ActivityLogConfig.def", "ActivityLogConfig.bin", PARSER_OPTIONALFLAG, parse_ActivityLogConfig, &s_ActivityLogConfig);
	loadend_printf("done.");
}

//
// Get the config struct for a given entry type
//
AUTO_TRANS_HELPER_SIMPLE;
ActivityLogEntryTypeConfig *
ActivityLog_GetTypeConfig(ActivityLogEntryType entryType)
{
	return eaIndexedGetUsingInt(&s_ActivityLogConfig.entryTypeConfigs, entryType);
}

ActivityLogPetEventConfig *
ActivityLog_GetPetEventConfig(CharClassTypes petType)
{
	return eaIndexedGetUsingInt(&s_ActivityLogConfig.petEventConfigs, petType);
}

//
// Get the max guild activity log size
//
int
ActivityLog_GetMaxGuildLogSize(void)
{
	return s_ActivityLogConfig.maxGuildLogSize;
}

//
// Return a list of all entry type names.
//
const char ***
ActivityLog_GetEntryTypeNames(void)
{
	static const char **s_entryTypeNames = NULL;

	if ( s_entryTypeNames == NULL )
	{
		DefineFillAllKeysAndValues(ActivityLogEntryTypeEnum, &s_entryTypeNames, NULL);
	}

	return &s_entryTypeNames;
}



void ActivityLog_FormatEntry(Language lang, char **estrDest, ActivityLogEntry *pLogEntry, Guild *pGuild) 
{
	ActivityLogEntryTypeConfig *pActivityEntryTypeConfig = ActivityLog_GetTypeConfig(pLogEntry->type);
	if (pActivityEntryTypeConfig)
	{
		const char *subjectName;
		if (pLogEntry->subjectID != 0)
		{
			GuildMember *guildMember = eaIndexedGetUsingInt(&pGuild->eaMembers, pLogEntry->subjectID);
			if ( guildMember == NULL )
			{
				return;
			}
			subjectName = guildMember->pcName;
		}
		else
		{
			subjectName = "";
		}
		if (pActivityEntryTypeConfig->splitArgString) 
		{
			char *tmpString1 = NULL;
			char *tmpString2 = NULL;
			const char *argString1;
			const char *argString2;

			estrAppend2(&tmpString1, pLogEntry->argString);
			estrAppend2(&tmpString2, pLogEntry->argString);

			estrTruncateAtFirstOccurrence(&tmpString1, ':');
			estrRemoveUpToFirstOccurrence(&tmpString2, ':');

			if (pActivityEntryTypeConfig->translateArg)
			{
				argString1 = TranslateMessageKey(tmpString1);
			}
			else
			{
				argString1 = tmpString1;
			}

			if (pActivityEntryTypeConfig->translateArg2)
			{
				argString2 = TranslateMessageKey(tmpString2);
			}
			else
			{
				argString2 = tmpString2;
			}

			langFormatGameDisplayMessage(lang, estrDest, &pActivityEntryTypeConfig->guildLogDisplayStringMsg, 
				STRFMT_STRING("SubjectName", subjectName), 
				STRFMT_STRING("ArgString1", argString1),
				STRFMT_STRING("ArgString2", argString2),
				STRFMT_END);

			estrDestroy(&tmpString1);
			estrDestroy(&tmpString2);
		}
		else
		{
			const char *argString;
			if (pLogEntry->type == ActivityLogEntryType_GuildRankChange)
			{
				argString = pLogEntry->argString;
				if (argString && argString[0] == '\33')
				{
					S32 rankNum = atoi(argString+1);
					GuildCustomRank *customRank = eaGet(&pGuild->eaRanks, rankNum);
					if (customRank)
					{
						if (customRank->pcDisplayName)
							argString = customRank->pcDisplayName;
						else
							argString = TranslateMessageKey(customRank->pcDefaultNameMsg);
					}
					else
					{
						argString = NULL;
					}
				}
			}
			else if (pActivityEntryTypeConfig->translateArg)
			{
				argString = TranslateMessageKey(pLogEntry->argString);
			}
			else
			{
				argString = pLogEntry->argString;
			}
			langFormatGameDisplayMessage(lang, estrDest, &pActivityEntryTypeConfig->guildLogDisplayStringMsg, 
				STRFMT_STRING("SubjectName", subjectName),
				STRFMT_STRING("ArgString", argString),
				STRFMT_END);
		}
	}
}

#include "ActivityLogCommon_h_ast.c"
