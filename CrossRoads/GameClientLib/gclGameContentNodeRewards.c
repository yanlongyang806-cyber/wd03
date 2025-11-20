/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "LobbyCommon.h"
#include "stdtypes.h"
#include "UIGen.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "inventoryCommon.h"
#include "rewardCommon.h"
#include "GlobalStateMachine.h"
#include "progression_common.h"
#include "gclBaseStates.h"
#include "gclLogin.h"
#include "net.h"
#include "structNet.h"
#include "GlobalComm.h"

#include "AutoGen/LobbyCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

static GameContentNodeRewardResult s_lastGameContentRewardResult;

// This gen expression is designed to support one group at a time. If used for more than one group
// in the same frame you would get the results for the most recently queried.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Lobby_GetGameContentNodeRewards);
void gclLobby_GetGameContentNodeRewards(SA_PARAM_NN_VALID UIGen *pGen,  SA_PARAM_OP_STR const char *pchGameContentNodeName)
{
	Item ***peaItemList = ui_GenGetManagedListSafe(pGen, Item);	

    S32 iCount = 0;

    if (pchGameContentNodeName && pchGameContentNodeName[0])
    {
        GameContentNodeRef *pRef = gameContentNode_GetRefFromName(pchGameContentNodeName);

        if (pRef)
        {
            if (gameContentNode_RefsAreEqual(&s_lastGameContentRewardResult.nodeRef, pRef))
            {
                // Display the stored result
                S32 i;
                for (i = 0; i < eaSize(&s_lastGameContentRewardResult.eaMissionRewards); i++)
                {
                    GameContentNodeMissionReward *pMissionReward = s_lastGameContentRewardResult.eaMissionRewards[i];

                    FOR_EACH_IN_CONST_EARRAY_FORWARDS(pMissionReward->eaRewardBags, InventoryBag, pBag)
                    {
                        if (pBag->pRewardBagInfo->PickupType != kRewardPickupType_Choose)
                        {
                            iCount += inv_bag_GetSimpleItemListAndReUseArrayElements(pBag, peaItemList, true, iCount);
                        }
                    }
                    FOR_EACH_END
                }
            }
            else
            {
                bool bAskServer = true;
                if (IS_HANDLE_ACTIVE(pRef->hNode))
                {
                    GameProgressionNodeDef *pProgressionNodeDef = GET_REF(pRef->hNode);
                    if (pProgressionNodeDef == NULL || 
                        pProgressionNodeDef->pMissionGroupInfo == NULL ||
                        !pProgressionNodeDef->pMissionGroupInfo->bShowRewardsInUI)
                    {
                        bAskServer = false;
                    }
                }

                // Clear the results
                StructReset(parse_GameContentNodeRewardResult, &s_lastGameContentRewardResult);

                // Set the node ref again
                StructCopyAll(parse_GameContentNodeRef, pRef, &s_lastGameContentRewardResult.nodeRef);

                if (bAskServer)
                {
                    // Ask the server about the rewards for this content node
                    COPY_HANDLE(s_lastGameContentRewardResult.nodeRef.hNode, pRef->hNode);
                    s_lastGameContentRewardResult.nodeRef.iUGCProjectID = pRef->iUGCProjectID;

                    if (GSM_IsStateActive(GCL_GAMEPLAY))
                    {
                        ServerCmd_LobbyCommand_GetMissionRewards(&s_lastGameContentRewardResult.nodeRef);
                    }
                    else
                    {
                        Packet *pPak = pktCreate(gpLoginLink, TOLOGIN_REQUEST_GAME_CONTENT_NODE_REWARDS);

                        pktSendStruct(pPak, &s_lastGameContentRewardResult.nodeRef, parse_GameContentNodeRef);

                        // Send the request to the login server
                        pktSend(&pPak);
                    }
                }
            }

            StructDestroy(parse_GameContentNodeRef, pRef);
        }	
    }

    eaSetSizeStruct(peaItemList, parse_Item, iCount);

    ui_GenSetManagedListSafe(pGen, peaItemList, Item, true);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(LobbyCommand_ReceiveGameContentNodeRewards) ACMD_PRIVATE ACMD_CLIENTCMD;
void gclLobby_cmdReceiveGameContentNodeRewards(GameContentNodeRewardResult *pResult)
{
    // Make sure the result matches what has been requested most recently
    if (gameContentNode_RefsAreEqual(&pResult->nodeRef, &s_lastGameContentRewardResult.nodeRef))
    {
        eaClearStruct(&s_lastGameContentRewardResult.eaMissionRewards, parse_GameContentNodeMissionReward);
        eaCopyStructs(&pResult->eaMissionRewards, &s_lastGameContentRewardResult.eaMissionRewards, parse_GameContentNodeMissionReward);
    }
}

void gclLobbyHandleGameContentNodeRewardsPacket(Packet *pak)
{
    GameContentNodeRewardResult *pResult = StructCreate(parse_GameContentNodeRewardResult);
    ParserRecv(parse_GameContentNodeRewardResult, pak, pResult, 0);

    // Make sure the result matches what has been requested most recently
    if (gameContentNode_RefsAreEqual(&pResult->nodeRef, &s_lastGameContentRewardResult.nodeRef))
    {
        eaClearStruct(&s_lastGameContentRewardResult.eaMissionRewards, parse_GameContentNodeMissionReward);
        eaCopyStructs(&pResult->eaMissionRewards, &s_lastGameContentRewardResult.eaMissionRewards, parse_GameContentNodeMissionReward);
    }

    StructDestroy(parse_GameContentNodeRewardResult, pResult);
}
