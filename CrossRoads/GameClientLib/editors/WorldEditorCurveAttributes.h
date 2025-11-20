#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORCURVEATTRIBUTES_H__
#define __WORLDEDITORCURVEATTRIBUTES_H__

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;

int wleAECurveReload(EMPanel *panel, EditorObject *edObj);

#endif // NO_EDITORS

#endif // __WORLDEDITORCURVEATTRIBUTES_H__