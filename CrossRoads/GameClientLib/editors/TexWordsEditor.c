#include "TexWordsEditor.h"
#include "EditorManager.h"
#include "GraphicsLib.h"

#include "wininclude.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "StringCache.h"
#include "file.h"
#include "MessageStore.h"
#include "Prefs.h"
#include "Color.h"
#include "UIRectangularSelection.h"
#include "UIAutoWidget.h"
#include "GfxTextureTools.h"
#include "GfxDebug.h"

// Including "private" GraphicsLib header files because we're an editor, and the editor can't live in GraphicsLib
#include "../GraphicsLib/GfxTextures.h"
#include "../GraphicsLib/texWords.h"
#include "../GraphicsLib/texWordsPrivate.h"

#ifndef NO_EDITORS

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// Convenience macros

#define addButton(node, name, func, tooltip) ui_AutoWidgetAddButton(node, name, func, twDoc, false, tooltip, NULL)
#define addButtonNewLine(node, name, func, tooltip) ui_AutoWidgetAddButton(node, name, func, twDoc, true, tooltip, NULL)
#define addButtonEx(parent, name, clickedF, clickedData, newline, tooltip) ui_AutoWidgetAddButton(parent, name, clickedF, clickedData, newline, tooltip, NULL)


#define addAutoWidget ui_AutoWidgetAdd

#define addGroupEx ui_RebuildableTreeAddGroup
#define addGroup(parent, name, defaultOpen, tooltip) addGroupEx(parent, name, name, defaultOpen, tooltip)

#define PARAMS(s) ui_AutoWidgetParamsFromString(s)

#define USE_SMF USE_SMF_TAG
#define USER_PARAMETER(s) "<color #7F2020>" s "</color>"
#define IMAGE_ONLY IMAGE_ONLY_TAG

static bool texWordsEdit_layersOnMenu=true;
// Displays layer options on main menu instead of context menus
AUTO_CMD_INT(texWordsEdit_layersOnMenu, texWordsEdit_layersOnMenu);



static EMEditor texwords_editor;

typedef struct TWEditDoc
{
	EMEditorDoc base_doc;

	TexWord *texWord; // Pointer into the shared list
	BasicTexture *bind; // Either a dynamic texture, or the texture which uses this TexWord
	BasicTexture *drawMeBind; // The one to be drawn (might be the original sans translation)
	MessageStore *messageStore;
	char tweLocale[256];

	// Undo stuff
	TexWord undoHist[128];
	int undoIndex; // Where we will write the next undo Journal to (current state should be mirrored in undoIndex-1)
	int undoMax;

	int needToRebuildUI;
	int needToRebuildTextures;

	int layerUID;
	int filterUID;

	bool stretch;
	bool orig;
	F32 colorPickerX;
	F32 colorPickerY;
	Vec4 color;
	Vec4 bgcolor;

	BasicTexture dummyBind;

	struct TexWordsEditorStateExtentsEdit {
//		int on;
		int edit;
		TexWordLayer *editLayer;
//		int index;
//		int mousegrabbed;
//		ExtentsMask mask; // Mask of what corners are grabbed
		F32 x, y, w, h; // Local copies in texture space
		UIRectangularSelection *rect;
		int was_resizing;
	} extents;
	struct {
		F32 x0, y0, xs, ys;
	} display;

	// UI stuff

	UIColorWindow *colorPicker;
	UIWindow *mainWindow;
	UIRebuildableTree *ui_tree;
	UIButton *button_colorPicker;
	UIButton *button_BGColor;

	// For the context menu
	UIMenu *layerPopupMenu;
	int layerPopupIndex;
	TexWordLayer *layerPopupLayer;

	UIMenu *filterPopupMenu;
	TexWordLayerFilter *filterPopupFilter;

	void **eaCallbackData; // per-UI-rebuild dynamically allocated callback data

} TWEditDoc;

static TexWord layerClipboard={0};

static void tweCloseDoc(EMEditorDoc* twDoc);
static EMTaskStatus tweSaveDoc(EMEditorDoc* twDoc);
void tweOnAnyDataChangedCallback(UIRTNode *node, TWEditDoc *twDoc);
void tweOnDataChangedCallback(UIRTNode *node, TWEditDoc *twDoc);
void texWordsEdit_layerExtentsApply(TWEditDoc *twDoc);
void texWordsEdit_layerExtentsFinish(UIAnyWidget *widget_UNUSED, TWEditDoc *twDoc);
void tweUpdateTextBeforeEditLayer(TexWordLayer *layer);
static void tweUpdateTextAfterEdit(SA_PARAM_OP_VALID TWEditDoc *twDoc, TexWord *texWord, MessageStore *messageStore);
static void cloneLayerText(TexWordLayer ***layerList);

#define GET_ACTIVE_TWDOC TWEditDoc *dummy_twDoc = twDoc?twDoc:((twDoc = (TWEditDoc*)emGetActiveEditorDoc()), assert(twDoc),twDoc)

typedef struct Pair {
	intptr_t first;
	intptr_t second;
} Pair;
typedef struct Triple {
	intptr_t first;
	intptr_t second;
	intptr_t third;
} Triple;
Pair *temp_pair;
Triple *temp_triple;
#define PAIR(v0, v1)		(eaPush(&twDoc->eaCallbackData, temp_pair=calloc(sizeof(*temp_pair),1)),temp_pair->first=(intptr_t)v0,temp_pair->second=(intptr_t)v1,temp_pair)
#define TRIPLE(v0, v1, v2)	(eaPush(&twDoc->eaCallbackData, temp_triple=calloc(sizeof(*temp_triple),1)),temp_triple->first=(intptr_t)v0,temp_triple->second=(intptr_t)v1,temp_triple->third=(intptr_t)v2,temp_triple)

static void texWordsEdit_playErrorSound(void)
{
#if !PLATFORM_CONSOLE
	Beep(880, 200);
#endif
}

static bool texWordsEdit_layerExistsSub(TexWordLayer *layer, TexWordLayer *layer_ref)
{
	int i;
	if (layer == layer_ref)
		return true;
	for (i=eaSize(&layer->sublayer)-1; i>=0; i--) {
		return texWordsEdit_layerExistsSub(layer->sublayer[i], layer_ref);
	}
	return false;
}

static bool texWordsEdit_layerExists(TWEditDoc *twDoc, TexWordLayer *layer)
{
	int i;
	if (!twDoc->texWord)
		return false;
	for (i=eaSize(&twDoc->texWord->layers)-1; i>=0; i--) {
		if (texWordsEdit_layerExistsSub(twDoc->texWord->layers[i], layer))
			return true;
	}
	return false;
}

static void textureSpaceToScreenSpace(TWEditDoc *twDoc, F32 x, F32 y, F32 *xout, F32 *yout)
{
	if (xout)
		*xout = twDoc->display.x0 + (x/* - 0.5*/)* twDoc->display.xs;
	if (yout)
		*yout = twDoc->display.y0 + (y/* - 0.5*/) * twDoc->display.ys;
}

static void screenSpaceToTextureSpace(TWEditDoc *twDoc, F32 x, F32 y, F32 *xout, F32 *yout)
{
	if (xout)
		*xout = (x - twDoc->display.x0) / twDoc->display.xs/* + 0.5*/;
	if (yout)
		*yout = (y - twDoc->display.y0) / twDoc->display.ys/* + 0.5*/;
}

static void texWordsEdit_updateExtentsFromWidget(TWEditDoc *twDoc)
{
	if (!twDoc->extents.rect)
		return;
	screenSpaceToTextureSpace(twDoc, twDoc->extents.rect->widget.x + g_ui_State.viewportMin[0], twDoc->extents.rect->widget.y + g_ui_State.viewportMin[1], &twDoc->extents.x, &twDoc->extents.y);
	screenSpaceToTextureSpace(twDoc, twDoc->extents.rect->widget.x+twDoc->extents.rect->widget.width + g_ui_State.viewportMin[0], twDoc->extents.rect->widget.y+twDoc->extents.rect->widget.height + g_ui_State.viewportMin[1], &twDoc->extents.w, &twDoc->extents.h);
	twDoc->extents.w-=twDoc->extents.x;
	twDoc->extents.h-=twDoc->extents.y;
}

static void texWordsEdit_updateWidgetFromExtents(TWEditDoc *twDoc)
{
	if (!twDoc->extents.rect)
		return;
	textureSpaceToScreenSpace(twDoc, twDoc->extents.x, twDoc->extents.y, &twDoc->extents.rect->widget.x, &twDoc->extents.rect->widget.y);
	textureSpaceToScreenSpace(twDoc, twDoc->extents.x + twDoc->extents.w, twDoc->extents.y + twDoc->extents.h, &twDoc->extents.rect->widget.width, &twDoc->extents.rect->widget.height);
	twDoc->extents.rect->widget.width  -= twDoc->extents.rect->widget.x;
	twDoc->extents.rect->widget.height -= twDoc->extents.rect->widget.y;
	twDoc->extents.rect->widget.x -= g_ui_State.viewportMin[0];
	twDoc->extents.rect->widget.y -= g_ui_State.viewportMin[1];
}

static void texWordsEdit_layerExtentsUnApply(TWEditDoc *twDoc)
{
	TexWordLayer *layer = twDoc->extents.editLayer;
	
	if (!twDoc->extents.edit)
		return;
	if (!texWordsEdit_layerExists(twDoc, layer)) {
		// This layer didn't exist
		twDoc->extents.editLayer = NULL;
		twDoc->extents.edit = 0;
		//twDoc->extents.on = 0;
		if (twDoc->extents.rect) {
			ui_WidgetQueueFree(UI_WIDGET(twDoc->extents.rect));
			twDoc->extents.rect = NULL;
		}
		return;
	}

	if (!twDoc->extents.rect) {
		twDoc->extents.rect = ui_RectangularSelectionCreate(0, 0, 0, 0,
			colorFromRGBA(0x7FFF7FFF),
			colorFromRGBA(0xFF7F7FFF));
		ui_WidgetAddToDevice(UI_WIDGET(twDoc->extents.rect), NULL);
	}

	twDoc->extents.x = layer->pos[0];
	twDoc->extents.y = layer->pos[1];
	twDoc->extents.w = layer->size[0];
	twDoc->extents.h = layer->size[1];
	texWordsEdit_updateWidgetFromExtents(twDoc);
}

static void texWordsEditRedoFunc(void *context, void *data_UNUSED);
static void texWordsEditUndoFunc(void *context, void *data_UNUSED);

static void texWordsEdit_undoJournal(TWEditDoc *twDoc)
{
	TexWord *texWord = twDoc->texWord;

	if (twDoc->undoIndex && texWord) {
		int ret = StructCompare(parse_TexWord, texWord, &twDoc->undoHist[twDoc->undoIndex-1], 0, 0, 0);
		if (!ret)
			return; // No difference, don't log!

		if (eaSize(&twDoc->base_doc.files) && !twDoc->base_doc.files[0]->file->checked_out)
			gfxShowPleaseWaitMessage( "Please wait, checking out file..." );
		emSetDocUnsaved(&twDoc->base_doc, true); // Only doing this if something changed
	}

	// Create dummy undo entry into the EditLib's undo stack
	EditUndoSetContext(twDoc->base_doc.edit_undo_stack, twDoc);
	EditUndoBeginGroup(twDoc->base_doc.edit_undo_stack); 
	EditCreateUndoCustom(twDoc->base_doc.edit_undo_stack, texWordsEditUndoFunc, texWordsEditRedoFunc, NULL, 0);
	EditUndoEndGroup(twDoc->base_doc.edit_undo_stack);

	if (twDoc->undoIndex == ARRAY_SIZE(twDoc->undoHist)) {
		StructDeInit(parse_TexWord, &twDoc->undoHist[0]);
		memmove(&twDoc->undoHist[0], &twDoc->undoHist[1], sizeof(twDoc->undoHist[0])*(ARRAY_SIZE(twDoc->undoHist)-1));
		twDoc->undoIndex--;
		memset(&twDoc->undoHist[ARRAY_SIZE(twDoc->undoHist)-1], 0, sizeof(TexWord));
	} else {
		// In case of overwriting some leftover entry
		StructDeInit(parse_TexWord, &twDoc->undoHist[twDoc->undoIndex]);
	}

	if (texWord) {
		StructCopyAll(parse_TexWord, texWord, &twDoc->undoHist[twDoc->undoIndex]);
	}
	twDoc->undoIndex++;
	twDoc->undoMax = twDoc->undoIndex;
}

static void texWordsEdit_undoReset(TWEditDoc *twDoc)
{
	int i;
	for (i=0; i<twDoc->undoIndex; i++) {
		StructDeInit(parse_TexWord, &twDoc->undoHist[i]);
	}
	twDoc->undoIndex = 0;
	texWordsEdit_undoJournal(twDoc);
}

