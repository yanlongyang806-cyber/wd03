#include "StringCache.h"
#include "textparser.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "ESet.h"
#include "Expression.h"
#include "GameStringFormat.h"
#include "StringUtil.h"
#include "sysutil.h"

#include "GameClientLib.h"
#include "inputLib.h"
#include "inputData.h"
#include "inputKeyBind.h"
#include "gclControlScheme.h"
#include "ControlScheme.h"

#include "Entity.h"
#include "gclEntity.h"
#include "gclKeyBind.h"
#include "gclOptions.h"
#include "Player.h"
#include "RegionRules.h"
#include "UIGen.h"
#include "WorldLibEnums.h"

#include "Entity_h_ast.h"
#include "Player_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "ControlScheme_h_ast.h"
#include "gclKeyBind_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static KeyBindProfile s_Profile = {"EntityBinds", __FILE__, NULL, true, true, NULL, NULL, InputBindPriorityUser};

static EntityBindCommands s_BindCommands;
static BindSpecialEntries s_BindSpecials;
static KeyboardLayouts s_Keyboards;

static EntityBindEntry **s_eaBinds = NULL;
static KeyCommandEntry **s_eaKeys = NULL;
static bool s_BindsEnabled;
static U32 s_uiBindsChangedIndex = 0;

// used by my hack below
static char *s_estrGameplayKeys = NULL;
char const * _getKeyProfileToLoad()
{
	if (gConf.bKeybindsFromControlSchemes)
		return schemes_GetActiveStoredScheme()->pchKeyProfileToLoad;
	else
		return s_estrGameplayKeys;
}

static bool gclKeyBindCommandsShouldDisplay(EntityBindCommands *pCommands)
{
	const char* pchProfile = _getKeyProfileToLoad();
	ControlSchemeRegionType eRegions = gclControlSchemeGetCurrentSchemeRegionType();
	
	if (eRegions != kControlSchemeRegionType_None && 
		pCommands->eSchemeRegions != kControlSchemeRegionType_None &&
		(pCommands->eSchemeRegions & eRegions) == 0)
	{
		return false;
	}
	else if (pCommands->ppchProfiles && 
			 eaFindString(&pCommands->ppchProfiles, pchProfile) < 0)
	{
		return false;
	}
	return true;
}

static S32 gclKeyBindCreateZeroBindEntries(EntityBindCommands *pCommands, S32 iStart)
{
	S32 i;

	// Reset hidden flag
	pCommands->bHidden = false;
	pCommands->iPrimaryBinds = 0;
	pCommands->iSecondaryBinds = 0;
	pCommands->iGamepadBinds = 0;

	if (GET_REF(pCommands->hDisplayName))
	{
		if (!gclKeyBindCommandsShouldDisplay(pCommands))
		{
			return iStart;
		}
		else
		{
			EntityBindEntry *pEntry = eaGetStruct(&s_eaBinds, parse_EntityBindEntry, iStart++);
			pEntry->pDef = NULL;
			COPY_HANDLE(pEntry->hCategoryName, pCommands->hDisplayName);
			SAFE_FREE(pEntry->Primary.pchKey);
			pEntry->Primary.iKey1 = pEntry->Primary.iKey2 = 0;
			SAFE_FREE(pEntry->Secondary.pchKey);
			pEntry->Secondary.iKey1 = pEntry->Secondary.iKey2 = 0;
			SAFE_FREE(pEntry->Gamepad.pchKey);
			pEntry->Gamepad.iKey1 = pEntry->Gamepad.iKey2 = 0;
			pEntry->pCategoryDef = pCommands;
			pEntry->bIsCategory = true;
		}
	}
	for (i = 0; i < eaSize(&pCommands->eaCommand); i++)
	{
		EntityBindEntry *pEntry = eaGetStruct(&s_eaBinds, parse_EntityBindEntry, iStart++);
		pEntry->pDef = pCommands->eaCommand[i];
		REMOVE_HANDLE(pEntry->hCategoryName);
		SAFE_FREE(pEntry->Primary.pchKey);
		pEntry->Primary.iKey1 = pEntry->Primary.iKey2 = 0;
		SAFE_FREE(pEntry->Secondary.pchKey);
		pEntry->Secondary.iKey1 = pEntry->Secondary.iKey2 = 0;
		SAFE_FREE(pEntry->Gamepad.pchKey);
		pEntry->Gamepad.iKey1 = pEntry->Gamepad.iKey2 = 0;
		pEntry->pCategoryDef = pCommands;
		pEntry->bIsCategory = false;
	}
	for (i = 0; i < eaSize(&pCommands->eaCategory); i++)
		iStart = gclKeyBindCreateZeroBindEntries(pCommands->eaCategory[i], iStart);
	return iStart;
}

static bool gclKeyBindSetKey(EntityBindCommandKey *pKey, KeyBind *pBind)
{
	if (!pKey->iKey1)
	{
		pKey->pchKey = StructAllocString(pBind->pchKey);
		pKey->iKey1 = pBind->iKey1;
		pKey->iKey2 = pBind->iKey2;
		return true;
	}
	return false;
}

