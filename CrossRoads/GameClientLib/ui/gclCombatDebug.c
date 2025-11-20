/***************************************************************************
*     Copyright (c) 2000-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "Character.h"
#include "Color.h"
#include "EString.h"
#include "gclEntity.h"

#include "UIWindow.h"
#include "UILabel.h"
#include "MemoryPool.h"
#include "GfxDebug.h"
#include "GraphicsLib.h"
#include "PowerActivation.h"
#include "DamageTracker.h"
#include "PowersMovement.h"
#include "EntityMovementManager.h"
#include "GfxPrimitive.h"
#include "CharacterAttribs.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static UIWindow* s_pWindow = NULL;

static UILabel *s_pLabelOverflow = NULL;
static UILabel *s_pLabelQueue = NULL;
static UILabel *s_pLabelCurrent = NULL;
static UILabel *s_pLabelFinished = NULL;

#define WidgetBottom(w) (((UIWidget*)(w))->height + ((UIWidget*)(w))->y)
#define WidgetRight(w) (((UIWidget*)(w))->width + ((UIWidget*)(w))->x)

static void TickWindow(SA_PARAM_NN_VALID UIWindow *pWindow, UI_PARENT_ARGS)
{
	char *pchTemp = NULL;
	PowerDef *pdef = NULL;
	Character *pchar = characterActivePlayerPtr();

	estrStackCreate(&pchTemp);
	
	estrCopy2(&pchTemp,"Overflow:");
	if(pchar && pchar->pPowActOverflow && NULL!=(pdef = GET_REF(pchar->pPowActOverflow->hdef)))
	{
		estrConcatf(&pchTemp," %d: %s (%s) %s",pchar->pPowActOverflow->uchID,powerdef_GetLocalName(pdef),pdef->pchName,REF_STRING_FROM_HANDLE(pdef->hFX));
	}
	ui_LabelSetText(s_pLabelOverflow,pchTemp);

	estrCopy2(&pchTemp,"Queue:");
	if(pchar && pchar->pPowActQueued && NULL!=(pdef = GET_REF(pchar->pPowActQueued->hdef)))
	{
		estrConcatf(&pchTemp," %d: %s (%s) %s",pchar->pPowActQueued->uchID,powerdef_GetLocalName(pdef),pdef->pchName,REF_STRING_FROM_HANDLE(pdef->hFX));
	}
	ui_LabelSetText(s_pLabelQueue,pchTemp);

	estrCopy2(&pchTemp,"Current:");
	if(pchar && pchar->pPowActCurrent && NULL!=(pdef = GET_REF(pchar->pPowActCurrent->hdef)))
	{
		estrConcatf(&pchTemp," %d: %d %s (%s) %s",pchar->pPowActCurrent->uchID,pchar->pPowActCurrent->uiPeriod,powerdef_GetLocalName(pdef),pdef->pchName,REF_STRING_FROM_HANDLE(pdef->hFX));
	}
	ui_LabelSetText(s_pLabelCurrent,pchTemp);

	estrCopy2(&pchTemp,"Finished:");
	if(pchar && pchar->pPowActFinished && NULL!=(pdef = GET_REF(pchar->pPowActFinished->hdef)))
	{
		estrConcatf(&pchTemp," %d: %s (%s) %s",pchar->pPowActFinished->uchID,powerdef_GetLocalName(pdef),pdef->pchName,REF_STRING_FROM_HANDLE(pdef->hFX));
	}
	ui_LabelSetText(s_pLabelFinished,pchTemp);

	estrDestroy(&pchTemp);

	ui_WindowTick(pWindow,pX,pY,pW,pH,pScale);
}

static void BuildWindow(void)
{
	s_pWindow = ui_WindowCreate("CombatDebugClient",0,200,350,125);

	if(s_pWindow)
	{
		ui_LabelCreateAndAdd(s_pLabelOverflow,s_pWindow,"",UI_HSTEP,0);
		ui_LabelCreateAndAdd(s_pLabelQueue,s_pWindow,"",UI_HSTEP,UI_HSTEP+WidgetBottom(s_pLabelOverflow));
		ui_LabelCreateAndAdd(s_pLabelCurrent,s_pWindow,"",UI_HSTEP,UI_HSTEP+WidgetBottom(s_pLabelQueue));
		ui_LabelCreateAndAdd(s_pLabelFinished,s_pWindow,"",UI_HSTEP,UI_HSTEP+WidgetBottom(s_pLabelCurrent));

		((UIWidget*)s_pWindow)->tickF = TickWindow;
	}
}

AUTO_COMMAND ACMD_NAME("CombatDebugClient");
void gclCombatDebugClient(S32 bShow)
{
	if(!s_pWindow)
	{
		BuildWindow();
	}

	if(s_pWindow)
	{
		if(ui_WindowIsVisible(s_pWindow))
		{
			if(!bShow)
			{
				ui_WindowHide(s_pWindow);
			}
		}
		else
		{
			if(bShow)
			{
				ui_WindowShow(s_pWindow);
			}
		}
	}
}


// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------
typedef struct CombatDebugMeterDatam
{
	F32		fDamage;
	U32		processCount;
} CombatDebugMeterDatam;

static struct 
{
	F32 fTimeWindow;

	EntityRef erEntity;
	U32 bEnabled;
	U32 bShowGraph;
	F32 fDamageScale;
	CombatDebugMeterDatam **eaTrackedData;
} g_combatMeterDebug = {3.f, 0, 0, 0, 5.f, NULL};

MP_DEFINE(CombatDebugMeterDatam);

// ------------------------------------------------------------------------------------------------------------------------
static void gclCombatDebugMeters_SetEntityInternal(EntityRef erEnt)
{
	if (g_combatMeterDebug.erEntity == erEnt)
		return;

	FOR_EACH_IN_EARRAY(g_combatMeterDebug.eaTrackedData, CombatDebugMeterDatam, pCombatDebug)
	{
		MP_FREE(CombatDebugMeterDatam, pCombatDebug);
	}
	FOR_EACH_END

	eaClear(&g_combatMeterDebug.eaTrackedData);
	
	g_combatMeterDebug.erEntity = erEnt;
	g_combatMeterDebug.bEnabled = (erEnt != -1);
}

AUTO_COMMAND ACMD_NAME(CombatDebugMetersShowGraph) ACMD_ACCESSLEVEL(7);
void gclCombatDebugMeters_ShowGraph(S32 showGraph)
{
	g_combatMeterDebug.bShowGraph = !!showGraph;
}

AUTO_COMMAND ACMD_NAME(CombatDebugMetersGraphDamageScale) ACMD_ACCESSLEVEL(7);
void gclCombatDebugMeters_SetGraphDamageScale(F32 damageScale)
{
	damageScale = CLAMP(damageScale, 1.f, 10.f);
	g_combatMeterDebug.fDamageScale = damageScale;
}


// ------------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(CombatDebugMetersTime) ACMD_ACCESSLEVEL(7);
void gclCombatDebugMeters_SetTimeWindow(F32 time)
{
	time = CLAMP(time, 1.f, 10.f);
	
	g_combatMeterDebug.fTimeWindow = time;
}

// ------------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(CombatDebugMetersEnt) ACMD_ACCESSLEVEL(7);
void gclCombatDebugMeters_SetEntity(const char* target)
{
	EntityRef debugRef;
	Entity *pPlayerEnt = entActivePlayerPtr();
	
	U32 selected = !stricmp(target, "selected");
	
	if(!entGetClientTarget(pPlayerEnt, target, &debugRef) && !selected)
	{
		gclCombatDebugMeters_SetEntityInternal(-1);
		return;
	}

	gclCombatDebugMeters_SetEntityInternal(debugRef);
}


// ------------------------------------------------------------------------------------------------------------------------
void gclCombatDebugMeters_Notify(CombatTrackerNet *pNet)
{
	if (g_combatMeterDebug.bEnabled && pNet->erSource == g_combatMeterDebug.erEntity)
	{
		if ( ATTRIB_DAMAGE(pNet->eType) && pNet->fMagnitude > 0)
		{
			CombatDebugMeterDatam *pDPS;

			MP_CREATE(CombatDebugMeterDatam, 128);
			
			pDPS = MP_ALLOC(CombatDebugMeterDatam);

			pDPS->fDamage = pNet->fMagnitude;
			// CombatTrackerNet doesn't have a timestamp on it, so let's just assume this is happening this frame
			pDPS->processCount = pmTimestamp(0);

			eaPush(&g_combatMeterDebug.eaTrackedData, pDPS);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static void gclCombatDebugMeters_DrawGraph()
{
	U32 uFirstTimestamp = pmTimestamp(-g_combatMeterDebug.fTimeWindow);
	U32 uEndTimestamp = pmTimestamp(0);
	U32 uCurTimestamp;
	S32 x, y;
	S32 iBaseBarHeight;
	S32 i, w, h;
	S32 iBarWidth = 10;
	S32 iNumTicks = 0;

	x = 10;
	y = 100;
	iBaseBarHeight = 1;

	i = eaSize(&g_combatMeterDebug.eaTrackedData) - 1;

	gfxGetActiveDeviceSize(&w, &h);

	iNumTicks = (uEndTimestamp - uFirstTimestamp) / MM_PROCESS_COUNTS_PER_STEP;
	iBarWidth = (w - iNumTicks) / iNumTicks;
	
	if (iBarWidth == 0)
		iBarWidth = 1;
	else if (iBarWidth > 10)
		iBarWidth = 10;
			

	for (uCurTimestamp = uEndTimestamp; uCurTimestamp >= uFirstTimestamp; uCurTimestamp -= MM_PROCESS_COUNTS_PER_STEP)
	{
		F32 fDamage = 0.f;
		S32 iBarHeight = iBaseBarHeight;
		U32 color = 0xFF000000;

		for (i; i >= 0; --i)
		{
			CombatDebugMeterDatam *pDamage = g_combatMeterDebug.eaTrackedData[i];
			if (uCurTimestamp >= pDamage->processCount && 
				uCurTimestamp - MM_PROCESS_COUNTS_PER_STEP < pDamage->processCount)
			{
				fDamage += pDamage->fDamage;
			}
			else
			{
				break;
			}
		}

		iBarHeight += fDamage * g_combatMeterDebug.fDamageScale;
		
		if (fDamage)
		{
			color = 0xFFFF0000;
		}
		else
		{
			color = 0xFFFFFFFF;
		}

		gfxDrawQuadARGB(x, 
						h - y, 
						x + iBarWidth, 
						h - y - iBarHeight, 
						2000, 
						color);
		
		x += iBarWidth + 1;
	}
	
}

// ------------------------------------------------------------------------------------------------------------------------
void gclCombatDebugMeters_Tick()
{
	if (g_combatMeterDebug.bEnabled)
	{
		F32 fDPS = 0.f;
		U32 uLastTimestamp = pmTimestamp(-g_combatMeterDebug.fTimeWindow);
		U32 uCurTimestamp = pmTimestamp(0);

		S32 idx = -1;

		FOR_EACH_IN_EARRAY(g_combatMeterDebug.eaTrackedData, CombatDebugMeterDatam, pDPS)
		{

			if (pDPS->processCount > uLastTimestamp)
			{
				F32 fWeight = ((uCurTimestamp - pDPS->processCount)/(F32)MM_PROCESS_COUNTS_PER_SECOND) / g_combatMeterDebug.fTimeWindow;
				fWeight = 1.f - fWeight;
				
				fDPS += pDPS->fDamage;// * fWeight;
			}
			else
			{	// past the time we care about, free the struct
				if (idx == -1)
					idx = FOR_EACH_IDX(g_combatMeterDebug.eaTrackedData, pDPS);

				MP_FREE(CombatDebugMeterDatam, pDPS);
			}
		}
		FOR_EACH_END

		if (g_combatMeterDebug.bShowGraph)
			gclCombatDebugMeters_DrawGraph();

		if (idx != -1)
			eaRemoveRange(&g_combatMeterDebug.eaTrackedData, 0, idx+ 1);
		
		fDPS = fDPS / g_combatMeterDebug.fTimeWindow;

		gfxXYprintfColor(1, 1, ColorRed.r, ColorRed.g, ColorRed.b, ColorRed.a, "%2.2f", fDPS);
	}
}
