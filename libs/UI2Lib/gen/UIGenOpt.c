#include "cmdparse.h"
#include "StringCache.h"
#include "Expression.h"
#include "structInternals.h"
#include "ScratchStack.h"
#include "memcheck.h"
#include "ExpressionPrivate.h"
#include "BlockEarray.h"
#include "sysutil.h"          // for winCopyToClipboard

#include "GfxClipper.h"
#include "GfxSpriteText.h"
#include "GfxPrimitive.h"
#include "GfxSpriteList.h"
#include "GfxSprite.h"
#include "GfxTextures.h"
#include "GfxDXT.h"
#include "MatrixStack.h"
#include "GfxDebug.h"

#include "input.h"
#include "InputLib.h"
#include "inputMouse.h"
#include "dynFxManager.h"
#include "partition_enums.h"

#include "UIGen.h"
#include "UITextureAssembly.h"
#include "UIGenPrivate.h"
#include "UIGenTutorial.h"
#include "UIGen_h_ast.h"
#include "UIGenBox_h_ast.h"
#include "UIGenPrivate_h_ast.h"
#include "UIGenDnD.h"
#include "UIGenList.h"
#include "DynFxInterface.h"
#include "structinternals_h_ast.h"

#include "AutoGen/UIGenDnD_h_ast.h"
#include "AutoGen/UIGenTutorial_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// Parts of the gen system we want to compile with optimization enabled...

static int s_bNewMouseOverBehavior = false;
AUTO_CMD_INT(s_bNewMouseOverBehavior, InputNewMouseOverBehavior) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9);

static int s_bGenPositionRound = true;
AUTO_CMD_INT(s_bGenPositionRound, GenPositionRound) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9);

#define genRound(a) roundTiesUp(a)

static const char *s_pchSelf = NULL;
static const char *s_pchFocusedGen = NULL;
static const char *s_pchAll;
static const char *s_pch_Self = NULL;
static const char *s_pch_Parent = NULL;
static const char *s_pch_Previous = NULL;
static const char *s_pchTutorial = NULL;

int g_ui_iGenData;
const char *g_ui_pchGenData;

struct TimerThreadData * g_CurrentGenTimerTD = NULL;
struct TimerThreadData * g_SpecialGenTimerTD = NULL;
bool g_bUsingSpecialTimer = false;
bool g_bProfilerConnected = false;

AUTO_RUN;
void ui_GenOptInitStaticStrings(void)
{
	s_pchSelf = allocAddStaticString("Self");
	s_pchFocusedGen = allocAddStaticString("FocusedGen");
	g_ui_pchGenData = allocAddStaticString("GenData");
	s_pchAll = allocAddStaticString("*");
	s_pch_Self = allocAddStaticString("_Self");
	s_pch_Parent = allocAddStaticString("_Parent");
	s_pch_Previous = allocAddStaticString("_Previous");
	s_pchTutorial = allocAddStaticString("Tutorial");
	ui_GenDebugRegisterContextPointer(g_ui_pchGenData, NULL);
}

#define GEN_FORCE_INLINE_L2
// #define GEN_FORCE_INLINE_L2 __forceinline

GEN_FORCE_INLINE_L2 static UIGenInternal *GenInternalClone(UIGen *pGen, UIGenInternal *pBase, ParseTable *pTable, UIGenType eRealType, bool bBorrow)
{
	UIGen **eaChildren = NULL;
	UIGenInternal *pResult;
	eaCopy(&eaChildren, &pBase->eaInlineChildren);
	eaClearFast(&pBase->eaInlineChildren);
	if (pBase->eType == kUIGenTypeBox && pTable != parse_UIGenBox)
	{
		pResult = StructCreateVoid(pTable);
		StructCopyAllVoid(parse_UIGenBox, pBase, pResult);
		pResult->eType = eRealType;
	}
	else
		pResult = StructCloneVoid(pTable, pBase);
	assertmsg(pResult, "No result was generated!");
	eaCopy(&pBase->eaInlineChildren, &eaChildren);

	if (bBorrow)
	{
		S32 i;
		for (i = 0; i < eaSize(&pBase->eaInlineChildren); i++)
		{
			UIGen *pChild = pBase->eaInlineChildren[i];
			UIGen *pExisting = eaIndexedGetUsingString(&pGen->eaBorrowedInlineChildren, pChild->pchName);
			if (!pExisting)
			{
				pExisting = ui_GenClone(pChild);
				eaPush(&pGen->eaBorrowedInlineChildren, pExisting);
			}
			eaPush(&pResult->eaInlineChildren, pExisting);
		}
	}
	else
		eaPushEArray(&pResult->eaInlineChildren, &pBase->eaInlineChildren);
	eaDestroy(&eaChildren);
	return pResult;
}

GEN_FORCE_INLINE_L2 static void ui_GenUpdateFromGlobalState(UIGen *pGen)
{
	S32 i;
	for (i = 0; i < eaiSize(&g_GenState.eaiGlobalOffStates); i++)
		ui_GenUnsetState(pGen, g_GenState.eaiGlobalOffStates[i]);
	for (i = 0; i < eaiSize(&g_GenState.eaiGlobalOnStates); i++)
		ui_GenSetState(pGen, g_GenState.eaiGlobalOnStates[i]);
}

__forceinline static void DynNodeFromCBox(CBox *pBox, UIDirection eOffsetFrom, DynNode *pNode)
{
	Vec3 v3Pos = {0, 0, 0};
	F32 fX;
	F32 fY;
	if (eOffsetFrom & UILeft)
		fX = pBox->lx;
	else if (eOffsetFrom & UIRight)
		fX = pBox->hx;
	else
		fX = (pBox->lx + pBox->hx) / 2;
	if (eOffsetFrom & UITop)
		fY = pBox->ly;
	else if (eOffsetFrom & UIBottom)
		fY = pBox->hy;
	else
		fY = (pBox->ly + pBox->hy) / 2;
	v3Pos[0] = fX / g_ui_State.screenWidth;
	v3Pos[1] = 1 - fY / g_ui_State.screenHeight;
	dynNodeSetPos(pNode, v3Pos);
}

GEN_FORCE_INLINE_L2 static bool ui_GenSetTweenInfo(UIGen *pGen, UIGenTweenInfo *pInfo, UIGenInternal *pOld, UIGenInternal *pNew)
{
	// FIXME: Actually we need different behavior depending on the tween type, or we
	// need some kind of queuing system. For linear tweens (or any other kind with
	// an end) we want to start from the current widget state, but for cyclical
	// tweens we need to go back to whatever our state would be without
	// the state causing the cyclical tween.
	//
	// Actually, the more I think about it, the more I'm convinced we need both;
	// we need to allow several tweens to be attached to gens at the same time,
	// and we need to let cyclical tweens give their start and end states.
	// Then a cyclical tween really just becomes a linear tween to the start
	// state followed by a cyclical tween. Basically rather than just assigning
	// pTween during the layout, we should be pushing it somewhere.
	//
	// Another possibility is to interpolate based on USEDFIELD presence only.
	// Along with a queue this might solve all the problems involved.
	//
	// The only decision that needs to be made right now is the file format though.

	ParseTable *pTable = ui_GenGetType(pGen);
	if (!(pGen->pTweenState || pGen->pBoxTweenState) && !pInfo)
		// No tween to do, and we're not doing one.
		return false;
	if (pGen->pTweenState && pGen->pTweenState->pInfo == pInfo &&
		!StructCompare(pTable, pOld, pGen->pTweenState->pStart, 0, 0, 0) &&
		!StructCompare(pTable, pNew, pGen->pTweenState->pEnd, 0, 0, 0))
		// We're already doing this tween, don't do it again.
		return false;
	else if (!pOld && !pInfo->pTweenBox)
	{
		// We don't have a current state, so we have nothing to tween from.
		pGen->pResult = pNew;
		return false;
	}
	else
	{
		StructDestroySafe(parse_UIGenTweenState, &pGen->pTweenState);
		if (pInfo)
		{
			if (!pInfo->pTweenBox)
			{
				StructDestroySafe(parse_UIGenTweenBoxState, &pGen->pBoxTweenState);
				pGen->pTweenState = StructCreate(parse_UIGenTweenState);
				pGen->pTweenState->pInfo = pInfo;
				pGen->pTweenState->pStart = GenInternalClone(pGen, pOld, ui_GenInternalGetType(pOld), pOld->eType, false);
				pGen->pTweenState->pEnd = GenInternalClone(pGen, pNew, ui_GenInternalGetType(pNew), pNew->eType, false);
				eaDestroy(&pGen->pTweenState->pStart->eaInlineChildren);
				eaDestroy(&pGen->pTweenState->pEnd->eaInlineChildren);
			}
			else if (!pGen->pBoxTweenState)
			{
				pGen->pBoxTweenState = StructCreate(parse_UIGenTweenBoxState);
				pGen->pBoxTweenState->pInfo = StructClone(parse_UIGenTweenInfo, pInfo);
			}
			else
			{
				StructCopyAll(parse_UIGenTweenInfo, pInfo, pGen->pBoxTweenState->pInfo);
			}
		}
		else
		{
			StructDestroySafe(parse_UIGenTweenBoxState, &pGen->pBoxTweenState);
		}
		return true;
	}
}

__forceinline static F32 ui_GenTweenGetParam(UIGenTweenInfo *pInfo, F32 *pfElapsedTime)
{
	if (!pInfo || pInfo->fTotalTime < 0.00001)
		return 2.f;
	return ui_TweenGetParam(pInfo->eType, pInfo->fTotalTime, pInfo->fTimeBetweenCycles, *pfElapsedTime, pfElapsedTime);
}

__forceinline static F32 ui_GenTweenStateUpdate(UIGen *pGen, UIGenTweenState *pState)
{
	pState->fElapsedTime += pGen->fTimeDelta;
	return ui_GenTweenGetParam(pState->pInfo, &pState->fElapsedTime);
}

GEN_FORCE_INLINE_L2 static void ui_GenInternalTween(UIGenInternal *pStart, UIGenInternal *pEnd, UIGenInternal *pResult, F32 fP)
{
	ParseTable *pTable = ui_GenInternalGetType(pStart);
	if (pTable)
	{
		S32 i;
		FORALL_PARSETABLE(pTable, i)
		{
			if (pTable[i].type & TOK_REDUNDANTNAME) continue;
			else if (pTable[i].type & TOK_EARRAY) continue;
			interp_autogen(pTable, i, pStart, pEnd, pResult, 0, fP);
		}
	}
}

__forceinline static bool ui_GenTween(UIGen *pGen)
{
	// It should actually be impossible to have a TweenState but no Result, but just in case...
	if (pGen->pTweenState && pGen->pResult)
	{
		F32 fParam = ui_GenTweenStateUpdate(pGen, pGen->pTweenState);
		if (fParam > 1.00001)
		{
			ui_GenInternalTween(pGen->pTweenState->pStart, pGen->pTweenState->pEnd, pGen->pResult, 1.0);
			StructDestroySafe(parse_UIGenTweenState, &pGen->pTweenState);
		}
		else
			ui_GenInternalTween(pGen->pTweenState->pStart, pGen->pTweenState->pEnd, pGen->pResult, fParam);
		return true;
	}
	return false;
}

__forceinline static F32 ui_GenTweenBoxStateUpdate(UIGen *pGen, UIGenTweenBoxState *pState)
{
	pState->fElapsedTime += pGen->fTimeDelta;
	return ui_GenTweenGetParam(pState->pInfo, &pState->fElapsedTime);
}

__forceinline static void ui_GenTweenBoxTo(CBox *pStart, CBox *pEnd, F32 fValue, CBox *pBox)
{
	pBox->lx = pStart->lx + (pEnd->lx - pStart->lx) * fValue;
	pBox->ly = pStart->ly + (pEnd->ly - pStart->ly) * fValue;
	pBox->hx = pStart->hx + (pEnd->hx - pStart->hx) * fValue;
	pBox->hy = pStart->hy + (pEnd->hy - pStart->hy) * fValue;
}

__forceinline static bool ui_GenTweenBoxRule(UIGenTweenBoxMode eMode, F32 fStart, F32 fEnd)
{
	switch (eMode)
	{
	case kUIGenTweenBoxModeIncrease:
		return fStart < fEnd;
	case kUIGenTweenBoxModeDecrease:
		return fStart > fEnd;
	case kUIGenTweenBoxModeBoth:
		return !nearf(fStart, fEnd);
	}
	return false;
}

GEN_FORCE_INLINE_L2 static bool ui_GenApplyTweenBoxRule(UIGenTweenBoxInfo *pInfo, CBox *pStart, CBox *pEnd)
{
	bool bTweenChanged = false;
	F32 fStart, fEnd;
	bool bTweenPos, bTweenSize;

	fStart = CBoxWidth(pStart);
	fEnd = CBoxHeight(pEnd);
	bTweenPos = ui_GenTweenBoxRule(pInfo->eXMode, pStart->lx, pEnd->lx);
	bTweenSize = ui_GenTweenBoxRule(pInfo->eWidthMode, fStart, fEnd);

	if (bTweenPos != bTweenSize)
	{
		if (bTweenPos)
		{
			// Width change is immediate
			pStart->hx = pStart->lx + fEnd;
		}
		else if (pInfo->eOffsetFrom & UIRight)
		{
			// Position change is immediate
			pStart->lx = pEnd->hx - fStart;
			pStart->hx = pEnd->hx;
		}
		else
		{
			// Position change is immediate
			pStart->hx = pEnd->lx + fStart;
			pStart->lx = pEnd->lx;
		}
		bTweenChanged = true;
	}
	else if (!bTweenPos)
	{
		// Not tweened
		pStart->lx = pEnd->lx;
		pStart->hx = pEnd->hx;
	}

	fStart = CBoxHeight(pStart);
	fEnd = CBoxHeight(pEnd);
	bTweenPos = ui_GenTweenBoxRule(pInfo->eYMode, pStart->ly, pEnd->ly);
	bTweenSize = ui_GenTweenBoxRule(pInfo->eHeightMode, fStart, fEnd);

	if (bTweenPos != bTweenSize)
	{
		if (bTweenPos)
		{
			// Height change is immediate
			pStart->hy = pStart->ly + fEnd;
		}
		else if (pInfo->eOffsetFrom & UIBottom)
		{
			// Position change is immediate
			pStart->ly = pEnd->hy - fStart;
			pStart->hy = pEnd->hy;
		}
		else
		{
			// Position change is immediate
			pStart->hy = pEnd->ly + fStart;
			pStart->ly = pEnd->ly;
		}
		bTweenChanged = true;
	}
	else if (!bTweenPos)
	{
		// Not tweened
		pStart->ly = pEnd->ly;
		pStart->hy = pEnd->hy;
	}

	return bTweenChanged;
}

GEN_FORCE_INLINE_L2 static bool ui_GenTweenBox(UIGen *pGen)
{
	if (pGen->pBoxTweenState)
	{
		UIGenTweenBoxState *pTweenState = pGen->pBoxTweenState;

		if (pTweenState->bInitialized)
		{
			UIGenTweenBoxInfo *pInfo = pTweenState->pInfo->pTweenBox;

			if (pTweenState->fElapsedTime >= 0)
			{
				CBox ScreenBox = pTweenState->End.ScreenBox;
				CBox UnpaddedScreenBox = pTweenState->End.UnpaddedScreenBox;
				bool bRestartTween = false;
				F32 fParam;

				// Only restart the tween if a tweened target value changes
				if (ui_GenApplyTweenBoxRule(pInfo, &pTweenState->End.ScreenBox, &pGen->ScreenBox))
					bRestartTween = true;
				if (ui_GenApplyTweenBoxRule(pInfo, &pTweenState->End.UnpaddedScreenBox, &pGen->UnpaddedScreenBox))
					bRestartTween = true;

				if (bRestartTween)
				{
					// Calculate where in the tween the box was last frame and start from there
					fParam = ui_GenTweenBoxStateUpdate(pGen, pTweenState);
					ui_GenTweenBoxTo(&pTweenState->Start.ScreenBox, &ScreenBox, fParam, &pTweenState->Start.ScreenBox);
					ui_GenTweenBoxTo(&pTweenState->Start.UnpaddedScreenBox, &UnpaddedScreenBox, fParam, &pTweenState->Start.UnpaddedScreenBox);

					pTweenState->End.ScreenBox = pGen->ScreenBox;
					pTweenState->End.UnpaddedScreenBox = pGen->UnpaddedScreenBox;

					pTweenState->fElapsedTime = 0;
				}

				ui_GenApplyTweenBoxRule(pInfo, &pTweenState->Start.ScreenBox, &pGen->ScreenBox);
				ui_GenApplyTweenBoxRule(pInfo, &pTweenState->Start.UnpaddedScreenBox, &pGen->UnpaddedScreenBox);

				fParam = ui_GenTweenBoxStateUpdate(pGen, pTweenState);
				if (fParam > 1.00001)
					pTweenState->fElapsedTime = -1;
				else
				{
					ui_GenTweenBoxTo(&pTweenState->Start.ScreenBox, &pTweenState->End.ScreenBox, fParam, &pGen->ScreenBox);
					ui_GenTweenBoxTo(&pTweenState->Start.UnpaddedScreenBox, &pTweenState->End.UnpaddedScreenBox, fParam, &pGen->UnpaddedScreenBox);
				}
				return true;
			}
			else
			{
				bool bStartTween = false;

				// Only start the tween if a tweened value changes
				if (ui_GenApplyTweenBoxRule(pInfo, &pTweenState->End.ScreenBox, &pGen->ScreenBox))
					bStartTween = true;
				if (ui_GenApplyTweenBoxRule(pInfo, &pTweenState->End.UnpaddedScreenBox, &pGen->UnpaddedScreenBox))
					bStartTween = true;

				if (bStartTween)
				{
					// Start at the previous position
					pTweenState->Start.ScreenBox = pTweenState->End.ScreenBox;
					pTweenState->Start.UnpaddedScreenBox = pTweenState->End.UnpaddedScreenBox;

					pTweenState->End.ScreenBox = pGen->ScreenBox;
					pTweenState->End.UnpaddedScreenBox = pGen->UnpaddedScreenBox;

					pTweenState->fElapsedTime = 0;
				}
			}
		}
		else
		{
			pTweenState->End.ScreenBox = pGen->ScreenBox;
			pTweenState->End.UnpaddedScreenBox = pGen->UnpaddedScreenBox;
			pTweenState->bInitialized = true;
		}
	}
	return false;
}

__forceinline static void ui_GenInitTween(UIGen *pGen)
{
	if (pGen->pTweenState && pGen->pResult)
	{
		ui_GenInternalTween(pGen->pTweenState->pStart, pGen->pTweenState->pEnd, pGen->pResult, 0);
	}
}

GEN_FORCE_INLINE_L2 static bool ui_GenQueueStateChange(UIGen *pContainer, UIGen *pGen, UIGenState eState, bool bEnable)
{
	S32 i;
	UIGenStateDef *pDef;
	bool bRebuild = false;
	if (!pContainer)
		return false;
	for (i = 0; i < eaSize(&pContainer->eaBorrowed); i++)
	{
		UIGen *pBorrowed = GET_REF(pContainer->eaBorrowed[i]->hGen);
		if (pBorrowed && (pDef = eaIndexedGetUsingInt(&pBorrowed->eaStates, eState)))
		{
			if (bEnable && pDef->pOnEnter)
			{
				eaPushUnique(&pGen->eaTransitions, pDef->pOnEnter);
				GEN_PRINTF("%s: Queue OnEnter: State=%s, From=%s", pGen->pchName, ui_GenGetStateName(eState), pBorrowed->pchName);
			}
			else if (!bEnable && pDef->pOnExit)
			{
				eaPushUnique(&pGen->eaTransitions, pDef->pOnExit);
				GEN_PRINTF("%s: Queue OnExit: State=%s, From=%s", pGen->pchName, ui_GenGetStateName(eState), pBorrowed->pchName);
			}
			bRebuild = bRebuild || pDef->pOverride;
		}
	}

	if (pDef = eaIndexedGetUsingInt(&pContainer->eaStates, eState))
	{
		if (bEnable && pDef->pOnEnter)
		{
			eaPushUnique(&pGen->eaTransitions, pDef->pOnEnter);
			GEN_PRINTF("%s: Queue OnEnter: State=%s, From=%s", pGen->pchName, ui_GenGetStateName(eState), pContainer->pchName);
		}
		else if (!bEnable && pDef->pOnExit)
		{
			eaPushUnique(&pGen->eaTransitions, pDef->pOnExit);
			GEN_PRINTF("%s: Queue OnExit: State=%s, From=%s", pGen->pchName, ui_GenGetStateName(eState), pContainer->pchName);
		}
		bRebuild = bRebuild || pDef->pOverride;
	}
	return bRebuild;
}

void ui_GenStates(UIGen *pGen, ...)
{
	UIGenState eState;
	bool bEnable;
	va_list va;
	va_start(va, pGen);
	while ((eState = va_arg(va, UIGenState)) && eState < sizeof(pGen->bfStates) * 8)
	{
		bool bChanged = false;
		bEnable = va_arg(va, int);
		if (bEnable && !TSTB(pGen->bfStates, eState))
		{
			GEN_PRINTF_LEVEL(1, "%s: Entering %s", pGen->pchName, ui_GenGetStateName(eState));
			SETB(pGen->bfStates, eState);
			bChanged = true;
		}
		else if (!bEnable && TSTB(pGen->bfStates, eState))
		{
			GEN_PRINTF_LEVEL(1, "%s: Exiting %s", pGen->pchName, ui_GenGetStateName(eState));
			CLRB(pGen->bfStates, eState);
			bChanged = true;
		}

		if (bChanged && ui_GenQueueStateChange(pGen, pGen, eState, bEnable))
		{
			GEN_PRINTF("%s: State transition requires reresult", pGen->pchName);
			ui_GenMarkDirty(pGen);
		}
	}
	va_end(va);
}

