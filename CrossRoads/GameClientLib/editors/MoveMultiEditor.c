#ifndef NO_EDITORS

#include "MoveMultiEditor.h"
#include "dynMove.h"
#include "dynAnimTrack.h"
#include "dynAnimChart.h"
#include "AnimEditorCommon.h"
#include "gclCostumeView.h"
#include "dynSkeleton.h"
#include "dynFxManager.h"
#include "dynFxInfo.h"

#include "MultiEditFieldContext.h"
#include "GameClientLib.h"

#include "error.h"
#include "EditLibUIUtil.h"
#include "winutil.h"
#include "Color.h"
#include "StringCache.h"
#include "UIGimmeButton.h"
#include "EditorPrefs.h"
#include "UISeparator.h"
#include "GfxClipper.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "inputMouse.h"
#include "qsortG.h"
#include "file.h"
#include "string.h"

#include "soundLib.h"
#include "gclUtils.h"
#include "WorldLib.h"

#include "dynNode.h"
#include "dynNodeInline.h"

#include "MoveMultiEditor_h_ast.c"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static int mmeNewNameCount = 0;

static bool mmeDrawGrid = true;
static bool mmeUseBaseSkeleton = false;

static UIButton			*mmeSortOnceButton;
static UICheckButton	*mmeAutoSortButton;

static UICheckButton *mmeTrackButton;
static UICheckButton *mmeBoneCheckButton;
static UICheckButton *mmePhysicsCheckButton;
static UICheckButton *mmeFxCheckButton;
static UICheckButton *mmeIkCheckButton;
static UICheckButton *mmeRagdollCheckButton;
static UICheckButton *mmeMatchBaseSkelAnimCheckButton;

static UICheckButton *mmeDrawGridCheckButton;
static UICheckButton *mmeUseBaseSkeletonCheckButton;

static UIButton *mmePickCostumeButton;
static UIButton *mmeFitToScreenButton;
static UIButton *mmeAddFxButton;
static UIButton *mmeClearTestFxButton;

static bool mmeUINeedsRefresh = false;

static bool mmeWarnedNoSkeleton = false;


//-----------------------------------------------------------------------------------
// Headers for out-of-order calls
//-----------------------------------------------------------------------------------

static bool MMEDocPassesFilters(MoveDoc *pDoc, MoveMultiDoc *pEMDoc);
static void MMEInitDisplay(EMEditor *pEditor, MoveDoc *pDoc);
static MoveDoc* MMEInitDoc(DynMove *pMove, MoveMultiDoc *pParent, bool bCreated);
static void MMEBindToParent(MoveDoc *pDoc, MoveMultiDoc *pParent);
static void MMERefreshUI(MoveDoc *pDoc, bool bUndoable);
static void MMEPostDeleteDoc(MoveDoc *pDoc);
static void MMEPostSaveDoc(MoveDoc *pDoc);
static EMTaskStatus MMEDeleteMove(MoveDoc *pDoc);
static EMTaskStatus MMESaveMove(MoveDoc *pDoc, bool bSaveAsNew);
static AnimEditor_CostumePickerData* MMEGetCostumePickerData(void);


//---------------------------------------------------------------------------------------------------
// Grid
//---------------------------------------------------------------------------------------------------

static void MMEDrawGridCheckboxToggle(UICheckButton *pButton, UserData pData)
{
	mmeDrawGrid = mmeDrawGridCheckButton->state;
	mmeUINeedsRefresh = true;
}

static void MMEDrawGrid(void)
{
	Vec3 vGridPt1, vGridPt2;
	S32 uiGridLine;
	U32 uiGridColor;

	for (uiGridLine = -30; uiGridLine <= 30; uiGridLine++)
	{
		uiGridColor = uiGridLine % 10 == 0 ?
			uiGridLine == 0 ? 0x88FFFFFF : 0x44FFFFFF :
			0x22FFFFFF;
		vGridPt1[0] = uiGridLine; vGridPt1[1] = 0.f; vGridPt1[2] = -30.f;
		vGridPt2[0] = uiGridLine; vGridPt2[1] = 0.f; vGridPt2[2] =  30.f;

		wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);
	}

	for (uiGridLine = -30; uiGridLine <= 30; uiGridLine++)
	{
		uiGridColor = uiGridLine % 10 == 0 ?
			uiGridLine == 0 ? 0x88FFFFFF : 0x44FFFFFF :
			0x22FFFFFF;
		vGridPt1[0] = -30.f; vGridPt1[1] = 0.f; vGridPt1[2] = uiGridLine;
		vGridPt2[0] =  30.f; vGridPt2[1] = 0.f; vGridPt2[2] = uiGridLine;

		wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);
	}

	uiGridColor = 0x44FFFFFF;
	vGridPt1[1] = 0.f;
	vGridPt2[1] = 0.f;

	vGridPt1[0] = -0.05f; vGridPt1[2] = -30.f;
	vGridPt2[0] = -0.05f; vGridPt2[2] =  30.f;
	wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);

	vGridPt1[0] = 0.05f; vGridPt1[2] = -30.f;
	vGridPt2[0] = 0.05f; vGridPt2[2] =  30.f;
	wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);

	vGridPt1[0] = -30.f; vGridPt1[2] = -0.05f;
	vGridPt2[0] =  30.f; vGridPt2[2] = -0.05f;
	wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);

	vGridPt1[0] = -30.f; vGridPt1[2] = 0.05f;
	vGridPt2[0] =  30.f; vGridPt2[2] = 0.05f;
	wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);
}


//---------------------------------------------------------------------------------------------------
// Use Base Skeleton
//---------------------------------------------------------------------------------------------------

static void MMEUseBaseSkeletonCheckboxToggle(UICheckButton *pButton, UserData pData)
{
	mmeUseBaseSkeleton = mmeUseBaseSkeletonCheckButton->state;
	mmeUINeedsRefresh = true;
}

//---------------------------------------------------------------------------------------------------
// Searching
//---------------------------------------------------------------------------------------------------

static void MMERefreshSearchPanel(UIButton *pButton, UserData uData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();

	if (pDoc->pSearchPanel)
	{
		eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
		emPanelFree(pDoc->pSearchPanel);
	}
	pDoc->pSearchPanel = MMEInitSearchPanel(pDoc);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
	emRefreshDocumentUI();
}

static void MMESearchTextChanged(UITextEntry *pEntry, UserData uData)
{
	if (pEntry) {
		MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
		AnimEditor_SearchText = allocAddString(ui_TextEntryGetText(pEntry));
		if (!strlen(AnimEditor_SearchText))
			AnimEditor_SearchText = NULL;
		MMERefreshSearchPanel(NULL, uData);
	}
}

EMPanel* MMEInitSearchPanel(MoveMultiDoc* pDoc)
{
	EMPanel* pPanel;
	F32 y = 0.0;

	// Create the panel
	pPanel = emPanelCreate("Search", "Search", 0);

	y += AnimEditor_Search(	pDoc,
		pPanel,
		AnimEditor_SearchText,
		MMESearchTextChanged,
		MMERefreshSearchPanel
		);

	emPanelSetHeight(pPanel, y);

	emRefreshDocumentUI();

	return pPanel;
}

//-----------------------------------------------------------------------------------
// Misc functions
//-----------------------------------------------------------------------------------

void MMESndPlayerMatCB(Mat4 mat)
{
	identityMat4(mat);
}

void MMESndPlayerVelCB(Vec3 vel)
{
	zeroVec3(vel);
}

static void MMEScrollToDoc(MoveDoc *pDoc)
{
	F32 y = 0.0;

	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pParent->eaMoveDocs, MoveDoc, pSkipDoc)
	{
		if (pSkipDoc == pDoc) {
			break;
		} else if (pSkipDoc->bIsSelected) {
			if (ui_ExpanderIsOpened(pSkipDoc->pExpander))
				y += pSkipDoc->pExpander->openedHeight;
			y += 24;
		}
	}
	FOR_EACH_END;

	UI_WIDGET(pDoc->pParent->pExpanderGroup)->sb->ypos = y;
}

static void MMEFixupNoAstData(DynMove *pMove)
{
	FOR_EACH_IN_EARRAY(pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq)
	{
		pMoveSeq->pDynMove = pMove;
		pMoveSeq->dynMoveAnimTrack.pAnimTrackHeader = dynAnimTrackHeaderFind(pMoveSeq->dynMoveAnimTrack.pcAnimTrackName);
		FOR_EACH_IN_EARRAY(pMoveSeq->dynMoveAnimTrack.eaNoInterpFrameRange, DynMoveFrameRange, pFrameRange)
		{
			pFrameRange->iFirstFrame = pFrameRange->iFirst;
			if (pFrameRange->iFirstFrame > 0)
				pFrameRange->iFirstFrame--;

			pFrameRange->iLastFrame = pFrameRange->iLast;
			if (pFrameRange->iLastFrame > 0)
				pFrameRange->iLastFrame--;
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

static void MMESetVisualizeMove(MoveMultiDoc *pDoc)
{
	DynMove    *pPrevMov = pDoc->pVisualizeMove;
	DynMoveSeq *pPrevSeq = pDoc->pVisualizeMoveSeq;

	pDoc->pVisualizeMove     = NULL;
	pDoc->pVisualizeMoveDoc  = NULL;
	pDoc->pVisualizeMoveOrig = NULL;

	FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pChkDoc)
	{
		if (stricmp(pChkDoc->pObject->pcName, pDoc->pcVisualizeMove) == 0) {
			pDoc->pVisualizeMove     = pChkDoc->pObject;
			pDoc->pVisualizeMoveOrig = pChkDoc->pOrigObject;
			pDoc->pVisualizeMoveDoc  = pChkDoc;
			break;
		}
	}
	FOR_EACH_END;

	if (!pDoc->pVisualizeMove	||
		pDoc->pVisualizeMove != pPrevMov)
	{
		pDoc->pVisualizeMoveSeq = NULL;
		pDoc->pVisualizeMoveSeqOrig = NULL;
	}

	if (pDoc->pVisualizeMove &&
		eaSize(&pDoc->pVisualizeMove->eaDynMoveSeqs) == 1)
	{
		pDoc->pVisualizeMoveSeq  = pDoc->pVisualizeMove->eaDynMoveSeqs[0];
		pDoc->pcVisualizeMoveSeq = pDoc->pVisualizeMove->eaDynMoveSeqs[0]->pcDynMoveSeq;
		if (eaSize(&pDoc->pVisualizeMoveOrig->eaDynMoveSeqs) == 1) {
			pDoc->pVisualizeMoveSeqOrig = pDoc->pVisualizeMoveOrig->eaDynMoveSeqs[0];
		}
	}

	if (!pDoc->pVisualizeMove				||
		pDoc->pVisualizeMove != pPrevMov	||
		pDoc->pVisualizeMoveSeq != pPrevSeq	)
	{
		if (!pDoc->pVisualizeMoveSeq) {
			pDoc->pcVisualizeMoveSeq = NULL;
			pDoc->bVisualizePlaying = false;
		} else if (pDoc->pVisualizeMoveSeq != pPrevSeq) {
			pDoc->fVisualizeFirstFrame = 1;
			pDoc->fVisualizeLastFrame  = pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiLastFrame - pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiFirstFrame + pDoc->fVisualizeFirstFrame + uiFixDynMoveOffByOneError;
			pDoc->fVisualizeFrame = pDoc->fVisualizeFirstFrame;
			pDoc->bVisualizePlaying = false;
		}
	}

	if (pDoc->pVisualizeMove    != pPrevMov ||
		pDoc->pVisualizeMoveSeq != pPrevSeq)
		mmeUINeedsRefresh = true;
}

static void MMESetVisualizeMoveSeq(MoveMultiDoc *pDoc)
{
	DynMoveSeq *pPrevSeq = pDoc->pVisualizeMoveSeq;
	pDoc->pVisualizeMoveSeq     = NULL;
	pDoc->pVisualizeMoveSeqOrig = NULL;

	if (pDoc->pVisualizeMove) {
		FOR_EACH_IN_EARRAY(pDoc->pVisualizeMove->eaDynMoveSeqs, DynMoveSeq, pChkMoveSeq)
		{
			if (stricmp(pChkMoveSeq->pcDynMoveSeq, pDoc->pcVisualizeMoveSeq) == 0) {
				pDoc->pVisualizeMoveSeq = pChkMoveSeq;
				break;
			}
		}
		FOR_EACH_END;

		if (pDoc->pVisualizeMoveSeq != pPrevSeq) {
			pDoc->fVisualizeFirstFrame = 1;
			pDoc->fVisualizeLastFrame  = pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiLastFrame - pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiFirstFrame + pDoc->fVisualizeFirstFrame + uiFixDynMoveOffByOneError;
			pDoc->fVisualizeFrame = pDoc->fVisualizeFirstFrame;
			pDoc->bVisualizePlaying = false;
		}
	}

	if (pDoc->pVisualizeMoveOrig) {
		FOR_EACH_IN_EARRAY(pDoc->pVisualizeMoveOrig->eaDynMoveSeqs, DynMoveSeq, pChkMoveSeq)
		{
			if (stricmp(pChkMoveSeq->pcDynMoveSeq, pDoc->pcVisualizeMoveSeq) == 0) {
				pDoc->pVisualizeMoveSeqOrig = pChkMoveSeq;
				break;
			}
		}
		FOR_EACH_END;
	}

	if (pDoc->pVisualizeMoveSeq != pPrevSeq)
		mmeUINeedsRefresh = true;
}


//-----------------------------------------------------------------------------------
// History CBs
//-----------------------------------------------------------------------------------

static void MMEMUndoCB(MoveMultiDoc *pEMDoc, MMEMUndoData *pData)
{
	MoveDoc *pDoc = pData->pDoc;
	int i = eaFind(&pEMDoc->eaMoveDocs, pDoc);
	if (i >= 0)
	{
		if (pDoc->pObject->pcName != pDoc->pNextUndoObject->pcName) {
			sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pObject->pcName);
			sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pObject->pcName);
			pDoc->emDoc.name_changed = 1;
		}

		StructDestroy(parse_DynMove, pDoc->pObject);
		pDoc->pObject = StructClone(parse_DynMove, pData->pPreObject);
		MMESetVisualizeMove(pDoc->pParent);
		MMESetVisualizeMoveSeq(pDoc->pParent);

		if (pDoc->pNextUndoObject) StructDestroy(parse_DynMove, pDoc->pNextUndoObject);
		pDoc->pNextUndoObject = StructClone(parse_DynMove, pDoc->pObject);

		MMERefreshUI(pDoc, false);
	}
	else {
		EditUndoLast(pEMDoc->emDoc.edit_undo_stack);
	}
}

static void MMEMRedoCB(MoveMultiDoc *pEMDoc, MMEMUndoData *pData)
{
	MoveDoc *pDoc = pData->pDoc;
	int i = eaFind(&pEMDoc->eaMoveDocs, pDoc);
	if (i >= 0)
	{
		StructDestroy(parse_DynMove, pDoc->pObject);
		pDoc->pObject = StructClone(parse_DynMove, pData->pPostObject);
		MMESetVisualizeMove(pDoc->pParent);
		MMESetVisualizeMoveSeq(pDoc->pParent);

		if (pDoc->pNextUndoObject) StructDestroy(parse_DynMove, pDoc->pNextUndoObject);
		pDoc->pNextUndoObject = StructClone(parse_DynMove, pDoc->pObject);

		if (pDoc->pObject->pcName != pDoc->pNextUndoObject->pcName) {
			sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pObject->pcName);
			sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pObject->pcName);
			pDoc->emDoc.name_changed = 1;
		}

		MMERefreshUI(pDoc, false);
	} 
	else {
		EditRedoLast(pEMDoc->emDoc.edit_undo_stack);
	}
}

static void MMEMUndoFreeCB(MoveMultiDoc *pEMDoc, MMEMUndoData *pData)
{
	int i = eaFind(&pEMDoc->eaMoveDocs, pData->pDoc);
	if (i >= 0) {
		pData->pDoc = NULL;
		StructDestroy(parse_DynMove, pData->pPreObject);
		StructDestroy(parse_DynMove, pData->pPostObject);
		free(pData);
	}
}


//-----------------------------------------------------------------------------------
// Callbacks
//-----------------------------------------------------------------------------------

static void MMESortOnceButton(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	pDoc->bOneTimeSortWindow = true;
	mmeUINeedsRefresh = true;
}

static void MMEAutoSortToggle(UICheckButton *pButton, UserData pData)
{
	mmeUINeedsRefresh = true;
}

static void MMEDisplayCheckboxToggle(UICheckButton *pButton, UserData pData)
{
	mmeUINeedsRefresh = true;
}

static void MMEVisualizeCheckboxToggle(UICheckButton *pButton, UserData pData)
{
	;
}

static void MMEDocButtonOpenGraph(UIButton *pButton, UserData pData)
{
	DynAnimGraph *pGraph = (DynAnimGraph*)pData;
	if (pGraph && pGraph->pcName)
		emOpenFileEx(pGraph->pcName, "AnimGraph");
}

static void MMEDocButtonOpenChart(UIButton *pButton, UserData pData)
{
	DynAnimChartLoadTime *pChartLT = (DynAnimChartLoadTime*)pData;
	if (pChartLT && pChartLT->pcName)
		emOpenFileEx(pChartLT->pcName, ANIM_CHART_EDITED_DICTIONARY);
}

static void MMEDocButtonRefreshGraphs(UIButton *pButton, UserData pData)
{
	mmeUINeedsRefresh = true;
}

static void MMEDocButtonUp(UIButton *pButton, UserData pData)
{
	MoveDoc *pMMEDoc = (MoveDoc*)pData;
	MoveMultiDoc *pDoc = pMMEDoc->pParent;

	if (pDoc && !MEContextExists())
	{
		int i = eaFind(&pDoc->eaMoveDocs, pMMEDoc);
		if (i > 0) {
			eaRemove(&pDoc->eaMoveDocs, i);
			eaInsert(&pDoc->eaMoveDocs, pMMEDoc, i-1);
			FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
				ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pCurDoc->pExpander);
			FOR_EACH_END;
			FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
				ui_ExpanderGroupAddExpander(pDoc->pExpanderGroup, pCurDoc->pExpander);
			FOR_EACH_END;
			MMEScrollToDoc(pMMEDoc);
			mmeUINeedsRefresh = true;
		}
	}
}

static void MMEDocButtonDown(UIButton *pButton, UserData pData)
{
	MoveDoc *pMMEDoc = (MoveDoc*)pData;
	MoveMultiDoc *pDoc = pMMEDoc->pParent;

	if (pDoc && !MEContextExists())
	{
		int i = eaFind(&pDoc->eaMoveDocs, pMMEDoc);
		if (0 <= i && i+1 < eaSize(&pDoc->eaMoveDocs)) {
			MoveDoc *pRmDoc = eaRemove(&pDoc->eaMoveDocs, i+1);
			eaInsert(&pDoc->eaMoveDocs, pRmDoc, i);
			FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
				ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pCurDoc->pExpander);
			FOR_EACH_END;
			FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
				ui_ExpanderGroupAddExpander(pDoc->pExpanderGroup, pCurDoc->pExpander);
			FOR_EACH_END;
			MMEScrollToDoc(pMMEDoc);
			mmeUINeedsRefresh = true;
		}
	}
}

static void MMEDocButtonKill(UIButton *pButton, UserData pData)
{
	MoveDoc *pMMEDoc = (MoveDoc*)pData;
	MoveMultiDoc *pDoc = pMMEDoc->pParent;

	if (pDoc && !MEContextExists())
	{
		MMECloseMove(pMMEDoc);
		mmeUINeedsRefresh = true;
	}
}

