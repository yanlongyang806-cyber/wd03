#pragma once

#include "GlobalTypes.h"

void SortyByRatings_Init(void);

void SortByRatings_AddOrUpdate(GlobalType nodeType, U32 iKey, void *pVal, float fRating); 
//tries to do as little as possible if pVal and/or fRating have not changed

void SortByRatings_Remove(GlobalType nodeType, U32 iKey);

//returns true if done
typedef int (*SortByRatingsIterationFunc)(GlobalType nodeType, void *pVal, U32 iKey, void *pUserData);

void SortByRatings_Iterate(float fMaxRating, float fMinRating, SortByRatingsIterationFunc pFunc, void *pUserData);
