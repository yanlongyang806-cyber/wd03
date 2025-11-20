/***************************************************************************



***************************************************************************/

#pragma once

#ifndef _GDEBUG_H
#define _GDEBUG_H
GCC_SYSTEM

// The goal here is to divorce the debug system from the actual systems as much as feasible
// The reasoning is that a broken system shouldn't affect the debugging and vice versa

#include "stdtypes.h"
#include "ReferenceSystem.h"

typedef struct DebuggerObject DebuggerObject;
typedef struct Debugger Debugger;
typedef int DebuggerType;
typedef int DebuggerFlag;
typedef struct DebuggerObject DebuggerObject;

typedef struct UIScrollArea UIScrollArea;
typedef struct UIPane UIPane;

typedef enum DebuggerObjectMsgType {
	DOMSG_GETNAME,
	DOMSG_DRAWSELF,
} DebuggerObjectMsgType;

typedef struct DebuggerObjectMsg {
	DebuggerObjectMsgType type;
	DebuggerObject *obj;
	void *obj_data;
	union {
		struct {
			struct {
				char *name;
				int len;
			} out;
		} getName;
	};
} DebuggerObjectMsg;

typedef struct DebuggerObjectStruct {
	REF_TO(DebuggerObject) object;
} DebuggerObjectStruct;

typedef void (*GDebuggerInitFunc)(void);
typedef void (*GDebuggerTickFunc)(void);
typedef void (*GDebuggerSettingPaneTickFunc)(void);
typedef void (*GDebuggerDrawRootFunc)(DebuggerType type, void *data, void *persist);
typedef void (*GDebuggerDrawObjFunc)(DebuggerType type, void *data, void *persist);
typedef void (*GDebuggerFlagChangedCallback)(ReferenceHandle *handle, DebuggerType type, void *obj, void *persist, F32 value, U32 inherited);
typedef void (*GDebuggerSettingsAreaFunc)(UIScrollArea *area);
typedef void (*GDebuggerUserPaneFunc)(ReferenceHandle *handle, DebuggerType type, void *data, void *persist, UIPane *pane);
typedef void (*GDebuggerInfoAreaFunc)(DebuggerType type, UIScrollArea *area, void *data, void *persist, int initial);
typedef void (*GDebuggerTypeMsgHandler)(DebuggerObjectMsg *msg);

void gDebugOncePerFrame(void);

void gDebugUIToggle(void);
void gDebugUpdateUI(void);
void gDebugDraw(void);

Debugger* gDebugRegisterDebugger(const char *name);
DebuggerType gDebugRegisterDebugObjectType(const char *name, GDebuggerTypeMsgHandler msghandler);
DebuggerType gDebugRegisterDebugObjectFlag(const char *name, U32 trickle_up, U32 trickle_down);
DebuggerFlag gDebugRegisterDebugObjectValueFlag(const char *name, U32 trickle_up, U32 trickle_down, F32 default_value, U32 min_based, U32 max_based);

void gDebuggerFlagSetChangedCallback(DebuggerFlag flag, GDebuggerFlagChangedCallback func);

void gDebuggerSetInitCallback(Debugger *dbger, GDebuggerInitFunc func);
void gDebuggerSetTickCallback(Debugger *dbger, GDebuggerTickFunc func);
void gDebuggerSetPaneCallbacks(Debugger *dbger, GDebuggerSettingsAreaFunc settings_func, GDebuggerInfoAreaFunc info_func);
void gDebuggerSetSettingsTickCallback(Debugger *dbger, GDebuggerSettingPaneTickFunc settings_tick_func);
void gDebuggerSetUserPaneCallback(Debugger *dbger, GDebuggerUserPaneFunc userpane_func);

#define gDebuggerAddRoot(dbger, root_name, draw, obj) gDebuggerAddRootByHandle((dbger), (root_name), (draw), &(obj).__handle_INTERNAL)
void gDebuggerAddRootByHandle(Debugger *dbger, const char* root_name, GDebuggerDrawRootFunc draw, ReferenceHandle *handle);

#define gDebuggerRootAddType(root, type) gDebuggerRootAddTypeByHandle(&(root).__handle_INTERNAL, (type))
void gDebuggerRootAddTypeByHandle(ReferenceHandle *root, DebuggerType type);

#define gDebuggerRootSetDrawFunc(root, draw_func) gDebuggerRootSetDrawFuncByHandle(&(root).__handle_INTERNAL, draw_func)
void gDebuggerRootSetDrawFuncByHandle(ReferenceHandle *root, GDebuggerDrawRootFunc draw_func);

void gDebuggerTypeSetDrawFunc(DebuggerType type, GDebuggerDrawObjFunc draw);
void gDebuggerTypeCreateGroup(DebuggerType type);
void gDebuggerTypeAddToGroup(DebuggerType type1, DebuggerType type2);

#define gDebuggerObjectIs(object) gDebuggerObjectIsByHandle(&(object).__handle_INTERNAL)
U32 gDebuggerObjectIsByHandle(ReferenceHandle *object);

