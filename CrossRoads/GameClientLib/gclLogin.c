/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclBaseStates.h"
#include "globalstatemachine.h"
#include "structnet.h"
#include "file.h"
#include "endian.h"
#include "crypt.h"
#include "gameclientlib.h"
#include "MapDescription.h"
#include "LoginCommon.h"
#include "Logging.h"
#include "GraphicsLib.h"
#include "GfxSpriteText.h"
#include "GfxLoadScreens.h"
#include "GfxSprite.h"
#include "chat/gclChatLog.h"
#include "gclCommandParse.h"
#include "gclLogin.h"
#include "gclPatchStreaming.h"
#include "systemspecs.h"
#include "TimedCallback.h"
#include "gclSendToServer.h"
#include "gclQuickPlay.h"
#include "fileWatch.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "utilitieslib.h"
#include "UGCProjectChooser.h"
#include "UGCSeriesEditor.h"
#include "AccountDataCache.h"
#include "net/accountnet.h"
#include "gclCamera.h"
#include "Expression.h"
#include "ResourceManager.h"
#include "progression_common.h"
#include "objPath.h"
#include "sysutil.h"
#include "GameStringFormat.h"
#include "StringUtil.h"
#include "StringCache.h"
#include "gclDialogBox.h"
#include "ContinuousBuilderSupport.h"
#include "sock.h"
#include "NewControllerTracker_Pub.h"
#include "NameGen.h"
#include "Prefs.h"
#include "gclOptions.h"
#include "inputData.h"
#include "accountnet.h"
#include "gclPatching.h"
#include "MicroTransactions.h"
#include "Login2Common.h"
#include "Login2CharacterDetail.h"
#include "gclLogin2.h"

#include "Player.h"
#include "UIGen.h"
#include "gclUGC.h"
#include "Organization.h"
#include "gclAccountProxy.h"
#include "WorldGrid.h"
#include "gclChat.h"
#include "../StaticWorld/WorldGridPrivate.h"

#include "AccountDataCache_h_ast.h"
#include "AutoGen/LoginCommon_h_ast.h"
#include "AutoGen/GameAccountData_h_ast.h"
#include "AutoGen/NewControllerTracker_Pub_h_ast.h"
#include "AutoGen/NameGen_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/Login2Common_h_ast.h"
#include "UGCCommon.h"
#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"
#include "AppRegCache.h"
#include "MicroTransactions.h"
#include "MicroTransactions_h_ast.h"
#include "gclMicroTransactions.h"
#include "UGCEditorMain.h"
#include "wlGroupPropertyStructs.h"
#include "mission_common.h"
#include "AutoGen/mission_common_h_ast.h"
#include "itemCommon.h"
#include "AutoGen/itemCommon_h_ast.h"
#include "gclSteam.h"
#include "accountnet_h_ast.h"
#include "StashTable.h"
#include "chatCommonStructs.h"
#include "Team.h"
#include "qsortG.h"
#include "UGCProductViewer.h"
#include "gclUGC.h"
#include "gclCostumeUI.h"
#include "CharacterClass.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "wlPerf.h"
#include "CostumeCommonLoad.h"
#include "wlUGC.h"
#include "gclNotify.h"

#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/Message_h_ast.h"
#include "AutoGen/GamePermissionsCommon_h_ast.h"
#include "AutoGen/Login2CharacterDetail_h_ast.h"
#include "AutoGen/GameClientLib_h_ast.h"

#include "LoadScreen\LoadScreen_Common.h"
#include "AutoGen/LoadScreen_Common_h_ast.h"
#include "gfxHeadShot.h"
#include "gclPlayVideo.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("LoginCommon", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("accountCommon", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("accountnet", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("AccountDataCache", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("newcontrollertracker_pub", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("LoadScreen_Common", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("Login2ClusterStatus", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("Login2ShardStatus", BUDGET_GameSystems););

extern ParseTable parse_AccountTicketSigned[];
#define TYPE_parse_AccountTicketSigned AccountTicketSigned
extern ParseTable parse_AccountTicket[];
#define TYPE_parse_AccountTicket AccountTicket
extern char prodVersion[];

extern InputState input_state;

bool g_bShowAllStaticMaps = false;
bool g_bCharIsSuperPremium = false;

// Show static maps in the map selector.
AUTO_CMD_INT(g_bShowAllStaticMaps, ShowAllStaticMaps) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(Debug) ACMD_CMDLINE ACMD_HIDE;



bool gbAllowVersionMismatch = false;
AUTO_CMD_INT(gbAllowVersionMismatch, AllowVersionMismatch) ACMD_CMDLINE;

//if true, then log the player back into their most recent static map instead of anything else. (For instance,
//if the player is having trouble doing client patching for a NS map, they might be effectively blackholed
//and have to use this to back out
bool gbSafeLogin = false;
AUTO_CMD_INT(gbSafeLogin, SafeLogin) ACMD_ACCESSLEVEL(0);

//unlike other quicklogin variants, this never creates a character or anything, just always makes the first availalbe selection, except that it waits for an existing map and chooses the first existing map
bool gbClickThroughLoginProcess = false;

U32 giLoginQueuePos = 0;
U32 giLoginQueueSize = 0;
int giLoginTimeoutFalloffFactor = 1;

static Login2CharacterChoice *s_chosenCharacter = NULL;
static Login2CharacterCreationData *s_characterCreationData = NULL;
static PossibleUGCProjects *spPossibleUGCProjects = NULL;
static PossibleUGCProjects *spPossibleUGCImports = NULL;
static PossibleUGCProject *spChosenUGCProject = NULL;
static UGCProjectReviews *spReviews = NULL;

static bool s_bLoggingInForUGC = false;

// Data for shard redirects.
static bool s_doRedirect = false;
static ContainerID s_redirectAccountID = 0;
static U32 s_redirectIP = 0;
static U32 s_redirectPort = 0;
static U64 s_transferCookie = 0;
static char s_redirectIPString[128];
static Login2ClusterStatus *s_ClusterStatus = NULL;
static U32 s_ClusterStatusVersion = 0;

int giLoginShowShardList = -1;
AUTO_CMD_INT(giLoginShowShardList, ShowShardList) ACMD_CMDLINE;

int giShowPlayerTypeChoice = 0;
AUTO_CMD_INT(giShowPlayerTypeChoice, ShowPlayerTypeChoice) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

AUTO_EXPR_FUNC(UIGen);
S32 LoginShowPlayerTypeChoice(void)
{
	return giShowPlayerTypeChoice;
}

// If this is true, we are in show-shard-list mode, but we need a ticket for the login server
bool gbLoginNeedGameTicket = false;

//on the game client, if the server is "localhost" or default and the accountserver has not been set, return "localhost" as the account server
char *DEFAULT_LATELINK_getAccountServer(void);
char *OVERRIDE_LATELINK_getAccountServer(void)
{

	if (accountServerWasSet())
	{
		return DEFAULT_LATELINK_getAccountServer();
	}

	if ( ( eaSize(&gGCLState.ppLoginServerNames) == 0 && eaSize(&gGCLState.eaLoginServers) == 0 )
		|| eaSize(&gGCLState.ppLoginServerNames) == 1 && stricmp(gGCLState.ppLoginServerNames[0], "localhost") == 0
        || eaSize(&gGCLState.eaLoginServers) == 1 && strcmp(gGCLState.eaLoginServers[0]->machineNameOrAddress, "127.0.0.1") == 0 )
	{
		return "localhost";
	}

	return DEFAULT_LATELINK_getAccountServer();
}

AUTO_COMMAND ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void ClickThroughLoginProcess(int iSet)
{
	if (isDevelopmentMode())
	{
		gbClickThroughLoginProcess = iSet;
	}
}

// Login Information for submitting Ticket from Login / Character Creation
static LoginData sLoginData = {0};
U32 LoginGetAccountID(void)
{
	if (GSM_IsStateActiveOrPending(GCL_LOGIN))
		return sLoginData.uAccountID;
	return 0;
}
AUTO_EXPR_FUNC(UIGen);
S32 LoginGetAccessLevel(void)
{
	if (GSM_IsStateActiveOrPending(GCL_LOGIN))
		return sLoginData.iAccessLevel;
	return 0;
}
AUTO_EXPR_FUNC(UIGen);
U32 LoginGetPlayerType(void)
{
	if (GSM_IsStateActiveOrPending(GCL_LOGIN))
		return sLoginData.uAccountPlayerType;
	return 0;
}
const char * LoginGetAccountName(void)
{
	if (GSM_IsStateActiveOrPending(GCL_LOGIN))
		return sLoginData.pAccountName;
	return NULL;
}
AUTO_EXPR_FUNC(UIGen);
const char * LoginGetDisplayName(void)
{
	if (GSM_IsStateActiveOrPending(GCL_LOGIN))
		return sLoginData.pDisplayName;
	return NULL;
}
const char * LoginGetShardInfo(void)
{
	if (GSM_IsStateActiveOrPending(GCL_LOGIN))
		return sLoginData.pShardInfoString;
	return NULL;
}
GameAccountData* LoginGetAccountData(void)
{
	if (GSM_IsStateActiveOrPending(GCL_LOGIN))
    {
        return GET_REF(g_characterSelectionData->hGameAccountData);
    }
	return NULL;
}

// I have no earthly idea where to put this, so it's going here for now - know that I am sorry for doing this
char *OVERRIDE_LATELINK_ShardCommon_GetClusterName(void)
{
	return gGCLState.shardClusterName;
}


//QUICKLOGIN stuff
int g_iQuickLogin = 0;
char gQuickLoginRequestedCharName[256] = ""; //if this is nonempty, then this username will be used by quicklogin
static char sQuickLoginRequestedMapName[256] = ""; //if this is nonempty, then quicklogin will attempt to find a map name containing this string to log into
static char sQuickLoginRequestedMapVariables[1024] = ""; //if this is nonempty, then quicklogin will attempt to set map variables
static Vec3 sQuickLoginPos;
static Vec3 sQuickLoginPYR;
bool sQuickLoginWithPos;
bool sQuickLoginForceCreate;
static int sQuickLoginMapInstanceIndex = 0;
CharacterCreationDataHolder *sCreationData;

// For UGC logging out and returning to the UGC Project Chooser.
bool g_bChoosePreviousCharacterForUGC = false;

static char spPassword[MAX_PASSWORD];

// Defined in gclCommandParse.c
extern int access_override;

static struct {
	bool done;
	F32 alpha;
} login_loading;

//turns on quick login
AUTO_CMD_INT(g_iQuickLogin, quickLogin) ACMD_CMDLINE;

bool gbSkipAccountLogin = false;
AUTO_CMD_INT(gbSkipAccountLogin, autoLogin) ACMD_CMDLINE;

bool gbSkipUsernameEntry = false;
AUTO_CMD_INT(gbSkipUsernameEntry, quickLoginWithAccount) ACMD_CMDLINE;

AUTO_CMD_STRING(gGCLState.loginName, setUsername) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
AUTO_CMD_STRING(gGCLState.loginPassword, setPassword) ACMD_CMDLINE;
AUTO_CMD_INT(gGCLState.eLoginType, setLoginType) ACMD_CMDLINE; // see accountnet.h AccountLoginType for integer values

//a "hello world" file allows us to automatedly verify that the game client is working well enough at least to get to the login 
//screen.
static char *spHelloWorldFileName = NULL;

AUTO_COMMAND ACMD_CMDLINE;
void SetHelloWorldFile(char *pName)
{
	estrCopy2(&spHelloWorldFileName, pName);
}

//turns on quick login with specified character and map
AUTO_COMMAND ACMD_NAME(InitQuickLogin) ACMD_CMDLINE;
void gclInitQuickLogin(int iActive, char *pRequestedCharName, char *pRequestedMapName)
{
	sQuickLoginMapInstanceIndex = 0;
	
	if (iActive)
	{
		g_iQuickLogin = iActive;
		strcpy(gQuickLoginRequestedCharName, pRequestedCharName);
		strcpy(sQuickLoginRequestedMapName, pRequestedMapName);
		sQuickLoginRequestedMapVariables[0] = 0;
		forwardSlashes(sQuickLoginRequestedMapName);
		sQuickLoginWithPos = 0;
	}
	else
	{
		g_iQuickLogin = 0;
		gQuickLoginRequestedCharName[0] = 0;
		sQuickLoginRequestedMapName[0] = 0;
		sQuickLoginRequestedMapVariables[0] = 0;
		sQuickLoginWithPos = 0;
	}	
}

//turns on quick login with specified character and map
AUTO_COMMAND ACMD_NAME(InitQuickLoginUGC) ACMD_CMDLINE;
void gclInitQuickLoginUGC(int iActive)
{
	sQuickLoginMapInstanceIndex = 0;
	
	if (iActive)
	{
		g_iQuickLogin = iActive;
		strcpy(gQuickLoginRequestedCharName, "Foundry Preview");
		sQuickLoginRequestedMapVariables[0] = 0;
		sQuickLoginWithPos = 0;
		s_bLoggingInForUGC = true;
	}
	else
	{
		g_iQuickLogin = 0;
		gQuickLoginRequestedCharName[0] = 0;
		sQuickLoginRequestedMapName[0] = 0;
		sQuickLoginRequestedMapVariables[0] = 0;
		sQuickLoginWithPos = 0;
	}	
}

//turns on quick login with specified character and map and instance ID of map
AUTO_COMMAND ACMD_NAME(InitQuickLoginWithInstanceID) ACMD_CMDLINE;
void gclInitQuickLoginWithInstanceID(int iActive, char *pRequestedCharName, char *pRequestedMapName, int iRequestedInstanceID)
{
	if (iActive)
	{
		g_iQuickLogin = iActive;
		strcpy(gQuickLoginRequestedCharName, pRequestedCharName);
		strcpy(sQuickLoginRequestedMapName, pRequestedMapName);
		sQuickLoginRequestedMapVariables[0] = 0;
		forwardSlashes(sQuickLoginRequestedMapName);	
		sQuickLoginWithPos = 0;
		sQuickLoginMapInstanceIndex = iRequestedInstanceID;
	}
	else
	{
		g_iQuickLogin = 0;
		gQuickLoginRequestedCharName[0] = 0;
		sQuickLoginRequestedMapName[0] = 0;
		sQuickLoginRequestedMapVariables[0] = 0;
		sQuickLoginWithPos = 0;
		sQuickLoginMapInstanceIndex = 0;
	}
}

AUTO_COMMAND ACMD_NAME(InitQuickLoginForceCreate) ACMD_CMDLINE;
void gclInitQuickLoginForceCreate(int iActive)
{
	g_iQuickLogin = iActive;
	sQuickLoginForceCreate = iActive;
}

//turns on quick login with specified account name
AUTO_COMMAND ACMD_NAME(InitQuickLoginWithAccountName) ACMD_CMDLINE;
void gclInitQuickLoginWithAccountName(int iActive, char *pAccountName)
{
	if (iActive)
	{
		g_iQuickLogin = iActive;
		gbSkipUsernameEntry = iActive;
		strcpy(gGCLState.loginName, pAccountName);
		strcpy(gGCLState.loginPassword, pAccountName);
	}
	else
	{
		g_iQuickLogin = 0;
		gbSkipUsernameEntry = false;
	}
}

AUTO_COMMAND ACMD_NAME(InitQuickLoginWithPos);
void gclInitQuickLoginWithPos(int iActive, char *pRequestedCharName, char *pRequestedMapName, Vec3 vPos, Vec3 vRot, char *pMapVariables)
{
	sQuickLoginMapInstanceIndex = 0;

	if (iActive)
	{	
		g_iQuickLogin = iActive;
		strcpy(gQuickLoginRequestedCharName, pRequestedCharName);
		strcpy(sQuickLoginRequestedMapName, pRequestedMapName);
		strcpy(sQuickLoginRequestedMapVariables, pMapVariables);
		forwardSlashes(sQuickLoginRequestedMapName);
		copyVec3(vPos, sQuickLoginPos);
		copyVec3(vRot, sQuickLoginPYR);
		sQuickLoginWithPos = 1;
	}
	else
	{
		g_iQuickLogin = 0;
		gQuickLoginRequestedCharName[0] = 0;
		sQuickLoginRequestedMapName[0] = 0;
		sQuickLoginRequestedMapVariables[0] = 0;
		sQuickLoginWithPos = 0;
	}
}

// Send the packet to initiate play or UGC edit.
void gclLoginSendChosenCharacterInfo(ContainerID characterID, bool loggingInForUGC)
{	
	Packet *pPak;

	pPak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_CHOOSECHARACTER);

    pktSendU32(pPak, characterID);
	pktSendBits(pPak, 1, loggingInForUGC);	

	pktSend(&pPak);
}

// Send the packet to initiate character creation.
void gclLoginSendCharacterCreationData(Login2CharacterCreationData *creationData, bool loggingInForUGC)
{
    Packet *pPak;
    pPak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_CREATECHARACTER);
    pktSendStruct(pPak, creationData, parse_Login2CharacterCreationData);
    pktSendBool(pPak, loggingInForUGC);
    pktSend(&pPak);
}

