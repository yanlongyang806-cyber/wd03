#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORLOGICALGROUPATTRIBUTES_H__
#define __WORLDEDITORLOGICALGROUPATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAELogicalGroupReload(EMPanel *panel, EditorObject *edObj);
void wleAELogicalGroupAddProps(UserData *pUnused, UserData *pUnused2);
void wleAELogicalGroupRemoveProps(UserData *pUnused, UserData *pUnused2);

#endif // __WORLDEDITORLOGICALGROUPATTRIBUTES_H__

#endif // NO_EDITORS