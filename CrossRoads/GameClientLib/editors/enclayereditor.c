/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "contact_common.h"
#include "crypt.h"
#include "EditLib.h"
#include "EditLibGizmos.h"
#include "enclayereditor.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "eventeditor.h"
#include "Expression.h"
#include "GfxSpriteText.h"
#include "GfxPrimitive.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "partition_enums.h"
#include "quat.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "wlbeacon.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "bounds.h"

#include "AutoGen/oldencounter_common_h_ast.h"
#include "Autogen/encounter_enums_h_ast.h"
#include "Autogen/StateMachine_h_ast.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h" // for ServerCmd_EventLogSetFilter

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// ----------------------------------------------------------------------------------
// Typedefs and forward function definitions
// ----------------------------------------------------------------------------------

typedef struct WorldInteractionEntry WorldInteractionEntry;

void ELEConvertWorldMatToActorMatInEncounter(OldStaticEncounter* staticEnc, Mat4 actorWorldMat, Mat4 newActorMat);
void ELERefreshEncounterTree(EncounterLayerEditDoc* encLayerDoc);
static GEObjectType ELEFindObjectUnderMouse(EncounterLayerEditDoc *encLayerDoc, EncounterLayer **foundInLayer,
											Vec3 selPos, Vec3 selOffset,
											int *groupIdx, int *objIdx, WorldInteractionEntry **entry,
											GEObjectType *foundObjType, bool checkAllLayers, bool clickablesOnly);

static void ELEGetSelectedIndexListGeneric(int **indexList, EncounterLayerEditDoc* encLayerDoc, GEObjectType objectType);


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

EMEditor s_EncounterLayerEditor = {0};

extern bool g_InClickableAttachMode;
EventEditor* s_EncounterLayerEventDebugEditor = NULL;

extern EditDocDefinition ELEDocDefinition;

U32 g_DisableQuickPlace = 0;


// ----------------------------------------------------------------------------------
// ELE Actors
// ----------------------------------------------------------------------------------

#define ENCOUNTER_CHECK_HEIGHT_DIST 20

static void ELEStoreStaticEncounterHeights(SA_PARAM_NN_VALID EncounterLayer* encLayer)
{
	// Save info for each static encounter that will let us flag it if the terrain changes under it
	int i, numEncounters = eaSize(&encLayer->staticEncounters);
	for (i = 0; i < numEncounters; i++)
	{
		OldStaticEncounter *staticEnc = encLayer->staticEncounters[i];
		S32 foundFloor = false;
		F32 distToGround = oldencounter_GetStaticEncounterHeight(PARTITION_CLIENT, staticEnc, &foundFloor);

		// If the floor wasn't found, it's most likely because geometry wasn't loaded for that region
		if( foundFloor )
			staticEnc->distToGround = distToGround;
		staticEnc->bDistToGroundChanged = false;
	}
}

static OldActor** ELEGetSelectedActorList(EncounterLayerEditDoc* encLayerDoc, int whichEnc)
{
	static OldActor** actorList = NULL;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	eaSetSize(&actorList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if ((selObject->selType == GEObjectType_Actor) && 
			((whichEnc == -1) || (selObject->groupIndex == whichEnc)) &&
			(selObject->groupIndex < eaSize(&encLayer->staticEncounters)))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[selObject->groupIndex];
			if (selObject->objIndex < eaSize(&staticEnc->spawnRule->actors))
			{
				OldActor* actor = staticEnc->spawnRule->actors[selObject->objIndex];
				eaPush(&actorList, actor);
			}
		}
	}
	return actorList;
}
static OldNamedPointInEncounter** ELEGetSelectedEncPointList(EncounterLayerEditDoc* encLayerDoc, int whichEnc)
{
	static OldNamedPointInEncounter** pointList = NULL;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	eaSetSize(&pointList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if ((selObject->selType == GEObjectType_Point) && 
			(whichEnc == -1 || selObject->groupIndex == whichEnc) &&
			(selObject->objIndex != -1) &&
			(selObject->groupIndex < eaSize(&encLayer->staticEncounters)))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[selObject->groupIndex];
			if (selObject->objIndex < eaSize(&staticEnc->spawnRule->namedPoints))
			{
				OldNamedPointInEncounter* point = staticEnc->spawnRule->namedPoints[selObject->objIndex];
				eaPush(&pointList, point);
			}
		}
	}
	return pointList;
}

OldStaticEncounter* ELEGetLastSelectedEncounter(EncounterLayerEditDoc* encLayerDoc)
{
	OldStaticEncounter* encounter = NULL;
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if ((selObject->selType == GEObjectType_Encounter) &&
			(selObject->groupIndex < eaSize(&encLayerDoc->layerDef->staticEncounters)))
		{
			encounter = encLayerDoc->layerDef->staticEncounters[selObject->groupIndex];
		}
	}
	return encounter;
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static int* ELEGetSelectedPatrolPointIndexList(EncounterLayerEditDoc* encLayerDoc, int whichRoute)
{
	static int* pointIndexList = NULL;
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	eaiSetSize(&pointIndexList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if ((selObject->selType == GEObjectType_PatrolPoint) && 
			(selObject->groupIndex == whichRoute))
			eaiPush(&pointIndexList, selObject->objIndex);
	}
	return pointIndexList;
}

static OldPatrolPoint** ELEGetSelectedPatrolPointList(EncounterLayerEditDoc* encLayerDoc, int whichRoute)
{
	static OldPatrolPoint** patrolPointList = NULL;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	eaSetSize(&patrolPointList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if ((selObject->selType == GEObjectType_PatrolPoint) && 
			((whichRoute == -1) || (selObject->groupIndex == whichRoute)) &&
			(selObject->groupIndex < eaSize(&encLayer->oldNamedRoutes)))
		{
			OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[selObject->groupIndex];
			if (selObject->objIndex < eaSize(&patrolRoute->patrolPoints))
			{
				OldPatrolPoint* patrolPoint = patrolRoute->patrolPoints[selObject->objIndex];
				eaPush(&patrolPointList, patrolPoint);
			}
		}
	}
	return patrolPointList;
}

static OldPatrolRoute** ELEGetSelectedPatrolRouteList(EncounterLayerEditDoc* encLayerDoc)
{
	static OldPatrolRoute** patrolRouteList = NULL;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	eaSetSize(&patrolRouteList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if ((selObject->selType == GEObjectType_PatrolRoute) &&
			(selObject->groupIndex < eaSize(&encLayer->oldNamedRoutes)))
		{
			OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[selObject->groupIndex];
			eaPush(&patrolRouteList, patrolRoute);
		}
	}
	return patrolRouteList;
}

static OldPatrolRoute* ELEPatrolRouteFromSelection(EncounterLayerEditDoc* encLayerDoc, int groupIdx, int objIdx)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	OldPatrolRoute* route = NULL;
	int n = eaSize(&encLayer->oldNamedRoutes);

	// In the layer
	if (groupIdx >= 0 && groupIdx < n)
		route = encLayer->oldNamedRoutes[groupIdx];

	return route;
}
#endif

static int* ELEGetSelectedEncounterActorIndexList(EncounterLayerEditDoc* encLayerDoc, int whichEnc)
{
	static int* actorIndexList = NULL;
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	eaiSetSize(&actorIndexList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if ((selObject->selType == GEObjectType_Actor) && 
			(selObject->groupIndex == whichEnc))
			eaiPush(&actorIndexList, selObject->objIndex);
	}
	return actorIndexList;
}
static int* ELEGetSelectedEncounterPointIndexList(EncounterLayerEditDoc* encLayerDoc, int whichEnc)
{
	static int* pointIndexList = NULL;
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	eaiSetSize(&pointIndexList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if ((selObject->selType == GEObjectType_Point) && 
			(selObject->groupIndex == whichEnc) && 
			(selObject->objIndex != -1))
			eaiPush(&pointIndexList, selObject->objIndex);
	}
	return pointIndexList;
}

static int** ELEGetFullSelectedActorIndexList(EncounterLayerEditDoc* encLayerDoc)
{
	static int** actorIndexList = NULL;
	int numEncs = eaSize(&encLayerDoc->layerDef->staticEncounters);
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	for (i = eaSize(&actorIndexList) - 1; i >= 0; i--)
		eaiDestroy(&actorIndexList[i]);
	eaSetSize(&actorIndexList, numEncs);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if (selObject->selType == GEObjectType_Actor)
			eaiPush(&actorIndexList[selObject->groupIndex], selObject->objIndex);
	}
	return actorIndexList;
}

// Get all named points inside encounters
static int** ELEGetFullSelectedEncounterPointIndexList(EncounterLayerEditDoc* encLayerDoc)
{
	static int** pointIndexList = NULL;
	int numEncs = eaSize(&encLayerDoc->layerDef->staticEncounters);
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	for (i = eaSize(&pointIndexList) - 1; i >= 0; i--)
		eaiDestroy(&pointIndexList[i]);
	eaSetSize(&pointIndexList, numEncs);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if (selObject->selType == GEObjectType_Point && selObject->objIndex != -1)
			eaiPush(&pointIndexList[selObject->groupIndex], selObject->objIndex);
	}
	return pointIndexList;
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static int** ELEGetFullSelectedPatrolIndexList(EncounterLayerEditDoc* encLayerDoc)
{
	static int** patrolIndexList = NULL;
	int numEncs = eaSize(&encLayerDoc->layerDef->oldNamedRoutes);
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	for (i = eaSize(&patrolIndexList) - 1; i >= 0; i--)
		eaiDestroy(&patrolIndexList[i]);
	eaSetSize(&patrolIndexList, numEncs);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if (selObject->selType == GEObjectType_PatrolPoint)
			eaiPush(&patrolIndexList[selObject->groupIndex], selObject->objIndex);
	}
	return patrolIndexList;
}
#endif

// Generic function to get index list for objects whose group index is their full index
static void ELEGetSelectedIndexListGeneric(int **indexList, EncounterLayerEditDoc* encLayerDoc, GEObjectType objectType)
{
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	eaiSetSize(indexList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if (selObject->selType == objectType)
		{
			eaiPush(indexList, selObject->groupIndex);
		}
	}
}
int* ELEGetSelectedEncounterIndexList(EncounterLayerEditDoc* encLayerDoc)
{
	static int* indexList = NULL;
	ELEGetSelectedIndexListGeneric(&indexList, encLayerDoc, GEObjectType_Encounter);
	return indexList;
}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static int* ELEGetSelectedPatrolRouteIndexList(EncounterLayerEditDoc* encLayerDoc)
{
	static int* indexList = NULL;
	ELEGetSelectedIndexListGeneric(&indexList, encLayerDoc, GEObjectType_PatrolRoute);
	return indexList;
}
#endif
static int* ELEGetSelectedNamedPointIndexList(EncounterLayerEditDoc* encLayerDoc)
{
	static int* indexList = NULL;
	int i, numSelected = eaSize(&encLayerDoc->selectedObjects);
	eaiSetSize(&indexList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
		if (selObject->selType == GEObjectType_Point && selObject->objIndex == -1)
		{
			eaiPush(&indexList, selObject->groupIndex);
		}
	}

	return indexList;
}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static void ELERefreshPatrolRouteUI(EncounterLayerEditDoc* encLayerDoc)
{
	OldPatrolRoute** selRouteList = ELEGetSelectedPatrolRouteList(encLayerDoc);
	int i, numSelected = eaSize(&selRouteList);
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	if (numSelected == 1)
	{
		OldPatrolRoute* patrolRoute = selRouteList[0];
		ui_EditableSetText(encLayerDoc->uiInfo.patrolNameEntry, patrolRoute->routeName);
		ui_ComboBoxSetSelectedEnum(encLayerDoc->uiInfo.patrolTypeCombo, patrolRoute->routeType);
	}
	else if (numSelected)
	{
		OldPatrolRouteType commonType = selRouteList[0]->routeType;
		ui_EditableSetText(encLayerDoc->uiInfo.patrolNameEntry, "");
		for (i = 0; i < numSelected; i++)
			if (commonType != selRouteList[i]->routeType)
				commonType = -1;
		ui_ComboBoxSetSelectedEnum(encLayerDoc->uiInfo.patrolTypeCombo, commonType);
	}
	else
	{
		ui_EditableSetText(encLayerDoc->uiInfo.patrolNameEntry, "");
		ui_ComboBoxSetSelectedEnum(encLayerDoc->uiInfo.patrolTypeCombo, -1);
	}

	if (numSelected && !encLayerDoc->uiInfo.patrolExpander->group)
		ui_ExpanderGroupInsertExpander(encLayerDoc->uiInfo.propExpGroup, encLayerDoc->uiInfo.patrolExpander, 0);
	else if (!numSelected && encLayerDoc->uiInfo.patrolExpander->group)
		ui_ExpanderGroupRemoveExpander(encLayerDoc->uiInfo.propExpGroup, encLayerDoc->uiInfo.patrolExpander);
}
#endif

static void ELERefreshActorPropUI(EncounterLayerEditDoc* encLayerDoc, bool fullRefresh)
{
	OldActor** actorList = ELEGetSelectedActorList(encLayerDoc, -1);
	ActorPropUI* actorUI = encLayerDoc->uiInfo.actorUI;
	GEActorPropUIRefresh(actorUI, &actorList, fullRefresh);
	if (eaSize(&actorList) && !actorUI->actorPropExpander->group)
		ui_ExpanderGroupInsertExpander(encLayerDoc->uiInfo.propExpGroup, actorUI->actorPropExpander, 0);
	else if (!eaSize(&actorList) && actorUI->actorPropExpander->group)
		ui_ExpanderGroupRemoveExpander(encLayerDoc->uiInfo.propExpGroup, actorUI->actorPropExpander);
}

static void ELERefreshEncounterPropUI(EncounterLayerEditDoc* encLayerDoc, bool fullRefresh)
{
	EncounterDef** encDefList = NULL;
	OldStaticEncounter** staticEncList = NULL;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	EncounterPropUI* encounterUI = encLayerDoc->uiInfo.encounterUI;
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	int i, numSelEncs = eaiSize(&selEncList);
	for (i = 0; i < numSelEncs; i++)
	{
		if (encLayer->staticEncounters && (selEncList[i] < eaSize(&encLayer->staticEncounters)))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[selEncList[i]];
			EncounterDef* baseDef = GET_REF(staticEnc->baseDef); 
			EncounterDef* defOverride = staticEnc->defOverride;

			// If the def has been instanced use that, otherwise the basedef, and if no basedef, use the override
			// TODO: If there is no basedef and no override (basedef deleted), how do we recover?
			//       This should work in the meantime, but there may be many places we don't handle that
			if (defOverride && (defOverride->name || !baseDef))	
			{
				eaPush(&encDefList, defOverride);
				eaPush(&staticEncList, staticEnc);
			}
			else if (baseDef)
			{
				eaPush(&encDefList, baseDef);
				eaPush(&staticEncList, staticEnc);
			}
		}
	}

	GEEncounterPropUIRefresh(encounterUI, &encDefList, &staticEncList, encLayerDoc->layerDef, fullRefresh);

	if (eaSize(&encDefList) && !encounterUI->encPropExpander->group)
		ui_ExpanderGroupInsertExpander(encLayerDoc->uiInfo.propExpGroup, encounterUI->encPropExpander, 0);
	else if (!eaSize(&encDefList) && encounterUI->encPropExpander->group)
		ui_ExpanderGroupRemoveExpander(encLayerDoc->uiInfo.propExpGroup, encounterUI->encPropExpander);

	eaDestroy(&encDefList);
	eaDestroy(&staticEncList);
}

void ELERefreshUI(EncounterLayerEditDoc* encLayerDoc)
{
	ELERefreshEncounterTree(encLayerDoc);
	GERefreshMapNamesList();

	// Expanders will appear in reverse order of this list
	ELERefreshActorPropUI(encLayerDoc, true);
	ELERefreshEncounterPropUI(encLayerDoc, true);
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	if (gConf.bAllowOldPatrolData)
	{
		ELERefreshPatrolRouteUI(encLayerDoc);
	}
#endif
}

#define ELESelectObjectGeneric(objectType, whichObject) GESelectObject(&encLayerDoc->selectedObjects, objectType, NULL, whichObject, -1, additive)

static void ELESelectEncounter(EncounterLayerEditDoc* encLayerDoc, int whichEncounter, bool additive)
{
	ELESelectObjectGeneric(GEObjectType_Encounter, whichEncounter);
}

static void ELESelectActor(EncounterLayerEditDoc* encLayerDoc, int whichEncounter, int whichActor, bool additive)
{
	if (whichEncounter != -1)
	{
		GESelectObject(&encLayerDoc->selectedObjects, GEObjectType_Actor, NULL, whichEncounter, whichActor, additive);
	}
}
static void ELESelectEncPoint(EncounterLayerEditDoc* encLayerDoc, int whichEncounter, int whichPoint, bool additive)
{
	if (whichEncounter != -1)
	{
		GESelectObject(&encLayerDoc->selectedObjects, GEObjectType_Point, NULL, whichEncounter, whichPoint, additive);
	}
}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static void ELESelectPatrolPoint(EncounterLayerEditDoc* encLayerDoc, int routeNum, int pointNum, bool additive)
{
	GESelectObject(&encLayerDoc->selectedObjects, GEObjectType_PatrolPoint, NULL, routeNum, pointNum, additive);
}
static void ELESelectPatrolRoute(EncounterLayerEditDoc* encLayerDoc, int routeNum, bool additive)
{
	ELESelectObjectGeneric(GEObjectType_PatrolRoute, routeNum);
}
#endif
static void ELESelectNamedPoint(EncounterLayerEditDoc* encLayerDoc, int whichPoint, bool additive)
{
	ELESelectObjectGeneric(GEObjectType_Point, whichPoint);
}

static GESelectedObject* ELEFindSelectedActorObject(EncounterLayerEditDoc* encLayerDoc, int whichEncounter, int whichActor)
{
	return GESelectedObjectFind(&encLayerDoc->selectedObjects, GEObjectType_Actor, NULL, whichEncounter, whichActor);
}

static GESelectedObject* ELEFindSelectedEncPointObject(EncounterLayerEditDoc* encLayerDoc, int whichEncounter, int whichPoint)
{
	return GESelectedObjectFind(&encLayerDoc->selectedObjects, GEObjectType_Point, NULL, whichEncounter, whichPoint);
}

static GESelectedObject* ELEFindSelectedEncounterObject(EncounterLayerEditDoc* encLayerDoc, int whichEncounter)
{
	return GESelectedObjectFind(&encLayerDoc->selectedObjects, GEObjectType_Encounter, NULL, whichEncounter, -1);
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static GESelectedObject* ELEFindSelectedPatrolPointObject(EncounterLayerEditDoc* encLayerDoc, int whichRoute, int whichPoint)
{
	return GESelectedObjectFind(&encLayerDoc->selectedObjects, GEObjectType_PatrolPoint, NULL, whichRoute, whichPoint);
}

static GESelectedObject* ELEFindSelectedPatrolRouteObject(EncounterLayerEditDoc* encLayerDoc, int whichRoute)
{
	return GESelectedObjectFind(&encLayerDoc->selectedObjects, GEObjectType_PatrolRoute, NULL, whichRoute, -1);
}
#endif

static GESelectedObject* ELEFindSelectedNamedPoint(EncounterLayerEditDoc* encLayerDoc, int whichPoint)
{
	return GESelectedObjectFind(&encLayerDoc->selectedObjects, GEObjectType_Point, NULL, whichPoint, -1);
}

// Creates a list of all parents to this group
static void ELECreateParentGroupList(OldStaticEncounterGroup* staticEncGroup, OldStaticEncounterGroup*** retListPtr)
{
	OldStaticEncounterGroup* currParent = staticEncGroup;
	while (currParent)
	{
		eaPush(retListPtr, currParent);
		currParent = currParent->parentGroup;
	}
}

void ELERefreshStaticEncounters(EncounterLayerEditDoc* encLayerDoc, bool fullRefresh)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	int i, n = eaSize(&encLayer->staticEncounters);
	for (i = 0; i < n; i++)
	{
		int* selActorList = ELEGetSelectedEncounterActorIndexList(encLayerDoc, i);
		bool groupIsSelected = (eaiFind(&selEncList, i) != -1);
		bool groupHasActorSelected = (eaiSize(&selActorList));
		bool cleanupChanged = oldencounter_TryCleanupEncDefs(encLayer->staticEncounters[i]);

		if (fullRefresh || groupIsSelected || groupHasActorSelected || cleanupChanged)
			oldencounter_UpdateStaticEncounterSpawnRule(encLayer->staticEncounters[i], encLayer);
	}
	ELERefreshActorPropUI(encLayerDoc, fullRefresh);
	ELERefreshEncounterPropUI(encLayerDoc, fullRefresh);
	ELERefreshEncounterTree(encLayerDoc);
}

static char* ELECreateUniqueStaticEncName(const char* desiredName)
{
	static char nextName[GE_NAMELENGTH_MAX];
	int counter = 1;
	strcpy(nextName, desiredName);
	while (oldencounter_StaticEncounterFromName(nextName))
	{
		sprintf(nextName, "%s%i", desiredName, counter);
		counter++;
	}
	return nextName;
}

void ele_ChangeStaticEncounterName(EncounterLayer* encLayer, int* whichEncounters, char* desiredName)
{
	int i, n = eaiSize(&whichEncounters);
	for (i = 0; i < n; i++)
	{
		int whichEnc = whichEncounters[i];
		if (whichEnc >= 0 && whichEnc < eaSize(&encLayer->staticEncounters))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
			if (stricmp(staticEnc->name, desiredName))
			{
				oldencounter_RemoveStaticEncounterReference(staticEnc);
				staticEnc->name = allocAddString(ELECreateUniqueStaticEncName(desiredName));
				oldencounter_AddStaticEncounterReference(staticEnc);
			}
		}
	}
}

static void ELEInstanceAndApplyChangeToStaticEncounters(EncounterLayer* encLayer, int* whichEncounters, EncounterChangeFunc changeFunc, void* changeData)
{
	int i, n = eaiSize(&whichEncounters);
	for (i = 0; i < n; i++)
	{
		int whichEnc = whichEncounters[i];
		if (whichEnc >= 0 && whichEnc < eaSize(&encLayer->staticEncounters))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
			GEInstanceStaticEncounter(staticEnc);

			// Call the change function now
			changeFunc(encLayer, staticEnc, staticEnc->defOverride, changeData);
		}
	}
}

static void ELEChangeSuccessCond(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, Expression* successCond)
{
	GEUpdateExpressionFromExpression(&defOverride->successCond, successCond);
}

static void ELEChangeFailCond(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, Expression* failCond)
{
	GEUpdateExpressionFromExpression(&defOverride->failCond, failCond);
}

static void ELEChangeSuccessAction(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, Expression* successAction)
{
	int i, size = eaSize(&defOverride->actions);
	
	// Find the first Success Action
	for (i = 0; i < size; ++i)
		if (defOverride->actions[i]->state == EncounterState_Success)
			break;

	// If there isn't a Success action, create one
	if (i == size)
	{
		eaPush(&defOverride->actions, StructCreate(parse_OldEncounterAction));
		defOverride->actions[i]->state = EncounterState_Success;
	}

	// Update the Success action
	GEUpdateExpressionFromExpression(&defOverride->actions[i]->actionExpr, successAction);
}

static void ELEChangeFailAction(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, Expression* failAction)
{
	int i, size = eaSize(&defOverride->actions);
	
	// Find the first Success Action
	for (i = 0; i < size; ++i)
		if (defOverride->actions[i]->state == EncounterState_Failure)
			break;

	// If there isn't a Success action, create one
	if (i == size)
	{
		eaPush(&defOverride->actions, StructCreate(parse_OldEncounterAction));
		defOverride->actions[i]->state = EncounterState_Failure;
	}

	// Update the Success action
	GEUpdateExpressionFromExpression(&defOverride->actions[i]->actionExpr, failAction);
}

static void ELEChangeWaveCond(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, Expression* waveCond)
{
	GEUpdateExpressionFromExpression(&defOverride->waveCond, waveCond);
}

static void ELEChangeWaveInterval(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, void* waveIntervalPtr)
{
	int waveInterval = PTR_TO_S32(waveIntervalPtr);
	defOverride->waveInterval = waveInterval;
}

static void ELEChangeWaveMinDelay(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, void* waveIntervalPtr)
{
	int waveInterval = PTR_TO_S32(waveIntervalPtr);
	defOverride->waveDelayMin = waveInterval;
}


static void ELEChangeWaveMaxDelay(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, void* waveIntervalPtr)
{
	int waveInterval = PTR_TO_S32(waveIntervalPtr);
	defOverride->waveDelayMax = waveInterval;
}


static void ELEChangeSpawnRadius(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, void* spawnRadiusPtr)
{
	int spawnRadius = PTR_TO_S32(spawnRadiusPtr);
	defOverride->spawnRadius = spawnRadius;
}

static void ELEChangeLockoutRadius(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, void* lockoutRadiusPtr)
{
	int lockoutRadius = PTR_TO_S32(lockoutRadiusPtr);
	defOverride->lockoutRadius = lockoutRadius;
}

static void ELEChangeRespawnTime(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, void* respawnTimePtr)
{
	int respawnTime = PTR_TO_S32(respawnTimePtr);
	defOverride->respawnTimer = respawnTime;
}

static void ELEChangeGangID(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, void* gangIDPtr)
{
	U32 newGangID = PTR_TO_U32(gangIDPtr);
	defOverride->gangID = newGangID;
}

static void ELEChangeEncIsAmbushEnc(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, bool* isAmbushEnc)
{
	defOverride->bAmbushEncounter = *isAmbushEnc;
}
static void ELEChangeEncSnapToGround(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, bool* bSnapToGround)
{
	staticEnc->bNoSnapToGround = !(*bSnapToGround);
}

