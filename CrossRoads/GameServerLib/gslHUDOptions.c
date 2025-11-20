#include "gslHUDOptions.h"
#include "ControlScheme.h"
#include "crypt.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "EString.h"
#include "HUDOptionsCommon.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "RegionRules.h"

static S32 gslHUDOptions_EntGetIndexForSchemeRegion(Entity* pEnt, ControlSchemeRegionType eRegion)
{
	S32 i = -1;
	if (pEnt && pEnt->pPlayer)
	{
		for (i = eaSize(&pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions)-1; i >= 0; i--)
		{
			PlayerHUDOptions* pHUDOptions = pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions[i];
			if (pHUDOptions->eRegion == eRegion)
			{
				break;
			}
		}
	}
	return i;
}

static PlayerHUDOptions* gslHUDOptions_EntGetCurrentOptions(Entity* pEnt)
{
	ControlSchemeRegionType eSchemeRegion = getSchemeRegionTypeFromRegionType(entGetWorldRegionTypeOfEnt(pEnt));
	S32 i = gslHUDOptions_EntGetIndexForSchemeRegion(pEnt, eSchemeRegion);
	PlayerHUDOptions* pHUDOptions = eaGet(&pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions, i);

	if (!pHUDOptions)
	{
		pHUDOptions = StructClone(parse_PlayerHUDOptions, getDefaultHUDOptions(eSchemeRegion));
		eaPush(&pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions, pHUDOptions);
	}
	return pHUDOptions;
}

static bool gslHUDOptions_FixupInternal(Entity* pEnt, PlayerLooseUI* pLooseUI, ControlSchemeRegionType eSchemeRegion)
{
	if (	pLooseUI->pShowOverhead_Obsolete 
		||	pLooseUI->eNotifyAudioMode_Obsolete != PlayerNotifyAudioMode_Unset
		||	pLooseUI->uiTrayMode_Obsolete > 0
		||	pLooseUI->uiPowerLevelsMode_Obsolete > 0)
	{
		S32 i = gslHUDOptions_EntGetIndexForSchemeRegion(pEnt, eSchemeRegion);
		PlayerHUDOptions* pHUDOptions = eaGet(&pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions, i);
		if (!pHUDOptions)
		{
			pHUDOptions = StructCreate(parse_PlayerHUDOptions);
			pHUDOptions->eRegion = eSchemeRegion;
			pHUDOptions->eNotifyAudioMode = pLooseUI->eNotifyAudioMode_Obsolete;
			pHUDOptions->uiTrayMode = pLooseUI->uiTrayMode_Obsolete;
			pHUDOptions->uiPowerLevelsMode = pLooseUI->uiPowerLevelsMode_Obsolete;
			eaPush(&pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions, pHUDOptions);

			if (pLooseUI->pShowOverhead_Obsolete)
			{
				StructCopyAll(parse_PlayerShowOverhead, pLooseUI->pShowOverhead_Obsolete, &pHUDOptions->ShowOverhead);
			}
			else
			{
				PlayerHUDOptions* pDefaultOptions = getDefaultHUDOptions(eSchemeRegion);
				StructCopyAll(parse_PlayerShowOverhead, &pDefaultOptions->ShowOverhead, &pHUDOptions->ShowOverhead);
			}
		}
		return true;
	}
	return false;
}

static bool gslHUDOptions_FixupPuppet(Entity* pEnt, PuppetEntity* pPuppet)
{
	bool bSuccess = false;
	if (pPuppet->pchLooseUI_Obsolete)
	{
		PlayerLooseUI* pLooseUI = StructCreate(parse_PlayerLooseUI);
		size_t sz = estrLength(&pPuppet->pchLooseUI_Obsolete) * 2 + 1;
		char *pchLooseUI = calloc(1, sz);
		decodeBase64String(pPuppet->pchLooseUI_Obsolete, estrLength(&pPuppet->pchLooseUI_Obsolete), pchLooseUI, sz);
		if (ParserReadText(pchLooseUI, parse_PlayerLooseUI, pLooseUI, PARSER_NOERRORFSONPARSE))
		{
			//HACK: convert class type to region type
			WorldRegionType eWorldRegion = pPuppet->eType == 1 ? WRT_Space : WRT_Ground; 
			ControlSchemeRegionType eSchemeRegion = getSchemeRegionTypeFromRegionType(eWorldRegion);
			bSuccess = gslHUDOptions_FixupInternal(pEnt, pLooseUI, eSchemeRegion);
		}
		StructDestroy(parse_PlayerLooseUI, pLooseUI);
		free(pchLooseUI);
	}
	return bSuccess;
}

