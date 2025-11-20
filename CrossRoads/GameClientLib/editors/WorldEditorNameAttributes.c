#ifndef NO_EDITORS

#include "WorldEditorNameAttributes.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorOperations.h"
#include "WorldGrid.h"
#include "WorldEditorUtil.h"
#include "EditLibUIUtil.h"
#include "EditorManager.h"
#include "estring.h"
#include "StringUtil.h"
#include "ResourceSearch.h"
#include "qsortG.h"
#include "MultiEditFieldContext.h"

#include "autogen/WorldEditorNameAttributes_c_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

AUTO_STRUCT;
typedef struct wleAENameUI {
	char *pchDefName;					AST(NAME("DefName"))
	char **ppchDefLogicalNames;			AST(NAME("DefLogicalNames"))
	char *pchDefTags;					AST(NAME("DefTags"))

	AST_STOP

	GroupTracker *pTracker;
	bool bLogicalNameEnabled;
} wleAENameUI;
static wleAENameUI g_wleAENameUI = {0};


static void wleFillTagsList(char ***ppcNewTagsList)
{
	int i, j, k;
	ResourceSearchRequest request = {0};
	ResourceSearchResult *pResult;

	//Do a search for anything with a tag
	request.eSearchMode = SEARCH_MODE_TAG_SEARCH;
	request.pcSearchDetails = "";
	request.pcType = OBJECT_LIBRARY_DICT;
	pResult = handleResourceSearchRequest(&request);
	for(i=eaSize(&pResult->eaRows)-1; i>=0; --i) {
		GroupDef *pGroup;
		char *pcName = pResult->eaRows[i]->pcName;

		//Get the Def
		pGroup = objectLibraryGetGroupDefByName(pcName, false);
		if (pGroup) {
			//Get the Tag List
			char *pcTagList = pGroup->tags;
			if(pcTagList) {
				char **ppcTagsArray = NULL;

				DivideString(pcTagList, ",", &ppcTagsArray, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE );

				for ( j=0; j < eaSize(&ppcTagsArray); j++ ) {
					char *pcCheckTag = ppcTagsArray[j];
					bool found = false;

					for ( k=0; k < eaSize(ppcNewTagsList); k++ ) {
						int strRet = stricmp((*ppcNewTagsList)[k], pcCheckTag);

						if(strRet == 0) {
							//Remove Duplicates
							found = true;
							break;
						} else if (strRet > 0) {
							//Sort Alphabetically
							found = true;
							eaInsert(ppcNewTagsList, StructAllocString(pcCheckTag), k);
							break;
						}
					}
					if(!found)
						eaPush(ppcNewTagsList, StructAllocString(pcCheckTag));
				}

				eaDestroyEx(&ppcTagsArray, NULL);
			}
		}
	}
}

typedef struct WleUIEditTagsWin
{
	UIWindow *pWindow;
	UITextEntry *pNewTagsEntry;

	EditorObject **ppObjects;
	char **ppTagsList;

} WleUIEditTagsWin;
typedef void(*wleUIEditTagsDialogApplyFunc)(GroupDef *pGroup, WleUIEditTagsWin *ui);

static bool wleUIEditTagsDialogCancel(void *unused, WleUIEditTagsWin *ui)
{
	if(ui->pWindow)
		elUIWindowClose(NULL, ui->pWindow);
	eaDestroy(&ui->ppObjects);
	eaDestroyEx(&ui->ppTagsList, StructFreeString);
	SAFE_FREE(ui);
	return true;
}

