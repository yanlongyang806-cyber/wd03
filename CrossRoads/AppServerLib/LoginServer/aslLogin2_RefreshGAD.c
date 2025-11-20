/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_RefreshGAD.h"
#include "aslLogin2_Error.h"
#include "ServerLib.h"
#include "AppServerLib.h"
#include "UtilitiesLib.h"
#include "textparser.h"
#include "stdtypes.h"
#include "objTransactions.h"
#include "GameAccountDataCommon.h"
#include "aslLoginCharacterSelect.h"
#include "accountnet.h"
#include "MicroTransactions.h"
#include "ShardCluster.h"
#include "ShardCommon.h"
#include "Player.h"
#include "GamePermissionsCommon.h"
#include "itemCommon.h"
#include "LoggedTransactions.h"
#include "AccountProxyCommon.h"

#include "AutoGen/aslLogin2_RefreshGAD_c_ast.h"
#include "AutoGen/GameAccountData_h_ast.h"
#include "AutoGen/GamePermissionsCommon_h_ast.h"
#include "Autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

extern ParseTable parse_GameAccountData[];
extern ParseTable parse_GamePermissionDefs[];

AUTO_STRUCT;
typedef struct RefreshGADState
{
    ContainerID accountID;
    PlayerType playerType;

    STRING_EARRAY extraGamePermissions;     AST(POOL_STRING)

    U32 timeStarted;
    U32 overrideLevel;

    bool containerCheckComplete;
    bool accountDataRequestComplete;
    bool failed;
    bool isLifetime;
    bool isPress;

    // The data returned from the account server about this account.
    AccountProxyAccountDataResponse *accountData;

    // String used to record errors for logging.
    STRING_MODIFIABLE errorString;          AST(ESTRING)

    // Completion callback data.
    RefreshGADCB cbFunc;                    NO_AST
    void *userData;                         NO_AST
} RefreshGADState;

// This creates the GameAccountData container if it doesn't already exist.
static void 
EnsureGameAccountDataExists(ContainerID accountID, TransactionReturnCallback cbFunc, void *userData)
{
    if( accountID > 0 )
    {
        static char *diffString = NULL;
        TransactionRequest *request = objCreateTransactionRequest();
        GameAccountData *protoGameAccountData;

        estrClear(&diffString);

        // Call game specific function to create initial game account data.
        protoGameAccountData = gameSpecific_GameAccountDataCreateInit();

        if ( protoGameAccountData )
        {
            StructTextDiffWithNull_Verify(&diffString, parse_GameAccountData, protoGameAccountData, NULL, 0, TOK_PERSIST, 0, 0);
        }
        objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
            "VerifyOrCreateAndInitContainer containerIDVar %s %d %s",
            GlobalTypeToName(GLOBALTYPE_GAMEACCOUNTDATA),
            accountID,
            diffString);
        objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
            objCreateManagedReturnVal(cbFunc, userData), "EnsureGameContainerExists", request);
        objDestroyTransactionRequest(request);
    }
}

// Handle the completion of the refresh operation.  Log any errors and call the callback to notify the caller of success or failure.
static void
GADRefreshComplete(RefreshGADState *refreshState)
{
    if ( refreshState->failed )
    {
        aslLogin2_Log("GameAccountData refresh failed for accountID %d. %s", refreshState->accountID, NULL_TO_EMPTY(refreshState->errorString));
    }
    else
    {
        aslLogin2_Log("GameAccountData refresh succeeded for accountID %d", refreshState->accountID);
    }

    // Notify the caller that we are done.
    if ( refreshState->cbFunc )
    {
        (* refreshState->cbFunc)(refreshState->accountID, !refreshState->failed, 
            refreshState->accountData ? refreshState->accountData->uHighestLevel : 0, 
            refreshState->accountData ? refreshState->accountData->uNumCharacters :0,
            refreshState->accountData ? refreshState->accountData->uLastLogoutSS2000 : 0,
            refreshState->userData);
    }

    // Clean up state.
    StructDestroy(parse_RefreshGADState, refreshState);
}

