// Controller.cpp : Defines the entry point for the application.
//
   
#if _MSC_VER < 1600
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/x64/debug/AttachToDebuggerLibX64.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")
#endif
#else
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLibX64_vs10.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLib_vs10.lib")
#endif
#endif

#include "mastercontrolprogram.h"
#include "GlobalTypes.h"
#include "file.h"
#include "logging.h"

#include "mastercontrolprogram_h_ast.h"

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


#include "Serverlist.h"
#include "productinfo.h"
#include "timing.h"
#include "StructNet.h"
#include "MCPErrors.h"

#if !STANDALONE
#include "artisttools_h_ast.h"
#include "controllerScriptScreen.h"
#include "genericmessage.h"
#include "MCPHttp.h"
#include "Resource_MasterControlProgram.h"
#endif

#include "stringutil.h"
#include "accountinfo.h"
#include "network/crypt.h"
#include "ControllerLink.h"
#include "NameList.h"
#include "UGCProjectCommon.h"

#define ONE_MCP_PROCESS_ONLY 0


bool gbDisableNewErrorScreen = false;
AUTO_CMD_INT(gbDisableNewErrorScreen, DisableNewErrorScreen);

//-------------------------------forward declard functions
bool AttachDebugger(long iProcessID);


//-------------------------------global variables
bool gbRunPublic = false;
bool gbQuit = false;
int gbRunInLocalMode = true;
int gbRunInLocalPublicMode = false;
bool gbArtist = false;
bool gbComplex = false;
bool gbProgrammer = false;
bool gbDontCreateController = false;
bool gbPressButtonAutomatically = false;
bool gbStartMinimized = false;
bool gbControllerHasALauncher = false;
bool gbOpenScriptWindow = false;
bool gbHttpMonitor = false;

bool gbCommandLinesMightHaveChanged = false;

bool gbYouAreMonitoringMCP = false;
AUTO_CMD_INT(gbYouAreMonitoringMCP, YouAreMonitoringMCP) ACMD_CMDLINE;

__time32_t giExecutableTimestamp = 0;
char gExecutableFilename[MAX_PATH];


//if true, the MCP starts out in server monitoring mode
AUTO_CMD_INT(gbHttpMonitor, HttpMonitor) ACMD_CMDLINE;

bool gbQaMode;
AUTO_CMD_INT(gbQaMode, qa) ACMD_CMDLINE;

static bool sbController64 = false;
AUTO_CMD_INT(sbController64, Controller64) ACMD_CMDLINE;

//PID of client spawned when attaching to a server
int gClientPID = 0;

HINSTANCE ghInstance;

//extern prototypes of splash functions
void CreateSplash();
void DestroySplash();

GlobalMCPStaticSettings gGlobalStaticSettings = {0};
GlobalMCPDynamicSettings gGlobalDynamicSettings = {0};
QueuedErrorDialog **ppQueuedErrorDialogs = NULL;

bool MCP_OncePerFrame(SimpleWindow *pMeaninglessWindow);


FILE *GetTempBMPFile(char **ppOutFileName)
{
	static int iCounter = 0;

	static char filename[MAX_PATH];

	int i;

	FILE *pOutFile;

	for (i=0; i < 100; i++)
	{
		sprintf_s(SAFESTR(filename),"%s/_temp_%u.bmp", fileLocalDataDir(), iCounter++);
		mkdirtree(filename);

		pOutFile = fopen(filename, "wb");

		if (pOutFile)
		{
			estrCopy2(ppOutFileName, filename);
			return pOutFile;
		}
	}

	assertmsgf(0, "Unable to create a temporary bmp file, despite 100 tries\n");

	return NULL;
}


ContainerDynamicInfo *GetContainerDynamicInfoFromSlot(int iSlotNum)
{
	assert(iSlotNum >= 0 && iSlotNum < eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo));
	return gGlobalDynamicSettings.ppContainerDynamicInfo[iSlotNum];
}

ContainerDynamicInfo *GetContainerDynamicInfoFromType(int iContainerType)
{
	int i;
	int iSize = eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo);

	for (i=0; i < iSize; i++)
	{
		if (gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType == iContainerType)
		{
			return gGlobalDynamicSettings.ppContainerDynamicInfo[i];
		}
	}

	assert(0);
	return NULL;
}
#if !STANDALONE

int GetNumContainerTypes(void)
{
	static int iRetVal = -1;

	if (iRetVal == -1)
	{
		iRetVal = eaSize(&gGlobalStaticSettings.ppContainerStaticInfo);
		while (iRetVal && gGlobalStaticSettings.ppContainerStaticInfo[iRetVal - 1]->iSlotNum == -2)
		{
			iRetVal--;
		}
	}

	return iRetVal;
}


ContainerStaticInfo *GetContainerStaticInfoFromSlot(int iSlotNum)
{
	assert(iSlotNum >= 0 && iSlotNum < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo));
	return gGlobalStaticSettings.ppContainerStaticInfo[iSlotNum];
}


ContainerStaticInfo *GetContainerStaticInfoFromType(int iContainerType)
{
	int i;
	int iSize = eaSize(&gGlobalStaticSettings.ppContainerStaticInfo);

	for (i=0; i < iSize; i++)
	{
		if (gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType == iContainerType)
		{
			return gGlobalStaticSettings.ppContainerStaticInfo[i];
		}
	}

	assert(0);
	return NULL;
}



ContainerDynamicInfo *GetContainerDynamicInfoFromType(int iContainerType);


