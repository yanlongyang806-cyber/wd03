#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// Forward declarations
typedef struct AttribCurve			AttribCurve;
typedef struct AttribModDef			AttribModDef;
typedef struct Character		Character;
typedef struct Entity			Entity;
typedef struct PowerActivation		PowerActivation;
typedef struct Power				Power;
typedef struct WorldInteractionNode WorldInteractionNode;

typedef enum ActivationFailureReason ActivationFailureReason;


// General callbacks

// Handle a PowersMovement Event
typedef void (*CombatCBHandlePMEvent)(Entity *pent, U32 uiEventID, U32 uiUserID);
extern CombatCBHandlePMEvent combatcbHandlePMEvent;

// Character to character perception
typedef bool (*CombatCBCharacterCanPerceive)(Character *pchar, Character *pcharTarget);
extern CombatCBCharacterCanPerceive combatcbCharacterCanPerceive;

// Returns the amount of time needed to predict a particular AttribModDef
typedef F32 (*CombatCBPredictAttribModDef)(AttribModDef *pdef);
extern CombatCBPredictAttribModDef combatcbPredictAttribModDef;
#define ccbPredictAttribModDef(...) combatcbPredictAttribModDef ? combatcbPredictAttribModDef(__VA_ARGS__) : 0.0f;

// Called when a Character's general Powers list changes
typedef void (*CombatCBCharacterPowersChanged)(Character *pchar);
extern CombatCBCharacterPowersChanged combatcbCharacterPowersChanged;

// Parameters for the activation failure. None of these parameters are guaranteed to exist in the handler.
typedef struct ActivationFailureParams
{
	Entity *pEnt;
	Power *pPow;
	WorldInteractionNode *pNode;
	Vec3 vector;
	const char *pchStringParam;
} ActivationFailureParams;

// Handles the feedbacks for power activation failures
void character_ActivationFailureFeedback(ActivationFailureReason eReason, SA_PARAM_NN_VALID ActivationFailureParams *pParams);