static void MMEDocButtonEnableDelete(UIButton *pBUtton, UserData pData)
{
	MoveDoc *pDoc = (MoveDoc*)pData;
	if (pData) {
		pDoc->bEnableDelete = !pDoc->bEnableDelete;
		mmeUINeedsRefresh = true;
	}
}

static void MMEDocButtonDelete(UIButton *pButton, UserData pData)
{
	//if (pData) {
	//	MMEDeleteDoc((MoveDoc*)pData);
	//}

	MoveDoc *pDoc = (MoveDoc*)pData;
	bool bNeededToCheckAFileOut = false;

	if (fileExists(pDoc->pObject->pcFilename) && fileIsReadOnly(pDoc->pObject->pcFilename))
	{
		bNeededToCheckAFileOut = true;
		emStatusPrintf("Delete failed, need to checkout file %s", pDoc->pObject->pcFilename);
		AnimEditor_AskToCheckout(&pDoc->emDoc, pDoc->pObject->pcFilename);
	}

	if (!bNeededToCheckAFileOut)
	{
		MMEDeleteDoc(pDoc);
	}
}

static void MMEDocButtonSave(UIButton *pButton, UserData pData)
{
	//if (pData) {
	//	MMESaveDoc((MoveDoc*)pData);
	//}

	MoveDoc *pDoc = (MoveDoc*)pData;
	bool bNeededToCheckAFileOut = false;

	if (fileExists(pDoc->pObject->pcFilename) && fileIsReadOnly(pDoc->pObject->pcFilename))
	{
		bNeededToCheckAFileOut = true;
		emStatusPrintf("Save failed, need to checkout file %s", pDoc->pObject->pcFilename);
		AnimEditor_AskToCheckout(&pDoc->emDoc, pDoc->pObject->pcFilename);
	}

	if (!bNeededToCheckAFileOut)
	{
		MMESaveDoc(pDoc);
	}
}

static void MMEDocButtonDuplicate(UIButton *pButton, UserData pData)
{
	MMEDuplicateDoc((MoveDoc*)pData);
}

static void MMEDocButtonRevert(UIButton *pButton, UserData pData)
{
	MMERevertDoc((MoveDoc*)pData);
}

