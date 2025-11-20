#include "ServerLib.h"
#include "net.h"
#include "error.h"
#include "ResourceManager.h"
#include "file.h"
#include "rand.h"
#include "ResourceInfo.h"
#include "MapDescription.h"
#include "pcl_client.h"
#include <sys/stat.h>
#include "fileUtil.h"
#include "errornet.h"
#include "utilitiesLib.h"
#include "continuousBuilderSupport.h"
#include "winInclude.h"
#include "logging.h"
#include "timing.h"
#include "timing_profiler.h"
#include "patchclient.h"
#include "pcl_client_struct.h"
#include "sysutil.h"

#include "AutoGen/MapDescription_h_ast.h"

// If set, go through the motions of dynamic patching even if we're not in production mode.
bool g_force_dynamic_patch = false;
AUTO_CMD_INT(g_force_dynamic_patch, force_dynamic_patch) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

//if true, don't patch even if we otherwise would (should be passed into gameservers for newly-created ugc projects that have
//never been saved)
bool gbNoDownPatch = false;
AUTO_CMD_INT(gbNoDownPatch, NoDownPatch) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

char gpcOverridePatchDir[MAX_PATH];
AUTO_CMD_STRING(gpcOverridePatchDir, OverridePatchDir) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

// Number of times to attempt to patch before giving up.
static unsigned siPatchTries = 4;
AUTO_CMD_INT(siPatchTries, DynamicPatchTries) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

static DynamicPatchInfo *gServerLibPatchInfo = NULL;

DynamicPatchInfo *ServerLib_GetPatchInfo(void)
{
	return gServerLibPatchInfo;
}

void ServerLib_SetPatchInfo(DynamicPatchInfo *pDynamicPatchInfo)
{
	if(pDynamicPatchInfo)
		gServerLibPatchInfo = StructClone(parse_DynamicPatchInfo, pDynamicPatchInfo);
	else
		gServerLibPatchInfo = NULL;
}

// Return true if dynamic patching should be used.
bool ServerLib_DynamicPatchingEnabled()
{
	return isProductionMode() || g_force_dynamic_patch;
}

// Set a map to be dynamically patched
AUTO_COMMAND ACMD_NAME(PatchInfo) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void ServerLib_cmd_PatchInfo(const char *encoded_struct)
{
	char *unesc=NULL;
	estrSuperUnescapeString(&unesc, encoded_struct);
	gServerLibPatchInfo = StructCreateFromString(parse_DynamicPatchInfo, unesc);
	assertmsg(gServerLibPatchInfo, "Can't decode patch information struct");
	estrDestroy(&unesc);
}

