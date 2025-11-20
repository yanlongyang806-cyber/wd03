/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclPatching.h"
#include "GlobalComm.h"
#include "GameClientLib.h"
#include "gclBaseStates.h"
#include "GlobalStateMachine.h"
#include "pcl_client.h"
#include "sysutil.h"
#include "FolderCache.h"
#include "hoglib.h"
#include "file.h"
#include "ScratchStack.h"
#include "GraphicsLib.h" // gfxDisplayLogo
#include "patchtrivia.h"
#include "earray.h"
#include "GlobalTypes.h"
#include "MapDescription.h"
#include "gclSendToServer.h"
#include "gclLogin.h"
#include "trivia.h"
#include "StashTable.h"
#include "patchcommonutils.h"
#include "fileutil.h"

#include "AutoGen/gclPatching_c_ast.h"
#include "AutoGen/MapDescription_h_ast.h"

#if _XBOX
#include "Xbox.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define MIN_HOGG_SIZE (100*1024*1024)	// 100MB
#define MAX_HOGG_SIZE (500*1024*1024)	// 500MB
#define AGE_OUT_TIME (14*24*60*60)		// 14 days
#define MAX_PATCH_ATTEMPTS 3

static PCL_Client * g_patch_client = NULL;			// Active PatchClientLib link to Patch Server
static DynamicPatchInfo *g_patch_info = NULL;		// Information about what to patch to
static TriviaMutex g_patch_mutex;					// Lock around entire client patching process
static unsigned g_patch_retries = 0;				// Number of times we've retried patching.

bool g_pruning_enabled = true;
AUTO_CMD_INT(g_pruning_enabled, patch_pruning) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

bool g_patch_metadata_in_memory = true;
AUTO_CMD_INT(g_patch_metadata_in_memory, patch_metadata_in_memory) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

// If set, verify each patched file after it is patched.
bool g_patch_verify = true;
AUTO_CMD_INT(g_patch_verify, patch_verify) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

// If set, go through the motions of dynamic patching even if we're not in production mode.
bool g_force_dynamic_patch = false;
AUTO_CMD_INT(g_force_dynamic_patch, force_dynamic_patch) ACMD_CMDLINE;

AUTO_STRUCT;
typedef struct DynamicPatchLogEntry
{
	char *name; AST(STRUCTPARAM KEY)
	time_t atime; AST(INT STRUCTPARAM)
} DynamicPatchLogEntry;

AUTO_STRUCT;
typedef struct DynamicPatchLog
{
	DynamicPatchLogEntry **entries; AST(NAME(Entry))
} DynamicPatchLog;

static DynamicPatchLog *g_patch_log=NULL;

DynamicPatchInfo *gclPatching_GetPatchInfo(void)
{
	return g_patch_info;
}

void gclPatching_SetPatchInfo(DynamicPatchInfo *info)
{
	if(info)
	{
		if(!g_patch_info)
			g_patch_info = StructCreate(parse_DynamicPatchInfo);
		StructCopy(parse_DynamicPatchInfo, info, g_patch_info, 0, 0, 0);
	}
	else if(g_patch_info)
	{
		StructDestroy(parse_DynamicPatchInfo, g_patch_info);
		g_patch_info = NULL;
	}
}


// PATCHING STATE MACHINE
//////////////////////////////////////////////////////////////////////////

static void patchStop(void);
static void patchStart(void);

static char *patchGetRoot(char *out, size_t out_len)
{
#if _XBOX
	strcpy_s(out, out_len, "data:\\");
#elif _PS3
	assertmsg(0, "Client patching not supported on PS3");
#else
	char *s;
	strcpy_s(out, out_len, fileDataDir());
	s = strrchr(out,'/');
	if (s) // If not fileDataDir must be "."
		*s = 0;
#endif
	return out;
}

