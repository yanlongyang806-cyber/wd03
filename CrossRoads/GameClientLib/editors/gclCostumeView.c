//
// CostumeView.c
//

#include "textparser.h"
#include "ClientTargeting.h"
#include "CostumeCommonGenerate.h"
#include "gclCostumeView.h"
#include "dynFxInterface.h"
#include "dynCloth.h"
#include "DynFxManager.h"
#include "EditLib.h"
#include "GameClientLib.h"
#include "GfxPrimitive.h"
#include "GfxSpriteText.h"
#include "GraphicsLib.h"
#include "Quat.h"
#include "mathutil.h"
#include "partition_enums.h"
#include "WorldGrid.h"
#include "CBox.h"
#include "dynFxInfo.h"
#include "StringCache.h"
#include "Expression.h"
#include "AutoGen/dynFxInfo_h_ast.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


extern ParseTable parse_DynDefineParam[];
#define TYPE_parse_DynDefineParam DynDefineParam

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

//
// The editor requires access to the camera
//
static GfxCameraController *gCamera;


// The costume editor runs in a separate graphics context.  It requires a root node.

static DynNode *gRootNode;

AUTO_STRUCT;
typedef struct CostumeCameraSettings
{
	Vec3 vDesiredOffset;
	F32 f2TanFOVy;
	F32 f2TanFOVx;
	F32 fCamDistMin;			AST(DEFAULT(-1))
	F32	fCamDistMax;			AST(DEFAULT(10))
	F32	fAspect;				AST(DEFAULT(1))
	F32 fFOV;					AST(DEFAULT(-1))
	F32 fCurrentCostumeHeight;
	F32 fCurrentTargetHeight;
	F32 fCameraSpeed;			AST(DEFAULT(3))
	F32 fMaxCameraSpeed;		AST(DEFAULT(5))
	U8 bResetCamera : 1;
	U8 bFOVSet : 1;
	U8 bUseHorizontalFOV : 1;
} CostumeCameraSettings;

extern ParseTable parse_CostumeCameraSettings[];
#define TYPE_parse_CostumeCameraSettings CostumeCameraSettings

static CostumeCameraSettings gCostumeCameraSettings = {0};

AUTO_STRUCT;
typedef struct WeaponFXRef {
	REF_TO(DynFx) hFX;
} WeaponFXRef;

extern ParseTable parse_WeaponFXRef[];
#define TYPE_parse_WeaponFXRef WeaponFXRef

