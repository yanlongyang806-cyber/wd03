#include "wlSavedTaggedGroups.h"
#include "group.h"
#include "groupUtil.h"
#include "Quat.h"
#include "WorldGrid.h"
#include "WorldGridPrivate.h"

#include "wlSavedTaggedGroups_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

// 
static ETaggedObjectType tagToTrafficObjectType(const char *tagName)
{
	if (!stricmp(tagName, "stop_sign"))
	{
		return ETagGleanObjectType_TRAFFIC_STOPSIGN;
	}
	else if (!stricmp(tagName, "traffic_light"))
	{
		return ETagGleanObjectType_TRAFFIC_LIGHT;
	}
	else if (!stricmp(tagName, "one_way_sign"))
	{
		return ETagGleanObjectType_TRAFFIC_ONEWAY_SIGN;
	}

	return ETagGleanObjectType_NONE;
}

static void addNewType(SavedTaggedGroups *data, ETaggedObjectType type, GroupInfo *info, GroupInheritedInfo *inherited_info)
{
	S32 i;
	TaggedObjectData *object;
	bool bRandomedChild = false;
	
	// walk up the parent def and see if any of them have the flag GRP_RANDOM_CHILD
	// if so, we are going to assume that this object type is being placed here. 
	for (i = 0; i < eaSize(&inherited_info->parent_defs); i++)
	{
		GroupDef *pParent = inherited_info->parent_defs[i];
		if (pParent->property_structs.physical_properties.bRandomSelect)
		{
			bRandomedChild = true;
			break;
		}
	}

	if (bRandomedChild)
	{
		// search our list of objects and find if we have any other object with this parent group
		// and is of the same type
		for (i = 0; i < eaSize(&data->eaParentRandomChild); i++)
		{
			TaggedObjectData *pOtherObject = data->eaParentRandomChild[i];

			if (nearSameVec3(pOtherObject->vPos, info->world_matrix[3]))
			{
				if (pOtherObject->eType == type)
				{
					bRandomedChild = false;
					break;
				}
			}
		}

		if (bRandomedChild == false)
		{	// we already recorded an object with this parent of the same type
			return;
		}
	}

	// get the pos and yaw of this object
	object = StructAlloc(parse_TaggedObjectData);
	object->eType = type;
	copyVec3(info->world_matrix[3], object->vPos);
	mat3ToQuat(info->world_matrix, object->qRot);

	eaPush(&data->eaTrafficObjects, object);

	if (bRandomedChild)
	{
		object->randomedChild = true;
		eaPush(&data->eaParentRandomChild, object);
	}
}

static bool createTagGleanedData(SavedTaggedGroups *data, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	if (def->tags)
	{
		const char *pszDelims = " ,";
		char *pszTok, *pszTagsCopy = NULL, *tokContext = NULL;

		// Tags is string, delimited list
		strdup_alloca(pszTagsCopy, def->tags);
		
		pszTok = strtok_s(pszTagsCopy, pszDelims, &tokContext);
		while(pszTok)
		{
			ETaggedObjectType type = tagToTrafficObjectType(pszTok);

			if (type != ETagGleanObjectType_NONE)
			{
				addNewType(data, type, info, inherited_info);
			}

			pszTok = strtok_s(NULL, pszDelims, &tokContext);
		}
		
	}

	return true;
}

bool wlGenerateAndSaveTaggedGroups(const char *filename) 
{
	ZoneMap			*zmap = worldGetActiveMap();
	SavedTaggedGroups	*tagData;
	int				ok = false;
	S32				i;

	if (!zmap)
	{
		return false;
	}

	tagData = StructCreate(parse_SavedTaggedGroups);
	
	for (i = 0; i < eaSize(&zmap->layers); ++i)
		layerGroupTreeTraverse(zmap->layers[i], createTagGleanedData, tagData, false, true);

	// only write if we have any objects
	if (eaSize(&tagData->eaTrafficObjects) > 0)
		ok = ParserWriteTextFile(filename, parse_SavedTaggedGroups, tagData, 0, 0);
	
	eaDestroy(&tagData->eaParentRandomChild);
	StructDestroySafe(parse_SavedTaggedGroups, &tagData);
	
	return ok != 0;
}

SavedTaggedGroups* wlLoadSavedTaggedGroupsForCurrentMap()
{
	char filename[MAX_PATH];
	SavedTaggedGroups	*tagData;
	
	{
		char map_path[MAX_PATH];
		worldGetServerBaseDir(zmapGetFilename(NULL), SAFESTR(map_path));
		sprintf(filename, "%s/tagGlean.tagg", map_path);
	}

	tagData = StructCreate(parse_SavedTaggedGroups);

	if (! ParserReadTextFile(filename, parse_SavedTaggedGroups, tagData, 0))
	{
		StructDestroySafe(parse_SavedTaggedGroups, &tagData);
		return NULL;
	}

	return tagData;
}

TaggedObjectData* wlTGDFindNearestObject(SavedTaggedGroups *data, const Vec3 vPos, F32 fMaxDist, const EArray32Handle eaTypes)
{
	S32 i;
	F32 fClosestSQ = FLT_MAX;
	TaggedObjectData* pClosestObj = NULL;
	U32 numTypesToTest;
	F32 fMaxDistSQ;
	
	if (!data)
		return NULL;

	fMaxDistSQ = fMaxDist * fMaxDist;
	numTypesToTest = ea32Size(&eaTypes);
	
	for (i = 0; i < eaSize(&data->eaTrafficObjects); i++)
	{
		F32 fDistSQ;
		TaggedObjectData *obj = data->eaTrafficObjects[i];

		if (numTypesToTest)
		{
			bool bValidType = false;
			S32 x = numTypesToTest - 1;
			do {
				if (obj->eType == eaTypes[x])
				{
					bValidType = true;
					break;
				}
			} while (--x >= 0);

			if (!bValidType)
				continue;
		}

		fDistSQ = distance3Squared(obj->vPos, vPos);

		if (fDistSQ <= fMaxDistSQ && fDistSQ < fClosestSQ)
		{
			fClosestSQ = fDistSQ;
			pClosestObj = obj;
		}
	}

	return pClosestObj;
}

#include "wlSavedTaggedGroups_h_ast.c"
