#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppLocale.h"
#include "CharacterAttribsMinimal.h"
#include "PowersEnums.h"

// Code to automatically describe various Powers data (include PowerDefs, AttribMods, etc)

// Forward declarations
typedef struct AttribModDef		AttribModDef;
typedef struct AttribModNet		AttribModNet;
typedef struct Character		Character;
typedef struct CharacterClass	CharacterClass;
typedef struct CombatTrackerNet	CombatTrackerNet;
typedef struct Entity			Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct Message			Message;
typedef struct Power			Power;
typedef struct PowerDef			PowerDef;
typedef struct PowerStat		PowerStat;
typedef struct PTNodeDef		PTNodeDef;
typedef struct Item				Item;

// Redefined for the ApplyPower call
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere

AUTO_STRUCT;
typedef struct AutoDescConfig
{
	S32 *piPercentAttribs;	AST(NAME(PercentAttribs) SUBTABLE(AttribTypeEnum))
		// General Attributes that should be considered percentages for display purposes

	S32 *piScaledAttribs;	AST(NAME(ScaledAttribs) SUBTABLE(AttribTypeEnum))
		// General Attributes that should be scaled by 100 for display purposes

	U32 bVariance : 1;
		// Show the variance in X-Y format in Normal and Maximum detail

} AutoDescConfig;

typedef enum AutoDescPowerHeader
{
	kAutoDescPowerHeader_None = 0,
	kAutoDescPowerHeader_ApplySelf,
	kAutoDescPowerHeader_Apply,
	kAutoDescPowerHeader_Full,
} AutoDescPowerHeader;

// Simple function to find the entity's desired Power AutoDesc detail.  Returns Normal if it can't find a preference.
int entGetPowerAutoDescDetail(SA_PARAM_OP_VALID Entity *pent, S32 bTooltip);


typedef struct AutoDescPower AutoDescPower;
typedef struct AutoDescAttribMod AutoDescAttribMod;