AUTO_FIXUPFUNC;
TextParserResult WeaponFXRefFixup(WeaponFXRef *pWeaponFX, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (GET_REF(pWeaponFX->hFX)) {
			dynFxKill(GET_REF(pWeaponFX->hFX), true, true, true, eDynFxKillReason_ManualKill);
			REMOVE_HANDLE(pWeaponFX->hFX);
		}
		break;
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void costumeView_InitCameraSettings(void)
{
	StructReset(parse_CostumeCameraSettings, &gCostumeCameraSettings);
}

//-----------------------------------------------------------------------------------
// Graphics functions
//-----------------------------------------------------------------------------------

void costumeView_InitGraphics(void)
{
	if (!gRootNode) {
		gRootNode = dynNodeAlloc();
		assert(gRootNode);
	}

	if (!gCamera->inited) {
		gCamera->camdist = 10.0;
		zeroVec3(gCamera->camcenter);
		zeroVec3(gCamera->campyr);
		gCamera->inited = true;
	}
}

void costumeView_UpdateWeaponEffects(CostumeViewCostume* pGraphics, PlayerCostume *pCostume)
{
	TailorWeaponStance *pStance = GET_REF(pGraphics->hWeaponStance);
	if (pStance)
	{
		PCBoneDef *pBoneDef = GET_REF(pStance->hBoneDef);
		PCPart *pPart = CONTAINER_RECONST(PCPart, costumeTailor_GetPartByBone(CONTAINER_NOCONST(PlayerCostume, pCostume), pBoneDef, NULL));
		// NULL is a valid return from costumeTailor_GetPartByBone therefore it needs to be checked
		if(pPart)
		{
			const char* pchGeoName = REF_STRING_FROM_HANDLE(pPart->hGeoDef);
			const char* pchModelName = GET_REF(pPart->hGeoDef) ? GET_REF(pPart->hGeoDef)->pcModel : NULL;
			DynDefineParam *pParam;
			int iParamIndex = 0;
			int i;
			bool bCustomColor = false;

			Vec4 vec4;

			if (!pchGeoName)
				return;

			bCustomColor = pPart->eColorLink == kPCColorLink_None;

			if (pGraphics->pWeaponFxParams) {
				eaClearStruct(&pGraphics->pWeaponFxParams->eaDefineParams, parse_DynDefineParam);
			} else {
				pGraphics->pWeaponFxParams = dynParamBlockCreate();
			}

			pParam = eaGetStruct(&pGraphics->pWeaponFxParams->eaDefineParams, parse_DynDefineParam, iParamIndex++);
			pParam->pcParamName = allocAddString("Colorparam");
			copyVec3(pPart->color0, vec4);
			MultiValSetVec3(&pParam->mvVal, bCustomColor ? (Vec3*)&vec4 : &pStance->vColor0);

			pParam = eaGetStruct(&pGraphics->pWeaponFxParams->eaDefineParams, parse_DynDefineParam, iParamIndex++);
			pParam->pcParamName = allocAddString("Color1param");
			copyVec4(pPart->color1, vec4);
			MultiValSetVec4(&pParam->mvVal, bCustomColor ? &vec4 : &pStance->vColor1);

			pParam = eaGetStruct(&pGraphics->pWeaponFxParams->eaDefineParams, parse_DynDefineParam, iParamIndex++);
			pParam->pcParamName = allocAddString("Color2param");
			copyVec4(pPart->color2, vec4);
			MultiValSetVec4(&pParam->mvVal, bCustomColor ? &vec4 : &pStance->vColor2);

			pParam = eaGetStruct(&pGraphics->pWeaponFxParams->eaDefineParams, parse_DynDefineParam, iParamIndex++);
			pParam->pcParamName = allocAddString("Color3param");
			copyVec4(pPart->color3, vec4);
			MultiValSetVec4(&pParam->mvVal, bCustomColor ? &vec4 : &pStance->vColor3);

			pParam = eaGetStruct(&pGraphics->pWeaponFxParams->eaDefineParams, parse_DynDefineParam, iParamIndex++);
			pParam->pcParamName = allocAddString("GeometryParam");
			MultiValSetString(&pParam->mvVal, (stricmp(pchGeoName, "None")==0 || !pchModelName || !*pchModelName) ?  allocAddString(pStance->pchDefaultGeo) : allocAddString(pchModelName));

			//pParam = eaGetStruct(&pWeaponFxParams->eaDefineParams, parse_DynDefineParam, iParamIndex++);
			//pParam->pcParamName = allocAddString("AtParam");
			//MultiValSetString(&pParam->mvVal, allocAddString("WepR"));

			// Go through all the params, evaluate them, and if they work out, add them to the block
			for(i=eaSize(&pStance->eaParams)-1; i>=0; i--)
			{
				static ExprContext *s_pContext = NULL;
				if (!s_pContext)
				{
					s_pContext = exprContextCreate();
					exprContextSetFuncTable(s_pContext, exprContextCreateFunctionTable());
				}
				pParam = eaGetStruct(&pGraphics->pWeaponFxParams->eaDefineParams, parse_DynDefineParam, iParamIndex++);
				exprGenerate(pStance->eaParams[i]->expr,s_pContext);
				exprEvaluateDeepCopyAnswer(pStance->eaParams[i]->expr,s_pContext,&pParam->mvVal);
				pParam->pcParamName = pStance->eaParams[i]->cpchParam;

				// Early fixup/validation
				if(pParam->mvVal.type==MULTI_INT)
				{
					// Convert Int to Float
					MultiValSetFloat(&pParam->mvVal,MultiValGetFloat(&pParam->mvVal,NULL));
				}
				else if(pParam->mvVal.type==MULTI_STRING_F && (!pParam->mvVal.str || !pParam->mvVal.str[0]))
				{
					// String doesn't have valid data
					MultiValClear(&pParam->mvVal);
					continue;
				}

				if(pParam->mvVal.type==pStance->eaParams[i]->eType)
				{
					if(pParam->mvVal.type == MULTIOP_LOC_MAT4_F)
					{
						Vec3 v;
						copyVec3(pParam->mvVal.vecptr[3],v);
						MultiValSetVec3(&pParam->mvVal, &v);
					}
					pParam->pcParamName = pStance->eaParams[i]->cpchParam;
				}
				else if(pParam->mvVal.type==kPowerFXParamType_VEC && pStance->eaParams[i]->eType==kPowerFXParamType_VC4)
				{
					// Automatically convert a Vec3 to a Vec4 with 0 w
					Vec4 v;
					copyVec3(pParam->mvVal.vecptr[3],v);
					v[3] = 0;
					MultiValSetVec4(&pParam->mvVal, &v);
					pParam->pcParamName = pStance->eaParams[i]->cpchParam;
				}
			}
		}
	}
	else
	{
		if (pGraphics->pWeaponFxParams) {
			dynParamBlockFree(pGraphics->pWeaponFxParams);
			pGraphics->pWeaponFxParams = NULL;
		}
		eaDestroyStruct(&pGraphics->eaWeaponFx, parse_WeaponFXRef);
	}
}


// This procedure re-creates the skeleton and should be used after any change to the
// costume structure to make the change take effect
void costumeView_ReinitDrawSkeleton(CostumeViewCostume* pGraphics)
{
	const DynBaseSkeleton* pOldSkeleton = pGraphics->pSkel?GET_REF(pGraphics->pSkel->hBaseSkeleton):NULL;
	WLCostume* pCostume = GET_REF(pGraphics->hWLCostume);
	SkelInfo* pSkelInfo = pCostume?GET_REF(pCostume->hSkelInfo):NULL;
	const DynBaseSkeleton* pNewSkeleton = pSkelInfo?GET_REF(pSkelInfo->hBaseSkeleton):NULL;
	bool bKeepOldSequencers = pNewSkeleton && pOldSkeleton && (pNewSkeleton == pOldSkeleton && !pGraphics->bNeverKeepSequencers);
	bool bKeepSkeleton; // = bKeepOldSequencers;
	DynSequencer** eaSqr = NULL;
	DynAnimGraphUpdater       **swapAGUpdater = NULL;
	const DynAnimChartRunTime **swapMovementCharts = NULL;
	const char                **swapMovementStances = NULL;
	U32                         swapMovementIntStanceCount = 0;
	U32                         swapMovementDirStanceCount = 0;
	bool						swapMovementDirty = false;
	DynMovementBlock          **swapMovementBlock = NULL;
	const char                 *swapMovementDebugOverrides = NULL;
	DynFxRef** eaDrawSkeletonFXRefs = NULL;
	DynClothObjectSavedState **eaClothStates = NULL;

	if (gConf.bNewAnimationSystem	&&
		bKeepOldSequencers			&&
		pGraphics->pSkel			&&
		pCostume)
	{
		bKeepOldSequencers &= dynSkeletonCostumeBitsMatch(pGraphics->pSkel, pCostume, pGraphics->pcAnimStanceWords);
	}

	bKeepSkeleton = bKeepOldSequencers;

	// If the number or type of sub skeletons (tails, wings) has changed, we must reset the sequencers and recreate the skeletons
	if (pCostume && pGraphics->pSkel)
	{
		if (eaSize(&pCostume->eaSubCostumes) != eaSize(&pGraphics->pSkel->eaDependentSkeletons))
			bKeepSkeleton = false;
		else
		{
			FOR_EACH_IN_EARRAY(pGraphics->pSkel->eaDependentSkeletons, DynSkeleton, pDependentSkeleton)
			{
				if (GET_REF(pDependentSkeleton->hCostume) != GET_REF(pCostume->eaSubCostumes[ipDependentSkeletonIndex]->hSubCostume))
				{
					bKeepSkeleton = false;
					break;
				}
			}
			FOR_EACH_END;
		}
	}

	// Recreate the skeletons
	if (pGraphics->pDrawSkel)
	{
		if (pCostume && bKeepSkeleton)
		{
			eaDrawSkeletonFXRefs = pGraphics->pDrawSkel->eaDynFxRefs;
			pGraphics->pDrawSkel->eaDynFxRefs = NULL;
		}
	
		dynClothObjectSaveAllStates(pGraphics->pDrawSkel, &eaClothStates);
		dynDrawSkeletonFree(pGraphics->pDrawSkel);
	}

	if (pGraphics->bResetFXManager) {
		if(pGraphics->pFxManager) {
			dynFxManDestroy(pGraphics->pFxManager);
			pGraphics->pFxManager = NULL;
		}
		pGraphics->bResetFXManager = false;
	}

	if (!bKeepSkeleton && pGraphics->pSkel)
	{
		if (bKeepOldSequencers)
		{
			if (!gConf.bNewAnimationSystem)
			{
				eaSqr = pGraphics->pSkel->eaSqr;
				pGraphics->pSkel->eaSqr = NULL;
			}
			else
			{
				swapAGUpdater = pGraphics->pSkel->eaAGUpdaterMutable;
				swapMovementCharts         = pGraphics->pSkel->movement.chartStack.eaChartStackMutable;
				swapMovementStances        = pGraphics->pSkel->movement.chartStack.eaStanceWordsMutable;
				swapMovementIntStanceCount = pGraphics->pSkel->movement.chartStack.interruptingMovementStanceCount;
				swapMovementDirStanceCount = pGraphics->pSkel->movement.chartStack.directionMovementStanceCount;
				swapMovementDirty          = pGraphics->pSkel->movement.chartStack.bStackDirty;
				swapMovementBlock          = pGraphics->pSkel->movement.eaBlocks;
				swapMovementDebugOverrides = pGraphics->pSkel->movement.pcDebugMoveOverride;
				pGraphics->pSkel->eaAGUpdaterMutable = NULL;
				pGraphics->pSkel->movement.eaBlocks = NULL;
				pGraphics->pSkel->movement.pcDebugMoveOverride = NULL;
				pGraphics->pSkel->movement.chartStack.eaChartStackMutable  = NULL;
				pGraphics->pSkel->movement.chartStack.eaStanceWordsMutable = NULL;
			}
		}

		if(pGraphics->pFxManager) {
			dynFxManDestroy(pGraphics->pFxManager);
			pGraphics->pFxManager = NULL;
		}

		dynSkeletonFree(pGraphics->pSkel);
		pGraphics->pSkel = NULL;
	}

	if (pCostume)
	{
		TailorWeaponStance *pStance = GET_REF(pGraphics->hWeaponStance);
		if (!pGraphics->pSkel)
			pGraphics->pSkel = dynSkeletonCreate(pCostume, false, true, bKeepOldSequencers, false, false, NULL);
		else
			dynSkeletonReprocess(pGraphics->pSkel, pCostume);

		assertmsgf(pGraphics->pSkel, "Skeleton referenced by costume \"%s\" doesn't exist on the client. The skeleton may be incorrectly tagged as non player-initial, or may have been deleted.", pCostume->pcName);
		// Create FX manager if it doesn't exist
		if (pCostume && !pGraphics->pFxManager)
		{
			pGraphics->pFxManager = dynFxManCreate(pGraphics->pSkel->pRoot, pGraphics->pFxRegion ? pGraphics->pFxRegion : worldGetEditorWorldRegion(), NULL, eFxManagerType_Tailor, 0, PARTITION_CLIENT, false, false);
		}

		if(pGraphics->pWeaponFxParams && pStance)
		{
			DynAddFxParams params = {0};
			int iFX, iParam;
			params.pSourceRoot = pGraphics->pSkel->pRoot;
			params.eSource = eDynFxSource_Power;
			eaClearStruct(&pGraphics->eaWeaponFx, parse_WeaponFXRef);
			for (iFX = 0; iFX < eaSize(&pStance->pchStanceStickyFX); iFX++) {
				DynFx *pDynFx;

				// Copy the param block
				params.pParamBlock = dynParamBlockCreate();
				for (iParam = 0; iParam < eaSize(&pGraphics->pWeaponFxParams->eaDefineParams); iParam++) {
					DynDefineParam *pDst = eaGetStruct(&params.pParamBlock->eaDefineParams, parse_DynDefineParam, iParam);
					DynDefineParam *pSrc = pGraphics->pWeaponFxParams->eaDefineParams[iParam];
					pDst->iLineNum = pSrc->iLineNum;
					MultiValCopy(&pDst->mvVal, &pSrc->mvVal);
					pDst->pcParamName = pSrc->pcParamName;
				}

				// Add the Fx
				pDynFx = dynAddFx(pGraphics->pFxManager, pStance->pchStanceStickyFX[iFX], &params);
				if (pDynFx) {
					WeaponFXRef *pRef = StructCreate(parse_WeaponFXRef);
					ADD_SIMPLE_POINTER_REFERENCE_DYN(pRef->hFX, pDynFx);
					eaPush(&pGraphics->eaWeaponFx, pRef);
				}
			}
		}

		if (!bKeepSkeleton && bKeepOldSequencers)
		{
			if (!gConf.bNewAnimationSystem)
			{
				pGraphics->pSkel->eaSqr = eaSqr;
			}
			else
			{
				pGraphics->pSkel->eaAGUpdaterMutable	= swapAGUpdater;
				pGraphics->pSkel->movement.chartStack.eaChartStackMutable	= swapMovementCharts;
				pGraphics->pSkel->movement.chartStack.eaStanceWordsMutable	= swapMovementStances;
				pGraphics->pSkel->movement.chartStack.interruptingMovementStanceCount	= swapMovementIntStanceCount;
				pGraphics->pSkel->movement.chartStack.directionMovementStanceCount		= swapMovementDirStanceCount;
				pGraphics->pSkel->movement.chartStack.bStackDirty	= swapMovementDirty;
				pGraphics->pSkel->movement.eaBlocks				= swapMovementBlock;
				pGraphics->pSkel->movement.pcDebugMoveOverride	= swapMovementDebugOverrides;

				//we want to do this here so the stances for the skeleton will match the chart stacks
				dynSkeletonApplyCostumeBits(pGraphics->pSkel, pCostume);
			}
		}

		pGraphics->pDrawSkel = dynDrawSkeletonCreate(pGraphics->pSkel, pCostume, pGraphics->pFxManager, 0, true, true);

		//Make sure we properly re-apply our stancewords
		if (gConf.bNewAnimationSystem	&&
			pGraphics->pcAnimStanceWords)
		{
			char buf[1024];
			char *s;
			char *context=NULL;	
			strcpy(buf, pGraphics->pcAnimStanceWords);
			s = strtok_s(buf, " ", &context);
			while (s)
			{
				dynSkeletonSetCostumeStanceWord(pGraphics->pSkel, allocAddString(s));
				s = strtok_s(NULL, " ", &context);
			}
			pGraphics->pcLastAnimStanceWords = pGraphics->pcAnimStanceWords;
		}
		else
		{
			pGraphics->pcLastAnimStanceWords = NULL;
		}
		
		if (eaDrawSkeletonFXRefs)
		{
			if (!pGraphics->pDrawSkel->eaDynFxRefs)
				pGraphics->pDrawSkel->eaDynFxRefs = eaDrawSkeletonFXRefs;
			else
			{
				eaPushEArray(&pGraphics->pDrawSkel->eaDynFxRefs, &eaDrawSkeletonFXRefs);
				eaDestroy(&eaDrawSkeletonFXRefs);
			}
		}

		dynClothObjectApplyAllStates(pGraphics->pDrawSkel, &eaClothStates);

		// Put in animation bits
		if (!gConf.bNewAnimationSystem)
		{
			dynSeqPushBitFieldFeed(pGraphics->pSkel, &pGraphics->costumeBFG);
		}
		pGraphics->bResetFX = true;
	}

	// If, for some reason old cloth states were not applied, clean up
	// saved cloth states.
	if(eaSize(&eaClothStates))
		eaDestroyEx(&eaClothStates, dynClothObjectDestroySavedState);
}


void costumeView_SetCamera(GfxCameraController *pCamera)
{
	gCamera = pCamera;
}


GfxCameraController *costumeView_GetCamera(void)
{
	return gCamera;
}

DynNode *costumeView_GetRootNode(void)
{
	return gRootNode;
}

void costumeView_DrawHeightRuler(F32 fHeight)
{
	int i;
	GfxCameraController *pCamera;
	int iLineCnt, iNumSubLines, fMarkerScale;
	F32 fWidth, fHeightStep;
	Vec3 v3Start, v3End;
	Color lineColor = {0};

	if(fHeight <= 0)
		fHeight = 10.0f;
	if(fHeight > 15.0f) {
		iLineCnt = (int)(fHeight/10.0f) + 2;
		fMarkerScale = 10;
		iNumSubLines = 10;
		fHeightStep = 10.0f / iNumSubLines;
		iLineCnt *= iNumSubLines;
		fWidth = 10.0f;
	} else {
		iLineCnt = (int)fHeight + 2;		
		fMarkerScale = 1;
		iNumSubLines = 12;
		fHeightStep = 1.0f / iNumSubLines;
		iLineCnt *= iNumSubLines;
		fWidth = 5.0f;
	}

	//Try to make the lines face the camera
	pCamera = costumeView_GetCamera();
	if(fabs(pCamera->last_camera_matrix[2][0]) > fabs(pCamera->last_camera_matrix[2][2])) {
		v3Start[0] = v3End[0] = 0;
		v3Start[2] = -fWidth;
		v3End[2] = fWidth;
	} else {
		v3Start[2] = v3End[2] = 0;
		v3Start[0] = -fWidth;
		v3End[0] = fWidth;
	}

	for ( i=0; i < iLineCnt+1; i++ ) {
		v3Start[1] = v3End[1] = i * fHeightStep;
		if(i%iNumSubLines) {
			lineColor.r = 0;
			lineColor.g = 0;
			lineColor.a = 100;
		} else {
			Vec2 screen_pos;
			gfxfont_SetColorRGBA(0xFFFF00FF, 0xFFFF00FF);
			editLibGetScreenPos(v3Start, screen_pos);
			gfxfont_Printf(screen_pos[0], screen_pos[1], 1, 1, 1, 0, "%d ft", i/iNumSubLines * fMarkerScale);
			editLibGetScreenPos(v3End, screen_pos);
			gfxfont_Printf(screen_pos[0], screen_pos[1], 1, 1, 1, 0, "%d ft", i/iNumSubLines * fMarkerScale);

			lineColor.r = 255;
			lineColor.g = 100;
			lineColor.a = 255;
		}
		gfxDrawLine3D(v3Start, v3End, lineColor);
	}
}

// This needs to be called per frame to properly draw the costume
void costumeView_DrawViewCostume(CostumeViewCostume *pViewCostume)
{
	TailorWeaponStance *pStance = GET_REF(pViewCostume->hWeaponStance);

	if (!gConf.bNewAnimationSystem)
	{
		char buf[260] = {0};

		if (pStance) {
			int i;
			for (i = 0; i < eaSize(&pStance->ppchStanceStickyBits); i++)
			{
				strcat(buf, pStance->ppchStanceStickyBits[i]);
				strcat(buf, " ");
			}
		}
		else if (pViewCostume->pcBits) {
			strcpy(buf, pViewCostume->pcBits);
		}
		else {
			strcpy(buf, "IDLE");
		}
		strcat(buf, " NOLOD");

		dynBitFieldGroupSetToMatchSentence(&pViewCostume->costumeBFG, buf);
	}
	else if (pViewCostume->pSkel)
	{
		if (pViewCostume->pcAnimStanceWords != pViewCostume->pcLastAnimStanceWords)
		{
			char buf[1024];
			char *s;
			char *context=NULL;
			if (pViewCostume->pcLastAnimStanceWords)
			{
				strcpy(buf, pViewCostume->pcLastAnimStanceWords);
				s = strtok_s(buf, " ", &context);
				while (s)
				{
					dynSkeletonClearCostumeStanceWord(pViewCostume->pSkel, allocAddString(s));
					s = strtok_s(NULL, " ", &context);
				}
			}
			if (pViewCostume->pcAnimStanceWords)
			{
				strcpy(buf, pViewCostume->pcAnimStanceWords);
				s = strtok_s(buf, " ", &context);
				while (s)
				{
					dynSkeletonSetCostumeStanceWord(pViewCostume->pSkel, allocAddString(s));
					s = strtok_s(NULL, " ", &context);
				}
			}
			pViewCostume->pcLastAnimStanceWords = pViewCostume->pcAnimStanceWords;
		}
		if (pViewCostume->bNeedsResetToDefault)
		{
			dynSkeletonResetToADefaultGraph(pViewCostume->pSkel, "costume change", 0, 1);
			pViewCostume->bNeedsResetToDefault = false;
		}
		if (pViewCostume->pcAnimKeyword &&
			eaSize(&pViewCostume->pSkel->eaAGUpdater))
		{
			if (dynAnimGraphUpdaterIsOnADefaultGraph(pViewCostume->pSkel->eaAGUpdater[0]) || dynAnimGraphUpdaterIsInPostIdle(pViewCostume->pSkel->eaAGUpdater[0])) {
				dynSkeletonStartGraph(pViewCostume->pSkel, allocAddString(pViewCostume->pcAnimKeyword), 0);
				pViewCostume->pcAnimKeyword = NULL;
			}
		}

		if (pViewCostume->pcAnimMove != pViewCostume->pcLastAnimMove)
		{
			pViewCostume->pSkel->movement.pcDebugMoveOverride = pViewCostume->pcAnimMove;
			pViewCostume->pcLastAnimMove = pViewCostume->pcAnimMove;
		}
	}

	// This happens per frame, reset costume here
	if( pViewCostume->bReset && GET_REF(pViewCostume->hWLCostume)) {
		costumeView_ReinitDrawSkeleton(pViewCostume);
		pViewCostume->bReset = 0;
	}
}

void costumeView_Draw(CostumeViewGraphics *pGraphics)
{
	S32 i;

	costumeView_DrawViewCostume(&pGraphics->costume);

	for (i = 0; i < eaSize(&pGraphics->eaExtraCostumes); i++) {
		costumeView_DrawViewCostume(pGraphics->eaExtraCostumes[i]);
	}
}

bool bDisableCameraSpaceCharacter;

// Turns off the camera space positioning of the character in the costume creator.
AUTO_CMD_INT(bDisableCameraSpaceCharacter, disableCameraSpaceCharacter) ACMD_CATEGORY(Debug);

void costumeView_DrawGhostsViewCostume(CostumeViewCostume *pViewCostume, bool bForceEditorRegion)
{
	if (!pViewCostume->pSkel) {
		// Can't draw anything useful if there's no skeleton
		return;
	}

	if (!pViewCostume->pCamOffset)
	{
		pViewCostume->pCamOffset = dynNodeAlloc();
		if (!pViewCostume->pCamOffset)
			return;
	}

	if( pViewCostume->bReset && GET_REF(pViewCostume->hWLCostume)) {
		costumeView_ReinitDrawSkeleton(pViewCostume);
		pViewCostume->bReset = 0;
	}

	dynNodeParent(pViewCostume->pCamOffset, gRootNode);
	dynNodeParent(pViewCostume->pSkel->pRoot, pViewCostume->pCamOffset);
	dynNodeSetPos(pViewCostume->pCamOffset, pViewCostume->v3SkelPos);
	dynNodeSetRot(pViewCostume->pCamOffset, pViewCostume->qSkelRot);

	if (pViewCostume->bPositionInCameraSpace && !bDisableCameraSpaceCharacter)
	{
		Mat4 mCameraMatrix, mSkelMatrix, mWorldMatrix;
		Vec3 vPYR;
		copyVec3(gCamera->campyr, vPYR);
		vPYR[0] = vPYR[2] = 0;
		createMat3YPR(mCameraMatrix, vPYR);
		copyVec3(gCamera->camcenter, mCameraMatrix[3]);
		dynNodeGetWorldSpaceMat(pViewCostume->pCamOffset, mSkelMatrix, false);
		mulMat4(mCameraMatrix, mSkelMatrix, mWorldMatrix);
		dynNodeSetFromMat4(pViewCostume->pCamOffset, mWorldMatrix);
	}

	if (dynDebugState.bDebugTailorSkeleton)
		dynDebugSetDebugSkeleton(pViewCostume->pDrawSkel);

	pViewCostume->pDrawSkel->bBodySock = 0;
	dynSkeletonUpdate(pViewCostume->pSkel, gGCLState.frameElapsedTime, NULL);
	if (pViewCostume->bResetAnimation){
		// Skip awkward tweens.  This has to be a separate Update call for unknown reasons.
		dynSkeletonUpdate(pViewCostume->pSkel, 2.0, NULL);
		pViewCostume->bResetAnimation = false;
	}

	if (pViewCostume->pFxManager)
	{
		dynFxManagerRemoveFromGrid(pViewCostume->pFxManager);

		if (pViewCostume->bResetFX) {
			dynFxManagerUpdate(pViewCostume->pFxManager, DYNFXTIME(1.0));
			pViewCostume->bResetFX = false;
		} else {
			dynFxManagerUpdate(pViewCostume->pFxManager, DYNFXTIME(gGCLState.frameElapsedTime));
		}
	}
	
	gfxQueueSingleDynDrawSkeleton(pViewCostume->pDrawSkel, bForceEditorRegion ? worldGetEditorWorldRegion() : NULL, true, true);
	FOR_EACH_IN_EARRAY(pViewCostume->pDrawSkel->eaSubDrawSkeletons, DynDrawSkeleton, pSub)
		gfxQueueSingleDynDrawSkeleton(pSub, bForceEditorRegion ? worldGetEditorWorldRegion() : NULL, false, true);
	FOR_EACH_END;
}

void costumeView_DrawGhosts(CostumeViewGraphics *pGraphics)
{
	S32 i;

	if(!pGraphics->bOverrideForceEditorRegion) {
		// Deal with overrides
		if (pGraphics->bOverrideTime) {
			gCamera->override_time = (pGraphics->fTime > 0);
			gCamera->time_override = pGraphics->fTime;
		}
		if (pGraphics->bOverrideSky) {
			gfxCameraControllerSetSkyOverride(gCamera, pGraphics->pcSkyOverride, __FILE__);
		}
	}

	costumeView_DrawGhostsViewCostume(&pGraphics->costume, pGraphics->bOverrideSky || pGraphics->bOverrideForceEditorRegion);

	for (i = 0; i < eaSize(&pGraphics->eaExtraCostumes); i++) {
		costumeView_DrawGhostsViewCostume(pGraphics->eaExtraCostumes[i], pGraphics->bOverrideSky || pGraphics->bOverrideForceEditorRegion);
	}
}


CostumeViewGraphics *costumeView_CreateGraphics(void)
{
	CostumeViewGraphics *pGraphics = calloc(1,sizeof(CostumeViewGraphics));
	return pGraphics;
}

CostumeViewCostume *costumeView_CreateViewCostume(CostumeViewGraphics *pGraphics)
{
	CostumeViewCostume *pViewCostume = pGraphics ? calloc(1,sizeof(CostumeViewCostume)) : NULL;
	if (pViewCostume)
	{
		Vec3 pyr = {0, 0, 0};
		PYRToQuat(pyr, pViewCostume->qSkelRot);
		eaPush(&pGraphics->eaExtraCostumes, pViewCostume);
	}
	return pViewCostume;
}

void costumeView_RegenViewCostume(CostumeViewCostume *pViewCostume, PlayerCostume *pCostume, const PCSlotType *pSlotType, PCMood *pMood, CharacterClass* pClass)
{
	SpeciesDef *pSpecies = pCostume ? GET_REF(pCostume->hSpecies) : NULL;
	WLCostume *pWLCostume;
	WLCostume** eaSubCostumes = NULL;
	const char *pchOldCostumeName = GET_REF(pViewCostume->hWLCostume)?GET_REF(pViewCostume->hWLCostume)->pcName:NULL;

	// Create the graphics layer costume and put it in the dictionary
	REMOVE_HANDLE(pViewCostume->hWLCostume);
	pWLCostume = (WLCostume*)costumeGenerate_CreateWLCostumeEx(pCostume, pSpecies, pClass, NULL, pSlotType, pMood, &g_CostumeEditState.eaFXArray, "CostumeEditor.", 0, 0, false, !pViewCostume->bIgnoreSkelFX, &eaSubCostumes);

	if (pWLCostume)
	{
		// First add sub costumes to dictionary and add references to the main costume
		FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume)
			wlCostumePushSubCostume(pSubCostume, pWLCostume);
		FOR_EACH_END;

		// Now add the main costume
		wlCostumeAddToDictionary(pWLCostume, pWLCostume->pcName);
	}

	SET_HANDLE_FROM_REFERENT("Costume", pWLCostume, pViewCostume->hWLCostume);

	// Force later regen of draw state
	pViewCostume->bReset = true;
	

	// If this is a different costume, reset any playing FX/Animations as well
	if (pchOldCostumeName && pWLCostume && pchOldCostumeName != pWLCostume->pcName)
	{
		costumeView_StopViewCostumeFx(pViewCostume);
		pViewCostume->bResetAnimation = true;
	}
	else
	{
		costumeView_UpdateWeaponEffects(pViewCostume, pCostume);
	}
}

