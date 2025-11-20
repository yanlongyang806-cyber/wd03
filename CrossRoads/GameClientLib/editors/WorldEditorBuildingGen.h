#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORBUILDINGGEN_H__
#define __WORLDEDITORBUILDINGGEN_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAEBuildingGenReload(EMPanel *panel, EditorObject *edObj);

#endif // NO_EDITORS

#endif // __WORLDEDITORBUILDINGGEN_H__