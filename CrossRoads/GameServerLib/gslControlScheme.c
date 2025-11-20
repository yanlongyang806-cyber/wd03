/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#include "StringCache.h"

#include "gslEntity.h"
#include "gslEntityDebug.h"
#include "GlobalTypes.h"
#include "cmdServer.h"
#include "Character.h"
#include "ControlScheme.h"
#include "CombatConfig.h"
#include "EntityMovementDefault.h"
#include "EntityMovementTactical.h"
#include "estring.h"
#include "gamestringformat.h"
#include "gslLogSettings.h"
#include "logging.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "RegionRules.h"
#include "WorldGrid.h"
#include "AutoGen/ControlScheme_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "EntityMovementManager.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//forward declarations
void entity_SetCurrentScheme(Entity *e, const char *pch);

/***************************************************************************
 * SetCurrentScheme
 *
 * Sets the current scheme. 
 *
 */
static void SetCurrentScheme(SA_PARAM_NN_VALID ControlSchemes *p, SA_PARAM_NN_STR const char *pchName, ControlSchemeRegionInfo *pInfo)
{
	p->pchCurrent = allocAddString(pchName);
	p->iVersion++;

	if ( pInfo && pInfo->ppchAllowedSchemes )
	{
		S32 i, iSize = eaSize(&p->eaSchemeRegions);
		S32 eRegion = pInfo->eType;
		for ( i = 0; i < iSize; i++ )
		{
			if ( eRegion == p->eaSchemeRegions[i]->eType )
			{
				break;
			}
		}
		if ( i < iSize )
		{
			p->eaSchemeRegions[i]->pchScheme = allocAddString(p->pchCurrent);
		}
		else
		{
			ControlSchemeRegion* pData = StructCreate( parse_ControlSchemeRegion );
			pData->eType = eRegion;
			pData->pchScheme = allocAddString(p->pchCurrent);
			eaPush(&p->eaSchemeRegions,pData);
		}
	}
}

/***************************************************************************
 * entity_UpdateForCurrentControlScheme
 *
 * Sets any global state so that the entity actually operates according to
 *   the current control scheme. This should be called any time the scheme
 *   changes.
 * If the entity has no current control scheme, the default scheme is set to
 *   current.
 */

