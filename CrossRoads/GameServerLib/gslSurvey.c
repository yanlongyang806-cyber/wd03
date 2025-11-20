/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#include "textparser.h"
#include "earray.h"
#include "file.h"
#include "estring.h"
#include "logging.h"
#include "entity.h"
#include "mission_common.h"
#include "Message.h"
#include "ResourceInfo.h"
#include "GameStringFormat.h"
#include "Player.h"

#include "survey.h"
#include "autogen/survey_h_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/UI2Lib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

int g_iShowSurvey = 0;
int g_iSurveyMissionLevelMin = 0;
int g_iSurveyMissionLevelMax = 999;
int g_iSurveyMissionNeedsReturn = 0;
float g_fSurveyMissionRewardScaleMin = 0.0f;

AUTO_CMD_INT(g_iShowSurvey, ShowSurvey) ACMD_CMDLINE ACMD_HIDE;
AUTO_CMD_INT(g_iShowSurvey, survey_Show) ACMD_CMDLINE ACMD_HIDE;
AUTO_CMD_INT(g_iShowSurvey, Survey_ShowReal) ACMD_ACCESSLEVEL(9) ACMD_HIDE;
AUTO_CMD_INT(g_iSurveyMissionLevelMin, survey_MissionLevelMin) ACMD_CMDLINE ACMD_HIDE;
AUTO_CMD_INT(g_iSurveyMissionLevelMax, survey_MissionLevelMax) ACMD_CMDLINE ACMD_HIDE;
AUTO_CMD_INT(g_iSurveyMissionNeedsReturn, survey_MissionNeedsReturn) ACMD_CMDLINE ACMD_HIDE;
AUTO_CMD_FLOAT(g_fSurveyMissionRewardScaleMin, survey_MissionRewardScaleMin) ACMD_CMDLINE ACMD_HIDE;

void survey_Mission(Entity *playerEnt, MissionDef *missionDef)
{
	char *p = NULL;

	if(!g_iShowSurvey
		|| !missionDef
		|| (g_iSurveyMissionNeedsReturn && !missionDef->needsReturn)
		|| (missionDef->levelDef.missionLevel < g_iSurveyMissionLevelMin)
		|| (missionDef->levelDef.missionLevel > g_iSurveyMissionLevelMax)
		|| (missionDef->params && missionDef->params->NumericRewardScale < g_fSurveyMissionRewardScaleMin))
	{
		return;
	}

	if(missionDef)
	{
		ClientCmd_GenSetValue(playerEnt, "Survey_Root", "_mission", 0, missionDef->name);
		ClientCmd_GenSetValue(playerEnt, "Survey_Root", "_display_name", 0, entTranslateDisplayMessage(playerEnt, missionDef->displayNameMsg));
		ClientCmd_GenSetValue(playerEnt, "Survey_Root", "_summary", 0, entTranslateDisplayMessage(playerEnt, missionDef->summaryMsg));
	}
	else
	{
		ClientCmd_GenSetValue(playerEnt, "Survey_Root", "_display_name", 0, "(none)");
	}

	ClientCmd_GenSendMessage(playerEnt, "Survey_Root", "Show");
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void survey_Log(Entity *e, IndexedPairs *pPairs)
{
	if(e && pPairs)
	{
		char *estr;

		eaSortUsingKey(&pPairs->ppPairs, parse_IndexedPair);

		estrStackCreate(&estr);
		ParserWriteText(&estr, parse_IndexedPairs, &pPairs->ppPairs, 0, 0, 0);
		entLog(LOG_SURVEY, e, "SurveyMission", "%s", estr);

		estrDestroy(&estr);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9);
void survey_Test(Entity *e)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_MissionDictionary);
	if(pStruct && pStruct->ppReferents)
	{
		int n = eaSize(&pStruct->ppReferents);
		MissionDef *pDef = pStruct->ppReferents[randInt(n)];

		survey_Mission(e, pDef);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9);
void survey_log_generate(Entity *e, int iCnt)
{
	int i;
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_MissionDictionary);

	if(pStruct && pStruct->ppReferents)
	{
		int n = eaSize(&pStruct->ppReferents);
		IndexedPairs pairs = {0};
		IndexedPair *p = malloc(sizeof(IndexedPair)*5);

		eaPush(&pairs.ppPairs, p);
		eaPush(&pairs.ppPairs, p+1);
		eaPush(&pairs.ppPairs, p+2);
		eaPush(&pairs.ppPairs, p+3);
		eaPush(&pairs.ppPairs, p+4);

		for(i=0; i<iCnt; i++)
		{
			MissionDef *pDef = pStruct->ppReferents[randInt(n)];

			pairs.ppPairs[0]->pchKey = "Overall";
			pairs.ppPairs[0]->pchValue = (randInt(4)<1 ? "2_Awesome" : (randInt(4)<1 ? "0_Boring" : "1_ModeratelyFun" ));

			pairs.ppPairs[1]->pchKey = "_mission";
			pairs.ppPairs[1]->pchValue = pDef->name;

			pairs.ppPairs[2]->pchKey = "_display_name";
			pairs.ppPairs[2]->pchValue = TranslateDisplayMessage(pDef->displayNameMsg);

			pairs.ppPairs[3]->pchKey = "Directions";
			pairs.ppPairs[3]->pchValue = (randInt(4)<1 ? "2_CrystalClear" : (randInt(4)<1 ? "1_OK" : "0_Unclear" ));

			pairs.ppPairs[4]->pchKey = "Comments";
			pDef = pStruct->ppReferents[randInt(n)];
			pairs.ppPairs[4]->pchValue = TranslateDisplayMessage(pDef->detailStringMsg);

			survey_Log(e, &pairs);
		}

		eaDestroy(&pairs.ppPairs);
		free(p);
	}
}

#include "survey_h_ast.c"

/* End of File */
