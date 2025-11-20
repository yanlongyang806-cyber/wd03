#include "stdtypes.h"

#include "chatVoice.h"

#include "Alerts.h"
#include "AutoTransDefs.h"
#include "chatdb.h"
#include "chatGlobal.h"
#include "chatLocal.h"
#include "ChatServer/chatShared.h"
#include "chatShardConfig.h"
#include "ContinuousBuilderSupport.h"
#include "file.h"
#include "GameAccountData\GameAccountData.h"
#include "httpAsync.h"
#include "HttpClient.h"
#include "logging.h"
#include "objTransactions.h"
#include "objContainer.h"
#include "msgsend.h"
#include "net.h"
#include "rand.h"
#include "StashTable.h"
#include "StringCache.h"
#include "TransactionOutcomes.h"
#include "url.h"
#include "users.h"
#include "utilitiesLib.h"
#include "XMLParsing.h"
#include "XMLRPC.h"

#include "chatVoice_h_ast.h"
#include "chatVoice_c_ast.h"
#include "AutoGen\ChatServer_autotransactions_autogen_wrappers.h"
#include "AutoGen\chatdb_h_ast.h"
#include "GameAccountData_h_ast.h"

#define VIVOX_API_DEF		"http://trekd.vivox.com/api2"
#define VIVOX_ADMIN_USERID	"cryptic_vivox_admin"
#define VIVOX_ADMIN_PWD		"d*9RtL3uK$5"
#define TUNNEL_HOST_DEF		"QAGlobalChat"
#define TUNNEL_PORT_DEF		4432

// Admin login
#define VIVOX_SIGNIN		"viv_signin.php"
#define VIVOX_SIGNOUT		"viv_signout.php"

// Account API
#define VIVOX_ACCT_CREATE	"viv_adm_acct_new.php"
#define VIVOX_ACCT_VERIFY	"viv_acct.php"
#define VIVOX_ACCT_SETPWD	"viv_adm_password.php"
#define VIVOX_ACCT_NOADS	"viv_adm_properties.php"

// Channel API
#define VIVOX_CHAN_CREATE	"viv_chan_mod.php"
#define VIVOX_CHAN_DELETE	"viv_chan_mod.php"
#define VIVOX_CHAN_JOIN		"viv_chan_mod.php"
#define VIVOX_CHAN_LEAVE	"viv_chan_mod.php"
#define VIVOX_CHAN_FIND		"viv_chan_mod.php"

// Admin login
#define VIVOX_SIGNIN		"viv_signin.php"
#define VIVOX_SIGNOUT		"viv_signout.php"

// Account API
#define VIVOX_ACCT_CREATE	"viv_adm_acct_new.php"


// Permission Token
#define PERMISSION_TOKEN_NOADS	"Chat.DisableAds"

void cvFinishLogin(ChatUser *user);

VoiceChatState g_VoiceChatState;

AUTO_STRUCT;
typedef struct VivoxAccountMapping {
	ContainerID crypticID;
	const char* username;
	const char* password;
	int	vivoxID;
} VivoxAccountMapping;

AUTO_STRUCT;
typedef struct ChannelRef {
	U32 key;
	char* name;
} ChannelRef;

AUTO_STRUCT;
typedef struct UserChannel {
	ContainerID userId;
	ContainerID chanId;
	char*		chanName;
} UserChannel;

AUTO_STRUCT;
typedef struct UserQueue {
	ContainerID id;

	HttpFormRequest **requests;

	U32 loggedin : 1;
	U32 forceVerify : 1;		// For prod servers which don't default verify
} UserQueue;

typedef struct ResponseChain {
	voidVoidStarFunc cb;
	void *cbdata;

	void *data;

	int callOnError : 1;
} ResponseChain;

StashTable queuedRequests;
U32 g_VoiceOverride = false;
char* g_TunnelHostOverride = TUNNEL_HOST_DEF;
int g_TunnelPortOverride = TUNNEL_PORT_DEF;
char* g_VoiceAPIServerOverride = NULL;

int g_VoiceMaxActiveRequests = 200;
int g_VoiceErrorCountBeforeDeactivate = 100;
int g_VoiceErrorTimeBeforeDeactivate = 60;

// Name of machine for voice chat secure tunnel proxy
AUTO_CMD_ESTRING(g_TunnelHostOverride, VoiceTunnelHost) ACMD_AUTO_SETTING(Chat, CHATSERVER);
// Port to use for voice chat secure tunnel proxy
AUTO_CMD_INT(g_TunnelPortOverride, VoiceTunnelPort) ACMD_AUTO_SETTING(Chat, CHATSERVER);

AUTO_CMD_ESTRING(g_VoiceAPIServerOverride, VoiceAPIOverride);


AUTO_CMD_INT(g_VoiceMaxActiveRequests, VoiceMaxActiveRequests) ACMD_AUTO_SETTING(Chat, CHATSERVER);
AUTO_CMD_INT(g_VoiceErrorCountBeforeDeactivate, VoiceErrorCountBeforeDeactivate) ACMD_AUTO_SETTING(Chat, CHATSERVER);
AUTO_CMD_INT(g_VoiceErrorTimeBeforeDeactivate, VoiceErrorTimeBeforeDeactivate) ACMD_AUTO_SETTING(Chat, CHATSERVER);

extern ShardChatServerConfig gShardChatServerConfig;

void cvFinishLogin(ChatUser *user);
void cvVerifyAccountInternal(VivoxAccountMapping *map);
void cvAccountCreateInternal(VivoxAccountMapping *map);

U32 cvIsVoiceEnabled(void)
{
	return g_VoiceOverride || (gShardChatServerConfig.bVoiceEnabled && !g_isContinuousBuilder);
}

