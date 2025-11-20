#include "TicketTrackerConfig.h"

#include "AppLocale.h"
#include "Category.h"
#include "file.h"
#include "FolderCache.h"
#include "StringUtil.h"
#include "timing.h"
#include "timing_profiler.h"

#include "AutoGen/TicketTrackerConfig_h_ast.h"
#include "AutoGen/AppLocale_h_ast.h"

#define TICKETTRACKER_CONFIG_FILE "server/TicketTracker/ticketTracker_Config.txt"
static TicketTrackerConfig sTicketTrackerConfig= {0};

static char sDefaultFileDir[MAX_PATH];

static void TicketTrackerConfig_FileUpdate(FolderCache * pFolderCache,
	FolderNode * pFolderNode, int iVirtualLocation,
	const char * szRelPath, int iWhen, void * pUserData)
{
	PERFINFO_AUTO_START_FUNC();
	StructInit(parse_TicketTrackerConfig, &sTicketTrackerConfig);
	if (fileExists(szRelPath))
	{
		ParserReadTextFile(szRelPath, parse_TicketTrackerConfig, &sTicketTrackerConfig, 0);
		printf("TicketTracker config loaded: %s\n", szRelPath);
	}
	PERFINFO_AUTO_STOP();
}

static void TicketTrackerConfig_AutoLoadFromFile(void)
{
	PERFINFO_AUTO_START_FUNC();
	FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE, TICKETTRACKER_CONFIG_FILE, TicketTrackerConfig_FileUpdate, NULL);
	TicketTrackerConfig_FileUpdate(NULL, NULL, 0, TICKETTRACKER_CONFIG_FILE, 1, NULL);
	PERFINFO_AUTO_STOP();
}

char *GetPandoraAccountLink (char **estr, const char *accountName)
{
	if (!estr || !accountName || !*accountName)
		return NULL;
	estrPrintf(estr, "https://%s/%s", sTicketTrackerConfig.pandoraAddress, sTicketTrackerConfig.pandoraAccountLink);
	estrReplaceOccurrences(estr, PANDORA_NAME_TAG, accountName);
	return *estr;
}

char *GetPandoraCharacterLink (char **estr, const char *game, U32 uCharacterID)
{
	char buffer[16];
	if (!estr || !uCharacterID)
		return NULL;
	estrPrintf(estr, "https://%s/%s", sTicketTrackerConfig.pandoraAddress, sTicketTrackerConfig.pandoraCharacterLink);
	strcpy(buffer, game);
	string_tolower(buffer);
	estrReplaceOccurrences(estr, PANDORA_GAME_TAG, buffer);
	itoa(uCharacterID, buffer, 10);
	estrReplaceOccurrences(estr, PANDORA_ID_TAG, buffer);
	return *estr;
}

char *GetPandoraTicketLink (char **estr, U32 uTicketID)
{
	char buffer[16];
	if (!estr || !uTicketID)
		return NULL;
	estrPrintf(estr, "https://%s/%s", sTicketTrackerConfig.pandoraAddress, sTicketTrackerConfig.pandoraTicketLink);
	itoa(uTicketID, buffer, 10);
	estrReplaceOccurrences(estr, PANDORA_ID_TAG, buffer);
	return *estr;
}

TicketTrackerConfig *GetTicketTrackerConfig(void)
{
	return &sTicketTrackerConfig;
}

U32 GetRightNowUserQueryLimit(void)
{
	return sTicketTrackerConfig.uRightNowUserQueryLimit;
}

U32 GetRightNowQueryLimit(void)
{
	return sTicketTrackerConfig.uRightNowQueryLimit;
}

const char *GetTicketFileParentDirectory(void)
{
	if (sTicketTrackerConfig.ticketFileDir)
		return sTicketTrackerConfig.ticketFileDir;
	return sDefaultFileDir;
}

