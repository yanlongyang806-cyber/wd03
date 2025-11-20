#ifndef NO_EDITORS

#include "WorldEditorAutoPlacementAttributes.h"

#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorAttributes.h"
#include "WorldEditorOperations.h"
#include "EditorObject.h"
#include "EditorManager.h"
#include "WorldGrid.h"
#include "EditLibUIUtil.h"
#include "WorldEditorUtil.h"

#include "tokenStore.h"
#include "estring.h"
#include "crypt.h"
#include "Expression.h"

#include "autoPlacementCommon.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* DEFINITIONS
********************/

// #define USE_ADD_REMOVE_BUTTONS

#define WLE_AE_AUTOPLACE_ALIGN_WIDTH 75
#define WLE_AE_AUTOPLACE_INDENT 20
#define WLE_AE_AUTOPLACE_NUM_ENTRY_WIDTH 100
#define WLE_AE_AUTOPLACE_ALIGN_WIDE 100

#define wleAEAutoPlacementSetupParam(param, propertyName)\
	param.left_pad = 20;\
	param.entry_align = WLE_AE_AUTOPLACE_ALIGN_WIDTH;\
	param.property_name = propertyName

#define wleAEAutoPlacementSetupStructParam(param, defStructMember, pti, fieldName)\
	param.entry_align = WLE_AE_AUTOPLACE_ALIGN_WIDTH;\
	param.left_pad = WLE_AE_AUTOPLACE_INDENT;\
	param.struct_offset = offsetof(GroupDef, defStructMember);\
	param.struct_pti = pti;\
	param.struct_fieldname = fieldName

#define wleAEAutoPlaceUpdateInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = trackerFromTrackerHandle(obj->obj);\
	def = tracker ? tracker->def : NULL

#define wleAEAutoPlacerApplyInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = wleOpPropsBegin(obj->obj);\
	if (!tracker)\
		return;\
	def = tracker ? tracker->def : NULL;\
	if (!def)\
	{\
		wleOpPropsEnd();\
		return;\
	}\

#define wleAEAutoPlacerApplyInitAt(i)\
	GroupTracker *tracker;\
	GroupDef *def;\
	assert(objs[i]->type->objType == EDTYPE_TRACKER);\
	tracker = wleOpPropsBegin(objs[i]->obj);\
	if (!tracker)\
		continue;\
	def = tracker ? tracker->def : NULL;\
	if (!def)\
	{\
		wleOpPropsEndNoUIUpdate();\
		continue;\
	}\

#define RELEASE_TRACKER_HANDLE(x)	if (x) { free(x); (x) = NULL; }
//#define RELEASE_TRACKER_HANDLE(x)	(((x)!=NULL?free(x):0),(x)=NULL)


typedef enum eAPParamType
{
	eAPParamType_NULL = -1,
	eAPParamType_WEIGHT,
	eAPParamType_REQUIRED_EXPR,
	eAPParamType_FITNESS_EXPR,
	eAPParamType_SLOPESNAP,
	eAPParamType_COUNT
} eAPParamType;

typedef enum eSubPropertyDisplay
{
	eSubPropertyDisplay_NONE = 0,
	eSubPropertyDisplay_GROUP,
	eSubPropertyDisplay_OBJECT
} eSubPropertyDisplay;

#define MAX_AUTO_PLACE_PROPERTY_SETS	4

typedef struct UIAutoPlacementSetProperties
{
	// Auto-Placer
	WleAEParamFloat proximity;
	WleAEParamFloat proximity_variance;

	// parameters
	WleAEParamBool  snapToSlope;
	WleAEParamFloat weight;
	WleAEParamExpression required_expr;
	WleAEParamExpression fitness_expr;

	U32		treeID;
	eSubPropertyDisplay ePropertyDisplay;

} UIAutoPlacementSetProperties;

typedef struct WleAEAutoPlacementUI
{
	EMPanel*			panel;
	UIRebuildableTree*	autoWidget;
	UIScrollArea*		scrollArea;
	UIMenu*				rclickMenu;

	UIWindow*		pTempWindow;
	UITextEntry*	pTempTextEntry;
	char*			pTempTrackerHandle;
	

	eAPParamType	eParamType_Weight;
	eAPParamType	eParamType_RequiredExpr;
	eAPParamType	eParamType_FitnessExpr;
	eAPParamType	eParamType_SlopeSnap;

	WleAEParamBool		isAutoPlacer;
	WleAEParamBool		isObjectOverride;
	UIAutoWidgetParams	genericWidgetParams;

	
	UIAutoPlacementSetProperties	aAutoPlacementSets[MAX_AUTO_PLACE_PROPERTY_SETS];

} WleAEAutoPlacementUI;

/********************
* GLOBALS
********************/
static WleAEAutoPlacementUI wleAEGlobalAutoPlacementUI;

static const char *sAUTO_PLACER_TREE_NAME = "AutoPlacerTree";
static const char *sOVERRIDE_LIST_NAME = "autoPlacerOverrideList";

#define TREENAME_BUFFER_SIZE 32

void addAutoPlacementSet(AutoPlacementSet *pAutoPlaceSet, UIAutoPlacementSetProperties *pUIProperty, U32 treeIdx);
void createAutoPlacementSetUI(UIAutoPlacementSetProperties *pUIProperty);



/********************
* MAIN
********************/

__forceinline static UITree* getUITreeByIndex(U32 treeIdx)
{
	char szFullTreeName[1024];
	sprintf(szFullTreeName, "set%d/%s", treeIdx, sAUTO_PLACER_TREE_NAME);

	return (UITree*)ui_RebuildableTreeGetWidgetByName(wleAEGlobalAutoPlacementUI.autoWidget, szFullTreeName);
}

#define getCurrentUITree() getUITreeByIndex(0)

__forceinline static UITreeNode* getUITreeNodeCurrentSelectedNode(U32 treeIdx)
{
	UITree *pTree = getUITreeByIndex(treeIdx);
	return (pTree) ? ui_TreeGetSelected(pTree) : NULL;
}

#define getCurrentSelectedUITreeNode() getUITreeNodeCurrentSelectedNode(treeIdx)

// ------------------------------------------------------------------------------------------------------------
UITree *getAutoPlaceSetUITree(AutoPlacementSet *pAutoPlaceSet)
{
	S32 i;
	UITree *pUITree = NULL;
	EditorObject **objects = NULL;

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		GroupTracker *handle = trackerFromTrackerHandle(objects[i]->obj);
		if (handle->def && handle->def->property_structs.auto_placement_properties)
		{
			S32 setIdx = eaFind(&handle->def->property_structs.auto_placement_properties->auto_place_set, pAutoPlaceSet);
			if (setIdx != -1)
			{
				pUITree = getUITreeByIndex(setIdx);
			}
		}
	}

	eaDestroy(&objects);
	return pUITree;
}


// ------------------------------------------------------------------------------------------------------------
static void wleAEAppearanceDisplayUIDName(UIList *list, UIListColumn *col, int row, UserData unused, char **output)
{
	AutoPlacementOverride *pDef = eaGet(list->peaModel, row);

	estrPrintf(output, "%s", pDef->resource_name);
}

// ------------------------------------------------------------------------------------------------------------
// AutoPlacement Structure Creations
// ------------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------
void createAddNewAutoPlacementSet(WorldAutoPlacementProperties *pProperties, const char *pszName)
{
	AutoPlacementSet *pAutoPlaceSet = StructCreate(parse_AutoPlacementSet);

	// set up the default properties
	pAutoPlaceSet->proximity = 50.0f;
	pAutoPlaceSet->variance = 25.0f;
	pAutoPlaceSet->set_name = estrCreateFromStr(pszName);

	eaPush(&pProperties->auto_place_set, pAutoPlaceSet);
}



// ------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------------------
static void getNameCancel(UITextEntry *theWidget, UserData nil)
{
	wleAEGlobalAutoPlacementUI.pTempTextEntry = NULL;
	RELEASE_TRACKER_HANDLE(wleAEGlobalAutoPlacementUI.pTempTrackerHandle)
	elUIWindowClose(NULL, wleAEGlobalAutoPlacementUI.pTempWindow);
	wleAEGlobalAutoPlacementUI.pTempWindow = NULL;
}


// ------------------------------------------------------------------------------------------------------------
static bool getNameWindowClosed(UIAnyWidget *uiWindow, UserData nil)
{
	getNameCancel(NULL, NULL);
	return true;
}


// ------------------------------------------------------------------------------------------------------------
// prompts the user to enter a name for the new group
static void promptUserForName(SA_PARAM_OP_VALID UIActivationFunc enterOkF, UserData enterOkData )
{
	UITextEntry *pTextEntry;
	UIButton *pButton;


	UIWindow *pWindow = ui_WindowCreate("New Group Name", 0, 0, 300, 0);

	ui_WindowSetCloseCallback(pWindow, getNameWindowClosed, NULL);

	// create the text entry 
	pTextEntry = ui_TextEntryCreate("", 0, 0);
	pTextEntry->widget.offsetFrom = UITopLeft;
	pTextEntry->widget.width = 1;
	pTextEntry->widget.widthUnit = UIUnitPercentage;
	pTextEntry->widget.leftPad = 5;
	pTextEntry->widget.rightPad = 5;
	ui_TextEntrySetSelectOnFocus(pTextEntry, true);
	ui_TextEntrySetEnterCallback(pTextEntry, enterOkF, enterOkData);
	ui_WindowAddChild(pWindow, pTextEntry);

	pButton = elUIAddCancelOkButtons(pWindow, getNameCancel, NULL, enterOkF, enterOkData);

	pWindow->widget.height = elUINextY(pButton) + elUINextY(pTextEntry) + 5;

	elUICenterWindow(pWindow);
	ui_SetFocus(UI_WIDGET(pTextEntry));
	ui_WindowSetModal(pWindow, true);
	ui_WindowShow(pWindow);

	wleAEGlobalAutoPlacementUI.pTempTextEntry = pTextEntry;
	wleAEGlobalAutoPlacementUI.pTempWindow = pWindow;
}	