static void
BuildGamePermisionsList(GamePermissionDefs *gamePermissionDefs, PlayerType playerType, U32 daysSubscribed, U32 secondsPlayed, bool hasBeenBilled, STRING_EARRAY extraGamePermissions)
{
    GamePermissionDef *gamePermission = NULL;
    int i;
    U32 currentTime = timeSecondsSince2000();

    // Avoid memory allocation by reusing the same array.
    eaClearFast(&gamePermissionDefs->eaPermissions);

    // Grab the permissions granted due to player type.
    if ( playerType == kPlayerType_Premium )
    {
        gamePermission = g_pPremiumPermission;
    }

    // If the player is standard or there are not premium permissions, fall back to standard permissions.
    if ( gamePermission == NULL )
    {
        gamePermission = g_pBasePermission;
    }

    // Add the permissions due to player type.
    if ( gamePermission )
    {
        eaPush(&gamePermissionDefs->eaPermissions, gamePermission);
    }

    // Add any extra game permissions.
    for ( i = eaSize(&extraGamePermissions) - 1; i >= 0; i-- )
    {
        gamePermission = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, extraGamePermissions[i]);
        if ( gamePermission != NULL )
        {
            eaPush(&gamePermissionDefs->eaPermissions, gamePermission);
        }
    }

    // Calculate optional permissions based on time and billed state.
    for(i = 0; i < eaSize(&g_GamePermissions.eaTimedTokenList); ++i)
    {
        GamePermissionTimed *timedPermission = g_GamePermissions.eaTimedTokenList[i];
        int numDateRanges = eaSize(&timedPermission->eaFromToDates);
        bool timedPermissionGranted = false;

        if ( currentTime >= timedPermission->iStartSeconds )
        {
            // If there are date ranges, the permission is granted if the current time matches any of them, regardless of days subscribed or hours played.
            if ( numDateRanges > 0 )
            {
                int dateIndex;

                // Check whether the current date matches one of the date ranges
                for(dateIndex = numDateRanges - 1; dateIndex >= 0; dateIndex--)
                {
                    FromToDate *fromToDate = timedPermission->eaFromToDates[dateIndex];

                    if (currentTime >= fromToDate->uFromTimeSeconds && currentTime <= fromToDate->uToTimeSeconds)
                    {
                        timedPermissionGranted = true;
                        break;
                    }
                }
            }
            else
            {
                bool hasPaid = daysSubscribed || hasBeenBilled;

                // Grant the permission if the player meets "days subscribed" or "hours played" criteria.
                // Players who have paid money automatically match any "hours played" criteria even if they have not played that long.
                if ( daysSubscribed >= timedPermission->iDaysSubscribed && ( hasPaid || secondsPlayed >= (timedPermission->iHours * SECONDS_PER_HOUR)) )
                {
                    timedPermissionGranted = true;
                }
            }


            if ( timedPermissionGranted )
            {
                S32 j;
                // Add the timed permissions to the list we are granting to the player.
                for(j = eaSize(&timedPermission->eaPermissions) - 1; j >= 0; j--)
                {
                    eaPush(&gamePermissionDefs->eaPermissions, timedPermission->eaPermissions[j]);
                }
            }
        }
    }
}

// Build a ParsedAttributeList that contains the data needed to rebuild the species and costume unlocks on the GameAccountData.
static void
BuildParsedAttributeList(ParsedAVPList *attributeList, AccountProxyKeyValueInfoList *accountKeyValues)
{
    // Avoid memory allocation by reusing the same array.
    eaClearFast(&attributeList->eaPairs);

    if(accountKeyValues)
    {
        int j;
        for( j = eaSize(&accountKeyValues->ppList)-1; j >= 0; j--)
        {
            AccountProxyKeyValueInfo *pInfo = accountKeyValues->ppList[j];
            if(pInfo)
            {
                APAppendParsedAVPsFromKeyAndValue(pInfo->pKey, pInfo->pValue, &attributeList->eaPairs);
            }
        }
    }
}

// Clean up after the refresh transaction is done.
static void
DoGADRefreshCB(TransactionReturnVal *returnVal, RefreshGADState *refreshState)
{
    if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        estrConcatf(&refreshState->errorString, "Transaction failed to refresh GameAccountData container %d.", refreshState->accountID);
        refreshState->failed = true;
    }

    GADRefreshComplete(refreshState);
}

// Now that we have all the data necessary to refresh the GameAccountData, process the data into the form that the transaction needs, and then run the transaction.
static void
DoGADRefresh(RefreshGADState *refreshState)
{
    U32 daysSubscribed;
    static GamePermissionDefs *s_gamePermissions = NULL;
    static ParsedAVPList *s_AttributeList = NULL;
    TransactionReturnVal *returnVal;
	bool bHasCurrency = false;
	char *pKeyValue;
    PooledStringArrayStruct tmpExtraGamePermissions = {0};

    // Keep one GamePermissionDefs struct and reuse it to avoid excessive memory allocation.
    if ( s_gamePermissions == NULL )
    {
        s_gamePermissions = StructCreate(parse_GamePermissionDefs);
    }

    // Keep one ParsedAVPList struct and reuse it to avoid excessive memory allocation.
    if ( s_AttributeList == NULL )
    {
        s_AttributeList = StructCreate(parse_ParsedAVPList);
    }

    daysSubscribed = (refreshState->accountData->uSubscribedSeconds + (SECONDS_PER_DAY-1)) / SECONDS_PER_DAY;

	if(refreshState->accountData)
	{
		pKeyValue = AccountProxyFindValueFromKeyInList(refreshState->accountData->pKeyValues, microtrans_GetShardCurrency());
		if(pKeyValue && atoi(pKeyValue) > 0)
		{
			bHasCurrency = true;
		}
	}

    // Build the list of game permissions for this account.
    BuildGamePermisionsList(s_gamePermissions, refreshState->playerType, daysSubscribed, refreshState->accountData->uTotalPlayedSS2000, (refreshState->accountData->bBilled || bHasCurrency), refreshState->extraGamePermissions);

    // Build the list of costume and species unlocks for this account.
    BuildParsedAttributeList(s_AttributeList, refreshState->accountData->pKeyValues);

    tmpExtraGamePermissions.eaStrings = refreshState->extraGamePermissions;

    // Run the transaction to refresh GameAccountData.
    returnVal = LoggedTransactions_CreateManagedReturnVal("Login2_RefreshGAD", DoGADRefreshCB, refreshState);
    AutoTrans_aslLogin2_tr_RefreshGAD(returnVal, GLOBALTYPE_LOGINSERVER, GLOBALTYPE_GAMEACCOUNTDATA, refreshState->accountID, s_AttributeList, s_gamePermissions,
        refreshState->isLifetime, refreshState->isPress, refreshState->accountData, &tmpExtraGamePermissions, refreshState->overrideLevel);

    return;
}

