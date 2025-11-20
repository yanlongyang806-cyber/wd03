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

#include "AutoGen\GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#ifdef ENABLE_SOCIAL

#define FBAPI_URL "http://api.facebook.com/restserver.php"

static const char *api_key, *api_secret;
static const char *co_api_key = "820d1f783c071a055e6793219f398d78";
static const char *co_api_secret = "d6bbbd747587f4a8f7749f7fcc979d14";
static const char *sto_api_key = "e17639cdc9d14ed37952b6c1cf140a6a";
static const char *sto_api_secret = "d08797e2eef27e8efac52d3ea50e89ec";

enum
{
	kState_Start,
	kState_Authorizing,
	kState_GrantingOfflineAccess,
	kState_GrantingPublishStream,
};

static void fbapiSign(UrlArgumentList *args, const char *method, const char *token, const char *secret)
{
	char *sig_base = NULL, md5[33];
	int i;
	UrlArgument *arg;

	// Add standard arguments
	urlAddValue(args, "method", method, HTTPMETHOD_POST);
	urlAddValue(args, "api_key", api_key, HTTPMETHOD_POST);
	urlAddValue(args, "v", "1.0", HTTPMETHOD_POST);
	if(token)
	{	
		urlAddValue(args, "session_key", token, HTTPMETHOD_POST);
		urlAddValue(args, "call_id", STACK_SPRINTF("%"FORM_LL"u", timerCpuTicks64()), HTTPMETHOD_POST);
	}

	// First add the cannonicalized arguments
	eaQSort(args->ppUrlArgList, urlCmpArgs);
	for(i=0; i<eaSize(&args->ppUrlArgList); i++)
	{
		arg = args->ppUrlArgList[i];
		if(arg->method == HTTPMETHOD_MULTIPART)
			continue;
		estrConcatf(&sig_base, "%s=%s", arg->arg, arg->value);
	}

	// then add the secret
	if(secret)
		estrConcatf(&sig_base, "%s", secret);
	else
		estrConcatf(&sig_base, "%s", api_secret);

	// Create the MD5 hash
	cryptMD5Hex(sig_base, estrLength(&sig_base), SAFESTR(md5));

	// Add the signature to the arg list
	urlAddValue(args, "sig", md5, HTTPMETHOD_POST);

	// Profit!
	estrDestroy(&sig_base);
}

static bool fbapiIsError(const char *response, U32 *error_code, char **error_msg)
{
	if(response && strstri(response, "<error_response ")!=NULL)
	{
		if(error_code)
		{
			char *tmp = strstri(response, "<error_code>");
			assert(tmp);
			tmp += 12; // 12 == strlen("<error_code>")
			*error_code = atoi(tmp);
		}
		if(error_msg)
		{
			*error_msg = suXmlParse(response, "error_msg");
		}

		return true;
	}
	return false;
}

static void createTokenCB(Entity *ent, const char *response, int response_code, void *userdata)
{
	if(response_code == 200)
	{
		char *tmp;
		response += 1;
		tmp = strchr(response, '"');
		if(!tmp)
		{
			gslSocialUpdateEnrollment(ent, "Facebook", kState_Start, NULL);
			return;
		}
		*tmp = '\0';
		gslSocialUpdateEnrollment(ent, "Facebook", kState_Authorizing, response);
		ClientCmd_gclSocialOpenWebpage(ent, STACK_SPRINTF("http://www.facebook.com/login.php?api_key=%s&v=1.0&auth_token=%s", api_key, response));
	}
}

static void getSessionCB(Entity *ent, const char *response, int response_code, void *userdata)
{
	U32 error_code;
	char *error_msg=NULL;

	if(fbapiIsError(response, &error_code, &error_msg))
	{
		char *message=NULL;
		entFormatGameMessageKey(ent, &message, "Social.Facebook.Error", STRFMT_INT("code", error_code), STRFMT_STRING("message", error_msg), STRFMT_END);
		notify_NotifySend(ent, kNotifyType_FacebookError, message, NULL, NULL);
		estrDestroy(&message);
		estrDestroy(&error_msg);
	}
	else
	{
		char *uid = suXmlParse(response, "uid");
		char *session_key = suXmlParse(response, "session_key");
		char *secret = suXmlParse(response, "secret");
		gslStoreCredentials(ent, "Facebook", uid, session_key, secret);
		estrDestroy(&uid);
		estrDestroy(&session_key);
		estrDestroy(&secret);
	}
}

