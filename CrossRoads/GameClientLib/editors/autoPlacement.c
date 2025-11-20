#include "autoPlacementCommon.h"

#include "WorldGrid.h"

#ifndef NO_EDITORS
#include "WorldEditorUI.h"
#include "WorldEditorOperations.h"
#include "groupdbmodify.h"
#include "wlEncounter.h"

#include "Quat.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define AUTO_PLACE_GROUP_BASENAME	"AutoPlacementGroup"

typedef enum AutoPlacerState
{
	kAutoPlacerState_NONE,
	kAutoPlacerState_WAITINGFORSERVER,
	kAutoPlacerState_COUNT
	
} AutoPlacerState;

#endif 


// run-time auto placement data
AUTO_STRUCT;
typedef struct RTGroupPlacementData
{
	
	TrackerHandle* target_group;
	const char **papLogicalNamesList;

} RTGroupPlacementData;

AUTO_STRUCT;
typedef struct AutoPlaceQueuedData
{
	char* pGroupTrackerStringHandle;
	const AutoPlacementSet* pAutoPlacementSet;  NO_AST
} AutoPlaceQueuedData;


#ifndef NO_EDITORS


extern ParseTable parse_AutoPlaceQueuedData[];
#define TYPE_parse_AutoPlaceQueuedData AutoPlaceQueuedData
extern ParseTable parse_RTGroupPlacementData[];
#define TYPE_parse_RTGroupPlacementData RTGroupPlacementData


static const F32 MAGIC_PENETRATION_DEPTH = 2.25f/12.0f; // 3 inches

static AutoPlacerState sAutoPlacerState = kAutoPlacerState_NONE;
static AutoPlaceQueuedData** s_eaVolumeProcessQueue = NULL;

static AutoPlaceQueuedData s_queuedProcessData;

#define RELEASE_TRACKER_HANDLE(x)	if (x) { free(x); (x) = NULL; }
//#define RELEASE_TRACKER_HANDLE(x)	(((x)!=NULL?free(x):0),(x)=NULL)


static void processQueuedData();



// -------------------------------------------------------------------------------------
static void apGetVolumeInfo(GroupTracker *pTracker, AutoPlacementVolume *pVolume)
{
	Mat4 mtxWorld;
	GroupVolumeProperties *pProps;

	// get the object's translation 
	trackerGetMat(pTracker, mtxWorld);

	copyVec3(mtxWorld[3], pVolume->vPos);

	// get the volume data
	pProps = pTracker->def->property_structs.volume;
	if (pProps && pProps->eShape == GVS_Sphere) {
		pVolume->bAsCube = false;
		pVolume->fRadius = pProps->fSphereRadius;
	} else if(pProps && pProps->eShape == GVS_Box) {
		Vec3 vHalfExtents;

		pVolume->bAsCube = true;

		mat3ToQuat(mtxWorld, pVolume->qInvRot);
		quatInverse(pVolume->qInvRot, pVolume->qInvRot);

		copyVec3(pProps->vBoxMin, pVolume->vMin);
		copyVec3(pProps->vBoxMax, pVolume->vMax);

		// get the half extents for this bounding box
		subVec3(pVolume->vMax, pVolume->vMin, vHalfExtents);

		pVolume->fRadius = lengthVec3(vHalfExtents);
	}

}