void costumeView_RegenCostumeEx(CostumeViewGraphics *pGraphics, PlayerCostume *pCostume, const PCSlotType *pSlotType, PCMood *pMood, CharacterClass* pClass)
{
	// For backwards compatibility, regen the "primary" costume
	costumeView_RegenViewCostume(&pGraphics->costume, pCostume, pSlotType, pMood, pClass);
}

void costumeView_StopViewCostumeFx(CostumeViewCostume *pViewCostume)
{
	if (pViewCostume->pWeaponFxParams) {
		dynParamBlockFree(pViewCostume->pWeaponFxParams);
		pViewCostume->pWeaponFxParams = NULL;
	}
	eaClearStruct(&pViewCostume->eaWeaponFx, parse_WeaponFXRef);
	if (pViewCostume->pFxManager) {
		dynFxManDestroy(pViewCostume->pFxManager);
		pViewCostume->pFxManager = NULL;
	}
}

void costumeView_StopFx(CostumeViewGraphics *pGraphics)
{
	S32 i;

	costumeView_StopViewCostumeFx(&pGraphics->costume);

	for (i = eaSize(&pGraphics->eaExtraCostumes) - 1; i >= 0; i--) {
		costumeView_StopViewCostumeFx(pGraphics->eaExtraCostumes[i]);
	}
}