void cvDeferRequest(ContainerID id, HttpFormRequest *req)
{
	if(!g_VoiceChatState.signed_in)
	{
		StructDestroy(parse_HttpFormRequest, req);
		return;
	}

	if(objGetContainerData(GLOBALTYPE_CHATUSER, id))
		chatVoiceTakeRequest(req);
	else
	{
		UserQueue *q = NULL;

		stashIntFindPointer(queuedRequests, id, &q);

		if(!q)
		{
			q = StructCreate(parse_UserQueue);
			q->id = id;

			stashIntAddPointer(queuedRequests, id, q, true);
		}

		eaPush(&q->requests, req);
	}
}

UserQueue* cvGetUserQueue(ContainerID id)
{
	UserQueue *q = NULL;

	if(!g_VoiceChatState.signed_in)
		return NULL;

	stashIntFindPointer(queuedRequests, id, &q);
	if(!q)
	{
		q = StructCreate(parse_UserQueue);
		q->id = id;

		stashIntAddPointer(queuedRequests, id, q, true);
	}

	return q;
}

void cvUserLoggedIn(ContainerID id)
{
	UserQueue *q = cvGetUserQueue(id);

	if(!q)
		return;

	q->loggedin = true;
}

void cvUserJoinAll(void *user_id)
{
	ChatUser *user = userFindByContainerId((U32) (intptr_t) user_id);
	if (!user)
		return;
	FOR_EACH_IN_EARRAY(user->reserved, Watching, watch)
	{
		ChatChannel *chan = objGetContainerData(GLOBALTYPE_CHATCHANNEL, watch->channelID);

		if(!chan)
			chan = channelFindByName(watch->name);

		chatVoiceJoin(chan, user);
	}
	FOR_EACH_END
}

const char* cvConfigGetAPIUrl(void)
{
	return isDevelopmentMode() ? 
		gShardChatServerConfig.pchVoiceServerDev : 
		gShardChatServerConfig.pchVoiceServer;	
}

void cvSetURL(char** estrUrl, const char* page)
{
	estrPrintf(estrUrl, "%s/%s", cvConfigGetAPIUrl(), page);
}

ResponseChain* rcCreate(voidVoidStarFunc cb, void *userdata, int callOnError)
{
	ResponseChain *rc = NULL;

	rc = callocStruct(ResponseChain);
	rc->data = NULL;
	rc->cb = cb;
	rc->cbdata = userdata;
	rc->callOnError = callOnError;

	return rc;
}

void cvAcctDisableAdsCB(ResponseChain *rc, const char* xmldoc, int len)
{
	static StashTable st = NULL;
	XMLExtractState state;
	char *status = NULL;

	if(!st)
		st = stashTableCreateWithStringKeys(10, StashDeepCopyKeys);

	stashTableClearEx(st, NULL, simpleFree);

	cvExtractInit(&state, st);
	stashAddPointer(st, "status", NULL, true);
	cvExtractKeysValues(xmldoc, len, &state);
	stashFindPointer(st, "status", &status);

	rc->cb(rc->cbdata);

	free(rc);
}

void cvAcctDisableAds(VivoxAccountMapping *map, int enable, voidVoidStarFunc cb, void *userdata)
{
	UrlArgumentList args = {0};
	ResponseChain *rc = rcCreate(cb, userdata, true);

	if(enable)
		urlAddValue(&args, "mode", "add", HTTPMETHOD_GET);
	else
		urlAddValue(&args, "mode", "delete", HTTPMETHOD_GET);
	urlAddValue(&args, "acct_name", map->username, HTTPMETHOD_GET);
	urlAddValue(&args, "name", "NoAds", HTTPMETHOD_GET);
	cvSetURL(&args.pBaseURL, VIVOX_ACCT_NOADS);

	cvDeferRequest(map->crypticID, chatVoiceProcessPage(&args, cvAcctDisableAdsCB, rc, false));

	StructDestroy(parse_VivoxAccountMapping, map);
	StructDeInit(parse_UrlArgumentList, &args);
}

bool cvAcctNeedsDisableAds(VivoxAccountMapping *map)
{
	return 0;
}

void cvAcctRecreateCB(VivoxAccountMapping *map, const char* xmldoc, int len)
{
	static StashTable st = NULL;
	char *status = NULL;
	XMLExtractState state;
	ChatUser *user = objGetContainerData(GLOBALTYPE_CHATUSER, map->crypticID);

	if(!user)
	{
		StructDestroy(parse_VivoxAccountMapping, map);
		return;
	}

	if(!st)
		st = stashTableCreateWithStringKeys(10, StashDeepCopyKeys);

	stashTableClearEx(st, NULL, simpleFree);

	cvExtractInit(&state, st);

	stashAddPointer(st, "status", NULL, true);
	cvExtractKeysValues(xmldoc, len, &state);
	stashFindPointer(st, "status", &status);

	if(status && !stricmp(status, "ERR"))
	{
		Errorf("Failed to change account password: %s - %s", map->username, status);

		StructDestroy(parse_VivoxAccountMapping, map);
		return;
	}

	user->voice_accountid = map->vivoxID;
	AutoTrans_tr_cvSetVoiceAccountData(	NULL, 
										GLOBALTYPE_CHATSERVER, 
										GLOBALTYPE_GAMEACCOUNTDATA, 
										map->crypticID, 
										map->username,
										map->password,
										map->vivoxID);

	cvUserJoinAll((void*)(intptr_t)user->id);
	//cvAcctDisableAds(map, cvAcctNeedsDisableAds(map), cvUserJoinAll, (void*)(intptr_t) user->id);
}

