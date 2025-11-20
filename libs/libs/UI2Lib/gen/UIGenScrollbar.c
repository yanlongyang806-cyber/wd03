#include "earray.h"
#include "ResourceManager.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "inputGamepad.h"
#include "GraphicsLib.h"
#include "GfxSprite.h"

#include "UICore.h"
#include "UIGen.h"
#include "UIGenScrollbar.h"
#include "UITextureAssembly.h"
#include "UICore_h_ast.h"
#include "UIGenScrollbar_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define UI_GEN_CHOOSE_SCROLLBAR(pBar, pState) \
	((pState)->bUseDisabled && GET_REF((pBar)->hDisabledScrollbarDef) \
		? GET_REF((pBar)->hDisabledScrollbarDef) \
		: GET_REF((pBar)->hScrollbarDef))

S32 ui_GenScrollbarWidth(UIGenScrollbar *pBar, UIGenScrollbarState *pState)
{
	UIGenScrollbarDef *pScrollbarDef = UI_GEN_CHOOSE_SCROLLBAR(pBar, pState);
	return pState->bUnneeded ? 0 : SAFE_MEMBER(pScrollbarDef, iWidth) + pBar->iScrollbarRightMargin + pBar->iScrollbarLeftMargin;
}

static void GenGetScrollbarParams(UIGenScrollbar *pBar, UIGenScrollbarState *pState, F32 fTop, F32 fBottom, F32 fVisibleHeight, F32 fMinHandle, F32 *pfPageCount, F32 *pfHandleHeight, F32 *pfHandleTop, F32 *pfAvailableSpace)
{
	F32 fTotalHeight = pState->fTotalHeight;
	F32 fPageCount = fVisibleHeight ? max(1, fTotalHeight / fVisibleHeight) : 1;
	F32 fHandleHeight = max(fMinHandle, (fBottom - fTop) / max(fPageCount, 1));
	F32 fAvailableSpace = (fBottom - fTop) - fHandleHeight;
	F32 fHandleTop = fTop;
	if (fTotalHeight != fVisibleHeight)
		fHandleTop = fTop + (pState->fScrollPosition / (pState->fTotalHeight - fVisibleHeight) * fAvailableSpace);
	if (pfPageCount)
		*pfPageCount = fPageCount;
	if (pfHandleHeight)
		*pfHandleHeight = fHandleHeight;
	if (pfHandleTop)
		*pfHandleTop = fHandleTop;
	if (pfAvailableSpace)
		*pfAvailableSpace = fAvailableSpace;
}

