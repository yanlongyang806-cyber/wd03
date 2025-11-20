/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CombatDebugViewer.h"

#include "EditorManager.h"
#include "Color.h"
#include "gclEntity.h"
#include "inputMouse.h"
#include "Player.h"
#include "qsortG.h"

#include "AttribMod.h"
#include "AutoGen/AttribMod_h_ast.h"
#include "AttribModFragility.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CombatDebug.h"
#include "CombatDebug_h_ast.h"
#include "PowerModes.h"
#include "PowerActivation.h"
#include "AutoGen/PowerActivation_h_ast.h"

#include "gclCommandParse.h"

#include "AutoGen/CombatDebugViewer_c_ast.h"

#include "net.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

CombatDebug *g_pCombatDebugData = NULL;


static UIPane *s_Pane;
static bool s_bShowing = false;
static U32 s_uiHideUntil = 0;

static UIButton *s_pButtonMainMenu;
static UILabel *s_pLabelName;
static UIButton *s_pButtonSwitchEnt;
static UIButton *s_pButtonSwitchEntSelected;
static UILabel *s_pLabelStatus;

static UIButton *s_pTogglePowers;
static UIButton *s_pToggleAttribMods;
static UIButton *s_pToggleAttributes;
static UIButton *s_pTogglePowerModes;
static U32 s_uiShowPowers = 2;
static U32 s_uiShowAttribMods = 2;
static U32 s_uiShowAttributes = 2;
static U32 s_uiShowPowerModes = 1;

// Struct we fill in for character attributes
AUTO_STRUCT;
typedef struct CDAttribute
{
	AttribType offAttrib;
		// Offset of the attribute

	const char *pchName;
		// Name of the attribute
	
	F32 fBasic;
		// Basic value of the attribute

	F32 fStr;
		// Strength value of the attribute

	F32 fRes;
		// Resist value of the attribute

	F32 fChanged;
		// Set to indicate this value has changed since last time, decays
} CDAttribute;

// Struct we fill in for PowerModes
AUTO_STRUCT;
typedef struct CDPowerMode
{
	const char *cpchPowerMode;	AST(UNOWNED NAME(PowerMode))
		// Name of the PowerMode


	EntityRef erPersonal;
	char *pchPersonalName;			AST(NAME(PersonalName))
	char *pchPersonalDebugName;		AST(NAME(PersonalDebugName))
		// If personal, who it's with
} CDPowerMode;

static UIList *s_pListPowers;
static UIList *s_pListMods;
static UIList *s_pListAttribs;
static Power **s_ppPowers = NULL;
static AttribMod **s_ppMods = NULL;
static CDAttribute **s_ppAttribs = NULL;
static CDPowerMode **s_ppPowerModes = NULL;
static PowerActivation *s_pActivation;

static UIWindow *s_pWindowPowerModes = NULL;
static UIList *s_pListPowerModes = NULL;

static bool s_bShowPowers = true;

static UIMenu *s_pMenuMain;
static UIMenu *s_pMenuPowers;
static UIMenu *s_pMenuAttribMods;

static UISkin s_skinPane = {0};
static UISkin s_skinLabelMild = {0};
static UISkin s_skinLabelBold = {0};
static UISkin s_skinToggleGroup = {0};
static UISkin s_skinList = {0};

static EntityRef s_erDebugEntPrev = 0;
static bool s_bInvincible = 0;
static bool s_bInvulnerable = 0;
static bool s_bUnstoppable = 0;

static void MainMenuCloseViewerCB(UIMenuItem *pMenuItem, void *pUnused)
{
	globCmdParsef("combatDebugEnt off");
	s_uiHideUntil = timeSecondsSince2000() + 2;
	combatDebugView(false);
}

static void MainMenu(UIWidget *pWidget, void *pUnused)
{
	char *estr = NULL;
	char *estr2 = NULL;
	estrStackCreate(&estr);
	estrStackCreate(&estr2);
	
	ui_MenuClear(s_pMenuMain);
	ui_MenuAppendItem(s_pMenuMain,ui_MenuItemCreate("Close",UIMenuCallback,MainMenuCloseViewerCB,NULL,NULL));

	if(!g_pCombatDebugData)
	{
		estrDestroy(&estr2);
		estrDestroy(&estr);
		ui_MenuPopupAtCursor(s_pMenuMain);
		return;
	}
	ui_MenuAppendItem(s_pMenuMain,ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL));

	estrPrintf(&estr,"ec %d invincible 1",g_pCombatDebugData->erDebugEnt);
	ui_MenuAppendItem(s_pMenuMain,ui_MenuItemCreate("Set Invincible On",UIMenuCommand,NULL,estr,NULL));

	estrPrintf(&estr,"ec %d invincible 0",g_pCombatDebugData->erDebugEnt);
	ui_MenuAppendItem(s_pMenuMain,ui_MenuItemCreate("Set Invincible Off",UIMenuCommand,NULL,estr,NULL));
	
	estrPrintf(&estr,"ec %d invulnerable %d",g_pCombatDebugData->erDebugEnt,!s_bInvulnerable);
	estrPrintf(&estr2,"Set Invulnerable %s",s_bInvulnerable ? "Off" : "On");
	ui_MenuAppendItem(s_pMenuMain,ui_MenuItemCreate(estr2,UIMenuCommand,NULL,estr,NULL));

	estrPrintf(&estr,"ec %d unstoppable %d",g_pCombatDebugData->erDebugEnt,!s_bUnstoppable);
	estrPrintf(&estr2,"Set Unstoppable %s",s_bUnstoppable ? "Off" : "On");
	ui_MenuAppendItem(s_pMenuMain,ui_MenuItemCreate(estr2,UIMenuCommand,NULL,estr,NULL));

	estrDestroy(&estr2);
	estrDestroy(&estr);

	ui_MenuPopupAtCursor(s_pMenuMain);
}

static void SwitchDebugEnt(UIWidget *pWidget, void *pUnused)
{
	if(!stricmp(ui_WidgetGetText(pWidget),"Switch to Selected"))
	{
		globCmdParsef("combatDebugEnt selected");
	}
	else if(g_pCombatDebugData && s_erDebugEntPrev && s_erDebugEntPrev!=g_pCombatDebugData->erDebugEnt)
	{
		globCmdParsef("combatDebugEnt %d",s_erDebugEntPrev);
	}
	else
	{
		globCmdParsef("combatDebugEnt me");
	}
}