bool ui_GenTextureAssemblyRegionCheck(const UITextureInstance *pInstance, const CBox *pBox, int iMouseX, int iMouseY)
{
	BasicTexture *tex = texLoadRawData(pInstance->pchTexture, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);
	BasicTextureRareData *rare_data = texGetRareData(tex);
	TexReadInfo *raw_info = SAFE_MEMBER(rare_data, bt_rawInfo);
	U8 iThreshold = pInstance->iMouseRegion;
	bool bExceedsThreshold;
	U8* pColor;
	if (!raw_info)
		return 0; // Bad image specified, hopefully errored elsewhere
	assert(raw_info && raw_info->texture_data);
	verify(uncompressRawTexInfo(raw_info,textureMipsReversed(tex)));

	if (pInstance->pNinePatch)
	{

	}
	else
	{
		iMouseX -= pBox->lx;
		iMouseY -= pBox->ly;
		iMouseX *= raw_info->width / CBoxWidth(pBox);
		iMouseY *= raw_info->height / CBoxHeight(pBox);
	}

	pColor = &raw_info->texture_data[(iMouseX + iMouseY*raw_info->width)*4];

	gfxDrawBox(
		pBox->lx + iMouseX * (CBoxWidth(pBox) / raw_info->width),
		pBox->ly + iMouseY * (CBoxHeight(pBox) / raw_info->height), 
		pBox->lx + (iMouseX+1) * (CBoxWidth(pBox) / raw_info->width),
		pBox->ly + (iMouseY+1) * (CBoxHeight(pBox) / raw_info->height), 
		UI_INFINITE_Z, ColorCyan);
	
	if (pInstance->bAdditive)
	{
		U8 b = pColor[0];
		U8 g = pColor[1];
		U8 r = pColor[2];
		U8 a = pColor[3];
		//printf("(%u, %u, %u, %u)\n", (r*a)/255, (g*a)/255, (b*a)/255, a);
		bExceedsThreshold = (r*a)/255 >= iThreshold || (g*a)/255 >= iThreshold || (b*a)/255 >= iThreshold;
	}
	else
	{
		//printf("%u\n", pColor[3]);
		bExceedsThreshold = (pColor[3] >= iThreshold);
	}

	texUnloadRawData(tex);
	return bExceedsThreshold;
}

bool ui_GenInsideMouseRegion(const CBox *pBox, UIGenTextureAssembly *pAssembly, bool bClip)
{
	CBox box = *pBox;
	S32 iCurrentX = gInput->mouseInpCur.x;
	S32 iCurrentY = gInput->mouseInpCur.y;
	UITextureInstance *pInstance = ui_TextureAssemblyGetMouseRegion(ui_GenTextureAssemblyGetAssembly(NULL, pAssembly));
	//inpMousePos(&iCurrentX, &iCurrentY);
	
	if (false && pInstance) // Temporarily disabling, no need to have this running yet. 
	{
		F32 fWidth = CBoxWidth(&box);
		F32 fHeight = CBoxHeight(&box);
		if (pInstance->LeftFrom.fOffset >= 1)
			box.lx += pInstance->LeftFrom.fOffset;
		else
			box.lx += fWidth * pInstance->LeftFrom.fOffset;

		if (pInstance->TopFrom.fOffset >= 1)
			box.ly += pInstance->TopFrom.fOffset;
		else
			box.ly += fWidth * pInstance->TopFrom.fOffset;

		if (pInstance->RightFrom.fOffset >= 1)
			box.hx -= pInstance->RightFrom.fOffset;
		else
			box.hx -= fWidth * pInstance->RightFrom.fOffset;

		if (pInstance->BottomFrom.fOffset >= 1)
			box.hy -= pInstance->BottomFrom.fOffset;
		else
			box.hy -= fWidth * pInstance->BottomFrom.fOffset;

		if (bClip)
		{
			const CBox *pClipBox = mouseClipGet();
			CBoxClipTo(pClipBox, &box);
		}
		
		if (point_cbox_clsn(iCurrentX, iCurrentY, &box))
			return ui_GenTextureAssemblyRegionCheck(pInstance, pBox, iCurrentX, iCurrentY);
		else
			return false;
	}
	else
	{
		if (bClip)
		{
			const CBox *pClipBox = mouseClipGet();
			CBoxClipTo(pClipBox, &box);
		}
		return point_cbox_clsn(iCurrentX, iCurrentY, &box);
	}
}

bool ui_GenGetMouseEvents(const CBox *pBox,
						  UIGenTextureAssembly *pAssembly,
						  bool *pbCollision,
						  bool abIsDown[MS_MAXBUTTON],
						  bool abDownEvent[MS_MAXBUTTON],
						  bool abDownWasOver[MS_MAXBUTTON],
						  bool abUpEvent[MS_MAXBUTTON],
						  bool abDoubleClickEvent[MS_MAXBUTTON],
						  bool abDragEvent[MS_MAXBUTTON])
{
	S32 i;
	if (MouseInputIsAllowed() && pBox)
	{
		const CBox *pClipBox = mouseClipGet();
		CBox box = *pBox;
		S32 iCurrentX = gInput->mouseInpCur.x;
		S32 iCurrentY = gInput->mouseInpCur.y;
		S32 j;

		CBoxClipTo(pClipBox, &box);
		if (CBoxWidth(&box) >= 1 && CBoxHeight(&box) >= 1)
		{
			*pbCollision = ui_GenInsideMouseRegion(pBox, pAssembly, true);
			for (i = 0; i < ARRAY_SIZE_CHECKED(gInput->buttons); i++)
			{
				S32 iDownX = gInput->buttons[i].downx;
				S32 iDownY = gInput->buttons[i].downy;
				abIsDown[i] = (gInput->buttons[i].state == MS_DOWN);
				abDownWasOver[i] = abIsDown[i] && mouseBoxCollisionTest(iDownX, iDownY, &box);
			}

			for (j = 0; j < gInput->mouseBufSize; j++)
			{
				S32 iX = gInput->mouseInpBuf[j].x;
				S32 iY = gInput->mouseInpBuf[j].y;
				if (mouseBoxCollisionTest(iX, iY, &box))
				{
					for (i = 0; i < ARRAY_SIZE_CHECKED(gInput->mouseInpBuf[j].states); i++)
					{
						switch (gInput->mouseInpBuf[j].states[i])
						{
						case MS_DOWN:
							abDownEvent[i] = true;
							break;
						case MS_UP:
							abUpEvent[i] = true;
							break;
						case MS_DRAG:
							abDragEvent[i] = true;
							break;
						case MS_DBLCLICK:
							abDoubleClickEvent[i] = true;
							break;
						}
					}
				}
			}
			return true;
		}
	}
	else if (gInput)
	{
		*pbCollision = false;
		// even if no box is present, mouse buttons can still be down.
		for (i = 0; i < ARRAY_SIZE_CHECKED(gInput->buttons); i++)
		{
			S32 iDownX = gInput->buttons[i].downx;
			S32 iDownY = gInput->buttons[i].downy;
			abIsDown[i] = (gInput->buttons[i].state == MS_DOWN);
			if (pBox)
				abDownWasOver[i] = abIsDown[i] && mouseBoxCollisionTest(iDownX, iDownY, pBox);
		}
	}
	return false;
}


bool ui_GenTickMouse(UIGen *pGen)
{
	if (ui_GenInState(pGen, kUIGenStateXbox))
		return false;
	else
	{
		CBox *pBox = &pGen->UnpaddedScreenBox;
		UIGenTextureAssembly *pAssembly = SAFE_MEMBER(pGen, pResult) ? pGen->pResult->pAssembly : NULL;
		bool bHover = false;
		bool abDownEvent[MS_MAXBUTTON] = {0};
		bool abDownWasOver[MS_MAXBUTTON] = {0};
		bool abUpEvent[MS_MAXBUTTON] = {0};
		bool abDoubleClickEvent[MS_MAXBUTTON] = {0};
		bool abDragEvent[MS_MAXBUTTON] = {0};
		bool abIsDown[MS_MAXBUTTON] = {0};
		bool abClick[MS_MAXBUTTON] = {0};
		bool abDownStartedOver[MS_MAXBUTTON] = {0};
		bool bDragHandled = false;
		bool bAny = false;
		bool bInside;
		bool bDragMightStart = !(ui_GenInState(pGen, kUIGenStateLeftMouseDrag) || ui_GenInState(pGen, kUIGenStateRightMouseDrag));

		bInside = ui_GenInsideMouseRegion(pBox, pAssembly, false);

		ui_GenGetMouseEvents(&pGen->UnpaddedScreenBox, pAssembly, &bHover, abIsDown, abDownEvent, abDownWasOver, abUpEvent, abDoubleClickEvent, abDragEvent);

		if (pGen->pResult && pGen->pResult->bFocusOnClick && (abDownEvent[MS_LEFT] || abDownEvent[MS_RIGHT]))
			ui_GenSetFocus(pGen);

		// TODO(AMA):
		//
		//   This  needs  some  cleanup. 
		// I'll  be  the  one  to  do  it. 
		//       Infinite  sadness.
		//
		//                    ~ a haiku
		abDownStartedOver[MS_LEFT] = (ui_GenInState(pGen, kUIGenStateLeftMouseDownStartedOver) || abDownEvent[MS_LEFT]) && mouseIsDown(MS_LEFT);
		abDownStartedOver[MS_RIGHT] = (ui_GenInState(pGen, kUIGenStateRightMouseDownStartedOver) || abDownEvent[MS_RIGHT]) && mouseIsDown(MS_RIGHT);

		// Both down and up must be inside to register a click.
		abClick[MS_LEFT] = bHover && ui_GenInState(pGen, kUIGenStateLeftMouseDownStartedOver) && !mouseIsDown(MS_LEFT);
		abClick[MS_RIGHT] = bHover && ui_GenInState(pGen, kUIGenStateRightMouseDownStartedOver) && !mouseIsDown(MS_RIGHT);

		// Mouse is down if it's down this frame or last down was here.
		// the && bHover ensures that nothing else handled the input event,
		// as DownWasOver ignores capturing.
		abDownEvent[MS_LEFT] = abDownEvent[MS_LEFT] || (abDownWasOver[MS_LEFT] && bHover);
		abDownEvent[MS_RIGHT] = abDownEvent[MS_RIGHT] || (abDownWasOver[MS_RIGHT] && bHover);

		// Down must be within to register a double-click.
		abDoubleClickEvent[MS_LEFT] &= abDownEvent[MS_LEFT];
		abDoubleClickEvent[MS_RIGHT] &= abDownEvent[MS_RIGHT];

		// If we were dragging, and the mouse is still down, keep dragging.
		abDragEvent[MS_LEFT] = abIsDown[MS_LEFT] && (abDragEvent[MS_LEFT] || ui_GenInState(pGen, kUIGenStateLeftMouseDrag));
		abDragEvent[MS_RIGHT] = abIsDown[MS_RIGHT] && (abDragEvent[MS_RIGHT] || ui_GenInState(pGen, kUIGenStateRightMouseDrag));

		ui_GenStates(pGen,
			kUIGenStateMouseInside, bInside,
			kUIGenStateMouseOutside, !bInside,
			kUIGenStateMouseOver, bHover,
			kUIGenStateMouseNotOver, !bHover,
			kUIGenStateMouseDown, s_bNewMouseOverBehavior ? bHover && (mouseIsDown(MS_LEFT) || mouseIsDown(MS_RIGHT)) : abDownEvent[MS_LEFT] || abDownEvent[MS_RIGHT],
			kUIGenStateMouseUp, abUpEvent[MS_LEFT] || abUpEvent[MS_RIGHT],
			kUIGenStateMouseClick, abClick[MS_LEFT] || abClick[MS_RIGHT],
			kUIGenStatePressed, (abDownStartedOver[MS_LEFT] || abDownStartedOver[MS_RIGHT]) && bHover,
			kUIGenStateLeftMousePressed, abDownStartedOver[MS_LEFT] && bHover,
			kUIGenStateRightMousePressed, abDownStartedOver[MS_RIGHT] && bHover,
			kUIGenStateLeftMouseDownStartedOver, abDownStartedOver[MS_LEFT],
			kUIGenStateRightMouseDownStartedOver, abDownStartedOver[MS_RIGHT],
			kUIGenStateLeftMouseDownOutside, abIsDown[MS_LEFT] && !abDownWasOver[MS_LEFT],
			kUIGenStateRightMouseDownOutside, abIsDown[MS_RIGHT] && !abDownWasOver[MS_RIGHT],
			kUIGenStateMouseDownOutside, (abIsDown[MS_LEFT] && !abDownWasOver[MS_LEFT]) || (abIsDown[MS_RIGHT] && !abDownWasOver[MS_RIGHT]),
			kUIGenStateLeftMouseDown, s_bNewMouseOverBehavior ? bHover && mouseIsDown(MS_LEFT) : abDownEvent[MS_LEFT],
			kUIGenStateLeftMouseUp, abUpEvent[MS_LEFT],
			kUIGenStateLeftMouseDrag, abDragEvent[MS_LEFT],
			kUIGenStateLeftMouseClick, abClick[MS_LEFT],
			kUIGenStateLeftMouseDoubleClick, abDoubleClickEvent[MS_LEFT],
			kUIGenStateRightMouseDown, s_bNewMouseOverBehavior ? bHover && mouseIsDown(MS_RIGHT): abDownEvent[MS_RIGHT],
			kUIGenStateRightMouseUp, abUpEvent[MS_RIGHT],
			kUIGenStateRightMouseDrag, abDragEvent[MS_RIGHT],
			kUIGenStateRightMouseClick, abClick[MS_RIGHT],
			kUIGenStateRightMouseDoubleClick, abDoubleClickEvent[MS_RIGHT],
			kUIGenStateNone);

		if (ui_GenDragWasDropped(pBox))
			bDragHandled = ui_GenDragDropAccept(pGen);
		else if (ui_GenIsDragging())
		{
			int bValid = !!ui_GenDragDropWillAccept(pGen);
			ui_GenStates(pGen,
				kUIGenStateDropTargetValid, bValid,
				kUIGenStateDropTargetInvalid, !bValid,
				kUIGenStateNone);
		}
		else
		{
			ui_GenStates(pGen,
				kUIGenStateDropTargetValid, 0,
				kUIGenStateDropTargetInvalid, 0,
				kUIGenStateNone);
		}

		if (g_GenState.bHighlight && bHover && (inpLevelPeek(INP_SHIFT) || inpLevelPeek(INP_CONTROL)) && !pGen->bIsRoot && !pGen->bNoHighlight)
		{
			if (!UI_GEN_READY(g_GenState.pHighlighted))
			{
				ui_GenSetHighlighted(pGen);
			}
			else
			{
				UIGen *pParent = pGen->pParent;
				for (; pParent; pParent = pParent->pParent)
				{
					if (pParent == g_GenState.pHighlighted)
					{
						ui_GenSetHighlighted(pGen);
						break;
					}
				}
			}
		}
		else if (!bHover && g_GenState.pHighlighted == pGen && (inpLevelPeek(INP_SHIFT) || inpLevelPeek(INP_CONTROL)))
			ui_GenSetHighlighted(NULL);

		if (pGen->pResult && GET_REF(pGen->pResult->hCursor) && bHover)
			ui_SetCursorByPointer(GET_REF(pGen->pResult->hCursor));

		bAny = bHover || bDragHandled || abDownEvent[MS_LEFT] || abDownEvent[MS_RIGHT] || abClick[MS_LEFT] || abClick[MS_RIGHT];
		// Eat drag only on the frame in which it is started; otherwise, we still want
		// MouseOver / MouseUp events even when something is dragging.
		if (bDragMightStart && (abDragEvent[MS_LEFT] || abDragEvent[MS_RIGHT]))
			bAny = true;
		return bAny;
	}
}

__forceinline void ui_GenUpdateMouseRowStates(SA_PARAM_NN_VALID UIGen *pMoused, UIGen *pRowGen)
{
	if (pRowGen)
	{
		ui_GenStates(pRowGen,
			kUIGenStateMouseInsideRow, ui_GenInState(pMoused, kUIGenStateMouseInside),
			kUIGenStateMouseOutsideRow, ui_GenInState(pMoused, kUIGenStateMouseOutside),
			kUIGenStateMouseOverRow, ui_GenInState(pMoused, kUIGenStateMouseOver),
			kUIGenStateMouseNotOverRow, ui_GenInState(pMoused, kUIGenStateMouseNotOver),
			kUIGenStateNone);
	}
}

__forceinline void ui_GenClearMouseRowStates(UIGen *pRowGen)
{
	if (pRowGen)
	{
		ui_GenStates(pRowGen,
			kUIGenStateMouseInsideRow, 0,
			kUIGenStateMouseOutsideRow, 1,
			kUIGenStateMouseOverRow, 0,
			kUIGenStateMouseNotOverRow, 1,
			kUIGenStateNone);
	}
}

GEN_FORCE_INLINE_L2 static void ui_GenTick(UIGen *pGen)
{
	UIGenInternal *pResult = pGen->pResult;
	UIGenLoopFunc cbTickEarly = g_GenState.aFuncTable[pResult->eType].cbTickEarly;
	bool bMouseInteracted;
	bool bClip = pGen->pResult->bClipInput;
	bool bResetClip = pGen->pResult->bResetInputClip;
	bool bOver = ui_GenInState(pGen, kUIGenStateMouseOver);
	bool bUseXFormMatrix = UI_GEN_USE_MATRIX(pResult);

	if (bUseXFormMatrix)
	{
		Mat3 m3Prev, m3Inverse;
		if (inputMatrixGet())
			copyMat3(*inputMatrixGet(), m3Prev);
		else
			copyMat3(unitmat, m3Prev);
		inputMatrixPush();
		if (invertMat3(pGen->TransformationMatrix, m3Inverse))
			mulMat3(m3Inverse, m3Prev, *inputMatrixGet());
	}

	ui_GenStates(pGen,
		kUIGenStateFocused, g_GenState.pFocused == pGen,
		kUIGenStateFocusedAncestor, pGen->pParent && (g_GenState.pFocused == pGen->pParent || ui_GenInState(pGen->pParent, kUIGenStateFocusedAncestor)),
		kUIGenStateNone);

	if (bResetClip && bClip)
		mouseClipPush(&pGen->UnpaddedScreenBox);
	else if (bClip)
		mouseClipPushRestrict(&pGen->UnpaddedScreenBox);
	else if (bResetClip)
		mouseClipPush(NULL);

	if (pResult->pBeforeTick)
		ui_GenTimeAction(pGen, pResult->pBeforeTick, "BeforeTick");
	ui_GenForEachInPriority(&pGen->pResult->eaInlineChildren, ui_GenTickCB, pGen, UI_GEN_TICK_ORDER);
	ui_GenChildForEachInPriority(&pGen->pResult->eaChildren, ui_GenTickCB, pGen, UI_GEN_TICK_ORDER);
	if (cbTickEarly)
		cbTickEarly(pGen);

	bMouseInteracted = ui_GenTickMouse(pGen);
	if ((pResult->bFocusOnClick || pResult->bCaptureMouse) && bMouseInteracted)
		inpHandled();
	if (pResult->bCaptureMouseWheel
		&& ui_GenInState(pGen, kUIGenStateMouseInside))
		mouseCaptureZ();
	if (pGen->pResult->pTooltip && (pGen->pResult->pTooltip->bForceTooltipOwnership || ui_GenInState(pGen, kUIGenStateMouseOver)))
	{
		ui_GenTooltipSet(pGen);
		if (!bOver)
		{
			// Entered MouseOver this frame
			ui_GenSetTooltipFocus(pGen);
		}
	}
	else if (bOver && !ui_GenInState(pGen, kUIGenStateMouseOver) && g_GenState.pTooltipFocused == pGen)
		ui_GenSetTooltipFocus(NULL);

	ui_GenState(pGen, kUIGenStateSelectedInFocusPath,
		ui_GenInState(pGen, kUIGenStateSelected)
		&& (ui_GenInState(pGen, kUIGenStateFocused)
			|| ui_GenInState(pGen, kUIGenStateFocusedChild)
			|| ui_GenInState(pGen, kUIGenStateFocusedAncestor)
			));

	// Note: The MouseOverRow behavior of UIGenListRow is a single case where
	// a TickLate is partially useful. Though in general, TickLate is not
	// useful. If you believe that you have found another case TickLate, please
	// talk to Alex or Joshua before extending this quick hack.
	if (pResult->eType == kUIGenTypeListRow)
	{
		if (ui_GenInState(pGen, kUIGenStateMouseInside))
		{
			UIGenListRowState *pListRowState = UI_GEN_STATE(pGen, ListRow);
			UIGenListState *pListState = pListRowState && pListRowState->pList ? UI_GEN_STATE(pListRowState->pList, List): NULL;
			pListState->iMouseInsideRow = pListRowState->iRow;
			pListState->iMouseInsideCol = pListRowState->iCol;
		}
	}
	else if (pResult->eType == kUIGenTypeList)
	{		
		UIGenListState *pState = UI_GEN_STATE(pGen, List);
		UIGen *pMousedGen;
		if(eaSize(&pState->eaRows))
		{
			if (pState->iMouseInsideRow != pState->iLastMouseInsideRow)
			{
				if (pState->iLastMouseInsideRow != -1)
					ui_GenClearMouseRowStates(eaGet(&pState->eaRows, pState->iLastMouseInsideRow));
				if (pState->iMouseInsideRow != -1)
					ui_GenUpdateMouseRowStates(eaGet(&pState->eaRows, pState->iMouseInsideRow), eaGet(&pState->eaRows, pState->iMouseInsideRow));
			}
		}
		else if (eaSize(&pState->eaCols))
		{
			if (pState->iMouseInsideRow != pState->iLastMouseInsideRow)
			{
				int iCol;
				UIGen *pMousedColGen = eaGet(&pState->eaCols, pState->iMouseInsideCol);
				UIGenListColumnState *pMousedColState = pMousedColGen ? UI_GEN_STATE(pMousedColGen, ListColumn) : NULL;
				pMousedGen = eaGet(&pMousedColState->eaRows, pState->iMouseInsideRow);
				for (iCol = 0; iCol < eaSize(&pState->eaCols); iCol++)
				{
					UIGen *pColumn = eaGet(&pState->eaCols, iCol);
					if (pColumn)
					{
						UIGenListColumnState *pColState = UI_GEN_STATE(pState->eaCols[iCol], ListColumn);
						if (pState->iLastMouseInsideRow != -1)
							ui_GenClearMouseRowStates(eaGet(&pColState->eaRows, pState->iLastMouseInsideRow));
						if (pMousedGen)
							ui_GenUpdateMouseRowStates(pMousedGen, eaGet(&pColState->eaRows, pState->iMouseInsideRow));
					}
				}
			}
		}
		pState->iLastMouseInsideRow = pState->iMouseInsideRow;
	}

	if (bClip || bResetClip)
		mouseClipPop();

	if (bUseXFormMatrix)
		inputMatrixPop();
}

