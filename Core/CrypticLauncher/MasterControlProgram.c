#include "mastercontrolprogram.h"
#include "patcher.h"
#include "launcherUtils.h"
#include "file.h"
#include "textparser.h"
#include "pcl_typedefs.h"
#include "registry.h"
#include "GameDetails.h"
#include "systemtray.h"
#include "options.h"

#include "globalcomm.h"
#include "utils.h"
#include "earray.h"
#include "cmdparse.h"
#include "estring.h"
#include "sysutil.h"
#include "memorymonitor.h"
#include "winutil.h"
#include "foldercache.h"
#include "gimmedllwrapper.h"
#include "sock.h"
#include "netlinkprintf.h"
#include "otheroptions.h"
#include "utilitieslib.h"
#include "trivia.h"
#include "wininclude.h"
#include <locale.h>
#include "ThreadSafeQueue.h"
#include "error.h"


#include "Serverlist.h"
#include "productinfo.h"


#include "simplewindowmanager.h"
#include "stringutil.h"
#include "accountinfo.h"
#include "network/crypt.h"
#include "ControllerLink.h"
#include "CrypticLauncher.h"
#include "resource_CrypticLauncher.h"
#include "version.h"
#include "bundled_modtimes.h"
#include "Organization.h"
#include "systemspecs.h"

char *g_base_url = NULL;
AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_NAME(baseurl); void cmd_baseurl(char *s) { g_base_url = strdup(s); }

bool g_read_password = false;
AUTO_CMD_INT(g_read_password, readpassword);

bool g_qa_mode = false;
AUTO_CMD_INT(g_qa_mode, qa);

bool g_dev_mode = false;
AUTO_CMD_INT(g_dev_mode, dev);

bool g_pwrd_mode = false;
AUTO_CMD_INT(g_pwrd_mode, pwrd);

bool g_launcher_debug = false;
AUTO_CMD_INT(g_launcher_debug, launcher_debug) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

extern int g_force_sockbsd;

// PW Core Client login bypass account name
static char *s_bypass_accountname;

// PW Core Client login bypass password
static int s_bypass_pipe;

// PW Core Client login bypass
// Design description, from email conversation with Shen Hui on 2012-06-21:
//  1.	User logs into CORE with PWRD account name and password
// 	2.	User tries to launch NeverWinter from CORE
// 	3.	CORE creates an anonymous pipe and put MD5(PWRD account name + password) into pipe buffer
// 	4.	CORE launches game with the handle of the anonymous pipe through command line
// 	5.	Game launcher parses pipe handle from command line and use the handle to read MD5(PWRD account name + password) from pipe buffer
//  I think the exact spelling of the option should be:
//    -Bypass <accountname> <handle id>
//  It would be better if we can make the option string case-insensitive.
AUTO_COMMAND ACMD_HIDE;
void Bypass(char *account, char *handle)
{
	void *ptr;
	char dummy;
	int count;

	// Get account name.
	SAFE_FREE(s_bypass_accountname);
	if (account && *account)
		s_bypass_accountname = strdup(account);
	
	// Get pipe handle.
	s_bypass_pipe = 0;
	count = sscanf(handle, "%p%c", &ptr, &dummy);
	if (count == 1)
		s_bypass_pipe = (int)ptr;
}

static bool MCP_OncePerFrame(SimpleWindow *pMeaninglessWindow)
{
	static int bInside = false;
	static int counter = 0;

	if (!bInside)
	{
		static int frameTimer;
		F32 frametime;

		bInside = true;

		if (!frameTimer)
			frameTimer = timerAlloc();
		frametime = timerElapsedAndStart(frameTimer);
		utilitiesLibOncePerFrame(frametime, 1);	
		counter++;
		bInside =false;
	}
	

	return true;
}



char *g_product_override = NULL;
AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0); void productOverride(char *product) { g_product_override = strdup(product); }

//note that for standalone mode, the "short product name" is never used, so we just set it to XX
U32 GetProductNameFromExecutableName(void)
{
	char exeName[MAX_PATH];
	U32 gameID;

	if(g_product_override)
	{
		gameID = gdGetIDByName(g_product_override);
		SetProductName(g_product_override, gdGetCode(gameID));
		return gameID;
	}

	getFileNameNoExtNoDirs(exeName, getExecutableName());
	gameID = gdGetIDByExecutable(exeName);
	SetProductName(gdGetName(gameID), gdGetCode(gameID));
	return gameID;
}

// Set or reset the launcher's base URL.
void SetLauncherBaseUrl(CrypticLauncherWindow *launcher)
{
	if(g_base_url)
		launcher->baseUrl = g_base_url;
	else if(g_qa_mode)
		launcher->baseUrl = strdup(gdGetQALauncherURL(launcher->gameID));
	else if(g_dev_mode)
		launcher->baseUrl = strdup(gdGetDevLauncherURL(launcher->gameID));
	else if(g_pwrd_mode)
		launcher->baseUrl = strdup(gdGetPWRDLauncherURL(launcher->gameID));
	else
		launcher->baseUrl = strdup(gdGetLauncherURL(launcher->gameID));
}

