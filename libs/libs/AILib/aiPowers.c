#include "aiPowers.h"
#include "aiPowers_h_ast.h"

#include "aiBrawlerCombat.h"
#include "aiConfig.h"
#include "aiDebug.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiMultiTickAction.h"
#include "aiStructCommon.h"
#include "aiTeam.h"

#include "Character.h"
#include "CharacterClass.h"
#include "Character_combat.h"
#include "Character_target.h"
#include "Entity_h_ast.h"
#include "EntityMovementTactical.h"
#include "EntitySavedData.h"
#include "entCritter.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "gslMapState.h"
#include "gslPetCommand.h"
#include "MemoryPool.h"
#include "Player.h"
#include "PowerActivation.h"
#include "PowersMovement.h"
#include "PowerModes.h"
#include "rand.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "StructMod.h"
#include "inventoryCommon.h"
#include "TextParserSimpleInheritance.h"

#include "aiStruct_h_ast.h"
#include "aiMovement_h_ast.h"
#include "AILib_autogen_QueuedFuncs.h"
#include "AttribMod_h_ast.h"

#include "entCritter_h_ast.h"

AIAllPowerConfigLists allPowerConfigs = {0};

static void aiPowersExprVarsAdd(SA_PARAM_NN_VALID ExprContext* context, Entity* target, AIPowerInfo* powInfo, AttribModDef *modDef);
static void aiPowersExprVarsRemove(SA_PARAM_NN_VALID ExprContext* context);
static int aiCheckPowersRequiresExpr(Entity* e, AIVarsBase* aib, Entity* target, AIPowerInfo* powInfo, Expression* expr, const char* blamefile);

void aiPowersCureRequiresExprVarsAdd(Entity* e, AIVarsBase* aib, ExprContext* context, AttribMod *pMod, AttribModDef *pModDef);
void aiPowersCureRequiresExprVarsRemove(Entity* e, AIVarsBase* aib, ExprContext* context);
static Entity* aiPowersGetOverrideTarget(Entity* e, AIVarsBase* aib, Entity* target, AIPowerInfo* powInfo, AIPowerConfig* powConfig);;

typedef struct AIWeightedPowerRange {
	F32 min;
	F32 max;
	F32 weight;
} AIWeightedPowerRange;

static int s_bAIPowersDebug = false;
AUTO_CMD_INT(s_bAIPowersDebug, aiPowersDebug);

