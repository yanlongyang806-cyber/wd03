#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "windefinclude.h"
#include "gfxcamera.h"
#include "gfxsettings.h"
#include "net\net.h"
#include "GlobalTypeEnum.h"
#include "accountCommon.h"

typedef struct Entity Entity;
typedef struct NetLink NetLink;
typedef struct InputDevice InputDevice;
typedef struct FrameLockedTimer FrameLockedTimer;
typedef struct ReturnedGameServerAddress ReturnedGameServerAddress;
typedef struct PowerDef PowerDef;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct LoadScreenDynamic LoadScreenDynamic;
typedef struct PortIPPair PortIPPair;

extern NetLink *gServerLink;
extern NetLink **gppLastLink;

typedef enum HeadshotServerFlags
{
	HSF_FORCE_BODYSHOT = 1 << 0,
	HSF_TRANSPARENT = 1 << 1,
	HSF_COSTUME_V0 = 1 << 2,
} HeadshotServerFlags;

typedef struct DeviceDesc 
{
	RdrDevice *device;
	InputDevice *inp_device;
	// activecamera is not necessarily any of these.
	GfxCameraController gamecamera, freecamera, democamera, cutscenecamera, contactcamera, *activecamera;
	GfxCameraView *overrideCameraView;
	void *windowHandle;
} DeviceDesc;

AUTO_STRUCT;
typedef struct LoginServerAddress
{
    STRING_POOLED shardName;    AST(POOL_STRING)
    const char *machineNameOrAddress;
    U32 portNum;
} LoginServerAddress;

typedef struct GameClientLibState
{
	bool bNoThread;
	bool bForceThread;

	char patchCommandLine[1024];

	char gameServerName[1024];
	int gameServerPort;

	char gameServerIP[32];
	ContainerID iGameServerContainerID;

	char **ppLoginServerNames;

    // The list of login server port/ip pairs passed to the client on the command line by the launcher.
    LoginServerAddress **eaLoginServers;
    int iLoginServerIndex;

    // This flag is set when a connection to the login server is attempted, and reset if we connect successfully.
    bool bLoginServerConnectPending;

	// A list of active shard events, which triggers various name munging
	// and similar behaviors to temporarily "theme" the game. These are
	// passed via -GameEvent on the command line, which comes from the
	// ControllerTracker via the client launcher.
	const char **shardWideEvents;

	//this is only set once the connection has happened
	U32  connectedLoginServerIP;

	// this seems like the most appropriate place to put this - name of the cluster we're connected to, if any
	char shardClusterName[256];

	char accountServerName[1024];
	U32 accountServerIP;
	U32 accountID;
	U32 accountTicketID;
	U32 accountConflictID;

	char loginName[MAX_LOGIN_FIELD];
	char loginField[MAX_LOGIN_FIELD];
	char pwAccountName[MAX_ACCOUNTNAME];
	char displayName[MAX_ACCOUNTNAME];
	char loginPassword[MAX_PASSWORD];
	bool bSaveLoginUsername;
	bool bEditLogin;
	U32 eLoginType; // see accountnet.h AccountLoginType
	ContainerID loginCharacterID;
	S32 eDefaultPlayerType;

	char loginCharacterName[128];
	int loginCookie;

	int bUseFreeCamera;
	int bUseStationaryCamera;

	DeviceDesc *pPrimaryDevice;

	HINSTANCE hInstance;

	bool bDrawWorldThisFrame;
	bool bHideWorld;

	FrameLockedTimer *frameLockedTimer;
	F32 frameElapsedTime;
	F32 frameElapsedTimeReal;
	F32 frameElapsedTimeRealNext;
	U32 totalElapsedTimeMs;
	U32 logoutElapsedTime;

	F32 startupTime;
	
	F32 sendToServerInterval;

	bool bGotWorldUpdate;
	bool bGotGeneralUpdate;
	bool bGotGameAccount;
	bool bGotLoginSuccess;
	bool bAttemptingLogout;
	bool bGettingAuthTicket;
	bool bReadyForGeneralUpdates;

	bool bSkipWorldUpdate;		// Test client only... or anyone who doesn't want the world loaded
	bool bPreLoadEditData;
	bool bSkipPreload;
	bool bSkipPressAnyKey;
	bool bForcePromptToStartLevel;
	bool bDisableFolderCacheAfterLoad;
	bool bDoNotQueueErrors;
	bool bNoTimeout;
	bool bAllowSharedMemory;
	bool bForceLoadCostumes;
	bool bRunning;
	bool bInitialPopUp; // For NVIDIA/gfx debug
	
	bool bSkipTutorial;			// used for skipping the tutorial for new characters

	// If true, the analog controls won't affect the camera or movement.
	// Since these don't use the keybinds system (because they're analog),
	// it needs to be filtered elsewhere, which is here for now.
	bool bLockPlayerAndCamera;

	// Lock only the right stick, used for things like manual targeting
	bool bLockControllerCameraControl;
	
	bool bCutsceneActive : 1;
	
	U32 executableCRC; //CRC of the current executable (GameClient.exe or GameClientXBOX.exe, presumably)


	// Resource section
	int logoResource;
	int iconResource;
	// JE: For some reason, I don't think the 3 things below have anything to do with resources...
	int gameModeResource;
	int teamIDResource;
	int findTeamResource;

	// Click to Move /////////////////////////////
	Vec3 v3MoveToLocation;
	bool bMovingTowardsCursor		: 1;
	bool bMoveToTarget				: 1;
	bool bContinuouslyAttacking		: 1;
	bool bClickToMoveCameraAdjust	: 1;


	bool bAudioListenerModeShortBoom : 1;

	// Disables the XBOX title menu and XUID logins
	bool bDisableXBoxTitleMenu;

	// If this is set to true executable is in patch mode, 
	// otherwise executable is in game mode
	bool bXBoxPatchModeEnabled;

	// change the joystick controls to controlling a ship instead of a character

	LoadScreenDynamic	*pLoadingScreens;
	// loading screens that are passed to client from login server

} GameClientLibState;

