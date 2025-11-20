#include "ChoiceTable.h"
#include"ChoiceTable_common.h"

#include"error.h"
#include"ExpressionFunc.h"
#include"rand.h"
#include"HashFunctions.h"
#include"GlobalTypes.h"
#include"StringCache.h"
#include"timing.h"

#include "ChoiceTable_C_ast.h"

AUTO_STRUCT;
typedef struct DailyRandomTable
{
	U32 iSeed;				AST(NAME(DailyTableSeed))

		// The seed for the mersenne table is iStartDay + iKey
		S32 iStartInterval;

	U32 iTimeIntervalInSecs;
	S32 iRandomRange;
	S32 iValuesPerInterval;
	S32 iIntervals;			AST(NAME(DailyTableIntervals))

		// time this table was last used
		U32 uLastUsed;			AST(NAME(DailyTableLastUsed))

		// table of random values
		S32 *eaiTable;			AST(NAME(DailyTableTable))

		S32 iValuesFilled;		NO_AST

} DailyRandomTable;

#define CHOICE_MAX_RECURSE_DEPTH 32

SA_RET_NN_VALID static WorldVariable* choice_ChooseValueInternal(
		SA_PARAM_NN_VALID ChoiceTable* table, const char* valueName, SA_PARAM_NN_VALID U32* seed, U32 uiTimeSecsSince2000,
		float weight, int entryIndex, int recurseDepth );
static float choice_TotalWeight( SA_PARAM_OP_VALID ChoiceTable* table );

// Data saved to speed up daily random value function
static int s_iSavedDayNumber = 0;
static StashTable s_DailyNumStashTable = NULL;

// This is a 12 hour offset required to make the time intervals start at 4am Pacific Time
#define TIME_OFFSET   43200

int GetTimeIntervalNumber(int iTimeIntervalInSecs, U32 uiTimeSecsSince2000)
{
	iTimeIntervalInSecs = MAX(1, iTimeIntervalInSecs);
	if (!uiTimeSecsSince2000)
	{
#ifdef GAMECLIENT
		uiTimeSecsSince2000 = timeServerSecondsSince2000(); // Client uses approximate server time
#else
		uiTimeSecsSince2000 = timeSecondsSince2000();
#endif
	}
	return (int)((uiTimeSecsSince2000 - TIME_OFFSET)/iTimeIntervalInSecs);
}

int GetDayNumber(void)
{
	return GetTimeIntervalNumber(SECONDS_PER_DAY, 0);
}

AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetDailyValue);
int exprFuncGetDailyValue(int iNumValues)
{
	// Returns a value from 0 to iMaxValue-1.  For example, 7 returns 0 to 6.
	// This value is guaranteed to be the same for a single day, then increment the
	// next day and so on.
	return GetDayNumber() % iNumValues;
}

AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetDailyRandomValue);
int exprFuncGetDailyRandomValue(int iNumValues, const char *pcKey)
{
	int iValue = 0;
	int iDayNumber;
	U32 uSeed;

	// Returns a random value from 0 to iMaxValue-1.  For example, 7 returns 0 to 6.
	// This value is guaranteed to be the same for a single day for a given key, then 
	// re-randomize the next day and so on.

	if (!s_DailyNumStashTable) {
		s_DailyNumStashTable = stashTableCreateWithStringKeys(10,StashDefault);
	}

	// Clear history when day changes
	iDayNumber = GetDayNumber();
	if (iDayNumber != s_iSavedDayNumber) {
		s_iSavedDayNumber = iDayNumber;
		stashTableClear(s_DailyNumStashTable);
	}

	// If value already stored for this key, use it
	if (!stashFindInt(s_DailyNumStashTable, pcKey, &iValue)) {
		// Value not already stored for this key, so figure it out
		uSeed = hashStringInsensitive(pcKey) ^ iDayNumber;
		iValue = randomIntSeeded(&uSeed, RandType_LCG);
		if (iValue < 0) {
			iValue = -iValue;
		}
		stashAddInt(s_DailyNumStashTable, pcKey, iValue, true);
	}
	
	return iValue % iNumValues;
}

// Data saved to speed up daily random value function
static int s_iPeriod = 0;
static int s_iPeriodNum = 0;
static StashTable s_PeriodicValueStashTable = NULL;

AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetPeriodicValue);
int exprFuncGetPeriodicValue(int iPeriodMinutes, int iNumValues)
{
	// Returns a value from 0 to iMaxValue-1.  For example, 7 returns 0 to 6.
	// This value is guaranteed to be the same for a single day, then increment the
	// next day and so on.
	return GetTimeIntervalNumber(iPeriodMinutes*60, 0) % iNumValues;
}