void ui_GenLayoutScrollbar(UIGen *pGen, UIGenScrollbar *pBar, UIGenScrollbarState *pState, F32 fTotalHeight)
{
	F32 fHeight = CBoxHeight(&pGen->ScreenBox);
	bool bFullHeight = pState->bUseDisabled = fTotalHeight <= fHeight + 0.95;
	UIGenScrollbarDef *pScrollbarDef = UI_GEN_CHOOSE_SCROLLBAR(pBar, pState);
	F32 fScale = pGen->fScale;
	F32 fTop = pGen->ScreenBox.ly + pBar->iScrollbarTopMargin * pGen->fScale;
	F32 fBottom = pGen->ScreenBox.hy - pBar->iScrollbarBottomMargin * pGen->fScale;
	F32 fLeft = (SAFE_MEMBER(pScrollbarDef, eOffsetFrom) == UIRight) 
		? pGen->ScreenBox.hx - (pBar->iScrollbarRightMargin * pGen->fScale) - (SAFE_MEMBER(pScrollbarDef, iWidth) * pGen->fScale) 
		: pGen->ScreenBox.lx + (pBar->iScrollbarLeftMargin * pGen->fScale);
	F32 fRight = (SAFE_MEMBER(pScrollbarDef, eOffsetFrom) == UIRight) 
		? pGen->ScreenBox.hx - (pBar->iScrollbarRightMargin * pGen->fScale)
		: pGen->ScreenBox.lx + (pBar->iScrollbarLeftMargin * pGen->fScale) + (SAFE_MEMBER(pScrollbarDef, iWidth) * pGen->fScale);
	F32 fHandleHeight;
	F32 fHandleTop;
	F32 fUsableSpace;
	F32 fMinHandle = SAFE_MEMBER(pScrollbarDef, iMinHandleHeight) * fScale;
	CBox box;
	bool bUnneeded = !pScrollbarDef || (pBar->bScrollbarOnlyShowWhenNeeded && bFullHeight);
	
	if (!pScrollbarDef)
	{
		pState->bUnneeded = true;
		pState->fScrollPosition = 0;
		return;
	}
	BuildCBox(&box, fLeft, fTop, pScrollbarDef->iWidth * fScale, pScrollbarDef->iTopHeight * fScale);
	pState->pTop = GET_REF(pScrollbarDef->hTopDefault);
	if (mouseCollision(&box) && GET_REF(pScrollbarDef->hTopHover))
		pState->pTop = GET_REF(pScrollbarDef->hTopHover);
	if (mouseDownOver(MS_LEFT, &box) && GET_REF(pScrollbarDef->hTopMouseDown))
		pState->pTop = GET_REF(pScrollbarDef->hTopMouseDown);
	if (pState->pTop)
	{
		BuildCBox(&box, fLeft, fTop, pScrollbarDef->iWidth * fScale, pScrollbarDef->iTopHeight * fScale);
		ui_GenState(pGen, kUIGenStateScrollingUp, mouseDownOver(MS_LEFT, &box));
		fTop += pScrollbarDef->iTopHeight * fScale;
	}
	BuildCBox(&box, 
		fLeft, fBottom - pScrollbarDef->iBottomHeight * fScale, 
		pScrollbarDef->iWidth * fScale, pScrollbarDef->iBottomHeight * fScale);
	pState->pBottom = GET_REF(pScrollbarDef->hBottomDefault);
	if (mouseCollision(&box) && GET_REF(pScrollbarDef->hBottomHover))
		pState->pBottom = GET_REF(pScrollbarDef->hBottomHover);
	if (mouseDownOver(MS_LEFT, &box) && GET_REF(pScrollbarDef->hBottomMouseDown))
		pState->pBottom = GET_REF(pScrollbarDef->hBottomMouseDown);

	pState->fTotalHeight = fTotalHeight;

	if (pState->pTop)
		fTop += pScrollbarDef->iTopHeight;
	if (pState->pBottom)
		fBottom -= pScrollbarDef->iBottomHeight;

	GenGetScrollbarParams(pBar, pState, fTop, fBottom, fHeight, fMinHandle, NULL, &fHandleHeight, &fHandleTop, &fUsableSpace);

	BuildCBox(&box, fRight - pScrollbarDef->iWidth * fScale, fHandleTop, pScrollbarDef->iWidth * fScale, fHandleHeight);
	pState->pHandle = GET_REF(pScrollbarDef->hHandleDefault);
	if (mouseCollision(&box) && GET_REF(pScrollbarDef->hHandleHover))
		pState->pHandle = GET_REF(pScrollbarDef->hHandleHover);
	if (((mouseCollision(&box) && mouseDown(MS_LEFT)) || ui_GenInState(pGen, kUIGenStateDragging)) && GET_REF(pScrollbarDef->hHandleMouseDown))
		pState->pHandle = GET_REF(pScrollbarDef->hHandleMouseDown);
	
	pState->bHideHandle = fHeight < (fScale * (pScrollbarDef->iTopHeight + pScrollbarDef->iBottomHeight) + fHandleHeight);

	if (!pState->pHandle)
		bUnneeded = true;
	if (bUnneeded && !pState->bUnneeded || !bUnneeded && pState->bUnneeded)
	{
		if (pState->iHysteresis++ >= 3)
			pState->bUnneeded = bUnneeded;
	}
	else
	{
		pState->iHysteresis = 0;
	}

	if (pState->bUnneeded)
		pState->fScrollPosition = 0;
}

void ui_GenScrollbarBox(UIGenScrollbar *pBar, UIGenScrollbarState *pState, const CBox* pIn, CBox *pOut, F32 fScale)
{
	UIGenScrollbarDef *pScrollbarDef = UI_GEN_CHOOSE_SCROLLBAR(pBar, pState);
	assert(pIn && pOut && pBar);
	*pOut = *pIn;
	if(pScrollbarDef && !pState->bUnneeded)
	{
		if (pScrollbarDef->eOffsetFrom == UILeft)
			pOut->lx += ui_GenScrollbarWidth(pBar, pState) * fScale;
		else if (pScrollbarDef->eOffsetFrom == UIRight)
			pOut->hx -= ui_GenScrollbarWidth(pBar, pState) * fScale;
	}
}

