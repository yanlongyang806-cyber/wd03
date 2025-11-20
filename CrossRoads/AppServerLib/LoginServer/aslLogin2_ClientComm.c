/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "aslLogin2_ClientComm.h"
#include "aslLogin2_StateMachine.h"
#include "aslLogin2_Error.h"
#include "Login2Common.h"
#include "GlobalComm.h"
#include "aslLoginServer.h"
#include "StringCache.h"
#include "Message.h"
#include "AppLocale.h"
#include "error.h"
#include "file.h"
#include "timing_profiler.h"
#include "InstancedStateMachine.h"
#include "structNet.h"
#include "NotifyEnum.h"
#include "MapDescription.h"
#include "ResourceManager.h"
#include "NameGen.h"
#include "ugcprojectcommon.h"
#include "aslLoginUGCProject.h"
#include "aslLoginCStore.h"
#include "accountnet.h"
#include "LoginCommon.h"
#include "ShardCommon.h"
#include "utilitiesLib.h"
#include "MicroTransactions.h"
#include "LoadScreen/LoadScreen_Common.h"
#include "GamePermissionsCommon.h"

#include "AutoGen/aslLogin2_ClientComm_h_ast.h"
#include "AutoGen/Login2Common_h_ast.h"
#include "AutoGen/LoginCommon_h_ast.h"
#include "AutoGen/Login2CharacterDetail_h_ast.h"
#include "AutoGen/MapDescription_h_ast.h"
#include "AutoGen/NameGen_h_ast.h"
#include "AutoGen/ugcprojectcommon_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/LoadScreen_Common_h_ast.h"
#include "AutoGen/GamePermissionsCommon_h_ast.h"

extern bool g_isContinuousBuilder;
extern LoadScreenDynamic gLoadScreens;

bool g_Login2PrintFailures = false;
AUTO_CMD_INT(g_Login2PrintFailures, Login2PrintFailures) ACMD_CMDLINE;

void
aslLogin2_ConfigureClientLoginNetLink(NetLink *netLink)
{
    linkSetTimeout(netLink, CLIENT_LINK_TIMEOUT);

    if (aslLoginServerClientsAreUntrustworthy())
    {
        linkSetIsNotTrustworthy(netLink, true);
    }

}

void 
aslLogin2_SendLoginInfo(Login2State *loginState)
{
    if (loginState->netLink && linkConnected(loginState->netLink))
    {
        const char *pchTerms = aslLogin2_GetAccountValuesFromKeyTemp(loginState, TERMS_OF_USE_KEY);
        Packet *pak = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_LOGIN_INFO);
        LoginData data = {0};
        data.uAccountID = loginState->accountID;
        data.iAccessLevel = loginState->clientAccessLevel;
        data.uAccountPlayerType = loginState->playerType;
        data.pAccountName = loginState->accountName;
        data.pPwAccountName = loginState->pweAccountName;
        data.pDisplayName = loginState->accountDisplayName;
        data.pShardInfoString = GetShardInfoString();
        data.pShardClusterName = ShardCommon_GetClusterName();
		data.pUgcShardName = ugc_ShardName();
        data.uiLastTermsOfUse = pchTerms ? _atoi64(pchTerms) : 0;
        data.uiServerTimeSS2000 = timeSecondsSince2000();
        pktSendStruct(pak, &data, parse_LoginData);
        pktSend(&pak);

        pak = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_MICROTRANSACTION_CATEGORY);
        pktSendU32(pak, g_eMicroTrans_ShardCategory);
        pktSend(&pak);

        // send load screens here
        if(eaSize(&gLoadScreens.esLoadScreens) > 0)
        {
            Packet *pPakLoadScreen = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_LOADING_SCREENS);
            ParserSend(parse_LoadScreenDynamic, pPakLoadScreen, NULL, &gLoadScreens, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
            pktSend(&pPakLoadScreen);
        }
    }
}

