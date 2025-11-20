#include "error.h"
#include "earray.h"

#include "inputMouse.h"
#include "inputLib.h"

#include "UICore.h"
#include "UITextureAssembly.h"
#include "UIGenMovableBox.h"
#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenPrivate.h"
#include "RdrDevice.h"
#include "GfxSpriteText.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

bool ui_GenValidateMovableBox(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor)
{
	UIGenMovableBox *pBase = (UIGenMovableBox*)pGen->pBase;
	UIGenMovableBox *pMovableBox = (UIGenMovableBox*)pInt;

	UI_GEN_WARN_IF(pGen, pInt->pos.iLeftMargin, "%s: MovableBoxes and LeftMargin do not work together, use X instead", pchDescriptor);
	UI_GEN_WARN_IF(pGen, pInt->pos.iRightMargin, "%s: MovableBoxes and RightMargin do not work together, use X instead", pchDescriptor);
	UI_GEN_WARN_IF(pGen, pInt->pos.iTopMargin, "%s: MovableBoxes and TopMargin do not work together, use Y instead", pchDescriptor);
	UI_GEN_WARN_IF(pGen, pInt->pos.iBottomMargin, "%s: MovableBoxes and BottomMargin do not work together, use Y instead", pchDescriptor);

	//UI_GEN_WARN_IF(pGen, pMovableBox->eResizableHorizontal && (!pInt->pos.MinimumWidth.fMagnitude || !pInt->pos.MinimumWidth.eUnit), "%s: MovableBoxes must have MinimumWidth if flagged with ResizableHorizontal", pchDescriptor);
	//UI_GEN_WARN_IF(pGen, pMovableBox->eResizableHorizontal && (!pInt->pos.MinimumHeight.fMagnitude || !pInt->pos.MinimumHeight.eUnit), "%s: MovableBoxes must have MinimumHeight if flagged with ResizableVertical", pchDescriptor);

	// If no base, then most likely a borrowfrom
	//if (pBase)
	//{
	//	UI_GEN_WARN_IF(pGen, !pBase->iVersion && (pMovableBox->eResizableHorizontal || pMovableBox->eResizableVertical), "%s: MovableBoxes must have a Version number if they are resizable", pchDescriptor);
	//	UI_GEN_WARN_IF(pGen, !pBase->iVersion && (pMovableBox->eMovable), "%s: MovableBoxes must have a Version number if they are movable", pchDescriptor);
	//}

	UI_GEN_WARN_IF(pGen, pMovableBox->iVersion && (stricmp(pchDescriptor, "Base") != 0 && stricmp(pchDescriptor, "Last") != 0), "%s: MovableBoxes Version number should be in the Base", pchDescriptor);

	return true;
}

void ui_GenMovableBoxRaise(UIGen *pGen)
{
	UIGenMovableBoxState *pState = UI_GEN_STATE(pGen, MovableBox);
	S32 iMaxPriority = 0;
	// If you're caught dragging a movable box when its jail disappears, bad things happen.
	// So, don't let people do that.
	if (pGen->pParent && pGen->pParent->pResult)
		ui_GenRaiseTopLevel(pGen);
}

static void ui_GenMovableBoxCheckInteracting(UIGen *pGen, UIGenMovableBox *pMovableBox, UIGenMovableBoxState *pState)
{
	bool bInteracting = (pState->bMoving || pState->eResizing);

	if (pState->bMoving && !mouseIsDown(pMovableBox->eMovable))
		pState->bMoving = false;
	if (pState->eResizing & (UILeft | UIRight) && !mouseIsDown(pMovableBox->eResizableHorizontal))
		pState->eResizing &= ~(UILeft | UIRight);
	if (pState->eResizing & (UITop | UIBottom) && !mouseIsDown(pMovableBox->eResizableVertical))
		pState->eResizing &= ~(UITop | UIBottom);

	// Stopped moving this frame.
	if (bInteracting && !(pState->bMoving || pState->eResizing) && g_GenState.cbPositionSet)
	{
		g_GenState.cbPositionSet(pGen->pchName, &pMovableBox->polyp.pos, pMovableBox->iVersion, pGen->chClone, pState->chPriority, NULL);
	}
}