bool ui_GenCheckSpriteCache(UIGen *pGen)
{
	if (pGen->pSpriteCache)
	{
		if (pGen->bNeedsRebuild
			// If the gen needs to be set up at all, it needs to be updated.
			|| !pGen->pResult

			// Not caught by mouseInvolvedWith - this tells us about *last* frame.
			// So if the mouse moved off us this frame, make sure to process that ASAP.
			|| ui_GenInState(pGen, kUIGenStateMouseInside)

			// If the gen's involved in the focus path, it needs to be ready for key events.
			|| (g_ui_State.chInputDidAnything
			&& (ui_GenInState(pGen, kUIGenStateFocused)
			|| ui_GenInState(pGen, kUIGenStateFocusedChild)))

			// During drags, pretty much everything needs to be updated.
			|| ui_GenIsDragging()

			// If any mouse events happened this frame, be ready for them - maybe not 100%
			// accurate in case the gen's box itself changed, but if we don't update,
			// we also won't do anything with the click.
			|| mouseInvolvedWith(&pGen->ChildBoundingBox)

			// If a child of the current gen has tooltip focus
			|| pGen->pSpriteCache->bTooltipFocus
			)
		{
			return true;
		}
	}
	return false;
}


bool ui_GenTickCB(UIGen *pChild, UIGen *pParent)
{
	if (UI_GEN_READY(pChild)
		&& !pChild->bPopup
		&& !(pChild->pParent && pChild->pParent->bTopLevelChildren)
		&& (!pChild->pSpriteCache || pChild->pSpriteCache->iAccumulate <= 0)
		)
	{
		GEN_PERFINFO_START(pChild);
		ui_GenTick(pChild);
		GEN_PERFINFO_STOP(pChild);
	}
	return false;
}

static const char *ui_GenInstanceType(UIGen *pGen)
{
	S32 i;

	if (!pGen)
		return "null";

	if (ui_GenInDictionary(pGen))
		return "Dictionary";

	// A placeholder fake root gen (e.g. layers, entity, jails)
	if (!pGen->pParent)
		return "FakeRootHolder";

	// Child is a clone of a borrowed inline child
	if (eaFind(&pGen->pParent->eaBorrowedInlineChildren, pGen) >= 0)
		return "BorrowedInlineChild";

	// Inline child from data
	if (pGen->pParent->pBase && eaFind(&pGen->pParent->pBase->eaInlineChildren, pGen) >= 0)
		return "InlineChild";
	if (pGen->pParent->pLast && eaFind(&pGen->pParent->pLast->eaInlineChildren, pGen) >= 0)
		return "InlineChild";
	for (i = eaSize(&pGen->pParent->eaStates) - 1; i >= 0; i--)
		if (pGen->pParent->eaStates[i]->pOverride && eaFind(&pGen->pParent->eaStates[i]->pOverride->eaInlineChildren, pGen) >= 0)
			return "InlineChild";
	for (i = eaSize(&pGen->pParent->eaComplexStates) - 1; i >= 0; i--)
		if (pGen->pParent->eaComplexStates[i]->pOverride && eaFind(&pGen->pParent->eaComplexStates[i]->pOverride->eaInlineChildren, pGen) >= 0)
			return "InlineChild";

	// Cloned type based on parent
	switch (pGen->pParent->eType)
	{
	case kUIGenTypeList:
		if (pGen->eType == kUIGenTypeListColumn)
			return "Column";
		return "Row";
	case kUIGenTypeListColumn:
		return "Row";
	case kUIGenTypeTabGroup:
		return "Tab";
	case kUIGenTypeLayoutBox:
		return "Instance";
	}

	// Possibly a clonable window
	return "unknown";
}

GEN_FORCE_INLINE_L2 static void ui_GenDraw(UIGen *pGen)
{
	UIGenInternal *pResult = pGen->pResult;
	UITextureAssembly *pAssembly = ui_GenTextureAssemblyGetAssembly(pGen, pResult->pAssembly);
	UITextureAssembly *pOverlayAssembly = ui_GenTextureAssemblyGetAssembly(pGen, pResult->pOverlayAssembly);
	UIGenLoopFunc cbDrawEarly = g_GenState.aFuncTable[pResult->eType].cbDrawEarly;
	bool bClip = pGen->pResult->bClip || pGen->pResult->bClipToPadding;
	bool bResetClip = pGen->pResult->bResetClip;
	// Only push a transformation matrix if there's a transform to apply. 
	bool bUseXFormMatrix = UI_GEN_USE_MATRIX(pResult);
	CBox ClipBox = pGen->UnpaddedScreenBox;
	SpriteProperties *pSpriteProperties = GenSpritePropGetCurrent();
	bool is3D = pSpriteProperties && pSpriteProperties->is_3D;

	if (bUseXFormMatrix)
	{
		Mat3 m;
		if (gfxMatrixGet())
			copyMat3(*gfxMatrixGet(), m);
		else
			copyMat3(unitmat, m);
		gfxMatrixPush();
		mulMat3(m, pGen->TransformationMatrix, *gfxMatrixGet());
	}

	if (pAssembly)
	{
		F32 fMinZ = UI_GET_Z();
		F32 fMaxZ = UI_GET_Z();

		if (bClip && pAssembly)
		{
			// modify box based on max margins
			ClipBox.lx -= pAssembly->iClipMarginLeft;
			ClipBox.ly -= pAssembly->iClipMarginTop;
			ClipBox.hx += pAssembly->iClipMarginRight;
			ClipBox.hy += pAssembly->iClipMarginBottom;
		}

		if (bResetClip && bClip)
			clipperPush(&ClipBox);
		else if (bClip)
			clipperPushRestrict(&ClipBox);
		else if (bResetClip)
			clipperPush(NULL);

		if (pSpriteProperties)
		{
			ui_TextureAssemblyDrawEx(pAssembly, &pGen->UnpaddedScreenBox, NULL, pGen->fScale, fMinZ, fMaxZ, pGen->chAlpha, &pResult->pAssembly->Colors, pSpriteProperties->screen_distance, pSpriteProperties->is_3D);
		}
		else
			ui_TextureAssemblyDraw(pAssembly, &pGen->UnpaddedScreenBox, NULL, pGen->fScale, fMinZ, fMaxZ, pGen->chAlpha, &pResult->pAssembly->Colors);

		if (bClip || bResetClip)
			clipperPop();
	}

	if (pGen == g_GenState.pHighlighted)
	{
		static F32 fTime = 0;
		Color c;
		Color c2;
		Color c3;
		fTime += pGen->fTimeDelta;
		c = ColorLerp(ColorBlue, ColorWhite, fTime - genRound(fTime));
		c2 = ColorLerp(ColorRed, ColorWhite, fTime - genRound(fTime));
		c3 = ColorLerp(ColorGreen, ColorWhite, fTime - genRound(fTime));
		clipperPush(NULL);
		// Don't pad right/bottom as those values are already exclusive for the box.
		gfxDrawBox(pGen->ChildBoundingBox.lx - 1, pGen->ChildBoundingBox.ly - 1, pGen->ChildBoundingBox.hx, pGen->ChildBoundingBox.hy, UI_INFINITE_Z, c3);
		gfxDrawBox(pGen->UnpaddedScreenBox.lx - 1, pGen->UnpaddedScreenBox.ly - 1, pGen->UnpaddedScreenBox.hx, pGen->UnpaddedScreenBox.hy, UI_INFINITE_Z, c);
		gfxDrawBox(pGen->ScreenBox.lx - 1, pGen->ScreenBox.ly - 1, pGen->ScreenBox.hx, pGen->ScreenBox.hy, UI_INFINITE_Z, c2);
		gfxfont_SetFontEx(&g_font_Sans, false, false, 1, 0, 0xFFFFFFFF, 0xFFFFFFFF);
		gfxfont_PrintfEx(50, 50, is3D ? pSpriteProperties->screen_distance : UI_INFINITE_Z + 1, 1, 1, 0, is3D ? pSpriteProperties : NULL, "[%s] %s : %s", ui_GenInstanceType(pGen), pGen->pchName, pGen->pchFilename);
		if (isDevelopmentMode() && ui_GenInState(pGen, kUIGenStateRightMouseClick))
		{
			char achResolved[CRYPTIC_MAX_PATH];
			fileLocateWrite(pGen->pchFilename, achResolved);

			if(inpLevelPeek(INP_CONTROL) && inpLevelPeek(INP_ALT))
			{
				fileOpenWithEditor(achResolved);
			}

			if(inpLevelPeek(INP_CONTROL) && inpLevelPeek(INP_SHIFT))
			{
				winCopyToClipboard(achResolved);
			}
		}
		clipperPop();
	}
	if (g_GenState.bFocusHighlight && pGen == g_GenState.pFocused)
	{
		Color c = ColorOrange;
		clipperPush(NULL);
		gfxDrawBox(pGen->UnpaddedScreenBox.lx - 1, pGen->UnpaddedScreenBox.ly - 1, pGen->UnpaddedScreenBox.hx, pGen->UnpaddedScreenBox.hy, UI_INFINITE_Z, c);
		gfxfont_SetFontEx(&g_font_Sans, false, false, 1, 0, RGBAFromColor(c), RGBAFromColor(c));
		gfxfont_PrintfEx(50, 70, is3D ? pSpriteProperties->screen_distance : UI_INFINITE_Z + 1, 1, 1, 0, is3D ? pSpriteProperties : NULL, "(Focus) [%s] %s : %s", ui_GenInstanceType(pGen), pGen->pchName, pGen->pchFilename);
		clipperPop();
	}

	ClipBox = pGen->UnpaddedScreenBox;
	if (pAssembly && pGen->pResult->bClipToPadding)
	{
		// modify box based on padding
		ClipBox.lx += pAssembly->iPaddingLeft * pGen->fScale;
		ClipBox.ly += pAssembly->iPaddingTop * pGen->fScale;
		ClipBox.hx -= pAssembly->iPaddingRight * pGen->fScale;
		ClipBox.hy -= pAssembly->iPaddingBottom * pGen->fScale;
	}

	if (bResetClip && bClip)
		clipperPush(&ClipBox);
	else if (bClip)
		clipperPushRestrict(&ClipBox);
	else if (bResetClip)
		clipperPush(NULL);

	if (cbDrawEarly)
		cbDrawEarly(pGen);

	if (pGen->pResult->pBackground)
		g_GenState.pBackground = pGen->pResult->pBackground;
	
	ui_GenChildForEachInPriority(&pGen->pResult->eaChildren, ui_GenDrawCB, pGen, UI_GEN_DRAW_ORDER);
	ui_GenForEachInPriority(&pGen->pResult->eaInlineChildren, ui_GenDrawCB, pGen, UI_GEN_DRAW_ORDER);

	if (bClip || bResetClip)
		clipperPop();

	if (pOverlayAssembly)
	{
		F32 fMinZ = UI_GET_Z();
		F32 fMaxZ = UI_GET_Z();

		if (bClip)
		{
			// modify box based on max margins
			ClipBox = pGen->UnpaddedScreenBox;
			ClipBox.lx -= pOverlayAssembly->iClipMarginLeft;
			ClipBox.ly -= pOverlayAssembly->iClipMarginTop;
			ClipBox.hx += pOverlayAssembly->iClipMarginRight;
			ClipBox.hy += pOverlayAssembly->iClipMarginBottom;
		}

		if (bResetClip && bClip)
			clipperPush(&ClipBox);
		else if (bClip)
			clipperPushRestrict(&ClipBox);
		else if (bResetClip)
			clipperPush(NULL);

		ui_TextureAssemblyDraw(pOverlayAssembly, &pGen->UnpaddedScreenBox, NULL, pGen->fScale, fMinZ, fMaxZ, pGen->chAlpha, &pResult->pOverlayAssembly->Colors);

		if (bClip || bResetClip)
			clipperPop();
	}

	if (bUseXFormMatrix)
		gfxMatrixPop();
}

bool ui_GenDrawCB(UIGen *pChild, UIGen *pParent)
{
	if (UI_GEN_READY(pChild) && !pChild->bPopup && !(pChild->pParent && pChild->pParent->bTopLevelChildren))
	{
		SpriteProperties *pSpriteProperties = GenSpritePropGetCurrent();
		GEN_PERFINFO_START(pChild);

		if (pChild->pSpriteCache && pChild->pSpriteCache->iAccumulate > 0)
		{
			//Merge our saved list into the current frame's normal list.
			//we need to update the tex handles since they might be stale if they are from a previous frame
			if (!pSpriteProperties)
			{
				gfxMergeSpriteLists(gfxGetCurrentSpriteList(), pChild->pSpriteCache->pSprites, true); 
			
				g_ui_State.drawZ += pChild->pSpriteCache->fUsedZ;
			}
		}
		else
		{
			F32 fStartZ;
			// Seed based on an ever-increasing number so that updates
			// don't get synchronized on the same frame.
			static U32 s_uiCache = 0;
			bool bCreated = false;
			if (pChild->pSpriteCache)
			{
				if (pChild->pSpriteCache->pSprites)
				{
					//empty the list of the old sprites
					gfxClearSpriteList(pChild->pSpriteCache->pSprites); 
				}
				else
				{
					//make a new list and enable the tex handle update flag
					pChild->pSpriteCache->pSprites = gfxCreateSpriteList(100, true, true);
					bCreated = true;
				}

				//make sure we are drawing to the normal sprite list and aren't already
				//trying to compile a sprite list
				assert(gfxGetCurrentSpriteList() == gfxGetDefaultSpriteList());
				
				gfxSetCurrentSpriteList(pChild->pSpriteCache->pSprites);
				fStartZ = g_ui_State.drawZ;
				
			}
			ui_GenDraw(pChild);
			if (pChild->pSpriteCache)
			{
				//make sure nobody switched the list on us
				assert(gfxGetCurrentSpriteList() == pChild->pSpriteCache->pSprites);
				
				gfxSetCurrentSpriteList(gfxGetDefaultSpriteList()); //put it back to the old one
				if (pChild->pSpriteCache->iAccumulate <= 0)
				{
					// Add one because we decrement at the beginning of the next frame.
					if (bCreated)
						pChild->pSpriteCache->iAccumulate = (++s_uiCache) % (pChild->pSpriteCache->iFrameSkip + 1);
					else
						pChild->pSpriteCache->iAccumulate = pChild->pSpriteCache->iFrameSkip + 1;
				}
				pChild->pSpriteCache->fUsedZ = g_ui_State.drawZ - fStartZ;

				//draw the sprite list, we are sure the texture handles are up to date
				gfxMergeSpriteLists(gfxGetCurrentSpriteList(), pChild->pSpriteCache->pSprites, false); 
			}
		}
		GEN_PERFINFO_STOP(pChild);
	}
	return false;
}

static void ui_GenUpdateContext(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	static int s_iTutorial = 0;
	if (pGen->pParent)
		ui_GenUpdateContext(pGen->pParent, pContext, pFor);
	if (g_GenState.aFuncTable[pGen->eType].cbUpdateContext)
		g_GenState.aFuncTable[pGen->eType].cbUpdateContext(pGen, pContext, pFor);
	{
		ParseTable *pTable = NULL;
		void *pStruct = ui_GenGetPointer(pGen, NULL, &pTable);
		if (pTable)
			exprContextSetPointerVarPooledCached(pContext, g_ui_pchGenData, pStruct, pTable, true, true, &g_ui_iGenData);
	}
	if (pFor == pGen)
	{
		ParseTable *pTable = NULL;
		void *pStruct = ui_GenGetPointer(pGen, NULL, &pTable);
		UIGen *pFocused = g_GenState.pFocused;
		static int s_iSelf = 0;
		static int s_iFocusedGen = 0;

		exprContextSetPointerVarPooledCached(pContext, s_pchSelf, pFor, parse_UIGen, true, true, &s_iSelf);
		exprContextSetPointerVarPooledCached(pContext, s_pchFocusedGen, pFocused ? pFocused : pFor, parse_UIGen, true, true, &s_iFocusedGen);
		exprContextSetUserPtr(pContext, pFor, parse_UIGen);
		exprContextSetPartition(pContext, PARTITION_CLIENT);
	}
	if (pGen->pTutorialInfo)
		exprContextSetPointerVarPooledCached(pContext, s_pchTutorial, pGen->pTutorialInfo, parse_UIGenTutorialInfo, true, true, &s_iTutorial);
}

ExprContext *ui_GenGetContext(UIGen *pGen)
{
	GenExprContext *pGenContext = eaGet(&g_GenState.eaContexts, g_GenState.iCurrentContextDepth);
	devassertmsg(pGenContext, "No gen context for this stack depth, something is horribly wrong.");
	if (pGen != pGenContext->pLastGen)
	{
		if (pGenContext->pContext != g_GenState.pContext)
			exprContextClear(pGenContext->pContext);
		if (pGen)
			ui_GenUpdateContext(pGen, pGenContext->pContext, pGen);
		pGenContext->pLastGen = pGen;
	}
	return pGenContext->pContext;
}

static bool GenExpressionIsSafe(const UIGen *pGen, const Expression *pExpr, ExprContext *pContext, bool bErrofDetails)
{
	static const char *s_pchRowData;
	static const char *s_pchGenData;
	static const char *s_pchGenInstanceData;
	static const char *s_pchTabData;
	bool bRowData = false;
	bool bGenData = false;
	bool bGenInstanceData = false;
	bool bTabData = false;
	S32 iSize = beaSize(&pExpr->postfixEArray);
	S32 i;
	const UIGen *pTmp = NULL;

	GEN_PERFINFO_START_DEFAULT(__FUNCTION__, 0);

	if (!s_pchRowData)
		s_pchRowData = allocFindString("RowData");
	if (!s_pchGenData)
		s_pchGenData = allocFindString("GenData");
	if (!s_pchGenInstanceData)
		s_pchGenInstanceData = allocFindString("GenInstanceData");
	if (!s_pchTabData)
		s_pchTabData = allocFindString("TabData");

	if(isDevelopmentMode())
	{
		assertmsg(s_pchRowData && s_pchGenData && s_pchGenInstanceData && s_pchTabData,
			"Critical gen expression variable names could not be found.");
	}

	for (i = 0; i < iSize; i++)
	{
		const char *pchName = exprMultiValGetVarName(&pExpr->postfixEArray[i], pContext);
		if (pchName)
		{
			bRowData = bRowData || pchName == s_pchRowData;
			bGenData = bGenData || pchName == s_pchGenData;
			bGenInstanceData = bGenInstanceData || pchName == s_pchGenInstanceData;
			bTabData = bTabData || pchName == s_pchTabData;
		}
	}

	// It's basically never safe to use GenData if we're not updated.
	if (bGenData && pGen->uiFrameLastUpdate != g_ui_State.uiFrameCount)
	{
		if(bErrofDetails)
		{
			Errorf("Unsafe use of GenData.");
		}
		GEN_PERFINFO_STOP_DEFAULT();
		return false;
	}

	// It's safe to use RowData if the innermost list has updated.
	if (bRowData)
	{
		for (pTmp = pGen; pTmp && pTmp->eType != kUIGenTypeList && !pTmp->bIsRoot; pTmp = pTmp->pParent);
		if (!pTmp || pTmp->bIsRoot || pTmp->uiFrameLastUpdate != g_ui_State.uiFrameCount)
		{
			if(bErrofDetails)
			{
				Errorf("Unsafe use of RowData.");
			}
			GEN_PERFINFO_STOP_DEFAULT();
			return false;
		}
	}

	// It's safe to use GenInstanceData if the innermost layout box has updated.
	if (bGenInstanceData)
	{
		for (pTmp = pGen; pTmp && pTmp->eType != kUIGenTypeLayoutBox && !pTmp->bIsRoot; pTmp = pTmp->pParent);
		if (!pTmp || pTmp->bIsRoot || pTmp->uiFrameLastUpdate != g_ui_State.uiFrameCount)
		{
			if(bErrofDetails)
			{
				Errorf("Unsafe use of GenInstanceData.");
			}
			GEN_PERFINFO_STOP_DEFAULT();
			return false;
		}
	}

	// It's safe to use TabData if the innermost tab group has updated.
	if (bTabData)
	{
		for (pTmp = pGen; pTmp && pTmp->eType != kUIGenTypeTabGroup && !pTmp->bIsRoot; pTmp = pTmp->pParent);
		if (!pTmp || pTmp->bIsRoot || pTmp->uiFrameLastUpdate != g_ui_State.uiFrameCount)
		{
			if(bErrofDetails)
			{
				Errorf("Unsafe use of TabData.");
			}
			GEN_PERFINFO_STOP_DEFAULT();
			return false;
		}
	}

	// If this gen updated, all its parents should be updated too.
	if (pGen->uiFrameLastUpdate == g_ui_State.uiFrameCount)
	{
		for (pTmp = pGen; pTmp && !pTmp->bIsRoot; pTmp = pTmp->pParent)
		{
			if (!pTmp->bIsRoot && pTmp->uiFrameLastUpdate != g_ui_State.uiFrameCount)
			{
				if(bErrofDetails)
				{
					Errorf("Parent %s has not been updated.", pTmp->pchName);
				}
				GEN_PERFINFO_STOP_DEFAULT();
				return false;
			}
		}
	}

	GEN_PERFINFO_STOP_DEFAULT();
	return true;
}

