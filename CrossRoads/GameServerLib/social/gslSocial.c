#include "gslSocial.h"
#include "StoredCredentialsCommon.h"
#include "earray.h"
#include "objTransactions.h"
#include "AutoTransDefs.h"
#include "accountnet.h"
#include "Entity.h"
#include "Player.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "utils.h"
#include "GlobalTypes.h"
#include "mission_common.h"
#include "itemCommon.h"
#include "gslChat.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static bool g_disable_social = false;
AUTO_CMD_INT(g_disable_social, DisableSocial) ACMD_CMDLINE;

static bool g_allow_superuser = false;
AUTO_COMMAND ACMD_NAME(social_allow_superuser) ACMD_CMDLINE ACMD_SERVERCMD ACMD_ACCESSLEVEL(9);
void cmd_allow_superuser(int allow)
{
	g_allow_superuser = allow;
}

typedef struct gslSocialImplementation
{
	const char *name;
	gslSocialInvokeCB invoke;
	gslSocialEnrollCB enroll;
} gslSocialImplementation;
static gslSocialImplementation **g_implementations[kActivityType_Count+2] = {0};
static gslSocialImplementation **g_implementations_enroll = NULL;
static const char *g_cryptic = NULL;

typedef struct gslSocialActivityEntry
{
	EntityRef entref;
	ActivityType type;
	const char *service;
	void *data;
} gslSocialActivityEntry;

void gslSocialRegister(const char *service, ActivityType type, gslSocialInvokeCB invoke_cb)
{
	gslSocialImplementation *imp = calloc(1, sizeof(gslSocialImplementation));
	imp->name = allocAddStaticString(service);
	imp->invoke = invoke_cb;
	eaPush(&g_implementations[type], imp);
}

void gslSocialRegisterEnrollment(const char *service, gslSocialEnrollCB enroll_cb)
{
	gslSocialImplementation *imp = calloc(1, sizeof(gslSocialImplementation));
	imp->name = allocAddStaticString(service);
	imp->enroll = enroll_cb;
	eaPush(&g_implementations_enroll, imp);
}

AUTO_RUN;
void gclSocialAutoRun(void)
{
	int i;
	for(i=0; i<kActivityType_Count; i++)
	{
		allocAddStaticString(StaticDefineIntRevLookup(ActivityTypeEnum, i));
	}
	g_cryptic = allocAddStaticString("Cryptic");
}