static void Toggle(UIWidget *pWidget, U32 *puiToggle)
{
	if(*puiToggle)
	{
		*puiToggle = 0;
	}
	else
	{
		*puiToggle = 2;
	}
}

static Power *GetPowerFromRow(int row)
{
	return row>=0 && row<eaSize(&s_ppPowers) ? s_ppPowers[row] : NULL;
}

static void ListMakePowerRechargeText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	Power *ppow = GetPowerFromRow(row);
	if(ppow) estrConcatf(output,"%.2f",ppow->fTimeRecharge);
	else estrConcatChar(output,'-');
}

static void ListMakePowerTimerText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	Power *ppow = GetPowerFromRow(row);
	if(ppow) estrConcatf(output,"%.2f",0.f);
	else estrConcatChar(output,'-');
}

static void ListMakePowerChargeText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	Power *ppow = GetPowerFromRow(row);
	PowerDef *pDef = GET_REF(ppow->hDef);
	F32 fTimeCharge = 0.0f;

	if(s_pActivation && GET_REF(s_pActivation->hdef) == pDef)
	{
		fTimeCharge = s_pActivation->fTimeCharged;
	}
	if(pDef && pDef->fTimeCharge == 0.0f) estrConcatf(output,"--");
	else if(ppow && pDef) estrConcatf(output,"%.2f",max(pDef->fTimeCharge - fTimeCharge,0.0f));
	else estrConcatChar(output,'-');
}

static void ListMakePowerDefText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	PowerDef *pdef = NULL;
	Power *ppow = GetPowerFromRow(row);
	if(ppow)
	{
		pdef = GET_REF(ppow->hDef);
	}

	if(pdef)
	{
		int i,col=0;

		FORALL_PARSETABLE(parse_PowerDef, i)
		{
			if (!stricmp(parse_PowerDef[i].name, (char*)userData))
			{
				col = i;
				break;
			}
		}

		if(col)
		{
			char buf[1024];
			if(TokenToSimpleString(parse_PowerDef, col, pdef, buf, 1024, WRITETEXTFLAG_PRETTYPRINT))
			{
				if(!stricmp("Name",ui_ListColumnGetTitle(column)))
				{
					if(ppow->pParentPower)
					{
						estrConcatChar(output,'-');
						ppow = ppow->pParentPower;
					}
				}
				estrConcatf(output,"%s",buf);
				return;
			}
		}
	}

	estrConcatf(output, "UNKNOWN");
}

static void PowerListContextCB(UIList *list, void *pUnused)
{
	Power *ppow = ui_ListGetSelectedObject(s_pListPowers);
	if(ppow)
	{
		ui_MenuPopupAtCursor(s_pMenuPowers);
	}
}

static void PowerMenuRechargeCB(UIMenuItem *pitem, void *pUnused)
{
	int id = -1;
	if(!stricmp(ui_MenuItemGetText(pitem),"Recharge"))
	{
		Power *ppow = ui_ListGetSelectedObject(s_pListPowers);
		if(ppow) id = ppow->uiID;
	}
	else
	{
		id = 0;
	}
	
	if(id>=0 && g_pCombatDebugData) globCmdParsef("ec %d recharge %d 0",g_pCombatDebugData->erDebugEnt,id);
}

static void PowerMenuEditCB(UIMenuItem *pitem, void *pUnused)
{
#ifndef NO_EDITORS
	Power *ppow = ui_ListGetSelectedObject(s_pListPowers);
	if(ppow)
	{
		PowerDef *pdef = GET_REF(ppow->hDef);
		if(pdef)
		{
			CommandEditMode(true);
			emOpenFileEx(pdef->pchName,"PowerDef");
		}
	}
#endif
}


static void AttribModListContextCB(UIList *list, void *pUnused)
{
	AttribMod *pmod = ui_ListGetSelectedObject(s_pListMods);
	if(pmod)
	{
		ui_MenuPopupAtCursor(s_pMenuAttribMods);
	}
}

static void AttribModMenuEditCB(UIMenuItem *pitem, void *pUnused)
{
#ifndef NO_EDITORS
	AttribMod *pmod = ui_ListGetSelectedObject(s_pListMods);
	AttribModDef *pmoddef = mod_GetDef(pmod);
	if(pmoddef)
	{
		PowerDef *pdef = pmoddef->pPowerDef;
		if(pdef)
		{
			CommandEditMode(true);
			emOpenFileEx(pdef->pchName,"PowerDef");
		}
	}
#endif
}

static void AttribModMenuExpireCB(UIMenuItem *pitem, void *pUnused)
{
#ifndef NO_EDITORS
	AttribMod *pmod = ui_ListGetSelectedObject(s_pListMods);
	if(pmod && g_pCombatDebugData)
		globCmdParsef("ec %d attribmodexpire %s %d %d",g_pCombatDebugData->erDebugEnt,REF_STRING_FROM_HANDLE(pmod->hPowerDef),pmod->uiDefIdx,pmod->uiApplyID);
#endif
}


static AttribMod *GetAttribModFromRow(int row)
{
	int iNumMods = s_ppMods ? eaSize(&s_ppMods) : 0;
	return s_ppMods && row>=0 && row<iNumMods ? s_ppMods[row] : NULL;
}

static PowerDef *GetAttribModPowerDefFromRow(int row)
{
	AttribMod *pmod = GetAttribModFromRow(row);
	return pmod ? GET_REF(pmod->hPowerDef) : NULL;
}

static AttribModDef *GetAttribModDefFromRow(int row)
{
	AttribMod *pmod = GetAttribModFromRow(row);
	PowerDef *pdef = pmod ? GET_REF(pmod->hPowerDef) : NULL;
	int s = pdef ? eaSize(&pdef->ppOrderedMods) : 0;
	return (s && (int)pmod->uiDefIdx < s) ? pdef->ppOrderedMods[pmod->uiDefIdx] : NULL;
}

static CDAttribute *GetAttributeFromRow(int row)
{
	static int s = 0;
	if(!s) s = eaSize(&s_ppAttribs);
	return (row>=0 && row<s) ? s_ppAttribs[row] : NULL;

}

