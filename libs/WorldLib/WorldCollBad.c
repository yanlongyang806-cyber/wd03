//#define NO_MEMCHECK_OK // to allow including stdtypes.h in headers


// After Novodex, before any of ours
#undef NO_MEMCHECK_OK
#undef NO_MEMCHECK_H

#include "WorldCollPrivate.h"
#include "wlterrain.h"
#include "wlState.h"
#include "ControllerScriptingSupport.h"
#include "beacon.h"
#include "BeaconFile.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

static StashTable stPhysPropertyMaterials;



S32 wcMaterialFromPhysicalPropertyName(	WorldCollMaterial** wcmOut,
										const char* physPropertyName)
{
#if PSDK_DISABLED
	return 0;
#else
	PSDKMaterialDesc materialDesc = {0};
	PhysicalProperties* pPhysProperties;
	REF_TO(PhysicalProperties) physical_properties = {0};
	if(	stPhysPropertyMaterials &&
		stashFindPointer(stPhysPropertyMaterials, physPropertyName, wcmOut))
	{
		return 1;
	}
	// Otherwise, we haven't been added to the table yet, so create the material
	if(!physicalPropertiesFindByName(physPropertyName, &physical_properties.__handle_INTERNAL)){
		return 0;
	}
	
	pPhysProperties = GET_REF(physical_properties);
	
	if(pPhysProperties){
		materialDesc.dynamicFriction = pPhysProperties->dynamicFriction > 0.0f ?
											pPhysProperties->dynamicFriction :
											defaultDFriction;

		materialDesc.staticFriction = pPhysProperties->staticFriction > 0.0f ?
											pPhysProperties->staticFriction :
											defaultSFriction;

		materialDesc.restitution = pPhysProperties->restitution > 0.0f ?
										pPhysProperties->restitution :
										defaultRestitution;
		{
			WorldCollMaterial* wcmNew;
			wcMaterialCreate(&wcmNew, &materialDesc);
			if(!stPhysPropertyMaterials)
				stPhysPropertyMaterials = stashTableCreateWithStringKeys(32, StashDefault);
			stashAddPointer(stPhysPropertyMaterials, physPropertyName, wcmNew, true);
			*wcmOut = wcmNew;
		}
	}

    REMOVE_HANDLE(physical_properties);
	return 1;
#endif
}

// When a physical property is reloaded, update it if it has already been created.

S32 wcMaterialUpdatePhysicalProperties(void){
#if !PSDK_DISABLED
	StashTableIterator iter;
	StashElement elem;
	stashGetIterator(stPhysPropertyMaterials, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		PhysicalProperties* pPhysProperties = RefSystem_ReferentFromString("PhysicalProperties", stashElementGetStringKey(elem));
		WorldCollMaterial* wcm;
		
		if(pPhysProperties && (wcm = stashElementGetPointer(elem)))
		{
			PSDKMaterialDesc materialDesc = {0};
			materialDesc.dynamicFriction = pPhysProperties->dynamicFriction > 0.0f ? pPhysProperties->dynamicFriction : defaultDFriction;
			materialDesc.staticFriction = pPhysProperties->staticFriction > 0.0f ? pPhysProperties->staticFriction : defaultSFriction;
			materialDesc.restitution = pPhysProperties->restitution > 0.0f ? pPhysProperties->restitution : defaultRestitution;

			wcMaterialSetDesc(wcm, &materialDesc);
		}
	}
#endif

	return 1;
}

PhysicalProperties* wcoGetPhysicalProperties(WorldCollObject *wco, 
											 S32 triangleIndex,
											 Vec3 worldPos, Material **material_out)
{
	HeightMap *height_map = NULL;
	WorldCollisionEntry *entry = NULL;

	if(wcoGetUserPointer(wco, entryCollObjectMsgHandler, &entry)){
		Model *model = SAFE_MEMBER(entry, model);
		Material* mat = modelGetCollisionMaterialByTriEx(model, triangleIndex, entry->eaMaterialSwaps);
		if(mat)
		{
			if(material_out)
				*material_out = mat;

			if (IS_HANDLE_ACTIVE(mat->world_props.physical_properties))
			{
				PhysicalProperties *props = GET_REF(mat->world_props.physical_properties);
				if(props)
					return props;
			}
		}
	}
	else if(wcoGetUserPointer(wco, heightMapCollObjectMsgHandler, &height_map))
	{
		PhysicalProperties* phys_props = terrainGetPhysicalProperties(height_map, worldPos[0], worldPos[2]);
		if (phys_props)
			return phys_props;
	}

	return NULL;
}

const char* wcoGetPhysicalPropertyName(	WorldCollObject* wco,
										S32 triangleIndex,
										Vec3 worldPos)
{
	PhysicalProperties *props = wcoGetPhysicalProperties(wco, triangleIndex, worldPos, NULL);

	if(props)
		return props->countAs ? props->countAs : props->name_key;

	return NULL;
}

AUTO_COMMAND ACMD_HIDE;
void wcTestMapReloadCRC(void)
{
	U32 crc1, crc2;
	
	crc1 = wcGenerateCollCRC(0, 0, 1, NULL);

	worldReloadMap();

	crc2 = wcGenerateCollCRC(0, 0, 1, NULL);

	if(crc1!=crc2)
	{
		ControllerScript_Failed("CRC Failed Match on Reload");
	}
	else
	{
		ControllerScript_Succeeded();
	}
}

// Basically copied directly from the beaconizer
U32 wcGenerateCollCRC(S32 printInfo, S32 logInfo, S32 spawnPos, char* logName)
{
#if !PLATFORM_CONSOLE
	beaconClearCRCData();
	if(spawnPos)
	{
		beaconGatherSpawnPositions(1);
		beaconServerCreateSpaceBeacons();
	}

	return beaconFileGetMapGeoCRC();
#else
	return 0;
#endif
}

AUTO_COMMAND ACMD_SERVERONLY;
void wcTestProdCRCS(void)
{
	U32 prodmode = isProductionMode();

	printf("%s: %x\n", prodmode ? "Pro" : "Not", wcGenerateCollCRC(0, 0, 1, NULL));
}

void wcPrintActors(	int iPartitionIdx,
							S32 cellx,
							S32 cellz)
{
#if !PSDK_DISABLED
	WorldCollGridCell* cell;
	
	wcGetCellByGridPosFG(	worldGetActiveColl(iPartitionIdx),
							&cell,
							cellx,
							cellz);

	if(SAFE_MEMBER(cell, placement)){
		psdkScenePrintActors(cell->placement->ss->psdkScene);
	}
#endif
}


