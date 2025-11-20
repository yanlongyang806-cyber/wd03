#ifndef CLIENTTARGETING_H
#define CLIENTTARGETING_H
GCC_SYSTEM

#include "referencesystem.h"

typedef struct Character Character;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct Power Power;
typedef struct PowerDef PowerDef;
typedef struct PowerTarget PowerTarget;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct CBox CBox;
typedef U32	   EntityRef;
typedef struct UICursor UICursor;

typedef struct ClientTargetDef
{
	EntityRef entRef;
	REF_TO(WorldInteractionNode) hInteractionNode;
	F32 fDist;
	F32 fSortDist;
	bool bSoft;
	bool bIsOffscreen;
	S32 luckyCharmType;
	S32 luckyCharmIndex;
}ClientTargetDef;


typedef struct ClientTargetMutableDef
{
	EntityRef entRef;
	REF_TO(WorldInteractionNode) hInteractionNode;
	F32 fDist;
	F32 fSortDist;
	bool bSoft;
}ClientTargetMutableDef;


typedef int (*ClientTargetSortFunc) (const ClientTargetDef **DefA, const ClientTargetDef **DefB);
typedef int (*ClientTargetVisibleCheck) (Entity *pEnt, WorldInteractionNode *pNode);
typedef int (*ClientTargetFilterCheck) (Entity *pEnt, WorldInteractionNode *pNode);
typedef F32 (*ClientTargetSortDistCheck) (Entity *pEnt, WorldInteractionNode *pNode);


// Targeting Utilities
bool target_ClampToMinimumInteractBox( CBox* pBox );
S32 target_IsLegalTargetForExpression(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID Expression *pExprLegal, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID WorldInteractionNode *pNode);
WorldInteractionNode *target_GetPotentialLegalNode(void);
void target_GetCursorRayEx(Entity* e, Vec3 vCursorStart, Vec3 vCursorDir, bool bCheckShowMouseLookReticle, bool bCombatEntityTargeting);
#define target_GetCursorRay(e, vCursorStart, vCursorDir) target_GetCursorRayEx(e, vCursorStart, vCursorDir, false, false)
int wlInteractionNodeGetWindowScreenPos(WorldInteractionNode *pNode, Vec2 pixel_pos, F32 yOffsetInFeet);
bool wlNodeIsVisible(WorldInteractionNode *pNode);


// Mouse Targeting
Entity * target_SelectUnderMouseEx(Entity *e, PowerTarget *pPowerTarget, U32 target_type_req, U32 target_type_exc, Vec3 worldPos, bool bSelectable, bool bIsClick, bool preferInteractables);
Entity * target_SelectUnderMouse(Entity *e, U32 target_type_req, U32 target_type_exc, Vec3 worldPos, bool bSelectable, bool bIsClick, bool preferInteractables);
WorldInteractionNode * target_SelectObjectUnderMouse(Entity *e, U32 interaction_class_mask);
WorldInteractionNode * target_SelectTooltipObjectUnderMouse(Entity *e);
bool target_IsObjectTargetable(Entity* e, WorldInteractionNode *pTargetNode);
Entity* getEntityUnderMouse(bool bExcludePlayer);


// Debug commands
void cmdCmdOnClick(S32 enabled, const char* cmd1, const char* cmd2);

// Once a frame tick
void clientTarget_tick(F32 fTickTime);

//Target cycling
bool gclClientTarget_TargetCyclingDisabled(void);
void clientTarget_CycleEx(bool bBackwards, U32 targetRequirements, U32 targetExclusions, ClientTargetFilterCheck cbFilterFunc);

//Slash Commands for targeting
const ClientTargetDef *clientTarget_FindNext(Entity *e, bool bBackwards, U32 target_type_req, U32 target_type_exc, ClientTargetFilterCheck cbFilterFunc);
const ClientTargetDef *clientTarget_FindNextManual(Entity *e, bool bBackwards, U32 target_type_req, U32 target_type_exc, Vec2 dir);

const ClientTargetDef *clientTarget_FindNearest(Entity *e, U32 target_type_req, U32 target_type_exc, float arc, bool bForwardArc, bool bRotArc90, bool bBothArcs);
const ClientTargetDef *clientTarget_FindFarthest(Entity *e, U32 target_type_req, U32 target_type_exc);

void clientTarget_GetVecTargetingDirection(Entity *pPlayerEnt, SA_PARAM_OP_VALID PowerDef *pdef, Vec3 vDirectionOut);
Entity* clientTarget_FindProximityTargetingAssistEnt(Entity *pPlayerEnt, const Vec3 vDirection);


ClientTargetDef *clientTarget_SelectBestTargetForPowerEx(Entity *pEntPlayer, Power *ppow, PowerTarget *pPowTargetOverride, bool *pShouldSetHard);
ClientTargetDef *clientTarget_SelectBestTargetForPower(Entity *pEntPlayer, Power *ppow, bool *pShouldSetHard);
const ClientTargetDef *clientTarget_GetCurrentTarget(void);
const ClientTargetDef *clientTarget_GetCurrentHardTarget(void);
Entity* clientTarget_GetCurrentHardTargetEntity();

bool clientTarget_IsTargetHard(void);
bool clientTarget_IsTargetSoft(void);
bool clientTarget_MatchesType(Entity *e, U32 target_type_req, U32 target_type_exc);
bool clientTarget_MatchesTypeEx(Entity *ePlayer, Entity *eTarget, PowerTarget *pPowerTarget, U32 target_type_req, U32 target_type_exc);