static void ELEChangeDoNotDespawn(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, bool* bNoDespawn)
{
	staticEnc->noDespawn = (*bNoDespawn);
}

static void ELEChangeSpawnPerPlayer(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, bool* state)
{
	defOverride->bCheckSpawnCondPerPlayer = *state;
}

static void ELEChangeEncUsePlayerLevel(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, bool* usePlayerLevel)
{
	defOverride->bUsePlayerLevel = *usePlayerLevel;
}

static void ELEChangeSpawnCond(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, Expression* spawnCond)
{
	GEUpdateExpressionFromExpression(&defOverride->spawnCond, spawnCond);
}

static void ELEChangeSpawnChance(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, void* spawnChancePtr)
{
	U32 spawnChance = PTR_TO_U32(spawnChancePtr);
	defOverride->spawnChance = spawnChance;
}

static void ELEChangeDynamicSpawn(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, void* eValue)
{
	defOverride->eDynamicSpawnType = PTR_TO_S32(eValue);
}

void ele_ChangeStaticEncounterFaction(EncounterLayer* encLayer, int* whichEncounters, const char* factionName)
{
	int i, n = eaiSize(&whichEncounters);
	for (i = 0; i < n; i++)
	{
		int whichEnc = whichEncounters[i];
		if (whichEnc >= 0 && whichEnc < eaSize(&encLayer->staticEncounters))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
			REMOVE_HANDLE(staticEnc->encFaction);
			if (factionName)
			{
				SET_HANDLE_FROM_STRING(g_hCritterFactionDict, factionName, staticEnc->encFaction);
			}
		}
	}
}

void ele_ChangeStaticEncounterMinLevel(EncounterLayer* encLayer, int* whichEncounters, int minLevel)
{
	int i, n = eaiSize(&whichEncounters);
	for (i = 0; i < n; i++)
	{
		int whichEnc = whichEncounters[i];
		if (whichEnc >= 0 && whichEnc < eaSize(&encLayer->staticEncounters))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
			if (!staticEnc->maxLevel && !staticEnc->minLevel)
				staticEnc->maxLevel = staticEnc->spawnRule->maxLevel;
			staticEnc->minLevel = minLevel;
		}
	}
}

void ele_ChangeStaticEncounterMaxLevel(EncounterLayer* encLayer, int* whichEncounters, int maxLevel)
{
	int i, n = eaiSize(&whichEncounters);
	for (i = 0; i < n; i++)
	{
		int whichEnc = whichEncounters[i];
		if (whichEnc >= 0 && whichEnc < eaSize(&encLayer->staticEncounters))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
			if (!staticEnc->maxLevel && !staticEnc->minLevel)
				staticEnc->minLevel = staticEnc->spawnRule->minLevel;
			staticEnc->maxLevel = maxLevel;
		}
	}
}

void ele_ChangeStaticEncounterPatrol(EncounterLayer* encLayer, int* whichEncounters, char* patrolRouteName)
{
	int i, n = eaiSize(&whichEncounters);
	for (i = 0; i < n; i++)
	{
		int whichEnc = whichEncounters[i];
		if (whichEnc >= 0 && whichEnc < eaSize(&encLayer->staticEncounters))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
			staticEnc->patrolRouteName = (char*)allocAddString(patrolRouteName);
		}
	}
}

void ele_ChangeStaticEncounterCritterGroup(EncounterLayer* encLayer, int* whichEncounters, const char* groupName)
{
	int i, n = eaiSize(&whichEncounters);
	for (i = 0; i < n; i++)
	{
		int whichEnc = whichEncounters[i];
		if (whichEnc >= 0 && whichEnc < eaSize(&encLayer->staticEncounters))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
			REMOVE_HANDLE(staticEnc->encCritterGroup);
			if (groupName)
				SET_HANDLE_FROM_STRING(g_hCritterGroupDict, groupName, staticEnc->encCritterGroup);
		}
	}
}

static void ELEFixupActorMessages(EncounterLayer *layer, OldStaticEncounter *staticEnc, OldActor *actor)
{
	EncounterDef* baseDef = GET_REF(staticEnc->baseDef);
	OldActor *baseDefActor = baseDef ? oldencounter_FindDefActorByID(baseDef, actor->uniqueID, false) : NULL;
	OldActorAIInfo *aiInfo = actor->details.aiInfo;

	// Fix the display name message
	if (actor->displayNameMsg.pEditorCopy)
	{
		Message *defaultMsg = oldencounter_CreateDisplayNameMessageForStaticEncActor(staticEnc, actor);
		GEGenericUpdateMessage(&actor->displayNameMsg.pEditorCopy, NULL, defaultMsg);
		StructDestroy(parse_Message, defaultMsg);
	}

	if (aiInfo)
	{
		// We want the actual actorinfo that would be used at this team size, i.e. from the spawn rule.
		// Otherwise, we won't get the correct FSM in all cases.  -BF
		const OldActorInfo *actorInfo = GEGetActorInfoNoUpdate(actor, baseDefActor);
		FSM *fsm = oldencounter_GetActorFSM(actorInfo, aiInfo);
		
		int iVar, numVars = eaSize(&aiInfo->actorVars);
		for (iVar = numVars-1; iVar >= 0; --iVar)
		{
			OldEncounterVariable *var = aiInfo->actorVars[iVar];
			FSMExternVar *externVar = (fsm&&var&&var->varName)?fsmExternVarFromName(fsm, var->varName, "encounter"):0;
			if (externVar && externVar->scType && (stricmp(externVar->scType, "message") == 0))
			{
				Message *defaultMsg = oldencounter_CreateVarMessageForStaticEncounterActor(staticEnc, actor, var->varName, SAFE_MEMBER(fsm, name));
				GEGenericUpdateMessage(&var->message.pEditorCopy, NULL, defaultMsg);
				MultiValSetString(&var->varValue, defaultMsg->pcMessageKey);
				StructDestroy(parse_Message, defaultMsg);
			}
			else if (fsm && !externVar)
			{
				eaRemove(&aiInfo->actorVars, iVar);
				StructDestroy(parse_OldEncounterVariable, var);
			}
		}
	}
}

static void ELEFixupStaticEncounterMessages(EncounterLayer *layer, OldStaticEncounter *staticEnc)
{
	if (staticEnc->defOverride)
	{
		int i, n = eaSize(&staticEnc->defOverride->actors);
		for (i = 0; i < n; i++)
		{
			OldActor *actor = staticEnc->defOverride->actors[i];
			ELEFixupActorMessages(layer, staticEnc, actor);
		}
	}
}

static void ELEFixupLayerMessages(EncounterLayer *layer)
{
	int i;
	for (i = 0; i < eaSize(&layer->staticEncounters); i++){
		ELEFixupStaticEncounterMessages(layer, layer->staticEncounters[i]);
	}
}

int encounterdef_NextUniqueActorOverrideID(EncounterDef* def)
{
	int lowestUniqueID = 0;
	int i, n = eaSize(&def->actors);
	for (i = 0; i < n; i++)
		if (def->actors[i]->uniqueID < lowestUniqueID)
			lowestUniqueID = def->actors[i]->uniqueID;
	return lowestUniqueID - 1;
}

static void ELEApplyChangeToStaticEncounterActor(OldStaticEncounter* staticEnc, int whichActor, bool makeCopy, ActorChangeFunc changeFunc, const void* changeData, U32 iTeamSize)
{
	EncounterDef* def = staticEnc->spawnRule;
	EncounterDef* defOverride = staticEnc->defOverride;
	if (whichActor >= 0 && whichActor < eaSize(&def->actors))
	{
		OldActor* changeActor;
		OldActor* srcActor = def->actors[whichActor];

		// Create the override information if it does not already exist, all movement goes into this
		if (!staticEnc->defOverride)
			defOverride = staticEnc->defOverride = StructCreate(parse_EncounterDef);

		// If the actor ID is less than 0, it is already an override actor
		// Otherwise, find the override actor with the same id
		// Since this is a new actor, make a copy then force team size to 1 to update default position
		if (makeCopy)
		{
			changeActor = GEActorCreate(encounterdef_NextUniqueActorOverrideID(defOverride), srcActor);
			langMakeEditorCopy(parse_OldActor, changeActor, false);
			ELEFixupActorMessages(staticEnc->layerParent, staticEnc, changeActor);
			eaPush(&defOverride->actors, changeActor);
		}
		else
		{
			changeActor = oldencounter_FindDefActorByID(defOverride, srcActor->uniqueID, false);
			if (!changeActor)
			{
				changeActor = StructCreate(parse_OldActor);
				changeActor->uniqueID = srcActor->uniqueID;
				eaPush(&defOverride->actors, changeActor);
			}
		}

		// Call whatever function we need to apply to the actor
		changeFunc(staticEnc, changeActor, changeData, iTeamSize, srcActor);
	}
}

typedef struct ActorChangeData
{
	GEActorChangeFunc changeFunc;
	const void *pChangeData;
} ActorChangeData;

static void ELEModifySelectedActorsHelper(OldStaticEncounter* staticEnc, OldActor* actor, const ActorChangeData *pData, U32 teamSize, OldActor* srcActor)
{
	if (pData && pData->changeFunc && actor){
		pData->changeFunc(actor, srcActor, pData->pChangeData);
	}
}

static void ELEModifySelectedActors(EncounterLayerEditDoc *pDoc, GEActorChangeFunc changeFunc, const void *pChangeData)
{
	if (pDoc && pDoc->layerDef)
	{
		int** selActorsList = ELEGetFullSelectedActorIndexList(pDoc);
		int whichEnc, numEncs = eaSize(&selActorsList);
		int numStaticEncs = eaSize(&pDoc->layerDef->staticEncounters);
		MIN1(numEncs, numStaticEncs);
		for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
		{
			OldStaticEncounter* staticEnc = pDoc->layerDef->staticEncounters[whichEnc];
			int i, n = eaiSize(&selActorsList[whichEnc]);
			ActorChangeData data = {0};
			data.changeFunc = changeFunc;
			data.pChangeData = pChangeData;

			for (i = 0; i < n; i++)
				ELEApplyChangeToStaticEncounterActor(staticEnc, selActorsList[whichEnc][i], false, ELEModifySelectedActorsHelper, &data, 0);
		}
	}

	ELERefreshStaticEncounters(pDoc, false);
	GESetDocUnsaved(pDoc);
	ELERefreshActorPropUI(pDoc, true);
}

static void ELEChangeActorMat(OldStaticEncounter* staticEnc, OldActor* actor, const Mat4 changeMat, U32 teamSize, OldActor* srcActor)
{
	GEApplyActorPositionChange(actor, changeMat, srcActor);
}

static void ELEChangeActorCritter(OldStaticEncounter* staticEnc, OldActor* actor, const char* newCritterName, U32 teamSize, OldActor* srcActor)
{
	OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, srcActor);
	REMOVE_HANDLE(actorInfo->critterDef);
	if (newCritterName && resGetInfo("CritterDef", newCritterName))
	{
		SET_HANDLE_FROM_STRING(g_hCritterDefDict, newCritterName, actorInfo->critterDef);
	}
}

static void ELEChangeActorName(OldStaticEncounter* staticEnc, OldActor* actor, const char* actorName, U32 teamSize, OldActor* srcActor)
{
	if (!srcActor->name || stricmp(srcActor->name, actorName))
	{
		char* uniqueName = GECreateUniqueActorName(staticEnc->spawnRule, actorName);
		actor->name = GEAllocPooledStringIfNN(uniqueName);

		// NOTE: Currently the only solution I could come up with
		// Update the actual src actor, normally don't do but that is part of the unique name code
		srcActor->name = GEAllocPooledStringIfNN(uniqueName);
	}
}

static void ELEChangeActorDisplayName(OldStaticEncounter* staticEnc, OldActor* actor, const Message* displayName, U32 teamSize, OldActor* srcActor)
{
	if (!actor->displayNameMsg.pEditorCopy)
		actor->displayNameMsg.pEditorCopy = oldencounter_CreateDisplayNameMessageForStaticEncActor(staticEnc, actor);
	else // Update the Key and Scope, in case this was copied from an EncounterDef
	{
		Message *temp = oldencounter_CreateDisplayNameMessageForStaticEncActor(staticEnc, actor);
		actor->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(temp->pcMessageKey);
		actor->displayNameMsg.pEditorCopy->pcScope = allocAddString(temp->pcScope);
		StructDestroy(parse_Message, temp);
	}
	if (actor->displayNameMsg.pEditorCopy)
	{
		StructFreeString(actor->displayNameMsg.pEditorCopy->pcDefaultString);
		actor->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(displayName->pcDefaultString);
		if (displayName->pcDescription)
		{
			StructFreeString(actor->displayNameMsg.pEditorCopy->pcDescription);
			actor->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(displayName->pcDescription);
		}
		actor->displayNameMsg.pEditorCopy->bDoNotTranslate = displayName->bDoNotTranslate;
		actor->displayNameMsg.pEditorCopy->bFinal = displayName->bFinal;
	}
	ELERefreshEncounterTree((EncounterLayerEditDoc*) GEGetActiveEditorDocEM("encounterlayer"));
}

static void ELEChangeActorSpawnCond(OldStaticEncounter* staticEnc, OldActor* actor, const Expression* spawnWhen, U32 teamSize, OldActor* srcActor)
{
	OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, srcActor);
	GEUpdateExpressionFromExpression(&actorInfo->spawnCond, spawnWhen);
}

static void ELEChangeActorInteractCond(OldStaticEncounter* staticEnc, OldActor* actor, const Expression* newCond, U32 teamSize, OldActor* srcActor)
{
	OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, srcActor);
	GEUpdateExpressionFromExpression(&actorInfo->oldActorInteractProps.interactCond, newCond);
}

static void ELEChangeActorSpawnEnabled(OldStaticEncounter* staticEnc, OldActor* actor, const void* enableSpawnPtr, U32 teamSize, OldActor* srcActor)
{
	bool enableSpawn = PTR_TO_U32(enableSpawnPtr);
	if (actor->disableSpawn == ActorScalingFlag_Inherited)
		actor->disableSpawn = srcActor->disableSpawn;
	if (enableSpawn)
		actor->disableSpawn &= ~(1 << teamSize);
	else
		actor->disableSpawn |= (1 << teamSize);
	actor->disableSpawn &= (~ActorScalingFlag_Inherited);
}

static void ELEChangeActorBossBarEnabled(OldStaticEncounter* staticEnc, OldActor* actor, const void* enableBossBarPtr, U32 teamSize, OldActor* srcActor)
{
	bool enableBossBar = PTR_TO_U32(enableBossBarPtr);
	if (actor->useBossBar == ActorScalingFlag_Inherited)
		actor->useBossBar = srcActor->useBossBar;
	if (enableBossBar)
		actor->useBossBar |= (1 << teamSize);
	else
		actor->useBossBar &= ~(1 << teamSize);
	actor->useBossBar &= (~ActorScalingFlag_Inherited);
}

static void ELEChangeActorRank(OldStaticEncounter* staticEnc, OldActor* actor, const void* rankPtr, U32 teamSize, OldActor* srcActor)
{
	const char *rank = rankPtr;
	OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, srcActor);
	actorInfo->pcCritterRank = allocAddString(rank);
}

static void ELEChangeActorSubRank(OldStaticEncounter* staticEnc, OldActor* actor, const void* subRankPtr, U32 teamSize, OldActor* srcActor)
{
	const char *pcSubRank = subRankPtr;
	OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, srcActor);
	actorInfo->pcCritterSubRank = allocAddString(pcSubRank);
}

static void ELEChangeActorFaction(OldStaticEncounter* staticEnc, OldActor* actor, const char* factionName, U32 teamSize, OldActor* srcActor)
{
	OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, srcActor);
	REMOVE_HANDLE(actorInfo->critterFaction);
	if (factionName)
	{
		SET_HANDLE_FROM_STRING(g_hCritterFactionDict, factionName, actorInfo->critterFaction);
	}
}

static void ELEChangeActorContact(OldStaticEncounter* staticEnc, OldActor* actor, const char* contactName, U32 teamSize, OldActor* srcActor)
{
	OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, srcActor);
	REMOVE_HANDLE(actorInfo->contactScript);
	if (contactName)
	{
		SET_HANDLE_FROM_STRING(g_ContactDictionary, contactName, actorInfo->contactScript);
	}
}

static void ELEChangeActorSpawnAnim(OldStaticEncounter* staticEnc, OldActor* actor, const char* newAnim, U32 teamSize, OldActor* srcActor)
{
	OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, srcActor);
	actorInfo->pchSpawnAnim = (char*)allocAddString(newAnim);
}


static int s_WhichDialogIndex = 0x0badbad0;
static char* s_WhichVarName = (char*)0x0badbad0;

static void ELEChangeActorVar(OldStaticEncounter* staticEnc, OldActor* actor, const char* varStr, U32 teamSize, OldActor* srcActor)
{
	OldActorAIInfo* actorAIInfo = GEGetActorAIInfoForUpdate(actor, srcActor);
	OldEncounterVariable* actorVar = oldencounter_LookupActorVariable(actorAIInfo, s_WhichVarName);
	if (varStr && varStr[0])
	{
		if (!actorVar)
		{
			actorVar = StructCreate(parse_OldEncounterVariable);
			actorVar->varName = (char*)allocAddString(s_WhichVarName);
			eaPush(&actorAIInfo->actorVars, actorVar);
		}
		MultiValSetString(&actorVar->varValue, varStr);
	}
	else if (actorVar)
	{
		eaFindAndRemove(&actorAIInfo->actorVars, actorVar);
		StructDestroy(parse_OldEncounterVariable, actorVar);
	}
}

static void ELEChangeActorVarMessage(OldStaticEncounter* staticEnc, OldActor* actor, const Message* varMessage, U32 teamSize, OldActor* srcActor)
{
	const OldActorInfo *actorInfo = GEGetActorInfoNoUpdate(actor, srcActor);
	OldActorAIInfo* actorAIInfo = GEGetActorAIInfoForUpdate(actor, srcActor);
	OldEncounterVariable* actorVar = oldencounter_LookupActorVariable(actorAIInfo, s_WhichVarName);
	EncounterLayer *layer = staticEnc->layerParent;
	FSM *fsm = oldencounter_GetActorFSM(actorInfo, actorAIInfo);
	Message *defaultMessage = oldencounter_CreateVarMessageForStaticEncounterActor(staticEnc, actor, s_WhichVarName, SAFE_MEMBER(fsm, name));

	if (!actorVar)
	{
		actorVar = StructCreate(parse_OldEncounterVariable);
		actorVar->varName = (char*)allocAddString(s_WhichVarName);
		eaPush(&actorAIInfo->actorVars, actorVar);
	}

	GEGenericUpdateMessage(&actorVar->message.pEditorCopy, varMessage, defaultMessage);

	if (isValidMessage(actorVar->message.pEditorCopy))
	{
		MultiValSetString(&actorVar->varValue, actorVar->message.pEditorCopy->pcMessageKey);
	}
	else if (actorVar)
	{
		eaFindAndRemove(&actorAIInfo->actorVars, actorVar);
		StructDestroy(parse_OldEncounterVariable, actorVar);
	}

	StructDestroy(parse_Message, defaultMessage);
}


void ele_MoveStaticEncounterActor(OldStaticEncounter* staticEnc, int whichActor, Mat4 newRelMat, bool makeCopy)
{
	ELEApplyChangeToStaticEncounterActor(staticEnc, whichActor, makeCopy, ELEChangeActorMat, newRelMat, 0);
}
void ele_MoveStaticEncounterPoint(OldStaticEncounter* staticEnc, int whichPoint, Mat4 newRelMat)
{
	// There is no makeCopy field; points can only have their locations changed
	EncounterDef* def = staticEnc->spawnRule;
	EncounterDef* defOverride = staticEnc->defOverride;
	EncounterDef* baseDef = GET_REF(staticEnc->baseDef);
	if (whichPoint >= 0 && whichPoint < eaSize(&def->namedPoints))
	{
		OldNamedPointInEncounter* changePoint;
		OldNamedPointInEncounter* srcPoint = def->namedPoints[whichPoint];
		OldNamedPointInEncounter* origPoint;

		// Create the override information if it does not already exist, all movement goes into this
		if (!staticEnc->defOverride)
			defOverride = staticEnc->defOverride = StructCreate(parse_EncounterDef);


		changePoint = oldencounter_FindDefPointByID(defOverride, srcPoint->id, false);
		if (!changePoint)
		{
			changePoint = StructCreate(parse_OldNamedPointInEncounter);
			changePoint->id = srcPoint->id;
			eaPush(&defOverride->namedPoints, changePoint);
		}

		// Adjust the point's position
		origPoint = oldencounter_FindDefPointByID(baseDef, srcPoint->id, false);
		if(origPoint)
			subVec3(newRelMat[3], origPoint->relLocation[3], newRelMat[3]);
		
		copyMat4(newRelMat, changePoint->relLocation);
	}
}

void ele_ChangeStaticEncounterActorName(EncounterLayer* encLayer, int** whichEncActors, char* actorName)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorName, actorName, 0);
	}
}


void ele_ChangeStaticEncounterActorCritter(EncounterLayer* encLayer, int** whichEncActors, const char* critterName)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorCritter, critterName, 0);
	}
}

void ele_ChangeStaticEncounterActorFSM(EncounterLayer* encLayer, int** whichEncActors, char* fsmName)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, GEChangeActorFSM, fsmName, 0);
	}
}

void ele_ChangeStaticEncounterActorDisplayName(EncounterLayer* encLayer, int** whichEncActors, const Message* displayName)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorDisplayName, displayName, 0);
	}
}

void ele_ChangeStaticEncounterActorSpawnCond(EncounterLayer* encLayer, int** whichEncActors, Expression* spawnWhen)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorSpawnCond, spawnWhen, 0);
	}
}

void ele_ChangeStaticEncounterActorInteractCond(EncounterLayer* encLayer, int** whichEncActors, Expression* newCond)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorInteractCond, newCond, 0);
	}
}


void ele_ChangeStaticEncounterActorSpawnEnabled(EncounterLayer* encLayer, int** whichEncActors, bool spawnEnabled, U32 teamSize)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorSpawnEnabled, U32_TO_PTR(spawnEnabled), teamSize);
	}
}

void ele_ChangeStaticEncounterActorBossBarEnabled(EncounterLayer* encLayer, int** whichEncActors, bool enableBossBar, U32 teamSize)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorBossBarEnabled, U32_TO_PTR(enableBossBar), teamSize);
	}
}

void ele_ChangeStaticEncounterActorRank(EncounterLayer* encLayer, int** whichEncActors, const char *pcRank)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorRank, pcRank, 0);
	}
}

void ele_ChangeStaticEncounterActorSubRank(EncounterLayer* encLayer, int** whichEncActors, const char *pcSubRank)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorSubRank, pcSubRank, 0);
	}
}

void ele_ChangeStaticEncounterActorFaction(EncounterLayer* encLayer, int** whichEncActors, const char* factionName)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorFaction, factionName, 0);
	}
}

void ele_ChangeStaticEncounterActorContact(EncounterLayer* encLayer, int** whichEncActors, const char* contactName)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorContact, contactName, 0);
	}
}

// JAMES TODO: these functions are all identical, except with different callbacks

void ele_ChangeStaticEncounterActorSpawnAnim(EncounterLayer* encLayer, int** whichEncActors, const char* newAnim)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorSpawnAnim, newAnim, 0);
	}
}

void ele_ChangeStaticEncounterActorVar(EncounterLayer* encLayer, int** whichEncActors, const char* varName, const char* varStr)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	s_WhichVarName = (char*)varName;
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorVar, (char*)varStr, 0);
	}
}

void ele_ChangeStaticEncounterActorVarMessage(EncounterLayer* encLayer, int** whichEncActors, const char* varName, const Message* varMessage)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	s_WhichVarName = (char*)varName;
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
			ELEApplyChangeToStaticEncounterActor(staticEnc, whichEncActors[whichEnc][i], false, ELEChangeActorVarMessage, varMessage, 0);
	}
}


static void ELEEncounterNameChanged(UITextEntry* textEntry, EncounterLayerEditDoc* encLayerDoc)
{
	const char* newName = ui_TextEntryGetText(textEntry);
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ele_ChangeStaticEncounterName(encLayerDoc->layerDef, selEncList, (char*)newName);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshEncounterTree(encLayerDoc);
}

static void ELEEncAmbushCheckBoxChanged(UICheckButton* checkBox, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeEncIsAmbushEnc, &checkBox->state);
	GESetDocUnsaved(encLayerDoc);
}
static void ELEEncSnapToGroundCheckBoxChanged(UICheckButton* checkBox, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeEncSnapToGround, &checkBox->state);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEDoNotDespawnCheckBoxChanged(UICheckButton* checkBox, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeDoNotDespawn, &checkBox->state);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEDynamicSpawnTypeChanged(UIComboBox* pCombo, int iNewValue, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	WorldEncounterDynamicSpawnType eType = iNewValue;
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeDynamicSpawn, S32_TO_PTR(eType));
	GESetDocUnsaved(encLayerDoc);
}
static void ELESpawnPerPlayerCheckBoxChanged(UICheckButton* checkBox, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeSpawnPerPlayer, &checkBox->state);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEEncUsePlayerLevelCheckBoxChanged(UICheckButton* checkBox, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeEncUsePlayerLevel, &checkBox->state);
	GESetDocUnsaved(encLayerDoc);
}

static void ELESpawnCondChanged(Expression* newExpr, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeSpawnCond, newExpr);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshEncounterPropUI(encLayerDoc, true);
}

static void ELESuccessCondChanged(Expression* newExpr, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeSuccessCond, newExpr);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshEncounterPropUI(encLayerDoc, true);
}

static void ELEFailCondChanged(Expression* newExpr, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeFailCond, newExpr);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshEncounterPropUI(encLayerDoc, true);
}

