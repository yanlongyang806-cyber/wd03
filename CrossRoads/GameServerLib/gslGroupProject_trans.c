/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslGroupProject.h"
#include "GroupProjectCommon.h"
#include "GroupProjectCommon_trans.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "Player.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "timing.h"
#include "AutoTransDefs.h"
#include "StringCache.h"
#include "logging.h"
#include "rewardCommon.h"

#include "AutoGen/GroupProjectCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/gslGroupProject_h_ast.h"

void
SendContributionNotification(ATR_ARGS, GlobalType playerType, ContainerID playerID, GlobalType projectContainerType, ContainerID projectContainerID, 
    const char *donatedItemName, S32 requestedDonationCount, S32 donationCount, S32 initialContribution, S32 contributionGiven, const char *contributionNumericName, S32 contributionEarned, const char *projectName, const char *taskName, const char *bucketName, bool bucketFilled, bool taskFinalized, bool fullDonation, ContributionItemData **requestedDonations, ContributionItemData **actualDonations)
{
    ContributionNotifyData *notifyData = StructCreate(parse_ContributionNotifyData);
    notifyData->playerID = playerID;
    notifyData->projectcontainerType = projectContainerType;
    notifyData->projectContainerID = projectContainerID;
    notifyData->donatedItemName = allocAddString(donatedItemName);
    notifyData->requestedDonationCount = requestedDonationCount;
    notifyData->donationCount = donationCount;
    notifyData->initialContribution = initialContribution;
    notifyData->contributionGiven = contributionGiven;
    notifyData->contributionNumericName = allocAddString(contributionNumericName);
    notifyData->contributionEarned = contributionEarned;
    notifyData->projectName = allocAddString(projectName);
    notifyData->taskName = allocAddString(taskName);
    notifyData->bucketName = allocAddString(bucketName);
    notifyData->bucketFilled = bucketFilled;
    notifyData->taskFinalized = taskFinalized;
    notifyData->partialDonation = !fullDonation;
    eaCopyStructs(&requestedDonations, &notifyData->requestedDonations, parse_ContributionItemData);
    eaCopyStructs(&actualDonations, &notifyData->actualDonations, parse_ContributionItemData);

    QueueRemoteCommand_gslGroupProject_ContributionNotify(ATR_RESULT_SUCCESS, playerType, playerID, notifyData);

    StructDestroy(parse_ContributionNotifyData, notifyData);
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_RecordContributionStats(ATR_ARGS, ATH_ARG NOCONST(Entity) *playerEnt, ATH_ARG NOCONST(GroupProjectState) *projectState, S32 contribution)
{
    char *displayName = NULL;
    NOCONST(GroupProjectDonationStats) *donationStats;

    if ( ISNULL(playerEnt->pSaved) || ISNULL(playerEnt->pPlayer) )
    {
        return false;
    }
    estrPrintf(&displayName, "%s@%s", playerEnt->pSaved->savedName, playerEnt->pPlayer->publicAccountName);

    // Find the donation stats for this player, or create one if it doesn't already exist.
    donationStats = eaIndexedGetUsingInt(&projectState->donationStats, playerEnt->myContainerID);
    if ( ISNULL(donationStats) )
    {
        donationStats = StructCreateNoConst(parse_GroupProjectDonationStats);
        donationStats->donatorID = playerEnt->myContainerID;
        eaIndexedPushUsingIntIfPossible(&projectState->donationStats, playerEnt->myContainerID, donationStats);
    }

    // Free the old name.
    if ( NONNULL(donationStats->displayName) )
    {
        free(donationStats->displayName);
    }

    // We copy the name every time in case the player's name changes, which is usually because of CSR intervention.
    donationStats->displayName = strdup(displayName);

    donationStats->contribution += contribution;

    return true;
}

AUTO_TRANSACTION
ATR_LOCKS(playerEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Psaved.Savedname, .Pplayer.Publicaccountname, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems")
ATR_LOCKS(projectContainer, ".Containertype, .Containerid, projectList[]");
enumTransactionOutcome
GroupProject_tr_DonateSimpleItems(ATR_ARGS, NOCONST(Entity) *playerEnt, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int slotNum, const char *bucketName, const char *itemName, S32 count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
    NOCONST(GroupProjectState) *projectState;
    NOCONST(DonationTaskSlot) *taskSlot;
    NOCONST(DonationTaskBucketData) *bucketData;
    DonationTaskDef *taskDef;
    GroupProjectDef *projectDef;
    ItemDef *contributionNumeric;
    ItemDef *lifetimeContributionNumeric;
    GroupProjectDonationRequirement *donationRequirement;
    S32 remaining;
    const char *itemNameFromReq;
    S32 contribution = 0;
    S32 contributionPerItem;
	S32 donationRequireIndex;
    S32 countRemoved = 0;
    S32 initialContribution = 0, contributionGiven = 0;
    bool bucketFilled = false;
    bool taskFinalized = false;
    S32 requestedCount = count;
    S32 donationIncrement;
    ItemDef *spendingNumeric = NULL;
    ItemDef *donationItemDef = NULL;

    if ( ISNULL(projectContainer) || ISNULL(projectName) || ISNULL(bucketName) || ISNULL(itemName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Fail if there is nothing to donate.
    if ( count <= 0 )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Validate task slot number.
    if ( ( slotNum < 0 ) || ( slotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, slotNum);
    if ( ISNULL(taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task is in the correct state to accept donations.
    if ( taskSlot->state != DonationTaskState_AcceptingDonations )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task def.
    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the requirements.
	donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
	donationRequirement = eaGet(&taskDef->buckets, donationRequireIndex);
    if ( ISNULL(donationRequirement) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // This transaction can only handle simple item donations.
    if ( donationRequirement->specType != DonationSpecType_Item )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the item name from the requirement.
    itemNameFromReq = REF_STRING_FROM_HANDLE(donationRequirement->requiredItem);
    if ( ISNULL(itemNameFromReq) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the item matches.
    if ( stricmp(itemName, itemNameFromReq) != 0 )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }
    
    // Get the bucket data.
    bucketData = eaIndexedGetUsingString(&taskSlot->buckets, bucketName);
    if ( ISNULL(bucketData) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the value that contribution is divided by.
    donationIncrement = donationRequirement->donationIncrement;
    if ( donationIncrement == 0 )
    {
        donationIncrement = 1;
    }

    // Calculate the number of remaining items required.
    remaining = donationRequirement->count - bucketData->donationCount;
    if ( remaining < 0 )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get contribution numeric.
    contributionNumeric = GET_REF(projectDef->contributionNumeric);
    if ( ISNULL(contributionNumeric) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    if ( remaining > 0 )
    {
        ItemChangeReason reason = {0};

        inv_FillItemChangeReason(&reason, NULL, "GroupProject:DonateItems", playerEnt->debugName);

        // Trim donation count to avoid donating too much.
        if ( remaining <= count )
        {
            count = remaining;
        }
        else
        {
            // Make sure that the player is donating in increments of the donationIncrement.
            // Note that we don't do this on donations that are equal to or greater than the remaining count required.  
            //   This is to keep from getting stuck with less than the donationIncrement remaining.
            int extra = count % donationIncrement;
            if ( extra != 0 )
            {
                count = count - extra;
            }
        }

        // Handle numerics that have separate spending numerics.  These are numerics that are spent by adding to separate numeric rather than
        //  by subtracting from the original numeric.  One example in STO is OfficerSkillPoint and OfficerSkillPointsSpent
        donationItemDef = GET_REF(donationRequirement->requiredItem);
        if ( NONNULL(donationItemDef) && ( donationItemDef->eType == kItemType_Numeric ) )
        {
            spendingNumeric = GET_REF(donationItemDef->hSpendingNumeric);
        }

        if ( NONNULL(spendingNumeric) )
        {
            S32 donationNumericValue;
            S32 spendingNumericValue;

            donationNumericValue = inv_trh_GetNumericValue(ATR_PASS_ARGS, playerEnt, donationItemDef->pchName);
            spendingNumericValue = inv_trh_GetNumericValue(ATR_PASS_ARGS, playerEnt, spendingNumeric->pchName);
            if ( spendingNumericValue >= donationNumericValue )
            {
                // Player has spent all the points they have.
                countRemoved = 0;
            }
            else
            {
                S32 numericRemaining = donationNumericValue - spendingNumericValue;
                if ( numericRemaining >= count )
                {
                    countRemoved = count;
                }
                else
                {
                    countRemoved = numericRemaining;
                }
                if ( !inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, playerEnt, true, spendingNumeric->pchName, countRemoved, NumericOp_Add, &reason) )
                {
                    return TRANSACTION_OUTCOME_FAILURE;
                }
            }
        }
        else
        {
            // Remove the items from inventory
            countRemoved = inv_trh_FindItemCountByDefName(ATR_PASS_ARGS, playerEnt, itemName, count, true, pReason, pExtract);
        }

        // Update bucket count.
        bucketData->donationCount += countRemoved;

        // Get the value of the contribution constant.
        if ( !GroupProject_trh_GetProjectConstant(ATR_PASS_ARGS, projectState, donationRequirement->contributionConstant, &contributionPerItem) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    
        // Calculate contribution.  Round up if we end up donating a fraction of the donation increment.
        contribution = ( ( countRemoved + donationIncrement - 1 ) / donationIncrement )  * contributionPerItem;

        initialContribution = inv_trh_GetNumericValue(ATR_PASS_ARGS, playerEnt, contributionNumeric->pchName);

        // Give contribution.
        if ( !inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, playerEnt, false, contributionNumeric->pchName, contribution, NumericOp_Add, &reason) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        contributionGiven = inv_trh_GetNumericValue(ATR_PASS_ARGS, playerEnt, contributionNumeric->pchName) - initialContribution;

        // Get lifetime contribution numeric.
        lifetimeContributionNumeric = GET_REF(projectDef->lifetimeContributionNumeric);

        if ( NONNULL(lifetimeContributionNumeric) )
        {
            if ( !inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, playerEnt, false, lifetimeContributionNumeric->pchName, contribution, NumericOp_Add, &reason) )
            {
                return TRANSACTION_OUTCOME_FAILURE;
            }
        }

        if (!GroupProject_trh_RecordContributionStats(ATR_PASS_ARGS, playerEnt, projectState, contribution))
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    // Check for bucket completion.
    if ( ( countRemoved > 0 )  && ( bucketData->donationCount == donationRequirement->count ) )
    {
        taskSlot->completedBuckets++;
        bucketFilled = true;
    }

    // If all donations are completed, we can finalize the project.
	if ( bucketFilled && ( taskSlot->completedBuckets == (U32)eaSize(&taskDef->buckets) ) )
    {
        GroupProject_trh_FinalizeTask(ATR_PASS_ARGS, taskSlot, taskDef, projectName);
        taskFinalized = true;
    }

    SendContributionNotification(ATR_PASS_ARGS, playerEnt->myEntityType, playerEnt->myContainerID, projectContainer->containerType, projectContainer->containerID,
		itemName, requestedCount, countRemoved, initialContribution, contributionGiven, contributionNumeric->pchName, contribution, projectName, taskDef->name, bucketName, bucketFilled, taskFinalized, requestedCount == countRemoved, NULL, NULL);

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "DonateSimpleItems", 
        "ProjectName %s TaskSlot %d TaskName %s BucketName %s ItemName %s RequestedCount %d CountRemoved %d ContributionNumeric %s Contribution %d BucketFilled %d TaskFinalized %d", 
        projectName, taskSlot->taskSlotNum, taskDef->name, bucketName, itemName, requestedCount, countRemoved, contributionNumeric->pchName, contribution, bucketFilled, taskFinalized);

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
int
GroupProject_trh_DonateSingleItemStack(ATR_ARGS, ATH_ARG NOCONST(Entity) *playerEnt, int remaining, const char *itemName, int bagID, int slotIdx, int count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
    int contributedCount = 0;

    if ( count > remaining )
    {
        count = remaining;
    }

    if ( remaining > 0 )
    {
        Item *removedItem;
        ItemDef *removedItemDef;

        // Remove the item from inventory.
        removedItem = invbag_RemoveItem(ATR_PASS_ARGS, playerEnt, false, bagID, slotIdx, count, pReason, pExtract);
        if ( ISNULL(removedItem) )
        {
            return -1;
        }

        // Make sure we removed the right item.
        removedItemDef = GET_REF(removedItem->hItem);
        if ( stricmp(removedItemDef->pchName, itemName) != 0 )
        {
            StructDestroy(parse_Item, removedItem);
            return -1;
        }

        // Update bucket count.
        contributedCount = removedItem->count;

        StructDestroy(parse_Item, removedItem);
    }

    return contributedCount;
}

AUTO_TRANSACTION
ATR_LOCKS(playerEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Psaved.Savedname, .Pplayer.Publicaccountname, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
ATR_LOCKS(projectContainer, ".Containertype, .Containerid, projectList[]");
enumTransactionOutcome
GroupProject_tr_DonateExpressionItem(ATR_ARGS, NOCONST(Entity) *playerEnt, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, const char *itemName, int bagID, int slotIdx, int count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
    NOCONST(GroupProjectState) *projectState;
    NOCONST(DonationTaskSlot) *taskSlot;
    NOCONST(DonationTaskBucketData) *bucketData;
    DonationTaskDef *taskDef;
    GroupProjectDef *projectDef;
    ItemDef *contributionNumeric = NULL;
    ItemDef *lifetimeContributionNumeric;
    GroupProjectDonationRequirement *donationRequirement;
    S32 remaining;
    S32 donationCount;
    S32 contributedCount = 0;
    S32 contributionTotal;
    S32 contributionPerItem = 0;
    S32 donationRequireIndex;
    S32 initialContribution = 0, contributionGiven = 0;
    ItemChangeReason reason = {0};
    bool bucketFilled = false;
    bool taskFinalized = false;
    bool itemRemoved = false;

    if ( ISNULL(projectContainer) || ISNULL(projectName) || ISNULL(bucketName) || ISNULL(itemName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Validate task slot number.
    if ( ( taskSlotNum < 0 ) || ( taskSlotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, taskSlotNum);
    if ( ISNULL(taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task is in the correct state to accept donations.
    if ( taskSlot->state != DonationTaskState_AcceptingDonations )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task def.
    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task name matches.
    if ( stricmp(taskName, taskDef->name) != 0 )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the requirements.
    donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
    donationRequirement = eaGet(&taskDef->buckets, donationRequireIndex);
    if ( ISNULL(donationRequirement) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // This transaction can only handle simple item donations.
    if ( donationRequirement->specType != DonationSpecType_Expression )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the bucket data.
    bucketData = eaIndexedGetUsingString(&taskSlot->buckets, bucketName);
    if ( ISNULL(bucketData) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Calculate the number of remaining items required.
    remaining = donationRequirement->count - bucketData->donationCount;
    if ( remaining < 0 )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Determine actual count to try to donate
    if ( count > remaining )
    {
        donationCount = remaining;
    }
    else
    {
        donationCount = count;
    }

    if ( remaining > 0 )
    {
        // Remove the item from inventory.
        contributedCount = GroupProject_trh_DonateSingleItemStack(ATR_PASS_ARGS, playerEnt, remaining, itemName, bagID, slotIdx, donationCount, pReason, pExtract);
        if ( contributedCount < 0 )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        // Update bucket count.
        bucketData->donationCount += contributedCount;

        // Get the value of the contribution constant.
        if ( !GroupProject_trh_GetProjectConstant(ATR_PASS_ARGS, projectState, donationRequirement->contributionConstant, &contributionPerItem) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        contributionTotal = contributionPerItem * contributedCount;

         // Get contribution numeric.
        contributionNumeric = GET_REF(projectDef->contributionNumeric);
        if ( ISNULL(contributionNumeric) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        initialContribution = inv_trh_GetNumericValue(ATR_PASS_ARGS, playerEnt, contributionNumeric->pchName);

        // Give contribution.
        inv_FillItemChangeReason(&reason, NULL, "GroupProject:DonateItems", playerEnt->debugName);
        if ( !inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, playerEnt, false, contributionNumeric->pchName, contributionTotal, NumericOp_Add, &reason) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        contributionGiven = inv_trh_GetNumericValue(ATR_PASS_ARGS, playerEnt, contributionNumeric->pchName) - initialContribution;

        // Get lifetime contribution numeric.
        lifetimeContributionNumeric = GET_REF(projectDef->lifetimeContributionNumeric);

        if ( NONNULL(lifetimeContributionNumeric) )
        {
            if ( !inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, playerEnt, false, lifetimeContributionNumeric->pchName, contributionTotal, NumericOp_Add, &reason) )
            {
                return TRANSACTION_OUTCOME_FAILURE;
            }
        }

        if (!GroupProject_trh_RecordContributionStats(ATR_PASS_ARGS, playerEnt, projectState, contributionPerItem * contributedCount))
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        itemRemoved = contributedCount > 0;
    }

    if ( itemRemoved )
    {
        // Check for bucket completion.
        if ( bucketData->donationCount == donationRequirement->count )
        {
            taskSlot->completedBuckets++;
            bucketFilled = true;
        }

        // If all donations are completed, we can finalize the project.
		if ( taskSlot->completedBuckets == (U32)eaSize(&taskDef->buckets) )
        {
            GroupProject_trh_FinalizeTask(ATR_PASS_ARGS, taskSlot, taskDef, projectName);

            taskFinalized = true;
        }


        SendContributionNotification(ATR_PASS_ARGS, playerEnt->myEntityType, playerEnt->myContainerID, projectContainer->containerType, projectContainer->containerID,
            itemName, count, contributedCount, initialContribution, contributionGiven, contributionNumeric->pchName, contributionPerItem, projectName, taskDef->name, bucketName, bucketFilled, taskFinalized, count == contributedCount, NULL, NULL);

        TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "DonateExpressionItem", 
            "ProjectName %s TaskSlot %d TaskName %s BucketName %s ItemName %s RequestedCount %d CountRemoved %d ContributionNumeric %s Contribution %d BucketFilled %d TaskFinalized %d", 
            projectName, taskSlot->taskSlotNum, taskDef->name, bucketName, itemName, count, contributedCount, contributionNumeric->pchName, contributionPerItem, bucketFilled, taskFinalized);

    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(playerEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Psaved.Savedname, .Pplayer.Publicaccountname, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags, pInventoryV2.ppLiteBags")
ATR_LOCKS(projectContainer, ".Containertype, .Containerid, projectList[]");
enumTransactionOutcome
GroupProject_tr_DonateExpressionItemList(ATR_ARGS, NOCONST(Entity) *playerEnt, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, ContributionItemList *donationQueue, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
    NOCONST(GroupProjectState) *projectState;
    NOCONST(DonationTaskSlot) *taskSlot;
    NOCONST(DonationTaskBucketData) *bucketData;
    DonationTaskDef *taskDef;
    GroupProjectDef *projectDef;
    ItemDef *contributionNumeric;
    ItemDef *lifetimeContributionNumeric;
    GroupProjectDonationRequirement *donationRequirement;
    S32 i;
    S32 remaining;
    S32 requestedCount = 0, contributedCount = 0;
    S32 contributionTotal;
    S32 contributionPerItem;
    S32 donationRequireIndex;
    S32 initialContribution = 0, contributionGiven = 0;
    ItemChangeReason reason = {0};
    bool bucketFilled = false;
    bool taskFinalized = false;
    bool fullDonation = true;
    ContributionItemData **donatedCounts = NULL;

    if ( ISNULL(projectContainer) || ISNULL(projectName) || ISNULL(bucketName) || ISNULL(donationQueue) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Validate task slot number.
    if ( ( taskSlotNum < 0 ) || ( taskSlotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, taskSlotNum);
    if ( ISNULL(taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task is in the correct state to accept donations.
    if ( taskSlot->state != DonationTaskState_AcceptingDonations )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task def.
    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task name matches.
    if ( stricmp(taskName, taskDef->name) != 0 )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the requirements.
    donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
    donationRequirement = eaGet(&taskDef->buckets, donationRequireIndex);
    if ( ISNULL(donationRequirement) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // This transaction can only handle simple item donations.
    if ( donationRequirement->specType != DonationSpecType_Expression )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the bucket data.
    bucketData = eaIndexedGetUsingString(&taskSlot->buckets, bucketName);
    if ( ISNULL(bucketData) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Calculate the number of remaining items required.
    remaining = donationRequirement->count - bucketData->donationCount;
    if ( remaining < 0 )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    for ( i = 0; i < eaSize(&donationQueue->items); i++ )
    {
        ContributionItemData *donation = donationQueue->items[i];
        int donatedCount;

        requestedCount += donation->count;

        // Remove the item from inventory.
        donatedCount = GroupProject_trh_DonateSingleItemStack(ATR_PASS_ARGS, playerEnt, remaining, donation->itemName, donation->bagID, donation->slotIdx, donation->count, pReason, pExtract);
        if ( donatedCount < 0 )
        {
            eaDestroyStruct(&donatedCounts, parse_ContributionItemData);
            return TRANSACTION_OUTCOME_FAILURE;
        }

        // Check to see if the items were completely donated
        if ( donatedCount != donation->count )
        {
            fullDonation = false;
        }

        // If anything was donated
        if ( donatedCount > 0 )
        {
            // Track this donation information
            ContributionItemData *donated = StructCreate(parse_ContributionItemData);
            donated->itemName = donation->itemName;
            donated->bagID = donation->bagID;
            donated->slotIdx = donation->slotIdx;
            donated->count = donatedCount;
            eaPush(&donatedCounts, donated);

            // Update bucket count.
            contributedCount += donatedCount;
            bucketData->donationCount += donatedCount;
            remaining = donationRequirement->count - bucketData->donationCount;
        }
    }

    if ( contributedCount > 0 )
    {
        ContributionItemList itemList = {0};
        char *requestedItemList = NULL;
        char *donatedItemList = NULL;
        char *temp = NULL;

        // Get the value of the contribution constant.
        if ( !GroupProject_trh_GetProjectConstant(ATR_PASS_ARGS, projectState, donationRequirement->contributionConstant, &contributionPerItem) )
        {
            eaDestroyStruct(&donatedCounts, parse_ContributionItemData);
            return TRANSACTION_OUTCOME_FAILURE;
        }

        contributionTotal = contributionPerItem * contributedCount;

         // Get contribution numeric.
        contributionNumeric = GET_REF(projectDef->contributionNumeric);
        if ( ISNULL(contributionNumeric) )
        {
            eaDestroyStruct(&donatedCounts, parse_ContributionItemData);
            return TRANSACTION_OUTCOME_FAILURE;
        }

        initialContribution = inv_trh_GetNumericValue(ATR_PASS_ARGS, playerEnt, contributionNumeric->pchName);

        // Give contribution.
        inv_FillItemChangeReason(&reason, NULL, "GroupProject:DonateItems", playerEnt->debugName);
        if ( !inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, playerEnt, false, contributionNumeric->pchName, contributionTotal, NumericOp_Add, &reason) )
        {
            eaDestroyStruct(&donatedCounts, parse_ContributionItemData);
            return TRANSACTION_OUTCOME_FAILURE;
        }

        contributionGiven = inv_trh_GetNumericValue(ATR_PASS_ARGS, playerEnt, contributionNumeric->pchName) - initialContribution;

        // Get lifetime contribution numeric.
        lifetimeContributionNumeric = GET_REF(projectDef->lifetimeContributionNumeric);

        if ( NONNULL(lifetimeContributionNumeric) )
        {
            if ( !inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, playerEnt, false, lifetimeContributionNumeric->pchName, contributionTotal, NumericOp_Add, &reason) )
            {
                eaDestroyStruct(&donatedCounts, parse_ContributionItemData);
                return TRANSACTION_OUTCOME_FAILURE;
            }
        }

        if (!GroupProject_trh_RecordContributionStats(ATR_PASS_ARGS, playerEnt, projectState, contributionPerItem * contributedCount))
        {
            eaDestroyStruct(&donatedCounts, parse_ContributionItemData);
            return TRANSACTION_OUTCOME_FAILURE;
        }

        // Check for bucket completion.
        if ( bucketData->donationCount == donationRequirement->count )
        {
            taskSlot->completedBuckets++;
            bucketFilled = true;
        }

        // If all donations are completed, we can finalize the project.
		if ( taskSlot->completedBuckets == (U32)eaSize(&taskDef->buckets) )
        {
            GroupProject_trh_FinalizeTask(ATR_PASS_ARGS, taskSlot, taskDef, projectName);

            taskFinalized = true;
        }

        SendContributionNotification(ATR_PASS_ARGS, playerEnt->myEntityType, playerEnt->myContainerID, projectContainer->containerType, projectContainer->containerID,
            NULL, 0, contributedCount, initialContribution, contributionGiven, contributionNumeric->pchName, contributionPerItem, projectName, taskDef->name, bucketName, bucketFilled, taskFinalized, fullDonation, donationQueue->items, donatedCounts);

        estrClear(&temp);
        ParserWriteText(&temp, parse_ContributionItemList, donationQueue, 0, 0, TOK_NO_LOG);
        estrAppendEscaped(&requestedItemList, temp);

        estrClear(&temp);
        itemList.items = donatedCounts;
        ParserWriteText(&temp, parse_ContributionItemList, &itemList, 0, 0, TOK_NO_LOG);
        estrAppendEscaped(&donatedItemList, temp);

        TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "DonateExpressionItemList", 
            "ProjectName %s TaskSlot %d TaskName %s BucketName %s RequestedItemList \"%s\" DonatedItemList \"%s\" RequestedCount %d CountRemoved %d ContributionNumeric %s Contribution %d BucketFilled %d TaskFinalized %d", 
            projectName, taskSlot->taskSlotNum, taskDef->name, bucketName, requestedItemList, donatedItemList, requestedCount, contributedCount, contributionNumeric->pchName, contributionPerItem, bucketFilled, taskFinalized);

        estrDestroy(&donatedItemList);
        estrDestroy(&requestedItemList);
        estrDestroy(&temp);
    }

    eaDestroyStruct(&donatedCounts, parse_ContributionItemData);

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, "projectList[]");
enumTransactionOutcome
GroupProject_tr_ForceFillBucket(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int slotNum, const char *bucketName)
{
    NOCONST(GroupProjectState) *projectState;
    NOCONST(DonationTaskSlot) *taskSlot;
    NOCONST(DonationTaskBucketData) *bucketData;
    DonationTaskDef *taskDef;
    GroupProjectDef *projectDef;
    S32 donationRequireIndex;
    GroupProjectDonationRequirement *donationRequirement;

    if ( ISNULL(projectContainer) || ISNULL(projectName) || ISNULL(bucketName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Validate task slot number.
    if ( ( slotNum < 0 ) || ( slotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, slotNum);
    if ( ISNULL(taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task is in the correct state to accept donations.
    if ( taskSlot->state != DonationTaskState_AcceptingDonations )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task def.
    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the requirements.
    donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
    donationRequirement = eaGet(&taskDef->buckets, donationRequireIndex);
    if ( ISNULL(donationRequirement) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the bucket data.
    bucketData = eaIndexedGetUsingString(&taskSlot->buckets, bucketName);
    if ( ISNULL(bucketData) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Fill the bucket
    bucketData->donationCount = donationRequirement->count;

    // Check for bucket completion.
    if ( bucketData->donationCount == donationRequirement->count )
    {
        taskSlot->completedBuckets++;
    }

    // If all donations are completed, we can finalize the project.
	if ( taskSlot->completedBuckets == (U32)eaSize(&taskDef->buckets) )
    {
        GroupProject_trh_FinalizeTask(ATR_PASS_ARGS, taskSlot, taskDef, projectName);
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Projectlist");
enumTransactionOutcome
GroupProject_tr_ForceFillAllBuckets(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer)
{
    NOCONST(GroupProjectState) *projectState;
    NOCONST(DonationTaskSlot) *taskSlot;
    NOCONST(DonationTaskBucketData) *bucketData;
    DonationTaskDef *taskDef;
    GroupProjectDef *projectDef;
    GroupProjectDonationRequirement *donationRequirement;
    int i, j, k;

    if ( ISNULL(projectContainer) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    for ( i = eaSize(&projectContainer->projectList) - 1; i >= 0; i-- )
    {
        // Find the project state.
        projectState = projectContainer->projectList[i];
        if ( ISNULL(projectState) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        projectDef = GET_REF(projectState->projectDef);
        if ( ISNULL(projectDef) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        for ( j = eaSize(&projectState->taskSlots) - 1; j >= 0; j-- )
        {
            taskSlot = projectState->taskSlots[j];
            if ( ISNULL(taskSlot) )
            {
                return TRANSACTION_OUTCOME_FAILURE;
            }

            // Make sure the task is in the correct state to accept donations.
            if ( taskSlot->state != DonationTaskState_AcceptingDonations )
            {
                continue;
            }

            // Get the task def.
            taskDef = GET_REF(taskSlot->taskDef);
            if ( ISNULL(taskDef) )
            {
                return TRANSACTION_OUTCOME_FAILURE;
            }

            for ( k = eaSize(&taskDef->buckets) - 1; k >= 0; k-- )
            {
                donationRequirement = taskDef->buckets[k];
                if ( ISNULL(donationRequirement) )
                {
                    return TRANSACTION_OUTCOME_FAILURE;
                }

                // Get the bucket data.
                bucketData = eaIndexedGetUsingString(&taskSlot->buckets, donationRequirement->name);
                if ( ISNULL(bucketData) )
                {
                    return TRANSACTION_OUTCOME_FAILURE;
                }

                if ( bucketData->donationCount < donationRequirement->count )
                {
                    // Fill the bucket
                    bucketData->donationCount = donationRequirement->count;

                    // Check for bucket completion.
                    if ( bucketData->donationCount == donationRequirement->count )
                    {
                        taskSlot->completedBuckets++;
                    }

                    // If all donations are completed, we can finalize the project.
					if ( taskSlot->completedBuckets == (U32)eaSize(&taskDef->buckets) )
                    {
                        GroupProject_trh_FinalizeTask(ATR_PASS_ARGS, taskSlot, taskDef, projectDef->name);
                    }
                }
            }
        }
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, "projectList[]");
enumTransactionOutcome
GroupProject_tr_ForceSetBucketFill(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int slotNum, const char *bucketName, int count)
{
    NOCONST(GroupProjectState) *projectState;
    NOCONST(DonationTaskSlot) *taskSlot;
    NOCONST(DonationTaskBucketData) *bucketData;
    DonationTaskDef *taskDef;
    GroupProjectDef *projectDef;
    S32 donationRequireIndex;
    GroupProjectDonationRequirement *donationRequirement;

    if ( ISNULL(projectContainer) || ISNULL(projectName) || ISNULL(bucketName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Validate task slot number.
    if ( ( slotNum < 0 ) || ( slotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, slotNum);
    if ( ISNULL(taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task is in the correct state to accept donations.
    if ( taskSlot->state != DonationTaskState_AcceptingDonations )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task def.
    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the requirements.
    donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
    donationRequirement = eaGet(&taskDef->buckets, donationRequireIndex);
    if ( ISNULL(donationRequirement) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the bucket data.
    bucketData = eaIndexedGetUsingString(&taskSlot->buckets, bucketName);
    if ( ISNULL(bucketData) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Check for bucket completion.
    if ( bucketData->donationCount == donationRequirement->count )
    {
        taskSlot->completedBuckets--;
    }

    if (count > (int)donationRequirement->count)
    {
        count = donationRequirement->count;
    }

    // Set the bucket count
    bucketData->donationCount = count;

    // Check for bucket completion.
    if ( bucketData->donationCount == donationRequirement->count )
    {
        taskSlot->completedBuckets++;
    }

    // If all donations are completed, we can finalize the project.
	if ( taskSlot->completedBuckets == (U32)eaSize(&taskDef->buckets) )
    {
        GroupProject_trh_FinalizeTask(ATR_PASS_ARGS, taskSlot, taskDef, projectName);
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, "projectList[]")
ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2.Ppinventorybags, .Psaved.Ppallowedcritterpets, .Pplayer.Pugckillcreditlimit, .Psaved.pSCPdata.Isummonedscp, .Psaved.Pscpdata.Erscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .Pinventoryv2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid")
ATR_LOCKS(pets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems");
enumTransactionOutcome
GroupProject_tr_ClaimRewards(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, NOCONST(Entity) *pEnt, 
    CONST_EARRAY_OF(NOCONST(Entity)) pets, GiveRewardBagsData* rewards, const ItemChangeReason *reason, GameAccountDataExtract* extract,
    const char *projectName, int slotNum)
{
    NOCONST(GroupProjectState) *projectState;
    NOCONST(DonationTaskSlot) *taskSlot;
    DonationTaskDef *taskDef;
    GroupProjectDef *projectDef;

    if ( ISNULL(projectContainer) || ISNULL(projectName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Validate task slot number.
    if ( ( slotNum < 0 ) || ( slotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, slotNum);
    if ( ISNULL(taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task is in the correct state to claim rewards.
    if ( taskSlot->state != DonationTaskState_RewardPending )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task def.
    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Attempt to give the player the rewards
    if (!inv_trh_GiveRewardBags(ATR_PASS_ARGS, pEnt, pets, rewards, kRewardOverflow_DisallowOverflowBag, NULL, reason, extract, NULL))
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Set the state indicating that the reward has been claimed.
    taskSlot->state = DonationTaskState_RewardClaimed;

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, "projectList[]");
enumTransactionOutcome
GroupProject_tr_CancelProject(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int slotNum)
{
    NOCONST(GroupProjectState) *projectState;
    NOCONST(DonationTaskSlot) *taskSlot;
    DonationTaskDef *taskDef;
    GroupProjectDef *projectDef;

    if ( ISNULL(projectContainer) || ISNULL(projectName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Validate task slot number.
    if ( ( slotNum < 0 ) || ( slotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, slotNum);
    if ( ISNULL(taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task is in the correct state to claim rewards.
    if ( taskSlot->state != DonationTaskState_AcceptingDonations )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task def.
    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    if ( !taskDef->cancelable )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    GroupProject_trh_CancelTask(ATR_PASS_ARGS, taskSlot, taskDef, projectName);

    return TRANSACTION_OUTCOME_SUCCESS;
}