#define AIPowersDebugError(format, ...) if(s_bAIPowersDebug) printf( "AIPowers [ERROR]: " format "\n",##__VA_ARGS__)
#define AIPowersDebugWarning(format, ...) if(s_bAIPowersDebug) printf( "AIPowers [Warning]: " format "\n",##__VA_ARGS__)
#define AIPowersDebug(format, ...) if(s_bAIPowersDebug) printf( "AIPowers: " format "\n",##__VA_ARGS__)
#define AIPowersDebugSelected(entRef, format, ...) if (entRef == aiDebugEntRef) AIPowersDebug(format, ##__VA_ARGS__)
#define AIPowersDebugSelectedPowDef(entRef, hPowDef, format, ...)\
if ((entRef) == aiDebugEntRef && s_bAIPowersDebug){\
	PowerDef *powDef = GET_REF((hPowDef));\
	printf( "AIPowers: (%s)" format "\n", powDef->pchName, ##__VA_ARGS__);}


int aiPowersGenerateConfigExpression(Expression* expr)
{
	ExprContext* context = aiGetStaticCheckExprContext();
	int success;

	aiPowersExprVarsAdd(context, NULL, NULL, NULL);
	success = exprGenerate(expr, context);
	aiPowersExprVarsRemove(context);

	return success;
}

static int aiPowerConfigListProcess(AIPowerConfigList* configList)
{
	int i, j;
	F32 totalWeight = 0;
	int success = true;
	ExprContext* context = aiGetStaticCheckExprContext();

	for(i = eaSize(&configList->entries)-1; i >= 0; i--)
	{
		AIPowerConfig* entry = configList->entries[i];

		totalWeight += configList->entries[i]->absWeight;

		if(entry->weightModifier)
			aiPowersGenerateConfigExpression(entry->weightModifier);

		if(entry->aiRequires)
			aiPowersGenerateConfigExpression(entry->aiRequires);

		if(entry->aiEndCondition)
			aiPowersGenerateConfigExpression(entry->aiEndCondition);

		if(entry->chainRequires)
			aiPowersGenerateConfigExpression(entry->chainRequires);

		if(entry->targetOverride)
			aiPowersGenerateConfigExpression(entry->targetOverride);

		if(entry->cureRequires)
			aiPowersGenerateConfigExpression(entry->cureRequires);

		if(entry->chainTarget)
		{
			int found = false;
			for(j = eaSize(&configList->entries)-1; j >= 0; j--)
			{
				PowerDef* powDef = GET_REF(configList->entries[j]->powDef);
				if(powDef && !stricmp(entry->chainTarget, powDef->pchName))
				{
					found = true;
					break;
				}
			}
			if(!found)
			{
				PowerDef* powDef = GET_REF(entry->powDef);
				ErrorFilenamef(configList->filename, "Chaintarget %s referenced from %s was not found",
					entry->chainTarget, powDef ? powDef->pchName : "<INVALID POWER>");
			}
		}
	}

	return success;
}

AUTO_FIXUPFUNC;
TextParserResult fixupAIPowerConfigList(AIPowerConfigList* configList, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		return aiPowerConfigListProcess(configList);
	}

	return 1;
}



static void aiPowerConfigRegisterReferences(AIPowerConfigList* curList)
{
	int i;
	char* refStr = NULL;

	estrStackCreate(&refStr);

	RefSystem_AddReferent("AIPowerConfigList",  curList->name,
		 curList);

	for(i = eaSize(&curList->entries)-1; i >= 0; i--)
	{
		AIPowerConfig* entry = curList->entries[i];
		PowerDef* curDef = (PowerDef*)GET_REF(entry->powDef);
		/*
		// This should be handled by REQUIRED NON_NULL_REF in the parsetable now
		if(!curDef)
		{
			ErrorFilenamef(curList->filename, "Unknown power %s in PowerConfigList %s",
				REF_STRING_FROM_HANDLE(entry->powDef), curList->name);
			break;
		}
		*/
		if(curDef)
		{
			estrPrintf(&refStr, "%s:%s", curList->name, curDef->pchName);
			RefSystem_AddReferent("AIPowerConfig",  refStr,  entry);
		}
	}

	estrDestroy(&refStr);
}

static void aiPowerConfigUnregisterReferences(AIPowerConfigList* curList)
{
	int i;
	char* refStr = NULL;

	estrStackCreate(&refStr);


	RefSystem_RemoveReferent(RefSystem_ReferentFromString("AIPowerConfigList", curList->name), false);

	for(i = eaSize(&curList->entries)-1; i >= 0; i--)
	{
		AIPowerConfig* entry = curList->entries[i];
		PowerDef* curDef = (PowerDef*)GET_REF(entry->powDef);
		if(curDef) // I guess if the power is gone you're just screwed trying to remove this reference...
		{
			estrPrintf(&refStr, "%s:%s", curList->name, curDef->pchName);
			RefSystem_RemoveReferent(RefSystem_ReferentFromString("AIPowerConfig", refStr), false);
		}
	}

	estrDestroy(&refStr);
}

static int aiPowerConfigReloadCallback(void *newStruct, void *oldStruct, ParseTable *pTPI, eParseReloadCallbackType eType)
{
	AIPowerConfigList* oldConfigList = (AIPowerConfigList*) oldStruct;
	AIPowerConfigList* newConfigList = (AIPowerConfigList*) newStruct;

	if(eType==eParseReloadCallbackType_Add)
		aiPowerConfigRegisterReferences(newConfigList);
	else if(eType==eParseReloadCallbackType_Delete)
		aiPowerConfigUnregisterReferences(newConfigList);
	else if(eType==eParseReloadCallbackType_Update)
	{
		aiPowerConfigUnregisterReferences(oldConfigList);
		aiPowerConfigRegisterReferences(newConfigList);
	}
	return 1;
}

static void aiPowerConfigReload(const char *path, int UNUSED_when)
{
	loadstart_printf("Reloading PowerConfigLists...");
	fileWaitForExclusiveAccess(path);
	errorLogFileIsBeingReloaded(path);
	if(!ParserReloadFile(path, parse_AIAllPowerConfigLists, &allPowerConfigs, aiPowerConfigReloadCallback, 0))
		Errorf("Error reloading power configs");

	loadend_printf(" done");
}

void aiPowerConfigLoad()
{
	int i;
	static int initted = false;

	if(!initted)
	{
		RefSystem_RegisterSelfDefiningDictionary("AIPowerConfigList", false, parse_AIPowerConfigList, true, true, NULL);
		RefSystem_RegisterSelfDefiningDictionary("AIPowerConfig", false, parse_AIPowerConfig, true, false, NULL);
	}

	loadstart_printf("Loading AI Power Configs.. ");

	//if(!ParserLoadFilesShared(MakeSharedMemoryName("FSMs"), path, ".fsm", binName,
		//PARSER_SERVERONLY, parse_FSMGroup, fsmGroup,
		//NULL, NULL, fsmPreprocess, NULL, fsmPointerPostProcess))
	ParserLoadFiles("ai/PowerConfig", ".apc", "AIPowerConfigs.bin",
		PARSER_OPTIONALFLAG, parse_AIAllPowerConfigLists, &allPowerConfigs);

	for(i = eaSize(&allPowerConfigs.configs)-1; i >= 0; i--)
		aiPowerConfigRegisterReferences(allPowerConfigs.configs[i]);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ai/PowerConfig/*.apc", aiPowerConfigReload);
	loadend_printf("done (%d found).", eaSize(&allPowerConfigs.configs));
}

MP_DEFINE(AIPowerConfig);

static AIPowerConfig* aiPowerConfigCreate(void)
{
	MP_CREATE(AIPowerConfig, 4);
	return MP_ALLOC(AIPowerConfig);
}

static void aiPowerConfigDestroy(AIPowerConfig* entry)
{
	if(entry->weightModifier)
	{
		exprDestroy(entry->weightModifier);
		entry->weightModifier = NULL;
	}

	if(entry->aiRequires)
	{
		exprDestroy(entry->aiRequires);
		entry->aiRequires = NULL;
	}

	if(entry->aiEndCondition)
	{
		exprDestroy(entry->aiEndCondition);
		entry->aiEndCondition = NULL;
	}

	if(entry->chainRequires)
	{
		exprDestroy(entry->chainRequires);
		entry->chainRequires = NULL;
	}

	if(entry->targetOverride)
	{
		exprDestroy(entry->targetOverride);
		entry->targetOverride = NULL;
	}

	if(entry->cureRequires)
	{
		exprDestroy(entry->cureRequires);
		entry->cureRequires = NULL;
	}

	MP_FREE(AIPowerConfig, entry);
}

MP_DEFINE(AIPowerInfo);

static AIPowerInfo* aiPowerInfoCreate(void)
{
	MP_CREATE(AIPowerInfo, 16);
	return MP_ALLOC(AIPowerInfo);
}

static void aiPowerInfoDestroy(AIPowerInfo* info)
{
	if(info->localModifiedPowConfig)
	{
		StructDestroy(parse_AIPowerConfig, info->localModifiedPowConfig);
		info->localModifiedPowConfig = NULL;
	}

	eaDestroyEx(&info->powerConfigMods, structModDestroy);

	MP_FREE(AIPowerInfo, info);
}

MP_DEFINE(AIPowerEntityInfo);

AIPowerEntityInfo* aiPowerEntityInfoCreate(void)
{
	MP_CREATE(AIPowerEntityInfo, 4);
	return MP_ALLOC(AIPowerEntityInfo);
}

static void aiPowerEntityInfoDestroy(AIPowerEntityInfo* powers)
{
	MP_FREE(AIPowerEntityInfo, powers);
}

MP_DEFINE(AIQueuedPower);


// ------------------------------------------------------------------------------------------
MP_DEFINE(AIWeightedPowerRange);

static AIWeightedPowerRange* aiPowerWeightedPowerRangeCreate(void)
{
	MP_CREATE(AIWeightedPowerRange, 16);
	return MP_ALLOC(AIWeightedPowerRange);
}

static void aiPowerWeightedPowerRangeDestroy(AIWeightedPowerRange* range)
{
	MP_FREE(AIWeightedPowerRange, range);
}

// ------------------------------------------------------------------------------------------
static S32 aiPowerSortPoints(const F32 **left, const F32 **right)
{
	return SIGN(**left - **right); // ascending
}

// ------------------------------------------------------------------------------------------
static S32 aiPowerSortPowerRangesByWeightDesc(const AIWeightedPowerRange **left, const AIWeightedPowerRange **right)
{
	return SIGN((*right)->weight - (*left)->weight); // sort descending
}

// ------------------------------------------------------------------------------------------
static S32 aiPowerSortPowerRangesByMin(const AIWeightedPowerRange **left, const AIWeightedPowerRange **right)
{
	return SIGN((*left)->min - (*right)->min);
}

// ------------------------------------------------------------------------------------------
static bool aiPowersDoesRangeIntersect(F32 rMin, F32 rMax, F32 sMin, F32 sMax)
{
	bool result = false;

	if(rMin <= sMin)
	{
		if(rMax > sMin)
		{
			result = true;
		}
	}
	else
	{
		if(sMax > rMin)
		{
			result = true;
		}
	}

	return result;
}

// ------------------------------------------------------------------------------------------
// return true if they intersect
// minOut and maxOut will be set to the min and max of the intersection
static bool aiPowersIntersectRange(F32 rMin, F32 rMax, F32 sMin, F32 sMax, F32 *minOut, F32 *maxOut)
{
	bool result = false;

	if(aiPowersDoesRangeIntersect(rMin, rMax, sMin, sMax))
	{
		// what do they have in common
		if(rMin <= sMin)
		{
			*minOut = sMin;
		}
		else
		{
			*minOut = rMin;
		}

		if(rMax <= sMax)
		{
			*maxOut = rMax;
		}
		else
		{
			*maxOut = sMax;
		}

		result = true;
	}

	return result;
}

// ------------------------------------------------------------------------------------------
static F32 aiPowersShortestDistForRanges(F32 rMin, F32 rMax, F32 sMin, F32 sMax)
{
	F32 result;

	if(sMin >= rMax)
	{
		result = sMin - rMax;
	}
	else
	{
		result = rMin - sMax;
	}

	return result;
}

// ------------------------------------------------------------------------------------------
bool aiPowersIsDefaultRangeValid(AIWeightedPowerRange **usableRanges, F32 minPrefRange, F32 maxPrefRange, F32 *croppedMin, F32 *croppedMax)
{
	F32 contiguousUsableRangeMin;
	F32 contiguousUsableRangeMax;
	F32 intersectMin;
	F32 intersectMax;
	int i;
	bool result = false;
	bool intersects = false;
	bool continuous = true;
	F32 maxIntersection = 0.0;
	AIWeightedPowerRange *rangeWithMaxIntersection = NULL;

	contiguousUsableRangeMin = -1;
	contiguousUsableRangeMax = -1;

	// requires ranges be sorted by minimum
	eaQSort(usableRanges, aiPowerSortPowerRangesByMin);

	// see if any of the 'usable' power ranges intersect with the AIConfig range
	// and combine them into the maximum contiguous range
	for(i = 0; i < eaSize(&usableRanges); i++)
	{
		AIWeightedPowerRange *powerRange = usableRanges[i];

		if(aiPowersIntersectRange(powerRange->min, powerRange->max, minPrefRange, maxPrefRange, &intersectMin, &intersectMax))
		{
			F32 intersectAmount;
			intersects = true;

			intersectAmount = intersectMax - intersectMin;

			if(contiguousUsableRangeMin == -1)
			{
				contiguousUsableRangeMin = intersectMin;
			}

			if(contiguousUsableRangeMax == -1)
			{
				contiguousUsableRangeMax = intersectMax;
			}
			else if(intersectMax > contiguousUsableRangeMax)
			{
				// check for continuity
				if(intersectMin <= contiguousUsableRangeMax)
				{
					// ok to expand
					contiguousUsableRangeMax = intersectMax;
				}
				else
				{
					continuous = false;
				}
			}

			// keep track of maximum intersection for non-continuous case
			if(intersectAmount > maxIntersection)
			{
				maxIntersection = intersectAmount;
				rangeWithMaxIntersection = powerRange;
			}
		}
	}

	if(continuous && intersects)
	{
		result = true;

		*croppedMin = contiguousUsableRangeMin;
		*croppedMax = contiguousUsableRangeMax;
	}
	else if(intersects && rangeWithMaxIntersection != NULL)
	{
		// we have intersection, but it is not continuous
		// so let's choose the range with the highest amount of intersection
		// BUT! it must be at least 5
		if(aiPowersIntersectRange(rangeWithMaxIntersection->min, rangeWithMaxIntersection->max, minPrefRange, maxPrefRange, &intersectMin, &intersectMax))
		{
			if(intersectMax - intersectMin >= 5)
			{
				result = true;

				*croppedMin = intersectMin;
				*croppedMax = intersectMax;
			}
		}
	}

	return result;
}

// ------------------------------------------------------------------------------------------
// returns true if a valid range was determined
bool aiPowersCalcDynamicPreferredRange(Entity *e, AIVarsBase *aib)
{
	bool result = false;

	// make sure the mode is enabled before we calculate -
	// otherwise, it is probably a waste of processing to do so
	if(aib->useDynamicPrefRange)
	{
		AIConfig *config = aiGetConfig(e, aib);
		result = aiPowersCalcDynamicPreferredRangeEx(e, aib, config, &aib->minDynamicPrefRange, &aib->maxDynamicPrefRange);

		if(!result)
		{
			// just use the default range
			aib->minDynamicPrefRange = config->prefMinRange;
			aib->maxDynamicPrefRange = config->prefMaxRange;
		}
	}

	return result;
}

// ------------------------------------------------------------------------------------------
// Gets a list of powers that will be considered for the dynamic preferred range
static void aiPowersGetDPRPowers(Entity *e, AIVarsBase *aib, AIPowerInfo ***usablePowers)
{
	int i;
	AIPowerInfo* powInfo;
	AIPowerConfig* curPowConfig;

	for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
	{
		powInfo = aib->powers->powInfos[i];

		curPowConfig = aiGetPowerConfig(e, aib, powInfo);

		// exclude powers with weight 0 (this implies disabled)
		if(curPowConfig->absWeight <= 0.0)
		{
			continue;
		} 
		
		// test for equal or mis-matched range
		if(curPowConfig->maxDist <= curPowConfig->minDist)
		{
			continue; 
		}

		// do not test if power has AI requires
		if(curPowConfig->aiRequires)
		{
			continue;
		}
		
		// do not add non-attack powers
		if(!powInfo->isAttackPower)
		{
			continue;
		} 
		
		// if the power has a cooldown that exceeds a threshold, do not include it
		if (powInfo->powerTimeRecharge > aiGlobalSettings.dynPrefRange_powerRechargeTimeThreshold)
		{
			continue;
		}


		eaPush(usablePowers, powInfo);
	}
}

// ------------------------------------------------------------------------------------------
bool aiPowersCalcDynamicPreferredRangeEx(Entity *e, AIVarsBase *aib, 
										 AIConfig *config, F32 *rangeMinOut, F32 *rangeMaxOut)
{
	bool result = false;
	AIPowerInfo* powInfo;
	int i;
	int numPoints;
	static F32 **rangePoints = NULL;
	static AIPowerInfo **usablePowers = NULL;
	static AIWeightedPowerRange **usableRanges = NULL;
	F32 intersectMin;
	F32 intersectMax;
	F32 croppedMin;
	F32 croppedMax;

	AIPowerConfig* curPowConfig;
	AIWeightedPowerRange *weightedPowerRange = NULL;

	// get a list of all generally "usable" powers
	eaClear(&usablePowers);
	aiPowersGetDPRPowers(e, aib, &usablePowers);

	// extract usable ranges for analysis
	eaClear(&usableRanges);
	for(i = eaSize(&usablePowers)-1; i >= 0; i--)
	{
		powInfo = usablePowers[i];

		curPowConfig = aiGetPowerConfig(e, aib, powInfo);

		weightedPowerRange = aiPowerWeightedPowerRangeCreate();
		weightedPowerRange->min = curPowConfig->minDist;
		weightedPowerRange->max = curPowConfig->maxDist;

		eaPush(&usableRanges, weightedPowerRange);
	}


	// Check if a power is available for preferred range
	if(aiPowersIsDefaultRangeValid(usableRanges, config->prefMinRange, config->prefMaxRange, &croppedMin, &croppedMax))
	{
		// we have at least one power available at the (AIConfig) preferred range, no need to analyze any further
		*rangeMinOut = croppedMin;
		*rangeMaxOut = croppedMax;

		result = true;
	}
	else
	{
		// AIConfig prefRange does not intersect with any usable powers - look for heaviest range
		eaClear(&rangePoints);

		// Add the min and max distances as (1D) points
		for(i = eaSize(&usablePowers)-1; i >= 0; i--)
		{
			powInfo = usablePowers[i];
			curPowConfig = aiGetPowerConfig(e, aib, powInfo);

			eaPush(&rangePoints, &curPowConfig->minDist);
			eaPush(&rangePoints, &curPowConfig->maxDist);
		}

		// sort the points
		eaQSort(rangePoints, aiPowerSortPoints);

		// step through each segment and total the weight
		// (worst case O(n^2) though n should be very small)
		numPoints = eaSize(&rangePoints);
		if(numPoints > 0)
		{
			static AIWeightedPowerRange **weightedRanges = NULL;
			F32 lastPoint = *(rangePoints[0]);
			F32 point;
			int numRanges;

			eaClear(&weightedRanges);

			for(i = 1; i < numPoints; i++)
			{
				point = *(rangePoints[i]);
				if(lastPoint < point)
				{
					int j;
					F32 weightSum = 0;

					// Accumulate weights based on intersection test
					for(j = eaSize(&usablePowers)-1; j >= 0; j--)
					{
						powInfo = usablePowers[j];
						curPowConfig = aiGetPowerConfig(e, aib, powInfo);

						if( aiPowersDoesRangeIntersect(lastPoint, point, curPowConfig->minDist, curPowConfig->maxDist) )
						{
							weightSum += powInfo->defaultPowConfig->absWeight;
						}
					}

					// store the segmented range and its weight sum
					weightedPowerRange = aiPowerWeightedPowerRangeCreate();
					weightedPowerRange->min = lastPoint;
					weightedPowerRange->max = point;
					weightedPowerRange->weight = weightSum;

					eaPush(&weightedRanges, weightedPowerRange);
				}
				lastPoint = point;
			}

			numRanges = eaSize(&weightedRanges);

			if(numRanges > 0)
			{
				AIWeightedPowerRange *heaviestRange;
				AIWeightedPowerRange *range;

				// sort the ranges by weight (descending)
				eaQSort(weightedRanges, aiPowerSortPowerRangesByWeightDesc);

				heaviestRange = weightedRanges[0]; // default

				// check for 1st place tie-breakers
				// in case of a tie, we will favor range closest to our AIConfig's preferred range
				// the more it overlaps, the higher it ranks
				if(numRanges > 1)
				{
					F32 highestWeight = heaviestRange->weight;
					F32 overlap;
					F32 highestRangeOverlap; // the highest overlap value breaks the tie
					static AIWeightedPowerRange **tiedPowerRanges = NULL;

					eaClear(&tiedPowerRanges);

					// evaluate only those that match the highest weight
					for(i = 1; i < numRanges; i++)
					{
						if(weightedRanges[i]->weight < highestWeight)
						{
							break; // we're done
						}
						eaPush(&tiedPowerRanges, weightedRanges[i]);
					}

					// break the tie
					if(eaSize(&tiedPowerRanges) > 0)
					{
						eaPush(&tiedPowerRanges, heaviestRange);

						highestRangeOverlap = FLT_MIN;
						for(i = eaSize(&tiedPowerRanges)-1; i >= 0; i--)
						{
							range = tiedPowerRanges[i];

							// determine the overlap and/or distance to the AIConfig's preferred range
							if(aiPowersIntersectRange(range->min, range->max, config->prefMinRange, config->prefMaxRange, &intersectMin, &intersectMax))
							{
								overlap = intersectMax - intersectMin;
							}
							else
							{
								// range does not overlap, so determine distance
								// make it negative
								overlap = -1 * aiPowersShortestDistForRanges(range->min, range->max, config->prefMinRange, config->prefMaxRange);
							}

							if(overlap > highestRangeOverlap)
							{
								highestRangeOverlap = overlap;
								heaviestRange = range;
							}
						} // end for
					}
				}

				// check it against the AIConfig
				// if it overlaps, crop the range
				// otherwise, just use the power's range
				if( aiPowersIntersectRange(heaviestRange->min, heaviestRange->max, config->prefMinRange, config->prefMaxRange, &intersectMin, &intersectMax) )
				{
					*rangeMinOut = intersectMin;
					*rangeMaxOut = intersectMax;
				}
				else
				{
					// no intersection
					*rangeMinOut = heaviestRange->min;
					*rangeMaxOut = heaviestRange->max;
				}

				// clean up
				for(i = eaSize(&weightedRanges)-1; i >= 0; i--)
				{
					aiPowerWeightedPowerRangeDestroy(weightedRanges[i]);
				}

				result = true;
			}

		} // end if numPoints > 0
	}

	// clean up
	for(i = eaSize(&usableRanges)-1; i >= 0; i--)
	{
		aiPowerWeightedPowerRangeDestroy(usableRanges[i]);
	}

	return result;
}


static __forceinline void AIPowerEntityInfo_AddFlags(AIPowerEntityInfo *powers, AIPowerInfo *powInfo)
{
	powers->hasAttackPowers |= powInfo->isAttackPower && !powInfo->isAfterDeathPower;
	powers->hasHealPowers |= powInfo->isHealPower;
	powers->hasBuffPowers |= powInfo->isBuffPower;
	powers->hasCurePower |= powInfo->isCurePower;
	powers->hasLungePowers |= powInfo->isLungePower;
	powers->hasResPowers |= powInfo->isResPower;
	powers->hasShieldHealPowers |= powInfo->isShieldHealPower;
	powers->hasControlPowers |= powInfo->isControlPower;
	powers->hasArcLimitedPowers |= powInfo->isArcLimitedPower;
	powers->hasAfterDeathPowers |= powInfo->isAfterDeathPower;
	powers->hasDeadOrAlivePowers |= powInfo->isDeadOrAlivePower;
}

// Resets the flags on AIPowerEntityInfo based on the currently activatable AIPowerInfos 
static void aiRecompilePowerFlags(Entity *e, AIVarsBase *aib)
{
	aib->powers->hasAttackPowers = 0;
	aib->powers->hasHealPowers = 0;
	aib->powers->hasBuffPowers = 0;
	aib->powers->hasCurePower = 0;
	aib->powers->hasLungePowers = 0;
	aib->powers->hasResPowers = 0;
	aib->powers->hasShieldHealPowers = 0;
	aib->powers->hasControlPowers = 0;
	aib->powers->hasArcLimitedPowers = 0;
	aib->powers->hasAfterDeathPowers = 0;
	aib->powers->hasDeadOrAlivePowers = 0;
	
	FOR_EACH_IN_EARRAY(aib->powers->powInfos, AIPowerInfo, powInfo)
		AIPowerConfig *powConf = aiGetPowerConfig(e, aib, powInfo);
		
		if (powConf->absWeight > 0)
		{
			AIPowerEntityInfo_AddFlags(aib->powers, powInfo);
		}	
	FOR_EACH_END
	
}

static void aiAddPowerInfo(Entity *e, AIVarsBase *aib, Power *power, PowerDef *powDef, CritterPowerConfig *cpc, aiModifierDef *pModifierDef, bool recalcDynamicRange, const char* blameFile)
{
	U32 powIsFlight = 0;
	U32 powIsActivated = power_DefDoesActivate(powDef);
	AIConfig *config = aiGetConfig(e, aib);

	powIsFlight = !!(powDef->eAITags & kPowerAITag_Flight);
	aib->powers->hasFlightPowers = aib->powers->hasFlightPowers || powIsFlight;
	aib->powers->alwaysFlight = aib->powers->alwaysFlight || (powIsFlight && !powIsActivated);

	if(powIsActivated)
	{
		AIPowerInfo* powInfo = aiPowerInfoCreate();
		AIPowerConfig* powConf = aiPowerConfigCreate();
		PowerTarget* mainTarget = GET_REF(powDef->hTargetMain);
		PowerTarget* affectedTarget = GET_REF(powDef->hTargetAffected);
		AIPowerConfigDef effective = {0}, *applyDef;

		StructInit(parse_AIPowerConfigDef, &effective);

		powConf->filename = allocAddString(blameFile);

		powInfo->power = power;

		powInfo->critterPow = cpc;
		powInfo->defaultPowConfig = powConf;

		if(applyDef = RefSystem_ReferentFromString("AIPowerConfigDef", powDef->pchAIPowerConfigDef))
			SimpleInheritanceApply(parse_AIPowerConfigDef, &effective, applyDef, NULL, NULL);
		if(powDef->pAIPowerConfigDefInst)
			SimpleInheritanceApply(parse_AIPowerConfigDef, &effective, powDef->pAIPowerConfigDefInst, NULL, NULL);

		if(cpc)
		{
			if(cpc && (applyDef = RefSystem_ReferentFromString("AIPowerConfigDef", cpc->pchAIPowerConfigDef)))
				SimpleInheritanceApply(parse_AIPowerConfigDef, &effective, applyDef, NULL, NULL);
			if(cpc && cpc->aiPowerConfigDefInst)
				SimpleInheritanceApply(parse_AIPowerConfigDef, &effective, cpc->aiPowerConfigDefInst, NULL, NULL);

			if (gConf.bExposeDeprecatedPowerConfigVars)
			{
				if(!effective.absWeight)
					effective.absWeight = cpc->fAIWeight;
				if(!effective.minDist)
					effective.minDist = cpc->fAIPreferredMinRange;
				if(!effective.maxDist)
					effective.maxDist = cpc->fAIPreferredMaxRange;
				if(!effective.chainTarget)
					effective.chainTarget = cpc->pchAIChainTarget;
				if(!effective.chainTime)
					effective.chainTime = cpc->fAIChainTime;
				if(!effective.chainRequires && cpc->pExprAIChainRequires)
					effective.chainRequires = exprClone(cpc->pExprAIChainRequires);
				if(!effective.aiRequires && cpc->pExprAIRequires)
					effective.aiRequires = exprClone(cpc->pExprAIRequires);
				if(!effective.aiEndCondition && cpc->pExprAIEndCondition)
					effective.aiEndCondition = exprClone(cpc->pExprAIEndCondition);
				if(!effective.targetOverride && cpc->pExprAITargetOverride)
					effective.targetOverride = exprClone(cpc->pExprAITargetOverride);
				if(!effective.weightModifier && cpc->pExprAIWeightModifier)
					effective.weightModifier = exprClone(cpc->pExprAIWeightModifier);
				if(!effective.cureRequires && cpc->pExprAICureRequires)
					effective.cureRequires = exprClone(cpc->pExprAICureRequires);
			}
		}

		// iAIMinRange and iAIMaxRange should always be 0 for everyone but champs.  (Though STO won't benefit from this assert either, currently)
		devassert(gConf.bExposeDeprecatedPowerConfigVars || (powDef->iAIMinRange == 0 && powDef->iAIMaxRange == 0));
		if(!effective.minDist)
			effective.minDist = powDef->iAIMinRange;
		if(!effective.maxDist)
		{
			if(powDef->iAIMaxRange)
				effective.maxDist = powDef->iAIMaxRange;
			else
				effective.maxDist = power_GetRange(power, powDef);
		}

		if (powDef->eAITags & kPowerAITag_UseWithinPreferredMax)
		{
			if (config->prefMaxRange > 0.f)
			{
				#define PREFERRED_RANGE_MAX_BUFFER	1.6f
				effective.maxDist = MIN(config->prefMaxRange + PREFERRED_RANGE_MAX_BUFFER, effective.maxDist);
			}
		}

		powConf->minDist = effective.minDist;
		powConf->maxDist = effective.maxDist;

		if(effective.chainTarget)
			powConf->chainTarget = allocAddString(effective.chainTarget);
		powConf->chainTime = effective.chainTime;
		powConf->bChainLocksFacing = effective.bChainLocksFacing;
		powConf->bChainLocksMovement = effective.bChainLocksMovement;

		powConf->absWeight = effective.absWeight;

		if(pModifierDef)
		{
			F32 fRange = power_GetRange(power, powDef);
			powConf->absWeight *= pModifierDef->fWeightMulti;
			powConf->maxDist = min(powConf->maxDist * pModifierDef->fMaxDistMulti, fRange);
			powConf->minDist *= pModifierDef->fMinDistMulti;
		}

		if(effective.weightModifier)
		{
			powConf->weightModifier = exprCreate();
			exprCopy(powConf->weightModifier, effective.weightModifier);
		}

		if(effective.aiRequires)
		{
			powConf->aiRequires = exprCreate();
			exprCopy(powConf->aiRequires, effective.aiRequires);
			if(exprIsNonGenerated(powConf->aiRequires))
				exprGenerate(powConf->aiRequires, aib->exprContext);
		}

		if(effective.aiEndCondition)
		{
			powConf->aiEndCondition = exprCreate();
			exprCopy(powConf->aiEndCondition, effective.aiEndCondition);
		}

		if(effective.chainRequires)
		{
			powConf->chainRequires = exprCreate();
			exprCopy(powConf->chainRequires, effective.chainRequires);
		}

		if(effective.targetOverride)
		{
			powConf->targetOverride = exprCreate();
			exprCopy(powConf->targetOverride, effective.targetOverride);
			if(exprIsNonGenerated(powConf->targetOverride))
				exprGenerate(powConf->targetOverride, aib->exprContext);
		}

		if(effective.cureRequires)
		{
			powConf->cureRequires = exprCreate();
			exprCopy(powConf->cureRequires, effective.cureRequires);
		}

		if(effective.curePowerTags)
		{
			eaiCopy(&powConf->curePowerTags, &effective.curePowerTags);
		}

		powConf->maxRandomQueueTime = effective.maxRandomQueueTime;

		StructDeInit(parse_AIPowerConfigDef, &effective);

		powInfo->isFlyPower = powIsFlight;

		powInfo->powerTimeRecharge = powDef->fTimeRecharge;

		// Cache the bits for later
		powInfo->aiTagBits = powDef->eAITags;
		if(!(powInfo->aiTagBits & kPowerAITag_AllCode))
		{
			// Take some guesses
			if(affectedTarget && affectedTarget->bAllowFoe)
			{
				powInfo->aiTagBits |= kPowerAITag_Attack;
			}
			if(affectedTarget && affectedTarget->bAllowFriend)
			{
				powInfo->aiTagBits |= kPowerAITag_Heal;
			}
		}

		powInfo->isAttackPower = !!(powInfo->aiTagBits & kPowerAITag_Attack);
		powInfo->isHealPower = !!(powInfo->aiTagBits & kPowerAITag_Heal);
		powInfo->isControlPower = !!(powInfo->aiTagBits & kPowerAITag_Control);
		powInfo->isShieldHealPower = !!(powInfo->aiTagBits & kPowerAITag_Shield_Heal);
		powInfo->isBuffPower = !!(powInfo->aiTagBits & kPowerAITag_Buff);
		powInfo->isCurePower = !!eaiSize(&powConf->curePowerTags);
		powInfo->isTargetCreatorPower =  !!(affectedTarget->eRequire & kTargetType_Creator);
		powInfo->isTargetOwnerPower = !!(affectedTarget->eRequire & kTargetType_Owner);
		if (powInfo->isCurePower)
		{
			powInfo->aiTagBits |= kPowerAITag_Cure;
		}

		powInfo->isOutOfCombatPower = !!(powInfo->aiTagBits & kPowerAITag_OutOfCombat);
		powInfo->isLungePower =  !!(powInfo->aiTagBits & kPowerAITag_Lunge);
		powInfo->isResPower = !!(powInfo->aiTagBits & kPowerAITag_Resurrect);

		if (powInfo->aiTagBits & kPowerAITag_AreaEffect)
		{
			#define DEFAULT_AE_RANGE	15.f
			powInfo->isAEPower = true;
			powInfo->areaEffectRange = DEFAULT_AE_RANGE;
			// todo: try and get the type of AE and radius 
		}

		powInfo->canAffectSelf = affectedTarget && affectedTarget->bAllowSelf;
		powInfo->isSelfTarget = (mainTarget && mainTarget->bRequireSelf) ? 1 : 0;
		powInfo->isSelfTargetOnly = powInfo->canAffectSelf && affectedTarget->bRequireSelf;
		powInfo->isAfterDeathPower = (powDef->eActivateRules & (kPowerActivateRules_SourceAlive|kPowerActivateRules_SourceDead)) == kPowerActivateRules_SourceDead;
		powInfo->isDeadOrAlivePower = !!(powDef->eActivateRules & (kPowerActivateRules_SourceAlive|kPowerActivateRules_SourceDead));
		powInfo->isArcLimitedPower = powDef->fTargetArc!=0;
		powInfo->isInterruptedOnMovement = !!(powDef->eInterrupts & kPowerInterruption_Movement);
		powInfo->resetValid = true;

		// validate the power info
		{
			if (powConf->maxDist <= 0.f && !powInfo->isSelfTargetOnly)
			{
				// the range of this power is 0, but it says it can effect others
				// set the range to some default
				powConf->maxDist = 10.f;
				AIPowersDebugError(	"AI power info cannot get a valid Max Range for the power %s. "
									"Set it explicitly in the AI section of the power.", powDef->pchName);
				
			}
		}
		
		

		if (powConf->absWeight > 0)
		{
			AIPowerEntityInfo_AddFlags(aib->powers, powInfo);
		}
		
		eaPush(&aib->powers->powInfos, powInfo);

		if(recalcDynamicRange)
		{
			aiPowersCalcDynamicPreferredRange(e, aib);
		}
	}
}

AIPowerInfo* aiPowersFindInfo(Entity* be, AIVarsBase* aib, const char* powName)
{
	int i;

	for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
	{
		Power* curPow = aib->powers->powInfos[i]->power;
		PowerDef* curPowDef = GET_REF(curPow->hDef);

		if(!curPowDef)
			continue;

		if(!stricmp(curPowDef->pchName, powName))
			return aib->powers->powInfos[i];
	}

	return NULL;
}

AIPowerInfo* aiPowersFindInfoByID(Entity* be, AIVarsBase* aib, U32 id)
{
	int i;

	for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
	{
		Power* curPow = aib->powers->powInfos[i]->power;
		if(curPow->uiID ==  id)
			return aib->powers->powInfos[i];
	}

	return NULL;

}

static void aiAddPowersInternal(Entity* e, AIVarsBase* aib, Power** powers, CritterPowerConfig** critterPow, const char* blameFile)
{
	int i, j;
	CritterPowerConfig** critterPowOnce = NULL;
	eaCopy(&critterPowOnce,&critterPow); // remove critterPow's when found
	for(i = eaSize(&powers)-1; i >= 0; i--)
	{
		Power* curPow = powers[i];
		PowerDef* curPowDef = GET_REF(curPow->hDef);
		int found = 0;
		// CritterPowerConfigs get moved to shared memory, so even though they are pooled, it's a different pool?
		//const char* allocName = allocFindString(curPowDef->pchName);

		//if(!allocName)
		//continue; // apparently there is no CritterPowerConfig that has this name...

		if(!curPowDef)
			continue;

		if(curPow->eSource == kPowerSource_Item)
		{
			continue;
		}

		for(j = eaSize(&critterPowOnce)-1; j >= 0; j--)
		{
			//if(power->pchName == allocName)
			if(GET_REF(critterPowOnce[j]->hPower)==curPowDef)
			{
				found = 1;
				aiAddPowerInfo(e, aib, curPow, curPowDef, critterPowOnce[j], NULL, false, blameFile);
				eaRemoveFast(&critterPowOnce,j); // don't want to use same CritterPowerConfig
				break;
			}
		}

		if(!found)
			aiAddPowerInfo(e, aib, curPow, curPowDef, NULL, NULL, false, blameFile);

		// wait until all powers are added and then recalculate dynamic range
		aiPowersCalcDynamicPreferredRange(e, aib);
	}
	eaDestroy(&critterPowOnce);
}

void aiAddPowersFromCritterPowerConfigs(Entity* e, AIVarsBase* aib, CritterPowerConfig** critterPow, const char* blameFile)
{
	F32 totalWeight = 0;
	F32 fFlight = 0;

	PERFINFO_AUTO_START_FUNC();

	pmEnableFaceSelected(e, false);

	fFlight = character_GetClassAttrib(e->pChar, kClassAttribAspect_Basic, kAttribType_Flight);

	if(fFlight>0)
	{
		aib->powers->alwaysFlight = 1;
	}

	aiAddPowersInternal(e, aib, e->pChar->ppPowers, critterPow, blameFile);
	if(e->externalInnate)
		aiAddPowersInternal(e, aib, e->externalInnate->ppPowersExternalInnate, critterPow, blameFile);

	PERFINFO_AUTO_STOP();
}

static void aiApplyPowerConfigList(Entity* e, AIVarsBase* ai)
{
	int i;
	char* configRefStr = NULL;
	bool foundPowerInConfig = false;

	estrStackCreate(&configRefStr);

	for(i = eaSize(&ai->powers->powInfos)-1; i >= 0; i--)
	{
		AIPowerInfo* curPowInfo = ai->powers->powInfos[i];
		PowerDef* curPowDef = GET_REF(curPowInfo->power->hDef);
		AIPowerConfig* configListConf;

		if(!curPowDef)
			continue;

		estrClear(&configRefStr);
		estrConcatf(&configRefStr, "%s:%s", REF_STRING_FROM_HANDLE(ai->powers->powerConfigList),curPowDef->pchName);
		REMOVE_HANDLE(curPowInfo->powConfig);
		SET_HANDLE_FROM_STRING("AIPowerConfig", configRefStr, curPowInfo->powConfig);

		configListConf = (AIPowerConfig*)GET_REF(curPowInfo->powConfig);

		if(configListConf) // the current power config has an entry for this power
			foundPowerInConfig = true;
	}

	// we found at least one power that was specified in the current power config
	ai->powers->validPowerConfig = foundPowerInConfig;

	estrDestroy(&configRefStr);
}

void aiDestroyPowers(Entity* e, AIVarsBase* aib)
{
	int i;

	for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
	{
		AIPowerInfo* curPowInfo = aib->powers->powInfos[i];

		if(curPowInfo->defaultPowConfig)
			aiPowerConfigDestroy(curPowInfo->defaultPowConfig);

		REMOVE_HANDLE(curPowInfo->powConfig);
	}

	for(i = eaSize(&aib->powers->queuedPowers)-1; i >= 0; i--)
		MP_FREE(AIQueuedPower, aib->powers->queuedPowers[i]);

	eaDestroyEx(&aib->powers->powInfos,aiPowerInfoDestroy);
	aiMultiTickAction_DestroyQueue(e, aib);

	eaDestroy(&aib->powers->queuedPowers);

	REMOVE_HANDLE(aib->powers->powerConfigList);

	aiPowerEntityInfoDestroy(aib->powers);
	aib->powers = NULL;
}

void aiAddPower(Entity *e, AIVarsBase* aib, Power* power)
{
	int i;
	AIPowerInfo *info = NULL;

	for(i = eaSize(&aib->powers->powInfos)-1; i>=0; i--)
	{
		AIPowerInfo *curPowInfo = aib->powers->powInfos[i];

		if(curPowInfo->power==power)
			return;
	}

	aiAddPowerInfo(e, aib, power, GET_REF(power->hDef), NULL, NULL, true, NULL);
}

void aiPowersStopChainLocked(Entity *e, AIVarsBase* aib)
{
	if (aib->chainPowerExecutionActive)
	{
		if (aib->chainLockedFacing)
		{
			aiMovementSetRotationFlag(e, false);
			aib->chainLockedFacing = false;
		}
		aib->chainLockedMovement = false;
		aib->chainPowerExecutionActive = false;
	}
}

void aiRemovePower(Entity* e, AIVarsBase* aib, Power *power)
{
	int i;
	AIPowerInfo **infosToDestroy = NULL;

	for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
	{
		AIPowerInfo* curPowInfo = aib->powers->powInfos[i];
		if(curPowInfo->power == power)
		{
			eaPushUnique(&infosToDestroy,curPowInfo);
			eaRemoveFast(&aib->powers->powInfos,i);
		}
	}

	for(i = eaSize(&aib->powers->queuedPowers)-1; i >= 0; i--)
	{
		AIQueuedPower *pQueuedPower = aib->powers->queuedPowers[i];
		bool bRemove = false;
		if( (pQueuedPower->powerInfo && pQueuedPower->powerInfo->power == power))
		{
			bRemove = true;
			eaPushUnique(&infosToDestroy,pQueuedPower->powerInfo);
			
		}
		else if (pQueuedPower->chainPowerInfo && pQueuedPower->chainPowerInfo->power == power)
		{
			bRemove = true;
			eaPushUnique(&infosToDestroy,pQueuedPower->chainPowerInfo);
		}

		if (bRemove)
		{
			MP_FREE(AIQueuedPower, pQueuedPower);
			eaRemoveFast(&aib->powers->queuedPowers,i);

			aiPowersStopChainLocked(e, aib);
		}
	}

	if(aib->powers->lastUsedPower && aib->powers->lastUsedPower->power == power)
	{
		eaPushUnique(&infosToDestroy,aib->powers->lastUsedPower);
		aib->powers->lastUsedPower = NULL;
	}

	if(aib->powers->preferredPower && aib->powers->preferredPower->power == power)
	{
		eaPushUnique(&infosToDestroy,aib->powers->preferredPower);
		aib->powers->preferredPower = NULL;
	}

	for(i=eaSize(&infosToDestroy)-1; i>=0; i--)
	{
		if(infosToDestroy[i]->defaultPowConfig)
			aiPowerConfigDestroy(infosToDestroy[i]->defaultPowConfig);

		aiMultiTickAction_RemoveQueuedAIPowerInfos(e, aib, infosToDestroy[i]);

		REMOVE_HANDLE(infosToDestroy[i]->powConfig);
	}
	eaDestroyEx(&infosToDestroy,aiPowerInfoDestroy);

	aiRecompilePowerFlags(e, aib);
}

// Probably don't need aiPowersResetPowersBegin, but it's nice to assert that when powers are added
// that we are inside the character_ResetPowersArray function
// leaving this here for now...
typedef struct AIPowerResetHelper
{
	Entity *pCurEnt;
} AIPowerResetHelper;

static AIPowerResetHelper gAIPowerResetHelper = {0};

void aiPowersResetPowersBegin(Entity *e, Power **ppOldPowers)
{
	gAIPowerResetHelper.pCurEnt = e;
}

bool aiPowersResetPowersIsInReset()
{
	return gAIPowerResetHelper.pCurEnt != NULL;
}


static int aiPowersResetAddPowerInfo(Entity *e, Power *pPower, PowerDef* pPowDef)
{
	if (pPower->eSource == kPowerSource_Class ||  
		pPower->eSource == kPowerSource_PowerTree || 
		pPower->eSource == kPowerSource_Personal ||
		pPower->eSource == kPowerSource_AttribMod)
	{
		aiAddPowerInfo(e,e->aibase,pPower,pPowDef,NULL,NULL,false,NULL);
	}
	else if (pPower->eSource == kPowerSource_Item)
	{
		aiModifierDef *pModifier = NULL;
		ItemPowerDef *pItemPowerDef;
		
		if (!pPowDef)
			return false;

		pItemPowerDef = item_GetItemPowerDefByPowerDef(pPower->pSourceItem, pPowDef);
		if (!pItemPowerDef || ! pItemPowerDef->pPowerConfig)
			return false;

		if (e->pCritter)
		{
			CritterDef *pCritterDef = GET_REF(e->pCritter->critterDef);

			pCritterDef = GET_REF(e->pCritter->critterDef);
			if (!pCritterDef || !pPower->pSourceItem)
				return false;

			FOR_EACH_IN_EARRAY(pCritterDef->ppCritterItems, DefaultItemDef, pDefaultItemDef);
				if(REF_COMPARE_HANDLES(pDefaultItemDef->hItem, pPower->pSourceItem->hItem))
				{
					pModifier = pDefaultItemDef->pModifierInfo;
					break;
				}
			FOR_EACH_END;
		}

		aiAddPowerInfo(	e, e->aibase, pPower, pPowDef,
						pItemPowerDef->pPowerConfig, pModifier, 
						false, pItemPowerDef->pchFileName);
	}

	return true;
}

void aiPowersResetPowersEnd(Entity *e, Power **ppNewPowers)
{
	bool bRecalculateSettings = false;
	bool bFoundPower;
	S32 i;
	AIVarsBase *aib;
	
	if (!e || !e->aibase)
		return;

	aib = e->aibase;

	FOR_EACH_IN_EARRAY(aib->powers->powInfos, AIPowerInfo, pPowInfo)
		pPowInfo->resetValid = false;
	FOR_EACH_END
	
	// go through the new power list and for all the powers we care about
	// add powers that aren't on our aib->powers->powInfos
	FOR_EACH_IN_EARRAY(ppNewPowers, Power, pPower);
	{
		PowerDef* pPowDef;

		bFoundPower = false;
		if (pPower->eSource != kPowerSource_PowerTree && 
			pPower->eSource != kPowerSource_Item &&
			pPower->eSource != kPowerSource_Personal && 
			pPower->eSource != kPowerSource_Class &&
			pPower->eSource != kPowerSource_AttribMod)
			continue;

		pPowDef = GET_REF(pPower->hDef);
		if (!pPowDef || !power_DefDoesActivate(pPowDef))
			continue;
		
		FOR_EACH_IN_EARRAY(e->aibase->powers->powInfos, AIPowerInfo, pPowInfo);
			if (pPowInfo->power == pPower)
			{
				pPowInfo->resetValid = true;
				bFoundPower = true;
				break;
			}
		FOR_EACH_END;

		if (!bFoundPower)
		{
			if (aiPowersResetAddPowerInfo(e, pPower, pPowDef))
				bRecalculateSettings = true;
		}
	}
	FOR_EACH_END;

	for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
	{
		AIPowerInfo *pPowerInfo = aib->powers->powInfos[i];
		if (pPowerInfo->resetValid == false)
		{
			S32 oldSize = eaSize(&aib->powers->powInfos);
			S32 newSize;
			aiRemovePower(e, e->aibase, pPowerInfo->power);
			bRecalculateSettings = true;
			// 
			newSize = eaSize(&aib->powers->powInfos);
			if (oldSize - newSize > 1)
			{	// removed more than one power, we need to reset our traversal
				i = eaSize(&aib->powers->powInfos);
			}
		}
	}

	// Might need to rebuild the owner's PetPowerState
	if(e->erOwner)
	{
		Entity *pentOwner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);
		if(pentOwner && pentOwner->pPlayer && entCheckFlag(pentOwner,ENTITYFLAG_IS_PLAYER))
		{
			EntityRef erPet = entGetRef(e);
			for(i=eaSize(&pentOwner->pPlayer->petInfo)-1; i>=0; i--)
			{
				if(pentOwner->pPlayer->petInfo[i]->iPetRef==erPet)
				{
					entity_RebuildPetPowerStates(pentOwner,pentOwner->pPlayer->petInfo[i]);
				}
			}
		}
	}


	if (bRecalculateSettings)
	{
		aiPowersCalcDynamicPreferredRange(e, e->aibase);
		// todo: might be better to have a aiTeam rescan for only powers related fields
		if (e->aibase->team)
			aiTeamRescanSettings(e->aibase->team);
	}

	gAIPowerResetHelper.pCurEnt = NULL;
}




void aiPowersUpdateConfigSettings(Entity *e, AIVarsBase *aib, AIConfig *config)
{
	if(config->preferredAITag)
	{
		aib->powers->preferredAITagBit = StaticDefineIntGetInt(PowerAITagsEnum, config->preferredAITag);
		if(aib->powers->preferredAITagBit==-1)
		{
			aib->powers->preferredAITagBit = 0;
			//error?
		}
	}
}

AIPowerConfig zeroWeightConfig = {NULL, 0, 0};

AIPowerConfig* aiGetOriginalPowerConfig(Entity *e, AIVarsBase* aib, AIPowerInfo* powInfo)
{
	AIPowerConfig* returnConf = (AIPowerConfig*)GET_REF(powInfo->powConfig);

	if(aib->powers->validPowerConfig)
	{
		if(returnConf)
			return returnConf;
		else
			return &zeroWeightConfig;
	}
	else
		return powInfo->defaultPowConfig;
}

AIPowerConfig* aiGetPowerConfig(Entity* e, AIVarsBase* aib, AIPowerInfo* powInfo)
{
	if(powInfo->localModifiedPowConfig)
		return powInfo->localModifiedPowConfig;
	else
		return aiGetOriginalPowerConfig(e, aib, powInfo);
}

AIPowerConfig* aiPowerConfigGetModifiedConfig(Entity* e, AIVarsBase* aib, AIPowerInfo* powInfo)
{
	if(!powInfo->localModifiedPowConfig)
	{
		AIPowerConfig *orig = aiGetPowerConfig(e, aib, powInfo);
		powInfo->localModifiedPowConfig = StructAlloc(parse_AIPowerConfig);
		StructCopyAll(parse_AIPowerConfig, orig, powInfo->localModifiedPowConfig);
	}

	return powInfo->localModifiedPowConfig;
}

void aiTurnOffPower(Entity* be, Power *power)
{
	if(power && power->bActive)
	{
		PowerDef *def = GET_REF(power->hDef);
		if(def && def->eType==kPowerType_Toggle)
		{
			character_ActivatePowerServerBasic(entGetPartitionIdx(be), be->pChar, power, be, NULL, true, false, NULL);
		}
		else
		{
			character_ActivatePowerServerBasic(entGetPartitionIdx(be), be->pChar, power, be, NULL, false, false, NULL);
		}
	}
}

void aiCancelQueuedPower(Entity *e)
{
	if (e->pChar->pPowActQueued)
	{
		Power *power = character_ActGetPower(e->pChar, e->pChar->pPowActQueued);
		if (power)
		{
			character_ActivatePowerServerBasic(entGetPartitionIdx(e), e->pChar, power, e, NULL, false, false, NULL);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------
static bool aiPowers_GetTargetPos(	Entity* e, 
									AIPowerInfo* powerInfo, 
									PowerDef* powerDef, 
									Entity* powTarget, 
									Entity* secondaryTarget, 
									Vec3 vOutPos)
{
	Entity* pTargetPosEnt = NULL;

	if (powTarget && powTarget != e)
	{
		pTargetPosEnt = powTarget;
		
	} 
	else if (secondaryTarget)
	{
		pTargetPosEnt = secondaryTarget;
	}

	if (pTargetPosEnt)
	{
		if (!powerInfo->isLungePower)
		{
			entGetCombatPosDir(pTargetPosEnt, NULL, vOutPos, NULL);
			
			if (powerdef_ignorePitch(powerDef))
			{
				Vec3 vCurCombatPos;
  				entGetCombatPosDir(e, NULL, vCurCombatPos, NULL);
				vOutPos[1] = vCurCombatPos[1];
			}
		}
		else
		{	// for lunges, we need to use the entity root position instead of the combat position
			entGetPos(pTargetPosEnt, vOutPos);
		}

		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------------------
void aiUsePower(Entity* e, AIVarsBase* aib, 
				AIPowerInfo* powerInfo, 
				Entity* target, 
				Entity* secondaryTarget, 
				const Vec3 vTargetPos,
				int doPowersCheck, 
				AIPowerInfo* chainSource, 
				int forceTarget, 
				int cancelExisting)
{
	PowerDef* powerDef = (PowerDef*)GET_REF(powerInfo->power->hDef);
	AIPowerConfig* powConf;
	int partitionIdx = entGetPartitionIdx(e);

	if(!powerDef)
		return;

	if(chainSource)
	{
		AIPowerConfig* chainSourcePowConf = aiGetPowerConfig(e, aib, chainSource);

		if(chainSourcePowConf->chainRequires &&
			!aiCheckPowersRequiresExpr(	e, aib, target, chainSource,
										chainSourcePowConf->chainRequires, 
										chainSourcePowConf->filename))
		{
			aiPowersStopChainLocked(e, aib);
			return;
		}
	}

	powConf = aiGetPowerConfig(e, aib, powerInfo);

	if(!doPowersCheck || aiPowersAllowedToExecutePower(e, aib, target, powerInfo, powConf, NULL))
	{
		Entity* powTarget;
		const F32 *pvSecondaryTargetEntPos = NULL;
		Vec3 secondaryTargetEntPos;

		if (!forceTarget)
		{
			powTarget = aiPowersGetTarget(e, aib, target, powerInfo, powConf, false);
		}
		else
		{
			// target is forced, though if this is a self target power we need to target self,
			// even though it may be an attack power, such as a PBAoE
			if (powerInfo->isSelfTarget)
			{
				powTarget = e;
			}
			else
			{
				powTarget = target;
			}
		}

		AI_DEBUG_PRINT(e, AI_LOG_COMBAT, 5, "Executing power %s", powerDef->pchName);

		entSetActive(e);

		// RRP: is this needed anymore now that we have the kPowerAITag_UseTargetPos ?
		// from what I see character_ActivatePowerServerBasic will ignore any passed in position if there is a valid entity..
		if (secondaryTarget)
		{
			entGetCombatPosDir(secondaryTarget, NULL, secondaryTargetEntPos, NULL);
			pvSecondaryTargetEntPos = secondaryTargetEntPos;
		}
		else if (powTarget)
		{
			entGetPos(powTarget, secondaryTargetEntPos);
			pvSecondaryTargetEntPos = secondaryTargetEntPos;
		}

		if (vTargetPos)
		{
			pvSecondaryTargetEntPos = vTargetPos;
			powTarget = NULL;
		}
		else if (powerInfo->aiTagBits & kPowerAITag_UseTargetPos)
		{
			if (aiPowers_GetTargetPos(e, powerInfo, powerDef, powTarget, secondaryTarget, secondaryTargetEntPos))
			{
				pvSecondaryTargetEntPos = secondaryTargetEntPos;
				powTarget = NULL;
			}
		}
		
		
		if(!character_ActivatePowerServerBasic(partitionIdx, e->pChar, powerInfo->power, 
												powTarget, pvSecondaryTargetEntPos, true, cancelExisting, NULL))
		{
			AI_DEBUG_PRINT(e, AI_LOG_COMBAT, 4,
				"Tried to use power %s but powers system says it failed", powerDef->pchName);
			return;
		}

		powerInfo->timesUsed++;
		powerInfo->lastUsed = ABS_TIME_PARTITION(partitionIdx);
		aib->powers->totalUses++;
		aib->powers->lastUsedPower = powerInfo;
		aib->time.lastUsedPower = ABS_TIME_PARTITION(partitionIdx);
				
		// if we used a power against our attack target
		if (aib->pBrawler && powerInfo->isAttackPower && aib->attackTarget == secondaryTarget)
		{
			aiBrawlerCombat_SetState(aib, EBrawlerCombatState_ENGAGED);
		}

		if(powConf->chainTarget)
		{
			bool bDisableRotation = powConf->bChainLocksFacing;
			bool bDisableMovement = powConf->bChainLocksMovement;
			int i;

			for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
			{
				AIPowerInfo* chainInfo = aib->powers->powInfos[i];
				PowerDef* powDef = GET_REF(chainInfo->power->hDef);
				if(powDef && !stricmp(powDef->pchName, powConf->chainTarget))
				{
					S64 execTime;
					if(powConf->chainTime == -1)
					{
						AIConfig* config = aiGetConfig(e, aib);
						execTime = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(config->globalPowerRecharge);
					}
					else
						execTime = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(powConf->chainTime);

					powConf = aiGetPowerConfig(e, aib, chainInfo);
					aiQueuePowerAtTime(e, aib, chainInfo, target, secondaryTarget, execTime, powerInfo);

					if (bDisableRotation)
					{
						// Disable rotation
						aiMovementSetRotationFlag(e, true);
						aib->chainLockedFacing = true;
					}
					else
					{
						// Enable rotation
						aiMovementSetRotationFlag(e, false);
						aib->chainLockedFacing = false;
					}

					if (bDisableMovement)
					{
						aiMovementResetPath(e, aib);
						aib->chainLockedMovement = true;
					}
					else 
					{
						aib->chainLockedMovement = false;
					}

					// Chained power execution is on
					aib->chainPowerExecutionActive = true;

					break;
				}
			}

			if (i < 0)
			{
				// Enable rotation when the power execution ends
				aib->chainPowerExecutionActive = false;
			}
		}
		else
		{
			// Enable rotation when the power execution ends
			aib->chainPowerExecutionActive = false;
		}
	}
	else
	{
		AI_DEBUG_PRINT(e, AI_LOG_COMBAT, 4, "Failed to use power %s (did %spowers check)",
			powerDef->pchName, doPowersCheck ? "" : "not do");
	}
}

static void aiPowersExprVarsAdd(ExprContext* context, Entity* target, AIPowerInfo* powInfo, AttribModDef *modDef)
{
	exprContextSetPointerVarPooledCached(context, targetEntString, target, parse_Entity, true, true, &targetEntVarHandle);
	exprContextSetPointerVarPooledCached(context, powInfoString, powInfo, parse_AIPowerInfo, true, true, &powInfoVarHandle);
	exprContextSetPointerVarPooledCached(context, attribModDefString, modDef, parse_AttribModDef, true, true, &attribModDefHandle);
}

static void aiPowersExprVarsRemove(ExprContext* context)
{
	exprContextRemoveVarPooled(context, targetEntString);
	exprContextRemoveVarPooled(context, powInfoString);
}

static int aiCheckPowersRequiresExpr(Entity* e, AIVarsBase* aib, Entity* target, AIPowerInfo* powInfo, Expression* expr, const char* blamefile)
{
	MultiVal answer = {0};
	aiPowersExprVarsAdd(aib->exprContext, target, powInfo, NULL);
	exprEvaluate(expr, aib->exprContext, &answer);
	aiPowersExprVarsRemove(aib->exprContext);
	if(answer.type != MULTI_INT)
	{
		ErrorFilenamef(blamefile, "Expression %s does not return a valid answer for AIRequires",
			exprGetCompleteString(expr));
		return false;
	}
	else if(!QuickGetInt(&answer))
		return false;

	return true;
}

int aiCheckPowersEndConditionExpr(Entity* e, AIVarsBase* aib, Entity* target, AIPowerInfo* powInfo, Expression* expr, const char* blamefile)
{
	// TODO(GT)
	//MultiVal answer = {0};
	//aiPowersExprVarsAdd(e, aib, aib->exprContext, target, powInfo);
	//exprEvaluate(expr, aib->exprContext, &answer);
	//aiPowersExprVarsRemove(e, aib, aib->exprContext);
	//if(answer.type != MULTI_INT)
	//{
	//	ErrorFilenamef(blamefile, "Expression %s does not return a valid answer for AIEndCondition",
	//		exprGetCompleteString(expr));
	//	return false;
	//}
	//else if(!QuickGetInt(&answer))
	//	return false;

	//return true;
	return false;
}


F32 aiEvaluatePowerWeight(Entity *e, AIVarsBase *aib, Entity *target, AIPowerInfo* powInfo,
						  Expression* expr, const char* blamefile)
{
	bool valid;
	F32 weight;
	MultiVal answer = {0};
	aiPowersExprVarsAdd(aib->exprContext, target, powInfo, NULL);
	exprEvaluate(expr, aib->exprContext, &answer);
	aiPowersExprVarsRemove(aib->exprContext);
	weight = MultiValGetFloat(&answer, &valid);
	if(!valid)
	{
		if(answer.type == MULTI_INVALID)
			;
		else
			ErrorFilenamef(blamefile, "Weight modifier expression did not return a number");
	}
	return weight;
}

// checks the expression to see if the AttribMod can be cured, otherwise
static int aiCanPowerCure(Entity *e, AIVarsBase *aib, Entity *target, AIPowerInfo *pInfo,
						  AIPowerConfig *pConfig, AttribModDef *pModDef)
{

	if (pConfig->cureRequires)
	{
		MultiVal answer = {0};

		aiPowersExprVarsAdd(aib->exprContext, target, pInfo, pModDef);
		exprEvaluate(pConfig->cureRequires, aib->exprContext, &answer);
		aiPowersExprVarsRemove(aib->exprContext);


		if (answer.type != MULTI_INT)
		{
			ErrorFilenamef(pConfig->filename, "Expression %s does not return a valid answer for CureRequires",
							exprGetCompleteString(pConfig->cureRequires));
			return false;
		}
		else if(!QuickGetInt(&answer))
			return false;

		return true;
	}
	else
	{
		S32 i;
		// just check if any of the power tags on the AttribModDef match any tag on the AIPowerConfig
		for (i = eaiSize(&pConfig->curePowerTags) - 1; i >= 0; i--)
		{
			if (powertags_Check(&pModDef->tags, pConfig->curePowerTags[i]))
			{
				return true;
			}
		}

		return false;
	}

}
//
// NOTE: aiPowersRatePowerForTarget and aiPowersGetBestPowerForTarget,
// somewhat duplicates parts of aiRatePowers in some ways, it may be worth it to
// fold the functions together via flags and such, but for now I am keeping them separated
// these are mainly used for buff/healing, but can probably be used for attack powers as well
typedef F32 (*InternalWeightingCB)(Entity *e, Entity *target);

typedef struct AIPowersRateForTargetInput
{
	Vec3 curPos;
	Vec3 leashPos;
	F32 leashDistance;
	AIPowerInfo *powInfo;
	AIPowerConfig *powConfig;
	InternalWeightingCB fpWeight;
} AIPowersRateForTargetInput;

static __forceinline void rateForTargetInputInit(SA_PARAM_NN_VALID AIPowersRateForTargetInput *p, SA_PARAM_NN_VALID Entity *e,
												 SA_PARAM_OP_VALID AIPowerInfo *powInfo)
{
	entGetPos(e, p->curPos);

	aiGetLeashPosition(e, e->aibase, p->leashPos);
	p->leashDistance = aiGetLeashingDistance(e, NULL, NULL, 0.f);

	if (powInfo)
	{
		p->powInfo = powInfo;
		p->powConfig = aiGetPowerConfig(e, e->aibase, powInfo);
	}
	else
	{
		p->powInfo = NULL;
		p->powConfig = NULL;
	}

}

static __forceinline int aiPowersIsPowerReadyToUseOnTarget(const Entity *e, const Entity *target,
															const AIPowerInfo *pPowInfo, const AIPowerConfig *pPowConfig,
															const Vec3 vTargetPos, const Vec3 vLeashPos, F32 leashDist)
{
	F32 range;
	if (pPowInfo->power->fTimeRecharge > 0.f)
		return false; // recharging, can't use yet
	if (pPowInfo->isSelfTargetOnly && target != e)
		return false; // this power can only target self, and this isn't me
	if (pPowConfig->absWeight <= 0.f)
		return false; // being disallowed from using power

	range = interpF32(0.9f, pPowConfig->minDist, pPowConfig->maxDist);
	// use a range that is near the max range of our power
	// *assumes min range isn't an issue!
	// todo: fix assumption
	range += leashDist;

	return distance3Squared(vTargetPos, vLeashPos) < SQR(range);
}


AIPowerInfo* aiPowersGetCurePowerForAttribMod(Entity *e, AIVarsBase *aib, Entity *target, AttribMod *pMod, AttribModDef *pModDef)
{
	Vec3 leashPos, targetPos;
	F32 leashDist;

	aiGetLeashPosition(e, aib, leashPos);
	leashDist = aiGetLeashingDistance(e, NULL, NULL, 0.f);
	entGetPos(target, targetPos);

	FOR_EACH_IN_EARRAY(aib->powers->powInfos, AIPowerInfo, pPowInfo)
	{
		AIPowerConfig *pConfig;

		if (!pPowInfo->isCurePower)
			continue;

		pConfig = aiGetPowerConfig(e, aib, pPowInfo);

		if(!aiPowersIsPowerReadyToUseOnTarget(e, target, pPowInfo, pConfig, targetPos, leashPos, leashDist))
			continue;
		if (!aiPowersAllowedToExecutePower(e, aib, target, pPowInfo, pConfig, NULL))
			continue;

		if(aiCanPowerCure(e, aib, target, pPowInfo, pConfig, pModDef))
		{
			return pPowInfo;
		}
	}
	FOR_EACH_END

	return NULL;
}

// Rates a specific power on a given target.
// Returns true if the power is valid on the target
static int aiPowersRatePowerForTarget(Entity *e, Entity *target, const Vec3 targetPos,
									   SA_PARAM_NN_VALID AIPowersRateForTargetInput *input, 
									   SA_PARAM_NN_VALID F32 *rating) 
{
	F32 distSQ;
	int partitionIdx = entGetPartitionIdx(e);
	*rating = -FLT_MAX;
		
	if(! aiPowersIsPowerReadyToUseOnTarget(e, target, input->powInfo, input->powConfig, 
											targetPos, input->leashPos, input->leashDistance))
	{
		return false;
	}
	
	if(! aiPowersAllowedToExecutePower(e, e->aibase, target, input->powInfo, input->powConfig, NULL))
	{	// cannot execute this power from some reason
		return false;
	}

	*rating = 0.f;
	distSQ = distance3Squared(input->curPos, targetPos);
		
	if(distSQ > SQR(input->powConfig->maxDist))
	{	// we'll need to move to cast this, so penalize it
		#define HEAL_DIST_BASIS_SQ	SQR(20.f)
		*rating -= (distSQ - SQR(input->powConfig->maxDist)) / HEAL_DIST_BASIS_SQ;
	}

	if(input->powInfo->isAEPower)
	{	// rate up for the number of things this will hit. 
		#define AE_WEIGHT_PER	0.2f
		Entity *esource;
		S32 i, count = 0;
		F32 rangeSQR = SQR(input->powInfo->areaEffectRange);
		int bHostile = critter_IsKOS(partitionIdx, e, target);
		
		// check the source of the AE
		if (input->powInfo->isSelfTargetOnly) 
		{
			esource = e;
		}
		else 
		{
			esource = target;
		}
		
		// todo: is this a friendly or hostile power?

		aiUpdateProxEnts(esource, esource->aibase);		
		for(i = 0; i < esource->aibase->proxEntsCount; i++)
		{
			if(esource->aibase->proxEnts[i].maxDistSQR > rangeSQR)
				break;
					
			count += bHostile && critter_IsKOS(partitionIdx, e, esource->aibase->proxEnts[i].e);
		}

		*rating += (F32)count * AE_WEIGHT_PER;
	}

	if(input->powConfig->weightModifier)
	{
		*rating += aiEvaluatePowerWeight(e, e->aibase, target, input->powInfo,
											input->powConfig->weightModifier, input->powConfig->filename);
	}
	else 
	{	// give a slight randomization, otherwise we will always pick the first target we find
		// if all the weights are equal
		*rating += randomPositiveF32() * 0.1f;
	}

	if(input->fpWeight)
	{
		*rating += input->fpWeight(e, target);
	}

	return true;
}

// Goes through all the powers on the entity that match the required powerTypeFlags.
// It will rate the power on the target, and pick the highest rated power.
// Returns true if a power was found.
int aiPowersGetBestPowerForTarget(Entity *e, Entity *target, U32 powerTypeFlags, int bIgnoreLeashing,
								   AIPowerRateOutput *pOutput)
{
	AIPowersRateForTargetInput targetRateInput = {0};
	Vec3 myPos,targetPos;
	F32 dist;
	AIVarsBase *aib = e->aibase;

	entGetPos(target, targetPos);
	entGetPos(e, myPos);
	dist = distance3(myPos, targetPos);

	rateForTargetInputInit(&targetRateInput, e, NULL);

	if (bIgnoreLeashing)
	{
		targetRateInput.leashDistance = FLT_MAX;
	}

	ZeroStruct(pOutput);
	pOutput->rating = -FLT_MAX;

	FOR_EACH_IN_EARRAY(aib->powers->powInfos, AIPowerInfo, pPowInfo)
	{
		F32 rating = 0.f;
		if ((pPowInfo->aiTagBits & powerTypeFlags) != powerTypeFlags)
			continue;

		targetRateInput.powInfo = pPowInfo;
		targetRateInput.powConfig = aiGetPowerConfig(e, aib, targetRateInput.powInfo);

		if (!aib->lastAttackActionAllowedMovement && dist > targetRateInput.powConfig->maxDist) // too far away to use power if can't move
			continue;
		if(!aiPowersRatePowerForTarget(e, target, targetPos, &targetRateInput, &rating))
			continue;

		if(rating > pOutput->rating)
		{
			pOutput->rating = rating;
			pOutput->targetPower = pPowInfo;
		}

	}
	FOR_EACH_END

	return pOutput->targetPower != NULL;
}


// Given a power, finds the best target in the given team for the power
// Returns true if a valid target was found
static int aiPowersGetBestTargetInTeamForPower(Entity *e, AITeam *team, AIPowerInfo *powInfo,
												AITeamAssignmentType type, SA_PARAM_NN_VALID Entity **ppOutTarget)
{
	AIPowersRateForTargetInput targetRateInput = {0};
	F32 fBestRating = -FLT_MAX;
	int partitionIdx = entGetPartitionIdx(e);

	*ppOutTarget = NULL;
	rateForTargetInputInit(&targetRateInput, e, powInfo);

	if(targetRateInput.powConfig->targetOverride)
	{
		*ppOutTarget = aiPowersGetOverrideTarget(e, e->aibase, NULL, powInfo, targetRateInput.powConfig);
		if (*ppOutTarget)
			return true;
	}

	FOR_EACH_IN_EARRAY(team->members, AITeamMember, pMember)
	{
		Vec3 targetPos;
		F32 rating = 0.f;
		
		if (type > AITEAM_ASSIGNMENT_TYPE_NULL && type < AITEAM_ASSIGNMENT_TYPE_COUNT)
		{
			#define TIME_THRESHOLD	3.f
			if (ABS_TIME_SINCE_PARTITION(partitionIdx, pMember->timeLastActedOn[type]) < SEC_TO_ABS_TIME(TIME_THRESHOLD))
				continue;
		}
		entGetPos(pMember->memberBE, targetPos);

		if(!aiPowersRatePowerForTarget(e, pMember->memberBE, targetPos, &targetRateInput, &rating))
			continue;

		if (rating > fBestRating)
		{
			fBestRating = rating;
			*ppOutTarget = pMember->memberBE;
		}
	}
	FOR_EACH_END

	return *ppOutTarget != NULL;
}


static int aiPowersGetBestEnemyTargetForPower(Entity *e, AIVarsBase* aib, AIPowerInfo *powInfo,
											  SA_PARAM_OP_VALID InternalWeightingCB fpWeight, 
											  SA_PARAM_NN_VALID Entity **ppOutTarget)
{
	AIPowersRateForTargetInput targetRateInput = {0};
	F32 fBestRating = -FLT_MAX; 

	*ppOutTarget = NULL;
	rateForTargetInputInit(&targetRateInput, e, powInfo);
	targetRateInput.fpWeight = fpWeight;

	if(targetRateInput.powConfig->targetOverride)
	{
		*ppOutTarget = aiPowersGetOverrideTarget(e, e->aibase, NULL, powInfo, targetRateInput.powConfig);
		if (*ppOutTarget)
			return true;
	}

	// loop through the status table
	FOR_EACH_IN_EARRAY(aib->statusTable, AIStatusTableEntry, status)
	{	
		F32 rating = 0.f;
		Entity *target;
		Vec3 targetPos;
		AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, aib, status);

		// choosing to ignore visible targets
		if(!status->visible || !teamStatus || !teamStatus->legalTarget)
			continue; 

		target = entFromEntityRef(entGetPartitionIdx(e), status->entRef);
		if (!target)
			continue;

		entGetPos(target, targetPos);
		
		if(!aiPowersRatePowerForTarget(e, target, targetPos, &targetRateInput, &rating))
			continue;

		if (rating > fBestRating)
		{
			fBestRating = rating;
			*ppOutTarget = target;
		}
	}
	FOR_EACH_END
	

	return *ppOutTarget != NULL;
}


int aiPowersPickBuffPowerAndTarget(Entity* e, AIVarsBase* aib, int outOfCombat, 
								   AIPowerInfo **ppPowInfoOut, Entity **ppTargetOut)
{
	static AIPowerInfo **s_eaBuffPowers = NULL;
	S32 num;
	AITeam* combatTeam = aiTeamGetCombatTeam(e, aib);

	*ppPowInfoOut = NULL;
	*ppTargetOut = NULL;

	if (! aib->powers->hasBuffPowers)
		return false;

	// todo: I might want to have sub-lists of power types I most care about for faster searching
	//  or a function that returns a list of power types, cached if possible...
	eaClear(&s_eaBuffPowers);
	FOR_EACH_IN_EARRAY(aib->powers->powInfos, AIPowerInfo, pPowInfo)
		if (!pPowInfo->isBuffPower)
			continue;
		if (outOfCombat && !pPowInfo->isOutOfCombatPower)
			continue;
		if (pPowInfo->power->fTimeRecharge > 0.f)
			continue;
		eaPush(&s_eaBuffPowers, pPowInfo);
	FOR_EACH_END

	num = eaSize(&s_eaBuffPowers);
	if (num)
	{
		// some pseudo shuffle, start at a random index of the available buff powers
		S32 idx = randInt(num);
		S32 i;
		for (i = 0; i < num; ++i)
		{
			Entity *pTarget = NULL;
			AIPowerInfo *pPowInfo = s_eaBuffPowers[idx];
			if (aiPowersGetBestTargetInTeamForPower(e, combatTeam, pPowInfo, AITEAM_ASSIGNMENT_TYPE_BUFF, &pTarget))
			{
				*ppTargetOut = pTarget;
				*ppPowInfoOut = pPowInfo;
				return true;
			}

			idx++;
			if (idx >= num)
				idx = 0;
		}
	}

	return false;
}

static F32 aiPowersControlInternalWeighting(Entity *e, Entity *target)
{
	// if the target is our current attack target, deduct some from the weight
	// we prefer to not cast controls on our current target
	return (e->aibase->attackTarget == target) ? -0.25f : 0.f;
}

int aiPowersPickControlPowerAndTarget(Entity* be, AIVarsBase* aib, AIPowerInfo **ppPowInfoOut, Entity **ppTargetOut)
{
	static AIPowerInfo **s_eaControlPowers = NULL;
	S32 num;

	*ppPowInfoOut = NULL;
	*ppTargetOut = NULL;

	if (! aib->powers->hasControlPowers)
		return false;

	// get a list of control powers that are ready to use
	eaClear(&s_eaControlPowers);
	FOR_EACH_IN_EARRAY(aib->powers->powInfos, AIPowerInfo, pPowInfo)
		if (!pPowInfo->isControlPower)
			continue;
		if (pPowInfo->power->fTimeRecharge > 0.f)
			continue;
		eaPush(&s_eaControlPowers, pPowInfo);
	FOR_EACH_END

	num = eaSize(&s_eaControlPowers);
	if (num)
	{
		// some pseudo shuffle, start at a random index of the available buff powers
		S32 idx = randInt(num);
		S32 i;
		for (i = 0; i < num; ++i)
		{
			Entity *pTarget = NULL;
			AIPowerInfo *pPowInfo = s_eaControlPowers[idx];

			if (aiPowersGetBestEnemyTargetForPower(be, aib, pPowInfo, aiPowersControlInternalWeighting, &pTarget))
			{
				*ppTargetOut = pTarget;
				*ppPowInfoOut = pPowInfo;
				return true;
			}

			idx++;
			if (idx >= num)
				idx = 0;
		}
	}

	return false;
}


// ------------------------------------------------------------------------------------------------------------
void aiRatePowers(Entity *e, AIVarsBase *aib, Entity *target, F32 targetDistSQR,
				  AIPowersRateOptions options, AIPowerInfo ***usablePowersOut, F32 *totalRatingOut)
{
	F32 totalRating = 0;
	F32 bestPowRating = 0;
	F32 bestInRangePowRating = 0;
	int dead = options & AI_POWERS_RATE_DEAD;
	int requireUsable = options & AI_POWERS_RATE_USABLE;
	int preferred = options & AI_POWERS_RATE_PREFERRED;
	int notpreferred = options & AI_POWERS_RATE_NOT_PREFERRED;
	int useBonusWeight = !(options & AI_POWERS_RATE_IGNOREBONUSWEIGHT);

	int i;

	if(usablePowersOut)
		eaClearFast(usablePowersOut);

	for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
	{
		AIPowerInfo* curPowInfo = aib->powers->powInfos[i];
		AIPowerConfig* curPowConf = aiGetPowerConfig(e, aib, curPowInfo);
		PowerDef *powDef = GET_REF(curPowInfo->power->hDef);
		Entity* powTarget = NULL;
		F32 distRating = 0;
		F32 absRating = 0;
		int inRange = false;

		curPowInfo->curRating = 0;
		if (!powDef)
		{
			AIPowersDebug("Could not get power def");
			continue;
		}

		if(!curPowInfo->isSelfTargetOnly && powDef->eTargetVisibilityMain==kTargetVisibility_LineOfSight &&
			!(options & AI_POWERS_RATE_IGNORE_WORLD))
		{
			AIStatusTableEntry *status = NULL;

			if(!target)
			{
				AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s)\n\t"
												"Power requires visibility but AI has no target", powDef->pchName);
				continue;
			}
			status = aiStatusFind(e, aib, target, false);
			if(!status)
			{
				AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s)\n\t"
												"Power requires visibility but target has no status entry", powDef->pchName);
				continue;
			}

			if(!status->visible)
			{
				AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s)\n\t"
												"Power requires visibility but target isn't visible", powDef->pchName);
				continue;
			}
		}

		if(!dead && curPowInfo->isAfterDeathPower)
		{
			AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s).\n\t" 
										"Power is after death, and not dead.", powDef->pchName);
			continue;
		}
		if (dead && !curPowInfo->isAfterDeathPower)
		{
			AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s).\n\t" 
										"Dead and power is not for after death.", powDef->pchName);
			continue;
		}

		if(!(curPowInfo->isAttackPower && target) && !curPowInfo->isAfterDeathPower)
		{
			if (!curPowInfo->isSelfTarget)
			{
				AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s).\n\t" 
										"(No target or not an attack) and not a self target power.", powDef->pchName);
				continue;
			}
			if (curPowInfo->isHealPower)
			{
				AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s).\n\t" 
										"Heals not rated for attacks.", powDef->pchName);
				continue;
			}
		}

		if(preferred && !(curPowInfo->aiTagBits & aib->powers->preferredAITagBit))
		{
			AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s).\n\t" 
									"Not a preferred power.", powDef->pchName);
			continue;
		}

		if(notpreferred && (curPowInfo->aiTagBits & aib->powers->preferredAITagBit))
		{
			AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s).\n\t" 
										"Not a not-preferred power.", powDef->pchName);
			continue;
		}

		distRating = 1;
		inRange = true;
		if(curPowInfo->isAttackPower)
		{
			if(!requireUsable || options & AI_POWERS_RATE_IGNORE_WORLD ||
				(target &&
				targetDistSQR >= SQR(curPowConf->minDist) &&
				targetDistSQR <= SQR(curPowConf->maxDist)))
			{
				;
			}
			else
			{
				AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s).\n\t" 
										"Attack power not in range.", powDef->pchName);
				continue;
			}
		}
		else if(curPowInfo->isSelfTarget)
		{
			;
		}
		else if(curPowInfo->isTargetCreatorPower)
		{
			;
		}
		else if(curPowInfo->isTargetOwnerPower)
		{
			;
		}
		else
		{
			AIPowersDebugSelected(e->myRef, "RateAttacks: Power disqualified (%s).\n\t" 
									"Not an attack and not a self target.", powDef->pchName);
			continue;
		}

		// This does the override target calculation, and returns the overridden target
		// but the following expr calculations need the attacktarget as attacktarget
		powTarget = aiPowersGetTarget(e, aib, target, curPowInfo, curPowConf, true);

		if(useBonusWeight && curPowConf->weightModifier)
		{
			curPowInfo->curBonusWeight = aiEvaluatePowerWeight(e, aib, target, curPowInfo,
																curPowConf->weightModifier, curPowConf->filename);
		}
		else
			curPowInfo->curBonusWeight = 0;

		{
			AIPowersExecuteFailureReason failReason;
			int succeedOverride = false;
			if(requireUsable && !aiPowersAllowedToExecutePower(e, aib, target, curPowInfo, curPowConf, &failReason))
			{
				if(options & AI_POWERS_RATE_IGNORE_WORLD &&
					(	
						failReason==AI_POWERS_EXECUTE_FAIL_LOS ||
						failReason==AI_POWERS_EXECUTE_FAIL_RANGE ||
						failReason==AI_POWERS_EXECUTE_FAIL_PERCEIVE
					)
				  )
				{
					succeedOverride = true;
				}
				if(!succeedOverride && (options & AI_POWERS_RATE_COUNT_VALID_FAILS))
				{
					if(!(	failReason == AI_POWERS_EXECUTE_FAIL_COST ||
							failReason == AI_POWERS_EXECUTE_FAIL_OTHER ||
							failReason == AI_POWERS_EXECUTE_FAIL_LOS ||
							failReason == AI_POWERS_EXECUTE_FAIL_RANGE))
					{
						succeedOverride = true;
					}
				}

				if(!succeedOverride)
					continue;
			}
		}

		// this is always 1, yay! :)
		absRating = distRating;

		curPowInfo->curRating = absRating * (curPowConf->absWeight + curPowInfo->curBonusWeight);
		totalRating += curPowInfo->curRating;
		if(usablePowersOut)
			eaPush(usablePowersOut, curPowInfo);
	}

	if(totalRatingOut)
		*totalRatingOut = totalRating;
}

U32 aiIsUsingPowers(Entity *e, AIVarsBase *aib)
{
	// if you're already trying to do a power, don't try to use powers because of combat
	if(eaSize(&aib->powers->queuedPowers))
		return 1;

	// Don't get out of sync with powers
	if(e->pChar->pPowActCurrent || e->pChar->pPowActQueued)
		return 1;

	return 0;
}

void aiStopAttackPowersOnTarget(Entity *e, Entity *target)
{
	EntityRef targetRef;
	if(!target){
		return;
	}
	targetRef = entGetRef(target);

	{
		S32 i = eaSize(&e->aibase->powers->queuedPowers) - 1;	
		// go through the queued powers and remove all the ones that target this guy
		for(; i >= 0; --i)
		{
			AIQueuedPower *pQueued = e->aibase->powers->queuedPowers[i];
			if (pQueued->targetRef == targetRef)
			{
				eaRemove(&e->aibase->powers->queuedPowers, i);
				
				aiPowersStopChainLocked(e, e->aibase);
			}
		}
	}

	// if the pPowActCurrent or pPowActQueued target this guy, stop them
	if(e->pChar)
	{
		if (e->pChar->pPowActCurrent && e->pChar->pPowActCurrent->erTarget == targetRef)
		{
			Power *pPowerCur = character_ActGetPower(e->pChar, e->pChar->pPowActCurrent);
			if (pPowerCur)
			{
				aiTurnOffPower(e, pPowerCur);
			}
		}
		if (e->pChar->pPowActQueued && e->pChar->pPowActQueued->erTarget == targetRef)
		{
			aiCancelQueuedPower(e);
		}
	}
}

S64 aiPowersGetPostRechargeTime(Entity* e, AIVarsBase* aib, AIConfig* config)
{
	S64 referenceTime;

	// Never used a power
	if(!aib->time.lastActivatedPower && !aib->time.lastUsedPower)
		return 0;

	// Here we are checking the global recharge.  If it's already past the recharge, such as the
	//  case of a power with a long charge-up or activation time, that's fine.  If the next AI
	//  tick will be after the recharge, that's also fine (and taken care of later).
	if(aiGlobalSettings.globalRechargeFromStart)
		referenceTime = aib->time.lastUsedPower;
	else
		referenceTime = aib->time.lastActivatedPower;

	referenceTime += SEC_TO_ABS_TIME(config->globalPowerRecharge);

	return referenceTime;
}

// ----------------------------------------------------------------------------------------------------------------
// Indicates whether the entity can use/queue a power right now. Ignores the distance to the target.
bool aiCanUseOrQueuePowerNow(Entity* e, AIVarsBase* aib, AIConfig* config)
{
	U32 ratingOptions = AI_POWERS_RATE_USABLE | AI_POWERS_RATE_IGNORE_WORLD;
	F32 totalRating = 0;
	S32 i;
	static AIPowerInfo** usablePowers = NULL;

	if(aiIsUsingPowers(e, aib) || !aib->attackTarget)
	{
		return false;
	}

	if(aib->powers->preferredAITagBit)
		ratingOptions |= AI_POWERS_RATE_PREFERRED;

	aiRatePowers(e, aib, aib->attackTarget, 0.f, ratingOptions, &usablePowers, &totalRating);

	for (i = eaSize(&usablePowers)-1; i >= 0; i--)
	{
		AIPowerConfig *powConf = aiGetPowerConfig(e, aib, usablePowers[i]);

		if(usablePowers[i]->curBonusWeight+powConf->absWeight<=0)
			continue;

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------------------------------------------
void aiUsePowers(Entity* e, AIVarsBase* aib, AIConfig* config, U32 dead, int randomizeExecutionTime, S64 minTimeToUse)
{
	int i;
	Entity* powerTarget = NULL;
	static AIPowerInfo** usablePowers = NULL;
	F32 totalRating = 0;
	F32 powSelectionNum;
	U32 ratingOptions = 0;
	int partitionIdx = entGetPartitionIdx(e);

	if(!dead && !aib->attackTarget)
		return;

	if(aiIsUsingPowers(e, aib)) 
	{
#if 0 // Jira STO-1490, canceled?
		MultiVal mv; // See if we power end condition is met.
		PowerDef* pd = GET_REF(e->pChar->pPowActCurrent->hdef);
		AIPowerInfo* aipi = aiPowersFindInfo(e,aib,pd->pchName);
		AIPowerConfig* aipc = aiGetPowerConfig(e,aib,aipi);
		exprEvaluate(aipc->aiEndCondition,aib->exprContext,&mv);
		if (mv.type != MULTI_INT)
		{
			ErrorFilenamef(aipc->filename,"Expression %s does not return an int for aiEndCondition",
				exprGetCompleteString(aipc->aiEndCondition));
		}
		if (mv.intval)
		{
			character_ActivatePowerServerBasic(partitionIdx,e->pChar,aipi->power,NULL,NULL,false,true,NULL);
		}
		else
		{
			return; // end condition not met and still using a power, just return
		}
#else
		return;
#endif
	}

	ratingOptions = (dead ? AI_POWERS_RATE_DEAD : 0) | AI_POWERS_RATE_USABLE;
	if(aib->powers->preferredAITagBit)
		ratingOptions |= AI_POWERS_RATE_PREFERRED;

	aiRatePowers(e, aib, aib->attackTarget, aib->attackTargetDistSQR, ratingOptions, &usablePowers, &totalRating);

	if(aib->powers->preferredAITagBit && !eaSize(&usablePowers))
	{
		ratingOptions = (ratingOptions & ~AI_POWERS_RATE_PREFERRED) | AI_POWERS_RATE_NOT_PREFERRED;
		aiRatePowers(e, aib, aib->attackTarget, aib->attackTargetDistSQR, ratingOptions, &usablePowers, &totalRating);
	}

	aib->powers->preferredPower = NULL;
	powSelectionNum = randomPositiveF32() * totalRating;

	for(i = eaSize(&usablePowers)-1; i >= 0; i--)
	{
		AIPowerConfig *powConf = aiGetPowerConfig(e, aib, usablePowers[i]);

		if(usablePowers[i]->curBonusWeight+powConf->absWeight<=0)
			continue;

		powSelectionNum -= usablePowers[i]->curRating;
		if(powSelectionNum <= 0)
		{
			aib->powers->preferredPower = usablePowers[i];
			break;
		}
	}

	if(aib->powers->preferredPower)
	{
		AIPowerInfo* powInfo = aib->powers->preferredPower;
		AIPowerConfig* powConfig = aiGetPowerConfig(e, aib, powInfo);
		powerTarget = aiPowersGetTarget(e, aib, aib->attackTarget, powInfo, powConfig, false);

		if(powerTarget)
		{
			S64 execTime = ABS_TIME_PARTITION(partitionIdx);

			// TODO: The correct timing would not have this if statement here (but would have the true-clause)
			if(minTimeToUse && minTimeToUse > execTime)
				execTime = minTimeToUse;

			if(randomizeExecutionTime)
			{
				if(aib->time.lastEnteredCombat > aib->time.lastUsedPower)
					execTime += SEC_TO_ABS_TIME(randomPositiveF32() * config->globalPowerRecharge);
				else
				{
					if(aiGlobalSettings.useFCRPowerTiming && !entIsPlayer(entGetOwner(e)))
					{
						execTime = aib->time.lastActivatedPower + 
									SEC_TO_ABS_TIME(config->globalPowerRecharge);
					}

					execTime += SEC_TO_ABS_TIME(randomPositiveF32() * config->randomizedPowerDelay);
				}
			}			

			if(powConfig->maxRandomQueueTime > 0)
				execTime += SEC_TO_ABS_TIME(powConfig->maxRandomQueueTime * randomPositiveF32());

			// if we used a power against our attack target
			if (aib->pBrawler && powInfo->isAttackPower)
			{
				aiBrawlerCombat_SetState(aib, EBrawlerCombatState_ENGAGED);
			}

			if(execTime <= ABS_TIME_PARTITION(partitionIdx))
				aiUsePower(e, aib, powInfo, powerTarget, aib->attackTarget, NULL, false, NULL, false, false);
			else
				aiQueuePowerAtTime(e, aib, powInfo, aib->attackTarget, aib->attackTarget, execTime, NULL);
		}
	}
}

// ----------------------------------------------------------------------------------------------------------------
void aiQueuePower(Entity* e, AIVarsBase* aib, AIPowerInfo* powerInfo, Entity* target, Entity* secondaryTarget)
{
	int partitionIdx = entGetPartitionIdx(e);
	aiQueuePowerAtTime(e, aib, powerInfo, target, secondaryTarget, 
						ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(randomPositiveF32() / 2), NULL);
}

// ----------------------------------------------------------------------------------------------------------------
__forceinline static AIQueuedPower* aiQueuePowerInternal(	Entity* e, 
															AIVarsBase* aib,
															AIPowerInfo* powerInfo,
															S64 execTime, 
															AIPowerInfo* chainSource)
{
	AIQueuedPower* pQueuedPower;

	MP_CREATE_COMPACT(AIQueuedPower, 16, 128, 0.8);
	pQueuedPower = MP_ALLOC(AIQueuedPower);

	pQueuedPower->powerInfo = powerInfo;
	pQueuedPower->execTime = execTime;
	pQueuedPower->chainPowerInfo = chainSource;
	
	if(AI_DEBUG_ENABLED(AI_LOG_COMBAT, 6))
	{
		PowerDef* powerDef = GET_REF(powerInfo->power->hDef);
		char secStr[200];
		F32 time = ABS_TIME_TO_SEC(execTime);
		secStr[0] = 0;

		if(powerDef)
		{
			timeMakeOffsetStringFromSeconds(secStr, time);

			AI_DEBUG_PRINT(e, AI_LOG_COMBAT, 6,
				"Queueing power %s for execution at %s.%02d", powerDef->pchName, secStr,
				(int)((time - (int)time) * 100));
		}
	}

	eaPush(&aib->powers->queuedPowers, pQueuedPower);
	return pQueuedPower;
}

// ----------------------------------------------------------------------------------------------------------------
void aiQueuePowerTargetedPosAtTime(	Entity* e, AIVarsBase* aib, AIPowerInfo* powerInfo, const Vec3 vTargetPos, S64 execTime)
{
	AIQueuedPower* pQueuedPower = aiQueuePowerInternal(e, aib, powerInfo, execTime, NULL);
	copyVec3(vTargetPos, pQueuedPower->targetPos);
	pQueuedPower->validTargetPos = true;
}

// ----------------------------------------------------------------------------------------------------------------
void aiQueuePowerAtTime(Entity* e, AIVarsBase* aib, AIPowerInfo* powerInfo, Entity* target,
						Entity* secondaryTarget, S64 execTime, AIPowerInfo* chainSource)
{
	AIQueuedPower* pQueuedPower = aiQueuePowerInternal(e, aib, powerInfo, execTime, chainSource);
	
	pQueuedPower->targetRef = target ? entGetRef(target) : 0;
	pQueuedPower->secondaryTargetRef = (secondaryTarget != NULL) ? entGetRef(secondaryTarget) : 0;
}

void aiCheckQueuedPowers(Entity* e, AIVarsBase* aib)
{
	AIQueuedPower* queuedPower = eaGet(&aib->powers->queuedPowers,0);
	int partitionIdx = entGetPartitionIdx(e);
	Entity *target = NULL;
	Entity *secondaryTarget = NULL;
	Entity *powerTarget = NULL;
	AIPowerConfig* powConfig = NULL;

	if(!queuedPower)
		return;

	if(e->pChar->pPowActCurrent || e->pChar->pPowActQueued)
		return;

	if(ABS_TIME_PARTITION(partitionIdx) < queuedPower->execTime)
		return;
	
	target = entFromEntityRef(partitionIdx, queuedPower->targetRef);
	secondaryTarget = entFromEntityRef(partitionIdx, queuedPower->secondaryTargetRef);

	powConfig = aiGetPowerConfig(e, aib, queuedPower->powerInfo);
	powerTarget = aiPowersGetTarget(e, aib, target, queuedPower->powerInfo, powConfig, true);
	
	if(powerTarget || queuedPower->validTargetPos)
	{
		aiUsePower(	e, e->aibase, 
					queuedPower->powerInfo, 
					target, 
					secondaryTarget,
					(queuedPower->validTargetPos ? queuedPower->targetPos : NULL),
					true, 
					queuedPower->chainPowerInfo, 
					false, 
					false);
	}

	MP_FREE(AIQueuedPower, queuedPower);
	eaRemove(&aib->powers->queuedPowers, 0);
}

AUTO_COMMAND_QUEUED();
void aiClearAllQueuedPowers(ACMD_POINTER Entity* e, ACMD_POINTER AIVarsBase* aib)
{
	int i;

	for(i = eaSize(&aib->powers->queuedPowers)-1; i >= 0; i--)
		MP_FREE(AIQueuedPower, aib->powers->queuedPowers[i]);

	eaClearFast(&aib->powers->queuedPowers);

	aiPowersStopChainLocked(e, aib);
}

// Queues a power for usage as soon as the AI isn't using other powers after at least minTimeSec
//  targetEnt is an entarray, but ONLY THE FIRST IS TARGETED (and this may be overridden by power's target override, unless force is set)
AUTO_EXPR_FUNC(ai) ACMD_NAME(QueuePower);
ExprFuncReturnVal exprFuncQueuePower(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_DICT(PowerDef) const char* powerName, ACMD_EXPR_ENTARRAY_IN targetEnt, int minTimeSec, ACMD_EXPR_ERRSTRING errString)
{
	AIPowerInfo* info = aiPowersFindInfo(e, e->aibase, powerName);
	int partitionIdx = entGetPartitionIdx(e);

	if(!info)
	{
		estrPrintf(errString, "Failed to find power to queue: %s", powerName);
		return ExprFuncReturnError;
	}

	if(!eaSize(targetEnt))
		return ExprFuncReturnFinished;

	aiQueuePowerAtTime(e, e->aibase, info, (*targetEnt)[0], (*targetEnt)[0], 
						ABS_TIME_PARTITION(partitionIdx)+SEC_TO_ABS_TIME(minTimeSec), NULL);
	return ExprFuncReturnFinished;
}

void aiKnockDownCritter(Entity* e)
{
	int partitionIdx = entGetPartitionIdx(e);
	e->aibase->powers->notAllowedToUsePowersUntil = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(1);
}

F32 aiGetPreferredMinRange(Entity* e, AIVarsBase* aib)
{
	AIPowerConfigList* configList = GET_REF(aib->powers->powerConfigList);
	AIConfig *targetconfig = aib->attackTarget ? aiGetConfig(aib->attackTarget, aib->attackTarget->aibase) : NULL;

	if(targetconfig && targetconfig->targetedConfig && targetconfig->targetedConfig->minRange!=0)
		return targetconfig->targetedConfig->minRange;

	// TODO: use the "is this field set" functionality in text parser instead of checking the value...
	if(configList && configList->prefMinRange)
		return configList->prefMinRange;
	else
	{
		// Use dynamic preferred range?
		if(aib->useDynamicPrefRange)
		{
			return aib->minDynamicPrefRange;
		}
		else
		{
			AIConfig* config = aiGetConfig(e, aib);
			return config->prefMinRange;
		}
	}
}

F32 aiGetPreferredMaxRange(Entity* e, AIVarsBase* aib)
{
	AIPowerConfigList* configList = GET_REF(aib->powers->powerConfigList);

	// TODO: use the "is this field set" functionality in text parser instead of checking the value...
	if(configList && configList->prefMaxRange)
		return configList->prefMaxRange;
	else
	{
		// Use dynamic preferred range?
		if(aib->useDynamicPrefRange)
		{
			return aib->maxDynamicPrefRange;
		}
		else
		{
			AIConfig* config = aiGetConfig(e, aib);
			return config->prefMaxRange;
		}
	}
}

// Assign the entity the specified power config, which can override settings like power weighting
AUTO_EXPR_FUNC(ai) ACMD_NAME(AssignPowerConfig);
void exprFuncAssignPowerConfig(ACMD_EXPR_SELF Entity* e, const char* configName)
{
	AIVarsBase* aib = e->aibase;

	REMOVE_HANDLE(aib->powers->powerConfigList);
	SET_HANDLE_FROM_STRING("AIPowerConfigList", configName, aib->powers->powerConfigList);

	// TODO:
	// check if this powerconfig is valid

	aiApplyPowerConfigList(e,aib);
}

// Turns the power mode for this entity on or off (lasts until cleared)
AUTO_EXPR_FUNC(ai, Player) ACMD_NAME(SetPowerMode);
void exprFuncSetPowerMode(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENUM(PowerMode) const char *mode, S32 on)
{
	int i = StaticDefineIntGetInt(PowerModeEnum,mode);
	if(i>kPowerMode_LAST_CODE_SET)
	{
		if(on)
		{
			eaiPushUnique(&e->aibase->powersModes,i);
		}
		else
		{
			int j = eaiFind(&e->aibase->powersModes,i);
			if(j>=0) eaiRemoveFast(&e->aibase->powersModes,j);
		}
		if(e->pChar) e->pChar->bSkipAccrueMods = false;
	}
}

void aiPowersRunAIExpr(Entity* e, Entity* modowner, Entity* modsource, Expression* expr, CommandQueue **cleanupHandlers, ExprLocalData ***localData)
{
	MultiVal answer = {0};

	if(expr)
	{
		AIVarsBase* aib = e->aibase;
		aiUpdateProxEnts(e, aib);
		if(modowner)
			exprContextSetPointerVar(aib->exprContext, "ModOwnerEnt", modowner, parse_Entity, true, true);

		if(modsource)
			exprContextSetPointerVar(aib->exprContext, "ModSourceEnt", modsource, parse_Entity, true, true);

		exprContextCleanupPush(aib->exprContext, cleanupHandlers, localData);

		exprEvaluate(expr, aib->exprContext, &answer);

		exprContextCleanupPop(aib->exprContext);

		exprContextRemoveVar(aib->exprContext, "ModSourceEnt");
		exprContextRemoveVar(aib->exprContext, "ModOwnerEnt");
	}
}

static Entity* aiPowersGetOverrideTarget(Entity* e, AIVarsBase* aib, Entity* target, AIPowerInfo* powInfo, AIPowerConfig* powConfig)
{
	if(powConfig->targetOverride)
	{
		bool valid;
		Entity*** ents;
		MultiVal answer = {0};
		Entity* overrideTarget = NULL;
		int partitionIdx = entGetPartitionIdx(e);

		aiPowersExprVarsAdd(aib->exprContext, target, powInfo, NULL);
		exprEvaluate(powConfig->targetOverride, aib->exprContext, &answer);
		aiPowersExprVarsRemove(aib->exprContext);

		ents = MultiValGetEntityArray(&answer, &valid);
		if(valid)
		{
			if(eaSize(ents) == 1)
			{
				overrideTarget = (*ents)[0];
				powInfo->curOverrideTargetRef = entGetRef(overrideTarget);
				powInfo->overrideTargetTime = ABS_TIME_PARTITION(partitionIdx);
			}
			else if(eaSize(ents) > 1)
				ErrorFilenamef(powConfig->filename, "Target override expression returns multiple entities");
		}
		else if(answer.type != MULTI_INVALID)
			ErrorFilenamef(powConfig->filename, "Target override expression did not return an entity array");

		return overrideTarget ? overrideTarget : target;
	}

	return target;
}


Entity* aiPowersGetTarget(Entity* e, AIVarsBase* aib, Entity* target, AIPowerInfo* powInfo, AIPowerConfig* powConfig, int updateOverrideTarget)
{
	int partitionIdx = entGetPartitionIdx(e);
	if(!updateOverrideTarget && powInfo->curOverrideTargetRef)
	{
		Entity* overrideTarget = entFromEntityRef(partitionIdx, powInfo->curOverrideTargetRef);
		if(overrideTarget)
			return overrideTarget;
	}

	if(powInfo->overrideTargetTime != ABS_TIME_PARTITION(partitionIdx))
		powInfo->curOverrideTargetRef = 0;

	if(updateOverrideTarget && powConfig->targetOverride)
	{
		return aiPowersGetOverrideTarget(e, aib, target, powInfo, powConfig);
	}
	else if(powInfo->isSelfTarget)
		return e;
	else if(powInfo->isAttackPower && e==target)
	{
		// If current target is self, but power isn't a self target, just attack attack target
		return aib->attackTarget;
	}
	else if(powInfo->isTargetCreatorPower)
	{
		return entFromEntityRef(partitionIdx, e->erCreator);
	}
	else if(powInfo->isTargetOwnerPower)
	{
		return entFromEntityRef(partitionIdx, e->erOwner);
	}
	
	return target;
}

int aiPowersAllowedToExecutePower(Entity* e, AIVarsBase* aib, Entity* target, 
								  AIPowerInfo* powInfo, AIPowerConfig* powConfig, 
								  AIPowersExecuteFailureReason *pFailureReason)
{
	int allowed = true;
	Entity* powTarget;

	if (pFailureReason)
		*pFailureReason = AI_POWERS_EXECUTE_FAIL_OTHER;

	
	if(e == target && !powInfo->isSelfTarget && !powInfo->canAffectSelf)
	{
		AIPowersDebugSelectedPowDef(e->myRef, powInfo->power->hDef, "Disallowed to execute.\n\t"
									 "Self targeted but power cannot affect self.")
		return false;
	}

	// this power is currently being activated or is a toggle power that's already on
	if(powInfo->power->bActive)
	{
		if (pFailureReason)
			*pFailureReason = AI_POWERS_EXECUTE_FAIL_ISACTIVE;
		return false;
	}

	// a power is being charged - do not activate another
	if(e->pChar->eChargeMode)
		return false;

	if(powInfo->isLungePower)
	{
		Vec3 entPos;
		Vec3 targetPos;
		AICollideRayFlag flags;
		int yCapable = aiMovementGetFlying(e, aib);

		if(!target)
			return false;

		entGetPos(e, entPos);
		entGetPos(target, targetPos);
		flags = AICollideRayFlag_DOAVOIDCHECK;

		if(aiCollideRay(entGetPartitionIdx(e), e, entPos, target, targetPos, flags))
		{
			AIPowersDebugSelectedPowDef(e->myRef, powInfo->power->hDef, "Disallowed to execute.\n\t"
											"Lunge failed aiRaycast")
			return false;
		}
	}

	powTarget = aiPowersGetTarget(e, aib, target, powInfo, powConfig, false);

	if(powConfig->aiRequires &&
	   !aiCheckPowersRequiresExpr(e, aib, target, powInfo, powConfig->aiRequires,
			powConfig->filename))
	{
		AIPowersDebugSelectedPowDef(e->myRef, powInfo->power->hDef, "Disallowed to execute.\n\t"
										"Failed requires.")
		return false;
	}


	{
		ActivationFailureReason	queuefailedReason = kActivationFailureReason_None;

		if(!character_CanQueuePower(entGetPartitionIdx(e), e->pChar, powInfo->power, powTarget, NULL, NULL, NULL, NULL, NULL, NULL,
			pmTimestamp(0), -1, &queuefailedReason, false, true, true, NULL))
		{
			char *pcFailureReason = "unknown";
			if (pFailureReason)
			{	// for now, ignore these reasons the power can't be queued
				switch(queuefailedReason)
				{
					acase kActivationFailureReason_Recharge:
						*pFailureReason = AI_POWERS_EXECUTE_FAIL_RECHARGE;
						pcFailureReason = "Failed due to Recharge.";
					xcase kActivationFailureReason_Cooldown:
						*pFailureReason = AI_POWERS_EXECUTE_FAIL_COOLDOWN;
						pcFailureReason = "Failed due to Cooldown.";
					xcase kActivationFailureReason_Cost:
						*pFailureReason = AI_POWERS_EXECUTE_FAIL_COST;
						pcFailureReason = "Failed due to Cost.";
					xcase kActivationFailureReason_TargetLOSFailed:
						*pFailureReason = AI_POWERS_EXECUTE_FAIL_LOS;
						pcFailureReason = "Failed due to LOS";
					xcase kActivationFailureReason_TargetOutOfRange:
					acase kActivationFailureReason_TargetOutOfRangeMin:
						*pFailureReason = AI_POWERS_EXECUTE_FAIL_RANGE;
						pcFailureReason = "Failed due to range";
					xcase kActivationFailureReason_TargetImperceptible:
						*pFailureReason = AI_POWERS_EXECUTE_FAIL_PERCEIVE;
						pcFailureReason = "Failed due to perception (stealth/range)";
					xdefault:
						*pFailureReason = AI_POWERS_EXECUTE_FAIL_OTHER;
						pcFailureReason = "Failed due to Other.";
				}
			}

			AIPowersDebugSelectedPowDef(e->myRef, powInfo->power->hDef, "Disallowed to execute.\n\t"
											"Character cannot queue power: %s", pcFailureReason)
			return false;
		}
	}


	if (pFailureReason)
		*pFailureReason = AI_POWERS_EXECUTE_FAIL_NONE;

	return true;
}

int aiCheckOutOfCombatPowerTimeout(Entity *target, AIPowerInfo *powInfo)
{
	int partitionIdx = entGetPartitionIdx(target);
	if(powInfo->isOutOfCombatPower)
	{
		if(powInfo->isHealPower && ABS_TIME_SINCE_PARTITION(partitionIdx, target->aibase->ooc.timeLastHealed)<SEC_TO_ABS_TIME(5))
			return false;
		if(powInfo->isResPower && ABS_TIME_SINCE_PARTITION(partitionIdx, target->aibase->ooc.timeLastResed)<SEC_TO_ABS_TIME(5))
			return false;
		if(powInfo->isBuffPower && ABS_TIME_SINCE_PARTITION(partitionIdx, target->aibase->ooc.timeLastBuffed)<SEC_TO_ABS_TIME(5))
			return false;
	}

	return true;
}

AIPowerInfo* aiPowersCanUseOutOfCombatPowers(Entity* e, AIVarsBase* aib, Entity** targetOut,
											 AIPowerConfig** configOut)
{
	int i;
	AIPowerInfo* powInfo = NULL;
	Entity *target = NULL;
	AITeam* team = aiTeamGetAmbientTeam(e, aib);

	if(aib->powers->hasHealPowers || aib->powers->hasShieldHealPowers || aib->powers->hasResPowers)
	{
		AITeamMemberAssignment *pAssignment = aiTeamGetAssignmentForMember(team, e);
		if (pAssignment && pAssignment->powID)
		{
			powInfo = aiPowersFindInfoByID(e, aib, pAssignment->powID);
			if (powInfo)
			{
				if(configOut)
					*configOut = aiGetPowerConfig(e, aib, powInfo);
				if(targetOut)
					*targetOut = pAssignment->target->memberBE;
				return powInfo;
			}
		}
	}

	if(aiPowersPickBuffPowerAndTarget(e, aib, true, &powInfo, &target))
	{
		if(configOut)
			*configOut = aiGetPowerConfig(e, aib, powInfo);
		if(targetOut)
			*targetOut = target;

		return powInfo;
	}

	for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
	{
		AIPowerConfig* powConf;
		powInfo = aib->powers->powInfos[i];

		if(!powInfo->isOutOfCombatPower || 
			powInfo->isHealPower || 
			powInfo->isShieldHealPower ||
			powInfo->isBuffPower || 
			powInfo->isCurePower || 
			powInfo->isResPower)
			continue;	// don't use any OOC heals, buffs, cures or rezes, 
						// as they should be assigned by the team instead of automatically used

		powConf = aiGetPowerConfig(e, aib, powInfo);

		target = aiPowersGetTarget(e, aib, NULL, powInfo, powConf, true);

		if(!target || !aiPowersAllowedToExecutePower(e, aib, target, powInfo, powConf, NULL))
			continue;

		if(targetOut)
			*targetOut = target;
		if(configOut)
			*configOut = powConf;

		return powInfo;
	}

	return NULL;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(CanUseOutOfCombatPowers);
int exprFuncCanUseOutOfCombatPowers(ACMD_EXPR_SELF Entity* e)
{
	return !!aiPowersCanUseOutOfCombatPowers(e, e->aibase, NULL, NULL);
}

// Returns true if the entity is in currently in the process of performing an out of combat power
AUTO_EXPR_FUNC(ai) ACMD_NAME(IsPerformingOutOfCombatPower);
int exprFuncIsPerformingOutOfCombatPower(ACMD_EXPR_SELF Entity* e)
{
	return aiMultiTickAction_HasAction(e, e->aibase);
}

// This function should be used in the target override for out-of-combat powers to ensure
// that you select a valid target.
AUTO_EXPR_FUNC(ai, OpenMission) ACMD_NAME(EntCropInvalidOOCTargets);
void exprFuncEntCropInvalidOOCTargets(ExprContext *context, ACMD_EXPR_ENTARRAY_IN_OUT ents)
{
	int i;
	AIPowerInfo *powInfo = exprContextGetVarPointerPooled(context, powInfoString, parse_AIPowerInfo);

	for(i=0; i<eaSize(ents); i++)
	{
		Entity *e = (*ents)[i];

		if(!aiCheckOutOfCombatPowerTimeout(e, powInfo))
			eaRemoveFast(ents, i);
	}
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(UseOutOfCombatPowers);
void exprFuncUseOutOfCombatPowers(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDGenericSetData* mydata = getMyData(context, parse_FSMLDGenericSetData, (U64)"UseOutOfCombatPowers");
	AIVarsBase* aib = e->aibase;
	AIPowerInfo* bestInfo = NULL;
	AIPowerConfig* bestConf = NULL;
	Entity* target = NULL;
	AITeam* team = aiTeamGetAmbientTeam(e, aib);

	if(!mydata->setData)
	{
		CommandQueue* exitHandlers = NULL;

		exprContextGetCleanupCommandQueue(context, &exitHandlers, NULL);

		if(!exitHandlers)
		{
			estrPrintf(errString, "Unable to call UseOutOfCombatPowers in this section - missing exit handlers");
			return;
		}

		QueuedCommand_aiMovementResetPath(exitHandlers, e, aib);
		mydata->setData = true;
	}

	if(aiMultiTickAction_ProcessActions(e, aib))
		return;

	bestInfo = aiPowersCanUseOutOfCombatPowers(e, aib, &target, &bestConf);

	if(bestInfo)
	{
		F32 dist = entGetDistance(e, NULL, target, NULL, NULL);
		int partitionIdx = entGetPartitionIdx(e);

		if(bestInfo->isHealPower)
		{
			AITeamMember *pMember = aiTeamFindMemberByEntity(team, target);
			if (pMember)
				pMember->timeLastActedOn[AITEAM_ASSIGNMENT_TYPE_HEAL] = ABS_TIME_PARTITION(partitionIdx);
			target->aibase->ooc.timeLastHealed = ABS_TIME_PARTITION(partitionIdx);
		}
		if(bestInfo->isShieldHealPower)
		{// might be better to put this on the assignment?
			AITeamMember *pMember = aiTeamFindMemberByEntity(team, target);
			if (pMember)
				pMember->timeLastActedOn[AITEAM_ASSIGNMENT_TYPE_SHIELD_HEAL] = ABS_TIME_PARTITION(partitionIdx);
		}
		
		if(bestInfo->isResPower)
		{
			AITeamMember *pMember = aiTeamFindMemberByEntity(team, target);
			if (pMember)
				pMember->timeLastActedOn[AITEAM_ASSIGNMENT_TYPE_RESSURECT] = ABS_TIME_PARTITION(partitionIdx);
		}
		
		if(bestInfo->isBuffPower)
			target->aibase->ooc.timeLastBuffed = ABS_TIME_PARTITION(partitionIdx);

		if(!bestInfo->isInterruptedOnMovement && dist >= bestConf->minDist && dist <= bestConf->maxDist)
		{
			aiQueuePower(e, aib, bestInfo, target, target);
		}
		else
		{
			aiMultiTickAction_QueuePower(e, aib, target, AI_POWER_ACTION_OUT_OF_COMBAT, 
											bestInfo, MTAFlag_NONE, NULL);
		}
	}
}


static struct aiPowerConfigPreviousState{
	F32 absWeight;
	F32 minDist;
	F32 maxDist;
	U32 valid : 1;
}powerConfigPrevState;

static void aiPowerConfigRecordPreviousState(Entity* e, AIVarsBase* aib, AIPowerConfig* powConfig)
{
	devassert(!powerConfigPrevState.valid);
	powerConfigPrevState.absWeight = powConfig->absWeight;
	powerConfigPrevState.minDist = powConfig->minDist;
	powerConfigPrevState.maxDist = powConfig->maxDist;
	powerConfigPrevState.valid = true;
}

// --------------------------------------------------------------------------------------------------------------
static void aiPowerConfigUpdateOtherSystems(Entity* e, AIVarsBase* aib, AIPowerConfig* powConfig)
{
	devassert(powerConfigPrevState.valid);

	// do we need to recalculate the dynamic preferred range / powerflags
	if(powerConfigPrevState.absWeight != powConfig->absWeight ||
		powerConfigPrevState.minDist != powConfig->minDist ||
		powerConfigPrevState.maxDist != powConfig->maxDist)
	{
		aiPowersCalcDynamicPreferredRange(e, aib);

		if(powerConfigPrevState.absWeight == 0.f || powConfig->absWeight == 0.f)
		{
			aiRecompilePowerFlags(e, aib);
		}
	}

	powerConfigPrevState.valid = false;
}

// --------------------------------------------------------------------------------------------------------------
static void aiPowerConfigModApply(Entity* e, AIVarsBase* aib, AIPowerConfig* powConf, StructMod* mod, int updateOtherSystems)
{
	if(updateOtherSystems)
		aiPowerConfigRecordPreviousState(e, aib, powConf);

	structModApply(mod);

	if(updateOtherSystems)
		aiPowerConfigUpdateOtherSystems(e, aib, powConf);
}

// --------------------------------------------------------------------------------------------------------------
static void aiPowerConfigModReapplyAll(Entity* e, AIVarsBase* aib, AIPowerInfo* powInfo)
{
	AIPowerConfig* orig = aiGetOriginalPowerConfig(e, aib, powInfo);
	AIPowerConfig* localConfig = aiGetPowerConfig(e, aib, powInfo);
	int i, n;

	if(!eaSize(&powInfo->powerConfigMods))
	{
		aiPowerConfigRecordPreviousState(e, aib, (localConfig? localConfig : orig));
		
		if(powInfo->localModifiedPowConfig)
		{
			StructDestroy(parse_AIPowerConfig, powInfo->localModifiedPowConfig);
			powInfo->localModifiedPowConfig = NULL;
		}
		localConfig = orig;

		aiPowerConfigUpdateOtherSystems(e, aib, localConfig);
	}
	else
	{
		if(powInfo->localModifiedPowConfig)
		{
			localConfig = powInfo->localModifiedPowConfig;

			StructCopyAll(parse_AIPowerConfig, orig, localConfig);
		}
		else
			localConfig = aiPowerConfigGetModifiedConfig(e, aib, powInfo);

		aiPowerConfigRecordPreviousState(e, aib, localConfig);

		// order actually matters
		for(i = 0, n = eaSize(&aib->configMods); i < n; i++)
		{
			aiPowerConfigModApply(e, aib, localConfig, aib->configMods[i], false);
		}

		aiPowerConfigUpdateOtherSystems(e, aib, localConfig);
	}
}

// --------------------------------------------------------------------------------------------------------------
void aiPowerConfigModRemove(Entity* e, AIVarsBase* aib, int handle)
{
	int i;

	for(i=eaSize(&aib->powers->powInfos)-1; i>=0; i--)
	{
		AIPowerInfo *powInfo = aib->powers->powInfos[i];
		int j;
		int found = false;

		for(j=eaSize(&powInfo->powerConfigMods)-1; j>=0; j--)
		{
			StructMod *mod = powInfo->powerConfigMods[j];

			if(mod->id==handle)
			{
				found = true;

				structModDestroy(mod);
				eaRemove(&powInfo->powerConfigMods, j);
				break;
			}
		}

		if(found)
		{
			aiPowerConfigModReapplyAll(e, aib, powInfo);
			break;
		}
	}
}

// --------------------------------------------------------------------------------------------------------------
AUTO_COMMAND_QUEUED();
void aiPowerConfigModRemoveMods(ACMD_POINTER Entity* e, ACMD_POINTER FSMLDAddStructModTagSet *modSet)
{
	AIVarsBase *aib = e->aibase;
	int found = false;
	S32 i;

	if(entCheckFlag(e, ENTITYFLAG_DESTROY | ENTITYFLAG_PLAYER_LOGGING_OUT) || !aib)
		return;		// AI Destroy has already cleaned up the configmods and reapplication is moot

	for (i = eaiSize(&modSet->ids)-1; i >= 0; --i)
	{
		int handle = modSet->ids[i];
		aiPowerConfigModRemove(e, aib, handle);
	}
}

StructMod* aiPowerConfigModFind(Entity* e, AIVarsBase* aib, AIPowerInfo* powInfo, int id)
{
	int i;

	for(i=eaSize(&powInfo->powerConfigMods)-1; i>=0; i--)
	{
		if(powInfo->powerConfigMods[i]->id==id)
			return powInfo->powerConfigMods[i];
	}

	return NULL;
}

int aiPowerConfigModAddFromString(Entity* e, AIVarsBase* aib, AIPowerInfo* powInfo, const char* relObjPath, const char* val, ACMD_EXPR_ERRSTRING errString)
{
	AIPowerConfig* powConf = aiPowerConfigGetModifiedConfig(e, aib, powInfo);
	StructMod* mod;
	StructMod lookup = {0};
	static int s_id = 0;

	if(!structModResolvePath(powConf, parse_AIPowerConfig, relObjPath, &lookup))
	{
		if(errString)
			estrPrintf(errString, "%s does not resolve to a valid AIConfig setting, please check the wiki for the correct name", relObjPath);
		return 0;
	}

	mod = structModCreate();

	*mod = lookup;

	mod->id = ++s_id;
	if (s_id == 0) s_id = 1;

	mod->val = allocAddString(val);
	mod->name = allocAddString(relObjPath);

	eaPush(&powInfo->powerConfigMods, mod);
	aiPowerConfigModApply(e, aib, powConf, mod, true);
	return mod->id;
}

int aiPowerConfigCheckSettingName(const char* setting)
{
	return structModResolvePath(NULL, parse_AIPowerConfig, setting, NULL);
}

// Modifies a power config
AUTO_EXPR_FUNC(ai) ACMD_NAME(AddPowerConfigMod);
ExprFuncReturnVal exprFuncAddPowerConfigMod(ACMD_EXPR_SELF Entity *e, ExprContext *context, ACMD_EXPR_DICT(PowerDef) const char* powerDefName, const char *objPath, const char *val, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDAddStructMod* mydata = getMyData(context, parse_FSMLDAddStructMod, PTR_TO_UINT(objPath));
	AIVarsBase *aib = e->aibase;
	AIPowerInfo *powInfo = aiPowersFindInfo(e, aib, powerDefName);
	StructMod *mod = NULL;

	if(!powInfo)
	{
		estrPrintf(errString, "Trying to mod a power critter doesn't have: %s", powerDefName);
		return ExprFuncReturnError;
	}

	mod = aiPowerConfigModFind(e, aib, powInfo, mydata->id);
	if(!mod)
		mydata->id = aiPowerConfigModAddFromString(e, aib, powInfo, objPath, val, errString);
	else if(stricmp(mod->val, val))
	{
		aiPowerConfigModRemove(e, aib, mydata->id);
		mydata->id = aiPowerConfigModAddFromString(e, aib, powInfo, objPath, val, errString);
	}

	if(!mydata->id)
		return ExprFuncReturnError;

	return ExprFuncReturnFinished;
}

// --------------------------------------------------------------------------------------------------------------
void aiAddPowerConfigModByTagBit(Entity *e, AIVarsBase *aib, U32 **idsOut, int powerAITagBitRequire, int powerAITagBitExclude, const char *objPath, const char *val)
{
	int i;

	for(i=eaSize(&aib->powers->powInfos)-1; i>=0; i--)
	{
		AIPowerInfo *powInfo = aib->powers->powInfos[i];
		PowerDef *pdef = GET_REF(powInfo->power->hDef);

		if(pdef)
		{
			U32 powDataBits = powInfo->aiTagBits & ~kPowerAITag_AllCode;
			int require = !powerAITagBitRequire || (powDataBits & powerAITagBitRequire);
			int exclude = !powerAITagBitExclude || !(powDataBits & powerAITagBitExclude);
			if(powDataBits && require && exclude)
			{
				U32 id = aiPowerConfigModAddFromString(e, aib, powInfo, objPath, val, NULL);

				ea32Push(idsOut, id);
			}
		}
	}
}

// --------------------------------------------------------------------------------------------------------------
void aiAddPowerConfigModByTag(Entity *e, AIVarsBase *aib, U32 **idsOut, const char* tagRequire, const char* tagExclude, const char *objPath, const char *val)
{
	int tagRequireBit = StaticDefineIntGetInt(PowerAITagsEnum, tagRequire);
	int tagExcludeBit = StaticDefineIntGetInt(PowerAITagsEnum, tagExclude);

	if(tagRequireBit==-1)
		tagRequireBit = 0;
	if(tagExcludeBit==-1)
		tagExcludeBit = 0;

	aiAddPowerConfigModByTagBit(e, aib, idsOut, tagRequireBit, tagExcludeBit, objPath, val);
}

// --------------------------------------------------------------------------------------------------------------
static int aiPowers_AddPowerConfigModByPowerCategory(	Entity *e, 
														int **idsOut, 
														const char* powerCategory,
														const char* objPath,
														const char* val, 
														bool bMatchCategory)
{
	int iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,powerCategory);
	if (iCategory == -1)
		return false;
	
	FOR_EACH_IN_EARRAY(e->aibase->powers->powInfos, AIPowerInfo, powInfo)
	{
		Power* pPower = powInfo->power;
		PowerDef *pPowerDef = GET_REF(pPower->hDef);
		if (pPowerDef)
		{
			S32 hasCategory = eaiFind(&pPowerDef->piCategories, iCategory) >= 0;
			if ((bMatchCategory && hasCategory) || (!bMatchCategory && !hasCategory))
			{
				U32 id = aiPowerConfigModAddFromString(e, e->aibase, powInfo, objPath, val, NULL);
				if (idsOut)
					eaiPush(idsOut, id);
			}
		}
	}
	FOR_EACH_END

	return true;
}


// --------------------------------------------------------------------------------------------------------------
static int aiPowers_AddPowerConfigModByPowerTag(Entity *e, 
												int **idsOut, 
												const char* powerTag,
												const char* objPath,
												const char* val, 
												bool bMatchTag)
{
	int iTag = StaticDefineIntGetInt(PowerTagsEnum,powerTag);
	if (iTag == -1)
		return false;
	
	FOR_EACH_IN_EARRAY(e->aibase->powers->powInfos, AIPowerInfo, powInfo)
	{
		Power* pPower = powInfo->power;
		PowerDef *pPowerDef = GET_REF(pPower->hDef);
		if (pPowerDef)
		{
			S32 hasTag = eaiFind(&pPowerDef->tags.piTags, iTag) >= 0;
			if ((bMatchTag && hasTag) || (!bMatchTag && !hasTag))
			{
				U32 id = aiPowerConfigModAddFromString(e, e->aibase, powInfo, objPath, val, NULL);
				if (idsOut)
					eaiPush(idsOut, id);
			}
		}
	}
	FOR_EACH_END

	return true;
}

// --------------------------------------------------------------------------------------------------------------

// Modifies all power configs with the given powerAI tag
AUTO_EXPR_FUNC(ai) ACMD_NAME(AddPowerConfigModByTag);
ExprFuncReturnVal exprFuncAddPowerConfigModByTag(ACMD_EXPR_SELF Entity *e, ExprContext *context, const char* powerAITag, const char *objPath, const char *val, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDAddStructModTagSet* mydata = getMyData(context, parse_FSMLDAddStructModTagSet, PTR_TO_UINT(objPath));
	AIVarsBase *aib = e->aibase;
	int tagBit = StaticDefineIntGetInt(PowerAITagsEnum, powerAITag);

	if(tagBit==-1)
	{
		estrPrintf(errString, "Could not find Power AI Tag: %s", powerAITag);
		return ExprFuncReturnError;
	}

	if(!aiPowerConfigCheckSettingName(objPath))
	{
		estrPrintf(errString, "Could not find objPath: %s", objPath);
		return ExprFuncReturnError;
	}

	if(!mydata->dataIsSet)
	{
		mydata->dataIsSet = 1;
		mydata->tagBit = tagBit;

		aiAddPowerConfigModByTagBit(e, aib, &mydata->ids, tagBit, 0, objPath, val);
	}

	return ExprFuncReturnFinished;
}

// Modifies a power config for the current state
AUTO_EXPR_FUNC(ai) ACMD_NAME(AddPowerConfigModCurState);
ExprFuncReturnVal exprFuncAddPowerConfigModCurState(ACMD_EXPR_SELF Entity *e, ExprContext *context, ACMD_EXPR_DICT(PowerDef) const char* powerDefName, const char *objPath, const char *val, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDAddStructMod* mydata = getMyData(context, parse_FSMLDAddStructMod, PTR_TO_UINT(objPath));

	if(!mydata->dataIsSet)
	{
		AIVarsBase *aib = e->aibase;
		AIPowerInfo *powInfo = aiPowersFindInfo(e, aib, powerDefName);
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;

		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);

		if(!exitHandlers)
		{
			estrPrintf(errString, "Trying to call CurState func without exitHandler reference");
			return ExprFuncReturnError;
		}

		if(!powInfo)
		{
			estrPrintf(errString, "Trying to mod a power critter doesn't have: %s", powerDefName);
			return ExprFuncReturnError;
		}

		mydata->dataIsSet = 1;
		mydata->id = aiPowerConfigModAddFromString(e, aib, powInfo, objPath, val, errString);

		if(!mydata->id)
			return ExprFuncReturnError;

		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDAddStructMod, localData, PTR_TO_UINT(objPath));
		QueuedCommand_aiConfigModRemove(exitHandlers, e, aib, mydata->id);
	}

	return ExprFuncReturnFinished;
}


// --------------------------------------------------------------------------------------------------------------
typedef int (*AddPowerConfigModCB)(Entity *e, int **idsOut, const char* enumTag, const char* objPath,const char* val, bool bMatch);

static ExprFuncReturnVal aiPowers_ExprAddPowerConfigModHelper(	Entity *e, 
																ExprContext *context, 
																const char *enumTag, 
																const char *objPath, 
																const char *val, 
																int onMatchingPowerCategory,
																int curStateOnly,
																ACMD_EXPR_ERRSTRING errString,
																AddPowerConfigModCB fp)
{
	FSMLDAddStructModTagSet* mydata;

	if (!enumTag || !objPath || !val)
		return ExprFuncReturnError;

	mydata = getMyData(context, parse_FSMLDAddStructModTagSet, PTR_TO_UINT(objPath));
	if(!mydata->dataIsSet)
	{
		AIVarsBase *aib = e->aibase;
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;

		if (curStateOnly)
		{
			exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);
			if(!exitHandlers)
			{
				estrPrintf(errString, "Trying to call CurState func without exitHandler reference");
				return ExprFuncReturnError;
			}
		}

		if (!fp(e, &mydata->ids, enumTag, objPath, val, onMatchingPowerCategory))
		{
			deleteMyData(context, parse_FSMLDAddStructModTagSet, localData, PTR_TO_UINT(objPath));
			estrPrintf(errString, "Trying to call CurState func without exitHandler reference");
			return ExprFuncReturnError;
		}
		mydata->dataIsSet = 1;

		if (exitHandlers)
		{
			QueuedCommand_aiPowerConfigModRemoveMods(exitHandlers, e, mydata);
			QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDAddStructModTagSet, localData, PTR_TO_UINT(objPath));
		}

		return ExprFuncReturnFinished;
	}
	return ExprFuncReturnFinished;
}

// --------------------------------------------------------------------------------------------------------------

// Adds a power mod to all powers of an appropriate power category
// if matchPowerCategory is true, it will apply the mod to all powers that match the category
//	if matchPowerCategory is false, it will apply the mod to all powers that do not match the category
// curStateOnly: if true, will only apply the mods for the current FSM state
AUTO_EXPR_FUNC(ai) ACMD_NAME(AddPowerConfigModByPowerCategory);
ExprFuncReturnVal exprFuncAddPowerConfigModByPowerCategory(ACMD_EXPR_SELF Entity *e, 
														   ExprContext *context, 
														   const char *powerCategory, 
														   const char *objPath, 
														   const char *val, 
														   int matchPowerCategory,
														   int curStateOnly,
														   ACMD_EXPR_ERRSTRING errString)
{
	return aiPowers_ExprAddPowerConfigModHelper(e, 
												context, 
												powerCategory, 
												objPath, 
												val, 
												matchPowerCategory, 
												curStateOnly, 
												errString, 
												aiPowers_AddPowerConfigModByPowerCategory);
}

// --------------------------------------------------------------------------------------------------------------

// Adds a power mod to all powers of an appropriate powerTag
// if matchPowerTag is true, it will apply the mod to all powers that match the category
//	if matchPowerTag is false, it will apply the mod to all powers that do not match the category
// curStateOnly: if true, will only apply the mods for the current FSM state
AUTO_EXPR_FUNC(ai) ACMD_NAME(AddPowerConfigModByPowerTag);
ExprFuncReturnVal exprFuncAddPowerConfigModByPowerTag(ACMD_EXPR_SELF Entity *e, 
														   ExprContext *context, 
														   const char *powerTag, 
														   const char *objPath, 
														   const char *val, 
														   int matchPowerTag,
														   int curStateOnly,
														   ACMD_EXPR_ERRSTRING errString)
{
	return aiPowers_ExprAddPowerConfigModHelper(e, 
												context, 
												powerTag, 
												objPath, 
												val, 
												matchPowerTag, 
												curStateOnly, 
												errString, 
												aiPowers_AddPowerConfigModByPowerTag);
}

void aiPowerClearAllPowerConfigMods(Entity *e, AIVarsBase *aib, AIPowerInfo *powInfo)
{
	eaClearEx(&powInfo->powerConfigMods, structModDestroy);
	if(powInfo->localModifiedPowConfig)
	{
		StructDestroy(parse_AIPowerConfig, powInfo->localModifiedPowConfig);
		powInfo->localModifiedPowConfig = NULL;
	}
}

// Clears all ConfigMods
AUTO_EXPR_FUNC(ai) ACMD_NAME(ClearAllPowerConfigMods);
void exprFuncClearAllPowerConfigMods(ACMD_EXPR_SELF Entity* e)
{
	AIVarsBase* aib = e->aibase;
	int i;

	for(i=eaSize(&aib->powers->powInfos)-1; i>=0; i--)
	{
		AIPowerInfo *powInfo = aib->powers->powInfos[i];

		aiPowerClearAllPowerConfigMods(e, aib, powInfo);
	}
}

void aiPowersGetCureTagUnionList(Entity *e, AIVarsBase *aib, U32 **ppeaInOutList)
{
	FOR_EACH_IN_EARRAY(aib->powers->powInfos, AIPowerInfo, pPowInfo)
		if(pPowInfo->isCurePower)
		{
			AIPowerConfig* pConfig = aiGetPowerConfig(e, aib, pPowInfo);
			S32 i;
			for(i = eaiSize(&pConfig->curePowerTags) - 1; i >= 0; i--)
			{
				eaiPushUnique(ppeaInOutList, pConfig->curePowerTags[i]);
			}
		}
	FOR_EACH_END
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void aiSetPowerAutoCastForPet(Entity* playerEnt, EntityRef petRef, const char *pchName, int disableAutocast)
{
	Entity *petEnt = entFromEntityRef(entGetPartitionIdx(playerEnt), petRef);

	if (playerEnt && playerEnt->pChar && playerEnt->pPlayer && petEnt && petEnt->pChar && petEnt->aibase && petEnt->erOwner==entGetRef(playerEnt))
	{
		S32 modIdx, count;
		AIVarsBase *aib = petEnt->aibase;
		AIAutoCastPowers *pAutoCastMod = NULL;
		Power *ppow;
		PowerDef *pdef;
		PetPowerState *pState;

		AIPowerInfo* aiPowInfo = aiPowersFindInfo(petEnt, aib, pchName);
		if (!aiPowInfo || !aiPowInfo->power)
			return;

		ppow = aiPowInfo->power;
		pdef = GET_REF(ppow->hDef);

		count = eaSize(&aib->autocastPowers);
		for(modIdx = 0; modIdx < count; modIdx++)
		{
			AIAutoCastPowers *p = aib->autocastPowers[modIdx];
			if(p->powId == ppow->uiID)
			{
				pAutoCastMod = p;
				break;
			}
		}

		if (pAutoCastMod)
		{
			if (disableAutocast)
				return; // auto-cast already disabled for this power

			aiPowerConfigModRemove(petEnt, aib, pAutoCastMod->configModId);
			eaRemoveFast(&aib->autocastPowers, modIdx);
			free(pAutoCastMod);

			// Update the flag on the PetPowerState
			pState = player_GetPetPowerState(playerEnt->pPlayer,petRef,pdef);
			if(pState)
			{
				// todo: Put the autocharge in a persistent place
				pState->bAIUsageDisabled = false;
				entity_SetDirtyBit(playerEnt,parse_Player,playerEnt->pPlayer,false);
			}

		}
		else if (disableAutocast)
		{
			U32 modId;

			modId = aiPowerConfigModAddFromString(petEnt, aib, aiPowInfo, "Weight:", "0", NULL);
			if (modId)
			{
				pAutoCastMod = (AIAutoCastPowers*)malloc(sizeof(AIAutoCastPowers));
				pAutoCastMod->powId = ppow->uiID;
				pAutoCastMod->configModId = modId;
				eaPush(&aib->autocastPowers, pAutoCastMod);

				// Update the flag on the PetPowerState
				pState = player_GetPetPowerState(playerEnt->pPlayer,petRef,pdef);
				if(pState)
				{
					// todo: Put the autocharge in a persistent place
					pState->bAIUsageDisabled = true;
					entity_SetDirtyBit(playerEnt,parse_Player,playerEnt->pPlayer,false);
				}
			}
		}
	}

}

bool isValidTargetForPower(Entity *e, Entity *pTarget, const AIPowerInfo *powInfo, PowerTarget *powerTarget)
{
	if (character_TargetMatchesPowerType(entGetPartitionIdx(e), e->pChar, pTarget->pChar, powerTarget))
		return true;

	// to handle PBAoE, as the powerTarget is SELF and the above check will fail
	if (powInfo->isAttackPower && critter_IsKOS(entGetPartitionIdx(e), e, pTarget) && powInfo->isSelfTarget)
		return true;

	return false;
}

Entity* aiPetGetTarget(Entity* pOwner, Entity *petEnt, AIPowerInfo *powInfo, const PowerDef *powDef)
{
	Entity* pTarget = NULL;
	PowerTarget *powerTarget = GET_REF(powDef->hTargetMain);
	if(! powerTarget)
		return NULL;

	if (pOwner )
	{
		if (pOwner->pChar->currentTargetRef)
		{
			pTarget = entFromEntityRef(entGetPartitionIdx(pOwner), pOwner->pChar->currentTargetRef);
			if (pTarget && isValidTargetForPower(petEnt, pTarget, powInfo, powerTarget))
			{
				return pTarget;
			}
		}
		if (pOwner->pChar->erTargetDual)
		{
			pTarget = entFromEntityRef(entGetPartitionIdx(pOwner), pOwner->pChar->erTargetDual);
			if (pTarget && isValidTargetForPower(petEnt, pTarget, powInfo, powerTarget))
			{
				return pTarget;
			}
		}
	}
	
	if (petEnt->pChar->currentTargetRef)
	{
		pTarget = entFromEntityRef(entGetPartitionIdx(pOwner), petEnt->pChar->currentTargetRef);
		if (pTarget && isValidTargetForPower(petEnt, pTarget, powInfo, powerTarget))
		{
			return pTarget;
		}
	}

	// player target and pet's current target are both not valid for power
	// choose best target
	if (powerTarget->bAllowFoe)
	{
		AIVarsBase *aib = petEnt->aibase;
		AITeam* combatTeam = aiTeamGetCombatTeam(petEnt, aib);
		// if I'm in combat, choose my current attack target
		if (!combatTeam || combatTeam->combatState != AITEAM_COMBAT_STATE_FIGHT)
			return NULL;

		return aib->attackTarget;
	}

	// try targeting the player
	if (pOwner && powerTarget->bAllowFriend && isValidTargetForPower(petEnt, pOwner, powInfo, powerTarget))
		return pOwner;
	// try targeting yourself
	if (powerTarget->bAllowSelf && isValidTargetForPower(petEnt, petEnt, powInfo, powerTarget))
		return petEnt;

	return NULL;
}

bool aiPetCanQueuePower(Entity* e, AIPowerInfo *aiPowInfo, Entity *pTarget)
{
	// for now keeping it simple for whether or not the power can be queued
	// just look at the recharge time
	return (power_GetRecharge(aiPowInfo->power) <= 0.f);

	//ActivationFailureReason	queuefailedReason = kActivationFailureReason_Other;
	//if (aiPowInfo->isSelfTarget)
	//	pTarget = e;
	//return NULL != character_CanQueuePower(e->pChar, aiPowInfo->power, pTarget, NULL, NULL, NULL, NULL,
	//								pmTimestamp(0), -1, &queuefailedReason, false, true);
}



int aiPowerForceUsePower(Entity* e, const char *pchName, MultiTickActionClearedCallback cb)
{
	if (e && e->aibase )
	{
		Entity *pOwner; 
		Entity *pTarget;
		PowerDef* powerDef;
		AIPowerConfig* powerConfig;
		AIPowerInfo* aiPowInfo = aiPowersFindInfo(e, e->aibase, pchName);

		pOwner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);

		if (!aiPowInfo)
			return false;

		powerConfig = aiGetPowerConfig(e, e->aibase, aiPowInfo);
		if (!powerConfig)
			return false;

		powerDef = (PowerDef*)GET_REF(aiPowInfo->power->hDef);
		if (!powerDef)
			return false;

		// check if the power is already being activated or on cooldown?
		pTarget = aiPetGetTarget(pOwner, e, aiPowInfo, powerDef);

		if (!aiPetCanQueuePower(e, aiPowInfo, pTarget))
			return false;

		return aiMultiTickAction_QueuePower(e, e->aibase, pTarget, 
											AI_POWER_ACTION_USE_POWINFO, aiPowInfo, 
											(MTAFlag_FORCEUSETARGET | MTAFlag_USERFORCEDACTION), 
											cb);

		return true;
	}

	return false;
}


#include "aiPowers_h_ast.c"
