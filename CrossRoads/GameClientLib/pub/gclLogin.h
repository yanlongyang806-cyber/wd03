#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLLOGIN_H_
#define GCLLOGIN_H_

#include "Message.h"

typedef U32 ContainerID;
typedef struct CharacterCreationDataHolder CharacterCreationDataHolder;
typedef struct PlayerCostumeRef PlayerCostumeRef;
typedef struct PlayerTypeConversion PlayerTypeConversion;
typedef struct PossibleMapChoices PossibleMapChoices;
typedef struct PossibleMapChoice PossibleMapChoice;
typedef struct PossibleUGCProject PossibleUGCProject;
typedef struct UIWindow UIWindow;
typedef struct ShardInfo_Basic ShardInfo_Basic;
typedef struct GenNameListReq GenNameListReq;
typedef struct GameSession GameSession;
typedef struct GameAccountData GameAccountData;
typedef struct GameAccountDataNumericPurchaseDef GameAccountDataNumericPurchaseDef;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct Entity Entity;
typedef struct Login2CharacterChoice Login2CharacterChoice;
typedef struct Login2CharacterSelectionData Login2CharacterSelectionData;
typedef struct Login2CharacterDetail Login2CharacterDetail;
typedef struct Login2CharacterCreationData Login2CharacterCreationData;

typedef enum LoginLinkState LoginLinkState;