static void wleUIEditTagsDialogRemove(GroupDef *pGroup, WleUIEditTagsWin *ui)
{
	int i, j;
	const char *pcExistingTags = pGroup->tags;
	bool bHasTags = false;
	//Make sure we have at least one tag
	if(pcExistingTags)
	{
		int length = (int)strlen(pcExistingTags);
		for ( i=0; i < length; i++ ) {
			if(pcExistingTags[i] != ' ') {
				bHasTags = true;
				break;
			}
		}
	}
	if(bHasTags)
	{
		char **ppcExistingTagsArray = NULL;
		char **ppcAppendTagsArray = NULL;
		char pcNewTagString[MAX_PATH];

		pcNewTagString[0] = 0;

		//Move the list of existing tags into an eArray
		DivideString(pcExistingTags, ",", &ppcExistingTagsArray, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE );
		//Move the list of new tags into an eArray. Tokenized because there may be tags that don't exist on any objects currently
		DivideString(ui_TextEntryGetText(ui->pNewTagsEntry), ",", &ppcAppendTagsArray, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE );

		//For each new tag
		for ( i=0; i < eaSize(&ppcAppendTagsArray); i++ )
		{
			const char *pcNewTag = ppcAppendTagsArray[i];
			//Remove from existing
			for ( j = eaSize(&ppcExistingTagsArray)-1; j >= 0 ; j--)
			{
				if(stricmp(ppcExistingTagsArray[j], pcNewTag)==0)
				{
					char *pcRemoveString = eaRemove(&ppcExistingTagsArray, j);
					free(pcRemoveString);
					break;
				}
			}
		}

		eaQSort(ppcExistingTagsArray, strCmp);

		//Construct string
		for ( j=0; j < eaSize(&ppcExistingTagsArray); j++ )
		{
			strcat(pcNewTagString, ppcExistingTagsArray[j]);
			if(j < eaSize(&ppcExistingTagsArray)-1)
				strcat(pcNewTagString, ","); //No comma on the last one
		}

		//Set the new list of tags
		StructCopyString(&pGroup->tags, pcNewTagString);
		eaDestroyEx(&ppcExistingTagsArray, NULL);
		eaDestroyEx(&ppcAppendTagsArray, NULL);
	}
}

static void wleUIEditTagsDialogAppend(GroupDef *pGroup, WleUIEditTagsWin *ui)
{
	int i, j;
	const char *pcExistingTags = pGroup->tags;
	bool bHasTags = false;
	//Make sure we have at least one tag
	if(pcExistingTags)
	{
		int length = (int)strlen(pcExistingTags);
		for ( i=0; i < length; i++ ) {
			if(pcExistingTags[i] != ' ') {
				bHasTags = true;
				break;
			}
		}
	}
	if(bHasTags)
	{
		char **ppcExistingTagsArray = NULL;
		char **ppcAppendTagsArray = NULL;
		const char **ppcNewTagsArray = NULL;
		char pcNewTagString[MAX_PATH];

		pcNewTagString[0] = 0;

		//Move the list of existing tags into an eArray
		DivideString(pcExistingTags, ",", &ppcExistingTagsArray, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE );
		//Move the list of new tags into an eArray. Tokenized because there may be tags that don't exist on any objects currently
		DivideString(ui_TextEntryGetText(ui->pNewTagsEntry), ",", &ppcAppendTagsArray, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE );

		//Start with the existing tags
		eaCopy(&ppcNewTagsArray, &ppcExistingTagsArray);

		//For each new tag
		for ( i=0; i < eaSize(&ppcAppendTagsArray); i++ )
		{
			bool exists = false;
			const char *pcNewTag = ppcAppendTagsArray[i];
			//Check for duplicates
			for ( j=0; j < eaSize(&ppcExistingTagsArray); j++ )
			{
				if(stricmp(ppcExistingTagsArray[j], pcNewTag)==0)
				{
					exists = true;
					break;
				}
			}
			if(!exists)
			{
				//Append Tag
				eaPush(&ppcNewTagsArray, pcNewTag);
			}
		}

		eaQSort(ppcNewTagsArray, strCmp);

		//Construct string
		for ( j=0; j < eaSize(&ppcNewTagsArray); j++ )
		{
			strcat(pcNewTagString, ppcNewTagsArray[j]);
			if(j < eaSize(&ppcNewTagsArray)-1)
				strcat(pcNewTagString, ","); //No comma on the last one
		}

		//Set the new list of tags
		StructCopyString(&pGroup->tags, pcNewTagString);
		eaDestroyEx(&ppcExistingTagsArray, NULL);
		eaDestroyEx(&ppcAppendTagsArray, NULL);
		eaDestroy(&ppcNewTagsArray);
	}
	else
	{
		//If there are no existing tags, just set the value.
		const char *pcNewTagString = ui_TextEntryGetText(ui->pNewTagsEntry);
		StructCopyString(&pGroup->tags, pcNewTagString);
	}
}

