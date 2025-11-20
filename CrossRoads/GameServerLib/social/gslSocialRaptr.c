#include "gslSocial.h"
#include "gslSocialUtils.h"
#include "gslStoredCredentials.h"
#include "StoredCredentialsCommon.h"
#include "NotifyCommon.h"
#include "GameStringFormat.h"
#include "GlobalTypes.h"
#include "Entity.h"
#include "Player.h"
#include "Character.h"
#include "EntitySavedData.h"
#include "url.h"
#include "EString.h"
#include "earray.h"
#include "rand.h"
#include "crypt.h"
#include "utils.h"
#include "mission_common.h"
#include "itemCommon.h"
#include "Organization.h"

#include "AutoGen\GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#ifdef ENABLE_SOCIAL

#define REWARD_DEFINE_URL "http://api.raptr.com/v1/award/define"
#define REWARD_GRANT_URL "http://api.raptr.com/v1/award/grant"

//static const char *app_key = ORGANIZATION_NAME_SINGLEWORD_LCASE ".DataTester";
static const char *app_key = ORGANIZATION_NAME_SINGLEWORD_LCASE ".champions_online";
static const char *game_id = "/game/PC/Champions_Online";

// gamer_id
// {"character_name": "blah", "account_name": "foo", "raptr_name": "baz"}

enum
{
	kState_Start,
	kState_EnteringKey,
};

static void raptrEnroll(Entity *ent, const char *service, U32 state, const char *userdata, const char *input)
{
	char *username, *key;
	switch(state)
	{
	case kState_Start:
		gslSocialUpdateEnrollment(ent, "Raptr", kState_EnteringKey, NULL);
		if(input && stricmp(input, "1")!=0)
			ClientCmd_gclSocialOpenWebpage(ent, "http://raptr.com/account/remotekey?src=champions");
		break;
	case kState_EnteringKey:
		gslSocialUpdateEnrollment(ent, "Raptr", kState_Start, NULL);
		username = strdup(input);
		key = strchr(username, '&');
		if(!key) { free(username); return; }
		*key = '\0';
		key += 1;
		gslStoreCredentials(ent, "Raptr", username, "", key);
		free(username);
		break;
	}

}

static void raptrStdArgs(Entity *ent, UrlArgumentList *args, StoredCredentials *creds, const char *award_id)
{
	char buf[1024];
	urlAddValue(args, "app_key", app_key, HTTPMETHOD_POST);
	urlAddValue(args, "game_id", game_id, HTTPMETHOD_POST);
	if(award_id)
	{
		sprintf(buf, "{\"character_name\":\"%s\", \"account_name\":\"%s\"}", ent->pSaved->savedName, ent->pPlayer->publicAccountName);
		urlAddValue(args, "gamer_id", buf, HTTPMETHOD_POST);
		sprintf(buf, "{\"raptr_name\":\"%s\", \"remote_key\":\"%s\"}", creds->user, creds->secret);
		urlAddValue(args, "grant_meta", buf, HTTPMETHOD_POST);
		urlAddValue(args, "award_id", award_id, HTTPMETHOD_POST);
		sprintf(buf, "%s-%s-%s-%s", game_id, ent->pSaved->savedName, ent->pPlayer->publicAccountName, award_id);
		urlAddValue(args, "award_inst_id", buf, HTTPMETHOD_POST);
	}
}

static void raptrLevelUpInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, void *level)
{
	UrlArgumentList *args = urlToUrlArgumentList(REWARD_GRANT_URL);
	raptrStdArgs(ent, args, creds, STACK_SPRINTF("Level_%i", (int)(intptr_t)level));
	urlAddValue(args, "award_value", STACK_SPRINTF("%i", (int)(intptr_t)level), HTTPMETHOD_POST);
	suRequest(ent, args, NULL, NULL, NULL);
}

static void raptrGrantAward(Entity *ent, const char *response, int response_code, UrlArgumentList *args)
{
	suRequest(ent, args, NULL, NULL, NULL);
}

static void raptrTimeout(Entity *ent, UrlArgumentList *args)
{
	urlDestroy(&args);
}

static void raptrPerkInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, MissionDef *def)
{
	UrlArgumentList *args, *args_inner;
	if(def->iPerkPoints < 25) return;
	args = urlToUrlArgumentList(REWARD_DEFINE_URL);
	urlAddValue(args, "app_key", app_key, HTTPMETHOD_POST);
	urlAddValue(args, "game_id", game_id, HTTPMETHOD_POST);
	urlAddValue(args, "award_type", "Achievement", HTTPMETHOD_POST);
	urlAddValue(args, "award_category", "Perk", HTTPMETHOD_POST);
	urlAddValue(args, "award_id", def->name, HTTPMETHOD_POST);
	urlAddValue(args, "award_name", langTranslateMessageRef(0, def->displayNameMsg.hMessage), HTTPMETHOD_POST);
	urlAddValue(args, "award_description", langTranslateMessageRef(0, def->summaryMsg.hMessage), HTTPMETHOD_POST);
	args_inner = urlToUrlArgumentList(REWARD_GRANT_URL);
	raptrStdArgs(ent, args_inner, creds, def->name);
	suRequest(ent, args, raptrGrantAward, raptrTimeout, args_inner);
}

static void raptrItemInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataItem *item)
{
	UrlArgumentList *args, *args_inner;
	args = urlToUrlArgumentList(REWARD_DEFINE_URL);
	urlAddValue(args, "app_key", app_key, HTTPMETHOD_POST);
	urlAddValue(args, "game_id", game_id, HTTPMETHOD_POST);
	urlAddValue(args, "award_type", "Item", HTTPMETHOD_POST);
	urlAddValue(args, "award_category", "Item", HTTPMETHOD_POST);
	urlAddValue(args, "award_id", item->def_name, HTTPMETHOD_POST);
	urlAddValue(args, "award_name", item->name, HTTPMETHOD_POST);
	urlAddValue(args, "award_description", langTranslateMessageRef(0, item->def->descriptionMsg.hMessage), HTTPMETHOD_POST);
	args_inner = urlToUrlArgumentList(REWARD_GRANT_URL);
	raptrStdArgs(ent, args_inner, creds, item->def_name);
	suRequest(ent, args, raptrGrantAward, raptrTimeout, args_inner);
}

#endif

AUTO_RUN;
void gslSocialRaptrRegister(void)
{
#ifdef ENABLE_SOCIAL
	if(stricmp(GetShortProductName(),"FC")==0)
	{
		gslSocialRegisterEnrollment("Raptr", raptrEnroll);
		gslSocialRegister("Raptr", kActivityType_LevelUp, raptrLevelUpInvoke);
		gslSocialRegister("Raptr", kActivityType_Perk, raptrPerkInvoke);
		gslSocialRegister("Raptr", kActivityType_Item, raptrItemInvoke);
	}
#endif
}

