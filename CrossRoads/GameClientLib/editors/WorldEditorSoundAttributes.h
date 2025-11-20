#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORSOUNDATTRIBUTES_H__
#define __WORLDEDITORSOUNDATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAESoundReload(EMPanel *panel, EditorObject *edObj);

#endif // __WORLDEDITORSOUNDATTRIBUTES_H__

#endif // NO_EDITORS