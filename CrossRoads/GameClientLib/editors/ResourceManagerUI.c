#include "Cbox.h"

#include "EString.h"
#include "ResourceManagerUI.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "Color.h"
#include "UIComboBox.h"
#include "StringCache.h"
#include "GraphicsLib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/*
typedef struct CheckinWindow {
	UIWindow *window;
	UIList *list;
	UIButton *button_toggle_all;
	UIButton *button_toggle_selected;
	UIButton *button_checkin;
	UIButton *button_revert;

	ResourceActionList *pHolder;
	bool bInit;
} CheckinWindow;

CheckinWindow gCheckinWindow;

static bool closeCheckinWindow(UIAnyWidget *window, void *userData_UNUSED)
{
	ui_WindowHide(window);
	if (gCheckinWindow.pHolder)
	{
		StructDestroy(parse_ResourceActionList, gCheckinWindow.pHolder);
		gCheckinWindow.pHolder = NULL;
	}
	return true;
}

static void checkinListClickedCB(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, void *pUserData)
{
	if (iColumn == 0)
	{
		if (iRow >= 0 && iRow < eaSize(&gCheckinWindow.pHolder->ppActions))
		{
			gCheckinWindow.pHolder->ppActions[iRow]->bSelected = !gCheckinWindow.pHolder->ppActions[iRow]->bSelected;
		}
	}
	else
	{
		ui_ListCellClickedDefault(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pUserData);
	}
}

static void displayIsSelected(struct UIList *uiList, struct UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	AtlasTex *checkTex = (g_ui_Tex.checkBoxChecked);
	CBox box;
	F32 width = checkTex->width * scale;
	F32 height = checkTex->height * scale;
	F32 xPos = x + (col->fWidth*0.5f) - (width*0.5f);
	F32 yPos = y + (uiList->fRowHeight*0.5f) - (height*0.5f);

	BuildIntCBox(&box, xPos, yPos, width, height);
	if (gCheckinWindow.pHolder->ppActions[index]->bSelected)
		display_sprite_box((g_ui_Tex.checkBoxChecked), &box, z, 0xFFFFFFFF);
	else
		display_sprite_box((g_ui_Tex.checkBoxUnchecked), &box, z, 0xFFFFFFFF);
}

static void toggleAll(UIWidget *pWidget, void *pUnused)
{
	int i;
	for (i = 0; i < eaSize(&gCheckinWindow.pHolder->ppActions); i++)
	{
		gCheckinWindow.pHolder->ppActions[i]->bSelected = !gCheckinWindow.pHolder->ppActions[i]->bSelected;
	}
}

static void toggleSelected(UIWidget *pWidget, void *pUnused)
{
	int i;
	const int * const *peaiRows;
	peaiRows = ui_ListGetSelectedRows(gCheckinWindow.list);

	for (i = 0; i < eaiSize(peaiRows); i++)
	{
		int index = (*peaiRows)[i];
		gCheckinWindow.pHolder->ppActions[index]->bSelected = !gCheckinWindow.pHolder->ppActions[index]->bSelected;
	}

}

static void checkinChecked(UIWidget *pWidget, void *pUnused)
{
	ResourceActionList tempHolder = {0};
	int i;
	for (i = 0; i < eaSize(&gCheckinWindow.pHolder->ppActions); i++)
	{
		ResourceAction *pAction = gCheckinWindow.pHolder->ppActions[i];
		if (pAction->bSelected)
		{
			pAction->actionType = kResAction_Check_In;
			eaPush(&tempHolder.ppActions, pAction);
		}
	}

	if (eaSize(&tempHolder.ppActions))
	{
		resRequestResourceActions(&tempHolder);
	}
	eaDestroy(&tempHolder.ppActions);

	closeCheckinWindow(gCheckinWindow.window, NULL);

}

static void revertChecked(UIWidget *pWidget, void *pUnused)
{
	ResourceActionList tempHolder = {0};
	int i;
	for (i = 0; i < eaSize(&gCheckinWindow.pHolder->ppActions); i++)
	{
		ResourceAction *pAction = gCheckinWindow.pHolder->ppActions[i];
		if (pAction->bSelected)
		{
			pAction->actionType = kResAction_Undo_Checkout;
			eaPush(&tempHolder.ppActions, pAction);
		}
	}

	if (eaSize(&tempHolder.ppActions))
	{
		resRequestResourceActions(&tempHolder);
	}
	eaDestroy(&tempHolder.ppActions);

	closeCheckinWindow(gCheckinWindow.window, NULL);
}