static void ScrollbarHandleWheel(UIGen *pGen, UIGenScrollbar *pBar, UIGenScrollbarState *pState)
{
	if (mouseZ()
		&& mouseBoxCollisionTest(g_ui_State.mouseX, g_ui_State.mouseY, &pGen->UnpaddedScreenBox))
	{
		UIGenInternal *pResult = pGen->pResult;
		F32 fHeight = CBoxHeight(&pGen->ScreenBox);
		F32 fOffSpace = max(pState->fTotalHeight - fHeight, 0);
		F32 fNewPosition = pState->fScrollPosition - mouseZ() * 20 * pGen->fScale;
		fNewPosition = floorf(CLAMP(fNewPosition, 0, fOffSpace));
		
		if (!UI_GEN_NEARF(fNewPosition, pState->fScrollPosition))
		{
			pState->fScrollPosition = fNewPosition;
			if (pBar->bScrollbarCaptureMouse || pResult->bCaptureMouse)
				inpHandled();
			mouseCaptureZ();
		}
		inpCapture(INP_MOUSEWHEEL_BACKWARD);
		inpCapture(INP_MOUSEWHEEL_FORWARD);
	}
}

static void ScrollbarHandleStick(UIGen *pGen, UIGenScrollbar *pBar, UIGenScrollbarState *pState)
{
	if (ui_GenInState(pGen, kUIGenStateFocused)
		|| ui_GenInState(pGen, kUIGenStateFocusedChild)
		|| ui_GenInState(pGen, kUIGenStateFocusedAncestor))
	{
		F32 fHeight = CBoxHeight(&pGen->ScreenBox);
		F32 fOffSpace = max(pState->fTotalHeight - fHeight, 0);
		F32 fNewPosition;
		F32 fDiff;
		F32 fY;
		// TODO: Acceleration.
		gamepadGetRightStick(NULL, &fY);
		fDiff = fY * 300 * pGen->fScale * pGen->fTimeDelta;
		fNewPosition = pState->fScrollPosition - fDiff;
		fNewPosition = floorf(CLAMP(fNewPosition, 0, fOffSpace));
		if (!UI_GEN_NEARF(fNewPosition, pState->fScrollPosition))
		{
			pState->fScrollPosition = fNewPosition;
			gamepadCaptureRightStick();
		}
	}
}

