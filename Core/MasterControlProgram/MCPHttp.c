#include "serverlib.h"
#include "HttpLib.h"
#include "HttpServing.h"
#include "FileServing.h"
#include "MCPHttp.h"
#include "ControllerLInk.h"
#include "StructNet.h"
#include "Alerts.h"
#include "HttpServingStats.h"
#include "estring.h"
#include "cmdparse.h"
#include "utilitiesLib.h"

static FileServingRequestFulfilledCallBack *spFileFulfillCB = NULL;

void MCP_XpathCB(GlobalType eContainerType, ContainerID iContainerID, int iRequestID, char *pXPath,
	UrlArgument **ppServerSideURLArgs, int iAccessLevel, GetHttpFlags eFlags, const char *pAuthNameAndIP)
{
	if (GetControllerLink())
	{
		int i;
		Packet *pPak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_XPATH_FOR_HTTP);
		PutContainerTypeIntoPacket(pPak,eContainerType);
		PutContainerIDIntoPacket(pPak, iContainerID);
		PutContainerIDIntoPacket(pPak, gServerLibState.containerID);
		pktSendBits(pPak, 32,iRequestID);
		pktSendBits(pPak, 32, eFlags);
		pktSendString(pPak, pXPath);

		for (i=0; i < eaSize(&ppServerSideURLArgs); i++)
		{
			pktSendString(pPak, ppServerSideURLArgs[i]->arg);
			pktSendString(pPak, ppServerSideURLArgs[i]->value);
		}

		pktSendString(pPak, "");

		pktSendBits(pPak, 32, iAccessLevel);

		pktSendString(pPak, pAuthNameAndIP);

		pktSend(&pPak);
	}
}
void MCP_CommandCB(GlobalType eContainerType, ContainerID iContainerID, int iClientID, int iRequestID,
	char *pCommandString, int iAccessLevel, bool bNoReturn, const char *pAuthNameAndIP)
{
	if (GetControllerLink())
	{
		Packet *pPak;
		pPak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_MONITORING_COMMAND);
												
		PutContainerTypeIntoPacket(pPak, eContainerType);
		PutContainerIDIntoPacket(pPak, iContainerID);

		pktSendBits(pPak, 32, iClientID);
		pktSendBits(pPak, 32, iRequestID);

		PutContainerIDIntoPacket(pPak, gServerLibState.containerID);

	
		pktSendString(pPak, pCommandString);
		pktSendString(pPak, pAuthNameAndIP);
		pktSendBits(pPak, 1, bNoReturn);

		pktSendBits(pPak, 32, iAccessLevel);

		pktSend(&pPak);
	}
}
char *MCP_CanServeCB(void)
{
	if (!GetControllerLink())
	{
		return "No controller - can not browse xpaths";
	}

	return NULL;
}

void MCP_FileCB(char *pFileName, int iRequestID, enumFileServingCommand eCommand,
	U64 iBytesRequested, FileServingRequestFulfilledCallBack *pFulfillCB)
{
	if (GetControllerLink())
	{
		Packet *pPak;
		pPak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_FILE_SERVING);
		PutContainerIDIntoPacket(pPak, GetAppGlobalID());
		pktSendString(pPak, pFileName);
		pktSendBits(pPak, 32, iRequestID);
		pktSendBits(pPak, 32, eCommand);
		pktSendBits64(pPak, 64, iBytesRequested);


		if (!spFileFulfillCB)
		{
			spFileFulfillCB = pFulfillCB;
		}
		else
		{
			//for now, assume MCP only getting file requests from one piece of code
			assert(pFulfillCB == spFileFulfillCB);
		}

		pktSend(&pPak);
	}

}


void MCP_JpegCB(char *pJpegName, UrlArgumentList *pArgList, int iRequestID)
{
	if (GetControllerLink())
	{
		Packet *pPak;
		pPak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_MONITORING_JPEG);
		pktSendBits(pPak, 32, iRequestID);
		PutContainerIDIntoPacket(pPak, gServerLibState.containerID);
		pktSendString(pPak, pJpegName);
		
		ParserSendStruct(parse_UrlArgumentList, pPak, pArgList);

		pktSend(&pPak);
	}
}

