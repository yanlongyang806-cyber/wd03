#ifndef NO_EDITORS

#include "WorldEditorUtil.h"
#include "WorldGrid.h"
#include "wlEncounter.h"
#include "WorldEditorUI.h"
#include "EditorManager.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/******
* This function determines whether a particular layer is locked.
* PARAMS:
*   layer - ZoneMapLayer to check for lock
*   verbose - bool indicating whether to print a status message telling the user the layer is locked
*   prompt - bool indicating whether to pop up a lock prompt if the user does not have the layer locked
* RETURNS:
*   bool indicating whether the layer is locked
******/
bool wleLayerLocked(ZoneMapLayer *layer, bool verbose, bool prompt)
{
	if (!layerGetLocked(layer))
	{
		if (verbose)
		{
			GroupDef *layerDef = layerGetDef(layer);
			emStatusPrintf("Please lock layer \"%s\" before modifying it.", layerDef ? layerDef->name_str : "UNKNOWN");
		}
		if (prompt)
			wleUILockPrompt(layerGetFilename(layer));
		return false;
	}
	return true;
}

/******
* This function determines whether one tracker is a descendant (inclusive) of another.
* PARAMS:
*   child - TrackerHandle to check whether descended from parent
*   parent - TrackerHandle to check whether parent of child
* RETURNS:
*   bool indicating whether child is a descendant of parent (inclusively)
******/
bool wleTrackerIsDescendant(SA_PARAM_NN_VALID const TrackerHandle *child, SA_PARAM_NN_VALID const TrackerHandle *parent)
{
	GroupTracker *childTracker = trackerFromTrackerHandle(child);
	GroupTracker *parentTracker = trackerFromTrackerHandle(parent);

	// invalid trackers can never be considered descendants
	if (!childTracker || !parentTracker)
		return false;

	do
	{
		if (childTracker == parentTracker)
			return true;
		childTracker = childTracker->parent;
	} while (childTracker);

	return false;
}

/******
* This function determines whether the user is allowed to modify the tracker at the specified handle
* according to the locks the user possesses.  If verbose is set to true, the user will be notified
* that the he does not have a lock and TODO: prompted to lock the appropriate file.
* PARAMS:
*   handle - TrackerHandle to the tracker to check for editability
*   verbose - boolean indicating whether to report to the user that he does not possess the appropriate
*             lock
*   prompt - boolean indicating whether to prompt the user to lock the tracker's file
* RETURNS:
*   bool indicating whether the user is allowed to modify the tracker given by handle
******/
bool wleTrackerIsEditable(const TrackerHandle *handle, bool verbose, bool prompt, bool modifying_children)
{
	GroupTracker *tracker, *parent;
	ZoneMapLayer *layer;

	if (!handle)
		return false;
	if (!(tracker = trackerFromTrackerHandle(handle)))
		return false;
	if (!tracker->def)
		return false;
	if (tracker->def->name_uid == -1)
		return false;

	parent = modifying_children ? tracker : tracker->parent;
	while (parent)
	{
		if (parent->def)
		{
			if( parent->def->property_structs.building_properties)
			{
				if (verbose || prompt)
					emStatusPrintf("Cannot modify children of dynamically generated groups.");
				return false;
			}
		}
		parent = parent->parent;
	}

	// determine whether the user has a lock on the file/layer corresponding to the tracker
	layer = tracker->def->def_lib->zmap_layer;
	if (layer)
		return !layerIsGenesis(layer) && wleLayerLocked(layer, verbose, prompt);
	else
	{
		if (!groupIsEditable(tracker->def))
		{
			if (verbose)
				emStatusPrintf("Please lock group file \"%s\" before modifying it.", tracker->def->filename);
			if (prompt)
			{
				char write_path[MAX_PATH];
				objectLibraryGetWritePath(tracker->def->filename, SAFESTR(write_path));
				wleUILockPrompt(write_path);
			}
			return groupIsEditable(tracker->def);
		}
		else
			return true;
	}
}

/******
* This function returns a TrackerHandle to the layer to which a specified tracker belongs.
* PARAMS:
*   handle - TrackerHandle of tracker whose layer is to be returned
* RETURNS:
*   TrackerHandle to layer where specified handle belongs
******/
TrackerHandle *wleTrackerGetLayer(const TrackerHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	ZoneMapLayer *layer;
	
	assert(tracker && tracker->def);
	layer = tracker->parent_layer;
	if (!layer)
		return NULL;

	return trackerHandleCreate(layerGetTracker(layer));
}

/******
* This function takes a list of trackers and determines whether they are all in the same
* layer.
* PARAMS:
*   handles - TrackerHandle EArray of trackers to check
* RETURNS:
*   TrackerHandle indicating the layer to which all handles belong (or NULL if there's no common
*   layer or if no trackers were specified)
******/
TrackerHandle *wleTrackersGetLayer(const TrackerHandle **handles)
{
	int i;
	ZoneMapLayer *layer = NULL;

	for (i = 0; i < eaSize(&handles); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handles[i]);
		assert(tracker && tracker->def);
		if (i == 0)
			layer = tracker->parent_layer;
		else if (tracker->parent_layer != layer)
			return NULL;
	}	
	return trackerHandleCreate(layerGetTracker(layer));
}