static void ELESuccessActionChanged(Expression* newExpr, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeSuccessAction, newExpr);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshEncounterPropUI(encLayerDoc, true);
}

static void ELEFailActionChanged(Expression* newExpr, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeFailAction, newExpr);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshEncounterPropUI(encLayerDoc, true);
}

static void ELEWaveCondChanged(Expression* newExpr, EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeWaveCond, newExpr);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshEncounterPropUI(encLayerDoc, true);
}

static void ELEWaveIntervalChanged(UITextEntry* textEntry, EncounterLayerEditDoc* encLayerDoc)
{
	int newInterval = atoi(ui_TextEntryGetText(textEntry));
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeWaveInterval, S32_TO_PTR(newInterval));
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEWaveMinDelayChanged(UITextEntry* textEntry, EncounterLayerEditDoc* encLayerDoc)
{
	int newInterval = atoi(ui_TextEntryGetText(textEntry));
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeWaveMinDelay, S32_TO_PTR(newInterval));
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEWaveMaxDelayChanged(UITextEntry* textEntry, EncounterLayerEditDoc* encLayerDoc)
{
	int newInterval = atoi(ui_TextEntryGetText(textEntry));
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeWaveMaxDelay, S32_TO_PTR(newInterval));
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELESpawnRadiusChanged(UITextEntry* textEntry, EncounterLayerEditDoc* encLayerDoc)
{
	int newRadius = atoi(ui_TextEntryGetText(textEntry));
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeSpawnRadius, S32_TO_PTR(newRadius));
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELELockoutRadiusChanged(UITextEntry* textEntry, EncounterLayerEditDoc* encLayerDoc)
{
	int newRadius = atoi(ui_TextEntryGetText(textEntry));
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeLockoutRadius, S32_TO_PTR(newRadius));
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEEncRespawnTimeChanged(UITextEntry* textEntry, EncounterLayerEditDoc* encLayerDoc)
{
	int newTime = atoi(ui_TextEntryGetText(textEntry));
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeRespawnTime, S32_TO_PTR(newTime));
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEGangIDChanged(UITextEntry* textEntry, EncounterLayerEditDoc* encLayerDoc)
{
	U32 newGangID = atoi(ui_TextEntryGetText(textEntry));
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeGangID, U32_TO_PTR(newGangID));
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELETeamSizeChanged(UISlider* slider, bool bFinished, EncounterLayerEditDoc* encLayerDoc)
{
	char countStr[64];
	EncounterPropUI* encounterUI = encLayerDoc->uiInfo.encounterUI;
	encLayerDoc->activeTeamSize = ui_IntSliderGetValue(slider);
	sprintf(countStr, "%i", encLayerDoc->activeTeamSize);
	ui_LabelSetText(encounterUI->teamSizeCount, countStr);
	ELERefreshStaticEncounters(encLayerDoc, false);
	ELERefreshActorPropUI(encLayerDoc, true);
	ELERefreshEncounterPropUI(encLayerDoc, false);
}