static void fbEnroll(Entity *ent, const char *service, U32 state, const char *userdata, const char *input)
{
	UrlArgumentList *args = urlToUrlArgumentList(FBAPI_URL);
	switch(state)
	{
	xcase kState_Start:
		urlAddValue(args, "format", "JSON", HTTPMETHOD_POST);
		fbapiSign(args, "auth.createToken", NULL, NULL);
		suRequest(ent, args, createTokenCB, NULL, NULL);
	xcase kState_Authorizing:
		gslSocialUpdateEnrollment(ent, "Facebook", kState_GrantingOfflineAccess, userdata);
		ClientCmd_gclSocialOpenWebpage(ent, STACK_SPRINTF("http://www.facebook.com/authorize.php?api_key=%s&v=1.0&ext_perm=offline_access", api_key));
	xcase kState_GrantingOfflineAccess:
		gslSocialUpdateEnrollment(ent, "Facebook", kState_GrantingPublishStream, userdata);
		ClientCmd_gclSocialOpenWebpage(ent, STACK_SPRINTF("http://www.facebook.com/authorize.php?api_key=%s&v=1.0&ext_perm=publish_stream", api_key));
	xcase kState_GrantingPublishStream:
		gslSocialUpdateEnrollment(ent, "Facebook", kState_Start, NULL);
		urlAddValue(args, "auth_token", userdata, HTTPMETHOD_POST);
		fbapiSign(args, "auth.getSession", NULL, NULL);
		suRequest(ent, args, getSessionCB, NULL, NULL);
	}
}

static void checkErrorCB(Entity *ent, const char *response, int response_code, void *userdata)
{
	U32 error_code;
	char *error_msg=NULL;
	if(fbapiIsError(response, &error_code, &error_msg))
	{
		char *message=NULL;
		entFormatGameMessageKey(ent, &message, "Social.Facebook.Error", STRFMT_INT("code", error_code), STRFMT_STRING("message", error_msg), STRFMT_END);
		notify_NotifySend(ent, kNotifyType_FacebookError, message, NULL, NULL);
		estrDestroy(&message);
		estrDestroy(&error_msg);
	}
}

static void fbStatusInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, char *msg)
{
	UrlArgumentList *args = urlToUrlArgumentList(FBAPI_URL);
	urlAddValue(args, "status", msg, HTTPMETHOD_POST);
	urlAddValue(args, "format", "JSON", HTTPMETHOD_POST);
	fbapiSign(args, "status.set", creds->token, creds->secret);
	suRequest(ent, args, checkErrorCB, NULL, strdup(msg));
}

static void fbScreenshotInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataScreenshot *screenshot)
{
	UrlArgumentList *args = urlToUrlArgumentList(FBAPI_URL);
	UrlArgument *photo = urlAddValueExt(args, NULL, screenshot->data, HTTPMETHOD_MULTIPART);
	photo->length = screenshot->len;
	photo->filename = StructAllocString("tempss.jpg");
	photo->content_type = StructAllocString("image/jpg");
	if(screenshot->title)
		urlAddValue(args, "caption", screenshot->title, HTTPMETHOD_POST);
	fbapiSign(args, "photos.upload", creds->token, creds->secret);
	suRequest(ent, args, checkErrorCB, NULL, NULL);
}

static void fbLevelUpInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, void *level)
{
	char buf[1024], *message=NULL;
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
	args = urlToUrlArgumentList(FBAPI_URL);
	entFormatGameMessageKey(ent, &message, "Social.Facebook.LevelUp", STRFMT_ENTITY_KEY("Entity", ent), STRFMT_INT("Level", (int)(intptr_t)level), STRFMT_END);
	estrEscapeJSONString(&message);
	sprintf(buf, "{\"description\":\"%s\"}", message);
	urlAddValue(args, "attachment", buf, HTTPMETHOD_POST);
	fbapiSign(args, "stream.publish", creds->token, creds->secret);
	suRequest(ent, args, checkErrorCB, NULL, NULL);
	estrDestroy(&message);
}

static void fbPerkInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, MissionDef *def)
{
	char buf[1024], *message=NULL, *message2=NULL;
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
	args = urlToUrlArgumentList(FBAPI_URL);
	entFormatGameMessageKey(ent, &message, "Social.Facebook.PerkName", STRFMT_ENTITY_KEY("Entity", ent), STRFMT_MISSIONDEF(def), STRFMT_END);
	entFormatGameMessageKey(ent, &message2, "Social.Facebook.PerkDescription", STRFMT_ENTITY_KEY("Entity", ent), STRFMT_MISSIONDEF(def), STRFMT_END);
	estrEscapeJSONString(&message);
	estrEscapeJSONString(&message2);
	sprintf(buf, "{\"name\": \"%s\", \"description\":\"%s\"}", message, message2);
	urlAddValue(args, "attachment", buf, HTTPMETHOD_POST);
	fbapiSign(args, "stream.publish", creds->token, creds->secret);
	suRequest(ent, args, checkErrorCB, NULL, NULL);
	estrDestroy(&message);
	estrDestroy(&message2);
}

