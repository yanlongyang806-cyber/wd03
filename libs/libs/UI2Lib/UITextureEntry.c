#include "inputMouse.h"

#include "GfxClipper.h"
#include "GfxTexAtlas.h"

#include "UITextureEntry.h"
#include "UISprite.h"
#include "EArray.h"
#include "EString.h"
#include "EditorManager.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static const char** textureExts = NULL;
static const char** textureDirs = NULL;

static void setInitialSelections(UITextureEntry *pTexEntry, const char*** peaNewSelections)
{
	int i;

	if (pTexEntry->eaInitialSelections)
	{
		eaClearEx(&pTexEntry->eaInitialSelections, NULL);
		pTexEntry->eaInitialSelections = NULL;
	}

	if (peaNewSelections)
	{
		for (i = 0; i < eaSize(peaNewSelections); i++)
			eaPush(&pTexEntry->eaInitialSelections, strdup((*peaNewSelections)[i]));
	}
}
static void TexturePickerSelected(const char *pchDir, const char *pchFile, UITextureEntry *pTexEntry)
{
	ui_TextEntrySetTextAndCallback(pTexEntry->pEntry, pchFile);
	ui_WidgetQueueFree(UI_WIDGET(pTexEntry->pFileBrowser));
	pTexEntry->pFileBrowser = NULL;

	if (pTexEntry->cbFinished)
		pTexEntry->cbFinished(pTexEntry, pTexEntry->pFinishedData);
}

static void TexturePickerMultipleSelected(const char **pchDir, const char **pchFile, UITextureEntry *pTexEntry)
{
	char* estrFilenames = NULL;
	int i = 0;
	estrCreate(&estrFilenames);
	for (i = 0; i < eaSize(&pchFile); i++)
		estrConcatf(&estrFilenames, "%s ", pchFile[i]);
	ui_TextEntrySetTextAndCallback(pTexEntry->pEntry, estrFilenames);
	ui_WidgetQueueFree(UI_WIDGET(pTexEntry->pFileBrowser));
	setInitialSelections(pTexEntry, &pchFile);
	pTexEntry->pFileBrowser = NULL;

	if (pTexEntry->cbFinished)
		pTexEntry->cbFinished(pTexEntry, pTexEntry->pFinishedData);

	estrDestroy(&estrFilenames);
}

static void TexturePickerCancel(UITextureEntry *pTexEntry)
{
	ui_WidgetQueueFree(UI_WIDGET(pTexEntry->pFileBrowser));
	pTexEntry->pFileBrowser = NULL;
}

static void TextureNameFilter( char* outBuffer, int outBuffer_size, const char* fileName, void* ignored )
{
    char* extBegin;
    
    strcpy_s( SAFESTR2( outBuffer ), fileName );

    extBegin = strrchr( outBuffer, '.' );
    if( extBegin ) {
        *extBegin = '\0';
    }
}

static bool TexturePickerFinishedCB(EMPicker *picker, EMPickerSelection **selections, UITextureEntry *pTexEntry)
{
	char pcFileName[MAX_PATH];

	if (eaSize(&selections) != 1)
		return false;

	getFileNameNoExt(pcFileName, selections[0]->doc_name);
	ui_TextEntrySetTextAndCallback(pTexEntry->pEntry, pcFileName);

	if (pTexEntry->cbFinished)
		pTexEntry->cbFinished(pTexEntry, pTexEntry->pFinishedData);

	return true;
}

static void OpenTexturePicker(UIButton *pButton, UITextureEntry *pTexEntry)
{
	char *pcStartDir = pTexEntry->pcFileStartDir;
	EMPicker* texturePicker = emPickerGetByName( "Texture Picker" );

    if( !textureExts ) {
        eaCreate( &textureExts );
        eaPush( &textureExts, ".wtex" );
        eaPush( &textureExts, ".TexWord" );
    }

    if( !textureDirs ) {
        eaCreate( &textureDirs );
        eaPush( &textureDirs, "texture_library" );
        eaPush( &textureDirs, "texts\\English\\texture_library" );
    }

	if (pTexEntry->cbCustomChooseFn) {
		pTexEntry->cbCustomChooseFn(pTexEntry, pTexEntry->pCustomChooseData);
	} else if (texturePicker) {
		emPickerShow( texturePicker, "Select", false, TexturePickerFinishedCB, pTexEntry );
	} else {
		if (!pTexEntry->pFileBrowser)
			pTexEntry->pFileBrowser = ui_FileBrowserCreateEx(
					"Select a Texture",
					"Select", pTexEntry->bMultiTexture ? UIBrowseMultipleExisting : UIBrowseExisting, UIBrowseTextureFiles, false, textureDirs,
					pcStartDir ? pcStartDir : textureDirs[ 0 ], NULL, textureExts, TexturePickerCancel,
					pTexEntry, TexturePickerSelected, pTexEntry,
					TextureNameFilter, NULL, TexturePickerMultipleSelected, false);
		if (pTexEntry->bMultiTexture)
			ui_FileBrowserSetSelectedList(pTexEntry->eaInitialSelections);
		ui_WindowShow(pTexEntry->pFileBrowser);
	}
}