// Returns a random value from 0 to iMaxValue-1.  For example, 7 returns 0 to 6.
// This value is guaranteed to be the same for every iPeriodMinutes for a given key, then 
// re-randomize the next day and so on.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetPeriodicRandomValue);
int exprFuncGetPeriodicRandomValue(int iPeriodMinutes, int iNumValues, const char *pcKey)
{
	int iValue = 0;
	int iPeriodNumber;
	U32 uSeed;

	if (!s_PeriodicValueStashTable) {
		s_PeriodicValueStashTable = stashTableCreateWithStringKeys(10,StashDefault);
	}

	// Clear history when day changes
	iPeriodNumber = GetTimeIntervalNumber(iPeriodMinutes*60, 0);
	if(s_iPeriod != iPeriodMinutes || s_iPeriodNum != iPeriodNumber)
	{
		s_iPeriod = iPeriodMinutes;
		s_iPeriodNum = iPeriodNumber;
		stashTableClear(s_PeriodicValueStashTable);
	}

	// If value already stored for this key, use it
	if (!stashFindInt(s_PeriodicValueStashTable, pcKey, &iValue)) {
		// Value not already stored for this key, so figure it out
		uSeed = hashStringInsensitive(pcKey) ^ iPeriodNumber;
		iValue = randomIntSeeded(&uSeed, RandType_LCG);
		if (iValue < 0) {
			iValue = -iValue;
		}
		stashAddInt(s_PeriodicValueStashTable, pcKey, iValue, true);
	}

	return iValue % iNumValues;
}

// Daily table code
// used for returning random values based on the day / block. These are non-repeating values.
// The core creates a key from table size, random range and cycle size.
static StashTable s_DailyTableStashTable = NULL;

// error e string
static char * s_DailyRandomError = NULL;

#define MAX_DAILY_TABLES 4
#define DAILY_RANDOM_FAILURE 999999
#define DAILY_RANDOM_RETRIES 3

static void DailyTableError(DailyRandomTable *pDailyTable)
{
	if(pDailyTable)
	{
		estrPrintf(&s_DailyRandomError, "Daily Random Table error: Seed %d, Range %d, Per interval %d, Intervals %d, Interval secs %d.", 
			pDailyTable->iSeed, pDailyTable->iRandomRange, pDailyTable->iValuesPerInterval, pDailyTable->iIntervals, pDailyTable->iTimeIntervalInSecs);
	}
	else
	{
		estrPrintf(&s_DailyRandomError, "Daily Random Table error: Invalid table.");
	}
	
}

static void DailyRandomTableAddDay(DailyRandomTable *pDailyTable, MersenneTable *pmTable,S32 **eaiCurrentValues)
{
	S32 iFilled = 0;
	
	if(!pDailyTable|| !pmTable || !eaiCurrentValues)
	{
		return;
	}
	
	while(iFilled < pDailyTable->iValuesPerInterval)
	{
		S32 iRandomIdx, iCurSize, iRandom, iRollNumber;
		S32 iRetries = DAILY_RANDOM_RETRIES;
	
		if(eaiSize(eaiCurrentValues) < 1)	
		{
			S32 j;
			for(j = 0; j < pDailyTable->iRandomRange; ++j)
			{
				eaiPush(eaiCurrentValues, j);
			}
		}
		
		iRollNumber = 0;
		iRandom = 0;
		iRandomIdx = -1;
		while(iRollNumber < iRetries)		
		{
			bool bOk = true;
			S32 k;
			// get a random table spot
			iCurSize = eaiSize(eaiCurrentValues);
			iRandomIdx = randomMersenneIntRange(pmTable, 0, iCurSize - 1);
			iRandom = (*eaiCurrentValues)[iRandomIdx];

			// test random number to previous days values
			for(k = 0; k < pDailyTable->iValuesPerInterval; ++k)		
			{
				S32 iLastIdx;
				
				iLastIdx = pDailyTable->iValuesFilled + k;
				if(iLastIdx < eaiSize(&pDailyTable->eaiTable))
				{
					if(iRandom == pDailyTable->eaiTable[iLastIdx])
					{
						// already have this number
						bOk = false;
						++iRollNumber;
						break;
					}
				}
				else
				{
					// Not relay a failure as a new table will always hit this
					iRollNumber = DAILY_RANDOM_FAILURE;		
					break;
				}
			}
			
			if(bOk)
			{
				break;
			}
		}
		
		// use this random number
		eaiPush(&pDailyTable->eaiTable, iRandom);
		++iFilled;
		if(iRandomIdx >= 0)
		{
			eaiRemove(eaiCurrentValues, iRandomIdx);
		}
		
	}

	// record number of values filled	
	pDailyTable->iValuesFilled += iFilled;
}