void DisplayCheckinWindow(ResourceActionList *pHolder)
{
	if (!pHolder)
	{
		return;
	}
	if (gCheckinWindow.pHolder)
	{
		StructDestroy(parse_ResourceActionList, gCheckinWindow.pHolder);
	}
	gCheckinWindow.pHolder = StructClone(parse_ResourceActionList, pHolder);
	assert(gCheckinWindow.pHolder);
	if (!gCheckinWindow.bInit)
	{	
		UIListColumn *pColumn;
		gCheckinWindow.bInit = true;
		gCheckinWindow.window = ui_WindowCreate("Resource Status", 0, 0, 500, 300);
		ui_WindowSetResizable(gCheckinWindow.window, true);

		gCheckinWindow.list = ui_ListCreate(parse_ResourceAction, &gCheckinWindow.pHolder->ppActions, 16);
		ui_WidgetSetDimensionsEx(UI_WIDGET(gCheckinWindow.list), 1, 1, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(gCheckinWindow.list), 0, 0, 0, 40);
		ui_ListSetMultiselect(gCheckinWindow.list, 1);		

		pColumn = ui_ListColumnCreateCallback("", displayIsSelected, NULL);
		ui_ListColumnSetWidth(pColumn, false, 20);
		ui_ListAppendColumn(gCheckinWindow.list, pColumn);

		pColumn = ui_ListColumnCreateParseName("Status", "resStatus", NULL);
		ui_ListColumnSetWidth(pColumn, true, 1.0f);
		ui_ListColumnSetSortable(pColumn, true);
		ui_ListAppendColumn(gCheckinWindow.list, pColumn);

		pColumn = ui_ListColumnCreateParseName("Type", "resourceDict", NULL);
		ui_ListColumnSetWidth(pColumn, true, 1.0f);
		ui_ListColumnSetSortable(pColumn, true);
		ui_ListAppendColumn(gCheckinWindow.list, pColumn);

		pColumn = ui_ListColumnCreateParseName("Name", "resourceName", NULL);
		ui_ListColumnSetWidth(pColumn, true, 1.0f);
		ui_ListColumnSetSortable(pColumn, true);
		ui_ListAppendColumn(gCheckinWindow.list, pColumn);

		pColumn = ui_ListColumnCreateParseName("Scope", "resourceScope", NULL);
		ui_ListColumnSetWidth(pColumn, true, 1.0f);
		ui_ListColumnSetSortable(pColumn, true);
		ui_ListAppendColumn(gCheckinWindow.list, pColumn);

		ui_ListSetCellClickedCallback(gCheckinWindow.list, checkinListClickedCB, NULL);
		ui_ListSetSortedColumn(gCheckinWindow.list, 1);

		ui_WindowAddChild(gCheckinWindow.window, UI_WIDGET(gCheckinWindow.list));

		gCheckinWindow.button_toggle_all = ui_ButtonCreate("Toggle All", 0, 0, toggleAll, NULL);
		ui_WidgetSetWidth(UI_WIDGET(gCheckinWindow.button_toggle_all), 110);
		ui_WidgetSetPositionEx(UI_WIDGET(gCheckinWindow.button_toggle_all), 0, 0, 0, 0, UIBottomLeft);
		ui_WindowAddChild(gCheckinWindow.window, UI_WIDGET(gCheckinWindow.button_toggle_all));

		gCheckinWindow.button_toggle_selected = ui_ButtonCreate("Toggle Selected", 0, 0, toggleSelected, NULL);
		ui_WidgetSetWidth(UI_WIDGET(gCheckinWindow.button_toggle_selected), 110);
		ui_WidgetSetPositionEx(UI_WIDGET(gCheckinWindow.button_toggle_selected), 120, 0, 0, 0, UIBottomLeft);
		ui_WindowAddChild(gCheckinWindow.window, UI_WIDGET(gCheckinWindow.button_toggle_selected));

		gCheckinWindow.button_checkin = ui_ButtonCreate("Check In", 0, 0, checkinChecked, NULL);
		ui_WidgetSetWidth(UI_WIDGET(gCheckinWindow.button_checkin), 110);
		ui_WidgetSetPositionEx(UI_WIDGET(gCheckinWindow.button_checkin), 0, 0, 0, 0, UIBottomRight);
		ui_WindowAddChild(gCheckinWindow.window, UI_WIDGET(gCheckinWindow.button_checkin));

		gCheckinWindow.button_revert = ui_ButtonCreate("Revert", 0, 0, revertChecked, NULL);
		ui_WidgetSetWidth(UI_WIDGET(gCheckinWindow.button_revert), 110);
		ui_WidgetSetPositionEx(UI_WIDGET(gCheckinWindow.button_revert), 120, 0, 0, 0, UIBottomRight);
		ui_WindowAddChild(gCheckinWindow.window, UI_WIDGET(gCheckinWindow.button_revert));

		ui_WindowSetCloseCallback(gCheckinWindow.window, closeCheckinWindow, NULL);
	}
	else
	{
		ui_ListSetModel(gCheckinWindow.list, parse_ResourceAction, &gCheckinWindow.pHolder->ppActions);
	}
	ui_WindowShow(gCheckinWindow.window);
}

