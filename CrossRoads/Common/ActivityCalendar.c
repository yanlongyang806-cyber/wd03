/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ActivityCalendar.h"

#include "textparser.h"

#include "AutoGen/ActivityCalendar_h_ast.h"

DefineContext* g_pTagCategories = NULL;
ActivityDisplayTagData g_TagCategories = {0};

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_STARTUP(ActivityCalendar);
void ActivitiyCalendar_Startup(void)
{
	int i;
	int iSize;

	StructReset(parse_ActivityDisplayTagData, &g_TagCategories);

	if (g_pTagCategories)
	{
		DefineDestroy(g_pTagCategories);
	}

	g_pTagCategories = DefineCreate();

	ParserLoadFiles(NULL, 
		"defs/events/ClientTags.def", 
		"ClientEventTags.bin", 
		PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, 
		parse_ActivityDisplayTagData,
		&g_TagCategories);

	iSize = eaSize(&g_TagCategories.eaData);
	for (i = 0; i < iSize; i++)
	{
		const char* pchData = g_TagCategories.eaData[i];
		DefineAddInt(g_pTagCategories, pchData, i+1);
	}
}


bool ActivityCalendarFilterByTag(U32* piTags, U32* piTagsInclude, U32* piTagsExclude)
{
	int iTag;

	if (ea32Size(&piTagsInclude)>0)
	{
		for (iTag=ea32Size(&piTagsInclude)-1;iTag>=0;iTag--)
		{
			if (ea32Find(&piTags,piTagsInclude[iTag]) != -1)
				break;
		}

		if (iTag == -1)
			return true;
	}

	for (iTag=ea32Size(&piTagsExclude)-1;iTag>=0;iTag--)
	{
		if (ea32Find(&piTags,piTagsExclude[iTag]) != -1)
			break;
	}

	if (iTag != -1)
		return true;

	return false;
}

#include "AutoGen/ActivityCalendar_h_ast.c"