static S32 GetDailyIndex(DailyRandomTable *pDailyTable, U32 uiTimeSecsSince2000)
{
	S32 iIntervalIndex;
	S32 iInterval = GetTimeIntervalNumber(pDailyTable->iTimeIntervalInSecs, uiTimeSecsSince2000);

	if(iInterval >= pDailyTable->iStartInterval + pDailyTable->iIntervals)
	{
		// reset start day
		S32 iNewStartBlock = iInterval / pDailyTable->iIntervals;
		S32 iNewStartInterval = iNewStartBlock * pDailyTable->iIntervals;
		MersenneTable *pmTable = NULL;
		S32 *eaiCurrentValues = NULL;
		S32 i;
		
		pDailyTable->iStartInterval = iNewStartInterval;
		
		// rebuild table
		eaiClear(&pDailyTable->eaiTable);
		pDailyTable->iValuesFilled = 0;
		
		// reseed
		pmTable = mersenneTableCreate(pDailyTable->iSeed + iNewStartInterval);
		
		// build our tables
		for(i = 0; i < pDailyTable->iIntervals; ++i)		
		{
			DailyRandomTableAddDay(pDailyTable, pmTable, &eaiCurrentValues);
		}
		
		// free mersenne table
		mersenneTableFree(pmTable);
	}

	iIntervalIndex = (iInterval - pDailyTable->iStartInterval) * pDailyTable->iValuesPerInterval;

	return iIntervalIndex;

}

static S32 GetDailyRandom(S32 iIndex, DailyRandomTable *pDailyTable, U32 uiTimeSecsSince2000)
{
	S32 iOutIndex;

	iOutIndex = GetDailyIndex(pDailyTable, uiTimeSecsSince2000) + iIndex; 

	if(iOutIndex >= 0 && iOutIndex < eaiSize(&pDailyTable->eaiTable))
	{
		// record use
		pDailyTable->uLastUsed = timeServerSecondsSince2000();

		// return value
		return pDailyTable->eaiTable[iOutIndex];
	}

	DailyTableError(pDailyTable);
	estrConcatf(&s_DailyRandomError, " Outindex %d is out of range of table size %d.", iOutIndex, eaiSize(&pDailyTable->eaiTable));
	Errorf("%s", s_DailyRandomError);

	return 0;
}