static void ListMakeAttribModPowerDefText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	PowerDef *pdef = GetAttribModPowerDefFromRow(row);
	if(pdef)
	{
		int i,col=0;

		FORALL_PARSETABLE(parse_PowerDef, i)
		{
			if (!stricmp(parse_PowerDef[i].name, (char*)userData))
			{
				col = i;
				break;
			}
		}

		if(col)
		{
			char buf[1024];
			if(TokenToSimpleString(parse_PowerDef, col, pdef, buf, 1024, WRITETEXTFLAG_PRETTYPRINT))
			{
				estrConcatf(output,"%s",buf);
				return;
			}
		}
	}

	estrConcatf(output, "UNKNOWN");
}

static void ListMakeAttribModHealthText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AttribMod *pdef = GetAttribModFromRow(row);
	if(pdef && pdef->pFragility) estrConcatf(output,"%f",pdef->pFragility->fHealth);
	else	estrConcatf(output,"--");
}
static void ListMakeAttribModAttribText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AttribModDef *pdef = GetAttribModDefFromRow(row);
	if(pdef) estrConcatf(output,"%s",StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
	else estrConcatf(output,"UNKNOWN");
}

static void ListMakeAttribModAspectText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AttribModDef *pdef = GetAttribModDefFromRow(row);
	if(pdef) estrConcatf(output,"%s",StaticDefineIntRevLookup(AttribAspectEnum,pdef->offAspect));
	else estrConcatf(output,"UNKNOWN");
}

static void ListMakeAttribModMagnitudeText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AttribMod *pmod = GetAttribModFromRow(row);
	if(pmod) estrConcatf(output,"%.2f",pmod->fMagnitude);
	else estrConcatChar(output,'-');
}

static void ListMakeAttribModDurationText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AttribMod *pmod = GetAttribModFromRow(row);
	if(pmod) 
	{
		PowerDef *powerdef = GET_REF(pmod->hPowerDef);
		if(powerdef && powerdef->eType == kPowerType_Innate)
			estrConcatf(output,"Innate");
		else
			estrConcatf(output,"%.2f",pmod->fDuration);
	}
	else estrConcatChar(output,'-');
}

static void ListMakeAttributeAttribText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	CDAttribute *pAttr = GetAttributeFromRow(row);
	if(pAttr) estrConcatf(output,"%s",pAttr->pchName);
	else estrConcatf(output,"UNKNOWN");
}

static void ListMakeAttributeBasicText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	CDAttribute *pAttr = GetAttributeFromRow(row);
	if(pAttr) estrConcatf(output,"%.2f",pAttr->fBasic);
	else estrConcatChar(output,'-');
}

static void ListMakeAttributeStrText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	CDAttribute *pAttr = GetAttributeFromRow(row);
	if(pAttr) estrConcatf(output,"%.2f",pAttr->fStr);
	else estrConcatChar(output,'-');
}

static void ListMakeAttributeResText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	CDAttribute *pAttr = GetAttributeFromRow(row);
	if(pAttr) estrConcatf(output,"%.2f",pAttr->fRes);
	else estrConcatChar(output,'-');
}



// Utility functions for RefreshData
static void PushAndUnrollPower(Power *ppow, Power *ppowParent)
{
	int i;
	Power *ppowCopy = StructCreate(parse_Power);
	StructCopyFields(parse_Power,ppow,ppowCopy,0,0);
	if(ppowParent) ppowCopy->pParentPower = ppowParent;
	eaPush(&s_ppPowers,ppowCopy);
	for(i=0; i<eaSize(&ppow->ppSubPowers); i++)
	{
		PushAndUnrollPower(ppow->ppSubPowers[i],ppow);
	}
}

static int SortAttribMods(const AttribMod** a, const AttribMod** b)
{
	int r = (int)(*a)->uiApplyID - (int)(*b)->uiApplyID;

	// If only 1 is under 0, put that one at the bottom
	if(((*a)->fDuration < 0.0f) != ((*b)->fDuration < 0.0f))
	{
		if((*a)->fDuration < 0.0f)
			return 1;
		else
			return -1;
	}
	if(!r)
	{
		if(!(*a)->uiApplyID)
		{
			// No apply id for either of them, sort by name
			PowerDef *pdefa = GET_REF((*a)->hPowerDef);
			PowerDef *pdefb = GET_REF((*b)->hPowerDef);
			if(!pdefa && !pdefb)
			{
				const char *pchDefA = REF_STRING_FROM_HANDLE((*a)->hPowerDef);
				const char *pchDefB = REF_STRING_FROM_HANDLE((*b)->hPowerDef);
				if(!pchDefA || !pchDefB)
				{
					r=0;
				}
				else
					r = stricmp(pchDefA,pchDefB);
			}
			else if(!pdefa) r = 1;
			else if (!pdefb) r = -1;
			else r = stricmp(pdefa->pchName,pdefb->pchName);
		}
		if(!r) r = (int)(*a)->uiDefIdx - (int)(*b)->uiDefIdx;
	}
	return r;
}

static void CopyAttributes(CombatDebug *pDbg)
{
	int i;
	
	// Make the list if it doesn't exist
	if(!eaSize(&s_ppAttribs))
	{
		eaDestroy(&s_ppAttribs);
		for(i=0; i<NUM_NORMAL_ATTRIBS; i++)
		{
			CDAttribute *pAttr;
			int offAttrib = i*SIZE_OF_NORMAL_ATTRIB;
			const char *pchName = StaticDefineIntRevLookup(AttribTypeEnum,offAttrib);

			if(pchName)
			{
				pAttr = StructCreate(parse_CDAttribute);
				pAttr->pchName = pchName;
				pAttr->offAttrib = offAttrib;
				eaPush(&s_ppAttribs,pAttr);
			}
		}
	}

	for(i=0; i<eaSize(&s_ppAttribs); i++)
	{
		CDAttribute *pAttr = s_ppAttribs[i];
		if(pDbg->pattrBasic)
		{
			F32 f = *F32PTR_OF_ATTRIB(pDbg->pattrBasic,pAttr->offAttrib);
			if(f!=pAttr->fBasic) pAttr->fChanged = 1.0f;
			pAttr->fBasic = f;
		}
		if(pDbg->pattrStr)
		{
			F32 f = *F32PTR_OF_ATTRIB(pDbg->pattrStr,pAttr->offAttrib);
			if(f!=pAttr->fStr) pAttr->fChanged = 1.0f;
			pAttr->fStr = f;
		}
		if(pDbg->pattrRes)
		{
			F32 f = *F32PTR_OF_ATTRIB(pDbg->pattrRes,pAttr->offAttrib);
			if(f!=pAttr->fRes) pAttr->fChanged = 1.0f;
			pAttr->fRes = f;
		}

		if(pAttr->fChanged!=1.0f && pAttr->fChanged > 0.0f)
		{
			pAttr->fChanged -= 0.1f;
			if(pAttr->fChanged < 0.0f) pAttr->fChanged = 0.0f;
		}
	}
}

