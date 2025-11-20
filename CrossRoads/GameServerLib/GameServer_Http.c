#include "serverlib.h"
#include "autogen/GameServer_Http_c_ast.h"
#include "httpXpathSupport.h"
#include "Autogen/controller_autogen_remotefuncs.h"
#include "gameserverlib.h"
#include "GameAccountDataCommon.h"
#include "httpJpegLibrary.h"
#include "EntityLib.h"
#include "wlBeacon.h"
#include "gslBeaconInterface.h"
#include "File.h"
#include "../common/autogen/GameClientLib_autogen_clientcmdwrappers.h"
#include "ServerLibCmdParse.h"
#include "HttpLib.h"
#include "Timing.h"
#include "Character.h"
#include "PowersAutoDesc.h"
#include "powerTree.h"
#include "Gateway/gslGatewayServer.h"
#include "GamePermissionsCommon.h"

AUTO_STRUCT;
typedef struct GameServerOverview
{
	char *pBrowsePlayers; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
	char *pBrowseCritters; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
	
	char *pCurEntityDensity; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))

	AST_COMMAND("Add a test client", "AddTestClient $SELECT(What type of test client?|NORMAL,HASGRAPHICS) $STRING(Extra Command Line) $NORETURN")

    GamePermissionDefs *gamePermissionDefs;
} GameServerOverview;



