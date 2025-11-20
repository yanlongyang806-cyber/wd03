#include "UGCProjectCommon.h"
#include "UGCCommon.h"
#include "earray.h"
#include "objContainer.h"
#include "UGCProjectCommon_h_Ast.h"
#include "aslMapManager.h"
#include "logging.h"
#include "autogen/AppServerLib_autogen_slowfuncs.h"
#include "aslMapManager_h_ast.h"
#include "aslUGCDataManagerProject_c_ast.h"
#include "aslUGCDataManagerProject.h"
#include "autogen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "alerts.h"
#include "subStringSearchTree.h"
#include "Autogen/controller_autogen_remotefuncs.h"
#include "Autogen/appserverlib_autogen_remotefuncs.h"
#include "Autogen/chatserver_autogen_remotefuncs.h"
#include "Autogen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "sock.h"
#include "aslJobManagerPub.h"
#include "TimedCallback.h"
#include "StringCache.h"
#include "ResourceInfo.h"
#include "aslPatching.h"
#include "expressionPrivate.h"
#include "JobMAnagerSupport.h"
#include "RemoteCommandGroup.h"
#include "aslJobManagerPub.h"
#include "aslJobManagerPub_h_ast.h"
#include "httpXpathSupport.h"
#include "WorldGrid.h"
#include "ContinuousBuilderSupport.h"
#include "UGCProjectUtils.h"
#include "UGCAchievements.h"
#include "rand.h"
#include "stringCache.h"
#include "aslUGCDataManagerWhatsHot.h"
#include "serverlib.h"
#include "stringUtil.h"
#include "gameStringFormat.h"
#include "aslMapManagerPub.h"
#include "aslMapManagerNewMapTransfer_GetAddress.h"
#include "AutoTransDefs.h"
#include "aslMapManagerVirtualShard.h"
#include "controllerScriptingSupport.h"
#include "LoggedTransactions.h"
#include "aslMapManagerNewMapTransfer.h"
#include "aslMapManagerNewMapTransfer_Private.h"
#include "aslMapManagerNewMapTransfer_InnerStructs.h"
#include "utilitiesLib.h"
#include "ShardCluster.h"
#include "LoggedTransactions.h"

#include "aslUGCDataManager.h"

#define REPUBLISH_PROCESS_BEGUN "Republish process begun" 

typedef struct ContainerIdReturnData
{
	char *pcShardName;
	U32 loginServerID;
	U64 loginCookie;

	ContainerID id;
} ContainerIdReturnData;

AUTO_STRUCT;
typedef struct UGCProjectCreateData
{
	UGCProjectInfo* pInfo;

	ContainerID iUGCProjectID;
	ContainerID iCopyProjectID;
	ContainerID iVirtualShardID;

	U32 iPossibleUGCProjectFlags;

	UGCProjectRequestData *pUGCProjectRequestData;
} UGCProjectCreateData;

AUTO_STRUCT;
typedef struct UGCProjectVersionRef
{
	ContainerID iID;
	char *pNameSpaceName;
} UGCProjectVersionRef;

AUTO_STRUCT;
typedef struct FilteredListStateChangeTracker
{
	UGCProjectVersionState eStateToChangeTo;
	bool bAlsoSetCRC;
	int iNumToStateChange;
	int iNumStateChangeBegins;
	int iNumStateChangeSucceeds;
	int iNumStateChangeFails;
} FilteredListStateChangeTracker;

AUTO_STRUCT;
typedef struct FilteredListRepublishListTracker
{
	int iNumToRepublish;
	int iNumRepublishBegins;
	int iNumRepublishesToJobManager;
	int iNumRepublishSucceeds;
	int iNumRepublishFails;
} FilteredListRepublishListTracker;

AUTO_STRUCT;
typedef struct UGCProjectFilteredList
{
	char ID[10]; AST(KEY)
	char *pSummaryString; AST(ESTRING)
	U32 iRepublishFlags;
	FilteredListStateChangeTracker stateChangeTracker;
	FilteredListRepublishListTracker republishTracker;

	UGCProjectVersionRef **ppVersions; AST(FORMATSTRING(collapsed = 1))
	char *pErrors; AST(ESTRING)

	char *pComment; AST(ESTRING)
	AST_COMMAND("Delete this list", "DeleteFilteredList $FIELD(ID)", "\q$SERVERTYPE\q = \qUGCDataManager\q")
	AST_COMMAND("Republish these projects", "RepublishFilteredList $FIELD(ID) $INT(Regenerate) $INT(Beaconize) $STRING(Comment)", "\q$SERVERTYPE\q = \qUGCDataManager\q")
	AST_COMMAND("Export to text file", "ExportFilteredList $FIELD(ID) $STRING(File name)")
	AST_COMMAND("Divide into sublists, newest and other", "DivideIntoNewestAndOther $FIELD(ID)", "\q$SERVERTYPE\q = \qUGCDataManager\q")
	AST_COMMAND("Set state", "SetStateForFilteredList $FIELD(ID) $SELECT(What state|NAMELIST_UGCProjectVersionState) $INT(Also update CRC?) $STRING(Comment)", "\q$SERVERTYPE\q = \qUGCDataManager\q")

} UGCProjectFilteredList;

UGCProjectFilteredList *CreateAndRegisterEmptyFilteredList(void);


bool gbUseNewMapTransferCodeForUGCEditServers = true;
AUTO_CMD_INT(gbUseNewMapTransferCodeForUGCEditServers, UseNewMapTransferCodeForUGCEditServers);

//how many seconds after a project is "deleted" before we actually destroy the container
static int sUGCProjectZombieTime = 30 * 24 * 3600;
AUTO_CMD_INT(sUGCProjectZombieTime, UGCProjectZombieTime);

static U32 giRepubFlagsForAutoRepub = 0;
AUTO_CMD_INT(giRepubFlagsForAutoRepub, RepubFlagsForAutoRepub);

//after a version has been publishing for > 10 minutes, start making sure that it's still on the job manager
#define TIME_BEFORE_VERIFYING_ONGOING_PUBLISH 600

static void aslDailyUGCProjectUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);
enumTransactionOutcome trUGCProjectSeries_Update( ATR_ARGS, const char *pOwnerAccountName, NOCONST(UGCProjectSeries)* ugcSeries, NON_CONTAINER UGCProjectSeriesVersion* newSeriesVersion, CONST_EARRAY_OF(NOCONST(UGCProject)) ugcProjectsInSeries, CONST_EARRAY_OF(NOCONST(UGCProject)) oldUgcProjectsInSeries );
static void aslUGCDataManager_ProjectSeriesGetExistingProjectIDs( ContainerID** peaiIDs, CONST_EARRAY_OF(UGCProjectSeriesNode) eaSeriesNodes );


bool gbAutoRepubOnVersionChange = true;
AUTO_CMD_INT(gbAutoRepubOnVersionChange, AutoRepubOnVersionChange);

#define SEND_TO_OTHER_SHARD_FAILURE_TIME (60 * 5)

#define TIME_OF_DAY_FOR_UGCPROJ_DAILY_UPDATE 200

static U32 sIPForShardToSendUGCProjectsTo = 0;

static ContainerID *spNewlyAddedProjectIDForRepubTestings = NULL;

U32 iDelayBeforeDeletingUnplayableUGCProject = 7 * 24 * 3600;
AUTO_CMD_INT(iDelayBeforeDeletingUnplayableUGCProject, DelayBeforeDeletingUnplayableUGCProject);

U32 iDelayBeforeMakingWithdrawnUGCProjectUnplayable = 30 * 24 * 3600;
AUTO_CMD_INT(iDelayBeforeMakingWithdrawnUGCProjectUnplayable, DelayBeforeMakingWithdrawnUGCProjectUnplayable);

void StartWithdraw(ContainerID iID, const char *pNameSpaceName, const char *pListID /*POOL_STRING*/, char *pComment);
void StartStateChange(ContainerID iID, const char *pNameSpaceName, const char *pListID /*POOL_STRING*/, UGCProjectVersionState eNewState, char *pComment, bool bAlsoSetCRC);

char *RepublishFilteredList(char *pID, U32 iRegenerate, U32 iBeaconize, ACMD_SENTENCE pComment);

bool CheckEditQueueCookieValidity(U32 iCookie);

//pause this long after periodic updating a UGC project before doing it again
static int sMinPeriodicUpdateDelay = 60 * 60;
AUTO_CMD_INT(sMinPeriodicUpdateDelay, MinPeriodicUpdateDelay);

bool gbDbgForceAutoRepub = false;
AUTO_CMD_INT(gbDbgForceAutoRepub, DbgForceAutoRepub);

static UGCProjectFilteredList *spListChangedToNeedsRepublishing = NULL;
static UGCProjectFilteredList *spListChangedToNeedsUnplayable = NULL;

void DbgRepublishNeedsRepublishListCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	char *pResult = RepublishFilteredList(spListChangedToNeedsRepublishing->ID, /*iRegenerate=*/0, /*iBeaconize=*/0, "Republishing due to DbgForceAutoRepub");

	if (stricmp(pResult, REPUBLISH_PROCESS_BEGUN) != 0)
	{
		ControllerScript_Failedf("Got result %s back from RepublishFilteredList", pResult);
	}
}

void AddVersionToChangedToNeedsRepublishingList(ContainerID iProjectID, const char *pNameSpace, const char *pWhy)
{
	UGCProjectVersionRef *pRef;
	static bool sbFirst = true;

	if (sbFirst)
	{
		sbFirst = false;
		CRITICAL_NETOPS_ALERT("UGC_REPUBS_HAPPENING", "One or more projects are being marked as NEEDS_REPUBLISHING, a summary alert will be generated shortly.  For safety, the UGC Editing Shard is being taken down.  Bring it back up, once these projects have been handled.");
		RemoteCommand_aslMapManager_SetVirtualShardEnabled(GLOBALTYPE_MAPMANAGER, 0, "UGCShard", false);
	}
	TriggerAutoGroupingAlert("UGC_AUTO_REPUB", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 120, "Setting UGC proj %u to NEEDS_REPUBLISHING because %s (all these will be put into a filtered list)", 
							 iProjectID, pWhy);

	if (!spListChangedToNeedsRepublishing)
	{
		spListChangedToNeedsRepublishing = CreateAndRegisterEmptyFilteredList();
		estrPrintf(&spListChangedToNeedsRepublishing->pComment, "List created at server startup of versions set to NEEDS_REPUBLISHING");
	}

	pRef = StructCreate(parse_UGCProjectVersionRef);
	pRef->iID = iProjectID;
	pRef->pNameSpaceName = strdup(pNameSpace);

	eaPush(&spListChangedToNeedsRepublishing->ppVersions, pRef);

	AutoTrans_trSetVersionNeedsRepublishing(LoggedTransactions_CreateManagedReturnVal( __FUNCTION__, NULL, NULL ), GetAppGlobalType(), GLOBALTYPE_UGCPROJECT,
											iProjectID, pNameSpace, pWhy);

	if ( gbDbgForceAutoRepub )
	{
		ONCE(TimedCallback_Run(DbgRepublishNeedsRepublishListCB, NULL, 15.0f));
	}
}
	
void AddVersionToChangedToNeedsUnplayableList(ContainerID iProjectID, const char *pNameSpace, const char *pWhy)
{
	UGCProjectVersionRef *pRef;
	static bool sbFirst = true;

	if (sbFirst)
	{
		sbFirst = false;
		CRITICAL_NETOPS_ALERT("UGC_MAKE_UNPLAYABLES_HAPPENING", "One or more projects are being marked as NEEDS_UNPLAYABLE, a summary alert will be generated shortly.  For safety, the UGC Editing Shard is being taken down.  Bring it back up, once these projects have been handled.");
		RemoteCommand_aslMapManager_SetVirtualShardEnabled(GLOBALTYPE_MAPMANAGER, 0, "UGCShard", false);
	}
	TriggerAutoGroupingAlert("UGC_AUTO_MAKE_UNPLAYABLE", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 120, "Setting UGC proj %u to NEEDS_UNPLAYABLE because %s (all these will be put into a filtered list)", 
							 iProjectID, pWhy);

	if (!spListChangedToNeedsUnplayable)
	{
		spListChangedToNeedsUnplayable = CreateAndRegisterEmptyFilteredList();
		estrPrintf(&spListChangedToNeedsUnplayable->pComment, "List created at server startup of versions set to NEEDS_UNPLAYABLE");
	}

	pRef = StructCreate(parse_UGCProjectVersionRef);
	pRef->iID = iProjectID;
	pRef->pNameSpaceName = strdup(pNameSpace);

	eaPush(&spListChangedToNeedsUnplayable->ppVersions, pRef);

	AutoTrans_trSetVersionNeedsUnplayable(LoggedTransactions_CreateManagedReturnVal( __FUNCTION__, NULL, NULL ), GetAppGlobalType(), GLOBALTYPE_UGCPROJECT,
										  iProjectID, pNameSpace, pWhy);
}

AUTO_STRUCT;
typedef struct ProjectsForSingleOwner
{
	U32 iOwnerAccount;
	ContainerID *pProjectIDs;
	ContainerID *pProjectSeriesIDs;
} ProjectsForSingleOwner;

AUTO_STRUCT;
typedef struct UGCProjectSeriesCreateData
{
	char *pcShardName;
	U32 loginServerID;
	U64 loginCookie;
	UGCProjectSeries* ugcSeries;
} UGCProjectSeriesCreateData;
extern ParseTable parse_UGCProjectSeriesCreateData[];
#define TYPE_parse_UGCProjectSeriesCreateData UGCProjectSeriesCreateData

StashTable sProjectsForSingleOwnerByAccountID = NULL;

//during some testing, there will be ugc projects with account ID 0
ProjectsForSingleOwner sProjectsWithAccountID0 = {0};


bool TimeStampIndicatesRepubNeeded(const UGCTimeStamp *pTimeStamp, char **ppWhy)
{
	static NOCONST(UGCTimeStamp) *pSysTimeStamp = NULL;

	if (gbDbgForceAutoRepub)
	{
		if (ppWhy)
		{
			estrPrintf(ppWhy, "DbgForceAutoRepub");
		}
		return true;
	}

	if (!pSysTimeStamp)
	{
		pSysTimeStamp = StructCreateNoConst(parse_UGCTimeStamp);
		UGCProject_FillInTimestamp(pSysTimeStamp);
	}

	if (pSysTimeStamp->iMajorVer != pTimeStamp->iMajorVer)
	{
		if (ppWhy)
		{
			estrPrintf(ppWhy, "App has major version %d, doesn't match proj's major version %d",
				pSysTimeStamp->iMajorVer, pTimeStamp->iMajorVer);
		}
		return true;
	}

	if (pSysTimeStamp->iWorldCellOverrideCRC != pTimeStamp->iWorldCellOverrideCRC)
	{
		if (ppWhy)
		{
			estrPrintf(ppWhy, "App has CRC %x, doesn't match proj's CRC %x",
				pSysTimeStamp->iWorldCellOverrideCRC, pTimeStamp->iWorldCellOverrideCRC);
		}
		return true;
	}

	if (pSysTimeStamp->iBeaconProcessVersion != pTimeStamp->iBeaconProcessVersion)
	{
		if (ppWhy)
		{
			estrPrintf(ppWhy, "BeaconProcessVersion: app(%d) != proj(%d)", 
						pSysTimeStamp->iBeaconProcessVersion,
						pTimeStamp->iBeaconProcessVersion);
		}
		return true;
	}

	return false;
}

void AddProjectForOwner(UGCProject *pProj)
{
	ProjectsForSingleOwner *pList;

	ASSERT_CONTAINER_DATA_UGC("Trying to access sProjectsForSingleOwnerByAccountID when not the UGCDataManager!");

	//happens in some test setups, like CB
	if (!pProj->iOwnerAccountID)
	{
		pList = &sProjectsWithAccountID0;
	}
	else
	{
		if (!sProjectsForSingleOwnerByAccountID)
		{
			sProjectsForSingleOwnerByAccountID = stashTableCreateInt(1024);
		}

		if (!stashIntFindPointer(sProjectsForSingleOwnerByAccountID, pProj->iOwnerAccountID, &pList))
		{
			pList = StructCreate(parse_ProjectsForSingleOwner);
			pList->iOwnerAccount = pProj->iOwnerAccountID;
			stashIntAddPointer(sProjectsForSingleOwnerByAccountID, pList->iOwnerAccount, pList, false);
		}
	}

	ea32Push(&pList->pProjectIDs, pProj->id);
}

void RemoveProjectForOwner(UGCProject *pProj)
{
	ProjectsForSingleOwner *pList;

	ASSERT_CONTAINER_DATA_UGC("Trying to access sProjectsForSingleOwnerByAccountID when not the UGCDataManager!");

	//happens in some test setups, like CB
	if (!pProj->iOwnerAccountID)
	{
		pList = &sProjectsWithAccountID0;
	}
	else
	{
		if (!sProjectsForSingleOwnerByAccountID)
		{
			return;
		}

		if (!stashIntFindPointer(sProjectsForSingleOwnerByAccountID, pProj->iOwnerAccountID, &pList))
		{
			return;
		}
	}

	ea32FindAndRemoveFast(&pList->pProjectIDs, pProj->id);
}

void AddProjectSeriesForOwner(UGCProjectSeries *pSeries)
{
	ProjectsForSingleOwner *pList;

	ASSERT_CONTAINER_DATA_UGC("Trying to access sProjectsForSingleOwnerByAccountID when not the UGCDataManager!");

	//happens in some test setups, like CB
	if (!pSeries->iOwnerAccountID)
	{
		pList = &sProjectsWithAccountID0;
	}
	else
	{
		if (!sProjectsForSingleOwnerByAccountID)
		{
			sProjectsForSingleOwnerByAccountID = stashTableCreateInt(1024);
		}

		if (!stashIntFindPointer(sProjectsForSingleOwnerByAccountID, pSeries->iOwnerAccountID, &pList))
		{
			pList = StructCreate(parse_ProjectsForSingleOwner);
			pList->iOwnerAccount = pSeries->iOwnerAccountID;
			stashIntAddPointer(sProjectsForSingleOwnerByAccountID, pList->iOwnerAccount, pList, false);
		}
	}

	ea32Push(&pList->pProjectSeriesIDs, pSeries->id);
}

void RemoveProjectSeriesForOwner(UGCProjectSeries *pSeries)
{
	ProjectsForSingleOwner *pList;

	ASSERT_CONTAINER_DATA_UGC("Trying to access sProjectsForSingleOwnerByAccountID when not the UGCDataManager!");

	//happens in some test setups, like CB
	if (!pSeries->iOwnerAccountID)
	{
		pList = &sProjectsWithAccountID0;
	}
	else
	{
		if (!sProjectsForSingleOwnerByAccountID)
		{
			return;
		}

		if (!stashIntFindPointer(sProjectsForSingleOwnerByAccountID, pSeries->iOwnerAccountID, &pList))
		{
			return;
		}
	}

	ea32FindAndRemoveFast(&pList->pProjectSeriesIDs, pSeries->id);
}

bool NoRepublishBecauseHasntBeenTouchedRecently(UGCProject *pProj, UGCProjectVersion *pVersion)
{
	U32 iCutoffTime = timeSecondsSince2000() - gConf.iUGCProjectNoPlayDaysBeforeNoRepublish * 24 * 60 * 60;
	
	if (pProj->iCreationTime < iCutoffTime && pProj->iMostRecentPlayedTime < iCutoffTime && pVersion->sLastPublishTimeStamp.iTimestamp < iCutoffTime)
	{
		return true;
	}

	return false;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcSendMailForProjectChange( ContainerID projectID, UGCChangeReason reason )
{
	const UGCProject* pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, projectID);
	const UGCProjectVersion* pVersion;
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");
	if( !pProject ) {
		return;
	}

	pVersion = UGCProject_GetMostRecentPublishedVersion( pProject );
	if( !pVersion ) {
		pVersion = UGCProject_GetMostRecentVersion( pProject );
	}
	if( !pVersion ) {
		return;
	}

	RemoteCommand_gslUGC_SendMailForChange( GLOBALTYPE_WEBREQUESTSERVER, 0, pProject, pVersion, reason );
}

static void FillInPlayableDataForProject(UGCPlayableNameSpaces *pUGCPlayableNameSpaces, UGCProject *pUGCProject, bool bIncludeButFlagUnplayable)
{
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pUGCProject->ppProjectVersions, UGCProjectVersion, pUGCProjectVersion)
	{
		if(bIncludeButFlagUnplayable || UGCProject_VersionIsPlayable(pUGCProject, pUGCProjectVersion))
		{
			UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData = StructCreate(parse_UGCPlayableNameSpaceData);

			pUGCPlayableNameSpaceData->uAccountID = pUGCProject->iOwnerAccountID;

			pUGCPlayableNameSpaceData->strNameSpace = StructAllocString(pUGCProjectVersion->pNameSpace);
			pUGCPlayableNameSpaceData->bPlayable = UGCProject_VersionIsPlayable(pUGCProject, pUGCProjectVersion);

			pUGCPlayableNameSpaceData->bStatsQualifyForUGCRewards = UGCProject_QualifiesForRewards(pUGCProject);
			pUGCPlayableNameSpaceData->iNumberOfPlays = UGCProject_GetTotalPlayedCount(pUGCProject);
			pUGCPlayableNameSpaceData->fAverageDurationInMinutes = UGCProject_AverageDurationInMinutes(pUGCProject);

			pUGCPlayableNameSpaceData->iFeaturedStartTimestamp = pUGCProject->pFeatured ? pUGCProject->pFeatured->iStartTimestamp : 0;
			pUGCPlayableNameSpaceData->iFeaturedEndTimestamp = pUGCProject->pFeatured ? pUGCProject->pFeatured->iEndTimestamp : 0;

			eaPush(&pUGCPlayableNameSpaces->eaUGCPlayableNameSpaceData, pUGCPlayableNameSpaceData);
		}
	}
	FOR_EACH_END;
}

static StashTable stashShardsAlreadySentPlayableNameSpaces = NULL;
static U32 s_LastTimePlayableNameSpacesSent = 0;

static bool SendPlayableNameSpacesToAllMapManagers(UGCPlayableNameSpaces *pUGCPlayableNameSpaces, bool bReplace)
{
	Cluster_Overview *pOverview = GetShardClusterOverview_EvenIfNotInCluster();

	if(!stashShardsAlreadySentPlayableNameSpaces)
		stashShardsAlreadySentPlayableNameSpaces = stashTableCreateWithStringKeys(8, StashDeepCopyKeys);

	s_LastTimePlayableNameSpacesSent = timeSecondsSince2000();

	if(pOverview) // This code path is taken by UGCDataManagers in a shard cluster, as well as a single shard launched by ShardLauncher
	{
		bool bAllSent = true;

		FOR_EACH_IN_EARRAY(pOverview->ppShards, ClusterShardSummary, pClusterShard)
		{
			if(pClusterShard->eState == CLUSTERSHARDSTATE_CONNECTED || pClusterShard->eState == CLUSTERSHARDSTATE_THATS_ME)
			{
				int iAlreadySent = false;
				if(!bReplace || !stashFindInt(stashShardsAlreadySentPlayableNameSpaces, pClusterShard->pShardName, &iAlreadySent) || !iAlreadySent)
				{
					ClusterServerTypeStatus *pClusterServerTypeStatus = NULL;
					if(pClusterShard->pMostRecentStatus)
						pClusterServerTypeStatus = eaIndexedGetUsingInt(&pClusterShard->pMostRecentStatus->ppServersByType, GLOBALTYPE_MAPMANAGER);

					if(pClusterServerTypeStatus && pClusterServerTypeStatus->ppServers[0] && 0 == stricmp(pClusterServerTypeStatus->ppServers[0]->pStateString, "ready"))
					{
						RemoteCommand_Intershard_aslMapManager_UpdateNameSpacesPlayable(pClusterShard->pShardName, GLOBALTYPE_MAPMANAGER, 0, pUGCPlayableNameSpaces, bReplace);

						stashAddInt(stashShardsAlreadySentPlayableNameSpaces, pClusterShard->pShardName, 1, true);
					}
					else
						bAllSent = false;
				}
			}
		}
		FOR_EACH_END;

		return bAllSent;
	}

	return false;
}

static void SendPlayableNameSpacesToAllMapManagersAndAlertIfNotSent(UGCPlayableNameSpaces *pUGCPlayableNameSpaces, bool bReplace)
{
	if(!SendPlayableNameSpacesToAllMapManagers(pUGCPlayableNameSpaces, bReplace))
	{
		static U32 lastTimeTriggered = 0;

		if(lastTimeTriggered == 0 || timeSecondsSince2000() > lastTimeTriggered + UGC_DATA_MANAGER_NOT_SENDING_PLAYABLE_NAMESPACES_ALERT_PERIOD)
		{
			bool bTriggered = false;

			Cluster_Overview *pOverview = GetShardClusterOverview_EvenIfNotInCluster();
			if(!pOverview) // This code path is taken by UGCDataManagers in a shard cluster, as well as a single shard launched by ShardLauncher
			{
				lastTimeTriggered = timeSecondsSince2000();
				TriggerAutoGroupingAlert("UGC_DATA_MANAGER_NO_CLUSTER_OVERVIEW", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 12*60*60,
					"UGCDataManager could not send playable namespaces to MapManager(s) because no Cluster Overview was present. This should be available in a cluster as well as in a single shard. UGC projects may not be playable until this is resolved.");
				return;
			}
			else
				lastTimeTriggered = 0;

			FOR_EACH_IN_EARRAY(pOverview->ppShards, ClusterShardSummary, pClusterShard)
			{
				if(pClusterShard->eState == CLUSTERSHARDSTATE_CONNECTED || pClusterShard->eState == CLUSTERSHARDSTATE_THATS_ME)
				{
					ClusterServerTypeStatus *pClusterServerTypeStatus = NULL;
					if(pClusterShard->pMostRecentStatus)
						pClusterServerTypeStatus = eaIndexedGetUsingInt(&pClusterShard->pMostRecentStatus->ppServersByType, GLOBALTYPE_MAPMANAGER);

					if(!pClusterServerTypeStatus || !pClusterServerTypeStatus->ppServers[0] || 0 != stricmp(pClusterServerTypeStatus->ppServers[0]->pStateString, "ready"))
					{
						bTriggered = true;
						lastTimeTriggered = timeSecondsSince2000();
						TriggerAutoGroupingAlert("UGC_DATA_MANAGER_MAP_MANAGER_NOT_READY", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 12*60*60,
							"UGCDataManager could not send playable namespaces to MapManager in shard (%s) because the MapManager is not accessible or in the ready state. Some UGC projects may be unplayable until this is resolved.", pClusterShard->pShardName);
					}
				}
			}
			FOR_EACH_END;

			if(!bTriggered)
				lastTimeTriggered = 0;
		}
	}
}

static UGCPlayableNameSpaces ugcPlayableNameSpacesFromPreCommit;

void UgcProjectPreCommitCB(Container *con, ObjectPathOperation **operations)
{
	UGCProject *pUGCProject = (UGCProject *)con->containerData;

	StructInit(parse_UGCPlayableNameSpaces, &ugcPlayableNameSpacesFromPreCommit);

	FillInPlayableDataForProject(&ugcPlayableNameSpacesFromPreCommit, pUGCProject, /*bIncludeButFlagUnplayable=*/true);

	UgcProjectRemoveCB(con, pUGCProject);
}

void UgcProjectPostCommitCB(Container *con, ObjectPathOperation **operations)
{
	UGCProject *pUGCProject = (UGCProject *)con->containerData;

	UGCPlayableNameSpaces ugcPlayableNameSpacesFromPostCommit;
	UGCPlayableNameSpaces ugcPlayableNameSpacesChanged;

	StructInit(parse_UGCPlayableNameSpaces, &ugcPlayableNameSpacesFromPostCommit);
	FillInPlayableDataForProject(&ugcPlayableNameSpacesFromPostCommit, pUGCProject, /*bIncludeButFlagUnplayable=*/true);

	StructInit(parse_UGCPlayableNameSpaces, &ugcPlayableNameSpacesChanged);

	FOR_EACH_IN_EARRAY_FORWARDS(ugcPlayableNameSpacesFromPostCommit.eaUGCPlayableNameSpaceData, UGCPlayableNameSpaceData, pUGCPlayableNameSpaceDataFromPostCommit)
	{
		UGCPlayableNameSpaceData *pUGCPlayableNameSpaceDataFromPreCommit = NULL;
		FOR_EACH_IN_EARRAY_FORWARDS(ugcPlayableNameSpacesFromPreCommit.eaUGCPlayableNameSpaceData, UGCPlayableNameSpaceData, pUGCPlayableNameSpaceDataFromPreCommitIter)
		{
			if(0 == stricmp(pUGCPlayableNameSpaceDataFromPreCommitIter->strNameSpace, pUGCPlayableNameSpaceDataFromPostCommit->strNameSpace))
			{
				pUGCPlayableNameSpaceDataFromPreCommit = pUGCPlayableNameSpaceDataFromPreCommitIter;
				break;
			}
		}
		FOR_EACH_END;

		// If there was no version of this namespace in the pre commit, OR if the Playable status changed (or anything else we are caching on the MapManager), add it
		if(!pUGCPlayableNameSpaceDataFromPreCommit ||
			pUGCPlayableNameSpaceDataFromPreCommit->bPlayable != pUGCPlayableNameSpaceDataFromPostCommit->bPlayable ||
			pUGCPlayableNameSpaceDataFromPreCommit->bStatsQualifyForUGCRewards != pUGCPlayableNameSpaceDataFromPostCommit->bStatsQualifyForUGCRewards ||
			pUGCPlayableNameSpaceDataFromPreCommit->iNumberOfPlays != pUGCPlayableNameSpaceDataFromPostCommit->iNumberOfPlays ||
			pUGCPlayableNameSpaceDataFromPreCommit->fAverageDurationInMinutes != pUGCPlayableNameSpaceDataFromPostCommit->fAverageDurationInMinutes ||
			pUGCPlayableNameSpaceDataFromPreCommit->iFeaturedEndTimestamp != pUGCPlayableNameSpaceDataFromPostCommit->iFeaturedEndTimestamp ||
			pUGCPlayableNameSpaceDataFromPreCommit->iFeaturedStartTimestamp != pUGCPlayableNameSpaceDataFromPostCommit->iFeaturedStartTimestamp)
		{
			eaPush(&ugcPlayableNameSpacesChanged.eaUGCPlayableNameSpaceData, StructClone(parse_UGCPlayableNameSpaceData, pUGCPlayableNameSpaceDataFromPostCommit));

			aslUGCDataManager_ServerMonitor_RecordRecentlyChangedPlayableNamespace(pUGCPlayableNameSpaceDataFromPostCommit);
		}
	}
	FOR_EACH_END;

	SendPlayableNameSpacesToAllMapManagersAndAlertIfNotSent(&ugcPlayableNameSpacesChanged, /*bReplace=*/false);

	StructReset(parse_UGCPlayableNameSpaces, &ugcPlayableNameSpacesFromPreCommit);
	StructReset(parse_UGCPlayableNameSpaces, &ugcPlayableNameSpacesFromPostCommit);
	StructReset(parse_UGCPlayableNameSpaces, &ugcPlayableNameSpacesChanged);

	UgcProjectAddCB(con, pUGCProject);
}