static void ELESpawnChanceChanged(UISlider* slider, bool bFinished, EncounterLayerEditDoc* encLayerDoc)
{
	char countStr[64];
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	EncounterPropUI* encounterUI = encLayerDoc->uiInfo.encounterUI;
	U32 spawnChance = ui_IntSliderGetValue(slider);
	sprintf(countStr, "%u", spawnChance);
	ui_LabelSetText(encounterUI->chanceCount, countStr);
	ELEInstanceAndApplyChangeToStaticEncounters(encLayerDoc->layerDef, selEncList, ELEChangeSpawnChance, U32_TO_PTR(spawnChance));
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELECritterGroupChanged(UIComboBox* combo, EncounterLayerEditDoc* encLayerDoc)
{
	ResourceInfo *info = ui_ComboBoxGetSelectedObject(combo);
	const char *groupName = info?info->resourceName:NULL;
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ele_ChangeStaticEncounterCritterGroup(encLayerDoc->layerDef, selEncList, groupName);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEFactionChanged(UIComboBox* combo, EncounterLayerEditDoc* encLayerDoc)
{
	ResourceInfo *info = ui_ComboBoxGetSelectedObject(combo);
	const char *factionName = info?info->resourceName:NULL;
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ele_ChangeStaticEncounterFaction(encLayerDoc->layerDef, selEncList, factionName);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEPatrolChanged(UIComboBox* combo, EncounterLayerEditDoc* encLayerDoc)
{
	char* patrolRouteName = ui_ComboBoxGetSelectedObject(combo);
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ele_ChangeStaticEncounterPatrol(encLayerDoc->layerDef, selEncList, patrolRouteName);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEEncMinLevelChanged(UIEditable* levelEntry, EncounterLayerEditDoc* encLayerDoc)
{
	const char* levelStr = ui_EditableGetText(levelEntry);
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ele_ChangeStaticEncounterMinLevel(encLayerDoc->layerDef, selEncList, atoi(levelStr));
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEEncMaxLevelChanged(UIEditable* levelEntry, EncounterLayerEditDoc* encLayerDoc)
{
	const char* levelStr = ui_EditableGetText(levelEntry);
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	ele_ChangeStaticEncounterMaxLevel(encLayerDoc->layerDef, selEncList, atoi(levelStr));
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEActorNameChanged(UIEditable* nameEntry, EncounterLayerEditDoc* encLayerDoc)
{
	char* actorName = (char*)ui_EditableGetText(nameEntry);
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ele_ChangeStaticEncounterActorName(encLayerDoc->layerDef, selActorsList, actorName);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshStaticEncounters(encLayerDoc, false);
}

static void ELEActorCritterSelected(UITextEntry* entry, EncounterLayerEditDoc* encLayerDoc)
{
	const char* critterName = ui_TextEntryGetText(entry);
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ele_ChangeStaticEncounterActorCritter(encLayerDoc->layerDef, selActorsList, critterName);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshStaticEncounters(encLayerDoc, false);
	ELERefreshActorPropUI(encLayerDoc, true);
}


static void ELEActorFSMChanged(UITextEntry* entry, EncounterLayerEditDoc* encLayerDoc)
{
	char* fsmName = (char*)ui_TextEntryGetText(entry);
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ele_ChangeStaticEncounterActorFSM(encLayerDoc->layerDef, selActorsList, fsmName);
	ELERefreshStaticEncounters(encLayerDoc, false);
	//ELERefreshActorPropUI(encLayerDoc, true);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEActorFSMOpen(UITextEntry* entry, EncounterLayerEditDoc* encLayerDoc)
{
	OldActor** selActorsList = ELEGetSelectedActorList(encLayerDoc, -1);
	const char* commonFSM = GEFindCommonActorFSMName(&selActorsList);
	if(commonFSM)
		emOpenFileEx(commonFSM, "fsm");
}

static void ELEActorDisplayNameChanged(UIMessageEntry* entry, EncounterLayerEditDoc* encLayerDoc)
{
	const Message* dispName = ui_MessageEntryGetMessage(entry);
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ele_ChangeStaticEncounterActorDisplayName(encLayerDoc->layerDef, selActorsList, dispName);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEActorSpawnWhenChanged(Expression* newExpr, EncounterLayerEditDoc* encLayerDoc)
{
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ele_ChangeStaticEncounterActorSpawnCond(encLayerDoc->layerDef, selActorsList, newExpr);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshActorPropUI(encLayerDoc, true);
}

static void ELEActorInteractCondChanged(Expression* newExpr, EncounterLayerEditDoc* encLayerDoc)
{
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ele_ChangeStaticEncounterActorInteractCond(encLayerDoc->layerDef, selActorsList, newExpr);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshActorPropUI(encLayerDoc, true);
}

static void ELEActorSpawnEnabledChanged(UICheckButton* button, void* teamSizePtr)
{
	U32 teamSize = PTR_TO_U32(teamSizePtr);
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*)GEGetActiveEditorDocEM("encounterlayer");
	if (encLayerDoc)
	{
		int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
		ele_ChangeStaticEncounterActorSpawnEnabled(encLayerDoc->layerDef, selActorsList, ui_CheckButtonGetState(button), teamSize);
		ELERefreshStaticEncounters(encLayerDoc, false);
		GESetDocUnsaved(encLayerDoc);
	}
}

static void ELEActorBossBarEnabledChanged(UICheckButton* button, void* teamSizePtr)
{
	U32 teamSize = PTR_TO_U32(teamSizePtr);
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*)GEGetActiveEditorDocEM("encounterlayer");
	if (encLayerDoc)
	{
		int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
		ele_ChangeStaticEncounterActorBossBarEnabled(encLayerDoc->layerDef, selActorsList, ui_CheckButtonGetState(button), teamSize);
		ELERefreshStaticEncounters(encLayerDoc, false);
		GESetDocUnsaved(encLayerDoc);
	}
}

static void ELEActorRankChanged(UIComboBox* combo, EncounterLayerEditDoc* encLayerDoc)
{
	char *pcSelRank = NULL;
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ui_ComboBoxGetSelectedAsString(combo, &pcSelRank);
	ele_ChangeStaticEncounterActorRank(encLayerDoc->layerDef, selActorsList, pcSelRank);
	estrDestroy(&pcSelRank);
	ELERefreshStaticEncounters(encLayerDoc, false);
	ELERefreshEncounterPropUI(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEActorSubRankChanged(UIComboBox* combo, EncounterLayerEditDoc* encLayerDoc)
{
	char *pcSelSubRank = NULL;
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ui_ComboBoxGetSelectedAsString(combo, &pcSelSubRank);
	ele_ChangeStaticEncounterActorSubRank(encLayerDoc->layerDef, selActorsList, pcSelSubRank);
	estrDestroy(&pcSelSubRank);
	ELERefreshStaticEncounters(encLayerDoc, false);
	ELERefreshEncounterPropUI(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEActorFactionChanged(UIComboBox* combo, EncounterLayerEditDoc* encLayerDoc)
{
	ResourceInfo *info = ui_ComboBoxGetSelectedObject(combo);
	const char *factionName = info?info->resourceName:NULL;
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ele_ChangeStaticEncounterActorFaction(encLayerDoc->layerDef, selActorsList, factionName);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEActorSpawnAnimChanged(UIEditable * text, EncounterLayerEditDoc* encLayerDoc)
{
	char* spawnAnim = (char*)ui_EditableGetText(text);
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ele_ChangeStaticEncounterActorSpawnAnim(encLayerDoc->layerDef, selActorsList, spawnAnim);
	GESetDocUnsaved(encLayerDoc);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

static void ELEActorVarChanged(UIEditable* textEdit, const char* varName)
{
	const char* varStr = ui_EditableGetText(textEdit);
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*)GEGetActiveEditorDocEM("encounterlayer");
	if(encLayerDoc)
	{
		int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
		ele_ChangeStaticEncounterActorVar(encLayerDoc->layerDef, selActorsList, varName, varStr);
		ELERefreshStaticEncounters(encLayerDoc, false);
		GESetDocUnsaved(encLayerDoc);
	}
}

static void ELEActorVarMessageChanged(UIMessageEntry* msgEntry, const char* varName)
{
	const Message* varMsg = ui_MessageEntryGetMessage(msgEntry);
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*)GEGetActiveEditorDocEM("encounterlayer");
	if(encLayerDoc)
	{
		int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
		ele_ChangeStaticEncounterActorVarMessage(encLayerDoc->layerDef, selActorsList, varName, varMsg);
		ELERefreshStaticEncounters(encLayerDoc, false);
		GESetDocUnsaved(encLayerDoc);
	}
}

static void ELEActorContactChanged(UIEditable* editable, EncounterLayerEditDoc* encLayerDoc)
{
	const char *contactName = ui_EditableGetText(editable);
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	ele_ChangeStaticEncounterActorContact(encLayerDoc->layerDef, selActorsList, contactName);
	ELERefreshStaticEncounters(encLayerDoc, false);
	GESetDocUnsaved(encLayerDoc);
}

// Finds the distance of the furthest selected object
static float ELEFindFurthestSelectedDist(EncounterLayerEditDoc* encLayerDoc, Vec3 centerPos)
{
	int i, j, numSelActors;
	Mat4 encMat, actorMat;
	F32 dist, furthestDistSquared = 0;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int numEncs = eaSize(&encLayer->staticEncounters);
	for (i = 0; i < numEncs; i++)
	{
		OldActor** selActorList = NULL;
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[i];
		
		// Use the entire list of actors if the encounter is selected
		if (ELEFindSelectedEncounterObject(encLayerDoc, i))
			selActorList = staticEnc->spawnRule->actors;
		else
			selActorList = ELEGetSelectedActorList(encLayerDoc, i);
		
		quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
		numSelActors = eaSize(&selActorList);
		for (j = 0; j < numSelActors; j++)
		{
			OldActor* actor = selActorList[j];
			GEFindActorMat(actor, encMat, actorMat);
			dist = distance3SquaredXZ(actorMat[3], centerPos);
			if (dist > furthestDistSquared)
				furthestDistSquared = dist;
		}
	}
	return sqrt(furthestDistSquared);
}

// Finds the center of all selected encounters, returns the number selected
static int ELEFindSelectedCenter(EncounterLayerEditDoc* encLayerDoc, Vec3 centerPos)
{
	Mat4 encMat, actorMat;
	int totalSelected = 0;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int i, numObjects;
	zeroVec3(centerPos);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	// Patrols
	numObjects = eaSize(&encLayer->oldNamedRoutes);
	for (i = 0; i < numObjects; i++)
	{
		Mat4 pointMat;
		int j, numPoints;
		OldPatrolPoint** selPatrolPoints = NULL;
		OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[i];
		if (ELEFindSelectedPatrolRouteObject(encLayerDoc, i))
			selPatrolPoints = patrolRoute->patrolPoints;
		else
			selPatrolPoints = ELEGetSelectedPatrolPointList(encLayerDoc, i);
		numPoints = eaSize(&selPatrolPoints);
		for (j = 0; j < numPoints; j++)
		{
			OldPatrolPoint* patrolPoint = selPatrolPoints[j];
			quatVecToMat4(patrolPoint->pointRot, patrolPoint->pointLoc, pointMat);
			addVec3(pointMat[3], centerPos, centerPos);
			totalSelected++;
		}
	}
#endif
	// Encounters
	numObjects = eaSize(&encLayer->staticEncounters);
	for (i = 0; i < numObjects; i++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[i];
		quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
		if (ELEFindSelectedEncounterObject(encLayerDoc, i))
		{
			addVec3(encMat[3], centerPos, centerPos);
			totalSelected++;
		}
		else
		{
			OldActor** selActorList = ELEGetSelectedActorList(encLayerDoc, i);
			OldNamedPointInEncounter** selPointList = ELEGetSelectedEncPointList(encLayerDoc, i);
			int j, numActors = eaSize(&selActorList);
			int numPoints = eaSize(&selPointList);
			for (j = 0; j < numActors; j++)
			{
				OldActor* actor = selActorList[j];
				GEFindActorMat(actor, encMat, actorMat);
				addVec3(actorMat[3], centerPos, centerPos);
				totalSelected++;
			}
			for (j = 0; j < numPoints; j++)
			{
				OldNamedPointInEncounter* point = selPointList[j];
				GEFindEncPointMat(point, encMat, actorMat);
				addVec3(actorMat[3], centerPos, centerPos);
				totalSelected++;
			}
		}
	}

	if (totalSelected)
	{
		F32 scVal = 1.0 / totalSelected;
		scaleVec3(centerPos, scVal, centerPos);
	}

	return totalSelected;
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
void ele_DeletePatrolRoutes(EncounterLayer* encLayer, int* whichRoutes)
{
	int i, n = eaiSize(&whichRoutes);
	for (i = 0; i < n; i++)
	{
		int whichRoute = whichRoutes[i];
		if (whichRoute >= 0 && whichRoute < eaSize(&encLayer->oldNamedRoutes))
		{
			OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[whichRoute];
			StructDestroy(parse_OldPatrolRoute, patrolRoute);
			encLayer->oldNamedRoutes[whichRoute] = NULL;
		}
	}

	// Now remove all the NULLs
	for (i = eaSize(&encLayer->oldNamedRoutes) - 1; i >= 0; i--)
		if (!encLayer->oldNamedRoutes[i])
			eaRemove(&encLayer->oldNamedRoutes, i);
}

void ele_DeletePatrolPoints(EncounterLayer* encLayer, int** whichPointsList)
{
	int whichRoute, numRoutes = eaSize(&whichPointsList);
	int totalRoutes = eaSize(&encLayer->oldNamedRoutes);
	MIN1(numRoutes, totalRoutes);
	for (whichRoute = 0; whichRoute < numRoutes; whichRoute++)
	{
		OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[whichRoute];
		int i, n = eaiSize(&whichPointsList[whichRoute]);
		for (i = 0; i < n; i++)
		{
			int whichPoint = whichPointsList[whichRoute][i];
			if (whichPoint >= 0 && whichPoint < eaSize(&patrolRoute->patrolPoints))
			{
				OldPatrolPoint* patrolPoint = patrolRoute->patrolPoints[whichPoint];
				StructDestroy(parse_OldPatrolPoint, patrolPoint);
				patrolRoute->patrolPoints[whichPoint] = NULL;
			}
		}
		for (i = eaSize(&patrolRoute->patrolPoints) - 1; i >= 0; i--)
			if (!patrolRoute->patrolPoints[i])
				eaRemove(&patrolRoute->patrolPoints, i);
	}
}

void ele_DeleteEmptyRoutes(EncounterLayer* encLayer)
{
	int i;
	for (i = eaSize(&encLayer->oldNamedRoutes) - 1; i >= 0; i--)
	{
		OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[i];
		if (!eaSize(&patrolRoute->patrolPoints))
		{
			StructDestroy(parse_OldPatrolRoute, patrolRoute);
			eaRemove(&encLayer->oldNamedRoutes, i);
		}
	}
}
#endif

void ele_DeleteEncounterLayerActor(EncounterLayer* encLayer, int** whichEncActors)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		EncounterDef* def = staticEnc->spawnRule;
		int i, n = eaiSize(&whichEncActors[whichEnc]);
		for (i = 0; i < n; i++)
		{
			int whichActor = whichEncActors[whichEnc][i];
			if (whichActor >= 0 && whichActor < eaSize(&def->actors))
			{
				OldActor* actor = def->actors[whichActor];
				if (actor->uniqueID > 0)
					eaiPushUnique(&staticEnc->delActorIDs, actor->uniqueID);
				else if (staticEnc->defOverride)
				{
					OldActor* delActor = oldencounter_FindDefActorByID(staticEnc->defOverride, actor->uniqueID, true);
					if (delActor)
						StructDestroy(parse_OldActor, delActor);
				}
			}
		}
		oldencounter_UpdateStaticEncounterSpawnRule(staticEnc, encLayer);
	}
}

void ele_DeleteEncounter(EncounterLayer* encLayer, int* whichEncounters)
{
	int i, n = eaiSize(&whichEncounters);

	// Free all the encounters but just set the old positions to null
	for (i = 0; i < n; i++)
	{
		int whichEnc = whichEncounters[i];
		if (whichEnc >= 0 && whichEnc < eaSize(&encLayer->staticEncounters))
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
			OldStaticEncounterGroup* staticEncGroup = staticEnc->groupOwner;
			eaFindAndRemove(&staticEncGroup->staticEncList, staticEnc);

			// Recurse up the tree deleting empty Encounter Groups
			while (staticEncGroup && staticEncGroup->parentGroup && !eaSize(&staticEncGroup->childList) && !eaSize(&staticEncGroup->staticEncList) )
			{
				OldStaticEncounterGroup *parent = staticEncGroup->parentGroup;
				eaFindAndRemove(&staticEncGroup->parentGroup->childList, staticEncGroup);
				StructDestroy(parse_OldStaticEncounterGroup, staticEncGroup);
				staticEncGroup = parent;
			}

			oldencounter_RemoveStaticEncounterReference(staticEnc);
			StructDestroy(parse_OldStaticEncounter, staticEnc);
			encLayer->staticEncounters[whichEnc] = NULL;
		}
	}

	// Now remove all the NULLs
	for (i = eaSize(&encLayer->staticEncounters) - 1; i >= 0; i--)
		if (!encLayer->staticEncounters[i])
			eaRemove(&encLayer->staticEncounters, i);
}

static void ELEDeleteSelected(EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	if (!GEPlacementToolIsInPlacementMode(&encLayerDoc->placementTool))
	{
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
		int* selRoutes = ELEGetSelectedPatrolRouteIndexList(encLayerDoc);
		int** selPatrolPointsList = ELEGetFullSelectedPatrolIndexList(encLayerDoc);
#endif
		int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
		int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
		int *selPoints = ELEGetSelectedNamedPointIndexList(encLayerDoc);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
		// Patrol Routes
		ele_DeletePatrolPoints(encLayer, selPatrolPointsList);
		ele_DeletePatrolRoutes(encLayer, selRoutes);
		ele_DeleteEmptyRoutes(encLayer);
#endif

		// Encounters
		ele_DeleteEncounterLayerActor(encLayer, selActorsList);
		ele_DeleteEncounter(encLayer, selEncList);

		// Unselect all deleted objects
		eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);

		// Refresh the UI to reflect the deletions
		ELERefreshUI(encLayerDoc);
		GESetDocUnsaved(encLayerDoc);
	}
}

static void ELECopySelected(EncounterLayerEditDoc* encLayerDoc, bool makeCopy, bool useGizmos, const Vec3 selObjectPos, const Vec3 clickOffset)
{
	GEPlacementTool* placementTool = &encLayerDoc->placementTool;
	if (!GEPlacementToolIsInPlacementMode(placementTool))
	{
		Vec3 encCenter, moveCenter;
		placementTool->useGizmos = !!useGizmos;
		if (ELEFindSelectedCenter(encLayerDoc, moveCenter))
		{
			if (!placementTool->useGizmos)
				copyVec3(selObjectPos, encCenter);
			else
				copyVec3(moveCenter, encCenter);
			GEPlacementToolReset(placementTool, encCenter, clickOffset);
			placementTool->moveSelected = 1;
			placementTool->copySelected = !!makeCopy;
		}
	}
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static OldPatrolRoute** clipboard_Patrols;
#endif

static OldStaticEncounter** clipboard_Encounters;
static OldNamedPointInEncounter** clipboard_Points;

void ELECopySelectedToClipboard(EncounterLayerEditDoc* encLayerDoc, bool makeCopy)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int i, numObjects;

	// Wipe out the current clipboard
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	if(clipboard_Patrols)
		eaDestroyStruct(&clipboard_Patrols, parse_OldPatrolRoute);
#endif
	if(clipboard_Encounters)
		eaDestroyStruct(&clipboard_Encounters, parse_OldStaticEncounter);
	if(clipboard_Points)
		eaDestroyStruct(&clipboard_Points, parse_OldNamedPointInEncounter);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	// Patrols
	numObjects = eaSize(&encLayer->oldNamedRoutes);
	for (i = 0; i < numObjects; i++)
	{
		OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[i];
		if (ELEFindSelectedPatrolRouteObject(encLayerDoc, i))
		{
			OldPatrolRoute* newRoute = StructCreate(parse_OldPatrolRoute);
			StructCopyAll(parse_OldPatrolRoute, patrolRoute, newRoute);
			eaPush(&clipboard_Patrols, newRoute);
		}
		// Don't try to copy single patrol points
	}
#endif
	// Encounters
	numObjects = eaSize(&encLayer->staticEncounters);
	for (i = 0; i < numObjects; i++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[i];
		if (ELEFindSelectedEncounterObject(encLayerDoc, i))
		{
			OldStaticEncounter* newEncounter = StructCreate(parse_OldStaticEncounter);
			StructCopyAll(parse_OldStaticEncounter, staticEnc, newEncounter);

			// We just shallow-copied the NO_AST parts of the static encounter
			newEncounter->spawnRule = NULL;
			newEncounter->layerParent = NULL;
			newEncounter->groupOwner = NULL;

			eaPush(&clipboard_Encounters, newEncounter);
		}
		// Don't try to copy actors or points within encounters, if those are selected
	}

	if(!makeCopy)
		ELEDeleteSelected(encLayerDoc);
}

void ele_PasteStaticEncounter(EncounterLayer* encLayer, OldStaticEncounter* copiedEnc)
{
	OldStaticEncounter* staticEnc = StructAlloc(parse_OldStaticEncounter);
	StructCopyAll(parse_OldStaticEncounter, copiedEnc, staticEnc);

	// The parts of the encounter that are NO_AST only get shallow copied.
	staticEnc->layerParent = NULL;
	staticEnc->groupOwner = NULL;
	staticEnc->spawnRule = NULL;
	staticEnc->frozen = false;

	staticEnc->name = allocAddString(ELECreateUniqueStaticEncName(copiedEnc->name));
	oldencounter_AddStaticEncounterReference(staticEnc);

	staticEnc->layerParent = encLayer;
	staticEnc->groupOwner = &encLayer->rootGroup;
	eaPush(&encLayer->staticEncounters, staticEnc);
	eaPush(&encLayer->rootGroup.staticEncList, staticEnc);

	// Regenerate the spawn rule on the new encounter
	oldencounter_UpdateStaticEncounterSpawnRule(staticEnc, encLayer);

	ELEFixupStaticEncounterMessages(encLayer, staticEnc);
}

void ELEPasteSelectedFromClipboard(EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int i, numObjects;

	// Clear the list of selected objects
	eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	// Patrols
	numObjects = eaSize(&clipboard_Patrols);
	for (i = 0; i < numObjects; i++)
	{
		OldPatrolRoute* patrolRoute = clipboard_Patrols[i];
		OldPatrolRoute* newRoute = StructCreate(parse_OldPatrolRoute);
		StructCopyAll(parse_OldPatrolRoute, patrolRoute, newRoute);
		eaPush(&encLayer->oldNamedRoutes, newRoute);
		ELESelectPatrolRoute(encLayerDoc, eaSize(&encLayer->oldNamedRoutes) - 1, true);
	}
#endif
	// Encounters
	numObjects = eaSize(&clipboard_Encounters);
	for (i = 0; i < numObjects; i++)
	{
		OldStaticEncounter* staticEnc = clipboard_Encounters[i];
		ele_PasteStaticEncounter(encLayer, staticEnc);
		ELESelectEncounter(encLayerDoc, eaSize(&encLayer->staticEncounters) - 1, true);
	}
	GESetDocUnsaved(encLayerDoc);
	ELERefreshStaticEncounters(encLayerDoc, true);
	ELERefreshUI(encLayerDoc);
}

void ELESaveEncAsDef(EncounterLayerEditDoc* encLayerDoc, OldStaticEncounter* staticEnc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;

	if(staticEnc)
	{
		UITextEntry* renameWidget = ui_TextEntryCreate("NewEncounterDef", 0, 0);
		ui_TextEntrySetFinishedCallback(renameWidget, GESaveEncAsDefHelper, staticEnc->spawnRule);

		encLayerDoc->uiInfo.renameWindow = ui_WindowCreate("Save Encounter As", (g_ui_State.screenWidth * 0.5) - 100, (g_ui_State.screenHeight * 0.5) - 35, 200, 20);
		renameWidget->widget.width = 1;
		renameWidget->widget.widthUnit = UIUnitPercentage;
		renameWidget->widget.rightPad = renameWidget->widget.leftPad = 10;
		ui_TextEntrySetIgnoreSpace(renameWidget, true);
		ui_TextEntrySetSelectOnFocus(renameWidget, true);
		ui_WindowAddChild(encLayerDoc->uiInfo.renameWindow, renameWidget);
		ui_WindowSetModal(encLayerDoc->uiInfo.renameWindow, true);
		ui_WindowSetResizable(encLayerDoc->uiInfo.renameWindow, false);
		ui_WindowSetMovable(encLayerDoc->uiInfo.renameWindow, false);
		ui_WindowShow(encLayerDoc->uiInfo.renameWindow);
		ui_SetFocus(renameWidget);
	}
}

static void ELESnapCamera(EncounterLayerEditDoc* encLayerDoc)
{
	Vec3 centerPos;
	GfxCameraController* camera = gfxGetActiveCameraController();
	int numSelected = ELEFindSelectedCenter(encLayerDoc, centerPos);
	if (numSelected)
	{
		// Bump the center position up so it isn't looking at the objects feet
		centerPos[1] += 5;
		gfxCameraControllerSetTarget(camera, centerPos);
		if (numSelected > 1)
			camera->camdist = ME_MINSNAPDIST + ELEFindFurthestSelectedDist(encLayerDoc, centerPos) * 2;
		else
			camera->camdist = ME_MINSNAPDIST;
	}
	else
	{
		globCmdParse("Camera.center");
	}
}

void ele_GroupStaticEncounters(EncounterLayer* encLayer, int* whichGroups, OldStaticEncounterGroup* parentGroup)
{
	int i, n = eaiSize(&whichGroups);
	OldStaticEncounterGroup* newGroup = StructCreate(parse_OldStaticEncounterGroup);
	newGroup->groupName = (char*)allocAddString("NewGroup");
	newGroup->parentGroup = parentGroup;
	eaPush(&parentGroup->childList, newGroup);
	for (i = 0; i < n; i++)
	{
		int whichGroup = whichGroups[i];
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichGroup];
		eaFindAndRemove(&staticEnc->groupOwner->staticEncList, staticEnc);
		eaPush(&newGroup->staticEncList, staticEnc);
		staticEnc->groupOwner = newGroup;
	}
}

static void ELERemoveWeightedGroup(OldStaticEncounterGroup* staticEncGroup)
{
	int i, childCount;
	OldStaticEncounter** childEncs = NULL;
	OldStaticEncounterGroup** childEncGroups = NULL;
	int newValue = oldencounter_StaticEncounterGroupFindWeightedParent(staticEncGroup) ? 1 : 0;
	
	oldencounter_FunctionalGroupGetFunctionalGroupChildren(staticEncGroup, &childEncGroups);
	oldencounter_FunctionalGroupGetStaticEncounterChildren(staticEncGroup, &childEncs);
	
	childCount = eaSize(&childEncGroups);
	for (i = 0; i < childCount; i++)
		childEncGroups[i]->groupWeight = newValue;
	childCount = eaSize(&childEncs);
	for (i = 0; i < childCount; i++)
		childEncs[i]->spawnWeight = newValue;
	staticEncGroup->groupWeight = 0;
}

void ele_Ungroup(EncounterLayer* encLayer, int* whichStaticEncs, int* whichClickables)
{
	OldStaticEncounterGroup** groupList = NULL;
	int i, n = eaiSize(&whichStaticEncs);

	// Find all unique groups that are being undone
	for (i = 0; i < n; i++)
	{
		int whichGroup = whichStaticEncs[i];
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichGroup];
		OldStaticEncounterGroup* staticEncGroup = staticEnc->groupOwner;
		if (staticEncGroup && staticEncGroup->parentGroup)
			eaPushUnique(&groupList, staticEncGroup);
	}

	// Destroy them all
	n = eaSize(&groupList);
	for (i = 0; i < n; i++)
	{
		OldStaticEncounterGroup* group = groupList[i];
		OldStaticEncounterGroup* parentGroup = group->parentGroup;
		int j, numChildren = 0;
		eaFindAndRemove(&parentGroup->childList, group);
		
		// If this was a weighted grouping, we need to clear that		
		if (oldencounter_StaticEncounterGroupIsWeighted(group))
			ELERemoveWeightedGroup(group);

		// Reattach all of the child groups to the parent
		numChildren = eaSize(&group->childList);
		for (j = 0; j < numChildren; j++)
		{
			OldStaticEncounterGroup* childGroup = group->childList[j];
			childGroup->parentGroup = parentGroup;
			eaPush(&parentGroup->childList, childGroup);
		}

		// Add all of the static encounters to the new parent
		numChildren = eaSize(&group->staticEncList);
		for (j = 0; j < numChildren; j++)
		{
			OldStaticEncounter* staticEnc = group->staticEncList[j];
			staticEnc->groupOwner = parentGroup;
			eaPush(&parentGroup->staticEncList, staticEnc);
		}

		// Clear the group lists before freeing because it no longer owns them
		eaClear(&group->childList);
		eaClear(&group->staticEncList);
		StructDestroy(parse_OldStaticEncounterGroup, group);
	}
	eaDestroy(&groupList);
}

// Group command from the editor, currently just tries to group things
static void ELEGroupSelected(EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	int numSelGroups = eaiSize(&selEncList);
	if (numSelGroups && encLayer->staticEncounters && (selEncList[0] < eaSize(&encLayer->staticEncounters)))
	{
		int i, j, numParents;
		OldStaticEncounterGroup* firstEncGroup = encLayer->staticEncounters[selEncList[0]]->groupOwner;
		OldStaticEncounterGroup** firstEncParentGroups = NULL;
		OldStaticEncounterGroup* deepestCommonParent = NULL;
		ELECreateParentGroupList(firstEncGroup, &firstEncParentGroups);
		numParents = eaSize(&firstEncParentGroups);
		for (i = 0; i < numParents; i++)
		{
			OldStaticEncounterGroup* parentGroup = firstEncParentGroups[i];
			bool isSharedByAll = true;
			for (j = 1; (j < numSelGroups) && isSharedByAll; j++)
			{
				bool found = false;
				int whichGroup = selEncList[j];
				if (encLayer->staticEncounters && (whichGroup < eaSize(&encLayer->staticEncounters))) {
					OldStaticEncounterGroup* staticEncGroup = encLayer->staticEncounters[whichGroup]->groupOwner;
					OldStaticEncounterGroup* currParent = staticEncGroup;
					while (currParent && !found)
					{
						found = (currParent == parentGroup);
						currParent = currParent->parentGroup;
					}
					isSharedByAll = found;
				}
			}

			if (isSharedByAll)
			{
				deepestCommonParent = parentGroup;
				break;
			}
		}
		eaDestroy(&firstEncParentGroups);

		// If they didn't share a parent, make the common parent the root
		if (!deepestCommonParent)
			deepestCommonParent = &encLayer->rootGroup;

		// Detach all encounters from their group and put them in the root
		ele_GroupStaticEncounters(encLayer, selEncList, deepestCommonParent);
	}

	if (numSelGroups)
	{
		ELERefreshEncounterTree(encLayerDoc);
		GESetDocUnsaved(encLayerDoc);
	}
}

static void ELEUngroupSelected(EncounterLayerEditDoc* encLayerDoc)
{
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	int* selClickableList = NULL;
	ele_Ungroup(encLayerDoc->layerDef, selEncList, selClickableList);
	ELERefreshEncounterTree(encLayerDoc);
	GESetDocUnsaved(encLayerDoc);
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static void ele_FreezePatrolPoints(SA_PARAM_NN_VALID EncounterLayer* encLayer, int** whichPointsList, bool freeze)
{
	int whichRoute, numRoutes = eaSize(&whichPointsList);
	int totalRoutes = eaSize(&encLayer->oldNamedRoutes);
	MIN1(numRoutes, totalRoutes);
	for (whichRoute = 0; whichRoute < numRoutes; whichRoute++)
	{
		OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[whichRoute];
		int i, n = eaiSize(&whichPointsList[whichRoute]);
		for (i = 0; i < n; i++)
		{
			int whichPoint = whichPointsList[whichRoute][i];
			int numPoints = eaSize(&patrolRoute->patrolPoints);
			if (whichPoint >= 0 && whichPoint < numPoints)
			{
				// Freeze selected points
				OldPatrolPoint* point = patrolRoute->patrolPoints[whichPoint];
				if(point)
					point->frozen = freeze;
			}
		}
		for (i = eaSize(&patrolRoute->patrolPoints) - 1; i >= 0; i--)
			if (!patrolRoute->patrolPoints[i])
				eaRemove(&patrolRoute->patrolPoints, i);
	}
}
#endif

// If whichEncounters is NULL, freeze or unfreeze all encounters
static void ele_FreezeEncounter(SA_PARAM_NN_VALID EncounterLayer* encLayer, int* whichEncounters, bool freeze)
{
	int i, n;

	if(whichEncounters)
		n = eaiSize(&whichEncounters);
	else
		n = eaSize(&encLayer->staticEncounters);

	for (i = 0; i < n; i++)
	{
		int whichEnc;
		
		if(whichEncounters)
			whichEnc = whichEncounters[i];
		else
			whichEnc = i;

		encLayer->staticEncounters[whichEnc]->frozen = freeze;
	}
}

static void ele_FreezeEncounterLayerActor(SA_PARAM_NN_VALID EncounterLayer* encLayer, int** whichEncActors, bool freeze)
{
	int whichEnc, numEncs = eaSize(&whichEncActors);
	int numStaticEncs = eaSize(&encLayer->staticEncounters);
	MIN1(numEncs, numStaticEncs);
	for (whichEnc = 0; whichEnc < numEncs; whichEnc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
		EncounterDef* def = staticEnc->spawnRule;
		int n = eaiSize(&whichEncActors[whichEnc]);

		// If there are any selected actors in this encounter, freeze the whole encounter
		if(n > 0)
			staticEnc->frozen = freeze;
	}
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
// If whichRoutes is NULL, freeze or unfreeze all routes
static void ele_FreezePatrolRoutes(SA_PARAM_NN_VALID EncounterLayer* encLayer, int* whichRoutes, bool freeze)
{
	int i, n;

	if(whichRoutes)
		n = eaiSize(&whichRoutes);
	else
		n = eaSize(&encLayer->oldNamedRoutes);

	for (i = 0; i < n; i++)
	{
		int whichRoute;

		if(whichRoutes)
			whichRoute = whichRoutes[i];
		else
			whichRoute = i;

		if (whichRoute >= 0 && whichRoute < eaSize(&encLayer->oldNamedRoutes))
		{
			OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[whichRoute];
			int j, m = eaSize(&patrolRoute->patrolPoints);

			// Freeze all points in this route
			for(j=0; j<m; j++)
			{
				patrolRoute->patrolPoints[j]->frozen = freeze;
			}
		}
	}
}
#endif

// Freeze selected objects so that they can be seen but can't be selected
static void ELEFreezeSelected(EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	if (!GEPlacementToolIsInPlacementMode(&encLayerDoc->placementTool))
	{
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
		int* selRoutes = ELEGetSelectedPatrolRouteIndexList(encLayerDoc);
		int** selPatrolPointsList = ELEGetFullSelectedPatrolIndexList(encLayerDoc);
#endif
		int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
		int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
		int *selPoints = ELEGetSelectedNamedPointIndexList(encLayerDoc);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
		// Patrol Routes
		ele_FreezePatrolPoints(encLayer, selPatrolPointsList, true);
		ele_FreezePatrolRoutes(encLayer, selRoutes, true);
#endif

		// Encounters
		ele_FreezeEncounterLayerActor(encLayer, selActorsList, true);
		ele_FreezeEncounter(encLayer, selEncList, true);

		// Unselect all selected objects
		eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);

		// Refresh the UI to reflect the items that are no longer selected
		ELERefreshUI(encLayerDoc);
		ELERefreshStaticEncounters(encLayerDoc, true);
	}
}

// Freeze selected objects so that they can be seen but can't be selected
static void ELEUnfreezeAll(EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;

	// Passing in NULL will unfreeze all points/volumes/actors
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	ele_FreezePatrolRoutes(encLayer, NULL, false);
#endif
	ele_FreezeEncounter(encLayer, NULL, false);
}

static void ELESaveEncAsDefMenuCB(void* menuItem, OldStaticEncounter* data)
{
	EncounterLayerEditDoc *encLayerDoc = GEGetActiveEditorDocEM("encounterlayer");
	if (encLayerDoc)
		ELESaveEncAsDef(encLayerDoc, data);
}

void ELEForceTeamSizeChanged(UISlider* slider, bool bFinished, EncounterLayerEditDoc* encLayerDoc)
{
	char tmpStr[1024];
	EncounterLayer* layerDef = encLayerDoc->layerDef;
	U32 sliderVal = ui_IntSliderGetValue(slider);

	if (sliderVal != layerDef->forceTeamSize)
	{
		layerDef->forceTeamSize = sliderVal;

		if (layerDef->forceTeamSize)
			sprintf(tmpStr, "%i", layerDef->forceTeamSize);
		else
			strcpy(tmpStr, "No Override");
		ui_LabelSetText(encLayerDoc->uiInfo.teamSizeLabel, tmpStr);
		GESetDocUnsaved(encLayerDoc);
	}
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static char* ELECreateUniquePatrolRouteName(EncounterLayer* encLayer, char* desiredName)
{
	static char nextName[GE_NAMELENGTH_MAX];
	int counter = 1;
	strcpy(nextName, desiredName);
	while (oldencounter_OldPatrolRouteFromName(encLayer, nextName))
	{
		sprintf(nextName, "%s%i", desiredName, counter);
		counter++;
	}
	return nextName;
}

void ele_ChangePatrolRouteName(EncounterLayer* encLayer, OldPatrolRoute** patrolRouteList, char* desiredName)
{
	int which, numRoutes = eaSize(&patrolRouteList);
	if (!desiredName)
		desiredName = "patrol";
	for (which = 0; which < numRoutes; which++)
	{
		OldPatrolRoute* patrolRoute = patrolRouteList[which];
		if (patrolRoute->routeName && stricmp(patrolRoute->routeName, desiredName))
		{
			char* newName = ELECreateUniquePatrolRouteName(encLayer, desiredName);
			int i, n = eaSize(&encLayer->staticEncounters);
			for (i = 0; i < n; i++)
			{
				OldStaticEncounter* staticEnc = encLayer->staticEncounters[i];
				if (staticEnc->patrolRouteName && !stricmp(patrolRoute->routeName, staticEnc->patrolRouteName))
				{
					staticEnc->patrolRouteName = (char*)allocAddString(newName);
				}
			}
			patrolRoute->routeName = (char*)allocAddString(newName);
		}
	}
}

void ele_ChangePatrolRouteType(EncounterLayer* encLayer, int* whichRoutes, OldPatrolRouteType routeType)
{
	int i, n = eaiSize(&whichRoutes);
	for (i = 0; i < n; i++)
	{
		int whichRoute = whichRoutes[i];
		if (whichRoute >= 0 && whichRoute < eaSize(&encLayer->oldNamedRoutes))
		{
			OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[whichRoute];
			patrolRoute->routeType = routeType;
		}
	}
}

static void ELEPatrolRouteNameChanged(UIEditable* nameEntry, EncounterLayerEditDoc* encLayerDoc)
{
	char* newName = (char*)ui_EditableGetText(nameEntry);
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	OldPatrolRoute** selRouteList = ELEGetSelectedPatrolRouteList(encLayerDoc);
	ele_ChangePatrolRouteName(encLayer, selRouteList, newName);
	GESetDocUnsaved(encLayerDoc);
}
#endif

static void ELELayerLevelChanged(UIEditable* levelEntry, EncounterLayerEditDoc* encLayerDoc)
{
	const char* lvlStr = ui_EditableGetText(levelEntry);
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	encLayer->layerLevel = atoi(lvlStr);
	GESetDocUnsaved(encLayerDoc);
}

static void ELELayerLockoutChanged(UICheckButton* checkButton, EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	encLayer->useLockout = !!ui_CheckButtonGetState(checkButton);
	GESetDocUnsaved(encLayerDoc);
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static void ELEPatrolRouteTypeSelected(UIComboBox* combo, int whichType, EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int* selRoutes = ELEGetSelectedPatrolRouteIndexList(encLayerDoc);
	ele_ChangePatrolRouteType(encLayer, selRoutes, whichType);
	GESetDocUnsaved(encLayerDoc);
}
#endif


static bool ELEBeginQuickPlace(EncounterLayerEditDoc* encLayerDoc)
{
	GEPlacementTool* placementTool = &encLayerDoc->placementTool;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	Vec3 selActorPos, clickOffset;
	int groupIndex, objIndex;
	GEObjectType foundType = -1;
	GEObjectType foundObjType = -1;
	EncounterLayer* foundInLayer = NULL;

	// Only if quick placing is enabled, of course
	if(g_DisableQuickPlace)
		return false;

	// Try to find the closest object that isn't filtered out
	foundType = ELEFindObjectUnderMouse(encLayerDoc, &foundInLayer, selActorPos, clickOffset, &groupIndex, &objIndex, NULL, &foundObjType, true, false);

	if (foundInLayer == encLayer)
	{
		// Now we know if we found anything
		if(foundType == GEObjectType_Encounter)
		{
			// Select either an actor or point in the encounter, or the whole encounter
			if (placementTool->useBoreSelect && foundObjType == GEObjectType_Actor && !ELEFindSelectedActorObject(encLayerDoc, groupIndex, objIndex))
				ELESelectActor(encLayerDoc, groupIndex, objIndex, placementTool->useAdditiveSelect);
			if (placementTool->useBoreSelect && foundObjType == GEObjectType_Point && !ELEFindSelectedEncPointObject(encLayerDoc, groupIndex, objIndex))
				ELESelectEncPoint(encLayerDoc, groupIndex, objIndex, placementTool->useAdditiveSelect);
			else if (!placementTool->useBoreSelect && !ELEFindSelectedActorObject(encLayerDoc, groupIndex, objIndex)
						&& !ELEFindSelectedEncPointObject(encLayerDoc, groupIndex, objIndex)
						&& !ELEFindSelectedEncounterObject(encLayerDoc, groupIndex))
				ELESelectEncounter(encLayerDoc, groupIndex, placementTool->useAdditiveSelect);
			ELECopySelected(encLayerDoc, false, false, selActorPos, clickOffset);
			ELERefreshUI(encLayerDoc);
			return true;
		}
		else if(foundType == GEObjectType_Point)
		{
			if(!ELEFindSelectedNamedPoint(encLayerDoc, groupIndex))
				ELESelectNamedPoint(encLayerDoc, groupIndex, placementTool->useAdditiveSelect);
			ELECopySelected(encLayerDoc, false, false, selActorPos, clickOffset);
			ELERefreshUI(encLayerDoc);
			return true;
		}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
		else if(foundType == GEObjectType_PatrolRoute)
		{
			if (placementTool->useBoreSelect && !ELEFindSelectedPatrolPointObject(encLayerDoc, groupIndex, objIndex))
				ELESelectPatrolPoint(encLayerDoc, groupIndex, objIndex, placementTool->useAdditiveSelect);
			else if (!placementTool->useBoreSelect && !ELEFindSelectedPatrolPointObject(encLayerDoc, groupIndex, objIndex) && !ELEFindSelectedPatrolRouteObject(encLayerDoc, groupIndex))
				ELESelectPatrolRoute(encLayerDoc, groupIndex, placementTool->useAdditiveSelect);
			ELECopySelected(encLayerDoc, false, false, selActorPos, clickOffset);
			ELERefreshUI(encLayerDoc);
			return true;
		}
#endif
	}
	return false;
}

typedef enum ELERootGroupType
{
	ELERootGroupType_Encounter,
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	ELERootGroupType_PatrolRoute,
#endif

	ELERootGroupType_Count,
} ELERootGroupType;

static void ELEDrawTrackerNode(UITreeNode* uiNode, void* unused, UI_MY_ARGS, F32 z);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static void ELEPatrolMode(EncounterLayerEditDoc* encLayerDoc)
{
	int* patrolIdx;
	encLayerDoc->typeToCreate = GEObjectType_PatrolRoute;

	// If an encounter is selected but no patrol route is selected, select the encounter's patrol route
	patrolIdx = ELEGetSelectedPatrolRouteIndexList(encLayerDoc);
	if(eaiSize(&patrolIdx) == 0)
	{
		OldStaticEncounter* encounter = ELEGetLastSelectedEncounter(encLayerDoc);

		// Find the encounter's patrol route and select it additively
		if(encounter && encounter->patrolRouteName)
		{
			int i, n = eaSize(&encLayerDoc->layerDef->oldNamedRoutes);
			const char* encounterRouteName = encounter->patrolRouteName;
			for (i = 0; i < n; i++)
			{
				const char* routeName = encLayerDoc->layerDef->oldNamedRoutes[i]->routeName;
				if (routeName && !stricmp(encounterRouteName, routeName))
					ELESelectPatrolRoute(encLayerDoc, i, true);
			}
			ELERefreshUI(encLayerDoc);
		}
	}
}
#endif

static void ELECreateEncounterButton(UIButton* button, EncounterLayerEditDoc* encLayerDoc)
{
	ELEPlaceEncounterEM();
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static void ELECreatePatrolButton(UIButton* button, EncounterLayerEditDoc* encLayerDoc)
{
	ELEPatrolMode(encLayerDoc);
	encLayerDoc->docDefinition->PlaceObjectCB(encLayerDoc, "New", "NewFromKeyBind");
}
#endif

static void ELEPointMode(EncounterLayerEditDoc* encLayerDoc)
{
	encLayerDoc->typeToCreate = GEObjectType_Point;
}

static void ELEFreeNode(UITreeNode* uiNode)
{
	free(uiNode->contents);
	uiNode->contents = NULL;
}

static ELETreeNode* ELECreateNode(ELETreeNodeType type, void* data, ELETreeNode* parent)
{
	ELETreeNode* newNode = calloc(1, sizeof(ELETreeNode));
	newNode->nodeType = type;
	newNode->nodeData = data;
	newNode->parent = parent;
	return newNode;
}

// Helper function for selecting volumes, spawn points, and named points.  Selects the child of parentNode whose data is childData
static bool ELESelectChildNode(UITreeNode* parentNode, const void* childData)
{
	ELETreeNode* foundNode = NULL;
	bool alreadyopen = parentNode->open;
	int i, n;

	if (!parentNode->open)
		ui_TreeNodeExpand(parentNode);
	// Find the volume's node below its parent
	n = eaSize(&parentNode->children);
	for (i = 0; i < n; i++)
	{
		ELETreeNode* meChildNode = parentNode->children[i]->contents;
		if(meChildNode->nodeData == childData || ELESelectChildNode(parentNode->children[i], childData))
			foundNode = meChildNode;
	}

	// Mark the volume as selected in the tree
	if (foundNode)	
	{
		foundNode->selected = true;
		return true;
	}

	// Child wasn't found; collapse tree node
	if (!alreadyopen)
		ui_TreeNodeCollapse(parentNode);
	return false;
}

// Updates the state of the edit doc when something is selected
static void ELERefreshTreeEnc(EncounterLayerEditDoc* encLayerDoc, int whichEnc, int whichActor, bool forceSelected)
{
	bool isSelected = forceSelected;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	UITree* uiTree = encLayerDoc->uiInfo.layerTree;
	OldStaticEncounter* staticEnc = encLayer->staticEncounters[whichEnc];
	OldActor *actor = NULL;

	if (whichActor >= 0 && whichActor < eaSize(&staticEnc->spawnRule->actors))
		actor = staticEnc->spawnRule->actors[whichActor];

	// Figure out if this is a selection or deselection
	if (whichEnc >= 0 && !forceSelected)
	{
		if (whichActor == -1)
			isSelected = ELEFindSelectedEncounterObject(encLayerDoc, whichEnc) ? true : false;
		else
			isSelected = ELEFindSelectedActorObject(encLayerDoc, whichEnc, whichActor) ? true : false;
	}
	
	if (actor)
		ELESelectChildNode(uiTree->root.children[ELERootGroupType_Encounter], actor);
	else
		ELESelectChildNode(uiTree->root.children[ELERootGroupType_Encounter], staticEnc);
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static void ELERefreshTreePatrol(EncounterLayerEditDoc* encLayerDoc, int groupIdx, int objIdx)
{
	UITree* uiTree = encLayerDoc->uiInfo.layerTree;
	OldPatrolRoute* route = ELEPatrolRouteFromSelection(encLayerDoc, groupIdx, objIdx);
	UITreeNode* parentNode;

	parentNode = uiTree->root.children[ELERootGroupType_PatrolRoute];

	if (route)
		ELESelectChildNode(parentNode, route);
}
#endif

// Note that it isn't safe to refresh the encounter tree if you're in the process of editing (you can't
// do it for each modified actor, for instance; just do it once after all edits are complete).
void ELERefreshEncounterTree(EncounterLayerEditDoc* encLayerDoc)
{
	int i, n = eaSize(&encLayerDoc->selectedObjects);

	// We can't rebuild the tree during a callback from the tree; make sure that it gets done later
	if(encLayerDoc->selectionFromTree)
		encLayerDoc->refreshTree = true;
	else
	{
		ui_TreeRefresh(encLayerDoc->uiInfo.layerTree);
		for (i = 0; i < n; i++)
		{
			GESelectedObject* selObject = encLayerDoc->selectedObjects[i];
			if (selObject->selType == GEObjectType_Actor)
				ELERefreshTreeEnc(encLayerDoc, selObject->groupIndex, selObject->objIndex, true);
			else if (selObject->selType == GEObjectType_Encounter)
				ELERefreshTreeEnc(encLayerDoc, selObject->groupIndex, -1, true);
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
			else if (selObject->selType == GEObjectType_PatrolRoute || selObject->selType == GEObjectType_PatrolPoint)
				ELERefreshTreePatrol(encLayerDoc, selObject->groupIndex, selObject->objIndex);
#endif
		}
		encLayerDoc->refreshTree = false;
	}
}

static void ELEDrawTrackerNode(UITreeNode* uiNode, EncounterLayerEditDoc* encLayerDoc, UI_MY_ARGS, F32 z)
{
	static REF_TO(UIStyleFont) boldFont;
	static REF_TO(UIStyleFont) normalFont;
	static REF_TO(UIStyleFont) redFont;
	UIStyleFont* fontToUse;

	ELETreeNode* node = uiNode->contents;
	char* dispString = NULL;

	if (!GET_REF(normalFont))
	{
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default", normalFont);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "MissionEditor_Bold", boldFont);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_PlacementRed", redFont);
	}

	fontToUse = GET_REF(normalFont);
	estrStackCreate(&dispString);

	if (node->nodeType == ELETreeNodeType_EncGroup)
	{
		EncounterLayer* encLayer = (EncounterLayer*) encLayerDoc->layerDef;
		OldStaticEncounterGroup* staticEncGroup = (OldStaticEncounterGroup*)node->nodeData;
		const char* groupName = staticEncGroup->groupName ? staticEncGroup->groupName : "Encounters";
		estrPrintf(&dispString, "%s", groupName);
		if(oldencounter_StaticEncounterGroupIsFunctional(staticEncGroup))
			fontToUse = GET_REF(boldFont);
	}
	else if (node->nodeType == ELETreeNodeType_Encounter)
	{
		OldStaticEncounter* staticEnc = (OldStaticEncounter*)node->nodeData;
		EncounterDef* def = staticEnc->spawnRule;
		if (staticEnc->name)
			estrPrintf(&dispString, "%s", staticEnc->name);
		else if (def->name)
			estrPrintf(&dispString, "(Unnamed) Def: %s", def->name);
		else if (!GET_REF(staticEnc->baseDef))
			estrPrintf(&dispString, "Unnamed Encounter");
		else
			estrPrintf(&dispString, "Unknown Ref: ");
		if (GET_REF(staticEnc->baseDef))
			estrConcatf(&dispString, " (%s)", REF_STRING_FROM_HANDLE(staticEnc->baseDef));
		if(staticEnc->bDistToGroundChanged)
			fontToUse = GET_REF(redFont);
	}
	else if (node->nodeType == ELETreeNodeType_Actor)
	{
		OldActor* actor = (OldActor*)node->nodeData;
		char* actorName = NULL;
		estrStackCreate(&actorName);
		oldencounter_GetActorName(actor, &actorName);
		estrPrintf(&dispString, "%s", actorName);
		estrDestroy(&actorName);
	}
	else if (node->nodeType == ELETreeNodeType_NamedPoint)
	{
		OldNamedPointInEncounter* point = (OldNamedPointInEncounter*)node->nodeData;
		estrPrintf(&dispString, "%s", point->pointName);
	}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	else if (gConf.bAllowOldPatrolData && (node->nodeType == ELETreeNodeType_PatrolGroup))
	{
		estrPrintf(&dispString, "%s", "Patrols");
	}
	else if (gConf.bAllowOldPatrolData && (node->nodeType == ELETreeNodeType_Patrol))
	{
		OldPatrolRoute* route = (OldPatrolRoute*)node->nodeData;
		estrPrintf(&dispString, "%s", route->routeName);
	}
#endif
	else
	{
		estrPrintf(&dispString, "Unknown Node - Get Programmer");
	}

	ui_StyleFontUse(fontToUse, node->selected, UI_WIDGET(uiNode->tree)->state);
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", dispString);
	estrDestroy(&dispString);
}

static bool ELEStaticEncGroupHasEncounters(OldStaticEncounterGroup *group)
{
	int i, n;

	if (eaSize(&group->staticEncList))
		return true;

	n = eaSize(&group->childList);
	for (i = 0; i < n; i++)
	{
		if (ELEStaticEncGroupHasEncounters(group->childList[i]))
			return true;
	}

	return false;
}

static U32 getStaticEncounterGroupCRC(OldStaticEncounterGroup* staticEncGroup)
{
	if (!staticEncGroup)
		return 0;
	cryptAdler32Init();
	cryptAdler32Update((void*)&staticEncGroup, sizeof(staticEncGroup));
	if (staticEncGroup->groupName)
		cryptAdler32UpdateString(staticEncGroup->groupName);
	return cryptAdler32Final();
}

static U32 getActorCRC(OldActor* actor)
{
	if (!actor)
		return 0;
	return cryptAdler32((void*)&actor->uniqueID, sizeof(actor->uniqueID));
}

static U32 getStaticEncounterCRC(OldStaticEncounter* staticEnc)
{
	if (!staticEnc)
		return 0;
	cryptAdler32Init();
	cryptAdler32Update((void*)&staticEnc->spawnRule, sizeof(staticEnc->spawnRule));
	if (staticEnc->name)
		cryptAdler32UpdateString(staticEnc->name);
	return cryptAdler32Final();
}

static U32 getPointCRC(OldNamedPointInEncounter* point)
{
	if (!point)
		return 0;
	cryptAdler32Init();
	if (point->pointName)
		cryptAdler32UpdateString(point->pointName);
	return cryptAdler32Final();
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static U32 getRouteCRC(OldPatrolRoute* route)
{
	if (!route || (route && !route->routeName))
		return 0;
	cryptAdler32Init();
	if (route->routeName)
		cryptAdler32UpdateString(route->routeName);
	return cryptAdler32Final();
}
#endif

static void ELEFillTrackerNode(UITreeNode* uiNode, EncounterLayerEditDoc* encLayerDoc)
{
	ELETreeNode* node = uiNode->contents;
	EncounterLayer* encLayer = encLayerDoc->layerDef;

	// Create root tree entries
	if (!node)
	{
		ELERootGroupType rootType;
		for (rootType = ELERootGroupType_Encounter; rootType < ELERootGroupType_Count; rootType++)
		{
			ELETreeNode* newNode;
			UITreeNode* uiNewNode;
			switch (rootType)
			{
				xcase ELERootGroupType_Encounter:
					newNode = ELECreateNode(ELETreeNodeType_EncGroup, &encLayer->rootGroup, node);
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
				xcase ELERootGroupType_PatrolRoute:
					if (gConf.bAllowOldPatrolData)
						newNode = ELECreateNode(ELETreeNodeType_PatrolGroup, NULL, node);
					else
						continue; // Skip this node
#endif
				xdefault:
					assertmsg(0, "Programmer Error: New group was added but does not have a create defined");
			}
			uiNewNode = ui_TreeNodeCreate(uiNode->tree, rootType + 1, NULL, newNode, ELEFillTrackerNode, encLayerDoc, ELEDrawTrackerNode, encLayerDoc, 10);
			uiNewNode->freeF = ELEFreeNode;
			ui_TreeNodeAddChild(uiNode, uiNewNode);
		}		
	}
	else if(node->nodeType == ELETreeNodeType_EncGroup)
	{
		int i, n;
		OldStaticEncounterGroup* parentGroup = node->nodeData;
		n = eaSize(&parentGroup->childList);
		for (i = 0; i < n; i++)
		{
			OldStaticEncounterGroup* staticEncGroup = parentGroup->childList[i];
			if (ELEStaticEncGroupHasEncounters(staticEncGroup))
			{
				ELETreeNode* newNode = ELECreateNode(ELETreeNodeType_EncGroup, staticEncGroup, node);
				UITreeNode* uiNewNode = ui_TreeNodeCreate(uiNode->tree, getStaticEncounterGroupCRC(staticEncGroup), NULL, newNode, ELEFillTrackerNode, encLayerDoc, ELEDrawTrackerNode, encLayerDoc, 10);
				uiNewNode->freeF = ELEFreeNode;
				ui_TreeNodeAddChild(uiNode, uiNewNode);
			}
		}

		n = eaSize(&parentGroup->staticEncList);
		for (i = 0; i < n; i++)
		{
			OldStaticEncounter* staticEnc = parentGroup->staticEncList[i];
			if (staticEnc)
			{
				ELETreeNode* newNode = ELECreateNode(ELETreeNodeType_Encounter, staticEnc, node);
				UITreeNode* uiNewNode = ui_TreeNodeCreate(uiNode->tree, getStaticEncounterCRC(staticEnc), NULL, newNode, ELEFillTrackerNode, encLayerDoc, ELEDrawTrackerNode, encLayerDoc, 10);
				uiNewNode->freeF = ELEFreeNode;
				ui_TreeNodeAddChild(uiNode, uiNewNode);
			}
		}
	}
	else if (node->nodeType == ELETreeNodeType_Encounter)
	{
		OldStaticEncounter* staticEnc = (OldStaticEncounter*)node->nodeData;
		EncounterDef* def = staticEnc->spawnRule;

		// If there isn't a spawn rule, this encounter is in the middle
		// of being created, so just don't worry about it.
		// The tree should refresh again once this encounter's spawn rule
		// is updated anyway.
		if (def)
		{
			int i, n = eaSize(&def->actors);
			for (i = 0; i < n; i++)
			{
				OldActor* actor = def->actors[i];
				ELETreeNode* newNode = ELECreateNode(ELETreeNodeType_Actor, actor, node);
				UITreeNode* uiNewNode = ui_TreeNodeCreate(uiNode->tree, getActorCRC(actor), NULL, newNode, NULL, NULL, ELEDrawTrackerNode, encLayerDoc, 10);
				uiNewNode->freeF = ELEFreeNode;
				ui_TreeNodeAddChild(uiNode, uiNewNode);
			}
		}
	}
	else if (node->nodeType == ELETreeNodeType_Actor)
	{
		// Actors currently don't have children or the fill function
	}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	else if(gConf.bAllowOldPatrolData && (node->nodeType == ELETreeNodeType_PatrolGroup))
	{
		int i, n = eaSize(&encLayer->oldNamedRoutes);
		for(i=0; i<n; i++)
		{
			OldPatrolRoute* route = encLayer->oldNamedRoutes[i];
			ELETreeNode* newNode = ELECreateNode(ELETreeNodeType_Patrol, route, node);
			UITreeNode* uiNewNode = ui_TreeNodeCreate(uiNode->tree, getRouteCRC(route), NULL, newNode, NULL, NULL, ELEDrawTrackerNode, encLayerDoc, 10);
			uiNewNode->freeF = ELEFreeNode;
			ui_TreeNodeAddChild(uiNode, uiNewNode);					
		}
	}
#endif
}

static void ELESelectGroup(OldStaticEncounterGroup* staticEncGroup, EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int i, n = eaSize(&staticEncGroup->staticEncList);

	for (i = 0; i < n; i++)
	{
		OldStaticEncounter* staticEnc = staticEncGroup->staticEncList[i];
		if (staticEnc)
		{
			int whichGrp = eaFind(&encLayer->staticEncounters, staticEnc);
			if (!ELEFindSelectedEncounterObject(encLayerDoc, whichGrp))
				ELESelectEncounter(encLayerDoc, whichGrp, true);
		}
	}

	n = eaSize(&staticEncGroup->childList);
	for (i = 0; i < n; i++)
		ELESelectGroup(staticEncGroup->childList[i], encLayerDoc);
}

static void ELERenameStaticEncGroup(UITextEntry* textEntry, OldStaticEncounterGroup* staticEncGroup)
{
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*)GEGetActiveEditorDocEM("encounterlayer");
	if(encLayerDoc)
	{
		const char* newName = ui_TextEntryGetText(textEntry);
		staticEncGroup->groupName = (char*)allocAddString(newName);
		GESetDocUnsaved(encLayerDoc);
	}
}

void ELEBecomeWeightedGroup(OldStaticEncounterGroup* staticEncGroup)
{
	int i, childCount;
	OldStaticEncounter** childEncs = NULL;
	OldStaticEncounterGroup** childEncGroups = NULL;

	oldencounter_FunctionalGroupGetFunctionalGroupChildren(staticEncGroup, &childEncGroups);
	oldencounter_FunctionalGroupGetStaticEncounterChildren(staticEncGroup, &childEncs);

	childCount = eaSize(&childEncGroups);
	for (i = 0; i < childCount; i++)
		if (!childEncGroups[i]->groupWeight)
			childEncGroups[i]->groupWeight = 1;
	childCount = eaSize(&childEncs);
	for (i = 0; i < childCount; i++)
		if (!childEncs[i]->spawnWeight)
			childEncs[i]->spawnWeight = 1;
	if (oldencounter_StaticEncounterGroupFindWeightedParent(staticEncGroup))
		staticEncGroup->groupWeight = 1;
}

void ele_GroupSetNumToSpawn(OldStaticEncounterGroup* staticEncGroup, int numToSpawn)
{
	bool wasWeighted = oldencounter_StaticEncounterGroupIsWeighted(staticEncGroup);
	staticEncGroup->numToSpawn = numToSpawn;
	if (wasWeighted && !oldencounter_StaticEncounterGroupIsWeighted(staticEncGroup))
		ELERemoveWeightedGroup(staticEncGroup);
	if (!wasWeighted && oldencounter_StaticEncounterGroupIsWeighted(staticEncGroup))
		ELEBecomeWeightedGroup(staticEncGroup);
}

static void ELEStaticEncGroupNumToSpawnChanged(UISlider* slider, bool bFinished, OldStaticEncounterGroup* staticEncGroup)
{
	char tmpStr[1024];
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*)GEGetActiveEditorDocEM("encounterlayer");
	int sliderVal = ui_IntSliderGetValue(slider);
	if (sliderVal != staticEncGroup->numToSpawn)
	{
		ele_GroupSetNumToSpawn(staticEncGroup, sliderVal);
		if (staticEncGroup->numToSpawn)
			sprintf(tmpStr, "%i", staticEncGroup->numToSpawn);
		else
			strcpy(tmpStr, "Spawn All");
		ui_WidgetGroupSetActive((UIWidgetGroup*)&encLayerDoc->uiInfo.weightEntries, oldencounter_StaticEncounterGroupIsFunctional(staticEncGroup));
		ui_LabelSetText(encLayerDoc->uiInfo.spawnCountLabel, tmpStr);
		GESetDocUnsaved(encLayerDoc);
	}
}

static void ELEStaticEncGroupWeightChanged(UITextEntry* entry, OldStaticEncounterGroup* staticEncGroup)
{
	staticEncGroup->groupWeight = atoi(ui_TextEntryGetText(entry));
	GESetCurrentDocUnsaved();
}

static void ELEStaticEncWeightChanged(UITextEntry* entry, OldStaticEncounter* staticEnc)
{
	staticEnc->spawnWeight = atoi(ui_TextEntryGetText(entry));
	GESetCurrentDocUnsaved();
}

void ELECreateEncGroupUI(OldStaticEncounterGroup* staticEncGroup, EncounterLayerEditDoc* encLayerDoc, bool nameonly)
{
	char tmpStr[1024];
	int i, n, currHeight = 0;
	UILabel* sliderDescLabel;
	UISlider* spawnCountSlider;
	OldStaticEncounter** childEncs = NULL;
	OldStaticEncounterGroup** childEncGroups = NULL;

	// A static encounter group has two parts: first, it can make only some of its children spawn.
	// Second, it can control a "zone event".

	// Create a list of this groups children to find the cap for the slider
	oldencounter_FunctionalGroupGetFunctionalGroupChildren(staticEncGroup, &childEncGroups);
	oldencounter_FunctionalGroupGetStaticEncounterChildren(staticEncGroup, &childEncs);

	// Create the modal window for the group properties
	strcpy(tmpStr, "Encounter Group: ");
	if(staticEncGroup->groupName)
		strcat(tmpStr, staticEncGroup->groupName);
	encLayerDoc->uiInfo.groupPropWindow = ui_WindowCreate(tmpStr, g_ui_State.mouseX, g_ui_State.mouseY, 260, 500);
	ui_WindowSetModal(encLayerDoc->uiInfo.groupPropWindow, true);
	ui_WindowShow(encLayerDoc->uiInfo.groupPropWindow);

	currHeight = GETextEntryCreate(NULL, "Group Name", staticEncGroup->groupName, 0, currHeight, 100, 0, 90, GE_VALIDFUNC_NOSPACE, ELERenameStaticEncGroup, staticEncGroup, encLayerDoc->uiInfo.groupPropWindow);

	if (!nameonly)
	{
		// Create a spawn count slider for how many of the children this group should spawn
		sliderDescLabel = ui_LabelCreate("Num to Spawn", 0, currHeight);
		spawnCountSlider = ui_IntSliderCreate(sliderDescLabel->widget.width + UI_HSTEP, currHeight, 100, 0, eaSize(&childEncGroups) + eaSize(&childEncs), staticEncGroup->numToSpawn);
		ui_SliderSetPolicy(spawnCountSlider, UISliderContinuous);
		if (staticEncGroup->numToSpawn)
			sprintf(tmpStr, "%i", staticEncGroup->numToSpawn);
		else
			strcpy(tmpStr, "Spawn All");
		encLayerDoc->uiInfo.spawnCountLabel = ui_LabelCreate(tmpStr, spawnCountSlider->widget.x + spawnCountSlider->widget.width + GE_SPACE, 0);
		ui_SliderSetChangedCallback(spawnCountSlider, ELEStaticEncGroupNumToSpawnChanged, staticEncGroup);
		ui_WindowAddChild(encLayerDoc->uiInfo.groupPropWindow, sliderDescLabel);
		ui_WindowAddChild(encLayerDoc->uiInfo.groupPropWindow, spawnCountSlider);
		ui_WindowAddChild(encLayerDoc->uiInfo.groupPropWindow, encLayerDoc->uiInfo.spawnCountLabel);
		currHeight += spawnCountSlider->widget.height;

		// Add text entries for all the weight listings
		eaSetSize(&encLayerDoc->uiInfo.weightEntries, 0);
		n = eaSize(&childEncGroups);
		for (i = 0; i < n; i++)
		{
			UITextEntry* weightEntry;
			OldStaticEncounterGroup* childGroup = childEncGroups[i];
			sprintf(tmpStr, "%i", childGroup->groupWeight ? childGroup->groupWeight : 1);
			currHeight = GETextEntryCreate(&weightEntry, childGroup->groupName, tmpStr, 0, currHeight + UI_HSTEP, 50, 0, 0, GE_VALIDFUNC_INTEGER, ELEStaticEncGroupWeightChanged, childGroup, encLayerDoc->uiInfo.groupPropWindow);
			weightEntry->widget.offsetFrom = UITopRight;
			eaPush(&encLayerDoc->uiInfo.weightEntries, weightEntry);
		}
		n = eaSize(&childEncs);
		for (i = 0; i < n; i++)
		{
			UITextEntry* weightEntry;
			OldStaticEncounter* childEnc = childEncs[i];
			sprintf(tmpStr, "%i", childEnc->spawnWeight ? childEnc->spawnWeight : 1);
			currHeight = GETextEntryCreate(&weightEntry, childEnc->name, tmpStr, 0, currHeight + UI_HSTEP, 50, 0, 0, GE_VALIDFUNC_INTEGER, ELEStaticEncWeightChanged, childEnc, encLayerDoc->uiInfo.groupPropWindow);
			weightEntry->widget.offsetFrom = UITopRight;
			eaPush(&encLayerDoc->uiInfo.weightEntries, weightEntry);
		}
		ui_WidgetGroupSetActive((UIWidgetGroup*)&encLayerDoc->uiInfo.weightEntries, oldencounter_StaticEncounterGroupIsFunctional(staticEncGroup));
		eaDestroy(&childEncs);
		eaDestroy(&childEncGroups);
	}

	encLayerDoc->uiInfo.groupPropWindow->widget.height = currHeight;
}

static void ELETreeSelectEntry(UITree* tree, EncounterLayerEditDoc* encLayerDoc)
{
	UITreeNode* uiNode = tree->selected;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	ELETreeNode* node = uiNode ? uiNode->contents : NULL;
	encLayerDoc->selectionFromTree = true;

	if(!node)
		return;

	if (node->nodeType == ELETreeNodeType_EncGroup)
	{
		OldStaticEncounterGroup* staticEncGroup = node->nodeData;
		if(staticEncGroup && staticEncGroup != &encLayer->rootGroup)
			ELESelectGroup(staticEncGroup, encLayerDoc);
	}
	else if (node->nodeType == ELETreeNodeType_Encounter)
	{
		OldStaticEncounter* staticEnc = node->nodeData;
		int whichEnc = eaFind(&encLayer->staticEncounters, staticEnc);
		ELESelectEncounter(encLayerDoc, whichEnc, true);
	}
	else if (node->nodeType == ELETreeNodeType_Actor)
	{
		OldActor* actor = node->nodeData;
		ELETreeNode* encNode = node->parent;
		OldStaticEncounter* staticEnc = encNode->nodeData;
		int whichEnc = eaFind(&encLayer->staticEncounters, staticEnc);
		EncounterDef* def = staticEnc->spawnRule;
		int actorIdx = eaFind(&def->actors, actor);
		ELESelectActor(encLayerDoc, whichEnc, actorIdx, true);
	}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	else if (gConf.bAllowOldPatrolData && (node->nodeType == ELETreeNodeType_Patrol))
	{
		OldPatrolRoute* route = node->nodeData;
		int whichRoute = eaFind(&encLayer->oldNamedRoutes, route);
		ELESelectPatrolRoute(encLayerDoc, whichRoute, true);
	}
#endif
	
	ELERefreshUI(encLayerDoc);
	encLayerDoc->selectionFromTree = false;
//	encLayerDoc->lastSelectedNode = tree->selected;
//	tree->selected = NULL;
}

static void ELETreeRenameStaticEnc(UITextEntry* textEntry, EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	OldStaticEncounter *staticEnc = ELEGetLastSelectedEncounter(encLayerDoc);
	int index = eaFind(&encLayer->staticEncounters, staticEnc);
	const char* newName = ui_TextEntryGetText(textEntry);
	if (index != -1)
	{
		int* whichEncs = NULL;
		eaiPush(&whichEncs, index);
		ele_ChangeStaticEncounterName(encLayer, whichEncs, (char*)newName);
		ELERefreshUI(encLayerDoc);
		GESetDocUnsaved(encLayerDoc);
		eaiDestroy(&whichEncs);
	}
	ui_WidgetQueueFree((UIWidget*)encLayerDoc->uiInfo.renameWindow);
	encLayerDoc->uiInfo.renameWindow = NULL;
}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static void ELETreeRenamePatrolRoute(UITextEntry* textEntry, EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	const char* newName = ui_TextEntryGetText(textEntry);
	OldPatrolRoute **routes = ELEGetSelectedPatrolRouteList(encLayerDoc);
	ele_ChangePatrolRouteName(encLayer, routes, (char*)newName);
	ELERefreshUI(encLayerDoc);
	GESetDocUnsaved(encLayerDoc);
	ui_WidgetQueueFree((UIWidget*)encLayerDoc->uiInfo.renameWindow);
	encLayerDoc->uiInfo.renameWindow = NULL;
}
#endif

static void ELETreeRenameNode(ELETreeNode* node, EncounterLayerEditDoc* encLayerDoc)
{
	UITextEntry* renameWidget = NULL;
	char* windowName = NULL;

	if(!node)
		return;

	if (node->nodeType == ELETreeNodeType_EncGroup)
	{
		if(node->nodeData != &encLayerDoc->layerDef->rootGroup)
			ELECreateEncGroupUI(node->nodeData, encLayerDoc, false);
	}
	else if (node->nodeType == ELETreeNodeType_Encounter)
	{
		OldStaticEncounter* staticEnc = node->nodeData;
		renameWidget = ui_TextEntryCreate(staticEnc->name, 0, 0);
		ui_TextEntrySetFinishedCallback(renameWidget, ELETreeRenameStaticEnc, encLayerDoc);
		windowName = "Rename Encounter";
	}
	else if (node->nodeType == ELETreeNodeType_Actor)
	{
		// Add when actor naming is complete
	}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	else if (node->nodeType == ELETreeNodeType_Patrol)
	{
		OldPatrolRoute* route = node->nodeData;
		renameWidget = ui_TextEntryCreate(route->routeName, 0, 0);
		ui_TextEntrySetFinishedCallback(renameWidget, ELETreeRenamePatrolRoute, encLayerDoc);
		windowName = "Rename Point";
	}
#endif

	// Create the modal window, make sure the finish function removes this window upon completion
	if (renameWidget)
	{
		encLayerDoc->uiInfo.renameWindow = ui_WindowCreate(windowName, (g_ui_State.screenWidth * 0.5) - 100, (g_ui_State.screenHeight * 0.5) - 35, 200, 20);
		renameWidget->widget.width = 1;
		renameWidget->widget.widthUnit = UIUnitPercentage;
		renameWidget->widget.rightPad = renameWidget->widget.leftPad = 10;
		ui_TextEntrySetIgnoreSpace(renameWidget, true);
		ui_TextEntrySetSelectOnFocus(renameWidget, true);
		ui_WindowAddChild(encLayerDoc->uiInfo.renameWindow, renameWidget);
		ui_WindowSetModal(encLayerDoc->uiInfo.renameWindow, true);
		ui_WindowSetResizable(encLayerDoc->uiInfo.renameWindow, false);
		ui_WindowSetMovable(encLayerDoc->uiInfo.renameWindow, false);
		ui_WindowShow(encLayerDoc->uiInfo.renameWindow);
		ui_SetFocus(renameWidget);
	}
}

static void ELETreeActivateEntry(UITree* tree, EncounterLayerEditDoc* encLayerDoc)
{
	UITreeNode* uiNode = tree->selected;
	//encLayerDoc->lastSelectedNode;
	ELETreeNode* node = uiNode? uiNode->contents : NULL;

	if(!node)
		return;

	ELETreeRenameNode(node, encLayerDoc);
}

static void ELETreeRightClick(UITree* tree, EncounterLayerEditDoc* encLayerDoc)
{
	UITreeNode* uiNode = tree->selected;
	//encLayerDoc->lastSelectedNode;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	ELETreeNode* node = uiNode ? uiNode->contents : NULL;

	if(!node)
		return;

	ELETreeRenameNode(node, encLayerDoc);
}

void ELESelectInvalidSpawn(UIList* badSpawnList, EncounterLayerEditDoc* encLayerDoc)
{
	char* selBadActor = ui_ListGetSelectedObject(badSpawnList);
	if (selBadActor)
	{
		int actorID, teamSize;
		char* colon;
		OldStaticEncounter* staticEnc;
		char* dupBadActorStr = strdup(selBadActor);
		
		// The team size is the last parameter
		if (!(colon = strrchr(dupBadActorStr, ':')))
		{
			free(dupBadActorStr);
			return;
		}
		*colon = 0; colon++;
		teamSize = atoi(colon);
		
		// Followed by the actor ID
		if (!(colon = strrchr(dupBadActorStr, ':')))
		{
			free(dupBadActorStr);
			return;
		}
		*colon = 0; colon++;
		actorID = atoi(colon);

		// Last is the Static Encounter name
		if ((staticEnc = oldencounter_StaticEncounterFromName(dupBadActorStr)))
		{
			EncounterLayer* encLayer = encLayerDoc->layerDef;
			int groupIndex = eaFind(&encLayer->staticEncounters, staticEnc);
			OldActor* actor = oldencounter_FindDefActorByID(staticEnc->spawnRule, actorID, false);
			int actorIndex = eaFind(&staticEnc->spawnRule->actors, actor);
			ELESelectActor(encLayerDoc, groupIndex, actorIndex, false);
			ELERefreshUI(encLayerDoc);
			ui_IntSliderSetValueAndCallback(encLayerDoc->uiInfo.encounterUI->teamSizeSlider, teamSize);
			ELESnapCamera(encLayerDoc);
		}
		free(dupBadActorStr);
	}
}

void ELEInvalidSpawnName(UIList* badSpawnList, UIListColumn* column, int row, UserData userData, char** output)
{
	estrConcatf(output, "%s", (char*)(*badSpawnList->peaModel)[row]);
}

char* ELEDefaultLayerName(const char* mapName)
{
	static char defaultEncLayer[MAX_PATH];
	char* extension;
	fileRelativePath(mapName, defaultEncLayer);
	extension = strrchr(defaultEncLayer, '.');
	if (extension)
		*extension = '\0';
	strcat(defaultEncLayer, ".encounterlayer");
	return defaultEncLayer;
}

static void ELEToggleVisible(EMMapLayerType *emMap, void *unused, bool vis)
{
	if (g_EncounterMasterLayer && hasServerDir_NotASecurityCheck())
	{
		EncounterLayer* encLayer = oldencounter_FindSubLayer(g_EncounterMasterLayer, emMapLayerGetFilename(emMap));
		if (encLayer)
		{
			encLayer->visible = emMapLayerGetVisible(emMap);
		}
	}
}

static void ELEDrawSpawnLoc(Mat4 spawnMat, bool isSelected, Color selColor, bool bInWorld)
{
	Mat4 camMat;
	F32 distSquared = 0.0f;
	
	PERFINFO_AUTO_START("ELEDrawSpawnLoc",1);
	
	gfxGetActiveCameraMatrix(camMat);
	distSquared = distance3Squared(camMat[3], spawnMat[3]);

	GEDrawPatrolPoint(spawnMat, isSelected, selColor, bInWorld);
	GEDrawActor(NULL, spawnMat, isSelected, selColor, 0, bInWorld, distSquared);
	
	PERFINFO_AUTO_STOP();
}


// Draw everything related to the encounter layer
static void ELEDrawEncounterLayer(EncounterLayer* encLayer, int* selectedEncs, int** selActorsList, int ** selEncounterPointsList, int* selectedPatrols, int** selPatrolsList, int* selectedVols, int* selectedSpawns, int* selectedPoints, U32 teamSize)
{
	int i, n = eaSize(&encLayer->staticEncounters);
	
	PERFINFO_AUTO_START("ELEDrawEncounterLayer",1);
	
	// Refresh omni group defs
	GELoadDisplayDefs(NULL, false);

	for (i = 0; i < n; i++)
	{
		Mat4 encMat;
		int* selectedActors = (i < eaSize(&selActorsList)) ? selActorsList[i] : NULL;
		int* selectedEncounterPoints = (i < eaSize(&selEncounterPointsList)) ? selEncounterPointsList[i] : NULL;
		bool encSelected = (eaiFind(&selectedEncs, i) != -1);
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[i];
		quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
		GEDrawEncounter(staticEnc->spawnRule, encMat, selectedActors, selectedEncounterPoints, encSelected, false, false, false, teamSize, true, false);
	}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	if (gConf.bAllowOldPatrolData)
	{
		// Draw all the named patrol routes on the map
		n = eaSize(&encLayer->oldNamedRoutes);
		for (i = 0; i < n; i++)
		{
			Mat4 pointMat, lastPointMat;
			OldPatrolRoute* route = encLayer->oldNamedRoutes[i];
			bool patSelected = (eaiFind(&selectedPatrols, i) != -1);
			int* selectedPatrolPoints = (i < eaSize(&selPatrolsList)) ? selPatrolsList[i] : NULL;
			int j, numPoints = eaSize(&route->patrolPoints);
			for (j = 0; j < numPoints; j++)
			{
				bool isSelected = (eaiFind(&selectedPatrolPoints, j) != -1);
				quatVecToMat4(route->patrolPoints[j]->pointRot, route->patrolPoints[j]->pointLoc, pointMat);
				GEDrawPatrolPoint(pointMat, (patSelected || isSelected), GE_SEL_COLOR, true);

				// Draw a line to connect adjacent patrol points
				pointMat[3][1] += 0.1;
				if (j > 0) gfxDrawLine3DWidth(pointMat[3], lastPointMat[3], ColorRed, ColorRed, 2);
				copyMat4(pointMat, lastPointMat);
			}
		}
	}
#endif

	PERFINFO_AUTO_STOP();
}

static void ELEDrawInactiveLayer(EMMapLayerType* map_layer, void *unused)
{
	if (g_EncounterMasterLayer && hasServerDir_NotASecurityCheck())
	{
		EncounterLayer* encLayer = oldencounter_FindSubLayer(g_EncounterMasterLayer, emMapLayerGetFilename(map_layer));
		if (encLayer)
		{
			bool bDrawLayer = encLayer->visible;
			EMEditorDoc* pActiveEditorDoc = GEGetActiveEditorDocEM(NULL);

			// If this layer is currently open and active, don't draw it here
			if (bDrawLayer && eaFind(&s_EncounterLayerEditor.open_docs, pActiveEditorDoc) != -1 && ((EncounterLayerEditDoc*)pActiveEditorDoc)->layerDef == encLayer){
				bDrawLayer = false;
			}

			if (bDrawLayer)
				ELEDrawEncounterLayer(encLayer, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, MAX_TEAM_SIZE);
		}
	}
}

static void ELEEditLayer(EMMapLayerType *layer, void *unused)
{
	emOpenFile(emMapLayerGetFilename(layer));
}

// JAMES TODO: see below
static EMMapLayerType* ELEAddLayerToWorldEditor(ELEWorldEditorInterfaceEM* weInterface, const char* filename)
{
	EMMapLayerType* map_layer = emMapLayerCreate(filename, getFileNameConst(filename), "Encounters", NULL, ELEToggleVisible, ELEDrawInactiveLayer);
	eaPush(&weInterface->mapLayers, map_layer);
	emMapLayerAdd(map_layer);

	return map_layer;
}

// JAMES TODO: Separate out logic from EM-specific code, move half of this function to EM.c file?
void ELESetupWorldEditorInterface(ELEWorldEditorInterfaceEM* weInterface, bool bMapResetUnused)
{
	int i, numLayers;

	numLayers = eaSize(&weInterface->mapLayers);
	for (i = 0; i < numLayers; ++i)
	{
		emMapLayerRemove(weInterface->mapLayers[i]);
		emMapLayerDestroy(weInterface->mapLayers[i]);
	}
	eaSetSize(&weInterface->mapLayers, 0);

	// All maps need to have a default layer
	if (g_EncounterMasterLayer && (numLayers = eaSize(&g_EncounterMasterLayer->encLayers)))
	{
		for (i = 0; i < numLayers; i++)
		{
			EncounterLayer* currLayer = g_EncounterMasterLayer->encLayers[i];
			EMMapLayerType* map_layer = emMapLayerCreate(currLayer->pchFilename, getFileNameConst(currLayer->pchFilename), "Encounters", NULL, ELEToggleVisible, ELEDrawInactiveLayer);

			emMapLayerMenuItemAdd(map_layer, "Edit", ELEEditLayer);
			eaPush(&weInterface->mapLayers, map_layer);
			emMapLayerAdd(map_layer);
		}
	}
	else if (gConf.bAllowOldEncounterData)
	{
		const char* mapFileName = zmapGetFilename(NULL);

		if (mapFileName)
		{
			char* layerName = ELEDefaultLayerName(mapFileName);
			EMMapLayerType* map_layer = emMapLayerCreate(layerName, getFileName(layerName), "Encounters", NULL, ELEToggleVisible, ELEDrawInactiveLayer);
		
			emMapLayerMenuItemAdd(map_layer, "Edit", ELEEditLayer);
			eaPush(&weInterface->mapLayers, map_layer);
			emMapLayerAdd(map_layer);
		}
	}
}


char* ELELayerNameFromFilename(const char *filename)
{
	char layerName[GE_NAMELENGTH_MAX];
	char *tmp;

	fileRelativePath(filename, layerName);
	for (tmp = layerName; tmp && *tmp; tmp++)
		if (*tmp == '/')
			*tmp = '.';

	return strdup(layerName);
}

static char* ELECreateUniqueEncounterLayerFileName(const char* desiredFileName)
{
	char desiredNameNoExt[GE_NAMELENGTH_MAX];
	static char nextName[GE_NAMELENGTH_MAX];
	char *layerName = NULL;
	int counter = 1;
	char *tmp;

	if (!desiredFileName)
		return NULL;
	
	fileRelativePath(desiredFileName, desiredNameNoExt);
	tmp = strrchr(desiredNameNoExt, '.');
	if (tmp) *tmp = '\0';

	sprintf(nextName, "%s.encounterlayer", desiredNameNoExt);
	layerName = ELELayerNameFromFilename(nextName);

	while (oldencounter_FindSubLayer(g_EncounterMasterLayer, nextName) || resGetInfo("EncounterLayer", layerName))
	{
		sprintf(nextName, "%s_%i.encounterlayer", desiredNameNoExt, counter);
		free(layerName);
		layerName = ELELayerNameFromFilename(nextName);
		counter++;
	}
	free(layerName);
	return nextName;
}

void MDEEventLogSendFilterToServer(EventEditor *editor, void *unused)
{
	char *estrTemp = NULL; 
	estrStackCreate(&estrTemp);
	eventeditor_GetEventStringEscaped(editor, &estrTemp);
	ServerCmd_EventLogSetFilter(estrTemp);
	estrDestroy(&estrTemp);
}

void ELESetupUI(EncounterLayerEditDoc* encLayerDoc)
{
	int currX, currY;
	UIButton* button;
	UICheckButton* checkButton;
	char tmpStr[64];
	EncounterLayerEditorUI* uiInfo = &encLayerDoc->uiInfo;

	// Main properties window for all the following properties
	// Contains an expander group that holds an expander for all the different properties
	uiInfo->propertiesWindow = ui_WindowCreate("Properties", 100000, 0, 415, 500);
	uiInfo->propExpGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition((UIWidget*)uiInfo->propExpGroup, 0, 0);
	ui_WidgetSetDimensionsEx((UIWidget*)uiInfo->propExpGroup, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(uiInfo->propertiesWindow, uiInfo->propExpGroup);

	// Setup the tree window
	uiInfo->layerTreeWindow = ui_WindowCreate("Encounter Tracker", 0, 100, 200, 400);

	// Add the encounter layer properties to the StaticEncWindow
	sprintf(tmpStr, "%i", encLayerDoc->layerDef->layerLevel);
	currY = GETextEntryCreate(NULL, "Level", tmpStr, 0, 0, 50, 0, 40, GE_VALIDFUNC_LEVEL, ELELayerLevelChanged, encLayerDoc, uiInfo->layerTreeWindow);
	checkButton = ui_CheckButtonCreate(90 + UI_HSTEP, 0, "Lockout", encLayerDoc->layerDef->useLockout);
	ui_CheckButtonSetToggledCallback(checkButton, ELELayerLockoutChanged, encLayerDoc);
	ui_WindowAddChild(uiInfo->layerTreeWindow, checkButton);

	// Encounter Tree
	uiInfo->layerTree = ui_TreeCreate(0, 0, 1.0, 1.0);
	uiInfo->layerTree->root.contents = NULL;
	ui_TreeSetContextCallback(uiInfo->layerTree, ELETreeRightClick, encLayerDoc);
	ui_TreeSetActivatedCallback(uiInfo->layerTree, ELETreeActivateEntry, encLayerDoc);
	ui_TreeSetSelectedCallback(uiInfo->layerTree, ELETreeSelectEntry, encLayerDoc);
	ui_TreeNodeSetFillCallback(&uiInfo->layerTree->root, ELEFillTrackerNode, encLayerDoc);
	uiInfo->layerTree->widget.topPad = currY + UI_HSTEP;
	uiInfo->layerTree->widget.bottomPad = UI_HSTEP;
	uiInfo->layerTree->widget.leftPad = uiInfo->layerTree->widget.rightPad = UI_HSTEP;
	uiInfo->layerTree->widget.widthUnit = uiInfo->layerTree->widget.heightUnit = UIUnitPercentage;
	ui_TreeNodeExpand(&uiInfo->layerTree->root);
	ui_WindowAddChild(uiInfo->layerTreeWindow, uiInfo->layerTree);

	// Creation toolbar
	currX = 0;
	uiInfo->createToolbar = ui_WindowCreate("Create New", 0, 0, 200, 25);
	button = ui_ButtonCreate("Encounter", currX, 0, ELECreateEncounterButton, encLayerDoc);
	ui_WindowAddChild(uiInfo->createToolbar, button);
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	if (gConf.bAllowOldPatrolData)
	{
		currX += button->widget.width;
		button = ui_ButtonCreate("Patrol Point", currX, 0, ELECreatePatrolButton, encLayerDoc);
		ui_WindowAddChild(uiInfo->createToolbar, button);
	}
#endif
	uiInfo->createToolbar->widget.width = currX + button->widget.width;

	// Encounter properties
	GERefreshPatrolRouteList();
	uiInfo->encounterUI = GEEncounterPropUICreate(encLayerDoc, 0, ELEEncounterNameChanged, ELEEncAmbushCheckBoxChanged, ELESpawnPerPlayerCheckBoxChanged, ELEEncSnapToGroundCheckBoxChanged, ELEDoNotDespawnCheckBoxChanged, ELEDynamicSpawnTypeChanged, ELESpawnCondChanged, ELESuccessCondChanged, ELEFailCondChanged, ELESuccessActionChanged, ELEFailActionChanged, ELEWaveCondChanged, ELEWaveIntervalChanged, ELEWaveMinDelayChanged, ELEWaveMaxDelayChanged, ELESpawnRadiusChanged, ELELockoutRadiusChanged, ELETeamSizeChanged, encLayerDoc->activeTeamSize, NULL, ELECritterGroupChanged, ELEFactionChanged, ELEGangIDChanged, ELESpawnChanceChanged, ELEPatrolChanged, &g_GEPatrolRouteNames, ELEEncMinLevelChanged, ELEEncMaxLevelChanged, ELEEncUsePlayerLevelCheckBoxChanged, NULL, ELEEncRespawnTimeChanged, NULL);

	// Actor Properties
	uiInfo->actorUI = GEActorPropUICreate(encLayerDoc, ELEActorNameChanged, ELEActorCritterSelected, ELEActorFSMChanged, ELEActorFSMOpen, ELEActorSpawnEnabledChanged, ELEActorRankChanged, ELEActorSubRankChanged, ELEActorFactionChanged, NULL, NULL, NULL, NULL, ELEActorContactChanged, ELEActorDisplayNameChanged, ELEActorSpawnWhenChanged, ELEActorInteractCondChanged, ELEActorBossBarEnabledChanged, ELEActorVarChanged, ELEActorVarMessageChanged, ELEActorSpawnAnimChanged, ELEModifySelectedActors);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	if (gConf.bAllowOldPatrolData) 
	{
		UILabel *widgetLabel;
		// Patrol Window
		uiInfo->patrolExpander = ui_ExpanderCreate("Patrol Route Properties", 0);
		GETextEntryCreate(&uiInfo->patrolNameEntry, "Route Name", "", 0, 0, 150, 0, 80, GE_VALIDFUNC_NOSPACE, ELEPatrolRouteNameChanged, encLayerDoc, uiInfo->patrolExpander);
		widgetLabel = ui_LabelCreate("Route Type", 0, uiInfo->patrolNameEntry->widget.height + UI_HSTEP);
		uiInfo->patrolTypeCombo = ui_ComboBoxCreate(80, uiInfo->patrolNameEntry->widget.height + UI_HSTEP, 150, NULL, NULL, NULL);
		ui_ComboBoxSetEnum(uiInfo->patrolTypeCombo, OldPatrolRouteTypeEnum, ELEPatrolRouteTypeSelected, encLayerDoc);
		ui_ExpanderAddChild(uiInfo->patrolExpander, widgetLabel);
		ui_ExpanderAddChild(uiInfo->patrolExpander, uiInfo->patrolTypeCombo);

		// Set the patrol expander height
		uiInfo->patrolExpander->openedHeight = uiInfo->patrolTypeCombo->widget.y + uiInfo->patrolTypeCombo->widget.height;
		ui_ExpanderSetOpened(uiInfo->patrolExpander, true);
	}
#endif

	// Invalid Spawn Position Window - default is closed
	{
		UIList* badSpawnList;
#if !PLATFORM_CONSOLE
		beaconReadInvalidSpawnFile();
#endif
		uiInfo->invalidSpawnWindow = ui_WindowCreate("Invalid Spawn Locations", 400, 0, 200, 300);
		uiInfo->invalidSpawnWindow->show = false;
		badSpawnList = ui_ListCreate(NULL, beaconGetInvalidSpawns(), 16);
		ui_ListAppendColumn(badSpawnList, ui_ListColumnCreate(UIListTextCallback, "StaticEnc:ActorID:TeamSize", (intptr_t)ELEInvalidSpawnName, NULL));
		ui_WidgetSetDimensionsEx((UIWidget*)badSpawnList, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
		ui_ListSetActivatedCallback(badSpawnList, ELESelectInvalidSpawn, encLayerDoc);
		ui_WindowAddChild(uiInfo->invalidSpawnWindow, badSpawnList);
	}

	// Event Tool menu - default is closed
	{
		if (!s_EncounterLayerEventDebugEditor)
			s_EncounterLayerEventDebugEditor = eventeditor_Create(NULL, MDEEventLogSendFilterToServer, NULL, false);
		
		uiInfo->eventEditorWindow = eventeditor_GetWindow(s_EncounterLayerEventDebugEditor);
		if (uiInfo->eventEditorWindow)
			ui_WidgetSetTextString(UI_WIDGET(uiInfo->eventEditorWindow), "Event Debug Filter");
	}

	// Create the mouseover info window
	uiInfo->mouseoverInfoWindow = ui_WindowCreate("Object Info", 100000, 100000, 180, 100);
	uiInfo->mouseoverInfoObjectName = ui_LabelCreate("", 0, 0);
	uiInfo->mouseoverInfoObjectLayer = ui_LabelCreate("", 0, uiInfo->mouseoverInfoObjectName->widget.height + UI_STEP);
	ui_WindowAddChild(uiInfo->mouseoverInfoWindow, uiInfo->mouseoverInfoObjectName);
	ui_WindowAddChild(uiInfo->mouseoverInfoWindow, uiInfo->mouseoverInfoObjectLayer);
}

// Name should be the name of the file which is somehow associated with the map
EncounterLayer* ELENewEncounterLayer(const char* name, EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* layerDef;
	const char* mapFileName = zmapGetFilename(NULL);
	char subLayerName[MAX_PATH];
	char *subLayerFileName;
	char *tmp;

	if (name)
	{
		char buffer[MAX_PATH];
		fileRelativePath(g_EncounterMasterLayer->mapFileName, buffer);
		getDirectoryName(buffer);
		if (!strEndsWith(buffer, "/"))
			strcat(buffer, "/");
		strcat(buffer, getFileNameConst(name));
		if (!strEndsWith(buffer, ".encounterlayer"))
			strcat(buffer, ".encounterlayer");
		subLayerFileName = buffer;
	}
	else if (g_EncounterMasterLayer)
	{
		subLayerFileName = ELEDefaultLayerName(g_EncounterMasterLayer->mapFileName);
	}
	else
	{
		// Something is terribly wrong
		Errorf("Error: No map loaded!");
		return NULL;
	}

	subLayerFileName = ELECreateUniqueEncounterLayerFileName(subLayerFileName);

	strcpy(subLayerName, subLayerFileName);
	for (tmp = subLayerName; *tmp != 0; tmp++)
		if (*tmp == '/')
			*tmp = '.';

	layerDef = StructCreate(parse_EncounterLayer);
	layerDef->pchFilename = allocAddFilename(subLayerFileName);
	layerDef->name = (char*)allocAddString(subLayerName);
	eaPush(&g_EncounterMasterLayer->encLayers, layerDef);

	encLayerDoc->docDefinition = &ELEDocDefinition;
	encLayerDoc->origLayerDef = oldencounter_SafeCloneLayer(layerDef);

	encLayerDoc->layerDef = layerDef;
	encLayerDoc->activeTeamSize = 1;

	encLayerDoc->placementTool.rotGizmo = RotateGizmoCreate();
	encLayerDoc->placementTool.transGizmo = TranslateGizmoCreate();
	encLayerDoc->placementTool.collideWithWorld = 1;
	GEPlacementToolReset(&encLayerDoc->placementTool, zerovec3, zerovec3);

	return layerDef;
}

// Name should be the name of the file which is somehow associated with the map
void ELEOpenEncounterLayer(EncounterLayer *layerDef, EncounterLayerEditDoc* encLayerDoc)
{
	encLayerDoc->docDefinition = &ELEDocDefinition;
	encLayerDoc->origLayerDef = oldencounter_SafeCloneLayer(layerDef);
	encLayerDoc->layerDef = layerDef;
	encLayerDoc->activeTeamSize = 1;

	encLayerDoc->placementTool.rotGizmo = RotateGizmoCreate();
	encLayerDoc->placementTool.transGizmo = TranslateGizmoCreate();
	encLayerDoc->placementTool.collideWithWorld = 1;
	GEPlacementToolReset(&encLayerDoc->placementTool, zerovec3, zerovec3);
	langMakeEditorCopy(parse_EncounterLayer, layerDef, false);
	langMakeEditorCopy(parse_EncounterLayer, encLayerDoc->origLayerDef, false);
}

void ELECloseEncounterLayer(EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer *dictLayer = RefSystem_ReferentFromString(g_EncounterLayerDictionary, encLayerDoc->layerDef->name);

	RotateGizmoDestroy(encLayerDoc->placementTool.rotGizmo);
	TranslateGizmoDestroy(encLayerDoc->placementTool.transGizmo);
	REMOVE_HANDLE(encLayerDoc->newEncDef);
	REMOVE_HANDLE(encLayerDoc->newCritter);

	eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);

	StructDestroy(parse_EncounterLayer, encLayerDoc->origLayerDef);

	// Reload layer from dictionary to clear any edits since the last save
	// (otherwise it will draw incorrectly)
	oldencounter_LoadToMasterLayer(dictLayer);
}


static void ELECancelAction(EncounterLayerEditDoc* encLayerDoc)
{
	GEPlacementTool* placementTool = &encLayerDoc->placementTool;
	if (!GEPlacementToolCancelAction(placementTool))
	{
		eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);
		ELERefreshUI(encLayerDoc);
	}
}

void ELEGotFocus(EncounterLayerEditDoc* encLayerDoc)
{
	ELERefreshStaticEncounters(encLayerDoc, true);
	ELERefreshUI(encLayerDoc);
	if (encLayerDoc->uiInfo.renameWindow)
	{
		ui_WindowClose(encLayerDoc->uiInfo.renameWindow);
		encLayerDoc->uiInfo.renameWindow = NULL;
	}
}

bool ele_SaveEncounterLayer(EncounterLayer* encLayer, char* fileOverride)
{
	int saved;

	saved = ParserWriteTextFileFromSingleDictionaryStruct(fileOverride ? fileOverride : encLayer->pchFilename, g_EncounterLayerDictionary, encLayer, 0, 0);

	if(!saved)
		Alertf("Save failed: unable to write file!");

	return (bool) saved;
}

EMTaskStatus ELESaveEncounterLayer(EncounterLayerEditDoc* editDoc)
{
	EncounterLayer* layer = editDoc->layerDef;
	EMTaskStatus status;
	if (emHandleSaveResourceState(editDoc->emDoc.editor, layer->name, &status)) {
		return status;
	}

	// --- Pre-save validation goes here ---

	ELEStoreStaticEncounterHeights(editDoc->layerDef);

	// Fixup the layer's messages
	ELEFixupLayerMessages(editDoc->layerDef);

	// We may have changed actor variables; refresh the encounters for the editor
	if (editDoc){
		ELERefreshStaticEncounters(editDoc, true);
	}

	resSetDictionaryEditMode(g_EncounterLayerDictionary, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// Aquire the lock
	if (!resGetLockOwner(g_EncounterLayerDictionary, layer->name)) {
		// Don't have lock, so ask server to lock and go into locking state
		emSetResourceState(editDoc->emDoc.editor, layer->name, EMRES_STATE_LOCKING_FOR_SAVE);
		resRequestLockResource(g_EncounterLayerDictionary, layer->name, layer);
		return EM_TASK_INPROGRESS;
	}
	// Get here if have the lock

	// Send save to server
	emSetResourceStateWithData(editDoc->emDoc.editor, layer->name, EMRES_STATE_SAVING, editDoc);
	resRequestSaveResource(g_EncounterLayerDictionary, layer->name, layer);
	return EM_TASK_INPROGRESS;
}

// Snap the actor to stand on the nearest thing in the world.
// The first distance tries to make sure the actor isn't buried.  If it doesn't encounter any terrain,
// use the second snap-to distance to try to find anything for the actor to stand on.
#define FIRST_ACTOR_SNAP_TO_DIST 7
#define SECOND_ACTOR_SNAP_TO_DIST 20

void ELESnapActorsToGround(SA_PARAM_NN_VALID OldStaticEncounter* staticEnc, SA_PARAM_NN_VALID EncounterLayer* encLayer)
{
	int i, n;
	EncounterDef* spawnRule;

	// Update the spawn rule.  Not really ideal to have to do check this every tick, but we
	// may have an encounter that was just created from the library, so it won't yet
	// have a spawn rule.
	if(!staticEnc->spawnRule)
		oldencounter_UpdateStaticEncounterSpawnRule(staticEnc, encLayer);

	spawnRule = staticEnc->spawnRule;

	// Spawn rule shouldn't ever be NULL here, but this makes the compiler happy
	n = spawnRule ? eaSize(&spawnRule->actors) : 0;
	for(i=0; i<n; i++)
	{
		OldActor* actor = spawnRule->actors[i];
		Mat4 encMat;
		Mat4 actorMat;
		Mat4 relActorMat;
		S32 floorFound = false;

		quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
		GEFindActorMat(actor, encMat, actorMat);

		worldSnapPosToGround(PARTITION_CLIENT, actorMat[3], FIRST_ACTOR_SNAP_TO_DIST, -FIRST_ACTOR_SNAP_TO_DIST, &floorFound);

		if(!floorFound)
			worldSnapPosToGround(PARTITION_CLIENT, actorMat[3], SECOND_ACTOR_SNAP_TO_DIST, -SECOND_ACTOR_SNAP_TO_DIST, &floorFound);

		ELEConvertWorldMatToActorMatInEncounter(staticEnc, actorMat, relActorMat);

		ele_MoveStaticEncounterActor(staticEnc, i, relActorMat, 0);
	}
}

void ele_PlaceStaticEncounter(EncounterLayer* encLayer, EncounterDef* def, Mat4 encMat, bool snapToGround)
{
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) GEGetActiveEditorDocEM("encounterlayer");
	OldStaticEncounter* staticEnc = StructCreate(parse_OldStaticEncounter);

	staticEnc->name = (char*)allocAddString(ELECreateUniqueStaticEncName(def->name));
	staticEnc->layerParent = encLayer;
	oldencounter_AddStaticEncounterReference(staticEnc);
	SET_HANDLE_FROM_STRING(g_EncounterDictionary, def->name, staticEnc->baseDef);
	copyVec3(encMat[3], staticEnc->encPos);
	mat3ToQuat(encMat, staticEnc->encRot);
	staticEnc->frozen = false;

	if(snapToGround && !staticEnc->bNoSnapToGround)
	{
		ELESnapActorsToGround(staticEnc, encLayer);
	}

	eaPush(&encLayer->staticEncounters, staticEnc);
	eaPush(&encLayer->rootGroup.staticEncList, staticEnc);
	staticEnc->groupOwner = &encLayer->rootGroup;
}

void ELEConvertWorldMatToActorMatInEncounter(OldStaticEncounter* staticEnc, Mat4 actorWorldMat, Mat4 newActorMat)
{
	Vec3 encOffset;
	Mat3 staticEncMat, invStaticEncMat;

	quatToMat(staticEnc->encRot, staticEncMat);
	invertMat3Copy(staticEncMat, invStaticEncMat);
	mulMat3(invStaticEncMat, actorWorldMat, newActorMat);
	subVec3(actorWorldMat[3], staticEnc->encPos, encOffset);
	mulVecMat3(encOffset, invStaticEncMat, newActorMat[3]);
}

void ele_PlaceActorInEncounterLayer(EncounterLayer* encLayer, int whichEncounter, CritterDef* critterDef, Mat4 critterMat)
{
	OldStaticEncounter* staticEnc;
	Mat4 newActorMat;

	// Find the correct static encounter to add the actor to
	if (whichEncounter >= 0 && whichEncounter < eaSize(&encLayer->staticEncounters))
		staticEnc = encLayer->staticEncounters[whichEncounter];
	else
	{
		char layerName[1024];
		getFileNameNoExt(layerName, encLayer->pchFilename);
		staticEnc = StructCreate(parse_OldStaticEncounter);
		staticEnc->name = (char*)allocAddString(ELECreateUniqueStaticEncName(layerName));
		oldencounter_AddStaticEncounterReference(staticEnc);
		copyVec3(critterMat[3], staticEnc->encPos);
		mat3ToQuat(critterMat, staticEnc->encRot);
		eaPush(&encLayer->staticEncounters, staticEnc);
		eaPush(&encLayer->rootGroup.staticEncList, staticEnc);
		staticEnc->groupOwner = &encLayer->rootGroup;
	}

	// Now add the critter to the def override
	if (!staticEnc->defOverride)
		staticEnc->defOverride = StructCreate(parse_EncounterDef);

	// Figure out what the stored actor mat should be
	ELEConvertWorldMatToActorMatInEncounter(staticEnc, critterMat, newActorMat);

	GEEncounterDefAddActor(staticEnc->defOverride, critterDef, newActorMat, encounterdef_NextUniqueActorOverrideID(staticEnc->defOverride));
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
void ele_PlacePatrolPoint(EncounterLayer* encLayer, int whichRoute, Mat4 newMat)
{
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) GEGetActiveEditorDocEM("encounterlayer");
	OldPatrolRoute* patrolRoute;
	OldPatrolPoint* patrolPoint = StructCreate(parse_OldPatrolPoint);
	if (whichRoute >= 0 && whichRoute < eaSize(&encLayer->oldNamedRoutes))
		patrolRoute = encLayer->oldNamedRoutes[whichRoute];
	else
	{
		OldStaticEncounter* encounter;
		char* newName = ELECreateUniquePatrolRouteName(encLayer, "NewRoute");
		patrolRoute = StructCreate(parse_OldPatrolRoute);
		patrolRoute->routeName = (char*)allocAddString(newName);
		eaPush(&encLayer->oldNamedRoutes, patrolRoute);

		// If the last encounter selected has no route, attach this route to it
		encounter = ELEGetLastSelectedEncounter(encLayerDoc);
		if(encounter && (NULL == encounter->patrolRouteName))
		{
			encounter->patrolRouteName = (char*)allocAddString(newName);
		}
	}
	mat3ToQuat(newMat, patrolPoint->pointRot);
	copyVec3(newMat[3], patrolPoint->pointLoc);
	eaPush(&patrolRoute->patrolPoints, patrolPoint);
}

void ele_CreatePatrolRoute(EncounterLayer* encLayer)
{
	OldPatrolRoute* newRoute = StructCreate(parse_OldPatrolRoute);
	char* newName = ELECreateUniquePatrolRouteName(encLayer, "NewRoute");
	newRoute->routeName = (char*)allocAddString(newName);
	eaPush(&encLayer->oldNamedRoutes, newRoute);
}

void ele_MovePatrolPoint(OldPatrolRoute* patrolRoute, OldPatrolPoint* srcPoint, Mat4 newMat, bool makeCopy)
{
	OldPatrolPoint* patrolPoint;
	if (!makeCopy)
		patrolPoint = srcPoint;
	else
	{
		int srcIndex = eaFind(&patrolRoute->patrolPoints, srcPoint);
		patrolPoint = StructAlloc(parse_OldPatrolPoint);
		StructCopyAll(parse_OldPatrolPoint, srcPoint, patrolPoint);
		eaInsert(&patrolRoute->patrolPoints, patrolPoint, srcIndex + 1);
	}
	mat3ToQuat(newMat, patrolPoint->pointRot);
	copyVec3(newMat[3], patrolPoint->pointLoc);
}
#endif

void ele_MovePoint(EncounterLayer* encLayer, OldNamedPointInEncounter* srcPoint, Mat4 newPos, bool makeCopy)
{
	OldNamedPointInEncounter* point;
	if (!makeCopy)
	{
		point = srcPoint;
		copyMat4(newPos, point->relLocation);
	}
}

void ele_MoveStaticEncounter(EncounterLayer* encLayer, int whichEncounter, Mat4 newMat, bool makeCopy, bool snapToGround)
{
	if (whichEncounter >= 0 && whichEncounter < eaSize(&encLayer->staticEncounters))
	{
		OldStaticEncounter* staticEnc;
		EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) GEGetActiveEditorDocEM("encounterlayer");
		if (!makeCopy)
			staticEnc = encLayer->staticEncounters[whichEncounter];
		else
		{
			ele_PasteStaticEncounter(encLayer, encLayer->staticEncounters[whichEncounter]);
			staticEnc = encLayer->staticEncounters[eaSize(&encLayer->staticEncounters) - 1];
		}
		mat3ToQuat(newMat, staticEnc->encRot);
		copyVec3(newMat[3], staticEnc->encPos);

		if(snapToGround && !staticEnc->bNoSnapToGround)
		{
			ELESnapActorsToGround(staticEnc, encLayer);
		}
	}
}

// Apply the tool to find the changes that need to be made to the placed items
static void ELEPasteSelected(EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	GEPlacementTool* placementTool = &encLayerDoc->placementTool;

	if (GEPlacementToolIsInPlacementMode(placementTool))
	{
		Mat4 newObjMat, encMat, invEncMat, actorMat, relActorMat;
		if (placementTool->createNew)
		{
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
			int* selRouteList = ELEGetSelectedPatrolRouteIndexList(encLayerDoc);
#endif
			int newSize, selListSize;
			int whichGroup;

			copyMat4(encLayerDoc->newObjectMat, newObjMat);
			GEPlacementToolApply(placementTool, newObjMat);
			
			// All new objects should select the most recent thing placed
			if (IS_HANDLE_ACTIVE(encLayerDoc->newEncDef))
			{
				EncounterDef *newEncDef = GET_REF(encLayerDoc->newEncDef);
				if (newEncDef)
				{
					ele_PlaceStaticEncounter(encLayer, newEncDef, newObjMat, !placementTool->useGizmos);
					if ((newSize = eaSize(&encLayer->staticEncounters)))
						ELESelectEncounter(encLayerDoc, newSize - 1, false);
				}
				REMOVE_HANDLE(encLayerDoc->newEncDef);
			}
			else if (IS_HANDLE_ACTIVE(encLayerDoc->newCritter))
			{
				CritterDef *newCritterDef = GET_REF(encLayerDoc->newCritter);
				if (newCritterDef)
				{
					int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
					selListSize = eaiSize(&selEncList);
					whichGroup = (selListSize > 0) ? selEncList[selListSize - 1] : -1;
					ele_PlaceActorInEncounterLayer(encLayer, whichGroup, newCritterDef, newObjMat);
					if (whichGroup == -1 && (newSize = eaSize(&encLayer->staticEncounters)))
						ELESelectEncounter(encLayerDoc, newSize - 1, false);
				}
				REMOVE_HANDLE(encLayerDoc->newCritter);
			}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
			else if(encLayerDoc->typeToCreate == GEObjectType_PatrolRoute)
			{
				selListSize = eaiSize(&selRouteList);
				whichGroup = (selListSize > 0) ? selRouteList[selListSize - 1] : -1;
				ele_PlacePatrolPoint(encLayer, whichGroup, newObjMat);
				if ((whichGroup == -1) && (newSize = eaSize(&encLayer->oldNamedRoutes)))
					ELESelectPatrolRoute(encLayerDoc, newSize - 1, false);
			}
#endif
			placementTool->createNew = 0;
		}
		else if (placementTool->moveSelected)
		{
			bool makeCopy = GEPlacementToolShouldCopy(placementTool);
			int i, n;

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
			Mat4 pointMat;

			// Patrol Routes
			n = eaSize(&encLayer->oldNamedRoutes);
			for (i = 0; i < n; i++)
			{
				OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[i];
				if (ELEFindSelectedPatrolRouteObject(encLayerDoc, i))
				{
					if (makeCopy)
					{
						int newRouteIndex;
						int j, numPoints = eaSize(&patrolRoute->patrolPoints);
						ele_CreatePatrolRoute(encLayer);
						newRouteIndex = eaSize(&encLayer->oldNamedRoutes) - 1;
						for (j = 0; j < numPoints; j++)
						{
							OldPatrolPoint* patrolPoint = patrolRoute->patrolPoints[j];
							quatVecToMat4(patrolPoint->pointRot, patrolPoint->pointLoc, pointMat);
							GEPlacementToolApply(placementTool, pointMat);
							ele_PlacePatrolPoint(encLayer, newRouteIndex, pointMat);
						}
					}
					else
					{
						int j, numPoints = eaSize(&patrolRoute->patrolPoints);
						for (j = 0; j < numPoints; j++)
						{
							OldPatrolPoint* patrolPoint = patrolRoute->patrolPoints[j];
							quatVecToMat4(patrolPoint->pointRot, patrolPoint->pointLoc, pointMat);
							GEPlacementToolApply(placementTool, pointMat);
							ele_MovePatrolPoint(patrolRoute, patrolPoint, pointMat, false);
						}
					}
				}
				else
				{
					OldPatrolPoint** selPatrolPoints = ELEGetSelectedPatrolPointList(encLayerDoc, i);
					int j, numPoints = eaSize(&selPatrolPoints);
					for (j = 0; j < numPoints; j++)
					{
						OldPatrolPoint* patrolPoint = selPatrolPoints[j];
						quatVecToMat4(patrolPoint->pointRot, patrolPoint->pointLoc, pointMat);
						GEPlacementToolApply(placementTool, pointMat);
						ele_MovePatrolPoint(patrolRoute, patrolPoint, pointMat, makeCopy);
					}
				}
			}
#endif
			// Encounters
			n = eaSize(&encLayer->staticEncounters);
			for (i = 0; i < n; i++)
			{
				OldStaticEncounter* staticEnc = encLayer->staticEncounters[i];
				quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
				invertMat4Copy(encMat, invEncMat);
				if (ELEFindSelectedEncounterObject(encLayerDoc, i))
				{
					GEPlacementToolApply(placementTool, encMat);
					ele_MoveStaticEncounter(encLayer, i, encMat, makeCopy, !placementTool->useGizmos);
				}
				else
				{
					int* selActorIndexList = ELEGetSelectedEncounterActorIndexList(encLayerDoc, i);
					int* selPointIndexList = ELEGetSelectedEncounterPointIndexList(encLayerDoc, i);
					int j, numActors = eaiSize(&selActorIndexList);
					int numPoints = eaiSize(&selPointIndexList);
					for (j = 0; j < numActors; j++)
					{
						int whichActor = selActorIndexList[j];
						if (staticEnc->spawnRule->actors && (whichActor < eaSize(&staticEnc->spawnRule->actors)))
						{
							OldActor* actor = staticEnc->spawnRule->actors[whichActor];
							GEFindActorMat(actor, encMat, actorMat);
							GEPlacementToolApply(placementTool, actorMat);
							mulMat4(invEncMat, actorMat, relActorMat);
							ele_MoveStaticEncounterActor(staticEnc, whichActor, relActorMat, makeCopy);
						}
					}
					for (j = 0; j < numPoints; j++)
					{
						int whichPoint = selPointIndexList[j];
						if (staticEnc->spawnRule->namedPoints && (whichPoint < eaSize(&staticEnc->spawnRule->namedPoints)))
						{
							OldNamedPointInEncounter* point = staticEnc->spawnRule->namedPoints[whichPoint];
							GEFindEncPointMat(point, encMat, actorMat);
							GEPlacementToolApply(placementTool, actorMat);
							mulMat4(invEncMat, actorMat, relActorMat);
							ele_MoveStaticEncounterPoint(staticEnc, whichPoint, relActorMat);
						}
					}
				}
			}
			placementTool->moveSelected = placementTool->copySelected = 0;
		}
		else if (placementTool->moveOrigin)
		{
			// TODO: Implement this
			// Currently this is not implemented
			placementTool->moveOrigin = 0;
		}

		ELERefreshStaticEncounters(encLayerDoc, true);
		ELERefreshUI(encLayerDoc);

		GESetDocUnsaved(encLayerDoc);
	}
}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
static bool ELEWhichPatrolPointUnderMouse(EncounterLayer* encLayer, int* whichRoute, int* whichPoint, Vec3 selActorPos, Vec3 selOffset, F32 *clickedPointDist)
{
	Mat4 pointMat;
	Vec3 result, start, end, boxMin, boxMax;
	int clickedRoute = -1;
	int clickedPoint = -1;
	int i, n = eaSize(&encLayer->oldNamedRoutes);

	// Shoot a ray from where the button was clicked (not where the mouse is now)
	// and find the closest actor whose bounding box is intersected
	editLibCursorRay(start, end);
	
	PERFINFO_AUTO_START("ELEWhichPatrolPointUnderMouse",1);
	for (i = 0; i < n; i++)
	{
		OldPatrolRoute* namedRoute = encLayer->oldNamedRoutes[i];
		int j, numPoints = eaSize(&namedRoute->patrolPoints);
		for (j = 0; j < numPoints; j++)
		{
			OldPatrolPoint* point = namedRoute->patrolPoints[j];
			if(!point->frozen)
			{
				quatVecToMat4(point->pointRot, point->pointLoc, pointMat);
				mulBoundsAA(g_GEDisplayDefs.spawnLocDispDef->bounds.min, g_GEDisplayDefs.spawnLocDispDef->bounds.max, pointMat, boxMin, boxMax);
				if (lineBoxCollision(start, end, boxMin, boxMax, result))
				{
					F32 dist = distance3Squared(start, result);
					if ((*clickedPointDist == -1) || (dist < *clickedPointDist))
					{
						*clickedPointDist = dist;
						clickedRoute = i;
						clickedPoint = j;
						if (selActorPos)
							copyVec3(pointMat[3], selActorPos);
						if (selOffset)
							subVec3(pointMat[3], result, selOffset);
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();

	// Now check to see if we found a patrol point
	if (clickedRoute != -1 && clickedPoint != -1)
	{
		*whichRoute = clickedRoute;
		*whichPoint = clickedPoint;
		return true;
	}

	return false;
}
#endif


// Returns true if there is an actor under the mouse, and stores which group and which actor in the passed
static bool ELEWhichGroupActorUnderMouse(EncounterLayer* encLayer, int* whichGroup, int* whichActor, Vec3 selActorPos, Vec3 selOffset, F32 *clickedActorDist)
{
	Mat4 actorMat;
	Vec3 result, start, end, boxMin, boxMax;
	int groupNum = -1;
	int clickedActor = -1;
	int enc, numEncs = eaSize(&encLayer->staticEncounters);

	editLibCursorRay(start, end);

	PERFINFO_AUTO_START("ELEWhichGroupActorUnderMouse",1);
	for (enc = 0; enc < numEncs; enc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[enc];
		EncounterDef* def = staticEnc->spawnRule;
		if (def)
		{
			int i, n = eaSize(&def->actors);
			Mat4 encMat;
			Mat4 camMat;

			if(!staticEnc->frozen)
			{
				F32 distToEncSquared;
				quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
				gfxGetActiveCameraMatrix(camMat);
				distToEncSquared = distance3Squared(camMat[3], encMat[3]);
				if (distToEncSquared <= ENC_MAX_DIST_SQ)
				{
					for (i = 0; i < n; i++)
					{
						OldActor* actor = def->actors[i];
						GEFindActorMat(actor, encMat, actorMat);
						mulBoundsAA(g_GEDisplayDefs.actDispDef->bounds.min, g_GEDisplayDefs.actDispDef->bounds.max, actorMat, boxMin, boxMax);
						if (lineBoxCollision(start, end, boxMin, boxMax, result))
						{
							F32 dist = distance3Squared(start, result);
							if ((*clickedActorDist == -1) || (dist < *clickedActorDist))
							{
								*clickedActorDist = dist;
								groupNum = enc;
								clickedActor = i;
								if (selActorPos)
									copyVec3(actorMat[3], selActorPos);
								if (selOffset)
									subVec3(actorMat[3], result, selOffset);
							}
						}
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();

	// Now check to see if we found an actor
	if (groupNum != -1 && clickedActor != -1)
	{
		*whichGroup = groupNum;
		*whichActor = clickedActor;
		return true;
	}

	return false;
}
static bool ELEWhichGroupPointUnderMouse(EncounterLayer* encLayer, int* whichGroup, int* whichPoint, Vec3 selPointLoc, Vec3 selOffset, F32 *clickedPointDist)
{
	Vec3 result, start, end;
	int groupNum = -1;
	int clickedPoint = -1;
	int enc, numEncs = eaSize(&encLayer->staticEncounters);

	editLibCursorRay(start, end);
	
	PERFINFO_AUTO_START("ELEWhichGroupPointUnderMouse",1);
	for (enc = 0; enc < numEncs; enc++)
	{
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[enc];
		EncounterDef* def = staticEnc->spawnRule;
		int i, n = eaSize(&def->namedPoints);
		Mat4 encMat;

		if(!staticEnc->frozen)
		{
			quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
			for (i = 0; i < n; i++)
			{
				OldNamedPointInEncounter* point = def->namedPoints[i];
				Mat4 pointMat;
				GEFindEncPointMat(point, encMat, pointMat);
				if (findSphereLineIntersection(start, end, pointMat[3], ME_NAMED_POINT_RADIUS, result))
				{
					F32 dist = distance3Squared(start, result);
					if ((*clickedPointDist == -1) || (dist < *clickedPointDist))
					{
						*clickedPointDist = dist;
						groupNum = enc;
						clickedPoint = i;
						if (selPointLoc)
							copyVec3(pointMat[3], selPointLoc);
						if (selOffset)
							subVec3(pointMat[3], result, selOffset);
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();

	// Now check to see if we found a point
	if (groupNum != -1 && clickedPoint != -1)
	{
		*whichGroup = groupNum;
		*whichPoint = clickedPoint;
		return true;
	}

	return false;
}

static GEObjectType ELEFindObjectUnderMouse(EncounterLayerEditDoc *encLayerDoc, EncounterLayer **foundInLayer,
											Vec3 selPos, Vec3 selOffset,
											int *groupIdx, int *objIdx, WorldInteractionEntry **entry,
											GEObjectType *foundObjType, bool checkAllLayers, bool clickablesOnly)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	GEObjectType foundType = -1;
	Vec3 start, end;
	WorldCollCollideResults wcResults;
	WorldInteractionEntry *interactionEntry = NULL;
	F32 closestObjDist = -1;

	PERFINFO_AUTO_START("ELEFindObjectUnderMouse",1);

	// Check to see if we clicked on a world editor object
	PERFINFO_AUTO_START("World object",1);
	editLibCursorRay(start, end);
	if (worldCollideRay(PARTITION_CLIENT, start, end, WC_FILTER_BIT_EDITOR, &wcResults))
	{
		// Keep the closest distance even if nothing was found so they can't accidentally select behind walls
		closestObjDist = (wcResults.distance * wcResults.distance);
	}
	PERFINFO_AUTO_STOP();

	if (!clickablesOnly)
	{
		EncounterLayer *layer;
		int i, n = eaSize(&g_EncounterMasterLayer->encLayers);
		for (i = 0; i < n; i++)
		{
			if (!checkAllLayers)
				layer = encLayer;
			else
				layer = g_EncounterMasterLayer->encLayers[i];

			if (layer->visible || layer == encLayer)
			{
				// Check to see if any of the mission editor objects are closer
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
				if(ELEWhichPatrolPointUnderMouse(layer, groupIdx, objIdx, selPos, selOffset, &closestObjDist))
				{
					foundType = GEObjectType_PatrolRoute;
					if (foundInLayer) *foundInLayer = layer;
				}
#endif
				if(ELEWhichGroupActorUnderMouse(layer, groupIdx, objIdx, selPos, selOffset, &closestObjDist))
				{
					foundType = GEObjectType_Encounter;
					if (foundObjType) *foundObjType = GEObjectType_Actor;
					if (foundInLayer) *foundInLayer = layer;
				}
				if(ELEWhichGroupPointUnderMouse(layer, groupIdx, objIdx, selPos, selOffset, &closestObjDist))
				{
					foundType = GEObjectType_Encounter;
					if (foundObjType) *foundObjType = GEObjectType_Point;
					if (foundInLayer) *foundInLayer = layer;
				}
			}

			if (!checkAllLayers)
				break;
		}
	}

	PERFINFO_AUTO_STOP();

	return foundType;
}

static void ELEEditSelectedLayer(UIMenuItem* item, EncounterLayer *layer)
{
	GESelectedObject *object = (GESelectedObject*)item->data.voidPtr;
	if (layer && layer->pchFilename && layer->pchFilename[0])
	{
		char fullname[MAX_PATH];
		fileLocateWrite(layer->pchFilename, fullname);
		if (emOpenFile(fullname))
		{
			if (object)
			{
				GEObjectType foundType = object->selType;
				int groupIdx = object->groupIndex;
				EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*)GEGetActiveEditorDocEM("encounterlayer");
				if (encLayerDoc->layerDef == layer)
				{
					if(foundType == GEObjectType_Encounter)
						ELESelectEncounter(encLayerDoc, groupIdx, false);
					else if(foundType == GEObjectType_Point)
						ELESelectNamedPoint(encLayerDoc, groupIdx, false);
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
					else if(foundType == GEObjectType_PatrolRoute)
						ELESelectPatrolRoute(encLayerDoc, groupIdx, false);
#endif
					ELERefreshUI(encLayerDoc);
				}
			}
		}
	}
}

static void ELEInputRightClick(EncounterLayerEditDoc* encLayerDoc)
{
	UIMenu *menu = NULL;
	EncounterLayer *layer = NULL;
	GEObjectType foundType = -1;
	GEObjectType foundObjType = -1;
	WorldInteractionEntry* interactionEntry = NULL;
	int groupIdx, objIdx;

	foundType = ELEFindObjectUnderMouse(encLayerDoc, &layer, NULL, NULL, &groupIdx, &objIdx, &interactionEntry, &foundObjType, true, false);
	
	if (layer)
	{
		static GESelectedObject object;
		object.selType = foundType;
		object.groupIndex = groupIdx;
		object.objIndex = objIdx;
// 		object.objData = interactionEntry?interactionEntry->name:NULL;

		// Create context-specific menu items if the object is in the current layer
		if(layer == encLayerDoc->layerDef)
		{
			OldStaticEncounter* staticEnc = layer->staticEncounters[object.groupIndex];
			if(foundType == GEObjectType_Encounter)
				menu = ui_MenuCreateWithItems("",
				ui_MenuItemCreate("Save Encounter As Def", UIMenuCallback, ELESaveEncAsDefMenuCB, staticEnc, NULL),
				NULL);
		}
		else
		{
			menu = ui_MenuCreateWithItems("",
			ui_MenuItemCreate("Open Layer", UIMenuCallback, ELEEditSelectedLayer, layer, &object),
			NULL);
		}

		if(menu)
			ui_MenuPopupAtCursor(menu);
	}

	if (encLayerDoc->uiInfo.contextMenu)
		ui_WidgetQueueFree(UI_WIDGET(encLayerDoc->uiInfo.contextMenu));
	encLayerDoc->uiInfo.contextMenu = menu;	
}

static void ELEInputLeftClick(EncounterLayerEditDoc* encLayerDoc)
{
	GEPlacementTool* placementTool = &encLayerDoc->placementTool;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	bool inPlaceMode = GEPlacementToolIsInPlacementMode(placementTool);

	if (inPlaceMode && !placementTool->useGizmos)
	{
		ELEPasteSelected(encLayerDoc);
		if (placementTool->useAdditiveSelect)
			placementTool->createNew = 1;
	}
	else if (!inPlaceMode && encLayerDoc)
	{
		int groupIdx, objIdx;
		GEObjectType foundType = -1;
		GEObjectType foundObjType = -1;
		WorldInteractionEntry* interactionEntry = NULL;
		EncounterLayer *foundInLayer = NULL;
		
		// Find the object under the mouse
		foundType = ELEFindObjectUnderMouse(encLayerDoc, &foundInLayer, NULL, NULL, &groupIdx, &objIdx, &interactionEntry, &foundObjType, true, g_InClickableAttachMode);

		if (foundInLayer == encLayer)
		{
			// If we found something, select it.
			if(foundType == GEObjectType_Encounter)
			{
				if (placementTool->useBoreSelect && foundObjType == GEObjectType_Actor)
					ELESelectActor(encLayerDoc, groupIdx, objIdx, placementTool->useAdditiveSelect);
				else if (placementTool->useBoreSelect && foundObjType == GEObjectType_Point)
					ELESelectEncPoint(encLayerDoc, groupIdx, objIdx, placementTool->useAdditiveSelect);
				else
					ELESelectEncounter(encLayerDoc, groupIdx, placementTool->useAdditiveSelect);
			}
			else if(foundType == GEObjectType_Point)
			{
				ELESelectNamedPoint(encLayerDoc, groupIdx, placementTool->useAdditiveSelect);
			}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
			else if(foundType == GEObjectType_PatrolRoute)
			{
				if (placementTool->useBoreSelect)
					ELESelectPatrolPoint(encLayerDoc, groupIdx, objIdx, placementTool->useAdditiveSelect);
				else
					ELESelectPatrolRoute(encLayerDoc, groupIdx, placementTool->useAdditiveSelect);
			}
#endif
			else if (!placementTool->useAdditiveSelect && !g_InClickableAttachMode) // No object selected; unselect everything
			{
				eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);
			}
			ELERefreshUI(encLayerDoc);
		}
		else if (!placementTool->useAdditiveSelect && !g_InClickableAttachMode) // No object selected; unselect everything
		{
			eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);
			ELERefreshUI(encLayerDoc);
		}
		g_InClickableAttachMode = false;
	}
}

void ELEProcessInput(EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	GEPlacementTool* placementTool = &encLayerDoc->placementTool;
	
	// This should be done first before we handle the state of the placement tool
	GEPlacementToolUpdate(placementTool);

	// Handle a marquee selection searching for anything that can be selected
	if (placementTool->processMarqueeSelect)
	{
		Mat4 camMat, pointMat;
		Vec3 screenBL, screenTR;
		int i, n;
		int enc, numEncs;

		gfxGetActiveCameraMatrix(camMat);
		if (!placementTool->useAdditiveSelect)
			eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
		// Patrol Routes
		n = eaSize(&encLayer->oldNamedRoutes);
		for (i = 0; i < n; i++)
		{
			bool selectedRoute = false;
			OldPatrolRoute* namedRoute = encLayer->oldNamedRoutes[i];
			int j, numPoints = eaSize(&namedRoute->patrolPoints);
			for (j = 0; j < numPoints; j++)
			{
				OldPatrolPoint* point = namedRoute->patrolPoints[j];
				quatVecToMat4(point->pointRot, point->pointLoc, pointMat);
				GEFindScreenCoords(g_GEDisplayDefs.spawnLocDispDef, pointMat, placementTool->scrProjMat, screenBL, screenTR);
				if (GEIsLegalMargqueeSelection(pointMat[3], camMat) && boxBoxCollision(screenBL, screenTR, placementTool->mouseMin, placementTool->mouseMax) && !point->frozen)
				{
					if (placementTool->useBoreSelect)
						ELESelectPatrolPoint(encLayerDoc, i, j, true);
					else
						selectedRoute = true;
				}
			}
			if (selectedRoute)
				ELESelectPatrolRoute(encLayerDoc, i, true);
		}
#endif

		// Encounters
		numEncs = eaSize(&encLayer->staticEncounters);
		for (enc = 0; enc < numEncs; enc++)
		{
			OldStaticEncounter* staticEnc = encLayer->staticEncounters[enc];
			EncounterDef* def = staticEnc->spawnRule;
			Mat4 encMat, actorMat;
			bool selectedEnc = false;

			if(!staticEnc->frozen)
			{
				n = eaSize(&def->actors);

				// Now project all actors to the screen to see if they are within the marquee selection
				quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
				for (i = 0; i < n; i++)
				{
					Vec3 actorBL, actorTR, actorCenter;
					OldActor* actor = def->actors[i];
					GEFindActorMat(actor, encMat, actorMat);
					copyVec3(actorMat[3], actorCenter);
					actorCenter[1] += 5.0; // estimate the center
					GEFindScreenCoords(g_GEDisplayDefs.actDispDef, actorMat, placementTool->scrProjMat, actorBL, actorTR);
					if (GEIsLegalMargqueeSelection(actorCenter, camMat) && boxBoxCollision(actorBL, actorTR, placementTool->mouseMin, placementTool->mouseMax))
					{
						// If we are selecting actors, call now, otherwise save to do one call after the loop
						// Fixes the problem where the group would be deselected when an even number of actors were selected
						if (placementTool->useBoreSelect)
							ELESelectActor(encLayerDoc, enc, i, true);
						else
							selectedEnc = true;
					}
				}

				// Project all named points in the encounter to the screen
				n = eaSize(&def->namedPoints);
				for (i = 0; i < n; i++)
				{
					Vec3 local_min, local_max;
					OldNamedPointInEncounter* point = def->namedPoints[i];
					int c;
					GEFindEncPointMat(point, encMat, pointMat);

					// Set up a bounding box for the point.  Not terribly pretty.
					for(c=0; c<3; c++)
					{
						local_min[c] = -0.7;
						local_max[c] = 0.7;
					}

					GEFindScreenCoordsEx(local_min, local_max, pointMat, placementTool->scrProjMat, screenBL, screenTR);

					if (GEIsLegalMargqueeSelection(pointMat[3], camMat) && boxBoxCollision(screenBL, screenTR, placementTool->mouseMin, placementTool->mouseMax))
					{
						// If we are selecting actors, call now, otherwise save to do one call after the loop
						// Fixes the problem where the group would be deselected when an even number of actors were selected
						if (placementTool->useBoreSelect)
							ELESelectEncPoint(encLayerDoc, enc, i, true);
						else
							selectedEnc = true;
					}
				}

	
				if (selectedEnc)
					ELESelectEncounter(encLayerDoc, enc, true);
			}
		}

		ELERefreshUI(encLayerDoc);
		placementTool->processMarqueeSelect = 0;
	}

	// If we flagged the encounter tree as needing to be refreshed, now's the time to do it.
	if(encLayerDoc->refreshTree)
		ELERefreshEncounterTree(encLayerDoc);

	// Currently placing something using quickrotate and no longer dragging or rotating means paste
	if (GEPlacementToolIsInPlacementMode(placementTool) && !placementTool->useGizmos && !placementTool->isQuickPlacing && !placementTool->isRotating && !placementTool->createNew)
		ELEPasteSelected(encLayerDoc);
}

static void ELEDrawMouseoverInfo(EncounterLayerEditDoc* encLayerDoc)
{
	int groupIdx = -1;
	int objIdx = -1;
	EncounterLayer *foundInLayer = NULL;
	WorldInteractionEntry *entry = NULL;
	GEObjectType foundType = -1;
	GEObjectType foundObjType = -1;
	
	PERFINFO_AUTO_START("ELEDrawMouseoverInfo",1);

	foundType = ELEFindObjectUnderMouse(encLayerDoc, &foundInLayer, NULL, NULL, &groupIdx, &objIdx, &entry, &foundObjType, true, false);

	ui_LabelSetText(encLayerDoc->uiInfo.mouseoverInfoObjectLayer, "");
	ui_LabelSetText(encLayerDoc->uiInfo.mouseoverInfoObjectName, "");

	if (foundType == GEObjectType_Encounter)
	{
		int* selectedEncs = ELEGetSelectedEncounterIndexList(encLayerDoc);
		OldStaticEncounter *staticEnc = (foundInLayer->staticEncounters && (groupIdx < eaSize(&foundInLayer->staticEncounters)) ? foundInLayer->staticEncounters[groupIdx] : NULL);
		bool encSelected = ((foundInLayer == encLayerDoc->layerDef) &&(eaiFind(&selectedEncs, groupIdx) != -1));
		Mat4 encMat;

		if (!encSelected && staticEnc)
		{
			quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
			//GEDrawEncounter(staticEnc->spawnRule, encMat, NULL, NULL, true, false, false, true, NULL, encLayerDoc->activeTeamSize, true);
		}
		if (staticEnc)
			ui_LabelSetText(encLayerDoc->uiInfo.mouseoverInfoObjectName, staticEnc->name);
	}
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	else if (foundType == GEObjectType_PatrolRoute)
	{
		int *selectedList = ELEGetSelectedPatrolRouteIndexList(encLayerDoc);
		OldPatrolRoute* route = (groupIdx < eaSize(&foundInLayer->oldNamedRoutes) ? foundInLayer->oldNamedRoutes[groupIdx] : NULL);
		bool selected = ((foundInLayer == encLayerDoc->layerDef) &&(eaiFind(&selectedList, groupIdx) != -1));
		if (route && !selected)
		{
			/*
			int j, numPoints = eaSize(&route->patrolPoints);
			Mat4 pointMat, lastPointMat;
			for (j = 0; j < numPoints; j++)
			{
				quatVecToMat4(route->patrolPoints[j]->pointRot, route->patrolPoints[j]->pointLoc, pointMat);
				GEDrawPatrolPoint(pointMat, selected, GE_INFO_COLOR, NULL, true);

				// Draw a line to connect adjacent patrol points
				pointMat[3][1] += 0.1;
				if (j > 0) gfxDrawLine3DWidth(pointMat[3], lastPointMat[3], GE_INFO_COLOR, GE_INFO_COLOR, 2);
				copyMat4(pointMat, lastPointMat);
			}
			*/
		}
		if (route)
			ui_LabelSetText(encLayerDoc->uiInfo.mouseoverInfoObjectName, route->routeName);
	}
#endif

	if (foundInLayer)
		ui_LabelSetText(encLayerDoc->uiInfo.mouseoverInfoObjectLayer, getFileNameConst(foundInLayer->pchFilename));

	PERFINFO_AUTO_STOP();
}

static void ELEDrawMouseoverTrackers(EncounterLayerEditDoc* encLayerDoc)
{
	S32 mouseX, mouseY;
	mousePos(&mouseX, &mouseY);
	gfxDrawLineEx(mouseX-8, mouseY-8, 0, mouseX+8, mouseY+8, GE_SEL_COLOR, GE_SEL_COLOR, 5, false);
	gfxDrawLineEx(mouseX-8, mouseY+8, 0, mouseX+8, mouseY-8, GE_SEL_COLOR, GE_SEL_COLOR, 5, false);
}

void ELEDrawActiveLayer(EncounterLayerEditDoc* encLayerDoc)
{
	GEPlacementTool* placementTool = &encLayerDoc->placementTool;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
	int* selRouteList = NULL;
	int** selPatrolsList = NULL;
	int** selActorsList = ELEGetFullSelectedActorIndexList(encLayerDoc);
	int** selEncPointsList = ELEGetFullSelectedEncounterPointIndexList(encLayerDoc);
	int* selPointsList = ELEGetSelectedNamedPointIndexList(encLayerDoc);
	Mat4 encMat, newObjectMat, actorMat;

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	selRouteList = ELEGetSelectedPatrolRouteIndexList(encLayerDoc);
	selPatrolsList = ELEGetFullSelectedPatrolIndexList(encLayerDoc);
#endif

	// Draw the Encounter Layer objects
	ELEDrawEncounterLayer(encLayer, selEncList, selActorsList, selEncPointsList, selRouteList, selPatrolsList, NULL, NULL, selPointsList, encLayerDoc->activeTeamSize);

	// Draw the Mouseover Info window
	if (!g_InClickableAttachMode && ui_WindowIsVisible(encLayerDoc->uiInfo.mouseoverInfoWindow))
		ELEDrawMouseoverInfo(encLayerDoc);

	// If placing/copying an object, draw the new object
	if (GEPlacementToolIsInPlacementMode(placementTool))
	{
		if (placementTool->createNew)
		{
			copyMat4(encLayerDoc->newObjectMat, newObjectMat);
			GEPlacementToolApply(&encLayerDoc->placementTool, newObjectMat);
			if (GET_REF(encLayerDoc->newEncDef))
				GEDrawEncounter(GET_REF(encLayerDoc->newEncDef), newObjectMat, NULL, NULL, true, true, true, false, encLayerDoc->activeTeamSize, true, true);
			else if (GET_REF(encLayerDoc->newCritter))
				GEDrawActor(NULL, newObjectMat, true, GE_COPY_COLOR, encLayerDoc->activeTeamSize, true, 0);
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
			else if (gConf.bAllowOldPatrolData && (encLayerDoc->typeToCreate == GEObjectType_PatrolRoute))
				GEDrawPatrolPoint(newObjectMat, true, GE_COPY_COLOR, true);
#endif
			else if (encLayerDoc->typeToCreate == GEObjectType_Point)
				GEDrawPoint(newObjectMat, true, GE_COPY_COLOR);
		}
		else if (placementTool->moveSelected)
		{
			bool makeCopy = GEPlacementToolShouldCopy(placementTool);
			int i, numGroups;

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
			if (gConf.bAllowOldPatrolData)
			{
				Mat4 currMat;

				// Patrol Routes
				numGroups = eaSize(&encLayer->oldNamedRoutes);
				for (i = 0; i < numGroups; i++)
				{
					int j, numPoints;
					OldPatrolPoint** selPatrolPoints = NULL;
					OldPatrolRoute* patrolRoute = encLayer->oldNamedRoutes[i];
					if (ELEFindSelectedPatrolRouteObject(encLayerDoc, i))
						selPatrolPoints = patrolRoute->patrolPoints;
					else
						selPatrolPoints = ELEGetSelectedPatrolPointList(encLayerDoc, i);
					numPoints = eaSize(&selPatrolPoints);
					for (j = 0; j < numPoints; j++)
					{
						OldPatrolPoint* patrolPoint = selPatrolPoints[j];
						quatVecToMat4(patrolPoint->pointRot, patrolPoint->pointLoc, currMat);
						GEPlacementToolApply(placementTool, currMat);
						GEDrawPatrolPoint(currMat, true, makeCopy ? GE_COPY_COLOR : GE_MOVE_COLOR, true);
					}
				}
			}
#endif

			// Encounters
			numGroups = eaSize(&encLayer->staticEncounters);
			for (i = 0; i < numGroups; i++)
			{
				OldStaticEncounter* staticEnc = encLayer->staticEncounters[i];
				quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
				if (ELEFindSelectedEncounterObject(encLayerDoc, i))
				{
					GEPlacementToolApply(placementTool, encMat);
					GEDrawEncounter(staticEnc->spawnRule, encMat, NULL, NULL, true, true, makeCopy, false, encLayerDoc->activeTeamSize, true, !placementTool->useGizmos);
				}
				else
				{
					OldActor** selActorList = ELEGetSelectedActorList(encLayerDoc, i);
					OldNamedPointInEncounter** selPointList = ELEGetSelectedEncPointList(encLayerDoc, i);
					int j, numActors = eaSize(&selActorList);
					int numPoints = eaSize(&selPointList);
					Mat4 camMat;
					F32 distToEncSquared = 0.0f;
					gfxGetActiveCameraMatrix(camMat);
					distToEncSquared = distance3Squared(camMat[3], staticEnc->encPos);
					for (j = 0; j < numActors; j++)
					{
						OldActor* actor = selActorList[j];
						GEFindActorMat(actor, encMat, actorMat);
						GEPlacementToolApply(placementTool, actorMat);
						GEDrawActor(actor, actorMat, true, makeCopy ? GE_COPY_COLOR : GE_MOVE_COLOR, encLayerDoc->activeTeamSize, true, distToEncSquared);
					}
					for (j = 0; j < numPoints; j++)
					{
						OldNamedPointInEncounter* point = selPointList[j];
						GEFindEncPointMat(point, encMat, actorMat);
						GEPlacementToolApply(placementTool, actorMat);
						GEDrawPoint(actorMat, true, makeCopy ? GE_COPY_COLOR : GE_MOVE_COLOR);
					}
				}
			}
		}
		else if (placementTool->moveOrigin)
		{
			// do something
		}
	}
}

void ELEPlaceInEncounterLayer(EncounterLayerEditDoc* encLayerDoc, const char* name, const char* type)
{
	GEPlacementTool* placementTool = &encLayerDoc->placementTool;
	if (!GEPlacementToolIsInPlacementMode(placementTool))
	{
		Vec3 startPos;

		// If it can't find the mouse position, defaulting to 0,0,0
		zeroVec3(placementTool->clickOffset);
		if (!GEFindMouseLocation(&encLayerDoc->placementTool, startPos))
			copyVec3(zerovec3, startPos);
		copyMat3(unitmat, encLayerDoc->newObjectMat);
		copyVec3(startPos, encLayerDoc->newObjectMat[3]);
		GEPlacementToolReset(&encLayerDoc->placementTool, startPos, zerovec3);

		// Reset old placement fields
		REMOVE_HANDLE(encLayerDoc->newEncDef);
		REMOVE_HANDLE(encLayerDoc->newCritter);
		placementTool->useGizmos = 0;

		// Setup the appropriate fields depending on what type we are dropping
		if (!stricmp(type, "NewFromKeyBind"))
		{
			if (encLayerDoc->typeToCreate == GEObjectType_Point
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
				|| encLayerDoc->typeToCreate == GEObjectType_PatrolRoute 
#endif
				)
				placementTool->createNew = 1;
		}
		else if (!stricmp(type, "EncounterDef"))
		{
			SET_HANDLE_FROM_STRING(g_EncounterDictionary, name, encLayerDoc->newEncDef);
			placementTool->createNew = 1;
		}
		else if (!stricmp(type, "CritterDef"))
		{
			SET_HANDLE_FROM_STRING(g_hCritterDefDict, name, encLayerDoc->newCritter);
			placementTool->createNew = 1;
		}
	}
}

GEPlacementTool* ELEGetPlacementTool(EncounterLayerEditDoc* encLayerDoc)
{
	return &encLayerDoc->placementTool;
}

typedef struct ELEUndoStackState 
{
	EncounterLayer *prevState;
	EncounterLayer *nextState;
} ELEUndoStackState;

static bool ELEUndo(EncounterLayerEditDoc* encLayerDoc, ELEUndoStackState* state)
{
	ELECancelAction(encLayerDoc);
	eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);

	// Copy over the current def
	oldencounter_LoadToMasterLayer(state->prevState);

	ELERefreshStaticEncounters(encLayerDoc, true);

	// Rebuild UI
	ELERefreshUI(encLayerDoc);

	if (encLayerDoc->previousState)
		StructDestroy(parse_EncounterLayer, encLayerDoc->previousState);
	encLayerDoc->previousState = oldencounter_SafeCloneLayer(state->prevState);

	return true;
}

static bool ELERedo(EncounterLayerEditDoc* encLayerDoc, ELEUndoStackState* state)
{
	ELECancelAction(encLayerDoc);
	eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);

	// Copy over the current def
	oldencounter_LoadToMasterLayer(state->nextState);

	ELERefreshStaticEncounters(encLayerDoc, true);

	// Rebuild UI
	ELERefreshUI(encLayerDoc);

	if (encLayerDoc->previousState)
		StructDestroy(parse_EncounterLayer, encLayerDoc->previousState);
	encLayerDoc->previousState = oldencounter_SafeCloneLayer(state->nextState);

	return true;
}

static ELEUndoStackState *ELESaveState(EncounterLayerEditDoc* encLayerDoc)
{
	ELEUndoStackState* newState = calloc(1, sizeof(ELEUndoStackState));

	newState->nextState = oldencounter_SafeCloneLayer(encLayerDoc->layerDef);
	newState->prevState = encLayerDoc->previousState;
	encLayerDoc->previousState = oldencounter_SafeCloneLayer(encLayerDoc->layerDef);	

	return newState;
}

static bool ELEFreeState(void* unused, ELEUndoStackState* state)
{
	StructDestroy(parse_EncounterLayer, state->nextState);
	StructDestroy(parse_EncounterLayer, state->prevState);
	free(state);
	return true;
}

void ELEEncounterMapUnload(OldEncounterMasterLayer *encMasterLayer)
{
	int i;
	for (i = eaSize(&s_EncounterLayerEditor.open_docs) - 1; i >= 0; i--)
	{
		EMEditorDoc* editorDoc = s_EncounterLayerEditor.open_docs[i];
		// Force close the editor, or it'll crash trying to refresh the encounter layer tree.
		// TODO: fix the tree so the editor can stay open.
		emForceCloseDoc(editorDoc);
	}
}

EditDocDefinition ELEDocDefinition = 
{
	ELEPasteSelected,			// Paste
	ELECopySelected,			// Cut/Copy
	ELECancelAction,			// Cancel
	ELEDeleteSelected,			// Delete
	NULL,						// Move Origin
	NULL,		// Move SpawnLoc
	ELEFreezeSelected,			// Freeze Selection
	ELEUnfreezeAll,				// Unfreeze all frozen objects
	ELEInputLeftClick,			// LeftClick
	ELEInputRightClick,			// RightClick
	ELEGroupSelected,			// Group
	ELEUngroupSelected,			// Ungroup
	NULL,						// Attach
	NULL,						// Detach
	ELESnapCamera,				// Center Camera
	ELEGetPlacementTool,		// PlacementTool from Doc
	ELEBeginQuickPlace,			// Begin Quickplace
	ELEPlaceInEncounterLayer,	// Place an object
	ELESaveState,				// Push an undo/redo state
	ELEUndo,					// Revert to a previous state
	ELERedo,					// Revert to a future state
	ELEFreeState,				// Free a saved state
};

#endif // NO_EDITORS
