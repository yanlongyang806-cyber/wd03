#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORENCONTERHACKATTRIBUTES_H__
#define __WORLDEDITORENCONTERHACKATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAEEncounterHackReload(EMPanel *panel, EditorObject *edObj);
void wleAEEncounterHackCreate(EMPanel *panel);

#endif // __WORLDEDITORENCONTERHACKATTRIBUTES_H__

#endif // NO_EDITORS