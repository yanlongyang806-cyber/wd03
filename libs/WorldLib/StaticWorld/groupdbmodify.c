#include <string.h>
#include "timing.h"
#include "WorldGridPrivate.h"
#include "ObjectLibrary.h"
#include "groupdbmodify.h"
#include "wlEncounter.h"
#include "EString.h"
#include "wlState.h"
#include "logging.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef enum {
	UM_NORMAL,		// started on layer object
	UM_PRIVATE,		// started on private lib
	UM_PUBLIC,		// started on public lib
} UpdateMode;

/******
* This file will deal with the set of basis functions for doing group manipulation in the world.
* Every editor command should be able to combine these basis functions to perform their required
* operations.  The guidelines for what should be a basis function (as opposed to an editor
* operation) are as follows:
*
* 1) they should never depend on editor-specific states.
* 2) they should take care of all tracker dirtying and reloading - any calling functions should be
*    able to assume that the tracker tree is valid after each call to a basis function.
* 3) they should only take TrackerHandle inputs, as GroupTrackers are too volatile to use reliably.
* 4) they should encompass all possible functions required to do anything in an editor without
*    violating 1-3.
*
* Note that these basis functions will operate on the GroupDefs themselves, even though TrackerHandles
* are used as arguments.  If instancing is to occur, groupdbInstance should be used beforehand.  Also,
* operations are to call worldUpdateBounds to recalculate bounds after all manipulation has occurred.
******/
/******
* UTIL
******/
void groupdbUpdateBounds(GroupDefLib *def_lib)
{
	if (def_lib->zmap_layer)
	{
		GroupDef **lib_defs = groupLibGetDefEArray(def_lib);
		int i;

		// for layer groups, just dirty all of the bounds on the layer defs
		for (i = 0; i < eaSize(&lib_defs); i++)
			lib_defs[i]->bounds_valid = 0;
		worldUpdateBounds(false, false);
	}
	else
		worldUpdateBounds(true, false);
}

void groupdbDirtyDef(GroupDef *def, int child_idx)
{
	groupDefModify(def, child_idx, true);
}

void groupdbDirtyTracker(GroupTracker *tracker, int child_idx)
{
	groupdbDirtyDef(tracker->def, child_idx);
}

/******
* TRANSACTIONS
******/
/******
* This function should generally be called at the beginning of any operation.  This function initializes
* the undo journal.
******/
void groupdbTransactionBegin(void)
{
	journalBeginEntry();
}

/******
* This function should generally be called at the end of any operation.  This function finalizes an
* undo journal entry and recalculates bounds.
******/
JournalEntry *groupdbTransactionEnd(void)
{
	return journalEndEntry();
}

void groupdbUndo(JournalEntry *entry)
{
	journalUndo(entry);
}

/******
* MOVING
******/
/******
* This function moves a specified tracker to a certain position/rotation specified by a Mat4.
* PARAMS:
*   handle - TrackerHandle to the tracker being moved
*   mat - Mat4 world matrix of new position of the tracker
******/
void groupdbMove(const TrackerHandle *handle, Mat4 mat)
{
	GroupTracker *tracker;
	TrackerHandle *parentHandle;
	ZoneMapLayer *layer;
	Mat4 parentMat, buf;
	bool isObjLib;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->parent);
	assert(parentHandle = trackerHandleFromTracker(tracker->parent));
	layer = tracker->parent_layer;
	isObjLib = groupIsObjLib(tracker->parent->def);
	
	filelog_printf("objectLog", "groupdbMove %d %s, child %d (%d %s)\n", tracker->parent->def->name_uid, tracker->parent->def->name_str,
		tracker->idx_in_parent, tracker->def->name_uid, tracker->def->name_str);

	// perform the move
	journalDef(tracker->parent->def);
	trackerGetMat(tracker->parent, parentMat);
	invertMat4Copy(parentMat, buf);
	mulMat4(buf, mat, tracker->parent->def->children[tracker->idx_in_parent]->mat);
	groupdbDirtyTracker(tracker->parent, tracker->idx_in_parent);

	// refresh the trackers
	if (tracker->parent->def)
		groupdbUpdateBounds(tracker->parent->def->def_lib);

	if (isObjLib)
		zmapTrackerUpdate(NULL, false, false);
	else
		layerTrackerUpdate(layer, false, false);  // this can delete the tracker, so the pointer will not be valid after this
}

/******
* SCALING
******/
/******
* This function scales a specified tracker to a certain scale specified by a Vec3.
* PARAMS:
*   handle - TrackerHandle to the tracker being moved
*   scale - Vec3 scale
******/
void groupdbSetScale(const TrackerHandle *handle, const Vec3 scale)
{
	GroupTracker *tracker;
	ZoneMapLayer *layer;
	bool isObjLib;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);
	layer = tracker->parent_layer;
	isObjLib = groupIsObjLib(tracker->def);

	// perform the scaling
	journalDef(tracker->def);
	groupQuantizeScale(scale, tracker->def->model_scale);
	groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);

	// refresh the trackers
	groupdbUpdateBounds(tracker->def->def_lib);
	if (isObjLib)
		zmapTrackerUpdate(NULL, false, false);
	else
		layerTrackerUpdate(layer, false, false);

	filelog_printf("objectLog", "groupdbSetScale %d %s\n", tracker->def->name_uid, tracker->def->name_str);
}