AUTO_COMMAND;
void texWordsEdit_undoUndo(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	int oldMax = twDoc->undoMax;
	// Save current version
	texWordsEdit_undoJournal(twDoc);
	twDoc->undoMax = MAX(twDoc->undoMax, oldMax);
	if (twDoc->undoIndex<=1) {
		texWordsEdit_playErrorSound();
	} else {
		// Go back to last version
		twDoc->undoIndex-=1;
		if (twDoc->texWord) {
			StructDeInit(parse_TexWord, twDoc->texWord);
		}
		StructCopyAll(parse_TexWord, &twDoc->undoHist[twDoc->undoIndex-1], twDoc->texWord);
		texWordsEdit_layerExtentsUnApply(twDoc);
		twDoc->needToRebuildTextures = twDoc->needToRebuildUI = 1;
	}
}

AUTO_COMMAND;
void texWordsEdit_undoRedo(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	if (twDoc->undoIndex < twDoc->undoMax) {
		twDoc->undoIndex++;
		StructDeInit(parse_TexWord, twDoc->texWord);
		StructCopyAll(parse_TexWord, &twDoc->undoHist[twDoc->undoIndex-1], twDoc->texWord);
		texWordsEdit_layerExtentsUnApply(twDoc);
		twDoc->needToRebuildTextures = twDoc->needToRebuildUI = 1;
	} else {
		texWordsEdit_playErrorSound();
	}
}

static void texWordsEditUndoFunc(void *context, void *data_UNUSED)
{
	TWEditDoc *twDoc = context;
	texWordsEdit_undoUndo(NULL, twDoc);
}

static void texWordsEditRedoFunc(void *context, void *data_UNUSED)
{
	TWEditDoc *twDoc = context;
	texWordsEdit_undoRedo(NULL, twDoc);
}

AUTO_COMMAND;
void texWordsEdit_create(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	assert(twDoc->bind); // Created in tweNewDoc
	if (!twDoc->texWord) {
		char path[MAX_PATH];
		char textureName[MAX_PATH];
		TexWord *texWord = StructAlloc(parse_TexWord);
		TexWordLayer *layer;
		memset(texWord, 0, sizeof(*texWord));

		assert(eaSize(&twDoc->base_doc.files) == 2);
		strcpy(path, twDoc->base_doc.files[0]->file->filename);

		texWord->filename = allocAddFilename(path);

		layer = StructAlloc(parse_TexWordLayer);
		memset(layer, 0, sizeof(*layer));
		layer->type = TWLT_TEXT;
		layer->pos[0] = layer->pos[1] = 5;
		layer->size[0] = 100;
		layer->size[1] = 40;
		layer->layerName = StructAllocString("#2");
		eaPush(&texWord->layers, layer);

		layer = StructAlloc(parse_TexWordLayer);
		memset(layer, 0, sizeof(*layer));
		layer->type = TWLT_BASEIMAGE;
		layer->stretch = TWLS_FULL;
		layer->layerName = StructAllocString("#1");
		eaPush(&texWord->layers, layer);

		// Fill in default values and such
		texWordVerify(texWord, true, NULL);

		// Add to global lists
		eaPush(&texWords_list.texWords, texWord);
		// Extract texture name
		getFileNameNoExt(textureName, texWord->filename);
		stashAddPointer(htTexWords, textureName, texWord, true);

		if (texFind(textureName, 0)==NULL) {
			// New dynamic texture
			TexWordParams *params = createTexWordParams();

			if (twDoc->bind != white_tex) {
				texUnloadDynamic(twDoc->bind);
			}
			twDoc->bind = texFindDynamic(textureName, params, WL_FOR_UI, NULL);
			twDoc->bind = twDoc->bind->actualTexture;
			texWord->size[0] = 128;
			texWord->size[1] = 128;
		} else {
			texAllocRareData(twDoc->bind)->texWord = texWordFind(twDoc->bind->name, 0);
			texWord->size[0] = twDoc->bind->width;
			texWord->size[1] = twDoc->bind->height;
		}
		twDoc->texWord = texGetRareDataConst(twDoc->bind)->texWord;
		if (texGetRareDataConst(twDoc->bind)->hasBeenComposited || texGetRareDataConst(twDoc->bind)->texWord) {
			// Free the old composited data
			texFree(twDoc->bind, 0);
		}

		emSetDocUnsaved(&twDoc->base_doc, true);

		twDoc->needToRebuildUI = 1;
		texWordsEdit_undoReset(twDoc);
	}
}

bool texWordsEdit_fileSaveInternal(TexWord *texWord, MessageStore *messageStore)
{
	bool ok=true;
	if (texWord) {
		char fullname[MAX_PATH];
		TexWordList dummy={0};
		eaPush(&dummy.texWords, texWord);
		fileLocateWrite(texWord->filename, fullname);
		fileRenameToBak(fullname);
		mkdirtree(fullname);
		ok = 0!=ParserWriteTextFile(fullname, parse_TexWordList, &dummy, 0, 0);
		gfxStatusPrintf("File saved %s", ok?"successfully":"FAILED");
		msSaveMessageStoreFresh(messageStore);
		eaDestroy(&dummy.texWords);
	}
	return ok;
}

AUTO_COMMAND;
bool texWordsEdit_fileSave(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	bool ret;
	GET_ACTIVE_TWDOC;
	if (!twDoc->texWord)
		return true;
	ret = texWordsEdit_fileSaveInternal(twDoc->texWord, twDoc->messageStore);
	twDoc->base_doc.saved = 1; // Needed?
	return ret;
}

static void saveAsCancel(UserData dummy)
{
	ui_FileBrowserFree();
}

bool texWordsEdit_trySaveAsNewDynamic(const char *dir, const char *fileName_in, const char *base_texture_name)
{
	bool bRet=false;
	char textureName[1024];
	char fileName[MAX_PATH];
	TexWord *texWord;

	assert(!strchr(base_texture_name, '.')); // Should just be short name, otherwise fix it up here

	sprintf(fileName, "%s/%s", dir, fileName_in);
	//texWordsEdit_undoReset(twDoc);

	// Extract texture name
	getFileNameNoExt(textureName, fileName);
	if (texWord = texWordFind(textureName, 0)) {
		Alertf("There is already a layout named \"%s\" (at %s), please save under a different name.", textureName, texWord->filename);
	} else {
		// Create the TexWord
		MessageStore *messageStore = createMessageStore(1);
		char messageStoreFilename[MAX_PATH];
		TexWord *baseTexWord;
		int i;
		BasicTexture *baseTexture = texFind(base_texture_name, 0);
		TexWordLayer *layer;

		initMessageStore(messageStore, locGetIDByName("English"), NULL);

		fileLocateWrite(fileName, messageStoreFilename);
		changeFileExt(messageStoreFilename, ".ms", messageStoreFilename);
		msSetFilename(messageStore, messageStoreFilename);

		// Create a new TexWord first
		texWord = StructAlloc(parse_TexWord);

		// Look for existing TexWord to copy from
		baseTexWord = texWordFind(base_texture_name, 1);
		if (baseTexWord)
		{
			// Copy
			StructCopyAll(parse_TexWord, baseTexWord, texWord);

			// Change any BaseImage to Image
			for (i=0; i<eaSize(&texWord->layers); i++)
			{
				layer = texWord->layers[i];
				if (layer->type == TWLT_BASEIMAGE) {
					layer->type = TWLT_IMAGE;
					layer->imageName = NULL;
					if (baseTexture)
						layer->imageName = allocAddString(base_texture_name);
				}
			}
			if (baseTexWord->size[0]>0 && baseTexWord->size[1]>0)
			{
				copyVec2(baseTexWord->size, texWord->size);
			} else if (baseTexture) {
				setVec2(texWord->size, baseTexture->width, baseTexture->height);
			}

			texWord->filename = allocAddFilename(fileName);
			cloneLayerText(&texWord->layers);
			tweUpdateTextAfterEdit(NULL, texWord, messageStore);

			// add to global lists
			eaPush(&texWords_list.texWords, texWord);
			stashAddPointer(htTexWords, textureName, texWord, false);
			verify(texAllocateDynamic(textureName, true));

			// Save to disk
			bRet = texWordsEdit_fileSaveInternal(texWord, messageStore);

			// Then load in a new editor
			emOpenFileEx(textureName, "TexWord");
		} else if (baseTexture) {
			// Create based on base texture
			texWord->filename = allocAddFilename(fileName);
			setVec2(texWord->size, baseTexture->width, baseTexture->height);

			layer = StructAlloc(parse_TexWordLayer);
			memset(layer, 0, sizeof(*layer));
			layer->type = TWLT_TEXT;
			layer->pos[0] = layer->pos[1] = 5;
			layer->size[0] = 100;
			layer->size[1] = 40;
			layer->layerName = StructAllocString("#2");
			eaPush(&texWord->layers, layer);

			layer = StructAlloc(parse_TexWordLayer);
			memset(layer, 0, sizeof(*layer));
			layer->type = TWLT_IMAGE;
			layer->stretch = TWLS_FULL;
			layer->imageName = allocAddString(base_texture_name);
			layer->layerName = StructAllocString("#1");
			eaPush(&texWord->layers, layer);

			cloneLayerText(&texWord->layers);
			tweUpdateTextAfterEdit(NULL, texWord, messageStore);

			// Fill in default values and such
			texWordVerify(texWord, true, NULL);

			// Add to global lists
			eaPush(&texWords_list.texWords, texWord);
			stashAddPointer(htTexWords, textureName, texWord, false);
			verify(texAllocateDynamic(textureName, true));

			// Save it
			bRet = texWordsEdit_fileSaveInternal(texWord, messageStore);

			// Then load in a new editor
			emOpenFileEx(textureName, "TexWord");

			bRet = true;
		} else {
			Alertf("No texword or texture to base off of named \"%s\"!", base_texture_name);
		}

		// Free stuff
		destroyMessageStore(messageStore);
	}
	return bRet;
}

static bool saveAsSelectFile(const char *dir, const char *fileName_in, UserData dummy)
{
	bool bRet=false;
	const TWEditDoc *twDoc = (TWEditDoc*)emGetActiveEditorDoc();
	int i;
	TexWord *texWord;
	char textureName[1024];
	char fileName[MAX_PATH];

	assert(twDoc);

	sprintf(fileName, "%s/%s", dir, fileName_in);
	//texWordsEdit_undoReset(twDoc);

	// Extract texture name
	getFileNameNoExt(textureName, fileName);
	if (texWordFind(textureName, 0)) {
		Alertf("There is already a layout named \"%s\", please save under a different name.", textureName);
	} else {
		MessageStore *messageStore = createMessageStore(1);
		char messageStoreFilename[MAX_PATH];
		initMessageStore(messageStore, locGetIDByName(twDoc->tweLocale), NULL);

		fileLocateWrite(fileName, messageStoreFilename);
		changeFileExt(messageStoreFilename, ".ms", messageStoreFilename);
		msSetFilename(messageStore, messageStoreFilename);

		// Create a new TexWord first
		texWord = StructAlloc(parse_TexWord);
		// Copy
		StructCopyAll(parse_TexWord, twDoc->texWord, texWord);

		// Change any BaseImage to Image
		for (i=0; i<eaSize(&texWord->layers); i++)
		{
			TexWordLayer *layer = texWord->layers[i];
			if (layer->type == TWLT_BASEIMAGE) {
				layer->type = TWLT_IMAGE;
				layer->imageName = NULL;
				if (twDoc->bind)
					layer->imageName = allocAddString(twDoc->bind->name);
			}
		}
		if (twDoc->bind) {
			texWord->size[0] = twDoc->bind->width;
			texWord->size[1] = twDoc->bind->height;
		}

		texWord->filename = allocAddFilename(fileName);
		cloneLayerText(&texWord->layers);
		tweUpdateTextAfterEdit(NULL, texWord, messageStore);
		texWordVerify(texWord, true, NULL);

		// add to global lists
		eaPush(&texWords_list.texWords, texWord);
		stashAddPointer(htTexWords, textureName, texWord, false);

		// Save to disk
		bRet = texWordsEdit_fileSaveInternal(texWord, messageStore);
		// Free stuff
		//StructDestroy(parse_TexWord, texWord); - it's in the list, can't!
		destroyMessageStore(messageStore);
		// Then load in a new editor
		emOpenFileEx(textureName, "TexWord");
	}

	ui_FileBrowserFree();
	return bRet;
}


AUTO_COMMAND;
void texWordsEdit_fileSaveDynamic(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	UIWindow *file_browser;
	if (!twDoc->texWord)
		return;

	file_browser = ui_FileBrowserCreate("Choose new file name", "Save As", UIBrowseNew, UIBrowseFiles, false,
										"texts/English/texture_library/Dynamic", "texts/English/texture_library/Dynamic", NULL,
										".TexWord", saveAsCancel, NULL, saveAsSelectFile, NULL);
	ui_WindowShow(file_browser);
}