// Container for formatted strings from the automatic description of a Power or PowerDef
// as well as raw data for certain fields so they can be custom formatted
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL WIKI("AutoDescPower");
typedef struct AutoDescPower
{
	const char *pchKey;				AST(KEY UNOWNED)
		// Key, the internal name of the Power

	const char *pchName;			AST(UNOWNED WIKI(AUTO))
		// Display name

	const char *pchDesc;			AST(UNOWNED WIKI(AUTO))
		// Description
	
	const char *pchDescLong;		AST(UNOWNED WIKI(AUTO))
		// Long description
	
	const char *pchDescFlavor;		AST(UNOWNED WIKI(AUTO))
		// Flavor description

	char *pchCustom;				AST(ESTRING WIKI(AUTO))
		// Custom (AutoDescOverride) description

	char *pchDev;					AST(ESTRING WIKI(AUTO))
		// Dev mode data (internal power name, error/warning messages)

	char *pchType;					AST(ESTRING WIKI(AUTO))
		// Type (click, toggle, etc)

	
	char *pchComboRequires;			AST(ESTRING WIKI(AUTO))
		// Requires expression from the parent Combo Power for this child Power

	char *pchComboCharge;			AST(ESTRING WIKI(AUTO))
		// PercentChargeRequired from the parent Combo Power for this child Power


	char *pchEnhanceAttach;			AST(ESTRING WIKI(AUTO))
		// EnhanceAttach expression

	char *pchEnhanceAttached;		AST(ESTRING WIKI(AUTO))
		// List of Powers being enhanced

	char *pchEnhanceApply;			AST(ESTRING WIKI(AUTO))
		// EnhanceAppy expression


	F32 fCostMin;					AST(DEFAULT(-1.f))
	F32 fCostMax;					AST(DEFAULT(-1.f))
	F32 fCostPeriodicMin;			AST(DEFAULT(-1.f))
	F32 fCostPeriodicMax;			AST(DEFAULT(-1.f))

	char *pchCost;					AST(ESTRING WIKI(AUTO))
		// Effective Cost expression

	char *pchCostPeriodic;			AST(ESTRING WIKI(AUTO))
		// Effective CostPeriodic expression


	S32 iMaxTargets;				AST(DEFAULT(-1))
	F32 fRangeMin;					AST(DEFAULT(-1.f))
	F32 fRangeMax;					AST(DEFAULT(-1.f))
	F32 fAreaMin;					AST(DEFAULT(-1.f))
	F32 fAreaMax;					AST(DEFAULT(-1.f))
	F32 fInnerAreaMin;				AST(DEFAULT(-1.f))
	F32 fInnerAreaMax;				AST(DEFAULT(-1.f))

	char *pchTarget;				AST(ESTRING WIKI(AUTO))
		// Combination of TargetMain, TargetAffected and MaxTargets fields

	char *pchRange;					AST(ESTRING WIKI(AUTO))
		// Combination of effective Range, Radius, Arc, EffectArea and Lunge

	char *pchTargetArc;				AST(ESTRING WIKI(AUTO))
		// TargetArc

	
	F32 fTimeChargeMin;				AST(DEFAULT(-1.f))
	F32 fTimeChargeMax;				AST(DEFAULT(-1.f))
	F32 fTimeActivateMin;			AST(DEFAULT(-1.f))
	F32 fTimeActivateMax;			AST(DEFAULT(-1.f))
	F32 fTimeActivatePeriodMin;		AST(DEFAULT(-1.f))
	F32 fTimeActivatePeriodMax;		AST(DEFAULT(-1.f))
	F32 fTimeMaintainMin;			AST(DEFAULT(-1.f))
	F32 fTimeMaintainMax;			AST(DEFAULT(-1.f))
	F32 fTimeRechargeMin;			AST(DEFAULT(-1.f))
	F32 fTimeRechargeMax;			AST(DEFAULT(-1.f))

	char *pchTimeCharge;			AST(ESTRING WIKI(AUTO))
		// TimeCharge

	char *pchTimeActivate;			AST(ESTRING WIKI(AUTO))
		// TimeActivate

	char *pchTimeActivatePeriod;	AST(ESTRING WIKI(AUTO))
		// TimeActivatePeriod

	char *pchTimeMaintain;			AST(ESTRING WIKI(AUTO))
		// TimeMaintain (derived from MaxPeriods)

	char *pchTimeRecharge;			AST(ESTRING WIKI(AUTO))
		// Effective TimeRecharge


	char *pchCharges;				AST(ESTRING WIKI(AUTO))
		// Charges, including charges remaining

	char *pchLifetimeUsage;			AST(ESTRING WIKI(AUTO))
		// LifetimeUsage, including usage time remaining

	char *pchLifetimeGame;			AST(ESTRING WIKI(AUTO))
		// LifetimeGame, including game time remaining

	char *pchLifetimeReal;			AST(ESTRING WIKI(AUTO))
		// LifetimeReal, including real time remaining


	char *pchAttribs;				AST(ESTRING WIKI(AUTO))
		// Describes the attribs which modify the duration and/or magnitude of the power

	char **ppchFootnotes;			AST(NAME(Footnotes) ESTRING WIKI(AUTO))
		// Footnotes for this Power and all its child Powers or AttribMods


	AutoDescPower **ppPowersCombo;					AST(NO_INDEX)
		// Powers that are part of this combo, if this is a Combo-type Power

	AutoDescPower **ppPowersEnhancements;			AST(NO_INDEX)
		// Enhancement Powers that are attached to this Power

	AutoDescPower **ppPowersIndexed;				AST(NAME(ps) UNOWNED)
		// INDEXED UNOWNED copy of the ppPowers arrays.  Used for Power-level custom
		//  AutoDescription messages to include data about the child/attached Powers.

	AutoDescAttribMod **ppAttribMods;				AST(NO_INDEX)
		// AttribMods that are part of this Power (including any additional AttribMods from attached Enhancements)

	AutoDescAttribMod **ppAttribModsEnhancements;	AST(NO_INDEX)
		// Enhancement AttribMods (from this Power or attached Enhancement) that aren't
		//  integrated into instanced AttribMods in the ppAttribMods list, such as CritChance

	AutoDescAttribMod **ppAttribModsIndexed;		AST(NAME(m) UNOWNED)
		// INDEXED UNOWNED copy of the two AttribMod arrays above.  Used for Power-level custom
		//  AutoDescription messages to include data about their AttribMods.
	
	S32 eRequiredGemType;
	bool bActive;

	S32* eaPowerTags;
		// EArray of power tags applied to the power which are allowed to be displayed.

} AutoDescPower;

