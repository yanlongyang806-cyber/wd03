#include "FXParticleEditor.h"

#include "dynNode.h"

#ifndef NO_EDITORS

#include "Prefs.h"
#include "StringCache.h"
#include "gimmeDLLWrapper.h"
#include "cmdparse.h"
#include "GraphicsLib.h"
#include "quat.h"

#include "WorldGrid.h"
#include "ObjectLibrary.h"
#include "EditLibGizmos.h"
#include "UIAutoWidget.h"
#include "UIGraph.h"
#include "EditLibUIUtil.h"

#include "dynFxInfo.h"
#include "dynFxFastParticle.h"
#include "FxParticleEditor_h_ast.h"
#include "inputLib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMTaskStatus partEdSave(ParticleDoc* pDoc);

/*typedef struct ParticleEditorUI
{
	ParticleEditorPanel preview;
	ParticleEditorPanel texture;
	ParticleEditorPanel emission;
	ParticleEditorPanel movement;

	// graphs
	ParticleEditorPanel graphs[PART_NUM_GRAPHS];
	ParticleEditorPanel streakScale;
/*	ParticleEditorPanel hue;
	ParticleEditorPanel saturation;
	ParticleEditorPanel value;
	ParticleEditorPanel alpha;
	ParticleEditorPanel scaleX;
	ParticleEditorPanel scaleY;
	ParticleEditorPanel rotation;
	ParticleEditorPanel spin;*/
//} ParticleEditorUI;

EMEditor particle_editor;
//ParticleEditorUI particle_ui;
ParticleDoc** eaDocs;

GeneralParticlePreviewSettings genPreviewSettings;


typedef struct FXParticleEditorGraphChangedData
{
	ParticleDoc* pDoc;
	size_t szOffset;
} FXParticleEditorGraphChangedData;

static const char* ppcGraphNames[PART_NUM_GRAPHS] = 
{
	"Hue",
	"Saturation",
	"Value",
	"Alpha",
	"ScaleX",
	"ScaleY",
	"Rotation",
	"Spin"
};

static UIGraphPane* pGraphPanes[PART_NUM_GRAPHS];
static U32 aGraphSelectedPoints[PART_NUM_GRAPHS];

static void partEdRefreshPanelSize(ParticleEditorPanel *panel);
static void partEdReAddGraphs(SA_PARAM_NN_VALID ParticleDoc *pDoc);
static void partEdDataChangedFromGraph(UIGraphPane* pPane, FXParticleEditorGraphChangedData* pData);

#define GET_ACTIVE_PARTICLE_DOC(doc_name) ParticleDoc* doc_name = emGetActiveEditorDoc()?((ParticleDoc*)emGetActiveEditorDoc()):NULL

AUTO_COMMAND ACMD_LIST(ParticleEditorCmdList);
void partEdUndo(ACMD_IGNORE ParticleDoc* pDoc)
{
	emUndo(&pDoc->base);
	pDoc->bRecalculateSet = true;
}

AUTO_COMMAND ACMD_LIST(ParticleEditorCmdList);
void partEdRedo(ACMD_IGNORE ParticleDoc* pDoc)
{
	emRedo(&pDoc->base);
	pDoc->bRecalculateSet = true;
}

static void partEdBackupInfo(ParticleDoc* pDoc)
{
	if (pDoc->pBackup)
		StructDeInit(parse_DynFxFastParticleInfo, pDoc->pBackup);
	else
		pDoc->pBackup = StructAlloc(parse_DynFxFastParticleInfo);
	StructCopyFields(parse_DynFxFastParticleInfo, pDoc->pInfo, pDoc->pBackup, 0, 0);
}

void partEdUndoChanged(EditorObject* pDummy, DynFxFastParticleInfo* pNewInfo)
{
	ParticleDoc* pDoc = (ParticleDoc*)emGetActiveEditorDoc();
	if (pDoc)
	{
		partEdBackupInfo(pDoc);
		genPreviewSettings.bRebuildUI = true;
		emSetDocUnsaved(&pDoc->base, true);
	}
}

/*
AUTO_COMMAND ACMD_LIST(ParticleEditorCmdList);
void partEdUndo(ACMD_IGNORE ParticleDoc* pDoc)
{
	GET_ACTIVE_PARTICLE_DOC;
	EditUndoLast(pDoc->pUndoStack);
}

AUTO_COMMAND ACMD_LIST(ParticleEditorCmdList);
void partEdRedo(ACMD_IGNORE ParticleDoc* pDoc)
{
	GET_ACTIVE_PARTICLE_DOC;
	EditRedoLast(pDoc->pUndoStack);
}
*/

AUTO_COMMAND;
void partEdSaveAll(void)
{
	int i;
	for (i = 0; i < eaSize(&particle_editor.open_docs); i++)
	{
		ParticleDoc *pDoc = (ParticleDoc*) particle_editor.open_docs[i];
		if (pDoc->pcLoadedFilename)
			emSaveDoc(particle_editor.open_docs[i]);
	}
}

AUTO_COMMAND;
void partEdCenterCamera(void)
{
	if (emIsFreecamActive())
	{
		Vec3 vTarget;
		Vec3 vEye;
		dynNodeGetWorldSpacePos(genPreviewSettings.pTestNode, vTarget);
		copyVec3(vTarget, vEye);
		particle_editor.camera->camdist = 20;
		vEye[2] -= 20.0f;
		gfxCameraControllerLookAt(vEye, vTarget, upvec);
	}
	else
	{
		Vec3 vTarget;
		dynNodeGetWorldSpacePos(genPreviewSettings.pTestNode, vTarget);

		gfxCameraControllerSetTarget(particle_editor.camera, vTarget);
		particle_editor.camera->camdist = 20;
	}
}

static void partEdPreviewRecenterObject( UIButton* button, void* user_data)
{
	partEdCenterCamera();
}

AUTO_COMMAND;
void partEdClickedReset(void)
{
	genPreviewSettings.bResetSet = true;
}

static void partEdResetSet( UIButton* button, void* user_data)
{
	partEdClickedReset();
}


AUTO_COMMAND;
void partEdDropTestNode(void)
{
	Mat4 mat;
	Vec3 vNewPos;
	Vec3 vOffset;
	GfxCameraController* pCam = gfxGetActiveCameraController();
	setVec3(vOffset, 0.0f, 0.0f, -20.0f);

	mulVecMat4(vOffset, pCam->last_camera_matrix, vNewPos);

	dynNodeSetPos(genPreviewSettings.pTestNode, vNewPos);
	dynNodeSetRot(genPreviewSettings.pTestNode, unitquat);
	copyMat3(unitmat, mat);
	copyVec3(vNewPos, mat[3]);
	partEdClickedReset();
}

static void partEdPreviewDropTestNode( UIButton* button, void* user_data)
{
	partEdDropTestNode();
}

static void partEdDrawWorld( UIButton* button, void* user_data)
{
	genPreviewSettings.bDrawWorld = !genPreviewSettings.bDrawWorld;
	GamePrefStoreInt("ParticleEditor\\Preview\\DrawWorld",  (int)(genPreviewSettings.bDrawWorld));

	ui_ButtonSetText(button, genPreviewSettings.bDrawWorld ? "No World" : "Draw World");

	emEditorHideWorld(&particle_editor, !genPreviewSettings.bDrawWorld);

	genPreviewSettings.bDropNode = true;
	partEdDropTestNode();
}

static void partEdResetNode( UIButton* button, void* user_data )
{
	dynNodeSetPos( genPreviewSettings.pTestNode, zerovec3);
}

static void partEdToggleAutosave( UICheckButton* checkbutton, void* user_data)
{
	GET_ACTIVE_PARTICLE_DOC(pDoc);
	genPreviewSettings.bAutoSave = checkbutton->state;
	if (genPreviewSettings.bAutoSave && pDoc)
		partEdSave(pDoc);
}

typedef enum eDrawFXType
{
	eDrawFxType_All, 
	eDrawFxType_None, 
	eDrawFxType_Isolate, 
	eDrawFxType_Toggle,
} eDrawFxType;

static void partEdSetDrawFX( UIButton* button, void* casted_type)
{
	eDrawFxType eType = (eDrawFxType)(casted_type);
	GET_ACTIVE_PARTICLE_DOC(pCurrentDoc);

	switch (eType)
	{
		xcase eDrawFxType_All:
			FOR_EACH_IN_EARRAY(eaDocs, ParticleDoc, pDoc)
			{
				pDoc->previewSettings.bDontDraw = false;
			}
			FOR_EACH_END;
		xcase eDrawFxType_None:
			FOR_EACH_IN_EARRAY(eaDocs, ParticleDoc, pDoc)
			{
				pDoc->previewSettings.bDontDraw = true;
			}
			FOR_EACH_END;
		xcase eDrawFxType_Isolate:
			FOR_EACH_IN_EARRAY(eaDocs, ParticleDoc, pDoc)
			{
				pDoc->previewSettings.bDontDraw = true;
			}
			FOR_EACH_END;
			if (pCurrentDoc)
				pCurrentDoc->previewSettings.bDontDraw = false;
		xcase eDrawFxType_Toggle:
			if (pCurrentDoc)
				pCurrentDoc->previewSettings.bDontDraw = !pCurrentDoc->previewSettings.bDontDraw;
	}
	genPreviewSettings.bRebuildUI = true;
}


static void partEdDataChanged(UIRTNode* pDummy, ParticleDoc* pDoc)
{
	emSetDocUnsaved(&pDoc->base, true);
	pDoc->bRecalculateSet = true;

	genPreviewSettings.bRebuildUI = true;

	/*
	if (StructCompare(parse_DynFxFastParticleInfo, pDoc->pBackup, pDoc->pInfo)==0)
		return;
	*/
	EditCreateUndoStruct(pDoc->base.edit_undo_stack, NULL, pDoc->pBackup, pDoc->pInfo, parse_DynFxFastParticleInfo, partEdUndoChanged);

	partEdBackupInfo(pDoc);


	if (genPreviewSettings.bAutoSave)
		partEdSave(pDoc);
	//pDoc->bValidated = true;
}

static void partEdDataChangedFromSpinner(UIRTNode* pNode, ParticleDoc* pDoc)
{
	UISpinner* pSpinner = (UISpinner*)pNode->widget2;
	genPreviewSettings.bRebuildUI = true;
	pDoc->bRecalculateSet = true;
	emSetDocUnsaved(&pDoc->base, true);
	if (!pSpinner->pressed)
	{
		/*
		if (StructCompare(parse_DynFxFastParticleInfo, pDoc->pBackup, pDoc->pInfo)==0)
			return;
			*/
		EditCreateUndoStruct(pDoc->base.edit_undo_stack, NULL, pDoc->pBackup, pDoc->pInfo, parse_DynFxFastParticleInfo, partEdUndoChanged);

		partEdBackupInfo(pDoc);

		//pDoc->bValidated = true;
	}

	if (genPreviewSettings.bAutoSave)
		partEdSave(pDoc);
}

static int iValidSync = -1;