extern GameClientLibState gGCLState;

AUTO_STRUCT;
typedef struct EntityScreenBoundingBoxAccelConfig
{
	// the base units per second the bounding box will adjust
	F32			fBaseRate;

	// the maximum units per second allowed
	F32			fMaxRate;

	// if the bounding units exceed this threshold, start accelerating
	F32			fAccelerateStartThreshold;
	
	// if the bounding units go below this threshold, stop accelerating
	F32			fAccelerateStopThreshold;

	// units per second. The velocity will not exceed fMaxRate
	F32			fAcceleration;
	
} EntityScreenBoundingBoxAccelConfig;

// project client config, which is loaded from data\server\ProjectGameClientConfig.txt
AUTO_STRUCT;
typedef struct ProjectGameClientConfig
{
	// Use capsules entirely if they're valid
	bool bUseCapsuleBounds;

	// Use the fixed over head data or not in pEntUi
	bool bUseFixedOverHead;
	
	// number of seconds to do a full reset on all overhead data in pEntUi
	F32 fHeightResetTime;
	
	// Time in seconds to wait before lowering UI top for overhead
	F32 fLowHeightHoldTime;	

	// Don't lower height unless below this percent of hi
	F32 fLowHeightThreshold;	
	
	// Adjustment rate of height 1.0 == about 1 second 0 to top raise, slower going down
	F32 fHeightAdjustMentRate;	
	
	// if the skeleton is smaller than this then there is a problem with the skeleton, use capsule instead
	F32 fBadSkeletonY;
	
	// offset for chat with no name
	F32 fChatNormalOffsetY;
	
	// offset for chat with name
	F32 fChatNameOffsetY;

	// maximum height on map and mini map to show rooms above. See GenMapDrawTexture()
	F32 fMapMaxShowY;

	// If this is set, the object gens uses the center point of the object as the bounding box used for calculating gen position and size.
	U32 bUseObjectCenterAsBoundingBoxForObjectGens : 1;

	// Should fMapMaxShowY be used or the hard coded 10.0f value?
	U32 bUseMapMaxShowY;
	
	// if defined will take priority over the bUseFixedOverHead and bUseCapsuleBounds when computing an entity's screen bounding box
	EntityScreenBoundingBoxAccelConfig	*pScreenBoundingAccelConfig;
	

} ProjectGameClientConfig;