void
aslLogin2_SendCharacterSelectionData(Login2State *loginState)
{
    Packet *pPak;

    pPak = pktCreate(loginState->netLink, LOGIN2_TO_CLIENT_CHARACTER_SELECTION_DATA);
    if ( loginState->isProxy )
    {
        pktSendStructJSON(pPak, loginState->characterSelectionData, parse_Login2CharacterSelectionData);
    }
    else
    {
        ParserSend(parse_Login2CharacterSelectionData, pPak, NULL, loginState->characterSelectionData, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
    }
    pktSend(&pPak);
}

void
aslLogin2_SendCharacterDetail(Login2State *loginState, Login2CharacterDetail *characterDetail)
{
    Packet *pPak;

    pPak = pktCreate(loginState->netLink, LOGIN2_TO_CLIENT_CHARACTER_DETAIL);
    ParserSend(parse_Login2CharacterDetail, pPak, NULL, characterDetail, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
    pktSend(&pPak);
}

void
aslLogin2_SendShardLockedNotification(Login2State *loginState)
{
    Packet *pak;

    pak = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_NOTIFYSEND);
    pktSendU32(pak, kNotifyType_ServerBroadcast);
    pktSendString(pak, "THE SHARD IS LOCKED!\nTHE SHARD IS LOCKED!\nTHE SHARD IS LOCKED!\n\nIf you are seeing this, it is because you are a special\ninternal person of some sort playing on a locked shard.\nIf you do not think the shard should be locked,\nPANIC!!! For the love of god, PANIC!!!!\n\nAlso, talk to Netops\n");
    pktSendString(pak, NULL);
    pktSendString(pak, NULL);
    pktSend(&pak);
}

void
aslLogin2_SendQueueUpdate(Login2State *loginState, int queuePosition, int queueSize)
{
    Packet *pkt;

    pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_HEARTBEAT);
    pktSendU32(pkt, queuePosition);
    // Make sure we don't report a size that is smaller than the position we are reporting.
    pktSendU32(pkt, queueSize < queuePosition ? queuePosition : queueSize);
    pktSend(&pkt);
}

void
aslLogin2_SendMapChoices(Login2State *loginState, PossibleMapChoices *possibleMapChoices)
{
    Packet *pkt;

    pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_POSSIBLE_GAMESERVERS);
    ParserSend(parse_PossibleMapChoices, pkt, NULL, possibleMapChoices, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
    pktSend(&pkt);
}

void
aslLogin2_SendGameserverAddress(Login2State *loginState, ReturnedGameServerAddress *gameserverAddress)
{
    Packet *pkt;
    pkt = pktCreate(loginState->netLink, TOCLIENT_GAME_SERVER_ADDRESS);
    pktSendBits(pkt, 1, 1);
    ParserSend(parse_ReturnedGameServerAddress, pkt, NULL, gameserverAddress, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
    pktSend(&pkt);
}

void
aslLogin2_SendCharacterCreationData(Login2State *loginState, CharacterCreationDataHolder *dataHolder)
{
    Packet *pkt;
    pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_CHARACTER_CREATION_DATA);
    ParserSend(parse_CharacterCreationDataHolder, pkt, NULL, dataHolder, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
    pktSend(&pkt);
}

void
aslLogin2_SendNewCharacterID(Login2State *loginState, ContainerID newCharacterID)
{
    Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_NEWLY_CREATED_CHARACTER_ID);
    PutContainerIDIntoPacket(pkt, newCharacterID);
    pktSend(&pkt);
}

void
aslLogin2_SendPossibleUGCProjects(Login2State *loginState, PossibleUGCProjects *possibleUGCProjects)
{
    Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_POSSIBLE_UGC_PROJECTS);
    ParserSendStruct(parse_PossibleUGCProjects, pkt, possibleUGCProjects);
    pktSend(&pkt);
}

void
aslLogin2_SendPossibleUGCImports(Login2State *loginState, PossibleUGCProjects *possibleUGCImports)
{
    Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_UGC_IMPORT_PROJECT_SEARCH);
    ParserSendStruct(parse_PossibleUGCProjects, pkt, possibleUGCImports);
    pktSend(&pkt);
}

void
aslLogin2_SendReviews(Login2State *loginState, ContainerID projectID, ContainerID seriesID, S32 pageNumber, const UGCProjectReviews *ugcReviews)
{
    Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_UGC_REQUEST_MORE_REVIEWS);

    pktSendU32(pkt, projectID);
	pktSendU32(pkt, seriesID);
    pktSendU32(pkt, pageNumber);
    ParserSendStruct(parse_UGCProjectReviews, pkt, ugcReviews );
    pktSend(&pkt);
}