static void partEdSyncChanged(UIRTNode* pDummy, ParticleDoc* pDoc)
{
	int i;

	if (iValidSync < 0 || !pDoc->pInfo->bSync[iValidSync])
	{
		int iMostCPs = -1;
		iValidSync = -1;
		// We don't have a valid sync, so pick the one with the most control points to be our new sync target, in case several were activated at once
		for (i=0; i<PART_NUM_GRAPHS; ++i)
		{
			if (pDoc->pInfo->bSync[i])
			{
				int iCPs = 0;
				int j;
				// Count CP's
				for (j=0; j<ARRAY_SIZE(pDoc->pInfo->curveTime); ++j)
				{
					size_t szOffset = ((FXParticleEditorGraphChangedData*)(pGraphPanes[i]->pDraggingData))->szOffset;
					F32 fX = *(F32 *)(((char *)&pDoc->pInfo->curveTime[j]) + szOffset);
					if (fX >= 0.0f)
						++iCPs;
				}
				if (iCPs > iMostCPs)
				{
					iValidSync = i;
					iMostCPs = iCPs;
				}
			}
		}
	}

	if (iValidSync >= 0)
	{
		size_t szOffset = ((FXParticleEditorGraphChangedData*)(pGraphPanes[iValidSync]->pDraggingData))->szOffset;

		for (i=1; i<ARRAY_SIZE(pDoc->pInfo->curveTime); ++i)
		{
			int j;
			for (j=0; j<PART_NUM_GRAPHS; ++j)
			{
				if (j != iValidSync && pDoc->pInfo->bSync[j])
				{
					F32 fSyncX = *(F32 *)(((char *)&pDoc->pInfo->curveTime[i]) + szOffset);
					size_t szOtherOffset = ((FXParticleEditorGraphChangedData*)(pGraphPanes[j]->pDraggingData))->szOffset;
					F32* pfX = (F32 *)(((char *)&pDoc->pInfo->curveTime[i]) + szOtherOffset);
					F32* pfY = (F32 *)(((char *)&pDoc->pInfo->curvePath[i]) + szOtherOffset);
					F32* pfJitter = (F32 *)(((char *)&pDoc->pInfo->curveJitter[i]) + szOtherOffset);
					if (fSyncX >= 0.0f)
					{
						F32 fPrevY = *(F32 *)(((char *)&pDoc->pInfo->curvePath[i-1]) + szOtherOffset);
						// if the other colors are missing points, create new ones by copying the previous path values
						if (*pfX < 0.0f)
						{
							*pfY = fPrevY;
						}
						*pfX = fSyncX;
					}
					else // clear points to match
					{
						*pfX = -1.0f;
						*pfY = 0.0f;
						*pfJitter = 0.0f;
					}
				}
			}
		}
	}
	partEdDataChanged(pDummy, pDoc);
}

static void partEdRGBBlendChanged(UIRTNode* pDummy, ParticleDoc* pDoc)
{

	/*
	if (pDoc->pInfo->bRGBBlend)
	{
		// Make sure the data conforms to RGB blend rules
		// Use the hue graph as our starting point
		int i;
		int iMaxIndex = -1;
		for (i=1; i<ARRAY_SIZE(pDoc->pInfo->curveTime); ++i)
		{
			if (pDoc->pInfo->curveTime[i].vColor[0] >= 0.0f)
			{
				// if the other colors are missing points, create new ones by copying the previous path values
				if (pDoc->pInfo->curveTime[i].vColor[1] < 0.0f)
					pDoc->pInfo->curvePath[i].vColor[1] = pDoc->pInfo->curvePath[i-1].vColor[1];
				if (pDoc->pInfo->curveTime[i].vColor[2] < 0.0f)
					pDoc->pInfo->curvePath[i].vColor[2] = pDoc->pInfo->curvePath[i-1].vColor[2];

				// copy the times across all points
				pDoc->pInfo->curveTime[i].vColor[1] = pDoc->pInfo->curveTime[i].vColor[2] = pDoc->pInfo->curveTime[i].vColor[0];
			}
			else // clear points to match
			{
				setVec3same(pDoc->pInfo->curveTime[i].vColor, -1.0f);
				setVec3same(pDoc->pInfo->curvePath[i].vColor, 0.0f);
				setVec3same(pDoc->pInfo->curveJitter[i].vColor, 0.0f);
			}
		}
	}
	*/

	if (pDoc->pInfo->bRGBBlend)
		pDoc->pInfo->bSync[0] = pDoc->pInfo->bSync[1] = pDoc->pInfo->bSync[2] = true;

	partEdSyncChanged(pDummy, pDoc);
}

static void partEdGotFocus(ParticleDoc* pDoc)
{
	genPreviewSettings.bRebuildUI = true;
}

static void partEdPreviewSettingsChanged(UIRTNode* pDummy, ParticleDoc* pDoc)
{
	genPreviewSettings.bRebuildUI = true;
	GamePrefStoreFloat("ParticleEditor\\Preview\\GraphHeight",  genPreviewSettings.fGraphHeight);
	GamePrefStoreFloat("ParticleEditor\\Preview\\ScaleFactor",  genPreviewSettings.fScaleFactor);
	GamePrefStoreFloat("ParticleEditor\\Preview\\Opacity",  genPreviewSettings.fOpacity);
	GamePrefStoreFloat("ParticleEditor\\Preview\\Axes",  genPreviewSettings.fAxes);
	GamePrefStoreFloat("ParticleEditor\\Preview\\GridSize",  genPreviewSettings.fGridSize);
	GamePrefStoreFloat("ParticleEditor\\Preview\\GridAlpha",  genPreviewSettings.fGridAlpha);
	GamePrefStoreInt("ParticleEditor\\Preview\\GridXZ",  genPreviewSettings.bGridXZ);
	GamePrefStoreInt("ParticleEditor\\Preview\\GridXY",  genPreviewSettings.bGridXY);
	GamePrefStoreInt("ParticleEditor\\Preview\\GridYZ",  genPreviewSettings.bGridYZ);
}


static void partEdCreateNew(SA_PARAM_NN_VALID ParticleDoc* pDoc)
{
	int i=1;
	const char* pcDefaultName = allocAddString("NewParticle");
	pDoc->pInfo = StructCreate(parse_DynFxFastParticleInfo);
	pDoc->pInfo->pcName = pcDefaultName;
	pDoc->pInfo->bLowRes = false;
	dynDebugState.bEditorHasOpened = true;
	strcpy(pDoc->base.doc_display_name, pDoc->pInfo->pcName);
	dynFxInitNewFastParticleInfo(pDoc->pInfo);
	while (dynFxFastParticleInfoFromNameNonConst(pDoc->pInfo->pcName))
	{
		char pcNewName[256];
		char pcNum[16];
		strcpy(pcNewName, pcDefaultName);
		sprintf(pcNum, "%d", i);
		strcat(pcNewName, pcNum);
		pDoc->pInfo->pcName = allocAddString(pcNewName);
		++i;
	}
	RefSystem_AddReferent("DynParticle", pDoc->pInfo->pcName, pDoc->pInfo);
}

static bool partEdSetupDocFromExisting(SA_PARAM_NN_VALID ParticleDoc* pDoc, SA_PARAM_NN_STR const char* pcFileName)
{
	char cName[256];
	DynFxFastParticleInfo* pSource;
	getFileNameNoExt(cName, pcFileName);
	pSource = dynFxFastParticleInfoFromNameNonConst(cName);
	if (!pSource)
	{
		Errorf("Unable to find DynParticle %s", cName);
		return false;
	}
	else
	{
		const char* pcOldName = pDoc->pInfo->pcName;
		StructCopyAll(parse_DynFxFastParticleInfo, pSource, pDoc->pInfo);
		pDoc->pInfo->pcName = pcOldName;
		strcpy(pDoc->base.doc_display_name, pSource->pcName);
		pDoc->pcLoadedFilename = allocAddString(pcFileName);
		emDocAssocFile(&pDoc->base, pDoc->pcLoadedFilename);
		pDoc->pInfo->pcFileName = allocAddString("NoSuchFile");
	}
	return true;
}

static bool partEdChooseTextureCallback(EMPicker *picker, EMPickerSelection **selections, ParticleDoc* pDoc)
{
	char cTextureName[1024];

	if (!eaSize(&selections))
		return false;

	getFileNameNoExt(cTextureName, selections[0]->doc_name);
	pDoc->pInfo->pcTexture = allocAddString(cTextureName);
	pDoc->pInfo->pTexture = NULL;
	genPreviewSettings.bRebuildUI = true;
	partEdDataChanged(NULL, pDoc);

	return true;
}

static void partEdChooseTexture(UIAnyWidget* pWidget, SA_PARAM_NN_VALID ParticleDoc* pDoc)
{
	EMPicker* texturePicker = emPickerGetByName( "Texture Picker" );
	
	if (texturePicker)
		emPickerShow(texturePicker, NULL, false, partEdChooseTextureCallback, pDoc);
}

static UISkin *partEdGetSkin(ParticleDoc* pDoc, UISkin* pBase)
{
	static UISkin* pSkin = NULL;
	if (!pSkin)
		pSkin = ui_SkinCreate(pBase);
	pSkin->background[0].a = genPreviewSettings.fOpacity;
	return pSkin;
}

static UISkin *partEdGetGraphSkin(ParticleDoc* pDoc, UISkin* pBase)
{
	static UISkin* pSkin = NULL;
	if (!pSkin)
		pSkin = ui_SkinCreate(pBase);
	pSkin->background->a = genPreviewSettings.fOpacity;
	return pSkin;
}


void partEdGraphSetFromPoints(DynFxFastParticleInfo* pInfo, UIGraphPane* pPane, size_t szOffset)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(pInfo->curvePath); i++)
	{
		F32 fX = *(F32 *)(((char *)&pInfo->curveTime[i]) + szOffset);
		F32 fY = *(F32 *)(((char *)&pInfo->curvePath[i]) + szOffset);
		F32 fJitter = *(F32 *)(((char *)&pInfo->curveJitter[i]) + szOffset);

		if (fX >= 0.0f)
		{
			// it's a valid point. Make sure it exists on graph and update it
			Vec2 v2Pos = {fX, fY};
			if (eaSize(&pPane->pGraph->eaPoints) < (i+1))
				ui_GraphAddPoint(pPane->pGraph, ui_GraphPointCreate(v2Pos, fJitter));
			else 
				ui_GraphMovePointIndex(pPane->pGraph, i, v2Pos, fJitter);
		}
		else
		{
			// i is a bad index, so there should be no index i in the graph list
			while (eaSize(&pPane->pGraph->eaPoints) > i)
			{
				ui_GraphPointFree(eaPop(&pPane->pGraph->eaPoints));
			}
		}
	}
	ui_GraphPaneGraphResetTextEntries(pPane->pGraph, pPane);
}

