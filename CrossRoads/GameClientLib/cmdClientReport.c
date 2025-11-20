/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

//system
#include "cmdClientReport.h"
#include "error.h"
#include "file.h"
#include "gclEntity.h"
#include "GraphicsLib.h"
#include "qsortG.h"
#include "ResourceInfo.h"
#include "wlEditorIncludes.h"
#include "wlUGC.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

//used by animation assets commands
#include "CostumeCommon.h"
#include "dynAnimChart.h"
#include "dynAnimGraph.h"
#include "dynAnimTemplate.h"
#include "dynAnimTrack.h"
#include "DynMove.h"
#include "dynMoveTransition.h"
#include "dynRagdollData.h"
#include "gclCostumeUI.h"
#include "wlSkelInfo.h"

//used by audio assets commands
#include "dynFxInfo.h"
#include "gclNotify.h"
#include "soundLib.h"
#include "UIGen.h"

#include "cmdClientReport_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););


// +---------------------------------+
// | Report Animation Assets Command |
// +---------------------------------+

//NOTE: at its highest level, this command will always assume all of the cskels & cgeos with subskeleton names are used,
//if you want to clean up the unused costume data 1st, run the costume report command found in cmdServerReport,
//( ReportCostumeAssets), and manually remove the unused costume data

AUTO_ENUM;
typedef enum ReportAnimationAssets_Mode
{
	RAA_ASSUME_COSTUMES = 1,
	RAA_ASSUME_SKELINFOS,
	RAA_ASSUME_CHARTS,
	RAA_ASSUME_MOVES,
} ReportAnimationAssets_Mode;
extern StaticDefineInt ReportAnimationAssets_ModeEnum[];

static char *ReportAnimationAssets_Helper(bool bAssumeCostumes, bool bAssumeSkelInfos, bool bAssumeCharts, bool bAssumeMoves, bool bPrettyPrint);

AUTO_COMMAND;
char *ReportAnimationAssets(ACMD_NAMELIST(ReportAnimationAssets_ModeEnum,STATICDEFINE) char *AssumeMode, bool bPrettyPrint)
{
	ReportAnimationAssets_Mode mode = StaticDefineIntGetInt(ReportAnimationAssets_ModeEnum, AssumeMode);
	if      (mode == RAA_ASSUME_COSTUMES ) return ReportAnimationAssets_Helper(1,0,0,0,bPrettyPrint);
	else if (mode == RAA_ASSUME_SKELINFOS) return ReportAnimationAssets_Helper(0,1,0,0,bPrettyPrint);
	else if (mode == RAA_ASSUME_CHARTS   ) return ReportAnimationAssets_Helper(0,0,1,0,bPrettyPrint);
	else if (mode == RAA_ASSUME_MOVES    ) return ReportAnimationAssets_Helper(0,0,0,1,bPrettyPrint);
	else return "Error: Attempted to run ReportAnimationAssets in an invalid mode!\n";
}

//spacing sizes used for pretty printing
#define S1 7
#define S2 51

static void ReportAnimationAssets_FindSpacers(U32 uiCount, const char *pcName, char *pcSpacer1, char *pcSpacer2)
{
	U32 i, j;

	assert(uiCount && S1 > 2);

	i = S1 - 2; //for the () characters
	j = uiCount;
	while (i > 1 && j > 0) {
		j /= 10;
		i--;
	}

	pcSpacer1[i] = '\0';
	for (j = 0; j < i; j++) {
		pcSpacer1[j] = ' ';
	}

	if ((S2-1) > (strlen(pcName) + (S1-1))) {
		i = (S2-1) - (strlen(pcName) + (S1-1));
	} else {
		i = 1;
	}
	
	pcSpacer2[i] = '\0';
	for (j = 0; j < i; j++) {
		pcSpacer2[j] = ' ';
	}
}

static void ReportAnimationAssets_FindSpacer(const char *pcName, char *pcSpacer2)
{
	U32 i, j;

	if ((S2-1) > strlen(pcName)) {
		i = (S2-1) - strlen(pcName);
	} else {
		i = 1;
	}

	pcSpacer2[i] = '\0';
	for (j = 0; j < i; j++) {
		pcSpacer2[j] = ' ';
	}
}

static void ReportAnimationAssets_InitCounters(	bool bAssumeSkelInfos,
												bool bAssumeCharts,
												bool bAssumeTemplates,
												bool bAssumeGraphs,
												bool bAssumeMoveTransitions,
												bool bAssumeMoves,
												bool bAssumeAnimTracks	)
{
	ResourceIterator rI = {0};
	StashTableIterator stI;
	StashElement stE;

	SkelInfo				*pSkelInfo;
	DynAnimChartLoadTime	*pChartLT;
	DynAnimTemplate			*pTemplate;
	DynAnimGraph			*pGraph;
	DynMoveTransition		*pMoveTransition;
	DynMove					*pMove;
	DynAnimTrackHeader		*pAnimTrackHeader;

	resInitIterator("SkelInfo", &rI);
	while (resIteratorGetNext(&rI, NULL, &pSkelInfo)) {
		pSkelInfo->uiReportCount = bAssumeSkelInfos;
	}
	resFreeIterator(&rI);

	resInitIterator(ANIM_CHART_EDITED_DICTIONARY, &rI);
	while (resIteratorGetNext(&rI, NULL, &pChartLT)) {
		pChartLT->uiReportCount = bAssumeCharts;
	}
	resFreeIterator(&rI);

	resInitIterator(ANIM_TEMPLATE_EDITED_DICTIONARY, &rI);
	while (resIteratorGetNext(&rI, NULL, &pTemplate)) {
		pTemplate->uiReportCount = bAssumeTemplates;
	}
	resFreeIterator(&rI);

	resInitIterator(ANIM_GRAPH_EDITED_DICTIONARY, &rI);
	while (resIteratorGetNext(&rI, NULL, &pGraph)) {
		pGraph->uiReportCount = bAssumeGraphs;
	}
	resFreeIterator(&rI);

	resInitIterator(MOVE_TRANSITION_EDITED_DICTIONARY, &rI);
	while (resIteratorGetNext(&rI, NULL, &pMoveTransition)) {
		pMoveTransition->uiReportCount = bAssumeMoveTransitions;
	}
	resFreeIterator(&rI);

	resInitIterator(DYNMOVE_DICTNAME, &rI);
	while (resIteratorGetNext(&rI, NULL, &pMove)) {
		pMove->uiReportCount = bAssumeMoves;
	}
	resFreeIterator(&rI);

	stashGetIterator(stAnimTrackHeaders, &stI);
	while (stashGetNextElement(&stI, &stE)) {
		pAnimTrackHeader = stashElementGetPointer(stE);
		pAnimTrackHeader->uiReportCount = bAssumeAnimTracks;
	}
}

static void ReportAnimationAssets_ExtrudeCostumeData_IncSkelInfo(const char *pcSkelInfoName)
{
	ResourceIterator iSkelInfo = {0};
	SkelInfo *pSkelInfo;

	resInitIterator("SkelInfo", &iSkelInfo);
	while (resIteratorGetNext(&iSkelInfo, NULL, &pSkelInfo))
	{
		if (pcSkelInfoName == pSkelInfo->pcSkelInfoName)
			pSkelInfo->uiReportCount++;
	}
	resFreeIterator(&iSkelInfo);
}

static void ReportAnimationAssets_ExtrudeCGeoUsage(void)
{
	ResourceIterator iCGeo = {0};
	PCGeometryDef *pCGeo;

	resInitIterator("CostumeGeometry", &iCGeo);
	while (resIteratorGetNext(&iCGeo, NULL, &pCGeo))
	{
		if (SAFE_MEMBER(pCGeo->pOptions,pcSubSkeleton)) {
			ReportAnimationAssets_ExtrudeCostumeData_IncSkelInfo(pCGeo->pOptions->pcSubSkeleton);
		}
	}
	resFreeIterator(&iCGeo);
}