void entity_UpdateForCurrentControlScheme(SA_PARAM_NN_VALID Entity *e)
{
	if(SAFE_MEMBER3(e, pPlayer, pUI, pSchemes))
	{
		ControlSchemes *pSchemes = e->pPlayer->pUI->pSchemes;
		ControlScheme *pOldScheme;

		PERFINFO_AUTO_START_FUNC();

		if(!pSchemes->pchCurrent)
		{
			// There is no scheme set, so grab the default one and set it as current.
			if(eaGet(&g_DefaultControlSchemes.eaSchemes,0) && g_DefaultControlSchemes.eaSchemes[0]->pchName)
			{
				entity_SetCurrentScheme(e, g_DefaultControlSchemes.eaSchemes[0]->pchName);
			}
		}
		pOldScheme = schemes_FindScheme(&g_DefaultControlSchemes, pSchemes->pchCurrent);
		if (!pOldScheme)
		{
			RegionRules* pRules = getRegionRulesFromEnt(e);

			if ( pRules && pRules->pchSchemeToLoad )
			{
				entity_SetCurrentScheme(e, pRules->pchSchemeToLoad);
			}
			else if(eaGet(&g_DefaultControlSchemes.eaSchemes,0) && g_DefaultControlSchemes.eaSchemes[0]->pchName)
			{
				entity_SetCurrentScheme(e, g_DefaultControlSchemes.eaSchemes[0]->pchName);
			}
			pOldScheme = schemes_FindScheme(&g_DefaultControlSchemes, pSchemes->pchCurrent);
		}

		if(pSchemes->pchCurrent && pOldScheme)
		{
			ControlScheme *p = schemes_FindScheme(pSchemes, pSchemes->pchCurrent);

			if(!p)
			{				
				p = StructClone(parse_ControlScheme, pOldScheme);
				eaPush(&e->pPlayer->pUI->pSchemes->eaSchemes, p);
			}
			else if (pOldScheme->iVersion > p->iVersion)
			{
				StructCopyAll(parse_ControlScheme, pOldScheme, p);
			}
			else
			{
				StructCopy(parse_ControlScheme, pOldScheme, p, 0, 0, TOK_PERSIST);
			}

			if(p)
			{
				//
				// Finally, Now we have the scheme definition, set the extra state we need set.
				//
				bool bOverrideRequireValidTarget = zmapInfoGetPowersRequireValidTarget(NULL);

				// Always set the 'Maintain' type auto attack for shooter control schemes
				if (p->bEnableShooterCamera)
				{
					p->eAutoAttackType = kAutoAttack_Maintain;
				}
				e->pPlayer->bUseFacingPitch = p->bUseFacingPitch;

				if (!g_CombatConfig.bCharacterClassSpecifiesStrafing)
					gslEntitySetIsStrafing(e, p->bStrafing);

				gslEntitySetUseThrottle(e, p->bUseThrottle);
				gslEntitySetUseOffsetRotation(e, p->bUseOffsetRotation);

				// Set character flags
				e->pChar->bDisableFaceSelected = (p->bEnableShooterCamera || p->bDisableFaceSelected);
				e->pChar->bDisableFaceActivate = p->bDisableFaceActivate;
				e->pChar->bRequireValidTarget = bOverrideRequireValidTarget ? true : p->bRequireValidTarget;
				e->pChar->bUseCameraTargeting = (g_CombatConfig.bUseCameraTargeting || p->bUseCameraTargeting);
				e->pChar->bDisablePowerQueuing = (g_CombatConfig.bDisablePowerQueuing || p->bDisablePowerQueuing);
				e->pChar->bShooterControls = p->bEnableShooterCamera;
				entity_SetDirtyBit(e, parse_Character, e->pChar, false);

				mrTacticalSetRollOnDoubleTap(e->mm.mrTactical, p->bDoubleTapDirToRoll);
				mrSurfaceSetPitchDiffMultiplier(e->mm.mrSurface, p->fPitchDiffMultiplier);
			}
		}
		entity_SetDirtyBit(e, parse_PlayerUI, e->pPlayer->pUI, true);
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);

		PERFINFO_AUTO_STOP();
	}
}

/***************************************************************************
 * schemes_SetCurrentCmd
 *
 */
// Sets your current control/targeting scheme to the given named scheme
AUTO_COMMAND ACMD_NAME("schemes_SetCurrent") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void entity_SetCurrentScheme(Entity *e, const char *pch)
{
	ControlScheme *p;
	ControlSchemeRegionInfo* pRegionInfo;

	if(!pch || !*pch || !e || !e->pPlayer || !e->pPlayer->pUI || !e->pPlayer->pUI->pSchemes)
		return;
	
	if (stricmp(pch, e->pPlayer->pUI->pSchemes->pchCurrent) == 0)
		return; // Nothing to do

	//make sure this is the right control scheme for the region
	if (!Entity_IsValidControlSchemeForCurrentRegionEx(e, pch, &pRegionInfo))
		return;

	p = schemes_FindScheme(&g_DefaultControlSchemes, pch);
	if(schemes_IsSchemeSelectable(e, p))
	{
		char * tmpS = NULL;

		estrStackCreate(&tmpS);

		entFormatGameMessageKey(e, &tmpS, "ControlScheme_ChangeSucceeded", STRFMT_MESSAGEREF("Name", p->hNameMsg),STRFMT_END);

		notify_NotifySend(e, kNotifyType_ControlScheme_ChangeSucceeded, tmpS, NULL, NULL);
		estrDestroy(&tmpS);
		
		SetCurrentScheme(e->pPlayer->pUI->pSchemes, p->pchName, pRegionInfo);

		entity_UpdateForCurrentControlScheme(e);

		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_OPTIONS, e, "ChangedControlSchemePreset", "%s", p->pchName);
		}
		entity_SetDirtyBit(e, parse_PlayerUI, e->pPlayer->pUI, true);
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}
	else
	{
		char * tmpS = NULL;
		estrStackCreate(&tmpS);

		entFormatGameMessageKey(e, &tmpS, "ControlScheme_ChangeFailed", STRFMT_STRING("Name", pch),STRFMT_END);

		notify_NotifySend(e, kNotifyType_ControlScheme_ChangeFailed, tmpS, NULL, NULL);
		estrDestroy(&tmpS);
	}
}

