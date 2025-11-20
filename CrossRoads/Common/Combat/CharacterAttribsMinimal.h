/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef CHARACTERATTRIBSMINIMAL_H__
#define CHARACTERATTRIBSMINIMAL_H__
GCC_SYSTEM

#include "textparser.h"

// If you change either of these, you have to ask Jered, and add the buckets to the structure below
#define DAMAGETYPECOUNT 30
#define DATADEFINED_ATTRIB_COUNT 192

AUTO_STRUCT AST_CONTAINER AST_FORCE_USE_ACTUAL_FIELD_NAME;
typedef struct CharacterAttribs
{
	/***************************************************************************
	* WARNING: If you change anything about this struct...
	*
	* YOU MUST CHANGE AttribType in this file
	* AND THEN AttribTypeEnum in CharacterAttribs.c
	* YOU MUST keep the #defines below it correct
	*
	***************************************************************************/

	// Data defined buckets for damage
	//  Do not add any without talking to Jered
	F32 fDamageType01;							AST(SELF_ONLY)
	F32 fDamageType02;							AST(SELF_ONLY)
	F32 fDamageType03;							AST(SELF_ONLY)
	F32 fDamageType04;							AST(SELF_ONLY)
	F32 fDamageType05;							AST(SELF_ONLY)
	F32 fDamageType06;							AST(SELF_ONLY)
	F32 fDamageType07;							AST(SELF_ONLY)
	F32 fDamageType08;							AST(SELF_ONLY)
	F32 fDamageType09;							AST(SELF_ONLY)
	F32 fDamageType10;							AST(SELF_ONLY)
	F32 fDamageType11;							AST(SELF_ONLY)
	F32 fDamageType12;							AST(SELF_ONLY)
	F32 fDamageType13;							AST(SELF_ONLY)
	F32 fDamageType14;							AST(SELF_ONLY)
	F32 fDamageType15;							AST(SELF_ONLY)
	F32 fDamageType16;							AST(SELF_ONLY)
	F32 fDamageType17;							AST(SELF_ONLY)
	F32 fDamageType18;							AST(SELF_ONLY)
	F32 fDamageType19;							AST(SELF_ONLY)
	F32 fDamageType20;							AST(SELF_ONLY)
	F32 fDamageType21;							AST(SELF_ONLY)
	F32 fDamageType22;							AST(SELF_ONLY)
	F32 fDamageType23;							AST(SELF_ONLY)
	F32 fDamageType24;							AST(SELF_ONLY)
	F32 fDamageType25;							AST(SELF_ONLY)
	F32 fDamageType26;							AST(SELF_ONLY)
	F32 fDamageType27;							AST(SELF_ONLY)
	F32 fDamageType28;							AST(SELF_ONLY)
	F32 fDamageType29;							AST(SELF_ONLY)
	F32 fDamageType30;							AST(SELF_ONLY)

	// Health .. assumptions made about the order of these
	F32 fHitPointsMax;							AST(PERSIST NO_TRANSACT SUBSCRIBE)
	F32 fHitPoints;								AST(PERSIST NO_TRANSACT SUBSCRIBE)

	// Power .. assumptions made about the order of these and their position relative to health
	F32 fPowerMax;								AST(PERSIST NO_TRANSACT SUBSCRIBE)
	F32 fPower;									AST(PERSIST NO_TRANSACT SUBSCRIBE)

	// Air .. assumptions are made about the order of these and their position relative to power
	F32 fAirMax;								AST(PERSIST NO_TRANSACT SUBSCRIBE)
	F32 fAir;									AST(PERSIST NO_TRANSACT SUBSCRIBE)

	// UPDATE the FIRST_NORMAL_ATTRIB #define if anything is put between Air and StatDamage

	// Stats!
	F32 fStatDamage;							AST(SELF_ONLY)
	F32 fStatHealth;							AST(SELF_ONLY)
	F32 fStatPower;								AST(SELF_ONLY)
	F32 fStatStrength;							AST(SELF_ONLY)
	F32 fStatAgility;							AST(SELF_ONLY)
	F32 fStatIntelligence;						AST(SELF_ONLY)
	F32 fStatEgo;								AST(SELF_ONLY)
	F32 fStatPresence;							AST(SELF_ONLY)
	F32 fStatRecovery;							AST(SELF_ONLY)

	// Regen
	F32 fRegeneration;							AST(SELF_ONLY)
	F32 fPowerDecay;							AST(SELF_ONLY)
	F32 fPowerRecovery;							AST(SELF_ONLY)
	F32 fPowerEquilibrium;						AST(SELF_ONLY)
	F32 fAirRecovery;							AST(SELF_ONLY)

	// Combat Ability
	F32 fDodge;									AST(SELF_ONLY)
	F32 fAvoidance;								AST(SELF_ONLY)
	F32 fCritChance;							AST(SELF_ONLY)
	F32 fCritSeverity;							AST(SELF_ONLY)

	// Movement
	F32 fFlight;								AST(SELF_ONLY)
	F32 fSwinging;								AST(SELF_ONLY)
	F32 fNoCollision;							AST(SELF_ONLY)
	F32 fSpeedRunning;							AST(SELF_ONLY)
	F32 fSpeedFlying;							AST(SELF_ONLY)
	F32	fFlightGlideDecent;						AST(SELF_ONLY)
	F32 fSpeedJumping;							AST(SELF_ONLY)
	F32 fHeightJumping;							AST(SELF_ONLY)
	F32 fFrictionRunning;						AST(SELF_ONLY)
	F32 fFrictionFlying;						AST(SELF_ONLY)
	F32 fTractionRunning;						AST(SELF_ONLY)
	F32 fTractionFlying;						AST(SELF_ONLY)
	F32 fTractionJumping;						AST(SELF_ONLY)
	F32 fGravity;								AST(SELF_ONLY)
	F32 fGravityJumpingUp;						AST(SELF_ONLY)
	F32 fGravityJumpingDown;					AST(SELF_ONLY)
	F32 fTurnRateFlying;						AST(SELF_ONLY)

	// Perception
	F32 fAggroStealth;							AST(ADDNAMES("fAggroStealth"))
	F32 fPerceptionStealth;						AST(ADDNAMES("fPerceptionStealth"))
	F32 fStealthSight;							AST(SELF_ONLY)
	F32 fAggro;									AST(SELF_ONLY)
	F32 fPerception;							AST(SELF_ONLY)
	F32 fMinimap;								AST(SELF_ONLY)

	// CC - Negative!
	F32 fRoot;									AST(SELF_ONLY)
	F32 fHold;									AST(SELF_ONLY)
	F32 fConfuse;								AST(SELF_ONLY)
	F32 fDisable;								AST(SELF_ONLY)

	// CC - Negative!
	F32 fKnockUp;								AST(SELF_ONLY)
	F32 fKnockBack;								AST(SELF_ONLY)
	F32 fRepel;									AST(SELF_ONLY)

	// Powers Effect Area
	F32 fRadius;								AST(SELF_ONLY)
	F32 fArc;									AST(SELF_ONLY)

	// Powers Attributes
	F32 fSpeedActivate;							AST(SELF_ONLY)
	F32 fSpeedRecharge;							AST(SELF_ONLY)
	F32 fSpeedCharge;							AST(SELF_ONLY)
	F32 fSpeedPeriod;							AST(SELF_ONLY)
	F32 fSpeedCooldown;							AST(SELF_ONLY)
	F32 fDiscountCost;							AST(SELF_ONLY)
	F32 fSubtargetAccuracy;						AST(SELF_ONLY)
	F32 fOnlyAffectSelf;						AST(SELF_ONLY)

	// AI
	F32 fAIThreatScale;							AST(SELF_ONLY, ADDNAMES(fAIDamageScalar, AIDamageScalar))

	// Data defined buckets with no explicit mechanical meaning
	//  Do not add any without talking to Jered
	F32 fDataDefined01;							AST(SELF_ONLY)
	F32 fDataDefined02;							AST(SELF_ONLY)
	F32 fDataDefined03;							AST(SELF_ONLY)
	F32 fDataDefined04;							AST(SELF_ONLY)
	F32 fDataDefined05;							AST(SELF_ONLY)
	F32 fDataDefined06;							AST(SELF_ONLY)
	F32 fDataDefined07;							AST(SELF_ONLY)
	F32 fDataDefined08;							AST(SELF_ONLY)
	F32 fDataDefined09;							AST(SELF_ONLY)
	F32 fDataDefined10;							AST(SELF_ONLY)
	F32 fDataDefined11;							AST(SELF_ONLY)
	F32 fDataDefined12;							AST(SELF_ONLY)
	F32 fDataDefined13;							AST(SELF_ONLY)
	F32 fDataDefined14;							AST(SELF_ONLY)
	F32 fDataDefined15;							AST(SELF_ONLY)
	F32 fDataDefined16;							AST(SELF_ONLY)
	F32 fDataDefined17;							AST(SELF_ONLY)
	F32 fDataDefined18;							AST(SELF_ONLY)
	F32 fDataDefined19;							AST(SELF_ONLY)
	F32 fDataDefined20;							AST(SELF_ONLY)
	F32 fDataDefined21;							AST(SELF_ONLY)
	F32 fDataDefined22;							AST(SELF_ONLY)
	F32 fDataDefined23;							AST(SELF_ONLY)
	F32 fDataDefined24;							AST(SELF_ONLY)
	F32 fDataDefined25;							AST(SELF_ONLY)
	F32 fDataDefined26;							AST(SELF_ONLY)
	F32 fDataDefined27;							AST(SELF_ONLY)
	F32 fDataDefined28;							AST(SELF_ONLY)
	F32 fDataDefined29;							AST(SELF_ONLY)
	F32 fDataDefined30;							AST(SELF_ONLY)
	F32 fDataDefined31;							AST(SELF_ONLY)
	F32 fDataDefined32;							AST(SELF_ONLY)
	F32 fDataDefined33;							AST(SELF_ONLY)
	F32 fDataDefined34;							AST(SELF_ONLY)
	F32 fDataDefined35;							AST(SELF_ONLY)
	F32 fDataDefined36;							AST(SELF_ONLY)
	F32 fDataDefined37;							AST(SELF_ONLY)
	F32 fDataDefined38;							AST(SELF_ONLY)
	F32 fDataDefined39;							AST(SELF_ONLY)
	F32 fDataDefined40;							AST(SELF_ONLY)
	F32 fDataDefined41;							AST(SELF_ONLY)
	F32 fDataDefined42;							AST(SELF_ONLY)
	F32 fDataDefined43;							AST(SELF_ONLY)
	F32 fDataDefined44;							AST(SELF_ONLY)
	F32 fDataDefined45;							AST(SELF_ONLY)
	F32 fDataDefined46;							AST(SELF_ONLY)
	F32 fDataDefined47;							AST(SELF_ONLY)
	F32 fDataDefined48;							AST(SELF_ONLY)
	F32 fDataDefined49;							AST(SELF_ONLY)
	F32 fDataDefined50;							AST(SELF_ONLY)
	F32 fDataDefined51;							AST(SELF_ONLY)
	F32 fDataDefined52;							AST(SELF_ONLY)
	F32 fDataDefined53;							AST(SELF_ONLY)
	F32 fDataDefined54;							AST(SELF_ONLY)
	F32 fDataDefined55;							AST(SELF_ONLY)
	F32 fDataDefined56;							AST(SELF_ONLY)
	F32 fDataDefined57;							AST(SELF_ONLY)
	F32 fDataDefined58;							AST(SELF_ONLY)
	F32 fDataDefined59;							AST(SELF_ONLY)
	F32 fDataDefined60;							AST(SELF_ONLY)
	F32 fDataDefined61;							AST(SELF_ONLY)
	F32 fDataDefined62;							AST(SELF_ONLY)
	F32 fDataDefined63;							AST(SELF_ONLY)
	F32 fDataDefined64;							AST(SELF_ONLY)
	F32 fDataDefined65;							AST(SELF_ONLY)
	F32 fDataDefined66;							AST(SELF_ONLY)
	F32 fDataDefined67;							AST(SELF_ONLY)
	F32 fDataDefined68;							AST(SELF_ONLY)
	F32 fDataDefined69;							AST(SELF_ONLY)
	F32 fDataDefined70;							AST(SELF_ONLY)
	F32 fDataDefined71;							AST(SELF_ONLY)
	F32 fDataDefined72;							AST(SELF_ONLY)
	F32 fDataDefined73;							AST(SELF_ONLY)
	F32 fDataDefined74;							AST(SELF_ONLY)
	F32 fDataDefined75;							AST(SELF_ONLY)
	F32 fDataDefined76;							AST(SELF_ONLY)
	F32 fDataDefined77;							AST(SELF_ONLY)
	F32 fDataDefined78;							AST(SELF_ONLY)
	F32 fDataDefined79;							AST(SELF_ONLY)
	F32 fDataDefined80;							AST(SELF_ONLY)
	F32 fDataDefined81;							AST(SELF_ONLY)
	F32 fDataDefined82;							AST(SELF_ONLY)
	F32 fDataDefined83;							AST(SELF_ONLY)
	F32 fDataDefined84;							AST(SELF_ONLY)
	F32 fDataDefined85;							AST(SELF_ONLY)
	F32 fDataDefined86;							AST(SELF_ONLY)
	F32 fDataDefined87;							AST(SELF_ONLY)
	F32 fDataDefined88;							AST(SELF_ONLY)
	F32 fDataDefined89;							AST(SELF_ONLY)
	F32 fDataDefined90;							AST(SELF_ONLY)
	F32 fDataDefined91;							AST(SELF_ONLY)
	F32 fDataDefined92;							AST(SELF_ONLY)
	F32 fDataDefined93;							AST(SELF_ONLY)
	F32 fDataDefined94;							AST(SELF_ONLY)
	F32 fDataDefined95;							AST(SELF_ONLY)
	F32 fDataDefined96;							AST(SELF_ONLY)
	F32 fDataDefined97;							AST(SELF_ONLY)
	F32 fDataDefined98;							AST(SELF_ONLY)
	F32 fDataDefined99;							AST(SELF_ONLY)
	F32 fDataDefined100;						AST(SELF_ONLY)
	F32 fDataDefined101;						AST(SELF_ONLY)
	F32 fDataDefined102;						AST(SELF_ONLY)
	F32 fDataDefined103;						AST(SELF_ONLY)
	F32 fDataDefined104;						AST(SELF_ONLY)
	F32 fDataDefined105;						AST(SELF_ONLY)
	F32 fDataDefined106;						AST(SELF_ONLY)
	F32 fDataDefined107;						AST(SELF_ONLY)
	F32 fDataDefined108;						AST(SELF_ONLY)
	F32 fDataDefined109;						AST(SELF_ONLY)
	F32 fDataDefined110;						AST(SELF_ONLY)
	F32 fDataDefined111;						AST(SELF_ONLY)
	F32 fDataDefined112;						AST(SELF_ONLY)
	F32 fDataDefined113;						AST(SELF_ONLY)
	F32 fDataDefined114;						AST(SELF_ONLY)
	F32 fDataDefined115;						AST(SELF_ONLY)
	F32 fDataDefined116;						AST(SELF_ONLY)
	F32 fDataDefined117;						AST(SELF_ONLY)
	F32 fDataDefined118;						AST(SELF_ONLY)
	F32 fDataDefined119;						AST(SELF_ONLY)
	F32 fDataDefined120;						AST(SELF_ONLY)
	F32 fDataDefined121;						AST(SELF_ONLY)
	F32 fDataDefined122;						AST(SELF_ONLY)
	F32 fDataDefined123;						AST(SELF_ONLY)
	F32 fDataDefined124;						AST(SELF_ONLY)
	F32 fDataDefined125;						AST(SELF_ONLY)
	F32 fDataDefined126;						AST(SELF_ONLY)
	F32 fDataDefined127;						AST(SELF_ONLY)
	F32 fDataDefined128;						AST(SELF_ONLY)
	F32 fDataDefined129;						AST(SELF_ONLY)
	F32 fDataDefined130;						AST(SELF_ONLY)
	F32 fDataDefined131;						AST(SELF_ONLY)
	F32 fDataDefined132;						AST(SELF_ONLY)
	F32 fDataDefined133;						AST(SELF_ONLY)
	F32 fDataDefined134;						AST(SELF_ONLY)
	F32 fDataDefined135;						AST(SELF_ONLY)
	F32 fDataDefined136;						AST(SELF_ONLY)
	F32 fDataDefined137;						AST(SELF_ONLY)
	F32 fDataDefined138;						AST(SELF_ONLY)
	F32 fDataDefined139;						AST(SELF_ONLY)
	F32 fDataDefined140;						AST(SELF_ONLY)
	F32 fDataDefined141;						AST(SELF_ONLY)
	F32 fDataDefined142;						AST(SELF_ONLY)
	F32 fDataDefined143;						AST(SELF_ONLY)
	F32 fDataDefined144;						AST(SELF_ONLY)
	F32 fDataDefined145;						AST(SELF_ONLY)
	F32 fDataDefined146;						AST(SELF_ONLY)
	F32 fDataDefined147;						AST(SELF_ONLY)
	F32 fDataDefined148;						AST(SELF_ONLY)
	F32 fDataDefined149;						AST(SELF_ONLY)
	F32 fDataDefined150;						AST(SELF_ONLY)
	F32 fDataDefined151;						AST(SELF_ONLY)
	F32 fDataDefined152;						AST(SELF_ONLY)
	F32 fDataDefined153;						AST(SELF_ONLY)
	F32 fDataDefined154;						AST(SELF_ONLY)
	F32 fDataDefined155;						AST(SELF_ONLY)
	F32 fDataDefined156;						AST(SELF_ONLY)
	F32 fDataDefined157;						AST(SELF_ONLY)
	F32 fDataDefined158;						AST(SELF_ONLY)
	F32 fDataDefined159;						AST(SELF_ONLY)
	F32 fDataDefined160;						AST(SELF_ONLY)
	F32 fDataDefined161;						AST(SELF_ONLY)
	F32 fDataDefined162;						AST(SELF_ONLY)
	F32 fDataDefined163;						AST(SELF_ONLY)
	F32 fDataDefined164;						AST(SELF_ONLY)
	F32 fDataDefined165;						AST(SELF_ONLY)
	F32 fDataDefined166;						AST(SELF_ONLY)
	F32 fDataDefined167;						AST(SELF_ONLY)
	F32 fDataDefined168;						AST(SELF_ONLY)
	F32 fDataDefined169;						AST(SELF_ONLY)
	F32 fDataDefined170;						AST(SELF_ONLY)
	F32 fDataDefined171;						AST(SELF_ONLY)
	F32 fDataDefined172;						AST(SELF_ONLY)
	F32 fDataDefined173;						AST(SELF_ONLY)
	F32 fDataDefined174;						AST(SELF_ONLY)
	F32 fDataDefined175;						AST(SELF_ONLY)
	F32 fDataDefined176;						AST(SELF_ONLY)
    F32 fDataDefined177;						AST(SELF_ONLY)
    F32 fDataDefined178;						AST(SELF_ONLY)
    F32 fDataDefined179;						AST(SELF_ONLY)
    F32 fDataDefined180;						AST(SELF_ONLY)
    F32 fDataDefined181;						AST(SELF_ONLY)
    F32 fDataDefined182;						AST(SELF_ONLY)
    F32 fDataDefined183;						AST(SELF_ONLY)
    F32 fDataDefined184;						AST(SELF_ONLY)
    F32 fDataDefined185;						AST(SELF_ONLY)
    F32 fDataDefined186;						AST(SELF_ONLY)
    F32 fDataDefined187;						AST(SELF_ONLY)
    F32 fDataDefined188;						AST(SELF_ONLY)
    F32 fDataDefined189;						AST(SELF_ONLY)
    F32 fDataDefined190;						AST(SELF_ONLY)
    F32 fDataDefined191;						AST(SELF_ONLY)
    F32 fDataDefined192;						AST(SELF_ONLY)
	// The dirty bit always comes last
	DirtyBit		 dirtyBit;					AST(NO_NETSEND)

} CharacterAttribs;

