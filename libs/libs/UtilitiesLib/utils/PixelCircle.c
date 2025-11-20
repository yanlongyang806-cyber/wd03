#include "stashtable.h"
#include "PixelCircle.h"
#include "PixelCircle_h_ast.h"


StashTable sCoordsByRadius = NULL;

PixelCircle *pFindCircleCoordsForRadius(int iRadius)
{
	PixelCircle *pCircle;
	int x;
	int y;
	int iRadSquared;

	if (!sCoordsByRadius)
	{
		sCoordsByRadius = stashTableCreateInt(16);
	}

	if (stashIntFindPointer(sCoordsByRadius, iRadius, &pCircle))
	{
		return pCircle;
	}

	pCircle = StructCreate(parse_PixelCircle);
	iRadSquared = iRadius * iRadius;

	//this could be efficientized quite a it, but it only happens once per size, and the real hard work
	//is updating all the event counters, and that has to happen no matter how efficiently we store our circle


	for (x = -iRadius; x <= iRadius; x++)
	{
		for (y = -iRadius; y <= iRadius; y++)
		{
			if (x * x + y * y <= iRadSquared)
			{
				PixelCircleCoords *pCoords = StructCreate(parse_PixelCircleCoords);

				pCoords->x = x;
				pCoords->y = y;

				eaPush(&pCircle->ppCoords, pCoords);
			}
		}
	}

	stashIntAddPointer(sCoordsByRadius, iRadius, pCircle, true);

	return pCircle;
}


#include "PixelCircle_h_ast.c"