MCPContainerButtonIDs gContainerButtonIDs[] = 
{
	{
		IDC_CONTROLLERHIDDEN,
		IDC_CONTROLLERDEBUG,
		0,
		0,
		0,
		0,
		IDC_CONTROLLER_EDITCOMMANDLINE,
		0,
	},
	{
		IDC_LAUNCHERHIDDEN,
		IDC_LAUNCHERDEBUG,
		0,
		0,
		0,
		0,
		IDC_LAUNCHER_EDITCOMMANDLINE,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN,
		IDC_CONTAINER_DEBUG,
		IDC_CONTAINER_NOAUTOLAUNCH,
		IDC_CONTAINER_GO,
		IDC_CONTAINER_STATUSTEXT,
		IDC_CONTAINER_STATE,
		IDC_CONTAINER_EDITCOMMANDLINE,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN2,
		IDC_CONTAINER_DEBUG2,
		IDC_CONTAINER_NOAUTOLAUNCH2,
		IDC_CONTAINER_GO2,
		IDC_CONTAINER_STATUSTEXT2,
		IDC_CONTAINER_STATE2,
		IDC_CONTAINER_EDITCOMMANDLINE2,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN3,
		IDC_CONTAINER_DEBUG3,
		IDC_CONTAINER_NOAUTOLAUNCH3,
		IDC_CONTAINER_GO3,
		IDC_CONTAINER_STATUSTEXT3,
		IDC_CONTAINER_STATE3,
		IDC_CONTAINER_EDITCOMMANDLINE3,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN4,
		IDC_CONTAINER_DEBUG4,
		IDC_CONTAINER_NOAUTOLAUNCH4,
		IDC_CONTAINER_GO4,
		IDC_CONTAINER_STATUSTEXT4,
		IDC_CONTAINER_STATE4,
		IDC_CONTAINER_EDITCOMMANDLINE4,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN5,
		IDC_CONTAINER_DEBUG5,
		IDC_CONTAINER_NOAUTOLAUNCH5,
		IDC_CONTAINER_GO5,
		IDC_CONTAINER_STATUSTEXT5,
		IDC_CONTAINER_STATE5,
		IDC_CONTAINER_EDITCOMMANDLINE5,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN6,
		IDC_CONTAINER_DEBUG6,
		IDC_CONTAINER_NOAUTOLAUNCH6,
		IDC_CONTAINER_GO6,
		IDC_CONTAINER_STATUSTEXT6,
		IDC_CONTAINER_STATE6,
		IDC_CONTAINER_EDITCOMMANDLINE6,
		IDC_CONTAINER_EDITSHAREDCOMMANDLINE6,
	},
	{
		IDC_CONTAINER_HIDDEN7,
		IDC_CONTAINER_DEBUG7,
		IDC_CONTAINER_NOAUTOLAUNCH7,
		IDC_CONTAINER_GO7,
		IDC_CONTAINER_STATUSTEXT7,
		IDC_CONTAINER_STATE7,
		IDC_CONTAINER_EDITCOMMANDLINE7,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN8,
		IDC_CONTAINER_DEBUG8,
		IDC_CONTAINER_NOAUTOLAUNCH8,
		IDC_CONTAINER_GO8,
		IDC_CONTAINER_STATUSTEXT8,
		IDC_CONTAINER_STATE8,
		IDC_CONTAINER_EDITCOMMANDLINE8,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN9,
		IDC_CONTAINER_DEBUG9,
		IDC_CONTAINER_NOAUTOLAUNCH9,
		IDC_CONTAINER_GO9,
		IDC_CONTAINER_STATUSTEXT9,
		IDC_CONTAINER_STATE9,
		IDC_CONTAINER_EDITCOMMANDLINE9,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN10,
		IDC_CONTAINER_DEBUG10,
		IDC_CONTAINER_NOAUTOLAUNCH10,
		IDC_CONTAINER_GO10,
		IDC_CONTAINER_STATUSTEXT10,
		IDC_CONTAINER_STATE10,
		IDC_CONTAINER_EDITCOMMANDLINE10,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN11,
		IDC_CONTAINER_DEBUG11,
		IDC_CONTAINER_NOAUTOLAUNCH11,
		IDC_CONTAINER_GO11,
		IDC_CONTAINER_STATUSTEXT11,
		IDC_CONTAINER_STATE11,
		IDC_CONTAINER_EDITCOMMANDLINE11,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN12,
		IDC_CONTAINER_DEBUG12,
		IDC_CONTAINER_NOAUTOLAUNCH12,
		IDC_CONTAINER_GO12,
		IDC_CONTAINER_STATUSTEXT12,
		IDC_CONTAINER_STATE12,
		IDC_CONTAINER_EDITCOMMANDLINE12,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN13,
		IDC_CONTAINER_DEBUG13,
		IDC_CONTAINER_NOAUTOLAUNCH13,
		IDC_CONTAINER_GO13,
		IDC_CONTAINER_STATUSTEXT13,
		IDC_CONTAINER_STATE13,
		IDC_CONTAINER_EDITCOMMANDLINE13,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN14,
		IDC_CONTAINER_DEBUG14,
		IDC_CONTAINER_NOAUTOLAUNCH14,
		IDC_CONTAINER_GO14,
		IDC_CONTAINER_STATUSTEXT14,
		IDC_CONTAINER_STATE14,
		IDC_CONTAINER_EDITCOMMANDLINE14,
		0,
	},	
	{
		IDC_CONTAINER_HIDDEN15,
		IDC_CONTAINER_DEBUG15,
		IDC_CONTAINER_NOAUTOLAUNCH15,
		IDC_CONTAINER_GO15,
		IDC_CONTAINER_STATUSTEXT15,
		IDC_CONTAINER_STATE15,
		IDC_CONTAINER_EDITCOMMANDLINE15,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN16,
		IDC_CONTAINER_DEBUG16,
		IDC_CONTAINER_NOAUTOLAUNCH16,
		IDC_CONTAINER_GO16,
		IDC_CONTAINER_STATUSTEXT16,
		IDC_CONTAINER_STATE16,
		IDC_CONTAINER_EDITCOMMANDLINE16,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN17,
		IDC_CONTAINER_DEBUG17,
		IDC_CONTAINER_NOAUTOLAUNCH17,
		IDC_CONTAINER_GO17,
		IDC_CONTAINER_STATUSTEXT17,
		IDC_CONTAINER_STATE17,
		IDC_CONTAINER_EDITCOMMANDLINE17,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN18,
		IDC_CONTAINER_DEBUG18,
		IDC_CONTAINER_NOAUTOLAUNCH18,
		IDC_CONTAINER_GO18,
		IDC_CONTAINER_STATUSTEXT18,
		IDC_CONTAINER_STATE18,
		IDC_CONTAINER_EDITCOMMANDLINE18,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN19,
		IDC_CONTAINER_DEBUG19,
		IDC_CONTAINER_NOAUTOLAUNCH19,
		IDC_CONTAINER_GO19,
		IDC_CONTAINER_STATUSTEXT19,
		IDC_CONTAINER_STATE19,
		IDC_CONTAINER_EDITCOMMANDLINE19,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN20,
		IDC_CONTAINER_DEBUG20,
		IDC_CONTAINER_NOAUTOLAUNCH20,
		IDC_CONTAINER_GO20,
		IDC_CONTAINER_STATUSTEXT20,
		IDC_CONTAINER_STATE20,
		IDC_CONTAINER_EDITCOMMANDLINE20,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN21,
		IDC_CONTAINER_DEBUG21,
		IDC_CONTAINER_NOAUTOLAUNCH21,
		IDC_CONTAINER_GO21,
		IDC_CONTAINER_STATUSTEXT21,
		IDC_CONTAINER_STATE21,
		IDC_CONTAINER_EDITCOMMANDLINE21,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN22,
		IDC_CONTAINER_DEBUG22,
		IDC_CONTAINER_NOAUTOLAUNCH22,
		IDC_CONTAINER_GO22,
		IDC_CONTAINER_STATUSTEXT22,
		IDC_CONTAINER_STATE22,
		IDC_CONTAINER_EDITCOMMANDLINE22,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN23,
		IDC_CONTAINER_DEBUG23,
		IDC_CONTAINER_NOAUTOLAUNCH23,
		IDC_CONTAINER_GO23,
		IDC_CONTAINER_STATUSTEXT23,
		IDC_CONTAINER_STATE23,
		IDC_CONTAINER_EDITCOMMANDLINE23,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN24,
		IDC_CONTAINER_DEBUG24,
		IDC_CONTAINER_NOAUTOLAUNCH24,
		IDC_CONTAINER_GO24,
		IDC_CONTAINER_STATUSTEXT24,
		IDC_CONTAINER_STATE24,
		IDC_CONTAINER_EDITCOMMANDLINE24,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN25,
		IDC_CONTAINER_DEBUG25,
		IDC_CONTAINER_NOAUTOLAUNCH25,
		IDC_CONTAINER_GO25,
		IDC_CONTAINER_STATUSTEXT25,
		IDC_CONTAINER_STATE25,
		IDC_CONTAINER_EDITCOMMANDLINE25,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN26,
		IDC_CONTAINER_DEBUG26,
		IDC_CONTAINER_NOAUTOLAUNCH26,
		IDC_CONTAINER_GO26,
		IDC_CONTAINER_STATUSTEXT26,
		IDC_CONTAINER_STATE26,
		IDC_CONTAINER_EDITCOMMANDLINE26,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN27,
		IDC_CONTAINER_DEBUG27,
		IDC_CONTAINER_NOAUTOLAUNCH27,
		IDC_CONTAINER_GO27,
		IDC_CONTAINER_STATUSTEXT27,
		IDC_CONTAINER_STATE27,
		IDC_CONTAINER_EDITCOMMANDLINE27,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN28,
		IDC_CONTAINER_DEBUG28,
		IDC_CONTAINER_NOAUTOLAUNCH28,
		IDC_CONTAINER_GO28,
		IDC_CONTAINER_STATUSTEXT28,
		IDC_CONTAINER_STATE28,
		IDC_CONTAINER_EDITCOMMANDLINE28,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN29,
		IDC_CONTAINER_DEBUG29,
		IDC_CONTAINER_NOAUTOLAUNCH29,
		IDC_CONTAINER_GO29,
		IDC_CONTAINER_STATUSTEXT29,
		IDC_CONTAINER_STATE29,
		IDC_CONTAINER_EDITCOMMANDLINE29,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN30,
		IDC_CONTAINER_DEBUG30,
		IDC_CONTAINER_NOAUTOLAUNCH30,
		IDC_CONTAINER_GO30,
		IDC_CONTAINER_STATUSTEXT30,
		IDC_CONTAINER_STATE30,
		IDC_CONTAINER_EDITCOMMANDLINE30,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN31,
		IDC_CONTAINER_DEBUG31,
		IDC_CONTAINER_NOAUTOLAUNCH31,
		IDC_CONTAINER_GO31,
		IDC_CONTAINER_STATUSTEXT31,
		IDC_CONTAINER_STATE31,
		IDC_CONTAINER_EDITCOMMANDLINE31,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN32,
		IDC_CONTAINER_DEBUG32,
		IDC_CONTAINER_NOAUTOLAUNCH32,
		IDC_CONTAINER_GO32,
		IDC_CONTAINER_STATUSTEXT32,
		IDC_CONTAINER_STATE32,
		IDC_CONTAINER_EDITCOMMANDLINE32,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN33,
		IDC_CONTAINER_DEBUG33,
		IDC_CONTAINER_NOAUTOLAUNCH33,
		IDC_CONTAINER_GO33,
		IDC_CONTAINER_STATUSTEXT33,
		IDC_CONTAINER_STATE33,
		IDC_CONTAINER_EDITCOMMANDLINE33,
		0,
	},
	{
		IDC_CONTAINER_HIDDEN34,
		IDC_CONTAINER_DEBUG34,
		IDC_CONTAINER_NOAUTOLAUNCH34,
		IDC_CONTAINER_GO34,
		IDC_CONTAINER_STATUSTEXT34,
		IDC_CONTAINER_STATE34,
		IDC_CONTAINER_EDITCOMMANDLINE34,
		0,
	}
};

