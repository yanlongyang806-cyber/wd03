#include "objContainer.h"
#include "objTransactions.h"
#include "channels.h"
#include "error.h"
#include "chatdb.h"
#include "chatLocal.h"
#include "chatdb_h_ast.h"

#include "AutoGen/ChatServer_autotransactions_autogen_wrappers.h"

ChatDb	chat_db;

extern ParseTable parse_ChatGuild[];
#define TYPE_parse_ChatGuild ChatGuild
ChatGuild **g_eaChatGuilds = NULL;

void initializeChannelNameStash(void)
{
	ContainerIterator iter = {0};
	ChatChannel *chan;
	ChatChannel **toKill = NULL;
	int i;

	objInitContainerIteratorFromType(GLOBALTYPE_CHATCHANNEL, &iter);
	chan = objGetNextObjectFromIterator(&iter);
	while (chan)
	{
		if ( ! stashAddPointer(chat_db.channel_names, chan->name, chan, false))
		{
			eaPush(&toKill, chan);
		}
		chan = objGetNextObjectFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	// Destroy channels there are somehow duplicates of
	for (i=eaSize(&toKill) - 1; i>=0; i--)
	{
		channelForceKill(toKill[i]);
	}
}

void initializeUserNameStash(void)
{
	ContainerIterator iter = {0};
	ChatUser *user;
	ChatUser **toRemove = NULL;
	int i;

	objInitContainerIteratorFromType(GLOBALTYPE_CHATUSER, &iter);
	user = objGetNextObjectFromIterator(&iter);
	while (user)
	{
		bool bRemoved = false;
		if (user->accountName[0] && !stashAddPointer(chat_db.account_names, user->accountName, user, false))
		{
			ChatUser *existing = NULL;
			stashRemovePointer(chat_db.account_names, user->accountName, &existing);
			if (existing)
				eaPush(&toRemove, existing);
			eaPush(&toRemove, user);
			bRemoved = true;
		}
		else if (!user->accountName[0])
		{
			eaPush(&toRemove, user);
			bRemoved = true;
		}

		if (!bRemoved && user->handle[0])
		{
			ChatUser *existingUser = NULL;
			ChatUser *existing = NULL;
			if (stashFindPointer(chat_db.user_names, user->handle, &existingUser))
			{
				if (user->uHandleUpdateTime > existingUser->uHandleUpdateTime)
					stashAddPointer(chat_db.user_names, user->handle, user, true);
			}
			else
				stashAddPointer(chat_db.user_names, user->handle, user, true);

			if(!stashIntFindPointer(chat_db.gad_by_id, user->id, NULL))
			{
				GADRef *r = StructCreate(parse_GADRef);
				char buf[50];

				sprintf(buf, "%d", user->id);
				SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), buf, r->hGAD);
				stashIntAddPointer(chat_db.gad_by_id, user->id, r, 0);
			}
		}
		if (!bRemoved)
		{
		}

		user = objGetNextObjectFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	for (i=eaSize(&toRemove)-1; i>=0; i--)
	{
		objRemoveContainerFromRepository(GLOBALTYPE_CHATUSER, toRemove[i]->id);
	}
}

void chatDbFixup()
{
	// Clear stash tables that may get entries from replaying logs on initialization
	stashTableClear(chat_db.account_names);
	stashTableClear(chat_db.user_names);
	stashTableClear(chat_db.channel_names);
}

void chatDbPreInit()
{
	chat_db.account_names = stashTableCreateWithStringKeys(1000, StashDefault);
	chat_db.user_names = stashTableCreateWithStringKeys(1000, StashDefault);
	chat_db.channel_names = stashTableCreateWithStringKeys(1000, StashDefault);
	chat_db.gad_by_id = stashTableCreateInt(1000);
}

void chatDbInit()
{
	chat_db.online_count = 0;
	chatDbFixup();
	initializeUserNameStash();
	initializeChannelNameStash();
	{ // Create the Global Channel here
		NOCONST(ChatChannel) *channel = StructCreateNoConst(parse_ChatChannel);
		channelInitialize(channel, SHARD_GLOBAL_CHANNEL_NAME, "", CHANNEL_SPECIAL_SHARDGLOBAL);
		stashAddPointer(chat_db.channel_names, channel->name, channel, false);
	}
}

void chatGuildsInit()
{
	eaCreate(&g_eaChatGuilds);
	eaIndexedEnable(&g_eaChatGuilds, parse_ChatGuild);
}