AUTO_TRANSACTION
ATR_LOCKS(pEntity, ".Pplayer.Pactivity");
enumTransactionOutcome Social_tr_DisableService(ATR_ARGS, NOCONST(Entity) *pEntity, const char *type, const char *service, U32 disable)
{
	bool found = false;
	const char *pool_type = allocAddString(type);
	const char *pool_service = allocAddString(service);
	if(ISNULL(pEntity) || ISNULL(pEntity->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;
	if(!pEntity->pPlayer->pActivity)
		pEntity->pPlayer->pActivity = StructCreateNoConst(parse_PlayerActivity);
	FOR_EACH_IN_EARRAY(pEntity->pPlayer->pActivity->disabled, NOCONST(PlayerActivityEntry), pae)
		if(pae->service == pool_service && pae->type == pool_type)
		{
			found = true;
			if(!disable)
				eaRemove(&pEntity->pPlayer->pActivity->disabled, FOR_EACH_IDX(pEntity->pPlayer->pActivity->disabled, pae));
			break;
		}
	FOR_EACH_END
	if(disable && !found)
	{
		NOCONST(PlayerActivityEntry) *disable_entry = StructCreateNoConst(parse_PlayerActivityEntry);
		disable_entry->service = pool_service;
		disable_entry->type = pool_type;
		eaPush(&pEntity->pPlayer->pActivity->disabled, disable_entry);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEntity, ".Pplayer.Pactivity");
enumTransactionOutcome Social_tr_SetServiceVerbosity(ATR_ARGS, NOCONST(Entity) *pEntity, const char *service, U32 level)
{
	bool found = false;
	const char *pool_service = allocAddString(service);
	if(ISNULL(pEntity) || ISNULL(pEntity->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;
	if(!pEntity->pPlayer->pActivity)
		pEntity->pPlayer->pActivity = StructCreateNoConst(parse_PlayerActivity);
	FOR_EACH_IN_EARRAY(pEntity->pPlayer->pActivity->verbosity, NOCONST(PlayerActivityVerbosity), pav)
		if(pav->service == pool_service)
		{
			found = true;
			pav->level = level;
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	FOR_EACH_END
	if(!found)
	{
		NOCONST(PlayerActivityVerbosity) *pav = StructCreateNoConst(parse_PlayerActivityVerbosity);
		pav->service = pool_service;
		pav->level = level;
		eaPush(&pEntity->pPlayer->pActivity->verbosity, pav);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEntity, ".Pplayer.Pactivity");
enumTransactionOutcome Social_tr_UpdateEnrollment(ATR_ARGS, NOCONST(Entity) *pEntity, const char *service, const U32 state, const char *userdata)
{
	int i;
	bool found = false;
	const char *pool_service = allocAddString(service);
	if(ISNULL(pEntity) || ISNULL(pEntity->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;
	if(!pEntity->pPlayer->pActivity)
		pEntity->pPlayer->pActivity = StructCreateNoConst(parse_PlayerActivity);
	for(i=eaSize(&pEntity->pPlayer->pActivity->enrollment)-1; i>=0; i--)
	{
		NOCONST(PlayerActivityEnrollment) *pae = pEntity->pPlayer->pActivity->enrollment[i];
		if(pae->service == pool_service)
		{
			found = true;
			if(state == 0)
			{
				StructDestroyNoConst(parse_PlayerActivityEnrollment, pae);
				eaRemoveFast(&pEntity->pPlayer->pActivity->enrollment, i);
			}
			else
			{
				pae->state = state;
				estrCopy2(&pae->userdata, userdata);
			}
			break;
		}
	}
	if(!found && state)
	{
		NOCONST(PlayerActivityEnrollment) *pae = StructCreateNoConst(parse_PlayerActivityEnrollment);
		pae->service = service;
		pae->state = state;
		estrCopy2(&pae->userdata, userdata);
		eaPush(&pEntity->pPlayer->pActivity->enrollment, pae);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEntity, ".Pplayer.Pactivity.Enrollment");
enumTransactionOutcome Social_tr_ClearEnrollment(ATR_ARGS, NOCONST(Entity) *pEntity)
{
	if(ISNULL(pEntity) || ISNULL(pEntity->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;
	if(NONNULL(pEntity->pPlayer->pActivity))
		eaDestroy(&pEntity->pPlayer->pActivity->enrollment);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void gslSocialUpdateEnrollment(Entity *ent, const char *service, const U32 state, const char *userdata)
{
	AutoTrans_Social_tr_UpdateEnrollment(NULL, GLOBALTYPE_GAMESERVER, entGetType(ent), entGetContainerID(ent), service, state, userdata);
}

void gslSocialGetEnrollment(Entity *ent, const char *service, U32 *state_out, char **userdata_out)
{
	int i;
	const char *pool_service = allocAddString(service);
	Player *player = entGetPlayer(ent);
	assert(player);
	if(player->pActivity)
	{
		for(i=eaSize(&player->pActivity->enrollment)-1; i>=0; i--)
		{
			PlayerActivityEnrollment *pae = player->pActivity->enrollment[i];
			if(pae->service == service)
			{
				if(state_out)
					*state_out = pae->state;
				if(userdata_out)
					estrCopy(userdata_out, (const char **)&pae->userdata);
				return;
			}
		}
	}
	if(state_out)
		*state_out = 0;
	if(userdata_out)
		*userdata_out = NULL;
}

void gslSocialEnroll(Entity *ent, const char *service, const char *input)
{
	int i;
	U32 state;
	char *userdata = NULL;
	const char *pool_service = allocAddString(service);
	gslSocialGetEnrollment(ent, pool_service, &state, &userdata);
	for(i=eaSize(&g_implementations_enroll)-1; i>=0; i--)
	{
		gslSocialImplementation *imp = g_implementations_enroll[i];
		if(imp->name == pool_service)
			imp->enroll(ent, service, state, userdata, input);
	}
	estrDestroy(&userdata);
}

static void freeActivityData(ActivityType type, void *data)
{
	switch(type)
	{
	case kActivityType_Status:
	case kActivityType_Perk:
	case kActivityType_GuildCreate:
	case kActivityType_GuildJoin:
	case kActivityType_GuildLeave:
		SAFE_FREE(data);
		break;
	case kActivityType_Screenshot:
		{
			ActivityDataScreenshot *screenshot = data;
			SAFE_FREE(screenshot->data);
			SAFE_FREE(screenshot->title);
			SAFE_FREE(screenshot);
		}
		break;
	case kActivityType_Blog:
		{
			ActivityDataBlog *blog = data;
			SAFE_FREE(blog->title);
			SAFE_FREE(blog->text);
			SAFE_FREE(blog);
		}
		break;
	case kActivityType_Item:
		{
			ActivityDataItem *item = data;
			SAFE_FREE(item->name);
			SAFE_FREE(item->def_name);
			SAFE_FREE(item);
		}
		break;
	}
}

void gslSocialActivityEx(Entity *ent, ActivityType type, const char *service, void *data)
{
	Player *player;
	const char *typestr, *servicestr;
	void *activity_data = data;
	StoredCredentials creds;

	if(g_disable_social)
	{
		freeActivityData(type, data);
		return;
	}

	player = entGetPlayer(ent);
	if(!player)
	{
		freeActivityData(type, data);
		return;
	}

	// Block all social features from AL'd characters
	if(!g_allow_superuser && player->accessLevel != ACCESS_USER)
	{
		freeActivityData(type, data);
		return;
	}

	devassert(type >= 0 && type < kActivityType_Count);
	typestr = allocAddString(StaticDefineIntRevLookup(ActivityTypeEnum, type));
	servicestr = service ? allocAddString(service) : NULL;

	if(type == kActivityType_Perk)
		activity_data = missiondef_DefFromRefString(data);
	else if(type == kActivityType_Item)
		((ActivityDataItem*)data)->def = item_DefFromName(((ActivityDataItem*)data)->def_name);

	FOR_EACH_IN_EARRAY(g_implementations[type], gslSocialImplementation, imp)
		if(servicestr && servicestr != imp->name)
			continue;
		if(imp->name == g_cryptic)
		{
			// Always invoke the "Cryptic" handler
			imp->invoke(ent, imp->name, type, NULL, kActivityVerbosity_Default, activity_data);
			continue;
		}

		StoredCredentialsFromPlayer(player, &creds, imp->name);
		if((creds.user && creds.user[0]) || (creds.token && creds.token[0]) || (creds.secret && creds.secret[0]))
		{
			bool disabled = false;
			ActivityVerbosity verbosity = kActivityVerbosity_Default;
			if(player->pActivity && !service)
			{
				FOR_EACH_IN_EARRAY(player->pActivity->disabled, PlayerActivityEntry, pae)
					if(pae->service == imp->name && pae->type == typestr)
					{
						disabled = true;
						break;
					}
				FOR_EACH_END
				if(disabled)
					continue;

				FOR_EACH_IN_EARRAY(player->pActivity->verbosity, PlayerActivityVerbosity, pav)
					if(pav->service == imp->name)
					{
						verbosity = pav->level;
						break;
					}
				FOR_EACH_END
			}
			imp->invoke(ent, imp->name, type, &creds, verbosity, activity_data);
		}
	FOR_EACH_END

	freeActivityData(type, data);
}

void gslSocialActivity(Entity *ent, ActivityType type, void *data)
{
	gslSocialActivityEx(ent, type, NULL, data);
	if (type == kActivityType_GuildCreate ||
		type == kActivityType_GuildJoin ||
		type == kActivityType_GuildLeave)
		ServerChat_PlayerUpdate(ent, CHATUSER_UPDATE_SHARD);
}

const char *gslGetCurrentActivity(Entity *ent)
{
	if (!ent || !ent->pPlayer)
		return NULL;
	return ent->pPlayer->pchActivityString;
}

void gslSetCurrentActivity(Entity *ent, const char *string)
{
	if (!ent || !ent->pPlayer)
	{
		return;		
	}
	if (strcmp_safe(string, ent->pPlayer->pchActivityString) == 0)
		return;
	// String is different then old value, so set the string
	if (ent->pPlayer->pchActivityString)
	{
		SAFE_FREE(ent->pPlayer->pchActivityString);
	}
	if (string)
	{
		ent->pPlayer->pchActivityString = strdup(string);
	}

	entity_SetDirtyBit(ent, parse_Player, ent->pPlayer, false);
}

bool gslSocialServiceRegistered(const char *service, ActivityType type)
{
	// If only checking a specfic type
	if(type < kActivityType_Count)
	{
		FOR_EACH_IN_EARRAY(g_implementations[type], gslSocialImplementation, imp)
			if(stricmp(imp->name, service) == 0)
				return true;
		FOR_EACH_END
		return false;
	}
	// ... otherwise we need to scan them all
	else
	{
		FOR_EACH_IN_EARRAY(g_implementations_enroll, gslSocialImplementation, imp)
			if(stricmp(imp->name, service) == 0)
				return true;
		FOR_EACH_END
		return false;
	}
}

void gslSocialServicesRegistered(SocialServices *services, ActivityType type)
{
	// If only checking a specfic type
	if(type < kActivityType_Count)
	{
		FOR_EACH_IN_EARRAY(g_implementations[type], gslSocialImplementation, imp)
			eaPush(&services->ppServices, imp->name);
		FOR_EACH_END
	}
	// ... otherwise we need to scan them all
	else
	{
		FOR_EACH_IN_EARRAY(g_implementations_enroll, gslSocialImplementation, imp)
			eaPush(&services->ppServices, imp->name);
		FOR_EACH_END
	}
}