AUTO_STRUCT;
typedef struct CooldownRateModifier
{
	S32 iPowerCategory;			AST(KEY)
		// Relates to the power category enum

	F32 fBasicAbs;				AST(NO_NETSEND)
	F32 fBasicPos;				AST(NO_NETSEND)
	F32 fBasicNeg;				AST(NO_NETSEND)

	F32 fValue;					AST(DEFAULT(1))
		// The amount to scale the rate by

	U32 bDirty : 1;				AST(NO_NETSEND)
		// Special dirty bit used for cleanup
} CooldownRateModifier;

AUTO_STRUCT;
typedef struct AttribAccrualSetBasic
{
	// Changes to your basic attrib
	CharacterAttribs attrBasicAbs;
	CharacterAttribs attrBasicFactPos;
	CharacterAttribs attrBasicFactNeg;
	
	// Changes to your strength
	CharacterAttribs attrStrBase;
	CharacterAttribs attrStrFactPos;
	CharacterAttribs attrStrFactNeg;
	CharacterAttribs attrStrFactBonus;
	CharacterAttribs attrStrMult;
	CharacterAttribs attrStrAdd;

	// Changes to your resistance
	CharacterAttribs attrResTrue;
	CharacterAttribs attrResBase;
	CharacterAttribs attrResFactPos;
	CharacterAttribs attrResFactNeg;
	CharacterAttribs attrResFactBonus;
	CharacterAttribs attrImmunity;
} AttribAccrualSetBasic;