// ------------------------------------------------------------------------------------------------------------
// callback when the objects have been picked
static bool autoPlacerObjectPicked( EMPicker* picker, EMPickerSelection** selections, AutoPlacementSet *pAutoPlaceSet )
{
	int i, x;
	int bDuplicate;
	UITree* pUITree;
	UITreeNode *pGroupTreeNode;
	AutoPlacementGroup *pAutoPlacementGroup;

	if( eaSize( &selections ) == 0 ) 
	{
		return false;
	}
	
	pUITree = getAutoPlaceSetUITree(pAutoPlaceSet);
	if (! pUITree)
	{
		// throw a warning that the UI list has gone away 
		// (most likely the volume param auto placer was unchecked.)
		return true;
	}

	// get the current GroupTree node 
	pGroupTreeNode = ui_TreeGetSelected(pUITree);
	if (! pGroupTreeNode || pGroupTreeNode->table != parse_AutoPlacementGroup)
	{
		ui_DialogPopup("No Group Selected", "A group in the Auto-Placement UITree must be selected in order to add objects to it." );
		return true;
	}
	assert(pGroupTreeNode->contents);
	
	pAutoPlacementGroup = (AutoPlacementGroup*)pGroupTreeNode->contents;

	// for each selection, add it to the list. 
	// make sure we do not add duplicates
	for (i = 0; i < eaSize(&selections); i++)
	{
		EMPickerSelection *pSelection = selections[i];
		ResourceInfo* entry = (ResourceInfo*)pSelection->data;

		bDuplicate = false;
		// check for duplicate resource ID
		for (x = 0; x < eaSize(&pAutoPlacementGroup->auto_place_objects); x++)
		{
			AutoPlacementObject *pDef = pAutoPlacementGroup->auto_place_objects[x];
			if (pDef->resource_id == entry->resourceID)
			{
				bDuplicate = true;
				break;
			}
		}

		if (bDuplicate == true)
		{
			continue;
		}

		// add it to our list 
		{
			AutoPlacementObject	*pAutoPlaceObject;
			char *pFileName = strchr(pSelection->doc_name, ',');
			if (! pFileName)
			{	// bad doc_name, probably spit out a warning here
				continue;
			}

			pAutoPlaceObject = StructCreate(parse_AutoPlacementObject);

			pAutoPlaceObject->weight = 1.0f;
			pAutoPlaceObject->resource_id = entry->resourceID;
			// force a termination of the string so I can copy just the name over
			*pFileName = 0;
			pAutoPlaceObject->resource_name = estrCreateFromStr(pSelection->doc_name);
			// put the ',' character back
			*pFileName = ',';

			eaPush(&pAutoPlacementGroup->auto_place_objects, pAutoPlaceObject);
		}

	}

	// finally refresh the UITree so we can see the update to the tree
	ui_TreeRefresh(pUITree);

	return true;
}



// ------------------------------------------------------------------------------------------------------------
// callback from the UI when the 'Add Object' button was picked
static void wleAEAutoPlacerAddObject(UIAnyWidget *pButton, AutoPlacementSet *pAutoPlaceSet)
{
	EMPicker* picker;
	UITreeNode *pTreeNode;
	UITree *pUITree;

	pUITree = getAutoPlaceSetUITree(pAutoPlaceSet);
	if (! pUITree)
	{
		// throw an error that the UI list has gone away 
		// this shouldn't ever happen?
		return;
	}

	// validate that there is a currently selected group in the tree
	pTreeNode = ui_TreeGetSelected(pUITree);
	if (! pTreeNode || pTreeNode->table != parse_AutoPlacementGroup)
	{
		ui_DialogPopup("No Group Selected", "A group in the Auto-Placement UITree must be selected in order to add objects to it." );
		return;
	}

	picker = emPickerGetByName( "Object Picker" );
	assert( picker );
	emPickerShow( picker, "Select", true, autoPlacerObjectPicked, pAutoPlaceSet );
}


// ------------------------------------------------------------------------------------------------------------


static void wleAEAutoPlacerRemoveObject(UIAnyWidget *pButton, AutoPlacementSet *pAutoPlaceSet)
{
	UITreeNode *pTreeNode;
	UITree *pUITree;

	pUITree = getAutoPlaceSetUITree(pAutoPlaceSet);
	if (! pUITree)
	{
		return;
	}

	// validate that there is a currently selected placed-object in the tree
	pTreeNode = ui_TreeGetSelected(pUITree);
	if (! pTreeNode || pTreeNode->table != parse_AutoPlacementObject)
	{
		// placed object selected in the tree
		ui_DialogPopup("No Object Selected", "You must select an object in the Auto-Placement UITree to be removed." );
		return;
	}
	
	
	assert(pAutoPlaceSet == pTreeNode->tree->root.contents);
	
	{
		S32 i, idx;
		for (i = 0; i < eaSize(&pAutoPlaceSet->auto_place_group); i++)
		{
			// found the group to destroy, remove it from the list then free the memory
			AutoPlacementGroup *pAutoPlaceGroup = pAutoPlaceSet->auto_place_group[i];
			
			idx = eaFind(&pAutoPlaceGroup->auto_place_objects, pTreeNode->contents);
			if (idx != -1)
			{
				AutoPlacementObject *pRem = eaRemove(&pAutoPlaceGroup->auto_place_objects, idx);
				StructDestroySafe(parse_AutoPlacementObject, &pRem);
				break;
			}
			
		}
	}
	
	ui_TreeRefresh(pUITree);
	wleAERefresh();
}

// ------------------------------------------------------------------------------------------------------------
// Auto-Place GROUP functions
// ------------------------------------------------------------------------------------------------------------
static void wleAEAutoPlacerRemoveGroup(UIAnyWidget *pButton, AutoPlacementSet *pAutoPlaceSet)
{
	UITreeNode *pTreeNode;
	UITree *pUITree;

	pUITree = getAutoPlaceSetUITree(pAutoPlaceSet);
	if (! pUITree)
	{
		return;
	}

	// validate that there is a currently selected group in the tree
	pTreeNode = ui_TreeGetSelected(pUITree);
	if (! pTreeNode || pTreeNode->table != parse_AutoPlacementGroup)
	{
		// no group selected in the tree
		ui_DialogPopup("No Group Selected", "You must select a group in the Auto-Placement UITree to be removed." );
		return;
	}
	
	assert(pAutoPlaceSet == pTreeNode->tree->root.contents);
	
	{
		S32 idx = eaFind(&pAutoPlaceSet->auto_place_group, pTreeNode->contents);
		if (idx != -1)
		{
			// found the group to destroy, remove it from the list then free the memory
			AutoPlacementGroup *pRem; 
			pRem = eaRemove(&pAutoPlaceSet->auto_place_group, idx);

			StructDestroySafe(parse_AutoPlacementGroup, &pRem);
		}
	}

	ui_TreeRefresh(pUITree);
	wleAERefresh();
}


// ------------------------------------------------------------------------------------------------------------
static void groupNameEntryEnter(UIAnyWidget *theWidget, AutoPlacementSet *pAutoPlaceSet)
{
	// get the name from the text entry field
	// create the new group
	if (wleAEGlobalAutoPlacementUI.pTempTextEntry)
	{
		GroupTracker *pTracker;
		const char *name = ui_TextEntryGetText(wleAEGlobalAutoPlacementUI.pTempTextEntry);
		if (strlen(name) == 0)
		{
			getNameCancel(NULL, NULL);
			ui_DialogPopup("Name Entry Error", "The name must not be empty." );
			return;
		}

		pTracker = trackerFromHandleString(wleAEGlobalAutoPlacementUI.pTempTrackerHandle);
		if (pTracker)
		{
			int i;
			bool bUnqiue = true;
			S32 setIdx;

			setIdx = eaFind(&pTracker->def->property_structs.auto_placement_properties->auto_place_set, pAutoPlaceSet);
			if (setIdx == -1)
			{
				getNameCancel(NULL, NULL);
				ui_DialogPopup("Error With AutoPlacementSet", "The autoplacement set does not match the editor object." );
				return;
			}
			
			// check for unique name
			for (i = 0; i < eaSize(&pAutoPlaceSet->auto_place_group); i++)
			{
				AutoPlacementGroup *pGroup = pAutoPlaceSet->auto_place_group[i];
				if(stricmp(pGroup->group_name, name) == 0)
				{
					bUnqiue = false;
					break;
				}
			}

			if (!bUnqiue)
			{
				getNameCancel(NULL, NULL);
				// prompt the user that the name chosen was not unique and they must try again
				// note: case insensitive
				ui_DialogPopup("Invalid Group Name", "The group name must be unique. The name is case insensitive." );
				return;
			}

			// create the object
			{
				AutoPlacementGroup *pAutoPlacementGroup  = StructCreate(parse_AutoPlacementGroup);

				pAutoPlacementGroup->group_name = estrCreateFromStr(name);
				pAutoPlacementGroup->weight = 1.0f;

				eaPush(&pAutoPlaceSet->auto_place_group, pAutoPlacementGroup);
			}
		
			// refresh the UI tree so we can see the changes
			{
				UITree *pUITree = getUITreeByIndex(setIdx);
				if (pUITree)
				{
					ui_TreeRefresh(pUITree);
				}
			}
		}
	}
	
	getNameCancel(NULL, NULL);
}


