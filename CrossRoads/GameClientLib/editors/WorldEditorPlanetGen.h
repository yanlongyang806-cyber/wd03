#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORPLANETGEN_H__
#define __WORLDEDITORPLANETGEN_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAEPlanetGenReload(EMPanel *panel, EditorObject *edObj);

#endif // NO_EDITORS

#endif // _WORLDEDITORPLANETGEN_H_