AUTO_RUN;
void RegisterResourceManagerUI(void)
{
	resRegisterResourceStatusDisplayCB(DisplayCheckinWindow);
}

AUTO_COMMAND;
void TestCheckinWindow(void)
{
	int i;
	ResourceActionList *pHolder = StructCreate(parse_ResourceActionList);
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo("SkyInfo");

	for (i = 0; i < eaSize(&pDictInfo->ppInfos); i++)
	{
		ResourceInfo *pInfo = pDictInfo->ppInfos[i];
		ResourceAction *pAction = StructCreate(parse_ResourceAction);
		pAction->bSelected = 1;
		StructCopy(parse_ResourceInfo, pInfo, &pAction->resInfo, 0, 0, 0);
		eaPush(&pHolder->ppActions, pAction);
	}

	DisplayCheckinWindow(pHolder);
	StructDestroy(parse_ResourceActionList, pHolder);
}

AUTO_COMMAND;
void GetResourceStatus(void)
{
	resRequestResourceStatusList();
}
*/



// Display previews

typedef struct PreviewDrawStruct
{
	resGetBasicTexture *pGetBasic;
	resGetAtlasTex *pGetAtlas;
	resClearTextures *pClearTex;
} PreviewDrawStruct;

StashTable gPreviewDrawStruct;

PreviewDrawStruct *GetPreviewDrawStruct(const char *pDictName)
{
	PreviewDrawStruct *pStruct;
	if (stashFindPointer(gPreviewDrawStruct, pDictName, &pStruct))
		return pStruct;
	return NULL;
}


void resRegisterPreviewCallback(DictionaryHandleOrName dictHandle, resGetBasicTexture *basicCB, resGetAtlasTex *atlasCB, resClearTextures *clearCB)
{
	PreviewDrawStruct *pStruct;
	const char *pDictName = resDictGetName(dictHandle);
	assertmsg(pDictName, "Invalid dict passed in");

	assertmsgf(!GetPreviewDrawStruct(pDictName),"Dictionary %s already has registered preview type!",pDictName);
	assertmsg((basicCB || atlasCB) && !(basicCB && atlasCB),"Must pass in exactly one CB");
	assert(clearCB);

	pStruct = calloc(sizeof(PreviewDrawStruct),1);
	pStruct->pGetAtlas = atlasCB;
	pStruct->pGetBasic = basicCB;
	pStruct->pClearTex = clearCB;

	if (!gPreviewDrawStruct)
	{
		gPreviewDrawStruct = stashTableCreateWithStringKeys(32, StashDefault);
	}

	assert(stashAddPointer(gPreviewDrawStruct, pDictName, pStruct, false));
}

AtlasTex *resGetAtlasTexFromResourceInfo(const ResourceInfo *pInfo)
{
	if (pInfo->resourceIcon)
		return atlasLoadTexture(pInfo->resourceIcon);
	return NULL;
}

void resFreePreviews(void)
{
	FOR_EACH_IN_STASHTABLE( gPreviewDrawStruct, PreviewDrawStruct, s ) {
		if( s->pClearTex ) {
			s->pClearTex();
		}
	} FOR_EACH_END;
}

