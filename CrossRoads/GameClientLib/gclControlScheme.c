/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/

#include "gclControlScheme.h"

#include "gclEntity.h"
#include "Character_target.h"
#include "cmdparse.h"
#include "ClientTargeting.h"
#include "file.h"
#include "gclCamera.h"
#include "gfxCamera.h"
#include "gclOptions.h"
#include "gclPlayerControl.h"
#include "inputLib.h"
#include "ControlScheme.h"
#include "StringCache.h"
#include "gclKeyBind.h"
#include "Player.h"
#include "RegionRules.h"
#include "GlobalTypes.h"
#include "UIGen.h"
#include "WorldLibEnums.h"
#include "GlobalTypes.h"
#include "gclPlayerControl.h"

// required for changing the keybinds to something else
#include "inputKeyBind.h"
#include "AutoGen/ControlScheme_h_ast.h"
#include "AutoGen/gclControlScheme_h_ast.h"
#include "AutoGen/gclControlScheme_h_ast.c"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define CONTROLSCHEME_CATEGORY	"ControlScheme"

int s_iLastVersion = -1;

extern F32 g_CameraMaxDist_Min;
extern F32 g_CameraMaxDist_Max;
extern F32 g_CameraControllerSensitivity_Min;
extern F32 g_CameraControllerSensitivity_Max;

bool g_bInvertMousePerScheme = false;
CameraTypeRules g_CameraTypeRules = {0};
ControlScheme g_CurrentScheme = { 0 };
ControlScheme** g_eaStoredSchemes = NULL; //temporary storage for filling schemes in the options menu
static ControlScheme s_ActiveStoredScheme = { 0 };
static int s_iCurrentControlScheme = -1;
static int s_iCurrentControlSchemeDisplay = 0;
static int s_iCurrentKeyBindProfileDisplay = 0;
static int s_iCurrentRegion = kControlSchemeRegionType_None;
static int s_iCurrentRegionDisplay = 0;
static int s_iCurrentCameraTypeDisplay = 0;
static int s_iCurrentAutoAttackTypeDisplay = 0;
static U32 s_uLastSchemeChangeTime = 0;

ControlSchemeRegionData** g_eaUIRegions = NULL;

void schemes_LoadCameraTypeRules(void)
{
	StructDeInit(parse_CameraTypeRules, &g_CameraTypeRules);

	ParserLoadFiles(NULL, "defs/config/CameraTypeRules.def", "CameraTypeRules.bin", PARSER_OPTIONALFLAG,
		parse_CameraTypeRules, &g_CameraTypeRules);
}

/***************************************************************************
 * schemes_GetActiveStoredScheme
 *
 * Gets the active stored scheme if there is one, otherwise returns the 
 *	player's current scheme.
 *
  * The stored scheme is used to temporarily store fields on the control scheme that 
 *	are changd through the options menu but have not been committed. It is also used 
 *	for fields that will change automatically during gameplay. The latter example is 
 *	needed when a map transfer takes place, as there is no safe point on the client 
 *	to send a server command once the map transfer commences. Instead, this data is 
 *	stored until the client enters the gameplay state on the other server, at which
 *	point it is sent to the server.
 *
 */
const ControlScheme* schemes_GetActiveStoredScheme(void)
{
	if (s_ActiveStoredScheme.pchName)
		return &s_ActiveStoredScheme;

	return &g_CurrentScheme;
}

/***************************************************************************
 * gclControlSchemeFindSchemeRegionForEnt
 *
 * Gets the current region for the entity that also matches a scheme region.
 *
 */
static ControlSchemeRegionType gclControlSchemeFindSchemeRegionForEnt(Entity* pEnt)
{
	S32 i;
	ControlSchemes* pSchemes = SAFE_MEMBER3(pEnt, pPlayer, pUI, pSchemes);

	if (!pSchemes)
		return kControlSchemeRegionType_None;

	for (i = 0; i < eaSize(&pSchemes->eaSchemeRegions); i++)
	{
		if (stricmp(pSchemes->pchCurrent, pSchemes->eaSchemeRegions[i]->pchScheme)==0)
		{
			return pSchemes->eaSchemeRegions[i]->eType;
		}
	}
	return kControlSchemeRegionType_None;
}

/***************************************************************************
 * gclControlSchemeGetCurrentRegionFlag
 *
 * Gets the current stored region flag if there is an active stored scheme.
 *	Otherwise get the region flag of the player's current scheme.
 *
 */
S32 gclControlSchemeGetCurrentSchemeRegionType(void)
{
	return s_ActiveStoredScheme.pchName ? s_iCurrentRegion : gclControlSchemeFindSchemeRegionForEnt(entActivePlayerPtr());
}

/***************************************************************************
 * schemes_StoreAutoAdjustingData
 *
 * This saves control scheme data that adjusts automatically during gameplay, 
 *	and should be called whenever you leave gameplay or change control schemes.
 *	Returns whether or not any auto-adjusting fields differ from corresponding
 *	fields that reside on the current scheme.
 */
static void schemes_UpdateAutoAdjustingData(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID ControlScheme* pScheme)
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		F32 fDistance = gclCamera_GetDefaultModeDistance(pCamera);

		if (!nearSameF32(fDistance, pScheme->fCamDistance))
		{
			pScheme->fCamDistance = fDistance;
		}
	}
}

static RegionRules* ControlSchemeGetCurrentRegionRules(void)
{
	Entity* pEnt = entActivePlayerPtr();
	ControlSchemeRegionType eSchemeRegion = gclControlSchemeGetCurrentSchemeRegionType();
	if (pEnt && eSchemeRegion != kControlSchemeRegionType_None)
	{
		RegionRules* pRules = getRegionRulesFromEnt(pEnt);
		
		if (pRules && pRules->eSchemeRegionType == eSchemeRegion)
		{
			return pRules;
		}
		return getRegionRulesFromSchemeRegionType(eSchemeRegion);
	}
	return NULL;
}

// Ensures that camera settings are valid
static void scheme_FixupCameraSettings(SA_PARAM_NN_VALID ControlScheme* pScheme)
{
	CameraSettings* pCameraSettings = gclCamera_GetSettings(NULL);
	OptionSetting *pCameraMaxDistanceSetting;
	RegionRules* pRegionRules = ControlSchemeGetCurrentRegionRules();
	F32 fMaxDistanceMinValue = pCameraSettings->fMaxDistanceMinValue;
	F32 fMaxDistanceMaxValue = pCameraSettings->fMaxDistanceMaxValue;

	// Max camera distance
	if (pRegionRules && pRegionRules->pCamDistPresets)
	{
		fMaxDistanceMinValue = pRegionRules->pCamDistPresets->fMaxZoomMin;
		fMaxDistanceMaxValue = pRegionRules->pCamDistPresets->fMaxZoomMax;
	}
	if (pCameraMaxDistanceSetting = OptionSettingGet(CONTROLSCHEME_CATEGORY, "CameraMaxDistance"))
	{
		pCameraMaxDistanceSetting->fMin = fMaxDistanceMinValue;
		pCameraMaxDistanceSetting->fMax = fMaxDistanceMaxValue;
	}
	pScheme->fCamMaxDistance = CLAMPF32(pScheme->fCamMaxDistance, fMaxDistanceMinValue, fMaxDistanceMaxValue);
	
	// Mouse look sensitivity
	if (nearSameF32(pScheme->fCamMouseLookSensitivity,0.0f))
	{
		pScheme->fCamMouseLookSensitivity = pCameraSettings->fMouseSensitivityDefault;
	}
	else
	{
		pScheme->fCamMouseLookSensitivity = CLAMPF32(pScheme->fCamMouseLookSensitivity,
													 pCameraSettings->fMouseSensitivityMin,
													 pCameraSettings->fMouseSensitivityMax);
	}

	// Controller sensitivity
	if (nearSameF32(pScheme->fCamControllerLookSensitivity,0.0f))
	{
		pScheme->fCamControllerLookSensitivity = pCameraSettings->fControllerSensitivityDefault;
	}
	else
	{
		pScheme->fCamControllerLookSensitivity = CLAMPF32(pScheme->fCamControllerLookSensitivity, 
														  pCameraSettings->fControllerSensitivityMin, 
														  pCameraSettings->fControllerSensitivityMax);
	}
}

