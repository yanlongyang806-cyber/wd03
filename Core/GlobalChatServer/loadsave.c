#include "objContainer.h"
#include "objTransactions.h"
#include "chatdb.h"
#include "channels.h"
#include "error.h"
#include "file.h"
#include "AutoGen\chatdb_h_ast.h"
#include "ChatServer/chatShared.h"

#include "AutoGen/GlobalChatServer_autotransactions_autogen_wrappers.h"

ChatDb	chat_db;

bool gbInitializeEmailExpire = false;
AUTO_CMD_INT(gbInitializeEmailExpire, InitEmailExpire);

bool gbConvertFriendRequests = false;
AUTO_CMD_INT(gbConvertFriendRequests, ConvertFriends);

extern enumTransactionOutcome trInitializeEmailExpirations(ATR_ARGS, NOCONST(ChatUser) *user); // from users.c
AUTO_TRANSACTION ATR_LOCKS(user, ".friendReqs_out[AO], .friendReqs_in[AO], .befriend_reqs, .befrienders");
enumTransactionOutcome trUserConvertFriends(ATR_ARGS, NOCONST(ChatUser) *user)
{
	int i, size;
	size = eaSize(&user->befriend_reqs);
	for (i=0; i<size; i++)
	{
		NOCONST(ChatFriendRequestStruct) *request = StructCreateNoConst(parse_ChatFriendRequestStruct);
		request->userID = user->befriend_reqs[i]->targetID;
		request->uTimeSent = user->befriend_reqs[i]->uTimeSent;
		eaIndexedAdd(&user->friendReqs_out, request);
	}
	size = eaSize(&user->befrienders);
	for (i=0; i<size; i++)
	{
		NOCONST(ChatFriendRequestStruct) *request = StructCreateNoConst(parse_ChatFriendRequestStruct);
		request->userID = user->befrienders[i]->userID;
		request->uTimeSent = user->befrienders[i]->uTimeSent;
		request->eDirection = CHATFRIEND_INCOMING;
		eaIndexedAdd(&user->friendReqs_in, request);
	}
	eaDestroyStructNoConst(&user->befriend_reqs, parse_ChatFriendRequest);
	eaDestroyStructNoConst(&user->befrienders, parse_ChatFriendRequest);
	return TRANSACTION_OUTCOME_SUCCESS;
}

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
		else if (chan->uMemberCount == 0)
		{
			objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATCHANNEL, chan->uKey, 
				"changeChannelMemberCount", "set uMemberCount = %d", getChannelMemberCount(chan));
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
			Errorf("\nChat user DB in inconsistent state: multiple users with account name %s. Deleting users with name.\n", user->accountName);

			stashRemovePointer(chat_db.account_names, user->accountName, &existing);
			//stashRemovePointer(chat_db.user_names, user->handle, &existing);
			if (existing)
				eaPush(&toRemove, existing);
			eaPush(&toRemove, user);
			bRemoved = true;
			// TODO temporary solution: delete both; need to backup the deleted data somewhere
		}
		else if (!user->accountName[0])
		{
			Errorf("\nChat user DB in inconsistent state: user with no account name - ID #%d.\n", user->id);
			//stashRemovePointer(chat_db.user_names, user->handle, NULL);
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

			/*Errorf("Chat user DB in inconsistent state: multiple users with handle %s. Deleting users with handle.", user->handle);

			stashRemovePointer(chat_db.user_names, user->handle, &existing);
			if (existing)
			eaPush(&toRemove, existing);
			eaPush(&toRemove, user);*/
			// TODO temporary solution: delete both; need to backup the deleted data somewhere
		}
		if (!bRemoved)
		{
			if (gbInitializeEmailExpire)
				AutoTrans_trInitializeEmailExpirations(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id);
			if (gbConvertFriendRequests)
				AutoTrans_trUserConvertFriends(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id);
		}

		user = objGetNextObjectFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	for (i=eaSize(&toRemove)-1; i>=0; i--)
	{
		objRequestContainerDestroyLocal(NULL, GLOBALTYPE_CHATUSER, toRemove[i]->id);
	}
}

void chatDbFixup()
{
	// Clear stash tables that may get entries from replaying logs on initialization
	stashTableClear(chat_db.account_names);
	stashTableClear(chat_db.user_names);
	stashTableClear(chat_db.channel_names);
}

#define DEFAULT_USER_STASH_SIZE 100000
#define DEFAULT_USER_STASH_SIZE_PRODUCTION 2000000
void chatDbPreInit()
{
	if (isProductionMode())
	{
		chat_db.account_names = stashTableCreateWithStringKeys(DEFAULT_USER_STASH_SIZE_PRODUCTION, StashDefault);
		chat_db.user_names = stashTableCreateWithStringKeys(DEFAULT_USER_STASH_SIZE_PRODUCTION, StashDefault);
	}
	else
	{
		chat_db.account_names = stashTableCreateWithStringKeys(DEFAULT_USER_STASH_SIZE, StashDefault);
		chat_db.user_names = stashTableCreateWithStringKeys(DEFAULT_USER_STASH_SIZE, StashDefault);
	}
	chat_db.channel_names = stashTableCreateWithStringKeys(5000, StashDefault);
}

void chatDbInit()
{
	chat_db.online_count = 0;
	chatDbFixup();

	loadstart_printf("Initializing user name stash...");
	initializeUserNameStash();
	loadend_printf(" done.");
	loadstart_printf("Initializing channel name stash...");
	initializeChannelNameStash();
	loadend_printf(" done.");
}