AUTO_COMMAND;
void texWordsEdit_fileRemoveTexWord(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	TexWord *texWord = twDoc->texWord;
	if (texWord) {
		bool bDynamic = stricmp(texFindFullPath(twDoc->bind), "dynamicTexture")==0;
		if (UIYes == ui_ModalDialog("Confirm Remove TexWord", "Are you sure you want to remove the layout information associated with this file (this action cannot be undone)?", ColorBlack, UIYes | UINo))
		{
			char textureName[MAX_PATH];
			char fullname[MAX_PATH];
			TexWordList dummy={0};
			texWordsEdit_layerExtentsFinish(NULL, twDoc);
			texWordsEdit_undoJournal(twDoc);

			eaPush(&dummy.texWords, texWord);
			fileLocateWrite(texWord->filename, fullname);
			if (fileExists(fullname)) {
				fileRenameToBak(fullname);
			}
			texWordMessageStoreFileName(SAFESTR(fullname), texWord);
			fileLocateWrite(fullname, fullname);
			if (fileExists(fullname)) {
				fileRenameToBak(fullname);
			}

			twDoc->base_doc.saved = true;

			// Remove from global lists
			eaFindAndRemove(&texWords_list.texWords, texWord);
			// Extract texture name
			getFileNameNoExt(textureName, texWord->filename);
			stashRemovePointer(htTexWords, textureName, NULL);

			texAllocRareData(twDoc->bind)->texWord = texWordFind(twDoc->bind->name, 0);
			twDoc->texWord = texGetRareDataConst(twDoc->bind)->texWord;
			if (texGetRareDataConst(twDoc->bind)->hasBeenComposited || texGetRareDataConst(twDoc->bind)->texWord) {
				// Free the old composited data
				texFree(twDoc->bind, 0);
			}
			twDoc->needToRebuildUI = 1;
			texWordsEdit_undoReset(twDoc);

			if (bDynamic)
			{
				Alertf("Layout for dynamic texture removed - nothing to edit, closing document");
				emCloseDoc(&twDoc->base_doc);
			}
		}
	}
}

AUTO_COMMAND;
bool texWordsEdit_generalToggleColorPicker(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	if (twDoc->colorPicker) {
		GamePrefStoreInt("TWEColorPickerX", twDoc->colorPickerX = twDoc->colorPicker->widget.x);
		GamePrefStoreInt("TWEColorPickerY", twDoc->colorPickerY = twDoc->colorPicker->widget.y);
		// Destroy!
		ui_WidgetQueueFree((UIWidget *)twDoc->colorPicker);
		eaFindAndRemove(&twDoc->base_doc.ui_windows_private, (UIWindow*)twDoc->colorPicker);
		twDoc->colorPicker = NULL;
	} else {
		twDoc->colorPicker = ui_ColorWindowCreate(NULL, twDoc->colorPickerX, twDoc->colorPickerY, 0.f, 1.f, NULL, false, false, false, true, false);
		ui_WindowSetCloseCallback(UI_WINDOW(twDoc->colorPicker), texWordsEdit_generalToggleColorPicker, twDoc);
		//ui_ColorWindowSetCallback(twDoc->colorPicker, texWordsEdit_generalToggleColorPicker);
		ui_ColorWindowSetColorAndCallback(twDoc->colorPicker, twDoc->color);
		//ui_WindowShow(twDoc->colorPicker);
		eaPush(&twDoc->base_doc.ui_windows_private, (UIWindow*)twDoc->colorPicker);
	}
	twDoc->needToRebuildUI = 1;
	return false;
}

AUTO_COMMAND;
void texWordsEdit_generalToggleOrig(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	twDoc->orig = !twDoc->orig;
	twDoc->needToRebuildUI = 1;
	twDoc->needToRebuildTextures = 1;
}


static void texWordsEdit_updateScale(TWEditDoc *twDoc)
{
	F32 x0 = -1;
	F32 y0 = 59;
	F32 w, h;
	F32 xs=1.f, ys=1.f;
	emGetCanvasSize(&x0, &y0, &w, &h);
	if (twDoc->bind) {
		if (twDoc->stretch) {
			xs = w / (F32)twDoc->bind->width;
			ys = h / (F32)twDoc->bind->height;
		}
	}
	if (!twDoc->stretch) {
		y0+=28;
	}
	// If values are changing, need to update Extents
	texWordsEdit_updateExtentsFromWidget(twDoc);
	twDoc->display.x0 = x0;
	twDoc->display.y0 = y0;
	twDoc->display.xs = xs;
	twDoc->display.ys = ys;
	texWordsEdit_updateWidgetFromExtents(twDoc);
	if (twDoc->extents.rect) {
		if (twDoc->extents.rect->resizing) {
			twDoc->extents.was_resizing = true;
		} else {
			if (twDoc->extents.was_resizing) {
				texWordsEdit_layerExtentsApply(twDoc);
			}
			twDoc->extents.was_resizing = false;
		}
	}
}

AUTO_COMMAND;
void texWordsEdit_generalToggleStretch(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	twDoc->stretch = !twDoc->stretch;
	twDoc->needToRebuildUI = 1;
	// Rebuild extents
	texWordsEdit_updateScale(twDoc);
}

AUTO_COMMAND;
void texWordsEdit_generalSetBG(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	copyVec4(twDoc->color, twDoc->bgcolor);
}

void texWordsEdit_hideAllSub(TexWordLayer *layer, TexWordLayer *except, int newHidden)
{
	if (layer == except) {
		layer->hidden = !newHidden;
	} else {
		int i;
		layer->hidden = newHidden;
		for (i=eaSize(&layer->sublayer)-1; i>=0; i--) {
			texWordsEdit_hideAllSub(layer->sublayer[i], except, newHidden);
		}
	}
}

void texWordsEdit_hideAll(TWEditDoc *twDoc, TexWordLayer *except)
{
	int i;
	for (i=eaSize(&twDoc->texWord->layers)-1; i>=0; i--) {
		texWordsEdit_hideAllSub(twDoc->texWord->layers[i], except, 1);
	}
	
	twDoc->needToRebuildUI = 1;
	twDoc->needToRebuildTextures = 1;
}

void texWordsEdit_soloLayer(UIAnyWidget *widget_UNUSED, TexWordLayer *layer)
{
	texWordsEdit_hideAll(layer->editor_parent, layer);
}

bool texWordsEdit_isSoloSub(TexWordLayer *layer, TexWordLayer *layer_ref)
{
	int i;
	if (layer_ref == layer)
		return !layer->hidden;
	if (!layer->hidden)
		return false;
	for (i=eaSize(&layer->sublayer)-1; i>=0; i--) {
		if (!texWordsEdit_isSoloSub(layer->sublayer[i], layer_ref))
			return false;
	}
	return true;
}

bool texWordsEdit_isSolo(TWEditDoc *twDoc, TexWordLayer *layer)
{
	bool ret = true;
	int i;
	for (i=eaSize(&twDoc->texWord->layers)-1; i>=0; i--) {
		ret &= texWordsEdit_isSoloSub(twDoc->texWord->layers[i], layer);
	}
	return ret;
}


AUTO_COMMAND;
void texWordsEdit_unhideAll(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	int i;
	if (!twDoc->texWord)
		return;
	for (i=eaSize(&twDoc->texWord->layers)-1; i>=0; i--) {
		texWordsEdit_hideAllSub(twDoc->texWord->layers[i], NULL, 0);
	}

	twDoc->needToRebuildUI = 1;
	twDoc->needToRebuildTextures = 1;
}


AUTO_COMMAND;
void texWordsEdit_layerCopyAll(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	if (!twDoc->texWord)
		return;

	if (layerClipboard.layers) {
		StructDeInit(parse_TexWord, &layerClipboard);
	}
	StructCopyAll(parse_TexWord, twDoc->texWord, &layerClipboard);

	tweOnAnyDataChangedCallback(NULL, twDoc);
}


static void cloneLayerText(TexWordLayer ***layerList)
{
	int num = eaSize(layerList);
	int i;
	for (i=0; i<num; i++) {
		TexWordLayer *layer = eaGet(layerList, i);
		tweUpdateTextBeforeEditLayer(layer);
		if (layer->text) {
			char *text = layer->editor_text;

			assert(layer->editor_text);

			// Free the old key
			StructFreeString(layer->text);
			layer->text = NULL;

			if (stricmp(text, "Placeholder")==0) {
				StructFreeString(layer->editor_text);
				layer->editor_text = NULL;
			} else {
				// Assign a new key happens in tweUpdateTextAfterEdit()
			}
		}
		if (layer->sublayer) {
			cloneLayerText(&layer->sublayer);
		}
	}
}

AUTO_COMMAND;
void texWordsEdit_layerPasteAll(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;

	if (!layerClipboard.layers)
		return;
	if (!twDoc->texWord) {
		texWordsEdit_create(NULL, twDoc);
	}

	assert(twDoc->texWord);

	StructDeInit(parse_TexWord, twDoc->texWord);

	StructCopyAll(parse_TexWord, &layerClipboard, twDoc->texWord);
	cloneLayerText(&twDoc->texWord->layers);
	tweUpdateTextAfterEdit(twDoc, twDoc->texWord, twDoc->messageStore);
	twDoc->texWord->filename = allocAddFilename(twDoc->base_doc.files[0]->file->filename);

	tweOnDataChangedCallback(NULL, twDoc);
}


void texWordsEdit_extentsPatchup(TWEditDoc *twDoc)
{
	TexWordLayer *layer = twDoc->extents.editLayer;
	if (!texWordsEdit_layerExists(twDoc, layer)) {
		// This layer didn't exist
		twDoc->extents.editLayer = NULL;
		twDoc->extents.edit = 0;
		//twDoc->extents.on = 0;
		if (twDoc->extents.rect) {
			ui_WidgetQueueFree(UI_WIDGET(twDoc->extents.rect));
			twDoc->extents.rect = NULL;
		}
	}
}


void texWordsEdit_layerExtentsApply(TWEditDoc *twDoc)
{
	if (twDoc->extents.edit) {
		int changed=false;
		TexWordLayer *layer = twDoc->extents.editLayer;
		if (!texWordsEdit_layerExists(twDoc, layer)) {
			// This layer didn't exist
			twDoc->extents.editLayer = NULL;
			twDoc->extents.edit = 0;
			//twDoc->extents.on = 0;
			if (twDoc->extents.rect) {
				ui_WidgetQueueFree(UI_WIDGET(twDoc->extents.rect));
				twDoc->extents.rect = NULL;
			}
			return;
		}
		texWordsEdit_undoJournal(twDoc);
		texWordsEdit_updateExtentsFromWidget(twDoc);
		if (layer->pos[0] != twDoc->extents.x) {
			changed = true;
			layer->pos[0] = twDoc->extents.x;
		}
		if (layer->pos[1] != twDoc->extents.y) {
			changed = true;
			layer->pos[1] = twDoc->extents.y;
		}
		if (layer->size[0] != twDoc->extents.w) {
			changed = true;
			layer->size[0] = twDoc->extents.w;
		}
		if (layer->size[1] != twDoc->extents.h) {
			changed = true;
			layer->size[1] = twDoc->extents.h;
		}
		if (changed) {
			tweOnDataChangedCallback(NULL, twDoc);
		} else {
			// Just update UI
			tweOnAnyDataChangedCallback(NULL, twDoc);
		}
	}
}

AUTO_COMMAND;
void texWordsEdit_layerExtentsFinish(ACMD_IGNORE UIAnyWidget *widget_UNUSED, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;

	texWordsEdit_layerExtentsApply(twDoc);
	//twDoc->extents.on = 0;
	twDoc->extents.editLayer = NULL;
	twDoc->extents.edit = 0;
	if (twDoc->extents.rect) {
		ui_WidgetQueueFree(UI_WIDGET(twDoc->extents.rect));
		twDoc->extents.rect = NULL;
	}
}


void texWordsEdit_editExtents(UIAnyWidget *widget_UNUSED, TexWordLayer *layer)
{
	TWEditDoc *twDoc = layer->editor_parent;

	if (twDoc->extents.edit && twDoc->extents.editLayer == layer) {
		texWordsEdit_layerExtentsFinish(NULL, twDoc);
	} else {
		texWordsEdit_layerExtentsFinish(NULL, twDoc);
		twDoc->extents.edit = 1;
		twDoc->extents.editLayer = layer;
		// Fix up sizes
		if (layer->size[0]==0) {
			if (layer->type == TWLT_IMAGE) {
				layer->size[0] = layer->image->width;
			} else {
				layer->size[0] = 10;
			}
		}
		if (layer->size[1]==0) {
			if (layer->type == TWLT_IMAGE) {
				layer->size[1] = layer->image->height;
			} else {
				layer->size[1] = 10;
			}
		}
	}

	texWordsEdit_layerExtentsUnApply(twDoc); // Get starting values

	twDoc->needToRebuildUI = 1;
	twDoc->needToRebuildTextures = 1;
}

AUTO_COMMAND;
void texWordsEdit_layerExtents(int layerNum, ACMD_IGNORE TWEditDoc *twDoc)
{
	GET_ACTIVE_TWDOC;
	TexWordLayer *layer;

	if (!twDoc->texWord)
		return;

	layer = eaGet(&twDoc->texWord->layers, layerNum-1);
	if (layer)
		texWordsEdit_editExtents(NULL, layer);
}


static bool changeTextureCallback(EMPicker *picker, EMPickerSelection **selections, TexWordLayer *layer)
{
	char texname[MAX_PATH];
	
	if (!eaSize(&selections))
		return false;

	getFileNameNoExt(texname, selections[0]->doc_name);
	layer->imageName = allocAddString(texname);
	tweOnDataChangedCallback(NULL, layer->editor_parent);

	return true;
}

