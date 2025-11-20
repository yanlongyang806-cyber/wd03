#include "NewControllerTracker.h"
#include "Stashtable.h"
#include "NewControllerTracker_pub_h_ast.h"
#include "Structnet.h"
#include "serverlib.h"
#include "StringCache.h"
#include "../../CrossRoads/common/AccountCommon.h"
#include "sock.h"
#include "estring.h"
#include "timing.h"
#include "netipfilter.h"
#include "accountnet.h"
#include "logging.h"
#include "alerts.h"
#include "accountnet_h_ast.h"

bool sbGiveAllShardsToLocal = false;
AUTO_CMD_INT(sbGiveAllShardsToLocal, GiveAllShardsToLocal) ACMD_COMMANDLINE;

bool gbVerboseLogWhenNoShardsFound = true;
AUTO_CMD_INT(gbVerboseLogWhenNoShardsFound, VerboseLogWhenNoShardsFound);

static ShardInfo_Basic_List *ProcessShardListForClusters(ShardInfo_Basic_List *pInList);


typedef struct TicketValidationRequest
{
	AccountValidator *validator;
	NetLink *link;
	MCPConnectionUserData *userData;
	const char *productName;
	int iRetryCount;
} TicketValidationRequest;

static TicketValidationRequest **sTicketValidationQueue;
static void TicketProcessShardList(NetLink *link, const char *pProductName, MCPConnectionUserData *pUserData);

__forceinline static void removeTicketValidationRequest(int index)
{
	TicketValidationRequest *request = sTicketValidationQueue[index];

	request->link = NULL;
	request->userData = NULL;
	eaRemoveFast(&sTicketValidationQueue, index);
	free(request);
}

typedef enum TicketExtractionCode
{
	TICKET_SUCCESS = 0,
	TICKET_FAILED_PARSE,
	TICKET_CONNECTION_FAILED,
	TICKET_AUTH_FAILED,
	TICKET_UNKNOWN_ERROR

} TicketExtractionCode;

static TicketExtractionCode TicketValidationExtractTicket(MCPConnectionUserData *pUserData, AccountValidator *validator)
{
	switch (accountValidatorGetResult(validator))
	{
	case ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_SUCCESS:
		{
			AccountTicketSigned signedTicket = {0};
			char *pTicketString = NULL;

			accountValidatorGetTicket(validator, &pTicketString);
			ParserReadText(pTicketString, parse_AccountTicketSigned, &signedTicket, 0);

			if (signedTicket.ticketText && signedTicket.strTicketTPI)
			{
				pUserData->pTicket = StructCreate(parse_AccountTicket);
				if (pUserData->pTicket)
				{
					ParserReadTextSafe(signedTicket.ticketText, signedTicket.strTicketTPI, signedTicket.uTicketCRC, 
						parse_AccountTicket, pUserData->pTicket, 0);

					if (eaSize(&pUserData->pTicket->ppPermissions) == 0)
					{
						StructDestroy(parse_AccountTicket, pUserData->pTicket);
						pUserData->pTicket = NULL;
					}
				}
				else
				{
					Errorf("StructCreate of AccountTicket returned NULL!");
					StructDeInit(parse_AccountTicketSigned, &signedTicket);
					estrDestroy(&pTicketString);
					return TICKET_FAILED_PARSE;
				}
			}
			StructDeInit(parse_AccountTicketSigned, &signedTicket);
			estrDestroy(&pTicketString);
			return TICKET_SUCCESS;
		}
	xcase ACCOUNTVALIDATORRESULT_FAILED_CONN_TIMEOUT:
		accountValidatorDestroyLink(accountValidatorGetPersistent());
	case ACCOUNTVALIDATORRESULT_FAILED_GENERIC:
		return TICKET_CONNECTION_FAILED;
	xcase ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_FAILED:
		{
			return TICKET_AUTH_FAILED;
		}
	}
	return TICKET_UNKNOWN_ERROR;
}

