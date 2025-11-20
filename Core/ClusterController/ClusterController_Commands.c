#include "ClusterController_Commands.h"

#include "sysutil.h"
#include "file.h"
#include "memorymonitor.h"
#include "foldercache.h"
#include "winutil.h"
#include "gimmeDLLWrapper.h"
#include "serverlib.h"
#include "UtilitiesLib.h"
#include "ResourceInfo.h"
#include "timing.h"

#include "GenericHttpServing.h"
#include "StringCache.h"
#include "../../Core/Controller/Pub/ControllerPub.h"
#include "controllerpub_h_ast.h"
#include "structNet.h"
#include "ClusterController_c_ast.h"
#include "ResourceInfo.h"
#include "Alerts.h"
#include "TimedCallback.h"
#include "StringUtil.h"
#include "structDefines.h"
#include "cmdparse.h"
#include "logging.h"
#include "ClusterController_h_ast.h"
#include "ClusterController_Commands_h_ast.h"
#include "../../libs/serverlib/pub/ShardCluster.h"
#include "rand.h"
#include "SentryServerComm.h"
#include "zutils.h"
#include "ShardCommon.h"

char *spLocalTemplateDataDir = NULL;
AUTO_CMD_ESTRING(spLocalTemplateDataDir, LocalTemplateDataDir) ACMD_COMMANDLINE;

static CommandResponse **sppActiveCommands = NULL;
static CommandResponse **sppCompleteCommands = NULL;

static StashTable sPerShardCommandResponsesByID = NULL;


//if a command to a controller doesn't return in this many seconds, consider it to have failed
static int sClusterControllerCommandTimeout = 15;
AUTO_CMD_INT(sClusterControllerCommandTimeout, ClusterControllerCommandTimeout) ACMD_COMMANDLINE;

//when sending files to a shard as part of a command, send this many bytes per second
static int siFileSendingBytesPerTick = 32 * 1024;
AUTO_CMD_INT(siFileSendingBytesPerTick, FileSendingBytesPerTick) ACMD_COMMANDLINE;
	


AUTO_FIXUPFUNC;
TextParserResult fixupCommandResponse(CommandResponse *pResponse, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		SAFE_FREE(pResponse->pSlowReturnInfo);
		break;
	}

	return 1;
}


AUTO_FIXUPFUNC;
TextParserResult fixupPerShardCommandResponse(PerShardCommandResponse *pResponse, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		stashIntRemovePointer(sPerShardCommandResponsesByID, pResponse->iUniqueID, NULL);
		break;
	}

	return 1;
}


void ClusterControllerCommands_ShardDisconnected(Shard *pShard)
{
	FOR_EACH_IN_EARRAY(sppActiveCommands, CommandResponse, pCommand)
	{
		PerShardCommandResponse *pResponse = eaIndexedGetUsingString(&pCommand->ppPerShardResponses, pShard->pShardName);
		if (pResponse && pResponse->eStatus == COMMANDRESPONSE_NOT_RESPONDED)
		{
			pResponse->eStatus = COMMANDRESPONSE_DISCONNECTED;
			CheckForCommandCompletion(pCommand);
		}
	}
	FOR_EACH_END;
}