void texWordsEdit_changeTexture(UIAnyWidget *widget_UNUSED, TexWordLayer *layer)
{
	EMPicker* picker = emPickerGetByName( "Texture Picker" );

	if (picker)
		emPickerShow(picker, NULL, false, changeTextureCallback, layer);
}

void texWordsEdit_removeDropShadow(UIAnyWidget *widget_UNUSED, TexWordLayer *layer)
{
	setVec2(layer->font.dropShadowOffset, 0, 0);
	tweOnDataChangedCallback(NULL, layer->editor_parent);
}

void texWordsEdit_toggleColorMode(UIAnyWidget *widget_UNUSED, TexWordLayer *layer)
{
	int i, j;
	int newmode;
	if (eaiSize(&(layer->rgbas[0])))
		newmode = 1;
	else
		newmode = 4;

	while (eaiSize(&layer->rgba) != 4)
		eaiPush(&layer->rgba, 255);

	if (newmode==1) {
		for (i=0; i<4; i++) 
			layer->rgba[i] = 0;
		for (i=0; i<4; i++) {
			for (j=0; j<4; j++) 
				layer->rgba[j]+=(eaiSize(&layer->rgbas[i])>j)?layer->rgbas[i][j]:255;
			eaiDestroy(&layer->rgbas[i]);
		}
		for (i=0; i<4; i++) 
			layer->rgba[i] /=4;
	} else {
		for (i=0; i<4; i++) {
			if (layer->rgbas[i])
				eaiDestroy(&layer->rgbas[i]);
			for (j=0; j<4; j++) 
				eaiPush(&layer->rgbas[i], layer->rgba[j]);
		}
		eaiDestroy(&layer->rgba);
	}

	tweOnDataChangedCallback(NULL, layer->editor_parent);
}

typedef struct CopyLayerData {
	TexWordLayer *layer;
	TexWordLayer *src;
} CopyLayerData;
void texWordsEdit_copySize(UIAnyWidget *widget_UNUSED, CopyLayerData *data)
{
	copyVec2(data->src->size, data->layer->size);
	tweOnDataChangedCallback(NULL, data->layer->editor_parent);
}

void texWordsEdit_copyPos(UIAnyWidget *widget_UNUSED, CopyLayerData *data)
{
	copyVec2(data->src->pos, data->layer->pos);
	tweOnDataChangedCallback(NULL, data->layer->editor_parent);
}

void texWordsEdit_copyRot(UIAnyWidget *widget_UNUSED, CopyLayerData *data)
{
	data->layer->rot = data->src->rot;
	tweOnDataChangedCallback(NULL, data->layer->editor_parent);
}

void texWordsEdit_onLayerFilterTypeChange(UIRTNode *node, TWEditDoc *twDoc)
{
	TexWordLayerFilter *filter = node->structptr;

	if (filter->type == TWFILTER_BLUR || filter->type == TWFILTER_DESATURATE) {
		filter->blend = TWBLEND_REPLACE;
		filter->offset[0] = 0;
		filter->offset[1] = 0;
	} else {
		filter->blend = TWBLEND_OVERLAY;
	}

	tweOnDataChangedCallback(NULL, twDoc);
}

void texWordsEdit_layerFilterDelete(UIAnyWidget *widget_UNUSED, TexWordLayerFilter *filter)
{
	TWEditDoc *twDoc=NULL;
	GET_ACTIVE_TWDOC;
	TexWordLayer *layer;

	if (!filter)
		filter = twDoc->filterPopupFilter;
	layer = filter->layer_parent;

	eaFindAndRemove(&layer->filter, filter);
	StructDestroy(parse_TexWordLayerFilter, filter);

	tweOnDataChangedCallback(NULL, layer->editor_parent);
}

typedef struct FilterOpData {
	TexWordLayer *layer;
	intptr_t filterIndex;
} FilterOpData;

void texWordsEdit_filterMoveDown(UIAnyWidget *widget_UNUSED, FilterOpData *data)
{
	TexWordLayer *layer = data->layer;
	assert(data->filterIndex >= 0 && data->filterIndex < eaSize(&layer->filter) - 1);
	eaSwap(&layer->filter, data->filterIndex, data->filterIndex + 1);
	tweOnDataChangedCallback(NULL, layer->editor_parent);
}

void texWordsEdit_filterMoveUp(UIAnyWidget *widget_UNUSED, FilterOpData *data)
{
	TexWordLayer *layer = data->layer;
	assert(data->filterIndex > 0 && data->filterIndex < eaSize(&layer->filter));
	eaSwap(&layer->filter, data->filterIndex, data->filterIndex - 1);
	tweOnDataChangedCallback(NULL, layer->editor_parent);
}

void texWordsEdit_insertFilter(UIAnyWidget *widget_UNUSED, FilterOpData *data)
{
	TexWordLayer *layer = data->layer;
	TexWordLayerFilter *filter;

	filter = StructAlloc(parse_TexWordLayerFilter);
	memset(filter, 0, sizeof(*filter));
	filter->type = TWFILTER_DROPSHADOW;
	filter->magnitude = 3;
	filter->offset[0] = filter->offset[1] = 3;
	eaiCreate(&filter->rgba);
	eaiPush(&filter->rgba, 0);
	eaiPush(&filter->rgba, 0);
	eaiPush(&filter->rgba, 0);
	eaiPush(&filter->rgba, 255);
	filter->spread = 0.5;

	eaInsert(&layer->filter, filter, data->filterIndex);

	tweOnDataChangedCallback(NULL, layer->editor_parent);
}

void texWordsEdit_insertFilterRelative(UIAnyWidget *widget_UNUSED, void *index_p)
{
	intptr_t index = (intptr_t)index_p;
	FilterOpData data;
	TWEditDoc *twDoc=NULL;
	GET_ACTIVE_TWDOC;

	data.layer = twDoc->filterPopupFilter->layer_parent;
	data.filterIndex = eaFind(&data.layer->filter, twDoc->filterPopupFilter);
	assert(data.filterIndex!=-1);
	data.filterIndex += index;
	texWordsEdit_insertFilter(NULL, &data);
}

static TexWordLayer ***getLayersListForLayer(TexWordLayer ***layersList, TexWordLayer *layer)
{
	int i;
	if (!layersList || !*layersList)
		return NULL;

	for (i=eaSize(layersList)-1; i>=0; i--) {
		TexWordLayer ***ret;
		TexWordLayer *test = eaGet(layersList, i);
		if (test==layer)
			return layersList;
		ret = getLayersListForLayer(&test->sublayer, layer);
		if (ret)
			return ret;
	}
	return NULL;
}

void texWordsEdit_layerDelete(UIAnyWidget *widget_UNUSED, TexWordLayer *oldLayer)
{
	TexWordLayer ***layersList;
	int relativeIndex;
	TWEditDoc *editor_parent;

	if (!oldLayer) {
		TWEditDoc *twDoc=NULL;
		GET_ACTIVE_TWDOC;
		oldLayer = twDoc->layerPopupLayer;
	}

	layersList = getLayersListForLayer(&oldLayer->editor_parent->texWord->layers, oldLayer);

	texWordsEdit_layerExtentsFinish(NULL, oldLayer->editor_parent);

	relativeIndex = eaFind(layersList, oldLayer);
	assert(relativeIndex!=-1);
	eaRemove(layersList, relativeIndex);
	if (eaSize(layersList)==0) {
		eaDestroy(layersList);
	}
	editor_parent = oldLayer->editor_parent;
	StructDestroy(parse_TexWordLayer, oldLayer);

	tweOnDataChangedCallback(NULL, editor_parent);
}

typedef struct LayerMoveData {
	TexWordLayer *layer;
	intptr_t offset;
} LayerMoveData;
void texWordsEdit_layerMove(UIAnyWidget *widget_UNUSED, LayerMoveData *data)
{
	int index0, index1;
	TWEditDoc *twDoc = data->layer->editor_parent;

	index0 = eaFind(&twDoc->texWord->layers, data->layer);
	assert(index0!=-1);
	assert(data->offset);
	index1 = index0 + data->offset;
	eaSwap(&twDoc->texWord->layers, index0, index1);

	tweOnDataChangedCallback(NULL, twDoc);
}

void texWordsEdit_layerSubInsert(UIAnyWidget *widget_UNUSED, TexWordLayer *layer)
{
	TexWordLayer *sublayer;
	assert(!(layer->sublayer && layer->sublayer[0]));

	sublayer = StructAlloc(parse_TexWordLayer);
	memset(sublayer, 0, sizeof(*sublayer));
	eaPush(&layer->sublayer, sublayer);

	layer->subBlend = TWBLEND_MULTIPLY;
	layer->subBlendWeight = 1.0;
	sublayer->imageName = allocAddString("white");
	sublayer->stretch = TWLS_FULL;
	sublayer->type = TWLT_IMAGE;

	tweOnDataChangedCallback(NULL, layer->editor_parent);
}

static TexWordLayer *findLayerByName(TexWordLayer ***layersList, char *layerName)
{
	int i;
	for (i=0; i<eaSize(layersList); i++) {
		if (stricmp(layerName, (*layersList)[i]->layerName)==0)
			return (*layersList)[i];
	}
	return NULL;
}

static char *generateName(TexWordLayer ***layersList, char *sourceName)
{
	static char buf[256];
	// Generate new name
	if (sourceName) {
		strncpyt(buf, sourceName, ARRAY_SIZE(buf) - 8);
		if (!strstri(buf, "copy")) {
			strcat(buf, " copy");
		}
		while (findLayerByName(layersList, buf)) {
			if (strEndsWith(buf, "copy")) {
				// Already has a "copy"!
				strcat(buf, " 2");
			} else {
				incrementName(buf, ARRAY_SIZE(buf)-1);
			}
		}
	} else {
		strcpy(buf, "#1");
		while (findLayerByName(layersList, buf)) {
			incrementName(buf, ARRAY_SIZE(buf)-1);
		}
	}
	return buf;
}

static void texWordsEdit_insertLayer(UIAnyWidget *widget_UNUSED, void *index_p)
{
	intptr_t index = (intptr_t)index_p;
	TexWordLayer *layer;
	TWEditDoc *twDoc=NULL;
	GET_ACTIVE_TWDOC;
	layer = StructAlloc(parse_TexWordLayer);
	// Default new layer: text
	layer->type = TWLT_TEXT;
	layer->pos[0] = layer->pos[1] = 5;
	layer->size[0] = 100;
	layer->size[1] = 40;
	layer->editor_parent = twDoc;

	layer->layerName = StructAllocString(generateName(&twDoc->texWord->layers, NULL));
	eaInsert(&twDoc->texWord->layers, layer, index);
	tweOnDataChangedCallback(NULL, twDoc);
}

static void texWordsEdit_insertLayerRelative(UIAnyWidget *widget_UNUSED, void *index_p)
{
	intptr_t index = (intptr_t)index_p;
	TWEditDoc *twDoc=NULL;
	GET_ACTIVE_TWDOC;
	if (twDoc->layerPopupIndex < 0)
		return;
	texWordsEdit_insertLayer(NULL, (void*)(twDoc->layerPopupIndex + index));
}

static void texWordsEdit_layerClone(UIAnyWidget *widget_UNUSED, TexWordLayer *oldLayer)
{
	TexWordLayer ***layersList;
	TexWordLayer *newLayer;
	int relativeIndex;

	if (!oldLayer) {
		TWEditDoc *twDoc=NULL;
		GET_ACTIVE_TWDOC;
		oldLayer = twDoc->layerPopupLayer;
	}

	layersList = getLayersListForLayer(&oldLayer->editor_parent->texWord->layers, oldLayer);

	relativeIndex = eaFind(layersList, oldLayer);
	assert(relativeIndex!=-1);

	newLayer = StructAlloc(parse_TexWordLayer);
	StructCopyFields(parse_TexWordLayer, oldLayer, newLayer, 0, 0);
	if (newLayer->text) {
		StructFreeString(newLayer->text);
		newLayer->text = NULL;
	}
	assert(!newLayer->editor_text);
	if (oldLayer->editor_text)
	{
		newLayer->editor_text = StructAllocString(oldLayer->editor_text);
	}

	if (newLayer->layerName) {
		char *temp = StructAllocString(generateName(layersList, newLayer->layerName));
		StructFreeString(newLayer->layerName);
		newLayer->layerName = temp;
	} else {
		newLayer->layerName = StructAllocString(generateName(layersList, NULL));
	}

	eaInsert(layersList, newLayer, relativeIndex);

	tweOnDataChangedCallback(NULL, oldLayer->editor_parent);
}

void tweUpdateTextBeforeEditLayer(TexWordLayer *layer)
{
	char translated[2048];
	if (!layer->text || !layer->text[0])
		return;
	msPrintf(texWordsMessages, SAFESTR(translated), layer->text);

	if (!layer->editor_text || stricmp(translated, layer->editor_text)!=0) {
		if (layer->editor_text)
			StructFreeString(layer->editor_text);
		layer->editor_text = StructAllocString(translated);
	}
}