S32 GetDailyRandomTableValue(S32 iRandomRange, S32 iIntervalIndex, S32 iValuesPerInterval, S32 iIntervals, U32 iSupplementalSeed, U32 iTimeIntervalInSecs, U32 uiTimeSecsSince2000)
{
	S32 iRetVal = 0;
	
	if(iRandomRange < 1 || iValuesPerInterval < 1 || iIntervals < 1 || iIntervalIndex < 0 || iIntervalIndex >= iValuesPerInterval ||
		iRandomRange > MAX_DAILY_TABLE_RANDOM_RANGE || iValuesPerInterval > MAX_DAILY_TABLE_VALUES_PER_INTERVAL || iIntervals > MAX_DAILY_TABLE_INTERVALS)
	{
	
		estrPrintf(&s_DailyRandomError, "Daily Random Table Newtable error: Range %d, Per day %d, Days %d, Index %d, Interval secs %d.", 
			iRandomRange, iValuesPerInterval, iIntervals, iValuesPerInterval, iTimeIntervalInSecs);
			
		if(iRandomRange < 1 || iValuesPerInterval < 1 || iIntervals < 1)
		{
			estrConcatf(&s_DailyRandomError, " Range, Values per interval and intervals must be greater than zero.");
		}
		
		if(iIntervalIndex < 0 || iIntervalIndex >= iValuesPerInterval)
		{
			estrConcatf(&s_DailyRandomError, " Index must be greater >= 0 and < %d.", iValuesPerInterval);
		}
		
		if(iRandomRange > MAX_DAILY_TABLE_RANDOM_RANGE || iValuesPerInterval > MAX_DAILY_TABLE_VALUES_PER_INTERVAL || iIntervals > MAX_DAILY_TABLE_INTERVALS)
		{
			estrConcatf(&s_DailyRandomError, " Range must be < %d, Per interval must be < %d and Intervals must be < %d",MAX_DAILY_TABLE_RANDOM_RANGE, MAX_DAILY_TABLE_VALUES_PER_INTERVAL, MAX_DAILY_TABLE_INTERVALS);
		}
		
		Errorf("%s", s_DailyRandomError);
		
		return 0;
	}

	{
		S32 iTableSize = iValuesPerInterval * iIntervals;
		DailyRandomTable *pDailyTable;
		char *pchTableKey = NULL;

		// create string key
		pchTableKey = STACK_SPRINTF("%i,%i,%i,%i,%i", iSupplementalSeed, iRandomRange, iValuesPerInterval, iIntervals, iTimeIntervalInSecs);

		// do we have this key?
		if(stashFindPointer(s_DailyTableStashTable, pchTableKey, &pDailyTable))
		{
			// return value for our index
			iRetVal = GetDailyRandom(iIntervalIndex, pDailyTable, uiTimeSecsSince2000);
		}
		else
		{
			// if too large remove oldest key
			if(s_DailyTableStashTable && stashGetCount(s_DailyTableStashTable) >= MAX_DAILY_TABLES)
			{
				StashTableIterator iter;
				StashElement elem;
				DailyRandomTable *pOldTab = NULL;
				U32 iOldTime = timeSecondsSince2000() + 1;
				const char *pchOldKey = NULL;
				
				stashGetIterator(s_DailyTableStashTable, &iter);
				while(stashGetNextElement(&iter, &elem))
				{
					DailyRandomTable *pTab = stashElementGetPointer(elem);
					if(pTab->uLastUsed < iOldTime)
					{
						iOldTime = pTab->uLastUsed;
						pOldTab = pTab;
						pchOldKey = stashElementGetStringKey(elem);
					}
				}
				
				if(pOldTab)
				{
					stashRemovePointer(s_DailyTableStashTable, pchOldKey, NULL);
				}
			}
			
			// create new table
			pDailyTable = StructCreate(parse_DailyRandomTable);
			pDailyTable->iSeed = hashString(pchTableKey, true);
			pDailyTable->iIntervals = iIntervals;
			pDailyTable->iRandomRange = iRandomRange;
			pDailyTable->iValuesPerInterval = iValuesPerInterval;
			pDailyTable->iTimeIntervalInSecs = iTimeIntervalInSecs;
			
			if(!s_DailyTableStashTable)
				s_DailyTableStashTable = stashTableCreateWithStringKeys(MAX_DAILY_TABLES * 2, StashDeepCopyKeys_NeverRelease);

			// add table to stash
			stashAddPointer(s_DailyTableStashTable, pchTableKey, pDailyTable, false);

			// return value for our index
			iRetVal = GetDailyRandom(iIntervalIndex, pDailyTable, uiTimeSecsSince2000);

		}
	}
	return iRetVal;
}

// Return a number from 0 to iRandomRange -1, with an unique number iValuesPerDay not repeating for iDays
AUTO_EXPR_FUNC(util) ACMD_NAME(GetDailyTableRandomValue);
int exprFuncGetDailyTableRandomValue(S32 iRandomRange, S32 iDailyIndex, S32 iValuesPerDay, S32 iDays)
{
	S32 iRandom = GetDailyRandomTableValue(iRandomRange, iDailyIndex, iValuesPerDay, iDays, 0, SECONDS_PER_DAY, 0);
	return iRandom;
}

AUTO_COMMAND;
S32 DailyRT_Get(S32 iRandomRange, S32 iDailyIndex, S32 iValuesPerDay, S32 iDays)
{
	S32 iRandom = GetDailyRandomTableValue(iRandomRange, iDailyIndex, iValuesPerDay, iDays, 0, SECONDS_PER_DAY, 0);
	return iRandom;
	
}

/// Boil TABLE down to a single actual value.
///
/// For a given SEED, this function will always return the same value
WorldVariable* choice_ChooseValue( ChoiceTable* table, const char* name, int timedRandomIndex, U32 seed, U32 uiTimeSecsSince2000)
{
	float totalWeight = choice_TotalWeight( table );
	name = allocAddString( name );

	if (table->eSelectType == CST_TimedRandom)
	{
		// TODO (JDJ): we are ignoring the seed for timed random for now; this means
		// any timed random choice table will always return the same values no matter where it's used;
		// if we want, we can add a "context seed" parameter to differentiate uses of the same choice table in multiple
		// places; for now, the table name hash should suffice in differentiation different tables with similar
		// setups
		int index = GetDailyRandomTableValue(choice_TimedRandomRange(table), timedRandomIndex, choice_TimedRandomValuesPerInterval(table), choice_TimedRandomNumIntervals(table), hashString(table->pchName, true), (int) table->eTimeInterval, uiTimeSecsSince2000);
		return choice_ChooseValueInternal(table, name, &seed, uiTimeSecsSince2000, -1, index, 0);
	}
	else
		return choice_ChooseValueInternal(table, name, &seed, uiTimeSecsSince2000, randomPositiveF32Seeded(&seed, RandType_LCG) * totalWeight, -1, 0);
}

