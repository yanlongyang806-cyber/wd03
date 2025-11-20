#ifndef __WLINTERACTION_H__
#define __WLINTERACTION_H__
GCC_SYSTEM

#include "ResourceManager.h"

#define INTERACTION_DICTIONARY "InteractionDictionary"
#define ENTRY_DICTIONARY "WorldDynamicEntryDictionary"

typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct WorldBaseInteractionProperties WorldBaseInteractionProperties;
typedef struct WorldInteractionProperties WorldInteractionProperties;
typedef struct WorldCollisionEntry WorldCollisionEntry;
typedef struct ExprContext ExprContext;
typedef struct CritterDef CritterDef;
typedef struct CritterOverrideDef CritterOverrideDef;
typedef struct PowerDef PowerDef;
typedef struct Message Message;
typedef struct Entity Entity;
typedef struct ZoneMap ZoneMap;
typedef struct WorldCollObject WorldCollObject;
typedef U32 EntityRef;

extern ParseTable parse_WorldInteractionNode[];
#define TYPE_parse_WorldInteractionNode WorldInteractionNode

typedef struct WorldInteractionQuery
{
	U32 interaction_class_mask;
	int iPartitionIdx;
	bool check_line_of_sight;
	bool check_selection;
	bool check_hidden;
	void *user_data;
} WorldInteractionQuery;

//////////////////////////////////////////////////////////////////////////
// Interaction Class is the action type an entity wants to use 
// on a node, such as "Clickable" or "Throwable"
//
// Interaction Type is the actual type of node it is, such as 
// MissionObjective, and defines callbacks for when the node is activated
//
//////////////////////////////////////////////////////////////////////////


typedef bool (*InteractionNodeTestCallbackEnt)(Entity* ent, WorldInteractionNode *node, void *user_data);
typedef bool (*InteractionNodeTestCallback)(WorldInteractionNode *node, void *user_data);
typedef bool (*InteractionNodeIsSelectableCallback)(WorldInteractionNode *node);


void wlInteractionSystemStartup(void);
void wlInteractionFreeAllNodes(void);


U32 wlInteractionClassNameToBitMask(SA_PARAM_NN_STR const char *class_name);
bool wlInteractionClassMatchesMask(WorldInteractionNode* pNode, U32 iMask);
void wlInteractionGetClassNames(const char ***names_earray);

bool wlInteractionBaseIsDestructible(WorldBaseInteractionProperties *props);

// These are accessors for destructible object properties
Message *wlInteractionGetDestructibleDisplayName(WorldInteractionProperties *props);
CritterDef *wlInteractionGetDestructibleCritterDef(WorldInteractionProperties *props);
CritterOverrideDef *wlInteractionGetDestructibleCritterOverride(WorldInteractionProperties *props);
int wlInteractionGetDestructibleCritterLevel(WorldInteractionProperties *props);
PowerDef *wlInteractionGetDestructibleDeathPower(WorldInteractionProperties *props);
F32 wlInteractionGetDestructibleRespawnTime(WorldInteractionProperties *props);
const char *wlInteractionGetDestructibleEntityName(WorldInteractionProperties *props);
U32 wlInteractionGetInteractDist(WorldInteractionProperties *props);
U32 wlInteractionGetInteractDistForNode(WorldInteractionNode *pNode);
U32 wlInteractionGetTargetDist(WorldBaseInteractionProperties *props);
U32 wlInteractionGetTargetDistForNode(WorldInteractionNode *pNode);
const char* wlInteractionGetInteractFXForNode(WorldInteractionNode *pNode, bool bIsAttemptable);
const char* wlInteractionGetAdditionalUniqueInteractFXForNode(WorldInteractionNode *pNode);
bool wlInteractionCanTabSelect(WorldBaseInteractionProperties *props);
bool wlInteractionCanTabSelectNode(WorldInteractionNode *pNode);

void wlInteractionRegisterGameCallbacks(InteractionNodeIsSelectableCallback destructible_selectable_callback);

void wlInteractionOpenForEntry(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID WorldInteractionEntry *entry);
void wlInteractionCloseForEntry(SA_PARAM_NN_VALID WorldInteractionEntry *entry);
void wlInteractionAddToDictionary(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID WorldInteractionEntry *entry);

WorldBaseInteractionProperties *wlInteractionCreateBaseProperties(WorldInteractionProperties *props);

bool wlInteractionNodeCheckLineOfSight(int iPartitionIdx, SA_PARAM_OP_VALID WorldInteractionNode *node, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 src_pos);
bool wlInteractionEntryCheckLineOfSight(int iPartitionIdx, SA_PARAM_OP_VALID WorldInteractionEntry *entry, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 src_pos);

void wlInteractionQuerySphere(int iPartitionIdx, U32 interaction_class_mask, void *user_data, 
																 SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 world_mid, 
																 F32 radius,
																 bool check_line_of_sight, 
																 bool check_selection,
																 bool check_hidden,
																 SA_PARAM_NN_VALID WorldInteractionNode*** nodes);