static void MMEDocButtonAddBonePosition(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveSeq *pMS = (DynMoveSeq*)pData;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			int i = eaFind(&pCurDoc->pObject->eaDynMoveSeqs, pMS);
			if (i >= 0) {
				DynMoveSeq *pCurMoveSeq = pCurDoc->pObject->eaDynMoveSeqs[i];
				DynMoveBoneOffset *pNewBO = StructCreate(parse_DynMoveBoneOffset);
				eaPush(&pCurMoveSeq->dynMoveAnimTrack.eaBoneOffset, pNewBO);
				MMERefreshUI(pCurDoc, true);
				break;
			}
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonAddBoneRotation(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveSeq *pMS = (DynMoveSeq*)pData;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			int i = eaFind(&pCurDoc->pObject->eaDynMoveSeqs, pMS);
			if (i >= 0) {
				DynMoveSeq *pCurMoveSeq = pCurDoc->pObject->eaDynMoveSeqs[i];
				DynMoveBoneRotOffset *pNewBR = StructCreate(parse_DynMoveBoneRotOffset);
				eaPush(&pCurMoveSeq->dynMoveAnimTrack.eaBoneRotation, pNewBR);
				MMERefreshUI(pCurDoc, true);
				break;
			}
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonAddFx(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveSeq *pMS = (DynMoveSeq*)pData;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			int i = eaFind(&pCurDoc->pObject->eaDynMoveSeqs, pMS);
			if (i >= 0) {
				DynMoveSeq *pCurMoveSeq = pCurDoc->pObject->eaDynMoveSeqs[i];
				DynMoveFxEvent *pNewFX = StructCreate(parse_DynMoveFxEvent);
				pNewFX->bMessage = true;
				eaPush(&pCurMoveSeq->eaDynMoveFxEvents, pNewFX);
				MMERefreshUI(pCurDoc, true);	
				break;
			}
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonAddMatchBaseSkelAnim(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveSeq *pMS = (DynMoveSeq*)pData;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			int i = eaFind(&pCurDoc->pObject->eaDynMoveSeqs, pMS);
			if (i >= 0) {
				DynMoveSeq *pCurMoveSeq = pCurDoc->pObject->eaDynMoveSeqs[i];
				DynMoveMatchBaseSkelAnim *pNewMBSA = StructCreate(parse_DynMoveMatchBaseSkelAnim);
				eaPush(&pCurMoveSeq->eaMatchBaseSkelAnim, pNewMBSA);
				MMERefreshUI(pCurDoc, true);	
				break;
			}
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonAddFrameRange(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveSeq *pMS = (DynMoveSeq*)pData;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			int i = eaFind(&pCurDoc->pObject->eaDynMoveSeqs, pMS);
			if (i >= 0) {
				DynMoveSeq *pCurMoveSeq = pCurDoc->pObject->eaDynMoveSeqs[i];
				DynMoveFrameRange *pNewFR = StructCreate(parse_DynMoveFrameRange);
				eaPush(&pCurMoveSeq->dynMoveAnimTrack.eaNoInterpFrameRange, pNewFR);
				MMERefreshUI(pCurDoc, true);
				break;
			}
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonAddMoveSequence(UIButton *pButton, UserData pData)
{
	char newName[MAX_PATH];
	MoveDoc *pDoc = (MoveDoc*)pData;
	DynMoveSeq *pMS = StructCreate(parse_DynMoveSeq);
	eaPush(&pDoc->pObject->eaDynMoveSeqs, pMS);

	sprintf(newName,"%s_%d",MME_NEWSEQNAME,++mmeNewNameCount);
	pMS->pcDynMoveSeq = StructAllocString(newName);
	
	sprintf(newName, "%s_%d", MME_NEWANIMTRACKNAME, ++mmeNewNameCount);
	pMS->dynMoveAnimTrack.pcAnimTrackName = StructAllocString(newName);

	MMERefreshUI(pDoc, true);
}

static void MMEDocButtonAddTag(UIButton *pButton, UserData pData)
{
	MoveDoc *pDoc = (MoveDoc*)pData;
	DynMoveTag *pTag = StructCreate(parse_DynMoveTag);
	pTag->pcTag = allocAddString("");
	eaPush(&pDoc->pObject->eaDynMoveTags,pTag);
	MMERefreshUI(pDoc, true);
}

static void MMEDocButtonAddTextFilter(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)pData;
	MMEMFilter *pFilter = StructCreate(parse_MMEMFilter);
	pFilter->eMoveFilterType = mmeFilterType_Text;
	pFilter->pcMoveFilterText = NULL;
	eaPush(&pDoc->eaFilters, pFilter);
	mmeUINeedsRefresh = true;
}

static void MMEDocButtonAddNumberFilter(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)pData;
	MMEMFilter *pFilter = StructCreate(parse_MMEMFilter);
	pFilter->eMoveFilterType = mmeFilterType_Number;
	pFilter->fMoveFilterValue = 0.0;
	eaPush(&pDoc->eaFilters, pFilter);
	mmeUINeedsRefresh = true;
}

static void MMEDocButtonAddBitFilter(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)pData;
	MMEMFilter *pFilter = StructCreate(parse_MMEMFilter);
	pFilter->eMoveFilterType = mmeFilterType_Bit;
	pFilter->eMoveFilterBit = mmeBit_DisableLeftWrist;
	eaPush(&pDoc->eaFilters, pFilter);
	mmeUINeedsRefresh = true;
}

static void MMEDocButtonKillBoneOffset(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveBoneOffset *pBO = (DynMoveBoneOffset*)pData;
	bool bRemoved = false;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			FOR_EACH_IN_EARRAY(pCurDoc->pObject->eaDynMoveSeqs, DynMoveSeq, pCurMoveSeq)
			{
				int i = eaFind(&pCurMoveSeq->dynMoveAnimTrack.eaBoneOffset, pBO);
				if (i >= 0) {
					eaRemove(&pCurMoveSeq->dynMoveAnimTrack.eaBoneOffset, i);
					MMERefreshUI(pCurDoc, true);
					bRemoved = true;
					break;
				}
			}
			FOR_EACH_END;
			if (bRemoved) break;
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonKillBoneRotation(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveBoneRotOffset *pBR = (DynMoveBoneRotOffset*)pData;
	bool bRemoved = false;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			FOR_EACH_IN_EARRAY(pCurDoc->pObject->eaDynMoveSeqs, DynMoveSeq, pCurMoveSeq)
			{
				int i = eaFind(&pCurMoveSeq->dynMoveAnimTrack.eaBoneRotation, pBR);
				if (i >= 0) {
					eaRemove(&pCurMoveSeq->dynMoveAnimTrack.eaBoneRotation, i);
					MMERefreshUI(pCurDoc, true);
					bRemoved = true;
					break;
				}
			}
			FOR_EACH_END;
			if (bRemoved) break;
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonKillFx(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveFxEvent *pFx = (DynMoveFxEvent*)pData;
	bool bRemoved = false;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			FOR_EACH_IN_EARRAY(pCurDoc->pObject->eaDynMoveSeqs, DynMoveSeq, pCurMoveSeq)
			{
				int i = eaFind(&pCurMoveSeq->eaDynMoveFxEvents, pFx);
				if (i >= 0) {
					eaRemove(&pCurMoveSeq->eaDynMoveFxEvents, i);
					MMERefreshUI(pCurDoc, true);
					bRemoved = true;
					break;
				}
			}
			FOR_EACH_END;
			if (bRemoved) break;
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonKillMatchBaseSkelAnim(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveMatchBaseSkelAnim *pMBSA = (DynMoveMatchBaseSkelAnim *)pData;
	bool bRemoved = false;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			FOR_EACH_IN_EARRAY(pCurDoc->pObject->eaDynMoveSeqs, DynMoveSeq, pCurMoveSeq)
			{
				int i = eaFind(&pCurMoveSeq->eaMatchBaseSkelAnim, pMBSA);
				if (i >= 0) {
					eaRemove(&pCurMoveSeq->eaMatchBaseSkelAnim, i);
					StructDestroy(parse_DynMoveMatchBaseSkelAnim, pMBSA);
					MMERefreshUI(pCurDoc, true);
					bRemoved = true;
					break;
				}
			}
			FOR_EACH_END;
			if (bRemoved) break;
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonKillFrameRange(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveFrameRange *pFR = (DynMoveFrameRange*)pData;
	bool bRemoved = false;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			FOR_EACH_IN_EARRAY(pCurDoc->pObject->eaDynMoveSeqs, DynMoveSeq, pCurMoveSeq)
			{
				int i = eaFind(&pCurMoveSeq->dynMoveAnimTrack.eaNoInterpFrameRange, pFR);
				if (i >= 0) {
					eaRemove(&pCurMoveSeq->dynMoveAnimTrack.eaNoInterpFrameRange, i);
					MMERefreshUI(pCurDoc, true);
					bRemoved = true;
					break;
				}
			}
			FOR_EACH_END;
			if (bRemoved) break;
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonKillMoveSequence(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveSeq *pMS = (DynMoveSeq*)pData;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			int i = eaFind(&pCurDoc->pObject->eaDynMoveSeqs, pMS);
			if (i >= 0) {
				eaRemove(&pCurDoc->pObject->eaDynMoveSeqs, i);
				MMERefreshUI(pCurDoc, true);
				break;
			}
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonKillTag(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc =  (MoveMultiDoc*)emGetActiveEditorDoc();
	DynMoveTag *pTag = (DynMoveTag*)pData;

	if (pDoc && !MEContextExists())
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			int i = eaFind(&pCurDoc->pObject->eaDynMoveTags, pTag);
			if (i >= 0) {
				eaRemove(&pCurDoc->pObject->eaDynMoveTags, i);
				MMERefreshUI(pCurDoc, true);
				break;
			}
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonKillFilter(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	MMEMFilter *pFilter = (MMEMFilter*)pData;
	int i = eaFind(&pDoc->eaFilters, pFilter);
	if (i >= 0) {
		eaRemove(&pDoc->eaFilters, i);
		mmeUINeedsRefresh = true;
	}
}

static void MMEDocButtonPlay(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)pData;
	pDoc->bVisualizePlaying = true;
	if (pDoc->fVisualizeFrame == pDoc->fVisualizeLastFrame)
		pDoc->fVisualizeFrame = pDoc->fVisualizeFirstFrame;
}

static void MMEDocButtonPause(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)pData;
	pDoc->bVisualizePlaying = !pDoc->bVisualizePlaying;
}

static void MMEDocButtonSelectAll(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	S32 i = PTR_TO_S32(pData);

	FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
	{
		if (i == -1 || pCurDoc->pObject->pcFilename == pDoc->eaFileNames[i])
		{
			pCurDoc->bIsSelected = true;
			MMERefreshUI(pCurDoc, false);
		}
	}
	FOR_EACH_END;
}

static void MMEDocButtonSelectNone(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	S32 i = PTR_TO_S32(pData);

	FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
	{
		if (i == -1 || pCurDoc->pObject->pcFilename == pDoc->eaFileNames[i])
		{
			pCurDoc->bIsSelected = false;
			MMERefreshUI(pCurDoc, false);
		}
	}
	FOR_EACH_END;
}

static void MMEDocButtonApplyFilters(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	S32 i = PTR_TO_S32(pData);

	FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
	{
		if (i == -1 || pCurDoc->pObject->pcFilename == pDoc->eaFileNames[i])
		{
			bool bitPresent = MMEDocPassesFilters(pCurDoc, pDoc);
			if (bitPresent) {
				if      (pDoc->eMoveFilterPresentOp == mmeFilterOp_Add)    pCurDoc->bIsSelected = true;
				else if (pDoc->eMoveFilterPresentOp == mmeFilterOp_Remove) pCurDoc->bIsSelected = false;
				//else if (pDoc->eMoveFilterPresentOp == mmeFilterOp_Same) ;//do nothing
			} else {
				if      (pDoc->eMoveFilterAbsentOp == mmeFilterOp_Add)    pCurDoc->bIsSelected = true;
				else if (pDoc->eMoveFilterAbsentOp == mmeFilterOp_Remove) pCurDoc->bIsSelected = false;
				//else if (pDoc->eMoveFilterAbsentOp == mmeFilterOp_Same) ;//do nothing
			}
			MMERefreshUI(pCurDoc, false);
		}
	}
	FOR_EACH_END;
}

static void MMEDocButtonSaveSelected(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	S32 i = PTR_TO_S32(pData);
	const char **eaFileNames = NULL;
	bool bNeededToCheckAFileOut = false;

	if (i == -1) {
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
			eaPushUnique(&eaFileNames, pCurDoc->pObject->pcFilename);
		FOR_EACH_END;
	} else {
		eaPush(&eaFileNames, pDoc->eaFileNames[i]);
	}

	FOR_EACH_IN_EARRAY(eaFileNames, const char, pcFileName)
	{
		if (fileExists(pcFileName) && fileIsReadOnly(pcFileName))
		{
			bNeededToCheckAFileOut = true;
			FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
			{
				if (pCurDoc->pObject->pcFilename == pcFileName) {
					emStatusPrintf("Save failed, need to checkout file %s", pcFileName);
					AnimEditor_AskToCheckout(&pCurDoc->emDoc, pcFileName);
					break;
				}
			}
			FOR_EACH_END;
			break;
		}
	}
	FOR_EACH_END;

	eaDestroy(&eaFileNames);

	if (!bNeededToCheckAFileOut)
	{
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
		{
			if (pCurDoc->bIsSelected && (i == -1 || pCurDoc->pObject->pcFilename == pDoc->eaFileNames[i]))
			{
				MMESaveDoc(pCurDoc);
			}
		}
		FOR_EACH_END;
	}
}

static void MMEDocButtonDuplicateSelected(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	S32 i = PTR_TO_S32(pData);

	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
	{
		if (pCurDoc->bIsSelected && (i == -1 || pCurDoc->pObject->pcFilename == pDoc->eaFileNames[i]))
		{
			MMEDuplicateDoc(pCurDoc);
		}
	}
	FOR_EACH_END;
}

static void MMEDocButtonRevertSelected(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	S32 i = PTR_TO_S32(pData);

	FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
	{
		if (pCurDoc->bIsSelected && (i == -1 || pCurDoc->pObject->pcFilename == pDoc->eaFileNames[i]))
		{
			MMERevertDoc(pCurDoc);
		}
	}
	FOR_EACH_END;
}

static void MMEDocButtonCopyFxSelected(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	S32 i = PTR_TO_S32(pData);

	mmeUINeedsRefresh = true;

	while (eaSize(&pDoc->eaFxClipBoard))
	{
		DynMoveFxEvent *pFx = eaPop(&pDoc->eaFxClipBoard);
		StructDestroy(parse_DynMoveFxEvent, pFx);
	}

	FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
	{
		if (pCurDoc->bIsSelected && (i == -1 || pCurDoc->pObject->pcFilename == pDoc->eaFileNames[i]))
		{
			FOR_EACH_IN_EARRAY(pCurDoc->pObject->eaDynMoveSeqs, DynMoveSeq, pCurMoveSeq)
			{
				FOR_EACH_IN_EARRAY(pCurMoveSeq->eaDynMoveFxEvents, DynMoveFxEvent, pCopyFx)
				{
					bool bUnique = true;

					FOR_EACH_IN_EARRAY(pDoc->eaFxClipBoard, DynMoveFxEvent, pCheckEvent)
					{
						if (pCheckEvent->pcFx     == pCopyFx->pcFx    &&
							pCheckEvent->uiFrame  == pCopyFx->uiFrame &&
							pCheckEvent->bMessage == pCopyFx->bMessage )
						{
							bUnique = false;
							break;
						}
					}
					FOR_EACH_END;

					if (bUnique) {
						DynMoveFxEvent *pPushFx = StructClone(parse_DynMoveFxEvent, pCopyFx);
						eaPush(&pDoc->eaFxClipBoard, pPushFx);
					}
				}
				FOR_EACH_END;
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
}

static void MMEDocButtonPasteFxSelected(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	S32 i = PTR_TO_S32(pData);

	FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
	{
		if (pCurDoc->bIsSelected && (i == -1 || pCurDoc->pObject->pcFilename == pDoc->eaFileNames[i]))
		{
			bool bRefresh = false;

			FOR_EACH_IN_EARRAY(pCurDoc->pObject->eaDynMoveSeqs, DynMoveSeq, pCurMoveSeq)
			{
				FOR_EACH_IN_EARRAY(pDoc->eaFxClipBoard, DynMoveFxEvent, pClipboardFx)
				{
					bool bUnique = true;
					
					FOR_EACH_IN_EARRAY(pCurMoveSeq->eaDynMoveFxEvents, DynMoveFxEvent, pCheckEvent)
					{
						if (pCheckEvent->pcFx     == pClipboardFx->pcFx    &&
							pCheckEvent->uiFrame  == pClipboardFx->uiFrame &&
							pCheckEvent->bMessage == pClipboardFx->bMessage )
						{
								bUnique = false;
								break;
						}
					}
					FOR_EACH_END;

					if (bUnique) {
						DynMoveFxEvent *pPushFx = StructClone(parse_DynMoveFxEvent, pClipboardFx);
						eaPush(&pCurMoveSeq->eaDynMoveFxEvents, pPushFx);
						bRefresh = true;
					}
				}
				FOR_EACH_END;
			}
			FOR_EACH_END;

			if (bRefresh) {
				MMERefreshUI(pCurDoc, true);
			}
		}
	}
	FOR_EACH_END;
}

static void MMEDocButtonDeleteFxSelected(UIButton *pButton, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	S32 i = PTR_TO_S32(pData);

	FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCurDoc)
	{
		if (pCurDoc->bIsSelected && (i == -1 || pCurDoc->pObject->pcFilename == pDoc->eaFileNames[i]))
		{
			bool bRefresh = false;

			FOR_EACH_IN_EARRAY(pCurDoc->pObject->eaDynMoveSeqs, DynMoveSeq, pCurMoveSeq)
			{
				while (eaSize(&pCurMoveSeq->eaDynMoveFxEvents))
				{
					DynMoveFxEvent *pFxEvent = eaPop(&pCurMoveSeq->eaDynMoveFxEvents);
					StructDestroy(parse_DynMoveFxEvent, pFxEvent);
					bRefresh = true;
				}
			}
			FOR_EACH_END;

			if (bRefresh) {
				MMERefreshUI(pCurDoc, true);
			}
		}
	}
	FOR_EACH_END;
}

static void MMEDocButtonSelected(UIButton *pButton, UserData pData)
{
	MoveDoc *pDoc = (MoveDoc*)pData;
	if (pDoc && !MEContextExists()) {
		pDoc->bIsSelected = !pDoc->bIsSelected;
		MMERefreshUI(pDoc, false);
	}
}

static void MMEDocButtonName(UIButton *pButton, UserData pData)
{
	MoveDoc *pDoc = (MoveDoc*)pData;

	if (pDoc && pDoc->bIsSelected && !MEContextExists())
	{
		MMEScrollToDoc(pDoc);
		MMERefreshUI(pDoc, false);
	}
}


//-----------------
// Special CBs
//-----------------

static void MMEWindowContextChangedCB(MEField *pField, bool bFinished, UserData pData)
{
	MoveDoc *pDoc = (MoveDoc*)pData;
	bool nameChanged, fileChanged, scopeChanged;
	char name[MAX_PATH];

	//check for changes in the data
	nameChanged  = pDoc->pObject->pcName         != pDoc->pNextUndoObject->pcName;
	fileChanged  = pDoc->pObject->pcUserFilename != pDoc->pNextUndoObject->pcUserFilename;
	scopeChanged = pDoc->pObject->pcUserScope    != pDoc->pNextUndoObject->pcUserScope;

	//handle name changes
	if (nameChanged) {
		pDoc->emDoc.name_changed = 1;
		sprintf(pDoc->emDoc.doc_name,         "%s",  pDoc->pObject->pcName);
		sprintf(pDoc->emDoc.doc_display_name, "%s*", pDoc->pObject->pcName);
	}

	//handle file name changes
	if (nameChanged || fileChanged || scopeChanged) {
		resFixFilename(hDynMoveDict, pDoc->pObject->pcName, pDoc->pObject);
	}

	//update the expander bar name
	if (StructCompare(parse_DynMove, pDoc->pObject, pDoc->pOrigObject, 0, 0, 0)) {
		sprintf(name, "%s*", pDoc->pObject->pcName);
	} else {
		strcpy(name, pDoc->pObject->pcName);
	}
	ui_ExpanderSetName(pDoc->pExpander, allocAddString(name));
	
	//show the document with any updates
	MMERefreshUI(pDoc, true);
}

static void MMESortChangedCB(MEField *pField, bool bFinished, UserData pData)
{
	if (!MEContextExists())
	{
		mmeUINeedsRefresh = true;
	}
}

static void MMEVisualizeMoveChangedCB(MEField *pField, bool bFinished, UserData pData)
{
	if (!MEContextExists())
	{
		MoveMultiDoc *pDoc = (MoveMultiDoc*)pData;
		MMESetVisualizeMove(pDoc);
	}
}

static void MMEVisualizeMoveSeqChangedCB(MEField *pField, bool bFinished, UserData pData)
{
	if (!MEContextExists())
	{
		MoveMultiDoc *pDoc = (MoveMultiDoc*)pData;
		MMESetVisualizeMoveSeq(pDoc);
	}
}

static void MMEVisualizeFrameChangedCB(MEField *pField, bool bFinished, UserData pData)
{
	;
}

static void MMEVisualizeFxChangedCB(MEField *pField, bool bFinished, UserData pData)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)pData;
	FOR_EACH_IN_EARRAY(pDoc->pVisualizeMoveSeq->eaDynMoveFxEvents, DynMoveFxEvent, pFx)
	{
		pFx->uiFrameTrigger = pFx->uiFrame;
		if (pFx->uiFrameTrigger > 0) pFx->uiFrameTrigger--; //convert to 0 1st
	}
	FOR_EACH_END;
	mmeUINeedsRefresh = true;
}

void MMEContentDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData)
{
	;
}


//-----------------------------------------------------------------------------------
// Filter functions
//-----------------------------------------------------------------------------------

static bool MMEDocPassesFilters(MoveDoc *pDoc, MoveMultiDoc *pEMDoc)
{
	DynMove *pMove = pDoc->pObject;

	//look for each filter, once you find it once just go on to look for the next one, if you never find a filter return false and quit early
	//note this it only looks at fields which the user enters & skips past ones which are procedurally filled
	FOR_EACH_IN_EARRAY(pEMDoc->eaFilters, MMEMFilter, pFilter)
	{
		bool foundIt = false;

		if (pFilter->eMoveFilterType == mmeFilterType_Text)
		{
			//basic move info
			if      (pMove->pcName     && strstri(pMove->pcName,     pFilter->pcMoveFilterText)) foundIt = true;
			else if (pMove->pcFilename && strstri(pMove->pcFilename, pFilter->pcMoveFilterText)) foundIt = true;
			else if (pMove->pcComments && strstri(pMove->pcComments, pFilter->pcMoveFilterText)) foundIt = true;
			else {
				FOR_EACH_IN_EARRAY(pMove->eaDynMoveTags, DynMoveTag, pTag)
				{
					if (pTag->pcTag && strstri(pTag->pcTag, pFilter->pcMoveFilterText)) {
						foundIt = true;
						break;
					}
				}
				FOR_EACH_END;
			}
		}
		if (foundIt) continue;

		FOR_EACH_IN_EARRAY(pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq)
		{
			//basic move seq info
			switch (pFilter->eMoveFilterType) {
			case mmeFilterType_Text:
				{
					if      (pMoveSeq->pcDynMoveSeq			&& strstri(pMoveSeq->pcDynMoveSeq,			pFilter->pcMoveFilterText)) foundIt = true;
					else if (pMoveSeq->pcIKTarget			&& strstri(pMoveSeq->pcIKTarget,			pFilter->pcMoveFilterText)) foundIt = true;
					else if (pMoveSeq->pcIKTargetNodeLeft	&& strstri(pMoveSeq->pcIKTargetNodeLeft,	pFilter->pcMoveFilterText)) foundIt = true;
					else if (pMoveSeq->pcIKTargetNodeRight	&& strstri(pMoveSeq->pcIKTargetNodeRight,	pFilter->pcMoveFilterText)) foundIt = true;
					else if (pMoveSeq->dynMoveAnimTrack.pcAnimTrackName && strstri(pMoveSeq->dynMoveAnimTrack.pcAnimTrackName, pFilter->pcMoveFilterText)) foundIt = true;
				}
				break;
			case mmeFilterType_Number:
				{
					if      (pMoveSeq->fBankMaxAngle == pFilter->fMoveFilterValue) foundIt = true;
					else if (pMoveSeq->fBankScale    == pFilter->fMoveFilterValue) foundIt = true;
					else if (pMoveSeq->fDistance     == pFilter->fMoveFilterValue) foundIt = true;
					else if (pMoveSeq->fLength       == pFilter->fMoveFilterValue) foundIt = true;
					else if (pMoveSeq->fMaxRate      == pFilter->fMoveFilterValue) foundIt = true;
					else if (pMoveSeq->fMinRate      == pFilter->fMoveFilterValue) foundIt = true;
					else if (pMoveSeq->fSpeed        == pFilter->fMoveFilterValue) foundIt = true;
					else if (pMoveSeq->dynMoveAnimTrack.uiFirst == pFilter->fMoveFilterValue) foundIt = true;
					else if (pMoveSeq->dynMoveAnimTrack.uiLast  == pFilter->fMoveFilterValue) foundIt = true;
					else if (pMoveSeq->uiRagdollFrame			== pFilter->fMoveFilterValue) foundIt = true;
					else if (pMoveSeq->fRagdollAdditionalGravity== pFilter->fMoveFilterValue) foundIt = true;
				}
				break;
			case mmeFilterType_Bit:
				{
					if      (pFilter->eMoveFilterBit == mmeBit_DisableLeftWrist && pMoveSeq->bDisableIKLeftWrist)	foundIt = true;
					else if (pFilter->eMoveFilterBit == mmeBit_DisableRightArm  && pMoveSeq->bDisableIKRightArm)	foundIt = true;
					else if (pFilter->eMoveFilterBit == mmeBit_EnableSliding    && pMoveSeq->bEnableIKSliding)		foundIt = true;
					else if (pFilter->eMoveFilterBit == mmeBit_MeleeMode        && pMoveSeq->bIKMeleeMode)			foundIt = true;
					else if (pFilter->eMoveFilterBit == mmeBit_Message          && pMoveSeq->bIKMeleeMode)			foundIt = true;
					else if (pFilter->eMoveFilterBit == mmeBit_Ragdoll          && pMoveSeq->bRagdoll)				foundIt = true;
					else if (pFilter->eMoveFilterBit == mmeBit_RegisterWep      && pMoveSeq->bRegisterWep)			foundIt = true;
					else if (pFilter->eMoveFilterBit == mmeBit_IKBothHands		&& pMoveSeq->bIKBothHands)			foundIt = true;
					else if (pFilter->eMoveFilterBit == mmeBit_NoInterp         && pMoveSeq->dynMoveAnimTrack.bNoInterp)	foundIt = true;
				}
				break;
			}
			if (foundIt) break;

			//no interp frame ranges
			if (pFilter->eMoveFilterType == mmeFilterType_Number)
			{
				FOR_EACH_IN_EARRAY(pMoveSeq->dynMoveAnimTrack.eaNoInterpFrameRange, DynMoveFrameRange, pFrameRange)
				{
					if      (pFrameRange->iFirst == pFilter->fMoveFilterValue) foundIt = true;
					else if (pFrameRange->iLast  == pFilter->fMoveFilterValue) foundIt = true;

					if (foundIt) break;
				}
				FOR_EACH_END;
			}
			if (foundIt) break;
			
			//bone offsets
			FOR_EACH_IN_EARRAY(pMoveSeq->dynMoveAnimTrack.eaBoneOffset, DynMoveBoneOffset, pBoneOffset)
			{
				if (pFilter->eMoveFilterType == mmeFilterType_Text &&
					pBoneOffset->pcBoneName &&
					strstri(pBoneOffset->pcBoneName, pFilter->pcMoveFilterText))
					foundIt = true;

				if (pFilter->eMoveFilterType == mmeFilterType_Number) {
					if      (pBoneOffset->vOffset[0] == pFilter->fMoveFilterValue) foundIt = true;
					else if (pBoneOffset->vOffset[1] == pFilter->fMoveFilterValue) foundIt = true;
					else if (pBoneOffset->vOffset[2] == pFilter->fMoveFilterValue) foundIt = true;
				}

				if (foundIt) break;
			}
			FOR_EACH_END;
			if (foundIt) break;

			//bone rotations
			FOR_EACH_IN_EARRAY(pMoveSeq->dynMoveAnimTrack.eaBoneRotation, DynMoveBoneRotOffset, pBoneRotation)
			{
				if (pFilter->eMoveFilterType == mmeFilterType_Text &&
					pBoneRotation->pcBoneName &&
					strstri(pBoneRotation->pcBoneName, pFilter->pcMoveFilterText))
					foundIt = true;

				if (pFilter->eMoveFilterType == mmeFilterType_Number) {
					if      (pBoneRotation->qRotOffset[0] == pFilter->fMoveFilterValue) foundIt = true;
					else if (pBoneRotation->qRotOffset[1] == pFilter->fMoveFilterValue) foundIt = true;
					else if (pBoneRotation->qRotOffset[2] == pFilter->fMoveFilterValue) foundIt = true;
					else if (pBoneRotation->qRotOffset[3] == pFilter->fMoveFilterValue) foundIt = true;
				}

				if (foundIt) break;
			}
			FOR_EACH_END;
			if (foundIt) break;

			//FX
			FOR_EACH_IN_EARRAY(pMoveSeq->eaDynMoveFxEvents, DynMoveFxEvent, pFX)
			{
				if (pFilter->eMoveFilterType == mmeFilterType_Text &&
					pFX->pcFx &&
					strstri(pFX->pcFx, pFilter->pcMoveFilterText))
					foundIt = true;

				if (pFilter->eMoveFilterType == mmeFilterType_Number &&
					pFX->uiFrame == pFilter->fMoveFilterValue)
					foundIt = true;

				if (pFilter->eMoveFilterType == mmeFilterType_Bit &&
					pFilter->eMoveFilterBit == mmeBit_Message &&
					pFX->bMessage
					)
					foundIt = true;

				if (foundIt) break;
			}
			FOR_EACH_END;
			if (foundIt) break;
		}
		FOR_EACH_END;
		if (foundIt) continue;

		return false;
	}
	FOR_EACH_END;

	return true;
}


//-----------------------------------------------------------------------------------
// Display functions
//-----------------------------------------------------------------------------------

static void MMERefreshUI(MoveDoc *pDoc, bool bUndoable)
{
	mmeUINeedsRefresh = true;
	if (bUndoable && StructCompare(parse_DynMove, pDoc->pObject, pDoc->pNextUndoObject, 0, 0, 0))
	{
		MMEMUndoData *pData = (MMEMUndoData*)calloc(1,sizeof(MMEMUndoData));

		//setup the undo stack
		pData->pDoc = pDoc;
		pData->pPreObject = pDoc->pNextUndoObject;

		pData->pPostObject = StructClone(parse_DynMove, pDoc->pObject);

		EditCreateUndoCustom(pDoc->pParent->emDoc.edit_undo_stack, MMEMUndoCB, MMEMRedoCB, MMEMUndoFreeCB, pData);

		pDoc->pNextUndoObject = StructClone(parse_DynMove, pDoc->pObject);
	}
}

static void MMEUpdateDisplayAddButton(const char *pcContextName, const char *pcButtonName, UIActivationFunc cbFunc, UserData pData, F32 xOffset, U32 yRowsUp, F32 width, const char *pcToolTip)
{
	U32 i;
	MEFieldContextEntry *pContextEntry;
	char pcUniqueName[MAX_PATH];
	strcpy(pcUniqueName, pcContextName);
	strcat(pcUniqueName, pcButtonName);
	for (i = 0; i < yRowsUp; i++)
		MEContextStepBackUp();
	pContextEntry = MEContextAddButton(pcButtonName, NULL, cbFunc, pData, allocAddString(pcUniqueName), NULL, pcToolTip);
	ui_WidgetSetWidthEx(UI_WIDGET(pContextEntry->pButton), width, UIUnitFixed);
	ui_WidgetSetPosition(
		UI_WIDGET(pContextEntry->pButton),
		UI_WIDGET(pContextEntry->pButton)->x + xOffset,
		UI_WIDGET(pContextEntry->pButton)->y);
}

static void MMEUpdateDisplayAddCheck(const char *pcContextName, const char *pcCheckName, bool bCurState, UIActivationFunc cbFunc, UserData pData, F32 xOffset, U32 yRowsUp, F32 width, const char *pcToolTip)
{
	U32 i;
	MEFieldContextEntry *pContextEntry;
	char pcUniqueName[MAX_PATH];
	strcpy(pcUniqueName, pcContextName);
	strcat(pcUniqueName, pcCheckName);
	for (i = 0; i < yRowsUp; i++)
		MEContextStepBackUp();
	pContextEntry = MEContextAddCheck(cbFunc, pData, bCurState, pcUniqueName, "", pcToolTip);
	ui_WidgetSetWidthEx(UI_WIDGET(pContextEntry->pCheck), width, UIUnitFixed);
	ui_WidgetSetPosition(
		UI_WIDGET(pContextEntry->pCheck),
		UI_WIDGET(pContextEntry->pCheck)->x + xOffset,
		UI_WIDGET(pContextEntry->pCheck)->y);
}

static void MMEUpdateDisplayAddLabel(const char *pcContextName, const char *pcLabelText, F32 xOffset, U32 yRowsUp, F32 width, const char *pcToolTip)
{
	U32 i;
	MEFieldContextEntry *pContextEntry;
	char pcUniqueName[MAX_PATH];
	strcpy(pcUniqueName, pcContextName);
	strcat(pcUniqueName, pcLabelText);
	for (i = 0; i < yRowsUp; i++)
		MEContextStepBackUp();
	pContextEntry = MEContextAddLabel(allocAddString(pcUniqueName), pcLabelText, pcToolTip);
	ui_WidgetSetWidthEx(UI_WIDGET(pContextEntry->pLabel), width, UIUnitFixed);
	ui_WidgetSetPosition(
		UI_WIDGET(pContextEntry->pLabel),
		UI_WIDGET(pContextEntry->pLabel)->x + xOffset,
		UI_WIDGET(pContextEntry->pLabel)->y);
}

static MEFieldContextEntry *MMEUpdateDisplayAddSimple(MEFieldType fieldType, const char *pcDisplayName, F32 xOffset, U32 yRowsUp, F32 width, const char *pcFieldName, const char *pcToolTip)
{
	U32 i;
	MEFieldContextEntry *pContextEntry;
	for (i = 0; i < yRowsUp; i++)
		MEContextStepBackUp();
	pContextEntry = MEContextAddSimple(fieldType, pcFieldName, pcDisplayName, pcToolTip);
	ui_WidgetSetWidthEx(pContextEntry->ppFields[0]->pUIWidget, width, UIUnitFixed);
	ui_WidgetSetPosition(
		pContextEntry->ppFields[0]->pUIWidget,
		pContextEntry->ppFields[0]->pUIWidget->x + xOffset,
		pContextEntry->ppFields[0]->pUIWidget->y);
	ui_WidgetSetPadding(
		pContextEntry->ppFields[0]->pUIWidget,
		0.f, 0.f);
	return pContextEntry;
}

static void MMEUpdateDisplayWindow(MoveDoc *pDoc)
{
	MEFieldContext *pContext;
	DynMove *pMove = pDoc->pObject;
	F32 oldDataStart;
	char baseUID[MAX_PATH], baseNum[5];
	int loopId = 0;

	//determine the doc's window id
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pParent->eaMoveDocs, MoveDoc, pCheckDoc)
	{
		if (pCheckDoc == pDoc) {
			loopId = ipCheckDocIndex;
			break;
		}
	}
	FOR_EACH_END;

	//UID setup
	itoa(loopId, baseNum, 10);
	strcpy(baseUID, "Base");
	strcat(baseUID, baseNum);

	//Context setup
	pContext = MEContextPush(baseUID, pDoc->pOrigObject, pDoc->pObject, parse_DynMove);
	pContext->pUIContainer = UI_WIDGET(pDoc->pExpander);
	pContext->cbChanged = MMEWindowContextChangedCB;
	pContext->pChangedData = pDoc;
	MEContextAddSeparator("Line1");

	//up & down action buttons
	oldDataStart = pContext->iXDataStart;
	pContext->iXDataStart = 0;
	if (!mmeAutoSortButton->state) {
		MMEUpdateDisplayAddButton(baseUID, "^",			MMEDocButtonUp,			pDoc,   0, 0, 20, "Move up display list");
		MMEUpdateDisplayAddButton(baseUID, "v",			MMEDocButtonDown,		pDoc,  20, 1, 20, "Move down display list");
		MMEUpdateDisplayAddButton(baseUID, "Save",		MMEDocButtonSave,		pDoc,  40, 1, 50, "Save the move");
		MMEUpdateDisplayAddButton(baseUID, "Duplicate",	MMEDocButtonDuplicate,	pDoc,  90, 1, 75, "Duplicate the move");
		MMEUpdateDisplayAddButton(baseUID, "Revert",	MMEDocButtonRevert,		pDoc, 165, 1, 62, "Revert the move");
		MMEUpdateDisplayAddButton(baseUID, pDoc->bEnableDelete ? "Lock Delete" : "Unlock Delete", MMEDocButtonEnableDelete, pDoc, 227, 1, 100, "Enable the Delete Button");
		if (pDoc->bEnableDelete)
			MMEUpdateDisplayAddButton(baseUID, "Delete", MMEDocButtonDelete, pDoc, 327, 1, 100, "Delete the move");
	} else {
		MMEUpdateDisplayAddButton(baseUID, "Save",		MMEDocButtonSave,		pDoc,   0, 0, 50, "Save the move");
		MMEUpdateDisplayAddButton(baseUID, "Duplicate",	MMEDocButtonDuplicate,	pDoc,  50, 1, 75, "Duplicate the move");
		MMEUpdateDisplayAddButton(baseUID, "Revert",	MMEDocButtonRevert,		pDoc, 125, 1, 62, "Revert the move");
		MMEUpdateDisplayAddButton(baseUID, pDoc->bEnableDelete ? "Lock Delete" : "Unlock Delete", MMEDocButtonEnableDelete, pDoc, 187, 1, 100, "Enable the Delete Button");
		if (pDoc->bEnableDelete)
			MMEUpdateDisplayAddButton(baseUID, "Delete", MMEDocButtonDelete, pDoc, 287, 1, 100, "Delete the move");
	}
	//MMEUpdateDisplayAddButton(pDoc->pObject->pcName, "X",			MMEDocButtonKill,		pDoc,  40, 1, 25, "Remove from the display list");
	pContext->iXDataStart = oldDataStart;
	
	//Name (Move)
	oldDataStart = pContext->iXDataStart;
	pContext->iXDataStart = 65;
	MEContextAddSimple(kMEFieldType_TextEntry,	"Name",			"Name",		"Name of the move");
	MEContextAddSimple(kMEFieldType_TextEntry,	"UserScope",	"Scope",	"Directory the move is stored to on disk");
	MEContextAddSimple(kMEFieldType_TextEntry,	"UserFilename",	"File",		"File the move is stored to on disk");
	MEContextAddSimple(kMEFieldType_TextEntry,	"Comments",		"Comments",	"Comments about the move");
	pContext->iXDataStart = oldDataStart;

	EARRAY_FOREACH_BEGIN(pMove->eaDynMoveTags, i);
	{
		MEFieldContext *tagContext;
		char tagUID[MAX_PATH], tagNum[5];
			
		itoa(i, tagNum, 10);
		strcpy(tagUID, baseUID);
		strcat(tagUID, "Tag");
		strcat(tagUID, tagNum);

		tagContext = MEContextPush(tagUID,
			pDoc->pOrigObject && eaSize(&pDoc->pOrigObject->eaDynMoveTags) > i ? pDoc->pOrigObject->eaDynMoveTags[i] : NULL,
			pDoc->pObject->eaDynMoveTags[i],
			parse_DynMoveTag);

		oldDataStart = tagContext->iXDataStart;
		tagContext->iXDataStart = 0;
		MMEUpdateDisplayAddLabel(tagUID, "Tag", 35, 0, 30, "");
		MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 65, 1, 500, "Tag", "Tag that describes the move");
		MMEUpdateDisplayAddButton(tagUID, "X", MMEDocButtonKillTag, pDoc->pObject->eaDynMoveTags[i], 10, 1, 25, "Remove the tag");
		tagContext->iXDataStart = oldDataStart;

		MEContextPop(tagUID);
	}
	EARRAY_FOREACH_END;

	MMEUpdateDisplayAddButton(baseUID, "Add Tag", MMEDocButtonAddTag, pDoc, -150, 0, 185, "Add a descriptive tag to the move");

	//Move Seq
	EARRAY_FOREACH_BEGIN(pMove->eaDynMoveSeqs, i);
	{
		MEFieldContext *seqContext;
		DynMoveSeq *pNewSeq = pDoc->pObject->eaDynMoveSeqs[i];
		DynMoveSeq *pOldSeq = pDoc->pOrigObject && eaSize(&pDoc->pOrigObject->eaDynMoveSeqs) > i ? pDoc->pOrigObject->eaDynMoveSeqs[i] : NULL;
		char seqUID[MAX_PATH], seqNum[5];
		
		itoa(i, seqNum, 10);
		strcpy(seqUID, baseUID);
		strcat(seqUID, "Seq");
		strcat(seqUID, seqNum);

		seqContext = MEContextPush(seqUID, pOldSeq, pNewSeq, parse_DynMoveSeq);
		MEContextIndentRight();
		MEContextIndentRight();

		//Name (Move Seq)
		oldDataStart = seqContext->iXDataStart;
		seqContext->iXDataStart = 100;
		MEContextAddSimple(kMEFieldType_TextEntry,	"DynMoveSeq",	"Move Sequence",	"Name of the move sequence");
		MMEUpdateDisplayAddButton(seqUID, "X", MMEDocButtonKillMoveSequence, pNewSeq, -130, 1, 25, "Remove the move sequence");
		seqContext->iXDataStart = oldDataStart;
		MEContextIndentRight();
		MEContextIndentRight();

		//Anim track data
		{
			MEFieldContext *atContext;
			char atUID[MAX_PATH];

			strcpy(atUID, seqUID);
			strcat(atUID, "DynAnimTrack");

			atContext = MEContextPush(atUID,
				pOldSeq ? &pOldSeq->dynMoveAnimTrack : NULL,
				&pNewSeq->dynMoveAnimTrack,
				parse_DynMoveAnimTrack);

			if (mmeTrackButton->state)
			{
				//Name (Anim Track)
				oldDataStart = atContext->iXDataStart;
				atContext->iXDataStart = 100;
				MEContextAddSimple(kMEFieldType_TextEntry,	"AnimTrackName",	"Animation Track",	"Name of the animation track played by this move sequence");
				atContext->iXDataStart = oldDataStart;

				//Frame info
				oldDataStart = atContext->iXDataStart;
				atContext->iXDataStart = 0;
				MMEUpdateDisplayAddLabel(atUID, "Animation Track Frame Range", 50, 0, 200, "Animation Track Frame Range");
				MMEUpdateDisplayAddLabel(atUID, "First Frame", 250, 1, 75, "First frame of animation track to use for playback");
				MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 410, 1, 100, "First", "First frame of animation track to use for playback");
				MMEUpdateDisplayAddLabel(atUID, "Last Frame", 450, 1, 75, "Last frame of animation tack to use for playback");
				MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 610, 1, 100, "Last", "Last frame of animation tack to use for playback");
				//MMEUpdateDisplayAddLabel(atUID, "Offset", 650, 1, 50, "The offset relative to the other anim tracks");
				//MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 700, 1, 100, "Offset", "The offset relative to the other anim tracks");
				atContext->iXDataStart = oldDataStart;

				//Start Offset info
				oldDataStart = atContext->iXDataStart;
				atContext->iXDataStart = 0;
				MMEUpdateDisplayAddLabel(atUID, "Start Offset Frame Range", 50, 0, 200, "Frame Range to allow random 1st frame selection over");
				//MMEUpdateDisplayAddLabel(atUID, "First Frame", 250, 1, 75, "First frame in start offset frame range");
				MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 410, 1, 100, "StartOffsetFirst", "First frame in start offset frame range");
				//MMEUpdateDisplayAddLabel(atUID, "Last Frame", 450, 1, 75, "Last frame in start offset frame range");
				MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 610, 1, 100, "StartOffsetLast", "Last frame in start offset frame range");
				atContext->iXDataStart = oldDataStart;

				//No interpolation frame ranges
				EARRAY_FOREACH_BEGIN(pNewSeq->dynMoveAnimTrack.eaNoInterpFrameRange, j);
				{
					MEFieldContext *frContext;
					char frLoopUID[MAX_PATH], frLoopId[5];

					itoa(j, frLoopId, 10);
					strcpy(frLoopUID, atUID);
					strcat(frLoopUID, "FrameRange");
					strcat(frLoopUID, frLoopId);

					frContext = MEContextPush(frLoopUID,
						pOldSeq && eaSize(&pOldSeq->dynMoveAnimTrack.eaNoInterpFrameRange) > j ? pOldSeq->dynMoveAnimTrack.eaNoInterpFrameRange[j] : NULL,
						pNewSeq->dynMoveAnimTrack.eaNoInterpFrameRange[j],
						parse_DynMoveFrameRange);

					oldDataStart = frContext->iXDataStart;
					frContext->iXDataStart = 0;
					MMEUpdateDisplayAddLabel(frLoopUID, "No Interp. Frame Range", 50, 0, 200, "No Interp. Frame Range");
					//MMEUpdateDisplayAddLabel(frLoopUID, "First Frame", 250, 1, 75, "First frame to apply no interpolation");
					MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 410, 1, 100, "First", "First frame to apply no interpolation");
					//MMEUpdateDisplayAddLabel(frLoopUID, "Last Frame", 450, 1, 75, "Last frame to apply no interpolation");
					MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 610, 1, 100, "Last", "Last frame to apply no interpolation");
					MMEUpdateDisplayAddButton(frLoopUID, "X", MMEDocButtonKillFrameRange, pNewSeq->dynMoveAnimTrack.eaNoInterpFrameRange[j], 12, 1, 25, "Remove the no interp. frame range");
					frContext->iXDataStart = oldDataStart;

					MEContextPop(frLoopUID);
				}
				EARRAY_FOREACH_END;

				MMEUpdateDisplayAddButton(atUID, "Add No Interp. Frame Range", MMEDocButtonAddFrameRange, pNewSeq, -21, 0, 185, "Add a new no interp. frame range");
			}

			//Bone offsets
			if (mmeBoneCheckButton->state)
			{
				//Position
				EARRAY_FOREACH_BEGIN(pNewSeq->dynMoveAnimTrack.eaBoneOffset, j);
				{
					MEFieldContext *boContext;
					MEFieldContextEntry *boContextEntry;
					char boUID[MAX_PATH], boNum[5];

					itoa(j, boNum, 10);
					strcpy(boUID, atUID);
					strcat(boUID, "BoneOffsets");
					strcat(boUID, boNum);

					boContext = MEContextPush(boUID,
						pOldSeq && eaSize(&pOldSeq->dynMoveAnimTrack.eaBoneOffset) > j ? pOldSeq->dynMoveAnimTrack.eaBoneOffset[j] : NULL,
						pNewSeq->dynMoveAnimTrack.eaBoneOffset[j],
						parse_DynMoveBoneOffset);

					oldDataStart = boContext->iXDataStart;
					boContext->iXDataStart = 0;
					MMEUpdateDisplayAddLabel(boUID, "Bone Offset", 50, 0, 85, "");
					MMEUpdateDisplayAddLabel(boUID, "Name", 145, 1, 35, "Name of the bone to offset");
					MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 275, 1, 100, "BoneName", "Name of the bone to offset");
					MMEUpdateDisplayAddLabel(boUID, "Position", 325, 1, 50, "Position to offset the bone by");
					boContext->iXDataStart = 380;
					MEContextStepBackUp();
					boContextEntry = MEContextAddMinMax(kMEFieldType_MultiSpinner, -10, 10, 0.001, "Offset", "", "Position to offset the bone by");
					ui_MultiSpinnerEntrySetValue(boContextEntry->ppFields[0]->pUIMultiSpinner, pNewSeq->dynMoveAnimTrack.eaBoneOffset[j]->vOffset, 3);
					boContext->iXDataStart = 0;
					MMEUpdateDisplayAddButton(boUID, "X", MMEDocButtonKillBoneOffset, pNewSeq->dynMoveAnimTrack.eaBoneOffset[j], 12, 1, 25, "Remove the bone offset");
					boContext->iXDataStart = oldDataStart;

					MEContextPop(boUID);
				}
				EARRAY_FOREACH_END;

				//Rotation
				EARRAY_FOREACH_BEGIN(pNewSeq->dynMoveAnimTrack.eaBoneRotation, j);
				{
					MEFieldContext *brContext;
					MEFieldContextEntry *brContextEntry;
					char brUID[MAX_PATH], brNum[5];

					itoa(j, brNum, 10);
					strcpy(brUID, atUID);
					strcat(brUID, "BoneRotations");
					strcat(brUID, brNum);

					brContext = MEContextPush(brUID,
						pOldSeq && eaSize(&pOldSeq->dynMoveAnimTrack.eaBoneRotation) > j ? pOldSeq->dynMoveAnimTrack.eaBoneRotation[j] : NULL,
						pNewSeq->dynMoveAnimTrack.eaBoneRotation[j],
						parse_DynMoveBoneRotOffset);
					
					oldDataStart = brContext->iXDataStart;
					brContext->iXDataStart = 0;
					MMEUpdateDisplayAddLabel(brUID, "Bone Rotation", 50, 0, 85, "");
					MMEUpdateDisplayAddLabel(brUID, "Name", 145, 1, 35, "Name of the bone to offset");
					MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 275, 1, 100, "BoneName", "Name of the bone to offset");
					MMEUpdateDisplayAddLabel(brUID, "Rotation", 325, 1, 50, "Rotation to offset the bone by");
					brContext->iXDataStart = 380;
					MEContextStepBackUp();
					brContextEntry = MEContextAddMinMax(kMEFieldType_MultiSpinner, -1, 1, 0.001, "qRotOffset", "", "Rotation to offset the bone by");
					ui_MultiSpinnerEntrySetValue(brContextEntry->ppFields[0]->pUIMultiSpinner, pNewSeq->dynMoveAnimTrack.eaBoneRotation[j]->qRotOffset, 4);
					brContext->iXDataStart = 0;
					MMEUpdateDisplayAddButton(brUID, "X", MMEDocButtonKillBoneRotation, pNewSeq->dynMoveAnimTrack.eaBoneRotation[j], 12, 1, 25, "Remove the bone rotation");
					brContext->iXDataStart = oldDataStart;

					MEContextPop(brUID);
				}
				EARRAY_FOREACH_END;

				MMEUpdateDisplayAddButton(atUID, "Add Bone Offset", MMEDocButtonAddBonePosition, pNewSeq, -20, 0, 125, "Add a new bone position offset");
				MMEUpdateDisplayAddButton(atUID, "Add Bone Rotation", MMEDocButtonAddBoneRotation, pNewSeq,  105, 1, 130, "Add a new bone rotation offset");
			}
			MEContextPop(atUID);
		}

		//Physics data
		if (mmePhysicsCheckButton->state)
		{
			oldDataStart = seqContext->iXDataStart;
			seqContext->iXDataStart = 85;
			MEContextAddSimple(kMEFieldType_TextEntry,	"Distance",		"Distance",		"Distance traveled");
			MEContextAddSimple(kMEFieldType_TextEntry,	"Speed",		"Speed (Low)",	"Animation track speed when Speed High is set to zero, otherwise the lower speed bound for a range of values)");
			MEContextAddSimple(kMEFieldType_TextEntry,	"SpeedHigh",	"Speed High",	"Upper anim track speed when using a range of values");
			MEContextAddSimple(kMEFieldType_TextEntry,	"MinRate",		"Min Rate",		"Min Rate");
			MEContextAddSimple(kMEFieldType_TextEntry,	"MaxRate",		"Max Rate",		"Max Rate");
			MEContextAddSimple(kMEFieldType_TextEntry,	"BlendFrames",	"Blend Frames",	"Number of frames to blend into/out of the move for if it's played as a movement animation (7.5 is standard)");
			MEContextAddSimple(kMEFieldType_TextEntry,	"BankMaxAngle",	"Bank Angle",	"Maximum angle to allow during auto-banking");
			MEContextAddSimple(kMEFieldType_TextEntry,	"BankScale",	"Bank Scale",	"Effects how hard a character needs to turn to experience the maximum angle");
			seqContext->iXDataStart = 305;
			MEContextAddSimple(kMEFieldType_Check, "EnableTerrainTiltBlend", "Blend w/ T-pose based on the terrain's slope", "The moves appearance is scaled based on the terrain's slope and cskel's TerrainTiltMaxBlendAngle, this is useful to do stuff like make a character dynamically bend further forward at the waist while running up steeper slopes");
			seqContext->iXDataStart = oldDataStart;
		}
		
		oldDataStart = seqContext->iXDataStart;
		seqContext->iXDataStart = 305;
		MEContextAddSimple(kMEFieldType_Check, "PlayWhileStopped", "Show on Movement Sequencer Bones while Stopped", "When directly used as a movement in an animation chart (NOT as part of a graph), this animation will display on the movement sequencer bones (usually lower body) even if the character's movement type is stopped");
		seqContext->iXDataStart = 190;
		MEContextAddSimple(kMEFieldType_TextEntry, "DisableTorsoPointingTimeout", "Disable Torso Pointing Timeout", "Blend run'n'gun bone rotation off within the timeout period, potentially useful for vehicle fx and mounts. Must be set to a value greater than 0 to enable. Use 0.01 for instantaneous.");
		seqContext->iXDataStart = oldDataStart;

		//IK data
		if (mmeIkCheckButton->state)
		{
			oldDataStart = seqContext->iXDataStart;
			seqContext->iXDataStart = 0;
			MMEUpdateDisplayAddLabel(seqUID, "IK", 0, 0, 35, "");

			MMEUpdateDisplayAddLabel(seqUID, "(Skel) L.H. Target", 35, 1, 120, "Skeleton joint used as left hand's IK target, use this if the IK is not driven by FX");
			MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 230, 1, 200, "IKTargetNodeLeft", "Skeleton joint used as the left hand's IK target, use this if the IK is not driven by FX");
			MMEUpdateDisplayAddLabel(seqUID, "(Skel) R.H. Target", 370, 1, 120, "Skeleton joint used as the right hand's IK target, use this if the IK is not driven by FX");
			MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 565, 1, 200, "IKTargetNodeRight", "Skeleton joint used as the right hand's IK target, use this if the IK is not driven by FX");

			MMEUpdateDisplayAddLabel(seqUID, "(FX) Target Alias", 35, 0, 120, "Alias used as the IK target, a way to not directly use node names so that the same target can be mapped to different nodes in FX");
			MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, "", 230, 1, 200, "IKTarget", "Alias used as the IK target, not directly using node names here so that the same target can be mapped to different nodes in FX");

			MMEUpdateDisplayAddLabel(seqUID, "Holding Weapon", 35, 0, 120, "The right hand will be holding a weapon, and the left hand will be IK'd to a node on that weapon");
			MMEUpdateDisplayAddSimple(kMEFieldType_Check, "", 220, 1, 17, "RegisterWep", "The right hand will be holding a weapon, and the left hand will be IK'd to a node on that weapon");
			MMEUpdateDisplayAddLabel(seqUID, "Melee Mode", 165, 1, 80, "Use melee mode IK solver tweaks, for stuff like axes and bat'leths");
			MMEUpdateDisplayAddSimple(kMEFieldType_Check, "", 325, 1, 17, "IKMeleeMode", "Use melee mode IK solver tweaks, for stuff like axes and bat'leths");
			MMEUpdateDisplayAddLabel(seqUID, "Enable Sliding", 270, 1, 90, "Allow left hand to slide on weapon handle");
			MMEUpdateDisplayAddSimple(kMEFieldType_Check, "", 440, 1, 17, "EnableIKSliding", "Allow left hand to slide on weapon handle");
			MMEUpdateDisplayAddLabel(seqUID, "Disable Left Wrist", 385, 1, 110, "Don't modify the left wrist during weapon-based IK");
			MMEUpdateDisplayAddSimple(kMEFieldType_Check, "", 577, 1, 17, "DisableIKLeftWrist", "Don't modify the left wrist during weapon-based IK");
			MMEUpdateDisplayAddLabel(seqUID, "Disable Right Arm", 525, 1, 110, "Don't modify the right arm during weapon-based IK");
			MMEUpdateDisplayAddSimple(kMEFieldType_Check, "", 720, 1, 17, "DisableIKRightArm", "Don't modify the right arm during weapon-based IK");

			MMEUpdateDisplayAddLabel(seqUID, "Both Hands", 35, 0, 80, "Both hands have IK targets, useful for stuff like motorcycle handlebars");
			MMEUpdateDisplayAddSimple(kMEFieldType_Check, "", 220, 1, 17, "IKBothHands", "Both hands have IK targets, useful for stuff like motorcycle handlebars");

			seqContext->iXDataStart = oldDataStart;
		}

		//Ragdoll
		if (mmeRagdollCheckButton->state)
		{
			oldDataStart = seqContext->iXDataStart;
			seqContext->iXDataStart = 90;
			MEContextAddSimple(kMEFieldType_Check,	"Ragdoll",	"Ragdoll Enable",	"Enable Ragdoll");
			MEContextAddSimple(kMEFieldType_TextEntry, "RagdollFrame", "Ragdoll Frame", "Frame of animtrack that ragdoll starts on");
			MEContextAddSimple(kMEFieldType_TextEntry, "RagdollAdditionalGravity", "Ragdoll +Grvty", "Additional gravity to apply during ragdoll");
			seqContext->iXDataStart = oldDataStart;
		}
		
		//FX
		if (mmeFxCheckButton->state)
		{
			EARRAY_FOREACH_BEGIN(pNewSeq->eaDynMoveFxEvents, j);
			{
				MEFieldContext *fxContext;
				char fxUID[MAX_PATH], fxNum[5];

				itoa(j, fxNum, 10);
				strcpy(fxUID, seqUID);
				strcat(fxUID, "Fx");
				strcat(fxUID, fxNum);

				fxContext = MEContextPush(fxUID,
					pOldSeq && eaSize(&pOldSeq->eaDynMoveFxEvents) > j ? pOldSeq->eaDynMoveFxEvents[j] : NULL,
					pNewSeq->eaDynMoveFxEvents[j],
					parse_DynMoveFxEvent);

				oldDataStart = fxContext->iXDataStart;
				fxContext->iXDataStart = 0;
				MMEUpdateDisplayAddLabel(fxUID, "FX", 0, 0, 50, "");
				MMEUpdateDisplayAddLabel(fxUID, "Name", 40, 1, 40, "Name of the FX");
				MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry,	NULL,	175,	1,	250,	"Fx",		"Name of the FX"			);
				MMEUpdateDisplayAddLabel(fxUID, "Frame", 350, 1, 45, "Frame to trigger the FX");
				MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry,	NULL,	485,	1,	100,	"Frame",	"Frame to trigger the FX"	);
				MMEUpdateDisplayAddLabel(fxUID, "Message", 520, 1, 60, "Is the FX a message");
				MMEUpdateDisplayAddSimple(kMEFieldType_Check,		NULL,	670,	1,	20,	"Message",	"Is the FX a message"		);
				MMEUpdateDisplayAddButton(fxUID, "X", MMEDocButtonKillFx, pNewSeq->eaDynMoveFxEvents[j], -35, 1, 25, "Remove the FX");
				fxContext->iXDataStart = oldDataStart;

				MEContextPop(fxUID);
			}
			EARRAY_FOREACH_END;

			MMEUpdateDisplayAddButton(seqUID, "Add FX", MMEDocButtonAddFx, pNewSeq, -70, 0, 80, "Add a new FX");
		}

		if (mmeMatchBaseSkelAnimCheckButton->state)
		{
			EARRAY_FOREACH_BEGIN(pNewSeq->eaMatchBaseSkelAnim, j);
			{
				MEFieldContext *mbsaContext;
				char mbsaUID[MAX_PATH], mbsaNum[5];

				itoa(j, mbsaNum, 10);
				strcpy(mbsaUID, seqUID);
				strcat(mbsaUID, "MBSA");
				strcat(mbsaUID, mbsaNum);

				mbsaContext = MEContextPush(mbsaUID,
											pOldSeq && eaSize(&pOldSeq->eaMatchBaseSkelAnim) > j ? pOldSeq->eaMatchBaseSkelAnim[j] : NULL,
											pNewSeq->eaMatchBaseSkelAnim[j],
											parse_DynMoveMatchBaseSkelAnim);

				oldDataStart = mbsaContext->iXDataStart;
				mbsaContext->iXDataStart = 0;
				MMEUpdateDisplayAddLabel(mbsaUID, "Match B.S.A.",				0,		0,	75,									""																				);
				MMEUpdateDisplayAddLabel(mbsaUID, "Bone",						80,		1,	40,									"Name of the Bone"																);
				MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, NULL,			200,	1,	175,	"BoneName",					"Name of the Bone"																);
				MMEUpdateDisplayAddLabel(mbsaUID, "Blend-in (secs)",			300,	1,	90,									"How long does it take to blend from OFF to ON in seconds"						);
				MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, NULL,			475,	1,	100,	"BlendInTime",				"How long does it take to get to a full bend from off in seconds"				);
				MMEUpdateDisplayAddLabel(mbsaUID, "Blend-out (secs)",			500,	1,	95,									"How long does it take to blend from ON to OFF in seconds"						);
				MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, NULL,			685,	1,	100,	"BlendOutTime",				"How long does it take to blend from ON to OFF in seconds"						);
				MMEUpdateDisplayAddLabel(mbsaUID, "Play Blend Out During Move",	720,	1,	220,								"Normally, the blend-in is played, if this is set, the blend-out is played"		);
				MMEUpdateDisplayAddSimple(kMEFieldType_Check, NULL,				975,	1,	20,		"PlayBlendOutDuringMove",	"Normally, the blend-in is played, if this is set, the blend-out is played"		);
				MMEUpdateDisplayAddLabel(mbsaUID, "Start ON",					920,	1,	160,								"Normally, the blend value starts at OFF. When this is set, it'll start at ON"	);
				MMEUpdateDisplayAddSimple(kMEFieldType_Check, NULL,				1060,	1,	20,		"StartFullyBlended",		"Normally, the blend value starts at OFF. When this is set, it'll start at ON"	);
				MMEUpdateDisplayAddButton(mbsaUID, "X", MMEDocButtonKillMatchBaseSkelAnim, pNewSeq->eaMatchBaseSkelAnim[j], -35, 1, 25, "Remove the Match Base Skel. Anim. setting");
				mbsaContext->iXDataStart = oldDataStart;

				MEContextPop(mbsaUID);
			}
			EARRAY_FOREACH_END;

			MMEUpdateDisplayAddButton(seqUID, "Add Match Base Skel. Anim.", MMEDocButtonAddMatchBaseSkelAnim, pNewSeq, -70, 0, 200, "Add a new Match Base Skel. Anim. setting");
		}

		MEContextIndentLeft();
		MEContextIndentLeft();
		MEContextIndentLeft();
		MEContextIndentLeft();
		MEContextPop(seqUID);
	}
	EARRAY_FOREACH_END;

	MMEUpdateDisplayAddButton(baseUID, "Add Move Sequence", MMEDocButtonAddMoveSequence, pDoc, -150, 0, 185, "Add a new move sequence");

	MEContextAddSeparator("Line2");
	MEContextAddSpacer();
	MEContextAddSpacer();

	MEContextPop(baseUID);

	ui_ExpanderSetHeight(pDoc->pExpander, pContext->iYPos);
}