void UgcProjectAddCB(Container *con, UGCProject *pProj)
{
	UGCProject *pOtherProject = NULL;

	if (pProj->iOwnerAccountID)
	{
		AddProjectForOwner(pProj);
	}

	if (gbAutoRepubOnVersionChange && !pProj->bTestOnly)
	{
		ea32Push(&spNewlyAddedProjectIDForRepubTestings, pProj->id);
	}
}
		
void UgcProjectRemoveCB(Container *con, UGCProject *pProj)
{
	if (pProj->iOwnerAccountID)
	{
		RemoveProjectForOwner(pProj);
	}
}

void UgcProjectSeriesPreCommitCB(Container *con, ObjectPathOperation **operations)
{
	UGCProjectSeries *pUGCProjectSeries = (UGCProjectSeries *)con->containerData;

	UgcProjectSeriesRemoveCB(con, pUGCProjectSeries);
}

void UgcProjectSeriesPostCommitCB(Container *con, ObjectPathOperation **operations)
{
	UGCProjectSeries *pUGCProjectSeries = (UGCProjectSeries *)con->containerData;

	UgcProjectSeriesAddCB(con, pUGCProjectSeries);
}

void UgcProjectSeriesAddCB(Container *con, UGCProjectSeries *pSeries)
{
	AddProjectSeriesForOwner(pSeries);
}

void UgcProjectSeriesRemoveCB(Container *con, UGCProjectSeries *pSeries)
{
	RemoveProjectSeriesForOwner(pSeries);
}

AUTO_COMMAND ACMD_CMDLINE;
void SendUGCProjectsToShard(char *pOtherShardName)
{
	sIPForShardToSendUGCProjectsTo = ipFromString(pOtherShardName);
}

PossibleUGCProject *CreatePossibleUGCProject(UGCProject *pProject, U32 iCopyID)
{
	PossibleUGCProject *pRetVal = StructCreate(parse_PossibleUGCProject);
	const UGCProjectVersion *pVersion = UGCProject_GetMostRecentVersion(pProject);
	UGCProjectStatusQueryInfo* pStatus = NULL;
	UGCSingleReview** eaReviews = NULL;
	UGCProject *pCopyProject = NULL;
	const UGCProjectVersion *pCopyVersion = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if(pVersion)
	{
		pRetVal->bEditVersionIsNew = (ugcProjectGetVersionStateConst(pVersion) == UGC_NEW);
		pRetVal->strEditVersionNamespace = StructAllocString(pVersion->pNameSpace);
	}

	if (ugcProjectGetVersionStateConst(pVersion) == UGC_NEW)
	{
		pRetVal->iPossibleUGCProjectFlags |= POSSIBLEUGCPROJECT_FLAG_NEW_NEVER_SAVED;
	}

	pRetVal->iID = pProject->id;
	pRetVal->iSeriesID = pProject->seriesID;

	if (iCopyID != 0)
	{
		pCopyProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, iCopyID);
		pCopyVersion = UGCProject_GetMostRecentVersion(pCopyProject);

		if(pCopyVersion)
		{
			pRetVal->bCopyEditVersionIsNew = (ugcProjectGetVersionStateConst(pCopyVersion) == UGC_NEW);
			pRetVal->strCopyEditVersionNamespace = StructAllocString(pCopyVersion->pNameSpace);
		}
	}
	addPatchInfoForUGCProject(pRetVal, pCopyProject ? pCopyProject : pProject);

	pRetVal->pProjectInfo = ugcCreateProjectInfo( pProject, pVersion );
	pRetVal->pStatus = UGCProject_GetStatusFromProject(pProject, NULL, NULL);

	// Reviews
	pRetVal->pProjectReviews = StructCreate(parse_UGCProjectReviews);
	StructCopyFields(parse_UGCProjectReviews, &pProject->ugcReviews, pRetVal->pProjectReviews, 0, TOK_EARRAY | TOK_FIXED_ARRAY);
	eaiCopy(&CONTAINER_NOCONST(UGCProjectReviews, pRetVal->pProjectReviews)->piNumRatings, &pProject->ugcReviews.piNumRatings);
	ugcReviews_GetForPage( &pProject->ugcReviews, 0, CONTAINER_NOCONST(UGCProjectReviews, pRetVal->pProjectReviews));
	pRetVal->iProjectReviewsPageNumber = 0;

	return pRetVal;
}

void GetUGCProjectsByUGCAccount(ContainerID uUGCAccountID, ContainerID **peauUGCProjectIDs, ContainerID **peauUGCProjectSeriesIDs)
{
	ASSERT_CONTAINER_DATA_UGC("Trying to access sProjectsForSingleOwnerByAccountID when not the UGCDataManager!");

	if(!peauUGCProjectIDs && !peauUGCProjectSeriesIDs)
		return;

	if(sProjectsForSingleOwnerByAccountID)
	{
		ProjectsForSingleOwner *pList = NULL;

		if(stashIntFindPointer(sProjectsForSingleOwnerByAccountID, uUGCAccountID, &pList))
		{
			if(peauUGCProjectIDs)
				eaiCopy(peauUGCProjectIDs, &pList->pProjectIDs);
			if(peauUGCProjectSeriesIDs)
				eaiCopy(peauUGCProjectSeriesIDs, &pList->pProjectSeriesIDs);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_GetPossibleUGCProjects(UGCProjectSearchInfo *pSearchInfo)
{
	ContainerIterator iter = {0};
	UGCProject *pProject;
	int i;
	int iProjNum;

	ProjectsForSingleOwner *pList = NULL;

	ASSERT_CONTAINER_DATA_UGC("Trying to access sProjectsForSingleOwnerByAccountID when not the UGCDataManager!");
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	pSearchInfo->pPossibleUGCProjects = StructCreate(parse_PossibleUGCProjects);

	if (pSearchInfo->iOwnerAccountID == 0)
	{
		pList = &sProjectsWithAccountID0;
	}
	else
	{
		stashIntFindPointer(sProjectsForSingleOwnerByAccountID, pSearchInfo->iOwnerAccountID, &pList);
	}

	if (pList)
	{
		for (iProjNum = 0; iProjNum < ea32Size(&pList->pProjectIDs); iProjNum++)
		{
			pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, pList->pProjectIDs[iProjNum]);

			if (!pProject)
			{
				continue;
			}

			if (eaSize(&pProject->ppProjectVersions) && !UGCProject_CanAutoDelete(pProject))
			{
				PossibleUGCProject *project = CreatePossibleUGCProject(pProject, 0);

				eaPush(&pSearchInfo->pPossibleUGCProjects->ppProjects, project);

				if(nullStr(pProject->strPreviousShard) && !pProject->pFeatured)
					pSearchInfo->pPossibleUGCProjects->iProjectSlotsUsed++;
			}
		}
	}

	if( pSearchInfo->iMaxProjectSlots == 0 || pSearchInfo->pPossibleUGCProjects->iProjectSlotsUsed < pSearchInfo->iMaxProjectSlots ) {
		PossibleUGCProject *pNew = StructCreate(parse_PossibleUGCProject);
		eaInsert(&pSearchInfo->pPossibleUGCProjects->ppProjects, pNew, 0);
	}

	for (i=0; i < eaSize(&pSearchInfo->pPossibleUGCProjects->ppProjects); i++)
	{
		pSearchInfo->pPossibleUGCProjects->ppProjects[i]->iVirtualShardID = pSearchInfo->iVirtualShardID;

		if (gbUseNewMapTransferCodeForUGCEditServers)
		{
			pSearchInfo->pPossibleUGCProjects->ppProjects[i]->iPossibleUGCProjectFlags |= POSSIBLEUGCPROJECT_FLAG_USENEWMAPTRANSFER;
		}
	}

	if (pList)
	{
		int it;
		for (it = 0; it < ea32Size(&pList->pProjectSeriesIDs); it++)
		{
			UGCProjectSeries* pProjectSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, pList->pProjectSeriesIDs[it]);

			if (!pProjectSeries || UGCProjectSeries_CanAutoDelete(pProjectSeries))
			{
				continue;
			}

			eaPush(&pSearchInfo->pPossibleUGCProjects->eaProjectSeries, UGCProjectSeries_CreateEditingCopy( pProjectSeries ));
		}
	}

	if( pSearchInfo->iMaxSeriesSlots == 0 || eaSize( &pSearchInfo->pPossibleUGCProjects->eaProjectSeries ) < pSearchInfo->iMaxSeriesSlots )
	{
		pSearchInfo->pPossibleUGCProjects->bNewProjectSeries = true;
	}

	RemoteCommand_Intershard_aslLoginUGCProject_GetPossibleUGCProjects_Return(pSearchInfo->pcShardName, GLOBALTYPE_LOGINSERVER, pSearchInfo->loginServerID, pSearchInfo);
}

/*
static bool sbGlobalChatOnline = false;
static S64 timeLastGlobalChatCheck = 0;

void mapMgrGlobalChatQueryReturn(TransactionReturnVal *pReturnVal, void *data)
{
	bool exists;
	if(RemoteCommandCheck_QueryGlobalChatConnected(pReturnVal, &exists)==TRANSACTION_OUTCOME_SUCCESS)
		sbGlobalChatOnline = exists;
}

void mapMgrUGCTick(void)
{
	if (ABS_TIME_SINCE(timeLastGlobalChatCheck)>SEC_TO_ABS_TIME(15))
	{
		timeLastGlobalChatCheck = ABS_TIME;
		RemoteCommand_QueryGlobalChatConnected(objCreateManagedReturnVal(mapMgrGlobalChatQueryReturn, NULL), 
												GLOBALTYPE_CHATSERVER, 0);
	}

	FOR_EACH_IN_EARRAY(g_ProjectRequests, MapMgrUGCProjectRequest, request)
	{
		ChatPlayerList *pFriends = lruCacheGet(g_FriendsCache, (void*)request->searchInfo->iOwnerAccountID);

		if(!pFriends)
			continue;

		mapMgrUGCProjectRequestComplete(request->cmdID, request->searchInfo, pFriends);
		StructDestroy(parse_UGCProjectSearchInfo, request->searchInfo);
		eaRemoveFast(&g_ProjectRequests, FOR_EACH_IDX(0, request));
		free(request);
	}
	FOR_EACH_END;

	lruCacheTick();
}
*/

AUTO_TRANS_HELPER;
void ugc_trh_RebuildRatingBucketsIfNecessary( ATH_ARG NOCONST(UGCProjectReviews)* pReviews )
{
	if( ea32Size( &pReviews->piNumRatings ) == UGCPROJ_NUM_RATING_BUCKETS ) {
		return;
	}

	ea32Clear( &pReviews->piNumRatings );
	ea32SetSize( &pReviews->piNumRatings, UGCPROJ_NUM_RATING_BUCKETS );

	{
		S32 i;
		for( i = eaSize( &pReviews->ppReviews )- 1 ; i >= 0; i-- ) {
			NOCONST(UGCSingleReview)* pReview = pReviews->ppReviews[ i ];
			S32 iBucket = ugcReviews_FindBucketForRating( pReview->fRating );
			if( iBucket >= 0 ) {
				pReviews->piNumRatings[ iBucket ]++;
			}
		}
	}
}


AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".ugcReviews.ppReviews, .ugcReviews.piNumRatings");
enumTransactionOutcome trUGCProjectRebuildRatingBucketsIfNecessary(ATR_ARGS, NOCONST(UGCProject)* pUGCProject)
{
	ugc_trh_RebuildRatingBucketsIfNecessary( &pUGCProject->ugcReviews );
	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(pUGCProjectSeries, ".ugcReviews.ppReviews, .ugcReviews.piNumRatings");
enumTransactionOutcome trUGCProjectSeriesRebuildRatingBucketsIfNecessary(ATR_ARGS, NOCONST(UGCProjectSeries)* pUGCProjectSeries)
{
	ugc_trh_RebuildRatingBucketsIfNecessary( &pUGCProjectSeries->ugcReviews );
	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".pIDString, .id, .ppProjectVersions, .ugcReviews.ppReviews, .ugcReviews.piNumRatings");
enumTransactionOutcome trUGCProjectFillInAfterCreation(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, ContainerID projectID, UGCProjectInfo* pInfo)
{
	char IDString[UGC_IDSTRING_LENGTH_BUFFER_LENGTH];
	NOCONST(UGCProjectVersion)* newVersion = UGCProject_CreateEmptyVersion(ATR_RECURSE, pUGCProject, NULL, NULL);
	eaPush(&pUGCProject->ppProjectVersions, newVersion);
	
	UGCIDString_IntToString(pUGCProject->id, /*isSeries=*/false, IDString);
	pUGCProject->pIDString = strdup(IDString);
	UGCProject_ApplyProjectInfoToVersion(newVersion, pInfo);

	ugc_trh_RebuildRatingBucketsIfNecessary( &pUGCProject->ugcReviews );

	return TRANSACTION_OUTCOME_SUCCESS;
}

void aslUGCDataManager_UGCProjectCreate_FillInAfterCreation_CB(TransactionReturnVal *pReturn, UGCProjectCreateData *pUGCProjectCreateData)
{
	ReturnedPossibleUGCProject retVal = {0};

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		PossibleUGCProject *pPossibleProject = NULL;
		UGCProject *pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, pUGCProjectCreateData->iUGCProjectID);
		if(!pProject)
		{
			char *estrError = NULL;

			estrPrintf(&estrError, "UGC project %u not found after creation", pUGCProjectCreateData->iUGCProjectID);
			retVal.strError = estrError;

			log_printf(LOG_LOGIN, "UGC project %u not found after creation", pUGCProjectCreateData->iUGCProjectID);

			RemoteCommand_Intershard_aslMapManager_UGCProjectCreate_Return(pUGCProjectCreateData->pUGCProjectRequestData->requesterInfo.pcRequestingShardName, GLOBALTYPE_MAPMANAGER, 0,
				&retVal, pUGCProjectCreateData->pUGCProjectRequestData);

			estrDestroy(&estrError);
		}
		else
		{
			pPossibleProject = CreatePossibleUGCProject(pProject, pUGCProjectCreateData->iCopyProjectID);
			pPossibleProject->iPossibleUGCProjectFlags = pUGCProjectCreateData->iPossibleUGCProjectFlags;
			pPossibleProject->iVirtualShardID = pUGCProjectCreateData->iVirtualShardID;
			pPossibleProject->iCopyID = pUGCProjectCreateData->iCopyProjectID;

			retVal.pPossibleUGCProject = pPossibleProject;

			RemoteCommand_Intershard_aslMapManager_UGCProjectCreate_Return(pUGCProjectCreateData->pUGCProjectRequestData->requesterInfo.pcRequestingShardName, GLOBALTYPE_MAPMANAGER, 0,
				&retVal, pUGCProjectCreateData->pUGCProjectRequestData);

			StructDestroy(parse_PossibleUGCProject, pPossibleProject);
		}
	}
	else
	{
		char *estrError = NULL;

		estrPrintf(&estrError, "Failed to fill in new UGC project %u: %s", pUGCProjectCreateData->iUGCProjectID, GetTransactionFailureString(pReturn));
		retVal.strError = estrError;

		log_printf(LOG_LOGIN, "Failed to fill in new UGC project %u: %s", pUGCProjectCreateData->iUGCProjectID, GetTransactionFailureString(pReturn));

		RemoteCommand_Intershard_aslMapManager_UGCProjectCreate_Return(pUGCProjectCreateData->pUGCProjectRequestData->requesterInfo.pcRequestingShardName, GLOBALTYPE_MAPMANAGER, 0,
			&retVal, pUGCProjectCreateData->pUGCProjectRequestData);

		estrDestroy(&estrError);
	}

	StructDestroy(parse_UGCProjectCreateData, pUGCProjectCreateData);
}