//if this assert hits, then you chanegd MAX_CONTAINER_TYPES without adding
//to gContainerButtonIDs. To do this, you need to hand-edit both masterControlProgram.rc and
//resource_MasterControlProgram.h, find the block of IDs that are currently at the end of
//gContainerButtonIDs, copy them, and increment their ID#s.
STATIC_ASSERT(ARRAY_SIZE(gContainerButtonIDs) == MAX_CONTAINER_TYPES);




#endif



int gTimer;
int sCreatedControllerPID = 0;




GlobalType gCurActiveServerType;


void WriteSettingsToFile()
{
#if STANDALONE
	ParserWriteRegistryStringified("CrypticLauncherSettings", parse_GlobalMCPDynamicSettings, &gGlobalDynamicSettings, 0, 0, "Settings");
#else
	char fullPath[MAX_PATH];

	sprintf(fullPath, "%s/server/MCPDynamicSettings.txt", fileLocalDataDir());
	ParserWriteTextFile(fullPath, parse_GlobalMCPDynamicSettings, &gGlobalDynamicSettings, 0, 0);
#endif
}

int SortDynamicSettingsBySlot(const ContainerDynamicInfo **pInfo1, const ContainerDynamicInfo **pInfo2)
{
	if ((*pInfo1)->iSlotNum < (*pInfo2)->iSlotNum)
	{
		return -1;
	}
	else if ((*pInfo1)->iSlotNum > (*pInfo2)->iSlotNum)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
int SortStaticSettingsBySlot(const ContainerStaticInfo **pInfo1, const ContainerStaticInfo **pInfo2)
{
	if ((*pInfo1)->iSlotNum < (*pInfo2)->iSlotNum)
	{
		return -1;
	}
	else if ((*pInfo1)->iSlotNum > (*pInfo2)->iSlotNum)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

#if !STANDALONE
int SortArtistStaticSettingsBySlot(const ArtistToolStaticInfo **pInfo1, const ArtistToolStaticInfo **pInfo2)
{
	if ((*pInfo1)->iToolNum < (*pInfo2)->iToolNum)
	{
		return -1;
	}
	else if ((*pInfo1)->iToolNum > (*pInfo2)->iToolNum)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}


int SortArtistDynamicSettingsBySlot(const ArtistToolDynamicInfo **pInfo1, const ArtistToolDynamicInfo **pInfo2)
{
	if ((*pInfo1)->iToolNum < (*pInfo2)->iToolNum)
	{
		return -1;
	}
	else if ((*pInfo1)->iToolNum > (*pInfo2)->iToolNum)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
#endif


void RemoveFromSlots(GlobalType eType)
{
	int i, j;

	for (i = eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo) - 1; i >= 0; i--)
	{
		if (gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType == eType)
		{
			gGlobalDynamicSettings.ppContainerDynamicInfo[i]->iSlotNum = -2;
			for (j = i + 1; j < eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo); j++)
			{
				if (gGlobalDynamicSettings.ppContainerDynamicInfo[j]->iSlotNum > 0)
				{
					gGlobalDynamicSettings.ppContainerDynamicInfo[j]->iSlotNum--;
				}
			}

			//removes this element, sticks it back in at the end of the list
			eaPush(&gGlobalDynamicSettings.ppContainerDynamicInfo, eaRemove(&gGlobalDynamicSettings.ppContainerDynamicInfo, i));

			break;


		}
	}

	for (i = eaSize(&gGlobalStaticSettings.ppContainerStaticInfo) - 1; i >= 0; i--)
	{
		if (gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType == eType)
		{
			gGlobalStaticSettings.ppContainerStaticInfo[i]->iSlotNum = -2;
			for (j = i + 1; j < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo); j++)
			{
				if (gGlobalStaticSettings.ppContainerStaticInfo[j]->iSlotNum > 0)
				{
					gGlobalStaticSettings.ppContainerStaticInfo[j]->iSlotNum--;
				}
			}
			//removes this element, sticks it back in at the end of the list
			eaPush(&gGlobalStaticSettings.ppContainerStaticInfo, eaRemove(&gGlobalStaticSettings.ppContainerStaticInfo, i));

			break;



		}
	}
}

void FixupServerTypeConfigs(void)
{
	ServerTypeConfiguration *pAll = StructCreate(parse_ServerTypeConfiguration);
	ServerTypeConfiguration *pMinimal = StructCreate(parse_ServerTypeConfiguration);
	int i, j;
	int iConfigNum;

	pAll->pName = strdup("all");
	pAll->pComment = strdup("All possible server types");

	for (i=0 ; i < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo); i++)
	{
		ea32Push(&pAll->pFixedUpTypes, gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType);
	}

	eaInsert(&gGlobalStaticSettings.ppServerTypeConfigurations, pAll, 1);

	pMinimal->pName = strdup("MinimalShard");
	pMinimal->pComment = strdup("Minimal servers needed for a functional shard");

	for (i=0 ; i < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo); i++)
	{
		if (gGlobalStaticSettings.ppContainerStaticInfo[i]->bIsInMinimalShard)
		{
			ea32Push(&pMinimal->pFixedUpTypes, gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType);
		}
	}

	eaInsert(&gGlobalStaticSettings.ppServerTypeConfigurations, pMinimal, 0);



	//remove duplicates, which will always exist in dev mode due to the way c:\src is loaded
	for (i = 0; i < eaSize(&gGlobalStaticSettings.ppServerTypeConfigurations); i++)
	{
		for (j = eaSize(&gGlobalStaticSettings.ppServerTypeConfigurations) - 1; j > i; j--)
		{
			if (stricmp(gGlobalStaticSettings.ppServerTypeConfigurations[i]->pName, gGlobalStaticSettings.ppServerTypeConfigurations[j]->pName) == 0)
			{
				StructDestroy(parse_ServerTypeConfiguration, gGlobalStaticSettings.ppServerTypeConfigurations[j]);
				eaRemoveFast(&gGlobalStaticSettings.ppServerTypeConfigurations, j);
			}
		}
	}

	for (iConfigNum = 1; iConfigNum < eaSize(&gGlobalStaticSettings.ppServerTypeConfigurations); iConfigNum++)
	{
		ServerTypeConfiguration *pCurConfig = gGlobalStaticSettings.ppServerTypeConfigurations[iConfigNum];

		assertmsgf(pCurConfig->pName && pCurConfig->pName[0], "Server type config has no name");

		if (pCurConfig->pBasedOn && pCurConfig->pBasedOn[0])
		{
			ServerTypeConfiguration *pBasedOnConfig = NULL;


			for (i=0; i < iConfigNum; i++)
			{
				if (stricmp(gGlobalStaticSettings.ppServerTypeConfigurations[i]->pName, pCurConfig->pBasedOn) == 0)
				{
					pBasedOnConfig = gGlobalStaticSettings.ppServerTypeConfigurations[i];
					break;
				}
			}

			assertmsgf(pBasedOnConfig, "Couldn't find server type config config \"%s\" to base config \"%s\" on",
				pCurConfig->pBasedOn, pCurConfig->pName);

			ea32Copy(&pCurConfig->pFixedUpTypes, &pBasedOnConfig->pFixedUpTypes);
		}
		else
		{
			assertmsgf(ea32Size(&pCurConfig->pRemove) == 0, "While fixing up server type config \"%s\", tried to remove server types without being based on anything",
				pCurConfig->pName);
		}

		for (i=0; i < ea32Size(&pCurConfig->pRemove); i++)
		{
			ea32FindAndRemoveFast(&pCurConfig->pFixedUpTypes, pCurConfig->pRemove[i]);
		}

		for (i=0; i < ea32Size(&pCurConfig->pAdd); i++)
		{
			ea32Push(&pCurConfig->pFixedUpTypes, pCurConfig->pAdd[i]);
		}
	}
}

ServerTypeConfiguration *FindServerTypeConfig(char *pName)
{
	int i;

	for (i=0; i < eaSize(&gGlobalStaticSettings.ppServerTypeConfigurations); i++)
	{
		if (stricmp(gGlobalStaticSettings.ppServerTypeConfigurations[i]->pName, pName) == 0)
		{
			return gGlobalStaticSettings.ppServerTypeConfigurations[i];
		}
	}

	return NULL;
}