static void MMEUpdateDisplayAllFilesPanel(MoveMultiDoc *pDoc)
{
	MEFieldContext *pContext;
	EMPanel *pPanel = pDoc->pAllFilesPanel;
	char panelUID[MAX_PATH];

	//start the UI Context
	strcpy(panelUID, "AllFilesPanel");
	pContext = MEContextPush(panelUID, NULL, NULL, NULL);
	pContext->pUIContainer = emPanelGetUIContainer(pPanel);
	pContext->iXDataStart = 0;

	//modify selection
	MMEUpdateDisplayAddButton(panelUID, "Select All",    MMEDocButtonSelectAll,    S32_TO_PTR(-1),   0, 0, 87, "Select all moves");
	MMEUpdateDisplayAddButton(panelUID, "Select None",   MMEDocButtonSelectNone,   S32_TO_PTR(-1),  87, 1, 87, "Unselect all moves");
	MMEUpdateDisplayAddButton(panelUID, "Apply Filters", MMEDocButtonApplyFilters, S32_TO_PTR(-1), 174, 1, 87, "Select based on current filters");

	//alter fx
	MMEUpdateDisplayAddButton(panelUID, "Copy FX",   MMEDocButtonCopyFxSelected,   S32_TO_PTR(-1),   0, 0, 87, "Copy FX from selected moves");
	MMEUpdateDisplayAddButton(panelUID, "Paste FX",  MMEDocButtonPasteFxSelected,  S32_TO_PTR(-1),  87, 1, 87, "Paste FX to selected moves");
	MMEUpdateDisplayAddButton(panelUID, "Delete FX", MMEDocButtonDeleteFxSelected, S32_TO_PTR(-1), 174, 1, 87, "Delete FX from selected moves");

	//perform action
	MMEUpdateDisplayAddButton(panelUID, "Save",      MMEDocButtonSaveSelected,      S32_TO_PTR(-1),   0, 0, 87, "Save selected moves");
	MMEUpdateDisplayAddButton(panelUID, "Duplicate", MMEDocButtonDuplicateSelected, S32_TO_PTR(-1),  87, 1, 87, "Duplicate selected moves");
	MMEUpdateDisplayAddButton(panelUID, "Revert",    MMEDocButtonRevertSelected,    S32_TO_PTR(-1), 174, 1, 87, "Revert selected moves");

	MEContextAddSpacer();
	MEContextAddSpacer();

	MEContextPop(panelUID);

	emPanelSetHeight(pDoc->pAllFilesPanel, pContext->iYPos);
}

