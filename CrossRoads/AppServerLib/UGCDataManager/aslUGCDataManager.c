#include "aslUGCDataManager.h"

#include "UGCProjectCommon.h"
#include "AppServerLib.h"
#include "objSchema.h"
#include "serverlib.h"
#include "autostartupsupport.h"
#include "resourceManager.h"
#include "objTransactions.h"
#include "file.h"
#include "sock.h"
#include "aslUGCDataManagerWhatsHot.h"
#include "aslUGCDataManagerProject.h"

#include "autogen/Controller_autogen_RemoteFuncs.h"

#include "AutoGen/aslUGCDataManager_c_ast.h"

AUTO_STRUCT;
typedef struct UGCPlayableNameSpaceDataOverview
{
	char *estrProjectLink;								AST(ESTRING NAME(Link) FORMATSTRING(HTML=1))
	U32 iWhenSecondsSince2000;							AST(NAME(When) FORMATSTRING(HTML_SECS_AGO=1))
	UGCPlayableNameSpaceData ugcMissionCompletedData;	AST(EMBEDDED_FLAT)
} UGCPlayableNameSpaceDataOverview;
extern ParseTable parse_UGCPlayableNameSpaceDataOverview[];
#define TYPE_parse_UGCPlayableNameSpaceDataOverview UGCPlayableNameSpaceDataOverview

AUTO_STRUCT;
typedef struct UGCMissionCompletedData
{
	char *estrProjectLink;								AST(ESTRING NAME(Link) FORMATSTRING(HTML=1))
	U32 iWhenSecondsSince2000;							AST(NAME(When) FORMATSTRING(HTML_SECS_AGO=1))
	char *strNameSpace;									AST(NAME(NameSpace))
	U32 uDurationInMinutes;								AST(NAME(DurationInMinutes))
} UGCMissionCompletedData;
extern ParseTable parse_UGCMissionCompletedData[];
#define TYPE_parse_UGCMissionCompletedData UGCMissionCompletedData

AUTO_STRUCT;
typedef struct UGCDataManagerOverview 
{
	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1))

	AST_COMMAND("Make unplayable all UGC projects that need to be unplayable", "MakeUnplayableAllProjectsThatNeedToBeUnplayable $STRING(you MUST supply a comment)")
	AST_COMMAND("Republish all UGC projects that need republishing", "RepublishAllMapsThatNeedRepublishing $INT(Regenerate 0/1) $INT(Beaconize 0/1) $STRING(You MUST supply a comment)")
	AST_COMMAND("Republish all UGC projects that need first publish", "RepublishAllMapsThatNeedFirstPublish $INT(Regenerate 0/1) $INT(Beaconize 0/1) $STRING(You MUST supply a comment)")
	AST_COMMAND("Resend playable namespaces to MapManager(s)", "ResendAllPlayableNameSpacesToAllMapManagers")

	char *estrUGCPublishDisabled;											AST(ESTRING, FORMATSTRING(HTML_CLASS="Alerts", HTML_NO_HEADER = 1))
	UGCPlayableNameSpaceDataOverview **eaRecentlyChangedPlayableNamespaces;	AST(NAME(RecentlyChangedPlayableNamespaces))
	UGCMissionCompletedData **eaMissionsRecentlyCompleted;					AST(NAME(MissionsRecentlyCompleted))
} UGCDataManagerOverview;

static UGCDataManagerOverview s_UGCDataManagerOverview = {0};

typedef enum
{
	UDML_STATE_INIT,
	UDML_STATE_WHATS_HOT_WAITING,
	UDML_STATE_NORMAL,
} enumUGCDataManagerLibState;

enumUGCDataManagerLibState eUDMLState = UDML_STATE_INIT;

//runs once right as normal operations begin
void aslUGCDataManagerLibStartedNormalOperation(void)
{
	TimedCallback_Add(aslUGCDataManagerWhatsHot_PeriodicUpdate, NULL, 600.0f);

	aslUGCDataManagerProject_StartNormalOperation();
}

