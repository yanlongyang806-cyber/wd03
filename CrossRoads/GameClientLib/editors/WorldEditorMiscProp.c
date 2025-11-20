#ifndef NO_EDITORS

#include "WorldEditorMiscProp.h"

#include "EditLibUIUtil.h"
#include "EditorManager.h"
#include "MultiEditField.h"
#include "MultiEditFieldContext.h"
#include "ResourceSearch.h"
#include "StringCache.h"
#include "UGCInteriorCommon.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUtil.h"
#include "WorldGrid.h"
#include "groupdbmodify.h"
#include "WorldEditorClientMain.h"
#include "Color.h"

#include "wlGroupPropertyStructs_h_ast.h"
#include "autogen/WorldEditorMiscProp_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

AUTO_ENUM;
typedef enum WleWeldInstances
{
	WLEWI_NOWELD=0,	ENAMES("No Weld")
	WLEWI_WELD,		ENAMES("Weld")
	WLEWI_OPT,		ENAMES("Optimized Weld")
	WLEWI_COUNT,	EIGNORE
} WleWeldInstances;


AUTO_ENUM;
typedef enum WleCollPreset
{
	WLECP_VIS_COL=0,		ENAMES("Visible Full Collision")
	WLECP_INVIS_NOCOL,		ENAMES("Invisible No Collision")
	WLECP_CUSTOM,			ENAMES("Custom")
} WleCollPreset;

AUTO_STRUCT;
typedef struct WlePhysicalPropsUI
{
	WleCollPreset eCollType;		AST(NAME("CollType"))

	AST_STOP

	int iCollTypeCommon;

} WlePhysicalPropsUI;
WlePhysicalPropsUI g_WlePhysicalPropsUI = {0};

WorldPhysicalProperties* wlePhysicalPropsGetDefaultProps()
{
	static WorldPhysicalProperties *pProps = NULL;
	if(!pProps) {
		pProps = StructCreate(parse_WorldPhysicalProperties);
	}
	return pProps;
}

//////////////////////////////////////////////////////////////////////////
// Child Weight Window

typedef struct WleUIEditChildWeightsWin
{
	UIWindow *pWindow;

	EditorObject **ppObjects;
	UIDropSliderTextEntry **entries;

} WleUIEditChildWeightsWin;

static bool wleUIEditChildWeightsDialogCancel(void *unused, WleUIEditChildWeightsWin *ui)
{
	if(ui->pWindow)
		elUIWindowClose(NULL, ui->pWindow);
	eaDestroy(&ui->ppObjects);
	eaDestroy(&ui->entries);
	SAFE_FREE(ui);
	return true;
}

static bool wleUIEditChildWeightsDialogApply(void *unused, WleUIEditChildWeightsWin *ui)
{
	int i;
	EditUndoStack *stack = edObjGetUndoStack();
	EditorObject *object;
	GroupTracker *tracker;
	GroupDef *def;

	if(!stack) {
		Errorf("Could Not Find Undo Stack");
		return false;
	}
	if(eaSize(&ui->ppObjects) != 1 || ui->ppObjects[0]->type->objType != EDTYPE_TRACKER) {
		Errorf("Selection has changed");
		return false;
	}
	object = ui->ppObjects[0];
	tracker = trackerFromTrackerHandle(object->obj);
	def = SAFE_MEMBER(tracker, def);
	if(!tracker || !def || !wleTrackerIsEditable(object->obj, false, false, false)) {
		Errorf("Tracker has changed editable state.");
		return false;
	}
	if(eaSize(&def->children) != eaSize(&ui->entries)) {
		Errorf("Child count has changed.");
		return false;
	}

	wleOpPropsBegin(object->obj);
	{
		for ( i=0; i < eaSize(&def->children); i++ ) {
			GroupChild *child = def->children[i];
			UIDropSliderTextEntry *entry = ui->entries[i];
			child->weight = ui_DropSliderTextEntryGetValue(entry);
		}
	}	
	wleOpPropsUpdate();
	wleOpPropsEnd();
	wleUIEditChildWeightsDialogCancel(NULL, ui);
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.EditChildWeights");
void wleCmdEditChildWeights(void)
{
	int i, y = 5;
	WleUIEditChildWeightsWin *ui;
	EditorObject *object;
	GroupTracker *tracker;
	GroupDef *def;
	UILabel *label;
	UIDropSliderTextEntry *entry;
	UIButton *pButton;

	ui = calloc(1, sizeof(*ui));

	//Get the selected objects
	wleAEGetSelectedObjects(&ui->ppObjects);

	//Make sure we have only one
	if(eaSize(&ui->ppObjects) != 1 || ui->ppObjects[0]->type->objType != EDTYPE_TRACKER) {
		Alertf("You must have one and only one object selected to edit child weights.");
		wleUIEditChildWeightsDialogCancel(NULL, ui);
		return;
	}
	object = ui->ppObjects[0];

	//Make sure there it is editable
	if(!wleTrackerIsEditable(object->obj, false, false, false))
	{
		Alertf("The object must be editable to change child weights.");
		wleUIEditChildWeightsDialogCancel(NULL, ui);
		return;		
	}

	tracker = trackerFromTrackerHandle(object->obj);
	def = SAFE_MEMBER(tracker, def);
	if(!tracker || !def) {
		Alertf("Could not find group def or tracker.");
		wleUIEditChildWeightsDialogCancel(NULL, ui);
		return;				
	}

	//Window
	ui->pWindow = ui_WindowCreate("Editing Child Weights", 100, 100, 160, 55);

	for ( i=0; i < eaSize(&def->children); i++ ) {
		GroupChild *child = def->children[i];
		
		label = ui_LabelCreate(child->name, UI_STEP, y);
		ui_WindowAddChild(ui->pWindow, label);
		entry = ui_DropSliderTextEntryCreate("", 0.01, 10.0, 0.01, 0, 0, 75, 20, 100, 20);
		ui_DropSliderTextEntrySetValue(entry, (child->weight ? child->weight : 1.0f));
		ui_WidgetSetPositionEx(UI_WIDGET(entry), 5, y, 0, 0, UITopRight);
		ui_WindowAddChild(ui->pWindow, entry);
		eaPush(&ui->entries, entry);
		y = ui_WidgetGetNextY(UI_WIDGET(entry)) + UI_HSTEP;
	}

	//Cancel Button
	pButton = ui_ButtonCreate("Cancel", 5, y, wleUIEditChildWeightsDialogCancel, ui);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 5, 5, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 75);
	ui_WindowAddChild(ui->pWindow, pButton);
	//Apply Button
	pButton = ui_ButtonCreate("Apply", 5, y, wleUIEditChildWeightsDialogApply, ui);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 85, y, 0, 0, UITopRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 75);
	ui_WindowAddChild(ui->pWindow, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + 5;
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 85, 5, 0, 0, UIBottomRight);

	//Finalize and open window
	ui_WindowSetCloseCallback(ui->pWindow, wleUIEditChildWeightsDialogCancel, ui);
	ui_WidgetSetDimensions(UI_WIDGET(ui->pWindow), 400, y);
	elUICenterWindow(ui->pWindow);
	ui_WindowSetModal(ui->pWindow, true);
	ui_WindowShow(ui->pWindow);
}