/******
* This function transfers the GroupDef (and its children) at the specified TrackerHandle into another
* layer given by a specified filename.  The transferred group is given the specified new name.
* Note that this operation does not do any sort of deletion.
* PARAMS:
*   handle - TrackerHandle pointing to the GroupDef to transfer
*   destFileName - string of the destination file
*   newName - string of the new name to give the transferred GroupDef
* RETURNS:
*   int UID of the new, transferred GroupDef, 0 if nothing was transferred
******/
int groupdbTransfer(const TrackerHandle *handle, const char *destFileName, const char *newName)
{
	GroupTracker *tracker;
	GroupDefLib *destLib;
	GroupDef *oldDef, *newDef;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->parent && tracker->def);
	oldDef = tracker->def;	

	filelog_printf("objectLog", "groupdbTransfer %d %s FROM %s TO %s\n", oldDef->name_uid, oldDef->name_str, oldDef->filename, destFileName);

	// don't need to do anything if the tracker's GroupDef is already in the destination file
	if (oldDef->filename == destFileName)
		return 0;

	// get the destination group library
	destLib = layerGetGroupDefLib(zmapGetLayerByName(NULL, destFileName));
	if (!destLib)
		destLib = objectLibraryGetEditingDefLib();
	assert(destLib);

	if (oldDef->def_lib != destLib)
	{
		// copy the definition into the new file
		newDef = groupLibCopyGroupDef(destLib, destFileName, oldDef, newName, true, false, false, 0, true);
		assert(newDef);
	}
	else
	{
		newDef = oldDef;
		newDef->filename = destFileName;
		groupDefModify(newDef, UPDATE_GROUP_PROPERTIES, true);
	}

	return newDef->name_uid;
}

/******
* INSERTING
******/
/******
* This function checks to make sure that placing a specified child group definition into a particular parent
* will not create an infinite loop.
* PARAMS:
*   parent - GroupTracker parent
*   child - GroupDef child to test with parent
* RETURNS:
*   bool indicating whether placing child in parent is safe or not
******/
bool groupdbLoopCheck(GroupDef *parent, GroupDef *child)
{
	int i;

	// NULL GroupDefs cannot produce loops
	if (!child || !parent)
		return true;
	// base case, when a child is caught being inserted into itself
	else if (child == parent)
		return false;
	// recursive case, checking to make sure none of the child's descendants ever become the parent
	else
	{
		GroupChild **def_children = groupGetChildren(child);
		for (i = 0; i < eaSize(&def_children); i++)
		{
			GroupDef *child_def = groupChildGetDefEx(parent, def_children[i]->name_uid, def_children[i]->name, true, true);
			if (child_def && !groupdbLoopCheck(parent, child_def))
				return false;
		}
	}

	return true;
}

/******
* This function inserts a child GroupDef into a parent GroupDef at the specified index.
* PARAMS:
*   parent - GroupDef parent where child will be inserted
*   child - GroupDef child to be inserted
*   index - index at which child will be inserted into parent
******/
static bool groupdbInsertInGroup(GroupDef *parent, GroupDef *child, int index)
{
	GroupChild *new_child;
	int parent_id = 0;
	if (!child || !parent)
		return false;

	filelog_printf("objectLog", "groupdbInsertInGroup %d %s INTO %d %s AT %d", child->name_uid, child->name_str, parent->name_uid, parent->name_str, index);

	journalDef(parent);

	// ensure the index does not exceed the range of [0, parent->def_children.child_count + 1]
	if (index < 0 || index > eaSize(&parent->children))
		index = eaSize(&parent->children);

	new_child =  StructCreate(parse_GroupChild);
	new_child->name_uid = child->name_uid;
	new_child->name = child->name_str;
	new_child->always_use_seed = true;
	eaInsert(&parent->children, new_child, index);

	if (!groupIsPublic(child) && groupIsObjLib(parent))
		parent_id = (parent->root_id != 0) ? parent->root_id : parent->name_uid;

	if (groupShouldDoTransfer(parent, child))
	{
		child = groupLibCopyGroupDef(parent->def_lib, parent->filename, child, NULL, true, false, false, parent_id, true);
	}

	// and populate the new GroupChild
	groupChildInitialize(parent, index, child, NULL, 0, 0, 0);

	groupDefModify(parent, index, true);

	if (groupIsPrivate(child))
	{
		journalDef(child);
	}

	filelog_printf("objectLog", "groupdbInsertInGroup DONE");

	return true;
}

TrackerHandle *groupdbInsertEx(const TrackerHandle *parent, GroupDef *def, Mat4 worldMat, F32 scale, const GroupChildSimpleData* pSimpleData, U32 seed, int index)
{
	TrackerHandle *ret;
	GroupTracker *parentTracker;
	ZoneMapLayer *layer;
	bool isObjLib;

	assert((parentTracker = trackerFromTrackerHandle(parent)) && parentTracker->def && def);
	layer = parentTracker->parent_layer;
	isObjLib = groupIsObjLib(parentTracker->def);
	ret = trackerHandleCreate(parentTracker);

	if (index == -1)
		index = eaSize(&parentTracker->def->children);
	else
		index = CLAMP(index, 0, eaSize(&parentTracker->def->children));

	// perform the creation
	if (groupdbLoopCheck(parentTracker->def, def))
	{
		Mat4 buf, parentMat;

		// add the new def to the parent's list of children
		if (groupdbInsertInGroup(parentTracker->def, def, index))
		{
			trackerHandlePushUID(ret, parentTracker->def->children[index]->uid_in_parent);

			parentTracker->def->children[index]->scale = scale;
			if( pSimpleData ) {
				StructCopy( parse_GroupChildSimpleData, pSimpleData, &parentTracker->def->children[index]->simpleData, 0, 0, 0 );
			}

			// set the matrix to the GroupChild
			trackerGetMat(parentTracker, parentMat);
			invertMat4Copy(parentMat, buf);
			mulMat4(buf, worldMat, parentTracker->def->children[index]->mat);
			if (groupIsObjLib(def) || 
				def->property_structs.building_properties || 
				def->property_structs.debris_field_properties || 
				def->property_structs.physical_properties.bRandomSelect)
			{
				parentTracker->def->children[index]->seed = seed;
			}
			groupdbDirtyTracker(parentTracker, index);

			// refresh the trackers
			groupdbUpdateBounds(parentTracker->def->def_lib);
			if (isObjLib)
				zmapTrackerUpdate(NULL, false, false);
			else
				layerTrackerUpdate(layer, false, false);
		}
	}
	else
	{
		// if a loop is found, delete any new GroupDefs created for this function
		Alertf("Aborting operation - infinite loop detected!");
		ret = NULL;
	}

	return ret;
}