extern U32 g_default_langid;

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	char path[CRYPTIC_MAX_PATH];
	char *tokCmdLine;
	int argc = 0, oldargc;
	char *args[1000];
	char **argv = args;
	char buf[1024]={0};
	SimpleWindow *pWindow;
	CrypticLauncherWindow *launcher;
	U32 gameID;
	bool do_proxy = false;
	size_t len;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER_LPCMDLINE

	// Reserve large memory chunk to be able to handle large files.
	memTrackReserveMemoryChunk(1024*1024*1024);

	setDefaultProductionMode(1);
	DO_AUTO_RUNS

	gimmeDLLDisable(1);
	fileAllPathsAbsolute(1);
	FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);

	// Parse the command line
	// FIXME: This should use the generic commandline-from-file support, rather than this questionable hack.
	tokCmdLine = strdup(lpCmdLine);
	loadCmdline("./cmdline.txt",buf,sizeof(buf));
	args[0] = getExecutableName();
	oldargc = 1 + tokenize_line_quoted_safe(buf,&args[1],ARRAY_SIZE(args)-1,0);
	argc = oldargc + tokenize_line_quoted_safe(tokCmdLine,&args[oldargc],ARRAY_SIZE(args)-oldargc,0);
	cmdParseCommandLine(argc, argv);

	//ghInstance = hInstance;

	setDefaultAssertMode();
	memMonitorInit();

	// Initialize system specs.
	systemSpecsInit();

	// Extract dbghelp.dll if needed
	// Note: this logic is faulty, you can have an old version with a new timestamp, since it doesn't timestamp the file on write (gets current time)
	if(!fileExists("dbghelp.dll") || fileLastChanged("dbghelp.dll") < DBGHELP_MODTIME)
	{
		HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_DBGHELP), "DLL");
		if (rsrc)
		{
			HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
			if (gptr)
			{
				FILE *pExeFile;
				void *pExeData = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
				size_t iExeSize = SizeofResource(GetModuleHandle(NULL), rsrc);

				pExeFile = fopen("dbghelp.dll", "wb");
				if (pExeFile)
				{
					fwrite(pExeData, iExeSize, 1, pExeFile);
					fclose(pExeFile);
				}
			}
		}
	}

	// Extract CrypticError.exe if needed
	// Note: this logic is faulty, you can have an old version with a new timestamp, since it doesn't timestamp the file on write (gets current time)
	if(!fileExists("CrypticError.exe") || fileLastChanged("CrypticError.exe") < CRYPTICERROR_MODTIME)
	{
		HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_CRYPTICERROR), "DLL");
		if (rsrc)
		{
			HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
			if (gptr)
			{
				FILE *pExeFile;
				void *pExeData = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
				size_t iExeSize = SizeofResource(GetModuleHandle(NULL), rsrc);

				pExeFile = fopen("CrypticError.exe", "wb");
				if (pExeFile)
				{
					fwrite(pExeData, iExeSize, 1, pExeFile);
					fclose(pExeFile);
				}
			}
		}
	}


	gimmeDLLDisable(1);
	
	getExecutableDir(path);
	if(!!strstri(path, "/bin"))
		setDefaultAssertMode();
	else
		setProductionClientAssertMode();
	setAssertMode(getAssertMode() | ASSERTMODE_MINIDUMP | ASSERTMODE_FORCEFULLDUMPS);
	setAssertMode(getAssertMode() & ~ASSERTMODE_TEMPORARYDUMPS);

	// XXX: Force disable crypticerror until we can figure out how to make it recognize internal users better <NPK 2009-03-02>
	//setAssertMode(getAssertMode() & ~ASSERTMODE_USECRYPTICERROR);

	triviaPrintf("LauncherVersion", "%s", BUILD_TIMESTAMP);


	srand((unsigned int)time(NULL));

    //attempt to get product name from executable name
	gameID = GetProductNameFromExecutableName();

	//normally this is called in ServerLibStartup(), which standalone MCP doesn't do
	utilitiesLibStartup();
	sockStart();
	cryptMD5Init();
	//fileLoadGameDataDirAndPiggs();

	printf("Ready.\n");

	autoPatch(gameID);

	// Create the launcher structure
	launcher = callocStruct(CrypticLauncherWindow);
	launcher->ctConnStatus = callocStruct(ControllerTrackerConnectionStatusStruct);
	launcher->shardList = callocStruct(ShardInfo_Basic_List);
	launcher->config = callocStruct(PatchClientConfig);
	launcher->delta_timer = timerAlloc();
	launcher->gameID = gameID;
	SetLauncherBaseUrl(launcher);
	if(lpCmdLine && lpCmdLine[0])
		launcher->commandline = strdup(lpCmdLine);

	// Create the command queues for the PCL thread.
	{
		XLOCKFREE_CREATE XLFCreateInfo = { 0 };
		XLFCreateInfo.maximumLength = 1024;
		XLFCreateInfo.structureSize = sizeof(XLFCreateInfo);
		XLFQueueCreate(&XLFCreateInfo, &launcher->queueToPCL);
		XLFQueueCreate(&XLFCreateInfo, &launcher->queueFromPCL);
	}

	// Load the game history from the registry
	strcpy(buf, "");
	readRegStr("CrypticLauncher", "GameHistory", SAFESTR(buf), NULL);
	DivideString(buf, ",", &launcher->history, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	// Filter out old-style history entries.
	// TODO: Remove this after the next launcher release. <NPK 2009-06-03>
	FOR_EACH_IN_EARRAY(launcher->history, char, hist)
		if(strchr(hist, ':')==strrchr(hist, ':') || strchr(hist, ':') == NULL)
			eaRemove(&launcher->history, FOR_EACH_IDX(launcher->history, hist));
	FOR_EACH_END

	// Load all the per-product registry settings
	loadRegistrySettings(launcher, NULL);

	// If our product name is CrypticLauncher, we should running a general mode
	// XXX: Always run in all-game mode, so sayeth the Bruce. This flag should just be removed eventually. <NPK 2009-01-12>
	// XXX: This decision has been reversed since it confuses a lot of STO players. Adding an option to re-enable for users that want it. <NPK 2009-12-23>
	if(launcher->gameID == 0)
		launcher->allMode = true;

	// Check if proxying is needed
	if(launcher->proxy_patching)
	{
		g_force_sockbsd = 1;
		do_proxy = true;
	}

	// Create the NetComms for the launcher
	launcher->comm = commDefault();
	launcher->pclComm = commCreate(1, 0);

	// Actually setup the proxy
	if(do_proxy)
		commSetProxy(launcher->pclComm, "us1.proxy." ORGANIZATION_DOMAIN, 80);

	// If -readpassword is given, look for a username and password on stdin
	if(g_read_password)
	{
		char *success, *success2;
		char userbuf[256], pwbuf[256];
		success = fgets(userbuf, ARRAY_SIZE_CHECKED(userbuf), fileWrap(stdin));
		success2 = fgets(pwbuf, ARRAY_SIZE_CHECKED(pwbuf), fileWrap(stdin));
		if(success && success2)
		{
			success = strrchr(userbuf, '\n');
			if(success) *success = '\0';
			strcpy(launcher->username, userbuf);
			success = strrchr(pwbuf, '\n');
			if(success) *success = '\0';
			strcpy(launcher->password, pwbuf);
			launcher->passwordHashed = true;
		}
	}

	// If bypass is enabled, get password from pipe buffer.
	if (!g_read_password && s_bypass_accountname && s_bypass_pipe && (len = pipe_buffer_read(launcher->password, sizeof(launcher->password), s_bypass_pipe)))
	{
		launcher->loginType = ACCOUNTLOGINTYPE_PerfectWorld;
		strcpy(launcher->username, s_bypass_accountname);
		launcher->password[len] = 0;
		g_read_password = true;
		launcher->passwordHashed = true;
	}

	// One-time unset of the proxy if it is set to US now that there is a warning.
	{
		int oneshot;
		if(!readRegInt(NULL, "ProxyClearOneShot", &oneshot, launcher->history))
			oneshot = 0;
		if(!oneshot)
		{
			if(stricmp(NULL_TO_EMPTY(launcher->proxy), "US")==0)
			{
				MessageBox(NULL, _("To improve your connection stability we are disabling the use of the US proxy by default. If you are sure you need it enabled, re-set it in the launcher options."), _("Disabling proxy"), MB_OK|MB_ICONWARNING);
				SAFE_FREE(launcher->proxy);
				writeRegStr(NULL, "Proxy", "None");
			}
			writeRegInt(NULL, "ProxyClearOneShot", 1);
		}
	}

	SimpleWindowManager_Init("Cryptic Launcher", true);

	SimpleWindowManager_AddOrActivateWindow(CL_WINDOW_MAIN, 0, IDD_MCP_START_STANDALONE, true, MCPStartDlgFunc, MCPStartTickFunc, launcher);
	pWindow = SimpleWindowManager_FindWindow(CL_WINDOW_MAIN, 0);
	assertmsg(pWindow, "Unable to open main launcher window");
	pWindow->pPreDialogCB = MCPPreDlgFunc;
	launcher->window = pWindow;
	if(launcher->showTrayIcon)
		systemTrayAdd(pWindow->hWnd);


	SimpleWindowManager_Run(MCP_OncePerFrame, NULL);

	EXCEPTION_HANDLER_END

	return (int) 1;
}

AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_CRYPTICLAUNCHER);
}

//create a text console window
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void console(int iDummy)
{
	newConsoleWindow();
	showConsoleWindow();
}



