#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORDEBRISFIELDGEN_H__
#define __WORLDEDITORDEBRISFIELDGEN_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAEDebrisFieldGenReload(EMPanel *panel, EditorObject *edObj);

#endif // NO_EDITORS

#endif // __WORLDEDITORDEBRISFIELDGEN_H__