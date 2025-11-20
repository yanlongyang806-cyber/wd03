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

#define API_URL "http://champions-online.com/userspace/service"

static char *g_url = NULL;
AUTO_COMMAND ACMD_CMDLINE;
void gslSocialCrypticSetUrl(const char *url)
{
	SAFE_FREE(g_url);
	g_url = strdup(url);
}

static void crypticStdArgs(Entity *ent, UrlArgumentList *args, const char *operation)
{
	urlAddValue(args, "op", operation, HTTPMETHOD_POST);
	if(ent->pPlayer)
		urlAddValue(args, "user", ent->pPlayer->privateAccountName, HTTPMETHOD_POST);
	if(ent->pSaved)
		urlAddValue(args, "character", ent->pSaved->savedName, HTTPMETHOD_POST);
}

static void crypticStatusInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, char *msg)
{
	UrlArgumentList *args = urlToUrlArgumentList(g_url ? g_url : API_URL);
	crypticStdArgs(ent, args, "status");
	urlAddValue(args, "status", msg, HTTPMETHOD_POST);
	suRequest(ent, args, suPrintCB, NULL, strdup(msg));
}

static void crypticScreenshotInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataScreenshot *screenshot)
{
	UrlArgumentList *args = urlToUrlArgumentList(g_url ? g_url : API_URL);
	UrlArgument *photo = urlAddValueExt(args, "file", screenshot->data, HTTPMETHOD_MULTIPART);
	photo->length = screenshot->len;
	photo->filename = StructAllocString("tempss.jpg");
	photo->content_type = StructAllocString("image/jpg");
	if(screenshot->title)
		urlAddValue(args, "caption", screenshot->title, HTTPMETHOD_POST);
	crypticStdArgs(ent, args, "screenshot");
	suRequest(ent, args, suPrintCB, NULL, NULL);
}

static void crypticBlogInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataBlog *blog)
{
	UrlArgumentList *args = urlToUrlArgumentList(g_url ? g_url : API_URL);
	crypticStdArgs(ent, args, "blog");
	urlAddValue(args, "title", blog->title, HTTPMETHOD_POST);
	urlAddValue(args, "text", blog->text, HTTPMETHOD_POST);
	suRequest(ent, args, suPrintCB, NULL, NULL);
}

static void crypticLevelUpInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, void *level)
{
	char buf[1024];
	UrlArgumentList *args = urlToUrlArgumentList(g_url ? g_url : API_URL);
	crypticStdArgs(ent, args, "activity");
	urlAddValue(args, "type", "LevelUp", HTTPMETHOD_POST);
	sprintf(buf, "{\"level\":\"%i\"}", (int)(intptr_t)level);
	urlAddValue(args, "data", buf, HTTPMETHOD_POST);
	suRequest(ent, args, suPrintCB, NULL, NULL);
}

static void crypticPerkInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, MissionDef *def)
{
	char buf[1024];
	UrlArgumentList *args = urlToUrlArgumentList(g_url ? g_url : API_URL);
	crypticStdArgs(ent, args, "activity");
	urlAddValue(args, "type", "Perk", HTTPMETHOD_POST);
	sprintf(buf, "{\"id\":\"%s\",\"name\":\"%s\",\"summary\":\"%s\"}", def->name, langTranslateMessageRef(0, def->displayNameMsg.hMessage), langTranslateMessageRef(0, def->summaryMsg.hMessage));
	urlAddValue(args, "data", buf, HTTPMETHOD_POST);
	suRequest(ent, args, suPrintCB, NULL, NULL);
}

static void crypticItemInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataItem *item)
{
	char buf[1024];
	UrlArgumentList *args = urlToUrlArgumentList(g_url ? g_url : API_URL);
	crypticStdArgs(ent, args, "activity");
	urlAddValue(args, "type", "Item", HTTPMETHOD_POST);
	sprintf(buf, "{\"id\":\"%s\",\"name\":\"%s\",\"description\":\"%s\"}", item->def_name, item->name, langTranslateMessageRef(0, item->def->descriptionMsg.hMessage));
	urlAddValue(args, "data", buf, HTTPMETHOD_POST);
	suRequest(ent, args, suPrintCB, NULL, NULL);
}

static void crypticGuildInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, char *guild_name)
{
	char buf[1024], *type_str;
	UrlArgumentList *args = urlToUrlArgumentList(g_url ? g_url : API_URL);
	crypticStdArgs(ent, args, "activity");
	switch(type)
	{
	case kActivityType_GuildCreate:
		type_str = "GuildCreate";
		break;
	case kActivityType_GuildJoin:
		type_str = "GuildJoin";
		break;
	case kActivityType_GuildLeave:
		type_str = "GuildLeave";
		break;
	default:
		return;
	}
	urlAddValue(args, "type", type_str, HTTPMETHOD_POST);
	sprintf(buf, "{\"name\":\"%s\"}", guild_name);
	urlAddValue(args, "data", buf, HTTPMETHOD_POST);
	suRequest(ent, args, suPrintCB, NULL, NULL);
}

AUTO_RUN;
void gslSocialCrypticRegister(void)
{
	//gslSocialRegister("Cryptic", kActivityType_Status, crypticStatusInvoke);
	gslSocialRegister("Cryptic", kActivityType_Screenshot, crypticScreenshotInvoke);
	//gslSocialRegister("Cryptic", kActivityType_Blog, crypticBlogInvoke);
	//gslSocialRegister("Cryptic", kActivityType_LevelUp, crypticLevelUpInvoke);
	//gslSocialRegister("Cryptic", kActivityType_Perk, crypticPerkInvoke);
	//gslSocialRegister("Cryptic", kActivityType_Item, crypticItemInvoke);
	//gslSocialRegister("Cryptic", kActivityType_GuildCreate, crypticGuildInvoke);
	//gslSocialRegister("Cryptic", kActivityType_GuildJoin, crypticGuildInvoke);
	//gslSocialRegister("Cryptic", kActivityType_GuildLeave, crypticGuildInvoke);
}