// ------------------------------------------------------------------------------------------------------------
static void wleAEAutoPlacerAddGroup(UIAnyWidget *pButton, AutoPlacementSet *pAutoPlaceSet)
{
	EditorObject **objects = NULL;
	S32 i;

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->obj)
		{
			GroupTracker *handle = trackerFromTrackerHandle(objects[i]->obj);
			if (handle->def && handle->def->property_structs.auto_placement_properties)
			{
				RELEASE_TRACKER_HANDLE(wleAEGlobalAutoPlacementUI.pTempTrackerHandle)
				wleAEGlobalAutoPlacementUI.pTempTrackerHandle = handleStringFromTracker(handle);
				
				promptUserForName(groupNameEntryEnter, pAutoPlaceSet);
				// only allowing for the first selected object that has auto_placement_properties
				// to be handled
				break;
			}
		}
	}

	eaDestroy(&objects);
}

// ------------------------------------------------------------------------------------------------------------
// Set Button callbacks
// ------------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------
static void autoPlaceSetNameEntryEnter(UIAnyWidget *theWidget, UserData nil)
{
	// get the name from the text entry field
	// create the new group
	if (wleAEGlobalAutoPlacementUI.pTempTextEntry)
	{
		GroupTracker *pTracker;

		const char *name = ui_TextEntryGetText(wleAEGlobalAutoPlacementUI.pTempTextEntry);
		if (strlen(name) == 0)
		{
			getNameCancel(NULL, NULL);
			ui_DialogPopup("Name Entry Error", "The name must not be empty." );
			return;
		}

		pTracker = trackerFromHandleString(wleAEGlobalAutoPlacementUI.pTempTrackerHandle);
		if (pTracker)
		{
			int i;
			bool bUnqiue = true;
			WorldAutoPlacementProperties *pProperties;
			
			pProperties = pTracker->def->property_structs.auto_placement_properties;
			
			// check for unique name
			for (i = 0; i < eaSize(&pProperties->auto_place_set); i++)
			{
				AutoPlacementSet *pSet = pProperties->auto_place_set[i];
				if(stricmp(pSet->set_name, name) == 0)
				{
					bUnqiue = false;
					break;
				}
			}

			if (!bUnqiue)
			{
				getNameCancel(NULL, NULL);
				// prompt the user that the name chosen was not unique and they must try again
				// note: case insensitive
				ui_DialogPopup("Invalid Set Name", "The set name must be unique. The name is case insensitive." );
				return;
			}

			createAddNewAutoPlacementSet(pProperties, name);
			wleAERefresh();
		}
	}

	getNameCancel(NULL, NULL);
}

static void wleAEAutoPlacerAddSet(UIAnyWidget *pButton, UserData nil)
{
	EditorObject **objects = NULL;
	S32 i;

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->obj)
		{
			GroupTracker *handle = trackerFromTrackerHandle(objects[i]->obj);
			if (handle->def && handle->def->property_structs.auto_placement_properties)
			{
				if (eaSize(&handle->def->property_structs.auto_placement_properties->auto_place_set) < MAX_AUTO_PLACE_PROPERTY_SETS)
				{
					RELEASE_TRACKER_HANDLE(wleAEGlobalAutoPlacementUI.pTempTrackerHandle)
					wleAEGlobalAutoPlacementUI.pTempTrackerHandle = handleStringFromTracker(handle);

					promptUserForName(autoPlaceSetNameEntryEnter, NULL);
				}
				else
				{
					ui_DialogPopup("Too Many AutoPlaceSets", "You have reached the maximum number of AutoPlaceSets.");
				}
				
				// only allowing for the first selected object that has auto_placement_properties
				// to be handled
				break;
			}
		}
	}

	eaDestroy(&objects);
}

// ------------------------------------------------------------------------------------------------------------
// UITree Node Display Callbacks
void drawNULLTreeNode(UITreeNode *node, const char *field, UI_MY_ARGS, F32 z)
{
	
}

// ------------------------------------------------------------------------------------------------------------
static void wleAEAutoPlacerDeleteSet(UIAnyWidget *pButton, AutoPlacementSet *pAutoPlaceSet)
{
	EditorObject **objects = NULL;
	S32 i;
	bool bDeleted = false;

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->obj)
		{
			GroupTracker *handle = trackerFromTrackerHandle(objects[i]->obj);
			if (handle->def && handle->def->property_structs.auto_placement_properties)
			{
				int idx = eaFind(&handle->def->property_structs.auto_placement_properties->auto_place_set, pAutoPlaceSet);
				
				if (idx != -1)
				{
					{
						UITree *pUITree = getUITreeByIndex(idx);
						if (pUITree)
						{
							if (pUITree->root.contents != pAutoPlaceSet)
							{
								int xxx = 0;
							}

							ui_TreeNodeCollapse(&pUITree->root);
							ui_TreeNodeSetDisplayCallback(&pUITree->root, drawNULLTreeNode, NULL);
						}
					}
					
					// Remove the autoPlaceSet
					eaRemove(&handle->def->property_structs.auto_placement_properties->auto_place_set, idx);
					StructDestroySafe(parse_AutoPlacementSet, &pAutoPlaceSet);

					wleAERefresh();
					bDeleted = true;
					break;
				}
				
			}
		}
	}

	if (bDeleted == false)
	{
		ui_DialogPopup("Error deleting AutoPlaceSet", "There was an error trying to remove the AutoPlaceSet.");
	}

	eaDestroy(&objects);
}


// -------------------------------------------------------------------------------------
// goes through the properties and checks if any of the objects to be placed are valid references
// if not, delete them from the list
// Return: true if the there is a valid object to be placed in the properties
static bool wleValidateAutoPlacedObjects(AutoPlacementSet *pAutoPlaceSet)
{
	int i, x;
	bool bEmptyGroups = true;
	bool bInvalidObject = false;

	for (i = 0; i < eaSize(&pAutoPlaceSet->auto_place_group); i++)
	{
		AutoPlacementGroup *pGroup = pAutoPlaceSet->auto_place_group[i];

		for (x = eaSize(&pGroup->auto_place_objects) - 1; x >= 0; x--)
		{
			AutoPlacementObject *pObject = pGroup->auto_place_objects[x];
			GroupDef *pDef = objectLibraryGetGroupDef(pObject->resource_id, false);
			if (!pDef)
			{
				char szBuffer[1024];
				sprintf(szBuffer, "Object has an invalid resource ID named (%s).", pObject->resource_name);
				ui_DialogPopup("Error: Invalid Object", szBuffer);
				return false;
			}
		}

		bEmptyGroups &= (eaSize(&pGroup->auto_place_objects) == 0);
	}		

	if (bEmptyGroups)
	{
		ui_DialogPopup("Error: Empty Groups", "All groups to be placed have no objects.");
		return false;
	}

	return true;
}

// -------------------------------------------------------------------------------------
static bool apValidateOverrideProperties(GroupTracker *tracker, WorldAutoPlacementProperties* pProperties)
{
	S32 i;

	if (!pProperties)
		return true;

	for (i = 0; i < eaSize(&pProperties->override_list); i++)
	{
		AutoPlacementOverride *pOverride = pProperties->override_list[i];

		GroupDef *pDef = objectLibraryGetGroupDef(pOverride->override_resource_id, false);
		if (!pDef)
		{
			char szBuffer[1024];
			sprintf(szBuffer, "The groupDef named (%s) has an invalid override resource ID named (%s).", tracker->def->name_str, pOverride->override_name);
			ui_DialogPopup("Error: Invalid Object", szBuffer);
			return false;
		}

		pDef = objectLibraryGetGroupDef(pOverride->resource_id, false);
		if (!pDef)
		{
			char szBuffer[1024];
			sprintf(szBuffer, "The groupDef named (%s) has an invalid resource ID named (%s).", tracker->def->name_str, pOverride->resource_name);
			ui_DialogPopup("Error: Invalid Object", szBuffer);
			return false;
		}
	}

	return true;
}


// -------------------------------------------------------------------------------------
static bool wleValidateObjectOverrides(GroupTracker *tracker)
{
	S32 i;
	
	if (!apValidateOverrideProperties(tracker, tracker->def->property_structs.auto_placement_properties) )
		return false;

	// go through the children of the groupTracker,
	// and find all the children that are sub-volumes
	// and add those to the volume list as well.
	for (i = 0; i < tracker->child_count; i++)
	{
		GroupTracker *pChild = tracker->children[i];
		if (pChild->def->property_structs.volume && pChild->def->property_structs.volume->bSubVolume)
		{
			if (!apValidateOverrideProperties(pChild, pChild->def->property_structs.auto_placement_properties) )
				return false;
		}
	}

	return true;
}

