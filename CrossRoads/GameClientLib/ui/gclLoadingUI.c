#include "Message.h"
#include "ResourceManager.h"
#include "Expression.h"
#include "timing.h"

#include "WorldLib.h"
#include "WorldGrid.h"
#include "UIGen.h"
#include "GameClientLib.h"
#include "gclEntity.h"
#include "Player.h"
#include "mapstate_common.h"
#include "gclMapState.h"
#include "LoadScreen_Common.h"
#include "gclSmartAd.h"
#include "EntityMovementManager.h"

#include "AutoGen/UICore_h_ast.h"
#include "AutoGen/Message_h_ast.h"
#include "AutoGen/gclLoadingUI_c_ast.h"
#include "autogen/gclSmartAd_h_ast.h"

extern bool gbNoGraphics;


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct LoadingScreen
{
	REF_TO(Message) hText;			AST(REQUIRED NON_NULL_REF NAME(Text))
	const char *pchForegroundTexture; AST(POOL_STRING RESOURCEDICT(Texture))
	U32 uiColor;					AST(SUBTABLE(ColorEnum) DEFAULT(0xFFFFFFFF) NAME(Color))
	REF_TO(UIGen) hGen;				AST(NON_NULL_REF NAME(Gen))

	SmartAdAutoTag eSmartAdTag;		AST(NAME(SmartAd) DEFAULT(kSmartAutoTag_NONE))
} LoadingScreen;

AUTO_STRUCT;
typedef struct LoadingScreenGroup
{
	const char *pchName; AST(KEY POOL_STRING STRUCTPARAM REQUIRED)
	REF_TO(Message) hMapName; AST(NAME(MapName))
	const char *pchBackgroundTexture; AST(POOL_STRING RESOURCEDICT(Texture))
	U32 uiColor; AST(SUBTABLE(ColorEnum) DEFAULT(0xFFFFFFFF) NAME(Color))
	UITextureMode eBackgroundMode; AST(NAME(BackgroundMode))
	LoadingScreen **eaScreen; AST(NAME(Screen) REQUIRED)
	const char *pchFilename; AST(CURRENTFILE)
} LoadingScreenGroup;

static DisplayMessageList s_LoadingTips;

static DictionaryHandle s_hLoadingScreenDict;

static REF_TO(LoadingScreenGroup) s_hLoadingScreen;
static S32 s_iLoadingScreen;

static char s_achMapNameOverride[256];

#define LOADING_ZMAP_NAME ((*s_achMapNameOverride) ? s_achMapNameOverride : zmapInfoGetPublicName(NULL))

bool gclExprLoadingScreenGotWorldUpdate(void);

// Override the loading screen, useful to debug 
AUTO_CMD_STRING(s_achMapNameOverride, LoadingMapNameOverride) ACMD_HIDE;

AUTO_STARTUP(LoadingScreens) ASTRT_DEPS(AS_Messages GameUI);
void gclLoadingScreenLoad(void)
{
	if(!gbNoGraphics)
	{
		s_hLoadingScreenDict = RefSystem_RegisterSelfDefiningDictionary("LoadingScreen", false, parse_LoadingScreenGroup, true, true, NULL);
		resLoadResourcesFromDisk(s_hLoadingScreenDict, "ui/loadingscreens", ".loadingscreen", NULL, PARSER_OPTIONALFLAG);
		ParserLoadFiles("ui/loadingscreens", "TipMessages.def", "TipMessages.bin", 0, parse_DisplayMessageList, &s_LoadingTips);
	}
}