static char *patchGetMutexName(char *out, size_t out_size)
{
	char cwd[MAX_PATH], *c;
	assert(getcwd(cwd, ARRAY_SIZE_CHECKED(cwd)));
	if(c = strchr(cwd, ':'))
		*c = '_';
	sprintf_s(SAFESTR2(out), "gcl_patch\\%s", cwd);
	return out;
}

static HogFile *prune_hogg;
static time_t prune_threshold;
static int prune_i, prune_count;
static bool sbOrphansPrunedOnce = false;

static bool patchPruneGetHoggSizeCB(HogFile *handle, HogFileIndex index, const char* filename, U64 * userData)
{
	U32 unpacked, packed;
	hogFileGetSizes(handle, index, &unpacked, &packed);
	if(packed)
		*userData += packed;
	else
		*userData += unpacked;
	return true;
}

static U64 patchPruneGetHoggSize(HogFile *hogg)
{
	U64 size = 0;
	hogScanAllFiles(hogg, patchPruneGetHoggSizeCB, &size);
	return size;
}

static bool patchPruneDeleteMap(HogFile *handle, HogFileIndex index, const char* filename, char* prefix)
{
	if(strStartsWith(filename, prefix))
		hogFileModifyDelete(handle, index);
	return true;
}

static void patchPruneEnter(void)
{
	char path[MAX_PATH];

	loadstart_printf("Pruning old maps");
	fileSpecialDir("piggs", SAFESTR(path));
	strcat(path, "/dynamic.hogg");

	prune_hogg = hogFileRead(path, NULL, PIGERR_QUIET, NULL, HOG_NOCREATE);
	if(!prune_hogg)
	{
		loadend_printf("(skipped)");
		GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_CONNECT);
		return;
	}

	if(!sbOrphansPrunedOnce)
	{
		GSM_SwitchToState_Complex(GCL_PATCH_PRUNE "/" GCL_PATCH_PRUNE_ORPHANED);
	}
	else if(patchPruneGetHoggSize(prune_hogg) > MIN_HOGG_SIZE)
	{
		prune_count = 0;
		GSM_SwitchToState_Complex(GCL_PATCH_PRUNE "/" GCL_PATCH_PRUNE_OLD);
	}
	else
	{
		GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_CONNECT);
	}
}

static void patchPruneLeave(void)
{
	loadend_printf("(%d removed)", prune_count);
}

static StashTable sKnownPatchPrefixes = NULL;
static int siOrphansPruned = 0;

static bool patchPruneDeleteOrphaned(HogFile *handle, HogFileIndex index, const char* filename, void *userData)
{
	char fn[CRYPTIC_MAX_PATH];
	char *secondSlash = NULL;

	fn[0] = 0;

	strcpy(fn, filename);
	secondSlash = strchr(fn, '/');
	secondSlash = strchr(secondSlash+1, '/');
	*secondSlash = 0;

	if(!stashFindElement(sKnownPatchPrefixes, fn, NULL))
	{
		hogFileModifyDelete(handle, index);
		++siOrphansPruned;
	}

	return true;
}

static void patchPruneOrphanedEnter(void)
{
	int i;
	char prefix[CRYPTIC_MAX_PATH];

	prefix[0] = 0;

	loadstart_printf("Pruning orphaned files...");

	for(i = 0; i < eaSize(&g_patch_log->entries); ++i)
	{
		strcpy(prefix, g_patch_log->entries[i]->name + MIN(strlen(g_patch_log->entries[i]->name), 6));

		if(!sKnownPatchPrefixes)
		{
			sKnownPatchPrefixes = stashTableCreateWithStringKeys(8, StashDeepCopyKeys);
		}

		stashAddInt(sKnownPatchPrefixes, prefix, 1, true);
	}

	hogScanAllFiles(prune_hogg, patchPruneDeleteOrphaned, NULL);
	sbOrphansPrunedOnce = true;
	stashTableDestroy(sKnownPatchPrefixes);

	// Defrag the hogg if it is at least twice as big as the data inside it
	// TODO: Find out if this is possible - AWL or VAS?
/*
	if(hogFileCalcFragmentation(prune_hogg) > 0.5)
	{
		char path[MAX_PATH] = "";

		hogFileDestroy(prune_hogg, true);
		prune_hogg = NULL;

		fileSpecialDir("piggs", SAFESTR(path));
		strcat(path, "/dynamic.hogg");

		if(hogDefrag(path, 0, false))
		{
			Errorf("Failed to defrag dynamic.hogg! No changes were made to the hogg in place, however.");
		}

		prune_hogg = hogFileRead(path, NULL, PIGERR_QUIET, NULL, HOG_NOCREATE);
	}
*/

	loadend_printf("...completed. (%d pruned)\n", siOrphansPruned);

	if(patchPruneGetHoggSize(prune_hogg) > MIN_HOGG_SIZE)
	{
		GSM_SwitchToState_Complex(GCL_PATCH_PRUNE "/" GCL_PATCH_PRUNE_OLD);
	}
	else
	{
		GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_CONNECT);
	}
}

