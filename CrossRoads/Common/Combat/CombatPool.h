#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


// CombatPool defines generic enums and functionality for the combat system's
//  concept of a pool of "stuff", which is typically an attribute, but may be
//  something custom that just wants to work similarly.

// CombatPoolPoint defines the different data points in a CombatPool
AUTO_ENUM;
typedef enum CombatPoolPoint
{

	kCombatPoolPoint_Unset = 0,	EIGNORE
		// Not set, which may generate an error if it is needed

	kCombatPoolPoint_Min,
		// The pool's minimum value (zero if the pool has no minimum)

	kCombatPoolPoint_Max,
		// The pool's maximum value (FLT_MAX if the pool has no maximum)

	kCombatPoolPoint_Center,
		// The pool's center point (halfway between min and max)

	kCombatPoolPoint_Target,
		// The pool's target value

} CombatPoolPoint;

// CombatPoolBound defines the different ways a CombatPool's current value changes in 
//  response to changes in its minimum and maximum values
AUTO_ENUM;
typedef enum CombatPoolBound
{

	kCombatPoolBound_Unset = 0,	EIGNORE
		// The bounding is not set, which will generate an error

	kCombatPoolBound_None,
		// The current value does not change

	kCombatPoolBound_Clamp,
		// The current value is clamped to the min and max

	kCombatPoolBound_Proportional,
		// The current value is set to the proportion of the range it had before the change

} CombatPoolBound;

// CombatPoolUnit defines how generic units are converted into actual numbers
AUTO_ENUM;
typedef enum CombatPoolUnit
{

	kCombatPoolUnit_Unset = 0,	EIGNORE
		// The unit is not set, which will generate an error if it is needed

	kCombatPoolUnit_Absolute,
		// The unit is represented as an absolute value

	kCombatPoolUnit_Percent,
		// The unit is a percent of the absolute range (which thus requires a max)

} CombatPoolUnit;



// CombatPoolTarget defines the rules regarding the target of a CombatPool, 
//  including how it regen and decay.
AUTO_STRUCT;
typedef struct CombatPoolTarget
{

	CombatPoolPoint ePoint;
		// The target point may be the pool's min or max, or it may be
		//  the pool's actual target value

	CombatPoolUnit eUnitTarget;
		// If the pool has a target value, this is the units it is specified in


	F32 fMagRegen;
		// If the pool regens, this is the base amount per tick

	CombatPoolUnit eUnitRegen;
		// If the pool regens, this is the units of the regen magnitude

	F32 fMagDecay;
		// If the pool decays, this is the base amount per tick

	CombatPoolUnit eUnitDecay;
		// If the pool decays, this is the units of the decay magnitude

	F32 fTimeTick;
		// Seconds. If the pool has a target, this is the base frequency at which
		//  the current value is moved towards the target.

} CombatPoolTarget;

// CombatPoolDef defines the basic rules of operation for a CombatPool, including initialization,
//  bounding and the optional target.
AUTO_STRUCT;
typedef struct CombatPoolDef
{

	CombatPoolPoint eInit;
		// The point at which the the pool is initialized

	CombatPoolBound eBound;
		// How the pool changes the current value when the min or max value change

	CombatPoolTarget *pTarget;
		// Optional description of the pool's target
	
} CombatPoolDef;



// Validates a CombatPool.  Flags indicates if the CombatPool has actual
//  max and target values.
int combatpool_Validate(SA_PARAM_NN_VALID CombatPoolDef *pdef,
						int bHasMaxValue,
						int bHasTargetValue);

// Returns the initial current value of a CombatPool
F32 combatpool_Init(SA_PARAM_NN_VALID CombatPoolDef *pdef,
					F32 fMin,
					F32 fMax,
					F32 fTar);

// Returns the new current value of a CombatPool based on its old cur as well as min and max
F32 combatpool_Bound(SA_PARAM_NN_VALID CombatPoolDef *pdef,
					 F32 fCur,
					 F32 fMinOld,
					 F32 fMinNew,
					 F32 fMaxOld,
					 F32 fMaxNew);

// Evaluates the CombatPool's attempt to approach its target value through regen/decay.  Returns
//  the new current value.  Clamping is optional.
S32 combatpool_Tick(SA_PARAM_NN_VALID CombatPoolDef *pdef,
					SA_PARAM_NN_VALID F32 *pfTimer,
					SA_PARAM_NN_VALID F32 *pfCurInOut,
					F32 fRate,
					F32 fMin,
					F32 fMax,
					F32 fTar,
					F32 fScaleRegenFreq,
					F32 fScaleRegenMag,
					F32 fScaleDecayFreq,
					F32 fScaleDecayMag);