// Append shard-wide event names to the loading screen name, to see if
// we have an event-specific one for this map.
static bool gclLoadingScreenTryShardWideEvents(const char *pchMap)
{
	S32 j;
	if (!pchMap)
		return false;

	if(gGCLState.pLoadingScreens)
	{
		for(j = 0; j < eaSize(&gGCLState.pLoadingScreens->esLoadScreens); ++j)
		{
			if(gGCLState.pLoadingScreens->esLoadScreens[j]->pMap && gGCLState.pLoadingScreens->esLoadScreens[j]->pLoadScreen && stricmp(pchMap, gGCLState.pLoadingScreens->esLoadScreens[j]->pMap) == 0)
			{
				SET_HANDLE_FROM_STRING(s_hLoadingScreenDict, gGCLState.pLoadingScreens->esLoadScreens[j]->pLoadScreen, s_hLoadingScreen);
				if (GET_REF(s_hLoadingScreen))
				{
					return true;
				}
			}
		}
	}

	for (j = eaSize(&gGCLState.shardWideEvents) - 1; j >= 0; j--)
	{
		char achMapEvent[2048];
		sprintf(achMapEvent, "%s_%s", pchMap, gGCLState.shardWideEvents[j]);
		SET_HANDLE_FROM_STRING(s_hLoadingScreenDict, achMapEvent, s_hLoadingScreen);
		if (GET_REF(s_hLoadingScreen))
			return true;
	}
	SET_HANDLE_FROM_STRING(s_hLoadingScreenDict, pchMap, s_hLoadingScreen);
	if (GET_REF(s_hLoadingScreen))
		return true;
	return false;
}

// Set up the loading screen for the given map name.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenSetForMap);
void gclExprLoadingScreenSetForMap(void)
{
	const char *pchMap = NULL;
	DictionaryEArrayStruct *pArrStruct = resDictGetEArrayStruct(s_hLoadingScreenDict);
	LoadingScreenGroup *pGroup = NULL;
	Entity *pEnt = entActivePlayerPtr();
	WorldRegion *pRegion;
	const char *pchRegionName;
	Vec3 v3Pos;
	

	// reset the loading screen handle to catch new changes
	REMOVE_HANDLE(s_hLoadingScreen);

	// check if we've gotten the world update as gGCLState.bGotWorldUpdate && gGCLState.bGotGeneralUpdate
	// and if so, use the client mapstate to get the mapName, as the zonemap current map may not be updated yet 
	if (gclExprLoadingScreenGotWorldUpdate())
	{	
		MapState *pClientMapState = mapStateClient_Get();
		if(pClientMapState && pClientMapState->pcMapName)
		{
			pchMap = pClientMapState->pcMapName;
		}
	}
		
	if (!pchMap)
	{
		pchMap = LOADING_ZMAP_NAME;
	}
	
	
	if (pEnt)
	{
		// I want to remove the word "Debug" from this function, but I can't at this time due to other changes on my machine. [RMARR - 5/4/12]
		mmGetDebugLatestServerPositionFG(pEnt->mm.movement, v3Pos);

		pRegion = worldGetWorldRegionByPos(v3Pos);
		pchRegionName = pRegion ? worldRegionGetRegionName(pRegion) : NULL;
		s_iLoadingScreen = -1;

		REMOVE_HANDLE(s_hLoadingScreen);

		// Try <MapName>_<RegionName>.
		if (pchRegionName)
		{
			char achMapAndRegion[2048];
			sprintf(achMapAndRegion, "%s_%s", pchMap, pchRegionName);
			gclLoadingScreenTryShardWideEvents(achMapAndRegion);
		}
	}

	// Try <MapName>.
	if (!GET_REF(s_hLoadingScreen))
		gclLoadingScreenTryShardWideEvents(pchMap);

	if (pEnt)
	{
		// Try From_<OldRegionType>_To_<NewRegionType>.
		if (!GET_REF(s_hLoadingScreen))
		{
			const char *pchFrom = StaticDefineIntRevLookup(WorldRegionTypeEnum, pEnt->pPlayer->pUI ? pEnt->pPlayer->pUI->eLastRegion : WRT_None);
			const char *pchTo = StaticDefineIntRevLookup(WorldRegionTypeEnum, entGetWorldRegionTypeOfEnt(pEnt));
			if (pchFrom && pchTo)
			{
				char achFromAndToRegion[2048];
				sprintf(achFromAndToRegion, "From_%s_To_%s", pchFrom, pchTo);
				gclLoadingScreenTryShardWideEvents(achFromAndToRegion);
			}
		}
	}

	// Try "Default".
	if (!GET_REF(s_hLoadingScreen))
		gclLoadingScreenTryShardWideEvents("Default");

	s_iLoadingScreen = -1;

	// Check to see if we have any smart ads to show
	if(gclSmartAds_IsSetUp())
	{
		SmartAdAutoTag eSmartAd = gclSmartAds_GetQuickAd();

		if(eSmartAd != kSmartAutoTag_NONE)
		{
			pGroup = GET_REF(s_hLoadingScreen);

			if(pGroup)
			{
				int i;
				int iCount=0;
				
				for(i=0;i<eaSize(&pGroup->eaScreen);i++)
				{
					if(pGroup->eaScreen[i]->eSmartAdTag == eSmartAd)
						iCount++;
				}

				if(iCount>0)
				{
					s_iLoadingScreen = randInt(iCount-1);

					for(i=0;i<=s_iLoadingScreen;i++)
					{
						if(pGroup->eaScreen[i]->eSmartAdTag != eSmartAd)
							s_iLoadingScreen++;
					}
				}				
			}
		}
	}

	if(s_iLoadingScreen == -1)
	{
		pGroup = GET_REF(s_hLoadingScreen);
		if (pGroup && eaSize(&pGroup->eaScreen))
		{
			int iCount=0;
			int i;

			for(i=0;i<eaSize(&pGroup->eaScreen);i++)
			{
				if(pGroup->eaScreen[i]->eSmartAdTag == kSmartAutoTag_NONE)
					iCount++;
			}

			if(iCount>0)
			{
				s_iLoadingScreen = randInt(iCount);

				for(i=0;i<=s_iLoadingScreen;i++)
				{
					if(pGroup->eaScreen[i]->eSmartAdTag != kSmartAutoTag_NONE)
						s_iLoadingScreen++;
				}
			}
		}
		else
		{
			s_iLoadingScreen = -1;
		}
	}
	
}