static void ReportAnimationAssets_ExtrudeCSkelUsage(void)
{
	ResourceIterator iCSkel = {0};
	PCSkeletonDef *pCSkel;

	resInitIterator("CostumeSkeleton", &iCSkel);
	while (resIteratorGetNext(&iCSkel, NULL, &pCSkel))
	{
		if (pCSkel->pcSkeleton) {
			ReportAnimationAssets_ExtrudeCostumeData_IncSkelInfo(pCSkel->pcSkeleton);
		}
	}
	resFreeIterator(&iCSkel);
}

static void ReportAnimationAssets_ExtrudeSkelInfo_AnimTrackHelper(SkelInfo *pSkelInfo)
{
	SkelScaleInfo *pScaleInfo = GET_REF(pSkelInfo->hScaleInfo);
	DynRagdollData *pRagdollData = GET_REF(pSkelInfo->hRagdollData);
	DynRagdollData *pRagdollDataHD = GET_REF(pSkelInfo->hRagdollDataHD);

	if (pSkelInfo->bodySockInfo.pcBodySockPose) {
		DynAnimTrackHeader *pAnimTrackHeader = dynAnimTrackHeaderFind(pSkelInfo->bodySockInfo.pcBodySockPose);
		if (pAnimTrackHeader) pAnimTrackHeader->uiReportCount++;
		else Errorf("Failed to find Animation Track %s for Bodysock in SkelInfo file %s", pSkelInfo->bodySockInfo.pcBodySockPose, pSkelInfo->pcFileName);
	}

	if (SAFE_MEMBER(pRagdollData,pcPoseAnimTrack)) {
		DynAnimTrackHeader *pAnimTrackHeader = dynAnimTrackHeaderFind(pRagdollData->pcPoseAnimTrack);
		if (pAnimTrackHeader) pAnimTrackHeader->uiReportCount++;
		else Errorf("Failed to find Animation Track %s for Ragdoll file %s", pRagdollData->pcPoseAnimTrack, pRagdollData->pcFileName);
	}

	if (SAFE_MEMBER(pRagdollDataHD,pcPoseAnimTrack)) {
		DynAnimTrackHeader *pAnimTrackHeader = dynAnimTrackHeaderFind(pRagdollDataHD->pcPoseAnimTrack);
		if (pAnimTrackHeader) pAnimTrackHeader->uiReportCount++;
		else Errorf("Failed to find Animation Track %s for RagdollHD file %s", pRagdollDataHD->pcPoseAnimTrack, pRagdollDataHD->pcFileName);
	}

	if (pScaleInfo) {
		FOR_EACH_IN_EARRAY(pScaleInfo->eaScaleAnimTrack, SkelScaleAnimTrack, pScaleAnimTrack)
		{
			DynAnimTrackHeader *pAnimTrackHeader = dynAnimTrackHeaderFind(pScaleAnimTrack->pcScaleAnimFile);
			if (pAnimTrackHeader) pAnimTrackHeader->uiReportCount++;
			else Errorf("Failed to find Animation Track %s for Scale Info file %s", pScaleAnimTrack->pcScaleAnimFile, pScaleInfo->pcFileName);
		}
		FOR_EACH_END;
	}
}

static void ReportAnimationAssets_ExtrudeSkelInfo_IncLoadTimeChart(const char *pcName)
{
	ResourceIterator iChart = {0};
	DynAnimChartLoadTime *pChartLT;

	resInitIterator(ANIM_CHART_EDITED_DICTIONARY, &iChart);
	while (resIteratorGetNext(&iChart, NULL, &pChartLT))
	{
		if (pChartLT->pcName == pcName) {
			assert(!pChartLT->bIsSubChart && !REF_HANDLE_IS_ACTIVE(pChartLT->hBaseChart));
			pChartLT->uiReportCount++;
			break;
		}
	}
	resFreeIterator(&iChart);

}

static void ReportAnimationAssets_ExtrudeSkelInfoUsage(void)
{
	ResourceIterator iSkelInfo = {0};
	SkelInfo *pSkelInfo;

	resInitIterator("SkelInfo", &iSkelInfo);
	while (resIteratorGetNext(&iSkelInfo, NULL, &pSkelInfo))
	{
		if (pSkelInfo->uiReportCount)
		{
			DynAnimChartRunTime *pChartRT;
			SkelBlendInfo *pBlendInfo;

			//find any animation tracks linked through the skelinfo (via BodySockInfo, SkelScaleInfo, DynRagdollData)
			ReportAnimationAssets_ExtrudeSkelInfo_AnimTrackHelper(pSkelInfo);

			//find any base charts directly linked to the skelinfo
			if (pChartRT = GET_REF(pSkelInfo->hDefaultChart)) ReportAnimationAssets_ExtrudeSkelInfo_IncLoadTimeChart(pChartRT->pcName);
			if (pChartRT = GET_REF(pSkelInfo->hMountedChart)) ReportAnimationAssets_ExtrudeSkelInfo_IncLoadTimeChart(pChartRT->pcName);

			//find any base charts linked to the blend info
			if (pBlendInfo = GET_REF(pSkelInfo->hBlendInfo))
			{
				if (pChartRT = GET_REF(pBlendInfo->hDefaultChart)) ReportAnimationAssets_ExtrudeSkelInfo_IncLoadTimeChart(pChartRT->pcName);
				if (pChartRT = GET_REF(pBlendInfo->hMountedChart)) ReportAnimationAssets_ExtrudeSkelInfo_IncLoadTimeChart(pChartRT->pcName);
				FOR_EACH_IN_EARRAY(pBlendInfo->eaSequencer, SkelBlendSeqInfo, pSqr) {
					if (pChartRT = GET_REF(pSqr->hChart)) ReportAnimationAssets_ExtrudeSkelInfo_IncLoadTimeChart(pChartRT->pcName);
				} FOR_EACH_END;
			}

		}
	}
	resFreeIterator(&iSkelInfo);
}

static void ReportAnimationAssets_ExtrudeAllSkelInfoAnimTrackUsage(void)
{
	ResourceIterator iSkelInfo = {0};
	SkelInfo *pSkelInfo;

	resInitIterator("SkelInfo", &iSkelInfo);
	while (resIteratorGetNext(&iSkelInfo, NULL, &pSkelInfo))
	{
		//find any animation tracks linked through the skelinfo (via BodySockInfo, SkelScaleInfo, DynRagdollData)
		ReportAnimationAssets_ExtrudeSkelInfo_AnimTrackHelper(pSkelInfo);
	}
	resFreeIterator(&iSkelInfo);
}

static void ReportAnimationAssets_ExtrudeSubChartUsage(DynAnimChartLoadTime *pChartLT)
{
	FOR_EACH_IN_EARRAY(pChartLT->eaSubCharts, DynAnimSubChartRef, pSubChartRef) {
		DynAnimChartLoadTime *pSubChart;
		if (pSubChart = GET_REF(pSubChartRef->hSubChart))
		{
			pSubChart->uiReportCount += pChartLT->uiReportCount;
			ReportAnimationAssets_ExtrudeSubChartUsage(pSubChart);
		}
	} FOR_EACH_END;
}