AUTO_COMMAND ACMD_NAME("schemes_Reset") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void entity_ResetControlSchemes(Entity *e)
{
	int iVersion;
	if(!e || !e->pPlayer || !e->pPlayer->pUI || !e->pPlayer->pUI->pSchemes)
		return;

	iVersion = e->pPlayer->pUI->pSchemes->iVersion;
	StructReset(parse_ControlSchemes, e->pPlayer->pUI->pSchemes);
	e->pPlayer->pUI->pSchemes->iVersion = iVersion;
	entity_SetDirtyBit(e, parse_PlayerUI, e->pPlayer->pUI, true);
	entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);

	entity_UpdateForCurrentControlScheme(e);
}

// Checks to see if the settings in pScheme are valid
static bool scheme_Validate(SA_PARAM_NN_VALID ControlScheme* pScheme, SA_PARAM_NN_VALID ControlScheme* pDefaultScheme)
{
	if (eaSize(&pDefaultScheme->ppchAllowedKeyProfiles) &&
		eaFindString(&pDefaultScheme->ppchAllowedKeyProfiles, pScheme->pchKeyProfileToLoad) < 0)
	{
		return false;
	}

	if (pScheme->eAutoAttackType != kAutoAttack_None &&
		eaiSize(&pDefaultScheme->peAllowedAutoAttackTypes) &&
		eaiFind(&pDefaultScheme->peAllowedAutoAttackTypes, pScheme->eAutoAttackType) < 0)
	{
		return false;
	}

	return true;
}

AUTO_COMMAND ACMD_NAME("schemes_SetDetails") ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void entity_SetSchemeDetails(Entity *e, ControlScheme *pScheme)
{
	static char *diffString;
	ControlScheme *pDefaultScheme;
	if (!pScheme || !e)
	{
		return;
	}
	pDefaultScheme = schemes_FindScheme(&g_DefaultControlSchemes, pScheme->pchName);
	if (!pDefaultScheme || !schemes_IsSchemeSelectable(e, pDefaultScheme))
	{
		return;
	}
	if (!scheme_Validate(pScheme, pDefaultScheme))
	{
		return;
	}
	if(e && e->pPlayer && e->pPlayer->pUI && e->pPlayer->pUI->pSchemes)
	{
		ControlScheme *pOldScheme = schemes_FindScheme(e->pPlayer->pUI->pSchemes, pScheme->pchName);
		if (!pOldScheme)
		{
			pOldScheme = StructClone(parse_ControlScheme, pDefaultScheme);
			eaPush(&e->pPlayer->pUI->pSchemes->eaSchemes, pOldScheme);
		}

		estrClear(&diffString);
		StructWriteTextDiff(&diffString, parse_ControlScheme, pOldScheme, pScheme, NULL, TOK_USEROPTIONBIT_1, 0, 0);
		// Only copy over a subset
		StructCopy(parse_ControlScheme, pScheme, pOldScheme, 0, TOK_USEROPTIONBIT_1, 0);
		e->pPlayer->pUI->pSchemes->iVersion++;

		entity_UpdateForCurrentControlScheme(e);
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_OPTIONS, e, "ChangedControlSchemeDetails", "Scheme %s changes: %s", pScheme->pchName, diffString);
		}
		entity_SetDirtyBit(e, parse_PlayerUI, e->pPlayer->pUI, true);
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}

}