#define MAX_SCALE 2.0

bool resDrawPreview(ResourceInfo *pInfo, const void* pExtraData, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 z, unsigned char alpha)
{
	F32 imageScale;
	CBox pBox = {0};
	PreviewDrawStruct *pStruct = GetPreviewDrawStruct(pInfo->resourceDict);

	// Only scale to 2x texture size, then center it
		
	if (!pStruct)
	{
		AtlasTex *tex = resGetAtlasTexFromResourceInfo(pInfo);

		if (tex)
		{		
			imageScale = MIN(w / tex->width, h / tex->height);
			imageScale = MIN(imageScale, MAX_SCALE);

			BuildCBoxFromCenter(&pBox, x + w/2, y + h/2, tex->width * imageScale, tex->height * imageScale);
			
			display_sprite_box(tex, &pBox, z, RGBAFromColor(ColorWhite));

			return true;
		}
		return false;
	}
	else if (pStruct->pGetAtlas)
	{
		AtlasTex *tex = NULL;
		Color modColor = ColorWhite;
		
		pStruct->pGetAtlas(pInfo, NULL, pExtraData, MAX(w,h), &tex, &modColor);

		if (tex)
		{				
			imageScale = MIN(w / tex->width, h / tex->height);
			imageScale = MIN(imageScale, MAX_SCALE);

			BuildCBoxFromCenter(&pBox, x + w/2, y + h/2, tex->width * imageScale, tex->height * imageScale);

			display_sprite_box(tex, &pBox, z, RGBAFromColor(modColor));

			return true;
		}
		return false;
	}
	else if (pStruct->pGetBasic)
	{
		BasicTexture *tex = NULL;
		Color modColor = ColorWhite;

		pStruct->pGetBasic(pInfo, NULL, pExtraData, MAX(w,h), &tex, &modColor);

		if (tex)
		{				
			imageScale = MIN(w / texWidth(tex), h / texHeight(tex));
			imageScale = MIN(imageScale, MAX_SCALE);

			BuildCBoxFromCenter(&pBox, x + w/2, y + h/2, texWidth(tex) * imageScale, texHeight(tex) * imageScale);

			display_sprite_box2(tex, &pBox, z, ColorRGBAMultiplyAlpha( RGBAFromColor(modColor), alpha ));

			return true;
		}
		return false;
	}
	
	return false;
}

bool resDrawResource(const char* dictName, void* resourceData, const void* pExtraData, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 z, unsigned char alpha)
{
	F32 imageScale;
	CBox pBox = {0};
	PreviewDrawStruct *pStruct = GetPreviewDrawStruct(dictName);

	// Only scale to 2x texture size, then center it
		
	if (!pStruct)
	{
		return false;
	}
	else if (pStruct->pGetAtlas)
	{
		AtlasTex *tex = NULL;
		Color modColor = ColorWhite;
		
		pStruct->pGetAtlas(NULL, resourceData, pExtraData, MAX(w,h), &tex, &modColor);

		if (tex)
		{				
			imageScale = MIN(w / tex->width, h / tex->height);
			imageScale = MIN(imageScale, MAX_SCALE);

			BuildCBoxFromCenter(&pBox, x + w/2, y + h/2, tex->width * imageScale, tex->height * imageScale);

			display_sprite_box(tex, &pBox, z, ColorRGBAMultiplyAlpha( RGBAFromColor(modColor), alpha ));

			return true;
		}
		return false;
	}
	else if (pStruct->pGetBasic)
	{
		BasicTexture *tex = NULL;
		Color modColor = ColorWhite;

		pStruct->pGetBasic(NULL, resourceData, pExtraData, MAX(w,h), &tex, &modColor);

		if (tex)
		{				
			imageScale = MIN(w / texWidth(tex), h / texHeight(tex));
			imageScale = MIN(imageScale, MAX_SCALE);

			BuildCBoxFromCenter(&pBox, x + w/2, y + h/2, texWidth(tex) * imageScale, texHeight(tex) * imageScale);

			display_sprite_box2(tex, &pBox, z, RGBAFromColor(modColor));

			return true;
		}
		return false;
	}
	
	return false;
}

