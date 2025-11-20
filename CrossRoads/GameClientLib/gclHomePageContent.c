/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

/************************************************************************/
/* Home page content stuff                                              */
/************************************************************************/

#include "LobbyCommon.h"
#include "stdtypes.h"
#include "UIGen.h"

#include "AutoGen/LobbyCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

static HomePageContentInfo *s_pHomePageContentInfo;

// Sets the list of root level content in the given gen
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Lobby_GetRecommendedContentForHomePage);
void gclLobby_GetRecommendedContentForHomePage(SA_PARAM_NN_VALID UIGen *pGen)
{
    PlayerSpecificRecommendedContent ***peaContentList = ui_GenGetManagedListSafe(pGen, PlayerSpecificRecommendedContent);	

    S32 iCount = 0;

    if (s_pHomePageContentInfo)
    {
        // Do the pass for all results
        FOR_EACH_IN_EARRAY_FORWARDS(s_pHomePageContentInfo->ppPlayerSpecificRecommendedContent, PlayerSpecificRecommendedContent, pContent)
        {
            PlayerSpecificRecommendedContent *pNewContent = eaGetStruct(peaContentList, parse_PlayerSpecificRecommendedContent, iCount++);
            StructCopyAll(parse_PlayerSpecificRecommendedContent, pContent, pNewContent);
        }
        FOR_EACH_END
    }

    eaSetSizeStruct(peaContentList, parse_PlayerSpecificRecommendedContent, iCount);

    ui_GenSetManagedListSafe(pGen, peaContentList, PlayerSpecificRecommendedContent, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Lobby_IsHomePageContentLoading);
bool gclLobby_IsHomePageContentLoading(void)
{
    return s_pHomePageContentInfo && s_pHomePageContentInfo->uDataGenerationTimestamp == 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Lobby_RequestHomePageContent);
void gclLobby_RequestHomePageContent(void)
{
    if (s_pHomePageContentInfo == NULL || // We never received home page content
        timeSecondsSince2000() - s_pHomePageContentInfo->uDataGenerationTimestamp > 5) // We did not receive home page content in the last 5 seconds
    {
        if (s_pHomePageContentInfo)
        {
            // Reset the data we store
            StructReset(parse_HomePageContentInfo, s_pHomePageContentInfo);
        }
        else
        {
            s_pHomePageContentInfo = StructCreate(parse_HomePageContentInfo);
        }

        // Ask the game server for the home page content
        ServerCmd_gslLobby_GetHomePageContent();
    }
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclLobbyReceiveHomePageContentInfo(HomePageContentInfo *pHomePageContentInfo)
{
    if (s_pHomePageContentInfo == NULL)
    {
        s_pHomePageContentInfo = StructClone(parse_HomePageContentInfo, pHomePageContentInfo);
    }
    else
    {
        StructCopyAll(parse_HomePageContentInfo, pHomePageContentInfo, s_pHomePageContentInfo);
    }

    // Store the last time we've received this data
    s_pHomePageContentInfo->uDataGenerationTimestamp = timeSecondsSince2000();
}