void partEdSetPath( DynFxFastParticleInfo* pInfo, S32 i, FXParticleEditorGraphChangedData* pData, UIGraphPoint * pPoint, UIGraphPane* pPane) 
{
	F32 *pfX = (F32 *)(((char *)&pInfo->curveTime[i]) + pData->szOffset);
	F32 *pfY = (F32 *)(((char *)&pInfo->curvePath[i]) + pData->szOffset);
	F32 *pfJitter = (F32 *)(((char *)&pInfo->curveJitter[i]) + pData->szOffset);
	if (pPoint)
	{
		if (!pPane)
		{
			*pfY = pPoint->v2Position[1];
			*pfJitter = pPoint->fMargin;
		}
		else if (i > 0 && *pfX == -1.0f && *pfY == 0.0f)
		{
			// Must have added a new point, so init this one to the previous point's Y.
			F32 *pfPrevY = (F32 *)(((char *)&pInfo->curvePath[i-1]) + pData->szOffset);
			*pfY = *pfPrevY;
			*pfJitter = 0.0f;
		}

		*pfX = pPoint->v2Position[0];

		if (pPane)
		{
			Vec2 v2Pos;
			v2Pos[0] = *pfX;
			v2Pos[1] = *pfY;
			ui_GraphMovePointIndex(pPane->pGraph, i, v2Pos, *pfJitter);
		}
	}
	else
	{
		*pfX = -1.0f;
		*pfY = 0;
		*pfJitter = 0;
	}
}

static void partEdDataChangedFromGraph(UIGraphPane* pPane, FXParticleEditorGraphChangedData* pData)
{
	ParticleDoc* pDoc = pData->pDoc;
	DynFxFastParticleInfo* pInfo = pDoc->pInfo;
	S32 i;
	S32 iPaneIndex = -1;

	for (i=0; i<PART_NUM_GRAPHS; ++i)
	{
		if (pGraphPanes[i] == pPane)
			iPaneIndex = i;
	}

	if (iPaneIndex >= 0)
		aGraphSelectedPoints[iPaneIndex] = ui_GraphGetSelectedPointBitField(pPane->pGraph);

	for (i = 0; i < ARRAY_SIZE(pInfo->curveTime); i++)
	{
		UIGraphPoint *pPoint = ui_GraphGetPoint(ui_GraphPaneGetGraph(pPane), i);
		partEdSetPath(pInfo, i, pData, pPoint, NULL);

		if (iPaneIndex >= 0 && pInfo->bSync[iPaneIndex])
		{
			int j;
			for (j=0; j<PART_NUM_GRAPHS; ++j)
			{
				if (j != iPaneIndex && pInfo->bSync[j])
				{
					partEdSetPath(pInfo, i, pGraphPanes[j]->pDraggingData, pPoint, pGraphPanes[j]);
				}
			}
		}
	}

	if (iPaneIndex >= 0 && pInfo->bSync[iPaneIndex])
	{
		int j;
		for (j=0; j<PART_NUM_GRAPHS; ++j)
		{
			if (j != iPaneIndex && pInfo->bSync[j])
			{
				FXParticleEditorGraphChangedData* pOtherData = pGraphPanes[j]->pDraggingData;
				partEdGraphSetFromPoints(pInfo, pGraphPanes[j], pOtherData->szOffset);
				aGraphSelectedPoints[j] = ui_GraphGetSelectedPointBitField(pGraphPanes[j]->pGraph);
			}
		}
	}

	emSetDocUnsaved(&pDoc->base, true);
	if (genPreviewSettings.bAutoSave)
		partEdSave(pDoc);
}

static void partEdDataChangedFromGraphUndo(UIGraphPane* pPane, FXParticleEditorGraphChangedData* pData)
{
	ParticleDoc* pDoc = pData->pDoc;
	/*
	if (StructCompare(parse_DynFxFastParticleInfo, pDoc->pBackup, pDoc->pInfo)==0)
		return;
		*/
	EditCreateUndoStruct(pDoc->base.edit_undo_stack, NULL, pDoc->pBackup, pDoc->pInfo, parse_DynFxFastParticleInfo, partEdUndoChanged);
	partEdBackupInfo(pDoc);
	partEdDataChangedFromGraph(pPane, pData);
	genPreviewSettings.bRebuildUI = true;
	if (genPreviewSettings.bAutoSave)
		partEdSave(pDoc);
}

static void partEdGraphPaneFree(UIGraphPane *pPane)
{
	SAFE_FREE(pPane->pChangedData);
}

static UIGraphPane* partEdAddGraph(SA_PARAM_NN_VALID UIRTNode* root, SA_PARAM_NN_VALID ParticleDoc* pDoc, size_t szOffset, SA_PARAM_NN_STR const char *pchYAxis, Vec2 v2Min, Vec2 v2Max, SA_PARAM_NN_STR const char* pchName, F32 fHeight, SA_PARAM_NN_VALID const char* pchLoopName, SA_PARAM_NN_VALID const char* pchSyncName, const char* pchMinName, Vec2 vMinBounds, const char* pchMaxName, Vec2 vMaxBounds, U32 uiSelectedPoints)
{
	DynFxFastParticleInfo* pInfo = pDoc->pInfo;
	UIGraphPane* pPane = ui_GraphPaneCreate("Time", pchYAxis, v2Min, v2Max, ARRAY_SIZE(pInfo->curvePath), true, true, true);
	UIGraph* pGraph = ui_GraphPaneGetGraph(pPane);
	S32 i;
	UI_WIDGET(pGraph)->pOverrideSkin = partEdGetGraphSkin(pDoc, UI_GET_SKIN(pGraph));
	UI_WIDGET(pPane)->pOverrideSkin = partEdGetGraphSkin(pDoc, UI_GET_SKIN(pGraph));
	UI_WIDGET(pPane)->uClickThrough = true;
	ui_GraphSetDrawScale(pGraph, true);
	ui_GraphSetDrawConnection(pGraph, true, true);
	ui_GraphSetLockToX0(pGraph, true);
	ui_GraphSetLockToIndex(pGraph, true);
	ui_GraphSetSort(pGraph, true);
	ui_GraphSetResolution(pGraph, 4, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pPane), 1.f, fHeight, UIUnitPercentage, UIUnitFixed);
	ui_RebuildableTreeAddWidget(root, UI_WIDGET(pPane), NULL, true, pchName, NULL);

	for (i = 0; i < ARRAY_SIZE(pInfo->curvePath); i++)
	{
		F32 fX = *(F32 *)(((char *)&pInfo->curveTime[i]) + szOffset);
		F32 fY = *(F32 *)(((char *)&pInfo->curvePath[i]) + szOffset);
		F32 fJitter = *(F32 *)(((char *)&pInfo->curveJitter[i]) + szOffset);
		if ((i == 0 && fX >= 0) || (i > 0 && fX > 0))
		{
			Vec2 v2Pos = {fX, fY};
			ui_GraphAddPointAndCallback(pGraph, ui_GraphPointCreate(v2Pos, fJitter));
		}
	}

	ui_GraphSetMinPoints(pGraph, 1);

	{
		FXParticleEditorGraphChangedData *pData = calloc(1, sizeof(FXParticleEditorGraphChangedData));
		pData->pDoc = pDoc;
		pData->szOffset = szOffset;
		ui_GraphPaneSetChangedCallback(pPane, partEdDataChangedFromGraphUndo, pData);
		ui_GraphPaneSetDraggingCallback(pPane, partEdDataChangedFromGraph, pData);
		ui_WidgetSetFreeCallback(UI_WIDGET(pPane), partEdGraphPaneFree);
	}

	// Add spinner to expander
	{
		UIAutoWidgetParams params = {0};
		params.min[0] = 1.0f;
		params.max[0] = 60.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, pchLoopName, pInfo, true, partEdDataChanged, pDoc, &params, "Loop scale factor");

		ZeroStruct(&params);
		if (pInfo->bRGBBlend && (  stricmp(pchName, "Hue")==0 || stricmp(pchName, "Saturation")==0 || stricmp(pchName, "Value")==0  )  )
		{
			params.disabled = true;
		}
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, pchSyncName, pInfo, true, partEdSyncChanged, pDoc, &params, "Sync times");

		if (pchMinName)
		{
			params.min[0] = vMinBounds[0];
			params.max[0] = vMinBounds[1];
			params.step[0] = 0.1f;
			params.type = AWT_Spinner;
			ui_AutoWidgetAdd(root, parse_ParticlePreviewSettings, pchMinName, &pDoc->previewSettings, true, partEdPreviewSettingsChanged, pDoc, &params, "Min graph bounds");
		}
		if (pchMaxName)
		{
			params.min[0] = vMaxBounds[0];
			params.max[0] = vMaxBounds[1];
			params.step[0] = 0.1f;
			params.type = AWT_Spinner;
			ui_AutoWidgetAdd(root, parse_ParticlePreviewSettings, pchMaxName, &pDoc->previewSettings, true, partEdPreviewSettingsChanged, pDoc, &params, "Max graph bounds");
		}
	}

	if (stricmp(pchName, "ScaleX")==0 && pInfo->eStreakMode == DynFastStreakMode_None)
	{
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "LinkScale", pInfo, true, partEdDataChanged, pDoc, NULL, "Link scale.y and scale.x so it is always square");
	}

	if (stricmp(pchName, "Hue")==0)
	{
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "RGBBlend", pInfo, true, partEdRGBBlendChanged, pDoc, NULL, "Blend colors in RGB instead of HSV. Locks control points together across Hue, Saturation, and Value graphs.");
	}

	ui_GraphSetSelectedPointBitField(pPane->pGraph, uiSelectedPoints);

	return pPane;
}

static void partEdSetupGraphUI(SA_PARAM_NN_VALID ParticleDoc* pDoc)
{
	DynFxFastParticleInfo* pInfo = pDoc->pInfo;

	S32 i;

	{
		UIGraphPane *pPane = NULL;
		for (i=0; i< 8; ++i)
		{
			ParticleEditorPanel *activePanel = (i == 5 && pDoc->pInfo->eStreakMode ? &pDoc->streakScale : &pDoc->graphs[i]);
			int a;
			char cLoopName[64];
			char cSyncName[64];
			size_t szOffset;
			Vec2 vMinBounds = { 0, 0};
			Vec2 vMaxBounds = { 0, 0};
			Vec2 vMin, vMax;
			const char* pcMaxName = NULL;
			const char* pcMinName = NULL;
			char cMaxName[64];
			const char* pcGraphName = ppcGraphNames[i];

			sprintf(cLoopName, "Loop");
			strcat(cLoopName, ppcGraphNames[i]);

			sprintf(cSyncName, "Sync");
			strcat(cSyncName, ppcGraphNames[i]);

			if (i==5 && pDoc->pInfo->eStreakMode)
			{
				pcGraphName = "StreakScale";
			}

			sprintf(cMaxName, "Max_%d", i);

			if (i<4) 
				szOffset = offsetof(DynFxFastParticleCurveInfo, vColor[i]);
			else if (i<6) // scale 
			{
				szOffset = offsetof(DynFxFastParticleCurveInfo, vScale[i-4]);
				pcMaxName = cMaxName;
				vMaxBounds[0] = 1.0f;
				vMaxBounds[1] = 500.0f * genPreviewSettings.fScaleFactor;
			}
			else if (i==6)
				szOffset = offsetof(DynFxFastParticleCurveInfo, fRot);
			else // 7
				szOffset = offsetof(DynFxFastParticleCurveInfo, fSpin);

			// Value can go over 1.0
			if (i==2)
			{
				pcMaxName = cMaxName;
				vMaxBounds[0] = 1.0f;
				vMaxBounds[1] = 100.0f;
			}

			for (a = 0; a < ARRAY_SIZE(pInfo->curvePath); a++)
			{
				pDoc->previewSettings.fMin[i] = MIN(pDoc->previewSettings.fMin[i], *(F32*)((char*)&pInfo->curvePath[a] + szOffset));
				pDoc->previewSettings.fMax[i] = MAX(pDoc->previewSettings.fMax[i], *(F32*)((char*)&pInfo->curvePath[a] + szOffset));
			}
			vMin[0] = 0.0f;
			vMax[0] = 1.0f;
			vMin[1] = pDoc->previewSettings.fMin[i];
			vMax[1] = pDoc->previewSettings.fMax[i];
			if (i==5 && pDoc->pInfo->eStreakMode)
			{
				vMinBounds[0] = -500.0f;
				vMinBounds[1] = 0.0f;
				pcMinName = allocAddString("Min_5");
			}

			if (i==5 && pDoc->pInfo->bLinkScale && !pDoc->pInfo->eStreakMode)
				continue;

			// build UI
			ui_RebuildableTreeInit(activePanel->auto_widget, &UI_WIDGET(activePanel->scroll_area)->children, 0, 0, UIRTOptions_Default);
			pPane = partEdAddGraph(activePanel->auto_widget->root, pDoc, szOffset, ppcGraphNames[i], vMin, vMax, pcGraphName, genPreviewSettings.fGraphHeight, cLoopName, cSyncName, pcMinName, vMinBounds, pcMaxName, vMaxBounds, aGraphSelectedPoints[i]);
			ui_RebuildableTreeDoneBuilding(activePanel->auto_widget);
			partEdRefreshPanelSize(activePanel);
			//ui_GraphPaneSetBounds(pPane, pDoc->previewSettings.vMin[i], pDoc->previewSettings.vMin[i], vMinMax[i], vMaxMax[i]);

			pGraphPanes[i] = pPane;
		}

		partEdReAddGraphs(pDoc);
	}
}