static void schemes_SetActiveStoredScheme(SA_PARAM_NN_VALID ControlScheme* pScheme)
{
	ControlScheme* pActiveScheme = &s_ActiveStoredScheme;
	
	// Copy the passed scheme
	StructCopyAll(parse_ControlScheme, pScheme, pActiveScheme);
	
	// Fix up camera settings
	scheme_FixupCameraSettings(pActiveScheme);
}

/***************************************************************************
 * schemes_UpdateStoredSchemes
 *
 * Updates stored schemes to the latest player data, or defaults if player data is absent.
 *
 */
void schemes_UpdateStoredSchemes(bool bUpdateOld, bool bUpdateNew)
{
	Entity* pEnt = entActivePlayerPtr();
	S32 i, j;

	if (!pEnt || !pEnt->pPlayer)
		return;

	for (i = 0; i < eaSize(&g_DefaultControlSchemes.eaSchemes); i++)
	{
		ControlScheme* pCurrScheme = g_DefaultControlSchemes.eaSchemes[i];

		if (!schemes_IsSchemeSelectable(pEnt, pCurrScheme))
			continue;

		for (j = 0; j < eaSize(&pEnt->pPlayer->pUI->pSchemes->eaSchemes); j++)
		{
			if (pEnt->pPlayer->pUI->pSchemes->eaSchemes[j]->pchName == pCurrScheme->pchName)
			{
				pCurrScheme = pEnt->pPlayer->pUI->pSchemes->eaSchemes[j];
				break;
			}
		}

		for (j = 0; j < eaSize(&g_eaStoredSchemes); j++)
		{
			if (g_eaStoredSchemes[j]->pchName == pCurrScheme->pchName)
			{
				eaSwap(&g_eaStoredSchemes,i,j);
				break;
			}
		}

		if (j == eaSize(&g_eaStoredSchemes))
		{
			if (bUpdateNew)
			{
				ControlScheme* pScheme = StructCreate(parse_ControlScheme);
				
				StructCopyAll(parse_ControlScheme, pCurrScheme, pScheme);
				if (pEnt->pPlayer->pUI->pSchemes->pchCurrent == pCurrScheme->pchName)
					schemes_UpdateAutoAdjustingData(pEnt,pScheme);
				eaInsert(&g_eaStoredSchemes, pScheme, i);

				if (schemes_GetActiveStoredScheme()->pchName == pCurrScheme->pchName)
				{
					schemes_SetActiveStoredScheme(pScheme);
				}
			}
		}
		else if (bUpdateOld)
		{
			ControlScheme* pScheme = eaGet(&g_eaStoredSchemes,i);

			if (pScheme)
			{
				StructCopyAll(parse_ControlScheme, pCurrScheme, pScheme);

				if (pEnt->pPlayer->pUI->pSchemes->pchCurrent == pCurrScheme->pchName)
					schemes_UpdateAutoAdjustingData(pEnt,pScheme);

				if (schemes_GetActiveStoredScheme()->pchName == pCurrScheme->pchName)
				{
					schemes_SetActiveStoredScheme(pScheme);
				}
			}
		}
	}
}

/***************************************************************************
 * schemes_UpdateCurrentStoredScheme
 *
 * Updates the stored scheme corresponding to the current player's scheme.
 *	Creates a stored scheme if one doesn't exist.
 *
 */
ControlScheme* schemes_UpdateCurrentStoredScheme(void)
{
	Entity* pEnt = entActivePlayerPtr();
	ControlScheme* pScheme = NULL;
	S32 i;

	if (!pEnt || !pEnt->pPlayer)
		return false;

	for (i = 0; i < eaSize(&g_eaStoredSchemes); i++)
	{
		if (g_eaStoredSchemes[i]->pchName == pEnt->pPlayer->pUI->pSchemes->pchCurrent)
		{
			pScheme = g_eaStoredSchemes[i];
			break;
		}
	}

	if (!pScheme)
	{
		pScheme = StructCreate(parse_ControlScheme);
		eaPush(&g_eaStoredSchemes, pScheme);
	}
	
	if (pScheme)
	{
		StructCopyAll(parse_ControlScheme, &g_CurrentScheme, pScheme);

		schemes_UpdateAutoAdjustingData(pEnt, pScheme);
	}

	return pScheme;
}

/***************************************************************************
 * schemes_DestroyAllStoredSchemes
 *
 * Wipe all stored control scheme data.
 * 
 */

static void schemes_DestroyAllStoredSchemes(void)
{
	eaDestroyStruct(&g_eaStoredSchemes, parse_ControlScheme);
	StructReset(parse_ControlScheme, &s_ActiveStoredScheme);
	s_iCurrentControlScheme = -1;
	s_iCurrentRegion = kControlSchemeRegionType_None;
}

/***************************************************************************
 * schemes_SaveStoredSchemeEx
 *
 * Send the stored schemes to the server to save over the existing schemes on the player.
 *	If pSetCurrent is non-null, it will save this control scheme for the player and then
 *	immediately set it to the player's currently selected control scheme, if it is valid.
 * 
 */
static void schemes_SaveStoredSchemesEx(ControlScheme* pSetCurrent)
{
	S32 i;
	for (i = 0; i < eaSize(&g_eaStoredSchemes); i++)
	{
		ControlScheme* pScheme = g_eaStoredSchemes[i];

		if (pScheme->pchName)
		{
			bool bSetCurrent = false;
			
			if (pSetCurrent && pSetCurrent->pchName == pScheme->pchName)
			{
				bSetCurrent = Entity_IsValidControlSchemeForCurrentRegion(entActivePlayerPtr(),pScheme->pchName);
			}

			if (bSetCurrent)
			{
				ServerCmd_schemes_SaveAndChangeScheme(pScheme, pScheme->pchName);
			}
			else
			{
				ServerCmd_schemes_SetDetails(pScheme);
			}
		}
	}

	schemes_DestroyAllStoredSchemes();
}

/***************************************************************************
 * schemes_SaveStoredScheme
 *
 * See schemes_SaveStoredSchemeEx.
 * 
 */
void schemes_SaveStoredSchemes(void)
{
	schemes_SaveStoredSchemesEx(NULL);
}

/***************************************************************************
 * PeriodicStoredSchemesUpdate
 *
 * Updates stored schemes to the server if stored schemes have changed
 * 
 */
void PeriodicStoredSchemesUpdate(void)
{
	static char *diffString;
	static ControlScheme oldScheme = { 0 };

	ControlScheme* pScheme = schemes_UpdateCurrentStoredScheme();

	// If this is the first time we're running this, then just load
	// the current scheme into the old scheme
	if (!oldScheme.pchName)
	{
		StructCopyAll(parse_ControlScheme, pScheme, &oldScheme);
		return;
	}

	estrClear(&diffString);
	StructWriteTextDiff(&diffString, parse_ControlScheme, &oldScheme, pScheme, NULL, TOK_USEROPTIONBIT_1, 0, 0);

	// Send the new scheme to the server if it has changed since
	// the last update
	if (estrLength(&diffString))
	{
		ServerCmd_schemes_SetDetails(pScheme);
		StructCopyAll(parse_ControlScheme, pScheme, &oldScheme);
	}
}

/***************************************************************************
 * ControlSchemeFillKeyBindsFromScheme
 *
 * Fills keybinds from the keybind profile on the passed in scheme.
 * 
 */