void HandleCommandReturn(Packet *pPak, Shard *pShard)
{
	int iCmdID;
	CommandResponse *pResponse;
	PerShardCommandResponse *pPerShardResponse;

	if (!pShard || pShard == UNKNOWN_SHARD)
	{
		return;
	}

	iCmdID = pktGetBits(pPak, 32);
	pResponse = eaIndexedGetUsingInt(&sppActiveCommands, iCmdID);
	
	if (!pResponse)
	{
		pResponse = eaIndexedGetUsingInt(&sppCompleteCommands, iCmdID);
		if (pResponse)
		{
			pPerShardResponse = eaIndexedGetUsingString(&pResponse->ppPerShardResponses, pShard->pShardName);
			
			if (pPerShardResponse)
			{
				//presumably already timed out, or some logic error
				CRITICAL_NETOPS_ALERT("RESPONSE_TOO_LATE", "Got a response from shard %s to command <<%s>>, but we'd already decided that that response was %s, sent the command %d seconds ago",
					pShard->pShardName, pResponse->pCommand, StaticDefineInt_FastIntToString(CommandResponseStatusEnum, pPerShardResponse->eStatus), timeSecondsSince2000() - pResponse->iTimeIssued);
			}
			else
			{
				AssertOrAlert("CMD_RESPONSE_UNKNOWN_SHARD", "Got a response from shard %s to command <<%s>>, but we didn't send it to that shard",
					pShard->pShardName, pResponse->pCommand);
			}
		}
		else
		{
			AssertOrAlert("UNKNOWN_CMD_RESPONSE", "Got a response from shard %s to a command with ID %d, which we never sent",
				pShard->pShardName, iCmdID);
		}
		return;
	}

	pPerShardResponse = eaIndexedGetUsingString(&pResponse->ppPerShardResponses, pShard->pShardName);
	if (!pPerShardResponse)
	{
		AssertOrAlert("CMD_RESPONSE_UNKNOWN_SHARD", "Got a response from shard %s to command <<%s>>, but we didn't send it to that shard",
			pShard->pShardName, pResponse->pCommand);
		return;
	}

	if (pPerShardResponse->eStatus != COMMANDRESPONSE_NOT_RESPONDED)
	{
		AssertOrAlert("CMD_RESPONSE_CORRUPTION", "Got a response from shard %s to command <<%s>>, but we already think that the response is %s",
			pShard->pShardName, pResponse->pCommand, StaticDefineInt_FastIntToString(CommandResponseStatusEnum, pPerShardResponse->eStatus));
		return;
	}

	if (pktGetBits(pPak, 1))
	{
		pPerShardResponse->eStatus = COMMANDRESPONSE_RESPONDED;
		pPerShardResponse->pResponseString = pktMallocString(pPak);
	}
	else
	{
		pPerShardResponse->eStatus = COMMANDRESPONSE_FAILED;
	}

	CheckForCommandCompletion(pResponse);



}


void ClusterControllerCommands_OncePerSecond(float timeSinceLastCallback)
{
	//iterate through all active commands, looking for responses that have timed out. However, any command
	//that is being sent to a shard for which there are active file transfers can not time out, because the
	//command will presumably not actually begin until the file transfers complete
	FOR_EACH_IN_EARRAY(sppActiveCommands, CommandResponse, pResponse)
	{
		bool bSomethingChanged = false;

		FOR_EACH_IN_EARRAY(pResponse->ppPerShardResponses, PerShardCommandResponse, pPerShardResponse)
		{
			if (pPerShardResponse->eStatus == COMMANDRESPONSE_NOT_RESPONDED && pPerShardResponse->bWaitForFileTransfers)
			{
				Shard *pShard;
				if (stashFindPointer(gShardsByName, pPerShardResponse->pShardName, &pShard))
				{
					if (pShard->pLink)
					{
						if (linkFileSendingMode_linkHasActiveManagedFileSend(pShard->pLink))
						{
							pPerShardResponse->fExtraTime += timeSinceLastCallback;
						}
					}
				}

				if (pResponse->iTimeIssued < timeSecondsSince2000() - pResponse->iTimeOut - pPerShardResponse->fExtraTime)
				{
					pPerShardResponse->eStatus = COMMANDRESPONSE_TIMED_OUT;
					bSomethingChanged = true;
				}
			}
		}
		FOR_EACH_END;

		if (bSomethingChanged)
		{
			CheckForCommandCompletion(pResponse);
		}
	}
	FOR_EACH_END;
}




static int SortByStatusAndResponseString(const PerShardCommandResponse **ppLeft, const PerShardCommandResponse **ppRight)
{
	const PerShardCommandResponse *pLeft = *ppLeft;
	const PerShardCommandResponse *pRight = *ppRight;

	if (pLeft->eStatus != pRight->eStatus)
	{
		return pLeft->eStatus - pRight->eStatus;
	}

	return stricmp_safe(pLeft->pResponseString, pRight->pResponseString);
}

static char *GetSingleShardResponseSummary(PerShardCommandResponse *pPerShardResponse)
{
	static char *spRetVal = NULL;

	if (pPerShardResponse->eStatus == COMMANDRESPONSE_RESPONDED)
	{
		estrPrintf(&spRetVal, "RESPONDED with \"%s\"", pPerShardResponse->pResponseString);
	}
	else if (pPerShardResponse->eStatus == COMMANDRESPONSE_FAILED)
	{
		estrPrintf(&spRetVal, "handshaked with shard in both directions, but cmd execution FAILED");
	}
	else
	{
		estrPrintf(&spRetVal, "%s", StaticDefineInt_FastIntToString(CommandResponseStatusEnum, pPerShardResponse->eStatus));
	}

	return spRetVal;
}