bool ui_GenEvaluateWithContext(UIGen *pGen, Expression *pExpr, MultiVal *pValue, ExprContext *pContext)
{
	MultiVal mv = {0};
	if (pValue == NULL)
		pValue = &mv;
	if (pGen)
	{
		if(isDevelopmentMode()
			&& !GenExpressionIsSafe(pGen, pExpr, pContext, true))
		{
			ErrorFilenamef(pGen->pchFilename, "%s: Running expression without updating this frame:\n\n%s",
				pGen->pchName, exprGetCompleteString(pExpr));
			return false;
		}
		else if (pGen->uiFrameLastUpdate != g_ui_State.uiFrameCount)
		{
			// some mismatched frame counts indicate a fatal condition
			// check to make sure this isn't one of them
			static U32 uLastErrorFTime = 0;
			bool bDoErrorf = false;
			if(timeSecondsSince2000() > uLastErrorFTime)
			{
				bDoErrorf = true;
			}
			if(!GenExpressionIsSafe(pGen, pExpr, pContext, bDoErrorf))
			{
				if(bDoErrorf)
				{
					ErrorFilenamef(pGen->pchFilename, "%s: Running expression without updating this frame:\n\n%s",
						pGen->pchName, exprGetCompleteString(pExpr));

					uLastErrorFTime = timeSecondsSince2000();	// on dev mode do errorf max of ~once per second
				}
				return false;
			}
		}

		g_GenState.iExpressionsRun++;
		g_GenState.iCurrentContextDepth++;
		if (g_GenState.iCurrentContextDepth == eaSize(&g_GenState.eaContexts))
		{
			GenExprContext *pGenContext = calloc(1, sizeof(*pGenContext));
			ExprContext *pNewContext = exprContextCreateEx(SCRATCHSTACK_DEFAULT_SIZE_LARGE MEM_DBG_PARMS_INIT);
			exprContextSetFuncTable(pNewContext, g_GenState.stGenFuncTable);
			assert(g_GenState.eaContexts); // make /analyze shut up.
			exprContextSetParent(pNewContext, g_GenState.pContext);
			pGenContext->pContext = pNewContext;
			exprContextSetStaticCheck(pNewContext, ExprStaticCheck_AllowTypeChanges);
			eaPush(&g_GenState.eaContexts, pGenContext);
			GEN_PRINTF("Allocating new context for evaluation stack depth %d.", g_GenState.iCurrentContextDepth);
		}
		devassertmsg(g_GenState.iCurrentContextDepth <= eaSize(&g_GenState.eaContexts), "Context evaluation depth is out of sync, oh crap.");
		if (g_GenState.pchDumpExpressions)
		{
			eaPush(&g_GenState.eapchExprs, pGen->pchFilename);
			eaPush(&g_GenState.eapchExprs, pGen->pchName);
			eaPush(&g_GenState.eapchExprs, pExpr->lines[0]->origStr);
		}
		exprEvaluate(pExpr, pContext, pValue);
		g_GenState.iCurrentContextDepth--;
	}
	devassertmsg(!MULTI_IS_FREE(pValue->type), "Gen expressions must not return a freeable type.");
	return MultiValToBool(pValue);
}

__forceinline static F32 ui_GenPositionSpecToScalar(UISizeSpec *pSpec, F32 fParent, F32 fFitContents, F32 fFitParent, F32 fRatio, F32 fMargin, F32 fScale)
{
	switch (pSpec->eUnit)
	{
		// For percentage sizes, we should not scale (since the parent is already scaled), and
		// we should subtract the margin. For all other types, the margin is already handled.
	case UIUnitPercentage:
		return pSpec->fMagnitude * fParent - fScale * fMargin;

		// For fitting to contents, we should scale, because our contents will scale.
	case UIUnitFitContents:
		return pSpec->fMagnitude * fFitContents * fScale;

		// For fitting to parent, we should not scale, because our parent is already scaled.
	case UIUnitFitParent:
		return pSpec->fMagnitude * fFitParent;

		// Do not scale Ratios because the other dimension is has already taken care of any scaling 
	case UIUnitRatio:
		return pSpec->fMagnitude * fRatio;

	default:
		return pSpec->fMagnitude * fScale;
	}
}

__forceinline static void ui_GenPositionBoxOnScreen(CBox *pBox) {
	// If we still fail to fit on screen, then align with top/left/bottom/right of screen
	// If we're overflowing both edges (top & bottom) or (right & left), then give precedence to top & left.
	F32 fHeight = CBoxHeight(pBox);
	F32 fWidth = CBoxWidth(pBox);

	if (pBox->lx < 0 || fWidth > g_ui_State.screenWidth)
	{
		CBoxMoveX(pBox, 0);
	}
	else if (pBox->hx > g_ui_State.screenWidth)
	{
		CBoxMoveX(pBox, g_ui_State.screenWidth - fWidth);
	}

	if (pBox->ly < 0 || fHeight > g_ui_State.screenHeight)
	{
		CBoxMoveY(pBox, 0);
	}
	else if (pBox->hy > g_ui_State.screenHeight)
	{
		CBoxMoveY(pBox, g_ui_State.screenHeight - fHeight);
	}
}

__forceinline static void ui_GenRepositionBoxAroundAnchorGen(CBox *pBox, const CBox *pAnchorBox, UIDirection ePrimaryEdge, UIDirection eSecondaryEdge) 
{
	F32 fWidth = CBoxWidth(pBox);
	F32 fHeight = CBoxHeight(pBox);
	F32 fNewX = pBox->lx;
	F32 fNewY = pBox->ly;

	// Position such that that primary edge of pAnchorGen is against the opposite edge of pGen
	switch (ePrimaryEdge) 
	{
	case UILeft:
		fNewX = pAnchorBox->lx - fWidth;
		break;
	case UIRight:
		fNewX = pAnchorBox->hx;
		break;
	case UITop:
		fNewY = pAnchorBox->ly - fHeight;
		break;
	case UIBottom:
		fNewY = pAnchorBox->hy;
		break;
	}

	// Position such that that secondary edge of pAnchorGen is against the same edge of pGen
	// If both or neither secondary edges are defined, then center along the primary edge.
	switch (eSecondaryEdge) 
	{
	case UILeft:
		fNewX = pAnchorBox->lx;
		break;
	case UIRight:
		fNewX = pAnchorBox->hx - fWidth;
		break;
	case UITop:
		fNewY = pAnchorBox->ly;
		break;
	case UIBottom:
		fNewY = pAnchorBox->hy - fHeight;
		break;
	default:   		
		// Align center of anchor with center of box
		if (ePrimaryEdge & UIHorizontal) 
		{
			F32 fAnchorCenterY = (pAnchorBox->ly + pAnchorBox->hy) / 2;
			fNewY = fAnchorCenterY - (fHeight / 2);
		} else {
			F32 fAnchorCenterX = (pAnchorBox->lx + pAnchorBox->hx) / 2;
			fNewX = fAnchorCenterX - (fWidth / 2);
		}
	}

	CBoxMoveX(pBox, fNewX);
	CBoxMoveY(pBox, fNewY);
}

static void ui_GenRepositionAroundAnchor(const CBox *pParentBox, CBox *pBox, UIGenAnchor *pAnchor, bool bPositionOnScreen)
{
	const CBox* pAnchorBox = NULL;
	CBox OffsetBox;
	UIDirection ePrimaryEdge;
	UIDirection eSecondaryEdge;

	if (!pAnchor->pchName || !*pAnchor->pchName)
	{
		return;
	}
	else if (pAnchor->pchName == s_pch_Parent ) 
	{
		pAnchorBox = pParentBox;
	} 
	else 
	{
		UIGen *pAnchorGen = RefSystem_ReferentFromString(g_GenState.hGenDict, pAnchor->pchName);
		if (!UI_GEN_READY(pAnchorGen))
		{
			Errorf("Trying to position based on anchor %s, which does not exist or is not run before this gen", pAnchor->pchName);
			return;
		}
		pAnchorBox = &pAnchorGen->UnpaddedScreenBox;
	}

	if (pAnchor->iOffset)
	{
		OffsetBox = *pAnchorBox;
		OffsetBox.hx += pAnchor->iOffset;
		OffsetBox.hy += pAnchor->iOffset;
		OffsetBox.lx -= pAnchor->iOffset;
		OffsetBox.ly -= pAnchor->iOffset;
		pAnchorBox = &OffsetBox;
	}

	if (pAnchor->eOrientation == UIHorizontal)
	{
		// Primary edges are left/right
		ePrimaryEdge = pAnchor->eAlignment & UIHorizontal;
		eSecondaryEdge = pAnchor->eAlignment & UIVertical;
	}
	else if (pAnchor->eOrientation == UIVertical)
	{
		// Primary edges are top/bottom
		ePrimaryEdge = pAnchor->eAlignment & UIVertical;
		eSecondaryEdge = pAnchor->eAlignment & UIHorizontal;
	}

	ui_GenRepositionBoxAroundAnchorGen(pBox, pAnchorBox, ePrimaryEdge, eSecondaryEdge);

	if (bPositionOnScreen) 
	{
		F32 fHeight = CBoxHeight(pBox);
		F32 fWidth = CBoxWidth(pBox);
		UIDirection ePrimaryEdge2 = ePrimaryEdge;
		UIDirection eSecondaryEdge2 = eSecondaryEdge;

		// If the gen is off screen as a result, reposition around the anchor as needed
		if (pBox->lx < 0 || pBox->hx >= g_ui_State.screenWidth) 
		{
			if (ePrimaryEdge2 & UIHorizontal) 
			{
				ePrimaryEdge2 = (~ePrimaryEdge2) & UIHorizontal; // Swap left & right alignment
			}
			else
			{
				eSecondaryEdge2 = (~eSecondaryEdge2) & UIHorizontal; // Swap left & right alignment
			}
		}

		if (pBox->ly < 0 || pBox->hy >= g_ui_State.screenHeight) 
		{
			if (ePrimaryEdge2 & UIVertical) 
			{
				ePrimaryEdge2 = (~ePrimaryEdge2) & UIVertical; // Swap top & bottom alignment
			}
			else
			{
				eSecondaryEdge2 = (~eSecondaryEdge2) & UIVertical; // Swap top & bottom alignment
			}
		}

		if (ePrimaryEdge != ePrimaryEdge2 || eSecondaryEdge != eSecondaryEdge2)
		{
			ui_GenRepositionBoxAroundAnchorGen(pBox, pAnchorBox, ePrimaryEdge2, eSecondaryEdge2);

			// If we still fail to fit on screen, reset the alignment to be the originally desired
			// alignment and then make a final adjustment to get it on screen as close as possible
			// the originally desired position.
			if (pBox->lx < 0 || pBox->ly < 0 || pBox->hx >= g_ui_State.screenWidth || pBox->hy >= g_ui_State.screenHeight) 
			{
				ui_GenRepositionBoxAroundAnchorGen(pBox, pAnchorBox, ePrimaryEdge, eSecondaryEdge);
				ui_GenPositionBoxOnScreen(pBox);
			}
		}
	}
}


__forceinline F32 ui_GenPositionToCBoxDimension(UISizeSpec *pSpec, UISizeSpec *pMinSpec, UISizeSpec *pMaxSpec, F32 fParentSize, F32 fFitContentsSize, F32 fFitParentSize, F32 fRatioSize, F32 fMargin, F32 fScale)
{
	F32 fSize = ui_GenPositionSpecToScalar(pSpec, fParentSize, fFitContentsSize, fFitParentSize, fRatioSize, fMargin, fScale);
	if (pMinSpec->fMagnitude || pMinSpec->eUnit)
	{
		F32 fMin = ui_GenPositionSpecToScalar(pMinSpec, fParentSize, fFitContentsSize, fFitParentSize, fRatioSize, fMargin, fScale);
		if (fMin > fSize)
			fSize = fMin;
	}
	if (pMaxSpec->fMagnitude || pMaxSpec->eUnit)
	{
		F32 fMax = ui_GenPositionSpecToScalar(pMaxSpec, fParentSize, fFitContentsSize, fFitParentSize, fRatioSize, fMargin, fScale);
		if (fMax < fSize)
			fSize = fMax;
	}
	return fSize;
}

void ui_GenPositionToCBox(UIPosition *pChildPos, const CBox *pParentBox, F32 fParentScale, F32 fChildScale, CBox *pResult, const CBox *pFitContents, const CBox *pFitParent, UIGenAnchor *pAnchor)
{
	F32 fX;
	F32 fY;
	F32 fWidth;
	F32 fHeight;
	F32 fScale = fParentScale * fChildScale;
	F32 fParentWidth = CBoxWidth(pParentBox);
	F32 fParentHeight = CBoxHeight(pParentBox);
	F32 fPercentCalcX = fParentWidth * pChildPos->fPercentX;
	F32 fPercentCalcY = fParentHeight * pChildPos->fPercentY;
	F32 fFitContentsWidth = CBoxWidth(pFitContents);
	F32 fFitContentsHeight = CBoxHeight(pFitContents);
	F32 fFitParentWidth = CBoxWidth(pFitParent);
	F32 fFitParentHeight = CBoxHeight(pFitParent);
	F32 fHorizontalMargin = pChildPos->iLeftMargin + pChildPos->iRightMargin;
	F32 fVerticalMargin = pChildPos->iTopMargin + pChildPos->iBottomMargin;

	// If using ratio the order height and width are calculated is important
	if (pChildPos->Height.eUnit == UIUnitRatio
		|| pChildPos->MinimumHeight.eUnit == UIUnitRatio
		|| pChildPos->MaximumHeight.eUnit == UIUnitRatio)
	{
		fWidth = ui_GenPositionToCBoxDimension(&pChildPos->Width, &pChildPos->MinimumWidth, &pChildPos->MaximumWidth, fParentWidth, fFitContentsWidth, fFitParentWidth, 0, fHorizontalMargin, fScale);
		fHeight = ui_GenPositionToCBoxDimension(&pChildPos->Height, &pChildPos->MinimumHeight, &pChildPos->MaximumHeight, fParentHeight, fFitContentsHeight, fFitParentHeight, fWidth, fVerticalMargin, fScale);
	}
	else
	{
		fHeight = ui_GenPositionToCBoxDimension(&pChildPos->Height, &pChildPos->MinimumHeight, &pChildPos->MaximumHeight, fParentHeight, fFitContentsHeight, fFitParentHeight, 0, fVerticalMargin, fScale);
		fWidth = ui_GenPositionToCBoxDimension(&pChildPos->Width, &pChildPos->MinimumWidth, &pChildPos->MaximumWidth, fParentWidth, fFitContentsWidth, fFitParentWidth, fHeight, fHorizontalMargin, fScale);
	}

	if (pChildPos->eOffsetFrom & UILeft)
		fX = pParentBox->lx + fPercentCalcX + fParentScale * pChildPos->iX + fScale * pChildPos->iLeftMargin;
	else if (pChildPos->eOffsetFrom & UIRight)
		fX = pParentBox->hx - (fPercentCalcX + fWidth + fParentScale * pChildPos->iX + fScale * pChildPos->iRightMargin);
	else
		fX = ((pParentBox->lx + pParentBox->hx) - (fPercentCalcX + fWidth + fParentScale * pChildPos->iX + fScale * (fHorizontalMargin))) / 2;

	if (pChildPos->eOffsetFrom & UITop)
		fY = pParentBox->ly + fPercentCalcY + fParentScale * pChildPos->iY + fScale * pChildPos->iTopMargin;
	else if (pChildPos->eOffsetFrom & UIBottom)
		fY = pParentBox->hy - (fPercentCalcY + fHeight + fParentScale * pChildPos->iY + fScale * pChildPos->iBottomMargin);
	else
		fY = ((pParentBox->ly + pParentBox->hy) - (fPercentCalcY + fHeight + fParentScale * pChildPos->iY + fScale * (fVerticalMargin))) / 2;

	// Alex says to put a comment here to document that fScale == 1 is intentional.
	if (s_bGenPositionRound && fScale == 1)
	{
		fX = genRound(fX);
		fY = genRound(fY);
		fWidth = genRound(fWidth);
		fHeight = genRound(fHeight);
	}
	BuildCBox(pResult, fX, fY, fWidth, fHeight);

	if (pAnchor) 
		ui_GenRepositionAroundAnchor(pParentBox, pResult, pAnchor, pChildPos->bPositionOnScreen);
	else if (pChildPos->bPositionOnScreen) 
		ui_GenPositionBoxOnScreen(pResult);

	if (pChildPos->bClipToParent)
		CBoxClipTo(pParentBox, pResult);
	else if (pChildPos->bClipToScreen)
	{
		CBox Screen = {0, 0, g_ui_State.screenWidth, g_ui_State.screenHeight};
		CBoxClipTo(&Screen, pResult);
	}
}

bool ui_GenCBoxToPosition(UIPosition *pChildPos, CBox *pChild, const CBox *pParent, UIDirection eResize, F32 fScale)
{
	F32 fMinWidth = 0;
	F32 fMinHeight = 0;
	F32 fMaxWidth = 0;
	F32 fMaxHeight = 0;
	Vec2 v2ChildCenter;
	Vec2 v2ParentCenter;
	bool bValid = true;
	pChildPos->iX = 0.f;
	pChildPos->iY = 0.f;

	pChildPos->eOffsetFrom = 0;

	// adjust the incoming CBox so it's accurate wrt min width/height, otherwise
	// we get bouncing as it goes below min size and then corrects itself.
	switch (pChildPos->MinimumWidth.eUnit)
	{
	case UIUnitFixed:
		fMinWidth = pChildPos->MinimumWidth.fMagnitude * fScale;
		break;
	case UIUnitPercentage:
		fMinWidth = genRound(pChildPos->MinimumWidth.fMagnitude * CBoxWidth(pParent));
		break;
	}
	switch (pChildPos->MaximumWidth.eUnit)
	{
	case UIUnitFixed:
		fMaxWidth = pChildPos->MaximumWidth.fMagnitude * fScale;
		break;
	case UIUnitPercentage:
		fMaxWidth = genRound(pChildPos->MaximumWidth.fMagnitude * CBoxWidth(pParent));
		break;
	}
	switch (pChildPos->MinimumHeight.eUnit)
	{
	case UIUnitFixed:
		fMinHeight = pChildPos->MinimumHeight.fMagnitude * fScale;
		break;
	case UIUnitPercentage:
		fMinHeight = genRound(pChildPos->MinimumHeight.fMagnitude * CBoxHeight(pParent));
		break;
	}
	switch (pChildPos->MaximumHeight.eUnit)
	{
	case UIUnitFixed:
		fMaxHeight = pChildPos->MaximumHeight.fMagnitude * fScale;
		break;
	case UIUnitPercentage:
		fMaxHeight = genRound(pChildPos->MaximumHeight.fMagnitude * CBoxHeight(pParent));
		break;
	}
	if (eResize & UIRight)
	{
		if (fMaxWidth)
		{
			MIN1(pChild->hx, pChild->lx + fMaxWidth);
		}
		if (fMinWidth)
		{
			MAX1(pChild->hx, pChild->lx + fMinWidth);
		}
	}
	else if (eResize & UILeft)
	{
		if (fMinWidth)
		{
			MIN1(pChild->lx, pChild->hx - fMinWidth);
		}
		if (fMaxWidth)
		{
			MAX1(pChild->lx, pChild->hx - fMaxWidth);
		}
	}
	if (eResize & UIBottom)
	{
		if (fMinHeight)
		{
			MAX1(pChild->hy, pChild->ly + fMinHeight);
		}
		if (fMaxHeight)
		{
			MIN1(pChild->hy, pChild->ly + fMaxHeight);
		}
	}
	else if (eResize & UITop)
	{
		if (fMaxHeight)
		{
			MAX1(pChild->ly, pChild->hy - fMaxHeight);
		}
		if (fMinHeight)
		{
			MIN1(pChild->ly, pChild->hy - fMinHeight);
		}
	}

	CBoxGetCenter(pChild, &v2ChildCenter[0], &v2ChildCenter[1]);
	CBoxGetCenter(pParent, &v2ParentCenter[0], &v2ParentCenter[1]);
	if (v2ChildCenter[0] < v2ParentCenter[0])
		pChildPos->eOffsetFrom |= UILeft;
	else
		pChildPos->eOffsetFrom |= UIRight;

	if (v2ChildCenter[1] < v2ParentCenter[1])
		pChildPos->eOffsetFrom |= UITop;
	else
		pChildPos->eOffsetFrom |= UIBottom;

	if (pChildPos->eOffsetFrom & UILeft)
	{
		pChildPos->fPercentX = (pChild->lx - pParent->lx) / CBoxWidth(pParent);
	}
	else if (pChildPos->eOffsetFrom & UIRight)
	{
		pChildPos->fPercentX = (pParent->hx - pChild->hx) / CBoxWidth(pParent);
	}
	if (pChildPos->eOffsetFrom & UITop)
		pChildPos->fPercentY = (pChild->ly - pParent->ly) / CBoxHeight(pParent);
	else if (pChildPos->eOffsetFrom & UIBottom)
		pChildPos->fPercentY = (pParent->hy - pChild->hy) / CBoxHeight(pParent);
	pChildPos->fPercentX = CLAMP(pChildPos->fPercentX, 0, 0.5);
	pChildPos->fPercentY = CLAMP(pChildPos->fPercentY, 0, 0.5);

	if (eResize & (UILeft | UIRight))
	{
		switch (pChildPos->Width.eUnit)
		{
		case UIUnitFixed:
			pChildPos->Width.fMagnitude = genRound(CBoxWidth(pChild) / fScale);
			break;
		case UIUnitPercentage:
			pChildPos->Width.fMagnitude = CBoxWidth(pChild) / CBoxWidth(pParent);
			break;
		default:
			bValid = false;
		}
	}
	if (eResize & (UITop | UIBottom))
	{
		switch (pChildPos->Height.eUnit)
		{
		case UIUnitFixed:
			pChildPos->Height.fMagnitude = genRound(CBoxHeight(pChild) / fScale);
			break;
		case UIUnitPercentage:
			pChildPos->Height.fMagnitude = CBoxHeight(pChild) / CBoxHeight(pParent);
			break;
		default:
			bValid = false;
		}
	}
	return bValid;
}