void
aslLogin2_SendUGCProjectSeriesCreateResult(Login2State *loginState, ContainerID seriesID)
{
    Packet* pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_UGCPROJECTSERIES_CREATE_RESULT);
    // Send the new ContainerID
    pktSendU32(pkt, seriesID);
    pktSend(&pkt);
}

void
aslLogin2_SendUGCProjectSeriesUpdateResult(Login2State *loginState, bool success, const char *errorMsg)
{
    Packet* pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_UGCPROJECTSERIES_UPDATE_RESULT);
    pktSendBool(pkt, success);
    pktSendString(pkt, errorMsg);
    pktSend(&pkt);
}

void
aslLogin2_SendUGCSearchResults(Login2State *loginState, UGCSearchResult *searchResult)
{
    Packet *pkt;
    pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_UGC_SEARCH_RESULTS);
    pktSendStruct(pkt, searchResult, parse_UGCSearchResult);
    pktSend(&pkt);
}

void
aslLogin2_SendUGCProjectRequestByIDResults(Login2State *loginState, UGCProjectList *projectList)
{
    Packet *pkt;
    pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_UGC_PROJECT_REQUEST_BY_ID_RESULTS);
    pktSendStruct(pkt, projectList, parse_UGCProjectList);
    pktSend(&pkt);
}

void
aslLogin2_SendUGCNumAheadInQueue(Login2State *loginState, U32 numAhead)
{
    Packet *pkt;
    pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_HERE_IS_NUM_AHEAD_OF_YOU_IN_EDIT_QUEUE);
    pktSendBits(pkt, 32, numAhead);
    pktSend(&pkt);
}

void
aslLogin2_SendUGCEditPermissionCookie(Login2State *loginState, U32 queueCookie)
{
    Packet *pkt;
    pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_HERE_IS_EDIT_PERMISSION_COOKIE);
    pktSendBits(pkt, 32, queueCookie);
    pktSend(&pkt);
}

void
aslLogin2_SendClientRedirect(Login2State *loginState, U32 redirectIP, U32 redirectPort, U64 transferCookie)
{
    Packet *pkt;
    pkt = pktCreate(loginState->netLink, LOGIN2_TO_CLIENT_REDIRECT);
    pktSendBool(pkt, true);
    pktSendU32(pkt, loginState->accountID);
    pktSendU32(pkt, redirectIP);
    pktSendU32(pkt, redirectPort);
    pktSendU64(pkt, transferCookie);
    pktSend(&pkt);
}

void
aslLogin2_SendClientNoRedirect(Login2State *loginState)
{
    Packet *pkt;
    pkt = pktCreate(loginState->netLink, LOGIN2_TO_CLIENT_REDIRECT);
    pktSendBool(pkt, false);
    pktSend(&pkt);
}

void
aslLogin2_SendRedirectDone(Login2State *loginState)
{
    Packet *pkt;
    pkt = pktCreate(loginState->netLink, LOGIN2_TO_CLIENT_REDIRECT_DONE);
    pktSendBool(pkt, loginState->requestedUGCEdit);
    pktSend(&pkt);
}

void
aslLogin2_SendClusterStatus(Login2State *loginState, Login2ClusterStatus *clusterStatus)
{
    Packet *pkt;
    pkt = pktCreate(loginState->netLink, LOGIN2_TO_CLIENT_CLUSTER_STATUS);
    pktSendStruct(pkt, clusterStatus, parse_Login2ClusterStatus);
    pktSend(&pkt);
}

void
aslLogin2_SendGamePermissions(Login2State*loginState)
{
    Packet *pkt;
    // Send gamepermissions down to the client immediately.
    pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_GAMEPERMISSIONDEFS);
    pktSendStruct(pkt, &g_GamePermissions, parse_GamePermissionDefs);
    pktSend(&pkt);
}