void ui_GenUpdateMovableBox(UIGen *pGen)
{
	CBox *pBox = &pGen->UnpaddedScreenBox;
	UIGenMovableBox *pMovableBox = UI_GEN_RESULT(pGen, MovableBox);
	UIGenMovableBoxState *pState = UI_GEN_STATE(pGen, MovableBox);
	if (!pState->pOverride)
		pState->Original = pMovableBox->polyp.pos;

	if (pState->bMoving && pGen->pParent)
	{
		CBox box = *pBox;
		S32 iX, iY;
		mousePos(&iX, &iY);
		CBoxMoveX(&box, iX - pState->iGrabbedX);
		CBoxMoveY(&box, iY - pState->iGrabbedY);
		if (!ui_GenCBoxToPosition(pState->pOverride, &box, &pGen->pParent->ScreenBox, UINoDirection, pGen->fScale))
			ErrorFilenamef(pGen->pchFilename, "%s: Resizable windows must be sized using Fixed or Percentage units.", pGen->pchName);
	}
	else if (pState->eResizing && pGen->pParent)
	{
		CBox box = *pBox;
		S32 iX, iY;
		mousePos(&iX, &iY);
		if (pState->eResizing & UILeft)
			box.lx = max(iX - pState->iGrabbedX, pGen->pParent->ScreenBox.lx);
		else if (pState->eResizing & UIRight)
			box.hx = min(iX + pState->iGrabbedX, pGen->pParent->ScreenBox.hx);
		if (pState->eResizing & UITop)
			box.ly = max(iY - pState->iGrabbedY, pGen->pParent->ScreenBox.ly);
		else if (pState->eResizing & UIBottom)
			box.hy = min(iY + pState->iGrabbedY, pGen->pParent->ScreenBox.hy);
		if (!ui_GenCBoxToPosition(pState->pOverride, &box, &pGen->pParent->ScreenBox, pState->eResizing, pGen->fScale))
			ErrorFilenamef(pGen->pchFilename, "%s: Resizable windows must be sized using Fixed or Percentage units.", pGen->pchName);
	}
}

void ui_GenLayoutEarlyMovableBox(UIGen *pGen)
{
	UIGenMovableBox *pMovableBox = UI_GEN_RESULT(pGen, MovableBox);
	UIGenMovableBoxState *pState = UI_GEN_STATE(pGen, MovableBox);
	CBox *pBox = &pGen->UnpaddedScreenBox;
	F32 fScale = pGen->fScale;
	bool bInteracting = (pState->bMoving || pState->eResizing);

	if (!pState->pOverride)
	{
		pState->pOverride = StructClone(parse_UIPosition, &pMovableBox->polyp.pos);
		if (g_GenState.cbPositionGet)
			g_GenState.cbPositionGet(pGen->pchName, pState->pOverride, pMovableBox->iVersion, pGen->chClone, &pState->chPriority, NULL);
	}
	if (pMovableBox->eMovable >= 0)
	{
		pMovableBox->polyp.pos.eOffsetFrom = pState->pOverride->eOffsetFrom;
		pMovableBox->polyp.pos.iX = pState->pOverride->iX;
		pMovableBox->polyp.pos.iY = pState->pOverride->iY;
		pMovableBox->polyp.pos.fPercentX = pState->pOverride->fPercentX;
		pMovableBox->polyp.pos.fPercentY = pState->pOverride->fPercentY;
	}
	if (pMovableBox->eResizableHorizontal >= 0)
		pMovableBox->polyp.pos.Width = pState->pOverride->Width;
	if (pMovableBox->eResizableVertical >= 0)
		pMovableBox->polyp.pos.Height = pState->pOverride->Height;
	pGen->chPriority = pMovableBox->polyp.chPriority = pState->chPriority;

	if (bInteracting)
		ui_GenMovableBoxCheckInteracting(pGen, pMovableBox, pState);
}