void TicketValidationOncePerFrame(void)
{
	int i;
	commMonitor(accountCommDefault());
	for (i=eaSize(&sTicketValidationQueue)-1; i>=0; i--)
	{
		TicketValidationRequest *request = sTicketValidationQueue[i];
		if (request->validator)
		{
			accountValidatorTick(request->validator);
			if (accountValidatorIsReady(request->validator))
			{
				char *errorMessage = NULL;
				TicketExtractionCode eResult;
				bool bShouldAlert = false;
				bool bShouldDisconnect = false;

				eResult = TicketValidationExtractTicket(request->userData, request->validator);
				switch(eResult)
				{
				case TICKET_SUCCESS:
					TicketProcessShardList(request->link, request->productName, request->userData);
					bShouldAlert = false;
				xcase TICKET_FAILED_PARSE:
					estrPrintf(&errorMessage, "Ticket parsing failed");
					bShouldAlert = true;
					bShouldDisconnect = true;
				xcase TICKET_CONNECTION_FAILED:
					estrPrintf(&errorMessage, "Account server connection failed");
					bShouldAlert = true;
					bShouldDisconnect = true;
				xcase TICKET_AUTH_FAILED:
					estrPrintf(&errorMessage, "User authentication failed");
					bShouldAlert = false;
				xcase TICKET_UNKNOWN_ERROR:
					estrPrintf(&errorMessage, "Unknown error");
					bShouldAlert = true;
					bShouldDisconnect = true;
				}

				if (errorMessage)
				{
					request->iRetryCount++;
					if (eResult == TICKET_AUTH_FAILED || request->iRetryCount == 3)
					{
						ShardInfo_Basic_List outList = {0};
						Packet *pOutPack;
						struct in_addr ina = {0};
						char *ipString;
						ina.S_un.S_addr = linkGetIp(request->link);
						ipString = inet_ntoa(ina);

						objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_NoTicket_ShardList", NULL, 
							"IP: %s; Ticket Extraction Error: %s", ipString, errorMessage);

						if (bShouldDisconnect)
						{
							linkRemove_wReason(&request->link, "Unhandlable error during auth");
						}	
						else
						{
							outList.pMessage = errorMessage;
							pOutPack = pktCreate(request->link, FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_IS_SHARD_LIST);
							ParserSendStructSafe(parse_ShardInfo_Basic_List, pOutPack, &outList);
							pktSend(&pOutPack);
						}

						if (bShouldAlert)
						{
							TriggerAlertf("TICKET_AUTH_FAILED", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0,0, 0, 0, 0, NULL, 0, "Something went wrong while authenticating a player through a controller tracker. This is likely causing people to be unable to log on: %s",
								errorMessage);
						}

						estrDestroy(&errorMessage);


					}
					else
					{
						estrDestroy(&errorMessage);
						accountValidatorRestartValidation(request->validator);
						return;
					}
				}
				accountValidatorRemoveValidateRequest(accountValidatorGetPersistent(), request->validator);
				accountValidatorDestroy(request->validator);
				removeTicketValidationRequest(i);
			}
		}
		else
		{
			removeTicketValidationRequest(i);
		}
	}
}

bool TicketIsAllAll(AccountTicket *pTicket)
{
	if (eaSize(&pTicket->ppPermissions) == 1
		&& stricmp(pTicket->ppPermissions[0]->pProductName, "all") == 0
		&& stricmp(pTicket->ppPermissions[0]->pPermissionString, "all") == 0)
	{
		return true;
	}

	return false;
}

bool TicketSupportsShardCategory(AccountTicket *pTicket, const char *pCategoryName, const char *pProductName)
{
	if (TicketIsAllAll(pTicket))
	{
		return true;
	}
	else
	{

		AccountPermissionStruct* productPermissions = findGamePermissionsByShard(pTicket, pProductName, pCategoryName);
		AccountPermissionStruct* pAllPermissions = findGamePermissionsByShard(pTicket, pProductName, "all");

		if (!productPermissions && !pAllPermissions)
			return false;
		if (productPermissions)
		{
			char **pTokens = NULL;
			bool bCanLogin = true;
			U32 uTime;
			permissionsParseTokenString(&pTokens, productPermissions);
			//bCanLogin = ProcessSpecialTokenAccess(pTokens, pProductName);
			eaDestroyEx(&pTokens, freeWrapper);

			if (!bCanLogin)
				return false;

			uTime = timeSecondsSince2000();
			if (!permissionsCheckStartTime(productPermissions, uTime) || !permissionsCheckEndTime(productPermissions, uTime))
				return false;
		}
		// TODO special controller tracker parsing of tokens
		return true;
	}
}