static bool ResponsesMatch(PerShardCommandResponse *pResponse1, PerShardCommandResponse *pResponse2)
{
	return (SortByStatusAndResponseString(&pResponse1, &pResponse2) == 0);
}

static void SummarizeList(char **ppOutEString, PerShardCommandResponse **ppPerShardResponses)
{
	int iNumResponses = eaSize(&ppPerShardResponses);
	int iIndex1 = 0, iIndex2 = 1;

	if (iNumResponses == 1)
	{
		estrConcatf(ppOutEString, "Shard %s: %s\n", ppPerShardResponses[0]->pShardName, GetSingleShardResponseSummary(ppPerShardResponses[0]));
		return;
	}

	while ( iIndex1 < iNumResponses)
	{
		while (iIndex2 < iNumResponses && ResponsesMatch(ppPerShardResponses[iIndex1], ppPerShardResponses[iIndex2]))
		{
			iIndex2++;
		}

		if (iIndex2 == iIndex1 + 1)
		{
			estrConcatf(ppOutEString, "Shard %s: %s\n", ppPerShardResponses[iIndex1]->pShardName, GetSingleShardResponseSummary(ppPerShardResponses[iIndex1]));
		}
		else
		{
			int i;
			estrConcatf(ppOutEString, "Shards ");
			for (i = iIndex1; i < iIndex2; i++)
			{
				estrConcatf(ppOutEString, "%s%s", i == iIndex1 ? "" : ", ", ppPerShardResponses[i]->pShardName);
			}

			estrConcatf(ppOutEString, ": %s\n", GetSingleShardResponseSummary(ppPerShardResponses[iIndex1]));
		}

		iIndex1 = iIndex2;
		iIndex2++;
	}
}

//should be something that summarizes what happened in as human readable a fashion as possible, puts it into a string which will get
//returned when the command was executed via server monitoring
//
//returns true if all succeeded with same return string
bool GenerateAndReturnResponseSummary(CommandResponse *pResponse)
{
	//note... NOT indexed
	PerShardCommandResponse **ppSucceeded = NULL;
	PerShardCommandResponse **ppOther = NULL;

	PerShardCommandResponse *pSucceeded1, *pSucceeded2;

	bool bAllSucceeded = false;

	if (eaSize(&pResponse->ppPerShardResponses) == 1)
	{
		estrPrintf(&pResponse->pResponseSummary, "Command sent only to %s: %s",
			pResponse->ppPerShardResponses[0]->pShardName, GetSingleShardResponseSummary(pResponse->ppPerShardResponses[0]));
		return pResponse->ppPerShardResponses[0]->eStatus == COMMANDRESPONSE_RESPONDED;
	}

	FOR_EACH_IN_EARRAY(pResponse->ppPerShardResponses, PerShardCommandResponse, pPerShardResponse)
	{
		if (pPerShardResponse->eStatus == COMMANDRESPONSE_RESPONDED)
		{
			eaPush(&ppSucceeded, pPerShardResponse);
		}
		else
		{
			eaPush(&ppOther, pPerShardResponse);
		}
	}
	FOR_EACH_END;

	eaQSort(ppSucceeded, SortByStatusAndResponseString);
	eaQSort(ppOther, SortByStatusAndResponseString);

	if (eaSize(&ppSucceeded) == 0)
	{
		estrPrintf(&pResponse->pResponseSummary, "Command sent to %d shards. None responsed successfully:",
			eaSize(&pResponse->ppPerShardResponses));

		SummarizeList(&pResponse->pResponseSummary, ppOther);		
	}
	else
	{
		pSucceeded1 = ppSucceeded[0];
		pSucceeded2 = eaTail(&ppSucceeded);

		if (eaSize(&ppOther) == 0 && stricmp_safe(pSucceeded1->pResponseString, pSucceeded2->pResponseString) == 0)
		{
			estrPrintf(&pResponse->pResponseSummary, "Command sent to %d shards, all responded identically: %s", 
				eaSize(&ppSucceeded), GetSingleShardResponseSummary(pSucceeded1));
			bAllSucceeded = true;
		}
		else
		{

			if (eaSize(&ppOther) == 0)
			{
				estrPrintf(&pResponse->pResponseSummary, "Command sent to %d shards. They all succeeded, but with non-identical responses:\n",
					eaSize(&ppSucceeded));
				SummarizeList(&pResponse->pResponseSummary, ppSucceeded);
			}
			else
			{
				if (stricmp_safe(pSucceeded1->pResponseString, pSucceeded2->pResponseString) == 0)
				{
					estrPrintf(&pResponse->pResponseSummary, "Command sent to %d shards. %d responded successfully:",
						eaSize(&pResponse->ppPerShardResponses), eaSize(&ppSucceeded));
					SummarizeList(&pResponse->pResponseSummary, ppSucceeded);

					estrConcatf(&pResponse->pResponseSummary, "\n%d failed:\n", eaSize(&ppOther));

					SummarizeList(&pResponse->pResponseSummary, ppOther);
				}
				else
				{
					estrPrintf(&pResponse->pResponseSummary, "Command sent to %d shards. %d succeeded, but with non-identical responses:\n",
						eaSize(&pResponse->ppPerShardResponses),
						eaSize(&ppSucceeded));
					SummarizeList(&pResponse->pResponseSummary, ppSucceeded);
					estrConcatf(&pResponse->pResponseSummary, "\n%d failed:\n", eaSize(&ppOther));
					SummarizeList(&pResponse->pResponseSummary, ppOther);
				}
			}
		}
	}


	eaDestroy(&ppSucceeded);
	eaDestroy(&ppOther);

	return bAllSucceeded;
}

