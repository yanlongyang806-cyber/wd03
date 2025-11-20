#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#define MOVE_EDITOR "Move Editor"

#include "dynSeqData.h"
#include "gclCostumeView.h"
#include "dynDraw.h"
#include "dynSkeleton.h"
#include "EditorManager.h"
#include "CostumeCommon.h"
#include "CostumeCommonTailor.h"
#include "AnimEditorCommon.h"
#include "wlCostume.h"

typedef struct MoveEditDoc MoveEditDoc;
typedef struct MEField MEField;
typedef struct DynMove DynMove;
typedef struct DynMoveSeq DynMoveSeq;


// ---- Public Interface ------------------------------------------------------

// Called in order to initialize the UI and create all necessary panels
void MoveEditor_CreateUI(MoveEditDoc *pDoc);

// This is called to save the Sequence being edited
EMTaskStatus MoveEditor_SaveMove(MoveEditDoc *pDoc, bool bSaveAsNew);

// This is called to close the Sequence being edited
void MoveEditor_CloseMove(MoveEditDoc *pDoc);

// This is called prior to close to see if the Sequence can be closed
bool MoveEditor_CloseCheck(MoveEditDoc *pDoc);

// These are called for focus changes
void MoveEditor_GotFocus(MoveEditDoc *pDoc);
void MoveEditor_LostFocus(MoveEditDoc *pDoc);

// This is called once to initialize global data
void MoveEditor_InitData(EMEditor *pEditor);

// Tells if a given Sequence is already open
bool MoveEditorEMIsDocOpen(char *pcName);

// Gets a document if it is open and NULL otherwise
MoveEditDoc *MoveEditorEMGetOpenDoc(const char *pcName);

// Gets all open documents
void MoveEditorEMGetAllOpenDocs(MoveEditDoc ***peaDocs);

// Gets the editor
EMEditor *MoveEditorEMGetEditor(void);

// ---- Graphics Control ------------------------------------------------------

// This is called during the per-frame drawing of ghosts
void MoveEditor_DrawGhosts(MoveEditDoc *pDoc);

// This is called during the per-frame drawing
void MoveEditor_Draw(MoveEditDoc *pDoc);

// ---- Editor Document -------------------------------------------------------

#define DEFAULT_MOVE_NAME     "New_Move"
#define DEFAULT_MOVE_FILE     "NoFileChosen"

typedef struct MoveEditSeqPanel {
	UITextEntry *firstFrameEntry;
	UITextEntry *lastFrameEntry;
	UISliderTextEntry *currentFrame;
} MoveEditSeqPanel;

typedef struct MoveEditDoc {
	// NOTE: This must be first for EDITOR MANAGER
	EMEditorDoc emDoc;

	// The current costume data
	AnimEditor_CostumePickerData costumeData;

	// The current Sequence
	char moveName[255];
	DynMove *pMove;

	// Save and Close State Flags
	const char* pOrigFileAssociation;
	const char* pOrigRefName;
	bool bSaved;
	bool bSaveRename;
	bool bSaveOverwrite;
	bool bNewMove;
	bool bGrid;
	Vec2 gridPos;
	F32 gridDirectionAngle;

	// Animation variables
	bool bHasPickedCostume;
	bool animate;
	bool loop;
	UISliderTextEntry *activeSlider;	// used to display relative position in the animTrack to the user when animating
	DynMoveSeq *moveSeq;
	F32 currentFrame;

	// Panel variables
	void **panelData;
} MoveEditDoc;

#endif