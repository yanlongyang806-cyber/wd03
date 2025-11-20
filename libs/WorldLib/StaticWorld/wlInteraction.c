#include "wlInteraction.h"
#include "wlEncounter.h"
#include "Quat.h"
#include "stringcache.h"
#include "qsortG.h"
#include "Expression.h"
#include "timing.h"
#include "Octree.h"
#include "logging.h"
#include "DebugState.h"
#include "bounds.h"

#include "wlState.h"
#include "wlModelLoad.h"
#include "WorldCellEntry.h"
#include "beaconConnection.h"
#include "partition_enums.h"
#include "PhysicsSDK.h"
#include "ZoneMap.h"
#include "../wcoll/collide.h"
#include "WorldGrid.h"
#include "WorldGridPrivate.h"
#include "WorldCellEntryPrivate.h"
#include "strings_opt.h"

GCC_SYSTEM

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

int gLogInteractionChanges;

#define DEBUG_PRINTF(format, ...) if (gLogInteractionChanges) filelog_printf("interaction.log", format, __VA_ARGS__)
#define DEFAULT_TARGETABLE_NODE_FX "FX_TrackerHighlight"
#define DEFAULT_TARGETABLE_NODE_UNATTEMPTABLE_FX "FX_TrackerHighlight_Non_Interact"

static int interaction_node_count, interaction_node_updates;

static int s_iDestructibleMask;
static int s_iNamedObjectMask;

static bool s_bCanAlterInteractionBits = false;

static const char *s_pcPooledDestructible;

static int s_reset_child_indexes;

typedef struct WorldInteractionNodeLocalData WorldInteractionNodeLocalData;

AUTO_STRUCT AST_IGNORE(visible_child_idx) AST_IGNORE(disabled) AST_IGNORE(hidden) AST_IGNORE(show_until_entity_exists);
typedef struct WorldInteractionNode
{
	char *key;									AST( STRUCTPARAM KEY )
	REF_TO(WorldInteractionEntry) hDynEntry;	AST( REFDICT(WorldDynamicEntryDictionary) )
	bool selectable;							AST( DEFAULT(true) )

	// Cached client only fields used when hDynEntry isn't loaded
	Vec3 vCachedPlayerPos;						AST( CLIENT_ONLY VEC3 )
	Vec3 vCachedNearestPoint;					AST( CLIENT_ONLY VEC3 )

	// this gets cleared at every update from the server
	WorldInteractionNodeLocalData *local_data;	NO_AST
} WorldInteractionNode;

// interaction node data that is not sent down from the server
typedef struct WorldInteractionNodeLocalData
{
	// server and client
	WorldInteractionNode *node;
	U32 interaction_class;
	Mat4 inv_world_mat;
	Vec3 local_min, local_max;
	OctreeEntry octree_entry;

	// client only
	const char *client_fx_name;
	bool bUseChildBounds;

} WorldInteractionNodeLocalData;

#include "wlInteraction_c_ast.c"

static DictionaryHandle interaction_dict;
static StashTable interaction_local_data_hash;
static DictionaryHandle entry_dict;
static StashTable interaction_class_hash;
static Octree *interaction_node_octree;
static ExprContext *interaction_expr_context;

static InteractionNodeIsSelectableCallback game_interaction_destructible_selectable_callback = NULL;

static U32 destructible_bit = 0;
static U32 clickable_bit = 0;
static S32 harvest_node = 0;


AUTO_CMD_INT(gLogInteractionChanges, LogInteractionChanges) ACMD_CMDLINE;

static void wlInteractionNodeLocalDataDestroy(WorldInteractionNodeLocalData *local_data)
{
	WorldInteractionNode *node;

	if (!local_data)
		return;

	node = local_data->node;

	assert(node);

	// remove from octree
	if (interaction_node_octree && octreeEntryInUse(&local_data->octree_entry))
		octreeRemove(&local_data->octree_entry);

	node->local_data = NULL;
	stashRemovePointer(interaction_local_data_hash, node->key, NULL);
	free(local_data);
}

static void wlInteractionNodeLocalDataCreate(SA_PARAM_NN_VALID WorldInteractionNode *node)
{
	WorldInteractionEntry *entry = GET_REF(node->hDynEntry);
	WorldBaseInteractionProperties *props = SAFE_MEMBER(entry, base_interaction_properties);
	WorldInteractionNodeLocalData *local_data;

	if (stashRemovePointer(interaction_local_data_hash, node->key, &local_data))
	{
		// remove from octree
		if (interaction_node_octree && octreeEntryInUse(&local_data->octree_entry))
			octreeRemove(&local_data->octree_entry);
		ZeroStructForce(local_data);
	}
	else
	{
		local_data = calloc(1, sizeof(*local_data));
	}

	local_data->node = node;
	node->local_data = local_data;

	stashAddPointer(interaction_local_data_hash, node->key, local_data, true);
	local_data->interaction_class = props ? props->eInteractionClass : 0;

	if (wlIsServer())
	{
		if (game_interaction_destructible_selectable_callback)
			wlInteractionNodeSetSelectableAll(node, game_interaction_destructible_selectable_callback(node));
		else
			wlInteractionNodeSetSelectableAll(node, true);
	}

	// add new octree entry
	local_data->octree_entry.node = node;
	if (entry)
	{
		WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
		WorldCellEntrySharedBounds *shared_bounds = entry->base_entry.shared_bounds;
		int i;
		bool bHasChildrenWithValidBounds = false;
		
		setVec3same(local_data->local_min, 8e16);
		setVec3same(local_data->local_max, -8e16);

		invertMat4Copy(bounds->world_matrix, local_data->inv_world_mat);

		for (i = 0; i < eaSize(&entry->child_entries); ++i)
		{
			if (entry->child_entries[i]->type == WCENT_COLLISION ||
				entry->child_entries[i]->type > WCENT_BEGIN_DRAWABLES && !((WorldDrawableEntry *)entry->child_entries[i])->editor_only)
			{
				WorldCellEntryBounds *child_bounds = &entry->child_entries[i]->bounds;
				WorldCellEntrySharedBounds *child_shared_bounds = entry->child_entries[i]->shared_bounds;
				Vec3 local_min, local_max;
				Mat4 child_to_local;
				mulMat4Inline(local_data->inv_world_mat, child_bounds->world_matrix, child_to_local);
				mulBoundsAA(child_shared_bounds->local_min, child_shared_bounds->local_max, child_to_local, local_min, local_max);
				vec3RunningMin(local_min, local_data->local_min);
				vec3RunningMax(local_max, local_data->local_max);
			}
			else if (entry->child_entries[i]->type == WCENT_INTERACTION)
			{
				// do all interact children of interact nodes have valid bounds?  This is something I do not know.  [RMARR - 3/29/11]
				bHasChildrenWithValidBounds = true;
			}
		}

		// check if we were unable to generate a legitimate bounding box from children
		if (local_data->local_min[0] > local_data->local_max[0] || local_data->local_min[1] > local_data->local_max[1] || local_data->local_min[2] > local_data->local_max[2])
		{
			// If we have child interacts with bounds we should use instead, set this bool
			if (bHasChildrenWithValidBounds)
			{
				local_data->bUseChildBounds = true;
			}
			copyVec3(shared_bounds->local_min, local_data->local_min);
			copyVec3(shared_bounds->local_max, local_data->local_max);
		}

		mulBoundsAA(local_data->local_min, local_data->local_max, bounds->world_matrix, local_data->octree_entry.bounds.min, local_data->octree_entry.bounds.max);
		local_data->octree_entry.bounds.radius = boxCalcMid(local_data->octree_entry.bounds.min, local_data->octree_entry.bounds.max, local_data->octree_entry.bounds.mid);

		if (interaction_node_octree)
			octreeAddEntry(interaction_node_octree, &local_data->octree_entry, OCT_MEDIUM_GRANULARITY);
	}

}