void ServerLibPatch(void)
{
	char *cmdline=NULL, *delpath=NULL, *output=NULL;
	int rv;
	struct _stat64 statbuf;
	char cwd[CRYPTIC_MAX_PATH];
	const char *patchclient;
	unsigned i;
	bool success;

	loadstart_printf("Patching dynamic map...");

	// Fast out
	if(!gServerLibPatchInfo || !ServerLib_DynamicPatchingEnabled() || gbNoDownPatch)
	{
		loadend_printf("skipped");
		return;
	}

	if (gpcOverridePatchDir[0] == 0)
		fileGetcwd(SAFESTR(cwd));
	else
		strcpy(cwd,gpcOverridePatchDir);
	forwardSlashes(cwd);

	// Delete $PREFIX from the FS in case old files were left behind
	estrPrintf(&delpath, "%s%s", cwd, gServerLibPatchInfo->pPrefix);
	if(_stat64(delpath, &statbuf)==0)
	{
		estrCopy(&cmdline, &delpath);
		backSlashes(cmdline);
		estrInsert(&cmdline, 0, "rd /s /q ", 9); 

		printf("Clearing old data files for namespace: %s\n", cmdline);
		rv = system(cmdline);

		// retry every 10 seconds for 2 minutes, in case other servers are shutting down
		if (rv)
		{
			bool bErased = false;
	
			RunHandleExeAndAlert("SERVERLIB_DYNPATCH_DELETE", gServerLibPatchInfo->pPrefix, "ugc_sharing", "could not erase files with this command line: %s (rv %d). Will retry for 2 minutes\n", cmdline, rv);

			for (i=0; i < 12; i++)
			{
				Sleep(10000);
				rv = system(cmdline);
				if (rv == 0 || _stat64(delpath, &statbuf))
				{
					bErased = true;
					break;
				}
			}

			assertmsgf(bErased, "Despite repeated retries, couldn't erase old files for namespace: %s\n", cmdline);
		}
	}

	// Build patchclient command line.
	patchclient = patchclientCmdLine(false);
	assert(patchclient);
	estrPrintf(&cmdline, "%s -sync -server %s -port %u -project %s -prefix %s -root %s -timeout %d -save_trivia 0 -metadatainmemory -sharehoggs -profiletimeout",
		patchclient, gServerLibPatchInfo->pServer, gServerLibPatchInfo->iPort, gServerLibPatchInfo->pServerProject, gServerLibPatchInfo->pPrefix, cwd, gServerLibPatchInfo->iTimeout);
	if(gServerLibPatchInfo->pViewName)
		estrConcatf(&cmdline, " -name %s", gServerLibPatchInfo->pViewName);
	else
		estrConcatf(&cmdline, " -branch %d", gServerLibPatchInfo->iBranch);

	// Try to run patchclient command line up to siPatchTries times.
	success = false;
	for (i = 0; i < siPatchTries; ++i)
	{
		char *alert_cmdline=NULL, *output_esc=NULL;
		U64 patch_start, patch_stop;
		char *pCTName;

		// Try to patch.
		if (i)
			printf("Patch failed, retrying...\n");
		else
			printf("Running %s\n", cmdline);
		patch_start = timerCpuTicks64();
		rv = system_w_output(cmdline, &output);
		patch_stop = timerCpuTicks64();

		// If it worked, proceed successfully.
		if(rv == 0)
		{
			success = true;
			break;
		}


		pCTName = GetShardControllerTrackerFromShardInfoString();

		if (pCTName && pCTName[0])
		{
			// If it failed, send an alert.
			estrConcatf(&output, "\n\nServerLib patch time: %f s\n", timerSeconds64(patch_stop - patch_start));
			estrSuperEscapeString(&output_esc, output);
			estrPrintf(&alert_cmdline, "C:/Night/tools/bin/sendalert.exe -level %s -controllerTrackerName %s -criticalSystemName %s -alertKey SERVER_DYNPATCH_ERROR -superesc alertString %s",
				i >= siPatchTries/2 ? "critical" : "warning", pCTName, GetShardNameFromShardInfoString(), output_esc);
			printf("Running %s\n", alert_cmdline);
			system_detach(alert_cmdline, false, false);
		}
		else
		{
			estrConcatf(&output, "\n\nServerLib patch time: %f s\n", timerSeconds64(patch_stop - patch_start));
			estrSuperEscapeString(&output_esc, output);
				estrPrintf(&alert_cmdline, "messagebox -title \"Server dynpatch error\" -superesc message \"%s\" -icon error",
					output);
			system_detach(alert_cmdline, false, false);

		}
	}

	// If patching failed, exit.
	if (!success)
		exit(1);

	loadend_printf("complete");
	estrDestroy(&cmdline);
	estrDestroy(&delpath);
	estrDestroy(&output);
}