// Container for formatted strings from the automatic description of an AttribMod or AttribModDef
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL WIKI("AutoDescAttribMod");
typedef struct AutoDescAttribMod
{
	S32 iKey;						AST(KEY)
		// Key for indexing, copied from AttribModDef's uiKeyAutoDesc

	char *pchAttribName;			AST(ESTRING WIKI(AUTO))
		// Attribute name

	char *pchAttribDesc;			AST(ESTRING WIKI(AUTO))
		// Attribute description

	char *pchAttribDescLong;		AST(ESTRING WIKI(AUTO))
		// Attribute long description

	char *pchDev;					AST(ESTRING WIKI(AUTO))
		// Dev mode data (error/warning messages)


	char *pchDefault;				AST(ESTRING WIKI(AUTO))
		// Default description, generated so it's available to Custom description

	char *pchCustom;				AST(ESTRING WIKI(AUTO))
		// Custom (AutoDescOverride) description

	char *pchEffect;				AST(ESTRING WIKI(AUTO))
		// Description of the effect, including magnitude, attribute and aspect, may be
		//  highly customized for the particular attribute and aspect.  Does not include
		//  duration, chance, etc.


	char *pchMagnitude;				AST(ESTRING WIKI(AUTO))
		// Effective magnitude (not including variance)

	char *pchMagnitudePerSecond;	AST(ESTRING WIKI(AUTO))
		// Effective magnitude per second (not including variance), only
		//  filled in when it might be useful and a reasonable calculation can be made.

	char *pchMagnitudeVariance;		AST(ESTRING WIKI(AUTO))
		// Variance when relevant to magnitude

	F32 fMagnitudeActual;
	F32 fMagnitudeMaxActual;
		// Calculated magnitudes, saved to pass down to inline PowerDefs for magnitude-based descriptions



	char *pchDuration;				AST(ESTRING WIKI(AUTO))
		// Effective duration (including variance and variable reason)

	char *pchPeriod;				AST(ESTRING WIKI(AUTO))
		// Period


	char *pchChance;				AST(ESTRING WIKI(AUTO))
		// Chance

	char *pchChanceDenormalized;	AST(ESTRING WIKI(AUTO))
		// Chance when normalized

	char *pchDelay;					AST(ESTRING WIKI(AUTO))
		// Delay

	const char *pchTarget;			AST(UNOWNED WIKI(AUTO))
		// Target

	char *pchStackLimit;			AST(ESTRING WIKI(AUTO))
		// StackLimit

	const char *pchRequires;		AST(UNOWNED WIKI(AUTO))
		// Textual indicator of the existence of a Requires expression

	const char *pchAffects;			AST(UNOWNED WIKI(AUTO))
		// Textual indicator of the existence of an Affects expression


	AutoDescPower **ppPowersInline;	AST(STRUCT_NORECURSE, NO_INDEX)
		// Powers that should be considered for inline display


	char *pchExpire;				AST(ESTRING WIKI(AUTO))
		// Description of expiration event

	AutoDescPower *pPowerExpire;	AST(STRUCT_NORECURSE)
		// Power from expiration that should be considered for inline display


	AutoDescPower *pPowerSub;			AST(NAME(p) UNOWNED)
		// UNOWNED copy of an Inline or Expire Power, in the case there is only one total
		//  sub-Power for this AttribMod.  Makes walking into sub-Powers for custom
		//  AutoDescription messages trivial in 95% of the cases.

	AutoDescPower **ppPowersSubIndexed;	AST(NAME(ps) UNOWNED)
		// INDEXED UNOWNED copy of the Inline Powers array and the Expire Power, in the
		//  case there is more than one total, so each can be referenced by name for
		//  custom AutoDescription messages to include data about their Powers.


	AutoDescAttribMod **ppModUnrollCost;	AST(NAME(c))
		// INDEXED versions of this AttribMod with various Activation.CostPaid, keyed
		//  by that value.  Used when the Power has a custom cost attribute and the
		//  AttribMod varies significantly based on how much of that Attribute was
		//  paid.


	AttribType offAttrib;			AST(NAME(AttribType))
		// The attribute of the AttribMod

	AttribAspect offAspect;			AST(NAME(AttribAspect))
		// The aspect of the AttribMod

	U32 bEnhancementExtension : 1;	AST(WIKI(AUTO))
		// Set to true when this is an AttribMod from an Enhancement

} AutoDescAttribMod;