void gclKeyBindFillBinds(void)
{
	KeyBindProfileIterator Iter;
	KeyBindProfile *pProfile;
	EntityBindEntry ***peaBinds = &s_eaBinds;
	KeyCommandEntry ***peaKeys = &s_eaKeys;
	Message *pLastCategory = NULL;
	ESet Set = NULL;
	bool bFoundEntBind = false;
	S32 i;
	S32 j;
	S32 iOld;
	S32 iKeys = 0;

	s_uiBindsChangedIndex++;
	i = gclKeyBindCreateZeroBindEntries(&s_BindCommands, 0);
	eaSetSizeStruct(peaBinds, parse_EntityBindEntry, i); 
	eSetCreate(&Set, eaSize(peaBinds));

	keybind_NewProfileIterator(&Iter);
	while ((pProfile = keybind_ProfileIteratorNext(&Iter)))
	{
		if (pProfile == &s_Profile)
			bFoundEntBind = true;
		if (!bFoundEntBind && !pProfile->bEntityBinds)
			continue;
		for (j = 0; j < eaSize(&pProfile->eaBinds); j++)
		{
			KeyBind *pBind = pProfile->eaBinds[j];
			EntityBindEntry *pBindEntry = NULL;
			S32 iKey1, iKey2;
			if (eSetFind(&Set, allocAddString(pBind->pchKey)))
				continue;
			if (pProfile->bJoystick)
			{
				for (i = 0; i < eaSize(peaBinds); i++)
				{
					EntityBindEntry *pEntry = (*peaBinds)[i];
					bool bHandled = false;
					if (!pEntry->pDef)
						continue;
					if (!stricmp(pBind->pchCommand, pEntry->pDef->pchCommand))
					{
						gclKeyBindSetKey(&pEntry->Gamepad, pBind);
						pBindEntry = pEntry;
						continue;
					}
					for (iOld = eaSize(&pEntry->pDef->eaOldCommands) - 1; iOld >= 0; iOld--)
					{
						if (!stricmp(pBind->pchCommand, pEntry->pDef->eaOldCommands[iOld]))
						{
							gclKeyBindSetKey(&pEntry->Gamepad, pBind);
							pBindEntry = pEntry;
							break;
						}
					}
				}
			}
			else
			{
				for (i = 0; i < eaSize(peaBinds); i++)
				{
					EntityBindEntry *pEntry = (*peaBinds)[i];
					bool bHandled = false;
					if (!pEntry->pDef)
						continue;
					if (!stricmp(pBind->pchCommand, pEntry->pDef->pchCommand))
					{
						if (pBind->bSecondary)
							gclKeyBindSetKey(&pEntry->Secondary, pBind);
						else
							gclKeyBindSetKey(&pEntry->Primary, pBind);
						pBindEntry = pEntry;
						continue;
					}
					for (iOld = eaSize(&pEntry->pDef->eaOldCommands) - 1; iOld >= 0; iOld--)
					{
						if (!stricmp(pBind->pchCommand, pEntry->pDef->eaOldCommands[iOld]))
						{
							if (pBind->bSecondary)
								gclKeyBindSetKey(&pEntry->Secondary, pBind);
							else
								gclKeyBindSetKey(&pEntry->Primary, pBind);
							pBindEntry = pEntry;
							break;
						}
					}
				}
				eSetAdd(&Set, allocAddString(pBind->pchKey));
			}

			keybind_ParseKeyString(pBind->pchKey, &iKey1, &iKey2);
			for (i = iKeys - 1; i >= 0; i--)
			{
				KeyCommandEntry *pKey = (*peaKeys)[i];
				if (pKey->iKey1 == iKey1 && pKey->iKey2 == iKey2 || pKey->iKey1 == iKey2 && pKey->iKey2 == iKey1)
					break;
			}
			if (i < 0 && pBind->pchCommand && *pBind->pchCommand)
			{
				KeyCommandEntry *pKey = eaGetStruct(peaKeys, parse_KeyCommandEntry, iKeys++);
				pKey->pchKey = allocAddString(pBind->pchKey);
				pKey->iKey1 = iKey1;
				pKey->iKey2 = iKey2;
				pKey->pCommand = SAFE_MEMBER(pBindEntry, pDef);
				if (!pKey->pchBindCommand || stricmp(pKey->pchBindCommand, pBind->pchCommand))
					StructCopyString(&pKey->pchBindCommand, pBind->pchCommand);
				pKey->bUsed = true;
			}
		}
	}
	eSetDestroy(&Set);
	eaSetSizeStruct(peaKeys, parse_KeyCommandEntry, iKeys);

	for (i = eaSize(peaBinds) - 1; i >= 0; i--)
	{
		EntityBindEntry *pEntry = (*peaBinds)[i];
		if (pEntry->pCategoryDef)
		{
			if (pEntry->bIsCategory)
			{
				if (pEntry->pCategoryDef->eVisible & kEntityBindVisibility_Never)
					pEntry->pCategoryDef->bHidden = true;
				if (pEntry->pCategoryDef->eVisible & (kEntityBindVisibility_AnyPrimaryBinds | kEntityBindVisibility_AnySecondaryBinds | kEntityBindVisibility_AnyGamepadBinds))
				{
					S32 iTotalBinds = 0;
					if (pEntry->pCategoryDef->eVisible & kEntityBindVisibility_AnyPrimaryBinds)
						iTotalBinds += pEntry->pCategoryDef->iPrimaryBinds;
					if (pEntry->pCategoryDef->eVisible & kEntityBindVisibility_AnySecondaryBinds)
						iTotalBinds += pEntry->pCategoryDef->iSecondaryBinds;
					if (pEntry->pCategoryDef->eVisible & kEntityBindVisibility_AnyGamepadBinds)
						iTotalBinds += pEntry->pCategoryDef->iGamepadBinds;
					pEntry->pCategoryDef->bHidden = pEntry->pCategoryDef->bHidden || iTotalBinds == 0;
				}
			}
			else
			{
				if (pEntry->Primary.pchKey && *pEntry->Primary.pchKey)
					pEntry->pCategoryDef->iPrimaryBinds++;
				if (pEntry->Secondary.pchKey && *pEntry->Secondary.pchKey)
					pEntry->pCategoryDef->iSecondaryBinds++;
				if (pEntry->Gamepad.pchKey && *pEntry->Gamepad.pchKey)
					pEntry->pCategoryDef->iGamepadBinds++;
			}
		}
	}
}

void gclKeyBindEnable(void)
{
	devassert(!s_BindsEnabled);
	s_BindsEnabled = 1;
	keybind_PushProfile(&s_Profile);
	gclKeyBindFillBinds();
	OptionCategoryAdd("KeyBinds");
}

void gclKeyBindDisable(void)
{
	s_BindsEnabled = 0;
	keybind_PopProfile(&s_Profile);
	eaDestroyStruct(&s_eaBinds, parse_EntityBindEntry);
	OptionCategoryDestroy("KeyBinds");
}

bool gclKeyBindIsEnabled(void)
{
	return s_BindsEnabled;
}

// Copy key bind states of eaBinds onto profile binds, 
// and cleanup any toggle commands that are no longer bound to the same keys as before.
void gclKeyBindProfileFixKeyBindStates(KeyBind** eaBinds)
{
	S32 i;
	for (i = eaSize(&eaBinds)-1; i >= 0; i--)
	{
		KeyBind* pSrcBind = eaBinds[i];

		if (pSrcBind->bPressed)
		{
			KeyBind* pDstBind = keybind_GetBindByKeys(pSrcBind->iKey1, pSrcBind->iKey2);
			if (pDstBind && !pDstBind->bPressed && stricmp(pSrcBind->pchCommand, pDstBind->pchCommand) == 0)
			{
				ANALYSIS_ASSUME(pDstBind);
				keybind_ExecuteKeyBind(pDstBind, true, GetTickCount());
			}
		}
	}
}

static EntityKeyBinds* gclKeyBinds_GetEntityBindsForProfile(Entity* pEnt, const char* pchProfileName)
{
	PlayerUI* pUI = SAFE_MEMBER2(pEnt, pPlayer, pUI);
	if (pUI)
	{
		S32 i;
		for (i = eaSize(&pUI->eaBindProfiles)-1; i >= 0; i--)
		{
			EntityKeyBinds* pBinds = pUI->eaBindProfiles[i];
			if (!pBinds->pchProfile || stricmp(pBinds->pchProfile, pchProfileName)==0)
			{
				return pBinds;
			}
		}
	}
	return NULL;
}