static bool wleUIEditTagsDialogApplyAction(WleUIEditTagsWin *ui, wleUIEditTagsDialogApplyFunc apply_func)
{
	EditUndoStack *stack = edObjGetUndoStack();
	if(stack)
	{
		int i;

		EditUndoBeginGroup(stack);

		for ( i=0; i < eaSize(&ui->ppObjects); i++ )
		{
			EditorObject *obj = ui->ppObjects[i];
			if (obj->type->objType == EDTYPE_TRACKER)
			{
				GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
				GroupDef *pGroup = SAFE_MEMBER(tracker, def);
				//Trackers should still be editable since last we checked
				assertmsg(wleTrackerIsEditable(obj->obj, false, false, false), "Tracker has changed editable state.");
				if(pGroup)
				{
					wleOpPropsBegin(obj->obj);

					apply_func(pGroup, ui);

					wleOpPropsUpdate();
					wleOpPropsEnd();
				}
			}
		}

		EditUndoEndGroup(stack);
	}
	else
	{
		Errorf("Could Not Find Undo Stack");
		return false;
	}
	return true;
}

static void wleUIEditTagsDialogAppendCB(void *unused, WleUIEditTagsWin *ui)
{
	if(wleUIEditTagsDialogApplyAction(ui, wleUIEditTagsDialogAppend))
		wleUIEditTagsDialogCancel(NULL, ui);
}

static void wleUIEditTagsDialogRemoveCB(void *unused, WleUIEditTagsWin *ui)
{
	if(wleUIEditTagsDialogApplyAction(ui, wleUIEditTagsDialogRemove))
		wleUIEditTagsDialogCancel(NULL, ui);
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.EditTags");
void wleCmdEditTags(void)
{
	int i, y = 5;
	int edit_cnt = 0;
	WleUIEditTagsWin *ui;
	UIButton *pButton;

	ui = calloc(1, sizeof(*ui));

	//Get the selected objects
	wleAEGetSelectedObjects(&ui->ppObjects);

	//Make sure there are none that are not editable
	for ( i=0; i < eaSize(&ui->ppObjects); i++ )
	{
		EditorObject *obj = ui->ppObjects[i];
		if (obj->type->objType == EDTYPE_TRACKER)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = SAFE_MEMBER(tracker, def);
			if(!wleTrackerIsEditable(obj->obj, false, false, false))
			{
				Alertf("Your selection contains objects that are not editable.");
				wleUIEditTagsDialogCancel(NULL, ui);
				return;		
			}
			if(def)
				edit_cnt++;
		}
	}
	//Make sure we are editing at least one
	if(edit_cnt <= 0)
	{
		Alertf("You have no valid objects selected.");
		wleUIEditTagsDialogCancel(NULL, ui);
		return;
	}

	//Window
	{
		char pcWindowTitle[256];
		sprintf(pcWindowTitle, "Editing %d Object's Tags", edit_cnt);
		ui->pWindow = ui_WindowCreate(pcWindowTitle, 100, 100, 160, 55);
	}

	//Tag Selector
	wleFillTagsList(&ui->ppTagsList);
	ui->pNewTagsEntry = ui_TextEntryCreateWithStringMultiCombo("", 5, y, &ui->ppTagsList, true, true, false, false);
	ui_WidgetSetDimensionsEx(UI_WIDGET(ui->pNewTagsEntry), 1, 20, UIUnitPercentage, UIUnitFixed);
	ui->pNewTagsEntry->pchIndexSeparator = ", ";
	ui_WindowAddChild(ui->pWindow, ui->pNewTagsEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(ui->pNewTagsEntry)) + 5;

	//Append Button
	pButton = ui_ButtonCreate("Append", 5, y, wleUIEditTagsDialogAppendCB, ui);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 5, y, 0, 0, UITopRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 75);
	ui_WindowAddChild(ui->pWindow, pButton);
	//Remove Button
	pButton = ui_ButtonCreate("Remove", 5, y, wleUIEditTagsDialogRemoveCB, ui);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 85, y, 0, 0, UITopRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 75);
	ui_WindowAddChild(ui->pWindow, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + 5;

	//Finalize and open window
	ui_WindowSetCloseCallback(ui->pWindow, wleUIEditTagsDialogCancel, ui);
	ui_WindowAutoSetDimensions(ui->pWindow);
	ui_WidgetSetWidth(UI_WIDGET(ui->pWindow), 400);
	elUICenterWindow(ui->pWindow);
	ui_WindowSetModal(ui->pWindow, true);
	ui_WindowShow(ui->pWindow);
}