void aslUGCDataManager_UGCProjectCreate_ContainerCreate_CB(TransactionReturnVal *pReturn, UGCProjectCreateData *pUGCProjectCreateData)
{
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		pUGCProjectCreateData->iUGCProjectID = atoi(pReturn->pBaseReturnVals[0].returnString);

		AutoTrans_trUGCProjectFillInAfterCreation(objCreateManagedReturnVal(aslUGCDataManager_UGCProjectCreate_FillInAfterCreation_CB, pUGCProjectCreateData), GLOBALTYPE_UGCDATAMANAGER,
			GLOBALTYPE_UGCPROJECT, pUGCProjectCreateData->iUGCProjectID, pUGCProjectCreateData->iUGCProjectID,
			pUGCProjectCreateData->pInfo);
	}
	else
	{
		ReturnedPossibleUGCProject retVal = {0};
		char *estrError = NULL;

		estrPrintf(&estrError, "Failed to create UGC Project: %s", GetTransactionFailureString(pReturn));

		log_printf(LOG_LOGIN, "Failed to create UGC Project: %s", GetTransactionFailureString(pReturn));
		retVal.strError = estrError;

		RemoteCommand_Intershard_aslMapManager_UGCProjectCreate_Return(pUGCProjectCreateData->pUGCProjectRequestData->requesterInfo.pcRequestingShardName, GLOBALTYPE_MAPMANAGER, 0,
			&retVal, pUGCProjectCreateData->pUGCProjectRequestData);

		StructDestroy(parse_UGCProjectCreateData, pUGCProjectCreateData);

		estrDestroy(&estrError);
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void aslUGCDataManager_UGCProjectCreate(PossibleUGCProject *pUGCProjectRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, UGCProjectRequestData *pUGCProjectRequestData)
{
	NOCONST(UGCProject) *pUgcProject = StructCreateNoConst(parse_UGCProject);
	UGCProjectCreateData *pUGCProjectCreateData = StructCreate(parse_UGCProjectCreateData);
	const char *accountNameToUse = NULL;

	pUGCProjectCreateData->pUGCProjectRequestData = StructClone(parse_UGCProjectRequestData, pUGCProjectRequestData);
	pUGCProjectCreateData->pInfo = StructClone( parse_UGCProjectInfo, pUGCProjectRequest->pProjectInfo);
	pUGCProjectCreateData->iPossibleUGCProjectFlags = pUGCProjectRequest->iPossibleUGCProjectFlags;
	pUGCProjectCreateData->iVirtualShardID = pUGCProjectRequest->iVirtualShardID;
	pUGCProjectCreateData->iCopyProjectID = pUGCProjectRequest->iCopyID;

	pUgcProject->iOwnerAccountID = pRequesterInfo->iPlayerAccountID;
	pUgcProject->iCreationTime = timeSecondsSince2000();

	if(nullStr(pRequesterInfo->pPlayerAccountName))
	{
		if (g_isContinuousBuilder)
			accountNameToUse = "CBAccount";
		else
			AssertOrAlertWarning("UGC_NO_ACCOUNT_NAME", "Someone (acct %u) trying to create a ugc project (%s) with no account name", pUgcProject->iOwnerAccountID, SAFE_MEMBER(pUGCProjectRequest->pProjectInfo, pcPublicName));
	}
	else
		accountNameToUse = pRequesterInfo->pPlayerAccountName;
	UGCProject_trh_SetOwnerAccountName(ATR_EMPTY_ARGS, pUgcProject, accountNameToUse);

	pUgcProject->iOwnerLangID = pRequesterInfo->iPlayerLangID;

	//can't add a starting version until the thing is created so we have a container ID
	objRequestContainerCreate(objCreateManagedReturnVal(aslUGCDataManager_UGCProjectCreate_ContainerCreate_CB, pUGCProjectCreateData),
		GLOBALTYPE_UGCPROJECT, pUgcProject, GetAppGlobalType(), GetAppGlobalID());

	StructDestroyNoConst(parse_UGCProject, pUgcProject);
}

typedef struct UGCAccountEnsureExistsData {
	ContainerID containerID;
	UGCAccountContainerCB cb;
	UserData userData;
} UGCAccountEnsureExistsData;

static void UGCAccountEnsureExistsCB(TransactionReturnVal *returnVal, UGCAccountEnsureExistsData* data )
{
	ContainerID containerID = data->containerID;
	UGCAccountContainerCB cb = data->cb;
	UserData userData = data->userData;
	UGCAccount* authorContainer = objGetContainerData(GLOBALTYPE_UGCACCOUNT, data->containerID);

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	SAFE_FREE( data );
	
	// MJF (July/17/2012): We don't care about the result of this
	// "transaction", because if two requests come in rapid
	// succession, the second transaction will fail to create the
	// container.  However, at that point the container exists, so
	// it's all okay!
	if( !authorContainer ) {
		AssertOrAlert( "UGC_ACCOUNT_ENSURE_FAILED", "Tried to create a UGCAccount with id %d, but that failed.  This should never happen.",
					   containerID );
		return;
	}

	if( cb ) {
		cb( authorContainer, userData );
	}
}

void UGCAccountEnsureExists(ContainerID containerID, UGCAccountContainerCB cb, UserData userData)
{
	UGCAccount* account;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if( containerID == 0 ) {
		cb( NULL, userData );
		return;
	}

	account = objGetContainerData(GLOBALTYPE_UGCACCOUNT, containerID);
	if( account ) {
		if( cb ) {
			cb( account, userData );
		}
	} else {
		UGCAccountEnsureExistsData* data = calloc( 1, sizeof( *data ));
		TransactionRequest* request = objCreateTransactionRequest();
		// MJF (July/17/2012): If no fields are filled in,
		// VerifyAndSet doesn't actually create the container.
		NOCONST(UGCAccount) acct = { 0 };
		acct.accountID = containerID;

		data->containerID = containerID;
		data->cb = cb;
		data->userData = userData;

		//objAddToTransactionRequestf( request, GLOBALTYPE_GAMEACCOUNTDATA, containerID, NULL,
		//							"set bUGCAccountCreated = \"1\"\n" );

		objAddToTransactionContainerVerifyOrCreateAndInit( request, GLOBALTYPE_UGCACCOUNT, containerID, &acct, GetAppGlobalType(), GetAppGlobalID() );
		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, objCreateManagedReturnVal( UGCAccountEnsureExistsCB, (UserData)data ),
									  "UGCAccountEnsureExists", request );
		objDestroyTransactionRequest( request );
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".ugcStats.eaDurationStatsByMap")
ATR_LOCKS(pVersion, ".ppMapNames");
static void ugcFixupDurationStatsByMap(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pProject, ATH_ARG NOCONST(UGCProjectVersion) *pVersion)
{
	// remove maps that no longer exist in this version
	FOR_EACH_IN_EARRAY(pProject->ugcStats.eaDurationStatsByMap, NOCONST(UGCProjectMapDurationStats), mapstats)
	{
		bool bStillExists = false;
		FOR_EACH_IN_EARRAY(pVersion->ppMapNames, const char, name)
		{
			if(0 == stricmp(mapstats->pName, name))
			{
				bStillExists = true;
				break;
			}
		}
		FOR_EACH_END;

		if(!bStillExists)
		{
			eaRemove(&pProject->ugcStats.eaDurationStatsByMap, FOR_EACH_IDX(pProject->ugcStats.eaDurationStatsByMap, mapstats));
			StructDestroyNoConst(parse_UGCProjectMapDurationStats, mapstats);
		}
	}
	FOR_EACH_END;

	// add new maps added in this version
	FOR_EACH_IN_EARRAY(pVersion->ppMapNames, const char, name)
	{
		bool bPreviouslyExisted = false;
		FOR_EACH_IN_EARRAY(pProject->ugcStats.eaDurationStatsByMap, NOCONST(UGCProjectMapDurationStats), mapstats)
		{
			if(0 == stricmp(mapstats->pName, name))
			{
				bPreviouslyExisted = true;
				break;
			}
		}
		FOR_EACH_END;

		if(!bPreviouslyExisted)
		{
			NOCONST(UGCProjectMapDurationStats) *mapstats = StructCreateNoConst(parse_UGCProjectMapDurationStats);
			mapstats->pName = StructAllocString(name);
			eaPush(&pProject->ugcStats.eaDurationStatsByMap, mapstats);
		}
	}
	FOR_EACH_END;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ppprojectversions, .Id, .Iowneraccountid, .pPublicName_ForSearching, .pPublishedVersionName, .bUGCFeaturedCopyProjectInProgress, .ugcStats.eaDurationStatsByMap")
ATR_LOCKS(pSeries, ".iLastUpdatedTime");
enumTransactionOutcome trUGCProjectSetPublishResult(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, NOCONST(UGCProjectSeries)* pSeries, char *pNameSpace, int iStatus, char *pComment)
{
	int i;

	for (i=eaSize(&pUGCProject->ppProjectVersions) -1; i >= 0; i--)
	{
		if (stricmp(pUGCProject->ppProjectVersions[i]->pNameSpace, pNameSpace) == 0)
		{
			UGCProjectVersionState ePrevState = ugcProjectGetVersionState( pUGCProject->ppProjectVersions[ i ]);
			if (ePrevState == UGC_PUBLISH_BEGUN || ePrevState == UGC_REPUBLISHING)
			{
				if (iStatus)
				{
					int j;
					ugcProjectSetVersionState(pUGCProject->ppProjectVersions[i], UGC_PUBLISHED, pComment);

					ugcFixupDurationStatsByMap(ATR_PASS_ARGS, pUGCProject, pUGCProject->ppProjectVersions[i]);

					pUGCProject->bUGCFeaturedCopyProjectInProgress = false;
					if( !ISNULL( pSeries )) {
						pSeries->iLastUpdatedTime = timeSecondsSince2000();
					}

					//when setting most recent version to published, set all other published versions to withdrawn
					for (j = i-1; j >= 0; j--)
					{
						UGCProjectVersionState eOtherState = ugcProjectGetVersionState(pUGCProject->ppProjectVersions[j]);
						if (eOtherState == UGC_PUBLISHED)
						{
							ugcProjectSetVersionState(pUGCProject->ppProjectVersions[j], UGC_WITHDRAWN, "Setting newer version to PUBLISHED");
						}
						else if (eOtherState == UGC_PUBLISH_BEGUN || eOtherState == UGC_REPUBLISHING)
						{
							ugcProjectSetVersionState(pUGCProject->ppProjectVersions[j], UGC_UNPLAYABLE, "Setting newer version to PUBLISHED");
						}
					}

					estrClear(&pUGCProject->pPublicName_ForSearching);
					SSSTree_InternalizeString(&pUGCProject->pPublicName_ForSearching, pUGCProject->ppProjectVersions[i]->pName);
					StructCopyString(&pUGCProject->pPublishedVersionName, pUGCProject->ppProjectVersions[i]->pName);
					UGCProject_FillInTimestamp(&pUGCProject->ppProjectVersions[i]->sLastPublishTimeStamp);

					if(ePrevState == UGC_PUBLISH_BEGUN)
					{
						UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
						event->uUGCAuthorID = pUGCProject->iOwnerAccountID;
						event->uUGCProjectID = pUGCProject->id;
						event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
						event->ugcAchievementServerEvent->ugcProjectPublishedEvent = StructCreate(parse_UGCProjectPublishedEvent);
						QueueRemoteCommand_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
						StructDestroy(parse_UGCAchievementEvent, event);
					}
				}
				else
				{
					if (ePrevState == UGC_REPUBLISHING)
					{
						ugcProjectSetVersionState(pUGCProject->ppProjectVersions[i], UGC_REPUBLISH_FAILED, pComment);
					}
					else
					{
						ugcProjectSetVersionState(pUGCProject->ppProjectVersions[i], UGC_PUBLISH_FAILED, pComment);
					}
				}
			}

			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}


	TRANSACTION_RETURN_FAILURE("In UGCProj %u, Didn't find version with namespace %s", pUGCProject->id, pNameSpace);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".id, .Ppprojectversions");
enumTransactionOutcome trSetVersionRepublishFailed(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, char *pNameSpace, char *pComment)
{
	int i;

	for (i=eaSize(&pUGCProject->ppProjectVersions) -1; i >= 0; i--)
	{
		if (stricmp(pUGCProject->ppProjectVersions[i]->pNameSpace, pNameSpace) == 0)
		{
			if (ugcProjectGetVersionState(pUGCProject->ppProjectVersions[i]) == UGC_REPUBLISHING)
			{
				ugcProjectSetVersionState(pUGCProject->ppProjectVersions[i], UGC_REPUBLISH_FAILED, pComment);
			}
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}


	TRANSACTION_RETURN_FAILURE("In UGCProj %u, Didn't find version with namespace %s", pUGCProject->id, pNameSpace);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".PpprojectVersions, .Id");
enumTransactionOutcome trUGCProjectSetPublishValidated(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, char *pNameSpace)
{
	int i;

	for (i=eaSize(&pUGCProject->ppProjectVersions) -1; i >= 0; i--)
	{
		if (stricmp(pUGCProject->ppProjectVersions[i]->pNameSpace, pNameSpace) == 0)
		{
			if (ugcProjectGetVersionState(pUGCProject->ppProjectVersions[i]) == UGC_PUBLISHED)
			{
				pUGCProject->ppProjectVersions[i]->bPublishValidated = true;
			}

			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}


	TRANSACTION_RETURN_FAILURE("In UGCProj %u, Didn't find version with namespace %s", pUGCProject->id, pNameSpace);
}

AUTO_STRUCT;
typedef struct SetPublishResultCache
{
	ContainerID iUGCProj;
	ContainerID iUGCProjSeriesID;
	
	char *pNameSpace;
	int iStatus;
	char *pComment;
	SlowRemoteCommandID iCmdID;
} SetPublishResultCache;

void SetPublishResult_CB(TransactionReturnVal *pReturn, SetPublishResultCache *pCache)
{
	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS) 
	{
		AssertOrAlertWarning("UGC_SETPUBLISHRESULT_FAILED", "Setting the UGC publish result in a UGCProject container failed because: %s. ID: %u. NS: %s. Status: %d. Comment: %s", 
			GetTransactionFailureString(pReturn), pCache->iUGCProj, pCache->pNameSpace, pCache->iStatus, pCache->pComment);
	}
	
	SlowRemoteCommandReturn_aslUGCDataManager_UpdateUGCProjectPublishStatus(pCache->iCmdID, 1);

	StructDestroy(parse_SetPublishResultCache, pCache);
}

static void UpdateUGCProjectPublishStatus_AccountCB( UGCAccount* ignored, SetPublishResultCache* pCache )
{
	AutoTrans_trUGCProjectSetPublishResult(LoggedTransactions_CreateManagedReturnVal("UGCProjectPublishStatus", SetPublishResult_CB, pCache), GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pCache->iUGCProj, GLOBALTYPE_UGCPROJECTSERIES, pCache->iUGCProjSeriesID, pCache->pNameSpace, pCache->iStatus, pCache->pComment);
}

AUTO_COMMAND_REMOTE_SLOW(int);
void aslUGCDataManager_UpdateUGCProjectPublishStatus(SlowRemoteCommandID iCmdID, ContainerID iUGCProj, char *pNameSpace, int iStatus, char *pComment)
{
	SetPublishResultCache *pCache = StructCreate(parse_SetPublishResultCache);
	UGCProject* pUGCProj = objGetContainerData(GLOBALTYPE_UGCPROJECT, iUGCProj);
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");
	pCache->iUGCProj = iUGCProj;
	pCache->iUGCProjSeriesID = SAFE_MEMBER( pUGCProj, seriesID );
	pCache->pNameSpace = strdup(pNameSpace);
	pCache->iStatus = iStatus;
	pCache->pComment = strdup(pComment);
	pCache->iCmdID = iCmdID;

	UGCAccountEnsureExists( SAFE_MEMBER( pUGCProj, iOwnerAccountID ), UpdateUGCProjectPublishStatus_AccountCB, pCache );
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".*");
enumTransactionOutcome trUGCProjectSendToOtherShard(ATR_ARGS, ATR_ALLOW_FULL_LOCK NOCONST(UGCProject) *pUGCProject)
{
	static char *pErrorString = NULL;
	NOCONST(UGCProjectVersion) *pVersion;

	estrClear(&pErrorString);

	if (!UGCProject_IsReadyForSendToOtherShard((UGCProject*)pUGCProject, &pErrorString))
	{
		TRANSACTION_RETURN_FAILURE("Unable to transfer project to other shard because: %s", pErrorString);
	}

	pVersion = CONTAINER_NOCONST(UGCProjectVersion, UGCProject_GetMostRecentPublishedVersion(CONTAINER_RECONST(UGCProject, pUGCProject)));

	pVersion->iLastTimeSentToOtherShard = timeSecondsSince2000();
	pVersion->bSendToOtherShardSucceeded = false;

	TRANSACTION_RETURN_SUCCESS("%s", pVersion->pNameSpace);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ppprojectversions");
enumTransactionOutcome trUGCProjectSendToOtherShard_Result(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, char *pNameSpace, int bSucceeded)
{
	int i;

	for (i=eaSize(&pUGCProject->ppProjectVersions) - 1; i >= 0; i--)
	{
		if (stricmp(pUGCProject->ppProjectVersions[i]->pNameSpace, pNameSpace) == 0)
		{
			if (bSucceeded)
			{
				pUGCProject->ppProjectVersions[i]->bSendToOtherShardSucceeded = true;
			}
			else
			{
				pUGCProject->ppProjectVersions[i]->iLastTimeSentToOtherShard = 0;
			}
			break;
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}


void NotifyPlayerOfProjectSendResult(const UGCProject *pProj, const UGCProjectVersion *pVersion, bool bSucceeded, char *pComment)
{


}

AUTO_COMMAND;
void AddUGCProjectFromOtherShard_Result(U32 iContainerID, char *pNameSpace, int bSucceeded, char *pComment)
{
	UGCProject *pProj;
	const UGCProjectVersion *pVersion;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	AutoTrans_trUGCProjectSendToOtherShard_Result(NULL, GLOBALTYPE_MAPMANAGER, GLOBALTYPE_UGCPROJECT, iContainerID, pNameSpace, bSucceeded);

	log_printf(LOG_UGC, "Heard back from other shard. AddUGCProjectToOtherShard for %u(%s) %s: %s", iContainerID, pNameSpace,  
		bSucceeded ? "SUCCEEDED" : "FAILED", pComment);

	pProj = objGetContainerData(GLOBALTYPE_UGCPROJECT, iContainerID);
	if(pProj)
	{
		pVersion = UGCProject_GetSpecificVersion(pProj, pNameSpace);

		NotifyPlayerOfProjectSendResult(pProj, pVersion, bSucceeded, pComment);
	}
}

AUTO_STRUCT;
typedef struct PeriodicVersionCheckCache
{
	ContainerID iProjID;
	char *pNameSpace;
	bool bRepublishNeeded;
	U32 iRepublishFlags;
} PeriodicVersionCheckCache;


void PeriodicProjectVersion_CB(TransactionReturnVal *pReturn, PeriodicVersionCheckCache *pCache)
{
	JobManagerGroupResult *pRetVal = NULL;
	enumTransactionOutcome eOutcome;
	UGCProject *pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, pCache->iProjID);
	const UGCProjectVersion *pVersion = NULL;
	bool bRepublishNeeded;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	assert(pCache);
	bRepublishNeeded = pCache->bRepublishNeeded;

	if(!pProject)
	{
		StructDestroy(parse_PeriodicVersionCheckCache, pCache);
		return;
	}

	pVersion = UGCProject_GetSpecificVersion(pProject, pCache->pNameSpace);

	StructDestroy(parse_PeriodicVersionCheckCache, pCache);

	if (!pVersion)
	{
		return;
	}

	eOutcome = RemoteCommandCheck_RequestJobGroupStatus(pReturn, &pRetVal);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
	

		if (pVersion)
		{
			switch(pRetVal->eResult)
			{
			case JMR_UNKNOWN:
			case JMR_FAILED:
				if (bRepublishNeeded)
				{
					TriggerAutoGroupingAlert("UGC_UNKNOWN_OR_FAILED_REPUB", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 600,
						"Project %u version %s is being set to NEEDS_REPUBLISHING because its job is unknown or failed, presumably due to a jobmanager crash or restart",
						pProject->id, pVersion->pNameSpace);
					AutoTrans_trSetVersionRepublishFailed(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id, pVersion->pNameSpace, "PeriodicProjectVersion_CB says that JobManager says that the publish task failed or is unknown");
				}
				else
				{
					AutoTrans_trUGCProjectSetPublishResult(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id, GLOBALTYPE_UGCPROJECTSERIES, pProject->seriesID, pVersion->pNameSpace, 0, "PeriodicProjectVersion_CB says that JobManager says that the publish task failed or is unknown");
				}
				break;

			case JMR_SUCCEEDED:
				AutoTrans_trUGCProjectSetPublishResult(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id, GLOBALTYPE_UGCPROJECTSERIES, pProject->seriesID, pVersion->pNameSpace, 1, "PeriodicProjectVersion_CB says that jobmanager says that the publish task succeeded");
				break;
			}
		}

		StructDestroy(parse_JobManagerGroupResult, pRetVal);
	}
	else
	{
		if (bRepublishNeeded)
		{
			AutoTrans_trSetVersionRepublishFailed(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id, pVersion->pNameSpace, "PeriodicProjectVersion_CB tried to ask jobmanager for publish state, trans failed");
			TriggerAutoGroupingAlert("UGC_UNKNOWN_OR_FAILED_REPUB", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 600,
				"Project %u version %s is being set to NEEDS_REPUBLISHING because its job status couldn't be queried from the job manager, presumably due to a jobmanager crash or restart",
				pProject->id, pVersion->pNameSpace);
		}
		else
		{
			AutoTrans_trUGCProjectSetPublishResult(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id, GLOBALTYPE_UGCPROJECTSERIES, pProject->seriesID, pVersion->pNameSpace, 0, "PeriodicProjectVersion_CB tried to ask jobmanager for publish state, trans failed");
		}
	}



}


//returns true if it's OK if this version is deleted
bool PeriodicUpdateUGCProjectVersion(const UGCProject *pProject, const UGCProjectVersion *pVersion)
{
	UGCProjectVersionState eState = ugcProjectGetVersionStateConst(pVersion);
	if (eState == UGC_PUBLISH_BEGUN)
	{
		if (pVersion->iModTime < timeSecondsSince2000() - TIME_BEFORE_VERIFYING_ONGOING_PUBLISH)
		{
			if (!(pVersion->pPublishJobName && pVersion->pPublishJobName[0]))
			{
				AssertOrAlertWarning("UGC_NO_JOB_NAME", "Project %u version %s has begun publish but has no job name",
					pProject->id, pVersion->pNameSpace);
				AutoTrans_trUGCProjectSetPublishResult(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id, GLOBALTYPE_UGCPROJECTSERIES, pProject->seriesID, pVersion->pNameSpace, 0, 
					"PeriodicUpdateUGCProjectVersion thinks it should be publishing, but there is no publish job name");
			}
			else
			{
				PeriodicVersionCheckCache *pCache = StructCreate(parse_PeriodicVersionCheckCache);
				pCache->iProjID = pProject->id;
				pCache->pNameSpace = strdup(pVersion->pNameSpace);
				RemoteCommand_RequestJobGroupStatus(objCreateManagedReturnVal(PeriodicProjectVersion_CB, pCache), GLOBALTYPE_JOBMANAGER, 0, 
					pVersion->pPublishJobName);
			}
		}
	}
	else if (eState == UGC_REPUBLISHING)
	{
		if (pVersion->iModTime < timeSecondsSince2000() - TIME_BEFORE_VERIFYING_ONGOING_PUBLISH)
		{
			if (!(pVersion->pPublishJobName && pVersion->pPublishJobName[0]))
			{
				AssertOrAlertWarning("UGC_NO_JOB_NAME", "Project %u version %s should be re-publishing but has no job name. Setting to NEEDS_REPUBLISHING",
							  pProject->id, pVersion->pNameSpace);
				AutoTrans_trSetVersionRepublishFailed(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id, pVersion->pNameSpace, "PeriodicUpdateUGCProjectVersion thinks version is republishing, but has no job name");
			}
			else
			{
				PeriodicVersionCheckCache *pCache = StructCreate(parse_PeriodicVersionCheckCache);
				pCache->iProjID = pProject->id;
				pCache->pNameSpace = strdup(pVersion->pNameSpace);
				pCache->bRepublishNeeded = true;
				pCache->iRepublishFlags = pVersion->iMostRecentRepublishFlags;
				RemoteCommand_RequestJobGroupStatus(objCreateManagedReturnVal(PeriodicProjectVersion_CB, pCache), GLOBALTYPE_JOBMANAGER, 0, 
													pVersion->pPublishJobName);
			}
		}
	}

	if (pVersion->iLastTimeSentToOtherShard && !pVersion->bSendToOtherShardSucceeded)
	{
		if (pVersion->iLastTimeSentToOtherShard < timeSecondsSince2000() - SEND_TO_OTHER_SHARD_FAILURE_TIME)
		{
			log_printf(LOG_UGC, "%d seconds since proj %u(%s) was sent to other shard, hasn't succeeded. Timing out",
				SEND_TO_OTHER_SHARD_FAILURE_TIME, pProject->id, pVersion->pNameSpace);
			AutoTrans_trUGCProjectSendToOtherShard_Result(NULL, GLOBALTYPE_MAPMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id, pVersion->pNameSpace, false);
			NotifyPlayerOfProjectSendResult(pProject, pVersion, false, "Timed out");
		}
	}

	if (eState == UGC_WITHDRAWN)
	{
		if (!pVersion->iModTime || pVersion->iModTime < timeSecondsSince2000() - iDelayBeforeMakingWithdrawnUGCProjectUnplayable)
		{
			log_printf(LOG_UGC, "%s has been WITHDRAWN for a long time, making it unplayable", pVersion->pNameSpace);
			AutoTrans_trSetVersionUnplayable(NULL, GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, pProject->id, 
				pVersion->pNameSpace, "WITHDRAWN for a long time");
			return false;
		}
	}

	//check if OK to delete this verison
	if (eState == UGC_PUBLISH_FAILED || eState == UGC_UNPLAYABLE)
	{
		//going forward, modtime should always be set, but it didn't used to be
		if (!pVersion->iModTime || pVersion->iModTime < timeSecondsSince2000() - iDelayBeforeDeletingUnplayableUGCProject)
		{
			return true;
		}
	}

	return false;

}

static void ugcProjectDestroyCB_CheckLocationCB(TransactionReturnVal *returnVal, char *pUserDataString)
{
	char *pResultString = NULL;


	if(RemoteCommandCheck_GetDebugContainerLocString(returnVal, &pResultString) == TRANSACTION_OUTCOME_FAILURE)
	{
		AssertOrAlertWarning("UGC_PROJ_DELETE_FAIL", "%s (GetDebugContainerLocString FAILED)", 
			pUserDataString);
	}
	else
	{
		AssertOrAlertWarning("UGC_PROJ_DELETE_FAIL", "%s %s", 
			pUserDataString, pResultString);
		estrDestroy(&pResultString);

	}

	free(pUserDataString);
}

typedef struct UGCProjectDestroyData
{
	ContainerID ugcAccountID;
	ContainerID ugcProjectID;
} UGCProjectDestroyData;

void ugcProjectDestroyCB(TransactionReturnVal *pReturn, UGCProjectDestroyData *pUGCProjectDestroyData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		AutoTrans_trUgcRemoveProjectAchievements(NULL, GetAppGlobalType(), GLOBALTYPE_UGCACCOUNT, pUGCProjectDestroyData->ugcAccountID, pUGCProjectDestroyData->ugcProjectID);
	}
	else
	{
		char *pFailureString = GetTransactionFailureString(pReturn);

		if (strstri(pFailureString, "doesn't own the container"))
		{
			char *pTempString = NULL;
			pTempString = strdupf("Unable to delete project %u because: %s. Querying container location from object DB: ",
				pUGCProjectDestroyData->ugcProjectID, pFailureString);

			RemoteCommand_GetDebugContainerLocString(objCreateManagedReturnVal(ugcProjectDestroyCB_CheckLocationCB, pTempString),
				GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_UGCPROJECT, pUGCProjectDestroyData->ugcProjectID);

		}
		else
		{
			AssertOrAlertWarning("UGC_PROJ_DELETE_FAIL", "Unable to delete ugc project %u because: %s", 
				pUGCProjectDestroyData->ugcProjectID, pFailureString);
		}
	}

	free(pUGCProjectDestroyData);
}

static void UGCProjectNaughtyDecay_CB(TransactionReturnVal* pReturn, void* pUserData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		char* pchResult = objAutoTransactionGetResult(pReturn);
		if (strlen(pchResult) > 4)
		{
			log_printf(LOG_UGC, "%s", pchResult+4);
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Ugcreporting.Utemporarybanexpiretime, .Ugcreporting.Inaughtyvalue, .Ugcreporting.Unextnaughtydecaytime, .Id, .Pidstring, .Powneraccountname, .Iowneraccountid");
enumTransactionOutcome trUgcProjectPerformNaughtyDecay(ATR_ARGS, NOCONST(UGCProject) *pProject)
{
	bool bNaughtyDecay = false;
	bool bTempUnban = false;
	U32 uCurrentTime = timeSecondsSince2000();

	// Handle temporary ban expiration
	if (pProject->ugcReporting.uTemporaryBanExpireTime > 0 &&
		uCurrentTime >= pProject->ugcReporting.uTemporaryBanExpireTime)
	{
		pProject->ugcReporting.uTemporaryBanExpireTime = 0;
		TRANSACTION_APPEND_LOG_SUCCESS("Project %s(%d) owned by account %s(%d) was unbanned (temporary ban) automatically. Naughty value: %d",
				pProject->pIDString, pProject->id, 
				pProject->pOwnerAccountName, pProject->iOwnerAccountID,
				pProject->ugcReporting.iNaughtyValue);
		bTempUnban = true;
	}

	// Handle naughty decay
	if (pProject->ugcReporting.iNaughtyValue > 0 &&
		uCurrentTime >= pProject->ugcReporting.uNextNaughtyDecayTime)
	{
		pProject->ugcReporting.iNaughtyValue -= g_ReportingDef.iNaughtyDecayValue;

		if (pProject->ugcReporting.iNaughtyValue > 0)
		{
			pProject->ugcReporting.uNextNaughtyDecayTime = uCurrentTime + g_ReportingDef.uNaughtyDecayInterval;
		}
		else
		{
			pProject->ugcReporting.iNaughtyValue = 0;
			pProject->ugcReporting.uNextNaughtyDecayTime = 0;
		}
		TRANSACTION_APPEND_LOG_SUCCESS("Naughty Decay: Project %s(%d) owned by account %s(%d) now has a naughty value of %d.",
				pProject->pIDString, pProject->id, 
				pProject->pOwnerAccountName, pProject->iOwnerAccountID,
				pProject->ugcReporting.iNaughtyValue);
		bNaughtyDecay = true;
	}
	if (!bNaughtyDecay && !bTempUnban)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void PeriodicUpdateUGCProjectNaughtyDecay(const UGCProject* pProject)
{
	U32 uCurrentTime = timeSecondsSince2000();
	ContainerID uProjectID = pProject->id;
	bool bDirty = false;

	if (pProject->bBanned || UGCProject_CanAutoDelete(pProject))
	{
		return;
	}
	if (pProject->ugcReporting.uTemporaryBanExpireTime > 0 &&
		uCurrentTime >= pProject->ugcReporting.uTemporaryBanExpireTime)
	{
		bDirty = true;
	}
	if (g_ReportingDef.iNaughtyDecayValue > 0 &&
		pProject->ugcReporting.iNaughtyValue > 0 &&
		uCurrentTime >= pProject->ugcReporting.uNextNaughtyDecayTime)
	{
		bDirty = true;
	}
	if (bDirty)
	{
		AutoTrans_trUgcProjectPerformNaughtyDecay(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, UGCProjectNaughtyDecay_CB, NULL),
			GetAppGlobalType(),
			GLOBALTYPE_UGCPROJECT, uProjectID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Ppprojectversions, .Ppdeletedprojectversions");
enumTransactionOutcome trUgcProjectRemoveVersion(ATR_ARGS, NOCONST(UGCProject) *pProject, char *pNameSpace)
{
	int i;

	for (i=0; i < eaSize(&pProject->ppProjectVersions); i++)
	{
		if (stricmp(pProject->ppProjectVersions[i]->pNameSpace, pNameSpace) == 0)
		{
			NOCONST(UGCDeletedProjectVersion) *pUGCDeletedProjectVersion = StructCreateNoConst(parse_UGCDeletedProjectVersion);

			pUGCDeletedProjectVersion->eState = ugcProjectGetVersionState(pProject->ppProjectVersions[i]);
			estrCopy(&pUGCDeletedProjectVersion->pNameSpace, &pProject->ppProjectVersions[i]->pNameSpace);
			pUGCDeletedProjectVersion->iDeletedTime = timeSecondsSince2000();

			StructDestroyNoConst(parse_UGCProjectVersion, pProject->ppProjectVersions[i]);
			eaRemove(&pProject->ppProjectVersions, i);

			eaPush(&pProject->ppDeletedProjectVersions, pUGCDeletedProjectVersion);

			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

void PeriodicUpdateUGCProject(const UGCProject *pProject)
{
	int i;

	if (pProject->bTestOnly)
	{
		return;
	}

	if (pProject->iNextTimeOKForPeriodicUpdate > timeSecondsSince2000())
	{
		return;
	}

	((UGCProject*)pProject)->iNextTimeOKForPeriodicUpdate = timeSecondsSince2000() + sMinPeriodicUpdateDelay;


	if (pProject->iDeletionTime && pProject->iDeletionTime < timeSecondsSince2000() - sUGCProjectZombieTime)
	{
		static char *pDurationString = NULL;

		UGCProjectDestroyData *pUGCProjectDestroyData = calloc(1, sizeof(UGCProjectDestroyData));
		pUGCProjectDestroyData->ugcAccountID = pProject->iOwnerAccountID;
		pUGCProjectDestroyData->ugcProjectID = pProject->id;

		if(!pDurationString)
			timeSecondsDurationToPrettyEString(sUGCProjectZombieTime, &pDurationString);

		log_printf(LOG_UGC, "Requesting destruction of container %u (%s) (owned by %u %s) because it was deleted %s ago",
			pProject->id, pProject->pIDString, pProject->iOwnerAccountID, pProject->pOwnerAccountName, pDurationString);

		objRequestContainerDestroy(objCreateManagedReturnVal(ugcProjectDestroyCB, pUGCProjectDestroyData), GLOBALTYPE_UGCPROJECT, pProject->id, objServerType(), objServerID());
	}
	else if( pProject->bUGCFeaturedCopyProjectInProgress && pProject->iCreationTime < timeSecondsSince2000() - sUGCProjectZombieTime )
	{
		static char *pDurationString = NULL;

		UGCProjectDestroyData *pUGCProjectDestroyData = calloc(1, sizeof(UGCProjectDestroyData));
		pUGCProjectDestroyData->ugcAccountID = pProject->iOwnerAccountID;
		pUGCProjectDestroyData->ugcProjectID = pProject->id;

		if(!pDurationString)
			timeSecondsDurationToPrettyEString(sUGCProjectZombieTime, &pDurationString);

		log_printf(LOG_UGC, "Requesting destruction of container %u (%s) (owned by %u %s) because its UGCFeaturedCopyProject process was started %s ago and never completed",
			pProject->id, pProject->pIDString, pProject->iOwnerAccountID, pProject->pOwnerAccountName, pDurationString);

		objRequestContainerDestroy(objCreateManagedReturnVal(ugcProjectDestroyCB, pUGCProjectDestroyData), GLOBALTYPE_UGCPROJECT, pProject->id, objServerType(), objServerID());
	}
	else
	{
		// If necessary, rebuild rating buckets
		if( ea32Size( &pProject->ugcReviews.piNumRatings ) != UGCPROJ_NUM_RATING_BUCKETS ) {
			AutoTrans_trUGCProjectRebuildRatingBucketsIfNecessary( NULL, GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, pProject->id );
		}
		
		for (i=eaSize(&pProject->ppProjectVersions) - 1; i >= 0; i--)
		{
			bool bOKToDelete = PeriodicUpdateUGCProjectVersion(pProject, pProject->ppProjectVersions[i]);
			if (bOKToDelete && i < eaSize(&pProject->ppProjectVersions) - UGC_NUM_RECENT_VERSIONS_TO_ALWAYS_KEEP)
			{
				AutoTrans_trUgcProjectRemoveVersion(NULL, GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, pProject->id, pProject->ppProjectVersions[i]->pNameSpace);
			}
		}

		// Handle project naughty decay
		PeriodicUpdateUGCProjectNaughtyDecay(pProject);
	}
}

typedef struct UGCProjectSeriesDestroyData
{
	ContainerID ugcAccountID;
	ContainerID ugcProjectSeriesID;
} UGCProjectSeriesDestroyData;

static void ugcProjectSeries_DestroyCB(TransactionReturnVal *pReturn, UGCProjectSeriesDestroyData *pUGCProjectSeriesDestroyData)
{
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		AutoTrans_trUgcRemoveSeriesAchievements(NULL, GetAppGlobalType(), GLOBALTYPE_UGCACCOUNT, pUGCProjectSeriesDestroyData->ugcAccountID, pUGCProjectSeriesDestroyData->ugcProjectSeriesID);
	}
	else
	{
		AssertOrAlertWarning("UGC_PROJECTSERIES_DELETE_FAIL", "Unable to delete ugc UGCProjectSeries %u because: %s", 
					  pUGCProjectSeriesDestroyData->ugcProjectSeriesID, GetTransactionFailureString( pReturn )); 
	}

	free(pUGCProjectSeriesDestroyData);
}

void PeriodicUpdateUGCProjectSeries( const UGCProjectSeries* ugcSeries )
{
	if( UGCProjectSeries_CanAutoDelete( ugcSeries )) {
		UGCProjectSeriesDestroyData *pUGCProjectSeriesDestroyData = calloc(1, sizeof(UGCProjectSeriesDestroyData));
		pUGCProjectSeriesDestroyData->ugcAccountID = ugcSeries->iOwnerAccountID;
		pUGCProjectSeriesDestroyData->ugcProjectSeriesID = ugcSeries->id;

		log_printf( LOG_UGC, "Requesting destruction of Series container %d (%s) because it is empty.", ugcSeries->id, ugcSeries->strIDString );

		objRequestContainerDestroy( objCreateManagedReturnVal( ugcProjectSeries_DestroyCB, pUGCProjectSeriesDestroyData), GLOBALTYPE_UGCPROJECTSERIES, ugcSeries->id, objServerType(), objServerID() );
	} else {
		// If necessary, rebuild rating buckets
		if( ea32Size( &ugcSeries->ugcReviews.piNumRatings ) != UGCPROJ_NUM_RATING_BUCKETS ) {
			AutoTrans_trUGCProjectSeriesRebuildRatingBucketsIfNecessary( NULL, GetAppGlobalType(), GLOBALTYPE_UGCPROJECTSERIES, ugcSeries->id );
		}
	}
}

static void PeriodicUpdateUGCAccount_TransCB( TransactionReturnVal* pReturn, void* ignored )
{
	if( pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS ) {
		printf( "Transaction Failed: %s\n", GetTransactionFailureString( pReturn ));
	}
}

void PeriodicUpdateUGCAccount( const UGCAccount* ugcAccount )
{
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");
	
	// Validate project achievements
	{
		ContainerID *removals = NULL;
		int achievementsIt, removalsIt;
		for( achievementsIt = 0; achievementsIt != eaSize( &ugcAccount->author.eaProjectAchievements ); ++achievementsIt ) {
			UGCProjectAchievementInfo* pUGCProjectAchievementInfo = ugcAccount->author.eaProjectAchievements[ achievementsIt ];
			UGCProject* pUGCProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, pUGCProjectAchievementInfo->projectID);
			if(gConf.bUGCAchievementsEnable)
			{
				if(!pUGCProject)
				{
					AssertOrAlertWarning( "UGC_PROJECT_ACHIEVEMENTS_INCONSISTENT",
						"UGCAccount author %d has UGC Achievement data for UGCProject %d, yet that project has been deleted. "
						"This will be fixed automatically by assuming the UGC Achievement data for that project should be deleted.  Please report this to the UGC dev team.",
						ugcAccount->accountID, pUGCProjectAchievementInfo->projectID );
					eaiPush(&removals, pUGCProjectAchievementInfo->projectID);
				}
			}
			else
				eaiPush(&removals, pUGCProjectAchievementInfo->projectID);
		}

		for( removalsIt = 0; removalsIt != eaiSize( &removals ); ++removalsIt )
		{
			AutoTrans_trUgcRemoveProjectAchievements( LoggedTransactions_CreateManagedReturnVal( __FUNCTION__, PeriodicUpdateUGCAccount_TransCB, NULL ), GetAppGlobalType(),
				GLOBALTYPE_UGCACCOUNT, ugcAccount->accountID,
				removals[ removalsIt ] );
		}

		eaiDestroy(&removals);
	}

	// Validate series achievements
	{
		ContainerID *removals = NULL;
		int achievementsIt, removalsIt;
		for( achievementsIt = 0; achievementsIt != eaSize( &ugcAccount->author.eaSeriesAchievements ); ++achievementsIt ) {
			UGCSeriesAchievementInfo* pUGCSeriesAchievementInfo = ugcAccount->author.eaSeriesAchievements[ achievementsIt ];
			UGCProjectSeries* pUGCProjectSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, pUGCSeriesAchievementInfo->seriesID);
			if(gConf.bUGCAchievementsEnable)
			{
				if(!pUGCProjectSeries)
				{
					AssertOrAlertWarning( "UGC_PROJECT_SERIES_ACHIEVEMENTS_INCONSISTENT",
						"UGCAccount author %d has UGC Achievement data for UGCProjectSeries %d, yet that project series has been deleted. "
						"This will be fixed automatically by assuming the UGC Achievement data for that project series should be deleted.  Please report this to the UGC dev team.",
						ugcAccount->accountID, pUGCSeriesAchievementInfo->seriesID );
					eaiPush(&removals, pUGCSeriesAchievementInfo->seriesID);
				}
			}
			else
				eaiPush(&removals, pUGCSeriesAchievementInfo->seriesID);
		}

		for( removalsIt = 0; removalsIt != eaiSize( &removals ); ++removalsIt )
		{
			AutoTrans_trUgcRemoveSeriesAchievements( LoggedTransactions_CreateManagedReturnVal( __FUNCTION__, PeriodicUpdateUGCAccount_TransCB, NULL ), GetAppGlobalType(),
				GLOBALTYPE_UGCACCOUNT, ugcAccount->accountID,
				removals[ removalsIt ] );
		}

		eaiDestroy(&removals);
	}

	// Validate account achievements
	{
		if(!gConf.bUGCAchievementsEnable)
			if(eaSize(&ugcAccount->author.ugcAccountAchievements.eaAchievements))
				AutoTrans_trUgcRemoveAccountAchievements(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, PeriodicUpdateUGCAccount_TransCB, NULL), GetAppGlobalType(),
					GLOBALTYPE_UGCACCOUNT, ugcAccount->accountID);
	}
}

#define PERIODIC_PROCESS_TIME 0.1f

static U64 sMaxTicksInPeriodicUpdate = 0;
static bool bAlreadyAlertedSlowUpdate = false;

typedef void (*PeriodicUpdateFn)( void* containerData );

typedef struct PeriodicUpdateDef {
	// Set once on startup
	GlobalType containerType;		//< container type for this, e.g. GLOBALTYPE_UGCPROJECT
	PeriodicUpdateFn cb;			//< the update function, called on each container
	float updateInterval;			//< how often the updates happen (in seconds)
	float fullTimeAlertThreshold;	//< if more than this time is needed to process all containers, alert

	// Used during runtime
	ContainerIterator it;
} PeriodicUpdateDef;

void GenericPeriodicUpdate(TimedCallback* callback, F32 timeSinceLastCallback, PeriodicUpdateDef* def)
{
	PERFINFO_AUTO_START_FUNC();
	{
		U64 startTime = timerCpuTicks64();
		int containerCount = objCountTotalContainersWithType( def->containerType );
		int processedCount = 0;

		if( containerCount == 0 ) {
			PERFINFO_AUTO_STOP();
			return;
		}

		while( true ) {
			void* containerData = objGetNextObjectFromIteratorEx( &def->it, false, true);

			if( !containerData ) {
				objInitContainerIteratorFromTypeEx( def->containerType, &def->it, false, true );
				containerData = objGetNextObjectFromIteratorEx( &def->it, false, true );
				assert( containerData );
			}

			++processedCount;
			def->cb( containerData );

			if( processedCount == containerCount ) {
				break;
			}
			if( timerCpuTicks64() - startTime > sMaxTicksInPeriodicUpdate ) {
				if( !bAlreadyAlertedSlowUpdate ) {
					// Could not process all containers in this tick,
					// check how long we think it will take to do them
					// all
					float estimatedFullTime = (float)containerCount / processedCount * def->updateInterval;
					if( estimatedFullTime > def->fullTimeAlertThreshold ) {
						TriggerAlertf( "UGC_SLOW_PERIODIC_UPDATE", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0,
									   "In %f seconds, only ran periodic update on %d out of %d %s containers.  Periodic updates happen every %f seconds.  This indicates it will likely take %f seconds to update all the containers, which is too slow.",
									   PERIODIC_PROCESS_TIME, processedCount, containerCount, GlobalTypeToName( def->containerType ), def->updateInterval, estimatedFullTime );
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

void StartGenericPeriodicUpdate( PeriodicUpdateDef* def )
{
	TimedCallback_Add( GenericPeriodicUpdate, def, def->updateInterval );
}

/// Description of each type's periodic update
PeriodicUpdateDef periodicUpdateUGCProjectDef =			{ GLOBALTYPE_UGCPROJECT,	   PeriodicUpdateUGCProject,       5, 300 };
PeriodicUpdateDef periodicUpdateUGCProjectSeriesDef =	{ GLOBALTYPE_UGCPROJECTSERIES, PeriodicUpdateUGCProjectSeries, 7, 300 };
PeriodicUpdateDef periodicUpdateUGCAccountDef =			{ GLOBALTYPE_UGCACCOUNT,	   PeriodicUpdateUGCAccount,       7, 300 };

void aslUGCDataManagerProject_StartNormalOperation(void)
{
	sMaxTicksInPeriodicUpdate = timerCpuSpeed64() * PERIODIC_PROCESS_TIME;
	StartGenericPeriodicUpdate( &periodicUpdateUGCProjectDef );
	StartGenericPeriodicUpdate( &periodicUpdateUGCProjectSeriesDef );
	StartGenericPeriodicUpdate( &periodicUpdateUGCAccountDef );

	if ( gbUseShardNameInPatchProjectNames && isProductionMode()  )
	{
		TimedCallback_RunAtTimeOfDay(aslDailyUGCProjectUpdate, NULL, TIME_OF_DAY_FOR_UGCPROJ_DAILY_UPDATE);
	}
}

UGCProject *GetProjectAndVersionFromNameSpace(const char *pNameSpace, UGCProjectVersion **ppOutVersion)
{
	U32 iContainerIDFromNS = UGCProject_GetContainerIDFromUGCNamespace(pNameSpace);
	UGCProject *pProj = objGetContainerData(GLOBALTYPE_UGCPROJECT, iContainerIDFromNS);
	int i;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if (pProj)
	{
		for (i = eaSize(&pProj->ppProjectVersions) -1; i >= 0; i--)
		{
			if (stricmp(pProj->ppProjectVersions[i]->pNameSpace, pNameSpace) == 0)
			{
				*ppOutVersion = pProj->ppProjectVersions[i];
				return pProj;
			}
		}
	}

	return NULL;
}

AUTO_STRUCT;
typedef struct UGCProjectFilter
{
	//3 relevant variables: "project" "version" and "map"
	char *pExprString;
	Expression *pExpr;

	bool bStates[UGC_LAST];
} UGCProjectFilter;

static ExprFuncTable* sSharedFuncTable = NULL;


UGCProjectVersionRef *MakeVersionRef(UGCProject *pProject, UGCProjectVersion *pVersion)
{
	UGCProjectVersionRef *pRetVal = StructCreate(parse_UGCProjectVersionRef);
	pRetVal->iID = pProject->id;
	pRetVal->pNameSpaceName = strdup(pVersion->pNameSpace);

	return pRetVal;
}

bool VersionMeetsFilter(UGCProject *pProject, UGCProjectVersion *pVersion, UGCProjectFilter *pFilter, ExprContext *pContext)
{
	if (!pFilter->bStates[ugcProjectGetVersionStateConst(pVersion)])
	{
		return false;
	}

	exprContextSetPointerVar(pContext, "project", pProject, parse_UGCProject, false, true);
	exprContextSetPointerVar(pContext, "version", pVersion, parse_UGCProjectVersion, false, true);

	{
		MultiVal answer = {0};
		int iAnswer;
				
		if (!pFilter->pExpr)
		{
			pFilter->pExpr = exprCreate();
			exprGenerateFromString(pFilter->pExpr, pContext, pFilter->pExprString, NULL);
		}

		exprEvaluate(pFilter->pExpr, pContext, &answer);
	

		iAnswer = QuickGetInt(&answer);

		if (iAnswer)
		{
			return true;
		}
	}

	return false;
}

char *GetDescriptiveFilterString(UGCProjectFilter *pFilter)
{
	static char *pOutString = NULL;
	int i;

	estrClear(&pOutString);

	for (i = 0; i < UGC_LAST; i++)
	{
		if (pFilter->bStates[i])
		{
			estrConcatf(&pOutString, "%s%s", estrLength(&pOutString) ? ", " : "", StaticDefineIntRevLookup(UGCProjectVersionStateEnum, i));
		}
	}

	if (!estrLength(&pOutString))
	{
		estrPrintf(&pOutString, "Expr string \"%s\", but no states (ie UGC_PUBLISHED) specified, so nothing will ever match", pFilter->pExprString);
	}
	else
	{
		estrInsertf(&pOutString, 0, "Expr string \"%s\", states: ", pFilter->pExprString);
	}

	return pOutString;
}


void FillInFilteredList(UGCProjectFilteredList *pFilteredList, UGCProjectFilter *pFilter)
{
	ExprContext *pContext;
	
	UGCProject *pProject;

	if (!sSharedFuncTable)
	{
		sSharedFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(sSharedFuncTable, "util");
	}

	pContext = exprContextCreate();
	exprContextSetFuncTable(pContext, sSharedFuncTable);

	if (GetAppGlobalType() == GLOBALTYPE_UGCDATAMANAGER)
	{
		ContainerIterator iter;
		objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECT, &iter);

		while ((pProject = objGetNextObjectFromIterator(&iter)))
		{
			int i;

			for (i=eaSize(&pProject->ppProjectVersions) - 1; i >= 0; i--)
			{
		
				if (VersionMeetsFilter(pProject, pProject->ppProjectVersions[i], pFilter, pContext))
				{
					eaPush(&pFilteredList->ppVersions, MakeVersionRef(pProject, pProject->ppProjectVersions[i]));
				}
			}
		}
	}
	else
	{
		ResourceIterator iter = {0};
		
		if (resInitIterator(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT), &iter))
		{
			char *pName;

			while (resIteratorGetNext(&iter, &pName, &pProject))
			{
				int i;

				for (i=eaSize(&pProject->ppProjectVersions) - 1; i >= 0; i--)
				{
		
					if (VersionMeetsFilter(pProject, pProject->ppProjectVersions[i], pFilter, pContext))
					{
						eaPush(&pFilteredList->ppVersions, MakeVersionRef(pProject, pProject->ppProjectVersions[i]));
					}
				}
			}
			resFreeIterator(&iter);
		}
	}

	estrPrintf(&pFilteredList->pSummaryString, "ID %s: Search found %d versions. Filter: \"%s\"",
		pFilteredList->ID, eaSize(&pFilteredList->ppVersions), GetDescriptiveFilterString(pFilter));
}


StashTable sFilteredListsByID = NULL;
static int siNextFilteredListID = 1;

AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_ACCESSLEVEL(7);
void DeleteFilteredList(char *pID)
{
	UGCProjectFilteredList *pList;

	if (!sFilteredListsByID || !stashRemovePointer(sFilteredListsByID, pID, &pList))
	{
		return;
	}

	StructDestroy(parse_UGCProjectFilteredList, pList);
}






UGCProjectFilteredList *CreateAndRegisterEmptyFilteredList(void)
{

	UGCProjectFilteredList *pList;

	if (!sFilteredListsByID)
	{
		sFilteredListsByID = stashTableCreateWithStringKeys(16, StashDefault);
		resRegisterDictionaryForStashTable("FilteredUGCProjLists", RESCATEGORY_SYSTEM, 0, sFilteredListsByID, parse_UGCProjectFilteredList);

	}
	pList = StructCreate(parse_UGCProjectFilteredList);
	sprintf(pList->ID, "%u", siNextFilteredListID++);

	stashAddPointer(sFilteredListsByID, pList->ID, pList, false);

	return pList;

}

int SortVersionsByiID(const UGCProjectVersionRef **pRef1, const UGCProjectVersionRef **pRef2)
{
	return (*pRef1)->iID - (*pRef2)->iID;
}


AUTO_COMMAND ACMD_CATEGORY(UGC);
char *DivideIntoNewestAndOther(char *pID)
{
	UGCProjectFilteredList *pInList;
	UGCProjectFilteredList *pOutListNewest;
	UGCProjectFilteredList *pOutListOther;

	int iFirstForProjectIndex = 0;
	int i;
	static char *pRetString = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if (!sFilteredListsByID || !stashRemovePointer(sFilteredListsByID, pID, &pInList))
	{
		return "unknown list";
	}

	if (!eaSize(&pInList->ppVersions))
	{
		return "No versions, nothing to sort";
	}

	eaQSort(pInList->ppVersions, SortVersionsByiID);

	pOutListNewest = CreateAndRegisterEmptyFilteredList();
	pOutListOther = CreateAndRegisterEmptyFilteredList();

	i = 1;

	while (1)
	{
		if (i >= eaSize(&pInList->ppVersions) || pInList->ppVersions[i]->iID != pInList->ppVersions[iFirstForProjectIndex]->iID)
		{
			UGCProject *pProj = objGetContainerData(GLOBALTYPE_UGCPROJECT, pInList->ppVersions[iFirstForProjectIndex]->iID);
			int iBestIndex = iFirstForProjectIndex;
			int iBest = pProj ? UGCProject_GetVersionIndex(pProj, pInList->ppVersions[iFirstForProjectIndex]->pNameSpaceName) : 0;
			int j;

			for (j = iFirstForProjectIndex + 1; j < i; j++)
			{
				int iNew = pProj ? UGCProject_GetVersionIndex(pProj, pInList->ppVersions[j]->pNameSpaceName) : 0;
				if (iNew > iBest)
				{
					iBest = iNew;
					iBestIndex = j;
				}
			}

			for (j = iFirstForProjectIndex; j < i; j++)
			{
				if (j == iBestIndex)
				{
					eaPush(&pOutListNewest->ppVersions, StructClone(parse_UGCProjectVersionRef, pInList->ppVersions[j]));
				}
				else
				{
					eaPush(&pOutListOther->ppVersions, StructClone(parse_UGCProjectVersionRef, pInList->ppVersions[j]));
				}
			}


			if (i >= eaSize(&pInList->ppVersions))
			{
				break;
			}
			else
			{
				iFirstForProjectIndex = i;
			}
		}
		i++;
	}

	estrPrintf(&pOutListNewest->pSummaryString, "ID %s. Contains newest-only versions from %s",
		pOutListNewest->ID, pInList->ID);
	estrPrintf(&pOutListOther->pSummaryString, "ID %s. Contains NOT-newest versions from %s",
		pOutListOther->ID, pInList->ID);

	estrPrintf(&pRetString, "List divided. <a href=\"%s.globObj.Filteredugcprojlists[%s]\">Newest versions</a> <a href=\"%s.globObj.Filteredugcprojlists[%s]\">other versions</a>",
		LinkToThisServer(), pOutListNewest->ID, LinkToThisServer(), pOutListOther->ID);

	return pRetString;
}

UGCProjectFilteredList *GetFilteredListOfUGCProjectsEx(char *pExprString_in, bool bSAVED, bool bPUBLISH_BEGUN, bool bPUBLISH_FAILED, bool bPUBLISHED, bool bWITHDRAWN, bool bREPUBLISHING, bool bREPUBLISH_FAILED, bool bUNPLAYABLE, bool bNEEDS_REPUBLISHING, bool bNEEDS_UNPLAYABLE, bool bNEEDS_FIRST_PUBLISH)
{
	if (GetAppGlobalType() == GLOBALTYPE_UGCDATAMANAGER || GetAppGlobalType() == GLOBALTYPE_UGCSEARCHMANAGER)
	{
		UGCProjectFilteredList *pList = CreateAndRegisterEmptyFilteredList();

		UGCProjectFilter *pFilter = StructCreate(parse_UGCProjectFilter);
		static char *pExprString = NULL;
		estrClear(&pExprString);
		estrAppendUnescaped(&pExprString, pExprString_in);

		pFilter->bStates[UGC_NEW] = false; // had to get rid of this because we exceeded max number of arguments for AUTO_COMMANDs
		pFilter->bStates[UGC_SAVED] = bSAVED;
		pFilter->bStates[UGC_PUBLISH_BEGUN] = bPUBLISH_BEGUN;
		pFilter->bStates[UGC_PUBLISH_FAILED] = bPUBLISH_FAILED;
		pFilter->bStates[UGC_PUBLISHED] = bPUBLISHED;
		pFilter->bStates[UGC_WITHDRAWN] = bWITHDRAWN;
		pFilter->bStates[UGC_REPUBLISHING] = bREPUBLISHING;
		pFilter->bStates[UGC_REPUBLISH_FAILED] = bREPUBLISH_FAILED;
		pFilter->bStates[UGC_UNPLAYABLE] = bUNPLAYABLE;
		pFilter->bStates[UGC_NEEDS_REPUBLISHING] = bNEEDS_REPUBLISHING;
		pFilter->bStates[UGC_NEEDS_UNPLAYABLE] = bNEEDS_UNPLAYABLE;
		pFilter->bStates[UGC_NEEDS_FIRST_PUBLISH] = bNEEDS_FIRST_PUBLISH;

		pFilter->pExprString = strdup(pExprString);

		FillInFilteredList(pList, pFilter);

		StructDestroy(parse_UGCProjectFilter, pFilter);

		return pList;
	}
	
	return NULL;
}



AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_ACCESSLEVEL(7);
char *GetFilteredListOfUGCProjects(char *pExprString_in, bool bSAVED, bool bPUBLISH_BEGUN, bool bPUBLISH_FAILED, bool bPUBLISHED, bool bWITHDRAWN, bool bREPUBLISHING, bool bREPUBLISH_FAILED, bool bUNPLAYABLE, bool bNEEDS_REPUBLISHING, bool bNEEDS_UNPLAYABLE, bool bNEEDS_FIRST_PUBLISH)
{
	UGCProjectFilteredList *pList = GetFilteredListOfUGCProjectsEx(pExprString_in, bSAVED, bPUBLISH_BEGUN, bPUBLISH_FAILED,
		bPUBLISHED, bWITHDRAWN, bREPUBLISHING, bREPUBLISH_FAILED, bUNPLAYABLE, bNEEDS_REPUBLISHING, bNEEDS_UNPLAYABLE, bNEEDS_FIRST_PUBLISH);
	static char *pRetString = NULL;

	if (pList)
	{
		estrPrintf(&pRetString, "<a href=\"%s.globObj.Filteredugcprojlists[%s]\">Here is your filtered list</a>",
			LinkToThisServer(), pList->ID);

		return pRetString;
	}
	else
	{
		return "Command not supported on this server type";
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Id, .Ppprojectversions");
enumTransactionOutcome trWithdrawVersion(ATR_ARGS, NOCONST(UGCProject) *pProject, char *pNameSpace, char *pComment)
{
	NOCONST(UGCProjectVersion) *pVersion = UGCProject_trh_GetSpecificVersion(ATR_RECURSE, pProject, pNameSpace);
	if (!pVersion)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Can't withdraw %s from project %u, doesn't exist",
			pNameSpace, pProject->id);
	}

	ugcProjectSetVersionState(pVersion, UGC_WITHDRAWN, pComment);

	SAFE_FREE(pVersion->pPublishJobName);
	SAFE_FREE(pVersion->pPublishResult);

	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Id, .Ppprojectversions");
enumTransactionOutcome trStateChangeVersion(ATR_ARGS, NOCONST(UGCProject) *pProject, char *pNameSpace, int eNewState, char *pComment, int iAlsoSetCRC)
{
	NOCONST(UGCProjectVersion) *pVersion = UGCProject_trh_GetSpecificVersion(ATR_RECURSE, pProject, pNameSpace);
	if (!pVersion)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Can't state change %s from project %u, doesn't exist",
			pNameSpace, pProject->id);
	}

	ugcProjectSetVersionState(pVersion, eNewState, pComment);

	if (iAlsoSetCRC)
	{
		UGCProject_FillInTimestamp(&pVersion->sLastPublishTimeStamp);
	}

	SAFE_FREE(pVersion->pPublishJobName);
	SAFE_FREE(pVersion->pPublishResult);

	return TRANSACTION_OUTCOME_SUCCESS;
}



AUTO_STRUCT;
typedef struct WithdrawCache
{
	const char *pFilteredListID; AST(POOL_STRING)
	ContainerID iProjID;
	char *pNameSpace;
} WithdrawCache;

AUTO_STRUCT;
typedef struct StateChangeCache
{
	const char *pFilteredListID; AST(POOL_STRING)
	ContainerID iProjID;
	char *pNameSpace;
} StateChangeCache;

void Withdraw_CB(TransactionReturnVal *pReturn, WithdrawCache *pCache)
{

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
	
	} 
	else 
	{
		log_printf(LOG_UGC, "Failed to withdraw namespace %s for project %u because: %s", 
			pCache->pNameSpace, pCache->iProjID, GetTransactionFailureString(pReturn));

	}

	StructDestroy(parse_WithdrawCache, pCache);
}


void StateChange_CB(TransactionReturnVal *pReturn, StateChangeCache *pCache)
{
	UGCProjectFilteredList *pList = NULL;
	if (sFilteredListsByID)
	{
		stashFindPointer(sFilteredListsByID, pCache->pFilteredListID, &pList);
	}

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		if (pList)
		{
			pList->stateChangeTracker.iNumStateChangeSucceeds++;
		}
	} 
	else 
	{
		log_printf(LOG_UGC, "Failed to withdraw namespace %s for project %u because: %s", 
			pCache->pNameSpace, pCache->iProjID, GetTransactionFailureString(pReturn));
		if (pList)
		{
			pList->stateChangeTracker.iNumStateChangeFails++;
			estrConcatf(&pList->pErrors, "Failed to StateChange namespace %s for project %u because: %s\n", 
			pCache->pNameSpace, pCache->iProjID, GetTransactionFailureString(pReturn));
		}
	}

	StructDestroy(parse_StateChangeCache, pCache);
}





// @@ REPUBLISH FLAGS @@ 
#define REPUBLISH_FLAG_PROJECT_REGENERATE	(1<<0)
#define REPUBLISH_FLAG_MAP_BEACONIZE		(1<<1)

AUTO_STRUCT;
typedef struct RepublishInfo
{
	ContainerID iProjID;
	char *pNameSpace;
	char *pJobName;
	RemoteCommandGroup *pWhatToDoIfPublishJobDoesntStart;
	const char *pFilteredListID; AST(POOL_STRING)
	int iIndexInFilteredList;
	U32 iRepublishFlags; // See @@ REPUBLISH FLAGS @@ above

	UGCProjectInfo *pProjectInfo;
} RepublishInfo;

AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Btestonly, .Id, .Ppprojectversions");
enumTransactionOutcome trRepublish(ATR_ARGS, NOCONST(UGCProject) *pProject, RepublishInfo *pInfo, char *pComment)
{
	NOCONST(UGCProjectVersion) *pVersion = UGCProject_trh_GetSpecificVersion(ATR_RECURSE, pProject, pInfo->pNameSpace);
	if (!pVersion)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Can't republish %s from project %u, doesn't exist",
			pInfo->pNameSpace, pProject->id);
	}

	if (pProject->bTestOnly)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}


	ugcProjectSetVersionState(pVersion, UGC_REPUBLISHING, pComment);
	UGCProject_ApplyProjectInfoToVersion( pVersion, pInfo->pProjectInfo );

 	SAFE_FREE(pVersion->pPublishJobName);
	SAFE_FREE(pVersion->pPublishResult);

	pVersion->pPublishJobName = strdup(pInfo->pJobName);

	return TRANSACTION_OUTCOME_SUCCESS;
}

void RepublishBegun_CB(TransactionReturnVal *pReturn, RepublishInfo *pRepublishInfo)
{

	UGCProjectFilteredList *pFilteredList = NULL;
	if (sFilteredListsByID)
	{
		stashFindPointer(sFilteredListsByID, pRepublishInfo->pFilteredListID, &pFilteredList);
	}

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		
	}
	else
	{
		if (pFilteredList)
		{
			pFilteredList->republishTracker.iNumRepublishFails++;

			estrConcatf(&pFilteredList->pErrors, "Unable to start republish job for proj %u NS %s. Was job manager down?",
				pRepublishInfo->iProjID, pRepublishInfo->pNameSpace);

		}

		if (pRepublishInfo->pWhatToDoIfPublishJobDoesntStart)
		{
			ExecuteAndFreeRemoteCommandGroup(pRepublishInfo->pWhatToDoIfPublishJobDoesntStart, NULL, NULL);
			pRepublishInfo->pWhatToDoIfPublishJobDoesntStart = NULL;
		}

	}

	StructDestroy(parse_RepublishInfo, pRepublishInfo);
}

void AddErrorToFilteredList(UGCProjectFilteredList *pFilteredList, FORMAT_STR const char *pFmt, ...)
{
	char *pErrorString = NULL;
	estrGetVarArgs(&pErrorString, pFmt);
	
	estrConcatf(&pFilteredList->pErrors, "%s\n", pErrorString);

	estrDestroy(&pErrorString);
}

#define REP_CB_FAIL(fmt, ...) if (pFilteredList) { pFilteredList->republishTracker.iNumRepublishFails++; AddErrorToFilteredList(pFilteredList, fmt, __VA_ARGS__); }\
	StructDestroySafe(parse_JobManagerJobGroupDef, &pJobGroupDef); \
	StructDestroySafe(parse_DynamicPatchInfo, &pPatchInfo); \
	SAFE_FREE(pPatchInfoText);	\
	SAFE_FREE(pPatchInfoTextSuperEsc); \
	StructDestroy(parse_RepublishInfo, pRepublishInfo); \
	return;

void Republish_CB(TransactionReturnVal *pReturn, RepublishInfo *pRepublishInfo)
{
	JobManagerJobGroupDef *pJobGroupDef = NULL;
	JobManagerJobDef *pServerBinJob = NULL;
	JobManagerJobDef *pBeaconizingJob = NULL;
	JobManagerJobDef *pClientBinJob = NULL;
	JobManagerJobDef *pFlushCacheJob = NULL;

	DynamicPatchInfo *pPatchInfo = NULL;

	char *pPatchInfoText = NULL;
	char *pPatchInfoTextSuperEsc = NULL;
	UGCProjectInfo *pUGCProjectInfo;
	char *pProjectInfoText = NULL;
	char *pProjectInfoTextSuperEsc = NULL;

	UGCProject *pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, pRepublishInfo->iProjID);
	const UGCProjectVersion *pVersion = NULL;

	UGCProjectFilteredList *pFilteredList = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if (sFilteredListsByID)
	{
		stashFindPointer(sFilteredListsByID, pRepublishInfo->pFilteredListID, &pFilteredList);
	}

	if (pFilteredList)
	{
		pFilteredList->republishTracker.iNumRepublishesToJobManager++;
	}

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		//start job manager job
	
		
		if (!pProject)
		{
			REP_CB_FAIL("UGC project %u seems to have vanished before we can republish it",	pRepublishInfo->iProjID);
		}

		pVersion = UGCProject_GetSpecificVersion(pProject, pRepublishInfo->pNameSpace);

		if (!pVersion)
		{
			REP_CB_FAIL("Can't find version %s in ugc proj %u for republish",
				pRepublishInfo->pNameSpace, pRepublishInfo->iProjID);
		}

		if (ugcProjectGetVersionStateConst(pVersion) != UGC_REPUBLISHING)
		{
			REP_CB_FAIL("Version %s of ugc proj %u in wrong state for republishing",
				pRepublishInfo->pNameSpace, pRepublishInfo->iProjID);
		}

		pPatchInfo = StructCreate(parse_DynamicPatchInfo);
		aslFillInPatchInfo(pPatchInfo, pRepublishInfo->pNameSpace, 0);
		ParserWriteText(&pPatchInfoText, parse_DynamicPatchInfo, pPatchInfo, 0, 0, 0);
		estrSuperEscapeString(&pPatchInfoTextSuperEsc, pPatchInfoText);

		pJobGroupDef = StructCreate(parse_JobManagerJobGroupDef);

		pJobGroupDef->pComment = strdupf("Republish job for ugc proj %u NS %s", pRepublishInfo->iProjID, pRepublishInfo->pNameSpace);

		pJobGroupDef->bAlertOnFailure = true;

		pJobGroupDef->pGroupTypeName = allocAddString("UGCRePublish");
		pJobGroupDef->pJobGroupName = strdup(pRepublishInfo->pJobName);
		pJobGroupDef->owner.pPlayerOwnerAccountName = strdup(pProject->pOwnerAccountName);
		pJobGroupDef->owner.iPlayerAccountID = pProject->iOwnerAccountID;

		pUGCProjectInfo = ugcCreateProjectInfo(pProject, pVersion);
		ParserWriteText(&pProjectInfoText, parse_UGCProjectInfo, pUGCProjectInfo, 0, 0, 0);
		estrSuperEscapeString(&pProjectInfoTextSuperEsc, pProjectInfoText);
		StructDestroy(parse_UGCProjectInfo, pUGCProjectInfo);
	
		// Keep this synchronized with gslUGC_DoProjectPublishPostTransaction() in gslUgcTransactions.c
		pServerBinJob = StructCreate(parse_JobManagerJobDef);
		pServerBinJob->pJobName = strdup("Server binning");
		pServerBinJob->eType = JOB_SERVER_W_CMD_LINE;
		pServerBinJob->pServerWCmdLineDef = StructCreate(parse_JobManagerServerWCmdLineDef);
		pServerBinJob->pServerWCmdLineDef->eServerTypeToLaunch = GLOBALTYPE_SERVERBINNER;
		pServerBinJob->pServerWCmdLineDef->pExtraCmdLine = strdupf("-NoSharedMemory -ProductionEdit -loadUserNameSpaces %s -makebinsAndExitForNameSpace %s -PatchInfo %s -ServerBinnerProjectInfo %s %s",
			pRepublishInfo->pNameSpace, pRepublishInfo->pNameSpace, pPatchInfoTextSuperEsc, pProjectInfoTextSuperEsc,
			(pRepublishInfo->iRepublishFlags & REPUBLISH_FLAG_PROJECT_REGENERATE) ? "-ServerBinnerDoRegenerate" : "" );
		eaPush(&pJobGroupDef->ppJobs, pServerBinJob);

		if(pRepublishInfo->iRepublishFlags & REPUBLISH_FLAG_MAP_BEACONIZE)
		{
			pBeaconizingJob = StructCreate(parse_JobManagerJobDef);
			pBeaconizingJob->pJobName = strdup("Beaconizing");
			pBeaconizingJob->eType = JOB_SERVER_W_CMD_LINE;
			pBeaconizingJob->pServerWCmdLineDef = StructCreate(parse_JobManagerServerWCmdLineDef);
			pBeaconizingJob->pServerWCmdLineDef->eServerTypeToLaunch = GLOBALTYPE_BCNSUBSERVER;
			pBeaconizingJob->pServerWCmdLineDef->pExtraCmdLine = strdupf("-NoSharedMemory -LoadUserNamespaces %s -bcnReqProcessProject %s -PatchInfo %s",
				pRepublishInfo->pNameSpace, pRepublishInfo->pNameSpace, pPatchInfoTextSuperEsc);
			eaPush(&pBeaconizingJob->ppJobsIDependOn, strdup("Server binning"));
			eaPush(&pJobGroupDef->ppJobs, pBeaconizingJob);
		}

		// Keep this synchronized with gslUGC_DoProjectPublishPostTransaction() in gslUgcTransactions.c
		pClientBinJob = StructCreate(parse_JobManagerJobDef);
		pClientBinJob->pJobName = strdup("Client binning");
		pClientBinJob->eType = JOB_SERVER_W_CMD_LINE;
		pClientBinJob->pServerWCmdLineDef = StructCreate(parse_JobManagerServerWCmdLineDef);
		pClientBinJob->pServerWCmdLineDef->eServerTypeToLaunch = GLOBALTYPE_CLIENTBINNER;
		pClientBinJob->pServerWCmdLineDef->pExtraCmdLine = strdupf("-nameSpaceForClient %s -PatchInfo %s -CmdLineClient hvDisable 1",
			pRepublishInfo->pNameSpace, pPatchInfoTextSuperEsc);
		eaPush(&pClientBinJob->ppJobsIDependOn, strdup("Server binning"));
		eaPush(&pJobGroupDef->ppJobs, pClientBinJob);

		pFlushCacheJob = StructCreate(parse_JobManagerJobDef);
		pFlushCacheJob->pJobName = strdup("Flush ResDB Cache");
		pFlushCacheJob->eType = JOB_REMOTE_CMD;
		pFlushCacheJob->pRemoteCmdDef = StructCreate(parse_JobManagerRemoteCommandDef);
		pFlushCacheJob->pRemoteCmdDef->bSlow = false;
		pFlushCacheJob->pRemoteCmdDef->eTypeForCommand = GLOBALTYPE_UGCDATAMANAGER;
		pFlushCacheJob->pRemoteCmdDef->iIDForCommand = 0;
		pFlushCacheJob->pRemoteCmdDef->iInitialCommandTimeout = 60;
		pFlushCacheJob->pRemoteCmdDef->pCommandString = strdupf("FlushResDBCacheOnEachShard_ForJob \"%s\" \"$JOBNAME$\"", pRepublishInfo->pNameSpace);
		eaPush(&pFlushCacheJob->ppJobsIDependOn, strdup("Client binning"));
		eaPush(&pJobGroupDef->ppJobs, pFlushCacheJob);

		pJobGroupDef->pWhatToDoOnSuccess = CreateEmptyRemoteCommandGroup();
		AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnSuccess, GLOBALTYPE_UGCDATAMANAGER, 0, true,
			"aslUGCDataManager_UpdateUGCProjectPublishStatus %u \"%s\" 1 Republish_CB", pRepublishInfo->iProjID, pRepublishInfo->pNameSpace);
		if (pRepublishInfo->pFilteredListID)
		{
			AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnSuccess, GLOBALTYPE_UGCDATAMANAGER, 0, false,
				"aslUGCDataManager_UpdateUGCProjectRePublishList %s %d 1", pRepublishInfo->pFilteredListID, pRepublishInfo->iIndexInFilteredList);
		}
		

		pJobGroupDef->pWhatToDoOnFailure = CreateEmptyRemoteCommandGroup();
		AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnFailure, GLOBALTYPE_UGCDATAMANAGER, 0, true,
			"aslUGCDataManager_UpdateUGCProjectPublishStatus %u \"%s\" 0 Republish_CB_Failure", pRepublishInfo->iProjID, pRepublishInfo->pNameSpace);
		if (pRepublishInfo->pFilteredListID)
		{	
			AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnFailure, GLOBALTYPE_UGCDATAMANAGER, 0, false,
				"aslUGCDataManager_UpdateUGCProjectRePublishList %s %d 0", pRepublishInfo->pFilteredListID, pRepublishInfo->iIndexInFilteredList);
		}
	

//if there's a non-"real" failure, like the job manager crashing, we want to leave the thing in state "republishing" so our
//periodic code comes through and restarts the republish
//		AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnJobManagerCrash, GLOBALTYPE_UGCDATAMANAGER, 0, true,
//			"aslUGCDataManager_UpdateUGCProjectPublishStatus %u \"%s\" 0", pRepublishInfo->iProjID, pRepublishInfo->pNameSpace);
		
		if (pRepublishInfo->pFilteredListID)
		{
			pJobGroupDef->pWhatToDoOnJobManagerCrash = CreateEmptyRemoteCommandGroup();
			AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnJobManagerCrash, GLOBALTYPE_UGCDATAMANAGER, 0, false,
				"aslUGCDataManager_UpdateUGCProjectRePublishList %s %d 0", pRepublishInfo->pFilteredListID, pRepublishInfo->iIndexInFilteredList);
		}

