#include "dynAnimChart.h"

#include "error.h"
#include "fileutil.h"
#include "foldercache.h"
#include "StringCache.h"
#include "ResourceManager.h"
#include "NameList.h"
#include "rand.h"
#include "SimpleParser.h"
#include "MemoryPool.h"

#include "dynAnimGraph.h"
#include "dynAnimTemplate.h"
#include "dynSeqData.h"
#include "dynSkeleton.h"
#include "dynSkeletonMovement.h"
#include "dynMove.h"
#include "dynMoveTransition.h"

#include "dynAnimChart_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

MP_DEFINE(DynAnimChartStack);

DynAnimStancesList stance_list;

static int bLoadedOnce = false;
static int bLoadedOnceStances = false;

DictionaryHandle hAnimChartDictLoadTime;
DictionaryHandle hAnimChartDictRunTime;
DictionaryHandle hStanceDict;

static const char* pcAnimChartDefaultKey;
static const char* pcTagDetail;
static const char* pcTagMovement;
static const char* pcTagMovementKeyword;
static const char* pcStanceMoving;
static const char* pcStanceJumping;
static const char* pcStanceFalling;
static const char* pcStanceRising;

AUTO_RUN;
void initDynAnimChartStackPools(void)
{
	MP_CREATE(DynAnimChartStack, 128);
}

int dynAnimCompareStanceWordPriority(const void **pa, const void **pb)
{
	const char *a = *(const char **)pa;
	const char *b = *(const char **)pb;
	F32 fa = dynAnimStancePriority(a);
	F32 fb = dynAnimStancePriority(b);
	if (fa < fb)
		return 1;
	if (fa > fb)
		return -1;
	return 0;
}

int dynAnimCompareTimedStancesPriority(const void **pa, const void **pb)
{
	const DynAnimTimedStance *a = *(const DynAnimTimedStance **)pa;
	const DynAnimTimedStance *b = *(const DynAnimTimedStance **)pb;
	F32 fa = dynAnimStancePriority(a->pcName);
	F32 fb = dynAnimStancePriority(b->pcName);
	if (fa < fb)
		return 1;
	else if (fa > fb)
		return -1;
	else { //fa == fb
		if (a->fTime < b->fTime)
			return 1;
		else if (a->fTime > b->fTime)
			return -1;
	}
	return 0;
}

static int cmpStanceWordsPriority(const char **eaStanceWordsA, const char **eaStanceWordsB, U32 uiPreferMore, U32 uiIgnoreDetails, U32 uiIgnoreMovementInA, U32 uiIgnoreMovementInB)
{
	int i = 0, j = 0;
	int sign = uiPreferMore ? -1 : 1;
	bool bEarlyStanceFind = uiIgnoreDetails || uiIgnoreMovementInA || uiIgnoreMovementInB;

	do
	{
		F32 fa, fb;

		if (bEarlyStanceFind)
		{
			DynAnimStanceData *sdataA = NULL;
			DynAnimStanceData *sdataB = NULL;
			
			if (uiIgnoreMovementInA && !uiIgnoreMovementInB)
			{
				while(	j < eaSize(&eaStanceWordsB) &&
						(	!stashFindPointer(stance_list.stStances, eaStanceWordsB[j], &sdataB)	||
							uiIgnoreDetails     && sdataB->pcTag == pcTagDetail						||
							uiIgnoreMovementInB && (sdataB->pcTag == pcTagMovementKeyword || sdataB->pcTag == pcTagMovement)))
				{
					j++;
				}
				fb = SAFE_MEMBER(sdataB, fStancePriority);

				while(	i < eaSize(&eaStanceWordsA) &&
						(	!stashFindPointer(stance_list.stStances, eaStanceWordsA[i], &sdataA)	||
							uiIgnoreDetails     && sdataA->pcTag == pcTagDetail						||
							uiIgnoreMovementInA && (sdataA->pcTag == pcTagMovementKeyword || sdataA->pcTag == pcTagMovement) && (uiIgnoreMovementInB || sdataA != sdataB)))
				{
					i++;
				}
				fa = SAFE_MEMBER(sdataA, fStancePriority);
			}
			else
			{
				while(	i < eaSize(&eaStanceWordsA) &&
						(	!stashFindPointer(stance_list.stStances, eaStanceWordsA[i], &sdataA)	||
							uiIgnoreDetails     && sdataA->pcTag == pcTagDetail						||
							uiIgnoreMovementInA && (sdataA->pcTag == pcTagMovementKeyword || sdataA->pcTag == pcTagMovement)))
				{
					i++;
				}
				fa = SAFE_MEMBER(sdataA, fStancePriority);

				while(	j < eaSize(&eaStanceWordsB) &&
						(	!stashFindPointer(stance_list.stStances, eaStanceWordsB[j], &sdataB)	||
							uiIgnoreDetails     && sdataB->pcTag == pcTagDetail						||
							uiIgnoreMovementInB && (sdataB->pcTag == pcTagMovementKeyword || sdataB->pcTag == pcTagMovement) && (uiIgnoreMovementInA || sdataA != sdataB)))
				{
					j++;
				}
				fb = SAFE_MEMBER(sdataB, fStancePriority);
			}
		}

		if (i >= eaSize(&eaStanceWordsA))
			if (j >= eaSize(&eaStanceWordsB))
				return 0; // Identical! If we're comparing animation chart stances then this shouldn't be allowed but might exist with bad data.. if we're checking a skeleton's stances this is ok
			else
				return -1*sign; // B has more stance words
		if (j >= eaSize(&eaStanceWordsB))
			return 1*sign; // A has more stance words

		if (eaStanceWordsA[i] == eaStanceWordsB[j])
		{
			// Highest priority stance words are the same
			// Check the next one
			i++;
			j++;
			continue;
		}
		// Get priorities
		if (!bEarlyStanceFind)
		{
			fa = dynAnimStancePriority(eaStanceWordsA[i]);
			fb = dynAnimStancePriority(eaStanceWordsB[j]);
		}
		if (fa == fb)
		{
			// Identical priorities, an error should have been fired elsewhere
			i++;
			j++;
			continue;
		} else if (fa > fb)
			return 1*sign;
		else
			return -1*sign;
	} while (true);
}

int dynAnimCompareStanceWordsPriorityLarge(const char **eaStanceWordsA, const char **eaStanceWordsB, U32 uiIgnoreDetails, U32 uiIgnoreMovementInA, U32 uiIgnoreMovementInB)
{
	return cmpStanceWordsPriority(eaStanceWordsA, eaStanceWordsB, 1, uiIgnoreDetails, uiIgnoreMovementInA, uiIgnoreMovementInB);
}

int dynAnimCompareStanceWordsPrioritySmall(const char **eaStanceWordsA, const char **eaStanceWordsB, U32 uiIgnoreDetails, U32 uiIgnoreMovementInA, U32 uiIgnoreMovementInB)
{
	return cmpStanceWordsPriority(eaStanceWordsA, eaStanceWordsB, 0, uiIgnoreDetails, uiIgnoreMovementInA, uiIgnoreMovementInB);
}

static int cmpTimedStancesPriority(const DynAnimTimedStance **eaTimedStancesA, const DynAnimTimedStance **eaTimedStancesB, bool bPreferMore)
{
	int i=0;
	int sign = bPreferMore?-1:1;
	do
	{
		F32 fa, fb;
		if (i >= eaSize(&eaTimedStancesA))
			if (i >= eaSize(&eaTimedStancesB))
				return 0; // Identical!  Shouldn't be allowed, but might exist with bad data
			else
				return -1*sign; // B has more stance words
		if (i >= eaSize(&eaTimedStancesB))
			return 1*sign; // A has more stance words
		if (eaTimedStancesA[i]->pcName == eaTimedStancesB[i]->pcName &&
			eaTimedStancesA[i]->fTime  == eaTimedStancesB[i]->fTime)
		{
			// Highest priority stance words are the same
			// Check the next one
			i++;
			continue;
		}
		// Get priorities
		fa = dynAnimStancePriority(eaTimedStancesA[i]->pcName);
		fb = dynAnimStancePriority(eaTimedStancesB[i]->pcName);
		if (fa == fb)
		{
			if (eaTimedStancesA[i]->fTime > eaTimedStancesB[i]->fTime)
				return 1*sign; // A has a higher time requirement
			if (eaTimedStancesB[i]->fTime > eaTimedStancesA[i]->fTime)
				return -1*sign; // B has a higher time requirement

			// Identical priorities, an error should have been fired elsewhere
			i++;
			continue;
		} else if (fa > fb)
			return 1*sign;
		else
			return -1*sign;
	} while (true);
}

int dynAnimCompareTimedStancesPriorityLarge(const DynAnimTimedStance **eaTimedStancesA, const DynAnimTimedStance **eaTimedStancesB)
{
	return cmpTimedStancesPriority(eaTimedStancesA, eaTimedStancesB, true);
}

int dynAnimCompareTimedStancesPrioritySmall(const DynAnimTimedStance **eaTimedStancesA, const DynAnimTimedStance **eaTimedStancesB)
{
	return cmpTimedStancesPriority(eaTimedStancesA, eaTimedStancesB, false);
}

int cmpJointStancesPriority(const char **eaStanceWordsA, const DynAnimTimedStance **eaTimedStancesA,
							const char **eaStanceWordsB, const DynAnimTimedStance **eaTimedStancesB,
							bool bPreferMore)
{
	int ia = 0, ja = 0;
	int ib = 0, jb = 0;
	int sign = bPreferMore?-1:1;
	F32 fia = ia < eaSize(&eaStanceWordsA)  ? dynAnimStancePriority(eaStanceWordsA[ia])			: -1.0f;
	F32 fja = ja < eaSize(&eaTimedStancesA) ? dynAnimStancePriority(eaTimedStancesA[ja]->pcName): -1.0f;
	F32 fib = ib < eaSize(&eaStanceWordsB)  ? dynAnimStancePriority(eaStanceWordsB[ib])			: -1.0f;
	F32 fjb = jb < eaSize(&eaTimedStancesB) ? dynAnimStancePriority(eaTimedStancesB[jb]->pcName): -1.0f;
	do
	{
		bool aTimed, bTimed;
		F32 fa, fb;

		if (ia >= eaSize(&eaStanceWordsA) && ja >= eaSize(&eaTimedStancesA))
			if (ib >= eaSize(&eaStanceWordsB) && jb >= eaSize(&eaTimedStancesB))
				return 0; //Identical! Shouldn't be allowed, but might exist with bad data
			else
				return -1*sign; //B has more stance words
		if (ib >= eaSize(&eaStanceWordsB) && jb >= eaSize(&eaTimedStancesB))
			return 1*sign; //A has more stance words

		if (fja >= fia) {
			aTimed = true;
			fa = fja;
		} else {
			aTimed = false;
			fa = fia;
		}

		if (fjb >= fib) {
			bTimed = true;
			fb = fjb;
		} else {
			bTimed = false;
			fb = fib;
		}

		if (fa == fb)
		{
			if (aTimed && bTimed) {
				if (eaTimedStancesA[ja]->fTime > eaTimedStancesB[jb]->fTime)
					return 1*sign; //A has a higher time requirement
				if (eaTimedStancesB[jb]->fTime > eaTimedStancesA[ja]->fTime)
					return -1*sign; //B has a higher time requirement
				fja = ++ja < eaSize(&eaTimedStancesA) ? dynAnimStancePriority(eaTimedStancesA[ja]->pcName) : -1.0f;
				fjb = ++jb < eaSize(&eaTimedStancesB) ? dynAnimStancePriority(eaTimedStancesB[jb]->pcName) : -1.0f;

			} else if (aTimed) {
				if (eaTimedStancesA[ja]->fTime > 0.0f)
					return 1*sign; //A has a higher time requirement
				fja = ++ja < eaSize(&eaTimedStancesA) ? dynAnimStancePriority(eaTimedStancesA[ja]->pcName) : -1.0f;
				fib = ++ib < eaSize(&eaStanceWordsB)  ? dynAnimStancePriority(eaStanceWordsB[ib]) : -1.0f;

			} else if (bTimed) {
				if (eaTimedStancesB[jb]->fTime > 0.0f)
					return -1*sign; //B has a higher time requirement
				fia = ++ia < eaSize(&eaStanceWordsA)  ? dynAnimStancePriority(eaStanceWordsA[ia]) : -1.0f;
				fjb = ++jb < eaSize(&eaTimedStancesB) ? dynAnimStancePriority(eaTimedStancesB[jb]->pcName) : -1.0f;
			
			} else { //neither is timed
				fia = ++ia < eaSize(&eaStanceWordsA)  ? dynAnimStancePriority(eaStanceWordsA[ia]) : -1.0f;
				fib = ++ib < eaSize(&eaStanceWordsB)  ? dynAnimStancePriority(eaStanceWordsB[ib]) : -1.0f;
			}
		} else if (fa > fb)
			return 1*sign; //A has higher priority
		else
			return -1*sign; //B has higher priority
	} while (true);
}

int dynAnimCompareJointStancesPriorityLarge(const char **eaStanceWordsA, const DynAnimTimedStance **eaTimedStancesA,
											const char **eaStanceWordsB, const DynAnimTimedStance **eaTimedStancesB)
{
	return cmpJointStancesPriority(eaStanceWordsA, eaTimedStancesA, eaStanceWordsB, eaTimedStancesB, true);
}

int dynAnimCompareJointStancesPrioritySmall(const char **eaStanceWordsA, const DynAnimTimedStance **eaTimedStancesA,
											const char **eaStanceWordsB, const DynAnimTimedStance **eaTimedStancesB)
{
	return cmpJointStancesPriority(eaStanceWordsA, eaTimedStancesA, eaStanceWordsB, eaTimedStancesB, false);
}

static void sortStanceWords(const char **eaStanceWords)
{
	eaQSort(eaStanceWords, dynAnimCompareStanceWordPriority);
}