#define ui_GenConditionGetGlob(pGen, pchVar, pchGen, hGen, pGlob) \
do { \
	UIGen *pTarget; \
	if (pchGen == s_pch_Self) \
		pTarget = pGen; \
	else if (pchGen == s_pch_Parent) \
		pTarget = pGen->pParent; \
	else \
		pTarget = GET_REF(hGen); \
	if (pTarget) \
		pGlob = eaIndexedGetUsingString(&pTarget->eaVars, pchVar); \
	if (pTarget && !pGlob) \
		ErrorFilenamef(pGen->pchFilename, "%s references unknown variable %s on gen %s", pGen->pchName, pchVar, pTarget->pchName); \
} while(0)

GEN_FORCE_INLINE_L2 static bool ui_GenEvalComplexStateCondition(UIGen *pGen, UIGenComplexStateDef *pOverride, ExprContext *pContext)
{
	int i;
	for (i = eaiSize(&pOverride->eaiInState) - 1; i >= 0 ; --i)
	{
		if (!ui_GenInState(pGen, pOverride->eaiInState[i]))
			return false;
	}
	for (i = eaiSize(&pOverride->eaiNotInState) - 1; i >= 0 ; --i)
	{
		if (ui_GenInState(pGen, pOverride->eaiNotInState[i]))
			return false;
	}
	for (i = eaSize(&pOverride->eaIntCondition) - 1; i >= 0; --i)
	{
		UIGenIntCondition *pCond = pOverride->eaIntCondition[i];
		UIGenVarTypeGlob *pGlob = NULL;
		ui_GenConditionGetGlob(pGen, pCond->pchVar, pCond->pchGen, pCond->hGen, pGlob);
		if (pGlob)
		{
			switch (pCond->eCondition)
			{
			case kUIGenConditionEqual:              if (pGlob->iInt == pCond->iValue) continue; break;
			case kUIGenConditionNotEqual:           if (pGlob->iInt != pCond->iValue) continue; break;
			case kUIGenConditionGreaterThan:        if (pGlob->iInt >  pCond->iValue) continue; break;
			case kUIGenConditionLessThan:           if (pGlob->iInt <  pCond->iValue) continue; break;
			case kUIGenConditionGreaterThanOrEqual: if (pGlob->iInt >= pCond->iValue) continue; break;
			case kUIGenConditionLessThanOrEqual:    if (pGlob->iInt <= pCond->iValue) continue; break;
			default: ;
			}
		}
		return false;
	}
	for (i = eaSize(&pOverride->eaFloatCondition) - 1; i >= 0; --i)
	{
		UIGenFloatCondition *pCond = pOverride->eaFloatCondition[i];
		UIGenVarTypeGlob *pGlob = NULL;
		ui_GenConditionGetGlob(pGen, pCond->pchVar, pCond->pchGen, pCond->hGen, pGlob);
		if (pGlob)
		{
			switch (pCond->eCondition)
			{
			case kUIGenConditionEqual:              if (pGlob->fFloat == pCond->fValue) continue; break;
			case kUIGenConditionNotEqual:           if (pGlob->fFloat != pCond->fValue) continue; break;
			case kUIGenConditionGreaterThan:        if (pGlob->fFloat >  pCond->fValue) continue; break;
			case kUIGenConditionLessThan:           if (pGlob->fFloat <  pCond->fValue) continue; break;
			case kUIGenConditionGreaterThanOrEqual: if (pGlob->fFloat >= pCond->fValue) continue; break;
			case kUIGenConditionLessThanOrEqual:    if (pGlob->fFloat <= pCond->fValue) continue; break;
			default: ;
			}
		}
		return false;
	}
	for (i = eaSize(&pOverride->eaStringCondition) - 1; i >= 0; --i)
	{
		UIGenStringCondition *pCond = pOverride->eaStringCondition[i];
		UIGenVarTypeGlob *pGlob = NULL;
		const char* pchLeft;
		const char* pchRight;
		ui_GenConditionGetGlob(pGen, pCond->pchVar, pCond->pchGen, pCond->hGen, pGlob);
		pchLeft = pGlob->pchString ? pGlob->pchString : "";
		pchRight = pCond->pchValue ? pCond->pchValue : "";
		if (pGlob)
		{
			switch (pCond->eCondition)
			{
			case kUIGenConditionEqual:              if (stricmp(pchLeft, pchRight) == 0) continue; break;
			case kUIGenConditionNotEqual:           if (stricmp(pchLeft, pchRight) != 0) continue; break;
			case kUIGenConditionGreaterThan:        if (stricmp(pchLeft, pchRight) >  0) continue; break;
			case kUIGenConditionLessThan:           if (stricmp(pchLeft, pchRight) <  0) continue; break;
			case kUIGenConditionGreaterThanOrEqual: if (stricmp(pchLeft, pchRight) >= 0) continue; break;
			case kUIGenConditionLessThanOrEqual:    if (stricmp(pchLeft, pchRight) <= 0) continue; break;
			default: ;
			}
		}
		return false;
	}

	for (i = eaSize(&pOverride->eaIntCondition2) - 1; i >= 0; --i)
	{
		UIGenCondition2 *pCond = pOverride->eaIntCondition2[i];
		UIGenVarTypeGlob *pGlob1 = NULL;
		UIGenVarTypeGlob *pGlob2 = NULL;
		ui_GenConditionGetGlob(pGen, pCond->pchVar1, pCond->pchGen1, pCond->hGen1, pGlob1);
		ui_GenConditionGetGlob(pGen, pCond->pchVar2, pCond->pchGen2, pCond->hGen2, pGlob2);
		if (pGlob1 && pGlob2)
		{
			switch (pCond->eCondition)
			{
			case kUIGenConditionEqual:              if (pGlob1->iInt == pGlob2->iInt) continue; break;
			case kUIGenConditionNotEqual:           if (pGlob1->iInt != pGlob2->iInt) continue; break;
			case kUIGenConditionGreaterThan:        if (pGlob1->iInt >  pGlob2->iInt) continue; break;
			case kUIGenConditionLessThan:           if (pGlob1->iInt <  pGlob2->iInt) continue; break;
			case kUIGenConditionGreaterThanOrEqual: if (pGlob1->iInt >= pGlob2->iInt) continue; break;
			case kUIGenConditionLessThanOrEqual:    if (pGlob1->iInt <= pGlob2->iInt) continue; break;
			default: ;
			}
		}
		return false;
	}
	for (i = eaSize(&pOverride->eaFloatCondition2) - 1; i >= 0; --i)
	{
		UIGenCondition2 *pCond = pOverride->eaFloatCondition2[i];
		UIGenVarTypeGlob *pGlob1 = NULL;
		UIGenVarTypeGlob *pGlob2 = NULL;
		ui_GenConditionGetGlob(pGen, pCond->pchVar1, pCond->pchGen1, pCond->hGen1, pGlob1);
		ui_GenConditionGetGlob(pGen, pCond->pchVar2, pCond->pchGen2, pCond->hGen2, pGlob2);
		if (pGlob1 && pGlob2)
		{
			switch (pCond->eCondition)
			{
			case kUIGenConditionEqual:              if (pGlob1->fFloat == pGlob2->fFloat) continue; break;
			case kUIGenConditionNotEqual:           if (pGlob1->fFloat != pGlob2->fFloat) continue; break;
			case kUIGenConditionGreaterThan:        if (pGlob1->fFloat >  pGlob2->fFloat) continue; break;
			case kUIGenConditionLessThan:           if (pGlob1->fFloat <  pGlob2->fFloat) continue; break;
			case kUIGenConditionGreaterThanOrEqual: if (pGlob1->fFloat >= pGlob2->fFloat) continue; break;
			case kUIGenConditionLessThanOrEqual:    if (pGlob1->fFloat <= pGlob2->fFloat) continue; break;
			default: ;
			}
		}
		return false;
	}
	for (i = eaSize(&pOverride->eaStringCondition2) - 1; i >= 0; --i)
	{
		UIGenCondition2 *pCond = pOverride->eaStringCondition2[i];
		UIGenVarTypeGlob *pGlob1 = NULL;
		UIGenVarTypeGlob *pGlob2 = NULL;
		const char* pchLeft;
		const char* pchRight;
		ui_GenConditionGetGlob(pGen, pCond->pchVar1, pCond->pchGen1, pCond->hGen1, pGlob1);
		ui_GenConditionGetGlob(pGen, pCond->pchVar2, pCond->pchGen2, pCond->hGen2, pGlob2);
		pchLeft = pGlob1->pchString ? pGlob1->pchString : "";
		pchRight = pGlob2->pchString ? pGlob2->pchString : "";
		if (pGlob1 && pGlob2)
		{
			switch (pCond->eCondition)
			{
			case kUIGenConditionEqual:              if (stricmp(pchLeft, pchRight) == 0) continue; break;
			case kUIGenConditionNotEqual:           if (stricmp(pchLeft, pchRight) != 0) continue; break;
			case kUIGenConditionGreaterThan:        if (stricmp(pchLeft, pchRight) >  0) continue; break;
			case kUIGenConditionLessThan:           if (stricmp(pchLeft, pchRight) <  0) continue; break;
			case kUIGenConditionGreaterThanOrEqual: if (stricmp(pchLeft, pchRight) >= 0) continue; break;
			case kUIGenConditionLessThanOrEqual:    if (stricmp(pchLeft, pchRight) <= 0) continue; break;
			default: ;
			}
		}
		return false;
	}

	if (pOverride->pCondition)
	{
		if (g_GenState.pchDumpCSDExpressions)
		{
			eaPush(&g_GenState.eapchExprs, pGen->pchFilename);
			eaPush(&g_GenState.eapchExprs, pGen->pchName);
			eaPush(&g_GenState.eapchExprs, pOverride->pCondition->lines[0]->origStr);
		}
		return !!ui_GenTimeEvaluateWithContext(pGen, pOverride->pCondition, NULL, pContext ? pContext : (pContext = ui_GenGetContext(pGen)), "ComplexStateDef");
	}
	else
	{
		return true;
	}
}

GEN_FORCE_INLINE_L2 static void ui_GenEvalComplexStates(UIGen *pGen)
{
	S32 i;
	S32 j;
	ExprContext *pContext = NULL;

	for (j = 0; j < eaSize(&pGen->eaBorrowed); j++)
	{
		UIGenBorrowed *pBorrowed = pGen->eaBorrowed[j];
		UIGen *pBorrowedGen = GET_REF(pBorrowed->hGen);
		if (!pBorrowedGen)
			continue;
		for (i = 0; i < eaSize(&pBorrowedGen->eaComplexStates); i++)
		{
			UIGenComplexStateDef *pOverride = pBorrowedGen->eaComplexStates[i];
			bool bResult = ui_GenEvalComplexStateCondition(pGen, pOverride, pContext);
			bool bOldResult = !!(pBorrowed->uiComplexStates & ((U32)1 << i));
			if (bResult && !bOldResult && pOverride->pOnEnter)
				eaPushUnique(&pGen->eaTransitions, pOverride->pOnEnter);
			else if (!bResult && bOldResult && pOverride->pOnExit)
				eaPushUnique(&pGen->eaTransitions, pOverride->pOnExit);
			if (bResult)
				pBorrowed->uiComplexStates |= ((U32)1 << i);
			else
				pBorrowed->uiComplexStates &= ~((U32)1 << i);
			if (bResult != bOldResult && pOverride->pOverride)
				ui_GenMarkDirty(pGen);
		}
	}

	for (i = 0; i < eaSize(&pGen->eaComplexStates); i++)
	{
		UIGenComplexStateDef *pOverride = pGen->eaComplexStates[i];
		bool bResult = ui_GenEvalComplexStateCondition(pGen, pGen->eaComplexStates[i], pContext);
		bool bOldResult = !!(pGen->uiComplexStates & ((U32)1 << i));
		if (bResult && !bOldResult && pOverride->pOnEnter)
			eaPushUnique(&pGen->eaTransitions, pOverride->pOnEnter);
		else if (!bResult && bOldResult && pOverride->pOnExit)
			eaPushUnique(&pGen->eaTransitions, pOverride->pOnExit);

		if (bResult)
			pGen->uiComplexStates |= ((U32)1 << i);
		else
			pGen->uiComplexStates &= ~((U32)1 << i);
		if (bResult != bOldResult && pOverride->pOverride)
			ui_GenMarkDirty(pGen);
	}
}

__forceinline static bool ui_GenUsesUnit(UIPosition *pPos, UIUnitType eType)
{
	return (
		pPos->Width.eUnit == eType || pPos->MaximumWidth.eUnit == eType || pPos->MinimumWidth.eUnit == eType ||
		pPos->Height.eUnit == eType || pPos->MaximumHeight.eUnit == eType || pPos->MinimumHeight.eUnit == eType
		);
}

__forceinline static void ui_GenTransform(const CBox* pScreenBox, Mat3 *pMatrix, UIGenTransformation *pTransform, F32 fScale)
{
	if (pMatrix)
	{
		F32 fSin, fCos;
		// Translation used for shearing
		const F32 x1 = pScreenBox->lx;
		const F32 y1 = g_ui_State.screenHeight - pScreenBox->ly;

		// Translation used for scaling and rotation
		const F32 x2 = pScreenBox->lx 
			+ ui_GenPositionSpecToScalar(&pTransform->CenterX, CBoxWidth(pScreenBox), 0, 0, 0, 0, fScale);
		const F32 y2 = g_ui_State.screenHeight 
			- (pScreenBox->ly + ui_GenPositionSpecToScalar(&pTransform->CenterY, CBoxHeight(pScreenBox), 0, 0, 0, 0, fScale));

		// Need to reverse shear because rendering and gens use different coordinate spaces
		const F32 fShearX = -pTransform->fShearX;
		const F32 fShearY = -pTransform->fShearY;
		sincosf(UI_ANGLE_TO_RAD(pTransform->Rotation), &fSin, &fCos);
		//////////////////////////////////////////////////////////////////////////
		//
		// This represents the following matrix multiplication:
		//
		//    [ 1 0 x1 + x2 ]   [ cos(a) -sin(a) 0 ]   [ ScaleX 0      0 ]   [ 1 0 -(x1 + x2) ]   [ 1 0 x1 ]   [ 1      ShearX 0 ]   [ 1 0 -x1 ]
		//    [ 0 1 y1 + y2 ] * [ sin(a) cos(a)  0 ] * [ 0      ScaleY 0 ] * [ 0 1 -(y1 + y2) ] * [ 1 0 y1 ]   [ ShearY 1      0 ] * [ 1 1 -y1 ]
		//    [ 0 0 1       ]   [ 0      0       1 ]   [ 0      0      1 ]   [ 0 0 1          ]   [ 1 0 1  ]   [ 0      0      1 ]   [ 0 0 1   ]
		//    Translate         Rotate                 Shear                 Scale                 Translate
		//
		// Remember, do matrix multiplication in the opposite order that you want to apply them
		//

		(*pMatrix)[0][0] = (fCos * pTransform->fScaleX) - (fSin * pTransform->fScaleY * fShearY);
		(*pMatrix)[0][1] = (fCos * pTransform->fScaleY * fShearY) + (fSin * pTransform->fScaleX);
		(*pMatrix)[0][2] = 0;
		(*pMatrix)[1][0] = (fCos * pTransform->fScaleX * fShearX) - (fSin * pTransform->fScaleY);
		(*pMatrix)[1][1] = (fSin * pTransform->fScaleX * fShearX) + (fCos * pTransform->fScaleY);
		(*pMatrix)[1][2] = 0;
		(*pMatrix)[2][0] = (-fCos * pTransform->fScaleX * y1 * fShearX) + (fSin * pTransform->fScaleY * x1 * fShearY) + (fCos * pTransform->fScaleX * -x2) + (fSin * pTransform->fScaleY * y2) + x2;
		(*pMatrix)[2][1] = (-fSin * pTransform->fScaleX * y1 * fShearX) - (fCos * pTransform->fScaleY * x1 * fShearY) - (fCos * pTransform->fScaleY * y2) + (fSin * pTransform->fScaleX * -x2) + y2;
		(*pMatrix)[2][2] = 1;


	}
}

GEN_FORCE_INLINE_L2 static void ui_GenLayoutInternal(UIGen *pGen, UIGenInternal *pInt, bool bEstimate)
{
	CBox UnpaddedScreenBox = pGen->UnpaddedScreenBox;
	UIGen *pParent = pGen->pParent;
	UIGenInternal *pParentInt = pParent->pResult;
	F32 fParentScale = pInt->pos.bResetScale ? g_GenState.fScale : pParent->fScale;
	U32 iAlpha = pInt->fAlpha * (pInt->bResetAlpha ? 255 : pParent->chAlpha);
	CBox FitParentBox = {0, 0, 0, 0};
	CBox FitContentsBox = {0, 0, 0, 0};
	const CBox *pParentBox = pInt->pos.bIgnoreParentPadding ? &pParent->UnpaddedScreenBox : &pParent->ScreenBox;
	UITextureAssembly *pAssembly = ui_GenTextureAssemblyGetAssembly(pGen, pInt->pAssembly);
	bool bUsesFitContents = ui_GenUsesUnit(&pInt->pos, UIUnitFitContents);
	bool bUseXFormMatrix = UI_GEN_USE_MATRIX(pInt);

	PERFINFO_AUTO_START_PIX(__FUNCTION__, 1);

	pGen->fScale = fParentScale * pInt->pos.fScale;
	if (!bEstimate && ui_GenUsesUnit(&pInt->pos, UIUnitFitParent))
	{
		UIGenFitSizeFunc cbFitParent = g_GenState.aFuncTable[pInt->eType].cbFitParentSize;
		if (cbFitParent)
			cbFitParent(pGen, pInt, &FitParentBox);
	}
	if (!bEstimate && bUsesFitContents)
		ui_GenFitContentsSize(pGen, pInt, &FitContentsBox);
	ui_GenPositionToCBox(&pInt->pos, pParentBox, fParentScale, pInt->pos.fScale, &pGen->UnpaddedScreenBox, &FitContentsBox, &FitParentBox, pGen->pResult->pAnchor);
	if (bEstimate && (bUsesFitContents || ui_GenUsesUnit(&pInt->pos, UIUnitFitParent)))
	{
		// If this is an estimation pass, whatever our box's size was last frame is going
		// to be better estimate than a smaller one; most things grow, not shrink,
		// and ui_GenPositionToCBox may have forced us to a MinWidth/MinHeight.
		if (pGen->UnpaddedScreenBox.hx - pGen->UnpaddedScreenBox.lx < UnpaddedScreenBox.hx - UnpaddedScreenBox.lx)
		{
			pGen->UnpaddedScreenBox.lx = UnpaddedScreenBox.lx;
			pGen->UnpaddedScreenBox.hx = UnpaddedScreenBox.hx;
		}
		if (pGen->UnpaddedScreenBox.hy - pGen->UnpaddedScreenBox.ly < UnpaddedScreenBox.hy - UnpaddedScreenBox.ly)
		{
			pGen->UnpaddedScreenBox.ly = UnpaddedScreenBox.ly;
			pGen->UnpaddedScreenBox.hy = UnpaddedScreenBox.hy;
		}
	}
	if (pInt->pos.ausScaleAsIf[0] && pInt->pos.ausScaleAsIf[1])
	{
		F32 fScaleX = g_ui_State.screenWidth / (F32)pInt->pos.ausScaleAsIf[0];
		F32 fScaleY = g_ui_State.screenHeight / (F32)pInt->pos.ausScaleAsIf[1];
		F32 fScale = min(fScaleX, fScaleY);
		if (pInt->pos.bScaleNoGrow && fScale > 1) 
			fScale = 1;
		if (pInt->pos.bScaleNoShrink && fScale < 1) 
			fScale = 1;
		pGen->fScale = (pInt->pos.bScaleAsIfWithGlobal ? g_GenState.fScale : 1) * fScale;
	}

	if (pInt->pRelative) 
	{
		ui_GenHandleRelativeOffset(pGen, &pGen->UnpaddedScreenBox, pInt->pRelative);
	}

	pGen->chAlpha = CLAMP(iAlpha, 0, 255);

	if (pAssembly)
	{
		pGen->ScreenBox.lx = pGen->UnpaddedScreenBox.lx + pAssembly->iPaddingLeft * pGen->fScale;
		pGen->ScreenBox.ly = pGen->UnpaddedScreenBox.ly + pAssembly->iPaddingTop * pGen->fScale;
		pGen->ScreenBox.hx = pGen->UnpaddedScreenBox.hx - pAssembly->iPaddingRight * pGen->fScale;
		pGen->ScreenBox.hy = pGen->UnpaddedScreenBox.hy - pAssembly->iPaddingBottom * pGen->fScale;
	}
	else if (pInt->pAssembly)
	{
		pGen->ScreenBox.lx = pGen->UnpaddedScreenBox.lx + pInt->pAssembly->iPaddingLeft * pGen->fScale;
		pGen->ScreenBox.ly = pGen->UnpaddedScreenBox.ly + pInt->pAssembly->iPaddingTop * pGen->fScale;
		pGen->ScreenBox.hx = pGen->UnpaddedScreenBox.hx - pInt->pAssembly->iPaddingRight * pGen->fScale;
		pGen->ScreenBox.hy = pGen->UnpaddedScreenBox.hy - pInt->pAssembly->iPaddingBottom * pGen->fScale;
	}
	else
		pGen->ScreenBox = pGen->UnpaddedScreenBox;


	if (bUseXFormMatrix)
		ui_GenTransform(&pGen->UnpaddedScreenBox, &pGen->TransformationMatrix, &pInt->Transformation, pGen->fScale);

	PERFINFO_AUTO_STOP_PIX();
}

