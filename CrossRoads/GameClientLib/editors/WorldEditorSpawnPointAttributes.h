#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORSPAWNPOINTATTRIBUTES_H__
#define __WORLDEDITORSPAWNPOINTATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAESpawnPointReload(EMPanel *panel, EditorObject *edObj);

#endif // __WORLDEDITORSPAWNPOINTATTRIBUTES_H__

#endif // NO_EDITORS