static void tweUpdateTextAfterEditLayer(SA_PARAM_OP_VALID TWEditDoc *twDoc, TexWord *texWord, TexWordLayer *layer, MessageStore *messageStore)
{
	int i;

	if (layer->editor_text) {
		char oldtranslated[2048];
		if (layer->text)
			msPrintf(texWordsMessages, SAFESTR(oldtranslated), layer->text);
		if (!layer->text || 0!=stricmp(oldtranslated, layer->editor_text)) {
			// Text has changed, need to save it!
			char keyString[1024];
			char *newString = layer->editor_text;

			if (twDoc)
				emSetDocUnsaved(&twDoc->base_doc, true);

			if (layer->text && layer->text[0] && stricmp(layer->text, "Placeholder")!=0) {
				// We already have a keyString, just update the translation for this string
				strcpy(keyString, layer->text);
			} else {
				unsigned char *s;
				// Generate a new key!
				getFileNameNoExt(keyString, texWord->filename);
				strcat(keyString, "/");
				strcat(keyString, newString);
				for (s=keyString; *s; ) {
					if (!isalnum((unsigned char)*s) && *s!='_' && *s!='/') {
						strcpy_unsafe(s, s+1);
						s[0] = toupper(s[0]);
					} else {
						s++;
					}
				}
				// Verify that this is a unique key, if not, increment
				while (msContainsKey(texWordsMessages, keyString)) {
					incrementName(keyString, ARRAY_SIZE(keyString)-1);
				}
			}
			// Save the Key string/entered string in message store
			msUpdateMessage(texWordsMessages, keyString, newString, NULL);
			msUpdateMessage(messageStore, keyString, newString, NULL);

			if (layer->text)
				StructFreeString(layer->text);
			layer->text = StructAllocString(keyString);
		}
	}

	// Do sub-layers
	for (i=eaSize(&layer->sublayer)-1; i>=0; i--) {
		tweUpdateTextAfterEditLayer(twDoc, texWord, layer->sublayer[i], messageStore);
	}
}

static void tweUpdateTextAfterEdit(TWEditDoc *twDoc, TexWord *texWord, MessageStore *messageStore)
{
	int i;
	// TODO: Prune unused keys?
	for (i=eaSize(&texWord->layers)-1; i>=0; i--) {
		tweUpdateTextAfterEditLayer(twDoc, texWord, texWord->layers[i], messageStore);
	}
}

void tweOnAnyDataChangedCallback(UIRTNode *node_UNUSED, TWEditDoc *twDoc)
{
	texWordVerify(twDoc->texWord, true, NULL);
	texWordsEdit_undoJournal(twDoc);
	twDoc->needToRebuildUI = 1;
}

void tweOnDataChangedCallback(UIRTNode *node_UNUSED, TWEditDoc *twDoc)
{
	tweUpdateTextAfterEdit(twDoc, twDoc->texWord, twDoc->messageStore);
	tweOnAnyDataChangedCallback(NULL, twDoc);
	twDoc->needToRebuildTextures = 1;
}

static char *makeColorStringFromInts(int color[4])
{
	static char colorstring[64];
	sprintf(colorstring, "<color #%02x%02x%02x%02x>%d,%d,%d,%d</color>", color[0], color[1], color[2], color[3], color[0], color[1], color[2], color[3]);
	return colorstring;
}

static char *makeColorStringFromEArray(EArrayIntHandle color) {
	int param[4];

	if (color) {
		copyVec3(color, param);
		param[3] = eaiSize(&color)==4?color[3]:255;
	} else {
		setVec4same(param, 255);
	}
	return makeColorStringFromInts(param);
}
static char *makeColorStringFromVec4(Vec4 color) {
	int param[4];
	setVec4(param, 
		(int)(color[0]*255),
		(int)(color[1]*255),
		(int)(color[2]*255),
		(int)(color[3]*255));
	return makeColorStringFromInts(param);
}
typedef struct SetLayerColorData {
	TexWordLayer *layer;
	intptr_t index;
} SetLayerColorData;

static void texWordsEdit_setLayerColorFunc(UIAnyWidget *widget_UNUSED, SetLayerColorData *set_color_data)
{
	int i;
	EArrayIntHandle *color;
	TexWordLayer *layer = set_color_data->layer;
	TWEditDoc *twDoc = layer->editor_parent;
	int colorindex = set_color_data->index;

	assert(colorindex>=0 && colorindex<=4);
	if (colorindex==0) {
		color = &layer->rgba;
	} else {
		color = &layer->rgbas[colorindex-1];
	}

	while (eaiSize(color) != 4)
		eaiPush(color, 255);
	for (i=0; i<4; i++)
		(*color)[i] = (int)(twDoc->color[i] * 255);

	tweOnDataChangedCallback(NULL, twDoc);
}

typedef struct SetFilterColorData {
	TexWordLayerFilter *filter;
	TexWordLayer *layer;
} SetFilterColorData;

static void texWordsEdit_setFilterColorFunc(UIAnyWidget *widget_UNUSED, SetFilterColorData *data)
{
	TexWordLayerFilter *filter = data->filter;
	TWEditDoc *twDoc = data->layer->editor_parent;
	int i;
	while (eaiSize(&filter->rgba) != 4)
		eaiPush(&filter->rgba, 255);
	for (i=0; i<4; i++) 
		filter->rgba[i] = (int)(twDoc->color[i] * 255);

	tweOnDataChangedCallback(NULL, twDoc);
}

typedef struct ColorChoiceData {
	TWEditDoc *twDoc;
	UIActivationFunc func;
	void *userData;
} ColorChoiceData;

static void colorChoiceCallback(UIAnyWidget *widget, bool finished, ColorChoiceData *data)
{
	Vec4 color;
	ui_ColorButtonGetColor((UIColorButton*)widget, color);
	copyVec4(color, data->twDoc->color);
	data->func(widget, data->userData);
}

typedef struct CopyColorData {
	TWEditDoc *twDoc;
	Color c;
} CopyColorData;

static void copyColorCallback(UIAnyWidget *widget, CopyColorData *data)
{
	colorToVec4(data->twDoc->color, data->c);
	tweOnAnyDataChangedCallback(NULL, data->twDoc);
}

static void addColorChoice(UIRTNode *root, TWEditDoc *twDoc, EArrayIntHandle currentColor, char *name, UIActivationFunc func, void *userData)
{
	Color c;
	char buf[1024];
	//RTNode *sub;
	F32 alpha;
	if (eaiSize(&currentColor)==4) {
		alpha = currentColor[3]*U8TOF32_COLOR;
	} else {
		alpha = 1.0;
	}

	//sprintf(buf, USE_SMF "%s: %s", name, makeColorStringFromEArray(currentColor));
	//sub = addGroupEx(root, buf, name, false);

	ui_RebuildableTreeAddLabel(root, name, NULL, true);
	ui_RebuildableTreeAddLabel(root, ": ", NULL, false);

	{
		Vec4 color;
		UIColorButton *widget;
		int i;
		for (i=0; i<4; i++)
			color[i] = (eaiSize(&currentColor)>i?currentColor[i]:255) * U8TOF32_COLOR;
		vec4ToColor(&c, color);
		widget = ui_ColorButtonCreate(0, 0, color);
		ui_ColorButtonSetChangedCallback(widget, colorChoiceCallback, TRIPLE(twDoc, func, userData));
		ui_RebuildableTreeAddWidget(root, (UIWidget*)widget, NULL, false, "ColorPicker", NULL);
	}

	sprintf(buf, USE_SMF "Paste %s", makeColorStringFromVec4(twDoc->color));
	addButtonEx(root, buf, func, userData, false, "Set the color to the most recently selected/copied color");

	sprintf(buf, USE_SMF "Copy %s", makeColorStringFromEArray(currentColor));
	addButtonEx(root, buf, copyColorCallback, PAIR(twDoc, *(U32*)&c), false, "Copy this color");
}

static void addCopyFrom(UIRTNode *parent, TexWordLayer **allLayers, TexWordLayer *layer, UIActivationFunc func)
{
	int i;
	UIRTNode *menu = addGroup(parent, "Copy from layer...", false, "Copy values from one layer into this layer");
	for (i=0; i<eaSize(&allLayers); i++) {
		if (allLayers[i] != layer) {
			CopyLayerData *data;
			eaPush(&layer->editor_parent->eaCallbackData, data = calloc(sizeof(*data), 1));
			data->layer = layer;
			data->src = allLayers[i];
			addButtonEx(menu, allLayers[i]->layerName, func, data, false, "Copy values from the specified layer into this layer");
		}
	}
}

static void layerContextMenuCallback(UIExpander *widget_UNUSED, TexWordLayer *layer)
{
	TWEditDoc *twDoc=NULL;
	GET_ACTIVE_TWDOC;

	twDoc->layerPopupIndex = eaFind(&twDoc->texWord->layers, layer);
	twDoc->layerPopupLayer = layer;
	ui_MenuPopupAtCursor(twDoc->layerPopupMenu);
}

static void filterContextMenuCallback(UIExpander *widget_UNUSED, TexWordLayerFilter *filter)
{
	TWEditDoc *twDoc=NULL;
	GET_ACTIVE_TWDOC;
	twDoc->filterPopupFilter = filter;
	ui_MenuPopupAtCursor(twDoc->filterPopupMenu);
}