static void TicketProcessShardList(NetLink *link, const char *pProductName, MCPConnectionUserData *pUserData)
{
	int i, j;
	const char *pAll = allocAddString("all");
	ShardInfo_Basic_List outList = {0};
	Packet *pOutPack;
	ShardInfo_Basic_List *pListToActuallySend;

	struct in_addr ina = {0};
	char *ipString;
	ina.S_un.S_addr = linkGetIp(link);
	ipString = inet_ntoa(ina);

	if (!pUserData->pTicket)
	{
		if (ipfIsLocalIp(linkGetIp(link)))
		{
			//got no ticket from a local IP... we trust this person, so give them "all" "all" permissions
			AccountPermissionStruct *pPermission = StructCreate(parse_AccountPermissionStruct);
			pUserData->pTicket = StructCreate(parse_AccountTicket);
			estrPrintf(&pPermission->pProductName, "all");
			estrPrintf(&pPermission->pPermissionString, "all");
			eaPush(&pUserData->pTicket->ppPermissions, pPermission);
			objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_NoTicket_ShardList", NULL, 
				"Local IP: %s; Product Name: %s", ipString, pProductName);
		}
		else
		{
			//got no ticket from a NON-local IP... we don't trust this person, so give them permissions only for the
			//live server (this won't let them log in or anything, but will let them get patching information etc)
			//
			//Now we are changing this. Talk to Noah.
//			AccountPermissionStruct *pPermission = StructCreate(parse_AccountPermissionStruct);
			pUserData->pTicket = StructCreate(parse_AccountTicket);
/*			estrCopy2(&pPermission->pProductName, pProductName);
			estrPrintf(&pPermission->pPermissionString, "live");
			eaPush(&pUserData->pTicket->ppPermissions, pPermission);*/
			objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_NoTicket_ShardList", NULL, 
				"External IP: %s; Product Name: %s", ipString, pProductName);
		}
	}
	else
	{
		objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, pUserData->pTicket->accountName, "ControllerTracker_ShardListTicket", NULL, 
			"Account Name: %s; Product Name: %s", pUserData->pTicket->accountName, pProductName);
	}

	for (i=0; i < eaSize(&gppShardCategories); i++)
	{
		if (gppShardCategories[i]->pProductName == pProductName || pProductName == pAll)
		{
			if (TicketSupportsShardCategory(pUserData->pTicket, gppShardCategories[i]->pCategoryName, gppShardCategories[i]->pProductName))
			{
				for (j=0; j < eaSize(&gppShardCategories[i]->ppShards); j++)
				{
					eaPush(&outList.ppShards, &gppShardCategories[i]->ppShards[j]->basicInfo);
					objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, pUserData->pTicket->accountName, "ControllerTracker_ShardList", NULL, 
						"Product Name: %s; Shard Category: %s; Shard Name: %s",  
						gppShardCategories[i]->pProductName, 
						gppShardCategories[i]->pCategoryName, 
						gppShardCategories[i]->ppShards[j]->basicInfo.pShardName);
				}
			}
		}
	}

	for (i=0; i < eaSize(&gStaticData.ppPermanentShards); i++)
	{
		if (gStaticData.ppPermanentShards[i]->basicInfo.pProductName == pProductName || pProductName == pAll)
		{
			if (TicketSupportsShardCategory(pUserData->pTicket, gStaticData.ppPermanentShards[i]->basicInfo.pShardCategoryName, gStaticData.ppPermanentShards[i]->basicInfo.pProductName))
			{
				//check to see if there's a non-permanent version of this already sent
				bool bFound = false;
				for (j=0; j < eaSize(&outList.ppShards); j++)
				{
					if (stricmp(outList.ppShards[j]->pShardName, gStaticData.ppPermanentShards[i]->basicInfo.pShardName) == 0)
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					eaPush(&outList.ppShards, &gStaticData.ppPermanentShards[i]->basicInfo);
					objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, pUserData->pTicket->accountName, "ControllerTracker_ShardList", NULL, 
						"(permanent) Product Name: %s; Shard Category: %s; Shard Name: %s",  
						gStaticData.ppPermanentShards[i]->basicInfo.pProductName, 
						gStaticData.ppPermanentShards[i]->basicInfo.pShardCategoryName, 
						gStaticData.ppPermanentShards[i]->basicInfo.pShardName);
				}
			}
		}
	}
	

	if (eaSize(&outList.ppShards) == 0)
	{
		if (gbVerboseLogWhenNoShardsFound)
		{
			char *pFullString = NULL;
			char *pTempString = NULL;

			estrPrintf(&pFullString, "About to send zero shards. Ticket:\n");
			ParserWriteText(&pTempString, parse_AccountTicket, pUserData->pTicket, 0, 0, 0);
			estrConcatf(&pFullString, "%s\n", pTempString);

			estrConcatf(&pFullString, "\nAll Shards:\n");

	
			for (i=0; i < eaSize(&gppShardCategories); i++)
			{
				for (j=0; j < eaSize(&gppShardCategories[i]->ppShards); j++)
				{
					estrClear(&pTempString);
					ParserWriteText(&pTempString, parse_ShardInfo_Full, gppShardCategories[i]->ppShards[j], 0, 0, 0);
					estrConcatf(&pFullString, "%s\n", pTempString);
				}
			}

			estrConcatf(&pFullString, "\nPermanent shards:\n");

			for (i=0; i < eaSize(&gStaticData.ppPermanentShards); i++)
			{
				estrClear(&pTempString);
				ParserWriteText(&pTempString, parse_ShardInfo_Perm, gStaticData.ppPermanentShards[i], 0, 0, 0);
				estrConcatf(&pFullString, "%s\n", pTempString);
			}

			objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, pUserData->pTicket->accountName, "SendingZeroShards", NULL, 
				"%s", pFullString);
			estrDestroy(&pTempString);
			estrDestroy(&pFullString);
		}
	

		outList.pMessage = STACK_SPRINTF("You have a ticket with %d permission pairs, but no shards were found",
			eaSize(&pUserData->pTicket->ppPermissions));
		outList.pUserMessage = "No shards were found";
	}

	pListToActuallySend = ProcessShardListForClusters(&outList);


	pOutPack = pktCreate(link, FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_IS_SHARD_LIST);
	ParserSendStructSafe(parse_ShardInfo_Basic_List, pOutPack, pListToActuallySend);
	pktSend(&pOutPack);

	StructDestroy(parse_ShardInfo_Basic_List, pListToActuallySend);
	eaDestroy(&outList.ppShards);
}

