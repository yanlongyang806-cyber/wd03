/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ShardEventTimingCommon.h"

#include "HashFunctions.h"
#include "rand.h"
#include "timing.h"

#include "autogen/ShardEventTimingCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

void ShardEventTiming_Validate(ShardEventTimingDef* pShardEventTimingDef, const char* pContextualName)
{
	S32 i;
	for (i = eaSize(&pShardEventTimingDef->ppTimingEntries)-1; i >= 0; i--)
	{
		ShardEventTimingEntry *pShardEventTimingEntry = pShardEventTimingDef->ppTimingEntries[i];

		if (!pShardEventTimingEntry->uDateStart)
		{
			Errorf("Shard Event Timing in %s has a start date (%s) that is an invalid format", pContextualName, pShardEventTimingEntry->pchStartDate);
		}
		if (!pShardEventTimingEntry->uTimeEnd)
		{
			Errorf("Shard Event Timing in %s has a end date (%s) that is an invalid format", pContextualName, pShardEventTimingEntry->pchEndDate);
		}
		if (pShardEventTimingEntry->uDateStart > pShardEventTimingEntry->uTimeEnd)
		{
			Errorf("Shard Event Timing in %s has a start date (%s) after the end date (%s)", pContextualName, pShardEventTimingEntry->pchStartDate, pShardEventTimingEntry->pchEndDate);
		}

		if (pShardEventTimingEntry->bRandomStartTime)
		{
			if (!pShardEventTimingEntry->uTimeRepeat)
			{
				Errorf("Shard Event Timing in %s has a timing def flagged as RandomStartTime, but the timing def does not specify a repeat time", pContextualName);
			}
			else if (pShardEventTimingEntry->uTimeDuration >= pShardEventTimingEntry->uTimeRepeat)
			{
				Errorf("Shard Event Timing in %s has a repeating timing def flagged as RandomStartTime with a duration (%d) that is greater than or equal to the repeat time (%d)", pContextualName, pShardEventTimingEntry->uTimeDuration, pShardEventTimingEntry->uTimeRepeat);
			}
		}
	}
}

void ShardEventTiming_Fixup(ShardEventTimingDef* pShardEventTimingDef, U32 uSeedBase)
{
	int i;

	pShardEventTimingDef->uSeedBase = uSeedBase;

	for(i=0;i<eaSize(&pShardEventTimingDef->ppTimingEntries);i++)
	{
		ShardEventTimingEntry *pShardEventTimingEntry = pShardEventTimingDef->ppTimingEntries[i];

		if(pShardEventTimingEntry->pchStartDate)
		{
			pShardEventTimingEntry->uDateStart=timeGetSecondsSince2000FromGenericString(pShardEventTimingEntry->pchStartDate);
		}
		
		if(pShardEventTimingEntry->pchEndDate)
		{
			pShardEventTimingEntry->uTimeEnd=timeGetSecondsSince2000FromGenericString(pShardEventTimingEntry->pchEndDate);
		}
	}
}

// Return the starting time of a particular cycle of a repeating timing entry, or the timing entry start date if it is not repeating.
// Ignores the end date.
U32 ShardEventTiming_GetTimingDefStartTimeFromCycle(ShardEventTimingDef* pShardEventTimingDef, int iTimingIndex, U32 uCycleIdx)
{
	U32 uTimeStart = 0;
	if (pShardEventTimingDef && iTimingIndex >= 0 && iTimingIndex < eaSize(&pShardEventTimingDef->ppTimingEntries))
	{
		ShardEventTimingEntry* pTimingEntry = pShardEventTimingDef->ppTimingEntries[iTimingIndex];
		if (pTimingEntry->uTimeRepeat)
		{
			if (pTimingEntry->bRandomStartTime)
			{
				U32 uSeed = pShardEventTimingDef->uSeedBase + iTimingIndex + uCycleIdx;
				U32 uTimeStartFirst = pTimingEntry->uDateStart + pTimingEntry->uTimeRepeat * uCycleIdx;
				U32 uTimeStartLast = uTimeStartFirst + pTimingEntry->uTimeRepeat - pTimingEntry->uTimeDuration;
				uTimeStart = randomIntRangeSeeded(&uSeed, RandType_LCG, uTimeStartFirst, uTimeStartLast);
			}
			else
			{
				uTimeStart = pTimingEntry->uDateStart + pTimingEntry->uTimeRepeat * uCycleIdx;
			}
		}
		else
		{
			uTimeStart = pTimingEntry->uDateStart;
		}
	}
	return uTimeStart;
}