static void patchPruneOldEnter(void)
{
	prune_threshold = time(NULL) - AGE_OUT_TIME;
	prune_i = eaSize(&g_patch_log->entries) - 1;
	
}

static void patchPruneOldFrame(void)
{
	char prune_prefix[CRYPTIC_MAX_PATH];

	prune_prefix[0] = 0;

	for(; prune_i >= 0; prune_i--)
	{
		if(g_patch_log->entries[prune_i]->atime <= prune_threshold)
		{
			loadstart_printf("Pruning %s", g_patch_log->entries[prune_i]->name);
			
			// NOTE VAS 2010-08-20: We chop off the first 6 characters of the prefix because they
			// contain the string '/data/' which is part of the patch prefix but NOT part of the file
			// paths in the hogg. However, if the hogg's implicit file path prefix is changed at some
			// point in the future, then this MUST BE CHANGED!!
			strcpy(prune_prefix, g_patch_log->entries[prune_i]->name + MIN(strlen(g_patch_log->entries[prune_i]->name), 6));
			
			hogScanAllFiles(prune_hogg, patchPruneDeleteMap, prune_prefix);
			eaRemove(&g_patch_log->entries, prune_i);
			prune_count += 1;
			--prune_i;
			loadend_printf("");
			break;
		}
	}

	if(prune_i < 0)
	{
		U64 total_size = patchPruneGetHoggSize(prune_hogg);
		if(total_size >= MAX_HOGG_SIZE)
		{
			GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_PRUNE "/" GCL_PATCH_PRUNE_LRU);
		}
		else
		{
			GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_CONNECT);
		}
	}
}

static void patchPruneLRUFrame(void)
{
	int i, last_i = -1;
	char prune_prefix[CRYPTIC_MAX_PATH];

	prune_prefix[0] = 0;

	for(i=eaSize(&g_patch_log->entries)-1; i>=0; i--)
	{
		if(last_i == -1 || g_patch_log->entries[i]->atime < g_patch_log->entries[last_i]->atime)
		{
			last_i = i;
		}
	}

	if(last_i != -1)
	{
		U64 size;
		loadstart_printf("Pruning %s", g_patch_log->entries[last_i]->name);
		
		// NOTE VAS 2010-08-20: We chop off the first 6 characters of the prefix because they
		// contain the string '/data/' which is part of the patch prefix but NOT part of the file
		// paths in the hogg. However, if the hogg's implicit file path prefix is changed at some
		// point in the future, then this MUST BE CHANGED!!
		strcpy(prune_prefix, g_patch_log->entries[last_i]->name + MIN(strlen(g_patch_log->entries[last_i]->name), 6));
		
		hogScanAllFiles(prune_hogg, patchPruneDeleteMap, prune_prefix);
		eaRemove(&g_patch_log->entries, last_i);
		prune_count += 1;

		loadstart_printf("Getting hogg size");
		size = patchPruneGetHoggSize(prune_hogg);
		loadend_printf("");

		loadend_printf("");

		if(size < MAX_HOGG_SIZE)
		{
			GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_CONNECT);
		}
	}
	else
	{
		Errorf("Unable to prune dynamic patch hogg below max size.");
		GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_CONNECT);
	}
}