void gclLogin_GetUnlockedCostumes(PlayerCostumeRef ***pppCostumes)
{
	S32 iDataIdx;
	if(!sCreationData)
		return;

	for(iDataIdx = eaSize(&sCreationData->ppRefs)-1; iDataIdx >= 0; iDataIdx--)
	{
		const char *pchCostumeName = REF_STRING_FROM_HANDLE(sCreationData->ppRefs[iDataIdx]->hCostume);
		if(pchCostumeName && pchCostumeName[0])
		{
			PlayerCostumeRef *pCostume = StructCreate(parse_PlayerCostumeRef);
			SET_HANDLE_FROM_STRING("PlayerCostume", pchCostumeName, pCostume->hCostume);
			eaPush(pppCostumes, pCostume);
		}
	}
}

static bool g_bCommandLineTicket = false;
static U32 g_uCommandLineAccountID;
static U32 g_uCommandLineTicketID;
static char *g_uCommandLineAccountName = NULL;

AUTO_COMMAND ACMD_NAME(AuthTicketNew) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclSetAuthTicketNew(U32 accountid, U32 ticketid)
{
	g_uCommandLineAccountID = accountid;
	g_uCommandLineTicketID = ticketid;
	g_bCommandLineTicket = true;
}

AUTO_COMMAND ACMD_NAME(AuthTicket) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclSetAuthTicket(U32 accountid, U32 ticketid, const char *accountname)
{
	g_uCommandLineAccountID = accountid;
	g_uCommandLineTicketID = ticketid;
	g_uCommandLineAccountName = strdup(accountname);
	g_bCommandLineTicket = true;
}


NetLink *gpLoginLink = NULL;
extern NetLink *gpAccountLink;
PossibleMapChoices *g_pGameServerChoices = NULL;
U32 g_characterSelectionDataVersionNumber = 0;
Login2CharacterSelectionData *g_characterSelectionData = NULL;
EARRAY_OF(Login2CharacterChoice) g_SortedCharacterChoices = NULL;

char *g_pchLoginStatusString;
static char *s_pchInvalidDisplayName;
static char *s_pchLoginFailureString;

#if !PLATFORM_CONSOLE
static const char *getProgrammerRoot(void)
{
	static char buf[MAX_PATH];
	if (!buf[0])
	{
		char *s;
		getExecutableDir(buf); // c:/src/proj/bin
		if (s=strrchr(buf, '/'))
			*s='\0'; // c:/src/proj
		if (s=strrchr(buf, '/'))
			*s='\0'; // c:/src
	}
	return buf;
}

static bool isProgrammerBuild(void)
{
	static bool bCached=false;
	static bool bResult=false;
	if (!bCached)
	{
		bCached = true;
		if (UserIsInGroup("Software"))
		{
			char buf[MAX_PATH];
			strcpy(buf, getProgrammerRoot());
			strcat(buf, "/ProjectList.txt");
			if (fileExists(buf))
			{
				//Looks like we're running out of a source folder
				bResult = true;
			}
		}
	}
	return bResult;
}

static bool hasRecentSourceBackup(void)
{
	static bool bCached=false;
	static bool bResult=false;
	if (!bCached)
	{
		char path[MAX_PATH];
		char buf[MAX_PATH];
		bCached = true;
		strcpy(buf, getProgrammerRoot());
		if (strStartsWith(buf, "C:/"))
		{
			sprintf(path, "//SOMNUS/data/users/%s/%sbackup.hogg", getComputerName(), getFileName(buf));
			if (fileExists(path))
			{
				U32 now = time(NULL);
				U32 filetime = fileLastChanged(path);
				if (now - filetime < 7*24*60*60)
					bResult = true;
			}
		}
	}
	return bResult;
}
#endif

static void corruptSLICursorFixHack(void)
{
	static int sli_cursor_fix_hack=0;

	if (sli_cursor_fix_hack < 60)
	{
		sli_cursor_fix_hack++;
		if (sli_cursor_fix_hack == 60)
		{
			// Once on startup, re-create the cursor in case it is bad, which is happening on SLI systems
			//  (though even with SLI disabled, so we cannot reliably detect it, so do it anyway, since
			//  it can't hurt.
			extern void ui_CursorForceUpdate(void);
			ui_CursorForceUpdate();
		}
	}
}

void gclLoginDisplayDriverWarningAndBuildNumber(void)
{
	int width, height;
	int y;
	int x = 10;

	corruptSLICursorFixHack();

	gfxGetActiveSurfaceSize(&width, &height);
	y = height;
	gfxfont_SetFontEx(&g_font_Sans, 0, 0, 1, 0, 0xffffffff, 0xffffffff);
	if (system_specs.videoDriverState)
	{
		y -= 18*5;
		if (showDevUI())
		{
			gfxfont_PrintMultiline(10, y, 2001.0, 1, 1, TranslateMessageKeyDefault("GameClientLib.OldDriverInternal", "[UNTRANSLATED]Drivers are too old"));
		} else if (system_specs.videoCardVendorID == VENDOR_NV)
		{
			gfxfont_PrintMultiline(10, y, 2001.0, 1, 1, TranslateMessageKeyDefault("GameClientLib.OldDriver_NVIDIA", "[UNTRANSLATED]Drivers are too old"));
		} else if (system_specs.videoCardVendorID == VENDOR_ATI)
		{
			gfxfont_PrintMultiline(10, y, 2001.0, 1, 1, TranslateMessageKeyDefault("GameClientLib.OldDriver_ATI", "[UNTRANSLATED]Drivers are too old"));
		} else if (system_specs.videoCardVendorID == VENDOR_INTEL)
		{
			gfxfont_PrintMultiline(10, y, 2001.0, 1, 1, TranslateMessageKeyDefault("GameClientLib.OldDriver_Intel", "[UNTRANSLATED]Drivers are too old"));
		} else
		{
			gfxfont_PrintMultiline(10, y, 2001.0, 1, 1, TranslateMessageKeyDefault("GameClientLib.OldDriver", "[UNTRANSLATED]Drivers are too old"));
		}
	}
	if (system_specs.isUsingD3DDebug)
	{
		y -= 18*3;
		gfxfont_PrintMultiline(10, y, 2001.0, 1, 1, TranslateMessageKeyDefault("GameClientLib.D3DDebug", "[UNTRANSLATED]Using D3D Debug Runtime, stop!"));
	}

	
	y -= 18;

	{
		char *pVersionStr = NULL;
		F32 sWidth;

		char buf[1024];
		char *s;
		strcpy(buf, GetUsefulVersionString());
		s = strchr(buf, ' ');

		estrStackCreate(&pVersionStr);

		if(isDevelopmentMode()) {
			estrConcatf(&pVersionStr, "SVN Revision: %s", buf);
		} else { // Trim gigantically long version description into just the build version (full version string accessible with /version)
			// If no space, or it appears to be a numeric version (e.g. executable timestamp), do not trim anything
			if (s && (s - buf)>8 && !isdigit((unsigned char)buf[0])) {
				*s = '\0';
			}
			estrConcatf(&pVersionStr, "Version: %s", buf);
		}

		sWidth = gfxfont_StringWidth(gfxfont_GetFont(), 1.0, 1.0, pVersionStr) / 2.0;
		gfxfont_Printf(((width / 2) - sWidth), (gfxfont_StringHeightf(gfxfont_GetFont(), 1.0, 1.0, pVersionStr) + 5), 2001.0, 1, 1, 0, "%s", pVersionStr);
		estrDestroy(&pVersionStr);
	}


#if !PLATFORM_CONSOLE
	if (showDevUI() && !fileWatcherIsRunning())
	{
		y -= 18;
		gfxfont_Printf(10, y, 2001.0, 1, 1, 0, "WARNING: FileWatcher is not running.  Performance will be degraded.  Please enable FileWatcher.");
	}
	if (showDevUI() && isDevelopmentMode())
	{
		if (isProgrammerBuild() && !hasRecentSourceBackup())
		{
			y -= 18;
			gfxfont_Printf(10, y, 2001.0, 1, 1, 0, "   Contact IT if you need assistance in this.");
			y -= 18;
			gfxfont_Printf(10, y, 2001.0, 1, 1, 0, "WARNING: No nightly source code backup detected for %s. Check the scheduled task and %%SRC_DIRS%%.", getProgrammerRoot());
		}
	}
#endif
	if(gclGetProxyHost())
	{
		y -= 18;
		gfxfont_Printf(10, y, 2001.0, 1, 1, 0, "Proxy: %s", gclGetProxyHost());
	}
}

static void gclLoginCheckLoading(F32 fFrameTime)
{
	U32 color;
	if (!login_loading.done)
	{
		if (!gfxLoadingIsStillLoadingEx(0.25, 5, false) || GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_CHARACTER))
		{
			login_loading.done = true;
			gfxLoadingFinishWaiting();
			login_loading.alpha = 1.0;
		}
	}
	if (login_loading.alpha <= 0)
	{
		return;
	}
	color = CLAMP((int)(login_loading.alpha * 255), 0, 255); // Black with alpha
	display_sprite_tex(white_tex, 0, 0, -1, 1000, 1000, color);
	if (login_loading.done)
		login_loading.alpha -= fFrameTime/0.5f; // Fade out in 0.5s

	// Also check to see if a new map was just loaded that needs preloading
	wlPerfEndMiscBudget();
	gfxMaterialPreloadOncePerFrame(true);
	wlPerfStartMiscBudget();
}

void gclLoginRequestRandomNames(GenNameListReq *pReq)
{
	if (gpLoginLink)
	{
		Packet *pak = pktCreate(gpLoginLink, TOLOGIN_REQUEST_RANDOM_NAMES);
		ParserSendStruct(parse_GenNameListReq, pak, pReq);
		pktSend(&pak);
	}
}

//LOGIN2TODO - deal with translating errors from the login server
void gclLoginFail_internal(const char *pErrorString, bool bNotDuringNormalLoginProcess)
{
	gclServerForceDisconnect(pErrorString);

	// Clear any state from UGC loading
	ugcLoadingUpdateState(UGC_LOAD_NONE, 0);
	ugcProjectChooserFree();

	if(gpLoginLink)
	{
		linkRemove_wReason(&gpLoginLink, "gclLoginFail");
	}
	accountValidateCloseAccountServerLink();

	if(!GSM_IsStateActiveOrPending(GCL_LOGIN_FAILED))
	{
		if(!pErrorString)
		{
			pErrorString = TranslateMessageKeyDefault(	LOGINSTATUS_FAILURE_UNKNOWN,
														LOGINSTATUS_FAILURE_UNKNOWN);
		}

		GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOGIN "/" GCL_LOGIN_FAILED);
		estrCopy2(&g_pchLoginStatusString, pErrorString);
		estrCopy2(&s_pchLoginFailureString, pErrorString);

		printf("Login failure: %s\n", pErrorString);

		if (g_isContinuousBuilder && !bNotDuringNormalLoginProcess)
		{
			assertmsgf(0, "Login failed with message: %s", pErrorString);
		}

		// MAKE SURE TO EXIT UGC
	}
}

static void RequestChoicesUpdate_CB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_GAMESERVER)&& !GSM_AreAnyStateChangesRequested() && gpLoginLink && isDevelopmentMode())
	{
		gclLoginRequestPossibleMaps();
	}
}

static void gclLoginReceiveGameAccountNumericPurchaseResult(Packet *pak)
{
	U32 bSuccess = pktGetBits(pak, 1);
	U32 uEntID = pktGetU32(pak);
	const char* pchUGCProductDef = pktGetStringTemp(pak);
	UGCProductViewer_SetPurchaseResult(uEntID, pchUGCProductDef, bSuccess);
}

// Check for any registry keys indicating that a login was in process with one or more of the character choices.
// If there was a login in process for a character than flag it for recommending save login.
void FlagCharacterChoicesForSafeLogin(Login2CharacterChoices *characterChoices)
{
	int i;

	for ( i = eaSize(&characterChoices->characterChoices) - 1; i >= 0; i-- )
	{
        Login2CharacterChoice *characterChoice = characterChoices->characterChoices[i];
		if (regGetAppInt(GetLoginBeganKeyName(characterChoice->savedName), 0))
		{
			characterChoice->isSaveLoginRecommended = true;
		}
	}
}

#define PACKET_CASE(cmdName, packet)	xcase cmdName: PERFINFO_AUTO_START(#cmdName, 1); START_BIT_COUNT(packet, #cmdName);
#define PACKET_CASE_END(packet) STOP_BIT_COUNT(packet); PERFINFO_AUTO_STOP()

static int cmpLastPlayedCharacterChoices(const Login2CharacterChoice** ppA, const Login2CharacterChoice** ppB)
{
	const Login2CharacterChoice* pA = *ppA;
	const Login2CharacterChoice* pB = *ppB;

	return (int)(pB->lastPlayedTime - pA->lastPlayedTime);
}

static void ignoredCB( const char* ignored1, void* ignored2 )
{
}

// forward references
void gclLoginHandleRedirect(bool doRedirect, ContainerID redirectAccountID, U32 redirectIP, U32 redirectPort, U64 transferCookie);
void gclLoginHandleRedirectDone(bool doUGCEdit);