/********************
* MAIN
********************/

static TrackerHandle* wleAENameGetTrackerHandle()
{
	EditorObject **ppObjects = NULL;
	EditorObject *pRet;
	wleAEGetSelectedObjects(&ppObjects);
	assert(eaSize(&ppObjects) == 1);
	pRet = ppObjects[0];
	assert(pRet->type->objType == EDTYPE_TRACKER);
	eaDestroy(&ppObjects);
	return pRet->obj;
}

static void wleAENameGetScopes(GroupTracker *pTracker, WorldScope ***pppScopes)
{
	if(pTracker->closest_scope) {
		WorldScope *pWorldScope = pTracker->closest_scope;
		while(pWorldScope) {		
			if(pWorldScope->tracker != pTracker)
				eaPush(pppScopes, pWorldScope);
			pWorldScope = pWorldScope->parent_scope;
		}
	}
}

static WorldScope* wleAENameGetScopeFromIdx(int idx)
{
	WorldScope *pRet;
	WorldScope **ppScopes = NULL;
	EditorObject **ppObjects = NULL;
	GroupTracker *pTracker;

	wleAEGetSelectedObjects(&ppObjects);
	assert(eaSize(&ppObjects) == 1 && ppObjects[0]->type->objType == EDTYPE_TRACKER);
	pTracker = trackerFromTrackerHandle(ppObjects[0]->obj);
	wleAENameGetScopes(pTracker, &ppScopes);
	assert(idx >= 0 && idx < eaSize(&ppScopes));
	pRet = ppScopes[eaSize(&ppScopes)-idx-1];
	eaDestroy(&ppScopes);
	eaDestroy(&ppObjects);
	return pRet;
}

static void wleAENameChangedCB(MEField *pField, bool bFinished, void *pUnused)
{
	if(!bFinished || MEContextExists())
		return;
	if(!wleOpRename(wleAENameGetTrackerHandle(), g_wleAENameUI.pchDefName))
		wleOpRefreshUI();
}

static void wleAELogiclaNameChangedCB(MEField *pField, bool bFinished, UserData pUnused)
{
	char *pchNewName = NULL;
	TrackerHandle *handle;
	WorldScope *pScope;
	bool bSucceed = true;

	if(!bFinished || MEContextExists())
		return;

	assert(pField->arrayIndex >= 0 && pField->arrayIndex < eaSize(&g_wleAENameUI.ppchDefLogicalNames));
	pScope = wleAENameGetScopeFromIdx(pField->arrayIndex);

	if (g_wleAENameUI.ppchDefLogicalNames[pField->arrayIndex])
		pchNewName = strdup_removeWhiteSpace(g_wleAENameUI.ppchDefLogicalNames[pField->arrayIndex]);

	if (!pchNewName || !pchNewName[0]) {
		emStatusPrintf("The name cannot be set to an empty string!");
		bSucceed = false;
	} else if (!resIsValidName(pchNewName)) {
		// Name is not valid so fix it up
		char *estrFixedName = NULL;
		resFixName(pchNewName, &estrFixedName);
		handle = trackerHandleFromTracker(pScope->tracker);
		bSucceed = wleOpSetUniqueScopeName(handle, wleAENameGetTrackerHandle(), estrFixedName);
		estrDestroy(&estrFixedName);
		emStatusPrintf("The name contained invalid characters that were fixed automatically.");
	} else {
		// name is good so apply it
		handle = trackerHandleFromTracker(pScope->tracker);
		bSucceed = wleOpSetUniqueScopeName(handle, wleAENameGetTrackerHandle(), pchNewName);
	}

	if(!bSucceed)
		wleOpRefreshUI();

	SAFE_FREE(pchNewName);
}