/******
* This function adds a GroupDef (from a specified tracker) as a child to a parent GroupDef
* (from another specified tracker) at a specified index.  Note that this does NOT delete the child
* tracker from its original location.
* PARAMS:
*   parent - TrackerHandle to parent GroupDef
*   child - TrackerHandle to child GroupDef
*   index - index at which to add child to parent; -1 to add to the end
******/
TrackerHandle *groupdbInsert(const TrackerHandle *parent, const TrackerHandle *child, int index, bool randomizeSeed)
{
	GroupTracker *parentTracker, *childTracker;
	Mat4 childMat;
	GroupChildSimpleData* pSimpleData;

	assert((parentTracker = trackerFromTrackerHandle(parent)) && parentTracker->def);
	assert((childTracker = trackerFromTrackerHandle(child)) && childTracker->def);

	// perform the addition
	trackerGetMat(childTracker, childMat);

	if( childTracker->parent ) {
		pSimpleData = &childTracker->parent->def->children[ childTracker->idx_in_parent ]->simpleData;
	} else {
		pSimpleData = NULL;
	}
	
	return groupdbInsertEx(parent, childTracker->def, childMat, 0, pSimpleData, randomizeSeed ? rand() : trackerGetSeed(childTracker) ^ trackerGetSeed(parentTracker), index);
}

/******
* CREATING
******/
/******
*  This function creates a new tracker in the specified parent tracker's GroupDef.
* PARAMS:
*   parent - TrackerHandle to where the new tracker will be created
*   uid - int UID of the definition to paste/create in parent; 0 for a new definition
*   mat - Mat4 world matrix of tracker's initial position/rotation
*   index - int index in parent where tracker should be created
* RETURNS:
*   TrackerHandle (static - do not destroy!) to the pasted tracker
******/
TrackerHandle *groupdbCreate(const TrackerHandle *parent, int uid, Mat4 mat, F32 scale, int index)
{
	TrackerHandle *ret;
	GroupTracker *parentTracker;
	GroupDef *newDef;

	assert((parentTracker = trackerFromTrackerHandle(parent)) && parentTracker->def);

	// obtain the GroupDef that will be pasted/placed
	if (uid == 0)
	{
		// if uid is 0, then we create a new def from scratch
		char groupName[256];
		GroupDefLib *def_lib = parentTracker->def->def_lib;
		int root_id = 0;
		if (!def_lib->zmap_layer)
			root_id = (parentTracker->def->root_id != 0) ? parentTracker->def->root_id : parentTracker->def->name_uid;
		groupLibMakeGroupName(def_lib, NULL, SAFESTR(groupName), parentTracker->def->root_id);
		newDef = groupLibNewGroupDef(def_lib, parentTracker->def->filename, uid, groupName, root_id, true, true);
	}
	else
	{
		// for non-zero uid's, find the GroupDef that is paired with the uid
		newDef = groupLibFindGroupDef(parentTracker->def->def_lib, uid, false);
		if (!newDef && uid < 0)
			newDef = objectLibraryGetGroupDef(uid, false);
	}

	// something went wrong if we can't find the def
	assert(newDef);

	ret = groupdbInsertEx(parent, newDef, mat, scale, NULL, rand(), index);
	if (!ret && uid == 0)
		groupDefFree(newDef);
	else
		groupdbCheckRemoveDefFromLib(newDef, NULL);

	return ret;
}