GameServerOverview *GetGameServerOverview(void)
{
	static GameServerOverview *pGameServerOverview = NULL;
	static U32 iTimeStamp = 0;

	if (pGameServerOverview && (timeSecondsSince2000() - iTimeStamp < 3))
	{
		return pGameServerOverview;
	}

	if (pGameServerOverview)
	{
        pGameServerOverview->gamePermissionDefs = NULL;
		StructDestroy(parse_GameServerOverview, pGameServerOverview);
	}
	pGameServerOverview = StructCreate(parse_GameServerOverview);

	estrPrintf(&pGameServerOverview->pBrowsePlayers, "<a href=\"%s%s%s\">Players</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, GlobalTypeToName(GLOBALTYPE_ENTITYPLAYER));
/*	estrPrintf(&pGameServerOverview->pBrowsePlayers, "<a href=\"%s%s%s\">Saved Pets</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, GlobalTypeToName(GLOBALTYPE_ENTITYSAVEDPET));*/
	estrPrintf(&pGameServerOverview->pBrowseCritters, "<a href=\"%s%s%s\">Critters</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, GlobalTypeToName(GLOBALTYPE_ENTITYCRITTER));


	estrPrintf(&pGameServerOverview->pGenericInfo, "<input type=\"hidden\" id=\"defaultautorefresh\" value=\"1\"><a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));

	estrPrintf(&pGameServerOverview->pCurEntityDensity, "<a href=\"/viewimage?imagename=GAMESERVER_%u_Heatmap_EntityDensity.jpg&jpgMinOutputSize=500&jpgYellowCutoff=2&jpgRedCutoff=3&jpgGameUnitsPerPixel=16&jpgPenRadius=3\">Current entity density heatmap</a>",
		GetAppGlobalID());

/*
	{	
		char *pTempString;
		estrStackCreate(&pTempString);
		JpegLibrary_GetFixedUpJpegFileName(&pTempString, "server/jpgs/logo_large.jpg");
		estrPrintf(&pGameServerOverview->pLogo, "<IMG src=\"%s\" alt=\"Cryptic Logo\">", pTempString);
		estrDestroy(&pTempString);
	}*/

    pGameServerOverview->gamePermissionDefs = &g_GamePermissions;

	return pGameServerOverview;
}



void OVERRIDE_LATELINK_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	if(beaconIsBeaconizer())
	{
		gslBeaconGetInfoStructForHttp(pUrl, ppTPI, ppStruct);
	}
	else if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
	{
		gslGatewayServer_GetCustomServerInfoStructForHttp(pUrl, ppTPI, ppStruct);
	}
	else
	{
		*ppTPI = parse_GameServerOverview;
		*ppStruct = GetGameServerOverview();
	}
}


AUTO_COMMAND;
void AddTestClient(char *pTestClientType, ACMD_SENTENCE pExtraCommandLine)
{
	

	char *pCommandLine = NULL;
	estrStackCreate(&pCommandLine);
	estrPrintf(&pCommandLine, " -loginServerNameForClient $LOGINSERVERIP -mapNameForClient %s -scriptName MoveRandomNoAttack -instanceIndexForClient %d %s %s",
		gGSLState.gameServerDescription.baseMapDescription.mapDescription, gGSLState.gameServerDescription.baseMapDescription.mapInstanceIndex,
		(stricmp(pTestClientType, "HASGRAPHICS") == 0) ? "-showGraphics" : "", pExtraCommandLine);

	RemoteCommand_StartServer_NoReturn(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_TESTCLIENT, 0, pCommandLine, "AddTestClient()", NULL);

	estrDestroy(&pCommandLine);


}

#define GSL_SCREENSHOT_REQ_EXPIRATION_TIME 20

typedef struct ScreenShotCache
{
	int iRequestID;
	U32 iRequestTime; //screenshot requests time out after 20 seconds
	JpegLibrary_ReturnJpegCB *pCB;
	void *pUserData;
} ScreenShotCache;

static ScreenShotCache **sppScreenShotCaches = NULL;
static int siNextScreenShotID = 1;


void PurgeScreenShotCache(void)
{
	int i;
	U32 iTimeCutOff = timeSecondsSince2000() - GSL_SCREENSHOT_REQ_EXPIRATION_TIME;

	PERFINFO_AUTO_START_FUNC();

	for (i=eaSize(&sppScreenShotCaches) - 1; i >= 0; i--)
	{
		if (sppScreenShotCaches[i]->iRequestTime < iTimeCutOff)
		{
			sppScreenShotCaches[i]->pCB(NULL, 0, 0, "Timeout while waiting for screenshot", sppScreenShotCaches[i]->pUserData);

			free(sppScreenShotCaches[i]);
			eaRemove(&sppScreenShotCaches, i);
		}
	}

	PERFINFO_AUTO_STOP();
}



void GetScreenShotCB(char *pName, UrlArgumentList *pUrlArgs, JpegLibrary_ReturnJpegCB *pCB, void *pUserData)
{
	ContainerID iEntID = 0;
	Entity *pEntity;
	ScreenShotCache *pCache;

	sscanf(pName, "%u.jpg", &iEntID);
	if (iEntID == 0)
	{
		pCB(NULL, 0, 0, "Invalid JPEG syntax", pUserData);
		return;
	}

	pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);

	if (!pEntity)
	{
		pCB(NULL, 0, 0, STACK_SPRINTF("Can't screenshot invalid entity %u", iEntID), pUserData);
		return;
	}



	pCache = malloc(sizeof(ScreenShotCache));
	

	pCache->iRequestID = siNextScreenShotID++;
	pCache->iRequestTime = timeSecondsSince2000();
	pCache->pCB = pCB;
	pCache->pUserData = pUserData;

	eaPush(&sppScreenShotCaches, pCache);

	ClientCmd_ScreenshotForServerMonitor(pEntity, pCache->iRequestID);
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void HereIsScreenshotForServerMonitor(TextParserBinaryBlock *pInBlock, int iRequestID)
{
	int i;

	for (i=0; i < eaSize(&sppScreenShotCaches); i++)
	{
		if (sppScreenShotCaches[i]->iRequestID == iRequestID)
		{
			int iSize;
			char *pBuf = TextParserBinaryBlock_PutIntoMallocedBuffer(pInBlock, &iSize);

			if (iSize)
			{
				sppScreenShotCaches[i]->pCB(pBuf, iSize, 10, NULL, sppScreenShotCaches[i]->pUserData);

				free(pBuf);
			}
			else
			{
				sppScreenShotCaches[i]->pCB(NULL, 0, 0, "Unknown screenshotting error", sppScreenShotCaches[i]->pUserData);
			}

			free(sppScreenShotCaches[i]);
			eaRemove(&sppScreenShotCaches, i);
			return;
		}
	}
}

AUTO_STRUCT;
typedef struct PlayerPowerAutoTextRequestCache
{
	U32 iPlayerID;
	U32 iPowerID;
	GetStructForHttpXpath_CB *pCB; NO_AST
	U32 iReqID1;
	U32 iReqID2;
	U32 iInitRequestTime;
	REF_TO(Entity) hPlayer;
} PlayerPowerAutoTextRequestCache;

static PlayerPowerAutoTextRequestCache **sppAutoTextRequestCaches = NULL;

static void PowerAutoDescForWeb(int iPartitionIdx,
								SA_PARAM_NN_VALID Power *ppow,
								SA_PARAM_NN_VALID Character *pchar,
								Language eLanguage,
								SA_PARAM_NN_VALID char **ppchDesc,
								GameAccountDataExtract *pExtract)
{
	if(!pchar->iLevelCombat)
	{
		pchar->iLevelCombat = 1;
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
	}
	power_AutoDesc(iPartitionIdx, ppow, pchar, ppchDesc, NULL, "<line>", "<indent>", "<prefix>", false, 0, kAutoDescDetail_Normal, pExtract,NULL);
}




void PlayerPowerAutoTextDelayedCB(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, U32 iReqID1, U32 iReqID2, GetStructForHttpXpath_CB *pCB, GetHttpFlags eFlags)
{
	U32 iPlayerID;
	U32 iPowerID;
	char idBuf[128];
	int iResult;

	Entity *pPlayer;

	PlayerPowerAutoTextRequestCache *pCache;

	StructInfoForHttpXpath structInfo = {0};

	if (!devassert(GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER))
		return;

	iResult = urlFindUInt(pArgList, "svrPlayerID", &iPlayerID);

	if (iResult != 1)
	{
		GetMessageForHttpXpath("Couldn't find valid PlayerID", &structInfo, 1);
		pCB(iReqID1, iReqID2, &structInfo);
		return;
	}

	iResult = urlFindUInt(pArgList, "svrPowerID", &iPowerID);

	if (iResult != 1)
	{
		GetMessageForHttpXpath("Couldn't find valid powerID", &structInfo, 1);
		pCB(iReqID1, iReqID2, &structInfo);
		return;
	}


	pCache = StructCreate(parse_PlayerPowerAutoTextRequestCache);
	SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(iPlayerID, idBuf), pCache->hPlayer);

	pPlayer = GET_REF(pCache->hPlayer);

	if (pPlayer)
	{
		Character *pCharacter = entGetChar(pPlayer);

		if (pCharacter)
		{
			Power *pPower = character_FindPowerByIDTree(pCharacter, iPowerID, NULL, NULL);
			pCharacter->pEntParent = pPlayer;

			if (pPower)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayer);
				char *pMessage = NULL;
				PowerAutoDescForWeb(0, pPower, pCharacter, LANGUAGE_DEFAULT, &pMessage, pExtract);
				GetMessageForHttpXpath(pMessage, &structInfo, 0);
				pCB(iReqID1, iReqID2, &structInfo);

				StructDestroy(parse_PlayerPowerAutoTextRequestCache, pCache);
				estrDestroy(&pMessage);
				StructDeInit(parse_StructInfoForHttpXpath, &structInfo);

				return;
			}
		}


		GetMessageForHttpXpath("Couldn't find character or power", &structInfo, 1);
		pCB(iReqID1, iReqID2, &structInfo);

		StructDestroy(parse_PlayerPowerAutoTextRequestCache, pCache);
		StructDeInit(parse_StructInfoForHttpXpath, &structInfo);

		return;
	}
	else
	{
		pCache->iPlayerID = iPlayerID;
		pCache->iPowerID = iPowerID;
		pCache->pCB = pCB;
		pCache->iReqID1 = iReqID1;
		pCache->iReqID2 = iReqID2;
		pCache->iInitRequestTime = timeSecondsSince2000();
		eaPush(&sppAutoTextRequestCaches, pCache);
	}
}