/// Boil TABLE down to a single actual value.
///
/// Like CHOICE_CHOOSE-VALUE, but this chooses a random seed
WorldVariable* choice_ChooseRandomValue( ChoiceTable* table, const char* name, int timedRandomIndex)
{
	name = allocAddString( name );
	
	return choice_ChooseValue( table, name, timedRandomIndex, randomInt(), 0);
}

/// Boil TABLE down to a single actual value.
WorldVariable* choice_ChooseValueInternal(
		ChoiceTable* table, const char* valueName, U32* seed, U32 uiTimeSecsSince2000, float weight, int entryIndex, int recurseDepth )
{
	int it;

	if( recurseDepth > CHOICE_MAX_RECURSE_DEPTH ) {
		ErrorFilenamef( table->pchFileName, "Choice Table appears to be in a recursive loop." );
		return &choice_defaultValue;
	}
	assert(weight >= 0 || entryIndex >= 0);
	{
		for( it = 0; it < eaSize( &table->eaEntry ); ++it ) {
			ChoiceEntry* entry = table->eaEntry[ it ];

			switch( entry->eType ) {
				case CET_Value:
					if((weight >= 0 && weight < entry->fWeight) || entryIndex == 0) {
						int valueIndex = choice_ValueIndex( table, valueName );
						ChoiceValue* value;
						
						if( valueIndex < 0 ) {
							Errorf( "Request made for Choice Table '%s', value '%s', but that value does not exist.",
									table->pchName, valueName );
							return &choice_defaultValue;
						}
						value = entry->eaValues[ valueIndex ];

						switch( value->eType ) {
							case CVT_Value:
								return &value->value;

							case CVT_Choice: {
								ChoiceTable* valueTable = GET_REF( value->hChoiceTable );

								if( !valueTable ) {
									ErrorFilenamef( table->pchFileName, "Entry: #%d, Value: %s -- Value does not have a choice table.",
													it + 1, table->eaDefs[ valueIndex ]->pchName );
									return &choice_defaultValue;
								} else {
									float valueTotalWeight = choice_TotalWeight( valueTable );
									if (valueTable->eSelectType == CST_TimedRandom) {
										int index = GetDailyRandomTableValue(choice_TimedRandomRange(valueTable), value->iChoiceIndex, choice_TimedRandomValuesPerInterval(valueTable), choice_TimedRandomNumIntervals(valueTable), 0, (int) valueTable->eTimeInterval, uiTimeSecsSince2000);
										return choice_ChooseValueInternal(table, value->pchChoiceName, seed, uiTimeSecsSince2000, -1, index, recurseDepth + 1);
									}
									else {
										return choice_ChooseValueInternal( valueTable, value->pchChoiceName, seed, uiTimeSecsSince2000,
											randomPositiveF32Seeded(seed, RandType_LCG) * valueTotalWeight, -1, recurseDepth + 1 );
									}
								}
							}
						}
					}
					if (weight >= 0)
						weight -= entry->fWeight;
					else if (entryIndex >= 0)
						entryIndex--;
				xcase CET_Include: {
					ChoiceTable* includeTable = GET_REF( entry->hChoiceTable );
					float includeWeight = choice_TotalWeight( includeTable );
					int includeEntryCount = choice_TotalEntries(includeTable);
					if ((weight > 0 && weight < includeWeight) ||
						(entryIndex >= 0 && entryIndex < includeEntryCount)) {
						return choice_ChooseValueInternal( includeTable, valueName, seed, uiTimeSecsSince2000, weight, entryIndex, recurseDepth + 1 );
					}
					if (weight >= 0)
						weight -= includeWeight;
					else if (entryIndex >= 0)
						entryIndex -= includeEntryCount;
				}
			}
		}
	}

	return &choice_defaultValue;
}

/// Calcualate the total weight of all entries in table.
float choice_TotalWeight( ChoiceTable* table )
{
	float accum = 0;

	if( table ) {
		int it;
		for( it = 0; it != eaSize( &table->eaEntry ); ++it ) {
			ChoiceEntry* entry = table->eaEntry[ it ];

			switch( entry->eType ) {
				case CET_Value:
					accum += entry->fWeight;

				xcase CET_Include:
					accum += choice_TotalWeight( GET_REF( entry->hChoiceTable ));
			}
		}
	}

	return accum;
}

#include "ChoiceTable_c_ast.c"
