#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct Character Character;
typedef struct CritterDef CritterDef;
typedef struct AttribModDef AttribModDef;

AUTO_STRUCT;
typedef struct CombatMod
{
	// Holds a set of percentage modifiers used to vary combat between
	// characters of different level.
	float fMagnitude;
	float fDuration;
}CombatMod;

AUTO_STRUCT;
typedef struct CombatMods
{
	CombatMod *pHigher;
	CombatMod *pLower;
	CombatMod *pHigherPlayer;
	CombatMod *pLowerPlayer;
}CombatMods;

AUTO_STRUCT;
typedef struct CombatList
{
	// the valid attribs that can affect magnitudes
	// -2 means to affect all
	// -1 means none
	S32 eAllowedMagnitudeAttrib;			AST(SUBTABLE(AttribTypeEnum), DEFAULT(-2))

	// the valid attribs that can affect magnitudes
	// -2 means to affect all
	// -1 means none
	S32 eAllowedDurationAttrib;			AST(SUBTABLE(AttribTypeEnum), DEFAULT(-2))
	
	CombatMods **ppCombatMods;
}CombatList;


void CombatMods_Load(void);
CombatMod *CombatMod_getMod(int iSourceLevel, int iTargetLevel,bool bPlayer);

F32 CombatMod_getMagnitude(CombatMod *pMod);
F32 CombatMod_getDuration(CombatMod *pMod);
bool CombatMod_ignore(int iPartitionIdx, SA_PARAM_NN_VALID Character *pSource,SA_PARAM_NN_VALID Character *pTarget);

bool CombatMod_DoesAffectMagnitude(S32 attrib);
bool CombatMod_DoesAffectDuration(S32 attrib);
