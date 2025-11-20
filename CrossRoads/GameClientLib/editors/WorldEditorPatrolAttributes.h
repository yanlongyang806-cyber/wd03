#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORPATROLATTRIBUTES_H__
#define __WORLDEDITORPATROLATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAEPatrolReload(EMPanel *panel, EditorObject *edObj);

#endif // __WORLDEDITORPATROLATTRIBUTES_H__

#endif // NO_EDITORS