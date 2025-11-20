#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORAMBIENTJOB_H__
#define __WORLDEDITORAMBIENTJOB_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAEInteractLocationReload(EMPanel *panel, EditorObject *edObj);

#endif // __WORLDEDITORAMBIENTJOB_H__

#endif // NO_EDITORS