/******
* This function takes an array of tracker handles and returns a TrackerHandle to the "lowest"
* tracker that is an ancestor of all of the trackers referenced in the array.  This function
* assumes that the same TrackerHandle is not passed in multiple times and that the passed-in handles
* are all members of the active layer (this is validated).
* PARAMS:
*   handles - TrackerHandle EArray from which to find the common parent
* RETURNS:
*   TrackerHandle of the "lowest" common parent to the handles; NULL if that parent is "above" the
*   layer level
******/
TrackerHandle *wleFindCommonParent(const TrackerHandle **handles)
{
	TrackerHandle *parentHandle;
	int arrayLength = eaSize(&handles);

	// find the most common parent among the grouped trackers
	if (arrayLength == 0)
		parentHandle = NULL;
	else if (arrayLength == 1)
	{
		if (eaiSize(&handles[0]->uids) == 0)
			parentHandle = NULL;
		else
		{
			parentHandle = trackerHandleCopy(handles[0]);
			trackerHandlePopUID(parentHandle);
		}
	}
	else
	{
		int i;
		int count = 0;
		int endLoop = 0;

		parentHandle = wleTrackersGetLayer(handles);
		if (!parentHandle)
			return NULL;

		// loop through the handle indexes from the top down until one of the tracker handle indexes
		// does not match the other handles' index at the same depth
		do
		{
			U32 commonVal = 0;
			for (i = 0; !endLoop && i < arrayLength; i++)
			{
				if (count >= eaiSize(&handles[i]->uids))
					endLoop = 1;
				else if (i == 0)
					commonVal = handles[i]->uids[count];
				else if (handles[i]->uids[count] != commonVal)
					endLoop = 1;
			}
			if (!endLoop)
			{
				trackerHandlePushUID(parentHandle, handles[0]->uids[count]);
				count++;
			}
		} while (!endLoop);
	}
	return parentHandle;
}

/******
* This function should generally be used in conjunction with the above function.  It takes a parent
* tracker and a list child trackers that should all be descendants of the parent, and it determines
* the index of the first tracker in the parent that is an ancestor of at least one of the children.
* PARAMS:
*   parent - TrackerHandle common parent
*   handles - TrackerHandle EArray of children of the parent
* RETURNS:
*   int index of the first child tracker of parent that is an ancestor of one of the specified children;
*   -1 if the input is erroneous (i.e. if all children are not descendants of parent)
******/
int wleFindCommonParentIndex(TrackerHandle *parent, TrackerHandle **handles)
{
	GroupTracker *parentTracker = trackerFromTrackerHandle(parent);
	int i, ret = -1;

	if (!parentTracker)
		return -1;

	for (i = 0; i < eaSize(&handles); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handles[i]);

		while (tracker->parent && tracker->parent != parentTracker)
			tracker = tracker->parent;
			
		if (!tracker->parent)
			return -1;

		if (ret == -1)
			ret = tracker->idx_in_parent;
		else
			ret = MIN(ret, tracker->idx_in_parent);
	}

	return ret;
}

/******
* This function searches through a root tracker and finds all trackers with a definition that
* matches a specified one.  If the root is passed as NULL, then the function searches through
* all trackers on the map.  The results of the search are pushed onto a results EArray, also passed
* as a parameter.
* PARAMS:
*   root - GroupTracker where to start the search
*   def - GroupDef to search for
*   results - TrackerHandle EArray pointing to GroupTrackers that have GroupDef's that match with def
******/
void wleFindAllTrackers(GroupTracker *root, GroupDef *def, TrackerHandle ***results)
{
	int i;
	if (!root)
	{
		for (i = 0; i < zmapGetLayerCount(NULL); i++)
		{
			GroupTracker *layerRoot = zmapGetLayerTracker(NULL, i);
			if (layerRoot)
				wleFindAllTrackers(layerRoot, def, results);
		}
	}
	else if (root->def == def)
		eaPush(results, trackerHandleCreate(root));
	else
	{
		for (i = 0; i < root->child_count; i++)
			wleFindAllTrackers(root->children[i], def, results);
	}
}

/******
* This function finds the "average" matrix for a group of TrackerHandles.  The average is defined
* as follows:
*   -for a single tracker, the average is the tracker's matrix
*   -for multiple trackers, the average is a unit rotation with a position that is an average of the
*    trackers' positions
*   -for no trackers, the average is the unit matrix
* PARAMS:
*   handles - TrackerHandle EArray to average
*   outMat - Mat4 where the average is stored
******/
void wleTrackerGetAverageMat(const TrackerHandle **handles, Mat4 outMat)
{
	copyMat4(unitmat, outMat);
	if (eaSize(&handles) == 1)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handles[0]);
		if (tracker)
			trackerGetMat(tracker, outMat);
	}
	else
	{
		int i, count = 0;

		// recalculate the average matrix of the selection
		for (i = 0; i < eaSize(&handles); i++)
		{
			Mat4 tempMat;
			GroupTracker *tracker = trackerFromTrackerHandle(handles[i]);
			if (tracker)
			{
				trackerGetMat(tracker, tempMat);
				addVec3(outMat[3], tempMat[3], outMat[3]);
				count++;
			}
		}
		if (count)
			scaleVec3(outMat[3], 1.0f / count, outMat[3]);
	}	
}