static void wleAEEditChildWeightsCB(void *unused, void *unused2)
{
	wleCmdEditChildWeights();
}

static void wleAEAddChildParameterCB( UIWidget* ignored, UserData ignored2 )
{
	GroupChild* pChild = wleAEGetSingleSelectedGroupChild( true, NULL );
	if( pChild ) {
		eaPush( &pChild->simpleData.params, StructCreate( parse_GroupChildParameter ));
		wleAECallGroupChildFieldChangedCallback( NULL, NULL );
	}
}

static void wleAERemoveChildParameterCB( UIWidget* ignored, UserData rawIndex )
{
	int index = (int)rawIndex;
	GroupChild* pChild = wleAEGetSingleSelectedGroupChild( true, NULL );
	if( pChild && index < eaSize( &pChild->simpleData.params )) {
		StructDestroy( parse_GroupChildParameter, pChild->simpleData.params[ index ]);
		eaRemove( &pChild->simpleData.params, index );
		wleAECallGroupChildFieldChangedCallback( NULL, NULL );
	}
}

//////////////////////////////////////////////////////////////////////////
// Physical Properties

static bool wleAEPhysicalPropExclude(GroupTracker *pTracker)
{
	WorldPhysicalProperties *pProps;
	GroupDef *pDef = pTracker->def;
	WleCollPreset eCollType;
	if(pDef->property_structs.physical_properties.bOnlyAVolume)
		return true;
	if(	wleNeedsEncounterPanels(pDef) && 
		!pDef->property_structs.encounter_hack_properties && 
		!pDef->property_structs.encounter_properties )
		return true;

	pProps = &pDef->property_structs.physical_properties;
	if(	pProps->bVisible &&
		pProps->eGameCollType == WLGCT_NotPermeable &&
		pProps->eCameraCollType == WLCCT_FullCamCollision &&
		pProps->bPhysicalCollision &&
		pProps->bSplatsCollision) {
		eCollType = WLECP_VIS_COL;
	} else if(	!pProps->bVisible &&
				pProps->eGameCollType == WLGCT_FullyPermeable &&
				pProps->eCameraCollType == WLCCT_NoCamCollision &&
				!pProps->bPhysicalCollision &&
				!pProps->bSplatsCollision) {
		eCollType = WLECP_INVIS_NOCOL;
	} else {
		eCollType = WLECP_CUSTOM;
	}

	if(g_WlePhysicalPropsUI.iCollTypeCommon == WL_VAL_UNSET) {
		g_WlePhysicalPropsUI.iCollTypeCommon = eCollType;
	} else if (g_WlePhysicalPropsUI.iCollTypeCommon != eCollType) {
		g_WlePhysicalPropsUI.iCollTypeCommon = WLECP_CUSTOM;
	}

	return false;
}

static void wleAEPhysicalPropChangedCB(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	if(pNewProps->physical_properties.bIsDebris) {
		pNewProps->physical_properties.eGameCollType = WLGCT_NotPermeable;
		pNewProps->physical_properties.eCameraCollType = WLCCT_FullCamCollision;
		pNewProps->physical_properties.bPhysicalCollision = true;
		pNewProps->physical_properties.bSplatsCollision = true;
	}
}

static void wleAEPhysicalPropColTypeChangedCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	WorldPhysicalProperties *pProps = &pData->pTracker->def->property_structs.physical_properties;
	if(g_WlePhysicalPropsUI.eCollType == WLECP_VIS_COL) {
		pProps->bVisible = true;
		pProps->eGameCollType = WLGCT_NotPermeable;
		pProps->eCameraCollType = WLCCT_FullCamCollision;
		pProps->bPhysicalCollision = true;
		pProps->bSplatsCollision = true;
	} else if (g_WlePhysicalPropsUI.eCollType == WLECP_INVIS_NOCOL) {
		pProps->bVisible = false;
		pProps->eGameCollType = WLGCT_FullyPermeable;
		pProps->eCameraCollType = WLCCT_NoCamCollision;
		pProps->bPhysicalCollision = false;
		pProps->bSplatsCollision = false;
	}
}

