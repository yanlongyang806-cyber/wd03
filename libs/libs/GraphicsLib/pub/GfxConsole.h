#pragma once
GCC_SYSTEM


#include "stdtypes.h"

C_DECLARATIONS_BEGIN

typedef struct MRUList MRUList;

typedef void (*ConPrintCallback)(char *s);

// called on conPrint
void conSetPrintCallback(ConPrintCallback cb);
ConPrintCallback conGetPrintCallback(void);

void conCreate(void);
void conPrint(char *s);
void conPrintf(FORMAT_STR char const *fmt, ...);
#define conPrintf(fmt, ...) conPrintf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void conPrintfUpdate(FORMAT_STR char const *fmt, ...);
#define conPrintfUpdate(fmt, ...) conPrintfUpdate(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
MRUList *conGetMRUList(void);

void gfxConsoleRender(void);
void gfxConsoleProcessInput(void);
int gfxConsoleVisible(void);
void gfxConsoleEnable(int enable); // mis-named, this actually just shows it, if it is allowed
void gfxConsoleAllow(int allow);


C_DECLARATIONS_END