static void aslUGCDataManagerLibNormalOperation(F32 fElapsed)
{
	aslUGCDataManager_SendAllPlayableNameSpacesToAllMapManagersAtStartup();

	aslUGCDataManagerLibProjectNormalOperation();
}

static bool s_bAcquiredProjectContainers = false;

static void AcquiredProjectContainers(void)
{
	s_bAcquiredProjectContainers = true;
}

static int aslUGCDataManagerLibOncePerFrame(F32 fElapsed)
{
	switch (eUDMLState)
	{
	case UDML_STATE_INIT:
		aslAcquireContainerOwnership(GLOBALTYPE_UGCPROJECT, AcquiredProjectContainers);
		aslAcquireContainerOwnership(GLOBALTYPE_UGCPROJECTSERIES, NULL);
		aslAcquireContainerOwnership(GLOBALTYPE_UGCACCOUNT, NULL);

		objRegisterContainerTypeCommitCallback(GLOBALTYPE_UGCPROJECT, UgcProjectPreCommitCB, "*", /*bRunOnce=*/true, /*bRunOnceWithAllPathOps=*/false, /*bPreCommit=*/true, /*filterCallback=*/NULL);
		objRegisterContainerTypeCommitCallback(GLOBALTYPE_UGCPROJECT, UgcProjectPostCommitCB, "*", /*bRunOnce=*/true, /*bRunOnceWithAllPathOps=*/false, /*bPreCommit=*/false, /*filterCallback=*/NULL);

		objRegisterContainerTypeCommitCallback(GLOBALTYPE_UGCPROJECTSERIES, UgcProjectSeriesPreCommitCB, "*", /*bRunOnce=*/true, /*bRunOnceWithAllPathOps=*/false, /*bPreCommit=*/true, /*filterCallback=*/NULL);
		objRegisterContainerTypeCommitCallback(GLOBALTYPE_UGCPROJECTSERIES, UgcProjectSeriesPostCommitCB, "*", /*bRunOnce=*/true, /*bRunOnceWithAllPathOps=*/false, /*bPreCommit=*/false, /*filterCallback=*/NULL);

		objRegisterContainerTypeAddCallback(GLOBALTYPE_UGCPROJECT, UgcProjectAddCB);
		objRegisterContainerTypeRemoveCallback(GLOBALTYPE_UGCPROJECT, UgcProjectRemoveCB);

		objRegisterContainerTypeAddCallback(GLOBALTYPE_UGCPROJECTSERIES, UgcProjectSeriesAddCB);
		objRegisterContainerTypeRemoveCallback(GLOBALTYPE_UGCPROJECTSERIES, UgcProjectSeriesRemoveCB);

		eUDMLState = UDML_STATE_WHATS_HOT_WAITING;

		aslUGCDataManagerWhatsHot_Init();
		break;
	case UDML_STATE_WHATS_HOT_WAITING:
		if(aslUGCDataManagerWhatsHot_InitComplete() && s_bAcquiredProjectContainers)
		{
			eUDMLState = UDML_STATE_NORMAL;
			aslUGCDataManagerLibStartedNormalOperation();
			RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
		}
		break;
	case UDML_STATE_NORMAL:
		aslUGCDataManagerLibNormalOperation(fElapsed);
		break;
	}

	return 1;
}

AUTO_STARTUP(UGCDataManager) ASTRT_DEPS(UGCReporting, UGCAchievements, AS_TextFilter);
void aslUGCDataManagerStartup(void)
{
}