void cvAcctResetPwd(VivoxAccountMapping *map)
{
	UrlArgumentList args = {0};

	urlAddValue(&args, "user_name", map->username, HTTPMETHOD_GET);
	urlAddValue(&args, "new_pwd", map->password, HTTPMETHOD_GET);
	cvSetURL(&args.pBaseURL, VIVOX_ACCT_SETPWD);

	cvDeferRequest(map->crypticID, chatVoiceProcessPage(&args, cvAcctRecreateCB, map, false));
	StructDeInit(parse_UrlArgumentList, &args);
}

void cvAcctResetGetIDCB(VivoxAccountMapping *map, const char* xmldoc, int len)
{
	static StashTable st = NULL;
	XMLExtractState state;
	char *acct = NULL;
	
	if(!st)
		st = stashTableCreateWithStringKeys(10, StashDeepCopyKeys);

	stashTableClearEx(st, NULL, simpleFree);

	cvExtractInit(&state, st);
	stashAddPointer(st, "accountid", NULL, true);
	cvExtractKeysValues(xmldoc, len, &state);
	stashFindPointer(st, "accountid", &acct);

	if(!acct)
	{
		Errorf("Failed to find account: %s", map->username);

		StructDestroy(parse_VivoxAccountMapping, map);
		return;
	}

	// Got id, now change password
	map->vivoxID = atoi(acct);

	cvAcctResetPwd(map);
}

void cvAccountReset(VivoxAccountMapping *map)
{
	UrlArgumentList args = {0};

	urlAddValue(&args, "mode", "list", HTTPMETHOD_GET);
	urlAddValue(&args, "acct_name", map->username, HTTPMETHOD_GET);
	cvSetURL(&args.pBaseURL, VIVOX_ACCT_VERIFY);

	cvDeferRequest(map->crypticID, chatVoiceProcessPage(&args, cvAcctResetGetIDCB, map, false));
	StructDeInit(parse_UrlArgumentList, &args);
}

void cvAcctCreateCB(VivoxAccountMapping *map, const char* xmldoc, int len)
{
	static StashTable st = NULL;
	XMLExtractState state;
	char *auth = NULL, *acct = NULL, *code = NULL, *dbcode = NULL, *status = NULL;
	ChatUser *user = objGetContainerData(GLOBALTYPE_CHATUSER, map->crypticID);

	if(!user)
	{
		StructDestroy(parse_VivoxAccountMapping, map);
		return;
	}

	if(!st)
		st = stashTableCreateWithStringKeys(10, StashDeepCopyKeys);

	stashTableClearEx(st, NULL, simpleFree);

	cvExtractInit(&state, st);
	stashAddPointer(st, "auth_token", NULL, true);
	stashAddPointer(st, "accountid", NULL, true);
	stashAddPointer(st, "code", NULL, true);
	stashAddPointer(st, "db_code", NULL, true);
	stashAddPointer(st, "status", NULL, true);

	cvExtractKeysValues(xmldoc, len, &state);

	stashFindPointer(st, "auth_token", &auth);
	stashFindPointer(st, "accountid", &acct);
	stashFindPointer(st, "code", &code);
	stashFindPointer(st, "db_code", &dbcode);
	stashFindPointer(st, "status", &status);

	// TODO: succeed_if_exists param could bypass this
	if(status && !stricmp(status, "ERR") && code)
	{
		int c = atoi(code);

		switch(c)
		{
			xcase 400: {			// Account create failed
				if(dbcode) 
				{
					int d = atoi(dbcode);

					switch(d)
					{
						xcase 1203: {	// Account already exists, have to get id then change pwd
							cvAccountReset(map);
							return;
						}
					}
				}
			}	
		}
	}

	if(!auth || !acct)
		return;

	user->voice_accountid = atoi(acct);

	AutoTrans_tr_cvSetVoiceAccountData(	NULL, 
										GLOBALTYPE_CHATSERVER, 
										GLOBALTYPE_GAMEACCOUNTDATA, 
										map->crypticID, 
										map->username,
										map->password,
										atoi(acct));

	cvUserJoinAll((void*)(intptr_t)user->id);
	//cvAcctDisableAds(map, cvAcctNeedsDisableAds(map), cvUserJoinAll, (void*)(intptr_t) user->id);
}

void cvAccountCreateInternal(VivoxAccountMapping *map)
{
	UrlArgumentList args = {0};
	HttpFormRequest *req;

	urlAddValue(&args, "username", map->username, HTTPMETHOD_GET);
	urlAddValue(&args, "pwd", map->password, HTTPMETHOD_GET);
	cvSetURL(&args.pBaseURL, VIVOX_ACCT_CREATE);

	req = chatVoiceProcessPage(&args, cvAcctCreateCB, map, false);

	cvDeferRequest(map->crypticID, req);
	StructDeInit(parse_UrlArgumentList, &args);
}

void cvCreateAccount(ContainerID id, const char* un, const char *pw)
{
	VivoxAccountMapping *map  = StructCreate(parse_VivoxAccountMapping);
	map->crypticID = id;
	map->username = StructAllocString(un);
	map->password = StructAllocString(pw);

	cvAccountCreateInternal(map);
}

void cvAcctVerifyCB(VivoxAccountMapping *map, const char* xmldoc, int len)
{
	static StashTable st = NULL;
	XMLExtractState state;
	char *auth = NULL, *acct = NULL;
	ChatUser *user = objGetContainerData(GLOBALTYPE_CHATUSER, map->crypticID);

	if(!user)
	{
		StructDestroy(parse_VivoxAccountMapping, map);
		return;
	}

	if(!st)
		st = stashTableCreateWithStringKeys(10, StashDeepCopyKeys);

	stashTableClearEx(st, NULL, simpleFree);

	cvExtractInit(&state, st);
	stashAddPointer(st, "accountid", NULL, true);
	cvExtractKeysValues(xmldoc, len, &state);
	stashFindPointer(st, "accountid", &acct);

	if(!acct)
	{
		cvCreateAccount(map->crypticID, map->username, map->password);

		StructDestroy(parse_VivoxAccountMapping, map);
		return;
	}

	map->vivoxID = atoi(acct);
	cvAcctResetPwd(map);
}