// The set of possible ways to modify an attrib.
// Some code (character_mods.c) makes some assumptions
// about the layout of this struct, so please don't
// touch it unless you know what you're doing.
AUTO_STRUCT;
typedef struct AttribAccrualSet
{
	AttribAccrualSetBasic CharacterAttribs;

	// Speed cooldown information
	CooldownRateModifier** ppSpeedCooldown;

	DirtyBit		 dirtyBit;			AST(NO_NETSEND)
} AttribAccrualSet;
extern ParseTable parse_AttribAccrualSet[];
#define TYPE_parse_AttribAccrualSet AttribAccrualSet

// WARNING: If for whatever reason we add a new value to this enumeration
// we must update the InitCharacterAttribs function so the UI is 
// aware of the enum value
typedef enum AttribAspect
{
	kAttribAspect_BasicAbs = offsetof(AttribAccrualSetBasic,attrBasicAbs),
	kAttribAspect_BasicFactPos = offsetof(AttribAccrualSetBasic,attrBasicFactPos),
	kAttribAspect_BasicFactNeg = offsetof(AttribAccrualSetBasic,attrBasicFactNeg),

	kAttribAspect_StrBase = offsetof(AttribAccrualSetBasic,attrStrBase),
	kAttribAspect_StrFactPos = offsetof(AttribAccrualSetBasic,attrStrFactPos),
	kAttribAspect_StrFactNeg = offsetof(AttribAccrualSetBasic,attrStrFactNeg),
	kAttribAspect_StrFactBonus = offsetof(AttribAccrualSetBasic,attrStrFactBonus),
	kAttribAspect_StrMult = offsetof(AttribAccrualSetBasic,attrStrMult),
	kAttribAspect_StrAdd = offsetof(AttribAccrualSetBasic,attrStrAdd),

	kAttribAspect_ResTrue = offsetof(AttribAccrualSetBasic,attrResTrue),
	kAttribAspect_ResBase = offsetof(AttribAccrualSetBasic,attrResBase),
	kAttribAspect_ResFactPos = offsetof(AttribAccrualSetBasic,attrResFactPos),
	kAttribAspect_ResFactNeg = offsetof(AttribAccrualSetBasic,attrResFactNeg),
	kAttribAspect_ResFactBonus = offsetof(AttribAccrualSetBasic,attrResFactBonus),
	kAttribAspect_Immunity = offsetof(AttribAccrualSetBasic,attrImmunity),
} AttribAspect;

