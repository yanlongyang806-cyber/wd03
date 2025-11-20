#pragma once
GCC_SYSTEM
#ifndef NO_EDITORS

#ifndef _WORLDEDITORMENUS_H
#define _WORLDEDITORMENUS_H

typedef struct BasicTexture BasicTexture;
typedef struct EditorObject EditorObject;
typedef struct EdObjCustomMenu EdObjCustomMenu;
typedef struct UIMenu UIMenu;
typedef struct UIMenuItem UIMenuItem;
typedef struct GroupTracker GroupTracker;
typedef struct Model Model;
typedef struct Material Material;

typedef enum EditorMenuContext
{
	EDITCONTEXT_NONE = 1,
	EDITCONTEXT_TRACKER,
	EDITCONTEXT_TERRAIN,
} EditorMenuContext;

// Global externs
extern EdObjCustomMenu *wleMenuRightClick;

// Menu setup functions
void wleMenuInitMenus(void);
void wleTrackerContextMenuCreateForModel(Model *model, UIMenuItem ***outItems);
void wleTrackerContextMenuCreateForMaterial(const Material *material, UIMenuItem ***outItems);
void wleTrackerContextMenuCreateForTexture(BasicTexture *texture, UIMenuItem ***outItems);
void wleTrackerContextMenuCreate(EditorObject *edObjs, UIMenuItem ***outItems);

void wleMenuSetDefaultParentTracker(UIMenuItem *item, GroupTracker *tracker);

#endif // _WORLDEDITORMENUS_H

#endif // NO_EDITORS