#include "dynMoveTransition.h"

#include "dynAnimChart.h"
#include "dynAnimGraph.h"
#include "dynSeqData.h"
#include "dynSkeletonMovement.h"
#include "error.h"
#include "fileutil.h"
#include "ResourceManager.h"
#include "StringCache.h"

#include "dynMoveTransition_h_ast.h"

#define DEBUG_DYNMOVETRANSITION 0

static int bLoadedOnce = false;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

DictionaryHandle hMoveTransitionDict;

const char *pcStopped;

static void dynMoveTransitionRefDictCallback(enumResourceEventType eType, const char *pDictName, const char *pRefData, DynMoveTransition* pMoveTransition, void *pUserData)
{
	if (bLoadedOnce) {
		danimForceDataReload();
		// we should fix up the sorted stances here if priorities changed
		// but we can't mess with shared memory
	}
}

AUTO_RUN;
void dynMoveTransition_registerCommonStrings(void)
{
	pcStopped = allocAddStaticString("Stopped");
}

AUTO_FIXUPFUNC;
TextParserResult fixupDynMoveTransition(DynMoveTransition* pMoveTransition, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	xcase FIXUPTYPE_POST_TEXT_READ:
	{
		if (!dynMoveTransitionVerify(pMoveTransition))
			return PARSERESULT_ERROR;
	}
	xcase FIXUPTYPE_PRE_TEXT_WRITE:
	case FIXUPTYPE_PRE_BIN_WRITE:
		{
			if (!dynMoveTransitionVerify(pMoveTransition))
				return PARSERESULT_ERROR;
		}
	}
	return PARSERESULT_SUCCESS;
}

static bool dynMoveTransitionCheckForGraphBlend(const char **eaDirectionMovementStances,
												const char **eaInterruptingMovementStances,
												const char **eaMovementTypes,
												const char **eaStances,
												DynAnimTimedStance **eaTimedStances)
{
	bool bFail = false;

	if (eaSize(&eaMovementTypes) == 1 &&
		eaMovementTypes[0] == pcStopped)
	{
		FOR_EACH_IN_EARRAY(eaStances, const char, pcStance)
		{
			if (eaFindString(&eaDirectionMovementStances, pcStance) >= 0 ||
				eaFindString(&eaInterruptingMovementStances, pcStance) >= 0)
			{
				bFail = true;
				break;
			}
		}
		FOR_EACH_END;

		if (!bFail)
		{
			FOR_EACH_IN_EARRAY(eaTimedStances, DynAnimTimedStance, pTimedStance)
			{
				if (eaFindString(&eaDirectionMovementStances, pTimedStance->pcName) >= 0 ||
					eaFindString(&eaInterruptingMovementStances, pTimedStance->pcName) >= 0)
				{
					bFail = true;
					break;
				}
			}
			FOR_EACH_END;
		}
	}
	else
	{
		bFail = true;
	}
	
	return !bFail;
}

static void dynMoveTransitionSetGraphBlendFlags(DynMoveTransition *pMT, DynMovementSet *pSet)
{
	bool bBlendFromGraph = false;
	bool bBlendToGraph = false;

	if (pSet)
	{
		bBlendFromGraph = dynMoveTransitionCheckForGraphBlend(	pSet->eaDirectionMovementStances,
																pSet->eaInterruptingMovementStances,
																pMT->eaMovementTypesSource,
																pMT->eaStanceWordsSource,
																pMT->eaTimedStancesSource);

		bBlendToGraph = dynMoveTransitionCheckForGraphBlend(pSet->eaDirectionMovementStances,
															pSet->eaInterruptingMovementStances,
															pMT->eaMovementTypesTarget,
															pMT->eaStanceWordsTarget,
															pMT->eaTimedStancesTarget);
	}

	pMT->bBlendLowerBodyFromGraph = bBlendFromGraph;
	pMT->bBlendWholeBodyFromGraph = bBlendFromGraph;
	pMT->bBlendLowerBodyToGraph = bBlendToGraph;
	pMT->bBlendWholeBodyToGraph = bBlendToGraph;
}

