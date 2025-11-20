/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "DiaryCommon.h"
#include "DiaryEnums.h"

#include "error.h"
#include "objSchema.h"
#include "AutoGen/DiaryEnums_h_ast.h"
#include "AutoGen/mission_enums_h_ast.h"
#include "AutoGen/DiaryCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static DiaryConfig s_DiaryConfig;

AUTO_STARTUP(DiaryDefs);
void DiaryConfig_LoadDefs(void)
{
	loadstart_printf("Loading DiaryConfig...");
	StructInit(parse_DiaryConfig, &s_DiaryConfig);
	ParserLoadFiles(NULL, "defs/config/DiaryConfig.def", "DiaryConfig.bin", 0, parse_DiaryConfig, &s_DiaryConfig);
	loadend_printf("done.");
}

DiaryTagStrings *
DiaryConfig_CopyDefaultTagStrings()
{
	int i;
	int n;
	DiaryTagStrings *tagStrings = StructCreate(parse_DiaryTagStrings);

	n = eaSize(&s_DiaryConfig.defaultTags);
	for ( i = 0; i < n; i++ )
	{
		DiaryTagString *defaultTag = s_DiaryConfig.defaultTags[i];
		NOCONST(DiaryTagString) *tagString = StructCreateNoConst(parse_DiaryTagString);
		tagString->permanent = defaultTag->permanent;
		tagString->tagName = defaultTag->tagName;

		eaPush(&tagStrings->tagStrings, (DiaryTagString *)tagString);
	}

	return tagStrings;
}

bool
DiaryConfig_AutoAddCurrentMissionType(MissionType type)
{
	DiaryMissionTypeConfig *typeConfig = eaIndexedGetUsingInt(&s_DiaryConfig.missionTypeConfigs, type);

	return typeConfig->autoAddCurrent;
}

bool
DiaryConfig_AutoAddCompletedMissionType(MissionType type)
{
	DiaryMissionTypeConfig *typeConfig = eaIndexedGetUsingInt(&s_DiaryConfig.missionTypeConfigs, type);

	return typeConfig->autoAddComplete;
}

DisplayMessage *
DiaryConfig_CommentFormatMessage()
{
	return &s_DiaryConfig.commentFormatMessage;
}

DisplayMessage *
DiaryConfig_MissionHeaderFormatMessage()
{
	return &s_DiaryConfig.missionHeaderFormatMessage;
}

DisplayMessage *
DiaryConfig_CompletedMissionHeaderFormatMessage()
{
	return &s_DiaryConfig.completedMissionHeaderFormatMessage;
}

DisplayMessage *
DiaryConfig_RepeatedMissionHeaderFormatMessage()
{
	return &s_DiaryConfig.repeatedMissionHeaderFormatMessage;
}
#include "AutoGen/DiaryEnums_h_ast.c"
#include "AutoGen/DiaryCommon_h_ast.c"