void costumeView_FreeViewCostume(CostumeViewCostume *pViewCostume, bool bFree)
{
	if (!pViewCostume)
		return;

	// Clean up graphics
	if (pViewCostume->pDrawSkel) {
		dynDrawSkeletonFree(pViewCostume->pDrawSkel);
		pViewCostume->pDrawSkel = NULL;
	}
	if (pViewCostume->pSkel) {
		dynSkeletonFree(pViewCostume->pSkel);
		pViewCostume->pSkel = NULL;
	}
	if (pViewCostume->pWeaponFxParams) {
		dynParamBlockFree(pViewCostume->pWeaponFxParams);
		pViewCostume->pWeaponFxParams = NULL;
	}
	if (pViewCostume->pFxManager) {
		dynFxManDestroy(pViewCostume->pFxManager);
		pViewCostume->pFxManager = NULL;
	}
	if (pViewCostume->pCamOffset) {
		dynNodeFree(pViewCostume->pCamOffset);
		pViewCostume->pCamOffset = NULL;
	}

	eaDestroy(&pViewCostume->eaWeaponFx);

	REMOVE_HANDLE(pViewCostume->hWLCostume);
	REMOVE_HANDLE(pViewCostume->hWeaponStance);

	if (bFree) {
		free(pViewCostume);
	}
}

