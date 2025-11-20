#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORTERRAINPROP_H__
#define __WORLDEDITORTERRAINPROP_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAETerrainPropReload(EMPanel *panel, EditorObject *edObj);
void wleAETerrainPropAdd(UserData *pUnused, UserData *pUnused2);
void wleAETerrainPropRemove(UserData *pUnused, UserData *pUnused2);

#endif // NO_EDITORS

#endif // __WORLDEDITORTERRAINPROP_H__