// THIS WILL BREAK IF THE DEFINITIONS OF AttribAccrualSet and AttribAspect change
#define ATTRIBASPECT_INDEX(x) ((x)/sizeof(CharacterAttribs))

// a utility structure used in some strength compilation functions
typedef struct StrengthAspectSet
{
	F32 fStrBase;
	F32 fStrFactPos;
	F32 fStrFactNeg;
	F32 fStrFactBonus;
	F32 fStrMult;
	F32 fStrAdd;
} StrengthAspectSet;

// a utility structure used in some resist compilation functions
typedef struct ResistAspectSet
{
	F32 fResTrue;
	F32 fResBase;
	F32 fResFactPos;
	F32 fResFactNeg;
	F32 fResFactBonus;
} ResistAspectSet;

/***************************************************************************
* WARNING: If you change anything about this struct...
*
* YOU MUST CHANGE AttribType in this file
* AND THEN AttribTypeEnum in CharacterAttribs.c
*
***************************************************************************/
/***** COMMENTED - make changes here, then explicitly include them in
                   AttribType enum below

typedef enum SpecialAttribType
{
	// Special Attribs (modify the commented part of CharacterAttribsMinimal.h, then update real structures)
	kAttribType_Null = sizeof(CharacterAttribs),
	kAttribType_AIAvoid,
	kAttribType_AICommand,
	kAttribType_AISoftAvoid,
	kAttribType_AIThreat,
	kAttribType_All,
	kAttribType_ApplyObjectDeath,
	kAttribType_ApplyPower,
	kAttribType_AttribModDamage,
	kAttribType_AttribModExpire,
	kAttribType_AttribModFragilityHealth,
	kAttribType_AttribModFragilityScale,
	kAttribType_AttribModHeal,
	kAttribType_AttribModShare,
	kAttribType_AttribModShieldPercentIgnored,
	kAttribType_AttribOverride,
	kAttribType_BecomeCritter,
	kAttribType_BePickedUp,
	kAttribType_CombatAdvantage,
	kAttribType_ConstantForce,
	kAttribType_CurveDodgeAndAvoidance,
	kAttribType_CurveTriggeredPercentHeals,
	kAttribType_DamageTrigger,
	kAttribType_DropHeldObject,
	kAttribType_EntAttach,
	kAttribType_EntCreate,
	kAttribType_EntCreateVanity,
	kAttribType_Faction,
	kAttribType_Flag,
	kAttribType_GrantPower,
	kAttribType_GrantReward,
	kAttribType_IncludeEnhancement,
	kAttribType_Interrupt,
	kAttribType_ItemDurability,
	kAttribType_Kill,
	kAttribType_KillTrigger,
	kAttribType_KnockTo,
	kAttribType_MissionEvent,
	kAttribType_ModifyCostume,
	kAttribType_Shield,
	kAttribType_Placate,
	kAttribType_PowerMode,
	kAttribType_PowerRecharge,
	kAttribType_PowerShield,
	kAttribType_ProjectileCreate,
	kAttribType_PVPFlag,
	kAttribType_RemovePower,
	kAttribType_RewardModifier,
	kAttribType_Ride,
	kAttribType_SetCostume,
	kAttribType_SpeedCooldownCategory,
	kAttribType_SubtargetSet,
	kAttribType_Taunt,
	kAttribType_Teleport,
	kAttribType_TeleThrow,
	kAttribType_TriggerComplex,
	kAttribType_TriggerSimple,
	kAttribType_WarpSet,
	kAttribType_WarpTo,
	kAttribType_AIAggroTotalScale,

	kAttribType_LAST = kAttribType_AIAggroTotalScale,
} SpecialAttribType;

END COMMENTED *****/