static void ReportAnimationAssets_ExtrudeChartUsage(void)
{
	ResourceIterator iChart = {0};
	DynAnimChartLoadTime *pChartLT;

	//1st pass to find stance and sub-chart reference counts
	resInitIterator(ANIM_CHART_EDITED_DICTIONARY, &iChart);
	while (resIteratorGetNext(&iChart, NULL, &pChartLT))
	{
		//stance charts
		DynAnimChartLoadTime *pBaseChartLT;
		if (pBaseChartLT = GET_REF(pChartLT->hBaseChart)) {
			assert(!REF_HANDLE_IS_ACTIVE(pBaseChartLT->hBaseChart));
			pChartLT->uiReportCount = pBaseChartLT->uiReportCount;
		}

		//sub-charts (sub-subs are handled recursively)
		if (pChartLT->uiReportCount > 0) {
			ReportAnimationAssets_ExtrudeSubChartUsage(pChartLT);
		}
	}
	resFreeIterator(&iChart);

	//2nd pass to extrude the usage
	resInitIterator(ANIM_CHART_EDITED_DICTIONARY, &iChart);
	while (resIteratorGetNext(&iChart, NULL, &pChartLT))
	{
		if (pChartLT->uiReportCount > 0)
		{
			FOR_EACH_IN_EARRAY(pChartLT->eaGraphRefs, DynAnimChartGraphRefLoadTime, pGraphRef)
			{
				DynAnimGraph *pGraph;

				if (pGraph = GET_REF(pGraphRef->hGraph)) {
					pGraph->uiReportCount++;
				}

				FOR_EACH_IN_EARRAY(pGraphRef->eaGraphChances, DynAnimGraphChanceRef, pGraphChanceRef) {
					if (pGraph = GET_REF(pGraphChanceRef->hGraph)) {
						pGraph->uiReportCount++;
					}
				} FOR_EACH_END;
			}
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY(pChartLT->eaMoveRefs, DynAnimChartMoveRefLoadTime, pMoveRef)
			{
				DynMove *pMove;

				if (pMove = GET_REF(pMoveRef->hMove)) {
					pMove->uiReportCount++;
				}

				FOR_EACH_IN_EARRAY(pMoveRef->eaMoveChances, DynAnimMoveChanceRef, pMoveChanceRef) {
					if (pMove = GET_REF(pMoveChanceRef->hMove)) {
						pMove->uiReportCount++;
					}
				} FOR_EACH_END;
			}
			FOR_EACH_END;
		}
	}
	resFreeIterator(&iChart);
}

static void ReportAnimationAssets_ExtrudeGraphUsage(void)
{
	DynAnimGraph **eaUntraversedGraphs = NULL;
	ResourceIterator iGraph = {0};
	DynAnimGraph *pGraph;

	eaCreate(&eaUntraversedGraphs);

	//build a list of all graphs referenced by a chart
	resInitIterator(ANIM_GRAPH_EDITED_DICTIONARY, &iGraph);
	while (resIteratorGetNext(&iGraph, NULL, &pGraph))
	{
		if (pGraph->uiReportCount > 0) {
			eaPush(&eaUntraversedGraphs, pGraph);
		}
	}
	resFreeIterator(&iGraph);

	//loop through the list of graphs and add post-idles when their graph is found for the 1st time only (so we don't double count any references)
	while (eaSize(&eaUntraversedGraphs) > 0)
	{
		DynAnimGraph *pPostIdle;
		DynAnimTemplate *pTemplate;
		DynMove *pMove;

		pGraph = eaUntraversedGraphs[0];
		assert(pGraph->uiReportCount > 0);
		eaRemoveFast(&eaUntraversedGraphs, 0);

		if (pTemplate = GET_REF(pGraph->hTemplate)) {
			pTemplate->uiReportCount++;
		}

		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pGraphNode)
		{
			FOR_EACH_IN_EARRAY(pGraphNode->eaMove, DynAnimGraphMove, pGraphMove)
			{
				if (pMove = GET_REF(pGraphMove->hMove)) {
					pMove->uiReportCount++;
				}
			}
			FOR_EACH_END;

			if (pPostIdle = GET_REF(pGraphNode->hPostIdle))
			{
				pPostIdle->uiReportCount++;
				if (pPostIdle->uiReportCount == 1) {
					eaPush(&eaUntraversedGraphs, pPostIdle);
				}
			}
		}
		FOR_EACH_END;
	}

	eaDestroy(&eaUntraversedGraphs);
}

static void ReportAnimationAssets_ExtrudeMoveTransitionUsage(void)
{
	ResourceIterator iMoveTransition = {0};
	DynMoveTransition *pMoveTransition;

	resInitIterator(MOVE_TRANSITION_EDITED_DICTIONARY, &iMoveTransition);
	while (resIteratorGetNext(&iMoveTransition, NULL, &pMoveTransition))
	{
		DynAnimChartLoadTime *pChartLT = GET_REF(pMoveTransition->hChart);
		if (pChartLT &&
			pChartLT->uiReportCount > 0)
		{
			DynMove *pMove;
			pMoveTransition->uiReportCount++;
			if (pMove = GET_REF(pMoveTransition->hMove)) {
				pMove->uiReportCount++;
			}
		}
	}
	resFreeIterator(&iMoveTransition);
}

static void ReportAnimationAssets_ExtrudeMoveUsage(void)
{
	ResourceIterator iMove = {0};
	DynMove *pMove;

	resInitIterator(DYNMOVE_DICTNAME, &iMove);
	while (resIteratorGetNext(&iMove, NULL, &pMove))
	{
		if (pMove->uiReportCount > 0)
		{
			FOR_EACH_IN_EARRAY(pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq)
			{
				pMoveSeq->dynMoveAnimTrack.pAnimTrackHeader->uiReportCount++;
			}
			FOR_EACH_END;
		}
	}
	resFreeIterator(&iMove);
}

static void ReportAnimationAssets_DetermineCGeoUsage(	PCGeometryDef ***peaCGeoUsed,
														PCGeometryDef ***peaCGeoUnused)
{
	PCGeometryDef *pCGeo;
	ResourceIterator rI = {0};

	//I dumbed down this function since we can only ever assume that all cgeos are used,
	//what it'll do here is make the lists based on cgeos with sub-skeletons
	resInitIterator("CostumeGeometry", &rI);
	while (resIteratorGetNext(&rI, NULL, &pCGeo)) {
		if (SAFE_MEMBER(pCGeo->pOptions,pcSubSkeleton)) {
			eaPush(peaCGeoUsed, pCGeo);
		} else {
			eaPush(peaCGeoUnused, pCGeo);
		}
	}
	resFreeIterator(&rI);
}

static void ReportAnimationAssets_DetermineCSkelUsage(	PCSkeletonDef ***peaCSkelUsed,
														PCSkeletonDef ***peaCSkelUnused)
{
	PCSkeletonDef *pCSkel;
	ResourceIterator rI = {0};

	//I dumbed down this function since we can only ever assume that all cskels are used
	resInitIterator("CostumeSkeleton", &rI);
	while (resIteratorGetNext(&rI, NULL, &pCSkel)) {
		eaPush(peaCSkelUsed, pCSkel);
	}
	resFreeIterator(&rI);
}

static void ReportAnimationAssets_DetermineSkelInfoUsage(	SkelInfo ***peaSkelInfoUsed,
															SkelInfo ***peaSkelInfoUnused)
{
	SkelInfo *pSkelInfo;
	ResourceIterator rI = {0};

	resInitIterator("SkelInfo", &rI);
	while (resIteratorGetNext(&rI, NULL, &pSkelInfo)) {
		if (pSkelInfo->uiReportCount) {
			eaPush(peaSkelInfoUsed, pSkelInfo);
		} else {
			eaPush(peaSkelInfoUnused, pSkelInfo);
		}
	}
	resFreeIterator(&rI);
}

static void ReportAnimationAssets_DetermineChartUsage(	DynAnimChartLoadTime ***peaDynAnimChartLoadTimeUsed,
														DynAnimChartLoadTime ***peaDynAnimChartLoadTimeUnused)
{
	DynAnimChartLoadTime *pChartLT;
	ResourceIterator rI = {0};

	resInitIterator(ANIM_CHART_EDITED_DICTIONARY, &rI);
	while (resIteratorGetNext(&rI, NULL, &pChartLT)) {
		if (pChartLT->uiReportCount) {
			eaPush(peaDynAnimChartLoadTimeUsed, pChartLT);
		} else {
			eaPush(peaDynAnimChartLoadTimeUnused, pChartLT);
		}
	}
	resFreeIterator(&rI);
}

static void ReportAnimationAssets_DetermineTemplateUsage(	DynAnimTemplate ***peaDynAnimTemplateUsed,
															DynAnimTemplate ***peaDynAnimTemplateUnused)
{
	DynAnimTemplate *pTemplate;
	ResourceIterator rI = {0};

	resInitIterator(ANIM_TEMPLATE_EDITED_DICTIONARY, &rI);
	while (resIteratorGetNext(&rI, NULL, &pTemplate)) {
		if (pTemplate->uiReportCount) {
			eaPush(peaDynAnimTemplateUsed, pTemplate);
		} else {
			eaPush(peaDynAnimTemplateUnused, pTemplate);
		}
	}
	resFreeIterator(&rI);
}

