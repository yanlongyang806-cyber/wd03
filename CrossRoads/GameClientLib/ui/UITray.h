/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCL_UI_UITRAY_H
#define GCL_UI_UITRAY_H
GCC_SYSTEM

typedef struct Entity		Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct Item			Item;
typedef struct TrayElem		TrayElem;
typedef struct PowerDef		PowerDef;
typedef struct Character	Character;
typedef struct PetPowerState PetPowerState;
typedef struct PlayerPetInfo PlayerPetInfo;

typedef struct Power		Power;
typedef struct WorldInteractionNode WorldInteractionNode;

typedef enum PowerPurpose PowerPurpose;

AUTO_STRUCT;
typedef struct PetTrayElemData
{
	EntityRef			erOwner;
	const char*			pchPower;		AST(POOL_STRING)
	const char*			pchAiState;		AST(POOL_STRING)
	F32					fTimerRecharge;
	F32					fTimerRechargeBase;
} PetTrayElemData;

extern ParseTable parse_PetTrayElem[];
#define TYPE_parse_PetTrayElem PetTrayElem

AUTO_STRUCT;
typedef struct UITrayElem
{
	TrayElem *pTrayElem; AST(UNOWNED)
		// The TrayElem under this

	PetTrayElemData *pPetData; AST(ADDNAMES(PetTrayElem))
		// Valid if this is a pet power and it doesn't have full power information

	Item *pSourceItem; AST(UNOWNED)
		// The source item for the activated power

	const char *pchIcon; AST(POOL_STRING)
		// The icon to display

	char* estrShortDesc; AST(NAME(Description) ESTRING)
		// Optional description text

	int iTray;
		// The tray this is in (from TrayElem)

	int iSlot;
		// The slot of the tray this is in (from TrayElem)

	PowerPurpose ePurpose;
		// Determines how this power is ordered in the UI (from PowerDef)

	const char *pchDragType;	AST(UNOWNED)
		// The type that this drags as (usually "TrayElem")
	
	int iKey;
		// Key used for drag/drop

	int iHighlightType;
		// The type of highlighting that should happen (if bHighlight is true)

	S32 iLastStackedItemCount;
	U32 uiNextStackedItemUpdate;

	S32 iNumTotalCharges;
		// The number of total charges if applicable.

	U32 bValid : 1;
		// The UITrayElem has useful data (though Tray, Slot and Key are always useful)

	U32 bActivatable : 1;
		// The object can be activated

	U32 bInRange : 1;
		// The object meets range requirements to activate

	U32 bEnoughPower: 1;
		// The object meets power cost requirements

	U32 bLineOfSight: 1;
		// The object meets line of sight requirements

	U32 bFacing : 1;
		// The object meets facing requirements 

	U32 bNotDisabled : 1;
		// The object meets disabled requirements (e.g. it's not disabled by kAttribType_Disable)

	U32 bActive : 1;
		// The object is currently active

	U32 bCurrent : 1;
		// The object is the entity's 'current' activity

	U32 bQueued : 1;
		// The object is queued to be activated

	U32 bMultiExec : 1;
		// The object is in the multiexec list

	U32 bCharging : 1;
		// The object is being charged

	U32 bMaintaining : 1;
		// The object is being maintained

	U32 bRecharging : 1;
		// The object is recharging

	U32 bRefillingCharges : 1;
		// The object refilling charges

	U32 bInCooldown : 1;
		// The object is in a cooldown

	U32 bModeEnabled : 1;
		// The object is enabled because of the current state of the characters modes

	U32 bAutoActivating : 1;	AST(ADDNAMES(AutoActivate))
		// AutoAttack is enabled, and this object will automatically activate

	U32 bAutoActivateEnabled : 1;
		// AutoAttack is not enabled, but if it were this object would automatically activate

	U32 bOwnedTrayElem : 1;
		// This UITrayElem actually owns its TrayElem and should free it

	U32 bHighlight : 1;
		//This element should be highlighted on the UI

	U32 bReferencesTrayElem : 1;
		//This tray element references an existing tray element with its Key

	U32 bUseBaseReplacePower : 1;
		//This tray element uses the base replace power instead

	U32 bInterruptOnRequest : 1;
		//Whether or not this tray element can be "turned off" on request

	U32 bLocked : 1;
		// Indicates whether this tray element is ready only

	U32 bStateSwitchedPower : 1;
		// if set, this power has been switched to another based on the combatPowerState

	U32 bDirty : 1;
		//Keeps track of frame updates
	
	U32 bWillActivateOnUse : 1;
		//If true, the object can be activated and will do something when it does
} UITrayElem;

// Create a temporary override for the entity's UITray data.
bool gclTraySetUIOverlay(int iUITray, int iTray);

// Remove a tray overlay at the specified UITray index.
bool gclTrayRemoveUIOverlay(int iUITray);

// Gets the tray index in the Entity's UITray array.  Returns iUITray if unset.
int entity_TrayGetUITrayIndex(Entity *e, int iUITray);

bool EntIsPowerTrayActivatable(SA_PARAM_NN_VALID Character *pchar, Power *ppow, Entity* pTarget, GameAccountDataExtract *pExtract);
//Gets the activated power for the tray,
// If bUseBaseReplacePower is true, it'll respect the power replace system
// If ppPowerTrayBase is non-null, it'll return the power before the statuses (held, disabled etc) override the returned power
SA_RET_OP_VALID Power *EntTrayGetActivatedPower(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID TrayElem *pelem, bool bUseBaseReplacePower, Power **ppPowerTrayBase);

SA_RET_NN_VALID PowerDef *PetEntGuessExecutedPower(SA_PARAM_NN_VALID Entity *pent, SA_PARAM_NN_VALID PowerDef *pdef);

// Utility function to gets the PetPowerState given the TrayElem.  May return NULL.
SA_RET_OP_VALID PetPowerState *UITrayElemGetPetPowerState(SA_PARAM_NN_VALID UITrayElem *pelem);

// Utility function to fill in UITrayElem data based on pet information.
void EntSetPetTrayElemPowerDataEx(SA_PARAM_NN_VALID UITrayElem *pelem, SA_PARAM_NN_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID PlayerPetInfo *pPetInfo, SA_PARAM_OP_VALID PetPowerState *pPetPowerState, SA_PARAM_OP_VALID PowerDef *pdef, SA_PARAM_OP_STR const char *cpchDefaultIcon);

// Utility function to set the UITrayElem's TrayElem, handles destroying the pre-existing TrayElem if
//  there is one, and cleaning up the UITrayElem's flags if the new TrayElem is NULL.
void tray_UITrayElemSetTrayElem(SA_PARAM_NN_VALID UITrayElem *pUIElem, SA_PARAM_OP_VALID TrayElem *pElem, bool bOwned);

bool UITrayExec(bool bActive, SA_PARAM_NN_VALID UITrayElem *pelem);
bool gclTrayExec(bool bActive, TrayElem *pelem);
void gclPowerSlotExec(int bActive, int iSlot);
SA_RET_OP_VALID Entity *UITrayGetOwner(SA_PARAM_OP_VALID UITrayElem *pelem);
void cmdTrayExecByTrayNotifyAudio(int bActive, int iTray, int iSlot, const char* pchCooldownSnd, const char* pchFailSnd);

LATELINK;
void GameSpecific_TrayUpdateHighlightTypeForElem(Entity* pEnt, PowerDef* pPowerDef, UITrayElem* pElem, Entity* pEntTarget, WorldInteractionNode* pNodeTarget);

#endif