GEN_FORCE_INLINE_L2 static UIGenInternal *GenOverride(UIGen *pGen, UIGenInternal *pResult, UIGenInternal *pOverride, ParseTable *pTable, UIGenTweenInfo **ppTween, UIGenType eRealType, bool bBorrow)
{
	S32 i;
	if (pOverride)
	{
		if (pResult)
		{
			if (pOverride->eType == kUIGenTypeBox)
				pTable = parse_UIGenBox;
			StructOverride(pTable, pResult, pOverride, true, true, true);

			if (eaFind(&pOverride->eaRemovedChildren, s_pchAll) >= 0)
			{
				eaClearFast(&pResult->eaInlineChildren);
				eaClearStruct(&pResult->eaChildren, parse_UIGenChild);
			}
			else
			{
				// Remove any suppressed children.
				for (i = eaSize(&pResult->eaInlineChildren) - 1; i >= 0; i--)
				{
					UIGen *pChild = pResult->eaInlineChildren[i];
					if (eaFind(&pOverride->eaRemovedChildren, pChild->pchName) >= 0)
						eaRemove(&pResult->eaInlineChildren, i);
				}
				for (i = eaSize(&pResult->eaChildren) - 1; i >= 0; i--)
				{
					UIGenChild *pChildContainer = pResult->eaChildren[i];
					UIGen *pChild = GET_REF(pChildContainer->hChild);
					if (!pChild || eaFind(&pOverride->eaRemovedChildren, pChild->pchName) >= 0)
					{
						eaRemove(&pResult->eaChildren, i);
						StructDestroy(parse_UIGenChild, pChildContainer);
					}
				}
			}


			if (bBorrow)
			{
				for (i = 0; i < eaSize(&pOverride->eaInlineChildren); i++)
				{
					UIGen *pChild = pOverride->eaInlineChildren[i];
					UIGen *pExisting = eaIndexedGetUsingString(&pGen->eaBorrowedInlineChildren, pChild->pchName);
					if (!pExisting)
					{
						pExisting = ui_GenClone(pChild);
						eaPush(&pGen->eaBorrowedInlineChildren, pExisting);
					}
					eaPush(&pResult->eaInlineChildren, pExisting);
				}
			}
			else
				eaPushEArray(&pResult->eaInlineChildren, &pOverride->eaInlineChildren);
		}
		else
		{
			pResult = GenInternalClone(pGen, pOverride, pTable, eRealType, bBorrow);
		}
		if (pOverride->pTween && ppTween)
			*ppTween = pOverride->pTween;
	}
	return pResult;
}

GEN_FORCE_INLINE_L2 static void ui_GenUpdateCreateResult(UIGen *pGen)
{
	ParseTable *pTable = ui_GenGetType(pGen);
	UIGenTweenInfo *pTween = NULL;
	UIGenInternal *pOldResult = pGen->pResult;
	UIGenInternal *pNewResult = NULL;
	UIGen **eaOldInlineChildren = NULL;
	S32 i, j;
	UIGenState *eaiStates = NULL;
	bool bTween = false;
	if (!pTable)
		return;

	pGen->bNeedsRebuild = false;

	ui_GenRunAction(pGen, pGen->pBeforeResult);

	GEN_PRINTF("%s: Rebuilding.", pGen->pchName);

	if (pOldResult)
	{
		eaCopy(&eaOldInlineChildren, &pOldResult->eaInlineChildren);
		eaClearFast(&pOldResult->eaInlineChildren);
	}

	// First, we start with the base state.
	for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
	{
		UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[i]->hGen);
		if (!pBorrowed)
			continue;
		pNewResult = GenOverride(pGen, pNewResult, pBorrowed->pBase, pTable, &pTween, pGen->eType, true);
	}
	pNewResult = GenOverride(pGen, pNewResult, pGen->pBase, pTable, &pTween, pGen->eType, false);

	if (!pGen->pResult)
		pGen->pResult = pNewResult;

	pNewResult = GenOverride(pGen, pNewResult, pGen->pCodeOverrideEarly, pTable, &pTween, pGen->eType, false);

	for (i = 0; i < g_GenState.iMaxStates; i++)
	{
		if (TSTB(pGen->bfStates, i))
		{
			eaiPush(&eaiStates, i);
		}
	}

	for (i = 0; i < eaiSize(&eaiStates); i++)
	{
		UIGenState eState = eaiStates[i];
		UIGenStateDef *pOverride = eaIndexedGetUsingInt(&pGen->eaStates, eState);

		for (j = 0; j < eaSize(&pGen->eaBorrowed); j++)
		{
			UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[j]->hGen);
			UIGenStateDef *pBorrowedOverride = pBorrowed ? eaIndexedGetUsingInt(&pBorrowed->eaStates, eState) : NULL;
			if (pBorrowedOverride)
				pNewResult = GenOverride(pGen, pNewResult, pBorrowedOverride->pOverride, pTable, &pTween, pGen->eType, true);
		}

		if (pOverride)
			pNewResult = GenOverride(pGen, pNewResult, pOverride->pOverride, pTable, &pTween, pGen->eType, false);
	}

	eaiDestroy(&eaiStates);

	for (j = 0; j < eaSize(&pGen->eaBorrowed); j++)
	{
		UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[j]->hGen);
		if (!pBorrowed)
			continue;
		for (i = 0; i < eaSize(&pBorrowed->eaComplexStates); i++)
		{
			UIGenComplexStateDef *pOverride = pBorrowed->eaComplexStates[i];
			if (pOverride && (pGen->eaBorrowed[j]->uiComplexStates & ((U32)1 << i)))
				pNewResult = GenOverride(pGen, pNewResult, pOverride->pOverride, pTable, &pTween, pGen->eType, true);
		}
	}

	for (i = 0; i < eaSize(&pGen->eaComplexStates); i++)
	{
		UIGenComplexStateDef *pOverride = pGen->eaComplexStates[i];
		if (pOverride && (pGen->uiComplexStates & ((U32)1 << i)))
			pNewResult = GenOverride(pGen, pNewResult, pOverride->pOverride, pTable, &pTween, pGen->eType, false);
	}

	if (pGen->pTutorialInfo)
	{
		UIGenTutorialInfo *pTutorialInfo = pGen->pTutorialInfo;
		if (pTutorialInfo->pOverride && (pTutorialInfo->pOverride->eType == pGen->eType || pTutorialInfo->pOverride->eType == kUIGenTypeBox))
			GenOverride(pGen, pNewResult, pTutorialInfo->pOverride, pTable, &pTween, pGen->eType, true);
	}

	for (j = 0; j < eaSize(&pGen->eaBorrowed); j++)
	{
		UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[j]->hGen);
		if (pBorrowed)
			GenOverride(pGen, pNewResult, pBorrowed->pLast, pTable, &pTween, pGen->eType, true);
	}
	pNewResult = GenOverride(pGen, pNewResult, pGen->pLast, pTable, &pTween, pGen->eType, false);

	if (pNewResult)
	{
		// Although pTween is equal to pResult->pTween, we do pointer comparisons to
		// figure out when we've actually switched states and need to tween again,
		// So, we can't actually just pass in pTween->pTween here.
		bTween = ui_GenSetTweenInfo(pGen, pTween, pOldResult, pNewResult);

		if (eaFind(&pNewResult->eaSuppressedChildren, s_pchAll) >= 0)
		{
			eaClearFast(&pNewResult->eaInlineChildren);
			eaClearStruct(&pNewResult->eaChildren, parse_UIGenChild);
		}
		else
		{
			for (i = eaSize(&pNewResult->eaInlineChildren) - 1; i >= 0; i--)
			{
				UIGen *pChild = pNewResult->eaInlineChildren[i];
				if (eaFind(&pNewResult->eaSuppressedChildren, pChild->pchName) >= 0)
					eaRemove(&pNewResult->eaInlineChildren, i);
			}
			for (i = eaSize(&pNewResult->eaChildren) - 1; i >= 0; i--)
			{
				UIGenChild *pChildContainer = pNewResult->eaChildren[i];
				UIGen *pChild = GET_REF(pChildContainer->hChild);
				if (!pChild || eaFind(&pNewResult->eaSuppressedChildren, pChild->pchName) >= 0)
				{
					eaRemove(&pNewResult->eaChildren, i);
					StructDestroy(parse_UIGenChild, pChildContainer);
				}
			}
		}

		// Remove any duplicate children.
		for (i = 0; i < eaSize(&pNewResult->eaChildren); i++)
		{
			for (j = eaSize(&pNewResult->eaChildren) - 1; j > i; j--)
			{
				if (GET_REF(pNewResult->eaChildren[i]->hChild) == GET_REF(pNewResult->eaChildren[j]->hChild))
					StructDestroy(parse_UIGenChild, eaRemove(&pNewResult->eaChildren, j));
			}
		}
	}

	// Our child list may have changed. Reset everything that was removed, to save
	// memory and to make sure its hide functions get called. Inner children can
	// be ignored, since they were just all regenerated/reset anyway.
	if (pOldResult)
	{
		UIGen **eaNewChildren = NULL;
		if (pNewResult)
		{
			for (i = 0; i < eaSize(&pNewResult->eaChildren); i++)
			{
				UIGen *pChild = GET_REF(pNewResult->eaChildren[i]->hChild);
				if (pChild)
					eaPush(&eaNewChildren, pChild);
			}
		}
		for (i = 0; i < eaSize(&pOldResult->eaChildren); i++)
		{
			UIGen *pChild = GET_REF(pOldResult->eaChildren[i]->hChild);
			if (pChild && eaFindAndRemoveFast(&eaNewChildren, pChild) < 0)
				ui_GenReset(pChild);
		}
		eaDestroy(&eaNewChildren);

		for (i = 0; i < eaSize(&eaOldInlineChildren); i++)
			if (eaFind(&pNewResult->eaInlineChildren, eaOldInlineChildren[i]) < 0)
				ui_GenReset(eaOldInlineChildren[i]);
	}

	// Children are UNOWNED by the old result.
	eaDestroy(&eaOldInlineChildren);
	ui_GenInternalDestroySafe(&pOldResult);
	pGen->pResult = pNewResult;

	// If this started a tween, pResult is the end result, we want to evaluate to the current result.
	if (bTween)
	{
		ui_GenInitTween(pGen);
	}
}

__forceinline static void ui_GenUpdateTimers(UIGen *pGen)
{
	S32 i;
	// Update timer-based transitions
	for (i = 0; i < eaSize(&pGen->eaTimers); i++)
	{
		UIGenTimer *pTimer = pGen->eaTimers[i];
		if (!pTimer->bPaused)
		{
			pTimer->fCurrent += pGen->fTimeDelta;
			if (pTimer->fCurrent >= pTimer->fTime && &pTimer->OnUpdate)
			{
				eaPushUnique(&pGen->eaTransitions, &pTimer->OnUpdate);
				pTimer->fCurrent = 0;
			}
		}
	}
}

bool ui_GenRunTransitions(UIGen *pGen)
{
	// Run queued transitions
	S32 i;
	GEN_PERFINFO_START_L1("ui_GenRunTransitions", 1);
	for (i = 0; i < eaSize(&pGen->eaTransitions); i++)
	{
		UIGenAction *pTrans = pGen->eaTransitions[i];
		ui_GenTimeAction(pGen, pTrans, "Transition");
	}
	eaClearFast(&pGen->eaTransitions);
	GEN_PERFINFO_STOP_L1();
	return !!i;
}

#define UI_GEN_MAX_LOOP 4

__forceinline static void ui_GenPointerUpdateEarly(UIGen *pGen)
{
	ExprContext *pContext = NULL; 
	int i;

	pGen->uiFrameLastUpdate = g_ui_State.uiFrameCount;

	GEN_PERFINFO_START_L1("PointerUpdate", 1);
	for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
	{
		UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[i]->hGen);
		if (pBorrowed && pBorrowed->pPointerUpdate)
		{
			if (pBorrowed->pPointerUpdate)
			{
				if (!pContext)
					pContext = ui_GenGetContext(pGen);
				ui_GenTimeEvaluateWithContext(pGen, pBorrowed->pPointerUpdate->pExpression, NULL, pContext, "PointerUpdate");
			}
		}
	}
	if (pGen->pPointerUpdate)
	{
		if (pGen->pPointerUpdate->pExpression)
		{
			if (!pContext)
				pContext = ui_GenGetContext(pGen);
			ui_GenTimeEvaluateWithContext(pGen, pGen->pPointerUpdate->pExpression, NULL, pContext, "PointerUpdate");
		}
	}
	GEN_PERFINFO_STOP_L1();
}

__forceinline static void ui_GenPointerUpdateLate(UIGen *pGen)
{
	UIGenLoopFunc cbPointerUpdate = g_GenState.aFuncTable[pGen->eType].cbPointerUpdate;
	if (cbPointerUpdate)
	{
		GEN_PERFINFO_START_L1("Per Gen Type PointerUpdate", 1);
		cbPointerUpdate(pGen);
		GEN_PERFINFO_STOP_L1();
	}
	if (UI_GEN_READY(pGen))
		ui_GenInternalForEachChild(pGen->pResult, ui_GenPointerUpdateCB, pGen, UI_GEN_UPDATE_ORDER);

}

__forceinline static void ui_GenPointerUpdate(UIGen *pGen)
{
	// split into two phases so that they can be run on gens the exact frame they're created. 
	if (UI_GEN_READY(pGen))
	{
		ui_GenPointerUpdateEarly(pGen);
		ui_GenPointerUpdateLate(pGen);
	}
}

GEN_FORCE_INLINE_L2 static void ui_GenUpdate(UIGen *pGen)
{
	static UIGenSpriteCache *s_pCurrentSpriteCache;
	UIGenLoopFunc cbUpdate = g_GenState.aFuncTable[pGen->eType].cbUpdate;
	UIGenLoopFunc cbPointerUpdate = g_GenState.aFuncTable[pGen->eType].cbPointerUpdate;
	UIGenSpriteCache *pLastSpriteCache = s_pCurrentSpriteCache;
	bool bCreated = !pGen->pResult;
	S32 i = 0;
	S32 j = 0;
	// Some gen types require mouse position handling in update. 
	bool bUseXFormMatrix;

	if (pGen->uiTimeLastUpdateInMs)
		pGen->fTimeDelta = (g_ui_State.totalTimeInMs - pGen->uiTimeLastUpdateInMs) / 1000.f;
	pGen->uiTimeLastUpdateInMs = g_ui_State.totalTimeInMs;

	if (!pGen->pResult)
	{
		pGen->uiFrameLastUpdate = g_ui_State.uiFrameCount;

		for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
		{
			UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[i]->hGen);
			if (pBorrowed)
			{
				ui_GenTimeAction(pGen, pBorrowed->pBeforeCreate, "BeforeCreate(%s)", pBorrowed->pchName);
			}
		}

		ui_GenTimeAction(pGen, pGen->pBeforeCreate, "BeforeCreate");
		ui_GenDemandTextures(pGen);
		ui_GenPointerUpdateEarly(pGen);

		g_GenState.bGenLifeChange = true;
	}
	else if (pGen->pResult->bFocusByDefault && !(g_GenState.pFocused || g_ui_State.focused))
	{
		ui_GenSetFocus(pGen);
	}

	ui_GenStates(pGen,
		kUIGenStateHasGenData, SAFE_MEMBER(pGen->pCode, pTable) ? !!pGen->pCode->pStruct : (pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateHasGenData)),
		kUIGenStateNone);

	GEN_PERFINFO_START_L1("BeforeUpdate", 1);
	for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
	{
		UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[i]->hGen);
		if (pBorrowed && pBorrowed->pBeforeUpdate)
			ui_GenTimeAction(pGen, pBorrowed->pBeforeUpdate, "BeforeUpdate(%s)", pBorrowed->pchName);
	}

	ui_GenTimeAction(pGen, pGen->pBeforeUpdate, "BeforeUpdate");
	GEN_PERFINFO_STOP_L1();

	GEN_PERFINFO_START_L1("ui_GenUpdateTimers", 1);
	ui_GenUpdateTimers(pGen);
	GEN_PERFINFO_STOP_L1();

	GEN_PERFINFO_START_L1("ui_GenDragDropUpdate", 1);
	ui_GenDragDropUpdate(pGen);
	GEN_PERFINFO_STOP_L1();

	do 
	{
		i = 0;
		do
		{
			GEN_PERFINFO_START_L1("Evaluate", 1);
			ui_GenUpdateFromGlobalState(pGen);
			ui_GenEvalComplexStates(pGen);
			GEN_PERFINFO_STOP_L1();
			if (i++ > UI_GEN_MAX_LOOP)
			{
				ErrorFilenamef(pGen->pchFilename, "Gen %s has reevaluted %d times, forcing stop", pGen->pchName, UI_GEN_MAX_LOOP);
				break;
			}
		} while (pGen->pResult && ui_GenRunTransitions(pGen));

		// If the result is dirty, do the hard (= slow) work to rebuild it.
		if (pGen->bNeedsRebuild || !pGen->pResult)
		{
			GEN_PERFINFO_START_L1("Build Result", 1);
			ui_GenUpdateCreateResult(pGen);
			ui_GenPointerUpdateLate(pGen);
			if (pGen->pResult && (pGen->bNextFocusOnCreate || pGen->pResult->bFocusOnCreate) && bCreated)
			{
				UIGenInternal *pFocusInternal = NULL;
				if(g_GenState.pFocused)
					pFocusInternal = g_GenState.pFocused->pResult ? g_GenState.pFocused->pResult : ui_GenGetBase(g_GenState.pFocused, false);

				if(!pFocusInternal || !pFocusInternal->bKeepFocusOnCreate)
					ui_GenSetFocus(pGen);

				pGen->bNextFocusOnCreate = false;
			}
			GEN_PERFINFO_STOP_L1();
		}
		if (!pGen->pResult)
		{
			ParseTable *pTable = ui_GenGetType(pGen);
			ErrorFilenamef(pGen->pchFilename,
				"%s: No gen result could be created. This means you only had override states, "
				"and none of them were applied. Either you didn't test this, or one of your "
				"BorrowFroms was supposed to provide this and is broken.", pGen->pchName);
			pGen->pResult = StructCreateVoid(pTable);
			if (!pGen->pResult)
			{
				return;
			}
		}

		if (pGen->pResult->bFocusEveryFrame || (pGen->pResult->bFocusByDefault && !(g_GenState.pFocused || g_ui_State.focused)))
			ui_GenSetFocus(pGen);

		if (j++ > UI_GEN_MAX_LOOP)
		{
			ErrorFilenamef(pGen->pchFilename, "Gen %s has reevaluted %d times, forcing stop", pGen->pchName, UI_GEN_MAX_LOOP);
			break;
		}
		// Unless we re-evaluate complex states here there is still a potential frame delay
		// if the rebuild itself, rather than an OnEnter/OnExit, causes a CSD condition change.
		// But if we do do it here, we run all CSD conditions a minimum of twice per frame.
		//ui_GenEvalComplexStates(pGen);
	} while (ui_GenRunTransitions(pGen) || pGen->bNeedsRebuild);

	// If a transition made us lose our parent, we shouldn't continue;
	// we're probably being removed from the screen.
	if (!pGen->pParent)
		return;

	if (bCreated)
	{

		for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
		{
			UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[i]->hGen);
			if (pBorrowed)
			{
				ui_GenTimeAction(pGen, pBorrowed->pAfterCreate, "AfterCreate(%s)", pBorrowed->pchName);
			}
		}

		ui_GenTimeAction(pGen, pGen->pAfterCreate, "AfterCreate");
	}

	assertmsgf(pGen->pParent, "Gen %s does not have a parent after AfterCreate, something is wrong!", pGen->pchName);

	// Force evaluate the assembly now
	GEN_PERFINFO_START_L1("ui_GenTextureAssemblyGetAssembly", 1);
	ui_GenTextureAssemblyGetAssembly(pGen, pGen->pResult->pAssembly);
	ui_GenTextureAssemblyGetAssembly(pGen, pGen->pResult->pOverlayAssembly);
	GEN_PERFINFO_STOP_L1();

	// Set these now, but children may change them during their updates.
	pGen->chPriority = pGen->pResult->chPriority;
	pGen->chLayer = pGen->pParent->chLayer;

	bUseXFormMatrix = UI_GEN_USE_MATRIX(pGen->pResult);

	if (bUseXFormMatrix)
	{
		Mat3 m3Prev, m3Inverse;
		if (inputMatrixGet())
			copyMat3(*inputMatrixGet(), m3Prev);
		else
			copyMat3(unitmat, m3Prev);
		inputMatrixPush();
		if (invertMat3(pGen->TransformationMatrix, m3Inverse))
			mulMat3(m3Inverse, m3Prev, *inputMatrixGet());
	}

	if (cbUpdate)
	{
		GEN_PERFINFO_START_L1("Per Gen Type Update", 1);
		cbUpdate(pGen);
		GEN_PERFINFO_STOP_L1();
	}

	if (pGen->pSpriteCache)
	{
		s_pCurrentSpriteCache = pGen->pSpriteCache;
		eaClearFast(&s_pCurrentSpriteCache->eaPopupChildren);
	}

	if (UI_GEN_READY(pGen))
		ui_GenInternalForEachChild(pGen->pResult, ui_GenUpdateCB, pGen, UI_GEN_UPDATE_ORDER);

	if (pGen->pSpriteCache)
		s_pCurrentSpriteCache = pLastSpriteCache;

	// If this gen is a Popup, make sure to add it to the list of Popup children on the parent with SpriteCache
	if (pGen->bPopup && s_pCurrentSpriteCache)
		eaPush(&s_pCurrentSpriteCache->eaPopupChildren, pGen);

	if (UI_GEN_READY(pGen->pParent) && pGen->pParent->pResult->bCopyChildPriority)
	{
		MAX1(pGen->pParent->chPriority, pGen->chPriority);
	}

	if (bUseXFormMatrix)
		inputMatrixPop();
}

