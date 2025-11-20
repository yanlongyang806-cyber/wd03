#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"
#include "MultiEditField.h"
#include "dynMove.h"
#include "AnimEditorCommon.h"

#define MME_MOVEEDITOR allocAddString("Move Multi-Editor")
#define MME_NEWMOVENAME allocAddString("NewMove")
#define MME_NEWSEQNAME allocAddString("NewSeq")
#define MME_NEWANIMTRACKNAME allocAddString("NewAnimTrack")

typedef struct UIWindow UIWindow;
typedef struct UIExpanderGroup UIExpanderGroup;

typedef struct MoveMultiDoc MoveMultiDoc;
typedef struct MoveDoc MoveDoc;


typedef struct MMEMUndoData
{
	MoveDoc *pDoc;
	DynMove* pPreObject;
	DynMove* pPostObject;
} 
MMEMUndoData;

AUTO_ENUM;
typedef enum MMEMFilterType
{
	mmeFilterType_Text,		ENAMES("Text")
	mmeFilterType_Number,	ENAMES("Number")
	mmeFilterType_Bit,		ENAMES("Bit")
} MMEMFilterType;

AUTO_ENUM;
typedef enum MMEMFilterOp
{
	mmeFilterOp_Add,	ENAMES("Add")
	mmeFilterOp_Remove,	ENAMES("Remove")
	mmeFilterOp_Same,	ENAMES("Don't Change")
} MMEMFilterOp;

AUTO_ENUM;
typedef enum MMEMBits
{
	mmeBit_RegisterWep,			ENAMES("Register Wep")
	mmeBit_MeleeMode,			ENAMES("Melee Mode")
	mmeBit_EnableSliding,		ENAMES("Enable Sliding")
	mmeBit_DisableLeftWrist,	ENAMES("Disable Left Wrist")
	mmeBit_DisableRightArm,		ENAMES("Disable Right Arm")
	mmeBit_Ragdoll,				ENAMES("Ragdoll")
	mmeBit_Message,				ENAMES("Message")
	mmeBit_IKBothHands,			ENAMES("IK Both Hands")
	mmeBit_NoInterp,			ENAMES("No Interp.")
} MMEMBits;

AUTO_STRUCT;
typedef struct MMEMFilter
{
	MMEMFilterType	eMoveFilterType;
	const char		*pcMoveFilterText;
	F32				fMoveFilterValue;
	MMEMBits		eMoveFilterBit;
}
MMEMFilter;
extern ParseTable parse_MMEMFilter[];
#define TYPE_parse_MMEMFilter MMEMFilter

AUTO_STRUCT;
typedef struct MoveMultiDoc
{
	EMEditorDoc	emDoc;				NO_AST
	MoveDoc		**eaMoveDocs;		NO_AST
	MoveDoc     **eaSortedMoveDocs;	NO_AST
	const char	**eaFileNames;		NO_AST
	const char  **eaMoveNames;		NO_AST
	const char  **eaMoveSeqNames;	NO_AST

	bool bOneTimeSortWindow;

	MMEMFilterOp eMoveFilterPresentOp;
	MMEMFilterOp eMoveFilterAbsentOp;
	MMEMFilter   **eaFilters;

	DynMoveFxEvent **eaFxClipBoard;

	AnimEditor_CostumePickerData costumeData;	NO_AST
	MoveDoc    *pVisualizeMoveDoc;		NO_AST
	DynMove    *pVisualizeMove;			NO_AST
	DynMove    *pVisualizeMoveOrig;		NO_AST
	DynMoveSeq *pVisualizeMoveSeq;		NO_AST
	DynMoveSeq *pVisualizeMoveSeqOrig;	NO_AST
	const char *pcVisualizeMove;
	const char *pcVisualizeMoveSeq;
	F32 fVisualizeFrame;
	F32 fVisualizeFirstFrame;
	F32 fVisualizeLastFrame;
	bool bVisualizeCostumePicked;
	bool bVisualizePlaying;
	bool bVisualizeLoop;

	UIWindow		*pMainWindow;		NO_AST
	UIExpanderGroup	*pExpanderGroup;	NO_AST

	EMPanel *pAllFilesPanel;	NO_AST
	EMPanel *pFiltersPanel;		NO_AST
	EMPanel *pMovesPanel;		NO_AST
	EMPanel *pFxClipboardPanel;	NO_AST
	EMPanel *pVisualizePanel;	NO_AST
	EMPanel *pGraphsPanel;		NO_AST
	EMPanel *pSearchPanel;		NO_AST
}
MoveMultiDoc;
extern ParseTable parse_MoveMultiDoc[];
#define TYPE_parse_MoveMultiDoc MoveMultiDoc

AUTO_STRUCT;
typedef struct MoveDoc
{
	EMEditorDoc emDoc;		NO_AST
	MoveMultiDoc *pParent;	NO_AST

	UIExpander *pExpander;	NO_AST
	UISkin *pExpanderSkin;	NO_AST

	DynMove* pOrigObject;
	DynMove* pObject;
	DynMove* pNextUndoObject;

	bool bIsSaving;
	EMTaskStatus saveStatus;	NO_AST

	bool bIsDeleting;
	bool bEnableDelete;
	const char *pcDeleteScope;	NO_AST
	EMTaskStatus deleteStatus;	NO_AST

	bool bIsSelected;
} MoveDoc;
extern ParseTable parse_MoveDoc[];
#define TYPE_parse_MoveDoc MoveDoc

MoveDoc* MMEOpenMove(EMEditor *pEditor, MoveMultiDoc *pParent, const char *pcName);
void MMERevertDoc(MoveDoc *pDoc);
void MMEDuplicateDoc(MoveDoc *pDoc);
void MMECloseMove(MoveDoc *pDoc);
void MMEOncePerFrameStart(MoveMultiDoc *pDoc);
void MMEOncePerFrameEnd(MoveMultiDoc *pDoc);
bool MMEOncePerFramePerDoc(MoveDoc *pDoc);
void MMEInitCustomToolbars(EMEditor *pEditor);
void MMEGotFocus(void);
void MMELostFocus(void);
void MMEDeleteDoc(MoveDoc *pDoc);
void MMESaveDoc(MoveDoc *pDoc);
void MMEContentDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData);
void MMEDrawGhost(MoveMultiDoc *pDoc);

EMPanel* MMEInitSearchPanel(MoveMultiDoc* pDoc);

#endif