int MCPConnectionDisconnect(NetLink* link,MCPConnectionUserData *pUserData)
{
	int i;
	if (pUserData->pTicket)
	{
		StructDestroy(parse_AccountTicket, pUserData->pTicket);
		pUserData->pTicket = NULL;
	}
	for (i=eaSize(&sTicketValidationQueue)-1; i>=0; i--)
	{
		TicketValidationRequest *request = sTicketValidationQueue[i];
		if (request->link == link)
		{
			removeTicketValidationRequest(i);
		}
	}
	return 1;
}

void HandleShardListRequest(Packet *pak, NetLink *link, MCPConnectionUserData *pUserData)
{
	ShardInfo_Basic_List outList = {0};
	Packet *pOutPack;
	bool bGotNoTicket = false;
	bool bGotTicketID = false;
	const char *pProductName = allocAddString(pktGetStringTemp(pak));


	struct in_addr ina = {0};
	char *ipString;
	ina.S_un.S_addr = linkGetIp(link);
	ipString = inet_ntoa(ina);

	if (pUserData->pTicket)
	{
		StructDestroy(parse_AccountTicket, pUserData->pTicket);
		pUserData->pTicket = NULL;
	}
	
	if (sbGiveAllShardsToLocal && isLocalIp(linkGetIp(link)))
	{
		//got no ticket from a local IP... we trust this person, so give them "all" "all" permissions
		AccountPermissionStruct *pPermission = StructCreate(parse_AccountPermissionStruct);
		pUserData->pTicket = StructCreate(parse_AccountTicket);
		estrPrintf(&pPermission->pProductName, "all");
		estrPrintf(&pPermission->pPermissionString, "all");
		eaPush(&pUserData->pTicket->ppPermissions, pPermission);
		objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_NoTicket", NULL, 
			"Local IP: %s", ipString);
		TicketProcessShardList(link, pProductName, pUserData);
		return;
	}



	if (!pktEnd(pak))
	{
		char *pTicketString = pktGetStringTemp(pak);
		
		if (!pTicketString[0])
		{
			bGotNoTicket = true;
		}
		else if (stricmp(pTicketString, ACCOUNT_FASTLOGIN_LABEL) == 0)
		{
			U32 uAccountID = pktGetU32(pak);
			U32 uTicketID = pktGetU32(pak);
			TicketValidationRequest *request = calloc(1, sizeof(TicketValidationRequest));
			request->validator = accountValidatorAddValidateRequest(accountValidatorGetPersistent(), uAccountID, uTicketID);
			if (!request->validator)
			{
				objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_FastLogin_Start", NULL, 
					"Account ID: %d; Could not create link to Account Server", uAccountID);
				outList.pMessage = "Error connecting to Account Server";
				outList.pUserMessage = "Error connecting to Account Server";
				pOutPack = pktCreate(link, FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_IS_SHARD_LIST);
				ParserSendStructSafe(parse_ShardInfo_Basic_List, pOutPack, &outList);
				pktSend(&pOutPack);
				return;
			}
			request->link = link;
			request->productName = pProductName;
			request->userData = pUserData;

			objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_FastLogin_Start", NULL, 
				"Account ID: %d; Ticket ID: %d; Product Name: %s", uAccountID, uTicketID, pProductName);
			eaPush(&sTicketValidationQueue, request);
			bGotTicketID = true;
		}
		else // Old-style "slow" login no longer supported
		{
			AccountTicketSigned signedTicket = {0};
			ParserReadText(pTicketString, parse_AccountTicketSigned, &signedTicket, 0);

			outList.pMessage = "Invalid Permissions... no shards available";
			outList.pUserMessage = "No shards were found";
			pOutPack = pktCreate(link, FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_IS_SHARD_LIST);
			ParserSendStructSafe(parse_ShardInfo_Basic_List, pOutPack, &outList);
			pktSend(&pOutPack);
			objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_SlowLogin_Start", NULL, 
				"Invalid Ticket Signature: %s", pTicketString);
			StructDeInit(parse_AccountTicketSigned, &signedTicket);
		}
	}
	else
	{
		bGotNoTicket = true;
	}

	if (bGotNoTicket)
	{
		if (isLocalIp(linkGetIp(link)))
		{
			//got no ticket from a local IP... we trust this person, so give them "all" "all" permissions
			AccountPermissionStruct *pPermission = StructCreate(parse_AccountPermissionStruct);
			pUserData->pTicket = StructCreate(parse_AccountTicket);
			estrPrintf(&pPermission->pProductName, "all");
			estrPrintf(&pPermission->pPermissionString, "all");
			eaPush(&pUserData->pTicket->ppPermissions, pPermission);
			objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_NoTicket", NULL, 
				"Local IP: %s", ipString);
		}
		else
		{
			//got no ticket from a NON-local IP... we don't trust this person, so give them permissions only for the
			//live server (this won't let them log in or anything, but will let them get patching information etc)
			//
			//ABW now we are changing this. Talk to Noah.
//			AccountPermissionStruct *pPermission = StructCreate(parse_AccountPermissionStruct);
			pUserData->pTicket = StructCreate(parse_AccountTicket);
		/*	estrCopy2(&pPermission->pProductName, pProductName);
			estrPrintf(&pPermission->pPermissionString, "live");
			eaPush(&pUserData->pTicket->ppPermissions, pPermission);*/
			objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_NoTicket", NULL, 
				"External IP: %s", ipString);
		}
	}

	if (!bGotTicketID)
		TicketProcessShardList(link, pProductName, pUserData);
}