// For the given ShardEventTimingDef and QueryTime, return the last time the event started before the query time, the end time associated with
//  that start time (it may be before or after the query time depending on if the event is active), and the next time the event will
//  start. 
//		-Returns 0 for LastStart if the event hasn't happened yet.
//		-Returns 0 for EndofLastStart if the event hasn't happened yet.
//		-Returns 0xffffffff for EndofLastStart if it's a one-time event that has started and that has no end.
//		-Returns 0xffffffff for NextStart if the event will never start again after the QueryTime 
//		-Returns 0 for NextStart if it is a one-time unending event that has already started

void ShardEventTiming_GetUsefulTimes(ShardEventTimingDef *pShardEventTimingDef, U32 uQueryTime, U32 *puLastStart, U32 *puEndOfLastStart, U32 *puNextStart)
{
	int i;

	U32 uLastStart=0;
	U32 uEndOfLastStart=0;
	U32 uNextStart = 0xffffffff;

	if (pShardEventTimingDef)
	{
		for(i=0;i<eaSize(&pShardEventTimingDef->ppTimingEntries);i++)
		{
			ShardEventTimingEntry* pShardEventTimingEntry = pShardEventTimingDef->ppTimingEntries[i];
		
			if(uQueryTime < pShardEventTimingEntry->uDateStart)
			{
				 // We're before the start date
				
				U32 uFirstStartTime;
				
				if (pShardEventTimingEntry->uTimeRepeat)
				{
					// Random may make uDateStart not the actual start time. Get the start time within the first cycle.
					uFirstStartTime = ShardEventTiming_GetTimingDefStartTimeFromCycle(pShardEventTimingDef, i, 0);
				}
				else
				{
					// Otherwise use the start date
					uFirstStartTime = pShardEventTimingEntry->uDateStart;
				}

				// We need to check against end time because a random event could be pushed past the end time, in which case we will never start
				if (uFirstStartTime < pShardEventTimingEntry->uTimeEnd)
				{
					uNextStart = MIN(uNextStart,uFirstStartTime);
				}
			}
			else if (pShardEventTimingEntry->uTimeEnd && uQueryTime > pShardEventTimingEntry->uTimeEnd)
			{
				// We're after the end date				
				if (pShardEventTimingEntry->uTimeRepeat)
				{
					// We want to use the last start time that was before the timeEnd.
					U32 uStartTimeOfLastCycle=0;

					U32 uCycleIdx = (pShardEventTimingEntry->uTimeEnd-1 - pShardEventTimingEntry->uDateStart) / pShardEventTimingEntry->uTimeRepeat;
					U32 uStartTimeInQueryTimesCycle = ShardEventTiming_GetTimingDefStartTimeFromCycle(pShardEventTimingDef, i, uCycleIdx);

					if (uStartTimeInQueryTimesCycle > pShardEventTimingEntry->uTimeEnd-1)
					{
						// Random has pushed this cycle's occurrence past the end time
						if (uCycleIdx==0)
						{
							// this is the first cycle. This event will never occur.
						}
						else
						{
							// Back us up one cycle and try again
						
							uCycleIdx = uCycleIdx - 1;
							uStartTimeOfLastCycle = ShardEventTiming_GetTimingDefStartTimeFromCycle(pShardEventTimingDef, i, uCycleIdx);
						}
					}
					if (uStartTimeOfLastCycle > 0)
					{
						uLastStart = MAX(uLastStart,uStartTimeOfLastCycle);
						uEndOfLastStart = MAX(uEndOfLastStart,MIN(uStartTimeOfLastCycle + pShardEventTimingEntry->uTimeDuration, pShardEventTimingEntry->uTimeEnd));
					}
				}
				else
				{
					uLastStart = MAX(uLastStart,pShardEventTimingEntry->uDateStart);
					uEndOfLastStart = MAX(uEndOfLastStart,pShardEventTimingEntry->uTimeEnd);
				}
			}
			else if (pShardEventTimingEntry->uTimeRepeat)
			{
				// Repeating event that we're in the middle of.
				// We know we are after the event start time, so these calls should be safe with regards to negative numbers
				
				U32 uStartTimeOfCurrentCycle = 0;	// We use MAX to set these. So 0 will cause no change
				U32 uEndTimeOfCurrentCycle = 0;		// We use MAX to set these. So 0 will cause no change
				U32 uStartTimeOfNextCycle = 0xffffffff;
				
				// We want to find the cycle that holds the last started time. It may not be the cycle that queryTime is in
				//  since random may have made the start time in that cycle later than we would expect

				U32 uCycleIdx = (uQueryTime - pShardEventTimingEntry->uDateStart) / pShardEventTimingEntry->uTimeRepeat;

				U32 uStartTimeInQueryTimesCycle = ShardEventTiming_GetTimingDefStartTimeFromCycle(pShardEventTimingDef, i, uCycleIdx);

				if (uStartTimeInQueryTimesCycle > uQueryTime)
				{
					// Random has pushed this cycle's occurrence past the query time.
					if (uCycleIdx==0)
					{
						// Worse, this is the first cycle. Treat this as if we are before the start date.
						uStartTimeOfNextCycle = uStartTimeInQueryTimesCycle;
					}
					else
					{
						// The start time in this cycle is actually the next starting time
						uStartTimeOfNextCycle = uStartTimeInQueryTimesCycle;
						
						// Back us up one cycle and try again
						
						uCycleIdx = uCycleIdx - 1;
						uStartTimeOfCurrentCycle = ShardEventTiming_GetTimingDefStartTimeFromCycle(pShardEventTimingDef, i, uCycleIdx);
						uEndTimeOfCurrentCycle = uStartTimeOfCurrentCycle + pShardEventTimingEntry->uTimeDuration;
					}
				}
				else
				{
					uStartTimeOfCurrentCycle = uStartTimeInQueryTimesCycle;
					uEndTimeOfCurrentCycle = uStartTimeOfCurrentCycle + pShardEventTimingEntry->uTimeDuration;

					// Advance one cycle
					uCycleIdx = uCycleIdx + 1;
					uStartTimeOfNextCycle = ShardEventTiming_GetTimingDefStartTimeFromCycle(pShardEventTimingDef, i, uCycleIdx);
				}

				if (pShardEventTimingEntry->uTimeEnd!=0)
				{
					uEndTimeOfCurrentCycle = MIN(pShardEventTimingEntry->uTimeEnd, uEndTimeOfCurrentCycle);
				}
			
				uLastStart = MAX(uLastStart,uStartTimeOfCurrentCycle);
				uEndOfLastStart = MAX(uEndOfLastStart, uEndTimeOfCurrentCycle);

				if (uStartTimeOfNextCycle < pShardEventTimingEntry->uTimeEnd || pShardEventTimingEntry->uTimeEnd==0)
				{
					uNextStart = MIN(uNextStart,uStartTimeOfNextCycle);
				}
			}
			else
			{
				// One-time event that we're in the middle of.

				uLastStart = MAX(uLastStart,pShardEventTimingEntry->uDateStart);
				if (pShardEventTimingEntry->uTimeEnd==0)
				{
					uEndOfLastStart = 0xffffffff;
					uNextStart = 0;  // Set to zero so we don't sneak in with another timedef and change nextstart
				}
				else
				{
					uEndOfLastStart = MAX(uEndOfLastStart, pShardEventTimingEntry->uTimeEnd);
				}
			}
		}
	}

	if (puLastStart!=NULL)
	{
		*puLastStart=uLastStart;
	}
	if (puEndOfLastStart!=NULL)
	{
		*puEndOfLastStart=uEndOfLastStart;
	}
	if (puNextStart!=NULL)
	{
		*puNextStart=uNextStart;
	}
}


