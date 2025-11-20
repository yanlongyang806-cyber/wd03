#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORWIND_H__
#define __WORLDEDITORWIND_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAEWindReload(EMPanel *panel, EditorObject *edObj);

int wleAEWindSourceReload(EMPanel *panel, EditorObject *edObj);

#endif // NO_EDITORS

#endif // __WORLDEDITORWIND_H__