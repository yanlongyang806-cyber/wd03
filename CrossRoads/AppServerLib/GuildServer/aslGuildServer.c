/**************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppServerLib.h"
#include "ServerLib.h"

#include "objContainer.h"
#include "AutoTransDefs.h"
#include "AutoStartupSupport.h"

#include "StringCache.h"
#include "FolderCache.h"
#include "inventoryCommon.h"
#include "itemCommon.h"

#include "aslGuildServer.h"
#include "chatCommonStructs.h"
#include "chatCommonStructs_h_ast.h"
#include "Entity.h"
#include "Guild.h"
#include "PowerTree.h"
#include "ResourceManager.h"
#include "StructDefines.h"
#include "TimedCallback.h"
#include "utilitiesLib.h"
#include "WorldLib.h"

#include "autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/appserverlib_autotransactions_autogen_wrappers.h"
#include "AutoGen/Guild_h_ast.h"
#include "AutoGen/aslGuildServer_h_ast.h"

GuildRankList g_GuildRanks;
GuildEmblemList g_GuildEmblems;
GuildConfig gGuildConfig;

TimingHistory *gCreationHistory;
TimingHistory *gActionHistory;

// Set to true when guilds have been transferred.
static bool bGuildsAcquired = false;

static S32 *eaiNewMembers = NULL;

///////////////////////////////////////////////////////////////////////////////////////////
// Utility functions
///////////////////////////////////////////////////////////////////////////////////////////

Guild *aslGuild_GetGuild(ContainerID iGuildID)
{
	Container *pContainer = objGetContainer(GLOBALTYPE_GUILD, iGuildID);
	if (pContainer) {
		return (Guild *)pContainer->containerData;
	} else {
		return NULL;
	}
}

Entity *aslGuild_GetGuildBank(ContainerID iGuildID)
{
	Container *pContainer = objGetContainer(GLOBALTYPE_ENTITYGUILDBANK, iGuildID);
	if (pContainer) {
		return (Entity *)pContainer->containerData;
	} else {
		return NULL;
	}
}

Guild *aslGuild_GetGuildByName(const char *pcName, ContainerID iVirtualShardID)
{
	ContainerIterator iter;
	Guild *pGuild;
	objInitContainerIteratorFromType(GLOBALTYPE_GUILD, &iter);
	while (pGuild = objGetNextObjectFromIterator(&iter)) {
		if (stricmp(pGuild->pcName, pcName) == 0 && pGuild->iVirtualShardID == iVirtualShardID) 
		{
			objClearContainerIterator(&iter);
			return pGuild;
		}
	}
	objClearContainerIterator(&iter);
	return NULL;
}

AUTO_COMMAND_REMOTE;
ContainerID aslGuild_GetGuildIdForName(const char *pcName, ContainerID iVirtualShardID)
{
	ContainerID idRet = 0;

	Guild *pGuild = aslGuild_GetGuildByName(pcName, iVirtualShardID);
	if(pGuild)
	{
		idRet = pGuild->iContainerID;
	}

	return idRet;
}

// Get the permission flags for a guild member.
static U32 aslGuild_ChatGuildMemberArrayFlags(const GuildCustomRank *const *ppRanks, S32 iRank)
{
	U32 flags = 0;
	flags |= guild_RankHasPermissions(iRank, ppRanks, GuildPermission_Chat) ? CHATSERVER_CHATGUILDMEMBERARRAY_CHAT : 0;
	flags |= guild_RankHasPermissions(iRank, ppRanks, GuildPermission_OfficerChat) ? CHATSERVER_CHATGUILDMEMBERARRAY_OFFICER : 0;
	return flags;
}

// Pack list of all guild membership.
static void aslGuild_DumpAllGuildMembers(int **eaMemberData)
{
	ContainerIterator iter;
	const Guild *pGuild;

	PERFINFO_AUTO_START_FUNC();

	eaiClear(eaMemberData);

	// Loop over each guild collecting member data.
	eaiPush(eaMemberData, CHATSERVER_CHATGUILDMEMBERARRAY_MAGIC);
	objInitContainerIteratorFromType(GLOBALTYPE_GUILD, &iter);
	while (pGuild = objGetNextObjectFromIterator(&iter)) {
		int size = eaSize(&pGuild->eaMembers);
		if (size) {
			int i;
			eaiPush(eaMemberData, pGuild->iContainerID);
			eaiPush(eaMemberData, size);
			for (i = 0; i != size; ++i)
			{
				const GuildMember *member = pGuild->eaMembers[i];
				eaiPush(eaMemberData, member->iAccountID);
				eaiPush(eaMemberData, aslGuild_ChatGuildMemberArrayFlags(pGuild->eaRanks, member->iRank));
			}
		}
	}
	objClearContainerIterator(&iter);

	PERFINFO_AUTO_STOP_FUNC();
}

//Updates the chat server list of guilds
AUTO_COMMAND_REMOTE;
void aslGuild_ChatUpdate(void)
{
	ContainerIterator iter;
	Guild *pGuild;
	ChatGuildList list = {0};
	ChatGuildMemberArray memberArray;
	int guildCount, memberCount = 0;

	PERFINFO_AUTO_START_FUNC();
	
	// Send list of guild names.
	loadstart_printf("Sending guild list...");
	objInitContainerIteratorFromType(GLOBALTYPE_GUILD, &iter);
	while (pGuild = objGetNextObjectFromIterator(&iter))
	{
		ChatGuild *pStruct = StructCreate(parse_ChatGuild);
		memberCount += eaSize(&pGuild->eaMembers);
		pStruct->iGuildID = pGuild->iContainerID;
		pStruct->pchName = pGuild->pcName;
		eaPush(&list.ppGuildList, pStruct);
	}
	objClearContainerIterator(&iter);
	RemoteCommand_ChatServerUpdateGuildBatch(NULL, GLOBALTYPE_CHATSERVER, 0, &list);
	guildCount = eaSize(&list.ppGuildList);
	StructDeInit(parse_ChatGuildList, &list);
	loadend_printf(" done (%d guilds)", guildCount);

	// Send list of guild members.
	loadstart_printf("Sending guild member list...");
	StructInit(parse_ChatGuildMemberArray, &memberArray);
	aslGuild_DumpAllGuildMembers(&memberArray.pMemberData);
	RemoteCommand_ChatServerAllGuildMembers(NULL, GLOBALTYPE_CHATSERVER, 0, &memberArray);
	StructDeInit(parse_ChatGuildMemberArray, &memberArray);
	loadend_printf(" done (%d members)", memberCount);

	PERFINFO_AUTO_STOP_FUNC();
}

void aslGuild_PushMemberAddToChatServer (U32 uGuildID, const char *pchGuildName, U32 uAccountId,
										 S32 iRank, const GuildCustomRank *const *ppRanks)
{
	ChatGuild guild = {0};
	guild.iGuildID = uGuildID;
	guild.pchName = StructAllocString(pchGuildName);
	RemoteCommand_ChatServerAddGuildMemberById(NULL, GLOBALTYPE_CHATSERVER, 0, &guild, uAccountId,
		aslGuild_ChatGuildMemberArrayFlags(ppRanks, iRank));
	StructDeInit(parse_ChatGuild, &guild);	
}

void aslGuild_PushMemberRemoveToChatServer (U32 uGuildID, const char *pchGuildName, U32 uAccountId)
{
	ChatGuild guild = {0};
	guild.iGuildID = uGuildID;
	guild.pchName = StructAllocString(pchGuildName);
	RemoteCommand_ChatServerRemoveGuildMemberById(NULL, GLOBALTYPE_CHATSERVER, 0, &guild, uAccountId);
	StructDeInit(parse_ChatGuild, &guild);	
}

void aslGuild_NameChange_CB(Container *con, ObjectPathOperation **operations)
{
	Guild *pGuild = con->containerData;
	RemoteCommand_ChatServerUpdateGuild(NULL, GLOBALTYPE_CHATSERVER, 0, pGuild->iContainerID, pGuild->pcName);
}

void aslGuild_RemoveCB(Container *con, Guild *guild)
{
	RemoteCommand_ChatServerRemoveGuild(GLOBALTYPE_CHATSERVER, 0, guild->iContainerID);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Normal operation
///////////////////////////////////////////////////////////////////////////////////////////

// Sort a pair of guild members by their leadership quality, used to QSort the member list
int aslGuild_LeadershipSortComparator(const GuildMember **pMember1, const GuildMember **pMember2)
{
	// Check who has a higher rank
	if ((*pMember1)->iRank > (*pMember2)->iRank) {
		return -1;
	} else if ((*pMember1)->iRank < (*pMember2)->iRank) {
		return 1;
	}
	
	// Check who has been with the guild longer
	if ((*pMember1)->iJoinTime < (*pMember2)->iJoinTime) {
		return -1;
	} else if ((*pMember1)->iJoinTime > (*pMember2)->iJoinTime) {
		return 1;
	}
	
	// In the incredibly unlikely circumstance that they both have the same rank and joined
	// in the same second, it doesn't really matter who gets promoted, so we return a tie.
	return 0;
}

AUTO_COMMAND_REMOTE;
void aslGuild_AddToAutoGuild(U32 iEntID)
{
	eaiPush(&eaiNewMembers, iEntID);
}

void aslGuild_ValidateGuilds(void)
{
	ContainerIterator iter;
	Guild *pGuild;
	U32 iCurrentTime = timeSecondsSince2000();

	printf("Creations in last 30.0 seconds: %d\n", timingHistoryInLastInterval(gCreationHistory, 30.0f));
	printf("Actions in last 30.0 seconds: %d\n", timingHistoryInLastInterval(gActionHistory, 30.0f));
	loadstart_printf("Beginning aslValidateGuilds... ");
	objInitContainerIteratorFromType(GLOBALTYPE_GUILD, &iter);
	while (pGuild = objGetNextObjectFromIterator(&iter)) {
		int i;
		int iMembership = eaSize(&pGuild->eaMembers);
		int iMaxRank;
		int iLeaderID = 0;
		GuildMember **eaCandidates = NULL;
		Entity *pGuildBank = NULL;
		Container *pGuildBankContainer = NULL;
		
		if (pGuild->iVersion < 1 && timeSecondsSince2000() - pGuild->iCreatedOn < GUILD_CLEANUP_INTERVAL*2) {
			continue;
		}
		
		// Check if the guild is empty, and if so, delete it
		if (iMembership == 0 && !pGuild->bIsOwnedBySystem) {
			RemoteCommand_aslGuild_Destroy(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, 0);
		}
				
		if (eaSize(&pGuild->eaRanks) < eaSize(&g_GuildRanks.eaRanks)) {
			RemoteCommand_aslGuild_UpdateRanks(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, 0);
		}
		iMaxRank = eaSize(&pGuild->eaRanks)-1;
		
		// Make sure the guild leader rank has all permissions
		for (i = 1; GuildRankPermissionsEnum[i].key != U32_TO_PTR(DM_END); i++) {
			if (!guild_HasPermission(iMaxRank, pGuild, GuildRankPermissionsEnum[i].value)) {
				RemoteCommand_aslGuild_SetPermission(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, 0, iMaxRank, GuildRankPermissionsEnum[i].value, true, NULL, NULL, NULL);
			}
		}
		
		// make sure guild permissions have all ranks in the guild bank info
		pGuildBankContainer = objGetContainer(GLOBALTYPE_ENTITYGUILDBANK, pGuild->iContainerID);
		if(pGuildBankContainer)
			pGuildBank = pGuildBankContainer->containerData;

		if(pGuildBank)
		{
			if(pGuildBank->pInventoryV2)
			{
				for(i = 0; i < eaSize(&pGuildBank->pInventoryV2->ppInventoryBags); ++i)
				{
					if ( pGuildBank->pInventoryV2->ppInventoryBags[i]->pGuildBankInfo )
					{
						if(eaSize(&pGuildBank->pInventoryV2->ppInventoryBags[i]->pGuildBankInfo->eaPermissions) != iMaxRank + 1)
						{
							AutoTrans_aslGuild_tr_FixGuildBankInfo(NULL, GetAppGlobalType(), GLOBALTYPE_GUILD, pGuild->iContainerID, GLOBALTYPE_ENTITYGUILDBANK, pGuild->iContainerID);
							// stop as all bags will be fixed by this transaction
							break;
						}
					}
				}
			}
		}

		// First, remove expired events and reschedule recurring events that need rescheduling
		for (i = eaSize(&pGuild->eaEvents)-1; i >= 0; i--)
		{
			GuildEvent *pGuildEvent = pGuild->eaEvents[i];

			// If the event has an invalid duration, remove it
			if (pGuildEvent->iDuration > DAYS(1) || pGuildEvent->iDuration < 0)
			{
				RemoteCommand_aslGuild_RemoveGuildEvent(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, pGuildEvent->uiID, NULL, NULL, NULL);
			}
			// If the event is over and past its removal time, either remove the event or recur it
			else if (pGuildEvent->iStartTimeTime + pGuildEvent->iDuration + MIN_GUILD_EVENT_TIME_PAST_REMOVE < iCurrentTime)
			{
				// We do a strict 0 check here, since anything below 0 means it recurs infinitely
				if (pGuildEvent->iRecurrenceCount == 0 || pGuildEvent->eRecurType == 0 || pGuildEvent->bCanceled)
				{
					RemoteCommand_aslGuild_RemoveGuildEvent(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, pGuildEvent->uiID, NULL, NULL, NULL);
				}
				else
				{
					RemoteCommand_aslGuild_RecurGuildEvent(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, pGuildEvent->uiID, NULL, NULL, NULL);
				}
			}
		}
	}
	objClearContainerIterator(&iter);

	loadend_printf("...done.");
}

#define MAX_AUTO_GUILD_NAME_TRIES 26

static bool aslGuild_CreateAutoName(char **esName, S32 iGuildCount)
{
	Guild *pGuild;
	S32 i;

	// try base name
	estrPrintf(esName, "%s: %s %d", GetShardNameFromShardInfoString(), gGuildConfig.pcAutoName, iGuildCount);
	pGuild = aslGuild_GetGuildByName(*esName, 0);
	if(!pGuild)
	{
		return true;
	}

	// try alternate names
	for(i = 0; i < MAX_AUTO_GUILD_NAME_TRIES; ++i)
	{
		estrPrintf(esName, "%s: %s %d%c", GetShardNameFromShardInfoString(), gGuildConfig.pcAutoName, iGuildCount, 'A'+i);
		pGuild = aslGuild_GetGuildByName(*esName, 0);
		if(!pGuild)
		{
			return true;
		}
	}

	return false;
}

// check and set data as needed
static void aslGuild_SetAutoData(Guild *pGuild)
{
	if(gGuildConfig.pcDescription && gGuildConfig.pcDescription[0] && 
		(!pGuild->pcDescription || stricmp(pGuild->pcDescription, gGuildConfig.pcDescription) != 0))
	{
		// set description
		RemoteCommand_aslGuild_SetDescription(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, 0, gGuildConfig.pcDescription, true, NULL, NULL, NULL);
	}

	if(gGuildConfig.pcMessage && gGuildConfig.pcMessage[0] && 
		(!pGuild->pcMotD || stricmp(pGuild->pcMotD, gGuildConfig.pcMessage) != 0))
	{
		// set message of the day
		RemoteCommand_aslGuild_SetMotD(GLOBALTYPE_GUILDSERVER, 0, pGuild->iContainerID, 0, gGuildConfig.pcMessage, true, NULL, NULL, NULL);
	}

	if
	(
		gGuildConfig.pcEmblem && gGuildConfig.pcEmblem[0] && 
		(!pGuild->pcEmblem || stricmp(pGuild->pcEmblem, gGuildConfig.pcEmblem) != 0)
	)
	{
		RemoteCommand_aslGuild_SetEmblem(GetAppGlobalType(), 0, pGuild->iContainerID, 0, gGuildConfig.pcEmblem, true, NULL, NULL, NULL);
	}

	if(gGuildConfig.iColor1 != pGuild->iColor1)
	{
		RemoteCommand_aslGuild_SetColors(GetAppGlobalType(), 0, pGuild->iContainerID, 0, gGuildConfig.iColor1, true, true, NULL, NULL, NULL);
	}

	if(gGuildConfig.iColor2 != pGuild->iColor2)
	{
		RemoteCommand_aslGuild_SetColors(GetAppGlobalType(), 0, pGuild->iContainerID, 0, gGuildConfig.iColor2, false, true, NULL, NULL, NULL);
	}

}

// handle all 
void aslGuild_AutoJoinOncePerFrame(void)
{
	static ContainerID uBestAutoGuild = 0;
	static U32 uLastTime = 0;
	Guild *pGuild = NULL;
	U32 tm = timeSecondsSince2000();
	static S32 iLastCreateID = -1;		// last create id
	static U32 uCreateTm = 0;			// time of creation
	static S32 iSetData = -1;			// last container data was checked for
	static bool bFinishedData = false;	// a pass has been made to update all system controlled guilds

	// If there is an auto name then the auto guild functions will be enabled
	if(gGuildConfig.pcAutoName && gGuildConfig.pcAutoName[0])
	{
		if(tm >= uLastTime)
		{
			uLastTime = tm + 4;	// every 4 seconds
			if(uBestAutoGuild)
			{
				pGuild = aslGuild_GetGuild(uBestAutoGuild);
			}

			if(pGuild)
			{
				S32 iMembership = eaSize(&pGuild->eaMembers);

				if(eaiSize(&eaiNewMembers) > 0)
				{
					S32 iNumToInvite = eaiSize(&eaiNewMembers);
					if(iNumToInvite > GUILD_MAX_SIZE - iMembership)
					{
						iNumToInvite = GUILD_MAX_SIZE - iMembership;
					}

					if(iNumToInvite > 0)
					{
						// put as many characters into the guild as possible
						S32 i;
						for(i = 0; i < iNumToInvite; ++i)
						{
							RemoteCommand_aslGuild_JoinWithoutInvite(GLOBALTYPE_GUILDSERVER, 0, uBestAutoGuild, eaiNewMembers[i], "");
						}

						// remove members that were attempted to be added
						eaiRemoveRange(&eaiNewMembers, 0, iNumToInvite);
					}
					else
					{
						// guild is full
						uBestAutoGuild = 0;
					}
				}
			}
			else
			{
				// can't find guild, won't happen unless delete which is normally only during debug
				uBestAutoGuild = 0;
			}
			
			if(!bFinishedData || uBestAutoGuild < 1)
			{
				// find best guild
				S32 iGuildSizeBest = GUILD_MAX_SIZE - 10;	// only count if there is a decent amount of space left. otherwise create a new one
				ContainerIterator iter;
				S32 iGuildCount = 0;
				S32 iDone = 0;

				uBestAutoGuild = 0;

				objInitContainerIteratorFromType(GLOBALTYPE_GUILD, &iter);
				while (pGuild = objGetNextObjectFromIterator(&iter))
				{
					S32 iMembership = eaSize(&pGuild->eaMembers);

					// Do list best autoguild if config is set for auto guilds
					if(pGuild->bIsOwnedBySystem)
					{
						++iGuildCount;

						// set data for this guild, only do 20 per pass
						if(iSetData < iGuildCount && iDone < 20)
						{
							++iDone;
							iSetData = iGuildCount;
							aslGuild_SetAutoData(pGuild);
						}

						if(iMembership < iGuildSizeBest)
						{
							iGuildSizeBest = iMembership;
							uBestAutoGuild = pGuild->iContainerID;
						}
					}

					if(iSetData >= iGuildCount)
					{
						bFinishedData = true;
					}
				}
				objClearContainerIterator(&iter);

				// does a new guild need to be created
				if(uBestAutoGuild == 0 && ( iGuildCount != iLastCreateID || tm >= uCreateTm))
				{
					// create guild
					char *esName = NULL;
					if(aslGuild_CreateAutoName(&esName, iGuildCount))
					{
						uCreateTm = tm + (SECONDS_PER_MINUTE / 2);	// 1/2 minute for creation of same id
						iLastCreateID = iGuildCount;
						RemoteCommand_aslGuild_Create(GLOBALTYPE_GUILDSERVER, 0, 0, 0, esName, "", 0, 0, "", gGuildConfig.pcDescription ? gGuildConfig.pcDescription : "", "", 0, "", true, NULL, NULL, NULL);
					}
					else
					{
						// failure, issue error
						Errorf("Guild server can not create auto guild, count is %d", iGuildCount);
						// try again in an 1/2 hour
						uLastTime = timeSecondsSince2000() + SECONDS_PER_HOUR / 2;
					}
					estrDestroy(&esName);
				}
			}
		}
	}
}

static TimedCallback *guildBankMigrationCallback;
static U32 *pGuildsToMigrate;
static U32 uGuildsMigrating;
static U32 uTotalGuildsMigrated;
static U32 uGuildMigrationStartTime;

// All guild containers have been acquired.
void aslGuild_AcquiredContainersCB(void)
{
	bGuildsAcquired = true;
}

typedef struct CreateGuildBankCBStruct
{
	ContainerID guildID;
} CreateGuildBankCBStruct;

static void MigrateGuildBank_CB(TransactionReturnVal *returnVal, CreateGuildBankCBStruct *cbData)
{
	uGuildsMigrating--;
	uTotalGuildsMigrated++;

	if(returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		AssertOrAlert("GUILDSERVER.FAILEDTOCREATEGUILDBANK", "Failed to migrate guild bank for guild %u. (%s)", cbData->guildID, 
						returnVal->pBaseReturnVals && returnVal->pBaseReturnVals->returnString ? returnVal->pBaseReturnVals->returnString : "");
	}
}

static void CreateGuildBankForMigrate_CB(TransactionReturnVal *returnVal, CreateGuildBankCBStruct *cbData)
{
	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		// Autotransaction to copy guild inventory to gbank
		AutoTrans_aslGuild_tr_MoveGuildInventoryToGuildBankContainer(objCreateManagedReturnVal(MigrateGuildBank_CB, cbData), GetAppGlobalType(), GLOBALTYPE_GUILD, cbData->guildID, GLOBALTYPE_ENTITYGUILDBANK, cbData->guildID);
	}
	else
	{
		uTotalGuildsMigrated++;
		uGuildsMigrating--;
		AssertOrAlert("GUILDSERVER.FAILEDTOCREATEGUILDBANK", "Failed to create guild bank for guild %u. ", cbData->guildID);
	}
}

//return true if finished
#define ASLGUILD_NUM_CONTAINERS_PER_REQUEST 10 // max number of requests per 0.1 seconds
#define ASLGUILD_NUM_OUTSTANDING_REQUESTS 100 // max number of outstanding requests
void aslMigrateGuildBanksDispatch(TimedCallback *pCallback, F32 timeSinceLastCallback, void *pData)
{
	static int printfThrottle = 0;
	int count = 0;

	while(++count < ASLGUILD_NUM_CONTAINERS_PER_REQUEST && uGuildsMigrating < ASLGUILD_NUM_OUTSTANDING_REQUESTS && eaiSize(&pGuildsToMigrate) > 0) 
	{
		CreateGuildBankCBStruct *data = malloc(sizeof(CreateGuildBankCBStruct));
		U32 containerID = eaiPop(&pGuildsToMigrate);
		++uGuildsMigrating;
		data->guildID = containerID;
		objRequestContainerVerifyAndSet(objCreateManagedReturnVal(CreateGuildBankForMigrate_CB, data), GLOBALTYPE_ENTITYGUILDBANK, containerID, NULL, GetAppGlobalType(), GetAppGlobalID());
	}

	if(++printfThrottle >= 600)
	{
		printf("Migrating Banks: %u waiting, %u queued transactions, %u completed\n", eaiSize(&pGuildsToMigrate), uGuildsMigrating, uTotalGuildsMigrated);
		printfThrottle = 0;
	}

	if(bGuildsAcquired && eaiSize(&pGuildsToMigrate) == 0 && uGuildsMigrating == 0)
	{
		TimedCallback_Remove(pCallback);
		guildBankMigrationCallback = NULL;
		printf("Done migrating banks (%u). (%u seconds)\n", uTotalGuildsMigrated, timeSecondsSince2000() - uGuildMigrationStartTime);
	}
}

void aslGuild_AcquiredSingleContainerCB(ContainerID containerID)
{
	Guild *guild = aslGuild_GetGuild(containerID);
	char *diffstr = NULL;
	assert(guild);

	if(guild->pInventoryDeprecated)
	{
		if(!guildBankMigrationCallback)
		{
			guildBankMigrationCallback = TimedCallback_Add(aslMigrateGuildBanksDispatch, NULL, 0.1f);
			printf("Queueing banks to migrate.\n");
			uGuildMigrationStartTime = timeSecondsSince2000();
		}

		eaiPush(&pGuildsToMigrate, containerID);
	}
	else if (guild->iFixupVersion < CURRENT_GUILD_FIXUP_VERSION)
	{
		AutoTrans_aslGuild_tr_FixupGuild(NULL, GetAppGlobalType(), GLOBALTYPE_GUILD, guild->iContainerID, GLOBALTYPE_ENTITYGUILDBANK, guild->iContainerID);
	}

	if(guild_LazyCreateBank())
	{
		objRequestContainerMove(NULL, GLOBALTYPE_ENTITYGUILDBANK, containerID, GLOBALTYPE_OBJECTDB, 0, GetAppGlobalType(), GetAppGlobalID());
	}
	else
	{
		objRequestContainerVerifyAndMove(NULL, GLOBALTYPE_ENTITYGUILDBANK, containerID, GetAppGlobalType(), GetAppGlobalID());
	}

}

int aslGuild_OncePerFrame(F32 fElapsed)
{
	static U32 iLastDeleteTime = 0;
	static bool bOnce = false;
	static bool bSentChatUpdate = false;
	
	if(!bOnce) {
		aslAcquireContainerOwnershipEx(GLOBALTYPE_GUILD, aslGuild_AcquiredContainersCB, aslGuild_AcquiredSingleContainerCB);
		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
		ATR_DoLateInitialization();

		gCreationHistory = timingHistoryCreate(10000);
		gActionHistory = timingHistoryCreate(10000);
		bOnce = true;
	}

	if (bGuildsAcquired)
	{
		if (!bSentChatUpdate)
		{
			aslGuild_ChatUpdate();
			bSentChatUpdate = true;
		}

		if (timeSecondsSince2000() - iLastDeleteTime >= GUILD_CLEANUP_INTERVAL)
		{
			aslGuild_ValidateGuilds();
			iLastDeleteTime = timeSecondsSince2000();
		}

		aslGuild_AutoJoinOncePerFrame();
	}
	
	return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Server initialization
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_STARTUP(AS_GuildSchemas) ASTRT_DEPS(AS_AttribSets);
void aslGuild_TransactionInit(void)
{
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_GUILD, aslGuild_NameChange_CB, ".pcName", true, false, false, NULL);
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_GUILD, aslGuild_RemoveCB);
	objLoadAllGenericSchemas();
}

// RewardsCommon is required to trigger the gated reward enum loading which is used in pPlayer on the entity. We shouldn't be locking the entire .Player struct anyway
AUTO_STARTUP(GuildServer) ASTRT_DEPS(AS_GuildSchemas, AS_ActivityLogConfig, AS_TextFilter, AS_GuildRecruitParam, AS_GuildStats, AS_GuildThemes, AS_GuildBankConfig, RewardGatedLoad );
void aslGuild_ServerStartup(void)
{
}

int aslGuild_Init(void)
{
	static StashTable s_ = NULL;
	
	AutoStartup_SetTaskIsOn("GuildServer", 1);
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");
	
	resFinishLoading();

	stringCacheFinalizeShared();
	assertmsg(GetAppGlobalType() == GLOBALTYPE_GUILDSERVER, "Guild server type not set");
	
	loadstart_printf("Connecting GuildServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
	while (!InitObjectTransactionManager(
			GetAppGlobalType(),
			gServerLibState.containerID,
			gServerLibState.transactionServerHost,
			gServerLibState.transactionServerPort,
			gServerLibState.bUseMultiplexerForTransactions, NULL)) {
		Sleep(1000);
	}
	if (!objLocalManager()) {
		loadend_printf("Failed.");
		return 0;
	}
	loadend_printf("Connected.");
	
	gAppServer->oncePerFrame = aslGuild_OncePerFrame;
	
	ParserLoadFiles(NULL, "defs/config/GuildEmblems.def", "GuildEmblems.bin", 0, parse_GuildEmblemList, &g_GuildEmblems);
	ParserLoadFiles(NULL, "defs/config/GuildRanks.def", "GuildRanks.bin", 0, parse_GuildRankList, &g_GuildRanks);
	ParserLoadFiles(NULL, "defs/config/GuildConfig.def", "GuildConfig.bin", 0, parse_GuildConfig, &gGuildConfig);
	guild_FixupRanks(&g_GuildRanks);

	if(gGuildConfig.pcEmblem && gGuildConfig.pcEmblem[0])
	{
		S32 i;
		bool ok = false;
		for (i = 0; i < eaSize(&g_GuildEmblems.eaEmblems); ++i)
		{
			const char* pchTextureName = REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[i]->hTexture);
			if(pchTextureName && stricmp(pchTextureName, gGuildConfig.pcEmblem) == 0)
			{
				ok = true;
				break;
			}
		}

		if(!ok)
		{
			ErrorFilenamef("defs/config/GuildConfig.def", "Guild emblem %s is not in emblem list (GuildEmblems.def)", gGuildConfig.pcEmblem);
		}
	}

	// Temporary hack to make sure the basic inventory data loads here, even though the AUTO_STARTUP
	// stuff doesn't normally run on the app servers.
	itemtags_Load();
	invIDs_Load();
	inv_Load();
	
	return 1;
}

AUTO_RUN;
int aslGuild_RegisterServer(void)
{
	aslRegisterApp(GLOBALTYPE_GUILDSERVER, aslGuild_Init, APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);

	return 1;
}

#include "aslGuildServer_h_ast.c"
