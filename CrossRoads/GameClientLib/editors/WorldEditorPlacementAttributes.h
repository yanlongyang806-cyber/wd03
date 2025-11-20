#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORPLACEMENTATTRIBUTES_H__
#define __WORLDEDITORPLACEMENTATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EMPanel EMPanel;
typedef struct EditorObject EditorObject;

void wleAEPlacementPosRotUpdate(Mat4 refMat);
int wleAEPlacementReload(EMPanel *panel, EditorObject *edObj);
void wleAEPlacementCreate(EMPanel *panel);

#endif // NO_EDITORS

#endif // __WORLDEDITORPLACEMENTATTRIBUTES_H__