static void patchingFail(PCL_ErrorCode error, const char *extra_error)
{
	char buf[1024], buf2[1024];
	const char *server = "(no client)";

	// Report the error.
	pclGetErrorString(error, SAFESTR(buf));
	pclGetConnectedServer(g_patch_client, &server);
	triviaPrintf("dynpatch.server", "%s", server?server:"(no client)");
	triviaPrintf("dynpatch.error", "%d", error);
	triviaPrintf("dynpatch.retries", "%d", g_patch_retries);
	Errorf("Client dynamic patch failure: %s%s%s", buf, extra_error ? ", " : "", extra_error);

	// Retry a few times in case this is an itermittent or correctable problem.
	++g_patch_retries;
	if (g_patch_retries < MAX_PATCH_ATTEMPTS)
	{
		g_patch_log = NULL;
		patchStop();
		patchStart();
		return;
	}

	// Otherwise, return to login screen.
	sprintf(buf2, "Dynamic patch failure: %s", buf);
	gclLoginFailNotDuringNormalLogin(buf2);
}

static void patchingConnectCallback(PCL_Client * client, bool updated, PCL_ErrorCode error, const char * error_details, void * userData)
{
	if(error == PCL_SUCCESS)
	{
		pclVerifyAllFiles(client, g_patch_verify);
		if(g_patch_metadata_in_memory)
			pclAddFileFlags(client, PCL_METADATA_IN_MEMORY);
		GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_SET_VIEW);
	}
	else
	{
		patchingFail(error, error_details);
	}
}

static void patchConnectEnter(void)
{

	char root[MAX_PATH];
	PCL_ErrorCode error;
	char extra_error[256] = {0};

	if(!g_patch_info)
	{
		GSM_SwitchToState_Complex(GCL_PATCH "/ .." );
		return;
	}

	patchGetRoot(SAFESTR(root));
	error = pclConnectAndCreateEx(&g_patch_client, g_patch_info->pServer, g_patch_info->iPort, g_patch_info->iTimeout, commDefault(), root, "GameClient", NULL, patchingConnectCallback, NULL, NULL, SAFESTR(extra_error));

	if(error)
	{
		patchingFail(error, *extra_error ? extra_error : NULL);
	}
}

static void patchingSetViewCallback(PCL_Client * client, PCL_ErrorCode error, const char * error_details, void * userData)
{
	if(error == PCL_SUCCESS)
	{
		loadend_printf("");
		GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_XFER);
	}
	else
	{
		patchingFail(error, error_details);
	}
}

static void patchSetViewEnter(void)
{
	loadstart_printf("Setting view to %s branch %d %s", g_patch_info->pClientProject, g_patch_info->iBranch, NULL_TO_EMPTY(g_patch_info->pPrefix));
	if(g_patch_info->pPrefix)
		pclSetPrefix(g_patch_client, g_patch_info->pPrefix);
	if(g_patch_info->pViewName)
		pclSetNamedView(g_patch_client, g_patch_info->pClientProject, g_patch_info->pViewName, true, false, patchingSetViewCallback, NULL);
	else
		pclSetViewLatest(g_patch_client, g_patch_info->pClientProject, g_patch_info->iBranch, NULL, true, false, patchingSetViewCallback, NULL);
}