void costumeView_FreeGraphics(CostumeViewGraphics *pGraphics)
{
	if (!pGraphics)
		return;

	costumeView_FreeViewCostume(&pGraphics->costume, false);

	while (eaSize(&pGraphics->eaExtraCostumes) > 0) {
		costumeView_FreeViewCostume(eaPop(&pGraphics->eaExtraCostumes), true);
	}

	free(pGraphics);
}

void costumeView_SetPos(CostumeViewGraphics *pGraphics, const Vec3 v3Pos)
{
	copyVec3(v3Pos, pGraphics->costume.v3SkelPos);
}

void costumeView_SetRot(CostumeViewGraphics *pGraphics, const Quat qRot)
{
	copyQuat(qRot, pGraphics->costume.qSkelRot);
}

void costumeView_SetPosRot(CostumeViewGraphics *pGraphics, const Vec3 v3Pos, const Quat qRot)
{
	copyVec3(v3Pos, pGraphics->costume.v3SkelPos);
	copyQuat(qRot, pGraphics->costume.qSkelRot);
}

void costumeView_SetViewCostumePos(CostumeViewCostume *pViewCostume, const Vec3 v3Pos)
{
	copyVec3(v3Pos, pViewCostume->v3SkelPos);
}

void costumeView_SetViewCostumeRot(CostumeViewCostume *pViewCostume, const Quat qRot)
{
	copyQuat(qRot, pViewCostume->qSkelRot);
}