void gclLoginHandleInput(Packet* pak, int cmd, NetLink* link,void *user_data)
{
	PERFINFO_AUTO_START_FUNC();

	switch (cmd)
	{
        // LOGIN2TODO - handle new packet that sends message key.
	PACKET_CASE(LOGINSERVER_TO_CLIENT_LOGIN_FAILED, pak);
		if (!GSM_IsStateActive(GCL_LOGIN_FAILED))
		{
			char *msg = pktGetStringTemp(pak);
			gclLoginFail(msg);
		}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_INVALID_DISPLAYNAME, pak);
		{
			const char *displayName = pktGetStringTemp(pak);
			estrCopy2(&s_pchInvalidDisplayName, displayName);
			GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_INVALID_DISPLAY_NAME);
		}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_LOGIN_INFO, pak);
		{
			LoginData *loginData = pktGetStruct(pak, parse_LoginData);

			if (loginData)
			{
				StructCopyAll(parse_LoginData, loginData, &sLoginData);

				if (gGCLState.loginName[0])
					strcpy(gGCLState.loginField, gGCLState.loginName);

				gGCLState.loginName[0] = 0;
				gGCLState.pwAccountName[0] = 0;
				gGCLState.displayName[0] = 0;

				if(loginData->pAccountName)
					strcpy_trunc(gGCLState.loginName, loginData->pAccountName);
				if(loginData->pPwAccountName)
					strcpy_trunc(gGCLState.pwAccountName, loginData->pPwAccountName);
				if(loginData->pDisplayName)
					strcpy_trunc(gGCLState.displayName, loginData->pDisplayName);
				if(loginData->pShardClusterName)
					strcpy(gGCLState.shardClusterName, loginData->pShardClusterName);
				if(loginData->pUgcShardName)
					ugc_SetShardName(loginData->pUgcShardName);

				StructDestroy(parse_LoginData, loginData);
				timeSetServerDelta(sLoginData.uiServerTimeSS2000);

                // Inform the command parser of the player's access level.
                access_override = sLoginData.iAccessLevel;

                // Set the shard info string.
                SetShardInfoString((char *)sLoginData.pShardInfoString);

				// If you ever have an accesslevel above 0, report the missing glyph errors.
				// This should allow Devs, QA and GMs to report these errors, even if their current
				// character isn't >AL0.
				if( access_override > 0 )
				{
					gfxFontSetReportMissingGlyphErrors(true);
				}
			}
		}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - add to login info packet?
	PACKET_CASE(LOGINSERVER_TO_CLIENT_MICROTRANSACTION_CATEGORY, pak);
	{
		MicroTrans_SetShardCategory(pktGetU32(pak));
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - deprecated
	PACKET_CASE(LOGINSERVER_TO_CLIENT_SELECT_PLAYERTYPE, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_SELECT_PLAYERTYPE");
	}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_DISPLAYNAME_RESPONSE, pak);
		{
			if (GSM_IsStateActive(GCL_LOGIN_INVALID_DISPLAY_NAME))
			{
				char *errorString = pktGetStringTemp(pak);
				if (errorString && *errorString)
				{
					// TODO make this return to display name changing? and display an error
					// TODO(jfw) - localize, because no one else can apparently
					char *fullErrorString = NULL;
					estrPrintf(&fullErrorString, "Display name change error: %s", errorString);
					gclLoginFail(fullErrorString);
					estrDestroy(&fullErrorString);
				}
				else
				{
					// TODO message for success change
					GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_WAITING_FOR_CHARACTER_LIST);
				}
			}
		}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - add to client info packet?
	PACKET_CASE(LOGINSERVER_TO_CLIENT_LOADING_SCREENS, pak);
		{
			LoadScreenDynamic *pLoadScreens = StructCreate(parse_LoadScreenDynamic);

			if(gGCLState.pLoadingScreens)
			{
				StructDestroySafe(parse_LoadScreenDynamic, &gGCLState.pLoadingScreens);
			}

			ParserRecv(parse_LoadScreenDynamic, pak, pLoadScreens, 0);

			gGCLState.pLoadingScreens = pLoadScreens;

		}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - deprecated
    PACKET_CASE(LOGINSERVER_TO_CLIENT_POSSIBLE_CHARACTERS, pak);
        {
            devassertmsg(0, "LOGINSERVER_TO_CLIENT_POSSIBLE_CHARACTERS");
        }
    PACKET_CASE_END(pak);

	PACKET_CASE(LOGIN2_TO_CLIENT_CHARACTER_SELECTION_DATA, pak);
		{
            bool initialLogin;
            if ( g_characterSelectionData )
            {
                StructReset(parse_Login2CharacterSelectionData, g_characterSelectionData);
                initialLogin = false;
            }
            else
            {
                g_characterSelectionData = StructCreate(parse_Login2CharacterSelectionData);
                initialLogin = true;
            }

            ParserRecv(parse_Login2CharacterSelectionData, pak, g_characterSelectionData, 0);
			++g_characterSelectionDataVersionNumber;

            FlagCharacterChoicesForSafeLogin(g_characterSelectionData->characterChoices);
            gclLogin2_CharacterDetailCache_Clear();

            // Make a copy of the possible character choices, and optionally sort it.
            eaCopy(&g_SortedCharacterChoices, &g_characterSelectionData->characterChoices->characterChoices);
			if ( gConf.bLoginCharactersSortByLastPlayed )
			{
                eaQSort(g_SortedCharacterChoices, cmpLastPlayedCharacterChoices);
			}

			if (GSM_IsStateActiveOrPending(GCL_LOGIN_WAITING_FOR_CHARACTER_LIST) || GSM_IsStateActiveOrPending(GCL_LOGIN_SELECT_PLAYERTYPE))
			{
				GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_USER_CHOOSING_CHARACTER);
			}
			else if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_CHARACTER))
			{
				GSM_ResetState(GCL_LOGIN_USER_CHOOSING_CHARACTER);
			}

			// Force resolving of unresolved resource references
			//   Normally the client auto-requests for reference completion, but if login fails then
			//   the requests may get stuck and this unsticks those requests.  It should do no real
			//   work if there are no unresolved references.  We have similar logic on connection to 
			//   game servers.  We need to do this each time we unpack a new set of character choices
			//   to ensure that references for those choices get resolved.
			if ( initialLogin )
			{
				resClientCancelAnyPendingRequests();
				resSyncAllDictionariesToServer();
			}
		}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_CHARACTER_CREATION_DATA, pak);
		if (GSM_IsStateActiveOrPending(GCL_LOGIN_NEW_CHARACTER_WAITING_FOR_DATA))
		{
			estrCopy2(&g_pchLoginStatusString, "");
			if (sCreationData)
			{
				StructDestroy(parse_CharacterCreationDataHolder, sCreationData);
			}
			sCreationData = StructCreate(parse_CharacterCreationDataHolder);

			ParserRecv(parse_CharacterCreationDataHolder, pak, sCreationData, 0);

            GSM_SwitchToState_Complex(GCL_LOGIN_NEW_CHARACTER_WAITING_FOR_DATA "/../" GCL_LOGIN_NEW_CHARACTER_CREATION);
		}
		else
		{
			// TODO(jfw) - localize, because no one else can apparently
			gclLoginFail("Got character creation data at wrong time");
		}	
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_POSSIBLE_GAMESERVERS, pak);		
		{
			PossibleMapChoices *pNewChoices = StructCreate(parse_PossibleMapChoices);
			ParserRecv(parse_PossibleMapChoices, pak, pNewChoices, 0);

			if (!pNewChoices->ppChoices)
			{
				// TODO(jfw) - localize, because no one else can apparently
				gclLoginFail("Got empty gameserver list");
				PACKET_CASE_END(pak);
				break;
			}

			if (!g_pGameServerChoices || StructCompare(parse_PossibleMapChoices, pNewChoices, g_pGameServerChoices, 0, 0, TOK_USEROPTIONBIT_1))
			{
				if (g_pGameServerChoices)
				{
					eaDestroyStruct(&g_pGameServerChoices->ppChoices, parse_PossibleMapChoice);
					g_pGameServerChoices->ppChoices = pNewChoices->ppChoices;
					pNewChoices->ppChoices = NULL;
				}
				else
				{
					g_pGameServerChoices = pNewChoices;
					pNewChoices = NULL;
				}
			}

			if (pNewChoices)
				StructDestroy(parse_PossibleMapChoices, pNewChoices);

			if(!g_bShowAllStaticMaps)
			{
				PossibleMapChoice *pBestChoice = ChooseBestMapChoice(&g_pGameServerChoices->ppChoices);

				if (pBestChoice)
				{
					gclLoginChooseMap(pBestChoice);
				}
			}
			if (GSM_IsStateActiveOrPending(GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST))
			{
				GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_USER_CHOOSING_GAMESERVER);
			}
			TimedCallback_Run(RequestChoicesUpdate_CB,NULL,2.0);
		}
	PACKET_CASE_END(pak);
		
	PACKET_CASE(LOGINSERVER_TO_CLIENT_SEND_REFDICT_DATA_UPDATES, pak);
		resClientProcessServerUpdates(pak);
	PACKET_CASE_END(pak);

	PACKET_CASE(TOCLIENT_GAME_SERVER_ADDRESS, pak);
		assertmsg(0, "Login process out of sync - got GAME_SERVER_ADDRESS in login, not loading\n");	
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_RELOAD_CHARACTER_CHOICES, pak);
		//TODO: make states to reload characters.
		gclLoginFailNotDuringNormalLogin("Characters loaded.");
	PACKET_CASE_END(pak);

	PACKET_CASE(TO_CLIENT_DEBUG_MESSAGE, pak);
		{
			GameDialogGenericMessage("Login Debug Message", pktGetStringTemp(pak));
		}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_HEARTBEAT, pak);
		{
			giLoginQueuePos = pktGetU32(pak);

			if(giLoginQueuePos)
				giLoginQueueSize = pktGetU32(pak);
			else
				giLoginQueueSize = 0;
			if(GSM_IsStateActive(GCL_LOGIN_WAITING_FOR_CHARACTER_LIST))
				GSM_ResetStateTimers(GCL_LOGIN_WAITING_FOR_CHARACTER_LIST);
			else if(GSM_IsStateActive(GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST))
				GSM_ResetStateTimers(GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST);

		}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - when does this get called?
	PACKET_CASE(LOGINSERVER_TO_CLIENT_NEWLY_CREATED_CHARACTER_ID, pak);
	{
		gGCLState.loginCharacterID = GetContainerIDFromPacket(pak);
	}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_NOTIFYSEND, pak);
	{
		U32 eType = pktGetU32(pak);
		const char *pchDisplayString = pktGetStringTemp(pak);
		const char *pchLogicalString = pktGetStringTemp(pak);
		const char *pchTexture = pktGetStringTemp(pak);
		if(pchDisplayString)
			notify_NotifySend(NULL, eType, pchDisplayString, pchLogicalString, pchTexture);
	}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_NOTIFYSEND_STRUCT, pak);
	{
		U32 eType = pktGetU32(pak);
		MessageStruct *pFmt = (MessageStruct*) pktGetStruct(pak, parse_MessageStruct);
		if (pFmt && pFmt->pchKey)
			gclNotifyReceiveMessageStruct(eType, pFmt);
		StructDestroy(parse_MessageStruct, pFmt);
	}
	PACKET_CASE_END(pak);


	PACKET_CASE(LOGINSERVER_TO_CLIENT_RANDOM_NAMES, pak);
	{
		GenNameList *pNameList = StructCreate(parse_GenNameList);
		ParserRecv(parse_GenNameList, pak, pNameList, 0);
		nameGen_ClientReceiveNames(pNameList);
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_FULL_CHARACTER_CHOICE, pak);
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_FULL_CHARACTER_CHOICE");
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_POSSIBLE_UGC_PROJECTS, pak);
		if (GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_UGC_PROJECT))
		{	
			char *pTempString = NULL;
			StructDestroySafe(parse_PossibleUGCProjects, &spPossibleUGCProjects);
			spPossibleUGCProjects = StructCreate(parse_PossibleUGCProjects);
			
			ParserRecv(parse_PossibleUGCProjects, pak, spPossibleUGCProjects, 0);
			if( !ugcProjectChooser_IsOpen() ) {
				ugcProjectChooserInit();
			}
			ugcProjectChooserSetPossibleProjects(spPossibleUGCProjects);

			// QuickLogin is used by the Publish tester to create a new project
			if (g_iQuickLogin)
			{
				gclChooseNewUGCProject( "QuickLoginUGC", NULL, 0 );
			}
		}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_UGC_IMPORT_PROJECT_SEARCH, pak)
		if(GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_UGC_PROJECT))
		{
			StructDestroySafe(parse_PossibleUGCProjects, &spPossibleUGCImports);
			spPossibleUGCImports = StructCreate(parse_PossibleUGCProjects);

			ParserRecv(parse_PossibleUGCProjects, pak, spPossibleUGCImports, 0);

			if( !ugcProjectChooser_IsOpen() ) {
				ugcProjectChooserInit();
			}
			ugcProjectChooserSetImportProjects(spPossibleUGCImports->ppProjects);
		}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_UGCPROJECTSERIES_CREATE_RESULT, pak)
	{
		if(GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_UGC_PROJECT))
		{
			ContainerID newID = pktGetU32( pak );
			ugcSeriesEditor_ProjectSeriesCreate_Result( newID );
		}
	}
	PACKET_CASE_END(pak);
	   

	PACKET_CASE(LOGINSERVER_TO_CLIENT_UGCPROJECTSERIES_UPDATE_RESULT, pak)
	{
		if(GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_UGC_PROJECT))
		{
			bool result = pktGetBool( pak );
			const char* errorMsg = pktGetStringTemp( pak );
			ugcSeriesEditor_ProjectSeriesUpdate_Result( result, errorMsg );
		}
	}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_UGC_REQUEST_MORE_REVIEWS, pak)
		if(GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_UGC_PROJECT))
		{
			ContainerID uProjectID = pktGetU32(pak);
			ContainerID uSeriesID = pktGetU32(pak);
			int iPageNumber = pktGetU32(pak);
			
			StructDestroySafe(parse_UGCProjectReviews, &spReviews);
			spReviews = StructCreate(parse_UGCProjectReviews);
			ParserRecv(parse_UGCProjectReviews, pak, spReviews, 0);

			gclUGC_ReceiveReviewsForPage(uProjectID, uSeriesID, iPageNumber, spReviews);
		}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_CSTORE_UPDATE, pak);
	{
		CStoreUpdate *pUpdate = pktGetStruct(pak, parse_CStoreUpdate);

		switch(pUpdate->eType)
		{
		case kCStoreUpdate_MOTD:
			{
				gclMicroTrans_SetMOTD(pUpdate->pProduct);
				break;
			}
		case kCStoreUpdate_ProductList:
			{
				gclMicroTrans_RecvProductList(pUpdate->pList);
				break;
			}
		case kCStoreUpdate_RemoveKey:
			{
				if(pUpdate->pInfo)
					gclAPCacheRemoveKeyValue(pUpdate->pInfo->pKey);
				
				break;
			}
		case kCStoreUpdate_SetKey:
			{
				if(pUpdate->pInfo)
					gclAPCacheSetKeyValue(pUpdate->pInfo);

				break;
			}
		case kCStoreUpdate_SetKeyList:
			{
				gclAPCacheSetAllKeyValues(pUpdate->pInfoList);
				break;
			}

		case kCStoreUpdate_PointBuyProducts:
			{
				gclMicroTrans_RecvPointBuyProducts(pUpdate->pProductList);
				break;
			}

		case kCStoreUpdate_PaymentMethods:
			{
				PaymentMethodsResponse *pResponse = StructCreate(parse_PaymentMethodsResponse);
				
				pResponse->eaPaymentMethods = pUpdate->ppMethods;
				pUpdate->ppMethods = NULL;
					
				gclMicroTrans_RecvPaymentMethods(pResponse);
				StructDestroy(parse_PaymentMethodsResponse, pResponse);
				break;
			}
			
		default:
			break;
		}

		StructDestroy(parse_CStoreUpdate, pUpdate);
	}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_HERE_IS_EDIT_PERMISSION_COOKIE, pak);
	{
		if(GSM_IsStateActiveOrPending(GCL_LOGIN_WAITING_UGC_EDIT_PERMISSION))
		{	
			if (spChosenUGCProject)
			{
				spChosenUGCProject->iEditQueueCookie = pktGetBits(pak, 32);
			}

			// Loads the Zeni dictionary data into the client
			ugcLoadDictionaries();
			ugcResourceInfoPopulateDictionary();
			zmapLoadClientDictionary();

			// Patch all the UGC-related minimap hoggs so that can be
			// done in parallel with the server starting up.
			FOR_EACH_IN_REFDICT("ZoneMap", ZoneMapInfo, zminfo)
			{
				ResourceInfo* resInfo = ugcResourceGetInfo( "ZoneMap", zmapInfoGetPublicName( zminfo ));
				const char* tags = SAFE_MEMBER( resInfo, resourceTags );
				if( tags && strstri( tags, "UGC" )) {
					char buffer[ MAX_PATH ];
					sprintf( buffer, "bin/geobin/%s/Map_Snap_Mini.Hogg", zmapInfoGetFilename( zminfo ));
					fileLoaderRequestAsyncExec( allocAddFilename( buffer ), FILE_MEDIUM_PRIORITY, false, ignoredCB, NULL );
				}
			}
			FOR_EACH_END;

			// client-side zenis are only the data in UGC
			FOR_EACH_IN_REFDICT("ZoneMapEncounterInfo", ZoneMapEncounterInfo, zeni)
			{
				ZoneMapInfo* zminfo = zmapInfoGetByPublicName( zeni->map_name );
				if( zminfo ) {
					char buffer[ MAX_PATH ];
					sprintf( buffer, "bin/geobin/%s/Map_Snap_Mini.Hogg", zmapInfoGetFilename( zminfo ));
					fileLoaderRequestAsyncExec( allocAddFilename( buffer ), FILE_MEDIUM_PRIORITY, false, ignoredCB, NULL );
				}
			}
			FOR_EACH_END;
			
			ugcLoadingUpdateState(UGC_LOAD_WAITING_FOR_SERVER, 0);
			gclPatchStreamingFastMode();
		}
	}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_HERE_IS_NUM_AHEAD_OF_YOU_IN_EDIT_QUEUE, pak);
	{
		if(GSM_IsStateActiveOrPending(GCL_LOGIN_WAITING_UGC_EDIT_PERMISSION))
		{
			ugcLoadingUpdateState(UGC_LOAD_WAITING_IN_QUEUE, pktGetBits(pak, 32));
		}
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_JOIN_SESSION_SUCCEEDED, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_JOIN_SESSION_SUCCEEDED");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_JOIN_SESSION_FAILED, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_JOIN_SESSION_FAILED");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_JOIN_SESSION_REQUESTSENT, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_JOIN_SESSION_REQUESTSENT");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_CREATE_SESSION_FAILED, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_CREATE_SESSION_FAILED");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_CREATE_SESSION_SUCCEEDED, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_CREATE_SESSION_SUCCEEDED");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_SAVE_SESSION_FAILED, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_SAVE_SESSION_FAILED");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_SAVE_SESSION_SUCCEEDED, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_SAVE_SESSION_SUCCEEDED");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_GAME_SESSION_INFO_SAME_OR_DOES_NOT_EXIST, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_GAME_SESSION_INFO_SAME_OR_DOES_NOT_EXIST");
	}

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_GAME_SESSION_INFO, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_GAME_SESSION_INFO");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_LOBBY_SWITCH_CHARACTERS_RESULT, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_LOBBY_SWITCH_CHARACTERS_RESULT");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_LIST_OF_GAME_SESSIONS_BY_GROUP, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_LIST_OF_GAME_SESSIONS_BY_GROUP");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_GAME_SESSION_COUNT_BY_GROUP, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_GAME_SESSION_COUNT_BY_GROUP");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_GAME_CONTENT_NODE_REWARDS, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_GAME_CONTENT_NODE_REWARDS");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_ENTERED_LOBBY, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_ENTERED_LOBBY");
	}
	PACKET_CASE_END(pak);

    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_EXITED_LOBBY, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_EXITED_LOBBY");
	}
	PACKET_CASE_END(pak);
		
    // LOGIN2TODO - remove
	PACKET_CASE(LOGINSERVER_TO_CLIENT_CHATAUTHDATA, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_CHATAUTHDATA");
	}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_UGC_SEARCH_RESULTS, pak);
	{
		UGCSearchResult* searchResult = pktGetStruct( pak, parse_UGCSearchResult );
		gclUGC_ReceiveSearchResult( searchResult );
		StructDestroy( parse_UGCSearchResult, searchResult );
	}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_UGC_PROJECT_REQUEST_BY_ID_RESULTS, pak);
	{
		UGCProjectList* results = pktGetStruct( pak, parse_UGCProjectList );
		gclUGC_CacheReceiveSearchResult( results );
		StructDestroy( parse_UGCProjectList, results );
	}
	PACKET_CASE_END(pak);	

	PACKET_CASE(LOGINSERVER_TO_CLIENT_GAME_ACCOUNT_NUMERIC_PURCHASE_RESULT, pak);
	{
		gclLoginReceiveGameAccountNumericPurchaseResult(pak);
	}
	PACKET_CASE_END(pak);
	
    // LOGIN2TODO - remove
#ifdef USE_CHATRELAY
	PACKET_CASE(LOGINSERVER_TO_CLIENT_HAS_GAD, pak);
	{
        devassertmsg(0, "LOGINSERVER_TO_CLIENT_HAS_GAD");
	}
	PACKET_CASE_END(pak);