static void ControlSchemeFillKeyBindsFromProfile(const char* pchProfile)
{
	static char *s_estr = NULL;
	Entity *pEnt = entActivePlayerPtr();
	KeyBindProfile *pOldProfile = NULL;
	KeyBind **eaBinds = NULL;

	if (pchProfile && s_estr && !stricmp(pchProfile, s_estr))
		return; // trying to set the same keybind profile

	keybind_GetBinds(&eaBinds, true);

	if (!s_estr)
	{
		estrCreate(&s_estr);
	}
	if (s_estr && s_estr[0])
	{
		pOldProfile = keybind_FindProfile(s_estr);
		keybind_PopProfile(pOldProfile);
		estrClear(&s_estr);
	}
	
	if (pchProfile && pchProfile[0])
	{	// if we have valid profile for this scheme, check to see that it already isn't active
		KeyBindProfile *pProfile = keybind_FindProfile(pchProfile);
		if (pProfile && !keybind_IsProfileActive(pProfile))
		{
			estrCopy2(&s_estr, pchProfile);
			keybind_PushProfile(pProfile);
		}
	}
	if (pEnt && gclKeyBindIsEnabled())
	{
		gclKeyBindFillFromEntity(pEnt);
		gclKeyBindProfileFixKeyBindStates(eaBinds);
	}
	eaDestroyStruct(&eaBinds, parse_KeyBind);
}

/***************************************************************************
 * schemes_UpdateForCurrentControlScheme
 *
 * Sets any global state so that the client actually operates according to
 *   the current control scheme. This should be called any time the scheme
 *   changes from a server update.
 */
void schemes_UpdateForCurrentControlSchemeEx(bool bUpdateCameraMode, bool bNewScheme)
{
	Entity* pEnt = entActivePlayerPtr();

	if (gConf.bKeybindsFromControlSchemes)
	{
		ControlSchemeFillKeyBindsFromProfile(g_CurrentScheme.pchKeyProfileToLoad);
	}

	// The other parameters are directly accessed from g_CurrentScheme as needed.
	gclCamera_SetDistance(g_CurrentScheme.fCamDistance);
	gclCamera_AdjustCameraHeight(g_CurrentScheme.iCamHeight);
	gclCamera_SetOffset(g_CurrentScheme.fCamOffset);
	gclCamera_SetLockPitch(g_CurrentScheme.iLockCamPitch);
	gclCamera_SetStartingPitch(g_CurrentScheme.fCamStartingPitch, g_CurrentScheme.iLockCamPitch);
	gclCamera_UpdateNearOffset(pEnt);

	gclCamera_AdjustDistancePresetsFromMax(g_CurrentScheme.fCamMaxDistance);
	gclCamera_SetMouseLookSensitivity(g_CurrentScheme.fCamMouseLookSensitivity);
	gclCamera_SetControllerLookSensitivity(g_CurrentScheme.fCamControllerLookSensitivity);
	if (bNewScheme)
	{
		gclAutoAttack_Disable();
	}
	if (g_bInvertMousePerScheme)
	{
		input_state.invertX = g_CurrentScheme.bInvertMouseX;
		input_state.invertY = g_CurrentScheme.bInvertMouseY;
	}
	if (bUpdateCameraMode)
	{
		gclCamera_UpdateModeForRegion(pEnt);
		if (g_CurrentScheme.bAutoCamMouseLook)
			globCmdParse("cammouselooktoggle 1");
	}
	if (bNewScheme && g_CurrentScheme.bAutoUnholster && gclPlayerControl_ShouldAutoUnholster(true))
	{
		GameSpecific_HolsterRequest(pEnt, NULL, true);
	}
	s_uLastSchemeChangeTime = 0;
}


/***************************************************************************
* schemes_ClearLocal
*
* This resets the control scheme to defaults, and should be called whenever the
*	player leaves gameplay.
*
*/
void schemes_ClearLocal(void)
{
	StructReset(parse_ControlScheme, &g_CurrentScheme);
	schemes_UpdateForCurrentControlScheme(true);
	s_iLastVersion = 0;
}

/***************************************************************************
 * schemes_HandleUpdate
 *
 * This must be called any time the entity is updated from the server.
 * The current scheme is updated to match any changes.
 *
 */
void schemes_HandleUpdate(void)
{
	Player *pPlayer = playerActivePlayerPtr();

	if(pPlayer && pPlayer->pUI && pPlayer->pUI->pSchemes)
	{
		ControlSchemes *pSchemes = pPlayer->pUI->pSchemes;

		if(pSchemes->iVersion != s_iLastVersion)
		{
			ControlScheme *pNewScheme = schemes_FindScheme(pSchemes, pSchemes->pchCurrent);
			if(!pNewScheme)
			{
				pNewScheme = schemes_FindScheme(&g_DefaultControlSchemes, pSchemes->pchCurrent);
			}

			if(pNewScheme)
			{
				bool bIsNew = stricmp(pNewScheme->pchName, g_CurrentScheme.pchName)!=0;

				StructCopyAll(parse_ControlScheme, pNewScheme, &g_CurrentScheme);

				schemes_UpdateForCurrentControlSchemeEx(true, bIsNew);
			}

			if (s_iCurrentControlScheme >= 0)
			{
				schemes_UpdateStoredSchemes(true, false);
			}

			s_iLastVersion = pSchemes->iVersion;
			
			if (eaSize(&g_eaStoredSchemes))
			{
				OptionsReload();
			}
		}
	}
}

/***************************************************************************
 * ControlSchemeCopyActiveScheme
 *
 * bCopyOld
 *	Copies from the active stored scheme to the stored scheme it respresents.
 * bCopyNew
 *	Copies from the latest UI scheme selection to the active stored scheme.
 *
 */
static void ControlSchemeCopyActiveScheme(bool bCopyOld, bool bCopyNew)
{
	if (bCopyOld)
	{
		S32 i;
		for (i = 0; i < eaSize(&g_eaStoredSchemes); i++)
		{
			if (g_eaStoredSchemes[i]->pchName == s_ActiveStoredScheme.pchName)
			{
				StructCopyAll(parse_ControlScheme, &s_ActiveStoredScheme, g_eaStoredSchemes[i]);
				break;
			}
		}
	}
	
	if (bCopyNew)
	{
		ControlScheme *pScheme = eaGet(&g_eaStoredSchemes, s_iCurrentControlScheme);

		if (pScheme)
		{
			schemes_SetActiveStoredScheme(pScheme);
		}
	}
}

//***** CONTROL SCHEME UI CALLBACKS *****

static const char* ControlSchemeGetValidSchemesForCurrentRegion(const char*** pppchSchemes)
{
	S32 i, j, iStart = -1;
	ControlSchemeRegionInfo* pSchemeRegionInfo = schemes_GetSchemeRegionInfo(s_iCurrentRegion);
	ControlScheme* pCurrScheme = eaGet(&g_DefaultControlSchemes.eaSchemes, s_iCurrentControlScheme);
	Entity* pEnt = entActivePlayerPtr();
	const char* pchCurrName = NULL;

	if (!schemes_IsSchemeSelectable(pEnt, pCurrScheme))
	{
		const char* pchCurrScheme = SAFE_MEMBER4(pEnt, pPlayer, pUI, pSchemes, pchCurrent);
		pCurrScheme = schemes_FindScheme(&g_DefaultControlSchemes, pchCurrScheme);
		if (!pCurrScheme)
		{
			for (i = 0; i < eaSize(&g_DefaultControlSchemes.eaSchemes); i++)
			{
				if (schemes_IsSchemeSelectable(pEnt, g_DefaultControlSchemes.eaSchemes[i]))
				{
					pCurrScheme = g_DefaultControlSchemes.eaSchemes[i];
					break;
				}
			}
		}
		devassert(pCurrScheme);
	}
	if (!pSchemeRegionInfo)
	{
		s_iCurrentControlSchemeDisplay = eaFind(&g_DefaultControlSchemes.eaSchemes, pCurrScheme);
		s_iCurrentControlScheme = s_iCurrentControlSchemeDisplay;
		return pCurrScheme->pchName;
	}

	s_iCurrentControlSchemeDisplay = 0;

	for (i = 0; i < eaSize(&g_DefaultControlSchemes.eaSchemes); i++)
	{
		ControlScheme* pScheme = g_DefaultControlSchemes.eaSchemes[i];
		
		if (!schemes_IsSchemeSelectable(pEnt, pScheme))
		{
			continue;
		}
		for (j = 0; j < eaSize(&pSchemeRegionInfo->ppchAllowedSchemes); j++)
		{
			if (pScheme->pchName == pSchemeRegionInfo->ppchAllowedSchemes[j])
			{
				if (pScheme->pchName == pCurrScheme->pchName)
				{
					s_iCurrentControlSchemeDisplay = eaSize(pppchSchemes);
					s_iCurrentControlScheme = i;
					pchCurrName = pScheme->pchName;
				}
				if (iStart < 0)
					iStart = i;
				eaPush(pppchSchemes,TranslateMessageRef(pScheme->hNameMsg));
			}
		}
	}

	if (!pchCurrName && iStart >= 0)
	{
		pchCurrName = g_DefaultControlSchemes.eaSchemes[iStart]->pchName;
		s_iCurrentControlScheme = iStart;
	}

	return pchCurrName;
}

