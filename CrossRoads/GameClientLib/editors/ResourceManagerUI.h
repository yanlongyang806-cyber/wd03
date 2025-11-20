#pragma once
GCC_SYSTEM

#include "ResourceInfo.h"

typedef struct ResourceAction ResourceAction;
typedef struct ResourceActionList ResourceActionList;
typedef struct UIWindow UIWindow;
typedef struct AtlasTex AtlasTex;
typedef struct BasicTexture BasicTexture;
typedef struct ResourceInfo ResourceInfo;
typedef struct UIComboBox UIComboBox;

typedef void resGetBasicTexture(const ResourceInfo *pInfo, const void* pData, const void* pExtraData, F32 size, BasicTexture** outTex, Color* outModColor);
typedef void resGetAtlasTex(const ResourceInfo *pInfo, const void* pData, const void* pExtraData, F32 size, AtlasTex** outTex, Color* outModColor);
typedef void resClearTextures(void);


// Register callback for a certain type
// You must define one of the two callbacks, but not both
void resRegisterPreviewCallback(DictionaryHandleOrName dictHandle, resGetBasicTexture *basicCB, resGetAtlasTex *atlasCB, resClearTextures *clearCB);

// Call this to free any associated state with the preview.
void resFreePreviews(void);

// Actually handle drawing the specified resource
// return false if there is no available preview
bool resDrawPreview(ResourceInfo *pInfo, const void* pExtraData, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 z, unsigned char alpha);
bool resDrawResource(const char* dictName, void* resourceData, const void* pExtraData, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 z, unsigned char alpha);

// Default callback to get AtlasTex off the info
AtlasTex *resGetAtlasTexFromResourceInfo(const ResourceInfo *pInfo);

// Creates a UIComboBox with the model corresponding to a list of all resource dictionaries
// The model consists of ResourceDictionaryInfos, but some of them are categories, with a NULL dictionary
// The model will be sorted by category
UIComboBox *resCreateDictionaryComboBox(F32 x, F32 y, F32 w);

// Select a specific dictionary
void resDictionaryComboSelectDictionary(UIComboBox *pCombo, const char *pDictName, bool bRunCallback);

// Spawns a new checkin window, with a copy of the passed in resource action holder
void DisplayCheckinWindow(ResourceActionList *pHolder);