void costumeView_SetViewCostumePosRot(CostumeViewCostume *pViewCostume, const Vec3 v3Pos, const Quat qRot)
{
	copyVec3(v3Pos, pViewCostume->v3SkelPos);
	copyQuat(qRot, pViewCostume->qSkelRot);
}

static PCBoneDef *costumeView_GetCostumeBoneForSkelBone(PCBoneDef **eaBones, const char *pcSkelBone)
{
	PCBoneDef *pBone;
	int i;

	for(i=eaSize(&eaBones)-1; i>=0; --i) {
		pBone = eaBones[i];
		if (pBone && pBone->pcClickBoneName && stricmp(pBone->pcClickBoneName, pcSkelBone) == 0) {
			return pBone;
		}
	}

	return NULL;
}

static bool costumeView_ExcludeBones(const DynNode* pNode, PCBoneDef **eaBones)
{
	if (pNode->pcTag && costumeView_GetCostumeBoneForSkelBone(eaBones, pNode->pcTag)) {
		return false;
	} else {
		return true;
	}
}


PCBoneDef *costumeView_GetSelectedBone(CostumeViewGraphics *pGraphics, NOCONST(PlayerCostume) *pCostume)
{
	SpeciesDef *pSpecies = pCostume ? GET_REF(pCostume->hSpecies) : NULL;
	Vec3 vStart, vEnd, vDir;
	const DynNode *pBone;
	PCBoneDef *pResult = NULL;
	PCBoneDef **eaBones = NULL;

	if (!pCostume) return NULL;
	if (!pGraphics) return NULL;

	if (pGraphics->costume.pSkel) {
		costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), NULL, NULL, pSpecies, NULL, NULL, &eaBones, CGVF_OMIT_EMPTY | CGVF_UNLOCK_ALL);
		target_GetCursorRay(NULL, vStart, vDir);
		scaleAddVec3(vDir, 300.0f, vStart, vEnd);
		pBone = dynSkeletonGetClosestBoneToLineSegment(pGraphics->costume.pSkel, vStart, vEnd, costumeView_ExcludeBones, eaBones);
		if (pBone) {
			pResult = costumeView_GetCostumeBoneForSkelBone(eaBones, pBone->pcTag);
		}
		eaDestroy(&eaBones);
	}

	return pResult;
}