#define gDebuggerObjectAddObject(parent, type, obj, child) gDebuggerObjectAddObjectByHandle(&(parent).__handle_INTERNAL, (type), (obj), &(child).__handle_INTERNAL)
void gDebuggerObjectAddObjectByHandle(ReferenceHandle *parent, DebuggerType type, void *obj, ReferenceHandle *child);

#define gDebuggerObjectAddVirtualObject(parent, type, name, child) gDebuggerObjectAddVirtualObjectByHandle(&(parent).__handle_INTERNAL, (type), (name), &(child).__handle_INTERNAL)
void gDebuggerObjectAddVirtualObjectByHandle(ReferenceHandle *parent, DebuggerType type, const char *name, ReferenceHandle *child);

#define gDebuggerObjectRemoveChildByContents(parent, child) gDebuggerObjectRemoveChildByContentsByHandle(&(parent).__handle_INTERNAL, (child))
void gDebuggerObjectRemoveChildByContentsByHandle(ReferenceHandle *parent, void *child);

#define gDebuggerObjectRemove(object) gDebuggerObjectRemoveByHandle(&(object).__handle_INTERNAL)
void gDebuggerObjectRemoveByHandle(ReferenceHandle *object);

#define gDebuggerObjectRemoveChildren(object) gDebuggerObjectRemoveChildrenByHandle(&(object).__handle_INTERNAL)
void gDebuggerObjectRemoveChildrenByHandle(ReferenceHandle *object);

#define gDebuggerObjectAddChild(parent, child) gDebuggerObjectAddChildByHandle(&(parent).__handle_INTERNAL, &(child).__handle_INTERNAL)
void gDebuggerObjectAddChildByHandle(ReferenceHandle *parent, ReferenceHandle *child);

#define gDebuggerObjectRemoveChild(parent, child) gDebuggerObjectRemoveChildByHandle(&(parent).__handle_INTERNAL, &(child).__handle_INTERNAL)
void gDebuggerObjectRemoveChildByHandle(ReferenceHandle *parent, ReferenceHandle *child);

// This allows some data to persist after the object is destroyed
#define gDebuggerObjectUnlink(object, saved_obj, pt) gDebuggerObjectUnlinkByHandle(&(object).__handle_INTERNAL, saved_obj, pt)
void gDebuggerObjectUnlinkByHandle(ReferenceHandle *object, void *saved_obj, ParseTable *pt);  

#define gDebuggerObjectGetType(object) gDebuggerObjectGetTypeByHandle(&(object).__handle_INTERNAL)
DebuggerType gDebuggerObjectGetTypeByHandle(ReferenceHandle *object);

#define gDebuggerObjectGetData(object) gDebuggerObjectGetDataByHandle(&(object).__handle_INTERNAL)
void* gDebuggerObjectGetDataByHandle(ReferenceHandle *object);

#define gDebuggerObjectGetPersistData(object) gDebuggerObjectGetPersistDataByHandle(&(object).__handle_INTERNAL)
void* gDebuggerObjectGetPersistDataByHandle(ReferenceHandle *object);

#define gDebuggerObjectGetUserPane(object) gDebuggerObjectGetUserPaneByHandle(&(object).__handle_INTERNAL)
UIPane* gDebuggerObjectGetUserPaneByHandle(ReferenceHandle *object);

#define gDebuggerObjectHasFlag(object, flag) gDebuggerObjectHasFlagByHandle(&(object).__handle_INTERNAL, (flag))
U32 gDebuggerObjectHasFlagByHandle(ReferenceHandle *object, DebuggerFlag flag);

#define gDebuggerObjectGetFlagValue(object, flag) gDebuggerObjectGetFlagValueByHandle(&(object).__handle_INTERNAL, (flag))
F32 gDebuggerObjectGetFlagValueByHandle(ReferenceHandle *object, DebuggerFlag flag);

#define gDebuggerObjectRemoveFlag(object, flag) gDebuggerObjectRemoveFlagByHandle(&(object).__handle_INTERNAL, (flag))
void gDebuggerObjectRemoveFlagByHandle(ReferenceHandle *object, DebuggerFlag flag);

#define gDebuggerObjectAddFlag(object, flag) gDebuggerObjectAddFlagByHandle(&(object).__handle_INTERNAL, (flag), 1)
#define gDebuggerObjectAddFlagCount(object, flag, count) gDebuggerObjectAddFlagByHandle(&(object).__handle_INTERNAL, (flag), (count))
void gDebuggerObjectAddFlagByHandle(ReferenceHandle *object, DebuggerFlag flag, int count);

#define gDebuggerObjectSetFlag(object, flag, value) gDebuggerObjectSetFlagByHandle(&(object).__handle_INTERNAL, (flag), (value))
void gDebuggerObjectSetFlagByHandle(ReferenceHandle *object, DebuggerFlag flag, F32 value);

#define gDebuggerObjectResetFlag(object, flag, over) gDebuggerObjectResetFlagByHandle(&(object).__handle_INTERNAL, (flag), (over))
void gDebuggerObjectResetFlagByHandle(ReferenceHandle *object, DebuggerFlag flag, U32 override_all);

// This means the object will have to get a timestamp from a parent to determine children and state
//void gDebuggerObjectSetTimeStamped(DebuggerObject *object, U32 timestamped);

#endif