void
aslLogin2_FailLogin(Login2State *loginState, const char *errorMessageKey)
{
    const char *errorString = NULL;
    loginState->loginFailed = true;

    // Get english version of error string for logging, builder and dev errors.
    errorString  = langTranslateMessageKey(LANGUAGE_ENGLISH, (char *)errorMessageKey);
    if ( errorString == NULL || errorString[0] == 0 )
    {
        errorString = errorMessageKey;
    }

    if ( g_Login2PrintFailures )
    {
        printf("Login failed.  Reason: %s\n", errorString);
    }

    if (g_isContinuousBuilder)
    {
        assertmsgf(0, "Login failed. Reason: %s\n", errorString);
    }

    // Make it so login failures are easier to understand in development
    if (isDevelopmentMode()) 
    {
        Errorf("Login failed.  Reason: %s", errorString);
    }

    if (loginState->netLink)
    {
        Packet *pak = pktCreate(loginState->netLink, LOGIN2_TO_CLIENT_LOGIN_FAILED);

        pktSendString(pak, errorMessageKey);
        pktSend(&pak);

        aslLogin2_Log("LoginFailure: accountID=%u, error=%s", loginState->accountID, errorString);

        linkFlushAndClose(&loginState->netLink, "Login Failed");
    }
    else
    {
        aslLogin2_Log("LoginFailure", "%s (NO LINK)", errorString);
    }
}

BeginLoginPacketData * 
aslLogin2_ReadBeginLoginPacket(Packet *pak)
{
    char *accountTicketString;
    BeginLoginPacketData *beginLoginPacketData = StructCreate(parse_BeginLoginPacketData);

    PERFINFO_AUTO_START_FUNC();

    beginLoginPacketData->noTimeout = pktGetBits(pak,1);
    beginLoginPacketData->clientLanguageID = pktGetU32(pak);

    accountTicketString = pktGetStringTemp(pak);
    if (accountTicketString[0] != '\r' && accountTicketString[0] != '\n' && accountTicketString[0] != '{')
    {
        // Client is just sending an account name.  Is this builder only?
        beginLoginPacketData->accountName = StructAllocString(accountTicketString);
    }
    else if (stricmp(accountTicketString, ACCOUNT_FASTLOGIN_LABEL) == 0)
    {
        // Regular login flow.  
        beginLoginPacketData->accountID = pktGetU32(pak);
        beginLoginPacketData->ticketID = pktGetU32(pak);
        beginLoginPacketData->machineID = pktMallocString(pak);
    }

    // The CRC of the client executable.
    beginLoginPacketData->clientCRC = pktGetU32(pak);

    beginLoginPacketData->affiliate = allocAddString(pktGetStringTemp(pak));

    PERFINFO_AUTO_STOP_FUNC();

    return beginLoginPacketData;
}

RedirectLoginPacketData *
aslLogin2_ReadRedirectLoginPacket(Packet *pak)
{
    RedirectLoginPacketData *packetData = StructCreate(parse_RedirectLoginPacketData);
    packetData->accountID = pktGetU32(pak);
    packetData->transferCookie = pktGetU64(pak);
    packetData->noTimeout = pktGetBool(pak);
    return packetData;
}

SaveNextMachinePacketData *
aslLogin2_ReadSaveNextMachinePacket(Packet *pak)
{
    SaveNextMachinePacketData *packetData = StructCreate(parse_SaveNextMachinePacketData);
    packetData->machineName = pktMallocString(pak);

    return packetData;
}

OneTimeCodePacketData *
aslLogin2_ReadOneTimeCodePacket(Packet *pak)
{
    OneTimeCodePacketData *packetData = StructCreate(parse_OneTimeCodePacketData);
    packetData->oneTimeCode = pktMallocString(pak);
    packetData->machineName = pktMallocString(pak);

    return packetData;
}

RequestCharacterDetailPacketData *
aslLogin2_ReadRequestCharacterDetailPacket(Packet *pak)
{
    RequestCharacterDetailPacketData *packetData;
    packetData = StructCreate(parse_RequestCharacterDetailPacketData);
    packetData->characterID = pktGetU32(pak);

    return packetData;
}

ChooseCharacterPacketData *
aslLogin2_ReadChooseCharacterPacket(Packet *pak)
{
    ChooseCharacterPacketData *packetData;
    packetData = StructCreate(parse_ChooseCharacterPacketData);
    packetData->characterID = pktGetU32(pak);
    packetData->UGCEdit = pktGetBool(pak);

    return packetData;
}