static void ReportAnimationAssets_DetermineGraphUsage(	DynAnimGraph ***peaDynAnimGraphUsed,
														DynAnimGraph ***peaDynAnimGraphUnused)
{
	DynAnimGraph *pGraph;
	ResourceIterator rI = {0};

	resInitIterator(ANIM_GRAPH_EDITED_DICTIONARY, &rI);
	while (resIteratorGetNext(&rI, NULL, &pGraph)) {
		if (pGraph->uiReportCount) {
			eaPush(peaDynAnimGraphUsed, pGraph);
		} else {
			eaPush(peaDynAnimGraphUnused, pGraph);
		}
	}
	resFreeIterator(&rI);
}

static void ReportAnimationAssets_DetermineMoveTransitionUsage(	DynMoveTransition ***peaDynMoveTransitionUsed,
																	DynMoveTransition ***peaDynMoveTransitionUnused)
{
	DynMoveTransition *pMoveTransition;
	ResourceIterator rI = {0};

	resInitIterator(MOVE_TRANSITION_EDITED_DICTIONARY, &rI);
	while (resIteratorGetNext(&rI, NULL, &pMoveTransition)) {
		if (pMoveTransition->uiReportCount) {
			eaPush(peaDynMoveTransitionUsed, pMoveTransition);
		} else {
			eaPush(peaDynMoveTransitionUnused, pMoveTransition);
		}
	}
	resFreeIterator(&rI);
}

static void ReportAnimationAssets_DetermineMoveUsage(	DynMove	***peaDynMoveUsed,
														DynMove ***peaDynMoveUnused)
{
	DynMove *pMove;
	ResourceIterator rI = {0};

	resInitIterator(DYNMOVE_DICTNAME, &rI);
	while (resIteratorGetNext(&rI, NULL, &pMove)) {
		if (pMove->uiReportCount) {
			eaPush(peaDynMoveUsed, pMove);
		} else {
			eaPush(peaDynMoveUnused, pMove);
		}
	}
	resFreeIterator(&rI);
}

static void ReportAnimationAssets_DetermineAnimTrackUsage(	DynAnimTrackHeader ***peaDynAnimTrackHeaderUsed,
															DynAnimTrackHeader ***peaDynAnimTrackHeaderUnused)
{
	DynAnimTrackHeader *pAnimTrackHeader;
	StashTableIterator stI;
	StashElement stE;

	stashGetIterator(stAnimTrackHeaders, &stI);
	while (stashGetNextElement(&stI, &stE)) {
		pAnimTrackHeader = (DynAnimTrackHeader*)stashElementGetPointer(stE);
		if (pAnimTrackHeader->uiReportCount) {
			eaPush(peaDynAnimTrackHeaderUsed, pAnimTrackHeader);
		} else {
			eaPush(peaDynAnimTrackHeaderUnused, pAnimTrackHeader);
		}
	}
}

static void ReportAnimationAssets_PrintUsed(FILE *file, U32 uiUses, const char *pcName, const char *pcFilename, bool bPrettyPrint)
{
	if (bPrettyPrint)
	{
		char spacer1[S1];
		char spacer2[S2];
		ReportAnimationAssets_FindSpacers(uiUses,pcName,spacer1,spacer2);
		fprintf(file,"(%u)%s%s%s%s\n",uiUses,spacer1,pcName,spacer2,pcFilename);
	}
	else
	{
		fprintf(file,"%u\t%s\t%s\n",uiUses,pcName,pcFilename);
	}
}

static void ReportAnimationAssets_PrintUnused(FILE *file, const char *pcName, const char *pcFilename, bool bPrettyPrint)
{
	if (bPrettyPrint)
	{
		char spacer[S2];
		ReportAnimationAssets_FindSpacer(pcName,spacer);
		fprintf(file,"%s%s%s\n",pcName,spacer,pcFilename);
	}
	else
	{
		fprintf(file,"%s\t%s\n",pcName,pcFilename);
	}
}

static void ReportAnimationAssets_PrintSpecialCostumeUICSkel(FILE *file, bool bPrettyPrint)
{
	PCSkeletonDef *pCSkel = CharacterCreation_GetPlainSkeleton();

	fprintf(file, "\nSpecial CSkels used by Character Creator for Plain Costumes:\n");
	ReportAnimationAssets_PrintUsed(file, 1, pCSkel->pcName, pCSkel->pcFileName, bPrettyPrint);
}