// Handle a PCL error.
#define PCL_DO_ERROR()																	\
{																						\
	if(error != PCL_SUCCESS)															\
	{																					\
		if (estrErrorMessage)															\
		{																				\
			char error_string[CRYPTIC_MAX_PATH];										\
			char state_string[CRYPTIC_MAX_PATH];										\
			pclGetErrorString(error, SAFESTR(error_string));							\
			pclGetStateString(client, SAFESTR(state_string));							\
			estrPrintf(estrErrorMessage,												\
				"Patch upload error: Error %s, state %s, debug %s, line %d",			\
				error_string, state_string, pclGetUsefulDebugString_Static(client),		\
				__LINE__);																\
		}																				\
		pclDisconnectAndDestroy(client);												\
		PERFINFO_AUTO_STOP_FUNC();														\
		return false;																	\
	}																					\
}

// Run PCL commands.
#define PCL_DO(fn)	\
{					\
	error = fn;		\
	PCL_DO_ERROR();	\
}

// Run PCL commands, and wait for them to finish.
#define PCL_DO_WAIT(fn)			\
{								\
	PCL_DO(fn);					\
	error = pclWait(client);	\
	PCL_DO_ERROR();				\
}

bool ServerLibPatchUpload(char **paths, const char *author, char **estrErrorMessage, const char *comment_in, ...)
{
	PCL_ErrorCode error;
	char *pCommentToUse = NULL;
	char** folderList = NULL;
	char** countsAsList = NULL;
	int *recurseList = NULL;
	PCL_Client *client = NULL;
	DynamicPatchInfo *patchInfo;
	char *pLogString = NULL;
	static NetComm *patch_comm = NULL;
	char prefix[MAX_PATH];
	bool do_checkin = false;

	PERFINFO_AUTO_START_FUNC();

	patchInfo = ServerLib_GetPatchInfo();

	// If nothing to do, return true.
	if (!eaSize(&paths))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	estrGetVarArgs(&pCommentToUse, comment_in);

	if(!patch_comm)
		patch_comm = commCreate(1, 1);

	estrPrintf(&pLogString, "ServerLibPatchUpload called for project %s with comment %s, about to upload the following folders:", patchInfo->pUploadProject, pCommentToUse);

	// Make a list of all paths to checkin.
	prefix[0] = 0;
	FOR_EACH_IN_EARRAY_FORWARDS(paths, char, path)
		char abspath[MAX_PATH], trimpath[MAX_PATH], pathprefix[MAX_PATH];
		char *pathprefixend;
		const char data_ns_prefix[] = "/data/ns/";
		// Make sure we have an absolute path
		if(fileIsAbsolutePath(path))
			strcpy(abspath, path);
		else
			fileLocateWrite(path, abspath);

		// Trim the base dir off, this should leave paths like /data/ns/foo/...
		assertmsgf(strStartsWith(abspath, fileBaseDir()), "%s not in base dir (%s)", abspath, fileBaseDir());
		strcpy(trimpath, abspath+strlen(fileBaseDir()));

		// Currently, only /data/ns type paths are supported.
		if (!strStartsWith(trimpath, data_ns_prefix))
		{
			if (estrErrorMessage)
				estrCopy2(estrErrorMessage, "Path type unimplemented");
			devassertmsg(0, "Unimplemented patch path type");
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		// Disallow null.
		if (strstri(trimpath, "(null)"))
		{
			if (estrErrorMessage)
				estrConcatf(estrErrorMessage, "Invalid \"(null)\" substring in \"%s\"", trimpath);
			devassertmsg(0, "(null) substring");
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		// Try to determine the correct prefix to use for this checkin.
		strcpy(pathprefix, trimpath);
		pathprefixend = strchr(pathprefix + sizeof(data_ns_prefix) - 1, '/');
		if (pathprefixend)
			*pathprefixend = 0;

		// Make sure all paths are using the same prefix.
		if (*prefix)
		{
			if (stricmp(prefix, pathprefix))
			{
				if (estrErrorMessage)
					estrCopy2(estrErrorMessage, "Not all paths share the same prefix");
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
		}
		else
		{
			strcpy(prefix, pathprefix);
			if (!prefix[sizeof(data_ns_prefix)-1])
			{
				if (estrErrorMessage)
					estrCopy2(estrErrorMessage, "An empty prefix is not allowed.");
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
		}

		eaPush(&folderList, strdup(abspath));
		eaPush(&countsAsList, strdup(trimpath));
		eaiPush(&recurseList, 1);

		estrConcatf(&pLogString, "%s, ", trimpath);

	FOR_EACH_END

	// Connect.
	PCL_DO_WAIT(pclConnectAndCreate(&client, 
		patchInfo->pServer, 
		patchInfo->iPort, 
		60, 
		patch_comm, 
		fileBaseDir(),
		NULL, 
		NULL, 
		NULL, 
		NULL));
	pclSetBadFilesDirectory(client, fileTempDir());

	// Set the prefix.
	devassert(*prefix);
	PCL_DO(pclSetPrefix(client, prefix));

	// Get the manifest, in-memory.
	PCL_DO(pclAddFileFlags(client, PCL_METADATA_IN_MEMORY));
	PCL_DO(pclSoftenError(client, PCL_INVALID_VIEW));
	error = pclSetViewLatest(client, patchInfo->pUploadProject, patchInfo->iBranch, NULL, true, false, NULL, NULL);
	if (error == PCL_SUCCESS)
		error = pclWait(client);
	pclUnsoftenError(client, PCL_INVALID_VIEW);
	if (error == PCL_INVALID_VIEW)
	{
		PCL_DO(pclClearError(client));
		PCL_DO(pclSetPrefix(client, ""));
		PCL_DO(pclAddFileFlags(client, PCL_FORCE_EMPTY_DB));
		PCL_DO_WAIT(pclSetViewLatest(client, patchInfo->pUploadProject, patchInfo->iBranch, NULL, true, false, NULL, NULL));
		printf("Setting view succeeded on second try!  Everything is OK so far!\n");
	}
	PCL_DO_ERROR();
	PCL_DO(pclRemoveFileFlags(client, PCL_FORCE_EMPTY_DB));

	// Set author.
	PCL_DO_WAIT(pclSetAuthor(client, author, NULL, NULL));

	// Check in these paths.
	log_printf(LOG_UGC, "%s", pLogString);

	FOR_EACH_IN_EARRAY_FORWARDS(countsAsList, const char, countsAs)
	{
		// Check in these files in this path.
		char **diffnames = NULL;
		PCL_DiffType *diff_types = NULL;
		estrPrintf(&pLogString, "%s: ", countsAs);
		PCL_DO_WAIT(pclDiffFolder(client, countsAs, /*forceIfNotLockedByClient=*/true, /*forceIfLockedByClient=*/false, /*matchAsFiles=*/false, &diffnames, &diff_types));
		FOR_EACH_IN_EARRAY_FORWARDS(diffnames, const char, filename)
		{
			estrConcatf(&pLogString, "%s, ", filename);
		}
		FOR_EACH_END;

		if(eaSize(&diffnames) > 0)
			do_checkin = true;

		eaDestroyEx(&diffnames, NULL);
		eaiDestroy(&diff_types);

		log_printf(LOG_UGC, "%s", pLogString);
	}
	FOR_EACH_END;

	estrDestroy(&pLogString);

	if(do_checkin)
	{
		PCL_DO_WAIT(pclForceInFiles(client, folderList, countsAsList, recurseList, eaSize(&folderList), NULL, 0, pCommentToUse, false, NULL, NULL));
	}
	else
		log_printf(LOG_UGC, "%s: Not uploading patch because there is no diff", __FUNCTION__);

	// Clean up.
	PCL_DO(pclDisconnectAndDestroy(client));
	eaDestroyEx(&folderList, NULL);
	eaDestroyEx(&countsAsList, NULL);
	eaiDestroy(&recurseList);
	estrDestroy(&pCommentToUse);

	return true;
}
