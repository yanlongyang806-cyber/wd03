/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "AttribCurveImp.h"

#include "timing.h"

#include "Powers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Actually applies the curve and returns the value
F32 character_ApplyAttribCurve(Character *pchar,
							   AttribCurve *pcurve,
							   F32 fVal)
{
	F32 fReturn = fVal;

	PERFINFO_AUTO_START_FUNC();

	switch(pcurve->eType)
	{
		case kAttribCurveType_Max:
			{
				AttribCurveMax *pcurveMax = (AttribCurveMax*)pcurve;
				fReturn = MIN(fReturn,pcurveMax->fMax);
			}
			break;
		case kAttribCurveType_QuadraticMax:
			{
				AttribCurveQuadraticMax *pcurveQMax = (AttribCurveQuadraticMax*)pcurve;
				F32 fMax = pcurveQMax->fMax;
				if(fMax>0)
				{
					F32 fRatio = fVal / fMax;
					fReturn = fMax - fMax/(1+fRatio+fRatio*fRatio);
				}
			}
			break;
		default:
			PowersError("Unknown diminishing returns type %d",pcurve->eType);
	}

	PERFINFO_AUTO_STOP();

	return fReturn;
}



#include "AutoGen/AttribCurveImp_h_ast.c"
#include "AutoGen/AttribCurve_h_ast.c"