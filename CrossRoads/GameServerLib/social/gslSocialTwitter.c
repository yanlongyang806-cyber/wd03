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
#include "mission_common.h"
#include "oauth.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#ifdef ENABLE_SOCIAL

static const char *consumer_key, *consumer_secret;
static const char *co_consumer_key = "6oZ4WdVj9GAAtl9CyHwsGA";
static const char *co_consumer_secret = "c9nXpepmFvvh3hmVqHMu1PkakBxwj9hiB6brVCylZBY";
static const char *sto_consumer_key = "CkZKGBuosHZheD1FB3VX2g";
static const char *sto_consumer_secret = "cFRsvLHuUAdDfOiiD9TjfCrdBTkjHleem9GsCYPwA";

enum
{
	kState_Start,
	kState_NeedPIN,
};

static bool twitterError(Entity *ent, const char *response, int response_code)
{
	if(response_code != 200)
	{
		char *message = NULL, *error = NULL;
		if(response_code == 400)
			error = "Bad Request";
		else if(response_code == 401)
			error = "Not Authorized";
		else if(response_code == 403)
			error = "Forbidden";
		else if(response_code == 404)
			error = "Not Found";
		else if(response_code == 406)
			error = "Not Acceptable";
		else if(response_code == 500)
			error = "Internal Server Error";
		else if(response_code == 502)
			error = "Bad Gateway";
		else if(response_code == 503)
			error = "Service Unavailable";
		else if(response_code == 304)
			error = "Not Modified";
		entFormatGameMessageKey(ent, &message, "Social.Twitter.Error", STRFMT_INT("code", response_code), STRFMT_STRING("message", NULL_TO_EMPTY(error)), STRFMT_END);
		notify_NotifySend(ent, kNotifyType_TwitterError, message, NULL, NULL);
		estrDestroy(&message);
		return true;
	}
	return false;
}

static void enrollCB(Entity *ent, const char *response, int response_code, void *userdata)
{
	UrlArgumentList *response_args;
	char buf[1024];

	
	if(response_code == 200)
	{
		// Parse the response
		const char *token, *secret;
		response_args = urlToUrlArgumentList_Internal(response, true);
		token = urlFindValue(response_args, "oauth_token");
		secret = urlFindValue(response_args, "oauth_token_secret");
		sprintf(buf, "%s&%s", token, secret);
		gslSocialUpdateEnrollment(ent, "Twitter", kState_NeedPIN, buf);
		sprintf(buf, "http://twitter.com/oauth/authorize?oauth_token=%s&oauth_callback=oob", token);
		ClientCmd_gclSocialOpenWebpage(ent, buf);
		urlDestroy(&response_args);
	}
	else
		gslSocialUpdateEnrollment(ent, "Twitter", kState_Start, NULL);
}

static void pinCB(Entity *ent, const char *response, int response_code, void *userdata)
{
	UrlArgumentList *response_args;

	if(!twitterError(ent, response, response_code))
	{
		// Parse the response
		response_args = urlToUrlArgumentList_Internal(response, true);
		gslStoreCredentials(ent, "Twitter", urlFindValue(response_args, "screen_name"), urlFindValue(response_args, "oauth_token"), urlFindValue(response_args, "oauth_token_secret"));
		urlDestroy(&response_args);
	}
}

static void twitterEnroll(Entity *ent, const char *service, U32 state, const char *userdata, const char *input)
{
	UrlArgumentList *args;
	switch(state)
	{
	xcase kState_Start:
		args = urlToUrlArgumentList("http://twitter.com/oauth/request_token");
		oauthSign(args, HTTPMETHOD_GET, consumer_key, consumer_secret, NULL, NULL);
		suRequest(ent, args, enrollCB, NULL, NULL);
	xcase kState_NeedPIN:
		{
		char token[256], *secret;
		strcpy(token, userdata);
		secret = strchr(token, '&');
		if(!secret)
		{
			gslSocialUpdateEnrollment(ent, "Twitter", kState_Start, NULL);
			return;
		}
		*secret = '\0';
		secret += 1;
		args = urlToUrlArgumentList("http://twitter.com/oauth/access_token");
		urlAddValue(args, "oauth_verifier", input, HTTPMETHOD_GET);
		oauthSign(args, HTTPMETHOD_GET, consumer_key, consumer_secret, token, secret);
		suRequest(ent, args, pinCB, NULL, NULL);
		gslSocialUpdateEnrollment(ent, "Twitter", kState_Start, NULL);
		}
	}
}

