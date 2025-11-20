#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITOR_AUTOPLACEATTRIBUTES_H__
#define __WORLDEDITOR_AUTOPLACEATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAEAutoPlacementReload(EMPanel *panel, EditorObject *edObj);
void wleAEAutoPlacementCreate(EMPanel *panel);

#endif // NO_EDITORS

#endif // __WORLDEDITORVOLUMEATTRIBUTES_H__