bool clientTarget_IsTargetActive(const ClientTargetDef *pTarget);
bool clientTarget_HasHardTarget(Entity *e);

int clientTarget_CheckLOS(Entity *entSource, ClientTargetDef *pTarget);
void clientTarget_Clear(void);
void clientTarget_ClearIfOffscreen(F32 elapsedTime);
void clientTarget_ResetTargetChangeTimer(void);

bool clientTarget_InteractionNodeGetWindowScreenPos(WorldInteractionNode *pNode, Vec2 pixel_pos, F32 yOffsetInFeet);

// Finds a target location a given distance from the player in the direction of the center of the screen
S32 clientTarget_GetSimpleSecondaryRangeTarget(F32 fRange, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecTargetOut);

// Gets the near point to the target node: result is cached for the current frame, so use this function!
bool clientTarget_GetNearestPointForTargetNode(Entity* pPlayerEnt, WorldInteractionNode* pNodeTarget, Vec3 vNear);
// Cached (per-frame) LoS check for current target
bool clientTarget_IsTargetInLoS(Entity* pPlayerEnt, Vec3 vCamera, Entity* eTarget, WorldInteractionNode* pNodeTarget);
// specific check for Entity's target, calls clientTarget_IsTargetInLoS
bool clientTarget_IsMyTargetInLoS( Entity* pPlayerEnt );

void MouseLookTargeting(int activate);
void clientTarget_HandleServerChange(void);
void clientTarget_ChangedClientTarget(void);
void clientTarget_ResetClientTarget(void);
void clientTarget_HandleServerFocusChange(void);
void clientTarget_ChangedClientFocusTarget(void);
void clientTarget_ResetClientFocusTarget(void);
void clientTarget_ResetFaceTimeout(void);
void clientTarget_NotifyAttacked(Entity *ent);


// NearDeath utility function, gets the best NearDeath Power and optionally the target
//  based on client targeting rules
U32 clientTarget_GetBestNearDeathPower(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID EntityRef *pEntRefTargetOut, bool bConfineToCursor);

// Auto Attack

bool gclAutoAttack_IsEnabled();

// Returns true if this PowerID is legal for AutoAttack.  Uses the cached calculation.
S32 gclAutoAttack_PowerIDLegal(U32 uiID);

// Returns true if AutoAttack is currently enabled and including this PowerID
S32 gclAutoAttack_PowerIDAttacking(U32 uiID);

// Disables AutoAttack and does some misc cleanup
void gclAutoAttack_Disable(void);

// DefaultAutoAttack <1/0>: Enable or disable auto attack
void gclAutoAttack_DefaultAutoAttack(int enable);

// sets the auto-attack power that should be cast
void gclAutoAttack_SetExplicitPowerEnabledID(U32 powerID);

bool gclAutoAttack_IsExplicitPowerEnabled(U32 powerID);

bool gclAutoAttack_IsExplicitPowerSet();

U32 gclAutoAttack_GetExplicitPowerID();

// ToggleDefaultAutoAttack: Toggles the state of auto attack
void gclAutoAttack_ToggleDefaultAutoAttack(void);

// Sets the override auto attack ID
void gclAutoAttack_SetOverrideID(U32 uiID);

// Returns the override auto attack ID
U32 gclAutoAttack_GetOverrideID(void);

void gclAutoAttack_StopActivations(bool bTargetSwitch, bool bPowerSwitch);

// MultiExec (similar to Auto Attack, but each Power added is only executed one time)

// Adds a Power ID to the list of Powers to attempt to exec all at once
// Also enables autoattack if the Power ID is a legal one for autoattack
void clientTarget_AddMultiPowerExec(U32 uiID);

// Internal version of the standard MultiPowerExec call.  AutoAttack enabling
//  is optional, as well as clearing the existing MultiExec list.
void clientTarget_AddMultiPowerExecEx(U32 uiID, S32 bEnableAutoAttack, S32 bClear);

// Returns true if the PowerID is in the MultiExec list
S32 clientTarget_IsMultiPowerExec(U32 uiID);

// Gets the max range of the current active weapon
F32 gclClientTarget_GetActiveWeaponRangeEx(Entity* pEnt, S32 iActiveWeaponIndex, S32 iPowerIdx);
#define gclClientTarget_GetActiveWeaponRange(pEnt) gclClientTarget_GetActiveWeaponRangeEx(pEnt, 0, -1)

int clientTarget_IsMouseOverBoxCheck(Entity *entTarget, WorldInteractionNode *pNode);

extern bool g_bSelectAnyEntity;

extern U32 iBitMaskDes;
extern U32 iBitMaskThrow;
extern U32 iBitMaskClick;
extern U32 iBitMaskDoor;
extern U32 iBitMaskNamed;

void setClientBitmasks(void);

Power* clientTarget_GetMultiExecPower(int index);
void cursorModeDefault_OnClickEx(bool bDown, Entity** pTargetOut, WorldInteractionNode** pNodeOut, Vec3 vecOut);
void targetCursor_Update(void);

// Returns the cursor set by the client targeting code
UICursor * targetCursor_GetCurrent(void);

void clientTarget_SetCameraTargetingUsesDirectionKeysOverride(S32 bEnabled, S32 bUseDirectionalKeys);

void gclClientTarget_Shutdown();

#endif