void ReadSettingsFromFile()
{
	int i, j;
	int iNextEmptySlot = 0;
	int *piDupIndices = NULL;
	ServerTypeConfiguration *pServerTypeConfig;
	U32 *pUnusedTypes = NULL;
#if !STANDALONE
	char fullPath[CRYPTIC_MAX_PATH];
#endif

	{
		HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(MCP_SERVER_SETUP_TXT), "TXT");
		if (rsrc)
		{
			HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
			if (gptr)
			{
				void *pTxtFile = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
				size_t iExeSize = SizeofResource(GetModuleHandle(NULL), rsrc);

		
				ParserReadTextForFile(pTxtFile, "src\\data\\server\\MCPServerSetup.txt", parse_GlobalMCPStaticSettings, &gGlobalStaticSettings, 0);
			}
		}	
	}

	ParserLoadFiles(GetDirForBaseConfigFiles(), "MCPServerSetup.txt", NULL, 0, parse_GlobalMCPStaticSettings, &gGlobalStaticSettings);

#if !STANDALONE
	assertmsg(eaSize(&gGlobalStaticSettings.ppContainerStaticInfo) > 5, "Couldn't load core_mcpServerSetup.txt. Your core/data folder is missing or corrupt or set up wrong.");
#endif

#if STANDALONE
	ParserReadRegistryStringified("CrypticLauncherSettings", parse_GlobalMCPDynamicSettings, &gGlobalDynamicSettings, "Settings");
#else
	sprintf(fullPath, "%s/server/MCPDynamicSettings.txt", fileLocalDataDir());
	ParserReadTextFile(fullPath, parse_GlobalMCPDynamicSettings, &gGlobalDynamicSettings, 0);
#endif





	//check for duplicate container types in static settings (for instance, if the same container type is in
	//core_ and fc_ server setup files, or one is compiled into controller.exe, one is loaded)
	for (i=0; i < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo) - 1; i++)
	{
		for (j=i+1; j < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo); j++)
		{
			if (gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType == gGlobalStaticSettings.ppContainerStaticInfo[j]->eContainerType)
			{
				//mark earlier one for removal
				gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType = GLOBALTYPE_NONE;
				break;

			}
		}
	}

	for (i = eaSize(&gGlobalStaticSettings.ppContainerStaticInfo) - 1; i >= 0; i--)
	{
		GlobalType eType = gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType;

		if (eType == GLOBALTYPE_NONE || stricmp(GlobalTypeToName(eType), GlobalTypeToName(GLOBALTYPE_NONE)) == 0)
		{
			StructDestroy(parse_ContainerStaticInfo, gGlobalStaticSettings.ppContainerStaticInfo[i]);
			eaRemove(&gGlobalStaticSettings.ppContainerStaticInfo, i);
		}
	}

	//remove all unknown or obsolete dynamic types
	for (i = eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo) - 1; i >= 0; i--)
	{
		GlobalType eType = gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType;

		if (eType == GLOBALTYPE_NONE || stricmp(GlobalTypeToName(eType), GlobalTypeToName(GLOBALTYPE_NONE)) == 0)
		{
			StructDestroy(parse_ContainerDynamicInfo, gGlobalDynamicSettings.ppContainerDynamicInfo[i]);
			eaRemove(&gGlobalDynamicSettings.ppContainerDynamicInfo, i);
		}
	}


	//check for duplicates in dynamic settings (not sure how this could happen, but might as well check
	for (i=0; i < eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo) - 1; i++)
	{
		for (j=i+1; j < eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo); j++)
		{
			if (gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType == gGlobalDynamicSettings.ppContainerDynamicInfo[j]->eContainerType
				&& gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType != GLOBALTYPE_NONE) // NONE happens for invalid entries which we're complained about elsewhere.
			{
				AssertOrAlert("DUP_MCP_DYN_SETTINGS", "Duplicate MCPDynamicSettings.txt entries for container type %s. WTF did you do, man?",
					GlobalTypeToName(gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType));
			}
		}
	}



	//assign slot numbers to static settings with slot number -1
	for (i=0; i < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo); i++)
	{
		if (gGlobalStaticSettings.ppContainerStaticInfo[i]->iSlotNum >= iNextEmptySlot)
		{
			iNextEmptySlot = gGlobalStaticSettings.ppContainerStaticInfo[i]->iSlotNum + 1;
		}
	}

	for (i=0; i < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo); i++)
	{
		if (gGlobalStaticSettings.ppContainerStaticInfo[i]->iSlotNum == -1)
		{
			gGlobalStaticSettings.ppContainerStaticInfo[i]->iSlotNum = iNextEmptySlot++;
		}
	}

	//now sort static settings
	eaQSort(gGlobalStaticSettings.ppContainerStaticInfo, SortStaticSettingsBySlot);

	//now, set the slot num of all dynamic container info to -1, so we can identify obsolete container info
	for (i=0; i < eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo); i++)
	{
		gGlobalDynamicSettings.ppContainerDynamicInfo[i]->iSlotNum = -1;
	}


	//now, for each static data, create the accompanying dynamic data, if not already present
	for (i=0 ; i < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo); i++)
	{
		bool bFound = false;

		for (j=0; j < eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo); j++)
		{
			if (gGlobalDynamicSettings.ppContainerDynamicInfo[j]->eContainerType == gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType)
			{
				gGlobalDynamicSettings.ppContainerDynamicInfo[j]->iSlotNum = i;
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			ContainerDynamicInfo *pDynamicInfo = StructCreate(parse_ContainerDynamicInfo);
			pDynamicInfo->eContainerType = gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType;
			pDynamicInfo->iSlotNum = i;
			pDynamicInfo->bHideButtonChecked = !(pDynamicInfo->eContainerType == GLOBALTYPE_GAMESERVER || pDynamicInfo->eContainerType == GLOBALTYPE_CLIENT);
			eaPush(&gGlobalDynamicSettings.ppContainerDynamicInfo, pDynamicInfo);
		}
	}



	for (i=0; i < eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo); i++)
	{
		//replace default=2 value for bAutoLaunchButtonChecked with better values
		if (gGlobalDynamicSettings.ppContainerDynamicInfo[i]->bAutoLaunchButtonChecked == 2)
		{
			switch (gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType)
			{
			//server types that should be ON by default for development
			case GLOBALTYPE_LOGSERVER:
			case GLOBALTYPE_OBJECTDB:
			case GLOBALTYPE_TRANSACTIONSERVER:
			case GLOBALTYPE_GAMESERVER:
			case GLOBALTYPE_CLIENT:
			case GLOBALTYPE_MAPMANAGER:
			case GLOBALTYPE_LOGINSERVER:
			case GLOBALTYPE_CONTROLLER:
			case GLOBALTYPE_LAUNCHER:
			case GLOBALTYPE_MULTIPLEXER:
			case GLOBALTYPE_ACCOUNTSERVER:
				gGlobalDynamicSettings.ppContainerDynamicInfo[i]->bAutoLaunchButtonChecked = 0;
				break;

			//server types that should be OFF by default for development
			case GLOBALTYPE_CLONEOBJECTDB:
			case GLOBALTYPE_TEAMSERVER:
			case GLOBALTYPE_LEADERBOARDSERVER:
			case GLOBALTYPE_LOGPARSER:
			case GLOBALTYPE_AUCTIONSERVER:
			case GLOBALTYPE_GUILDSERVER:
			case GLOBALTYPE_CHATSERVER:
			case GLOBALTYPE_CHATRELAY:
			case GLOBALTYPE_QUEUESERVER:
			case GLOBALTYPE_ACCOUNTPROXYSERVER:
			case GLOBALTYPE_TESTCLIENTSERVER:
			case GLOBALTYPE_DIARYSERVER:
			case GLOBALTYPE_GLOBALCHATSERVER:
			case GLOBALTYPE_TESTSERVER:
			case GLOBALTYPE_JOBMANAGER:
			case GLOBALTYPE_BCNMASTERSERVER:
			case GLOBALTYPE_BCNCLIENTSENTRY:
			case GLOBALTYPE_WEBREQUESTSERVER:
			case GLOBALTYPE_GATEWAYSERVER:
			case GLOBALTYPE_CLIENTBINNER:
			case GLOBALTYPE_SERVERBINNER:
			case GLOBALTYPE_RESOURCEDB:
			case GLOBALTYPE_UGCSEARCHMANAGER:
			case GLOBALTYPE_CLONEOFCLONE:
			case GLOBALTYPE_LOGINHAMMER:
            case GLOBALTYPE_GROUPPROJECTSERVER:
			case GLOBALTYPE_TEXTURESERVER:
			case GLOBALTYPE_GATEWAYLOGINLAUNCHER:
			case GLOBALTYPE_UGCDATAMANAGER:
            case GLOBALTYPE_CURRENCYEXCHANGESERVER:
					gGlobalDynamicSettings.ppContainerDynamicInfo[i]->bAutoLaunchButtonChecked = 1;
				break;

			default:
				if (gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType != GLOBALTYPE_NONE)
					Errorf("server type %s neither default-on nor default-off.  Add the server type to the first switch in ReadSettingsFromFile() in MasterControlProgram.c.", GlobalTypeToName(gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType));
				break;
			}
		}
	}

	//now remove any dynamic info that still has slot -1, and thus corresponds to no static info
	i = 0;
	while (i < eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo))
	{
		if (gGlobalDynamicSettings.ppContainerDynamicInfo[i]->iSlotNum == -1)
		{
			eaRemoveFast(&gGlobalDynamicSettings.ppContainerDynamicInfo, i);
		}
		else
		{
			i++;
		}
	}

	//now sort dynamic settings
	eaQSort(gGlobalDynamicSettings.ppContainerDynamicInfo, SortDynamicSettingsBySlot);

	FixupServerTypeConfigs();

	if (gGlobalDynamicSettings.serverTypeConfig[0] == 0 || !FindServerTypeConfig(gGlobalDynamicSettings.serverTypeConfig))
	{
		sprintf(gGlobalDynamicSettings.serverTypeConfig, "all");
	}

	//this should never fail, because we just fixed it up one line higher
	pServerTypeConfig = FindServerTypeConfig(gGlobalDynamicSettings.serverTypeConfig);

	for (i=0 ; i < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo); i++)
	{
		ea32Push(&pUnusedTypes, gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType);
	}

	for (i=0; i < ea32Size(&pServerTypeConfig->pFixedUpTypes); i++)
	{
		ea32FindAndRemoveFast(&pUnusedTypes, pServerTypeConfig->pFixedUpTypes[i]);
	}

	for (i=0 ; i < eaSize(&gGlobalDynamicSettings.ppContainerDynamicInfo); i++)
	{
		if (gGlobalDynamicSettings.ppContainerDynamicInfo[i]->bForceHideServerType)
		{
			ea32PushUnique(&pUnusedTypes, gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType);
		}
		else if (gGlobalDynamicSettings.ppContainerDynamicInfo[i]->bForceShowServerType)
		{
			ea32FindAndRemoveFast(&pUnusedTypes, gGlobalDynamicSettings.ppContainerDynamicInfo[i]->eContainerType);
		}
	}

	for (i=0; i < ea32Size(&pUnusedTypes); i++)
	{
		RemoveFromSlots(pUnusedTypes[i]);
	}

	ea32Destroy(&pUnusedTypes);







