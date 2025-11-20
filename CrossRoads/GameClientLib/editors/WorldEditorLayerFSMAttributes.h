#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORLAYERFSMATTRIBUTES_H__
#define __WORLDEDITORLAYERFSMATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAELayerFSMReload(EMPanel *panel, EditorObject *edObj);
void wleAELayerFSMCreate(EMPanel *panel);

#endif // __WORLDEDITORLAYERFSMATTRIBUTES_H__

#endif // NO_EDITORS