void ui_GenTickEarlyMovableBox(UIGen *pGen)
{
	UIGenMovableBox *pMovableBox = UI_GEN_RESULT(pGen, MovableBox);
	UIGenMovableBoxState *pState = UI_GEN_STATE(pGen, MovableBox);
	CBox *pBox = &pGen->UnpaddedScreenBox;
	F32 fScale = pGen->fScale;
	bool bInteracting = (pState->bMoving || !!pState->eResizing);
	CBox *pMoveBox = &pGen->UnpaddedScreenBox;
	CBox TitleBox;

	if ((pMovableBox->eResizableHorizontal >= 0 || pMovableBox->eResizableVertical >= 0)
		&& !pState->eResizing)
	{
		UITextureAssembly *pAssembly = ui_GenTextureAssemblyGetAssembly(pGen, pMovableBox->polyp.pAssembly);
		F32 fLeftWidth = (pMovableBox->fResizeBorder ? pMovableBox->fResizeBorder : (pAssembly ? pAssembly->iPaddingLeft : 4)) * fScale;
		F32 fRightWidth = (pMovableBox->fResizeBorder ? pMovableBox->fResizeBorder : (pAssembly ? pAssembly->iPaddingRight : 4)) * fScale;
		F32 fBottomHeight = (pMovableBox->fResizeBorder ? pMovableBox->fResizeBorder : (pAssembly ? pAssembly->iPaddingBottom : 4)) * fScale;
		F32 fTopHeight = (pMovableBox->fResizeBorder ? pMovableBox->fResizeBorder : (pAssembly ? pAssembly->iPaddingTop : 4)) * fScale;
		CBox check;
		UIDirection eCursor = UINoDirection;

		int mx;
		int my;
		mousePos(&mx, &my);

		if (!pState->eResizing)
		{
			BuildCBox(&check, pBox->lx, pBox->hy - fBottomHeight, fLeftWidth, fBottomHeight);
			if (mouseDownHit(pMovableBox->eResizableHorizontal, &check) || mouseDownHit(pMovableBox->eResizableVertical, &check))
				pState->eResizing = UIBottomLeft;
		}

		if (!pState->eResizing)
		{
			BuildCBox(&check, pBox->hx - fRightWidth, pBox->hy - fBottomHeight, fRightWidth, fBottomHeight);
			if (mouseDownHit(pMovableBox->eResizableHorizontal, &check) || mouseDownHit(pMovableBox->eResizableVertical, &check))
				pState->eResizing = UIBottomRight;
		}

		if (!pState->eResizing)
		{
			BuildCBox(&check, pBox->lx, pBox->ly, fLeftWidth, fTopHeight);
			if (mouseDownHit(pMovableBox->eResizableHorizontal, &check) || mouseDownHit(pMovableBox->eResizableVertical, &check))
				pState->eResizing = UITopLeft;
		}

		if (!pState->eResizing)
		{
			BuildCBox(&check, pBox->hx - fRightWidth, pBox->ly, fRightWidth, fTopHeight);
			if (mouseDownHit(pMovableBox->eResizableHorizontal, &check) || mouseDownHit(pMovableBox->eResizableVertical, &check))
				pState->eResizing = UITopRight;
		}

		if (!pState->eResizing)
		{
			BuildCBox(&check, pBox->lx, pBox->ly, fLeftWidth, CBoxHeight(pBox));
			if (mouseDownHit(pMovableBox->eResizableHorizontal, &check))
				pState->eResizing = UILeft;
		}

		if (!pState->eResizing)
		{
			BuildCBox(&check, pBox->hx - fRightWidth, pBox->ly, fRightWidth, CBoxHeight(pBox));
			if (mouseDownHit(pMovableBox->eResizableHorizontal, &check))
				pState->eResizing = UIRight;
		}

		if (!pState->eResizing)
		{
			BuildCBox(&check, pBox->lx, pBox->ly, CBoxWidth(pBox), fTopHeight);
			if (mouseDownHit(pMovableBox->eResizableVertical, &check))
				pState->eResizing = UITop;
		}

		if (!pState->eResizing)
		{
			BuildCBox(&check, pBox->lx, pBox->hy - fBottomHeight, CBoxWidth(pBox), fBottomHeight);
			if (mouseDownHit(pMovableBox->eResizableVertical, &check))
				pState->eResizing = UIBottom;
		}

		// Check to see if the cursor needs to be changed for moving
		//BuildCBox(&check, pBox->lx, pBox->ly, CBoxWidth(pBox), CBoxHeight(pBox));
		//if (point_cbox_clsn(mx, my, &check))
		//	eCursor = UIAnyDirection;

		// Check to see if the cursor needs to be changed for resizing
		BuildCBox(&check, pBox->lx, pBox->ly, fLeftWidth, CBoxHeight(pBox));
		if (point_cbox_clsn(mx, my, &check))
			eCursor = UILeft;
		BuildCBox(&check, pBox->hx - fRightWidth, pBox->ly, fRightWidth, CBoxHeight(pBox));
		if (point_cbox_clsn(mx, my, &check))
			eCursor = UIRight;
		BuildCBox(&check, pBox->lx, pBox->ly, CBoxWidth(pBox), fTopHeight);
		if (point_cbox_clsn(mx, my, &check))
			eCursor = UITop;
		BuildCBox(&check, pBox->lx, pBox->hy - fBottomHeight, CBoxWidth(pBox), fBottomHeight);
		if (point_cbox_clsn(mx, my, &check))
			eCursor = UIBottom;

		BuildCBox(&check, pBox->lx, pBox->hy - fBottomHeight, fLeftWidth, fBottomHeight);
		if (point_cbox_clsn(mx, my, &check))
			eCursor = UIBottomLeft;
		BuildCBox(&check, pBox->hx - fRightWidth, pBox->hy - fBottomHeight, fRightWidth, fBottomHeight);
		if (point_cbox_clsn(mx, my, &check))
			eCursor = UIBottomRight;
		BuildCBox(&check, pBox->lx, pBox->ly, fLeftWidth, fTopHeight);
		if (point_cbox_clsn(mx, my, &check))
			eCursor = UITopLeft;
		BuildCBox(&check, pBox->hx - fRightWidth, pBox->ly, fRightWidth, fTopHeight);
		if (point_cbox_clsn(mx, my, &check))
			eCursor = UITopRight;

		if (pMovableBox->eResizableHorizontal < 0)
			eCursor &= ~(UILeft | UIRight);
		if (pMovableBox->eResizableVertical < 0)
			eCursor &= ~(UITop | UIBottom);
		if (eCursor && !inpCheckHandled())
			ui_SetCursorForDirection(eCursor);

		if (pState->eResizing)
		{
			S32 iX, iY;
			mousePos(&iX, &iY);
			pState->iGrabbedX = (pState->eResizing & UILeft) ? iX - pGen->UnpaddedScreenBox.lx : pGen->UnpaddedScreenBox.hx - iX;
			pState->iGrabbedY = (pState->eResizing & UITop) ? iY - pGen->UnpaddedScreenBox.ly : pGen->UnpaddedScreenBox.hy - iY;
			if (pMovableBox->eResizableHorizontal < 0)
				pState->eResizing &= ~(UILeft | UIRight);
			if (pMovableBox->eResizableVertical < 0)
				pState->eResizing &= ~(UITop | UIBottom);
		}
	}

	if (pMovableBox->pMovableWidth || pMovableBox->pMovableHeight)
	{
		TitleBox = pGen->UnpaddedScreenBox;
		pMoveBox = &TitleBox;
		if (pMovableBox->pMovableWidth)
		{
			F32 fWidth = (pMovableBox->pMovableWidth->eUnit == UIUnitPercentage
				? pMovableBox->pMovableWidth->fMagnitude * CBoxWidth(&pGen->UnpaddedScreenBox)
				: pMovableBox->pMovableWidth->fMagnitude);
			CBoxSetWidth(&TitleBox, fWidth);
		}
		if (pMovableBox->pMovableHeight)
		{
			F32 fHeight = (pMovableBox->pMovableHeight->eUnit == UIUnitPercentage
				? pMovableBox->pMovableHeight->fMagnitude * CBoxHeight(&pGen->UnpaddedScreenBox)
				: pMovableBox->pMovableHeight->fMagnitude);
			CBoxSetHeight(&TitleBox, fHeight);
		}
	}

	if (!pState->eResizing
		&& pMovableBox->eMovable >= 0
		&& mouseDownHit(pMovableBox->eMovable, pMoveBox))
	{
		mouseDownPos(pMovableBox->eMovable, &pState->iGrabbedX, &pState->iGrabbedY);
		pState->bMoving = true;
		pState->iGrabbedX = pState->iGrabbedX - pGen->UnpaddedScreenBox.lx;
		pState->iGrabbedY = pState->iGrabbedY - pGen->UnpaddedScreenBox.ly;
	}

	if ((pState->eResizing || pState->bMoving || mouseDownHit(MS_LEFT, pMoveBox)) && pMovableBox->bRaise)
		ui_GenMovableBoxRaise(pGen);

	if (!bInteracting)
		ui_GenMovableBoxCheckInteracting(pGen, pMovableBox, pState);
}