static void addLayerToMenu(TWEditDoc *twDoc, UIRTNode *root, TexWordLayer *layer, TexWordLayer *parentlayer, int relativeIndex, int depth, TexWordLayer **allLayers, char *parentLayerString)
{
	UIRTNode *layeritem;
	char indexStringPrint[32];
	char buf[1024];
	char indexString[32];
	int i;
	UIWidget *widget;
#define	DEFAULT_ALIGN "AlignTo 80\n"
	UIAutoWidgetParams params_align = *PARAMS(DEFAULT_ALIGN);

	layer->editor_parent = twDoc; // Set parent for callbacks
	if (!layer->uid)
		layer->uid = ++twDoc->layerUID;

	// Calculate internal index number
	sprintf(indexString, "%d", layer->uid);

	if (parentLayerString) {
		sprintf(indexStringPrint, "%s.%s", parentLayerString, layer->layerName);
	} else {
		sprintf(indexStringPrint, "%s", layer->layerName);
	}

	if (parentLayerString) {
		sprintf(buf, USE_SMF "Sub-Layer \"" USER_PARAMETER("%s") "\"", indexStringPrint);
		layeritem = addGroupEx(root, buf, indexString, 0, "A sub layer is blended with the layer it's attached to");

		addAutoWidget(layeritem, parse_TexWordLayer, "SubBlend", layer, true, tweOnDataChangedCallback, twDoc, NULL, "The way the sub layer is blended with it's parent layer");
		addAutoWidget(layeritem, parse_TexWordLayer, "SubBlendWeight", layer, true, tweOnDataChangedCallback, twDoc, NULL, "How heavily the sublayer is applied.  0 would cause the sub-layer to have no effect.");
	} else {
		sprintf(buf, USE_SMF "Layer \"" USER_PARAMETER("%s") "\" (" USER_PARAMETER("%s") ")", indexStringPrint, StaticDefineIntRevLookup(ParseTexWordLayerType, layer->type));
		if (layer->type == TWLT_TEXT) {
			tweUpdateTextBeforeEditLayer(layer);
			strcatf(buf, " (\"" USER_PARAMETER("%s") "\")", layer->editor_text);
		}
		layeritem = addGroupEx(root, buf, indexString, 0, "A layer containing text or an image");
		ui_ExpanderSetHeaderContextCallback(layeritem->expander, layerContextMenuCallback, layer);
	}

	addAutoWidget(layeritem, parse_TexWordLayer, "Name", layer, true, tweOnAnyDataChangedCallback, twDoc, &params_align, "Name of the layer (only used in the editor)");
	addAutoWidget(layeritem, parse_TexWordLayer, "Type", layer, true, tweOnDataChangedCallback, twDoc, &params_align, "The type of layer");
	addAutoWidget(layeritem, parse_TexWordLayer, "Hidden", layer, true, tweOnDataChangedCallback, twDoc, NULL, "Hides a layer (usually just for debugging/troubleshooting)");
	if (!parentLayerString) { // Sub-layers can't be set as Solo
		if (texWordsEdit_isSolo(twDoc, layer)) {
			addButton(layeritem, "Unhide All [J]", texWordsEdit_unhideAll, "Unhide all layers which are hidden");
		} else {
			addButtonEx(layeritem, "Hide Others", texWordsEdit_soloLayer, layer, false, "Hide all layers except for this layer");
		}
	}

	if (layer->stretch != TWLS_FULL || layer->type == TWLT_TEXT) {
		char buttonText[1024];
		UIButton *button;
		bool editingExtents = (twDoc->extents.edit && twDoc->extents.editLayer == layer);
		if (parentLayerString || relativeIndex > 9) {
			sprintf(buttonText, "%s Extents", editingExtents?"Editing":"Edit");
		} else {
			if (editingExtents) {
				strcpy(buttonText, USE_SMF "Editing Extents" SMF_HOTKEY("[Esc]"));
			} else {
				sprintf(buttonText, USE_SMF "%s Extents" SMF_HOTKEY("[%d]"), editingExtents?"Editing":"Edit", relativeIndex + 1);
			}
		}
		button = addButtonEx(layeritem, buttonText, texWordsEdit_editExtents, layer, false, editingExtents?"Commit any changes to the extents of this layer":"Edit the extents (position, size) of this layer");
		if (editingExtents) {
			Vec4 color = {1.f, 0.5f, 0.5f, 1.f};
			// Color the button or something!
			button->widget.pOverrideSkin = NULL;
			vec4ToColor(&button->widget.color[0], color);
		}
	}

	if (layer->type == TWLT_IMAGE)
	{
		// Image selection
		sprintf(buf, "Image \"%s\"", layer->imageName?layer->imageName:"white");
		addButtonEx(layeritem, buf, texWordsEdit_changeTexture, layer, true, "Change the image/texture used on this layer");

		// TODO: Add right-click menu with MRU list
	}

	if (layer->type == TWLT_BASEIMAGE || layer->type == TWLT_IMAGE) // Or for everything?
	{
		addAutoWidget(layeritem, parse_TexWordLayer, "Stretch", layer, true, tweOnDataChangedCallback, twDoc, &params_align, "Change how this layer is stretched");
	}

	if (layer->type == TWLT_TEXT)
	{
		UIRTNode *fontmenu;
		// Text
		tweUpdateTextBeforeEditLayer(layer);
		addAutoWidget(layeritem, parse_TexWordLayer, "Text", layer, true, tweOnDataChangedCallback, twDoc, &params_align, "The text in the current locale to be used on this layer (this will be translated for other languages)");

		// Font
		fontmenu = addGroupEx(layeritem, "Font", NULL, false, "Font settings");
		{
			// Font Name: Combo box and text entry (or just a combo box?)
			widget = addAutoWidget(fontmenu, parse_TexWordLayerFont, "Name", &layer->font, true, tweOnDataChangedCallback, twDoc, &params_align, "Which font file will be used for this layer's text");
			if (!((UITextEntry*)widget)->cb)
				ui_TextEntrySetComboBox((UITextEntry*)widget, ui_ComboBoxCreate(1, 1, 1, NULL, gfxFontGetFontNames(), NULL));
			addAutoWidget(fontmenu, parse_TexWordLayerFont, "Bold", &layer->font, false, tweOnDataChangedCallback, twDoc, NULL, "Use the bold version of the font, or manually bolden if no bold version exists.");
			addAutoWidget(fontmenu, parse_TexWordLayerFont, "Italic", &layer->font, false, tweOnDataChangedCallback, twDoc, NULL, "Use the italic version of the font, or manually italicize of no italic version exists.");
			// Disabling low-level outline/dropshadow, because it's confusing to artists with fairly little benefit
			//addAutoWidget(fontmenu, parse_TexWordLayerFont, "Outline", &layer->font, true, tweOnDataChangedCallback, twDoc, &params_align, "Enable black outlining on each glyph.  Note: you can often get a better/softer outline by applying a Dropshadow filter to this layer instead");
			addAutoWidget(fontmenu, parse_TexWordLayerFont, "Size", &layer->font, true, tweOnDataChangedCallback, twDoc, &params_align, "Font size affects the size the font is drawn at before stretching to fit the designated extents.  Setting this value as close to the actual size will produce the best results.");
			addAutoWidget(fontmenu, parse_TexWordLayerFont, "Align", &layer->font, true, tweOnDataChangedCallback, twDoc, &params_align, "Fill alignment will not respect the aspect ratio of the font, other alignments will.");
			//addAutoWidget(fontmenu, parse_TexWordLayerFont, "DropShadow", &layer->font, true, tweOnDataChangedCallback, twDoc, &params_align,  "Enable a black drop shadow on each glyph.  Effectively limited to around 5px offsets.  Note: you can often get a better/softer outline by applying a Dropshadow filter to this layer instead");
			if (layer->font.dropShadowOffset[0] || layer->font.dropShadowOffset[1]) {
				addButtonEx(fontmenu, "Remove Dropshadow", texWordsEdit_removeDropShadow, layer, false, "Remove any drop shadow from this font");
			}
		}
	}

 	if (eaiSize(&(layer->rgbas[0]))) {
		// 4-color mode
		UIRTNode *colormenu = addGroupEx(layeritem, USE_SMF "Color " USER_PARAMETER("(4-color mode)"), "Color", false, "Color settings");
		addButtonEx(colormenu, "Switch to Single-color mode", texWordsEdit_toggleColorMode, layer, true, "Use a single flat color for the whole layer");

		for (i=0; i<4; i++) {
			sprintf(buf, "Color%s",
				i==0?"UL":
				i==1?"UR":
				i==2?"LR":
				i==3?"LL":0);
			addColorChoice(colormenu, twDoc, layer->rgbas[i], buf, texWordsEdit_setLayerColorFunc, PAIR(layer, i+1));
		}
	} else {
		UIRTNode *colormenu = addGroupEx(layeritem, USE_SMF "Color " USER_PARAMETER("(Single-color mode)"), "Color", false, "Color settings");
		addButtonEx(colormenu, "Switch to 4-color mode", texWordsEdit_toggleColorMode, layer, true, "Use a different color for each corner");

		addColorChoice(colormenu, twDoc, layer->rgba, "Color", texWordsEdit_setLayerColorFunc, PAIR(layer, 0));
	}

	if (layer->stretch != TWLS_FULL || layer->type == TWLT_TEXT) {
		UIRTNode *submenu;

		// Size : WxH
		sprintf(buf, USE_SMF "Size: " USER_PARAMETER("%1.f") "x" USER_PARAMETER("%1.f"), layer->size[0], layer->size[1]);
		submenu = addGroupEx(layeritem, buf, "Size", false, "The size of this layer");
		addAutoWidget(submenu, parse_TexWordLayer, "Size", layer, true, tweOnDataChangedCallback, twDoc, &params_align, "The size of this layer");
		addCopyFrom(submenu, allLayers, layer, texWordsEdit_copySize);

		// Pos : X,Y
		sprintf(buf, USE_SMF "Position: " USER_PARAMETER("%1.f") ", " USER_PARAMETER("%1.f") "", layer->pos[0], layer->pos[1]);
		submenu = addGroupEx(layeritem, buf, "Position", false, "The position of this layer, (0,0) is the upper-left");
		addAutoWidget(submenu, parse_TexWordLayer, "Pos", layer, true, tweOnDataChangedCallback, twDoc, &params_align, "The position of this layer, (0,0) is the upper-left");
		addCopyFrom(submenu, allLayers, layer, texWordsEdit_copyPos);

		// Rotation : R
		sprintf(buf, USE_SMF "Rotation: " USER_PARAMETER("%1.f") " degrees", layer->rot);
		submenu = addGroupEx(layeritem, buf, "Rotation", false, "Rotation for this layer");
		addAutoWidget(submenu, parse_TexWordLayer, "Rot", layer, true, tweOnDataChangedCallback, twDoc, PARAMS("Type Slider\n Min 0\n Max 360\n Step 0\n" DEFAULT_ALIGN), "Rotation for this layer");
		addAutoWidget(submenu, parse_TexWordLayer, "Rot", layer, false, tweOnDataChangedCallback, twDoc, PARAMS("NoLabel\n"), "Manually enter rotation for this layer");
		addCopyFrom(submenu, allLayers, layer, texWordsEdit_copyRot);
	}

	// Filters
	{
		int filterIndex;
		UIRTNode *allFiltersMenu;
		sprintf(buf, "Filters (%d)", eaSize(&layer->filter));
		allFiltersMenu = addGroupEx(layeritem, buf, "Filters", true, "Filters adjust the visual look of a layer");

		if (eaSize(&layer->filter)==0)
			addButtonEx(allFiltersMenu, "Insert New Filter", texWordsEdit_insertFilter, PAIR(layer, 0), true, "Insert New Filter");

		for (filterIndex=0; filterIndex<eaSize(&layer->filter); filterIndex++) 
		{
			TexWordLayerFilter *filter = layer->filter[filterIndex];
			UIRTNode *filterMenu;
			char key[64];
			bool bDidNewline;

			if (!filter->uid)
				filter->uid = ++twDoc->filterUID;
			filter->layer_parent = layer;

			sprintf(key, "FILTER%d", filter->uid);

			if (filter->type == TWFILTER_DESATURATE) {
				sprintf(buf, "Filter: %s %1.f%%", StaticDefineIntRevLookup(ParseTexWordFilterType, filter->type), filter->percent*100);
			} else {
				sprintf(buf, "Filter: %s %dpx", StaticDefineIntRevLookup(ParseTexWordFilterType, filter->type), filter->magnitude);
			}
			filterMenu = addGroupEx(allFiltersMenu, buf, key, false, "A filter applied to this layer");
			ui_ExpanderSetHeaderContextCallback(filterMenu->expander, filterContextMenuCallback, filter);

			// Filter|Type
			addAutoWidget(filterMenu, parse_TexWordLayerFilter, "Type", filter, true, texWordsEdit_onLayerFilterTypeChange, twDoc, &params_align, "What type of filtering to apply");

			// Filter|Magnitude
			if (filter->type == TWFILTER_BLUR || filter->type == TWFILTER_DROPSHADOW)
			{
				// Slider, 1..10
				addAutoWidget(filterMenu, parse_TexWordLayerFilter, "Magnitude", filter, true, tweOnDataChangedCallback, twDoc, PARAMS("Type Slider\n Min 1\n Max 10\n Step 1\n" DEFAULT_ALIGN), "The magnitude of the effect");
				addAutoWidget(filterMenu, parse_TexWordLayerFilter, "Magnitude", filter, false, tweOnDataChangedCallback, twDoc, PARAMS("NoLabel\n"), "The magnitude of the effect");
			}

			// Filter|Percent
			if (filter->type == TWFILTER_DESATURATE) {
				// Slider, 0.0 ... 1.0
				addAutoWidget(filterMenu, parse_TexWordLayerFilter, "Percent", filter, true, tweOnDataChangedCallback, twDoc, PARAMS("Type Slider\n Min 0.0\n Max 1.0\n" DEFAULT_ALIGN), "The magnitude of the effect");
				addAutoWidget(filterMenu, parse_TexWordLayerFilter, "Percent", filter, false, tweOnDataChangedCallback, twDoc, PARAMS("NoLabel\n"), "The magnitude of the effect");
			}

			// Filter|Spread
			if (filter->type == TWFILTER_DROPSHADOW) {
				// Slider, 0.0 ... 1.0
				addAutoWidget(filterMenu, parse_TexWordLayerFilter, "Spread", filter, true, tweOnDataChangedCallback, twDoc, PARAMS("Type Slider\n Min 0.0\n Max 1.0\n" DEFAULT_ALIGN), "The spread determines how soft or harsh the shadow is");
				addAutoWidget(filterMenu, parse_TexWordLayerFilter, "Spread", filter, false, tweOnDataChangedCallback, twDoc, PARAMS("NoLabel\n"), "The spread determines how soft or harsh the shadow is");
			}

			// Filter|Offset
			if (filter->type == TWFILTER_BLUR || filter->type == TWFILTER_DROPSHADOW) {
				// Text box for large values and slider -10...10
				addAutoWidget(filterMenu, parse_TexWordLayerFilter, "Offset", filter, true, tweOnDataChangedCallback, twDoc, &params_align, "Offset of the filtered version of the layer");
				addAutoWidget(filterMenu, parse_TexWordLayerFilter, "OffsetX", filter, true, tweOnDataChangedCallback, twDoc, PARAMS("Type Slider\n Min -10\n Max 10\n NoLabel\n" DEFAULT_ALIGN), "Offset of the filtered version of the layer");
				addAutoWidget(filterMenu, parse_TexWordLayerFilter, "OffsetY", filter, false, tweOnDataChangedCallback, twDoc, PARAMS("Type Slider\n Min -10\n Max 10\n NoLabel\n"), "Offset of the filtered version of the layer");
			}

			// Filter|Color
			if (filter->type == TWFILTER_DROPSHADOW) {
				addColorChoice(filterMenu, twDoc, filter->rgba, "Color", texWordsEdit_setFilterColorFunc, PAIR(filter, layer));
			}

			// Filter|BlendMode
			// Blend mode
			addAutoWidget(filterMenu, parse_TexWordLayerFilter, "Blend", filter, true, tweOnDataChangedCallback, twDoc, &params_align, "How the filtered version of the layer is blended with the original layer");

			bDidNewline=false;
			if (filterIndex != 0) {
				addButtonEx(filterMenu, IMAGE_ONLY "eui_arrow_large_up", texWordsEdit_filterMoveUp, PAIR(layer, filterIndex), bDidNewline=true, "Move this filter up");
			}
			if (filterIndex != eaSize(&layer->filter)-1) {
				addButtonEx(filterMenu, IMAGE_ONLY "eui_arrow_large_down", texWordsEdit_filterMoveDown, PAIR(layer, filterIndex), bDidNewline?false:(bDidNewline=true), "Move this filter down");
			}
			// On context menu now:
			//addButtonEx(filterMenu, "Remove Filter", texWordsEdit_layerFilterDelete, filter, bDidNewline?false:(bDidNewline=true), "Remove Filter");
			//addButtonEx(allFiltersMenu, "Insert New Filter", texWordsEdit_insertFilter, PAIR(layer, filterIndex+1), true, "Insert New Filter");
		}
	}

	// Sub-layers
	if (layer->sublayer && layer->sublayer[0]) {
		// Already have one
		addLayerToMenu(twDoc, layeritem, layer->sublayer[0], layer, 0, depth+1, allLayers, indexStringPrint);
	} else {
		addButtonEx(layeritem, "Insert New Sub-Layer", texWordsEdit_layerSubInsert, layer, false, "Insert New Sub-Layer");
	}

	// Move up/down
	if (relativeIndex != 0 && !parentLayerString) {
		addButtonEx(layeritem, IMAGE_ONLY "eui_arrow_large_up", texWordsEdit_layerMove, PAIR(layer, -1), false, "Move this layer up");
	}
	if (relativeIndex != eaSize(&allLayers)-1 && !parentLayerString) {
		addButtonEx(layeritem, IMAGE_ONLY "eui_arrow_large_down", texWordsEdit_layerMove, PAIR(layer, 1), false, "Move this layer down");
	}

	// Delete
	if (parentLayerString) {
		sprintf(buf, "Delete Sub-Layer \"%s\"", indexStringPrint);
		addButtonEx(layeritem, buf, texWordsEdit_layerDelete, layer, false, "Delete Sub-Layer");
	} else {
		// On context menu now:
		if (texWordsEdit_layersOnMenu)
		{
			addButtonEx(layeritem, "Duplicate Layer", texWordsEdit_layerClone, layer, false, "Insert a new duplicate copy of this layer");
			addButtonEx(layeritem, "Delete Layer", texWordsEdit_layerDelete, layer, false, "Delete Layer");
		}
	}

}

