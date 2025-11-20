GCC_SYSTEM

#ifndef NO_EDITORS

#include"MaterialEditor2EM.h"
#include"MaterialEditor2EM_c_ast.h"

#include"EditLibGizmos.h"
#include"EditorManager.h"
#include"EditorManagerUtils.h"
#include"GfxCommandParse.h"
#include"GfxDebug.h"
#include"GfxHeadshot.h"
#include"GfxMaterials.h"
#include"GfxSpriteText.h"
#include"GfxTexAtlas.h"
#include"GfxTextureTools.h"
#include"MaterialEditor2EMInput.h"
#include"MaterialEditor2EMNode.h"
#include"ObjectLibrary.h"
#include"Prefs.h"
#include"RdrShader.h"
#include"RdrState.h"
#include"StaticWorld/group.h"
#include"UIInternal.h"
#include"UnitSpec.h"
#include"WorldEditorClientMain.h"
#include"WorldEditorUI.h"
#include"XRenderLib.h"
#include"fileutil.h"
#include"gimmeDLLWrapper.h"
#include"sysutil.h"
#include"winutil.h"
#include"wlCostume.h"
#include"wlModel.h"
#include"ResourceInfo.h"
#include"ResourceManagerUI.h"
#include"Message.h"

typedef enum Mated2PreviewMode Mated2PreviewMode;
typedef struct Mated2Editor Mated2Editor;
typedef struct Mated2EditorDoc Mated2EditorDoc;

/// Editor interface
static void mated2EditorInit( SA_PARAM_NN_VALID Mated2Editor* editor );
static char* mated2PickerPathFilter( SA_PARAM_NN_STR const char* path );
void mated2ForceAllowTemplateEditing( void );
static void mated2EditorPreviewWindowInit( void );
static bool mated2EditorPreviewWindowClosed( SA_PARAM_NN_VALID RdrDevice* device, void* ignored );
static void mated2EditorPreviewWindowOncePerFrame( SA_PARAM_NN_VALID RdrDevice* device, void* ignored );
static void mated2EditorPreviewSetMode( Mated2PreviewMode previewMode );
static void mated2EditorPreviewModeChanged(
		SA_PARAM_NN_VALID UIComboBox* comboBox, int value, SA_PARAM_NN_VALID Mated2Editor* editor );
static void mated2EditorCmdParse(
		SA_PARAM_NN_VALID UIButton* button, SA_PARAM_NN_STR const char* command );
static void mated2EditorObjectPicker(
		SA_PARAM_NN_VALID UIButton* button, SA_PARAM_NN_VALID Mated2Editor* editor );
static bool mated2EditorObjectPicked(
		EMPicker* picker, EMPickerSelection** selections, void* ignored );
static void mated2EditorPreviewRecenterObject(
		UIButton* button, SA_PARAM_NN_VALID Mated2Editor* editor );
static void mated2EditorRotateObjectToggle(
		SA_PARAM_NN_VALID UIButton* button, SA_PARAM_NN_VALID Mated2Editor* editor );
static bool mated2EditorWarnIfTemplateReferenced(
		SA_PARAM_NN_STR const char* templateName,
		SA_PARAM_OP_VALID Mated2EditorDoc* docToIgnore );

/// Doc interface
SA_RET_NN_VALID static Mated2EditorDoc* mated2DocAlloc( void );
SA_RET_OP_VALID static Mated2EditorDoc* mated2DocNewDispatcher(
		SA_PARAM_NN_STR const char* type, SA_PRE_OP_ELEMS(0) SA_POST_OP_VALID void* ignored );
SA_RET_OP_VALID static Mated2EditorDoc* mated2DocNewTemplate(
		SA_PARAM_NN_STR const char* type, SA_PRE_OP_ELEMS(0) SA_POST_OP_VALID void* ignored );
SA_RET_NN_VALID static Mated2EditorDoc* mated2DocNewMaterial(
		SA_PARAM_NN_STR const char* type, SA_PRE_OP_ELEMS(0) SA_POST_OP_VALID void* ignored );
SA_RET_OP_VALID static Mated2EditorDoc* mated2DocLoad(
		SA_PARAM_NN_STR const char* name, SA_PARAM_NN_STR const char* type );
