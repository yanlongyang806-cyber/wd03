#pragma once
GCC_SYSTEM

typedef struct StringListStruct StringListStruct;

void aiAnimListDebugSetList(StringListStruct *list);
void gclAIDebugOncePerFrame(void);
void gclAIDebugGameplayLeave(void);