/***************************************************************************



***************************************************************************/

#include "UIAutoWidget.h"

#include "mathutil.h"
#include "tokenstore.h"
#include "rgb_hsv.h"
#include "Expression.h"

#include "UIExpander.h"
#include "UIScrollbar.h"
#include "UISMFView.h"
#include "UISlider.h"
#include "UIComboBox.h"
#include "UITextEntry.h"
#include "UILabel.h"
#include "UICheckButton.h"
#include "UIColorButton.h"
#include "UISpinner.h"
#include "UITextureEntry.h"
#include "UIExpressionEntry.h"
#include "UIDictionaryEntry.h"

#include "AutoGen/UIAutoWidget_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););



void ui_RTNodeDestroy(UIRTNode *node, bool freeMe)
{
	int i;
	for (i=eaSize(&node->children)-1; i>=0; i--) {
		ui_RTNodeDestroy(node->children[i], true);
	}
	if (node->eaModel) {
		eaDestroy(node->eaModel);
		SAFE_FREE(node->eaModel);
	}
	eaDestroy(&node->children);
	if (freeMe) {
		SAFE_FREE(node->name);
		free(node);
	}
}

UIRebuildableTree *ui_RebuildableTreeCreate(void)
{
	UIRebuildableTree *ret = calloc(sizeof(*ret), 1);
	ret->root = NULL;
	return ret;
}

static void getFullName(UIRTNode *parent, const char *name_key, char *buf, size_t buf_size)
{
	// Could add verification here to make sure there's only one child with this name,
	//  but that is checked on create, so that's probably not necessary.
	if (parent->name && parent->name[0]) {
		sprintf_s(SAFESTR2(buf), "%s/%s", parent->name, name_key);
	} else {
		strcpy_s(SAFESTR2(buf), name_key);
	}
}

static UIRTNode *getOldNodeSub(UIRTNode *node, const char *fullname)
{
	int i;
	if (stricmp(node->name, fullname)==0 && node->widget1)
		return node;
	for (i=0; i<eaSize(&node->children); i++) { // Must do ascending
		UIRTNode *ret = getOldNodeSub(node->children[i], fullname);
		if (ret)
			return ret;
	}
	return NULL;
}

// Finds a node from the old tree
static UIRTNode *getOldNode(UIRebuildableTree *uirt, const char *fullname)
{
	// This could be made much faster
	if (uirt->old_root)
		return getOldNodeSub(uirt->old_root, fullname);
	else
		return NULL;
}

// detaches its widget from the old UI group
static UIWidget *getOldNodesWidget1(UIRTNode *node)
{
	UIWidget *ret = node->widget1;
	if (!ret)
		return NULL;
	ui_WidgetGroupRemove(ret->group, ret);
	node->widget1 = NULL;
	return ret;
}

static UIWidget *getOldNodesWidget2(UIRTNode *node)
{
	UIWidget *ret = node->widget2;
	if (!ret)
		return NULL;
	ui_WidgetGroupRemove(ret->group, ret);
	node->widget2 = NULL;
	return ret;
}

UIWidget *ui_RebuildableTreeGetOldWidget(UIRebuildableTree *uirt, const char *fullname)
{
	UIRTNode *node = getOldNode(uirt, fullname);
	if (node) {
		return getOldNodesWidget1(node);
	}
	return NULL;
}




static UIRTNode *getCurrentNodeNodeSub(UIRTNode *node, const char *fullname)
{
	int i;
	if (stricmp(node->name, fullname)==0 && node->widget1)
		return node;
	for (i=0; i<eaSize(&node->children); i++) { // Must do ascending
		UIRTNode *ret = getCurrentNodeNodeSub(node->children[i], fullname);
		if (ret)
			return ret;
	}
	return NULL;
}

// Finds a node from the old tree
static UIRTNode *getCurrentNode(UIRebuildableTree *uirt, const char *fullname)
{
	// This could be made much faster
	if (uirt->root)
		return getCurrentNodeNodeSub(uirt->root, fullname);
	else
		return NULL;
}

UIWidget *ui_RebuildableTreeGetWidgetByName(UIRebuildableTree *uirt, const char *fullname)
{
	UIRTNode *node = getCurrentNode(uirt, fullname);
	if (node)
		return node->widget1;
	return NULL;
}


void ui_RebuildableTreeDeinitForRebuild(UIRebuildableTree *uirt)
{
	assert(!uirt->old_root);
	assert(!uirt->old_scrollArea);

	// Must remove the root widget from it's parent group
	ui_WidgetGroupRemove(uirt->scrollArea->widget.group, UI_WIDGET(uirt->scrollArea));
	// Destroy the scrollArea?
	// Save the old tree so we can grab it later
	uirt->old_root = uirt->root;
	uirt->root = NULL;
	uirt->old_scrollArea = uirt->scrollArea;
	uirt->scrollArea = NULL;
}

static void ui_RebuildableTreeApplyFocusSub(UIRTNode *node )
{
	int i;
 	if (node->widget1 && ui_IsFocused(node->widget1)) {
 		ui_SetFocus(node->widget1);
 		return;
 	}
	for (i=eaSize(&node->children)-1; i>=0; i--) {
		ui_RebuildableTreeApplyFocusSub(node->children[i]);
	}
}


void ui_RebuildableTreeDoneBuilding(UIRebuildableTree *uirt)
{
	if (uirt->old_root) {
		ui_RTNodeDestroy(uirt->old_root, true);
		uirt->old_root = NULL;
	}
	if (uirt->old_scrollArea) {
		// Free old scrollArea
		ui_WidgetQueueFree(UI_WIDGET(uirt->old_scrollArea));
		uirt->old_scrollArea = NULL;
	}
	// Re-apply focus
	ui_RebuildableTreeApplyFocusSub(uirt->root);

	ui_RebuildableTreeReflow(uirt);
}