ResourceDictionaryInfo **ppDictionaryComboModel;

static ResourceDictionaryInfo *findDictionaryInModel(const char *pCatName, const char *pDictName)
{
	int i;

	if (!pCatName && !pDictName)
	{
		return NULL;
	}
	for (i = 0; i < eaSize(&ppDictionaryComboModel); i++)
	{
		if ((!pCatName || ppDictionaryComboModel[i]->pDictCategoryName == pCatName) 
			&& ppDictionaryComboModel[i]->pDictName == pDictName)
		{
			return ppDictionaryComboModel[i];
		}
	}
	return NULL;
}


void resDictionaryComboSelectDictionary(UIComboBox *pCombo, const char *pDictName, bool bRunCallback)
{
	ResourceDictionaryInfo *pInfo = findDictionaryInModel(NULL, allocFindString(pDictName));
	if (pInfo)
	{
		if (bRunCallback)
		{
			ui_ComboBoxSetSelectedObjectAndCallback(pCombo, pInfo);
		}
		else
		{
			ui_ComboBoxSetSelectedObject(pCombo, pInfo);
		}
	}
	else
	{
		ui_ComboBoxSetSelected(pCombo, -1);
	}
}

static int sortDictionaryComboModel(const ResourceDictionaryInfo **a, const ResourceDictionaryInfo **b)
{
	const char *aName = NULL, *bName = NULL;
	int diff = stricmp( (*a)->pDictCategoryName, (*b)->pDictCategoryName );

	if (diff !=0)
		return diff;

	aName = resDictGetPluralDisplayName((*a)->pDictName);
	bName = resDictGetPluralDisplayName((*b)->pDictName);

	return stricmp( aName?aName:"", bName?bName:"" );
}

static void initDictionaryComboModel(void)
{
	ResourceDictionaryIterator resDictIterator;
	ResourceDictionaryInfo *pGlobDictInfo;
	
	resDictInitIterator(&resDictIterator);

	while ((pGlobDictInfo = resDictIteratorGetNextInfo(&resDictIterator)))
	{
		int iNumObjects = eaSize(&pGlobDictInfo->ppInfos);

		if (iNumObjects)
		{
			if (strcmpi(RESCATEGORY_DESIGN, pGlobDictInfo->pDictCategoryName) != 0 &&
				strcmpi(RESCATEGORY_ART, pGlobDictInfo->pDictCategoryName) != 0 &&
				strcmpi(RESCATEGORY_REFDICT, pGlobDictInfo->pDictCategoryName) != 0)
			{
				// Skip weird dictionaries
				continue;
			}
			if (!findDictionaryInModel(pGlobDictInfo->pDictCategoryName,NULL))
			{
				ResourceDictionaryInfo *pFakeInfo = StructCreate(parse_ResourceDictionaryInfo);
				pFakeInfo->pDictCategoryName = pGlobDictInfo->pDictCategoryName;
				eaPush(&ppDictionaryComboModel, pFakeInfo);
			}
			eaPush(&ppDictionaryComboModel, pGlobDictInfo);
		}
	}
	eaQSort(ppDictionaryComboModel, sortDictionaryComboModel);
}

static void resourceDictionaryComboMakeText(UIComboBox *combo, S32 row, bool inBox, void *unused, char **output)
{
	if (row == -1)
	{
		estrPrintf(output, "Select Type");
	}
	else
	{	
		ResourceDictionaryInfo *pInfo = ((ResourceDictionaryInfo**) *combo->model)[row];

		if (!pInfo->pDictName)
		{
			estrPrintf(output, "%s", pInfo->pDictCategoryName);
		}
		else
		{
			estrPrintf(output, "  %s", resDictGetPluralDisplayName(pInfo->pDictName));
		}
	}
}

UIComboBox *resCreateDictionaryComboBox(F32 x, F32 y, F32 w)
{
	UIComboBox *pCombo;

	if (!ppDictionaryComboModel)
	{
		initDictionaryComboModel();
	}

	pCombo = ui_ComboBoxCreate(x, y, w, parse_ResourceDictionaryInfo, &ppDictionaryComboModel, NULL);

	ui_ComboBoxSetTextCallback(pCombo, resourceDictionaryComboMakeText, NULL);

	return pCombo;
}