static void MMEUpdateDisplayFiltersPanel(MoveMultiDoc *pDoc)
{
	MEFieldContext *pContext;
	EMPanel *pPanel = pDoc->pFiltersPanel;
	char panelUID[MAX_PATH];

	//start the UI Context
	strcpy(panelUID, "FiltersPanel");
	pContext = MEContextPush(panelUID, pDoc, pDoc, parse_MoveMultiDoc);
	pContext->pUIContainer = emPanelGetUIContainer(pPanel);
	
	pContext->iXDataStart = 130;
	MEContextAddEnum(kMEFieldType_Combo, MMEMFilterOpEnum, "MoveFilterPresentOp", "Filters present op", "Operation to preform on selection list when a move passes the filters");
	MEContextAddEnum(kMEFieldType_Combo, MMEMFilterOpEnum, "MoveFilterAbsentOp",  "Filters absent op",  "Operation to preform on selection list when a move fails the filters");
	pContext->iXDataStart = 0;

	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaFilters, MMEMFilter, pFilter)
	{
		MEFieldContext *pFilterContext;
		MEFieldContextEntry *pContextEntry = NULL;
		char filterUID[MAX_PATH], filterId[5];

		itoa(ipFilterIndex, filterId, 10);
		strcpy(filterUID, panelUID);
		strcpy(filterUID, "Filter");
		strcat(filterUID, filterId);

		pFilterContext = MEContextPush(filterUID, pFilter, pFilter, parse_MMEMFilter);
		pFilterContext->iXDataStart = 50;

		if (pFilter->eMoveFilterType == mmeFilterType_Text) {
			pContextEntry = MEContextAddSimple(
				kMEFieldType_TextEntry, "MoveFilterText", "Text", "Text to search for when filtering"
				);
		} else if (pFilter->eMoveFilterType == mmeFilterType_Number) {
			pContextEntry = MEContextAddSimple(
				kMEFieldType_TextEntry, "MoveFilterValue", "Number", "Value to search for when filtering"
				);
		} else if (pFilter->eMoveFilterType == mmeFilterType_Bit) {
			pContextEntry = MEContextAddEnum(
				kMEFieldType_Combo, MMEMBitsEnum, "MoveFilterBit", "Bit", "Bit tag that must be enabled when filtering selection list"
				);
		}
		ANALYSIS_ASSUME(pContextEntry);
		MEContextEntryAddActionButton(pContextEntry, "X", NULL, MMEDocButtonKillFilter, pFilter, -1, "Remove filter");

		MEContextPop(filterUID);
	}
	FOR_EACH_END;

	MEContextAddButton("Add Text Filter",   NULL, MMEDocButtonAddTextFilter,   pDoc, "FiltersPanelAddTextFilterButton",   "", "Add a new text filter");
	MEContextAddButton("Add Number Filter", NULL, MMEDocButtonAddNumberFilter, pDoc, "FiltersPanelAddNumberFilterButton", "", "Add a new number filter");
	MEContextAddButton("Add Bit Filter",	NULL, MMEDocButtonAddBitFilter,	   pDoc, "FiltersPanelAddBitFilterButton",    "", "Add a new bit filter");

	MEContextAddSpacer();
	MEContextAddSpacer();

	MEContextPop(panelUID);

	emPanelSetHeight(pDoc->pFiltersPanel, pContext->iYPos);
}

static void MMEUpdateDisplayMovesPanel(MoveMultiDoc *pDoc)
{
	MEFieldContext *pContext;
	EMPanel *pPanel = pDoc->pMovesPanel;
	char panelUID[MAX_PATH];

	//start the UI Context
	strcpy(panelUID, "MovesPanel");
	pContext = MEContextPush(panelUID, NULL, NULL, NULL);
	pContext->pUIContainer = emPanelGetUIContainer(pPanel);
	pContext->iXDataStart = 0;

	//output the open file list based on open files
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaFileNames, const char, curFileName)
	{
		MEContextAddLabel(curFileName, curFileName, "File that moves belong to");

		//REWORK FOR MY INTERFACE + CHANGE THIS TO PASS ACTUAL DATA
		MMEUpdateDisplayAddButton(curFileName, "Select All",    MMEDocButtonSelectAll,    S32_TO_PTR(icurFileNameIndex),   0, 0,  87, "Select all moves for this file");
		MMEUpdateDisplayAddButton(curFileName, "Select None",   MMEDocButtonSelectNone,   S32_TO_PTR(icurFileNameIndex),  87, 1,  87, "Unselect all moves for this file");
		MMEUpdateDisplayAddButton(curFileName, "Apply Filters", MMEDocButtonApplyFilters, S32_TO_PTR(icurFileNameIndex), 174, 1,  87, "Select based on current filters");

		//REWORK FOR MY INTERFACE + CHANGE THIS TO PASS ACTUAL DATA (copying an outdated comment?)
		MMEUpdateDisplayAddButton(curFileName, "Copy FX",   MMEDocButtonCopyFxSelected,   S32_TO_PTR(icurFileNameIndex),   0, 0, 87, "Copy FX from selected moves in this file");
		MMEUpdateDisplayAddButton(curFileName, "Paste FX",  MMEDocButtonPasteFxSelected,  S32_TO_PTR(icurFileNameIndex),  87, 1, 87, "Paste FX to selected moves in this file");
		MMEUpdateDisplayAddButton(curFileName, "Delete FX", MMEDocButtonDeleteFxSelected, S32_TO_PTR(icurFileNameIndex), 174, 1, 87, "Delete FX from selected moves in this file");

		//REWORK FOR MY INTERFACE + CHANGE THIS TO PASS ACTUAL DATA
		MMEUpdateDisplayAddButton(curFileName, "Save",      MMEDocButtonSaveSelected,      S32_TO_PTR(icurFileNameIndex),   0, 0, 87, "Save selected moves for this file");
		MMEUpdateDisplayAddButton(curFileName, "Duplicate", MMEDocButtonDuplicateSelected, S32_TO_PTR(icurFileNameIndex),  87, 1, 87, "Duplicate selected moves for this file");
		MMEUpdateDisplayAddButton(curFileName, "Revert",    MMEDocButtonRevertSelected,    S32_TO_PTR(icurFileNameIndex), 174, 1, 87, "Revert selected moves for this file");

		FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaMoveDocs, MoveDoc, pMoveDoc)
		{
			if (pMoveDoc->pObject->pcFilename == curFileName)
			{
				MEFieldContext *pMoveDocContext;
				MEFieldContextEntry *pEntry;
				char nameButtonUID[MAX_PATH];
				char movename[MAX_PATH];
				char docUID[MAX_PATH], docNum[5];

				itoa(ipMoveDocIndex, docNum, 10);
				strcpy(docUID, panelUID);
				strcat(docUID, "Doc");
				strcat(docUID, docNum);

				pMoveDocContext = MEContextPush(docUID, pMoveDoc, pMoveDoc, parse_MoveDoc);
				pMoveDocContext->iXDataStart = 0;

				strcpy(nameButtonUID, docUID);
				strcat(nameButtonUID, "Button");

				if (StructCompare(parse_DynMove, pMoveDoc->pObject, pMoveDoc->pOrigObject, 0, 0, 0)) {
					sprintf(movename, "%s*", pMoveDoc->pObject->pcName);
				} else {
					strcpy(movename, pMoveDoc->pObject->pcName);
				}

				//add the panel elements
				MMEUpdateDisplayAddCheck(docUID, pMoveDoc->pObject->pcName, pMoveDoc->bIsSelected, MMEDocButtonSelected, pMoveDoc, 0, 0, 25, "Select this move");
				pMoveDocContext->iXDataStart = 25;
				MEContextStepBackUp();
				pEntry = MEContextAddButton(movename, NULL, MMEDocButtonName, pMoveDoc, allocAddString(nameButtonUID), "", pMoveDoc->pObject->pcName);
				ui_SetActive(&pEntry->pButton->widget, pMoveDoc->bIsSelected);
				MEContextEntryAddActionButton(pEntry, "X", NULL, MMEDocButtonKill, pMoveDoc, -1, "Close this move");

				MEContextPop(docUID);
			}
		}
		FOR_EACH_END;
		MEContextAddSpacer();
	}
	FOR_EACH_END;

	MEContextPop(panelUID);

	emPanelSetHeight(pDoc->pMovesPanel, pContext->iYPos);
}