static float patchingProgressPercentage = 0;
static char patchingProgressStatus[1024] = {0};
static bool patchingProcessCallback(PatchProcessStats *stats, void *userData)
{
	F32 rec_num, tot_num, act_num;
	char *rec_units, *tot_units, *act_units;
	U32 rec_prec, tot_prec, act_prec;
	double percent;
	U32 compression;
	F32 total_time;

	percent = stats->received * 100.0 / stats->total;
	compression = stats->received ? stats->actual_transferred * 100 / stats->received : 0;
	total_time = stats->received ? stats->elapsed * stats->total / stats->received : 0;
	humanBytes(stats->received, &rec_num, &rec_units, &rec_prec);
	humanBytes(stats->total, &tot_num, &tot_units, &tot_prec);
	humanBytes(stats->actual_transferred, &act_num, &act_units, &act_prec);

	patchingProgressPercentage = percent;
	sprintf(patchingProgressStatus, "%.1f%%  Time: %d:%.2d/%d:%.2d  Files: %d/%d  Data: %.*f%s/%.0f%s  Data transferred: %.*f%s",
		percent, (U32)stats->elapsed / 60, (U32)stats->elapsed % 60, (U32)total_time / 60, (U32)total_time % 60,
		stats->received_files, stats->total_files, rec_prec, rec_num, rec_units, tot_num, tot_units, act_prec, act_num, act_units);

	return false;
}

AUTO_EXPR_FUNC(UIGen);
float gclPatchingGetProgressPercentage(void)
{
	return patchingProgressPercentage;
}

AUTO_EXPR_FUNC(UIGen);
char *gclPatchingGetProgressStatus(void)
{
	return patchingProgressStatus;
}

static void patchingGetRequired(PCL_Client * client, PCL_ErrorCode error, const char * error_details, void * userData)
{
	if(error == PCL_SUCCESS)
	{
		GSM_SwitchToState_Complex(GCL_PATCH "/..");
	}
	else
	{
		patchingFail(error, error_details);
	}
}

static void patchXferEnter(void)
{
	if(g_patch_info->pPrefix && g_patch_info->pPrefix[0])
	{
		DynamicPatchLogEntry *entry = eaIndexedGetUsingString(&g_patch_log->entries, g_patch_info->pPrefix);
		if(!entry)
		{
			entry = StructCreate(parse_DynamicPatchLogEntry);
			entry->name = StructAllocString(g_patch_info->pPrefix);
			eaIndexedAdd(&g_patch_log->entries, entry);
		}
		entry->atime = time(NULL); //{	patchAddLogEntry(g_patch_info->pPrefix);}
	}
	pclSetProcessCallback(g_patch_client, patchingProcessCallback, NULL);
	pclGetAllFiles(g_patch_client, patchingGetRequired, NULL, NULL);
}

static void patchEnter(void)
{
	g_patch_retries = 0;
	patchStart();
}

static void patchStart(void)
{
	char path[MAX_PATH], mutex_name[MAX_PATH];
	loadstart_printf("Patching map...");

	// (Re-)init global status
	patchingProgressPercentage = 0;
	patchingProgressStatus[0] = '\0';

	// Acquire GCL_PATCH mutex for this folder
	patchGetMutexName(SAFESTR(mutex_name));
	g_patch_mutex = triviaAcquireDumbMutex(mutex_name);

	// Load the dynamic patch log if present
	loadstart_printf("Reading dynamic patch log");
	patchGetRoot(SAFESTR(path));
	strcat(path, "/.patch/dynamiclog.txt");
	if(g_patch_log)
		StructDestroy(parse_DynamicPatchLog, g_patch_log);
	g_patch_log = StructCreate(parse_DynamicPatchLog);
	ParserReadTextFile(path, parse_DynamicPatchLog, g_patch_log, PARSER_OPTIONALFLAG);
	loadend_printf("(%d entries)", eaSize(&g_patch_log->entries));

	if(g_pruning_enabled)
		GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_PRUNE);
	else
		GSM_SwitchToState_Complex(GCL_PATCH "/" GCL_PATCH_CONNECT);
}

static void patchFrame(void)
{
	PCL_ErrorCode error = PCL_SUCCESS;

	gclServerMonitorConnection();

	// Process PatchClientLib connection, if any.
	if(g_patch_client)
		error = pclProcess(g_patch_client);

	// Check for PCL problems.
	if(error && !(error == PCL_WAITING || error == PCL_LOST_CONNECTION))
	{
		patchingFail(error, NULL);
	}
	else if(g_patch_client && pclCheckTimeout(g_patch_client))
	{
		pclSendLog(g_patch_client, "gclPatchingTimeout", "");
		patchingFail(PCL_TIMED_OUT, NULL);
	}
}

