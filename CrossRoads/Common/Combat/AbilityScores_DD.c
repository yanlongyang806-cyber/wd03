/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "AbilityScores_DD.h"

#include "Entity.h"
#include "Character.h"
#include "StatPoints_h_ast.h"
#include "ContinuousBuilderSupport.h"

// All stat point costs to have a certain ability score
const int g_DDAbilityStatPointCostLookupTable[11] =
{
	0,		// Cost to have an ability score of 8
	1,		// Cost to have an ability score of 9
	2,		// Cost to have an ability score of 10
	3,		// Cost to have an ability score of 11
	4,		// Cost to have an ability score of 12
	5,		// Cost to have an ability score of 13
	7,		// Cost to have an ability score of 14
	9,		// Cost to have an ability score of 15
	11,		// Cost to have an ability score of 16
	14,		// Cost to have an ability score of 17
	18		// Cost to have an ability score of 18
};

// Returns -1 in case there is an invalid value, otherwise returns the points left
S32 DDGetPointsLeftForAbilityScores(NOCONST(AssignedStats) **eaAssignedStats)
{
	S32 i;
	S32 iPointsSpent = 0;
	StatPointPoolDef *pDef = StatPointPool_DefFromName(STAT_POINT_POOL_DEFAULT);

	if (eaAssignedStats == NULL)
		return DD_MAX_STAT_POINTS;

	// Look for the key
	for (i = eaSize(&eaAssignedStats) - 1; i >= 0; i--)
	{
		if (pDef == NULL || !StatPointPool_ContainsAttrib(pDef, eaAssignedStats[i]->eType))
			continue;

		// Remove any stat points with a point value of 0
		if (eaAssignedStats[i]->iPoints == 0)
		{
			eaRemove(&eaAssignedStats, i);
			continue;
		}

		// Following 2 conditions should not be met unless someone is trying to hack the values
		if (eaAssignedStats[i]->iPoints < 1 || eaAssignedStats[i]->iPoints > 10) 
			return -1;

		iPointsSpent += g_DDAbilityStatPointCostLookupTable[eaAssignedStats[i]->iPoints];

		// Set the penalty to the correct value no matter what
		eaAssignedStats[i]->iPointPenalty = 
			g_DDAbilityStatPointCostLookupTable[eaAssignedStats[i]->iPoints] - eaAssignedStats[i]->iPoints;
	}

	return DD_MAX_STAT_POINTS - iPointsSpent;
}

F32 DDGetBaseAbilityScore(const char *pszAttribName)
{
	return DD_MIN_ABILITY_SCORE;
}

F32 DDApplyStatPointsToBaseAbilityScore(const char *pszAttribName, NOCONST(AssignedStats) *pAssignedStats)
{
	F32 fBaseValue = DDGetBaseAbilityScore(pszAttribName);
	return pAssignedStats ? fBaseValue + pAssignedStats->iPoints : fBaseValue;
}

F32 DDGetAbilityScoreFromAssignedStats(NOCONST(AssignedStats) **eaAssignedStats, const char *pszAttribName)
{
	S32 i;
	AttribType eAttribType = StaticDefineIntGetInt(AttribTypeEnum, pszAttribName);

	if (eAttribType < 0)
		return 0.0f;

	// Check if the array is initialized already
	if (eaAssignedStats)
	{
		// Look for the key
		for (i = 0; i < eaSize(&eaAssignedStats); i++)
		{			
			if (eaAssignedStats[i]->eType == eAttribType)
			{
				return DDApplyStatPointsToBaseAbilityScore(pszAttribName, eaAssignedStats[i]);
			}
		}
	}

	// Return the base value
	return DDGetBaseAbilityScore(pszAttribName);
}

// If bCheckPointsRemaining is set to true, function makes sure that points left is 0
bool DDIsAbilityScoreSetValid(NOCONST(AssignedStats) **eaAssignedStats, bool bCheckPointsRemaining)
{
	S32 i;
	S32 iAttribsLessThan10 = 0;

	static F32 arrAttribValues[6] = { 0 };

	arrAttribValues[0] = DDGetAbilityScoreFromAssignedStats(eaAssignedStats, "STR");
	arrAttribValues[1] = DDGetAbilityScoreFromAssignedStats(eaAssignedStats, "CON");
	arrAttribValues[2] = DDGetAbilityScoreFromAssignedStats(eaAssignedStats, "DEX");
	arrAttribValues[3] = DDGetAbilityScoreFromAssignedStats(eaAssignedStats, "INT");
	arrAttribValues[4] = DDGetAbilityScoreFromAssignedStats(eaAssignedStats, "WIS");
	arrAttribValues[5] = DDGetAbilityScoreFromAssignedStats(eaAssignedStats, "CHA");

	for (i = 0; i < 6; i++)
	{
		// No attrib can be greater than 18
		if (arrAttribValues[i] > 18.0f)
		{
			return false;
		}

		if (arrAttribValues[i] < 10.0f)
		{
			++iAttribsLessThan10;
		}

		// Only one attrib can be less than 10
		if (iAttribsLessThan10 > 1)
		{
			return false;
		}
	}

	return bCheckPointsRemaining ? DDGetPointsLeftForAbilityScores(eaAssignedStats) == 0 : true;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CharacterCreation_IsValidNNOCharacter");
const char * CharacterCreation_IsValidNNOCharacter(SA_PARAM_NN_VALID Entity *pEntity)
{
	// Do not validate while continuous builder is running
	if (g_isContinuousBuilder)
		return "";

	if (pEntity == NULL)
		return "Character validation failed: Invalid entity.";

	if (pEntity->pChar == NULL)
		return "Character validation failed: Invalid character.";

	// Do not validate for quickplay (Need to implement proper characters for quick play later on)
	if (pEntity->pChar->ppAssignedStats == NULL)
		return "";

	if (!DDIsAbilityScoreSetValid((NOCONST(AssignedStats) **)pEntity->pChar->ppAssignedStats, true))
		return "Character validation failed: Ability score set is invalid.";

	return "";
}