static void wleAEPhysicalPropColTypeChanged(MEField *pField, bool bFinished, UserData *pUnused)
{
	if(!bFinished || MEContextExists() || g_WlePhysicalPropsUI.eCollType == WLECP_CUSTOM)
		return;
	wleAEApplyToSelection(wleAEPhysicalPropColTypeChangedCB, NULL, NULL);
}


int wleAEPhysicalPropReload(EMPanel *panel, EditorObject *edObj)
{
	WorldPhysicalProperties **ppProps = NULL;
	WorldPhysicalProperties *pProp;
	MEFieldContext *pContext;
	U32 iRetFlags;
	bool bNotDebris;

	g_WlePhysicalPropsUI.iCollTypeCommon = WL_VAL_UNSET;
	ppProps = (WorldPhysicalProperties**)wleAEGetSelectedDataFromPath("Physical", wleAEPhysicalPropExclude, &iRetFlags);
	if(eaSize(&ppProps) == 0)
		return WLE_UI_PANEL_INVALID;

	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_PhysicalProperties", ppProps, ppProps, parse_WorldPhysicalProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	pContext->bDontSortComboEnums = true;
	wleAEAddFieldChangedCallback(pContext, wleAEPhysicalPropChangedCB);
	bNotDebris = (!MEContextFieldDiff("IsDebris") && !pProp->bIsDebris);

	MEContextAddSimple(kMEFieldType_Check,			"IsDebris",				"Debris",					"Debris gets client-side physics.");
	if(iRetFlags & WleAESelectedDataFlags_Model) {
		MEContextAddSimple(kMEFieldType_Check,		"CameraFacing",			"Face Camera",				"Object always faces the camera");
		MEContextAddSimple(kMEFieldType_Check,		"AxisCameraFacing",		"Face Camera (maintain y)",	"Object always faces the camera, but maintains its y axis");
	}
	MEContextAddSimple(kMEFieldType_Check,			"DontCastShadows",		"Don't Cast Shadows",		"Makes this object not cast shadows.");
	MEContextAddSimple(kMEFieldType_Check,			"DontReceiveShadows",	"Don't Receive Shadows",	"Makes this object not receive shadows.");
	
	if(iRetFlags & WleAESelectedDataFlags_ObjLib) {
		MEContextAddSimple(kMEFieldType_Check,		"InstanceOnPlace",		"Instance On Place",		"Automatically makes an instance of the object when you place it.");
	}
	MEContextAddSimple(kMEFieldType_Check,			"NoOcclusion",			"No Occlusion",				"Disables using this group or any of its children as occluders.");
	if(iRetFlags & WleAESelectedDataFlags_Model) {
		MEContextAddSimple(kMEFieldType_Check,		"OcclusionOnly",		"Occlusion Only",			"Makes this object used only for occlusion.");
	}
	MEContextAddSimple(kMEFieldType_Check,			"DoubleSidedOccluder",	"Double Sided Occlusion",	"Makes occlusion geometry for this group or any of its children draw double sided");

	//////////////////////////////////////////////////////////////////////////
	// Visibility and Collision

	if(bNotDebris) {
		MEFieldContext *pCollContext;

		MEContextAddSpacer();

		assert(g_WlePhysicalPropsUI.iCollTypeCommon >= 0);
		g_WlePhysicalPropsUI.eCollType = g_WlePhysicalPropsUI.iCollTypeCommon;

		pCollContext = MEContextPush("CollType", &g_WlePhysicalPropsUI, &g_WlePhysicalPropsUI, parse_WlePhysicalPropsUI);
		pCollContext->cbChanged = wleAEPhysicalPropColTypeChanged;
		MEContextAddEnum(kMEFieldType_Combo, WleCollPresetEnum,			"CollType",				"Visibility and Collision:","Shows you which preset you are using or lets you switch to a different one");
		MEContextPop("CollType");

		MEContextIndentRight();
	}

	MEContextAddSimple(kMEFieldType_Check,								"Visible",				"Visible",					"If true, visible in the game. Otherwise only visible in the editor.");
	MEContextAddSimple(kMEFieldType_Check,								"HeadshotVisible",		"Visible In Preview",		"If true and the object is not already visible, the object is visible in object previews in the editor and in UGC.");

	if(bNotDebris) {
		MEContextAddSimple(kMEFieldType_Check,							"PhysicalCollision",	"Physical Collision",		"If true, movement is stopped by this object.");
		MEContextAddSimple(kMEFieldType_Check,							"SplatsCollision",		"Splats Collision",			"If true, splats and shadows on mid to low end collide.");
		MEContextAddEnum(kMEFieldType_Combo, WLCameraCollisionTypeEnum, "CameraCollType",		"Camera Collision",			"Camera should collide with the object, fade the object out if would collide, or not collide.");
		MEContextAddEnum(kMEFieldType_Combo, WLGameCollisionTypeEnum,	"GameCollType",			"Game Collision",			"Can't target or attack, can target but not attack, or can target and attack.");

		MEContextIndentLeft();
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_PhysicalProperties");

	return WLE_UI_PANEL_OWNED;
}

//////////////////////////////////////////////////////////////////////////
// LOD Properties

static void wleAELODPropAddCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	if(pData->pTracker->def) {
		pData->pTracker->def->property_structs.physical_properties.oLodProps.bShowPanel = 1;
	}
}