static const char *dynAnimKeyFromStanceWordsEx(	const char*const*eaStanceWords,
												const char* pcMovementStance)
{
	char multi_stance[1024];
	static const char **eaTemp=NULL;
	multi_stance[0] = 0;
	eaCopy(&eaTemp, &eaStanceWords);
	sortStanceWords(eaTemp);

	FOR_EACH_IN_EARRAY(eaTemp, const char, pcStance)
	{
		if (multi_stance[0])
			strcat(multi_stance, ", ");
		strcat(multi_stance, pcStance);
	}
	FOR_EACH_END;
	if (pcMovementStance)
	{
		if (multi_stance[0])
			strcat(multi_stance, ", ");
		strcat(multi_stance, pcMovementStance);
	}
	return allocAddString(multi_stance);
}

const char *dynAnimKeyFromStanceWords(const char*const* eaStanceWords)
{
	return dynAnimKeyFromStanceWordsEx(eaStanceWords, NULL);
}

void dynAnimStanceWordsFromKey(const char *pcKey, const char ***eaStanceWords)
{
	int i;
	const char *pcStart;
	char multi_stance[1024];
	multi_stance[0] = 0;
	strcpy(multi_stance, pcKey);
	pcStart = multi_stance;

	for ( i=0; i < 1024; i++ ) {
		if(multi_stance[i] == ',') {
			multi_stance[i] = '\0';
			eaPush(eaStanceWords, allocAddString(pcStart));
			i+=2;
			pcStart = multi_stance + i;
		} else if (!multi_stance[i]) {
			eaPush(eaStanceWords, allocAddString(pcStart));
			return;
		}
	}
	assert(false);
}