#endif
	PACKET_CASE(LOGINSERVER_TO_CLIENT_REQUIRE_ONETIMECODE, pak);
	{
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_ACCOUNT_ONETIMECODE);
	}
	PACKET_CASE_END(pak);

	PACKET_CASE(LOGINSERVER_TO_CLIENT_SAVENEXTMACHINE, pak);
	{
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_ACCOUNT_SAVENEXTMACHINE);
	}
	PACKET_CASE_END(pak);

    PACKET_CASE(LOGINSERVER_TO_CLIENT_GAMEPERMISSIONDEFS, pak);
    {
        // Receive GamePermissions from the LoginServer.
        GamePermissionDefs *gamePermissionDefs = NULL;
        StructReset(parse_GamePermissionDefs, &g_GamePermissions);
        gamePermissionDefs = pktGetStruct(pak, parse_GamePermissionDefs);
        StructCopyAll(parse_GamePermissionDefs, gamePermissionDefs, &g_GamePermissions);
        StructDestroy(parse_GamePermissionDefs, gamePermissionDefs);
        g_pBasePermission = NULL;
        g_pPremiumPermission = NULL;
        GamePermissions_SetBaseAndPremium();
    }
    PACKET_CASE_END(pak);

    PACKET_CASE(LOGIN2_TO_CLIENT_CHARACTER_DETAIL, pak);
    {
        Login2CharacterDetail *characterDetail = StructCreate(parse_Login2CharacterDetail);
        TextParser_SetGlobalStructCreationComment("Receiving character detail during character selection.");
        ParserRecv(parse_Login2CharacterDetail, pak, characterDetail, RECVDIFF_FLAG_GET_GLOBAL_CREATION_COMMENT);
        gclLogin2_CharacterDetailCache_Add(characterDetail);
    }
    PACKET_CASE_END(pak);

    PACKET_CASE(LOGIN2_TO_CLIENT_REDIRECT, pak);
    {
        bool doRedirect;
        ContainerID redirectAccountID;
        U32 redirectIP;
        U32 redirectPort;
        U64 transferCookie;

        doRedirect = pktGetBool(pak);
        if ( doRedirect )
        {
            redirectAccountID = pktGetU32(pak);
            redirectIP = pktGetU32(pak);
            redirectPort = pktGetU32(pak);
            transferCookie = pktGetU64(pak);
        }
        else
        {
            redirectAccountID = 0;
            redirectIP = 0;
            redirectPort = 0;
            transferCookie = 0;
        }
        gclLoginHandleRedirect(doRedirect, redirectAccountID, redirectIP, redirectPort, transferCookie);
    }
    PACKET_CASE_END(pak);

    PACKET_CASE(LOGIN2_TO_CLIENT_REDIRECT_DONE, pak);
    {
        bool doUGCEdit = pktGetBool(pak);
        gclLoginHandleRedirectDone(doUGCEdit);
    }
    PACKET_CASE_END(pak);

    PACKET_CASE(LOGIN2_TO_CLIENT_CLUSTER_STATUS, pak);
    {
        if ( s_ClusterStatus == NULL )
        {
            s_ClusterStatus = StructCreate(parse_Login2ClusterStatus);
        }
        else
        {
            StructReset(parse_Login2ClusterStatus, s_ClusterStatus);
        }
        ParserRecv(parse_Login2ClusterStatus, pak, s_ClusterStatus, 0);
        s_ClusterStatusVersion++;
    }
    PACKET_CASE_END(pak);

    PACKET_CASE(LOGIN2_TO_CLIENT_LOGIN_FAILED, pak);
    {
        if (!GSM_IsStateActive(GCL_LOGIN_FAILED))
        {
            const char *errorMessageKey = NULL;
            const char *translatedError = NULL;

            errorMessageKey = pktGetStringTemp(pak);
            if ( errorMessageKey )
            {
                translatedError = TranslateMessageKey(errorMessageKey);
            }
            gclLoginFail(translatedError);
        }
    }
    PACKET_CASE_END(pak);

	}

	PERFINFO_AUTO_STOP();
}

static void gclLogin_LoginTransition(void)
{
	if (gfxIsHeadshotServer())
	{
		return;
	}

	if (gbSkipUsernameEntry)
	{
		spPassword[0] = '\0';
		strcpy(spPassword, gGCLState.loginPassword);
		GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOGIN "/" GCL_ACCOUNT_SERVER_CONNECT);
	}
	else if (g_iQuickLogin || gbSkipAccountLogin)
	{
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_SERVER_CONNECT);
	}
	else if(g_bCommandLineTicket)
	{
		printf("Skipping login because ticket was provided on command line\n");
		if(g_uCommandLineAccountName)
		{
			strcpy(gGCLState.loginName, g_uCommandLineAccountName);
			free(g_uCommandLineAccountName);
			g_uCommandLineAccountName = NULL;
		}
		gGCLState.accountID = g_uCommandLineAccountID;
		gGCLState.accountTicketID = g_uCommandLineTicketID;
		g_bCommandLineTicket = false;
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_SERVER_LOAD_WAIT);
	}
	else
	{
		gGCLState.accountID = gGCLState.accountTicketID = 0;
		// Temporary hack to test things on PC
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_ACCOUNT_SERVER_LOGIN);
	}
}

void gclLogin_Enter(void)
{
	if (spHelloWorldFileName)
	{
		FILE *pOutFile = fopen(spHelloWorldFileName, "wt");
		if (!pOutFile)
		{
			assertmsgf(0, "Couldn't open hello world file %s... testing will fail", spHelloWorldFileName);
		}
		else
		{
			fprintf(pOutFile, "Hello, world!");
			fclose(pOutFile);
		}
	}

	gclServerDestroyInputPacket();

	login_loading.done = false;
	login_loading.alpha = 1;
	gfxLoadingStartWaiting();
	gfxNotifyDoneLoading();

	ChatLog_Clear();
	StructDeInit(parse_LoginData, &sLoginData);

	gclServerForceDisconnect("Entered gclLogin");
	gclLogin_LoginTransition();
	gclSetOverrideCameraActive(gclLoginGetCameraController(), NULL);

	// Remove all cached built in channels
	ClientChat_ResetSubscribedChannels(true);
}


void gclLogin_BeginFrame(void)
{
	if ((gpLoginLink && !linkDisconnected(gpLoginLink)) || (gpAccountLink && !linkDisconnected(gpAccountLink)))
	{
		gclServerMonitorConnection();
	}
	// Remove these warnings/messages while creating characters
	// *Must* be visible on initial login screen (for dev) and character selection screen (first
	//   screen seen in production).
	if (!GSM_IsStateActive(GCL_LOGIN_NEW_CHARACTER_CREATION) && !gbNoGraphics)
		gclLoginDisplayDriverWarningAndBuildNumber();
	gclLoginCheckLoading(gGCLState.frameElapsedTime);

}

void gclLogin_EndFrame(void)
{
	gclLibsOncePerFrame();
}

void gclLogin_Leave(void)
{
	if (sCreationData)
	{
		StructDestroy(parse_CharacterCreationDataHolder, sCreationData);
		sCreationData = NULL;
	}
	gclSetGameCameraActive();
	StructDeInit(parse_LoginData, &sLoginData);
	gclSteam_PurchaseActive(false);

	if (!login_loading.done)
	{
		gfxLoadingFinishWaiting();
		login_loading.done = true;
	}
}


//--------------stuff for GCL_ACCOUNT_SERVER_LOGIN

static void gclAccountLogin(const char *pchUser, const char *pchPassword, bool bSaveUsername, bool bEditLogin, AccountLoginType eLoginType)
{
	if (!GSM_IsStateActive(GCL_ACCOUNT_SERVER_LOGIN))
		return;

	if (!pchUser || !pchUser[0])
	{
		GameDialogGenericMessage(TranslateMessageKey("AccountLogin.Title"), TranslateMessageKeyDefault("AccountLogin.NoUsername", "No username was entered!"));
	}
	else if (strlen(pchUser) > ARRAY_SIZE_CHECKED(gGCLState.loginName) - 1)
	{
		GameDialogGenericMessage(TranslateMessageKey("AccountLogin.Title"), "Error: Username is longer than maximum length (127 characters).");
	}
	else if (strlen(pchPassword) > ARRAY_SIZE_CHECKED(spPassword) - 1)
	{
		GameDialogGenericMessage(TranslateMessageKey("AccountLogin.Title"), "Error: Password is longer than maximum length (127 characters).");
	}
	else
	{
		gGCLState.eLoginType = eLoginType;
		spPassword[0] = '\0';
		strcpy(spPassword, pchPassword);
		gGCLState.bSaveLoginUsername = bSaveUsername;
		gGCLState.bEditLogin = bEditLogin;

		gGCLState.pwAccountName[0] = 0;
		strcpy(gGCLState.loginName, pchUser);

		if (gGCLState.loginName[0])
			strcpy(gGCLState.loginField, gGCLState.loginName);
		//strcpy(gGCLState.loginPassword, pchPassword);
		//memset(gGCLState.loginPassword, 0, ARRAY_SIZE_CHECKED(gGCLState.loginPassword));
		gclSaveLoginConfig();

		GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOGIN "/" GCL_ACCOUNT_SERVER_CONNECT); 
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void gclAccountLogin_UsernamePassword(const char *pchUser, const char *pchPassword, bool bSaveUsername, bool bEditLogin)
{
	gclAccountLogin(pchUser, pchPassword, bSaveUsername, bEditLogin, ACCOUNTLOGINTYPE_Default);
}

static bool sbForceEditMode = false;
AUTO_CMD_INT(sbForceEditMode, ForceEditMode) ACMD_CMDLINE;

static bool sbDebugInitialUsername = false;
AUTO_CMD_INT(sbDebugInitialUsername, DebugLoginInitialUsername) ACMD_CMDLINEORPUBLIC;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AccountLogin");
void gclAccountLoginExpr_UsernamePassword(ExprContext *pContext, const char *pchUser, const char *pchPassword, bool bSaveUsername)
{
	gclAccountLogin(pchUser, pchPassword, bSaveUsername, sbForceEditMode, ACCOUNTLOGINTYPE_CrypticAndPW);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PerfectWorldAccountLogin");
void gclAccountLoginExpr_PerfectWorldLogin(ExprContext *pContext, const char *pchUser, const char *pchPassword, bool bSaveUsername)
{
	gclAccountLogin(pchUser, pchPassword, bSaveUsername, sbForceEditMode, ACCOUNTLOGINTYPE_PerfectWorld);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CrypticAccountLogin");
void gclAccountLoginExpr_CrypticLogin(ExprContext *pContext, const char *pchUser, const char *pchPassword, bool bSaveUsername)
{
	gclAccountLogin(pchUser, pchPassword, bSaveUsername, sbForceEditMode, ACCOUNTLOGINTYPE_Cryptic);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AccountLogin_GetInitialUsername");
char *gclAccountLoginExpr_GetInitialUsername(ExprContext *pContext)
{
	if (gGCLState.bSaveLoginUsername || isDevelopmentMode() && !sbDebugInitialUsername)
	{
		if (gGCLState.loginField[0])
			return gGCLState.loginField;
		if (gGCLState.pwAccountName[0])
			return gGCLState.pwAccountName;
		if (gGCLState.loginName[0])
			return gGCLState.loginName;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AccountLogin_GetInitialPassword");
char *gclAccountLoginExpr_GetInitialPassword(ExprContext *pContext)
{
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AccountLogin_GetInitialSaveUsernameState");
bool gclAccountLoginExpr_GetInitialSaveUsernameState(ExprContext *pContext)
{
	return gGCLState.bSaveLoginUsername;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AccountLogin_GetInitialPerfectWorldLogin");
int gclAccountLoginExpr_GetInitialLoginType(void)
{
	// Fix invalid values
	if (gGCLState.eLoginType >= ACCOUNTLOGINTYPE_Max || gGCLState.eLoginType < ACCOUNTLOGINTYPE_Default)
		gGCLState.eLoginType = ACCOUNTLOGINTYPE_Default; 
	return gGCLState.eLoginType;
}

// Return a string with the current login status, or "" if login is
// waiting for user interaction and nothing is going wrong.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Login_GetStatus");
const char *gclLoginExpr_GetStatus(ExprContext *pContext)
{
	return NULL_TO_EMPTY(g_pchLoginStatusString);
}

// Return a string with the failure message from the last login.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Login_GetFailureString");
const char *gclLoginExpr_GetFailureString(ExprContext *pContext)
{
	return NULL_TO_EMPTY(s_pchLoginFailureString);
}

// Return whether the last login failed.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Login_LastLoginFailed");
bool gclLoginExpr_LastLoginFailed(ExprContext *pContext)
{
	return s_pchLoginFailureString && *s_pchLoginFailureString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MapSelection_ChooseMapByName");
bool gclMapSelectionExpr_ChooseMapByName(const char *pchMap)
{
	S32 i;
	if (!g_pGameServerChoices || !GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_GAMESERVER))
		return false;
	for (i = 0; i < eaSize(&g_pGameServerChoices->ppChoices); i++)
	{
		if (!stricmp(pchMap, g_pGameServerChoices->ppChoices[i]->baseMapDescription.mapDescription))
		{
			gclLoginChooseMap(g_pGameServerChoices->ppChoices[i]);
			return true;
		}
	}
	return false;
}

// LOGIN2TODO - remove? Waiting for word from STO.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AccountLogin_SetDefaultPlayerType");
void gclAccountLoginExpr_SetDefaultPlayerType(U32 eType)
{
	gGCLState.eDefaultPlayerType = eType;
}

static S32 s_iTermsOfUse;
// Terms of use behavior. 0 - default, 1 - behave like production mode, 2 - always prompt
AUTO_CMD_INT(s_iTermsOfUse, TermsOfUseOverride);

// Return whether or not this string matches the most recently agreed to terms of use.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AccountLogin_ValidTermsOfUse");
bool gclAccountLoginExpr_LastTermsOfUse(ExprContext *pContext, const char *pchTerms)
{
	// The CB doesn't have to agree to terms of use.
	U32 uiTerms = cryptAdler32(pchTerms, strlen(pchTerms));
	bool bDevelopmentMode = isDevelopmentMode() && !s_iTermsOfUse;
	if (s_iTermsOfUse == 2)
		return false;
	return bDevelopmentMode || g_isContinuousBuilder || (sLoginData.uiLastTermsOfUse == uiTerms);
}

// Accept the terms of use matching this string.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AccountLogin_AcceptTermsOfUse");
void gclAccountLoginExpr_AcceptTermsOfUse(ExprContext *pContext, const char *pchTerms)
{
	if (gpLoginLink)
	{
		U32 uiTerms = cryptAdler32(pchTerms, strlen(pchTerms));
		Packet *pak = pktCreate(gpLoginLink, TOLOGIN_SET_TERMS_OF_USE);
		pktSendU32(pak, uiTerms);
		pktSend(&pak);
	}
}

// Get the name of the computer
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Login_GetDefaultMachineName");
const char *gclLoginExpr_GetDefaultMachineName(void)
{
	const char *pchName = getHostName();
	if (strcmp(pchName, "Error getting hostname"))
		return pchName;
	return "Computer";
}

// Reset terms of use information on the account
AUTO_COMMAND;
void AccountLogin_ResetTermsOfUse(void)
{
	if (gpLoginLink)
	{
		Packet *pak = pktCreate(gpLoginLink, TOLOGIN_SET_TERMS_OF_USE);
		pktSendU32(pak, 0);
		pktSend(&pak);
	}
}

void gclAccountServerLogin_Enter(void)
{
	linkRemove(&gpLoginLink);

	//Clear the CStore's cached data
	//These caches change during login, they must be cleared here instead of gclLogin_Enter.
	// You could log into the game, use the cstore on the login server, and logout back out.  All without ever leaving the GCL_LOGIN state.
	ADCClearProductCache();
	ADCClearDiscountCache();
	AccountProxyClearKeyValueList();
	gclMicroTrans_ClearLists();
	gclSteam_PurchaseActive(false);

	// Disconnect from Chat Relay if connected
	gclChatConnect_Logout();

	if (gbClickThroughLoginProcess)
	{
		if (gGCLState.loginName[0])
		{
			gclAccountLogin(gGCLState.loginName, "", false, false, gGCLState.eLoginType);
		}
		else
		{
			GameDialogGenericMessage("ClickThrough fail", "Can't do ClickThroughLoginProcess, no saved username. Cancelling");
			gbClickThroughLoginProcess = false;
		}
	}
	
}

void gclAccountServerLogin_Leave(void)
{
}

//--------------stuff for GCL_ACCOUNT_SERVER_CONNECT
static U32 g_account_server_connect_failures = 0;

int gclAccountHandleInput(Packet* pak, int cmd, NetLink* link,void *user_data)
{
	switch (cmd)
	{

	case FROM_ACCOUNTSERVER_LOGIN_NEW:
		if (GSM_IsStateActive(GCL_ACCOUNT_SERVER_WAITING_FOR_TICKET))
		{
			gGCLState.accountID = pktGetU32(pak);
			gGCLState.accountTicketID = pktGetU32(pak);
			accountValidateCloseAccountServerLink();
			if(gbLoginNeedGameTicket)
				gbLoginNeedGameTicket = false;
			GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_SERVER_CONNECT);
		}
		break;

	case FROM_ACCOUNTSERVER_LOGIN_FAILED:
	case FROM_ACCOUNTSERVER_FAILED:
		if (!GSM_IsStateActive(GCL_LOGIN_FAILED))
		{
			const char *msg;
			char *estring = NULL;

			// Get failure reason.
			if (cmd == FROM_ACCOUNTSERVER_LOGIN_FAILED)
			{
				LoginFailureCode code = pktGetU32(pak);
				switch (code)
				{
					case LoginFailureCode_NotFound:
					case LoginFailureCode_BadPassword:
						msg = TranslateMessageKeyDefault("LoginStatus_Account_Failure_NotFound", "[UNTRANSLATED]Invalid username or password.");
						break;
					case LoginFailureCode_RateLimit:
						msg = TranslateMessageKeyDefault("LoginStatus_Account_Failure_RateLimit", "[UNTRANSLATED]Too many attempts; please try again later.");
						break;
					case LoginFailureCode_Disabled:
						msg = TranslateMessageKeyDefault("LoginStatus_Account_Failure_Disabled", "[UNTRANSLATED]This account has been disabled.");
						break;
					case LoginFailureCode_DisabledLinked:
						msg = TranslateMessageKeyDefault("LoginStatus_Account_Failure_DisabledLinked",
							"[UNTRANSLATED]Please log in using your Perfect World account name.");
						break;
					case LoginFailureCode_UnlinkedPWCommonAccount:
						msg = TranslateMessageKeyDefault("LoginStatus_Account_Failure_UnlinkedPWCommonAccount",
							"[UNTRANSLATED]Please log in with this Perfect World account using the game Launcher first, before it may be used to log in.");
						break;
					case LoginFailureCode_Banned:
						msg = TranslateMessageKeyDefault("LoginStatus_Account_Failure_Banned", "[UNTRANSLATED]Your account has been banned. Please contact customer support.");
						break;
					case LoginFailureCode_CrypticDisabled:
						gGCLState.accountConflictID = pktGetU32(pak);
						estrStackCreate(&estring);
						if (gGCLState.accountConflictID)
							FormatGameMessageKey(&estring, "LoginStatus_Account_Failure_Cryptic_Disabled", STRFMT_END);
						else
							FormatGameMessageKey(&estring, "LoginStatus_Account_Failure_Disabled", STRFMT_END);
						msg = estring;
						break;
					default:
						estrStackCreate(&estring);
						FormatMessageKey(&estring,"LoginStatus_Account_Failure_Unknown",STRFMT_INT("Code",code),STRFMT_END);
						msg = estring;
				}
			}
			else
				msg = pktGetStringTemp(pak);

			// Close link to Account Server.
			accountValidateCloseAccountServerLink();
			// Clear this flag
			gbLoginNeedGameTicket = false;
			// Handle login failure
			gclLoginFail(msg);
			estrDestroy(&estring);
		}
		break;

	default:
		break;
	}
	return 1;
}

static void gclAccountServerConnectFail(void)
{
	g_account_server_connect_failures += 1;
	if(gclGetProxyHost() && g_account_server_connect_failures >= 2)
	{
		GameDialogGenericMessage(TranslateMessageKey(LOGINSTATUS_DISABLE_PROXY_TITLE), TranslateMessageKey(LOGINSTATUS_DISABLE_PROXY_MESSAGE));
		gclDisableProxy();
	}
}

void gclAccountServerConnect_Enter(void)
{
	AccountValidateData validateData = {0};
	estrCopy2(&g_pchLoginStatusString, TranslateMessageKey(LOGINSTATUS_ACCOUNT_CONNECTING));

	validateData.eLoginType = gGCLState.eLoginType;
	validateData.login_cb = gclAccountHandleInput;
	validateData.pLoginField = gGCLState.loginName;
	validateData.pPassword = spPassword;
	accountValidateInitializeLinkEx(&validateData);
	if (!gpAccountLink)
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_ACCOUNT_NOSERVER));
}
void gclAccountServerConnect_Leave(void)
{
}
void gclAccountServerConnect_BeginFrame(void)
{
	if (!gpAccountLink || linkDisconnected(gpAccountLink) || gbSkipAccountLogin || g_iQuickLogin)
	{
		gclAccountServerConnectFail();
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_ACCOUNT_DISCONNECT));
	}
	else if (linkConnected(gpAccountLink))
	{
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_ACCOUNT_SERVER_WAITING_FOR_TICKET);
	}
	else if (GSM_TimeInState(NULL) >= GCL_LOGIN_TIMEOUT * giLoginTimeoutFalloffFactor && !gGCLState.bNoTimeout)
	{
		linkRemove(&gpAccountLink);
		gclAccountServerConnectFail();
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_ACCOUNT_TIMEOUT));
	}
}