// Message keys for the login process. As of the time I'm writing this,
// they're in Core/data/ui/messages/LoginStatus.ms.
#define LOGINSTATUS_ACCOUNT_CONNECTING	"LoginStatus_Account_Connecting"
#define LOGINSTATUS_ACCOUNT_NOSERVER	"LoginStatus_Account_NoServer"
#define LOGINSTATUS_ACCOUNT_WAITING		"LoginStatus_Account_Waiting"
#define LOGINSTATUS_ACCOUNT_ONETIMECODE	"LoginStatus_Account_OneTimeCode"
#define LOGINSTATUS_ACCOUNT_TIMEOUT		"LoginStatus_Account_Timeout"
#define LOGINSTATUS_ACCOUNT_DISCONNECT	"LoginStatus_Account_Disconnect"
#define LOGINSTATUS_GETTING_SHARDS		"LoginStatus_Getting_Shards"
#define LOGINSTATUS_LOGIN_CONNECT		"LoginStatus_Login_Connect"
#define LOGINSTATUS_LOGIN_NOSERVER		"LoginStatus_Login_NoServer"
#define LOGINSTATUS_LOGIN_TIMEOUT		"LoginStatus_Login_Timeout"
#define LOGINSTATUS_LOGIN_DISCONNECT	"LoginStatus_Login_Disconnect"
#define LOGINSTATUS_LOGIN_CHARACTERS	"LoginStatus_Login_Characters"
#define LOGINSTATUS_LOGIN_MAPS			"LoginStatus_Login_Maps"
#define LOGINSTATUS_WAITING_FOR_REDIRECT "LoginStatus_Waiting_For_Redirect"
#define LOGINSTATUS_WAITING_FOR_REDIRECT_DONE "LoginStatus_Waiting_For_Redirect_Done"
#define LOGINSTATUS_REDIRECTING         "LoginStatus_Redirecting"
#define LOGINSTATUS_LOGIN_NEWCHARACTER	"LoginStatus_Login_NewCharacterData"
#define LOGINSTATUS_LOGIN_NOMAPS		"LoginStatus_Login_NoMaps"
#define LOGINSTATUS_FAILURE_UNKNOWN		"LoginStatus_Failure_Unknown"
#define LOGINSTATUS_FAILURE_NOSHARDS	"LoginStatus_Failure_NoShards"
#define LOGINSTATUS_DISABLE_PROXY_TITLE	"LoginStatus_Disable_Proxy_Title"
#define LOGINSTATUS_DISABLE_PROXY_MESSAGE "LoginStatus_Disable_Proxy_Message"
#define LOGINSTATUS_GET_INITIAL_LOBBY_DATA		"LoginStatus_Get_Initial_Lobby_Data"
#define LOGINSTATUS_GET_INITIAL_LOBBY_DATA_TIMEOUT	"LoginStatus_Get_Initial_Lobby_Data_TimeOut"
#define LOGINSTATUS_JOIN_SESSION_FAILED	"LoginStatus_Join_Session_Failed"
#define LOGINSTATUS_JOINING_GAME_SESSION	"LoginStatus_Joining_Game_Session"
#define LOGINSTATUS_CREATE_SESSION_TIMEOUT	"LoginStatus_Create_Session_Timeout"
#define LOGINSTATUS_SAVE_SESSION_TIMEOUT	"LoginStatus_Save_Session_Timeout"
#define LOGINSTATUS_CREATING_SESSION	"LoginStatus_Creating_Session"
#define LOGINSTATUS_SAVING_SESSION	"LoginStatus_Saving_Session"
#define LOGINSTATUS_CREATE_SESSION_FAILED	"LoginStatus_Create_Session_Failed"
#define LOGINSTATUS_SAVE_SESSION_FAILED	"LoginStatus_Save_Session_Failed"
#define LOGINSTATUS_GET_SESSION			"LoginStatus_Get_Session"
#define LOGINSTATUS_GET_SESSION_FAILED	"LoginStatus_Get_Session_Failed"
#define LOGINSTATUS_GET_SESSION_TIMEOUT	"LoginStatus_Get_Session_Timeout"
#define LOGINSTATUS_SESSION_NO_MORE_VALID	"LoginStatus_Session_No_More_Valid"
#define LOGINSTATUS_KICKED_OUT_OF_SESSION	"LoginStatus_Kicked_Out_Of_Session"
#define LOGINSTATUS_FAILED_TO_TRANSFER_FROM_LOBBY	"LoginStatus_Failed_To_Transfer_From_Lobby"
#define LOGINSTATUS_TRANSFERRING_TO_MAP_FROM_LOBBY	"LoginStatus_Transferring_To_Map_From_Lobby"
#define LOGINSTATUS_GET_GAME_PROGRESSION_NODES_TIMEOUT	"LoginStatus_Get_Game_Progression_Nodes_Timeout"
#define LOGINSTATUS_FAILED_TO_SWITCH_CHARS_IN_LOBBY		"LoginStatus_Failed_To_Switch_Chars_In_Lobby"
#define LOGINSTATUS_CHOOSING_CHAR_IN_LOBBY				"LoginStatus_Choosing_Char_In_Lobby"
#define LOGINSTATUS_LOGIN_CREATING_NEW_CHARACTER	"LoginStatus_Login_Creating_New_Character"
#define LOGINSTATUS_LEAVING_LOBBY	"LoginStatus_Leaving_Lobby"
#define LOGINSTATUS_TRANSFERRING_TO_MAP_FROM_QUEST_SELECTOR	"LoginStatus_Transferring_To_Map_From_Quest_Selector"
#define LOGINSTATUS_FAILED_TO_TRANSFER_FROM_SOLO_PLAY	"LoginStatus_Failed_To_Transfer_From_Solo_Play"
#define LOGINSTATUS_LOADING_LOBBY	"LoginStatus_Loading_Lobby"

#define GCL_LOBBY_TIMEOUT (20.f)
#define GCL_LOGIN_TIMEOUT (60.f)
#define GCL_LOGIN_ONETIMECODE_TIMEOUT (180.f)
#define GCL_GETINITIALLOBBYDATA_TIMEOUT (45.f)
#define GCL_GET_GAME_PROGRESSION_NODES_TIMEOUT (45.f)
#define GCL_JOIN_SESSION_TIMEOUT (60.f)
#define GCL_CREATE_SESSION_TIMEOUT (60.f)
#define GCL_GET_SESSION_INFO_TIMEOUT (60.f)
#define GCL_TRANSFER_TO_MAP_FROM_LOBBY_TIMEOUT (120.f)

void gclLogin_GetUnlockedCostumes(PlayerCostumeRef ***pppCostumes);


// Select a particular character to the be the one to login
bool gclLoginChooseCharacter(SA_PARAM_NN_VALID Login2CharacterChoice *chosenCharacter);

