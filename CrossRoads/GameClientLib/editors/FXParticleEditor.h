#pragma once
GCC_SYSTEM
#ifndef NO_EDITORS

#include "EditorManager.h"

extern ParseTable parse_ParticlePreviewSettings[];
#define TYPE_parse_ParticlePreviewSettings ParticlePreviewSettings

void fxbNeedsRebuild(void);
void fxbRegister(EMEditor *editor);

#endif

typedef struct DynFxFastParticleInfo DynFxFastParticleInfo;
typedef struct DynFxFastParticleSet DynFxFastParticleSet;
typedef struct DynNode DynNode;
typedef struct UIWindow UIWindow;
typedef struct UIRebuildableTree UIRebuildableTree;
typedef struct RotateGizmo RotateGizmo;


AUTO_STRUCT;
typedef struct ParticlePreviewSettings
{
	bool bDontDraw; AST(BOOLFLAG)
	F32 fMin[8]; AST( AUTO_INDEX(min))
	F32 fMax[8]; AST( AUTO_INDEX(max))
} ParticlePreviewSettings;

AUTO_STRUCT;
typedef struct GeneralParticlePreviewSettings
{
	F32 fUpdateRate;
	Vec3 vTestVelocity;
	Vec3 vTestMagnetPos;
	Vec3 vTestEmitTargetPos;
	F32 fGraphHeight;
	F32 fOpacity;
	F32 fAxes;
	F32 fGridSize;
	F32 fGridAlpha;
	F32 fHueShift;
	F32 fSaturationShift;
	F32 fValueShift;
	F32 fScaleFactor;
	bool bTestHueShift; AST(BOOLFLAG)
	bool bGridXZ; AST(BOOLFLAG)
	bool bGridXY; AST(BOOLFLAG)
	bool bGridYZ; AST(BOOLFLAG)
	bool bDrawWorld; AST(BOOLFLAG)
    bool bIsInScreenSpace; AST(BOOLFLAG)
	bool bRebuildUI;
	bool bDropNode;
	bool bResetSet;
	bool bAutoSave;

	DynNode* pTestNode;
	DynNode* pTestMagnetNode;
	DynNode* pTestEmitTargetNode;

	RotateGizmo* pRotateGizmo; NO_AST
} GeneralParticlePreviewSettings;
#ifndef NO_EDITORS

typedef struct ParticleEditorPanel
{
	EMPanel *panel;
	UIScrollArea *scroll_area;
	UIRebuildableTree *auto_widget;
} ParticleEditorPanel;

#define PART_NUM_GRAPHS 8

typedef struct ParticleDoc
{
	EMEditorDoc base;

	DynFxFastParticleInfo* pInfo;

	DynFxFastParticleInfo* pBackup;

	// Preview info
	DynFxFastParticleSet* pTestSet;

	UIRebuildableTree* pUITree;
	bool bRecalculateSet;
	bool bSetNeedsRecreation;
	ParticlePreviewSettings previewSettings;

	// UI
	ParticleEditorPanel preview;
	ParticleEditorPanel texture;
	ParticleEditorPanel emission;
	ParticleEditorPanel movement;
	ParticleEditorPanel graphs[PART_NUM_GRAPHS];
	ParticleEditorPanel streakScale;

	const char* pcLoadedFilename;
	//bool bValidated;
	bool bFileCheckedOut;
	bool bFileCouldntBeCheckedOut;
} ParticleDoc;

#endif