static void partEdChangeHue(UIColorSlider* pSlider, UITextEntry* pText)
{
	char cHue[16];
	ParticleDoc* pDoc = (ParticleDoc*)emGetActiveEditorDoc();
	if (pDoc)
	{
		genPreviewSettings.fHueShift = pSlider->current[0];
		genPreviewSettings.bRebuildUI = true;
	}
	sprintf(cHue, "%.1f", pSlider->current[0]);
	ui_TextEntrySetText(pText, cHue);
}

static void partEdSetHueFromEntry(UITextEntry *entry, UIColorSlider *slider)
{
	F32 value;
	UIColorWindow *window = slider->changedData;
	Vec3 newColor;
	copyVec3(slider->current, newColor);
	if (sscanf(ui_TextEntryGetText(entry), "%f", &value) == 1)
	{
		newColor[0] = value;
	}
	ui_TextEntrySetCursorPosition(entry, 0);
	ui_ColorSliderSetValueAndCallback(slider, newColor);
}




static void partEdChangeSaturation(UIColorSlider* pSlider, UITextEntry* pText)
{
	char cSaturation[16];
	ParticleDoc* pDoc = (ParticleDoc*)emGetActiveEditorDoc();
	if (pDoc)
	{
		genPreviewSettings.fSaturationShift = pSlider->current[1] - 1;
		genPreviewSettings.bRebuildUI = true;
	}
	sprintf(cSaturation, "%.1f", pSlider->current[1] - 1);
	ui_TextEntrySetText(pText, cSaturation);
}

static void partEdSetSaturationFromEntry(UITextEntry *entry, UIColorSlider *slider)
{
	F32 value;
	UIColorWindow *window = slider->changedData;
	Vec3 newColor;
	copyVec3(slider->current, newColor);
	if (sscanf(ui_TextEntryGetText(entry), "%f", &value) == 1)
	{
		newColor[1] = value + 1;
	}
	ui_TextEntrySetCursorPosition(entry, 0);
	ui_ColorSliderSetValueAndCallback(slider, newColor);
}




static void partEdChangeValue(UIColorSlider* pSlider, UITextEntry* pText)
{
	char cValue[16];
	ParticleDoc* pDoc = (ParticleDoc*)emGetActiveEditorDoc();
	if (pDoc)
	{
		genPreviewSettings.fValueShift = pSlider->current[2] - 1;
		genPreviewSettings.bRebuildUI = true;
	}
	sprintf(cValue, "%.1f", pSlider->current[2] - 1);
	ui_TextEntrySetText(pText, cValue);
}

static void partEdSetValueFromEntry(UITextEntry *entry, UIColorSlider *slider)
{
	F32 value;
	UIColorWindow *window = slider->changedData;
	Vec3 newColor;
	copyVec3(slider->current, newColor);
	if (sscanf(ui_TextEntryGetText(entry), "%f", &value) == 1)
	{
		newColor[2] = value + 1;
	}
	ui_TextEntrySetCursorPosition(entry, 0);
	ui_ColorSliderSetValueAndCallback(slider, newColor);
}




static void partEdInitPanel(ParticleEditorPanel *panel, const char *tabName, const char *panelName, bool scrollX)
{
	panel->panel = emPanelCreate(tabName, panelName, 0);
	panel->scroll_area = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, scrollX, false);
	panel->scroll_area->widget.widthUnit = panel->scroll_area->widget.heightUnit = UIUnitPercentage;
	panel->scroll_area->widget.sb->alwaysScrollX = scrollX;
	panel->auto_widget = ui_RebuildableTreeCreate();
	emPanelAddChild(panel->panel, panel->scroll_area, false);
}

static void partEdRefreshPanelSize(ParticleEditorPanel *panel)
{
	emPanelSetHeight(panel->panel, panel->auto_widget->root->h + (panel->scroll_area->widget.sb->alwaysScrollX ? 20 : 0));
	panel->scroll_area->xSize = emGetSidebarScale() * elUIGetEndX(panel->scroll_area->widget.children[0]->children) + 5;
}

static void partEdReAddGraphs(ParticleDoc *pDoc)
{
	DynFxFastParticleInfo* pInfo = pDoc->pInfo;
	int i;

	for (i = 0; i < PART_NUM_GRAPHS; i++)
		eaFindAndRemove(&pDoc->base.em_panels, pDoc->graphs[i].panel);
	eaFindAndRemove(&pDoc->base.em_panels, pDoc->streakScale.panel);

	for (i = 0; i < PART_NUM_GRAPHS; i++)
	{
		if (i == 5 && pInfo->eStreakMode)
			eaPush(&pDoc->base.em_panels, pDoc->streakScale.panel);
		else if (i != 5 || !pInfo->bLinkScale)
			eaPush(&pDoc->base.em_panels, pDoc->graphs[i].panel);
	}
}

static void partEdInitUI(ParticleDoc *pDoc)
{
	int i; 

	// init panels
	partEdInitPanel(&pDoc->preview, "Preview", "Preview", true);
	partEdInitPanel(&pDoc->texture, "Properties", "Texture", true);
	partEdInitPanel(&pDoc->emission, "Properties", "Emission", true);
	partEdInitPanel(&pDoc->movement, "Properties", "Movement", true);


	for (i = 0; i < PART_NUM_GRAPHS; i++)
		partEdInitPanel(&pDoc->graphs[i], "Graphs", ppcGraphNames[i], false);
	partEdInitPanel(&pDoc->graphs[i], "Graphs", "StreakScale", false);

	eaPush(&pDoc->base.em_panels, pDoc->preview.panel);
	eaPush(&pDoc->base.em_panels, pDoc->texture.panel);
	eaPush(&pDoc->base.em_panels, pDoc->emission.panel);
	eaPush(&pDoc->base.em_panels, pDoc->movement.panel);

	partEdReAddGraphs(pDoc);
}

static void partEdSetupUI(SA_PARAM_NN_VALID ParticleDoc *pDoc)
{
	DynFxFastParticleInfo* pInfo = pDoc->pInfo;

	if (!pInfo)
	{
		Errorf("Particle Doc is missing a particle to edit!");
		return;
	}

	// Preview
	ui_RebuildableTreeInit(pDoc->preview.auto_widget, &UI_WIDGET(pDoc->preview.scroll_area)->children, 0, 0, UIRTOptions_Default);
	{
		UIRTNode *root = pDoc->preview.auto_widget->root;
		UIAutoWidgetParams params = {0};
		int i;

		params.min[0] = 0.0f;
		params.max[0] = 1.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Slider;
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "UpdateRate", &genPreviewSettings, false, partEdPreviewSettingsChanged, pDoc, &params, "Preview time scale");
		ui_AutoWidgetAdd(root, parse_ParticlePreviewSettings, "DontDraw", &pDoc->previewSettings, false, partEdPreviewSettingsChanged, pDoc, NULL, "Dont draw this particle system while editing");
		params.min[0] = 1.0f;
		params.max[0] = 100.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "ScaleFactor", &genPreviewSettings, false, partEdPreviewSettingsChanged, pDoc, &params, "Overall Scale Factor");
		params.min[0] = 140.0f;
		params.max[0] = 1200.0f;
		params.step[0] = 10.0f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "GraphHeight", &genPreviewSettings, true, partEdPreviewSettingsChanged, pDoc, &params, "Graph height");
