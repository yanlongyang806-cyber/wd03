/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "enclayereditor.h"
#include "Quat.h"
#include "encountereditor.h"
#include "FSMEditorMain.h"
#include "oldencounter_common.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#endif

AUTO_COMMAND ACMD_NAME("EncounterEditor.PlaceRandomCritter") ;
void EDEPlaceRandomCritter(void)
{
#ifndef NO_EDITORS
	EncounterEditDoc* encDoc = (EncounterEditDoc*)GEGetActiveEditorDocEM("EncounterDef");
	GEPlacementTool* placementTool = &encDoc->placementTool;
	if (encDoc && !GEPlacementToolIsInPlacementMode(placementTool))
	{
		// Reset old placement fields
		GEPlacementToolReset(placementTool, encDoc->encCenter[3], zerovec3);
		REMOVE_HANDLE(encDoc->newCritter);
		placementTool->useGizmos = 0;

		encDoc->typeToCreate = GEObjectType_Actor;
		placementTool->createNew = 1;
	}
#endif
}

/*
This is a hack to run TryCleanupActors on all encounter defs.  We've had to do this twice, so here's the
code in case we need to do it again.
amuCheckoutFileFake is a hacked version of amuCheckoutFile that takes a char* instead of a AMFile.
You'll need to include the asset manager utility file.
Needless to say, this code shouldn't be checked in uncommented.
AUTO_COMMAND;
void cleanupEncDefs(void)
{
	int i, n;
	// BF - g_EncounterDefList doesn't exist anymore, you'll have you iterate through the dictionary
	n = eaSize(&g_EncounterDefList.encounterDefs);
	for(i=0; i<n; i++)
	{
		EncounterDef* encDef = g_EncounterDefList.encounterDefs[i];

		if (amuCheckoutFileFake(encDef->filename))
		{
			oldencounter_TryCleanupActors(NULL, &encDef->actors);
			GESaveEncounterInternal(encDef, NULL);
		}
		else
			ErrorFilenamef(encDef->filename, "Couldn't checkout file %s.", encDef->filename);
	}
}
*/

#ifndef NO_EDITORS
static EditDocDefinition* EDEGetDocDefinition(GEEditorDocPtr doc)
{
	if (!doc) return NULL;
	return ((GenericMissionEditDoc*)doc)->docDefinition;
}
#endif

// JAMES TODO: all of these commands used to be on the mission editor command list, but that's not as well
// supported under EM.  Revisit at some point.
// Paste currently selected objects
AUTO_COMMAND ACMD_NAME("MissionEditor.Paste");
void CmdMissionPaste(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->PasteCB) return;
	def->PasteCB(editorDoc);
#endif
}

// Cut all selected objects
AUTO_COMMAND ACMD_NAME("MissionEditor.Cut");
void CmdMissionCut(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->CutCopyCB) return;
	def->CutCopyCB(editorDoc, false, true, zerovec3, zerovec3);
#endif
}

// Copy all selected objects
AUTO_COMMAND ACMD_NAME("MissionEditor.Copy");
void CmdMissionCopy(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->CutCopyCB) return;
	def->CutCopyCB(editorDoc, true, true, zerovec3, zerovec3);
#endif
}

// Cancel current operation, or if no operation, unselects the last selected object
AUTO_COMMAND ACMD_NAME("MissionEditor.Cancel") ;
void CmdMissionCancel(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->CancelCB) return;
	def->CancelCB(editorDoc);
#endif
}

// Deletes all selected objects
AUTO_COMMAND ACMD_NAME("MissionEditor.Delete") ;
void CmdMissionDelete(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->DeleteCB) return;
	def->DeleteCB(editorDoc);
#endif
}

// Moves the center/origin point of the encounter
AUTO_COMMAND ACMD_NAME("MissionEditor.MoveOrigin") ;
void CmdMissionMoveOrigin(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->MoveOriginCB) return;
	def->MoveOriginCB(editorDoc);
#endif
}

// Moves the spawnlocation for the map
// TODO: Upgrade this to work for all spawn locations in the future
AUTO_COMMAND ACMD_NAME("MissionEditor.MoveSpawnLoc") ;
void CmdMissionMoveSpawnLoc(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->MoveSpawnLocCB) return;
	def->MoveSpawnLocCB(editorDoc);
#endif
}

// Groups the selected objects
AUTO_COMMAND ACMD_NAME("MissionEditor.Group") ;
void CmdMissionGroup(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->GroupCB) return;
	def->GroupCB(editorDoc);
#endif
}

// Ungroups the selected objects
AUTO_COMMAND ACMD_NAME("MissionEditor.Ungroup") ;
void CmdMissionUngroup(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->UngroupCB) return;
	def->UngroupCB(editorDoc);
#endif
}

// Switches to volume editing mode
AUTO_COMMAND ACMD_NAME("MissionEditor.Freeze") ;
void CmdMissionFreeze(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->FreezeCB) return;
	def->FreezeCB(editorDoc);
#endif
}

// Switches to volume editing mode
AUTO_COMMAND ACMD_NAME("MissionEditor.Unfreeze") ;
void CmdMissionUnfreeze(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->UnfreezeCB) return;
	def->UnfreezeCB(editorDoc);
#endif
}

// Create a new object
AUTO_COMMAND ACMD_NAME("MissionEditor.CreateNew") ;
void CmdMissionCreateNew(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->PlaceObjectCB) return;
	def->PlaceObjectCB(editorDoc, "New", "NewFromKeyBind");