SA_ORET_NN_VALID static WorldInteractionNode *wlInteractionNodeCreate(SA_PARAM_NN_VALID WorldInteractionEntry *entry, SA_PARAM_NN_STR const char *name)
{
	WorldInteractionNode *node = StructCreate(parse_WorldInteractionNode);

	assert(wlIsServer());

	DEBUG_PRINTF("Create interaction node for \"%s\"\n", name);

	node->key = StructAllocString(name);

	// add reference to entry
	SET_HANDLE_FROM_STRING(entry_dict, name, node->hDynEntry);

	wlInteractionNodeLocalDataCreate(node);

	// add interaction node to dictionary
	RefSystem_AddReferent(interaction_dict, name, node);

	return node;
}

static void wlInteractionNodeDestroy(WorldInteractionNode *node)
{
	if (!node)
		return;

	DEBUG_PRINTF("Destroy interaction node for \"%s\"\n", node?node->key:"");

	if (wl_state.interaction_node_free_func)
		wl_state.interaction_node_free_func(node);

	{
		WorldInteractionEntry *entry = GET_REF(node->hDynEntry);
		if(entry)
			beaconHandleInteractionDestroyed(entry);
	}

	wlInteractionNodeLocalDataDestroy(node->local_data);

	RefSystem_RemoveReferent(node, false);
	
	StructDestroy(parse_WorldInteractionNode, node);
}

static void wlInteractionNodeApply(SA_PARAM_NN_VALID WorldInteractionNode *node, bool update_octree)
{
	WorldInteractionEntry *entry = GET_REF(node->hDynEntry);

	if (!entry)
		return;

	if (update_octree && wlIsClient())
		wlInteractionNodeLocalDataCreate(node);

	if (gLogInteractionChanges)
		filelog_printf("interaction.log","EntryNodeApply %d", entry->uid);
}

static void wlInteractionClientNodeChanged(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	WorldInteractionNode *node = pReferent;
	const char *handle_string = pRefData;

	if (!node)
		return;

	assert(!wlIsServer());

	if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
	{
		DEBUG_PRINTF("Client add or modify interaction node for \"%s\"\n", handle_string);
		wlInteractionNodeApply(node, true);
		interaction_node_updates++;
	}
	else if (eType == RESEVENT_RESOURCE_PRE_MODIFIED || eType == RESEVENT_RESOURCE_REMOVED)
	{
		DEBUG_PRINTF("Client remove or pre-modify interaction node for \"%s\"\n", handle_string);
		wlInteractionNodeLocalDataDestroy(node->local_data);
		if (eType == RESEVENT_RESOURCE_REMOVED)
			interaction_node_updates++;
	}
}

bool wlInteractionUidFromName(const char *name_str, ZoneMap **zmap_out, int *uid_out)
{
	int idx;
	const char *s;

	*zmap_out = NULL;
	*uid_out = 0;

	if (!name_str)
		return false;

	idx = atoi(name_str);
	if (idx < 0 || idx >= eaSize(&world_grid.maps))
		return false;

	s = strchr(name_str, ' ');
	if (!s)
		return false;

	*uid_out = atoi(s+1);
	*zmap_out = world_grid.maps[idx];

	return *uid_out != 0;
}

void wlInteractionNameFromUid(char *name_str, size_t name_str_size, ZoneMap *zmap, int uid)
{
	int map_idx = eaFind(&world_grid.maps, zmap);
	assert(map_idx >= 0);
	sprintf_s(SAFESTR2(name_str), "%d %d", map_idx, uid);
}

static void *wlInteractionGetEntryFromNameString(const char *name_str)
{
	ZoneMap *zmap;
	int uid;
	if (!wlInteractionUidFromName(name_str, &zmap, &uid))
		return NULL;
	return worldGetInteractionEntryFromUid(zmap, uid, true);
}

void wlInteractionAddToDictionary(ZoneMap *zmap, WorldInteractionEntry *entry)
{
	char name[1024];

	if (entry->uid)
	{	
		wlInteractionNameFromUid(SAFESTR(name), zmap, entry->uid);
		if (!RefSystem_DoesReferentExist(entry))
		{
			RefSystem_AddReferent(entry_dict, name, entry);
			resNotifyObjectPreModified(interaction_dict, name);
			resNotifyObjectModified(interaction_dict, name);
		}
	}
}

void wlInteractionSystemStartup(void)
{
	if (interaction_dict)
		return;

	interaction_dict = RefSystem_RegisterSelfDefiningDictionary(INTERACTION_DICTIONARY, false, parse_WorldInteractionNode, true, false, NULL);
	entry_dict = RefSystem_RegisterDictionaryWithStringRefData(ENTRY_DICTIONARY, wlInteractionGetEntryFromNameString, false, NULL, false);
	interaction_node_octree = octreeCreate();
	interaction_local_data_hash = stashTableCreateWithStringKeys(1024, StashDefault);

	// SDANGELO: Interaction dictionary send logic
	if (wlIsServer())
	{
		resDictProvideMissingResources(interaction_dict);
	}
	else
	{
		resDictRequestMissingResources(interaction_dict, 20, true, resClientRequestSendReferentCommand);
		resDictRegisterEventCallback(interaction_dict, wlInteractionClientNodeChanged, NULL);
	}

	// Register interaction class names
	// NOTE: If you alter the order of these bits or add one to anyplace other than the last bit
	//       then you must update WORLD_STREAMING_BIN_VERSION to a new number.
	s_bCanAlterInteractionBits = true;
	wlInteractionClassNameToBitMask("Clickable");
	wlInteractionClassNameToBitMask("Contact");
	wlInteractionClassNameToBitMask("CraftingStation");
	s_iDestructibleMask = wlInteractionClassNameToBitMask("Destructible");
	wlInteractionClassNameToBitMask("Door");
	wlInteractionClassNameToBitMask("FromDefinition");
	wlInteractionClassNameToBitMask("Gate");
	s_iNamedObjectMask = wlInteractionClassNameToBitMask("NamedObject");
	wlInteractionClassNameToBitMask("Throwable");
	s_pcPooledDestructible = allocAddString("Destructible");
	wlInteractionClassNameToBitMask("Ambientjob");
	wlInteractionClassNameToBitMask("CombatJob");
	wlInteractionClassNameToBitMask("Chair");
	wlInteractionClassNameToBitMask("TeamCorral");
	s_bCanAlterInteractionBits = false;

	dbgAddIntWatch("InteractionNodeCount", interaction_node_count);
	dbgAddClearedIntWatch("InteractionNodeUpdates", interaction_node_updates);
}

