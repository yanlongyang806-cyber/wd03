/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslGroupProject.h"
#include "GroupProjectCommon.h"
#include "Entity.h"
#include "Player.h"
#include "Guild.h"
#include "GameAccountDataCommon.h"
#include "ResourceInfo.h"
#include "StringCache.h"
#include "gslPartition.h"
#include "inventoryCommon.h"
#include "EntityLib.h"
#include "LoggedTransactions.h"
#include "GameStringFormat.h"
#include "NotifyEnum.h"
#include "Reward.h"
#include "SavedPetCommon.h"
#include "gslEventSend.h"

#include "AutoGen/Player_h_ast.h"
#include "AutoGen/GroupProjectCommon_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void
gslGroupProject_ContributionNotify(ContributionNotifyData *notifyData)
{
    Entity *playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, notifyData->playerID);
    ClientCmd_gclGroupProject_ContributionNotify(playerEnt, notifyData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GroupProject_DebugGuildProjectInit(Entity *pEnt)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        RemoteCommand_aslGroupProject_DebugCreateAndInit(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, GLOBALTYPE_GUILD, pEnt->pPlayer->iGuildID);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GroupProject_ContributionReport(const char *projectName)
{
    GroupProjectDef *projectDef = RefSystem_ReferentFromString("GroupProjectDef", projectName);
    int i, j;

    if ( projectDef )
    {
        for ( i = eaSize(&projectDef->donationTaskDefs) - 1; i >= 0; i-- )
        {
            DonationTaskDef *taskDef = GET_REF(projectDef->donationTaskDefs[i]->taskDef);
            S32 contribution = 0;

            for ( j = eaSize(&taskDef->buckets) - 1; j >= 0; j-- )
            {
                GroupProjectConstant *constant;
                GroupProjectDonationRequirement *bucket = taskDef->buckets[j];
                int constantIndex;

                constantIndex = GroupProject_FindConstant(projectDef, bucket->contributionConstant);
                if ( constantIndex < 0 )
                {
                    printf("contribution constant %s not found\n", bucket->contributionConstant);
                    continue;
                }
                constant = eaGet(&projectDef->constants, constantIndex);
                if ( constant )
                {
                    contribution += ( bucket->count * constant->value );
                }
            }

            printf("%s, %d\n", taskDef->name, contribution);
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_GiveNumeric(Entity *pEnt, const char *projectName, const char *numericName, S32 value)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        RemoteCommand_aslGroupProject_GiveNumeric(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, numericName, value);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_GiveNumeric(Entity *pEnt, const char *projectName, const char *numericName, S32 value)
{
    if ( pEnt )
    {
        RemoteCommand_aslGroupProject_GiveNumeric(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, numericName, value);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_SetUnlock(Entity *pEnt, const char *projectName, const char *unlockName)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        RemoteCommand_aslGroupProject_SetUnlock(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, unlockName);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_SetUnlock(Entity *pEnt, const char *projectName, const char *unlockName)
{
    if ( pEnt )
    {
        RemoteCommand_aslGroupProject_SetUnlock(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, unlockName);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_ClearUnlock(Entity *pEnt, const char *projectName, const char *unlockName)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        RemoteCommand_aslGroupProject_ClearUnlock(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, unlockName);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_ClearUnlock(Entity *pEnt, const char *projectName, const char *unlockName)
{
    if ( pEnt )
    {
        RemoteCommand_aslGroupProject_ClearUnlock(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, unlockName);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_SetNextTask(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        Guild* pGuild = guild_GetGuild(pEnt);
        GuildMember *pMember = guild_FindMemberInGuild(pEnt, pGuild);
        DonationTaskDef *taskDef = RefSystem_ReferentFromString("DonationTaskDef", taskName);

        // Only set the task if the player has permission and the task allowed expression is true.
        if ( taskDef && pMember && 
             guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildProjectManagement) &&
             GuildProject_DonationTaskAllowed(pEnt, projectName, taskDef, taskSlotNum) )
        {
            RemoteCommand_aslGroupProject_SetNextTask(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, 
                GLOBALTYPE_GUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, taskSlotNum, taskName);
        }
        else
        {
            //XXX - notify here
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_SetNextTask(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName)
{
    if ( pEnt )
    {
        DonationTaskDef *taskDef = RefSystem_ReferentFromString("DonationTaskDef", taskName);

        // Only set the task if the player has permission and the task allowed expression is true.
        if ( taskDef )
        {
            RemoteCommand_aslGroupProject_SetNextTask(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), 
                GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), projectName, taskSlotNum, taskName);
        }
        else
        {
            //XXX - notify here
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_SetNextTask(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName)
{
    GuildProject_SetNextTask(pEnt, projectName, taskSlotNum, taskName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_SetNextTask(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName)
{
    PlayerProject_SetNextTask(pEnt, projectName, taskSlotNum, taskName);
}


static void
DonateSimpleItems_CB(TransactionReturnVal *returnVal, void *userData)
{

}

static void
SendDonationNoPermissionNotify(Entity *pEnt, const char *itemName, const char *projectName, const char *bucketName, S32 count, ContributionItemData **donationQueue)
{
    ContributionNotifyData notifyData = {0};
    notifyData.playerID = pEnt->myContainerID;
    notifyData.projectcontainerType = GLOBALTYPE_GROUPPROJECTCONTAINERGUILD;
    notifyData.projectContainerID = pEnt->pPlayer->pGuild->iGuildID;
    notifyData.donatedItemName = allocAddString(itemName);
    notifyData.requestedDonationCount = count;
    notifyData.donationCount = 0;
    notifyData.contributionNumericName = NULL;
    notifyData.contributionEarned = 0;
    notifyData.projectName = allocAddString(projectName);
    notifyData.taskName = NULL;
    notifyData.bucketName = allocAddString(bucketName);
    notifyData.bucketFilled = false;
    notifyData.taskFinalized = false;
    notifyData.noPermission = true;
    notifyData.requestedDonations = donationQueue;

    gslGroupProject_ContributionNotify(&notifyData);

    notifyData.requestedDonations = NULL;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_DonateSimpleItems(Entity *pEnt, Guild* pGuild, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
    if ( guild_IsMember(pEnt) && pGuild )
    {
        GuildMember *pMember;
        if ( pGuild == NULL )
        {
            return;
        }

        pMember = guild_FindMemberInGuild(pEnt, pGuild);
        if ( pMember == NULL )
        {
            return;
        }

        if ( guild_HasPermission(pMember->iRank, pGuild, GuildPermission_DonateToProjects) )
        {
            TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateSimpleItems", pEnt, DonateSimpleItems_CB, NULL);
            GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pEnt, "GroupProject:Donate", projectName);

            AutoTrans_GroupProject_tr_DonateSimpleItems(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, pEnt->myContainerID, 
                GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pGuild->iContainerID, projectName, taskSlotNum, bucketName, itemName, count, &reason, pExtract);
        }
        else
        {
            SendDonationNoPermissionNotify(pEnt, itemName, projectName, bucketName, count, NULL);
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_DonateSimpleItems(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
    if ( pEnt )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateSimpleItems", pEnt, DonateSimpleItems_CB, NULL);
        GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "PlayerProject:Donate", projectName);

        AutoTrans_GroupProject_tr_DonateSimpleItems(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, entGetContainerID(pEnt), 
            GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, taskSlotNum, bucketName, itemName, count, &reason, pExtract);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_DonateSimpleItems(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildProject_DonateSimpleItems(pEnt, pGuild, projectName, taskSlotNum, bucketName, itemName, count);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(GuildProject_DonateSimpleItems) ACMD_LIST(gGatewayCmdList);
void
gslGuildProject_DonateSimpleItems_Gateway(Entity *pEnt, const char *guildID, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
	Guild *pGuild = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), guildID);
	// Gateway sends down all values with the pItem->fScaleUI already applied, so undo that here. 
	ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, itemName);
	count /= SAFE_MEMBER(pItemDef, fScaleUI);
	GuildProject_DonateSimpleItems(pEnt, pGuild, projectName, taskSlotNum, bucketName, itemName, count);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_DonateSimpleItems(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
    PlayerProject_DonateSimpleItems(pEnt, projectName, taskSlotNum, bucketName, itemName, count);
}

static void
DonateExpressionItem_CB(TransactionReturnVal *returnVal, void *userData)
{

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_DonateExpressionItem(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, const char *itemName, int bagID, int slotIdx, int count)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) && count > 0 )
    {
        Guild* pGuild;
        GuildMember *pMember;

        pGuild = guild_GetGuild(pEnt);
        if ( pGuild == NULL )
        {
            return;
        }

        pMember = guild_FindMemberInGuild(pEnt, pGuild);
        if ( pMember == NULL )
        {
            return;
        }

        if ( guild_HasPermission(pMember->iRank, pGuild, GuildPermission_DonateToProjects) )
        {
            GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
            Item *item;

            item = inv_GetItemFromBag(pEnt, bagID, slotIdx, pExtract);
            if ( item != NULL )
            {
                DonationTaskDef *taskDef;
                GroupProjectDonationRequirement *taskBucket;

                taskDef = RefSystem_ReferentFromString(g_DonationTaskDict, taskName);
                if ( taskDef != NULL )
                {
                    int donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
                    taskBucket = eaGet(&taskDef->buckets, donationRequireIndex);

                    if ( taskBucket != NULL )
                    {
                        if ( DonationTask_ItemMatchesExpressionRequirement(pEnt, taskBucket, item) )
                        {
                            TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateExpressionItem", pEnt, DonateExpressionItem_CB, NULL);
							ItemChangeReason reason = {0};

							inv_FillItemChangeReason(&reason, pEnt, "GroupProject:Donate", projectName);
           
                            AutoTrans_GroupProject_tr_DonateExpressionItem(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, pEnt->myContainerID, 
                                GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, taskSlotNum, taskName, bucketName, itemName, bagID, slotIdx, count, &reason, pExtract);
                        }
                    }
                }
            }
        }
        else
        {
            SendDonationNoPermissionNotify(pEnt, itemName, projectName, bucketName, count, NULL);
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_DonateExpressionItem(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, const char *itemName, int bagID, int slotIdx, int count)
{
    if ( pEnt && count > 0 )
    {
        GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
        Item *item;

        item = inv_GetItemFromBag(pEnt, bagID, slotIdx, pExtract);
        if ( item != NULL )
        {
            DonationTaskDef *taskDef;
            GroupProjectDonationRequirement *taskBucket;

            taskDef = RefSystem_ReferentFromString(g_DonationTaskDict, taskName);
            if ( taskDef != NULL )
            {
                int donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
                taskBucket = eaGet(&taskDef->buckets, donationRequireIndex);

                if ( taskBucket != NULL )
                {
                    if ( DonationTask_ItemMatchesExpressionRequirement(pEnt, taskBucket, item) )
                    {
                        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateExpressionItem", pEnt, DonateExpressionItem_CB, NULL);
						ItemChangeReason reason = {0};

						inv_FillItemChangeReason(&reason, pEnt, "PlayerProject:Donate", projectName);

                        AutoTrans_GroupProject_tr_DonateExpressionItem(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, entGetContainerID(pEnt), 
                            GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, taskSlotNum, taskName, bucketName, itemName, bagID, slotIdx, count, &reason, pExtract);
                    }
                }
            }

        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_DonateExpressionItemList(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, ContributionItemList *donationQueue)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) && donationQueue && eaSize(&donationQueue->items) > 0 )
    {
        Guild* pGuild;
        GuildMember *pMember;

        pGuild = guild_GetGuild(pEnt);
        if ( pGuild == NULL )
        {
            return;
        }

        pMember = guild_FindMemberInGuild(pEnt, pGuild);
        if ( pMember == NULL )
        {
            return;
        }

        if ( guild_HasPermission(pMember->iRank, pGuild, GuildPermission_DonateToProjects) )
        {
            GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
            DonationTaskDef *taskDef = RefSystem_ReferentFromString(g_DonationTaskDict, taskName);
            GroupProjectDonationRequirement *taskBucket;
            int i, donationRequireIndex;
            TransactionReturnVal *returnVal;
			ItemChangeReason reason = {0};

            if ( taskDef == NULL )
            {
                return;
            }

            donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
            taskBucket = eaGet(&taskDef->buckets, donationRequireIndex);
            if ( taskBucket == NULL )
            {
                return;
            }

            for ( i = eaSize(&donationQueue->items) - 1; i >= 0; i-- )
            {
                Item *item;
                ContributionItemData *donation = donationQueue->items[i];

                if ( donation == NULL || donation->count <= 0 )
                {
                    return;
                }

                item = inv_GetItemFromBag(pEnt, donation->bagID, donation->slotIdx, pExtract);
                if ( item == NULL )
                {
                    return;
                }


                if ( !DonationTask_ItemMatchesExpressionRequirement(pEnt, taskBucket, item) )
                {
                    return;
                }
            }

            returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateExpressionItemList", pEnt, DonateExpressionItem_CB, NULL);

			inv_FillItemChangeReason(&reason, pEnt, "GroupProject:DonateItemList", projectName);

            AutoTrans_GroupProject_tr_DonateExpressionItemList(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, pEnt->myContainerID, 
                GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, taskSlotNum, taskName, bucketName, 
                donationQueue, &reason, pExtract);
        }
        else
        {
            SendDonationNoPermissionNotify(pEnt, NULL, projectName, bucketName, 0, donationQueue->items);
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_DonateExpressionItemList(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, ContributionItemList *donationQueue)
{
    if ( pEnt && donationQueue && eaSize(&donationQueue->items) > 0 )
    {
        GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
        DonationTaskDef *taskDef = RefSystem_ReferentFromString(g_DonationTaskDict, taskName);
        GroupProjectDonationRequirement *taskBucket;
        int i, donationRequireIndex;
        TransactionReturnVal *returnVal;
		ItemChangeReason reason = {0};

        if ( taskDef == NULL )
        {
            return;
        }

        donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
        taskBucket = eaGet(&taskDef->buckets, donationRequireIndex);
        if ( taskBucket == NULL )
        {
            return;
        }

        for ( i = eaSize(&donationQueue->items) - 1; i >= 0; i-- )
        {
            Item *item;
            ContributionItemData *donation = donationQueue->items[i];

            if ( donation == NULL || donation->count <= 0 )
            {
                return;
            }

            item = inv_GetItemFromBag(pEnt, donation->bagID, donation->slotIdx, pExtract);
            if ( item == NULL )
            {
                return;
            }


            if ( !DonationTask_ItemMatchesExpressionRequirement(pEnt, taskBucket, item) )
            {
                return;
            }
        }

        returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateExpressionItemList", pEnt, DonateExpressionItem_CB, NULL);

		inv_FillItemChangeReason(&reason, pEnt, "PlayerProject:DonateItemList", projectName);

        AutoTrans_GroupProject_tr_DonateExpressionItemList(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, entGetContainerID(pEnt), 
            GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, taskSlotNum, taskName, bucketName, 
            donationQueue, &reason, pExtract);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGroupProject_RequestProjectDefsByType(Entity *pEnt, /* GroupProjectType */ int projectType)
{
    EARRAY_OF(GroupProjectDefRef) returnDefs = NULL;
    ResourceIterator iter;
    GroupProjectDef *projectDef;

    if ( !resInitIterator(g_GroupProjectDict, &iter) )
    {
        return;
    }

    // Iterate all GroupProjectDefs and find the ones that match the given type.
    while ( resIteratorGetNext(&iter, NULL, &projectDef) )
    {
        if ( projectDef->type == projectType )
        {
            GroupProjectDefRef *projectDefRef = StructCreate(parse_GroupProjectDefRef);
            SET_HANDLE_FROM_REFERENT(g_GroupProjectDict, projectDef, projectDefRef->projectDef);
            eaPush(&returnDefs, projectDefRef);
        }
    }
	resFreeIterator(&iter);

    // If any matching projects were found, send them to the client.
    if ( eaSize(&returnDefs) > 0 )
    {
        GroupProjectDefs *projectDefs = StructCreate(parse_GroupProjectDefs);
        projectDefs->projectDefs = returnDefs;
        ClientCmd_gclGroupProject_ReceiveProjectDefsByType(pEnt, projectType, projectDefs);
        StructDestroy(parse_GroupProjectDefs, projectDefs);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGroupProject_SubscribeToGuildProject(Entity *pEnt)
{
    GroupProject_SubscribeToGuildProject(pEnt);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGroupProject_SubscribeToPlayerProject(Entity *pEnt)
{
    GroupProject_SubscribeToPlayerProjectContainer(pEnt);
}

// This is a debug command that allows a developer to override the guild allegiance on a guild owned map.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_OverrideMapAllegiance(Entity *pEnt, const char *overrideAllegiance)
{
    GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(entGetPartitionIdx(pEnt));

    if ( pPartitionState != NULL )
    {
        pPartitionState->overrideAllegiance = allocAddString(overrideAllegiance);
    }
}

// This is a debug command that allows a designer to query the values of the Group Project Numerics associated with the guild owner of their current map.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
const char *
GuildProject_GetProjectNumeric(Entity *pEnt, const char *projectName, const char *numericName)
{
    GroupProjectState *projectState;
    GroupProjectNumericData *numericData;

    static char *estrBuf = NULL;

    estrClear(&estrBuf);

    // Find the project state.
    projectState = gslGroupProject_GetGroupProjectStateForMap(GroupProjectType_Guild, projectName, entGetPartitionIdx(pEnt));
    if ( projectState == NULL )
    {
        estrPrintf(&estrBuf, "GroupProjectState %s not found", projectName);
    }
    else
    {
        // Find the numeric.
        numericData = eaIndexedGetUsingString(&projectState->numericData, numericName);
        if ( numericData == NULL )
        {
            GroupProjectDef *projectDef = GET_REF(projectState->projectDef);
            if ( projectDef )
            {
                if ( eaIndexedGetUsingString(&projectDef->validNumerics, numericName) )
                {
                    // The numeric is valid, so return 0 which is the default value.
                    estrPrintf(&estrBuf, "0");
                    return estrBuf;
                }
            }
            estrPrintf(&estrBuf, "GroupProjectNumericData %s not found", numericName);
        }
        else
        {
            estrPrintf(&estrBuf, "%d", numericData->numericVal);
        }
    }

    return estrBuf;
}

static void
FillBucket_CB(TransactionReturnVal *returnVal, void *cbData)
{

}

// This is a debug command that that instantly fills a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_FillBucket(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("FillBucket", GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID, FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceFillBucket(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID, projectName, taskSlotNum, bucketName);
    }
}

// This is a debug command that that instantly fills a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_FillBucket(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName)
{
    if ( pEnt )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("FillBucket", GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt), FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceFillBucket(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt), projectName, taskSlotNum, bucketName);
    }
}

// This is a debug command that that instantly fills a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_FillAllBuckets(Entity *pEnt)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("FillAllBuckets", GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID, FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceFillAllBuckets(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID);
    }
}

// This is a debug command that that instantly fills a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_FillAllBuckets(Entity *pEnt)
{
    if ( pEnt )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("FillAllBuckets", GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt), FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceFillAllBuckets(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt));
    }
}

// This is a debug command that will set the filled amount of a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_SetBucketFill(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, int count)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("SetBucketFill", GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID, FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceSetBucketFill(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID, projectName, taskSlotNum, bucketName, count);
    }
}

// This is a debug command that will set the filled amount of a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_SetBucketFill(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, int count)
{
    if ( pEnt )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("SetBucketFill", GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt), FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceSetBucketFill(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt), projectName, taskSlotNum, bucketName, count);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(GroupProject);
void
gslGroupProject_GetMapState(Entity *pEnt)
{
    if (pEnt)
    {
        int iPartitionIdx = entGetPartitionIdx(pEnt);

        ClientCmd_gclGroupProject_SetMapState(pEnt, gslGroupProject_GetGuildAllegianceForGroupProjectMap(iPartitionIdx),
            partition_OwnerTypeFromIdx(iPartitionIdx), partition_OwnerIDFromIdx(iPartitionIdx));
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_SetProjectMessage(Entity *pEnt, const char *projectName, ACMD_SENTENCE projectMessage)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        Guild* pGuild = guild_GetGuild(pEnt);
        GuildMember *pMember = guild_FindMemberInGuild(pEnt, pGuild);

        // Only set the message if the player has permission.
        if ( guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildProjectManagement) )
        {
            RemoteCommand_aslGroupProject_SetProjectMessage(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, 
                GLOBALTYPE_GUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, projectMessage);
        }
        else
        {
            //XXX - notify here
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_SetProjectMessage(Entity *pEnt, const char *projectName, ACMD_SENTENCE projectMessage)
{
    GuildProject_SetProjectMessage(pEnt, projectName, projectMessage);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_SetProjectPlayerName(Entity *pEnt, const char *projectName, ACMD_SENTENCE projectPlayerName)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        Guild* pGuild = guild_GetGuild(pEnt);
        GuildMember *pMember = guild_FindMemberInGuild(pEnt, pGuild);

        // Only set the name if the player has permission.
        if ( guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildProjectManagement) )
        {
            RemoteCommand_aslGroupProject_SetProjectPlayerName(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, 
                GLOBALTYPE_GUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, projectPlayerName);
        }
        else
        {
            //XXX - notify here
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_SetProjectPlayerName(Entity *pEnt, const char *projectName, ACMD_SENTENCE projectPlayerName)
{
    GuildProject_SetProjectPlayerName(pEnt, projectName, projectPlayerName);
}

static void
DumpProjectState(char **hStrReturn, GroupProjectState *projectState)
{
    int i;
    estrAppend2(hStrReturn, "Numerics:\n");
    for ( i = eaSize(&projectState->numericData) - 1; i >= 0; i-- )
    {
        GroupProjectNumericData *numericData = projectState->numericData[i];
        GroupProjectNumericDef *numericDef = GET_REF(numericData->numericDef);

        estrConcatf(hStrReturn, "  %s: %d\n", numericDef->name, numericData->numericVal);
    }
    estrAppend2(hStrReturn, "Unlocks:\n");
    for ( i = eaSize(&projectState->unlocks) - 1; i >= 0; i-- )
    {
        GroupProjectUnlockDefRefContainer *unlockDefRef = projectState->unlocks[i];
        GroupProjectUnlockDef *unlockDef = GET_REF(unlockDefRef->unlockDef);
        estrConcatf(hStrReturn, "  %s\n", unlockDef->name);
    }
    return;
}

// This is a debug command that that prints the numerics and unlocks from the guild projects associated with the current map.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject) ACMD_NAME(DumpGuildProjectStateForMap);
char *
GroupProject_DumpGuildProjectStateForMap(Entity *pEnt, const char *projectName)
{
    static char *s_estrReturn = NULL;

    GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(entGetPartitionIdx(pEnt));

    if ( pPartitionState != NULL )
    {
        GroupProjectContainer *projectContainer = GET_REF(pPartitionState->guildProjectContainerRef);

        if ( projectContainer )
        {
            GroupProjectState *projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);

            estrClear(&s_estrReturn);

            if ( projectState )
            {
                DumpProjectState(&s_estrReturn, projectState);
            }
        }
    }

    return(s_estrReturn);
}

// This is a debug command that that prints the numerics and unlocks from the guild project associated with the player.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject) ACMD_NAME(DumpGuildProjectStateForPlayer);
char *
GroupProject_DumpGuildProjectStateForPlayer(Entity *pEnt, const char *projectName)
{
    static char *s_estrReturn = NULL;
    GroupProjectContainer *projectContainer;

    GroupProject_ValidateContainer(pEnt, GroupProjectType_Guild);
    projectContainer = GroupProject_ResolveContainer(pEnt, GroupProjectType_Guild);

    if ( projectContainer )
    {
        GroupProjectState *projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);

        estrClear(&s_estrReturn);

        DumpProjectState(&s_estrReturn, projectState);
    }

    return(s_estrReturn);
}

// This is a debug command that that prints the numerics and unlocks from the player project associated with the player.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject) ACMD_NAME(DumpPlayerProjectState);
char *
GroupProject_DumpPlayerProjectState(Entity *pEnt, const char *projectName)
{
    static char *s_estrReturn = NULL;
    GroupProjectContainer *projectContainer;

    GroupProject_ValidateContainer(pEnt, GroupProjectType_Player);
    projectContainer = GroupProject_ResolveContainer(pEnt, GroupProjectType_Player);

    if ( projectContainer )
    {
        GroupProjectState *projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);

        estrClear(&s_estrReturn);

        DumpProjectState(&s_estrReturn, projectState);
    }

    return(s_estrReturn);
}

static void
ClaimReward_CB(TransactionReturnVal *returnVal, void *userData)
{

}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_ClaimReward(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    GroupProjectState *projectState;
    DonationTaskSlot *taskSlot;
    DonationTaskDef *taskDef;
    GroupProjectContainer *projectContainer;
    RewardTable *rewardTable;
    InventoryBag** rewardBags = NULL;
    bool uniqueItemsInBags;
    GameAccountDataExtract* extract;
    GiveRewardBagsData Rewards = {0};
    ItemChangeReason reason = {0};
    U32* eaPets = NULL;
    TransactionReturnVal* pReturn;

    if ( pEnt && pEnt->pPlayer )
    {
        projectContainer = GET_REF(pEnt->pPlayer->hGroupProjectContainer);
        if ( projectContainer == NULL )
        {
            return;
        }

        // Find the project state.
        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( projectState == NULL )
        {
            return;
        }

        // Get the task slot.
        taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, taskSlotNum);
        if ( taskSlot == NULL )
        {
            return;
        }

        // Make sure the task is in the correct state to collect rewards.
        if ( taskSlot->state != DonationTaskState_RewardPending )
        {
            return;
        }

        // Get the task def.
        taskDef = GET_REF(taskSlot->taskDef);
        if ( taskDef == NULL )
        {
            return;
        }

        // Get reward table for task.
        rewardTable = GET_REF(taskDef->completionRewardTable);
        if ( rewardTable == NULL )
        {
            return;
        }

        // Generate rewards.
        reward_GenerateBagsForPersonalProject(entGetPartitionIdx(pEnt), pEnt, rewardTable, entity_GetSavedExpLevel(pEnt), &rewardBags);

        // Is there a unique item in the reward bag?
        uniqueItemsInBags = inv_CheckUniqueItemsInBags(rewardBags);

        // Get the game account data extract.
        extract = entity_GetCachedGameAccountDataExtract(pEnt);

        inv_FillItemChangeReason(&reason, pEnt, "GroupProject:CollectRewards", taskDef->name);
        Rewards.ppRewardBags = rewardBags;

        pReturn = LoggedTransactions_CreateManagedReturnValEnt("PlayerProject_ClaimReward", pEnt, ClaimReward_CB, NULL);

        if (uniqueItemsInBags)
        {
            ea32Create(&eaPets);
            Entity_GetPetIDList(pEnt, &eaPets);				
        }

        AutoTrans_GroupProject_tr_ClaimRewards(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), 
            GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
            &Rewards, &reason, extract, projectName, taskSlotNum);

        if (eaPets)
            ea32Destroy(&eaPets);

		// free up this struct as its no longer needed and we dont want to leak memory
		StructDeInit(parse_GiveRewardBagsData, &Rewards);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_ClaimReward(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    PlayerProject_ClaimReward(pEnt, projectName, taskSlotNum);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_GetRewardBags(Entity *pEnt, const char *projectName, const char *taskName)
{
    GroupProjectDef *projectDef;
    GroupProjectState *projectState;
    DonationTaskDef *taskDef;
    DonationTaskDefRef *taskDefRef;
    GroupProjectContainer *projectContainer;
    RewardTable *rewardTable;
    InventoryBag** rewardBags = NULL;
    InvRewardRequest *rewardRequest = NULL;

    if ( pEnt && pEnt->pPlayer )
    {
        projectContainer = GET_REF(pEnt->pPlayer->hGroupProjectContainer);
        if ( projectContainer == NULL )
        {
            return;
        }

        // Find the project state.
        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( projectState == NULL )
        {
            return;
        }

        // Get the project def
        projectDef = GET_REF(projectState->projectDef);
        if ( projectDef == NULL )
        {
            return;
        }

        // Find the task def
        taskDefRef = eaIndexedGetUsingString(&projectDef->donationTaskDefs, taskName);
        if ( taskDefRef == NULL )
        {
            return;
        }

        // Get the task def.
        taskDef = GET_REF(taskDefRef->taskDef);
        if ( taskDef == NULL )
        {
            return;
        }

        // Get reward table for task.
        rewardTable = GET_REF(taskDef->completionRewardTable);
        if ( rewardTable != NULL )
        {
            // Generate rewards.
            reward_GenerateBagsForPersonalProject(entGetPartitionIdx(pEnt), pEnt, rewardTable, entity_GetSavedExpLevel(pEnt), &rewardBags);

            // Fill the reward request
            if ( eaSize(&rewardBags) > 0 )
            {
                rewardRequest = StructCreate(parse_InvRewardRequest);
                inv_FillRewardRequest(rewardBags, rewardRequest);
            }
        }

		// Send the rewards to the client
		ClientCmd_gclGroupProject_ReceiveRewards(pEnt, projectName, taskName, rewardRequest);

		// destroy rewardrequest otherwise we will leak memory
		if(rewardRequest)
		{
			StructDestroy(parse_InvRewardRequest, rewardRequest);
		}

		if(rewardBags)
		{
			// Destroy the reward bags as the above function never attaches them to rewardRequest. If it ever does this will crash
			eaDestroyStruct(&rewardBags, parse_InventoryBag);
		}
    }
}

// This is a command intended to be called from the GroupProject server. It's role is to fire a GameEvent when a task completes
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void
gslGroupProject_TaskCompleteCB(ContainerID entID, const char *projectName)
{
    Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);
    if (pEnt && projectName)
    {
        eventsend_RecordGroupProjectTaskComplete(entGetPartitionIdx(pEnt), pEnt, projectName);
    }
}

static void
CancelProject_CB(TransactionReturnVal *returnVal, void *userData)
{

}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_CancelProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    GroupProjectState *projectState;
    DonationTaskSlot *taskSlot;
    DonationTaskDef *taskDef;
    GroupProjectContainer *projectContainer;
    TransactionReturnVal* pReturn;

    if ( pEnt && pEnt->pPlayer )
    {
        projectContainer = GET_REF(pEnt->pPlayer->hGroupProjectContainer);
        if ( projectContainer == NULL )
        {
            return;
        }

        // Find the project state.
        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( projectState == NULL )
        {
            return;
        }

        // Get the task slot.
        taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, taskSlotNum);
        if ( taskSlot == NULL )
        {
            return;
        }

        // Make sure the task is in the correct state to cancel it.
        if ( taskSlot->state != DonationTaskState_AcceptingDonations )
        {
            return;
        }

        // Get the task def.
        taskDef = GET_REF(taskSlot->taskDef);
        if ( taskDef == NULL )
        {
            return;
        }

        if ( !taskDef->cancelable )
        {
            return;
        }

        pReturn = LoggedTransactions_CreateManagedReturnValObj("PlayerProject_CancelProject", GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), CancelProject_CB, NULL);

        AutoTrans_GroupProject_tr_CancelProject(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt),
            projectName, taskSlotNum);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_CancelProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    PlayerProject_CancelProject(pEnt, projectName, taskSlotNum);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_CancelProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    Guild *pGuild;
    GuildMember *pMember;
    TransactionReturnVal* pReturn;

    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        pGuild = guild_GetGuild(pEnt);
        if ( pGuild == NULL )
        {
            return;
        }

        pMember = guild_FindMemberInGuild(pEnt, pGuild);
        if ( pMember == NULL )
        {
            return;
        }

        if ( pMember->iRank < (eaSize(&pGuild->eaRanks) - 1) )
        {
            return;
        }

        pReturn = LoggedTransactions_CreateManagedReturnValObj("GuildProject_CancelProject", GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, CancelProject_CB, NULL);

        AutoTrans_GroupProject_tr_CancelProject(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID,
            projectName, taskSlotNum);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_CancelProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    GuildProject_CancelProject(pEnt, projectName, taskSlotNum);
}