//if there's a non-"real" failure, like the job manager crashing, we want to leave the thing in state "republishing" so our
//periodic code comes through and restarts the republish
//		pRepublishInfo->pWhatToDoIfPublishJobDoesntStart = CreateEmptyRemoteCommandGroup();
//		AddCommandToRemoteCommandGroup(pRepublishInfo->pWhatToDoIfPublishJobDoesntStart, GLOBALTYPE_UGCDATAMANAGER, 0, true,
//			"aslUGCDataManager_UpdateUGCProjectPublishStatus %u \"%s\" 0", pRepublishInfo->iProjID, pRepublishInfo->pNameSpace);

		if (g_isContinuousBuilder)
		{
			AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnSuccess, GLOBALTYPE_JOBMANAGER, 0, false,
				"ControllerScript_Succeeded_RC");
		}


		RemoteCommand_BeginNewJobGroup(objCreateManagedReturnVal(RepublishBegun_CB, pRepublishInfo), GLOBALTYPE_JOBMANAGER, 0, pJobGroupDef);

		StructDestroy(parse_JobManagerJobGroupDef, pJobGroupDef);
		estrDestroy(&pPatchInfoText);
		estrDestroy(&pPatchInfoTextSuperEsc);
		estrDestroy(&pProjectInfoText);
		estrDestroy(&pProjectInfoTextSuperEsc);
		StructDestroy(parse_DynamicPatchInfo, pPatchInfo);
	} 
	else 
	{
		REP_CB_FAIL("While trying to republish version %s of ugc proj %u, couldnt even begin publish",
				pRepublishInfo->pNameSpace, pRepublishInfo->iProjID);
	}

}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
int aslUGCDataManager_UpdateUGCProjectRePublishList(char *pListID, int iIndex, int iStatus)
{
	UGCProjectFilteredList *pFilteredList = NULL;
	if (sFilteredListsByID)
	{
		stashFindPointer(sFilteredListsByID, pListID, &pFilteredList);
	}

	if (!pFilteredList)
	{
		return 1;
	}

	if (iStatus)
	{
		pFilteredList->republishTracker.iNumRepublishSucceeds++;
	}
	else
	{
		pFilteredList->republishTracker.iNumRepublishFails++;

		if (pFilteredList->ppVersions && iIndex < eaSize(&pFilteredList->ppVersions))
		{
			AddErrorToFilteredList(pFilteredList, "Publish job failed for proj %u namespace %s. Job manager logs should show error",
				pFilteredList->ppVersions[iIndex]->iID, pFilteredList->ppVersions[iIndex]->pNameSpaceName);
		}
	}

	return 1;
}