static void AddMissedCommand(CommandResponse *pResponse, PerShardCommandResponse *pPerShardResponse)
{
	Shard *pShard;
	MissedCommand *pMissedCommand;

	if (!stashFindPointer(gShardsByName, pPerShardResponse->pShardName, &pShard))
	{
		return;
	}

	pMissedCommand = StructCreate(parse_MissedCommand);
	pMissedCommand->iID = ++pShard->iNextMissedCommandID;
	pMissedCommand->iSendTime = pResponse->iTimeIssued;
	pMissedCommand->pCommandString = strdup(pResponse->pCommand);
	pMissedCommand->eStatus = pPerShardResponse->eStatus;
	pMissedCommand->pShardName = pPerShardResponse->pShardName;

	if (pResponse->pFiles && pResponse->pFiles[0])
	{
		estrPrintf(&pMissedCommand->pFiles, "This command included file sends, so should not be retried without first transferring those files manually. Files: %s", 
			pResponse->pFiles);
	}

	eaPush(&pShard->ppMissedCommands, pMissedCommand);
}


bool CheckForCommandCompletion(CommandResponse *pResponse)
{
	bool bAllSucceeded;

	if (estrLength(&pResponse->pResponseSummary))
	{
		return false;
	}

	FOR_EACH_IN_EARRAY(pResponse->ppPerShardResponses, PerShardCommandResponse, pPerShardResponse)
	{
		if (pPerShardResponse->eStatus == COMMANDRESPONSE_NOT_RESPONDED)
		{
			return false;
		}
	}
	FOR_EACH_END;

	bAllSucceeded = GenerateAndReturnResponseSummary(pResponse);

	if (pResponse->pSlowReturnInfo)
	{
		DoSlowCmdReturn(bAllSucceeded, pResponse->pResponseSummary, pResponse->pSlowReturnInfo);
	}

	log_printf(LOG_COMMANDS, "Cmd %d returned: %s", pResponse->iID, pResponse->pResponseSummary); 

	
	eaIndexedRemoveUsingInt(&sppActiveCommands, pResponse->iID);
	eaPush(&sppCompleteCommands, pResponse);
	
	FOR_EACH_IN_EARRAY(pResponse->ppPerShardResponses, PerShardCommandResponse, pPerShardResponse)
	{
		if (pPerShardResponse->eStatus == COMMANDRESPONSE_DISCONNECTED || pPerShardResponse->eStatus == COMMANDRESPONSE_TIMED_OUT)
		{
			AddMissedCommand(pResponse, pPerShardResponse);
		}
	}
	FOR_EACH_END;

	return true;
}


void CmdFileSendErrorCB(char *pFileName, char *pErrorString, void *pUserData)
{
	U32 iID = (U32)((intptr_t)pUserData);
	PerShardCommandResponse *pPerShardResponse;
	if (stashIntFindPointer(sPerShardCommandResponsesByID, iID, &pPerShardResponse))
	{
		if (pPerShardResponse->eStatus == COMMANDRESPONSE_NOT_RESPONDED)
		{
			pPerShardResponse->eStatus = COMMANDRESPONSE_FILE_TRANSFER_FAILED;
			CheckForCommandCompletion(pPerShardResponse->pParent);
		}
	}
}



static int siNextID = 1;