void ui_GenHideMovableBox(UIGen *pGen)
{
	UIGenMovableBoxState *pState = UI_GEN_STATE(pGen, MovableBox);
	StructDestroySafe(parse_UIPosition, &pState->pOverride);
	pState->bMoving = false;
	pState->chPriority = 0;
	pState->eResizing = 0;
}

void ui_GenFitContentsSizeMovableBox(UIGen *pGen, UIGenMovableBox *pBox, CBox *pOut)
{
	// The native size of a box is the furthest point a fixed child expects to draw at.
	Vec2 v2Size = {0, 0};
	ui_GenInternalForEachChild(&pBox->polyp, ui_GenGetBounds, v2Size, UI_GEN_LAYOUT_ORDER);
	BuildCBox(pOut, 0, 0, v2Size[0], v2Size[1]);
}

void ui_GenPositionRegisterCallbacks(UIGenGetPosition cbGet, UIGenSetPosition cbSet, UIGenSetPosition cbForget)
{
	g_GenState.cbPositionGet = cbGet;
	g_GenState.cbPositionSet = cbSet;
	g_GenState.cbPositionForget = cbForget;
}

// Save a movable box's position. Use this sparingly, probably only in the OnEnter of StateDef Visible.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMovableBoxSavePosition);
void ui_GenExprMovableBoxSavePosition(SA_PARAM_OP_VALID UIGen *pGen)
{
	if (pGen && UI_GEN_IS_TYPE(pGen, kUIGenTypeMovableBox))
	{
		UIGenMovableBox *pMovableBox = UI_GEN_RESULT(pGen, MovableBox);
		UIGenMovableBoxState *pState = UI_GEN_STATE(pGen, MovableBox);
		if (g_GenState.cbPositionSet)
			g_GenState.cbPositionSet(pGen->pchName, &pMovableBox->polyp.pos, pMovableBox->iVersion, pGen->chClone, pState->chPriority, NULL);
	}
}

