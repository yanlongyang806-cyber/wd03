#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORPRIVATE_H__
#define __WORLDEDITORPRIVATE_H__

#ifndef NO_EDITORS

typedef struct EMEditor EMEditor;
typedef struct EMPicker EMPicker;
typedef struct EMMapLayerType EMMapLayerType;
typedef struct RoomPartition RoomPartition;

extern EMEditor worldEditor;
extern EMPicker skyPicker;
extern EMMapLayerType **wleMapLayers;

/********************
* DEBUGGING
********************/
void wleDebugDraw(void);
void wleDebugCacheRoomData(RoomPartition *partition);

#endif // NO_EDITORS

#endif // __WORLDEDITORPRIVATE_H__