#define MCP_MIN_MONITORING_PORT 80
#define MCP_MAX_MONITORING_PORT 90
static int siMonitoringPort = MCP_MIN_MONITORING_PORT;

static bool sbMonitoringSuccessfully = false;

static U32 *piMultipleMonitoringPorts = NULL;

AUTO_COMMAND ACMD_CMDLINE;
void SetMultipleMonitoringPorts(int iMin, int iMax)
{
	int i;

	if (iMin > iMax)
	{
		int iTemp = iMin;
		iMin = iMax;
		iMax = iTemp;
	}

	for (i=iMin; i <= iMax; i++)
	{
		ea32Push(&piMultipleMonitoringPorts, i);
	}
}


void BeginMCPHttpMonitoring(void)
{
	HttpStats_Init();

	if(!sbMonitoringSuccessfully)
	{
		if (ea32Size(&piMultipleMonitoringPorts))
		{
			if (!HttpServing_Begin_MultiplePorts(&piMultipleMonitoringPorts, MCP_XpathCB, MCP_CommandCB, MCP_CanServeCB, MCP_JpegCB, MCP_FileCB,
				"/viewxpath?xpath=controller[0].custom", "server/MCP/templates/mcpHtmlHeader.txt", "server/MCP/templates/mcpHtmlFooter.txt", "server/MCP/static_home", GetProductName(), NULL))
			{
				char *pErrorMessage = NULL;
				int i;
				estrPrintf(&pErrorMessage, "Failed to initiate multi-port MCP monitoring on ports: ");
				for (i=0; i < ea32Size(&piMultipleMonitoringPorts); i++)
				{
					estrConcatf(&pErrorMessage, "%s%u", i == 0 ? "" : ", ", piMultipleMonitoringPorts[i]);
				}

				assertmsgf(0, "%s", pErrorMessage);
			}
			
			sbMonitoringSuccessfully = true;

		}
		else
		{
			if(HttpServing_Begin(siMonitoringPort, MCP_XpathCB, MCP_CommandCB, MCP_CanServeCB, MCP_JpegCB, MCP_FileCB,
				"/viewxpath?xpath=controller[0].custom", "server/MCP/templates/mcpHtmlHeader.txt", "server/MCP/templates/mcpHtmlFooter.txt", "server/MCP/static_home", GetProductName(), NULL))
			{
				sbMonitoringSuccessfully = true;
				ea32Push(&piMultipleMonitoringPorts, siMonitoringPort);
				if(siMonitoringPort != MCP_MIN_MONITORING_PORT)
				{
					TriggerAlertf("MCP_MONITORING_PORT", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_MASTERCONTROLPROGRAM, gServerLibState.containerID,
						GLOBALTYPE_MASTERCONTROLPROGRAM, gServerLibState.containerID, getHostName(), 0,
						"MCP launched web monitoring on port %d; all ports from %d to %d were in use", siMonitoringPort, MCP_MIN_MONITORING_PORT, siMonitoringPort-1);
				}
			}
			else
			{
				assertmsgf(siMonitoringPort <= MCP_MAX_MONITORING_PORT, "Failed to initiate MCP web monitoring! Ports %d through %d were in use!", MCP_MIN_MONITORING_PORT, siMonitoringPort-1);
				++siMonitoringPort;
			}
		}
	}

}



void MCPHttpMonitoringUpdate(void)
{
	HttpServing_Tick();
}

//the type and ID of the server which is providing whatever data the MCP is looking at
static GlobalType sServingTypeForMCPMonitoring = GLOBALTYPE_NONE;
static ContainerID sContainerIDForMCPMonitoring = 0;