// To create this enum from scratch, copy the CharacterAttribs struct, 
//  remove the damage types and data defined arrays,
//  and then Find and Replace using regular expressions
//   Find: F32 f{.*};.*
//   Replace with: kAttribType_\1 = offsetof(CharacterAttribs,f\1),
//  and then copy in the SpecialAttribType enum list
typedef enum AttribType
{
	// Health .. assumptions made about the order of these
	kAttribType_HitPointsMax = offsetof(CharacterAttribs,fHitPointsMax),
	kAttribType_HitPoints = offsetof(CharacterAttribs,fHitPoints),

	// Power .. assumptions made about the order of these and their position relative to health
	kAttribType_PowerMax = offsetof(CharacterAttribs,fPowerMax),
	kAttribType_Power = offsetof(CharacterAttribs,fPower),

	// Air .. assumptions are made about the order of these and their position relative to power
	kAttribType_AirMax = offsetof(CharacterAttribs,fAirMax),
	kAttribType_Air = offsetof(CharacterAttribs,fAir),

	// UPDATE the FIRST_NORMAL_ATTRIB #define if anything is put between Air and StatDamage

	// Stats!
	kAttribType_StatDamage = offsetof(CharacterAttribs,fStatDamage),
	kAttribType_StatHealth = offsetof(CharacterAttribs,fStatHealth),
	kAttribType_StatPower = offsetof(CharacterAttribs,fStatPower),
	kAttribType_StatStrength = offsetof(CharacterAttribs,fStatStrength),
	kAttribType_StatAgility = offsetof(CharacterAttribs,fStatAgility),
	kAttribType_StatIntelligence = offsetof(CharacterAttribs,fStatIntelligence),
	kAttribType_StatEgo = offsetof(CharacterAttribs,fStatEgo),
	kAttribType_StatPresence = offsetof(CharacterAttribs,fStatPresence),
	kAttribType_StatRecovery = offsetof(CharacterAttribs,fStatRecovery),

	// Regen
	kAttribType_Regeneration = offsetof(CharacterAttribs,fRegeneration),
	kAttribType_PowerDecay = offsetof(CharacterAttribs,fPowerDecay),
	kAttribType_PowerRecovery = offsetof(CharacterAttribs,fPowerRecovery),
	kAttribType_PowerEquilibrium = offsetof(CharacterAttribs,fPowerEquilibrium),
	kAttribType_AirRecovery = offsetof(CharacterAttribs,fAirRecovery),

	// Combat Ability
	kAttribType_Dodge = offsetof(CharacterAttribs,fDodge),
	kAttribType_Avoidance = offsetof(CharacterAttribs,fAvoidance),
	kAttribType_CritChance = offsetof(CharacterAttribs,fCritChance),
	kAttribType_CritSeverity = offsetof(CharacterAttribs,fCritSeverity),

	// Movement
	kAttribType_Flight = offsetof(CharacterAttribs,fFlight),
	kAttribType_Swinging = offsetof(CharacterAttribs,fSwinging),
	kAttribType_NoCollision = offsetof(CharacterAttribs,fNoCollision),
	kAttribType_SpeedRunning = offsetof(CharacterAttribs,fSpeedRunning),
	kAttribType_SpeedFlying = offsetof(CharacterAttribs,fSpeedFlying),
	kAttribType_FlightGlideDecent = offsetof(CharacterAttribs,fFlightGlideDecent),
	kAttribType_SpeedJumping = offsetof(CharacterAttribs,fSpeedJumping),
	kAttribType_HeightJumping = offsetof(CharacterAttribs,fHeightJumping),
	kAttribType_FrictionRunning = offsetof(CharacterAttribs,fFrictionRunning),
	kAttribType_FrictionFlying = offsetof(CharacterAttribs,fFrictionFlying),
	kAttribType_TractionRunning = offsetof(CharacterAttribs,fTractionRunning),
	kAttribType_TractionFlying = offsetof(CharacterAttribs,fTractionFlying),
	kAttribType_TractionJumping = offsetof(CharacterAttribs,fTractionJumping),
	kAttribType_Gravity = offsetof(CharacterAttribs,fGravity),
	kAttribType_GravityJumpingUp = offsetof(CharacterAttribs,fGravityJumpingUp),
	kAttribType_GravityJumpingDown = offsetof(CharacterAttribs,fGravityJumpingDown),
	kAttribType_TurnRateFlying = offsetof(CharacterAttribs,fTurnRateFlying),

	// Perception
	kAttribType_AggroStealth = offsetof(CharacterAttribs,fAggroStealth),
	kAttribType_PerceptionStealth = offsetof(CharacterAttribs,fPerceptionStealth),
	kAttribType_StealthSight = offsetof(CharacterAttribs,fStealthSight),
	kAttribType_Aggro = offsetof(CharacterAttribs,fAggro),
	kAttribType_Perception = offsetof(CharacterAttribs,fPerception),
	kAttribType_Minimap = offsetof(CharacterAttribs,fMinimap),

	// CC - Negative!
	kAttribType_Root = offsetof(CharacterAttribs,fRoot),
	kAttribType_Hold = offsetof(CharacterAttribs,fHold),
	kAttribType_Confuse = offsetof(CharacterAttribs,fConfuse),
	kAttribType_Disable = offsetof(CharacterAttribs,fDisable),

	// CC - Negative!
	kAttribType_KnockUp = offsetof(CharacterAttribs,fKnockUp),
	kAttribType_KnockBack = offsetof(CharacterAttribs,fKnockBack),
	kAttribType_Repel = offsetof(CharacterAttribs,fRepel),

	// Powers Effect Area
	kAttribType_Radius = offsetof(CharacterAttribs,fRadius),
	kAttribType_Arc = offsetof(CharacterAttribs,fArc),

	// Powers Attributes
	kAttribType_SpeedActivate = offsetof(CharacterAttribs,fSpeedActivate),
	kAttribType_SpeedRecharge = offsetof(CharacterAttribs,fSpeedRecharge),
	kAttribType_SpeedCharge = offsetof(CharacterAttribs,fSpeedCharge),
	kAttribType_SpeedPeriod = offsetof(CharacterAttribs,fSpeedPeriod),
	kAttribType_SpeedCooldown = offsetof(CharacterAttribs,fSpeedCooldown),
	kAttribType_DiscountCost = offsetof(CharacterAttribs,fDiscountCost),
	kAttribType_SubtargetAccuracy = offsetof(CharacterAttribs,fSubtargetAccuracy),
	kAttribType_OnlyAffectSelf = offsetof(CharacterAttribs,fOnlyAffectSelf),

	// AI
	kAttribType_AIThreatScale = offsetof(CharacterAttribs,fAIThreatScale),

	kAttribType_FirstUserDefined = offsetof(CharacterAttribs,fDataDefined01),

	// Special Attribs (modify the commented part of CharacterAttribsMinimal.h, then update real structures)
	kAttribType_Null = sizeof(CharacterAttribs),
	kAttribType_AIAvoid,
	kAttribType_AICommand,
	kAttribType_AISoftAvoid,
	kAttribType_AIThreat,
	kAttribType_All,
	kAttribType_ApplyObjectDeath,
	kAttribType_ApplyPower,
	kAttribType_AttribLink,
	kAttribType_AttribModDamage,
	kAttribType_AttribModExpire,
	kAttribType_AttribModFragilityHealth,
	kAttribType_AttribModFragilityScale,
	kAttribType_AttribModHeal,
	kAttribType_AttribModShare,
	kAttribType_AttribModShieldPercentIgnored,
	kAttribType_AttribOverride,
	kAttribType_BecomeCritter,
	kAttribType_BePickedUp,
	kAttribType_CombatAdvantage,
	kAttribType_ConstantForce,
	kAttribType_CurveDodgeAndAvoidance,
	kAttribType_CurveTriggeredPercentHeals,
	kAttribType_DamageTrigger,
	kAttribType_DisableTacticalMovement,
	kAttribType_DropHeldObject,
	kAttribType_EntAttach,
	kAttribType_EntCreate,
	kAttribType_EntCreateVanity,
	kAttribType_Faction,
	kAttribType_Flag,
	kAttribType_GrantPower,
	kAttribType_GrantReward,
	kAttribType_IncludeEnhancement,
	kAttribType_Interrupt,
	kAttribType_ItemDurability,
	kAttribType_Kill,
	kAttribType_KillTrigger,
	kAttribType_KnockTo,
	kAttribType_MissionEvent,
	kAttribType_ModifyCostume,
	kAttribType_Notify,
	kAttribType_Placate,
	kAttribType_PowerMode,
	kAttribType_PowerRecharge,
	kAttribType_PowerShield,
	kAttribType_ProjectileCreate,
	kAttribType_PVPFlag,
	kAttribType_PVPSpecialAction,
	kAttribType_RemovePower,
	kAttribType_RewardModifier,
	kAttribType_Ride,
	kAttribType_SetCostume,
	kAttribType_Shield,
	kAttribType_SpeedCooldownCategory,
	kAttribType_SubtargetSet,
	kAttribType_Taunt,
	kAttribType_Teleport,
	kAttribType_TeleThrow,
	kAttribType_TriggerComplex,
	kAttribType_TriggerSimple,
	kAttribType_WarpSet,
	kAttribType_WarpTo,
	kAttribType_AIAggroTotalScale,
	kAttribType_DynamicAttrib,

	kAttribType_LAST = kAttribType_DynamicAttrib,
} AttribType;

#endif