DeleteCharacterPacketData *
aslLogin2_ReadDeleteCharacterPacket(Packet *pak)
{
    DeleteCharacterPacketData *packetData;
    packetData = StructCreate(parse_DeleteCharacterPacketData);
    packetData->characterID = pktGetU32(pak);

    return packetData;
}

RenameCharacterPacketData *
aslLogin2_ReadRenameCharacterPacket(Packet *pak)
{
    RenameCharacterPacketData *packetData;
    packetData = StructCreate(parse_RenameCharacterPacketData);
    packetData->characterID = pktGetU32(pak);
    packetData->badName = pktGetBool(pak);
    packetData->newName = pktMallocString(pak);

    return packetData;
}

CreateCharacterPacketData *
aslLogin2_ReadCreateCharacterPacket(Packet *pak)
{
    CreateCharacterPacketData *packetData;
    Login2CharacterCreationData *characterCreationData;

    characterCreationData = StructCreate(parse_Login2CharacterCreationData);

    if (!ParserRecv(parse_Login2CharacterCreationData, pak, characterCreationData, aslLoginServerClientsAreUntrustworthy() ? RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE : 0))
    {
        StructDestroy(parse_Login2CharacterCreationData, characterCreationData);
        return NULL;
    }

    packetData = StructCreate(parse_CreateCharacterPacketData);
    packetData->characterCreationData = characterCreationData;
    packetData->UGCEdit = pktGetBool(pak);

    return packetData;
}

MapSearchInfo *
aslLogin2_ReadRequestMapSearchPacket(Packet *pak)
{
    MapSearchInfo *packetData;
    packetData = StructCreate(parse_MapSearchInfo);

    if (!ParserRecv(parse_MapSearchInfo, pak, packetData, aslLoginServerClientsAreUntrustworthy() ? RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE : 0))
    {
        StructDestroy(parse_MapSearchInfo, packetData);
        return NULL;
    }

    return packetData;
}

PossibleMapChoice *
aslLogin2_ReadRequestGameserverAddress(Packet *pak)
{
    PossibleMapChoice *possibleMapChoice;

    possibleMapChoice = StructCreate(parse_PossibleMapChoice);
    if (!ParserRecv(parse_PossibleMapChoice, pak, possibleMapChoice, aslLoginServerClientsAreUntrustworthy() ? RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE : 0))
    {
        StructDestroy(parse_PossibleMapChoice, possibleMapChoice);
        return NULL;
    }

    return possibleMapChoice;
}

PossibleUGCProject *
aslLogin2_ReadPossibleUGCProject(Packet *pak)
{
    PossibleUGCProject * possibleUGCProject = StructCreate(parse_PossibleUGCProject);
    if (!ParserRecv(parse_PossibleUGCProject, pak, possibleUGCProject, aslLoginServerClientsAreUntrustworthy() ? RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE : 0))
    {
        return NULL;
    }

    return possibleUGCProject;
}

bool
aslLogin2_FailIfNotInState(Login2State *loginState, char *stateString)
{
    if (!ISM_IsStateActiveOrPending(LOGIN2_STATE_MACHINE, loginState, stateString))
    {
        aslLogin2_FailLogin(loginState, "Login2_WrongState");
        return false;
    }

    return true;
}

bool
LoginFailIfNotInState2(Login2State *loginState, char *stateString, char *stateString2)
{
    if (!ISM_IsStateActiveOrPending(LOGIN2_STATE_MACHINE, loginState, stateString) &&
        !ISM_IsStateActiveOrPending(LOGIN2_STATE_MACHINE, loginState, stateString2))
    {
        aslLogin2_FailLogin(loginState, "Login2_WrongState");
        return false;
    }

    return true;
}

