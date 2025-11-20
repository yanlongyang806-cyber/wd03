#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORNAMEATTRIBUTES_H__
#define __WORLDEDITORNAMEATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAENameReload(EMPanel *panel, EditorObject *edObj);

#endif // __WORLDEDITORNAMEATTRIBUTES_H__

#endif // NO_EDITORS