// -------------------------------------------------------------------------------------
static void apGetVolumeList(GroupTracker *tracker, AutoPlacementVolume ***peaVolumeList, bool bGetProperties)
{
	S32 i;
	AutoPlacementVolume* pVolume;

	pVolume = StructCreate(parse_AutoPlacementVolume);
	assert(pVolume);
	apGetVolumeInfo(tracker, pVolume);
	eaPush(peaVolumeList, pVolume);
	
	if (bGetProperties)
	{
		pVolume->pProperties = tracker->def->property_structs.auto_placement_properties;
	}

	// go through the children of the groupTracker,
	// and find all the children that are sub-volumes
	// and add those to the volume list as well.
	for (i = 0; i < tracker->child_count; i++)
	{
		GroupTracker *pChild = tracker->children[i];
		if (pChild->def->property_structs.volume && pChild->def->property_structs.volume->bSubVolume)
		{
			pVolume = StructCreate(parse_AutoPlacementVolume);
			assert(pVolume);
			apGetVolumeInfo(pChild, pVolume);

			if (bGetProperties)
			{
				pVolume->pProperties = pChild->def->property_structs.auto_placement_properties;
			}

			eaPush(peaVolumeList, pVolume);
		}
	}
}

// -------------------------------------------------------------------------------------
static bool apCheckForVolumeOverride(AutoPlacementVolume **eaVolumeList, const Vec3 vPos, U32 resourceID, U32 *pOverrideResourceID)
{
	S32 i;

	*pOverrideResourceID = 0;

	for (i = 0; i < eaSize(&eaVolumeList); i++)
	{
		const AutoPlacementVolume *pVolume = eaVolumeList[i];
		if (pVolume->pProperties && pVolume->pProperties->override_list && apvPointInVolume(pVolume, vPos))
		{
			S32 x;
			for (x = 0; x < eaSize(&pVolume->pProperties->override_list); x++)
			{
				AutoPlacementOverride *pOverride = pVolume->pProperties->override_list[x];

				if (pOverride->resource_id == resourceID && pOverride->override_resource_id)
				{
					// we found an override for this resource
					*pOverrideResourceID = pOverride->override_resource_id;
					return true;
				}
			}

		}
	}

	return false;
}


// -------------------------------------------------------------------------------------
static void DeleteGroupTrackerAndAssociatedLogicalGroups(GroupTracker *pGroupTrackerToDelete)
{ 
	char **pLogicalGroupNames = NULL;

	int x;
	for (x = 0; x < pGroupTrackerToDelete->child_count; x++)
	{
		int i;
		// assuming that this is a auto-placed sub-group of the tracker.
		GroupTracker *pChild = pGroupTrackerToDelete->children[x];

		for (i = 0; i < pChild->child_count; i++)
		{
			GroupTracker *pAssumedAutoPlacedChild = pChild->children[i];
			if (pAssumedAutoPlacedChild && 
				pAssumedAutoPlacedChild->enc_obj && 
				pAssumedAutoPlacedChild->enc_obj->closest_scope &&
				pAssumedAutoPlacedChild->enc_obj->parent_group)
			{
				char *pScopeName;

				if (stashFindPointer(pAssumedAutoPlacedChild->enc_obj->closest_scope->obj_to_name, 
					pAssumedAutoPlacedChild->enc_obj->parent_group, &pScopeName))
				{
					assert(pScopeName);
					if (eaFind(&pLogicalGroupNames, pScopeName) == -1)
					{
						eaPush(&pLogicalGroupNames, pScopeName);
					}
				}
			}
		}


	}

	// now delete the groupTracker
	{
		TrackerHandle *pTrackerToDelete = trackerHandleFromTracker(pGroupTrackerToDelete);
		assert(pTrackerToDelete);

		groupdbDelete(pTrackerToDelete);
	}


	{
		WorldScope *pScope = (WorldScope*)zmapGetScope(NULL);
		if (!pScope)
		{
			return;
		}
		// after we delete the tracker, go through the stored logical groups and delete them 
		// but only if they are empty, 
		// they should be if the clients did not move anything around in the logical groups
		for (x = 0; x < eaSize(&pLogicalGroupNames); x++)
		{
			WorldEncounterObject *pEncounterObj = worldScopeGetObject(pScope, pLogicalGroupNames[x]);
			if (pEncounterObj && pEncounterObj->type == WL_ENC_LOGICAL_GROUP)
			{
				int i; 
				bool bEmpty = true;
				WorldLogicalGroup *pLogicalGroup = (WorldLogicalGroup*)pEncounterObj;

				for (i = 0; i < eaSize(&pLogicalGroup->objects); i++)
				{
					if (pLogicalGroup->objects[i] != NULL)
					{
						bEmpty = false;
						break;
					}
				}

				if (bEmpty)
				{
					wleOpLogicalUngroup(NULL, pLogicalGroupNames[x]);
				}
			}
		}
	}

	eaDestroy(&pLogicalGroupNames);
}