static void patchLeave(void)
{
	patchStop();
}

static void patchStop(void)
{
	if(g_patch_client)
	{
		pclSetHoggsSingleAppMode(g_patch_client, false);
		pclDisconnectAndDestroy(g_patch_client);
		g_patch_client = NULL;
	}

	// Write out the patch log
	if(g_patch_log)
	{
		char path[MAX_PATH];
		int n;
		loadstart_printf("Writing dynamic patch log");
		patchGetRoot(SAFESTR(path));
		strcat(path, "/.patch/dynamiclog.txt");
		ParserWriteTextFile(path, parse_DynamicPatchLog, g_patch_log, 0, 0);
		n = eaSize(&g_patch_log->entries);
		StructDestroy(parse_DynamicPatchLog, g_patch_log);
		g_patch_log = NULL;
		loadend_printf("(%d entries)", n);
	}

	// Release GCL_PATCH mutex
	triviaReleaseDumbMutex(g_patch_mutex);

	loadend_printf("");
}

AUTO_RUN;
void gclPatching_AutoRegister(void)
{
	GSM_AddGlobalState(GCL_PATCH);
	GSM_AddGlobalStateCallbacks(GCL_PATCH, patchEnter, patchFrame, NULL, patchLeave);

	GSM_AddGlobalState(GCL_PATCH_PRUNE);
	GSM_AddGlobalStateCallbacks(GCL_PATCH_PRUNE, patchPruneEnter, NULL, NULL, patchPruneLeave);

	GSM_AddGlobalState(GCL_PATCH_PRUNE_ORPHANED);
	GSM_AddGlobalStateCallbacks(GCL_PATCH_PRUNE_ORPHANED, patchPruneOrphanedEnter, NULL, NULL, NULL);

	GSM_AddGlobalState(GCL_PATCH_PRUNE_OLD);
	GSM_AddGlobalStateCallbacks(GCL_PATCH_PRUNE_OLD, patchPruneOldEnter, patchPruneOldFrame, NULL, NULL);

	GSM_AddGlobalState(GCL_PATCH_PRUNE_LRU);
	GSM_AddGlobalStateCallbacks(GCL_PATCH_PRUNE_LRU, NULL, patchPruneLRUFrame, NULL, NULL);

	GSM_AddGlobalState(GCL_PATCH_CONNECT);
	GSM_AddGlobalStateCallbacks(GCL_PATCH_CONNECT, patchConnectEnter, NULL, NULL, NULL);

	GSM_AddGlobalState(GCL_PATCH_SET_VIEW);
	GSM_AddGlobalStateCallbacks(GCL_PATCH_SET_VIEW, patchSetViewEnter, NULL, NULL, NULL);

	GSM_AddGlobalState(GCL_PATCH_XFER);
	GSM_AddGlobalStateCallbacks(GCL_PATCH_XFER, patchXferEnter, NULL, NULL, NULL);
}


// PREPATCHING
//////////////////////////////////////////////////////////////////////////
static char *g_PrePatch = NULL;

// Set the prepatch command string
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_NAME(prepatch);
void patchingSetPrepatch(char *str)
{
	SAFE_FREE(g_PrePatch);
	g_PrePatch = strdup(str);
}

void gclPatching_RunPrePatch(void)
{
#if !PLATFORM_CONSOLE
	if(g_PrePatch)
		system_detach(g_PrePatch, false, false);
#endif
	SAFE_FREE(g_PrePatch);
}

// Return true if dynamic patching should be used.
bool gclPatching_DynamicPatchingEnabled()
{
	return isProductionMode() || g_force_dynamic_patch;
}


#include "AutoGen/gclPatching_c_ast.c"