static void wleAETagsChangedCB(MEField *pField, bool bFinished, void *pUnused)
{
	if(!bFinished || MEContextExists())
		return;
	if(!wleOpSetTags(wleAENameGetTrackerHandle(), g_wleAENameUI.pchDefTags))
		wleOpRefreshUI();
}

static bool wleAENameExclude(GroupTracker *pTracker)
{
	GroupTracker *pParent;
	g_wleAENameUI.pTracker = pTracker;
	if (!groupDefNeedsUniqueName(pTracker->def)) {
		g_wleAENameUI.bLogicalNameEnabled = false;
	} else {
		pParent = pTracker;
		while (pParent) {
			if (pParent->def && pParent->def->property_structs.curve) {
				g_wleAENameUI.bLogicalNameEnabled = false;
				break;
			}
			pParent = pParent->parent;
		}
	}
	return false;
}

int wleAENameReload(EMPanel *panel, EditorObject *edObj)
{
	int i, iLogicalNameCnt;
	WorldScope **ppScopes = NULL;
	GroupDef **ppDefs = NULL;
	GroupDef *pDef;
	MEFieldContext *pContext;
	U32 iRetFlags;
	GroupTracker *pTracker;

	g_wleAENameUI.pTracker = NULL;
	g_wleAENameUI.bLogicalNameEnabled = true;
	ppDefs = (GroupDef**)wleAEGetSelectedDataFromPath(NULL, wleAENameExclude, &iRetFlags);
	if(eaSize(&ppDefs) != 1)
		return WLE_UI_PANEL_INVALID;
	pDef = ppDefs[0];
	pTracker = g_wleAENameUI.pTracker;

	pContext = MEContextPush("WorldEditor_NameProps", &g_wleAENameUI, &g_wleAENameUI, parse_wleAENameUI);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	
	wleAENameGetScopes(pTracker, &ppScopes);
	iLogicalNameCnt = eaSize(&ppScopes);

	StructFreeString(g_wleAENameUI.pchDefName);
	g_wleAENameUI.pchDefName = StructAllocString(pTracker->def->name_str);
	pContext->cbChanged = wleAENameChangedCB;
	MEContextAddSimple(kMEFieldType_TextEntry, "DefName", "Group Name", "The name of the GroupDef.");

	if(iLogicalNameCnt > 0) {
		eaDestroyEx(&g_wleAENameUI.ppchDefLogicalNames, StructFreeString);

		for ( i=0; i < iLogicalNameCnt; i++ ) {
			MEFieldContextEntry *pEntry;
			WorldScope *pWorldScope = ppScopes[iLogicalNameCnt-i-1];
			char pchDispName[32];
			char pchToolTip[256];
			char *pchScopeName;

			if(i==0) {
				sprintf(pchDispName, "Layer Logical Name");
			} else if(i==1) {
				sprintf(pchDispName, "Logical Name");
			} else {
				sprintf(pchDispName, "Logical Name %d", i);
			}
			sprintf(pchToolTip, "The logical name used in events and choosers<br>Scope: %s", (pWorldScope->def ? pWorldScope->def->name_str : "Layer"));

			if (stashFindPointer(pWorldScope->obj_to_name, pTracker->enc_obj, &pchScopeName)) {
				eaPush(&g_wleAENameUI.ppchDefLogicalNames, StructAllocString(pchScopeName));
			} else {
				eaPush(&g_wleAENameUI.ppchDefLogicalNames, NULL);
			}

			pContext->cbChanged = wleAELogiclaNameChangedCB;
			pEntry = MEContextAddIndex(kMEFieldType_TextEntry, "DefLogicalNames", i, pchDispName, pchToolTip);
			MEContextSetActive(pEntry, g_wleAENameUI.bLogicalNameEnabled);
		}
	}

	StructFreeString(g_wleAENameUI.pchDefTags);
	g_wleAENameUI.pchDefTags = StructAllocString(pTracker->def->tags);
	pContext->cbChanged = wleAETagsChangedCB;
	MEContextAddSimple(kMEFieldType_TextEntry, "DefTags", "Tags", "Descriptive Tags for this object");

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	eaDestroy(&ppScopes);
	MEContextPop("WorldEditor_NameProps");

	return WLE_UI_PANEL_OWNED;
}

#include "autogen/WorldEditorNameAttributes_c_ast.c"

#endif