void HandleLastMinuteFileRequest(Packet *pak, NetLink *link, MCPConnectionUserData *pUserData)
{
	int iShardID = pktGetBits(pak, 32);
	ShardInfo_Full *pShard;
	Packet *pOutPack = pktCreate(link, FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_ARE_LAST_MINUTE_FILES);
	AllLastMinuteFilesInfo outList = {0};

	if (stashIntFindPointer(gShardsByID, iShardID, &pShard) && pShard->pAllLastMinuteFiles)
	{
		ParserSendStructSafe(parse_AllLastMinuteFilesInfo, pOutPack, pShard->pAllLastMinuteFiles);
	}
	else
	{
		ParserSendStructSafe(parse_AllLastMinuteFilesInfo, pOutPack, &outList);
	}

	pktSend(&pOutPack);
}


void HandleXboxShardListRequest(Packet *pak, NetLink *link, MCPConnectionUserData *pUserData)
{
	const char *pProductName = allocAddString(pktGetStringTemp(pak));
	Packet *pOutPack;
	int i, j;
	const char *pAll = allocAddString("all");
	ShardInfo_Basic_List outList = {0};
	ShardInfo_Basic_List *pListToActuallySend;

	char *ipString;
	struct in_addr ina = {0};
	ina.S_un.S_addr = linkGetIp(link);
	ipString = inet_ntoa(ina);

	if(!ipfIsIpInGroup("XLSP", linkGetIp(link)))
	{
		linkRemove_wReason(&link, "Attempt to request Xbox shards from non-XLSP gateway IP");
		return;
	}

	StructInit(parse_ShardInfo_Basic_List, &outList);


	for (i=0; i < eaSize(&gppShardCategories); i++)
	{
		if (strstri(gppShardCategories[i]->pCategoryName, "xbox"))
		{
			if (gppShardCategories[i]->pProductName == pProductName || pProductName == pAll)
			{
				for (j=0; j < eaSize(&gppShardCategories[i]->ppShards); j++)
				{
					eaPush(&outList.ppShards, &gppShardCategories[i]->ppShards[j]->basicInfo);
					objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_ShardList", NULL, 
						"Product Name: %s; Shard Category: %s; Shard Name: %s",  
						gppShardCategories[i]->pProductName, 
						gppShardCategories[i]->pCategoryName, 
						gppShardCategories[i]->ppShards[j]->basicInfo.pShardName);
				}
			}
		}
	}

	for (i=0; i < eaSize(&gStaticData.ppPermanentShards); i++)
	{
		if (strstri(gStaticData.ppPermanentShards[i]->basicInfo.pShardCategoryName, "xbox"))
		{
			if (gStaticData.ppPermanentShards[i]->basicInfo.pProductName == pProductName || pProductName == pAll)
			{
				//check to see if there's a non-permanent version of this already sent
				bool bFound = false;
				for (j=0; j < eaSize(&outList.ppShards); j++)
				{
					if (stricmp(outList.ppShards[j]->pShardName, gStaticData.ppPermanentShards[i]->basicInfo.pShardName) == 0)
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					eaPush(&outList.ppShards, &gStaticData.ppPermanentShards[i]->basicInfo);
					objLog(LOG_LOGIN, 0, 0, 0, ipString, NULL, ipString, "ControllerTracker_ShardList", NULL, 
						"(permanent) Product Name: %s; Shard Category: %s; Shard Name: %s",  
						gStaticData.ppPermanentShards[i]->basicInfo.pProductName, 
						gStaticData.ppPermanentShards[i]->basicInfo.pShardCategoryName, 
						gStaticData.ppPermanentShards[i]->basicInfo.pShardName);
				}		
			}
		}
	}

	pListToActuallySend = ProcessShardListForClusters(&outList);

	pOutPack = pktCreate(link, FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_IS_SHARD_LIST);
	ParserSendStructSafe(parse_ShardInfo_Basic_List, pOutPack, pListToActuallySend);
	pktSend(&pOutPack);

	eaDestroy(&outList.ppShards);
	StructDeInit(parse_ShardInfo_Basic_List, &outList);
	StructDestroy(parse_ShardInfo_Basic_List, pListToActuallySend);
}