// Check to see if all the data has arrived that is needed to do the GameAccountData refresh.  If it has then call the refresh transaction.
static void
DoGADRefreshIfReady(RefreshGADState *refreshState)
{
    if ( refreshState->containerCheckComplete && refreshState->accountDataRequestComplete )
    {
        if ( refreshState->failed )
        {
            // If we failed to get the data needed for the refresh, then clean up and report failure to the caller.
            GADRefreshComplete(refreshState);
        }
        else
        {
            DoGADRefresh(refreshState);
        }
    }
}

// Handle completion of GameAccountData container creation/verification.  Start the actual refresh if other data is ready.
static void
GADContainerExistsCB(TransactionReturnVal *returnVal, RefreshGADState *refreshState)
{
    refreshState->containerCheckComplete = true;
    if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        estrConcatf(&refreshState->errorString, "Failed to create or verify GameAccountData container %d.", refreshState->accountID);
        refreshState->failed = true;
    }

    DoGADRefreshIfReady(refreshState);
}

// Callback to receive the account data from the account proxy.  Start the actual refresh if other data is ready.
static void
GetAccountDataCB(TransactionReturnVal *returnVal, RefreshGADState *refreshState)
{
    refreshState->accountDataRequestComplete = true;
    if ( RemoteCommandCheck_aslAPCmdGetAccountData(returnVal, &refreshState->accountData) != TRANSACTION_OUTCOME_SUCCESS )
    {
        estrConcatf(&refreshState->errorString, "Failed to get account data for account %d.", refreshState->accountID);
        refreshState->failed = true;
    }

    DoGADRefreshIfReady(refreshState);
}

// Make sure the GameAccountData container exists, and rebuild/refresh it with permission and unlock data from the account server.
void 
aslLogin2_RefreshGAD(ContainerID accountID, PlayerType playerType, bool isLifetime, bool isPress, U32 overrideLevel, STRING_EARRAY extraGamePermissions, RefreshGADCB cbFunc, void *userData)
{
    RefreshGADState *refreshState = StructCreate(parse_RefreshGADState);
    TransactionReturnVal *returnVal;

    refreshState->accountID = accountID;
    refreshState->playerType = playerType;
    refreshState->isLifetime = isLifetime;
    refreshState->isPress = isPress;
    refreshState->timeStarted = timeSecondsSince2000();
    refreshState->cbFunc = cbFunc;
    refreshState->userData = userData;
    refreshState->overrideLevel = overrideLevel;

    // Copy the extra game permissions.
    eaCopy(&refreshState->extraGamePermissions, &extraGamePermissions);

    // NOTE - we check if the GameAccountData container exists and get the account data in parallel.

    // If the GameAccountData container doesn't exist, then create it.
    EnsureGameAccountDataExists(accountID, GADContainerExistsCB, refreshState);

    // Request the account data that will be used to refresh the GameAccountData.
    returnVal = objCreateManagedReturnVal(GetAccountDataCB, refreshState);
    RemoteCommand_aslAPCmdGetAccountData(returnVal, GLOBALTYPE_ACCOUNTPROXYSERVER, 0, accountID, GetProductName(), GetShardCategoryFromShardInfoString(), GetShardNameFromShardInfoString(), ShardCommon_GetClusterName());
}

AUTO_COMMAND_REMOTE;
bool 
aslLogin2_RefreshGADCmd(ContainerID accountID, PlayerType playerType, bool isLifetime, bool isPress, U32 overrideLevel, PooledStringArrayStruct *extraGamePermissions)
{
    aslLogin2_RefreshGAD(accountID, playerType, isLifetime, isPress, overrideLevel, extraGamePermissions->eaStrings, NULL, NULL);
    return true;
}

#include "AutoGen/aslLogin2_RefreshGAD_c_ast.c"