void CreateAndSendPerShardResponse(CommandResponse *pCommand, Shard *pShard, char *pCommandString, char **ppFilesToSend)
{
	static int siNextPerShardID = 1;
	PerShardCommandResponse *pPerShardResponse = StructCreate(parse_PerShardCommandResponse);
	
	pPerShardResponse->pShardName = pShard->pShardName;
	pPerShardResponse->pParent = pCommand;
	pPerShardResponse->iUniqueID = siNextPerShardID++;

	if (!sPerShardCommandResponsesByID)
	{
		sPerShardCommandResponsesByID = stashTableCreateInt(16);
	}

	stashIntAddPointer(sPerShardCommandResponsesByID, pPerShardResponse->iUniqueID, pPerShardResponse, true);

	if (eaSize(&ppFilesToSend))
	{
		pPerShardResponse->bWaitForFileTransfers = true;
	}

	if (pShard->eState == SHARD_CONNECTED)
	{
		if (!pShard->pLink)
		{
			AssertOrAlert("SHARD_CORRUPTION", "Shard %s is in connected state but has no link", pShard->pShardName);
		}
		else
		{
			Packet *pPack = pktCreate(pShard->pLink, CLUSTERCONTROLLER_TO_CONTROLLER__COMMAND);
			pktSendBits(pPack, 32, pCommand->iID);
			pktSendString(pPack, pCommandString);

			if (eaSize(&ppFilesToSend))
			{
				if (!linkFileSendingMode_SendMultipleManagedFiles(pShard->pLink, CLUSTERCONTROLLER_TO_CONTROLLER__SENDFILE, 0,
					ppFilesToSend, 
	
					siFileSendingBytesPerTick, 
					CmdFileSendErrorCB, (void*)((intptr_t)pPerShardResponse->iUniqueID),
					NULL, NULL, pPack))
				{
					AssertOrAlert("FILE_SEND_FAILED", "While trying to send command <<%s>> to shard %s, file sending couldn't even begin",
						pCommandString, pShard->pShardName);
				}

			}
			else
			{	
				pktSend(&pPack);
			}
		}
	}
	else
	{
		pPerShardResponse->eStatus = COMMANDRESPONSE_DISCONNECTED;
	}

	eaPush(&pCommand->ppPerShardResponses, pPerShardResponse);
}


//shardname ALL means send to all shards
//
//if ppFilesToSend is set, then the command will first send all those named files to each shard before 
//actually sending the command. ppFilenames must be even in size, alternating between local and remote filenames. Local
//files are absolute paths, remote files are local to the folder that controller.exe is in... so 
//just "gameserver_fb.exe" will put a frankenbuilt .exe file in the right place, "\localdata\server\foo.txt" will put
//something in localdata
AUTO_COMMAND;
char *SendCommandToShard(CmdContext *pContext, ACMD_IGNORE char **ppFilesToSend, char *pShardName_in, ACMD_SENTENCE pCommandString, ACMD_IGNORE int iTimeout)
{
	const char *pShardName = allocAddString(pShardName_in);

	CommandResponse *pCommand = StructCreate(parse_CommandResponse);
	bool bAll = (stricmp(pShardName, "all") == 0);
	Shard *pOnlyShard = NULL;
	int iNumFilesToSend = eaSize(&ppFilesToSend);

	//descriptive string with all the files to send, for reporting/logging/etc
	static char *spFileReportingString = NULL;

	if (iNumFilesToSend % 2 == 1)
	{
		AssertOrAlert("BAD_FILES", "Called SendCommandToShard with an odd number of arguments, this is illegal...");
		return "bad filenames";
	}	


	if (!bAll)
	{
		if (!stashFindPointer(gShardsByName, pShardName, &pOnlyShard))
		{
			AssertOrAlert("UNKOWN_SHARD_FOR_CMD", "Attempted to send cmd <<%s>> to unkown shard %s", 
				pCommandString, pShardName);
			return "Unknown Shard";
		}
	}

	estrClear(&spFileReportingString);
	if (iNumFilesToSend)
	{
		int i;

		for (i = 0; i < iNumFilesToSend; i+= 2)
		{

			if (!fileExists(ppFilesToSend[i]))
			{
				AssertOrAlert("NONEXISTENT_FILE_FOR_CMB", "Attempted to send file %s while executing cmd <<%s>>, that file doesn't exist", 
					ppFilesToSend[i], pCommandString);
				return "Nonexistent file";
			}

			estrConcatf(&spFileReportingString, "%s%s->%s", i == 0 ? "" : ", ", ppFilesToSend[i], ppFilesToSend[i+1]);
		}
	}


	pCommand->iID = siNextID++;
	pCommand->iTimeIssued = timeSecondsSince2000();
	pCommand->pCommand = strdup(pCommandString);
	pCommand->pFiles = strdup(spFileReportingString);
	pCommand->iTimeOut = iTimeout ? iTimeout : sClusterControllerCommandTimeout;

	log_printf(LOG_COMMANDS, "SendCommandToShard called via %s, assigned ID %d, sending <<%s>> to %s", 
		GetContextHowString(pContext), pCommand->iID, pCommandString, pShardName);

	if (iNumFilesToSend)
	{
		log_printf(LOG_COMMANDS, "Files being sent: %s", spFileReportingString);
	}


	if (bAll)
	{
		FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
		{
			CreateAndSendPerShardResponse(pCommand, pShard, pCommandString, ppFilesToSend);
		}
		FOR_EACH_END;
	}
	else
	{
		CreateAndSendPerShardResponse(pCommand, pOnlyShard, pCommandString, ppFilesToSend);
	}

	eaPush(&sppActiveCommands, pCommand);

	//in case all recipients were disconnected, or something like that
	if (CheckForCommandCompletion(pCommand))
	{
		return pCommand->pResponseSummary;
	}
	else
	{
		//if we get at least this far, we know we're going to send the command off to shards, so set up the slow return info
		pCommand->pSlowReturnInfo = calloc(sizeof(CmdSlowReturnForServerMonitorInfo), 1);
		memcpy(pCommand->pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));
		pContext->slowReturnInfo.bDoingSlowReturn = true;

		return "You should never see this";
	}
}