AUTO_STRUCT;
typedef struct AutoDescInnateModDetails {
	F32 fMagnitude;
	bool bAttribBoolean;
	bool bPercent;
	bool bDamageAttribAspect;
	AttribType offAttrib;
	AttribAspect offAspect;
	char *pchDefaultMessage;		AST(ESTRING)
	char *pchAttribName;			AST(ESTRING)
	char *pchDesc;					AST(ESTRING)
	char *pchDescLong;				AST(ESTRING)
	const char *pchPowerDef;		AST(POOL_STRING)
	char *pchRequiredGemSlot;		AST(ESTRING)
} AutoDescInnateModDetails;


// TODO(JW): AutoDesc: Design reasonable API for this entire system

void power_AutoDesc(int iPartitionIdx,
					SA_PARAM_NN_VALID Power *ppow,
					SA_PARAM_OP_VALID Character *pchar,
					SA_PARAM_OP_VALID char **ppchDesc,
					SA_PARAM_OP_VALID AutoDescPower *pAutoDescPower,
					SA_PARAM_OP_STR const char *cpchLine,
					SA_PARAM_OP_STR const char *cpchModIndent,
					SA_PARAM_OP_STR const char *cpchModPrefix,
					int bMinimalPowerDefHeader,
					int iDepth,
					AutoDescDetail eDetail,
					GameAccountDataExtract *pExtract,
					const char* pchPowerAutoDescMessageKey);

void powerdef_ConsolidateAutoDesc(AutoDescPower *pAutoDescPower);

void powerdef_AutoDesc(int iPartitionIdx,
					   SA_PARAM_NN_VALID PowerDef *pdef,
					   SA_PARAM_OP_VALID char **ppchDesc,
					   SA_PARAM_OP_VALID AutoDescPower *pAutoDescPower,
					   SA_PARAM_OP_STR const char *cpchLine,
					   SA_PARAM_OP_STR const char *cpchModIndent,
					   SA_PARAM_OP_STR const char *cpchModPrefix,
					   SA_PARAM_OP_VALID Character *pchar,
					   SA_PARAM_OP_VALID Power *ppow,
					   SA_PARAM_OP_VALID Power **ppEnhancements,
					   int iLevel,
					   int bIncludeStrength,
					   AutoDescDetail eDetail,
					   GameAccountDataExtract *pExtract,
					   const char *pchPowerAutoDescMessageKey);

void powerdefs_AutoDescInnateMods(int iPartitionIdx,
								  Item * pItem,
								  SA_PARAM_NN_VALID PowerDef **ppdefs,
								  SA_PARAM_NN_VALID F32 *pfScales,
								  SA_PARAM_NN_VALID char **ppchDesc,
								  SA_PARAM_OP_STR const char *cpchKey,
								  SA_PARAM_OP_VALID Character *pchar,
								  S32 *peaSlotRequired,
								  int iLevel,
								  int bIncludeStrength,
								  S32 eActiveGemSlotType,
								  AutoDescDetail eDetail);