static void updateCB(Entity *ent, const char *response, int response_code, void *userdata)
{
	//if(response_code == 200)
	//{
	//	char *message = NULL, *tweet = suXmlParse(response, "text");
	//	if(tweet)
	//	{
	//		estrReplaceOccurrences(&tweet, "&amp;", "&");
	//		estrReplaceOccurrences(&tweet, "&gt;", ">");
	//		estrReplaceOccurrences(&tweet, "&lt;", "<");
	//	}
	//	entFormatGameMessageKey(ent, &message, "Twitter.TweetSend", STRFMT_STRING("msg", NULL_TO_EMPTY(tweet)), STRFMT_END);
	//	notify_NotifySend(ent, kNotifyType_TweetSent, message, NULL, NULL);
	//	estrDestroy(&message);
	//	estrDestroy(&tweet);
	//}
	twitterError(ent, response, response_code);
}

static void timeoutCB(Entity *ent, void *userdata)
{
	if(ent)
	{
		char *message = NULL;
		entFormatGameMessageKey(ent, &message, "Social.Twitter.Error", STRFMT_INT("code", 256), STRFMT_STRING("message", "HTTP timeout"), STRFMT_END);
		notify_NotifySend(ent, kNotifyType_TwitterError, message, NULL, NULL);
		estrDestroy(&message);
	}
}

static void twitterStatusInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, void *data)
{
	UrlArgumentList *args = urlToUrlArgumentList("http://twitter.com/statuses/update.xml");
	urlAddValue(args, "status", data, HTTPMETHOD_POST);
	oauthSign(args, HTTPMETHOD_POST, consumer_key, consumer_secret, creds->token, creds->secret);
	suRequest(ent, args, updateCB, timeoutCB, NULL);
}

static void twitterLevelUpInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, void *level)
{
	char *message=NULL;
	UrlArgumentList *args;
	switch(verbosity)
	{
	case kActivityVerbosity_None:
		// Never
		return;
	case kActivityVerbosity_Low:
		if((int)(intptr_t)level % 20 != 0) return;
		break;
	case kActivityVerbosity_Medium:
		if((int)(intptr_t)level % 10 != 0) return;
		break;
	case kActivityVerbosity_High:
		if((int)(intptr_t)level % 5 != 0) return;
		break;
	case kActivityVerbosity_All:
		// Always
		break;
	}
	args = urlToUrlArgumentList("http://twitter.com/statuses/update.xml");
	entFormatGameMessageKey(ent, &message, "Social.Twitter.LevelUp", STRFMT_ENTITY(ent), STRFMT_INT("Level", (int)(intptr_t)level), STRFMT_END);
	urlAddValue(args, "status", message, HTTPMETHOD_POST);
	oauthSign(args, HTTPMETHOD_POST, consumer_key, consumer_secret, creds->token, creds->secret);
	suRequest(ent, args, updateCB, NULL, NULL);
	estrDestroy(&message);
}

static void twitterPerkInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, MissionDef *def)
{
	char *message=NULL;
	UrlArgumentList *args;
	switch(verbosity)
	{
	case kActivityVerbosity_None:
		// Never
		return;
	case kActivityVerbosity_Low:
		if(def->iPerkPoints < 50) return;
		break;
	case kActivityVerbosity_Medium:
		if(def->iPerkPoints < 25) return;
		break;
	case kActivityVerbosity_High:
		if(def->iPerkPoints < 10) return;
		break;
	case kActivityVerbosity_All:
		// Always
		break;
	}
	args = urlToUrlArgumentList("http://twitter.com/statuses/update.xml");
	entFormatGameMessageKey(ent, &message, "Social.Twitter.Perk", STRFMT_ENTITY_KEY("Entity", ent), STRFMT_MISSIONDEF(def), STRFMT_END);
	urlAddValue(args, "status", message, HTTPMETHOD_POST);
	oauthSign(args, HTTPMETHOD_POST, consumer_key, consumer_secret, creds->token, creds->secret);
	suRequest(ent, args, updateCB, NULL, NULL);
	estrDestroy(&message);
}

#endif

AUTO_RUN;
void gslSocialTwitterRegister(void)
{
#ifdef ENABLE_SOCIAL
	if(stricmp(GetShortProductName(),"ST")==0)
	{
		consumer_key = sto_consumer_key;
		consumer_secret = sto_consumer_secret;
	}
	else
	{
		consumer_key = co_consumer_key;
		consumer_secret = co_consumer_secret;
	}
	gslSocialRegisterEnrollment("Twitter", twitterEnroll);
	gslSocialRegister("Twitter", kActivityType_Status, twitterStatusInvoke);
	gslSocialRegister("Twitter", kActivityType_LevelUp, twitterLevelUpInvoke);
	gslSocialRegister("Twitter", kActivityType_Perk, twitterPerkInvoke);
#endif
}