static void ControlSchemeUpdateCallback(OptionSetting *setting)
{
	char** ppchSchemes = NULL;
	const char* pchLastControlScheme = s_ActiveStoredScheme.pchName;
	const char* pchCurrControlScheme = ControlSchemeGetValidSchemesForCurrentRegion(&ppchSchemes);

	schemes_UpdateStoredSchemes(false, true);

	if (pchCurrControlScheme && (!pchLastControlScheme || stricmp(pchLastControlScheme,pchCurrControlScheme)!=0))
	{
		ControlSchemeCopyActiveScheme(true,true);
	}

	if (eaSize(&ppchSchemes))
	{
		OptionSettingUpdateComboBoxOptions(setting,CONTROLSCHEME_CATEGORY,&ppchSchemes,false);
	}
	
	eaDestroy(&ppchSchemes);
}

static void ControlSchemeSetSchemeFromDisplayName(const char* pchDisplayName)
{
	S32 i;
	for (i = 0; i < eaSize(&g_DefaultControlSchemes.eaSchemes); i++)
	{
		ControlScheme* pScheme = g_DefaultControlSchemes.eaSchemes[i];
		if (stricmp(pchDisplayName,TranslateMessageRef(pScheme->hNameMsg))==0)
		{
			s_iCurrentControlScheme = i;
			return;
		}
	}
	s_iCurrentControlScheme = 0;
}

static void ControlSchemeChangedCallback(OptionSetting *setting)
{
	ControlSchemeSetSchemeFromDisplayName(setting->pchStringValue);
	OptionsReload();
}

static void ControlSchemeCommitCallback(OptionSetting *setting)
{
	ControlScheme *pScheme = eaGet(&g_DefaultControlSchemes.eaSchemes, s_iCurrentControlScheme);
	
	ControlSchemeCopyActiveScheme(true,false);

	if (pScheme)
	{
		schemes_SaveStoredSchemesEx(pScheme);
	}
	else
	{
		schemes_SaveStoredSchemes();
	}
}

static const char* ControlSchemeGetValidKeyBindProfiles(KeyBindStoredProfiles *pProfiles, const char*** pppchProfiles)
{
	int i, iStart = -1;
	int iCurrProfile = keybind_FindInProfiles(pProfiles, s_ActiveStoredScheme.pchKeyProfileToLoad);
	KeyBindProfile *pCurrProfile = eaGet(&pProfiles->eaProfiles, iCurrProfile);
	const char* pchCurrName = NULL;
	S32 eCurRegion = gclControlSchemeGetCurrentSchemeRegionType();
	const char* pchCurRegion = allocFindString(StaticDefineIntRevLookup(ControlSchemeRegionTypeEnum, eCurRegion));

	if (!pCurrProfile || !pCurrProfile->bUserSelectable)
		return NULL;
	if (eCurRegion == kControlSchemeRegionType_None)
	{
		s_iCurrentKeyBindProfileDisplay = iCurrProfile;
		return pCurrProfile->pchName;
	}

	s_iCurrentKeyBindProfileDisplay = 0;

	for (i = 0; i < eaSize(&pProfiles->eaProfiles); i++)
	{
		KeyBindProfile *pProfile = pProfiles->eaProfiles[i];

		if (eaSize(&s_ActiveStoredScheme.ppchAllowedKeyProfiles) &&
			eaFindString(&s_ActiveStoredScheme.ppchAllowedKeyProfiles, pProfile->pchName) < 0)
		{
			continue;
		}
		if (!pProfile->ppchSchemeRegions || eaFind(&pProfile->ppchSchemeRegions, pchCurRegion) >= 0)
		{
			if (stricmp(pProfile->pchName,pCurrProfile->pchName)==0)
			{
				s_iCurrentKeyBindProfileDisplay = eaSize(pppchProfiles);
				pchCurrName = pProfile->pchName;
			}
			if (iStart < 0)
				iStart = i;
			eaPush(pppchProfiles,TranslateMessageRef(pProfile->hNameMsg));
		}
	}

	if (!pchCurrName && iStart >= 0)
	{
		pchCurrName = pProfiles->eaProfiles[iStart]->pchName;
	}

	return pchCurrName;
}

// Set the base key bind profile for the current player.
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0);
void ControlSchemeSetKeyBindProfile(const char *pchProfile)
{
	ControlScheme *pScheme = eaGet(&g_DefaultControlSchemes.eaSchemes, s_iCurrentControlScheme);
	KeyBindStoredProfiles *pProfiles = keybind_GetUserSelectableProfiles();
	S32 i;
	for (i = 0; i < eaSize(&pProfiles->eaProfiles); i++)
	{
		KeyBindProfile *pProfile = pProfiles->eaProfiles[i];
		if (!stricmp(pProfile->pchName, pchProfile))
		{
			g_CurrentScheme.pchKeyProfileToLoad = allocAddString(pProfile->pchName);
			ServerCmd_schemes_SetDetails(&g_CurrentScheme);
			break;
		}
	}
}

bool gclControlSchemeIsChangingCurrent(void)
{
	return (s_uLastSchemeChangeTime + 5 > timeSecondsSince2000());
}

static void gclControlSchemeSetCurrent(const char* pchSchemeName)
{
	if (!gclControlSchemeIsChangingCurrent())
	{
		Entity* pEnt = entActivePlayerPtr();
		ControlScheme* pDefaultScheme = schemes_FindScheme(&g_DefaultControlSchemes, pchSchemeName);
		if (pEnt && pDefaultScheme)
		{
			schemes_UpdateAutoAdjustingData(pEnt, &g_CurrentScheme);
			ServerCmd_schemes_SaveAndChangeScheme(&g_CurrentScheme, pDefaultScheme->pchName);
			s_uLastSchemeChangeTime = timeSecondsSince2000();
		}
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0);
void ControlSchemeCycle(void)
{
	Entity* pEnt = entActivePlayerPtr();
	const char* pchCurrent = SAFE_MEMBER4(pEnt, pPlayer, pUI, pSchemes, pchCurrent);
	ControlScheme* pDefaultScheme = schemes_FindNextSelectableScheme(pEnt, &g_DefaultControlSchemes, pchCurrent);
	if (pDefaultScheme)
	{
		if (s_iCurrentControlScheme >= 0)
		{
			s_iCurrentControlScheme = eaFind(&g_DefaultControlSchemes.eaSchemes, pDefaultScheme);
			ControlSchemeCopyActiveScheme(false, true);
		}
		gclControlSchemeSetCurrent(pDefaultScheme->pchName);
	}
}

// This is a quick and dirty way to get ControlSchemeCycle to run on both
// key down and key up. 
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0);
void ControlSchemeChooseOneOf(bool bFirst, const char* pchFirst, const char* pchSecond)
{
	Entity* pEnt = entActivePlayerPtr();
	const char* pchSchemeName= bFirst ? pchFirst : pchSecond;
	ControlScheme* pNewScheme = schemes_FindScheme(&g_DefaultControlSchemes, pchSchemeName);
	printf("%s\n", pchSchemeName);
	if (pNewScheme)
	{
		if (s_iCurrentControlScheme >= 0)
		{
			s_iCurrentControlScheme = eaFind(&g_DefaultControlSchemes.eaSchemes, pNewScheme);
			ControlSchemeCopyActiveScheme(false, true);
		}
		gclControlSchemeSetCurrent(pNewScheme->pchName);
	}
}