//		params.min[0] = 1.0f;
//		params.max[0] = 255.0f;
//		params.step[0] = 1.0f;
//		params.type = AWT_Slider;
//		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "Opacity", &genPreviewSettings, false, partEdPreviewSettingsChanged, pDoc, &params, "Window Opacity");
		params.min[0] = 0.0f;
		params.max[0] = 30.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "Axes", &genPreviewSettings, true, partEdPreviewSettingsChanged, pDoc, &params, "RGB Axes length (0 means don't draw)");
		params.min[0] = 0.0f;
		params.max[0] = 50.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "GridSize", &genPreviewSettings, false, partEdPreviewSettingsChanged, pDoc, &params, "Size of the Black Lines in the Grid, in Feet");
		params.min[0] = 0.0f;
		params.max[0] = 255.0f;
		params.step[0] = 1.0f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "GridAlpha", &genPreviewSettings, false, partEdPreviewSettingsChanged, pDoc, &params, "Grid Alpha (0 means dont draw)");
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "GridXZ", &genPreviewSettings, true, partEdPreviewSettingsChanged, pDoc, NULL, "Draw XZ plane Grid");
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "GridXY", &genPreviewSettings, false, partEdPreviewSettingsChanged, pDoc, NULL, "Draw XY plane Grid");
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "GridYZ", &genPreviewSettings, false, partEdPreviewSettingsChanged, pDoc, NULL, "Draw YZ plane Grid");
		for (i=0; i<3; ++i)
		{
			params.min[i] = -300.0f;
			params.max[i] = 300.0f;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "TestVelocity", &genPreviewSettings, true, partEdPreviewSettingsChanged, pDoc, &params, "Simulates particle system as attached to moving particle");
		for (i=0; i<3; ++i)
		{
			params.min[i] = -50.0f;
			params.max[i] = 50.0f;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "TestMagnetPos", &genPreviewSettings, true, partEdPreviewSettingsChanged, pDoc, &params, "Simulates a magnet node location for the Magnetism slider below");
		for (i=0; i<3; ++i)
		{
			params.min[i] = -50.0f;
			params.max[i] = 50.0f;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "TestEmitTargetPos", &genPreviewSettings, true, partEdPreviewSettingsChanged, pDoc, &params, "Simulates a emit target node for line emission");
		{
			UIColorSlider* pHue;
			UIColorSlider* pSaturation;
			UIColorSlider* pValue;

			UITextEntry* pHueText;
			UITextEntry* pSaturationText;
			UITextEntry* pValueText;

			// Hue min/max
			Vec3 vHMin = {
				0.0f, 
				genPreviewSettings.fSaturationShift + 1,
				genPreviewSettings.fValueShift + 1};
			
			Vec3 vHMax = {
				360.0f,
				genPreviewSettings.fSaturationShift + 1,
				genPreviewSettings.fValueShift + 1};

			// Saturation min/max
			Vec3 vSMin = {
				genPreviewSettings.fHueShift, 
				0,
				genPreviewSettings.fValueShift + 1};

			Vec3 vSMax = {
				genPreviewSettings.fHueShift,
				2,
				genPreviewSettings.fValueShift + 1};

			// Value min/max
			Vec3 vVMin = {
				genPreviewSettings.fHueShift,
				genPreviewSettings.fSaturationShift + 1,
				0};

			Vec3 vVMax = {
				genPreviewSettings.fHueShift,
				genPreviewSettings.fSaturationShift + 1,
				2};
				
			Vec3 vStart = {
				genPreviewSettings.fHueShift,
				genPreviewSettings.fSaturationShift + 1,
				genPreviewSettings.fValueShift + 1};

			pHue        = ui_ColorSliderCreate(0, 0, 180, vHMin, vHMax, true);
			pSaturation = ui_ColorSliderCreate(0, 0, 180, vSMin, vSMax, true);
			pValue      = ui_ColorSliderCreate(0, 0, 180, vVMin, vVMax, true);

			ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "TestHueShift", &genPreviewSettings, true, partEdPreviewSettingsChanged, pDoc, NULL, "Apply the test hue shift to the right to see what an ingame hue shift will look like");

			// Hue
			ui_RebuildableTreeAddWidget(root, UI_WIDGET(pHue), NULL, true, "HuePicker", &params);
			pHueText = ui_TextEntryCreate("", 0, 0);
			ui_WidgetSetDimensions(UI_WIDGET(pHueText), 50.0f, 22.0f);
			ui_RebuildableTreeAddWidget(root, UI_WIDGET(pHueText), NULL, false, "HueText", &params);

			ui_ColorSliderSetChangedCallback(pHue, partEdChangeHue, pHueText);
			ui_TextEntrySetFinishedCallback(pHueText, partEdSetHueFromEntry, pHue);
			ui_ColorSliderSetValueAndCallback(pHue, vStart);

			// Saturation
			ui_RebuildableTreeAddWidget(root, UI_WIDGET(pSaturation), NULL, true, "SaturationPicker", &params);
			pSaturationText = ui_TextEntryCreate("", 0, 0);
			ui_WidgetSetDimensions(UI_WIDGET(pSaturationText), 50.0f, 22.0f);
			ui_RebuildableTreeAddWidget(root, UI_WIDGET(pSaturationText), NULL, false, "SaturationText", &params);

			ui_ColorSliderSetChangedCallback(pSaturation, partEdChangeSaturation, pSaturationText);
			ui_TextEntrySetFinishedCallback(pSaturationText, partEdSetSaturationFromEntry, pSaturation);
			ui_ColorSliderSetValueAndCallback(pSaturation, vStart);

			// Value
			ui_RebuildableTreeAddWidget(root, UI_WIDGET(pValue), NULL, true, "ValuePicker", &params);
			pValueText = ui_TextEntryCreate("", 0, 0);
			ui_WidgetSetDimensions(UI_WIDGET(pValueText), 50.0f, 22.0f);
			ui_RebuildableTreeAddWidget(root, UI_WIDGET(pValueText), NULL, false, "ValueText", &params);

			ui_ColorSliderSetChangedCallback(pValue, partEdChangeValue, pValueText);
			ui_TextEntrySetFinishedCallback(pValueText, partEdSetValueFromEntry, pValue);
			ui_ColorSliderSetValueAndCallback(pValue, vStart);
		}

		ui_AutoWidgetAdd(root, parse_GeneralParticlePreviewSettings, "IsInScreenSpace", &genPreviewSettings, true, partEdPreviewSettingsChanged, pDoc, NULL, "The particle system is in screen space");
	}
	ui_RebuildableTreeDoneBuilding(pDoc->preview.auto_widget);
	partEdRefreshPanelSize(&pDoc->preview);

	// Texture
	ui_RebuildableTreeInit(pDoc->texture.auto_widget, &UI_WIDGET(pDoc->texture.scroll_area)->children, 0, 0, UIRTOptions_Default);
	{
		UIRTNode *root = pDoc->texture.auto_widget->root;
		UIAutoWidgetParams params = {0};
		ui_AutoWidgetAddButton(root, "Texture: ", partEdChooseTexture, pDoc, false, "Choose a texture for this particle. There must be a texture for every particle.", NULL);
		ui_RebuildableTreeAddLabel(root, pInfo->pcTexture, NULL, false);
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "BlendMode", pInfo, true, partEdDataChanged, pDoc, NULL, "Set the blend mode");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "NoToneMap", pInfo, false, partEdDataChanged, pDoc, NULL, "If true, this particle won't get the default tone mapping.");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "OldestFirst", pInfo, false, partEdDataChanged, pDoc, NULL, "If true, reverses particle sort order (oldest particle on top).");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "HFlipTex", pInfo, true, partEdDataChanged, pDoc, NULL, "Randomly flip horizontally");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "VFlipTex", pInfo, false, partEdDataChanged, pDoc, NULL, "Randomly flip vertically");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "ConstantScreenSize", pInfo, false, partEdDataChanged, pDoc, NULL, "Distance no longer affects size on screen.");
		//ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "MultiChannelTex", pInfo, false, partEdDataChanged, pDoc, NULL, "Use R, G, and B channels of texture as random, distinct textures");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "QuadTex", pInfo, false, partEdDataChanged, pDoc, NULL, "Randomly choose a quadrant of the texture");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "LowRes", pInfo, true, partEdDataChanged, pDoc, NULL, "Particle can be rendered in low resolution (faster).");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "AnimatedTexture", pInfo, false, partEdDataChanged, pDoc, NULL, "Particle uses an animated texture.");
		{
			int i;
			if (pInfo->bQuadTex || pInfo->bAnimatedTexture)
				params.disabled = true;
			for (i=0; i<2; ++i)
			{
				params.min[i] = -10.0f;
				params.max[i] = 10.0f;
				params.step[i] = 0.1f;
			}
			params.type = AWT_Spinner;
			ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Scroll", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Rate at which the texture scrolls, in X or Y tex coords.");
			for (i=0; i<2; ++i)
			{
				params.min[i] = 0.0f;
				params.max[i] = 10.0f;
				params.step[i] = 0.1f;
			}
			params.type = AWT_Spinner;
			ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "ScrollJitter", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Jitter on rate at which the texture scrolls, in X or Y tex coords.");
			params.disabled = false;
		}

		{
			UIAutoWidgetParams animInfoParams = {0};			
			animInfoParams.disabled = !pInfo->bAnimatedTexture;
			
			// Animation cells.
			animInfoParams.min[0] = 1;
			animInfoParams.max[0] = pInfo->vAnimParams[1] * pInfo->vAnimParams[1];
			animInfoParams.step[0] = 1;

			// Rows and columns.
			animInfoParams.min[1] = 2;
			animInfoParams.max[1] = 8;
			animInfoParams.step[1] = 1;

			animInfoParams.type = AWT_Spinner;

			ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "AnimParams", pInfo, true, partEdDataChangedFromSpinner, pDoc, &animInfoParams, "Animation frames total and rows/columns in the texture.");
		}

		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "StreakMode", pInfo, true, partEdDataChanged, pDoc, NULL, "Set the streak mode");
		params.min[0] = -10.0f;
		params.max[0] = 10.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "ZBias", pInfo, true, partEdDataChanged, pDoc, &params, "Sets the system wide z bias, affects sorting and distance from camera");
		params.min[0] = -1000.0f;
		params.max[0] = 1000.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "SortBias", pInfo, false, partEdDataChanged, pDoc, &params, "Sets the system wide sort bias, for particle system sorting only");
	}
	ui_RebuildableTreeDoneBuilding(pDoc->texture.auto_widget);
	partEdRefreshPanelSize(&pDoc->texture);

	ui_RebuildableTreeInit(pDoc->emission.auto_widget, &UI_WIDGET(pDoc->emission.scroll_area)->children, 0, 0, UIRTOptions_Default);
	{
		UIRTNode *root = pDoc->emission.auto_widget->root;
		UIAutoWidgetParams params = {0};
		int i;

		params.min[0] = 0.1f;
		if (dynFxFastParticleMaxPossibleRate(pInfo, 0.0f) > 0.0f)
			params.max[0] = 500.0f / (dynFxFastParticleMaxPossibleRate(pInfo, 0.0f) *  pInfo->uiEmissionCount);
		else
			params.max[0] = 500.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Lifespan", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Lifespan in seconds");
		{
			int iCount = dynFxFastParticleMaxPossibleRate(pInfo, 0.0f) > 0.0f?(int)(pInfo->uiEmissionCount * dynFxFastParticleMaxPossibleRate(pInfo, 0.0f) * pInfo->fLifeSpan):(int)pInfo->uiEmissionCount;
			char cCalcCount[64];
			sprintf(cCalcCount, "Avg. Count: %d", iCount);
			ui_RebuildableTreeAddLabel(root, cCalcCount, NULL, false);
		}

		params.min[0] = 0.0f;
		params.max[0] = 500.0f / (pInfo->uiEmissionCount *  pInfo->fLifeSpan);
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Rate", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Particles per second");
		params.min[0] = 0.0f;
		params.max[0] = pInfo->fEmissionRate - 0.1f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "RateJitter", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Jitter on Particles per second");
		params.min[0] = 0.0f;
		params.max[0] = 500.0f / (pInfo->uiEmissionCount *  pInfo->fLifeSpan * fMaxFeetPerSecondForEmission);
		params.step[0] = 0.01f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "RatePerFoot", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Particles per second");
		params.min[0] = 0.0f;
		params.max[0] = pInfo->fEmissionRatePerFoot;
		params.step[0] = 0.01f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "RatePerFootJitter", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Jitter on Particles per second");
		params.min[0] = 0.0f;
		params.max[0] = 500.0f;
		params.step[0] = 0.01f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "MinSpeed", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Minimum speed for RatePerFoot to generate particles.");

		params.min[0] = 1.0f;
		if (dynFxFastParticleMaxPossibleRate(pInfo, 0.0f) > 0.0f)
			params.max[0] = floor(500.0f / (dynFxFastParticleMaxPossibleRate(pInfo, 0.0f) *  pInfo->fLifeSpan));
		else
			params.max[0] = 500.0f;
		params.step[0] = 1.0f;
		params.type = AWT_Spinner;
		if (pInfo->bCountByDistance)
			params.disabled = true;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Count", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Particles per burst");
		params.min[0] = 0.0f;
		params.max[0] = pInfo->uiEmissionCount;
		params.step[0] = 1.0f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "CountJitter", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Jitter on Particles per burst");        
		params.disabled = false;

		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "CountByDistance", pInfo, true, partEdDataChanged, pDoc, NULL, "The count is always multiplied by the length of the Line when used with an EmitTarget.");

		params.min[0] = 0.0f;
		params.max[0] = 50.0f;
		params.step[0] = 0.01f;
		params.type = AWT_Spinner;
		if (!pInfo->bCountByDistance)
			params.disabled = true;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "CountPerFoot", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Particles per foot when used with a line emitter");
		params.min[0] = 0.0f;
		params.max[0] = pInfo->fCountPerFoot;
		params.step[0] = 0.01f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "CountPerFootJitter", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Jitter on Particles per foot when used with a line emitter");        
		params.disabled = false;

		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "UniformLine", pInfo, true, partEdDataChanged, pDoc, NULL, "For a burst (count > 1), tries to uniformly place the values rather than randomly place them for different jitter types. Does not work with all jitter types.");
		if (!pInfo->bUniformLine)
			params.disabled = true;
		params.type = AWT_Default;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "LockEnds", pInfo, false, partEdDataChanged, pDoc, &params, "No position jitter on the first and last of any burst. Only makes sense with UniformJitter.");
		params.disabled = false;

		for (i=0; i<3; ++i)
		{
			params.min[i] = -50.0f * genPreviewSettings.fScaleFactor;
			params.max[i] = 50.0f * genPreviewSettings.fScaleFactor;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Position", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Starting position offset");
		for (i=0; i<3; ++i)
		{
			params.min[i] = 0.0f;
			params.max[i] = 50.0f * genPreviewSettings.fScaleFactor;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "PositionJitter", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Starting position random offset");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "UniformJitter", pInfo, false, partEdDataChanged, pDoc, NULL, "For a burst (count > 1), tries to uniformly place the values rather than randomly place them for different jitter types. Does not work with all jitter types.");
		params.min[0] = 0.0f;
		params.max[0] = 180.0f;
		params.step[0] = 1.0f;
		params.min[1] = 0.0f;
		params.max[1] = 180.0f;
		params.step[1] = 1.0f;
		params.min[2] = 0.0f;
		params.max[2] = 50.0f * genPreviewSettings.fScaleFactor;
		params.step[2] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "PositionSphereJitter", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Starting position random offset, spherical slice: THETA, PHI, RADIUS");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Shell", pInfo, false, partEdDataChanged, pDoc, NULL, "Use only the shell of the sphere jitter");
		params.min[0] = 0.0f;
		params.max[0] = 2.0f;
		params.step[0] = 0.01f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "InheritVelocity", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Percentage of parent's velocity we inherit");
		params.min[0] = 0.0f;
		params.max[0] = 1.0f;
		params.step[0] = 0.01f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Stickiness", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Percentage of movement every frame we inherit, NOT VELOCITY.");
	}
	ui_RebuildableTreeDoneBuilding(pDoc->emission.auto_widget);
	partEdRefreshPanelSize(&pDoc->emission);

	ui_RebuildableTreeInit(pDoc->movement.auto_widget, &UI_WIDGET(pDoc->movement.scroll_area)->children, 0, 0, UIRTOptions_Default);
	{
		UIRTNode *root = pDoc->movement.auto_widget->root;
		UIAutoWidgetParams params = {0};
		int i;
		for (i=0; i<3; ++i)
		{
			params.min[i] = -500.0f * genPreviewSettings.fScaleFactor;
			params.max[i] = 500.0f * genPreviewSettings.fScaleFactor;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Velocity", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Starting velocity");
		for (i=0; i<3; ++i)
		{
			params.min[i] = 0.0f;
			params.max[i] = 500.0f * genPreviewSettings.fScaleFactor;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "VelocityJitter", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Random range for starting velocity");
		for (i=0; i<3; ++i)
		{
			params.min[i] = -200.0f * genPreviewSettings.fScaleFactor;
			params.max[i] = 200.0f * genPreviewSettings.fScaleFactor;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Acceleration", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Starting Acceleration");
		for (i=0; i<3; ++i)
		{
			params.min[i] = 0.0f;
			params.max[i] = 200.0f * genPreviewSettings.fScaleFactor;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "AccelerationJitter", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Random range for starting acceleration");
		for (i=0; i<3; ++i)
		{
			params.min[i] = 0.0f;
			params.max[i] = 1.0f;
			params.step[i] = 0.01f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Drag", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Drag factor");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "DragJitter", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Drag factor jitter");
		for (i=0; i<3; ++i)
		{
			params.min[i] = -500.0f;
			params.max[i] = 500.0f;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Gravity", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Gravity acceleration");
		for (i=0; i<3; ++i)
		{
			params.min[i] = 0.0f;
			params.max[i] = 500.0f;
			params.step[i] = 0.1f;
		}
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "GravityJitter", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Gravity acceleration jitter");
		params.min[0] = -5000.0f;
		params.max[0] = 5000.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "GoTo", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "GoTo Speed");
		params.min[0] = -5000.0f;
		params.max[0] = 5000.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "Magnetism", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Magnetism acceleration");
		params.min[0] = -5000.0f;
		params.max[0] = 5000.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "VelocityOut", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Initial Velocity Out From MagnetPos");
		params.min[0] = 0.0f;
		params.max[0] = 10.0f;
		params.step[0] = 0.1f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "KillRadius", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Magnetism kill radius");
		params.min[0] = 0.0f;
		params.max[0] = 200.0f;
		params.step[0] = 0.01f;
		params.type = AWT_Spinner;
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "VelocityUpdateRate", pInfo, true, partEdDataChangedFromSpinner, pDoc, &params, "Rate at which velocity is recalculated from jitter values");
		ui_AutoWidgetAdd(root, parse_DynFxFastParticleInfo, "AccelerationUpdateRate", pInfo, false, partEdDataChangedFromSpinner, pDoc, &params, "Rate at which acceleration is recalculated from jitter values");
	}
	ui_RebuildableTreeDoneBuilding(pDoc->movement.auto_widget);
	partEdRefreshPanelSize(&pDoc->movement);

	partEdSetupGraphUI(pDoc);

	genPreviewSettings.bRebuildUI = false;
}