#if !STANDALONE
	//now do the same for artist tools
	//now sort static settings
	eaQSort(gGlobalStaticSettings.ppArtistToolStaticInfo, SortArtistStaticSettingsBySlot);


	//now, for each static data, create the accompanying dynamic data, if not already present
	for (i=0 ; i < eaSize(&gGlobalStaticSettings.ppArtistToolStaticInfo); i++)
	{
		bool bFound = false;

		for (j=0; j < eaSize(&gGlobalDynamicSettings.ppArtistToolDynamicInfo); j++)
		{
			if (gGlobalDynamicSettings.ppArtistToolDynamicInfo[j]->iToolNum == gGlobalStaticSettings.ppArtistToolStaticInfo[i]->iToolNum)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			ArtistToolDynamicInfo *pDynamicInfo = StructCreate(parse_ArtistToolDynamicInfo);
			pDynamicInfo->iToolNum = i;
			eaPush(&gGlobalDynamicSettings.ppArtistToolDynamicInfo, pDynamicInfo);
		}
	}

	//now sort dynamic settings
	eaQSort(gGlobalDynamicSettings.ppArtistToolDynamicInfo, SortArtistDynamicSettingsBySlot);
#endif

}


#if !STANDALONE


bool IsStartInDebuggerChecked(GlobalType eServerType)
{
	return GetContainerDynamicInfoFromType(eServerType)->bDebugButtonChecked;
}

bool IsHideChecked(GlobalType eServerType)
{
	return GetContainerDynamicInfoFromType(eServerType)->bHideButtonChecked;

}





void UpdateMasterControlProgramTitle(void)
{
	char buf[200];
	char cwd[200];

	char *pResult = _getcwd(cwd, ARRAY_SIZE(cwd));

	sprintf_s(SAFESTR(buf), "MasterControlProgram %s", cwd);
	setConsoleTitle(buf);
}

void SetServerStatus(GlobalType eServerType, enumServerState eState, char *pStateString)
{
	GetContainerStaticInfoFromType(eServerType)->eState = eState;
	strcpy(GetContainerStaticInfoFromType(eServerType)->stateString, pStateString);
}

void HandleLocalStatus(Packet *pak, NetLink *link)
{
	GlobalType eServerType;

	while ((eServerType = GetContainerTypeFromPacket(pak)) != GLOBALTYPE_NONE)
	{
		if (pktGetBits(pak, 1))
		{
			bool bCrashed = pktGetBits(pak, 1);
			bool bWaiting = pktGetBits(pak, 1);
			char *msg;

			msg = pktGetStringTemp(pak);
			if (bCrashed)
			{
				SetServerStatus(eServerType, SERVER_CRASHED, msg);
			}
			else if (bWaiting)
			{
				SetServerStatus(eServerType, SERVER_WAITING, msg);
			}
			else
			{
				SetServerStatus(eServerType, SERVER_RUNNING, msg);
			}
		}
		else
		{
			SetServerStatus(eServerType, SERVER_NOT_RUNNING, "");
		}
	}
}

void HandleErrorDialogMsg(Packet *pPak)
{
	if (gbYouAreMonitoringMCP)
	{
		return;
	}
	else
	{
		if (gbDisableNewErrorScreen)
		{
			QueuedErrorDialog *pDialog = calloc(sizeof(QueuedErrorDialog), 1);

			pDialog->pString = pktMallocString(pPak);
			pDialog->pTitle = pktMallocString(pPak);
			pDialog->pFault = pktMallocString(pPak);
			pDialog->iHighlight = pktGetBitsPack(pPak, 32);

			eaPush(&ppQueuedErrorDialogs, pDialog);
		}
		else
		{
			ReportMCPError(pktGetStringTemp(pPak));
		}
	}

}

void HandleScriptUpdate(Packet *pak)
{
	char *pString = pktGetStringTemp(pak);
	int iImportance = pktGetBits(pak, 1);

	if (iImportance)
	{
		SetControllerScriptingSubHead(pString);
	}
	else
	{
		AddControllerScriptingMainText(pString);
	}
}

void HandleXpathHttp(Packet *pak)
{
	StructInfoForHttpXpath structInfo = {0};
	GlobalType eServingType = GetContainerTypeFromPacket(pak);
	ContainerID iServingID = GetContainerIDFromPacket(pak);
	int iRequestID = pktGetBits(pak, 32);
	ParserRecv(parse_StructInfoForHttpXpath, pak, &structInfo, 0);

	handle_MCPHttpMonitoringProcessXpath(eServingType, iServingID, iRequestID, &structInfo);
	StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
}

extern void HandleScriptState(Packet *pak);

void MasterControlProgramHandleMsg(Packet *pak,int cmd, NetLink *link,void *userdata)
{
	switch(cmd)
	{
	xcase FROM_CONTROLLER_HERE_IS_LOCAL_STATUS:
		HandleLocalStatus(pak, link);
	xcase FROM_CONTROLLER_HERE_IS_SERVER_LIST_FOR_MCP:
		ServerList_HandleUpdate(pak);
	xcase FROM_CONTROLLER_HERE_IS_ERROR_DIALOG_FOR_MCP:
		HandleErrorDialogMsg(pak);
	xcase FROM_CONTROLLER_SCRIPT_UPDATE_FOR_MCP:
		HandleScriptUpdate(pak);
	xcase FROM_CONTROLLER_SCRIPT_STATE_FOR_MCP:
		HandleScriptState(pak);
	xcase FROM_CONTROLLER_HERE_IS_XPATH_HTTP_FOR_MCP:
		HandleXpathHttp(pak);
	xcase FROM_CONTROLLER_MONITORING_COMMAND_RETURN:
		HandleMonitoringCommandReturn(pak);
	xcase FROM_CONTROLLER_HERE_IS_JPEG_FOR_MCP:
		HandleJpegReturn(pak);
	xcase FROM_CONTROLLER_FILE_SERVING_RETURN:
		HandleFileServingReturn(pak);
		
	xdefault:
		printf("Unknown command %d\n",cmd);
	}
}