// -------------------------------------------------------------------------------------
static void wleAEAutoPlacerDoIt(UIAnyWidget *pButton, AutoPlacementSet *pAutoPlaceSet)
{
	EditorObject **objects = NULL;
	S32 i;
	bool bDidAutoPlacement = false;

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->obj)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(objects[i]->obj);

			if (tracker && tracker->def && tracker->def->property_structs.auto_placement_properties && 
				 (tracker->def->property_structs.volume && !tracker->def->property_structs.volume->bSubVolume))
			{
				if (wleValidateAutoPlacedObjects(pAutoPlaceSet) == false)
					return;
				
				if (wleValidateObjectOverrides(tracker) == false)
					return;

				performAutoPlacement(objects[i]->obj, pAutoPlaceSet);	
			}

			bDidAutoPlacement = true;
			// only allowing the first object to be processed
			break;
		}
	}

	if (bDidAutoPlacement == false)
	{
		ui_DialogPopup("Error", "No object currently selected has volume properties. Objects must have volume properties in order to perform the auto-placement feature.");
	}

	eaDestroy(&objects);
}

// ------------------------------------------------------------------------------------------------------------
// Is Auto-Placer checkbox
// ------------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------
static void wleAEAutoPlaceIsAutoPlacerUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEAutoPlaceUpdateInit();
	
	if (!def)
	{
		param->boolvalue = false;
	}
	else
	{
		param->boolvalue = !!def->property_structs.auto_placement_properties;
	}
}