/*****
* This function checks to see if there are no more references
* to this def in the library, and removes it from the
* stashtables if it is.
*****/
void groupdbCheckRemoveDefFromLib(GroupDef *def, StashTable new_defs)
{
	if (!def->def_lib || def->def_lib->dummy)
		return; // This is not a real def; don't delete

	if (def->name_uid == 1)
		return; // This is a layer root def; don't delete

	if (groupIsObjLib(def) && groupIsPublic(def))
		return; // Can't currently delete *public* defs from the Object Library

	// Look through all defs in the library for references (use new if we're in receiveIntoDefLib)
	FOR_EACH_IN_STASHTABLE(def->def_lib->defs, GroupDef, lib_def)
	{
		GroupDef *new_def;
		if (new_defs && stashIntFindPointer(new_defs, lib_def->name_uid, &new_def))
		{
			// this def has been replaced.  We'll check the new defs separately
			continue;
		}

		FOR_EACH_IN_EARRAY(lib_def->children, GroupChild, child)
		{
			if (child->name_uid == def->name_uid)
				return; // Reference exists; don't delete
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	// now check all the new defs
	FOR_EACH_IN_STASHTABLE(new_defs, GroupDef, lib_def)
	{
		FOR_EACH_IN_EARRAY(lib_def->children, GroupChild, child)
		{
			if (child->name_uid == def->name_uid)
				return; // Reference exists; don't delete
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	journalDef(def);

	filelog_printf("objectLog", "groupdbCheckRemoveDefFromLib: DELETING %s (%d)", def->name_str, def->name_uid);
	//printf("Deleting def %d\n", def->name_uid);

	stashIntRemovePointer(def->def_lib->defs, def->name_uid, NULL);
	stashRemoveInt(def->def_lib->def_name_table, def->name_str, NULL);
	def->filename = NULL;

	// Recurse
	FOR_EACH_IN_EARRAY(def->children, GroupChild, child)
	{
 		GroupDef *child_def = groupLibFindGroupDef(def->def_lib, child->name_uid, true);
		if (child_def)
			groupdbCheckRemoveDefFromLib(child_def, new_defs);
	}
	FOR_EACH_END;

	// We do not destroy def here, because in journalDefInEntry, the groupDefBack grabs a pointer to it, to support undo operations
}

/******
* REPLACING
******/
/******
* This function deletes a GroupChild from a specified GroupDef.
* PARAMS:
*   def - GroupDef from which to delete the GroupChild
*   uidInParent - int uid of the GroupChild in its parent def
******/
static void groupdbDeleteGroupChild(GroupDef *def, int uidInParent)
{
	int index;
	GroupChild *child;
	GroupDef *child_def;

	if (!def)
		return;
	for (index = 0; index < eaSize(&def->children); index++)
	{
		if (def->children[index]->uid_in_parent == uidInParent)
			break;
	}
	if (index >= eaSize(&def->children))
	{
		filelog_printf("objectLog", "groupdbDeleteGroupChild FAILED: Index %d is out of bounds in %d %s", index, def->name_uid, def->name_str);
		return;
	}

	journalDef(def);
	child = def->children[index];
	eaRemove(&def->children, index);

	child_def = groupChildGetDef(def, child, true);
	if (child_def)
	{
		groupdbCheckRemoveDefFromLib(child_def, NULL);
	}
	StructDestroy(parse_GroupChild, child);

	groupDefModify(def, UPDATE_REMOVED_CHILD, true);

	filelog_printf("objectLog", "groupdbDeleteGroupChild SUCCEEDED: Index %d removed from %d %s", index, def->name_uid, def->name_str);
}

/******
* This function looks through a specified GroupFile, finds all of its GroupChild references to a
* particular GroupDef and replaces that reference with a reference to a new GroupDef, deleting it
* if that new GroupDef is NULL.
* PARAMS:
*   def_lib - GroupDefLib to search through
*   oldDef - GroupDef reference to search for and replace
*   newDef - GroupDef that will replace oldDef; if NULL, oldDef references will be deleted
******/
static void groupdbReplaceRefsInFile(GroupDefLib *def_lib, GroupDef *oldDef, GroupDef *newDef)
{
	GroupDef **lib_defs;
	int i, j;

	if (!oldDef)
		return;

	filelog_printf("objectLog", "groupdbReplaceRefsInFile: REPLACE %d %s WITH %d %s", oldDef->name_uid, oldDef->name_str, newDef->name_uid, newDef->name_str);

	lib_defs = groupLibGetDefEArray(def_lib);
	for (i = 0; i < eaSize(&lib_defs); i++)
	{
		for (j = 0; j < eaSize(&lib_defs[i]->children); j++)
		{
			// if a child GroupChild's def is equal to the one we are replacing, replace it
			GroupDef *child_def = groupChildGetDef(lib_defs[i], lib_defs[i]->children[j], false);
			if (child_def == oldDef)
			{
				journalDef(lib_defs[i]);
				if (!newDef)
				{
					groupdbDeleteGroupChild(lib_defs[i], lib_defs[i]->children[j]->uid_in_parent);
					j--;
					groupDefModify(lib_defs[i], UPDATE_REMOVED_CHILD, true);
				}
				else
				{
					//lib_defs[i]->children[j]->def = newDef;
					lib_defs[i]->children[j]->name_uid = newDef->name_uid;
					lib_defs[i]->children[j]->name = newDef->name_str;
					groupDefModify(lib_defs[i], j, true);
				}
			}
		}
	}
}

/******
* This function replaces all GroupChild references to a particular GroupDef with references to
* a new GroupDef, deleting the old GroupChild reference entirely if the new GroupDef is specified as
* NULL.  TomY NOTE: For this to work it needs to be updated to use GroupDefLib's.
* PARAMS:
*   file - GroupFile in which the replacements will occur
*   oldDef - GroupDef to search for as a child of other GroupDef's
*   newDef - GroupDef to replace oldDef; if NULL, oldDef GroupChild's will be deleted
******/
/*static void groupdbReplaceRefs(GroupFile *file, GroupDef *oldDef, GroupDef *newDef)
{
	int i;

	if (!oldDef)
		return;

	// if the replacement is localized to a single group file
	groupdbReplaceRefsInFile(file, oldDef, newDef);

	// otherwise, we have to go through all of the map's group files
	// TODO: figure out how to deal with deletion of library pieces
	/*else
	{
		// replace throughout layer files
		for (i = 0; i < zmapGetLayerCount(NULL); i++)
		{
			GroupFile *file = layerGetGroupFile(zmapGetLayer(NULL, i));

			if (!file)
				continue;
			groupdbReplaceRefsInFile(file, oldDef, newDef);
		}

		// replace throughout object library group files
		for (i = 0; i < objlibGetGroupFileCount(); i++)
		{
			GroupFile *file = objlibGetGroupFileByIndex(i);

			if (!file)
				continue;
			groupdbReplaceRefsInFile(file, oldDef, newDef);
		}
	}
}*/

/******
* This function replaces all instances of the GroupDef at a specified TrackerHandle with another
* GroupDef given by a specified UID.  Note that this can be a somewhat expensive operation when
* replacing an object library GroupDef, as the function must iterate through every layer on
* the map.
* PARAMS:
*   handle - TrackerHandle pointing to the GroupDef being replaced
*   uid - int UID of what will replace the GroupDef at handle; if 0, then the old GroupDef at handle
*         will be deleted from the parent
******/
void groupdbReplace(const TrackerHandle *handle, int uid)
{
	GroupTracker *tracker;
	ZoneMapLayer *layer;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->parent && tracker->parent->def && tracker->def);

	// replacing a layer group
	if (layer = tracker->parent_layer)
	{
		// get the GroupDef that will replace the old GroupDef
		GroupDefLib *def_lib = layerGetGroupDefLib(layer);
		GroupDef *newDef;
		
		assert(def_lib);
		if (uid == 0)
			newDef = NULL;
		else
		{
			newDef = groupLibFindGroupDef(def_lib, uid, false);
			if (!newDef)
			{
				newDef = objectLibraryGetEditingGroupDef(uid, false);
				if (!newDef)
				{
					newDef = objectLibraryGetEditingCopy(objectLibraryGetGroupDef(uid, true), true, false);
				}
				assert(newDef && groupIsPublic(newDef));
			}
		}

		groupdbReplaceRefsInFile(tracker->parent->def->def_lib, tracker->def, newDef);
		layerTrackerUpdate(layer, false, false);
	}
}

/******
* MOVE TO INDEX
******/
/******
* This function moves the tracker at the specified handle to a new index in its parent.
* PARAMS:
*   handle - TrackerHandle to move
*   newIdxInParent - int destination index; make sure this is within the parent's index bounds
******/
void groupdbMoveToIndex(const TrackerHandle *handle, int newIdxInParent)
{
	GroupTracker *tracker;
	ZoneMapLayer *layer;
	bool isObjLib;
	GroupChild *child;
	int i;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->parent && tracker->parent->def);
	assert(newIdxInParent >= 0 && newIdxInParent < eaSize(&tracker->parent->def->children));

	// do nothing if nothing is changing
	if (newIdxInParent == tracker->idx_in_parent)
		return;

	filelog_printf("objectLog", "groupdbMoveToIndex: %d TO %d IN %d %s", tracker->idx_in_parent, newIdxInParent, tracker->parent->def->name_uid, tracker->parent->def->name_str);

	layer = tracker->parent->parent_layer;
	isObjLib = groupIsObjLib(tracker->parent->def);
	
	child = eaRemove(&tracker->parent->def->children, tracker->idx_in_parent);
	eaInsert(&tracker->parent->def->children, child, newIdxInParent);

	if (newIdxInParent < tracker->idx_in_parent)
	{
		for (i = newIdxInParent; i <= tracker->idx_in_parent; i++)
			groupdbDirtyTracker(tracker->parent, i);
	}
	else
	{
		for (i = tracker->idx_in_parent; i <= newIdxInParent; i++)
			groupdbDirtyTracker(tracker->parent, i);
	}

	// refresh the trackers
	groupdbUpdateBounds(tracker->parent->def->def_lib);
	if (isObjLib)
		zmapTrackerUpdate(NULL, false, false);
	else
		layerTrackerUpdate(layer, false, false);
}

/******
* DELETING
******/
/******
* This function determines whether a particular tracker should be deleted in the recursive processing
* of a delete command.  It checks to make sure that a model-less tracker is not being used as a sound,
* light, etc.
* PARAMS:
*   tracker - GroupTracker to check for deletion allowance
* RETURNS:
*   bool indicating whether the tracker can be deleted
******/
static bool groupdbShouldDelete(GroupTracker *tracker)
{
	if (!tracker->def)
		return true;
	if (eaSize(&tracker->def->children) != 0 || 
		tracker->def->model || 
		tracker->def->is_layer ||
		!StructIsZero(&tracker->def->property_structs))
	{
		return false;
	}
	if (tracker->def->property_structs.physical_properties.bNamedPoint)
		return false;
	if (tracker->def->property_structs.fx_properties && tracker->def->property_structs.fx_properties->pcName)
		return false;
	if (groupHasLight(tracker->def))
		return false;
	if (tracker->def->property_structs.volume)
		return false;
	if (tracker->def->property_structs.sound_sphere_properties)
		return false;
	return true;
}

/******
* This function initializes delete flags on GroupDef's to prepare for a delete operation, which needs
* these flags to all begin set to false.
* PARAMS:
*   tracker - GroupTracker where the flag reset will start from and propagate to its children
******/
static void groupdbResetDeleteFlags(GroupTracker *tracker)
{
	int i;

	if (!tracker || !tracker->def)
		return;

	tracker->def->deleteMe = false;
	for (i = 0; i < tracker->child_count; i++)
		groupdbResetDeleteFlags(tracker->children[i]);
}

/******
* This function is the deletion recursion workhorse - if a deletion empties a GroupDef, then this function
* is called on the top tracker that can be affected by the deletion (for UM_LIBRARY mode, this is the top-level
* library piece of the deleted group's parent; for UM_NORMAL mode, this is the layer tracker).  The function
* recurses through the children of the top tracker and deletes groups that need to be deleted; if, after a
* deletion, a group has no more children, it, too, is deleted.
* PARAMS:
*   tracker - GroupTracker of the top tracker where deletion recursion will start
*   mode - UpdateMode mode - in UM_NORMAL mode, the recursion should not go into library pieces
******/
static void groupdbDeleteMain(GroupTracker *tracker, UpdateMode mode)
{
	int i;

	if (!tracker->def || tracker->def->deleteMe || (mode == UM_NORMAL && groupIsObjLib(tracker->def)))
		return;
	for (i = tracker->child_count - 1; i >= 0; i--)
	{
		groupdbDeleteMain(tracker->children[i], mode);
		if (tracker->children[i]->def && tracker->children[i]->def->deleteMe)
			groupdbDeleteGroupChild(tracker->def, tracker->children[i]->uid_in_parent);
	}
	if (groupdbShouldDelete(tracker))
		tracker->def->deleteMe = true;
}

/******
* This function deletes the specified tracker's GroupDef from its parent's.
* PARAMS:
*   handle - TrackerHandle to the tracker to delete
******/
void groupdbDelete(const TrackerHandle *handle)
{
	GroupTracker *tracker, *root;
	ZoneMapLayer *layer;
	bool isObjLib;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->parent);
	layer = tracker->parent->parent_layer;
	isObjLib = groupIsObjLib(tracker->parent->def);

	filelog_printf("objectLog", "groupdbDelete BEGIN: %d %s IN %d %s", tracker->def->name_uid, tracker->def->name_str, tracker->parent->def->name_uid, tracker->parent->def->name_str);

	// determine where to stop propagating deletions (i.e. a "root" tracker/def that cannot be deleted)
	if (isObjLib)
	{
		root = tracker->parent;
		while (!groupIsPublic(root->def))
			root = root->parent;
	}
	else
		root = layerGetTracker(layer);

	// delete the specified GroupDef from its parent
	groupdbDeleteGroupChild(tracker->parent->def, tracker->uid_in_parent);
	if (groupdbShouldDelete(tracker->parent))
	{
		// propagate deletion upward as necessary
		groupdbResetDeleteFlags(root);
		tracker->parent->def->deleteMe = true;
		groupdbDeleteMain(root, isObjLib ? UM_PUBLIC : UM_NORMAL);
	}

	groupdbUpdateBounds(tracker->parent->def->def_lib);
	if (isObjLib)
		zmapTrackerUpdate(NULL, false, false);
	else
		layerTrackerUpdate(layer, false, false);

	filelog_printf("objectLog", "groupdbDelete DONE");
}

typedef struct ParentChildTracking
{
	GroupTracker *parent;
	GroupTracker **trackers;
} ParentChildTracking;

/******
* This function deletes the specified tracker's GroupDef from its parent.
* PARAMS:
*   handles - earray of TrackerHandles to the trackers to delete
******/
void groupdbDeleteList(const TrackerHandle **handles)
{
	GroupTracker *tracker, *root;
	ZoneMapLayer *layer, **layers = NULL;
	bool isObjLib, anyIsObjLib, found;
	int i, j;
	ParentChildTracking **eaTracking = NULL;

	filelog_printf("objectLog", "groupdbDeleteList BEGIN");

	anyIsObjLib = false;

	for (i = eaSize(&handles) - 1; i >= 0; i--)
	{
		assert((tracker = trackerFromTrackerHandle(handles[i])) && tracker->parent);
		for (j = 0; j < eaSize(&eaTracking); j++)
		{
			if (eaTracking[j]->parent == tracker->parent)
			{
				eaPush(&eaTracking[j]->trackers, tracker);
				break;
			}
		}
		if (j == eaSize(&eaTracking))
		{
			ParentChildTracking *tracking = calloc(1, sizeof(ParentChildTracking));
			tracking->parent = tracker->parent;
			eaPush(&tracking->trackers, tracker);
			eaPush(&eaTracking, tracking);
		}
	}

	for (i = eaSize(&eaTracking) - 1; i >= 0; i--)
	{
		layer = eaTracking[i]->trackers[0]->parent->parent_layer;
		isObjLib = groupIsObjLib(eaTracking[i]->trackers[0]->parent->def);

		// determine where to stop propagating deletions (i.e. a "root" tracker/def that cannot be deleted)
		if (isObjLib)
		{
			root = eaTracking[i]->trackers[0]->parent;
			while (!groupIsPublic(root->def))
			{
				root = root->parent;
			}
		}
		else
		{
			root = layerGetTracker(layer);
		}

		// delete the specified GroupDef from its parent
		for (j = eaSize(&(eaTracking[i]->trackers)) - 1; j >= 0; j--)
		{
			groupdbDeleteGroupChild(eaTracking[i]->trackers[0]->parent->def, eaTracking[i]->trackers[j]->uid_in_parent);
		}
		if (groupdbShouldDelete(eaTracking[i]->trackers[0]->parent))
		{
			// propagate deletion upward as necessary
			groupdbResetDeleteFlags(root);
			eaTracking[i]->trackers[0]->parent->def->deleteMe = true;
			groupdbDeleteMain(root, isObjLib ? UM_PUBLIC : UM_NORMAL);
		}

		groupdbUpdateBounds(eaTracking[i]->trackers[0]->parent->def->def_lib);
		if (isObjLib)
		{
			anyIsObjLib = true;
		}
		else if (!anyIsObjLib)
		{
			found = false;
			for (j = 0; j < eaSize(&layers); j++)
			{
				if (layers[j] == layer)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				eaPush(&layers, layer);
			}
		}
		eaDestroy(&(eaTracking[i]->trackers));
	}
	if (anyIsObjLib)
	{
		zmapTrackerUpdate(NULL, false, false);
	}
	else
	{
		for (i = 0; i < eaSize(&layers); i++)
		{
			layerTrackerUpdate(layers[i], false, false);
		}
	}
	eaDestroy(&layers);
	for (i = 0; i < eaSize(&eaTracking); i++)
	{
		free(eaTracking[i]);
	}
	eaDestroy(&eaTracking);

	filelog_printf("objectLog", "groupdbDeleteList DONE");
}

/*******
* This function deletes all instances of a library GroupDef and removes it from its GroupFile.
* Note that this operation is NOT UNDOABLE and should NOT be called within a transaction.  This
* also does not delete references to the GroupDef, so that should be done first.
* PARAMS:
*   handle - TrackerHandle to the GroupDef being destroyed
******/
void groupdbDestroy(int uid)
{
	// TODO: fix this later
	/*
	GroupDef *def = objectLibraryGetGroupDef(uid);

	// make sure the tracker is either NULL or is not a layer
	assert(def);
	groupdbReplaceRefs(def, NULL);
	groupdbDestroyDef(def);
	groupdbUpdateBounds(def->def_lib);
	zmapTrackerUpdate(NULL, false, false);
	*/
}

/******
* INSTANCING
******/
/******
* This function is the main instancing workhorse, called by groupdbInstance.  It begins by
* instancing the specified tracker if its definition is used for multiple trackers in the map (i.e.
* it makes sure instancing is actually necessary by checking if there are copies around).  Then
* the function recursively traverses up the tracker's ancestors to ensure those are unique.
* PARAMS:
*   handle - TrackerHandle where upward instancing begins.
*   mode - UM_NORMAL or UM_LIBRARY; instancing within a library piece (UM_LIBRARY mode) behaves
*          differently in that it stops recursing upward when it reaches the top-level GroupDef of
*          the library piece.
* RETURNS:
*   GroupDef - the instanced GroupDef
******/
static GroupDef *groupdbInstanceUpward(const TrackerHandle *handle, UpdateMode mode, bool recurse)
{
	GroupTracker *tracker;
	GroupDef *instanced = NULL;
	GroupDef *parentInstanced = NULL;
	int defCount = 0;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);

	// TODO: determine if this is correct: do we really instantiate up ALL parent libs?
	// base case for upward traversal of tracker tree; in edit library mode, we will only
	// recurse to the top-most tracker still in the object library
	if ((mode == UM_NORMAL && tracker->def->name_uid == 0) ||
		(mode == UM_PRIVATE && groupIsPublic(tracker->def)))
	{
		filelog_printf("objectLog", "groupdbInstanceUpward SKIP %d %s", tracker->def->name_uid, tracker->def->name_str);
		return NULL;
	}

	// count up the number of trackers with the same def and determine whether instancing is actually
	// necessary at all; public library pieces are always to be instantiated, regardless of the count
	if (mode == UM_PUBLIC)
		defCount = 2;
	else if (mode == UM_PRIVATE)
	{
		GroupTracker *topObjLibTracker = tracker;
		// TODO: this count isn't quite correct for nested libs if we don't instantiate through all parent libs
		// count occurrences only from topmost object library piece
		while (!groupIsPublic(topObjLibTracker->def))
			topObjLibTracker = topObjLibTracker->parent;
		defCount = defContainCount(topObjLibTracker->def, tracker->def);
	}
	else
		defCount = worldCountGroupDefs(tracker->def);

	if (defCount > 1)
	{
		GroupDef *def;
		int index = tracker->idx_in_parent;
		int parent_id = 0;

		if (groupIsObjLib(tracker->parent->def))
			parent_id = tracker->parent->def->root_id ? tracker->parent->def->root_id : tracker->parent->def->name_uid;
		else if (mode != UM_PUBLIC && recurse)
		{
			GroupTracker *parent_tracker = tracker->parent;
			while (parent_tracker)
			{
				if (parent_tracker->parent && groupIsObjLib(parent_tracker->parent->def))
				{
					parent_id = parent_tracker->parent->def->root_id ? parent_tracker->parent->def->root_id : parent_tracker->parent->def->name_uid;
					break;
				}
				parent_tracker = parent_tracker->parent;
			}
		}

		filelog_printf("objectLog", "groupdbInstanceUpward INSTANCING %d %s IN %d", tracker->def->name_uid, tracker->def->name_str, parent_id);

		def = groupLibCopyGroupDef(tracker->parent->def->def_lib, tracker->parent->def->filename, tracker->def, NULL, true, false, false, parent_id, true);

		// recurse upward to instance from the top downward (otherwise, the copied defs will be children
		// to non-copied defs)
		if (mode != UM_PUBLIC && recurse)
			parentInstanced = groupdbInstanceUpward(trackerHandleFromTracker(tracker->parent), mode, recurse);
		if (!parentInstanced)
		{
			parentInstanced = tracker->parent->def;

			// when recursion ends, we need to dirty the non-instanced parent to make sure its children
			// are reloaded
			groupDefModify(parentInstanced, UPDATE_GROUP_PROPERTIES, true);
		}

		// set the new definition onto the parent
		journalDef(parentInstanced);
		//parentInstanced->children[index]->def = def;
		parentInstanced->children[index]->name_uid = def->name_uid;
		parentInstanced->children[index]->name = def->name_str;

		instanced = def;

		filelog_printf("objectLog", "groupdbInstanceUpward RETURNING %d %s", def->name_uid, def->name_str);
	}
	return instanced;
}