void powerdefs_GetAutoDescInnateMods(int iPartitionIdx,
									 Item * pItem,
									 SA_PARAM_NN_VALID PowerDef **ppdefs,
								  SA_PARAM_NN_VALID F32 *pfScales,
								  SA_PARAM_OP_VALID char **ppchDesc,
								  SA_PARAM_OP_VALID AutoDescInnateModDetails ***pppDetails,
								  SA_PARAM_OP_STR const char *cpchKey,
								  SA_PARAM_OP_VALID Character *pchar,
								  S32 *peaSlotRequired,
								  int iGemPowersBegin,
								  int iLevel,
								  int bIncludeStrength,
								  S32 eActiveGemSlotType,
								  AutoDescDetail eDetail);

void powerdefs_GetAutoDescInnateModsDiff(	int iPartitionIdx,
											SA_PARAM_NN_VALID Item * pItem,
											SA_PARAM_NN_VALID Item * pOtherItem,
											SA_PARAM_NN_NN_STR char **ppchDesc,
											SA_PARAM_OP_STR const char *cpchKey,
											SA_PARAM_OP_VALID Character *pChar,
											AutoDescDetail eDetail);

void modnet_AutoDesc(SA_PARAM_NN_VALID Character *pchar,
					 SA_PARAM_NN_VALID AttribModNet *pmodnet,
					 SA_PARAM_NN_VALID char **ppchDesc,
					 Language lang,
					 AutoDescDetail eDetail);

void attrib_AutoDescPowerStats(SA_PARAM_NN_VALID char **ppchDesc,
							   AttribType eAttrib,
							   SA_PARAM_NN_VALID Character *pchar,
							   Language lang,
							   AutoDescDetail eDetail,
							   const char *pchPrimaryHeader,
							   const char *pchSecondaryHeader,
							   const char *pchSortingTag);


// Automatic description for the attribute granted by a PTNodeDef at a particular rank (0-based)
void powertreenode_AutoDescAttrib(SA_PARAM_NN_VALID char **ppchDesc,
								  SA_PARAM_NN_VALID PTNodeDef *pdef,
								  int iRank,
								  Language lang);


void combatevent_AutoDesc(SA_PARAM_NN_VALID char **ppchDesc,
						  EntityRef erTarget,
						  EntityRef erOwner,
						  EntityRef erSource,
						  S32 iAttrib,
						  F32 fMagnitude,
						  F32 fMagnitudeBase,
						  SA_PARAM_OP_VALID Message *pMsg,
						  SA_PARAM_OP_VALID Message *pSecondarymsg,
						  CombatTrackerFlag eFlags,
						  U32 bPositive);

void combattracker_CombatLog(SA_PARAM_NN_VALID CombatTrackerNet *pnet, EntityRef erTarget);


void AutoDesc_InnateAttribMods(int iPartitionIdx, Character *pchar, PowerDef *pdef, AutoDescAttribMod ***peaInfos);

// Returns the translated name, desc or desclong for the Attrib, or if that doesn't exist, returns the internal name
const char *attrib_AutoDescName(AttribType eAttrib, Language lang);
const char *attrib_AutoDescDesc(AttribType eAttrib, Language lang);
const char *attrib_AutoDescDescLong(AttribType eAttrib, Language lang);

// Returns if the Attrib is a percent as far as AutoDesc is concerned
S32 attrib_AutoDescIsPercent(AttribType eAttrib);

void powerdef_AutoDescCustom(Entity *pEnt, 
							 char **ppchDescription, 
							 PowerDef *pPowerDef, 
							 AutoDescPower *pAutoDescPower, 
							 const char* pchPowerMessageKey, 
							 const char* pchAttribModsMessageKey);

int GetAttributesAffectingPower(SA_PARAM_NN_VALID PowerDef *pdef, Language lang, U32** peaAttribs);
void powerdef_UseNNOFormattingForBuff(PowerDef* pDef, char** ppchDesc);