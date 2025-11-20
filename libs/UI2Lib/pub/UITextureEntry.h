#ifndef UI_TEXTURE_ENTRY_H
#define UI_TEXTURE_ENTRY_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UISprite UISprite;
typedef struct UITextEntry UITextEntry;
typedef struct UIButton UIButton;
typedef struct UIWindow UIWindow;
typedef struct EMPicker EMPicker;

typedef struct UITextureEntry
{
	UIWidget widget;

	UITextEntry *pEntry;
	UIButton *pButton;
	UISprite *pSprite;

	char *pcFileStartDir;

	UIActivationFunc cbChanged;
	UserData pChangedData;
	UIActivationFunc cbFinished;
	UserData pFinishedData;
	
	UIWindow *pFileBrowser;
	UIActivationFunc cbCustomChooseFn;
	UserData pCustomChooseData;

	bool bMultiTexture;
	char** eaInitialSelections;

} UITextureEntry;

SA_RET_NN_VALID UITextureEntry *ui_TextureEntryCreate(const char *pchTexture, const char*** eaInitialSelections, bool bMultiple);
void ui_TextureEntryFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UITextureEntry *pTexEntry);
void ui_TextureEntryTick(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, UI_PARENT_ARGS);
void ui_TextureEntryDraw(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, UI_PARENT_ARGS);

void ui_TextureEntrySetFileStartDir(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, const char *pcFileStartDir);

const char *ui_TextureEntryGetTextureName(SA_PARAM_NN_VALID UITextureEntry *pTexEntry);
AtlasTex *ui_TextureEntryGetTexture(SA_PARAM_NN_VALID UITextureEntry *pTexEntry);

void ui_TextureEntrySetTextureName(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, SA_PARAM_NN_STR const char *pchTextureName);
void ui_TextureEntrySetTexture(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, SA_PARAM_NN_VALID AtlasTex *pTexture);
void ui_TextureEntrySetTextureNameAndCallback(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, SA_PARAM_NN_STR const char *pchTextureName);
void ui_TextureEntrySetTextureAndCallback(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, SA_PARAM_NN_VALID AtlasTex *pTexture);
void ui_TextureEntrySetSelectOnFocus(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, bool select);

void ui_TextureEntrySetChangedCallback(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, UIActivationFunc cbChanged, UserData pChangedData);
void ui_TextureEntrySetFinishedCallback(SA_PARAM_NN_VALID UITextureEntry *pTextEntry, UIActivationFunc cbFinished, UserData pFinishedData);
void ui_TextureEntrySetCustomChooseCallback(SA_PARAM_NN_VALID UITextureEntry *pTexEntry, UIActivationFunc cbCustomChoose, UserData pCustomChooseData);

#endif