/******
* This function instances the GroupDef for the specified tracker.
* PARAMS:
*   handle - TrackerHandle to the tracker to instance
******/
void groupdbInstance(const TrackerHandle *handle, bool recurse)
{
	GroupTracker *tracker;
	GroupDef *def;
	UpdateMode mode;
	ZoneMapLayer *layer;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);
	layer = tracker->parent_layer;

	// we cannot instance layers
	if (!tracker->parent)
		return;

	filelog_printf("objectLog", "groupdbInstance BEGIN %d %s", tracker->def->name_uid, tracker->def->name_str);

	// library mode only instances within the context of a library piece
	if (groupIsPublic(tracker->def))
		mode = UM_PUBLIC;
	else if (groupIsPrivate(tracker->def))
		mode = UM_PRIVATE;
	// normal mode instances map defs and top-level object library defs
	else
		mode = UM_NORMAL;

	// perform the instancing recursively
	def = groupdbInstanceUpward(handle, mode, recurse);
	if (def)
	{
		eaDestroyStruct(&def->logical_groups, parse_LogicalGroup);
		groupDefFixupMessages(def);
	}

	// reopen all of the necessary trackers
	if (def)
		groupdbUpdateBounds(def->def_lib);
	if (mode == UM_NORMAL)
		layerTrackerUpdate(layer, false, false);
	else
		zmapTrackerUpdate(NULL, false, false);

	filelog_printf("objectLog", "groupdbInstance DONE");
}