GlobalType OVERRIDE_LATELINK_XPathSupport_GetServerType(void)
{
	return sServingTypeForMCPMonitoring;
}

ContainerID OVERRIDE_LATELINK_XPathSupport_GetContainerID(void)
{
	return sContainerIDForMCPMonitoring;
}


void handle_MCPHttpMonitoringProcessXpath(GlobalType eServingType, ContainerID iServingID, int iRequestID, StructInfoForHttpXpath *pStructInfo)
{
	sServingTypeForMCPMonitoring = eServingType;
	sContainerIDForMCPMonitoring = iServingID;

	HttpServing_XPathReturn(iRequestID, pStructInfo);
}

void HandleMonitoringCommandReturn(Packet *pPak)
{
	int iRequestID = pktGetBits(pPak, 32);
	int iClientID = pktGetBits(pPak, 32);
	char *pReturnString = pktGetStringTemp(pPak);

	HttpServing_CommandReturn(iRequestID, iClientID, pReturnString);
}

void HandleJpegReturn(Packet *pPak)
{
	int iRequestID = pktGetBits(pPak, 32);
	int iDataSize = pktGetBits(pPak, 32);
	int iLifeSpan;
	char *pBuf;

	if (!iDataSize)
	{
		char *pErrorMessage = pktGetStringTemp(pPak);
		HttpServing_JpegReturn(iRequestID, NULL, 0, 0, pErrorMessage);
		return;
	}

	pBuf = malloc(iDataSize);
	pktGetBytes(pPak, iDataSize, pBuf);

	iLifeSpan = pktGetBits(pPak, 32);

	HttpServing_JpegReturn(iRequestID, pBuf, iDataSize, iLifeSpan, NULL);
}

void HandleFileServingReturn(Packet *pak)
{
	int iRequestID = pktGetBits(pak, 32);
	char *pErrorString = pktGetBits(pak, 1) ? pktGetStringTemp(pak) : NULL;
	U64 iTotalSize = pktGetBits64(pak, 64);
	U64 iCurBeginByteOffset = pktGetBits64(pak, 64);
	U64 iCurNumBytes = pktGetBits64(pak, 64);
	char *pBuf = iCurNumBytes ? malloc(iCurNumBytes) : NULL;

	if (iCurNumBytes)
	{
		pktGetBytes(pak, iCurNumBytes, pBuf);
	}

	spFileFulfillCB(iRequestID, pErrorString, iTotalSize, iCurBeginByteOffset, iCurNumBytes, pBuf);
}


U32 **MCPHttpMonitoringGetMonitoredPorts(void)
{
	static U32 *pEmptyList = NULL;
	if (!sbMonitoringSuccessfully)
	{
		return &pEmptyList;
	}
	return &piMultipleMonitoringPorts;
}

AUTO_COMMAND;
void ExecuteCommandOnOtherServer(char *pServerTypeName, U32 iServerID, CmdContext *pContext, ACMD_SENTENCE pCommandString)
{
	static char *spTempAuthAndIPString = NULL;
	GlobalType eServerType = NameToGlobalType(pServerTypeName);
	if (!eServerType)
	{
		return;
	}

	estrPrintf(&spTempAuthAndIPString, "(ExecuteCommandOnOtherServer called on MCP via %s)", GetContextHowString(pContext));


	//less dangerous than it looks... as long as bNoReturn is true, the clientID and requestID are never used 
	MCP_CommandCB(eServerType, iServerID, 0, 0, pCommandString, pContext->access_level, true, spTempAuthAndIPString);
}





char *OVERRIDE_LATELINK_GetNotesSystemName(void)
{
	return GetShardNameFromShardInfoString();
}

void OVERRIDE_LATELINK_AddCustomHTMLFooter(char **ppOutString)
{
	estrConcatf(ppOutString,"<h1 style=\"position:fixed; left:4px; top:-10px; opacity:.7; z-index:1; color:#500\">%s(%s)</h1>",
		GetShardNameFromShardInfoString(), GetProductName());


}