void costumeView_CalculateCamera(CostumeViewGraphics* pGraphics,
								 const CBox *pBox,
								 // 0 <= fOriginX <= 1, then percentage of pBox, otherwise pixel.
								 // = 0: Left Edge
								 // = 1: Right Edge
								 F32 fOriginX,
								 // 0 <= fOriginY <= 1, then percentage of pBox, otherwise pixel.
								 // = 0: Top Edge
								 // = 1: Top Edge
								 F32 fOriginY,
								 F32 fCostumeWidth,
								 F32 fCostumeHeight,
								 // EXPECTED: -fCostumeWidth/2 <= fWidthMod <= fCostumeWidth/2
								 // = 0: the horizontal center of the costume
								 // < 0: offset towards screen left
								 // > 0: offset towards screen height
								 F32 fWidthMod,
								 // EXPECTED: 0 <= fHeightMod <= fCostumeHeight
								 // = 0: the root position of the costume
								 // = fCostumeHeight: the top of the costume
								 F32 fHeightMod,
								 // This is the base camera distance, it should be roughly
								 // half the costume height or half the costume width,
								 // which ever is bigger.
								 F32 fDepthMod,
								 // EXPECTED: 0 <= fZoom <= 5
								 F32 fZoom)
{
	const F32 c_fPadding = 0.1f;
	GfxSettings Settings;
	F32 fTargetHeight;
	Vec2 vCenter, vTanFOV, vOffset;
	F32 fCamMin, fCamMax;
	F32 fBoxWidth = CBoxWidth(pBox) * (1 - c_fPadding);
	F32 fBoxHeight = CBoxHeight(pBox) * (1 - c_fPadding);
	int screenWidth;
	int screenHeight;

	gfxGetSettings(&Settings);
	gfxSettingsGetScreenSize(&Settings, &screenWidth, &screenHeight);

	// Normalize zoom to 0 <= fZoom <= 1
	fZoom *= 0.2f;

	// The center of the costume will appear at the following location relative
	// to the provided box.
	setVec2(vCenter,
		pBox->lx + CBoxWidth(pBox) * c_fPadding * 0.5f + (0 <= fOriginX <= 1 ? fBoxWidth * fOriginX : fOriginX),
		pBox->ly + CBoxHeight(pBox) * c_fPadding * 0.5f + (0 <= fOriginY <= 1 ? fBoxHeight * fOriginY : fOriginY)
	);

	// Now transform the screen space location to camera space.
	// Assume: <0, 0> to be at the center of the screen and that the range of the screen is [-1, 1].
	vCenter[0] = vCenter[0] * (1.0f / screenWidth) - 0.5f;
	vCenter[1] = vCenter[1] * (1.0f / screenHeight) - 0.5f;

	// Calculate the desired screen size of the costume.
	fTargetHeight = fCostumeHeight;
	if (fBoxHeight >= 1)
		fTargetHeight *= screenHeight / fBoxHeight;

	// Calculate half screen FOV tangent.
	if (!gCamera->useHorizontalFOV)
	{
		F32 fAspect = screenWidth / (F32)screenHeight;
		F32 fTanFOVy = tan(RAD(gCamera->projection_fov) * 0.5f);
		setVec2(vTanFOV, 2 * fAspect * fTanFOVy, 2 * fTanFOVy);
	}
	else
	{
		F32 fAspect = screenWidth / (F32)screenHeight;
		F32 fTanFOVx = tan(RAD(gCamera->projection_fov) * 0.5f);
		setVec2(vTanFOV, 2 * fTanFOVx, 2 * (fTanFOVx / fAspect));
	}

	// Calculate min/max camera distance
	fCamMin = fDepthMod / vecY(vTanFOV);
	fCamMax = fTargetHeight / vecY(vTanFOV);
	MAX1F(fCamMax, fCamMin + 1.0f);

	// Calculate distance based on zoom
	gCamera->camdist = fCamMin + fZoom * (fCamMax - fCamMin);

	// The height & width modifier based on the zoom ratio
	setVec2(vOffset, (1 - fZoom) * fWidthMod /*- (1 - fZoom) * fCostumeWidth*/, (1 - fZoom) * fHeightMod);

	gCamera->centeroffset[0] = gCamera->camdist * vecX(vTanFOV) * vecX(vCenter) + vOffset[0];
	gCamera->centeroffset[1] = gCamera->camdist * vecY(vTanFOV) * vecY(vCenter) + vOffset[1];
	gCamera->centeroffset[2] = 0;

	assert(FINITEVEC3(gCamera->centeroffset));
}