static bool dynAnimChartRefStanceVerify(DynAnimChartLoadTime *pSuperChart, DynAnimChartLoadTime *pChart, const char **eaRefStanceWords, const char *pcKeyword)
{
	bool bRet = true;
	char blamee[256];

	if (pcKeyword) {
		sprintf(blamee, "Ref with keyword %s", pcKeyword);
	} else {
		sprintf(blamee, "Chart %s", pChart->pcName);
	}

	FOR_EACH_IN_EARRAY(eaRefStanceWords, const char, pcStanceWord)
	{
		int i;

		if (!pcStanceWord || strlen(pcStanceWord) == 0)
		{
			AnimFileError(pChart->pcFilename, "%s has an empty stance word!", blamee);
			bRet = false;
			continue;
		}

		if (!dynAnimStanceValid(pcStanceWord))
		{
			AnimFileError(pChart->pcFilename, "%s references an unknown stance word '%s', fix it or add it to dyn/AnimStance", blamee, pcStanceWord);
			bRet = false;
		}

		// Verify no dups
		for (i=0; i<ipcStanceWordIndex; i++)
		{
			if (pcStanceWord == eaRefStanceWords[i])
			{
				AnimFileError(pChart->pcFilename, "%s has duplicate Stance Words %s.", blamee, pcStanceWord);
				bRet = false;
			}
		}

		// Verify no overlap with chart we're part of (unless this is being called to verify those same stancewords
		if (pSuperChart)
		{
			FOR_EACH_IN_EARRAY(pSuperChart->eaStanceWords, const char, pcSuperStanceWord)
			{
				if (pcSuperStanceWord == pcStanceWord)
				{
					AnimFileError(pChart->pcFilename, "%s has Stance Word %s which is also a Stance Word of the Chart.", blamee, pcStanceWord);
					bRet = false;
				}
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
	return bRet;
}

static bool dynAnimChartGraphRefVerify(DynAnimChartLoadTime *pSuperChart, DynAnimChartLoadTime *pChart, DynAnimChartGraphRefLoadTime* pRef, bool bMoveSequencer)
{
	bool bRet=true;

	if (!pRef->pcKeyword)
	{
		if (GET_REF(pRef->hGraph))
			AnimFileError(pChart->pcFilename, "Must specify a keyword in chart %s for Graph %s", pChart->pcName, REF_HANDLE_GET_STRING(pRef->hGraph));

		if (eaSize(&pRef->eaGraphChances))
		{
			FOR_EACH_IN_EARRAY(pRef->eaGraphChances, DynAnimGraphChanceRef, pGraphChance)
			{
				if (GET_REF(pGraphChance->hGraph))
					AnimFileError(pChart->pcFilename, "Must specify a keyword in chart %s for Graph %s", pChart->pcName, REF_HANDLE_GET_STRING(pGraphChance->hGraph));
			}
			FOR_EACH_END;
		}
		bRet = false;
	}
	else
	{
		if (bMoveSequencer && pRef->pcKeyword != pcAnimChartDefaultKey && !dynAnimMovementStanceKeyValid(pRef->pcKeyword))
		{
			AnimFileError(pChart->pcFilename,"Movement sequencer uses non-movement stance as keyword %s in chart %s", pRef->pcKeyword, pChart->pcName);
			bRet = false;
		}
		else if (!bMoveSequencer && dynAnimMovementStanceValid(pRef->pcKeyword))
		{
			AnimFileError(pChart->pcFilename,"Default sequencer uses movement stance as keyword %s in chart %s", pRef->pcKeyword, pChart->pcName);
			bRet = false;
		}
	}

	if (!GET_REF(pRef->hGraph) &&
		eaSize(&pRef->eaGraphChances) == 0 &&
		!pRef->bBlank
		)
	{
		AnimFileError(pChart->pcFilename, "Can't find a Graph in chart %s for keyword %s", pChart->pcName, pRef->pcKeyword);
		bRet = false;
	}

	if (pRef->bBlank &&
		(GET_REF(pRef->hGraph) ||
		 eaSize(&pRef->eaGraphChances) > 0)
		)
	{
		AnimFileError(pChart->pcFilename, "Graph entry marked for skipping has graph(s) attached in chart %s for keyword %s, make it either skip or have attached graphs", pChart->pcName, pRef->pcKeyword);
		bRet = false;
	}

	if (pRef->bBlank &&
		pRef->pcKeyword == ANIM_CHART_DEFAULT_KEY)
	{
		AnimFileError(pChart->pcFilename, "Default keyword must be assigned animation, can not skip entry");
		bRet = false;
	}

	if (GET_REF(pRef->hGraph))
	{
		DynAnimGraph* pGraph = GET_REF(pRef->hGraph);
		DynAnimTemplate* pTemplate = pGraph ? GET_REF(pGraph->hTemplate) : NULL;

		if (pGraph->bPartialGraph == true) {
			AnimFileError(pChart->pcFilename, "Graph %s in chart %s for keyword %s is a partial graph! Change it to an actual graph", REF_HANDLE_GET_STRING(pRef->hGraph), pChart->pcName, pRef->pcKeyword);
			bRet = false;
		}

		if (pTemplate)
		{
			if (pTemplate->eType == eAnimTemplateType_Movement)
			{
				// we shouldn't hit this since movement graphs are being introduced after graph chances
				if (pRef->pcKeyword != pcAnimChartDefaultKey &&
					!dynAnimMovementStanceKeyValid(pRef->pcKeyword))
				{
					AnimFileError(pChart->pcFilename, "Non-movement stance keyword %s in chart %s references movement typed graph %s", pRef->pcKeyword, pChart->pcName, REF_HANDLE_GET_STRING(pRef->hGraph));
					bRet = false;
				}

				if (!bMoveSequencer) {
					AnimFileError(pChart->pcFilename, "Default sequencer contains movement typed graph %s triggered by keyword %s in chart %s", REF_HANDLE_GET_STRING(pRef->hGraph), pRef->pcKeyword, pChart->pcName);
					bRet = false;
				}
			}
			else
			{
				if (dynAnimMovementStanceValid(pRef->pcKeyword)) {
					AnimFileError(pChart->pcFilename, "Movement stance keyword %s in chart %s references non-movement typed graph %s", pRef->pcKeyword, pChart->pcName, REF_HANDLE_GET_STRING(pRef->hGraph));
					bRet = false;
				}

				if (bMoveSequencer) {
					AnimFileError(pChart->pcFilename, "Movement sequencer contains non-movement typed graph %s triggered by movement stance %s in chart %s", REF_HANDLE_GET_STRING(pRef->hGraph), pRef->pcKeyword, pChart->pcName);
					bRet = false;
				}
			}
		}
	}

	if (eaSize(&pRef->eaGraphChances) > 0)
	{
		FOR_EACH_IN_EARRAY(pRef->eaGraphChances, DynAnimGraphChanceRef, pGraphChanceRef)
		{
			DynAnimGraph* pGraph = GET_REF(pGraphChanceRef->hGraph);
			DynAnimTemplate* pTemplate = pGraph ? GET_REF(pGraph->hTemplate) : NULL;

			if (!pGraph)
			{
				AnimFileError(pChart->pcFilename, "Can't find Graph %s in chart %s for keyword %s", REF_HANDLE_GET_STRING(pGraphChanceRef->hGraph), pChart->pcName, pRef->pcKeyword);
				bRet = false;
			}
			else
			{
				if (pGraph->bPartialGraph == true) {
					AnimFileError(pChart->pcFilename, "Graph %s in chart %s for keyword %s is a partial graph! Change it to an actual graph", pGraph->pcName, pChart->pcName, pRef->pcKeyword);
					bRet = false;
				}

				if (pTemplate)
				{
					if (pTemplate->eType == eAnimTemplateType_Movement)
					{
						if (pRef->pcKeyword != pcAnimChartDefaultKey &&
							!dynAnimMovementStanceKeyValid(pRef->pcKeyword))
						{
							AnimFileError(pChart->pcFilename, "Non-movement stance keyword %s in chart %s references movement typed graph %s", pRef->pcKeyword, pChart->pcName, pGraph->pcName);
							bRet = false;
						}

						if (!bMoveSequencer) {
							AnimFileError(pChart->pcFilename, "Default sequencer contains movement typed graph %s triggered by keyword %s in chart %s", pGraph->pcName, pRef->pcKeyword, pChart->pcName);
							bRet = false;
						}
					}
					else
					{
						if (dynAnimMovementStanceValid(pRef->pcKeyword)) {
							AnimFileError(pChart->pcFilename, "Movement stance %s in chart %s references non-movement typed graph %s", pRef->pcKeyword, pChart->pcName, pGraph->pcName);
							bRet = false;
						}

						if (bMoveSequencer) {
							AnimFileError(pChart->pcFilename, "Movement sequencer contains non-movement typed graph %s triggered by movement stance %s in chart %s", pGraph->pcName, pRef->pcKeyword, pChart->pcName);
							bRet = false;
						}
					}
				}
			}

			if (pGraphChanceRef->fChance < 0.0)
			{
				AnimFileError(pChart->pcFilename, "Graph %s in chart %s for keyword %s has a negative chance (it must be >= 0)", REF_HANDLE_GET_STRING(pGraphChanceRef->hGraph), pChart->pcName, pRef->pcKeyword);
				bRet = false;
			}
		}
		FOR_EACH_END;
	}

	if (!dynAnimChartRefStanceVerify(pSuperChart, pChart, pRef->eaStanceWords, pRef->pcKeyword))
		bRet = false;

	return bRet;
}

// Verifies external references.  Internal references are verified during binning (dynAnimChartLoadTimeVerify)
bool dynAnimChartVerifyReferences(DynAnimChartLoadTime *pChart)
{
	bool bRet=true;
	DynAnimChartLoadTime* pBaseChart = GET_REF(pChart->hBaseChart);
	bool bHasBaseChart = !!REF_HANDLE_IS_ACTIVE(pChart->hBaseChart);
	bool bIsBaseChart = !bHasBaseChart && !pChart->bIsSubChart;
	DynMovementSet* pMovementSet;

	if (bHasBaseChart && !pBaseChart)
	{
		AnimFileError(pChart->pcFilename, "Unable to find base chart %s", REF_HANDLE_GET_STRING(pChart->hBaseChart));
		return false;
	}

	if (pBaseChart && REF_HANDLE_IS_ACTIVE(pBaseChart->hBaseChart))
	{
		AnimFileError(pChart->pcFilename, "Chart %s has a base chart %s that itself has a base chart (%s). This is not allowed.", pChart->pcName, pBaseChart->pcName, REF_STRING_FROM_HANDLE(pBaseChart->hBaseChart));
	}

	pMovementSet = GET_REF(pChart->hMovementSet);
	if (!gConf.bUseMovementGraphs &&
		REF_HANDLE_IS_ACTIVE(pChart->hMovementSet) &&
		!pMovementSet)
	{
		AnimFileError(pChart->pcFilename, "Can't find movement set %s", REF_HANDLE_GET_STRING(pChart->hMovementSet));
		bRet = false;
	}

	if (pMovementSet && bIsBaseChart)
	{
		// Found a movement set, so make sure every move is accounted for
		FOR_EACH_IN_EARRAY(pMovementSet->eaMovementSequences, DynMovementSequence, pSeq)
		{
			bool bFound = false;
			FOR_EACH_IN_EARRAY(pChart->eaMoveRefs, DynAnimChartMoveRefLoadTime, pRef)
			{
				if (eaSize(&pRef->eaStanceWords) || pRef->pcMovementStance)
					// Not really the base chart
					continue;
				if (pRef->pcMovementType == pSeq->pcMovementType)
				{
					if (REF_HANDLE_IS_ACTIVE(pRef->hMove))
					{
						bFound = true;
						break;
					}
					
					if (eaSize(&pRef->eaMoveChances))
					{
						FOR_EACH_IN_EARRAY(pRef->eaMoveChances, DynAnimMoveChanceRef, pChanceRef) {
							if (REF_HANDLE_IS_ACTIVE(pChanceRef->hMove)) {
								bFound = true;
								break;
							}
						} FOR_EACH_END;
						if (bFound) break;
					}
				}
			}
			FOR_EACH_END;
			if (!bFound)
			{
				AnimFileError(pChart->pcFilename, "Unable to find move for movetype %s.  Base charts require all moves.", pSeq->pcMovementType);
				bRet = false;
			}
		}
		FOR_EACH_END;
	}
	
	FOR_EACH_IN_EARRAY(pChart->eaMoveRefs, DynAnimChartMoveRefLoadTime, pRef)
	{
		if (REF_HANDLE_IS_ACTIVE(pRef->hMove) && !GET_REF(pRef->hMove))
		{
			AnimFileError(pChart->pcFilename, "Unable to find move %s referenced in movetype %s", REF_HANDLE_GET_STRING(pRef->hMove), pRef->pcMovementType);
			bRet = false;
		}

		if (eaSize(&pRef->eaMoveChances)) {
			F32 fProb = 0.f;
			FOR_EACH_IN_EARRAY(pRef->eaMoveChances, DynAnimMoveChanceRef, pChanceRef) {
				if (REF_HANDLE_IS_ACTIVE(pChanceRef->hMove) && !GET_REF(pChanceRef->hMove)) {
					AnimFileError(pChart->pcFilename, "Unable to find move %s referenced in movetype %s", REF_HANDLE_GET_STRING(pChanceRef->hMove), pRef->pcMovementType);
					bRet = false;
				}
				fProb += pChanceRef->fChance;
			} FOR_EACH_END;
			if (!nearSameF32(fProb,1.f)) {
				AnimFileError(pChart->pcFilename, "Move probabilities do not add up to 1");
				bRet = false;
			}
		}
	}
	FOR_EACH_END;

	return bRet;
}

static void dynAnimChartDupSubErrorHelper(DynAnimChartLoadTime* pTestChart, DynAnimChartLoadTime* pErrorChart, const DynAnimChartGraphRefLoadTime* pTestRef, const char* pcErrorKey)
{
	char testKey[1024];
	strcpy(testKey, pTestRef->pcKeyword);
	if (eaSize(&pTestRef->eaStanceWords))
	{
		strcat(testKey, ":");
		strcat(testKey, dynAnimKeyFromStanceWordsEx(pTestRef->eaStanceWords, NULL));
	}

	if (strcmpi(testKey, pcErrorKey) == 0)
	{
		if (eaSize(&pTestRef->eaStanceWords))
			ErrorFilenameDup(pTestChart->pcFilename, pErrorChart->pcFilename, pcErrorKey, "Keyword:stance");
		else
			ErrorFilenameDup(pTestChart->pcFilename, pErrorChart->pcFilename, pcErrorKey, "Keyword");
	}
}

static void dynAnimChartDupSubError(DynAnimChartLoadTime *pTestChart, DynAnimChartLoadTime *pErrorChart, const char *pcErrorKey)
{
	if (pTestChart != pErrorChart)
	{
		FOR_EACH_IN_EARRAY(pTestChart->eaGraphRefs, DynAnimChartGraphRefLoadTime, pTestRef) {
			dynAnimChartDupSubErrorHelper(pTestChart, pErrorChart, pTestRef, pcErrorKey);
		} FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pTestChart->eaMoveGraphRefs, DynAnimChartGraphRefLoadTime, pTestRef) {
			dynAnimChartDupSubErrorHelper(pTestChart, pErrorChart, pTestRef, pcErrorKey);
		} FOR_EACH_END;
	}

	FOR_EACH_IN_EARRAY(pTestChart->eaSubCharts, DynAnimSubChartRef, pTestSubChart)
	{
		if (GET_REF(pTestSubChart->hSubChart))
			dynAnimChartDupSubError(GET_REF(pTestSubChart->hSubChart), pErrorChart, pcErrorKey);
	}
	FOR_EACH_END;
}

static bool dynAnimChartLoadTimeVerifySub(const char *pcFilename, DynAnimChartLoadTime *pSuperChart, DynAnimChartLoadTime *pChart, StashTable stGraphs) 
{
	bool bRet=true;
	FOR_EACH_IN_EARRAY(pChart->eaGraphRefs, DynAnimChartGraphRefLoadTime, pRef)
	{
		char key[1024];
		if (!dynAnimChartGraphRefVerify(pSuperChart, pChart, pRef, false))
		{
			bRet = false;
			continue;
		}
		NameList_Bucket_AddName(dynAnimKeywordList, pRef->pcKeyword);
		strcpy(key, pRef->pcKeyword);
		if (eaSize(&pRef->eaStanceWords))
		{
			strcat(key, ":");
			strcat(key, dynAnimKeyFromStanceWordsEx(pRef->eaStanceWords, NULL));
		}
		if (!stashAddPointer(stGraphs, key, pRef, false))
		{
			dynAnimChartDupSubError(pSuperChart, pChart, key);
			/*
			if (eaSize(&pRef->eaStanceWords) || pRef->pcMovementStance)
				ErrorFilenameDup(pcFilename, pChart->pcFilename, key, "Keyword:Stance");
			else
				ErrorFilenameDup(pcFilename, pChart->pcFilename, key, "Keyword");
			*/
			bRet = false;
			continue;
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pChart->eaSubCharts, DynAnimSubChartRef, pSubChartRef)
	{
		DynAnimChartLoadTime* pSubChart = GET_REF(pSubChartRef->hSubChart);
		if (pSubChart)
		{
			if (!dynAnimChartLoadTimeVerifySub(pcFilename, pSuperChart, pSubChart, stGraphs))
				bRet = false;
		} else {
			AnimFileError(pChart->pcFilename, "Unable to find Sub-Chart %s", REF_HANDLE_GET_STRING(pSubChartRef->hSubChart));
			bRet = false;
		}
	}
	FOR_EACH_END;
	return bRet;
}

bool dynAnimChartLoadTimeVerify(DynAnimChartLoadTime *pChart)
{
	StashTable stGraphs;
	bool bRet=true;
	bool bHasBaseChart = false;

	if(!resIsValidName(pChart->pcName))
	{
		AnimFileError(pChart->pcFilename, "Chart name \"%s\" is illegal.", pChart->pcName);
		bRet = false;
	}
	if(!resIsValidScope(pChart->pcScope))
	{
		AnimFileError(pChart->pcFilename, "Chart scope \"%s\" is illegal.", pChart->pcScope);
		bRet = false;
	}
	{
		const char* pcTempFileName = pChart->pcFilename;
		if (resFixPooledFilename(&pcTempFileName, "dyn/animchart", pChart->pcScope, pChart->pcName, "achart"))
		{
			if (IsServer())
			{
				AnimFileError(pChart->pcFilename, "Chart filename does not match name '%s' scope '%s'", pChart->pcName, pChart->pcScope);
				bRet = false;
			}
		}
	}

	if (!pChart->bIsSubChart)
	{
		bHasBaseChart = !!REF_HANDLE_IS_ACTIVE(pChart->hBaseChart);
		if (bHasBaseChart)
		{
			// derives from a base chart, so must have a stance word
			if (eaSize(&pChart->eaStanceWords) < 1)
			{
				AnimFileError(pChart->pcFilename, "Must set at least one Stance Word for Charts with a Base Chart");
				bRet = false;
			}
			if (!dynAnimChartRefStanceVerify(NULL, pChart, pChart->eaStanceWords, NULL))
				bRet = false;
		}
	} else {
		if (eaSize(&pChart->eaStanceWords))
		{
			AnimFileError(pChart->pcFilename, "Sub-chart has stance words defined, this is not allowed");
			bRet = false;
		}
		if (REF_HANDLE_IS_ACTIVE(pChart->hBaseChart))
		{
			AnimFileError(pChart->pcFilename, "Sub-chart has a Base Chart, this is not allowed");
			bRet = false;
		}
	}

	stGraphs = stashTableCreateWithStringKeys(eaSize(&pChart->eaGraphRefs), StashDeepCopyKeys_NeverRelease); // Both Graph and MoveRef verification need DeepCopy
	if (!dynAnimChartLoadTimeVerifySub(pChart->pcFilename, pChart, pChart, stGraphs))
	{
		bRet = false;
	}

	// Verify "Default" graph
	if (!pChart->bIsSubChart && !bHasBaseChart) // it's a root chart
	{
		// Ensure we have a Default
		if (!stashFindPointer(stGraphs, ANIM_CHART_DEFAULT_KEY, NULL))
		{
			AnimFileError(pChart->pcFilename, "Base charts must have a Default Graph.");
			bRet = false;
		}
	}
	{
		DynAnimChartGraphRefLoadTime *pRef;
		if (stashFindPointer(stGraphs, ANIM_CHART_DEFAULT_KEY, &pRef))
		{
			DynAnimGraph *pGraph;

			// verify default graph (multi case)
			if (eaSize(&pRef->eaGraphChances) > 0)
			{
				FOR_EACH_IN_EARRAY(pRef->eaGraphChances, DynAnimGraphChanceRef, pGraphChanceRef)
				{
					pGraph = GET_REF(pGraphChanceRef->hGraph);
				}
				FOR_EACH_END;
			}

			//verify default graph (single case)
			if (GET_REF(pRef->hGraph))
			{
				pGraph = GET_REF(pRef->hGraph);
			}
		}
	}

	stashTableClear(stGraphs);

	FOR_EACH_IN_EARRAY(pChart->eaMoveGraphRefs, DynAnimChartGraphRefLoadTime, pRef)
	{
		char key[1024];
		if (!dynAnimChartGraphRefVerify(NULL, pChart, pRef, true))
		{
			bRet = false;
			continue;
		}
		// NO, we'll start these with stances instead
		// NameList_Bucket_AddName(dynAnimKeywordList, pRef->pcKeyword);
		strcpy(key, pRef->pcKeyword);
		if (eaSize(&pRef->eaStanceWords))
		{
			strcat(key, ":");
			strcat(key, dynAnimKeyFromStanceWordsEx(pRef->eaStanceWords, NULL));
		}
		if (!stashAddPointer(stGraphs, key, pRef, false))
		{
			dynAnimChartDupSubError(pChart, pChart, key);
			/*
			if (eaSize(&pRef->eaStanceWords) || pRef->pcMovementStance)
				ErrorFilenameDup(pcFilename, pChart->pcFilename, key, "Keyword:Stance");
			else
				ErrorFilenameDup(pcFilename, pChart->pcFilename, key, "Keyword");
			*/
			bRet = false;
			continue;
		}
	}
	FOR_EACH_END;

	if (eaSize(&pChart->eaMoveRefs))
	{
		DynMovementSet *pMovementSet = GET_REF(pChart->hMovementSet);

		if (!pMovementSet)
		{
			DynAnimChartLoadTime* pBaseChart = GET_REF(pChart->hBaseChart);
			if (pBaseChart) {
				pMovementSet = GET_REF(pBaseChart->hMovementSet);
			}
		}

		// Verify stances on MoveRefs
		FOR_EACH_IN_EARRAY(pChart->eaMoveRefs, DynAnimChartMoveRefLoadTime, pRef)
		{
			if (!dynAnimChartRefStanceVerify(pChart, pChart, pRef->eaStanceWords, pRef->pcMovementType))
				bRet = false;
			if (pRef->pcMovementStance &&
				pMovementSet &&
				eaFindString(&pMovementSet->eaMovementStances, pRef->pcMovementStance) < 0)
			{
				AnimFileError(pChart->pcFilename, "Movement ref for MoveType '%s' references invalid MovementStance '%s'.", pRef->pcMovementType, pRef->pcMovementStance);
				bRet = false;
			}
			if (pRef->pcMovementType == allocAddString("Stopped") && pRef->bBlank)
			{
				AnimFileError(pChart->pcFilename, "'%s' MoveType on MovementStance '%s' is not allowed to be set to SKIP.", pRef->pcMovementType, pRef->pcMovementStance);
				bRet = false;
			}
		}
		FOR_EACH_END;

		// Check for duplicate MoveRefs
		stashTableClear(stGraphs);
		FOR_EACH_IN_EARRAY(pChart->eaMoveRefs, DynAnimChartMoveRefLoadTime, pRef)
		{
			const char *stance_key = dynAnimKeyFromStanceWordsEx(pRef->eaStanceWords, pRef->pcMovementStance);
			char buf[1024];
			sprintf(buf, "%s:%s", pRef->pcMovementType, stance_key);
			if (!stashAddPointer(stGraphs, buf, pRef, false))
			{
				if (eaSize(&pRef->eaStanceWords) || pRef->pcMovementStance)
					AnimFileError(pChart->pcFilename, "Chart has two MoveRefs with the same MovementType:Stance '%s'.", buf);
				else
					AnimFileError(pChart->pcFilename, "Chart has two MoveRefs with the same MovementType '%s'.", pRef->pcMovementType);
			}
		}
		FOR_EACH_END;
	}

	stashTableDestroySafe(&stGraphs);

	return bRet;
}

void dynAnimChartLoadTimeFixup(DynAnimChartLoadTime* pChart)
{
	FOR_EACH_IN_EARRAY(pChart->eaStanceWords, const char, pcStanceWord)
	{
		char stanceWord[64];
		strcpy(stanceWord, removeLeadingWhiteSpaces(pcStanceWord));
		removeTrailingWhiteSpaces(stanceWord);
		pcStanceWord = pChart->eaStanceWords[ipcStanceWordIndex] = allocAddString(stanceWord);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pChart->eaGraphRefs, DynAnimChartGraphRefLoadTime, pRef)
	{
		eaPushUnique(&pChart->eaValidKeywords, pRef->pcKeyword);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pChart->eaMoveGraphRefs, DynAnimChartGraphRefLoadTime, pRef)
	{
		eaPushUnique(&pChart->eaValidMoveKeywords, pRef->pcKeyword);
	}
	FOR_EACH_END;

	if (pChart->bIsSubChart) {
		pChart->pcFileType = allocAddString("Sub");
	} else if (REF_HANDLE_IS_ACTIVE(pChart->hBaseChart)) {
		pChart->pcFileType = allocAddString("Stance");
	} else {
		pChart->pcFileType = allocAddString("Base");
	}

	FOR_EACH_IN_EARRAY(pChart->eaGraphRefs, DynAnimChartGraphRefLoadTime, graphRefLT)
	{
		//migrate from the old single graph syntax to the new chance syntax
		if (GET_REF(graphRefLT->hGraph) &&
			eaSize(&graphRefLT->eaGraphChances) == 0
			)
		{
			//create a new chance based version
			DynAnimGraphChanceRef *pGraphChanceNew = StructCreate(parse_DynAnimGraphChanceRef);
			COPY_HANDLE(pGraphChanceNew->hGraph, graphRefLT->hGraph);		
			pGraphChanceNew->fChance = 1.0;//it's the only option, this could be set to anything
			eaPush(&graphRefLT->eaGraphChances, pGraphChanceNew);

			//get rid of the old single-choice version
			REMOVE_HANDLE(graphRefLT->hGraph);
		}
		else if (
			GET_REF(graphRefLT->hGraph) &&
			eaSize(&graphRefLT->eaGraphChances) > 0
			)
		{
			//get rid of the old single-choice
			REMOVE_HANDLE(graphRefLT->hGraph);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pChart->eaMoveRefs, DynAnimChartMoveRefLoadTime, moveRefLT)
	{
		//migrate from the old single graph syntax to the new chance syntax
		if (GET_REF(moveRefLT->hMove) &&
			eaSize(&moveRefLT->eaMoveChances) == 0)
		{
			//create a new chance based version
			DynAnimMoveChanceRef *pMoveChanceNew = StructCreate(parse_DynAnimMoveChanceRef);
			COPY_HANDLE(pMoveChanceNew->hMove, moveRefLT->hMove);		
			pMoveChanceNew->fChance = 1.0;
			eaPush(&moveRefLT->eaMoveChances, pMoveChanceNew);

			//get rid of the old single-choice version
			REMOVE_HANDLE(moveRefLT->hMove);
		}
		else if (
			GET_REF(moveRefLT->hMove) &&
			eaSize(&moveRefLT->eaMoveChances) > 0
			)
		{
			//get rid of the old single-choice
			REMOVE_HANDLE(moveRefLT->hMove);
		}
	}
	FOR_EACH_END;
}

static void dynAnimChartRemoveMovementSequenceFromSubset(DynAnimChartRunTime *pChart, U32 uiSeqIndex)
{
	DynMovementSequence *pDelSeq = eaRemoveFast(&pChart->eaMovementSequencesSubset, uiSeqIndex);
	DynMovementSequence *pNeighbor1 = NULL, *pNeighbor2 = NULL;
	F32 fNeighbor1AngDif, fNeighbor2AngDif;
	
	//find the neighboring sequences
	FOR_EACH_IN_EARRAY(pChart->eaMovementSequencesSubset, DynMovementSequence, pCurSeq) {
		if (pCurSeq->fAngleWidth > 0.0f && //make sure it's not the stopped sequence
			(	fabsf(subAngle(pDelSeq->fAngle,pCurSeq->fCoverAngleMax)) <= RAD(1.0f) || //make sure it's a neighbor
				fabsf(subAngle(pDelSeq->fAngle,pCurSeq->fCoverAngleMin)) <= RAD(1.0f)))
		{
			F32 fAngleDiff = fabsf(subAngle(pDelSeq->fAngle,pCurSeq->fAngle));

			if (pNeighbor1)
			{
				if (fAngleDiff < fNeighbor1AngDif) {
					pNeighbor2 = pNeighbor1;
					pNeighbor1 = pCurSeq;
					fNeighbor2AngDif = fNeighbor1AngDif;
					fNeighbor1AngDif = fAngleDiff;
				}
				else if (!pNeighbor2 || fAngleDiff < fNeighbor2AngDif) {
					pNeighbor2 = pCurSeq;
					fNeighbor2AngDif = fAngleDiff;
				}
			}
			else
			{
				pNeighbor1 = pCurSeq;
				fNeighbor1AngDif = fAngleDiff;
			}
		}
	} FOR_EACH_END;
	
	//modify the neighbor's values
	//NOTE: we don't want to change the movement sequences angle, since the animation should line up with the original value only
	if (pNeighbor2)
	{
		F32 fMaxDiffA, fMinDiffA;
		F32 fMaxDiffB, fMinDiffB;
		F32 fWidthDiffA, fWidthDiffB;
		F32 fWidthDiffC, fWidthDiffD;
		
		//cover angle settings & pre-width calcs for neighbor 1
		fMaxDiffA = fabsf(subAngle(pNeighbor1->fAngle,pDelSeq->fCoverAngleMax));
		fMinDiffA = fabsf(subAngle(pNeighbor1->fAngle,pDelSeq->fCoverAngleMin));
			
		fMaxDiffB = fabsf(subAngle(pDelSeq->fAngle,pNeighbor1->fCoverAngleMax));	
		fMinDiffB = fabsf(subAngle(pDelSeq->fAngle,pNeighbor1->fCoverAngleMin));

		if (fMaxDiffB > fMinDiffB) {
			pNeighbor1->fCoverAngleMin = fMaxDiffA > fMinDiffA ? pDelSeq->fCoverAngleMax : pDelSeq->fCoverAngleMin;
		} else {
			pNeighbor1->fCoverAngleMax = fMaxDiffA > fMinDiffA ? pDelSeq->fCoverAngleMax : pDelSeq->fCoverAngleMin;
		}
		
		fWidthDiffA = fabsf(	(pNeighbor1->fCoverAngleMax < 0 ? pNeighbor1->fCoverAngleMax+TWOPI : pNeighbor1->fCoverAngleMax) -
								(pNeighbor1->fCoverAngleMin < 0 ? pNeighbor1->fCoverAngleMin+TWOPI : pNeighbor1->fCoverAngleMin));
		fWidthDiffB = TWOPI - fWidthDiffA;
		fWidthDiffC = 0.0f;
		fWidthDiffD = 0.0f;

		//cover angle settings & pre-width calcs for neighbor 2
		fMaxDiffA = fabsf(subAngle(pNeighbor2->fAngle,pDelSeq->fCoverAngleMax));
		fMinDiffA = fabsf(subAngle(pNeighbor2->fAngle,pDelSeq->fCoverAngleMin));

		fMaxDiffB = fabsf(subAngle(pDelSeq->fAngle,pNeighbor2->fCoverAngleMax));
		fMinDiffB = fabsf(subAngle(pDelSeq->fAngle,pNeighbor2->fCoverAngleMin));

		if (fMaxDiffB > fMinDiffB) {
			pNeighbor2->fCoverAngleMin = fMaxDiffA > fMinDiffA ? pDelSeq->fCoverAngleMax : pDelSeq->fCoverAngleMin;
		} else {
			pNeighbor2->fCoverAngleMax = fMaxDiffA > fMinDiffA ? pDelSeq->fCoverAngleMax : pDelSeq->fCoverAngleMin;
		}

		fWidthDiffC = fabsf(	(pNeighbor2->fCoverAngleMax < 0 ? pNeighbor2->fCoverAngleMax+TWOPI : pNeighbor2->fCoverAngleMax) -
								(pNeighbor2->fCoverAngleMin < 0 ? pNeighbor2->fCoverAngleMin+TWOPI : pNeighbor2->fCoverAngleMin));
		fWidthDiffD = TWOPI - fWidthDiffC;
		
		//redo the widths
		{
			F32 fSum = pNeighbor1->fAngleWidth + pNeighbor2->fAngleWidth + pDelSeq->fAngleWidth;
			F32 fAC = fabsf(fSum - (fWidthDiffA+fWidthDiffC));
			F32 fAD = fabsf(fSum - (fWidthDiffA+fWidthDiffD));
			F32 fBC = fabsf(fSum - (fWidthDiffB+fWidthDiffC));
			F32 fBD = fabsf(fSum - (fWidthDiffB+fWidthDiffD));

			if (fAC <= fAD && fAC <= fBC && fAC <= fBD) {
				pNeighbor1->fAngleWidth = fWidthDiffA;
				pNeighbor2->fAngleWidth = fWidthDiffC;
			}
			else if (fAD <= fAC && fAD <= fBC && fAD <= fBD) {
				pNeighbor1->fAngleWidth = fWidthDiffA;
				pNeighbor2->fAngleWidth = fWidthDiffD;
			}
			else if (fBC <= fAC && fBC <= fAD && fBC <= fBD) {
				pNeighbor1->fAngleWidth = fWidthDiffB;
				pNeighbor2->fAngleWidth = fWidthDiffC;
			}
			else { //fBD is min
				pNeighbor1->fAngleWidth = fWidthDiffB;
				pNeighbor2->fAngleWidth = fWidthDiffD;
			}
		}
	}
	else if (pNeighbor1)
	{
		//single neighbor case
		pNeighbor1->fAngleWidth = RAD(360.0f);
		pNeighbor1->fCoverAngleMax = pNeighbor1->fAngle;
		pNeighbor1->fCoverAngleMin = pNeighbor1->fAngle;
	}

	StructDestroy(parse_DynMovementSequence, pDelSeq);
}

static void dynAnimChartRunTimeFixup(DynAnimChartRunTime* pChart)
{
	DynMovementSet *pMovementSet = GET_REF(pChart->hMovementSet);

	stashTableDestroySafe(&pChart->stGraphs);
	pChart->stGraphs = stashTableCreateWithStringKeys(eaSize(&pChart->eaGraphRefs), StashDefault);

	FOR_EACH_IN_EARRAY(pChart->eaGraphRefs, DynAnimChartGraphRefRunTime, pRef) {
		stashAddPointer(pChart->stGraphs, pRef->pcKeyword, pRef, false);
	} FOR_EACH_END;

	stashTableDestroySafe(&pChart->stMovementGraphs);
	pChart->stMovementGraphs = stashTableCreateWithStringKeys(eaSize(&pChart->eaMoveGraphRefs), StashDefault);

	FOR_EACH_IN_EARRAY(pChart->eaMoveGraphRefs, DynAnimChartGraphRefRunTime, pRef) {
		stashAddPointer(pChart->stMovementGraphs, pRef->pcKeyword, pRef, false);
	} FOR_EACH_END;
	
	stashTableDestroySafe(&pChart->stMoves);
	pChart->stMoves = stashTableCreateWithStringKeys(eaSize(&pChart->eaMoveRefs), StashDefault);

	FOR_EACH_IN_EARRAY(pChart->eaMoveRefs, DynAnimChartMoveRefRunTime, pRef)
	{
		stashAddPointer(pChart->stMoves, pRef->pcMovementType, pRef, false);
		if (pRef->bBlank) pChart->uiNumMovementBlanks++;
	}
	FOR_EACH_END;

	if (pMovementSet) { // the -1 is for the stopped movement sequence
		pChart->uiNumMovementDirections = eaSize(&pMovementSet->eaMovementSequences) - pChart->uiNumMovementBlanks - 1;
	} else {
		pChart->uiNumMovementDirections = 0;
	}

	assert(!eaSize(&pChart->eaMovementSequencesSubset));

	if (!gConf.bUseMovementGraphs &&
		pChart->uiNumMovementBlanks)
	{
		//copy the full set of movement sequences
		FOR_EACH_IN_EARRAY(pMovementSet->eaMovementSequences, DynMovementSequence, pSeq) {
			DynMovementSequence *pSeqCopy = StructClone(parse_DynMovementSequence, pSeq);
			eaPush(&pChart->eaMovementSequencesSubset, pSeqCopy);
		} FOR_EACH_END;

		//remove blanks from the subset, and modify the subset's movement sequence data		
		FOR_EACH_IN_EARRAY(pChart->eaMovementSequencesSubset, DynMovementSequence, pSeq) {
			DynAnimChartMoveRefRunTime *pMoveRef;
			stashFindPointer(pChart->stMoves, pSeq->pcMovementType, &pMoveRef);
			if (SAFE_MEMBER(pMoveRef,bBlank)) {
				dynAnimChartRemoveMovementSequenceFromSubset(pChart, ipSeqIndex);
			}
		} FOR_EACH_END;
	}
	else
	{
		//just use the full set of movement sequences
		pChart->eaMovementSequencesSubset = NULL;
	}

	pChart->bHasJumpingStance = false;
	pChart->bHasFallingStance = false;
	pChart->bHasRisingStance  = false;
	FOR_EACH_IN_EARRAY(pChart->eaStanceWords, const char, pcStance)
	{
		if (pcStance == pcStanceJumping) {
			pChart->bHasJumpingStance = true;
		}
		else if (pcStance == pcStanceFalling) {
			pChart->bHasFallingStance = true;
		}
		else if (pcStance == pcStanceRising) {
			pChart->bHasRisingStance = true;
		}
	}
	FOR_EACH_END;
}

static int cmpChartPriorityNew(const DynAnimChartRunTime**pa, const DynAnimChartRunTime**pb)
{
	const DynAnimChartRunTime *a = *pa;
	const DynAnimChartRunTime *b = *pb;
	return dynAnimCompareStanceWordsPrioritySmall(a->eaStanceWords, b->eaStanceWords, 0, 0, 0);
}

static DynAnimChartRunTime **eaRunTimeCharts;
static DynAnimChartRunTime *dynAnimChartGenerateRunTimeGet(	DynAnimChartLoadTime *pChartLT,
															DynAnimChartLoadTime *pSuperChartLT,
															DynAnimChartRunTime *pBaseChart,
															StashTable stStanceChartGather,
															const char **eaExtraStanceWords,
															const char *pcMovementStance)
{
	DynAnimChartRunTime *pChart;
	const char *key = dynAnimKeyFromStanceWordsEx(eaExtraStanceWords, pcMovementStance);

	if (REF_HANDLE_IS_ACTIVE(pSuperChartLT->hBaseChart))
	{
		FOR_EACH_IN_EARRAY(pBaseChart->eaAllChildCharts, DynAnimChartRunTime, pChkRTChart)
		{
			bool bFoundStances = true;

			if (eaSize(&pChkRTChart->eaStanceWords) !=
					eaSize(&pSuperChartLT->eaStanceWords) +
					eaSize(&eaExtraStanceWords) +
					(pcMovementStance?1:0)) {
				bFoundStances = false;
				continue;
			}

			FOR_EACH_IN_EARRAY(pSuperChartLT->eaStanceWords, const char, pSuperStanceWord) {
				if (eaFind(&pChkRTChart->eaStanceWords, pSuperStanceWord) < 0)
					bFoundStances = false;
			} FOR_EACH_END;
			if (!bFoundStances) continue;

			FOR_EACH_IN_EARRAY(eaExtraStanceWords, const char, pExtraStanceWord) {
				if (eaFind(&pChkRTChart->eaStanceWords, pExtraStanceWord) < 0)
					bFoundStances = false;
			} FOR_EACH_END;
			if (!bFoundStances) continue;

			if (pcMovementStance && eaFind(&pChkRTChart->eaStanceWords, pcMovementStance) < 0) {
				bFoundStances = false;
				continue;
			}

			//bFoundStances == true
			return pChkRTChart;
		}
		FOR_EACH_END;
	}

	if (!stashFindPointer(stStanceChartGather, key, &pChart))
	{
		pChart = StructCreate(parse_DynAnimChartRunTime);
		pChart->pcName = pChartLT->pcName;
		pChart->pcFilename = pChartLT->pcFilename;
		pChart->eVisSet = pSuperChartLT->eVisSet;
		eaCopy(&pChart->eaStanceWords, &pSuperChartLT->eaStanceWords);
		eaPushEArray(&pChart->eaStanceWords, &eaExtraStanceWords);
		if(pcMovementStance)
			eaPush(&pChart->eaStanceWords, pcMovementStance);
		if (REF_HANDLE_IS_ACTIVE(pSuperChartLT->hBaseChart)) {
			DynAnimChartLoadTime *pBC = GET_REF(pSuperChartLT->hBaseChart);
			if (!gConf.bUseMovementGraphs)
				COPY_HANDLE(pChart->hMovementSet, pBC->hMovementSet);
			//If we need to, look back through inheritance hierarchy to find a vis set that's > -1
			
			while (pBC && pChart->eVisSet == -1)
			{
				if (pBC)
					pChart->eVisSet = pBC->eVisSet;
				pBC = GET_REF(pBC->hBaseChart);
			}

		} else {
			if (!gConf.bUseMovementGraphs)
				COPY_HANDLE(pChart->hMovementSet, pSuperChartLT->hMovementSet);
		}
		if (pBaseChart)
			eaPush(&pBaseChart->eaAllChildCharts, pChart);
		eaPush(&eaRunTimeCharts, pChart);
		assert(stashAddPointer(stStanceChartGather, key, pChart, false));
	}
	return pChart;
}

static void dynAnimChartMakeGraphChance(DynAnimGraphChanceRef ***cpy,
										DynAnimChartGraphRefLoadTime *src)
{
	DynAnimGraphChanceRef *pGraphChanceNew = StructCreate(parse_DynAnimGraphChanceRef);
	COPY_HANDLE(pGraphChanceNew->hGraph, src->hGraph);
	pGraphChanceNew->fChance = 1.0;
	eaPush(cpy, pGraphChanceNew);
}

static void dynAnimChartCopyGraphChances(	DynAnimGraphChanceRef ***cpy,
											DynAnimGraphChanceRef ***src)
{
	//used to change load-time weights into run-time probabilities
	F32 weightSoFar = 0.0;
	F32 weightTotal = 0.0;

	FOR_EACH_IN_EARRAY(*src, DynAnimGraphChanceRef, pGraphChanceSrc)
	{
		//hard copy over the chance graphs and weights
		DynAnimGraphChanceRef *pGraphChanceNew = StructCreate(parse_DynAnimGraphChanceRef);
		COPY_HANDLE(pGraphChanceNew->hGraph, pGraphChanceSrc->hGraph);		
		pGraphChanceNew->fChance = (pGraphChanceSrc->fChance < 0.0) ? 0.0 : pGraphChanceSrc->fChance;
		eaPush(cpy, pGraphChanceNew);

		//keep track of the total weight
		weightTotal += pGraphChanceNew->fChance;
	}
	FOR_EACH_END;

	//change load-time weights so they are stacked probabilities (i.e. 0.3, 0.2, 0.5 -> 0.3, 0.5, 1.0)
	//done here so it's cheaper to figure out which one to pick at runtime
	if (weightTotal > 0.0)
	{
		FOR_EACH_IN_EARRAY(*cpy, DynAnimGraphChanceRef, pGraphChanceCpy)
		{
			//normalize with total weight
			weightSoFar += pGraphChanceCpy->fChance;
			pGraphChanceCpy->fChance = weightSoFar/weightTotal;
		}
		FOR_EACH_END;
	}
	else
	{
		int weightCount = eaSize(src);
		FOR_EACH_IN_EARRAY(*cpy, DynAnimGraphChanceRef, pGraphChanceCpy)
		{
			//normalize with count and assume all equally likely
			weightSoFar += 1.0f;
			pGraphChanceCpy->fChance = weightSoFar/weightCount;
		}
		FOR_EACH_END;
	}
}

static void dynAnimChartGenerateRunTimeAddGraph(DynAnimChartLoadTime* pChartLT,
												DynAnimChartRunTime* pChartRT,
												DynAnimChartGraphRefLoadTime* pRefLT,
												bool bMovementSequencer)
{
	if(pRefLT->pcKeyword == pcAnimChartDefaultKey)
	{
		if (bMovementSequencer)
		{
			if (eaSize(&pChartRT->eaMoveDefaultChances) > 0) {
				Alertf("WARNING: Default stance key setup requested by Chart %s was already created for use at runtime by Chart %s, skipping incoming request (check charts for a duplicate stance column & fix any other chart output errors!)", pChartLT->pcName, pChartRT->pcName);
			} else {
				if(eaSize(&pRefLT->eaGraphChances) > 0){
					dynAnimChartCopyGraphChances(&pChartRT->eaMoveDefaultChances, &pRefLT->eaGraphChances);
				} else if (GET_REF(pRefLT->hGraph)){
					dynAnimChartMakeGraphChance(&pChartRT->eaMoveDefaultChances, pRefLT);
				}
			}
		}
		else
		{
			if (eaSize(&pChartRT->eaDefaultChances) > 0) {
				Alertf("WARNING: Default keyword setup requested by Chart %s was already created for use at runtime by Chart %s, skipping incoming request (check charts for a duplicate stance column & fix any other chart output errors!)", pChartLT->pcName, pChartRT->pcName);
			} else {
				if(eaSize(&pRefLT->eaGraphChances) > 0){
					dynAnimChartCopyGraphChances(&pChartRT->eaDefaultChances, &pRefLT->eaGraphChances);
				} else if (GET_REF(pRefLT->hGraph)){
					dynAnimChartMakeGraphChance(&pChartRT->eaDefaultChances, pRefLT);
				}
			}
		}
	}
	else
	{
		//check if the keyword already exist
		bool keywordExist = false;
		if (bMovementSequencer) {
			FOR_EACH_IN_EARRAY(pChartRT->eaMoveGraphRefs, DynAnimChartGraphRefRunTime, pRefRT) {
				if (pRefRT->pcKeyword == pRefLT->pcKeyword) {
					Alertf("WARNING: Stance key setup for %s requested by Chart %s was already created for use at runtime by Chart %s, skipping incoming request (check charts for a duplicate stance column & fix any other chart output errors!)", pRefLT->pcKeyword, pChartLT->pcName, pChartRT->pcName);
					keywordExist = true;
				}
			} FOR_EACH_END;
		} else {
			FOR_EACH_IN_EARRAY(pChartRT->eaGraphRefs, DynAnimChartGraphRefRunTime, pRefRT) {
				if (pRefRT->pcKeyword == pRefLT->pcKeyword) {
					Alertf("WARNING: Keyword setup for %s requested by Chart %s was already created for use at runtime by Chart %s, skipping incoming request (check charts for a duplicate stance column & fix any other chart output errors!)", pRefLT->pcKeyword, pChartLT->pcName, pChartRT->pcName);
					keywordExist = true;
				}
			} FOR_EACH_END;
		}

		if (!keywordExist)
		{
			//copy the values over
			DynAnimChartGraphRefRunTime *pRefRT = StructCreate(parse_DynAnimChartGraphRefRunTime);

			if(eaSize(&pRefLT->eaGraphChances) > 0){
				dynAnimChartCopyGraphChances(&pRefRT->eaGraphChances, &pRefLT->eaGraphChances);
			}
			else if(GET_REF(pRefLT->hGraph)){
				dynAnimChartMakeGraphChance(&pRefRT->eaGraphChances, pRefLT);
			}
			pRefRT->pcKeyword = pRefLT->pcKeyword;
			pRefRT->bBlank = pRefLT->bBlank;

			if (bMovementSequencer) {
				eaPush(&pChartRT->eaMoveGraphRefs, pRefRT);
			} else {
				eaPush(&pChartRT->eaGraphRefs, pRefRT);
			}
		}
	}
}

static void dynAnimChartGenerateRunTimeSub(	DynAnimChartLoadTime *pChartLT,
											DynAnimChartLoadTime *pSuperChartLT,
											DynAnimChartRunTime *pBaseChart,
											StashTable stStanceChartGather)
{
	if(!pChartLT){
		return;
	}

	FOR_EACH_IN_EARRAY(pChartLT->eaGraphRefs, DynAnimChartGraphRefLoadTime, pRefLT)
	{
		DynAnimChartRunTime *pChart = dynAnimChartGenerateRunTimeGet(	pChartLT,
																		pSuperChartLT,
																		pBaseChart,
																		stStanceChartGather,
																		pRefLT->eaStanceWords,
																		NULL );

		dynAnimChartGenerateRunTimeAddGraph(pChartLT, pChart, pRefLT, false);
	}
	FOR_EACH_END;
	FOR_EACH_IN_EARRAY(pChartLT->eaSubCharts, DynAnimSubChartRef, pSubChart)
	{
		dynAnimChartGenerateRunTimeSub(	GET_REF(pSubChart->hSubChart),
										pSuperChartLT,
										pBaseChart,
										stStanceChartGather);
	}
	FOR_EACH_END;
}

static void dynAnimChartGenerateRunTime(DynAnimChartLoadTime *pChartLT)
{
	DynAnimChartRunTime *pBaseChart=NULL;
	StashTable stStanceChartGather;

	if (pChartLT->bIsSubChart)
		return; // Collapsed by parent.

	// Make StashTable of stance keys to charts.
	stStanceChartGather = stashTableCreateWithStringKeys(16, StashDefault);

	if (REF_HANDLE_IS_ACTIVE(pChartLT->hBaseChart))
	{
		// Stance-chart, find the base.
		pBaseChart = RefSystem_ReferentFromString(	hAnimChartDictRunTime,
													REF_HANDLE_GET_STRING(pChartLT->hBaseChart));

		if (!pBaseChart)
		{
			stashTableDestroySafe(&stStanceChartGather);

			// Must have been NULL at load-time too, otherwise we would have created it.
			assert(!GET_REF(pChartLT->hBaseChart)); 
			// No base, error reported during validation.
			// Do nothing.
			return;
		}
	}else{
		// We are a base chart, create and add empty chart (graph/move refs will be added below).
		pBaseChart = dynAnimChartGenerateRunTimeGet(pChartLT,
													pChartLT,
													NULL,
													stStanceChartGather,
													NULL,
													NULL);

		RefSystem_AddReferent(hAnimChartDictRunTime, pBaseChart->pcName, pBaseChart);

		FOR_EACH_IN_REFDICT(hMoveTransitionDict, DynMoveTransition, curTrans)
		{
			if (REF_HANDLE_GET_STRING(curTrans->hChart) == pChartLT->pcName) {
				eaPush(&pBaseChart->eaMoveTransitions, StructClone(parse_DynMoveTransition, curTrans));
			}
		}
		EARRAY_FOREACH_END;
	}

	// Fill in stashtable as we parse GraphRef and MoveRefs and create new charts.

	// MoveGraphs first to ensure RunTime chart is created with this LT's filename.
	// MoveGraphs are not on sub-charts, only on base & stance charts
	FOR_EACH_IN_EARRAY(pChartLT->eaMoveGraphRefs, DynAnimChartGraphRefLoadTime, pRefLT)
	{
		DynAnimChartRunTime *pChart = dynAnimChartGenerateRunTimeGet(	pChartLT,
																		pChartLT,
																		pBaseChart,
																		stStanceChartGather,
																		pRefLT->eaStanceWords,
																		NULL );

		dynAnimChartGenerateRunTimeAddGraph(pChartLT, pChart, pRefLT, true);
	}
	FOR_EACH_END;

	// MoveRefs first to ensure RunTime chart is created with this LT's filename.
	// Move refs not on sub-charts, only on base & stance charts
	FOR_EACH_IN_EARRAY(pChartLT->eaMoveRefs, DynAnimChartMoveRefLoadTime, pRefLT)
	{
		bool moveExist = false;
		DynAnimChartRunTime *pChart = dynAnimChartGenerateRunTimeGet(	pChartLT,
																		pChartLT,
																		pBaseChart,
																		stStanceChartGather,
																		pRefLT->eaStanceWords,
																		pRefLT->pcMovementStance);

		//check if the move already exist
		FOR_EACH_IN_EARRAY(pChart->eaMoveRefs, DynAnimChartMoveRefRunTime, pChkRefRT)
		{
			if (pChkRefRT->pcMovementType == pRefLT->pcMovementType)
				moveExist = true;
		}
		FOR_EACH_END;
		
		if (moveExist)
		{
			Alertf("WARNING: Move setup for stance %s of type %s requested by Chart %s was already created for use at runtime by Chart %s, skipping incoming request (check charts for a duplicate stance column & fix any other chart output errors!)", pRefLT->pcMovementStance, pRefLT->pcMovementType,  pChartLT->pcName, pChart->pcName);
		}
		else
		{
			DynAnimChartMoveRefRunTime *pRef = StructCreate(parse_DynAnimChartMoveRefRunTime);
			F32 fProbSum = 0.f;
			COPY_HANDLE(pRef->hMove, pRefLT->hMove);
			pRef->pcMovementType = pRefLT->pcMovementType;
			pRef->bBlank = pRefLT->bBlank;
			FOR_EACH_IN_EARRAY(pRefLT->eaMoveChances, DynAnimMoveChanceRef, pChanceRefLT) {
				DynAnimMoveChanceRef *pChanceRef = StructCreate(parse_DynAnimMoveChanceRef);
				COPY_HANDLE(pChanceRef->hMove, pChanceRefLT->hMove);
				fProbSum += pChanceRefLT->fChance;
				pChanceRef->fChance = fProbSum;
				eaPush(&pRef->eaMoveChances, pChanceRef);
			} FOR_EACH_END;
			eaPush(&pChart->eaMoveRefs, pRef);
		}
	}
	FOR_EACH_END;

	dynAnimChartGenerateRunTimeSub(pChartLT, pChartLT, pBaseChart, stStanceChartGather);

	stashTableDestroySafe(&stStanceChartGather);
}

static void dynAnimChartGenerateRunTimeCharts(void)
{
	// Clear existing run-time dictionary and all run-time charts
	RefSystem_ClearDictionary(hAnimChartDictRunTime, false);
	FOR_EACH_IN_EARRAY(eaRunTimeCharts, DynAnimChartRunTime, pChart) {
		eaClearStruct(&pChart->eaMovementSequencesSubset, parse_DynMovementSequence);
	} FOR_EACH_END;
	eaClearStruct(&eaRunTimeCharts, parse_DynAnimChartRunTime);
	
	// Walk through LoadTime dictionary, and generate a RunTime chart for each.
	// First base charts (and their implied stance-charts).
	FOR_EACH_IN_REFDICT(hAnimChartDictLoadTime, DynAnimChartLoadTime, pChart)
	{
		if (!REF_HANDLE_IS_ACTIVE(pChart->hBaseChart))
			dynAnimChartGenerateRunTime(pChart);
	}
	FOR_EACH_END;

	// Then stance-charts.
	FOR_EACH_IN_REFDICT(hAnimChartDictLoadTime, DynAnimChartLoadTime, pChart)
	{
		if (REF_HANDLE_IS_ACTIVE(pChart->hBaseChart))
			dynAnimChartGenerateRunTime(pChart);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(eaRunTimeCharts, DynAnimChartRunTime, pChart)
	{
		dynAnimChartRunTimeFixup(pChart);
	}
	FOR_EACH_END;
}

static void dynAnimChartCheckGraphTemplatesMatch(
	const char *pcKeyword,
	DynAnimChartRunTime *pChart,     DynAnimGraph *pGraph,
	DynAnimChartRunTime *pBaseChart, DynAnimGraph *pBaseGraph
	)
{
	if (pGraph && pBaseGraph)
	{
		DynAnimTemplate* pTemplate     = GET_REF(pGraph->hTemplate);
		DynAnimTemplate* pBaseTemplate = GET_REF(pBaseGraph->hTemplate);
		if (pTemplate != pBaseTemplate)
		{
			//AnimFileError(pChart->pcFilename, "Graph %s in Chart %s for keyword %s has a different template (%s) than graph %s in base chart %s with template %s).",
			//Alertf("WARNING: Graph %s in RT Chart %s for Keyword %s has a different Template (%s) than Graph %s in Base RT Chart %s (with Template %s)",
			//printf("WARNING: Graph %s in RT Chart %s for Keyword %s has a different Template (%s) than Graph %s in Base RT Chart %s (with Template %s)",
			verbose_printf("WARNING: Graph %s in RT Chart %s for Keyword %s has a different Template (%s) than Graph %s in Base RT Chart %s (with Template %s)\n",
				pGraph->pcName,
				pChart->pcName,
				pcKeyword,
				pTemplate?pTemplate->pcName:"NOT FOUND",
				pBaseGraph->pcName,
				pBaseChart->pcName,
				pBaseTemplate?pBaseTemplate->pcName:"NOT FOUND"
				);
		}
	}
}

// Call this after all are loaded (or a reload is complete) to fix up the network of chart inheritance
static void dynAnimChartFixInterChartRefs(void)
{
	// generate the run-time data from the load-time data
	dynAnimChartGenerateRunTimeCharts();

	// Don't need to clear old stash tables because they're not going to exist because we just created the run-time charts

	// Now recreate them
	FOR_EACH_IN_REFDICT(hAnimChartDictRunTime, DynAnimChartRunTime, pBaseChart)
	{
		pBaseChart->stChildCharts = stashTableCreateWithStringKeys(4, StashDefault);
		FOR_EACH_IN_EARRAY(pBaseChart->eaAllChildCharts, DynAnimChartRunTime, pChart)
		{
			const char *stance_string;
			// stance chart, not base, not sub (subs don't exist in RunTime charts)
			assert(eaSize(&pChart->eaStanceWords) > 0);
			if (eaSize(&pChart->eaStanceWords) == 1)
			{
				stance_string = pChart->eaStanceWords[0];
			}
			else // must be more than one stance word
			{
				// Sort by priority
				// Must sort the highest priority stance words first (used in comparing Chart priorities later)
				sortStanceWords(pChart->eaStanceWords);
				stance_string = dynAnimKeyFromStanceWords(pChart->eaStanceWords);
			}

			// Adding multi-stance charts to stash table just for validation purposes now
			if (!stashAddPointer(pBaseChart->stChildCharts, stance_string, pChart, false))
			{
				DynAnimChartRunTime *pOtherChart;
				assert(stashFindPointer(pBaseChart->stChildCharts, stance_string, &pOtherChart));
				ErrorFilenameDup(pChart->pcFilename, pOtherChart->pcFilename, stance_string, "stance words");
			}
			if (eaSize(&pChart->eaStanceWords) > 1)
				eaPush(&pBaseChart->eaMultiStanceWordChildCharts, pChart);

			// Check that each graph in our chart matches the template of the graph corresponding to the same keyword in the base chart
			FOR_EACH_IN_EARRAY(pChart->eaGraphRefs, DynAnimChartGraphRefRunTime, pGraphRef)
			{
				DynAnimChartGraphRefRunTime* pBaseGraphRef;
				if (stashFindPointer(pBaseChart->stGraphs, pGraphRef->pcKeyword, &pBaseGraphRef))
				{
					int curGraphChance;
					for (curGraphChance=0; curGraphChance < eaSize(&pGraphRef->eaGraphChances); curGraphChance++)
					{
						int curBaseGraphChance;
						for (curBaseGraphChance=0; curBaseGraphChance < eaSize(&pBaseGraphRef->eaGraphChances); curBaseGraphChance++)
						{
							dynAnimChartCheckGraphTemplatesMatch(
								pGraphRef->pcKeyword,
								pChart,     GET_REF(pGraphRef->eaGraphChances[curGraphChance]->hGraph),
								pBaseChart, GET_REF(pBaseGraphRef->eaGraphChances[curBaseGraphChance]->hGraph)
								);
						}
					}
				}
			}
			FOR_EACH_END;

			// Check that each movement graph in our chart matches the template of the movement graph corresponding to the same keyword in the base chart
			FOR_EACH_IN_EARRAY(pChart->eaMoveGraphRefs, DynAnimChartGraphRefRunTime, pGraphRef)
			{
				DynAnimChartGraphRefRunTime* pBaseGraphRef;
				if (stashFindPointer(pBaseChart->stMovementGraphs, pGraphRef->pcKeyword, &pBaseGraphRef))
				{
					int curGraphChance;
					for (curGraphChance=0; curGraphChance < eaSize(&pGraphRef->eaGraphChances); curGraphChance++)
					{
						int curBaseGraphChance;
						for (curBaseGraphChance=0; curBaseGraphChance < eaSize(&pBaseGraphRef->eaGraphChances); curBaseGraphChance++)
						{
							dynAnimChartCheckGraphTemplatesMatch(
								pGraphRef->pcKeyword,
								pChart,     GET_REF(pGraphRef->eaGraphChances[curGraphChance]->hGraph),
								pBaseChart, GET_REF(pBaseGraphRef->eaGraphChances[curBaseGraphChance]->hGraph)
								);
						}
					}
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	// And now, generate priorities
	FOR_EACH_IN_REFDICT(hAnimChartDictRunTime, DynAnimChartRunTime, pChart)
	{
		// Only base charts in the dictionary
		pChart->fChartPriority = -1.0f;
		if (eaSize(&pChart->eaAllChildCharts))
		{
			F32 priority=1;
			// This is a base chart with children
			// Stances on children should have been sorted above
			// Sort the children
			eaQSort(pChart->eaAllChildCharts, cmpChartPriorityNew);
			FOR_EACH_IN_EARRAY_FORWARDS(pChart->eaAllChildCharts, DynAnimChartRunTime, pChildChart)
			{
				pChildChart->fChartPriority = priority;
				priority++;
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
}

AUTO_COMMAND;
void danimDumpCharts(void)
{
	FOR_EACH_IN_REFDICT(hAnimChartDictRunTime, DynAnimChartRunTime, pChart)
	{
		if (eaSize(&pChart->eaAllChildCharts))
		{
			// This is a base chart with children
			printf("BASE CHART %s:\n", pChart->pcName);
			printf("\t\t%d children, %d multi-stance children\n"
				"\t\t%d keywords, %d stance keys, %d moves\n",
				eaSize(&pChart->eaAllChildCharts),
				eaSize(&pChart->eaMultiStanceWordChildCharts),
				stashGetCount(pChart->stGraphs),
				stashGetCount(pChart->stMovementGraphs),
				stashGetCount(pChart->stMoves));
			FOR_EACH_IN_EARRAY(pChart->eaAllChildCharts, DynAnimChartRunTime, pChildChart)
			{
				bool bNeedComma=false;
				printf("\t%s: %f (", pChildChart->pcName, pChildChart->fChartPriority);
				FOR_EACH_IN_EARRAY(pChildChart->eaStanceWords, const char, pStanceWord)
				{
					printf("%s%s", bNeedComma?", ":"", pStanceWord);
					bNeedComma = true;
				}
				FOR_EACH_END;
				printf(")");
				printf(" %d keywords, %d stance keys, %s, %s, %d moves\n",
					stashGetCount(pChildChart->stGraphs),
					stashGetCount(pChildChart->stMovementGraphs),
					((eaSize(&pChildChart->eaDefaultChances) > 0)?"IDLE":"no idle"),
					((eaSize(&pChildChart->eaMoveDefaultChances) > 0)?"BASIC MOVE":"no basic move"),
					stashGetCount(pChildChart->stMoves)
					);
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDynAnimChartLoadtime(DynAnimChartLoadTime* pChart, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_PRE_STRUCTCOPY:
		{
		}

		xcase FIXUPTYPE_POST_STRUCTCOPY:
		{
			dynAnimChartLoadTimeFixup(pChart);
		}

		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			dynAnimChartLoadTimeFixup(pChart);
		}
		xcase FIXUPTYPE_POST_BIN_READ:
		{
			dynAnimChartLoadTimeFixup(pChart);
		}
		xcase FIXUPTYPE_PRE_TEXT_WRITE:
		case FIXUPTYPE_PRE_BIN_WRITE:
		{
			dynAnimChartLoadTimeFixup(pChart);
		}
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDynAnimChartRunTime(DynAnimChartRunTime* pChart, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_DESTRUCTOR:
		{
			stashTableDestroySafe(&pChart->stGraphs);
			stashTableDestroySafe(&pChart->stMovementGraphs);
			stashTableDestroySafe(&pChart->stChildCharts);
			stashTableDestroySafe(&pChart->stMoves);
			eaDestroy(&pChart->eaMultiStanceWordChildCharts);
			eaDestroy(&pChart->eaAllChildCharts);
		}
	}
	return PARSERESULT_SUCCESS;
}

static int dynAnimChartResValidateLoadTimeCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, DynAnimChartLoadTime *pChart, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
		{
			dynAnimChartLoadTimeVerify(pChart);
		}
		return VALIDATE_HANDLED;
		xcase RESVALIDATE_POST_BINNING:
		{
		}
		return VALIDATE_HANDLED;
		xcase RESVALIDATE_FIX_FILENAME:
		{
			resFixPooledFilename((char**)&pChart->pcFilename, "dyn/animchart", pChart->pcScope, pChart->pcName, "achart");
		}
		return VALIDATE_HANDLED;
		xcase RESVALIDATE_CHECK_REFERENCES:
		{
			dynAnimChartVerifyReferences(pChart);
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

static void dynAnimChartRefDictLoadTimeCallback(enumResourceEventType eType, const char *pDictName, const char *pRefData, DynAnimChartLoadTime *pChart, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
	{
		/*
		dynAnimChartLoadTimeFixup(pChart);
		if (pChart->bIsSubChart)
		{
			// TODO: Need to fixup parent too?
		}
		*/
	}
	if (bLoadedOnce)
		danimForceDataReload();
}

AUTO_RUN;
void registerAnimChartDictionary(void)
{
	pcAnimChartDefaultKey = allocAddString(ANIM_CHART_DEFAULT_KEY);
	pcStanceMoving  = allocAddString("Moving");
	pcStanceJumping = allocAddString("Jumping");
	pcStanceFalling = allocAddString("Falling");
	pcStanceRising  = allocAddString("Rising");

	hAnimChartDictRunTime = RefSystem_RegisterSelfDefiningDictionary("AnimChartRunTime", false, parse_DynAnimChartRunTime, true, true, NULL);
	hAnimChartDictLoadTime = RefSystem_RegisterSelfDefiningDictionary(ANIM_CHART_EDITED_DICTIONARY, false, parse_DynAnimChartLoadTime, true, true, NULL);

	resDictManageValidation(hAnimChartDictLoadTime, dynAnimChartResValidateLoadTimeCB);

	if (IsServer())
	{
		resDictProvideMissingResources(hAnimChartDictLoadTime);
		if (isDevelopmentMode() || isProductionEditMode())
		{
			resDictMaintainInfoIndex(hAnimChartDictLoadTime, ".name", ".scope", NULL, ".filetype", NULL);
		}
	}
	else if (IsClient())
	{
		resDictRequestMissingResources(hAnimChartDictLoadTime, 8, false, resClientRequestSendReferentCommand);
	}
	////resDictProvideMissingRequiresEditMode(hAnimChartDict);
	resDictRegisterEventCallback(hAnimChartDictLoadTime, dynAnimChartRefDictLoadTimeCallback, NULL);
}

void dynAnimChartLoadAll(void)
{
	if (!bLoadedOnce)
	{
		if (IsServer())
		{
			resLoadResourcesFromDisk(hAnimChartDictLoadTime, "dyn/animchart", ".achart", NULL, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
			dynAnimChartFixInterChartRefs();
		}
		else if (IsClient())
		{
			loadstart_printf("Loading DynAnimCharts...");
			ParserLoadFilesToDictionary("dyn/animchart", ".achart", "DynAnimChart.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, hAnimChartDictLoadTime);
			dynAnimChartFixInterChartRefs();
			loadend_printf(" done (%d LoadTime, %d Base, %d RunTime)", RefSystem_GetDictionaryNumberOfReferents(hAnimChartDictLoadTime), RefSystem_GetDictionaryNumberOfReferents(hAnimChartDictRunTime), eaSize(&eaRunTimeCharts));
		}
	}
	bLoadedOnce = true;
}

void dynAnimChartReloadAll(void)
{
	loadstart_printf("Reloading DynAnimCharts...");
	
	//this should only be run without shared memory since it reprocesses the
	//existing data.. to work with shared memory it would have to reload the
	//files from disk (which is significantly slower)
	assert(sharedMemoryGetMode() != SMM_ENABLED);

	FOR_EACH_IN_REFDICT(hAnimChartDictLoadTime, DynAnimChartLoadTime, pChart)
	{
		dynAnimChartLoadTimeFixup(pChart);
		if (!dynAnimChartLoadTimeVerify(pChart)){
			;
		}
	}
	FOR_EACH_END;
	dynAnimChartFixInterChartRefs();
	loadend_printf(" done (%d LoadTime, %d Base, %d RunTime)", RefSystem_GetDictionaryNumberOfReferents(hAnimChartDictLoadTime), RefSystem_GetDictionaryNumberOfReferents(hAnimChartDictRunTime), eaSize(&eaRunTimeCharts));
}

void dynAnimChartMoveRefFree(DynAnimChartMoveRefLoadTime* pRef)
{
	REMOVE_HANDLE(pRef->hMove);
	FOR_EACH_IN_EARRAY(pRef->eaMoveChances, DynAnimMoveChanceRef, pChanceRef) {
		REMOVE_HANDLE(pChanceRef->hMove);
	} FOR_EACH_END
	eaDestroyStruct(&pRef->eaMoveChances, parse_DynAnimMoveChanceRef);
	free(pRef);
}

void dynAnimChartMovementSetChanged(DynAnimChartLoadTime *pChart)
{
	DynAnimChartLoadTime* pBaseChart = GET_REF(pChart->hBaseChart);
	DynMovementSet* pMovementSet = pBaseChart?GET_REF(pBaseChart->hMovementSet):GET_REF(pChart->hMovementSet);
	DynAnimChartMoveRefLoadTime** eaOldRefs = pChart->eaMoveRefs;
	pChart->eaMoveRefs = NULL;
	if (pMovementSet)
	{
		// TODO: Do this for each set of stances on the chart
		FOR_EACH_IN_EARRAY_FORWARDS(pMovementSet->eaMovementSequences, DynMovementSequence, pSeq)
		{
			DynAnimChartMoveRefLoadTime* pRef = calloc(sizeof(DynAnimChartMoveRefLoadTime), 1);
			pRef->pcMovementType = pSeq->pcMovementType;
			eaPush(&pChart->eaMoveRefs, pRef);
			pRef->eaMoveChances = NULL;

			FOR_EACH_IN_EARRAY_FORWARDS(eaOldRefs, DynAnimChartMoveRefLoadTime, pOldRef)
			{
				if (pOldRef->pcMovementType == pRef->pcMovementType)
				{
					if (REF_HANDLE_IS_ACTIVE(pOldRef->hMove)) {
						REF_HANDLE_SET_FROM_STRING(hMovementSetDict, REF_HANDLE_GET_STRING(pOldRef->hMove), pRef->hMove);
					}

					FOR_EACH_IN_EARRAY(pOldRef->eaMoveChances, DynAnimMoveChanceRef, pChanceRefOld) {
						DynAnimMoveChanceRef *pChanceRef = calloc(sizeof(DynAnimMoveChanceRef), 1);
						REF_HANDLE_SET_FROM_STRING(hMovementSetDict, REF_HANDLE_GET_STRING(pChanceRefOld->hMove), pChanceRef->hMove);
						pChanceRef->fChance = pChanceRefOld->fChance;
						eaPush(&pRef->eaMoveChances, pChanceRefOld);
					} FOR_EACH_END;
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}

	eaDestroyEx(&eaOldRefs, dynAnimChartMoveRefFree);

	danimForceDataReload();
}

DynAnimChartStack *dynAnimChartStackCreate(const DynAnimChartRunTime *pBaseChart)
{
	DynAnimChartStack* pChartStack = MP_ALLOC(DynAnimChartStack);
	eaCreate(&pChartStack->eaChartStackMutable);
	eaCreate(&pChartStack->eaStanceWordsMutable);
	eaPush(&pChartStack->eaChartStackMutable, pBaseChart);
	pChartStack->bStackDirty = true;
	return pChartStack;
}

DynAnimChartStack *dynAnimChartStackFindByBaseChart(DynAnimChartStack** eaChartStacks, const DynAnimChartRunTime* pBaseChart)
{
	FOR_EACH_IN_EARRAY(eaChartStacks, DynAnimChartStack, pChartStack) {
		if (pChartStack->eaChartStack[0] == pBaseChart)
			return pChartStack;
	} FOR_EACH_END;
	return NULL;
}

void dynAnimChartStackDestroy(DynAnimChartStack* pChartStack)
{
	eaDestroy(&pChartStack->eaChartStackMutable);
	eaDestroy(&pChartStack->eaStanceWordsMutable);
	MP_FREE(DynAnimChartStack, pChartStack);
}

S32 dynAnimChartStackGetMoveSeq(const DynSkeleton *pSkeleton,
								DynAnimChartStack* pChartStack,
								const DynMovementSequence** pMovementSequenceInOut,
								const SkelInfo *pSkelInfo,
								const DynMoveSeq** pMoveSeqOut,
								const DynAnimChartRunTime** pChartOut,
								const DynMove *pCurMove)
{
	// MOVEMENT GRAPHS TODO

	if (pChartStack)
	{
		// update the chart stack (has internal check for dirty bit)
		dynAnimChartStackSetFromStanceWords(pChartStack);

		// must walk backwards so we go from highest priority to lowest
		FOR_EACH_IN_EARRAY(pChartStack->eaChartStack, const DynAnimChartRunTime, pChart)
		{
			const DynMovementSequence* pSubSequence = NULL;
			DynAnimChartMoveRefRunTime *pMoveRef = NULL;
			DynMove* pMove = NULL;

			if (pChart->uiNumMovementBlanks) {
				if (pSubSequence = dynSkeletonGetCurrentMovementSequence(pSkeleton,pChart->eaMovementSequencesSubset)) {
					if (!stashFindPointer(pChart->stMoves, pSubSequence->pcMovementType, &pMoveRef))
						continue;
				} else  continue;
			}
			else
			{
				if (!stashFindPointer(pChart->stMoves, (*pMovementSequenceInOut)->pcMovementType, &pMoveRef))
					continue;
			}

			if (eaSize(&pMoveRef->eaMoveChances) > 1)
			{
				DynMove *pChanceMove = NULL;

				//determine 0 <= rf < 1.0
				F32 rf = randomPositiveF32();

				//find the graph based on rf and stacked probabilities
				FOR_EACH_IN_EARRAY(pMoveRef->eaMoveChances, DynAnimMoveChanceRef, pMoveChanceRef)
				{
					pChanceMove = GET_REF(pMoveChanceRef->hMove);

					if (pCurMove && pCurMove == pChanceMove)
					{
						pMove = pChanceMove;
						break;
					}
					else if (rf <= pMoveChanceRef->fChance && pChanceMove)
					{
						pMove = pChanceMove;
					}
				}
				FOR_EACH_END;
			} else if (eaSize(&pMoveRef->eaMoveChances)) {
				pMove = GET_REF(pMoveRef->eaMoveChances[0]->hMove);
			} else {
				pMove = GET_REF(pMoveRef->hMove);
			}

			if (pMove)
			{
				*pMoveSeqOut = dynMoveSeqFromDynMove(pMove, pSkelInfo);

				if(!*pMoveSeqOut){
					if(pChartOut){
						*pChartOut = NULL;
					}
					return 0;
				}

				if(pChartOut){
					*pChartOut = pChart;
				}

				if (pSubSequence) {
					(*pMovementSequenceInOut) = pSubSequence;
				}

				return 1;
			}
		}
		FOR_EACH_END;
	}

	*pMoveSeqOut = NULL;
	if(pChartOut){
		*pChartOut = NULL;
	}
	return 0;
}

DynAnimGraph* dynAnimChartStackFindGraph(	DynAnimChartStack* pChartStack,
											const char* pcKeyword,
											bool bMovementSequencer,
											const DynAnimChartRunTime** pChartOut)
{
	if (pChartStack)
	{
		// update the chart stack (has internal check for dirty bit)
		dynAnimChartStackSetFromStanceWords(pChartStack);

		// must walk backwards so we go from highest priority to lowest
		FOR_EACH_IN_EARRAY(pChartStack->eaChartStack, const DynAnimChartRunTime, pChart)
		{
			DynAnimChartGraphRefRunTime* pRef;
			if (!bMovementSequencer && stashFindPointer(pChart->stGraphs, pcKeyword, &pRef) ||
				 bMovementSequencer && stashFindPointer(pChart->stMovementGraphs, pcKeyword, &pRef))
			{
				if (pRef->bBlank)
				{
					if(pChartOut){
						*pChartOut = NULL;
					}
					return NULL;
				}
				else if (eaSize(&pRef->eaGraphChances) > 0)
				{
					//determine 0 <= rf < 1.0
					F32 rf = randomPositiveF32();

					//find the graph based on rf and stacked probabilities
					FOR_EACH_IN_EARRAY(pRef->eaGraphChances, DynAnimGraphChanceRef, pGraphChanceRef)
					{
						DynAnimGraph *pRetGraph = GET_REF(pGraphChanceRef->hGraph);
						if (rf <= pGraphChanceRef->fChance && pRetGraph && !pRetGraph->bInvalid)
						{
							if(pChartOut){
								*pChartOut = pChart;
							}
							return pRetGraph;
						}
					}
					FOR_EACH_END;
				}
			}
		}
		FOR_EACH_END;
	}

	if(pChartOut){
		*pChartOut = NULL;
	}

	return NULL;
}

// Might eventually want to replace this with just adding all the charts and sorting, but for small chart stacks a simple linear search merge sort is probably just as fast.
static void dynAnimChartStackInsertChart(DynAnimChartStack* pChartStack, const DynAnimChartRunTime* pChart)
{
	//no need to call dynAnimChartStackSetFromStanceWords(pChartStack) here
	//we should be updating the chart stack right now!
	
	FOR_EACH_IN_EARRAY(pChartStack->eaChartStack, const DynAnimChartRunTime, pIterChart)
	{
		if (pChart->fChartPriority > pIterChart->fChartPriority)
		{
			eaInsert(&pChartStack->eaChartStackMutable, pChart, ipIterChartIndex+1);
			return;
		}
	}
	FOR_EACH_END;
}

void dynAnimChartStackSetFromStanceWords(DynAnimChartStack* pChartStack)
{
	if (pChartStack &&
		TRUE_THEN_RESET(pChartStack->bStackDirty))
	{
		const DynAnimChartRunTime* pBaseChart = eaGet(&pChartStack->eaChartStack, 0);

		// make sure there is a default chart at slot 0, and it's a base chart
		assert(pBaseChart && pBaseChart->fChartPriority < 0.0f);

		// Easiest to just wipe current charts and recreate the chart stack in one go
		eaRemoveTail(&pChartStack->eaChartStackMutable, 1);

		// First add single stance word charts
		FOR_EACH_IN_EARRAY(pChartStack->eaStanceWords, const char, pcStanceWord)
		{
			DynAnimChartRunTime* pChart;
			if (stashFindPointer(pBaseChart->stChildCharts, pcStanceWord, &pChart))
			{
				dynAnimChartStackInsertChart(pChartStack, pChart);
			}
		}
		FOR_EACH_END;

		// Now check multi-stanceword charts
		// Loop through all possible multi-stanceword charts, then compare each of their requirement lists to our current stance word list
		// If any required words are moving, move on to the next list
		// TODO Optimize (new algorithm or cache results)
		FOR_EACH_IN_EARRAY(pBaseChart->eaMultiStanceWordChildCharts, DynAnimChartRunTime, pChart)
		{
			bool bMatch = true;
			FOR_EACH_IN_EARRAY(pChart->eaStanceWords, const char, pcStanceWord)
			{
				if (eaFind(&pChartStack->eaStanceWords, pcStanceWord) < 0)
				{
					bMatch = false;
					break;
				}
			}
			FOR_EACH_END;
			if (bMatch) // found all the required words
				dynAnimChartStackInsertChart(pChartStack, pChart);
		}
		FOR_EACH_END;

		// make sure we haven't messed up the base chart:
		pBaseChart = eaGet(&pChartStack->eaChartStack, 0);
		assert(pBaseChart && pBaseChart->fChartPriority < 0.0f);
	}
}

void dynAnimChartStackSetStanceWord(DynAnimChartStack* pChartStack, const char* pcStance, const char** eaMovementStances, const char** eaDebugStances, U32 uiMovement)
{
	if (eaFind(&pChartStack->eaStanceWords, pcStance) < 0)
	{
		bool bInserted = false;

		//add in priority order so they'll be ready for post-idle comparison tests later
		FOR_EACH_IN_EARRAY_FORWARDS(pChartStack->eaStanceWordsMutable, const char, pcChkStance)
			{
				if (dynAnimCompareStanceWordPriority(&pcChkStance, &pcStance) > 0) {
					eaInsert(&pChartStack->eaStanceWordsMutable, pcStance, ipcChkStanceIndex);
					bInserted = true;
					break;
				}
			}
		FOR_EACH_END;
		if (!bInserted) eaPush(&pChartStack->eaStanceWordsMutable, pcStance);

		pChartStack->bStackDirty = true;

		if(!gConf.bUseMovementGraphs)
		{
			DynMovementSet* set = GET_REF(pChartStack->eaChartStack[0]->hMovementSet);
			if (set) {
				if(eaFind(&set->eaInterruptingMovementStances, pcStance) >= 0){
					pChartStack->interruptingMovementStanceCount++;
				}
				if(eaFind(&set->eaDirectionMovementStances, pcStance) >= 0){
					pChartStack->directionMovementStanceCount++;
				}
			}
		}
		else if (uiMovement)
		{
			if (dynAnimMovementStanceKeyValid(pcStance))
			{
				pChartStack->interruptingMovementStanceCount++;

				if (pChartStack->bMovement)
				{
					if (eaFindAndRemoveFast(&pChartStack->eaRemovedStanceKeys, pcStance) < 0)
						eaPush(&pChartStack->eaStanceKeys, pcStance);

					if (pcStance == pChartStack->pcPlayingStanceKey)
						pChartStack->bStopped = false;
				}
			}
			else // dynAnimMovementStanceValid(pcStance) assumed true since uiMovement is set
			{
				if (pChartStack->bMovement)
				{
					if (eaFindAndRemoveFast(&pChartStack->eaRemovedStanceFlags, pcStance) < 0)
						eaPush(&pChartStack->eaStanceFlags, pcStance);
				}
			}

			if (pcStance == pcStanceMoving) {
				pChartStack->directionMovementStanceCount++;
			}
		}
	}
}

void dynAnimChartStackClearStanceWord(DynAnimChartStack* pChartStack, const char* pcStance, const char** eaMovementStances, const char** eaDebugStances, U32 uiMovement)
{
	if (eaFindAndRemove(&pChartStack->eaStanceWordsMutable, pcStance) >= 0)
	{
		pChartStack->bStackDirty = true;

		if (!gConf.bUseMovementGraphs)
		{
			DynMovementSet* set = GET_REF(pChartStack->eaChartStack[0]->hMovementSet);
			if(set){
				if(eaFind(&set->eaInterruptingMovementStances, pcStance) >= 0){
					pChartStack->interruptingMovementStanceCount--;
				}
				if(eaFind(&set->eaDirectionMovementStances, pcStance) >= 0){
					pChartStack->directionMovementStanceCount--;
				}
			}
		}
		else if (uiMovement)
		{
			if (dynAnimMovementStanceKeyValid(pcStance))
			{
				pChartStack->interruptingMovementStanceCount--;

				if (pChartStack->bMovement)
				{
					if (eaFindAndRemoveFast(&pChartStack->eaStanceKeys, pcStance) < 0)
						eaPush(&pChartStack->eaRemovedStanceKeys, pcStance);

					if (pcStance == pChartStack->pcPlayingStanceKey)
						pChartStack->bStopped = true;
				}
			}
			else
			{
				if (pChartStack->bMovement)
				{
					if (eaFindAndRemoveFast(&pChartStack->eaStanceFlags, pcStance) < 0)
						eaPush(&pChartStack->eaRemovedStanceFlags, pcStance);
				}
			}

			if (pcStance == pcStanceMoving) {
				pChartStack->directionMovementStanceCount--;
			}
		}
	}
}

static void dynAnimStancesLoadAllInternal(void)
{
	// Clean up old one
	eaDestroy(&stance_list.eaMovementStances);
	stashTableDestroySafe(&stance_list.stStances);
	StructDeInit(parse_DynAnimStancesList, &stance_list);

	if (dynAnimStanceList)
		NameList_Bucket_Clear(dynAnimStanceList);
	else
		dynAnimStanceList = CreateNameList_Bucket();

	// Load, verify, and populate
	ParserLoadFilesShared("SM_DynAnimStances", "dyn/AnimStance", ".AStance", "DynAnimStances.bin", PARSER_BINS_ARE_SHARED|(gConf.bNewAnimationSystem?0:PARSER_OPTIONALFLAG), parse_DynAnimStancesList, &stance_list);
	stance_list.stStances = stashTableCreateWithStringKeys(32, StashDefault);
	FOR_EACH_IN_EARRAY(stance_list.eaStances, DynAnimStanceData, pStance)
	{
		int i;

		if (pStance->pcTag == pcTagMovementKeyword) {
			eaPush(&stance_list.eaMovementStances, pStance);
		}

		if (!stashAddPointer(stance_list.stStances, pStance->pcName, pStance, false))
		{
			// dup
			DynAnimStanceData *pOtherStance;
			assert(stashFindPointer(stance_list.stStances, pStance->pcName, &pOtherStance));
			ErrorFilenameDup(pStance->pcFileName, pOtherStance->pcFileName, pStance->pcName, "Stance");
		}

		for (i=0; i<ipStanceIndex; i++)
		{
			DynAnimStanceData *pOtherStance = stance_list.eaStances[i];
			if (pStance->fStancePriority == pOtherStance->fStancePriority)
			{
				ErrorFilenameDup(pStance->pcFileName, pOtherStance->pcFileName, pStance->pcName, "Stance Priority");
			}
		}

		NameList_Bucket_AddName(dynAnimStanceList, pStance->pcName);
	}
	FOR_EACH_END;
}

static void dynAnimStancesLoadAllDict(void)
{
	if (!bLoadedOnceStances)
	{
		if (IsServer())
		{
			resLoadResourcesFromDisk(hStanceDict, "dyn/animstance", ".astance", NULL, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
		}
		else if (IsClient())
		{
			ParserLoadFilesToDictionary("dyn/animstance", ".astance", "DynAnimStances.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, hStanceDict);
		}
	}
	bLoadedOnceStances = true;
}

static void dynAnimStancesReload(const char *relpath, int when)
{
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	sharedMemoryUnshare(stance_list.eaStances);
	sharedMemoryEnableEditorMode();

	bLoadedOnceStances = false;
	dynAnimStancesLoadAllInternal();
	RefSystem_ClearDictionary(hStanceDict, false);
	dynAnimStancesLoadAllDict();

	danimForceDataReload(); // New priorities need to take effect
}

void dynAnimStancesLoadAll(void)
{
	pcTagDetail = allocAddString("Detail");
	pcTagMovement = allocAddString("Movement");
	pcTagMovementKeyword = allocAddString("MovementKeyword");

	dynAnimStancesLoadAllInternal();
	dynAnimStancesLoadAllDict();
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/AnimStance/*.AStance", dynAnimStancesReload);
}

static int dynAnimStanceResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, DynAnimStanceData *pStance, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
		{
			if (pStance->pcBankingNodeOverride &&
				strcmpi(pStance->pcBankingNodeOverride, "AltBankingNodeAlias") != 0)
			{
				AnimFileError(	pStance->pcFileName,
								"Found invalid banking override node %s specified for stance %s, you are currently only allowed to use AltBankingNodeAlias",
								pStance->pcBankingNodeOverride,
								pStance->pcName);
			}
		}
		return VALIDATE_HANDLED;
		xcase RESVALIDATE_POST_BINNING:
		{
		}
		return VALIDATE_HANDLED;
		xcase RESVALIDATE_FIX_FILENAME:
		{
		}
		return VALIDATE_HANDLED;
		xcase RESVALIDATE_CHECK_REFERENCES:
		{
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void registerStancesDictionary(void)
{
	hStanceDict = RefSystem_RegisterSelfDefiningDictionary(STANCE_DICTIONARY, false, parse_DynAnimStanceData, true, true, NULL);

	resDictManageValidation(hStanceDict, dynAnimStanceResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(hStanceDict);
		if (isDevelopmentMode() || isProductionEditMode())
		{
			resDictMaintainInfoIndex(hStanceDict, ".name", ".scope", NULL, ".filetype", NULL);
		}
	}
	else if (IsClient())
	{
		resDictRequestMissingResources(hStanceDict, 8, false, resClientRequestSendReferentCommand);
	}
	////resDictProvideMissingRequiresEditMode(hStanceDict);
	//resDictRegisterEventCallback(hStanceDict, dynAnimStanceRefDictLoadTimeCallback, NULL);
}

bool dynAnimStanceValid(const char* pcStance)
{
	if (!gConf.bNewAnimationSystem)
		return true;
	assert(stance_list.stStances);
	return stashFindPointer(stance_list.stStances, pcStance, NULL);
}

bool dynAnimMovementStanceKeyValid(const char* pcStance)
{
	DynAnimStanceData* pStance = NULL;
	if (!gConf.bNewAnimationSystem)
		return true;
	assert(stance_list.stStances);
	if (stashFindPointer(stance_list.stStances, pcStance, &pStance))
		return pStance->pcTag == pcTagMovementKeyword;
	return false;
}

bool dynAnimMovementStanceValid(const char* pcStance)
{
	DynAnimStanceData* pStance = NULL;
	if (!gConf.bNewAnimationSystem)
		return true;
	assert(stance_list.stStances);
	if (stashFindPointer(stance_list.stStances, pcStance, &pStance))
		return	pStance->pcTag == pcTagMovementKeyword ||
				pStance->pcTag == pcTagMovement;
	return false;
}

F32 dynAnimStancePriority(const char* pcStance)
{
	DynAnimStanceData *stance_data;
	if (stashFindPointer(stance_list.stStances, pcStance, &stance_data))
		return stance_data->fStancePriority;
	return 0;
}

int dynAnimStanceIndex(const char* pcStance)
{
	DynAnimStanceData *stance_data;
	if (stashFindPointer(stance_list.stStances, pcStance, &stance_data))
		return eaFind(&stance_list.eaStances, stance_data);
	return -1;
}

int dynAnimMovementStanceIndex(const char* pcStance)
{
	DynAnimStanceData* stance_data;
	if (stashFindPointer(stance_list.stStances, pcStance, &stance_data))
		return eaFind(&stance_list.eaMovementStances, stance_data);
	return -1;
}

static int dynAnimChartGraphChanceGetSearchStringCount(const DynAnimGraphChanceRef *pGraphChance, const char *pcSearchText)
{
	int count = 0;

	if (REF_HANDLE_IS_ACTIVE(pGraphChance->hGraph) && strstri(REF_HANDLE_GET_STRING(pGraphChance->hGraph),	pcSearchText))	count++;

	return count;
}

static int dynAnimChartGraphRefGetSearchStringCount(const DynAnimChartGraphRefLoadTime *pGraphRef, const char *pcSearchText)
{
	int count = 0;

	if (pGraphRef->pcKeyword && strstri(pGraphRef->pcKeyword, pcSearchText))
		count++;

	if (REF_HANDLE_IS_ACTIVE(pGraphRef->hGraph) && strstri(REF_HANDLE_GET_STRING(pGraphRef->hGraph), pcSearchText))
		count++;

	FOR_EACH_IN_EARRAY(pGraphRef->eaGraphChances, DynAnimGraphChanceRef, pChance) {
		count += dynAnimChartGraphChanceGetSearchStringCount(pChance, pcSearchText);
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pGraphRef->eaStanceWords, const char, pcStanceWord) {
		if (pcStanceWord && strstri(pcStanceWord, pcSearchText))
			count++;
	} FOR_EACH_END;

	return count;
}

static int dynAnimChartMoveRefGetSearchStringCount(const DynAnimChartMoveRefLoadTime *pMoveRef, const char *pcSearchText)
{
	int count = 0;

	if (pMoveRef->pcMovementType	&& strstri(pMoveRef->pcMovementType,	pcSearchText))	count++;

	if (REF_HANDLE_IS_ACTIVE(pMoveRef->hMove)	&& strstri(REF_HANDLE_GET_STRING(pMoveRef->hMove),	pcSearchText)) count++;

	FOR_EACH_IN_EARRAY(pMoveRef->eaMoveChances, DynAnimMoveChanceRef, pChanceRef) {
		if (REF_HANDLE_IS_ACTIVE(pChanceRef->hMove)	&& strstri(REF_HANDLE_GET_STRING(pChanceRef->hMove), pcSearchText)) count++;
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pMoveRef->eaStanceWords, const char, pcStanceWord) {
		if (pcStanceWord	&& strstri(pcStanceWord,	pcSearchText))	count++;
	} FOR_EACH_END;

	if (pMoveRef->pcMovementStance	&& strstri(pMoveRef->pcMovementStance,	pcSearchText))	count++;

	return count;
}

static int dynAnimChartSubChartRefGetSearchStringCount(const DynAnimSubChartRef *pSubChart, const char *pcSearchText)
{
	int count = 0;

	if (REF_HANDLE_IS_ACTIVE(pSubChart->hSubChart) && strstri(REF_HANDLE_GET_STRING(pSubChart->hSubChart),	pcSearchText))	count++;

	return count;
}

int dynAnimChartGetSearchStringCount(const DynAnimChartLoadTime *pChart, const char *pcSearchText)
{
	int count = 0;

	if (pChart->pcName		&& strstri(pChart->pcName,     pcSearchText)) count++;
	if (pChart->pcFilename	&& strstri(pChart->pcFilename, pcSearchText)) count++;
	if (pChart->pcComments	&& strstri(pChart->pcComments, pcSearchText)) count++;
	if (pChart->pcScope		&& strstri(pChart->pcScope,    pcSearchText)) count++;
	if (pChart->pcFileType	&& strstri(pChart->pcFileType, pcSearchText)) count++;
	
	FOR_EACH_IN_EARRAY(pChart->eaStanceWords, const char, pcStanceWord) {
		if (pcStanceWord && strstri(pcStanceWord, pcSearchText)) count++;
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pChart->eaValidStances, const char, pcValidStanceWord) {
		if (pcValidStanceWord && strstri(pcValidStanceWord, pcSearchText)) count++;
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pChart->eaValidKeywords, const char, pcKeyword) {
		if (pcKeyword && strstri(pcKeyword, pcSearchText)) count++;
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pChart->eaValidMoveKeywords, const char, pcKeyword) {
		if (pcKeyword && strstri(pcKeyword, pcSearchText)) count++;
	} FOR_EACH_END;

	if (REF_HANDLE_IS_ACTIVE(pChart->hBaseChart)	&& strstri(REF_HANDLE_GET_STRING(pChart->hBaseChart),	pcSearchText))	count++;
	if (REF_HANDLE_IS_ACTIVE(pChart->hMovementSet)	&& strstri(REF_HANDLE_GET_STRING(pChart->hMovementSet),	pcSearchText))	count++;

	FOR_EACH_IN_EARRAY(pChart->eaGraphRefs, DynAnimChartGraphRefLoadTime, pGraphRef) {
		count += dynAnimChartGraphRefGetSearchStringCount(pGraphRef, pcSearchText);
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pChart->eaMoveGraphRefs, DynAnimChartGraphRefLoadTime, pMoveGraphRef) {
		count += dynAnimChartGraphRefGetSearchStringCount(pMoveGraphRef, pcSearchText);
	} FOR_EACH_END;
	
	FOR_EACH_IN_EARRAY(pChart->eaMoveRefs, DynAnimChartMoveRefLoadTime, pMoveRef) {
		count += dynAnimChartMoveRefGetSearchStringCount(pMoveRef, pcSearchText);
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pChart->eaSubCharts, DynAnimSubChartRef, pSubChart) {
		count += dynAnimChartSubChartRefGetSearchStringCount(pSubChart, pcSearchText);
	} FOR_EACH_END;

	if (pChart->bIsSubChart	&& strstri("IsSubChart", pcSearchText))
		count++;

	return count;
}

#include "dynAnimChart_h_ast.c"