/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "UGCAchievements.h"

#include "MemoryPool.h"
#include "ResourceManager.h"
#include "file.h"
#include "error.h"

#include "Entity.h"
#include "Player.h"

#include "AutoGen/UGCAchievements_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_UGCAchievementDictionary = NULL;

static void ugcAchievement_CreateRefString(UGCAchievementDef *pDef, UGCAchievementDef *pParentDef)
{
	static char *estrBuffer = NULL;

	if(pDef->name)
	{
		estrClear(&estrBuffer);
		if(!pParentDef)
			estrCopy2(&estrBuffer, pDef->name);
		else
			estrPrintf(&estrBuffer, "%s::%s", pParentDef->pchRefString, pDef->name);
		pDef->pchRefString = allocAddString(estrBuffer);
	}
}

static void ugcAchievement_CreateRefStringsRecursive(UGCAchievementDef *pDef, UGCAchievementDef *pParentDef)
{
	int i, n = eaSize(&pDef->subAchievements);

	ugcAchievement_CreateRefString(pDef, pParentDef);

	for (i = 0; i < n; ++i)
		ugcAchievement_CreateRefStringsRecursive(pDef->subAchievements[i], pDef);
}

static void ugcAchievement_SetParent(UGCAchievementDef *pDef, UGCAchievementDef *pParentDef)
{
	pDef->pParentDef = pParentDef;
}

static void ugcAchievement_SetParentRecursive(UGCAchievementDef *pDef, UGCAchievementDef *pParentDef)
{
	int i, n = eaSize(&pDef->subAchievements);

	ugcAchievement_SetParent(pDef, pParentDef);

	for (i = 0; i < n; ++i)
		ugcAchievement_SetParentRecursive(pDef->subAchievements[i], pDef);
}

static UGCAchievementDef* ugcAchievement_ChildDefFromName(UGCAchievementDef *pUGCAchievementDef, const char *pcRefString)
{
	if(pUGCAchievementDef && pcRefString)
	{
		static char *parentName = NULL;
		char *pcChildName = NULL;
		int i, n = eaSize(&pUGCAchievementDef->subAchievements);

		estrCopy2(&parentName, pcRefString);
		pcChildName = strstr(parentName, "::");
		if(pcChildName)
		{
			*pcChildName = 0;
			pcChildName += 2;
		}

		for(i = 0; i < n; i++)
			if(0 == stricmp(pUGCAchievementDef->subAchievements[i]->name, parentName))
			{
				if(pcChildName)
					return ugcAchievement_ChildDefFromName(pUGCAchievementDef->subAchievements[i], pcChildName);
				else
					return pUGCAchievementDef->subAchievements[i];
			}
	}
	return NULL;
}

UGCAchievementDef* ugcAchievement_DefFromRefString(const char *pcRefString)
{
	UGCAchievementDef *pDef = NULL;
	static char *parentName = NULL;
	char *pcChildName = NULL;

	estrCopy2(&parentName, pcRefString);
	pcChildName = strstr(parentName, "::");
	if(pcChildName)
	{
		*pcChildName = 0;
		pcChildName += 2;
	}

	pDef = (UGCAchievementDef*)RefSystem_ReferentFromString(g_UGCAchievementDictionary, parentName);
	if(pDef && pcChildName)
		pDef = ugcAchievement_ChildDefFromName(pDef, pcChildName);

	return pDef;
}