static void CopyPowerModes(CombatDebug *pDbg)
{
	int i;
	Entity *e = entFromEntityRefAnyPartition(pDbg->erDebugEnt);
	Character *p = e ? e->pChar : NULL;

	if(e)
	{
		char *pchTitle = estrStackCreateFromStr("Power Modes: ");
		estrConcatf(&pchTitle,"%s (%s %d)",entGetLocalName(e),ENTDEBUGNAME(e),entGetRef(e));
		ui_WindowSetTitle(s_pWindowPowerModes,pchTitle);
		estrDestroy(&pchTitle);
	}

	eaDestroyStruct(&s_ppPowerModes,parse_CDPowerMode);
	
	if(p && eaiSize(&p->piPowerModes))
	{
		for(i=0; i<eaiSize(&p->piPowerModes); i++)
		{
			CDPowerMode *pPowerMode = StructCreate(parse_CDPowerMode);
			pPowerMode->cpchPowerMode = StaticDefineIntRevLookup(PowerModeEnum,p->piPowerModes[i]);
			eaPush(&s_ppPowerModes,pPowerMode);
		}
	}

	for(i=eaSize(&pDbg->ppMods)-1; i>=0; i--)
	{
		AttribMod *pmod = g_pCombatDebugData->ppMods[i];
		if(pmod->erPersonal)
		{
			AttribModDef *pmoddef = mod_GetDef(pmod);
			if(pmoddef && pmoddef->offAttrib==kAttribType_PowerMode)
			{
				PowerModeParams *pParams = (PowerModeParams*)pmoddef->pParams;
				if(pParams)
				{
					Entity *ePersonal = entFromEntityRefAnyPartition(pmod->erPersonal);
					CDPowerMode *pPowerMode = StructCreate(parse_CDPowerMode);
					pPowerMode->cpchPowerMode = StaticDefineIntRevLookup(PowerModeEnum,pParams->iPowerMode);
					pPowerMode->erPersonal = pmod->erPersonal;
					pPowerMode->pchPersonalName = StructAllocString(ePersonal ? entGetLocalName(ePersonal) : "?");
					pPowerMode->pchPersonalDebugName = StructAllocString(ePersonal ? ENTDEBUGNAME(ePersonal) : "?");
					eaPush(&s_ppPowerModes,pPowerMode);
				}
			}
		}
	}
}



