#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORTRIGGERCONDITIONATTRIBUTES_H__
#define __WORLDEDITORTRIGGERCONDITIONATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAETriggerConditionReload(EMPanel *panel, EditorObject *edObj);

#endif // __WORLDEDITORTRIGGERCONDITIONATTRIBUTES_H__

#endif // NO_EDITORS