// Reset a movable box to its default position.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMovableBoxResetPosition);
void ui_GenExprMovableBoxResetPosition(SA_PARAM_OP_VALID UIGen *pGen)
{
	if (pGen && UI_GEN_IS_TYPE(pGen, kUIGenTypeMovableBox))
	{
		UIGenMovableBox *pMovableBox = UI_GEN_RESULT(pGen, MovableBox);
		UIGenMovableBoxState *pState = UI_GEN_STATE(pGen, MovableBox);
		if (g_GenState.cbPositionForget)
			g_GenState.cbPositionForget(pGen->pchName, pState ? pState->pOverride : NULL, pMovableBox ? pMovableBox->iVersion : 0, pGen->chClone, pState ? pState->chPriority : 0, NULL);
		if (pMovableBox)
			pMovableBox->polyp.pos = pState->Original;
		if (pState && pState->pOverride)
		{
			StructDestroySafe(parse_UIPosition, &pState->pOverride);
			pState->pOverride = StructClone(parse_UIPosition, &pState->Original);
		}
	}
}

// Reset a movable box to its default position.
AUTO_COMMAND ACMD_NAME(GenMovableBoxResetPosition) ACMD_ACCESSLEVEL(0);
void ui_GenCmdMovableBoxResetPosition(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	ui_GenExprMovableBoxResetPosition(ui_GenFind(pchGen, kUIGenTypeNone));
}

// Reset a movable box to its default position.
AUTO_COMMAND ACMD_NAME(GenMovableBoxResetAllPositions) ACMD_ACCESSLEVEL(0);
void ui_GenCmdMovableBoxResetAllPositions(void)
{
	RefDictIterator iter;
	UIGen *pGen;
	RefSystem_InitRefDictIterator("UIGen", &iter);
	while ((pGen = RefSystem_GetNextReferentFromIterator(&iter)))
		ui_GenExprMovableBoxResetPosition(pGen);
}

AUTO_RUN;
void ui_GenRegisterMovableBox(void)
{
	ui_GenRegisterType(kUIGenTypeMovableBox, 
		ui_GenValidateMovableBox, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateMovableBox, 
		ui_GenLayoutEarlyMovableBox, 
		UI_GEN_NO_LAYOUTLATE, 
		ui_GenTickEarlyMovableBox, 
		UI_GEN_NO_TICKLATE, 
		UI_GEN_NO_DRAWEARLY,
		ui_GenFitContentsSizeMovableBox, 
		UI_GEN_NO_FITPARENTSIZE,
		ui_GenHideMovableBox, 
		UI_GEN_NO_INPUT, 
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}

#include "UIGenMovableBox_h_ast.c"