static void RefreshData(void)
{
	char *estr = NULL;
	F32 bx = 10;
	F32 by = 45;
	F32 x = bx;
	F32 y = by;
	F32 ny = 0;

	estrStackCreate(&estr);

	// Main menu button
	ui_WidgetSetPosition(UI_WIDGET(s_pButtonMainMenu),x,y);
	x += s_pButtonMainMenu->widget.width + 5;
	ny = MAX(ny,y+s_pButtonMainMenu->widget.height);

	if(g_pCombatDebugData)
	{
		int i,j;
		Entity *e = entFromEntityRefAnyPartition(g_pCombatDebugData->erDebugEnt);
		Character *p = e ? e->pChar : NULL;
		Entity *eSwitch = NULL;
		
		if(e!=entActivePlayerPtr())
		{
			s_erDebugEntPrev = g_pCombatDebugData->erDebugEnt;
		}
		else if(s_erDebugEntPrev)
		{
			eSwitch = entFromEntityRefAnyPartition(s_erDebugEntPrev);
		}

		// Fill out utility data
		if(p)
		{
			s_bInvulnerable = p->bInvulnerable;
			s_bUnstoppable = p->bUnstoppable;
		}

		// Copy all our powers and mods into the local lists
		eaDestroyStruct(&s_ppPowers,parse_Power);
		eaDestroyStruct(&s_ppMods,parse_AttribMod);
		for(i=0; i<eaSize(&g_pCombatDebugData->ppPowers); i++)
		{
			PushAndUnrollPower(g_pCombatDebugData->ppPowers[i],NULL);
		}
		j = MIN(100,eaSize(&g_pCombatDebugData->ppMods));
		for(i=0; i<j; i++)
		{
			AttribMod *pModCopy = StructCreate(parse_AttribMod);
			StructCopyFields(parse_AttribMod,g_pCombatDebugData->ppMods[i],pModCopy,0,0);
			eaPush(&s_ppMods,pModCopy);
		}
 		eaQSortG(s_ppMods,SortAttribMods);
		CopyAttributes(g_pCombatDebugData);

		if(s_pActivation)
		{
			StructDestroy(parse_PowerActivation,s_pActivation);
			s_pActivation = NULL;
		}
		if(g_pCombatDebugData->pactivation)
		{
			s_pActivation = StructCreate(parse_PowerActivation);
			StructCopyFields(parse_PowerActivation,g_pCombatDebugData->pactivation,s_pActivation,0,0);
		}

		// Name/EntRef label
		ui_WidgetSetPosition(UI_WIDGET(s_pLabelName),x,y);
		estrPrintf(&estr,"%s   :: EntRef: %d",e ? e->debugName : "UNKNOWN", g_pCombatDebugData->erDebugEnt);
		ui_LabelSetText(s_pLabelName,estr);
		x += s_pLabelName->widget.width + 50;
		ny = MAX(ny,y+s_pLabelName->widget.height);

		// Switch buttons
		ui_WidgetSetPosition(UI_WIDGET(s_pButtonSwitchEntSelected),x,y);
		x += s_pButtonSwitchEntSelected->widget.width + 5;
		ny = MAX(ny,y+s_pButtonSwitchEntSelected->widget.height);

		ui_WidgetSetPosition(UI_WIDGET(s_pButtonSwitchEnt),x,y);
		estrPrintf(&estr,"Switch %sto %s",eSwitch ? "back " : "", eSwitch ? eSwitch->debugName : "Me");
		ui_ButtonSetTextAndResize(s_pButtonSwitchEnt,estr);
		x += s_pButtonSwitchEnt->widget.width + 5;
		ny = MAX(ny,y+s_pButtonSwitchEnt->widget.height);

		x = bx;
		y = ny + 5;

		// Status label
		ui_WidgetSetPosition(UI_WIDGET(s_pLabelStatus),x,y);
		estrClear(&estr);
		if(p) estrPrintf(&estr,"Level %d %s; ",p->iLevelCombat,IS_HANDLE_ACTIVE(p->hClassTemporary)?REF_STRING_FROM_HANDLE(p->hClassTemporary):REF_STRING_FROM_HANDLE(p->hClass));
		estrConcatf(&estr,"Sleep %.2f %.2f; ",g_pCombatDebugData->fTimerSleep,g_pCombatDebugData->fTimeSlept);
		if(s_bInvulnerable) estrConcatf(&estr,"*Invulnerable* ");
		if(s_bUnstoppable) estrConcatf(&estr,"*Unstoppable* ");
		if(p && eaiSize(&p->piPowerModes))
		{
			estrConcatf(&estr,"Modes: ");
			for(i=0; i<eaiSize(&p->piPowerModes); i++)
			{
				estrConcatf(&estr,"%s ",StaticDefineIntRevLookup(PowerModeEnum,p->piPowerModes[i]));
			}
		}
		ui_LabelSetText(s_pLabelStatus,estr);
		x += s_pLabelStatus->widget.width + 50;
		ny = MAX(ny,y+s_pLabelStatus->widget.height);

		y = ny + 5;

		// PowerModes button
		ui_ButtonSetTextAndResize(s_pTogglePowerModes,s_uiShowPowerModes ? "Hide PowerModes" : "Show PowerModes");
		ui_WidgetSetPositionEx(UI_WIDGET(s_pTogglePowerModes),s_pTogglePowerModes->widget.width/-2.0f,y,0.5,0,UITopLeft);

		y += s_pTogglePowerModes->widget.height + 5;

		// If we're JUST refreshing data, don't update the show state of our child window
		if (s_bShowing)
		{
			// Slightly different behavior here, init/show/hide a different window
			if(s_uiShowPowerModes)
			{
				CopyPowerModes(g_pCombatDebugData);

				if(!ui_WindowIsVisible(s_pWindowPowerModes))
					ui_WindowShow(s_pWindowPowerModes);
			}
			else
			{
				if(ui_WindowIsVisible(s_pWindowPowerModes))
					ui_WindowHide(s_pWindowPowerModes);
			}
		}


		// Powers button and list
		ui_ButtonSetTextAndResize(s_pTogglePowers,s_uiShowPowers ? "Hide Powers" : "Show Powers");
		ui_WidgetSetPositionEx(UI_WIDGET(s_pTogglePowers),s_pTogglePowers->widget.width/-2.0f,y,0.15,0,UITopLeft);

		if(s_uiShowPowers)
		{
			S32 s;
			if(s_uiShowPowers>1)
			{
				ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pListPowers));
				s_uiShowPowers = 1;
			}
			ui_WidgetSetDimensions(UI_WIDGET(s_pListPowers),275,400);
			ui_WidgetSetPositionEx(UI_WIDGET(s_pListPowers),s_pListPowers->widget.width/-2.0f,y+30,0.15,0,UITopLeft);
			s = ui_ListGetSelectedRow(s_pListPowers);
			ui_ListClearSelected(s_pListPowers); // hack to make this not crash because the model has been blown away
			ui_ListSetModel(s_pListPowers,parse_Power,&s_ppPowers);
			ui_ListSetSelectedRow(s_pListPowers,s);
		}
		else
		{
			ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pListPowers));
		}


		// AttribMods button and list
		ui_ButtonSetTextAndResize(s_pToggleAttribMods,s_uiShowAttribMods ? "Hide AttribMods" : "Show AttribMods");
		ui_WidgetSetPositionEx(UI_WIDGET(s_pToggleAttribMods),s_pToggleAttribMods->widget.width/-2.0f,y,0.5,0,UITopLeft);

		if(s_uiShowAttribMods)
		{
			S32 s;
			if(s_uiShowAttribMods>1)
			{
				ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pListMods));
				s_uiShowAttribMods = 1;
			}
			ui_WidgetSetDimensions(UI_WIDGET(s_pListMods),325,400);
			ui_WidgetSetPositionEx(UI_WIDGET(s_pListMods),s_pListMods->widget.width/-2.0f,y+30,0.5,0,UITopLeft);
			s = ui_ListGetSelectedRow(s_pListMods);
			ui_ListClearSelected(s_pListMods); // hack to make this not crash because the model has been blown away
			ui_ListSetModel(s_pListMods,parse_AttribMod,&s_ppMods);
			ui_ListSetSelectedRow(s_pListMods,s);
		}
		else
		{
			ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pListMods));
		}



		ui_ButtonSetTextAndResize(s_pToggleAttributes,s_uiShowAttributes ? "Hide Attributes" : "Show Attributes");
		ui_WidgetSetPositionEx(UI_WIDGET(s_pToggleAttributes),s_pToggleAttributes->widget.width/-2.0f,y,0.15,0,UITopRight);

		if(s_uiShowAttributes)
		{
			S32 s;
			if(s_uiShowAttributes>1)
			{
				ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pListAttribs));
				s_uiShowAttributes = 1;
			}
			ui_WidgetSetDimensions(UI_WIDGET(s_pListAttribs),325,400);
			ui_WidgetSetPositionEx(UI_WIDGET(s_pListAttribs),s_pListAttribs->widget.width/-2.0f,y+30,0.15,0,UITopRight);
			s = ui_ListGetSelectedRow(s_pListAttribs);
			ui_ListClearSelected(s_pListAttribs); // hack to make this not crash because the model has been blown away
			ui_ListSetModel(s_pListAttribs,parse_CDAttribute,&s_ppAttribs);
			ui_ListSetSelectedRow(s_pListAttribs,s);
		}
		else
		{
			ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pListAttribs));
		}


	}

	estrDestroy(&estr);
}

// Custom tick function to not take input in the pane
static void PaneTick(UIPane *pane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pane);

	UI_TICK_EARLY(pane, true, false);
	UI_TICK_LATE(pane);
}