void gclAccountServerWaiting_Enter(void)
{
	accountValidateStartLoginProcess(gGCLState.loginName);
	estrCopy2(&g_pchLoginStatusString, TranslateMessageKey(LOGINSTATUS_ACCOUNT_WAITING));	
}

void gclAccountServerWaiting_BeginFrame(void)
{
	if (GSM_TimeInState(NULL) >= GCL_LOGIN_TIMEOUT * giLoginTimeoutFalloffFactor && !gGCLState.bNoTimeout)
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_ACCOUNT_TIMEOUT));
		return;
	}

	if (linkDisconnected(gpAccountLink))
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_ACCOUNT_DISCONNECT));
	}
}

static void gclShowOTCUI(bool bShow, bool bOneTimeCodeEntry)
{
	UIGen *pGen = ui_GenFind("Login_OneTimeCode", kUIGenTypeNone);
	devassert(pGen);
	if (bShow)
	{
		if (bOneTimeCodeEntry)
			ui_GenSendMessage(pGen, "ShowOneTimeCode");
		else
			ui_GenSendMessage(pGen, "HideOneTimeCode");
		ui_GenAddWindow(pGen);
	}
	else
		ui_GenRemoveWindow(pGen, true);
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_SubmitOneTimeCode);
void gclAccountServerOneTimeCode_Submit(const char *pOneTimeCode, const char *pMachineName)
{
	// LoginServer already has machine ID and account information
	Packet *pak;	
	if (!GSM_IsStateActive(GCL_ACCOUNT_ONETIMECODE))
		return;
	pak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_ONETIMECODE);
	pktSendString(pak, pOneTimeCode);
	pktSendString(pak, pMachineName);
	pktSend(&pak);
	GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_WAITING_FOR_CHARACTER_LIST);
	gclShowOTCUI(false, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_CancelOneTimeCode);
void gclAccountServerOneTimeCode_Cancel(void)
{
	gclShowOTCUI(false, true);
	gclLoginFail("Cancelled."); // TODO(Theo) translation
}

void gclAccountServerOneTimeCode_Enter(void)
{
	estrCopy2(&g_pchLoginStatusString, "");
	gclShowOTCUI(true, true);
}

void gclAccountServerOneTimeCode_BeginFrame(void)
{
	if (GSM_TimeInState(NULL) >= GCL_LOGIN_ONETIMECODE_TIMEOUT * giLoginTimeoutFalloffFactor && !gGCLState.bNoTimeout)
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_ACCOUNT_TIMEOUT));
	}
}

void gclAccountServerOneTimeCode_Leave(void)
{
	gclShowOTCUI(false, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_SubmitSaveNextMachine);
void gclAccountServerSaveNextMachine_Submit(const char *pMachineName)
{
	// LoginServer already has machine ID and account information
	Packet *pak;
	if (!GSM_IsStateActive(GCL_ACCOUNT_SAVENEXTMACHINE))
		return;
	pak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_SAVENEXTMACHINE);
	pktSendString(pak, pMachineName);
	pktSend(&pak);
	GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_WAITING_FOR_CHARACTER_LIST);
	gclShowOTCUI(false, false);
}

void gclAccountServerSaveNextMachine_Enter(void)
{
	estrCopy2(&g_pchLoginStatusString, "");
	gclShowOTCUI(true, false);
}

void gclAccountServerSaveNextMachine_BeginFrame(void)
{
	if (GSM_TimeInState(NULL) >= GCL_LOGIN_ONETIMECODE_TIMEOUT * giLoginTimeoutFalloffFactor && !gGCLState.bNoTimeout)
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_ACCOUNT_TIMEOUT));
	}
}

void gclAccountServerSaveNextMachine_Leave(void)
{
	gclShowOTCUI(false, false);
}

void gclLoginSendRefDictDataRequests(void)
{
	if (resClientAreTherePendingRequests() && linkConnected(gpLoginLink))
	{
		Packet *pak;
		pak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_SEND_REFDICT_DATA_REQUESTS);		
		resClientSendRequestsToServer(pak);
		pktSend(&pak);		
	}
}

static void gclLoginConnect(NetLink* link, void* unused)
{
    gGCLState.bLoginServerConnectPending = false;
}

static void gclLoginLinkDisconnected(NetLink* link, void* unused)
{
	const U32	errorCode = linkGetDisconnectErrorCode(link);
	char		ipString[100];
	char *pDisconnectReason = NULL;

	estrStackCreate(&pDisconnectReason);
	linkGetDisconnectReason(link, &pDisconnectReason);


	linkGetIpListenPortStr(link, SAFESTR(ipString));
	
	log_printf(	LOG_CLIENTSERVERCOMM,
				"LoginServer link disconnected (%s). Reason: %s (socket error %d: %s)",
				ipString,
				pDisconnectReason,
				errorCode,
				sockGetReadableError(errorCode));

	estrDestroy(&pDisconnectReason);

	GSM_DestroyStateTransitionPackets();

#ifdef USE_CHATRELAY
	if (!GSM_IsStateActiveOrPending(GCL_LOADING) && !GSM_IsStateActiveOrPending(GCL_GAMEPLAY))
		gclChatConnect_Logout();
#endif
}

//--------------stuff for GCL_LOGIN_SERVER_LOAD_WAIT
void gclLoginServerLoadWait_Enter(void)
{
	gfxLoadingStartWaiting();
}

void gclLoginServerLoadWait_BeginFrame(void)
{
	if(!gfxLoadingIsStillLoading())
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_SERVER_CONNECT);
}

void gclLoginServerLoadWait_Leave(void)
{
	gfxLoadingFinishWaiting();
}

//--------------stuff for GCL_LOGIN_SERVER_CONNECT
static char login_server_addr[128];
// Sets the login server IP address to use
AUTO_CMD_STRING(login_server_addr, SetLoginServer) ACMD_CMDLINE;

void gclLoginServerConnect_Enter(void)
{
	static bool bFirst = true;
	const char* serverName;
	U32			serverPort;
    int i;
    LoginServerAddress *loginServerAddress;
	
	estrCopy2(&g_pchLoginStatusString, TranslateMessageKey(LOGINSTATUS_LOGIN_CONNECT));
	estrClear(&s_pchLoginFailureString);

	linkRemove(&gpLoginLink);

	assert(eaSize(&gGCLState.ppLoginServerNames) || eaSize(&gGCLState.eaLoginServers));

    if ( s_doRedirect )
    {
        char *tmpIPStr;
        tmpIPStr = makeIpStr(s_redirectIP);
        strcpy(s_redirectIPString, tmpIPStr);
        serverName = s_redirectIPString;
        serverPort = s_redirectPort;
    }
    else if (login_server_addr[0])
    {
		serverName = login_server_addr;
        serverPort = DEFAULT_LOGINSERVER_PORT;
    }
	else
    {
        if ( eaSize(&gGCLState.eaLoginServers) == 0 )
        {
            // If the new style login server list does not exist, then copy from the old style one.  This is here to handle
            //   any scripts or other uses of the client that use -server when invoking a client.
            for ( i = eaSize(&gGCLState.ppLoginServerNames) - 1; i >= 0; i-- )
            {
                loginServerAddress = StructCreate(parse_LoginServerAddress);
                loginServerAddress->shardName = NULL;
                loginServerAddress->machineNameOrAddress = StructAllocString(gGCLState.ppLoginServerNames[i]);
                loginServerAddress->portNum = DEFAULT_LOGINSERVER_PORT;
                eaPush(&gGCLState.eaLoginServers, loginServerAddress);
            }
        }

        if ( eaSize(&gGCLState.eaLoginServers) )
        {
            // New style login server selection that supports multiple loginservers per machine.
            const char *lastShardNamePooled = allocAddString(GamePrefGetString("Login.LastShard", NULL));
            static U32 *loginServerIndicesForShard = NULL;
            int numLoginServersOnLastShard;

            if ( gGCLState.bLoginServerConnectPending )
            {
                // We tried to connect already and it failed.  Just go to the next entry in the list.
                gGCLState.iLoginServerIndex++;
                if ( gGCLState.iLoginServerIndex >= eaSize(&gGCLState.eaLoginServers) )
                {
                    gGCLState.iLoginServerIndex = 0;
                }
            }
            else
            {
                // This is either the first time for this client connecting, or the last connection was successful.

                // Build an array of indices into the eaLoginServers for all login servers on the last shard the player played on.
                ea32ClearFast(&loginServerIndicesForShard);
                if ( lastShardNamePooled != NULL )
                {
                    for ( i = eaSize(&gGCLState.eaLoginServers) - 1; i >= 0; i-- )
                    {
                        if ( gGCLState.eaLoginServers[i]->shardName == lastShardNamePooled )
                        {
                            ea32Push(&loginServerIndicesForShard, i);
                        }
                    }
                }

                numLoginServersOnLastShard = ea32Size(&loginServerIndicesForShard);
                if ( numLoginServersOnLastShard > 1 )
                {
                    // Multiple login servers on shard, so pick one at random.
                    gGCLState.iLoginServerIndex = loginServerIndicesForShard[timeSecondsSince2000() % numLoginServersOnLastShard];
                }
                else if ( numLoginServersOnLastShard == 1 )
                {
                    // Exactly one loginserver on the shard, so pick it.
                    gGCLState.iLoginServerIndex = loginServerIndicesForShard[0];
                }
                else
                {
                    // No login servers from the last played shard, so pick from the entire list.
                    gGCLState.iLoginServerIndex = timeSecondsSince2000() % eaSize(&gGCLState.eaLoginServers);
                }
            }

            loginServerAddress = gGCLState.eaLoginServers[gGCLState.iLoginServerIndex];
            serverName = loginServerAddress->machineNameOrAddress;
            serverPort = loginServerAddress->portNum;
        }
        else
        {
            // Fail due to empty list of login servers.
            gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_NOSERVER));
            return;
        }
    }

	log_printf(	LOG_CLIENTSERVERCOMM,
				"LoginServer link attempting connect: %s:%d",
				serverName,
				serverPort);

    gGCLState.bLoginServerConnectPending = true;
	gpLoginLink = commConnect(	commDefault(),
								LINKTYPE_UNSPEC,
								LINK_FORCE_FLUSH,
								serverName,
								serverPort,
								gclLoginHandleInput,
								gclLoginConnect,
								gclLoginLinkDisconnected,
								0);

	if (gpLoginLink)
    {
		linkSetKeepAlive(gpLoginLink);
    }
	else
    {
        gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_NOSERVER));
    }
}

void gclLoginServerConnect_BeginFrame(void)
{
	Packet *pak;
	char *affiliate = "";

	if (linkConnected(gpLoginLink))
	{
		gGCLState.connectedLoginServerIP = linkGetSAddr(gpLoginLink);
		gGCLState.connectedLoginServerIP = endianSwapIfBig(U32, gGCLState.connectedLoginServerIP);

        if ( s_doRedirect )
        {
            pak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_REDIRECT);
            pktSendU32(pak, s_redirectAccountID);
            pktSendU64(pak, s_transferCookie);
            pktSendBool(pak, gGCLState.bNoTimeout);
            pktSend(&pak);
            GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_WAITING_FOR_REDIRECT_DONE);
            s_doRedirect = false;
        }
        else
        {
            pak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_BEGIN_LOGIN);
		    pktSendBits(pak,1,gGCLState.bNoTimeout);
		    pktSendU32(pak, locGetLanguage(getCurrentLocale())); // Send locale on login

		    if ((g_iQuickLogin && !gbSkipUsernameEntry) || (gbSkipAccountLogin && !gGCLState.accountID))
		    {
			    pktSendString(pak, gGCLState.loginName);
		    }
		    else
		    {
			    pktSendString(pak, ACCOUNT_FASTLOGIN_LABEL); // to indicate that IDs are incoming
			    pktSendU32(pak, gGCLState.accountID);
			    pktSendU32(pak, gGCLState.accountTicketID);
			    pktSendString(pak, getMachineID());
			    gbSkipAccountLogin = 0;
		    }

            pktSendU32(pak, gGCLState.executableCRC);

            // Send affiliate tracking data
		    if(gclSteamID())
		    {
			    affiliate = "Steam";
		    }
		    pktSendString(pak, affiliate);

		    pktSend(&pak);

		    // Cleanup some runtime data for GCL_LOGIN
		    memset(spPassword, 0, ARRAY_SIZE_CHECKED(spPassword));

		    GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_WAITING_FOR_CHARACTER_LIST);
        }
	}
	if(	!gpLoginLink
		||
		linkDisconnected(gpLoginLink)
		||
		GSM_TimeInState(NULL) >= GCL_LOGIN_TIMEOUT * giLoginTimeoutFalloffFactor &&
		!gGCLState.bNoTimeout)
	{
		linkRemove(&gpLoginLink);
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_TIMEOUT));
	}
}

void gclLoginSelectPlayerType_Enter(void)
{
	estrCopy2(&g_pchLoginStatusString, "");
}


//--------------stuff for GCL_LOGIN_WAITING_FOR_CHARACTER_LIST
void gclLoginWaitingForCharacterList_Enter(void)
{
	estrCopy2(&g_pchLoginStatusString, TranslateMessageKey(LOGINSTATUS_LOGIN_CHARACTERS));
}