/******
* This function converts a tracker handle into its unique name within a particular scope.
* PARAMS:
*   scopeHandle - TrackerHandle to the scope in which to find a unique name
*   handle - TrackerHandle of the tracker whose unique name is to be determined
* RETURNS:
*   string unique name of tracker pointed to by handle within the scope pointed at by scopeHandle
******/
const char *wleTrackerHandleToUniqueName(const TrackerHandle *scopeHandle, const TrackerHandle *handle)
{
	GroupTracker *scopeTracker = trackerFromTrackerHandle(scopeHandle);
	GroupTracker *tracker = trackerFromTrackerHandle(handle);

	return trackerGetUniqueScopeName(scopeTracker ? scopeTracker->def : NULL, tracker, NULL);
}

/******
* This function returns the LogicalGroup from which a WorldLogicalGroup was created.
* PARAMS:
*   scopeDef - GroupDef in which to search for the LogicalGroup
*   scope - WorldScope created from scopeDef
*   group - WorldLogicalGroup to look for
* RETURNS:
*   LogicalGroup from which WorldLogicalGroup was created
******/
LogicalGroup *wleFindLogicalGroup(GroupDef *scopeDef, WorldScope *scope, WorldLogicalGroup *group)
{
	char *groupName;

	if (!group)
		return NULL;
	if (stashFindPointer(scope->obj_to_name, group, &groupName))
	{
		int i;
		for (i = 0; i < eaSize(&scopeDef->logical_groups); i++)
		{
			if (strcmpi(scopeDef->logical_groups[i]->group_name, groupName) == 0)
				return scopeDef->logical_groups[i];
		}
	}
	return NULL;
}

bool wleIsUniqueNameSpecified(const char *uniqueName)
{
	if (!uniqueName || strncmp(GROUP_UNNAMED_PREFIX, uniqueName, strlen(GROUP_UNNAMED_PREFIX)) == 0)
		return false;
	return true;
}

bool wleIsUniqueNameable(TrackerHandle *scopeHandle, TrackerHandle *handle, const char *groupName)
{
	TrackerHandle *scopeTrackerHandle;
	GroupTracker *scopeTracker = trackerFromTrackerHandle(scopeHandle);
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	GroupTracker *tempTracker, *lastScopeTracker = NULL;

	if (!scopeTracker)
		scopeTracker = layerGetTracker(tracker->parent_layer);
	scopeTrackerHandle = trackerHandleFromTracker(scopeTracker);
	assert(scopeTrackerHandle);

	if (!wleTrackerIsDescendant(handle, scopeTrackerHandle) || (scopeTracker == tracker && (!groupName || !groupName[0])))
		return false;
	else if (groupName)
	{	
		if (strncmp(GROUP_UNNAMED_PREFIX, groupName, strlen(GROUP_UNNAMED_PREFIX)) == 0)
			return false;
		else if (scopeTracker == tracker)
			return true;
	}

	// at this point, tracker and scope tracker should be unequal, and group name may or may not be specified
	tempTracker = trackerGetScopeTracker(tracker);
	while (tempTracker != scopeTracker)
	{
		lastScopeTracker = tempTracker;
		tempTracker = trackerGetScopeTracker(tempTracker);
	}

	if (lastScopeTracker)
	{
		const char *scopeName = trackerGetUniqueScopeName(lastScopeTracker->def, tracker, groupName);
		if (!wleIsUniqueNameSpecified(scopeName))
			return false;
	}
	return true;
}


/*
// TODO: this function was primarily used by library deletion operation; we need to reevaluate how
// deletion from library will occur with the new lock system in place
static int countObjLibReferences(GroupDef *refDef, GroupDef ***containerDefs)
{
	int i, j, k;
	assert(refDef && groupIsObjLib(refDef));
	// map references
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		GroupFile *file = layerGetGroupFile(zmapGetLayer(NULL, i));
		if (!file)
			continue;
		for (j = 0; j < gfileGetGroupDefCount(file); j++)
		{
			GroupDef *def = gfileGetGroupDefByIndex(file, j);
			if (def->name_str[0] == '^')
				continue;
			for (k = 0; k < def->child_count; k++)
			{
				if (def->children[k].def == refDef)
					eaPush(containerDefs, def);
			}
		}
	}
	// object library references
	for (i = 0; i < objlibGetGroupFileCount(); i++)
	{
		GroupFile *file = objlibGetGroupFileByIndex(i);
		for (j = 0; j < gfileGetGroupDefCount(file); j++)
		{
			GroupDef *def = gfileGetGroupDefByIndex(file, j);
			if (def->name_str[0] == '^')
				continue;
			for (k = 0; k < def->child_count; k++)
			{
				if (def->children[k].def == refDef)
					eaPush(containerDefs, def);
			}
		}
	}
	return eaSize(containerDefs);
}
*/

#endif