AUTO_COMMAND;
char *SendCommandToShardWithTimeout(CmdContext *pContext, char *pShardName_in, int iTimeout, ACMD_SENTENCE pCommandString )
{
	return SendCommandToShard(pContext, NULL, pShardName_in, pCommandString, iTimeout);
}

AUTO_RUN;
void InitCommandArrays(void)
{
	eaIndexedEnable(&sppActiveCommands, parse_CommandResponse);
	eaIndexedEnable(&sppCompleteCommands, parse_CommandResponse);
	resRegisterDictionaryForEArray("ActiveCommands", RESCATEGORY_SYSTEM, 0, &sppActiveCommands, parse_CommandResponse); 
	resRegisterDictionaryForEArray("CompleteCommands", RESCATEGORY_SYSTEM, 0, &sppCompleteCommands, parse_CommandResponse);
}

AUTO_COMMAND;
void ClearMissedCommand(char *pShardName_in, int iID)
{
	const char *pShardName = allocAddString(pShardName_in);
	Shard *pShard;
	MissedCommand *pMissedCommand;

	if (!stashFindPointer(gShardsByName, pShardName, &pShard))
	{
		return;
	}

	pMissedCommand = eaIndexedRemoveUsingInt(&pShard->ppMissedCommands, iID);

	StructDestroySafe(parse_MissedCommand, &pMissedCommand);
}

AUTO_COMMAND;
char *RetryMissedCommand(CmdContext *pContext, char *pShardName_in, int iID)
{
	const char *pShardName = allocAddString(pShardName_in);
	Shard *pShard;
	MissedCommand *pMissedCommand;
	char *pRetVal;

	if (!stashFindPointer(gShardsByName, pShardName, &pShard))
	{
		return "Shard doesn't exist";
	}

	pMissedCommand = eaIndexedRemoveUsingInt(&pShard->ppMissedCommands, iID);

	if (!pMissedCommand)
	{
		return "command doesn't exist";
	}

	pRetVal = SendCommandToShard(pContext, NULL, pShardName_in, pMissedCommand->pCommandString, 0);

	StructDestroySafe(parse_MissedCommand, &pMissedCommand);

	return pRetVal;
}




void ClusterControllerCommands_ShardNewlyConnected(Shard *pShard)
{
	if (eaSize(&pShard->ppMissedCommands))
	{
		WARNING_NETOPS_ALERT("SHARD_MISSED_COMMANDS", "Shard %s is now connected to the ClusterController, but it missed some commands while it was disconnected, please either clear them or retry them",
			pShard->pShardName);
	}
}


//char *SendCommandToShard(CmdContext *pContext, ACMD_IGNORE char **ppFilesToSend, char *pShardName_in, ACMD_SENTENCE pCommandString)

static void FileSendTestSuccessCB(char *pFileName, char *pErrorString, void *pUserData)
{
	printf("Sending %s succeeded\n", pFileName);
}