void gclLoginWaitingForCharacterList_BeginFrame(void)
{
	gclLoginSendRefDictDataRequests();

	if (GSM_TimeInState(NULL) >= GCL_LOGIN_TIMEOUT * giLoginTimeoutFalloffFactor && !gGCLState.bNoTimeout)
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_TIMEOUT));
		return;
	}

	if (linkDisconnected(gpLoginLink))
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_DISCONNECT));
	}
}

//------------------stuff for GCL_LOGIN_USER_CHOOSING_CHARACTER

//---these things are shared between GCL_LOGIN_USER_CHOOSING_CHARACTER and GCL_LOGIN_USER_CHOOSING_GAMESERVER

const char *
gclLoginGetChosenCharacterName(void)
{
    if ( s_chosenCharacter )
    {
        return s_chosenCharacter->savedName;
    }
    else if ( s_characterCreationData )
    {
        return s_characterCreationData->name;
    }
    return NULL;
}

const char *
gclLoginGetChosenCharacterAllegiance(void)
{
    if ( s_chosenCharacter )
    {
        return Login2_GetAllegianceFromCharacterChoice(s_chosenCharacter);
    }
    else if ( s_characterCreationData )
    {
        return s_characterCreationData->allegianceName;
    }
    return NULL;
}

bool
gclLoginGetChosenCharacterUGCEditAllowed(void)
{
    if ( s_chosenCharacter )
    {
        return s_chosenCharacter->isUGCEditAllowed;
    }
    else if ( s_characterCreationData )
    {
        // LOGIN2TODO - this is a bit hacky
        return s_characterCreationData->virtualShardID == 1;
    }

    return false;
}

NOCONST(Entity) *gclLoginGetChosenEntity(void)
{
    if ( s_chosenCharacter )
    {
        return CONTAINER_NOCONST(Entity, gclLogin2_CharacterDetailCache_GetEntity(s_chosenCharacter->containerID));
    }
    return NULL;
}

bool gclLoginChooseCharacter(Login2CharacterChoice *chosenCharacter)
{
    s_chosenCharacter = chosenCharacter;

    // Save the shard we are playing on.
    GamePrefStoreString("Login.LastShard", chosenCharacter->shardName);

	if (g_iQuickLogin && s_bLoggingInForUGC)
	{
        // LOGIN2TODO - is this right?
		s_chosenCharacter->virtualShardID = 1;
	}

	return true;
}

bool gclLoginCreateCharacter(Login2CharacterCreationData *characterCreationData)
{
    // Save the name of the shard we are going to.
    if ( characterCreationData->shardName && characterCreationData->shardName[0] )
    {
        GamePrefStoreString("Login.LastShard", characterCreationData->shardName);
    }

    // If there is previous character creation data, free it.
    if ( s_characterCreationData )
    {
        StructDestroy(parse_Login2CharacterCreationData, s_characterCreationData);
    }

    // Copy new character creation data.
    s_characterCreationData = StructClone(parse_Login2CharacterCreationData, characterCreationData);

    if (g_iQuickLogin && s_bLoggingInForUGC)
    {
        // LOGIN2TODO - is this right?
        s_characterCreationData->virtualShardID = 1;
    }

    return true;
}

void gclLoginRequestCharacterDelete(Login2CharacterChoice *characterChoice)
{
	if (characterChoice)
	{
		Packet *pPak;

		pPak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_DELETE_CHARACTER);
        pktSendU32(pPak, characterChoice->containerID);
		pktSend(&pPak);
	}
}

// change the characters name from character selection screen
void gclLoginRequestChangeName(ContainerID playerID, const char *newName, bool bBadName)
{
    Packet *pPak;

	pPak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_RENAME_CHARACTER);
    pktSendU32(pPak, playerID);
    pktSendBool(pPak, bBadName);
    pktSendString(pPak, newName);
	pktSend(&pPak);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool gclLoginChooseCharacterByName(SA_PARAM_NN_STR const char *characterName, bool forceCreate)
{
	S32 i;
	Login2CharacterChoice *characterChoice = NULL;
    Login2CharacterCreationData *characterCreationData = NULL;

	if(!g_characterSelectionData)
	{
		return false;
	}
	
    for ( i = eaSize(&g_characterSelectionData->characterChoices->characterChoices) - 1; i >= 0; i-- )
	{
        characterChoice = g_characterSelectionData->characterChoices->characterChoices[i];

		if ( !strcmp(characterName, characterChoice->savedName) )
		{
			if(forceCreate)
			{
				gclLoginRequestCharacterDelete(characterChoice);
				GSM_ResetState(GCL_LOGIN_USER_CHOOSING_CHARACTER);
				return false;
			}

			gclLoginChooseCharacter(characterChoice);
			return true;
		}
	}

    characterCreationData = StructCreate(parse_Login2CharacterCreationData);
    gclQuickPlay_FillDefaultCharacter(characterCreationData);
    characterCreationData->name = StructAllocString(characterName);
    gclLoginCreateCharacter(characterCreationData);
	return true;
}

static void gclLoginUserChoosingCharacter_Enter(void)
{
    Login2CharacterCreationData *characterCreationData = gclQuickPlay_GetCharacterCreationData();
    Packet *pak;

    s_chosenCharacter = NULL;
    if ( s_characterCreationData )
    {
        StructDestroy(parse_Login2CharacterCreationData, s_characterCreationData);
        s_characterCreationData = NULL;
    }

	if (!g_iQuickLogin)
	{
		s_bLoggingInForUGC = false;
	}

	if (!GSM_AreAnyStateChangesRequested())
	{	
		GSM_SwitchToState_Complex(GCL_LOGIN_USER_CHOOSING_CHARACTER "/" GCL_LOGIN_USER_CHOOSING_EXISTING);
	}

	estrCopy2(&g_pchLoginStatusString, "");

	if (characterCreationData)
	{
		int i;
        for ( i = eaSize(&g_characterSelectionData->characterChoices->characterChoices) - 1; i >= 0; i-- )
        {
            Login2CharacterChoice *characterChoice = g_characterSelectionData->characterChoices->characterChoices[i];

            if (stricmp(characterChoice->savedName, characterCreationData->name) == 0)
			{
				gclLoginRequestCharacterDelete(characterChoice);
				break;
			}
		}
		if (i < 0)
		{
			// Duplicate not found, create new one
			s_characterCreationData = characterCreationData;
		}
	}

    pak = pktCreate(gpLoginLink, TOLOGIN_GO_TO_CHARACTER_SELECT);	
    pktSend(&pak);
}

void gclLoginUserChoosingCharacter_BeginFrame(void)
{
	gclLoginSendRefDictDataRequests();

	if (linkDisconnected(gpLoginLink) || !gpLoginLink)
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_DISCONNECT));
		return;
	}

	if(g_bChoosePreviousCharacterForUGC) // take previous choice of character and auto choose it if logging out to UGC project chooser
	{
		int i;
        for ( i = eaSize(&g_characterSelectionData->characterChoices->characterChoices) - 1; i >= 0; i-- )
        {
            Login2CharacterChoice *characterChoice = g_characterSelectionData->characterChoices->characterChoices[i];

			if ( characterChoice->containerID == gGCLState.loginCharacterID
					&& (0 == stricmp_safe(characterChoice->savedName, gGCLState.loginCharacterName)))
			{
				s_chosenCharacter = characterChoice;
				s_bLoggingInForUGC = true;
			}
        }
		g_bChoosePreviousCharacterForUGC = false;
	}

	if(s_chosenCharacter)
	{
		gGCLState.loginCharacterID = s_chosenCharacter->containerID;

		// Send the selected character back to the login server

		gclLoginSendChosenCharacterInfo(s_chosenCharacter->containerID, s_bLoggingInForUGC);

		strcpy(gGCLState.loginCharacterName, s_chosenCharacter->savedName);
			
        GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_WAITING_FOR_REDIRECT);
	}
    else if ( s_characterCreationData && s_characterCreationData->name)
    {
        gGCLState.loginCharacterID = 0;
        gclLoginSendCharacterCreationData(s_characterCreationData, s_bLoggingInForUGC);

        strcpy(gGCLState.loginCharacterName, s_characterCreationData->name);

        GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_WAITING_FOR_REDIRECT);
    }
}

void gclLoginUserChoosingExisting_Enter(void)
{
	bool bCharForceDeleted = false;	
    Login2CharacterChoice *characterChoice;

	if (gbClickThroughLoginProcess)
	{
		if (eaSize(&g_characterSelectionData->characterChoices->characterChoices) == 0)
		{
			GameDialogGenericMessage("ClickThrough fail", "Can't do ClickThroughLoginProcess, no characters exist. Cancelling.");
			gbClickThroughLoginProcess = false;
		}
		else
		{
			s_chosenCharacter = g_characterSelectionData->characterChoices->characterChoices[0];
		}
	}
	else if (g_iQuickLogin)
	{
		if (eaSize(&g_characterSelectionData->characterChoices->characterChoices) < g_iQuickLogin)
		{
			GSM_SwitchToState_Complex(GCL_LOGIN_USER_CHOOSING_CHARACTER "/" GCL_LOGIN_NEW_CHARACTER_WAITING_FOR_DATA);
		}
		else if (gQuickLoginRequestedCharName[0])
		{
			int i;

            for ( i = eaSize(&g_characterSelectionData->characterChoices->characterChoices) - 1; i >= 0; i-- )
            {
                characterChoice = g_characterSelectionData->characterChoices->characterChoices[i];

				if (!stricmp(characterChoice->savedName, gQuickLoginRequestedCharName))
				{
					if(sQuickLoginForceCreate)
					{
						printf( "QuickLogin deleting existing character with name (%s)!\n",
								gQuickLoginRequestedCharName);
						gclLoginRequestCharacterDelete(characterChoice);
						bCharForceDeleted = true;
					}
					else
					{
						printf(	"QuickLogging in with the exact character that I wanted (%s)!\n",
							gQuickLoginRequestedCharName);
						s_chosenCharacter = characterChoice;
					}
					break;
				}
			}

			if(!s_chosenCharacter && !sQuickLoginForceCreate)
			{
                for ( i = eaSize(&g_characterSelectionData->characterChoices->characterChoices) - 1; i >= 0; i-- )
                {
                    characterChoice = g_characterSelectionData->characterChoices->characterChoices[i];

					if (strstri(characterChoice->savedName, gQuickLoginRequestedCharName))
					{
						printf(	"QuickLogging in with different character (%s) than I wanted (%s)!\n",
								characterChoice->savedName,
								gQuickLoginRequestedCharName);
						s_chosenCharacter = characterChoice;
						break;
					}
				}
			}
			
			if(sQuickLoginForceCreate && bCharForceDeleted)
			{
				GSM_ResetState(GCL_LOGIN_USER_CHOOSING_CHARACTER);
			}
            else if (!s_chosenCharacter)
			{
				GSM_SwitchToState_Complex(GCL_LOGIN_USER_CHOOSING_CHARACTER "/" GCL_LOGIN_NEW_CHARACTER_WAITING_FOR_DATA);
			}
		}
		else
		{
			s_chosenCharacter = g_characterSelectionData->characterChoices->characterChoices[g_iQuickLogin-1];
		}
	}
}

void gclLoginUserChoosingExisting_Leave(void)
{
}

static void gclLoginNewCharacterWaitingForData_Enter(void)
{
	Packet *pak;

	pak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_GET_CHARACTER_CREATION_DATA);
	pktSend(&pak);
	estrCopy2(&g_pchLoginStatusString, TranslateMessageKey(LOGINSTATUS_LOGIN_NEWCHARACTER));
}

// Import a character from a container file on disk.
AUTO_COMMAND ACMD_NAME(ImportCharacter) ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9);
char* gclLoginImportCharacter(const char *fileName, bool overwriteAccountData)
{
	if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING))
	{
		Packet *pPak = pktCreate(gpLoginLink, TOLOGIN_TODB_IMPORT_CONTAINER);
		pktSendString(pPak, fileName);
		pktSendBool(pPak, overwriteAccountData);
		pktSend(&pPak);

		return "Asking login server to load character.";
	}
	else
	{
		return "You must be on the character selection screen to run this command.";
	}
}

//This command changes the accoundid and privateaccountname in the Entity's Player struct
//for all EntityPlayers matching the publicAccountName specified.
//Unless deemed safe by a higher authority, this command will remain unavailable on production shards.
AUTO_COMMAND ACMD_NAME(EnslaveCharacters) ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9);
char * gclDominateSouls(const char *publicAccountName)
{
	static char buf[MAX_NAME_LEN];
	if (isDevelopmentMode())
	{
		if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING))
		{
			Packet *pPak = pktCreate(gpLoginLink, TOLOGIN_DOMINATE_SOULS);
			pktSendString(pPak, publicAccountName);
			pktSend(&pPak);
			sprintf(buf, "Changing account ownership for all characters in public account: %s\n", publicAccountName);

			return buf;
		}
		else
		{
			return "You must be on the character selection screen to run this command.";
		}
	}
	else
	{
		return "Kelvin has deemed your soul unworthy.";
	}
}

static bool s_enableCharacterCreationVideo = true;
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EnableCharacterCreationVideo);
void gclLogin_ExprEnableCharacterCreationVideo(bool enable)
{
    s_enableCharacterCreationVideo = enable;
}

static void gclLoginCreateCharacter_Enter(void)
{
    // LOGIN2 - got rid of code here for quick login that seemed to always be overridden by HandleQuickLoginCreate() 
    if (gConf.pchCharCreateVideoPath && gConf.pchCharCreateVideoPath[0] && s_enableCharacterCreationVideo)
    {
        gclPlayVideo_Start(gConf.pchCharCreateVideoPath, 0, true);
    }
}

static void gclLoginCreateCharacter_BeginFrame(void)
{
	gclLoginSendRefDictDataRequests();
}

//--------------stuff for GCL_LOGIN_WAITING_FOR_REDIRECT

void gclLoginHandleRedirect(bool doRedirect, ContainerID redirectAccountID, U32 redirectIP, U32 redirectPort, U64 transferCookie)
{
    if ( doRedirect )
    {
        s_doRedirect = true;
        s_redirectAccountID = redirectAccountID;
        s_redirectIP = redirectIP;
        s_redirectPort = redirectPort;
        s_transferCookie = transferCookie;
    }
    else
    {
        s_doRedirect = false;
        if(s_bLoggingInForUGC)
        {
            GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_USER_CHOOSING_UGC_PROJECT);
        }
        else
        {
            GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST);
        }
    }
}

void gclLoginWaitingForRedirect_Enter(void)
{
    estrCopy2(&g_pchLoginStatusString, TranslateMessageKey(LOGINSTATUS_WAITING_FOR_REDIRECT));
}

void gclLoginWaitingForRedirect_BeginFrame(void)
{
    if (GSM_TimeInState(NULL) >= GCL_LOGIN_TIMEOUT * giLoginTimeoutFalloffFactor && !gGCLState.bNoTimeout)
    {
        gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_TIMEOUT));
        return;
    }

    if (linkDisconnected(gpLoginLink))
    {
        if ( s_doRedirect )
        {
            // Wait for the redirect packet to arrive and the previous link to be disconnected before connecting to the new login server.
            GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_SERVER_CONNECT);
        }
        else
        {
            gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_DISCONNECT));
        }
    }

}

//--------------stuff for GCL_LOGIN_WAITING_FOR_REDIRECT_DONE

void gclLoginHandleRedirectDone(bool doUGCEdit)
{
    devassertmsg(doUGCEdit == s_bLoggingInForUGC, "Client and server disagree about doign UGC after login redirect");

    if(doUGCEdit)
    {
        GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_USER_CHOOSING_UGC_PROJECT);
    }
    else
    {
        GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST);
    }
}

void gclLoginWaitingForRedirectDone_Enter(void)
{
    estrCopy2(&g_pchLoginStatusString, TranslateMessageKey(LOGINSTATUS_WAITING_FOR_REDIRECT_DONE));
}

void gclLoginWaitingForRedirectDone_BeginFrame(void)
{
    if (GSM_TimeInState(NULL) >= GCL_LOGIN_TIMEOUT * giLoginTimeoutFalloffFactor && !gGCLState.bNoTimeout)
    {
        gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_TIMEOUT));
        return;
    }

    if (linkDisconnected(gpLoginLink))
    {
        gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_DISCONNECT));
    }


}

//--------------stuff for GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST
void gclLoginRequestPossibleMaps(void)
{
	if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_GAMESERVER) ||
		GSM_IsStateActive(GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST))
	{
		MapSearchInfo mapSearch = {0};
		Packet *pPak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_REQUESTMAPSEARCH);

		if (sQuickLoginRequestedMapName[0])
		{			
			mapSearch.debugPosLogin = true;
			mapSearch.baseMapDescription.mapDescription = allocAddString(sQuickLoginRequestedMapName);
			mapSearch.baseMapDescription.mapVariables = allocAddString(sQuickLoginRequestedMapVariables);
			if (sQuickLoginWithPos)
			{
				copyVec3(sQuickLoginPos, mapSearch.baseMapDescription.spawnPos);
				copyVec3(sQuickLoginPYR, mapSearch.baseMapDescription.spawnPYR);
			}
			else
			{
				mapSearch.baseMapDescription.spawnPoint = allocAddString(START_SPAWN);
			}

			mapSearch.baseMapDescription.mapInstanceIndex = sQuickLoginMapInstanceIndex;
		}
		else
		{
			mapSearch.developerAllStatic = g_bShowAllStaticMaps;
		}

		mapSearch.safeLogin = !!gbSafeLogin;

		ParserSend(parse_MapSearchInfo, pPak, NULL, &mapSearch, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
		pktSend(&pPak);
	}
}