static const char* ControlSchemeGetKeyBindProfileFromDisplayName(const char* pchDisplayName)
{
	KeyBindStoredProfiles *pProfiles = keybind_GetUserSelectableProfiles();

	if (pProfiles)
	{
		S32 i;
		for (i = 0; i < eaSize(&pProfiles->eaProfiles); i++)
		{
			KeyBindProfile* pProfile = pProfiles->eaProfiles[i];
			if (stricmp(pchDisplayName,TranslateMessageRef(pProfile->hNameMsg))==0)
			{
				return pProfile->pchName;
			}
		}
	}

	return NULL;
}

static void ControlSchemeKeyBindUpdateCallback(OptionSetting *setting)
{
	char** ppchProfiles = NULL;
	KeyBindStoredProfiles *pProfiles = keybind_GetUserSelectableProfiles();
	const char* pchLastProfile = ControlSchemeGetKeyBindProfileFromDisplayName(setting->pchStringValue);
	const char* pchCurrProfile = ControlSchemeGetValidKeyBindProfiles(pProfiles,&ppchProfiles);

	if (pchCurrProfile && (!pchLastProfile || stricmp(pchLastProfile,pchCurrProfile)!=0))
	{
		ControlSchemeFillKeyBindsFromProfile(pchCurrProfile);
	}
	
	if (eaSize(&ppchProfiles))
	{
		OptionSettingUpdateComboBoxOptions(setting,CONTROLSCHEME_CATEGORY,&ppchProfiles,false);
	}
	eaDestroy(&ppchProfiles);
}

static void ControlSchemeKeyBindChangedCallback(OptionSetting *setting)
{
	const char* pchProfile = ControlSchemeGetKeyBindProfileFromDisplayName(setting->pchStringValue);
	if (pchProfile && pchProfile[0])
	{
		s_ActiveStoredScheme.pchKeyProfileToLoad = allocAddString(pchProfile);
		ControlSchemeFillKeyBindsFromProfile(pchProfile);
	}
}

static void ControlSchemeFieldCommitCallback(OptionSetting *setting)
{
	if (eaSize(&g_eaStoredSchemes))
	{
		ControlSchemeCopyActiveScheme(true,false);

		schemes_SaveStoredSchemes();

		gclAutoAttack_Disable();
	}
}


static void ControlSchemeRevertCallback(OptionSetting *setting)
{
	if (s_iCurrentRegion != kControlSchemeRegionType_None)
	{
		ControlSchemeFillKeyBindsFromProfile(g_CurrentScheme.pchKeyProfileToLoad);
	}
	if (eaSize(&g_eaStoredSchemes))
	{
		schemes_DestroyAllStoredSchemes();
	}
}

static void ControlSchemeGetValidCameraTypes(const char*** pppchCameraTypes)
{
	RegionRules* pRules = ControlSchemeGetCurrentRegionRules();
	S32 i, j, k, iCameraTypeRulesSize = eaSize(&g_CameraTypeRules.eaDefs);
	for (i = 0; i < kCameraType_Count; i++)
	{	
		bool bAllowed = false;
		if (!iCameraTypeRulesSize)
		{
			bAllowed = true;
		}
		else
		{
			for (j = iCameraTypeRulesSize-1; j >= 0; j--)
			{
				CameraTypeRulesDef* pDef = g_CameraTypeRules.eaDefs[j];
				if (!pDef->bUserSelectable)
				{
					continue;
				}
				if (pDef->eType == i)
				{
					for (k = eaSize(&pDef->eaModeRegions)-1; k >= 0; k--)
					{
						if (!pRules || (pDef->eaModeRegions[k]->eRegions & pRules->eSchemeRegionType))
						{
							bAllowed = true;
							break;
						}
					}
					if (bAllowed)
					{
						break;
					}
				}
			}
		}
		if (bAllowed)
		{
			eaPush(pppchCameraTypes, StaticDefineIntRevLookup(CameraTypeEnum, i));
		}
	}
}

static void ControlSchemeUpdateCameraTypeCallback(OptionSetting *setting)
{
	char** ppchCameraTypes = NULL;
	const char* pchCameraType;

	ControlSchemeGetValidCameraTypes(&ppchCameraTypes);

	if (eaSize(&ppchCameraTypes))
	{
		OptionSettingUpdateComboBoxOptions(setting,CONTROLSCHEME_CATEGORY,&ppchCameraTypes,false);
	}
	pchCameraType = StaticDefineIntRevLookup(CameraTypeEnum, s_ActiveStoredScheme.eCameraType);
	s_iCurrentCameraTypeDisplay = eaFindString(&ppchCameraTypes, pchCameraType);
	if (s_iCurrentCameraTypeDisplay < 0)
		s_iCurrentCameraTypeDisplay = 0;

	eaDestroy(&ppchCameraTypes);
}

static void ControlSchemeCameraTypeChangedCallback(OptionSetting *setting)
{
	OptionSettingComboChoice* pChoice = eaGet(&setting->eaComboBoxOptions, setting->iIntValue);
	const char* pchCameraType = SAFE_MEMBER(pChoice, pchName);
	s_ActiveStoredScheme.eCameraType = StaticDefineIntGetInt(CameraTypeEnum, pchCameraType);
	if (s_ActiveStoredScheme.eCameraType < 0)
		s_ActiveStoredScheme.eCameraType = kCameraType_Free;
	s_ActiveStoredScheme.eLastCameraType = s_ActiveStoredScheme.eCameraType;
}

static void ControlSchemeGetValidAutoAttackTypes(const char*** pppchAutoAttackTypes)
{
	const ControlScheme* pActiveScheme = schemes_GetActiveStoredScheme();
	S32 i;
	for (i = 0; i < kAutoAttack_Count; i++)
	{
		if (i != kAutoAttack_None &&
			eaiSize(&pActiveScheme->peAllowedAutoAttackTypes) &&
			eaiFind(&pActiveScheme->peAllowedAutoAttackTypes, i) < 0)
		{
			continue;
		}
		eaPush(pppchAutoAttackTypes, StaticDefineIntRevLookup(AutoAttackTypeEnum, i));
	}
}

static void ControlSchemeUpdateAutoAttackTypeCallback(OptionSetting *setting)
{
	char** ppchAutoAttackTypes = NULL;
	const char* pchAutoAttackType;

	ControlSchemeGetValidAutoAttackTypes(&ppchAutoAttackTypes);

	if (eaSize(&ppchAutoAttackTypes))
	{
		OptionSettingUpdateComboBoxOptions(setting,CONTROLSCHEME_CATEGORY,&ppchAutoAttackTypes,false);
	}
	pchAutoAttackType = StaticDefineIntRevLookup(AutoAttackTypeEnum, s_ActiveStoredScheme.eAutoAttackType);
	s_iCurrentAutoAttackTypeDisplay = eaFindString(&ppchAutoAttackTypes, pchAutoAttackType);
	if (s_iCurrentAutoAttackTypeDisplay < 0)
		s_iCurrentAutoAttackTypeDisplay = 0;

	eaDestroy(&ppchAutoAttackTypes);
}

