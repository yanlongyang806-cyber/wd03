#include "autoPlacementCommon.h"
#include "WorldGrid.h"
#include "Expression.h"
#include "LineDist.h"
#include "quat.h"

#ifndef NO_EDITORS

#include "StringCache.h"
#include "Materials.h"
#include "oldencounter_common.h"

#ifdef GAMESERVER
#include "gslEncounter.h"
#include "wlEncounter.h"
#endif


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


static ExprContext *s_pExprContext = NULL;
AutoPlacementExpressionData g_AutoPlacementExprData;

extern ParseTable parse_AutoPlacementExpressionData[];
#define TYPE_parse_AutoPlacementExpressionData AutoPlacementExpressionData


// -------------------------------------------------------------------------------------
__forceinline static bool apvPointInSphere(const AutoPlacementVolume *pVolume, const Vec3 vPt)
{
	F32 fDistSq = distance3Squared(pVolume->vPos, vPt);
	return fDistSq <= SQR(pVolume->fRadius);
}

// -------------------------------------------------------------------------------------
__forceinline static bool apvPointInBox(const AutoPlacementVolume *pVolume, const Vec3 vPt)
{
	Vec3 vLocalPt, vTmp;

	// transform the point into the local box's space
	subVec3(vPt, pVolume->vPos, vTmp);
	quatRotateVec3(pVolume->qInvRot, vTmp, vLocalPt);

	return pointBoxCollision(vLocalPt, pVolume->vMin, pVolume->vMax);
}

// -------------------------------------------------------------------------------------
bool apvPointInVolume(const AutoPlacementVolume *pVolume, const Vec3 vPt)
{
	if (pVolume->bAsCube)
	{
		if (apvPointInBox(pVolume, vPt))
			return true;
	}
	else
	{
		if (apvPointInSphere(pVolume, vPt))
			return true;
	}

	return false;
}

// -------------------------------------------------------------------------------------
ExprContext* getAutoPlacementExprContext( )
{
	if(s_pExprContext == NULL)
	{
		ExprFuncTable* stTable;

		s_pExprContext = exprContextCreate();
		
		// Functions
		//  Generic, Self, Character
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "AutoPlaceCommand");
		
		exprContextSetFuncTable(s_pExprContext, stTable);

		exprContextSetPointerVar(s_pExprContext, "AutoPlacementData", &g_AutoPlacementExprData, parse_AutoPlacementExpressionData, false, true );
	}

	exprContextSetSelfPtr(s_pExprContext,NULL);

	return s_pExprContext;
}


#endif // #ifndef NO_EDITORS


//#define IS_KOSHER_CORNER(P1, P2) ( ((P1)==eConnectionType_INTERNAL_CORNER && (P2)>=eConnectionType_EDGE) ||\
//									  ((P1)==eConnectionType_EXTERNAL_CORNER && ((P2)==eConnectionType_EDGE || (P2)==eConnectionType_INTERNAL_CORNER)) )
#define IS_KOSHER_CORNER(P1, P2) ( (P1)>=eConnectionType_EXTERNAL_CORNER && (!strict || (P2)>=eConnectionType_EDGE))


// Description: Returns a heuristic of how close the given potential position is to a corner.
//	If the potential position is within the fMinDistance, 1.0 is returned.
//	If the potential position is further than the fMaxDistance, 0.0 is returned.
//	Otherwise a value between 0 and 1 is returned. The closer to 0, the further the corner is from the position.
// Param: strict - signifies how strictly it determines if something is an appropriate corner/edge. 
//	A value of zero disables strict. 
AUTO_EXPR_FUNC(AutoPlaceCommand) ACMD_NAME(AutoPlacementFitnessToCorner);
F32 exprFuncAutoPlacementFitnessToCorner(F32 fMinDistance, F32 fMaxDistance, int strict)
#if defined(GAMESERVER) && !defined(NO_EDITORS)
{
	int i;
	F32 fMinDistanceSQ;
	F32 fMaxDistanceSQ;
	F32 fClosestDistSQ = FLT_MAX;

	if (fMaxDistance <= (fMinDistance + 1.0f)) 
		fMaxDistance = fMinDistance + 1;

	fMinDistanceSQ = fMinDistance*fMinDistance;
	fMaxDistanceSQ = fMaxDistance*fMaxDistance;


	for( i = 0; i < eaSize(&g_AutoPlacementExprData.eaPotentialLineSegmentList); i++)
	{
		PotentialLineSegment *pLineSegment = g_AutoPlacementExprData.eaPotentialLineSegmentList[i];

		if (IS_KOSHER_CORNER(pLineSegment->pt1Type, pLineSegment->pt2Type))
		{
			F32 fDistSQ = distance3Squared(g_AutoPlacementExprData.currentPotentialPos, pLineSegment->vPt1);
			
			if (fDistSQ < fClosestDistSQ)
			{
				if (fDistSQ <= fMinDistanceSQ)
				{
					return 1.0f;
				}

				fClosestDistSQ = fDistSQ;
			}
		}
		else if (IS_KOSHER_CORNER(pLineSegment->pt2Type, pLineSegment->pt1Type))
		{
			F32 fDistSQ = distance3Squared(g_AutoPlacementExprData.currentPotentialPos, pLineSegment->vPt2);
			
			if (fDistSQ < fClosestDistSQ)
			{
				if (fDistSQ <= fMinDistanceSQ)
				{
					return 1.0f;
				}

				fClosestDistSQ = fDistSQ;
			}
		}
	}

	if (fClosestDistSQ > fMaxDistanceSQ)
		return 0.0f;
	
	if (fMaxDistanceSQ == fMinDistanceSQ)
		return 1.0f;

	return 1.0f - (fClosestDistSQ - fMinDistanceSQ) / (fMaxDistanceSQ - fMinDistanceSQ);
}
#else
{
	return 0.0f;
}
#endif // #if defined(GAMESERVER) && !defined(NO_EDITORS)