void ui_GenTickScrollbar(UIGen *pGen, UIGenScrollbar *pBar, UIGenScrollbarState *pState)
{
	UIGenScrollbarDef *pScrollbarDef = UI_GEN_CHOOSE_SCROLLBAR(pBar, pState);
	UIGenInternal *pResult = pGen->pResult;
	UITextureAssembly *pTrough = pScrollbarDef ? GET_REF(pScrollbarDef->hTrough) : NULL;
	F32 fLeft = (SAFE_MEMBER(pScrollbarDef, eOffsetFrom) == UIRight) 
		? pGen->ScreenBox.hx - (pBar->iScrollbarRightMargin * pGen->fScale) - (SAFE_MEMBER(pScrollbarDef, iWidth) * pGen->fScale) 
		: pGen->ScreenBox.lx + (pBar->iScrollbarLeftMargin * pGen->fScale);
	F32 fRight = (SAFE_MEMBER(pScrollbarDef, eOffsetFrom) == UIRight) 
		? pGen->ScreenBox.hx - (pBar->iScrollbarRightMargin * pGen->fScale)
		: pGen->ScreenBox.lx + (pBar->iScrollbarLeftMargin * pGen->fScale) + (SAFE_MEMBER(pScrollbarDef, iWidth) * pGen->fScale);
	F32 fTop = pGen->ScreenBox.ly + pBar->iScrollbarTopMargin * pGen->fScale;
	F32 fBottom = pGen->ScreenBox.hy - pBar->iScrollbarBottomMargin * pGen->fScale;
	F32 fHeight = CBoxHeight(&pGen->ScreenBox);
	F32 fOffSpace = max(pState->fTotalHeight - fHeight, 0);
	F32 fScale = pGen->fScale;
	F32 fHandleHeight;
	F32 fHandleTop;
	F32 fUsableSpace = 0;
	F32 fMinHandle = SAFE_MEMBER(pScrollbarDef, iMinHandleHeight) * fScale;
	CBox box;

	if (!pScrollbarDef || pState->bUnneeded)
		return;
	
	if (pState->pTop)
	{
		BuildCBox(&box, fLeft, fTop, pScrollbarDef->iWidth * fScale, pScrollbarDef->iTopHeight * fScale);
		ui_GenState(pGen, kUIGenStateScrollingUp, mouseDownOver(MS_LEFT, &box));
		fTop += pScrollbarDef->iTopHeight * fScale;
	}

	if (pState->pBottom)
	{
		BuildCBox(&box, 
			fLeft, fBottom - pScrollbarDef->iBottomHeight * fScale - pBar->iScrollbarBottomMargin * pGen->fScale,
			pScrollbarDef->iWidth * fScale, pScrollbarDef->iTopHeight * fScale);
		ui_GenState(pGen, kUIGenStateScrollingDown, mouseDownOver(MS_LEFT, &box));
		fBottom -= pScrollbarDef->iBottomHeight * fScale;
	}

	GenGetScrollbarParams(pBar, pState, fTop, fBottom, fHeight, fMinHandle, NULL, &fHandleHeight, &fHandleTop, &fUsableSpace);
	

	if (!(pState->pHandle && mouseIsDown(MS_LEFT)))
	{
		if (ui_GenInState(pGen, kUIGenStateDragging))
		{
			ui_GenUnsetState(pGen, kUIGenStateDragging);
		}
	}
	else if (pState->pHandle)
	{
		S32 iX, iY;
		BuildCBox(&box, fLeft, fTop, fRight - fLeft, fBottom - fTop);
		if (mouseDownHit(MS_LEFT, &box))
		{
			mouseDownPos(MS_LEFT, &iX, &iY);
			if (iY < fHandleTop)
				pState->fScrollPosition -= CBoxHeight(&box) * 0.9;
			else if (iY > fHandleTop + fHandleHeight)
				pState->fScrollPosition += CBoxHeight(&box) * 0.9;
			else
			{
				pState->iDraggingOffset = iY - fHandleTop;
				ui_GenSetState(pGen, kUIGenStateDragging);
			}
			if(pBar->bScrollbarCaptureMouse)
				inpHandled();
		}
	}

	if (ui_GenInState(pGen, kUIGenStateScrollingUp))
	{
		pState->fScrollPosition -= g_ui_State.timestep * fHeight;
		if(pBar->bScrollbarCaptureMouse)
			inpHandled();
	}
	else if (ui_GenInState(pGen, kUIGenStateScrollingDown))
	{
		pState->fScrollPosition += g_ui_State.timestep * fHeight;
		if(pBar->bScrollbarCaptureMouse)
			inpHandled();
	}
	else if (ui_GenInState(pGen, kUIGenStateDragging))
	{
		fHandleTop = g_ui_State.mouseY - pState->iDraggingOffset;
		if (fUsableSpace > 0)
			pState->fScrollPosition = ((fHandleTop - fTop) / fUsableSpace) * fOffSpace;
		if(pBar->bScrollbarCaptureMouse)
			inpHandled();
		ui_SoftwareCursorThisFrame();
	}

	if (pBar->bScrollbarUseWheel)
		ScrollbarHandleWheel(pGen, pBar, pState);
	if (pBar->bScrollbarUseStick)
		ScrollbarHandleStick(pGen, pBar, pState);

	pState->fScrollPosition = floorf(CLAMP(pState->fScrollPosition, 0, fOffSpace));
}

