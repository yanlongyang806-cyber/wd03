#pragma once
GCC_SYSTEM

typedef struct TexWordParams {
	char **parameters; // EArray of strings to replace "TexWordParam1", "TexWordParam2", etc
} TexWordParams;

// Dynamic textures (from TexWords)
BasicTexture *texFindDynamic(const char *layoutName, TexWordParams *params, WLUsageFlags use_category, const char *blameFileName);
bool texUnloadDynamic( BasicTexture *basicBind );
TexWordParams *createTexWordParams(void);
void destroyTexWordParams(TexWordParams *params);