static int aslUGCDataManagerLibInit(void)
{
	objLoadAllGenericSchemas();

	AutoStartup_SetTaskIsOn("UGCDataManager", 1);

	if (isDevelopmentMode())
		fileLoadAllUserNamespaces(1);

	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_UGCDATAMANAGER, "UGC data manager type not set");

	loadstart_printf("Connecting UGCDataManager to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

	while (!InitObjectTransactionManager(
		GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}
	if (!objLocalManager())
	{
		loadend_printf("failed.");
		return 0;
	}

	loadend_printf("connected.");

	gAppServer->oncePerFrame = aslUGCDataManagerLibOncePerFrame;

	return 1;
}

AUTO_RUN;
int UGCDataManagerRegister(void)
{
	aslRegisterApp(GLOBALTYPE_UGCDATAMANAGER, aslUGCDataManagerLibInit, 0);
	return 1;
}

void aslUGCDataManager_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	if(aslUGCDataManager_IsPublishDisabled())
		estrCopy2(&s_UGCDataManagerOverview.estrUGCPublishDisabled, "UGC publishing is disabled");
	else
		estrClear(&s_UGCDataManagerOverview.estrUGCPublishDisabled);

	estrPrintf(&s_UGCDataManagerOverview.pGenericInfo, "<a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID, GlobalTypeToName(GetAppGlobalType()));

	*ppTPI = parse_UGCDataManagerOverview;
	*ppStruct = &s_UGCDataManagerOverview;
}

void aslUGCDataManager_ServerMonitor_RecordRecentlyChangedPlayableNamespace(UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData)
{
	UGCPlayableNameSpaceDataOverview *pUGCPlayableNameSpaceDataOverview = StructCreate(parse_UGCPlayableNameSpaceDataOverview);
	StructCopy(parse_UGCPlayableNameSpaceData, pUGCPlayableNameSpaceData, &pUGCPlayableNameSpaceDataOverview->ugcMissionCompletedData, 0, 0, 0);

	estrPrintf(&pUGCPlayableNameSpaceDataOverview->estrProjectLink, "<a href=\"/viewxpath?xpath=UGCDataManager[%u].globObj.UgcProject[%u]\">UGCProject</a>",
		GetAppGlobalID(), UGCProject_GetContainerIDFromUGCNamespace(pUGCPlayableNameSpaceData->strNameSpace));

	pUGCPlayableNameSpaceDataOverview->iWhenSecondsSince2000 = timeSecondsSince2000();

	if(eaSize(&s_UGCDataManagerOverview.eaRecentlyChangedPlayableNamespaces) > 200)
		eaRemove(&s_UGCDataManagerOverview.eaRecentlyChangedPlayableNamespaces, 199);

	eaInsert(&s_UGCDataManagerOverview.eaRecentlyChangedPlayableNamespaces, pUGCPlayableNameSpaceDataOverview, 0);
}

void aslUGCDataManager_ServerMonitor_RecordMissionRecentlyCompleted(const char *strNameSpace, U32 uDurationInMinutes)
{
	UGCMissionCompletedData *pUGCMissionCompletedData = StructCreate(parse_UGCMissionCompletedData);
	pUGCMissionCompletedData->strNameSpace = StructAllocString(strNameSpace);
	pUGCMissionCompletedData->uDurationInMinutes = uDurationInMinutes;

	estrPrintf(&pUGCMissionCompletedData->estrProjectLink, "<a href=\"/viewxpath?xpath=UGCDataManager[%u].globObj.UgcProject[%u]\">UGCProject</a>",
		GetAppGlobalID(), UGCProject_GetContainerIDFromUGCNamespace(strNameSpace));

	pUGCMissionCompletedData->iWhenSecondsSince2000 = timeSecondsSince2000();

	if(eaSize(&s_UGCDataManagerOverview.eaMissionsRecentlyCompleted) > 200)
		eaRemove(&s_UGCDataManagerOverview.eaMissionsRecentlyCompleted, 199);

	eaInsert(&s_UGCDataManagerOverview.eaMissionsRecentlyCompleted, pUGCMissionCompletedData, 0);
}

#include "AutoGen/aslUGCDataManager_c_ast.c"
