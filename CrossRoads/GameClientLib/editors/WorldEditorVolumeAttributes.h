#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORVOLUMEATTRIBUTES_H__
#define __WORLDEDITORVOLUMEATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;
typedef struct GroupDef GroupDef;

int wleAEVolumeReload(EMPanel *panel, EditorObject *edObj);
void wleAEVolumeCreate(EMPanel *panel);
void wleAEVolumeAdd(void *unused, void *unused2);
void wleAEVolumeRemove(void *unused, void *unused2);

#endif // NO_EDITORS

#endif // __WORLDEDITORVOLUMEATTRIBUTES_H__