static void MMEUpdateDisplayFxClipboardPanel(MoveMultiDoc *pDoc)
{
	MEFieldContext *pContext;
	EMPanel *pPanel = pDoc->pFxClipboardPanel;
	char panelUID[MAX_PATH];

	//start the UI Context
	strcpy(panelUID, "FxClipboardPanel");
	pContext = MEContextPush(panelUID, NULL, NULL, NULL);
	pContext->pUIContainer = emPanelGetUIContainer(pPanel);
	pContext->iXDataStart = 0;

	//output the clipboards contents
	EARRAY_FOREACH_BEGIN(pDoc->eaFxClipBoard, j);
	{
		MEFieldContext *fxContext;
		MEFieldContextEntry *pEntry;
		char fxUID[MAX_PATH], fxNum[5];

		itoa(j, fxNum, 10);
		strcpy(fxUID, "Fx");
		strcat(fxUID, fxNum);

		fxContext = MEContextPush(	fxUID,
									NULL,
									pDoc->eaFxClipBoard[j],
									parse_DynMoveFxEvent);

		MMEUpdateDisplayAddLabel(fxUID, "Name", 0, 0, 40, "Name of the FX");
		pEntry = MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, NULL, 45, 1, 150, "Fx", "Name of the FX");
		ui_SetActive(&pEntry->ppFields[0]->pUIText->widget, false);
		MMEUpdateDisplayAddLabel(fxUID, "Frame", 200, 1, 45, "Frame to trigger the FX");
		pEntry = MMEUpdateDisplayAddSimple(kMEFieldType_TextEntry, NULL, 250, 1, 45, "Frame", "Frame to trigger the FX");
		ui_SetActive(&pEntry->ppFields[0]->pUIText->widget, false);
		MMEUpdateDisplayAddLabel(fxUID, "Message", 300, 1, 60, "Is the FX a message");
		pEntry = MMEUpdateDisplayAddSimple(kMEFieldType_Check, NULL, 360, 1, 20, "Message", "Is the FX a message");
		ui_SetActive(&pEntry->ppFields[0]->pUICheck->widget, false);

		MEContextPop(fxUID);
	}
	EARRAY_FOREACH_END;

	MEContextPop(panelUID);

	emPanelSetHeight(pDoc->pFxClipboardPanel, pContext->iYPos);
}

static void MMEUpdateDisplayGraphsPanel(MoveMultiDoc *pDoc)
{
	MEFieldContext *pContext;
	EMPanel *pPanel = pDoc->pGraphsPanel;
	char panelUID[MAX_PATH];
	int oldXDataStart;

	//start the UI Context
	strcpy(panelUID, "GraphsPanel");
	pContext = MEContextPush(panelUID, pDoc, pDoc, parse_MoveMultiDoc);
	pContext->pUIContainer = emPanelGetUIContainer(pPanel);
	pContext->pChangedData = pDoc;

	MEContextAddSeparator("GraphsPanelLine1");
	MEContextAddSpacer();

	//refresh button
	oldXDataStart = pContext->iXDataStart;
	pContext->iXDataStart = 0;
	MEContextAddButton("Refresh", NULL, MMEDocButtonRefreshGraphs, pDoc, "GraphsPanelRefreshButton", NULL, "Refresh the list of graphs that use the selected move");
	pContext->iXDataStart = oldXDataStart;

	MEContextAddSpacer();
	MEContextAddSeparator("GraphsPanelLine2");
	MEContextAddSpacer();

	//selection of the visualize move
	pContext->iXDataStart = 40;
	pContext->cbChanged = MMEVisualizeMoveChangedCB;
	MEContextAddList(kMEFieldType_ValidatedTextEntry, &pDoc->eaMoveNames, "VisualizeMove", "Move", "Select the move to search for");

	MEContextAddSpacer();
	MEContextAddSeparator("GraphsPanelLine3");
	MEContextAddSpacer();

	MEContextAddLabel("GraphsPanelTagLine1", "Graphs that Reference this Move:", "List of graphs that reference this move");

	if (pDoc->pVisualizeMove)
	{
		DictionaryEArrayStruct *pAnimGraphArray = resDictGetEArrayStruct(hAnimGraphDict);
		FOR_EACH_IN_EARRAY(pAnimGraphArray->ppReferents, DynAnimGraph, pGraph)
		{
			int count = 0;
			FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pGraphNode) {
				FOR_EACH_IN_EARRAY(pGraphNode->eaMove, DynAnimGraphMove, pGraphMove) {
					DynMove *pMove = GET_REF(pGraphMove->hMove);
					if (pMove && strcmpi(pMove->pcName, pDoc->pVisualizeMove->pcName) == 0) {
						count++;
					}
				} FOR_EACH_END;
			} FOR_EACH_END;

			if (count > 0) {
				char gUID[MAX_PATH], iBuff[16];

				strcpy(gUID, panelUID);
				itoa(ipGraphIndex, iBuff, 10);
				strcat(gUID, "Graph");
				strcat(gUID, iBuff);

				itoa(count, iBuff, 10);
				strcat(iBuff, " x ");
				
				MEContextAddButton(pGraph->pcName, NULL, MMEDocButtonOpenGraph, pGraph, gUID, iBuff, "");
			}
		}
		FOR_EACH_END;
	}

	MEContextAddSpacer();
	MEContextAddSeparator("GraphsPanelLine4");
	MEContextAddSpacer();

	MEContextAddLabel("GraphsPanelTagLine2", "Charts that Reference this Move:", "List of charts that reference this move");

	if (pDoc->pVisualizeMove)
	{
		DictionaryEArrayStruct *pAnimChartArray = resDictGetEArrayStruct(hAnimChartDictLoadTime);
		FOR_EACH_IN_EARRAY(pAnimChartArray->ppReferents, DynAnimChartLoadTime, pChartLT)
		{
			int count = 0;
			FOR_EACH_IN_EARRAY(pChartLT->eaMoveRefs, DynAnimChartMoveRefLoadTime, pMoveRefLT) {
				DynMove *pMove = GET_REF(pMoveRefLT->hMove);
				if (pMove && strcmpi(pMove->pcName, pDoc->pVisualizeMove->pcName) == 0) {
					count++;
				}
			} FOR_EACH_END;

			if (count > 0) {
				char gUID[MAX_PATH], iBuff[16];

				strcpy(gUID, panelUID);
				itoa(ipChartLTIndex, iBuff, 10);
				strcat(gUID, "Chart");
				strcat(gUID, iBuff);

				itoa(count, iBuff, 10);
				strcat(iBuff, " x ");

				MEContextAddButton(pChartLT->pcName, NULL, MMEDocButtonOpenChart, pChartLT, gUID, iBuff, "");
			}
		}
		FOR_EACH_END;
	}

	MEContextAddSpacer();
	MEContextAddSeparator("GraphsPanelLine5");

	MEContextPop(panelUID);

	emPanelSetHeight(pPanel, pContext->iYPos);
}

static void MMEUpdateDisplaySearchPanel(MoveMultiDoc *pDoc)
{
	if (pDoc->pSearchPanel)
	{
		eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
		emPanelFree(pDoc->pSearchPanel);
	}
	pDoc->pSearchPanel = MMEInitSearchPanel(pDoc);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
}

static void MMEUpdateDisplayVisualizePanel(MoveMultiDoc *pDoc)
{
	MEFieldContext *pContext;
	EMPanel *pPanel = pDoc->pVisualizePanel;
	char panelUID[MAX_PATH];
	
	//make sure the visualize move is valid
	MMESetVisualizeMove(pDoc);
	MMESetVisualizeMoveSeq(pDoc);

	//start the UI Context
	strcpy(panelUID, "VisualizePanel");
	pContext = MEContextPush(panelUID, pDoc, pDoc, parse_MoveMultiDoc);
	pContext->pUIContainer = emPanelGetUIContainer(pPanel);
	pContext->pChangedData = pDoc;

	MEContextAddSeparator("VisualizePanelLine1");
	MEContextAddSpacer();

	pContext->iXDataStart = 0;
	MEContextAddButton(
		StructCompare(parse_DynMove, pDoc->pVisualizeMove, pDoc->pVisualizeMoveOrig, 0, 0, 0) ? "***Save*** (REQUIRED)" : "Save",
		NULL, MMEDocButtonSave, pDoc->pVisualizeMoveDoc ? pDoc->pVisualizeMoveDoc : NULL, "VisualizePanelSave", NULL, "Save the visualized move"
		);

	MEContextAddSpacer();
	MEContextAddSeparator("VisualizePanelLine2");
	MEContextAddSpacer();

	//selection of the visualize move
	pContext->iXDataStart = 40;
	pContext->cbChanged = MMEVisualizeMoveChangedCB;
	MEContextAddList(kMEFieldType_ValidatedTextEntry, &pDoc->eaMoveNames, "VisualizeMove", "Move", "Select the move to visualize");

	//move sequence selection
	if (pDoc->pVisualizeMove) 
	{
		eaDestroy(&pDoc->eaMoveSeqNames);
		FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pVisualizeMove->eaDynMoveSeqs, DynMoveSeq, pChkSeq)
			eaPush(&pDoc->eaMoveSeqNames, pChkSeq->pcDynMoveSeq);
		FOR_EACH_END;
		pContext->iXDataStart = 60;
		pContext->cbChanged = MMEVisualizeMoveSeqChangedCB;
		MEContextAddList(kMEFieldType_ValidatedTextEntry, &pDoc->eaMoveSeqNames, "VisualizeMoveSeq", "Sequence", "Select the move sequence to visualize");
	}

	if (pDoc->pVisualizeMoveSeq)
	{
		DynMoveSeq *pOldSeq = pDoc->pVisualizeMoveSeqOrig;
		DynMoveSeq *pNewSeq = pDoc->pVisualizeMoveSeq;

		MEContextAddSpacer();
		MEContextAddSeparator("VisualizePanelLine3");
		MEContextAddSpacer();

		pContext->iXDataStart = 40;
		pContext->cbChanged = MMEVisualizeFrameChangedCB;
		MEContextAddMinMax(kMEFieldType_SliderText, pDoc->fVisualizeFirstFrame, pDoc->fVisualizeLastFrame, 1.0, "VisualizeFrame", "Frame", "Frame of animation to display");
		pContext->iXDataStart = 0;
		pContext->cbChanged = NULL;
		MMEUpdateDisplayAddButton(panelUID, "Play",		MMEDocButtonPlay,	pDoc, 40, 0, 50, "Start playback of the animation");
		MMEUpdateDisplayAddButton(panelUID, "Pause",	MMEDocButtonPause,	pDoc, 90, 1, 50, "Pause playback of the animation");
		MMEUpdateDisplayAddLabel(panelUID, "Loop", 145, 1, 35, "Loop playback of the animation");
		MMEUpdateDisplayAddSimple(kMEFieldType_Check, "", 180, 1, 20, "VisualizeLoop", "Loop playback of the animation");

		MEContextAddSpacer();
		MEContextAddSeparator("VisualizePanelLine4");
		MEContextAddSpacer();

		{
			char frameLabel[MAX_PATH], frameNum[MAX_PATH];
			strcpy(frameLabel, "First Frame = ");
			itoa((int)(pDoc->fVisualizeFirstFrame), frameNum, 10);
			strcat(frameLabel, frameNum);
			if (uiFixDynMoveOffByOneError) {
				strcat(frameLabel, ", Last Frame (shown) = ");
				itoa((int)(pDoc->fVisualizeLastFrame-1), frameNum, 10);
				strcat(frameLabel, frameNum);
			} else {
				strcat(frameLabel, ", Last Frame (wall) = ");
				itoa((int)(pDoc->fVisualizeLastFrame), frameNum, 10);
				strcat(frameLabel, frameNum);
			}
			MEContextAddLabel("VisualizePanelFrameReport", frameLabel, "");
		}
		MEContextAddSpacer();

		EARRAY_FOREACH_BEGIN(pNewSeq->eaDynMoveFxEvents, i);
		{
			MEFieldContext *fxContext;
			char fxUID[MAX_PATH], fxUIDLabel[MAX_PATH], fxNum[5];

			itoa(i, fxNum, 10);
			strcpy(fxUID, panelUID);
			strcat(fxUID, "Fx");
			strcat(fxUID, fxNum);
			strcpy(fxUIDLabel, fxUID);
			strcat(fxUIDLabel, "Label");

			fxContext = MEContextPush(fxUID,
				pOldSeq && eaSize(&pOldSeq->eaDynMoveFxEvents) > i ? pOldSeq->eaDynMoveFxEvents[i] : NULL,
				pNewSeq->eaDynMoveFxEvents[i],
				parse_DynMoveFxEvent);

			fxContext->iXDataStart = 60;
			fxContext->cbChanged = MMEVisualizeFxChangedCB;
			MMEUpdateDisplayAddLabel(fxUID, "FX", 0, 0, 25, "");
			MMEUpdateDisplayAddButton(fxUID, "X", MMEDocButtonKillFx, pNewSeq->eaDynMoveFxEvents[i], 0, 1, 25, "Remove the FX");
			MEContextAddSimple(kMEFieldType_TextEntry, "Fx", "Name", "Name of the Fx or Fx Message");
			MEContextAddSimple(kMEFieldType_TextEntry, "Frame", "Frame", "Frame to trigger the FX");
			MEContextAddSimple(kMEFieldType_Check, "Message", "Message", "Is the FX a message?");
			MEContextAddSpacer();

			MEContextPop(fxUID);
		}
		EARRAY_FOREACH_END;

		MMEUpdateDisplayAddButton(panelUID, "Add FX", MMEDocButtonAddFx, pDoc->pVisualizeMoveSeq, 0, 0, 80, "Add a new FX");

		MEContextAddSpacer();
		MEContextAddSeparator("VisualizePanelLine5");
		MEContextAddSpacer();

		{
			DynFxManager *pFxManager = NULL;

			if (pDoc->costumeData.pGraphics &&
				pDoc->costumeData.pGraphics->costume.pSkel &&
				pDoc->costumeData.pGraphics->costume.pSkel->pFxManager) {
					pFxManager = pDoc->costumeData.pGraphics->costume.pSkel->pFxManager;
			} else if (
				pDoc->costumeData.pGraphics &&
				pDoc->costumeData.pGraphics->costume.pFxManager) {
					pFxManager = pDoc->costumeData.pGraphics->costume.pFxManager;
			}

			if (pFxManager) {
				EARRAY_FOREACH_BEGIN(pFxManager->eaMaintainedFx, i);
				{
					DynFxInfo *pFxInfo = GET_REF(pFxManager->eaMaintainedFx[i]->hInfo);	
					if (pFxInfo)
					{
						char manFxUID[MAX_PATH], manFxNum[5];

						itoa(i, manFxNum, 10);
						strcpy(manFxUID, panelUID);
						strcat(manFxUID, "ManFx");
						strcat(manFxUID, manFxNum);

						MEContextAddLabel(StructAllocString(manFxUID), pFxInfo->pcDynName, "Maintained FX on Current Costume");
					}
				}
				EARRAY_FOREACH_END;
			}
		}

		MEContextAddSpacer();
		MEContextAddSeparator("VisualizePanelLine6");
		MEContextAddSpacer();
	}

	MEContextPop(panelUID);

	emPanelSetHeight(pDoc->pVisualizePanel, pContext->iYPos);
}

static void MMEInitDisplay(EMEditor *pEditor, MoveDoc *pDoc)
{
	//expander setup
	{
		pDoc->pExpander = ui_ExpanderCreate(pDoc->pObject->pcName, 0);
		ui_ExpanderSetOpened(pDoc->pExpander, 1);
		
		pDoc->pExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", pDoc->pExpanderSkin->hNormal);
		ui_WidgetSkin(UI_WIDGET(pDoc->pExpander), pDoc->pExpanderSkin);
	}

	// Update the expander
	MMEUpdateDisplayWindow(pDoc);
}


//---------------------------------------------------------------------------------------------------
// Mostly Public Interface
//---------------------------------------------------------------------------------------------------

static int MMENameCompare(const MoveDoc **doc1, const MoveDoc **doc2)
{
	int fileCompare = stricmp((*doc1)->pObject->pcFilename, (*doc2)->pObject->pcFilename);
	if (fileCompare == 0)
		return stricmp((*doc1)->pObject->pcName, (*doc2)->pObject->pcName);
	return fileCompare;
}

void MMEOncePerFrameStart(MoveMultiDoc *pDoc)
{
	if (mmeUINeedsRefresh)
	{
		//build up a list of the different files currently open in the move multi-editor
		eaDestroy(&pDoc->eaFileNames);
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pMoveDoc)
		{
			bool bFoundIt = false;
			FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaFileNames, const char, curFilename)
			{
				if (pMoveDoc->pObject->pcFilename == curFilename)
					bFoundIt = true;
			}
			FOR_EACH_END;
			if (!bFoundIt)
				eaPush(&pDoc->eaFileNames, pMoveDoc->pObject->pcFilename);
		}
		FOR_EACH_END;

		//sort the file names last
		eaQSort(pDoc->eaFileNames, strCmp);

		//sort the move doc list
		eaDestroy(&pDoc->eaSortedMoveDocs);
		FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pMoveDoc)
			eaPush(&pDoc->eaSortedMoveDocs, pMoveDoc);
		FOR_EACH_END;
		eaQSort(pDoc->eaSortedMoveDocs, MMENameCompare);
		if (mmeAutoSortButton->state || pDoc->bOneTimeSortWindow)
			eaQSort(pDoc->eaMoveDocs, MMENameCompare);
		pDoc->bOneTimeSortWindow = false;

		//build up a list of move names
		eaDestroy(&pDoc->eaMoveNames);
		FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaSortedMoveDocs, MoveDoc, pMoveDoc)
			eaPush(&pDoc->eaMoveNames, pMoveDoc->pObject->pcName);
		FOR_EACH_END;

		//apply selection to the window
		FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaMoveDocs, MoveDoc, pMoveDoc)
		{
			ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pMoveDoc->pExpander);
			if (pMoveDoc->bIsSelected)
				ui_ExpanderGroupAddExpander(pDoc->pExpanderGroup, pMoveDoc->pExpander);
		}
		FOR_EACH_END;
	}
}

void MMEDoFx(MoveMultiDoc *pDoc, F32 fOldFrame, F32 fNewFrame)
{
	FOR_EACH_IN_EARRAY(pDoc->pVisualizeMoveSeq->eaDynMoveFxEvents, DynMoveFxEvent, pFx)
	{
		if (fOldFrame <= pFx->uiFrameTrigger && pFx->uiFrameTrigger < fNewFrame)
		{
			DynFxManager *pFxManager = NULL;
			if (pDoc->costumeData.pGraphics)
			{
				if (pDoc->costumeData.pGraphics->costume.pSkel &&
					pDoc->costumeData.pGraphics->costume.pSkel->pFxManager)
				{
					pFxManager = pDoc->costumeData.pGraphics->costume.pSkel->pFxManager;
				}
				else if (pDoc->costumeData.pGraphics->costume.pFxManager)
				{
					pFxManager = pDoc->costumeData.pGraphics->costume.pFxManager;
				}
			}

			if (pFxManager)
			{
				if (pFx->bMessage)
				{
					dynFxManBroadcastMessage(pFxManager, pFx->pcFx);
				}
				else if (pDoc->costumeData.pGraphics->costume.pSkel)
				{
					dynSkeletonQueueAnimationFx(pDoc->costumeData.pGraphics->costume.pSkel, pFxManager, pFx->pcFx);
				}
				else if (FALSE_THEN_SET(mmeWarnedNoSkeleton))
				{
					Errorf("Multi-move editor won't display graphics, missing costume or skeleton, please fix validation errors!");
				}
			}
		}
	}
	FOR_EACH_END;
}