void wlInteractionFreeAllNodes(void)
{
	// clear dictionaries

	if (interaction_local_data_hash)
	{
		stashTableClearEx(interaction_local_data_hash, NULL, wlInteractionNodeLocalDataDestroy);
	}

	if (interaction_dict)
	{
		if (wlIsServer())
			RefSystem_ClearDictionaryEx(interaction_dict, true, wlInteractionNodeDestroy);
		else
			RefSystem_ClearDictionary(interaction_dict, true);
		resDictResetRequestData(interaction_dict);
	}

	if (entry_dict)
	{
		RefSystem_ClearDictionary(entry_dict, false);
	}
}

void wlInteractionOpenForEntry(ZoneMap *zmap, WorldInteractionEntry *entry)
{
	if (entry->uid)
	{
		WorldInteractionNode *node;
		char name[1024];

		wlInteractionNameFromUid(SAFESTR(name), zmap, entry->uid);

		DEBUG_PRINTF("Open interaction node for %d\n", entry->uid);

		// create interaction node if needed
		if (wlIsServer() && !RefSystem_ReferentFromString(interaction_dict, name))
		{
			wlInteractionNodeCreate(entry, name);
		}

		// create reference to interaction node
		SET_HANDLE_FROM_STRING(interaction_dict, name, entry->hInteractionNode);
		entry->hasInteractionNode = 1;

		// set class and type and update octree
		node = GET_REF(entry->hInteractionNode);
		if (wlIsServer() && node)
			wlInteractionNodeLocalDataCreate(node);

		if (wlIsClient() && node)
			wlInteractionNodeApply(node, true);
	}
}

void wlInteractionCloseForEntry(WorldInteractionEntry *entry)
{
	WorldInteractionNode *node = GET_REF(entry->hInteractionNode);
	
	if (node && wlIsServer())
		wlInteractionNodeDestroy(node);

	// remove reference to interaction node
	REMOVE_HANDLE(entry->hInteractionNode);
	entry->hasInteractionNode = 0;
}


WorldBaseInteractionProperties *wlInteractionCreateBaseProperties(WorldInteractionProperties *props)
{
	int i;
	WorldBaseInteractionProperties *base_props;
	if (!props)
		return NULL;

	base_props = StructCreate(parse_WorldBaseInteractionProperties);

	// Don't add anything to base_props unless you want all maps on all games to re-bin
	// and you have permission to do that.
	base_props->bStartsHidden = !!props->bStartsHidden;
	base_props->bVisiblePerEnt = !!props->bEvalVisExprPerEnt;
	base_props->bTabSelect = !!props->bTabSelect;
	base_props->pchOverrideFX = props->pchOverrideFX;
	base_props->pchAdditionalUniqueFX = props->pchAdditionalUniqueFX;

	base_props->uInteractDist = props->uInteractDist;
	base_props->uTargetDist = props->uTargetDist;

	if ( IS_HANDLE_ACTIVE(props->displayNameMsg.hMessage) )
		COPY_HANDLE(base_props->hDisplayNameMsg, props->displayNameMsg.hMessage);

	// NOTE: The only base_props values that should be set by iterating the entries
	//       are the interaction class and the destructible display name.
	//       No other properties should be copied out of the entries!!
	//       If you have a question about why, contact SDANGELO.
	for(i=eaSize(&props->eaEntries)-1; i>=0; --i)
	{
		// Base properties interaction class is always either Destructible or NamedObject
		// Other class information is filtered out during binning to avoid the client being
		// able to test this class, because this class does not accurately represent the
		// class that the server thinks the object is, because not all properties are available
		// at bin time.
		if (props->eaEntries[i]->pcInteractionClass == s_pcPooledDestructible) 
			base_props->eInteractionClass |= s_iDestructibleMask;
		else
			base_props->eInteractionClass |= s_iNamedObjectMask;

		if (!IS_HANDLE_ACTIVE(base_props->hDisplayNameMsg) && props->eaEntries[i]->pDestructibleProperties)
			COPY_HANDLE(base_props->hDisplayNameMsg, props->eaEntries[i]->pDestructibleProperties->displayNameMsg.hMessage);
	}


	return base_props;
}

//////////////////////////////////////////////////////////////////////////
// accessors

F32 wlInteractionNodeGetSphereBounds(WorldInteractionNode *node, Vec3 world_mid)
{
	WorldInteractionNodeLocalData *local_data = node->local_data;
	if (local_data)
	{
		copyVec3(local_data->octree_entry.bounds.mid, world_mid);
		return local_data->octree_entry.bounds.radius;
	}

	zeroVec3(world_mid);
	return 0;
}

void wlInteractionNodeSetClientFXName(WorldInteractionNode *node, const char *fx_name)
{
	WorldInteractionNodeLocalData *local_data = node->local_data;
	WorldInteractionEntry *entry = GET_REF(node->hDynEntry);

	assert(wlIsClient());

	if (!local_data)
		return;

	if (fx_name)
		fx_name = allocAddString(fx_name);

	if (local_data->client_fx_name == fx_name)
		return;

	if(entry) {
		worldInteractionEntrySetFX(PARTITION_CLIENT, entry, fx_name, NULL);
	}

	local_data->client_fx_name = fx_name;
	wlInteractionNodeApply(node, false);
}

bool DEFAULT_LATELINK_wlInteractionGetSelectableFromCritter(WorldInteractionNode *node)
{
	
	return true;
}


void wlInteractionNodeSetSelectable(int iPartitionIdx, WorldInteractionNode *node, bool isSelectable)
{
	if (node->selectable == !!isSelectable)
		return;
	node->selectable = !!isSelectable;
	RefSystem_ReferentModified(node);
}

