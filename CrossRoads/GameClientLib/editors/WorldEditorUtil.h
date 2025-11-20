#ifndef __WORLDEDITORUTIL_H__
#define __WORLDEDITORUTIL_H__
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct TrackerHandle TrackerHandle;
typedef struct GroupTracker GroupTracker;
typedef struct GroupDef GroupDef;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct WorldScope WorldScope;
typedef struct LogicalGroup LogicalGroup;
typedef struct WorldLogicalGroup WorldLogicalGroup;

bool wleLayerLocked(SA_PARAM_NN_VALID ZoneMapLayer *layer, bool verbose, bool prompt);
bool wleTrackerIsEditable(SA_PARAM_OP_VALID const TrackerHandle *handle, bool verbose, bool prompt, bool modifying_children);

bool wleTrackerIsDescendant(SA_PARAM_NN_VALID const TrackerHandle *child, SA_PARAM_NN_VALID const TrackerHandle *parent);
SA_RET_OP_VALID TrackerHandle *wleTrackerGetLayer(SA_PARAM_NN_VALID const TrackerHandle *handle);
SA_RET_OP_VALID TrackerHandle *wleTrackersGetLayer(const TrackerHandle **handles);
SA_RET_OP_VALID TrackerHandle *wleFindCommonParent(const TrackerHandle **handles);
int wleFindCommonParentIndex(SA_PARAM_NN_VALID TrackerHandle *parent, TrackerHandle **handles);
void wleFindAllTrackers(SA_PARAM_OP_VALID GroupTracker *root, SA_PARAM_NN_VALID GroupDef *def, TrackerHandle ***results);
void wleTrackerGetAverageMat(const TrackerHandle **handles, SA_PARAM_NN_VALID Mat4 outMat);

SA_RET_OP_STR const char *wleTrackerHandleToUniqueName(SA_PARAM_OP_VALID const TrackerHandle *scopeHandle, SA_PARAM_NN_VALID const TrackerHandle *handle);
SA_RET_OP_VALID LogicalGroup *wleFindLogicalGroup(SA_PARAM_NN_VALID GroupDef *scopeDef, SA_PARAM_NN_VALID WorldScope *scope, SA_PARAM_OP_VALID WorldLogicalGroup *group);

bool wleIsUniqueNameSpecified(SA_PARAM_OP_STR const char *uniqueName);
bool wleIsUniqueNameable(SA_PARAM_OP_VALID TrackerHandle *scopeHandle, SA_PARAM_NN_VALID TrackerHandle *handle, SA_PARAM_OP_STR const char *groupName);

#endif // NO_EDITORS

#endif // __WORLDEDITORUTIL_H__