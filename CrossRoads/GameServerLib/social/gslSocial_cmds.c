#include "gslSocial.h"
#include "gslStoredCredentials.h"
#include "utils.h"
#include "GlobalTypes.h"
#include "Entity.h"
#include "EntityLib.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//	These commands are in some state of stubbed/broken. Per Jeff Weinstein they eventually
//   need to be reimplemented through the new PWE web stuff anyway.
//   Rather than rip out all the code and references to the code, 
//   hide the commands from the interface. WOLF[18Jul12]
////////////////////////////////////////////////////////////////////////////////////////////////

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

void gslSocialActivityEx(Entity *ent, ActivityType type, const char *service, void *data);
void gslSocialEnroll(Entity *ent, const char *service, const char *input);

// Update your status on all enrolled services
AUTO_COMMAND ACMD_NAME(social_status) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gslSocialCmdStatus(Entity *ent, const ACMD_SENTENCE msg)
{
	gslSocialActivity(ent, kActivityType_Status, strdup(msg));
}

// Create a blog post on all enrolled services
AUTO_COMMAND ACMD_NAME(social_blog) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_HIDE;
void gslSocialCmdBlog(Entity *ent, const char *title, const char *text)
{
	ActivityDataBlog *data = calloc(1, sizeof(ActivityDataBlog));
	data->title = strdup(title);
	data->text = strdup(text);
	gslSocialActivity(ent, kActivityType_Blog, data);
}

// Command the client uses to push a requested screenshot back up to the server
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslSocialCmdReceiveScreenshot(Entity *ent, const char *title, const char *escaped_screenshot)
{
	size_t len = strlen(escaped_screenshot) + 1;
	ActivityDataScreenshot *data = calloc(1, sizeof(ActivityDataScreenshot));
	data->data = malloc(len);
	data->len = (U32)unescapeDataStatic(escaped_screenshot, 0, data->data, len, true);
	if(title && title[0])
		data->title = strdup(title);
	else
		data->title = strdupf("%s screenshot", GetProductDisplayName(0));
	gslSocialActivity(ent, kActivityType_Screenshot, data);
}

AUTO_COMMAND ACMD_NAME(social_screenshot) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gslSocialCmdScreenshot(Entity *ent, const ACMD_SENTENCE title)
{
	ClientCmd_gclSocialRequestScreenshot(ent, false, title);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(social_screenshot);
void gslSocialCmdScreenshotError(Entity *ent)
{
	ClientCmd_gclSocialRequestScreenshot(ent, false, NULL);
}

AUTO_COMMAND ACMD_NAME(social_screenshot_ui) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gslSocialCmdScreenshotUI(Entity *ent, const ACMD_SENTENCE title)
{
	ClientCmd_gclSocialRequestScreenshot(ent, true, title);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(social_screenshot_ui);
void gslSocialCmdScreenshotUIError(Entity *ent)
{
	ClientCmd_gclSocialRequestScreenshot(ent, true, NULL);
}

AUTO_COMMAND ACMD_NAME(social_disable) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gslSocialCmdDisableService(Entity *ent, char *service, char *type, bool disable)
{
	AutoTrans_Social_tr_DisableService(NULL, GLOBALTYPE_GAMESERVER, entGetType(ent), entGetContainerID(ent), type, service, disable);
}

AUTO_COMMAND ACMD_NAME(social_verbosity) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gslSocialCmdSetServiceVerbosity(Entity *ent, char *service, U32 level)
{
	AutoTrans_Social_tr_SetServiceVerbosity(NULL, GLOBALTYPE_GAMESERVER, entGetType(ent), entGetContainerID(ent), service, level);
}

// A replacement for /tweet
AUTO_COMMAND ACMD_NAME(social_tweet) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gslSocialCmdStatusTweet(Entity *ent, const ACMD_SENTENCE msg)
{
	gslSocialActivityEx(ent, kActivityType_Status, "Twitter", strdup(msg));
}

AUTO_COMMAND ACMD_NAME(social_enroll_input) ACMD_HIDE ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslSocialCmdEnrollInput(Entity *ent, const char *service, const char *input)
{
	gslSocialEnroll(ent, service, input);
}

AUTO_COMMAND ACMD_NAME(social_enroll) ACMD_HIDE ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslSocialCmdEnroll(Entity *ent, const char *service)
{
	gslSocialEnroll(ent, service, "");
}

AUTO_COMMAND ACMD_NAME(social_enroll_reset) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gslSocialCmdEnrollReset(Entity *ent, const char *service)
{
	gslStoreCredentials(ent, service, "", "", "");
	gslSocialUpdateEnrollment(ent, service, 0, NULL);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(social_enroll_reset);
void gslSocialCmdClearEnrollment(Entity *ent)
{
	AutoTrans_Social_tr_ClearEnrollment(NULL, GLOBALTYPE_GAMESERVER, entGetType(ent), entGetContainerID(ent));
}

AUTO_COMMAND_REMOTE;
void gslSocialCmdActivity(ContainerID entID, ActivityType type, char *data)
{
	Entity *ent = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);
	if(ent)
		gslSocialActivity(ent, type, strdup(data));
}

AUTO_COMMAND ACMD_NAME(gslSocialRequestServiceCache) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslSocialCmdRequestServiceCache(Entity *ent)
{
	SocialServices *services = StructCreate(parse_SocialServices);
	gslSocialServicesRegistered(services, kActivityType_Count);
	ClientCmd_gclSocialUpdateServiceCache(ent, services);
	StructDestroy(parse_SocialServices, services);
}