void wleAELODPropAdd(UserData *pUnused, UserData *pUnused2)
{
	wleAEApplyToSelection(wleAELODPropAddCB, NULL, NULL);
}

static void wleAELODPropRemoveCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	if(pData->pTracker->def) {
		StructCopy(parse_WorldLODProperties, &wlePhysicalPropsGetDefaultProps()->oLodProps, 
					&pData->pTracker->def->property_structs.physical_properties.oLodProps, 0, 0, 0);
	}
}

void wleAELODPropRemove(UserData *pUnused, UserData *pUnused2)
{
	wleAEApplyToSelection(wleAELODPropRemoveCB, NULL, NULL);
}

int wleAELODPropReload(EMPanel *panel, EditorObject *edObj)
{
	WorldPhysicalProperties **ppProps = NULL;
	WorldPhysicalProperties *pProp;
	MEFieldContext *pContext;
	U32 iRetFlags;
	
	ppProps = (WorldPhysicalProperties**)wleAEGetSelectedDataFromPath("Physical", wleAEPhysicalPropExclude, &iRetFlags);
	if(eaSize(&ppProps) == 0)
		return WLE_UI_PANEL_INVALID;

	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_LODProps", ppProps, ppProps, parse_WorldPhysicalProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	MEContextAddSimple(kMEFieldType_Check,						"LowDetail",			"Low Detail",			"Low Detail objects only draw when High Detail is turned off via user settings.");
	MEContextAddSimple(kMEFieldType_Check,						"HighDetail",			"High Detail",			"High Detail objects can be turned off via user settings, use for things like grass which are not necessary for gameplay.  Also turns off collision.");
	MEContextAddSimple(kMEFieldType_Check,						"HighFillDetail",		"High Fill Detail",		"High Fill Detail objects can be turned off via user settings, use for things like extra detail nebula cards which are not strictly necessary for visual look.  Also turns off collision.");
	MEContextAddSimple(kMEFieldType_Check,						"FadeNode",				"LOD Controller",		"All children use this object's LOD midpoint and radius to determine their LOD level.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0.1, 20, 0.1,	"LodScale",				"LOD Scale",			"All children get this value multiplied against their LOD distances.  LOD scale is multiplicative against all children.");
	MEContextAddSimple(kMEFieldType_Check,						"IgnoreLODOverride",	"Ignore LOD Overrides",	"Parent LOD controllers and LOD overrides (such as BuildingGens) are ignored.  LOD Scale is still respected.");
	MEContextAddEnum(kMEFieldType_Combo,WleClusterOverrideEnum,	"ClusteringOverride",	"Clustering Override",	"Options to override clustering.");

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_LODProps");

	if(StructCompare(parse_WorldLODProperties, &wlePhysicalPropsGetDefaultProps()->oLodProps, &pProp->oLodProps, 0, 0, 0) == 0)
		return WLE_UI_PANEL_UNOWNED;
	return WLE_UI_PANEL_OWNED;
}

//////////////////////////////////////////////////////////////////////////
// UGC Properties

AUTO_STRUCT;
typedef struct UGCIntData {
	int value;					AST( NAME(Value))
} UGCIntData;
extern ParseTable parse_UGCIntData[];
#define TYPE_parse_UGCIntData UGCIntData

AUTO_STRUCT;
typedef struct UGCRoomPreviewData {
	UGCIntData** doorValue;		AST( NAME(DoorValue) )
	UGCIntData** detailValue;	AST( NAME(DetailValue) )
	UGCIntData** populateValue;	AST( NAME(PopulateValue) )
} UGCRoomPreviewData;
extern ParseTable parse_UGCRoomPreviewData[];
#define TYPE_parse_UGCRoomPreviewData UGCRoomPreviewData

UGCRoomPreviewData g_roomPreviewData;

static void wleAEUGCRoomPreviewChangedCB(MEField *pField, bool bFinished, UserData ignored)
{
	EditorObject** objects = NULL;
	
	if( MEContextExists() || !bFinished ) {
		return;
	}

	// MJF: Right now this only works if there's only one thing selected.
	wleAEGetSelectedObjects( &objects );
	if( eaSize( &objects ) != 1 || objects[ 0 ]->type->objType != EDTYPE_TRACKER ) {
		// do nothing yet
	} else {
		GroupTracker* tracker = trackerFromTrackerHandle( objects[ 0 ]->obj );
		UGCRoomInfo* roomInfo = ugcRoomAllocRoomInfo( tracker->def );

		groupClearOverrideParameters();
		if( roomInfo ) {
			int it;
			for( it = 0; it != eaSize( &roomInfo->doors ); ++it ) {
				groupSetOverrideIntParameter( roomInfo->doors[ it ]->astrScopeName,
											  g_roomPreviewData.doorValue[ it ]->value );
			}
			for( it = 0; it != eaSize( &roomInfo->details ); ++it ) {
				groupSetOverrideIntParameter( roomInfo->details[ it ]->astrParameter,
											  g_roomPreviewData.detailValue[ it ]->value );
			}
		}

		groupdbDirtyTracker( tracker, UPDATE_GROUP_PROPERTIES );
		wleOpRefreshUI();
		ugcRoomFreeRoomInfo( roomInfo );
	}
	
	eaDestroy( &objects );
}

int wleAEUGCPropReload(EMPanel *panel, EditorObject *edObj)
{
	static WleAEPropStructData pPropData = {"UGCRoomObjectProperties", parse_WorldUGCRoomObjectProperties};
	WorldUGCRoomObjectProperties** eaProps = NULL;
	WorldUGCRoomObjectProperties* pProp = NULL;
	MEFieldContext *pContext;
	U32 iRetFlags;
	GroupDef* pSingleDef = NULL;

	eaProps = (WorldUGCRoomObjectProperties**)wleAEGetSelectedDataFromPath("UGCRoomObjectProperties", NULL, &iRetFlags);
	pSingleDef = wleAEGetSingleSelectedGroupDef( false, NULL );
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;

	if(eaSize(&eaProps) > 0 && !(iRetFlags & WleAESelectedDataFlags_SomeMissing))
		pProp = eaProps[0];

	pContext = MEContextPushEA("WorldEditor_UGCProps", eaProps, eaProps, parse_WorldUGCRoomObjectProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	if(pProp) {
		UGCRoomInfo* roomInfo = ugcRoomAllocRoomInfo( pSingleDef );

		{
			int oldXDataStart = pContext->iXDataStart;
			pContext->iXDataStart = 0;
			MEContextAddButton( "Remove UGC Room Object Properties", NULL, wleAERemovePropsToSelection, &pPropData, "AddRemove", NULL, "Remove the UGC Room Object properties from this object." );
			pContext->iXDataStart = oldXDataStart;
		}

		MEContextAddEnum( kMEFieldType_Combo, WorldUGCRoomObjectTypeEnum, "Type", "Type", "The type of this room object." );
		if( pProp->eType == UGC_ROOM_OBJECT_DETAIL_SET || pProp->eType == UGC_ROOM_OBJECT_PREPOP_SET || pProp->eType == UGC_ROOM_OBJECT_DETAIL_ENTRY ) {
			wleAEMessageMakeEditorCopy( &pProp->dVisibleName, "VisibleName", "UGC", "Visible name of a UGC object." );
			MEContextAddSimple( kMEFieldType_DisplayMessage, "VisibleName", "Visible Name", "Name shown to UGC authors in combo boxen.");
		}
		
		if( roomInfo ) {
			pContext = MEContextPush( "UGCRoomPreview", &g_roomPreviewData, &g_roomPreviewData, parse_UGCRoomPreviewData );
			pContext->cbChanged = wleAEUGCRoomPreviewChangedCB;

			MEContextAddSpacer();
			MEContextAddLabel( "RoomHeader", "Room Info:", NULL );

			eaSetSizeStruct( &g_roomPreviewData.doorValue, parse_UGCIntData , eaSize( &roomInfo->doors ));
			FOR_EACH_IN_EARRAY_FORWARDS( roomInfo->doors, UGCRoomDoorInfo, door ) {
				int idx = FOR_EACH_IDX( roomInfo->doors, door );
				char buffer[ 256 ];
				sprintf( buffer, "Door%d", idx );
				MEContextPush( allocAddString( buffer ), g_roomPreviewData.doorValue[ idx ], g_roomPreviewData.doorValue[ idx ], parse_UGCIntData );
				MEContextAddSimple( kMEFieldType_TextEntry, "Value", door->astrScopeName, NULL );
				MEContextPop( allocAddString( buffer ));
			} FOR_EACH_END;

			eaSetSizeStruct( &g_roomPreviewData.detailValue, parse_UGCIntData , eaSize( &roomInfo->details ));
			FOR_EACH_IN_EARRAY_FORWARDS( roomInfo->details, UGCRoomDetailDef, detailSet ) {
				int idx = FOR_EACH_IDX( roomInfo->details, detailSet );
				char buffer[ 256 ];
				sprintf( buffer, "Detail%d", idx );
				MEContextPush( allocAddString( buffer ), g_roomPreviewData.detailValue[ idx ], g_roomPreviewData.detailValue[ idx ], parse_UGCIntData );
				MEContextAddSimple( kMEFieldType_TextEntry, "Value", detailSet->astrParameter, NULL );
				MEContextPop( allocAddString( buffer ));
			} FOR_EACH_END;

			eaSetSizeStruct( &g_roomPreviewData.populateValue, parse_UGCIntData, eaSize( &roomInfo->populates ));
			FOR_EACH_IN_EARRAY_FORWARDS( roomInfo->populates, UGCRoomPopulateDef, populateSet ) {
				int idx = FOR_EACH_IDX( roomInfo->populates, populateSet );
				char buffer[ 256 ];
				sprintf( buffer, "Populate%d", idx );
				MEContextPush( allocAddString( buffer ), g_roomPreviewData.populateValue[ idx ], g_roomPreviewData.populateValue[ idx ], parse_UGCIntData );
				MEContextAddSimple( kMEFieldType_TextEntry, "Value", TranslateMessageRef( populateSet->hDisplayName ), NULL );
				MEContextPop( allocAddString( buffer ));
			} FOR_EACH_END;

			MEContextPop( "UGCRoomPreview" );
		}

		ugcRoomFreeRoomInfo( roomInfo );
	} else {
		pContext->iXDataStart = 0;
		MEContextAddButton( "Add UGC Room Object Properties", NULL, wleAEAddPropsToSelection, &pPropData, "AddRemove", NULL, "Remove the UGC Room Object properties from this object." );
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_UGCProps");

	return (pProp ? WLE_UI_PANEL_OWNED : WLE_UI_PANEL_UNOWNED);
}

//////////////////////////////////////////////////////////////////////////
// System Properties

int wleAESystemPropReload(EMPanel *panel, EditorObject *edObj)
{
	static WleAEPropStructData pPropData = {"Physical", parse_WorldPhysicalProperties};
	bool isGroupChildEditable;
	GroupChild* pChild = wleAEGetSingleSelectedGroupChild( false, &isGroupChildEditable );
	WorldPhysicalProperties **ppProps = NULL;
	WorldPhysicalProperties *pProp = NULL;
	MEFieldContext *pContext;
	MEFieldContextEntry *pEntry;
	U32 iRetFlags;

	ppProps = (WorldPhysicalProperties**)wleAEGetSelectedDataFromPath("Physical", NULL, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;
	if(eaSize(&ppProps) == 0)
		return WLE_UI_PANEL_INVALID;
	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_PhysicalProps", ppProps, ppProps, parse_WorldPhysicalProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	pEntry = MEContextAddSimple(kMEFieldType_Check,							"RandomSelect",			"Random Select",			"A random child is selected to be visible.  Good for trees.");
	if(eaSize(&ppProps) == 1) {
		MEContextEntryAddActionButton(pEntry, "Child Weights", NULL, wleAEEditChildWeightsCB, NULL, -1,							"Change the chances each child will be selected");
	}
	MEContextAddSimple(kMEFieldType_Check,									"CivilianGenerator",	"Civilian Generator",		"Civilians spawn from this point and walk around.");
	MEContextAddSimple(kMEFieldType_Check,									"IsDebrisFieldCont",	"Debris Field Container",	"Will search its children for volumes to use as excluders to it's debris field children.");
	MEContextAddSimple(kMEFieldType_Check,									"MassPivot",			"Center of Mass Pivot",		"Marks this object's pivot as the center of mass on throwable objects.");
	MEContextAddSimple(kMEFieldType_Check,									"HandPivot",			"Hand Registration Pivot",	"Marks this object's pivot as the hand registration point on throwable objects");
	if(!MEContextFieldDiff("HandPivot") && pProp->bHandPivot) {
		MEContextAddEnum(kMEFieldType_Combo, WorldCarryAnimationModeEnum,	"CarryAnimationBit",	"Carry Bit",				"The animation bit to set when carrying this object.");
	}
	MEContextAddSimple(kMEFieldType_Check,									"MapSnapHidden",		"Hide in Map Snap",			"Do not draw when taking map photos. Use for things above the usable area.");
	MEContextAddSimple(kMEFieldType_Check,									"MapSnapFade",			"Fade in Map Snap",			"Will fade when taking map photos.  Currently identical to MapSnapHidden.  Use for things under the useable area.");
	MEContextAddSimple(kMEFieldType_Check,									"RoomExcluded",			"Exclude from Rooms",		"Do not include this piece's geometry in room hull calculations.");
	MEContextAddSimple(kMEFieldType_Check,									"ForbiddenPosition",	"Forbidden Pos",			"Creates an error if this position is reachable from any spawn position.");
	MEContextAddEnum  (kMEFieldType_Combo, WleWeldInstancesEnum,			"WeldInstances",		"Weld Instances",			"Creates a single mesh from the children. (Children must all have same model)");
	MEContextAddSimple(kMEFieldType_Check,									"ForceTrunkWind",		"Force Trunk-Type Wind",	"Force this to object behave like it has the !!TRUNKWIND flag set.");

	MEContextStepDown();
	MEContextAddSimple(kMEFieldType_TextEntry,								"ChildSelectParameter",	"Child Select Parameter",	"The name of a parameter that can be set on a parent that determines which child is visible.");
	
	// Technically, the following this are all stored on the parent,
	// so their enabled/disabled state needs to be manages seperately.
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));
	MEContextGetCurrent()->bDisabled = !isGroupChildEditable;

	if( pChild ) {
		int it;
		MEContextAddButton("Add Parameter", NULL, wleAEAddChildParameterCB, NULL, "ParameterAdd", NULL, NULL );
		
		for( it = 0; it != eaSize( &pChild->simpleData.params ); ++it ) {
			char ctxName[ 256 ];
			sprintf( ctxName, "WorldEditor_ChildParam%d", it );
			pContext = MEContextPush( ctxName, pChild->simpleData.params[ it ], pChild->simpleData.params[ it ], parse_GroupChildParameter );
			wleAEAddGroupChildFieldChangedCallback( pContext, NULL );
			
			pEntry = MEContextAddLabel( "Header", "Parameter:", NULL );
			MEContextEntryAddActionButton( pEntry, "X", NULL, wleAERemoveChildParameterCB, (UserData)it, 20, NULL );
			
			MEContextAddSimple( kMEFieldType_TextEntry, "ParameterName", "Name", "Name of the parameter" );
			MEContextAddSimple( kMEFieldType_TextEntry, "IntValue", "Int Value", "Integer value for the parameter" );
			MEContextAddSimple( kMEFieldType_TextEntry, "StringValue", "String Value", "String value for the parameter" );
			MEContextAddSimple( kMEFieldType_TextEntry, "InheritValue", "Inherit Value", "Value to inherit from" );
			MEContextAddSeparator( "---" );
			MEContextPop( ctxName );
		}
	}
	emPanelSetHeight(panel, pContext->iYPos);

	MEContextPop("WorldEditor_PhysicalProps");

	return WLE_UI_PANEL_OWNED;
}

void wleAEPathNodeConnectionIDColumnCB( UIList* pList, UIListColumn* pColumn, int iRow, UserData ignored, char** out_pestr )
{
	GroupDef* pSingleDef = wleAEGetSingleSelectedGroupDef( false, NULL );

	if( !pSingleDef || !pSingleDef->property_structs.path_node_properties ) {
		estrPrintf( out_pestr, "<error>" );
	} else {
		WorldPathEdge* connection = eaGet( &pSingleDef->property_structs.path_node_properties->eaConnections, iRow );
		if( !connection ) {
			estrPrintf( out_pestr, "<error>" );
		} else {
			estrPrintf( out_pestr, "%d", connection->uOther );
		}
	}
}

void wleAEPathNodeConnectionChangedCB( UIList* pList, UserData ignored )
{
	GroupDef* pSingleDef = wleAEGetSingleSelectedGroupDef( false, NULL );
	WorldPathEdge* connection = ui_ListGetSelectedObject( pList );
	if( pSingleDef && connection ) {
		wlePathNodeLinkSetSelected( pSingleDef->name_uid, connection->uOther );
	} else {
		wlePathNodeLinkSetSelected( 0, 0 );
	}
}

void wleAEPathNodeAddLink( UIButton* ignored, UserData ignored2 )
{
	EditorObject** eaSelection = NULL;
	GroupDef** eaSelectedDefs = NULL;

	edObjSelectionGetAll( &eaSelection );
	FOR_EACH_IN_EARRAY_FORWARDS( eaSelection, EditorObject, selection ) {
		if( selection->type->objType == EDTYPE_TRACKER ) {
			GroupTracker* tracker = trackerFromTrackerHandle( selection->obj );
			if( tracker ) {
				eaPush( &eaSelectedDefs, tracker->def );
			}
		} else {
			eaClear( &eaSelectedDefs );
			break;
		}
	} FOR_EACH_END;

	if(   eaSize( &eaSelectedDefs ) != 2 || !eaSelectedDefs[ 0 ]->property_structs.path_node_properties
		  || !eaSelectedDefs[ 1 ]->property_structs.path_node_properties ) {
		ui_ModalDialog( "Error", "To add a link, select two path nodes, then click this button", ColorBlack, UIOk );
		eaDestroy( &eaSelection );
		eaDestroy( &eaSelectedDefs );
		return;
	}
	// Verify there isn't already a link -- only one side is needed
	// because links are bidirectional.
	FOR_EACH_IN_EARRAY( eaSelectedDefs[ 0 ]->property_structs.path_node_properties->eaConnections, WorldPathEdge, connection ) {
		if( connection->uOther == eaSelectedDefs[ 1 ]->name_uid ) {
			ui_ModalDialog( "Error", "There already is a link between these nodes.", ColorBlack, UIOk );
			eaDestroy( &eaSelection );
			eaDestroy( &eaSelectedDefs );
			return;
		}
	} FOR_EACH_END;
	
	wleOpTransactionBegin();
	journalDef( eaSelectedDefs[ 0 ]);
	journalDef( eaSelectedDefs[ 1 ]);

	// Add the connection!
	{
		WorldPathEdge* toSelected1 = StructCreate( parse_WorldPathEdge );
		WorldPathEdge* toSelected0 = StructCreate( parse_WorldPathEdge );
		
		toSelected1->uOther = eaSelectedDefs[ 1 ]->name_uid;
		toSelected0->uOther = eaSelectedDefs[ 0 ]->name_uid;
		eaPush( &eaSelectedDefs[ 0 ]->property_structs.path_node_properties->eaConnections, toSelected1 );
		eaPush( &eaSelectedDefs[ 1 ]->property_structs.path_node_properties->eaConnections, toSelected0 );
	}
	groupdbDirtyDef( eaSelectedDefs[ 0 ], UPDATE_GROUP_PROPERTIES );
	groupdbDirtyDef( eaSelectedDefs[ 1 ], UPDATE_GROUP_PROPERTIES );
		
	wleOpTransactionEnd();

	eaDestroy( &eaSelection );
	eaDestroy( &eaSelectedDefs );

}

void wleAEPathNodeDeleteLink( UIButton* pButton, UIList* pList )
{
	GroupDef* pSingleDef = wleAEGetSingleSelectedGroupDef( false, NULL );
	GroupDef* pOtherDef = NULL;

	if( pSingleDef ) { 
		WorldPathEdge* connection = ui_ListGetSelectedObject( pList );
		if( connection ) {
			pOtherDef = groupLibFindGroupDef( pSingleDef->def_lib, connection->uOther, false );
		}
	}

	if( pSingleDef && pOtherDef ) {
		WorldPathNodeProperties* pSingleDefPath = pSingleDef->property_structs.path_node_properties;
		WorldPathNodeProperties* pOtherDefPath = pOtherDef->property_structs.path_node_properties;

		if( pSingleDefPath && pOtherDefPath ) {
			wleOpTransactionBegin();
			journalDef( pSingleDef );
			journalDef( pOtherDef );
			
			FOR_EACH_IN_EARRAY( pSingleDefPath->eaConnections, WorldPathEdge, connection ) {
				if( connection->uOther == pOtherDef->name_uid ) {
					StructDestroy( parse_WorldPathEdge, connection );
					eaRemove( &pSingleDefPath->eaConnections, FOR_EACH_IDX( pSingleDefPath->eaConnections, connection ));
				}
			} FOR_EACH_END;

			FOR_EACH_IN_EARRAY( pOtherDefPath->eaConnections, WorldPathEdge, connection ) {
				if( connection->uOther == pSingleDef->name_uid ) {
					StructDestroy( parse_WorldPathEdge, connection );
					eaRemove( &pOtherDefPath->eaConnections, FOR_EACH_IDX( pOtherDefPath->eaConnections, connection ));
				}
			} FOR_EACH_END;
			groupdbDirtyDef( pSingleDef, UPDATE_GROUP_PROPERTIES );
			groupdbDirtyDef( pOtherDef, UPDATE_GROUP_PROPERTIES );
			
			wleOpTransactionEnd();
			
			wleOpRefreshUI();
		}
	}
}

//
// Path nodes
int wleAEPathNodeReload(EMPanel *panel, EditorObject *edObj)
{
	static WleAEPropStructData pPropData = {"PathNode", parse_WorldPathNodeProperties};
	WorldPathNodeProperties** eaProps = NULL;
	WorldPathNodeProperties* pProp = NULL;
	MEFieldContext *pContext;
	U32 iRetFlags;
	GroupDef* pSingleDef = NULL;

	eaProps = (WorldPathNodeProperties**)wleAEGetSelectedDataFromPath("PathNode", NULL, &iRetFlags);
	pSingleDef = wleAEGetSingleSelectedGroupDef( false, NULL );
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;

	if(eaSize(&eaProps) == 0)
		return WLE_UI_PANEL_UNOWNED;

	if(eaSize(&eaProps) > 0 && !(iRetFlags & WleAESelectedDataFlags_SomeMissing))
		pProp = eaProps[0];

	pContext = MEContextPushEA("WorldEditor_PathNode", eaProps, eaProps, parse_WorldPathNodeProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	//MEContextAddSimple(kMEFieldType_Check,	"CanBeObstructed",	"Can be obstructed",	"If true, this node is marked if collisions are detected on edges.");
	//MEContextAddSimple(kMEFieldType_Check,	"IsSecret",	"Is Secret",	"If true, this node is ignored if collisions are detected on edges.");
	MEContextAddSimple(kMEFieldType_TextEntry, "TeleportID", "Teleport ID", "All nodes with a nonzero value will be connected to other nodes with the same value.");
	MEContextAddSimple(kMEFieldType_BooleanCombo, "UGC", "UGC Node", "If set, this node is not actually used for golden-pathing.  It is used by UGC to create its own UGC nodes.");

	if( pSingleDef && pSingleDef->property_structs.path_node_properties ) {
		MEFieldContextEntry* entry = MEContextAddCustom( "Connections" );
		WorldPathNodeProperties* pathProps = MEContextAllocStruct( "Connections", parse_WorldPathNodeProperties, true );
		UIList* entryList = (UIList*)ENTRY_WIDGET( entry );
		if( !entryList ) {
			entryList = ui_ListCreate( NULL, NULL, 15 );
			ENTRY_WIDGET( entry ) = UI_WIDGET( entryList );
			ui_ListAppendColumn( entryList, ui_ListColumnCreateText( "Connections", wleAEPathNodeConnectionIDColumnCB, NULL ));
		}
		ui_WidgetRemoveFromGroup( UI_WIDGET( entryList ));
		ui_WidgetAddChild( pContext->pUIContainer, UI_WIDGET( entryList ));
		
		StructCopy( parse_WorldPathNodeProperties, pSingleDef->property_structs.path_node_properties, pathProps, 0, 0, 0 );
		ui_ListSetModel( entryList, NULL, &pathProps->eaConnections );
		ui_ListSetSelectedCallback( entryList, wleAEPathNodeConnectionChangedCB, NULL );
		
		// Call the callback to update the selection
		wleAEPathNodeConnectionChangedCB( entryList, NULL );
			
		ui_WidgetSetPosition( UI_WIDGET( entryList ), pContext->iXPos, pContext->iYPos );
		ui_WidgetSetDimensionsEx( UI_WIDGET( entryList ), 1, 150, UIUnitPercentage, UIUnitFixed );
		pContext->iYPos += 150;

		{
			MEFieldContextEntry* addEntry = MEContextAddButton( "Add Link", NULL, wleAEPathNodeAddLink, entryList, "ConnectionAdd", NULL, NULL );
			MEFieldContextEntry* deleteEntry = MEContextAddButton( "Delete Link", NULL, wleAEPathNodeDeleteLink, entryList, "ConnectionDelete", NULL, NULL );
			MEContextStepBackUp();
			MEContextStepBackUp();

			ui_WidgetSetPositionEx( UI_WIDGET( ENTRY_BUTTON( addEntry )), 0, pContext->iYPos, 0, 0, UITopLeft );
			ui_WidgetSetWidthEx( UI_WIDGET( ENTRY_BUTTON( addEntry )), 0.5, UIUnitPercentage );
			ui_WidgetSetPositionEx( UI_WIDGET( ENTRY_BUTTON( deleteEntry )), 0, pContext->iYPos, 0, 0, UITopRight );
			ui_WidgetSetWidthEx( UI_WIDGET( ENTRY_BUTTON( deleteEntry )), 0.5, UIUnitPercentage );

			MEContextStepDown();
		}
	}

	emPanelSetHeight(panel, pContext->iYPos);

	MEContextPop("WorldEditor_PathNode");

	return WLE_UI_PANEL_OWNED;
}

#include "autogen/WorldEditorMiscProp_c_ast.c"
#endif
