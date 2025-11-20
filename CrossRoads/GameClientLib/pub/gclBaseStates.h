#pragma once

//this file declares that global states that are used by GameClientLib

//states for dynamic patching that prepares for streaming files while playing
#define GCL_PATCHSTREAMING "gclPatchStreaming"

//states for "dynamic" patching that happens on namespaces
#define GCL_PATCH "gclPatch"
#define GCL_PATCH_PRUNE "gclPatchPrune"
#define GCL_PATCH_PRUNE_ORPHANED "gclPatchPruneOrphaned"
#define GCL_PATCH_PRUNE_OLD "gclPatchPruneOld"
#define GCL_PATCH_PRUNE_LRU "gclPatchPruneLRU"
#define GCL_PATCH_CONNECT "gclPatchConnecting"
#define GCL_PATCH_SET_VIEW "gclPatchSetView"
#define GCL_PATCH_XFER "gclPatchXfer"

//init state
#define GCL_INIT "gclInit"

// XBOX patcher state
#define GCL_XBOX_PATCH "gclXBoxPatch"
#define GCL_XBOX_SHARD_GET "gclXBoxShardGet"
#define GCL_XBOX_SHARD_NONE "gclXBoxShardNone"

//main graphics/audio loop
#define GCL_BASE "gclBase"

#		define GCL_QUICK_PLAY "gclQuickPlay"

// state which plays the launch video
#define GCL_PLAY_VIDEO "gclPlayVideo"

//login process - all states defined in gclLogin.c
#	define GCL_LOGIN "gclLogin"

//login to account server to get ticket
#   define GCL_ACCOUNT_SERVER_LOGIN "gclAccountServerLogin"
#   define GCL_ACCOUNT_SERVER_CONNECT "gclAccountServerConnect"
#   define GCL_ACCOUNT_SERVER_WAITING_FOR_TICKET "gclAccountServerWaitingForTicket"
#   define GCL_ACCOUNT_ONETIMECODE "gclAccountServerOneTimeCode"
#   define GCL_ACCOUNT_SAVENEXTMACHINE "gclAccountServerSaveNextMachine"

//wait for gfx to finish loading before connecting to login server
#		define GCL_LOGIN_SERVER_LOAD_WAIT "gclLoginServerLoadWait"

//connecting to login server
#		define GCL_LOGIN_SERVER_CONNECT "gclLoginServerConnect"

//The login has succeeded, but the player type hasn't been chosen (AL4 or above with free-2-play enabled)
#	define GCL_LOGIN_SELECT_PLAYERTYPE "gclLoginSelectPlayerType"

// invalid display name state
#define GCL_LOGIN_INVALID_DISPLAY_NAME "gclLoginInvalidDisplayName"
#define GCL_LOGIN_CHANGING_DISPLAY_NAME "gclLoginChangeDisplayName"
#define GCL_LOGIN_DISPLAY_NAME_WAIT "gclLoginChangeDisplayNameWait"

//sent account info to login server, waiting for list of characters
#		define GCL_LOGIN_WAITING_FOR_CHARACTER_LIST "gclLoginWaitingForCharacterList"

//waiting while user chooses a character
#		define GCL_LOGIN_USER_CHOOSING_CHARACTER "gclLoginUserChoosingCharacter"

//user is choosing an existing character 
#			define GCL_LOGIN_USER_CHOOSING_EXISTING "gclLoginUserChoosingExisting"

//user has clicked "new character", waiting for data from server
#			define GCL_LOGIN_NEW_CHARACTER_WAITING_FOR_DATA "gclLoginNewCharacterWaiting"

//user is navigating "new character" creation (will certainly have app-specific substates)
#			define GCL_LOGIN_NEW_CHARACTER_CREATION "gclLoginNewCharacterCreation"

//sent character choice to login server, waiting to see if we will be redirected
#		define GCL_LOGIN_WAITING_FOR_REDIRECT "gclLoginWaitingForRedirect"

//after reconnecting to redirected login server, wait to hear from the loginserver before continuing.
#		define GCL_LOGIN_WAITING_FOR_REDIRECT_DONE "gclLoginWaitingForRedirectDone"

//sent character choice to login server, waiting for list of possibilities
#		define GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST "gclLoginWaitingForGameserverList"

//displaying login possibilities to user
#		define GCL_LOGIN_USER_CHOOSING_GAMESERVER "gclLoginUserChoosingGameServer"

//displaying possible UGC projects to user
#		define GCL_LOGIN_USER_CHOOSING_UGC_PROJECT "gclLoginUserChoosingUGCProject"

//displaying UGC products
#		define GCL_LOGIN_USER_BROWSING_UGC_PRODUCTS "gclLoginUserBrowsingUGCProducts"

//user has chosen a UGC project, waiting to be allowed to edit it
#		define GCL_LOGIN_WAITING_UGC_EDIT_PERMISSION "gclLoginUserWaitingUgcEditPermission"

//some part of the login process failed
#		define GCL_LOGIN_FAILED "gclLoginFailed"

//connecting to a gamesever, and loading game data
#	define GCL_LOADING "gclLoading"

//user made choice... waiting for connection info
#		define GCL_LOADING_WAITING_FOR_ADDRESS "gclLoadingWaitingForAddress"

//got address of game server
#		define GCL_LOADING_GAMESERVER_CONNECT "gclLoadingGameServerConnect"

//waiting for user input to enter game server
#	define GCL_LOADING_PRESS_ANY_KEY "gclLoadingPressAnyKey"

//actually loading game data, after connecting to server
#		define GCL_LOADING_GAMEPLAY "gclLoadingGameplay"

//replaying a demo movie
#	define GCL_DEMO_PLAYBACK "gclDemoPlayback"
#	define GCL_DEMO_LOADING "gclDemoLoading"

//gameplay
#	define GCL_GAMEPLAY "gclGameplay"
#	define GCL_GAME_MENU "gclGameMenu"

//displaying map choice UI
#		define GCL_GAMEPLAY_CHOOSING_GAMESERVER "gclGameplayChoosingGameServer"

//loading map data from an intra-map transfer
#		define GCL_LOADING_WITHIN_MAP "gclLoadingWithinMap"

// entity debug menu
#define GCL_DEBUG_MENU "gclDebugMenu"

//cleanup
#define GCL_CLEANUP "gclCleanup"

void FCCharacter_Back(void);
void WaypointFXOncePerFrame(void);
void Login_Back(void);
const char *gclGetRunOnConnectString(void);
