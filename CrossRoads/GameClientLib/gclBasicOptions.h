#ifndef BASIC_OPTIONS_H_
#define BASIC_OPTIONS_H_

GCC_SYSTEM

void gclBasicOptions_UpdateCameraOptions(void);

LATELINK;
void gameSpecific_gclBasicOptions_Init(const char* pchCategory);

void gclBasicOptions_InitPlayerOptions(Entity* pEnt);

#endif //BASIC_OPTIONS_H_