static void partEdResetGeneralPreviewSettings(GeneralParticlePreviewSettings* pSettings)
{
	pSettings->fUpdateRate = 1.0f;
	pSettings->fGraphHeight = GamePrefGetFloat("ParticleEditor\\Preview\\GraphHeight", 140);
	pSettings->fScaleFactor = GamePrefGetFloat("ParticleEditor\\Preview\\ScaleFactor", 1.0f);
	pSettings->fOpacity = GamePrefGetFloat("ParticleEditor\\Preview\\Opacity", 255);
	pSettings->fAxes = GamePrefGetFloat("ParticleEditor\\Preview\\Axes", 2000 * 0.001f);
	pSettings->fGridSize = GamePrefGetFloat("ParticleEditor\\Preview\\GridSize", 10.0f);
	pSettings->fGridAlpha = GamePrefGetFloat("ParticleEditor\\Preview\\GridAlpha", 64.0f);
	pSettings->bGridXZ = (bool)GamePrefGetInt("ParticleEditor\\Preview\\GridXZ", 1);
	pSettings->bGridXY = (bool)GamePrefGetInt("ParticleEditor\\Preview\\GridXY", 0);
	pSettings->bGridYZ = (bool)GamePrefGetInt("ParticleEditor\\Preview\\GridYZ", 0);
	pSettings->bDrawWorld = (bool)GamePrefGetInt("ParticleEditor\\Preview\\DrawWorld", 0);
    pSettings->bIsInScreenSpace = false;
	if (pSettings->pRotateGizmo)
		RotateGizmoDestroy(pSettings->pRotateGizmo);
	pSettings->pRotateGizmo = RotateGizmoCreate();
	emEditorHideWorld(&particle_editor, !pSettings->bDrawWorld);

}

static void partEdResetPreviewSettings(ParticlePreviewSettings* pSettings)
{
	pSettings->bDontDraw = false;
	{
		int i;
		const Vec2 vInitGraphBounds[8] =
		{
			{ -360, 360 },
			{ 0, 1 },
			{ 0, 1 },
			{ 0, 1 },

			{ 0, 3 },
			{ 0, 3 },
			{ -360, 360 },
			{ -1080, 1080 },
		};
		for (i=0; i<8; ++i)
		{
			pSettings->fMin[i] = vInitGraphBounds[i][0];
			pSettings->fMax[i] = vInitGraphBounds[i][1];
		}
	}
}

static void partEdSetupFPParams(ParticleDoc* pDoc)
{
	DynFPSetParams params = {0};
	params.pInfo = pDoc->pInfo;
	params.pLocation = genPreviewSettings.pTestNode;
	params.pMagnet = genPreviewSettings.pTestMagnetNode;
	params.pEmitTarget = genPreviewSettings.pTestEmitTargetNode;
	params.ePosFlag = DynParticleEmitFlag_Inherit;
	params.eRotFlag = DynParticleEmitFlag_Inherit;
	params.bMaxBuffer = true;
	pDoc->pTestSet = dynFxFastParticleSetCreate(&params);
}

static EMEditorDoc* partEdNewDoc(const char* type, void *unused)
{
	ParticleDoc* pDoc = calloc(1, sizeof(ParticleDoc));

	eaPush(&eaDocs, pDoc);

	pDoc->pUITree = ui_RebuildableTreeCreate();

	partEdCreateNew(pDoc);

	partEdBackupInfo(pDoc);

	pDoc->base.edit_undo_stack = EditUndoStackCreate();

	partEdResetPreviewSettings(&pDoc->previewSettings);
	dynFxFastParticleInfoInit(pDoc->pInfo);
	partEdInitUI(pDoc);
	partEdSetupUI(pDoc);

	/*
	genPreviewSettings.pTestNode = dynNodeAlloc();
	genPreviewSettings.pTestMagnetNode = dynNodeAlloc();
	genPreviewSettings.pTestEmitTargetNode = dynNodeAlloc();
	*/

	partEdSetupFPParams(pDoc);

	return &pDoc->base;

	/*
	if (!name_in)
		return NULL;

	strcpy(name, name_in);

	// Setup data on new fxDoc
	fxDoc->ui_tree = ui_RebuildableTreeCreate();
	fxDoc->colorPickerX = GamePrefGetInt("fxeColorPickerX", 10);
	fxDoc->colorPickerY = GamePrefGetInt("fxeColorPickerY", 100);
	setVec4(fxDoc->color, 1, 1, 1, 1);
	setVec4(fxDoc->bgcolor, 0.5, 0.5, 0.5, 1);
	strcpy(fxDoc->fxeLocale, locGetName(getCurrentLocale()));
	if (texFind(name, 0)==NULL) {
	if (texWordFind(name, 1)) {
	// Dynamic
	TexWordParams *params = createTexWordParams();
	fxDoc->bind = texFindDynamic(name, params, WL_FOR_UI, NULL);
	} else {
	// New Dynamic?
	// Not allowed
	Errorf("Trying to open a file for editing which is not recognized: %s", name);
	return NULL;
	}
	} else {
	fxDoc->bind = texLoadBasic(name, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);
	}
	assert(fxDoc->bind);
	fxDoc->bind = fxDoc->bind->actualTexture;
	fxDoc->pFX = fxDoc->bind->pFX;

	if (!fxDoc->pFX) {
	sprintf(filename, "%s/%s", texFindDirName(fxDoc->bind), texFindName(fxDoc->bind));
	emSetDocFile(&fxDoc->base_doc, textureToBaseTexWord(filename, SAFESTR(filename)));
	} else {
	strcpy(filename, fxDoc->pFX->name);
	emSetDocFile(&fxDoc->base_doc, fxDoc->pFX->name);
	}
	strcpy(fxDoc->base_doc.doc_display_name, name);

	fileLocateWrite(filename, messageStoreFilename); // Need to get Core for Core, etc
	changeFileExt(messageStoreFilename, ".ms", messageStoreFilename);
	emAddLinkedFile(fxDoc->base_doc.file, messageStoreFilename);

	// Create a new message store
	fxDoc->messageStore = createMessageStore(1);
	initMessageStore(fxDoc->messageStore, locGetIDByName(fxDoc->fxeLocale), NULL);
	msAddMessages(fxDoc->messageStore, messageStoreFilename, NULL);

	// Setup the fxDoc UI
	fxeSetupPopupMenus(fxDoc);
	fxeSetupUI(fxDoc);

	fxDoc->base_doc.file->saved = 1;

	return &fxDoc->base_doc;
	*/
}

