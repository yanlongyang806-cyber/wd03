#pragma once
GCC_SYSTEM
//// The Material Editor lets you edit material-templates using a
//// node-based system, and then customize specific instantiations of
//// that material which can actually be bound to pixel shaders
//// in-game.
#ifndef _MATERIALEDITOR2EM_H
#define _MATERIALEDITOR2EM_H

#ifndef NO_EDITORS

#include"EditLibUndo.h"
#include"ReferenceSystem.h"
#include"SystemSpecs.h"

typedef struct BasicTexture BasicTexture;
typedef struct EditUndoStack EditUndoStack;
typedef struct Mated2EditorDoc Mated2EditorDoc;
typedef struct Mated2Guide Mated2Guide;
typedef struct Mated2Node Mated2Node;
typedef struct MaterialData MaterialData;
typedef struct MaterialFallback MaterialFallback;
typedef struct MaterialGraphicPropertiesLoadTime MaterialGraphicPropertiesLoadTime;
typedef struct MaterialWorldPropertiesLoadTime MaterialWorldPropertiesLoadTime;
typedef struct ShaderGraph ShaderGraph;
typedef struct ShaderGuide ShaderGuide;
typedef struct ShaderOperationDef ShaderOperationDef;
typedef struct UIFlowchart UIFlowchart;
typedef struct UISkin UISkin;
typedef struct UIWidget UIWidget;

typedef struct Mated2NodeCreateAction {
	char* nodeName;
	Vec2 nodePos;
	REF_TO(const ShaderOperationDef) opDef;
} Mated2NodeCreateAction;

void mated2NodeCreateActionCreate(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeCreateAction* action );
void mated2NodeCreateActionRemove(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeCreateAction* action );
void mated2NodeCreateActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeCreateAction* action );

SA_ORET_NN_VALID ShaderGraph* mated2DocActiveShaderGraph( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
SA_ORET_NN_VALID ShaderGuide*** mated2DocActiveShaderGuide( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
SA_ORET_NN_VALID MaterialFallback* mated2DocActiveFallback( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
int mated2DocActiveFallbackIndex( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
int mated2DocFallbackIndexByFeatures( SA_PARAM_NN_VALID Mated2EditorDoc* doc, ShaderGraphFeatures sgfeat );
SA_ORET_NN_VALID MaterialData* mated2DocMaterialData( SA_PARAM_NN_VALID Mated2EditorDoc* doc );

SA_ORET_NN_VALID MaterialGraphicPropertiesLoadTime* mated2DocGfxProperties(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc );
SA_ORET_NN_VALID MaterialWorldPropertiesLoadTime* mated2DocWorldProperties(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc );
SA_ORET_NN_VALID Mated2Guide*** mated2ShaderGuides( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
SA_ORET_NN_VALID UIFlowchart* mated2Flowchart( SA_PARAM_NN_VALID Mated2EditorDoc* doc );

bool mated2IsLoading( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
bool mated2DocIsTemplate( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
bool mated2DocIsOneOff( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
bool mated2DocActiveShaderGraphIsEditable( SA_PARAM_NN_VALID Mated2EditorDoc* doc );

bool mated2SupportedSwizzle( SA_PRE_NN_RELEMS(4) const U8 swizzle[ 4 ]);
int mated2SwizzleOffset( SA_PRE_NN_RELEMS(4) const U8 swizzle[ 4 ]);

Mated2Node* mated2FindNodeByName(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* name );
SA_RET_NN_STR char* mated2UniqueNodeName(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* opName,
		char* buffer, int buffer_size );
bool mated2EnsureCheckout( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
bool mated2EnsureCheckoutFile( SA_PARAM_NN_STR const char* filename );
bool mated2EnsureCheckoutFiles( const char** filenames );
void mated2SetDirty( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
void mated2SetInputListNeedsReflow( SA_PARAM_NN_VALID Mated2EditorDoc* doc, bool needsReflow );

const char** mated2NodeNamesUsingOpDefInDefault(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID const ShaderOperationDef* opDef );
void mated2DocNodeRename(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* from,
		SA_PARAM_NN_STR const char* to );
void mated2DocSetInstancedNode(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc,
		SA_PARAM_OP_VALID const Mated2Node* node );

// undo/redo interface
void mated2UndoBeginGroup( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
void mated2UndoEndGroup( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
void mated2UndoCancelUnfisihedGroups( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
void mated2UndoRecord( SA_PARAM_NN_VALID Mated2EditorDoc* doc, EditUndoCustomFn undoFn,
					   EditUndoCustomFn redoFn, EditUndoCustomFreeFn freeFn,
					   void* userData );

// preview helpers
SA_ORET_OP_VALID Mated2Node* mated2PreviewSelectedNode( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
void mated2PreviewSetSelectedNode(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_OP_VALID Mated2Node* node );
void mated2EditorPreviewSetObject(
		SA_PARAM_NN_STR const char* objectName, SA_PARAM_NN_STR const char* fname );
void mated2EditorPreviewSetObjectDefault( void );
void mated2MaterialPickerPreview(
		SA_PARAM_NN_STR const char* materialPath,
		SA_PRE_NN_FREE SA_POST_NN_VALID BasicTexture** outTex,
		SA_PRE_NN_FREE SA_POST_NN_VALID Color* outModColor );
void mated2MaterialPickerPreviewClear(void);
Color mated2MaterialPickerColor( SA_PARAM_NN_STR const char* path, bool isSelected );

// skins
SA_ORET_NN_VALID UISkin* mated2NodeSkin(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, int shaderType, bool isPreview, bool isError );

// misc
SA_ORET_NN_STR const char* mated2TranslateOpName( const char* opName );

#endif

#endif