AUTO_COMMAND ACMD_NAME("schemes_SaveAndChangeScheme") ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void entity_SaveAndChangeScheme(Entity *e, ControlScheme *pSaveScheme, const char* pchChangeToScheme)
{
	entity_SetSchemeDetails(e,pSaveScheme);
	entity_SetCurrentScheme(e,pchChangeToScheme);
}

void entity_FixupControlSchemes(Entity *e)
{
	ControlSchemes* pSchemes = SAFE_MEMBER3(e, pPlayer, pUI, pSchemes);

	if (pSchemes)
	{
		S32 i, j;
		bool bFixupBinds = false;
		bool bClearBinds = false;

		// Check for invalid schemes
		for (i = eaSize(&pSchemes->eaSchemeRegions)-1; i >= 0; i--)
		{
			const char* pchScheme = pSchemes->eaSchemeRegions[i]->pchScheme;
			if (!pchScheme || !pchScheme[0] || pSchemes->eaSchemeRegions[i]->eType == kControlSchemeRegionType_None)
			{
				StructDestroy(parse_ControlSchemeRegion, eaRemove(&pSchemes->eaSchemeRegions, i));
				bClearBinds = true;
			}
		}
		// Check for duplicate regions
		for (i = eaSize(&pSchemes->eaSchemeRegions)-2; i >= 0; i--)
		{
			ControlSchemeRegionType eType = pSchemes->eaSchemeRegions[i]->eType;

			for (j = i + 1; j < eaSize(&pSchemes->eaSchemeRegions); j++)
			{
				if (eType == pSchemes->eaSchemeRegions[j]->eType)
				{
					StructDestroy(parse_ControlSchemeRegion, eaRemove(&pSchemes->eaSchemeRegions, j));
					bClearBinds = true;
					break;
				}
			}
		}
		// Remove any control schemes that have invalid key profiles
		for (i = eaSize(&pSchemes->eaSchemes)-1; i >= 0; i--)
		{
			ControlScheme* pScheme = pSchemes->eaSchemes[i];
			ControlScheme* pDefaultScheme = schemes_FindScheme(&g_DefaultControlSchemes, pScheme->pchName);
			
			if (!pDefaultScheme || 
				(pDefaultScheme->ppchAllowedKeyProfiles &&
				eaFindString(&pDefaultScheme->ppchAllowedKeyProfiles, pScheme->pchKeyProfileToLoad) < 0))
			{
				StructDestroy(parse_ControlScheme, eaRemove(&pSchemes->eaSchemes, i));
				bFixupBinds = true;
			}
		}
		if (bClearBinds || bFixupBinds)
		{
			if (bClearBinds)
			{
				// Reset the player's keybinds
				eaDestroyStruct(&e->pPlayer->pUI->eaBindProfiles, parse_EntityKeyBinds);
			}
			else if (bFixupBinds)
			{
				// Fixup the player's keybinds
				gslKeyBinds_Fixup(e);
			}
			entity_SetDirtyBit(e, parse_PlayerUI, e->pPlayer->pUI, true);
			entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
		}
	}
}

void entity_SetValidControlSchemeForRegion(Entity* pEnt, RegionRules* pRegionRules)
{
	ControlSchemes* pSchemes = SAFE_MEMBER3(pEnt, pPlayer, pUI, pSchemes);

	if (!pRegionRules && pEnt)
	{
		pRegionRules = getRegionRulesFromEnt(pEnt);
	}
	if (pSchemes && pRegionRules) 
	{
		S32 i;
		for (i = eaSize(&pSchemes->eaSchemeRegions)-1; i >= 0; i--)
		{
			if (pSchemes->eaSchemeRegions[i]->eType == pRegionRules->eSchemeRegionType)
			{
				break;
			}
		}

		if (i >= 0)
		{
			entity_SetCurrentScheme(pEnt, pSchemes->eaSchemeRegions[i]->pchScheme);
		}
		else
		{
			entity_SetCurrentScheme(pEnt, pRegionRules->pchSchemeToLoad);
		}
	}
}