static void partEdCloseDoc(ParticleDoc* pDoc)
{
	gfxQueueClearEditorSets();
	eaDestroyEx(&pDoc->base.ui_windows, ui_WindowFreeInternal);
	ui_RebuildableTreeDestroy(pDoc->pUITree);
	StructDestroy(parse_DynFxFastParticleInfo, pDoc->pBackup);
	if (pDoc->pTestSet)
		dynFxFastParticleSetDestroy(pDoc->pTestSet);
	RefSystem_RemoveReferent(pDoc->pInfo, true);
	StructDestroy(parse_DynFxFastParticleInfo, pDoc->pInfo);
	eaFindAndRemove(&eaDocs, pDoc);
	free(pDoc);
}


static EMEditorDoc* partEdLoadDoc(const char *name, const char *type)
{
	ParticleDoc* pDoc = calloc(1, sizeof(ParticleDoc));

	assert(strcmpi(type, "part") == 0);
	eaPush(&eaDocs, pDoc);

	pDoc->pUITree = ui_RebuildableTreeCreate();

	partEdCreateNew(pDoc);
	if (!partEdSetupDocFromExisting(pDoc, name))
	{
		partEdCloseDoc(pDoc);
		return NULL;
	}

	partEdBackupInfo(pDoc);

	pDoc->base.edit_undo_stack = EditUndoStackCreate();

	partEdResetPreviewSettings(&pDoc->previewSettings);
	dynFxFastParticleInfoInit(pDoc->pInfo);
	partEdInitUI(pDoc);
	partEdSetupUI(pDoc);

	partEdSetupFPParams(pDoc);



	return &pDoc->base;
}


static void partEdDrawDoc(ParticleDoc* pDoc)
{
	if (genPreviewSettings.bRebuildUI)
		partEdSetupUI(pDoc);
}

void partEdDrawGrid( int uid, ParticleDoc* pThisDoc, int iGridPlane) 
{
	GroupDef *ground_plane = objectLibraryGetGroupDef(uid, true);
	SingleModelParams params = {0};

	params.model = ground_plane->model;
	params.alpha = genPreviewSettings.fGridAlpha;
	scaleMat3(unitmat, params.world_mat, genPreviewSettings.fGridSize * 0.1f);
	if (iGridPlane == 1) // XZ
	{
		pitchMat3(HALFPI, params.world_mat);
	}
	else if (iGridPlane == 2) // YZ
	{
		rollMat3(HALFPI, params.world_mat);
	}
	zeroVec3(params.world_mat[3]);
	params.unlit = true;
	setVec3same(params.color, 1.0f);
	params.dist_offset = -6.0f;
	params.double_sided = true;
	gfxQueueSingleModelTinted(&params, -1);
}

static void partEdDrawGhosts(ParticleDoc* pThisDoc)
{
	Vec3 vTestPos;
	if (genPreviewSettings.bDropNode)
	{
		partEdDropTestNode();
		genPreviewSettings.bDropNode = false;
	}
	if (genPreviewSettings.bIsInScreenSpace)
	{
		dynNodeSetPos(genPreviewSettings.pTestNode, zerovec3);
	}
	dynNodeGetWorldSpacePos(genPreviewSettings.pTestNode, vTestPos);
	{
		Vec3 vOffsetTestPos;
		addVec3(vTestPos, genPreviewSettings.vTestMagnetPos, vOffsetTestPos);
		dynNodeSetPos(genPreviewSettings.pTestMagnetNode, vOffsetTestPos);

		addVec3(vTestPos, genPreviewSettings.vTestEmitTargetPos, vOffsetTestPos);
		dynNodeSetPos(genPreviewSettings.pTestEmitTargetNode, vOffsetTestPos);
	}

	FOR_EACH_IN_EARRAY(eaDocs, ParticleDoc, pDoc)
	if (!pDoc->previewSettings.bDontDraw && dynFxFastParticleInfoInit(pDoc->pInfo) && pDoc->pTestSet)
	{
		if (genPreviewSettings.bResetSet)
		{
			dynFxFastParticleSetReset(pDoc->pTestSet);
		}
		if (pDoc->bRecalculateSet || genPreviewSettings.bResetSet)
		{
			dynFxFastParticleSetRecalculate(pDoc->pTestSet, vTestPos, genPreviewSettings.vTestVelocity);
			dynFxFastParticleFakeVelocityRecalculate(pDoc->pTestSet, genPreviewSettings.vTestVelocity);
			pDoc->bRecalculateSet = false;
		}

		pDoc->pTestSet->fHueShift        = genPreviewSettings.bTestHueShift ? genPreviewSettings.fHueShift * 0.00277777777f : 0.0f;
		pDoc->pTestSet->fSaturationShift = genPreviewSettings.bTestHueShift ? genPreviewSettings.fSaturationShift : 0;
		pDoc->pTestSet->fValueShift      = genPreviewSettings.bTestHueShift ? genPreviewSettings.fValueShift : 0;

		dynFxFastParticleFakeVelocity(pDoc->pTestSet, genPreviewSettings.vTestVelocity, genPreviewSettings.fUpdateRate * gfxGetFrameTime());
		dynFxFastParticleSetUpdate(pDoc->pTestSet, genPreviewSettings.vTestVelocity, genPreviewSettings.fUpdateRate * gfxGetFrameTime(), true, true);
		gfxQueueFastParticleFromEditor(pDoc->pTestSet, genPreviewSettings.bIsInScreenSpace);
	}
	FOR_EACH_END;

	genPreviewSettings.bResetSet = false;


	if (inpLevelPeek(INP_ALT))
	{
		Mat4 mat;
		RotateGizmoUpdate(genPreviewSettings.pRotateGizmo);
		RotateGizmoDraw(genPreviewSettings.pRotateGizmo);
		RotateGizmoGetMatrix(genPreviewSettings.pRotateGizmo, mat);
		dynNodeSetFromMat4(genPreviewSettings.pTestNode, mat);
	}
	else
	{
		Mat4 mat;
		dynNodeGetWorldSpaceMat(genPreviewSettings.pTestNode, mat, false);
		RotateGizmoSetMatrix(genPreviewSettings.pRotateGizmo, mat);
	}

	if (genPreviewSettings.fAxes > 0.0f)
	{
		Mat4 mat;
		dynNodeGetWorldSpaceMat(genPreviewSettings.pTestNode, mat, false);
		InertAxesGizmoDraw(mat, genPreviewSettings.fAxes);
	}

	if ( (pThisDoc->pInfo->fMagnetism || pThisDoc->pInfo->fGoTo || pThisDoc->pInfo->fVelocityOut) && genPreviewSettings.fAxes > 0.0f)
	{
		Mat4 mat;
		dynNodeGetWorldSpaceMat(genPreviewSettings.pTestMagnetNode, mat, false);
		InertAxesGizmoDrawColored(mat, genPreviewSettings.fAxes, 0xFFFF00FF);
	}

	if ( genPreviewSettings.fAxes > 0.0f && !vec3IsZero(genPreviewSettings.vTestEmitTargetPos))
	{
		Mat4 mat;
		dynNodeGetWorldSpaceMat(genPreviewSettings.pTestEmitTargetNode, mat, false);
		InertAxesGizmoDrawColored(mat, genPreviewSettings.fAxes, 0xFFFFFF00);
	}

	if (genPreviewSettings.fGridSize && genPreviewSettings.fGridAlpha)
	{
		int uid = objectLibraryUIDFromObjName("Plane_Doublesided_2000ft");
		if (uid)
		{
			if (genPreviewSettings.bGridXZ)
				partEdDrawGrid(uid, pThisDoc, 0);
			if (genPreviewSettings.bGridXY)
				partEdDrawGrid(uid, pThisDoc, 1);
			if (genPreviewSettings.bGridYZ)
				partEdDrawGrid(uid, pThisDoc, 2);
		}
	}


}

bool partEdCheckOut(ParticleDoc* pDoc)
{
	int		ret;
	if (pDoc->bFileCheckedOut)
		return true;
	if (!pDoc->pcLoadedFilename)
		return true;

	if (!gimmeDLLQueryIsFileLatest(pDoc->pcLoadedFilename)) {
		// The user doesn't have the latest version of the file, do not let them edit it!
		// If we were to check out the file here, the file would be changed on disk, but not reloaded,
		//   and that would be bad!  Someone else's changes would most likely be lost.
		Alertf("Error: file (%s) unable to be checked out, someone else has changed it since you last got latest.  Exit, get latest and reload the file.", pDoc->pcLoadedFilename);
		pDoc->bFileCouldntBeCheckedOut = true;
		return false;
	}
	ret = gimmeDLLDoOperation(pDoc->pcLoadedFilename, GIMME_CHECKOUT, 0);
	if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_NO_DLL) {
		Alertf("Error checking out file \"%s\" (see console for details).", pDoc->pcLoadedFilename);
		gfxStatusPrintf("Check out FAILED on %s", pDoc->pcLoadedFilename);
		return false;
	}
	gfxStatusPrintf("You have checked out: %s", pDoc->pcLoadedFilename);
	pDoc->bFileCheckedOut = true;
	return true;
}


EMTaskStatus partEdSaveAs(ParticleDoc* pDoc);