static void UpdateSpriteAndCallback(UITextEntry *pEntry, UITextureEntry *pTexEntry)
{
	ui_SpriteSetTexture(pTexEntry->pSprite, ui_TextEntryGetText(pEntry));
	if (pTexEntry->cbChanged)
		pTexEntry->cbChanged(pTexEntry, pTexEntry->pChangedData);
}

static void UpdateSpriteAndCallbackFinished(UITextEntry *pEntry, UITextureEntry *pTexEntry)
{
	ui_SpriteSetTexture(pTexEntry->pSprite, ui_TextEntryGetText(pEntry));
	if (pTexEntry->cbFinished)
		pTexEntry->cbFinished(pTexEntry, pTexEntry->pFinishedData);
}

static void ui_TextureEntryContextProxy(UITextEntry *pEntry, UITextureEntry *pTexEntry)
{
	if (pTexEntry->widget.contextF)
		pTexEntry->widget.contextF(pTexEntry, pTexEntry->widget.contextData);	
}

UITextureEntry *ui_TextureEntryCreate(const char *pchTexture, const char*** eaInitialSelections, bool bMultipleTextures)
{
	UITextureEntry *pTexEntry = calloc(1, sizeof(UITextureEntry));
	F32 fHeight = 0;
	ui_WidgetInitialize(UI_WIDGET(pTexEntry), ui_TextureEntryTick, ui_TextureEntryDraw, ui_TextureEntryFreeInternal, NULL, NULL);
	pTexEntry->pEntry = ui_TextEntryCreate(pchTexture, 0, 0);
	pTexEntry->pButton = ui_ButtonCreate("...", 0, 0, OpenTexturePicker, pTexEntry);
	pTexEntry->pSprite = ui_SpriteCreate(0, 0, 0, 0, pchTexture);
	pTexEntry->pSprite->bDrawBorder = true;
	pTexEntry->bMultiTexture = bMultipleTextures;

	ui_TextEntrySetChangedCallback(pTexEntry->pEntry, UpdateSpriteAndCallback, pTexEntry);
	ui_TextEntrySetFinishedCallback(pTexEntry->pEntry, UpdateSpriteAndCallbackFinished, pTexEntry);
	ui_WidgetSetContextCallback(UI_WIDGET(pTexEntry->pEntry), ui_TextureEntryContextProxy, pTexEntry);

	ui_WidgetSetPositionEx(UI_WIDGET(pTexEntry->pSprite), 0, 0, 0, 0, UITopLeft);
	ui_WidgetSetPositionEx(UI_WIDGET(pTexEntry->pButton), 0, 0, 0, 0, UITopRight);
	ui_WidgetSetDimensions(UI_WIDGET(pTexEntry), 192, 64);
	MAX1(fHeight, UI_WIDGET(pTexEntry)->height);
	MAX1(fHeight, UI_WIDGET(pTexEntry->pButton)->height);
	ui_WidgetSetHeight(UI_WIDGET(pTexEntry), fHeight);
	ui_WidgetSetHeightEx(UI_WIDGET(pTexEntry->pButton), 1.f, UIUnitPercentage);
	ui_WidgetSetHeightEx(UI_WIDGET(pTexEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetHeightEx(UI_WIDGET(pTexEntry->pSprite), 1.f, UIUnitPercentage);
	ui_WidgetSetWidth(UI_WIDGET(pTexEntry->pSprite), fHeight);
	ui_WidgetSetWidthEx(UI_WIDGET(pTexEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pTexEntry->pEntry), UI_WIDGET(pTexEntry->pSprite)->width, UI_WIDGET(pTexEntry->pButton)->width, 0, 0);
	ui_WidgetAddChild(UI_WIDGET(pTexEntry), UI_WIDGET(pTexEntry->pEntry));
	ui_WidgetAddChild(UI_WIDGET(pTexEntry), UI_WIDGET(pTexEntry->pSprite));
	ui_WidgetAddChild(UI_WIDGET(pTexEntry), UI_WIDGET(pTexEntry->pButton));
	setInitialSelections(pTexEntry, eaInitialSelections);
	return pTexEntry;
}

void ui_TextureEntryFreeInternal(UITextureEntry *pTexEntry)
{
	setInitialSelections(pTexEntry, NULL);
	ui_WidgetFreeInternal(UI_WIDGET(pTexEntry));
}

void ui_TextureEntryTick(UITextureEntry *pTexEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pTexEntry);
	UI_TICK_EARLY(pTexEntry, true, true);
	UI_TICK_LATE(pTexEntry);
}

void ui_TextureEntryDraw(UITextureEntry *pTexEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pTexEntry);
	UI_DRAW_EARLY(pTexEntry);

	// Make sure changed/inherited flags move into text entry
	ui_SetChanged(UI_WIDGET(pTexEntry->pEntry), ui_IsChanged(UI_WIDGET(pTexEntry)));
	ui_SetInherited(UI_WIDGET(pTexEntry->pEntry), ui_IsInherited(UI_WIDGET(pTexEntry)));

	// This is here to ensure the child widgets respond to size changes to this widget
	ui_WidgetSetWidth(UI_WIDGET(pTexEntry->pSprite), UI_WIDGET(pTexEntry)->height);
	ui_WidgetSetPaddingEx(UI_WIDGET(pTexEntry->pEntry), UI_WIDGET(pTexEntry->pSprite)->width, UI_WIDGET(pTexEntry->pButton)->width, 0, 0);

	UI_DRAW_LATE(pTexEntry);
}