static void InitViewer(void)
{
	int bottom=g_ui_State.screenHeight;
	int right=g_ui_State.screenWidth;

	UIListColumn *pTempColumn = NULL;
	UIMenuItem *pTempMenuItem = NULL;

	UI_WIDGET(s_Pane)->tickF = PaneTick;
	UI_WIDGET(s_Pane)->uClickThrough = true;
	s_skinPane.background[0].a = 0x40;
	ui_WidgetSkin(UI_WIDGET(s_Pane),&s_skinPane);

	
	// Skins
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "CombatDebug_Bold", s_skinLabelBold.hNormal);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "CombatDebug_Mild", s_skinLabelMild.hNormal);

	SET_HANDLE_FROM_STRING(g_ui_FontDict, "CombatDebug_Bold", s_skinToggleGroup.hNormal);
	ui_SkinSetButton(&s_skinToggleGroup,colorFromRGBA(0x000000A0));

	SET_HANDLE_FROM_STRING(g_ui_FontDict, "CombatDebug_Normal", s_skinList.hNormal);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "CombatDebug_Normal", s_skinList.hWindowTitleFont);
	s_skinList.background[0] = colorFromRGBA(0x00000060);
	s_skinList.background[1] = colorFromRGBA(0x004040C0);

	// Menus
	s_pMenuMain = ui_MenuCreate(NULL);
	s_pMenuPowers = ui_MenuCreate(NULL);
	ui_MenuAppendItem(s_pMenuPowers,ui_MenuItemCreate("Recharge",UIMenuCallback,PowerMenuRechargeCB,NULL,NULL));
	ui_MenuAppendItem(s_pMenuPowers,ui_MenuItemCreate("Open in Editor",UIMenuCallback,PowerMenuEditCB,NULL,NULL));
	ui_MenuAppendItem(s_pMenuPowers,ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL));
	ui_MenuAppendItem(s_pMenuPowers,ui_MenuItemCreate("Recharge All Powers",UIMenuCallback,PowerMenuRechargeCB,NULL,NULL));
	s_pMenuAttribMods = ui_MenuCreate(NULL);
	ui_MenuAppendItem(s_pMenuAttribMods,ui_MenuItemCreate("Open in Editor",UIMenuCallback,AttribModMenuEditCB,NULL,NULL));
	ui_MenuAppendItem(s_pMenuAttribMods,ui_MenuItemCreate("Expire",UIMenuCallback,AttribModMenuExpireCB,NULL,NULL));

	// Main menu button
	s_pButtonMainMenu = ui_ButtonCreate("X", 0, 0, MainMenu, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(s_pButtonMainMenu),"Main Menu");
	ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pButtonMainMenu));

	// Name/EntRef label
	s_pLabelName = ui_LabelCreate("",0,0);
	ui_WidgetSkin(UI_WIDGET(s_pLabelName),&s_skinLabelBold);
	ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pLabelName));

	// Switch buttons
	s_pButtonSwitchEntSelected = ui_ButtonCreate("",0,0,SwitchDebugEnt,NULL);
	ui_WidgetSkin(UI_WIDGET(s_pButtonSwitchEntSelected),&s_skinToggleGroup);
	ui_ButtonSetTextAndResize(s_pButtonSwitchEntSelected,"Switch to Selected");
	ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pButtonSwitchEntSelected));

	s_pButtonSwitchEnt = ui_ButtonCreate("Switch Debug Ent",0,0,SwitchDebugEnt,NULL);
	ui_WidgetSkin(UI_WIDGET(s_pButtonSwitchEnt),&s_skinToggleGroup);
	ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pButtonSwitchEnt));
	
	// Status label
	s_pLabelStatus = ui_LabelCreate("",0,0);
	ui_WidgetSkin(UI_WIDGET(s_pLabelStatus),&s_skinLabelMild);
	ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pLabelStatus));

	// Toggle buttons
	s_pTogglePowers = ui_ButtonCreate("Powers",0,0,Toggle,&s_uiShowPowers);
	ui_WidgetSkin(UI_WIDGET(s_pTogglePowers),&s_skinToggleGroup);
	ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pTogglePowers));

	s_pToggleAttribMods = ui_ButtonCreate("AttribMods",0,0,Toggle,&s_uiShowAttribMods);
	ui_WidgetSkin(UI_WIDGET(s_pToggleAttribMods),&s_skinToggleGroup);
	ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pToggleAttribMods));

	s_pToggleAttributes = ui_ButtonCreate("Attributes",0,0,Toggle,&s_uiShowAttributes);
	ui_WidgetSkin(UI_WIDGET(s_pToggleAttributes),&s_skinToggleGroup);
	ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pToggleAttributes));

	s_pTogglePowerModes = ui_ButtonCreate("PowerModes",0,0,Toggle,&s_uiShowPowerModes);
	ui_WidgetSkin(UI_WIDGET(s_pTogglePowerModes),&s_skinToggleGroup);
	ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(s_pTogglePowerModes));

	// Powers list
	s_pListPowers = ui_ListCreate(parse_Power,&s_ppPowers,15);
	ui_WidgetSkin(UI_WIDGET(s_pListPowers),&s_skinList);
	ui_ListSetContextCallback(s_pListPowers,PowerListContextCB,NULL);
	
	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Name",(intptr_t)ListMakePowerDefText,"Name");
	pTempColumn->fWidth = 150;
	ui_ListAppendColumn(s_pListPowers,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Rchrg",(intptr_t)ListMakePowerRechargeText,NULL);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(s_pListPowers,pTempColumn);
	
	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Timer",(intptr_t)ListMakePowerTimerText,NULL);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(s_pListPowers,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Charge",(intptr_t)ListMakePowerChargeText,NULL);
	pTempColumn->fWidth = 40;
	ui_ListAppendColumn(s_pListPowers,pTempColumn);

	// Mods list
	s_pListMods = ui_ListCreate(parse_AttribMod,&s_ppMods,15);
	ui_WidgetSkin(UI_WIDGET(s_pListMods),&s_skinList);
	ui_ListSetContextCallback(s_pListMods,AttribModListContextCB,NULL);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Attrib",(intptr_t)ListMakeAttribModAttribText,NULL);
	pTempColumn->fWidth = 85;
	ui_ListAppendColumn(s_pListMods,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Aspect",(intptr_t)ListMakeAttribModAspectText,NULL);
	pTempColumn->fWidth = 85;
	ui_ListAppendColumn(s_pListMods,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Mag",(intptr_t)ListMakeAttribModMagnitudeText,NULL);
	pTempColumn->fWidth = 45;
	ui_ListAppendColumn(s_pListMods,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Dur",(intptr_t)ListMakeAttribModDurationText,NULL);
	pTempColumn->fWidth = 45;
	ui_ListAppendColumn(s_pListMods,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Health",(intptr_t)ListMakeAttribModHealthText,NULL);
	pTempColumn->fWidth = 45;
	ui_ListAppendColumn(s_pListMods,pTempColumn);

	// Attribs list
	s_pListAttribs = ui_ListCreate(parse_CDAttribute,&s_ppAttribs,15);
	ui_WidgetSkin(UI_WIDGET(s_pListAttribs),&s_skinList);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Attrib",(intptr_t)ListMakeAttributeAttribText,NULL);
	pTempColumn->fWidth = 100;
	ui_ListAppendColumn(s_pListAttribs,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Basic",(intptr_t)ListMakeAttributeBasicText,NULL);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(s_pListAttribs,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Str",(intptr_t)ListMakeAttributeStrText,NULL);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(s_pListAttribs,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Res",(intptr_t)ListMakeAttributeResText,NULL);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(s_pListAttribs,pTempColumn);


	// PowerModes window
	s_pWindowPowerModes = ui_WindowCreate("Power Modes", right-415, bottom-215, 400, 200);
	s_pListPowerModes = ui_ListCreate(parse_CDPowerMode, &s_ppPowerModes, 20);

	pTempColumn = ui_ListColumnCreateParseName("PowerMode", "PowerMode", NULL);
	ui_ListColumnSetSortable(pTempColumn, true);
	ui_ListColumnSetWidth(pTempColumn, true, 1.0f);
	ui_ListAppendColumn(s_pListPowerModes, pTempColumn);

	pTempColumn = ui_ListColumnCreateParseName("Personal", "PersonalName", NULL);
	ui_ListColumnSetSortable(pTempColumn, true);
	ui_ListColumnSetWidth(pTempColumn, true, 1.0f);
	ui_ListAppendColumn(s_pListPowerModes, pTempColumn);

	pTempColumn = ui_ListColumnCreateParseName("Personal Debug", "PersonalDebugName", NULL);
	ui_ListColumnSetSortable(pTempColumn, true);
	ui_ListColumnSetWidth(pTempColumn, true, 1.0f);
	ui_ListAppendColumn(s_pListPowerModes, pTempColumn);

	ui_WidgetSetDimensionsEx((UIWidget*)s_pListPowerModes, 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx((UIWidget*)s_pListPowerModes,0,0,0,0);
	ui_WindowAddChild(s_pWindowPowerModes,s_pListPowerModes);
}

// Copies combat debug information, which does some special processing
void combatdebug_Copy(CombatDebug *pSrc, CombatDebug *pDest, F32 fRate)
{
	static AttribMod **s_ppModsExpired;

	int i;

	StructCopyFields(parse_CombatDebug,pSrc,pDest,0,0);

	// Keep all the expired mods around for an extra 2 seconds for display only
	for(i=eaSize(&s_ppModsExpired)-1;i>=0;i--)
	{
		if(s_ppModsExpired[i]->fDuration > -2.0f)
		{
			AttribMod *pmod = StructCreate(parse_AttribMod);

			s_ppModsExpired[i]->fDuration -= fRate;
			StructCopyFields(parse_AttribMod,s_ppModsExpired[i],pmod,0,0);
			eaPush(&pDest->ppMods,pmod);
		}
		else
		{
			AttribMod *pmod = s_ppModsExpired[i];
			eaRemoveFast(&s_ppModsExpired,i);
			StructDestroy(parse_AttribMod,pmod);
		}
	}

	for(i=eaSize(&pSrc->ppMods)-1; i>=0;i--)
	{
		AttribMod *pmod = pSrc->ppMods[i];
		int c;

		if(GET_REF(pmod->hPowerDef) && pmod->fDuration <= 0.0f && ((PowerDef*)GET_REF(pmod->hPowerDef))->eType != kPowerType_Innate)
		{
			AttribMod *pModPush;

			for(c=eaSize(&s_ppModsExpired)-1;c>=0;c--)
			{
				if(pSrc->ppMods[i]->uiDefIdx == s_ppModsExpired[c]->uiDefIdx && pSrc->ppMods[i]->uiApplyID == s_ppModsExpired[c]->uiApplyID)
					break;
			}

			if(c>=0) 
				continue;

			pModPush = StructCreate(parse_AttribMod);

			StructCopyFields(parse_AttribMod,pmod,pModPush,0,0);
			eaPush(&s_ppModsExpired,pModPush);
		}
	}


}

void combatdebug_HandlePacket(Packet * pPacket)
{
	F32 fRate = pktGetF32(pPacket);
	CombatDebug *pData;

	StructDestroySafe(parse_CombatDebug, &g_pCombatDebugData);
	g_pCombatDebugData = StructAlloc(parse_CombatDebug);

	pData = pktGetStruct(pPacket, parse_CombatDebug);
	combatdebug_Copy(pData,g_pCombatDebugData,fRate);
	StructDestroySafe(parse_CombatDebug, &pData);
		
	combatDebugView(true);
}

void combatDebugView(int bShow)
{
	if(s_Pane == NULL)
	{
		s_Pane = ui_PaneCreate(0,0,1,1,UIUnitPercentage,UIUnitPercentage,0);
		InitViewer();
	}

	if(bShow)
	{
		if(!s_bShowing)
		{
			if(s_uiHideUntil <= timeSecondsSince2000())
			{
				s_bShowing = true;
				ui_WidgetAddToDevice(UI_WIDGET(s_Pane),NULL);
			}
		}
		RefreshData();
	}
	else if(!bShow && s_bShowing)
	{
		s_bShowing = false;
		ui_WidgetRemoveFromGroup(UI_WIDGET(s_Pane));
		ui_WindowHide(s_pWindowPowerModes);
	}
}



// Perf window statics
static UIWindow *s_pWindowPerf = NULL;
static UIList *s_pListPerf = NULL;
static UIButton *s_pButtonPerfEnable = NULL;
static CombatDebugPerfEvent **s_ppPerfEvents = NULL;

static void CombatDebugPerfEnableButton(UIAnyWidget *widget, UserData unused)
{
	S32 bEnable = !(stricmp("Enable",ui_ButtonGetText(s_pButtonPerfEnable)));
	globCmdParsef("CombatDebugPerfEnable %d",bEnable);
}

static void CombatDebugPerfResetButton(UIAnyWidget *widget, UserData unused)
{
	globCmdParsef("CombatDebugPerfReset");
}

static void CombatDebugPerfDumpButton(UIAnyWidget *widget, UserData unused)
{
	if(s_pListPerf)
	{
		ui_ListDoCrazyCSVDumpThing(s_pListPerf);
	}
}


static void CombatDebugPerfTick(UIWindow *pWindow, UI_PARENT_ARGS)
{
	Entity *e = entActivePlayerPtr();
	PlayerDebug* pDebug = e ? entGetPlayerDebug(e, false) : NULL;

	if(pDebug && pDebug->combatDebugPerf)
	{
		S32 i,iSelected = ui_ListGetSelectedRow(s_pListPerf);
		U64 ulTimeTotal = 0;
		CombatDebugPerfEvent *pSelected = NULL;
		if(iSelected>=0)
		{
			pSelected = s_ppPerfEvents[iSelected];
			eaRemoveFast(&s_ppPerfEvents,iSelected);
			ui_ListSetSelectedRow(s_pListPerf,-1);
		}

		eaDestroyStruct(&s_ppPerfEvents,parse_CombatDebugPerfEvent);
		
		eaCopyStructs(&pDebug->ppCombatEvents,&s_ppPerfEvents,parse_CombatDebugPerfEvent);
		for(i=eaSize(&s_ppPerfEvents)-1; i>=0; i--)
		{
			s_ppPerfEvents[i]->ulTimePerEvent = s_ppPerfEvents[i]->ulTime / s_ppPerfEvents[i]->uiCount;
			ulTimeTotal += s_ppPerfEvents[i]->ulTime;
		}
		for(i=eaSize(&s_ppPerfEvents)-1; i>=0; i--)
		{
			s_ppPerfEvents[i]->fPercentUsage = (F32)((F64)s_ppPerfEvents[i]->ulTime / (F64)ulTimeTotal);
		}

		ui_ListSort(s_pListPerf);

		if(pSelected)
		{
			for(i=eaSize(&s_ppPerfEvents)-1; i>=0; i--)
			{
				if(!stricmp(pSelected->pchEvent,s_ppPerfEvents[i]->pchEvent))
				{
					ui_ListSetSelectedRow(s_pListPerf,i);
					break;
				}
			}
			StructDestroy(parse_CombatDebugPerfEvent,pSelected);
		}

		ui_ButtonSetText(s_pButtonPerfEnable,"Disable");
	}
	else
	{
		ui_ButtonSetText(s_pButtonPerfEnable,"Enable");
	}
	ui_WindowTick(pWindow,UI_PARENT_VALUES);
}

void CombatDebugPerfCreateWindow(void)
{
	UIListColumn *pColumn = NULL;
	UIButton *pButton = NULL;

	s_pWindowPerf = ui_WindowCreate("Combat Performance", 0, 0, 400, 700);

	s_pButtonPerfEnable = ui_ButtonCreate("",0,0,CombatDebugPerfEnableButton,NULL);
	ui_WidgetSetDimensionsEx((UIWidget*)s_pButtonPerfEnable, 100, 20, UIUnitFixed, UIUnitFixed);
	ui_WindowAddChild(s_pWindowPerf,s_pButtonPerfEnable);

	pButton = ui_ButtonCreate("Reset",100,0,CombatDebugPerfResetButton,NULL);
	ui_WidgetSetDimensionsEx((UIWidget*)pButton, 100, 20, UIUnitFixed, UIUnitFixed);
	ui_WindowAddChild(s_pWindowPerf,pButton);

	pButton = ui_ButtonCreate("Dump CSV",200,0,CombatDebugPerfDumpButton,NULL);
	ui_WidgetSetDimensionsEx((UIWidget*)pButton, 100, 20, UIUnitFixed, UIUnitFixed);
	ui_WindowAddChild(s_pWindowPerf,pButton);

	s_pListPerf = ui_ListCreate(parse_CombatDebugPerfEvent, &s_ppPerfEvents, 20);

	pColumn = ui_ListColumnCreateParseName("Event", "Event", NULL);
	ui_ListColumnSetSortable(pColumn, true);
	ui_ListColumnSetWidth(pColumn, true, 1.0f);
	ui_ListAppendColumn(s_pListPerf, pColumn);

	pColumn = ui_ListColumnCreateParseName("Count", "uiCount", NULL);
	ui_ListColumnSetSortable(pColumn, true);
	ui_ListColumnSetWidth(pColumn, true, 1.0f);
	ui_ListAppendColumn(s_pListPerf, pColumn);

	pColumn = ui_ListColumnCreateParseName("Time", "ulTime", NULL);
	ui_ListColumnSetSortable(pColumn, true);
	ui_ListColumnSetWidth(pColumn, true, 1.0f);
	ui_ListAppendColumn(s_pListPerf, pColumn);

	pColumn = ui_ListColumnCreateParseName("Time Per Event", "ulTimePerEvent", NULL);
	ui_ListColumnSetSortable(pColumn, true);
	ui_ListColumnSetWidth(pColumn, true, 1.0f);
	ui_ListAppendColumn(s_pListPerf, pColumn);

	pColumn = ui_ListColumnCreateParseName("Percent Total", "PercentUsage", NULL);
	ui_ListColumnSetSortable(pColumn, true);
	ui_ListColumnSetWidth(pColumn, true, 1.0f);
	ui_ListAppendColumn(s_pListPerf, pColumn);

	pColumn = ui_ListColumnCreateParseName("TimeSub", "ulTimeSub", NULL);
	ui_ListColumnSetSortable(pColumn, true);
	ui_ListColumnSetWidth(pColumn, true, 1.0f);
	ui_ListAppendColumn(s_pListPerf, pColumn);

	ui_WidgetSetDimensionsEx((UIWidget*)s_pListPerf, 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx((UIWidget*)s_pListPerf,0,0,25,0);
	ui_WindowAddChild(s_pWindowPerf,s_pListPerf);

	((UIWidget*)s_pWindowPerf)->tickF = CombatDebugPerfTick;
}

AUTO_COMMAND ACMD_NAME("CombatDebugPerf");
void cmdCombatDebugPerf(S32 bShow)
{
	if(!s_pWindowPerf)
	{
		CombatDebugPerfCreateWindow();
	}

	if(s_pWindowPerf)
	{
		if(ui_WindowIsVisible(s_pWindowPerf))
		{
			if(!bShow)
			{
				ui_WindowHide(s_pWindowPerf);
			}
		}
		else
		{
			if(bShow)
			{
				ui_WindowShow(s_pWindowPerf);
			}
		}
	}
}

#include "AutoGen/CombatDebugViewer_c_ast.c"
