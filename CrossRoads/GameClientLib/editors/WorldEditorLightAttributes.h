#pragma once
GCC_SYSTEM
#ifndef NO_EDITORS

#ifndef _WORLDEDITORLIGHTATTRIBUTES_H_
#define _WORLDEDITORLIGHTATTRIBUTES_H_

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAELightReload(EMPanel *panel, EditorObject *edObj);

#endif // _WORLDEDITORLIGHTATTRIBUTES_H_

#endif // NO_EDITORS