void ui_TextureEntrySetFileStartDir(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, const char *pcFileStartDir)
{
	if (pTexEntry->pcFileStartDir)
		free(pTexEntry->pcFileStartDir);
	if (pcFileStartDir)
		pTexEntry->pcFileStartDir = strdup(pcFileStartDir);
}

const char *ui_TextureEntryGetTextureName(UITextureEntry *pTexEntry)
{
	return ui_TextEntryGetText(pTexEntry->pEntry);
}

AtlasTex *ui_TextureEntryGetTexture(UITextureEntry *pTexEntry)
{
	return atlasLoadTexture(ui_TextureEntryGetTextureName(pTexEntry));
}

void ui_TextureEntrySetTextureName(UITextureEntry *pTexEntry, const char *pchTextureName)
{
	ui_TextEntrySetText(pTexEntry->pEntry, pchTextureName);
	ui_SpriteSetTexture(pTexEntry->pSprite, pchTextureName);
}

void ui_TextureEntrySetTexture(UITextureEntry *pTexEntry, AtlasTex *pTexture)
{
	ui_TextureEntrySetTextureName(pTexEntry, pTexture->name);
}

void ui_TextureEntrySetTextureNameAndCallback(UITextureEntry *pTexEntry, const char *pchTextureName)
{
	ui_TextEntrySetTextAndCallback(pTexEntry->pEntry, pchTextureName);
}

void ui_TextureEntrySetTextureAndCallback(UITextureEntry *pTexEntry, AtlasTex *pTexture)
{
	ui_TextEntrySetTextAndCallback(pTexEntry->pEntry, pTexture->name);
}

void ui_TextureEntrySetSelectOnFocus(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, bool select)
{
	ui_TextEntrySetSelectOnFocus(pTexEntry->pEntry, select);
}

void ui_TextureEntrySetChangedCallback(UITextureEntry *pTexEntry, UIActivationFunc cbChanged, UserData pChangedData)
{
	pTexEntry->cbChanged = cbChanged;
	pTexEntry->pChangedData = pChangedData;
}

void ui_TextureEntrySetFinishedCallback(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, UIActivationFunc cbFinished, UserData pFinishedData)
{
	pTexEntry->cbFinished = cbFinished;
	pTexEntry->pFinishedData = pFinishedData;
}

void ui_TextureEntrySetCustomChooseCallback(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, UIActivationFunc cbCustomChoose, UserData pCustomChooseData)
{
	pTexEntry->cbCustomChooseFn = cbCustomChoose;
	pTexEntry->pCustomChooseData = pCustomChooseData;
}