// ------------------------------------------------------------------------------------------------------------
static void wleAEVolumeIsAutoPlacerApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEAutoPlacerApplyInitAt(i);

		if (wleAEGlobalAutoPlacementUI.isAutoPlacer.boolvalue)
		{
			if (def->property_structs.auto_placement_properties)
			{
				StructDestroySafe(parse_WorldAutoPlacementProperties, &def->property_structs.auto_placement_properties);
			}
			def->property_structs.auto_placement_properties = StructCreate(parse_WorldAutoPlacementProperties);
		}
		else
		{
			// clean up the UITrees so they don't reference bad data for the frame they still exist
			S32 j;
			for (j = 0; j < MAX_AUTO_PLACE_PROPERTY_SETS; j++)
			{
				UITree *pUITree = getUITreeByIndex(j);
				if (pUITree)
				{
					ui_TreeNodeCollapse(&pUITree->root);
					ui_TreeNodeSetDisplayCallback(&pUITree->root, drawNULLTreeNode, NULL);
				}
			}
			if (def->property_structs.auto_placement_properties)
			{
				StructDestroySafe(parse_WorldAutoPlacementProperties, &def->property_structs.auto_placement_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}


// ------------------------------------------------------------------------------------------------------------
// isObjectOverride checkbox
// ------------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------
static void wleAEAutoPlaceIsObjectOverrideUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEAutoPlaceUpdateInit();

	if (!def)
	{
		param->boolvalue = false;
	}
	else
	{
		if(def->property_structs.auto_placement_properties && eaSize(&def->property_structs.auto_placement_properties->override_list) > 0)
			param->boolvalue = true;
	}
}


// ------------------------------------------------------------------------------------------------------------
static void wleAEAutoPlaceIsObjectOverrideApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEAutoPlacerApplyInitAt(i);

		if (!wleAEGlobalAutoPlacementUI.isObjectOverride.boolvalue)
		{
			if (def->property_structs.auto_placement_properties)
			{
				eaDestroyEx(&def->property_structs.auto_placement_properties->override_list, NULL);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}



// ------------------------------------------------------------------------------------------------------------
// callback when the objects have been picked
static bool pickedAutoPlacerObjectOverride( EMPicker* picker, EMPickerSelection** selections, WorldAutoPlacementProperties *pProperties)
{
	AutoPlacementOverride	*pOverrideObject = NULL;

	if( eaSize( &selections ) == 0 ) 
	{
		return false;
	}

	pOverrideObject = eaTail(&pProperties->override_list);
	if (pOverrideObject)
	{
		if (pOverrideObject->override_name != NULL)
			pOverrideObject = NULL;
	}

	{
		EMPickerSelection *pSelection = selections[0];
		ResourceInfo* entry = (ResourceInfo*)pSelection->data;
		char *pFileName = strchr(pSelection->doc_name, ',');
		char *resource_name;
		if (! pFileName)
		{	// bad doc_name, probably spit out a warning here
			return true;
		}

		// force a termination of the string so I can copy just the name over
		*pFileName = 0;
		resource_name = estrCreateFromStr(pSelection->doc_name);
		// put the ',' character back
		*pFileName = ',';

		if (!pOverrideObject )
		{
			pOverrideObject = StructCreate(parse_AutoPlacementOverride);

			pOverrideObject->resource_id = entry->resourceID;
			pOverrideObject->resource_name = resource_name;

			eaPush(&pProperties->override_list, pOverrideObject);

			eaClear(&picker->selections);
			emPickerRefresh(picker);
			return false;
		}
		else
		{
			pOverrideObject->override_resource_id = entry->resourceID;
			pOverrideObject->override_name = resource_name;
			return true;
		}

	}
}

// ------------------------------------------------------------------------------------------------------------
static void pickerAutoPlacerObjectOverrideClosed(EMPicker *picker, WorldAutoPlacementProperties *pProperties)
{
	AutoPlacementOverride	*pOverrideObject = NULL;
	
	pOverrideObject = eaTail(&pProperties->override_list);
	if (pOverrideObject)
	{
		if (pOverrideObject->override_name == NULL)
		{
			// canceled the override picking before both were chosen, remove the last 
			eaPop(&pProperties->override_list);
			StructDestroy(parse_AutoPlacementOverride, pOverrideObject);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------
static void wleAEAutoPlacerAddObjectOverride(UIAnyWidget *pButton, WorldAutoPlacementProperties *pProperties)
{
	EMPicker* picker;
		
	picker = emPickerGetByName( "Object Picker" );
	assert( picker );
	emPickerShowEx( picker, "Select", false, pickedAutoPlacerObjectOverride, pickerAutoPlacerObjectOverrideClosed, pProperties);
}

// ------------------------------------------------------------------------------------------------------------
static void wleAEAutoPlacerRemoveObjectOverride(UIAnyWidget *pButton, WorldAutoPlacementProperties *pProperties)
{
	UIList *uiList;
	S32 iSelectedRow;

	uiList = (UIList*)ui_RebuildableTreeGetWidgetByName(wleAEGlobalAutoPlacementUI.autoWidget, sOVERRIDE_LIST_NAME);
	if (! uiList)
		return;

	iSelectedRow = ui_ListGetSelectedRow(uiList);
	if (iSelectedRow == -1)
		return;

	if (iSelectedRow < eaSize(&pProperties->override_list))
	{
		AutoPlacementOverride *pOverride = eaRemove(&pProperties->override_list, iSelectedRow);
		StructDestroySafe(parse_AutoPlacementOverride, &pOverride);
	}
}

// ------------------------------------------------------------------------------------------------------------
// 
// ------------------------------------------------------------------------------------------------------------
static void wleAEParamUpdate(void *p, eAPParamType *peAPType, EditorObject *obj)
{
	union {
		void*					asVoid;
		WleAEParamFloat*		asFloat;
		WleAEParamExpression*	asExpr;
		WleAEParamBool*			asBool;
	} param;
	U32 treeIdx = 0;
	wleAEAutoPlaceUpdateInit();
	
	assert(peAPType);

	param.asVoid = p;

	// set the default
	switch (*peAPType)
	{
		case eAPParamType_WEIGHT:
			// float type param
			param.asFloat->floatvalue = 0.0f;
			treeIdx = (U32)param.asFloat->struct_offset;

		xcase eAPParamType_REQUIRED_EXPR:
		case eAPParamType_FITNESS_EXPR:
			// expression type param
			param.asExpr->exprvalue = NULL;
			treeIdx = (U32)param.asExpr->struct_offset;

		xcase eAPParamType_SLOPESNAP:
			// bool type param
			param.asBool->boolvalue = false;
			treeIdx = (U32)param.asBool->struct_offset;

		xdefault:
		{
			// we should never get here!
			assert(0);
		} break;
	}


	if (def && def->property_structs.auto_placement_properties)
	{
		UITreeNode* pTreeNode = getUITreeNodeCurrentSelectedNode(treeIdx);
		if (pTreeNode && pTreeNode->contents)
		{
			switch (*peAPType)
			{
				case eAPParamType_WEIGHT:
				{
					if (pTreeNode->table == parse_AutoPlacementObject)
					{
						AutoPlacementObject * pAutoPlacementObject = (AutoPlacementObject*)pTreeNode->contents;
						param.asFloat->floatvalue = pAutoPlacementObject->weight;
					}
					else if (pTreeNode->table == parse_AutoPlacementGroup)
					{
						AutoPlacementGroup * pAutoPlacementGroup = (AutoPlacementGroup*)pTreeNode->contents;
						param.asFloat->floatvalue = pAutoPlacementGroup->weight;
					}
				}
				
				xcase eAPParamType_REQUIRED_EXPR:
				case eAPParamType_FITNESS_EXPR:
				{
					// expression type param
					Expression *pExpression = NULL;
					if (pTreeNode->table == parse_AutoPlacementObject)
					{
						AutoPlacementObject * pAutoPlacementObject = (AutoPlacementObject*)pTreeNode->contents;
						if (*peAPType == eAPParamType_FITNESS_EXPR)
						{
							pExpression = pAutoPlacementObject->fitness_expression;
						}
						else
						{
							pExpression = pAutoPlacementObject->required_condition;
						}
						
					}
					else if (pTreeNode->table == parse_AutoPlacementGroup)
					{
						AutoPlacementGroup * pAutoPlacementGroup = (AutoPlacementGroup*)pTreeNode->contents;
						
						if (*peAPType == eAPParamType_FITNESS_EXPR)
						{
							pExpression = pAutoPlacementGroup->fitness_expression;
						}
						else
						{
							pExpression = pAutoPlacementGroup->required_condition;
						}
					}

					param.asExpr->exprvalue = exprClone(pExpression);
				}
				
				xcase eAPParamType_SLOPESNAP:
				{
					// bool type param
					if (pTreeNode->table == parse_AutoPlacementObject)
					{
						AutoPlacementObject * pAutoPlacementObject = (AutoPlacementObject*)pTreeNode->contents;
						param.asBool->boolvalue = pAutoPlacementObject->snap_to_slope;
					}
					
				} 
			}

			
		}
	}
}

// ------------------------------------------------------------------------------------------------------------
static void wleAEParamApply(void *p, eAPParamType *peAPType, EditorObject **objs)
{
	union
	{
		void*					asVoid;
		WleAEParamFloat*		asFloat;
		WleAEParamExpression*	asExpr;
		WleAEParamBool*			asBool;
	} param;

	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		U32 treeIdx = 0;
		wleAEAutoPlacerApplyInitAt(i);
	
		assert(peAPType);

		param.asVoid = p;

		// set the default
		switch (*peAPType)
		{
			acase eAPParamType_WEIGHT:
				// float type param
				treeIdx = (U32)param.asFloat->struct_offset;

			xcase eAPParamType_REQUIRED_EXPR:
			case eAPParamType_FITNESS_EXPR:
				// expression type param
				treeIdx = (U32)param.asExpr->struct_offset;

			xcase eAPParamType_SLOPESNAP:
				// bool type param
				treeIdx = (U32)param.asBool->struct_offset;

			xdefault:
			{
				// we should never get here!
				assert(0);
			} break;
		}

		if (def && def->property_structs.auto_placement_properties)
		{
			UITreeNode* pTreeNode = getUITreeNodeCurrentSelectedNode(treeIdx);
			if (pTreeNode && pTreeNode->contents)
			{
				switch (*peAPType)
				{
					acase eAPParamType_WEIGHT:
					{
						F32 *pfValue = NULL;
						if (pTreeNode->table == parse_AutoPlacementObject)
						{
							AutoPlacementObject * pAutoPlacementObject = (AutoPlacementObject*)pTreeNode->contents;
							pAutoPlacementObject->weight = param.asFloat->floatvalue;
						 
						}
						else if (pTreeNode->table == parse_AutoPlacementGroup)
						{
							AutoPlacementGroup * pAutoPlacementGroup = (AutoPlacementGroup*)pTreeNode->contents;
							pAutoPlacementGroup->weight = param.asFloat->floatvalue;
						}
					}
					xcase eAPParamType_REQUIRED_EXPR:
					case eAPParamType_FITNESS_EXPR:
					{
						Expression **ppExpression = NULL;
						if (pTreeNode->table == parse_AutoPlacementObject)
						{
							AutoPlacementObject * pAutoPlacementObject = (AutoPlacementObject*)pTreeNode->contents;
							if (*peAPType == eAPParamType_FITNESS_EXPR)
							{
								ppExpression = &pAutoPlacementObject->fitness_expression;
							}
							else
							{
								ppExpression = &pAutoPlacementObject->required_condition;
							}
						
						}
						else if (pTreeNode->table == parse_AutoPlacementGroup)
						{
							AutoPlacementGroup * pAutoPlacementGroup = (AutoPlacementGroup*)pTreeNode->contents;

							if (*peAPType == eAPParamType_FITNESS_EXPR)
							{
								ppExpression = &pAutoPlacementGroup->fitness_expression;
							}
							else
							{
								ppExpression = &pAutoPlacementGroup->required_condition;
							}
						}

						// 
						if (*ppExpression != NULL)
						{
							exprDestroy(*ppExpression);
						}
						*ppExpression = exprClone(param.asExpr->exprvalue);
					}
					xcase eAPParamType_SLOPESNAP:
					{
						// bool type param
						if (pTreeNode->table == parse_AutoPlacementObject)
						{
							AutoPlacementObject * pAutoPlacementObject = (AutoPlacementObject*)pTreeNode->contents;
							pAutoPlacementObject->snap_to_slope = param.asBool->boolvalue;
						}
					}

				}
			
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

// ------------------------------------------------------------------------------------------------------------
// Float property update/apply
// ------------------------------------------------------------------------------------------------------------
static void autoPlacementFloatUpdate(WleAEParamFloat *pFloatParam, void *pStruct, EditorObject *obj)
{
	int col;
	wleAEAutoPlaceUpdateInit();

	if (def && def->property_structs.auto_placement_properties && pStruct)
	{
		assert(pFloatParam->struct_pti && pFloatParam->struct_fieldname);
		if (ParserFindColumn(pFloatParam->struct_pti, pFloatParam->struct_fieldname, &col))
		{
			pFloatParam->floatvalue = TokenStoreGetF32(pFloatParam->struct_pti, col, pStruct, 0, 0);
		}
	}
	else
	{
		pFloatParam->floatvalue = 0.0f;
	}
}

static void autoPlacementFloatApply(WleAEParamFloat *pFloatParam, void *pStruct, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		int col;
		wleAEAutoPlacerApplyInitAt(i);

		if (def && def->property_structs.auto_placement_properties && pStruct)
		{
			assert(pFloatParam->struct_pti && pFloatParam->struct_fieldname);
			if (ParserFindColumn(pFloatParam->struct_pti, pFloatParam->struct_fieldname, &col))
			{
				TokenStoreSetF32(pFloatParam->struct_pti, col, pStruct, 0, pFloatParam->floatvalue, NULL, NULL);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}



// ------------------------------------------------------------------------------------------------------------
static void fixupButtonWidth(UIButton *pButton)
{
	UIStyleFont* pFont;
	pFont = ui_WidgetGetFont(UI_WIDGET(pButton));
	if (pFont)
	{
		F32 fButtonWidth = ui_StyleFontWidth(pFont, 1.0f, ui_ButtonGetText(pButton));
		F32 fButtonHeight = ui_StyleFontLineHeight(pFont, 1.0f);
		if (fButtonWidth)
		{
			fButtonWidth += 30.0f;
			fButtonHeight += 11.0f;
			ui_WidgetSetDimensionsEx(UI_WIDGET(pButton), fButtonWidth, fButtonHeight, UIUnitFixed, UIUnitFixed);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------
void updatePropertyFields(UIAutoPlacementSetProperties *pUIProperties)
{
	
	
}

// ------------------------------------------------------------------------------------------------------------
int wleAEAutoPlacementReload(EMPanel *panel, EditorObject *edObj)
{
	bool bHide = false;
	bool bPanelActive = true;
	
	int i;
	S32 numAutoPlaceSets = 0;
	GroupTracker *tracker;
		
	if (edObj->type->objType != EDTYPE_TRACKER)
	{
		return WLE_UI_PANEL_INVALID;
	}

	tracker = trackerFromTrackerHandle(edObj->obj);
	if (!tracker || !tracker->def || wleNeedsEncounterPanels(tracker->def))
	{
		return WLE_UI_PANEL_INVALID;
	}

	if (!wleTrackerIsEditable(edObj->obj, false, false, false))
	{
		bPanelActive = false;
	}
	if (!tracker->def->property_structs.volume || tracker->def->property_structs.volume->bSubVolume)
	{
		bPanelActive = false;
	}
	
	if (tracker->def->property_structs.auto_placement_properties)
	{
		// get the number of auto place sets
		numAutoPlaceSets = eaSize(&tracker->def->property_structs.auto_placement_properties->auto_place_set);
		numAutoPlaceSets = MIN(numAutoPlaceSets, MAX_AUTO_PLACE_PROPERTY_SETS);
	}
	
	// we have valid auto_placement_properties
	// fix-up the UI params to make sure they are referencing the property properties
	for (i = 0; i < numAutoPlaceSets; i++)
	{
		AutoPlacementSet *pAutoPlaceSet = tracker->def->property_structs.auto_placement_properties->auto_place_set[i];
		UIAutoPlacementSetProperties *pUIProperties = &wleAEGlobalAutoPlacementUI.aAutoPlacementSets[i];
		UITree *pUITree;

		pUIProperties->proximity.apply_data = pAutoPlaceSet;
		pUIProperties->proximity.update_data = pAutoPlaceSet;
		
		pUIProperties->proximity_variance.apply_data = pAutoPlaceSet;
		pUIProperties->proximity_variance.update_data = pAutoPlaceSet;

		// using the struct_offset field to indicate which UIAutoPlacementSetProperties
		// index is being used when updating/applying the field
		pUIProperties->required_expr.struct_offset = i;
		pUIProperties->fitness_expr.struct_offset = i;
		pUIProperties->weight.struct_offset = i;
		pUIProperties->snapToSlope.struct_offset = i;
					
		pUITree = getUITreeByIndex(i);
		if (pUITree)
		{
			if (pUITree->root.contents != pAutoPlaceSet)
			{
				// the tree was pointing to another set, we need to refresh it
				pUITree->root.contents = pAutoPlaceSet;
				ui_TreeRefresh(pUITree);
			}
		}
	}

	// clear out the rest of the unused auto placement UI 
	for (i = numAutoPlaceSets; i < MAX_AUTO_PLACE_PROPERTY_SETS; i++)
	{
		UIAutoPlacementSetProperties *pUIProperties = &wleAEGlobalAutoPlacementUI.aAutoPlacementSets[i];
		UITree *pUITree;

		pUIProperties->proximity.apply_data = NULL;
		pUIProperties->proximity.update_data = NULL;
		pUIProperties->proximity_variance.apply_data = NULL;
		pUIProperties->proximity_variance.update_data = NULL;

		pUITree = getUITreeByIndex(i);
		if (pUITree && pUITree->root.contents != NULL)
		{
			ANALYSIS_ASSUME(pUITree != NULL);
			pUITree->root.contents = NULL;
			ui_TreeRefresh(pUITree);
		}
	}
	

	// update the current properties we are showing
	wleAEBoolUpdate(&wleAEGlobalAutoPlacementUI.isAutoPlacer);
	wleAEBoolUpdate(&wleAEGlobalAutoPlacementUI.isObjectOverride);
	
	for (i = 0; i < numAutoPlaceSets; i++)
	{
		UIAutoPlacementSetProperties *pUIProperties = &wleAEGlobalAutoPlacementUI.aAutoPlacementSets[i];
		
		wleAEFloatUpdate(&pUIProperties->proximity);
		wleAEFloatUpdate(&pUIProperties->proximity_variance);
		wleAEFloatUpdate(&pUIProperties->weight);
		wleAEExpressionUpdate(&pUIProperties->required_expr);
		wleAEExpressionUpdate(&pUIProperties->fitness_expr);
		wleAEBoolUpdate(&pUIProperties->snapToSlope);

		// decide whether to display the group or the object properties
		// depending on what is selected in the tree
		{
			UITree *pUITree = getUITreeByIndex(i);
			
			pUIProperties->ePropertyDisplay = eSubPropertyDisplay_NONE;

			if (pUITree)
			{
				UITreeNode *pTreeNode = ui_TreeGetSelected(pUITree);

				if (pTreeNode)
				{
					if (pTreeNode->table == parse_AutoPlacementGroup)
					{
						pUIProperties->ePropertyDisplay = eSubPropertyDisplay_GROUP;
					}
					else if (pTreeNode->table == parse_AutoPlacementObject)
					{
						pUIProperties->ePropertyDisplay = eSubPropertyDisplay_OBJECT;
					}
				}
			}
		}
	}
	

	// rebuild UI
	ui_RebuildableTreeInit(wleAEGlobalAutoPlacementUI.autoWidget, 
							&wleAEGlobalAutoPlacementUI.scrollArea->widget.children, 
							0, 0, UIRTOptions_Default);

	wleAEBoolAddWidget(wleAEGlobalAutoPlacementUI.autoWidget, 
						"Auto-Placer", "Enables this object to be able to auto-place objects.", "isAutoPlacer", 
						&wleAEGlobalAutoPlacementUI.isAutoPlacer);
	
	if (wleAEGlobalAutoPlacementUI.isAutoPlacer.boolvalue)
	{
		UIButton* pButton;

		// 
		pButton = ui_AutoWidgetAddButton(wleAEGlobalAutoPlacementUI.autoWidget->root, "Add AutoPlaceSet", wleAEAutoPlacerAddSet, NULL, true, 
										"Add a new auto-placement set.", &wleAEGlobalAutoPlacementUI.genericWidgetParams );
		fixupButtonWidth(pButton);
		
		for (i = 0; i < numAutoPlaceSets; i++)
		{
			UIAutoPlacementSetProperties *pUISetProperties = &wleAEGlobalAutoPlacementUI.aAutoPlacementSets[i];
			AutoPlacementSet *pAutoPlaceSet = eaGet(&tracker->def->property_structs.auto_placement_properties->auto_place_set, i);
			assert(pAutoPlaceSet);
			addAutoPlacementSet(pAutoPlaceSet, pUISetProperties, i);
		}

		// add the auto-placement override UI
		{
			wleAEBoolAddWidget(wleAEGlobalAutoPlacementUI.autoWidget, 
				"Object Override", "Whether this object has object override properties", "isObjectOverride", 
				&wleAEGlobalAutoPlacementUI.isObjectOverride);

			if (wleAEGlobalAutoPlacementUI.isObjectOverride.boolvalue)
			{
				UIList			*uiList = NULL;

				{
					UIWidget *pOldWidget;
					UIListColumn	*col;

					pOldWidget = ui_RebuildableTreeGetOldWidget(wleAEGlobalAutoPlacementUI.autoWidget, sOVERRIDE_LIST_NAME);
					// If we already had a UIList widget a part of this tree, use that one
					// otherwise we need to create a new list
					if (pOldWidget)
					{
						uiList = (UIList*)pOldWidget;
						ui_ListSetModel(uiList, parse_AutoPlacementOverride, &tracker->def->property_structs.auto_placement_properties->override_list);
					}
					else
					{
						uiList = ui_ListCreate(parse_AutoPlacementOverride, &tracker->def->property_structs.auto_placement_properties->override_list, 15);

						ui_ListAppendColumn(uiList, ui_ListColumnCreateParseName("Object", "ResourceName", NULL));

						col = ui_ListColumnCreateParseName("Override", "OverrideResourceName", NULL);
						ui_ListAppendColumn(uiList, col);
						ui_WidgetSetDimensionsEx((UIWidget*) uiList, 1, 150, UIUnitPercentage, UIUnitFixed);
					}
				}

				ui_RebuildableTreeAddWidget(wleAEGlobalAutoPlacementUI.autoWidget->root, UI_WIDGET(uiList), NULL, 
											true, sOVERRIDE_LIST_NAME, &wleAEGlobalAutoPlacementUI.genericWidgetParams);

				// add the buttons
				ui_AutoWidgetAddButton(wleAEGlobalAutoPlacementUI.autoWidget->root, "Add Override", 
										wleAEAutoPlacerAddObjectOverride, tracker->def->property_structs.auto_placement_properties, true, "Add an object override.", 
										&wleAEGlobalAutoPlacementUI.genericWidgetParams );

				ui_AutoWidgetAddButton(wleAEGlobalAutoPlacementUI.autoWidget->root, "Remove Override", 
										wleAEAutoPlacerRemoveObjectOverride, tracker->def->property_structs.auto_placement_properties, false, 
										"Remove the currently selected object override.", 
										&wleAEGlobalAutoPlacementUI.genericWidgetParams );
			}
		}
	}

	ui_RebuildableTreeDoneBuilding(wleAEGlobalAutoPlacementUI.autoWidget);
	emPanelSetHeight(wleAEGlobalAutoPlacementUI.panel, elUIGetEndY(wleAEGlobalAutoPlacementUI.scrollArea->widget.children[0]->children) + 20);
	wleAEGlobalAutoPlacementUI.scrollArea->xSize = emGetSidebarScale() * elUIGetEndX(wleAEGlobalAutoPlacementUI.scrollArea->widget.children[0]->children) + 5;

	emPanelSetActive(wleAEGlobalAutoPlacementUI.panel, bPanelActive);

	return tracker->def->property_structs.auto_placement_properties ? WLE_UI_PANEL_OWNED : WLE_UI_PANEL_UNOWNED;
}

// ------------------------------------------------------------------------------------------------------------
void createAutoPlacementSetUI(UIAutoPlacementSetProperties *pUIProperty) 
{
	// proximity float value
	wleAEAutoPlacementSetupParam(pUIProperty->proximity, NULL);
	pUIProperty->proximity.update_func = autoPlacementFloatUpdate;
	pUIProperty->proximity.apply_func = autoPlacementFloatApply;
	pUIProperty->proximity.struct_pti = parse_AutoPlacementSet;
	pUIProperty->proximity.struct_fieldname = "Proximity";
	pUIProperty->proximity.entry_width = WLE_AE_AUTOPLACE_NUM_ENTRY_WIDTH;
	pUIProperty->proximity.entry_align = WLE_AE_AUTOPLACE_ALIGN_WIDE;
	
	// proximity variance float value
	wleAEAutoPlacementSetupParam(pUIProperty->proximity_variance, NULL);
	pUIProperty->proximity_variance.update_func = autoPlacementFloatUpdate;
	pUIProperty->proximity_variance.apply_func = autoPlacementFloatApply;
	pUIProperty->proximity_variance.struct_pti = parse_AutoPlacementSet;
	pUIProperty->proximity_variance.struct_fieldname = "Variance";
	pUIProperty->proximity_variance.entry_width = WLE_AE_AUTOPLACE_NUM_ENTRY_WIDTH;
	pUIProperty->proximity_variance.entry_align = WLE_AE_AUTOPLACE_ALIGN_WIDE;
	

	// slope snaping
	wleAEAutoPlacementSetupParam(pUIProperty->snapToSlope, NULL);
	pUIProperty->snapToSlope.update_func = wleAEParamUpdate;
	pUIProperty->snapToSlope.update_data = &wleAEGlobalAutoPlacementUI.eParamType_SlopeSnap;
	pUIProperty->snapToSlope.apply_func = wleAEParamApply;
	pUIProperty->snapToSlope.apply_data = &wleAEGlobalAutoPlacementUI.eParamType_SlopeSnap;
	//pUIProperty->snapToSlope.entry_width = WLE_AE_AUTOPLACE_NUM_ENTRY_WIDTH;
	pUIProperty->snapToSlope.entry_align = WLE_AE_AUTOPLACE_ALIGN_WIDE;

	// weighting
	wleAEAutoPlacementSetupParam(pUIProperty->weight, NULL);
	pUIProperty->weight.update_func = wleAEParamUpdate;
	pUIProperty->weight.update_data = &wleAEGlobalAutoPlacementUI.eParamType_Weight;
	pUIProperty->weight.apply_func = wleAEParamApply;
	pUIProperty->weight.apply_data = &wleAEGlobalAutoPlacementUI.eParamType_Weight;
	pUIProperty->weight.entry_width = WLE_AE_AUTOPLACE_NUM_ENTRY_WIDTH;
	pUIProperty->weight.entry_align = WLE_AE_AUTOPLACE_ALIGN_WIDE;


	// required_expr
	wleAEAutoPlacementSetupParam(pUIProperty->required_expr, NULL);
	pUIProperty->required_expr.update_func = wleAEParamUpdate;
	pUIProperty->required_expr.update_data = &wleAEGlobalAutoPlacementUI.eParamType_RequiredExpr;
	pUIProperty->required_expr.apply_func = wleAEParamApply;
	pUIProperty->required_expr.apply_data = &wleAEGlobalAutoPlacementUI.eParamType_RequiredExpr;
	pUIProperty->required_expr.entry_width = 1.0;

	pUIProperty->required_expr.context = getAutoPlacementExprContext();

	// fitness_expr
	wleAEAutoPlacementSetupParam(pUIProperty->fitness_expr, NULL);
	pUIProperty->fitness_expr.update_func = wleAEParamUpdate;
	pUIProperty->fitness_expr.update_data = &wleAEGlobalAutoPlacementUI.eParamType_FitnessExpr;
	pUIProperty->fitness_expr.apply_func = wleAEParamApply;
	pUIProperty->fitness_expr.apply_data = &wleAEGlobalAutoPlacementUI.eParamType_FitnessExpr;
	pUIProperty->fitness_expr.entry_width = 1.0;
	
	pUIProperty->fitness_expr.context = getAutoPlacementExprContext();


}



// ------------------------------------------------------------------------------------------------------------
void wleAEAutoPlacementCreate(EMPanel *panel)
{
	int i;
	if (wleAEGlobalAutoPlacementUI.autoWidget)
		return;

	wleAEGlobalAutoPlacementUI.eParamType_Weight = eAPParamType_WEIGHT;
	wleAEGlobalAutoPlacementUI.eParamType_RequiredExpr = eAPParamType_REQUIRED_EXPR;
	wleAEGlobalAutoPlacementUI.eParamType_FitnessExpr = eAPParamType_FITNESS_EXPR;
	wleAEGlobalAutoPlacementUI.eParamType_SlopeSnap = eAPParamType_SLOPESNAP;

	wleAEGlobalAutoPlacementUI.panel = panel;

	// initialize auto widget and scroll area
	wleAEGlobalAutoPlacementUI.autoWidget = ui_RebuildableTreeCreate();
	wleAEGlobalAutoPlacementUI.scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, true, false);
	wleAEGlobalAutoPlacementUI.scrollArea->widget.widthUnit = UIUnitPercentage;
	wleAEGlobalAutoPlacementUI.scrollArea->widget.heightUnit = UIUnitPercentage;
	wleAEGlobalAutoPlacementUI.scrollArea->widget.sb->alwaysScrollX = false;
	emPanelAddChild(panel, wleAEGlobalAutoPlacementUI.scrollArea, false);

	// the isAutoPlacer check box
	wleAEAutoPlacementSetupParam(wleAEGlobalAutoPlacementUI.isAutoPlacer, NULL);
	wleAEGlobalAutoPlacementUI.isAutoPlacer.update_func = wleAEAutoPlaceIsAutoPlacerUpdate;
	wleAEGlobalAutoPlacementUI.isAutoPlacer.apply_func = wleAEVolumeIsAutoPlacerApply;

	wleAEAutoPlacementSetupParam(wleAEGlobalAutoPlacementUI.isObjectOverride, NULL);
	wleAEGlobalAutoPlacementUI.isObjectOverride.update_func = wleAEAutoPlaceIsObjectOverrideUpdate;
	wleAEGlobalAutoPlacementUI.isObjectOverride.apply_func = wleAEAutoPlaceIsObjectOverrideApply;
	

	wleAEGlobalAutoPlacementUI.genericWidgetParams.alignTo = /*2 **/ WLE_AE_AUTOPLACE_INDENT;
	
	for (i = 0; i < MAX_AUTO_PLACE_PROPERTY_SETS; i++)
	{
		createAutoPlacementSetUI(&wleAEGlobalAutoPlacementUI.aAutoPlacementSets[i]);
	}

}

// ------------------------------------------------------------------------------------------------------------
// UITree Node Display Callbacks
void drawAutoPlacementGroupTreeNode(UITreeNode *node, const char *field, UI_MY_ARGS, F32 z)
{
	ui_TreeDisplayText(node, ((AutoPlacementGroup*)node->contents)->group_name, UI_MY_VALUES, z);
}

void drawAutoPlacementNameUIDTreeNode(UITreeNode *node, const char *field, UI_MY_ARGS, F32 z)
{
	GroupDef *def = objectLibraryGetGroupDefByName(((AutoPlacementObject*)node->contents)->resource_name, false);
	ui_TreeDisplayText(node, def->name_str, UI_MY_VALUES, z);
}

// ------------------------------------------------------------------------------------------------------------
// UITree Functions
// ------------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------
// UITreeNode fill callbacks
#define WLE_TREE_NODE_HEIGHT 15

void uiTreeGroupFill(UITreeNode *parent, UserData nil)
{
	AutoPlacementSet *pAutoPlaceSet = (AutoPlacementSet*)parent->tree->root.contents;
		
	// populate the group node
	if (pAutoPlaceSet)
	{
		int i;
		for(i = 0; i < eaSize(&pAutoPlaceSet->auto_place_group); i++)
		{
			AutoPlacementGroup *pAutoPlaceGroup = pAutoPlaceSet->auto_place_group[i];
			
			if (pAutoPlaceGroup == parent->contents)
			{
				int x;

				for(x = 0; x < eaSize(&pAutoPlaceGroup->auto_place_objects); x++)
				{
					AutoPlacementObject *pAutoPlaceObject = pAutoPlaceGroup->auto_place_objects[x];

					// create the node for the object
					UITreeNode *pTreeNode = ui_TreeNodeCreate( parent->tree, cryptAdler32String(pAutoPlaceObject->resource_name), 
														parse_AutoPlacementObject, pAutoPlaceObject, NULL, NULL, 
														drawAutoPlacementNameUIDTreeNode, NULL, WLE_TREE_NODE_HEIGHT );

					ui_TreeNodeAddChild(parent, pTreeNode);
				}
				break;
			}
		}
	}
}


// ------------------------------------------------------------------------------------------------------------
void uiTreeRootFill(UITreeNode *parent, UserData nil)
{
	AutoPlacementSet *pAutoPlaceSet = (AutoPlacementSet*)parent->contents;
	
	// populate the root
	if (pAutoPlaceSet)
	{
		int i;

		for(i = 0; i < eaSize(&pAutoPlaceSet->auto_place_group); i++)
		{
			AutoPlacementGroup *pAutoPlaceGroup = pAutoPlaceSet->auto_place_group[i];

			// create the node for the group
			UITreeNode *pGroupTreeNode = ui_TreeNodeCreate( parent->tree, cryptAdler32String(pAutoPlaceGroup->group_name), 
												parse_AutoPlacementGroup, pAutoPlaceGroup, NULL, NULL, 
												drawAutoPlacementGroupTreeNode, NULL, WLE_TREE_NODE_HEIGHT );
			ui_TreeNodeSetFillCallback(pGroupTreeNode, uiTreeGroupFill, NULL);
			
			ui_TreeNodeAddChild(parent, pGroupTreeNode);
		}
	}
}


// ------------------------------------------------------------------------------------------------------------
static void UITreeActivation(UITree *pTree, UserData nil)
{
	if (pTree->selected)
	{
		if (! pTree->selected->open)
		{
			ui_TreeNodeExpand(pTree->selected);
		}
	}
	
	wleAERefresh();
}

// ------------------------------------------------------------------------------------------------------------
void UITreeRightClickMenu(UITree *tree, UserData nil)
{
	UITreeNode *currNode = ui_TreeGetSelected(tree);
	AutoPlacementSet *pAutoPlaceSet = tree->root.contents;

	if (!wleAEGlobalAutoPlacementUI.rclickMenu)
		wleAEGlobalAutoPlacementUI.rclickMenu = ui_MenuCreate("");
	
	eaDestroyEx(&wleAEGlobalAutoPlacementUI.rclickMenu->items, ui_MenuItemFree);
		
	ui_MenuAppendItems(wleAEGlobalAutoPlacementUI.rclickMenu,
						
						NULL);
	if (currNode)
	{
		if (currNode->table == parse_AutoPlacementGroup)
		{
			ui_MenuAppendItems(wleAEGlobalAutoPlacementUI.rclickMenu,
				ui_MenuItemCreate("Add Group...", UIMenuCallback, wleAEAutoPlacerAddGroup, pAutoPlaceSet, NULL),
				ui_MenuItemCreate("Remove Group", UIMenuCallback, wleAEAutoPlacerRemoveGroup, pAutoPlaceSet, NULL),
				ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
				ui_MenuItemCreate("Add Object...", UIMenuCallback, wleAEAutoPlacerAddObject, pAutoPlaceSet, NULL),
				NULL);
		}
		else if (currNode->table == parse_AutoPlacementObject)
		{
			ui_MenuAppendItems(wleAEGlobalAutoPlacementUI.rclickMenu,
				ui_MenuItemCreate("Add Group...", UIMenuCallback, wleAEAutoPlacerAddGroup, pAutoPlaceSet, NULL),
				ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
				ui_MenuItemCreate("Remove Object", UIMenuCallback, wleAEAutoPlacerRemoveObject, pAutoPlaceSet, NULL),
				NULL);
		}
	}
	else
	{
		ui_MenuAppendItem(wleAEGlobalAutoPlacementUI.rclickMenu,
			ui_MenuItemCreate("Add Group...", UIMenuCallback, wleAEAutoPlacerAddGroup, pAutoPlaceSet, NULL) );
	}
	
	
	
	wleAEGlobalAutoPlacementUI.rclickMenu->widget.scale = emGetSidebarScale() / g_ui_State.scale;
	ui_MenuPopupAtCursor(wleAEGlobalAutoPlacementUI.rclickMenu);
}

// ------------------------------------------------------------------------------------------------------------
void ui_TreeSetSelectedCallback(SA_PARAM_NN_VALID UITree *tree, UIActivationFunc selectedF, UserData selectedData);

UITree* wleAECreateAutoPlaceUITree(AutoPlacementSet *pAutoPlaceSet)
{
	UITree* pUITree;

	pUITree = ui_TreeCreate(0, 0, 1, 1);
	assert(pUITree);
	pUITree->root.contents = pAutoPlaceSet;

	ui_TreeSetSelectedCallback(pUITree, UITreeActivation, NULL);

	ui_WidgetSetDimensionsEx(UI_WIDGET(pUITree), 1, 150, UIUnitPercentage, UIUnitFixed);
	ui_TreeNodeSetFillCallback(&pUITree->root, uiTreeRootFill, NULL);
	ui_TreeSetContextCallback(pUITree, UITreeRightClickMenu, NULL);
	ui_TreeNodeExpand(&pUITree->root);

	return pUITree;
}

// ------------------------------------------------------------------------------------------------------------
void addUITreeToAutoTree(AutoPlacementSet *pAutoPlaceSet, UIRTNode* pPropertyGroup, S32 propertySetID)
{
	UITree* pUITree;
	char szFullTreeName[1024];
	
	sprintf(szFullTreeName, "set%d/%s", propertySetID, sAUTO_PLACER_TREE_NAME);

	pUITree = (UITree*)ui_RebuildableTreeGetOldWidget(wleAEGlobalAutoPlacementUI.autoWidget, szFullTreeName);
	if (pUITree)
	{
		if (pUITree->root.contents != pAutoPlaceSet)
		{
			pUITree = NULL;
		}
	}
	
	if (!pUITree)
	{
		pUITree = wleAECreateAutoPlaceUITree(pAutoPlaceSet);
	}

	// add the label first
	{
		UILabel *pLabel = ui_LabelCreate("Groups & Objects", 0, 0);
		ui_RebuildableTreeAddWidget(pPropertyGroup, UI_WIDGET(pLabel), NULL, true, "label", &wleAEGlobalAutoPlacementUI.genericWidgetParams);
	}
		
	ui_RebuildableTreeAddWidget(pPropertyGroup, UI_WIDGET(pUITree), NULL, true, sAUTO_PLACER_TREE_NAME, &wleAEGlobalAutoPlacementUI.genericWidgetParams);
}


// ------------------------------------------------------------------------------------------------------------
static void UIPanelToggleExpanded(UIExpander *expand, void *unused)
{
	wleAERefresh();
}

// ------------------------------------------------------------------------------------------------------------
void addAutoPlacementSet(AutoPlacementSet *pAutoPlaceSet, UIAutoPlacementSetProperties *pUIProperty, U32 setIdx)
{
	UIRTNode* pPropertyGroup;
	UIButton* pButton;

#define BUFFER_MAX_LENGTH 64
	char szGroupNameKey[BUFFER_MAX_LENGTH];
	char szGroupName[BUFFER_MAX_LENGTH];
	snprintf(szGroupName, BUFFER_MAX_LENGTH, "AutoPlaceSet: %s", pAutoPlaceSet->set_name);
	sprintf(szGroupNameKey, "set%d", setIdx);
	

	pPropertyGroup = ui_RebuildableTreeAddGroup(wleAEGlobalAutoPlacementUI.autoWidget->root, szGroupName, 
													szGroupNameKey, true, NULL);
	// indent the group 
	pPropertyGroup->params.alignTo = WLE_AE_AUTOPLACE_INDENT;
	ui_ExpanderSetExpandCallback(pPropertyGroup->expander, UIPanelToggleExpanded, NULL);
	

	// add a button to delete (should be valid only if there are more than one set?)
	pButton = ui_AutoWidgetAddButton(pPropertyGroup, "Delete AutoPlaceSet", wleAEAutoPlacerDeleteSet, pAutoPlaceSet, true, 
									"Add a new group to the Auto-Place set.", &wleAEGlobalAutoPlacementUI.genericWidgetParams );
	fixupButtonWidth(pButton);

	// Add the proximity 
	wleAEFloatAddWidgetEx(pPropertyGroup, "Proximity", "Minimum distance to another object. Measured in feet.", "autoPlaceProximity", 
						&pUIProperty->proximity, 1.0f, 10000.0f, 1.0f);
	wleAEFloatAddWidgetEx(pPropertyGroup, "Proximity Variance", "Distance from the proximity that the object will have a percentage chance to be placed.", "autoPlaceVariance", 
						&pUIProperty->proximity_variance, 1.0f, 10000.0f, 1.0f);
	
	// add the UITree
	addUITreeToAutoTree(pAutoPlaceSet, pPropertyGroup, setIdx);
	
	if (pUIProperty->ePropertyDisplay == eSubPropertyDisplay_GROUP)
	{
		wleAEFloatAddWidgetEx(pPropertyGroup, "Group- Weight", "The weight relative to the other items in the list.", 
			"weight", &pUIProperty->weight, 0.0f, 1000.0f, 1.0f);

		wleAEExpressionAddWidgetEx(pPropertyGroup, "Required Condition", "Condition that must pass if the item is to be placed.", 
			"condition", &pUIProperty->required_expr);

		wleAEExpressionAddWidgetEx(pPropertyGroup, "Fitness Expression", "A fitness value in the range of [0, 1] to determine the best placement for the item.", 
			"fitness", &pUIProperty->fitness_expr);

	}
	else if (pUIProperty->ePropertyDisplay == eSubPropertyDisplay_OBJECT)
	{
		wleAEBoolAddWidgetEx(pPropertyGroup, "Orient To Slope", "If this is true, a placed object will be aligned to the ground it is on.",
								"slopeSnap", &pUIProperty->snapToSlope);

		wleAEFloatAddWidgetEx(pPropertyGroup, "Object- Weight", "The weight relative to the other items in the list.", 
			"weight", &pUIProperty->weight, 0.0f, 1000.0f, 1.0f);

		wleAEExpressionAddWidgetEx(pPropertyGroup, "Required Condition", "Condition that must pass if the item is to be placed.", 
			"condition", &pUIProperty->required_expr);
		wleAEExpressionAddWidgetEx(pPropertyGroup, "Fitness Expression", "A fitness value in the range of [0, 1] to determine the best placement for the item.", 
			"fitness", &pUIProperty->fitness_expr);
	}

#if defined(USE_ADD_REMOVE_BUTTONS)
	// add the buttons
	pButton = ui_AutoWidgetAddButton(pPropertyGroup, "Add Group", wleAEAutoPlacerAddGroup, pAutoPlaceSet, true, 
										"Add a new group to the Auto-Place set.", &wleAEGlobalAutoPlacementUI.genericWidgetParams );
	fixupButtonWidth(pButton);
	pButton = ui_AutoWidgetAddButton(pPropertyGroup, "Remove Group", wleAEAutoPlacerRemoveGroup, pAutoPlaceSet, false, 
									 "Remove the selected group from the Auto-Place set.", &wleAEGlobalAutoPlacementUI.genericWidgetParams);
	fixupButtonWidth(pButton);

	pButton = ui_AutoWidgetAddButton(pPropertyGroup, "Add Object", wleAEAutoPlacerAddObject, pAutoPlaceSet, true, 
									"Add an object to the Auto-Place Set.", &wleAEGlobalAutoPlacementUI.genericWidgetParams );
	fixupButtonWidth(pButton);

	pButton = ui_AutoWidgetAddButton(pPropertyGroup, "Remove Object", wleAEAutoPlacerRemoveObject, pAutoPlaceSet, false, 
									"Remove the currently selected object from the Auto-Place Set.", &wleAEGlobalAutoPlacementUI.genericWidgetParams );
	fixupButtonWidth(pButton);
#endif	

	pButton = ui_AutoWidgetAddButton(pPropertyGroup, "Perform Auto-Placement", wleAEAutoPlacerDoIt, pAutoPlaceSet, true, 
									"Clear any auto-placed items for this volume and generate a new set of objects using the current set.", &wleAEGlobalAutoPlacementUI.genericWidgetParams );
	fixupButtonWidth(pButton);

	
	
}


#endif

