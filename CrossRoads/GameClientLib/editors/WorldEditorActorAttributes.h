#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORACTORATTRIBUTES_H__
#define __WORLDEDITORACTORATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAEActorReload(EMPanel *panel, EditorObject *edObj);
void wleAEActorCreate(EMPanel *panel);

#endif // __WORLDEDITORACTORATTRIBUTES_H__

#endif // NO_EDITORS