void gclLoginWaitingForGameserverList_Enter(void)
{
	estrCopy2(&g_pchLoginStatusString, TranslateMessageKey(LOGINSTATUS_LOGIN_MAPS));
	gclLoginRequestPossibleMaps();
}

void gclLoginWaitingForGameserverList_BeginFrame(void)
{
	if (GSM_TimeInState(NULL) >= GCL_LOGIN_TIMEOUT * giLoginTimeoutFalloffFactor && !gGCLState.bNoTimeout)
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_TIMEOUT));
		return;
	}

	if (linkDisconnected(gpLoginLink))
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_DISCONNECT));
	}

	if(giLoginQueuePos)
		estrPrintf(&g_pchLoginStatusString, "Queue position: %u", giLoginQueuePos);
}

void gclLoginWaitingForGameserverList_Exit(void)
{
	if(giLoginQueuePos)
	{
		estrPrintf(&g_pchLoginStatusString, "");
	}
	giLoginQueuePos = 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("loginQueuePosition");
int exprLoginQueuePosition(void)
{
	return giLoginQueuePos;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("loginQueueSize");
int exprLoginQueueSize(void)
{
	if(giLoginQueuePos)
		return giLoginQueueSize;
	return 0;
}

//------------------stuff for GCL_LOGIN_USER_CHOOSING_GAMESERVER

static PossibleMapChoice *s_pChosenMap = NULL;

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool gclLoginChooseMapByName(SA_PARAM_NN_STR const char *pMapName, int iInstanceIndex, bool bNewMap, bool bNewPartition)
{
	S32 i;
	PossibleMapChoice *pChoice = NULL;

	if(!g_pGameServerChoices)
	{
		return false;
	}

	for(i = 0; i < eaSize(&g_pGameServerChoices->ppChoices); ++i)
	{
		PossibleMapChoice *pPossibleChoice = g_pGameServerChoices->ppChoices[i];

		if(!stricmp(pMapName, pPossibleChoice->baseMapDescription.mapDescription) && !pPossibleChoice->bNotALegalChoice)
		{
			if(iInstanceIndex && pPossibleChoice->baseMapDescription.mapInstanceIndex == iInstanceIndex)
			{
				// If we find the requested instance, break immediately
				pChoice = pPossibleChoice;
				break;
			}
			else if(bNewPartition)
			{
				if(pPossibleChoice->eChoiceType == MAPCHOICETYPE_NEW_PARTITION_ON_SPECIFIED_SERVER)
				{
					// If we asked for a new partition, there's only one magical choice for that setting
					pChoice = pPossibleChoice;
					break;
				}
				else if(!pChoice && pPossibleChoice->bNewMap)
				{
					pChoice = pPossibleChoice;
					continue;
				}
			}
			else if(bNewMap)
			{
				if(pPossibleChoice->bNewMap)
				{
					pChoice = pPossibleChoice;
					break;
				}
				else if(!pChoice)
				{
					pChoice = pPossibleChoice;
					continue;
				}
			}
			else if(!pChoice || (pChoice->bNewMap && !pPossibleChoice->bNewMap) ||
				(pChoice->eChoiceType == MAPCHOICETYPE_NEW_PARTITION_ON_SPECIFIED_SERVER && pPossibleChoice->eChoiceType != MAPCHOICETYPE_NEW_PARTITION_ON_SPECIFIED_SERVER))
			{
				pChoice = pPossibleChoice;
				continue;
			}
		}
	}
	
	if(pChoice)
	{
		gclLoginChooseMap(pChoice);
		return true;
	}

	return false;
}

void gclLoginChooseMap(PossibleMapChoice *pMap)
{
	if ( s_pChosenMap )
	{
		StructDestroy(parse_PossibleMapChoice, s_pChosenMap);
	}
	s_pChosenMap = StructClone(parse_PossibleMapChoice, pMap);
}

void gclLoginUserChoosingGameserver_Enter(void)
{
	assertmsg(g_pGameServerChoices, "Login state out of sync");
	estrCopy2(&g_pchLoginStatusString, "");
}

void gclLoginUserChoosingGameserver_BeginFrame(void)
{
	const char *pchQuickPlayName = gclQuickPlay_GetMapName();
	assert(g_pGameServerChoices->ppChoices);

	gclLoginSendRefDictDataRequests();

	if (linkDisconnected(gpLoginLink) || !gpLoginLink)
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_TIMEOUT));
		return;
	}

	if (gbClickThroughLoginProcess)
	{
		S32 i;
		for (i=0; i < eaSize(&g_pGameServerChoices->ppChoices); i++)
		{
			if (!g_pGameServerChoices->ppChoices[i]->bNewMap)
			{
				gclLoginChooseMap(g_pGameServerChoices->ppChoices[i]);
				gbClickThroughLoginProcess = false; //turn it off for next time through
				break;
			}
		}
	}

	if (pchQuickPlayName)
	{
		S32 i;
		for (i=0; i < eaSize(&g_pGameServerChoices->ppChoices); i++)
		{
			if (strstri(g_pGameServerChoices->ppChoices[i]->baseMapDescription.mapDescription, pchQuickPlayName) &&
				g_pGameServerChoices->ppChoices[i]->baseMapDescription.containerID == 0)
			{
				gclLoginChooseMap(g_pGameServerChoices->ppChoices[i]);
				break;
			}
		}
	}

	if (g_iQuickLogin)
	{
		if (eaSize(&g_pGameServerChoices->ppChoices) == 0)
		{
			gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_NOMAPS));
			return;
		}
		else
		{

			if (sQuickLoginMapInstanceIndex)
			{
				int i;

				for (i=0; i < eaSize(&g_pGameServerChoices->ppChoices); i++)
				{
					if (g_pGameServerChoices->ppChoices[i]->baseMapDescription.mapInstanceIndex == sQuickLoginMapInstanceIndex && !g_pGameServerChoices->ppChoices[i]->bNotALegalChoice)
					{
						gclLoginChooseMap(g_pGameServerChoices->ppChoices[i]);
						break;
					}
				}

				if (i == eaSize(&g_pGameServerChoices->ppChoices))
				{
					for (i=0; i < eaSize(&g_pGameServerChoices->ppChoices); i++)
					{
						if (!g_pGameServerChoices->ppChoices[i]->bNotALegalChoice)
						{
							gclLoginChooseMap(g_pGameServerChoices->ppChoices[i]);
							break;
						}
					}

					if (i == eaSize(&g_pGameServerChoices->ppChoices))
					{
						assertmsgf(0, "Couldn't find requested instance OR any open instance OR even ask for a new instance of %s", sQuickLoginRequestedMapName);
					}
				}
			}
			else
			{
				int i;

				for (i=0; i < eaSize(&g_pGameServerChoices->ppChoices); i++)
				{
					if (!g_pGameServerChoices->ppChoices[i]->bNotALegalChoice)
					{
						gclLoginChooseMap(g_pGameServerChoices->ppChoices[i]);
						break;
					}
				}

				if (i == eaSize(&g_pGameServerChoices->ppChoices))
				{
					assertmsgf(0, "Couldn't find any open instance OR even ask for a new instance of %s", sQuickLoginRequestedMapName);
				}
			}
		}
	}

	if (s_pChosenMap)
	{
		Packet *pPak;

		pPak = pktCreate(gpLoginLink, TOLOGIN_LOGIN2_REQUESTGAMESERVERADDRESS);

		ParserSend(parse_PossibleMapChoice, pPak, NULL, s_pChosenMap, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);

		GSM_SendPacketOnStateTransition(pPak);

		gppLastLink = &gpLoginLink;

		//g_iQuickLogin = 0; // So logout works
		giLoginTimeoutFalloffFactor = 1;

		// Set (or clear) the patching info
		gclPatching_SetPatchInfo(s_pChosenMap->patchInfo);

		if(s_pChosenMap->patchInfo && gclPatching_DynamicPatchingEnabled())
			GSM_SwitchToState_Complex(GCL_LOGIN "/../" GCL_LOADING "/" GCL_LOADING_WAITING_FOR_ADDRESS "/" GCL_PATCH);
		else
			GSM_SwitchToState_Complex(GCL_LOGIN "/../" GCL_LOADING "/" GCL_LOADING_WAITING_FOR_ADDRESS);

		StructDestroy(parse_PossibleMapChoice, s_pChosenMap);
		s_pChosenMap = NULL;
	}
}

void gclLoginUserChoosingGameserver_Leave(void)
{
	if ( s_pChosenMap )
	{
		StructDestroy(parse_PossibleMapChoice, s_pChosenMap);
		s_pChosenMap = NULL;
	}
}

//--------------stuff for GCL_LOGIN_FAILED
void gclLoginFailed_Enter(void)
{
	gclServerForceDisconnect("Entered gclLoginFailed");
	
	linkRemove(gppLastLink);
	gppLastLink = NULL;

	if(GCL_LOGIN_TIMEOUT * giLoginTimeoutFalloffFactor < 120)
	{
		giLoginTimeoutFalloffFactor *= 2;
	}

	GSM_CancelRequestedTransitions();
}

void gclLoginFailed_BeginFrame(void)
{
	float fTimeInState = GSM_TimeInState(NULL);
	if (fTimeInState >= 5.f
		||
		fTimeInState >= 1.f &&
		(	g_iQuickLogin ||
			gbSkipAccountLogin))
	{
		estrCopy2(&g_pchLoginStatusString, "");
		gclLogin_LoginTransition();
	}
}

//--------------stuff for GCL_LOGIN_INVALID_DISPLAY_NAME
static char sNewDisplayName[ASCII_DISPLAYNAME_MAX_LENGTH+1];

void gclLoginInvalidDisplayName_Enter(void)
{
	sNewDisplayName[0] = 0;

	estrClear(&g_pchLoginStatusString);
}

void gclLoginInvalidDisplayName_Exit(void)
{
	estrDestroy(&s_pchInvalidDisplayName);
	sNewDisplayName[0] = 0;
}

const char *gclLogin_GetInvalidDisplayName(void)
{
	if (GSM_IsStateActiveOrPending(GCL_LOGIN_INVALID_DISPLAY_NAME))
		return s_pchInvalidDisplayName;
	return NULL;
}

void gclLogin_ChangeDisplayName(const char *newDisplayName)
{
	// Does not do any checking to see if the display name is valid (or if it was even changed)
	if (GSM_IsStateActiveOrPending(GCL_LOGIN_INVALID_DISPLAY_NAME))
	{
		if (strlen(newDisplayName) < ARRAY_SIZE_CHECKED(sNewDisplayName))
		{
			strcpy(sNewDisplayName, newDisplayName);
			GSM_SwitchToState_Complex(GCL_LOGIN_INVALID_DISPLAY_NAME "/" GCL_LOGIN_CHANGING_DISPLAY_NAME);
		}
	}
}

//--------------stuff for GCL_LOGIN_CHANGING_DISPLAY_NAME
void gclLoginChangeDisplayName_Enter(void)
{
	if (linkConnected(gpLoginLink))
	{
		Packet *response = pktCreate(gpLoginLink, TOLOGIN_CHANGE_DISPLAYNAME);
		pktSendString(response, sNewDisplayName);
		pktSend(&response);

		GSM_SwitchToState_Complex(GCL_LOGIN_INVALID_DISPLAY_NAME "/" GCL_LOGIN_DISPLAY_NAME_WAIT);
	}
	else
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_NOSERVER));
}

void gclLoginChangeDisplayWait_BeginFrame(void)
{
	if (GSM_TimeInState(NULL) >= GCL_LOGIN_TIMEOUT * giLoginTimeoutFalloffFactor && !gGCLState.bNoTimeout)
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_TIMEOUT));
	}
	else if (linkDisconnected(gpLoginLink))
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_DISCONNECT));
	}
}


//--------------stuff for GCL_LOGIN_USER_CHOOSING_UGC_PROJECT
static void gclLoginChoosingUGCProject_Enter(void)
{
	//there's a race condition in the UGC publish testing where the state transitions happen in an awkward order and 
	//this line is unsafe. Possibly will have to be fixed more stably in the future if we allow
	//going from character creation directly to UGC project choosing
	if (!g_isContinuousBuilder)
	{
		StructDestroySafe(parse_PossibleUGCProject, &spChosenUGCProject);
	}

	if (linkConnected(gpLoginLink))
	{
		Packet *response = pktCreate(gpLoginLink, TOLOGIN_REQUEST_POSSIBLE_UGC_CHOICES);
		pktSend(&response);
	}
	else
	{
		gclLoginFail(TranslateMessageKey(LOGINSTATUS_LOGIN_NOSERVER));
	}
}

static void gclLoginChoosingUGCProject_BeginFrame(void)
{
	if (spChosenUGCProject)
	{
		Packet *pPak;

		pPak = pktCreate(gpLoginLink, TOLOGIN_READY_TO_CHOOSE_UGC_PROJECT);	
		pktSend(&pPak);

		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_WAITING_UGC_EDIT_PERMISSION);
	}
}


static void gclLoginWaitingUgcEditPermission_BeginFrame(void)
{
	if (spChosenUGCProject && spChosenUGCProject->iEditQueueCookie)
	{
		Packet *pPak;

		pPak = pktCreate(gpLoginLink, TOLOGIN_CHOOSE_UGC_PROJECT);
		ParserSendStruct(parse_PossibleUGCProject, pPak, spChosenUGCProject);
		GSM_SendPacketOnStateTransition(pPak);

		gppLastLink = &gpLoginLink;

		giLoginTimeoutFalloffFactor = 1;

		gclPatching_SetPatchInfo(spChosenUGCProject->pPatchInfo);

		if(spChosenUGCProject->pPatchInfo && gclPatching_DynamicPatchingEnabled() && !(spChosenUGCProject->iPossibleUGCProjectFlags & POSSIBLEUGCPROJECT_FLAG_NEW_NEVER_SAVED))
			GSM_SwitchToState_Complex(GCL_LOGIN "/../" GCL_LOADING "/" GCL_LOADING_WAITING_FOR_ADDRESS "/" GCL_PATCH);
		else
			GSM_SwitchToState_Complex(GCL_LOGIN "/../" GCL_LOADING "/" GCL_LOADING_WAITING_FOR_ADDRESS);

		StructDestroySafe(parse_PossibleUGCProject, &spChosenUGCProject);
	}
}

