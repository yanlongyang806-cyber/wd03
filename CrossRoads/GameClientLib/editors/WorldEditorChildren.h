#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORCHILDREN_H__
#define __WORLDEDITORCHILDREN_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

void wleAEChildrenReload(EditorObject *edObj);
void wleAEChildrenCreate(EMPanel *panel);

#endif // NO_EDITORS

#endif // __WORLDEDITORCHILDREN_H__