static void ReportAnimationAssets_PrintUsedCGeos(FILE *file, PCGeometryDef ***peaCGeoUsed, bool bPrettyPrint)
{
	fprintf(file,"\nUSED CGeos:\n");
	FOR_EACH_IN_EARRAY(*peaCGeoUsed, PCGeometryDef, pCGeo)
	{
		assert(SAFE_MEMBER(pCGeo->pOptions,pcSubSkeleton));
		ReportAnimationAssets_PrintUsed(file, 1, pCGeo->pcName, pCGeo->pcFileName, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUnusedCGeos(FILE *file, PCGeometryDef ***peaCGeosUnused, bool bPrettyPrint)
{
	fprintf(file,"\nUNUSED CGeos:\n");
	FOR_EACH_IN_EARRAY(*peaCGeosUnused, PCGeometryDef, pCGeo)
	{
		assert(!SAFE_MEMBER(pCGeo->pOptions,pcSubSkeleton));
		ReportAnimationAssets_PrintUnused(file, pCGeo->pcName, pCGeo->pcFileName, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUsedCSkels(FILE *file, PCSkeletonDef ***peaCSkelUsed, bool bPrettyPrint)
{
	fprintf(file,"\nUSED CSkels:\n");
	FOR_EACH_IN_EARRAY(*peaCSkelUsed, PCSkeletonDef, pCSkel)
	{
		//should only ever happen when we've always assumed the cskels to be valid
		ReportAnimationAssets_PrintUsed(file, 1, pCSkel->pcName, pCSkel->pcFileName, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUnusedCSkels(FILE *file, PCSkeletonDef ***peaCSkelsUnused, bool bPrettyPrint)
{
	fprintf(file,"\nUNUSED CSkels:\n");
	FOR_EACH_IN_EARRAY(*peaCSkelsUnused, PCSkeletonDef, pCSkel)
	{
		//should never happen
		assert(0);
		ReportAnimationAssets_PrintUnused(file, pCSkel->pcName, pCSkel->pcFileName, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUsedSkelInfos(FILE *file, SkelInfo ***peaSkelInfoUsed, bool bPrettyPrint)
{
	fprintf(file,"\nUSED Skel Infos:\n");
	FOR_EACH_IN_EARRAY(*peaSkelInfoUsed, SkelInfo, pSkelInfo)
	{
		assert(pSkelInfo->uiReportCount);
		ReportAnimationAssets_PrintUsed(file, pSkelInfo->uiReportCount, pSkelInfo->pcSkelInfoName, pSkelInfo->pcFileName, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUnusedSkelInfos(FILE *file, SkelInfo ***peaSkelInfoUnused, bool bPrettyPrint)
{
	fprintf(file,"\nUNUSED Skel Infos:\n");
	FOR_EACH_IN_EARRAY(*peaSkelInfoUnused, SkelInfo, pSkelInfo)
	{
		assert(!pSkelInfo->uiReportCount);
		ReportAnimationAssets_PrintUnused(file, pSkelInfo->pcSkelInfoName, pSkelInfo->pcFileName, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUsedCharts(FILE *file, DynAnimChartLoadTime ***peaDynAnimChartLoadTimeUsed, bool bPrettyPrint)
{
	fprintf(file,"\nUSED Animation Charts:\n");
	FOR_EACH_IN_EARRAY(*peaDynAnimChartLoadTimeUsed, DynAnimChartLoadTime, pChartLT)
	{
		assert(pChartLT->uiReportCount);
		ReportAnimationAssets_PrintUsed(file, pChartLT->uiReportCount, pChartLT->pcName, pChartLT->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUnusedCharts(FILE *file, DynAnimChartLoadTime ***peaDynAnimChartLoadTimeUnused, bool bPrettyPrint)
{
	fprintf(file,"\nUNUSED Animation Charts:\n");
	FOR_EACH_IN_EARRAY(*peaDynAnimChartLoadTimeUnused, DynAnimChartLoadTime, pChartLT)
	{
		assert(!pChartLT->uiReportCount);
		ReportAnimationAssets_PrintUnused(file, pChartLT->pcName, pChartLT->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUsedTemplates(FILE *file, DynAnimTemplate ***peaDynAnimTemplateUsed, bool bPrettyPrint)
{
	fprintf(file, "\nUSED Animation Templates:\n");
	FOR_EACH_IN_EARRAY(*peaDynAnimTemplateUsed, DynAnimTemplate, pTemplate)
	{
		assert(pTemplate->uiReportCount);
		ReportAnimationAssets_PrintUsed(file, pTemplate->uiReportCount, pTemplate->pcName, pTemplate->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUnusedTemplates(FILE *file, DynAnimTemplate ***peaDynAnimTemplateUnused, bool bPrettyPrint)
{
	fprintf(file, "\nUNUSED Animation Templates:\n");
	FOR_EACH_IN_EARRAY(*peaDynAnimTemplateUnused, DynAnimTemplate, pTemplate)
	{
		assert(!pTemplate->uiReportCount);
		ReportAnimationAssets_PrintUnused(file, pTemplate->pcName, pTemplate->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUsedGraphs(FILE *file, DynAnimGraph ***peaDynAnimGraphUsed, bool bPrettyPrint)
{
	fprintf(file, "\nUSED Animation Graphs:\n");
	FOR_EACH_IN_EARRAY(*peaDynAnimGraphUsed, DynAnimGraph, pGraph)
	{
		assert(pGraph->uiReportCount);
		ReportAnimationAssets_PrintUsed(file, pGraph->uiReportCount, pGraph->pcName, pGraph->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUnusedGraphs(FILE *file, DynAnimGraph ***peaDynAnimGraphUnused, bool bPrettyPrint)
{
	fprintf(file, "\nUNUSED Animation Graphs:\n");
	FOR_EACH_IN_EARRAY(*peaDynAnimGraphUnused, DynAnimGraph, pGraph)
	{
		assert(!pGraph->uiReportCount);
		ReportAnimationAssets_PrintUnused(file, pGraph->pcName, pGraph->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUsedMoveTransitions(FILE *file, DynMoveTransition ***peaDynMoveTransitionUsed, bool bPrettyPrint)
{
	fprintf(file, "\nUSED Move Transitions:\n");
	FOR_EACH_IN_EARRAY(*peaDynMoveTransitionUsed, DynMoveTransition, pMoveTransition)
	{
		assert(pMoveTransition->uiReportCount);
		ReportAnimationAssets_PrintUsed(file, pMoveTransition->uiReportCount, pMoveTransition->pcName, pMoveTransition->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUnusedMoveTransitions(FILE *file, DynMoveTransition ***peaDynMoveTransitionUnused, bool bPrettyPrint)
{
	fprintf(file, "\nUNUSED Move Transitions:\n");
	FOR_EACH_IN_EARRAY(*peaDynMoveTransitionUnused, DynMoveTransition, pMoveTransition)
	{
		assert(!pMoveTransition->uiReportCount);
		ReportAnimationAssets_PrintUnused(file, pMoveTransition->pcName, pMoveTransition->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUsedMoves(FILE *file, DynMove ***peaDynMoveUsed, bool bPrettyPrint)
{
	fprintf(file, "\nUSED Moves:\n");
	FOR_EACH_IN_EARRAY(*peaDynMoveUsed, DynMove, pMove)
	{
		assert(pMove->uiReportCount);
		ReportAnimationAssets_PrintUsed(file, pMove->uiReportCount, pMove->pcName, pMove->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUnusedMoves(FILE *file, DynMove ***peaDynMoveUnused, bool bPrettyPrint)
{
	fprintf(file, "\nUNUSED Moves:\n");
	FOR_EACH_IN_EARRAY(*peaDynMoveUnused, DynMove, pMove)
	{
		assert(!pMove->uiReportCount);
		ReportAnimationAssets_PrintUnused(file, pMove->pcName, pMove->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUsedAnimTracks(FILE *file, DynAnimTrackHeader ***peaDynAnimTrackHeaderUsed, bool bPrettyPrint)
{
	fprintf(file, "\nUSED Animation Tracks:\n");
	FOR_EACH_IN_EARRAY(*peaDynAnimTrackHeaderUsed, DynAnimTrackHeader, pAnimTrackHeader)
	{
		assert(pAnimTrackHeader->uiReportCount);
		ReportAnimationAssets_PrintUsed(file, pAnimTrackHeader->uiReportCount, pAnimTrackHeader->pcName, pAnimTrackHeader->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static void ReportAnimationAssets_PrintUnusedAnimTracks(FILE *file, DynAnimTrackHeader ***peaDynAnimTrackHeaderUnused, bool bPrettyPrint)
{
	fprintf(file, "\nUNUSED Animation Tracks:\n");
	FOR_EACH_IN_EARRAY(*peaDynAnimTrackHeaderUnused, DynAnimTrackHeader, pAnimTrackHeader)
	{
		assert(!pAnimTrackHeader->uiReportCount);
		ReportAnimationAssets_PrintUnused(file, pAnimTrackHeader->pcName, pAnimTrackHeader->pcFilename, bPrettyPrint);
	}
	FOR_EACH_END;
}

static char *ReportAnimationAssets_Helper(	bool bAssumeCostumes,
											bool bAssumeSkelInfos,
											bool bAssumeCharts,
											bool bAssumeMoves,
											bool bPrettyPrint)
{
	PCGeometryDef			**eaCGeoUsed					= NULL, **eaCGeoUnused					= NULL;
	PCSkeletonDef			**eaCSkelUsed					= NULL, **eaCSkelUnused					= NULL;
	SkelInfo				**eaSkelInfoUsed				= NULL, **eaSkelInfoUnused				= NULL;
	DynAnimChartLoadTime	**eaDynAnimChartLoadTimeUsed	= NULL,	**eaDynAnimChartLoadTimeUnused	= NULL;
	DynAnimTemplate			**eaDynAnimTemplateUsed			= NULL,	**eaDynAnimTemplateUnused		= NULL;
	DynAnimGraph			**eaDynAnimGraphUsed			= NULL,	**eaDynAnimGraphUnused			= NULL;
	DynMoveTransition		**eaDynMoveTransitionUsed		= NULL,	**eaDynMoveTransitionUnused		= NULL;
	DynMove					**eaDynMoveUsed					= NULL,	**eaDynMoveUnused				= NULL;
	DynAnimTrackHeader		**eaDynAnimTrackHeaderUsed		= NULL,	**eaDynAnimTrackHeaderUnused	= NULL;

	U32 uiCGeoCount					= resDictGetNumberOfObjects("CostumeGeometry");
	U32 uiCSkelCount				= resDictGetNumberOfObjects("CostumeSkeleton");
	U32 uiSkelInfoCount				= resDictGetNumberOfObjects("SkelInfo");
	U32 uiDynAnimChartCount			= resDictGetNumberOfObjects(ANIM_CHART_EDITED_DICTIONARY);
	U32 uiDynAnimTemplateCount		= resDictGetNumberOfObjects(ANIM_TEMPLATE_EDITED_DICTIONARY);
	U32 uiDynAnimGraphCount			= resDictGetNumberOfObjects(ANIM_GRAPH_EDITED_DICTIONARY);
	U32 uiDynMoveTransitionCount	= resDictGetNumberOfObjects(MOVE_TRANSITION_EDITED_DICTIONARY);
	U32 uiDynMoveCount				= resDictGetNumberOfObjects(DYNMOVE_DICTNAME);
	U32 uiDynAnimTrackCount			= stashGetCount(stAnimTrackHeaders);

	FILE *file;	

	if (!gConf.bNewAnimationSystem) {
		return "ReportAnimationAssets only works with the new animation system!\n";
	}

	file = fopen("c:\\AnimationAssetReport.txt", "w");
	if (!file) {
		return "Can't open file 'c:\\AnimationAssetReport.txt'.\n";
	}

	ReportAnimationAssets_InitCounters(	//cskels will always be assumed (to get skif links)
										//cgeos with sub-skeletons will always be assumed (to get skif links)
										bAssumeSkelInfos,
										bAssumeCharts,
										0,
										0,
										0,
										bAssumeMoves,
										0);

	if (bAssumeCostumes) {
		ReportAnimationAssets_ExtrudeCGeoUsage();
		ReportAnimationAssets_ExtrudeCSkelUsage();
	}
	if (bAssumeCharts || bAssumeMoves)
		ReportAnimationAssets_ExtrudeAllSkelInfoAnimTrackUsage();
	else
		ReportAnimationAssets_ExtrudeSkelInfoUsage();
	if (!bAssumeMoves) {
		ReportAnimationAssets_ExtrudeChartUsage();
		ReportAnimationAssets_ExtrudeGraphUsage();
		ReportAnimationAssets_ExtrudeMoveTransitionUsage();
	}
	ReportAnimationAssets_ExtrudeMoveUsage();

	eaCreate(&eaCGeoUsed				);
	eaCreate(&eaCSkelUsed				);
	eaCreate(&eaSkelInfoUsed			);
	eaCreate(&eaDynAnimChartLoadTimeUsed);
	eaCreate(&eaDynAnimTemplateUsed		);
	eaCreate(&eaDynAnimGraphUsed		);
	eaCreate(&eaDynMoveTransitionUsed	);
	eaCreate(&eaDynMoveUsed				);
	eaCreate(&eaDynAnimTrackHeaderUsed	);

	eaCreate(&eaCGeoUnused					);
	eaCreate(&eaCSkelUnused					);
	eaCreate(&eaSkelInfoUnused				);
	eaCreate(&eaDynAnimChartLoadTimeUnused	);
	eaCreate(&eaDynAnimTemplateUnused		);
	eaCreate(&eaDynAnimGraphUnused			);
	eaCreate(&eaDynMoveTransitionUnused		);
	eaCreate(&eaDynMoveUnused				);
	eaCreate(&eaDynAnimTrackHeaderUnused	);

	ReportAnimationAssets_DetermineCGeoUsage			(&eaCGeoUsed,					&eaCGeoUnused					);
	ReportAnimationAssets_DetermineCSkelUsage			(&eaCSkelUsed,					&eaCSkelUnused					);
	ReportAnimationAssets_DetermineSkelInfoUsage		(&eaSkelInfoUsed,				&eaSkelInfoUnused				);
	ReportAnimationAssets_DetermineChartUsage			(&eaDynAnimChartLoadTimeUsed,	&eaDynAnimChartLoadTimeUnused	);
	ReportAnimationAssets_DetermineTemplateUsage		(&eaDynAnimTemplateUsed,		&eaDynAnimTemplateUnused		);
	ReportAnimationAssets_DetermineGraphUsage			(&eaDynAnimGraphUsed,			&eaDynAnimGraphUnused			);
	ReportAnimationAssets_DetermineMoveTransitionUsage	(&eaDynMoveTransitionUsed,		&eaDynMoveTransitionUnused		);
	ReportAnimationAssets_DetermineMoveUsage			(&eaDynMoveUsed,				&eaDynMoveUnused				);
	ReportAnimationAssets_DetermineAnimTrackUsage		(&eaDynAnimTrackHeaderUsed,		&eaDynAnimTrackHeaderUnused		);

	assert(uiCGeoCount				== (U32)(eaSize(&eaCGeoUsed)				+ eaSize(&eaCGeoUnused))				);
	assert(uiCSkelCount				== (U32)(eaSize(&eaCSkelUsed)				+ eaSize(&eaCSkelUnused))				);
	assert(uiSkelInfoCount			== (U32)(eaSize(&eaSkelInfoUsed)			+ eaSize(&eaSkelInfoUnused))			);
	assert(uiDynAnimChartCount		== (U32)(eaSize(&eaDynAnimChartLoadTimeUsed)+ eaSize(&eaDynAnimChartLoadTimeUnused)));
	assert(uiDynAnimTemplateCount	== (U32)(eaSize(&eaDynAnimTemplateUsed)		+ eaSize(&eaDynAnimTemplateUnused))		);
	assert(uiDynAnimGraphCount		== (U32)(eaSize(&eaDynAnimGraphUsed)		+ eaSize(&eaDynAnimGraphUnused))		);
	assert(uiDynMoveTransitionCount	== (U32)(eaSize(&eaDynMoveTransitionUsed)	+ eaSize(&eaDynMoveTransitionUnused))	);
	assert(uiDynMoveCount			== (U32)(eaSize(&eaDynMoveUsed)				+ eaSize(&eaDynMoveUnused))				);
	assert(uiDynAnimTrackCount		== (U32)(eaSize(&eaDynAnimTrackHeaderUsed)	+ eaSize(&eaDynAnimTrackHeaderUnused))	);

	fprintf(file, "==============================================================\n");
	fprintf(file, "Overall Asset Counts\n");
	if (bAssumeCostumes) {
		fprintf(file, "- Assuming ALL Costumes are valid!!!\n\
NOTE: at its highest level, this command will always assume all of the costumes (really cskels & cgeos wt. sub-skels)\n\
are used, if you want to clean up the unused costume data 1st, run the costume report command ReportCostumeAssets, which\n\
will require a server to be running and you to manually remove the bad costume data.\n");
	}
	if (bAssumeSkelInfos)	fprintf(file, "- Assuming ALL SkelInfos are valid!!!\n");
	if (bAssumeCharts)		fprintf(file, "- Assuming ALL Charts and SkelInfo referenced AnimTracks are valid!!!\n");
	if (bAssumeMoves)		fprintf(file, "- Assuming ALL Moves and SkelInfo referenced AnimTracks are valid!!!\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "(search for the string before \":\" to find a data-type in file)\n\n");
	if (bAssumeCostumes)
		fprintf(file, "CSkels:              %u total, %u used, %u unused\n",	uiCSkelCount,				eaSize(&eaCSkelUsed),					eaSize(&eaCSkelUnused)					);
		fprintf(file, "CGeos:               %u total, %u used, %u unused (used = has subskel, unused = NO subskel)\n", uiCGeoCount, eaSize(&eaCGeoUsed), eaSize(&eaCGeoUnused)				);
	if (!bAssumeCharts && !bAssumeMoves)
		fprintf(file, "Skel Infos:          %u total, %u used, %u unused\n",	uiSkelInfoCount,			eaSize(&eaSkelInfoUsed),				eaSize(&eaSkelInfoUnused)				);
	if (!bAssumeMoves) {
		fprintf(file, "Animation Charts:    %u total, %u used, %u unused\n",	uiDynAnimChartCount,		eaSize(&eaDynAnimChartLoadTimeUsed),	eaSize(&eaDynAnimChartLoadTimeUnused)	);
		fprintf(file, "Animation Templates: %u total, %u used, %u unused\n",	uiDynAnimTemplateCount,		eaSize(&eaDynAnimTemplateUsed),			eaSize(&eaDynAnimTemplateUnused)		);
		fprintf(file, "Animation Graphs:    %u total, %u used, %u unused\n",	uiDynAnimGraphCount,		eaSize(&eaDynAnimGraphUsed),			eaSize(&eaDynAnimGraphUnused)			);
		fprintf(file, "Move Transitions:    %u total, %u used, %u unused\n",	uiDynMoveTransitionCount,	eaSize(&eaDynMoveTransitionUsed),		eaSize(&eaDynMoveTransitionUnused)		);
	}
	fprintf(file, "Moves:               %u total, %u used, %u unused\n",	uiDynMoveCount,				eaSize(&eaDynMoveUsed),					eaSize(&eaDynMoveUnused)			);
	fprintf(file, "Animation Tracks:    %u total, %u used, %u unused\n",	uiDynAnimTrackCount,		eaSize(&eaDynAnimTrackHeaderUsed),		eaSize(&eaDynAnimTrackHeaderUnused)	);

	fprintf(file, "\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "Used Assets\n");
	fprintf(file, "==============================================================\n");
	if (bPrettyPrint) {
		fprintf(file, "(# of times used) data name : file name\n");
	} else {
		fprintf(file,"# of times used TAB data name TAB file name\n");
	}

	if (bAssumeCostumes) {
		ReportAnimationAssets_PrintSpecialCostumeUICSkel(file,								bPrettyPrint);
		ReportAnimationAssets_PrintUsedCSkels			(file, &eaCSkelUsed,				bPrettyPrint);
		ReportAnimationAssets_PrintUsedCGeos			(file, &eaCGeoUsed,					bPrettyPrint);
	}
	if (!bAssumeCharts && !bAssumeMoves)
		ReportAnimationAssets_PrintUsedSkelInfos		(file, &eaSkelInfoUsed,				bPrettyPrint);
	if (!bAssumeMoves) {
		ReportAnimationAssets_PrintUsedCharts			(file, &eaDynAnimChartLoadTimeUsed,	bPrettyPrint);
		ReportAnimationAssets_PrintUsedTemplates		(file, &eaDynAnimTemplateUsed,		bPrettyPrint);
		ReportAnimationAssets_PrintUsedGraphs			(file, &eaDynAnimGraphUsed,			bPrettyPrint);
		ReportAnimationAssets_PrintUsedMoveTransitions	(file, &eaDynMoveTransitionUsed,	bPrettyPrint);
	}
	ReportAnimationAssets_PrintUsedMoves			(file, &eaDynMoveUsed,				bPrettyPrint);
	ReportAnimationAssets_PrintUsedAnimTracks		(file, &eaDynAnimTrackHeaderUsed,	bPrettyPrint);

	eaDestroy(&eaCGeoUsed);
	eaDestroy(&eaCSkelUsed);
	eaDestroy(&eaSkelInfoUsed);
	eaDestroy(&eaDynAnimChartLoadTimeUsed);
	eaDestroy(&eaDynAnimTemplateUsed);
	eaDestroy(&eaDynAnimGraphUsed);
	eaDestroy(&eaDynMoveTransitionUsed);
	eaDestroy(&eaDynMoveUsed);
	eaDestroy(&eaDynAnimTrackHeaderUsed);

	fprintf(file, "\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "Unused Assets\n");
	fprintf(file, "==============================================================\n");
	if (bPrettyPrint) {
		fprintf(file, "data name : file name\n");
	} else {
		fprintf(file, "data name TAB file name\n");
	}

	if (bAssumeCostumes) {
		ReportAnimationAssets_PrintUnusedCSkels			(file, &eaCSkelUnused,	bPrettyPrint);
		ReportAnimationAssets_PrintUnusedCGeos			(file, &eaCGeoUnused,	bPrettyPrint);
	}
	if (!bAssumeCharts && !bAssumeMoves)
		ReportAnimationAssets_PrintUnusedSkelInfos		(file, &eaSkelInfoUnused, bPrettyPrint);
	if (!bAssumeMoves) {
		ReportAnimationAssets_PrintUnusedCharts			(file, &eaDynAnimChartLoadTimeUnused,	bPrettyPrint);
		ReportAnimationAssets_PrintUnusedTemplates		(file, &eaDynAnimTemplateUnused,		bPrettyPrint);
		ReportAnimationAssets_PrintUnusedGraphs			(file, &eaDynAnimGraphUnused,			bPrettyPrint);
		ReportAnimationAssets_PrintUnusedMoveTransitions(file, &eaDynMoveTransitionUnused,		bPrettyPrint);
	}
	ReportAnimationAssets_PrintUnusedMoves			(file, &eaDynMoveUnused,			bPrettyPrint);
	ReportAnimationAssets_PrintUnusedAnimTracks		(file, &eaDynAnimTrackHeaderUnused,	bPrettyPrint);

	eaDestroy(&eaCGeoUnused);
	eaDestroy(&eaCSkelUnused);
	eaDestroy(&eaSkelInfoUnused);
	eaDestroy(&eaDynAnimChartLoadTimeUnused);
	eaDestroy(&eaDynAnimTemplateUnused);
	eaDestroy(&eaDynAnimGraphUnused);
	eaDestroy(&eaDynMoveTransitionUnused);
	eaDestroy(&eaDynMoveUnused);
	eaDestroy(&eaDynAnimTrackHeaderUnused);

	fclose(file);

	return "Animation asset report written to 'c:\\AnimationAssetReport.txt'\n";
}

// +-----------------------------+
// | Report Audio Assets Command |
// +-----------------------------+

static void ReportAudioAssets_GetDefinedEvents(const char ***peaDefinedEvents)
{
	char **eaEvents = NULL;

	sndGetEventList(&eaEvents, NULL);

	FOR_EACH_IN_EARRAY(eaEvents, char, pEvent) {
		eaPushUnique(peaDefinedEvents, allocAddString(pEvent));
	} FOR_EACH_END;
}

static void ReportAudioAssets_AddAssets(const char ***peaAudioAssets, const char **eaAssetsToAdd)
{
	FOR_EACH_IN_EARRAY(eaAssetsToAdd, const char, pAsset) {
		eaPushUnique(peaAudioAssets, allocAddString(pAsset));
	} FOR_EACH_END;
}

static bool ReportAudioAssets_CheckEventForSound(const char *pcEvent, const char *pcSound)
{
	char pcEventCopy[1024];
	strcpy(pcEventCopy, pcEvent);
	while (strlen(pcEventCopy))
	{
		if (strcmpi(pcEventCopy, pcSound) == 0) {
			return true;
		} else {
			char *pcCopyFrom = strstr(pcEventCopy,"/");
			if (pcCopyFrom) {
				pcCopyFrom++;
				strcpy(pcEventCopy, pcCopyFrom);
			} else {
				return false;
			}
		}
	}
	return false;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE;
char *ReportAudioAssets_ProcessOnClient(AudioAssets *pServerAudioAssets)
{
	FILE *file;	
	const char **eaDefinedEvents = NULL;
	const char **eaAudioAssets   = NULL;
	const char **eaMissingEvents = NULL;
	const char **eaUnusedEvents  = NULL;
	const char **eaUsedEvents    = NULL;
	AudioAssets clientAudioAssets;
	AudioAssetComponents *pComponent;

	file = fopen("c:\\AudioAssetReport.txt", "w");
	if (!file) {
		return "Can't open file 'c:\\AudioAssetReport.txt'.\n";
	}

	ZeroStruct(&clientAudioAssets);

	ReportAudioAssets_GetDefinedEvents(&eaDefinedEvents);

	pComponent = StructCreate(parse_AudioAssetComponents);
	dynFxInfo_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&clientAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	gclNotify_NotifyAction_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&clientAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	gclNotify_NotifyAudioEvent_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&clientAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	ui_GenGetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&clientAudioAssets.eaComponents, pComponent);

	FOR_EACH_IN_EARRAY(clientAudioAssets.eaComponents, AudioAssetComponents, pAddComponent) {
		ReportAudioAssets_AddAssets(&eaAudioAssets, pAddComponent->eaStrings);
	} FOR_EACH_END;
	FOR_EACH_IN_EARRAY(pServerAudioAssets->eaComponents, AudioAssetComponents, pAddComponent) {
		ReportAudioAssets_AddAssets(&eaAudioAssets, pAddComponent->eaStrings);
	} FOR_EACH_END;
	eaQSort(eaAudioAssets, strCmp);

	FOR_EACH_IN_EARRAY(eaDefinedEvents, const char, pcEvent) {
		bool bFound = false;
		FOR_EACH_IN_EARRAY(eaAudioAssets, const char, pcSound) {
			if (bFound = ReportAudioAssets_CheckEventForSound(pcEvent, pcSound)) {
				break;
			}
		} FOR_EACH_END;
		if (!bFound) {
			eaPushUnique(&eaUnusedEvents, pcEvent);
		} else {
			eaPushUnique(&eaUsedEvents, pcEvent);
		}
	} FOR_EACH_END;
	eaQSort(eaUnusedEvents, strCmp);

	FOR_EACH_IN_EARRAY(eaAudioAssets, const char, pcSound) {
		bool bFound = false;
		FOR_EACH_IN_EARRAY(eaDefinedEvents, const char, pcEvent) {
			if (bFound = ReportAudioAssets_CheckEventForSound(pcEvent, pcSound)) {
				break;
			}
		} FOR_EACH_END;
		if (!bFound) {
			eaPushUnique(&eaMissingEvents, pcSound);
		}
	} FOR_EACH_END;
	eaQSort(eaMissingEvents, strCmp);

	fprintf(file, "=== BASIC DATA ===\n");
	fprintf(file, "%u defined audio events\n", eaSize(&eaDefinedEvents));
	fprintf(file, "%u referenced audio events\n", eaSize(&eaAudioAssets));
	fprintf(file, "%u used audio events (defined events that occur in data listed by file type counts)\n", eaSize(&eaUsedEvents));
	fprintf(file, "%u unused audio events (defined events that do NOT occur in data listed by file type counts)\n", eaSize(&eaUnusedEvents));
	fprintf(file, "%u missing audio events (events that occur in data listed by file type counts but are NOT defined)\n", eaSize(&eaMissingEvents));

	fprintf(file, "\n=== FILE TYPE COUNTS ===\n");
	FOR_EACH_IN_EARRAY_FORWARDS(clientAudioAssets.eaComponents, AudioAssetComponents, pDisplayComponent) {
		fprintf(file, "CLIENT: %s : %u total : %u with audio\n", pDisplayComponent->pcType,	pDisplayComponent->uiNumData, pDisplayComponent->uiNumDataWithAudio);
	} FOR_EACH_END;
	FOR_EACH_IN_EARRAY_FORWARDS(pServerAudioAssets->eaComponents, AudioAssetComponents, pDisplayComponent) {
		fprintf(file, "SERVER: %s : %u total : %u with audio\n", pDisplayComponent->pcType,	pDisplayComponent->uiNumData, pDisplayComponent->uiNumDataWithAudio);
	} FOR_EACH_END;

	fprintf(file,"\n=== MISSING AUDIO EVENTS ===\n");
	FOR_EACH_IN_EARRAY_FORWARDS(eaMissingEvents, const char, pcSound) {
		fprintf(file, "missing audio event: %s\n", pcSound);
	} FOR_EACH_END;

	fprintf(file,"\n=== UNUSED AUDIO EVENTS ===\n");
	FOR_EACH_IN_EARRAY_FORWARDS(eaUnusedEvents, const char, pcSound) {
		fprintf(file, "unused audio event: %s\n", pcSound);
	} FOR_EACH_END;

	fprintf(file,"\n=== UTILIZED AUDIO EVENTS ===\n");
	FOR_EACH_IN_EARRAY_FORWARDS(eaAudioAssets, const char, pcSound) {
		fprintf(file, "utilized audio event: %s\n", pcSound);
	} FOR_EACH_END;

	fclose(file);

	if (eaDefinedEvents) eaDestroy(&eaDefinedEvents);
	if (eaAudioAssets  ) eaDestroy(&eaAudioAssets);
	if (eaMissingEvents) eaDestroy(&eaMissingEvents);
	if (eaUnusedEvents ) eaDestroy(&eaUnusedEvents);

	FOR_EACH_IN_EARRAY(clientAudioAssets.eaComponents, AudioAssetComponents, pDestroyComponent) {
		if (pDestroyComponent->pcType   ) free(pDestroyComponent->pcType);
		if (pDestroyComponent->eaStrings) eaDestroyEx(&pDestroyComponent->eaStrings, NULL);
	} FOR_EACH_END;
	eaDestroy(&clientAudioAssets.eaComponents);

	return "Audio asset report written to 'c:\\AudioAssetReport.txt'\n";
}

AUTO_COMMAND ACMD_CLIENTONLY;
char *ReportAudioAssets()
{
	ServerCmd_ReportAudioAssets_ProcessOnServer(entPlayerRef(0));
	return "Generating audio asset report (requires a server connection and can take several minutes to finish)\n";
}


//
// Costume System Commands
//

static int CompareClientGraphicsTextureAsset(const ClientGraphicsTextureAsset** left, const ClientGraphicsTextureAsset** right)
{
	return stricmp((*left)->pcFilename,(*right)->pcFilename);
}

static int CompareClientGraphicsMaterialAsset(const ClientGraphicsMaterialAsset** left, const ClientGraphicsMaterialAsset** right)
{
	return stricmp((*left)->pcFilename,(*right)->pcFilename);
}

AUTO_COMMAND ACMD_CLIENTCMD;
void CostumeAssetReportForMaterialsAndTextures(char *pcFilename, ClientGraphicsLookupRequest *pRequest)
{
	FILE *file;
	int i;
	char buf[300];

	// Get texture file names
	for(i=0; i<eaSize(&pRequest->eaTextures); ++i) {
		BasicTexture *pBasicTex = texFind(pRequest->eaTextures[i]->pcTextureName, true);
		if (pBasicTex) {
			texFindFullName(pBasicTex,buf,300);
			pRequest->eaTextures[i]->pcFilename = StructAllocString(buf);
		} else if (stricmp(pRequest->eaTextures[i]->pcTextureName, "None") == 0) {
			pRequest->eaTextures[i]->pcFilename = StructAllocString("NONE");
		} else {
			pRequest->eaTextures[i]->pcFilename = StructAllocString("UNKNOWN");
		}
	}
	
	// Sort textures on file name
	eaQSort(pRequest->eaTextures, CompareClientGraphicsTextureAsset);

	// Get material file names
	for(i=0; i<eaSize(&pRequest->eaMaterials); ++i) {
		ClientGraphicsMaterialAsset *pAsset = pRequest->eaMaterials[i];
		Material *pMaterial = materialFindNoDefault(pAsset->pcMaterialName, 0);
		const MaterialData *pData;
		if (pMaterial) {
			pData = materialGetData(pMaterial);
			if (pData) {
				pAsset->pcFilename = StructAllocString(pData->filename);
			} else if (stricmp(pAsset->pcMaterialName, "None") == 0) {
				pAsset->pcFilename = StructAllocString("NONE");
			} else {
				pAsset->pcFilename = StructAllocString("UNKNOWN");
			}
		} else {
			pAsset->pcFilename = StructAllocString("UNKNOWN");
		}
	}

	// Sort materials on file name
	eaQSort(pRequest->eaMaterials, CompareClientGraphicsMaterialAsset);

	// Append to requested file
	file = fopen(pcFilename, "a");

	if (eaSize(&pRequest->eaMaterials) > 0) {
		fprintf(file, "---- Used Materials ----\n");
		fprintf(file, "\nThe following Materials are referenced by a Costume Material.\n");
		fprintf(file, "The count indicates how many times the material is referenced.\n\n");
		for(i=0; i<eaSize(&pRequest->eaMaterials); ++i) {
			fprintf(file, "  (%3d) %s (%s)\n", pRequest->eaMaterials[i]->count, pRequest->eaMaterials[i]->pcFilename, pRequest->eaMaterials[i]->pcMaterialName);
		}
		fprintf(file, "\n");
	}

	if (eaSize(&pRequest->eaTextures) > 0) {
		fprintf(file, "---- Used Textures ----\n");
		fprintf(file, "\nThe following Textures are referenced by a Costume Texture or by a Material used by a Costume Material.\n");
		fprintf(file, "The count indicates how many times the texture is referenced.\n\n");
		for(i=0; i<eaSize(&pRequest->eaTextures); ++i) {
			fprintf(file, "  (%3d) %s (%s)\n", pRequest->eaTextures[i]->count, pRequest->eaTextures[i]->pcFilename, pRequest->eaTextures[i]->pcTextureName);
		}
		fprintf(file, "\n");
	}

	fclose(file);
}

#include "cmdClientReport_h_ast.c"
#include "cmdClientReport_c_ast.c"