//have the camera fit the gen screenbox to a character costume height, by changing gCamera->camdist and cameraoffset
void costumeView_UpdateCamera(CostumeViewGraphics* pGraphics, 
							  const CBox* pBox, 
							  F32 fCostumeHeight, 
							  F32 fHeightMod, 
							  F32 fDepthMod, 
							  bool bZoom, 
							  F32 fZoom,
							  F32 fZoomMaxOverride,
							  bool bComputeHeightOffset,
							  F32 *fZoomSmoothing)
{
	GfxSettings pSettings;
	F32 fBufferBot, fBufferTop; 
	F32 fUIWidth, fUIHeight;
	F32 fAspect;
	Vec2 vBoxBot;
	F32 fTargetHeight, fHeightOffset = 0.0f;
	F32 fTotalOffset;
	F32 fSpeedFrame;
	Vec2 vCharScreenPos, vScreenDir;
	Vec3 vOffsetDir;
	bool bIsZooming = false;
	F32 fZoomCoef =0;
	int screenWidth;
	int screenHeight;

	gfxGetSettings( &pSettings );
	gfxSettingsGetScreenSize(&pSettings, &screenWidth, &screenHeight);

	if (gCostumeCameraSettings.bResetCamera)
	{
		bZoom = false;
	}

	// add 10% of windowHeight region on top of character
	fBufferTop = screenHeight * 0.1f;
	// add 5% of windowHeight region on bottom of character
	fBufferBot = screenHeight * 0.05f;

	fUIWidth = CBoxWidth( pBox );
	fUIHeight = CBoxHeight( pBox );

	vBoxBot[0] = pBox->lx + fUIWidth * 0.5f;
	vBoxBot[1] = pBox->hy - fBufferBot;

	fAspect = (screenWidth/((F32)(screenHeight)));
	fTargetHeight = (screenHeight / MAXF(fUIHeight-fBufferTop-fBufferBot,1.0f)) * (fCostumeHeight);
	
	if( bZoom )
	{
		if( nearSameF32(gCostumeCameraSettings.fCamDistMax,gCostumeCameraSettings.fCamDistMin) )
			fZoomCoef = 1;
		else
			fZoomCoef = MINF((gCamera->camdist-gCostumeCameraSettings.fCamDistMin) / (gCostumeCameraSettings.fCamDistMax-gCostumeCameraSettings.fCamDistMin), 1);
	}
	
	//set our min/max camera zoom distance
	gCostumeCameraSettings.fCamDistMin = fDepthMod / MAXF(gCostumeCameraSettings.f2TanFOVy, FLT_EPSILON);
	MAX1F(gCostumeCameraSettings.fCamDistMin, FLT_EPSILON);
	if (fZoomMaxOverride > FLT_EPSILON)
	{
		gCostumeCameraSettings.fCamDistMax = fZoomMaxOverride;
	}
	else if (bZoom && gCostumeCameraSettings.f2TanFOVy > FLT_EPSILON)
	{
		gCostumeCameraSettings.fCamDistMax = fTargetHeight / gCostumeCameraSettings.f2TanFOVy;
	}
	MAX1F(gCostumeCameraSettings.fCamDistMax, gCostumeCameraSettings.fCamDistMin+1.0f);

	//only run this when we have to
	if (!gCostumeCameraSettings.bFOVSet 
		|| !nearSameF32(gCostumeCameraSettings.fAspect,fAspect)
		|| gCostumeCameraSettings.bUseHorizontalFOV != gCamera->useHorizontalFOV)
	{
		//compute projection parameters
		F32 fFOV = RAD(gCamera->projection_fov) * 0.5f;
		
		if (!gCamera->useHorizontalFOV)
		{
			F32 fTanFOVy = tan(fFOV);
			gCostumeCameraSettings.f2TanFOVy = 2 * fTanFOVy;
			gCostumeCameraSettings.f2TanFOVx = 2 * fAspect * fTanFOVy;
		}
		else
		{
			F32 fTanFOVx = tan(fFOV);
			gCostumeCameraSettings.f2TanFOVx = 2 * fTanFOVx;
			gCostumeCameraSettings.f2TanFOVy = 2 * (fTanFOVx / fAspect);
		}

		gCostumeCameraSettings.bFOVSet = true;
		gCostumeCameraSettings.bUseHorizontalFOV = gCamera->useHorizontalFOV;
	}

	if (bZoom)
	{
		F32 fCamDistRange;
		if (!nearSameF32(gCostumeCameraSettings.fCurrentCostumeHeight,fCostumeHeight) || !nearSameF32(gCostumeCameraSettings.fAspect,fAspect) || !nearSameF32(gCostumeCameraSettings.fCurrentTargetHeight, fTargetHeight) )
		{
			if (fZoomMaxOverride <= FLT_EPSILON)
			{
				gCostumeCameraSettings.fCamDistMax = MAXF(fTargetHeight / MAXF(gCostumeCameraSettings.f2TanFOVy, 0.001f), gCostumeCameraSettings.fCamDistMin+1.0f);
			}
			gCamera->camdist = gCostumeCameraSettings.fCamDistMin + (gCostumeCameraSettings.fCamDistMax - gCostumeCameraSettings.fCamDistMin) * fZoomCoef;
		}
		else
		{
			gCamera->camdist += fZoom * (gCostumeCameraSettings.fCamDistMax - gCostumeCameraSettings.fCamDistMin) * 0.2f;

			bIsZooming = !nearSameF32(fZoom, 0.0f) || (fZoomSmoothing && !nearSameF32(*fZoomSmoothing, 0.0f));

			if (gCamera->camdist > gCostumeCameraSettings.fCamDistMax)
			{
				gCamera->camdist = gCostumeCameraSettings.fCamDistMax;

				if (fZoomSmoothing)
				{
					// Kill the zoom smoothing
					*fZoomSmoothing = 0;
				}
			}
			else if (gCamera->camdist < gCostumeCameraSettings.fCamDistMin)
			{
				gCamera->camdist = gCostumeCameraSettings.fCamDistMin;

				if (fZoomSmoothing)
				{
					*fZoomSmoothing = 0;
				}
			}
		}
		fCamDistRange = MAXF(gCostumeCameraSettings.fCamDistMax-gCostumeCameraSettings.fCamDistMin,0.001f);
		fHeightOffset += (1-((gCamera->camdist-gCostumeCameraSettings.fCamDistMin)/fCamDistRange))*(fHeightMod);
	}
	else
	{
		gCamera->camdist = fTargetHeight / MAXF(gCostumeCameraSettings.f2TanFOVy, 0.001f);
		gCostumeCameraSettings.fCamDistMax = MAXF(gCamera->camdist, gCostumeCameraSettings.fCamDistMin+1.0f);
	}

	if (bComputeHeightOffset)
	{
		F32 fCx, fCy;
		CBoxGetCenter(pBox, &fCx, &fCy);
		fCy -= screenHeight/2;
		fHeightOffset += (gCostumeCameraSettings.f2TanFOVy*gCamera->camdist) * (fCy/screenHeight);
	}

	//HACK: assume that our starting point is the middle of the screen!
	vCharScreenPos[0] = screenWidth*0.5f;
	vCharScreenPos[1] = screenHeight*0.5f;

	//compute screen-space offset
	subVec2(vBoxBot, vCharScreenPos, vScreenDir);

	//compute world-space offset
	gCostumeCameraSettings.vDesiredOffset[0] = gCamera->camdist * gCostumeCameraSettings.f2TanFOVx * (vScreenDir[0] / (F32)screenWidth);
	gCostumeCameraSettings.vDesiredOffset[1] = gCamera->camdist * gCostumeCameraSettings.f2TanFOVy * (vScreenDir[1] / (F32)screenHeight) + fHeightOffset;
	gCostumeCameraSettings.vDesiredOffset[2] = 0;

	subVec3(gCostumeCameraSettings.vDesiredOffset, gCamera->centeroffset, vOffsetDir);
	fTotalOffset = normalVec3(vOffsetDir);
	fSpeedFrame = MINF(gCostumeCameraSettings.fCameraSpeed*fTotalOffset, gCostumeCameraSettings.fMaxCameraSpeed) * gGCLState.frameElapsedTime;
	if (bIsZooming || fTotalOffset > 6.0f || fSpeedFrame >= fTotalOffset)
	{
		copyVec3(gCostumeCameraSettings.vDesiredOffset, gCamera->centeroffset);
	}
	else
	{
		scaleAddVec3(vOffsetDir, fSpeedFrame, gCamera->centeroffset, gCamera->centeroffset);
	}

	assert(FINITEVEC3(gCamera->centeroffset));

	//copy over our new values
	gCostumeCameraSettings.fAspect = fAspect;
	gCostumeCameraSettings.fCurrentCostumeHeight = fCostumeHeight;
	gCostumeCameraSettings.fCurrentTargetHeight = fTargetHeight;

	if (gCostumeCameraSettings.bResetCamera)
	{
		Mat4 camera_matrix;

		createMat3YPR(camera_matrix, gCamera->campyr);
		camera_matrix[3][0] = gCamera->centeroffset[0];
		camera_matrix[3][1] = gCamera->centeroffset[1];
		camera_matrix[3][2] = gCamera->camdist;
		gfxSetActiveCameraMatrix(camera_matrix,true);
		gfxSetActiveCameraMatrix(camera_matrix,false);

		gCostumeCameraSettings.bResetCamera = false;
	}
}

void costumeView_ResetCamera(void)
{
	gCostumeCameraSettings.fCurrentCostumeHeight = -1;
	gCostumeCameraSettings.bResetCamera = true;
}

#include "gclCostumeView_c_ast.c"
