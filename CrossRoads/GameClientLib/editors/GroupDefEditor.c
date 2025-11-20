///***************************************************************************
//*     Copyright (c) 2006, Cryptic Studios
//*     All Rights Reserved
//*     Confidential Property of Cryptic Studios
//***************************************************************************/
//
//
//#include "error.h"
//#include "MemoryPool.h"
//#include "earray.h"
//#include "utils.h"
//#include "textparser.h"
//#include "TimedCallback.h"
//#include "rgb_hsv.h"
//#include "Color.h"
//
//#include "WorldGrid.h"
//#include "ObjectLibrary.h"
//#include "wlModel.h"
//
//#include "GraphicsLib.h"
//
//#include "AssetManager.h"
//#include "AssetManagerUtils.h"
//
//#include "UILib.h"
//#include "EditLibGizmos.h"
//
//AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););
//
//typedef struct GroupDefEditDoc GroupDefEditDoc;
//static void refreshUI(GroupDefEditDoc *doc);
//
////////////////////////////////////////////////////////////////////////////
//
//static AMEditor groupdef_editor;
//
////////////////////////////////////////////////////////////////////////////
//
//AUTO_STRUCT;
//typedef struct GroupProperty
//{
//	char *propname;
//	char *propvalue;
//} GroupProperty;
//
//#include "GroupDefEditor_c_ast.c"
//
//typedef struct GroupDefEditDoc
//{
//	// The base_doc MUST be first in the struct!  Don't move it!
//	AMEditorDoc base_doc;
//
//	GroupDef *def;
//
//	UIWindow *main_window;
//	ModelEditToolbar *toolbar;
//
//	GroupProperty **properties;
//
//} GroupDefEditDoc;
//
//MP_DEFINE(GroupDefEditDoc);
//
//#define INC_Y(ui) y += (ui)->widget.height + 5
//#define INC_Y_H(height) y += (height) + 5
//
////////////////////////////////////////////////////////////////////////////
//
//static void editModel(GroupDefEditDoc *doc)
//{
//	char buffer[1024];
//	if (!doc || !doc->def || !doc->def->model)
//		return;
//	sprintf(buffer, "%s,%s", doc->def->model->name, doc->def->model->header->filename);
//	amEditFileEx(buffer, "model");
//}
//
//static void editModelCommand(void *unused, GroupDefEditDoc *doc)
//{
//	amQueueFunctionCall(editModel, doc);
//}
//
//static void refreshUI(GroupDefEditDoc *doc)
//{
//	UIWindow *main_window;
//	UILabel *label;
//	UIButton *button;
//	UIList *list;
//	UIListColumn *column;
//	char buffer[256];
//	int y = 5;
//	int i;
//	char **groupprops;
//
//	if (!doc->main_window)
//	{
//		doc->main_window = ui_WindowCreate("GroupDef Viewer", 400, 50, 200, 400);
//		doc->main_window->minW = 200;
//		doc->main_window->minH = 300;
//		eaPush(&doc->base_doc.ui_windows, doc->main_window);
//		doc->base_doc.primary_ui_window = doc->main_window;
//	}
//	
//	main_window = doc->main_window;
//	ui_WidgetGroupQueueFree(&main_window->widget.children);
//
//	sprintf(buffer, "Name: %s", doc->def->name_str);
//	label = ui_LabelCreate(buffer, 5, y);
//	ui_WindowAddChild(main_window, label);
//	INC_Y(label);
//
//	sprintf(buffer, "UID: %d", doc->def->name_uid);
//	label = ui_LabelCreate(buffer, 5, y);
//	ui_WindowAddChild(main_window, label);
//	INC_Y(label);
//
//	sprintf(buffer, "File: %s", groupDefGetFilename(doc->def));
//	label = ui_LabelCreate(buffer, 5, y);
//	ui_WindowAddChild(main_window, label);
//	INC_Y(label);
//
//	sprintf(buffer, "Model: %s", doc->def->model?doc->def->model->name:"NONE");
//	label = ui_LabelCreate(buffer, 5, y);
//	ui_WindowAddChild(main_window, label);
//
//	if (doc->def->model)
//	{
//		button = ui_ButtonCreate("Edit Model", label->widget.x + label->widget.width + 5, y - 2, editModelCommand, doc);
//		ui_WindowAddChild(main_window, button);
//	}
//
//	INC_Y(label);
//
//	INC_Y_H(5);
//
//	groupprops = groupDefGetPropertyNames(doc->def);
//	for (i = 0; i < eaSize(&groupprops); ++i)
//	{
//		GroupProperty *prop = StructAlloc(parse_GroupProperty);
//		prop->propname = StructAllocString(groupprops[i]);
//		prop->propvalue = groupDefFindProperty(doc->def, groupprops[i]);
//		if (prop->propvalue)
//			prop->propvalue = StructAllocString(prop->propvalue);
//		eaPush(&doc->properties, prop);
//	}
//
//	list = ui_ListCreate(parse_GroupProperty, &doc->properties, 12);
//	list->widget.width = 1;
//	list->widget.widthUnit = UIUnitPercentage;
//	list->widget.leftPad = 5;
//	list->widget.rightPad = 5;
//	list->widget.height = 1;
//	list->widget.heightUnit = UIUnitPercentage;
//	list->widget.topPad = y;
//	list->widget.bottomPad = 5;
//	ui_WindowAddChild(main_window, list);
//
//	column = ui_ListColumnCreate(UIListPTName, "Property", (intptr_t)"propname", NULL);
//	column->fWidth = 150;
//	ui_ListAppendColumn(list, column);
//
//	column = ui_ListColumnCreate(UIListPTName, "Value", (intptr_t)"propvalue", NULL);
//	column->fWidth = 200;
//	ui_ListAppendColumn(list, column);
//}
//
//static void gdeDrawGhosts(AMEditorDoc *doc_in)
//{
//	GroupDefEditDoc *doc = (GroupDefEditDoc *)doc_in;
//	TempGroupParams tgparams = {0};
//	Vec3 tint_color0, tint_color1;
//	Mat4 mat;
//
//	metUpdateAndDraw(doc->toolbar);
//
//	copyMat4(unitmat, mat);
//	setVec3(mat[3], 0, -doc->def->bounds.min[1], 0);
//
//	metGetTintColor0(doc->toolbar, tint_color0);
//	tgparams.tint_color0 = tint_color0;
//
//	metGetTintColor1(doc->toolbar, tint_color1);
//	tgparams.tint_color1 = tint_color1;
//
//	tgparams.wireframe = metGetWireframeSetting(doc->toolbar);
//	tgparams.unlit = metGetUnlitSetting(doc->toolbar);
//
//	worldAddTempGroup(doc->def, mat, &tgparams, false);
//}
//
//static void gdeLostFocus(AMEditorDoc *doc_in)
//{
//	GroupDefEditDoc *doc = (GroupDefEditDoc *)doc_in;
//	metLostFocus(doc->toolbar);
//}
//
//static void gdeGotFocus(AMEditorDoc *doc_in)
//{
//	GroupDefEditDoc *doc = (GroupDefEditDoc *)doc_in;
//	metGotFocus(doc->toolbar);
//}
//
//static AMEditorDoc *gdeNewDoc(const char *name, const char *type)
//{
//	GroupDefEditDoc *doc;
//	GroupDef *def = NULL;
//	TrackerHandle *handle = NULL;
//
//	if (!type)
//	{
//		Errorf("NULL file type passed to gdeNewDoc");
//		return NULL;
//	}
//
//	if (stricmp(type, "groupdef_handle")==0)
//	{
//		GroupTracker *tracker = NULL;
//
//		// it is a tracker handle in string form, decompose the handle
//		handle = trackerHandleFromString(name);
//		if (!handle)
//		{
//			Errorf("Invalid tracker handle string passed to gdeNewDoc");
//			return NULL;
//		}
//
//		tracker = trackerFromTrackerHandle(handle);
//
//		if (!tracker)
//		{
//			Errorf("Invalid tracker handle passed to gdeNewDoc, tracker not found");
//			trackerHandleDestroy(handle);
//			return NULL;
//		}
//
//		def = tracker->def;
//	}
//	else if (stricmp(type, "groupdef_id")==0)
//	{
//		int uid;
//
//		// it is an object library uid, extract the uid
//		uid = atoi(name);
//		if (uid >= 0)
//		{
//			Errorf("Invalid object library uid (%d) passed to gdeNewDoc, uid not negative", uid);
//			return NULL;
//		}
//
//		def = objectLibraryGetGroupDef(uid);
//
//		if (!def || !def->name_str)
//		{
//			Errorf("Invalid object library uid (%d) passed to gdeNewDoc, def not found", uid);
//			return NULL;
//		}
//
//	}
//	else
//	{
//		Errorf("Unknown file type \"%s\" passed to gdeNewDoc", type);
//		return NULL;
//	}
//
//	if (!def)
//	{
//		Errorf("NULL GroupDef found in gdeNewDoc");
//		return NULL;
//	}
//
//	doc = calloc(sizeof(*doc),1);
//	amSetDocFile(&doc->base_doc, gfileGetFilename(def->file));
//	doc->def = def;
//	strcpy(doc->base_doc.doc_display_name, def->name_str);
//
//	refreshUI(doc);
//
//	doc->toolbar = metCreateToolbar(MET_ALL & ~(MET_ALWAYS_ON_TOP), groupdef_editor.camera, def->bounds.min, def->bounds.max, def->bounds.radius);
//	eaPush(&doc->base_doc.ui_windows, metGetWindow(doc->toolbar));
//
//	trackerHandleDestroy(handle);
//
//	return &doc->base_doc;
//}
//
//static void freeGroupProperty(GroupProperty *prop)
//{
//	StructDestroy(parse_GroupProperty, prop);
//}
//
//static void gdeCloseDoc(AMEditorDoc *doc_in)
//{
//	GroupDefEditDoc *doc = (GroupDefEditDoc *)doc_in;
//	eaFindAndRemoveFast(&doc->base_doc.ui_windows, metGetWindow(doc->toolbar));
//	metFreeToolbar(doc->toolbar);
//	eaDestroyEx(&doc->base_doc.ui_windows, ui_WindowFreeInternal);
//	eaDestroyEx(&doc->properties, freeGroupProperty);
//	SAFE_FREE(doc);
//}
//
//static bool groupDefPreview(const char *name, const char *type)
//{
//	GroupDef *def = NULL;
//	TrackerHandle *handle = NULL;
//	Mat4 mat;
//
//	if (!type)
//		return false;
//
//	if (stricmp(type, "groupdef_handle")==0)
//	{
//		GroupTracker *tracker = NULL;
//
//		// it is a tracker handle in string form, decompose the handle
//		handle = trackerHandleFromString(name);
//		if (!handle)
//			return false;
//
//		tracker = trackerFromTrackerHandle(handle);
//
//		if (!tracker)
//		{
//			trackerHandleDestroy(handle);
//			return false;
//		}
//
//		def = tracker->def;
//	}
//	else if (stricmp(type, "groupdef_id")==0)
//	{
//		int uid;
//
//		// it is an object library uid, extract the uid
//		uid = atoi(name);
//		if (uid >= 0)
//			return false;
//
//		def = objectLibraryGetGroupDef(uid);
//	}
//	else
//	{
//		return false;
//	}
//
//	if (!def)
//		return false;
//
//	copyMat3(unitmat, mat);
//	setVec3(mat[3], 0, -def->bounds.min[1], 0);
//	worldAddTempGroup(def, mat, NULL, false);
//
//	return true;
//}
//
////////////////////////////////////////////////////////////////////////////
//// registration with asset manager
//
//AUTO_RUN;
//int gdeRegister(void)
//{
//	if (!areEditorsAllowed())
//		return 0;
//	strcpy(groupdef_editor.editor_name, "GroupDef Viewer");
//	groupdef_editor.allow_multiple_docs = 1;
//	groupdef_editor.allow_save = 1;
//	groupdef_editor.hide_world = 1;
//	groupdef_editor.allow_outsource = 1;
//	groupdef_editor.disable_auto_checkout = 1;
//	groupdef_editor.do_not_refresh = 1;
//
//	groupdef_editor.new_func = gdeNewDoc;
//	groupdef_editor.close_func = gdeCloseDoc;
//	groupdef_editor.got_focus_func = gdeGotFocus;
//	groupdef_editor.lost_focus_func = gdeLostFocus;
//	groupdef_editor.ghost_draw_func = gdeDrawGhosts;
//
//	eaPush(&groupdef_editor.gimme_groups, "Art");
//	eaPush(&groupdef_editor.gimme_groups, "Software");
//
//	amRegisterEditor(&groupdef_editor);
//
//	amRegisterFileTypeEx("groupdef_handle", "GroupDef Viewer", groupDefPreview);
//	amRegisterFileTypeEx("groupdef_id", "GroupDef Viewer", groupDefPreview);
//
//	return 1;
//}
//
//