// -------------------------------------------------------------------------------------
// creates the tracker groups that we will group the new objects in
// as well as create and populate the runtime auto-placement data

static void deleteOldAndCreateNewGroup(const TrackerHandle *pTrackerHandle, const AutoPlacementSet *pAutoPlaceSet)
{
	#define NAME_BUFFER_SIZE 256
	GroupTracker *pGroupTracker = NULL;
	TrackerHandle *pParentGroupTracker;
	Mat4 mtxTransform;
	int i;
	char szBaseNameBuffer[NAME_BUFFER_SIZE] = {0};

	identityMat4(mtxTransform);
	
	// create the groups we will be placing all the auto-placed objects
	pParentGroupTracker = groupdbCreate(pTrackerHandle, 0, mtxTransform, 0, -1);
	assert(pParentGroupTracker);

	// create the group name that is used for this set
	strcpy(szBaseNameBuffer, AUTO_PLACE_GROUP_BASENAME);
	if (s_queuedProcessData.pAutoPlacementSet->set_name)
	{
		strcat_s(szBaseNameBuffer, NAME_BUFFER_SIZE, pAutoPlaceSet->set_name);
	}

	pGroupTracker = trackerFromTrackerHandle(pTrackerHandle);

	// now try and find the auto-placement group and delete it if it exists
	for (i = 0; i < pGroupTracker->child_count; i++)
	{
		GroupTracker *pChild = pGroupTracker->children[i];
		if (!_strnicmp(pChild->def->name_str, szBaseNameBuffer, strlen(szBaseNameBuffer)))
		{
			DeleteGroupTrackerAndAssociatedLogicalGroups(pChild);
			break;
		}
	}

	// now rename our new group
	groupdbRename(pParentGroupTracker, szBaseNameBuffer);
}


// -------------------------------------------------------------------------------------
void performAutoPlacement(const TrackerHandle *handle, const AutoPlacementSet *pAutoPlacementSet)
{
	AutoPlacementParams	 autoPlaceParams = {0};
	GroupTracker*		 pGroupTracker;
	
	if (sAutoPlacerState != kAutoPlacerState_NONE)
	{
		// display a warning that we are still waiting for the server to return 
		// the results from the previous auto-placement
		printf( "Error: (Auto-placement) Still waiting for server to complete last request.\n" );
		return;
	}

	// todo: pop up a dialog prompting the user to confirm that this will destroy all children of the current
	// tracker, and then rebuild the objects in the list.
	
	pGroupTracker = trackerFromTrackerHandle(handle);
	if (! pGroupTracker || !pGroupTracker->def || !pGroupTracker->def->property_structs.auto_placement_properties)
	{
		printf( "Error: (Auto-Placement) internal group tracker error.\n" );
		return;
	}
	
	apGetVolumeList(pGroupTracker, &autoPlaceParams.ppAutoPlacementVolumes, false);
		
	autoPlaceParams.pPlacementProperties = (AutoPlacementSet*)pAutoPlacementSet;
	
	// setup our state and stash the current 
	sAutoPlacerState = kAutoPlacerState_WAITINGFORSERVER;
	RELEASE_TRACKER_HANDLE(s_queuedProcessData.pGroupTrackerStringHandle);
	s_queuedProcessData.pGroupTrackerStringHandle = handleStringFromTracker(pGroupTracker);
	s_queuedProcessData.pAutoPlacementSet = pAutoPlacementSet;
	// 

	//
	deleteOldAndCreateNewGroup(handle, pAutoPlacementSet);

	printf( "(Auto-Placement) Processing Group(%s) Set(%s)\nSent command to server to generate object placement data. Waiting for response.\n", 
					pGroupTracker->def->name_str, pAutoPlacementSet->set_name );
	// send out the command to the server
	ServerCmd_autoGenerateObjectPlacement(&autoPlaceParams);
} 