static bool dynMoveTransitionVerifySortedStances(DynMoveTransition *pMoveTransition)
{
	bool bRet = true;

	FOR_EACH_IN_EARRAY(pMoveTransition->eaStanceWordsSource, const char, pcStance)
	{
		if (eaFind(&pMoveTransition->eaAllStanceWordsSorted, pcStance) < 0)
		{
			if (DEBUG_DYNMOVETRANSITION) printfColor(COLOR_RED, "Failed to find source %s\n", pcStance);
			bRet = false;
		}
		
		if (0 <= eaFind(&pMoveTransition->eaStanceWordsTarget, pcStance) &&
			eaFind(&pMoveTransition->eaJointStanceWordsSorted, pcStance) < 0)
		{
			if (DEBUG_DYNMOVETRANSITION) printfColor(COLOR_RED, "Failed to find joint %s\n", pcStance);
			bRet = false;
		}
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pMoveTransition->eaStanceWordsTarget, const char, pcStance)
	{
		if (eaFind(&pMoveTransition->eaAllStanceWordsSorted, pcStance) < 0)
		{
			if (DEBUG_DYNMOVETRANSITION) printfColor(COLOR_RED, "Failed to find target %s\n", pcStance);
			bRet = false;
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pMoveTransition->eaTimedStancesSource, DynAnimTimedStance, pTimedStance)
	{
		bool bFoundIt = false;
		FOR_EACH_IN_EARRAY(pMoveTransition->eaAllTimedStancesSorted, DynAnimTimedStance, pSortedTimedStance) {
			if (pTimedStance->pcName == pSortedTimedStance->pcName &&
				pTimedStance->fTime  == pSortedTimedStance->fTime)
			{
				bFoundIt = true;
				break;
			}
		} FOR_EACH_END;
		if (!bFoundIt) {
			if (DEBUG_DYNMOVETRANSITION) printfColor(COLOR_RED, "Failed to find timed source %s\n", pTimedStance->pcName);
			bRet = false;
		}

		if (0 <= eaFind(&pMoveTransition->eaStanceWordsTarget, pTimedStance->pcName) &&
			eaFind(&pMoveTransition->eaJointStanceWordsSorted, pTimedStance->pcName) < 0)
		{
			if (DEBUG_DYNMOVETRANSITION) printfColor(COLOR_RED, "Failed to find joint %s\n", pTimedStance->pcName);
			bRet = false;
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pMoveTransition->eaTimedStancesTarget, DynAnimTimedStance, pTimedStance)
	{
		bool bFoundIt = false;
		FOR_EACH_IN_EARRAY(pMoveTransition->eaAllTimedStancesSorted, DynAnimTimedStance, pSortedTimedStance) {
			if (pTimedStance->pcName == pSortedTimedStance->pcName &&
				pTimedStance->fTime  == pSortedTimedStance->fTime)
			{
				bFoundIt = true;
				break;
			}
		} FOR_EACH_END;
		if (!bFoundIt) {
			if (DEBUG_DYNMOVETRANSITION) printfColor(COLOR_RED, "Failed to find timed target %s\n", pTimedStance->pcName);
			bRet = false;
		}

		if (0 <= eaFind(&pMoveTransition->eaStanceWordsSource, pTimedStance->pcName) &&
			eaFind(&pMoveTransition->eaJointStanceWordsSorted, pTimedStance->pcName) < 0)
		{
			if (DEBUG_DYNMOVETRANSITION) printfColor(COLOR_RED, "Failed to find joint %s\n", pTimedStance->pcName);
			bRet = false;
		}

		FOR_EACH_IN_EARRAY(pMoveTransition->eaTimedStancesSource, DynAnimTimedStance, pCompareTimedStance) {
			if (pCompareTimedStance->pcName == pTimedStance->pcName &&
				eaFind(&pMoveTransition->eaJointStanceWordsSorted, pTimedStance->pcName) < 0)
			{
				if (DEBUG_DYNMOVETRANSITION) printfColor(COLOR_RED, "Failed to find joint %s\n", pTimedStance->pcName);
				bRet = false;
			}
		} FOR_EACH_END;
	}
	FOR_EACH_END;

	return bRet;
}

static bool dynMoveTransitionVerifyChart(DynMoveTransition *pMT, DynAnimChartLoadTime *pChart)
{
	DynMovementSet *pMovementSet;
	bool bRet = true;

	if (pChart->bIsSubChart) {
		AnimFileError(pMT->pcFilename, "Referenced chart is a sub-chart, change it to a base chart");
		bRet = false;
	}
	else if (REF_HANDLE_IS_ACTIVE(pChart->hBaseChart)) {
		AnimFileError(pMT->pcFilename, "Referenced chart is a stance chart, change it to a base chart");
		bRet = false;
	}

	if (pMovementSet = GET_REF(pChart->hMovementSet)) {
		const char **eaMoveTypes = NULL;
		FOR_EACH_IN_EARRAY(pMovementSet->eaMovementSequences, DynMovementSequence, pMovementSequence)
		{
			eaPush(&eaMoveTypes, pMovementSequence->pcMovementType);
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pMT->eaMovementTypesSource, const char, pcMoveTypeSrc)
		{
			if (eaFind(&eaMoveTypes, pcMoveTypeSrc) < 0) {
				AnimFileError(pMT->pcFilename, "Found invalid source movement type %s", pcMoveTypeSrc);
				bRet = false;
			}
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pMT->eaMovementTypesTarget, const char, pcMoveTypeTgt)
		{
			if (eaFind(&eaMoveTypes, pcMoveTypeTgt) < 0) {
				AnimFileError(pMT->pcFilename, "Found invalid target movement type %s", pcMoveTypeTgt);
				bRet = false;
			}
		}
		FOR_EACH_END
			eaDestroy(&eaMoveTypes);
	}

	return bRet;
}

bool dynMoveTransitionVerify(DynMoveTransition* pMoveTransition)
{
	bool bRet = true;

	//verify file name & path data
	if(!resIsValidName(pMoveTransition->pcName))
	{
		AnimFileError(pMoveTransition->pcFilename, "Move transition name \"%s\" is illegal.", pMoveTransition->pcName);
		bRet = false;
	}
	if(!resIsValidScope(pMoveTransition->pcScope))
	{
		AnimFileError(pMoveTransition->pcFilename, "Template scope \"%s\" is illegal.", pMoveTransition->pcScope);
		bRet = false;
	}
	{
		const char* pcFileName = pMoveTransition->pcFilename;
		if (resFixPooledFilename(&pcFileName, "dyn/movetransition", pMoveTransition->pcScope, pMoveTransition->pcName, "movetrans"))
		{
			if (IsServer())
			{
				AnimFileError(pMoveTransition->pcFilename, "Move transition filename does not match name '%s' scope '%s'", pMoveTransition->pcName, pMoveTransition->pcScope);
				bRet = false;
			}
		}
	}

	//verify the link
	if (!REF_HANDLE_IS_ACTIVE(pMoveTransition->hChart)) {
		AnimFileError(pMoveTransition->pcFilename, "Missing chart reference");
		bRet = false;
	}
	else if (GET_REF(pMoveTransition->hChart))
	{
		//check the chart if it's already loaded, won't happen on startup here, but also called from chart's post-binning & reload to catch at other correct moments
		bRet = dynMoveTransitionVerifyChart(pMoveTransition, GET_REF(pMoveTransition->hChart));
	}

	//verify source movement types
	FOR_EACH_IN_EARRAY(pMoveTransition->eaMovementTypesSource, const char, pSourceMovementType)
	{
		int i;
		if (!pSourceMovementType || strlen(pSourceMovementType) == 0)
		{
			AnimFileError(pMoveTransition->pcFilename, "Found an empty source movement type");
			bRet = false;
			continue;
		}
		for (i=0; i < ipSourceMovementTypeIndex; i++)
		{
			if (pSourceMovementType == pMoveTransition->eaMovementTypesSource[i])
			{
				AnimFileError(pMoveTransition->pcFilename, "Found duplicate source movement type '%s'", pSourceMovementType);
				bRet = false;
			}
		}
	}
	FOR_EACH_END;

	//verify source stances
	FOR_EACH_IN_EARRAY(pMoveTransition->eaStanceWordsSource, const char, pSourceStance)
	{
		int i;
		if (!pSourceStance || strlen(pSourceStance) == 0)
		{
			AnimFileError(pMoveTransition->pcFilename, "Found an empty source stance");
			bRet = false;
			continue;
		}
		if (!dynAnimStanceValid(pSourceStance))
		{
			AnimFileError(pMoveTransition->pcFilename, "Found invalid source stance '%s'", pSourceStance);
			bRet = false;
		}
		for (i=0; i < ipSourceStanceIndex; i++)
		{
			if (pSourceStance == pMoveTransition->eaStanceWordsSource[i])
			{
				AnimFileError(pMoveTransition->pcFilename, "Found duplicate source stance '%s'", pSourceStance);
				bRet = false;
			}
		}
		for (i=eaSize(&pMoveTransition->eaTimedStancesSource)-1; i >= 0; i--)
		{
			if (pSourceStance == pMoveTransition->eaTimedStancesSource[i]->pcName)
			{
				AnimFileError(pMoveTransition->pcFilename, "Found duplicate source stance '%s'", pSourceStance);
				bRet = false;
			}
		}
	}
	FOR_EACH_END;

	//verify timed source stances
	FOR_EACH_IN_EARRAY(pMoveTransition->eaTimedStancesSource, const DynAnimTimedStance, pSourceStance)
	{
		int i;
		if (!pSourceStance || strlen(pSourceStance->pcName) == 0)
		{
			AnimFileError(pMoveTransition->pcFilename, "Found an empty source stance");
			bRet = false;
			continue;
		}
		if (!dynAnimStanceValid(pSourceStance->pcName))
		{
			AnimFileError(pMoveTransition->pcFilename, "Found invalid source stance '%s'", pSourceStance->pcName);
			bRet = false;
		}
		if (pSourceStance->fTime < 0.0f)
		{
			AnimFileError(pMoveTransition->pcFilename, "Found negative source stance time requirement for stance %s", pSourceStance->pcName);
			bRet = false;
		}
		for (i=0; i < ipSourceStanceIndex; i++)
		{
			if (pSourceStance->pcName == pMoveTransition->eaTimedStancesSource[i]->pcName)
			{
				AnimFileError(pMoveTransition->pcFilename, "Found duplicate source stance '%s'", pSourceStance->pcName);
				bRet = false;
			}
		}
	}
	FOR_EACH_END;

	//verify target movement types
	FOR_EACH_IN_EARRAY(pMoveTransition->eaMovementTypesTarget, const char, pTargetMovementType)
	{
		int i;
		if (!pTargetMovementType || strlen(pTargetMovementType) == 0)
		{
			AnimFileError(pMoveTransition->pcFilename, "Found an empty target movement type");
			bRet = false;
			continue;
		}
		for (i=0; i < ipTargetMovementTypeIndex; i++)
		{
			if (pTargetMovementType == pMoveTransition->eaMovementTypesTarget[i])
			{
				AnimFileError(pMoveTransition->pcFilename, "Found duplicate target movement type '%s'", pTargetMovementType);
				bRet = false;
			}
		}
	}
	FOR_EACH_END;

	//verify target stances
	FOR_EACH_IN_EARRAY(pMoveTransition->eaStanceWordsTarget, const char, pTargetStance)
	{
		int i;
		if (!pTargetStance || strlen(pTargetStance) == 0)
		{
			AnimFileError(pMoveTransition->pcFilename, "Found an empty target stance word");
			bRet = false;
			continue;
		}
		if (!dynAnimStanceValid(pTargetStance))
		{
			AnimFileError(pMoveTransition->pcFilename, "Found invalid target stance '%s'", pTargetStance);
			bRet = false;
		}
		for (i=0; i < ipTargetStanceIndex; i++)
		{
			if (pTargetStance == pMoveTransition->eaStanceWordsTarget[i])
			{
				AnimFileError(pMoveTransition->pcFilename, "Found duplicate target stance '%s'", pTargetStance);
				bRet = false;
			}
		}
		for (i=eaSize(&pMoveTransition->eaTimedStancesTarget)-1; i >= 0; i--)
		{
			if (pTargetStance == pMoveTransition->eaTimedStancesTarget[i]->pcName)
			{
				AnimFileError(pMoveTransition->pcFilename, "Found duplicate target stance '%s'", pTargetStance);
			}
		}
	}
	FOR_EACH_END;

	//verify timed target stances
	FOR_EACH_IN_EARRAY(pMoveTransition->eaTimedStancesTarget, const DynAnimTimedStance, pTargetStance)
	{
		int i;
		if (!pTargetStance || strlen(pTargetStance->pcName) == 0)
		{
			AnimFileError(pMoveTransition->pcFilename, "Found an empty target stance word");
			bRet = false;
			continue;
		}
		if (!dynAnimStanceValid(pTargetStance->pcName))
		{
			AnimFileError(pMoveTransition->pcFilename, "Found invalid target stance '%s'", pTargetStance->pcName);
			bRet = false;
		}
		if (pTargetStance->fTime < 0.0f)
		{
			AnimFileError(pMoveTransition->pcFilename, "Found negative target stance time requirement for stance %s", pTargetStance->pcName);
			bRet = false;
		}
		for (i=0; i < ipTargetStanceIndex; i++)
		{
			if (pTargetStance->pcName == pMoveTransition->eaTimedStancesTarget[i]->pcName)
			{
				AnimFileError(pMoveTransition->pcFilename, "Found duplicate target stance '%s'", pTargetStance->pcName);
				bRet = false;
			}
		}
	}
	FOR_EACH_END;

	//verify move
	if (!IS_HANDLE_ACTIVE(pMoveTransition->hMove))
	{
		AnimFileError(pMoveTransition->pcFilename, "Missing move reference");
		bRet = false;
	}
	else if (!GET_REF(pMoveTransition->hMove))
	{
		AnimFileError(pMoveTransition->pcFilename, "Found invalid move '%s'", REF_HANDLE_GET_STRING(pMoveTransition->hMove));
		bRet = false;
	}

	return bRet;
}

static void dynMoveTransitionFixup(DynMoveTransition *pMoveTransition);

static int dynMoveTransitionResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, DynMoveTransition* pMoveTransition, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_FIX_FILENAME:
		{
			resFixPooledFilename((char**)&pMoveTransition->pcFilename, "dyn/movetransition", pMoveTransition->pcScope, pMoveTransition->pcName, "movetrans");
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_POST_BINNING:
		{
			DynAnimChartLoadTime *pChart = GET_REF(pMoveTransition->hChart);
			DynMovementSet *pSet = pChart ? GET_REF(pChart->hMovementSet) : NULL;
			dynMoveTransitionFixup(pMoveTransition);
			dynMoveTransitionSetGraphBlendFlags(pMoveTransition, pSet);
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_CHECK_REFERENCES:
		{
			DynAnimChartLoadTime *pChart = GET_REF(pMoveTransition->hChart);
			dynMoveTransitionVerifyChart(pMoveTransition, pChart);
			if (!dynMoveTransitionVerifySortedStances(pMoveTransition)) {
				AnimFileError(pMoveTransition->pcFilename, "Sorted stances invalid after moving to shared memory\n");
			}
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void registerMoveTransitionDictionaries(void)
{
	hMoveTransitionDict = RefSystem_RegisterSelfDefiningDictionary(MOVE_TRANSITION_EDITED_DICTIONARY, false, parse_DynMoveTransition, true, true, NULL);

	resDictManageValidation(hMoveTransitionDict, dynMoveTransitionResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(hMoveTransitionDict);
		if (isDevelopmentMode() || isProductionEditMode())
		{
			resDictMaintainInfoIndex(hMoveTransitionDict, ".name", ".scope", NULL, NULL, NULL);
		}
	}
	else if (IsClient())
	{
		resDictRequestMissingResources(hMoveTransitionDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}
	//resDictProvideMissingRequiresEditMode(hMoveTransitionDict);
	resDictRegisterEventCallback(hMoveTransitionDict, dynMoveTransitionRefDictCallback, NULL);
}

void dynMoveTransitionLoadAll(void)
{
	if (!bLoadedOnce)
	{
		if (IsServer())
		{
			resLoadResourcesFromDisk(hMoveTransitionDict, "dyn/movetransition", ".movetrans", NULL, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
		}
		else if (IsClient())
		{
			loadstart_printf("Loading DynMoveTransitions...");
			ParserLoadFilesToDictionary("dyn/movetransition", ".movetrans", "DynMoveTransition.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, hMoveTransitionDict);
			loadend_printf(" done (%d DynMoveTransitions)", RefSystem_GetDictionaryNumberOfReferents(hMoveTransitionDict));
		}
		bLoadedOnce = true;
	}
}

static void dynMoveTransitionFixup(DynMoveTransition *pMoveTransition)
{
	FOR_EACH_IN_EARRAY(pMoveTransition->eaStanceWordsSource, const char, pcStance)
	{
		eaPushUnique(&pMoveTransition->eaAllStanceWordsSorted, pcStance);

		if (0 <= eaFind(&pMoveTransition->eaStanceWordsTarget, pcStance)) {
			eaPushUnique(&pMoveTransition->eaJointStanceWordsSorted, pcStance); // on source & target
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pMoveTransition->eaStanceWordsTarget, const char, pcStance)
	{
		eaPushUnique(&pMoveTransition->eaAllStanceWordsSorted, pcStance);
	}
	FOR_EACH_END;
	
	FOR_EACH_IN_EARRAY(pMoveTransition->eaTimedStancesSource, DynAnimTimedStance, pTimedStance)
	{
		DynAnimTimedStance *pNewTimedStance = StructClone(parse_DynAnimTimedStance,pTimedStance);
		eaPushUnique(&pMoveTransition->eaAllTimedStancesSorted, pNewTimedStance);

		if (0 <= eaFind(&pMoveTransition->eaStanceWordsTarget, pTimedStance->pcName)) {
			eaPushUnique(&pMoveTransition->eaJointStanceWordsSorted, pTimedStance->pcName); // on timed source & target
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pMoveTransition->eaTimedStancesTarget, DynAnimTimedStance, pTimedStance)
	{
		DynAnimTimedStance *pNewTimedStance = StructClone(parse_DynAnimTimedStance,pTimedStance);
		eaPushUnique(&pMoveTransition->eaAllTimedStancesSorted, pNewTimedStance);

		if (0 <= eaFind(&pMoveTransition->eaStanceWordsSource, pTimedStance->pcName)) {
			eaPushUnique(&pMoveTransition->eaJointStanceWordsSorted, pTimedStance->pcName); // on source & timed target
		}
		else {
			FOR_EACH_IN_EARRAY(pMoveTransition->eaTimedStancesSource, DynAnimTimedStance, pCompareTimedStance) {
				if (pTimedStance->pcName == pCompareTimedStance->pcName) {
					eaPushUnique(&pMoveTransition->eaJointStanceWordsSorted, pTimedStance->pcName); // on timed source & timed target
					break;
				}
			} FOR_EACH_END;
		}
	}
	FOR_EACH_END;

	eaQSort(pMoveTransition->eaAllStanceWordsSorted,   dynAnimCompareStanceWordPriority);
	eaQSort(pMoveTransition->eaJointStanceWordsSorted, dynAnimCompareStanceWordPriority);
	eaQSort(pMoveTransition->eaAllTimedStancesSorted,  dynAnimCompareTimedStancesPriority);
}

int dynMoveTransitionGetSearchStringCount(const DynMoveTransition *pMoveTransition, const char *pcSearchText)
{
	int count = 0;

	
	if (pMoveTransition->pcName		&& strstri(pMoveTransition->pcName,		pcSearchText))	count++;
	if (pMoveTransition->pcFilename	&& strstri(pMoveTransition->pcFilename,	pcSearchText))	count++;
	if (pMoveTransition->pcComments	&& strstri(pMoveTransition->pcComments,	pcSearchText))	count++;
	if (pMoveTransition->pcScope	&& strstri(pMoveTransition->pcScope,	pcSearchText))	count++;

	if (REF_HANDLE_IS_ACTIVE(pMoveTransition->hChart)	&& strstri(REF_HANDLE_GET_STRING(pMoveTransition->hChart),	pcSearchText))	count++;

	if (pMoveTransition->bForced	&& strstri("Forced",	pcSearchText)) count++;

	if (pMoveTransition->bBlendLowerBodyFromGraph	&& strstri("BlendLowerBodyFromGraph",	pcSearchText))	count++;
	if (pMoveTransition->bBlendWholeBodyFromGraph	&& strstri("BlendWholeBodyFromGraph",	pcSearchText))	count++;
	if (pMoveTransition->bBlendWholeBodyToGraph		&& strstri("BlendWholeBodyToGraph",		pcSearchText))	count++;
	if (pMoveTransition->bBlendLowerBodyToGraph		&& strstri("BlendLowerBodyToGraph",		pcSearchText))	count++;

	FOR_EACH_IN_EARRAY(pMoveTransition->eaStanceWordsSource, const char, pcStanceWord) {
		if (pcStanceWord && strstri(pcStanceWord, pcSearchText)) count++;
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pMoveTransition->eaMovementTypesSource, const char, pcMovementType) {
		if (pcMovementType && strstri(pcMovementType, pcSearchText)) count++;
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pMoveTransition->eaStanceWordsTarget, const char, pcStanceWord) {
		if (pcStanceWord && strstri(pcStanceWord, pcSearchText)) count++;
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pMoveTransition->eaMovementTypesTarget, const char, pcMovementType) {
		if (pcMovementType && strstri(pcMovementType, pcSearchText)) count++;
	} FOR_EACH_END;

	//sorted stance words ignored

	if (REF_HANDLE_IS_ACTIVE(pMoveTransition->hMove)	&& strstri(REF_HANDLE_GET_STRING(pMoveTransition->hMove),	pcSearchText))	count++;

	return count;
}

#include "dynMoveTransition_h_ast.c"