/******
* RENAMING
******/
/******
* This function renames the GroupDef for the specified tracker.
* PARAMS:
*   handle - TrackerHandle to the GroupDef to rename
*   newName - string name to give to the GroupDef
******/
void groupdbRename(const TrackerHandle *handle, const char *newName)
{
	GroupTracker *tracker;
	ZoneMapLayer *layer;
	bool isObjLib;
	char finalName[1024];
	int i;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);
	layer = tracker->parent_layer;
	isObjLib = groupIsObjLib(tracker->def);

	filelog_printf("objectLog", "groupdbRename: %d %s TO %s", tracker->def->name_uid, tracker->def->name_str, newName);

	journalDef(tracker->def);
	groupLibMakeGroupName(tracker->def->def_lib, newName, SAFESTR(finalName), tracker->def->root_id);
	groupDefRename(tracker->def, finalName);
	groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);

	for (i = 0; i < tracker->child_count; i++)
	{
		GroupTracker *child_tracker = tracker->children[i];
		if (groupIsPrivate(child_tracker->def))
		{
			char newChildName[1024];
			TrackerHandle *child_handle = trackerHandleCopy(trackerHandleFromTracker(child_tracker));
			devassert(handle != child_handle);
			groupGetInheritedName(newChildName, 1024, tracker->def, child_tracker->def);
			groupdbRename(child_handle, newChildName);
#pragma warning(suppress:6001) // /analyze flags "Using uninitialized memory '*child_handle'"
			trackerHandleDestroy(child_handle);
		}
	}

	if (tracker->def)
		groupdbUpdateBounds(tracker->def->def_lib);
	if (isObjLib)
		zmapTrackerUpdate(NULL, false, false);
	else
		layerTrackerUpdate(layer, false, false);
}

