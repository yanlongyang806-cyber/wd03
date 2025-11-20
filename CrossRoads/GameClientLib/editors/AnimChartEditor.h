#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#ifndef NO_EDITORS

#include "AnimEditorCommon.h"
#include "EditorManager.h"
#include "MultiEditField.h"

typedef struct MEField MEField;
typedef struct UITextEntry UITextEntry;
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UILabel UILabel;
typedef struct UIWindow UIWindow;
typedef struct AnimChartDoc AnimChartDoc;


typedef struct DynAnimChartLoadTime DynAnimChartLoadTime;


#define ANIMCHART_EDITOR "Animation Chart Editor"
#define DEFAULT_ANIMCHART_NAME     "New_Animation_Chart"



// ---- Animation Chart Editor ----

typedef struct ACEUndoData 
{
	DynAnimChartLoadTime* pPreObject;
	DynAnimChartLoadTime* pPostObject;
} ACEUndoData;

typedef struct ACEGraphRefField
{
	AnimChartDoc *pDoc;
	UITextEntry *pEntry;
	int iIndex;
} ACEGraphRefField;

typedef struct WleUIRandomGraphWin
{
	AnimChartDoc* pDoc;
	UIWindow *pWindow;
	U32 uiRefIndex;
	UIButton *pOKButton;
	DynAnimChartLoadTime *pPreOpenBackup;
	void *pFakePtr;

} WleUIRandomGraphWin;

typedef struct AnimChartDoc
{
	EMEditorDoc emDoc;

	DynAnimChartLoadTime* pOrigObject;
	DynAnimChartLoadTime* pObject;
	DynAnimChartLoadTime* pNextUndoObject;

	bool bIgnoreFieldChanges;
	bool bIgnoreFilenameChanges;
	WleUIRandomGraphWin *pRandomGraphWindow;
	WleUIRandomGraphWin *pRandomMoveWindow;
	WleUIRandomGraphWin *pMoveTransitionWindow;

	// Standalone main window controls
	UIExpanderGroup *pExpanderGroup;
	UIExpander *pPropsExpander;
	UIExpander *pStanceWordsExpander;
	UIExpander *pGraphsExpander;
	UIExpander *pSubChartsExpander;
	UIExpander *pMovesExpander;
	UIExpander *pMoveTransitionsExpander;
	UIExpander *pStanceChartsExpander;
	UIWindow *pMainWindow;
	UILabel *pFilenameLabel;
	UIGimmeButton *pFileButton;
	UIScrollArea *pMatrixExteriorScroll;

	// Info Expander
	UILabel *pTypeLabel;
	UILabel *pCommentLabel;
	MEField *pTypeField;
	MEField *pCommentField;

	// Props Expander
	//InteractionPropertiesGroup *pPropsGroup;

	EMPanel *pSearchPanel;

	// Simple fields
	MEField **eaDocFields;
	ACEGraphRefField **eaGraphRefFields;

} AnimChartDoc;

AnimChartDoc *ACEOpenAnimChart(EMEditor *pEditor, char *pcName, DynAnimChartLoadTime *pChartIn);
void ACERevertAnimChart(AnimChartDoc *pDoc);
void ACECloseAnimChart(AnimChartDoc *pDoc);
EMTaskStatus ACESaveAnimChart(AnimChartDoc* pDoc, bool bSaveAsNew);
void ACEInitData(EMEditor *pEditor);
void ACEOncePerFrame(AnimChartDoc* pDoc);
void ACEGotFocus(AnimChartDoc* pDoc);
void ACELostFocus(AnimChartDoc* pDoc);
void ACEDuplicateDoc(UIButton *button, UserData uData);

#endif