static void fbBlogInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataBlog *blog)
{
	UrlArgumentList *args = urlToUrlArgumentList(FBAPI_URL);
	urlAddValue(args, "title", blog->title, HTTPMETHOD_POST);
	urlAddValue(args, "content", blog->text, HTTPMETHOD_POST);
	fbapiSign(args, "notes.create", creds->token, creds->secret);
	suRequest(ent, args, checkErrorCB, NULL, NULL);
}

static void fbItemInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataItem *item)
{
	char buf[1024], *message=NULL, *message2=NULL;
	UrlArgumentList *args;
	if(verbosity == kActivityVerbosity_None)
		return;
	args = urlToUrlArgumentList(FBAPI_URL);
	entFormatGameMessageKey(ent, &message, "Social.Facebook.ItemName", STRFMT_ENTITY_KEY("Entity", ent), STRFMT_ITEMDEF(item->def), STRFMT_STRING("ItemName", item->name), STRFMT_END);
	entFormatGameMessageKey(ent, &message2, "Social.Facebook.ItemDescription", STRFMT_ENTITY_KEY("Entity", ent), STRFMT_ITEMDEF(item->def), STRFMT_STRING("ItemName", item->name), STRFMT_END);
	estrEscapeJSONString(&message);
	estrEscapeJSONString(&message2);
	sprintf(buf, "{\"name\": \"%s\", \"description\":\"%s\"}", message, message2);
	urlAddValue(args, "attachment", buf, HTTPMETHOD_POST);
	fbapiSign(args, "stream.publish", creds->token, creds->secret);
	suRequest(ent, args, checkErrorCB, NULL, NULL);
	estrDestroy(&message);
	estrDestroy(&message2);
}

static void fbGuildInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, char *guild_name)
{
	char buf[1024], *message=NULL, *message_key=NULL;
	UrlArgumentList *args;
	if(verbosity == kActivityVerbosity_None)
		return;
	switch(type)
	{
	case kActivityType_GuildCreate:
		message_key = "Social.Facebook.GuildCreate";
		break;
	case kActivityType_GuildJoin:
		message_key = "Social.Facebook.GuildJoin";
		break;
	case kActivityType_GuildLeave:
		message_key = "Social.Facebook.GuildLeave";
		break;
	default:
		return;
	}
	args = urlToUrlArgumentList(FBAPI_URL);
	entFormatGameMessageKey(ent, &message, message_key, STRFMT_ENTITY_KEY("Entity", ent), STRFMT_STRING("Guild", guild_name), STRFMT_END);
	estrEscapeJSONString(&message);
	sprintf(buf, "{\"name\": \"%s\"}", message);
	urlAddValue(args, "attachment", buf, HTTPMETHOD_POST);
	fbapiSign(args, "stream.publish", creds->token, creds->secret);
	suRequest(ent, args, checkErrorCB, NULL, NULL);
	estrDestroy(&message);
}

#endif

AUTO_RUN;
void gslSocialFacebookRegister(void)
{
#ifdef ENABLE_SOCIAL
	if(stricmp(GetShortProductName(),"ST")==0)
	{
		api_key = sto_api_key;
		api_secret = sto_api_secret;
	}
	else
	{
		api_key = co_api_key;
		api_secret = co_api_secret;
	}
	gslSocialRegisterEnrollment("Facebook", fbEnroll);
	gslSocialRegister("Facebook", kActivityType_Status, fbStatusInvoke);
	gslSocialRegister("Facebook", kActivityType_Screenshot, fbScreenshotInvoke);
	gslSocialRegister("Facebook", kActivityType_LevelUp, fbLevelUpInvoke);
	gslSocialRegister("Facebook", kActivityType_Perk, fbPerkInvoke);
	gslSocialRegister("Facebook", kActivityType_Blog, fbBlogInvoke);
	gslSocialRegister("Facebook", kActivityType_Item, fbItemInvoke);
	gslSocialRegister("Facebook", kActivityType_GuildCreate, fbGuildInvoke);
	gslSocialRegister("Facebook", kActivityType_GuildJoin, fbGuildInvoke);
	gslSocialRegister("Facebook", kActivityType_GuildLeave, fbGuildInvoke);
#endif
}
