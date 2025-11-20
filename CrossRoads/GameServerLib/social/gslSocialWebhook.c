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

#include "AutoGen\GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static void webhooksEnroll(Entity *ent, const char *service, U32 state, const char *userdata, const char *input)
{
	gslStoreCredentials(ent, service, input, "", "");
}

static void webhooksStdArgs(Entity *ent, UrlArgumentList *args, const char *type)
{
	urlAddValue(args, "activity_type", type, HTTPMETHOD_POST);
	if(ent->pPlayer)
		urlAddValue(args, "account", ent->pPlayer->publicAccountName, HTTPMETHOD_POST);
	if(ent->pSaved)
		urlAddValue(args, "character", ent->pSaved->savedName, HTTPMETHOD_POST);
}

static void webhooksStatusInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, char *msg)
{
	UrlArgumentList *args = urlToUrlArgumentList(creds->user);
	webhooksStdArgs(ent, args, "status");
	urlAddValue(args, "status", msg, HTTPMETHOD_POST);
	suRequest(ent, args, NULL, NULL, strdup(msg));
}

static void webhooksScreenshotInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataScreenshot *screenshot)
{
	UrlArgumentList *args = urlToUrlArgumentList(creds->user);
	UrlArgument *photo = urlAddValueExt(args, "file", screenshot->data, HTTPMETHOD_MULTIPART);
	photo->length = screenshot->len;
	photo->filename = StructAllocString("tempss.jpg");
	photo->content_type = StructAllocString("image/jpg");
	if(screenshot->title)
		urlAddValue(args, "caption", screenshot->title, HTTPMETHOD_POST);
	webhooksStdArgs(ent, args, "screenshot");
	suRequest(ent, args, NULL, NULL, NULL);
}

static void webhooksBlogInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataBlog *blog)
{
	UrlArgumentList *args = urlToUrlArgumentList(creds->user);
	webhooksStdArgs(ent, args, "blog");
	urlAddValue(args, "title", blog->title, HTTPMETHOD_POST);
	urlAddValue(args, "text", blog->text, HTTPMETHOD_POST);
	suRequest(ent, args, NULL, NULL, NULL);
}

static void webhooksLevelUpInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, void *level)
{
	char buf[16];
	UrlArgumentList *args = urlToUrlArgumentList(creds->user);
	webhooksStdArgs(ent, args, "levelup");
	sprintf(buf, "%i", (int)(intptr_t)level);
	urlAddValue(args, "level", buf, HTTPMETHOD_POST);
	suRequest(ent, args, NULL, NULL, NULL);
}

static void webhooksPerkInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, MissionDef *def)
{
	UrlArgumentList *args = urlToUrlArgumentList(creds->user);
	webhooksStdArgs(ent, args, "perk");
	urlAddValue(args, "name", langTranslateMessageRef(0, def->displayNameMsg.hMessage), HTTPMETHOD_POST);
	urlAddValue(args, "summary", langTranslateMessageRef(0, def->summaryMsg.hMessage), HTTPMETHOD_POST);
	suRequest(ent, args, NULL, NULL, NULL);
}

static void webhooksItemInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataItem *item)
{
	UrlArgumentList *args = urlToUrlArgumentList(creds->user);
	webhooksStdArgs(ent, args, "item");
	urlAddValue(args, "name", item->name, HTTPMETHOD_POST);
	urlAddValue(args, "description", langTranslateMessageRef(0, item->def->descriptionMsg.hMessage), HTTPMETHOD_POST);
	suRequest(ent, args, NULL, NULL, NULL);
}

static void webhooksGuildInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, char *guild_name)
{
	char *type_str;
	UrlArgumentList *args = urlToUrlArgumentList(creds->user);
	switch(type)
	{
	case kActivityType_GuildCreate:
		type_str = "guildcreate";
		break;
	case kActivityType_GuildJoin:
		type_str = "guildjoin";
		break;
	case kActivityType_GuildLeave:
		type_str = "guildleave";
		break;
	default:
		return;
	}
	webhooksStdArgs(ent, args, type_str);
	urlAddValue(args, "guild_name", guild_name, HTTPMETHOD_POST);
	suRequest(ent, args, NULL, NULL, NULL);
}

AUTO_RUN;
void gslSocialWebhooksRegister(void)
{
	gslSocialRegisterEnrollment("Webhooks", webhooksEnroll);
	gslSocialRegister("Webhooks", kActivityType_Status, webhooksStatusInvoke);
	gslSocialRegister("Webhooks", kActivityType_Screenshot, webhooksScreenshotInvoke);
	gslSocialRegister("Webhooks", kActivityType_Blog, webhooksBlogInvoke);
	gslSocialRegister("Webhooks", kActivityType_LevelUp, webhooksLevelUpInvoke);
	gslSocialRegister("Webhooks", kActivityType_Perk, webhooksPerkInvoke);
	gslSocialRegister("Webhooks", kActivityType_Item, webhooksItemInvoke);
	gslSocialRegister("Webhooks", kActivityType_GuildCreate, webhooksGuildInvoke);
	gslSocialRegister("Webhooks", kActivityType_GuildJoin, webhooksGuildInvoke);
	gslSocialRegister("Webhooks", kActivityType_GuildLeave, webhooksGuildInvoke);
}