#include "error.h"
#include "StringCache.h"
#include "earray.h"

#include "GfxTexAtlas.h"

#include "inputMouse.h"
#include "input.h"

#include "UIInternal.h"
#include "UIGen.h"
#include "UIGenDnD.h"
#include "UIGenDnD_h_ast.h"
#include "UIGen_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static UIGenDragDrop s_ActiveDrag;
static UIGenDragDropTarget **s_eaTargets;

void ui_GenDragDropStart(UIGen *pSource, const char *pchType, const char *pchPayload, S32 iIntPayload, F32 fFloatPayload,
						 void *pPointerPayload, ParseTable *pPointerType, const char *pchCursor, MouseButton eButton, int hotX, int hotY)
{
	if (s_ActiveDrag.pSource)
	{
		ErrorFilenameTwof(pSource->pchFilename, s_ActiveDrag.pSource->pchFilename,
			"Trying to start a drag with gen %s when gen %s is already active",
			pSource->pchName, s_ActiveDrag.pSource->pchName);
		return;
	}
	else
	{
		UIDeviceState *pState = ui_StateForDevice(NULL);
		s_ActiveDrag.pSource = pSource;
		s_ActiveDrag.pchType = allocAddString(pchType);
		StructFreeString(s_ActiveDrag.pchTextPayload);
		s_ActiveDrag.pchTextPayload = StructAllocString(pchPayload);
		s_ActiveDrag.iIntPayload = iIntPayload;
		s_ActiveDrag.fFloatPayload = fFloatPayload;
		s_ActiveDrag.pPointerPayload = pPointerPayload;
		s_ActiveDrag.pPointerType = pPointerType;

		s_ActiveDrag.pchCursor = allocAddString(pchCursor);
		s_ActiveDrag.eButton = eButton;
		pState->cursor.draggedIcon = s_ActiveDrag.pchCursor ? atlasLoadTexture(s_ActiveDrag.pchCursor) : NULL;
		pState->cursor.hotX = hotX;
		pState->cursor.hotY = hotY;
		ui_GenSetPointerVar("DragDropData", &s_ActiveDrag, parse_UIGenDragDrop);
	}
}

UIGenDragDropAction *ui_GenDragDropWillAccept(UIGen *pTarget)
{
	S32 i;
	if (!UI_GEN_READY(pTarget))
		return NULL;
	for (i = eaSize(&pTarget->pResult->eaDragDrop) - 1; i >= 0; i--)
		if (pTarget->pResult->eaDragDrop[i]->pchTypeMatch == s_ActiveDrag.pchType)
			return pTarget->pResult->eaDragDrop[i];
	return NULL;
}

bool ui_GenDragWasDropped(CBox *pBox)
{
	return 
		UI_GEN_READY(s_ActiveDrag.pSource) 
		&& !filteredMouseIsDown(s_ActiveDrag.eButton) 
		&& !inpCheckHandled() 
		&& mouseCollision(pBox) 
		&& mouseClipperCollision();
}

bool ui_GenDragDropAccept(UIGen *pGen)
{
	UIGenDragDropAction *pDrop = ui_GenDragDropWillAccept(pGen);
	if (pDrop)
	{
		UIDeviceState *pState = ui_StateForDevice(NULL);
		UIGenDragDropTarget *pTarget = StructCreate(parse_UIGenDragDropTarget);
		pState->cursor.draggedIcon = NULL;
		pTarget->pGen = pGen;
		eaPush(&s_eaTargets, pTarget);
		StructCopyAll(parse_UIGenAction, &pDrop->OnDropped, &pTarget->QueuedAction);
		return true;
	}
	return false;
}

void ui_GenDragDropOncePerFrame(void)
{
	if (s_ActiveDrag.pSource && (!UI_GEN_READY(s_ActiveDrag.pSource) || !mouseIsDown(s_ActiveDrag.eButton)))
		ui_GenDragDropCancel();
}

void ui_GenDragDropCancel(void)
{
	if (s_ActiveDrag.pSource)
	{
		UIDeviceState *pState = ui_StateForDevice(NULL);
		if (UI_GEN_READY(s_ActiveDrag.pSource) && s_ActiveDrag.pSource->pResult->pDragCancelled)
		{
			ui_GenRunAction(s_ActiveDrag.pSource, s_ActiveDrag.pSource->pResult->pDragCancelled);
			ui_GenSetPointerVar("DragDropData", NULL, parse_UIGenDragDrop);
		}
		eaDestroyStruct(&s_eaTargets, parse_UIGenDragDropTarget);
		StructDeInit(parse_UIGenDragDrop, &s_ActiveDrag);
		ZeroStruct(&s_ActiveDrag);
		pState->cursor.draggedIcon = NULL;
	}
}

UIGen *ui_GenDragDropGetSource(void)
{
	return s_ActiveDrag.pSource;
}

const char *ui_GenDragDropGetType(void)
{
	return s_ActiveDrag.pchType ? s_ActiveDrag.pchType : "";
}

bool ui_GenIsDragging(void)
{
	return !!s_ActiveDrag.pSource;
}

void ui_GenDragDropUpdate(UIGen *pGen)
{
	int i;
	UIGenDragDropTarget *pTarget = NULL;
	for (i = 0; i < eaSize(&s_eaTargets); i++)
	{
		if (s_eaTargets[i]->pGen == pGen)
		{
			pTarget = s_eaTargets[i];
			eaRemoveFast(&s_eaTargets, i);
			break;
		}
	}
	if (pTarget)
	{
		ui_GenRunAction(pGen, &pTarget->QueuedAction);
		StructDestroy(parse_UIGenDragDropTarget, pTarget);
		if (eaSize(&s_eaTargets) == 0)
		{
			StructDeInit(parse_UIGenDragDrop, &s_ActiveDrag);
			ZeroStruct(&s_ActiveDrag);
			ui_GenSetPointerVar("DragDropData", NULL, parse_UIGenDragDrop);
		}
	}
}

const char *ui_GenDragDropGetStringPayload(void)
{
	return s_ActiveDrag.pchTextPayload;
}


#include "UIGenDnD_h_ast.c"