#endif

/*
int MCPControllerTrackerCallback(Packet *pak,int cmd, NetLink *link, void *user_data)
{
	switch(cmd)
	{
	case FROM_CONTROLLERTRACKER_LISTOFPATCHEDCONTROLLERS_WITH_AUTOCLIENTCOMMANDLINES:
		{
			int iNumServers = pktGetBitsPack(pak, 1);
			int i;
			LRESULT lResult;

			char curServerName[SERVER_NAME_LENGTH] = "";

			SimpleWindow *pStartingWindow = SimpleWindowManager_FindWindow(MCPWINDOW_STARTING, 0);

			if (pStartingWindow)
			{

				if (giNumAvailableServers)
				{
					lResult = SendMessage(GetDlgItem(pStartingWindow->hWnd, IDC_CHOOSESERVER), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
					if (lResult != CB_ERR) 
					{
						lResult = SendMessage(GetDlgItem(pStartingWindow->hWnd, IDC_CHOOSESERVER), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
						if (lResult != CB_ERR) 
						{
							strcpy_s(curServerName, SERVER_NAME_LENGTH, gAvailableServers[lResult].name);
						}
					}
				}

				SendMessage(GetDlgItem(pStartingWindow->hWnd, IDC_CHOOSESERVER), CB_RESETCONTENT, 0, 0);

				if (iNumServers > MAX_AVAILABLE_SERVERS)
				{
					iNumServers = MAX_AVAILABLE_SERVERS;
				}

				giNumAvailableServers = iNumServers;

				for (i=0; i < iNumServers; i++)
				{
					char *pServerName;
					char *pServerPatchCmdLine;
					char *pAutoClientCmdLine;
					U32 IP;

					pServerName = pktGetStringTemp(pak);
					IP = pktGetBits(pak, 32);
					pServerPatchCmdLine = pktGetStringTemp(pak);
					pAutoClientCmdLine = pktGetStringTemp(pak);

					strcpy_s(gAvailableServers[i].name, SERVER_NAME_LENGTH, pServerName);
					gAvailableServers[i].IP = IP;
					strcpy(gAvailableServers[i].patchString, pServerPatchCmdLine);
					strcpy(gAvailableServers[i].auotClientCommandLine, pAutoClientCmdLine);
		
					lResult = SendMessage(GetDlgItem(pStartingWindow->hWnd, IDC_CHOOSESERVER), CB_ADDSTRING, 0, (LPARAM)pServerName);
					SendMessage(GetDlgItem(pStartingWindow->hWnd, IDC_CHOOSESERVER), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);

					if (curServerName[0] == 0)
					{
						strcpy_s(curServerName, SERVER_NAME_LENGTH, pServerName);
					}

				}

				SendMessage(GetDlgItem(pStartingWindow->hWnd, IDC_CHOOSESERVER), CB_SELECTSTRING, 0, (LPARAM)curServerName);
			}

			gbAvailableServersChanged = true;

		}
		break;

	xcase FROM_CONTROLLERTRACKER_LAST_MINUTE_FILES:
		HandleHereAreLastMinuteFiles(pak);

	xdefault:
		printf("Unknown command %d\n",cmd);
		return 0;
	}
	return 1;
}
*/





#if !STANDALONE

void CreateController(bool bStartInDebugger, bool bHide, bool bStartLocalLauncherInDebugger, bool bHideLocalLauncher, char *pControllerScriptFileName)
{
	char *pFullCommandLine = NULL;
	

	char controllerScriptFileCommandLine[256] = "";

	U32 pid;

	char *pLauncherCommandLine = NULL;
	char *pEscapedLauncherCommandLine = NULL;

	ResetMCPErrors();

	estrPrintf(&pLauncherCommandLine, "%s", GetDynamicCommandLine(GLOBALTYPE_LAUNCHER) ? GetDynamicCommandLine(GLOBALTYPE_LAUNCHER) : "");
	estrSuperEscapeString(&pEscapedLauncherCommandLine, pLauncherCommandLine);

	

	if (pControllerScriptFileName && pControllerScriptFileName[0])
	{
		sprintf(controllerScriptFileCommandLine, "-ScriptFile %s", pControllerScriptFileName);
	}

	KillAllEx("controller.exe", false, NULL, true, true, NULL);
	KillAllEx("controllerX64.exe", false, NULL, true, true, NULL);

	estrPrintf(&pFullCommandLine, "\"%s/Controller%s.exe\"  -SetAccountServer localhost -Cookie %u %s %s -StartedByMCP %u:%d %s %s %s %s %s -ExecDir \"%s\" -CoreExecDir \"%s\" -SetProductName %s %s - %s - %s - -superEsc SetLauncherCommandLine %s",
		    fileCoreExecutableDir(),
			sbController64 ? "X64" : "",
			gServerLibState.antiZombificationCookie,
			bHide ? "-hide" : "", controllerScriptFileCommandLine, 
			gServerLibState.containerID, _getpid(), bStartInDebugger ? "-WaitForDebugger" : "", 
			gbRunPublic ? "-ConnectToControllerTracker 1" : "",
			bStartLocalLauncherInDebugger ? "-StartLocalLauncherInDebugger" : "", 
			bHideLocalLauncher ? "-HideLocalLauncher" : "",
			GetContainerDynamicInfoFromType(GLOBALTYPE_GAMESERVER)->bDebugButtonChecked ? "-StartGameServersInDebugger" : "",
			fileExecutableDir(),
			fileCoreExecutableDir(),
			GetProductName(), GetShortProductName(),
			GetGlobalCommandLine(),
			GetDynamicCommandLine(GLOBALTYPE_CONTROLLER),
			pEscapedLauncherCommandLine ? pEscapedLauncherCommandLine : "");
	
	pid = system_detach_with_fulldebug_fixup(pFullCommandLine, 1, bHide);

	if (bStartInDebugger)
	{
		AttachDebugger(pid);
	}

	estrDestroy(&pFullCommandLine);
	estrDestroy(&pLauncherCommandLine);
	estrDestroy(&pEscapedLauncherCommandLine);
}

int GetUniqueID()
{
	SYSTEMTIME sysTime;

	GetSystemTime(&sysTime);

	
	return (int)timeMsecsSince2000();
}
#endif



#if !STANDALONE

void MCPReceivePrintfCB(char *pLinkName, int iLinkID, char *pString, int iFGColor, int iBGColor)
{
	char *pCh;
	char tempString[32];

	if (iLinkID < 0 || iLinkID >= MAX_PRINTF_BUFFERS)
	{
		return;
	}


	//check if there's any non-whitespace in this string
	pCh = pString;
	while (*pCh)
	{
		if (!IS_WHITESPACE(*pCh))
		{
			gbPrintfBuffersChanged[iLinkID] = true;

			if (iFGColor == (COLOR_RED|COLOR_BRIGHT))
			{
				SetArtToolNeedsAttention(iLinkID);
			}

			break;
		}

		pCh++;
	}

	if (!gPrintfBuffers[iLinkID])
	{
		estrCreate(&gPrintfBuffers[iLinkID]);
	}

	sprintf_s(SAFESTR(tempString), "\\cf%d ", iFGColor + 1);

	estrAppend2(&gPrintfBuffers[iLinkID], tempString);

	while (*pString)
	{
		if (*pString == '{' || *pString == '}' || *pString == '\\')
		{
			sprintf_s(SAFESTR(tempString), "\\%c", *pString);
			estrAppend2(&gPrintfBuffers[iLinkID], tempString);
		}
		else if (*pString == '\n')
		{
			estrAppend2(&gPrintfBuffers[iLinkID], "\\par ");
		}
		else if (*pString == '\r')
		{
			if (*(pString + 1) != '\n')
			{
				char *pLastNewLine = strrstr(gPrintfBuffers[iLinkID], "\\par ");

				if (pLastNewLine)
				{
				
					estrSetSize(&gPrintfBuffers[iLinkID], pLastNewLine - gPrintfBuffers[iLinkID] + 5);
					
				}	
			}
		}
		else
		{
			estrConcat(&gPrintfBuffers[iLinkID], pString, 1);
		}

		pString++;
	}

	//check if buffer is too large
	while (estrLength(&gPrintfBuffers[iLinkID]) > MAX_PRINTF_BUFFER_LENGTH)
	{
		char *pFirstNewLine = strstr(gPrintfBuffers[iLinkID], "\\par ");

		if (!pFirstNewLine)
		{
			pFirstNewLine  = strchr(gPrintfBuffers[iLinkID], '\n');
		}

		if (pFirstNewLine)
		{
			estrRemove(&gPrintfBuffers[iLinkID], 0, pFirstNewLine - gPrintfBuffers[iLinkID] + 5);
		}
		else
		{
			estrRemove(&gPrintfBuffers[iLinkID], 0, MAX_PRINTF_BUFFER_LENGTH / 4);
		}
	}
	

}