void snapToSlopeRandomWithFacing(Mat3 mtx, const Vec3 vUp)
{
	Vec3 vForward, vRight;

	sphericalCoordsToVec3(vForward, TWOPI * qufrand(), 0.0f, 1.0f);
	
	crossVec3(vUp, vForward, vRight);
	if (vec3IsZero(vRight))
	{	// bad normal, just give the identity.
		identityMat3(mtx);
		return;
	}
	normalVec3(vRight);
	crossVec3(vRight, vUp, vForward);
	normalVec3(vForward);
	
	copyVec3(vRight, mtx[0]);
	copyVec3(vUp, mtx[1]);
	copyVec3(vForward, mtx[2]);
}

// -------------------------------------------------------------------------------------
#define PRINT_UPDATE_FREQUENCY 100

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void receiveAutoPlacementData(AutoPlaceObjectData *pData)
{
	GroupTracker *pGroupTracker;
	const TrackerHandle *pTrackerHandle;
	const AutoPlacementSet *pAutoPlacmentSet = NULL;
	AutoPlacementVolume** eaAutoPlacementVolumes = NULL;

#define CLEAN_HANDLES()				RELEASE_TRACKER_HANDLE(s_queuedProcessData.pGroupTrackerStringHandle) \
									s_queuedProcessData.pAutoPlacementSet = NULL;\
									eaDestroyStruct(&eaAutoPlacementVolumes, parse_AutoPlacementVolume)\



	if (sAutoPlacerState != kAutoPlacerState_WAITINGFORSERVER)
	{
		// should never happen, 
		// the server sent data that we weren't ready for.
		printf( "Error: (Auto-Placement) received auto-placement data in a bad client state. Ignoring data.\n" );
		return;
	}
	
	sAutoPlacerState = kAutoPlacerState_NONE;

	// find the group def we are to operate on
	pGroupTracker = trackerFromHandleString(s_queuedProcessData.pGroupTrackerStringHandle);
	pTrackerHandle = trackerHandleFromTracker(pGroupTracker);
	if (! pGroupTracker || !pTrackerHandle || !pGroupTracker->def->property_structs.auto_placement_properties )
	{
		printf( "Error: (Auto-Placement) internal group tracker error.\n" );
		// spit out a warning that the operation failed due to a bad group tracker / group def
		CLEAN_HANDLES();
		return;
	}

	pAutoPlacmentSet = s_queuedProcessData.pAutoPlacementSet; 

	if (eaFind(&pGroupTracker->def->property_structs.auto_placement_properties->auto_place_set, pAutoPlacmentSet) == -1)
	{
		// the autoPlacementSet 
		printf( "Error: (Auto-Placement) AutoPlacement Set error.\n" );
		CLEAN_HANDLES();
		return;
	}

	if (eaSize(&pData->pPositionList) == 0)
	{
		printf( "\nWarning: (Auto-Placement) No positions generated. Check server output for any warnings or errors.\n" );
		CLEAN_HANDLES();
		return;
	}

	loadstart_printf("(Auto-Placement) Received auto-placement data. Creating %d objects...\n", eaSize(&pData->pPositionList) );

	apGetVolumeList(pGroupTracker, &eaAutoPlacementVolumes, true);
	
	{
		#define NAME_BUFFER_SIZE 256
		char szBaseNameBuffer[NAME_BUFFER_SIZE] = {0};
		char szOutputNameBuffer[NAME_BUFFER_SIZE] = {0};

		S32 i;
		Mat4 mtxTransform;
		TrackerHandle *pParentGroupTracker = NULL;
		
		RTGroupPlacementData **papRTAutoPlacementData = NULL;
		const char **ppLogicalGroupsList = NULL;
		
		identityMat4(mtxTransform);
		
		{
			eaSetCapacity(&papRTAutoPlacementData, eaSize(&pAutoPlacmentSet->auto_place_group));

			// setup the group name that is used for this set
			strcpy(szBaseNameBuffer, AUTO_PLACE_GROUP_BASENAME);
			if (pAutoPlacmentSet->set_name)
			{
				strcat_s(szBaseNameBuffer, NAME_BUFFER_SIZE, pAutoPlacmentSet->set_name);
			}

			// get the group we will be placing subgroups for the auto-placement
			for (i = 0; i < pGroupTracker->child_count; i++)
			{
				GroupTracker *pChild = pGroupTracker->children[i];
				if (!_strnicmp(pChild->def->name_str, szBaseNameBuffer, strlen(szBaseNameBuffer)))
				{
					pParentGroupTracker = trackerHandleFromTracker(pChild);
					break;
				}
			}
			if (!pParentGroupTracker)
			{
				printf("\nError: Failed to find the parent group tracker, %s, to populate with objects.", szBaseNameBuffer);
				CLEAN_HANDLES();
				return;
			}
						
			// create and initialize the RTGroupPlacementData list data
			for (i = 0; i < eaSize(&pAutoPlacmentSet->auto_place_group); i++)
			{
				AutoPlacementGroup *pAutoPlaceGroup = pAutoPlacmentSet->auto_place_group[i];

				RTGroupPlacementData *pGroupData = calloc(1, sizeof(RTGroupPlacementData));
				assert(pGroupData);

				// create the tracker group that all the objects for this group will be organized in
				pGroupData->target_group = groupdbCreate(pParentGroupTracker, 0, mtxTransform, 0, -1);
				assert(pGroupData->target_group);
				groupdbRename(pGroupData->target_group, pAutoPlaceGroup->group_name);

				eaPush(&papRTAutoPlacementData, pGroupData);
			}
		}
				
		
		

		// go through the positions we were given and create all the objects
		for (i = 0; i < eaSize(&pData->pPositionList); i++)
		{
			const AutoPlacePosition *pPosData = pData->pPositionList[i];

			const AutoPlacementObject *pAutoPlaceObject;
			const AutoPlacementGroup *pAutoPlaceGroup;
			RTGroupPlacementData *pRTGroupData;
			
			// get the group/object to create from the AutoPlacePosition
			{
				if (pPosData->groupIdx > eaSize(&pAutoPlacmentSet->auto_place_group))
				{
					continue;
				}

				ANALYSIS_ASSUME(pAutoPlacmentSet->auto_place_group);
				pAutoPlaceGroup = pAutoPlacmentSet->auto_place_group[pPosData->groupIdx];
				pRTGroupData = papRTAutoPlacementData[pPosData->groupIdx];
				assert(pAutoPlaceGroup);

				if (pPosData->objectIdx > eaSize(&pAutoPlaceGroup->auto_place_objects))
				{
					continue;
				}

				pAutoPlaceObject = eaGet(&pAutoPlaceGroup->auto_place_objects, pPosData->objectIdx);
				assert(pAutoPlaceObject);
			}

			// set up the position and orientation
			{
				TrackerHandle *pNewTracker;

				identityMat4(mtxTransform);
				
				if (pAutoPlaceObject->snap_to_slope)
				{
					// the object wants to have itself snapping to the slope
					snapToSlopeRandomWithFacing(mtxTransform, pPosData->vNormal);
				}
				else
				{
					// get a random yaw
					yawMat3(TWOPI * qufrand(), mtxTransform);	
				}
				
				// lower the object from our given position a slight bit in the opposite direction of the normal
				scaleAddVec3(pPosData->vNormal, -MAGIC_PENETRATION_DEPTH, pPosData->vPos, mtxTransform[3]);
				
				// create the object
				{
					U32 resourceId = pAutoPlaceObject->resource_id;
					U32 overrideId;
			
					if (apCheckForVolumeOverride(eaAutoPlacementVolumes, mtxTransform[3], resourceId, &overrideId))
					{
						resourceId = overrideId;
					}
					

					pNewTracker = groupdbCreate(pRTGroupData->target_group, resourceId, mtxTransform, 0, -1);
				}
				

				if (pNewTracker)
				{
					// if this object is in the logical tree, we need to create a unique name for it,
					// so we can later group it inside the logical tree
					GroupTracker *pNewGroupTracker = trackerFromTrackerHandle(pNewTracker);
					if (groupDefNeedsUniqueName(pNewGroupTracker->def))
					{
						char *pszUniqueName;
						GroupDef *pLayerDef = layerGetDef(pGroupTracker->parent_layer);
						assert(pLayerDef);
						
						// create the base name
						strcpy(szBaseNameBuffer, "AP_");
						strcat_s(szBaseNameBuffer, NAME_BUFFER_SIZE, pAutoPlaceObject->resource_name);
						// get a unique name given the base name
						groupDefScopeCreateUniqueName(pLayerDef, "", szBaseNameBuffer, 
												szOutputNameBuffer, NAME_BUFFER_SIZE, false);
						// set the new unique name for the logical object
						wleOpSetUniqueScopeName(NULL, pNewTracker, szOutputNameBuffer);
						// save the name for later when we're grouping these
						pszUniqueName = strdup(szOutputNameBuffer);
						eaPush(&pRTGroupData->papLogicalNamesList, pszUniqueName);
					}
				}
			}

			if ((i+1) % PRINT_UPDATE_FREQUENCY == 0)
			{
				loadupdate_printf("Placed %d objects...\n", i+1);
			}
		}


		
		// create the logical groups for the newly created objects
		for (i = 0; i < eaSize(&papRTAutoPlacementData); i++)
		{
			RTGroupPlacementData *pRTGroupData = papRTAutoPlacementData[i];
			AutoPlacementGroup* pAutoPlaceGroup = eaGet(&pAutoPlacmentSet->auto_place_group, i);
			assert(pAutoPlaceGroup);

			if (eaSize(&pRTGroupData->papLogicalNamesList) > 0)
			{
				GroupDef *pLayerDef = layerGetDef(pGroupTracker->parent_layer);
				assert(pLayerDef);
				
				// create the group name we will put these into
				// create a base name
				sprintf(szBaseNameBuffer, "AP_%s_%s_%s", 
							pGroupTracker->def->name_str, pAutoPlacmentSet->set_name, pAutoPlaceGroup->group_name);
				
				groupDefScopeCreateUniqueName(pLayerDef, "", 
								szBaseNameBuffer, szOutputNameBuffer, NAME_BUFFER_SIZE, false);

				// the parent name of the child group
				wleOpLogicalGroup(NULL, szOutputNameBuffer, pRTGroupData->papLogicalNamesList, false);
			}
			
		}


		// refresh the groupTracker UI since the changes have been made
		wleUITrackerTreeRefresh(NULL);

		eaDestroy(&ppLogicalGroupsList);
		eaDestroyStruct(&papRTAutoPlacementData, parse_RTGroupPlacementData);

		loadend_printf(" done.");
	}


	CLEAN_HANDLES();


	processQueuedData();
}

