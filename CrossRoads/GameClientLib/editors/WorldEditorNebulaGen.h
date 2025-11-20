#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORNEBULAGEN_H__
#define __WORLDEDITORNEBULAGEN_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAENebulaGenReload(EditorObject *edObj);
void wleAENebulaGenCreate(EMPanel *panel);

#endif // NO_EDITORS

#endif // __WORLDEDITORNEBULAGEN_H__