void CreateAndConnectToController(char *pControllerScriptFileName)
{
	bool bNeedMessageWindow = false;
	static bool bFirstTime = true;

	PERFINFO_AUTO_START_FUNC();

	if (bFirstTime)
	{
		bFirstTime = false;

		if ((ProcessCount("mastercontrolprogram.exe", true) > 1) && (ProcessCount("controller.exe", true) || ProcessCount("controllerX64.exe", true)) && !gbDontCreateController)
		{
			gbDontCreateController = true;
			sprintf(gGenericMessage, "You already have an MCP and a controller running, so this MCP can not create a controller for you");
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_GENERICMESSAGE, 0, IDD_GENERICMESSAGE, false,
					genericMessageMenuDlgProc, NULL, NULL);

		}
	}	

	gbControllerHasALauncher = false;

	if (!GetControllerLink())
	{
		U32 iStartTime;
		loadstart_printf("Attempting to create and connect to controller");
	
		loadstart_printf("Create controller");
		if (!gbDontCreateController)
		{
			KillAllEx("controller.exe", false, NULL, true, true, NULL);			
			KillAllEx("controllerX64.exe", false, NULL, true, true, NULL);
			KillAllEx("launcher.exe", false, NULL, true, true, NULL);
			KillAllEx("launcherX64.exe", false, NULL, true, true, NULL);
			CreateController(IsStartInDebuggerChecked(GLOBALTYPE_CONTROLLER), IsHideChecked(GLOBALTYPE_CONTROLLER),
				IsStartInDebuggerChecked(GLOBALTYPE_LAUNCHER), IsHideChecked(GLOBALTYPE_LAUNCHER), pControllerScriptFileName);
		}
		loadend_printf("done");

		iStartTime = timeSecondsSince2000();

		loadstart_printf("Controller connection");
		do
		{
			PERFINFO_AUTO_START("waiting for controller", 1);
			if (gbDontCreateController && timeSecondsSince2000() - iStartTime > 5)
			{
				//MCP doesn't sit there forever trying to connect to a controller that doesn't exist
				exit(-1);
			}
			AttemptToConnectToController(true, MasterControlProgramHandleMsg, gbHttpMonitor ? false : true);
			Sleep(1);
			commMonitor(commDefault());
			PERFINFO_AUTO_STOP();
		}
		while (!GetControllerLink());
		loadend_printf("done");
	

		loadstart_printf("Send cmd line settings");
		SendCommandLineSettingsToController();
		loadend_printf("done");


		{
			Packet *pPacket = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_IS_READY);
			pktSend(&pPacket);
		}


		loadstart_printf("Waiting for launcher");
		do
		{
			PERFINFO_AUTO_START("waiting for launcher", 1);
			Sleep(1);
			MCP_OncePerFrame(NULL);
			PERFINFO_AUTO_STOP();
		} while (!gbControllerHasALauncher && GetControllerLink());
		loadend_printf("done");
		loadend_printf("succeeded");
	}

	PERFINFO_AUTO_STOP();
}


#endif


#define SERVER_LIST_REQUEST_FREQ 60

bool MCP_OncePerFrame(SimpleWindow *pMeaninglessWindow)
{
	static int bInside = false;
	static int counter = 0;
	
	PERFINFO_AUTO_START_FUNC();

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


#if !STANDALONE

		if (counter % SERVER_LIST_REQUEST_FREQ == 0)
		{
			if (GetControllerLink())
			{
				Packet *pPak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_SERVER_LIST);
				pktSend(&pPak);
			}
		}

		if (eaSize(&ppQueuedErrorDialogs))
		{
			QueuedErrorDialog *pDialog = ppQueuedErrorDialogs[0];
			SimpleWindow *pWindow = SimpleWindowManager_FindWindowByType(MCPWINDOW_MAIN);
			if (!pWindow)
			{
				pWindow = SimpleWindowManager_FindWindowByType(MCPWINDOW_STARTING);
			}

			if (pWindow)
			{

				eaRemove(&ppQueuedErrorDialogs, 0);


				errorDialogInternal(pWindow->hWnd, pDialog->pString, pDialog->pTitle, pDialog->pFault, pDialog->iHighlight);

				free(pDialog->pString);
				free(pDialog->pTitle);
				free(pDialog->pFault);
				free(pDialog);
			}
		}	
#endif

		bInside =false;
	}
	
	commMonitor(commDefault());
#if !STANDALONE
	serverLibOncePerFrame();
	ReceiveNetLinkPrints_Monitor();
	MCPHttpMonitoringUpdate();
	FolderCacheDoCallbacks();
#endif



	PERFINFO_AUTO_STOP();

	return true;
}

/*

BOOL SWMTestSubMenuDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			return TRUE; 
		}
		break;

	case  WM_CLOSE:
		pWindow->bCloseRequested = true;
	
		break;

	}

	return FALSE;
}

void SWMTestSubMenuTick(SimpleWindow *pWindow)
{
	char tempString[256];
	sprintf(tempString, "This window has existed for %d seconds", pWindow->iTickCount / 60);

	SetTextFast(GetDlgItem(pWindow->hWnd, IDC_TICKTEXT), tempString);
}




BOOL SWMTestMainMenuDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	static int iCounter = 0;
			
	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			return TRUE; 
		}
		break;

	case  WM_CLOSE:
		pWindow->bCloseRequested = true;
	
		break;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDCREATE)
		{
			SimpleWindowManager_AddOrActivateWindow(1, iCounter++, IDD_SWMTESTSUB, false,
				SWMTestSubMenuDlgProc, SWMTestSubMenuTick, NULL);
		}
		else if (LOWORD(wParam) == IDC_RESETCOUNTER)
		{
			iCounter = 0;
		}
		break;

	}
	
	return FALSE;
}


*/


char *pProductAliases[] =
{
	"Champions", "FightClub",
	"CrypticPatcher", "all", 
};
	


#if STANDALONE
//note that for standalone mode, the "short product name" is never used, so we just set it to XX
void GetProductNameFromExecutableName(void)
{
	char exeName[MAX_PATH];
	int i;

	getFileNameNoExtNoDirs(exeName, getExecutableName());

	for (i=0 ; i < ARRAY_SIZE(pProductAliases) / 2; i++)
	{
		if (stricmp(exeName, pProductAliases[i * 2]) == 0)
		{
			SetProductName(pProductAliases[i * 2 + 1], "XX");
			return;
		}
	}
	


	SetProductName(exeName, "XX");
	
}
#endif




#if !STANDALONE
void StartMainWindow(void)
{
	if (gbComplex)
	{
		SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_MAIN, MAININDEX_COMPLEX, IDD_BGB_COMPLEX, true,
			MainScreenDlgFunc, MainScreenTickFunc, NULL);
	}
	else
	{
		if (gbArtist)
		{
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_MAIN, MAININDEX_ARTIST, IDD_BGB_ARTISTS, true,
				MainScreenDlgFunc, MainScreenTickFunc, NULL);

		}
		else
		{
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_MAIN, MAININDEX_SIMPLE, IDD_BGB_SIMPLE, true,
				MainScreenDlgFunc, MainScreenTickFunc, NULL);
		}
	}



}
#endif

S32 getMCPMutex(void){
	#if ONE_MCP_PROCESS_ONLY
	{
		HANDLE hMutex = CreateMutex(NULL, FALSE, "Local\\OneMCPPlease");
		U32 waitResult;
		assert(hMutex);
		WaitForSingleObjectWithReturn(hMutex, 0, waitResult);
		switch(waitResult){
			xcase WAIT_OBJECT_0:
			acase WAIT_ABANDONED:
				return 1;
			xdefault:
				return 0;
		}
	}
	#endif
	
	return 1;
}

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{

#if !STANDALONE
	int argc;
	char *argv[1000];
	LoadedProductInfo *productInfo;
#endif

	if(!getMCPMutex()){
		return 0;
	}

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER_LPCMDLINE
	DO_AUTO_RUNS

#if STANDALONE
	gimmeDLLDisable(1);

	fileAllPathsAbsolute(1);
#endif

	FolderCacheChooseModeNoPigsInDevelopment();

	fileAutoDataDir();

	filePrintDataDirs();

	strcpy(gExecutableFilename, getExecutableName());
	giExecutableTimestamp = fileLastChanged(gExecutableFilename);

	ghInstance = hInstance;

	setDefaultAssertMode();
	memMonitorInit();

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'T', 0x8080ff);
	
	if(isDevelopmentMode())
	{
		// TODO: REDUCE_DEV_MODE_CPU
		//commSetMinReceiveTimeoutMS(commDefault(), 33);
	}