// Get the background texture for a map loading screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenBackgroundTexture);
const char *gclExprLoadingScreenBackgroundTexture(void)
{
	LoadingScreenGroup *pGroup = GET_REF(s_hLoadingScreen);
	return (pGroup ? pGroup->pchBackgroundTexture : "");
}

// Get the background color for a map loading screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenBackgroundColor);
U32 gclExprLoadingScreenBackgroundColor(void)
{
	LoadingScreenGroup *pGroup = GET_REF(s_hLoadingScreen);
	return (pGroup ? ui_StyleColorPaletteIndex(pGroup->uiColor) : 0xFFFFFFFF);
}

// Get the background mode for a map loading screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenBackgroundMode);
U32 gclExprLoadingScreenBackgroundMode(void)
{
	LoadingScreenGroup *pGroup = GET_REF(s_hLoadingScreen);
	return (pGroup ? pGroup->eBackgroundMode : UITextureModeScaled);
}

// Get the foreground texture for a map loading screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenForegroundTexture);
const char *gclExprLoadingScreenForegroundTexture(void)
{
	LoadingScreenGroup *pGroup = GET_REF(s_hLoadingScreen);
	LoadingScreen *pScreen = pGroup ? eaGet(&pGroup->eaScreen, s_iLoadingScreen) : NULL;
	return (pScreen ? pScreen->pchForegroundTexture : "");
}

// Get the foreground color for a map loading screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenForegroundColor);
U32 gclExprLoadingScreenForegroundColor(void)
{
	LoadingScreenGroup *pGroup = GET_REF(s_hLoadingScreen);
	LoadingScreen *pScreen = pGroup ? eaGet(&pGroup->eaScreen, s_iLoadingScreen) : NULL;
	return (pScreen ? ui_StyleColorPaletteIndex(pScreen->uiColor) : 0xFFFFFFFF);
}

// Get the text for a map loading screen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenText);
const char *gclExprLoadingScreenText(void)
{
	LoadingScreenGroup *pGroup = GET_REF(s_hLoadingScreen);
	LoadingScreen *pScreen = pGroup ? eaGet(&pGroup->eaScreen, s_iLoadingScreen) : NULL;
	const char *pchMessage = (pScreen && GET_REF(pScreen->hText)) ? TranslateMessagePtr(GET_REF(pScreen->hText)) : "";
	return pchMessage ? pchMessage : "";
}

