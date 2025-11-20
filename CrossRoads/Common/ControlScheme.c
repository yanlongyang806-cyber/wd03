/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "error.h"

#include "ControlScheme.h"
#include "Entity.h"
#include "FolderCache.h"
#include "Player.h"
#include "RegionRules.h"
#include "WorldGrid.h"
#include "AutoGen/ControlScheme_h_ast.h"
#include "AutoGen/ControlScheme_h_ast.c"

#ifdef GAMECLIENT
#include "gclControlScheme.h"
#include "UIGen.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


ControlSchemes g_DefaultControlSchemes;
DefineContext *g_pDefineControlScehemeRegions = NULL;
ControlSchemeRegions g_ControlSchemeRegions = {0};

AUTO_STARTUP(AS_ControlSchemeRegions);
void schemes_LoadRegions(void)
{
	S32 i;

	if (IsAppServerBasedType() && !IsLoginServer()) {
		return;
	}

	loadstart_printf("Loading Control Scheme Regions...");

	g_pDefineControlScehemeRegions = DefineCreate();

	StructInit(parse_ControlSchemeRegions, &g_ControlSchemeRegions);

	ParserLoadFiles(NULL, "defs/config/ControlSchemeRegions.def", "ControlSchemeRegions.bin", PARSER_OPTIONALFLAG,
		parse_ControlSchemeRegions, &g_ControlSchemeRegions);

	for (i = 0; i < eaSize(&g_ControlSchemeRegions.eaSchemeRegions); i++)
	{
		ControlSchemeRegionInfo* pInfo = g_ControlSchemeRegions.eaSchemeRegions[i];
		pInfo->eType = (1<<i);
		DefineAddInt(g_pDefineControlScehemeRegions, pInfo->pchName, pInfo->eType);
	}

#ifdef GAMECLIENT
	//This relies on control scheme regions
	schemes_LoadCameraTypeRules();
	ui_GenInitStaticDefineVars(ControlSchemeRegionTypeEnum, "SchemeRegionType_");
#endif

	loadend_printf("done (loaded %d).", i);
}

static void schemes_ValidateSchemeRegions(void)
{
	S32 i;
	for ( i = eaSize(&g_ControlSchemeRegions.eaSchemeRegions)-1; i >= 0; i-- )
	{
		ControlSchemeRegionInfo* pInfo = g_ControlSchemeRegions.eaSchemeRegions[i];
		if ( IS_HANDLE_ACTIVE(pInfo->DisplayMsg.hMessage) )
		{
			if ( GET_REF(pInfo->DisplayMsg.hMessage)==NULL )
			{
				ErrorFilenamef(pInfo->pchFilename, "ControlSchemeRegion: DisplayMessage not found %s", REF_STRING_FROM_HANDLE(pInfo->DisplayMsg.hMessage));
			}
		}
	}
}

/***************************************************************************
 * schemes_Load
 *
 */
static void schemes_LoadInternal(const char *pchPath, S32 iWhen)
{
	ParserLoadFiles(NULL, 
					"defs/config/ControlSchemes.def", 
					"ControlSchemes.bin", 
					PARSER_OPTIONALFLAG,
					parse_ControlSchemes, 
					&g_DefaultControlSchemes);
}

AUTO_STARTUP(AS_ControlSchemes) ASTRT_DEPS(AS_Messages, AS_ControlSchemeRegions);
void schemes_Load(void)
{
	loadstart_printf("Loading %s...","Control Schemes");

	schemes_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ControlSchemes.def", schemes_LoadInternal);

	if (!IsClient() && isDevelopmentMode())
	{
		schemes_ValidateSchemeRegions();
	}
	loadend_printf(" done.");
}

/***************************************************************************
 * schemes_FindScheme
 *
 * Finds a named scheme in a scheme set.
 *
 */
ControlScheme *schemes_FindScheme(ControlSchemes *pSchemes, const char *pchName)
{
	ControlScheme *pRet = NULL;

	if(pchName && pSchemes)
	{
		int iCnt = eaSize(&pSchemes->eaSchemes);
		int i;

		for(i=0; i<iCnt; i++)
		{
			if(stricmp(pchName, pSchemes->eaSchemes[i]->pchName)==0)
			{
				pRet = pSchemes->eaSchemes[i];
				break;
			}
		}
	}

	return pRet;
}

