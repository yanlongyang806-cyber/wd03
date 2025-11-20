/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/
#include "file.h"
#include "hoglib.h"
//#include "AutoStartupSupport.h"
#include "aslLeaderboardServer.h"

#include "leaderboard_h_ast.h"
#include "aslLeaderboardServer_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

char gDBFileName[CRYPTIC_MAX_PATH] = "LeaderboardDB.hog";
char gDBDataFileName[CRYPTIC_MAX_PATH] = "LeaderboardEntryDB.hog";
HogFile *pDBFile = NULL;
HogFile *pDBDataFile = NULL;

// NOT Soucre Controlled
const char *leaderboardGetDatabaseDir(void)
{
	static char sDataDir[MAX_PATH] = "";
	if (!sDataDir[0])
	{
		sprintf(sDataDir, "%s/Leaderboarddb/", fileBaseDir());
		forwardSlashes(sDataDir);
	}
	return sDataDir;
}

void leaderboardDBAddToHog_Free(char *data)
{
	estrDestroy(&data);
}

void leaderboardDBAddToHog(Leaderboard *pLeaderboard)
{
	char *pchData = NULL;
	char schFileName[CRYPTIC_MAX_PATH];

	estrCreate(&pchData);

	ParserWriteText(&pchData,parse_Leaderboard,pLeaderboard,0,0,0);

	sprintf(schFileName,"%s_%d.leaderboard",pLeaderboard->pchLeaderboard,pLeaderboard->iInterval);
	hogFileModifyUpdateNamed(pDBFile,schFileName,pchData,(U32)strlen(pchData),time(NULL),leaderboardDBAddToHog_Free);
}

void leaderboardDataDBAddToHog(LeaderboardData *pLBData)
{
	char *pchData = NULL;
	char schFileName[CRYPTIC_MAX_PATH];

	estrCreate(&pchData);

	ParserWriteText(&pchData,parse_LeaderboardData,pLBData,0,0,0);

	sprintf(schFileName,"%s_%d.leaderboardData","PlayerData",pLBData->ePlayerID);
	hogFileModifyUpdateNamed(pDBDataFile,schFileName,pchData,(U32)strlen(pchData),time(NULL),leaderboardDBAddToHog_Free);
}

bool leaderboardDBRead(HogFile *handle, HogFileIndex index, const char* filename, void * userData)
{
	U32 uiCount;
	const char *pchText = hogFileExtract(handle,index,&uiCount,NULL);
	Leaderboard s_Leaderboard = {0};

	if(uiCount)
	{
		ParserReadText(pchText,parse_Leaderboard,&s_Leaderboard,0);
		leaderboard_postLoad(StructClone(parse_Leaderboard,&s_Leaderboard));

		return true;
	}

	return false;
}

bool leaderboardDataDBRead(HogFile *handle, HogFileIndex index, const char* filename, void * userData)
{
	U32 uiCount;
	const char *pchText = hogFileExtract(handle,index,&uiCount,NULL);
	LeaderboardData s_LBData = {0};

	if(uiCount)
	{
		ParserReadText(pchText,parse_LeaderboardData,&s_LBData,0);
		leaderboarddata_postLoad(StructClone(parse_LeaderboardData,&s_LBData));

		return true;
	}

	return false;
}

void leaderboardDBInit(void)
{
	Leaderboard sFakeData = {0};
	pDBFile = hogFileRead(STACK_SPRINTF("%s/%s", leaderboardGetDatabaseDir(), gDBFileName),NULL,PIGERR_PRINTF,NULL,HOG_DEFAULT);
	pDBDataFile = hogFileRead(STACK_SPRINTF("%s/%s",leaderboardGetDatabaseDir(), gDBDataFileName),NULL,PIGERR_PRINTF,NULL,HOG_DEFAULT);

	if(pDBFile)
		hogScanAllFiles(pDBFile,leaderboardDBRead,NULL);

	if(pDBDataFile)
		hogScanAllFiles(pDBDataFile,leaderboardDataDBRead,NULL);
}