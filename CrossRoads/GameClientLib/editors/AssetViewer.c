///***************************************************************************
//*     Copyright (c) 2007, Cryptic Studios
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
//#include "file.h"
//#include "utils.h"
//#include "strings_opt.h"
//#include "fileutil.h"
//#include "sysutil.h"
//#include "StringUtil.h"
//#include "Color.h"
//#include "cmdparse.h"
//#include "FolderCache.h"
//#include "StringCache.h"
//
//#include "GfxDebug.h"
//#include "EditLibGizmos.h"
//
//#include "AssetManager.h"
//#include "AssetManagerUtils.h"
//
//AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););
//
//AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
//typedef struct AssetData
//{
//	char *display_name; AST( STRUCTPARAM )
//	char *name; AST( STRUCTPARAM )
//	char *type; AST( STRUCTPARAM )
//} AssetData;
//
//AUTO_STRUCT;
//typedef struct AssetList
//{
//	char *name;			AST( STRUCTPARAM )
//	const char *filename;	AST(CURRENTFILE)
//	char *skyfile;		AST( NAME(SkyFile) )
//	F32 time;			AST( NAME(Time) DEFAULT(-1) )
//	AssetData **assets;	AST( NAME(Asset) )
//} AssetList;
//
//AUTO_STRUCT;
//typedef struct AssetListEntries
//{
//	AssetList **asset_lists; AST( NAME(AssetList) )
//} AssetListEntries;
//
//typedef struct AssetViewerDoc
//{
//	AMEditorDoc base_doc;
//
//	UIList *ui_assetlist;
//
//	AssetList *asset_list;
//	int cur_idx;
//
//	ModelEditToolbar *toolbar;
//
//} AssetViewerDoc;
//
//
//static AMEditor asset_viewer;
//static AssetListEntries avb_entries;
//
//CmdList assetviewer_cmdlist = {0};
//
//#include "AutoGen/AssetViewer_c_ast.c"
//
//
//static void avDrawGhosts(AMEditorDoc *doc_in)
//{
//	AssetViewerDoc *doc = (AssetViewerDoc *)doc_in;
//	int prev_idx = doc->cur_idx;
//
//	metUpdateAndDraw(doc->toolbar);
//
//	if (!eaSize(&doc->asset_list->assets))
//		return;
//
//	while (!amPreviewFileEx(doc->asset_list->assets[doc->cur_idx]->name, doc->asset_list->assets[doc->cur_idx]->type))
//	{
//		doc->cur_idx++;
//		if (doc->cur_idx >= eaSize(&doc->asset_list->assets))
//			doc->cur_idx -= eaSize(&doc->asset_list->assets);
//
//		ui_ListSetSelectedRow(doc->ui_assetlist, doc->cur_idx);
//
//		// if we went all the way through the list, none of the assets are viewable
//		if (doc->cur_idx == prev_idx)
//			return;
//
////		metSetObjectBounds(doc->toolbar, );
//	}
//}
//
//static void avLostFocus(AMEditorDoc *doc_in)
//{
//	AssetViewerDoc *doc = (AssetViewerDoc *)doc_in;
//	metLostFocus(doc->toolbar);
//}
//
//static void avGotFocus(AMEditorDoc *doc_in)
//{
//	AssetViewerDoc *doc = (AssetViewerDoc *)doc_in;
//	metGotFocus(doc->toolbar);
//}
//
//static void avDrawDoc(AMEditorDoc *doc_in)
//{
//}
//
//static void avSetSelectedAsset(UIList *list, AssetViewerDoc *doc)
//{
//	doc->cur_idx = ui_ListGetSelectedRow(list);
//	
//	if (doc->cur_idx >= eaSize(&doc->asset_list->assets))
//		doc->cur_idx = eaSize(&doc->asset_list->assets) - 1;
//	if (doc->cur_idx < 0)
//		doc->cur_idx = 0;
//
//	ui_ListSetSelectedRow(doc->ui_assetlist, doc->cur_idx);
//}
//
//static AMEditorDoc *avNewDoc(const char *name, const char *type)
//{
//	AssetViewerDoc *doc;
//	char filename[MAX_PATH];
//	UIWindow *window;
//	UIList *list;
//	UIListColumn *column;
//	int i;
//
//	// check type
//	if (!type || stricmp(type, "AssetList")!=0)
//		return NULL;
//
//	if (!name || !name[0])
//		return NULL;
//
//	// allocate document structure
//	doc = calloc(1, sizeof(*doc));
//	strcpy(doc->base_doc.doc_display_name, name);
//	sprintf(filename, "asset_lists/%s.%s", name, type);
//	amSetDocFile(&doc->base_doc, filename);
//	if (!fileExists(doc->base_doc.file->filename))
//		doc->base_doc.file->saved = 1;
//
//	for (i = 0; i < eaSize(&avb_entries.asset_lists); ++i)
//	{
//		if (stricmp(avb_entries.asset_lists[i]->filename, filename)==0)
//		{
//			doc->asset_list = avb_entries.asset_lists[i];
//			break;
//		}
//	}
//
//	if (!doc->asset_list)
//	{
//		doc->asset_list = StructCreate(parse_AssetList);
//		doc->asset_list->filename = allocAddFilename(filename);
//		doc->asset_list->time = -1;
//		eaPush(&avb_entries.asset_lists, doc->asset_list);
//	}
//
//	doc->toolbar = metCreateToolbar(MET_CAMDIST|MET_GRID|MET_TIME|MET_ORTHO|MET_LIGHTING|MET_SKIES, asset_viewer.camera, zerovec3, zerovec3, 50);
//	eaPush(&doc->base_doc.ui_windows, metGetWindow(doc->toolbar));
//
//	metSetTime(doc->toolbar, doc->asset_list->time);
//	metSetSkyFile(doc->toolbar, doc->asset_list->skyfile);
//
//	window = ui_WindowCreate("Asset List", 100, 400, 200, 500);
//	doc->base_doc.primary_ui_window = window;
//	eaPush(&doc->base_doc.ui_windows, window);
//
//	list = ui_ListCreate(parse_AssetData, &doc->asset_list->assets, 15);
//	ui_WidgetSetPosition(&list->widget, 0, 0);
//	ui_WidgetSetDimensionsEx(&list->widget, 1, 1, UIUnitPercentage, UIUnitPercentage);
//	ui_WidgetSetPadding(&list->widget, 10, 10);
//	ui_ListSetSelectedCallback(list, avSetSelectedAsset, doc);
//	doc->ui_assetlist = list;
//	ui_WindowAddChild(window, list);
//
//	column = ui_ListColumnCreate(UIListPTName, "Name", (intptr_t)"display_name",NULL);
//	ui_ListColumnSetWidth(column, true, 1);
//	ui_ListAppendColumn(list, column);
//
//	column = ui_ListColumnCreate(UIListPTName, "Type", (intptr_t)"type",NULL);
//	ui_ListColumnSetWidth(column, true, 1);
//	ui_ListAppendColumn(list, column);
//
//	column = ui_ListColumnCreate(UIListPTName, "Internal Name", (intptr_t)"name",NULL);
//	ui_ListColumnSetWidth(column, true, 1);
//	ui_ListAppendColumn(list, column);
//
//	ui_ListSetSelectedRow(doc->ui_assetlist, doc->cur_idx);
//
//	return &doc->base_doc;
//}
//
//static void avCloseDoc(AMEditorDoc *doc_in)
//{
//	AssetViewerDoc *doc = (AssetViewerDoc *)doc_in;
//
//	eaFindAndRemoveFast(&doc->base_doc.ui_windows, metGetWindow(doc->toolbar));
//	metFreeToolbar(doc->toolbar);
//
//	eaDestroyEx(&doc->base_doc.ui_windows, ui_WindowFreeInternal);
//	SAFE_FREE(doc);
//}
//
//static void avSaveDoc(AMEditorDoc *doc_in)
//{
//	AMFile *file = doc_in->file;
//
//	if (!file)
//		return;
//
//	assert(eaSize(&file->docs));
//
//	if (!amuCheckoutFile(file))
//	{
//		Errorf("Unable to checkout file \"%s\"", file->filename);
//		return;
//	}
//
//	// TODO
//
//	file->saved = 1;
//}
//
//static void avAddAsset(AMEditorDoc *doc_in, const char *name, const char *type)
//{
//	AssetViewerDoc *doc = (AssetViewerDoc *)doc_in;
//
//	// TODO
//}
//
//AUTO_COMMAND ACMD_NAME("AssetViewer.Previous") ACMD_LIST(assetviewer_cmdlist);
//void avPreviousAsset(void)
//{
//	AssetViewerDoc *doc = (AssetViewerDoc *)amGetActiveEditorDoc();
//
//	if (!eaSize(&doc->asset_list->assets))
//		return;
//
//	--doc->cur_idx;
//	if (doc->cur_idx < 0)
//		doc->cur_idx += eaSize(&doc->asset_list->assets);
//
//	ui_ListSetSelectedRow(doc->ui_assetlist, doc->cur_idx);
//}
//
//AUTO_COMMAND ACMD_NAME("AssetViewer.Next") ACMD_LIST(assetviewer_cmdlist);
//void avNextAsset(void)
//{
//	AssetViewerDoc *doc = (AssetViewerDoc *)amGetActiveEditorDoc();
//
//	if (!eaSize(&doc->asset_list->assets))
//		return;
//
//	++doc->cur_idx;
//	if (doc->cur_idx >= eaSize(&doc->asset_list->assets))
//		doc->cur_idx -= eaSize(&doc->asset_list->assets);
//
//	ui_ListSetSelectedRow(doc->ui_assetlist, doc->cur_idx);
//}
//
////////////////////////////////////////////////////////////////////////////
//// registration with asset manager
//
//AUTO_RUN;
//int avRegister(void)
//{
//	strcpy(asset_viewer.editor_name, "Asset Viewer");
//	asset_viewer.allow_multiple_docs = 1;
//	asset_viewer.allow_save = 1;
//	asset_viewer.hide_world = 1;
//	asset_viewer.allow_outsource = 1;
//
//	asset_viewer.new_func = avNewDoc;
//	asset_viewer.close_func = avCloseDoc;
//	asset_viewer.save_func = avSaveDoc;
//	asset_viewer.draw_func = avDrawDoc;
//	asset_viewer.ghost_draw_func = avDrawGhosts;
//	asset_viewer.lost_focus_func = avLostFocus;
//	asset_viewer.got_focus_func = avGotFocus;
//	asset_viewer.object_dropped_func = avAddAsset;
//
//	asset_viewer.keybinds.pchName = "Asset Viewer Commands";
//	asset_viewer.keybinds.pCmdList = &assetviewer_cmdlist;
//	asset_viewer.keybinds.bTrickleCommands = 1;
//	asset_viewer.keybinds.bTrickleKeys = 1;
//	asset_viewer.keybindsName = "AssetViewer";
//	asset_viewer.use_keybinds = 1;
//
//	amRegisterEditor(&asset_viewer);
//
//	amRegisterFileType("AssetList", "Asset Viewer");
//
//	return 1;
//}
//
////////////////////////////////////////////////////////////////////////////
//
//static AMBrowser assetviewer_browser;
//
//static void avbListToName(AMBrowser *browser, AssetList *al, ParseTable *parse_info)
//{
//	char *s;
//	const char *cs;
//
//	assert(al);
//	assert(parse_info == parse_AssetList);
//
//	cs = strrchr(al->filename, '/');
//	if (cs)
//		cs++;
//	else
//		cs = al->filename;
//
//	sprintf(browser->selected_doc_name, "%s", cs);
//	s = strrchr(browser->selected_doc_name, '.');
//	if (s)
//		*s = 0;
//
//	strcpy(browser->selected_doc_type, "AssetList");
//}
//
//static int reloadAssetListSubStructCallback(void *substruct, void *oldsubstruct, ParseTable *tpi, eParseReloadCallbackType callback_type)
//{
//	if (callback_type == eParseReloadCallbackType_Delete) {
//		return 1;
//	}
//	if (tpi != parse_AssetList) {
//		assertmsg(0, "Got unknown struct type passed to reloadAssetListSubStructCallback");
//	}
//	if (callback_type == eParseReloadCallbackType_Add) {
//	}
//	return 1;
//}
//
//static void reloadAssetListCallback(const char *relpath, int when)
//{
//	fileWaitForExclusiveAccess(relpath);
//	errorLogFileIsBeingReloaded(relpath);
//	if (!ParserReloadFile(relpath, parse_AssetListEntries, &avb_entries, reloadAssetListSubStructCallback)) {
//		gfxStatusPrintf("Error reloading AssetList: %s", relpath);
//	} else {
//		gfxStatusPrintf("AssetList reloaded: %s", relpath);
//	}
//}
//
//static void avbInit(AMBrowser *browser)
//{
//	AMBrowserDisplayType *display_type;
//
//	ParserLoadFiles("asset_lists", ".AssetList", "AssetLists.bin", PARSER_OPTIONALFLAG, parse_AssetListEntries, &avb_entries);
//	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "asset_lists/*.AssetList", reloadAssetListCallback);
//
//	browser->display_data_root = &avb_entries;
//	browser->display_parse_info_root = parse_AssetListEntries;
//
//	display_type = calloc(sizeof(*display_type),1);
//	display_type->parse_info = parse_AssetList;
//	display_type->display_name_parse_field = "name";
//	display_type->selected_func = avbListToName;
//	display_type->is_leaf = 1;
//	display_type->color = CreateColorRGB(0, 0, 0);
//	eaPush(&browser->display_types, display_type);
//}
//
//AUTO_RUN;
//int avbRegister(void)
//{
//	strcpy(assetviewer_browser.browser_name, "Asset Lists");
//	assetviewer_browser.init_func = avbInit;
//	assetviewer_browser.allow_outsource = 1;
//	amRegisterBrowser(&assetviewer_browser);
//
//	return 1;
//}