/******
* SET TAGS
******/
/******
* This function sets tags the GroupDef for the specified tracker.
* PARAMS:
*   handle - TrackerHandle to the GroupDef to rename
*   newTags - string tags to give to the GroupDef
******/
void groupdbSetTags(const TrackerHandle *handle, const char *newTags)
{
	GroupTracker *tracker;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);

	filelog_printf("objectLog", "groupdbSetTags: %d %s TO %s", tracker->def->name_uid, tracker->def->name_str, newTags);

	journalDef(tracker->def);
	StructCopyString(&tracker->def->tags, newTags);
	groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
}

/******
* RANDOMIZE
******/
/******
* This function sets a random seed onto the specified tracker.
* PARAMS:
*   handle - TrackerHandle on which to set the random seed
******/
void groupdbRandomize(const TrackerHandle *handle)
{
	GroupTracker *tracker;
	ZoneMapLayer *layer;
	bool isObjLib;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);

	// cannot randomize trackers without a parent
	if (!tracker->parent || !tracker->parent->def)
		return;

	layer = tracker->parent->parent_layer;
	isObjLib = groupIsObjLib(tracker->parent->def);

	journalDef(tracker->parent->def);
	tracker->parent->def->children[tracker->idx_in_parent]->seed = rand();
	groupdbDirtyTracker(tracker->parent, tracker->idx_in_parent);

	groupdbUpdateBounds(tracker->parent->def->def_lib);
	if (isObjLib)
		zmapTrackerUpdate(NULL, false, false);
	else
		layerTrackerUpdate(layer, false, false);

	filelog_printf("objectLog", "groupdbRandomize: %d %s NOW %d", tracker->def->name_uid, tracker->def->name_str, tracker->parent->def->children[tracker->idx_in_parent]->seed);
}