// Set the creation data that will be used to create a character, and begin the creation process
bool gclLoginCreateCharacter(SA_PARAM_NN_VALID Login2CharacterCreationData *characterCreationData);

// Choose character by name
bool gclLoginChooseCharacterByName(SA_PARAM_NN_STR const char *pCharacterName, bool bForceCreate);

// Request deleting a specified character (the type/id is the important part
void gclLoginRequestCharacterDelete(Login2CharacterChoice *characterChoice);

// change the name of this character
void gclLoginRequestChangeName(ContainerID playerID, const char *newName, bool bBadName);

// Request the possible maps, for the first time or as an update
void gclLoginRequestPossibleMaps(void);

// Select a particular map to connect to
// I think annotating this requires annotating more of textparser. -- jfw
void gclLoginChooseMap(PossibleMapChoice *pMap);

// Select a particular shard to connect to
void gclLoginChooseShard(ShardInfo_Basic *pShard);

// Fail the login process
void gclLoginFail_internal(SA_PARAM_OP_STR const char *pErrorString, bool bNotDuringNormalLoginProcess);
#define gclLoginFail(errorString) gclLoginFail_internal(errorString, false)
#define gclLoginFailNotDuringNormalLogin(errorString) gclLoginFail_internal(errorString, true)

// Errors in the login process due to invalid display names
const char *gclLogin_GetInvalidDisplayName(void); // Returns the current invalid display name in the login process, NULL if not in the correct state
void gclLogin_ChangeDisplayName(const char *newDisplayName); // Send a display name change request to the Account Server via the Login Server

// Displays the various login screen warnings
void gclLoginDisplayDriverWarningAndBuildNumber(void);

// Sets up quick loigin
void gclInitQuickLoginWithPos(int iActive, char *pRequestedCharName, char *pRequestedMapName, Vec3 vPos, Vec3 vRot, char *pMapVariables);
void gclInitQuickLogin(int iActive, char *pRequestedCharName, char *pRequestedMapName);

extern int g_iQuickLogin;
extern bool gbSkipAccountLogin;
extern PossibleMapChoices *g_pGameServerChoices;
extern char *g_pchLoginStatusString;
extern Login2CharacterSelectionData *g_characterSelectionData;
extern U32 g_characterSelectionDataVersionNumber;

// For UGC logging out and returning to the UGC Project Chooser.
extern bool g_bChoosePreviousCharacterForUGC;

extern bool g_bCharIsSuperPremium;

typedef struct NetLink NetLink;
extern NetLink *gpLoginLink;

// Login Data for Tickets
U32 LoginGetAccountID(void);
S32 LoginGetAccessLevel(void);
U32 LoginGetPlayerType(void);
GameAccountData* LoginGetAccountData(void);
const char * LoginGetAccountName(void);
const char * LoginGetDisplayName(void);
const char * LoginGetShardInfo(void);

// Request random names from the login server
void gclLoginRequestRandomNames(GenNameListReq *pReq);

const char *gclLoginGetChosenCharacterName(void);
const char *gclLoginGetChosenCharacterAllegiance(void);
bool gclLoginGetChosenCharacterUGCEditAllowed(void);
NOCONST(Entity) *gclLoginGetChosenEntity(void);

void gclChooseUGCProject(PossibleUGCProject *pProject);
void gclChooseNewUGCProject(const char* projName, const char* allegiance, int level);
void gclDeleteUGCProject(PossibleUGCProject *pProject);

void gclLogin_PushPlayerUpdate(void);

void gclLoginSendRefDictDataRequests(void);

void gclLoginSendChosenCharacterInfo(ContainerID characterID, bool loggingInForUGC);

void gclSetAuthTicketNew(U32 accountid, U32 ticketid);

void gclLogin_BrowseUGCProducts(bool bEnable);
bool gclLogin_GameAccountCanMakeNumericPurchaseWithAnyCharacter(GameAccountDataNumericPurchaseDef* pDef);
bool gclLogin_GameAccountCanMakeAnyNumericPurchaseWithAnyCharacter(S32 eCategory);

#endif