#endif
}

// Center the camera around selected objects, if nothing selected, reset camera pivot
AUTO_COMMAND ACMD_NAME("MissionEditor.Focus") ;
void CmdMissionFocusCam(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->CenterCamCB) return;
	def->CenterCamCB(editorDoc);
#endif
}

// Switches between rotate and translate gizmos when using widgets
// Holding down switches to quickrotate when in quickplace mode
AUTO_COMMAND ACMD_NAME("MissionEditor.ToggleGizmo") ;
void CmdMissionToggleGizmo(int keyDown)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->GetGizmoCB) return;
	EDEPlacementToggleGizmo(def->GetGizmoCB(editorDoc), keyDown);
#endif
}

AUTO_COMMAND ACMD_NAME("MissionEditor.ToggleCopy") ;
void CmdMissionToggleCopy(int keyDown)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->GetGizmoCB) return;
	EDEPlacementToggleCopy(def->GetGizmoCB(editorDoc), keyDown);
#endif
}

AUTO_COMMAND ACMD_NAME("MissionEditor.LeftClick") ;
void CmdMissionLeftClick(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->LeftClickCB) return;
	def->LeftClickCB(editorDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("MissionEditor.RightClick") ;
void CmdMissionRightClick(void)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->RightClickCB) return;
	def->RightClickCB(editorDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("MissionEditor.LeftDrag") ;
void CmdMissionLeftDrag(int startDrag)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->GetGizmoCB) return;
	EDEInputLeftDrag(editorDoc, def->GetGizmoCB(editorDoc), startDrag, def->QuickPlaceCB);
#endif
}

AUTO_COMMAND ACMD_NAME("MissionEditor.Additive") ;
void CmdMissionShift(int keyDown)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->GetGizmoCB) return;
	EDEPlacementAdditive(def->GetGizmoCB(editorDoc), keyDown);
#endif
}

AUTO_COMMAND ACMD_NAME("MissionEditor.Bore") ;
void CmdMissionAlt(int keyDown)
{
#ifndef NO_EDITORS
	GenericMissionEditDoc* editorDoc = GEGetActiveEditorDocEM(NULL);
	EditDocDefinition* def = EDEGetDocDefinition(editorDoc);
	if (!editorDoc || !def || !def->GetGizmoCB) return;
	EDEPlacementBore(def->GetGizmoCB(editorDoc), keyDown);
#endif
}


// ----------------------------------------------------
//  Fixup code to rotate an encounter layer around a point
// ----------------------------------------------------

#ifndef NO_EDITORS

static void fixupRotateMat(Mat4 inOut, Vec3 centerPoint)
{
	Mat4 tempLoc;
	F32 origY;
	Vec3 rotation = {0, PI, 0};

	copyMat4(inOut, tempLoc);

	// Rotate the object around its own "up" axis (which is probably the Y axis)
	rotateMat3(rotation, tempLoc);

	// Rotate the object's location around the actual Y axis
	// Note: this rotation won't work properly if the up vector of the thing being rotated isn't the Y axis (it'll 
	origY = tempLoc[3][1];
	subVec3(tempLoc[3], centerPoint, tempLoc[3]);
	subVec3(centerPoint, tempLoc[3], tempLoc[3]);
	tempLoc[3][1] = origY;

	copyMat4(tempLoc, inOut);

}

static void RotateEncounterGroup(OldStaticEncounterGroup* group, Vec3 centerPoint)
{
	int i, n;

	n = eaSize(&group->staticEncList);
	for(i=0; i<n; i++)
	{
		OldStaticEncounter* enc = group->staticEncList[i];
		Mat4 tempMat;
		quatToMat(enc->encRot, tempMat);
		copyVec3(enc->encPos, tempMat[3]);
		fixupRotateMat(tempMat, centerPoint);
		mat3ToQuat(tempMat, enc->encRot);
		copyVec3(tempMat[3], enc->encPos);
	}
	n = eaSize(&group->childList);
	for(i=0; i<n; i++)
	{
		RotateEncounterGroup(group->childList[i], centerPoint);
	}
}
#endif


// Rotate an encounter layer in X/Y plane
AUTO_COMMAND;
void RotateEncounterLayer(Vec3 centerPoint)
{
#ifndef NO_EDITORS
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) GEGetActiveEditorDocEM("encounterlayer");
	EncounterLayer* encLayer = encLayerDoc ? encLayerDoc->layerDef : NULL;

	if(!encLayer)
	{
		Errorf("Rotate Encounter Layer can only be run from inside the encounter layer editor");
		return;
	}

	printf("Rotating layer around %f %f %f\n", centerPoint[0], centerPoint[1], centerPoint[2]);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	{
		int i, n;
		n = eaSize(&encLayer->oldNamedRoutes);
		for(i=0; i<n; i++)
		{
			int j,k;
			OldPatrolRoute* route = encLayer->oldNamedRoutes[i];
			Mat4 tempMat;

			k = eaSize(&route->patrolPoints);
			for(j=0; j<k; j++)
			{
				OldPatrolPoint* point = route->patrolPoints[j];
				quatToMat(point->pointRot, tempMat);
				copyVec3(point->pointLoc, tempMat[3]);
				fixupRotateMat(tempMat, centerPoint);
				mat3ToQuat(tempMat, point->pointRot);
				copyVec3(tempMat[3], point->pointLoc);
			}
		}
	}
#endif

	RotateEncounterGroup(&encLayer->rootGroup, centerPoint);
#endif
}