// Translate information stored on the Entity into a keybind profile.
void gclKeyBindFillFromEntity(Entity *pEnt)
{
	KeyBindProfile* pProfile = &s_Profile;

	eaDestroyStruct(&pProfile->eaBinds, parse_KeyBind);
	
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		S32 i;
		ControlSchemeRegionType eRegionFlag = gclControlSchemeGetCurrentSchemeRegionType();
		EntityKeyBinds* pBinds = gclKeyBinds_GetEntityBindsForProfile(pEnt, _getKeyProfileToLoad());

		if (pBinds)
		{
			for (i = 0; i < eaSize(&pBinds->eaBinds); i++)
			{
				EntityKeyBind* pBind = pBinds->eaBinds[i];
				const char* pchKey = pBind->pchKey;
				const char* pchCommand = pBind->pchCommand;
				S32 eRegions = pBind->eSchemeRegions;
				bool bSecondary = pBind->bSecondary;

				if (eRegions != kControlSchemeRegionType_None && (eRegions & eRegionFlag) == 0)
					continue;

				keybind_BindKeyInProfileEx(pProfile, pchKey, pchCommand, bSecondary);
			}
		}
	}
	gclKeyBindFillBinds();
}

void gclKeyBindClear(void)
{
	eaClearStruct(&s_Profile.eaBinds, parse_KeyBind);
	eaDestroyStruct(&s_eaBinds, parse_EntityBindEntry);
}

// If a bind in the entity profile matches a default bind, remove it
// from the entity profile.
static void gclKeyBindRemoveDefaults(KeyBindProfile* pProfile)
{
	KeyBindProfile **eaProfiles = NULL;
	S32 i;
	S32 j;
	S32 k;
	keybind_BindsBelow(pProfile, &eaProfiles);
	for (i = 0; i < eaSize(&eaProfiles); i++)
	{
		if (eaProfiles[i]->bJoystick)
			continue;
		for (j = 0; j < eaSize(&eaProfiles[i]->eaBinds); j++)
		{
			KeyBind *pOther = eaProfiles[i]->eaBinds[j];
			for (k = eaSize(&pProfile->eaBinds) - 1; k >= 0; k--)
			{
				KeyBind *pBind = pProfile->eaBinds[k];
				if (!stricmp(pBind->pchCommand, pOther->pchCommand)
					&& ((pBind->iKey1 == pOther->iKey1 && pBind->iKey2 == pOther->iKey2)
					|| (pBind->iKey1 == pOther->iKey2 && pBind->iKey2 == pOther->iKey1))
					&& (pBind->bSecondary == pOther->bSecondary))
				{
					StructDestroy(parse_KeyBind, eaRemove(&pProfile->eaBinds, k));
				}
			}
		}
	}
	eaDestroy(&eaProfiles);
}

void gclKeyBindSyncToServerEx(KeyBindProfile* pProfile)
{
	S32 i;
	EntityKeyBinds *pBinds = StructCreate(parse_EntityKeyBinds);
	const char* pchProfileName = _getKeyProfileToLoad();
	pBinds->pchProfile = StructAllocString(pchProfileName);
	gclKeyBindRemoveDefaults(pProfile);

	for (i = 0; i < eaSize(&pProfile->eaBinds); i++)
	{
		KeyBind* pBind = pProfile->eaBinds[i];
		const char *pchCommand = pBind->pchCommand;
		const char *pchKey = keybind_GetNames(pBind);
		bool bSecondary = pBind->bSecondary;

		if (pchKey)
		{
			EntityKeyBind *pEntBind = StructCreate(parse_EntityKeyBind);
			pEntBind->pchCommand = StructAllocString(pchCommand);
			pEntBind->pchKey = StructAllocString(pchKey);
			pEntBind->bSecondary = bSecondary;
			eaPush(&pBinds->eaBinds, pEntBind);
		}
	}
	ServerCmd_gslKeyBindSet(pBinds);
	StructDestroy(parse_EntityKeyBinds, pBinds);
	gclKeyBindFillBinds();
}

void gclKeyBindSyncToServer(void)
{
	gclKeyBindSyncToServerEx(&s_Profile);
}

void gclKeyBindLoad(const char *pchFilename)
{
	char achFullFilename[CRYPTIC_MAX_PATH];
	char achFolder[CRYPTIC_MAX_PATH];

	if (!pchFilename)
		pchFilename = "ent_" KEYBIND_DEFAULT_FILENAME;

	strcpy(achFullFilename, pchFilename);
	if (!isFullPath(achFullFilename))
	{
		if(isDevelopmentMode())
			sprintf(achFullFilename, "%s/%s", fileLocalDataDir(), pchFilename);
		else
			sprintf(achFullFilename, "%s/%s", getExecutableDir(achFolder), pchFilename);
	}

	keybind_LoadProfile(&s_Profile, achFullFilename);
	gclKeyBindSyncToServer();
}

void gclKeyBindSave(const char *pchFilename)
{
	char achFullFilename[CRYPTIC_MAX_PATH];

	if (!pchFilename)
		pchFilename = "ent_" KEYBIND_DEFAULT_FILENAME;

	strcpy(achFullFilename, pchFilename);
	if (!isFullPath(achFullFilename))
	{
		char dir[CRYPTIC_MAX_PATH];
		if (isDevelopmentMode())
			sprintf(achFullFilename, "%s/%s", fileLocalDataDir(), pchFilename);
		else
			sprintf(achFullFilename, "%s/%s", getExecutableDir(dir), pchFilename);
	}

	keybind_SaveProfile(&s_Profile, achFullFilename);
}