bool ShardEventTiming_EntryShouldBeOn(ShardEventTimingDef* pShardEventTimingDef, int iTimingIndex, U32 uQueryTime)
{
	ShardEventTimingEntry *pTimingEntry;

	if(iTimingIndex <0 || iTimingIndex >= eaSize(&pShardEventTimingDef->ppTimingEntries))
	{
		return(false);
	}

	pTimingEntry = pShardEventTimingDef->ppTimingEntries[iTimingIndex];

	// We're before the start date
	if(uQueryTime < pTimingEntry->uDateStart)
	{
		return(false);
	}

	// We're after the end date
	if(pTimingEntry->uTimeEnd && uQueryTime >= pTimingEntry->uTimeEnd)
	{
		return(false);
	}

	// Check for repeating events
	if(pTimingEntry->uTimeRepeat)
	{
		// We know we are after the event start time, so this calls should be safe.

		U32 uCycleIdx = (uQueryTime - pTimingEntry->uDateStart) / pTimingEntry->uTimeRepeat;
		U32 uStartTime = ShardEventTiming_GetTimingDefStartTimeFromCycle(pShardEventTimingDef, iTimingIndex, uCycleIdx);

		// If the current time is greater than the start time plus active duration, we should not be on.
		// Now that we have random we also need to check the start time
		
		if (uQueryTime < uStartTime || uQueryTime >= uStartTime + pTimingEntry->uTimeDuration)
		{
			return(false);
		}
	}

	return(true);
}


bool ShardEventTiming_DefShouldBeOn(ShardEventTimingDef* pShardEventTimingDef, int *iIndexOut, U32 uQueryTime)
{
	int i;

	if(!pShardEventTimingDef)
		return false;
	 
	for(i=0;i<eaSize(&pShardEventTimingDef->ppTimingEntries);i++)
	{
		if(ShardEventTiming_EntryShouldBeOn(pShardEventTimingDef,i,uQueryTime))
		{
			if (iIndexOut)
			{
				*iIndexOut=i;
			}
			return true;
		}
	}

	return false;
}

#include "autogen/ShardEventTimingCommon_h_ast.c"