void MCPConnectionHandleMsg(Packet *pak,int cmd, NetLink *link,MCPConnectionUserData *pUserData)
{
	switch(cmd)
	{
	case FROM_MCP_TO_NEWCONTROLLERTRACKER_REQUEST_SHARD_LIST:
		HandleShardListRequest(pak, link, pUserData);
		break;
	case FROM_MCP_TO_NEWCONTROLLERTRACKER_REQUEST_LAST_MINUTE_FILES:
		HandleLastMinuteFileRequest(pak, link, pUserData);
		break;
	case FROM_MCP_TO_NEWCONTROLLERTRACKER_REQUEST_XBOX_SHARD_LIST:
		HandleXboxShardListRequest(pak, link, pUserData);
		break;
	default:
		break;
	}
}

void InitMCPCom(void)
{
	loadstart_printf("Trying to start listening for MCPs...");
	while (!commListen(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,NEWCONTROLLERTRACKER_GENERAL_MCP_PORT,
			MCPConnectionHandleMsg,NULL,MCPConnectionDisconnect,sizeof(MCPConnectionUserData)))
	{
		Sleep(1);
	}

	loadend_printf("done");
}

int SortByClusterName(const ShardInfo_Basic **ppList1, const ShardInfo_Basic **ppList2)
{
	return stricmp((*ppList1)->pClusterName, (*ppList2)->pClusterName);
}



