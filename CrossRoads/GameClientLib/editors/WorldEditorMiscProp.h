#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORMISCPROP_H__
#define __WORLDEDITORMISCPROP_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

#include "WorldEditorAttributesHelpers.h"

//Physical panel
int wleAEPhysicalPropReload(EMPanel *panel, EditorObject *edObj);

//LOD panel
int wleAELODPropReload(EMPanel *panel, EditorObject *edObj);
void wleAELODPropAdd(void *unused, void *unused2);
void wleAELODPropRemove(void *unused, void *unused2);

//UGC panel
int wleAEUGCPropReload(EMPanel *panel, EditorObject *edObj);

//System panel
int wleAESystemPropReload(EMPanel *panel, EditorObject *edObj);

// PathNode panel
int wleAEPathNodeReload(EMPanel *panel, EditorObject *edObj);


#include "autogen/WorldEditorAttributesHelpers_h_ast.h"

#endif // NO_EDITORS

#endif // __WORLDEDITORMISCPROP_H__