// -------------------------------------------------------------------------------------
static void processQueuedData()
{
	// check if we have any queued placements
	if (eaSize(&s_eaVolumeProcessQueue))
	{
		int i;

		// go through the queue until we find a valid GroupTracker 
		// (should be the first, but just incase something got deleted)
		for (i = 0; i < eaSize(&s_eaVolumeProcessQueue); i++)
		{
			AutoPlaceQueuedData *data = eaRemove(&s_eaVolumeProcessQueue, 0);
			GroupTracker *pGroupTracker = trackerFromHandleString(data->pGroupTrackerStringHandle);
			TrackerHandle *pHandle = trackerHandleFromTracker(pGroupTracker);

			if (! pHandle)
			{
				StructDestroySafe(parse_AutoPlaceQueuedData, &data);
				continue;
			}

			performAutoPlacement(pHandle, data->pAutoPlacementSet);
			StructDestroySafe(parse_AutoPlaceQueuedData, &data);
			break;
		}


		if (eaSize(&s_eaVolumeProcessQueue) == 0)
		{
			printf("\n(Auto-Placement) * Notice: Last queued auto-placement object sent for processing.\n\n");

			eaDestroy(&s_eaVolumeProcessQueue);
		}
	}
}


// -------------------------------------------------------------------------------------
__forceinline static bool compareGroupQueuedData(const AutoPlaceQueuedData *data, const GroupTracker *tracker, const AutoPlacementSet *set)
{
	GroupTracker *pOtherGroup = trackerFromHandleString(data->pGroupTrackerStringHandle);
	if (pOtherGroup == tracker)
	{
		// this groupTracker is already on the list to be processed
		// check if the set is already on here
		return (data->pAutoPlacementSet == set);
	}

	return false;
}

