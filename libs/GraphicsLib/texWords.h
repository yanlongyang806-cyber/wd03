#ifndef TEXWORDS_H
#define TEXWORDS_H
GCC_SYSTEM

#include "GfxTextureEnums.h"
#include "WorldLibEnums.h"

typedef struct TexWord TexWord;
typedef struct TexWordParams TexWordParams;

TexWord *texWordFind(const char *texName, int search); // Use search=1 if this is for a dynamic texture that wants to search the Base locale for a layout file as well
BasicTexture *texWordGetBaseImage(SA_PARAM_NN_VALID TexWord *texWord, SA_PRE_OP_FREE SA_POST_OP_VALID int *width, SA_PRE_OP_FREE SA_POST_OP_VALID int *height);
void texWordsCheckReload(void);
//void texWordLoadDeps(TexWord *texWord, TexLoadHow mode, WLUsageFlags use_category, BasicTexture *texBindParent);
void texWordSendToRenderer(TexWord *texWord, BasicTexture *texBindParent);
void texWordDoneLoading(TexWord *texWord, BasicTexture *texBindParent);

void texWordLoad(TexWord *texWord, TexLoadHow mode, WLUsageFlags use_category, BasicTexture *texBindParent);
void texWordsCheck(void);

TexWordParams *createTexWordParams(void);
void destroyTexWordParams(TexWordParams *params);

int texWordGetPixelsPerSecond(void);
int texWordGetTotalPixels(void);
int texWordGetLoadsPending(void);

void texWordsFlush(void); // Asynchronously cause the background renderer to finish quickly

void texWordsStartup(void);

typedef void (*voidVoidFunc)(void);
void texWordsSetReloadCallback(voidVoidFunc callback);


#endif