static void enableMenu(TWEditDoc *twDoc, const char *item_name, bool enable)
{
	UIMenuItem *item = ui_MenuListFindItem(texwords_editor.ui_menus, item_name);
	if (item)
		item->active = enable;
	else
		assertmsgf(0, "Could not find item named %s in the menu!", item_name);
}

static void tweSetupMenu(TWEditDoc *twDoc)
{
	// enable/disable various menu options
	enableMenu(twDoc, "File|Create TexWord", !twDoc->texWord);
	enableMenu(twDoc, "File|Save As New Dynamic...", !!twDoc->texWord);
	enableMenu(twDoc, "File|Remove TexWord", !!twDoc->texWord);

	enableMenu(twDoc, "Edit|Copy All Layers", !!twDoc->texWord);
	enableMenu(twDoc, "Edit|Paste All Layers", !!layerClipboard.layers);
}

static void tweSetupPopupMenus(TWEditDoc *twDoc)
{
	if (!twDoc->layerPopupMenu) {
		twDoc->layerPopupMenu = ui_MenuCreateWithItems("Layer Context Menu",
			ui_MenuItemCreate("New layer above this layer", UIMenuCallback, texWordsEdit_insertLayerRelative, (void*)(intptr_t)0, NULL),
			ui_MenuItemCreate("New layer below this layer", UIMenuCallback, texWordsEdit_insertLayerRelative, (void*)(intptr_t)1, NULL),
			ui_MenuItemCreate("Delete this layer", UIMenuCallback, texWordsEdit_layerDelete, NULL, NULL),
			ui_MenuItemCreate("Duplicate this layer", UIMenuCallback, texWordsEdit_layerClone, NULL, NULL),
			NULL);
		twDoc->filterPopupMenu = ui_MenuCreateWithItems("Filter Context Menu",
			ui_MenuItemCreate("New filter above this filter", UIMenuCallback, texWordsEdit_insertFilterRelative, (void*)(intptr_t)0, NULL),
			ui_MenuItemCreate("New filter below this filter", UIMenuCallback, texWordsEdit_insertFilterRelative, (void*)(intptr_t)1, NULL),
			ui_MenuItemCreate("Delete this filter", UIMenuCallback, texWordsEdit_layerFilterDelete, NULL, NULL),
			NULL);

	}
}

static void tweSetupUI(TWEditDoc *twDoc)
{
	UIWindow *window;
	F32 y, x;
	int i;
	char buf[1024];
	//const char *title = twDoc->texWord?"Edit TexWord":"Create TexWord";
	const char *title = "Edit TexWord";
	TexWord *texWord = twDoc->texWord;

	texWordsEdit_undoJournal(twDoc);

	eaClearEx(&twDoc->eaCallbackData, NULL);

	tweSetupMenu(twDoc);

	if (twDoc->mainWindow) {
		window = twDoc->mainWindow;
	} else {
		window = ui_WindowCreate(title, 400, 100, 300, 450);
		window->resizable = 1;
		ui_WindowSetClosable(window, false);
		twDoc->mainWindow = window;
		eaPush(&twDoc->base_doc.ui_windows, window);
	}

	y = 0;
	x = 0;

	if (twDoc->texWord) {
		sprintf(buf, "Editing %s", twDoc->texWord->filename);
	} else {
		strcpy(buf, "This texture has no TexWord defined for it.");
	}

	// Main menu

	ui_RebuildableTreeInit(twDoc->ui_tree, &UI_WIDGET(window)->children, x, y, UIRTOptions_YScroll);
	ui_RebuildableTreeAddLabel(twDoc->ui_tree->root, buf, NULL, true);
	{
		UIRTNode *group;
	 	group = twDoc->ui_tree->root; // addGroup(twDoc->ui_tree->root, "Edit", 1, "General editing commands (also available on the menu)");
		{
			if (!texWord) {
				addButton(group, "Create TexWord", texWordsEdit_create, "Create a new, empty TexWord for this texture");
			} else {
				//addButton(group, "Save [S]", texWordsEdit_fileSave);
				//sprintf(buf, USE_SMF "Undo (%d)" SMF_HOTKEY("[Z]"), twDoc->undoIndex?(twDoc->undoIndex - 1):0);
				//addButtonNewLine(group, buf, texWordsEdit_undoUndo, NULL);
				//sprintf(buf, USE_SMF "Redo (%d)" SMF_HOTKEY("[Y]"), twDoc->undoMax - twDoc->undoIndex);
				//addButton(group, buf, texWordsEdit_undoRedo, NULL);
				//addButtonNewLine(group, "Remove TexWord", texWordsEdit_fileRemoveTexWord);
			}
		}
		if (texWord) {
			group = addGroup(twDoc->ui_tree->root, "Layers", 1, "The layers used to composite the final texture");
			{
				if (!eaSize(&texWord->layers) || texWordsEdit_layersOnMenu)
					addButtonEx(group, "Insert Layer", texWordsEdit_insertLayer, (void*)0, true, "Insert New Layer");
				for (i=0; i<eaSize(&texWord->layers); i++) {
					addLayerToMenu(twDoc, group, texWord->layers[i], NULL, i, 0, texWord->layers, NULL);
				}
			}
		}
		group = addGroup(twDoc->ui_tree->root, "General Options", 0, NULL);
		{
			if (texWord)
				addAutoWidget(group, parse_TexWord, "Size", texWord, true, tweOnDataChangedCallback, twDoc, NULL, "The resolution of the final composited texture");
			twDoc->button_BGColor = addButtonNewLine(group, "Set BG Color", texWordsEdit_generalSetBG, "Set the background color to the currently selected color");
			vec4ToColor(&twDoc->button_BGColor->widget.color[0], twDoc->bgcolor);
			addButton(group, twDoc->stretch?USE_SMF "Preview: Stretched" SMF_HOTKEY("[Space]"):USE_SMF "Preview: Actual Size" SMF_HOTKEY("[Space]"),
				texWordsEdit_generalToggleStretch, "Toggle between previewing the actual size and a fully stretched version of the textre");
			addButton(group, twDoc->orig?USE_SMF "Preview: Original" SMF_HOTKEY("[Tab]"):USE_SMF "Preview: Translated" SMF_HOTKEY("[Tab]"),
				texWordsEdit_generalToggleOrig, "Allows you to easily preview the original image without any modifications");
			twDoc->button_colorPicker = addButtonNewLine(group, USE_SMF "ColorPicker" SMF_HOTKEY("[C]"), texWordsEdit_generalToggleColorPicker, "Used to choose a color which can be pasted into a layer");
			vec4ToColor(&twDoc->button_colorPicker->widget.color[0], twDoc->color);
		}
	}
	ui_RebuildableTreeDoneBuilding(twDoc->ui_tree);
	y += twDoc->ui_tree->root->h; // Lots of extra room

	//window->widget.height = MAX(window->widget.height, y);

	twDoc->needToRebuildUI = false;
	ui_WindowShow(window);
}


char *textureToBaseTexWord(const char *texturePath_in, char *dest, size_t dest_size)
{
	// texture_library/path/file#English.texture or
	// c:\game\data\texture_library/path/file#English.texture
	// to
	// C:/FightClub/data/texts/English/texture_library/path/file.TexWord
	char texturePath[CRYPTIC_MAX_PATH];
	char buf[CRYPTIC_MAX_PATH];
	char buf2[CRYPTIC_MAX_PATH];
	const char *path;
	char *ext;
	fileRelativePath(texturePath_in, texturePath);

	if (strStartsWith(texturePath, "texture_library")) {
		path = strchr(texturePath, '/');
		path++;
	} else {
		path = texturePath;
	}
	ext = strrchr(texturePath, '.');
	if (strrchr(texturePath, '#'))
		ext = strrchr(texturePath, '#');
	if (ext)
		*ext = '\0';
	sprintf(buf, "texture_library/%s.wtex", path);
	fileLocateWrite(buf, buf2);
	strstriReplace(buf2, "texture_library", "texts/English/texture_library");
	changeFileExt_s(buf2, ".TexWord", SAFESTR2(dest));
	return dest;
}

char *baseTexWordToTexture(const char *texwordPath_in, char *dest, size_t dest_size)
{
	// texts/English/texture_library/path/file.texword
	// or c:/game/data/texts/English/texture_library/path/file.texword
	// to
	// texture_library/path/file.texture // This file might not exist
	char buf[1024];
	char texwordPath[CRYPTIC_MAX_PATH];
	char *path, *ext;
	fileRelativePath(texwordPath_in, texwordPath);

	if (strStartsWith(texwordPath, "texts/")) {
		path = strchr(texwordPath, '/');
		path++;
	} else {
		path = texwordPath;
	}
	if (strStartsWith(path, "English/")) {
		path = strchr(path, '/');
		path++;
	} else {
		path = path;
	}
	if (strStartsWith(path, "texture_library/")) {
		path = strchr(path, '/');
		path++;
	} else {
		path = path;
	}
	sprintf(buf, "texture_library/%s", path);
	ext = strrchr(buf, '.');
	if (ext) {
		strcpy_s(ext, ARRAY_SIZE(buf) - (ext - buf), ".texture");
	} else {
		strcat(buf, ".texture");
	}
	strcpy_s(dest, dest_size, buf);
	return dest;
}



static EMEditorDoc* tweNewDoc(const char* name_in, const char* unused)
{
	char name[MAX_PATH]="";
	char filename[MAX_PATH];
	char messageStoreFilename[MAX_PATH];
	TWEditDoc* twDoc;

	// Cannot create new ones, you must edit an existing texture or TexWord
	if (!name_in)
		return NULL;

	getFileNameNoExt(name, name_in);

	// Setup data on new twDoc
	twDoc = calloc(1, sizeof(TWEditDoc));
	twDoc->base_doc.edit_undo_stack = EditUndoStackCreate();
	twDoc->ui_tree = ui_RebuildableTreeCreate();
	twDoc->colorPickerX = GamePrefGetInt("TWEColorPickerX", 10);
	twDoc->colorPickerY = GamePrefGetInt("TWEColorPickerY", 100);
	setVec4(twDoc->color, 1, 1, 1, 1);
	setVec4(twDoc->bgcolor, 0.5, 0.5, 0.5, 1);
	strcpy(twDoc->tweLocale, locGetName(getCurrentLocale()));
	if (texFind(name, 0)==NULL) {
		if (texWordFind(name, 1)) {
			// Dynamic
			TexWordParams *params = createTexWordParams();
			twDoc->bind = texFindDynamic(name, params, WL_FOR_UI, NULL);
		} else {
			// New Dynamic?
			// Not allowed
			Alertf("Trying to open a file for editing which is not recognized: %s", name);
			return NULL;
		}
	} else {
		twDoc->bind = texLoadBasic(name, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);
	}
	assert(twDoc->bind);
	twDoc->bind = twDoc->bind->actualTexture;
	twDoc->texWord = SAFE_MEMBER(texGetRareDataConst(twDoc->bind), texWord);

	if (!twDoc->texWord) {
		texFindFullName(twDoc->bind, SAFESTR(filename));
		emDocAssocFile(&twDoc->base_doc, textureToBaseTexWord(filename, SAFESTR(filename)));
	} else {
		strcpy(filename, twDoc->texWord->filename);
		emDocAssocFile(&twDoc->base_doc, twDoc->texWord->filename);
	}
	strcpy(twDoc->base_doc.doc_display_name, name);

	fileLocateWrite(filename, messageStoreFilename); // Need to get Core for Core, etc
	changeFileExt(messageStoreFilename, ".ms", messageStoreFilename);
	emDocAssocFile(&twDoc->base_doc, messageStoreFilename);

	// Create a new message store
	twDoc->messageStore = createMessageStore(1);
	initMessageStore(twDoc->messageStore, locGetIDByName(twDoc->tweLocale), NULL);
	msAddMessages(twDoc->messageStore, messageStoreFilename, NULL);

	// Setup the twDoc UI
	tweSetupPopupMenus(twDoc);
	tweSetupUI(twDoc);

	return &twDoc->base_doc;
}