// Description: Returns true/false if the potential position is within a given distance to a corner. 
// Param: strict - signifies how strictly it determines if something is an appropriate corner/edge. 
//	A value of zero disables strict. 
AUTO_EXPR_FUNC(AutoPlaceCommand) ACMD_NAME(AutoPlacementProximityToCorner);
int exprFuncAutoPlacementProximityToCorner(F32 fDistToCorner, int strict)
#if defined(GAMESERVER) && !defined(NO_EDITORS)
{
	return exprFuncAutoPlacementFitnessToCorner(fDistToCorner, fDistToCorner, strict) > 0.0f;
}
#else
{
	return false;
}
#endif // #if defined(GAMESERVER) && !defined(NO_EDITORS)





#define IS_KOSHER_EDGE(P1, P2)	( ((P1)==eConnectionType_EDGE && (!strict || (P2)>=eConnectionType_EDGE)) ||\
	((P2)==eConnectionType_EDGE && (!strict || (P1)>=eConnectionType_EDGE)) )



// Description: Returns a heuristic of how close the given potential position is to an edge.
//	If the potential position is within the fMinDistance, 1.0 is returned.
//	If the potential position is further than the fMaxDistance, 0.0 is returned.
//	Otherwise a value between 0 and 1 is returned. The closer to 0, the further the corner is from the position.
// Param: strict - signifies how strictly it determines if something is an appropriate corner/edge. 
AUTO_EXPR_FUNC(AutoPlaceCommand) ACMD_NAME(AutoPlacementFitnessToEdge);
F32 exprFuncAutoPlacementFitnessToEdge(F32 fMinDistance, F32 fMaxDistance, int strict)
#if defined(GAMESERVER) && !defined(NO_EDITORS)
{
	int i;
	F32 fMinDistanceSQ;
	F32 fMaxDistanceSQ;
	F32 fClosestDistSQ = FLT_MAX;

	if (fMaxDistance < fMinDistance) fMaxDistance = fMinDistance;

	fMinDistanceSQ = fMinDistance*fMinDistance;
	fMaxDistanceSQ = fMaxDistance*fMaxDistance;

	for( i = 0; i < eaSize(&g_AutoPlacementExprData.eaPotentialLineSegmentList); i++)
	{
		PotentialLineSegment *pLineSegment = g_AutoPlacementExprData.eaPotentialLineSegmentList[i];

		if (IS_KOSHER_EDGE(pLineSegment->pt1Type, pLineSegment->pt2Type))
		{
			F32 fPtLineDistSQ = pointLineDistSquared(g_AutoPlacementExprData.currentPotentialPos, pLineSegment->vPt1, pLineSegment->vPt2, NULL);

			if (fPtLineDistSQ < fClosestDistSQ)
			{
				if (fPtLineDistSQ <= fMinDistanceSQ)
				{
					return 1.0f;
				}

				fClosestDistSQ = fPtLineDistSQ;
			}
		}
	}

	if (fClosestDistSQ > fMaxDistanceSQ)
		return 0.0f;
	
	if (fMaxDistanceSQ == fMinDistanceSQ)
		return 1.0f;
	
	return 1.0f - (fClosestDistSQ - fMinDistanceSQ) / (fMaxDistanceSQ - fMinDistanceSQ);
}
#else
{
	return 0.0f;
}
#endif // #if defined(GAMESERVER) && !defined(NO_EDITORS)