#define TICKETTRACKER_SAVEDVALS_FILE "server/TicketTracker/ticketTracker_SavedValues.txt"
static TicketTrackerSavedValues sTTSavedValues = {0};
#define TTSV_SAVE ParserWriteTextFile(TICKETTRACKER_SAVEDVALS_FILE, parse_TicketTrackerSavedValues, &sTTSavedValues, 0, 0)

void TTSavedValues_Load(void)
{
	ParserReadTextFile(TICKETTRACKER_SAVEDVALS_FILE, parse_TicketTrackerSavedValues, &sTTSavedValues, 0);
	if (sTTSavedValues.lastRightNowUpdateTime == 0)
		TTSavedValues_ModifyUpdateTime(timeSecondsSince2000());
	if (sTTSavedValues.queryPeriod == 0)
		sTTSavedValues.queryPeriod = 3600;
}

U32 TTSavedValues_GetUpdateTime(void)
{
	return sTTSavedValues.lastRightNowUpdateTime;
}
void TTSavedValues_ModifyUpdateTime(U32 uTime)
{
	sTTSavedValues.lastRightNowUpdateTime = uTime;
	TTSV_SAVE;
}

U32 TTSavedValues_GetQueryPeriod(void)
{
	return sTTSavedValues.queryPeriod;
}
void TTSavedValues_ModifyQueryPeriod(U32 uTime)
{
	sTTSavedValues.queryPeriod = uTime;
	TTSV_SAVE;
}

///////////////////////////////////
// Auto-Response Rules

#define TICKETAUTORESPONSE_RULES_FILE "server/TicketTracker/ticketAutoResponse_Rules.txt"
// Used in ManageTickets.c
TicketAutoResponseRuleList gTicketAutoResponseRules = {0};

static void TicketAutoResponse_FileUpdate(FolderCache * pFolderCache,
	FolderNode * pFolderNode, int iVirtualLocation,
	const char * szRelPath, int iWhen, void * pUserData)
{
	PERFINFO_AUTO_START_FUNC();
	StructInit(parse_TicketAutoResponseRuleList, &gTicketAutoResponseRules);
	if (fileExists(szRelPath))
	{
		ParserReadTextFile(szRelPath, parse_TicketAutoResponseRuleList, &gTicketAutoResponseRules, 0);
		EARRAY_CONST_FOREACH_BEGIN(gTicketAutoResponseRules.eaRules, i, s);
		{
			TicketAutoResponseRule *rule = gTicketAutoResponseRules.eaRules[i];			
			if (!nullStr(rule->pMainCategory))
			{
				Category *main = getMainCategory(rule->pMainCategory);
				if (!main)
					Errorf("Invalid rule: unknown category '%s'", rule->pMainCategory);
				else if (!nullStr(rule->pCategory))
				{
					Category *sub = getCategoryFromMain(main, rule->pCategory);
					if (!sub)
						Errorf("Invalid rule: unknown category '%s - %s'", rule->pMainCategory, rule->pCategory);
				}
			}
		}
		EARRAY_FOREACH_END;
		printf("Ticket AutoResponse rules loaded: %s\n", szRelPath);
	}
	PERFINFO_AUTO_STOP();
}

static void TicketAutoResponse_LoadRules(void)
{
	PERFINFO_AUTO_START_FUNC();
	FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE, TICKETAUTORESPONSE_RULES_FILE, TicketAutoResponse_FileUpdate, NULL);
	TicketAutoResponse_FileUpdate(NULL, NULL, 0, TICKETAUTORESPONSE_RULES_FILE, 1, NULL);
	PERFINFO_AUTO_STOP();
}

extern char gTicketTrackerAltDataDir[MAX_PATH];
// Initialization
void LoadAllTicketTrackerConfigFiles(void)
{
	TicketTrackerConfig_AutoLoadFromFile();
	TicketAutoResponse_LoadRules();
	sprintf(sDefaultFileDir, "%s\\%s", fileLocalDataDir(), gTicketTrackerAltDataDir);
}

#include "AutoGen/TicketTrackerConfig_h_ast.c"