void cvVerifyAccountInternal(VivoxAccountMapping *map)
{
	UrlArgumentList args = {0};

	urlAddValue(&args, "mode", "list", HTTPMETHOD_GET);
	urlAddValue(&args, "username", map->username, HTTPMETHOD_GET);
	cvSetURL(&args.pBaseURL, VIVOX_ACCT_VERIFY);

	cvDeferRequest(map->crypticID, chatVoiceProcessPage(&args, cvAcctVerifyCB, map, false));
	StructDeInit(parse_UrlArgumentList, &args);
}

void cvVerifyAccount(ContainerID id, const char* username, const char* password, int acctId)
{
	VivoxAccountMapping *map  = StructCreate(parse_VivoxAccountMapping);

	map->crypticID = id;
	map->username = StructAllocString(username);
	map->password = StructAllocString(password);
	map->vivoxID = acctId;

	cvVerifyAccountInternal(map);
}

const char* cvChannelToExternTmp(ChatChannel *channel)
{
	static char buf[MAX_PATH];

	if(channel->uKey)
		sprintf(buf, "chan_%s_%d", GetShardNameFromShardInfoString(), channel->uKey);
	else
		sprintf(buf, "chan_%s_%s", GetShardNameFromShardInfoString(), channel->name);

	return buf;
}

void cvChanFindCB(ChannelRef *cref, const char* xmldoc, int len)
{
	static StashTable st = NULL;
	XMLExtractState state;
	ChatChannel *chan;
	char *chanid = NULL, *chanuri = NULL, *code = NULL, *dbcode = NULL, *status = NULL;

	if(!st)
		st = stashTableCreateWithStringKeys(10, StashDeepCopyKeys);

	chan = objGetContainerData(GLOBALTYPE_CHATCHANNEL, cref->key);
	if(!chan)
		chan = channelFindByName(cref->name);

	stashTableClearEx(st, NULL, simpleFree);

	cvExtractInit(&state, st);
	stashAddPointer(st, "id", NULL, true);
	stashAddPointer(st, "uri", NULL, true);
	stashAddPointer(st, "code", NULL, true);
	stashAddPointer(st, "db_code", NULL, true);
	stashAddPointer(st, "status", NULL, true);
	cvExtractKeysValues(xmldoc, len, &state);
	stashFindPointer(st, "id", &chanid);
	stashFindPointer(st, "uri", &chanuri);
	stashFindPointer(st, "code", &code);
	stashFindPointer(st, "db_code", &dbcode);
	stashFindPointer(st, "status", &status);

	if(!chanid || !chanuri)
		return;

	if(chan)
	{
		int i;
		chan->voiceId = atoi(chanid);
		chan->voiceURI = StructAllocString(chanuri);

		for(i=0; i<ea32Size(&chan->online); i++)
		{
			ContainerID userid = chan->online[i];
			ChatUser *user = objGetContainerData(GLOBALTYPE_CHATUSER, userid);

			if(user)
				chatVoiceJoin(chan, user);
		}
	}

	StructDestroy(parse_ChannelRef, cref);
}

void chatVoiceFindChannel(ChatChannel *channel)
{
	ChannelRef *cref = StructCreate(parse_ChannelRef);
	UrlArgumentList args = {0};

	if(!channel)
		return;

	cref->key = channel->uKey;
	cref->name = StructAllocString(channel->name);

	urlAddValue(&args, "mode", "get", HTTPMETHOD_GET);
	urlAddValue(&args, "chan_name", cvChannelToExternTmp(channel), HTTPMETHOD_GET);
	cvSetURL(&args.pBaseURL, VIVOX_CHAN_FIND);

	chatVoiceProcessPage(&args, cvChanFindCB, cref, true);
	StructDeInit(parse_UrlArgumentList, &args);
}

void cvChanCreateCB(ChannelRef *cref, const char* xmldoc, int len)
{
	static StashTable st = NULL;
	XMLExtractState state;
	ChatChannel *chan;
	char *chanid = NULL, *chanuri = NULL, *code = NULL, *dbcode = NULL, *status = NULL;

	if(!st)
		st = stashTableCreateWithStringKeys(10, StashDeepCopyKeys);

	chan = channelFindByID(cref->key);
	if(!chan)
		chan = channelFindByName(cref->name);

	if(!chan)
	{
		StructDestroy(parse_ChannelRef, cref);
		return;
	}

	stashTableClearEx(st, NULL, simpleFree);

	cvExtractInit(&state, st);
	stashAddPointer(st, "chan_id", NULL, true);
	stashAddPointer(st, "chan_uri", NULL, true);
	stashAddPointer(st, "code", NULL, true);
	stashAddPointer(st, "db_code", NULL, true);
	stashAddPointer(st, "status", NULL, true);
	cvExtractKeysValues(xmldoc, len, &state);
	stashFindPointer(st, "chan_id", &chanid);
	stashFindPointer(st, "chan_uri", &chanuri);
	stashFindPointer(st, "code", &code);
	stashFindPointer(st, "db_code", &dbcode);
	stashFindPointer(st, "status", &status);

	// TODO: succeed_if_exists param could bypass this
	if(status && !stricmp(status, "ERR") && code)
	{
		int c = atoi(code);
		
		switch(c)
		{
			xcase 709: {			// Channel create failed
				if(dbcode) 
				{
					int d = atoi(dbcode);

					switch(d)
					{
						xcase 1500: {	// Channel already exists
							chatVoiceFindChannel(chan);
							return;
						}
					}
				}
			}	
		}
	}

	if(!chanid || !chanuri)
		return;

	if(chan)
	{
		int i;
		chan->voiceId = atoi(chanid);
		chan->voiceURI = StructAllocString(chanuri);

		for(i=0; i<ea32Size(&chan->online); i++)
		{
			ContainerID userid = chan->online[i];
			ChatUser *user = objGetContainerData(GLOBALTYPE_CHATUSER, userid);

			if(user)
				chatVoiceJoin(chan, user);
		}
	}
}