// Description: Returns true/false if the potential position is within a given distance to an edge. 
// Param: strict - signifies how strictly it determines if something is an appropriate corner/edge. 
//	A value of zero disables strict. 
AUTO_EXPR_FUNC(AutoPlaceCommand) ACMD_NAME(AutoPlacementProximityToEdge);
int exprFuncAutoPlacementProximityToEdge(F32 fDistToEdge, int strict)
#if defined(GAMESERVER) && !defined(NO_EDITORS)
{
	return exprFuncAutoPlacementFitnessToEdge(fDistToEdge, fDistToEdge, strict) > 0.0f;
}
#else
{
	return false;
}
#endif // #if defined(GAMESERVER) && !defined(NO_EDITORS)



// Description: Returns true/false if the potential position is actually on a corner
// Param: strict - signifies how strictly it determines if something is an appropriate corner/edge. 
//	A value of zero disables strict. 
AUTO_EXPR_FUNC(AutoPlaceCommand) ACMD_NAME(AutoPlacementIsOnCorner);
int exprFuncAutoPlacementIsOnCorner(int strict)
#if defined(GAMESERVER) && !defined(NO_EDITORS)
{

	return IS_KOSHER_CORNER(g_AutoPlacementExprData.pCurrentLineSegment->pt1Type, g_AutoPlacementExprData.pCurrentLineSegment->pt2Type)  || 
		IS_KOSHER_CORNER(g_AutoPlacementExprData.pCurrentLineSegment->pt2Type, g_AutoPlacementExprData.pCurrentLineSegment->pt1Type);
}
#else
{
	return false;
}
#endif // #if defined(GAMESERVER) && !defined(NO_EDITORS)

// Description: Returns true/false if the potential position is actually on an edge
// Param: strict - signifies how strictly it determines if something is an appropriate corner/edge. 
//	A value of zero disables strict. 
AUTO_EXPR_FUNC(AutoPlaceCommand) ACMD_NAME(AutoPlacementIsOnEdge);
int exprFuncAutoPlacementIsOnEdge(int strict)
#if defined(GAMESERVER) && !defined(NO_EDITORS)
{
	return	IS_KOSHER_EDGE(g_AutoPlacementExprData.pCurrentLineSegment->pt1Type, g_AutoPlacementExprData.pCurrentLineSegment->pt2Type);
}
#else
{
	return false;
}
#endif // #if defined(GAMESERVER) && !defined(NO_EDITORS)

// Description: Returns a heuristic of how close the given potential position is to an encounter.
//	If the potential position is within the fMinDistance, 1.0 is returned.
//	If the potential position is further than the fMaxDistance, 0.0 is returned.
AUTO_EXPR_FUNC(AutoPlaceCommand) ACMD_NAME(AutoPlacementFitnessToEncounter);
F32 exprFuncAutoPlacementFitnessToEncounter(F32 fMinDistance, F32 fMaxDistance)
#if defined(GAMESERVER) && !defined(NO_EDITORS)
{
	S32 i;
	F32 fMinDistanceSQ;
	F32 fMaxDistanceSQ;
	F32 fClosestDistSQ = FLT_MAX;

	if (fMaxDistance < fMinDistance) fMaxDistance = fMinDistance;

	fMinDistanceSQ = SQR(fMinDistance);
	fMaxDistanceSQ = SQR(fMaxDistance);
	
	// Scan new encounters
	for(i = 0; i < eaSize(&g_AutoPlacementExprData.eaNearbyEncounters); i++)
	{
		GameEncounter *enc = g_AutoPlacementExprData.eaNearbyEncounters[i];
		Vec3 pos;
		F32 fDistSQ;

		encounter_GetPosition(enc, pos);
		fDistSQ = distance3Squared(g_AutoPlacementExprData.currentPotentialPos, pos);

		if (fDistSQ < fClosestDistSQ)
		{
			if (fDistSQ <= fMinDistanceSQ)
			{
				return 1.0f;
			}

			fClosestDistSQ = fDistSQ;
		}
	}

	if (gConf.bAllowOldEncounterData) 
	{
		for(i = 0; i < eaSize(&g_AutoPlacementExprData.eaNearbyOldEncounters); i++)
		{
			OldStaticEncounter *enc = g_AutoPlacementExprData.eaNearbyOldEncounters[i];
			F32 fDistSQ = distance3Squared(g_AutoPlacementExprData.currentPotentialPos, enc->encPos);

			if (fDistSQ < fClosestDistSQ)
			{
				if (fDistSQ <= fMinDistanceSQ)
				{
					return 1.0f;
				}

				fClosestDistSQ = fDistSQ;
			}
		}
	}

	if (fClosestDistSQ > fMaxDistanceSQ)
		return 0.0f;
	
	if (fMaxDistanceSQ == fMinDistanceSQ)
		return 1.0f;

	return 1.0f - (fClosestDistSQ - fMinDistanceSQ) / (fMaxDistanceSQ - fMinDistanceSQ);

}
#else
{
	return 0.0f;
}
#endif



#include "autoPlacementCommon_h_ast.c"