static void ControlSchemeAutoAttackTypeChangedCallback(OptionSetting *setting)
{
	OptionSettingComboChoice* pChoice = eaGet(&setting->eaComboBoxOptions, setting->iIntValue);
	const char* pchAutoAttackType = SAFE_MEMBER(pChoice, pchName);
	s_ActiveStoredScheme.eAutoAttackType = StaticDefineIntGetInt(AutoAttackTypeEnum, pchAutoAttackType);
	if (s_ActiveStoredScheme.eAutoAttackType < 0)
		s_ActiveStoredScheme.eAutoAttackType = kAutoAttack_None;
}

static void ControlSchemeRegionUpdateCallback(OptionSetting *setting)
{
	S32 i;
	if (s_iCurrentRegion == kControlSchemeRegionType_None)
	{
		s_iCurrentRegion = gclControlSchemeFindSchemeRegionForEnt(entActivePlayerPtr());
	}

	if (s_iCurrentRegion != kControlSchemeRegionType_None)
	{
		for (i = eaSize(&g_eaUIRegions)-1; i >= 0; i--)
		{
			if (g_eaUIRegions[i]->eSchemeRegion == s_iCurrentRegion)
				break;
		}
		if (i >= 0)
		{
			s_iCurrentRegionDisplay = i;
		}
	}
}

static void ControlSchemeRegionChangedCallback(OptionSetting *setting)
{
	ControlSchemeRegionData* pData = eaGet(&g_eaUIRegions,s_iCurrentRegionDisplay);

	if (pData)
	{
		s_iCurrentRegion = pData->eSchemeRegion;
	}

	OptionsReload();
}

S32 schemes_OptionsGetCurrentSelectedRegion()
{
	return s_iCurrentRegion;
}

void gclControlSchemeGenerateRegionList(void)
{
	if (!g_eaUIRegions)
	{
		S32 i;
		for (i = 0; i < eaSize(&g_ControlSchemeRegions.eaSchemeRegions); i++)
		{
			ControlSchemeRegionInfo* pInfo = g_ControlSchemeRegions.eaSchemeRegions[i];

			if (pInfo->ppchAllowedSchemes)
			{
				ControlSchemeRegionData* pData = StructCreate(parse_ControlSchemeRegionData);
				pData->eSchemeRegion = pInfo->eType;
				pData->pchName = TranslateDisplayMessage(pInfo->DisplayMsg);
				eaPush(&g_eaUIRegions, pData);
			}
		}
	}
}

static bool schemes_initRegionOptions(const char*** pppchSchemeRegionNames)
{
	S32 i;
	eaClear(pppchSchemeRegionNames);

	gclControlSchemeGenerateRegionList();

	for (i = 0; i < eaSize(&g_eaUIRegions); i++)
	{
		ControlSchemeRegionData* pData = g_eaUIRegions[i];

		eaPush(pppchSchemeRegionNames, pData->pchName);
	}

	if (eaSize(pppchSchemeRegionNames) == 0)
		return false;

	autoSettingsAddComboBox(CONTROLSCHEME_CATEGORY, "SchemeRegion", pppchSchemeRegionNames, false, &s_iCurrentRegionDisplay,
		ControlSchemeRegionChangedCallback, ControlSchemeFieldCommitCallback, ControlSchemeRegionUpdateCallback, ControlSchemeRevertCallback, NULL, true);

	return true;
}

// Return the name of the current keybind profile.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeGetCurrentKeyBindProfile);
const char *exprControlSchemeGetCurrentKeyBindProfile(void)
{
	return schemes_GetActiveStoredScheme()->pchKeyProfileToLoad;
}