static void FileSendTestFailCB(char *pFileName, char *pErrorString, void *pUserData)
{
	printf("Sending %s failed\n", pFileName);
}

AUTO_COMMAND;
void FileSendTest(void)
{
//	char **ppFileNames = NULL;
//	eaPush(&ppFileNames, "c:\\temp\\CrashDump.mdmp");
//	eaPush(&ppFileNames, "c:\\temp4\\CrashDump.mdmp");

	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		if (pShard->pLink)
		{
			linkFileSendingMode_SendManagedFile(pShard->pLink, CLUSTERCONTROLLER_TO_CONTROLLER__SENDFILE, SENDMANAGED_CHECK_FOR_EXISTENCE, 
				"c:\\temp\\CrashDump.mdmp", "c:\\temp4\\CrashDump.mdmp", siFileSendingBytesPerTick, 
				FileSendTestFailCB, NULL,
				FileSendTestSuccessCB, NULL, 
				NULL);

		}
	}
	FOR_EACH_END;
}

static char *spFrankenBuildDir = NULL;
AUTO_CMD_ESTRING(spFrankenBuildDir, FrankenBuildDir) ACMD_COMMANDLINE;

static char *ClusterController_GetFrankenBuildDir(void)
{
	return spFrankenBuildDir;
}

U32 ClusterController_GetNextFrankenID(void)
{
	return randomIntRange(1, MIN_FRANKENBUILD_ID_FROM_CLUSTER_CONTROLLER) + MIN_FRANKENBUILD_ID_FROM_CLUSTER_CONTROLLER;
}


AUTO_COMMAND;
char *SendFrankenBuildToShard(CmdContext *pContext, char *pShardName, char *pShortExeName)
{
	//making these all static just to avoid mem leaks
	static char *pInName = NULL;

	static char *p32BitExe = NULL;
	static char *p32BitPDB = NULL;
	static char *p64BitExe = NULL;
	static char *p64BitPDB = NULL;
	static char *pFull32BitExe = NULL;
	static char *pFull32BitPDB = NULL;
	static char *pFull64BitExe = NULL;
	static char *pFull64BitPDB = NULL;
	static char *spRetString = NULL;
	static char **sppFilesToSend = NULL;
	static char *spCommandString = NULL;

	eaClear(&sppFilesToSend);


	if (!strchr(pShortExeName, '_') || strchr(pShortExeName, '/') || strchr(pShortExeName, '\\'))
	{
		return "Invalid exe passed in to SendFrankenBuildToShard... should have no path, and look like appserver_foo.exe";
	}


	estrCopy2(&pInName, pShortExeName);
	if (!strEndsWith(pInName, ".exe"))
	{
		estrConcatf(&pInName, ".exe");
	}

	estrCopy2(&p32BitExe, pInName);
	estrCopy2(&p64BitExe, pInName);

	if (strstri(pInName, "X64"))
	{
		estrReplaceOccurrences(&p32BitExe, "X64", "");
	}
	else
	{
		char *pFirstUnderscore = strchr(p64BitExe, '_');
		estrInsertf(&p64BitExe, pFirstUnderscore - p64BitExe, "X64");
	}

	estrCopy2(&p32BitPDB, p32BitExe);
	estrCopy2(&p64BitPDB, p64BitExe);

	estrReplaceOccurrences(&p32BitPDB, ".exe", ".pdb");
	estrReplaceOccurrences(&p64BitPDB, ".exe", ".pdb");

	estrPrintf(&pFull32BitExe, "%s/%s", ClusterController_GetFrankenBuildDir(), p32BitExe);
	estrPrintf(&pFull32BitPDB, "%s/%s", ClusterController_GetFrankenBuildDir(), p32BitPDB);
	estrPrintf(&pFull64BitExe, "%s/%s", ClusterController_GetFrankenBuildDir(), p64BitExe);
	estrPrintf(&pFull64BitPDB, "%s/%s", ClusterController_GetFrankenBuildDir(), p64BitPDB);

	if (!fileExists(pFull32BitExe) && !fileExists(pFull64BitExe))
	{
		estrPrintf(&spRetString, "FAILED: At least one of %s or %s must exist", pFull32BitExe, pFull64BitExe);
		return spRetString;
	}

	if (fileExists(pFull32BitExe))
	{
		if (!fileExists(pFull32BitPDB))
		{
			estrPrintf(&spRetString, "FAILED: If %s exists, %s must exist as well", pFull32BitExe, pFull32BitPDB);
			return spRetString;
		}

		eaPush(&sppFilesToSend, pFull32BitExe);
		eaPush(&sppFilesToSend, p32BitExe);
		eaPush(&sppFilesToSend, pFull32BitPDB);
		eaPush(&sppFilesToSend, p32BitPDB);
	}

	if (fileExists(pFull64BitExe))
	{
		if (!fileExists(pFull64BitPDB))
		{
			estrPrintf(&spRetString, "FAILED: If %s exists, %s must exist as well", pFull64BitExe, pFull64BitPDB);
			return spRetString;
		}

		eaPush(&sppFilesToSend, pFull64BitExe);
		eaPush(&sppFilesToSend, p64BitExe);
		eaPush(&sppFilesToSend, pFull64BitPDB);
		eaPush(&sppFilesToSend, p64BitPDB);
	}

	estrPrintf(&spCommandString, "RequestFrankenBuildHotpush_ForceID %s %u", p32BitExe, ClusterController_GetNextFrankenID());

	return SendCommandToShard(pContext, sppFilesToSend, pShardName, spCommandString, 240);
}