void MMEOncePerFrameEnd(MoveMultiDoc *pDoc)
{
	if (pDoc->pVisualizeMoveSeq && pDoc->bVisualizePlaying)
	{
		F32 fOldFrame, fNewFrame;

		fOldFrame = pDoc->fVisualizeFrame - pDoc->fVisualizeFirstFrame;
		pDoc->fVisualizeFrame += 30.0*pDoc->pVisualizeMoveSeq->fSpeed*gGCLState.frameElapsedTime;
		fNewFrame = pDoc->fVisualizeFrame - pDoc->fVisualizeFirstFrame;
		
		if (pDoc->bVisualizeLoop || !pDoc->bVisualizeLoop && fOldFrame != pDoc->fVisualizeLastFrame - pDoc->fVisualizeFirstFrame)
			MMEDoFx(pDoc, fOldFrame, fNewFrame);

		if (pDoc->bVisualizeLoop)
		{
			while (pDoc->fVisualizeFrame > pDoc->fVisualizeLastFrame)
			{
				pDoc->fVisualizeFrame -= pDoc->fVisualizeLastFrame - pDoc->fVisualizeFirstFrame;
				fOldFrame = 0;
				fNewFrame = pDoc->fVisualizeFrame - pDoc->fVisualizeFirstFrame;
				MMEDoFx(pDoc, fOldFrame, fNewFrame);
			}
		}
		else if (pDoc->fVisualizeFrame > pDoc->fVisualizeLastFrame)
		{
			pDoc->fVisualizeFrame = pDoc->fVisualizeLastFrame;
		}

		fNewFrame = pDoc->fVisualizeFrame;
	}

	if (!pDoc->bVisualizeCostumePicked && (MoveMultiDoc*)emGetActiveEditorDoc() == pDoc) {
		AnimEditor_LastCostume(NULL, MMEGetCostumePickerData);
		pDoc->bVisualizeCostumePicked = true;
	}
	AnimEditor_DrawCostume(&pDoc->costumeData, gGCLState.frameElapsedTime);

	if (mmeUINeedsRefresh)
	{
		//refresh the panels
		MMEUpdateDisplayAllFilesPanel(pDoc);
		MMEUpdateDisplayFiltersPanel(pDoc);
		MMEUpdateDisplayMovesPanel(pDoc);
		MMEUpdateDisplayFxClipboardPanel(pDoc);
		MMEUpdateDisplayVisualizePanel(pDoc);
		MMEUpdateDisplayGraphsPanel(pDoc);
		MMEUpdateDisplaySearchPanel(pDoc);
	}
	else if (pDoc->bVisualizePlaying && pDoc->pVisualizeMoveSeq) {
		MMEUpdateDisplayVisualizePanel(pDoc);
	}
	
	mmeUINeedsRefresh = false;
	pDoc->emDoc.saved = true;
	FOR_EACH_IN_EARRAY(pDoc->eaMoveDocs, MoveDoc, pCheckDoc)
	{
		if (!pCheckDoc->emDoc.saved) {
			pDoc->emDoc.saved = false;
			break;
		}
	}
	FOR_EACH_END;

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pAllFilesPanel)))
		emPanelSetOpened(pDoc->pAllFilesPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pFiltersPanel)))
		emPanelSetOpened(pDoc->pFiltersPanel, true);
	
	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pMovesPanel)))
		emPanelSetOpened(pDoc->pMovesPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pFxClipboardPanel)))
		emPanelSetOpened(pDoc->pFxClipboardPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pVisualizePanel)))
		emPanelSetOpened(pDoc->pVisualizePanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pGraphsPanel)))
		emPanelSetOpened(pDoc->pGraphsPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pSearchPanel)))
		emPanelSetOpened(pDoc->pSearchPanel, true);
}

bool MMEOncePerFramePerDoc(MoveDoc *pDoc)
{
	//update the save
	if (pDoc->bIsSaving) {
		pDoc->saveStatus = MMESaveMove(pDoc,false);
		if (pDoc->saveStatus == EM_TASK_SUCCEEDED) {
			MMEPostSaveDoc(pDoc);
			pDoc->bIsSaving = false;
		} else if (pDoc->saveStatus == EM_TASK_FAILED) {
			pDoc->bIsSaving = false;
		}
	}

	//update the delete
	if (pDoc->bIsDeleting) {
		pDoc->deleteStatus = MMEDeleteMove(pDoc);
		if (pDoc->deleteStatus == EM_TASK_SUCCEEDED) {
			MMEPostDeleteDoc(pDoc);
			return true;
		} else if (pDoc->deleteStatus == EM_TASK_FAILED) {
			pDoc->bIsDeleting = false;
		}
	}

	//refresh the UI
	if (mmeUINeedsRefresh)
	{
		MMEUpdateDisplayWindow(pDoc);
		pDoc->emDoc.saved = pDoc->pOrigObject && (StructCompare(parse_DynMove, pDoc->pOrigObject, pDoc->pObject, 0, 0, 0) == 0);
	}

	return false;
}

static void debugHack(DynSkeleton *pSkeleton, const char *pcJointName, const char *pcAnimTrackName, F32 fAnimFrame)
{
	const DynBaseSkeleton *pBaseSkeleton = dynSkeletonGetBaseSkeleton(pSkeleton);
	DynAnimTrackHeader *pAnimTrackHeader = dynAnimTrackHeaderFind(pcAnimTrackName);
	DynAnimTrack *pAnimTrack = pAnimTrackHeader->pAnimTrack;
	Mat4 mRoot;

	dynNodeGetWorldSpaceMat(pSkeleton->pRoot, mRoot, false);

	if (pBaseSkeleton && pAnimTrack)
	{
		const DynNode* aNodes[32];
		S32 iNumNodes, iProcessNode;

		aNodes[0] = dynBaseSkeletonFindNode(pBaseSkeleton, allocFindString(pcJointName));
		iNumNodes = 1;

		if (aNodes[0])
		{
			DynNode *pSkelRootNode;
			//DynNode *pSkelMatchNode;

			DynTransform xRunning;
			//DynTransform xMatchingNode, xMatchingNodeInv;
			//Vec3 vPosMatchWS, vPosMatchLS;
			//Vec3 vPosDiff, vPosDiffInv;
			//Mat4 mMatchingNodeInv;

			while (aNodes[iNumNodes-1]->pParent)
			{
				aNodes[iNumNodes] = aNodes[iNumNodes-1]->pParent;
				iNumNodes++;
			}

			pSkelRootNode = dynSkeletonFindNodeNonConst(pSkeleton,aNodes[iNumNodes-1]->pcTag);
			dynNodeGetWorldSpaceTransform(pSkelRootNode, &xRunning);
			unitVec3(xRunning.vScale);

			for (iProcessNode = iNumNodes-1; iProcessNode >= 0; iProcessNode--)
			{
				DynTransform xBase, xBaseAnim;
				Vec3 vPos, vPosOld;
				Quat qRot;

				copyVec3(xRunning.vPos, vPosOld);

				if (!(aNodes[iProcessNode]->uiTransformFlags & ednRot))   unitQuat(xRunning.qRot);
				if (!(aNodes[iProcessNode]->uiTransformFlags & ednScale)) unitVec3(xRunning.vScale);
				if (!(aNodes[iProcessNode]->uiTransformFlags & ednTrans)) zeroVec3(xRunning.vPos);

				dynNodeGetLocalTransformInline(aNodes[iProcessNode], &xBase);
				dynBoneTrackUpdate(pAnimTrack, fAnimFrame, &xBaseAnim, aNodes[iProcessNode]->pcTag, &xBase, false);

				if (true) {
					printfColor(COLOR_BLUE|COLOR_BRIGHT, "%s (frame = %f)\n", aNodes[iProcessNode]->pcTag, fAnimFrame);
					printfColor(COLOR_BLUE, "axis : %f %f %f\n",
						xBaseAnim.qRot[0] / sqrt(1-xBaseAnim.qRot[3]*xBaseAnim.qRot[3]),
						xBaseAnim.qRot[1] / sqrt(1-xBaseAnim.qRot[3]*xBaseAnim.qRot[3]),
						xBaseAnim.qRot[2] / sqrt(1-xBaseAnim.qRot[3]*xBaseAnim.qRot[3])
						);
					printfColor(COLOR_BLUE, "angle: %f\n", 2 * acos(xBaseAnim.qRot[3]));
					printfColor(COLOR_BLUE, "trans: %f %f %f\n", vecParamsXYZ(xBaseAnim.vPos));
					printfColor(COLOR_BLUE, "scale: %f %f %f\n", vecParamsXYZ(xBaseAnim.vScale));
				}
				
				mulVecVec3(xBaseAnim.vPos, xRunning.vScale, xBaseAnim.vPos);
				quatRotateVec3Inline(xRunning.qRot, xBaseAnim.vPos, vPos);
				addVec3(vPos, xRunning.vPos, xRunning.vPos);
				quatMultiplyInline(xBaseAnim.qRot, xRunning.qRot, qRot);
				copyQuat(qRot, xRunning.qRot);

				if (true) {
					printfColor(COLOR_BLUE, "--------------------\n");
					printfColor(COLOR_BLUE, "axis : %f %f %f\n",
						xRunning.qRot[0] / sqrt(1-xRunning.qRot[3]*xRunning.qRot[3]),
						xRunning.qRot[1] / sqrt(1-xRunning.qRot[3]*xRunning.qRot[3]),
						xRunning.qRot[2] / sqrt(1-xRunning.qRot[3]*xRunning.qRot[3])
						);
					printfColor(COLOR_BLUE, "angle: %f\n", 2 * acos(xRunning.qRot[3]));
					printfColor(COLOR_BLUE, "trans: %f %f %f\n", vecParamsXYZ(xRunning.vPos));
					printfColor(COLOR_BLUE, "scale: %f %f %f\n", vecParamsXYZ(xRunning.vScale));
				}
				wlDrawLine3D_2(vPosOld, 0xFFFFFFFF, xRunning.vPos, 0xFFFFFFFF);
				//wl_state.drawAxesFromTransform_func(&xRunning, 5.f);
			}

			//pSkelMatchNode = dynSkeletonFindNodeNonConst(pSkeleton, pMatchJoint->pcName);
			//dynNodeGetWorldSpaceTransform(pSkelMatchNode, &xMatchingNode);
			//dynTransformInverse(&xMatchingNode, &xMatchingNodeInv);
			//dynTransformToMat4(&xMatchingNodeInv, mMatchingNodeInv);
			//dynNodeGetWorldSpacePos(pSkelMatchNode, vPosMatchWS);
			//dynNodeGetLocalPos(pSkelMatchNode, vPosMatchLS);

			//copyVec3(vPosMatchLS, pMatchJoint->vOriginalPos);

			//subVec3(xRunning.vPos, vPosMatchWS, vPosDiff);
			//scaleVec3(vPosDiff, pMatchJoint->fBlend, vPosDiff);
			//mulVecMat3(vPosDiff, mMatchingNodeInv, vPosDiffInv);
			//addVec3(vPosMatchLS, vPosDiffInv, vPosMatchLS);
			//dynNodeSetPos(pSkelMatchNode, vPosMatchLS);
		}
	}
}

void MMEDrawGhost(MoveMultiDoc *pDoc)
{
	if (!pDoc->costumeData.pGraphics)
		return;

	if (pDoc->costumeData.uiFrameCreated < dynDebugState.uiAnimDataResetFrame)
		AnimEditor_InitCostume(&pDoc->costumeData);

	if (pDoc->costumeData.pGraphics->costume.pSkel)
	{
		if (pDoc->pVisualizeMoveSeq)
		{
			if (pDoc->fVisualizeFrame == pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiLast) {
				dynSkeletonForceAnimationEx(pDoc->costumeData.pGraphics->costume.pSkel, pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->fVisualizeFrame-1 + pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiFirstFrame - 0.001, mmeUseBaseSkeleton, 0);
				//debugHack(pDoc->costumeData.pGraphics->pSkel, "WepR", pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->fVisualizeFrame-1 + pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiFirstFrame - 0.001f);
			} else {
				dynSkeletonForceAnimationEx(pDoc->costumeData.pGraphics->costume.pSkel, pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->fVisualizeFrame-1 + pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiFirstFrame, mmeUseBaseSkeleton, 0);
				//debugHack(pDoc->costumeData.pGraphics->pSkel, "WepR", pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->fVisualizeFrame-1 + pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiFirstFrame);
			}
		}

		AnimEditor_DrawCostumeGhosts(pDoc->costumeData.pGraphics, gGCLState.frameElapsedTime);
	}
	else if (FALSE_THEN_SET(mmeWarnedNoSkeleton))
	{
		Errorf("Multi-move editor won't display graphics, missing costume or skeleton, please fix validation errors!");
	}

	if (mmeDrawGrid)
	{
		MMEDrawGrid();
	}
}

void MMEGotFocus(void)
{
	mmeUINeedsRefresh = true;
	sndSetPlayerMatCallback(MMESndPlayerMatCB);
	sndSetVelCallback(MMESndPlayerVelCB);
}

void MMELostFocus(void)
{
	mmeUINeedsRefresh = false;
	sndSetPlayerMatCallback(gclSndGetPlayerMatCB);
	sndSetVelCallback(gclSndVelCB);
}

static void MMEPostCostumeChangeCB(void) 
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	assert(pDoc);

	if (pDoc->costumeData.pGraphics->costume.pSkel)
	{
		if (pDoc->pVisualizeMoveSeq)
		{
			if (pDoc->fVisualizeFrame == pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiLast) {
				dynSkeletonForceAnimationEx(pDoc->costumeData.pGraphics->costume.pSkel, pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->fVisualizeFrame-1 + pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiFirstFrame - 0.001, mmeUseBaseSkeleton, 0);
			} else {
				dynSkeletonForceAnimationEx(pDoc->costumeData.pGraphics->costume.pSkel, pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->fVisualizeFrame-1 + pDoc->pVisualizeMoveSeq->dynMoveAnimTrack.uiFirstFrame, mmeUseBaseSkeleton, 0);
			}
		}
	}
	else if (FALSE_THEN_SET(mmeWarnedNoSkeleton))
	{
		Errorf("Multi-move editor won't display graphics, missing costume or skeleton, please fix validation errors!");
	}

	mmeUINeedsRefresh = true;
}

static AnimEditor_CostumePickerData* MMEGetCostumePickerData(void)
{
	MoveMultiDoc *pDoc = (MoveMultiDoc*)emGetActiveEditorDoc();
	assert(pDoc);

	pDoc->costumeData.getCostumePickerData = MMEGetCostumePickerData;
	pDoc->costumeData.postCostumeChange = MMEPostCostumeChangeCB;

	return &pDoc->costumeData;
}

void MMEInitCustomToolbars(EMEditor *pEditor)
{
	EMToolbar* pToolbar;
	UILabel *pLabel;
	F32 fX;
	
	fX = 0.0;
	pToolbar = emToolbarCreate(50);

	pLabel = ui_LabelCreate("Sort", fX, 0);
	emToolbarAddChild(pToolbar, pLabel, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 5.0;

	mmeSortOnceButton = ui_ButtonCreate("One Time", fX, 0, MMESortOnceButton, NULL);
	mmeSortOnceButton->widget.widthUnit = UIUnitFitContents;
	emToolbarAddChild(pToolbar, mmeSortOnceButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeSortOnceButton)) + 5.0;

	mmeAutoSortButton = ui_CheckButtonCreate(fX, 0, "Auto", false);
	ui_CheckButtonSetToggledCallback(mmeAutoSortButton, MMEAutoSortToggle, NULL);
	emToolbarAddChild(pToolbar, mmeAutoSortButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeAutoSortButton)) + 5.0;

	emToolbarSetWidth(pToolbar, fX);
	eaPush(&pEditor->toolbars, pToolbar);

	fX = 0.0;
	pToolbar = emToolbarCreate(50);

	pLabel = ui_LabelCreate("Show", fX, 0);
	emToolbarAddChild(pToolbar, pLabel, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 5.0;

	mmeTrackButton = ui_CheckButtonCreate(fX, 0, "Anim Track", true);
	ui_CheckButtonSetToggledCallback(mmeTrackButton, MMEDisplayCheckboxToggle, NULL);
	emToolbarAddChild(pToolbar, mmeTrackButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeTrackButton)) + 5.0;

	mmeBoneCheckButton = ui_CheckButtonCreate(fX, 0, "Bone Mods", true);
	ui_CheckButtonSetToggledCallback(mmeBoneCheckButton, MMEDisplayCheckboxToggle, NULL);
	emToolbarAddChild(pToolbar, mmeBoneCheckButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeBoneCheckButton)) + 5.0;

	mmeMatchBaseSkelAnimCheckButton = ui_CheckButtonCreate(fX, 0, "Match Base Skel. Anim.", true);
	ui_CheckButtonSetToggledCallback(mmeMatchBaseSkelAnimCheckButton, MMEDisplayCheckboxToggle, NULL);
	emToolbarAddChild(pToolbar, mmeMatchBaseSkelAnimCheckButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeMatchBaseSkelAnimCheckButton)) + 5.0;

	mmePhysicsCheckButton = ui_CheckButtonCreate(fX, 0, "Physics", true);
	ui_CheckButtonSetToggledCallback(mmePhysicsCheckButton, MMEDisplayCheckboxToggle, NULL);
	emToolbarAddChild(pToolbar, mmePhysicsCheckButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmePhysicsCheckButton)) + 5.0;

	mmeFxCheckButton = ui_CheckButtonCreate(fX, 0, "FX", true);
	ui_CheckButtonSetToggledCallback(mmeFxCheckButton, MMEDisplayCheckboxToggle, NULL);
	emToolbarAddChild(pToolbar, mmeFxCheckButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeFxCheckButton)) + 5.0;

	mmeIkCheckButton = ui_CheckButtonCreate(fX, 0, "IK", true);
	ui_CheckButtonSetToggledCallback(mmeIkCheckButton, MMEDisplayCheckboxToggle, NULL);
	emToolbarAddChild(pToolbar, mmeIkCheckButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeIkCheckButton)) + 5.0;

	mmeRagdollCheckButton = ui_CheckButtonCreate(fX, 0, "Ragdoll", true);
	ui_CheckButtonSetToggledCallback(mmeRagdollCheckButton, MMEDisplayCheckboxToggle, NULL);
	emToolbarAddChild(pToolbar, mmeRagdollCheckButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeRagdollCheckButton)) + 5.0;

	emToolbarSetWidth(pToolbar, fX);
	eaPush(&pEditor->toolbars, pToolbar);

	fX = 0.0;
	pToolbar = emToolbarCreate(50);

	mmePickCostumeButton = ui_ButtonCreate("Pick Costume", fX, 0, AnimEditor_CostumePicker, MMEGetCostumePickerData);
	mmePickCostumeButton->widget.widthUnit = UIUnitFitContents;
	emToolbarAddChild(pToolbar, mmePickCostumeButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmePickCostumeButton)) + 5.0;

	mmeFitToScreenButton = ui_ButtonCreate("Fit to Screen", fX, 0, AnimEditor_UICenterCamera, MMEGetCostumePickerData);
	mmeFitToScreenButton->widget.widthUnit = UIUnitFitContents;
	emToolbarAddChild(pToolbar, mmeFitToScreenButton, false);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeFitToScreenButton)) + 5.0;

	//mmeAddFxButton = ui_ButtonCreate("Add Fx", fX, 0, AnimEditor_UIAddTestFX, MMEGetCostumePickerData);
	mmeAddFxButton = ui_ButtonCreateWithDownUp("Add Fx", fX, 0, AnimEditor_UIAddTestFX, MMEGetCostumePickerData, NULL, NULL);
	mmeAddFxButton->widget.widthUnit = UIUnitFitContents;
	emToolbarAddChild(pToolbar, mmeAddFxButton, false);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeAddFxButton)) + 5.0f;

	//mmeClearTestFxButton = ui_ButtonCreate("Clear Test Fx", fX, 0, AnimEditor_UIClearTestFX, MMEGetCostumePickerData);
	mmeClearTestFxButton = ui_ButtonCreateWithDownUp("Clear Fx", fX, 0, AnimEditor_UIClearTestFX, MMEGetCostumePickerData, NULL, NULL);
	mmeClearTestFxButton->widget.widthUnit = UIUnitFitContents;
	emToolbarAddChild(pToolbar, mmeClearTestFxButton, false);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeClearTestFxButton)) + 5.0f;

	mmeDrawGridCheckButton = ui_CheckButtonCreate(fX, 0, "Draw Grid", true);
	ui_CheckButtonSetToggledCallback(mmeDrawGridCheckButton, MMEDrawGridCheckboxToggle, NULL);
	emToolbarAddChild(pToolbar, mmeDrawGridCheckButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeDrawGridCheckButton)) + 5.0;

	mmeUseBaseSkeletonCheckButton = ui_CheckButtonCreate(fX, 0, "Use Base Skeleton", true);
	ui_CheckButtonSetToggledCallback(mmeUseBaseSkeletonCheckButton, MMEUseBaseSkeletonCheckboxToggle, NULL);
	emToolbarAddChild(pToolbar, mmeUseBaseSkeletonCheckButton, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(mmeUseBaseSkeletonCheckButton)) + 5.0;

	emToolbarSetWidth(pToolbar, fX);
	eaPush(&pEditor->toolbars, pToolbar);
}

