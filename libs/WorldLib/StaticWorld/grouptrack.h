#ifndef _GROUPTRACK_H
#define _GROUPTRACK_H
GCC_SYSTEM

typedef struct GroupTracker GroupTracker;
typedef struct GroupDef GroupDef;
typedef struct TrackerHandle TrackerHandle;
typedef struct Packet Packet;
typedef struct WorldCell WorldCell;
typedef struct GroupSplineParams GroupSplineParams;
typedef struct ZoneMapLayer ZoneMapLayer;

//////////////////////////////////////////////////////////////////////////
// trackers

SA_RET_NN_VALID GroupTracker *trackerAlloc(void);
void trackerFree(SA_PRE_OP_VALID SA_POST_P_FREE GroupTracker *tracker);

void trackerInit(SA_PARAM_OP_VALID GroupTracker *tracker);
void trackerOpenEx(SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_OP_VALID const Mat4 world_mat, F32 scale, bool child_entries, bool temp_group, bool in_headshot);
#define trackerOpen(tracker) trackerOpenEx(tracker, NULL, 1, true, false, false)
void trackerClose(SA_PARAM_OP_VALID GroupTracker *tracker);
void trackerUpdate(SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_OP_VALID GroupDef *def, bool force);

void trackerGetMatEx(SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_NN_VALID Mat4 world_mat, SA_PRE_OP_FREE SA_POST_OP_VALID F32 *scale);
#define trackerGetMat(tracker, world_mat) trackerGetMatEx(tracker, world_mat, NULL)
void trackerGetBounds(SA_PARAM_NN_VALID GroupTracker *tracker, SA_PARAM_OP_VALID const Mat4 tracker_world_matrix, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 world_min, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 world_max, SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 world_mid, SA_PARAM_OP_VALID F32 *radius, SA_PARAM_NN_VALID Mat4 world_mat, SA_PARAM_OP_VALID GroupSplineParams *spline);
U32 trackerGetSeed(SA_PARAM_OP_VALID GroupTracker *tracker);
void trackerGetRelativeMat(SA_PARAM_OP_VALID GroupTracker *tracker, Mat4 world_mat, GroupTracker *src);
void trackerGetRelativeTint(SA_PARAM_OP_VALID GroupTracker *tracker, Vec3 tint, GroupTracker *src);

GroupDef **trackerGetDefChain(SA_PARAM_OP_VALID GroupTracker *tracker);

void trackerUpdateWireframeRecursive(SA_PARAM_OP_VALID GroupTracker *tracker);
void trackerSetSelected(SA_PARAM_OP_VALID GroupTracker *tracker, bool selected);
void trackerSetInvisible(SA_PARAM_OP_VALID GroupTracker *tracker, bool invisible);
void trackerStopOnFXChanged(GroupTracker *tracker);
void trackerNotifyOnFXChanged(void *widget, void *block, GroupTracker *tracker);

//////////////////////////////////////////////////////////////////////////
// tracker handles

SA_RET_OP_VALID TrackerHandle *trackerHandleCreate(SA_PARAM_OP_VALID GroupTracker *tracker);
SA_RET_OP_VALID TrackerHandle *trackerHandleCreateFromDefChain(GroupDef **def_chain, SA_PARAM_OP_VALID int *idxs_in_parent);
SA_RET_OP_VALID TrackerHandle *trackerHandleCopy(SA_PARAM_OP_VALID const TrackerHandle *handle);
void trackerHandleDestroy(SA_PRE_OP_VALID SA_POST_P_FREE TrackerHandle *handle);

void trackerHandlePushUID(SA_PARAM_OP_VALID TrackerHandle *handle, U32 uid);
U32 trackerHandlePopUID(SA_PARAM_OP_VALID TrackerHandle *handle);

SA_RET_OP_VALID GroupTracker *trackerFromTrackerHandle(SA_PARAM_OP_VALID const TrackerHandle *handle);
SA_RET_OP_VALID GroupDef *groupDefFromTrackerHandle(SA_PARAM_OP_VALID const TrackerHandle *handle);
SA_RET_OP_VALID TrackerHandle *trackerHandleFromTracker(SA_PARAM_OP_VALID GroupTracker *tracker);
SA_RET_OP_VALID TrackerHandle *trackerHandleFromDefChain(GroupDef **def_chain, SA_PARAM_OP_VALID int *idxs_in_parent);

int trackerHandleComp(const TrackerHandle *a,const TrackerHandle *b);

SA_RET_OP_VALID GroupTracker *layerTrackerFromTrackerHandle(SA_PARAM_OP_VALID TrackerHandle *handle);

void pktSendTrackerHandle(Packet *pak, const TrackerHandle *handle);
SA_RET_OP_VALID TrackerHandle *pktGetTrackerHandle(Packet *pak);

SA_RET_OP_STR char *stringFromTrackerHandle(SA_PARAM_OP_VALID const TrackerHandle *handle);
SA_RET_OP_VALID TrackerHandle *trackerHandleFromString(SA_PARAM_OP_STR const char *str);

SA_RET_OP_STR char *handleStringFromTracker(SA_PARAM_OP_VALID GroupTracker *tracker);
SA_RET_OP_STR char *handleStringFromDefChain(GroupDef **def_chain, SA_PARAM_OP_VALID int *idxs_in_parent);
SA_RET_OP_VALID GroupTracker *trackerFromHandleString(SA_PARAM_OP_STR const char *str);

void trackerIdxListFromTrackerHandle(SA_PARAM_OP_VALID TrackerHandle *handle, int **idx_list);

SA_RET_OP_VALID GroupTracker *trackerGetScopeTracker(SA_PARAM_OP_VALID GroupTracker *tracker);
SA_RET_OP_STR const char *trackerGetUniqueScopeName(SA_PARAM_OP_VALID GroupDef *scope_def, SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_OP_STR const char *group_name);
SA_RET_OP_STR const char *trackerGetUniqueZoneMapScopeName(SA_PARAM_OP_VALID GroupTracker *tracker);
SA_RET_OP_VALID GroupTracker *trackerFromUniqueName(SA_PARAM_NN_VALID GroupTracker *scope_tracker, SA_PARAM_NN_STR const char *name);


//////////////////////////////////////////////////////////////////////////
// efficient tracker traversal

typedef struct TrackerTreeTraverserDrawParams
{
	// Traversal data
	GroupTracker				*tracker;
	Mat4						world_mat;
	Vec3						tint_color0;
	U32							seed;
	bool						editor_visible_only;
} TrackerTreeTraverserDrawParams;


// traverse the group tree, calling callback on every node.  if callback returns true it will traverse the node's children.
// return false from the callback to not traverse the group's children.
typedef int (*TrackerTreeTraverserCallback)(void *user_data, TrackerTreeTraverserDrawParams *draw);
void trackerTreeTraverse(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_OP_VALID GroupTracker *tracker, TrackerTreeTraverserCallback callback, void *user_data);

#endif