extern ProjectGameClientConfig gProjectGameClientConfig;

typedef int (*gclPreMainFunction)(HINSTANCE hInstance, LPSTR lpCmdLine);
typedef void (*gclMainFunction)(HINSTANCE hInstance, LPSTR lpCmdLine, const void *logo_jpg_data, int logo_data_size);

extern gclMainFunction pOverrideMain;
extern gclPreMainFunction pOverridePreMain;

void gclMain(HINSTANCE hInstance, LPSTR lpCmdLine, const void *logo_jpg_data, int logo_data_size);
bool gclPreMain(const char* pcAppName, const void *logo_jpg_data, int logo_data_size);

NetLink *gclGetLinkToTestClient(void);
void InitTestClientCommunication(void);
void SendCommandStringToTestClient(const char *pString);
void SendCommandStringToTestClientfEx(const char *pFmt, ...);
#define SendCommandStringToTestClientf(fmt, ...) SendCommandStringToTestClientfEx(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Common functions for gameplay and loading state
void gclConnectedToGameServerOncePerFrame(void);
void gclLibsOncePerFrame(void);
void gclSetCameraTarget(Entity* pEnt);
void gclSetFreeCamera(int bFreeCamera);  


bool gclHandleInteractTarget(Entity *ent, Entity *entTarget, WorldInteractionNode *nodeTarget, const char *pcVolumeName, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID, U32 uNodeInteractDist, Vec3 vNodePosFallback, F32 fNodeRadiusFallback, bool bClientValidatesInteract );
// Functions that attempt to have handle world lib interaction, return true if they handled it

typedef void (*gclGhostDrawFunc)(void);

void gclRegisterGhostDrawFunc(gclGhostDrawFunc cbDraw);
bool gclRemoveGhostDrawFunc(gclGhostDrawFunc cbDraw);

void gclSetOverrideCameraActive(GfxCameraController *pCamera, GfxCameraView *pCameraView);

//stuff that happens every 10 seconds during gameplay
void gclPeriodicGameplayUpdate(void);

//Saves out client settings if necessary every 10 seconds during gameplay
void gclSettingsUpdate(void);

void gclLoadingDone(void);
void gclLoadEndCallbackInternal(bool is_fake, int fake_count);

void InitSimpleCommandPort(void);

void console(int enable);


typedef void (*gclSetEditMode)(void);

// Registers a callback that will get called when enabling or disabling editor mode
void gclRegisterEditChangeCallback(gclSetEditMode cbChange);

// Enables or disables production edit on the client, based on what the server says
void gclSetProductionEdit(int enable);

void gclFixEntCostumes(void);

void ClientLogOut(void);
void ClientForceLogOut(void);
void ClientGoToCharacterSelect(void);
void ClientGoToCharacterSelectAndChoosePreviousForUGC(void);
void CancelLogOut(void);

int gclGetCurrentInstanceIndex(void);

// Indicates whether any of the cut-scene cameras are active
bool gclAnyCutsceneCameraActive(void);
bool gclContactDialogCameraActive(void); // more specific version

LATELINK;
void gclOncePerFrame_GameSpecific(F32 elapsed);

bool gclGetInstanceSwitchingAllowed(void);