bool ui_GenPointerUpdateCB(UIGen *pChild, UIGen *pParent)
{
	if (pChild)
	{
		if (ui_GenCheckSpriteCache(pChild))
			pChild->pSpriteCache->iAccumulate = 0;
		else if (SAFE_MEMBER2(pChild, pSpriteCache, iAccumulate) > 0)
			pChild->pSpriteCache->iAccumulate--;

		pChild->pParent = pParent;
		GEN_PERFINFO_START(pChild);
		ui_GenPointerUpdate(pChild);
		GEN_PERFINFO_STOP(pChild);
	}
	return false;
}

bool ui_GenUpdateCB(UIGen *pChild, UIGen *pParent)
{
	if (pChild)
	{
		pChild->pParent = pParent;
		if (!pChild->pSpriteCache || pChild->pSpriteCache->iAccumulate <= 0)
		{
			GEN_PERFINFO_START(pChild);
			ui_GenUpdate(pChild);
			GEN_PERFINFO_STOP(pChild);

			if (pChild->pSpriteCache)
				pChild->pSpriteCache->bTooltipFocus = false;
		}
	}
	return false;
}

void ui_GenRunAction(UIGen *pGen, UIGenAction *pAction)
{
	S32 j;
	ExprContext *pContext;

	if (!pAction)
		return;

	g_GenState.iActionsRun++;

	for (j = 0; j < eaiSize(&pAction->eaiEnterState); j++)
	{
		ui_GenSetState(pGen, pAction->eaiEnterState[j]);
	}
	for (j = 0; j < eaiSize(&pAction->eaiExitState); j++)
	{
		ui_GenUnsetState(pGen, pAction->eaiExitState[j]);
	}
	for (j = 0; j < eaiSize(&pAction->eaiToggleState); j++)
	{
		ui_GenState(pGen, pAction->eaiToggleState[j], !ui_GenInState(pGen, pAction->eaiToggleState[j]));
	}
	if (pGen->pParent)
	{
		for (j = 0; j < eaiSize(&pAction->eaiCopyState); j++)
		{
			ui_GenState(pGen, pAction->eaiCopyState[j], ui_GenInState(pGen->pParent, pAction->eaiCopyState[j]));
		}
	}

	pContext = pAction->pExpression ? ui_GenGetContext(pGen) : NULL;

	for (j = 0; j < eaSize(&pAction->eachCommands); j++)
		if (pAction->eachCommands[j])
			ui_GenRunCommandInExprContext(pGen, pAction->eachCommands[j]);
	if (pAction->pExpression)
		ui_GenEvaluateWithContext(pGen, pAction->pExpression, NULL, pContext);

	for (j = 0; j < eaSize(&pAction->eaMessage); j++)
	{
		UIGenMessagePacket *pMessage = pAction->eaMessage[j];
		UIGen *pTo = GET_REF(pMessage->hGen);
		const char *pchTo = pTo ? NULL : REF_STRING_FROM_HANDLE(pMessage->hGen);
		if (!pTo && pchTo)
			pTo = ui_GenGetSiblingEx(pGen, pchTo, false, false);
		else if (!pTo)
			pTo = pGen;
		if (!pTo)
			ErrorFilenamef(pGen->pchFilename, "Gen %s is trying to send a message (%s) to non-existent gen %s", pGen->pchName, pMessage->pchMessageName, REF_STRING_FROM_HANDLE(pMessage->hGen));
		else
		{
			GEN_PRINTF("Message: Source=%s, Dest=%s, Message=%s.", pGen->pchName, pTo->pchName, pMessage->pchMessageName);
			ui_GenSendMessage(pTo, pMessage->pchMessageName);
		}
	}

	for (j = 0; j < eaSize(&pAction->eaSetter); j++)
	{
		UIGenVarTypeGlob *pGlob = &pAction->eaSetter[j]->glob;
		UIGen *pTo = (GET_REF(pAction->eaSetter[j]->hTarget) || IS_HANDLE_ACTIVE(pAction->eaSetter[j]->hTarget)) ? GET_REF(pAction->eaSetter[j]->hTarget) : pGen;
		if (!pTo)
		{
			ErrorFilenamef(pGen->pchFilename, "Gen %s is trying to set a var (%s) to non-existent gen %s", pGen->pchName, pGlob->pchName, REF_STRING_FROM_HANDLE(pAction->eaSetter[j]->hTarget));
		}
		else
		{
			UIGenVarTypeGlob *pTargetGlob = eaIndexedGetUsingString(&pTo->eaVars, pGlob->pchName);
			if (!pTargetGlob)
			{
				ErrorFilenamef(pGen->pchFilename, "Gen %s is trying to set a var (%s) that doesn't exist on gen %s", pGen->pchName, pGlob->pchName, REF_STRING_FROM_HANDLE(pAction->eaSetter[j]->hTarget));
			}
			else
			{
				pTargetGlob->fFloat = pGlob->fFloat;
				pTargetGlob->iInt = pGlob->iInt;
				estrCopy(&pTargetGlob->pchString, &pGlob->pchString);
			}
		}
	}

	if (g_ui_State.cbPlayAudio && ui_GenInState(pGen, kUIGenStateVisible) && !ui_GenInState(pGen, kUIGenStateDisabled))
		for (j = 0; j < eaSize(&pAction->eachSounds); j++)
			g_ui_State.cbPlayAudio(pAction->eachSounds[j], pGen->pchFilename);

	for (j = 0; j < eaSize(&pAction->eaMutate); j++)
	{
		UIGenMutateOther *pOther = pAction->eaMutate[j];
		UIGen *pOtherGen = GET_REF(pOther->hGen);
		if (pOtherGen)
		{
			S32 k;
			for (k = 0; k < eaiSize(&pOther->eaiEnterState); k++)
			{
				ui_GenSetState(pOtherGen, pOther->eaiEnterState[k]);
			}
			for (k = 0; k < eaiSize(&pOther->eaiExitState); k++)
			{
				ui_GenUnsetState(pOtherGen, pOther->eaiExitState[k]);
			}
			for (k = 0; k < eaiSize(&pOther->eaiToggleState); k++)
			{
				ui_GenState(pOtherGen, pOther->eaiToggleState[k], !ui_GenInState(pOtherGen, pOther->eaiToggleState[k]));
			}
			if (pGen->pParent)
			{
				for (k = 0; k < eaiSize(&pAction->eaiCopyState); k++)
				{
					ui_GenState(pGen, pAction->eaiCopyState[k], ui_GenInState(pGen->pParent, pAction->eaiCopyState[k]));
				}
			}
		}
	}

	if (pAction->bFocus)
		ui_GenSetFocus(pGen);
	else if (pAction->bUnfocus && pGen == g_GenState.pFocused)
		ui_GenSetFocus(NULL);
	if (pAction->bTooltipFocus)
		ui_GenSetTooltipFocus(pGen);
	else if (pAction->bTooltipUnfocus && pGen == g_GenState.pTooltipFocused)
		ui_GenSetTooltipFocus(NULL);
}

GEN_FORCE_INLINE_L2 static void ui_GenLayout(UIGen *pGen)
{
	UIGenLoopFunc cbLayoutEarly = g_GenState.aFuncTable[pGen->eType].cbLayoutEarly;
	UIGenLoopFunc cbLayoutLate = g_GenState.aFuncTable[pGen->eType].cbLayoutLate;
	CBox *ScreenBox = NULL;
	float fCenterX, fCenterY;
	UIGenTweenState *pTweenState = pGen->pTweenState;

	// Reset the child bounding box
	pGen->ChildBoundingBox = pGen->UnpaddedScreenBox;

	// Update animation state.
	GEN_PERFINFO_START_L1("ui_GenTween", 1);
	ui_GenTween(pGen);
	GEN_PERFINFO_STOP_L1();

	// Do an estimation pass (no FitContents/FitParent) so known sizes
	// are ready for FitContents and FitParent.
	if (cbLayoutEarly || pGen->pResult->pBeforeLayout)
	{
		GEN_PERFINFO_START_L1("Estimation Pass", 1);
		ui_GenLayoutInternal(pGen, pGen->pResult, true);
		ui_GenTimeAction(pGen, pGen->pResult->pBeforeLayout, "BeforeLayout");
		GEN_PERFINFO_STOP_L1();
		// In case this removed the gen.
		if (!pGen->pParent)
			return;
	}

	// Then, lay out the result. This should be done each frame to deal with
	// simple position updates, global scale changes, etc.
	if (cbLayoutEarly)
	{
		GEN_PERFINFO_START_L1("Per Gen Type Layout Early", 1);
		cbLayoutEarly(pGen);
		GEN_PERFINFO_STOP_L1();
	}

	if (!pGen->bUseEstimatedSize)
		ui_GenLayoutInternal(pGen, pGen->pResult, false);

	GEN_PERFINFO_START_L1("ui_GenTweenBox", 1);
	ui_GenTweenBox(pGen);
	GEN_PERFINFO_STOP_L1();

	ui_GenTimeAction(pGen, pGen->pResult->pAfterLayout, "AfterLayout");

	// Then do the same thing to whatever we have for children this frame.
	ui_GenInternalForEachChild(pGen->pResult, ui_GenLayoutCB, pGen, UI_GEN_LAYOUT_ORDER);

	if (cbLayoutLate)
	{
		GEN_PERFINFO_START_L1("Per Gen Type Layout Late", 1);
		cbLayoutLate(pGen);
		GEN_PERFINFO_STOP_L1();
	}

	if (pGen->pResult->pPostLayoutRelative)
	{
		UITextureAssembly *pAssembly = ui_GenTextureAssemblyGetAssembly(pGen, pGen->pResult->pAssembly);
		ui_GenHandleRelativeOffset(pGen, &pGen->UnpaddedScreenBox, pGen->pResult->pPostLayoutRelative);
		pGen->ScreenBox = pGen->UnpaddedScreenBox;
		if (pAssembly)
		{
			pGen->ScreenBox.lx += pAssembly->iPaddingLeft * pGen->fScale;
			pGen->ScreenBox.ly += pAssembly->iPaddingTop * pGen->fScale;
			pGen->ScreenBox.hx -= pAssembly->iPaddingRight * pGen->fScale;
			pGen->ScreenBox.hy -= pAssembly->iPaddingBottom * pGen->fScale;
		}
		if (pGen->pResult->pAssembly)
		{
			pGen->ScreenBox.lx += pGen->pResult->pAssembly->iPaddingLeft * pGen->fScale;
			pGen->ScreenBox.ly += pGen->pResult->pAssembly->iPaddingTop * pGen->fScale;
			pGen->ScreenBox.hx -= pGen->pResult->pAssembly->iPaddingRight * pGen->fScale;
			pGen->ScreenBox.hy -= pGen->pResult->pAssembly->iPaddingBottom * pGen->fScale;
		}
	}

	CBoxCombine(&pGen->ChildBoundingBox, &pGen->UnpaddedScreenBox, &pGen->ChildBoundingBox);
	if (pGen->pResult->bClipInput)
	{
		CBoxClipTo(&pGen->UnpaddedScreenBox, &pGen->ChildBoundingBox);
	}

	if (SAFE_MEMBER2(pGen, pParent, bIsRoot))
	{
		if (pGen->pchJailCell && ui_GenInState(pGen, kUIGenStateJailed))
		{
			ScreenBox = &pGen->pParent->ScreenBox;
		}
		else
		{
			ScreenBox = &pGen->ScreenBox;
		}

		fCenterX = ScreenBox->lx + CBoxWidth(ScreenBox) / 2;
		fCenterY = ScreenBox->ly + CBoxHeight(ScreenBox) / 2;
	}

	ui_GenStates(pGen,
		kUIGenStateVisible, true,
		kUIGenStateLeftEdge, ScreenBox && ScreenBox->lx <= 0 || !ScreenBox && pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateLeftEdge),
		kUIGenStateTopEdge, ScreenBox && ScreenBox->ly <= 0 || !ScreenBox && pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateTopEdge),
		kUIGenStateRightEdge, ScreenBox && ScreenBox->hx >= g_ui_State.screenWidth || !ScreenBox && pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateRightEdge),
		kUIGenStateBottomEdge, ScreenBox && ScreenBox->hy >= g_ui_State.screenHeight || !ScreenBox && pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateBottomEdge),
		kUIGenStateLeftSide, ScreenBox && fCenterX < g_ui_State.screenWidth / 3 || !ScreenBox && pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateLeftSide),
		kUIGenStateHorizontalCenter, ScreenBox && fCenterX >= g_ui_State.screenWidth / 3 && fCenterX < g_ui_State.screenWidth * 2 / 3 || !ScreenBox && pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateHorizontalCenter),
		kUIGenStateRightSide, ScreenBox && fCenterX >= g_ui_State.screenWidth * 2 / 3 || !ScreenBox && pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateRightSide),
		kUIGenStateTopSide, ScreenBox && fCenterY < g_ui_State.screenHeight / 3 || !ScreenBox && pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateTopSide),
		kUIGenStateVerticalCenter, ScreenBox && fCenterY >= g_ui_State.screenHeight / 3 && fCenterY < g_ui_State.screenHeight * 2 / 3 || !ScreenBox && pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateVerticalCenter),
		kUIGenStateBottomSide, ScreenBox && fCenterY >= g_ui_State.screenHeight * 2 / 3 || !ScreenBox && pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateBottomSide),
		kUIGenStateNone);

	if (pGen->pParent)
	{
		CBoxCombine(&pGen->pParent->ChildBoundingBox, &pGen->ChildBoundingBox, &pGen->pParent->ChildBoundingBox);
	}
}

bool ui_GenLayoutCB(UIGen *pChild, UIGen *pParent)
{
	if (UI_GEN_READY(pChild) && pChild->pParent)
	{
		if (!pChild->pSpriteCache || pChild->pSpriteCache->iAccumulate <= 0)
		{
			GEN_PERFINFO_START(pChild);
			ui_GenLayout(pChild);
			if (pChild->bPopup || pChild->pParent->bTopLevelChildren)
				ui_GenManageTopLevel(pChild);
			GEN_PERFINFO_STOP(pChild);
		}
		else if (pChild->bPopup || pChild->pParent->bTopLevelChildren || eaSize(&pChild->pSpriteCache->eaPopupChildren))
		{
			UIGen **eaPopupChildren = pChild->pSpriteCache->eaPopupChildren;
			S32 i;
			if (pChild->bPopup || pChild->pParent->bTopLevelChildren)
				ui_GenManageTopLevel(pChild);
			for (i = 0; i < eaSize(&eaPopupChildren); i++)
				ui_GenManageTopLevel(eaPopupChildren[i]);
		}
	}
	return false;
}

GEN_FORCE_INLINE_L2 static F32 GenGetRelativeOffset(UIGen *pGen, UIGenRelativeAtom *pAtom, UIDirection eTo)
{
	UIGen *pSibling = (pGen->pParent->pchName == pAtom->pchName) ? pGen->pParent : ui_GenGetSibling(pGen, pAtom->pchName);
	bool bFlip = false;
	if (!pSibling)
		pSibling = ui_GenGetChild(pGen, pAtom->pchName);
	if (!pSibling && pAtom->pchName == s_pch_Parent)
		pSibling = pGen->pParent;
	if (!pSibling && pAtom->pchName == s_pch_Previous)
	{
		if (eTo != pAtom->eOffset)
			bFlip = true;
		pSibling = pGen->pParent;
	}
	if (!pSibling)
		pSibling = RefSystem_ReferentFromString(g_GenState.hGenDict, pAtom->pchName);
	if (!UI_GEN_READY(pSibling))
	{
		ErrorFilenamef(pGen->pchFilename, "%s: Trying to position based on sibling %s, which does not exist or is not run before this gen", pGen->pchName, pAtom->pchName);
		return 0.f;
	}
	else
	{
		F32 fMagnitude = ((eTo & UITopLeft) ? pAtom->iSpacing : -pAtom->iSpacing) * pGen->pParent->fScale;
		// Normally we want to use the unpadded box when positioning relatively.
		// But if we're using Parent, we want to u
		const CBox *pBox = &pSibling->ScreenBox;
		if ((pSibling != pGen->pParent)
			|| (pGen->pResult && pGen->pResult->pos.bIgnoreParentPadding))
			pBox = &pSibling->UnpaddedScreenBox;
		if (bFlip)
		{
			switch (pAtom->eOffset)
			{
			case UIRight: return pBox->lx + fMagnitude;
			case UILeft: return pBox->hx + fMagnitude;
			case UIBottom: return pBox->ly + fMagnitude;
			case UITop: return pBox->hy + fMagnitude;
			default: return 0.f;
			}
		}
		else
		{
			switch (pAtom->eOffset)
			{
			case UILeft: return pBox->lx + fMagnitude;
			case UIRight: return pBox->hx + fMagnitude;
			case UITop: return pBox->ly + fMagnitude;
			case UIBottom: return pBox->hy + fMagnitude;
			default: return 0.f;
			}
		}
	}
}

void ui_GenHandleRelativeOffset(UIGen *pGen, CBox *pBox, UIGenRelative *pRelative)
{
	PERFINFO_AUTO_START_PIX(__FUNCTION__, 1);
	if (pRelative->LeftFrom.pchName && pRelative->RightFrom.pchName)
	{
		pBox->lx = GenGetRelativeOffset(pGen, &pRelative->LeftFrom, UILeft);
		pBox->hx = GenGetRelativeOffset(pGen, &pRelative->RightFrom, UIRight);
	}
	else if (pRelative->LeftFrom.pchName)
		CBoxMoveX(pBox, GenGetRelativeOffset(pGen, &pRelative->LeftFrom, UILeft));
	else if (pRelative->RightFrom.pchName)
		CBoxMoveX(pBox, GenGetRelativeOffset(pGen, &pRelative->RightFrom, UIRight) - CBoxWidth(pBox));

	if (pRelative->TopFrom.pchName && pRelative->BottomFrom.pchName)
	{
		pBox->ly = GenGetRelativeOffset(pGen, &pRelative->TopFrom, UITop);
		pBox->hy = GenGetRelativeOffset(pGen, &pRelative->BottomFrom, UIBottom);
	}
	else if (pRelative->TopFrom.pchName)
		CBoxMoveY(pBox, GenGetRelativeOffset(pGen, &pRelative->TopFrom, UITop));
	else if (pRelative->BottomFrom.pchName)
		CBoxMoveY(pBox, GenGetRelativeOffset(pGen, &pRelative->BottomFrom, UIBottom) - CBoxHeight(pBox));
	PERFINFO_AUTO_STOP_PIX();
}

__forceinline static bool ui_GenBoundsMax(F32 *v2Result, F32 *v2Bounds)
{
	MAX1(v2Result[0], v2Bounds[0]);
	MAX1(v2Result[1], v2Bounds[1]);
	return false;
}

bool ui_GenAddBoundsHeight(UIGen *pGen, F32 *v2Size)
{
	Vec2 v2 = {0, 0};
	ui_GenGetBounds(pGen, v2);
	v2Size[1] += v2[1];
	return false;
}

bool ui_GenAddBoundsWidth(UIGen *pGen, F32 *v2Size)
{
	Vec2 v2 = {0, 0};
	ui_GenGetBounds(pGen, v2);
	v2Size[0] += v2[0];
	return false;
}