int iMaxStateChangesAtOnce = 1024;
AUTO_CMD_INT(iMaxStateChangesAtOnce, MaxStateChangesAtOnce);
int iMaxRepublishesAtOnce = 1024;
AUTO_CMD_INT(iMaxRepublishesAtOnce, MaxRepublishesAtOnce);

void StartRepublishNow(ContainerID iID, const char *pNameSpaceName, const char *pListID, int iIndexInList, U32 iRepublishFlags, char *pComment)
{
	UGCProject *pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, iID);
	const UGCProjectVersion* pVersion = NULL;
	RepublishInfo *pInfo;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if (!pProject)
	{
		WARNING_NETOPS_ALERT("UGC_PROJ_VANISHED_BEFORE_REPUB", "UGC project %u can't be republished, it no longer exists", iID);
		return;
	}
	pVersion = UGCProject_GetSpecificVersion( pProject, pNameSpaceName );
	if( !pVersion ) {
		AssertOrAlertWarning( "UGC_PROJ_REPUBLISH_MISSING_VERSION", "UGC project %u was republished with namespace %s, but it no longer exists",
							  iID, pNameSpaceName );
		return;
	}
	

	pInfo = StructCreate(parse_RepublishInfo);
	pInfo->iProjID = iID;
	pInfo->pNameSpace = strdup(pNameSpaceName);
	pInfo->pJobName = strdup(GetUniqueJobGroupName("UGC Republish"));
	pInfo->pFilteredListID = pListID;
	pInfo->iIndexInFilteredList = iIndexInList;
	pInfo->iRepublishFlags = iRepublishFlags;
	pInfo->pProjectInfo = ugcCreateProjectInfo( pProject, pVersion );

	AutoTrans_trRepublish(LoggedTransactions_CreateManagedReturnVal( __FUNCTION__, Republish_CB, pInfo), GetAppGlobalType(), GLOBALTYPE_UGCPROJECT,
		iID, pInfo, pComment);
}


void FilteredListRepublish_TimedCB(TimedCallback *callback, F32 timeSinceLastCallback, char *pAllocedID)
{
	UGCProjectFilteredList *pList;
	int iNumNotYetBegun;
	int iNumOutstanding;
	static char *pCommentToUse = NULL;


	if (!sFilteredListsByID || !stashFindPointer(sFilteredListsByID, pAllocedID, &pList))
	{
		return;
	}

	iNumNotYetBegun = pList->republishTracker.iNumToRepublish - pList->republishTracker.iNumRepublishBegins;
	
	if (!iNumNotYetBegun)
	{
		return;
	}

	iNumOutstanding = pList->republishTracker.iNumRepublishBegins - pList->republishTracker.iNumRepublishesToJobManager;

	estrPrintf(&pCommentToUse, "FilteredList %s, Flags %u: %s", pList->ID, pList->iRepublishFlags, pList->pComment);


	while (iNumNotYetBegun && iNumOutstanding < iMaxRepublishesAtOnce)
	{

		StartRepublishNow(pList->ppVersions[pList->republishTracker.iNumRepublishBegins]->iID, pList->ppVersions[pList->republishTracker.iNumRepublishBegins]->pNameSpaceName, 
			pAllocedID, pList->republishTracker.iNumRepublishBegins, pList->iRepublishFlags, pCommentToUse);

		pList->republishTracker.iNumRepublishBegins++;
		iNumOutstanding++;
		iNumNotYetBegun--;
	}
	

	if (iNumNotYetBegun)
	{
		TimedCallback_Run(FilteredListRepublish_TimedCB, (void*)pAllocedID, 0.1f);
	}
}

void FilteredListStateChange_TimedCB(TimedCallback *callback, F32 timeSinceLastCallback, char *pAllocedID)
{
	UGCProjectFilteredList *pList;
	int iNumNotYetBegun;
	int iNumOutstanding;

	if (!sFilteredListsByID || !stashFindPointer(sFilteredListsByID, pAllocedID, &pList))
	{
		return;
	}

	iNumNotYetBegun = pList->stateChangeTracker.iNumToStateChange - pList->stateChangeTracker.iNumStateChangeBegins;
	
	if (!iNumNotYetBegun)
	{
		return;
	}

	iNumOutstanding = pList->stateChangeTracker.iNumStateChangeBegins - pList->stateChangeTracker.iNumStateChangeFails - pList->stateChangeTracker.iNumStateChangeSucceeds;

	while (iNumNotYetBegun && iNumOutstanding < iMaxStateChangesAtOnce)
	{
		StartStateChange(pList->ppVersions[pList->stateChangeTracker.iNumStateChangeBegins]->iID, pList->ppVersions[pList->stateChangeTracker.iNumStateChangeBegins]->pNameSpaceName, pAllocedID, pList->stateChangeTracker.eStateToChangeTo, pList->pComment,
			pList->stateChangeTracker.bAlsoSetCRC);

		pList->stateChangeTracker.iNumStateChangeBegins++;

		iNumOutstanding++;
		iNumNotYetBegun--;
	}
	

	if (iNumNotYetBegun)
	{
		TimedCallback_Run(FilteredListStateChange_TimedCB, (void*)pAllocedID, 0.1f);
	}

}

static int RepublishFilteredList_Sort( const UGCProjectVersionRef** ppUGCProjectVersionRef1, const UGCProjectVersionRef** ppUGCProjectVersionRef2 )
{
	if((*ppUGCProjectVersionRef1)->iID != (*ppUGCProjectVersionRef2)->iID)
	{
		UGCProject *pUGCProject1 = objGetContainerData(GLOBALTYPE_UGCPROJECT, (*ppUGCProjectVersionRef1)->iID);
		UGCProject *pUGCProject2 = objGetContainerData(GLOBALTYPE_UGCPROJECT, (*ppUGCProjectVersionRef2)->iID);

		if(pUGCProject1->bFlaggedAsCryptic && !pUGCProject2->bFlaggedAsCryptic)
			return -1;
		else if(!pUGCProject1->bFlaggedAsCryptic && pUGCProject2->bFlaggedAsCryptic)
			return 1;

		if(pUGCProject1->pFeatured && !pUGCProject2->pFeatured)
			return -1;
		else if(!pUGCProject1->pFeatured && pUGCProject2->pFeatured)
			return 1;

		if(pUGCProject1->ugcReviews.fAdjustedRatingUsingConfidence > pUGCProject2->ugcReviews.fAdjustedRatingUsingConfidence)
			return -1;
		else if(pUGCProject1->ugcReviews.fAdjustedRatingUsingConfidence < pUGCProject2->ugcReviews.fAdjustedRatingUsingConfidence)
			return 1;
	}

	return 0;
}

AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_ACCESSLEVEL(7);
char *RepublishFilteredList(char *pID, U32 iRegenerate, U32 iBeaconize, ACMD_SENTENCE pComment)
{
	UGCProjectFilteredList *pList;
	int i;
	const char *pAllocedID;
	static char *pCommentToUse = NULL;

	if (!sFilteredListsByID || !stashFindPointer(sFilteredListsByID, pID, &pList))
	{
		return "Couldn't find filtered list";
	}

	if (pList->republishTracker.iNumToRepublish)
	{
		return "This list has already been republished... what craziness are you up to?";
	}

	estrCopy2(&pList->pComment, pComment);
	pList->iRepublishFlags = 0;
	if(iRegenerate)
		pList->iRepublishFlags |= REPUBLISH_FLAG_PROJECT_REGENERATE;
	if(iBeaconize)
		pList->iRepublishFlags |= REPUBLISH_FLAG_MAP_BEACONIZE;

	estrPrintf(&pCommentToUse, "FilteredList %s, Flags %u: %s", pList->ID, pList->iRepublishFlags, pList->pComment);

	pAllocedID = allocAddString(pID);

	pList->republishTracker.iNumToRepublish = eaSize(&pList->ppVersions);

	eaQSort(pList->ppVersions, RepublishFilteredList_Sort);

	for (i=0; i < eaSize(&pList->ppVersions) && i < iMaxRepublishesAtOnce; i++)
	{
		StartRepublishNow(pList->ppVersions[i]->iID, pList->ppVersions[i]->pNameSpaceName, 
			pAllocedID, i, pList->iRepublishFlags, pCommentToUse);

		pList->republishTracker.iNumRepublishBegins++;
	}
	
	if (pList->republishTracker.iNumRepublishBegins < pList->republishTracker.iNumToRepublish)
	{
		TimedCallback_Run(FilteredListRepublish_TimedCB, (void*)pAllocedID, 0.1f);
	}

	return REPUBLISH_PROCESS_BEGUN;
}



AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_ACCESSLEVEL(7);
char *SetStateForFilteredList(char *pID, char *pState, int iAlsoSetCRC, ACMD_SENTENCE pComment)
{
	UGCProjectFilteredList *pList;
	int i;
	const char *pAllocedID;

	if (!sFilteredListsByID || !stashFindPointer(sFilteredListsByID, pID, &pList))
	{
		return "Couldn't find filtered list";
	}

	if (pList->stateChangeTracker.iNumToStateChange)
	{
		return "This list has already been state changed... what craziness are you up to?";
	}

	pList->stateChangeTracker.eStateToChangeTo = StaticDefineIntGetInt(UGCProjectVersionStateEnum, pState);
	if (pList->stateChangeTracker.eStateToChangeTo == -1)
	{
		return "unknown state...";
	}

	if (iAlsoSetCRC && pList->stateChangeTracker.eStateToChangeTo != UGC_PUBLISHED)
	{
		return "Can't set CRC when setting to a state other than published;";
	}

	pList->stateChangeTracker.bAlsoSetCRC = iAlsoSetCRC;

	estrCopy2(&pList->pComment, pComment);

	pAllocedID = allocAddString(pID);

	pList->stateChangeTracker.iNumToStateChange = eaSize(&pList->ppVersions);

	for (i=0; i < eaSize(&pList->ppVersions) && i < iMaxStateChangesAtOnce; i++)
	{
		StartStateChange(pList->ppVersions[i]->iID, pList->ppVersions[i]->pNameSpaceName, 
			pAllocedID, pList->stateChangeTracker.eStateToChangeTo, pComment, pList->stateChangeTracker.bAlsoSetCRC);

		pList->stateChangeTracker.iNumStateChangeBegins++;
	}
	
	if (pList->stateChangeTracker.iNumStateChangeBegins < pList->stateChangeTracker.iNumToStateChange)
	{
		TimedCallback_Run(FilteredListStateChange_TimedCB, (void*)pAllocedID, 0.1f);
	}

	return "StateChange process begun";
}

AUTO_STRUCT;
typedef struct RepublishLaterCache
{
	ContainerID iID;
	const char *pNameSpaceName;
	const char *pListID;
	int iIndexInList;
	U32 iRepublishFlags;

	int iHotnessSortingValue; AST(DEF(-1))
} RepublishLaterCache;

static StashTable sPendingRepublishesByID = NULL;

//anything on the hot list gets its hot list index, everything else gets minutes since it's been played + 1000000
void CalcHotnessSortingValue(RepublishLaterCache *pCache)
{
	UGCProject *pProject;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if (pCache->iHotnessSortingValue >= 0)
	{
		return;
	}

	pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, pCache->iID);
	if (!pProject || !pProject->iMostRecentPlayedTime)
	{
		pCache->iHotnessSortingValue = INT_MAX;
		return;
	}

	pCache->iHotnessSortingValue = (timeSecondsSince2000() - pProject->iMostRecentPlayedTime)/60 + 1000000;
}

static int SortCachesByHotness(const RepublishLaterCache **ppCache1, const RepublishLaterCache **ppCache2)
{
	const RepublishLaterCache *pCache1 = *ppCache1;
	const RepublishLaterCache *pCache2 = *ppCache2;

	return pCache2->iHotnessSortingValue - pCache1->iHotnessSortingValue;
}





/*
AUTO_COMMAND ACMD_CATEGORY(Debug);
void RepublishProjectsFromFilteredlist(void)
{
	int i;

	if (spFilteredList)
	{
		for (i=0; i < eaSize(&spFilteredList->ppMostRecentPublishedVersions); i++)
		{
			RepublishInfo *pInfo = StructCreate(parse_RepublishInfo);
			pInfo->iProjID = spFilteredList->ppMostRecentPublishedVersions[i]->iID;
			pInfo->pNameSpace = strdup(spFilteredList->ppMostRecentPublishedVersions[i]->pNameSpaceName);
			pInfo->pJobName = strdup(GetUniqueJobGroupName("UGC Republish"));

			AutoTrans_trRepublish(objCreateManagedReturnVal(Republish_CB, pInfo), GetAppGlobalType(), GLOBALTYPE_UGCPROJECT,
				pInfo->iProjID, pInfo);
		}
	}
}
*/


AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ppprojectversions");
enumTransactionOutcome trUGCProjectDebugSetAllPublished(ATR_ARGS, NOCONST(UGCProject) *pUGCProject)
{
	int i;

	for (i=0; i < eaSize(&pUGCProject->ppProjectVersions); i++)
	{
		ugcProjectSetVersionState(pUGCProject->ppProjectVersions[i], UGC_PUBLISHED, "trUGCProjectDebugSetAllPublished");
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_CATEGORY(UGC);
void DebugSetAllPublished(void)
{
	ContainerIterator iter = {0};
	UGCProject *pProject;

	objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECT, &iter);

	while ((pProject = objGetNextObjectFromIterator(&iter)))
	{
		AutoTrans_trUGCProjectDebugSetAllPublished(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".*");
enumTransactionOutcome trTouchProject(ATR_ARGS, ATR_ALLOW_FULL_LOCK NOCONST(UGCProject) *pUGCProject)
{	
	NOCONST(UGCProjectVersion) *pVersion = CONTAINER_NOCONST(UGCProjectVersion, UGCProject_GetMostRecentPublishedVersion(CONTAINER_RECONST(UGCProject, pUGCProject)));
	
	pUGCProject->iIdOnPreviousShard++;

	if (pVersion)
	{
		if (!(pVersion->pDescription && pVersion->pDescription[0]))
		{
			pVersion->pDescription = strdup("Lovely fake description");
		}
		else
		{
			if (pVersion->pDescription[0] == 'Z')
			{
				pVersion->pDescription[0] = 'Q';
			}
			else
			{
				pVersion->pDescription[0] = 'Z';
			}
		}

		if (pVersion->pName[0] == 'z')
		{
			pVersion->pName[0] = 'Q';
		}
		else
		{
			pVersion->pName[0] = 'Z';
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_COMMAND ACMD_CATEGORY(UGC);
void DebugTouchAllProjects(void)
{
ContainerIterator iter = {0};
	UGCProject *pProject;

	objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECT, &iter);

	while ((pProject = objGetNextObjectFromIterator(&iter)))
	{
		AutoTrans_trTouchProject(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id);
	}
}


AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ideletiontime");
enumTransactionOutcome trUGCProjectDebugUnDelete(ATR_ARGS, NOCONST(UGCProject) *pUGCProject)
{
	pUGCProject->iDeletionTime = 0;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_ACCESSLEVEL(4);
void UGCProjectUndelete(ContainerID ugcProject)
{
	AutoTrans_trUGCProjectDebugUnDelete(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, ugcProject);
}

AUTO_COMMAND ACMD_CATEGORY(UGC);
void DebugUnDeleteAll(void)
{
	ContainerIterator iter = {0};
	UGCProject *pProject;

	objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECT, &iter);

	while ((pProject = objGetNextObjectFromIterator(&iter)))
	{
		AutoTrans_trUGCProjectDebugUnDelete(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ideletiontime");
enumTransactionOutcome trUGCProjectDebugDelete(ATR_ARGS, NOCONST(UGCProject) *pUGCProject)
{
	pUGCProject->iDeletionTime = timeSecondsSince2000();

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_CATEGORY(UGC);
void DebugDeleteAll(void)
{
	ContainerIterator iter = {0};
	UGCProject *pProject;
	UGCProjectSeries *pSeries;

	objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECT, &iter);
	while ((pProject = objGetNextObjectFromIterator(&iter)))
	{
		AutoTrans_trUGCProjectDebugDelete(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pProject->id);
	}

	objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECTSERIES, &iter);
	while ((pSeries = objGetNextObjectFromIterator(&iter)))
	{
		AutoTrans_trUGCProjectSeries_Destroy(NULL, GLOBALTYPE_UGCDATAMANAGER,
			GLOBALTYPE_UGCPROJECTSERIES, pSeries->id,
			GLOBALTYPE_UGCPROJECT, NULL );
	}
	
}

void StartWithdraw(ContainerID iID, const char *pNameSpaceName, const char *pListID /*POOL_STRING*/, char *pComment)
{
	WithdrawCache *pWithdrawCache = StructCreate(parse_WithdrawCache);
	pWithdrawCache->iProjID = iID;
	pWithdrawCache->pNameSpace = strdup(pNameSpaceName);
	pWithdrawCache->pFilteredListID = pListID;

	AutoTrans_trWithdrawVersion(objCreateManagedReturnVal(Withdraw_CB, pWithdrawCache), GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, iID, 
		pNameSpaceName, pComment);
}



void StartStateChange(ContainerID iID, const char *pNameSpaceName, const char *pListID /*POOL_STRING*/, UGCProjectVersionState eNewState, char *pComment, bool bAlsoSetCRC)
{
	StateChangeCache *pStateChangeCache = StructCreate(parse_StateChangeCache);
	static char *pFullComment = NULL;

	estrPrintf(&pFullComment, "Filtered list %s: %s%s", pListID, pComment, bAlsoSetCRC ? " (also setting CRC)" : "");
	pStateChangeCache->iProjID = iID;
	pStateChangeCache->pNameSpace = strdup(pNameSpaceName);
	pStateChangeCache->pFilteredListID = pListID;

	AutoTrans_trStateChangeVersion(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, StateChange_CB, pStateChangeCache), GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, iID, 
		pNameSpaceName, (int) eNewState, pFullComment, bAlsoSetCRC);
}

static void FillAllPlayableNameSpaces(UGCPlayableNameSpaces *pUGCPlayableNameSpaces)
{
	ContainerIterator it;
	UGCProject *pUGCProject = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECT, &it);
	while(pUGCProject = objGetNextObjectFromIterator(&it))
	{
		FillInPlayableDataForProject(pUGCPlayableNameSpaces, pUGCProject, /*bIncludeButFlagUnplayable=*/false);
	}
}

// returns whether or not all MapManagers were in a state to receive this data, yet.
bool aslUGCDataManager_SendAllPlayableNameSpacesToAllMapManagers(UGCPlayableNameSpaces **ppUGCAllPlayableNameSpaces)
{
	UGCPlayableNameSpaces *pUGCAllPlayableNameSpaces = ppUGCAllPlayableNameSpaces ? *ppUGCAllPlayableNameSpaces : NULL;
	bool done = false;

	if(!pUGCAllPlayableNameSpaces)
	{
		pUGCAllPlayableNameSpaces = StructCreate(parse_UGCPlayableNameSpaces);

		FillAllPlayableNameSpaces(pUGCAllPlayableNameSpaces);
	}

	done = SendPlayableNameSpacesToAllMapManagers(pUGCAllPlayableNameSpaces, /*bReplace=*/true);

	if(!ppUGCAllPlayableNameSpaces)
		StructDestroy(parse_UGCPlayableNameSpaces, pUGCAllPlayableNameSpaces);

	return done;
}

// This function should be called over and over again. Once done, it will stop doing any actual work.
void aslUGCDataManager_SendAllPlayableNameSpacesToAllMapManagersAtStartup(void)
{
	static UGCPlayableNameSpaces *pUGCAllPlayableNameSpaces = NULL;
	static bool s_bAllPlayableNameSpacesSentToAllMapManagersAtStartup = false;

	if(!s_bAllPlayableNameSpacesSentToAllMapManagersAtStartup)
	{
		s_bAllPlayableNameSpacesSentToAllMapManagersAtStartup = aslUGCDataManager_SendAllPlayableNameSpacesToAllMapManagers(&pUGCAllPlayableNameSpaces);

		if(s_bAllPlayableNameSpacesSentToAllMapManagersAtStartup)
			StructDestroy(parse_UGCPlayableNameSpaces, pUGCAllPlayableNameSpaces);
	}
}

AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_ACCESSLEVEL(7);
char *ResendAllPlayableNameSpacesToAllMapManagers()
{
	if(aslUGCDataManager_SendAllPlayableNameSpacesToAllMapManagers(NULL))
		return "All playable namespaces sent to all MapManagers";
	else
		return "Failed to send all playable namespaces to all MapManagers";
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void aslUGCDataManager_GetNameSpacesPlayable(const char *pcShardName)
{
	int iAlreadySent = false;
	if(stashFindInt(stashShardsAlreadySentPlayableNameSpaces, pcShardName, &iAlreadySent) && iAlreadySent)
	{
		// Presumably, a MapManager is requesting playable namespaces because it went down for some reason while this UGCDataManager remained up.
		// Therefore, it needs the list again.
		UGCPlayableNameSpaces ugcPlayableNameSpaces;
		StructInit(parse_UGCPlayableNameSpaces, &ugcPlayableNameSpaces);

		FillAllPlayableNameSpaces(&ugcPlayableNameSpaces);

		RemoteCommand_Intershard_aslMapManager_UpdateNameSpacesPlayable(pcShardName, GLOBALTYPE_MAPMANAGER, 0, &ugcPlayableNameSpaces, /*bReplace=*/true);

		StructReset(parse_UGCPlayableNameSpaces, &ugcPlayableNameSpaces);
	}
}

//how frequently to query to see if the namelist-getting is done
#define NAMELISTDONE_DELAY_TIME 60

//look at n namespaces every m seconds
#define NAMELIST_PROCESSING_CHUNKSIZE 100
#define NAMELIST_PROCESSING_DELAY 60


//if the modtime is fewer than this many seconds ago, then don't delete it no matter what
//(to avoid corner cases where a namespace is being created just as we query it)
#define MODTIME_GRACEPERIOD (24 * 3600)

static char **sppNameSpacesForDailyProcessing = NULL;
static U32 *pModTimes = NULL;

static void aslDaily_Processing(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	U32 iCurTime = timeSecondsSince2000();
	int i;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	for (i=0; i < NAMELIST_PROCESSING_CHUNKSIZE; i++)
	{
		char *pName = eaPop(&sppNameSpacesForDailyProcessing);
		int iHowOld = iCurTime - timePatchFileTimeToSecondsSince2000(ea32Pop(&pModTimes));
		ContainerID iID;
		UGCProject *pProj;
		const UGCProjectVersion *pVersion;
		bool bDelete = false;
		bool bValidate = false;

		if (eaSize(&sppNameSpacesForDailyProcessing) % 1000 == 0)
		{
			log_printf(LOG_NSTHREADING, "%d namespaces remain to process", eaSize(&sppNameSpacesForDailyProcessing));
		}

		if (!pName)
		{
			log_printf(LOG_NSTHREADING, "No more namespaces to process");
			break;
		}

		if (iHowOld < MODTIME_GRACEPERIOD)
		{
			//do nothing
		}
		else
		{

			iID = UGCProject_GetContainerIDFromUGCNamespace(pName);
			if (!iID)
			{
				CRITICAL_NETOPS_ALERT("UGC_CORRUPT_NAMESPACE", "UGC patcher reports corrupt/unknown namespace %s", pName);
			}
			else
			{
				pProj = objGetContainerData(GLOBALTYPE_UGCPROJECT, iID);
				if (!pProj)
				{
					log_printf(LOG_UGC, "Will request deletion of namespace %s because its project no longer exists", pName);
					bDelete = true;
				}
				else
				{
					pVersion = UGCProject_GetSpecificVersion(pProj, pName);
					if (!pVersion)
					{
						log_printf(LOG_UGC, "Will request deletion of namespace %s because its version no longer exists", pName);

						bDelete = true;
					}
					else
					{
						switch (ugcProjectGetVersionStateConst(pVersion))
						{
						case UGC_PUBLISH_FAILED:
							bDelete = true;
							log_printf(LOG_UGC, "Will request deletion of namespace %s because its publish failed", pName);

							break;
						case UGC_UNPLAYABLE:
							bDelete = true;
							log_printf(LOG_UGC, "Will request deletion of namespace %s because it has been marked unplayable", pName);

							break;

						case UGC_PUBLISHED:
							if (!pVersion->bPublishValidated)
							{
								bValidate = true;
								log_printf(LOG_UGC, "Will requeset validation of namespace %s", pName);
							}
							break;

						case UGC_NEEDS_REPUBLISHING:
						case UGC_NEEDS_UNPLAYABLE:
							if (pVersion->iModTime < timeSecondsSince2000() - 24 * 60 * 60)
							{
								TriggerAutoGroupingAlert("UGC_PROJECT_NEEDS", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 600, "Namespace %s, project %u has been NEEDS_REPUBLISHING or NEEDS_UNPLAYABLE for > 24 hours, it should get changed to something else ASAP",
									pName, pProj->id);
							}
						}
					}
				}
			}

			if (bDelete)
			{
				aslAddNamespaceToBeDeletedNextQuery(pName);
			}
			if (bValidate)
			{
				//aslAddNamespaceToBeValidatedNextQuery(pName);
				// Make sure that this eventually calls the auto transaction trUGCProjectSetPublishValidated.
			}
		}

	
		free(pName);
	}

	if (eaSize(&sppNameSpacesForDailyProcessing))
	{
		TimedCallback_Run(aslDaily_Processing, NULL, NAMELIST_PROCESSING_DELAY);
	}
}

static void aslDaily_CheckForNameListDoneAndBeginProcessing(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (aslGetNamespaceListDone(&sppNameSpacesForDailyProcessing, &pModTimes))
	{	
		U32 iTimeEstimate;

		log_printf(LOG_NSTHREADING, "aslDaily_CheckForNameListDoneAndBeginProcessing has gotten a true result back from aslGetNamespaceListDone(), about to begin processing %d namespaces, %d every %d seconds",
			eaSize(&sppNameSpacesForDailyProcessing), NAMELIST_PROCESSING_CHUNKSIZE, NAMELIST_PROCESSING_DELAY);

		aslNamespaceListCleanup(true);

		iTimeEstimate = eaSize(&sppNameSpacesForDailyProcessing) / NAMELIST_PROCESSING_CHUNKSIZE * NAMELIST_PROCESSING_DELAY;
		if (iTimeEstimate > 23 * 3600)
		{
			CRITICAL_NETOPS_ALERT("UGC_SLOW_DAILY_CHECK", "While verifying ugc namespaces from the ugc patcher, we have %d namespaces. Processing %d every %d seconds may take > 24 hours, interfering with tomorrow's check. This may or may not be a big deal",
				eaSize(&sppNameSpacesForDailyProcessing), NAMELIST_PROCESSING_CHUNKSIZE, NAMELIST_PROCESSING_DELAY);
		}

		TimedCallback_Run(aslDaily_Processing, NULL, NAMELIST_PROCESSING_DELAY);
	}
	else
	{
		TimedCallback_Run(aslDaily_CheckForNameListDoneAndBeginProcessing, NULL, NAMELISTDONE_DELAY_TIME);
	}
}

static void aslDailyUGCProjectUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	TimedCallback_RunAtTimeOfDay(aslDailyUGCProjectUpdate, NULL, TIME_OF_DAY_FOR_UGCPROJ_DAILY_UPDATE);

	if (eaSize(&sppNameSpacesForDailyProcessing))
	{
		log_printf(LOG_NSTHREADING, "aslDailyUGCProjectUpdate called, but there are still %d namespaces to process from yesterday", eaSize(&sppNameSpacesForDailyProcessing));

		return;
	}

	if (aslBeginBackgroundThreadNamespaceListQuery(UGC_GetShardSpecificNSPrefix(NULL)))
	{
		log_printf(LOG_NSTHREADING, "aslDailyUGCProjectUpdate called, background thread begun, beginning checking for completion");

		TimedCallback_Run(aslDaily_CheckForNameListDoneAndBeginProcessing, NULL, NAMELISTDONE_DELAY_TIME);
	}
	else
	{
		log_printf(LOG_NSTHREADING, "aslDailyUGCProjectUpdate called, background thread couldn't be begun, presumably still running from yesterday?");
		CRITICAL_NETOPS_ALERT("UGC_CANT_BEGIN_NAMESPACE_QUERY", "While doing daily ugc project update, couldn't initiate namespace query, presumably because yesterday's is taking > 24 hours. This is very bad");
	}
}

AUTO_COMMAND;
void DailyUGCTest(void)
{
	aslDailyUGCProjectUpdate(NULL, 0, NULL);
}

char *pFirstNames[] = 
{
	"Bob",
	"Joe",
	"Dave",
	"Anne",
	"Alice",
	"Fred",
	"Bubba",
	"Mike",
	"Mark",
	"Ringo",
	"Rajeem",
	"Ralph",
	"Bart",
	"Homer",
	"Lisa",
	"Marge",
	"Susur",
	"Marcus",
	"Flar",
	"Rand",
	"Perrin",
	"Matt",
};

char *pLastNames[] = 
{
	"Smith",
	"Johnson",
	"Werner",
	"LeMat",
	"Yedwab",
	"Dangelo",
	"Weinstein",
	"Mitchell",
	"Fenton",
	"Mapolis",
	"Duguid",
	"Esser",
	"Wiggum",
	"Wiggin",
	"Simpson",
	"Hutz",
	"McClure",
	"Bouvier",
	"VanHouten",
	"Muntz",
	"Jones",
};

char *pKeyWords[] = 
{
	"Borg",
	"Klingon",
	"Vulcan",
	"Dragon",
	"Banana",
	"Enterprise",
	"Khan",
	"Spock",
	"Kirk",
	"Picard",
	"Crusher",
	"Yoda",
	"Death Star",
	"Death Cookie",
	"Hardware Wars",
	"Big fat scottie from Star Trek",
	"Warp 9.5",
	"Tiberius",
	"Deep Space 9",
};

#define RANDOM_STRING(arrayName) (arrayName[randomIntRange(0, (ARRAY_SIZE(arrayName)) - 1)])


void CreateManyProjectsForSearchTest_Create_CB(TransactionReturnVal *pReturn, void *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		printf("Creation failed: %s\n", GetTransactionFailureString(pReturn));
	}
	else
	{
		U32 iID = atoi(pReturn->pBaseReturnVals[0].returnString);
		int iHotness = (intptr_t)pData;
		int i;

		for (i = 0; i < iHotness; i++)
		{
			aslUGCDataManager_ReportUGCProjectWasPlayedForWhatsHot(iID);
		}

		AutoTrans_trUGCProjectRebuildRatingBucketsIfNecessary(NULL, GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, iID);
	}
}

int DebugTest_GetIDFromAuthorName(char *pAuthorName)
{
	static char **sppAuthorNames = NULL;
	const char *pName = allocAddString(pAuthorName);
	int iIndex = eaFind(&sppAuthorNames, pName);
	if (iIndex != -1)
	{
		return iIndex + 1;
	}

	eaPush(&sppAuthorNames, (char*)pName);
	return eaSize(&sppAuthorNames);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ugcreviews.Fadjustedratingusingconfidence, .Ugcreviews.Faveragerating, .Ugcreviews.Pinumratings");
enumTransactionOutcome trComputeAdjustedRatingForProject(ATR_ARGS, NOCONST(UGCProject)* pUGCProject)
{
	pUGCProject->ugcReviews.fAdjustedRatingUsingConfidence = ugcReviews_ComputeAdjustedRatingUsingConfidence(&pUGCProject->ugcReviews);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_CATEGORY(ugc);
void ugcComputeAdjustedRatingsForAllProjects(void)
{
	ContainerIterator it = { 0 };
	UGCProject* pUGCProject = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to create a UGC container!");

	objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECT, &it);
	while(pUGCProject = objGetNextObjectFromIterator(&it))
	{
		AutoTrans_trComputeAdjustedRatingForProject(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, pUGCProject->id);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProjectSeries, ".Ugcreviews.Fadjustedratingusingconfidence, .Ugcreviews.Faveragerating, .Ugcreviews.Pinumratings");
enumTransactionOutcome trComputeAdjustedRatingForProjectSeries(ATR_ARGS, NOCONST(UGCProjectSeries)* pUGCProjectSeries)
{
	pUGCProjectSeries->ugcReviews.fAdjustedRatingUsingConfidence = ugcReviews_ComputeAdjustedRatingUsingConfidence(&pUGCProjectSeries->ugcReviews);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_CATEGORY(ugc);
void ugcComputeAdjustedRatingsForAllProjectSeries(void)
{
	ContainerIterator it = { 0 };
	UGCProjectSeries* pUGCProjectSeries = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to create a UGC container!");

	objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECTSERIES, &it);
	while(pUGCProjectSeries = objGetNextObjectFromIterator(&it))
	{
		AutoTrans_trComputeAdjustedRatingForProjectSeries(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECTSERIES, pUGCProjectSeries->id);
	}
}

AUTO_COMMAND ACMD_CATEGORY(ugc);
void ugcComputeAdjustedRatingsForAllContent(void)
{
	ugcComputeAdjustedRatingsForAllProjects();
	ugcComputeAdjustedRatingsForAllProjectSeries();
}

AUTO_COMMAND ACMD_CATEGORY(ugc);
void CreateManyProjectsForSearchTest(void)
{
	int i;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to create a UGC container!");

	for (i = 0 ; i < 1000; i++)
	{
		NOCONST(UGCProject) *pUgcProject;
		NOCONST(UGCProjectVersion) *pVersion;
		char *pAuthorName = NULL;
		char *pTitle = NULL;
		char *pDescription = NULL;
		S32 iRatingMin = randomIntRange(0, 4);
		S32 iRatingMax = randomIntRange(iRatingMin + 1, 5);
		int iNumReviews = 0;

		int iHotness = randomIntRange(0,500);

		int iDaysOld = randomIntRange(0,100);
		Language lang = randomIntRange( 1, LANGUAGE_MAX - 1 );

		bool bUnReviewed;

		if (randomPositiveF32() < 0.1f)
		{
			bUnReviewed = true;
			iHotness = 0;
			iDaysOld = 0;
		}
		else
		{
			bUnReviewed = false;
		}

		pUgcProject = StructCreateNoConst(parse_UGCProject);

		estrPrintf(&pAuthorName, "%s %s", RANDOM_STRING(pFirstNames), RANDOM_STRING(pLastNames));

		estrPrintf(&pTitle, "%i %s %s", i, RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords));

		if (bUnReviewed)
		{
			estrPrintf(&pDescription, "UNREVIEWED, Lang: %s\nHere are some keywords: %s %s %s %s %s",
					   StaticDefineIntRevLookup( LanguageEnum, lang ), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords));
		}
		else
		{
			iNumReviews = randomIntRange(0, 100);
			estrPrintf(&pDescription, "Reviews: %d, Rating: %d - %d stars, Lang: %s, Hotness %d, Days old %d\nHere are some keywords: %s %s %s %s %s",
					   iNumReviews, iRatingMin, iRatingMax, StaticDefineIntRevLookup( LanguageEnum, lang ), iHotness, iDaysOld, RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords));
		}

		pUgcProject->iOwnerAccountID = DebugTest_GetIDFromAuthorName(pAuthorName);
		pUgcProject->iCreationTime = timeSecondsSince2000();
		UGCProject_trh_SetOwnerAccountName(ATR_EMPTY_ARGS, pUgcProject, pAuthorName);
		pUgcProject->pPublishedVersionName = strdup(pTitle);
		pUgcProject->bAuthorAllowsFeatured = true;

		if (!bUnReviewed)
		{
			int reviewIt;
			
			// this is here so that ugcReviews_ComputeAdjustedRatingUsingConfidence below works... even though we will rebuild the NumRatings buckets in the callback
			eaiPush(&pUgcProject->ugcReviews.piNumRatings, iNumReviews);

			pUgcProject->ugcReviews.iTimeBecameReviewed = timeSecondsSince2000() - 100 - iDaysOld * 24 * 60 * 60;

			for( reviewIt = 0; reviewIt != iNumReviews; ++reviewIt ) {
				NOCONST(UGCSingleReview)* pReview = StructCreateNoConst(parse_UGCSingleReview);
				char* estr = NULL;
				
				estrPrintf(&estr, "Review %d -- %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
						   reviewIt,
						   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords),
						   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords),
						   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords),
						   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords),
						   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords),
						   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords));
				pReview->pComment = StructAllocString(estr);
				pReview->fRating = randomIntRange(iRatingMin, iRatingMax) / 5.0f;
				pReview->iReviewerAccountID = reviewIt;

				pUgcProject->ugcReviews.fRatingSum += pReview->fRating;

				estrPrintf(&estr, "%s %s", RANDOM_STRING(pFirstNames), RANDOM_STRING(pLastNames));
				pReview->pReviewerAccountName = StructAllocString(estr);
				
				estrDestroy(&estr);

				eaPush(&pUgcProject->ugcReviews.ppReviews, pReview);
			}

			pUgcProject->ugcReviews.fAverageRating = pUgcProject->ugcReviews.fRatingSum / (F32)iNumReviews;
			pUgcProject->ugcReviews.fAdjustedRatingUsingConfidence = ugcReviews_ComputeAdjustedRatingUsingConfidence(&pUgcProject->ugcReviews);
		}

		pUgcProject->ugcStats.durationStats.iAverageDurationInMinutes_IgnoreOutliers = randomPositiveF32() * 50 + 10;
		pUgcProject->ugcStats.iAverageDurationInMinutes_UsingMaps = pUgcProject->ugcStats.durationStats.iAverageDurationInMinutes_IgnoreOutliers;

		pUgcProject->bTestOnly = true;

		pVersion = StructCreateNoConst(parse_UGCProjectVersion);
		ugcProjectSetVersionState(pVersion, UGC_PUBLISHED, __FUNCTION__);
		pVersion->pName = strdup(pTitle);
		pVersion->pDescription = strdup(pDescription);
		pVersion->eLanguage = lang;
		pVersion->pRestrictions = StructCreateNoConst( parse_UGCProjectVersionRestrictionProperties );
		
		if( stricmp( GetProductName(), "Night" ) == 0 ) {
			pVersion->pRestrictions->iMinLevel = pVersion->pRestrictions->iMaxLevel = randomIntRange( 1, 10 );
		}

		SSSTree_InternalizeString(&pUgcProject->pPublicName_ForSearching, pVersion->pName);
		estrPrintf(&pVersion->pNameSpace, "ugc_ThisIsAtest");
			
		eaPush(&pUgcProject->ppProjectVersions, pVersion);

		objRequestContainerCreate(objCreateManagedReturnVal(CreateManyProjectsForSearchTest_Create_CB, (void*)(intptr_t)iHotness), GLOBALTYPE_UGCPROJECT, pUgcProject, GetAppGlobalType(), GetAppGlobalID());
		
		StructDestroyNoConst(parse_UGCProject, pUgcProject);


		estrDestroy(&pAuthorName);
		estrDestroy(&pTitle);
		estrDestroy(&pDescription);

	}
}

AUTO_TRANSACTION ATR_LOCKS(pUGCProject, ".ugcReviews.ppReviews, .ugcReviews.piNumRatings");
enumTransactionOutcome trAddManyReviewsForSearchTest(ATR_ARGS, NOCONST(UGCProject)* pUGCProject, NON_CONTAINER UGCProjectReviews* pNewReviews)
{
	eaPushStructsDeConst(&pUGCProject->ugcReviews.ppReviews, &pNewReviews->ppReviews, parse_UGCSingleReview);
	eaiClear(&pUGCProject->ugcReviews.piNumRatings);
	eaiPush(&pUGCProject->ugcReviews.piNumRatings, eaSize(&pUGCProject->ugcReviews.ppReviews));
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_CATEGORY(ugc);
void AddManyReviewsForSearchTest(void)
{
	ContainerIterator it = { 0 };
	UGCProject* ugcProject = NULL;
	
	objInitContainerIteratorFromType( GLOBALTYPE_UGCPROJECT, &it );
	while( ugcProject = objGetNextObjectFromIterator( &it )) {
		NOCONST(UGCProjectReviews)* pNewReviews = StructCreateNoConst( parse_UGCProjectReviews );
		int reviewIt;
		for( reviewIt = 0; reviewIt != 50; ++reviewIt ) {
			NOCONST(UGCSingleReview)* pReview = StructCreateNoConst(parse_UGCSingleReview);
			char* estr = NULL;
				
			estrPrintf(&estr, "Review %d -- %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
					   reviewIt,
					   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords),
					   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords),
					   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords),
					   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords),
					   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords),
					   RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords), RANDOM_STRING(pKeyWords));
			pReview->pComment = StructAllocString(estr);
			pReview->iReviewerAccountID = reviewIt;
			pReview->fRating = randomPositiveF32();

			estrPrintf(&estr, "%s %s", RANDOM_STRING(pFirstNames), RANDOM_STRING(pLastNames));
			pReview->pReviewerAccountName = StructAllocString(estr);
				
			estrDestroy(&estr);

			eaPush(&pNewReviews->ppReviews, pReview);
		}

		AutoTrans_trAddManyReviewsForSearchTest(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECT, ugcProject->id, CONTAINER_RECONST(UGCProjectReviews, pNewReviews));
		StructDestroyNoConstSafe(parse_UGCProjectReviews, &pNewReviews);
	}
}


static U32 siNextEditQueueCookie = 1;

static int sEditQueueCookieExpirationTime = 15;
AUTO_CMD_INT(sEditQueueCookieExpirationTime, EditQueueCookieExpirationTime);

AUTO_STRUCT;
typedef struct WaitingForCookieCache
{
	ContainerID iLoginServerContainerID;
	U32 iLoginServerCookie;
} WaitingForCookieCache;

static WaitingForCookieCache **sppEditQueueWaitingList = NULL;

AUTO_STRUCT;
typedef struct CurrentlyActiveEditQueueCookie
{
	U32 iCookie;
	U32 iExpirationTime;
} CurrentlyActiveEditQueueCookie;

CurrentlyActiveEditQueueCookie **sppCurEditQueueCookies = NULL;