void 
aslLogin2_HandleInput(Packet* pak, int cmd, NetLink* netLink, Login2State *loginState)
{
    switch (cmd)
    {
    case TOLOGIN_LOGIN2_BEGIN_LOGIN:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_INITIAL_CONNECTION) )
        {
            devassert(loginState->beginLoginPacketData == NULL );
            loginState->beginLoginPacketData = aslLogin2_ReadBeginLoginPacket(pak);
        }
        break;
    case TOLOGIN_LOGIN2_REDIRECT:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_INITIAL_CONNECTION) )
        {
            devassert(loginState->redirectLoginPacketData == NULL );
            loginState->redirectLoginPacketData = aslLogin2_ReadRedirectLoginPacket(pak);
        }
        break;
    case TOLOGIN_LOGIN2_SAVENEXTMACHINE:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_GET_MACHINE_NAME_FROM_CLIENT) )
        {
            devassert(loginState->saveNextMachinePacketData == NULL);
            loginState->saveNextMachinePacketData = aslLogin2_ReadSaveNextMachinePacket(pak);
        }
        break;
    case TOLOGIN_LOGIN2_ONETIMECODE:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_GENERATE_ONE_TIME_CODE) )
        {
            devassert(loginState->oneTimeCodePacketData == NULL);
            loginState->oneTimeCodePacketData = aslLogin2_ReadOneTimeCodePacket(pak);
        }
        break;
    case TOLOGIN_LOGIN2_REQUESTCHARACTERDETAIL:
        // If we are not in the character select state, then ignore detail request.
        if ( ISM_IsStateActiveOrPending(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_CHARACTER_SELECT) )
        {
            // If there is already a pending request, get rid of it and replace it with the new one.
            if ( loginState->requestCharacterDetailPacketData != NULL )
            {
                Errorf("Dropping request for characterdetail because a new one arrived.");
                StructDestroy(parse_RequestCharacterDetailPacketData, loginState->requestCharacterDetailPacketData);
            }
            loginState->requestCharacterDetailPacketData = aslLogin2_ReadRequestCharacterDetailPacket(pak);
        }
        break;
    case TOLOGIN_LOGIN2_CHOOSECHARACTER:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_CHARACTER_SELECT) )
        {
            devassert(loginState->chooseCharacterPacketData == NULL);
            loginState->chooseCharacterPacketData = aslLogin2_ReadChooseCharacterPacket(pak);
        }
        break;
    case TOLOGIN_LOGIN2_REQUESTMAPSEARCH:
        if ( loginState->requestedMapSearch )
        {
            StructDestroy(parse_MapSearchInfo, loginState->requestedMapSearch);
        }
        loginState->requestedMapSearch = aslLogin2_ReadRequestMapSearchPacket(pak);
        break;
    case TOLOGIN_LOGIN2_REQUESTGAMESERVERADDRESS:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_GET_MAP_CHOICES) )
        {
            devassert(loginState->requestedGameserver == NULL);
            loginState->requestedGameserver = aslLogin2_ReadRequestGameserverAddress(pak);
        }
        break;
    case TOLOGIN_LOGIN2_DELETE_CHARACTER:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_CHARACTER_SELECT) )
        {
            // If a pending delete request already exists, then ignore this one.
            if ( loginState->deleteCharacterPacketData == NULL )
            {
                loginState->deleteCharacterPacketData = aslLogin2_ReadDeleteCharacterPacket(pak);
            }
        }
        break;
    case TOLOGIN_LOGIN2_RENAME_CHARACTER:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_CHARACTER_SELECT) )
        {
            // If a pending rename request already exists, then ignore this one.
            if ( loginState->renameCharacterPacketData == NULL )
            {
                loginState->renameCharacterPacketData = aslLogin2_ReadRenameCharacterPacket(pak);
            }
        }
        break;
    case TOLOGIN_LOGIN2_SEND_REFDICT_DATA_REQUESTS:
        resServerProcessClientRequests(pak, loginState->resourceCache);
        break;
    case TOLOGIN_LOGIN2_CREATECHARACTER:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_CHARACTER_SELECT) )
        {
            // If a pending creation request already exists, then ignore this one.
            if ( loginState->createCharacterPacketData == NULL )
            {
                loginState->createCharacterPacketData = aslLogin2_ReadCreateCharacterPacket(pak);
            }
        }
        break;
    case TOLOGIN_LOGIN2_GET_CHARACTER_CREATION_DATA:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_CHARACTER_SELECT) )
        {
            CharacterCreationDataHolder *dataHolder;
            GameAccountData *gameAccountData = GET_REF(loginState->hGameAccountData);

            dataHolder = StructCreate(parse_CharacterCreationDataHolder);

            aslGetCharacterCreationData(gameAccountData, loginState->playerType, loginState->accountID, dataHolder);

            aslLogin2_SendCharacterCreationData(loginState, dataHolder);

            StructDestroy(parse_CharacterCreationDataHolder,dataHolder);
        }
        break;
    case TOLOGIN_REQUEST_RANDOM_NAMES:
        {
            Packet *pak_out;
            GenNameList *nameList = NULL;
            GenNameListReq *nameReq = StructCreate(parse_GenNameListReq);
            ParserRecv(parse_GenNameListReq, pak, nameReq, aslLoginServerClientsAreUntrustworthy() ? RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE : 0);
            nameList = nameGen_GenerateRandomNamesInternal(nameReq);
            pak_out = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_RANDOM_NAMES);
            ParserSendStruct(parse_GenNameList, pak_out, nameList);
            pktSend(&pak_out);
            StructDestroy(parse_GenNameList, nameList);
            StructDestroy(parse_GenNameListReq, nameReq);
        }
        break;

    case TOLOGIN_UGC_REQUEST_MORE_REVIEWS:
        aslLoginHandleRequestReviewsForPage(pak, loginState);
        break;

    case TOLOGIN_CHOOSE_UGC_PROJECT:
        if ( aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT) )
        {
            // Ignore the request if we already have a request to edit.
            if ( loginState->chosenUGCProjectForEdit == NULL )
            {
                loginState->chosenUGCProjectForEdit = aslLogin2_ReadPossibleUGCProject(pak);
                if ( loginState->chosenUGCProjectForEdit == NULL )
                {
                    aslLogin2_FailLogin(loginState, "Login2_InvalidProjectRequest");
                    return;
                }

            }
        }
        break;

    case TOLOGIN_ACCEPTED_UGC_PROJECT_CREATE_EULA:
        aslLoginHandleAcceptedUGCCreateProjectEULA(pak, loginState);
        break;

    case TOLOGIN_DELETE_UGC_PROJECT:
        aslLoginHandleDestroyUGCProject(pak, loginState);
        break;

    case TOLOGIN_READY_TO_CHOOSE_UGC_PROJECT:
        aslLoginHandleReadyToChooseUGCProject(pak, loginState);	
        break;

    case TOLOGIN_UGCPROJECTSERIES_CREATE:
        aslLoginUGCProjectSeriesCreate(pak, loginState);
        break;

    case TOLOGIN_UGCPROJECTSERIES_DESTROY:
        aslLoginUGCProjectSeriesDestroy(pak, loginState);
        break;

    case TOLOGIN_UGCPROJECTSERIES_UPDATE:
        aslLoginUGCProjectSeriesUpdate(pak, loginState);
        break;

    case TOLOGIN_UGC_SET_PLAYER_IS_REVIEWER:
        aslLoginUGCSetPlayerIsReviewer(pak, loginState);
        break;

    case TOLOGIN_UGC_SET_SEARCH_EULA_ACCEPTED:
        aslLoginUGCSetSearchEULAAccepted(pak, loginState);
        break;

    case TOLOGIN_UGC_PROJECT_REQUEST_BY_ID:
        aslLoginUGCProjectRequestByID(pak, loginState);
        break;

    case TOLOGIN_CSTORE_ACTION:
        HandleCStoreAction(loginState, pak);
        break;

    case TOLOGIN_STEAM_MICROTXN_AUTHZ_RESPONSE:
        aslLoginHandleSteamPurchase(loginState, pak);
        break;

    case TOLOGIN_SET_REQUEST_IP:
        if(loginState->isProxy)
        {
            loginState->clientIP = pktGetBits(pak, 32);
        }
        break;

    case TOLOGIN_SET_TERMS_OF_USE:
        {
            U32 crc = pktGetU32(pak);
            RemoteCommand_aslAPCmdSetKeyValue(NULL, GLOBALTYPE_ACCOUNTPROXYSERVER, 0, loginState->accountID, TERMS_OF_USE_KEY, crc, AKV_OP_SET);
        }
        break;

    case TOLOGIN_GO_TO_CHARACTER_SELECT:
        // Client has requested to return to character select state, generally by hitting the back button from UGC editing or map selection.
        aslLogin2_ReturnToCharacterSelect(loginState);
        break;

    default:
        Errorf("Invalid packet received from client: %d", cmd);
        break;
    }
}

#include "AutoGen/aslLogin2_ClientComm_h_ast.c"