void gslHUDOptions_Fixup(SA_PARAM_NN_VALID Entity* pEnt)
{
	bool bDirty = false;

	PERFINFO_AUTO_START_FUNC();

	if (pEnt->pPlayer && pEnt->pSaved && pEnt->pSaved->pPuppetMaster)
	{
		S32 i;
		PuppetMaster* pPuppetMaster = pEnt->pSaved->pPuppetMaster;
		for (i = eaSize(&pPuppetMaster->ppPuppets)-1; i >= 0; i--)
		{
			PuppetEntity* pPuppet = pPuppetMaster->ppPuppets[i];

			if (pPuppet->eState == PUPPETSTATE_ACTIVE)
			{
				bDirty |= gslHUDOptions_FixupPuppet(pEnt, pPuppet);
			}
			if (pPuppet->pchLooseUI_Obsolete)
			{
				estrDestroy(&pPuppet->pchLooseUI_Obsolete);
				bDirty = true;
			}
		}
	}
	else if (pEnt->pPlayer)
	{
		PlayerLooseUI* pLooseUI = pEnt->pPlayer->pUI->pLooseUI;
		ControlSchemeRegionType eSchemeRegion = getSchemeRegionTypeFromRegionType(entGetWorldRegionTypeOfEnt(pEnt));
		if (gslHUDOptions_FixupInternal(pEnt, pLooseUI, eSchemeRegion))
		{
			bDirty = true;
		}
	}

	if (bDirty)
	{
		PlayerLooseUI* pLooseUI = pEnt->pPlayer->pUI->pLooseUI;
		StructDestroySafe(parse_PlayerShowOverhead, &pLooseUI->pShowOverhead_Obsolete);
		pLooseUI->eNotifyAudioMode_Obsolete = PlayerNotifyAudioMode_Unset;
		pLooseUI->uiTrayMode_Obsolete = 0;
		pLooseUI->uiPowerLevelsMode_Obsolete = 0;
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}

	PERFINFO_AUTO_STOP();
}