void chatVoiceCreateChannel(ChatChannel *channel)
{
	ChannelRef *cref = StructCreate(parse_ChannelRef);
	UrlArgumentList args = {0};

	cref->key = channel->uKey;
	cref->name = StructAllocString(channel->name);

	urlAddValue(&args, "mode", "create", HTTPMETHOD_GET);
	urlAddValue(&args, "chan_name", cvChannelToExternTmp(channel), HTTPMETHOD_GET);
	urlAddValue(&args, "chan_desc", channel->description, HTTPMETHOD_GET);
	urlAddValue(&args, "preamble_ads", "0", HTTPMETHOD_GET);
	cvSetURL(&args.pBaseURL, VIVOX_CHAN_CREATE);

	chatVoiceProcessPage(&args, cvChanCreateCB, cref, true);
	StructDeInit(parse_UrlArgumentList, &args);
}

void chatVoiceChannelDelete(ChatChannel* channel)
{
	UrlArgumentList args = {0};
	char buf[MAX_PATH];

	sprintf(buf, "%d", channel->uKey);

	urlAddValue(&args, "mode", "delete", HTTPMETHOD_GET);
	urlAddValue(&args, "chan_id", buf, HTTPMETHOD_GET);
	cvSetURL(&args.pBaseURL, VIVOX_CHAN_DELETE);

	chatVoiceProcessPage(&args, NULL, NULL, true);

	channel->voiceId = 0;
	StructFreeStringSafe(&channel->voiceURI);
}

void cvChannelJoinCB(UserChannel *data, const char* xmldoc, int len)
{
	ChatUser *user = userFindByContainerId(data->userId);
	ChatChannel *chan = channelFindByID(data->chanId);
	ChatChannelInfo *info = NULL;

	if(!chan)
		chan = channelFindByName(data->chanName);

	if(!chan || !user)
		return;
	
	info = ChatServerCreateChannelUpdate(user, chan, CHANNELUPDATE_VOICE_ENABLED);

	if (info)
	{
		ChannelSendUpdateToUser(user, info, CHANNELUPDATE_VOICE_ENABLED);
		StructDestroy(parse_ChatChannelInfo, info);
	}

	StructDestroy(parse_UserChannel, data);
}

void chatVoiceJoin(ChatChannel *channel, ChatUser *user)
{
	UrlArgumentList args = {0};
	char buf[MAX_PATH];
	UserChannel *data;

	if(!channel || !user)
		return;
	if(!channel->voiceId)
		return;				// This is still in the process of being created - cvChanCreateCB will join this user
	if(!user->voice_accountid)
		return;				// Still in process of being created or retrieved - cvAcctCreateCB will join this user

	urlAddValue(&args, "mode", "acl_add", HTTPMETHOD_GET);
	sprintf(buf, "%d", channel->voiceId);
	urlAddValue(&args, "chan_id", buf, HTTPMETHOD_GET);
	sprintf(buf, "%d", user->voice_accountid);
	urlAddValue(&args, "user_id", buf, HTTPMETHOD_GET);
	cvSetURL(&args.pBaseURL, VIVOX_CHAN_JOIN);

	data = StructCreate(parse_UserChannel);
	data->userId = user->id;
	data->chanId = channel->uKey;
	data->chanName = StructAllocString(channel->name);

	devassert(objGetContainerData(GLOBALTYPE_CHATUSER, user->id));
	chatVoiceProcessPage(&args, cvChannelJoinCB, data, true);
	StructDeInit(parse_UrlArgumentList, &args);
}

void chatVoiceLeave(ChatChannel *channel, ChatUser *user)
{
	UrlArgumentList args = {0};
	char buf[MAX_PATH];

	if(!channel->voiceId)
		return;				// Still in process of being created, no need to leave then

	urlAddValue(&args, "mode", "acl_delete", HTTPMETHOD_GET);
	sprintf(buf, "%d", channel->voiceId);
	urlAddValue(&args, "chan_id", buf, HTTPMETHOD_GET);
	sprintf(buf, "%d", user->voice_accountid);
	urlAddValue(&args, "user_id", buf, HTTPMETHOD_GET);
	cvSetURL(&args.pBaseURL, VIVOX_CHAN_LEAVE);

	devassert(objGetContainerData(GLOBALTYPE_CHATUSER, user->id));
	chatVoiceProcessPage(&args, NULL, NULL, true);
	StructDeInit(parse_UrlArgumentList, &args);
}

AUTO_TRANSACTION
ATR_LOCKS(data, ".iVoiceAccountID, .pchVoiceUsername, .pchVoicePassword");
enumTransactionOutcome tr_cvSetVoiceAccountData(ATR_ARGS, NOCONST(GameAccountData) *data, const char* username, const char* password, int accountid)
{
	StructFreeString(data->pchVoiceUsername);
	StructFreeString(data->pchVoicePassword);

	data->pchVoiceUsername = StructAllocString(username);
	data->pchVoicePassword = StructAllocString(password);
	data->iVoiceAccountID  = accountid;

	return TRANSACTION_OUTCOME_SUCCESS;
}

//Currently unused
AUTO_TRANSACTION
ATR_LOCKS(data, ".iVoiceAccountID");
enumTransactionOutcome tr_cvSetVoiceAccountID(ATR_ARGS, NOCONST(GameAccountData) *data, int accountid)
{
	data->iVoiceAccountID  = accountid;

	return TRANSACTION_OUTCOME_SUCCESS;
}