static EMTaskStatus mated2DocSave( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static EMTaskStatus mated2DocSaveAs( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static EMTaskStatus mated2DocAutosave( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static const char* mated2DocSaveDefaultDir(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, char* buffer, int buffer_size );
static EMTaskStatus mated2DocSave1(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, bool forceSaveAs, bool isAutosave,
		bool isMaterialMeasured );
static void mated2DocClose( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocOncePerFrame( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocDrawGhosts( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocPerformanceOncePerFrame( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocValidateOncePerFrame( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2ValidateMaterialByName(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* materialName );
static Mated2EditorDoc* callbackContext;
static void mated2ValidateCallback( SA_PARAM_NN_VALID ErrorMessage* errMsg, void *userdata);
static void mated2DocGotFocus( SA_PARAM_OP_VALID Mated2EditorDoc* doc );
static void mated2DocLostFocus( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocSetAutosave( Mated2EditorDoc* doc, bool isAutosave );
static void mated2DrawPreview( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocTemplateCreateEmptyMaterial( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocErrorf( SA_PARAM_NN_VALID Mated2EditorDoc* doc, const char* fmt, ... );
static void mated2UpdateDependantMaterial(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* filename );
static bool mated2UpdateFallbacksDependantMaterial(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* filename );
static bool mated2ApplyRenamesDependantMaterial(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* filename );
static bool mated2QueryOverrideFallbacks( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static char** mated2AllWhoUseMe( SA_PARAM_NN_VALID Mated2EditorDoc* doc, bool includeUseByFallback );
static char** mated2AllWithSuspiciousBrightness( void );
static char** mated2AllMaterials( void );
static const char** mated2AllMaterialTemplates( void );


static void mated2ViewPaneTick( SA_PARAM_NN_VALID UIPane* pane, UI_PARENT_ARGS );
static void mated2SubgraphPreviewWarningTick( SA_PARAM_NN_VALID UILabel* label, UI_PARENT_ARGS );
static void mated2NodeViewHideShowClicked(
		SA_PARAM_OP_VALID UIButton* ignored, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2NodeViewExpandDrag(
		SA_PARAM_NN_VALID UIButton* button, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocCopyLinks(
		SA_PARAM_NN_VALID UIButton* button, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static const char** mated2DocShaderInputNameList( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static Mated2Node** mated2DocNodesSortedByGroup( SA_PARAM_NN_VALID Mated2EditorDoc* doc );

/// Doc panels...
static void mated2PanelCreateTemplateProperties( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2PanelCreateTemplateFeatures( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2PanelCreateMaterialProperties( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2PanelCreateMaterialFallbacks( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2PanelCreateLinks( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2PanelCreateTemplateNodeList( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2NodeListFillRoot( SA_PARAM_NN_VALID UITreeNode* root, SA_PARAM_OP_VALID UserData ignored );
static void mated2NodeListFillCategory( SA_PARAM_NN_VALID UITreeNode* node, UserData category );
static void mated2PanelCreateMaterialInputList( SA_PARAM_NN_VALID Mated2EditorDoc* doc );

/// General utilities
static void emToolbarAddChildXIt(
		SA_PARAM_NN_VALID EMToolbar* toolbar, SA_PARAM_NN_VALID UIAnyWidget* widget,
		bool update_width, SA_PARAM_NN_VALID int* xIt );
static void emPanelAddChildYIt(
		SA_PARAM_NN_VALID EMPanel* panel, SA_PARAM_NN_VALID UIAnyWidget* widget,
		bool update_width, SA_PARAM_NN_VALID int* yIt );
static void mated2RepopulateNodeView(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc,
		SA_PARAM_NN_VALID const MaterialGraphicPropertiesLoadTime* gfxProps,
		SA_PARAM_NN_VALID const MaterialWorldPropertiesLoadTime* worldProps );
static void mated2RepopulateInputList( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2RepopulateLinks( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2RepopulatePanels( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static bool mated2FlowchartNodeRemoveRequest(
		SA_PARAM_NN_VALID UIFlowchart* flowchart, SA_PARAM_NN_VALID UIFlowchartNode* uiNode,
		SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static bool mated2FlowchartNodeRemoved(
		SA_PARAM_NN_VALID UIFlowchart* flowchart, SA_PARAM_NN_VALID UIFlowchartNode* uiNode,
		SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static int mated2MostExpensiveLightTypes(
		SA_PARAM_NN_VALID ShaderGraph* graph, SA_PRE_NN_FREE SA_POST_NN_VALID RdrLightType* light1, SA_PRE_NN_FREE SA_POST_NN_VALID RdrLightType* light2,  SA_PRE_NN_FREE SA_POST_NN_VALID RdrLightShaderMask * pMostExpensiveLightCombo );

/// Template utils
static void mated2AddNodeBySelection( SA_PRE_OP_ELEMS(0) SA_POST_OP_VALID UIButton* ignored,
									  SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2AddGuide( SA_PRE_OP_ELEMS(0) SA_POST_OP_VALID UIButton* ignored,
							SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static bool mated2FinishLoadTemplate( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
SA_RET_OP_VALID static Mated2Node* mated2FindNodeByNameInGraph(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* name,
		SA_PARAM_NN_VALID const ShaderGraph* graph );
static void mated2TemplatePropertyChanged(
		SA_PARAM_NN_VALID UICheckButton* property, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2TemplateFeatureChanged(
		SA_PARAM_NN_VALID UICheckButton* feature, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2TemplateFeatureReset(
		SA_PARAM_NN_VALID UIButton* reset, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2MaterialPropertyChanged(
		SA_PARAM_NN_VALID UICheckButton* property, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static ShaderGraphFeatures mated2TemplateDefaultGraphFeature(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, bool isSynchronous );

/// Instancing utils
static void mated2DocInstancedNodeChanged(
		SA_PARAM_NN_VALID UIComboBox* comboBox, SA_PARAM_NN_VALID Mated2EditorDoc* doc );

typedef struct Mated2SetInstancedNodeAction {
	char* oldInstancedNodeName;
	char* newInstancedNodeName;
} Mated2SetInstancedNodeAction;
static void mated2SetInstancedNodeActionUndo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2SetInstancedNodeAction* action );
static void mated2SetInstancedNodeActionRedo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2SetInstancedNodeAction* action );
static void mated2SetInstancedNodeActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2SetInstancedNodeAction* action );

/// Material utils
static bool mated2FinishLoadMaterial( SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2SelectTemplate(
		SA_PARAM_NN_VALID UIButton* button, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static bool mated2SelectTemplateSelected(
		SA_PARAM_NN_VALID EMPicker* picker, SA_PARAM_NN_VALID EMPickerSelection** selections,
		SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2SetTemplate(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* name );
static void mated2SelectPhysicalProperties(
		SA_PARAM_NN_VALID UIButton* button, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2SetPhysicalProperties(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* name );
static int mated2MaterialValidate(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID MaterialData* material,
		bool bRepair );
static void mated2DocReflectionTypeChanged(
	SA_PARAM_NN_VALID UIComboBox* comboBox, int selected, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocShaderQualityChanged(
	SA_PARAM_NN_VALID UIComboBox* comboBox, int selected, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocMaxReflectionResolutionChanged(
	SA_PARAM_NN_VALID UIComboBox* comboBox, int selected, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocMaxReflectionResolutionHover(
	SA_PARAM_NN_VALID UIComboBox* comboBox, int selected, SA_PARAM_NN_VALID Mated2EditorDoc* doc );

/// Fallback utils
static void mated2DocFallbackAddBefore(
		UIButton* ignored, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocFallbackRemove(
		UIButton* ignored, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocFallbackRaisePriority(
		UIButton* ignored, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocFallbackLowerPriority(
		UIButton* ignored, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocFallbackClearSelection(
		UIButton* ignored, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocFallbackApplyDefaults(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID MaterialFallback* fallback );
static void mated2DocFallbackSelectedSetActive(
		UIButton* ignored, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2DocSetActiveFallback(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID MaterialFallback* fallback );
static bool mated2DocHasFallback(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* fallback_name );

/// Info window
static void mated2InfoWinMemoryTotal(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinMemoryPrivate(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static U32 mated2ScoreColor( int score );
static void mated2InfoWinPerfScore(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinPerfALU(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinPerfTexFetch(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinPerfTemp(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinPerfParam(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinPerfNVPerfHUD(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinInstructionCountMaxLights(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinInstructionCount2Lights(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinInstructionCountSM20(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinConstCountMaxLights(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinConstCount2Lights(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );
static void mated2InfoWinConstCountSM20(
		SA_PARAM_OP_STR const char* ignored, SA_PARAM_NN_VALID EMInfoWinText*** lines );

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define MATERIAL_EDITOR "Material Editor"
#define MATERIAL_EDITOR_PREVIEW_WINDOW "MaterialEditor\\PreviewWindow"

#define MATED2_DEFAULT_PREVIEW_OBJECT_NAME "MatED_Primitives_Organic"
#define MATED2_DEFAULT_PREVIEW_FILENAME "object_library/system/matED/matED_primitives.lego"


/// The skins used in the editor
UISkin mated2ShaderTypeSkin[ SOT_END ];

/// The skin used by the main pane
static UISkin mated2ViewPaneSkin;

/// The colors used for each skin
static const Color shaderTypeBgColor[ SOT_END ] = {
	{ 191, 126, 189, 255 },		//< custom   -- poppin' purple
	{ 126, 188, 191, 255 },		//< texture  -- beautiful blue
	{ 191, 188, 180, 255 },		//< simple   -- boring ol' gray
	{ 191, 169, 126, 255 },		//< advanced -- glintering gold
	{ 191, 126, 126, 255 },		//< sink     -- stop-sign red
};

/// The different preview modes.
AUTO_ENUM;
typedef enum Mated2PreviewMode {
	PREVIEW_NONE,			ENAMES( None )
	PREVIEW_SAME_WINDOW,	ENAMES( SameWindow )
	PREVIEW_OTHER_WINDOW,	ENAMES( OtherWindow )
	PREVIEW_IN_WORLD,		ENAMES( InWorld )
} Mated2PreviewMode;

/// Information for a property.
typedef struct Mated2PropertyInfo {
	int flag;
	const char* name;
	const char* tooltip;
	bool causes_alpha;
	int mutually_exclusive;
} Mated2PropertyInfo;

/// The template properties listed in the side-panel
static const Mated2PropertyInfo mated2TemplatePropertyInfo[] = {
	{ SGRAPH_NO_ALPHACUTOUT,  "Disable AlphaCutout", NULL, false, SGRAPH_ALPHA_TO_COVERAGE },
	{ SGRAPH_ALPHA_TO_COVERAGE,  "Alpha to Coverage", NULL, false, SGRAPH_NO_ALPHACUTOUT },
	{ SGRAPH_ALPHA_PASS_ONLY, "Alpha Only",          NULL },
	{ SGRAPH_NO_HDR,          "No HDR",				 NULL },
	{ SGRAPH_NO_TINT_FOR_HDR, "Ignore Color0 for HDR", NULL },
	{ SGRAPH_ALLOW_ALPHA_REF, "Allow AlphaRef", NULL },
	{ SGRAPH_USE_AMBIENT_CUBE, "Use Ambient Cube", NULL },
	{ SGRAPH_ALLOW_REF_MIP_BIASE, "Allow ReflectionMIPBias", NULL },
	{ SGRAPH_BACKLIGHT_IN_SHADOW, "Backlight in Shadow", NULL },
	{ SGRAPH_UNLIT_IN_SHADOW, "Unlit in Shadow", NULL },
	{ SGRAPH_EXCLUDE_FROM_CLUSTER, "Exclude from Clustering", NULL },
	{ SGRAPH_DIFFUSEWARP,	"Diffuse Warp",	NULL },
};

/// The material properties listed in the side-panel
static const Mated2PropertyInfo mated2MaterialPropertyInfo[] = {
	{ RMATERIAL_ADDITIVE,		"Additive",			NULL, true,  RMATERIAL_SUBTRACTIVE },
	{ RMATERIAL_SUBTRACTIVE,	"Subtractive",		NULL, true,  RMATERIAL_ADDITIVE },
	{ RMATERIAL_DOUBLESIDED,	"Double-sided",		NULL, false, RMATERIAL_BACKFACE },
	{ RMATERIAL_BACKFACE,		"Backface",			NULL, false, RMATERIAL_DOUBLESIDED },
	{ RMATERIAL_NOZWRITE,		"No ZWrite",		NULL, true },
	{ RMATERIAL_NOZTEST,		"No ZTest",			NULL, false },
	{ RMATERIAL_FORCEFARDEPTH,	"Force Far Depth",	NULL, false },
	{ RMATERIAL_NOFOG,			"No Fog",			NULL, false },
	{ RMATERIAL_NOTINT,			"No Tint",			NULL, false },
	{ RMATERIAL_NONDEFERRED,	"No Outlines",		NULL, false },
	{ RMATERIAL_NOBLOOM,		"No Bloom",			"Causes material to not be drawn in bloom pass.  Has no effect when low quality bloom is set.", false },
	{ RMATERIAL_DECAL,			"Decal",			"Causes alpha materials to use the opaque shadowing code, which allows multiple shadowcasters per object.  Should only be used for geometry that is very close to opaque geometry.", false },
	{ RMATERIAL_DEPTHBIAS,		"Depth Bias",		"Causes triangles to draw slightly in front of objects with which they would normally z-fight.", false },
	{ RMATERIAL_WORLD_TEX_COORDS_XZ, "World TexCoords XZ", "Use world texture coordinates, discards model texture coordinates.", false },
	{ RMATERIAL_WORLD_TEX_COORDS_XY, "World TexCoords XY", "Use world texture coordinates, discards model texture coordinates.", false },
	{ RMATERIAL_WORLD_TEX_COORDS_YZ, "World TexCoords YZ", "Use world texture coordinates, discards model texture coordinates.", false },
	{ RMATERIAL_SCREEN_TEX_COORDS, "Screen TexCoords", "Use per-vert screen texture coordinates, discards model texture coordinates.", false },
	{ RMATERIAL_LOW_RES_ALPHA,	"Low Res Alpha",	NULL, false },
	{ RMATERIAL_NOCOLORWRITE,	"No Color Write",	"Causes only depth to output from the shader. Use only for special effects.", false },
	{ RMATERIAL_UNLIT,			"Unlit",			NULL, false },
	{ RMATERIAL_ALPHA_NO_DOF,	"No DOF (Alpha objects)",	"For alpha objects, forces them to be rendered after applying DoF.  Use for chain link fences.", false },
	{ RMATERIAL_NO_CLUSTER,		"Do not cluster",	"This object will not be clustered with other objects.  Prevents things such as simplification of the shader being used for this object or to make sure that an optimal LoD that was created by hand is used.", true },
	{ RMATERIAL_TESSELATE,		"Tesselate",	"Create the object geometrically.  This is ignored if not in D3D11", true },
};

typedef struct Mated2UndoCommand {
	EditUndoCustomFn undoFn;
	EditUndoCustomFn redoFn;
	EditUndoCustomFreeFn freeFn;
	UserData userData;
} Mated2UndoCommand;

/// Global data for the material editor.
typedef struct Mated2Editor {
	EMEditor emEd;
	Mated2EditorDoc* activeDoc;

	RdrDevice* previewDevice;
	Mated2PreviewMode previewMode;
	bool previewIsRotating;
	Model* previewObject;
	Mat4 previewObjectMatrix;
	
	RdrShaderPerformanceValues perfValues;
	int perfTotalMemory;
	int perfSharedMemory;
	int instructionCountMaxLights;
	int instructionCount2Lights;
	int instructionCountSM20;
	int constantCountMaxLights;
	int constantCount2Lights;
	int constantCountSM20;
	bool lastFrameIsNVPerfShown;
	bool isTemplateEditingDisabled;

	// UI Widgets
	ModelOptionsToolbar* previewToolbar;
	UIComboBox* previewModeComboBox;
	UIStyleFont* warningFont;

	// Picker textures
	StashTable pickerTextures;
} Mated2Editor;

/// A material editor document.	 This is the central object of the
/// editor manager framework.
typedef struct Mated2EditorDoc {
	EMEditorDoc emEd;

	// validation data
	bool validationNeedsUpdate;
	int validationUpdateDelayFrames;
	char** validationWarnings;
	char** validationErrors;
	UIWindow* validationWindow;
	UIScrollArea* validationScrollArea;
	UISMFView* validationText;

	// Material info
	bool isLoading;
	bool fileCheckOutFailed;
	bool fileCheckedOut;
	MaterialLoadInfo* materialLoadInfo;
	bool materialPropertyFlags[ ARRAY_SIZE( mated2MaterialPropertyInfo )];
	bool buffer1, buffer2, buffer3; // CD: these are here to stop an odd compiler issue where checking the last element of the bool array would check an element of the next bool array as well
	bool templatePropertyFlags[ ARRAY_SIZE( mated2TemplatePropertyInfo )];
	bool buffer4, buffer5, buffer6;
	UICheckButton* templateFeatureCheckButtons[ 32 ];
	UIButton* templateFeatureResetButtons[ 32 ];
	Mated2Guide** guides;
	UIWidgetGroup inputListNodeNames;

	// Renames info
	StashTable renamesTable;
	UIWindow* renamesWindow;
	UIScrollArea* renamesScrollArea;
	UISMFView* renamesText;

	// If the doc is a template, these fields will point to the active
	// data.
	//
	// If the doc is not a template, then shaderTemplate will point to
	// a copy of the active data THAT MUST BE FREED.  THE OTHER FIELDS
	// POINT INTO THAT AND MUST NOT BE FREED.
	ShaderTemplate* shaderTemplate;
	ShaderGraph* shaderGraph;
	ShaderGuide*** shaderGuides;
	MaterialFallback* fallback;
	
	Material material;

	// Preview
	MaterialPreview previewMaterial;
	Mated2Node* previewSelectedNode;
	UISkin** previewShaderTypeSkin;
	UISkin* previewAndErrorSkin;
	UISkin* errorSkin;
	ShaderGraphFeatures activeFeatures;

	// undo/redo
	int undoGroupDepth;
	Mated2UndoCommand** undoQueuedCommands;

	// ui widgets
	UIButton* templateButton;
	UIButton* physicalPropertiesButton;
	EMPanel* panelTemplateProperties;
	EMPanel* panelTemplateFeatures;
	EMPanel* panelMaterialProperties;
	EMPanel* panelMaterialFallbacks;
	EMPanel* panelLinks;
	EMPanel* panelNodeList;
	EMPanel* panelInputList;
	UIPane* viewPane;
	UIList* listMaterialFallbacks;
	UIButton* nodeViewHideButton;
	UIButton* nodeViewShowButton;
	bool nodeViewIsResizing;
	int nodeViewResizingCanvasHeight;
	UISliderTextEntry* timeSlider;
	UITextEntry* sceneEntry;
	bool inputListNeedsReflow;
	UIComboBox* comboBoxInstancedNode;
	UIComboBox* comboBoxReflectionType;
	UIComboBox* comboBoxShaderQuality;
	UIComboBox* comboBoxMaxReflectionResolution;

	UILabel* subgraphPreviewWarning;
	bool subgraphPreviewWarningIsActive;
	
	UITree* treeNodes;
	UITextArea* textAreaLinks;
	UIFlowchart* flowchart;
} Mated2EditorDoc;

/// Used for various options that update links
typedef struct Mated2UpdateLinkInfo {
	Mated2EditorDoc* doc;
	int numFiles;
	char** filesIgnored;
} Mated2UpdateLinkInfo;

/// Info used for previews.
typedef struct Mated2MaterialPreviewTexture {
	BasicTexture* texture;
	U32 completeTime;
} Mated2MaterialPreviewTexture;

/// EArray container so that an earray can be passed in fields requiring a single void* like userdata.
typedef struct eaContainer {
	void*** theEArray;
}eaContainer;

/// *The* one and only, MatEd2.1 editor.
static Mated2Editor mated2Editor;

/// *The* Mated2.1 general material picker
static EMPicker* mated2Picker;

/// The Mated2.1 material template picker
static EMPicker* mated2TemplatePicker;

/// The menu times
static const EMMenuItemDef mated2MenuItems[] = {
	{ "mated2_stuff", "Stuff", NULL },
	{ "mated2_things", "Things", NULL },
};

/// The list of all material templates that are "super low end".  They
/// don't even require Shader Model 2.0.
static const char** mated2SuperLowEndTemplates = NULL;

#define MATED2_TABBAR_HEIGHT 32

/// Register the Material Editor.
#endif
AUTO_RUN;
int mated2Register( void )
{
#ifndef NO_EDITORS
	if( !areEditorsAllowed() ) {
		return 0;
	}

	// Register the pickers
	{
		mated2Picker = emEasyPickerCreate( "Material", ".Material", "materials/", mated2PickerPathFilter );
		emEasyPickerSetColorFunc( mated2Picker, mated2MaterialPickerColor );
		emEasyPickerSetTexFunc( mated2Picker, mated2MaterialPickerPreview );
		mated2Picker->allow_outsource = true;
		emPickerRegister(mated2Picker);
		mated2TemplatePicker = emEasyPickerCreate( "Material Template", ".Material", "Materials/Templates/", NULL );
		emEasyPickerSetColorFunc( mated2TemplatePicker, mated2MaterialPickerColor );
		emEasyPickerSetTexFunc( mated2TemplatePicker, mated2MaterialPickerPreview );
		mated2TemplatePicker->allow_outsource = true;
		emPickerRegister(mated2TemplatePicker);
	}

	// Register the editor
	{
		strcpy( mated2Editor.emEd.editor_name, MATERIAL_EDITOR );
		mated2Editor.emEd.type = EM_TYPE_SINGLEDOC;
		mated2Editor.emEd.disable_single_doc_menus = false;
		mated2Editor.emEd.default_type = "Material";
		mated2Editor.emEd.keybinds_name = "MaterialEditor2";
		mated2Editor.emEd.keybind_version = 1;

		mated2Editor.emEd.init_func = (EMEditorFunc)mated2EditorInit;
		mated2Editor.emEd.new_func = (EMEditorDocNewFunc)mated2DocNewDispatcher;
		mated2Editor.emEd.load_func = (EMEditorDocLoadFunc)mated2DocLoad;
		mated2Editor.emEd.save_func = (EMEditorDocStatusFunc)mated2DocSave;
		mated2Editor.emEd.save_as_func = (EMEditorDocStatusFunc)mated2DocSaveAs;
		mated2Editor.emEd.autosave_func = (EMEditorDocStatusFunc)mated2DocAutosave;
		mated2Editor.emEd.close_func = (EMEditorDocFunc)mated2DocClose;
		mated2Editor.emEd.draw_func = (EMEditorDocFunc)mated2DocOncePerFrame;
		mated2Editor.emEd.ghost_draw_func = (EMEditorDocGhostDrawFunc)mated2DocDrawGhosts;
		mated2Editor.emEd.got_focus_func = (EMEditorDocFunc)mated2DocGotFocus;
		mated2Editor.emEd.lost_focus_func = (EMEditorDocFunc)mated2DocLostFocus;
		mated2Editor.emEd.allow_multiple_docs = true;
		mated2Editor.emEd.allow_outsource = true;

		eaPush( &mated2Editor.emEd.pickers, mated2Picker );

		emRegisterEditor( &mated2Editor.emEd );
	}
	
	emRegisterFileType( "Material", "Material", MATERIAL_EDITOR );
	emRegisterFileType( "MaterialTemplate", "Material Template", MATERIAL_EDITOR );

	
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "4Color_Fallback" )); //< has issues with SM20, 70 instructions
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "ColorValue" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Diffuse_1Normal_Fallback" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Diffuse_2Color" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Diffuse_Lumi_Fallback" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Diffuse_NoAlphaCutout" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "SingleTexture" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Sprite_2Texture_Add_AlphaMask_Fallback" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Sprite_2Texture_Intersect_AlphaMask_Fallback" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Sprite_Default" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Sprite_DualColor_AlphaMask" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Sprite_TexChannel_AlphaSwitch" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Transparent_Fresnel_2Texture_Fallback" ));
	eaPush( &mated2SuperLowEndTemplates, allocAddString( "Transparent_Fresnel_Fallback" ));
	return 0;
#else
	return 0;
#endif
}
#ifndef NO_EDITORS

/// Sets up toolbars, menus, the side pane, and such.
static void mated2EditorInit( Mated2Editor* editor )
{
	// Force editor.emEd.camera to exist and be passed on to the
	// toolbar.
	//
	// Later on in this function, the preview mode should be set apropo.
	emEditorHideWorld( &editor->emEd, true );
	
	// UI setup...
	eaPush( &editor->emEd.toolbars, emToolbarCreateDefaultFileToolbar() );
	eaPush( &editor->emEd.toolbars, emToolbarCreateWindowToolbar() );
	
	{
		EMToolbar* accum = emToolbarCreate( 0 );
		int xIt = 0;

		{
			UILabel* label = ui_LabelCreate( "Preview:", 0, 0 );
			UIComboBox* comboBox = ui_ComboBoxCreateWithEnum( 0, 0, 150, Mated2PreviewModeEnum, mated2EditorPreviewModeChanged, editor );

			ui_ComboBoxSetSelected( comboBox, editor->previewMode );

			ui_WidgetSetPosition( UI_WIDGET( label ), xIt, 0 );
			emToolbarAddChildXIt( accum, label, false, &xIt );
			ui_WidgetSetPosition( UI_WIDGET( comboBox ), xIt, 0 );
			emToolbarAddChildXIt( accum, comboBox, false, &xIt );

			editor->previewModeComboBox = comboBox;
		}

		editor->warningFont = ui_StyleFontCreate( "Mated2WarningFont", &g_font_Sans, ColorRed, true, false, 1 );
		
		{
			static struct {
				char* label;
				U32 bgColor;
				char* cmd;
			} options[] = {
				{ "R", 0xFF0000FF, "shaderTestN 3 SHOW_RED" },
				{ "G", 0x00FF00FF, "shaderTestN 3 SHOW_GREEN" },
				{ "B", 0x0000FFFF, "shaderTestN 3 SHOW_BLUE" },
				{ "A", -1,         "shaderTestN 3 SHOW_ALPHA" },
				{ "X", -1,         "shaderTestN 3 \"\"" },
			};
			int it;

			for( it = 0; it != ARRAY_SIZE( options ); ++it ) {
				UIButton* button = ui_ButtonCreate( options[ it ].label, xIt, 0, mated2EditorCmdParse, options[ it ].cmd );

				if( options[ it ].bgColor != -1 ) {
					ui_WidgetUnskin( UI_WIDGET( button ), colorFromRGBA( options[ it ].bgColor ), ColorBlack, ColorBlack, ColorBlack );
				}
				
				emToolbarAddChildXIt( accum, button, false, &xIt );
			}
		}

		{
			UIButton* button = ui_ButtonCreateImageOnly( "button_modelpicker", xIt, 0, mated2EditorObjectPicker, editor );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			emToolbarAddChildXIt( accum, button, false, &xIt );
		}
		
		{
			UIButton* button = ui_ButtonCreateImageOnly( "button_center", xIt, 0, mated2EditorPreviewRecenterObject, editor );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			ui_WidgetSetTooltipString( UI_WIDGET( button ), "Recenter object" );
			emToolbarAddChildXIt( accum, button, false, &xIt );
		}

		{
			UIButton* button = ui_ButtonCreateImageOnly( "button_rotateCW", xIt, 0, mated2EditorRotateObjectToggle, editor );
			ui_WidgetSetHeightEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			emToolbarAddChildXIt( accum, button, true, &xIt );
		}
		
		eaPush( &editor->emEd.toolbars, accum );
	}
	
	editor->previewToolbar = motCreateToolbar(
			MET_TINT | MET_SKIES | MET_TIME | MET_CAMRESET, editor->emEd.camera, zerovec3, zerovec3, 20, "Material Editor" );
	eaPush( &editor->emEd.toolbars, motGetToolbar( editor->previewToolbar ));

	// Menus are not needed:
	//emMenuItemCreateFromTable( &editor->emEd, mated2MenuItems, ARRAY_SIZE( mated2MenuItems ));
	//emMenuRegister( &editor->emEd, emMenuCreate( &editor->emEd, "Material", "mated2_stuff", "mated2_things", NULL ));
	
	// skins
	{
		int it;
		for( it = 0; it != ARRAY_SIZE( mated2ShaderTypeSkin ); ++it ) {
			ui_SkinCopy( &mated2ShaderTypeSkin[ it ], NULL );
			ui_SkinSetBackground( &mated2ShaderTypeSkin[ it ], shaderTypeBgColor[ it ]);
		}

		ui_SkinCopy( &mated2ViewPaneSkin, NULL );
		mated2ViewPaneSkin.background[ 0 ].a /= 2;
		mated2ViewPaneSkin.background[ 1 ].a /= 2;
	}

	// info window
	{
		emInfoWinEntryRegister( &editor->emEd, "mated2memorytotal", "Memory (total)", mated2InfoWinMemoryTotal );
		emInfoWinEntryRegister( &editor->emEd, "mated2memoryprivate", "Memory (non-shared)", mated2InfoWinMemoryPrivate );
		emInfoWinEntryRegister( &editor->emEd, "mated2perfscore", "Score", mated2InfoWinPerfScore );
		emInfoWinEntryRegister( &editor->emEd, "mated2perfalu", "ALU Instructions", mated2InfoWinPerfALU );
		emInfoWinEntryRegister( &editor->emEd, "mated2perftexfetch", "Texture Fetches", mated2InfoWinPerfTexFetch );
		emInfoWinEntryRegister( &editor->emEd, "mated2perftemp", "Temporaries", mated2InfoWinPerfTemp );
		emInfoWinEntryRegister( &editor->emEd, "mated2perfparam", "Parameters", mated2InfoWinPerfParam );
		emInfoWinEntryRegister( &editor->emEd, "mated2perfnvperfhud", "NV PerfShaderPPS" , mated2InfoWinPerfNVPerfHUD );
		emInfoWinEntryRegister( &editor->emEd, "mated2instructioncountmaxlights", "Instruction Count (Max Lights)" , mated2InfoWinInstructionCountMaxLights );
		emInfoWinEntryRegister( &editor->emEd, "mated2instructioncount2lights", "Instruction Count (2 Lights)" , mated2InfoWinInstructionCount2Lights );
		emInfoWinEntryRegister( &editor->emEd, "mated2instructioncountsm20", "Instruction Count (SM2)" , mated2InfoWinInstructionCountSM20 );
		emInfoWinEntryRegister( &editor->emEd, "mated2constcountmaxlights", "Constant Count (Max Lights)" , mated2InfoWinConstCountMaxLights );
		emInfoWinEntryRegister( &editor->emEd, "mated2constcount2lights", "Constant Count (2 Lights)" , mated2InfoWinConstCount2Lights );
		emInfoWinEntryRegister( &editor->emEd, "mated2constcount2lightssm20", "Constant Count (SM2)" , mated2InfoWinConstCountSM20 );
		emInfoWinEntryRegister( &editor->emEd, "mat", "Material", wleUIInfoWinMaterial);
	}

	editor->emEd.autosave_interval = 1;
	rdr_state.disableShaderProfiling = false;				//< TODO: do not globally disable shader
															//< profiling...
	
	// Force an update to the preview mode by forcing its value to change
	editor->previewMode = -1;	
	mated2EditorPreviewSetMode( GamePrefGetInt( "MaterialEditor2\\PreviewMode", PREVIEW_SAME_WINDOW ));
	
	editor->previewIsRotating = GamePrefGetInt( "MaterialEditor2\\PreviewRotate", false );
	copyMat4( unitmat, editor->previewObjectMatrix );
	mated2EditorPreviewSetObjectDefault();
	mated2Editor.isTemplateEditingDisabled
		= (!gimmeDLLQueryExists()
		   || !(UserIsInGroup( "Software" ) || UserIsInGroup( "TemplateEditors" )));
}

static char* mated2PickerPathFilter( const char* path )
{
	char buffer[ 256 ];
	
	if( strstri( path, "/Templates/" )) {
		getFileNameNoExt( buffer, path );
		strcat( buffer, " (Template)" );
	} else {
		getFileNameNoExt( buffer, path );
	}

	return StructAllocString( buffer );
}

#endif
/// Grant the permision to edit templates, even if it would not be
/// avaliable.
AUTO_COMMAND;
void mated2ForceAllowTemplateEditing( void ) {
#ifndef NO_EDITORS
	mated2Editor.isTemplateEditingDisabled = false;
#endif
}
#ifndef NO_EDITORS

static void mated2EditorPreviewWindowInit( void )
{
#if !PLATFORM_CONSOLE
	{
		if( mated2Editor.previewDevice == NULL ) {
			MONITORINFOEX moninfo;
			WindowCreateParams params = { 0 };

			params.display.width = 512;
			params.display.height = 512;
			params.threaded = 0;
			params.display.preferred_monitor = -1;
			// attempt to open on non-primary monitor, if available
			multiMonGetMonitorInfo(multimonGetPrimaryMonitor() ? 0 : 1, &moninfo);
			params.display.xpos = moninfo.rcWork.left + 25;
			params.display.ypos = moninfo.rcWork.top + 25;
			params.display.fullscreen = 0;
			params.display.refreshRate = 0;

			mated2Editor.previewDevice = gfxAuxDeviceAdd(
					&params, MATERIAL_EDITOR_PREVIEW_WINDOW, mated2EditorPreviewWindowClosed,
					mated2EditorPreviewWindowOncePerFrame, NULL );
			rdrSetTitle( mated2Editor.previewDevice, "Material Preview" );

			{
				RdrDevice* primaryDevice = gfxGetActiveDevice();
				gfxSetActiveDevice( mated2Editor.previewDevice );
				gfxSetActiveCameraController( mated2Editor.emEd.camera, true );
				gfxSetFrameRatePercentBG( 1 );

				gfxSetActiveDevice( primaryDevice );
			}
		}
	}
#endif
}

static bool mated2EditorPreviewWindowClosed( RdrDevice* device, void* ignored )
{
	mated2EditorPreviewSetMode( PREVIEW_NONE );
	return true;
}

static void mated2EditorPreviewWindowOncePerFrame( RdrDevice* device, void* ignored )
{
	Mated2EditorDoc* doc = mated2Editor.activeDoc;
	if( mated2Editor.previewMode != PREVIEW_OTHER_WINDOW ) {
		return;
	}
	if (!doc)
		return;
	
	gfxAuxDeviceDefaultTop( device, 0, ui_OncePerFramePerDevice);
	gfxUpdateActiveCameraViewVolumes( NULL );
	if( doc ) {
		mated2DrawPreview( doc );
	}
	gfxAuxDeviceDefaultBottom( device, 0 );
}

static void mated2EditorPreviewSetMode( Mated2PreviewMode previewMode )
{
	{
		if( mated2Editor.previewMode == previewMode ) {
			return;
		}

		if( mated2Editor.previewMode == PREVIEW_OTHER_WINDOW ) {
			rdrShowWindow( mated2Editor.previewDevice, SW_HIDE );
		}
	
		mated2Editor.previewMode = previewMode;
		ui_ComboBoxSetSelectedEnum( mated2Editor.previewModeComboBox, previewMode );		
		emEditorHideWorld( &mated2Editor.emEd, previewMode != PREVIEW_IN_WORLD );

		if( previewMode == PREVIEW_OTHER_WINDOW ) {
			mated2EditorPreviewWindowInit();
			rdrShowWindow( mated2Editor.previewDevice, SW_SHOW );
		}

		if( previewMode == PREVIEW_OTHER_WINDOW || previewMode == PREVIEW_SAME_WINDOW ) {
			motApplyValues( mated2Editor.previewToolbar );
		}
	
		GamePrefStoreInt( "MaterialEditor2\\PreviewMode", mated2Editor.previewMode );
	}
}

void mated2EditorPreviewSetObject( const char* objectName, const char* fname )
{
	Model* model = modelFind( objectName, true, WL_FOR_UTIL );

	if( !model ) {
		ErrorFilenamef( fname, "Could not find object %s in the file.", objectName );
		objectName = MATED2_DEFAULT_PREVIEW_OBJECT_NAME;
		fname = MATED2_DEFAULT_PREVIEW_FILENAME;
		model = modelFind( objectName, true, WL_FOR_UTIL);
	}
	if( !model ) {
		FatalErrorFilenamef( fname, "The System MatED file has gotten corrupt!  Talk to Jared right away!" );
		return;
	}

	GamePrefStoreString( "MaterialEditor2\\PreviewObjectName", objectName );
	GamePrefStoreString( "MaterialEditor2\\PreviewFile", fname );
	mated2Editor.previewObject = model;
	mated2MaterialPickerPreviewClear();
}

void mated2EditorPreviewSetObjectDefault( void )
{
	mated2EditorPreviewSetObject(
				GamePrefGetString( "MaterialEditor2\\PreviewObjectName", MATED2_DEFAULT_PREVIEW_OBJECT_NAME),
				GamePrefGetString( "MaterialEditor2\\PreviewFile", MATED2_DEFAULT_PREVIEW_FILENAME));
}

static void mated2EditorPreviewModeChanged( UIComboBox* comboBox, int value, Mated2Editor* editor )
{
	if(value==PREVIEW_OTHER_WINDOW)
	{
		ui_ComboBoxSetSelectedEnum(comboBox, editor->previewMode);
		return;
	}

	mated2EditorPreviewSetMode( value );
}

static void mated2EditorCmdParse( UIButton* button, const char* command )
{
	globCmdParse( command );
}

static void mated2EditorObjectPicker( UIButton* button, Mated2Editor* editor )
{
	EMPicker* picker = emPickerGetByName( "Object Picker" );
	assert( picker );
	emPickerShow( picker, "Select", false, mated2EditorObjectPicked, NULL );
}

static bool mated2EditorObjectPicked( EMPicker* picker, EMPickerSelection** selections, void* ignored )
{
	if(   eaSize( &selections ) == 0
		  || selections[ 0 ]->table != parse_ResourceInfo ) {
		return false;
	} else {
		ResourceInfo* entry = (ResourceInfo*)selections[ 0 ]->data;
		GroupDef *def = objectLibraryGetGroupDefByName(entry->resourceName, false);
		if (!def)
			return false;
		mated2EditorPreviewSetObject( def->model_name, entry->resourceLocation );
	}

	return true;
}

static void mated2EditorPreviewRecenterObject( UIButton* button, Mated2Editor* editor )
{
	editor->previewIsRotating = false;
	GamePrefStoreInt( "MaterialEditor2\\PreviewRotate", editor->previewIsRotating );
	
	copyMat4( unitmat, editor->previewObjectMatrix );
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL( 0 );
void mated2EditorPreviewResetCameraCommand( void )
{
	motResetCamera( mated2Editor.previewToolbar );
}

static void mated2EditorRotateObjectToggle( UIButton* button, Mated2Editor* editor )
{
	editor->previewIsRotating = !editor->previewIsRotating;
	GamePrefStoreInt( "MaterialEditor2\\PreviewRotate", editor->previewIsRotating );
}

/// Update open materials with the new template settings.
static char*** mated2EditorUpdateMaterialTemplateReferenced(
	const char* templateName, Mated2EditorDoc* doc )
{
	Mated2EditorDoc** openDocs = (Mated2EditorDoc**)mated2Editor.emEd.open_docs;
	char*** docFileNamesAccum = (char***) calloc (1,sizeof(char**));
	char filename[255];
	char *filename_s;
	bool filesChanged = false;
	EMEditorDoc** toBeClosed = NULL;
	char** toBeSaved = NULL;

	{
		FOR_EACH_IN_EARRAY(openDocs,Mated2EditorDoc,openDoc) {
			if( openDoc == doc ) {
				continue;
			}

			// If any fallback is referenced, they have local caches
			// of the template while in the MaterialEditor.  You then
			// can't change the template behind its back.
			if( mated2DocHasFallback( openDoc, templateName )) {
				filename_s = (char*) calloc (255,sizeof(char));

				if (!openDoc->emEd.saved) {
					strcpy_unsafe(filename_s, openDoc->emEd.doc_display_name);
					eaPush(&toBeSaved,filename_s);
				} else {
					strcpy(filename, openDoc->emEd.doc_name);
					eaPush(&toBeClosed,&openDoc->emEd);

					if ( mated2EnsureCheckoutFile( filename ) ) {
						strcat(filename,".Material");
						mated2UpdateDependantMaterial( doc, filename);
					}
					strcpy_unsafe(filename_s,filename);
					eaPush(docFileNamesAccum,filename_s);
				}
			}
		}
		FOR_EACH_END;
	}
	if (eaSize(&toBeSaved) > 0) {
		// Let the user know which files need to be saved here.
		char buffer[ 512 ];
		int lastDisplayedName = MIN( eaSize( &toBeSaved ) - 1, 5 );
		int it;

		sprintf( buffer,
			"The template %s is referenced by currently unsaved\n"
			"documents.  You can not save any changes to this\n"
			"template until all those documents are saved or closed.\n"
			"\n"
			"Referenced by: ",
			templateName );
		for( it = 0; it <= lastDisplayedName; ++it ) {
			if( it != lastDisplayedName ) {
				strcatf( buffer, "%s, ", toBeSaved[ it ]);
			} else {
				if( it == eaSize( &toBeSaved ) - 1 ) {
					strcatf( buffer, "%s.", toBeSaved[ it ]);
				} else {
					strcatf( buffer, "%s...", toBeSaved[ it ]);
				}
			}
		}

		ui_ModalDialog( "Can Not Save", buffer, ColorWhite, UIOk );

		// clear memory
		eaDestroyEx(&toBeSaved,NULL);
		eaDestroyEx(docFileNamesAccum,NULL);
		free(docFileNamesAccum);
		docFileNamesAccum = NULL;
	} else {
		FOR_EACH_IN_EARRAY(toBeClosed,EMEditorDoc,theDoc) {
			emForceCloseDoc(theDoc);
		}
		FOR_EACH_END;
	}
	eaDestroy(&toBeClosed);
	return docFileNamesAccum;
}

/// Return TRUE if TEMPLATE-NAME is referenced by any open document.
/// Will also display a descriptive dialog if necesarry.
///
/// Ignores DOC-TO-IGNORE.
static bool mated2EditorWarnIfTemplateReferenced(
		const char* templateName, Mated2EditorDoc* docToIgnore )
{
	Mated2EditorDoc** openDocs = (Mated2EditorDoc**)mated2Editor.emEd.open_docs;
	char** docNamesAccum = NULL;

	{
		int it;
		for( it = 0; it != eaSize( &openDocs ); ++it ) {
			Mated2EditorDoc* openDoc = openDocs[ it ];

			if( openDoc == docToIgnore ) {
				continue;
			}

			if( mated2DocActiveFallback( openDoc )->shader_template_name == templateName ) {
				eaPush( &docNamesAccum, openDoc->emEd.doc_display_name );
			}
		}
	}

	if( eaSize( &docNamesAccum )) {
		char buffer[ 512 ];
		int lastDisplayedName = MIN( eaSize( &docNamesAccum ) - 1, 5 );
		int it;
		
		sprintf( buffer,
				 "The template %s is referenced by currently open "
				 "documents.  You can not save any changes to this "
				 "template until all those documents are closed.\n"
				 "\n"
				 "Referenced by: ",
				 templateName );
		for( it = 0; it <= lastDisplayedName; ++it ) {
			if( it != lastDisplayedName ) {
				strcatf( buffer, "%s, ", docNamesAccum[ it ]);
			} else {
				if( it == eaSize( &docNamesAccum ) - 1 ) {
					strcatf( buffer, "%s.", docNamesAccum[ it ]);
				} else {
					strcatf( buffer, "%s...", docNamesAccum[ it ]);
				}
			}
		}
		
		ui_ModalDialog( "Can Not Save", buffer, ColorWhite, UIOk );
		return true;
	} else {
		return false;
	}
}

/// Return a skin suitable for the given SHADER-TYPE and flags.
UISkin* mated2NodeSkin( Mated2EditorDoc* doc, int shaderType, bool isPreview, bool isError )
{
	if( isPreview && isError ) {
		return doc->previewAndErrorSkin;
	} else if( isError ) {
		return doc->errorSkin;
	} else if( isPreview ) {
		return doc->previewShaderTypeSkin[ shaderType ];
	} else {
		return &mated2ShaderTypeSkin[ shaderType ];
	}
}

/// Allocate an empty document.  This will have NO MATERIAL actually
/// loaded in it, so makea sure to initialize the MaterialLoadInfo and
/// call mated2FinishLoad[Template|Material]().
static Mated2EditorDoc* mated2DocAlloc( void )
{
	Mated2EditorDoc* accum = calloc( 1, sizeof( *accum ));
	F32 canvasX, canvasY, canvasWidth, canvasHeight;
	emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );

	strcpy( accum->emEd.doc_display_name, "New Material" );
	strcpy( accum->emEd.doc_type, "Material" );
	accum->emEd.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext( accum->emEd.edit_undo_stack, accum );

	accum->materialLoadInfo = StructCreate( parse_MaterialLoadInfo );

	// ze panels
	mated2PanelCreateLinks( accum );
	mated2PanelCreateMaterialFallbacks( accum );
	mated2PanelCreateMaterialInputList( accum );
	mated2PanelCreateMaterialProperties( accum );
	mated2PanelCreateTemplateFeatures( accum );
	mated2PanelCreateTemplateNodeList( accum );
	mated2PanelCreateTemplateProperties( accum );

	// ze material area
	accum->viewPane = ui_PaneCreate( 0, 0, 1, (canvasHeight - MATED2_TABBAR_HEIGHT) / 2, UIUnitPercentage, UIUnitFixed, 0 );
	
	accum->viewPane->widget.tickF = mated2ViewPaneTick;
	ui_WidgetSkin( UI_WIDGET( accum->viewPane ), &mated2ViewPaneSkin );
	ui_WidgetSetPositionEx( UI_WIDGET( accum->viewPane ), 0, 0, 0, 0, UIBottomRight );

	accum->nodeViewHideButton = ui_ButtonCreateImageOnly( "eui_arrow_large_up", -12, 0, mated2NodeViewHideShowClicked, accum );
	ui_WidgetSetDimensions( UI_WIDGET( accum->nodeViewHideButton ), 32, 12 );
	ui_ButtonSetImageStretch( accum->nodeViewHideButton, true );
	accum->nodeViewHideButton->widget.xPOffset = 0.5;
	accum->nodeViewHideButton->widget.offsetFrom = UITopLeft;
	accum->nodeViewHideButton->widget.priority = 100;
	ui_WidgetSetDragCallback( UI_WIDGET( accum->nodeViewHideButton ), mated2NodeViewExpandDrag, accum );
	accum->nodeViewShowButton = ui_ButtonCreateImageOnly( "eui_arrow_large_down", -12, 0, mated2NodeViewHideShowClicked, accum );
	ui_WidgetSetDimensions( UI_WIDGET( accum->nodeViewShowButton ), 32, 12 );
	ui_ButtonSetImageStretch( accum->nodeViewShowButton, true );
	accum->nodeViewShowButton->widget.xPOffset = 0.5;
	accum->nodeViewShowButton->widget.offsetFrom = UITopLeft;
	accum->nodeViewShowButton->widget.priority = 100;
	ui_WidgetSetDragCallback( UI_WIDGET( accum->nodeViewShowButton ), mated2NodeViewExpandDrag, accum );
	
	ui_WidgetAddChild( UI_WIDGET( accum->viewPane ), UI_WIDGET( accum->nodeViewHideButton ));

	// ze flowchart
	accum->flowchart = ui_FlowchartCreate( NULL, NULL, NULL, NULL );
	ui_ScrollAreaSetNoCtrlDraggable( UI_SCROLLAREA( accum->flowchart ), true );
	ui_WidgetSetDimensionsEx( UI_WIDGET( accum->flowchart ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	ui_WidgetAddChild( UI_WIDGET( accum->viewPane ), UI_WIDGET( accum->flowchart ));

	ui_FlowchartSetLinkedCallback( accum->flowchart, mated2FlowchartLinkRequest, accum );
	ui_FlowchartSetUnlinkedCallback( accum->flowchart, mated2FlowchartUnlinkRequest, accum );
	ui_FlowchartSetLinkBeginCallback( accum->flowchart, mated2FlowchartLinkBegin, accum );
	ui_FlowchartSetLinkEndCallback( accum->flowchart, mated2FlowchartLinkEnd, accum );
	ui_FlowchartSetNodeRemovedCallback( accum->flowchart, mated2FlowchartNodeRemoveRequest, accum );
	ui_FlowchartSetNodeRemovedLateCallback( accum->flowchart, mated2FlowchartNodeRemoved, accum );

	// the warning
	accum->subgraphPreviewWarning = ui_LabelCreate( "Previewing Subgraph", 10, 20 );
	UI_WIDGET( accum->subgraphPreviewWarning )->scale = 1.5;
	ui_WidgetSetFont( UI_WIDGET( accum->subgraphPreviewWarning ), "Warning" );
	accum->subgraphPreviewWarning->widget.tickF = mated2SubgraphPreviewWarningTick;

	// skinz
	eaSetSize( &accum->previewShaderTypeSkin, SOT_END );
	{
		int it;
		for( it = 0; it != eaSize( &accum->previewShaderTypeSkin ); ++it ) {
			accum->previewShaderTypeSkin[ it ] = ui_SkinCreate( NULL );
			ui_SkinCopy( accum->previewShaderTypeSkin[ it ], &mated2ShaderTypeSkin[ it ]);
		}
	}
	accum->previewAndErrorSkin = ui_SkinCreate( NULL );
	accum->errorSkin = ui_SkinCreate( NULL );

	// the errors
	accum->validationText = ui_SMFViewCreate( 0, 0, 1, 1 );
	ui_WidgetSetDimensionsEx( UI_WIDGET( accum->validationText ), 1, 1,
							  UIUnitPercentage, UIUnitPercentage );
	ui_SetActive( UI_WIDGET( accum->validationText ), false );

	accum->validationScrollArea = ui_ScrollAreaCreate( 0, 0, 1, 1, 1, 1, false, true );
	ui_WidgetSetDimensionsEx( UI_WIDGET( accum->validationScrollArea ), 1, 1,
							  UIUnitPercentage, UIUnitPercentage );
	accum->validationScrollArea->autosize = true;
	ui_ScrollAreaAddChild( accum->validationScrollArea, UI_WIDGET( accum->validationText ));

	accum->validationWindow = ui_WindowCreate( "", 320, 240, 400, 100 ); //< title is update in once per frame
	ui_WindowSetClosable( accum->validationWindow, false );
	ui_WindowAddChild( accum->validationWindow, accum->validationScrollArea );
	
	// the renames
	accum->renamesText = ui_SMFViewCreate( 0, 0, 1, 1 );
	ui_WidgetSetDimensionsEx( UI_WIDGET( accum->renamesText ), 1, 1,
							  UIUnitPercentage, UIUnitPercentage );
	ui_SetActive( UI_WIDGET( accum->renamesText ), false );

	accum->renamesScrollArea = ui_ScrollAreaCreate( 0, 0, 1, 1, 1, 1, false, true );
	ui_WidgetSetDimensionsEx( UI_WIDGET( accum->renamesScrollArea ), 1, 1,
							  UIUnitPercentage, UIUnitPercentage );
	accum->renamesScrollArea->autosize = true;
	ui_ScrollAreaAddChild( accum->renamesScrollArea, UI_WIDGET( accum->renamesText ));

	accum->renamesWindow = ui_WindowCreate( "Template Renames", 320, 240, 400, 300 );
	ui_WindowSetClosable( accum->renamesWindow, false );
	ui_WindowAddChild( accum->renamesWindow, UI_WIDGET( accum->renamesScrollArea ));
	accum->activeFeatures = systemSpecsMaterialSupportedFeatures();

	accum->renamesTable = stashTableCreateWithStringKeys( 32, StashDefault );
		
	accum->isLoading = true;
	
	return accum;
}

/// Create a new document based on the type passed in.
static Mated2EditorDoc* mated2DocNewDispatcher( const char* type, void* ignored )
{
	if( stricmp( type, "Material" ) == 0 ) {
		return mated2DocNewMaterial( type, ignored );
	} else if( stricmp( type, "MaterialTemplate" ) == 0 ) {
		return mated2DocNewTemplate( type, ignored );
	} else {
		return NULL;
	}
}

/// Create a new Material template doc.
static Mated2EditorDoc* mated2DocNewTemplate( const char* type, void* ignored )
{
	if( mated2Editor.isTemplateEditingDisabled ) {
		Alertf( "Template editing is disabled." );
		return NULL;
	} else {
		Mated2EditorDoc* accum = mated2DocAlloc();
	
		g_materials_skip_fixup = true;
		if (!ParserLoadFiles( NULL, "materials/Templates/system/NoPreload/NewTemplate.Material", NULL, 0, parse_MaterialLoadInfo, accum->materialLoadInfo ))
			assertmsg(0, "Failed to load materials/Templates/system/NoPreload/NewTemplate.Material");
		accum->materialLoadInfo->templates[ 0 ]->template_name = "";
		accum->materialLoadInfo->templates[ 0 ]->filename = "UNSAVED";
		accum->materialLoadInfo->material_datas[ 0 ]->material_name = "";
		accum->materialLoadInfo->material_datas[ 0 ]->graphic_props.default_fallback.shader_template_name = "";
		accum->materialLoadInfo->material_datas[ 0 ]->filename = "UNSAVED";
		g_materials_skip_fixup = false;
	
		mated2FinishLoadTemplate( accum );
		devassertmsg( eaSize( &accum->materialLoadInfo->material_datas ) == 1
					  && eaSize( &accum->materialLoadInfo->templates ) == 1,
					  "The material \"NewTemplate\" has gotten corrupted.  "
					  "Contact a programmer to fix this immediately!" );
	
		return accum;
	}
}

/// Create a new Material doc.
static Mated2EditorDoc* mated2DocNewMaterial( const char* type, void* ignored )
{
	Mated2EditorDoc* accum = mated2DocAlloc();

	g_materials_skip_fixup = true;
	if (!ParserLoadFiles( NULL, "materials/system/NewMaterial.Material", NULL, 0, parse_MaterialLoadInfo, accum->materialLoadInfo ))
		if (!ParserLoadFiles( NULL, "materials/system/engine/NewMaterial.Material", NULL, 0, parse_MaterialLoadInfo, accum->materialLoadInfo ))
			assertmsg(0, "Failed to load materials/system/engine/NewMaterial.Material");

	accum->materialLoadInfo->material_datas[ 0 ]->material_name = "";
	g_materials_skip_fixup = false;

	{
		bool ret = mated2FinishLoadMaterial( accum );
		devassertmsg( ret && eaSize( &accum->materialLoadInfo->material_datas ) == 1
					  && eaSize( &accum->materialLoadInfo->templates ) == 0,
					  "The material \"NewMaterial\" has gotten corrupted.  "
					  "Contact a programmer to fix this immediately!" );
	}
	
	return accum;
}

static Mated2EditorDoc* mated2DocLoad( const char* name, const char* type )
{
	Mated2EditorDoc* accum = mated2DocAlloc();
	char filenameLocal[ MAX_PATH ];

	strcpy( filenameLocal, name );
	forwardSlashes( filenameLocal );
	fileRelativePath( filenameLocal, filenameLocal );
	changeFileExt( filenameLocal, ".Material", filenameLocal );

	if(   strstri( filenameLocal, "DefaultTemplate" ) || strstri( filenameLocal, "NewTemplate" )
		  || strstri( filenameLocal, "NewMaterial" )) {
		Alertf( "Cannot edit this file." );
		mated2DocClose( accum );
		return NULL;
	}

	if( !fileExists( filenameLocal )) 
	{
		ResourceInfo *pResInfo = resGetInfo(MATERIAL_DICT, name);
		// See if it's a resource name

		if (pResInfo)
		{
			name = pResInfo->resourceLocation;

			strcpy( filenameLocal, name );
			forwardSlashes( filenameLocal );
			fileRelativePath( filenameLocal, filenameLocal );
			changeFileExt( filenameLocal, ".Material", filenameLocal );
		}

		if (!fileExists(filenameLocal))
		{		
			Alertf( "\"%s\" does not exist.", filenameLocal );
			mated2DocClose( accum );
			return NULL;
		}
	}

	strcpy( accum->emEd.doc_name, name );
	{
		char* dot = strrchr( accum->emEd.doc_name, '.' );
		if( dot ) { *dot = '\0'; }
	}
	strcpy( accum->emEd.doc_display_name, getFileNameConst( accum->emEd.doc_name ));

	g_materials_skip_fixup = true;
	ParserLoadFiles( NULL, filenameLocal, NULL, 0, parse_MaterialLoadInfo, accum->materialLoadInfo );
	g_materials_skip_fixup = false;

	if( mated2DocIsTemplate( accum )) {
		if( eaSize( &accum->materialLoadInfo->material_datas ) == 0 ) {
			Alertf( "Error: File selected has no Materials in it, creating default dummy material." );
			mated2DocTemplateCreateEmptyMaterial( accum );
		}

		if( !mated2FinishLoadTemplate( accum )) {
			mated2DocClose( accum );
			return NULL;
		}

		if( mated2Editor.isTemplateEditingDisabled ) {
			Alertf( "Template editing is disabled." );
			mated2DocClose( accum );
			return NULL;
		} else if( mated2DocIsOneOff( accum )) {
			gfxStatusPrintf( "Loaded One-off Material \"%s\".", name );
		} else {
			gfxStatusPrintf( "Loaded Material Template \"%s\".", name );
		}
	} else {
		if( !mated2FinishLoadMaterial( accum )) {
			mated2DocClose( accum );
			return NULL;
		}
		
		gfxStatusPrintf( "Loaded Material \"%s\".", name );
	}
		
	return accum;
}

static EMTaskStatus mated2DocSave( Mated2EditorDoc* doc )
{
	return mated2DocSave1( doc, false, false, true );
}

static EMTaskStatus mated2DocSaveAs( Mated2EditorDoc* doc )
{
	EMTaskStatus status = mated2DocSave1( doc, true, false, true );
	if( status == EM_TASK_SUCCEEDED ) {
		doc->emEd.saved = true;
	}

	// the name may have changed, we need to repopulate the links
	if( mated2DocIsTemplate( doc )) {
		mated2RepopulateLinks( doc );
	}
	
	return status;
}

static EMTaskStatus mated2DocAutosave( Mated2EditorDoc* doc )
{
	return mated2DocSave1( doc, false, true, false );
}

static const char* mated2DocSaveDefaultDir(
		Mated2EditorDoc* doc, char* buffer, int buffer_size )
{
	if( doc->emEd.doc_name[ 0 ]) {
		strcpy_s( SAFESTR2( buffer ), doc->emEd.doc_name );
		return getDirectoryName( buffer );
	} else if( mated2DocIsTemplate( doc )) {
		return "Materials/Templates/";
	} else {
		return "Materials/";
	}
}

void openRelatedMaterialFiles(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, UserData pData) {
	static bool firstTimeORMF = true;
	if (!firstTimeORMF) {
		char ***openRelatedFiles = (char***)((eaContainer*)pData)->theEArray;

		resDictRemoveEventCallback(MATERIAL_DICT, openRelatedMaterialFiles);
		if (*openRelatedFiles) {
			FOR_EACH_IN_EARRAY(*openRelatedFiles,char,relatedFile) {
				emOpenFileEx(relatedFile,"Material");
			}
			FOR_EACH_END;
		}
		eaDestroyEx(openRelatedFiles,NULL);
		free(openRelatedFiles);
		free(pData);
	}
	firstTimeORMF = !firstTimeORMF;
}

static void mated2UpdatePerfValues(ShaderGraph * graph)
{
	RdrLightType light1;
	RdrLightType light2;
	RdrLightShaderMask mostExpensiveCombo;

	mated2MostExpensiveLightTypes( graph, &light1, &light2, &mostExpensiveCombo );

	gfxMaterialsGetPerformanceValuesEx(graph->graph_render_info,
		&mated2Editor.perfValues,
		getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA,
												rdrGetMaterialShaderType( light1, 0 ) |
												rdrGetMaterialShaderType( light2, 1 )),
		true);
}

/// Internal saving function
static EMTaskStatus mated2DocSave1(
		Mated2EditorDoc* doc, bool forceSaveAs, bool isAutosave,
		bool isMaterialMeasured )
{
	bool isTemplate = mated2DocIsTemplate( doc );
	bool isSaveAs = forceSaveAs || doc->emEd.doc_name[ 0 ] == '\0';
	char ***openRelatedFiles = NULL;

	mated2DocSetAutosave( doc, isAutosave );
	assert( !forceSaveAs || !isAutosave );
	
	if( !isAutosave ) {
		MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
		bool anyFallbackChanged = false;
		int it;

		for( it = 0; it != eaSize( &gfxProps->fallbacks ); ++it ) {
			if( gfxProps->fallbacks[ it ]->fallback_changed ) {
				anyFallbackChanged = true;
				break;
			}
		}

		if( anyFallbackChanged && !mated2QueryOverrideFallbacks( doc )) {
			return EM_TASK_FAILED;
		}

		if( mated2DocIsTemplate( doc ) ) {
			 // && mated2EditorWarnIfTemplateReferenced( doc->shaderTemplate->template_name, doc )) {
			// update materials that are connected to the template with the new template settings and such.
			openRelatedFiles = mated2EditorUpdateMaterialTemplateReferenced( doc->shaderTemplate->template_name, doc );
			if (!openRelatedFiles)
				return EM_TASK_FAILED;
		//	return EM_TASK_FAILED;
		}
	}

	// There's no way to hook into a window getting moved around.
	// Instead, just manually update them on each save.
	if( isTemplate ) {
		int it;
		for( it = 0; it != eaSize( &doc->shaderGraph->operations ); ++it ) {
			mated2NodeUpdatePosition( mated2ShaderOpNode( doc->shaderGraph->operations[ it ]));
		}
	}

	if( isSaveAs && !isAutosave ) {
		char defaultDirBuffer[ MAX_PATH ];
		char fullPath[ MAX_PATH ];
		char saveDir[ MAX_PATH ];
		char saveFile[ MAX_PATH ];
		if( UIOk != ui_ModalFileBrowser( (isTemplate ? "Save Template As" : "Save Material As"),
										 "Save", UIBrowseNew, UIBrowseFiles, false,
										 "Materials",
										 mated2DocSaveDefaultDir( doc, SAFESTR( defaultDirBuffer )),
										 ".Material",
										 SAFESTR( saveDir ), SAFESTR( saveFile ), doc->emEd.doc_name))
		{
			return EM_TASK_FAILED;
		}
		getFileNameNoExt( saveFile, saveFile );
		sprintf( fullPath, "%s/%s", saveDir, saveFile );

		if( !isTemplate && strStartsWith( fullPath, "Materials/Templates/" )) {
			Alertf( "You can not save a material in the Materials/Templates/ "
					"directory." );
			return EM_TASK_FAILED;
		}

		strcpy( doc->emEd.doc_name, fullPath );
		strcpy( doc->emEd.doc_display_name, getFileNameConst( doc->emEd.doc_name ));

		if( isTemplate ) {
			doc->materialLoadInfo->templates[ 0 ]->template_name = allocAddString( doc->emEd.doc_display_name );
			mated2DocGfxProperties( doc )->default_fallback.shader_template_name = allocAddString( doc->emEd.doc_display_name );
			doc->shaderGraph->graph_flags &= ~SGRAPH_NO_CACHING;
		}
		doc->emEd.name_changed = true;
		doc->fileCheckedOut = false;
		doc->fileCheckOutFailed = false;
	}

	{
		char savePath[ MAX_PATH ];

		if( isAutosave ) {
			sprintf( savePath, "%s.Material.autosave", doc->emEd.doc_name );
		} else {
			sprintf( savePath, "%s.Material", doc->emEd.doc_name );

			// even if a previous checkout failed, we should try again here.
			doc->fileCheckOutFailed = false;
			if( !mated2EnsureCheckout( doc )) {
				Alertf( "This file could not be checked out.  Please check it "
						"out before saving." );
				return EM_TASK_FAILED;
			}

			if( eaSize( &doc->validationErrors ) != 0 && !isAutosave ) {
				Alertf( "This file appears to have validation errors.  Please "
						"fix them before checking in." );
			}

			fileMakeLocalBackup( savePath, 60 * 60 * 48 );
			doc->shaderGraph->graph_flags &= ~SGRAPH_NO_CACHING;

			if( isMaterialMeasured ) {
				MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
				Vec3 brightness;

				gfxMatPreviewUpdate( &doc->previewMaterial, &doc->material,
									 doc->shaderTemplate, doc->shaderGraph,
									 "", "", true );

				if( shaderTemplateIsSupported( doc->shaderTemplate )) {
					gfxLockActiveDevice();
					gfxMeasureMaterialBrightness( doc->previewMaterial.preview_material, brightness );
					gfxUnlockActiveDevice();
					
					gfxProps->unlit_contribution = brightness[ 0 ];
					gfxProps->diffuse_contribution = brightness[ 1 ];
					gfxProps->specular_contribution = brightness[ 2 ];
				}
					
				if( mated2DocIsTemplate( doc )) {
					ShaderGraphFeatures defaultFeatures = mated2TemplateDefaultGraphFeature( doc, true );
					ShaderGraph* graph = mated2DocActiveShaderGraph( doc );

					graph->graph_features &= graph->graph_features_overriden;
					graph->graph_features |= (defaultFeatures & ~graph->graph_features_overriden);

					mated2UpdatePerfValues(graph);
					doc->shaderTemplate->score = gfxMaterialScoreFromValues( &mated2Editor.perfValues );
				}
			}
		}

		{
			int result;
			const char** materialName = &doc->materialLoadInfo->material_datas[ 0 ]->material_name;
			const char* oldMaterialName = *materialName;

			*materialName = allocAddString( "" );
			result = ParserWriteTextFile( savePath, parse_MaterialLoadInfo, doc->materialLoadInfo, 0, 0 );
			*materialName = oldMaterialName;

			if( result ) {
				if (openRelatedFiles) {
					if (eaSize(openRelatedFiles) > 0) {
						eaContainer *theContainer = (eaContainer*) calloc (1,sizeof(eaContainer));
						theContainer->theEArray = openRelatedFiles;
						resDictRegisterEventCallback(MATERIAL_DICT,openRelatedMaterialFiles,theContainer);
					}
				}
				return EM_TASK_SUCCEEDED;
			} else {
				if (openRelatedFiles) {
					eaDestroyEx(openRelatedFiles,NULL);
					free(openRelatedFiles);
				}
				return EM_TASK_FAILED;
			}
		}
	}
}

static void mated2DocClose( Mated2EditorDoc* doc )
{
	eaDestroyEx( &doc->previewShaderTypeSkin, ui_SkinFree );
	ui_SkinFree( doc->previewAndErrorSkin );
	ui_SkinFree( doc->errorSkin );
	
	if( doc->undoGroupDepth > 0 ) {
		mated2UndoCancelUnfisihedGroups( doc );
	}
	gfxMatPreviewFreeData( &doc->previewMaterial );

	if( doc->material.material_data ) {
		doc->material.material_data->graphic_props.shader_template = NULL;
	}
	
	if( !mated2DocActiveShaderGraphIsEditable( doc )) {
		StructDestroy( parse_ShaderTemplate, doc->shaderTemplate );
		doc->shaderTemplate = NULL;
	}
	
	// input widgets are freed ONCE, with their nodes.  They should
	// not be freed with the material properties panel.
	if( doc->panelInputList ) {
		emPanelGetExpander( doc->panelInputList )->widget.children = NULL;
		ui_WidgetGroupQueueFree( &doc->inputListNodeNames );
		eaDestroy( &doc->inputListNodeNames );
	}

	// the call to materialUpdateFromData adds a reference for the
	// physical properties.  This needs to be removed to keep the
	// reference system happy.
	REMOVE_HANDLE( doc->material.world_props.physical_properties );
	
	ui_WidgetQueueFree( UI_WIDGET( doc->viewPane ));
	ui_WidgetQueueFree( UI_WIDGET( doc->subgraphPreviewWarning ));
	ui_WidgetQueueFree( UI_WIDGET( doc->validationWindow ));
	ui_WidgetQueueFree( UI_WIDGET( doc->renamesWindow ));

	eaDestroy( &doc->guides );

	emPanelFree( doc->panelTemplateProperties );
	emPanelFree( doc->panelTemplateFeatures );
	emPanelFree( doc->panelMaterialProperties );
	emPanelFree( doc->panelMaterialFallbacks );
	emPanelFree( doc->panelLinks );
	emPanelFree( doc->panelNodeList );
	emPanelFree( doc->panelInputList );
	
	StructDestroy( parse_MaterialLoadInfo, doc->materialLoadInfo );

	EditUndoStackDestroy( doc->emEd.edit_undo_stack );

	stashTableDestroy( doc->renamesTable );
	
	free( doc );
}

static void mated2DocOncePerFrame( Mated2EditorDoc* doc )
{
	if( doc->activeFeatures != systemSpecsMaterialSupportedFeatures() ) {
		int activeFallbackIndex = mated2DocActiveFallbackIndex( doc );
		int autoSetFallbackIndex = mated2DocFallbackIndexByFeatures( doc, systemSpecsMaterialSupportedFeatures() );

		if( activeFallbackIndex != autoSetFallbackIndex && autoSetFallbackIndex != -2 ) {
			MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
			
			ui_ModalDialogSetCustomButtons( "Change", "", "" );
			if( UICustomButton1 == ui_ModalDialog(
						"Change Fallback Too?",
						"Changing the active fallback will clear your undo buffer and "
						"CANNOT BE UNDONE.  Are you sure you want to change the active "
						"fallback?",
						ColorWhite, UICustomButton1 | UINo )) {
				if( autoSetFallbackIndex >= 0 ) {
					mated2DocSetActiveFallback( doc, gfxProps->fallbacks[ autoSetFallbackIndex ]);
				} else {
					mated2DocSetActiveFallback( doc, &gfxProps->default_fallback );
				}
				
				// Also set the fallback selected
				ui_ListSetSelectedRow( doc->listMaterialFallbacks,  autoSetFallbackIndex );
			}
		}

		doc->activeFeatures = systemSpecsMaterialSupportedFeatures();
	}
	
	if( doc->inputListNeedsReflow ) {
		mated2RepopulateInputList( doc );
		doc->inputListNeedsReflow = false;
	}
	
	// deal with resizing
	if( doc->nodeViewIsResizing ) {
		if( mouseIsDown( MS_LEFT )) {
			int mouseX, mouseY;
			F32 canvasX, canvasY, canvasWidth, canvasHeight;
			int maxHeight = doc->nodeViewResizingCanvasHeight - MATED2_TABBAR_HEIGHT;
			int newHeight;
			mousePos( &mouseX, &mouseY );
			emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );

			newHeight = MIN( MAX( doc->nodeViewResizingCanvasHeight - (mouseY - canvasY),
								  100 ),
							 doc->nodeViewResizingCanvasHeight - MATED2_TABBAR_HEIGHT );

			ui_WidgetSetHeight( UI_WIDGET( doc->viewPane ), newHeight );

			ui_WidgetRemoveChild( UI_WIDGET( doc->viewPane ), UI_WIDGET( doc->nodeViewHideButton ));
			ui_WidgetRemoveChild( UI_WIDGET( doc->viewPane ), UI_WIDGET( doc->nodeViewShowButton ));
			if( newHeight == maxHeight ) {
				ui_WidgetAddChild( UI_WIDGET( doc->viewPane ), UI_WIDGET( doc->nodeViewShowButton ));
			} else {
				ui_WidgetAddChild( UI_WIDGET( doc->viewPane ), UI_WIDGET( doc->nodeViewHideButton ));
			} 

		} else {
			doc->nodeViewIsResizing = false;
		}
	}

	{
		Color destBorder = ColorGreen;
		Color destBorderInactive = ColorDarken( ColorGreen, 64 );
		float interpAmount = sin( timeGetTime() / 250.0 ) / 2 + 0.5;

		int it;
		for( it = 0; it != eaSize( &doc->previewShaderTypeSkin ); ++it ) {
			Color srcBorder = mated2ShaderTypeSkin[ it ].border[ 0 ];
			Color srcBorderInactive = mated2ShaderTypeSkin[ it ].border[ 1 ];

			ui_SkinSetBorderEx( doc->previewShaderTypeSkin[ it ],
								ColorLerp( srcBorder, destBorder, interpAmount ),
								ColorLerp( srcBorderInactive, destBorderInactive, interpAmount ));
		}

		{
			Color srcBorder = doc->errorSkin->border[ 0 ];
			Color srcBorderInactive = doc->errorSkin->border[ 1 ];

			ui_SkinSetBorderEx( doc->previewAndErrorSkin,
								ColorLerp( srcBorder, destBorder, interpAmount ),
								ColorLerp( srcBorderInactive, destBorderInactive, interpAmount ));
		}

		{
			Color errColor = ColorLerp( ColorYellow, ColorRed, interpAmount );
			ui_SkinSetBackground( doc->previewAndErrorSkin, errColor );
			ui_SkinSetTitleBar( doc->previewAndErrorSkin, errColor );
			ui_SkinSetBackground( doc->errorSkin, errColor );
			ui_SkinSetTitleBar( doc->errorSkin, errColor );
		}
	}

	mated2DocPerformanceOncePerFrame( doc );
	mated2DocValidateOncePerFrame( doc );

	if( mated2Editor.previewIsRotating ) {
		Vec3 rpy;

		rpy[ 0 ] = -0.25 * gfxGetFrameTime();
		rpy[ 1 ] = -0.50 * gfxGetFrameTime();
		rpy[ 2 ] = -0.75 * gfxGetFrameTime();

		rotateMat3( rpy, mated2Editor.previewObjectMatrix );
	}

	if( mated2Editor.previewMode == PREVIEW_IN_WORLD ) {
		wleRayCollideUpdate();
	} else {
		wleRayCollideClear();
	}

	// renames
	if( stashGetCount( doc->renamesTable )) {
		char* text = NULL;
		estrCreate( &text );

		FOR_EACH_IN_STASHTABLE2( doc->renamesTable, it ) {
			const char* from = stashElementGetPointer( it );
			const char* to = stashElementGetKey( it );

			estrConcatf( &text, "%s -> %s<br>", from, to );
		} FOR_EACH_END;

		ui_SMFViewSetText( doc->renamesText, text, NULL );
		if( !doc->renamesWindow->widget.group ) {
			//ui_WidgetAddToDevice( UI_WIDGET( doc->renamesWindow ), NULL );
		}
		
		estrDestroy( &text );
	} else {
		if( doc->renamesWindow->widget.group ) {
			ui_WidgetRemoveFromGroup( UI_WIDGET( doc->renamesWindow ));
		}
	}
}

static void mated2DocDrawGhosts( Mated2EditorDoc* doc )
{
	if( mated2Editor.previewMode != PREVIEW_SAME_WINDOW ) {
		return;
	}
	
	mated2DrawPreview( doc );
}

Mated2Node* mated2PreviewSelectedNode( Mated2EditorDoc* doc )
{
	return doc->previewSelectedNode;
}

void mated2PreviewSetSelectedNode( Mated2EditorDoc* doc, Mated2Node* node )
{
	if( doc->previewSelectedNode ) {
		mated2NodeSetIsPreview( doc->previewSelectedNode, false );
	}
	
	doc->previewSelectedNode = node;

	if( node ) {
		mated2NodeSetIsPreview( node, true );
	}

	if(   node && GET_REF( mated2NodeShaderOp( node )->h_op_definition )
		  && GET_REF( mated2NodeShaderOp( node )->h_op_definition )->op_type != SOT_SINK ) {
		if( !doc->subgraphPreviewWarningIsActive ) {
			ui_WidgetAddToDevice( UI_WIDGET( doc->subgraphPreviewWarning ), NULL );
		}
		doc->subgraphPreviewWarningIsActive = true;
	} else {
		if( doc->subgraphPreviewWarningIsActive ) {
			ui_WidgetRemoveFromGroup( UI_WIDGET( doc->subgraphPreviewWarning ));
		}
		doc->subgraphPreviewWarningIsActive = false;
	}
}


static void mated2DocPerformanceOncePerFrame( Mated2EditorDoc* doc )
{
	// previews are needed to get perf values
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	// Reset perf values, the rest of the function will update what it can.
	mated2Editor.constantCountMaxLights = -1;
	mated2Editor.constantCount2Lights = -1;
	mated2Editor.constantCountSM20 = -1;
	mated2Editor.instructionCountMaxLights = -1;
	mated2Editor.instructionCount2Lights = -1;
	mated2Editor.instructionCountSM20 = -1;

	rdr_state.runNvPerfShader = mated2Editor.lastFrameIsNVPerfShown;
	mated2Editor.lastFrameIsNVPerfShown = false;
	
	mated2UpdatePerfValues( mated2DocActiveShaderGraph( doc ));
	gfxMaterialsGetMemoryUsage( &doc->material, &mated2Editor.perfTotalMemory, &mated2Editor.perfSharedMemory );

	if( mated2DocIsTemplate( doc )) {
		// If gfxGetPerformanceValues is called syncronously, it eats
		// up all keyboard input.  Since this is called once per
		// frame, we can't have that happen.
		ShaderGraphFeatures defaultFeatures = mated2TemplateDefaultGraphFeature( doc, false );
		ShaderGraph* graph = mated2DocActiveShaderGraph( doc );

		graph->graph_features &= graph->graph_features_overriden;
		graph->graph_features |= (defaultFeatures & ~graph->graph_features_overriden);
		{
			int it;
			for( it = 0; it != ARRAY_SIZE( doc->templateFeatureCheckButtons ); ++it ) {
				UICheckButton* checkButton = doc->templateFeatureCheckButtons[ it ];

				if( checkButton ) {
					ui_CheckButtonSetState( checkButton, (graph->graph_features & (1 << it)) != 0 );
				}
			}
		}
	} 
}

/// Validate DOC and record any errors.  Called once per frame.
static void mated2DocValidateOncePerFrame( Mated2EditorDoc* doc )
{
	char badFlagsAccum[ 1024 ] = { '\0' };

	if( !doc->validationNeedsUpdate ) {
		return;
	}
	if( doc->validationUpdateDelayFrames > 0 ) {
		--doc->validationUpdateDelayFrames;
		return;
	}

	eaClearEx( &doc->validationWarnings, NULL );
	eaClearEx( &doc->validationErrors, NULL );

	{
		int it;
		for( it = 0; it != ARRAY_SIZE( doc->materialPropertyFlags ); ++it ) {
			if(   mated2MaterialPropertyInfo[ it ].causes_alpha
				  && doc->materialPropertyFlags[ it ]) {
				if( badFlagsAccum[ 0 ]) {
					strcat( badFlagsAccum, ", " );
				}
				strcat( badFlagsAccum, mated2MaterialPropertyInfo[ it ].name );
			}
		}
	}

	if( !shaderTemplateIsSupported( doc->shaderTemplate )) {
		eaPush( &doc->validationWarnings,
				strdup( "Warning: The Material brightness values can not be "
						"updated because this template is too complicated for "
						"your video card.  Please save this on a better "
						"computer." ));
	}

	if(   (doc->shaderTemplate->graph->graph_features & SGFEAT_SM20) == 0 ) {
		eaPush( &doc->validationWarnings,
				strdup( "Warning: This Material or Template is marked as "
						"super low end, but does not run on SM2.  It will not "
						"run on SM2 cards." ));
	}

	if( badFlagsAccum[ 0 ]) {
		char buffer[ 1024 ];
		sprintf( buffer,
				 "Warning: This template has a flag set upon it which will "
				 "cause it to be alpha sorted, resulting in poorer performance "
				 "and lower quality lighting/effects being applied to this "
				 "material.\n"
				 "Offending flags: %s",
				 badFlagsAccum );
		
		eaPush( &doc->validationWarnings, strdup( buffer ));
	} else if( doc->previewMaterial.preview_material		//< The first frame of existance, this
															//< material does not yet exist.
			   && doc->previewMaterial.preview_material->graphic_props.has_transparency ) {
		eaPush( &doc->validationWarnings, strdup(
						"Warning: This Material or Template is detected to "
						"have transparency, and will not be used as an "
						"occluder and will get slightly worse drawing "
						"performance.  The template may need to be updated "
						"to be more specific on where the alpha comes from." ));
	}
	
	callbackContext = doc;
	ErrorfPushCallback( mated2ValidateCallback , NULL);
	pushDontReportErrorsToErrorTracker( true );
	pushDisableLastAuthor( true );
	{
		bool templateIsBadAccum = false;
		bool materialIsBadAccum = false;
		
		if( mated2DocIsTemplate( doc )) {
			if( !materialShaderTemplateValidate( doc->shaderTemplate, true )) {
				templateIsBadAccum = true;
			}

			materialForEachMaterialName( mated2ValidateMaterialByName, doc );
		}

		if( !mated2MaterialValidate( doc, doc->materialLoadInfo->material_datas[ 0 ], true )) {
			materialIsBadAccum = true;
			if( mated2DocIsTemplate( doc )) {
				templateIsBadAccum = true;
			}
		}
		if( !GET_REF( mated2DocWorldProperties( doc )->physical_properties )) {
			mated2DocErrorf( doc, "Missing or invalid PhysicalProperties specified on material." );
		}
	}
	popDisableLastAuthor();
	popDontReportErrorsToErrorTracker();
	ErrorfPopCallback();
	callbackContext = NULL;

	// draw status
	if(   eaSize( &doc->validationWarnings ) > 0
		  || eaSize( &doc->validationErrors ) > 0 ) {
		char* accum = NULL; 
		estrStackCreate( &accum );
		
		if( eaSize( &doc->validationErrors ) > 0 ) {
			estrConcatStatic( &accum, "<font color=Red><b>ERRORS:</b></font><br>" );

			{
				int it;
				for( it = 0; it != eaSize( &doc->validationErrors ); ++it ) {
					char* str = estrCreateFromStr( doc->validationErrors[ it ]);
					estrReplaceOccurrences( &str, "\n", "<br>" );
					estrConcatf( &accum, "<font color=Red>%s</font><br><br>", str );
					estrDestroy( &str );
				}
			}
			estrConcatf( &accum, "<br>" );
		}
		if( eaSize( &doc->validationWarnings ) > 0 ) {
			estrConcatStatic( &accum, "<b>WARNINGS:</b><br>" );

			{
				int it;
				for( it = 0; it != eaSize( &doc->validationWarnings ); ++it ) {
					char* str = estrCreateFromStr( doc->validationWarnings[ it ]);
					estrReplaceOccurrences( &str, "\n", "<br>" );
					estrConcatf( &accum, "%s<br><br>", str );
					estrDestroy( &str );
				}
			}
			estrConcatf( &accum, "<br>" );
		}

		ui_SMFViewSetText( doc->validationText, accum, NULL );
		ui_WidgetSetDimensionsEx( UI_WIDGET( doc->validationText ),
								  1, ui_SMFViewGetHeight( doc->validationText ),
								  UIUnitPercentage, UIUnitFixed );
		estrDestroy( &accum );

		{
			char title[ 256 ];
			sprintf( title, "Validation (%d Error%s, %d Warning%s)",
					 eaSize( &doc->validationErrors ), eaSize( &doc->validationErrors ) != 1 ? "s" : "",
					 eaSize( &doc->validationWarnings ), eaSize( &doc->validationWarnings ) != 1 ? "s" : "" );
			ui_WindowSetTitle( doc->validationWindow, title );
		}
		
		if( !doc->validationWindow->widget.group ) {
			ui_WidgetAddToDevice( UI_WIDGET( doc->validationWindow ), NULL );
		}
	} else {
		if( doc->validationWindow->widget.group ) {
			ui_WidgetRemoveFromGroup( UI_WIDGET( doc->validationWindow ));
		}
	}
	
	doc->validationNeedsUpdate = false;
}

static void mated2ValidateMaterialByName( Mated2EditorDoc* doc, const char* materialName )
{
	const MaterialData* materialData = materialFindData( materialName );
	assert( materialData );

	if( materialDataHasShaderTemplate( materialData, doc->shaderTemplate->template_name )
		&& 0 != stricmp( materialData->material_name,
						 doc->materialLoadInfo->material_datas[ 0 ]->material_name )) {
		mated2MaterialValidate( doc, (MaterialData*)materialData, false ); //< safe to cast away const, since bRepair=false
	}
}

static void mated2ValidateCallback( ErrorMessage *errMsg, void *userdata )
{
	const char *errString = errorFormatErrorMessage(errMsg);
	Mated2EditorDoc* doc = callbackContext;

	eaPush( &doc->validationErrors, strdup( errString ));
}

bool old_disable_sky_volume_value;

static void mated2DocGotFocus( Mated2EditorDoc* doc )
{
	old_disable_sky_volume_value = gfxGetDisableVolumeSkies();
	gfxSetDisableVolumeSkies(true);
	if( !doc ) {
		return;
	}
	mated2Editor.activeDoc = doc;
	
	motGotFocus( mated2Editor.previewToolbar );
	
	ui_WidgetAddToDevice( UI_WIDGET( doc->viewPane ), NULL );
	if( doc->subgraphPreviewWarningIsActive ) {
		ui_WidgetAddToDevice( UI_WIDGET( doc->subgraphPreviewWarning ), NULL );
	}

	if( mated2Editor.previewMode == PREVIEW_OTHER_WINDOW ) {
		rdrShowWindow( mated2Editor.previewDevice, SW_SHOW );
	}
}

static void mated2DocLostFocus( Mated2EditorDoc* doc )
{
	mated2Editor.activeDoc = NULL;


	gfxSetDisableVolumeSkies(old_disable_sky_volume_value);
	
	motLostFocus( mated2Editor.previewToolbar );
	
	ui_WidgetRemoveFromGroup( UI_WIDGET( doc->viewPane ));
	ui_WidgetRemoveFromGroup( UI_WIDGET( doc->subgraphPreviewWarning ));
	ui_WidgetRemoveFromGroup( UI_WIDGET( doc->validationWindow ));
	ui_WidgetRemoveFromGroup( UI_WIDGET( doc->renamesWindow ));
	doc->nodeViewIsResizing = false;

	if( mated2Editor.previewMode == PREVIEW_OTHER_WINDOW ) {
		rdrShowWindow( mated2Editor.previewDevice, SW_HIDE );
	}
}

static void mated2DocSetAutosave( Mated2EditorDoc* doc, bool isAutosave )
{
	int it;

	for( it = 0; it != eaSize( &doc->materialLoadInfo->templates ); ++it ) {
		doc->materialLoadInfo->templates[ it ]->is_autosave = isAutosave;
	}
	for( it = 0; it != eaSize( &doc->materialLoadInfo->material_datas ); ++it ) {
		doc->materialLoadInfo->material_datas[ it ]->is_autosave = isAutosave;
	}
	
}

/// Draw the active preview
static void mated2DrawPreview( Mated2EditorDoc* doc )
{
	mated2Editor.emEd.camera->override_no_fog_clip = 1;

	{
		Mated2Node* selectedNode = mated2PreviewSelectedNode( doc );

		doc->material.graphic_props.material_clean = false;

		gfxMatPreviewUpdate( &doc->previewMaterial, &doc->material,
							 doc->shaderTemplate, doc->shaderGraph,
							 (selectedNode ? mated2NodeName( selectedNode ) : ""),
							 mated2DocActiveFallback( doc )->shader_template_name,
							 true );
	}

	gfxFillDrawList( false, NULL );

	{
		SingleModelParams params = { 0 };
		motGetTintColor0( mated2Editor.previewToolbar, params.color );
		params.alpha = motGetTintAlpha(mated2Editor.previewToolbar);
		params.eaNamedConstants = gfxMaterialStaticTintColorArray( onevec3 );
		motGetTintColor1( mated2Editor.previewToolbar, params.eaNamedConstants[ 0 ]->value );
		params.model = mated2Editor.previewObject;
		params.material_replace = doc->previewMaterial.preview_material;
		params.material_replace->world_props.usage_flags |= WL_FOR_WORLD;
		copyMat4( mated2Editor.previewObjectMatrix, params.world_mat );
		gfxQueueSingleModelTinted( &params, -1 );
	}
}

static void mated2DocTemplateCreateEmptyMaterial( Mated2EditorDoc* doc )
{
	MaterialData* accum;

	assert( eaSize( &doc->materialLoadInfo->templates ) != 0 );
	assert( eaSize( &doc->materialLoadInfo->material_datas ) == 0 );

	accum = calloc( 1, sizeof( *accum ));
	{
		char buffer[ 1024 ];
		sprintf( buffer, "%s_default", doc->materialLoadInfo->templates[ 0 ]->template_name );
		accum->material_name = allocAddString( buffer );
	}
	accum->filename = allocAddString( doc->materialLoadInfo->templates[ 0 ]->filename );
	mated2MaterialValidate( doc, accum, true );			//< Fix it up, add missing values, etc.
	eaPush( &doc->materialLoadInfo->material_datas, accum );
}


static void mated2DocErrorf( Mated2EditorDoc* doc, const char* fmt, ... )
{
	char filename[ MAX_PATH ];
	va_list params;

	va_start( params, fmt);
	sprintf( filename, "%s.Material", doc->emEd.doc_name );
	ErrorFilenamevInternal( NULL, 0, filename, fmt, params );
	va_end( params );
}

static void mated2UpdateDependantMaterial( Mated2EditorDoc* doc, const char* filename )
{
	char filenameLocal[ MAX_PATH ];
	strcpy( filenameLocal, filename );
	forwardSlashes( filenameLocal );
	fileRelativePath( filenameLocal, filenameLocal );
	
	printf( "Updating %s...\n", filenameLocal );
	if( !mated2EnsureCheckoutFile( filenameLocal )) {
		return;
	}

	{
		MaterialLoadInfo materialLoadInfo = { 0 };
		
		g_materials_skip_fixup = true;
		ParserLoadFiles( NULL, filenameLocal, NULL, 0, parse_MaterialLoadInfo, &materialLoadInfo );
		g_materials_skip_fixup = false;

		assert( eaSize( &materialLoadInfo.material_datas ) == eaSize( &doc->materialLoadInfo->material_datas )
 				&& eaSize( &materialLoadInfo.material_datas ) == 1 );

		mated2MaterialValidate( doc, materialLoadInfo.material_datas[ 0 ], true );

		ParserWriteTextFile( filenameLocal, parse_MaterialLoadInfo, &materialLoadInfo, 0, 0 );
	}
}

static bool mated2UpdateFallbacksDependantMaterial( Mated2EditorDoc* doc, const char* filename )
{
	char filenameLocal[ MAX_PATH ];
	strcpy( filenameLocal, filename );
	forwardSlashes( filenameLocal );
	fileRelativePath( filenameLocal, filenameLocal );

	printf( "Updating %s...", filenameLocal );
	if( !mated2EnsureCheckoutFile( filenameLocal )) {
		return true;
	}
	
	{
		MaterialLoadInfo materialLoadInfo = { 0 };
		
		g_materials_skip_fixup = true;
		ParserLoadFiles( NULL, filenameLocal, NULL, 0, parse_MaterialLoadInfo, &materialLoadInfo );
		g_materials_skip_fixup = false;
		
		assert( eaSize( &materialLoadInfo.templates ) == 0 );
		assert( eaSize( &materialLoadInfo.material_datas ) == eaSize( &doc->materialLoadInfo->material_datas )
				&& eaSize( &materialLoadInfo.material_datas ) == 1 );

		if( materialLoadInfo.material_datas[ 0 ]->graphic_props.fallbacks_overriden ) {
			printf( "Fallbacks are overriden, not updating.\n" );
			return false;
		}

		eaCopyStructs( &mated2DocGfxProperties( doc )->fallbacks,
					   &materialLoadInfo.material_datas[ 0 ]->graphic_props.fallbacks,
					   parse_MaterialFallback );
			
		mated2MaterialValidate( doc, materialLoadInfo.material_datas[ 0 ], true );

		materialLoadInfo.material_datas[0]->material_name = "";
		ParserWriteTextFile( filenameLocal, parse_MaterialLoadInfo, &materialLoadInfo, 0, 0 );
		printf( "\n" );
	}

	return true;
}

static bool mated2ApplyRenamesDependantMaterial( Mated2EditorDoc* doc, const char* filename )
{
	char filenameLocal[ MAX_PATH ];
	strcpy( filenameLocal, filename );
	forwardSlashes( filenameLocal );
	fileRelativePath( filenameLocal, filenameLocal );
	printf( "Updating %s...", filenameLocal );
	if( !mated2EnsureCheckoutFile( filenameLocal )) {
		return false;
	}

	{
		MaterialLoadInfo materialLoadInfo = { 0 };

		g_materials_skip_fixup = true;
		ParserLoadFiles( NULL, filenameLocal, NULL, 0, parse_MaterialLoadInfo, &materialLoadInfo );
		g_materials_skip_fixup = false;

		assert( eaSize( &materialLoadInfo.templates ) == 0 );
		assert( eaSize( &materialLoadInfo.material_datas ) == eaSize( &doc->materialLoadInfo->material_datas )
				&& eaSize( &materialLoadInfo.material_datas ) == 1 );

		FOR_EACH_IN_STASHTABLE2( doc->renamesTable, it ) {
			MaterialData* material = materialLoadInfo.material_datas[ 0 ];
			ShaderOperationValues* op;
			const char* from = stashElementGetPointer( it );
			const char* to = stashElementGetKey( it );

			op = materialFindOperationValues( material, &material->graphic_props.default_fallback, from );
			if( op ) {
				op->op_name = to;
			}
		} FOR_EACH_END;
		
		mated2MaterialValidate( doc, materialLoadInfo.material_datas[ 0 ], true );

		materialLoadInfo.material_datas[0]->material_name = "";
		ParserWriteTextFile( filenameLocal, parse_MaterialLoadInfo, &materialLoadInfo, 0, 0 );
		printf( "\n" );
	}

	return true;
}

/// Check if DOC should be allowed to have its fallbacks overriden.
///
/// Will ask the user if they're sure, if DOC has not yet had its
/// fallbacks overriden yet.
static bool mated2QueryOverrideFallbacks( Mated2EditorDoc* doc )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	
	// templates are never "overriding"
	if( mated2DocIsTemplate( doc )) {
		return true;
	}
	// already overriden -- the damage has been done
	if( gfxProps->fallbacks_overriden ) {
		return true;
	}

	ui_ModalDialogSetCustomButtons( "Override", "Cancel", "" );
	if( UICustomButton1 != ui_ModalDialog(
				"Warning",
				"You are about to override the fallback setup by Tech Art for this\n"
				"material. You should not be changing this without specific\n"
				"permission from Sean.",
				ColorWhite, UICustomButton1 | UICustomButton2 )) {
		return false;
	}

	gfxProps->fallbacks_overriden = true;
	return true;
}

typedef struct Mated2AllWhoUseMeInfo {
	const char* templateName;
	bool includeUseByFallback;
	
	char** accum;
} Mated2AllWhoUseMeInfo;

static void mated2AllWhoUseMeSub( Mated2AllWhoUseMeInfo* info, const char* name )
{
	const char* templateName = info->templateName;
	const MaterialData* materialData = materialFindData( name );
	char templateMaterialName[ 256 ];
	assert( materialData );

	sprintf( templateMaterialName, "%s_default", templateName );

	if( stricmp( materialData->material_name, templateMaterialName ) == 0 ) {
		return;
	}
	if( info->includeUseByFallback ) {
		if( !materialDataHasShaderTemplate( materialData, templateName )) {
			return;
		}
	} else {
		if( stricmp( materialData->graphic_props.default_fallback.shader_template_name,
					 templateName ) != 0 ) {
			return;
		}
	}

	eaPush( &info->accum, (char*)materialData->filename );
}

static char** mated2AllWhoUseMeByName( const char* templateName, bool includeUseByFallback )
{
	Mated2AllWhoUseMeInfo info = { templateName, includeUseByFallback, NULL };
	materialForEachMaterialName( mated2AllWhoUseMeSub, &info );

	return info.accum;
}

/// Get a list of all files with materials that use DOC's template.
static char** mated2AllWhoUseMe( Mated2EditorDoc* doc, bool includeUseByFallback )
{
	return mated2AllWhoUseMeByName( doc->materialLoadInfo->templates[ 0 ]->template_name, includeUseByFallback );
}

static void mated2AllWithSuspiciousBrightnessSub( char*** accum, const char* name )
{
	const MaterialData* materialData = materialFindData( name );

	if( !materialData ) {
		return;
	}

	if(   materialData->graphic_props.unlit_contribution < MAX_MATERIAL_BRIGHTNESS
		  && materialData->graphic_props.specular_contribution < MAX_MATERIAL_BRIGHTNESS
		  && materialData->graphic_props.diffuse_contribution < MAX_MATERIAL_BRIGHTNESS ) {
		return;
	}

	eaPush( accum, (char*)materialData->filename );
}

/// Get a list of all the files that have a suspicious brightness.
static char** mated2AllWithSuspiciousBrightness( void )
{
	char** accum = NULL;
	materialForEachMaterialName( mated2AllWithSuspiciousBrightnessSub, &accum );
	return accum;
}

static void mated2AllMaterialsSub( char*** accum, const char* name )
{
	const MaterialData* materialData = materialFindData( name );

	if( !materialData ) {
		return;
	}

	eaPush( accum, (char*)materialData->filename );
}

/// Get a list of all the materials.
static char** mated2AllMaterials( void )
{
	char** accum = NULL;
	materialForEachMaterialName( mated2AllMaterialsSub, &accum );
	return accum;
}

/// Get a list of all the material templates.
static const char** mated2AllMaterialTemplates( void )
{
	const char** accum = NULL;

	int it;
	for( it = 0; it != eaSize( &material_load_info.templates ); ++it ) {
		eaPush( &accum, material_load_info.templates[ it ]->filename );
	}

	return accum;
}

/// Overriding Tick function for the editor pane.
static void mated2ViewPaneTick( UIPane* pane, UI_PARENT_ARGS )
{
	EMEditorDoc* rawDoc = emGetActiveEditorDoc();
	bool connectingShouldFinish = false;
	
	UI_GET_COORDINATES(pane);

	if( rawDoc && ((Mated2EditorDoc*)rawDoc)->viewPane == pane ) {
		Mated2EditorDoc* doc = (Mated2EditorDoc*)rawDoc;

		if( doc->flowchart->connecting && (mouseDown( MS_LEFT ) || mouseDown( MS_MID ) || mouseDown( MS_RIGHT )) ) {
			connectingShouldFinish = true;
		}
	}	
	ui_PaneTick( pane, UI_PARENT_VALUES );

	if( rawDoc && ((Mated2EditorDoc*)rawDoc)->viewPane == pane ) {
		Mated2EditorDoc* doc = (Mated2EditorDoc*)rawDoc;

		if( connectingShouldFinish ) {
			doc->flowchart->connecting = NULL;
		}
	}
}

/// Overriding Tick function for the subgraph preview warning label.
static void mated2SubgraphPreviewWarningTick( UILabel* label, UI_PARENT_ARGS )
{
	UI_GET_COORDINATES(label);

	if( mouseClickHit( MS_LEFT, &box )) {
		Mated2EditorDoc* doc = (Mated2EditorDoc*)emGetActiveEditorDoc();
		
		assert( doc );
		mated2PreviewSetSelectedNode( doc, NULL );
	}
}

/// Hide or show the node view window.
static void mated2NodeViewHideShowClicked( UIButton* ignored, Mated2EditorDoc* doc )
{
	F32 canvasX, canvasY, canvasWidth, canvasHeight;
	emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );
	
	ui_WidgetRemoveChild( UI_WIDGET( doc->viewPane ), UI_WIDGET( doc->nodeViewHideButton ));
	ui_WidgetRemoveChild( UI_WIDGET( doc->viewPane ), UI_WIDGET( doc->nodeViewShowButton ));
	
	if( doc->viewPane->widget.height < canvasHeight - MATED2_TABBAR_HEIGHT ) {
		ui_WidgetSetHeightEx( UI_WIDGET( doc->viewPane ), canvasHeight - MATED2_TABBAR_HEIGHT, UIUnitFixed );
		ui_WidgetAddChild( UI_WIDGET( doc->viewPane ), UI_WIDGET( doc->nodeViewShowButton ));
	} else {
		ui_WidgetSetHeightEx( UI_WIDGET( doc->viewPane ), (canvasHeight - MATED2_TABBAR_HEIGHT) / 2, UIUnitFixed );
		ui_WidgetAddChild( UI_WIDGET( doc->viewPane ), UI_WIDGET( doc->nodeViewHideButton ));
	}
}

/// Expand the node view window.
static void mated2NodeViewExpandDrag( UIButton* button, Mated2EditorDoc* doc )
{
	int x, y;
	F32 canvasX, canvasY, canvasWidth, canvasHeight;
	mousePos( &x, &y );
	emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );
	
	ui_DragStart( UI_WIDGET( button ), "mated2_node_view_expand", NULL, atlasLoadTexture( "eui_pointer_arrows_vert" ));
	doc->nodeViewIsResizing = true;
	doc->nodeViewResizingCanvasHeight = canvasHeight;
}

static void mated2DocCopyLinks( UIButton* button, Mated2EditorDoc* doc )
{
	winCopyToClipboard( ui_TextAreaGetText( doc->textAreaLinks ));
}

static int stableSortStringsComp( const char** str1, const char** str2, const void* ignored )
{
	return stricmp( *str1, *str2 );
}

/// Get a list of all the shader values' and input mappings' names.
static const char** mated2DocShaderInputNameList( Mated2EditorDoc* doc )
{
	ShaderGraph* graph = mated2DocActiveShaderGraph( doc );
	const char** accum = NULL;
	int it;

	for( it = 0; it != eaSize( &graph->operations ); ++it ) {
		ShaderOperation* op = graph->operations[ it ];
		const ShaderOperationDef* opDef = GET_REF( op->h_op_definition );
		int inputIt;
		bool allInputsHiddenAccum = true;

		for( inputIt = 0; inputIt != eaSize( &opDef->op_inputs ); ++inputIt ) {
			const ShaderInput* input = opDef->op_inputs[ inputIt ];
			if(   (input->input_default.default_type == SIDT_NODEFAULT
				   || input->input_not_for_assembler)
				  && !input->input_hidden
				  && !materialFindInputEdgeByNameConst( op, input->input_name )
				  && !materialFindFixedInputByNameConst( op, input->input_name )) {
				allInputsHiddenAccum = false;
				break;
			}
		}

		if( !allInputsHiddenAccum ) {
			eaPush( &accum, op->op_name );
		}
	}

	eaStableSort( accum, NULL, stableSortStringsComp );
	return accum;
}

static int stableSortNodesComp( const Mated2Node** node1, const Mated2Node** node2, const void* ignored )
{
	const char* group1 = mated2NodeGroup( *node1 );
	const char* group2 = mated2NodeGroup( *node2 );

	if( group1[ 0 ] == '\0' && group2[ 0 ] == '\0' ) {
		return 0;
	} else if( group1[ 0 ] == '\0' ) {
		return -1;
	} else if( group2[ 0 ] == '\0' ) {
		return +1;
	} else {
		return stricmp( group1, group2 );
	}
}

/// Get a list of all the nodes used as shader values, sorted by group name,
/// then by input name.
static Mated2Node** mated2DocNodesSortedByGroup( SA_PARAM_NN_VALID Mated2EditorDoc* doc )
{
	const char** inputNames = mated2DocShaderInputNameList( doc );
	Mated2Node** accum = NULL;

	int it;
	for( it = 0; it != eaSize( &inputNames ); ++it ) {
		Mated2Node* node = mated2FindNodeByName( doc, inputNames[ it ]);
		if( node ) {
			eaPush( &accum, node );
		}
	}

	eaDestroy( &inputNames );
	eaStableSort( accum, NULL, stableSortNodesComp );

	return accum;
}

/// Create the template-specific properties panel.
static void mated2PanelCreateTemplateProperties( Mated2EditorDoc* doc )
{
	EMPanel* accum = emPanelCreate( "Properties", "Template Properties", 0 );
	int yIt = 0;
	int it;

	for( it = 0; it != ARRAY_SIZE( mated2TemplatePropertyInfo ); ++it ) {
		const Mated2PropertyInfo* propertyInfo = &mated2TemplatePropertyInfo[ it ];
		UICheckButton* checkButton;
		if (propertyInfo->flag == SGRAPH_USE_AMBIENT_CUBE && gConf.bForceEnableAmbientCube)
			continue;
		checkButton = ui_CheckButtonCreate( 10, 20 * it, propertyInfo->name, false );
		ui_WidgetSetTooltipString( UI_WIDGET( checkButton ), propertyInfo->tooltip );
		ui_CheckButtonSetToggledCallback( checkButton, mated2TemplatePropertyChanged, doc );
		checkButton->statePtr = &doc->templatePropertyFlags[ it ];
		emPanelAddChildYIt( accum, checkButton, false, &yIt );
	}

	{
		UILabel* label = ui_LabelCreate( "Reflection Type:", 0, 0 );
		UIComboBox* cbox;
		int xIt = 0;
		cbox = ui_ComboBoxCreateWithEnum( 0, 0, 100, ShaderGraphReflectionTypeEnum, mated2DocReflectionTypeChanged, doc );

		ui_WidgetSetPosition( UI_WIDGET( label ), xIt, yIt );
		emPanelAddChild( accum, label, false );
		xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 4;

		ui_WidgetSetPosition( UI_WIDGET( cbox ), xIt, yIt );
		ui_WidgetSetWidthEx( UI_WIDGET( cbox ), 1, UIUnitPercentage );
		emPanelAddChildYIt( accum, cbox, false, &yIt );

		doc->comboBoxReflectionType = cbox;
	}

	{
		UILabel* label = ui_LabelCreate( "Material Quality:", 0, 0 );
		UIComboBox* cbox;
		int xIt = 0;
		cbox = ui_ComboBoxCreateWithEnum( 0, 0, 100, ShaderGraphQualityEnum, mated2DocShaderQualityChanged, doc );

		ui_WidgetSetPosition( UI_WIDGET( label ), xIt, yIt );
		emPanelAddChild( accum, label, false );
		xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 4;

		ui_WidgetSetPosition( UI_WIDGET( cbox ), xIt, yIt );
		ui_WidgetSetWidthEx( UI_WIDGET( cbox ), 1, UIUnitPercentage );
		emPanelAddChildYIt( accum, cbox, false, &yIt );

		doc->comboBoxShaderQuality = cbox;
	}

	emPanelUpdateHeight( accum );	
	emPanelSetOpened( accum, true );

	doc->panelTemplateProperties = accum;
}

/// Create the template features panel.
static void mated2PanelCreateTemplateFeatures( Mated2EditorDoc* doc )
{
	EMPanel* accum = emPanelCreate( "Properties", "Template Features", 0 );
	int yIt = 0;
	int it;

	for( it = 0; it != 32; ++it ) {
		const char* featureName = StaticDefineIntRevLookup( ShaderGraphFeaturesEnum, (1 << it));
		if( featureName ) {
			UICheckButton* checkButton = ui_CheckButtonCreate( 10, yIt, featureName, false );
			UIButton* button = ui_ButtonCreate( "Reset to Default", 120, yIt, mated2TemplateFeatureReset, doc );
			ui_CheckButtonSetToggledCallback( checkButton, mated2TemplateFeatureChanged, doc );
			
			doc->templateFeatureCheckButtons[ it ] = checkButton;
			doc->templateFeatureResetButtons[ it ] = button;
			
			emPanelAddChild( accum, checkButton, false );
			emPanelAddChildYIt( accum, button, false, &yIt );

			// now, hide the UIButton so it's not drawn...
			emPanelRemoveChild( accum, button, false );
		}
	}
	emPanelUpdateHeight( accum );
	emPanelSetOpened( accum, true );
	
	doc->panelTemplateFeatures = accum;
}

/// Create the material properties panel.
static void mated2PanelCreateMaterialProperties( Mated2EditorDoc* doc )
{
	EMPanel* accum = emPanelCreate( "Properties", "Material Properties", 0 );
	int yIt = 0;
	int it;

	{
		UIButton* button = ui_ButtonCreate( "Physical Props: <UNFILLED>", 10, yIt, mated2SelectPhysicalProperties, doc );
		ui_WidgetSetWidthEx( UI_WIDGET( button ), 1, UIUnitPercentage );
		emPanelAddChildYIt( accum, button, false, &yIt );
		yIt -= 5;

		doc->physicalPropertiesButton = button;
	}


	{
		UILabel* label = ui_LabelCreate( "Max Reflection Resolution:", 0, 0 );
		UIComboBox* cbox;
		int xIt = 0;
		static StaticDefineInt ReflectionTypeEnumStrings[] =
		{
			DEFINE_INT
			{ "Default", 0},
			{ "2x2", 1},
			{ "4x4", 2},
			{ "8x8", 3},
			{ "16x16", 4},
			{ "32x32", 5},
			{ "64x64", 6},
			{ "128x128", 7},
			{ "256x256", 8},
			DEFINE_END
		};
		cbox = ui_ComboBoxCreateWithEnum( 0, 0, 100, ReflectionTypeEnumStrings, mated2DocMaxReflectionResolutionChanged, doc );
		ui_ComboBoxSetHoverCallback(cbox, mated2DocMaxReflectionResolutionHover, doc);

		ui_WidgetSetPosition( UI_WIDGET( label ), xIt, yIt );
		emPanelAddChild( accum, label, false );
		xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 4;

		ui_WidgetSetPosition( UI_WIDGET( cbox ), xIt, yIt );
		ui_WidgetSetWidthEx( UI_WIDGET( cbox ), 1, UIUnitPercentage );
		emPanelAddChildYIt( accum, cbox, false, &yIt );

		doc->comboBoxMaxReflectionResolution = cbox;
	}

	for( it = 0; it != ARRAY_SIZE( mated2MaterialPropertyInfo ); ++it ) {
		const Mated2PropertyInfo* propertyInfo = &mated2MaterialPropertyInfo[ it ];
		UICheckButton* checkButton = ui_CheckButtonCreate( 10, yIt, propertyInfo->name, false );
		ui_WidgetSetTooltipString( UI_WIDGET( checkButton ), propertyInfo->tooltip );
		ui_CheckButtonSetToggledCallback( checkButton, mated2MaterialPropertyChanged, doc );
		checkButton->statePtr = &doc->materialPropertyFlags[ it ];
		emPanelAddChildYIt( accum, checkButton, false, &yIt );
		yIt -= 5;
	}
	emPanelUpdateHeight( accum );	
	emPanelSetOpened( accum, true );

	doc->panelMaterialProperties = accum;
}

static void mated2ListModelHasFeatureText(
		UIList* list, UIListColumn* ignored, S32 row, UserData rawFeature,
		char **output )
{
	ShaderGraphFeatures feature = (ShaderGraphFeatures)rawFeature;
	UIModel model;
	MaterialFallback* fallback;
	ShaderTemplate* fallbackTemplate;
	
	model = ui_ListGetModel( list );
	if( !model || !*model ) {
		return;
	}
	
	fallback = (*model)[ row ];
	if( !fallback ) {
		return;
	}

	fallbackTemplate = materialGetTemplateByName( fallback->shader_template_name );
	if( !fallbackTemplate ) {
		return;
	}

	if( fallbackTemplate->graph->graph_features & feature ) {
		estrConcatChar( output, '*' );
	}
}

/// Called to update the fallback list (which is the UIList calledback
/// on)'s tooltip based on what column is being hovered over.
static void mated2UpdateFallbackListTooltip(
		UIList* fallbackList, S32 column, S32 row, F32 mouseX, F32 mouseY,
		CBox* cellBox, UserData ignored )
{
	if( column == 0 ) {
		// first column is the fallback name -- no tooltip
		ui_WidgetSetTooltipString( UI_WIDGET( fallbackList ), NULL );
	} else {
		// otherwise, the columns are in bit order
		ShaderGraphFeatures feature = (1 << (column - 1));
		const char* featureName = StaticDefineIntRevLookup( ShaderGraphFeaturesEnum, feature );
		assert( featureName );

		ui_WidgetSetTooltipString( UI_WIDGET( fallbackList ), featureName );
	}
}

static void mated2UpdateFallbacksAllWhoUseMe( UIButton* button, Mated2EditorDoc* doc )
{
	char** allWhoUseMe = mated2AllWhoUseMe( doc, false );
	int numFilesAccum = 0;
	char** filesIgnoredAccum = NULL;

	mated2EnsureCheckoutFiles( allWhoUseMe );
	
	{
		int it;
		for( it = 0; it != eaSize( &allWhoUseMe ); ++it ) {
			if( mated2UpdateFallbacksDependantMaterial( doc, allWhoUseMe[ it ])) {
				++numFilesAccum;
			} else {
				eaPush( &filesIgnoredAccum, allWhoUseMe[ it ]);
			}
		}
	}

	if( numFilesAccum || eaSize( &filesIgnoredAccum )) {
		if( eaSize( &filesIgnoredAccum )) {
			char* alertMsg = NULL;

			estrStackCreate( &alertMsg );
			estrConcatf( &alertMsg,
						 "%d files modified.  You should check these changes and then "
						 "check in all Material files.\n"
						 "\n"
						 "%d files ignored, because they already have overriding "
						 "fallbacks:\n",
						 numFilesAccum, eaSize( &filesIgnoredAccum ));
			{
				int it;
				for( it = 0; it != eaSize( &filesIgnoredAccum ); ++it ) {
					estrConcatf( &alertMsg, "%s\n", filesIgnoredAccum[ it ]);
					
				}
			}
			Alertf( "%s", alertMsg );
			estrDestroy( &alertMsg );
		} else {
			Alertf( "%d files modified.  You should check these changes and then "
					"check in all Material files.",
					numFilesAccum );
		}
	}

	eaDestroy( &filesIgnoredAccum );
	eaDestroy( &allWhoUseMe );
	
	doc->validationNeedsUpdate = true;
	doc->validationUpdateDelayFrames = 2;
}

static void mated2ApplyRenamesAllWhoUseMe( UIButton* button, Mated2EditorDoc* doc )
{
	char** allWhoUseMe = mated2AllWhoUseMe( doc, false );
	int numFilesAccum = 0;
	char** filesIgnoredAccum = NULL;

	mated2EnsureCheckoutFiles( allWhoUseMe );
	
	{
		int it;
		for( it = 0; it != eaSize( &allWhoUseMe ); ++it ) {
			if( mated2ApplyRenamesDependantMaterial( doc, allWhoUseMe[ it ])) {
				++numFilesAccum;
			} else {
				eaPush( &filesIgnoredAccum, allWhoUseMe[ it ]);
			}
		}
	}

	if( numFilesAccum || eaSize( &filesIgnoredAccum )) {
		if( eaSize( &filesIgnoredAccum )) {
			char* alertMsg = NULL;

			estrStackCreate( &alertMsg );
			estrConcatf( &alertMsg,
						 "%d files modified.  You should check these changes and then "
						 "check in all Material files.\n"
						 "\n"
						 "%d files ignored, because they already have overriding "
						 "fallbacks:\n",
						 numFilesAccum, eaSize( &filesIgnoredAccum ));
			{
				int it;
				for( it = 0; it != eaSize( &filesIgnoredAccum ); ++it ) {
					estrConcatf( &alertMsg, "%s\n", filesIgnoredAccum[ it ]);
					
				}
			}
			Alertf( "%s", alertMsg );
			estrDestroy( &alertMsg );
		} else {
			Alertf( "%d files modified.  You should check these changes and then "
					"check in all Material files.",
					numFilesAccum );
		}
	}

	eaDestroy( &filesIgnoredAccum );
	eaDestroy( &allWhoUseMe );
	stashTableClear( doc->renamesTable );
	
	doc->validationNeedsUpdate = true;
	doc->validationUpdateDelayFrames = 2;
}

/// Create the material fallbacks panel.
static void mated2PanelCreateMaterialFallbacks( Mated2EditorDoc* doc )
{
	EMPanel* accum = emPanelCreate( "Properties", "Material Fallbacks", 0 );
	int yIt = 0;

	{
		UIList* fallbackList = ui_ListCreate( NULL, NULL, 14 );
		ui_ListAppendColumn( fallbackList, ui_ListColumnCreateParseName( "Fallback", "Template", NULL ));
		{
			int it;
			for( it = 0; it != 32; ++it ) {
				ShaderGraphFeatures feature = (1 << it);
				const char* featureName = StaticDefineIntRevLookup( ShaderGraphFeaturesEnum, feature );
				if( featureName ) {
					UIListColumn* column = ui_ListColumnCreateText( featureName, mated2ListModelHasFeatureText, (UserData)feature );
					ui_ListColumnSetWidth( column, false, 12 );
					ui_ListAppendColumn( fallbackList, column );
				}
			}
		}
		ui_ListSetCellHoverCallback( fallbackList, mated2UpdateFallbackListTooltip, doc );
		ui_WidgetSetDimensionsEx( UI_WIDGET( fallbackList ), 1, 100, UIUnitPercentage, UIUnitFixed );
		fallbackList->widget.y = yIt;
		emPanelAddChildYIt( accum, fallbackList, false, &yIt );

		doc->listMaterialFallbacks = fallbackList;
	}

	{
		int xIt = 0;
		int yMax = 0;

		{
			UIButton* newButton = ui_ButtonCreateImageOnly( "eui_button_plus", xIt, yIt, mated2DocFallbackAddBefore, doc );
			ui_WidgetSetDimensions( UI_WIDGET( newButton ), 32, 32 );
			ui_WidgetSetTooltipString( UI_WIDGET( newButton ), "Add Fallback" );
			ui_ButtonSetImageStretch( newButton, true );

			emPanelAddChild( accum, newButton, false );
			xIt += ui_WidgetGetWidth( UI_WIDGET( newButton )) + 4;
			yMax = MAX( yMax, yIt + ui_WidgetGetHeight( UI_WIDGET( newButton )) + 2 );
		}
		{
			UIButton* deleteButton = ui_ButtonCreateImageOnly( "eui_button_close", xIt, yIt, mated2DocFallbackRemove, doc );
			ui_WidgetSetDimensions( UI_WIDGET( deleteButton ), 32, 32 );
			ui_WidgetSetTooltipString( UI_WIDGET( deleteButton ), "Remove Fallback" );
			ui_ButtonSetImageStretch( deleteButton, true );

			emPanelAddChild( accum, deleteButton, false );
			xIt += ui_WidgetGetWidth( UI_WIDGET( deleteButton )) + 4;
			yMax = MAX( yMax, yIt + ui_WidgetGetHeight( UI_WIDGET( deleteButton )) + 2 );
		}
		{
			UIButton* upButton = ui_ButtonCreateImageOnly( "eui_arrow_large_up", xIt, yIt, mated2DocFallbackRaisePriority, doc );
			ui_WidgetSetDimensions( UI_WIDGET( upButton ), 32, 32 );
			ui_WidgetSetTooltipString( UI_WIDGET( upButton ), "Raise Priority" );
			ui_ButtonSetImageStretch( upButton, true );

			emPanelAddChild( accum, upButton, false );
			xIt += ui_WidgetGetWidth( UI_WIDGET( upButton )) + 4;
			yMax = MAX( yMax, yIt + ui_WidgetGetHeight( UI_WIDGET( upButton )) + 2 );
		}
		{
			UIButton* downButton = ui_ButtonCreateImageOnly( "eui_arrow_large_down", xIt, yIt, mated2DocFallbackLowerPriority, doc );
			ui_WidgetSetDimensions( UI_WIDGET( downButton ), 32, 32 );
			ui_WidgetSetTooltipString( UI_WIDGET( downButton ), "Lower Priority" );
			ui_ButtonSetImageStretch( downButton, true );

			emPanelAddChild( accum, downButton, false );
			xIt += ui_WidgetGetWidth( UI_WIDGET( downButton )) + 4;
			yMax = MAX( yMax, yIt + ui_WidgetGetHeight( UI_WIDGET( downButton )) + 2 );
		}

		xIt = 0;

		{
			UIButton* clearButton = ui_ButtonCreate( "Clear Selection", xIt, yIt, mated2DocFallbackClearSelection, doc );
			ui_WidgetSetHeight( UI_WIDGET( clearButton ), 32 );
			ui_WidgetSetPositionEx( UI_WIDGET( clearButton ), xIt, yIt, 0, 0, UITopRight );
			
			emPanelAddChild( accum, clearButton, false );
			xIt += ui_WidgetGetWidth( UI_WIDGET( clearButton )) + 4;
			yMax = MAX( yMax, yIt + ui_WidgetGetHeight( UI_WIDGET( clearButton )) + 2 );
		}

		yIt = yMax;		
	}

	{
		UIButton* button = ui_ButtonCreate( "Set Active Fallback", 0, yIt, mated2DocFallbackSelectedSetActive, doc );
		ui_WidgetSetWidthEx( UI_WIDGET( button ), 1, UIUnitPercentage );

		emPanelAddChildYIt( accum, button, false, &yIt );
	}
	
	emPanelUpdateHeight( accum );
	emPanelSetOpened( accum, true );

	doc->panelMaterialFallbacks = accum;
}

static void mated2UpdateAllWhoUseMe( UIButton* button, Mated2EditorDoc* doc )
{
	char** allWhoUseMe = mated2AllWhoUseMe( doc, true );
	int numFilesAccum = 0;

	mated2EnsureCheckoutFiles( allWhoUseMe );

	{
		int it;
		for( it = 0; it != eaSize( &allWhoUseMe ); ++it ) {
			mated2UpdateDependantMaterial( doc, allWhoUseMe[ it ]);
			++numFilesAccum;
		}
	}
	
	if( numFilesAccum ) {
		Alertf( "%d files modified.  You should check these changes and then "
				"check in all Material files.",
				numFilesAccum );
	} else {
		Alertf( "No files were found which needed modification." );
	}

	eaDestroy( &allWhoUseMe );

	doc->validationNeedsUpdate = true;
	doc->validationUpdateDelayFrames = 2;
}

typedef struct Mated2LinkInfo {
	Mated2EditorDoc* doc;
	char* buffer;
	int numLinks;
} Mated2LinkInfo;

static void mated2PanelCreateLinksSub( Mated2LinkInfo* linkInfo, const char* name )
{
	Mated2EditorDoc* doc = linkInfo->doc;

	const MaterialData* materialData = materialFindData( name );
	assert( materialData );

	if(   strEndsWith( materialData->material_name, "_default" )
		  || !materialDataHasShaderTemplate( materialData, doc->materialLoadInfo->templates[ 0 ]->template_name )) {
		return;
	}

	estrConcatf( &linkInfo->buffer, "%s (%s)\n",
				 materialData->material_name,
				 materialData->filename + strlen( "materials/" ));
	++linkInfo->numLinks;
}

/// Create the links panel
static void mated2PanelCreateLinks( Mated2EditorDoc* doc )
{
	EMPanel* accum = emPanelCreate( "Properties", "Links", 0 );
	int yIt = 0;

	{
		UIButton* button = ui_ButtonCreate( "Update All Linked", 10, yIt, mated2UpdateAllWhoUseMe, doc );
		ui_WidgetSetWidthEx( UI_WIDGET( button ), 0.49, UIUnitPercentage );
		emPanelAddChild( accum, button, false );
	}

	{
		UIButton* button = ui_ButtonCreate( "Update All Fallbacks", 10, yIt, mated2UpdateFallbacksAllWhoUseMe, doc );
		ui_WidgetSetWidthEx( UI_WIDGET( button ), 0.49, UIUnitPercentage );
		ui_WidgetSetPositionEx( UI_WIDGET( button ), 0, 0, 0, 0, UITopRight );
		emPanelAddChildYIt( accum, button, false, &yIt );
	}

	{
		UIButton* button = ui_ButtonCreate( "Apply Renames", 10, yIt, mated2ApplyRenamesAllWhoUseMe, doc );
		ui_WidgetSetWidthEx( UI_WIDGET( button ), 0.49, UIUnitPercentage );
		emPanelAddChildYIt( accum, button, false, &yIt );
	}

	{
		UIButton* button = ui_ButtonCreate( "Copy Links", 10, yIt, mated2DocCopyLinks, doc );
		ui_WidgetSetWidthEx( UI_WIDGET( button ), 1, UIUnitPercentage );
		emPanelAddChildYIt( accum, button, false, &yIt );
	}
	
	doc->textAreaLinks = ui_TextAreaCreate( "" );
	ui_WidgetSetDimensionsEx( UI_WIDGET( doc->textAreaLinks ), 1, 400, UIUnitPercentage, UIUnitFixed );
	ui_WidgetSetPosition( UI_WIDGET( doc->textAreaLinks ), 10, yIt );
	ui_SetActive( UI_WIDGET( doc->textAreaLinks ), false );
	emPanelAddChildYIt( accum, doc->textAreaLinks, true, &yIt );

	emPanelSetOpened( accum, true );

	doc->panelLinks = accum;
}

/// Create a list of all the possible nodes that can be placed in a
/// template.
static void mated2PanelCreateTemplateNodeList( Mated2EditorDoc* doc )
{
	EMPanel* accum = emPanelCreate( "Material", "Nodes", 0 );
	int yIt = 0;

	{
		UILabel* label = ui_LabelCreate( "Instanced Node:", 0, 0 );
		UIComboBox* cbox = ui_FilteredComboBoxCreate( 0, 0, 100, parse_ShaderOperation, NULL, "Name" );

		int xIt = 0;

		ui_WidgetSetPosition( UI_WIDGET( label ), xIt, yIt );
		emPanelAddChild( accum, label, false );
		xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 4;

		ui_WidgetSetPosition( UI_WIDGET( cbox ), xIt, yIt );
		ui_WidgetSetWidthEx( UI_WIDGET( cbox ), 1, UIUnitPercentage );
		emPanelAddChildYIt( accum, cbox, false, &yIt );

		doc->comboBoxInstancedNode = cbox;
	}

	{
		UITree* tree = ui_TreeCreate( 0, yIt, 0, 400 );
		ui_TreeSetActivatedCallback( tree, mated2AddNodeBySelection, doc );
		ui_WidgetSetWidthEx( UI_WIDGET( tree ), 1, UIUnitPercentage );
		ui_TreeNodeSetFillCallback( &tree->root, mated2NodeListFillRoot, NULL );
		ui_TreeNodeExpand( &tree->root );

		emPanelAddChildYIt( accum, tree, false, &yIt );
		doc->treeNodes = tree;
	}

	{
		UIButton* button = ui_ButtonCreate( "Add Node", 0, yIt, mated2AddNodeBySelection, doc );
		ui_WidgetSetWidthEx( UI_WIDGET( button ), 1, UIUnitPercentage );
		emPanelAddChildYIt( accum, button, false, &yIt );
	}

	yIt += 10;
	{
		UIButton* button = ui_ButtonCreate( "Add Guide", 0, yIt, mated2AddGuide, doc );
		ui_WidgetSetWidthEx( UI_WIDGET( button ), 1, UIUnitPercentage );
		emPanelAddChildYIt( accum, button, true, &yIt );
	}

	doc->panelNodeList = accum;
}

/// Fill in the root of the node list with the root categories.
static void mated2NodeListFillRoot( UITreeNode* root, UserData ignored )
{
	int it;
	for( it = 0; it != SOT_END; ++it ) {
		if( it != SOT_SINK ) {
			const char* categoryName = StaticDefineIntRevLookup( ShaderOperationTypeEnum, it );
			UITreeNode* categoryNode = ui_TreeNodeCreate(
					root->tree, 0, NULL, NULL,
					mated2NodeListFillCategory, S32_TO_PTR( it ),
					ui_TreeDisplayText, (UserData)StaticDefineIntRevLookup( ShaderOperationTypeEnum, it ),
					20 );

			ui_TreeNodeAddChild( root, categoryNode );
		}
	}
}

/// Fill in all nodes of this category
static void mated2NodeListFillCategory( UITreeNode* node, UserData category )
{
	ShaderOperationType opType = PTR_TO_S32( category );
	const ShaderOperationDef** opDefs = (const ShaderOperationDef**)(resDictGetEArrayStruct( g_hOpDefsDict )->ppReferents );
	
	int it;
	for( it = 0; it != eaSize( &opDefs ); ++it ) {
		const ShaderOperationDef* opDef = opDefs[ it ];

		if( opDef->op_parent_type_name && stricmp( opDef->op_parent_type_name, opDef->op_type_name ) != 0 ) {
			continue;
		} else if( opDef->op_type == opType ) {
			ui_TreeNodeAddChild( node, ui_TreeNodeCreate(
										 node->tree, 0, NULL, (UserData)opDef->op_type_name, NULL, NULL,
										 ui_TreeDisplayText, (UserData)strdup( mated2TranslateOpName( opDef->op_type_name )),
										 20 ));
		}
	}
}

/// Create a list of all inputs in DOC that can be edited
/// per-material.
static void mated2PanelCreateMaterialInputList( Mated2EditorDoc* doc )
{
	EMPanel* accum = emPanelCreate( "Material", "Material Nodes", 0 );
		
	doc->panelInputList = accum;
}

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

/// Clear out any prior nones and repopulate the material node view
/// with the shader template specified by GRAPH and GUIDES.  Uses
/// GFX-PROPS and WORLD-PROPS for material-specific data.
static void mated2RepopulateNodeView(
		Mated2EditorDoc* doc,
		const MaterialGraphicPropertiesLoadTime* gfxProps,
		const MaterialWorldPropertiesLoadTime* worldProps )
{
	const ShaderGraph* graph = mated2DocActiveShaderGraph( doc );
	ShaderGuide*** guides = mated2DocActiveShaderGuide( doc );
	MaterialFallback* fallback = mated2DocActiveFallback( doc );
	bool isEditable = mated2DocActiveShaderGraphIsEditable( doc );

	int it;
	int inputIt;
	const char *pPhysPropsName;

	// Clear out any data that would still be lying around
	ui_FlowchartClear( doc->flowchart );
	if( mated2DocIsTemplate( doc )) {
		ShaderGraph* templateGraph = doc->materialLoadInfo->templates[ 0 ]->graph;
		int opIt;
		for( opIt = 0; opIt != eaSize( &templateGraph->operations ); ++opIt ) {
			templateGraph->operations[ opIt ]->op_editor_data = NULL;
		}
	}

	assert( eaSize( &doc->flowchart->nodeWindows ) == 0 );

	for( it = 0; it != eaSize( &graph->operations ); ++it ) {
		ShaderOperation* op = graph->operations[ it ];
		Mated2Node* node = mated2NewNodeByOp( doc, op );
		UIFlowchartNode* nodeUI = mated2NodeUI( node );
		
		ui_SetActive( UI_WIDGET( nodeUI ), isEditable );
		ui_WindowSetClosable( UI_WINDOW( nodeUI ), isEditable );
		ui_WindowSetMovable( UI_WINDOW( nodeUI ), true );
		ui_WindowSetShadable( UI_WINDOW( nodeUI ), true );

		if( GET_REF( op->h_op_definition )->op_type == SOT_SINK ) {
			mated2PreviewSetSelectedNode( doc, node );
		}
	}

	for( it = 0; it != eaSize( &graph->operations ); ++it ) {
		ShaderOperation* op = graph->operations[ it ];
		Mated2Node* node = mated2ShaderOpNode( op );

		for( inputIt = 0; inputIt != eaSize( &op->inputs ); ++inputIt ) {
			ShaderInputEdge* input = op->inputs[ inputIt ];
			Mated2Node* inputNode = mated2FindNodeByNameInGraph( doc, input->input_source_name, graph );

			mated2NodeLink( inputNode, input->input_source_output_name,
							node, input->input_name,
							input->input_swizzle,
							true );
		}

		for( inputIt = 0; inputIt != eaSize( &op->fixed_inputs ); ++inputIt ) {
			ShaderFixedInput* fixedInput = op->fixed_inputs[ inputIt ];
			Mated2Input* input = mated2FindNodeInputByName( node, fixedInput->input_name );

			if( input ) {
				mated2InputSetValues( input, fixedInput->fvalues, NULL, true );
			}
		}
	}

	for( it = 0; it != eaSize( guides ); ++it ) {
		mated2NewGuide( doc, (*guides)[ it ]);
	}

	for( it = 0; it != ARRAY_SIZE( mated2TemplatePropertyInfo ); ++it ) {
		const Mated2PropertyInfo* property = &mated2TemplatePropertyInfo[ it ];

		doc->templatePropertyFlags[ it ] = ((graph->graph_flags & property->flag) != 0);
	}

	for( it = 0; it != 32; ++it ) {
		if( doc->templateFeatureCheckButtons[ it ]) {
			ui_CheckButtonSetState( doc->templateFeatureCheckButtons[ it ],
									(graph->graph_features & (1 << it)) != 0 );
		}
		if(   doc->templateFeatureResetButtons[ it ]
			  && (graph->graph_features_overriden & (1 << it)) != 0 ) {
			emPanelAddChild( doc->panelTemplateFeatures, doc->templateFeatureResetButtons[ it ], false );
		}
	}

	for( it = 0; it != eaSize( &fallback->shader_values ); ++it ) {
		ShaderOperationValues* input = fallback->shader_values[ it ];
		Mated2Node* node = mated2FindNodeByNameInGraph( doc, input->op_name, graph );
		if( !node ) {
			continue;
		}
		
		for( inputIt = 0; inputIt != eaSize( &input->values ); ++inputIt ) {
			const ShaderOperationSpecificValue* value = input->values[ inputIt ];

			if( value->fvalues != NULL || value->svalues != NULL ) {
				Mated2Input* matedInput = mated2FindNodeInputByName( node, value->input_name );

				if( matedInput ) {
					mated2InputSetValues( matedInput, value->fvalues, value->svalues, false );
				}
			}
		}
	}

	for( it = 0; it != ARRAY_SIZE( doc->materialPropertyFlags ); ++it ) {
		doc->materialPropertyFlags[ it ] = ((gfxProps->flags & mated2MaterialPropertyInfo[it].flag) != 0);
	}

	pPhysPropsName = REF_STRING_FROM_HANDLE( worldProps->physical_properties );
	assert(pPhysPropsName);
	mated2SetPhysicalProperties( doc, pPhysPropsName);
}

/// Clear out the input list and repopulate the input list panel with
/// a list of all inputs that can be edited per-material.
static void mated2RepopulateInputList( Mated2EditorDoc* doc )
{
	EMPanel* accum = doc->panelInputList;
	ShaderGraph* graph = doc->shaderGraph;

	// clear out the existing input list
	emPanelGetExpander( doc->panelInputList )->widget.children = NULL;
	ui_WidgetGroupQueueFree( &doc->inputListNodeNames );
	eaDestroy( &doc->inputListNodeNames );
	ui_WidgetForceQueueFree( UI_WIDGET( doc->templateButton ));

	// make a new input list
	{
		int yIt = 0;

		{
			char buffer[ 128 ];
			UIButton* button;

			sprintf( buffer, "Template: %s", mated2DocActiveFallback( doc )->shader_template_name );
			button = ui_ButtonCreate( buffer, 10, yIt, mated2SelectTemplate, doc );
			ui_WidgetSetWidthEx( UI_WIDGET( button ), 1, UIUnitPercentage );
			emPanelAddChildYIt( accum, button, false, &yIt );

			doc->templateButton = button;
		}

		{
			Mated2Node** nodes = mated2DocNodesSortedByGroup( doc );
			int it;
			int valueIt;
			const char* prevGroupName = ""; //< For backward compatibilty, don't
											//< show group headers if every
											//< group is the empty group.

			for( it = 0; it != eaSize( &nodes ); ++it ) {
				Mated2Node* node = nodes[ it ];
				Mated2Input** nodeInputs = mated2NodeInputs( node );
				UIFlowchartNode* uiNode = mated2NodeUI( node );
				const char* inheritName;

				if( mated2NodeShaderInputMapping( node )) {
					inheritName = mated2NodeShaderInputMapping( node )->op_name;
				} else {
					inheritName = NULL;
				}

				if( strcmp( prevGroupName, mated2NodeGroup( node )) != 0 ) {
					if( prevGroupName[ 0 ]) {
						UISeparator* seperator = ui_SeparatorCreate(UIHorizontal);
						ui_WidgetSetPosition(UI_WIDGET(seperator), 0, yIt);
						emPanelAddChildYIt( accum, seperator, false, &yIt );
						eaPush( &doc->inputListNodeNames, UI_WIDGET( seperator ));
					}
					{
						char buffer[ 128 ];
						UISMFView* group = ui_SMFViewCreate( 0, yIt, 0, 0 );
						const char* nodeGroup = mated2NodeGroup( node );
						sprintf( buffer, "<font shadow=1 scale=1.2><b>%s</b></font>", nodeGroup[ 0 ] ? nodeGroup : "Other" );

						ui_WidgetSetWidthEx( UI_WIDGET( group ), 1, UIUnitPercentage );
						ui_SMFViewSetText( group, buffer, NULL );
						ui_SMFViewUpdateDimensions( group );
						emPanelAddChildYIt( accum, group, false, &yIt );
						eaPush( &doc->inputListNodeNames, UI_WIDGET( group ));
					}
					
					
					prevGroupName = mated2NodeGroup( node );
				}
			
				{
					char buffer[ 128 ];
					char titleBuffer[ 128 ];
					UISMFView* title = ui_SMFViewCreate( 0, yIt, 0, 0 );
					sprintf( titleBuffer, "<b>%s</b>%s",
							 mated2NodeNameWithType( node, SAFESTR( buffer )),
							 mated2NodeShaderOp( node )->instance_param ? "<br><font scale=0.8>(Instanced)</font>" : "" );
				
					ui_WidgetSetWidthEx( UI_WIDGET( title ), 1, UIUnitPercentage );
					ui_SMFViewSetText( title, titleBuffer, NULL );
					ui_SMFViewUpdateDimensions( title );
					emPanelAddChildYIt( accum, title, false, &yIt );
					eaPush( &doc->inputListNodeNames, UI_WIDGET( title ));
				}

				{
					char notesBuffer[ 128 ];
					UISMFView* notes = ui_SMFViewCreate( 0, yIt, 0, 0 );
					sprintf( notesBuffer, "<font color=#373737>%s</font>", mated2NodeNotes( node ) );

					ui_WidgetSetWidthEx( UI_WIDGET( notes ), 1, UIUnitPercentage );
					ui_SMFViewSetText( notes, notesBuffer, NULL );
					ui_SMFViewUpdateDimensions( notes );
					emPanelAddChildYIt( accum, notes, false, &yIt );
				}

				if( mated2DocActiveFallback( doc ) != &mated2DocGfxProperties( doc )->default_fallback ) {
					UICheckButton* checkButton = mated2NodeInputListInheritValuesCheckButton( node );
					
					ui_WidgetSetPosition( UI_WIDGET( checkButton ), 0, yIt );
					emPanelAddChildYIt( accum, checkButton, false, &yIt );
				}

				if( !mated2NodeInheritValues( node )) { 
					for( valueIt = 0; valueIt != eaSize( &nodeInputs ); ++valueIt ) {
						Mated2Input* value = nodeInputs[ valueIt ];
 				
						if(   value != NULL
							  && (valueIt >= eaSize( &uiNode->inputButtons ) || eaSize( &uiNode->inputButtons[ valueIt ]->connected ) == 0)
							  && !mated2InputIsLocked( value )) {
							UIWidget* widget = mated2InputInputListWidget( value );
					
							ui_WidgetSetPosition( widget, widget->x, yIt );
							emPanelAddChildYIt( accum, widget, false, &yIt );
						}
					}
				} else {
					UITextEntry* textEntry = mated2NodeInputListInheritValuesNameWidget( node );
					
					ui_WidgetSetPosition( UI_WIDGET( textEntry ), 0, yIt );
					emPanelAddChildYIt( accum, textEntry, 0, &yIt );
				}
			}
		}

		emPanelUpdateHeight( accum );
	}
}

/// Clear out any existing text and repopulate the links panel with a
/// list of every instantiation of the template.
static void mated2RepopulateLinks( Mated2EditorDoc* doc )
{
	const char* templateName = doc->materialLoadInfo->templates[ 0 ]->template_name;
	Mated2LinkInfo linkInfo = { doc, NULL, 0 };
	estrStackCreate( &linkInfo.buffer );

	estrConcatf( &linkInfo.buffer, "Materials which reference %s:\n", templateName );
	materialForEachMaterialName( mated2PanelCreateLinksSub, &linkInfo );
	estrConcatf( &linkInfo.buffer, "Total material references:  %d\n", linkInfo.numLinks );
		
	estrConcatf( &linkInfo.buffer, "----------------------------------------\n" );

	estrConcatf( &linkInfo.buffer, "Maps which reference materials using %s:\n", templateName );
	materialGetMapsWhoUseMe( doc->materialLoadInfo->templates[ 0 ], &linkInfo.buffer );
		
	ui_TextAreaSetText( doc->textAreaLinks, linkInfo.buffer );
	estrDestroy( &linkInfo.buffer );
}

/// Clear out all the panels and repopulate them with something
/// apropriate.
static void mated2RepopulatePanels( Mated2EditorDoc* doc )
{
	eaClear( &doc->emEd.em_panels );

	eaPush( &doc->emEd.em_panels, doc->panelMaterialFallbacks );
	eaPush( &doc->emEd.em_panels, doc->panelTemplateFeatures );
	eaPush( &doc->emEd.em_panels, doc->panelTemplateProperties );
	eaPush( &doc->emEd.em_panels, doc->panelMaterialProperties );

	if( mated2DocIsTemplate( doc )) {
		eaPush( &doc->emEd.em_panels, doc->panelLinks );
	}

	ui_ComboBoxSetSelectedEnum(doc->comboBoxReflectionType, doc->shaderGraph->graph_reflection_type);
	ui_ComboBoxSetSelectedEnum(doc->comboBoxShaderQuality, doc->shaderGraph->graph_quality);
	ui_ComboBoxSetSelectedEnum(doc->comboBoxMaxReflectionResolution, doc->material.material_data->graphic_props.max_reflect_resolution);

	if( mated2DocActiveShaderGraphIsEditable( doc )) {
		eaPush( &doc->emEd.em_panels, doc->panelNodeList );

		ui_ComboBoxSetModel( doc->comboBoxInstancedNode, parse_ShaderOperation, &doc->shaderGraph->operations );
		ui_ComboBoxSetSelectedCallback( doc->comboBoxInstancedNode, mated2DocInstancedNodeChanged, doc );
		{
			int it;
			for( it = 0; it != eaSize( &doc->shaderGraph->operations ); ++it ) {
				ShaderOperation* op = doc->shaderGraph->operations[ it ];

				if( op->instance_param ) {
					ui_ComboBoxSetSelectedObject( doc->comboBoxInstancedNode, op );
					break;
				}
			}
		}

		emPanelSetActive( doc->panelTemplateProperties, true );
		emPanelSetActive( doc->panelTemplateFeatures, true );
	} else {
		mated2RepopulateInputList( doc );		
		eaPush( &doc->emEd.em_panels, doc->panelInputList );
	
		emPanelSetActive( doc->panelTemplateProperties, false );
		emPanelSetActive( doc->panelTemplateFeatures, false );
	}
}

/// Called by the flowchart widget every time a node is requested to
/// be removed, before its links are broken.
static bool mated2FlowchartNodeRemoveRequest(
		UIFlowchart* flowchart, UIFlowchartNode* uiNode, Mated2EditorDoc* doc )
{
	Mated2Node* node = (Mated2Node*)uiNode->userData;
	ShaderOperation *pShaderOp;
	const ShaderOperationDef *pShaderOpDef;

	pShaderOp = mated2NodeShaderOp( node );
	assert(pShaderOp);
	pShaderOpDef = GET_REF(pShaderOp->h_op_definition);
	assert(pShaderOpDef);

	if( pShaderOpDef->op_type == SOT_SINK ) {
		return false;
	}
	
	// Point of NO RETURN!  The removal has been accepted!
	mated2UndoBeginGroup( doc );		//< EndGroup is called in FlowchartNodeRemoved.
	if( mated2PreviewSelectedNode( doc ) == node ) {
		mated2PreviewSetSelectedNode( doc, NULL );
	}
	return true;
}

/// Called by the flowchart widget every time a node is requested to
/// be removed, after its links are broken.
static bool mated2FlowchartNodeRemoved(
		UIFlowchart* flowchart, UIFlowchartNode* uiNode, Mated2EditorDoc* doc )
{
	Mated2Node* node = (Mated2Node*)uiNode->userData;

	{
		{
			Mated2Input** inputs = mated2NodeInputs( node );
			int it;
			for( it = 0; it != eaSize( &inputs ); ++it ) {
				if(   inputs[ it ]
					  // The operation should have a specific value to set, if
					  // we are going to save that in the undo info.
					  && materialFindOperationSpecificValue2(
							  mated2NodeShaderOpValues( node ),
							  mated2InputName( inputs[ it ]))) {
					mated2InputSetValuesToDefault( inputs[ it ]);
					mated2InputHidden( inputs[ it ]);
				}
			}
		}

		if( mated2NodeShaderOp( node )->instance_param ) {
			mated2DocSetInstancedNode( doc, NULL );
		}
		
		{
			Mated2NodeCreateAction* accum = calloc( 1, sizeof( *accum ));
			accum->nodeName = strdup( mated2NodeName( node ));
			setVec2( accum->nodePos, ui_WidgetGetX( UI_WIDGET( uiNode )), ui_WidgetGetY( UI_WIDGET( uiNode )));
			COPY_HANDLE( accum->opDef, mated2NodeShaderOp( node )->h_op_definition );

			mated2UndoRecord( doc, mated2NodeCreateActionCreate,
							  mated2NodeCreateActionRemove, mated2NodeCreateActionFree,
							  accum );
		}
	}
	mated2UndoEndGroup( doc );

	mated2NodeFree( node );
	
	return true;
}

/// Add a new node corresponding to the currently selected node in the
/// side panel
static void mated2AddNodeBySelection( UIButton* ignored, Mated2EditorDoc* doc )
{
	UITreeNode* selection = ui_TreeGetSelected( doc->treeNodes );

	if( selection ) {
		const char* selectionName = selection->contents;

		if( selectionName ) {
			Mated2Node* node = mated2NewNodeByOpName( doc, selectionName );
			
			if( node ) {				
				UIFlowchartNode* nodeUI = mated2NodeUI( node );
				ui_WidgetSetPosition( UI_WIDGET( nodeUI ),
									  doc->flowchart->widget.sb->xpos + 50,
									  doc->flowchart->widget.sb->ypos + 50 );

				{
					Mated2NodeCreateAction* accum = calloc( 1, sizeof( *accum ));
					accum->nodeName = strdup( mated2NodeName( node ));
					setVec2( accum->nodePos, ui_WidgetGetX( UI_WIDGET( nodeUI )), ui_WidgetGetY( UI_WIDGET( nodeUI )));
					COPY_HANDLE( accum->opDef, mated2NodeShaderOp( node )->h_op_definition );

					mated2UndoRecord( doc, mated2NodeCreateActionRemove,
									  mated2NodeCreateActionCreate, mated2NodeCreateActionFree,
									  accum );
				}
			}
		}
	}
}

/// Add a new Guide to the material template.
///
/// Guide are purely visual tools to group related nodes in the
/// editor.
static void mated2AddGuide( UIButton* ignored, Mated2EditorDoc* doc )
{
	float x = 10;
	float y = 10;
	float w = 100;
	float h = 100;
	
	ShaderGuide* accum = StructCreate( parse_ShaderGuide );
	setVec2( accum->top_left, x, y );
	setVec2( accum->bottom_right, x + w, y + h );
	eaPush( mated2DocActiveShaderGuide( doc ), accum );
	mated2NewGuide( doc, accum );

	if( !doc->isLoading ) {
		doc->emEd.saved = false;
		mated2EnsureCheckout( doc );
	}
}

/// Finish up loading a template, adding to the DOC all nodes that the
/// template would need and setting material flags.
static bool mated2FinishLoadTemplate( Mated2EditorDoc* doc )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	const MaterialWorldPropertiesLoadTime* worldProps = mated2DocWorldProperties( doc );

	doc->shaderTemplate = doc->materialLoadInfo->templates[ 0 ];
	materialShaderTemplateValidate(doc->shaderTemplate, false);

	// patch up some pointers
	doc->shaderTemplate->graph = &doc->shaderTemplate->graph_parser;
	materialUpdateFromData( &doc->material, doc->materialLoadInfo->material_datas[ 0 ]);
	doc->material.graphic_props.material_clean = false;
	doc->material.material_data->graphic_props.active_fallback = &doc->material.material_data->graphic_props.default_fallback;
	doc->material.material_data->graphic_props.shader_template = doc->shaderTemplate;

	doc->shaderGraph = doc->shaderTemplate->graph;
	doc->shaderGuides = &doc->shaderTemplate->guides;
	ui_ListSetModel( doc->listMaterialFallbacks, parse_MaterialFallback, &gfxProps->fallbacks );
	doc->fallback = &mated2DocGfxProperties( doc )->default_fallback;

	// disable caching of the template so preview gets dynamically
	// updated as nodes get added/removed.
	doc->shaderGraph->graph_flags |= SGRAPH_NO_CACHING;
	
	mated2RepopulateNodeView( doc, gfxProps, worldProps );
	mated2RepopulateLinks( doc );
	mated2RepopulatePanels( doc );

	doc->isLoading = false;
	doc->validationNeedsUpdate = true;
	doc->validationUpdateDelayFrames = 2;

	/// ALL LOADED, NORMAL FUNCTIONS CAN BE CALLED

	// set the fallback if necesarry
	{
		int autoSetFallbackIndex = mated2DocFallbackIndexByFeatures( doc, systemSpecsMaterialSupportedFeatures() );
		if( autoSetFallbackIndex >= 0 ) {
			mated2DocSetActiveFallback( doc, gfxProps->fallbacks[ autoSetFallbackIndex ]);
			ui_ListSetSelectedRow( doc->listMaterialFallbacks, autoSetFallbackIndex );
		}
	}

	return true;
}

/// Return the node in DOC named NAME.  If there is no such node.
Mated2Node* mated2FindNodeByName( Mated2EditorDoc* doc, const char* name )
{
	return mated2FindNodeByNameInGraph( doc, name, mated2DocActiveShaderGraph( doc ));
}

/// Return the node in GRAPH named NAME.  If there is no such node,
/// returns NULL.
static Mated2Node* mated2FindNodeByNameInGraph(
		Mated2EditorDoc* doc, const char* name, const ShaderGraph* graph )
{
	int it;

	for( it = 0; it != eaSize( &graph->operations ); ++it ) {
		ShaderOperation* op = graph->operations[ it ];

		if( stricmp( op->op_name, name ) == 0 ) {
			return (Mated2Node*)op->op_editor_data;
		}
	}
	
	return NULL;
}

/// Called any time a template property has changed.
static void mated2TemplatePropertyChanged( UICheckButton* property, Mated2EditorDoc* doc )
{
	if( !doc->isLoading ) {
		const Mated2PropertyInfo* propertyInfo = NULL;
		ShaderGraph* graph = mated2DocActiveShaderGraph( doc );

		{
			int it;
			for( it = 0; it != ARRAY_SIZE( mated2TemplatePropertyInfo ); ++it ) {
				if( strcmp( mated2TemplatePropertyInfo[ it ].name, ui_WidgetGetText( UI_WIDGET( property ))) == 0 ) {
					propertyInfo = &mated2TemplatePropertyInfo[ it ];
					break;
				}
			}
			assert( propertyInfo );
		}

		if( ui_CheckButtonGetState( property )) {
			graph->graph_flags |= propertyInfo->flag;
			graph->graph_flags &= ~(propertyInfo->mutually_exclusive);
		} else {
			graph->graph_flags &= ~propertyInfo->flag;
		}

		{
			int it;
			for( it = 0; it != ARRAY_SIZE( doc->templatePropertyFlags); ++it ) {
				doc->templatePropertyFlags[ it ] = ((graph->graph_flags & mated2TemplatePropertyInfo[it].flag) != 0);
			}
		}
		
		mated2SetDirty( doc );
	}
}

/// Called any time a template feature has changed.
static void mated2TemplateFeatureChanged( UICheckButton* feature, Mated2EditorDoc* doc )
{
	if( !doc->isLoading ) {
		ShaderGraph* graph = mated2DocActiveShaderGraph( doc );
		int featureValue = 0;
		
		int it;
		for( it = 0; it != ARRAY_SIZE( doc->templateFeatureCheckButtons ); ++it ) {
			if( feature == doc->templateFeatureCheckButtons[ it ]) {
				featureValue = (1 << it);
				break;
			}
		}
		assert( featureValue != 0 );
		
		if( ui_CheckButtonGetState( feature )) {
			graph->graph_features |= featureValue;
		} else {
			graph->graph_features &= ~featureValue;
		}
		graph->graph_features_overriden |= featureValue;
		emPanelAddChild( doc->panelTemplateFeatures, doc->templateFeatureResetButtons[ it ], false );

		mated2SetDirty( doc );
	}
}

/// Called when the user wants a feature to get reset
static void mated2TemplateFeatureReset( UIButton* reset, Mated2EditorDoc* doc )
{
	if( !doc->isLoading ) {
		ShaderGraph* graph = mated2DocActiveShaderGraph( doc );
		int featureValue = 0;
		ShaderGraphFeatures defaultValues = mated2TemplateDefaultGraphFeature( doc, true );
		
		int it;
		for( it = 0; it != ARRAY_SIZE( doc->templateFeatureResetButtons ); ++it ) {
			if( reset == doc->templateFeatureResetButtons[ it ]) {
				featureValue = (1 << it);
				break;
			}
		}

		ui_CheckButtonSetState( doc->templateFeatureCheckButtons[ it ], (defaultValues & featureValue) != 0 );
		if( defaultValues & featureValue ) {
			graph->graph_features |= featureValue;
		} else {
			graph->graph_features &= ~featureValue;
		}
		graph->graph_features_overriden &= ~featureValue;
		emPanelRemoveChild( doc->panelTemplateFeatures, doc->templateFeatureResetButtons[ it ], false );

		mated2SetDirty( doc );
	}
}

/// Called any time a material property has changed.
static void mated2MaterialPropertyChanged(
		UICheckButton* property, Mated2EditorDoc* doc )
{
	if( !doc->isLoading ) {
		const Mated2PropertyInfo* propertyInfo = NULL;
		MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );

		{
			int it;
			for( it = 0; it != ARRAY_SIZE( mated2MaterialPropertyInfo ); ++it ) {
				if( strcmp( mated2MaterialPropertyInfo[ it ].name, ui_WidgetGetText( UI_WIDGET( property ))) == 0 ) {
					propertyInfo = &mated2MaterialPropertyInfo[ it ];
					break;
				}
			}
			assert( propertyInfo );
		}

		if( ui_CheckButtonGetState( property )) {
			gfxProps->flags |= propertyInfo->flag;
			gfxProps->flags &= ~(propertyInfo->mutually_exclusive);
		} else {
			gfxProps->flags &= ~propertyInfo->flag;
		}
		
		{
			int it;
			for( it = 0; it != ARRAY_SIZE( doc->materialPropertyFlags ); ++it ) {
				doc->materialPropertyFlags[ it ] = ((gfxProps->flags & mated2MaterialPropertyInfo[it].flag) != 0);
			}
		}
		doc->emEd.saved = false;
		mated2EnsureCheckout( doc );
	}
}

static float mated2GetMinShaderOpValue( const ShaderGraph *graph )
{
	float minShaderOpValue = 0.0;
	int i;
	for( i = 0; i != eaSize( &graph->operations ); ++i ) {
		ShaderOperation* op = graph->operations[ i ];
		const ShaderOperationDef* opDef = GET_REF( op->h_op_definition );
		if (opDef->min_shader_level > minShaderOpValue)
			minShaderOpValue = opDef->min_shader_level;
	}
	return minShaderOpValue;
}

static bool disableSuperLowEndTemplates;
AUTO_CMD_INT( disableSuperLowEndTemplates, disableSuperLowEndTemplates );

/// Calculate the default features based on performance values gotten.
static ShaderGraphFeatures mated2MeasureGraphFeatures(
		const char* templateName, const ShaderGraph* graph,
		const RdrShaderPerformanceValues* perfValues2Lights,
		const RdrShaderPerformanceValues* perfValuesMaxLights,
		const RdrShaderPerformanceValues* perfValuesSM20 )
{
	static const int maxSm2InstructionCount = 64;
	static const int maxSm2xInstructionCount = 512;
	static const int maxSm3InstructionCount = 512;
	static const int maxSm2ConstantCount = 32;
	static const int maxSm3ConstantCount = 224;

	float minShaderOpValue = mated2GetMinShaderOpValue(graph);
	ShaderGraphFeatures accum = 0;
	//int score = gfxMaterialScoreFromValues( defaultPerfValues );
	int instructionCount2Lights = (perfValues2Lights->instruction_count<0)?9999:perfValues2Lights->d3d_instruction_slots;
	int instructionCountMaxLights = (perfValuesMaxLights->instruction_count<0)?9999:perfValuesMaxLights->d3d_instruction_slots;
	int instructionCountSM20 = (perfValuesSM20->instruction_count<0)?9999:perfValuesSM20->d3d_alu_instruction_slots;
	int constantCountMaxLights = (PS_CONSTANT_MATERIAL_PARAM_OFFSET
						 + graph->graph_render_info->num_input_vectors
						 + MAX_NUM_SHADER_LIGHTS * SHADER_PER_LIGHT_CONST_COUNT);
	int constantCount2Lights = (PS_CONSTANT_MATERIAL_PARAM_OFFSET
								+ graph->graph_render_info->num_input_vectors
								+ 2 * SHADER_PER_LIGHT_CONST_COUNT_SIMPLE);

	// // Slow doesn't seem to actual be used...
	// if( score > 400 ) {
	// 	accum |= SGFEAT_SLOW;
	// }

	if( instructionCount2Lights > maxSm3InstructionCount || constantCount2Lights > maxSm3ConstantCount || minShaderOpValue >= 3.5 ) {
		accum |= SGFEAT_SM30_PLUS;
	}
	if( instructionCount2Lights > maxSm2xInstructionCount || constantCount2Lights > maxSm2ConstantCount || minShaderOpValue >= 3.0 ) {
		accum |= SGFEAT_SM30;
	}
	if( instructionCountSM20 > maxSm2InstructionCount || constantCount2Lights > maxSm2ConstantCount || minShaderOpValue >= 2.5 ) {
		accum |= SGFEAT_SM20_PLUS;
	}
	if( disableSuperLowEndTemplates || eaFindString( &mated2SuperLowEndTemplates, templateName ) == -1 || minShaderOpValue >= 2.0 ) {
		accum |= SGFEAT_SM20;
	}
	
	return accum;
}

/// Return the number of slots for the most expensive light type.
///
/// LIGHT1, LIGHT2 will get filled in with the light types that has
/// that slot count.
static int mated2MostExpensiveLightTypes( ShaderGraph* graph, RdrLightType* light1, RdrLightType* light2, RdrLightShaderMask * pMostExpensiveLightCombo )
{
	static RdrLightType mostExpensiveLight1 = 0;
	static RdrLightType mostExpensiveLight2 = 0;
	static int slotsMax2Light = 0;
	static RdrLightShaderMask mostExpensiveLightCombo = 0;
	static int slotsMax = 0;

	// TODO - fix this cache to check if the graph changed

	if( mostExpensiveLight1 == 0 && mostExpensiveLight2 == 0 ) {
		RdrShaderPerformanceValues values;
//		RdrLightType light1It;
	//	RdrLightType light2It;
		if (1) // gfx_state.cclighting) // This is on by default currently
		{
			FOR_EACH_IN_EARRAY(preloaded_light_combos, PreloadedLightCombo, light_combo)
			{
				if (light_combo->light_mask & rdrGetMaterialShaderType(RDRLIGHT_SHADOWED, 0))
					continue;

				gfxMaterialsGetPerformanceValuesEx(
					graph->graph_render_info, &values,
					(getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA,
						light_combo->light_mask)),
					true );

				if (!rdrGetLightType(light_combo->light_mask, 2))
				{
					if( values.d3d_instruction_slots > slotsMax2Light || values.instruction_count < 0 ) {
						mostExpensiveLight1 = rdrGetLightType(light_combo->light_mask, 0);
						mostExpensiveLight2 = rdrGetLightType(light_combo->light_mask, 1);
						slotsMax2Light = values.d3d_instruction_slots;
					}
				}

				if( values.d3d_instruction_slots > slotsMax || values.instruction_count < 0 ) {
					mostExpensiveLightCombo = light_combo->light_mask;
					slotsMax = values.d3d_instruction_slots;
				}
			}
			FOR_EACH_END;
		} 
		/*else {
			for( light1It = 1; light1It != RDRLIGHT_TYPE_MAX; ++light1It ) {
				for( light2It = light1It; light2It != RDRLIGHT_TYPE_MAX; ++light2It ) {

					gfxMaterialsGetPerformanceValuesEx(
							graph->graph_render_info, &values,
							getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA,
								rdrGetMaterialShaderType( light1It, 0 ) |
								rdrGetMaterialShaderType( light2It, 1 )),
							true );

					if( values.d3d_instruction_slots > slotsMax || values.instruction_count < 0 ) {
						mostExpensiveLight1 = light1It;
						mostExpensiveLight2 = light2It;
						slotsMax = values.d3d_instruction_slots;
					}
				}
			}
		}*/
	}

	*light1 = mostExpensiveLight1;
	*light2 = mostExpensiveLight2;
	*pMostExpensiveLightCombo = mostExpensiveLightCombo;
	return slotsMax;
}

/// Calcuate DOC's default template graph values.
///
/// (This function should be kept in sync with
/// mated2UpdateAllFeatures)
static ShaderGraphFeatures mated2TemplateDefaultGraphFeature( Mated2EditorDoc* doc, bool isSynchronous )
{
	assert( mated2DocIsTemplate( doc ));
	{
		static RdrShaderPerformanceValues perfValuesMaxLights;
		static RdrShaderPerformanceValues perfValuesSM20; // assumes 2 lights, as does mated2Editor.perfValues
		ShaderGraph* graph = mated2DocActiveShaderGraph( doc );
		RdrLightType light1;
		RdrLightType light2;
		RdrLightShaderMask mostExpensiveCombo;

		if( !graph->graph_render_info ) {
			gfxMaterialsInitShaderTemplate( doc->shaderTemplate );
			assert( graph->graph_render_info );
		}

		mated2MostExpensiveLightTypes( graph, &light1, &light2, &mostExpensiveCombo );

		gfx_disable_auto_force_2_lights = true;
	
		// updates mated2Editor.perfValues
		mated2UpdatePerfValues(graph);

		gfxMaterialsGetPerformanceValuesEx( graph->graph_render_info, &perfValuesSM20,
											getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA |
												MATERIAL_SHADER_FORCE_SM20,
												rdrGetMaterialShaderType( light1, 0 ) |
												rdrGetMaterialShaderType( light2, 1 )),
											isSynchronous );

		gfxMaterialsGetPerformanceValuesEx( graph->graph_render_info, &perfValuesMaxLights,
											getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA,
												mostExpensiveCombo),
											isSynchronous );

		gfx_disable_auto_force_2_lights = false;

		if (perfValuesMaxLights.instruction_count < 0)
		{
			mated2Editor.instructionCountMaxLights = -1;
			mated2Editor.constantCountMaxLights = -1;
		}
		else
		{
			mated2Editor.instructionCountMaxLights = perfValuesMaxLights.d3d_instruction_slots;
			mated2Editor.constantCountMaxLights = PS_CONSTANT_MATERIAL_PARAM_OFFSET
										  + graph->graph_render_info->num_input_vectors
											+ MAX_NUM_SHADER_LIGHTS * SHADER_PER_LIGHT_CONST_COUNT_SIMPLE;
		}
		if (mated2Editor.perfValues.instruction_count < 0)
		{
			mated2Editor.instructionCount2Lights = -1; // Compile failure
			mated2Editor.constantCount2Lights = -1;
		}
		else
		{
			mated2Editor.instructionCount2Lights = mated2Editor.perfValues.d3d_instruction_slots;
			mated2Editor.constantCount2Lights = PS_CONSTANT_MATERIAL_PARAM_OFFSET
												 + graph->graph_render_info->num_input_vectors
												 + 2 * SHADER_PER_LIGHT_CONST_COUNT_SIMPLE;
		}
		if (perfValuesSM20.instruction_count < 0)
		{
			mated2Editor.instructionCountSM20 = -1; // Compile failure
			mated2Editor.constantCountSM20 = -1;
		}
		else
		{
			mated2Editor.instructionCountSM20 = perfValuesSM20.d3d_alu_instruction_slots;
			mated2Editor.constantCountSM20 = PS_CONSTANT_MATERIAL_PARAM_OFFSET
												 + graph->graph_render_info->num_input_vectors
											  + 2 * SHADER_PER_LIGHT_CONST_COUNT_SIMPLE;
		}
		
		return mated2MeasureGraphFeatures(
				doc->shaderTemplate->template_name, graph,
				&mated2Editor.perfValues, &perfValuesMaxLights,
				&perfValuesSM20 );
	}
}

/// Called any time the instanced node has changed
static void mated2DocInstancedNodeChanged(
		UIComboBox* comboBox, Mated2EditorDoc* doc )
{
	ShaderOperation* newInstancedNode = ui_ComboBoxGetSelectedObject( comboBox );

	mated2DocSetInstancedNode( doc, newInstancedNode ? mated2ShaderOpNode( newInstancedNode ) : NULL );
}

static void mated2SetInstancedNodeActionUndo(
		Mated2EditorDoc* doc, Mated2SetInstancedNodeAction* action )
{
	Mated2Node* node = NULL;
	if( action->oldInstancedNodeName ) {
		node = mated2FindNodeByName( doc, action->oldInstancedNodeName );
	}

	mated2DocSetInstancedNode( doc, node );
}

static void mated2SetInstancedNodeActionRedo(
		Mated2EditorDoc* doc,  Mated2SetInstancedNodeAction* action )
{
	Mated2Node* node = NULL;
	if( action->newInstancedNodeName ) {
		node = mated2FindNodeByName( doc, action->newInstancedNodeName );
	}

	mated2DocSetInstancedNode( doc, node );
}

static void mated2SetInstancedNodeActionFree(
		Mated2EditorDoc* doc, Mated2SetInstancedNodeAction* action )
{
	free( action->oldInstancedNodeName );
	free( action->newInstancedNodeName );
	free( action );
}

/// Called any time the reflection type has changed
static void mated2DocReflectionTypeChanged(
	UIComboBox* comboBox, int selected, Mated2EditorDoc* doc )
{
	ShaderGraphReflectionType reflection_type = selected;

	doc->shaderGraph->graph_reflection_type = reflection_type;
	mated2SetDirty( doc );
}

static void mated2DocShaderQualityChanged(
	UIComboBox* comboBox, int selected, Mated2EditorDoc* doc )
{
	ShaderGraphQuality quality = selected;

	doc->shaderGraph->graph_quality = quality;
	mated2SetDirty( doc );
}

static void mated2DocMaxReflectionResolutionChanged(
	UIComboBox* comboBox, int selected, Mated2EditorDoc* doc )
{
	doc->material.material_data->graphic_props.max_reflect_resolution = selected;
	mated2SetDirty( doc );
}

static void mated2DocMaxReflectionResolutionHover(
	UIComboBox* comboBox, int selected, Mated2EditorDoc* doc )
{
	if (selected == -1)
		doc->material.material_data->graphic_props.max_reflect_resolution = ui_ComboBoxGetSelectedEnum(comboBox);
	else
		doc->material.material_data->graphic_props.max_reflect_resolution = selected;
}

/// Finish up loading a material, adding to the DOC all nodes that the
/// template would need and setting material flags.
static bool mated2FinishLoadMaterial( Mated2EditorDoc* doc )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	const MaterialWorldPropertiesLoadTime* worldProps = mated2DocWorldProperties( doc );
	ShaderTemplate* templ = materialGetTemplateByName( gfxProps->default_fallback.shader_template_name );

	if( !templ ) {
		Alertf( "The template, \"%s\" does not exist.  Changing template to "
				"\"DefaultTemplate\".",
				gfxProps->default_fallback.shader_template_name );
		gfxProps->default_fallback.shader_template_name = allocAddString( "DefaultTemplate" );
		templ = materialGetTemplateByName( gfxProps->default_fallback.shader_template_name );
		assert( templ );
	}
	
	assert( !mated2DocIsTemplate( doc ));
	doc->shaderTemplate = StructCreate( parse_ShaderTemplate );
	StructCopyFields( parse_ShaderTemplate, templ, doc->shaderTemplate, 0, 0 );
	doc->shaderTemplate->shader_template_clean = false;
	
	// For materials, the node view should start minimized
	ui_WidgetSetHeight( UI_WIDGET( doc->viewPane ), 100 );

	// patch up some pointers
	doc->shaderTemplate->graph = &doc->shaderTemplate->graph_parser;
	materialUpdateFromData( &doc->material, doc->materialLoadInfo->material_datas[ 0 ]);
	doc->material.graphic_props.material_clean = false;
	doc->material.material_data->graphic_props.active_fallback = &doc->material.material_data->graphic_props.default_fallback;
	doc->material.material_data->graphic_props.shader_template = doc->shaderTemplate;

	doc->shaderGraph = doc->shaderTemplate->graph;
	doc->shaderGuides = &doc->shaderTemplate->guides;
	doc->fallback = &mated2DocGfxProperties( doc )->default_fallback;
	ui_ListSetModel( doc->listMaterialFallbacks, parse_MaterialFallback, &gfxProps->fallbacks );

	mated2RepopulateNodeView( doc, gfxProps, worldProps );
	mated2RepopulatePanels( doc );
	
	doc->isLoading = false;
	doc->validationNeedsUpdate = true;
	doc->validationUpdateDelayFrames = 2;

	/// ALL LOADED, NORMAL FUNCTIONS CAN BE CALLED

	// set the fallback if necesarry
	{
		int autoSetFallbackIndex = mated2DocFallbackIndexByFeatures( doc, systemSpecsMaterialSupportedFeatures() );
		if( autoSetFallbackIndex >= 0 ) {
			mated2DocSetActiveFallback( doc, gfxProps->fallbacks[ autoSetFallbackIndex ]);
			ui_ListSetSelectedRow( doc->listMaterialFallbacks, autoSetFallbackIndex );
		}
	}
	
	return true;
}

/// Select a template by name, and then set it.
static void mated2SelectTemplate( UIButton* button, Mated2EditorDoc* doc )
{
	assert( !mated2DocActiveShaderGraphIsEditable( doc ));
	emPickerShow( mated2TemplatePicker, "Change", false, mated2SelectTemplateSelected, doc );
}

/// Selected cb for MATED2-SELECT-TEMPLATE
static bool mated2SelectTemplateSelected(
		EMPicker* picker, EMPickerSelection** selections, Mated2EditorDoc* doc )
{
	if( eaSize( &selections ) == 0 ) {
		return false;
	} else {
		char buffer[ MAX_PATH ];
		
		ui_ModalDialogSetCustomButtons( "Change Template", "Keep Template", "" );
		if( UICustomButton1 != ui_ModalDialog(
					"Are You Sure?",
					"Changing the template will clear your undo buffer and "
					"CANNOT BE UNDONE.  Are you sure you want to change the "
					"template?",
					ColorWhite, UICustomButton1 | UICustomButton2 )) {
			return false;
		}
		
		getFileNameNoExt( buffer, selections[ 0 ]->doc_name );
		mated2SetTemplate( doc, buffer );

		return true;
	}
}

/// Set a material's template by name.
static void mated2SetTemplate( Mated2EditorDoc* doc, const char* name )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	const MaterialWorldPropertiesLoadTime* worldProps = mated2DocWorldProperties( doc );
	
	ShaderTemplate* templ = materialGetTemplateByName( name );
	assert( !mated2DocActiveShaderGraphIsEditable( doc ));

	if( !templ ) {
		Alertf( "Could not find template %s.", name );
		return;
	}
	
	{
		char buffer[ 256 ];
		sprintf( buffer, "Template: %s", name );
		ui_ButtonSetText( doc->templateButton, buffer );
	}

	if( !doc->isLoading ) {
		doc->isLoading = true;
		doc->shaderGraph->graph_flags &= ~SGRAPH_NO_CACHING;
		doc->previewSelectedNode = NULL;	// Value needs to be cleared or will cause a dangling pointer problem.

		// We're about to enter that nebulus state where the "Active"
		// functions do not return consistant data.
		//
		// We must update the following variables before they work
		// again:
		//
		// * shaderTemplate
		// * shaderGraph
		// * shaderGuides
		// * fallback
		{
			doc->shaderTemplate = StructCreate( parse_ShaderTemplate );
			StructCopyFields( parse_ShaderTemplate, templ, doc->shaderTemplate, 0, 0 );
			doc->shaderTemplate->shader_template_clean = false;
			doc->shaderTemplate->graph = &doc->shaderTemplate->graph_parser;

			doc->shaderGraph = doc->shaderTemplate->graph;
			doc->shaderGuides = &doc->shaderTemplate->guides;
			mated2DocActiveFallback( doc )->shader_template_name = allocAddString( name );
			doc->material.material_data->graphic_props.shader_template = doc->shaderTemplate;
		}
		// All done!  "Active" functions should work again.
		if( !gfxProps->fallbacks_overriden && &mated2DocGfxProperties( doc )->default_fallback == mated2DocActiveFallback( doc )) {
			char buffer[ 256 ];
			const MaterialData* templateData;

			sprintf( buffer, "%s_default", name );
			templateData = materialFindData( buffer );
			if( templateData ) {
				eaClearStruct( &gfxProps->fallbacks, parse_MaterialFallback );
				eaCopyStructs( &templateData->graphic_props.fallbacks, &gfxProps->fallbacks, parse_MaterialFallback );
			} else {
				// questionable -- but I won't do anything
			}
		}
		mated2MaterialValidate( doc, doc->materialLoadInfo->material_datas[ 0 ], true );
		materialPruneOperationValues( doc->materialLoadInfo->material_datas[ 0 ]);

		// okay, now we can rebuild from scratch...
		mated2RepopulateNodeView( doc, gfxProps, worldProps );
		mated2RepopulatePanels( doc );
		EditUndoStackClear( doc->emEd.edit_undo_stack );

		doc->isLoading = false;
		
		mated2SetDirty( doc );
	}
}

/// Select a physical property by name, and then set it.
static void mated2SelectPhysicalProperties( UIButton* button, Mated2EditorDoc* doc )
{
	char buffer[ MAX_PATH ];
	if( UIOk == ui_ModalFileBrowser( "Select Physical Properties", "Select", UIBrowseExisting, UIBrowseFiles, false,
									 "world/PhysicalProperties", "world/PhysicalProperties", ".txt",
									 NULL, 0, SAFESTR( buffer ), NULL))
	{
		getFileNameNoExt( buffer, buffer );
		mated2SetPhysicalProperties( doc, buffer );
	}
}

/// Set the physical properties by name
static void mated2SetPhysicalProperties( Mated2EditorDoc* doc, const char* name )
{
	MaterialWorldPropertiesLoadTime* worldProps = mated2DocWorldProperties( doc );
	char buffer[ 256 ];

	if( doc->isLoading || SET_HANDLE_FROM_STRING( "PhysicalProperties", name, worldProps->physical_properties )) {
		sprintf( buffer, "Physical Props: %s", name );
		ui_ButtonSetText( doc->physicalPropertiesButton, buffer );

		if( !doc->isLoading ) {
			doc->emEd.saved = false;
			mated2EnsureCheckout( doc );
		}
	}
}

/// Validate a material, a-la MATERIAL-VALIDATE, but make sure that
/// any currently being edited templates are used instead of the saved
/// one.
static int mated2MaterialValidate( Mated2EditorDoc* doc, MaterialData* material, bool bRepair )
{
	if( mated2DocIsTemplate( doc )) {
		ShaderTemplate** overrides = NULL;

		eaPush( &overrides, doc->materialLoadInfo->templates[ 0 ]);
		{
			int result = materialValidate( material, bRepair, overrides );
			eaDestroy( &overrides );
			return result;
		}
	} else {
		return materialValidate( material, bRepair, NULL );
	}
}

void MaterialResourcePreview(const ResourceInfo *res_info, const void* pData, const void* ignored, F32 size, BasicTexture** out_tex, Color* out_mod_color)
{
	if (!res_info) {
		*out_tex = texFindAndFlag( "white", false, WL_FOR_UI );
		*out_mod_color = ColorTransparent;
		return;
	}
	
	mated2MaterialPickerPreview(res_info->resourceLocation, out_tex, out_mod_color);
}

AUTO_RUN_LATE;
void RegisterMaterialPreview(void)
{
#ifndef NO_EDITORS
	resRegisterPreviewCallback(MATERIAL_DICT, MaterialResourcePreview, NULL, mated2MaterialPickerPreviewClear);
#endif
}

/// Return a BasicTexture for the material named by MATERIAL-PATH.
void mated2MaterialPickerPreview( const char* materialPath, BasicTexture** outTex, Color* outModColor )
{
	char materialName[ RESOURCE_NAME_MAX_SIZE ];
	Material* material;
	Mated2MaterialPreviewTexture* previewTexture;

	if( !mated2Editor.pickerTextures ) {
		mated2Editor.pickerTextures = stashTableCreateWithStringKeys( 32, StashDeepCopyKeys );
	}
	if( !mated2Editor.previewObject ) {
		mated2EditorPreviewSetObjectDefault();
	}
	
	if( strstri( materialPath, "/Templates/" )) {
		getFileNameNoExt( materialName, materialPath );
		strcat( materialName, "_default" );
	} else {
		getFileNameNoExt( materialName, materialPath );
	}

	if( stashFindPointer( mated2Editor.pickerTextures, materialName, &previewTexture )) {
		assert( previewTexture && previewTexture->texture );

		*outTex = previewTexture->texture;
		if( gfxHeadshotRaisePriority( previewTexture->texture )) {
			U32 curTime = timerCpuMs();
			if (!previewTexture->completeTime) {
				previewTexture->completeTime = curTime;
			}

			*outModColor = ColorWhite;
			outModColor->a = CLAMP(lerp( 0, 255, (curTime - previewTexture->completeTime) * 0.002f), 0, 255 );
		} else {
			*outModColor = ColorTransparent;
			previewTexture->completeTime = 0;
		}
		return;
	}
	
	material = materialFindNoDefault( materialName, WL_FOR_UI );
	if( !material ) {
		*outTex = NULL;
		*outModColor = ColorTransparent;
		return;
	}

	previewTexture = calloc( 1, sizeof( *previewTexture ));
	previewTexture->texture = gfxHeadshotCaptureModel( "mated2MaterialPreview", 512, 512, mated2Editor.previewObject, material, ColorTransparent );
	stashAddPointer( mated2Editor.pickerTextures, materialName, previewTexture, true );

	*outTex = previewTexture->texture;
	*outModColor = ColorTransparent;
	return;
}

void mated2MaterialPickerPreviewClear(void)
{
	FOR_EACH_IN_STASHTABLE( mated2Editor.pickerTextures, Mated2MaterialPreviewTexture, it ) {
		gfxHeadshotRelease( it->texture );
		free( it );
	} FOR_EACH_END;
	stashTableClear( mated2Editor.pickerTextures );
}

/// Return a color for the template or material, based on the score
Color mated2MaterialPickerColor( const char* path, bool isSelected )
{
	ShaderTemplate* template = NULL;
	char name[ MAX_PATH ];
	getFileNameNoExt( name, path );

	if( !template ) {
		const MaterialData* materialData = materialFindData( name );
		if( materialData ) {
			const char* templateName = materialData->graphic_props.default_fallback.shader_template_name;
			if( templateName ) {
				template = materialGetTemplateByName( templateName );
			}
		}
	}
	
	if( !template ) {
		template = materialGetTemplateByName( name );
	}

	if( !template || template->score == -1 ) {
		if( isSelected ) {
			return CreateColorRGB( 255, 255, 255 );
		} else {
			return CreateColorRGB( 0, 0, 0 );
		}
	} else {
		Color color = colorFromRGBA( mated2ScoreColor( template->score ));

		if( !isSelected ) {
			color.r /= 2;
			color.g /= 2;
			color.b /= 2;
		}
		
		return color;
	}
}

/// Add a fallback before the currently selected fallback.
static void mated2DocFallbackAddBefore( UIButton* ignored, Mated2EditorDoc* doc )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	int selectedIndex = ui_ListGetSelectedRow( doc->listMaterialFallbacks );
	
	MaterialFallback* fallbackAccum = StructCreate( parse_MaterialFallback );
	fallbackAccum->shader_template_name = allocAddString( "DefaultTemplate" );
	mated2DocFallbackApplyDefaults( doc, fallbackAccum );

	if( !mated2QueryOverrideFallbacks( doc )) {
		return;
	}

	if( selectedIndex >= 0 ) {
		eaInsert( &gfxProps->fallbacks, fallbackAccum, selectedIndex );
		ui_ListSetSelectedRow( doc->listMaterialFallbacks, selectedIndex + 1 );
	} else {
		eaPush( &gfxProps->fallbacks, fallbackAccum );
	}
	
	mated2SetDirty( doc );
}

/// Remove the currently selected fallback
static void mated2DocFallbackRemove( UIButton* ignored, Mated2EditorDoc* doc )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	int selectedIndex = ui_ListGetSelectedRow( doc->listMaterialFallbacks );

	if( !mated2QueryOverrideFallbacks( doc )) {
		return;
	}
	
	if( selectedIndex >= 0 ) {
		MaterialFallback* fallback = gfxProps->fallbacks[ selectedIndex ];

		if( fallback == mated2DocActiveFallback( doc )) {
			Alertf( "You can not delete the fallback you are currently "
					"editing." );
			return;
		}
		
		eaRemove( &gfxProps->fallbacks, selectedIndex );
		ui_ListSetSelectedRow( doc->listMaterialFallbacks, -1 );

		StructDestroy( parse_MaterialFallback, fallback );
		
		mated2SetDirty( doc );
	}
}

/// Raise the priority of the currently selected fallback.
static void mated2DocFallbackRaisePriority( UIButton* ignored, Mated2EditorDoc* doc )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	int selectedIndex = ui_ListGetSelectedRow( doc->listMaterialFallbacks );

	if( !mated2QueryOverrideFallbacks( doc )) {
		return;
	}

	if( selectedIndex > 0 ) {
		eaSwap( &gfxProps->fallbacks, selectedIndex, selectedIndex - 1 );
		ui_ListSetSelectedRow( doc->listMaterialFallbacks, selectedIndex - 1 );
		mated2SetDirty( doc );
	}
}

/// Lower the priority fo the currently selected fallback.
static void mated2DocFallbackLowerPriority( UIButton* ignored, Mated2EditorDoc* doc )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	int selectedIndex = ui_ListGetSelectedRow( doc->listMaterialFallbacks );

	if( !mated2QueryOverrideFallbacks( doc )) {
		return;
	}

	if( selectedIndex >= 0 && selectedIndex < eaSize( &gfxProps->fallbacks ) - 1 ) {
		eaSwap( &gfxProps->fallbacks, selectedIndex, selectedIndex + 1 );
		ui_ListSetSelectedRow( doc->listMaterialFallbacks, selectedIndex + 1 );
		mated2SetDirty( doc );
	}
}

/// Clear any selection the fallback list has
static void mated2DocFallbackClearSelection( UIButton* ignored, Mated2EditorDoc* doc )
{
	ui_ListSetSelectedRow( doc->listMaterialFallbacks, -1 );
}

/// Apply default values to the newly created FALLBACK.
static void mated2DocFallbackApplyDefaults(
		Mated2EditorDoc* doc, MaterialFallback* fallback )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	ShaderGraph* defaultGraph = doc->shaderGraph;
	ShaderTemplate* fallbackTemplate = materialGetTemplateByName( fallback->shader_template_name );
	ShaderGraph* fallbackGraph;

	if( !fallbackTemplate ) {
		return;
	}
	fallbackGraph = &fallbackTemplate->graph_parser;
	
	assert( fallback->input_mappings == NULL );
	assert( fallback->shader_values == NULL );

	{
		int it;

		for( it = 0; it != eaSize( &fallbackGraph->operations ); ++it ) {
			const ShaderOperation* op = fallbackGraph->operations[ it ];
			const ShaderOperationDef* opDef = GET_REF( op->h_op_definition );

			if(   materialFindOpByName( defaultGraph, op->op_name )
				  && opDef->op_type != SOT_SINK
				  && eaSize( &opDef->op_inputs ) > eaSize( &op->inputs ) + eaSize( &op->fixed_inputs )) {
				ShaderInputMapping* inputMapping = StructCreate( parse_ShaderInputMapping );

				inputMapping->op_name = op->op_name;
				inputMapping->mapped_op_name = op->op_name;

				eaPush( &fallback->input_mappings, inputMapping );
			}
		}
	}
}

/// Set the active, editable fallback.  All mated2DocActive* functions
/// will return different values after this is called.
static void mated2DocFallbackSelectedSetActive( UIButton* ignored, Mated2EditorDoc* doc )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	int selectedIndex = ui_ListGetSelectedRow( doc->listMaterialFallbacks );

	ui_ModalDialogSetCustomButtons( "Change", "", "" );
	if( UICustomButton1 != ui_ModalDialog(
				"Are You Sure?",
				"Changing the active fallback will clear your undo buffer and "
				"CANNOT BE UNDONE.  Are you sure you want to change the active "
				"fallback?",
				ColorWhite, UICustomButton1 | UINo )) {
		return;
	}

	if( selectedIndex >= 0 ) {
		mated2DocSetActiveFallback( doc, gfxProps->fallbacks[ selectedIndex ]);
	} else {
		mated2DocSetActiveFallback( doc, &gfxProps->default_fallback );
	}
}

/// Set the active, editable fallback.  All mated2DocActive* functions
/// will return different values after this is called.
static void mated2DocSetActiveFallback( Mated2EditorDoc* doc, MaterialFallback* fallback )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
	const MaterialWorldPropertiesLoadTime* worldProps = mated2DocWorldProperties( doc );
	bool prevGraphIsEditable = mated2DocActiveShaderGraphIsEditable( doc );
	bool isTemplateDefaultFallback
		= (mated2DocIsTemplate( doc )
		   && fallback == &mated2DocGfxProperties( doc )->default_fallback);
	ShaderTemplate* templ = materialGetTemplateByName( fallback->shader_template_name );

	if( !isTemplateDefaultFallback && !templ ) {
		Alertf( "Could not find the template \"%s\" for the fallback.  "
				"Changing template to \"DefaultTemplate\".",
				fallback->shader_template_name );
		fallback->shader_template_name = allocAddString( "DefaultTemplate" );
		templ = materialGetTemplateByName( fallback->shader_template_name );
		assert( templ );
	}

	doc->isLoading = true;

	doc->shaderGraph->graph_flags &= ~SGRAPH_NO_CACHING;

	// We're about to enter that nebulus state where the "Active"
	// functions do not return consistant data.
	//
	// We must update the following variables before they work again:
	//
	// * shaderTemplate
	// * shaderGraph
	// * shaderGuides
	// * fallback
	{
		if( !prevGraphIsEditable ) {
			StructDestroy( parse_ShaderTemplate, doc->shaderTemplate );
		}
		
		if( isTemplateDefaultFallback ) {
			doc->shaderTemplate = doc->materialLoadInfo->templates[ 0 ];
		} else {
			doc->shaderTemplate = StructCreate( parse_ShaderTemplate );
			StructCopyFields( parse_ShaderTemplate, templ, doc->shaderTemplate, 0, 0 );
			doc->shaderTemplate->shader_template_clean = false;
		}
		doc->shaderTemplate->graph = &doc->shaderTemplate->graph_parser;
		
		doc->shaderGraph = doc->shaderTemplate->graph;
		doc->shaderGuides = &doc->shaderTemplate->guides;
		doc->fallback = fallback;
		
		doc->material.material_data->graphic_props.shader_template = doc->shaderTemplate;
	}
	// All done!  "Active" functions should work again.

	// okay, now we can rebuild from scratch...
	mated2RepopulateNodeView( doc, gfxProps, worldProps );
	mated2RepopulatePanels( doc );

	EditUndoStackClear( doc->emEd.edit_undo_stack );

	doc->isLoading = false;
}

static bool mated2DocHasFallback( Mated2EditorDoc* doc, const char* fallback_name )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );

	if( stricmp( gfxProps->default_fallback.shader_template_name, fallback_name ) == 0 ) {
		return true;
	} else {
		int it;
		for( it = 0; it != eaSize( &gfxProps->fallbacks ); ++it ) {
			if( stricmp( gfxProps->fallbacks[ it ]->shader_template_name, fallback_name ) == 0 ) {
				return true;
			}
		}
	}

	return false;
}

static void mated2InfoWinMemoryTotal( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	eaPush( lines, emInfoWinCreateTextLine( STACK_SPRINTF( "%d", mated2Editor.perfTotalMemory )));
}

static void mated2InfoWinMemoryPrivate( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	eaPush( lines, emInfoWinCreateTextLine( STACK_SPRINTF( "%d", mated2Editor.perfSharedMemory )));
}

static float mated2ScorePercentage( int score )
{
	const int BASE_SCORE = 100;
	const int MAX_GOOD_SCORE = 300;
	const int MAX_BAD_SCORE = 2000;
	const int GOOD_RANGE = (MAX_GOOD_SCORE - BASE_SCORE);

	if( score < BASE_SCORE ) {
		return MAX( 0, lerp( 0, 0.1, (float)score / BASE_SCORE ));
	} else if( score < MAX_GOOD_SCORE ) {
		return lerp( 0.1, 0.75, (float)(score - BASE_SCORE) / (MAX_GOOD_SCORE - BASE_SCORE) );
	} else {
		return MIN( 1,
					lerp( 0.75, 1, (float)(score - MAX_GOOD_SCORE) / (MAX_BAD_SCORE - MAX_GOOD_SCORE) ));
	}
}

static const char* mated2ScoreString( int score )
{
	const char* str = "********************";
	const size_t strLen = strlen( str );

	int desiredLen = MAX( 1, strLen * mated2ScorePercentage( score ));
		
	return str + strLen - desiredLen;
}

static U32 mated2ScoreColor( int score )
{
	const int BASE_SCORE = 100;
	const int MAX_GOOD_SCORE = 300;
	const int MAX_BAD_SCORE = 2000;
	const int GOOD_RANGE = (MAX_GOOD_SCORE - BASE_SCORE);

	if( score < BASE_SCORE ) {
		return 0x7FFF7FFF;
	} else if( score < MAX_GOOD_SCORE ) {
		float ratio = 0.10f + 0.65f * (score - BASE_SCORE) / GOOD_RANGE;
		U8 rb, g;
		
		if( score < (BASE_SCORE + GOOD_RANGE / 2) ) {
			rb = 0x7F * (ratio - 0.10f) / (0.65f/2);
			g = 0xFF;
		} else {
			rb = 0x00;
			g = 0xFF - 0x80 * (ratio - 0.10f - 0.65f/2) / (0.65f/2);
		}

		return (rb << 8) | (rb << 24) | (g << 16) | 0xFF;
	} else {
		float ratio = 0.75f + 0.25f * log( score - MAX_GOOD_SCORE ) / log( MAX_BAD_SCORE - MAX_GOOD_SCORE );
		U8 r, g;
		
		if( ratio < 0.75f + (0.25f/2) ) {
			g = 0x7f;
			r = 0xFF * (ratio - 0.75f) / (0.25f/2);
		} else {
			r = 0xFF;
			g = 0x7f - 0x7f * (ratio - 0.75f - 0.25f/2) / (0.25f/2);
		}

		return (r << 24) | (g << 16) | 0xFF;
	}
}

static void mated2InfoWinPerfScore( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	{
		int score = gfxMaterialScoreFromValues( &mated2Editor.perfValues );
		eaPush( lines, emInfoWinCreateTextLine( STACK_SPRINTF( "%d", score )));
		eaPush( lines, emInfoWinCreateTextLineWithColor(
						mated2ScoreString( score ), mated2ScoreColor( score )));
	}
}

static void mated2InfoWinPerfALU( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	eaPush( lines, emInfoWinCreateTextLine(
					STACK_SPRINTF( "%d", (mated2Editor.perfValues.instruction_count
										  - mated2Editor.perfValues.texture_fetch_count) )));
}

static void mated2InfoWinPerfTexFetch( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	eaPush( lines, emInfoWinCreateTextLine(
					STACK_SPRINTF( "%d", mated2Editor.perfValues.texture_fetch_count )));
}

static void mated2InfoWinPerfTemp( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	eaPush( lines, emInfoWinCreateTextLine(
					STACK_SPRINTF( "%d", mated2Editor.perfValues.temporaries_count )));
}

static void mated2InfoWinPerfParam( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	eaPush( lines, emInfoWinCreateTextLine(
					STACK_SPRINTF( "%d", mated2Editor.perfValues.dynamic_constant_count )));
}

static void mated2InfoWinPerfNVPerfHUD( const char* ignored, EMInfoWinText*** lines )
{
	mated2Editor.lastFrameIsNVPerfShown = true;
	if( mated2Editor.previewMode == PREVIEW_NONE ) { //< TODO: NVPerfHUD re-integration
		return;
	}

	{
		char buffer[ 128 ];
		
		eaPush( lines, emInfoWinCreateTextLine( 
						friendlyUnitBuf( metricSpec, mated2Editor.perfValues.nvps_pps, buffer )));
	}
}

static void mated2InfoWinInstructionCountMaxLights( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	if( mated2Editor.instructionCountMaxLights < 0 ) {
		eaPush( lines, emInfoWinCreateTextLine( "Unsupported" ));
	} else {
		eaPush( lines, emInfoWinCreateTextLine(
						STACK_SPRINTF( "%d", mated2Editor.instructionCountMaxLights )));
	}
}

static void mated2InfoWinInstructionCount2Lights( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	if( mated2Editor.instructionCount2Lights < 0 ) {
		eaPush( lines, emInfoWinCreateTextLine( "Unsupported" ));
	} else {
		eaPush( lines, emInfoWinCreateTextLine(
						STACK_SPRINTF( "%d", mated2Editor.instructionCount2Lights )));
	}
}

static void mated2InfoWinInstructionCountSM20( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	if( mated2Editor.instructionCountSM20 < 0 ) {
		eaPush( lines, emInfoWinCreateTextLine( "Unsupported" ));
	} else {
		eaPush( lines, emInfoWinCreateTextLine(
						STACK_SPRINTF( "%d", mated2Editor.instructionCountSM20 )));
	}
}

void mated2InfoWinConstCountMaxLights( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	if( mated2Editor.constantCountMaxLights < 0 ) {
		eaPush( lines, emInfoWinCreateTextLine( "Unsupported" ));
	} else {
		eaPush( lines, emInfoWinCreateTextLine(
						STACK_SPRINTF( "%d", mated2Editor.constantCountMaxLights )));
	}
}

void mated2InfoWinConstCount2Lights( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	if( mated2Editor.constantCount2Lights < 0 ) {
		eaPush( lines, emInfoWinCreateTextLine( "Unsupported" ));
	} else {
		eaPush( lines, emInfoWinCreateTextLine(
						STACK_SPRINTF( "%d", mated2Editor.constantCount2Lights )));
	}
}

void mated2InfoWinConstCountSM20( const char* ignored, EMInfoWinText*** lines )
{
	if( mated2Editor.previewMode == PREVIEW_NONE ) {
		return;
	}

	if( mated2Editor.constantCountSM20 < 0 ) {
		eaPush( lines, emInfoWinCreateTextLine( "Unsupported" ));
	} else {
		eaPush( lines, emInfoWinCreateTextLine(
						STACK_SPRINTF( "%d", mated2Editor.constantCountSM20 )));
	}
}

void mated2NodeCreateActionCreate( Mated2EditorDoc* doc, Mated2NodeCreateAction* action )
{
	const ShaderOperationDef* opDef = GET_REF( action->opDef );

	if( opDef == NULL ) {
		Alertf( "Trying to create a node that can not be created!  Talk to Jared right now!" );
		return;
	} else {
		Mated2Node* node = mated2NewNodeByOpAndName( doc, opDef, action->nodeName );
		ui_WidgetSetPosition( UI_WIDGET( mated2NodeUI( node )),
							  action->nodePos[ 0 ], action->nodePos[ 1 ]);
	}
}

void mated2NodeCreateActionRemove( Mated2EditorDoc* doc, Mated2NodeCreateAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );
	
	ui_WindowClose( UI_WINDOW( mated2NodeUI( node )));
}

void mated2NodeCreateActionFree( Mated2EditorDoc* doc, Mated2NodeCreateAction* action )
{
	free( action->nodeName );
	REMOVE_HANDLE( action->opDef );
	free( action );
}

/// Get the active shader graph.
ShaderGraph* mated2DocActiveShaderGraph( Mated2EditorDoc* doc )
{
	return doc->shaderGraph;
}

/// Get the active set of guides
ShaderGuide*** mated2DocActiveShaderGuide( Mated2EditorDoc* doc )
{
	return doc->shaderGuides;
}

/// Get the active MATERIAL-FALLBACK.
MaterialFallback* mated2DocActiveFallback( Mated2EditorDoc* doc )
{
	return doc->fallback;
}

/// Get the index for the active MATERIAL-FALLBACK.
int mated2DocActiveFallbackIndex( Mated2EditorDoc* doc )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );

	int it;
	for( it = 0; it != eaSize( &gfxProps->fallbacks ); ++it ) {
		if( gfxProps->fallbacks[ it ] == doc->fallback ) {
			return it;
		}
	}

	return -1;
}

///
int mated2DocFallbackIndexByFeatures( Mated2EditorDoc* doc, ShaderGraphFeatures sgfeat )
{
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );

	MaterialFallback* fallback = &gfxProps->default_fallback;
	ShaderTemplate *shaderTemplate = materialGetTemplateByName( fallback->shader_template_name );

	if( shaderTemplateIsSupportedEx( shaderTemplate, ~sgfeat )) {
		return -1;
	}
	{
		int it;
		for( it = 0; it != eaSize( &gfxProps->fallbacks ); ++it ) {
			fallback = gfxProps->fallbacks[ it ];
			shaderTemplate = materialGetTemplateByName( fallback->shader_template_name );

			if( shaderTemplateIsSupportedEx( shaderTemplate, ~sgfeat )) {
				return it;
			}
		}
	}

	return -2;
}

/// Get the doc's MATERIAL-DATA.
MaterialData* mated2DocMaterialData( Mated2EditorDoc* doc )
{
	return doc->materialLoadInfo->material_datas[0];
}

/// Get the active graphical properties.
MaterialGraphicPropertiesLoadTime* mated2DocGfxProperties( Mated2EditorDoc* doc )
{
	return &doc->materialLoadInfo->material_datas[ 0 ]->graphic_props;
}

/// Get the active world properties.
MaterialWorldPropertiesLoadTime* mated2DocWorldProperties( Mated2EditorDoc* doc )
{
	return &doc->materialLoadInfo->material_datas[ 0 ]->world_props;
}

Mated2Guide*** mated2ShaderGuides( Mated2EditorDoc* doc )
{
	return &doc->guides;
}

UIFlowchart* mated2Flowchart( Mated2EditorDoc* doc )
{
	return doc->flowchart;
}

/// Begin an undo group.  Any complete undo group will get undone or redone as one atomic
/// operation.  This group will not be recorded as being undo-able until
/// MATED2-UNDO-END-GROUP is called.
void mated2UndoBeginGroup( Mated2EditorDoc* doc )
{
	++doc->undoGroupDepth;
}

/// End and commit an undo group. 
void mated2UndoEndGroup( Mated2EditorDoc* doc )
{
	assert( doc->undoGroupDepth > 0 );
	--doc->undoGroupDepth;

	if( doc->undoGroupDepth == 0 ) {
		EditUndoBeginGroup( doc->emEd.edit_undo_stack );
		{
			int it;
			for( it = 0; it != eaSize( &doc->undoQueuedCommands ); ++it ) {
				Mated2UndoCommand* command = doc->undoQueuedCommands[ it ];

				EditCreateUndoCustom( doc->emEd.edit_undo_stack, command->undoFn,
									  command->redoFn, command->freeFn,
									  command->userData );
			}
		}
		EditUndoEndGroup( doc->emEd.edit_undo_stack );

		eaClearEx( &doc->undoQueuedCommands, NULL );
	}
}

/// Cancel all potential undo groups.  They will not get put into the undo stack.
void mated2UndoCancelUnfisihedGroups( Mated2EditorDoc* doc )
{
	assert( doc->undoGroupDepth > 0 );
	doc->undoGroupDepth = 0;

	{
		int it;
		for( it = 0; it != eaSize( &doc->undoQueuedCommands ); ++it ) {
			Mated2UndoCommand* command = doc->undoQueuedCommands[ it ];
			command->freeFn( doc, command->userData );
		}
	}
	eaClearEx( &doc->undoQueuedCommands, NULL );
}

/// Record an undo action.  If not in a group, this will get put directly into the undo
/// stack.  If in a group, these commands will get queued up until MATED2-UNDO-END-GROUP is
/// called.
void mated2UndoRecord( Mated2EditorDoc* doc, EditUndoCustomFn undoFn,
					   EditUndoCustomFn redoFn, EditUndoCustomFreeFn freeFn,
					   void* userData )
{
	if( doc->undoGroupDepth == 0 ) {
		EditCreateUndoCustom( doc->emEd.edit_undo_stack, undoFn, redoFn, freeFn,
							  userData );
	} else {
		Mated2UndoCommand* accum = calloc( 1, sizeof( *accum ));
		accum->undoFn = undoFn;
		accum->redoFn = redoFn;
		accum->freeFn = freeFn;
		accum->userData = userData;

		eaPush( &doc->undoQueuedCommands, accum );
	}
}

bool mated2IsLoading( Mated2EditorDoc* doc )
{
	return doc->isLoading;
}

/// Return TRUE if the current doc is a material template.  Note:
/// One-off materials are *also* considered templates.
bool mated2DocIsTemplate( Mated2EditorDoc* doc )
{
	return eaSize( &doc->materialLoadInfo->templates ) > 0;
}

/// Return TRUE if the current doc is a one-off material template.
///
/// A "one-off" material template is a template that is used only
/// once, so it is not saved in the templates directory.
bool mated2DocIsOneOff( Mated2EditorDoc* doc )
{
	return mated2DocIsTemplate( doc ) && !strStartsWith( doc->emEd.doc_name, "materials/templates/" );
}

/// Return TRUE if the current visible shader graph is editable.
///
/// A graph is editable if and only if it is in a template/one-off,
/// and that graph is part of the default fallback.
bool mated2DocActiveShaderGraphIsEditable( SA_PARAM_NN_VALID Mated2EditorDoc* doc )
{
	return (mated2DocIsTemplate( doc )
			&& doc->fallback == &mated2DocGfxProperties( doc )->default_fallback);
}

bool mated2SupportedSwizzle( const U8 swizzle[ 4 ])
{
	if( swizzle[ 0 ] == 0 && swizzle[ 1 ] == 1 && swizzle[ 2 ] == 2 && swizzle[ 3 ] == 3 ) {
		return true;
	}

	if(   swizzle[ 0 ] == swizzle[ 1 ] && swizzle[ 1 ] == swizzle[ 2 ] && swizzle[ 2 ] == swizzle[ 3 ]
		  && swizzle[ 0 ] < 4 ) {
		return true;
	}

	return false;
}

int mated2SwizzleOffset( const U8 swizzle[ 4 ])
{
	assert( mated2SupportedSwizzle( swizzle ));
	
	if( swizzle[ 0 ] == 0 && swizzle[ 1 ] == 1 && swizzle[ 2 ] == 2 && swizzle[ 3 ] == 3 ) {
		return 0;
	} else { // must be all the same type...
		return swizzle[ 0 ] + 1;
	}
}

/// Return a unique name based on OP-NAME.
///
/// The name will be unique at creation time, for every node in DOC.
/// This name can still be made non-unique if the user types in some
/// other name.
char* mated2UniqueNodeName(
		Mated2EditorDoc* doc, const char* opName, char* buffer, int buffer_size )
{
	ShaderGraph* graph = mated2DocActiveShaderGraph( doc );
	
	strcpy_s( SAFESTR2( buffer ), opName );

	if( !mated2FindNodeByNameInGraph( doc, buffer, graph )) {
		return buffer;
	} else {
		int suffix = 1;

		do {
			sprintf_s( SAFESTR2( buffer ), "%s%d", opName, suffix++ );
		} while( mated2FindNodeByNameInGraph( doc, buffer, graph ));

		return buffer;
	}
}

/// Ensure all relevant files for the doc are checked out.
///
/// Return true if this was successfully done.
bool mated2EnsureCheckout( Mated2EditorDoc* doc )
{
	char filename[ MAX_PATH ];
	sprintf( filename, "%s.material", doc->emEd.doc_name );
	
	if( doc->fileCheckedOut || doc->emEd.doc_name[ 0 ] == '\0' ) {
		return true;
	} else if( doc->fileCheckOutFailed ) {
		return false;
	}

	if( !mated2EnsureCheckoutFile( filename )) {
		doc->fileCheckOutFailed = true;
		return false;
	} else {
		doc->fileCheckedOut = true;
		return true;
	}
}

/// Ensure that FILENAME is checked out.
///
/// Return true if this was successfully done.
bool mated2EnsureCheckoutFile( const char* filename )
{
	int ret;
	
	gfxShowPleaseWaitMessage( "Please wait, checking out file..." );

	if( !gimmeDLLQueryIsFileLatest( filename )) {
		// The user doesn't have the latest version of the file, do
		// not let them edit it!  If we were to check out the file
		// here, the file would be changed on disk, but not reloaded,
		// and that would be bad!  Someone else's changes would most
		// likely be lost.
		Alertf( "Error: file (%s) unable to be checked out, someone else has "
				"changed it since you last got latest.  Exit, get latest and "
				"reload the file.",
				filename );
		return false;
	}

	ret = gimmeDLLDoOperation( filename, GIMME_CHECKOUT, 0 );
	if( ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_NO_DLL ) {
		emuHandleGimmeMessage( filename, ret, true, true );
		gfxStatusPrintf( "Check out FAILED on %s", filename );
		return false;
	}
	gfxStatusPrintf( "You have checked out: %s", filename );
	return true;
}

/// Ensure that all FILENAMES are checked out.
///
/// Return true if this was successfully done.
bool mated2EnsureCheckoutFiles( const char** filenames )
{
	int ret;

	gfxShowPleaseWaitMessage( "Please wait, checking out file..." );

	{
		bool isAllLatestAccum = true;
		int it;
		for( it = 0; it != eaSize( &filenames ); ++it ) {
			if( !gimmeDLLQueryIsFileLatest( filenames[ it ])) {
				// The user doesn't have the latest version of the
				// file, do not let them edit it!  If we were to check
				// out the file here, the file would be changed on
				// disk, but not reloaded, and that would be bad!
				// Someone else's changes would most likely be lost.
				Alertf( "Error: file (%s) unable to be checked out, someone "
						"else has changed it since you last got latest.  Exit, "
						"get latest and reload the file.",
						filenames[ it ]);
				isAllLatestAccum = false;
			}
		}

		if( !isAllLatestAccum ) {
			return false;
		}
	}

	ret = gimmeDLLDoOperations( filenames, GIMME_CHECKOUT, 0 );
	if( ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_NO_DLL ) {
		Alertf( "Error checking out all files (see console for details)." );
		gfxStatusPrintf( "Check out FAILED" );
		return false;
	}
	gfxStatusPrintf( "You have checked out %d files", eaSize( &filenames ));
	return true;
}

void mated2SetDirty( Mated2EditorDoc* doc )
{
	doc->validationNeedsUpdate = true;
	doc->validationUpdateDelayFrames = 2;
	doc->emEd.saved = false;
	mated2EnsureCheckout( doc );
	mated2DocActiveFallback( doc )->fallback_changed = true;

	if( mated2DocIsTemplate( doc )) {
		doc->materialLoadInfo->templates[ 0 ]->shader_template_clean = false;
	}
}

void mated2SetInputListNeedsReflow( Mated2EditorDoc* doc, bool needsReflow )
{
	doc->inputListNeedsReflow = needsReflow;
}

const char** mated2NodeNamesUsingOpDefInDefault(
		Mated2EditorDoc* doc, const ShaderOperationDef* opDef )
{
	MaterialFallback* defaultFallback = &mated2DocGfxProperties( doc )->default_fallback;
	const char** accum = NULL;
	int it;
	ShaderGraph* defaultGraph = NULL;

	if( mated2DocIsTemplate( doc )) {
		defaultGraph = doc->materialLoadInfo->templates[ 0 ]->graph;
	} else {
		ShaderTemplate* templ = materialGetTemplateByName( defaultFallback->shader_template_name );
		if( templ ) {
			defaultGraph = templ->graph;
			assert( defaultGraph );
		}
	}

	if( defaultGraph ) {
		for( it = 0; it != eaSize( &defaultFallback->shader_values ); ++it ) {
			ShaderOperationValues* input = defaultFallback->shader_values[ it ];
			ShaderOperation* node = materialFindOpByName( defaultGraph, input->op_name );
			if( !node ) {
				continue;
			}
			if(   GET_REF( node->h_op_definition ) != opDef
				  || eaSize( &node->fixed_inputs ) != 0 ) {
				continue;
			}

			eaPush( &accum, node->op_name );
		}
	}

	return accum;
}

/// Notify DOC that the node named FROM is now named TO. 
void mated2DocNodeRename( Mated2EditorDoc* doc, const char* from, const char* to )
{
	const char* originalName = NULL;
	if( stashFindPointer( doc->renamesTable, from, (void**)&originalName )) {
		stashRemovePointer( doc->renamesTable, from, NULL );
		from = originalName;
	}

	stashAddPointer( doc->renamesTable, to, from, true );
}

/// Set DOC's instanced node to NODE, or to none if NODE is null.
void mated2DocSetInstancedNode( Mated2EditorDoc* doc, const Mated2Node* node )
{
	const ShaderOperation* nodeOp = (node ? mated2NodeShaderOpConst( node ) : NULL);
	
	bool changeMadeAccum = false;
	int it;

	for( it = 0; it != eaSize( &doc->shaderGraph->operations ); ++it ) {
		ShaderOperation* op = doc->shaderGraph->operations[ it ];
		bool newInstanceParam = (op == nodeOp);

		if( op->instance_param != newInstanceParam ) {
			Mated2Node* opNode = mated2ShaderOpNode( op );

			changeMadeAccum = true;
			op->instance_param = newInstanceParam;
			mated2NodeInstancedUpdate( opNode, newInstanceParam );
		}
	}

	if( changeMadeAccum ) {
		ShaderOperation* oldOp = ui_ComboBoxGetSelectedObject( doc->comboBoxInstancedNode );
		ui_ComboBoxSetSelectedObject( doc->comboBoxInstancedNode, nodeOp );

		if( !mated2IsLoading( doc )) {
			Mated2SetInstancedNodeAction* accum = calloc( 1, sizeof( *accum ));

			if( oldOp ) {
				accum->oldInstancedNodeName = strdup( oldOp->op_name );
			}
			if( nodeOp ) {
				accum->newInstancedNodeName = strdup( nodeOp->op_name );
			}

			mated2UndoRecord( doc, mated2SetInstancedNodeActionUndo,
							  mated2SetInstancedNodeActionRedo,
							  mated2SetInstancedNodeActionFree,
							  accum );
			
			mated2SetDirty( doc );
		}
	}
}

/// Translate an op's name into the local language.
const char* mated2TranslateOpName( const char* opName )
{
	const char* translated;
	char buffer[ 256 ];
	sprintf( buffer, "MaterialEditor.Op.%s", opName );

	// Since message keys can't have a '+' in them, we replace them
	// with 'P'.
	strchrReplace( buffer, '+', 'P' );

	translated = langTranslateMessageKey( LANGUAGE_DEFAULT, buffer );
	if( translated ) {
		return translated;
	} else {
		return opName;
	}
}

static void mated2ListTemplatesWithoutFallbacks1(
		char** needingFallbacksAccum, const char *materialName )
{
	if( materialName && strEndsWith( materialName, "_Default" )) {
		const MaterialData* materialData = materialFindData( materialName );
	
		if( materialData && !materialDataHasRequiredFallbacks( materialData, NULL )) {
			ShaderTemplate* templ = materialGetTemplateByName( materialData->graphic_props.default_fallback.shader_template_name );
			if (templ) {
				ANALYSIS_ASSUME(templ != NULL);
				if (gfxMaterialShaderTemplateIsPreloaded( templ )) {
					estrConcatf( needingFallbacksAccum, "\n%s", materialData->filename );
				}
			}
		}
	}
}

/// Print a list of each template that does not have all required
/// fallbacks.
AUTO_COMMAND;
void mated2ListTemplatesWithoutFallbacks( void )
{
	char* needingFallbacks = NULL;
	int numTemplates;
	estrCreate( &needingFallbacks );
	
	materialForEachMaterialName( (ForEachMaterialNameCallback)mated2ListTemplatesWithoutFallbacks1,
								 &needingFallbacks );
	numTemplates = 0;
	{
		int it = 0;
		while( needingFallbacks[ it ] != '\0' ) {
			if( needingFallbacks[ it ] == '\n' ) {
				++numTemplates;
			}
			++it;
		}
	}

	if( estrLength( &needingFallbacks )) {
		if( estrLength( &needingFallbacks ) > 2000 ) {
			estrSetSize( &needingFallbacks, 1980 );
			estrConcatStatic( &needingFallbacks, "...\nand more." );
		}
		Alertf( "The following %d templates need their fallbacks updated:%s",
				numTemplates, needingFallbacks );
	} else {
		Alertf( "All templates have up to date fallbacks!" );
	}
	estrDestroy( &needingFallbacks );
}

/// Print a list of each material with a suspicious brightness.
AUTO_COMMAND;
void mated2ListAllWithSuspiciousBrightness( void )
{
	char* accum = NULL;
	int num;

	estrCreate( &accum );
	{
		char** allWithSuspiciousBrightness = mated2AllWithSuspiciousBrightness();
		int it;
		num = eaSize( &allWithSuspiciousBrightness );
		for( it = 0; it != num; ++it ) {
			estrConcatf( &accum, "\n%s", allWithSuspiciousBrightness[ it ]);
		}

		eaDestroy( &allWithSuspiciousBrightness );
	}

	if( estrLength( &accum ) > 2000 ) {
		estrSetSize( &accum, 1980 );
		estrConcatStatic( &accum, "...\nand more." );
		Alertf( "The following %d materials have suspicious brightness values:%s",
				num, accum );
	} else {
		Alertf( "No material has a suspicious brightness value!" );
	}
	estrDestroy( &accum );
}

/// Try to cause a crash.
AUTO_COMMAND;
void mated2CrashTest( void )
{
	char** allMaterials = mated2AllMaterials();
	int loopIt;
	int it = 0;
	
	for( loopIt = 0; true; ++loopIt ) {
		for( it = 0; it != eaSize( &allMaterials ); ++it ) {
			Mated2EditorDoc* doc = (Mated2EditorDoc*)emOpenFile( allMaterials[ it ]);
			if( !doc ) {
				continue;
			}
			doc->emEd.saved = false;
			emSaveDocEx( (EMEditorDoc*)doc, false, NULL );
			emCloseDoc( (EMEditorDoc*)doc );
			++it;
		}
		
		if( loopIt % 100 == 0 ) {
			printf( "%d iterations...\n", loopIt );
		}
	}
}

/// Update all materials that have a suspicious brightness.
AUTO_COMMAND;
void mated2UpdateAllWithSuspiciousBrightness( void )
{
	char** allWithSuspiciousBrightness = mated2AllWithSuspiciousBrightness();
	mated2EnsureCheckoutFiles( allWithSuspiciousBrightness );

	gfxShowPleaseWaitMessage( "Please wait, updating brightness..." );
	{
		int it;
		for( it = 0; it != eaSize( &allWithSuspiciousBrightness ); ++it ) {
			Mated2EditorDoc* doc = (Mated2EditorDoc*)emOpenFile( allWithSuspiciousBrightness[ it ]);
			doc->emEd.saved = false;
			emSaveDocEx( (EMEditorDoc*)doc, false, NULL );
			emCloseDoc( (EMEditorDoc*)doc );
		}
	}
	eaDestroy( &allWithSuspiciousBrightness );
}

/// Update all material templates.
AUTO_COMMAND;
void mated2UpdateAllMaterialTemplates( void )
{
	const char** templateFileNames = mated2AllMaterialTemplates();
	mated2EnsureCheckoutFiles( templateFileNames );

	gfxShowPleaseWaitMessage( "Please wait, updating templates..." );
	{
		int it;
		for( it = 0; it != eaSize( &templateFileNames ); ++it ) {
			Mated2EditorDoc* doc;
			printf( "Updating %s...\n", templateFileNames[ it ]);

			doc = (Mated2EditorDoc*)emOpenFile( templateFileNames[ it ]);
			if( doc ) {
				doc->emEd.saved = false;
				emSaveDocEx( (EMEditorDoc*)doc, false, NULL );
				emCloseDoc( (EMEditorDoc*)doc );
			}
		}
	}
	eaDestroy( &templateFileNames );
}

/// Checkout all the materials that have a suspicious brightness.
AUTO_COMMAND;
void mated2CheckoutAllWithSuspiciousBrightness( void )
{
	char** allWithSuspiciousBrightness = mated2AllWithSuspiciousBrightness();
	mated2EnsureCheckoutFiles( allWithSuspiciousBrightness );
	eaDestroy( &allWithSuspiciousBrightness );
}

/// Checkout all materials that use the currently edited template.
AUTO_COMMAND;
void mated2CheckoutAllWhoUseMe( void )
{
	if( mated2DocIsTemplate( mated2Editor.activeDoc )) {
		char** allWhoUseMe = mated2AllWhoUseMe( mated2Editor.activeDoc, false );
		mated2EnsureCheckoutFiles( allWhoUseMe );
		eaDestroy( &allWhoUseMe );
	}
}

/// Checkout all materials that use the currently edited template,
/// either directly or as a fallback.
AUTO_COMMAND;
void mated2CheckoutAllWhoUseMeFallbacks( void )
{
	if( mated2DocIsTemplate( mated2Editor.activeDoc )) {
		char** allWhoUseMe = mated2AllWhoUseMe( mated2Editor.activeDoc, true );
		mated2EnsureCheckoutFiles( allWhoUseMe );
		eaDestroy( &allWhoUseMe );
	}
}

static void mated2ListMaterialsWithoutFallbacks1(
		char** needingFallbacksAccum, const char *materialName )
{
	if( materialName && !strEndsWith( materialName, "_Default" )) {
		const MaterialData* materialData = materialFindData( materialName );

		if( materialData && !materialDataHasRequiredFallbacks( materialData, NULL )) {
			ShaderTemplate* templ = materialGetTemplateByName( materialData->graphic_props.default_fallback.shader_template_name );
			if (templ) {
				ANALYSIS_ASSUME(templ != NULL);
				if (gfxMaterialShaderTemplateIsPreloaded( templ )) {
					estrConcatf( needingFallbacksAccum, "\n%s", materialData->filename );
				}
			}
		}
	}
}

/// Print a list of each material that does not have all required
/// fallbacks.
AUTO_COMMAND;
void mated2ListMaterialsWithoutFallbacks( void )
{
	char* needingFallbacks = NULL;
	int numMaterials;
	estrCreate( &needingFallbacks );
	
	materialForEachMaterialName( (ForEachMaterialNameCallback)mated2ListMaterialsWithoutFallbacks1, &needingFallbacks );
	numMaterials = 0;
	{
		int it = 0;
		while( needingFallbacks[ it ] != '\0' ) {
			if( needingFallbacks[ it ] == '\n' ) {
				++numMaterials;
			}
			++it;
		}
	}

	if( estrLength( &needingFallbacks )) {
		if( estrLength( &needingFallbacks ) > 2000 ) {
			estrSetSize( &needingFallbacks, 1980 );
			estrConcatStatic( &needingFallbacks, "...\nand more." );
		}
		Alertf( "The following %d materials need their fallbacks updated:%s",
				numMaterials, needingFallbacks );
	} else {
		Alertf( "All materials have up to date fallbacks!" );
	}
	estrDestroy( &needingFallbacks );
}

/// Update each ShaderTemplate's feature list
///
/// (This function must be kept in sync with
/// mated2TemplateDefaultGraphFeature)
AUTO_COMMAND;
void mated2UpdateAllFeatures( void )
{
	rdr_state.disableShaderProfiling = false;
	{
		const char** templateFileNames = NULL;
		int it;
		for( it = 0; it != eaSize( &material_load_info.templates ); ++it ) {
			eaPush( &templateFileNames, material_load_info.templates[ it ]->filename );
		}
		mated2EnsureCheckoutFiles( templateFileNames );

		eaDestroy( &templateFileNames );
	}
	
	{
		int it;
		for( it = 0; it != eaSize( &material_load_info.templates ); ++it ) {
			ShaderTemplate* templ = material_load_info.templates[ it ];
			ShaderGraphFeatures defaultFeatures;
			
			loadstart_printf( "Updating %s...", templ->filename );
			mated2EnsureCheckoutFile( templ->filename );

			if( !templ->graph || !templ->graph->graph_render_info ) {
				gfxMaterialsInitShaderTemplate( templ );
				assert( templ->graph && templ->graph->graph_render_info );
			}

			{
				static RdrShaderPerformanceValues perfValuesMaxLights;
				static RdrShaderPerformanceValues perfValues2Lights;
				static RdrShaderPerformanceValues perfValuesLowEnd;
				ShaderGraphRenderInfo* graphRenderInfo = templ->graph->graph_render_info;
				RdrLightType light1;
				RdrLightType light2;
				RdrLightShaderMask mostExpensiveCombo;

				mated2MostExpensiveLightTypes( templ->graph, &light1, &light2, &mostExpensiveCombo );

				gfx_disable_auto_force_2_lights = true;
				mated2UpdatePerfValues(templ->graph);

				gfxMaterialsGetPerformanceValuesEx( graphRenderInfo, &perfValuesLowEnd,
					getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA |
					MATERIAL_SHADER_FORCE_SM20,
														rdrGetMaterialShaderType( light1, 0 ) |
														rdrGetMaterialShaderType( light2, 1 )),
													true );

				gfxMaterialsGetPerformanceValuesEx( graphRenderInfo, &perfValuesMaxLights,
											getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA,
												mostExpensiveCombo),
											true );

				gfx_disable_auto_force_2_lights = false;

				defaultFeatures = mated2MeasureGraphFeatures(
						templ->template_name, templ->graph,
						&mated2Editor.perfValues, &perfValuesMaxLights,
						&perfValuesLowEnd );
			}

			{
				MaterialLoadInfo loadInfo;
				ShaderGraph* graph;

				StructInit( parse_MaterialLoadInfo, &loadInfo );
			
				g_materials_skip_fixup = true;
				ParserLoadFiles( NULL, templ->filename, NULL, 0, parse_MaterialLoadInfo, &loadInfo );
				assert( eaSize( &loadInfo.templates ) && eaSize( &loadInfo.material_datas ));
				loadInfo.material_datas[ 0 ]->material_name = "";
				g_materials_skip_fixup = false;

				graph = &loadInfo.templates[ 0 ]->graph_parser;

				graph->graph_features &= graph->graph_features_overriden;
				graph->graph_features |= (defaultFeatures & ~graph->graph_features_overriden);

				ParserWriteTextFile( templ->filename, parse_MaterialLoadInfo, &loadInfo, 0, 0 );

				StructDeInit( parse_MaterialLoadInfo, &loadInfo );
			}

			loadend_printf( "done" );
		}
	}
}

/// Scan every material, looking for fixed inputs.
AUTO_COMMAND;
void mated2AnalyzeFixedInputs(void)
{
	printf("Analyzing templates for fixed inputs...\n");

	FOR_EACH_IN_EARRAY(material_load_info.templates, ShaderTemplate, shader_template)
	{
		if (shader_template->graph != &shader_template->graph_parser)
			continue; // Duplicate
		FOR_EACH_IN_EARRAY(shader_template->graph->operations, ShaderOperation, op)
		{
			const ShaderOperationDef *op_def = GET_REF(op->h_op_definition);
			if (!op_def)
				continue;
			FOR_EACH_IN_EARRAY(op_def->op_inputs, ShaderInput, op_input)
			{
				Vec4 values;
				bool bSame=true;
				bool bFirst=true;
				int count=0;
				if (!op_input->num_floats || op_input->num_floats > 4)
					continue;
				if (op_input->input_default.default_type != SIDT_NODEFAULT)
					continue;
				if (op_input->input_hidden)
					continue;
				if (op_input->data_type == SDT_SCROLL ||
					shaderDataTypeNeedsDrawableMapping(op_input->data_type))
					continue;
				if (materialFindInputEdgeByName(op, op_input->input_name))
					continue;
				if (materialFindFixedInputByName(op, op_input->input_name))
					continue;
				// Regular input, with values in each material, see if they're all the same
				// Check all materials that use this template
				FOR_EACH_IN_EARRAY(material_load_info.material_headers, Material, material)
				{
					const MaterialFallback* shader_fallback;
					const MaterialData* material_data = materialGetData(material);
					
					shader_fallback = materialDataHasShaderTemplate(material_data, shader_template->template_name);
					if (shader_fallback) {
						const ShaderOperationSpecificValue *spec_value = materialFindOperationSpecificValue2Const(materialFindOperationValuesConst(material_data, shader_fallback, op->op_name), op_input->input_name);
						int num_floats = spec_value?eafSize(&spec_value->fvalues):0;
						if (spec_value && num_floats) {
							Vec4 myvalues = {0};
							setVec4(myvalues, spec_value->fvalues[0], (num_floats>1)?spec_value->fvalues[1]:0, (num_floats>2)?spec_value->fvalues[2]:0, (num_floats>3)?spec_value->fvalues[3]:0);
							if (bFirst) {
								copyVec4(myvalues, values);
								bFirst = false;
								count=1;
							} else {
								if (!nearSameVec4(myvalues, values)) {
									bSame = false;
								} else {
									count++;
								}
							}
						}
					}
					if (!bSame)
						break;
				}
				FOR_EACH_END;
				if (bSame && count > 1) {
					// Check all costume files to see if they specify this parameter
					if (wlCostumeDoesAnyOverrideValue(shader_template, op->op_name))
						bSame = false;

					if (bSame) {
						printf("%d Materials, Template %s  Op %s  Input %s\n",
							   count, shader_template->filename, op->op_name, op_input->input_name);
					}
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
	printf("Done.\n");
}

typedef struct ShaderBin {
	double height;
	ShaderOperation** bin;
} ShaderBin;

// This function assumes that there is only one copy of the object in all of the EArrays contained within sbins.
void purgeFromContainers(ShaderBin** sBins, ShaderOperation* sOp) {
	int found = 0;

	if (sBins == NULL)
		return;
	FOR_EACH_IN_EARRAY(sBins, ShaderBin, sBin) {
		eaFindAndRemove(&(sBin->bin),sOp);
	}
	FOR_EACH_END;
}

void WipeShaderBin(ShaderBin* sBin) {
	eaDestroy(&(sBin->bin));
};

#define INITIAL_NODE_Y_OFFSET 20.0

AUTO_COMMAND;
/// Scan every node, rearrange graph to be more pleasing to the eye instead of one giant clump.
void mated2GraphArrange(void)
{
	ShaderOperation** currentShaderBin = NULL;
	ShaderOperation** nextShaderBin = NULL;
	ShaderOperation* myOutput;
	ShaderBin**	shaderBins = NULL;
	ShaderBin* currentBin = NULL;
	double x = 0.0;
	double y = 0.0;
	double adjustment = 0.0;
	double maxHeight = 0.0;

	if (!mated2Editor.activeDoc) {
		return;		// not even in the material editor, so get out.
	}
	if (!mated2DocActiveShaderGraphIsEditable(mated2Editor.activeDoc)) {
		Alertf("This is not an editable template.");
		return;	// not a template, so get out
	}

	myOutput = materialFindOpByType(mated2Editor.activeDoc->shaderGraph,SOT_SINK);
	eaCopy(&currentShaderBin,&mated2Editor.activeDoc->shaderGraph->operations);
	eaRemove(&currentShaderBin,eaFind(&currentShaderBin,myOutput));
	currentBin = (ShaderBin*) malloc (sizeof(ShaderBin));
	currentBin->bin = NULL;
	eaPush(&(currentBin->bin),myOutput);
	eaPush(&shaderBins,currentBin);
	// A loop that goes through and places each node into its respective column.
	while (currentShaderBin != NULL) {
		S32 cShaderIndex = 0;
		nextShaderBin = NULL;
		currentBin = (ShaderBin*)malloc(sizeof(ShaderBin));
		currentBin->bin = currentShaderBin;
		eaPush(&shaderBins,currentBin);
		while (cShaderIndex < eaSize(&currentShaderBin)) {
			U32 shaderMarker = 0;
			ShaderOperation *op = currentShaderBin[cShaderIndex];
			FOR_EACH_IN_EARRAY_FORWARDS(op->inputs, ShaderInputEdge, op_inputEdge) {
				ShaderOperation* temp = materialFindOpByName(mated2Editor.activeDoc->shaderGraph,op_inputEdge->input_source_name);
				if (temp != NULL) {
					if (eaFind(&nextShaderBin,temp) < 0) {
						S32 shaderCheck = eaFind(&currentShaderBin,temp);
						if (shaderCheck < cShaderIndex && shaderCheck >= 0) {
							shaderMarker++;
						}
						purgeFromContainers(shaderBins,temp);
						eaPush(&nextShaderBin,temp);
					}
				}
			} FOR_EACH_END;
			cShaderIndex -= shaderMarker;
			cShaderIndex++;
		}
		currentShaderBin = nextShaderBin;
	}
	// This sets the heights of each of the columns of nodes and finds the height of the largest column.
	FOR_EACH_IN_EARRAY(shaderBins,ShaderBin,sBin) {
		sBin->height = 0.0;
		FOR_EACH_IN_EARRAY(sBin->bin,ShaderOperation,op) {
			sBin->height += UI_WIDGET( mated2NodeUI( mated2ShaderOpNode( op )))->height + 100;
		}
		FOR_EACH_END;
		if (sBin->height > maxHeight) {
			maxHeight = sBin->height;
		}
	}
	FOR_EACH_END;
	// This list does the actual placement of the nodes according to which column the belong and adjusts for the heights of the nodes above it.
	FOR_EACH_IN_EARRAY(shaderBins,ShaderBin,sBin) {
		y = INITIAL_NODE_Y_OFFSET + ((maxHeight - sBin->height) / 2.0);	// This is to center the column of nodes.
		y = (y > INITIAL_NODE_Y_OFFSET ? y : INITIAL_NODE_Y_OFFSET);
		adjustment = 0.0;
		FOR_EACH_IN_EARRAY(sBin->bin,ShaderOperation,op) {
			double temp;
			ui_WidgetSetPosition( UI_WIDGET( mated2NodeUI( mated2ShaderOpNode( op ))), x, y );
			temp = UI_WIDGET( mated2NodeUI( mated2ShaderOpNode( op )))->width + (eaSize(&currentBin->bin) * 20) + 50;
			if (adjustment < temp) {
				adjustment = temp;
			}
			y += UI_WIDGET( mated2NodeUI( mated2ShaderOpNode( op )))->height + 100;
		}
		FOR_EACH_END;
		x += adjustment;
	}
	FOR_EACH_END;
	eaDestroyEx(&shaderBins,WipeShaderBin);
}

AUTO_COMMAND;
void mated2AnalyzeMaterialUsage( void )
{
	FILE* data = fopen( "c:/MaterialUsage.csv", "w" );

	if( !data ) {
		Alertf( "File: C:\\MaterialUsage.csv -- Could not open file for writing" );
		return;
	}
	
	fprintf( data, "TEMPLATE,#DIRECT,#MAT+FALLBACK,MATERIALS (STAR MEANS DIRECT)\n" );
	{
		int it;
		for( it = 0; it != eaSize( &material_load_info.templates ); ++it ) {
			ShaderTemplate* template = material_load_info.templates[ it ];

			const char* templateName = template->template_name;
			char** directUsers = mated2AllWhoUseMeByName( template->template_name, false );
			char** allUsers = mated2AllWhoUseMeByName( template->template_name, true );

			fprintf( data, "%s,%d,%d,", templateName, eaSize( &directUsers ), eaSize( &allUsers ));
			{
				int fallbackIt;
				for( fallbackIt = 0; fallbackIt != MIN( 5, eaSize( &allUsers )); ++fallbackIt ) {
					char buffer[ MAX_PATH ];
					getFileNameNoExt( buffer, allUsers[ fallbackIt ]);
					fprintf( data, "%s%s ",
							 buffer, (eaFindString( &directUsers, allUsers[ fallbackIt ]) != -1 ? "*" : ""));
				}
				if( fallbackIt < eaSize( &allUsers )) {
					fprintf( data, "..." );
				}
			}
			fprintf( data, "\n" );

			eaDestroy( &allUsers );
			eaDestroy( &directUsers );
		}
	}

	fclose( data );
	Alertf( "Logged to C:\\MaterialUsage.csv" );
}

#include"MaterialEditor2EM_c_ast.c"

#endif