// Sets player titles flag.
AUTO_COMMAND ACMD_NAME("SetHudShowPlayerTitles") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD;
void gslHUDOptions_SetHudShowPlayerTitles(Entity* pEnt, bool bShowPlayerTitles) 
{
	if (pEnt && pEnt->pPlayer)
	{
		PlayerHUDOptions* pHUDOptions = gslHUDOptions_EntGetCurrentOptions(pEnt);
		if (pHUDOptions->ShowOverhead.bShowPlayerTitles != bShowPlayerTitles)
		{
			pHUDOptions->ShowOverhead.bShowPlayerTitles = bShowPlayerTitles;
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

// Sets player reticle display.
AUTO_COMMAND ACMD_NAME("SetHudShowReticlesAs") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD;
void gslHUDOptions_SetHudShowReticlesAs(Entity* pEnt, OverHeadReticleFlags eShowReticlesAs) 
{
	if (pEnt && pEnt->pPlayer)
	{
		PlayerHUDOptions* pHUDOptions = gslHUDOptions_EntGetCurrentOptions(pEnt);

		if (pHUDOptions->ShowOverhead.eShowReticlesAs != eShowReticlesAs)
		{
			pHUDOptions->ShowOverhead.eShowReticlesAs = eShowReticlesAs;
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

// Sets player interaction icons flag.
AUTO_COMMAND ACMD_NAME("SetHudShowInteractionIcons") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD;
void gslHUDOptions_SetHudShowInteractionIcons(Entity* pEnt, bool bShowInteractionIcons) 
{
	if (pEnt && pEnt->pPlayer)
	{
		PlayerHUDOptions* pHUDOptions = gslHUDOptions_EntGetCurrentOptions(pEnt);

		if (pHUDOptions->ShowOverhead.bShowInteractionIcons != bShowInteractionIcons)
		{
			pHUDOptions->ShowOverhead.bShowInteractionIcons = bShowInteractionIcons;
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

// Sets player damage floaters flag.
AUTO_COMMAND ACMD_NAME("SetHudShowDamageFloaters") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD;
void gslHUDOptions_SetHudShowDamageFloaters(Entity* pEnt, 
											bool bShowDamageFloaters, 
											bool bShowPetDamageFloaters, 
											bool bShowUnrelatedDamageFloaters) 
{
	if (pEnt && pEnt->pPlayer)
	{
		PlayerHUDOptions* pHUDOptions = gslHUDOptions_EntGetCurrentOptions(pEnt);

		if (	pHUDOptions->ShowOverhead.bShowDamageFloaters != bShowDamageFloaters
			||	pHUDOptions->ShowOverhead.bShowPetDamageFloaters != bShowPetDamageFloaters
			||	pHUDOptions->ShowOverhead.bShowUnrelatedDamageFloaters != bShowUnrelatedDamageFloaters)
		{
			pHUDOptions->ShowOverhead.bShowDamageFloaters = bShowDamageFloaters;
			pHUDOptions->ShowOverhead.bShowPetDamageFloaters = bShowPetDamageFloaters;
			pHUDOptions->ShowOverhead.bShowUnrelatedDamageFloaters = bShowUnrelatedDamageFloaters;
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

// Set the notification sound type for the HUD
AUTO_COMMAND ACMD_NAME("SetNotifyAudioMode") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void gslHUDOptions_SetNotifyAudioMode(Entity* pEnt, S32 eMode)
{
	if (pEnt && pEnt->pPlayer)
	{
		PlayerHUDOptions* pHUDOptions = gslHUDOptions_EntGetCurrentOptions(pEnt);

		if (pHUDOptions->eNotifyAudioMode != eMode)
		{
			pHUDOptions->eNotifyAudioMode = eMode;
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

// Set the tray mode
AUTO_COMMAND ACMD_NAME("SetTrayMode") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void gslHUDOptions_SetTrayMode(Entity* pEnt, U32 uiMode)
{
	if (pEnt && pEnt->pPlayer)
	{
		PlayerHUDOptions* pHUDOptions = gslHUDOptions_EntGetCurrentOptions(pEnt);
		
		if (pHUDOptions->uiTrayMode != uiMode)
		{
			pHUDOptions->uiTrayMode = uiMode;
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

// Set the power levels mode
AUTO_COMMAND ACMD_NAME("SetPowerLevelsMode") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void gslHUDOptions_SetPowerLevelsMode(Entity* pEnt, U32 uiMode)
{
	if (pEnt && pEnt->pPlayer)
	{
		PlayerHUDOptions* pHUDOptions = gslHUDOptions_EntGetCurrentOptions(pEnt);

		if (pHUDOptions->uiPowerLevelsMode != uiMode)
		{
			pHUDOptions->uiPowerLevelsMode = uiMode;
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

// Sets overhead flags based on UI options.
AUTO_COMMAND ACMD_NAME("SetHudOptionsField") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslHUDOptions_SetHudOptionsField(Entity* pEnt, PlayerHUDOptions* pHUDOptions) 
{
	if (!pHUDOptions)
	{
		return;
	}
	else
	{
		bool bDirty = false;
		S32 i = gslHUDOptions_EntGetIndexForSchemeRegion(pEnt, pHUDOptions->eRegion);
		
		if (i >= 0)
		{
			PlayerHUDOptions* pCurOptions = pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions[i];

			if (StructCompare(parse_PlayerHUDOptions, pHUDOptions, pCurOptions, 0, 0, 0))
			{
				StructCopyAll(parse_PlayerHUDOptions, pHUDOptions, pCurOptions);
				bDirty = true;
			}
		}
		else if (pEnt && pEnt->pPlayer)
		{
			PlayerHUDOptions* pNewOptions = pNewOptions = StructClone(parse_PlayerHUDOptions, pHUDOptions);
			eaPush(&pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions, pNewOptions);
			bDirty = true;
		}

		if (bDirty)
		{
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

AUTO_COMMAND ACMD_NAME("ResetHudOptions") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslHUDOptions_ResetHudOptions(Entity* pEnt, S32 eSchemeRegion) 
{
	S32 i = gslHUDOptions_EntGetIndexForSchemeRegion(pEnt, eSchemeRegion);

	if (i >= 0 && pEnt && pEnt->pPlayer)
	{	
		StructDestroy(parse_PlayerHUDOptions, eaRemoveFast(&pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions, i));

		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}