void ui_GenDrawScrollbar(UIGen *pGen, UIGenScrollbar *pBar, UIGenScrollbarState *pState)
{
	UIGenInternal *pResult = pGen->pResult;
	UIGenScrollbarDef *pScrollbarDef = UI_GEN_CHOOSE_SCROLLBAR(pBar, pState);
	UITextureAssembly *pTop = pState->pTop;
	UITextureAssembly *pBottom = pState->pBottom;
	UITextureAssembly *pHandle = pState->pHandle;
	UITextureAssembly *pTrough = pScrollbarDef ? GET_REF(pScrollbarDef->hTrough) : NULL;
	F32 fScale = pGen->fScale;
	F32 fLeft = (SAFE_MEMBER(pScrollbarDef, eOffsetFrom) == UIRight) 
		? pGen->ScreenBox.hx - (pBar->iScrollbarRightMargin * fScale) - (pScrollbarDef->iWidth * fScale) 
		: pGen->ScreenBox.lx + (pBar->iScrollbarLeftMargin * fScale);
	F32 fTop = pGen->ScreenBox.ly + pBar->iScrollbarTopMargin * pGen->fScale;
	F32 fBottom = pGen->ScreenBox.hy - pBar->iScrollbarBottomMargin * pGen->fScale;
	F32 fHeight = CBoxHeight(&pGen->ScreenBox);
	F32 fHandleHeight;
	F32 fHandleTop;
	F32 fZ = UI_GET_Z();
	F32 fMinHandle = SAFE_MEMBER(pScrollbarDef, iMinHandleHeight) * fScale;
	CBox box;
	SpriteProperties *pSpriteProperties = GenSpritePropGetCurrent();
	F32 fScreenDist = pSpriteProperties ? pSpriteProperties->screen_distance : 0.0;
	bool bIs3D = pSpriteProperties ? pSpriteProperties->is_3D : false;

	if (!pScrollbarDef || pState->bUnneeded)
		return;

	if (pTrough)
	{
		F32 fTroughTop = fTop;
		F32 fTroughBottom = fBottom;
		if (pTop)
			fTroughTop += pScrollbarDef->iTopHeight* fScale;
		if (pBottom)
			fTroughBottom -= pScrollbarDef->iBottomHeight * fScale;
		BuildCBox(&box, fLeft, fTroughTop, pScrollbarDef->iWidth * fScale, fTroughBottom - fTroughTop);
		ui_TextureAssemblyDrawEx(pTrough, &box, NULL, fScale, fZ, fZ + .1, pGen->chAlpha, NULL, fScreenDist, bIs3D);
	}

	if (pTop)
	{
		BuildCBox(&box, fLeft, fTop, pScrollbarDef->iWidth * fScale, pScrollbarDef->iTopHeight * fScale);
		ui_TextureAssemblyDraw(pTop, &box, NULL, fScale, fZ, fZ + .1, pGen->chAlpha, NULL);
		fTop += pScrollbarDef->iTopHeight* fScale;
	}

	if (pBottom)
	{
		BuildCBox(&box, fLeft, fBottom - pScrollbarDef->iBottomHeight * fScale, pScrollbarDef->iWidth * fScale, pScrollbarDef->iBottomHeight * fScale);
		ui_TextureAssemblyDrawEx(pBottom, &box, NULL, fScale, fZ, fZ + .1, pGen->chAlpha, NULL, fScreenDist, bIs3D);
		fBottom -= pScrollbarDef->iBottomHeight * fScale;
	}

	GenGetScrollbarParams(pBar, pState, fTop, fBottom, fHeight, fMinHandle, NULL, &fHandleHeight, &fHandleTop, NULL);

	if (!pState->bHideHandle && pHandle)
	{
		BuildCBox(&box, fLeft, fHandleTop, pScrollbarDef->iWidth * fScale, fHandleHeight);
		ui_TextureAssemblyDrawEx(pHandle, &box, NULL, fScale, fZ, fZ + .1, pGen->chAlpha, NULL, fScreenDist, bIs3D);
	}
}

void ui_GenHideScrollbar(UIGen *pGen, UIGenScrollbar *pBar, UIGenScrollbarState *pState)
{
}

static DictionaryHandle s_UIGenScrollbarDict;

AUTO_STARTUP(UIGenScrollbar) ASTRT_DEPS(UITextureAssembly UISize);
void ui_GenScrollbarLoad(void)
{
	if(!gbNoGraphics)
	{
		s_UIGenScrollbarDict = RefSystem_RegisterSelfDefiningDictionary("UIGenScrollbarDef", false, parse_UIGenScrollbarDef, true, true, NULL);
		resLoadResourcesFromDisk(s_UIGenScrollbarDict, "ui/scrollbars/", ".scrollbar", NULL, RESOURCELOAD_USEOVERLAYS);
	}
}

#include "UIGenScrollbar_h_ast.c"