void GameServerHttp_OncePerFrame(void)
{
	int i;
	
	PERFINFO_AUTO_START_FUNC();

	for (i=eaSize(&sppAutoTextRequestCaches) - 1; i >= 0; i--)
	{
		PlayerPowerAutoTextRequestCache *pCache = sppAutoTextRequestCaches[i];

		Entity *pPlayer = GET_REF(pCache->hPlayer);

		if (pPlayer)
		{
			bool bSucceeded  = false;
			Character *pCharacter = entGetChar(pPlayer);
			StructInfoForHttpXpath structInfo = {0};

			if (pCharacter)
			{
				Power *pPower = character_FindPowerByIDTree(pCharacter, pCache->iPowerID, NULL, NULL);

				pCharacter->pEntParent = pPlayer;

				if (pPower)
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayer);
					char *pMessage = NULL;
					PowerAutoDescForWeb(0, pPower, pCharacter, LANGUAGE_DEFAULT, &pMessage, pExtract);
					GetMessageForHttpXpath(pMessage, &structInfo, 0);
					pCache->pCB(pCache->iReqID1, pCache->iReqID2, &structInfo);

					estrDestroy(&pMessage);
					StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
					StructDestroy(parse_PlayerPowerAutoTextRequestCache, pCache);

					eaRemoveFast(&sppAutoTextRequestCaches, i);
				
					bSucceeded = true;
				}
			}

			if (!bSucceeded)
			{
				char *pMessage = NULL;
				GetMessageForHttpXpath("Couldn't find character or power", &structInfo, 1);

				pCache->pCB(pCache->iReqID1, pCache->iReqID2, &structInfo);

				StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
				StructDestroy(parse_PlayerPowerAutoTextRequestCache, pCache);

				eaRemoveFast(&sppAutoTextRequestCaches, i);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}
	



AUTO_RUN;
void registerGSLHttpStuff(void)
{
	JpegLibrary_RegisterCB("SCREENSHOT", GetScreenShotCB);
	RegisterCustomXPathDomain(".playerPowerAutoText", NULL, PlayerPowerAutoTextDelayedCB);

}

#include "autogen/GameServer_Http_c_ast.c"