// -------------------------------------------------------------------------------------
static void addAutoPlacementToQueue(GroupTracker *pHandle)
{
	int x;

	if (!pHandle || !pHandle->def || !pHandle->def->property_structs.auto_placement_properties)
	{
		return;
	}

	// check each set
	for (x = 0; x < eaSize(&pHandle->def->property_structs.auto_placement_properties->auto_place_set); x++)
	{
		AutoPlacementSet *pSet = pHandle->def->property_structs.auto_placement_properties->auto_place_set[x];
		int i;
		bool dupe = false;

		for (i = 0; i < eaSize(&s_eaVolumeProcessQueue); i++)
		{
			const AutoPlaceQueuedData *data = s_eaVolumeProcessQueue[i];
			if (compareGroupQueuedData(data, pHandle, pSet))
			{
				dupe = true;
				break;
			}
		}

		if (!dupe && s_queuedProcessData.pGroupTrackerStringHandle)
		{
			dupe = compareGroupQueuedData(&s_queuedProcessData, pHandle, pSet);
		}

		if (dupe == false)
		{
			AutoPlaceQueuedData *data;
			
			data = StructCreate(parse_AutoPlaceQueuedData);
			data->pGroupTrackerStringHandle = handleStringFromTracker(pHandle);
			data->pAutoPlacementSet = pSet;

			eaPush(&s_eaVolumeProcessQueue, data);
		}
	}
	
	
}


// -------------------------------------------------------------------------------------
static int getAutoPlacementTreeTraverserCallback(void *user_data, TrackerTreeTraverserDrawParams *params)
{
	if (params->tracker && params->tracker->def && params->tracker->def->property_structs.auto_placement_properties)
	{
		addAutoPlacementToQueue(params->tracker);
		return false;
	}

	return true;
}

// -------------------------------------------------------------------------------------
AUTO_COMMAND;
void autoPlacementRegenerateAllSets()
{
	S32 i;

	// printf("(Auto-placement) Queuing all auto-placement objects on open layers for regeneration.\n" );

	// traverse all group trees to find objects in the marquee
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (layer)
			trackerTreeTraverse(layer, layerGetTracker(layer), getAutoPlacementTreeTraverserCallback, NULL);
	}

	if (eaSize(&s_eaVolumeProcessQueue) == 0)
	{
		printf("(Auto-placement) Warning: Found no auto-placement objects to queue.\n" );
	}
	else
	{
		printf("(Auto-placement) Queued %d auto-placement objects for regeneration.\n", eaSize(&s_eaVolumeProcessQueue) );
	}

	processQueuedData();
}

#endif


#include "autoPlacement_c_ast.c"