// Get a pro-tip.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenTip);
const char *gclExprLoadingScreenTip(S32 iPeriod)
{
	const char *pchRet = NULL;
	if (eaSize(&s_LoadingTips.eaMessages))
	{
		U32 uiTime = timeSecondsSince2000() / iPeriod; 
		DisplayMessage *pMessage = eaGet(&s_LoadingTips.eaMessages, uiTime % eaSize(&s_LoadingTips.eaMessages));
		pchRet = pMessage ? TranslateDisplayMessage(*pMessage) : NULL;
	}
	return pchRet ? pchRet : "No tips found.";
}

// See if this loading screen has a gen attached.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenGetGen);
SA_RET_OP_VALID UIGen *gclExprLoadingScreenGetGen(void)
{
	LoadingScreenGroup *pGroup = GET_REF(s_hLoadingScreen);
	LoadingScreen *pScreen = pGroup ? eaGet(&pGroup->eaScreen, s_iLoadingScreen) : NULL;
	return pScreen ? GET_REF(pScreen->hGen) : NULL;
}

static bool gclLoadingIsDynMap(const char* pchResourceName)
{
	char ns[RESOURCE_NAME_MAX_SIZE];
	if (!resExtractNameSpace_s(pchResourceName, SAFESTR(ns), NULL, 0))
		return false;
	return strStartsWith(ns, "dyn_");
}

// Gets the display name for a map, by name.  Has proper fallbacks for UGC
static const char* gclLoadingScreenGetDisplayNameForMap(const char* mapName)
{
	ZoneMapInfo *pZmapInfo = NULL;

	// MJF (July/25/2012) - UGC Maps are never in the ZoneMap
	// dictionary, but they may be the active map.
	if( resNamespaceIsUGC( mapName )) {
		const char* loadedMapName = zmapInfoGetPublicName( NULL );
		if( stricmp( loadedMapName, mapName ) == 0 ) {
			pZmapInfo = zmapGetInfo( NULL );
		}
	} else {
		pZmapInfo = zmapInfoGetByPublicName(mapName);
	}
	
	if( pZmapInfo ) {
		Message *pZMapMessage = zmapInfoGetDisplayNameMessagePtr(pZmapInfo);
		if( pZMapMessage ) {
			return TranslateMessagePtr(pZMapMessage);
		}
	}

	return NULL;
}

// Get the name of the map the screen is for.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenGetMapDisplayName);
const char *gclExprLoadingScreenGetMapDisplayName(void)
{
	LoadingScreenGroup *pGroup = GET_REF(s_hLoadingScreen);
	Message *pMessage = (pGroup && GET_REF(pGroup->hMapName)) ? GET_REF(pGroup->hMapName) : NULL;
	const char *pchTrans = pMessage ? TranslateMessagePtr(pMessage) : NULL;
	if(!pchTrans)
	{
		MapState *pClientMapState = mapStateClient_Get();
		if(pClientMapState)
		{
			pchTrans = gclLoadingScreenGetDisplayNameForMap(pClientMapState->pcMapName);
		}
	}
	if (!pchTrans && gclLoadingIsDynMap(LOADING_ZMAP_NAME))
		pchTrans = TranslateMessageKey("LoadingScreen_DynamicMapDefaultName");
	if (!pchTrans && resNamespaceIsUGC(LOADING_ZMAP_NAME))
		pchTrans = TranslateMessageKey("LoadingScreen_UGCMapDefaultName");
	if (!pchTrans)
		pchTrans = LOADING_ZMAP_NAME;
	if (!pchTrans && pGroup)
		pchTrans = pGroup->pchName;
	if (!pchTrans)
		pchTrans = "UNKNOWN MAP";
	return pchTrans;
}

// See if we got the world update yet (i.e. the map name is going to be right).
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenGotWorldUpdate);
bool gclExprLoadingScreenGotWorldUpdate(void)
{
	// Wait for the general update, so we have an Entity ready.
	return gGCLState.bGotWorldUpdate && gGCLState.bGotGeneralUpdate;
}

// See if we go the world update and the account information
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoadingScreenGotAccountData);
bool gclExprLoadingScreenGotAccountData(void)
{
	return gGCLState.bGotGameAccount && gGCLState.bGotGeneralUpdate;
}

#include "gclLoadingUI_c_ast.c"