// This function is a HACK.  I have copied code from gclControlScheme.c to be run when we are not setting keybinds from control schemes
// It attempts to find an appropriate keybind by language instead.  It's a mess, and this whole file needs to be rethought and rewritten,
// along with the options code that uses translated message keys to build other message keys. [RMARR - 4/15/13]
void gclKeyBindEnterGameplay()
{
	if (!gConf.bKeybindsFromControlSchemes)
	{
		KeyBindProfile *pProfile;
		Entity *pEnt = entActivePlayerPtr();
		KeyBindProfile *pOldProfile = NULL;
		KeyBind **eaBinds = NULL;
		char const * pchProfile;
		char szLocalizedKeybinds[100];

		snprintf(szLocalizedKeybinds,99,"%s%s","GameDefault",locGetName(getCurrentLocale()));
		szLocalizedKeybinds[99] = 0;
		pchProfile = szLocalizedKeybinds;
		pProfile = keybind_FindProfile(szLocalizedKeybinds);
		if (!pProfile)
		{
			pchProfile = "GameDefaultEnglish";
			pProfile = keybind_FindProfile("GameDefaultEnglish");
		}

		if (pchProfile && s_estrGameplayKeys && !stricmp(pchProfile, s_estrGameplayKeys))
			return; // trying to set the same keybind profile

		keybind_GetBinds(&eaBinds, true);

		if (!s_estrGameplayKeys)
		{
			estrCreate(&s_estrGameplayKeys);
		}
		if (s_estrGameplayKeys && s_estrGameplayKeys[0])
		{
			pOldProfile = keybind_FindProfile(s_estrGameplayKeys);
			keybind_PopProfile(pOldProfile);
			estrClear(&s_estrGameplayKeys);
		}
	
		if (pchProfile && pchProfile[0])
		{
			// if we have valid profile for this scheme, check to see that it already isn't active
			if (pProfile && !keybind_IsProfileActive(pProfile))
			{
				estrCopy2(&s_estrGameplayKeys, pchProfile);
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
}

void gclKeyBindExitGameplay()
{
	if (!gConf.bKeybindsFromControlSchemes)
	{
		if (s_estrGameplayKeys && s_estrGameplayKeys[0])
		{
			KeyBindProfile * pOldProfile = keybind_FindProfile(s_estrGameplayKeys);
			keybind_PopProfile(pOldProfile);
			estrClear(&s_estrGameplayKeys);
		}
	}
}

// Load entity keybinds from ent_keybinds.txt.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_load) ACMD_CATEGORY(Standard);
void gclKeyBindLoadCommand(void)
{
	gclKeyBindLoad(NULL);
}

// Save entity keybinds to ent_keybinds.txt.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_save) ACMD_CATEGORY(Standard);
void gclKeyBindSaveCommand(void)
{
	gclKeyBindSave(NULL);
}

// Load entity keybinds from the given filename.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_load_file) ACMD_CATEGORY(Standard);
void gclKeyBindLoadFileCommand(const ACMD_SENTENCE pchFilename)
{
	gclKeyBindLoad(pchFilename);
}

// Save entity keybinds to the given filename.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_save_file) ACMD_CATEGORY(Standard);
void gclKeyBindSaveFileCommand(const ACMD_SENTENCE pchFilename)
{
	gclKeyBindSave(pchFilename);
}


//////////////////////////////////////////////////////////////////////////
// Disk Loading

AUTO_RUN;
void gclKeyBind_Register(void)
{
	ui_GenInitIntVar("BindPrimary", 0);
	ui_GenInitIntVar("BindSecondary", 1);
}

static void BindCommandReload(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading BindCommands... ");
	if (pchPath)
		fileWaitForExclusiveAccess(pchPath);
	StructDeInit(parse_EntityBindCommands, &s_BindCommands);
	ParserLoadFiles("ui", "BindCommands.def", "BindCommands.bin", 0, parse_EntityBindCommands, &s_BindCommands);
	loadend_printf("Done. (%d)", eaSize(&s_BindCommands.eaCommand));
	if (eaSize(&s_eaBinds))
		gclKeyBindFillBinds();
}

static void BindSpecialsReload(const char *pchPath, S32 iWhen)
{
	S32 i;
	loadstart_printf("Loading BindSpecials... ");
	if (pchPath)
		fileWaitForExclusiveAccess(pchPath);
	StructDeInit(parse_BindSpecialEntries, &s_BindSpecials);
	ParserLoadFiles("ui", "BindSpecials.def", "BindSpecials.bin", 0, parse_BindSpecialEntries, &s_BindSpecials);
	for (i = 0; i < eaSize(&s_BindSpecials.eaEntry); i++)
	{
		s_BindSpecials.eaEntry[i]->iKeyCode = keybind_GetKeyScancode(s_BindSpecials.eaEntry[i]->pchKey);
		if (!s_BindSpecials.eaEntry[i]->iKeyCode)
		{
			ErrorFilenamef("ui/BindSpecials.def", "Invalid key name %s.", s_BindSpecials.eaEntry[i]->pchKey);
		}
	}
	loadend_printf("Done. (%d)", eaSize(&s_BindSpecials.eaEntry));
}

static void KeyboardLayoutsReload(const char *pchPath, S32 iWhen)
{
	S32 i, j, k;
	KeyboardLayout **eaInheritedLayouts = NULL;
	loadstart_printf("Loading KeyboardLayouts... ");
	if (pchPath)
		fileWaitForExclusiveAccess(pchPath);
	eaDestroy(&s_Keyboards.eaListedKeyboards);
	StructDeInit(parse_KeyboardLayouts, &s_Keyboards);
	ParserLoadFiles(NULL, "ui/KeyboardLayouts.def", "KeyboardLayouts.bin", PARSER_OPTIONALFLAG, parse_KeyboardLayouts, &s_Keyboards);
	for (i = 0; i < eaSize(&s_Keyboards.eaKeyboards); i++)
	{
		KeyboardLayout *pLayout = s_Keyboards.eaKeyboards[i];
		for (j = 0; j < eaSize(&pLayout->eaKeys); j++)
		{
			pLayout->eaKeys[j]->iKeyCode = keybind_GetKeyScancode(pLayout->eaKeys[j]->pchKey);
			if (!pLayout->eaKeys[j]->iKeyCode)
			{
				ErrorFilenamef("ui/KeyboardLayouts.def", "%s: Invalid key name %s.", pLayout->pchName, pLayout->eaKeys[j]->pchKey);
			}
		}
		for (j = 0; j < eaSize(&pLayout->eaSubLayouts); j++)
		{
			KeyboardLayoutSublayout *pSubLayoutRef = pLayout->eaSubLayouts[j];
			KeyboardLayout *pSubLayout = eaIndexedGetUsingString(&s_Keyboards.eaKeyboards, pSubLayoutRef->pchSubLayout);
			if (!pSubLayout)
			{
				ErrorFilenamef("ui/KeyboardLayouts.def", "%s: Invalid keyboard layout %s.", pLayout->pchName, pSubLayoutRef->pchSubLayout);
			}
			else if (eaSize(&pSubLayout->eaSubLayouts) > 0)
			{
				ErrorFilenamef("ui/KeyboardLayouts.def", "%s: Recursive keyboard layout %s.", pLayout->pchName, pSubLayoutRef->pchSubLayout);
			}
			else
			{
				eaCopyStructs(&pSubLayout->eaKeys, &pSubLayoutRef->eaTransformedKeys, parse_KeyboardLayoutKey);
				for (k = 0; k < eaSize(&pSubLayoutRef->eaTransformedKeys); k++)
				{
					pSubLayoutRef->eaTransformedKeys[k]->iX += pSubLayoutRef->iX;
					pSubLayoutRef->eaTransformedKeys[k]->iY += pSubLayoutRef->iY;
				}
			}
		}
		if (GET_REF(pLayout->hDisplayName))
		{
			eaPush(&s_Keyboards.eaListedKeyboards, pLayout);
		}
	}
	loadend_printf("Done. (%d)", eaSize(&s_Keyboards.eaKeyboards));
	if (eaSize(&s_eaBinds))
		gclKeyBindFillBinds();
}

AUTO_STARTUP(BindCommands) ASTRT_DEPS(AS_Messages, AS_ControlSchemeRegions);
void gclKeyBindLoadCommandDefs(void)
{
	BindCommandReload(NULL, 0);
	BindSpecialsReload(NULL, 0);
	KeyboardLayoutsReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/BindCommands.def", BindCommandReload);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/BindSpecials.def", BindSpecialsReload);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/KeyboardLayouts.def", KeyboardLayoutsReload);
}

//////////////////////////////////////////////////////////////////////////
// Commands

// Bind a key to a command, and store it on your character.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind) ACMD_CATEGORY(Standard);
void gclKeyBindBind(const char *pchKey, ACMD_NAMELIST(pAllCmdNamesForAutoComplete) const ACMD_SENTENCE pchCommand)
{
	keybind_BindKeyInProfile(&s_Profile, pchKey, pchCommand);
	gclKeyBindSyncToServer();
}

// Unbind a key stored on your character.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(unbind) ACMD_CATEGORY(Standard);
void gclKeyBindUnbind(const char *pchKey)
{
	keybind_BindKeyInProfile(&s_Profile, pchKey, "");
	gclKeyBindSyncToServer();
}

// Unbind all keys for the current keybind profile
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(unbind_all) ACMD_CATEGORY(Standard);
void gclKeyBindUnbindAll(void)
{
	eaDestroyStruct(&s_Profile.eaBinds, parse_KeyBind);
	gclKeyBindSyncToServer();
}

//////////////////////////////////////////////////////////////////////////
// Expressions / UI Support

// Unbind a key stored on your character.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(Unbind);
void gclKeyBindExprUnbindEnt(ExprContext *pContext, const char *pchKey)
{
	gclKeyBindUnbind(pchKey);
}

// Unbind a key stored on your character, using the command name.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(UnbindCommand);
void gclKeyBindExprUnbindEntCommand(ExprContext *pContext, const char *pchCommand, bool bSecondary)
{
	bool bUnbound = false;
	S32 i, j;
	for (i = eaSize(&s_Profile.eaBinds) - 1; i >= 0; i--)
	{
		KeyBind *pBind = s_Profile.eaBinds[i];
		if (pBind->pchCommand && pBind->pchCommand[0] && !stricmp(pBind->pchCommand, pchCommand)
			&& ((pBind->bSecondary && bSecondary) || (!pBind->bSecondary && !bSecondary)))
		{
			keybind_BindKeyInProfile(&s_Profile, pBind->pchKey, "");
			bUnbound = true;
			break;
		}
	}

	if (!bUnbound)
	{
		KeyBindProfile **eaProfiles = NULL;
		keybind_BindsBelow(&s_Profile, &eaProfiles);

		for (i = 0; i < eaSize(&eaProfiles); i++)
		{
			KeyBindProfile *pProfile = eaProfiles[i];
			for (j = 0; j < eaSize(&pProfile->eaBinds); j++)
			{
				KeyBind *pBind = pProfile->eaBinds[j];
				if (pBind->pchCommand && pBind->pchCommand[0] && pBind->pchKey && !stricmp(pBind->pchCommand, pchCommand)
					&& ((pBind->bSecondary && bSecondary) || !(pBind->bSecondary || bSecondary)))
				{
					KeyBind *pCurrentBind = keybind_ProfileFindBindByKeyString(&s_Profile, pBind->pchKey);
					if (!pCurrentBind)
					{
						keybind_BindKeyInProfile(&s_Profile, pBind->pchKey, "");
						bUnbound = true;
						break;
					}
				}
			}
			if (bUnbound)
				break;
		}	

		eaDestroy(&eaProfiles);
	}

	if ( bUnbound )
		gclKeyBindSyncToServer();
}

// Bind a key to a command, and store it on your character.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(Bind);
void gclKeyBindExprBindEnt(ExprContext *pContext, const char *pchKey, const char *pchCommand, bool bSecondary)
{
	S32 i;
	gclKeyBindExprUnbindEntCommand(pContext, pchCommand, bSecondary);
	// If we're using the expression version of this rather than the command
	// version, that means we're in the UI, so we want binding to be "nice"
	// rather than efficient. That means changing a bind needs to not only
	// unbind the original one, but also block it in case it existed on a
	// different profile level.
	for (i = 0; i < eaSize(&s_eaBinds); i++)
	{
		EntityBindEntry *pBind = s_eaBinds[i];
		if (pBind->pDef && pBind->pDef->pchCommand && pBind->pDef->pchCommand[0] && !stricmp(pBind->pDef->pchCommand, pchCommand))
		{
			if (bSecondary && pBind->Secondary.pchKey)
				keybind_BindKeyInProfileEx(&s_Profile, pBind->Secondary.pchKey, "", bSecondary);
			else if (!bSecondary && pBind->Primary.pchKey)
				keybind_BindKeyInProfileEx(&s_Profile, pBind->Primary.pchKey, "", bSecondary);
		}
	}
	keybind_BindKeyInProfileEx(&s_Profile, pchKey, pchCommand, bSecondary);
	gclKeyBindSyncToServer();
}

// Get a list of bindable commands and their current state.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetBindCommands);
S32 gclKeyBindExprGenGetBindCommands(UIGen *pGen)
{
	EntityBindEntry*** peaEntries = ui_GenGetManagedListSafe(pGen, EntityBindEntry);
	S32 i, iCount = 0;

	for (i = 0; i < eaSize(&s_eaBinds); i++)
	{
		EntityBindEntry* pEntry = s_eaBinds[i];
		EntityBindEntry* pCopyEntry;

		if (pEntry->pCategoryDef && pEntry->pCategoryDef->bHidden)
			continue;

		pCopyEntry = eaGetStruct(peaEntries, parse_EntityBindEntry, iCount++);
		StructCopyAll(parse_EntityBindEntry, pEntry, pCopyEntry);
	}

	eaSetSizeStruct(peaEntries, parse_EntityBindEntry, iCount);
	ui_GenSetManagedListSafe(pGen, peaEntries, EntityBindEntry, true);
	return eaSize(peaEntries);
}

// NOTE: Adjacent categories with the same display name will be combined.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetBindCommandsForCurrentScheme);
S32 gclKeyBindExprGenGetBindCommandsForCurrentScheme(SA_PARAM_NN_VALID UIGen *pGen)
{
	EntityBindEntry*** peaEntries = ui_GenGetManagedListSafe(pGen, EntityBindEntry);
	EntityBindEntry* pCategoryEntry = NULL;
	S32 i, iCount = 0;

	for (i = 0; i < eaSize(&s_eaBinds); i++)
	{
		EntityBindEntry* pEntry = s_eaBinds[i];
		EntityBindEntry* pCopyEntry;

		if (pEntry->pCategoryDef && pEntry->pCategoryDef->bHidden)
			continue;

		if (pEntry->bIsCategory)
		{
			if (pCategoryEntry && GET_REF(pCategoryEntry->hCategoryName) == GET_REF(pEntry->hCategoryName))
				continue;

			pCategoryEntry = pEntry;
		}

		pCopyEntry = eaGetStruct(peaEntries, parse_EntityBindEntry, iCount++);
		StructCopyAll(parse_EntityBindEntry, pEntry, pCopyEntry);
	}
	
	eaSetSizeStruct(peaEntries, parse_EntityBindEntry, iCount);
	ui_GenSetManagedListSafe(pGen, peaEntries, EntityBindEntry, true);
	return iCount;
}

//Retrieve the "KeyBind" index for the first occurence of a specific command. 
//Use this to get keybind information in the below expression functions. 
//This makes access O(1) most of the time, instead of O(n) if you check to see if 
//your command matches the one at the old index before running this again.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FindKeyBindIndexByCommand);
S32 gclKeyBindExprFindKeyBindIndexByCommand(const char* pchCommand)
{
	S32 i;
	for ( i = 0; i < eaSize(&s_eaBinds); i++ )
	{
		if ( s_eaBinds[i]->pDef && stricmp(pchCommand,s_eaBinds[i]->pDef->pchCommand)==0 )
			return i;
	}
	return -1;
}

//Get the command by index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetKeyBindCommandByIndex);
const char* gclKeyBindExprGetKeyBindCommandByIndex(S32 iIndex)
{
	if ( iIndex >= 0 && iIndex < eaSize(&s_eaBinds) && s_eaBinds[iIndex]->pDef )
	{
		return s_eaBinds[iIndex]->pDef->pchCommand;
	}

	return "";
}

// A fast way to update the index for a keybind.
// Equivalent to the following snippet of expression code:
// if (not GetKeyBindCommandByIndex(Self.Var[Index].Int) = "MyCommand")
//    GenSetValue(Self, "Index", FindKeyBindIndexByCommand("MyCommand"));
// endif
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UpdateKeyBindIndexByCommand);
S32 gclKeyBindExprUpdateKeyBindIndexByCommand(S32 iIndex, const char *pchCommand)
{
	if ( iIndex >= 0 && iIndex < eaSize(&s_eaBinds) && s_eaBinds[iIndex]->pDef )
	{
		if (!stricmp(s_eaBinds[iIndex]->pDef->pchCommand, pchCommand))
			return iIndex;
	}

	return gclKeyBindExprFindKeyBindIndexByCommand(pchCommand);
}

//Get the primary key by index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPrimaryKeyBindByIndex);
const char* gclKeyBindExprGetPrimaryKeyBindByIndex(S32 iIndex)
{
	if ( iIndex >= 0 && iIndex < eaSize(&s_eaBinds) )
	{
		return s_eaBinds[iIndex]->Primary.pchKey;
	}

	return "";
}

//Get the secondary key by index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetSecondaryKeyBindByIndex);
const char* gclKeyBindExprGetSecondaryKeyBindByIndex(S32 iIndex)
{
	if ( iIndex >= 0 && iIndex < eaSize(&s_eaBinds) )
	{
		return s_eaBinds[iIndex]->Secondary.pchKey;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPrimaryKeyBindDisplayNameByIndex);
const char* gclKeyBindExprGetPrimaryKeyBindDisplayNameByIndex(S32 iIndex, bool bAbbreviated)
{
	if ( iIndex >= 0 && iIndex < eaSize(&s_eaBinds) )
	{
		S32 iKey1 = s_eaBinds[iIndex]->Primary.iKey1;
		S32 iKey2 = s_eaBinds[iIndex]->Primary.iKey2;

		return keybind_GetDisplayNameFromKeys(iKey1, iKey2, bAbbreviated);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetSecondaryKeyBindDisplayNameByIndex);
const char* gclKeyBindExprGetSecondaryKeyBindDisplayNameByIndex(S32 iIndex, bool bAbbreviated)
{
	if ( iIndex >= 0 && iIndex < eaSize(&s_eaBinds) )
	{
		S32 iKey1 = s_eaBinds[iIndex]->Secondary.iKey1;
		S32 iKey2 = s_eaBinds[iIndex]->Secondary.iKey2;

		return keybind_GetDisplayNameFromKeys(iKey1, iKey2, bAbbreviated);
	}
	return "";
}

// Get a list of bindable special keys.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetBindSpecials);
S32 gclKeyBindExprGenGetBindSpecials(UIGen *pGen)
{
	ui_GenSetList(pGen, &s_BindSpecials.eaEntry, parse_BindSpecialEntry);
	return eaSize(&s_BindSpecials.eaEntry);
}

static void gclBindEntKey(ExprContext *pContext, S32 iKey, const char *pchCommand, bool bSecondary)
{
	KeyBind Bind = {0};
	const char *pchKeyString;
	S32 iKey2 = 0;
	if (inpLevelPeek(INP_SHIFT) && !(iKey == INP_SHIFT || iKey == INP_LSHIFT || iKey == INP_RSHIFT))
	{
		iKey2 = INP_SHIFT;
	}
	else if (inpLevelPeek(INP_CONTROL) && !(iKey == INP_CONTROL || iKey == INP_LCONTROL || iKey == INP_RCONTROL))
	{
		iKey2 = INP_CONTROL;
	}
	else if (inpLevelPeek(INP_ALT) && !(iKey == INP_ALT || iKey == INP_LALT || iKey == INP_RALT))
	{
		iKey2 = INP_ALT;
	}
	// Shift/Ctrl/Alt modifiers start at 0x800, and are typically written first.
	// They should be placed in the first key, however, there is no guarantee the
	// key profile data will obey this particular pattern.
	Bind.iKey1 = max(iKey, iKey2);
	Bind.iKey2 = min(iKey, iKey2);
	if (Bind.iKey1 == 0)
	{
		Bind.iKey1 = Bind.iKey2;
		Bind.iKey2 = 0;
	}
	pchKeyString = keybind_GetNames(&Bind);
	gclKeyBindExprBindEnt(pContext, pchKeyString, pchCommand, bSecondary);
}

// Bind a command using a key integer.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(BindKey);
bool gclKeyBindExprBindEntKey(ExprContext *pContext, S32 iKey, const char *pchCommand, bool bSecondary)
{
	gclBindEntKey(pContext, iKey, pchCommand, bSecondary);
	return true;
}

static char *s_pchCommand;
static bool s_bSecondary;
static bool s_bTrayBindMode = 0;

// Bind a command using a key integer.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(TrayBindKey);
bool gclKeyBindExprTrayBindEntKey(ExprContext *pContext, S32 iKey)
{
	if (s_pchCommand && s_pchCommand[0])
	{
		gclBindEntKey(pContext, iKey, s_pchCommand, false);
		return true;
	}
	return false;
}


static void BindCapture(S32 iKey)
{
	// FIXME: This was originally going to be a way to capture mouse events for easier
	// binding, but the UI eats all mouse events, so for now we'll do it a different way.
	// Still, it would be nicer to get this working.
	//gclKeyBindExprBindEntKey(NULL, iKey, s_pchCommand, s_bSecondary);
}

static KeyBindProfile s_Capture = {"CaptureKeys", __FILE__, NULL, false, true, BindCapture, NULL, InputBindPriorityUI};

// Enter bind capture mode, where the next key pressed will cause a rebind.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(BindCaptureModeEnter);
void gclKeyBindExprBindCaptureModeEnter(ExprContext *pContext, const char *pchCommand, bool bSecondary)
{
	keybind_PushProfile(&s_Capture);
	SAFE_FREE(s_pchCommand);
	s_pchCommand = StructAllocString(pchCommand);
	s_bSecondary = bSecondary;
}

// Exit bind capture mode.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(BindCaptureModeExit);
void gclKeyBindExprBindCaptureModeExit(ExprContext *pContext)
{
	keybind_PopProfile(&s_Capture);
	SAFE_FREE(s_pchCommand);
}


// Enter tray-bind capture mode, where pressed keys will bind the tray elem that has registered itself (in response to a mouseover event.)
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(TrayBindCaptureModeEnter);
void gclKeyBindExprTrayBindCaptureModeEnter(ExprContext *pContext)
{
	keybind_PushProfile(&s_Capture);
	s_bTrayBindMode = 1;
}

// Exit tray-bind capture mode.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(TrayBindCaptureModeExit);
void gclKeyBindExprTrayBindCaptureModeExit(ExprContext *pContext)
{
	keybind_PopProfile(&s_Capture);
	SAFE_FREE(s_pchCommand);
	s_bTrayBindMode = 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(TrayBindSetCommand);
void gclTrayBindingModeSetCommand(ExprContext *pContext, const char *pchCommand)
{
	SAFE_FREE(s_pchCommand);
	s_pchCommand = StructAllocString(pchCommand);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(TrayBindGetCommand);
const char* gclTrayBindingModeGetCommand(ExprContext *pContext)
{
	if (s_pchCommand) return s_pchCommand;
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(TrayBindModeIsActive);
int gclIsTrayBindingModeActive(ExprContext *pContext)
{
	return s_bTrayBindMode;
}

// See MessageFormatEntityBindEntry
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatEntityBindEntry);
const char *gclKeyBindExprFormatEntityBindEntry(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, SA_PARAM_OP_VALID EntityBindEntry *pEntry);

// Format a EntityBindEntry structure; available fields are:
// * Bind.Primary: Primary bind key string.
// * Bind.Secondary: Secondary bind key string.
// * Bind.Command: Raw command string.
// * Bind.Name: Localized bind display name.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatEntityBindEntry);
const char *gclKeyBindExprFormatEntityBindEntry(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, SA_PARAM_OP_VALID EntityBindEntry *pEntry)
{
	static char *s_pch;
	if (!pEntry)
		return "";
	FormatGameMessageKey(&s_pch, pchMessageKey,
		STRFMT_STRING("Bind.Primary", pEntry->Primary.pchKey),
		STRFMT_STRING("Bind.Secondary", pEntry->Secondary.pchKey),
		STRFMT_STRING("Bind.Command", pEntry->pDef->pchCommand),
		STRFMT_STRING("Bind.Name", TranslateMessageRef(pEntry->pDef->hDisplayName)),
		STRFMT_STRING("Bind", TranslateMessageRef(pEntry->pDef->hDisplayName)),
		STRFMT_STRING("Value.Primary", pEntry->Primary.pchKey),
		STRFMT_STRING("Value.Secondary", pEntry->Secondary.pchKey),
		STRFMT_STRING("Value.Command", pEntry->pDef->pchCommand),
		STRFMT_STRING("Value.Name", TranslateMessageRef(pEntry->pDef->hDisplayName)),
		STRFMT_STRING("Value", TranslateMessageRef(pEntry->pDef->hDisplayName)),
		STRFMT_END);
	return s_pch ? s_pch : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("keybind_GetOtherGamepadKeyByIndex");
int keybind_GetOtherGamepadKeyByIndex(int iIndex, int iKey)
{
	if (eaGet(&s_eaBinds, iIndex))
	{
		S32 iKey1 = s_eaBinds[iIndex]->Gamepad.iKey1;
		S32 iKey2 = s_eaBinds[iIndex]->Gamepad.iKey2;

		if(iKey1 == iKey)
			return iKey2;
		else if(iKey2 == iKey)
			return iKey1;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetKeyboardLayouts");
void gclKeyBindExprGetKeyboardLayouts(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetListSafe(pGen, &s_Keyboards.eaListedKeyboards, KeyboardLayout);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("KeyLayoutWidth");
S32 gclKeyBindExprKeyLayoutWidth(const char *pchKeyboardLayout)
{
	KeyboardLayout *pLayout = eaIndexedGetUsingString(&s_Keyboards.eaKeyboards, pchKeyboardLayout);
	if (pLayout)
	{
		S32 iWidth = pLayout->iWidth;
		S32 i;
		if (!iWidth)
		{
			for (i = eaSize(&pLayout->eaSubLayouts) - 1; i >= 0; i--)
			{
				KeyboardLayout *pSubLayout = eaIndexedGetUsingString(&s_Keyboards.eaKeyboards, pLayout->eaSubLayouts[i]->pchSubLayout);
				iWidth = MAX(iWidth, pSubLayout->iWidth + pLayout->eaSubLayouts[i]->iX);
			}
		}
		return iWidth;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("KeyLayoutHeight");
S32 gclKeyBindExprKeyLayoutHeight(const char *pchKeyboardLayout)
{
	KeyboardLayout *pLayout = eaIndexedGetUsingString(&s_Keyboards.eaKeyboards, pchKeyboardLayout);
	if (pLayout)
	{
		S32 iHeight = pLayout->iHeight;
		S32 i;
		if (!iHeight)
		{
			for (i = eaSize(&pLayout->eaSubLayouts) - 1; i >= 0; i--)
			{
				KeyboardLayout *pSubLayout = eaIndexedGetUsingString(&s_Keyboards.eaKeyboards, pLayout->eaSubLayouts[i]->pchSubLayout);
				iHeight = MAX(iHeight, pSubLayout->iHeight + pLayout->eaSubLayouts[i]->iY);
			}
		}
		return iHeight;
	}
	return 0;
}

__forceinline S32 gclKeyBindNormalizeKey(S32 iKey)
{
	switch (iKey)
	{
	case INP_LSHIFT:
	case INP_RSHIFT:
		return INP_SHIFT;
	case INP_LCONTROL:
	case INP_RCONTROL:
		return INP_CONTROL;
	case INP_LALT:
	case INP_RALT:
		return INP_ALT;
	}
	return iKey;
}

__forceinline bool gclKeyBindKeyCommandMatchKeys(KeyCommandEntry *pCommandKey, S32 iModifierKey, S32 iKey)
{
	if (iModifierKey == 0 && pCommandKey->iKey2 == 0)
	{
		if (pCommandKey->iKey1 == iKey)
			return true;
		else if (gclKeyBindNormalizeKey(pCommandKey->iKey1) == iKey)
			return true;
		else if (pCommandKey->iKey1 == gclKeyBindNormalizeKey(iKey))
			return true;
	}
	else if (iModifierKey != 0 && pCommandKey->iKey2 != 0)
	{
		// Normalize order of keys
		S32 iKey1 = MIN(pCommandKey->iKey1, pCommandKey->iKey2);
		S32 iKey2 = MAX(pCommandKey->iKey1, pCommandKey->iKey2);
		S32 iKeyM = MIN(iModifierKey, iKey);
		S32 iKeyK = MAX(iModifierKey, iKey);
		S32 nKey1, nKey2, nKeyM, nKeyK;
		S32 k, j;

		if (iKey1 == iKeyM && iKey2 == iKeyK)
			return true;

		k = gclKeyBindNormalizeKey(pCommandKey->iKey1);
		j = gclKeyBindNormalizeKey(pCommandKey->iKey2);
		nKey1 = MIN(k, j);
		nKey2 = MAX(k, j);
		k = gclKeyBindNormalizeKey(iModifierKey);
		j = gclKeyBindNormalizeKey(iKey);
		nKeyM = MIN(k, j);
		nKeyK = MAX(k, j);

		// If either low key matches, and either high key matches
		// Then the keybinds are equivalent
		if ((nKey1 == iKeyM || iKey1 == nKeyM) && (nKey2 == iKeyK || iKey2 == nKeyK))
			return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetKeyLayout");
void gclKeyBindExprGetKeyLayout(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchKeyboardLayout, const char *pchOtherKey)
{
	static KeyboardLayoutKey **s_eaKeyLayoutDefs;
	KeyboardLayout *pLayout = eaIndexedGetUsingString(&s_Keyboards.eaKeyboards, pchKeyboardLayout);
	KeyCommandEntry ***peaKeyCommands = ui_GenGetManagedListSafe(pGen, KeyCommandEntry);
	S32 i, iKey;
	S32 iOtherKey;

	if (pLayout)
	{
		iOtherKey = keybind_GetKeyScancode(pchOtherKey);
		if (pchOtherKey && *pchOtherKey && !iOtherKey)
		{
			ErrorFilenamef(exprContextGetBlameFile(pContext), "GenGetKeyLayout: Invalid key name %s.", pchOtherKey);
		}

		eaPushEArray(&s_eaKeyLayoutDefs, &pLayout->eaKeys);
		for (i = 0; i < eaSize(&pLayout->eaSubLayouts); i++)
			eaPushEArray(&s_eaKeyLayoutDefs, &pLayout->eaSubLayouts[i]->eaTransformedKeys);

		eaSetSizeStruct(peaKeyCommands, parse_KeyCommandEntry, eaSize(&s_eaKeyLayoutDefs));

		for (i = 0; i < eaSize(&s_eaKeyLayoutDefs); ++i)
		{
			KeyboardLayoutKey *pLayoutKey = s_eaKeyLayoutDefs[i];
			KeyCommandEntry *pKey = (*peaKeyCommands)[i];
			pKey->pDef = pLayoutKey;

			for (iKey = eaSize(&s_eaKeys) - 1; iKey >= 0; --iKey)
			{
				KeyCommandEntry *pCommandKey = s_eaKeys[iKey];
				if (gclKeyBindKeyCommandMatchKeys(pCommandKey, iOtherKey, pLayoutKey->iKeyCode))
				{
					pKey->bUsed = pCommandKey->bUsed;
					pKey->iKey1 = pCommandKey->iKey1;
					pKey->iKey2 = pCommandKey->iKey2;
					pKey->pchKey = pCommandKey->pchKey;
					pKey->pCommand = pCommandKey->pCommand;
					if (((!pKey->pchBindCommand) != (!pCommandKey->pchBindCommand)) || (pCommandKey->pchBindCommand && stricmp(pKey->pchBindCommand, pCommandKey->pchBindCommand)))
					{
						StructFreeStringSafe(&pKey->pchBindCommand);
						pKey->pchBindCommand = StructAllocString(pCommandKey->pchBindCommand);
					}
					break;
				}
			}

			if (iKey < 0)
			{
				KeyBind kb = {0};
				if (iOtherKey)
				{
					kb.iKey1 = iOtherKey;
					kb.iKey2 = pLayoutKey->iKeyCode;
				}
				else
				{
					kb.iKey1 = pLayoutKey->iKeyCode;
				}
				pKey->bUsed = false;
				pKey->iKey1 = kb.iKey1;
				pKey->iKey2 = kb.iKey2;
				pKey->pchKey = keybind_GetNames(&kb);
				pKey->pCommand = NULL;
				StructFreeStringSafe(&pKey->pchBindCommand);
			}
		}
	}
	else
	{
		eaClearStruct(peaKeyCommands, parse_KeyCommandEntry);
	}

	ui_GenSetManagedListSafe(pGen, peaKeyCommands, KeyCommandEntry, true);
	eaClear(&s_eaKeyLayoutDefs);
}

#include "gclKeyBind_h_ast.c"