// Gets the current keybind profile display name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeGetKeyBindProfileDisplayName);
const char* exprControlSchemeGetKeyBindProfileDisplayName(void)
{
	KeyBindStoredProfiles* pProfiles = keybind_GetUserSelectableProfiles();
	const char* pchProfile = schemes_GetActiveStoredScheme()->pchKeyProfileToLoad;
	S32 iIdx = keybind_FindInProfiles(pProfiles, pchProfile);
	KeyBindProfile* pProfile = eaGet(&pProfiles->eaProfiles, iIdx);
	if (pProfile)
	{
		return TranslateMessageRef(pProfile->hNameMsg);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeGetKeyBindProfileList);
void exprControlSchemeGetKeyBindProfileList(SA_PARAM_NN_VALID UIGen* pGen, S32 eRegion)
{
	KeybindProfileUI*** peaData = ui_GenGetManagedListSafe(pGen, KeybindProfileUI);
	KeyBindStoredProfiles* pProfiles = keybind_GetUserSelectableProfiles();
	const char* pchCurRegion = allocFindString(StaticDefineIntRevLookup(ControlSchemeRegionTypeEnum, eRegion));
	S32 i, iCount = 0;

	for (i = 0; i < eaSize(&pProfiles->eaProfiles); i++)
	{
		KeyBindProfile* pProfile = pProfiles->eaProfiles[i];
	
		if (!pProfile->ppchSchemeRegions || 
			eaFind(&pProfile->ppchSchemeRegions, pchCurRegion) >= 0)
		{
			KeybindProfileUI* pData = eaGetStruct(peaData, parse_KeybindProfileUI, iCount++);
			pData->pchDisplayName = TranslateMessageRef(pProfile->hNameMsg);
			pData->pchName = pProfile->pchName;
		}
	}

	eaSetSizeStruct(peaData, parse_KeybindProfileUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, KeybindProfileUI, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeUpdateKeyBindProfile);
void exprControlSchemeUpdateKeyBindProfile(const char* pchProfile)
{
	if (eaSize(&g_eaStoredSchemes) && s_iCurrentRegion >= 0 && s_iCurrentControlScheme >= 0)
	{
		KeyBindStoredProfiles* pProfiles = keybind_GetUserSelectableProfiles();
		S32 iIdx = keybind_FindInProfiles(pProfiles, pchProfile);
		KeyBindProfile* pProfile = eaGet(&pProfiles->eaProfiles, iIdx);
		const char* pchCurRegion = StaticDefineIntRevLookup(ControlSchemeRegionTypeEnum, s_iCurrentRegion);
		pchCurRegion = allocFindString(pchCurRegion);
		
		if (pProfile && 
			(!pProfile->ppchSchemeRegions || eaFind(&pProfile->ppchSchemeRegions, pchCurRegion) >= 0))
		{
			S32 i;
			for (i = eaSize(&g_eaStoredSchemes)-1; i >= 0; i--)
			{
				ControlScheme* pScheme = g_eaStoredSchemes[i];
				if (stricmp(pScheme->pchKeyProfileToLoad, pchProfile)==0)
				{
					s_iCurrentControlScheme = i;
					break;
				}
			}
			if (i >= 0)
			{
				ControlSchemeCopyActiveScheme(true, true);
			}
			else if (!eaSize(&s_ActiveStoredScheme.ppchAllowedKeyProfiles) ||
					 eaFindString(&s_ActiveStoredScheme.ppchAllowedKeyProfiles, pchProfile) >= 0)
			{
				s_ActiveStoredScheme.pchKeyProfileToLoad = allocAddString(pchProfile);
			}
			OptionsReload();
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeGetRegionList);
void exprControlSchemeGetRegionList(SA_PARAM_NN_VALID UIGen* pGen)
{
	gclControlSchemeGenerateRegionList();

	ui_GenSetList(pGen, &g_eaUIRegions, parse_ControlSchemeRegionData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeSetCurrentRegion);
void exprControlSchemeSetCurrentRegion(S32 eSchemeRegion)
{
	s_iCurrentRegion = eSchemeRegion;
	OptionsReload();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeGetCurrentRegion);
S32 exprControlSchemeGetCurrentRegion(void)
{
	if (s_iCurrentRegion == kControlSchemeRegionType_None)
	{
		return gclControlSchemeFindSchemeRegionForEnt(entActivePlayerPtr());
	}
	return s_iCurrentRegion;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeGetAllSchemeRegions);
S32 exprControlSchemeGetAllSchemeRegions(void)
{
	return schemes_GetAllSchemeRegions();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeGetRegionDisplayName);
const char* exprControlSchemeGetRegionDisplayName(S32 eSchemeRegion)
{
	gclControlSchemeGenerateRegionList();

	if (g_eaUIRegions && eSchemeRegion != kControlSchemeRegionType_None)
	{
		S32 i;
		for (i = eaSize(&g_eaUIRegions)-1; i >= 0; i--)
		{
			if (g_eaUIRegions[i]->eSchemeRegion == eSchemeRegion)
				break;
		}
		
		if (i >= 0)
		{
			ControlSchemeRegionData* pData = g_eaUIRegions[i];
			if (pData->pchName && pData->pchName[0])
			{
				return pData->pchName;
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeIsAutoAttackEnabled);
bool exprControlSchemeIsAutoAttackEnabled(void)
{
	return g_CurrentScheme.eAutoAttackType != kAutoAttack_None;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ControlSchemeIsKeepMovingOnContactEnabled);
bool exprControlSchemeIsKeepMovingOnContactEnabled(void)
{
	return !!g_CurrentScheme.bKeepMovingDuringContactDialogs;
}

static void displayDistanceValue(OptionSetting *setting)
{
	F32 fResult = 0.0f;
	F32 fCurrent = setting->eType==kOptionSettingFloatSlider ? setting->fFloatValue : (F32)setting->iIntValue;
	RegionRules* pRules = ControlSchemeGetCurrentRegionRules();
	if (pRules)
	{
		F32 fScale = pRules->fDefaultDistanceScale;
		F32 fType = pRules->eDefaultMeasurement;
		F32 fSize = pRules->eMeasurementSize;

		fResult = BaseToMeasurement(fCurrent,fType,fSize) * fScale;
	}
	else
	{
		fResult = fCurrent;
	}
	estrPrintf(&setting->pchStringValue, "%5.1f", fResult);
}

static void inputDistanceValue(OptionSetting *setting)
{
	F32 fResult = 0.0f;
	F32 fCurrent = setting->eType==kOptionSettingFloatSlider ? setting->fFloatValue : (F32)setting->iIntValue;
	RegionRules* pRules = ControlSchemeGetCurrentRegionRules();
	if (pRules)
	{
		F32 fScale = pRules->fDefaultDistanceScale;
		F32 fType = pRules->eDefaultMeasurement;
		F32 fSize = pRules->eMeasurementSize;
		MAX1F(fScale, FLT_EPSILON);
		fResult = MeasurementToBase(fCurrent/fScale,fType,fSize);
	}
	else
	{
		fResult = fCurrent;
	}
	if (setting->eType==kOptionSettingFloatSlider)
	{
		setting->fFloatValue = fResult;
	}
	else
	{
		setting->iIntValue = (S32)(fResult);
	}
}

// autoSettingsAddFloatSliderEx doesn't display the value as the actual float value, so I'm forcing it here.
void displayTheFloatValue(OptionSetting *setting)
{
	estrPrintf(&setting->pchStringValue, "%5.1f", setting->fFloatValue);
}

static bool ControlSchemeHideInvertMouseOptions(OptionSetting *setting)
{
	return !g_bInvertMousePerScheme;
}

void DEFAULT_LATELINK_schemes_ControlSchemeOptionInit(const char* pchCategory)
{
	// nothing to do
}

void schemes_initOptions(Entity *pEnt)
{
	int i, iCurrentKeyBindProfile = 0;
	int count = 0;
	ControlScheme *pScheme = NULL;
	KeyBindStoredProfiles *pProfiles = keybind_GetUserSelectableProfiles();
	const char** ppchControlSchemeNames = NULL;	
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pSchemes)
	{	
		ControlScheme* pStoredScheme = &s_ActiveStoredScheme;
		CameraSettings* pCameraSettings = gclCamera_GetSettings(NULL);

		schemes_initRegionOptions(&ppchControlSchemeNames);

		eaClear(&ppchControlSchemeNames);
		
		for (i = 0; i < eaSize(&g_DefaultControlSchemes.eaSchemes); i++)
		{
			pScheme = g_DefaultControlSchemes.eaSchemes[i];
			if (!schemes_IsSchemeSelectable(pEnt, pScheme))
				continue;
			eaPush(&ppchControlSchemeNames,TranslateMessageRef(pScheme->hNameMsg));
		}

		autoSettingsAddComboBox(CONTROLSCHEME_CATEGORY, CONTROLSCHEME_CATEGORY, &ppchControlSchemeNames, false, &s_iCurrentControlSchemeDisplay,
			ControlSchemeChangedCallback, ControlSchemeCommitCallback, ControlSchemeUpdateCallback, ControlSchemeRevertCallback, NULL, true);
		
		eaClear(&ppchControlSchemeNames);

		for (i = 0; i < eaSize(&pProfiles->eaProfiles); i++)
		{
			KeyBindProfile *pProfile = pProfiles->eaProfiles[i];
			eaPush(&ppchControlSchemeNames,TranslateMessageRef(pProfile->hNameMsg));
		}

		if (gConf.bKeybindsFromControlSchemes)
		{
			autoSettingsAddComboBox(CONTROLSCHEME_CATEGORY, "KeyBindSet", &ppchControlSchemeNames, false, &s_iCurrentKeyBindProfileDisplay,
				ControlSchemeKeyBindChangedCallback, ControlSchemeFieldCommitCallback, ControlSchemeKeyBindUpdateCallback, ControlSchemeRevertCallback, NULL, true);
		}

		eaClear(&ppchControlSchemeNames);
		ControlSchemeGetValidCameraTypes(&ppchControlSchemeNames);
		
		autoSettingsAddComboBox(CONTROLSCHEME_CATEGORY, "CameraType", &ppchControlSchemeNames, true, (int *)&s_iCurrentCameraTypeDisplay, ControlSchemeCameraTypeChangedCallback, ControlSchemeFieldCommitCallback, ControlSchemeUpdateCameraTypeCallback, ControlSchemeRevertCallback, NULL, true);

		eaClear(&ppchControlSchemeNames);
		for (i = 0; i < kCameraFollowType_Count; i++)
		{		
			eaPush(&ppchControlSchemeNames, StaticDefineIntRevLookup(CameraFollowTypeEnum, i));
		}
		autoSettingsAddComboBox(CONTROLSCHEME_CATEGORY, "CamFollowType", &ppchControlSchemeNames, true, (int *)&pStoredScheme->eCameraFollowType, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true);
										
		autoSettingsAddFloatSlider(CONTROLSCHEME_CATEGORY, "CameraMaxDistance", 
									 pCameraSettings->fMaxDistanceMinValue, 
									 pCameraSettings->fMaxDistanceMaxValue, 
									 &pStoredScheme->fCamMaxDistance, 
									 NULL, ControlSchemeFieldCommitCallback, NULL, 
									 ControlSchemeRevertCallback, displayDistanceValue, inputDistanceValue, true);
		
		autoSettingsAddFloatSliderEx(CONTROLSCHEME_CATEGORY, "CamMouseLookFactor", 
									pCameraSettings->fMouseSensitivityMin, 
									pCameraSettings->fMouseSensitivityMax,
									0.1f, 
									&pStoredScheme->fCamMouseLookSensitivity, 
									NULL, ControlSchemeFieldCommitCallback, NULL, 
									ControlSchemeRevertCallback, displayTheFloatValue, NULL, true);

		autoSettingsAddFloatSliderEx(CONTROLSCHEME_CATEGORY, "CamControllerLookFactor", 
									pCameraSettings->fControllerSensitivityMin, 
									pCameraSettings->fControllerSensitivityMax,
									0.1f, 
									&pStoredScheme->fCamControllerLookSensitivity, 
									NULL, ControlSchemeFieldCommitCallback, NULL, 
									ControlSchemeRevertCallback, displayTheFloatValue, NULL, true);
		
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"CameraShake", (int *)&pStoredScheme->bCameraShake, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"EnableNearOffset", (int*)&pCameraSettings->bUseNearOffset, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true);
		
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"DisableFaceSelected", (int *)&pStoredScheme->bDisableFaceSelected, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"SnapOnAttack", (int *)&pStoredScheme->bSnapCameraOnAttack, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"Strafing", (int *)&pStoredScheme->bStrafing, 1, NULL, ControlSchemeFieldCommitCallback, /*ControlSchemeStrafingUpdateCallback*/NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"TurningTurnsWhenFacingTarget", (int *)&pStoredScheme->bTurningTurnsCameraWhenFacingTarget, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"ShowMouseLookReticle", (int *)&pStoredScheme->bShowMouseLookReticle, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );

		count = 0;
		eaClear(&ppchControlSchemeNames);
		for (i = 0; i < kPowerCursorActivationType_Count; i++)
		{		
			eaPush(&ppchControlSchemeNames, StaticDefineIntRevLookup(PowerCursorActivationTypeEnum, i));
		}
		autoSettingsAddComboBox(CONTROLSCHEME_CATEGORY, "PowerCursorActivation", &ppchControlSchemeNames, true, (int *)&pStoredScheme->ePowerCursorActivationType, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true);
		
		count = 0;
		eaClear(&ppchControlSchemeNames);
		for (i = 0; i < kTargetOrder_Count; i++)
		{
			if (!(gConf.bDontShowLuckyCharmsInOptions && (i == kTargetOrder_LuckyCharms)))
			{
				eaPush(&ppchControlSchemeNames, StaticDefineIntRevLookup(TargetOrderEnum, count++));
			}
		}

		autoSettingsAddComboBox(CONTROLSCHEME_CATEGORY, "TabTargetOrder", &ppchControlSchemeNames, true, (int *)&pStoredScheme->eTabTargetOrder, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true);	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"InvertMouseX", (int *)&pStoredScheme->bInvertMouseX, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true);
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"InvertMouseY", (int *)&pStoredScheme->bInvertMouseY, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true);
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"ResetTabOverTime", (int *)&pStoredScheme->bResetTabOverTime, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"CancelOnOffClick", (int *)&pStoredScheme->bCancelTargetOnOffClick, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"CancelIfOffScreen", (int *)&pStoredScheme->bDeselectIfOffScreen, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );

		autoSettingsAddComboBox(CONTROLSCHEME_CATEGORY, "SoftTargetOrder", &ppchControlSchemeNames, true, (int *)&pStoredScheme->eInitialTargetOrder, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true);
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"SoftTarget", (int *)&pStoredScheme->bSoftTarget, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"RequireHardTarget", (int *)&pStoredScheme->bRequireHardTargetToExec, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"AttackBecomesHardTarget", (int *)&pStoredScheme->bAutoHardTarget, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"MeleeIgnoreBadHardTarget", (int *)&pStoredScheme->bMeleeIgnoreBadHardTarget, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"AssistTargetIfInvalid", (int *)&pStoredScheme->bAssistTargetIfInvalid, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );

		autoSettingsAddFloatSlider(CONTROLSCHEME_CATEGORY, "TabTargetDistance", 0.0f, 200.0f, (F32*)&pStoredScheme->fTabTargetMaxDist, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, displayDistanceValue, inputDistanceValue, true);
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"NeverTargetObjects", (int *)&pStoredScheme->bNeverTargetObjects, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"NeverTargetPets", (int *)&pStoredScheme->bNeverTargetPets, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"TargetThreateningFirst", (int *)&pStoredScheme->bTargetThreateningFirst, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"TargetAttackerIfAttacked", (int *)&pStoredScheme->bTargetAttackerIfAttacked, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"TabTargetOffscreen", (int *)&pStoredScheme->bTabTargetOffscreen, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"StopMovingOnInteract", (int *)&pStoredScheme->bStopMovingOnInteract, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"KeepMovingOnContact", (int *)&pStoredScheme->bKeepMovingDuringContactDialogs, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"DoubleTapDirToRoll", (int *)&pStoredScheme->bDoubleTapDirToRoll, 1, NULL, ControlSchemeFieldCommitCallback, /*ControlSchemeGroundFieldUpdateCallback*/NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"AimModeAsToggle", (int *)&pStoredScheme->bAimModeAsToggle, 1, NULL, ControlSchemeFieldCommitCallback, /*ControlSchemeGroundFieldUpdateCallback*/NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"UseZoomCameraWithAimMode", (int *)&pStoredScheme->bUseZoomCamWithAimMode, 1, NULL, ControlSchemeFieldCommitCallback, /*ControlSchemeGroundFieldUpdateCallback*/NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"TargetLockCamDisableWhenNoTarget", (int *)&pStoredScheme->bTargetLockCamDisableWhenNoTarget, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddBit(CONTROLSCHEME_CATEGORY,"DelayAutoAttackUntilCombat", (int *)&pStoredScheme->bDelayAutoAttackUntilCombat, 1, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, true );	
		autoSettingsAddFloatSlider(CONTROLSCHEME_CATEGORY, "AutoAttackDelay", 0.0f, 2.0f, (F32*)&pStoredScheme->fAutoAttackDelay, NULL, ControlSchemeFieldCommitCallback, NULL, ControlSchemeRevertCallback, NULL, NULL, true);

		eaClear(&ppchControlSchemeNames);
		ControlSchemeGetValidAutoAttackTypes(&ppchControlSchemeNames);

		autoSettingsAddComboBox(CONTROLSCHEME_CATEGORY, "AutoAttack", &ppchControlSchemeNames, true, (int *)&s_iCurrentAutoAttackTypeDisplay, ControlSchemeAutoAttackTypeChangedCallback, ControlSchemeFieldCommitCallback, ControlSchemeUpdateAutoAttackTypeCallback, ControlSchemeRevertCallback, NULL, true);	

		eaDestroy(&ppchControlSchemeNames);

		//always hide the following options by default
		options_HideOption(CONTROLSCHEME_CATEGORY, "InvertMouseX", ControlSchemeHideInvertMouseOptions);
		options_HideOption(CONTROLSCHEME_CATEGORY, "InvertMouseY", ControlSchemeHideInvertMouseOptions);
		options_HideOption(CONTROLSCHEME_CATEGORY, "KeepMovingOnContact", NULL);
		options_HideOption(CONTROLSCHEME_CATEGORY, "DoubleTapDirToRoll", NULL);
		options_HideOption(CONTROLSCHEME_CATEGORY, "AimModeAsToggle", NULL);
		options_HideOption(CONTROLSCHEME_CATEGORY, "UseZoomCameraWithAimMode", NULL);
		options_HideOption(CONTROLSCHEME_CATEGORY, "TargetLockCamDisableWhenNoTarget", NULL);
		options_HideOption(CONTROLSCHEME_CATEGORY, "TabTargetOffscreen", NULL);
		options_HideOption(CONTROLSCHEME_CATEGORY, "AutoAttackDelay", NULL);
		options_HideOption(CONTROLSCHEME_CATEGORY, "DelayAutoAttackUntilCombat", NULL);
		options_HideOption(CONTROLSCHEME_CATEGORY, "NeverTargetPets", NULL);
		schemes_ControlSchemeOptionInit(CONTROLSCHEME_CATEGORY);
	}
}

void schemes_Cleanup(void)
{
	OptionCategoryDestroy(CONTROLSCHEME_CATEGORY);
}

/* End of File */