AUTO_COMMAND;
char *KillAllShards(char *pConfirm)
{
	if (stricmp(pConfirm, ShardCommon_GetClusterName()) != 0)
	{
		return "You didn't type the cluster name";
	}

	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		if (pShard->pLink)
		{
			Packet *pPak = pktCreate(pShard->pLink, CLUSTERCONTROLLER_TO_CONTROLLER__KILLYOURSELF);
			pktSend(&pPak);
		}
	}
	FOR_EACH_END;

	return "Shards killed";
}

AUTO_COMMAND;
char *RunShard(char *pShardName, char *pConfirm)
{
	Shard *pShard;

	if (stricmp(pConfirm, "yes") != 0)
	{
		return "You didn't type yes";
	}

	if (!stashFindPointer(gShardsByName, allocAddString(pShardName), &pShard))
	{
		return "Unknown shard";
	}

	if (pShard->eState == SHARD_CONNECTED)
	{
		return "That shard is already running";
	}

	SentryServerComm_RunCommand_1Machine(pShard->pMachineName, pShard->pRestartBatchFile);

	return "Attempted to run the shard";
}

AUTO_COMMAND;
char *KillShard(CmdContext *pContext, char *pShardName, int iUseSentryServer, char *pConfirm)
{
	Shard *pShard;

	if (stricmp_safe(pShardName, pConfirm) != 0)
	{
		return "You didn't type the shard name";
	}

	if (!stashFindPointer(gShardsByName, allocAddString(pShardName), &pShard))
	{
		return "Unknown shard";
	}

	if (iUseSentryServer)
	{
		if (pShard->pMachineName)
		{
			SentryServerComm_KillProcess_1Machine(pShard->pMachineName, "controller.exe", NULL);
			SentryServerComm_KillProcess_1Machine(pShard->pMachineName, "controllerX64.exe", NULL);

			return "killed with sentry server";
		}
		else
		{
			return "can't kill shard, don't know machine";
		}
	}
	else
	{
		if (pShard->pLink)
		{
			Packet *pPak = pktCreate(pShard->pLink, CLUSTERCONTROLLER_TO_CONTROLLER__KILLYOURSELF);
			pktSend(&pPak);
			return "Kill packet sent";
		}
		else
		{
			return "Can't kill shard... not connected";
		}
	}
}


AUTO_COMMAND;
char *UpdateMapManagerConfigClusterLocal(CmdContext *pContext)
{
	char fileName[CRYPTIC_MAX_PATH];
	char *pFileBuf;
	int iFileSize;
	char *pEncodedBuf;
	char *pFullCommandString = NULL;
	char *pRetVal;


	sprintf(fileName, "%s/server/MapManagerConfig_ClusterLocal.txt", spLocalTemplateDataDir);
	pFileBuf = fileAlloc(fileName, &iFileSize);
	if (!pFileBuf)
	{
		return "file MapManagerConfig_ClusterLocal.txt appears not to exist locally";
	}

	pEncodedBuf = BufferToZippedEncodedString(pFileBuf, iFileSize + 1);

	estrPrintf(&pFullCommandString, "SendClusterControllerCommandToServer MapManager OverwriteClusterLocalMapManagerConfigAndReload %s", 
		pEncodedBuf);

	pRetVal = SendCommandToShard(pContext, NULL, "all", pFullCommandString, 60);

	free(pFileBuf);
	free(pEncodedBuf);
	estrDestroy(&pFullCommandString);

	return pRetVal;
}





#include "ClusterController_Commands_h_ast.c"