static bool ugcAchievementDef_Validate(UGCAchievementDef *pUGCAchievementDef, UGCAchievementDef *pRootUGCAchievementDef, bool bIsChild, bool bIsHidden)
{
	bool bResult = true;

	if(bIsChild || 0 != stricmp(pUGCAchievementDef->scope, "none")) // if scope is none for a root achievement, skip all validation
	{
		if(!resIsValidName(pUGCAchievementDef->name))
		{
			ErrorFilenamef(pUGCAchievementDef->filename, "Achievement name is illegal: '%s'", pUGCAchievementDef->name );
			bResult = false;
		}

		if(!bIsChild && !resIsValidScope(pUGCAchievementDef->scope))
		{
			ErrorFilenamef(pUGCAchievementDef->filename, "Achievement scope is illegal: '%s'", pUGCAchievementDef->scope );
			bResult = false;
		}
		else if(bIsChild && pUGCAchievementDef->scope)
		{
			ErrorFilenamef(pUGCAchievementDef->filename, "Child Achievement '%s' should not have a scope", pUGCAchievementDef->name );
			bResult = false;
		}

		if(!bIsChild)
		{
			const char *pcTempFileName = pUGCAchievementDef->filename;
			if(resFixPooledFilename(&pcTempFileName, "defs/UGCAchievements", pUGCAchievementDef->scope, pUGCAchievementDef->name, "ugcachievement"))
			{
				if(IsServer() && stricmp(pUGCAchievementDef->scope, "none") != 0)
				{
					char nameSpace[RESOURCE_NAME_MAX_SIZE];
					char baseObjectName[RESOURCE_NAME_MAX_SIZE];
					char baseObjectName2[RESOURCE_NAME_MAX_SIZE];
					if (!resExtractNameSpace(pcTempFileName, nameSpace, baseObjectName) || !resExtractNameSpace(pUGCAchievementDef->filename, nameSpace, baseObjectName2)
							|| stricmp(baseObjectName, baseObjectName2) != 0)
					{
						ErrorFilenamef(pUGCAchievementDef->filename, "Achievement filename does not match name '%s' scope '%s'", pUGCAchievementDef->name, pUGCAchievementDef->scope);
						bResult = false;
					}
				}
			}
		}

		// Validate Hidden - this is a temporary validation. We may, in the future, support non-Hidden UGC Achievements. The current design has the visible UGC Achievements implemented as Perks.
		if(!pUGCAchievementDef->bHidden)
		{
			ErrorFilenamef(pUGCAchievementDef->filename, "Achievement '%s' is not flagged as Hidden. UGC Achievements must be Hidden, for now", pUGCAchievementDef->name);
			bResult = false;
		}

		// Validate messages
		if(!bIsHidden && !pUGCAchievementDef->bHidden)
		{
			if(IsGameServerSpecificallly_NotRelatedTypes() && !GET_REF(pUGCAchievementDef->nameMsg.hMessage) && REF_STRING_FROM_HANDLE(pUGCAchievementDef->nameMsg.hMessage))
			{
				ErrorFilenamef(pUGCAchievementDef->filename, "Achievement refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pUGCAchievementDef->nameMsg.hMessage));
				bResult = false;
			}
			if(IsGameServerSpecificallly_NotRelatedTypes() && !GET_REF(pUGCAchievementDef->descriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(pUGCAchievementDef->descriptionMsg.hMessage))
			{
				ErrorFilenamef(pUGCAchievementDef->filename, "Achievement refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pUGCAchievementDef->descriptionMsg.hMessage));
				bResult = false;
			}
			if(IsGameServerSpecificallly_NotRelatedTypes() && !GET_REF(pUGCAchievementDef->grantedNotificationMsg.hMessage) && REF_STRING_FROM_HANDLE(pUGCAchievementDef->grantedNotificationMsg.hMessage))
			{
				ErrorFilenamef(pUGCAchievementDef->filename, "Achievement refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pUGCAchievementDef->grantedNotificationMsg.hMessage));
				bResult = false;
			}
		}

		// Validate target - this is a temporary validation. We may, in the future, use Targets and Granting. The current design strongly desires Targets of 0, so that UGC Achievements count forever.
		if(pUGCAchievementDef->uTarget > 0)
		{
			ErrorFilenamef(pUGCAchievementDef->filename, "UGC Achievement '%s' has a positive Target (%d). Target must zero, for now.", pUGCAchievementDef->name, pUGCAchievementDef->uTarget);
			bResult = false;
		}

		// Validate OrderedCounting - this is a temporary validation. We may, in the future, use Targets and Granting. The current design strongly desires a flat, leafy achievements structure.
		if(pUGCAchievementDef->bOrderedCounting)
		{
			ErrorFilenamef(pUGCAchievementDef->filename, "UGC Achievement '%s' marked as OrderedCounting. OrderedCounting parents are not allowed, for now.", pUGCAchievementDef->name);
			bResult = false;

			if(0 == eaSize(&pUGCAchievementDef->subAchievements))
			{
				ErrorFilenamef(pUGCAchievementDef->filename, "UGC Achievement '%s' is marked as OrderedCounting, yet has no child Achievements. OrderedCounting UGC Achievements must have child achievements.", pUGCAchievementDef->name);
				bResult = false;
			}
		}

		// Validate consecutives
		if(pUGCAchievementDef->uConsecutiveHours)
		{
			if(eaSize(&pUGCAchievementDef->subAchievements))
			{
				ErrorFilenamef(pUGCAchievementDef->filename, "UGC Achievement '%s' is marked as a consecutive time block activity, yet has child Achievements. Consecutive UGC Achievements must be leafy.", pUGCAchievementDef->name);
				bResult = false;
			}
		}

		// Validate repeatables - this is a temporary validation. We may, in the future, use Targets and Granting. The current design strongly desires no repeatables, so that UGC Achievements count forever.
		if(pUGCAchievementDef->bRepeatable)
		{
			ErrorFilenamef(pUGCAchievementDef->filename, "UGC Achievement '%s' is marked as a repeatable. Repeatables are not allowed, for now.", pUGCAchievementDef->name);
			bResult = false;

			if(eaSize(&pUGCAchievementDef->subAchievements))
			{
				ErrorFilenamef(pUGCAchievementDef->filename, "UGC Achievement '%s' is marked as repeatable, yet has child Achievements. Repeatable UGC Achievements must be leafy.", pUGCAchievementDef->name);
				bResult = false;
			}

			if(!bIsHidden && !pUGCAchievementDef->bHidden && !pUGCAchievementDef->uRepeatCooldownHours)
			{
				ErrorFilenamef(pUGCAchievementDef->filename, "Non-hidden UGC Achievement '%s' is marked as repeatable, yet RepeatCooldownHours is zero. Please specify a positive RepeatCooldownHours.", pUGCAchievementDef->name);
				bResult = false;
			}
		}

		if(pUGCAchievementDef->ugcAchievementFilter.ugcAchievementServerFilter.ugcAchievementGrantedFilter)
		{
			if(pUGCAchievementDef->ugcAchievementFilter.ugcAchievementServerFilter.ugcAchievementGrantedFilter->pUGCAchievementName)
			{
				if(!ugcAchievement_DefFromRefString(pUGCAchievementDef->ugcAchievementFilter.ugcAchievementServerFilter.ugcAchievementGrantedFilter->pUGCAchievementName))
				{
					ErrorFilenamef(pUGCAchievementDef->filename, "UGC Achievement '%s' is filtering for Achievement Granted events with an undefined, fully-scoped achievement name '%s'.",
						pUGCAchievementDef->name, pUGCAchievementDef->ugcAchievementFilter.ugcAchievementServerFilter.ugcAchievementGrantedFilter->pUGCAchievementName);
					bResult = false;
				}
			}
		}

		// Validate sub achievements - this is a temporary validation. We may, in the future, support sub achievements. The current design strongly desires a flat, leafy achievements structure.
		if(eaSize(&pUGCAchievementDef->subAchievements))
		{
			ErrorFilenamef(pUGCAchievementDef->filename, "UGC Achievement '%s' is marked as OrderedCounting, yet has no child Achievements. OrderedCounting UGC Achievements must have child achievements.", pUGCAchievementDef->name);
			bResult = false;
		}

		// Recurse
		FOR_EACH_IN_EARRAY_FORWARDS(pUGCAchievementDef->subAchievements, UGCAchievementDef, ugcAchivementDef)
			bResult &= ugcAchievementDef_Validate(ugcAchivementDef, pRootUGCAchievementDef, true, bIsHidden || pUGCAchievementDef->bHidden);
		FOR_EACH_END;
	}

	return bResult;
}

static int ugcAchievementDef_ValidateCB(enumResourceValidateType eType, const char *pcDictName, const char *pcResourceName, UGCAchievementDef *pUGCAchievementDef, U32 userID)
{
	switch(eType)
	{
		// Post text reading: do fixup that should happen before binning.  Generate expressions, do most post-processing here
		// also happens after receiving achievements from the resource dB, for basically the same reasons
		// This specifically falls through so that resource db receives are treated similarly to post text reads
		xcase RESVALIDATE_POST_TEXT_READING:
		acase RESVALIDATE_POST_RESDB_RECEIVE:
			ugcAchievement_CreateRefStringsRecursive(pUGCAchievementDef, NULL);
			return VALIDATE_HANDLED;

		// Post binning: gets run each time achievement is read from bin. Populate NO_AST fields here
		xcase RESVALIDATE_POST_BINNING:
		// Final location: after moving to shared memory.  Fix up pointers here
		xcase RESVALIDATE_FINAL_LOCATION:
		xcase RESVALIDATE_CHECK_REFERENCES:
			ugcAchievementDef_Validate(pUGCAchievementDef, pUGCAchievementDef, /*bIsChild=*/false, /*bIsHidden=*/false);
			ugcAchievement_SetParentRecursive(pUGCAchievementDef, NULL);
			return VALIDATE_HANDLED;

		// Fix filename: called during saving.
		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename(&pUGCAchievementDef->filename, "defs/UGCAchievements", pUGCAchievementDef->scope, pUGCAchievementDef->name, "ugcachievement");
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static void ugcAchievement_ResAddRecurse(Referent pReferent)
{
	UGCAchievementDef *pDef = (UGCAchievementDef*)pReferent;
	if(0 != stricmp("none", pDef->scope))
	{
		int n = eaSize(&pDef->subAchievements);
		int i;

		resUpdateInfo(ALL_UGC_ACHIEVEMENTS_INDEX, pDef->pchRefString, parse_UGCAchievementDef, pReferent, ".name", ".scope", NULL, NULL, NULL, false, false);

		for(i = 0; i < n; i++)
			ugcAchievement_ResAddRecurse(pDef->subAchievements[i]);
	}
}

static void ugcAchievement_ResRemoveRecurse(Referent pReferent)
{
	UGCAchievementDef *pDef = (UGCAchievementDef*)pReferent;
	int n = eaSize(&pDef->subAchievements);
	int i;

	resUpdateInfo(ALL_UGC_ACHIEVEMENTS_INDEX, pDef->pchRefString, parse_UGCAchievementDef, NULL, ".name", ".scope", NULL, NULL, NULL, false, false);

	for(i = 0; i < n; i++)
		ugcAchievement_ResRemoveRecurse(pDef->subAchievements[i]);
}

static void ugcAchievement_DictionaryChangeCB(enumResourceEventType eType, const char *pcDictName, const char *pcRefData, Referent pReferent, void *pUserData)
{
	UGCAchievementDef *pDef = (UGCAchievementDef*)pReferent;

	if(!isProductionMode() || isProductionEditMode())
	{
		if(eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
			ugcAchievement_ResAddRecurse(pReferent);
		else if(eType == RESEVENT_RESOURCE_REMOVED || eType == RESEVENT_RESOURCE_PRE_MODIFIED)
			ugcAchievement_ResRemoveRecurse(pReferent);
	}
}

AUTO_RUN;
void ugcAchievement_RegisterDictionary(void)
{
	g_UGCAchievementDictionary = RefSystem_RegisterSelfDefiningDictionary("UGCAchievement", false, parse_UGCAchievementDef, true, true, NULL);

	resDictManageValidation(g_UGCAchievementDictionary, ugcAchievementDef_ValidateCB);

	resRegisterIndexOnlyDictionary(ALL_UGC_ACHIEVEMENTS_INDEX, RESCATEGORY_INDEX);

	if(IsServer())
	{
		resDictRegisterEventCallback(g_UGCAchievementDictionary, ugcAchievement_DictionaryChangeCB, NULL);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_UGCAchievementDictionary, ".name", ".scope", NULL, NULL, NULL);
		}

		resDictProvideMissingResources(g_UGCAchievementDictionary);
		resDictProvideMissingResources(ALL_UGC_ACHIEVEMENTS_INDEX);

		resDictGetMissingResourceFromResourceDBIfPossible((void*)g_UGCAchievementDictionary);
	}
	else
	{
		resDictRequestMissingResources(g_UGCAchievementDictionary, 128, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(ALL_UGC_ACHIEVEMENTS_INDEX, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}
}

static void ugcAchievement_LoadDefs()
{
	resLoadResourcesFromDisk(g_UGCAchievementDictionary, "defs/UGCAchievements", ".ugcachievement", NULL, PARSER_OPTIONALFLAG);
}

AUTO_STARTUP(UGCAchievements);
void ugcAchievement_Load(void)
{
	ugcAchievement_LoadDefs();
}

static U32 ugcAchievement_MapCreatedEventFilter(UGCMapCreatedFilter *pUGCMapCreatedFilter, UGCMapCreatedEvent *pUGCMapCreatedEvent)
{
	if(pUGCMapCreatedFilter && pUGCMapCreatedEvent)
	{
		if(pUGCMapCreatedFilter->type == UGC_MAP_TYPE_ANY
			|| pUGCMapCreatedEvent->type == pUGCMapCreatedFilter->type)
			return 1;
	}
	return 0;
}

static U32 ugcAchievement_PlayerReviewerEventFilter(UGCPlayerReviewerFilter *pUGCPlayerReviewerFilter, UGCPlayerReviewerEvent *pUGCPlayerReviewerEvent)
{
	if(pUGCPlayerReviewerFilter && pUGCPlayerReviewerEvent)
	{
		if(pUGCPlayerReviewerEvent->bPlayerIsReviewer == pUGCPlayerReviewerFilter->bPlayerIsReviewer)
			return 1;
	}
	return 0;
}

static U32 ugcAchievement_ProjectPublishedEventFilter(UGCProjectPublishedFilter *pUGCProjectPublishedFilter, UGCProjectPublishedEvent *pUGCProjectPublishedEvent)
{
	if(pUGCProjectPublishedFilter && pUGCProjectPublishedEvent)
	{
		if(pUGCProjectPublishedEvent->uCustomMaps >= pUGCProjectPublishedFilter->uCustomMaps
				&& pUGCProjectPublishedEvent->uDialogs >= pUGCProjectPublishedFilter->uDialogs)
			return 1;
	}
	return 0;
}

static U32 ugcAchievement_SeriesPublishedEventFilter(UGCSeriesPublishedFilter *pUGCSeriesPublishedFilter, UGCSeriesPublishedEvent *pUGCSeriesPublishedEvent)
{
	if(pUGCSeriesPublishedFilter && pUGCSeriesPublishedEvent)
	{
		if(pUGCSeriesPublishedEvent->uProjectCount >= pUGCSeriesPublishedFilter->uProjectCount)
			return 1;
	}
	return 0;
}

static U32 ugcAchievement_ProjectPlayedEventFilter(UGCProjectPlayedFilter *pUGCProjectPlayedFilter, UGCProjectPlayedEvent *pUGCProjectPlayedEvent)
{
	if(pUGCProjectPlayedFilter && pUGCProjectPlayedEvent)
	{
		if(pUGCProjectPlayedEvent->uPlayDuration >= pUGCProjectPlayedFilter->uPlayDuration)
			return 1;
	}
	return 0;
}

static U32 ugcAchievement_PlayedProjectEventFilter(UGCPlayedProjectFilter *pUGCPlayedProjectFilter, UGCPlayedProjectEvent *pUGCPlayedProjectEvent)
{
	if(pUGCPlayedProjectFilter && pUGCPlayedProjectEvent)
	{
		if(pUGCPlayedProjectEvent->uPlayDuration >= pUGCPlayedProjectFilter->uPlayDuration)
			return 1;
	}
	return 0;
}

static U32 ugcAchievement_ProjectReviewedEventFilter(UGCProjectReviewedFilter *pUGCProjectReviewedFilter, UGCProjectReviewedEvent *pUGCProjectReviewedEvent)
{
	if(pUGCProjectReviewedFilter && pUGCProjectReviewedEvent)
	{
		if(pUGCProjectReviewedEvent->fRating >= pUGCProjectReviewedFilter->fRating
				&& pUGCProjectReviewedEvent->fHighestRating < pUGCProjectReviewedEvent->fRating
				&& pUGCProjectReviewedEvent->iTotalReviews >= pUGCProjectReviewedFilter->iTotalReviews
				&& pUGCProjectReviewedEvent->iTotalStars >= pUGCProjectReviewedFilter->iTotalStars
				&& pUGCProjectReviewedEvent->fAverageRating >= pUGCProjectReviewedFilter->fAverageRating
				&& pUGCProjectReviewedEvent->fAdjustedRatingUsingConfidence >= pUGCProjectReviewedFilter->fAdjustedRatingUsingConfidence
				&& pUGCProjectReviewedEvent->bBetaReviewing >= pUGCProjectReviewedFilter->bBetaReviewing)
			return pUGCProjectReviewedFilter->bCountRatingStars
				? ((pUGCProjectReviewedEvent->fRating - pUGCProjectReviewedEvent->fHighestRating) * 5) // if counting stars, give the difference between the rating and the highest they ever gave
				: (pUGCProjectReviewedEvent->fHighestRating == 0.0); // if not counting stars, only give 1 credit if it is the first review
	}
	return 0;
}

static U32 ugcAchievement_ReviewedProjectEventFilter(UGCReviewedProjectFilter *pUGCReviewedProjectFilter, UGCReviewedProjectEvent *pUGCReviewedProjectEvent)
{
	if(pUGCReviewedProjectFilter && pUGCReviewedProjectEvent)
	{
		if(pUGCReviewedProjectEvent->fRating >= pUGCReviewedProjectFilter->fRating
				&& pUGCReviewedProjectEvent->fHighestRating < pUGCReviewedProjectEvent->fRating
				&& pUGCReviewedProjectEvent->iTotalReviews >= pUGCReviewedProjectFilter->iTotalReviews
				&& pUGCReviewedProjectEvent->iTotalStars >= pUGCReviewedProjectFilter->iTotalStars
				&& pUGCReviewedProjectEvent->fAverageRating >= pUGCReviewedProjectFilter->fAverageRating
				&& pUGCReviewedProjectEvent->fAdjustedRatingUsingConfidence >= pUGCReviewedProjectFilter->fAdjustedRatingUsingConfidence
				&& pUGCReviewedProjectEvent->bBetaReviewing >= pUGCReviewedProjectFilter->bBetaReviewing)
			return pUGCReviewedProjectFilter->bCountRatingStars
				? ((pUGCReviewedProjectEvent->fRating - pUGCReviewedProjectEvent->fHighestRating) * 5) // if counting stars, give the difference between the rating and the highest they ever gave
				: (pUGCReviewedProjectEvent->fHighestRating == 0.0); // if not counting stars, only give 1 credit if it is the first review
	}
	return 0;
}

static U32 ugcAchievement_ProjectTippedEventFilter(UGCProjectTippedFilter *pUGCProjectTippedFilter, UGCProjectTippedEvent *pUGCProjectTippedEvent)
{
	if(pUGCProjectTippedFilter && pUGCProjectTippedEvent)
	{
		if(pUGCProjectTippedEvent->uTipAmount >= pUGCProjectTippedFilter->uTipAmount)
			return pUGCProjectTippedFilter->bCountTipAmount ? pUGCProjectTippedEvent->uTipAmount : 1;
	}
	return 0;
}

static U32 ugcAchievement_TippedProjectEventFilter(UGCTippedProjectFilter *pUGCTippedProjectFilter, UGCTippedProjectEvent *pUGCTippedProjectEvent)
{
	if(pUGCTippedProjectFilter && pUGCTippedProjectEvent)
	{
		if(pUGCTippedProjectEvent->uTipAmount >= pUGCTippedProjectFilter->uTipAmount)
			return pUGCTippedProjectFilter->bCountTipAmount ? pUGCTippedProjectEvent->uTipAmount : 1;
	}
	return 0;
}

static U32 ugcAchievement_ProjectFeaturedEventFilter(UGCProjectFeaturedFilter *pUGCProjectFeaturedFilter, UGCProjectFeaturedEvent *pUGCProjectFeaturedEvent)
{
	if(pUGCProjectFeaturedFilter && pUGCProjectFeaturedEvent)
	{
		if(pUGCProjectFeaturedEvent->uFeaturedProjectCurrentCount >= pUGCProjectFeaturedFilter->uFeaturedProjectCurrentCount
				&& pUGCProjectFeaturedEvent->uFeaturedProjectTotalCount >= pUGCProjectFeaturedFilter->uFeaturedProjectTotalCount)
			return 1;
	}
	return 0;
}

static U32 ugcAchievement_AchievementGrantedEventFilter(UGCAchievementGrantedFilter *pUGCAchievementGrantedFilter, UGCAchievementGrantedEvent *pUGCAchievementGrantedEvent)
{
	if(pUGCAchievementGrantedFilter && pUGCAchievementGrantedEvent)
	{
		UGCAchievementDef *pUGCAchievementDefGrantedEvent = ugcAchievement_DefFromRefString(pUGCAchievementGrantedEvent->pUGCAchievementName);
		if(pUGCAchievementDefGrantedEvent)
		{
			if(pUGCAchievementDefGrantedEvent->pchRefString == pUGCAchievementGrantedFilter->pUGCAchievementName)
				return 1;
			if(pUGCAchievementGrantedFilter->type > UGCAchievementGrantedFilterType_Exact)
			{
				pUGCAchievementDefGrantedEvent = pUGCAchievementDefGrantedEvent->pParentDef;
				if(pUGCAchievementDefGrantedEvent)
				{
					if(pUGCAchievementDefGrantedEvent->pchRefString == pUGCAchievementGrantedFilter->pUGCAchievementName)
						return 1;
					if(pUGCAchievementGrantedFilter->type == UGCAchievementGrantedFilterType_AnyDescendant)
					{
						pUGCAchievementDefGrantedEvent = pUGCAchievementDefGrantedEvent->pParentDef;
						while(pUGCAchievementDefGrantedEvent)
						{
							if(pUGCAchievementDefGrantedEvent->pchRefString == pUGCAchievementGrantedFilter->pUGCAchievementName)
								return 1;
							pUGCAchievementDefGrantedEvent = pUGCAchievementDefGrantedEvent->pParentDef;
						}
					}
				}
			}
		}
	}
	return 0;
}

static U32 ugcAchievement_SeriesReviewedEventFilter(UGCSeriesReviewedFilter *pUGCSeriesReviewedFilter, UGCSeriesReviewedEvent *pUGCSeriesReviewedEvent)
{
	if(pUGCSeriesReviewedFilter && pUGCSeriesReviewedEvent)
	{
		if(pUGCSeriesReviewedEvent->fRating >= pUGCSeriesReviewedFilter->fRating
				&& pUGCSeriesReviewedEvent->fHighestRating < pUGCSeriesReviewedEvent->fRating
				&& pUGCSeriesReviewedEvent->iTotalReviews >= pUGCSeriesReviewedFilter->iTotalReviews
				&& pUGCSeriesReviewedEvent->iTotalStars >= pUGCSeriesReviewedFilter->iTotalStars
				&& pUGCSeriesReviewedEvent->fAverageRating >= pUGCSeriesReviewedFilter->fAverageRating
				&& pUGCSeriesReviewedEvent->fAdjustedRatingUsingConfidence >= pUGCSeriesReviewedFilter->fAdjustedRatingUsingConfidence)
			return pUGCSeriesReviewedFilter->bCountRatingStars
				? ((pUGCSeriesReviewedEvent->fRating - pUGCSeriesReviewedEvent->fHighestRating) * 5) // if counting stars, give the difference between the rating and the highest they ever gave
				: (pUGCSeriesReviewedEvent->fHighestRating == 0.0); // if not counting stars, only give 1 credit if it is the first review
	}
	return 0;
}

static U32 ugcAchievement_ReviewedSeriesEventFilter(UGCReviewedSeriesFilter *pUGCReviewedSeriesFilter, UGCReviewedSeriesEvent *pUGCReviewedSeriesEvent)
{
	if(pUGCReviewedSeriesFilter && pUGCReviewedSeriesEvent)
	{
		if(pUGCReviewedSeriesEvent->fRating >= pUGCReviewedSeriesFilter->fRating
				&& pUGCReviewedSeriesEvent->fHighestRating < pUGCReviewedSeriesEvent->fRating
				&& pUGCReviewedSeriesEvent->iTotalReviews >= pUGCReviewedSeriesFilter->iTotalReviews
				&& pUGCReviewedSeriesEvent->iTotalStars >= pUGCReviewedSeriesFilter->iTotalStars
				&& pUGCReviewedSeriesEvent->fAverageRating >= pUGCReviewedSeriesFilter->fAverageRating
				&& pUGCReviewedSeriesEvent->fAdjustedRatingUsingConfidence >= pUGCReviewedSeriesFilter->fAdjustedRatingUsingConfidence)
			return pUGCReviewedSeriesFilter->bCountRatingStars
				? ((pUGCReviewedSeriesEvent->fRating - pUGCReviewedSeriesEvent->fHighestRating) * 5) // if counting stars, give the difference between the rating and the highest they ever gave
				: (pUGCReviewedSeriesEvent->fHighestRating == 0.0); // if not counting stars, only give 1 credit if it is the first review
	}
	return 0;
}

U32 ugcAchievement_ClientEventFilter(UGCAchievementClientFilter *pUGCAchievementClientFilter, UGCAchievementClientEvent *pUGCAchievementClientEvent)
{
	if(pUGCAchievementClientFilter && pUGCAchievementClientEvent)
	{
		U32 increment = ugcAchievement_MapCreatedEventFilter(pUGCAchievementClientFilter->ugcMapCreatedFilter, pUGCAchievementClientEvent->ugcMapCreatedEvent);
		if(increment)
			return increment;
	}
	return 0;
}

U32 ugcAchievement_ServerEventFilter(UGCAchievementServerFilter *pUGCAchievementServerFilter, UGCAchievementServerEvent *pUGCAchievementServerEvent)
{
	if(pUGCAchievementServerFilter && pUGCAchievementServerEvent)
	{
		U32 increment = ugcAchievement_PlayerReviewerEventFilter(pUGCAchievementServerFilter->ugcPlayerReviewerFilter, pUGCAchievementServerEvent->ugcPlayerReviewerEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_ProjectPublishedEventFilter(pUGCAchievementServerFilter->ugcProjectPublishedFilter, pUGCAchievementServerEvent->ugcProjectPublishedEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_SeriesPublishedEventFilter(pUGCAchievementServerFilter->ugcSeriesPublishedFilter, pUGCAchievementServerEvent->ugcSeriesPublishedEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_ProjectPlayedEventFilter(pUGCAchievementServerFilter->ugcProjectPlayedFilter, pUGCAchievementServerEvent->ugcProjectPlayedEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_PlayedProjectEventFilter(pUGCAchievementServerFilter->ugcPlayedProjectFilter, pUGCAchievementServerEvent->ugcPlayedProjectEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_ProjectReviewedEventFilter(pUGCAchievementServerFilter->ugcProjectReviewedFilter, pUGCAchievementServerEvent->ugcProjectReviewedEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_ReviewedProjectEventFilter(pUGCAchievementServerFilter->ugcReviewedProjectFilter, pUGCAchievementServerEvent->ugcReviewedProjectEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_ProjectTippedEventFilter(pUGCAchievementServerFilter->ugcProjectTippedFilter, pUGCAchievementServerEvent->ugcProjectTippedEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_TippedProjectEventFilter(pUGCAchievementServerFilter->ugcTippedProjectFilter, pUGCAchievementServerEvent->ugcTippedProjectEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_ProjectFeaturedEventFilter(pUGCAchievementServerFilter->ugcProjectFeaturedFilter, pUGCAchievementServerEvent->ugcProjectFeaturedEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_AchievementGrantedEventFilter(pUGCAchievementServerFilter->ugcAchievementGrantedFilter, pUGCAchievementServerEvent->ugcAchievementGrantedEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_SeriesReviewedEventFilter(pUGCAchievementServerFilter->ugcSeriesReviewedFilter, pUGCAchievementServerEvent->ugcSeriesReviewedEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_ReviewedSeriesEventFilter(pUGCAchievementServerFilter->ugcReviewedSeriesFilter, pUGCAchievementServerEvent->ugcReviewedSeriesEvent);
		if(increment)
			return increment;
	}
	return 0;
}

U32 ugcAchievement_EventFilter(UGCAchievementFilter *pUGCAchievementFilter, UGCAchievementEvent *pUGCAchievementEvent)
{
	if(pUGCAchievementFilter && pUGCAchievementEvent)
	{
		U32 increment = ugcAchievement_ClientEventFilter(&pUGCAchievementFilter->ugcAchievementClientFilter, pUGCAchievementEvent->ugcAchievementClientEvent);
		if(increment)
			return increment;

		increment = ugcAchievement_ServerEventFilter(&pUGCAchievementFilter->ugcAchievementServerFilter, pUGCAchievementEvent->ugcAchievementServerEvent);
		if(increment)
			return increment;
	}
	return 0;
}

#define UGC_ACHIEVEMENT_COOLDOWN_PST_OFFSET 8

static U32 blockSecondsHelper(U32 time, U32 hours)
{
	U32 uCurHour = time / SECONDS_PER_HOUR;
	U32 uBlocksSince = uCurHour / hours;	// round down to nearest block
	U32 uBlockSeconds = (uBlocksSince * hours) * SECONDS_PER_HOUR; // convert back to seconds

	// now adjust 12 and 24 hour blocks
	// to make PST time to be 12:00am (or 1:00 during daylight savings)
	if(hours >= 12 && hours % 12 == 0)
	{
		uBlockSeconds += UGC_ACHIEVEMENT_COOLDOWN_PST_OFFSET * SECONDS_PER_HOUR;

		if(uBlockSeconds > time)
		{
			// use previous block
			uBlockSeconds -= hours * SECONDS_PER_HOUR;
		}
	}

	return uBlockSeconds;
}

U32 ugcAchievement_GetCooldownBlockGrantTime(U32 grantTime, UGCAchievementDef *pUGCAchievementDef)
{
	if(pUGCAchievementDef && pUGCAchievementDef->bRepeatable && pUGCAchievementDef->uRepeatCooldownHours > 0 && pUGCAchievementDef->bRepeatCooldownBlockTime)
		return blockSecondsHelper(grantTime, pUGCAchievementDef->uRepeatCooldownHours);

	// no cooldown or don't use block time therefore return passed in time
	return grantTime;
}

U32 ugcAchievement_GetConsecutiveBlockLastCountTime(U32 lastCountTime, UGCAchievementDef *pUGCAchievementDef)
{
	if(pUGCAchievementDef && pUGCAchievementDef->uConsecutiveHours > 0)
		return blockSecondsHelper(lastCountTime, pUGCAchievementDef->uConsecutiveHours);

	// no consecutive hours therefore return passed in time
	return lastCountTime;
}

const char *ugcAchievement_Scope(UGCAchievementDef *pUGCAchievementDef)
{
	while(pUGCAchievementDef)
	{
		if(pUGCAchievementDef->scope)
			return pUGCAchievementDef->scope;
		pUGCAchievementDef = pUGCAchievementDef->pParentDef;
	}
	return "";
}

bool ugcAchievement_IsHidden(UGCAchievementDef *pUGCAchievementDef)
{
	while(pUGCAchievementDef)
	{
		if(pUGCAchievementDef->bHidden)
			return true;
		pUGCAchievementDef = pUGCAchievementDef->pParentDef;
	}
	return false;
}

UGCAchievementDef *ugcAchievement_GetPreviousSibling(UGCAchievementDef *pUGCAchievementDef)
{
	UGCAchievementDef *pParentDef = SAFE_MEMBER(pUGCAchievementDef, pParentDef);
	if(pParentDef)
	{
		UGCAchievementDef *pPreviousSiblingDef = NULL;
		FOR_EACH_IN_EARRAY_FORWARDS(pParentDef->subAchievements, UGCAchievementDef, pSubAchievementDef)
		{
			if(pSubAchievementDef == pUGCAchievementDef)
				return pPreviousSiblingDef;
			pPreviousSiblingDef = pSubAchievementDef;
		}
		FOR_EACH_END;
	}
	return NULL;
}

#include "AutoGen/UGCAchievements_h_ast.c"