const char* cvGenerateRandomPassword(void)
{
	static char buf[30];
	char allowed[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	int i;
	int n = randomIntRange(12, 15);
	int c = ARRAY_SIZE(allowed);

	for(i=0; i<n; i++)
		buf[i] = allowed[randomIntRange(0, c-1)];
	buf[i+1] = '\0';

	return buf;
}

HttpFormRequest* chatVoiceProcessPageEx(UrlArgumentList *list, ResponseHandler cb, void* userdata, u32VoidStarFunc timeoutcb, void* timeoutdata, int owned, const char* file, int line)
{
	HttpFormRequest *req = StructCreate(parse_HttpFormRequest);
	req->args = StructClone(parse_UrlArgumentList, list);
	req->cb = cb;
	req->userdata = userdata;
	req->timeoutcb = timeoutcb;
	req->timeoutdata = timeoutdata;
	req->file = file;
	req->line = line;

	if(owned)
	{
		eaPush(&g_VoiceChatState.http_requests, req);
		return NULL;
	}

	return req;
}

void cvHttpBody(const char *response, int len, int response_code, HttpFormRequest *req)
{
	g_VoiceChatState.timeLastReqSuccess = ABS_TIME;
	g_VoiceChatState.failuresSinceLastSuccess = 0;

	if(response_code!=200)
	{
		//req->errorcb(response, len, response_code);
		printfColor(COLOR_RED, "(%p) Error: %d %s\n", req, response_code, req->backup_args->pBaseURL);
		WARNING_NETOPS_ALERT("VOICE_REQ_ERROR", "Voice request error: %d, %s", response_code, req->backup_args->pBaseURL);
		servLogWithStruct(LOG_VOICE_CHAT, "Request_Error", req, parse_HttpFormRequest);
	}
	else if(req->attempt_count > 0)
		printfColor(COLOR_GREEN,"(%p) Success after %d attempts: %s\n", req, req->attempt_count, req->backup_args->pBaseURL);

	// If not signed in, we're in failure mode, so drop the request silently - we'll redo it later anyways
	if(req->cb && (g_VoiceChatState.signed_in || req->isSignin))
		req->cb(req->userdata, response, len);

	eaFindAndRemoveFast(&g_VoiceChatState.active_reqs, req);
	StructDestroy(parse_HttpFormRequest, req);
}

void cvHttpTimeout(HttpFormRequest *req)
{
	U32 handlerRes;
	g_VoiceChatState.failuresSinceLastSuccess++;

	printfColor(COLOR_RED|COLOR_GREEN, "(%p) Timeout: %s\n", req, req->backup_args->pBaseURL);

	req->args = req->backup_args;
	req->backup_args = NULL;
	req->attempt_count++;

	// Silently kill the request if we go into failure mode, unless of course it's the signin, which handles itself
	if((g_VoiceChatState.signed_in || g_VoiceChatState.signing_in) && !req->isSignin)
	{
		eaFindAndRemoveFast(&g_VoiceChatState.active_reqs, req);
		StructDestroy(parse_HttpFormRequest, req);
		return;
	}

	if(req->timeoutcb)
	{
		handlerRes = req->timeoutcb(req->timeoutdata);

		if(handlerRes)
		{
			eaFindAndRemoveFast(&g_VoiceChatState.active_reqs, req);
			StructDestroy(parse_HttpFormRequest, req);
			return;
		}
	}

	if(req->attempt_count<10)
		chatVoiceTakeRequest(req);
	else
	{
		ErrorDetailsf("%s:%d - %s", req->file, req->line, req->args->pBaseURL);
		Errorf("Unable to complete Vivox request");
		servLogWithStruct(LOG_VOICE_CHAT, "Request_Fail", req, parse_HttpFormRequest);
		WARNING_NETOPS_ALERT("VOICE_REQ_FAIL", "Failed to process request 10 times");

		eaFindAndRemoveFast(&g_VoiceChatState.active_reqs, req);
		StructDestroy(parse_HttpFormRequest, req);
	}
}

void chatVoiceProcessRequest(HttpFormRequest *req)
{
	HttpClient *hc = NULL;
	const char* tunnelHost;
	int tunnelPort;
	int isHTTPS = 0;
	UrlArgumentList *args = req->args;
	
	isHTTPS = strStartsWith(req->args->pBaseURL, "https");

	if(!req->backup_args)
		req->backup_args = StructClone(parse_UrlArgumentList, req->args);
	req->args = NULL;		// haSecureRequest/haRequest take ownership of args, keep backup above for dbg and retry

	eaPush(&g_VoiceChatState.active_reqs, req);

	if(isHTTPS)
	{
		tunnelHost = g_TunnelHostOverride;
		tunnelPort = g_TunnelPortOverride;

		haSecureRequest(commDefault(), tunnelHost, tunnelPort, &args, cvHttpBody, cvHttpTimeout, 60, req);
	}
	else
		haRequest(commDefault(), &args, cvHttpBody, cvHttpTimeout, 0, req);
}

static void cvXMLStart(XMLExtractState *state, const char* el, const char **attr)
{
	state->curState = NULL;

	if(stashFindPointer(state->table, el, NULL))
		state->curState = el;
}

static void cvXMLEnd(XMLExtractState *state, const char *el)
{
	state->curState = NULL;
}

static void cvXMLCharacters(XMLExtractState *state, const XML_Char *s, int len)
{
	if(state->curState && !stashFindPointer(state->table, s, NULL))
	{
		char *copy = calloc(1, sizeof(char)*(len+1));
		strncpy_s(copy, len+1, s, len);
		stashAddPointer(state->table, state->curState, copy, true);
		state->keysFound++;
	}
}

void cvExtractInit(XMLExtractState *state, StashTable table)
{
	state->curState = NULL;
	state->table = table;
	state->keysFound = 0;
}

void cvExtractKeysValues(const char* xmldoc, int len, XMLExtractState *state)
{
	XML_Parser p = XML_ParserCreate(NULL);
	int xmlRet;

	XML_SetUserData(p, state);
	XML_SetElementHandler(p, cvXMLStart, cvXMLEnd);
	XML_SetCharacterDataHandler(p, cvXMLCharacters);

	xmlRet = XML_Parse(p, xmldoc, len, true);
	XML_ParserFree(p);
}

void simpleFree(void* ptr)
{
	free(ptr);
}

void chatVoiceLoginCB(void* unused, const char* xmldoc, int len)
{
	static StashTable st = NULL;
	XMLExtractState state;
	char* status = NULL, *auth = NULL, *acct = NULL, *disp = NULL;

	if(!st)
		st = stashTableCreateWithStringKeys(10, StashDeepCopyKeys);

	stashTableClearEx(st, NULL, simpleFree);

	cvExtractInit(&state, st);
	stashAddPointer(st, "auth_token", NULL, true);
	stashAddPointer(st, "status", NULL, true);
	stashAddPointer(st, "account_id", NULL, true);
	stashAddPointer(st, "displayname", NULL, true);
	cvExtractKeysValues(xmldoc, len, &state);

	stashFindPointer(st, "status", &status);
	stashFindPointer(st, "auth_token", &auth);
	stashFindPointer(st, "account_id", &acct);
	stashFindPointer(st, "displayname", &disp);

	g_VoiceChatState.signing_in = 0;
	if(stricmp(status, "Ok")==0)
	{
		g_VoiceChatState.signed_in = 1;
		g_VoiceChatState.auth_token = strdup(auth);

		if(isDevelopmentMode())
			printfColor(COLOR_GREEN, "Signed into Vivox (%s): %s\n", gShardChatServerConfig.pchVoiceServerDev, auth);
		else
			printfColor(COLOR_GREEN, "Signed into Vivox (%s): %s\n", gShardChatServerConfig.pchVoiceServer, auth);
	}
	else
	{
		g_VoiceChatState.signed_in = 0;
		printf("Failed to sign into Vivox: %s\n", status);
	}

	servLog(LOG_VOICE_CHAT, "Login_Success", 
							"Login successful - processing %d users, %d channels", 
							stashGetCount(chat_db.user_names), 
							stashGetCount(chat_db.channel_names));

	FOR_EACH_IN_STASHTABLE(chat_db.user_names, ChatUser, user)
	{
		cvUserLoggedIn(user->id);
	}
	FOR_EACH_END;

	FOR_EACH_IN_STASHTABLE(chat_db.channel_names, ChatChannel, chan)
	{
		chatVoiceCreateChannel(chan);
	}
	FOR_EACH_END;

	g_VoiceChatState.timeLastSignin = ABS_TIME;
}

U32 cvLoginTimeout(void* userdata)
{
	g_VoiceChatState.signing_in = false;
	g_VoiceChatState.timeLastSignin = ABS_TIME;

	CRITICAL_NETOPS_ALERT("VOICE_LOGIN_TIMEOUT", "Failed to sign into Vivox");
	printfColor(COLOR_RED, "Failed to sign into Vivox - timeout\n");

	return true;
}

HttpFormRequest* chatVoiceSignin(void)
{
	UrlArgumentList args = {0};
	HttpFormRequest *request;

	printf("Signing into Vivox\n");

	if(isDevelopmentMode())
	{
		urlAddValue(&args, "userid", gShardChatServerConfig.pchVoiceAdminDev, HTTPMETHOD_GET);
		urlAddValue(&args, "pwd", gShardChatServerConfig.pchVoicePasswordDev, HTTPMETHOD_GET);
	}
	else
	{
		urlAddValue(&args, "userid", gShardChatServerConfig.pchVoiceAdmin, HTTPMETHOD_GET);
		urlAddValue(&args, "pwd", gShardChatServerConfig.pchVoicePassword, HTTPMETHOD_GET);	
	}
	cvSetURL(&args.pBaseURL, VIVOX_SIGNIN);

	servLog(LOG_VOICE_CHAT, "Voice_Login", "Logging into Vivox: %s", args.pBaseURL);

	request =  chatVoiceProcessPageEx(&args, chatVoiceLoginCB, NULL, cvLoginTimeout, NULL, false, __FILE__, __LINE__);
	request->isSignin = true;
	StructDeInit(parse_UrlArgumentList, &args);
	return request;
}

void chatVoiceTakeRequest(HttpFormRequest *req)
{
	eaPushUnique(&g_VoiceChatState.http_requests, req);
}

void cvSignOut(void)
{
	CRITICAL_NETOPS_ALERT("VOICE_CHAT_DOWN", 
							"Voice chat entering failure mode - errors: %d, active conns %d", 
							g_VoiceChatState.failuresSinceLastSuccess,
							eaSize(&g_VoiceChatState.active_reqs));

	g_VoiceChatState.signed_in = 0;
	g_VoiceChatState.signing_in = 0;

	eaClearStruct(&g_VoiceChatState.http_requests, parse_HttpFormRequest);
	stashTableClearStruct(queuedRequests, NULL, parse_UserQueue);
}

char* cvUserNameFromIdTmp(ContainerID id)
{
	static char buf[2000];

	sprintf(buf, "user_%s_%d", GetShardNameFromShardInfoString(), id);

	return buf;
}

void cvInit(void)
{
	g_VoiceChatState.timeLastReqSuccess = ABS_TIME;
	g_VoiceChatState.failuresSinceLastSuccess = 0;

	queuedRequests = stashTableCreateInt(10);

	if(g_VoiceAPIServerOverride)
	{
		gShardChatServerConfig.pchVoiceServer = StructAllocString(g_VoiceAPIServerOverride);
		gShardChatServerConfig.pchVoiceServerDev = StructAllocString(g_VoiceAPIServerOverride);
	}

	if(!gShardChatServerConfig.pchVoiceServer)
		gShardChatServerConfig.pchVoiceServer = StructAllocString(VIVOX_API_DEF);

	if(!gShardChatServerConfig.pchVoiceAdmin)
		gShardChatServerConfig.pchVoiceAdmin = StructAllocString(VIVOX_ADMIN_USERID);

	if(!gShardChatServerConfig.pchVoicePassword)
		gShardChatServerConfig.pchVoicePassword = StructAllocString(VIVOX_ADMIN_PWD);

	if(!gShardChatServerConfig.pchVoiceServerDev)
		gShardChatServerConfig.pchVoiceServerDev = StructAllocString(VIVOX_API_DEF);

	if(!gShardChatServerConfig.pchVoiceAdminDev)
		gShardChatServerConfig.pchVoiceAdminDev = StructAllocString(VIVOX_ADMIN_USERID);

	if(!gShardChatServerConfig.pchVoicePasswordDev)
		gShardChatServerConfig.pchVoicePasswordDev = StructAllocString(VIVOX_ADMIN_PWD);

}

void cvOncePerFrame(void)
{
	static int inited = 0;
	if(!cvIsVoiceEnabled())
		return;

	if(!inited)
	{
		inited = 1;

		cvInit();
	}

	if(!g_VoiceChatState.signed_in && 
		!g_VoiceChatState.signing_in && 
		eaSize(&g_VoiceChatState.active_reqs) == 0 &&
		ABS_TIME_SINCE(g_VoiceChatState.timeLastSignin) > SEC_TO_ABS_TIME(5*60) )
	{
		HttpFormRequest *req = chatVoiceSignin();

		g_VoiceChatState.signing_in = true;

		chatVoiceProcessRequest(req);
	}

	if(g_VoiceChatState.signed_in && 
		g_VoiceChatState.failuresSinceLastSuccess > g_VoiceErrorCountBeforeDeactivate && 
		ABS_TIME_SINCE(g_VoiceChatState.timeLastReqSuccess) > SEC_TO_ABS_TIME(g_VoiceErrorTimeBeforeDeactivate))
	{
		// Something isn't going well.  Probably lost connection to voice server, so restart it.
		// This will drop all requests until we decide we're back in a good state with the server.
		cvSignOut();
	}

	if(g_VoiceChatState.signed_in)
	{
		if(eaSize(&g_VoiceChatState.active_reqs)<g_VoiceMaxActiveRequests)
		{
			int numOpen = g_VoiceMaxActiveRequests - eaSize(&g_VoiceChatState.active_reqs);
			int i;
			for(i=0; i<numOpen && i<eaSize(&g_VoiceChatState.http_requests); i++)
			{
				HttpFormRequest *req = g_VoiceChatState.http_requests[i];

				urlAddValue(req->args, "auth_token", g_VoiceChatState.auth_token, HTTPMETHOD_GET);
				chatVoiceProcessRequest(req);
			}
			eaRemoveRange(&g_VoiceChatState.http_requests, 0, numOpen);
		}

		FOR_EACH_IN_STASHTABLE(queuedRequests, UserQueue, q)
		{
			ChatUser *user = objGetContainerData(GLOBALTYPE_CHATUSER, q->id);
			GADRef *r = NULL;
			GameAccountData *gad;

			if(!user)
				continue;

			if(!stashIntFindPointer(chat_db.gad_by_id, user->id, &r) || !GET_REF(r->hGAD))
				continue;

			gad = GET_REF(r->hGAD);

			if(q->loggedin)
			{
				if(gad->pchVoiceUsername)
				{
					// Assume it is valid in production mode - if client fails to authenticate, it will force verify
					if(!isProductionMode() || q->forceVerify)
						cvVerifyAccount(user->id, gad->pchVoiceUsername, gad->pchVoicePassword, gad->iVoiceAccountID);
					else
						user->voice_accountid = gad->iVoiceAccountID;
				}
				else
					cvCreateAccount(user->id, cvUserNameFromIdTmp(user->id), cvGenerateRandomPassword());
			}

			FOR_EACH_IN_EARRAY_FORWARDS(q->requests, HttpFormRequest, req)
			{
				chatVoiceTakeRequest(req);
			}
			FOR_EACH_END;

			eaClearFast(&q->requests);

			stashIntRemovePointer(queuedRequests, user->id, NULL);
			StructDestroy(parse_UserQueue, q);
		}
		FOR_EACH_END;
	}
}

AUTO_COMMAND_REMOTE;
void ChatServerUserLoggedIn(ContainerID userId)
{
	if(!cvIsVoiceEnabled())
		return;

	cvUserLoggedIn(userId);
}

AUTO_COMMAND_REMOTE;
void ChatServerVoice_Verify(ContainerID userId)
{
	UserQueue *q = cvGetUserQueue(userId);

	if(!q)
		return;

	q->loggedin = true;
	q->forceVerify = true;
}

AUTO_COMMAND ACMD_CMDLINE ACMD_NAME(VoiceOverride);
void cvVoiceOverride(U32 on)
{
	g_VoiceOverride = !!on;
}

AUTO_COMMAND;
void cvVoiceGetGAD(int accountID)
{
	RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), STACK_SPRINTF("%d", accountID));
}

AUTO_COMMAND;
void cvFakeErrorCount(int count)
{
	g_VoiceChatState.failuresSinceLastSuccess = count;
}

#include "chatVoice_h_ast.c"
#include "chatVoice_c_ast.c"