void ui_RebuildableTreeInit(UIRebuildableTree *ret, UIWidgetGroup *widget_group, F32 x, F32 y, U32 options)
{
	if (ret->scrollArea)
		ui_RebuildableTreeDeinitForRebuild(ret);
	assert(!ret->scrollArea);
	ret->scrollArea = ui_ScrollAreaCreate(x, y, 1.f, 1.f, 100, 100, !!(options & UIRTOptions_XScroll), !!(options & UIRTOptions_YScroll));
	if (ret->old_scrollArea) {
		// Grab the old one
		UIScrollbar *sb = ret->scrollArea->widget.sb;
		ret->scrollArea->widget.sb = ret->old_scrollArea->widget.sb;
		ret->old_scrollArea->widget.sb = sb;
	}
	ui_WidgetSetDimensionsEx(UI_WIDGET(ret->scrollArea),
		1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetGroupAdd(widget_group, UI_WIDGET(ret->scrollArea));
	if (!ret->root) {
		ret->root = calloc(sizeof(*ret->root), 1);
		ret->root->root = ret;
		ret->root->name = strdup("");
	}
	ret->root->x = ret->root->x0 = 0;
	ret->root->y = ret->root->y0 = 0;
	ret->root->h = 0;
	ret->root->groupWidget = UI_WIDGET(ret->scrollArea);
}

void ui_RebuildableTreeDestroy(UIRebuildableTree *uirt)
{
	if (uirt->root) {
		ui_RTNodeDestroy(uirt->root, false);
	}
	if (uirt->old_root) {
		ui_RTNodeDestroy(uirt->old_root, false);
	}
	uirt->scrollArea = NULL;
}

static void ui_RebuildableTreeReflowSub(UIRebuildableTree *uirt, UIRTNode *parent, UIRTNode *node)
{
	int i;
	// Set our height based on all children, then reflow our expander, updating the parent's x, y, h
	assert(!!parent == !!node->widget1);

	if (node->newline && parent) {
		parent->x = parent->x0;
		parent->y = parent->h;
	}
	if (node->widget1 && parent) {
		parent->x = MAX(parent->x, node->params.alignTo);
		if (node->expander)
		{
			if (parent->newline) // This check is intended to avoid indentation within scroll frames, etc.  good luck to me.
			{
				node->x = node->x0 = node->widget1->x = parent->x+UIAUTOWIDGET_INDENT;
			}
			else
			{
				node->x = node->x0 = node->widget1->x = parent->x;
			}
		}
		else
		{
			node->x = node->x0 = node->widget1->x = parent->x+30;
		}

		node->y = node->y0 = node->widget1->y = parent->y;
		if (node->widget2) {
			node->widget2->x = node->widget1->x + node->widget1->width + 2;
			node->widget2->y = parent->y;
		}
	} else {
		node->x = node->x0 = 0;
		node->y = node->y0 = 0;
	}

	if (node->children) { // Either the root or an expander
		// Update our height and place children
		node->x = node->x0 = 0;//parent?UIAUTOWIDGET_INDENT:0;
		node->y = 0;
		node->h = 0;
		for (i=0; i<eaSize(&node->children); i++) {
			ui_RebuildableTreeReflowSub(uirt, node, node->children[i]);
		}
	}
	if (node->expander) {
		// Our H field should be valid now
		node->expander->openedHeight = node->h;
		ui_ExpanderReflow(node->expander);
	}

	// node->widget.height is now valid
	if (node->widget1 && parent) {
		parent->x += node->widget1->width+2;
		parent->h = MAX(parent->h, parent->y + node->widget1->height);
		if (node->widget2) {
			parent->x += node->widget2->width+2;
			parent->h = MAX(parent->h, parent->y + node->widget2->height);
		}
		if (node->expander) {
			parent->y = parent->h+2;
		}
	}
}

void ui_RebuildableTreeReflow(UIRebuildableTree *uirt)
{
	ui_RebuildableTreeReflowSub(uirt, NULL, uirt->root);
	uirt->scrollArea->ySize = uirt->root->h;
	uirt->scrollArea->xSize = 600; // Hard-coded, perhaps we could detect the size
}

static void ui_RebuildableTreeReflowCallback(UIAnyWidget *widget_UNUSED, UIRebuildableTree *uirt)
{
	ui_RebuildableTreeReflow(uirt);
}

static bool nameExistsAtThisDepth(UIRTNode *parent, const char *name_key)
{
	int i;
	char fullname[1024];
	getFullName(parent, name_key, SAFESTR(fullname));
	for (i=eaSize(&parent->children)-1; i>=0; i--) {
		if (stricmp(parent->children[i]->name, name_key)==0)
			return true;
	}
	return false;
}

// name_key is not used for display, just for state tracking
UIRTNode *ui_RebuildableTreeAddWidget(UIRTNode *parent, UIWidget *widget1, UIWidget *widget2, bool newline, const char *name_key, UIAutoWidgetParams *params)
{
	UIRTNode *node;
	char fullname[1024];
	//assertmsg(parent != parent->root->root, "Cannot add anything other than Groups to the root");
	assert(parent->groupWidget);
	
	getFullName(parent, name_key, SAFESTR(fullname));
	assertmsgf(!nameExistsAtThisDepth(parent, name_key),
		"Two entries at the same depth with the same name (%s)!", fullname);

	node = calloc(sizeof(*node),1);
	node->root = parent->root;
	node->newline = newline;
	node->widget1 = widget1;
	node->widget2 = widget2;
	node->name = strdup(fullname);
	if (params)
		node->params = *params;

	ui_WidgetGroupAdd(&parent->groupWidget->children, widget1);
	if (widget2) {
		ui_WidgetGroupAdd(&parent->groupWidget->children, widget2);
		widget2->x = 0;
		widget2->y = 0;
	}
	node->x = node->x0 = widget1->x = 0;
	node->y = node->y0 = widget1->y = 0;
	eaPush(&parent->children, node);

	return node;
}

UIRTNode *ui_RebuildableTreeAddGroup(UIRTNode *parent, const char *name, const char *name_key, bool defaultOpen, const char *tooltip) // Sets open/closed on the expander
{
	int isOpen=0;
	char fullname[1024];
	const char *title;
	UIExpander *expander;
	UIRTNode *node=NULL;
	UIRTNode *oldNode;

	title = name;

	if (!name_key)
		name_key = name;

	getFullName(parent, name_key, SAFESTR(fullname));

	oldNode = getOldNode(parent->root, fullname);

	if (strStartsWith(name, USE_SMF_TAG)) {
		title = "";
	}
	expander = ui_ExpanderCreate(title, 1);
	if (oldNode && oldNode->expander) {
		isOpen = ui_ExpanderIsOpened(oldNode->expander);
		if (ui_IsFocused(oldNode->expander))
			ui_SetFocus(expander);
	} else {
		isOpen = defaultOpen;
	}
	if (isOpen) {
		ui_ExpanderSetOpened(expander, true);
	}
	if (tooltip)
	{
		ui_WidgetSetTooltipString(&expander->widget, tooltip);
	}

	if (strStartsWith(name, USE_SMF_TAG)) {
		// SMF
		UISMFView *smf_view;
		smf_view = ui_SMFViewCreate(0, 0, 10000, expander->widget.height);
		ui_SMFViewSetText(smf_view, name + strlen(USE_SMF_TAG), NULL);
		ui_SMFViewUpdateDimensions(smf_view);
		smf_view->widget.y = UI_HSTEP;
		ui_ExpanderAddLabel(expander, UI_WIDGET(smf_view));
	}

	ui_WidgetSetDimensionsEx(&expander->widget, 1.f, 0, UIUnitPercentage, UIUnitFixed);
	expander->expandF = ui_RebuildableTreeReflowCallback;
	expander->expandData = parent->root;
	node = ui_RebuildableTreeAddWidget(parent, (UIWidget*)expander, NULL, true, name_key, NULL);
	node->newline = true;
	node->expander = expander;
	node->groupWidget = UI_WIDGET(expander);
	SAFE_FREE(node->name);
	node->name = strdup(fullname); // Note: Expanders get the full name, children do not
	return node;
}

UIButton *ui_AutoWidgetAddButton(UIRTNode *parent, const char *name, UIActivationFunc clickedF, UserData clickedData, bool newline, const char *tooltip, UIAutoWidgetParams *params)
{
	UIButton *button;
	UIRTNode *node;
	UIRTNode *oldNode;
	char name_key[1024];
	char fullname[1024];
	bool changed = true;

	// Auto-gen name key for buttons since they have no important state this should be fine
	strcpy(name_key, name);
	while (nameExistsAtThisDepth(parent, name_key)) {
		incrementName(name_key, ARRAY_SIZE(name_key)-1);
	}

	getFullName(parent, name_key, SAFESTR(fullname));
	oldNode = getOldNode(parent->root, fullname);
	// If we have an old one, steal it and update it, otherwise make a new one
	if (oldNode) {
		button = (UIButton*)getOldNodesWidget1(oldNode);
	} else {
		button = NULL;
	}

	if (button) {
		ui_ButtonSetCallback(button, clickedF, clickedData);
		assert(!tooltip || stricmp(ui_WidgetGetTooltip(&button->widget), tooltip)==0); // Otherwise need to update it
	} else {
		if (strStartsWith(name, IMAGE_ONLY_TAG)) {
			button = ui_ButtonCreateImageOnly(name + strlen(IMAGE_ONLY_TAG), -1, -1, clickedF, clickedData);
		} else if (strStartsWith(name, USE_SMF_TAG)) {
			UISMFView *smf_view;
			int borderSize = 2;
			button = ui_ButtonCreate("", -1, -1, clickedF, clickedData);
			smf_view = ui_SMFViewCreate(borderSize, borderSize, 10000, -1);
			ui_SMFViewSetText(smf_view, name + strlen(USE_SMF_TAG), NULL);
			ui_ButtonAddChild(button, UI_WIDGET(smf_view));
			ui_SMFViewUpdateDimensions(smf_view);
			ui_WidgetSetWidth(UI_WIDGET(button), smf_view->widget.width + 2*borderSize);
		} else {
			button = ui_ButtonCreate(name, -1, -1, clickedF, clickedData);
		}
		if (tooltip)
		{
			ui_WidgetSetTooltipString(&button->widget, tooltip);
			//button->widget.tooltip = StructAllocString(tooltip);
		}
	}

	node = ui_RebuildableTreeAddWidget(parent, (UIWidget*)button, NULL, newline, name_key, params);
	return button;
}

static void dataChangedCallback(UIAnyWidget *widget_in, UIRTNode *node)
{
	// Update the data
	if (node->field) {
		bool bUpdateFromSimpleString=false;
		StructTokenType type = TOK_GET_TYPE(node->field->type);
		StructFormatOptions format = TOK_GET_FORMAT_OPTIONS(node->field->format);
		switch(type) {
		xcase TOK_STRUCT_X:
		{
			if (node->field->subtable == parse_Expression || node->field->subtable == parse_Expression_StructParam)
			{
				Expression *cur_expr = TokenStoreGetPointer(node->field, 0, node->structptr, node->index, NULL);
				const char *cur_text = exprGetCompleteString(cur_expr);
				const char *expr_text = ui_ExpressionEntryGetText((UIExpressionEntry *)node->widget1);
				
				if (expr_text && !expr_text[0])
					expr_text = NULL;

				if ((!cur_text && !expr_text) ||
					(cur_text && expr_text && strcmp(cur_text, expr_text) == 0))
					return; // Nothing changed so do nothing

				if (!cur_expr && expr_text)
				{
					cur_expr = exprCreate();
				}
				else if (cur_expr && !expr_text)
				{
					exprDestroy(cur_expr);
					cur_expr = NULL;
				}

				if (cur_expr && node->params.exprContext)
					exprGenerateFromString(cur_expr, node->params.exprContext, expr_text, NULL);

				TokenStoreSetPointer(node->field, 0, node->structptr, node->index, cur_expr, NULL);
			}
		}
		xcase TOK_STRING_X:
		case TOK_FILENAME_X:
		case TOK_MULTIVAL_X:
		case TOK_REFERENCE_X:
		{
			bUpdateFromSimpleString = true;
		}
		xcase TOK_INT_X:
		case TOK_INT64_X:
		case TOK_INT16_X:
		case TOK_U8_X:
		{
			if (format == TOK_FORMAT_FLAGS)
			{
				bool value = ui_CheckButtonGetState((UICheckButton*)node->widget1);
				int oldvalue = TokenStoreGetInt(node->field, 0, node->structptr, node->index, NULL);
				int newvalue = oldvalue;
				assert(node->fieldKey);
				if (value) {
					newvalue |= node->fieldKey->value;
				} else {
					newvalue &= ~node->fieldKey->value;
				}
				if (newvalue == oldvalue)
					return; // No change so do nothing
				TokenStoreSetInt(node->field, 0, node->structptr, node->index, newvalue, NULL, NULL);
			}
			else
			{
				if (node->field->subtable) {
					int newvalue = ui_ComboBoxGetSelectedEnum((UIComboBox*)node->widget1);
					int oldvalue = TokenStoreGetIntAuto(node->field, 0, node->structptr, node->index, NULL);
					if (newvalue == oldvalue) 
						return; // No change so do nothing
					TokenStoreSetIntAuto(node->field, 0, node->structptr, node->index, newvalue, NULL, NULL);
				} else {
					bUpdateFromSimpleString = true;
				}
			}
		}
		xcase TOK_F32_X:
		case TOK_QUATPYR_X: // This probably needs a special widget to be useful
		{
			if (format == TOK_FORMAT_HSV) {
				Vec4 v4;
				ui_ColorButtonGetColor((UIColorButton*)node->widget1, v4);
				rgbToHsv(v4, v4);
				TokenStoreSetF32(node->field, 0, node->structptr, 0, v4[0], NULL, NULL);
				TokenStoreSetF32(node->field, 0, node->structptr, 1, v4[1], NULL, NULL);
				TokenStoreSetF32(node->field, 0, node->structptr, 2, v4[2], NULL, NULL);
				if (TokenStoreGetNumElems(node->field, 0, node->structptr, NULL) > 3)
					TokenStoreSetF32(node->field, 0, node->structptr, 3, v4[3], NULL, NULL);
			} else {
				bUpdateFromSimpleString = true;
			}
		}
		xcase TOK_BOOLFLAG_X:
		{
			bool newvalue = ui_CheckButtonGetState((UICheckButton*)node->widget1);
			bool oldvalue = TokenStoreGetU8(node->field, 0, node->structptr, node->index, NULL);
			if (!!newvalue == !!oldvalue) 
				return; // No change so do nothing
			TokenStoreSetU8(node->field, 0, node->structptr, node->index, newvalue, NULL, NULL);
		}
		xcase TOK_BIT:
		{
			bool newvalue = ui_CheckButtonGetState((UICheckButton*)node->widget1);
			bool oldvalue = TokenStoreGetBit(node->field, 0, node->structptr, node->index, NULL);
			if (!!newvalue == !!oldvalue)
				return; // No change so do nothing
			TokenStoreSetBit(node->field, 0, node->structptr, node->index, newvalue, NULL, NULL);
		}
	/*	xcase TOK_FLAGS_X:
		{
			bool value = ui_CheckButtonGetState((UICheckButton*)node->widget1);
			int oldvalue = TokenStoreGetInt(node->field, 0, node->structptr, node->index, NULL);
			int newvalue = oldvalue;
			assert(node->fieldKey);
			if (value) {
				newvalue |= node->fieldKey->value;
			} else {
				newvalue &= ~node->fieldKey->value;
			}
			if (newvalue == oldvalue)
				return; // No change so do nothing
			TokenStoreSetInt(node->field, 0, node->structptr, node->index, newvalue, NULL, NULL);
		}*/
		xdefault:
		{
			assertmsg(0, "TODO: data type not handled");
		}
		}
		if (bUpdateFromSimpleString) {
			if (node->params.type == AWT_Slider)
			{
				F32 fValue = ui_FloatSliderGetValue((UISlider*)node->widget1);
				char oldbuf[1024];
				char newbuf[1024];

				sprintf(newbuf, "%g", fValue);

				if (!FieldToSimpleString(node->field, 0, node->structptr, node->index, oldbuf, 1024, false)) {
					// Invalid data?
				}
				if (strcmp(oldbuf, newbuf) == 0)
					return; // No change so do nothing

				if (!FieldFromSimpleString(node->field, 0, node->structptr, node->index, newbuf)) {
					// Invalid format
					assert(0);
				}
			} else if (node->params.type == AWT_Spinner) {
				F32 fValue = 0.0f;
				char oldbuf[1024];
				char newbuf[1024];
				if (widget_in == node->widget1) {
					// text changed
					const char *text = ui_TextEntryGetText((UITextEntry*)node->widget1);
					if(sscanf(text, "%f", &fValue) > 0)
						ui_SpinnerSetValue((UISpinner*)node->widget2, fValue);
				}

				fValue = ui_SpinnerGetValue((UISpinner*)node->widget2);
				if (node->params.precision > 0)
					sprintf(newbuf, "%.*f", node->params.precision, fValue);
				else
					sprintf(newbuf, "%g", fValue);

				ui_TextEntrySetText((UITextEntry*)node->widget1, newbuf);

				if (!FieldToSimpleString(node->field, 0, node->structptr, node->index, oldbuf, 1024, false)) {
					// Invalid data?
				}

				if (strcmp(oldbuf, newbuf) == 0)
					return; // No change so do nothing

				if (!FieldFromSimpleString(node->field, 0, node->structptr, node->index, newbuf)) {
					// Invalid format
					assert(0);
				}
			} else if (node->params.type == AWT_TextureEntry) {
				const char *newstr = ui_TextureEntryGetTextureName((UITextureEntry*)node->widget1);
				char oldbuf[1024];

				if (!FieldToSimpleString(node->field, 0, node->structptr, node->index, oldbuf, 1024, false)) {
					// Invalid data?
				}

				if ((!newstr || !newstr[0]) && !oldbuf[0])
					return; // No change so do nothing

				if (newstr)
				{
					ANALYSIS_ASSUME(newstr != NULL);
					if (strcmp(oldbuf, newstr) == 0)
						return; // No change so do nothing
				}
				if (!FieldFromSimpleString(node->field, 0, node->structptr, node->index, newstr ? newstr : "")) {
					// Invalid format
					assert(0);
				}
			} else if (node->params.type == AWT_ColorPicker) {
				int i;
				Vec4 value;
				ui_ColorButtonGetColor((UIColorButton*)node->widget1, value);
				for (i=0; i<(((UIColorButton*)node->widget1)->noAlpha?3:4); i++)
					TokenStoreSetF32(node->field, 0, node->structptr, i, value[i], NULL, NULL);
			} else {
				const char *newstr = NULL;
				char oldbuf[4096];

				if (node->params.type == AWT_DictionaryTextEntry)
					newstr = ui_DictionaryEntryGetText((UIDictionaryEntry*)node->widget1);
				else
					newstr = ui_TextEntryGetText((UITextEntry*)node->widget1);

				if (!FieldToSimpleString(node->field, 0, node->structptr, node->index, oldbuf, 4096, false)) {
					// Invalid data?
				}
				if (((!newstr || !newstr[0]) && !oldbuf[0]) || (newstr && strcmp(oldbuf, newstr) == 0))
					return; // No change so do nothing

				if (!FieldFromSimpleString(node->field, 0, node->structptr, node->index, newstr)) {
					// Invalid format?
				}
				if (type == TOK_F32_X && node->params.precision > 0)
				{
					F32 fValue = atof(newstr);
					char buf[1024];
					sprintf(buf, "%.*f", node->params.precision, fValue);
					ui_TextEntrySetText((UITextEntry*)node->widget1, buf);
				}
			}
		}
	}

	// Call user callback if appropriate
	if (node->callbackF)
		node->callbackF(node, node->callbackData);
}

void dataChangedCallback2(UIAnyWidget *widget_UNUSED, int val_UNUSED, UIRTNode *node)
{
	dataChangedCallback(widget_UNUSED, node);
}

void dataChangedCallback3(UIAnyWidget *widget_UNUSED, bool val_UNUSED, UIRTNode *node)
{
	dataChangedCallback(widget_UNUSED, node);
}

UIAutoWidgetParams *ui_AutoWidgetParamsFromString(const char *str)
{
	static UIAutoWidgetParams params;
	int ret;
	StructDeInit(parse_UIAutoWidgetParams, &params);
	ZeroStruct(&params);
	ret = ParserReadText(str, parse_UIAutoWidgetParams, &params, 0);
	assert(ret);
	return &params;
}

UILabel *ui_RebuildableTreeAddLabel(UIRTNode *parent, const char *labelText, UIAutoWidgetParams *params, bool newline)
{
	if (!labelText)
		labelText = "";
	return ui_RebuildableTreeAddLabelKeyed(parent, labelText, labelText, params, newline);
}

UILabel *ui_RebuildableTreeAddLabelWithTooltip(UIRTNode *parent, const char *labelText, const char* tooltipText, UIAutoWidgetParams *params, bool newline)
{
	if (!labelText)
		labelText = "";
	return ui_RebuildableTreeAddLabelKeyedWithTooltip(parent, labelText, labelText, tooltipText, params, newline);
}

UILabel *ui_RebuildableTreeAddLabelKeyed(UIRTNode *parent, const char *labelText, const char *labelKey, UIAutoWidgetParams *params, bool newline)
{
	UILabel *label;
	if (!labelText)
		labelText = "";
	ui_RebuildableTreeAddWidget(parent, (UIWidget *)(label=ui_LabelCreate(labelText, 0, 0)), NULL, newline, labelKey, params);
//	label->widget.height -= UI_STEP;
	return label;
}

UILabel *ui_RebuildableTreeAddLabelKeyedWithTooltip(UIRTNode *parent, const char *labelText, const char* tooltipText, const char *labelKey, UIAutoWidgetParams *params, bool newline)
{
	UILabel *label;
	if (!labelText)
		labelText = "";
	ui_RebuildableTreeAddWidget(parent, (UIWidget *)(label=ui_LabelCreate(labelText, 0, 0)), NULL, newline, labelKey, params);
	if(label && tooltipText) {
		ui_WidgetSetTooltipString(UI_WIDGET(label), tooltipText);
		ui_LabelEnableTooltips(label);
	}
	return label;
}

UIWidget *ui_AutoWidgetAdd(UIRTNode *parent, ParseTable *table, const char *fieldName, void *structptr, bool newline, RTNodeDataChangedCallback onDataChangedCallback, UserData userData, UIAutoWidgetParams *params, const char *tooltip)
{
	return ui_AutoWidgetAddKeyed(parent, table, fieldName, NULL, structptr, newline, onDataChangedCallback, userData, params, tooltip);
}

UIWidget *ui_AutoWidgetAddKeyed(UIRTNode *parent, ParseTable *table, const char *fieldName, const char *name_key, void *structptr, bool newline, RTNodeDataChangedCallback onDataChangedCallback, UserData userData, UIAutoWidgetParams *params, const char *tooltip)
{
	int row;
	ParseTable *field;
	StructTokenType type;
	UIWidget *widget = NULL, *widget2 = NULL;
	UIRTNode *node=NULL;
	bool bNeedLabel=true;
	int numElems;
	int index;
	char myname[1024];
	char fieldNameKey[1024]; // In the case of Field|SubField, this gets "Field"
	char *fieldNameSub=NULL; // "										 "SubField"
	F32 fValue;
	UIAutoWidgetParams params_local = {0};
	StructFormatOptions format;

	if (params)
		params_local = *params;

	if (!name_key)
		name_key = fieldName;

	strcpy(fieldNameKey, fieldName);
	if ( (fieldNameSub = strrchr(fieldNameKey, '|')) ) {
		*fieldNameSub = '\0';
		fieldNameSub++;
	}

	if (!ParserFindColumn(table, fieldNameKey, &row)) {
		assertmsgf(0, "Invalid field name passed to %s", __FUNCTION__);
		return NULL;
	}
	


	field = &table[row];

	if (field->type & TOK_USEDFIELD)
	{
		return NULL;
	}

	type = TOK_GET_TYPE(field->type);
	format = TOK_GET_FORMAT_OPTIONS(field->format);

	if (type == TOK_START ||
		type == TOK_END ||
		type == TOK_IGNORE ||
		type == TOK_COMMAND ||
		type == TOK_CURRENTFILE_X ||
		type == TOK_POLYMORPH_X)
	{
		// Not supported, and/or nothing to do!
		return NULL;
	}

	if (type == TOK_BOOLFLAG_X || format == TOK_FORMAT_FLAGS || type == TOK_BIT)
	{
		bNeedLabel = false;
	}

	if (bNeedLabel && !params_local.NoLabel) {
		// Label
		sprintf(myname, "%s_Label", name_key);
		ui_RebuildableTreeAddWidget(parent, widget = (UIWidget *)ui_LabelCreate(params_local.labelName ? params_local.labelName : field->name, 0, 0), NULL, newline, myname, NULL);
		if (params_local.disabled)
			ui_SetActive(widget, false);
		else 
			ui_SetActive(widget, true);
		widget = NULL;
		newline = false;
	}

	numElems = TokenStoreGetNumElems(field, 0, structptr, NULL);

	if (format == TOK_FORMAT_HSV || params_local.type == AWT_ColorPicker)
		numElems = 1;

	for (index=0; index < numElems; index++)
	{
		char fullname[1024];
		UIRTNode *oldNode;
		bool bNeedSimpleTextBox = false;
		if (index==0) {
			strcpy(myname, name_key);
		} else {
			sprintf(myname, "%s_%d", name_key, index);
		}
		getFullName(parent, myname, SAFESTR(fullname));
		oldNode = getOldNode(parent->root, fullname);
		// If we have an old one, steal it and update it, otherwise make a new one
		if (oldNode) {
			widget = getOldNodesWidget1(oldNode);
			widget2 = getOldNodesWidget2(oldNode);
		} else {
			widget = NULL;
			widget2 = NULL;
		}


		switch(type) {
		xcase TOK_STRUCT_X:
		{
			if (field->subtable == parse_Expression || field->subtable == parse_Expression_StructParam)
			{
				Expression *cur_expr = TokenStoreGetPointer(field, 0, structptr, index, NULL);
				if (!widget) {
					widget = (UIWidget *)ui_ExpressionEntryCreate("", params_local.exprContext);
					if ((params_local.overrideWidth > 0.0) && (params_local.overrideWidth <= 1.0))
					{
						ui_WidgetSetWidthEx(widget, params_local.overrideWidth, UIUnitPercentage);
					}
					else
					{
						ui_WidgetSetWidth(widget, params_local.overrideWidth?params_local.overrideWidth:150);
					}
				}

				node = ui_RebuildableTreeAddWidget(parent, widget, NULL, newline, myname, &params_local);
				ui_ExpressionEntrySetText((UIExpressionEntry *)widget, cur_expr?exprGetCompleteString(cur_expr):"");
				ui_ExpressionEntrySetChangedCallback((UIExpressionEntry *)widget, dataChangedCallback, node);
			}
			else
			{
				int i;
				UIAutoWidgetParams new_params_local = params_local;
				ParseTable *subtable = field->subtable;
				void *child_struct = TokenStoreGetPointer(field, 0, structptr, index, NULL);
				new_params_local.NoLabel = true;
				FORALL_PARSETABLE(subtable, i)
				{
					ui_AutoWidgetAdd(parent, subtable, subtable[i].name, child_struct, false, onDataChangedCallback, userData, &new_params_local, tooltip);
				}
			}
		}
		xcase TOK_STRING_X:
		case TOK_FILENAME_X:
		case TOK_MULTIVAL_X:
		{
			bNeedSimpleTextBox = true;
		}
		xcase TOK_INT_X:
		case TOK_INT64_X:
		case TOK_INT16_X:
		case TOK_U8_X:
		{
			if (format == TOK_FORMAT_FLAGS)
			{			
				// Create a CheckButton for the specified flag
				int value = TokenStoreGetInt(field, 0, structptr, index, NULL);
				StaticDefineInt *flag_list = (StaticDefineInt *)field->subtable;
				assertmsg(fieldNameSub, "Flags type must have a field name of \"Field|Flag\", or call ui_AutoWidgetAddAllFlags");
				assert(flag_list->key == (char*)DM_INT);
				flag_list++;
				while (flag_list->key!=(char*)DM_END) {
					if (flag_list->key==(char*)DM_TAILLIST) {
						flag_list = (StaticDefineInt *)flag_list->value;
						continue;
					}
					if (flag_list->value == 0) {
						flag_list++;
						continue;
					}
					if (stricmp(flag_list->key, fieldNameSub)==0) {
						if (!widget) {
							widget = (UIWidget*)ui_CheckButtonCreate(0, 0, params_local.NoLabel ? "" : fieldNameSub, false);
						}
						node = ui_RebuildableTreeAddWidget(parent, widget, NULL, newline, myname, &params_local);
						ui_CheckButtonSetToggledCallback((UICheckButton*)widget, dataChangedCallback, node);
						ui_CheckButtonSetStateAndCallback((UICheckButton*)widget, (value & flag_list->value) == flag_list->value);
						node->fieldKey = flag_list;
						break;
					}
					flag_list++;
				}		
			}
			else
			{

				int value = TokenStoreGetIntAuto(field, 0, structptr, index, NULL);
				fValue = (F32)value;
				if (field->subtable) {
					// combo box
					if (!widget) {
						widget = (UIWidget*)ui_ComboBoxCreate(0, 0, params_local.overrideWidth?params_local.overrideWidth:100, NULL, NULL, NULL);
						if ((params_local.overrideWidth > 0.0) && (params_local.overrideWidth <= 1.0))
						{
							ui_WidgetSetWidthEx(widget, params_local.overrideWidth, UIUnitPercentage);
						}
						ui_ComboBoxSetEnum((UIComboBox*)widget, field->subtable, NULL, NULL);
					}
					node = ui_RebuildableTreeAddWidget(parent, widget, NULL, newline, myname, &params_local);
					ui_ComboBoxSetSelectedEnumCallback((UIComboBox*)widget, dataChangedCallback2, node);
					ui_ComboBoxSetSelectedEnum((UIComboBox*)widget, value);
				} else {
					bNeedSimpleTextBox = true;
				}
			}
		}
		xcase TOK_F32_X:
		case TOK_QUATPYR_X: // This probably needs a special widget to be useful
		{
			if (format == TOK_FORMAT_HSV) {
				Vec4 v4;
				int numHSVElems = TokenStoreGetNumElems(field, 0, structptr, NULL);
				if (!widget) {
					UIColorButton *cbutton;
					cbutton = ui_ColorButtonCreateEx(0, 0, -15, 15, zerovec4);
					cbutton->parentGroup = params_local.parentGroup;
					cbutton->noAlpha = numHSVElems < 4 || !params_local.hsvAddAlpha;
					cbutton->noRGB = true;
					cbutton->liveUpdate = true;
					widget = UI_WIDGET(cbutton);
					if ((params_local.overrideWidth > 0.0) && (params_local.overrideWidth <= 1.0))
					{
						ui_WidgetSetWidthEx(widget, params_local.overrideWidth, UIUnitPercentage);
					}
					else if (params_local.overrideWidth)
					{
						ui_WidgetSetWidth(widget, params_local.overrideWidth);
					}
				}
				v4[0] = TokenStoreGetF32(field, 0, structptr, 0, NULL);
				v4[1] = TokenStoreGetF32(field, 0, structptr, 1, NULL);
				v4[2] = TokenStoreGetF32(field, 0, structptr, 2, NULL);
				v4[3] = numHSVElems < 4 ? 1 : TokenStoreGetF32(field, 0, structptr, 3, NULL);
				hsvToRgb(v4, v4);
				node = ui_RebuildableTreeAddWidget(parent, widget, NULL, newline, myname, &params_local);
				ui_ColorButtonSetChangedCallback((UIColorButton*)widget, dataChangedCallback3, node);
				ui_ColorButtonSetColorAndCallback((UIColorButton*)widget, v4);
			} else {
				fValue = TokenStoreGetF32(field, 0, structptr, index, NULL);
				bNeedSimpleTextBox = true;
			}
		}
		xcase TOK_BOOLFLAG_X:
		{
			int value = TokenStoreGetU8(field, 0, structptr, index, NULL);
			if (!widget) {
				widget = (UIWidget*)ui_CheckButtonCreate(0, 0, params_local.NoLabel ? "" : field->name, false);
			}
			node = ui_RebuildableTreeAddWidget(parent, widget, NULL, newline, myname, &params_local);
			ui_CheckButtonSetToggledCallback((UICheckButton*)widget, dataChangedCallback, node);
			ui_CheckButtonSetStateAndCallback((UICheckButton*)widget, value);
		}
		xcase TOK_BIT:
		{
			int value = TokenStoreGetBit(field, 0, structptr, index, NULL);
			if (!widget) {
				widget = (UIWidget*)ui_CheckButtonCreate(0, 0, params_local.NoLabel ? "" : field->name, false);
			}
			node = ui_RebuildableTreeAddWidget(parent, widget, NULL, newline, myname, &params_local);
			ui_CheckButtonSetToggledCallback((UICheckButton*)widget, dataChangedCallback, node);
			ui_CheckButtonSetStateAndCallback((UICheckButton*)widget, value);
		}
/*		xcase TOK_FLAGS_X:
		{
			// Create a CheckButton for the specified flag
			int value = TokenStoreGetInt(field, 0, structptr, index, NULL);
			StaticDefineInt *flag_list = (StaticDefineInt *)field->subtable;
			assertmsg(fieldNameSub, "Flags type must have a field name of \"Field|Flag\", or call ui_AutoWidgetAddAllFlags");
			assert(flag_list->key == (char*)DM_INT);
			flag_list++;
			while (flag_list->key!=(char*)DM_END) {
				if (flag_list->key==(char*)DM_TAILLIST) {
					flag_list = (StaticDefineInt *)flag_list->value;
					continue;
				}
				if (flag_list->value == 0) {
					flag_list++;
					continue;
				}
				if (stricmp(flag_list->key, fieldNameSub)==0) {
					if (!widget) {
						widget = (UIWidget*)ui_CheckButtonCreate(0, 0, params_local.NoLabel ? "" : fieldNameSub, false);
					}
					node = ui_RebuildableTreeAddWidget(parent, widget, NULL, newline, myname, &params_local);
					ui_CheckButtonSetToggledCallback((UICheckButton*)widget, dataChangedCallback, node);
					ui_CheckButtonSetStateAndCallback((UICheckButton*)widget, (value & flag_list->value) == flag_list->value);
					node->fieldKey = flag_list;
					break;
				}
				flag_list++;
			}
		}*/
		xcase TOK_REFERENCE_X:
		{
			bNeedSimpleTextBox = true;
			params_local.type = AWT_DictionaryTextEntry;
			params_local.dictionary = field->subtable;
			params_local.parseTable = NULL;
			params_local.parseNameField = NULL;
		}
		xdefault:
		{
			assertmsg(0, "TODO: data type not handled");
			// Options for unhandled data types:
			//   1. Do not call AutoWidgetAdd with them
			//   2. Add them to this case statement, create a widget, and to the dataChangedCallback case statement as well
			//   3. Add them to the not supported/nothing to do list above
		}
		}
		if (bNeedSimpleTextBox) {
			if (params_local.type == AWT_Slider)
			{
				F64 min=0, max=0, step=0;
				F32 fMin = params_local.min[CLAMP(index, 0, 2)], fMax = params_local.max[CLAMP(index, 0, 2)], fStep = params_local.step[CLAMP(index, 0, 2)];
				if (fMax == fMin && fMax == 0)
					fMax = 1;
				if (!widget) {
					widget = (UIWidget*)ui_FloatSliderCreate(0, 0, 100, fMin, fMax, fValue);
				}
				node = ui_RebuildableTreeAddWidget(parent, widget, NULL, newline, myname, &params_local);
				ui_FloatSliderSetValueAndCallbackEx((UISlider*)widget, 0, fValue, 0);
				min = (F64)fMin;
				max = (F64)fMax;
				step = (F64)fStep;
				ui_SliderSetRange((UISlider*)widget, min, max, step);
				ui_SliderSetChangedCallback((UISlider*)widget, dataChangedCallback3, node);
				ui_SliderSetPolicy((UISlider*)widget, UISliderContinuous);
			} else if (params_local.type == AWT_Spinner) {
				char text[1024];
				F32 fMin = params_local.min[CLAMP(index, 0, 2)], fMax = params_local.max[CLAMP(index, 0, 2)], fStep = params_local.step[CLAMP(index, 0, 2)];

				text[0] = 0;

				if (!widget) {
					widget = (UIWidget*)ui_TextEntryCreate("0", 0, 0);
					ui_TextEntrySetSelectOnFocus((UITextEntry*)widget, true);
					if ((params_local.overrideWidth > 0.0) && (params_local.overrideWidth <= 1.0))
					{
						ui_WidgetSetWidthEx(widget, params_local.overrideWidth, UIUnitPercentage);
					}
					else if (params_local.overrideWidth)
					{
						ui_WidgetSetWidth(widget, params_local.overrideWidth);
					}
					widget->width -= 14;
				}
				if (!widget2) {
					widget2 = (UIWidget*)ui_SpinnerCreate(0, 0, fMin, fMax, fStep, fValue, NULL, NULL);
					ui_SpinnerSetStartSpinCallback((UISpinner*) widget2, params_local.spinnerStartF, params_local.spinnerStartData);
					ui_SpinnerSetStopSpinCallback((UISpinner*) widget2, params_local.spinnerStopF, params_local.spinnerStopData);
					widget2->height = widget->height;
					widget2->width = 12;
				}

				node = ui_RebuildableTreeAddWidget(parent, widget, widget2, newline, myname, &params_local);
				
				ui_TextEntrySetFinishedCallback((UITextEntry*)widget, dataChangedCallback, node);
				ui_SpinnerSetChangedCallback((UISpinner*)widget2, dataChangedCallback, node);

				((UISpinner*)widget2)->min = fMin;
				((UISpinner*)widget2)->max = fMax;
				((UISpinner*)widget2)->step = fStep;

				if (params_local.precision > 0)
					sprintf(text, "%.*f", params_local.precision, fValue);
				else
					sprintf(text, "%g", fValue);
				ui_TextEntrySetText((UITextEntry*)widget, text);
				ui_SpinnerSetValue((UISpinner*)widget2, fValue);
			} else if (params_local.type == AWT_TextureEntry) {
				char text[1024];
				text[0] = 0;
				if (!FieldToSimpleString(field, 0, structptr, index, SAFESTR(text), 0))
					text[0]='\0';

				if (!widget) {
					widget = (UIWidget*)ui_TextureEntryCreate("", NULL, false);
				}
				ui_WidgetSetDimensionsEx(widget, 1.f, 64, UIUnitPercentage, UIUnitFixed);
				node = ui_RebuildableTreeAddWidget(parent, widget, widget2, newline, myname, &params_local);
				
				ui_TextureEntrySetChangedCallback((UITextureEntry*)widget, dataChangedCallback, node);

				ui_TextureEntrySetTextureName((UITextureEntry*)widget, text);
			} else if (params_local.type == AWT_ColorPicker) {
				int i;
				Vec4 value = {0};

				if (!widget) {
					widget = (UIWidget*)ui_ColorButtonCreateEx(0, 0, 0, 15, value);
				}
				((UIColorButton*)widget)->parentGroup = params_local.parentGroup;
				((UIColorButton*)widget)->liveUpdate = true;
				node = ui_RebuildableTreeAddWidget(parent, widget, widget2, newline, myname, &params_local);

				for (i=0; i<4; i++) {
					TextParserResult tpr=PARSERESULT_SUCCESS;
					value[i] = TokenStoreGetF32(field, 0, structptr, i, &tpr);
					if (tpr!=PARSERESULT_SUCCESS) {
						value[i] = 1;
						if (i==3)
							((UIColorButton*)widget)->noAlpha = true;
					}
				}
				ui_ColorButtonSetColor((UIColorButton*)widget, value);
				ui_ColorButtonSetChangedCallback((UIColorButton*)widget, dataChangedCallback3, node);
			} else if (params_local.type == AWT_DictionaryTextEntry) {
				// Generic handler
				char text[1024];

				text[0] = 0;

				// Text box
				if (!FieldToSimpleString(field, 0, structptr, index, SAFESTR(text), 0))
					text[0]='\0';
				if (!widget) {
					if (!params_local.parseTable)
						params_local.parseTable = RefSystem_GetDictionaryParseTable(params_local.dictionary);
					if (!params_local.parseNameField) {
						int key_index;
						if ((key_index = ParserGetTableKeyColumn(params_local.parseTable))==-1) {
							params_local.parseNameField = "Name";
						} else {
							params_local.parseNameField = params_local.parseTable[key_index].name;
						}
					}
					widget = (UIWidget*)ui_DictionaryEntryCreate("DEFAULT", params_local.dictionary, params_local.filterable, params_local.editable);
					if ((params_local.overrideWidth > 0.0) && (params_local.overrideWidth <= 1.0))
					{
						ui_WidgetSetWidthEx(widget, params_local.overrideWidth, UIUnitPercentage);
					}
					else if (params_local.overrideWidth)
					{
						ui_WidgetSetWidth(widget, params_local.overrideWidth);
					}
				}
				node = ui_RebuildableTreeAddWidget(parent, widget, NULL, newline, myname, &params_local);
				ui_DictionaryEntrySetFinishedCallback((UIDictionaryEntry*)widget, dataChangedCallback, node);
				if (params_local.changedUpdate)
					ui_DictionaryEntrySetChangedCallback((UIDictionaryEntry*)widget, dataChangedCallback, node);
				ui_DictionaryEntrySetText((UIDictionaryEntry*)widget, text);
			} else {
				// Generic handler
				char text[1024];

				text[0] = 0;

				// Text box
				if (!FieldToSimpleString(field, 0, structptr, index, SAFESTR(text), 0))
					text[0]='\0';
				if (!widget) {
					widget = (UIWidget*)ui_TextEntryCreate("DEFAULT", 0, 0);
					ui_TextEntrySetSelectOnFocus((UITextEntry*) widget, true);
				}
				if ((params_local.overrideWidth > 0.0) && (params_local.overrideWidth <= 1.0))
				{
					ui_WidgetSetWidthEx(widget, params_local.overrideWidth, UIUnitPercentage);
				}
				else if (params_local.overrideWidth)
				{
					ui_WidgetSetWidth(widget, params_local.overrideWidth);
				}
				node = ui_RebuildableTreeAddWidget(parent, widget, NULL, newline, myname, &params_local);
				ui_TextEntrySetFinishedCallback((UITextEntry*)widget, dataChangedCallback, node);
				if (params_local.changedUpdate)
					ui_TextEntrySetChangedCallback((UITextEntry*)widget, dataChangedCallback, node);
				ui_TextEntrySetText((UITextEntry*)widget, text);
			}
		} else {
			assert(params_local.type==AWT_Default); // Probably wasn't handled!
		}
		if (node) {
			node->callbackF = onDataChangedCallback;
			node->callbackData = userData;
			node->field = field;
			node->index = index;
			node->structptr = structptr;
		}
		if (widget) {
			if (params_local.disabled)
				ui_SetActive(widget, false);
			else 
				ui_SetActive(widget, true);
		}
		if (widget2) {
			if (params_local.disabled)
				ui_SetActive(widget2, false);
			else 
				ui_SetActive(widget2, true);
		}
		if (tooltip && widget)
		{
			//widget->tooltip = StructAllocString(tooltip);
			ui_WidgetSetTooltipString(widget, tooltip);
		}
	}

	return widget;
}

void ui_AutoWidgetAddAllFlags(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_NN_VALID ParseTable *table, SA_PARAM_NN_STR const char *fieldName, SA_PARAM_NN_VALID void *structptr, bool newline, RTNodeDataChangedCallback onDataChangedCallback, UserData userData, SA_PARAM_OP_VALID UIAutoWidgetParams *params, SA_PARAM_OP_STR const char *tooltip)
{
	int row;
	ParseTable *field;
	StructTokenType type;
	StructFormatOptions format;

	if (!ParserFindColumn(table, fieldName, &row)) {
		assertmsgf(0, "Invalid field name passed to %s", __FUNCTION__);
		return;
	}

	field = &table[row];
	type = TOK_GET_TYPE(field->type);
	format = TOK_GET_FORMAT_OPTIONS(field->format);

	assert(format == TOK_FORMAT_FLAGS);
	assert(1==TokenStoreGetNumElems(field, 0, structptr, NULL));

	// Label
	ui_RebuildableTreeAddWidget(parent, (UIWidget*)ui_LabelCreate(field->name, 0, 0), NULL, true, field->name, NULL);

	{
		U32 *data = (U32*)((char*)structptr + field->storeoffset);
		StaticDefineInt *flag_list = (StaticDefineInt *)field->subtable;
		assert(flag_list->key == (char*)DM_INT);
		flag_list++;
		while (flag_list && flag_list->key!=(char*)DM_END)
		{
			char name[1024];
			if (flag_list->key==(char*)DM_TAILLIST) {
				flag_list = (StaticDefineInt *)flag_list->value;
				continue;
			}
			if (flag_list->value == 0) {
				flag_list++;
				continue;
			}
			sprintf(name, "%s|%s", fieldName, flag_list->key);
			ui_AutoWidgetAdd(parent, table, name, structptr, newline, onDataChangedCallback, userData, params, tooltip);
			flag_list++;
		}
	}
}