void wlInteractionNodeSetSelectableAll(WorldInteractionNode *node, bool isSelectable)
{
	int i;

	for(i=eaSize(&world_grid.eaWorldColls)-1; i>=0; --i) {
		if (world_grid.eaWorldColls[i]) {
			wlInteractionNodeSetSelectable(i, node, isSelectable);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// classes and types

U32 wlInteractionClassNameToBitMask(const char *class_name)
{
	static U32 next_type_bit = 1;
	int bit = 0;

	if (!interaction_class_hash)
		interaction_class_hash = stashTableCreateWithStringKeys(256, StashDeepCopyKeys_NeverRelease);

	if (!stashFindInt(interaction_class_hash, class_name, &bit))
	{
		if (s_bCanAlterInteractionBits) {
			assert(next_type_bit);
			bit = next_type_bit;
			next_type_bit = next_type_bit << 1;
			stashAddInt(interaction_class_hash, class_name, bit, false);
		} else {
			devassertmsgf(0, "Tried to get interaction class name %s from bit system when it is not registered yet!  Make sure to register it in wlInteractionSystemStartup().  Maybe someone tried getting bit names before wlInteractionSystemStartup was run?", class_name);
		}
	}

	return bit;
}

bool wlInteractionClassMatchesMask(WorldInteractionNode* pNode, U32 iMask)
{
	if ( pNode==NULL || pNode->local_data==NULL )
		return false;

	return ( (pNode->local_data->interaction_class & iMask) > 0 );
}

void wlInteractionGetClassNames(const char ***names_earray)
{
	StashTableIterator iter;
	StashElement elem;

	if (!interaction_class_hash)
		return;

	stashGetIterator(interaction_class_hash, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		char *name = stashElementGetKey(elem);
		if (name)
			eaPush(names_earray, name);
	}

	eaQSortG(*names_earray, strCmp);
}

bool wlInteractionBaseIsDestructible(WorldBaseInteractionProperties *props)
{
	if (!props)
		return false;

	if (!destructible_bit)
		destructible_bit = wlInteractionClassNameToBitMask("Destructible");

	return ((props->eInteractionClass & destructible_bit) != 0);
}

Message *wlInteractionGetDestructibleDisplayName(WorldInteractionProperties *props)
{
	int i;

	for(i=eaSize(&props->eaEntries)-1; i>=0; --i)
	{
		if (props->eaEntries[i]->pDestructibleProperties)
			return GET_REF(props->eaEntries[i]->pDestructibleProperties->displayNameMsg.hMessage);
	}

	return NULL;
}

CritterDef *wlInteractionGetDestructibleCritterDef(WorldInteractionProperties *props)
{
	int i;

	for(i=eaSize(&props->eaEntries)-1; i>=0; --i)
	{
		if (props->eaEntries[i]->pDestructibleProperties)
			return GET_REF(props->eaEntries[i]->pDestructibleProperties->hCritterDef);
	}

	return NULL;
}

CritterOverrideDef *wlInteractionGetDestructibleCritterOverride(WorldInteractionProperties *props)
{
	int i;

	for(i=eaSize(&props->eaEntries)-1; i>=0; --i)
	{
		if (props->eaEntries[i]->pDestructibleProperties)
			return GET_REF(props->eaEntries[i]->pDestructibleProperties->hCritterOverrideDef);
	}

	return NULL;
}

int wlInteractionGetDestructibleCritterLevel(WorldInteractionProperties *props)
{
	int i;

	for(i=eaSize(&props->eaEntries)-1; i>=0; --i)
	{
		int level;

		// If the level on the destructible is nonzero, use it.  Otherwise, use the map level
		if (props->eaEntries[i]->pDestructibleProperties && props->eaEntries[i]->pDestructibleProperties->uCritterLevel)
			level = props->eaEntries[i]->pDestructibleProperties->uCritterLevel;
		else
			level = zmapInfoGetMapLevel(NULL);

		return MAX(1,level);
	}

	return 1;
}

U32 wlInteractionGetInteractDist(WorldInteractionProperties *props)
{
	return ( props != NULL ) ? props->uInteractDistCached : 0;
}


//client only
U32 wlInteractionGetTargetDist(WorldBaseInteractionProperties *props)
{
	return ( props != NULL ) ? props->uTargetDist : 0;
}

U32 wlInteractionGetTargetDistForNode(WorldInteractionNode *pNode)
{
	WorldInteractionEntry *pEntry = pNode ? wlInteractionNodeGetEntry(pNode) : NULL;

	if ( pEntry==NULL )
		return 0;

	if (!wlIsServer())
	{
		return (pEntry->base_interaction_properties) ? pEntry->base_interaction_properties->uTargetDist : 0;
	}
	return (pEntry->full_interaction_properties) ? pEntry->full_interaction_properties->uTargetDistCached : 0;
}

U32 wlInteractionGetInteractDistForNode(WorldInteractionNode *pNode)
{
	WorldInteractionEntry *pEntry = pNode ? wlInteractionNodeGetEntry(pNode) : NULL;

	if ( pEntry==NULL )
		return 0;

	if (!wlIsServer())
	{
		return (pEntry->base_interaction_properties) ? pEntry->base_interaction_properties->uInteractDist : 0;
	}
	return (pEntry->full_interaction_properties) ? pEntry->full_interaction_properties->uInteractDistCached : 0;
}

const char* wlInteractionGetInteractFXForNode(WorldInteractionNode *pNode, bool bIsAttemptable)
{
	WorldInteractionEntry *pEntry = pNode ? wlInteractionNodeGetEntry(pNode) : NULL;

	// For now override FX overrides both attemptable and non-attemptable
	if (pEntry)
	{
		if (!wlIsServer())
		{
			if (pEntry->base_interaction_properties &&
				pEntry->base_interaction_properties->pchOverrideFX &&
				pEntry->base_interaction_properties->pchOverrideFX[0])
			{
				return pEntry->base_interaction_properties->pchOverrideFX;
			}
		}
		else
		{
			if (pEntry->full_interaction_properties &&
				pEntry->full_interaction_properties->pchOverrideFX &&
				pEntry->full_interaction_properties->pchOverrideFX[0])
			{
				return pEntry->full_interaction_properties->pchOverrideFX;
			}
		}
	}

	if (!bIsAttemptable)
	{
		return DEFAULT_TARGETABLE_NODE_UNATTEMPTABLE_FX;
	}

	return DEFAULT_TARGETABLE_NODE_FX;
}

const char* wlInteractionGetAdditionalUniqueInteractFXForNode(WorldInteractionNode *pNode)
{
	WorldInteractionEntry *pEntry = pNode ? wlInteractionNodeGetEntry(pNode) : NULL;

	// For now override FX overrides both attemptable and non-attemptable
	if (pEntry)
	{
		if (!wlIsServer())
		{
			if (pEntry->base_interaction_properties &&
				pEntry->base_interaction_properties->pchAdditionalUniqueFX &&
				pEntry->base_interaction_properties->pchAdditionalUniqueFX[0])
			{
				return pEntry->base_interaction_properties->pchAdditionalUniqueFX;
			}
		}
		else
		{
			if (pEntry->full_interaction_properties &&
				pEntry->full_interaction_properties->pchAdditionalUniqueFX &&
				pEntry->full_interaction_properties->pchAdditionalUniqueFX[0])
			{
				return pEntry->full_interaction_properties->pchAdditionalUniqueFX;
			}
		}
	}

	return NULL;
}

bool wlInteractionCanTabSelect(WorldBaseInteractionProperties *props)
{
	return ( props != NULL ) ? props->bTabSelect : false;
}

//client
bool wlInteractionCanTabSelectNode(WorldInteractionNode *pNode)
{
	WorldInteractionEntry *pEntry = pNode ? wlInteractionNodeGetEntry(pNode) : NULL;

	if ( pEntry==NULL )
		return false;

	return (pEntry->base_interaction_properties) ? pEntry->base_interaction_properties->bTabSelect : false;
}

F32 wlInteractionGetDestructibleRespawnTime(WorldInteractionProperties *props)
{
	int i;

	for(i=eaSize(&props->eaEntries)-1; i>=0; --i)
	{
		if (props->eaEntries[i]->pDestructibleProperties)
			return props->eaEntries[i]->pDestructibleProperties->fRespawnTime;
	}

	return 0;
}

PowerDef *wlInteractionGetDestructibleDeathPower(WorldInteractionProperties *props)
{
	int i;

	for(i=eaSize(&props->eaEntries)-1; i>=0; --i)
	{
		if (props->eaEntries[i]->pDestructibleProperties)
			return GET_REF(props->eaEntries[i]->pDestructibleProperties->hOnDeathPowerDef);
	}

	return NULL;
}

const char *wlInteractionGetDestructibleEntityName(WorldInteractionProperties *props)
{
	int i;

	for(i=eaSize(&props->eaEntries)-1; i>=0; --i)
	{
		if (props->eaEntries[i]->pDestructibleProperties)
			return props->eaEntries[i]->pDestructibleProperties->pcEntityName;
	}

	return NULL;
}


void wlInteractionRegisterGameCallbacks(InteractionNodeIsSelectableCallback destructible_selectable_callback)
{
	game_interaction_destructible_selectable_callback = destructible_selectable_callback;
}


static bool checkLoS(int iPartitionIdx, WorldCellEntry *entry, const Vec3 src_pos)
{
	WorldCollCollideResults results;
	bool hit_world;
	int i;

	if (!entry)
		return false;

	hit_world = worldCollideRay(iPartitionIdx, src_pos, entry->bounds.world_mid, WC_QUERY_BITS_WORLD_ALL, &results);

	if (hit_world)
	{
		if (entry->type == WCENT_INTERACTION)
		{
			WorldInteractionEntry *interaction_entry = (WorldInteractionEntry *)entry;

			for (i = 0; i < eaSize(&interaction_entry->child_entries); ++i)
			{
				WorldCellEntry *child_entry = interaction_entry->child_entries[i];

				if (child_entry->type == WCENT_COLLISION)
				{
					WorldCollisionEntry *coll_entry = (WorldCollisionEntry*)child_entry;
					if (worldCollisionEntryGetCollObject(coll_entry, iPartitionIdx) == results.wco)
					{
						hit_world = false;
						break;
					}
				}

				if (checkLoS(iPartitionIdx, child_entry, src_pos))
				{
					hit_world = false;
					break;
				}
			}
		}
		else if (entry->type == WCENT_COLLISION)
		{
			WorldCollisionEntry *coll_entry = (WorldCollisionEntry *)entry;
			if (worldCollisionEntryGetCollObject(coll_entry, iPartitionIdx) == results.wco)
				hit_world = false;
		}
	}

	return !hit_world;
}

bool wlInteractionNodeCheckLineOfSight(int iPartitionIdx, WorldInteractionNode *node, const Vec3 src_pos)
{
	WorldInteractionEntry *entry;
	
	if (!node)
		return false;

	entry = wlInteractionNodeGetEntry(node);
	if (!entry)
		return false;
	
	return checkLoS(iPartitionIdx, &entry->base_entry, src_pos);
}

bool wlInteractionEntryCheckLineOfSight(int iPartitionIdx, WorldInteractionEntry *entry, const Vec3 src_pos)
{
	if (!entry)
		return false;
	
	return checkLoS(iPartitionIdx, &entry->base_entry, src_pos);
}

//////////////////////////////////////////////////////////////////////////
// query

int wlInteractionCheckNodeInQuery(int iPartitionIdx, WorldInteractionNode *node, const Vec3 scenter, F32 sradius, WorldInteractionQuery *query)
{
	WorldInteractionNodeLocalData *local_data = node->local_data;
	Vec3 local_mid;

	assert(local_data);

	if ((query->check_hidden && wlIsInteractionNodeHidden(iPartitionIdx, node)) || wlIsInteractionNodeDisabled(iPartitionIdx, node))
		return 0;

	if (query->check_selection && !node->selectable)
		return 0;

	if (!(local_data->interaction_class & query->interaction_class_mask))
		return 0;

	if (!sphereSphereCollision(local_data->octree_entry.bounds.mid, local_data->octree_entry.bounds.radius, scenter, sradius))
		return 0;

	mulVecMat4(scenter, local_data->inv_world_mat, local_mid);
	if (!boxSphereCollision(local_data->local_min, local_data->local_max, local_mid, sradius))
		return 0;

	if (query->check_line_of_sight && !wlInteractionNodeCheckLineOfSight(iPartitionIdx, node, scenter))
		return 0;

	return 1;
}

static int checkNodeInQueryCB(void *node_in, int node_type, const Vec3 scenter, F32 sradius, void *user_data)
{
	WorldInteractionNode *node = node_in;
	WorldInteractionQuery *query = user_data;

	return wlInteractionCheckNodeInQuery(query->iPartitionIdx, node, scenter, sradius, query);
}

void wlInteractionQuerySphere(int iPartitionIdx, U32 interaction_class_mask, void *user_data, const Vec3 world_mid, F32 radius, bool check_line_of_sight, bool check_selection, bool check_hidden, WorldInteractionNode*** nodes)
{
	WorldInteractionQuery query = {0};

	if (!interaction_node_octree)
		return;

	query.interaction_class_mask = interaction_class_mask;
	query.iPartitionIdx = iPartitionIdx;
	query.user_data = user_data;
	query.check_line_of_sight = check_line_of_sight;
	query.check_selection = check_selection;
	query.check_hidden = check_hidden;
	octreeFindInSphereEA(interaction_node_octree, nodes, world_mid, radius, checkNodeInQueryCB, &query);
}

WorldInteractionEntry* wlInteractionNodeGetEntry(WorldInteractionNode* node)
{
	return GET_REF(node->hDynEntry);
}

void wlInteractionNodeGetWorldMin(WorldInteractionNode *node, Vec3 pos)
{
	WorldInteractionNodeLocalData *local_data = node->local_data;
	if (local_data)
		copyVec3(local_data->octree_entry.bounds.min, pos);
	else
		zeroVec3(pos);
}

void wlInteractionNodeGetWorldMax(WorldInteractionNode *node, Vec3 pos)
{
	WorldInteractionNodeLocalData *local_data = node->local_data;
	if (local_data)
		copyVec3(local_data->octree_entry.bounds.max, pos);
	else
		zeroVec3(pos);
}

void wlInteractionNodeGetWorldMid(const WorldInteractionNode *node, Vec3 pos)
{
	WorldInteractionNodeLocalData *local_data = node->local_data;
	if (local_data)
		copyVec3(local_data->octree_entry.bounds.mid, pos);
	else
		zeroVec3(pos);
}

bool wlInteractionNodeUseChildBounds(WorldInteractionNode *node)
{
	return node->local_data && node->local_data->bUseChildBounds;
}

void wlInteractionNodeGetWorldBounds(WorldInteractionNode *node, Vec3 vMin, Vec3 vMax, Vec3 vMid)
{
	WorldInteractionNodeLocalData *local_data = node->local_data;

	if(local_data)
	{
		copyVec3(local_data->octree_entry.bounds.min, vMin);
		copyVec3(local_data->octree_entry.bounds.max, vMax);
		copyVec3(local_data->octree_entry.bounds.mid, vMid);
	}
	else
	{
		zeroVec3(vMin);
		zeroVec3(vMax);
		zeroVec3(vMid);
	}
}

void wlInteractionNodeGetLocalBounds(WorldInteractionNode *node, Vec3 vMin, Vec3 vMax, Mat4 mWorldMatrix)
{
	WorldInteractionNodeLocalData *local_data = node->local_data;

	if (local_data)
	{
		copyVec3(local_data->local_min, vMin);
		copyVec3(local_data->local_max, vMax);
		if (mWorldMatrix)
		{
			WorldInteractionEntry *entry = GET_REF(node->hDynEntry);
			if (entry)
				copyMat4(entry->base_entry.bounds.world_matrix, mWorldMatrix);
			else
				invertMat4Copy(local_data->inv_world_mat, mWorldMatrix);
		}
	}
	else
	{
		zeroVec3(vMin);
		zeroVec3(vMax);
		if (mWorldMatrix)
			zeroMat4(mWorldMatrix);
	}
}

F32 wlInteractionNodeGetRadius(WorldInteractionNode *node)
{
	WorldInteractionNodeLocalData *local_data = node->local_data;
	return SAFE_MEMBER(local_data, octree_entry.bounds.radius);
}

const char *wlInteractionNodeGetKey(WorldInteractionNode *node)
{
	return node->key;
}

bool wlInteractionNodeIsSelectable(WorldInteractionNode *node)
{
	return node->selectable;
}

void wlInteractionNodeGetRot(WorldInteractionNode *node, Quat rot)
{
	WorldInteractionEntry *entry = GET_REF(node->hDynEntry);

	if (entry)
		mat3ToQuat(entry->base_entry.bounds.world_matrix, rot);
	else
		zeroVec4(rot);
}

bool wlInteractionCheckClass(WorldInteractionNode *node, U32 mask)
{
	return node->local_data && (node->local_data->interaction_class & mask);
}

U32 wlInteractionNodeGetClass(SA_PARAM_NN_VALID WorldInteractionNode *node)
{
	return node->local_data->interaction_class;
}

typedef struct NearestPointResult
{
	Vec3 vSrcPosModelSpace;
	Mat4 mWorldMatrix;
	Vec3 vResult;
	F32 fDistSquared;
}NearestPointResult;

static void geo_ModelProcessCallback(	NearestPointResult* pResult,
										const GeoMeshTempData* meshData)
{
	Vec3 vTriPos;
	int i;
	int iBestTri = -1;

	PERFINFO_AUTO_START_FUNC();
	for (i = 0; i < meshData->tri_count; ++i)
	{
		int verti = i*3;
		F32 fDistSquared;

		// average vertices to get the triangle midpoint
		vTriPos[0] = (meshData->verts[meshData->tris[verti]][0] + meshData->verts[meshData->tris[verti+1]][0] + meshData->verts[meshData->tris[verti+2]][0])/3.f;
		vTriPos[1] = (meshData->verts[meshData->tris[verti]][1] + meshData->verts[meshData->tris[verti+1]][1] + meshData->verts[meshData->tris[verti+2]][1])/3.f;
		vTriPos[2] = (meshData->verts[meshData->tris[verti]][2] + meshData->verts[meshData->tris[verti+1]][2] + meshData->verts[meshData->tris[verti+2]][2])/3.f;

		fDistSquared = distance3Squared(pResult->vSrcPosModelSpace, vTriPos);
		if (pResult->fDistSquared > fDistSquared || pResult->fDistSquared < 0)
		{
			pResult->fDistSquared = fDistSquared;
			iBestTri = i;
		}
	}

	if (iBestTri != -1)
	{
		// refine the position
		int verti = iBestTri*3;
		Vec3 vBest;
		closestPointOnTriangle(meshData->verts[meshData->tris[verti]],meshData->verts[meshData->tris[verti+1]],meshData->verts[meshData->tris[verti+2]],pResult->vSrcPosModelSpace,vBest);
		pResult->fDistSquared = distance3Squared(pResult->vSrcPosModelSpace, vBest);
		mulVecMat4(vBest, pResult->mWorldMatrix, pResult->vResult);
	}

	PERFINFO_AUTO_STOP();
}

F32 wlInteractionNodeNearestPoint_Recurse(Vec3 vPos, WorldInteractionEntry *cell_entry, 
										  NearestPointResult *pResultOut, bool* pbMeshLoadFailed)
{
	int i;
	Mat4 mInvWorldMat;

	for(i=0;i<eaSize(&cell_entry->child_entries);i++)
	{
		WorldCellEntry *child_entry = cell_entry->child_entries[i];

		if (child_entry->type == WCENT_COLLISION)
		{
			WorldCollisionEntry *coll_entry = (WorldCollisionEntry*)child_entry;
			if (coll_entry->model)
			{
				PSDKCookedMesh* psdkMesh;
				GeoMeshTempData gtd = {0};
				copyMat4(coll_entry->base_entry.bounds.world_matrix, pResultOut->mWorldMatrix);
				invertMat4Copy(coll_entry->base_entry.bounds.world_matrix, mInvWorldMat);
				mulVecMat4(vPos, mInvWorldMat, pResultOut->vSrcPosModelSpace);
				psdkMesh = geoCookMesh(	coll_entry->model,
										coll_entry->scale,
										NULL,
										coll_entry,
										true,
										false);

				if ( psdkMesh==NULL )
				{
					if ( pbMeshLoadFailed )
						(*pbMeshLoadFailed) = true;

					continue;
				}
				
#if !PSDK_DISABLED
				psdkCookedMeshGetTriangles(psdkMesh, &gtd.tris, &gtd.tri_count);
				psdkCookedMeshGetVertices(psdkMesh, &gtd.verts, &gtd.vert_count);
#endif
				geo_ModelProcessCallback(pResultOut, &gtd);

				#if 0
					geoProcessTempData(	geo_ModelProcessCallback,
										pResultOut,
										coll_entry->model,
										0,
										coll_entry->scale,
										true,
										false,
										false,
										false,
										NULL);
				#endif
			}
		}
		else if(child_entry->type == WCENT_INTERACTION)
		{
			WorldInteractionEntry *pInteractionEntry = (WorldInteractionEntry*)child_entry;

			wlInteractionNodeNearestPoint_Recurse(vPos,pInteractionEntry,pResultOut,pbMeshLoadFailed);
		}
	}

	return pResultOut->fDistSquared;
}

F32 wlInterationNode_FindNearestPointFast_Recurse(Vec3 vPos, WorldInteractionEntry *cell_entry, Vec3 vResultOut)
{
	F32 fCloseDist = FLT_MAX;
	S32 i;
	bool bFound = false;
	
	for(i=0;i<eaSize(&cell_entry->child_entries);i++)
	{
		WorldCellEntry *child_entry = cell_entry->child_entries[i];

		if (child_entry->type == WCENT_COLLISION)
		{
			F32 fCurDist;
			Vec3 vClose;
			WorldCollisionEntry *coll_entry = (WorldCollisionEntry*)child_entry;
			WorldCellEntryBounds *bounds = &coll_entry->base_entry.bounds;
			WorldCellEntrySharedBounds *shared_bounds = coll_entry->base_entry.shared_bounds;
			char colModelName[1024];
			ModelHeader *pColModelHeader;

			if (!devassertmsgf(coll_entry->model, "WorldCollisionEntry found at position %f, %f, %f with no model!",
				bounds->world_mid[0], bounds->world_mid[1], bounds->world_mid[2]))
			{
				continue;
			}

			STR_COMBINE_SS(colModelName, coll_entry->model->name, "_COLL");

			pColModelHeader = wlModelHeaderFromName(colModelName);

			if (pColModelHeader)
			{
				// Use the bounding box of the collision geo
				fCurDist = boxPointNearestPoint( pColModelHeader->min, pColModelHeader->max, bounds->world_matrix, NULL, vPos, vClose );
			}
			else
			{
				fCurDist = boxPointNearestPoint( shared_bounds->local_min, shared_bounds->local_max, bounds->world_matrix, NULL, vPos, vClose );
			}			

			if ( fCurDist < fCloseDist )
			{
				fCloseDist = fCurDist;
				copyVec3(vClose,vResultOut);
				bFound = true;
			}
		}
		else if(child_entry->type == WCENT_INTERACTION)
		{
			Vec3 vClose;
			
			WorldInteractionEntry *pInteractionEntry = (WorldInteractionEntry*)child_entry;

			F32 fCurDist = wlInterationNode_FindNearestPointFast_Recurse(vPos,pInteractionEntry,vClose);

			if ( fCurDist < fCloseDist )
			{
				fCloseDist = fCurDist;
				copyVec3(vClose,vResultOut);
				bFound = true;
			}
		}
	}

	if ( bFound == false )
	{
		return -1;
	}

	return fCloseDist;
}

//finds the closest point on one of this node's bounding boxes to vPos, -1 on error
F32 wlInterationNode_FindNearestPointFast(Vec3 vPos, WorldInteractionNode *node, Vec3 vResultOut)
{
	F32 fCloseDist;
	WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(node);

	if ( pEntry==NULL )
		return -1;

	fCloseDist = wlInterationNode_FindNearestPointFast_Recurse(vPos, pEntry, vResultOut);

	if ( fCloseDist < 0 )	
	{
		S32 i;
		Vec3 vNodePos, vNodeMin, vNodeMax, vClose;
		wlInteractionNodeGetWorldBounds(node,vNodeMin,vNodeMax,vNodePos);

		for ( i = 0; i < 3; i++ )
		{
			F32 v = vPos[i];
			if ( v < vNodeMin[i] ) v = vNodeMin[i];
			if ( v > vNodeMax[i] ) v = vNodeMax[i];
			vClose[i] = v;
		}

		if (vResultOut)
			copyVec3(vClose, vResultOut);

		return distance3(vClose,vPos);
	}

	return fCloseDist;
}

F32 wlInterationNode_FindNearestPoint(Vec3 vPos, WorldInteractionNode *node, Vec3 vResultOut)
{
	WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(node);
	NearestPointResult result;
	bool bFoundCollision = false;
	bool bMeshFail = false;

	if (!pEntry)
		return 0.0f;

	PERFINFO_AUTO_START_FUNC();

	result.fDistSquared = -1;

	bFoundCollision = wlInteractionNodeNearestPoint_Recurse(vPos,pEntry,&result,&bMeshFail) > -1;

	if ( bMeshFail )
	{
		Vec3 vMid;
		wlInteractionNodeGetWorldMid(node,vMid);
		
		if ( vResultOut )
			copyVec3(vMid, vResultOut);

		return distance3(vMid,vPos);
	}
	else if( !bFoundCollision )
	{
		S32 i;
		Vec3 vNodePos, vNodeMin, vNodeMax, vClose;
		wlInteractionNodeGetWorldBounds(node,vNodeMin,vNodeMax,vNodePos);

		for ( i = 0; i < 3; i++ )
		{
			F32 v = vPos[i];
			if ( v < vNodeMin[i] ) v = vNodeMin[i];
			if ( v > vNodeMax[i] ) v = vNodeMax[i];
			vClose[i] = v;
		}

		if (vResultOut)
			copyVec3(vClose, vResultOut);
		
		PERFINFO_AUTO_STOP();
		
		return distance3(vClose,vPos);
	}
	else
	{
		if(vResultOut)
			copyVec3(result.vResult, vResultOut);

		PERFINFO_AUTO_STOP();

		return result.fDistSquared < 0 ? -1 : sqrtf(result.fDistSquared);
	}

	PERFINFO_AUTO_STOP();

	return -1;
}

void wlInteractionOncePerFrame(F32 frame_time)
{
	PERFINFO_AUTO_START_FUNC();

	interaction_node_count = resDictGetNumberOfObjects(interaction_dict);

	PERFINFO_AUTO_STOP();
}

void wlInteractionNodeGetCachedPlayerPos( WorldInteractionNode* pNode, Vec3 vReturn )
{
	if(pNode && wlIsClient()) {
		copyVec3(pNode->vCachedPlayerPos, vReturn);
	}
}

void wlInteractionNodeGetCachedNearestPoint( WorldInteractionNode* pNode, Vec3 vReturn )
{
	if(pNode && wlIsClient()) {
		copyVec3(pNode->vCachedNearestPoint, vReturn);
	}
}

void wlInteractionNodeSetCachedPlayerPos( WorldInteractionNode* pNode, Vec3 vPos )
{
	if(pNode && wlIsClient()) {
		copyVec3(vPos, pNode->vCachedPlayerPos);
	}
}

void wlInteractionNodeSetCachedNearestPoint( WorldInteractionNode* pNode, Vec3 vPos )
{
	if(pNode && wlIsClient()) {
		copyVec3(vPos, pNode->vCachedNearestPoint);
	}
}

//////////////////////////////////////////////////////////////////////////
// debugging commands

AUTO_COMMAND ACMD_CLIENTONLY ACMD_NAME("ClientSetVisibleChildAll") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void wlInteractionClientSetVisibleChildAll(int child_idx)
{
 	StashTableIterator iterator;
 	WorldInteractionEntry *entry;
 	StashElement element;
 	ZoneMap *zmap = worldGetActiveMap();
	int i;
 
 	stashGetIterator(zmap->world_cell_data.interaction_node_hash, &iterator);
 	while (stashGetNextElement(&iterator, &element))
 	{
 		entry = stashElementGetPointer(element);

		for (i = eaSize(&entry->child_entries) - 1; i >= 0; --i)
		{
			WorldCellEntry *child_entry = entry->child_entries[i];
			WorldCellEntryData *data = worldCellEntryGetData(child_entry);
			bool bDisabled;

			if (data && data->interaction_child_idx >= 0) {
				if (data->interaction_child_idx == child_idx) {
					bDisabled = false;
				} else {
					bDisabled = true;
				}
				worldCellEntrySetDisabled(PARTITION_CLIENT, child_entry, bDisabled);

				// recurse into interaction nodes
				if (child_entry->type == WCENT_INTERACTION)	{
					WorldInteractionEntry *child_interaction = (WorldInteractionEntry *)child_entry;
					worldInteractionEntrySetDisabled(PARTITION_CLIENT, child_interaction, bDisabled);
				}
			}
		}
 	}
}

bool wlInteractionNodeValidate(WorldInteractionNode *node)
{
	WorldInteractionEntry *entry;
	ZoneMap *zmap;
	int uid;

	if (!node->key)
		return false;
	if (!node->key[0])
		return false;

	if (!wlInteractionUidFromName(node->key, &zmap, &uid))
		return false;

	entry = GET_REF(node->hDynEntry);
	if (entry)
	{
		if (entry->base_entry.type != WCENT_INTERACTION)
			return false;
		if (!entry->hasInteractionNode)
			return false;
		if (GET_REF(entry->hInteractionNode) != node)
			return false;
		if (!entry->uid)
			return false;
		if (entry->uid != uid)
			return false;
	}

	return true;
}

const char* wlInteractionNodeGetDisplayName(WorldInteractionNode* pNode)
{
	WorldInteractionEntry* entry = SAFE_GET_REF(pNode, hDynEntry);
	if (!entry) return NULL;
	return langTranslateMessage(locGetLanguage(getCurrentLocale()), GET_REF(entry->base_interaction_properties->hDisplayNameMsg));
}


// Data and functions for world layer callbacks to check hidden and disabled states
static NodeCallbackFunc s_NodeHiddenCallback = NULL;
static NodeCallbackFunc s_NodeDisabledCallback = NULL;

int wlIsInteractionNodeHidden(int iPartitionIdx, WorldInteractionNode *pNode)
{
	if (s_NodeHiddenCallback) {
		return (s_NodeHiddenCallback)(iPartitionIdx, pNode);
	}
	return 0;
}

int wlIsInteractionNodeDisabled(int iPartitionIdx, WorldInteractionNode *pNode)
{
	if (s_NodeHiddenCallback) {
		return (s_NodeDisabledCallback)(iPartitionIdx, pNode);
	}
	return 0;
}

void wlInteractionRegisterCallbacks(NodeCallbackFunc nodeHiddenCallback,
									NodeCallbackFunc nodeDisabledCallback)
{
	s_NodeHiddenCallback = nodeHiddenCallback;
	s_NodeDisabledCallback = nodeDisabledCallback;
}

// returns true if the pObject is part of the interaction entry
bool wlInteractionCheckCollObject(int iPartitionIdx,WorldInteractionEntry *pInteractionEntry,WorldCollObject * pObject)
{
	int i;
	devassert(pInteractionEntry);

	for(i = 0; i < eaSize(&pInteractionEntry->child_entries); ++i)
	{
		if (pInteractionEntry->child_entries[i]->type == WCENT_COLLISION)
		{
			WorldCollisionEntry *pChild = (WorldCollisionEntry*)pInteractionEntry->child_entries[i];
			if (worldCollisionEntryGetCollObject(pChild, iPartitionIdx) == pObject)
			{
				return true;
			}
		}

		if (pInteractionEntry->child_entries[i]->type == WCENT_INTERACTION)
		{
			WorldInteractionEntry *pChild = (WorldInteractionEntry*)pInteractionEntry->child_entries[i];
			WorldInteractionNode *pNodeChild = GET_REF(pChild->hInteractionNode);
			int n;

			for(n = 0; n<eaSize(&pChild->child_entries); ++n)
			{
				if(pChild->child_entries[n]->type == WCENT_COLLISION)
				{
					WorldCollisionEntry *pChildOfChild = (WorldCollisionEntry*)pChild->child_entries[n];
					if (worldCollisionEntryGetCollObject(pChildOfChild, iPartitionIdx) == pObject)
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}