/******
* PROPERTIES
******/
/******
* This function edits a property on a GroupDef, overwriting any existing value for the given
* property or removing it if the value is NULL.
* PARAMS:
*   handle - TrackerHandle to the tracker being modified
*   propName - property name string
*   propVal - property value string; if NULL, removes property with propName
******/
//sfenton TODO: PropLoad: we can probably remove this function now
void groupdbEditProperty(const TrackerHandle *handle, const char *propName, const char *propVal)
{
	GroupTracker *tracker;
	ZoneMapLayer *layer;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);
	layer = tracker->parent_layer;
	if (!propVal)
		groupDefRemoveProperty(tracker->def, propName);
	else
		groupDefAddProperty(tracker->def, propName, propVal);
	groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
	groupdbUpdateBounds(tracker->def->def_lib);
	if (groupIsObjLib(tracker->def))
		zmapTrackerUpdate(NULL, false, false);
	else
		layerTrackerUpdate(layer, false, false);

	filelog_printf("objectLog", "groupdbEditProperty %d %s NAME %s VALUE %s\n", tracker->def->name_uid, tracker->def->name_str, propName, propVal ? propVal : "(null)");
}

/******
* This function sets the unique name for something at a specified scope.
* PARAMS:
*   scopeDef - GroupDef of the scope in which the name is being set
*   handle - TrackerHandle to the tracker whose unique name is being set
*   groupName - string name of a logical group in its closest scope (which should be specified
*               in the handle parameter
*   name - string new unique name to set 
*   update - bool indicating whether to call the trackerUpdate/worldUpdateBounds functions to refresh scope data
******/
void groupdbSetUniqueScopeName(GroupDef *scopeDef, const TrackerHandle *handle, const char *groupName, const char *name, bool update)
{
	GroupTracker *tracker, *origTracker;
	GroupDef *intermediateDef = NULL;
	char *oldName = NULL;
	int *uids = NULL;
	int i;

	assert(tracker = trackerFromTrackerHandle(handle));
	origTracker = tracker;

	filelog_printf("objectLog", "groupdbSetUniqueScopeName %d %s GROUP %s NAME %s\n", tracker->def->name_uid, tracker->def->name_str, groupName, name);

	if (!tracker->def || !name[0])
		return;
	if ((!groupDefNeedsUniqueName(tracker->def) || scopeDef == tracker->def) && (!groupName || !groupName[0]))
		return;
	if (groupName && groupName[0])
	{
		if (!groupIsPublic(tracker->def) && !tracker->def->is_layer)
			return;
		for (i = 0; i < eaSize(&tracker->def->logical_groups); i++)
		{
			if (strcmpi(tracker->def->logical_groups[i]->group_name, groupName) == 0)
				break;
		}
		if (i >= eaSize(&tracker->def->logical_groups))
			return;
	}

	// compile UIDs
	while (tracker->def != scopeDef)
	{
		// check for intermediate scopes
		if (tracker->def && tracker->def == tracker->closest_scope->def)		
		{
			intermediateDef = tracker->def;
			eaiClear(&uids);
		}

		eaiPush(&uids, tracker->uid_in_parent);
		tracker = tracker->parent;
	}
	assert(tracker->def == scopeDef);

	// change the name in the scope def
	if (tracker && tracker->def && tracker->def->name_to_path)
	{
		char *path = NULL;

		if (intermediateDef)
			oldName = strdup(trackerGetUniqueScopeName(intermediateDef, origTracker, groupName));

		eaiReverse(&uids);
		for (i = 0; i < eaiSize(&uids); i++)
			estrConcatf(&path, "%i,", uids[i]);
		if (oldName)
			estrConcatf(&path, "%s,", oldName);

		// deal with renaming logical groups at closest scope
		if (eaiSize(&uids) == 0 && groupName && groupName[0])
		{
			for (i = 0; i < eaSize(&tracker->def->logical_groups); i++)
			{
				if (strcmpi(tracker->def->logical_groups[i]->group_name, groupName) == 0)
				{
					journalDef(tracker->def);
					StructFreeString(tracker->def->logical_groups[i]->group_name);
					tracker->def->logical_groups[i]->group_name = StructAllocString(name);
					groupdbDirtyDef(tracker->def, -1);
				}
			}
		}
		else
		{
			// add under new name (if any) to both stash tables
			if (name && path)
			{
				journalDef(tracker->def);
				groupdbDirtyDef(tracker->def, -1);
				groupDefScopeSetPathName(tracker->def, path, name, true);
			}
		}

		estrDestroy(&path);
	}

	if (update)
	{
		worldUpdateBounds(true, false);
		zmapTrackerUpdate(NULL, false, false);
	}

	SAFE_FREE(oldName);
	eaiDestroy(&uids);
}