ShardInfo_Basic_List *ProcessShardListForClusters(ShardInfo_Basic_List *pInList)
{
	ShardInfo_Basic **ppShardsInClusters = NULL;
	int i;
	ShardInfo_Basic_List *pRetVal = StructClone(parse_ShardInfo_Basic_List, pInList);
	ShardInfo_Basic *pCurClusterDefiner = NULL;

	if (!eaSize(&pInList->ppShards))
	{
		return pRetVal;
	}

	for (i = eaSize(&pRetVal->ppShards) - 1; i >= 0; i--)
	{
		if (pRetVal->ppShards[i]->pClusterName)
		{
			eaPush(&ppShardsInClusters, eaRemove(&pRetVal->ppShards, i));
		}
	}

	if (!eaSize(&ppShardsInClusters))
	{
		return pRetVal;
	}

	eaQSort(ppShardsInClusters, SortByClusterName);


	for (i = 0; i < eaSize(&ppShardsInClusters); i++)
	{
		ShardInfo_Basic *pCurShard = ppShardsInClusters[i];
		if (pCurClusterDefiner && stricmp(pCurClusterDefiner->pClusterName, pCurShard->pClusterName))
		{
			eaPush(&pRetVal->ppShards, pCurClusterDefiner);
			pCurClusterDefiner = NULL;
		}

		if (!pCurClusterDefiner)
		{
			pCurClusterDefiner = StructClone(parse_ShardInfo_Basic, pCurShard);
			SAFE_FREE(pCurClusterDefiner->pShardName);
			pCurClusterDefiner->pShardName = strdup(pCurClusterDefiner->pClusterName);
		}
		else
		{
			ea32Append(&pCurClusterDefiner->allLoginServerIPs, &pCurShard->allLoginServerIPs);
			
			if (pCurShard->pLoginServerPortsAndIPs)
			{
				if (!pCurClusterDefiner->pLoginServerPortsAndIPs)
				{
					pCurClusterDefiner->pLoginServerPortsAndIPs = StructCreate(parse_PortIPPairList);
				}

				eaPushEArray(&pCurClusterDefiner->pLoginServerPortsAndIPs->ppPortIPPairs,
					&pCurShard->pLoginServerPortsAndIPs->ppPortIPPairs);
				eaDestroy(&pCurShard->pLoginServerPortsAndIPs->ppPortIPPairs);
			}

		}
	}

	eaPush(&pRetVal->ppShards, pCurClusterDefiner);

	eaDestroyStruct(&ppShardsInClusters, parse_ShardInfo_Basic);




	return pRetVal;
}
	