#if !STANDALONE
	preloadDLLs(0);
	LoadLibrary("riched32.dll");
	CreateSplash();
	UpdateMasterControlProgramTitle();

	{
		argv[0] = "file.exe";
		argc = 1 + tokenize_line_quoted_safe(lpCmdLine,&argv[1],ARRAY_SIZE(argv)-1,0);

	}
#endif

	if (fileIsUsingDevData()) 
	{
	} 
	else 
	{
		gimmeDLLDisable(1);
	}

	srand((unsigned int)time(NULL));
	consoleUpSize(110,128);
	gimmeDLLCreateConsoleHidden(true);

#if STANDALONE
//attempt to get product name from executable name
	GetProductNameFromExecutableName();


	//normally this is called in ServerLibStartup(), which standalone MCP doesn't do
	utilitiesLibStartup();
	sockStart();
	cryptMD5Init();


#else
	gServerLibState.containerID = GetUniqueID();

	sprintf(gServerLibState.logServerHost, "NONE");

	serverLibStartup(argc, argv);
	setErrorDialogCallback(NULL, NULL);

	if (strcmp(GetProductName(), PRODUCT_NAME_UNSPECIFIED) == 0)
	{
		productInfo = GetProductNameFromDataFile();

		if (productInfo->productName[0])
		{
			SetProductName(productInfo->productName, productInfo->shortProductName);
		}
		else
		{
			assertmsg(0, "Couldn't load product name from productinfo.txt");
		}
	}
	
	UpdateMasterControlProgramTitle();


/*	newConsoleWindow();
	TestTextParser();
*/


	logSetDir("MasterControlProgram");
	
	loadstart_printf("Networking startup...");
	gTimer = timerAlloc();
	timerStart(gTimer);
	sockStart();
	loadend_printf("");
#endif


	ReadSettingsFromFile();

#if !STANDALONE
	RegisterToReceiveNetLinkPrintfs( DEFAULT_MCP_PRINTF_PORT, 8, MCPReceivePrintfCB );

	FindOptionBatchFiles();
	FindControllerScripts();
#endif

	printf("Ready.\n");

	SimpleWindowManager_Init("MasterControlProgram",
#if STANDALONE
	true
#else
	false
#endif
		);


#if !STANDALONE
	if (gbRunInLocalPublicMode || gbRunInLocalMode || (gbDontCreateController && gServerLibState.controllerHost[0]))
	{

		if (gbRunInLocalPublicMode)
		{
			gbRunPublic = true;
		}

		CreateAndConnectToController(GetStartingScriptFileName());
		StartMainWindow();


	}
	else
#endif
	{
//		SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_STARTING, 0, gbStandAlone ? IDD_MCP_START_STANDALONE : IDD_MCP_START, true,
//			MCPStartDlgFunc, MCPStartTickFunc, NULL);
		SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_ACCOUNTINFO, 0, IDD_ACCOUNTINFO, true,
			MCPAccountInfoDlgFunc, MCPAccountInfoTickFunc, NULL);
	}

#if !STANDALONE
	DestroySplash();
#endif

	SimpleWindowManager_Run(MCP_OncePerFrame, NULL);


#if !STANDALONE
	KillAllArtistTools();
#endif

	EXCEPTION_HANDLER_END





	return (int) 1;
}

AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_MASTERCONTROLPROGRAM);
}

AUTO_RUN_LATE;
void ugcNamelistStartup(void)
{
	NameList *pUGCProjectVersionStateNameList = CreateNameList_StaticDefine((StaticDefine*)UGCProjectVersionStateEnum);
	NameList_AssignName(pUGCProjectVersionStateNameList, "UGCProjectVersionState");
}

//the little auto_monitor macro language that assembles UI button strings needs to be able to get at
//the MCP's container ID
AUTO_COMMAND;
U32 GetMCPID(void)
{
#if STANDALONE
	return 0;
#else
	return gServerLibState.containerID;
#endif
}

//create a text console window
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void console(int iDummy)
{
	newConsoleWindow();
	showConsoleWindow();
}




char *GetGlobalCommandLine(void)
{
	static char commandLine[COMMAND_LINE_SIZE];
	char *pFirstNewLine;
	strcpy(commandLine, gGlobalDynamicSettings.globalCommandLine);

	pFirstNewLine = strchr(commandLine, '\n');

	if (pFirstNewLine)
	{
		*pFirstNewLine = 0;
	}
	pFirstNewLine = strchr(commandLine, 0xd);

	if (pFirstNewLine)
	{
		*pFirstNewLine = 0;
	}

	return commandLine;
}

char *GetDynamicCommandLine(int iContainerType)
{
	static char commandLine[COMMAND_LINE_SIZE];
	char *pFirstNewLine;
	strcpy(commandLine, GetContainerDynamicInfoFromType(iContainerType)->commandLine);

	pFirstNewLine = strchr(commandLine, '\n');

	if (pFirstNewLine)
	{
		*pFirstNewLine = 0;
	}
	pFirstNewLine = strchr(commandLine, 0xd);

	if (pFirstNewLine)
	{
		*pFirstNewLine = 0;
	}

	return commandLine;
}


char *GetNthExtraGameServerNameFromDynamicSettings(int i)
{
	switch (i)
	{
	case 0:
		return gGlobalDynamicSettings.extraMapName_0;
	case 1:
		return gGlobalDynamicSettings.extraMapName_1;
	case 2:
		return gGlobalDynamicSettings.extraMapName_2;
	case 3:
		return gGlobalDynamicSettings.extraMapName_3;
	}

	return NULL;
}
void SetNthExtraGameServerNameFromDynamicSettings(int i, char *pName)
{
	switch (i)
	{
	xcase 0:
		strcpy( gGlobalDynamicSettings.extraMapName_0, pName);
	xcase 1:
		strcpy( gGlobalDynamicSettings.extraMapName_1, pName);
	xcase 2:
		strcpy( gGlobalDynamicSettings.extraMapName_2, pName);
	xcase 3:
		strcpy( gGlobalDynamicSettings.extraMapName_3, pName);
	}

}



static char sStartingScriptFileName[MAX_PATH] = "";
AUTO_CMD_STRING(sStartingScriptFileName, StartingControllerScript);

char *GetStartingScriptFileName(void)
{
	if (sStartingScriptFileName[0] == 0)
	{
		return NULL;
	}
	else if (fileIsAbsolutePath(sStartingScriptFileName))
	{
		return sStartingScriptFileName;
	}
	else
	{
		static char sTempBuffer[MAX_PATH];

		sprintf(sTempBuffer, "server/controllerscripts/%s.txt", sStartingScriptFileName);

		return sTempBuffer;
	}
}
	

#if !STANDALONE
#include "AutoGen/ArtistTools_h_ast.c"
#endif


//fake prototypes because structparser gets confused by having an .exe in the resources
#if STANDALONE
void doAutoRuns_PatchClient_0(void)
{
}
void doAutoRuns_PatchClient_1(void)
{
}
void doAutoRuns_PatchClient_2(void)
{
}
void doAutoRuns_PatchClient_3(void)
{
}
void doAutoRuns_PatchClient_4(void)
{
}
void doAutoRuns_PatchClient_5(void)
{
}
void doAutoRuns_PatchClient_6(void)
{
}
void doAutoRuns_PatchClient_7(void)
{
}

void _PatchClient_AutoRun_SPECIALINTERNAL(void)
{
}

#endif

#include "accountnet.h"
//on the mcp, if no account server was specifically set, and one is running in a local shard, use it
char *DEFAULT_LATELINK_getAccountServer(void);
char *OVERRIDE_LATELINK_getAccountServer(void)
{
	if (accountServerWasSet())
	{
		return DEFAULT_LATELINK_getAccountServer();
	}

	if (stricmp_safe(GetContainerStaticInfoFromType(GLOBALTYPE_ACCOUNTSERVER)->stateString, "ready") == 0)
	{
		return "localhost";
	}

	return DEFAULT_LATELINK_getAccountServer();
}


#include "AutoGen/MasterControlProgram_h_ast.c"