GEN_FORCE_INLINE_L2 static bool ui_GenAddRelativeBounds(UIGen *pGen, F32 *v2Size, UIDirection eAllowedDirs)
{
	UIGenRelative *pRelative = NULL;

	if (pGen->bRecursionLocked)
	{
		ErrorFilenamef(pGen->pchFilename, "You've constructed a set of gens with an infinitely recursive layout algorithm. %s is somehow involved. Stop using relative positioning and FitContents together.", pGen->pchName);
		return false;
	}

	pGen->bRecursionLocked = true;

	if (pGen->pResult && (pRelative = pGen->pResult->pRelative))
	{
		if (pRelative->LeftFrom.pchName && (eAllowedDirs & UILeft) && !pRelative->RightFrom.pchName)
		{
			UIGen *pSibling = ui_GenGetSiblingEx(pGen, pRelative->LeftFrom.pchName, UI_GEN_LAYOUT_ORDER, true);
			if (pSibling)
			{
				Vec2 v2SiblingSize = {0, 0};
				ui_GenAddBoundsWidth(pSibling, v2SiblingSize);
				if (pRelative->LeftFrom.eOffset & UILeft)
				{
					MAX1(v2Size[0], pRelative->LeftFrom.iSpacing + v2SiblingSize[0]);
				}
				else
					v2Size[0] += pRelative->LeftFrom.iSpacing + v2SiblingSize[0];
			}
		}
		else if (pRelative->RightFrom.pchName && (eAllowedDirs & UIRight) && !pRelative->LeftFrom.pchName)
		{
			UIGen *pSibling = ui_GenGetSiblingEx(pGen, pRelative->RightFrom.pchName, UI_GEN_LAYOUT_ORDER, true);
			if (pSibling)
			{
				Vec2 v2SiblingSize = {0, 0};
				ui_GenAddBoundsWidth(pSibling, v2SiblingSize);
				if (pRelative->RightFrom.eOffset & UIRight)
				{
					MAX1(v2Size[0], pRelative->RightFrom.iSpacing + v2SiblingSize[0]);
				}
				else
					v2Size[0] += pRelative->RightFrom.iSpacing + v2SiblingSize[0];
			}
		}

		if (pRelative->TopFrom.pchName && (eAllowedDirs & UITop) && !pRelative->BottomFrom.pchName)
		{
			UIGen *pSibling = ui_GenGetSiblingEx(pGen, pRelative->TopFrom.pchName, UI_GEN_LAYOUT_ORDER, true);
			if (pSibling)
			{
				Vec2 v2SiblingSize = {0, 0};
				ui_GenAddBoundsHeight(pSibling, v2SiblingSize);
				if (pRelative->TopFrom.eOffset & UITop)
				{
					MAX1(v2Size[1], pRelative->TopFrom.iSpacing + v2SiblingSize[1]);
				}
				else
					v2Size[1] += pRelative->TopFrom.iSpacing + v2SiblingSize[1];
			}
		}
		else if (pRelative->BottomFrom.pchName && (eAllowedDirs & UIBottom) && !pRelative->TopFrom.pchName)
		{
			UIGen *pSibling = ui_GenGetSiblingEx(pGen, pRelative->BottomFrom.pchName, UI_GEN_LAYOUT_ORDER, true);
			if (pSibling)
			{
				Vec2 v2SiblingSize = {0, 0};
				ui_GenAddBoundsHeight(pSibling, v2SiblingSize);
				if (pRelative->BottomFrom.eOffset & UIBottom)
				{
					MAX1(v2Size[1], pRelative->BottomFrom.iSpacing + v2SiblingSize[1]);
				}
				else
					v2Size[1] += pRelative->BottomFrom.iSpacing + v2SiblingSize[1];
			}
		}
	}

	pGen->bRecursionLocked = false;

	return false;
}

F32 ui_GenGetBoundsDimension(UISizeSpec *pSpec, UISizeSpec *pMinSpec, UISizeSpec *pMaxSpec, F32 fParentSize, F32 fRatioSize, F32 fMargin, F32 fScale)
{
	F32 fSize = ui_GenPositionSpecToScalar(pSpec, 0, fParentSize, 0, fRatioSize, fMargin, fScale);
	F32 fMinSize = ui_GenPositionSpecToScalar(pMinSpec, 0, fParentSize, 0, fRatioSize, fMargin, fScale);
	F32 fMaxSize = ui_GenPositionSpecToScalar(pMaxSpec, 0, fParentSize, 0, fRatioSize, fMargin, fScale);
	if (fMinSize > 0 && fSize < fMinSize) fSize = fMinSize;
	if (fMaxSize > 0 && fSize > fMaxSize) fSize = fMaxSize;
	return fSize;
}

bool ui_GenGetBounds(UIGen *pChild, F32 *v2OutSize)
{
	Vec2 v2Size = {0, 0};
	if (pChild)
	{
		UIGenInternal *pInt = pChild->pResult ? pChild->pResult : ui_GenGetBase(pChild, false);
		UIPosition *pPos = &pInt->pos;
		CBox FitContents = {0, 0, 0, 0};
		CBox *pBoundingBox = &pChild->BoundingBox;
		F32 fWidth;
		F32 fHeight;

		if (!pInt)
			return false;
		if (ui_GenUsesUnit(pPos, UIUnitFitContents))
			ui_GenFitContentsSize(pChild, pInt, &FitContents);

		// If using ratio the order height and width are calculated is important
		if (pPos->Height.eUnit == UIUnitRatio
			|| pPos->MinimumHeight.eUnit == UIUnitRatio
			|| pPos->MaximumHeight.eUnit == UIUnitRatio)
		{
			fWidth = ui_GenGetBoundsDimension(&pPos->Width, &pPos->MinimumWidth, &pPos->MaximumWidth, CBoxWidth(pBoundingBox), 0, pPos->iLeftMargin + pPos->iRightMargin, pPos->fScale);
			fHeight = ui_GenGetBoundsDimension(&pPos->Height, &pPos->MinimumHeight, &pPos->MaximumHeight, CBoxHeight(pBoundingBox), fWidth, pPos->iTopMargin + pPos->iBottomMargin, pPos->fScale);
		}
		else
		{
			fHeight = ui_GenGetBoundsDimension(&pPos->Height, &pPos->MinimumHeight, &pPos->MaximumHeight, CBoxHeight(pBoundingBox), 0, pPos->iTopMargin + pPos->iBottomMargin, pPos->fScale);
			fWidth = ui_GenGetBoundsDimension(&pPos->Width, &pPos->MinimumWidth, &pPos->MaximumWidth, CBoxWidth(pBoundingBox), fHeight, pPos->iLeftMargin + pPos->iRightMargin, pPos->fScale);
		}

		if (fWidth)
		{
			MAX1(v2Size[0], pPos->iX + fWidth + pPos->iLeftMargin + pPos->iRightMargin);
			ui_GenAddRelativeBounds(pChild, v2Size, UILeft|UIRight);
		}
		if (fHeight)
		{
			MAX1(v2Size[1], pPos->iY + fHeight + pPos->iTopMargin + pPos->iBottomMargin);
			ui_GenAddRelativeBounds(pChild, v2Size, UITop|UIBottom);
		}
		ui_GenBoundsMax(v2OutSize, v2Size);
	}
	return false;
}

void ui_AlignCBox(const CBox *pParent, CBox *pChild, UIDirection eAlignment)
{
	if (eAlignment & UILeft)
		CBoxMoveX(pChild, pParent->lx);
	else if (eAlignment & UIRight)
		CBoxMoveX(pChild, pParent->hx - CBoxWidth(pChild));
	else
		CBoxMoveX(pChild, ((pParent->lx + pParent->hx) - CBoxWidth(pChild)) / 2);

	if (eAlignment & UITop)
		CBoxMoveY(pChild, pParent->ly);
	else if (eAlignment & UIBottom)
		CBoxMoveY(pChild, pParent->hy - CBoxHeight(pChild));
	else
		CBoxMoveY(pChild, ((pParent->ly + pParent->hy) - CBoxHeight(pChild)) / 2);
}

UIGenTimer *ui_GenFindTimer(UIGen *pGen, const char *pchName)
{
    int i;
    if(!pGen || !pchName)
        return NULL;
    
    for (i = 0; i < eaSize(&pGen->eaTimers); i++)
	{
		UIGenTimer *pTimer = pGen->eaTimers[i];
		if (pTimer->pchName && !stricmp(pchName, pTimer->pchName))
            return pTimer;
    }        
    return NULL;
}

void ui_GenManageTopLevel(UIGen *pGen)
{
	if (pGen)
	{
		eaPushUnique(&g_GenState.eaManagedTopLevel, pGen);
	}
}

void ui_GenRaiseTopLevel(UIGen *pGen)
{
	if (pGen)
	{
		// Removing something from the comparitor will make it rise again.
		eaFindAndRemove(&g_GenState.eaManagedTopLevelComparitor, pGen);
	}
}

PerfInfoStaticData **ui_GenGetCommonPerfInfo(const char *pchName)
{
	union {
		PerfInfoStaticData **ppResult;
		void *pPointer;
	} u;

	if (!g_GenState.stTimingCommonData)
	{
		g_GenState.stTimingCommonData = stashTableCreateWithStringKeys(16384, StashDeepCopyKeys_NeverRelease | StashCaseInsensitive);
	}

	if (stashFindPointer(g_GenState.stTimingCommonData, pchName, &u.pPointer))
	{
		return u.ppResult;
	}

	u.pPointer = malloc(sizeof(PerfInfoStaticData *));
	stashAddPointer(g_GenState.stTimingCommonData, pchName, u.pPointer, true);
	return u.ppResult;
}

__forceinline static int ui_GenLerpRGBAColorsRound(int a, int b, F32 weight)
{
	int out;
	U8* pout = (U8 *)(&out);
	U8* pa = (U8 *)(&a);
	U8* pb = (U8 *)(&b);
	int i;
	F32 weightinv = 1.f - weight;
	for (i = 0; i < 4; i++)
		pout[i] = (pa[i] * weightinv + pb[i] * weight + 0.5f);
	return out;
}

// IF YOU CHANGE THIS FUNCTION, YOU SHOULD CHANGE DrawNinePatch AND display_sprite_NinePatch_test
void ui_GenDrawNinePatchAtlas(const UIGenBundleTexture *pBundle,
							  CBox *pBox,
							  AtlasTex *pTex, const NinePatch *pNinePatch,
							  AtlasTex *pRender, AtlasTex *pMask,
							  F32 fZ,
							  F32 fScaleX, F32 fScaleY,
							  U32 uTopLeftColor, U32 uTopRightColor,
							  U32 uBottomRightColor, U32 uBottomLeftColor)
{
	const F32 fX = pBox->lx;
	const F32 fY = pBox->ly;
	const F32 fWidth = pBox->hx - pBox->lx;
	const F32 fHeight = pBox->hy - pBox->ly;
	const F32 fInvTexWidth = 1.f / pTex->width;
	const F32 fInvTexHeight = 1.f / pTex->height;
	const F32 fInvWidth = 1.f / fWidth;
	const F32 fInvHeight = 1.f / fHeight;
	F32 afSizeX[3] = {pNinePatch->stretchableX[0] * fScaleX, -1, (pTex->width - pNinePatch->stretchableX[1] - 1)  * fScaleX};
	F32 afSizeY[3] = {pNinePatch->stretchableY[0] * fScaleY, -1, (pTex->height - pNinePatch->stretchableY[1] - 1) * fScaleY};
	F32 afScaleX[3];
	F32 afScaleY[3];
	F32 afU[2];
	F32 afV[2];
	U32 auiColors[4][4]; // vertex color, [Y][X] left to right, top to bottom
	F32 afFinalX[3];
	F32 afFinalY[3];
	F32 afFinalU[4];
	F32 afFinalV[4];
	F32 afOtherU[4];
	F32 afOtherV[4];
	F32 afInsetU[3] = {0};
	F32 afInsetV[3] = {0};
	const bool bAdditive = pBundle->bAdditive;

	{
		afSizeX[1] = fWidth - (afSizeX[2] + afSizeX[0]);
		afSizeY[1] = fHeight - (afSizeY[2] + afSizeY[0]);
		MAX1F(afSizeX[1], 0);
		MAX1F(afSizeY[1], 0);
		if (afSizeX[0] + afSizeX[2] > fWidth)
			scaleVec3(afSizeX, fWidth / (float)(afSizeX[0] + afSizeX[2]), afSizeX);
		if (afSizeY[0] + afSizeY[2] > fHeight)
			scaleVec3(afSizeY, fHeight / (float)(afSizeY[0] + afSizeY[2]), afSizeY);
		scaleVec3(afSizeX, fInvTexWidth, afScaleX);
		scaleVec3(afSizeY, fInvTexHeight, afScaleY);
		afFinalX[0] = fX;
		afFinalX[1] = fX + afSizeX[0];
		afFinalX[2] = fX + (afSizeX[0] + afSizeX[1]);
		afFinalY[0] = fY;
		afFinalY[1] = fY + afSizeY[0];
		afFinalY[2] = fY + (afSizeY[0] + afSizeY[1]);
	}

	{
		scaleVec2(pNinePatch->stretchableX, fInvTexWidth, afU);
		afFinalU[0] = 0;
		afFinalU[1] = afU[0];
		afFinalU[2] = afU[1] + fInvTexWidth;
		afFinalU[3] = 1;

		scaleVec2(pNinePatch->stretchableY, fInvTexHeight, afV);
		afFinalV[0] = 0;
		afFinalV[1] = afV[0];
		afFinalV[2] = afV[1] + fInvTexHeight;
		afFinalV[3] = 1;

		afInsetU[1] = 0.5f*fInvTexWidth;
		afInsetV[1] = 0.5f*fInvTexHeight;

		afOtherU[0] = 0;
		afOtherU[1] = afSizeX[0] * fInvWidth;
		afOtherU[2] = (afSizeX[0] + afSizeX[1]) * fInvWidth;
		afOtherU[3] = 1;

		afOtherV[0] = 0;
		afOtherV[1] = afSizeY[0] * fInvHeight;
		afOtherV[2] = (afSizeY[0] + afSizeY[1]) * fInvHeight;
		afOtherV[3] = 1;
	}

	{
		auiColors[0][0] = uTopLeftColor;
		auiColors[0][1] = ui_GenLerpRGBAColorsRound(uTopLeftColor, uTopRightColor, afSizeX[0] * fInvWidth);
		auiColors[0][2] = ui_GenLerpRGBAColorsRound(uTopLeftColor, uTopRightColor, (afSizeX[0] + afSizeX[1]) * fInvWidth);
		auiColors[0][3] = uTopRightColor;

		auiColors[3][0] = uBottomLeftColor;
		auiColors[3][1] = ui_GenLerpRGBAColorsRound(uBottomLeftColor, uBottomRightColor, afSizeX[0] * fInvWidth);
		auiColors[3][2] = ui_GenLerpRGBAColorsRound(uBottomLeftColor, uBottomRightColor, (afSizeX[0] + afSizeX[1]) * fInvWidth);
		auiColors[3][3] = uBottomRightColor;

		auiColors[1][0] = ui_GenLerpRGBAColorsRound(auiColors[0][0], auiColors[3][0], afSizeY[0] * fInvHeight);
		auiColors[1][1] = ui_GenLerpRGBAColorsRound(auiColors[0][1], auiColors[3][1], afSizeY[0] * fInvHeight);
		auiColors[1][2] = ui_GenLerpRGBAColorsRound(auiColors[0][2], auiColors[3][2], afSizeY[0] * fInvHeight);
		auiColors[1][3] = ui_GenLerpRGBAColorsRound(auiColors[0][3], auiColors[3][3], afSizeY[0] * fInvHeight);

		auiColors[2][0] = ui_GenLerpRGBAColorsRound(auiColors[0][0], auiColors[3][0], (afSizeY[0] + afSizeY[1]) * fInvHeight);
		auiColors[2][1] = ui_GenLerpRGBAColorsRound(auiColors[0][1], auiColors[3][1], (afSizeY[0] + afSizeY[1]) * fInvHeight);
		auiColors[2][2] = ui_GenLerpRGBAColorsRound(auiColors[0][2], auiColors[3][2], (afSizeY[0] + afSizeY[1]) * fInvHeight);
		auiColors[2][3] = ui_GenLerpRGBAColorsRound(auiColors[0][3], auiColors[3][3], (afSizeY[0] + afSizeY[1]) * fInvHeight);
	}

#define DISPLAY_NINEPATCH_PATCH_REND9(x, y)											\
	display_sprite_ex(pRender, NULL, pMask, NULL,									\
		afFinalX[x], afFinalY[y], fZ,												\
		afScaleX[x], afScaleY[y],													\
		auiColors[y][x], auiColors[y][x+1], auiColors[y+1][x+1], auiColors[y+1][x],	\
		afFinalU[x]+afInsetU[x], afFinalV[y]+afInsetV[y], afFinalU[x+1]-afInsetU[x], afFinalV[y+1]-afInsetV[y],						\
		afOtherU[x], afOtherV[y], afOtherU[x+1], afOtherV[y+1],						\
		0, bAdditive, clipperGetCurrent())
#define DISPLAY_NINEPATCH_PATCH_MASK9(x, y)											\
	display_sprite_ex(pRender, NULL, pMask, NULL,									\
		afFinalX[x], afFinalY[y], fZ,												\
		afScaleX[x], afScaleY[y],													\
		auiColors[y][x], auiColors[y][x+1], auiColors[y+1][x+1], auiColors[y+1][x],	\
		afOtherU[x], afOtherV[y], afOtherU[x+1], afOtherV[y+1],						\
		afFinalU[x]+afInsetU[x], afFinalV[y]+afInsetV[y], afFinalU[x+1]-afInsetU[x], afFinalV[y+1]-afInsetV[y],						\
		0, bAdditive, clipperGetCurrent())

	if (pTex == pRender)
	{
		DISPLAY_NINEPATCH_PATCH_REND9(0, 0);
		DISPLAY_NINEPATCH_PATCH_REND9(1, 0);
		DISPLAY_NINEPATCH_PATCH_REND9(2, 0);
		if (afScaleY[1] > 0)
		{
			DISPLAY_NINEPATCH_PATCH_REND9(0, 1);
			DISPLAY_NINEPATCH_PATCH_REND9(1, 1);
			DISPLAY_NINEPATCH_PATCH_REND9(2, 1);
		}
		DISPLAY_NINEPATCH_PATCH_REND9(0, 2);
		DISPLAY_NINEPATCH_PATCH_REND9(1, 2);
		DISPLAY_NINEPATCH_PATCH_REND9(2, 2);
	}
	else
	{
		DISPLAY_NINEPATCH_PATCH_MASK9(0, 0);
		DISPLAY_NINEPATCH_PATCH_MASK9(1, 0);
		DISPLAY_NINEPATCH_PATCH_MASK9(2, 0);
		if (afScaleY[1] > 0)
		{
			DISPLAY_NINEPATCH_PATCH_MASK9(0, 1);
			DISPLAY_NINEPATCH_PATCH_MASK9(1, 1);
			DISPLAY_NINEPATCH_PATCH_MASK9(2, 1);
		}
		DISPLAY_NINEPATCH_PATCH_MASK9(0, 2);
		DISPLAY_NINEPATCH_PATCH_MASK9(1, 2);
		DISPLAY_NINEPATCH_PATCH_MASK9(2, 2);
	}

#undef DISPLAY_NINEPATCH_PATCH_MASK9
#undef DISPLAY_NINEPATCH_PATCH_REND9
}

void ui_GenInitTimer()
{
	if (isDevelopmentMode() && (UserIsInGroup("Software") || UserIsInGroup("UI")))
	{
		g_CurrentGenTimerTD = g_SpecialGenTimerTD = timerMakeSpecialTD();

		if (autoTimersPublicState.connected)
		{
			g_bProfilerConnected = true;
			g_CurrentGenTimerTD = timerGetCurrentThreadData();
		}
		else
		{
			g_bUsingSpecialTimer = true;
		}
	}
	else
	{
		g_CurrentGenTimerTD = timerGetCurrentThreadData();
	}
}

void ui_GenConnectTimer(bool bConnected)
{
	g_bProfilerConnected = bConnected;
}

void ui_GenStartTimings()
{
	// don't bother doing any of this if we didn't make the special dev-mode timer
	if (g_SpecialGenTimerTD)
	{
		// before we do any gen timing blocks, sort out what mode we're running in.  We do this here to avoid threading issues.
		if (g_bProfilerConnected == g_bUsingSpecialTimer)
		{
			if (g_bProfilerConnected)
			{
				g_CurrentGenTimerTD = timerGetCurrentThreadData();
			}
			else
			{
				g_CurrentGenTimerTD = g_SpecialGenTimerTD;
			}
			g_bUsingSpecialTimer = !g_bProfilerConnected;
		}
	}
	else
	{
		g_CurrentGenTimerTD = timerGetCurrentThreadData();
	}
}

static TimingProfilerReport g_aGenTimingResults[10];
static int g_iNumGenTimingResults;

void ui_GenProcessTimings()
{
	if (!g_bProfilerConnected && g_bUsingSpecialTimer && isDevelopmentMode() && showDevUI())
	{
		static int iBudget = 600000;
		static int iCounter=0;
		
		iCounter++;
		if (iCounter > 100)
		{
			g_iNumGenTimingResults = timerGetPIOverBudget(iBudget,g_aGenTimingResults,g_SpecialGenTimerTD,100);
			iCounter = 0;
		}
		if (g_iNumGenTimingResults > 1)
		{
			int iLargest=0;
			int i;
			for (i=1;i<g_iNumGenTimingResults;i++)
			{
				if (g_aGenTimingResults[i].iTime > g_aGenTimingResults[iLargest].iTime)
				{
					iLargest = i;
				}
			}
			gfxDebugPrintfQueueColor(0xff8811ff,"WARNING: %d UIGens are using too much CPU. ",g_iNumGenTimingResults);
			gfxDebugPrintfQueueColor(0xff8811ff,"The largest is \"%s\". ",g_aGenTimingResults[iLargest].pchTimerName);
		}
	}
}

// currently requires a 2000 char buffer (200 * 10)
void ui_GenPrintTimingResults(char * pszOutput)
{
	if (!g_bProfilerConnected && g_bUsingSpecialTimer && isDevelopmentMode())
	{
		if (g_iNumGenTimingResults > 1)
		{
			int i;
			int iBufferLeft=2000;
			pszOutput[0] = 0;
			pszOutput[iBufferLeft-2] = '\n';
			pszOutput[iBufferLeft-1] = 0;
			iBufferLeft -= 2;
			for (i=0;i<g_iNumGenTimingResults;i++)
			{
				int iStrLen;
				quick_sprintf(pszOutput,iBufferLeft,"%s: %dk\n",g_aGenTimingResults[i].pchTimerName,g_aGenTimingResults[i].iTime/1000);
				iStrLen = strlen(pszOutput);
				pszOutput += iStrLen;
				iBufferLeft -= iStrLen;
			}
		}
		else
		{
			// someone might want to see gen timing info, even if a gen isn't over budget.  We'll deal with that problem
			// when it happens, after the big party.
			quick_sprintf(pszOutput,2000,"All gens in budget.");
		}
	}
	else
	{
		quick_sprintf(pszOutput,2000,"Gen timing not currently enabled.");
	}
}