static void tweCloseDoc(EMEditorDoc *doc_in)
{
	TWEditDoc* twDoc = (TWEditDoc*)doc_in;

	if (twDoc->extents.rect)
	{
		ui_WidgetQueueFree(UI_WIDGET(twDoc->extents.rect));
		twDoc->extents.rect = NULL;
	}

	// TODO: Destroy all memory allocated for the ui: tweDestroyUI

	// TODO: revert any changes
	eaDestroyEx(&twDoc->base_doc.ui_windows, ui_WindowFreeInternal);
	ui_RebuildableTreeDestroy(twDoc->ui_tree);
	free(twDoc);
}

static EMTaskStatus tweSaveDoc(EMEditorDoc *doc_in)
{
	TWEditDoc* twDoc = (TWEditDoc*)doc_in;
	bool ok = texWordsEdit_fileSave(NULL, twDoc);
	twDoc->base_doc.saved = 1;
	return ok?EM_TASK_SUCCEEDED:EM_TASK_FAILED;
}

static void rebuildTextures(TWEditDoc *twDoc)
{
	BasicTexture *bind;

	texWords_disableCache = true;

	if (texWordGetLoadsPending())
		return;

	gfxLockActiveDevice();
	// Restore old stuff
	bind = twDoc->bind;
	if (twDoc->orig) {
		char origName[256];
		twDoc->dummyBind.name = "dummy";

		strcpy(origName, getFileNameConst(bind->name));
		if (strchr(origName, '#')) {
			*strchr(origName, '#')=0;
		}
		twDoc->dummyBind.actualTexture = texFind(origName, 0);
		if ((!twDoc->dummyBind.actualTexture || stricmp(texFindFullPath(twDoc->dummyBind.actualTexture), "dynamicTexture")==0) && twDoc->texWord)
			twDoc->dummyBind.actualTexture = texWordGetBaseImage(twDoc->texWord, NULL, NULL);
		if (twDoc->dummyBind.actualTexture) {
			TexWord *old = SAFE_MEMBER(texGetRareDataConst(twDoc->dummyBind.actualTexture), texWord);
			texFree(twDoc->dummyBind.actualTexture, 0); // Free old one

			texOncePerFramePerDevice(); // Flush the free

			texAllocRareData(twDoc->dummyBind.actualTexture)->texWord = NULL;
			texLoadBasicInternal(twDoc->dummyBind.actualTexture, TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD, WL_FOR_UI, 0);
			texAllocRareData(twDoc->dummyBind.actualTexture)->texWord = old;

			twDoc->dummyBind.width = texGetOrigWidth(twDoc->dummyBind.actualTexture);
			twDoc->dummyBind.height = texGetOrigHeight(twDoc->dummyBind.actualTexture);
			twDoc->dummyBind.realWidth = pow2(twDoc->dummyBind.width);
			twDoc->dummyBind.realHeight = pow2(twDoc->dummyBind.height);
			bind = &twDoc->dummyBind;
		}
	} else {
		int i;
		texForceTexLoaderToComplete(1);
		texFree(bind, 0); // Free old one

		texOncePerFramePerDevice(); // Flush the free

		// Free raw data
		for (i=0; i<eaSize(&g_basicTextures); i++) {
			texFree(g_basicTextures[i], 1);
		}
	}

	// Force loading of file in case it needs to get regenerated
	//texLoadBasicInternal(bind, TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD, WL_FOR_UI, 0);
	twDoc->drawMeBind = bind;

	twDoc->needToRebuildTextures = 0;

	gfxUnlockActiveDevice();
}

static void tweUpdateStateFromCamera(TWEditDoc *twDoc)
{
	// Hack for mapping keybinds caught by the AssetManager on through
	if (texwords_editor.camera->zoom) {
		texwords_editor.camera->zoom = 0;
		texWordsEdit_generalToggleStretch(NULL, twDoc);
	}
	if (texwords_editor.camera->setcenter) {
		texwords_editor.camera->setcenter = 0;
		texWordsEdit_unhideAll(NULL, twDoc);
	}

}


static void tweDrawDoc(EMEditorDoc *doc_in)
{
	TWEditDoc* twDoc = (TWEditDoc*)doc_in;

	// Updating
	tweUpdateStateFromCamera(twDoc);
	if (twDoc->needToRebuildUI) {
		tweSetupUI(twDoc);
	}
	if (!twDoc->drawMeBind || twDoc->needToRebuildTextures) {
		rebuildTextures(twDoc);
		if (!twDoc->drawMeBind) // Perhaps because another texword load was pending we haven't been able to update this
			return;
		assert(twDoc->drawMeBind);
	}
	if (twDoc->colorPicker) {
		ui_ColorWindowGetColor(twDoc->colorPicker, twDoc->color);
	}
	twDoc->button_colorPicker->widget.pOverrideSkin = NULL;
	vec4ToColor(&(twDoc->button_colorPicker->widget.color[0]), twDoc->color);
	MAX1(twDoc->button_colorPicker->widget.color[0].r, 75);
	MAX1(twDoc->button_colorPicker->widget.color[0].g, 75);
	MAX1(twDoc->button_colorPicker->widget.color[0].b, 75);

	// Check extents having been lost
	texWordsEdit_extentsPatchup(twDoc);

	// Actual drawing
	texWordsEdit_updateScale(twDoc);
	if (twDoc->bind) {
		F32 wScale = twDoc->stretch?(twDoc->bind->width / twDoc->drawMeBind->width):1.f;
		F32 hScale = twDoc->stretch?(twDoc->bind->height / twDoc->drawMeBind->height):1.f;
		display_sprite_tex(twDoc->drawMeBind, twDoc->display.x0, twDoc->display.y0, 0, twDoc->display.xs * wScale, twDoc->display.ys * hScale, 0xFFFFFFFF);
		// Draw the extents
		//if (twDoc->extents.edit && texWordsEdit_layerExists(twDoc, twDoc->extents.editLayer))
		//{
		//	gfxDrawBox(x0 + twDoc->extents.x0 * xs - 0.5*xs, y0 + twDoc->extents.y0 * ys - 0.5*ys,
		//		x0 + twDoc->extents.x1 * xs - 0.5*xs, y0 + twDoc->extents.y1 * ys - 0.5*ys,
		//		1.f, colorFromRGBA(0x7FFF7FFF));
		//}
	}

	if (texwords_editor.camera) {
		texwords_editor.camera->override_bg_color = 1; 
		texwords_editor.camera->override_disable_3D = 1;
		copyVec4(twDoc->bgcolor, texwords_editor.camera->clear_color);
	}
}

static void tweGotFocus(EMEditorDoc *doc_in)
{
	TWEditDoc* twDoc = (TWEditDoc*)doc_in;
	tweSetupMenu(twDoc);
}

#define WRAPPED_NAME(name) texWordsEdit_##name##MenuCallback
#define WRAP_FUNCTION(name)	\
	void WRAPPED_NAME(name)(UIMenuItem *item_UNUSED, void *userData_UNUSED)	\
	{	\
		TWEditDoc *twDoc = (TWEditDoc*)emGetActiveEditorDoc();	\
		texWordsEdit_##name(NULL, twDoc);	\
	}

WRAP_FUNCTION(create)
WRAP_FUNCTION(fileSave)
WRAP_FUNCTION(fileSaveDynamic)
WRAP_FUNCTION(fileRemoveTexWord)
WRAP_FUNCTION(undoUndo)
WRAP_FUNCTION(undoRedo)
WRAP_FUNCTION(unhideAll)
WRAP_FUNCTION(layerCopyAll)
WRAP_FUNCTION(layerPasteAll)



static void tweInitEditor(EMEditor *editor)
{
	UIMenu *menu;
	// Setup all menu options
	menu = ui_MenuCreateWithItems("File",
		ui_MenuItemCreate("Create TexWord", UIMenuCallback, WRAPPED_NAME(create), NULL, NULL),
//		ui_MenuItemCreate("Save", UIMenuCallback, WRAPPED_NAME(fileSave), NULL, NULL),
		ui_MenuItemCreate("Save As New Dynamic...", UIMenuCallback, WRAPPED_NAME(fileSaveDynamic), NULL, NULL),
		ui_MenuItemCreate("Remove TexWord", UIMenuCallback, WRAPPED_NAME(fileRemoveTexWord), NULL, NULL),
		NULL);
	emMenuRegister(editor, menu);
	menu = ui_MenuCreateWithItems("Edit",
// 		ui_MenuItemCreate("Undo", UIMenuCallback, WRAPPED_NAME(undoUndo), NULL, NULL),
// 		ui_MenuItemCreate("Redo", UIMenuCallback, WRAPPED_NAME(undoRedo), NULL, NULL),
		ui_MenuItemCreate("Unhide All [J]", UIMenuCallback, WRAPPED_NAME(unhideAll), NULL, NULL),
		ui_MenuItemCreate("Copy All Layers", UIMenuCallback, WRAPPED_NAME(layerCopyAll), NULL, NULL),
		ui_MenuItemCreate("Paste All Layers", UIMenuCallback, WRAPPED_NAME(layerPasteAll), NULL, NULL),
		NULL);
	emMenuRegister(editor, menu);
}


/// Callback when the picker is selected
static bool twePickerSelected(EMPicker* picker, EMPickerSelection* selection)
{
	if (selection->table == parse_EMEasyPickerEntry) {
		strcpy(selection->doc_type, "TexWord");
		return true;
	} else {
		return false;
	}
}

void texWordsEdit_reloadCallback(void)
{
	int i;
	// Fix up all TWEDocs
	for (i=0; i<eaSize(&texwords_editor.open_docs); i++) {
		TWEditDoc *twDoc = (TWEditDoc*)texwords_editor.open_docs[i];
		if (twDoc->bind && texGetRareDataConst(twDoc->bind)) {
			twDoc->texWord = texGetRareDataConst(twDoc->bind)->texWord;
		}
		twDoc->needToRebuildTextures = twDoc->needToRebuildUI = 1;
	}
}

static Color twePickerColor( const char* path, bool isSelected )
{
	if( strEndsWith( path, ".TexWord" )) {
		return CreateColorRGB( 0, 80, 0 );
	} else {
		if( isSelected ) {
			return CreateColorRGB( 255, 255, 255 );
		} else {
			return CreateColorRGB( 0, 0, 0 );
		}
	}
}

#endif

AUTO_RUN_LATE;
int tweRegister(void)
{
#ifndef NO_EDITORS
	EMPicker *picker;
	
	if (!areEditorsAllowed())
		return 0;
	// Setup and register the mission editor
	strcpy(texwords_editor.editor_name, "TexWords Editor");
	texwords_editor.allow_multiple_docs = 1;
	texwords_editor.allow_save = 1;
	texwords_editor.hide_world = 1;
	texwords_editor.allow_outsource = 1;

	texwords_editor.camera_func = gfxNullCamFunc;

	texwords_editor.init_func = tweInitEditor;
	texwords_editor.load_func = tweNewDoc;
 	texwords_editor.close_func = tweCloseDoc;
 	texwords_editor.save_func = tweSaveDoc;
 	texwords_editor.draw_func = tweDrawDoc;
// 	texwords_editor.ghost_draw_func = tweDrawGhosts;
// 	texwords_editor.lost_focus_func = tweLostFocus;
 	texwords_editor.got_focus_func = tweGotFocus;
// 	texwords_editor.object_dropped_func = tweObjectSelected;

	texwords_editor.keybinds_name = "TexWordsEditor";
	texwords_editor.keybind_version = 1;

	emRegisterEditor(&texwords_editor);

	picker = emTexturePickerCreateForType( "TexWord" );
	emEasyPickerSetColorFunc( picker, twePickerColor );
	eaPush(&texwords_editor.pickers, picker);
	
	emRegisterFileTypeEx("TexWord", "TexWords Editor", "TexWords Editor", NULL);
#endif
	return 1;
}

void tweOncePerFrame(void)
{
	CHECK_EM_FUNC(tweNewDoc);
	CHECK_EM_FUNC(tweCloseDoc);
	CHECK_EM_FUNC(tweSaveDoc);
	CHECK_EM_FUNC(tweDrawDoc);
}

AUTO_RUN;
void tweCallbackInit(void)
{
#ifndef NO_EDITORS
	texWordsSetReloadCallback(texWordsEdit_reloadCallback);
#endif
}