int wlInteractionCheckNodeInQuery(int iPartitionIdx, WorldInteractionNode *node, const Vec3 scenter, F32 sradius, WorldInteractionQuery *query);

// node accessors
F32 wlInteractionNodeGetSphereBounds(SA_PARAM_NN_VALID WorldInteractionNode *node, SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 world_mid); // returns radius

// client functions
void wlInteractionNodeSetClientFXName(SA_PARAM_NN_VALID WorldInteractionNode *node, SA_PARAM_OP_STR const char *fx_name);
void wlInteractionNodeGetCachedPlayerPos( WorldInteractionNode* pNode, Vec3 vReturn );
void wlInteractionNodeGetCachedNearestPoint( WorldInteractionNode* pNode, Vec3 vReturn );
void wlInteractionNodeSetCachedPlayerPos( WorldInteractionNode* pNode, Vec3 vPos );
void wlInteractionNodeSetCachedNearestPoint( WorldInteractionNode* pNode, Vec3 vPos );

// server functions
void wlInteractionNodeSetSelectable(int iPartitionIdx, SA_PARAM_NN_VALID WorldInteractionNode *pNode, bool isSelectable);
void wlInteractionNodeSetSelectableAll(WorldInteractionNode *node, bool isSelectable);

bool wlInteractionCheckClass(SA_PARAM_NN_VALID WorldInteractionNode *node, U32 mask);
U32 wlInteractionNodeGetClass(SA_PARAM_NN_VALID WorldInteractionNode *node);

WorldInteractionEntry *wlInteractionNodeGetEntry(SA_PARAM_NN_VALID WorldInteractionNode *node);

SA_RET_NN_STR const char *wlInteractionNodeGetKey(SA_PARAM_NN_VALID WorldInteractionNode *node);
bool wlInteractionNodeIsSelectable(SA_PARAM_NN_VALID WorldInteractionNode *node);
void wlInteractionNodeGetWorldMid(SA_PARAM_NN_VALID const WorldInteractionNode *node,  SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 pos);
F32 wlInteractionNodeGetRadius(SA_PARAM_NN_VALID WorldInteractionNode *node);
void wlInteractionNodeGetWorldMin(SA_PARAM_NN_VALID WorldInteractionNode *node, SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 pos);
void wlInteractionNodeGetWorldMax(SA_PARAM_NN_VALID WorldInteractionNode *node, SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 pos);
bool wlInteractionNodeUseChildBounds(SA_PARAM_NN_VALID WorldInteractionNode *node);
void wlInteractionNodeGetWorldBounds(SA_PARAM_NN_VALID WorldInteractionNode *node,SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 vMin, SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 vMax, SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 vMid);
void wlInteractionNodeGetLocalBounds(SA_PARAM_NN_VALID WorldInteractionNode *node, SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 vMin, SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 vMax, SA_PRE_OP_BYTES(sizeof(Mat4)) Mat4 mWorldMatrix);
void wlInteractionNodeGetRot(SA_PARAM_NN_VALID WorldInteractionNode *node, SA_PRE_NN_BYTES(sizeof(Quat)) Quat rot);

F32 wlInterationNode_FindNearestPoint(Vec3 vPos, SA_PARAM_NN_VALID WorldInteractionNode *node, SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 vResultOut);

F32 wlInterationNode_FindNearestPointFast(Vec3 vPos, SA_PARAM_NN_VALID WorldInteractionNode *node, SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 vResultOut);

bool wlInteractionNodeValidate(SA_PARAM_NN_VALID WorldInteractionNode *node);

void wlInteractionClientSetVisibleChildAll(int child_idx);

LATELINK;
bool wlInteractionGetSelectableFromCritter(SA_PARAM_NN_VALID WorldInteractionNode *node);

void wlInteractionOncePerFrame(F32 frame_time);

void wlInteractionNameFromUid(char *name_str, size_t name_size, ZoneMap *zmap, int uid);
bool wlInteractionUidFromName(const char *name_str, ZoneMap **zmap_out, int *uid_out);

const char* wlInteractionNodeGetDisplayName(WorldInteractionNode* pNode);


// Data and functions for hidden/disabled state callbacks
typedef int (*NodeCallbackFunc)(int iPartitionIdx, WorldInteractionNode *pNode);

void wlInteractionRegisterCallbacks(NodeCallbackFunc nodeHiddenCallback,
									NodeCallbackFunc nodeDisabledCallback);

int wlIsInteractionNodeHidden(int iPartitionIdx, WorldInteractionNode *pNode);
int wlIsInteractionNodeDisabled(int iPartitionIdx, WorldInteractionNode *pNode);

bool wlInteractionCheckCollObject(int iPartitionIdx, WorldInteractionEntry *pInteractionEntry,WorldCollObject * pObject);

#endif //__WLINTERACTION_H__