AUTO_RUN;
void gclLogin_AutoRegister(void)
{
	GSM_AddGlobalState(GCL_LOGIN);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN, gclLogin_Enter, gclLogin_BeginFrame, gclLogin_EndFrame, gclLogin_Leave);

	// Account Server-related states
	GSM_AddGlobalState(GCL_ACCOUNT_SERVER_LOGIN);
	GSM_AddGlobalStateCallbacks(GCL_ACCOUNT_SERVER_LOGIN, 
		gclAccountServerLogin_Enter, NULL, NULL, gclAccountServerLogin_Leave);

	GSM_AddGlobalState(GCL_ACCOUNT_SERVER_CONNECT);
	GSM_AddGlobalStateCallbacks(GCL_ACCOUNT_SERVER_CONNECT, 
		gclAccountServerConnect_Enter, gclAccountServerConnect_BeginFrame, NULL, gclAccountServerConnect_Leave);

	GSM_AddGlobalState(GCL_ACCOUNT_SERVER_WAITING_FOR_TICKET);
	GSM_AddGlobalStateCallbacks(GCL_ACCOUNT_SERVER_WAITING_FOR_TICKET, 
		gclAccountServerWaiting_Enter, gclAccountServerWaiting_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GCL_ACCOUNT_ONETIMECODE);
	GSM_AddGlobalStateCallbacks(GCL_ACCOUNT_ONETIMECODE, gclAccountServerOneTimeCode_Enter, gclAccountServerOneTimeCode_BeginFrame, NULL, gclAccountServerOneTimeCode_Leave);

	GSM_AddGlobalState(GCL_ACCOUNT_SAVENEXTMACHINE);
	GSM_AddGlobalStateCallbacks(GCL_ACCOUNT_SAVENEXTMACHINE, gclAccountServerSaveNextMachine_Enter, gclAccountServerSaveNextMachine_BeginFrame, NULL, gclAccountServerSaveNextMachine_Leave);

	GSM_AddGlobalState(GCL_LOGIN_SERVER_LOAD_WAIT);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_SERVER_LOAD_WAIT, gclLoginServerLoadWait_Enter, gclLoginServerLoadWait_BeginFrame, 
		NULL, gclLoginServerLoadWait_Leave);

	GSM_AddGlobalState(GCL_LOGIN_SERVER_CONNECT);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_SERVER_CONNECT, gclLoginServerConnect_Enter, gclLoginServerConnect_BeginFrame, 
		NULL, NULL);

	GSM_AddGlobalState(GCL_LOGIN_SELECT_PLAYERTYPE);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_SELECT_PLAYERTYPE, gclLoginSelectPlayerType_Enter, 
		NULL, NULL, NULL);

	GSM_AddGlobalState(GCL_LOGIN_WAITING_FOR_CHARACTER_LIST);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_WAITING_FOR_CHARACTER_LIST, gclLoginWaitingForCharacterList_Enter, 
		gclLoginWaitingForCharacterList_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GCL_LOGIN_USER_CHOOSING_CHARACTER);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_USER_CHOOSING_CHARACTER, gclLoginUserChoosingCharacter_Enter, 
		gclLoginUserChoosingCharacter_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GCL_LOGIN_USER_CHOOSING_EXISTING);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_USER_CHOOSING_EXISTING, gclLoginUserChoosingExisting_Enter, 
		NULL, NULL, gclLoginUserChoosingExisting_Leave);

	GSM_AddGlobalState(GCL_LOGIN_NEW_CHARACTER_WAITING_FOR_DATA);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_NEW_CHARACTER_WAITING_FOR_DATA, gclLoginNewCharacterWaitingForData_Enter,
		NULL, NULL, NULL);

	GSM_AddGlobalState(GCL_LOGIN_NEW_CHARACTER_CREATION);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_NEW_CHARACTER_CREATION, gclLoginCreateCharacter_Enter, 
		gclLoginCreateCharacter_BeginFrame, NULL, NULL);

    GSM_AddGlobalState(GCL_LOGIN_WAITING_FOR_REDIRECT);
    GSM_AddGlobalStateCallbacks(GCL_LOGIN_WAITING_FOR_REDIRECT, gclLoginWaitingForRedirect_Enter, 
        gclLoginWaitingForRedirect_BeginFrame, NULL, NULL);

    GSM_AddGlobalState(GCL_LOGIN_WAITING_FOR_REDIRECT_DONE);
    GSM_AddGlobalStateCallbacks(GCL_LOGIN_WAITING_FOR_REDIRECT_DONE, gclLoginWaitingForRedirectDone_Enter, 
        gclLoginWaitingForRedirectDone_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST, gclLoginWaitingForGameserverList_Enter, 
		gclLoginWaitingForGameserverList_BeginFrame, NULL, gclLoginWaitingForGameserverList_Exit);

	GSM_AddGlobalState(GCL_LOGIN_USER_CHOOSING_GAMESERVER);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_USER_CHOOSING_GAMESERVER, gclLoginUserChoosingGameserver_Enter, 
		gclLoginUserChoosingGameserver_BeginFrame, NULL, gclLoginUserChoosingGameserver_Leave);

	GSM_AddGlobalState(GCL_LOGIN_FAILED);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_FAILED, gclLoginFailed_Enter, 
		gclLoginFailed_BeginFrame, NULL, NULL);	

	// Display name ones
	GSM_AddGlobalState(GCL_LOGIN_INVALID_DISPLAY_NAME);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_INVALID_DISPLAY_NAME,
		gclLoginInvalidDisplayName_Enter, NULL, NULL, gclLoginInvalidDisplayName_Exit);

	GSM_AddGlobalState(GCL_LOGIN_CHANGING_DISPLAY_NAME);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_CHANGING_DISPLAY_NAME, 
		gclLoginChangeDisplayName_Enter, NULL, NULL, NULL);

	GSM_AddGlobalState(GCL_LOGIN_DISPLAY_NAME_WAIT);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_DISPLAY_NAME_WAIT, 
		NULL, gclLoginChangeDisplayWait_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GCL_LOGIN_USER_CHOOSING_UGC_PROJECT);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_USER_CHOOSING_UGC_PROJECT, 
		gclLoginChoosingUGCProject_Enter, gclLoginChoosingUGCProject_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GCL_LOGIN_WAITING_UGC_EDIT_PERMISSION);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_WAITING_UGC_EDIT_PERMISSION, 
		NULL, gclLoginWaitingUgcEditPermission_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GCL_LOGIN_USER_BROWSING_UGC_PRODUCTS);
	GSM_AddGlobalStateCallbacks(GCL_LOGIN_USER_BROWSING_UGC_PRODUCTS, 
		UGCProductViewer_Enter, UGCProductViewer_BeginFrame, NULL, UGCProductViewer_Leave);

	g_bShowAllStaticMaps = isDevelopmentMode();
}

void gclLogin_BrowseUGCProducts(bool bEnable)
{
	static bool s_bChoosingUGCProject = false;
	if (bEnable)
	{
		if (GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_UGC_PROJECT))
		{
			GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_USER_CHOOSING_UGC_PROJECT "/" GCL_LOGIN_USER_BROWSING_UGC_PRODUCTS);
			s_bChoosingUGCProject = true;
		}
		else if (GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_CHARACTER))
		{
			GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_USER_BROWSING_UGC_PRODUCTS);
			s_bChoosingUGCProject = false;
		}
	}
	else if (GSM_IsStateActiveOrPending(GCL_LOGIN_USER_BROWSING_UGC_PRODUCTS))
	{
		if (s_bChoosingUGCProject)
		{
			GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_USER_CHOOSING_UGC_PROJECT);
		}
		else
		{
			GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_USER_CHOOSING_CHARACTER);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BrowseUGCProducts);
void gclLogin_ExprBrowseUGCProducts(void)
{
	gclLogin_BrowseUGCProducts(true);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void RequestUGCProjects(void)
{
	if (GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_GAMESERVER))
	{
		GSM_SwitchToState_Complex(GCL_LOGIN_USER_CHOOSING_GAMESERVER "/../" GCL_LOGIN_USER_CHOOSING_UGC_PROJECT);
	}
	else if (GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_CHARACTER))
	{
		s_bLoggingInForUGC = true;
	}
}

void gclChooseUGCProject(PossibleUGCProject *pProject)
{
	StructDestroySafe(parse_PossibleUGCProject, &spChosenUGCProject);
	spChosenUGCProject = StructClone(parse_PossibleUGCProject, pProject);
}

void gclChooseNewUGCProject(const char* newName, const char* allegiance, int level)
{
	StructDestroySafe(parse_PossibleUGCProject, &spChosenUGCProject);
	spChosenUGCProject = StructCreate(parse_PossibleUGCProject);
	spChosenUGCProject->pProjectInfo = StructCreate(parse_UGCProjectInfo);
	StructCopyString(&spChosenUGCProject->pProjectInfo->pcPublicName, newName);

	if( !nullStr( allegiance )) {
		if (!spChosenUGCProject->pProjectInfo->pRestrictionProperties)
			spChosenUGCProject->pProjectInfo->pRestrictionProperties = StructCreate( parse_WorldUGCRestrictionProperties );

		eaPush( &spChosenUGCProject->pProjectInfo->pRestrictionProperties->eaFactions, StructCreate( parse_WorldUGCFactionRestrictionProperties ));
		spChosenUGCProject->pProjectInfo->pRestrictionProperties->eaFactions[ 0 ]->pcFaction = allocAddString( allegiance );
	}

	if (level > 0) {
		if (!spChosenUGCProject->pProjectInfo->pRestrictionProperties)
			spChosenUGCProject->pProjectInfo->pRestrictionProperties = StructCreate( parse_WorldUGCRestrictionProperties );

		spChosenUGCProject->pProjectInfo->pRestrictionProperties->iMinLevel = 
			spChosenUGCProject->pProjectInfo->pRestrictionProperties->iMaxLevel = level;
	}
}

void gclDeleteUGCProject(PossibleUGCProject* pProject)
{
	if (!GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_UGC_PROJECT))
	{
		return;
	}

	if (pProject)
	{
		Packet *pPak;

		pPak = pktCreate(gpLoginLink, TOLOGIN_DELETE_UGC_PROJECT);
		ParserSendStruct(parse_PossibleUGCProject, pPak, pProject);
		pktSend(&pPak);	
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_IsSelectedCharacter);
bool gclLogin_IsSelectedCharacter(SA_PARAM_OP_VALID Login2CharacterChoice *pChoice)
{
    return pChoice && pChoice == s_chosenCharacter;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_GetSelectedCharacterID);
U32 gclLogin_GetSelectedCharacterID(void)
{
	return s_chosenCharacter ? s_chosenCharacter->containerID : 0;
}

void gclLogin_PushPlayerUpdate(void)
{
	if (linkConnected(gpLoginLink) && GSM_IsStateActiveOrPending(GCL_LOGIN))
	{
		Packet *pak = pktCreate(gpLoginLink, TOLOGIN_UPDATE_CHAT_PLAYERINFO);
		// sends no data
		pktSend(&pak);
	}
}

//  return true if this is a usable character class, uses pcGamePermissionValue field of CharacterPath
// NULL is a valid character path name (free form) and will always return true
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterPathCanUseId);
bool CharacterPath_CanUseId(ContainerID id)
{
    Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(id);
    if ( playerEnt )
	{
		GameAccountData *pData = GET_REF(g_characterSelectionData->hGameAccountData);
		return CharacterPath_trh_FromNameCanUse(CONTAINER_NOCONST(GameAccountData, pData), REF_STRING_FROM_HANDLE(playerEnt->pChar->hPath));
	}

	// no choice or not found
	return false;
}

//  return true if this is a usable character class, uses pcGamePermissionValue field of CharacterPath
// NULL is a valid character path name (free form) and will always return true
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterPathIsFreeId);
bool CharacterPath_IsFreeId(ContainerID id)
{
    Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(id);
	if( playerEnt )
	{
		GameAccountData *pData = GET_REF(g_characterSelectionData->hGameAccountData);
        // LOGIN2TODO - will this be called on characters that are not currently selected?
		CharacterPath *pPath = GET_REF(playerEnt->pChar->hPath);

		if(pPath && pData)
		{
			return CharacterPath_trh_CanUseEx(CONTAINER_NOCONST(GameAccountData, pData), pPath, GAME_PERMISSION_FREE);
		}

	}

	// no choice or not found
	return false;
}

// return true if the key is found in the game account data
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterPathIsFreeByName);
bool CharacterPath_CharacterPathIsFreeByName(const char *pName, const char *pKey)
{
	if(pName && pKey)
	{
		GameAccountData *pData = entity_GetGameAccount(NULL);
		if(pData)
		{
			return CharacterPath_trh_FromNameHasKey(CONTAINER_NOCONST(GameAccountData, pData), pName, pKey);
		}
	}

	// no choice or not found
	return false;
}

//LOGIN2TODO - still need to figure out how these purchases will work
bool gclLogin_GameAccountCanMakeNumericPurchaseWithAnyCharacter(GameAccountDataNumericPurchaseDef* pDef)
{
    if (pDef && g_characterSelectionData)
	{
		int i;
        for ( i = eaSize(&g_characterSelectionData->characterChoices->characterChoices) - 1; i >= 0; i-- )
        {
            Login2CharacterChoice *characterChoice = g_characterSelectionData->characterChoices->characterChoices[i];

			if (!characterChoice->virtualShardID)
			{
				if (GAD_PossibleCharacterCanMakeNumericPurchase(NULL, characterChoice->virtualShardID, NULL, pDef, false))
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool gclLogin_GameAccountCanMakeAnyNumericPurchaseWithAnyCharacter(S32 eCategory)
{
	int i;
	for (i = 0; i < eaSize(&g_GameAccountDataNumericPurchaseDefs.eaDefs); i++)
	{
		GameAccountDataNumericPurchaseDef* pDef = g_GameAccountDataNumericPurchaseDefs.eaDefs[i];
		GameAccountData* pData = entity_GetGameAccount(NULL);

		if (eCategory > kGameAccountDataNumericPurchaseCategory_None && pDef->eCategory != eCategory)
		{
			continue;
		}
		if (GAD_CanMakeNumericPurchaseCheckKeyValues(pData, pDef) &&
			gclLogin_GameAccountCanMakeNumericPurchaseWithAnyCharacter(pDef))
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GameAccountCanMakeAnyNumericPurchaseWithAnyCharacter);
bool gclLogin_ExprGameAccountCanMakeAnyNumericPurchaseWithAnyCharacter(S32 eCategory)
{
	return gclLogin_GameAccountCanMakeAnyNumericPurchaseWithAnyCharacter(eCategory);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoginGetUGCProjectSlotCount);
int gclLogin_ExprLoginGetUGCProjectSlotCount(void)
{
	GameAccountData* pAccountData = entity_GetGameAccount(NULL);
    return(Login2_UGCGetProjectMaxSlots(pAccountData));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoginIsCluster);
bool gclLogin_ExprLoginIsCluster(void)
{
    return ( s_ClusterStatus && s_ClusterStatus->isCluster );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoginAnyShardDown);
bool gclLogin_ExprLoginAnyShardDown(void)
{
    int i;

    // No status so be paranoid.
    if ( s_ClusterStatus == NULL )
    {
        return true;
    }

    // Single shard, so no shards are down.
    if ( s_ClusterStatus->isCluster == false )
    {
        return false;
    }

    for ( i = eaSize(&s_ClusterStatus->shardStatus) - 1; i >= 0; i-- )
    {
        if ( s_ClusterStatus->shardStatus[i]->isReady == false )
        {
            // Shard is down.
            return true;
        }
    }

    // No shards down.
    return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoginAnyNormalShardDown);
bool gclLogin_ExprLoginAnyNormalShardDown(void)
{
	int i;

	// No status so be paranoid.
	if ( s_ClusterStatus == NULL )
	{
		return true;
	}

	// Single shard, so no shards are down.
	if ( s_ClusterStatus->isCluster == false )
	{
		return false;
	}

	for ( i = eaSize(&s_ClusterStatus->shardStatus) - 1; i >= 0; i-- )
	{
		if ( s_ClusterStatus->shardStatus[i]->isReady == false && s_ClusterStatus->shardStatus[i]->shardType == SHARDTYPE_NORMAL )
		{
			// Shard is down.
			return true;
		}
	}

	// No shards down.
	return false;
}


// Model function that returns the list of Login2ShardStatus for just the normal shards (not UGC shards).
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoginGetClusterNormalShards);
void gclLogin_ExprLoginGetClusterNormalShards(SA_PARAM_NN_VALID UIGen *pGen)
{
    static EARRAY_OF(Login2ShardStatus) s_ShardStatusList = NULL;
    static U32 s_ShardStatusVersion = 0;
    int i;

    if ( s_ShardStatusVersion != s_ClusterStatusVersion )
    {
        eaClearFast(&s_ShardStatusList);
        if ( s_ClusterStatus )
        {
            for ( i = eaSize(&s_ClusterStatus->shardStatus) - 1; i >= 0; i-- )
            {
                Login2ShardStatus *shardStatus = s_ClusterStatus->shardStatus[i];
                // Push normal shards on the list, unless they have character creation disabled and the player is AL0
                if ( shardStatus->shardType == SHARDTYPE_NORMAL && !( sLoginData.iAccessLevel == 0 && shardStatus->creationDisabled ) )
                {
                    eaPush(&s_ShardStatusList, shardStatus);
                }
            }
        }
        s_ShardStatusVersion = s_ClusterStatusVersion;
    }

    if ( s_ShardStatusList )
    {
        ui_GenSetList(pGen, &s_ShardStatusList, parse_Login2ShardStatus);
    }
    else
    {
        ui_GenSetList(pGen, NULL, NULL);
    }
}

// Model function that returns a comma-separated string of offline normal shards (not UGC shards).
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoginGetClusterOfflineNormalShards);
SA_RET_NN_STR const char * gclLogin_ExprLoginGetClusterOfflineNormalShards(SA_PARAM_NN_VALID ExprContext* pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID const char *pchContextKey)
{
	char *estrResult = NULL;
	int iNumOffline = 0, iNumAppended = 0;
	char *estrRestOfShardNames = NULL;
	char *estrLastShardName = NULL;
	const char *result = NULL;
	int i;

	if ( s_ClusterStatus != NULL && s_ClusterStatus->isCluster )
	{
		// Count how many shards are down, so we know which one to put in LastShardName
		for ( i = eaSize(&s_ClusterStatus->shardStatus) - 1; i >= 0; i-- )
		{
			Login2ShardStatus *shardStatus = s_ClusterStatus->shardStatus[i];
			if ( shardStatus->isReady == false && shardStatus->shardType == SHARDTYPE_NORMAL )
			{
				// Shard is down.
				++iNumOffline;
			}
		}

		// Build the strings
		for ( i = eaSize(&s_ClusterStatus->shardStatus) - 1; i >= 0; i-- )
		{
			Login2ShardStatus *shardStatus = s_ClusterStatus->shardStatus[i];
			if ( shardStatus->isReady == false && shardStatus->shardType == SHARDTYPE_NORMAL )
			{
				// Shard is down.
				++iNumAppended;
				if( iNumAppended == iNumOffline )
				{
					estrAppend2(&estrLastShardName, shardStatus->shardName);
					break;
				}
				else //if( iNumAppended < iNumOffline )
				{
					if( iNumAppended > 1 )
						estrAppend2(&estrRestOfShardNames, ", ");

					estrAppend2(&estrRestOfShardNames, shardStatus->shardName);
				}
			}
		}
	}

	FormatGameMessageKey(&estrResult, pchContextKey,
		STRFMT_STRING("LastShardName", NULL_TO_EMPTY(estrLastShardName)),
		STRFMT_STRING("RestOfShardNames", NULL_TO_EMPTY(estrRestOfShardNames)),
		STRFMT_INT("NumOffline", iNumOffline),
		STRFMT_END
		);
	
	result = estrResult ? exprContextAllocString(pContext, estrResult) : NULL;

	estrDestroy(&estrResult);
	estrDestroy(&estrRestOfShardNames);
	estrDestroy(&estrLastShardName);

	return NULL_TO_EMPTY(result);
}

#include "AutoGen/NewControllerTracker_Pub_h_ast.c"