//AUTO_COMMAND ACMD_LIST(ParticleEditorCmdList);
EMTaskStatus partEdSave(ACMD_IGNORE ParticleDoc* pDoc)
{
	// TODO
	bool bSuccess;
//	GET_ACTIVE_PARTICLE_DOC(pTempDoc);
//	pDoc = pTempDoc;

	if (!pDoc) {
		Alertf("No document was found to save.");
		return EM_TASK_FAILED;
	}

	if (!pDoc->pcLoadedFilename) {
		return partEdSaveAs(pDoc);
	}

	if (!partEdCheckOut(pDoc)) {
		Alertf("Error checking out file \"%s\".  Changes have not been saved (see console for details).", pDoc->pcLoadedFilename);
		return EM_TASK_FAILED;
	}

	// Check for errors
	//if (!pDoc->bValidated)
		//Alertf("%s", "This file appears to have validation errors.  Please do *not* check in this file.");

	fileMakeLocalBackup(pDoc->pcLoadedFilename, 60*60*48);

	bSuccess = ParserWriteTextFileFromSingleDictionaryStruct(pDoc->pcLoadedFilename, "DynParticle", pDoc->pInfo, 0, 0);

	if (!bSuccess) {
		Alertf("Error saving file: %s", pDoc->pcLoadedFilename);
		return EM_TASK_FAILED;
	} else {
		gfxStatusPrintf("Successfully saved %s", pDoc->pcLoadedFilename);
		emDocRemoveAllFiles(&pDoc->base, true);
		emDocAssocFile(&pDoc->base, pDoc->pcLoadedFilename);
	}

	fxbNeedsRebuild();

	pDoc->base.saved = 1;
	return EM_TASK_SUCCEEDED;
	//pDoc->last_autosave = timerCpuSeconds();
}


EMTaskStatus partEdSaveAs(ParticleDoc* pDoc)
{
	char pathname[MAX_PATH];
	char filename[MAX_PATH];
	char topDir[MAX_PATH];
	char startDir[MAX_PATH];

	startDir[0] = 0;

	strcpy(topDir, "dyn/fx");

	if (pDoc->pcLoadedFilename)
	{
		strcpy(startDir, pDoc->pcLoadedFilename);
		getDirectoryName(startDir);
	}

	// Check for errors
	//if (!pDoc->bValidated)
		//Alertf("%s", "This file appears to have validation errors.  Please do *not* check in this file.");

	if (UIOk == ui_ModalFileBrowser("Save Particle As", "Save", UIBrowseNew, UIBrowseFiles, false,
		topDir, startDir, ".part", SAFESTR(pathname), SAFESTR(filename), pDoc->base.doc_name))
	{
		strcatf(pathname, "/%s", filename);
		forwardSlashes(pathname);

		if (!strEndsWith(pathname, ".part")) {
			strcat(pathname, ".part");
		}

		pDoc->pcLoadedFilename = allocAddString(pathname);
		{
			char cName[256];
			getFileNameNoExt(cName, pDoc->pcLoadedFilename);
			strcpy(pDoc->base.doc_display_name, cName);
		}

		return partEdSave(pDoc);
		//matEd2ClearUndoHist(me2Doc); // Cannot undo, otherwise we have a different name!
	}

	genPreviewSettings.bRebuildUI = true;
	return EM_TASK_FAILED;
}



#define WRAPPED_NAME(name) partEd##name##MenuCallback
#define WRAP_FUNCTION(name)	\
	void WRAPPED_NAME(name)(UIMenuItem *item_UNUSED, void *userData_UNUSED)	\
{	\
	ParticleDoc* pDoc = (ParticleDoc*)emGetActiveEditorDoc();	\
	partEd##name(pDoc);	\
}

WRAP_FUNCTION(Save)
WRAP_FUNCTION(SaveAs)
WRAP_FUNCTION(Undo)
WRAP_FUNCTION(Redo)


static void emToolbarAddChildXIt(
								 EMToolbar* toolbar, UIAnyWidget* widget, bool update_width, int* xIt )
{
	emToolbarAddChild( toolbar, widget, update_width );
	*xIt = ((UIWidget*)widget)->x + ((UIWidget*)widget)->width + 5;
}

static void emPanelAddChildYIt(
							   EMPanel* panel, UIAnyWidget* widget, bool update_width, int* yIt )
{
	emPanelAddChild( panel, widget, update_width );
	*yIt = ((UIWidget*)widget)->y + ((UIWidget*)widget)->height + 5;
}

static void partEdInitToolbars(EMEditor* pEditor)
{
	// File toolbar
	eaPush( &pEditor->toolbars, emToolbarCreateDefaultFileToolbar() );
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());


	// Preview toolbar
	{
		EMToolbar* accum = emToolbarCreate( 0 );
		int xIt = 0;
		{
			UILabel* label = ui_LabelCreate( "Preview:", 0, 0 );
			ui_WidgetSetPosition( UI_WIDGET( label ), xIt, 0 );
			emToolbarAddChildXIt( accum, label, true, &xIt );
		}
		{
			UIButton* button = ui_ButtonCreate( "Center", xIt, 0, partEdPreviewRecenterObject, NULL );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( button ), "Recenter the test particles" );
			emToolbarAddChildXIt( accum, button, true, &xIt );
		}
		{
			UIButton* button = ui_ButtonCreate( "Drop", xIt, 0, partEdPreviewDropTestNode, NULL );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( button ), "Drop the test particles in front of the camera" );
			emToolbarAddChildXIt( accum, button, true, &xIt );
		}
		{
			UIButton* button = ui_ButtonCreate( genPreviewSettings.bDrawWorld ? "No World" : "Draw World", xIt, 0, partEdDrawWorld, NULL );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( button ), "Toggle whether to draw the world or not." );
			emToolbarAddChildXIt( accum, button, true, &xIt );
		}
		{
			UIButton* button = ui_ButtonCreate( "Reset All", xIt, 0, partEdResetSet, NULL );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( button ), "Restarts all particle systems." );
			emToolbarAddChildXIt( accum, button, true, &xIt );
		}
		{
			UIButton* button = ui_ButtonCreate( "Origin", xIt, 0, partEdResetNode, NULL );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( button ), "Moves drop point back to origin." );
			emToolbarAddChildXIt( accum, button, true, &xIt );
		}
		eaPush( &pEditor->toolbars, accum );
	}
	// Draw FX toolbar
	{
		EMToolbar* accum = emToolbarCreate( 0 );
		int xIt = 0;
		{
			UILabel* label = ui_LabelCreate( "Draw FX:", 0, 0 );
			ui_WidgetSetPosition( UI_WIDGET( label ), xIt, 0 );
			emToolbarAddChildXIt( accum, label, true, &xIt );
		}
		{
			UIButton* button = ui_ButtonCreate( "All", xIt, 0, partEdSetDrawFX, (void*)eDrawFxType_All );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( button ), "Draw All Test Particles" );
			emToolbarAddChildXIt( accum, button, true, &xIt );
		}
		{
			UIButton* button = ui_ButtonCreate( "None", xIt, 0, partEdSetDrawFX, (void*)eDrawFxType_None );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( button ), "Draw No Test Particles" );
			emToolbarAddChildXIt( accum, button, true, &xIt );
		}
		{
			UIButton* button = ui_ButtonCreate( "Isolate", xIt, 0, partEdSetDrawFX, (void*)eDrawFxType_Isolate );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( button ), "Draw Only This Test Particle" );
			emToolbarAddChildXIt( accum, button, true, &xIt );
		}
		{
			UIButton* button = ui_ButtonCreate( "Toggle", xIt, 0, partEdSetDrawFX, (void*)eDrawFxType_Toggle );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( button ), "Toggle whether to draw this Test Particle or not." );
			emToolbarAddChildXIt( accum, button, true, &xIt );
		}
		eaPush( &pEditor->toolbars, accum );
	}
	// Other toolbar
	{
		EMToolbar* accum = emToolbarCreate( 0 );
		int xIt = 0;
		{
			UILabel* label = ui_LabelCreate( "Other:", 0, 0 );
			ui_WidgetSetPosition( UI_WIDGET( label ), xIt, 0 );
			emToolbarAddChildXIt( accum, label, true, &xIt );
		}
		{
			UICheckButton* checkbutton = ui_CheckButtonCreate(xIt, 0, "AutoSave", genPreviewSettings.bAutoSave);
			ui_CheckButtonSetToggledCallback(checkbutton, partEdToggleAutosave, NULL);
			ui_WidgetSetHeightEx( UI_WIDGET( checkbutton ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( checkbutton ), "Save Particles after any change" );
			emToolbarAddChildXIt( accum, checkbutton, true, &xIt );
		}

		eaPush( &pEditor->toolbars, accum );
	}
}

static void partEdInitEditor(EMEditor *editor)
{
	emMenuItemCreate(editor, "saveall", "Save all particles", NULL, NULL, "partEdSaveAll");
	emMenuItemCreate(editor, "center", "Center camera", NULL, NULL, "partEdCenterCamera");
	emMenuItemCreate(editor, "dropnode", "Drop Test Node", NULL, NULL, "partEdDropTestNode");
	emMenuRegister(editor, emMenuCreate(editor, "File", "saveall", NULL));
	emMenuRegister(editor, emMenuCreate(editor, "View", "center", NULL));

	partEdInitToolbars(editor);

	genPreviewSettings.pTestNode = dynNodeAlloc();
	genPreviewSettings.pTestMagnetNode = dynNodeAlloc();
	genPreviewSettings.pTestEmitTargetNode = dynNodeAlloc();
	partEdResetGeneralPreviewSettings(&genPreviewSettings);

	assert(editor->camera);
	editor->camera->override_no_fog_clip = 1;
}


CmdList ParticleEditorCmdList;

#endif
AUTO_RUN_LATE;
int partEdRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;
	// Setup and register the mission editor
	strcpy(particle_editor.editor_name, "Particle Editor");
	particle_editor.type = EM_TYPE_SINGLEDOC;
	particle_editor.default_type = "part";
	particle_editor.allow_multiple_docs = 1;
	particle_editor.allow_save = 1;
	particle_editor.hide_world = 1;
	particle_editor.do_not_reload = 1;
	particle_editor.keybind_version = 1;

	particle_editor.init_func = partEdInitEditor;
	particle_editor.new_func = partEdNewDoc;
	particle_editor.load_func = partEdLoadDoc;
 	particle_editor.close_func = (EMEditorDocFunc)partEdCloseDoc;
 	particle_editor.save_func = (EMEditorDocStatusFunc)partEdSave;
	particle_editor.save_as_func = (EMEditorDocStatusFunc)partEdSaveAs;
 	particle_editor.draw_func = (EMEditorDocFunc)partEdDrawDoc;
 	particle_editor.ghost_draw_func = (EMEditorDocGhostDrawFunc)partEdDrawGhosts;
	particle_editor.use_em_cam_keybinds = 1;
// 	particle_editor.lost_focus_func = partEdLostFocus;
	particle_editor.got_focus_func = (EMEditorDocFunc)partEdGotFocus;
// 	particle_editor.object_dropped_func = partEdObjectSelected;

 	particle_editor.keybinds_name = "ParticleEditor";
// 	particle_editor.keybinds.pCmdList = &ParticleEditorCmdList;
//	particle_editor.keybinds.bTrickleCommands = 1;
//	particle_editor.keybinds.bTrickleKeys = 1;
// 	particle_editor.keybindsName = "ParticleEditor";

//	eaPush(&particle_editor.gimme_groups, "Art");
//	eaPush(&particle_editor.gimme_groups, "Software");

	fxbRegister(&particle_editor);

	emRegisterEditor(&particle_editor);
	emRegisterFileType("part", "Particle", "Particle Editor");

	return 1;
#else
	return 0;
#endif
}


#include "FxParticleEditor_h_ast.c"