static MoveDoc* MMEInitDoc(DynMove *pMove, MoveMultiDoc *pParent, bool bCreated)
{
	// Initialize the structure
	MoveDoc *pDoc = (MoveDoc*)calloc(1,sizeof(MoveDoc));
	pDoc->pParent = pParent;
	pDoc->bIsSaving = false;
	pDoc->bIsSelected = true;

	// Fill in the def data
	if (bCreated)
	{
		char newName[MAX_PATH];
		pDoc->pObject = StructCreate(parse_DynMove);
		assert(pDoc->pObject);
		sprintf(newName,"%s_%d",MME_NEWMOVENAME,++mmeNewNameCount);
		pDoc->pObject->pcName = StructAllocString(newName);
		sprintf(newName,"File: %s_%d.move",MME_NEWMOVENAME,++mmeNewNameCount);
		pDoc->pObject->pcScope = StructAllocString(newName);
		sprintf(newName,"");
		pDoc->pObject->pcUserScope = allocAddFilename(newName);
		sprintf(newName,"%s_%d",MME_NEWMOVENAME,++mmeNewNameCount);
		pDoc->pObject->pcUserFilename = allocAddFilename(newName);
		sprintf(newName,"dyn/move/%s_%d.move",pDoc->pObject->pcName,mmeNewNameCount);
		pDoc->pObject->pcFilename = allocAddFilename(newName);
	}
	else
	{
		pDoc->pObject = StructClone(parse_DynMove, pMove);
		assert(pDoc->pObject);
		resFixFilename(hDynMoveDict, pDoc->pObject->pcName, pDoc->pObject);

		pDoc->pOrigObject = StructClone(parse_DynMove, pDoc->pObject);

		sprintf(pDoc->emDoc.orig_doc_name, "%s", pDoc->pOrigObject->pcName);
		emDocAssocFile(&pDoc->emDoc,    pDoc->pObject->pcFilename);
		emDocAssocFile(&pParent->emDoc, pDoc->pObject->pcFilename);
	}

	// Set up the document name
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pObject->pcName);
	sprintf(pDoc->emDoc.doc_type, "%s", DYNMOVE_TYPENAME);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->emDoc.doc_name);

	// Set up the undo stack
	pDoc->pNextUndoObject = StructClone(parse_DynMove, pDoc->pObject);

	return pDoc;
}

static void MMEBindToParent(MoveDoc *pDoc, MoveMultiDoc *pParent)
{
	//link editor
	pDoc->emDoc.editor = pParent->emDoc.editor;

	//link ui elements
	ui_ExpanderGroupAddExpander(pParent->pExpanderGroup, pDoc->pExpander);
	pDoc->emDoc.primary_ui_window = pParent->pMainWindow;
	eaPush(&pDoc->emDoc.ui_windows, pParent->pMainWindow);

	//add the doc to our list
	eaPush(&pParent->eaMoveDocs, pDoc);

	//show the main window if it's not open
	if (eaSize(&pParent->eaMoveDocs) == 1)
		ui_WindowPresent(pParent->pMainWindow);
}

MoveDoc* MMEOpenMove(EMEditor *pEditor, MoveMultiDoc *pParent, const char *pcName)
{
	MoveDoc *pDoc = NULL;
	DynMove *pMove = NULL;
	bool bCreated = false;

	if (pcName && resIsEditingVersionAvailable(hDynMoveDict, pcName)) {
		// Simply open the object since it is in the dictionary
		pMove = RefSystem_ReferentFromString(hDynMoveDict, pcName);
	} else if (pcName) {
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(hDynMoveDict, true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(hDynMoveDict, pcName);
	} else {
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pMove || bCreated) {
		//standard creation
		if (pMove) MMEFixupNoAstData(pMove);
		pDoc = MMEInitDoc(pMove, pParent, bCreated);
		MMEInitDisplay(pEditor, pDoc);
		resFixFilename(hDynMoveDict, pDoc->pObject->pcName, pDoc->pObject);
		MMEBindToParent(pDoc, pParent);
	}

	return pDoc;
}

static void MMEDeleteOldDirectoryIfEmpty(MoveDoc *pDoc)
{
	char dir[MAX_PATH], out_dir[MAX_PATH];
	char cmd[MAX_PATH];
	
	sprintf(dir, "/dyn/move/%s", pDoc->bIsDeleting ? NULL_TO_EMPTY(pDoc->pcDeleteScope) : NULL_TO_EMPTY(pDoc->pOrigObject->pcUserScope));
	
	fileLocateWrite(dir, out_dir);
	if (dirExists(out_dir))
	{
		backSlashes(out_dir);
		sprintf(cmd, "rd %s", out_dir);
		system(cmd);
	}
}

void MMERevertDoc(MoveDoc *pDoc)
{
	DynMove *pMove;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	//if we're reverting due to save, remove the old directory if it's empty post scope change
	if (pDoc->pOrigObject && pDoc->pObject->pcScope != pDoc->pOrigObject->pcScope)
		MMEDeleteOldDirectoryIfEmpty(pDoc);

	pMove = RefSystem_ReferentFromString(hDynMoveDict, pDoc->emDoc.orig_doc_name);
	if (pMove) {
		// Revert the def
		StructDestroy(parse_DynMove, pDoc->pObject);
		StructDestroy(parse_DynMove, pDoc->pOrigObject);

		pDoc->pObject = StructClone(parse_DynMove, pMove);
		resFixFilename(hDynMoveDict, pDoc->pObject->pcName, pDoc->pObject);

		pDoc->pOrigObject = StructClone(parse_DynMove, pDoc->pObject);

		MMESetVisualizeMove(pDoc->pParent);
		MMESetVisualizeMoveSeq(pDoc->pParent);

		// Clear the undo stack on revert
		StructDestroy(parse_DynMove, pDoc->pNextUndoObject);
		pDoc->pNextUndoObject = StructClone(parse_DynMove, pDoc->pObject);
	}

	//tweak ui extras
	EditUndoStackClear(pDoc->pParent->emDoc.edit_undo_stack);
	ui_ExpanderSetName(pDoc->pExpander, pDoc->pObject->pcName);
	mmeUINeedsRefresh = true;
}

void MMEDuplicateDoc(MoveDoc *pDoc)
{
	MoveDoc *pOrigDoc = pDoc, *pNewDoc;
	MoveMultiDoc *pEMDoc = pOrigDoc->pParent;
	DynMove *pMove = StructClone(parse_DynMove, pOrigDoc->pObject);
	char newName[MAX_PATH];

	sprintf(newName,"%sDup%d",pMove->pcName,++mmeNewNameCount);
	pMove->pcName = StructAllocString(newName);

	pNewDoc = MMEInitDoc(pMove, pEMDoc, false);
	MMEInitDisplay(pEMDoc->emDoc.editor, pNewDoc);
	resFixFilename(hDynMoveDict, pNewDoc->pObject->pcName, pNewDoc->pObject);
	MMEBindToParent(pNewDoc, pEMDoc);

	mmeUINeedsRefresh = true;
}

void MMECloseMove(MoveDoc *pDoc)
{
	char contextName[MAX_PATH], contextId[5];

	//remove from the parent
	MoveMultiDoc *pParent = pDoc->pParent;
	int i = eaFind(&pParent->eaMoveDocs, pDoc);
	int j = eaFind(&pParent->eaSortedMoveDocs, pDoc);
	ui_ExpanderGroupRemoveExpander(pParent->pExpanderGroup, pDoc->pExpander);
	eaRemove(&pParent->eaMoveDocs, i);
	eaRemove(&pParent->eaSortedMoveDocs, j);

	//remove ref to parent's window
	pDoc->emDoc.primary_ui_window = NULL;
	eaClear(&pDoc->emDoc.ui_windows);

	//clear the last context
	itoa(eaSize(&pDoc->pParent->eaMoveDocs), contextId, 10); //no -1 to size since eaRemove above
	strcpy(contextName, "Base");
	strcat(contextName, contextId);
	MEContextDestroyByName(contextName);

	//unlink the parent
	pDoc->pParent = NULL;

	//update the doc's file associations (for gimme)
	emDocRemoveAllFiles(&pDoc->emDoc, false);
	emDocRemoveAllFiles(&pParent->emDoc, false);
	FOR_EACH_IN_EARRAY(pParent->eaMoveDocs, MoveDoc, pChkDoc) {
		if (pChkDoc->bIsSaving)
			emDocAssocFile(&pParent->emDoc, pChkDoc->pObject->pcFilename);
		else if (!pChkDoc->bIsDeleting && SAFE_MEMBER(pChkDoc->pOrigObject,pcFilename))
			emDocAssocFile(&pParent->emDoc, pChkDoc->pOrigObject->pcFilename);
	}
	FOR_EACH_END;

	//clear the ui elements (for this doc only)
	//ui_WidgetQueueFree(UI_WIDGET(pDoc->pExpander));
	//pDoc->pExpander = NULL;

	// Free the objects
	StructDestroy(parse_DynMove, pDoc->pObject);
	if (pDoc->pOrigObject) {
		StructDestroy(parse_DynMove, pDoc->pOrigObject);
	}
	StructDestroy(parse_DynMove, pDoc->pNextUndoObject);

	//free the doc
	SAFE_FREE(pDoc);

	//close the parent if this was the only open move doc
	if (eaSize(&pParent->eaMoveDocs) == 0) {
		emCloseDoc(&pParent->emDoc);
	} else {
		MMESetVisualizeMove(pParent);
		MMESetVisualizeMoveSeq(pParent);
	}
}

void MMEDeleteDoc(MoveDoc *pDoc)
{
	if (pDoc && !MEContextExists() && !pDoc->bIsSaving && !pDoc->bIsDeleting)
	{
		if (emDocIsEditable(&pDoc->emDoc, false)) {
			pDoc->deleteStatus = MMEDeleteMove(pDoc);
			if (pDoc->deleteStatus == EM_TASK_SUCCEEDED) {
				pDoc->bIsDeleting = true;
				MMEPostDeleteDoc(pDoc);
			} else if (pDoc->deleteStatus == EM_TASK_INPROGRESS) {
				pDoc->bIsDeleting = true;
			}
		}
	}
}

void MMESaveDoc(MoveDoc *pDoc)
{
	if (pDoc && !MEContextExists() && !pDoc->bIsSaving && !pDoc->bIsDeleting)
	{
		if (emDocIsEditable(&pDoc->emDoc, false))
		{
			pDoc->saveStatus = MMESaveMove(pDoc,false);
			if (pDoc->saveStatus == EM_TASK_SUCCEEDED) {
				MMEPostSaveDoc(pDoc);
			} else if (pDoc->saveStatus == EM_TASK_INPROGRESS) {
				pDoc->bIsSaving = true;
			}
		}
	}
}

static void MMEPostDeleteDoc(MoveDoc *pDoc)
{
	emStatusPrintf("Doc successfully deleted");

	MMEDeleteOldDirectoryIfEmpty(pDoc);
	MMECloseMove(pDoc);
	mmeUINeedsRefresh = true;
}

static void MMEPostSaveDoc(MoveDoc *pDoc)
{
	DynMove *pMove;

	//if we're reverting due to save, remove the old directory if it's empty post scope change
	if (pDoc->pOrigObject && pDoc->pObject->pcScope != pDoc->pOrigObject->pcScope)
		MMEDeleteOldDirectoryIfEmpty(pDoc);

	//rebuild basic data
	pMove = (DynMove*)RefSystem_ReferentFromString(hDynMoveDict, pDoc->emDoc.doc_name);
	if (pMove)
	{
		MMEFixupNoAstData(pMove);

		// Revert the def
		StructDestroy(parse_DynMove, pDoc->pObject);
		StructDestroy(parse_DynMove, pDoc->pOrigObject);

		pDoc->pObject = StructClone(parse_DynMove, pMove);
		resFixFilename(hDynMoveDict, pDoc->pObject->pcName, pDoc->pObject);

		pDoc->pOrigObject = StructClone(parse_DynMove, pDoc->pObject);

		MMESetVisualizeMove(pDoc->pParent);
		MMESetVisualizeMoveSeq(pDoc->pParent);

		// Clear the undo stack on revert
		StructDestroy(parse_DynMove, pDoc->pNextUndoObject);
		pDoc->pNextUndoObject = StructClone(parse_DynMove, pDoc->pObject);
	}

	emStatusPrintf("Doc \"%s\" successfully saved", pDoc->pObject->pcName);

	//tweak ui extras
	EditUndoStackClear(pDoc->pParent->emDoc.edit_undo_stack);
	ui_ExpanderSetName(pDoc->pExpander, pDoc->pObject->pcName);
	mmeUINeedsRefresh = true;
}

static EMTaskStatus MMEDeleteMove(MoveDoc *pDoc)
{
	EMResourceState eState;
	DynMove *pMove, *pOrigMove;
	const char *pcName, *pcOrigName;

	//grab refs to the data
	pMove     = pDoc->pObject;
	pOrigMove = pDoc->pOrigObject;

	//grab refs to the names
	pcName     = pMove->pcName;
	if (pOrigMove) {
		pDoc->pcDeleteScope = pOrigMove->pcUserScope;
		pcOrigName = pOrigMove->pcName;
	} else {
		pDoc->pcDeleteScope = NULL;
		pcOrigName = NULL;
		return EM_TASK_SUCCEEDED;
	}

	//check the behavior for success, progress, or fail
	eState = emGetResourceState(pDoc->emDoc.editor, pcOrigName);
	switch (eState)
	{
		xcase EMRES_STATE_DELETE_SUCCEEDED:
			emSetResourceState(pDoc->emDoc.editor, pcOrigName, EMRES_STATE_NONE);
			return EM_TASK_SUCCEEDED;
		xcase EMRES_STATE_DELETE_FAILED:
			emSetResourceState(pDoc->emDoc.editor, pcOrigName, EMRES_STATE_NONE);
			return EM_TASK_FAILED;
		xcase EMRES_STATE_LOCK_FAILED:
			emSetResourceState(pDoc->emDoc.editor, pcOrigName, EMRES_STATE_NONE);
			return EM_TASK_FAILED;
		xcase EMRES_STATE_LOCKING_FOR_DELETE:
			return EM_TASK_INPROGRESS;
		xcase EMRES_STATE_DELETING:
			return EM_TASK_INPROGRESS;
		//all other cases continue
	}

	// Locking
	if (!resGetLockOwner(hDynMoveDict, pcOrigName)) {
		emSetResourceState(pDoc->emDoc.editor, pcOrigName, EMRES_STATE_LOCKING_FOR_DELETE);
		resRequestLockResource(hDynMoveDict, pcOrigName, pMove);
		return EM_TASK_INPROGRESS;
	}

	// If there is an original object explicitly delete it
	if (pOrigMove) {
		emSetResourceStateWithData(pDoc->emDoc.editor, pcOrigName, EMRES_STATE_DELETING, pOrigMove);
		resRequestSaveResource(hDynMoveDict, pcOrigName, NULL);
	}

	return EM_TASK_INPROGRESS;
}

static EMTaskStatus MMESaveMove(MoveDoc *pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	DynMove *pMove, *pOrigMove, *pMoveCopy;
	const char *pcName, *pcOrigName;

	//grab refs to the data
	pMove     = pDoc->pObject;
	pOrigMove = pDoc->pOrigObject;

	//grab refs to the names
	pcName     = pMove->pcName;
	if (pOrigMove)
		pcOrigName = pOrigMove->pcName;
	else
		pcOrigName = NULL;

	//check the behavior for success, progress, or fail
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		return status;
	}

	// Make a copy of the object we're saving
	pMoveCopy = StructClone(parse_DynMove, pDoc->pObject);

	// Perform validation
	if (!dynMoveVerify(pMoveCopy, 1)) {
		StructDestroy(parse_DynMove, pMoveCopy);
		return EM_TASK_FAILED;
	}

	// Locking
	if (!resGetLockOwner(hDynMoveDict, pcName)) {
		emSetResourceState(pDoc->emDoc.editor, pcName, EMRES_STATE_LOCKING_FOR_SAVE);
		resRequestLockResource(hDynMoveDict, pcName, pMove);
		StructDestroy(parse_DynMove, pMoveCopy);
		return EM_TASK_INPROGRESS;
	}

	// If there is an original object and the name changed, 
	// need to explicitly delete the old object when saving
	if (pOrigMove && stricmp(pcOrigName, pcName) != 0) {
		emSetResourceStateWithData(pDoc->emDoc.editor, pcOrigName, EMRES_STATE_DELETING, pOrigMove);
		resRequestSaveResource(hDynMoveDict, pcOrigName, NULL);
	}

	//preform the save
	emSetResourceState(pDoc->emDoc.editor, pcName, EMRES_STATE_SAVING);
	resRequestSaveResource(hDynMoveDict, pcName, pMoveCopy);

	//mod file associations
	emDocRemoveAllFiles(&pDoc->emDoc, false);
	emDocAssocFile(&pDoc->emDoc, pDoc->pObject->pcFilename);

	emDocRemoveAllFiles(&pDoc->pParent->emDoc, false);
	FOR_EACH_IN_EARRAY(pDoc->pParent->eaMoveDocs, MoveDoc, pChkDoc) {
		if (pChkDoc->bIsSaving)
			emDocAssocFile(&pDoc->pParent->emDoc, pChkDoc->pObject->pcFilename);
		else if (SAFE_MEMBER(pChkDoc->pOrigObject,pcFilename))
			emDocAssocFile(&pDoc->pParent->emDoc, pChkDoc->pOrigObject->pcFilename);
	}
	FOR_EACH_END;

	return EM_TASK_INPROGRESS;
}

#endif