CurrentlyActiveEditQueueCookie *GetNewActiveCookie(void)
{
	CurrentlyActiveEditQueueCookie *pCookie = StructCreate(parse_CurrentlyActiveEditQueueCookie);
	pCookie->iCookie = siNextEditQueueCookie;
	siNextEditQueueCookie++;
	if (siNextEditQueueCookie == U32_MAX)
	{
		siNextEditQueueCookie = 1;
	}

	pCookie->iExpirationTime = timeSecondsSince2000() + sEditQueueCookieExpirationTime;
	eaPush(&sppCurEditQueueCookies, pCookie);
	return pCookie;
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void EnterUGCEditQueue(ContainerID iLoginServerContainerID, U32 iLoginServerCookie)
{
	GameServerList *pGameServerList = NewMapTransfer_FindEditingGameServerList();
	int iNumUGCEditServers = pGameServerList ? eaSize(&pGameServerList->ppGameServers) : 0;

	if (iNumUGCEditServers + eaSize(&sppCurEditQueueCookies) < gMapManagerConfig.iMaxEditServers)
	{
		CurrentlyActiveEditQueueCookie *pCookie = GetNewActiveCookie();
		RemoteCommand_HereIsEditQueueCookie(GLOBALTYPE_LOGINSERVER, iLoginServerContainerID, iLoginServerCookie, pCookie->iCookie);
	}
	else
	{
		WaitingForCookieCache *pCache = StructCreate(parse_WaitingForCookieCache);
		pCache->iLoginServerContainerID = iLoginServerContainerID;
		pCache->iLoginServerCookie = iLoginServerCookie;
		eaPush(&sppEditQueueWaitingList, pCache);
		RemoteCommand_EditQueuePlaceUpdate(GLOBALTYPE_LOGINSERVER, iLoginServerContainerID, iLoginServerCookie, eaSize(&sppEditQueueWaitingList) - 1);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void CancelEditQueueByQueueCookie(U32 iEditQueueCookie)
{
	int i;

	if (siNextEditQueueCookie == U32_MAX) {
		return;
	}

	for (i=0; i < eaSize(&sppCurEditQueueCookies); i++)
	{
		if (sppCurEditQueueCookies[i]->iCookie == iEditQueueCookie)
		{
			StructDestroy(parse_CurrentlyActiveEditQueueCookie, sppCurEditQueueCookies[i]);
			eaRemoveFast(&sppCurEditQueueCookies, i);
			return;
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void CancelEditQueueByLoginCookie(ContainerID iLoginServerContainerID, U32 iLoginServerCookie)
{
	int i, j;

	for (i=0; i < eaSize(&sppEditQueueWaitingList); i++)
	{
		if (sppEditQueueWaitingList[i]->iLoginServerContainerID == iLoginServerContainerID && sppEditQueueWaitingList[i]->iLoginServerCookie == iLoginServerCookie)
		{
			StructDestroy(parse_WaitingForCookieCache, sppEditQueueWaitingList[i]);
			eaRemove(&sppEditQueueWaitingList, i);
			for (j=i; j < eaSize(&sppEditQueueWaitingList); j++)
			{
				RemoteCommand_EditQueuePlaceUpdate(GLOBALTYPE_LOGINSERVER, sppEditQueueWaitingList[j]->iLoginServerContainerID, sppEditQueueWaitingList[j]->iLoginServerCookie, j);
			}
			return;
		}
	}

}


bool CheckEditQueueCookieValidity(U32 iCookie)
{
	bool bRetVal = false;
	int i;

	if (iCookie == U32_MAX) {
		return true;
	}

	for (i=0; i < eaSize(&sppCurEditQueueCookies); i++)
	{
		if (sppCurEditQueueCookies[i]->iCookie == iCookie)
		{
			if (sppCurEditQueueCookies[i]->iExpirationTime >= timeSecondsSince2000())
			{
				bRetVal = true;
			}

			StructDestroy(parse_CurrentlyActiveEditQueueCookie, sppCurEditQueueCookies[i]);
			eaRemoveFast(&sppCurEditQueueCookies, i);

			break;
		}
	}

	return bRetVal;
}

U32 aslMapManager_UGCEditQueue_Size(void)
{
	return eaSize(&sppEditQueueWaitingList);
}

void EditQueue_OncePerSecond(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	static U32 sLastFullQueueUpdateTime = 0;
	int i;
	U32 iCurTime = timeSecondsSince2000();
	bool bSomethingChanged = false;

	GameServerList *pGameServerList = NewMapTransfer_FindEditingGameServerList();
	int iNumUGCEditServers = pGameServerList ? eaSize(&pGameServerList->ppGameServers) : 0;

	for (i=eaSize(&sppCurEditQueueCookies)-1 ; i >= 0; i--)
	{
		if (sppCurEditQueueCookies[i]->iExpirationTime < iCurTime)
		{
			StructDestroy(parse_CurrentlyActiveEditQueueCookie, sppCurEditQueueCookies[i]);
			eaRemoveFast(&sppCurEditQueueCookies, i);
		}
	}

	while(eaSize(&sppEditQueueWaitingList) && iNumUGCEditServers + eaSize(&sppCurEditQueueCookies) < gMapManagerConfig.iMaxEditServers)
	{
		WaitingForCookieCache *pCache = eaRemove(&sppEditQueueWaitingList, 0);
		CurrentlyActiveEditQueueCookie *pCookie = GetNewActiveCookie();
		RemoteCommand_HereIsEditQueueCookie(GLOBALTYPE_LOGINSERVER, pCache->iLoginServerContainerID, pCache->iLoginServerCookie, pCookie->iCookie);
		StructDestroy(parse_WaitingForCookieCache, pCache);
		bSomethingChanged = true;
	}

	if (bSomethingChanged || iCurTime - sLastFullQueueUpdateTime > 10)
	{
		sLastFullQueueUpdateTime = iCurTime;
		for (i=0; i < eaSize(&sppEditQueueWaitingList); i++)
		{
			RemoteCommand_EditQueuePlaceUpdate(GLOBALTYPE_LOGINSERVER, sppEditQueueWaitingList[i]->iLoginServerContainerID, sppEditQueueWaitingList[i]->iLoginServerCookie, i);
		}
	}

	{
		static bool bReportedQueueSizeAtLeastOnce = false;
		static U32 iLastQueueSizeReported = 0;
		U32 iThisQueueSize = aslMapManager_UGCEditQueue_Size();

		if(!bReportedQueueSizeAtLeastOnce || iThisQueueSize != iLastQueueSizeReported)
		{
			servLog(LOG_UGC, "EditQueueSize", "Size %u", iThisQueueSize);
			bReportedQueueSizeAtLeastOnce = true;
			iLastQueueSizeReported = iThisQueueSize;
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(ugcSeries, ".id, .iOwnerAccountID, .strIDString, .eaVersions, .ugcSearchCache.strPublishedName, .ugcSearchCache.eaPublishedProjectIDs, .iLastUpdatedTime, .ugcReviews.ppReviews, .ugcReviews.piNumRatings, .strOwnerAccountName, .bFlaggedAsCryptic")
ATR_LOCKS(ugcProjectsInSeries, ".id, .iOwnerAccountID, .seriesID");
enumTransactionOutcome trUGCProjectSeries_Create(ATR_ARGS, NOCONST(UGCProjectSeries)* ugcSeries, NON_CONTAINER UGCProjectSeriesVersion* seriesVersion, CONST_EARRAY_OF(NOCONST(UGCProject)) ugcProjectsInSeries)
{
	NOCONST(UGCProjectSeriesVersion)* pVersion = StructCreateNoConst( parse_UGCProjectSeriesVersion );
	char buffer[ 256 ];

	UGCIDString_IntToString( ugcSeries->id, /*isSeries=*/true, buffer );
	ugcSeries->strIDString = StructAllocString( buffer );
	
	if( eaSize( &ugcSeries->eaVersions ) != 0 ) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	ugc_trh_RebuildRatingBucketsIfNecessary( &ugcSeries->ugcReviews );

	{
		enumTransactionOutcome result = trUGCProjectSeries_Update( ATR_PASS_ARGS, ugcSeries->strOwnerAccountName, ugcSeries, seriesVersion, ugcProjectsInSeries, NULL );
		if( result == TRANSACTION_OUTCOME_SUCCESS ) {
			ugcSeries->eaVersions[ 0 ]->eState = UGC_SAVED;
		}
		
		return result;
	}
}

static void aslUGCDataManager_ProjectSeries_Create_TransactionCB(TransactionReturnVal* pReturn, UGCProjectSeriesCreateData *ugcProjectSeriesCreateData)
{
	if( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) {
		RemoteCommand_Intershard_aslLogin_ProjectSeries_Create_Return(ugcProjectSeriesCreateData->pcShardName, GLOBALTYPE_LOGINSERVER, ugcProjectSeriesCreateData->loginServerID,
			ugcProjectSeriesCreateData->loginCookie, ugcProjectSeriesCreateData->ugcSeries->id);
	} else {
		AssertOrAlertWarning( "UGC_PROJECTSERIES_CREATE_TRANSACTION_FAIL", "Unable to fillout UGCProjectSeries container because %s",
							  GetTransactionFailureString( pReturn ));
		RemoteCommand_Intershard_aslLogin_ProjectSeries_Create_Return(ugcProjectSeriesCreateData->pcShardName, GLOBALTYPE_LOGINSERVER, ugcProjectSeriesCreateData->loginServerID,
			ugcProjectSeriesCreateData->loginCookie, 0);
	}

	StructDestroy( parse_UGCProjectSeriesCreateData, ugcProjectSeriesCreateData );
}

static void aslUGCDataManager_ProjectSeries_Create_ContainerCB( TransactionReturnVal *pReturn, UGCProjectSeriesCreateData *ugcProjectSeriesCreateData )
{
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) {
		ContainerID newSeriesKey = atoi( pReturn->pBaseReturnVals[ 0 ].returnString );
		UGCProjectSeries* newSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, newSeriesKey);
		U32* newSeriesProjects = NULL;

		// In case the Transaction fails, we need to make sure the
		// series when first created will get auto-deleted.
		devassert( UGCProjectSeries_CanAutoDelete( newSeries ));
		
		ugcProjectSeriesGetProjectIDs( &newSeriesProjects, ugcProjectSeriesCreateData->ugcSeries->eaVersions[0]->eaChildNodes );
		
		CONTAINER_NOCONST(UGCProjectSeries, ugcProjectSeriesCreateData->ugcSeries)->id = newSeriesKey;
		AutoTrans_trUGCProjectSeries_Create( LoggedTransactions_CreateManagedReturnVal( __FUNCTION__, aslUGCDataManager_ProjectSeries_Create_TransactionCB, ugcProjectSeriesCreateData ),
											 GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECTSERIES, newSeriesKey,
											 ugcProjectSeriesCreateData->ugcSeries->eaVersions[0], GLOBALTYPE_UGCPROJECT, &newSeriesProjects );
		ea32Destroy( &newSeriesProjects );
	} else {
		AssertOrAlertWarning( "UGC_PROJECTSERIES_CREATE_CONTAINER_FAIL", "Unable to create UGCProjectSeries container because %s",
							  GetTransactionFailureString( pReturn ));
		RemoteCommand_Intershard_aslLogin_ProjectSeries_Create_Return(ugcProjectSeriesCreateData->pcShardName, GLOBALTYPE_LOGINSERVER, ugcProjectSeriesCreateData->loginServerID, ugcProjectSeriesCreateData->loginCookie, 0);
		StructDestroy(parse_UGCProjectSeriesCreateData, ugcProjectSeriesCreateData);
	}
}

static void ugcProjectSeriesAssignUniqueIDs( CONST_EARRAY_OF(NOCONST(UGCProjectSeriesNode)) seriesNodes, int* id )
{
	int it;
	for( it = 0; it != eaSize( &seriesNodes ); ++it ) {
		if( !seriesNodes[ it ]->iProjectID ) {
			seriesNodes[ it ]->iNodeID = (*id)++;
		}

		ugcProjectSeriesAssignUniqueIDs( seriesNodes[ it ]->eaChildNodes, id );
	}
}

static void ugcProjectSeriesVersionFixup( NOCONST(UGCProjectSeriesVersion)* pSeriesVersion )
{
	int nodeID = 1;
	ugcProjectSeriesAssignUniqueIDs( pSeriesVersion->eaChildNodes, &nodeID );
	UGCProject_FillInTimestamp( &pSeriesVersion->sPublishTimeStamp );
} 

// NOTE: I intentionally have the AccountID and AccountName seperate
// (even though they could be in the UGCProjectSeries), to make sure
// no one forgets to send *valid* fields.
AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ProjectSeries_Create( UGCProjectSeries* ugcSeries, U32 iForAccountID, const char* strForAccountName, const char *pcShardName, U32 loginServerID, U64 loginCookie)
{
	char* estrError = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to create a UGC container!");

	if( !ugcSeries ) {
		log_printf(LOG_LOGIN, "Invalid UGC project requested by %d, failing because No Series Specified", iForAccountID);
		RemoteCommand_Intershard_aslLogin_ProjectSeries_Create_Return(pcShardName, GLOBALTYPE_LOGINSERVER, loginServerID, loginCookie, 0);
	} else if( eaSize( &ugcSeries->eaVersions ) != 1 || !ugcSeries->eaVersions[ 0 ]) {
		log_printf(LOG_LOGIN, "Invalid UGC project requested by %d, failing because exactly one version required", iForAccountID);
		RemoteCommand_Intershard_aslLogin_ProjectSeries_Create_Return(pcShardName, GLOBALTYPE_LOGINSERVER, loginServerID, loginCookie, 0);
	} else {
		UGCProjectSeriesCreateData *ugcProjectSeriesCreateData = StructCreate(parse_UGCProjectSeriesCreateData);
		ugcProjectSeriesCreateData->pcShardName = StructAllocString(pcShardName);
		ugcProjectSeriesCreateData->loginCookie = loginCookie;
		ugcProjectSeriesCreateData->loginServerID = loginServerID;
		ugcProjectSeriesCreateData->ugcSeries = StructClone( parse_UGCProjectSeries, ugcSeries );

		ugcProjectSeriesVersionFixup( CONTAINER_NOCONST( UGCProjectSeriesVersion, ugcProjectSeriesCreateData->ugcSeries->eaVersions[ 0 ]));

		// Intentionally create the container with no versions so the
		// container can be auto-deleted.
		{
			NOCONST(UGCProjectSeries)* mutableUGCSeries = CONTAINER_NOCONST(UGCProjectSeries, ugcSeries);
			NOCONST(UGCProjectSeriesVersion)** oldVersions = mutableUGCSeries->eaVersions;

			mutableUGCSeries->iOwnerAccountID = iForAccountID;
			UGCProjectSeries_trh_SetOwnerAccountName(ATR_EMPTY_ARGS, mutableUGCSeries, strForAccountName);
			mutableUGCSeries->eaVersions = NULL;
			objRequestContainerCreate( objCreateManagedReturnVal( aslUGCDataManager_ProjectSeries_Create_ContainerCB, ugcProjectSeriesCreateData ),
									   GLOBALTYPE_UGCPROJECTSERIES, ugcSeries, GetAppGlobalType(), GetAppGlobalID() );
			mutableUGCSeries->eaVersions = oldVersions;
		}
	}

	estrDestroy( &estrError );
}

AUTO_TRANSACTION
ATR_LOCKS(ugcSeries, ".eaVersions")
ATR_LOCKS(ugcProjectsInSeries, ".seriesID");
enumTransactionOutcome trUGCProjectSeries_Destroy(ATR_ARGS, NOCONST(UGCProjectSeries)* ugcSeries, CONST_EARRAY_OF(NOCONST(UGCProject)) ugcProjectsInSeries)
{
	int it;
	for( it = 0; it != eaSize( &ugcProjectsInSeries ); ++it ) {
		ugcProjectsInSeries[ it ]->seriesID = 0;
	}
	eaDestroyStructNoConst( &ugcSeries->eaVersions, parse_UGCProjectSeriesVersion );

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void aslUGCDataManager_ProjectSeries_Destroy_CB(TransactionReturnVal *pReturn, ContainerIdReturnData* pContainerIdReturnData)
{
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) {
		UGCProjectSeries* newSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, pContainerIdReturnData->id);
		devassert( UGCProjectSeries_CanAutoDelete( newSeries ));

		RemoteCommand_Intershard_aslUGCDataManager_ProjectSeries_Return(pContainerIdReturnData->pcShardName, GLOBALTYPE_LOGINSERVER, pContainerIdReturnData->loginServerID,
			true, pContainerIdReturnData->loginCookie);
	} else {
		AssertOrAlertWarning( "UGC_PROJECTSERIES_DESTROY_TRANSACTION_FAIL", "Unable to delete UGCProjectSeries %d because %s",
							  pContainerIdReturnData->id, GetTransactionFailureString( pReturn ));

		RemoteCommand_Intershard_aslUGCDataManager_ProjectSeries_Return(pContainerIdReturnData->pcShardName, GLOBALTYPE_LOGINSERVER, pContainerIdReturnData->loginServerID,
			false, pContainerIdReturnData->loginCookie);
	}
	free(pContainerIdReturnData);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ProjectSeries_Destroy(ContainerID seriesID, U32 iForAccountID, const char *pcShardName, U32 loginServerID, U64 loginCookie)
{
	UGCProjectSeries* projectSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, seriesID);

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if( !projectSeries || projectSeries->iOwnerAccountID != iForAccountID ) {
		RemoteCommand_Intershard_aslUGCDataManager_ProjectSeries_Return(pcShardName, GLOBALTYPE_LOGINSERVER, loginServerID,
			false, loginCookie);
	} else {
		ContainerIdReturnData *pContainerIdReturnData = calloc(1, sizeof(ContainerIdReturnData));
		ContainerID* seriesProjects = NULL;
		pContainerIdReturnData->pcShardName = StructAllocString(pcShardName);
		pContainerIdReturnData->loginServerID = loginServerID;
		pContainerIdReturnData->loginCookie = loginCookie;
		pContainerIdReturnData->id = seriesID;

		if( eaTail( &projectSeries->eaVersions )) {
			aslUGCDataManager_ProjectSeriesGetExistingProjectIDs( &seriesProjects, eaTail( &projectSeries->eaVersions )->eaChildNodes );
		}
		
		AutoTrans_trUGCProjectSeries_Destroy( objCreateManagedReturnVal( aslUGCDataManager_ProjectSeries_Destroy_CB, pContainerIdReturnData ), GLOBALTYPE_UGCDATAMANAGER,
			GLOBALTYPE_UGCPROJECTSERIES, seriesID,
			GLOBALTYPE_UGCPROJECT, &seriesProjects );
		ea32Destroy( &seriesProjects );
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProjectSeries, ".id, .iOwnerAccountID, .eaVersions, .ugcSearchCache.strPublishedName, .ugcSearchCache.eaPublishedProjectIDs, .iLastUpdatedTime, .strOwnerAccountName, .bFlaggedAsCryptic")
ATR_LOCKS(ugcProjectsInSeries, ".seriesID, .id, .iOwnerAccountID")
ATR_LOCKS(oldUgcProjectsInSeries, ".id, .seriesID, .iOwnerAccountID");
enumTransactionOutcome trUGCProjectSeries_Update( ATR_ARGS, const char *pOwnerAccountName, NOCONST(UGCProjectSeries)* pUGCProjectSeries, NON_CONTAINER UGCProjectSeriesVersion* newSeriesVersion, CONST_EARRAY_OF(NOCONST(UGCProject)) ugcProjectsInSeries, CONST_EARRAY_OF(NOCONST(UGCProject)) oldUgcProjectsInSeries )
{
	int it;
	for( it = 0; it != eaSize( &oldUgcProjectsInSeries ); ++it ) {
		if( oldUgcProjectsInSeries[ it ]->iOwnerAccountID != pUGCProjectSeries->iOwnerAccountID ) {
			TRANSACTION_RETURN_LOG_FAILURE( "Can't remove project %d from series %d because they do not have the same owner.",
											oldUgcProjectsInSeries[ it ]->id, pUGCProjectSeries->id );
		}
		oldUgcProjectsInSeries[ it ]->seriesID = 0;
	}
	for( it = 0; it != eaSize( &ugcProjectsInSeries ); ++it ) {
		if( ugcProjectsInSeries[ it ]->iOwnerAccountID != pUGCProjectSeries->iOwnerAccountID ) {
			TRANSACTION_RETURN_LOG_FAILURE( "Can't add project %d into series %d because they do not have the same owner.",
											ugcProjectsInSeries[ it ]->id, pUGCProjectSeries->id );
		}
		ugcProjectsInSeries[ it ]->seriesID = pUGCProjectSeries->id;
	}

	{
		NOCONST(UGCProjectSeriesVersion)* newVersion = StructCloneDeConst( parse_UGCProjectSeriesVersion, newSeriesVersion );
		ugc_trh_UGCProjectSeries_UpdateCache( ATR_PASS_ARGS, &pUGCProjectSeries->ugcSearchCache, newSeriesVersion );
		newVersion->eState = UGC_PUBLISHED;
		
		eaPush( &pUGCProjectSeries->eaVersions, newVersion );

		if(eaSize(&pUGCProjectSeries->eaVersions) == 1)
		{
			UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
			event->uUGCAuthorID = pUGCProjectSeries->iOwnerAccountID;
			event->uUGCSeriesID = pUGCProjectSeries->id;
			event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
			event->ugcAchievementServerEvent->ugcSeriesPublishedEvent = StructCreate(parse_UGCSeriesPublishedEvent);
			QueueRemoteCommand_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
			StructDestroy(parse_UGCAchievementEvent, event);
		}
	}
	pUGCProjectSeries->iLastUpdatedTime = timeSecondsSince2000();

	UGCProjectSeries_trh_SetOwnerAccountName(ATR_PASS_ARGS, pUGCProjectSeries, pOwnerAccountName);

	return TRANSACTION_OUTCOME_SUCCESS;
}

typedef struct UGCProjectSeriesUpdateData {
	char *pOwnerAccountName;

	char *pcShardName;
	U32 loginServerID;
	U64 loginCookie;

	UGCProjectSeries* ugcSeries;
	ContainerID* eaUGCProjectIDs;
	ContainerID* eaOldUGCProjectIDs;
} UGCProjectSeriesUpdateData;

static void aslUGCDataManager_ProjectSeries_Update_CB(TransactionReturnVal *pReturn, UGCProjectSeriesUpdateData* pData)
{
	if( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) {
		RemoteCommand_Intershard_aslLogin_ProjectSeries_Update_Return(pData->pcShardName, GLOBALTYPE_LOGINSERVER, pData->loginServerID, true, pData->loginCookie);
	} else {
		AssertOrAlertWarning( "UGC_PROJECTSERIES_MOVE_PROJECT_FAIL", "UGCProjectSeries update failed because %s", GetTransactionFailureString( pReturn ));
		RemoteCommand_Intershard_aslLogin_ProjectSeries_Update_Return(pData->pcShardName, GLOBALTYPE_LOGINSERVER, pData->loginServerID, false, pData->loginCookie);
	}

	StructDestroySafe(parse_UGCProjectSeries, &pData->ugcSeries);
	ea32Destroy(&pData->eaUGCProjectIDs);
	ea32Destroy(&pData->eaOldUGCProjectIDs);
	SAFE_FREE(pData);
}

static void aslUGCDataManager_ProjectSeries_Update_AccountCB(UGCAccount* ignored, UGCProjectSeriesUpdateData* pData)
{
	AutoTrans_trUGCProjectSeries_Update(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, aslUGCDataManager_ProjectSeries_Update_CB, pData), GLOBALTYPE_UGCDATAMANAGER,
										pData->pOwnerAccountName,
										GLOBALTYPE_UGCPROJECTSERIES, pData->ugcSeries->id, pData->ugcSeries->eaVersions[0],
										GLOBALTYPE_UGCPROJECT, &pData->eaUGCProjectIDs, GLOBALTYPE_UGCPROJECT, &pData->eaOldUGCProjectIDs);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ProjectSeries_Update(UGCProjectSeries* ugcSeries, U32 iForAccountID, const char *pOwnerAccountName, const char *pcShardName, U32 loginServerID, U64 loginCookie)
{
	NOCONST(UGCProjectSeries)* mutableUgcSeries = CONTAINER_NOCONST( UGCProjectSeries, ugcSeries );
	UGCProjectSeries* oldUgcSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, ugcSeries->id);
	UGCProjectSeriesVersion* oldUgcSeriesVersion = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if( !oldUgcSeries || oldUgcSeries->iOwnerAccountID != iForAccountID )
		RemoteCommand_Intershard_aslLogin_ProjectSeries_Update_Return(pcShardName, GLOBALTYPE_LOGINSERVER, loginServerID, false, loginCookie);
	else if( eaSize( &ugcSeries->eaVersions ) != 1 || !ugcSeries->eaVersions[ 0 ])
		RemoteCommand_Intershard_aslLogin_ProjectSeries_Update_Return(pcShardName, GLOBALTYPE_LOGINSERVER, loginServerID, false, loginCookie);
	else if( eaSize( &oldUgcSeries->eaVersions ) == 0 )
		RemoteCommand_Intershard_aslLogin_ProjectSeries_Update_Return(pcShardName, GLOBALTYPE_LOGINSERVER, loginServerID, false, loginCookie);
	else
	{
		ugcProjectSeriesVersionFixup( mutableUgcSeries->eaVersions[ 0 ]);

		oldUgcSeriesVersion = eaTail( &oldUgcSeries->eaVersions );
	
		if( StructCompare( parse_UGCProjectSeries, oldUgcSeriesVersion, ugcSeries->eaVersions[ 0 ], 0, 0, 0 ) != 0 ) {
			UGCProjectSeriesUpdateData* pData = calloc( 1, sizeof( *pData ));
			int it;

			pData->pOwnerAccountName = strdup(pOwnerAccountName);
			pData->pcShardName = StructAllocString(pcShardName);
			pData->loginServerID = loginServerID;
			pData->loginCookie = loginCookie;
			pData->ugcSeries = StructClone( parse_UGCProjectSeries, ugcSeries );

			// Note: Do not look for containers that are removed for the
			// new values -- those must all exist!
			ugcProjectSeriesGetProjectIDs( &pData->eaUGCProjectIDs, ugcSeries->eaVersions[0]->eaChildNodes );
			aslUGCDataManager_ProjectSeriesGetExistingProjectIDs( &pData->eaOldUGCProjectIDs, oldUgcSeriesVersion->eaChildNodes );
		
			for( it = ea32Size( &pData->eaOldUGCProjectIDs ) - 1; it >= 0; --it ) {
				if( ea32Find( &pData->eaUGCProjectIDs, pData->eaOldUGCProjectIDs[ it ]) >= 0 ) {
					ea32Remove( &pData->eaOldUGCProjectIDs, it );
				}
			}
		
			UGCAccountEnsureExists( SAFE_MEMBER( oldUgcSeries, iOwnerAccountID ), aslUGCDataManager_ProjectSeries_Update_AccountCB, pData );
		} else {
			RemoteCommand_Intershard_aslLogin_ProjectSeries_Update_Return(pcShardName, GLOBALTYPE_LOGINSERVER, loginServerID, true, loginCookie);
		}
	}
}

static void aslUGCDataManager_ProjectSeriesGetExistingProjectIDs( ContainerID** peaiIDs, CONST_EARRAY_OF(UGCProjectSeriesNode) eaSeriesNodes )
{
	ugcProjectSeriesGetProjectIDs( peaiIDs, eaSeriesNodes );

	// Remove any projects that no longer exist, because that will cause transactions to fail
	{
		int it;
		for( it = ea32Size( peaiIDs ) - 1; it >= 0; --it ) {
			ContainerID projID = (*peaiIDs)[ it ];
			if( !objGetContainerData( GLOBALTYPE_UGCPROJECT, projID )) {
				ea32Remove( peaiIDs, it );
			}
		}
	}
}

static UGCDetails* aslUGCDataManager_RequestReviewsHelper(ContainerID iProjectID, ContainerID iSeriesID, U32 uReviewerAccountID, S32 iReviewsPage)
{
	UGCDetails* pResult = NULL;
	UGCProject* pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, iProjectID);
	UGCProjectSeries* pSeries = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if( iSeriesID == UGC_SERIES_ID_FROM_PROJECT ) {
		if( pProject ) {
			pSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, pProject->seriesID);
		}
	} else {
		pSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, iSeriesID);
	}

	if( pProject || pSeries ) {
		pResult = StructCreate( parse_UGCDetails );
		if( pProject ) {
			pResult->pProject = UGCProject_CreateDetailCopy( pProject, iReviewsPage, true, uReviewerAccountID );
		}
		if( pSeries ) {
			pResult->pSeries = UGCProjectSeries_CreateDetailCopy( pSeries, iReviewsPage, uReviewerAccountID );
		}
	}

	return pResult;
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void aslUGCDataManager_RequestDetails(const char *pcShardName, ContainerID entContainerID,
	ContainerID iProjectID, ContainerID iSeriesID, U32 uReviewerAccountID, S32 iRequesterID)
{
	UGCDetails* pResult = aslUGCDataManager_RequestReviewsHelper(iProjectID, iSeriesID, uReviewerAccountID, /*iReviewsPage=*/0);
	if(pResult)
	{
		RemoteCommand_Intershard_gslUGC_RequestDetails_Return(pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, entContainerID, pResult, iRequesterID);

		StructDestroy(parse_UGCDetails, pResult);
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void aslUGCDataManager_RequestReviewsForPage(const char *pcShardName, ContainerID entContainerID,
	ContainerID iProjectID, ContainerID iSeriesID, S32 iReviewsPage)
{
	UGCDetails* pResult = aslUGCDataManager_RequestReviewsHelper(iProjectID, iSeriesID, /*uReviewerAccountID=*/0, iReviewsPage);
	if(pResult)
	{
		RemoteCommand_Intershard_gslUGC_RequestReviewsForPageReturn(pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, entContainerID, pResult, iReviewsPage);

		StructDestroy(parse_UGCDetails, pResult);
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void aslUGCDataManager_RequestReviewsForPageFromLoginServer(ContainerID iProjectID, ContainerID iSeriesID, S32 iReviewsPage, const char *pcShardName, U32 loginServerID, U64 loginCookie)
{
	UGCDetails* pResult = aslUGCDataManager_RequestReviewsHelper(iProjectID, iSeriesID, /*uReviewerAccountID=*/0, iReviewsPage);
	if(pResult)
	{
		RemoteCommand_Intershard_aslLoginUGCProject_RequestReviewsForPageReturn(pcShardName, GLOBALTYPE_LOGINSERVER, loginServerID, pResult, iReviewsPage, loginCookie);

		StructDestroy(parse_UGCDetails, pResult);
	}
}

static void BanUGCProjectsByAccountID_CB(TransactionReturnVal *pReturn, UserData rawList)
{
	ProjectsForSingleOwner* pList = rawList;
	
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) {
		int it;
		for( it = 0; it != eaiSize( &pList->pProjectIDs ); ++it ) {
			const UGCProject* pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, pList->pProjectIDs[ it ]);
			const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion( pProject );
			if( pProject ) {
				RemoteCommand_gslUGC_SendMailForChange( GLOBALTYPE_WEBREQUESTSERVER, 0, pProject, pVersion, UGC_CHANGE_CSR_BAN );
			}
		}
	} else {
		AssertOrAlert( "UGC_BAN_BY_ACCOUNT_FAILED", "Ban failed for account project.  This should never ever ever happen." );
	}

	StructDestroySafe( parse_ProjectsForSingleOwner, &pList );
}

AUTO_TRANSACTION
ATR_LOCKS(ugcProjects, ".bBanned, .ugcReporting.uTemporaryBanExpireTime, .id, .iOwnerAccountID, .pOwnerAccountName, .ugcReporting.iNaughtyValue");
enumTransactionOutcome trBanUgcProjects( ATR_ARGS, CONST_EARRAY_OF(NOCONST(UGCProject)) ugcProjects, const char* pchCSRAccount )
{
	int it;
	for( it = 0; it != eaSize( &ugcProjects ); ++it ) {
		char* estrLogText = NULL;
		
		ugcProjects[ it ]->bBanned = true;
		ugcProjects[ it ]->ugcReporting.uTemporaryBanExpireTime = 0;
		
		UGCProject_GetBanStatusString(ugcProjects[it]->id, ugcProjects[it]->iOwnerAccountID, ugcProjects[it]->pOwnerAccountName,
									  pchCSRAccount,
									  ugcProjects[it]->ugcReporting.iNaughtyValue, 0, true, false, 
									  &estrLogText);
		TRANSACTION_APPEND_LOG_SUCCESS("%s", estrLogText);
		estrDestroy(&estrLogText);
	}
	
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void BanUGCProjectsByAccountID(ContainerID accountID, const char* pWho)
{
	ProjectsForSingleOwner *pList = NULL;

	ASSERT_CONTAINER_DATA_UGC("Trying to access sProjectsForSingleOwnerByAccountID when not the UGCDataManager!");

	if (!accountID)
		return;

	stashIntFindPointer(sProjectsForSingleOwnerByAccountID, accountID, &pList);
	if( pList ) {
		ProjectsForSingleOwner* pListData = StructClone( parse_ProjectsForSingleOwner, pList );
		AutoTrans_trBanUgcProjects(
				LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, BanUGCProjectsByAccountID_CB, pListData), 
				GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, &pList->pProjectIDs, 
				pWho );
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Id, .Ppprojectversions");
enumTransactionOutcome trSetVersionUnplayable(ATR_ARGS, NOCONST(UGCProject) *pProject, char *pNameSpace, char *pComment)
{
	NOCONST(UGCProjectVersion) *pVersion = UGCProject_trh_GetSpecificVersion(ATR_RECURSE, pProject, pNameSpace);
	if (!pVersion)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Can't mark unplayable %s from project %u, doesn't exist",
			pNameSpace, pProject->id);
	}

	ugcProjectSetVersionState(pVersion, UGC_UNPLAYABLE, pComment);

	SAFE_FREE(pVersion->pPublishJobName);
	SAFE_FREE(pVersion->pPublishResult);

	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Id, .Ppprojectversions");
enumTransactionOutcome trSetVersionNeedsRepublishing(ATR_ARGS, NOCONST(UGCProject) *pProject, char *pNameSpace, char *pComment)
{
	NOCONST(UGCProjectVersion) *pVersion = UGCProject_trh_GetSpecificVersion(ATR_RECURSE, pProject, pNameSpace);
	if (!pVersion)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Can't mark unplayable %s from project %u, doesn't exist",
			pNameSpace, pProject->id);
	}

	ugcProjectSetVersionState(pVersion, UGC_NEEDS_REPUBLISHING, pComment);

	SAFE_FREE(pVersion->pPublishJobName);
	SAFE_FREE(pVersion->pPublishResult);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Id, .Ppprojectversions");
enumTransactionOutcome trSetVersionNeedsUnplayable(ATR_ARGS, NOCONST(UGCProject) *pProject, char *pNameSpace, char *pComment)
{
	NOCONST(UGCProjectVersion) *pVersion = UGCProject_trh_GetSpecificVersion(ATR_RECURSE, pProject, pNameSpace);
	if (!pVersion)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Can't mark unplayable %s from project %u, doesn't exist",
			pNameSpace, pProject->id);
	}

	ugcProjectSetVersionState(pVersion, UGC_NEEDS_UNPLAYABLE, pComment);

	SAFE_FREE(pVersion->pPublishJobName);
	SAFE_FREE(pVersion->pPublishResult);

	return TRANSACTION_OUTCOME_SUCCESS;
}

//marks a bunch of namespaces as unplayable. do a trial run first to get a report on what will be done
//but not actually do anything

AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_ACCESSLEVEL(7);
char *MakeNamespacesUnplayable(bool bTrialRun, char *pComment, ACMD_SENTENCE pInNamespaces)
{
	char **ppNameSpaces = NULL;
	int i;
	static char *pResultString = NULL;
	static char *spFullComment = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	estrPrintf(&spFullComment, "MakeNamespacesUnplayable: %s", pComment);

	estrClear(&pResultString);
	DivideString(pInNamespaces, " ,", &ppNameSpaces, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	for (i = 0 ; i < eaSize(&ppNameSpaces); i++)
	{
		char *pNameSpace = ppNameSpaces[i];
		U32 iContainerID = UGCProject_GetContainerIDFromUGCNamespace(pNameSpace);
		if (!iContainerID)
		{
			estrConcatf(&pResultString, "Tried to interpret %s as a UGC namespace, but it was badly formatted\n", pNameSpace);
		}
		else
		{
			UGCProject *pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, iContainerID);
			if (!pProject)
			{
				estrConcatf(&pResultString, "Namespace %s should be in container ID %u, but that ugc project doesn't seem to exist\n",
					pNameSpace, iContainerID);
			}
			else
			{
				const UGCProjectVersion *pVersion = UGCProject_GetSpecificVersion(pProject, pNameSpace);
				if (!pVersion)
				{
					estrConcatf(&pResultString, "Namespace %s should be in project %u (%s by %s), but not found\n",
								pNameSpace, iContainerID, pProject->pIDString, pProject->pOwnerAccountName);
				}
				else
				{
					estrConcatf(&pResultString, "Namespace %s found in project %u (%s by %s), currently in state %s.",
								pNameSpace, iContainerID, pProject->pIDString, pProject->pOwnerAccountName, StaticDefineIntRevLookup(UGCProjectVersionStateEnum, ugcProjectGetVersionStateConst(pVersion)));
					if (bTrialRun)
					{
						estrConcatf(&pResultString, " State would be changed to UNPLAYABLE\n");
					}
					else
					{
						estrConcatf(&pResultString, " Changing state to UNPLAYABLE\n");
						AutoTrans_trSetVersionUnplayable(NULL, GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, iContainerID, 
							pNameSpace, spFullComment);
					}
				}
			}
		}
	}


	eaDestroyEx(&ppNameSpaces, NULL);
	return pResultString;
}

//////////////////////////////////////////////////////////////////////
// UGCFeaturedCopyProject process.
//
// Takes a specific project, copies it, then publishes it.
AUTO_STRUCT;
typedef struct UGCFeaturedCopyProjectData {
	char *pcShardName;
	ContainerID entContainerID;
	ContainerID importProjID;
	UGCProjectInfo* importProjInfo;
	char* importNamespace;
	UGCFeaturedData* featuredData;

	// Only available after CreateCB.
	ContainerID newProjID;
} UGCFeaturedCopyProjectData;
extern ParseTable parse_UGCFeaturedCopyProjectData[];
#define TYPE_parse_UGCFeaturedCopyProjectData UGCFeaturedCopyProjectData

void UGCFeaturedCopyProjectReturn( const char *pcShardName, ContainerID entContainerID, bool bSucceeded, char* fmtStr, ... )
{
	UGCShardReturnAndErrorString ret = { 0 };
	va_list ap;
	va_start( ap, fmtStr );
	
	ret.bSucceeded = bSucceeded;
	estrConcatfv( &ret.estrDetails, fmtStr, ap );
	log_printf( LOG_UGC, "%s", ret.estrDetails );
	RemoteCommand_Intershard_UGCFeaturedCopyProject_Return(pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, entContainerID, &ret);
	StructReset( parse_UGCShardReturnAndErrorString, &ret );
	
	va_end( ap );
}

AUTO_STRUCT;
typedef struct NewMapTransferData
{
	DynamicPatchInfo *pPatchInfo;
	UGCFeaturedCopyProjectData *data;
} NewMapTransferData;

static void aslMapManager_NewMapTransfer_LaunchNewServer_CB(TransactionReturnVal *returnVal, NewMapTransferData* pNewMapTransferData)
{
	TrackedGameServerExe *pServer = NULL;

	if(RemoteCommandCheck_aslMapManager_NewMapTransfer_LaunchNewServer(returnVal, &pServer) == TRANSACTION_OUTCOME_SUCCESS)
		UGCFeaturedCopyProjectReturn(pNewMapTransferData->data->pcShardName, pNewMapTransferData->data->entContainerID, true, "%s -- Successfully created clone project %u, starting publish on server id %d.", __FUNCTION__, pNewMapTransferData->data->newProjID, pServer->iContainerID);
	else
		UGCFeaturedCopyProjectReturn(pNewMapTransferData->data->pcShardName, pNewMapTransferData->data->entContainerID, false, "%s -- Could not get MapManager to launch new server for publishing clone project %u.", __FUNCTION__, pNewMapTransferData->data->newProjID);

	StructDestroy(parse_NewMapTransferData, pNewMapTransferData);
	StructDestroy(parse_TrackedGameServerExe, pServer);
}

void UGCFeaturedCopyProject_FillInCB( TransactionReturnVal* pReturn, UGCFeaturedCopyProjectData* data )
{
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) {
		UGCProject* newProj = objGetContainerData(GLOBALTYPE_UGCPROJECT, data->newProjID);
		if( newProj ) {
			GameServerExe_Description* pDescription = StructCreate( parse_GameServerExe_Description );
			NewOrExistingGameServerAddressRequesterInfo requesterInfo = { 0 };
			DynamicPatchInfo* pPatchInfo = CreatePatchInfoForNameSpace( data->importNamespace, false );
			pDescription->eMapType = ZMTYPE_MISSION;
			pDescription->eServerType = GSTYPE_UGC_EDIT;
			pDescription->iUGCProjectID = data->newProjID;
			pDescription->pMapDescription = GetEditMapNameFromUGCProject( newProj );

			requesterInfo.pcRequestingShardName = GetShardNameFromShardInfoString();
			requesterInfo.eRequestingServerType = GetAppGlobalType();
			requesterInfo.iRequestingServerID = GetAppGlobalID();
			// MJF -- Don't care about other fields?

			{
				char* estrCommandLine = NULL;
				NewMapTransferData *pNewMapTransferData = StructCreate(parse_NewMapTransferData);
				pNewMapTransferData->data = data;
				pNewMapTransferData->pPatchInfo = pPatchInfo;
				estrPrintf( &estrCommandLine, " -UGCImportProjectOnStart %s -LoadUserNamespaces %s -UGCPublishAndQuit -InactivityTimeoutMinutes 1 ",
							data->importNamespace, data->importNamespace );
				RemoteCommand_aslMapManager_NewMapTransfer_LaunchNewServer( objCreateManagedReturnVal(aslMapManager_NewMapTransfer_LaunchNewServer_CB, pNewMapTransferData),
					GLOBALTYPE_MAPMANAGER, 0,
					pDescription, STACK_SPRINTF( "UGCFeaturedCopyProject, source proj: %u, new proj: %u", data->importProjID, data->newProjID ),
					pPatchInfo, &requesterInfo, estrCommandLine, false );

				estrDestroy( &estrCommandLine );
			}
		} else {
			UGCFeaturedCopyProjectReturn( data->pcShardName, data->entContainerID, false, "%s -- Newly created project %u does not exist", __FUNCTION__, data->newProjID );
			StructDestroy( parse_UGCFeaturedCopyProjectData, data );
		}
	} else {
		UGCFeaturedCopyProjectReturn( data->pcShardName, data->entContainerID, false, "%s -- %s", __FUNCTION__, GetTransactionFailureString( pReturn ));
		StructDestroy( parse_UGCFeaturedCopyProjectData, data );
	}
}

void UGCFeaturedCopyProject_CreateCB( TransactionReturnVal* pReturn, UGCFeaturedCopyProjectData* data )
{
	if( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) {
		data->newProjID = atoi(pReturn->pBaseReturnVals[0].returnString);

		AutoTrans_trUGCProjectFillInAfterCreation( objCreateManagedReturnVal( UGCFeaturedCopyProject_FillInCB, data ), GLOBALTYPE_UGCDATAMANAGER,
												   GLOBALTYPE_UGCPROJECT, data->newProjID, data->newProjID, data->importProjInfo );
	} else {
		UGCFeaturedCopyProjectReturn( data->pcShardName, data->entContainerID, false, "%s -- %s", __FUNCTION__, GetTransactionFailureString( pReturn ));
		StructDestroy( parse_UGCFeaturedCopyProjectData, data );
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_FeaturedCopyProject(ContainerID projectID, UGCFeaturedData* featuredData, const char *pcShardName, ContainerID entContainerID)
{
	UGCProject* toImport = objGetContainerData(GLOBALTYPE_UGCPROJECT, projectID);
	const UGCProjectVersion* toImportVersion = UGCProject_GetMostRecentPublishedVersion( toImport );

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if( !toImport || !toImportVersion ) {
		UGCFeaturedCopyProjectReturn( pcShardName, entContainerID, false, "%s -- Told to import from UGCProject %u, but it does not have a published version.", __FUNCTION__, projectID );
	} else if( !toImport->bAuthorAllowsFeatured ) {
		UGCFeaturedCopyProjectReturn( pcShardName, entContainerID, false, "%s -- Told to import from UGCProject %u, but it does not have bAuthorAllowsFeatured set.", __FUNCTION__, projectID );
	} else {
		UGCFeaturedCopyProjectData* data = StructCreate( parse_UGCFeaturedCopyProjectData );
		NOCONST(UGCProject)* newProj = StructCreateNoConst( parse_UGCProject );

		data->pcShardName = StructAllocString(pcShardName);
		data->entContainerID = entContainerID;
		data->importProjID = projectID;
		data->importProjInfo = ugcCreateProjectInfo( toImport, toImportVersion );
		data->importNamespace = StructAllocString( toImportVersion->pNameSpace );

		// Copy old project
		newProj->iOwnerAccountID = toImport->iOwnerAccountID;
		newProj->iCreationTime = timeSecondsSince2000();
		newProj->bFlaggedAsCryptic = toImport->bFlaggedAsCryptic;
		UGCProject_trh_SetOwnerAccountName(ATR_EMPTY_ARGS, newProj, toImport->pOwnerAccountName);
		newProj->iOwnerLangID = newProj->iOwnerLangID;
		StructCopyDeConst( parse_UGCProjectReviews, &toImport->ugcReviews, &newProj->ugcReviews, 0, 0, 0 );
		StructCopyDeConst( parse_UGCProjectStats, &toImport->ugcStats, &newProj->ugcStats, 0, 0, 0 );
		StructCopyDeConst( parse_UGCProjectTags, &toImport->ugcTags, &newProj->ugcTags, 0, 0, 0 );

		// Set featured appropriates
		newProj->bAuthorAllowsFeatured = true;
		newProj->pFeatured = StructCloneDeConst( parse_UGCFeaturedData, featuredData );
		newProj->bUGCFeaturedCopyProjectInProgress = true;
		newProj->uUGCFeaturedOrigProjectID = projectID;

		objRequestContainerCreate( objCreateManagedReturnVal( UGCFeaturedCopyProject_CreateCB, data ),
								   GLOBALTYPE_UGCPROJECT, newProj, GetAppGlobalType(), GetAppGlobalID() );
	}
}

void UGCProject_Delete_TransCB(TransactionReturnVal* pReturn, ContainerIdReturnData *pContainerIdReturnData)
{
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		AssertOrAlertWarning("UGC_PROJ_DESTROY_FAILURE", "Failed to destroy ugc project because %s", GetTransactionFailureString(pReturn));

		RemoteCommand_Intershard_UGCProject_Delete_Return(pContainerIdReturnData->pcShardName, GLOBALTYPE_LOGINSERVER, pContainerIdReturnData->loginServerID, false, pContainerIdReturnData->loginCookie);
	}
	else
		RemoteCommand_Intershard_UGCProject_Delete_Return(pContainerIdReturnData->pcShardName, GLOBALTYPE_LOGINSERVER, pContainerIdReturnData->loginServerID, true, pContainerIdReturnData->loginCookie);

	free(pContainerIdReturnData);
}

AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Id, .Ideletiontime, .ppProjectVersions, .seriesID, .bAuthorAllowsFeatured, .pFeatured")
ATR_LOCKS(pProjectSeries, ".ugcSearchCache.eaPublishedProjectIDs, .eaVersions");
enumTransactionOutcome trDeleteUgcProject(ATR_ARGS, NOCONST(UGCProject) *pProject, NOCONST(UGCProjectSeries) *pProjectSeries)
{
	pProject->iDeletionTime = timeSecondsSince2000();

	if(NONNULL(pProjectSeries))
	{
		NOCONST(UGCProjectSeriesVersion) *pUGCProjectSeriesVersion = NULL;
		int it;
		for(it = eaSize(&pProjectSeries->eaVersions) - 1; it >= 0; --it)
		{
			if(pProjectSeries->eaVersions[it]->eState == UGC_PUBLISHED)
			{
				pUGCProjectSeriesVersion = pProjectSeries->eaVersions[it];
				break;
			}
		}

		for(it = 0; it < eaSize(&pUGCProjectSeriesVersion->eaChildNodes); it++)
		{
			if(pUGCProjectSeriesVersion->eaChildNodes[it]->iProjectID == pProject->id)
			{
				eaRemove(&pUGCProjectSeriesVersion->eaChildNodes, it);
				break;
			}
		}

		pProject->seriesID = 0;
	}

	if( !ISNULL( pProject->pFeatured )) {
		return TRANSACTION_OUTCOME_FAILURE;
	}
	if( ugcDefaultsAuthorAllowsFeaturedBlocksEditing() && pProject->bAuthorAllowsFeatured ) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if( UGCProject_WithdrawProject(ATR_RECURSE, pProject, pProjectSeries, /*bWithdraw=*/true, "User deleted project")) {
		return TRANSACTION_OUTCOME_SUCCESS;
	} else {
		return TRANSACTION_OUTCOME_FAILURE;
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void UGCProject_Delete(ContainerID projectID, const char *pcShardName, U32 loginServerID, U64 loginCookie)
{
	ContainerIdReturnData* pContainerIdReturnData = NULL;
	UGCProject *pUGCProject = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	pUGCProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, projectID);
	if(!pUGCProject)
	{
		AssertOrAlertWarning("UGC_PROJ_DESTROY_FAILURE", "Failed to destroy ugc project because UGCProject does not exist: %u", projectID);

		RemoteCommand_Intershard_UGCProject_Delete_Return(pContainerIdReturnData->pcShardName, GLOBALTYPE_LOGINSERVER, loginServerID, false, loginCookie);
	}

	pContainerIdReturnData = calloc(1, sizeof(ContainerIdReturnData));
	pContainerIdReturnData->id = projectID;
	pContainerIdReturnData->pcShardName = StructAllocString(pcShardName);
	pContainerIdReturnData->loginServerID = loginServerID;
	pContainerIdReturnData->loginCookie = loginCookie;

	AutoTrans_trDeleteUgcProject(LoggedTransactions_CreateManagedReturnVal( __FUNCTION__, UGCProject_Delete_TransCB, pContainerIdReturnData ), GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, projectID,
		GLOBALTYPE_UGCPROJECTSERIES, pUGCProject->seriesID);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
char *GetProjectToImport(char *pNameSpace)
{
	static DynamicPatchInfo *pPatchInfo = NULL;
	ContainerID iID = UGCProject_GetContainerIDFromUGCNamespace(pNameSpace);
	UGCProject *pProject;
	static char *pRetString = NULL;
	
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if (!iID)
	{
		return "Badly formatted namespace";
	}

	pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, iID);

	if (!pProject)
	{
		return "Unknown project";
	}

	if (!pPatchInfo)
	{
		pPatchInfo = StructCreate(parse_DynamicPatchInfo);
		aslFillInPatchInfo(pPatchInfo, pNameSpace, 0);
	}

	estrPrintf(&pRetString, "<a href=\"http://%s/%s/file/data/ns/%s/project/%s.gz/\">Download link</a>",
		pPatchInfo->pServer, pPatchInfo->pUploadProject, pNameSpace, pProject->pIDString);

	return pRetString;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
char *WithdrawProject(ContainerID uUGCProjectID, const char *pComment)
{
	UGCProject *pProject = NULL;
	NOCONST(UGCProject) *pProjectNoConst = NULL;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, uUGCProjectID);

	if(!pProject)
		return "Unknown project";

	pProjectNoConst = CONTAINER_NOCONST(UGCProject, pProject);

	if(!UGCProject_CanBeWithdrawn(pProjectNoConst))
		return "Project is not in a state allowing it to be withdrawn";

	AutoTrans_trWithdrawProject(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, uUGCProjectID,
		GLOBALTYPE_UGCPROJECTSERIES, pProject->seriesID,
		pComment);

	return "Transaction executed";
}

void aslUGCDataManagerLibProjectNormalOperation(void)
{
	int iProjectNum;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	for (iProjectNum=0; iProjectNum < ea32Size(&spNewlyAddedProjectIDForRepubTestings); iProjectNum++)
	{
		UGCProject *pProj = objGetContainerData(GLOBALTYPE_UGCPROJECT, spNewlyAddedProjectIDForRepubTestings[iProjectNum]);
		if (pProj)
		{
			int i;
			bool bMostRecent = true;

			for (i=eaSize(&pProj->ppProjectVersions) - 1; i >= 0; i--)
			{
				UGCProjectVersion *pVersion = pProj->ppProjectVersions[i];
				static char *pWhy = NULL;
				estrClear(&pWhy);

				if (ugcProjectGetVersionStateConst(pVersion) == UGC_PUBLISHED)
				{
					if (TimeStampIndicatesRepubNeeded(&pVersion->sLastPublishTimeStamp, &pWhy))
					{
						if (bMostRecent)
						{
							if (NoRepublishBecauseHasntBeenTouchedRecently(pProj, pVersion))
							{
								estrInsertf(&pWhy, 0, "Was published, hasn't been touched in a long time, out of date because: ");
								AddVersionToChangedToNeedsUnplayableList(pProj->id, pVersion->pNameSpace, pWhy);
								RemoteCommand_gslUGC_SendMailForChange( GLOBALTYPE_WEBREQUESTSERVER, 0, pProj, pVersion, UGC_CHANGE_AUTOWITHDRAW );
							}
							else
							{
								estrInsertf(&pWhy, 0, "Was published, out of date because: ");
								AddVersionToChangedToNeedsRepublishingList(pProj->id, pVersion->pNameSpace, pWhy);
							}
						}
						else
						{
							estrInsertf(&pWhy, 0, "Was published, isn't most recent version, out of date because: ");
							AddVersionToChangedToNeedsUnplayableList(pProj->id, pVersion->pNameSpace, pWhy);
							RemoteCommand_gslUGC_SendMailForChange( GLOBALTYPE_WEBREQUESTSERVER, 0, pProj, pVersion, UGC_CHANGE_AUTOWITHDRAW );
						}
					}

					bMostRecent = false;
				}
				else if (ugcProjectGetVersionStateConst(pVersion) == UGC_WITHDRAWN)
				{
					if (TimeStampIndicatesRepubNeeded(&pVersion->sLastPublishTimeStamp, &pWhy))
					{
						estrInsertf(&pWhy, 0, "Was WITHDRAWN, TimeStampIndicatesRepubNeeded was true because: ");
						AddVersionToChangedToNeedsUnplayableList(pProj->id, pVersion->pNameSpace, pWhy);
					}
				}
			}
		}
	}

	ea32Destroy(&spNewlyAddedProjectIDForRepubTestings);

	{
		static U32 startedChecking = 0;

		if(0 == startedChecking)
			startedChecking = timeSecondsSince2000();

		if(timeSecondsSince2000() > startedChecking + UGC_DATA_MANAGER_NOT_SENDING_PLAYABLE_NAMESPACES_ALERT_DELAY) // start caring
		{
			// heartbeat to MapManager so we can alert as soon as MapManagers are unreachable
			if(s_LastTimePlayableNameSpacesSent == 0 || timeSecondsSince2000() > s_LastTimePlayableNameSpacesSent + UGC_DATA_MANAGER_NOT_SENDING_PLAYABLE_NAMESPACES_ALERT_PERIOD)
			{
				UGCPlayableNameSpaces ugcPlayableNameSpacesChanged;

				StructInit(parse_UGCPlayableNameSpaces, &ugcPlayableNameSpacesChanged);

				SendPlayableNameSpacesToAllMapManagersAndAlertIfNotSent(&ugcPlayableNameSpacesChanged, /*bReplace=*/false);

				StructReset(parse_UGCPlayableNameSpaces, &ugcPlayableNameSpacesChanged);
			}
		}
	}

	{
		static U32 uLastUGCPublishingDisabledCheck = 0;

		if(aslUGCDataManager_IsPublishDisabled())
		{
			if(uLastUGCPublishingDisabledCheck == 0 || uLastUGCPublishingDisabledCheck + 12*60*60 < timeSecondsSince2000()) // once per twelve hours
			{
				CRITICAL_NETOPS_ALERT("UGC_AUTHOR_PUBLISHING_DISABLED", "UGC author initiated publishing is disabled. This alert will occur every 12 hours until enabled.");

				uLastUGCPublishingDisabledCheck = timeSecondsSince2000();
			}
		}
		else
			uLastUGCPublishingDisabledCheck = 0;
	}
}

AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_ACCESSLEVEL(7);
char *RepublishAllMapsThatNeedRepublishing(bool bRegenerate, bool bBeaconize, char *pComment)
{
	UGCProjectFilteredList *pList;
	int iNumToRepublish;
	
	if (!(pComment && pComment[0]))
	{
		return "You must specify a comment";
	}

	pList = GetFilteredListOfUGCProjectsEx("1", 
		0, //bool bSAVED, 
		0, //bool bPUBLISH_BEGUN, 
		0, //bool bPUBLISH_FAILED, 
		0, //bool bPUBLISHED, 
		0, //bool bWITHDRAWN, 
		0, //bool bREPUBLISHING, 
		0, //bool bREPUBLISH_FAILED, 
		0, //bool bUNPLAYABLE, 
		1, //bool bNEEDS_REPUBLISHING,
		0, //bool bNEEDS_UNPLAYABLE,
		0); //bool bNEEDS_FIRST_PUBLISH)

	if (!pList)
	{
		return "Something went wrong, couldn't get a list";
	}

	if (!(iNumToRepublish = eaSize(&pList->ppVersions)))
	{
		return "Nothing needed republishing, doing nothing";
	}

	return RepublishFilteredList(pList->ID, bRegenerate, bBeaconize, pComment);
}

AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_ACCESSLEVEL(7);
char *RepublishAllMapsThatNeedFirstPublish(bool bRegenerate, bool bBeaconize, char *pComment)
{
	UGCProjectFilteredList *pList;
	int iNumToRepublish;

	if (!(pComment && pComment[0]))
	{
		return "You must specify a comment";
	}

	pList = GetFilteredListOfUGCProjectsEx("1", 
		0, //bool bSAVED, 
		0, //bool bPUBLISH_BEGUN,
		0, //bool bPUBLISH_FAILED, 
		0, //bool bPUBLISHED, 
		0, //bool bWITHDRAWN, 
		0, //bool bREPUBLISHING, 
		0, //bool bREPUBLISH_FAILED, 
		0, //bool bUNPLAYABLE, 
		0, //bool bNEEDS_REPUBLISHING,
		0, //bool bNEEDS_UNPLAYABLE,
		1); //bool bNEEDS_FIRST_PUBLISH)

	if (!pList)
	{
		return "Something went wrong, couldn't get a list";
	}

	if (!(iNumToRepublish = eaSize(&pList->ppVersions)))
	{
		return "Nothing needed republishing, doing nothing";
	}

	return RepublishFilteredList(pList->ID, bRegenerate, bBeaconize, pComment);
}

AUTO_COMMAND ACMD_CATEGORY(Ugc) ACMD_ACCESSLEVEL(7);
char *MakeUnplayableAllProjectsThatNeedToBeUnplayable(char *pComment)
{
	UGCProjectFilteredList *pList;
	int iNumToMakeUnplayable;

	if (!(pComment && pComment[0]))
	{
		return "You must specify a comment";
	}

	pList = GetFilteredListOfUGCProjectsEx("1", 
		0, //bool bSAVED, 
		0, //bool bPUBLISH_BEGUN, 
		0, //bool bPUBLISH_FAILED, 
		0, //bool bPUBLISHED, 
		0, //bool bWITHDRAWN, 
		0, //bool bREPUBLISHING, 
		0, //bool bREPUBLISH_FAILED, 
		0, //bool bUNPLAYABLE, 
		0, //bool bNEEDS_REPUBLISHING,
		1, //bool bNEEDS_UNPLAYABLE,
		0); //bool bNEEDS_FIRST_PUBLISH)

	if (!pList)
	{
		return "Something went wrong, couldn't get a list";
	}

	if (!(iNumToMakeUnplayable = eaSize(&pList->ppVersions)))
	{
		return "Nothing needed to be made unplayable, doing nothing";
	}

	return SetStateForFilteredList(pList->ID, "UNPLAYABLE", 0, pComment);
}

AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_ACCESSLEVEL(7);
void CreateUGCAccountsForPublishedProjects( void )
{
	ContainerIterator it = { 0 };
	UGCProject* ugcProj = NULL;
	UGCProjectSeries* ugcSeries = NULL;
	
	objInitContainerIteratorFromType( GLOBALTYPE_UGCPROJECT, &it );
	while( ugcProj = objGetNextObjectFromIterator( &it )) {
		if( UGCProject_GetMostRecentPublishedVersion( ugcProj )) {
			UGCAccountEnsureExists( ugcProj->iOwnerAccountID, NULL, NULL );
		}
	}

	objInitContainerIteratorFromType( GLOBALTYPE_UGCPROJECTSERIES, &it );
	while( ugcSeries = objGetNextObjectFromIterator( &it )) {
		if( UGCProjectSeries_GetMostRecentPublishedVersion( ugcSeries )) {
			UGCAccountEnsureExists( ugcSeries->iOwnerAccountID, NULL, NULL );
		}
	}
}

static void UGCProjectInfo_Create_CB(TransactionReturnVal *pReturn, void *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		printf("Created UGC project %d\n", atoi(pReturn->pBaseReturnVals[0].returnString));
	} 
	else 
	{
		printf("Creation failed: %s\n", GetTransactionFailureString(pReturn));
	}
}

AUTO_COMMAND;
void testCreateUGCProject(void)
{
	NOCONST(UGCProject) *pUgcProject;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to create a UGC container!");

	pUgcProject = StructCreateNoConst(parse_UGCProject);

	pUgcProject->iOwnerAccountID = 3;
	pUgcProject->iCreationTime = timeSecondsSince2000();
	pUgcProject->pOwnerAccountName = strdup("awerner");

	objRequestContainerCreate(objCreateManagedReturnVal(UGCProjectInfo_Create_CB, NULL), GLOBALTYPE_UGCPROJECT, pUgcProject, GetAppGlobalType(), GetAppGlobalID());

	StructDestroyNoConst(parse_UGCProject, pUgcProject);
}

const char *GetEditMapNameFromUGCProject(const UGCProject *pProject)
{
    char buf[RESOURCE_NAME_MAX_SIZE];
    UGCProjectVersion *pMostRecentVersion = eaTail(&pProject->ppProjectVersions);
    assert(pMostRecentVersion);
    sprintf(buf, "%s:EmptyMap", pMostRecentVersion->pNameSpace);
    return allocAddString(buf);
}

const char *GetEditMapNameFromPossibleUGCProject(const PossibleUGCProject *pPossibleUGCProject, bool bCopy)
{
	char buf[RESOURCE_NAME_MAX_SIZE];
	sprintf(buf, "%s:EmptyMap", bCopy ? pPossibleUGCProject->strCopyEditVersionNamespace : pPossibleUGCProject->strEditVersionNamespace);
	return allocAddString(buf);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".iOwnerAccountID, .pOwnerAccountName, .pOwnerAccountName_ForSearching, .bFlaggedAsCryptic");
enumTransactionOutcome trTransferProjectOwnershipToUserByIDWithName(ATR_ARGS, NOCONST(UGCProject)* pUGCProject, ContainerID uUGCAccountID, const char *pcAccountName)
{
	if(uUGCAccountID)
	{
		pUGCProject->iOwnerAccountID = uUGCAccountID;

		UGCProject_trh_SetOwnerAccountName(ATR_PASS_ARGS, pUGCProject, pcAccountName);

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else
		return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProjectSeries, ".iOwnerAccountID, .strOwnerAccountName, .bFlaggedAsCryptic");
enumTransactionOutcome trTransferProjectSeriesOwnershipToUserByIDWithName(ATR_ARGS, NOCONST(UGCProjectSeries)* pUGCProjectSeries, ContainerID uUGCAccountID, const char *pcAccountName)
{
	if(uUGCAccountID)
	{
		pUGCProjectSeries->iOwnerAccountID = uUGCAccountID;
		
		UGCProjectSeries_trh_SetOwnerAccountName(ATR_PASS_ARGS, pUGCProjectSeries, pcAccountName);

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else
		return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcTransferProjectOwnershipToUserByIDWithName(ContainerID uUGCProjectID, ContainerID uUGCAccountID, const char *pcAccountName)
{
	const UGCProject *pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, uUGCProjectID);

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if(uUGCAccountID && pProject)
	{
		const UGCProjectSeries *pProjectSeries = NULL;
		if(pProject->seriesID)
			pProjectSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, pProject->seriesID);

		if(pProjectSeries)
		{
			const UGCProjectSeriesVersion *pUGCProjectSeriesVersion = eaTail(&pProjectSeries->eaVersions);
			int i;
			for(i = 0; i < eaSize(&pUGCProjectSeriesVersion->eaChildNodes); i++)
			{
				AutoTrans_trTransferProjectOwnershipToUserByIDWithName(NULL, GetAppGlobalType(),
					GLOBALTYPE_UGCPROJECT, pUGCProjectSeriesVersion->eaChildNodes[i]->iProjectID,
					uUGCAccountID, pcAccountName);
			}

			AutoTrans_trTransferProjectSeriesOwnershipToUserByIDWithName(NULL, GetAppGlobalType(),
				GLOBALTYPE_UGCPROJECTSERIES, pProjectSeries->id,
				uUGCAccountID, pcAccountName);
		}
		else
		{
			AutoTrans_trTransferProjectOwnershipToUserByIDWithName(NULL, GetAppGlobalType(),
				GLOBALTYPE_UGCPROJECT, pProject->id,
				uUGCAccountID, pcAccountName);
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".bFlaggedAsCryptic, .pOwnerAccountName, .pOwnerAccountName_ForSearching");
enumTransactionOutcome trFlagProjectAsCryptic(ATR_ARGS, NOCONST(UGCProject)* pUGCProject, int iFlaggedAsCryptic)
{
	bool bFlaggedAsCryptic = !!iFlaggedAsCryptic;
	if(bFlaggedAsCryptic != pUGCProject->bFlaggedAsCryptic)
	{
		pUGCProject->bFlaggedAsCryptic = bFlaggedAsCryptic;

		UGCProject_trh_SetOwnerAccountName(ATR_PASS_ARGS, pUGCProject, pUGCProject->pOwnerAccountName);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProjectSeries, ".bFlaggedAsCryptic, .strOwnerAccountName");
enumTransactionOutcome trFlagProjectSeriesAsCryptic(ATR_ARGS, NOCONST(UGCProjectSeries)* pUGCProjectSeries, int iFlaggedAsCryptic)
{
	bool bFlaggedAsCryptic = !!iFlaggedAsCryptic;
	if(bFlaggedAsCryptic != pUGCProjectSeries->bFlaggedAsCryptic)
	{
		pUGCProjectSeries->bFlaggedAsCryptic = bFlaggedAsCryptic;

		UGCProjectSeries_trh_SetOwnerAccountName(ATR_PASS_ARGS, pUGCProjectSeries, pUGCProjectSeries->strOwnerAccountName);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void ugcFlagProjectSeriesAsCryptic_Internal(const UGCProjectSeries *pProjectSeries, bool bFlaggedAsCryptic)
{
	if(pProjectSeries)
	{
		const UGCProjectSeriesVersion *pUGCProjectSeriesVersion = eaTail(&pProjectSeries->eaVersions);
		int i;
		for(i = 0; i < eaSize(&pUGCProjectSeriesVersion->eaChildNodes); i++)
		{
			AutoTrans_trFlagProjectAsCryptic(NULL, GetAppGlobalType(),
				GLOBALTYPE_UGCPROJECT, pUGCProjectSeriesVersion->eaChildNodes[i]->iProjectID, bFlaggedAsCryptic);
		}

		AutoTrans_trFlagProjectSeriesAsCryptic(NULL, GetAppGlobalType(),
			GLOBALTYPE_UGCPROJECTSERIES, pProjectSeries->id, bFlaggedAsCryptic);
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcFlagProjectAsCryptic(ContainerID uUGCProjectID, bool bFlaggedAsCryptic)
{
	const UGCProject *pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, uUGCProjectID);

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if(pProject)
	{
		const UGCProjectSeries *pProjectSeries = NULL;
		if(pProject->seriesID)
			pProjectSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, pProject->seriesID);

		if(pProjectSeries)
		{
			ugcFlagProjectSeriesAsCryptic_Internal(pProjectSeries, bFlaggedAsCryptic);
		}
		else
		{
			AutoTrans_trFlagProjectAsCryptic(NULL, GetAppGlobalType(),
				GLOBALTYPE_UGCPROJECT, pProject->id, bFlaggedAsCryptic);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcFlagProjectSeriesAsCryptic(ContainerID uUGCProjectSeriesID, bool bFlaggedAsCryptic)
{
	const UGCProjectSeries *pProjectSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, uUGCProjectSeriesID);

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	ugcFlagProjectSeriesAsCryptic_Internal(pProjectSeries, bFlaggedAsCryptic);
}

AUTO_COMMAND_REMOTE;
int FlushResDBCacheOnEachShard_ForJob(char *pNameSpaceName, char *pJobName)
{
	Cluster_Overview *pOverview = GetShardClusterOverview_EvenIfNotInCluster();
	if(pOverview) // This code path is taken by UGCDataManagers in a shard cluster, as well as a single shard launched by ShardLauncher
	{
		bool bAllSent = true;

		FOR_EACH_IN_EARRAY(pOverview->ppShards, ClusterShardSummary, pClusterShard)
		{
			if(pClusterShard->eState == CLUSTERSHARDSTATE_CONNECTED || pClusterShard->eState == CLUSTERSHARDSTATE_THATS_ME)
			{
				ClusterServerTypeStatus *pClusterServerTypeStatus = NULL;
				if(pClusterShard->pMostRecentStatus)
					pClusterServerTypeStatus = eaIndexedGetUsingInt(&pClusterShard->pMostRecentStatus->ppServersByType, GLOBALTYPE_RESOURCEDB);

				if(pClusterServerTypeStatus && pClusterServerTypeStatus->ppServers[0] && 0 == stricmp(pClusterServerTypeStatus->ppServers[0]->pStateString, "ready"))
					RemoteCommand_Intershard_FlushResDBCache_ForJob(pClusterShard->pShardName, GLOBALTYPE_RESOURCEDB, 0, pNameSpaceName, pJobName);
				else
					bAllSent = false;
			}
		}
		FOR_EACH_END;

		return bAllSent;
	}

	return false;
}

AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_NAME(ugcResetAchievementCountersForAllAccounts) ACMD_ACCESSLEVEL(7);
char *aslUGCDataManager_ResetAchievementCountersForAllAccounts()
{
	if(GetAppGlobalType() == GLOBALTYPE_UGCDATAMANAGER)
	{
		UGCAccount *pUGCAccount = NULL;
		ContainerIterator iter;

		objInitContainerIteratorFromType(GLOBALTYPE_UGCACCOUNT, &iter);
		while((pUGCAccount = objGetNextObjectFromIterator(&iter)))
			AutoTrans_trUgcResetAchievements(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCACCOUNT, pUGCAccount->accountID);

		return "All UGC Accounts have had their Achievement counters reset.";
	}
	else
		return "This command can only be run on the UGCDataManager.";
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProjectSeries, ".id, .strIDString");
enumTransactionOutcome trUgcFixupSeriesIDString(ATR_ARGS, NOCONST(UGCProjectSeries) *pUGCProjectSeries)
{
	char IDString[UGC_IDSTRING_LENGTH_BUFFER_LENGTH];

	UGCIDString_IntToString(pUGCProjectSeries->id, /*isSeries=*/true, IDString);
	pUGCProjectSeries->strIDString = strdup(IDString);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_CATEGORY(UGC) ACMD_NAME(ugcFixupSeriesIDStrings) ACMD_ACCESSLEVEL(7);
char *aslUGCDataManager_FixupSeriesIDStrings()
{
	if(GetAppGlobalType() == GLOBALTYPE_UGCDATAMANAGER)
	{
		UGCProjectSeries *pUGCProjectSeries = NULL;
		ContainerIterator iter;

		objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECTSERIES, &iter);
		while((pUGCProjectSeries = objGetNextObjectFromIterator(&iter)))
			AutoTrans_trUgcFixupSeriesIDString(NULL, GLOBALTYPE_UGCDATAMANAGER, GLOBALTYPE_UGCPROJECTSERIES, pUGCProjectSeries->id);

		return "All UGC Project Series have had their ID strings fixed up.";
	}
	else
		return "This command can only be run on the UGCDataManager.";
}

#include "aslUGCDataManagerProject_c_ast.c"