ControlScheme *schemes_FindCurrentScheme(ControlSchemes *pSchemes)
{
	ControlScheme *pRet = NULL;

	if(pSchemes && pSchemes->pchCurrent)
	{
		int iCnt = eaSize(&pSchemes->eaSchemes);
		int i;

		for(i=0; i<iCnt; i++)
		{
			if(pSchemes->pchCurrent == pSchemes->eaSchemes[i]->pchName)
			{
				pRet = pSchemes->eaSchemes[i];
				break;
			}
		}
	}

	return pRet;
}

ControlSchemeRegionInfo* schemes_GetSchemeRegionInfo(ControlSchemeRegionType eSchemeRegion)
{
	ControlSchemeRegionInfo* pSchemeRegionInfo = NULL;

	if ( eSchemeRegion != kControlSchemeRegionType_None )
	{
		const char* pchSchemeRegionName = StaticDefineIntRevLookup(ControlSchemeRegionTypeEnum, eSchemeRegion);

		if ( pchSchemeRegionName && pchSchemeRegionName[0] )
		{
			S32 i;
			for ( i = eaSize(&g_ControlSchemeRegions.eaSchemeRegions)-1; i >= 0; i-- )
			{
				if ( stricmp(g_ControlSchemeRegions.eaSchemeRegions[i]->pchName, pchSchemeRegionName)==0 )
				{
					return g_ControlSchemeRegions.eaSchemeRegions[i];
				}
			}
		}
	}
	return NULL;
}

S32 schemes_GetAllSchemeRegions(void)
{
	S32 i;
	S32 eAllRegions = 0;
	for (i = eaSize(&g_ControlSchemeRegions.eaSchemeRegions)-1; i >= 0; i--)
	{
		ControlSchemeRegionInfo* pInfo = g_ControlSchemeRegions.eaSchemeRegions[i];
		if (pInfo->ppchAllowedSchemes)
		{
			eAllRegions |= pInfo->eType;
		}
	}
	return eAllRegions;
}

ControlSchemeRegionType getSchemeRegionTypeFromRegionType(S32 eRegionType)
{
	RegionRules* pRules = getRegionRulesFromRegionType(eRegionType);
	if (pRules)
	{
		return pRules->eSchemeRegionType;
	}
	return kControlSchemeRegionType_None;
}

bool schemes_IsSchemeSelectable(Entity* pEnt, ControlScheme* pCurrScheme)
{
	if (!pCurrScheme)
	{
		return false;
	}
	if (pCurrScheme->bDebug)
	{
		if (!isDevelopmentMode() && entGetAccessLevel(pEnt) < ACCESS_DEBUG)
		{
			return false;
		}
	}
	return true;
}

#define WRAP_SCHEME_INDEX(i, c) i >= c ? i-c : i

ControlScheme* schemes_FindNextSelectableScheme(Entity* pEnt, ControlSchemes* pSchemes, const char* pchCurrent)
{
	S32 i, iFound = -1;
	S32 iSchemeCount = eaSize(&pSchemes->eaSchemes);
	ControlScheme* pCurrScheme = schemes_FindScheme(pSchemes, pchCurrent);
	if (pCurrScheme)
	{
		for (i = 0; i < iSchemeCount; i++)
		{
			ControlScheme* pScheme = pSchemes->eaSchemes[i];
			if (pScheme == pCurrScheme)
			{
				iFound = i;
				break;
			}
		}
	}
	if (iFound < 0)
	{
		return NULL;
	}
	for (i = iFound+1; i != iFound; i++)
	{
		ControlScheme* pScheme = g_DefaultControlSchemes.eaSchemes[WRAP_SCHEME_INDEX(i,iSchemeCount)];
		if (schemes_IsSchemeSelectable(pEnt, pScheme))
		{
			if (Entity_IsValidControlSchemeForCurrentRegion(pEnt, pScheme->pchName))
			{
				return pScheme;
			}
		}
	}
	return NULL;
}

bool schemes_DisableTrayAutoAttack(Entity* pEnt)
{
	ControlSchemes* pSchemes = SAFE_MEMBER3(pEnt, pPlayer, pUI, pSchemes);
	if (pSchemes)
	{
		ControlScheme* pCurrScheme = schemes_FindCurrentScheme(pSchemes);
		if (pCurrScheme)
		{
			return pCurrScheme->bDisableTrayAutoAttack;
		}
	}
	return false;
}

/* End of File */

