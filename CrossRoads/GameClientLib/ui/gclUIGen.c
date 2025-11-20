#include "gclUIGen.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "StringFormat.h"
#include "estring.h"
#include "EntityLib.h"
#include "PowerTree.h"
#include "Character.h"
#include "contact_common.h"
#include "CostumeCommonLoad.h"
#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "GameAccountDataCommon.h"
#include "gclUIGen.h"
#include "gclUIGen_h_ast.h"
#include "gclEntity.h"
#include "gclMapState.h"
#include "gclHUDOptions.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "SavedPetCommon.h"
#include "Entity_h_ast.h"
#include "entCritter.h"
#include "entEnums_h_ast.h"
#include "dynFxInterface.h"
#include "dynFxManager.h"
#include "dynFx.h"
#include "StringCache.h"
#include "PowerTree_h_ast.h"
#include "PowersAutoDesc.h"
#include "Expression.h"
#include "GraphicsLib.h"
#include "mapstate_common.h"
#include "Powers_h_ast.h"
#include "PowerModes.h"
#include "PowerActivation.h"
#include "PowerSubtarget.h"
#include "RegionRules.h"
#include "Character_target.h"
#include "Character_tick.h"
#include "AutoTransDefs.h"
#include "MessageExpressions.h"
#include "PowerTreeHelpers.h"
#include "GfxHeadshot.h"
#include "WLCostume.h"
#include "WorldGrid.h"
#include "dynSequencer.h"
#include "CharacterClass.h"
#include "CharacterAttribs.h"
#include "CombatEval.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonTailor.h"
#include "DamageTracker.h"
#include "GameClientLib.h"
#include "chatCommonStructs.h"
#include "chat/gclClientChat.h"
#include "Guild.h"
#include "EntityIterator.h"
#include "dynFxInfo.h"
#include "gclChat.h"
#include "chat/gclChatLog.h"
#include "gclChatConfig.h"
#include "mission_common.h"
#include "Player.h"
#include "StringUtil.h"
#include "wlTime.h"
#include "ResourceManager.h"
#include "wininclude.h"
#include "PowerGrid.h"
#include "MicroTransactionUI.h"
#include "smf_render.h"
#include "sysutil.h"
#include "species_common.h"
#include "CostumeCommonTailor.h"
#include "Tray.h"
#include "UITray.h"
#include "Character_target.h"
#include "LoginCommon.h"
#include "gclLogin.h"
#include "Prefs.h"
#include "utilitiesLib.h"
#include "FCInventoryUI.h"

#include "gclCostumeUI.h"
#include "gclCostumeCameraUI.h"
#include "gclCamera.h"
#include "GfxCamera.h"
#include "GfxSpriteText.h"

#include "GameStringFormat.h"

#include "soundLib.h"

#include "UIGenJail.h"

#include "smf_render.h"
#include "GamePermissionsCommon.h"
#include "GamePermissionsCommon_h_ast.h"

#include "GameAccountData_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
//#include "Autogen/gclChat_h_ast.h"
#include "Autogen/entCritter_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/SavedPetCommon_h_ast.h"
#include "Autogen/gclUIGen_c_ast.h"
#include "DamageTracker_h_ast.h"
#include "PowersEnums_h_ast.h"
#include "Player_h_ast.h"

#include "ClientTargeting.h"
#include "PowersAutoDesc_h_ast.h"


#define MAX_NAME_SUGGESTIONS 20
#define MAX_RECENT_TELL_SUGGESTIONS 7

static const char *s_pchGenEntityString;
static const char *s_pchGenObjectString;
static const char *s_pchGenObjEntityString;
static const char *s_pchGenWaypointString;
static int s_iGenEntityHandle;
static int s_iGenObjectHandle;
static int s_iGenWaypointHandle;

static bool s_bInitDictionaryListener = false;
static ContainerRef **s_eaChangedEntities = NULL;

static struct PowerTreeDefRef **s_eaTreeRefs;

AUTO_STRUCT;
typedef struct AlwaysPropSlotCandidate
{
	char*		pchName;
	U32			uiPetID;
	ContainerID	iID;
	bool		bIsEmpty;
} AlwaysPropSlotCandidate;

typedef struct SuggestionDisplayOptions {
	char *pchSource;
	U32 iColor;
	bool bShowSource : 1;
	bool bAddQuotesIfNeeded : 1;
	bool bAddCommaIfNeeded : 1;
	bool bAddTrailingSpace : 1;
} SuggestionDisplayOptions;

typedef struct SuggestionTestOptions {
	bool bMayPerformPartialCompletion : 1;
	bool bDoMinimalExpansion : 1;
} SuggestionTestOptions ;

AUTO_STRUCT;
typedef struct UIGenReopenInformation {
	const char *pchName;				AST(KEY STRUCTPARAM POOL_STRING)
	char displayName[128];
	char loginCharacterName[128];
	bool bJailed;
} UIGenReopenInformation;

static UIGenReopenInformation **s_eaSavedWindows = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

UIGenExpressionContainer g_ShowPayExpression;

UIGenExpressionContainer g_EntityGenExpression;
UIGenExpressionContainer g_ObjectGenExpression;
UIGenExpressionContainer g_WaypointGenExpression;

UIGenExpressionContainer g_EntityGenOffscreenExpression;
UIGenExpressionContainer g_ObjectGenOffscreenExpression;
UIGenExpressionContainer g_WaypointGenOffscreenExpression;

bool bDebugEntityComplete;
AUTO_CMD_INT(bDebugEntityComplete, debugEntityComplete) ACMD_CLIENTONLY;

extern ParseTable parse_DynDefineParam[];
#define TYPE_parse_DynDefineParam DynDefineParam

AUTO_FIXUPFUNC;
TextParserResult ui_GenExpressionContainerFixup(UIGenExpressionContainer *pContainer, enumTextParserFixupType eType, void *pExtraData)
{
	if ((eType == FIXUPTYPE_POST_TEXT_READ || eType == FIXUPTYPE_POST_RELOAD) && pContainer->pExpression)
	{
		if (!exprGenerate(pContainer->pExpression, ui_GenGetContext(NULL)))
			return PARSERESULT_INVALID;
	}
	return PARSERESULT_SUCCESS;
}

static void gclGenExprReload(const char *pchName, const char *pchPath, UIGenExpressionContainer *pContainer)
{
	UIGenExpressionContainer Container = {0};
	bool bSuccess = true;
	char achSpec[MAX_PATH];
	char achBin[MAX_PATH];
	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}
	loadstart_printf("Loading %s", pchName);
	sprintf(achSpec, "%s.uiexpr", pchName);
	sprintf(achBin, "%s.bin", pchName);
	bSuccess = ParserLoadFiles("ui/gens", achSpec, achBin, PARSER_OPTIONALFLAG, parse_UIGenExpressionContainer, &Container);
	if (bSuccess)
	{
		StructDestroySafe(parse_Expression, &pContainer->pExpression);
		pContainer->pExpression = Container.pExpression;
	}
	loadend_printf(" Done.");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenShouldShowPay);
bool exprGenShouldShowPay(ExprContext *pContext)
{
	if(g_ShowPayExpression.pExpression)
	{
		MultiVal mv;

		exprEvaluate(g_ShowPayExpression.pExpression, pContext, &mv);
		return !!mv.intval;
	}

	return true;
}

static void gclShowPayExprReload(const char *pchPath, S32 iWhen)
{
	gclGenExprReload("ShowPay", NULL, &g_ShowPayExpression);
}

static void gclEntityGensExprReload(const char *pchPath, S32 iWhen)
{
	gclGenExprReload("EntityGens", NULL, &g_EntityGenExpression);
}

static void gclObjectGensExprReload(const char *pchPath, S32 iWhen)
{
	gclGenExprReload("ObjectGens", NULL, &g_ObjectGenExpression);
}

static void gclWaypointGensExprReload(const char *pchPath, S32 iWhen)
{
	gclGenExprReload("WaypointGens", NULL, &g_WaypointGenExpression);
}

static void gclEntityGensOffscreenExprReload(const char *pchPath, S32 iWhen)
{
	gclGenExprReload("EntityGensOffscreen", NULL, &g_EntityGenOffscreenExpression);
}

static void gclObjectGensOffscreenExprReload(const char *pchPath, S32 iWhen)
{
	gclGenExprReload("ObjectGensOffscreen", NULL, &g_ObjectGenOffscreenExpression);
}

static void gclWaypointGensOffscreenExprReload(const char *pchPath, S32 iWhen)
{
	gclGenExprReload("WaypointGensOffscreen", NULL, &g_WaypointGenOffscreenExpression);
}

static int gclui_SMFNavigate(const char *pch)
{
	void exprGenOpenWebBrowser(const char *pchUrl);

	if (strStartsWith(pch, "http:") || strStartsWith(pch, "https:"))
	{
		exprGenOpenWebBrowser(pch);
		return true;
	}
	else if (strStartsWith(pch, "mt:"))
	{
		if (isdigit(pch[3]))
		{
			U32 uID = (U32) atoi(pch + 3);
			gclMicroTrans_expr_ShowProduct(uID);
			return true;
		}
		else
		{
			gclMicroTrans_expr_ShowProductByName(pch + 3);
			return true;
		}
	}
	return smf_Navigate(pch);
}

AUTO_STARTUP(GameUI) ASTRT_DEPS(UILib UIGen AS_CharacterClassTypes PetRally ClientTargeting PowerLocationTargeting PowerTargeting);
void gclLoadUI(void)
{
	const char *pchCursorSkin;
	if(gbNoGraphics)
	{
		return;
	}

	ui_GenInitIntVar("IsOnscreen", 0);
	ui_GenInitFloatVar("Distance");

	gclShowPayExprReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/gens/ShowPay.uiexpr", gclShowPayExprReload);
	gclEntityGensExprReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/gens/EntityGens.uiexpr", gclEntityGensExprReload);
	gclObjectGensExprReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/gens/ObjectGens.uiexpr", gclObjectGensExprReload);
	gclWaypointGensExprReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/gens/WaypointGens.uiexpr", gclWaypointGensExprReload);
	gclEntityGensOffscreenExprReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/gens/EntityGensOffscreen.uiexpr", gclEntityGensOffscreenExprReload);
	gclObjectGensOffscreenExprReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/gens/ObjectGensOffscreen.uiexpr", gclObjectGensOffscreenExprReload);
	gclWaypointGensOffscreenExprReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/gens/WaypointGensOffscreen.uiexpr", gclWaypointGensOffscreenExprReload);

	pchCursorSkin = GamePrefGetString("CursorSkin", NULL);
	if (pchCursorSkin != NULL)
		ResourceOverlayLoad("UICursor", pchCursorSkin);
}

//////////////////////////////////////////////////////////////////////////

void ui_GenEntityFxCreate(UIGen *pGen, const char *pchFx, DynParamBlock* pParams, UIGenEntityState *pEntState)
{
	Entity *pEntity = entFromEntityRefAnyPartition(pEntState->hEntity);
	if (pEntity)
	{
		DynNode *pNode = dynNodeFromGuid(pEntity->dyn.guidRoot);
		DynFxManager *pManager = dynFxManFromGuid(pEntity->dyn.guidFxMan);
		if (pNode && pManager)
		{
			UIGen3DFxState *pFxState = StructCreate(parse_UIGen3DFxState);
			DynFx *pDynFx;
			DynAddFxParams params = {0};
			pFxState->pchFxName = allocAddString(pchFx);

			REMOVE_HANDLE(pFxState->hFx);
			params.pParamBlock = pParams;
			params.pTargetRoot = params.pSourceRoot = pNode;
			params.eSource = eDynFxSource_UI;
			params.ePriorityOverride = edpOverride;
			params.bOverridePriority = true;
			pDynFx = dynAddFx(pManager, pchFx, &params);
			if (!pDynFx && !dynDebugState.bNoNewFx && !dynDebugState.bNoUIFX)
				ErrorFilenamef(pGen->pchFilename, "Unable to create FX %s", pchFx);
			ADD_SIMPLE_POINTER_REFERENCE_DYN(pFxState->hFx, pDynFx);

			eaPush(&pEntState->ppEntityFxState, pFxState);
		}
	}
}


void ui_GenObjectFxCreate(UIGen *pGen, const char *pchFx, DynParamBlock* pParams, UIGenObjectState *pObjState)
{
	WorldInteractionNode *pObject = GET_REF(pObjState->hKey);
	Entity *pEnt = entFromEntityRefAnyPartition(pObjState->hEntity);	
	if (pObjState->pObjectFxState)
	{
		return; // Only one at a time
	}
	if (pObject || pEnt)
	{	
		UIGen3DFxState *pFxState= StructCreate(parse_UIGen3DFxState);
		pFxState->pchFxName = allocAddString(pchFx);
		if (pObject)
		{
			wlInteractionNodeSetClientFXName(pObject,pchFx);
		}
		if (pEnt)
		{
			DynNode *pNode = dynNodeFromGuid(pEnt->dyn.guidRoot);
			DynFxManager *pManager = dynFxManFromGuid(pEnt->dyn.guidFxMan);
			if (pNode && pManager)
			{		
				DynFx *pDynFx;
				DynAddFxParams params = {0};

				REMOVE_HANDLE(pFxState->hFx);
				params.pParamBlock = pParams;
				params.pTargetRoot = params.pSourceRoot = pNode;
				params.eSource = eDynFxSource_UI;
				params.ePriorityOverride = edpOverride;
				params.bOverridePriority = true;
				pDynFx = dynAddFx(pManager, pchFx, &params);
				if (!pDynFx && !dynDebugState.bNoNewFx && !dynDebugState.bNoUIFX)
					ErrorFilenamef(pGen->pchFilename, "Unable to create FX %s", pchFx);
				ADD_SIMPLE_POINTER_REFERENCE_DYN(pFxState->hFx, pDynFx);				
			}
		}
		pObjState->pObjectFxState = pFxState;
	}
}

void ui_GenObjectStateDestroy(UIGenObjectState *pObjState)
{
	WorldInteractionNode *pNode = GET_REF(pObjState->hKey);
	// Fx in state that doesn't exist in result
	if (pNode)
	{
		wlInteractionNodeSetClientFXName(pNode,NULL);
	}
}

void ui_Gen3DFxDestroy(UIGen3DFxState *pFxState)
{
	if (GET_REF(pFxState->hFx))
		dynFxKill(GET_REF(pFxState->hFx), false, true, false, eDynFxKillReason_ExternalKill);
	REMOVE_HANDLE(pFxState->hFx);
}

AUTO_FIXUPFUNC;
TextParserResult ui_GenObjectStateParserFixup(UIGenObjectState *pObjState, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		ui_GenObjectStateDestroy(pObjState);
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult ui_Gen3DFxStateParserFixup(UIGen3DFxState *pFxState, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		ui_Gen3DFxDestroy(pFxState);
	}
	return PARSERESULT_SUCCESS;
}

static DynParamBlock* ui_GenCreateDynParamBlock(UIGen *pGen, UIGen3DFx* pFx)
{
	S32 i, j;
	DynParamBlock* pBlock;
	char *pchStr = NULL;

	if ( eaSize(&pFx->eaParams) == 0 )
	{
		return NULL;
	}
	
	pBlock = dynParamBlockCreate();
	
	estrStackCreate(&pchStr);

	for ( i = 0; i < eaSize(&pFx->eaParams); i++ )
	{
		DynDefineParam* pParam;
		
		estrClear(&pchStr);
		exprFormat(&pchStr, pFx->eaParams[i]->pchName, ui_GenGetContext(pGen), pGen->pchFilename);

		if ( eaSize(&pFx->eaParams[i]->ppchVals) == 0 )
		{
			ErrorFilenamef(pGen->pchFilename,"GenFX: No params found (%s)",pchStr);
			continue;
		}

		pParam = StructCreate( parse_DynDefineParam );
		pParam->pcParamName = allocAddString(pchStr);
	
		switch ( pFx->eaParams[i]->eType )
		{
			xcase UIGen3DFxParamType_STR:
			{
				estrClear(&pchStr);
				exprFormat(&pchStr, pFx->eaParams[i]->ppchVals[0], ui_GenGetContext(pGen), pGen->pchFilename);
				MultiValSetString( &pParam->mvVal, pchStr ); 
			}
			xcase UIGen3DFxParamType_FLT:
			{
				estrClear(&pchStr);
				exprFormat(&pchStr, pFx->eaParams[i]->ppchVals[0], ui_GenGetContext(pGen), pGen->pchFilename);
				MultiValSetFloat( &pParam->mvVal, (F64)atof(pchStr) ); 
			}
			xcase UIGen3DFxParamType_INT:
			{
				estrClear(&pchStr);
				exprFormat(&pchStr, pFx->eaParams[i]->ppchVals[0], ui_GenGetContext(pGen), pGen->pchFilename);
				MultiValSetInt( &pParam->mvVal, (S64)atoi(pchStr) ); 
			}
			xcase UIGen3DFxParamType_VEC:
			{
				S32 iSize = min(eaSize(&pFx->eaParams[i]->ppchVals),3);
				Vec3 v;
				
				for ( j = 0; j < iSize; j++ )
				{
					estrClear(&pchStr);
					exprFormat(&pchStr, pFx->eaParams[i]->ppchVals[j], ui_GenGetContext(pGen), pGen->pchFilename);
					v[j] = (F32)atof(pchStr);
				}
				MultiValSetVec3(&pParam->mvVal, &v);
			}
			xdefault:
			{
				ErrorFilenamef(pGen->pchFilename, "GenFX: Invalid param type (%s)", 
					StaticDefineIntRevLookup(UIGen3DFxParamTypeEnum, pFx->eaParams[i]->eType));
				StructDestroy( parse_DynDefineParam, pParam );
				continue;
			}
		}

		eaPush( &pBlock->eaDefineParams, pParam );
	}

	estrDestroy(&pchStr);

	return pBlock;
}

void ui_GenUpdateEntity(UIGen *pGen)
{
	int i,j;
	UIGenEntity *pEnt = UI_GEN_RESULT(pGen,Entity);
	UIGenEntityState *pEntState = UI_GEN_STATE(pGen,Entity);
	static char *s_pchFx = NULL;
	
	// Make sure state matches new result
	for (i = eaSize(&pEntState->ppEntityFxState) - 1; i >= 0; i--)
	{
		for (j = eaSize(&pEnt->ppEntityFx) - 1; j >= 0; j--)
		{
			estrClear(&s_pchFx);
			exprFormat(&s_pchFx, pEnt->ppEntityFx[j]->pchFxName, ui_GenGetContext(pGen), pGen->pchFilename);
			if (!stricmp(s_pchFx, pEntState->ppEntityFxState[i]->pchFxName))
				break;
		}
		if (j < 0 || !GET_REF(pEntState->ppEntityFxState[i]->hFx))
		{
			// Fx in state that doesn't exist in result, or has been killed some other way
			StructDestroy(parse_UIGen3DFxState, pEntState->ppEntityFxState[i]);
			eaRemoveFast(&pEntState->ppEntityFxState, i);
		}
	}
	for (i = eaSize(&pEnt->ppEntityFx) - 1; i >= 0; i--)
	{
		estrClear(&s_pchFx);
		exprFormat(&s_pchFx, pEnt->ppEntityFx[i]->pchFxName, ui_GenGetContext(pGen), pGen->pchFilename);
		for (j = eaSize(&pEntState->ppEntityFxState) - 1; j >= 0; j--)
		{
			if (!stricmp(s_pchFx, pEntState->ppEntityFxState[j]->pchFxName))
				break;
		}
		if (j < 0)
		{
			// Fx in result that doesn't exist in state
			ui_GenEntityFxCreate(pGen, s_pchFx, ui_GenCreateDynParamBlock(pGen, pEnt->ppEntityFx[i]), pEntState);
		}
	}
}


void ui_GenUpdateObject(UIGen *pGen)
{
	int i,j;
	UIGenObject *pObj = UI_GEN_RESULT(pGen,Object);
	UIGenObjectState *pObjState = UI_GEN_STATE(pGen,Object);
	static char *s_pchFx = NULL;
	UIGen3DFxState *pFxState;


	// Make sure state matches new result
	if (pFxState = pObjState->pObjectFxState)
	{
		for (j = eaSize(&pObj->ppObjectFx) - 1; j >= 0; j--)
		{
			estrClear(&s_pchFx);
			exprFormat(&s_pchFx, pObj->ppObjectFx[j]->pchFxName, ui_GenGetContext(pGen), pGen->pchFilename);
			if (!stricmp(s_pchFx, pFxState->pchFxName))
				break;
		}
		if (j < 0)
		{
			WorldInteractionNode *pNode = GET_REF(pObjState->hKey);
			// Fx in state that doesn't exist in result
			if (pNode)
			{
				wlInteractionNodeSetClientFXName(pNode,NULL);
			}

			StructDestroy(parse_UIGen3DFxState, pObjState->pObjectFxState);
			pObjState->pObjectFxState = NULL;
		}
	}
	for (i = eaSize(&pObj->ppObjectFx) - 1; i >= 0; i--)
	{
		estrClear(&s_pchFx);
		exprFormat(&s_pchFx, pObj->ppObjectFx[i]->pchFxName, ui_GenGetContext(pGen), pGen->pchFilename);
/*		for (j = eaSize(&pObjState->ppObjectFxState) - 1; j >= 0; j--)
		{
			if (!stricmp(pchFx, pObjState->ppObjectFxState[j]->pchFxName))
				break;
		}*/
		if (!pObjState->pObjectFxState)
		{
			// Fx in result that doesn't exist in state
			ui_GenObjectFxCreate(pGen, s_pchFx, ui_GenCreateDynParamBlock(pGen, pObj->ppObjectFx[i]), pObjState);
		}
	}
}


void ui_GenUpdateContextObject(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	UIGenObjectState *pObjState = UI_GEN_STATE(pGen,Object);
	if (pObjState)
	{
		WorldInteractionNode *pNode = GET_REF(pObjState->hKey);
		Entity *pEnt = entFromEntityRefAnyPartition(pObjState->hEntity);
		exprContextSetPointerVarPooledCached(pContext, s_pchGenObjEntityString, pEnt, parse_Entity,true, false, &s_iGenObjectHandle);
		exprContextSetPointerVarPooledCached(pContext, s_pchGenObjectString, pNode, parse_WorldInteractionNode,true, false, &s_iGenObjectHandle);
	}
}

void ui_GenUpdateContextEntity(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	UIGenEntityState *pEntState = UI_GEN_STATE(pGen, Entity);
	if (pEntState)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pEntState->hEntity);
		exprContextSetPointerVarPooledCached(pContext, s_pchGenEntityString, pEnt, parse_Entity, true, true, &s_iGenEntityHandle);
	}
}

void ui_GenUpdateContextWaypoint(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	UIGenWaypointState *pWaypointState = UI_GEN_STATE(pGen, Waypoint);
	if (pWaypointState)
	{
		MinimapWaypoint *pWaypoint = pWaypointState->pWaypoint;
		exprContextSetPointerVarPooledCached(pContext, s_pchGenWaypointString, pWaypoint, parse_MinimapWaypoint, true, true, &s_iGenWaypointHandle);
	}
}

//////////////////////////////////////////////////////////////////////////
// Methods for power tree gen handling...

static const char *PowerTreeNodeGetIcon(PTNodeDef *pNode, S32 iRank)
{
	if (pNode)
	{
		S32 i;
		for (i = iRank; i >= 0; i--)
		{
			PTNodeRankDef *pRankDef = eaGet(&pNode->ppRanks, i);
			if (pRankDef)
			{
				PowerDef *pPowerDef = GET_REF(pRankDef->hPowerDef);
				if (pPowerDef && pPowerDef->pchIconName)
					return pPowerDef->pchIconName;
			}
		}
	}
	return "Power_Default";
}
static void gclGenExprGetPowerTreeGroupsDepthFirstWalk(PTGroupTopDown ***peaGroups, PTGroupTopDown *pGroup)
{
	S32 i;
	for (i = 0; i < eaSize(&pGroup->ppOwnedNodes); i++)
	{
		PTNodeDef *pDef = GET_REF(pGroup->ppOwnedNodes[i]->hNode);
		if (pDef && !(pDef->eFlag & kNodeFlag_HideNode))
		{
			eaPush(peaGroups, pGroup);
			break;
		}
	}
	for (i = 0; i < eaSize(&pGroup->ppGroups); i++)
		gclGenExprGetPowerTreeGroupsDepthFirstWalk(peaGroups, pGroup->ppGroups[i]);
}

void gclGenExprGetBuyablePowerTreeGroupsDepthFirstWalk(Entity* pEnt, PTGroupTopDown ***peaGroups, PTGroupTopDown *pGroup)
{
	S32 i;
	for (i = 0; i < eaSize(&pGroup->ppOwnedNodes); i++)
	{
		PTNodeDef *pDef = GET_REF(pGroup->ppOwnedNodes[i]->hNode);
		if (pDef && !(pDef->eFlag & kNodeFlag_HideNode))
		{
			if(pEnt->pChar)
			{
				if(character_CanBuyPowerTreeGroup(PARTITION_CLIENT, pEnt->pChar, GET_REF(pGroup->hGroup))) {
					eaPush(peaGroups, pGroup);
					break;
				}
			} else {
				eaPush(peaGroups, pGroup);
				break;
			}
		}
	}
	for (i = 0; i < eaSize(&pGroup->ppGroups); i++)
		if(pEnt->pChar) {
			gclGenExprGetBuyablePowerTreeGroupsDepthFirstWalk(pEnt, peaGroups, pGroup->ppGroups[i]);
		} else {
			gclGenExprGetPowerTreeGroupsDepthFirstWalk(peaGroups, pGroup->ppGroups[i]);
		}
}

static void gclGenExprGetOwnedPowerTreeGroupsDepthFirstWalk(Entity* pEnt, PTGroupTopDown ***peaGroups, PTGroupTopDown *pGroup, bool bShowHidden)
{
	S32 i;
	for (i = 0; i < eaSize(&pGroup->ppOwnedNodes); i++)
	{
		PTNodeDef *pDef = GET_REF(pGroup->ppOwnedNodes[i]->hNode);
		if (pDef && (bShowHidden || !(pDef->eFlag & kNodeFlag_HideNode)))
		{
			if(pEnt->pChar)
			{
				PTNode *pNode = powertree_FindNode(pEnt->pChar,NULL,pDef->pchNameFull);
				if(pNode) {
					eaPush(peaGroups, pGroup);
					break;
				}
			}
		}
	}
	for (i = 0; i < eaSize(&pGroup->ppGroups); i++)
	{
		if(pEnt->pChar) {
			gclGenExprGetOwnedPowerTreeGroupsDepthFirstWalk(pEnt, peaGroups, pGroup->ppGroups[i], bShowHidden);
		}// else {
		//	gclGenExprGetPowerTreeGroupsDepthFirstWalk(peaGroups, pGroup->ppGroups[i]);
		//}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerTreeGroups");
bool gclGenExprGetPowerTreeGroups(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTreeName)
{
	PowerTreeDef *pTreeDef = RefSystem_ReferentFromString("PowerTreeDef", pchTreeName);
	static PTGroupTopDown **s_eaGroups = NULL;
	S32 i;
	eaClear(&s_eaGroups);
	if (pTreeDef)
	{
		if (!pTreeDef->pTopDown)
			pTreeDef->pTopDown = powertree_GetTopDown(pTreeDef);
		if (pTreeDef->pTopDown)
		{
			for (i = 0; i < eaSize(&pTreeDef->pTopDown->ppGroups); i++)
				gclGenExprGetPowerTreeGroupsDepthFirstWalk(&s_eaGroups, pTreeDef->pTopDown->ppGroups[i]);
		}
		else
			ErrorFilenamef(pTreeDef->pchFile, "Generating a top-down tree from %s failed", pTreeDef->pchName);
	}
	ui_GenSetManagedListSafe(pGen, &s_eaGroups, PTGroupTopDown, false);
	return !!pTreeDef;
}

S32 gclGetMaxBuyablePowerTreeNodesInGroup(Entity* pEnt, 
										  PowerTree* pTree,
										  PTGroupDef* pGroupDef, 
										  bool bCountAvailable,
										  PTNodeDef*** pppAvailableNodes)
{
	S32 i, iCount = pGroupDef->iMax;
	
	if (!pEnt->pChar || !character_CanBuyPowerTreeGroup(PARTITION_CLIENT, pEnt->pChar, pGroupDef))
	{
		return 0;
	}
	if (bCountAvailable && iCount > 0)
	{
		for (i = eaSize(&pGroupDef->ppNodes)-1; i >= 0; i--)
		{
			PTNodeDef* pNodeDef = pGroupDef->ppNodes[i];
			PTNode* pNode = (PTNode*)powertree_FindNodeHelper(CONTAINER_NOCONST(PowerTree, pTree), pNodeDef);
			S32 iRank = (pNode && !pNode->iRank) ? pNode->iRank+1 : 0;
			
			if (character_CanBuyPowerTreeNode(PARTITION_CLIENT, pEnt->pChar, pGroupDef, pNodeDef, iRank))
			{
				if (pppAvailableNodes)
				{
					eaPush(pppAvailableNodes, pNodeDef);
				}
			}
			else if (pNode)
			{
				if (--iCount == 0)
				{
					break;
				}
			}
		}
	}
	return iCount;
}

static S32 gclCountMaxBuyablePowerTreeNodes(SA_PARAM_NN_VALID Entity *pEnt, const char *pchTreeName, bool bCountAvailable)
{
	PowerTreeDef *pTreeDef = powertreedef_Find(pchTreeName);
	S32 i, iCount = 0;
	if (pEnt->pChar && pTreeDef)
	{
		PowerTree *pTree = (PowerTree*)entity_FindPowerTreeHelper(CONTAINER_NOCONST(Entity, pEnt), pTreeDef);
		for (i = eaSize(&pTreeDef->ppGroups)-1; i >= 0; i--)
		{
			PTGroupDef *pGroupDef = pTreeDef->ppGroups[i];
			iCount += gclGetMaxBuyablePowerTreeNodesInGroup(pEnt, pTree, pGroupDef, bCountAvailable, NULL);
		}
	}
	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CountAvailablePowerTreeNodes");
S32 gclGenExprCountAvailablePowerTreeNodes(SA_PARAM_NN_VALID Entity* pEnt, const char *pchTreeName)
{
	return gclCountMaxBuyablePowerTreeNodes(pEnt, pchTreeName, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMaxBuyablePowerTreeNodes");
S32 gclGenExprGetMaxBuyablePowerTreeNodes(SA_PARAM_NN_VALID Entity* pEnt, const char *pchTreeName)
{
	return gclCountMaxBuyablePowerTreeNodes(pEnt, pchTreeName, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetBuyablePowerTreeGroups");
bool gclGenExprGetBuyablePowerTreeGroups(ExprContext *pContext, SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTreeName)
{
	PowerTreeDef *pTreeDef = RefSystem_ReferentFromString("PowerTreeDef", pchTreeName);
	static PTGroupTopDown **s_eaGroups = NULL;
	S32 i;
	eaClear(&s_eaGroups);
	if (pTreeDef)
	{
		if (!pTreeDef->pTopDown)
			pTreeDef->pTopDown = powertree_GetTopDown(pTreeDef);
		if (pTreeDef->pTopDown)
		{
			for (i = 0; i < eaSize(&pTreeDef->pTopDown->ppGroups); i++)
			{
				gclGenExprGetBuyablePowerTreeGroupsDepthFirstWalk(pEnt, &s_eaGroups, pTreeDef->pTopDown->ppGroups[i]);
			}
		}
		else
			ErrorFilenamef(pTreeDef->pchFile, "Generating a top-down tree from %s failed", pTreeDef->pchName);
	}
	ui_GenSetManagedListSafe(pGen, &s_eaGroups, PTGroupTopDown, false);
	return !!pTreeDef;
}

static bool gclGenExprGetOwnedPowerTreeGroups_Internal(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTreeName, bool bShowHidden)
{
	PowerTreeDef *pTreeDef = RefSystem_ReferentFromString("PowerTreeDef", pchTreeName);
	static PTGroupTopDown **s_eaGroups = NULL;
	S32 i;
	eaClear(&s_eaGroups);
	if (pEnt && pTreeDef)
	{
		if (!pTreeDef->pTopDown)
			pTreeDef->pTopDown = powertree_GetTopDown(pTreeDef);
		if (pTreeDef->pTopDown)
		{
			for (i = 0; i < eaSize(&pTreeDef->pTopDown->ppGroups); i++)
				gclGenExprGetOwnedPowerTreeGroupsDepthFirstWalk(pEnt, &s_eaGroups, pTreeDef->pTopDown->ppGroups[i], bShowHidden);
		}
		else
			ErrorFilenamef(pTreeDef->pchFile, "Generating a top-down tree from %s failed", pTreeDef->pchName);
	}
	ui_GenSetManagedListSafe(pGen, &s_eaGroups, PTGroupTopDown, false);
	return !!pTreeDef;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetOwnedPowerTreeGroups");
bool gclGenExprGetOwnedPowerTreeGroups(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTreeName)
{
	return gclGenExprGetOwnedPowerTreeGroups_Internal(pContext, pEnt, pGen, pchTreeName, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetAllOwnedPowerTreeGroups");
bool gclGenExprGetAllOwnedPowerTreeGroups(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTreeName)
{
	return gclGenExprGetOwnedPowerTreeGroups_Internal(pContext, pEnt, pGen, pchTreeName, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerTreeGroupTop");
bool gclGenExprGetPowerTreeGroupTop(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTreeName)
{
	PowerTreeDef *pTreeDef = RefSystem_ReferentFromString("PowerTreeDef", pchTreeName);
	static PTGroupTopDown **s_eaGroups = NULL;
	S32 i;
	eaClear(&s_eaGroups);
	if(pTreeDef)
	{
		if(!pTreeDef->pTopDown)
			pTreeDef->pTopDown = powertree_GetTopDown(pTreeDef);
		if(pTreeDef->pTopDown)
		{
			for(i = 0; i < eaSize(&pTreeDef->pTopDown->ppGroups); i++)
			{
				eaPush(&s_eaGroups,pTreeDef->pTopDown->ppGroups[i]);
			}
		}
		else
			ErrorFilenamef(pTreeDef->pchFile, "Generating a top-down tree from %s failed", pTreeDef->pchName);
	}

	ui_GenSetManagedListSafe(pGen, &s_eaGroups, PTGroupTopDown, false);
	return !!pTreeDef;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerTreeNameFromNode");
char *gclGenExprGetPowerTreeNameFromNode(ExprContext *pContext, const char *pchNodeName)
{
	PTNodeDef *pNode = powertreenodedef_Find(pchNodeName);
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("PowerTreeDef");
	PowerTreeDef **ppTreeList = (PowerTreeDef**)pStruct->ppReferents;
	int i;

	for(i=0;i<eaSize(&ppTreeList);i++)
	{
		int g;
		for(g=0;g<eaSize(&ppTreeList[i]->ppGroups);g++)
		{
			int n;
			for(n=0;n<eaSize(&ppTreeList[i]->ppGroups[g]->ppNodes);n++)
			{
				if(ppTreeList[i]->ppGroups[g]->ppNodes[n] == pNode)
					return ppTreeList[i]->pchName;
			}
		}
	}

	return NULL;
}

void gclGenGetPowerNodeNodes(PTNodeTopDown *pNodeTop, PTNodeTopDown ***pppENodesOut)
{
	if(eaSize(&pNodeTop->ppNodes))
	{
		int i;

		for(i=0;i<eaSize(&pNodeTop->ppNodes);i++)
		{
			PTNodeDef *pNode = GET_REF(pNodeTop->ppNodes[i]->hNode);
			if(pNode && !(pNode->eFlag & kNodeFlag_HideNode))
				eaPush(pppENodesOut,pNodeTop->ppNodes[i]);
			gclGenGetPowerNodeNodes(pNodeTop->ppNodes[i],pppENodesOut);
		}
	}
}

int SortPTNodeTopDown(const PTNodeTopDown** ppA, const PTNodeTopDown** ppB)
{
	if (ppA && *ppA && ppB && *ppB)
	{
		PTNodeDef *pNodeA = GET_REF((*ppA)->hNode);
		PTNodeDef *pNodeB = GET_REF((*ppB)->hNode);
		return stricmp(
			TranslateDisplayMessage(pNodeA->pDisplayMessage), 
			TranslateDisplayMessage(pNodeB->pDisplayMessage));
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetSuggestedPowerGroupNodes");
bool gclGenExprGetSuggestedPowerGroupNodes(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID PTGroupTopDown *pGroupTop, SA_PARAM_OP_VALID Entity *pEnt, const char *pchClass)
{
	static PTNodeTopDown **s_eaNodes;
	S32 i, j;
	SpeciesDef *pSpeciesDef = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hSpecies) : NULL;
	eaClear(&s_eaNodes);
	for (i = 0; i < eaSize(&pGroupTop->ppOwnedNodes); i++)
	{
		PTNodeDef *pNode = GET_REF(pGroupTop->ppOwnedNodes[i]->hNode);
		PTNode *pCharNode = pEnt ? powertree_FindNode(pEnt->pChar, NULL, pNode->pchNameFull) : NULL;

		if (pSpeciesDef && eaSize(&pSpeciesDef->eaPowerSuggestions)
			&& pchClass && *pchClass
			&& (!pCharNode || pCharNode->iRank < 0))
		{
			SpeciesPowerSuggestion *pSuggestion = eaIndexedGetUsingString(&pSpeciesDef->eaPowerSuggestions, pchClass);
			if (!pSuggestion)
				continue;

			for (j = eaSize(&pSuggestion->eaNodes) - 1; j >= 0; j--)
			{
				if (GET_REF(pSuggestion->eaNodes[j]->hNodeDef) == pNode)
					break;
			}
			if (j < 0)
				continue;
		}

		if (!(pNode->eFlag & kNodeFlag_HideNode))
			eaPush(&s_eaNodes, pGroupTop->ppOwnedNodes[i]);

		gclGenGetPowerNodeNodes(pGroupTop->ppOwnedNodes[i],&s_eaNodes);
	}
	eaQSort(s_eaNodes, SortPTNodeTopDown);
	ui_GenSetManagedListSafe(pGen, &s_eaNodes, PTNodeTopDown, false);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerGroupNodes");
bool gclGenExprGetPowerGroupNodes(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID PTGroupTopDown *pGroupTop)
{
	return gclGenExprGetSuggestedPowerGroupNodes(pContext, pGen, pGroupTop, NULL, NULL);
}

static bool gclGenExprGetOwnedPowerGroupNodes_Internal(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_VALID PTGroupTopDown *pGroupTop, bool bShowHidden)
{
	static PTNodeTopDown **s_eaNodes;
	S32 i;
	eaClear(&s_eaNodes);
	if (pEnt)
	{
		for (i = 0; i < eaSize(&pGroupTop->ppOwnedNodes); i++)
		{
			PTNodeDef *pNode = GET_REF(pGroupTop->ppOwnedNodes[i]->hNode);
			PTNode *pN = powertree_FindNode(pEnt->pChar,NULL,pNode->pchNameFull);
			if (pN && (bShowHidden || !(pNode->eFlag & kNodeFlag_HideNode)))
				eaPush(&s_eaNodes, pGroupTop->ppOwnedNodes[i]);

			gclGenGetPowerNodeNodes(pGroupTop->ppOwnedNodes[i],&s_eaNodes);
		}
	}
	ui_GenSetManagedListSafe(pGen, &s_eaNodes, PTNodeTopDown, false);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetOwnedPowerGroupNodes");
bool gclGenExprGetOwnedPowerGroupNodes(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_VALID PTGroupTopDown *pGroupTop)
{
	return gclGenExprGetOwnedPowerGroupNodes_Internal(pContext, pGen, pEnt, pGroupTop, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetAllOwnedPowerGroupNodes");
bool gclGenExprGetAllOwnedPowerGroupNodes(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_VALID PTGroupTopDown *pGroupTop)
{
	return gclGenExprGetOwnedPowerGroupNodes_Internal(pContext, pGen, pEnt, pGroupTop, true);
}

static int gclPowerTreeSortListByType(const PowerTreeDef **powerTreeA, const PowerTreeDef **powerTreeB)
{
	PTTypeDef *pTypeA = GET_REF((*powerTreeA)->hTreeType);
	PTTypeDef *pTypeB = GET_REF((*powerTreeB)->hTreeType);

	if(!pTypeA || !pTypeB)
		return 0;

	if(pTypeA->iOrder > pTypeB->iOrder)
		return 1;
	else if(pTypeA->iOrder < pTypeB->iOrder)
		return -1;

	return 0;
}

// Get the rank of the given enhancement
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetEnhancementRank");
int gclGenGetEnhancementRank(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity, const char *pchNodeName, const char *pchEnhancementName)
{
	int i;
	PTNode *pNode = NULL;
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNodeName);
	Character *pChar = pEntity->pChar;
	PowerDef *pEnhDef = RefSystem_ReferentFromString("PowerDef",pchEnhancementName);

	for(i=0;i<eaSize(&pChar->ppPowerTrees);i++)
	{
		int n;

		for(n=0;n<eaSize(&pChar->ppPowerTrees[i]->ppNodes);n++)
		{
			if(GET_REF(pChar->ppPowerTrees[i]->ppNodes[n]->hDef) == pNodeDef)
			{
				pNode = pChar->ppPowerTrees[i]->ppNodes[n];
				break;
			}
		}

		if(pNode)
			break;
	}

	if(!pNode || pNode->bEscrow)
		return 0;

	i = powertreenode_FindEnhancementRankHelper(CONTAINER_NOCONST(PTNode, pNode),pEnhDef);

	return i;
}

// Set list to all enhancements associated with the given node of the particular type (e.g. "Advantage").
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetEnhancmentsOfType");
bool gclGenExprGenGetEnhancmentsOfType(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchNodeName, const char *pchEnhancementType)
{
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNodeName);
	PTEnhTypeDef *pEnhType = RefSystem_ReferentFromString("PTEnhTypeDef",pchEnhancementType);
	static PowerDef **s_eaEnhancements;
	int i;

	eaClear(&s_eaEnhancements);

	if (pNodeDef)
	{
		for(i=0;i<eaSize(&pNodeDef->ppEnhancements);i++)
		{
			if(GET_REF(pNodeDef->ppEnhancements[i]->hEnhType) == pEnhType)
			{
				eaPush(&s_eaEnhancements,GET_REF(pNodeDef->ppEnhancements[i]->hPowerDef));
			}
		}
	}

	ui_GenSetManagedListSafe(pGen,&s_eaEnhancements, PowerDef, false);

	return true;
}

// Set list to all enhancements associated with the given node
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetAllEnhancementsByPTNodeDef");
bool gclGenExprGenGetAllEnhancementsByPTNodeDef(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID PTNodeDef *pNodeDef)
{
	static PowerDef **s_eaEnhancements;
	int i;

	eaClear(&s_eaEnhancements);

	if (pNodeDef)
	{
		for(i=0;i<eaSize(&pNodeDef->ppEnhancements);i++)
		{
			eaPush(&s_eaEnhancements,GET_REF(pNodeDef->ppEnhancements[i]->hPowerDef));
		}
	}

	ui_GenSetManagedListSafe(pGen,&s_eaEnhancements, PowerDef,false);

	return pNodeDef && (eaSize(&pNodeDef->ppEnhancements) > 0);
}

// Set list to all enhancements associated with the given node
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetAllEnhancements");
bool gclGenExprGenGetAllEnhancements(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchNodeName)
{
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNodeName);
	return gclGenExprGenGetAllEnhancementsByPTNodeDef(pContext, pGen, pNodeDef);
}

// Set list to all owned enhancements associated with the given node
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetOwnedEnhancementsByPTNodeDef");
bool gclGenExprGenGetOwnedEnhancementsByPTNodeDef(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_OP_VALID PTNodeDef *pNodeDef)
{
	static PowerDef **s_eaEnhancements;
	int i;

	eaClear(&s_eaEnhancements);

	if (pNodeDef)
	{
		for(i=0;i<eaSize(&pNodeDef->ppEnhancements);i++)
		{
			PowerDef *pEnhancement = GET_REF(pNodeDef->ppEnhancements[i]->hPowerDef);
			const char *pchEnhancementName = SAFE_MEMBER(pEnhancement, pchName);
			if (gclGenGetEnhancementRank(pContext, pEntity, pNodeDef->pchNameFull, pchEnhancementName))
			{
				eaPush(&s_eaEnhancements,GET_REF(pNodeDef->ppEnhancements[i]->hPowerDef));
			}
		}
	}

	ui_GenSetManagedListSafe(pGen,&s_eaEnhancements, PowerDef,false);

	return pNodeDef && (eaSize(&pNodeDef->ppEnhancements) > 0);
}

// Set list to all owned enhancements associated with the given node
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetOwnedEnhancements");
bool gclGenExprGenGetOwnedEnhancements(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEntity, const char *pchNodeName)
{
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNodeName);
	return gclGenExprGenGetOwnedEnhancementsByPTNodeDef(pContext, pGen, pEntity, pNodeDef);
}

// Return true if this node has any enhancements or rank ups.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PowerNodeHasUpgrades");
bool gclGenExprPowerNodeHasUpgrades(ExprContext *pContext, const char *pchNodeName)
{
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNodeName);
	return pNodeDef && (eaSize(&pNodeDef->ppEnhancements) + eaSize(&pNodeDef->ppRanks) > 0);
}

// Return the points the player has currently spent on this node.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PowerNodeGetEnhancementPointsSpent");
S32 gclExprPowerNodeGetEnhancementPointsSpent(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEnt, const char *pchNodeName)
{
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNodeName);
	if (pEnt && pNodeDef)
		return entity_PowerTreeNodeEnhPointsSpentHelper(CONTAINER_NOCONST(Entity, pEnt), pNodeDef);
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetEnhanceableNodes");
bool gclGenExprGetEnhanceableNodes(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEntity)
{
	int i,n;
	static PTNodeDef **s_eaNodes;

	eaClear(&s_eaNodes);

	for(i=0;i<eaSize(&pEntity->pChar->ppPowerTrees);i++)
	{
		PowerTree *pTree = pEntity->pChar->ppPowerTrees[i];
		for(n=0;n<eaSize(&pTree->ppNodes);n++)
		{
			PTNode *pNode = pTree->ppNodes[n];
			PTNodeDef *pNodeDef = GET_REF(pNode->hDef);

			if (pNode->bEscrow)
				continue;

			if(eaSize(&pNodeDef->ppEnhancements))
			{
				eaPush(&s_eaNodes,pNodeDef);
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, &s_eaNodes, PTNodeDef, false);

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetEnhanceableNode");
bool gclGenExprSetEnhanceableNode(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchNodeName)
{
	static PTNodeDef **s_eaNodes;
	PTNodeDef *pNodeDef = RefSystem_ReferentFromString("PTNodeDef",pchNodeName);

	eaClear(&s_eaNodes);

	if(pNodeDef)
		eaPush(&s_eaNodes,pNodeDef);

	ui_GenSetManagedListSafe(pGen, &s_eaNodes, PTNodeDef, false);

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerTreeNamesByUICategory");
const char * gclGenExprGetPowerTreeNamesByUICategory(SA_PARAM_NN_VALID Entity *pEnt, S32 ePowerTreeUICategory, bool bShowOwned, bool bShowAvailable, S32 iMaxRecordsToReturn)
{
	S32 iTreeCount = 0;
	static char *pchCategoryNames = NULL;
	estrClear(&pchCategoryNames);

	if (iMaxRecordsToReturn == 0)
	{
		iMaxRecordsToReturn = INT_MAX;
	}

	if (pEnt && pEnt->pChar)
	{
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(s_eaTreeRefs, PowerTreeDefRef, pTreeDefRef)
		{
			PowerTreeDef *pTreeDef = GET_REF(pTreeDefRef->hRef);

			if (pTreeDef)
			{
				if (pTreeDef->eUICategory != ePowerTreeUICategory)
				{
					continue;
				}

				if (bShowOwned)
				{
					bool bFound = false;
					FOR_EACH_IN_CONST_EARRAY_FORWARDS(pEnt->pChar->ppPowerTrees, PowerTree, pTree)
					{
						if (GET_REF(pTree->hDef) == pTreeDef)
						{
							// Entity already owns the tree
							bFound = true;
							break;
						}
					}
					FOR_EACH_END

					if (!bFound)
					{
						continue;
					}
				}

				if (bShowAvailable && !character_CanBuyPowerTree(PARTITION_CLIENT, pEnt->pChar, pTreeDef))
				{
					// Not available to the entity
					continue;
				}

				// This is a power tree we're interested. Add to the list.
				if (iTreeCount > 0)
				{
					estrAppend2(&pchCategoryNames, " ");
				}
				estrAppend2(&pchCategoryNames, pTreeDef->pchName);
				++iTreeCount;

				if (iTreeCount == iMaxRecordsToReturn)
				{
					break;
				}
			}
		}
		FOR_EACH_END
	}

	return pchCategoryNames;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerTreeListByUICategory");
void gclGenExprGetPowerTreeListByUICategory(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEnt, S32 ePowerTreeUICategory, bool bExcludeOwned, bool bShowAvailable)
{
	if (pGen && pEnt && pEnt->pChar)
	{
		static PowerTreeDef **s_eaPowerTreeList = NULL;

		S32 iTreeCount = 0;

		FOR_EACH_IN_CONST_EARRAY_FORWARDS(s_eaTreeRefs, PowerTreeDefRef, pTreeDefRef)
		{
			PowerTreeDef *pTreeDef = GET_REF(pTreeDefRef->hRef);

			if (pTreeDef)
			{
				if (pTreeDef->eUICategory != ePowerTreeUICategory)
				{
					continue;
				}

				if (bExcludeOwned)
				{
					bool bFound = false;
					FOR_EACH_IN_CONST_EARRAY_FORWARDS(pEnt->pChar->ppPowerTrees, PowerTree, pTree)
					{
						if (GET_REF(pTree->hDef) == pTreeDef)
						{
							// Entity already owns the tree
							bFound = true;
							break;
						}
					}
					FOR_EACH_END

					if (bFound)
					{
						continue;
					}
				}

				if (bShowAvailable && !character_CanBuyPowerTree(PARTITION_CLIENT, pEnt->pChar, pTreeDef))
				{
					// Not available to the entity
					continue;
				}

				// This is a power tree we're interested. Add to the list.
				if (iTreeCount < eaSize(&s_eaPowerTreeList))
				{
					s_eaPowerTreeList[iTreeCount] = pTreeDef;
				}
				else
				{
					eaPush(&s_eaPowerTreeList, pTreeDef);
				}
				++iTreeCount;
			}
		}
		FOR_EACH_END

		// Set the final size of the array
		eaSetSize(&s_eaPowerTreeList, iTreeCount);

		ui_GenSetListSafe(pGen, &s_eaPowerTreeList, PowerTreeDef);
	}
	else if (pGen)
	{
		ui_GenSetListSafe(pGen, NULL, PowerTreeDef);
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerTreeList");
bool gclGenExprGetPowerTreeList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEntity)
{
	Character *pChar = pEntity->pChar;
	static PowerTreeDef **s_eaTrees;
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("PowerTreeDef");

	eaClear(&s_eaTrees);

	if (!pGen)
		return false; 
	if (pChar)
	{
		S32 i;

		for (i = 0; i < eaSize(&pChar->ppPowerTrees); i++)
		{
			PowerTree *pTree = eaGet(&pChar->ppPowerTrees, i);
			PowerTreeDef *pDef = pTree ? GET_REF(pTree->hDef) : NULL;
			if (pDef)
				eaPush(&s_eaTrees, pDef);
		}

		eaQSort(s_eaTrees,gclPowerTreeSortListByType);

		exprContextSetIntVar(pContext, "NewTreeLevel", eaSize(&s_eaTrees));

		for (i = 0; i < eaSize(&pStruct->ppReferents); i++)
		{
			PowerTreeDef *pDef = eaGet(&pStruct->ppReferents, i);
			if (pDef && character_CanBuyPowerTree(PARTITION_CLIENT, pChar, pDef))
				eaPushUnique(&s_eaTrees, pDef);
		}
	}
	ui_GenSetManagedListSafe(pGen, &s_eaTrees, PowerTreeDef, false);
	return true;
}

static struct PetDefRef **s_eaPetRefs = NULL;

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void gclGenAddPetRef(const char *pchPet)
{
	S32 i;
	PetDefRef *pRef;
	pchPet = allocAddString(pchPet);
	for (i = 0; i < eaSize(&s_eaPetRefs); i++)
		if (REF_STRING_FROM_HANDLE(s_eaPetRefs[i]->hPet) == pchPet)
			return;
	pRef = calloc(1, sizeof(*pRef));
	SET_HANDLE_FROM_STRING("PowerTreeDef", pchPet, pRef->hPet);
	eaPush(&s_eaPetRefs, pRef);
}

AUTO_COMMAND ACMD_HIDE ACMD_NAME("PetStore_RemoveRefs") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void gclGenRemovePetRefs()
{
	S32 i;
	for (i = eaSize(&s_eaPetRefs) - 1; i >= 0; i--)
	{
		PetDefRef *pRef = eaRemove(&s_eaPetRefs, i);
		REMOVE_HANDLE(s_eaPetRefs[i]->hPet);
		free(pRef);
	}
	eaDestroy(&s_eaPetRefs);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPetStoreList");
bool gclGenExprGetPetStoreList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{	
	int i;
	static PetDef **s_eaPetDefs;

	eaClear(&s_eaPetDefs);

	for(i=0;i<eaSize(&s_eaPetRefs);i++)
	{
		PetDef *pPetDef = GET_REF(s_eaPetRefs[i]->hPet);
		if(pPetDef)
			eaPush(&s_eaPetDefs,pPetDef);
	}
	ui_GenSetManagedListSafe(pGen, &s_eaPetDefs, PetDef, false);
	return true;
}

// Do not use, use EntGetPowerTreeNameWithType isntead
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPowerTreeNameWithType);
char *gclGenExprGetPowerTreeNameWithType(ExprContext *pContext,SA_PARAM_OP_VALID Entity *pEntity, ACMD_EXPR_DICT(PowerTreeTypeDef) const char *pchPowerTreeType);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetPowerTreeNameWithType);
char *gclGenExprGetPowerTreeNameWithType(ExprContext *pContext,SA_PARAM_OP_VALID Entity *pEntity, ACMD_EXPR_DICT(PowerTreeTypeDef) const char *pchPowerTreeType)
{
	PTTypeDef *pType = RefSystem_ReferentFromString("PowerTreeTypeDef", pchPowerTreeType);
	Character *pChar = pEntity ? pEntity->pChar : NULL;
	PowerTreeDef *pReturnTree = NULL;
	int i;

	if (!pEntity) return NULL;

	for(i=0;i<eaSize(&pChar->ppPowerTrees);i++)
	{
		PowerTreeDef *pTreeDef = GET_REF(pChar->ppPowerTrees[i]->hDef);
		if(pTreeDef && pType == GET_REF(pTreeDef->hTreeType))
		{
			return pTreeDef->pchName;
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetCurrentPowerTree");
bool gclGenExprGetCurrentPowerTree(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, ACMD_EXPR_DICT(PowerTreeDef) const char *pchPowerTree)
{
	PowerTreeDef *pTree = RefSystem_ReferentFromString("PowerTreeDef", pchPowerTree);
	static PowerTreeDef **s_eaPowerTrees;

	eaClear(&s_eaPowerTrees);
	eaPush(&s_eaPowerTrees, pTree);
	
	ui_GenSetManagedListSafe(pGen, &s_eaPowerTrees, PowerTreeDef, false);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenIsTreeOwned");
bool gclGenExprIsTreeOwned(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity, ACMD_EXPR_DICT(PowerTreeDef) const char *pchPowerTree)
{
	PowerTreeDef *pTree = RefSystem_ReferentFromString("PowerTreeDef", pchPowerTree);
	Character *pChar = pEntity ? pEntity->pChar : NULL;

	if(pChar)
	{
		S32 i;
		for(i=0;i<eaSize(&pChar->ppPowerTrees); i++)
		{
			if(GET_REF(pChar->ppPowerTrees[i]->hDef) == pTree)
				return true;
		}
	}

	return false;

}

// Add a reference to the given power tree; this is called by the
// server to force the client to get a power tree.
AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void gclGenAddPowerTreeRef(const char *pchTree)
{
	S32 i;
	PowerTreeDefRef *pRef;
	pchTree = allocAddString(pchTree);
	for (i = 0; i < eaSize(&s_eaTreeRefs); i++)
		if (REF_STRING_FROM_HANDLE(s_eaTreeRefs[i]->hRef) == pchTree)
			return;
	pRef = calloc(1, sizeof(*pRef));
	SET_HANDLE_FROM_STRING("PowerTreeDef", pchTree, pRef->hRef);
	eaPush(&s_eaTreeRefs, pRef);
}

// Add a reference to a list of power trees.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME("PowerTreeAddRefs");
void gclGenCmdPowerTreeAddRefs(ACMD_SENTENCE pchTrees)
{
	char *pchContext;
	char *pchStart = strtok_r(pchTrees, " \t\n\r", &pchContext);
	do 
	{
		gclGenAddPowerTreeRef(pchStart);
	} while (pchStart = strtok_r(NULL, " \t\n\r", &pchContext));
}

// Clear all references added by gclGenAddPowerTreeRef.
AUTO_COMMAND ACMD_HIDE ACMD_NAME("PowerTree_RemoveRefs") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void gclGenRemovePowerTreeRefs()
{
	S32 i;
	for (i = eaSize(&s_eaTreeRefs) - 1; i >= 0; i--)
	{
		PowerTreeDefRef *pRef = eaRemove(&s_eaTreeRefs, i);
		REMOVE_HANDLE(s_eaTreeRefs[i]->hRef);
		free(pRef);
	}
	eaDestroy(&s_eaTreeRefs);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerHasPowerCategory");
S32 gclGenExprPowerHasPowerCategory(const char* pchPowerDefName, ACMD_EXPR_ENUM(PowerCategory) const char *categoryName)
{
	PowerDef* pDef = powerdef_Find(pchPowerDefName);
	if(pDef && pDef->piCategories)
	{
		int eCat = StaticDefineIntGetInt(PowerCategoriesEnum,categoryName);
		if (eCat >= 0)
		{
			return eaiFind(&pDef->piCategories, eCat) >= 0;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerGetPurposeName");
const char *gclGenExprPowerGetPurposeName(SA_PARAM_OP_VALID PowerDef* pPowerDef)
{
	if (pPowerDef==NULL )
		return "";

	return StaticDefineIntRevLookup(PowerPurposeEnum, pPowerDef->ePurpose);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerGetPurposeNameByName");
const char *gclGenExprPowerGetPurposeNameByName(const char* pchPowerDefName)
{
	PowerDef* pDef = powerdef_Find(pchPowerDefName);
	return gclGenExprPowerGetPurposeName( pDef );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerGetPurposeNameByID");
const char *gclGenExprPowerGetPurposeNameByID(S32 iID)
{
	return StaticDefineIntRevLookup(PowerPurposeEnum, iID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerIconFromPower");
const char *gclGenExprPowerIconFromPower(SA_PARAM_OP_VALID Power* pPower)
{
	PowerDef *pPowerDef = pPower ? GET_REF(pPower->hDef) : NULL;
	if (pPowerDef)
	{
		return gclGetBestIconName(pPowerDef->pchIconName, "Power_Generic");
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerIconFromPowerName");
const char *gclGenExprPowerIconFromPowerName(const char *pchPower)
{
	PowerDef *pPowerDef = RefSystem_ReferentFromString("PowerDef",pchPower);
	if (pPowerDef)
	{
		return gclGetBestIconName(pPowerDef->pchIconName, "Power_Generic");
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerIconFromNodeRank");
const char *gclGenExprPowerIconFromNodeRank(const char *pchNode, S32 iRank)
{
	PTNodeDef *pNode = RefSystem_ReferentFromString("PowerTreeNodeDef", pchNode);
	return PowerTreeNodeGetIcon(pNode, iRank);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerNodeCanEnhance");
bool gclGenExprPowerNodeCanEnhance(SA_PARAM_OP_VALID Entity *pEnt,const char *pchTree, const char *pchNode, const char *pchEnhancement)
{
	bool bResult = false;
	if (pEnt && pEnt->pChar)
	{
		bResult = character_CanEnhanceNode(PARTITION_CLIENT,pEnt->pChar,pchTree,pchNode,pchEnhancement,true);
	}

	return bResult;
}

bool gclPowerNodeCanBuyForEnt(SA_PARAM_OP_VALID Entity *pBuyer, SA_PARAM_OP_VALID Entity *pEnt, 
							  const char *pchNode, const char *pchGroup, S32 iRank, 
							  S32 bRequireNextRank, S32 bCheckGroupMax, S32 bIsTraining)
{
	bool bResult = false;

	if (pEnt && pEnt->pChar)
	{
		PTNodeDef *pNode = RefSystem_ReferentFromString("PowerTreeNodeDef", pchNode);
		PTGroupDef *pGroup = RefSystem_ReferentFromString("PowerTreeGroupDef", pchGroup);
		if (pNode && pGroup)
		{
			bResult = entity_CanBuyPowerTreeNodeHelper(ATR_EMPTY_ARGS, PARTITION_CLIENT, CONTAINER_NOCONST(Entity, pBuyer), CONTAINER_NOCONST(Entity, pEnt), pGroup, pNode, iRank, bRequireNextRank, bCheckGroupMax, false, bIsTraining);
		}
	}
	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerNodeCanBuyForEnt");
bool gclGenExprPowerNodeCanBuyForEnt(SA_PARAM_OP_VALID Entity *pBuyer, SA_PARAM_OP_VALID Entity *pEnt, 
									 const char *pchNode, const char *pchGroup, S32 iRank, 
									 S32 bCheckGroupMax, S32 bIsTraining)
{
	return gclPowerNodeCanBuyForEnt(pBuyer, pEnt, pchNode, pchGroup, iRank, false, bCheckGroupMax, bIsTraining);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerNodeCanBuy");
bool gclGenExprPowerNodeCanBuy(SA_PARAM_OP_VALID Entity *pEnt, const char *pchNode, const char *pchGroup, S32 iRank)
{
	return gclPowerNodeCanBuyForEnt(pEnt, pEnt, pchNode, pchGroup, iRank, true, true, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerNodeGetRank");
S32 gclGenExprPowerNodeGetRank(SA_PARAM_OP_VALID Entity *pEnt, const char *pchNode)
{
	if (pEnt && pEnt->pChar)
	{
		PTNode *pNode = powertree_FindNode(pEnt->pChar, NULL, pchNode);
		if (pNode && !pNode->bEscrow)
			return pNode->iRank;
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerNodeGetDisplayNameFromName");
const char* gclGenExprPowerNodeGetDisplayNameFromName(const char *pchNode)
{
	PTNodeDef *pNodeDef = RefSystem_ReferentFromString("PowerTreeNodeDef", pchNode);

	return powertreenodedef_GetDisplayName( pNodeDef );
}

// Get the number of points an entity is able to spend from a given power table,
// or 0 if that table does not exist.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntityGetPowerTablePoints");
S32 gclGenExprEntityGetPowerTablePoints(SA_PARAM_OP_VALID Entity *pEnt, const char *pchTable)
{
	S32 iResult = 0;

	if (pEnt && pEnt->pChar)
	{
		iResult = entity_PointsRemaining(NULL, CONTAINER_NOCONST(Entity, pEnt), NULL, pchTable);
	}
	return iResult;
}

// Get the number of points an entity is able to spend from a given power table,
// or 0 if that table does not exist.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntityGetPowerTablePointsSpent");
S32 gclGenExprEntityGetPowerTablePointsSpent(SA_PARAM_OP_VALID Entity *pEnt, const char *pchTable)
{
	S32 iResult = 0;

	if (pEnt && pEnt->pChar)
	{
		iResult = entity_PointsSpent(CONTAINER_NOCONST(Entity, pEnt), pchTable);
	}
	return iResult;
}

// Get the number of points an entity has total, both available to spend and spent,
// from a given power table, or 0 if that table does not exist
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntityGetPowerTablePointsTotal");
S32 gclGenExprEntityGetPowerTablePointsTotal(SA_PARAM_OP_VALID Entity *pEnt, const char *pchTable)
{
	S32 iResult = 0;

	if (pEnt && pEnt->pChar)
	{
		iResult = entity_PointsSpent(CONTAINER_NOCONST(Entity, pEnt), pchTable) +
				  entity_PointsRemaining(NULL, CONTAINER_NOCONST(Entity, pEnt), NULL, pchTable);
	}
	return iResult;
}

// Deprecated; use EntityGetPowerTablePoints(Entity, "TreePoints").
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntityGetTreePoints");
S32 gclGenExprEntityGetTreePoints(SA_PARAM_OP_VALID Entity *pEnt)
{
	return gclGenExprEntityGetPowerTablePoints(pEnt, "TreePoints");
}

// Entity expressions

// Returns the combat level of the Entity.  Returns 0 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetLevelCombat");
S32 exprEntGetLevelCombat(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		return pEntity->pChar->iLevelCombat;
	}
	return 0;
}

// Returns the experience level of the Entity.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetLevelExp");
S32 exprEntGetLevelExp(SA_PARAM_OP_VALID Entity *pEntity)
{
	return entity_GetSavedExpLevel(pEntity);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetLevelExpScaled");
S32 exprEntGetLevelExpScaled(SA_PARAM_OP_VALID Entity *pEntity)
{
	ItemDef* pItemDef = item_DefFromName(gConf.pcLevelingNumericItem);
	F32 fScale = 1.0f;
	if (pItemDef)
	{
		fScale = pItemDef->fScaleUI;
	}
	return entity_GetSavedExpLevel(pEntity) * fScale;
}


// Returns the value of the Entity's Character's attribute.  Returns 0 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetOfflineAttrib");
F32 exprEntGetOfflineAttrib(SA_PARAM_OP_VALID Entity *pEntity,
							ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	if (!pEntity) return 0;
	if (entGetType(pEntity) != GLOBALTYPE_ENTITYSAVEDPET)
	{
		return exprEntGetAttrib(pEntity, attribName);
	}
	return exprEntGetAttrib(savedpet_GetOfflineCopy(entGetContainerID(pEntity)), attribName);
}

// Returns the value of the Entity's Character's base regen rate of the specified AttribPool.  Returns 0 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetAttribPoolRegenRate");
F32 exprEntGetAttribPoolRegenRate(SA_PARAM_OP_VALID Entity *pEntity,
									ACMD_EXPR_ENUM(AttribType) const char *curAttribName)
{
	if (g_iAttribPoolCount)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,curAttribName);
		if(eAttrib >= 0)
		{
			int i;
			AttribPool *ppool = NULL;

			// Find the attrib pool
			for(i=0; i<g_iAttribPoolCount; i++)
			{
				ppool = g_AttribPools.ppPools[i];
				if(ppool->eAttribCur == eAttrib && ppool->combatPool.pTarget)
				{
					return ppool->combatPool.pTarget->fMagRegen;
				}
			}
		}
	}
	return 0;
}

static F32 fPrevHP = -FLT_MAX;

// Returns true if the player just lost some health. This function should be called on every frame.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayerLostHealth");
bool gclGenExprPlayerLostHealth(void)
{
	Entity *pEntity = entActivePlayerPtr();

	if (pEntity)
	{				
		F32 fCurrentHP = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fHitPoints);
		F32 fMaxHP = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fHitPointsMax);

		// Make sure the current HP is less than the max HP.
		// There might be buffs setting your max HP.
		bool bLostHealth = fCurrentHP < fMaxHP && fPrevHP > fCurrentHP;

		// Set the previous HP to the current one for the check in the next frame.
		fPrevHP = fCurrentHP;

		return bLostHealth;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ResetPlayerLostHealthState");
void gclGenExprResetPlayerLostHealthState(void)
{
	fPrevHP = -FLT_MAX;
}


// Returns the health percent of the Entity [0..1].  Returns 1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetHealthPercent");
F32 exprEntGetHealthPercent(SA_PARAM_OP_VALID Entity *pEntity)
{
	F32 fHP = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fHitPointsMax);
	return fHP > 0 ? pEntity->pChar->pattrBasic->fHitPoints / fHP : 1;
}

// Returns the health points of the Entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetHealthPoints");
S32 exprEntGetHealthPoints(SA_PARAM_OP_VALID Entity *pEntity)
{
	F32 fHP = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fHitPoints);
	return (S32)(fHP < 1 ? ceil(MAX(0, fHP)) : fHP);
}

// Returns the maximum health points of the Entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetHealthPointsMax");
S32 exprEntGetHealthPointsMax(SA_PARAM_OP_VALID Entity *pEntity)
{
	F32 fHP = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fHitPointsMax);
	return (S32)(fHP < 1 ? ceil(MAX(0, fHP)) : fHP);
}

// Returns the base maximum health points of the Entity's class
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetClassBaseHealthPointsMax");
S32 exprEntGetClassBaseHealthPointsMax(SA_PARAM_OP_VALID Entity *pEntity)
{
	F32 fHP = 0;
	if(pEntity && pEntity->pChar)
	{
		AttribAccrualSet *pSet;
		fHP = character_GetClassAttrib(pEntity->pChar,kClassAttribAspect_Basic,kAttribType_HitPointsMax);
		pSet = character_GetInnateAccrual(PARTITION_CLIENT,pEntity->pChar,NULL);
		if(pSet)
			fHP += pSet->CharacterAttribs.attrBasicAbs.fHitPointsMax;
	}
	return (S32)(fHP < 1 ? ceil(MAX(0, fHP)) : fHP);
}

// Returns the health points of the Entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetOfflineHealthPoints");
S32 exprEntGetOfflineHealthPoints(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (!pEntity) return 0;
	if (entGetType(pEntity) != GLOBALTYPE_ENTITYSAVEDPET)
	{
		return exprEntGetHealthPoints(pEntity);
	}
	return exprEntGetHealthPoints(savedpet_GetOfflineOrNotCopy(entGetContainerID(pEntity)));
}

// Returns the maximum health points of the Entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetOfflineHealthPointsMax");
S32 exprEntGetOfflineHealthPointsMax(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (!pEntity) return 0;
	if (entGetType(pEntity) != GLOBALTYPE_ENTITYSAVEDPET)
	{
		return exprEntGetHealthPointsMax(pEntity);
	}
	return exprEntGetHealthPointsMax(savedpet_GetOfflineOrNotCopy(entGetContainerID(pEntity)));
}

// Returns the power percent of the Entity [0..1].  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerPercent");
F32 exprEntGetPowerPercent(SA_PARAM_OP_VALID Entity *pEntity)
{
	F32 fPower = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fPowerMax);
	return fPower > 0 ? pEntity->pChar->pattrBasic->fPower / fPower : -1;
}
// Returns the power percent of the Entity [0..1].  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerFloat");
F32 exprEntGetPowerFloat(SA_PARAM_OP_VALID Entity *pEntity)
{
	return SAFE_MEMBER3(pEntity, pChar, pattrBasic, fPower); 
}

// returns the current number of power points of the entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerPoints");
S32 exprEntGetPowerPoints(SA_PARAM_OP_VALID Entity *pEntity)
{
	F32 fPower = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fPower);
	return (S32)(fPower < 1 ? ceil(MAX(0, fPower)) : fPower);
}

// returns the current number of power points of the entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerPointsFloor");
S32 exprEntGetPowerPointsFloor(SA_PARAM_OP_VALID Entity *pEntity)
{
	F32 fPower = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fPower);
	return (S32)(fPower < 1 ? floor(MAX(0, fPower)) : fPower);
}

// returns the maximum number of power points of the entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerPointsMax");
S32 exprEntGetPowerPointsMax(SA_PARAM_OP_VALID Entity *pEntity)
{
	F32 fPower = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fPowerMax);
	return (S32)(fPower < 1 ? ceil(MAX(0, fPower)) : fPower);
}

// Returns the power equilibrium percent of the Entity [0..1].  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerNotch");
F32 exprEntGetPowerNotch(SA_PARAM_OP_VALID Entity *pEntity)
{
	F32 fPower = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fPowerMax);
	return fPower > 0 ? pEntity->pChar->pattrBasic->fPowerEquilibrium / fPower : -1;
}

// Returns the exp percent of the Entity [0..1] to its next level.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetExpPercent");
F32 exprEntGetExpPercent(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		return entity_PercentToNextExpLevel(pEntity);
	}
	return -1.0f;
}

// Returns the max player level
AUTO_EXPR_FUNC(UIGEN) ACMD_NAME("GetMaxPlayerLevel");
S32 exprGetMaxUserLevel(void)
{
	return MAX_USER_LEVEL;
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetExpOfLevel");
S32 exprGenGetExpOfLevel(int iLevel)
{
	return NUMERIC_AT_LEVEL(iLevel);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetExpOfNextLevel");
S32 exprEntGetExpOfNextLevel(SA_PARAM_OP_VALID Entity *pEntity)
{
	if ( pEntity && pEntity->pChar )
	{
		return entity_ExpOfNextExpLevel(pEntity);
	}
	return -1.0f;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetExpToNextLevel");
S32 exprEntGetExpToNextLevel(SA_PARAM_OP_VALID Entity *pEntity)
{
	if ( pEntity && pEntity->pChar )
	{
		return entity_ExpToNextExpLevel(pEntity);
	}
	return -1.0f;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetExpOfNextLevelScaled");
S32 exprEntGetExpOfNextLevelScaled(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		ItemDef* pItemDef = item_DefFromName(gConf.pcLevelingNumericItem);
		F32 fScale = 1.0f;
		if (pItemDef)
		{
			fScale = pItemDef->fScaleUI;
		}
		return ceilf(entity_ExpOfNextExpLevel(pEntity) * fScale);
	}
	return -1.0f;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetExpToNextLevelScaled");
S32 exprEntGetExpToNextLevelScaled(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		ItemDef* pItemDef = item_DefFromName(gConf.pcLevelingNumericItem);
		F32 fScale = 1.0f;
		if (pItemDef)
		{
			fScale = pItemDef->fScaleUI;
		}
		return ceilf(entity_ExpToNextExpLevel(pEntity) * fScale);
	}
	return -1.0f;
}

// Returns 1 if the Entity is a Player
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsPlayer");
S32 exprEntGetIsPlayer(SA_PARAM_OP_VALID Entity *pEntity)
{
	return (pEntity && entCheckFlag(pEntity,ENTITYFLAG_IS_PLAYER));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsVanityPet");
S32 exprEntGetIsVanityPet(SA_PARAM_OP_VALID Entity *pEntity)
{
	return (pEntity && entCheckFlag(pEntity,ENTITYFLAG_VANITYPET));
}

// Returns 1 if the Entity is a Critter of a specific rank
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsCritterRank");
bool exprEntGetIsCritterRank(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID const char *pchRank)
{
	if (pEntity && pEntity->pCritter) {
		if (!pEntity->pCritter->pcRank) {
			return !stricmp_safe(g_pcCritterDefaultRank, pchRank);
		} else {
			return !stricmp_safe(pEntity->pCritter->pcRank, pchRank);
		}
	}
	return 0;
}

// Return the pet's quality. Returns -1 if this is not a super critter pet.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPetQuality");
S32 exprEntGetPetQuality(SA_PARAM_OP_VALID Entity *pEntity){
	MultiVal mvIntVal = {0};
	bool isPet = entGetUIVar(pEntity, "Quality", &mvIntVal);
	if (isPet){
		return mvIntVal.int32;
	}
	return -1;
}

// Finds a PowerDef / With AttribMod for the Stat / In the PowerTree
// PowerDef Entity owns / Give a Tag for filtering / No guarantee made
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetPowerDefWithAttribInTree);
SA_RET_OP_VALID PowerDef *exprEntGetPowerDefForAttribInTree(SA_PARAM_OP_VALID Entity *pEntity, 
														const char *pchTreeName, 
														ACMD_EXPR_ENUM(AttribType) const char *pchStat, 
														const char *pchTag)
{
	if (pEntity && pEntity->pChar && eaSize(&pEntity->pChar->ppPowerTrees) && *pchTreeName && *pchStat) {
		S32 i, j, k, l;
		AttribType eStat = StaticDefineIntGetInt(AttribTypeEnum, pchStat);
		S32 iTag = StaticDefineIntGetInt(PowerTagsEnum, pchTag);

		for (i = 0; i < eaSize(&pEntity->pChar->ppPowerTrees) && eStat != -1; i++) {
			PowerTree *pPowerTree = pEntity->pChar->ppPowerTrees[i];
			PowerTreeDef *pPowerTreeDef = GET_REF(pPowerTree->hDef);
			if (!pPowerTreeDef || stricmp(pPowerTreeDef->pchName, pchTreeName) != 0)
				continue;

			for (j = 0; j < eaSize(&pPowerTree->ppNodes); j++) {
				PTNode *pNode = pPowerTree->ppNodes[j];
				if (pNode->bEscrow)
					continue;

				for (k = 0; k < eaSize(&pNode->ppPowers); k++) {
					Power *pPower = pNode->ppPowers[k];
					PowerDef *pPowerDef = GET_REF(pPower->hDef);
					if (!pPowerDef)
						continue;

					for (l = 0; l < eaSize(&pPowerDef->ppOrderedMods); l++) {
						AttribModDef *pMod = pPowerDef->ppOrderedMods[l];
						if (pMod->offAttrib == eStat) {
							if (iTag != -1 && eaiFind(&pMod->tags.piTags, iTag) < 0)
								continue;

							return pPowerDef;
						}
					}
				}
			}

			break;
		}
	}
	return NULL;
}

// Similar to EntGetPowerDefWithAttribInTree, except it looks for the Nth power with.
// The order in which stats are returned is unspecified.
// Returns an empty string if it cannot determine a stat.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetAttribFromPowerDefInTree);
const char *exprEntGetAttribFromPowerDefInTree(SA_PARAM_OP_VALID Entity *pEntity, const char *pchTreeName, const char *pchTag, S32 iDef)
{
	if (pEntity && pEntity->pChar && eaSize(&pEntity->pChar->ppPowerTrees) && *pchTreeName) {
		static PTNodeDef **s_eaOwnedNodes = NULL;
		S32 i, j, k, m;
		S32 iTag = StaticDefineIntGetInt(PowerTagsEnum, pchTag);
		CharacterPath** eaPaths = NULL;
		int iPath;

		eaStackCreate(&eaPaths, eaSize(&pEntity->pChar->ppSecondaryPaths) + 1);
		entity_GetChosenCharacterPaths(pEntity, &eaPaths);

		eaClear(&s_eaOwnedNodes);

		for (i = 0; i < eaSize(&pEntity->pChar->ppPowerTrees); i++) {
			PowerTree *pPowerTree = pEntity->pChar->ppPowerTrees[i];
			PowerTreeDef *pPowerTreeDef = GET_REF(pPowerTree->hDef);
			if (!pPowerTreeDef || stricmp(pPowerTreeDef->pchName, pchTreeName) != 0)
				continue;

			for (j = 0; j < eaSize(&pPowerTree->ppNodes); j++) {
				PTNode *pNode = pPowerTree->ppNodes[j];
				if (pNode->bEscrow)
					continue;

				if (iDef > 0) {
					if (eaSize(&eaPaths) > 0 && GET_REF(pNode->hDef)) {
						eaPush(&s_eaOwnedNodes, GET_REF(pNode->hDef));
					}
					iDef--;
					continue;
				}

				// look through all the powers
				for (k = 0; k < eaSize(&pNode->ppPowers); k++) {
					Power *pPower = pNode->ppPowers[k];
					PowerDef *pPowerDef = GET_REF(pPower->hDef);
					if (!pPowerDef)
						continue;

					for (m = 0; m < eaSize(&pPowerDef->ppOrderedMods); m++) {
						AttribModDef *pMod = pPowerDef->ppOrderedMods[m];
						if (iTag != -1 && eaiFind(&pMod->tags.piTags, iTag) < 0)
							continue;
						return StaticDefineIntRevLookupNonNull(AttribTypeEnum, pMod->offAttrib);
					}
				}

				break;
			}

			break;
		}

		// If there is a character path, then get suggestions from there.
		for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
		{
			for (i = 0; i < eaSize(&eaPaths[iPath]->eaSuggestedPurchases); i++) {
				CharacterPathSuggestedPurchase *pSuggested = eaPaths[iPath]->eaSuggestedPurchases[i];
				for (j = 0; j < eaSize(&pSuggested->eaChoices); j++) {
					CharacterPathChoice *pChoice = pSuggested->eaChoices[j];
					for (k = 0; k < eaSize(&pChoice->eaSuggestedNodes); k++) {
						CharacterPathSuggestedNode *pNode = pChoice->eaSuggestedNodes[k];
						PTNodeDef *pNodeDef = GET_REF(pNode->hNodeDef);
						PTNodeRankDef *pRank = pNodeDef && eaSize(&pNodeDef->ppRanks) > 0 ? pNodeDef->ppRanks[0] : NULL;
						PowerDef *pPowerDef = pRank ? GET_REF(pRank->hPowerDef) : NULL;
						PowerTreeDef *pPowerTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
						if (!pPowerDef || !pPowerTreeDef || stricmp(pPowerTreeDef->pchName, pchTreeName) != 0)
							continue;

						// Check to see if it's already been counted
						if (eaFind(&s_eaOwnedNodes, pNodeDef) >= 0) {
							continue;
						}

						if (iDef > 0) {
							iDef--;
							continue;
						}

						for (m = 0; m < eaSize(&pPowerDef->ppOrderedMods); m++) {
							AttribModDef *pMod = pPowerDef->ppOrderedMods[m];
							if (iTag != -1 && eaiFind(&pMod->tags.piTags, iTag) < 0)
								continue;
							return StaticDefineIntRevLookupNonNull(AttribTypeEnum, pMod->offAttrib);
						}
					}
				}
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetCritterRankOrder");
S32 exprEntGetCritterRankOrder(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pCritter) {
		return critterRankGetOrder(pEntity->pCritter->pcRank);
	}
	return 0;
}

// Returns 1 if the Entity is a Critter of a specific subrank
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsCritterSubRank");
bool exprEntGetIsCritterSubRank(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pchRank)
{
	if (pEntity && pEntity->pCritter)
		return !stricmp_safe(pEntity->pCritter->pcSubRank, pchRank);
	return 0;
}

// If the Entity has become another Critter via a BecomeCritter AttribMod, this returns
//  the internal name of the CritterDef
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetHasBecomeCritter");
const char* exprEntGetBeingCritter(SA_PARAM_OP_VALID Entity *pEntity)
{
	if(pEntity && pEntity->pChar && pEntity->pChar->bBecomeCritter)
	{
		// Bit of a hacky solution, since we don't currently actually track the CritterDef
		//  explicitly, we just take the last one the mod array
		int i;
		for(i=eaSize(&pEntity->pChar->ppModsNet)-1; i>=0; i--)
		{
			AttribModDef *pModDef;

			if(!ATTRIBMODNET_VALID(pEntity->pChar->ppModsNet[i]))
				continue;
			
			pModDef = modnet_GetDef(pEntity->pChar->ppModsNet[i]);
			
			if(pModDef && pModDef->offAttrib==kAttribType_BecomeCritter && pModDef->pParams)
			{
				BecomeCritterParams *pParams = (BecomeCritterParams*)pModDef->pParams;
				return REF_STRING_FROM_HANDLE(pParams->hCritter);
			}
		}
	}
	return "";
}

// Returns number of seconds left on the Entity's BattleForm timer
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBattleFormTimer");
S32 exprEntGetBattleFormTimer(SA_PARAM_OP_VALID Entity *pEntity)
{
	if(pEntity && pEntity->pChar && pEntity->pChar->uiTimeBattleForm)
	{
		U32 uiNow = timeServerSecondsSince2000();
		if(pEntity->pChar->uiTimeBattleForm > uiNow)
			return pEntity->pChar->uiTimeBattleForm - uiNow;
	}
	return 0;
}

// Returns 1 if the Entity can toggle BattleForm now
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBattleFormToggle");
S32 exprEntGetBattleFormToggle(SA_PARAM_OP_VALID Entity *pEntity)
{
	return (pEntity && pEntity->pChar && character_CanToggleBattleForm(pEntity->pChar));
}

AUTO_EXPR_FUNC(UIGen);
F32 AttribModMagnitudePct(SA_PARAM_OP_VALID Entity *pent,
						ACMD_EXPR_ENUM(AttribType) const char* attribName,
						S32 index)
{
	if (pent)
	{
		int attrib = StaticDefineIntGetInt(AttribTypeEnum, attribName);

		AttribModNet* pmodNet = character_ModsNetGetByIndexAndTag(pent->pChar, attrib, NULL, index);

		if (pmodNet)
		{
			if(attrib == kAttribType_Shield && pmodNet->iHealthMax)
			{
				return (F32)pmodNet->iHealth / (F32)pmodNet->iHealthMax;
			}
			else if(pmodNet->iMagnitudeOriginal)
			{
				return (F32)pmodNet->iMagnitude / (F32)pmodNet->iMagnitudeOriginal;
			}
			else
			{
				return 1;
			}
		}
	}
	return -1.f;
}

AUTO_EXPR_FUNC(UIGen);
F32 OfflineAttribModMagnitudePct(SA_PARAM_OP_VALID Entity *pent,
								 ACMD_EXPR_ENUM(AttribType) const char* attribName,
								 S32 index)
{
	if(pent && entGetType(pent) == GLOBALTYPE_ENTITYSAVEDPET)
		pent = savedpet_GetOfflineOrNotCopy(entGetContainerID(pent));

	if(!pent)
		return 0;

	return AttribModMagnitudePct(pent,attribName,index);
}

AUTO_EXPR_FUNC(UIGen);
F32 ShieldGetNearestYaw(SA_PARAM_OP_VALID Entity* pEnt, F32 fAngle)
{
	if (pEnt)
	{
		AttribModNet* pNet = ShieldPctOrientedAttrib(pEnt->pChar, fAngle);
		if (pNet)
		{
			AttribModDef* pDef = modnet_GetDef(pNet);
			if (pDef)
			{
				return pDef->fYaw;
			}
		}
	}
	return 0.0f;
}

AUTO_EXPR_FUNC(UIGen);
F32 OfflineShieldPctOriented(SA_PARAM_OP_VALID Entity *pent,
								 F32 angle)
{
	if(pent && entGetType(pent) == GLOBALTYPE_ENTITYSAVEDPET)
		pent = savedpet_GetOfflineOrNotCopy(entGetContainerID(pent));

	if(!pent)
		return 0;

	return ShieldPctOriented(pent,angle);
}

AUTO_EXPR_FUNC(UIGen);
F32 TrueOfflineShieldPctOriented(SA_PARAM_OP_VALID Entity *pent,
								 F32 angle)
{
	if(pent)
		pent = savedpet_GetOfflineCopy(entGetContainerID(pent));

	if(!pent)
		return 0;

	return ShieldPctOriented(pent,angle);
}

AUTO_EXPR_FUNC(UIGen);
F32 ShieldPctOriented(SA_PARAM_OP_VALID Entity *pEnt, F32 fAngle)
{
	if (pEnt)
	{
		return ShieldPct(pEnt->pChar, fAngle);
	}
	return -1.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowerDefModifiesAttrib);
bool exprPowerDefModifiesAttrib( SA_PARAM_OP_VALID PowerDef *pDef,
					   ACMD_EXPR_ENUM(AttribType) const char* attribName)
{
	AttribType eType = StaticDefineIntGetInt(AttribTypeEnum, attribName);
	if(pDef && pDef->ppOrderedMods && eType > -1)
	{
		int i;
		for(i = 0; i < eaSize(&pDef->ppOrderedMods); i++)
		{
			if(pDef->ppOrderedMods[i]->offAttrib == eType)
			{
				return true;
			}
		}
	}
	return false;
}

S32 AttribModMagnitudeEx(Entity *pent, const char* attribName, S32 index, S32 *piTags)
{
	if (pent)
	{
		int attrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		
		AttribModNet* pmodNet = character_ModsNetGetByIndexAndTag(pent->pChar, attrib, piTags, index);

		if ( pmodNet )
		{
			if(attrib == kAttribType_Shield && pmodNet->iHealth)
			{
				return pmodNet->iHealth;
			}
			else
			{
				return (S32)(pmodNet->iMagnitude/ATTRIBMODNET_MAGSCALE);
			}
		}
	}
	
	return -1;
}

AUTO_EXPR_FUNC(UIGen);
S32 AttribModMagnitude(SA_PARAM_OP_VALID Entity *pent,
					   ACMD_EXPR_ENUM(AttribType) const char* attribName,
					   S32 index)
{
	return AttribModMagnitudeEx(pent, attribName, index, NULL);
}

AUTO_EXPR_FUNC(UIGen);
S32 AttribModMagnitudeOriginal(	SA_PARAM_OP_VALID Entity *pent,
							   ACMD_EXPR_ENUM(AttribType) const char* attribName,
							   S32 index)
{
	if (pent)
	{
		int attrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		
		AttribModNet* pmodNet = character_ModsNetGetByIndexAndTag(pent->pChar, attrib, NULL, index);

		if ( pmodNet )
		{
			if(attrib == kAttribType_Shield && (pmodNet->iHealth || pmodNet->iHealthMax))
			{
				return pmodNet->iHealthMax ? pmodNet->iHealthMax : pmodNet->iHealth;
			}
			else
			{
				return (S32)((pmodNet->iMagnitudeOriginal ? pmodNet->iMagnitudeOriginal : pmodNet->iMagnitude)/ATTRIBMODNET_MAGSCALE);
			}
		}
	}
	
	return -1;
}

// Returns 1 if the Entity is a Critter of at least a specific rank
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsCritterAtLeastRank");
bool exprEntGetIsCritterAtLeastRank(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID const char *pchRank)
{
	pchRank = allocFindString(pchRank);
	if (pEntity && pEntity->pCritter && pchRank)
	{
		return (critterRankGetOrder(pchRank) <= critterRankGetOrder(pEntity->pCritter->pcRank));
	}
	else
		return 0;
}

// Returns a conning rank compared to the player.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetConningLevel);
int gclEntGetConningLevel(SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Entity *pEntity)
{
	int iRet;
	if (pPlayer == NULL || pPlayer->pChar == NULL || pEntity == NULL || pEntity->pChar == NULL)
		return 0;

	iRet = pEntity->pChar->iLevelCombat - pPlayer->pChar->iLevelCombat;

	if (pEntity->pCritter)
	{
		iRet += critterRankGetConModifier(pEntity->pCritter->pcRank, pEntity->pCritter->pcSubRank);
	}
	return iRet;
}

// Returns a conning rank compared to the player, or 0 if the entity is not a critter.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetCritterConningLevel);
int exprEntGetCritterConningLevel(SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pCritter)
		return gclEntGetConningLevel(pPlayer, pEntity);
	else
		return 0;
}

// Returns an encounter conning rank compared to the player, or 0 if the entity is not a critter.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetCritterEncounterConningLevel);
int exprEntGetCritterEncounterConningLevel(SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pCritter)
	{	
		Entity *pOwner = entFromEntityRefAnyPartition(pEntity->erOwner);

		if (pPlayer == NULL || pPlayer->pChar == NULL)
			return 0;
		
		if (pOwner && pOwner != pEntity)
		{
			// For summons use owner's con
			return exprEntGetCritterEncounterConningLevel(pPlayer,pOwner);
		}

		return pEntity->pCritter->encounterData.activeTeamLevel - pPlayer->pChar->iLevelCombat;
	}
	else
	{
		return 0;
	}
}

// Returns an encounter level, or 0 if the entity is not a critter.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetCritterEncounterLevel);
int exprEntGetCritterEncounterLevel(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pCritter)
	{	
		Entity *pOwner = entFromEntityRefAnyPartition(pEntity->erOwner);

		if (pOwner && pOwner != pEntity)
		{
			// For summons use owner's con
			return exprEntGetCritterEncounterLevel(pOwner);
		}

		return pEntity->pCritter->encounterData.activeTeamLevel;
	}
	else
	{
		return 0;
	}
}

// Returns an encounter team size, or 0 if the entity is not a critter.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetCritterEncounterTeamSize);
int exprEntGetCritterEncounterTeamSize(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pCritter)
	{
		Entity *pOwner = entFromEntityRefAnyPartition(pEntity->erOwner);

		if (pOwner && pOwner != pEntity)
		{
			// For summons use owner's con
			return exprEntGetCritterEncounterTeamSize(pOwner);
		}
		return pEntity->pCritter->encounterData.activeTeamSize;
	}
	else
	{
		return 0;
	}
}

// Returns 1 if the Entity is dead
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsDead");
S32 exprEntGetIsDead(SA_PARAM_OP_VALID Entity *pEntity)
{
	return (pEntity && entCheckFlag(pEntity, ENTITYFLAG_DEAD));
}

// Returns 1 if the Entity is near death
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsNearDeath");
S32 exprEntGetIsNearDeath(SA_PARAM_OP_VALID Entity *pEntity)
{
	return (pEntity && pEntity->pChar && pEntity->pChar->pNearDeath);
}

// if in near death, returns the time in seconds left until the entity dies
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetNearDeathTimeLeft");
F32 exprEntGetNearDeathTimeLeft(SA_PARAM_OP_VALID Entity *pEntity)
{
	return SAFE_MEMBER3(pEntity, pChar, pNearDeath, fTimer);
}

// returns the a normalized death time between 0 - 1
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetNearDeathNormalizedTimeLeft");
F32 exprEntGetNearDeathNormalizedTimeLeft(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pNearDeath)
	{
		CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);
		if (pClass && pClass->pNearDeathConfig)
		{
			F32 fDeathTime = character_NearDeathGetMaxDyingTime(pEntity->pChar, pClass->pNearDeathConfig);
			if (fDeathTime > 0.f)
				return pEntity->pChar->pNearDeath->fTimer / fDeathTime;
		}
	}
	return 0.f;
}

// returns true if there is a friendly interacting with the near death entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetNearDeathHasInteractingFriendly");
S32 exprEntGetNearDeathHasInteractingFriendly(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pNearDeath)
	{
		return eaiSize(&pEntity->pChar->pNearDeath->perFriendlyInteracts) > 0;
	}

	return false;
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerRespawnTimeLeft");
S32 exprPlayerRespawnTimeLeft(SA_PARAM_OP_VALID Entity *pEntity)
{
	if ( pEntity && pEntity->pPlayer && pEntity->pPlayer->uiRespawnTime > 0 )
		return MAX(pEntity->pPlayer->uiRespawnTime - timeServerSecondsSince2000(),0);
	return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenGetFactionName");
const char *gclGenGetFactionName(SA_PARAM_OP_VALID Entity *pEntity)
{
	CritterFaction *f1;

	if (!pEntity) return 0;
	f1 = entGetFaction(pEntity);
	if (!f1) return 0;

	return f1->pchName;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenGetCritterGroupName");
const char *gclGenGetCritterGroupName(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pCritter)
	{
		return TranslateMessageRef(pEntity->pCritter->hGroupDisplayNameMsg);
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenGetCritterGroupIcon");
const char *gclGenGetCritterGroupIcon(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pCritter)
	{
		return pEntity->pCritter->pcGroupIcon;
	}
	return "";
}

const char* gclGuild_expr_GetName(SA_PARAM_OP_VALID Entity* pEnt);

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenGetGuildName");
const char *gclGenGetGuildName(SA_PARAM_OP_VALID Entity *pEntity)
{
	return gclGuild_expr_GetName(pEntity);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenGetOwnerName");
const char *gclGenGetOwnerName(SA_PARAM_OP_VALID Entity *pEntity, const char *pchClassType)
{
	Entity *f1, *f2;

	f1 = entGetOwner(pEntity);
	if (!f1) return 0;
	f2 = entity_GetPuppetEntityByType(f1,pchClassType,NULL,true,true);
	if (!f2) return 0;

	return entGetLocalName(f2);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetEntNameToFit");
const char *gclGenGetEntNameToFit(SA_PARAM_OP_VALID Entity *pEntity, int useLastName, int iMaxLen)
{
	if (pEntity)
	{
		static char *gpchSaveEntName = NULL;
		if (gpchSaveEntName) StructFreeString(gpchSaveEntName);
		gpchSaveEntName = ConvertNameToFit(entGetLocalName(pEntity), useLastName, iMaxLen);
		return gpchSaveEntName;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTextToFit");
const char *gclGenGetTextToFit(const char *pchLongText, int iMaxLen)
{
	static char *gpchSaveFitText = NULL;
	if (gpchSaveFitText) StructFreeString(gpchSaveFitText);
	gpchSaveFitText = ConvertTextToFit(pchLongText, iMaxLen);
	return gpchSaveFitText;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenGetDistToTarget");
F32 gclGenGetDistToTarget(SA_PARAM_OP_VALID Entity *pEntitySource,
						  SA_PARAM_OP_VALID Entity *pEntityTarget)
{
	F32 dist;
	S32 crop;
	if ((!pEntitySource) || (!pEntityTarget)) return 0;
	dist = entGetDistance(pEntitySource, NULL, pEntityTarget, NULL, NULL);
	crop = (S32)(dist * 10.0f);
	return (((F32)crop) / 10.0f);
}

// Returns 1 if the second Entity is a teammate to the first Entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsTeammate");
S32 exprEntGetIsTeammate(SA_PARAM_OP_VALID Entity *pEntitySource,
					     SA_PARAM_OP_VALID Entity *pEntityTarget)
{
	return team_OnSameTeam(pEntitySource,pEntityTarget);
}

// Returns 1 if the Entity is a civilian, 0 if it's not.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsCivilian");
S32 exprEntGetIsCivilian(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity)
		return entCheckFlag(pEntity, ENTITYFLAG_CIVILIAN);
	return 0;
}


// Returns 1 if the Entity is in combat, 0 if it's not.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsInCombat");
S32 exprEntGetIsInCombat(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		return character_HasMode(pEntity->pChar,kPowerMode_Combat);
	}
	return 0;
}

// Returns 1 if the Entity has the specified personal power mode with the given target, 0 if it's not.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetHasPersonalPowerMode");
S32 exprEntGetHasPersonalPowerMode(SA_PARAM_OP_VALID Entity *pEntity, ACMD_EXPR_ENUM(PowerMode) const char *modeName, SA_PARAM_OP_VALID Entity *pEntityTarget)
{
	int iMode = StaticDefineIntGetInt(PowerModeEnum,modeName);
	if (pEntity && pEntity->pChar && pEntityTarget && pEntityTarget->pChar)
	{
		return character_HasModePersonal(pEntity->pChar,iMode,pEntityTarget->pChar);
	}
	return 0;
}


// Returns 1 if the Entity is PvP-flagged/enabled, 0 if it's not.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsPvP");
S32 exprEntGetIsPvP(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity)
	{
		// TODO(JW): PvP: Fix this when PvP is properly implemented
		return 0;
	}
	return 0;
}

// Returns 1 if the Entity has any control attribs on them (even if they aren't being fully rooted, held, or disabled), 0 if there aren't any.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsControlled");
S32 exprEntGetIsControlled(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pattrBasic)
	{
		if(character_AffectedBy(pEntity->pChar, kAttribType_Hold) || 
			character_AffectedBy(pEntity->pChar, kAttribType_Root) || 
			character_AffectedBy(pEntity->pChar, kAttribType_Disable))
		{
			return 1;
		}
	}

	return 0;
}

// Returns 1 if the Entity is currently being rooted, held, or disabled, 0 if isn't.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsActuallyControlled");
S32 exprEntGetIsActuallyControlled(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pattrBasic)
	{
		if(character_IsHeld(pEntity->pChar)
			|| pEntity->pChar->pattrBasic->fDisable > 0
			|| character_IsRooted(pEntity->pChar))
		{
			return 1;
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntCanBreakControlEffect");
S32 exprEntCanBreakControlEffect(SA_PARAM_OP_VALID Entity *pEntity)
{
	if(pEntity && pEntity->pChar)
	{
		if(exprEntGetIsControlled(pEntity)>0)
		{
			int i;
			Character *pchar = pEntity->pChar;
			for(i=eaSize(&pchar->ppModsNet)-1; i>=0; i--)
			{
				AttribModDef *pdef;
				AttribModNet *pmodNet = pchar->ppModsNet[i];

				if(!ATTRIBMODNET_VALID(pmodNet))
					continue;

				pdef = modnet_GetDef(pmodNet);

				if(pdef && pdef->offAspect==kAttribAspect_BasicAbs)
				{
					if( (pdef->offAttrib==kAttribType_Hold || pdef->offAttrib==kAttribType_Disable || pdef->offAttrib==kAttribType_Root)
						&& pmodNet->iHealth >= 0)
					{
						return 1;
					}
				}
			}
		}
	}
	return 0;
}

// Returns the remaining percentage [0..1] of the most debilitating form of control on the character.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetControlPercent");
F32 exprEntGetControlPercent(SA_PARAM_OP_VALID Entity *pEntity)
{
	if(pEntity && pEntity->pChar)
	{
		if(exprEntGetIsControlled(pEntity)>0)
		{
			int i;
			U32 uiRoot = 0, uiRootMax = 0, uiHold = 0, uiHoldMax = 0, uiDisable = 0, uiDisableMax = 0;
			Character *pchar = pEntity->pChar;
			for(i=eaSize(&pchar->ppModsNet)-1; i>=0; i--)
			{
				AttribModDef *pdef;
				AttribModNet *pmodNet = pchar->ppModsNet[i];

				if(!ATTRIBMODNET_VALID(pmodNet))
					continue;

				pdef = modnet_GetDef(pmodNet);

				if(pdef && pdef->offAspect==kAttribAspect_BasicAbs)
				{
					if(pdef->offAttrib==kAttribType_Hold)
					{
						uiHold += pmodNet->uiDuration;
						uiHoldMax += pmodNet->uiDurationOriginal;
					}
					else if(pdef->offAttrib==kAttribType_Disable)
					{
						uiDisable += pmodNet->uiDuration;
						uiDisableMax += pmodNet->uiDurationOriginal;
					}
					else if(pdef->offAttrib==kAttribType_Root)
					{
						uiRoot+= pmodNet->uiDuration;
						uiRootMax += pmodNet->uiDurationOriginal;
					}
				}
			}

			if(uiHold && uiHoldMax)
			{
				return (F32)uiHold / (F32)uiHoldMax;
			}
			if(uiDisable && uiDisableMax)
			{
				return (F32)uiDisable / (F32)uiDisableMax;
			}
			if(uiRoot && uiRootMax)
			{
				return (F32)uiRoot / (F32)uiRootMax;
			}
		}
		return 0;
	}
	return -1;
}

// Returns the description text for the control on the character.  Returns "" on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetControlDescription");
const char* exprEntGetControlDescription(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pattrBasic)
	{
		PowerDef *pPowerDef = NULL;
		if (pEntity->pChar->pPowActFinished) {
			pPowerDef = GET_REF(pEntity->pChar->pPowActFinished->ref.hdef);
			if(strcmp(pPowerDef->pchName, "System_Rest") == 0) {
				return TranslateMessageKey("ControlledBar_Resting");;
			}
			
		}
		if(pEntity->pChar->pattrBasic->fHold > 0)
		{
			return TranslateMessageKey("ControlledBar_Hold");
		}	
		else if(pEntity->pChar->pattrBasic->fDisable > 0)
		{
			return TranslateMessageKey("ControlledBar_Disable");
		}
		else if(pEntity->pChar->pattrBasic->fRoot > 0)
		{
			return TranslateMessageKey("ControlledBar_Root");
		}
		else if(character_AffectedBy(pEntity->pChar, kAttribType_Hold))
		{
			return TranslateMessageKey("ControlledBar_HoldPending");
		}
		else if(character_AffectedBy(pEntity->pChar, kAttribType_Disable))
		{
			return TranslateMessageKey("ControlledBar_DisablePending");
		}
		else if(character_AffectedBy(pEntity->pChar, kAttribType_Root))
		{
			return TranslateMessageKey("ControlledBar_RootPending");
		}
	}
	return "";
}

// Returns 1 if the Entity is activating a Power, 0 if it's not.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsActivating");
S32 exprEntGetIsActivating(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		if(pEntity->pChar->pPowActCurrent)
		{
			return 1;
		}
		return 0;
	}
	return -1;
}

// Returns 1 if the Entity is activating a maintained Power, 0 if it's not.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsActivatingMaintained");
S32 exprEntGetIsActivatingMaintained(SA_PARAM_OP_VALID Entity *pEntity)
{
	Character *pChar = SAFE_MEMBER(pEntity, pChar);
	PowerActivation *pPowAct = SAFE_MEMBER(pChar, pPowActCurrent);
	if (pPowAct)
	{
		PowerDef *pPowerDef = GET_REF(pPowAct->ref.hdef);
		return pPowerDef
			   && (pChar->eChargeMode == kChargeMode_CurrentMaintain)
			   && (pPowerDef->fTimeMaintain > 0);
	}
	return 0;
}

// Returns 1 if the Entity has a charge Power queued, 0 if it's not.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsQueueingCharged");
S32 exprEntGetIsQueueingCharged(SA_PARAM_OP_VALID Entity *pEntity)
{
	Character *pChar = SAFE_MEMBER(pEntity, pChar);
	PowerActivation *pPowAct = SAFE_MEMBER(pChar, pPowActQueued);
	if (pPowAct)
	{
		PowerDef *pPowerDef = GET_REF(pPowAct->ref.hdef);
		return pPowerDef && (pPowerDef->eType == kPowerType_Click) && (pPowerDef->fTimeCharge > 0);
	}
	return 0;
}

// Returns 1 if the Entity has a maintained Power queued, 0 if it's not.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsQueueingMaintained");
S32 exprEntGetIsQueueingMaintained(SA_PARAM_OP_VALID Entity *pEntity)
{
	Character *pChar = SAFE_MEMBER(pEntity, pChar);
	PowerActivation *pPowAct = SAFE_MEMBER(pChar, pPowActQueued);
	if (pPowAct)
	{
		PowerDef *pPowerDef = GET_REF(pPowAct->ref.hdef);
		return pPowerDef && (pPowerDef->eType == kPowerType_Maintained);
	}
	return 0;
}

// Returns 1 if the Entity is activating a charged Power, 0 if it's not.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsActivatingCharged");
S32 exprEntGetIsActivatingCharged(SA_PARAM_OP_VALID Entity *pEntity)
{
	return SAFE_MEMBER2(pEntity, pChar, eChargeMode) == kChargeMode_Current;
}

// Returns the power that is currently queued
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetQueuedPower");
SA_RET_OP_VALID PowerDef *exprEntGetQueuedPower(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (SAFE_MEMBER2(pEntity, pChar, pPowActQueued))
	{
		PowerDef *pPowerDef = GET_REF(pEntity->pChar->pPowActQueued->ref.hdef);
		return pPowerDef;
	}
	return NULL;
}

// Returns the power that is currently active
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetActivePower");
SA_RET_OP_VALID PowerDef *exprEntGetActivePower(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (SAFE_MEMBER2(pEntity, pChar, pPowActCurrent))
	{
		PowerDef *pPowerDef = GET_REF(pEntity->pChar->pPowActCurrent->ref.hdef);
		return pPowerDef;
	}
	return NULL;
}

// Returns the activation time for the given power. 
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetPowerActivationTime");
F32 exprGetPowerActivationTime(SA_PARAM_OP_VALID PowerDef* pPowerDef)
{
	if(pPowerDef)
	{
		return pPowerDef->fTimeActivate;
	}
	return 0;
}

// Returns the allow queue time for the given power. 
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetPowerAllowQueueTime");
F32 exprGetPowerQueueTime(SA_PARAM_OP_VALID PowerDef* pPowerDef)
{
	if(pPowerDef)
	{
		return pPowerDef->fTimeAllowQueue;
	}
	return 0;
}

// Returns the power name for the given power.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerName");
const char* exprEntGetPowerName(SA_PARAM_OP_VALID PowerDef* pPowerDef)
{
	if(pPowerDef)
	{
		return TranslateDisplayMessage(pPowerDef->msgDisplayName);
	}
	return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PowerDefFromPower");
SA_RET_OP_VALID PowerDef* exprPowerDefFromPower(SA_PARAM_OP_VALID Power* pPower)
{
	return SAFE_GET_REF(pPower, hDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TraySlotIsInRange");
bool exprTraySlotIsInRange(S32 tray, S32 slot)
{
	Entity* e = entActivePlayerPtr();
	TrayElem* pElem = entity_TrayGetTrayElem(e,tray,slot);
	Power* ppow = pElem ? EntTrayGetActivatedPower(e,pElem,false,NULL) : NULL;

	if (!e || !ppow)
		return false;
	return character_TargetInPowerRange(e->pChar, ppow, GET_REF(ppow->hDef), entity_GetTarget(e), NULL);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("SourceItemFromPower");
SA_RET_OP_VALID Item* exprSourceItemFromPower(SA_PARAM_OP_VALID Power* pPower)
{
	return pPower ? pPower->pSourceItem : NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsCurrentActivePowerOfPowerCat");
bool exprEntIsCurrentActivePowerOfPowerCat(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_STR const char *pchCategory)
{
	S32 eCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);

	if(pEntity && pEntity->pChar && pEntity->pChar->pPowActCurrent)
	{
		PowerDef *pDef = GET_REF(pEntity->pChar->pPowActCurrent->ref.hdef);

		if(pDef && ea32Find(&pDef->piCategories,eCategory) != -1)
		{
			return true;
		}
	}

	return false;
}

// Returns the time in seconds the Entity could charge it's active Power for.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetActivatingChargeMax");
F32 exprEntGetActivatingChargeMax(SA_PARAM_OP_VALID Entity *pEntity)
{
	float fMaxTime = -1;

	if (pEntity && pEntity->pChar && pEntity->pChar->pPowActCurrent)
	{
		PowerDef *pdef = GET_REF(pEntity->pChar->pPowActCurrent->ref.hdef);
		if(pdef)
		{
			fMaxTime = pdef->fTimeCharge;

			// Sorry about this bit of hardcoding, but pickup has a variant max charge which is hacked in.
			// This is part of that hack.
			if(strstri(pdef->pchName, "Pickup")!=NULL)
			{
				Entity *eTarget = entFromEntityRefAnyPartition(pEntity->pChar->pPowActCurrent->erTarget);
				if(eTarget && eTarget->pCritter && eTarget->pCritter->eInteractionFlag & kCritterOverrideFlag_Throwable)
				{
					int off = StaticDefineIntGetInt(AttribTypeEnum, "StatStrength");

					fMaxTime = 3*(eTarget->pCritter->fMass-1-log(*F32PTR_OF_ATTRIB(pEntity->pChar->pattrBasic, off)/10)/log(2));
					if(fMaxTime<0)
					{
						fMaxTime = 0;
					}
				}
			}
			else if (fMaxTime == 0)
				return -1;
			// End of pickup hack
		}
	}

	return fMaxTime;
}

// Returns the percentage [0..1] time the Entity has charged it's active Power for, relative to max.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetActivatingChargePercent");
F32 exprEntGetActivatingChargePercent(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pPowActCurrent)
	{
		F32 fMax = exprEntGetActivatingChargeMax(pEntity);
		if(fMax>0)
		{
			F32 fAdditional = pEntity->pChar->eChargeMode==kChargeMode_Current ? g_fCharacterTickTime : 0;
			return (pEntity->pChar->pPowActCurrent->fTimeCharged + fAdditional) / fMax;
		}
		else if(fMax==0)
		{
			return 1;
		}
		return -1;
	}
	return -1;
}


// Return the time the Entity has charged it's active power for. Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetActivatingChargeTime");
F32 exprEntGetActivatingCharge(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pPowActCurrent)
	{
		F32 fAdditional = pEntity->pChar->eChargeMode==kChargeMode_Current ? g_fCharacterTickTime : 0;
		return pEntity->pChar->pPowActCurrent->fTimeCharged + fAdditional;
	}
	return -1;
}

// Returns the percentage [0..1] time the Entity has charged it's active Power for, relative to max.  Returns -1 on failure.
//The tolerance is for hiding the first X seconds of an activation (typically 0.25 or so) to make tap/hold powers look better.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetActivatingChargePercentWithTolerance");
F32 exprEntGetActivatingChargePercentWithTolerance(SA_PARAM_OP_VALID Entity *pEntity, F32 fTolerance)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pPowActCurrent)
	{
		F32 fMax = exprEntGetActivatingChargeMax(pEntity) - fTolerance;
		if(fMax>0)
		{
			F32 fAdditional = pEntity->pChar->eChargeMode==kChargeMode_Current ? g_fCharacterTickTime : 0;
			return max((pEntity->pChar->pPowActCurrent->fTimeCharged + fAdditional - fTolerance), 0) / fMax;
		}
		return -1;
	}
	return -1;
}

// Returns the time in seconds the Entity could activate it's active Power for.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetActivatingActivateMax");
F32 exprEntGetActivatingActivateMax(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pPowActCurrent)
	{
		PowerDef *pdef = GET_REF(pEntity->pChar->pPowActCurrent->ref.hdef);
		if(pdef)
		{
			if (pdef->eType==kPowerType_Maintained || pdef->eType==kPowerType_Toggle)
				return pdef->fTimeMaintain;
		}
	}
	return -1;
}

// Returns the percentage [0..1] time the Entity has activated it's active Power for, relative to max.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetActivatingActivatePercent");
F32 exprEntGetActivatingActivatePercent(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity
		&& pEntity->pChar
		&& pEntity->pChar->pPowActCurrent 
		&& pEntity->pChar->eChargeMode!=kChargeMode_Current
		&& pEntity->pChar->pPowActCurrent->eLungeMode!=kLungeMode_Pending)
	{
		F32 fMax = exprEntGetActivatingActivateMax(pEntity);
		if(fMax > 0 && pEntity->pChar->pPowActCurrent->fTimeMaintained > 0)
		{
			return (pEntity->pChar->pPowActCurrent->fTimeMaintained + g_fCharacterTickTime) / fMax;
		}
		return -1;
	}
	return -1;
}

// Returns the percentage [0..1] time of the next interesting activate event of the Entity's active Power, relative to max.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetActivatingActivateNotch");
F32 exprEntGetActivatingActivateNotch(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pPowActCurrent)
	{
		// Copied from Max, since we need the def anyway
		PowerDef *pdef = GET_REF(pEntity->pChar->pPowActCurrent->ref.hdef);
		if(pdef)
		{
			F32 fResult = 0;
			F32 fMaintain = pdef->eType==kPowerType_Maintained ? pdef->fTimeMaintain : 0;
			F32 fActivate = pdef->fTimeActivate + fMaintain;
			F32 fCurrent = (pEntity->pChar->pPowActCurrent->fTimeActivating + g_fCharacterTickTime);
			if(fMaintain > 0)
			{
				if(pdef->fTimeActivate > fCurrent)
				{
					fResult = pdef->fTimeActivate / fActivate;
				}
				else
				{
					fCurrent -= pdef->fTimeActivate;
					fResult = (pdef->fTimeActivate + pdef->fTimeActivatePeriod * ceil(fCurrent/pdef->fTimeActivatePeriod)) / fActivate;
				}
			}
			else
			{
				fResult = 1;
			}
			return fResult;
		}
		return 0;
	}
	return -1;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetCategoryCooldownMax");
F32 exprCategoryGetCooldownMax(SA_PARAM_OP_STR const char *pchCategory)
{
	S32 eCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);
	PowerCategory *pCat = eCategory > -1 ? g_PowerCategories.ppCategories[eCategory] : NULL;

	if(pCat)
	{
		return pCat->fTimeCooldown;
	}

	return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetCategoryCooldownPercent");
F32 exprCategoryGetCooldownPercent(SA_PARAM_OP_STR const char *pchCategory)
{
	Entity* pEnt = entActivePlayerPtr();
	S32 eCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);

	if(pEnt && pEnt->pChar && eCategory != -1)
	{
		F32 fMax = exprCategoryGetCooldownMax(pchCategory);
		CooldownTimer *pTimer = character_GetCooldownTimerForCategory(pEnt->pChar,eCategory);
		return pTimer && fMax ? pTimer->fCooldown - g_fCharacterTickTime / fMax : 0;
	}

	return 0;
}

// Returns the internal name of the currently activating power, if there is one. If there isn't a power activating, returns ""
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetActivatingPowerName");
const char *exprEntGetActivatingPowerInternalName(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pPowActCurrent)
	{
		// Copied from Max, since we need the def anyway
		PowerDef *pdef = GET_REF(pEntity->pChar->pPowActCurrent->ref.hdef);
		if(pdef)
		{
			return pdef->pchName;
		}
	}
	return "";
}

// Returns the internal name of the currently activating power, if there is one. If there isn't a power activating, returns ""
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetQueuedPowerName");
const char *exprEntGetQueuedPowerInternalName(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->pPowActQueued)
	{
		// Copied from Max, since we need the def anyway
		PowerDef *pdef = GET_REF(pEntity->pChar->pPowActQueued->ref.hdef);
		if(pdef)
		{
			return pdef->pchName;
		}
	}
	return "";
}

// Returns the Entity's number of unspent points in stats.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPointsUnspentStatEx");
S32 exprEntGetPointsUnspentStatEx(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pchStatPointPoolName)
{
	S32 iResult = -1;

	if (pEntity && pEntity->pChar)
	{
		iResult = entity_GetAssignedStatUnspent(CONTAINER_NOCONST(Entity, pEntity), pchStatPointPoolName);
	}
	return iResult;
}

// Returns the Entity's number of unspent points in stats.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPointsUnspentStat");
S32 exprEntGetPointsUnspentStat(SA_PARAM_OP_VALID Entity *pEntity)
{
	return exprEntGetPointsUnspentStatEx(pEntity, STAT_POINT_POOL_DEFAULT);
}

// Returns the Entity's number of unspent points in power trees.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPointsUnspentTree");
S32 exprEntGetPointsUnspentTree(SA_PARAM_OP_VALID Entity *pEntity)
{
	if(pEntity)
	{
		return entity_TreePointsToSpend(pEntity);
	}
	return -1;
}

static S32 EntGetBuffsBySubTarget_SortByHealth(	const UIGenEntitySubTargetBuff** pBuffA, 
												const UIGenEntitySubTargetBuff** pBuffB )
{
	const UIGenEntitySubTargetBuff* pA = *pBuffA;
	const UIGenEntitySubTargetBuff* pB = *pBuffB;

	if ( pA->fPercentHealth < pB->fPercentHealth )
		return -1;
	if ( pA->fPercentHealth > pB->fPercentHealth )
		return 1;

	return 0;
}

static void EntGetBuffsBySubTarget(	UIGen *pGen, Entity *pEntity, 
									F32 fPercentMin, F32 fPercentMax, bool bSortByHealth)
{
	static UIGenEntitySubTargetBuff **s_eaBuffs = NULL;
	S32 i, c;
	
	eaClearStruct( &s_eaBuffs, parse_UIGenEntitySubTargetBuff );
	
	if(pEntity==NULL || pEntity->pChar==NULL)
	{
		ui_GenSetManagedListSafe(pGen, &s_eaBuffs, UIGenEntitySubTargetBuff, false);
		return;
	}

	for(i=eaSize(&pEntity->pChar->ppSubtargets)-1; i>=0; i--)
	{
		PowerSubtargetNet* pNet = pEntity->pChar->ppSubtargets[i];
		S32 iHealthMax = pNet->iHealthMax > 0 ? pNet->iHealthMax : 1;
		F32 fHealthPercent = pNet->iHealth / (F32)iHealthMax;
		
		if( fHealthPercent >= fPercentMin && fHealthPercent <= fPercentMax )
		{
			PowerSubtargetCategory* pCat;
			if ( pNet->bCategory )
			{
				pCat = powersubtarget_GetCategoryByName( pNet->cpchName );
			}
			else
			{
				PowerSubtarget* pSub = powersubtarget_GetByName( pNet->cpchName );
				pCat = pSub ? GET_REF( pSub->hCategory ) : NULL;
			}

			if ( pCat==NULL )
				continue;

			for (c = 0; c < eaSize(&s_eaBuffs); c++)
			{
				if (s_eaBuffs[c]->pCategory == pCat)
					break;
			}
			
			if ( c < eaSize(&s_eaBuffs) ) //update existing buff
			{
				s_eaBuffs[c]->fPercentHealth += fHealthPercent;
				s_eaBuffs[c]->iCount++;
			}
			else //create a new buff
			{
				UIGenEntitySubTargetBuff *pBuff = StructCreate( parse_UIGenEntitySubTargetBuff );

				pBuff->fPercentHealth = fHealthPercent;
				pBuff->iCount = 1;
				pBuff->pCategory = pCat;
				pBuff->pchIcon = pCat->pchIconName ? pCat->pchIconName : allocAddString("Power_Generic");
				pBuff->pchDescShort = TranslateDisplayMessage(pCat->msgDisplayName);
				if (!pBuff->pchDescShort)
					pBuff->pchDescShort = "";
				/*
				pBuff->pchDescLong = TranslateDisplayMessage(s_ppCats[i]->->msgDescription);
				if (!pBuff->pchDescLong)
				pBuff->pchDescLong = "";
				*/
				eaPush(&s_eaBuffs,pBuff);
			}
		}
	}

	for (i=eaSize(&s_eaBuffs)-1; i>=0; i--)
	{
		UIGenEntitySubTargetBuff *pBuff = s_eaBuffs[i];

		if ( pBuff->iCount > 1 )
		{
			pBuff->fPercentHealth /= pBuff->iCount;
		}
	}

	if ( bSortByHealth )
	{
		eaQSort( s_eaBuffs, EntGetBuffsBySubTarget_SortByHealth );
	}

	ui_GenSetManagedListSafe(pGen, &s_eaBuffs, UIGenEntitySubTargetBuff, false);

}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBuffsBySubtarget");
void exprEntGetBuffsBySubTarget(SA_PARAM_NN_VALID UIGen *pGen,
								SA_PARAM_OP_VALID Entity *pEntity,
								bool bSortByHealth)
{
	EntGetBuffsBySubTarget(pGen,pEntity,0.0f,1.0f,bSortByHealth);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBuffsBySubtargetInPercentRange");
void exprEntGetBuffsBySubTargetInPercentRange(	SA_PARAM_NN_VALID UIGen *pGen,
												SA_PARAM_OP_VALID Entity *pEntity, S32 iMin, S32 iMax, bool bSortByHealth)
{
	EntGetBuffsBySubTarget(pGen,pEntity,iMin*0.01f,iMax*0.01f, bSortByHealth);
}

static S32 SortEntBuffs(const S32* eaTags,
						const UIGenEntityBuff** ppBuffA, 
						const UIGenEntityBuff** ppBuffB)
{
	S32 i;
	const UIGenEntityBuff* pA = *ppBuffA;
	const UIGenEntityBuff* pB = *ppBuffB;
	const PowerDef *pDefA;
	const PowerDef *pDefB;
	if(eaTags)
	{
		S32 iIndexA = ea32Find(&eaTags, pA->ePowerTag);
		S32 iIndexB = ea32Find(&eaTags, pB->ePowerTag);
		i = iIndexA - iIndexB;
		if(i)
			return i;
	}
	i = stricmp(pA->pchDescShort,pB->pchDescShort);
	if(i)
		return i;
	
	pDefA = GET_REF(pA->hPowerDef);
	pDefB = GET_REF(pB->hPowerDef);

	return stricmp(pDefA->pchName,pDefB->pchName);
}

static S32 SortAttribModNets(const AttribModNet** ppNetA, const AttribModNet** ppNetB)
{
	S32 i;
	const AttribModNet *pNetA = *ppNetA;
	const AttribModNet *pNetB = *ppNetB;

	// Longer duration to the front, I guess
	i = (S32)pNetB->uiDuration - (S32)pNetA->uiDuration;
	if(i)
		return i;

	i = (S32)pNetA->uiDefIdx - (S32)pNetB->uiDefIdx;
	return i;
}

static void gclEntBuffCleanup(StashTable stBuffStash, S32 iUpdateIndex, S32 bUpdatePowerDefBuffList)
{
	StashTableIterator iter;
	StashElement elem;
	stashGetIterator(stBuffStash, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		EntityBuffData *pData = (EntityBuffData *)stashElementGetPointer(elem);
		if (pData->iUpdateIndex < iUpdateIndex)
		{
			stashAddressRemovePointer(stBuffStash, stashElementGetKey(elem), NULL);
			StructDestroy(parse_EntityBuffData, pData);
			continue;
		}
		if (bUpdatePowerDefBuffList)
		{
			PowerDefBuffList *pBuffList = &pData->PowerDefData;
			eaSetSizeStruct(&pBuffList->eaModStack, parse_AttribModStack, pBuffList->iSize);
		}
	}
}

//HACK: Display lifetime usage/game on passive buffs in STO because there is no where else to show it
static void CharacterBuffFillPassiveLifetimeUsageLeft(Character* pChar, UIGenEntityBuff* pBuff, PowerDef* pDef)
{
	if (gConf.bShowLifetimeOnPassiveBuffs && pDef->eType == kPowerType_Passive)
	{
		Power* pPower = character_FindPowerByDef(pChar, pDef);
		if (pDef->fLifetimeUsage > 0.0f)
		{
			pBuff->iLifetimeRemaining = pPower ? power_GetLifetimeUsageLeft(pPower) : 0;
			pBuff->bHasLifetimeTimer = true;
			return;
		}
		else if (pDef->fLifetimeGame > 0.0f)
		{
			pBuff->iLifetimeRemaining = pPower ? power_GetLifetimeGameLeft(pPower) : 0;
			pBuff->bHasLifetimeTimer = true;
			return;
		}
		else if (pDef->fLifetimeReal > 0.0f)
		{
			pBuff->iLifetimeRemaining = pPower ? power_GetLifetimeRealLeft(pPower) : 0;
			pBuff->bHasLifetimeTimer = true;
			return;
		}
	} 
	pBuff->bHasLifetimeTimer = false;
	pBuff->iLifetimeRemaining = 0;
}

static void ResetUIGenEntityBuff(UIGenEntityBuff *pBuff, PowerDef *pDef, S32 eTag, Character *pchar, S32 bDev)
{
	const char *pchIcon;
	static const char *pchIconGeneric = NULL;
	if(!pchIconGeneric)
		pchIconGeneric = allocAddString("Power_Generic");
	
	pchIcon = pDef->pchIconName ? pDef->pchIconName : pchIconGeneric;
	pBuff->pchIcon = gclGetBestPowerIcon(pchIcon, pBuff->pchIcon);
	pBuff->ePowerTag = eTag;
	pBuff->pchDescShort = NULL_TO_EMPTY(TranslateDisplayMessage(pDef->msgDisplayName));
	pBuff->pchDescLong = NULL_TO_EMPTY(TranslateDisplayMessage(pDef->msgDescription));
	pBuff->pchDescVeryLong = NULL_TO_EMPTY(TranslateDisplayMessage(pDef->msgDescriptionLong));
	pBuff->pchNameInternal = bDev ? pDef->pchName : NULL;
	estrClear(&pBuff->pchAutoDesc);
	pBuff->uiDuration = pBuff->uiDurationOriginal = 0;
	pBuff->bNoDuration = false;
	pBuff->fResist = 0.0f;
	pBuff->uiStack = 1;
	SET_HANDLE_FROM_REFERENT(g_hPowerDefDict, pDef, pBuff->hPowerDef);
	CharacterBuffFillPassiveLifetimeUsageLeft(pchar, pBuff, pDef);
}


static bool AttribModNet_CalculateDurationForToggle(Character *pChar, UIGenEntityBuff *pBuff, AttribModNet *pModNet, PowerDef *pDef)
{
	// see if this attribMod's duration should be computed by the activation's fTimeMaintained
	// find the toggle activation and use its fTimeMaintain to compute the attrib's duration
	PowerActivation *pAct = poweract_FindPowerInArrayByDef(pChar->ppPowerActToggle, pDef);
	
	if (pAct)
	{
		F32 fPowerDuration;
		F32 fActualMaxDuration;
		F32 fModDefDuration = 0.f;
		AttribModDef *pModDef = eaGet(&pDef->ppOrderedMods,pModNet->uiDefIdx);
				
		if (!pModDef)
			return false;
		if (pModDef->pExprDuration)
		{
			combateval_ContextSetupApply(pChar,NULL,NULL,NULL);
			fModDefDuration = combateval_EvalNew(PARTITION_CLIENT, pModDef->pExprDuration,kCombatEvalContext_Apply,NULL);	
		}

		if (fModDefDuration < pDef->fTimeActivatePeriod)
			return false; // the mod will be lasting less than the activation period
		
		fActualMaxDuration = pDef->fTimeMaintain + (fModDefDuration - pDef->fTimeActivatePeriod);

		fPowerDuration = fActualMaxDuration - pAct->fTimeMaintained;
		if (fPowerDuration < 0.f)
			fPowerDuration = 0.f;
		
		
		pBuff->uiDuration = (U32)ceilf(fPowerDuration);
		pBuff->uiDurationOriginal = (U32)ceilf(fActualMaxDuration);
		return true;
	}

	return false;
}

static void UpdateUIGenEntityBuffDuration(Character *pChar, UIGenEntityBuff *pBuff, AttribModNet *pModNet)
{
	PowerDef *pDef = GET_REF(pBuff->hPowerDef);
	if(!pDef || pBuff->bNoDuration)
		return;
	
	if (pDef->eType == kPowerType_Toggle && pDef->fTimeActivatePeriod && pDef->uiPeriodsMax)
	{	// for attribs that come from a toggle type power with activation periods
		// get the duration of the attrib based on the powerApplication's maintained time
		if (AttribModNet_CalculateDurationForToggle(pChar, pBuff, pModNet, pDef))
			return;
	}
	
	
	if(pDef->fTimeActivatePeriod && pModNet->uiDurationOriginal >= pDef->fTimeActivatePeriod)
	{
		pBuff->uiDuration = 0;
		pBuff->uiDurationOriginal = 0;
		pBuff->bNoDuration = true;
	}
	else 
	{
		U32 uiDuration = character_ModNetGetPredictedDuration(pChar, pModNet);
		if(uiDuration > pBuff->uiDuration)
		{
			pBuff->uiDuration = uiDuration;
			if(pModNet->bResistPositive)
				pBuff->fResist = 1.0f;
			else if(pModNet->bResistNegative)
				pBuff->fResist = -1.0f;
			else
				pBuff->fResist = 0.0f;
		}

		MAX1(pBuff->uiDurationOriginal,pModNet->uiDurationOriginal);
	}
}

#define ENT_STACK_GROUP_DATA_MAX_SIZE 10
static void EntUIStackGroupData_Cleanup(UIStackGroupEntityData*** peaStackGroupEntData)
{
	if (eaSize(peaStackGroupEntData) > ENT_STACK_GROUP_DATA_MAX_SIZE)
	{
		U32 uOldestUpdateFrame = 0;
		int iOldestUpdateIndex = -1;
		int i;
		for (i = eaSize(peaStackGroupEntData)-1; i >= 0; i--)
		{
			UIStackGroupEntityData* pData = (*peaStackGroupEntData)[i];
			if (!uOldestUpdateFrame || pData->uLastUpdateFrame < uOldestUpdateFrame)
			{
				uOldestUpdateFrame = pData->uLastUpdateFrame;
				iOldestUpdateIndex = i;
			}
		}
		StructDestroy(parse_UIStackGroupEntityData, eaRemove(peaStackGroupEntData, iOldestUpdateIndex));
	}
}

static UIStackGroupData** EntGetUIStackGroupData(Entity* pEnt)
{
	static UIStackGroupEntityData** s_eaStackGroupEntData = NULL;
	UIStackGroupEntityData* pEntData;
	U32 uThisFrame = 0;
	S32 i, j, iStackGroupSize = 0;

	if (!pEnt || !pEnt->pChar || !gConf.bEntBuffListOnlyShowBestModInStackGroup)
	{
		return NULL;
	}

	pEntData = eaIndexedGetUsingInt(&s_eaStackGroupEntData, entGetContainerID(pEnt));
	if (!pEntData)
	{
		// Cleanup old entity data
		EntUIStackGroupData_Cleanup(&s_eaStackGroupEntData);

		// Add new entity data
		pEntData = StructCreate(parse_UIStackGroupEntityData);
		pEntData->uEntID = entGetContainerID(pEnt);
		if (!s_eaStackGroupEntData)
			eaIndexedEnable(&s_eaStackGroupEntData, parse_UIStackGroupEntityData);
		eaPush(&s_eaStackGroupEntData, pEntData);
	}

	// Only update this data once per frame so that stored pointers are valid for this frame
	frameLockedTimerGetTotalFrames(gGCLState.frameLockedTimer, &uThisFrame);
	if (pEntData->uLastUpdateFrame == uThisFrame)
	{
		return pEntData->eaStackGroupData;
	}
	else
	{
		pEntData->uLastUpdateFrame = uThisFrame;
	}
	for (i = 0; i < eaSize(&pEnt->pChar->ppModsNet); i++)
	{
		AttribModNet *pModNet = pEnt->pChar->ppModsNet[i];
		PowerDef *pDef = GET_REF(pModNet->hPowerDef);
		AttribModDef *pModDef = pDef ? eaGet(&pDef->ppOrderedMods,pModNet->uiDefIdx) : NULL;
		
		if(!ATTRIBMODNET_VALID(pModNet) || !pDef)
			continue;

		if (pModDef && pModDef->eStackGroup != kModStackGroup_None)
		{
			for (j = iStackGroupSize-1; j >= 0; j--)
			{
				UIStackGroupData* pStackGroup = pEntData->eaStackGroupData[j];
				if (pModDef->eStackGroup == (ModStackGroup)pStackGroup->eStackGroup &&
					pModDef->offAttrib == (AttribType)pStackGroup->offAttrib &&
					pModDef->offAspect == (AttribAspect)pStackGroup->offAspect)
				{
					S32 iMag = abs(pModNet->iMagnitude);
					if (iMag > pStackGroup->iMagnitudeBest)
					{
						pStackGroup->iMagnitudeBest = iMag;
						pStackGroup->iModNetIdxBest = i;
					}
					ea32Push(&pStackGroup->piModNetIndices, i);
					break;
				}
			}
			if (j < 0)
			{
				UIStackGroupData* pStackGroup = eaGetStruct(&pEntData->eaStackGroupData, parse_UIStackGroupData, iStackGroupSize++);
				pStackGroup->eStackGroup = pModDef->eStackGroup;
				pStackGroup->offAttrib = pModDef->offAttrib;
				pStackGroup->offAspect = pModDef->offAspect;
				pStackGroup->iMagnitudeBest = abs(pModNet->iMagnitude);
				pStackGroup->iModNetIdxBest = i;
				ea32Clear(&pStackGroup->piModNetIndices);
				ea32Push(&pStackGroup->piModNetIndices, i);
			}
		}
	}
	eaSetSizeStruct(&pEntData->eaStackGroupData, parse_UIStackGroupData, iStackGroupSize);
	return pEntData->eaStackGroupData;
}

static bool IsModBestInUIStackGroupArray(UIStackGroupData** eaStackGroups, 
										 AttribModDef* pModDef, 
										 S32 iModNetIdx,
										 UIStackGroupData** ppStackGroup)
{
	S32 i;
	for (i = eaSize(&eaStackGroups)-1; i >= 0; i--)
	{
		UIStackGroupData* pStackGroup = eaStackGroups[i];
		if (pModDef->eStackGroup == (ModStackGroup)pStackGroup->eStackGroup &&
			pModDef->offAttrib == (AttribType)pStackGroup->offAttrib &&
			pModDef->offAspect == (AttribAspect)pStackGroup->offAspect)
		{
			if (ppStackGroup)
			{
				(*ppStackGroup) = pStackGroup;
			}
			if (pStackGroup->iModNetIdxBest == iModNetIdx)
			{
				return true;
			}
			break;
		}
	}
	return false;
}

#define BUFF_AUTODESC_MAX 8


// Builds a gen's list of UIGenEntityBuffs based on Powers, with an optional list of PowerTags
static S32 EntGetBuffsByPower(UIGen *pGen, Entity *pEntity, S32* eaTags, StashTable stBuffStash)
{
	// This whole function is a bit hard to follow. It could stand to be cleaned up. 
	//
	// What happens is this:
	// Each PowerDef that applies AttribModNets is used as the key to a stashtable, 
	// where the stash value is a structure that holds an EArray of AttribModStacks. 
	// Each AttribModStack keeps track of the AttribModNet and the number of times its 
	// been seen. Afterward, each UIGenEntityBuff (the icon that appears on screen) loops
	// uses it's powerdef to look up how many times each attribmodnet has been stacked and 
	// displays the largest value found. 
	//
	// Also, an index is kept that increases each frame. This is done so that I don't have
	// allocate and free stuff every frame. Instead, I can just write over what's there, 
	// and free what wasn't used at the end of the frame.
	UIGenEntityBuff ***peaBuffs = ui_GenGetManagedListSafe(pGen, UIGenEntityBuff);
	Character *pChar = SAFE_MEMBER(pEntity, pChar);
	UIStackGroupData **eaStackGroups = EntGetUIStackGroupData(pEntity);
	S32 i, j, iCurIndex = 0, iTags = ea32Size(&eaTags);
	static S32 s_iUpdateIndex = 0;
	static AttribModNet **s_ppBuffModNets = NULL;
	S32 bDev = entGetAccessLevel(entActivePlayerPtr()) > 0;
	AutoDescDetail eDetail = entGetPowerAutoDescDetail(entActivePlayerPtr(),true);
	Language lang = langGetCurrent();

	s_iUpdateIndex++;

	if(pChar == NULL)
	{
		ui_GenSetListSafe(pGen, NULL, UIGenEntityBuff);
		return iCurIndex;
	}

	for(i=0; i < eaSize(&pChar->ppModsNet); i++)
	{
		AttribModNet *pModNet = pChar->ppModsNet[i];
		PowerDef *pDef = GET_REF(pModNet->hPowerDef);
		AttribModDef *pModDef = pDef ? eaGet(&pDef->ppOrderedMods,pModNet->uiDefIdx) : NULL;
		StashElement elem;
		S32 eTag = -1;

		UIGenEntityBuff *pBuff;
		PowerDefBuffList *pBuffList;
		EntityBuffData *pData;
		AttribModStack *pStack = NULL;
		UIStackGroupData *pStackGroup = NULL;

		// TODO(JW): UI: Add code for unknown buffs
		if(!ATTRIBMODNET_VALID(pModNet) || !pDef)
			continue;

		if (eaTags)
		{
			for (j = iTags-1; j >= 0; j--)
			{
				if ((pModDef && powertags_Check(&pModDef->tags,eaTags[j])) || 
					(pModDef == NULL && powertags_Check(&pDef->tags,eaTags[j])))
				{
					eTag = eaTags[j];
					break;
				}
			}
			if (j < 0)
			{
				continue;
			}
		}
		if (gConf.bEntBuffListOnlyShowBestModInStackGroup &&
			pModDef->eStackGroup != kModStackGroup_None &&
			!IsModBestInUIStackGroupArray(eaStackGroups, pModDef, i, &pStackGroup))
		{
			continue;
		}
		if (stashAddressFindElement(stBuffStash, pDef, &elem))
		{
			pData = (EntityBuffData*)stashElementGetPointer(elem);
			pBuffList = &pData->PowerDefData;
			if (pData->iUpdateIndex < s_iUpdateIndex)
			{
				pBuff = eaGetStruct(peaBuffs, parse_UIGenEntityBuff, iCurIndex++);
				ResetUIGenEntityBuff(pBuff,pDef,eTag,pChar,bDev);

				pStack = eaGetStruct(&pBuffList->eaModStack, parse_AttribModStack, 0);
				pStack->pModNet = pModNet;
				pStack->pStackGroup = pStackGroup;
				pStack->uiStack = 1;
				pBuffList->iSize = 1;
			}
			else 
			{
				// If this mod has been seen, increment it. 
				bool bFound = false;
				for (j = 0; j < pBuffList->iSize; j++)
				{
					pStack = eaGetStruct(&pBuffList->eaModStack, parse_AttribModStack, j);
					if (pStack->pModNet->uiDefIdx == pModNet->uiDefIdx)
					{
						pStack->uiStack++;
						bFound = true;
						break;
					}
				}
				// Otherwise, add it to the list of seen mods
				if (!bFound)
				{
					pStack = eaGetStruct(&pBuffList->eaModStack, parse_AttribModStack, pBuffList->iSize++);
					pStack->pModNet = pModNet;
					pStack->pStackGroup = pStackGroup;
					pStack->uiStack = 1;
				}
			}
			pData->iUpdateIndex = s_iUpdateIndex;
			pStack->iUpdateIndex = s_iUpdateIndex;
		}
		else
		{
			pBuff = eaGetStruct(peaBuffs, parse_UIGenEntityBuff, iCurIndex++);
			ResetUIGenEntityBuff(pBuff,pDef,eTag,pChar,bDev);

			// Create the entry for the stashtable for keeping track of stacks
			pData = StructCreate(parse_EntityBuffData);
			pData->iUpdateIndex = s_iUpdateIndex;
			pData->eType = kEntityBuffType_PowerDef;
			pBuff->pData = pData;
			pBuffList = &pData->PowerDefData;
			pBuffList->iSize = 1;
			pStack = StructCreate(parse_AttribModStack);
			pStack->pModNet = pModNet;
			pStack->pStackGroup = pStackGroup;
			pStack->iUpdateIndex = s_iUpdateIndex;
			pStack->uiStack = 1;
			eaPush(&pBuffList->eaModStack, pStack);
			stashAddressAddPointer(stBuffStash, pDef, pData, false);
		}
	}

	// Clear out anything that wasn't updated this frame (which means it's dead)
	gclEntBuffCleanup(stBuffStash, s_iUpdateIndex, true);

	// Decide the stack and timer values for each remaining UIGenEntityBuff
	for (i = 0; i < eaSize(peaBuffs); i++)
	{
		UIGenEntityBuff *pBuff = (*peaBuffs)[i];
		StashElement elem = NULL;
		PowerDefBuffList *pBuffList = NULL;
		EntityBuffData *pBuffData = NULL;
		PowerDef *pPowerDef = GET_REF(pBuff->hPowerDef);
		int iModCount = 0;

		if (!pPowerDef)
			continue;

		// Stack
		stashAddressFindElement(stBuffStash, pPowerDef, &elem);
		pBuffData = elem ? (EntityBuffData*)stashElementGetPointer(elem) : NULL;
		if (pBuffData) {
			pBuffList = &pBuffData->PowerDefData;
			for (j = 0; j < pBuffList->iSize; j++) {
				MAX1(pBuff->uiStack, pBuffList->eaModStack[j]->uiStack);
			}
		}

		// Timer
		eaClearFast(&s_ppBuffModNets);
		for (j=eaSize(&pChar->ppModsNet)-1; j>=0; j--)
		{
			AttribModNet *pModNet = pChar->ppModsNet[j];
			if (ATTRIBMODNET_VALID(pModNet) && GET_REF(pModNet->hPowerDef) == pPowerDef) 
			{
				eaPush(&s_ppBuffModNets,pModNet);
				UpdateUIGenEntityBuffDuration(pChar,pBuff,pModNet);
			}
		}

		if (gConf.bUseNNOPowerDescs)
		{
			powerdef_UseNNOFormattingForBuff(pPowerDef, &pBuff->pchAutoDesc);
		}
		else
		{
			// AutoDesc
			iModCount = eaSize(&s_ppBuffModNets);
			eaQSort(s_ppBuffModNets,SortAttribModNets);
			for(j=0; j<BUFF_AUTODESC_MAX && j<iModCount; j++)
			{
				if(pBuff->pchAutoDesc && *pBuff->pchAutoDesc)
					estrAppend2(&pBuff->pchAutoDesc,"<br>");
				modnet_AutoDesc(pChar,s_ppBuffModNets[j],&pBuff->pchAutoDesc,lang,eDetail);
			}
			if(iModCount>BUFF_AUTODESC_MAX)
				estrAppend2(&pBuff->pchAutoDesc,"<br>...");
		}
	}
	eaSetSizeStruct(peaBuffs, parse_UIGenEntityBuff, iCurIndex);
	if(eaSize(peaBuffs)>1)
		eaQSort_s(*peaBuffs, SortEntBuffs, iTags > 1 ? eaTags : NULL);
	ui_GenSetManagedListSafe(pGen, peaBuffs, UIGenEntityBuff, true);
	return iCurIndex;
}

// Builds a gen's list of UIGenEntityBuffs based on AttribMods, with an optional list of PowerTags.
// Use this to correctly display multiple buffs/debuffs from a single source PowerDef. 
// EntGetBuffsByPower will not handle this correctly. This function should probably eventually 
// replace EntGetBuffsByPower, but I'm leaving it so that buff lists don't break in Champions.
static S32 EntGetBuffsByAttribMod(UIGen *pGen, Entity *pEntity, S32* eaTags, StashTable stBuffStash)
{
	UIGenEntityBuff ***peaBuffs = ui_GenGetManagedListSafe(pGen, UIGenEntityBuff);
	Character *pChar = SAFE_MEMBER(pEntity, pChar);
	UIStackGroupData **eaStackGroups = EntGetUIStackGroupData(pEntity);
	S32 i, j, iCurIndex = 0, iTags = ea32Size(&eaTags);
	static S32 s_iUpdateIndex = 0;
	S32 bDev = entGetAccessLevel(entActivePlayerPtr()) > 0;
	AutoDescDetail eDetail = entGetPowerAutoDescDetail(entActivePlayerPtr(),true);
	Language lang = langGetCurrent();

	s_iUpdateIndex++;

	if(pChar == NULL)
	{
		return iCurIndex;
	}

	for(i=0; i < eaSize(&pChar->ppModsNet); i++)
	{
		AttribModNet *pModNet = pChar->ppModsNet[i];
		PowerDef *pDef = GET_REF(pModNet->hPowerDef);
		AttribModDef *pModDef = pDef ? eaGet(&pDef->ppOrderedMods,pModNet->uiDefIdx) : NULL;
		StashElement elem;
		S32 eTag = -1;
		UIGenEntityBuff *pBuff;
		EntityBuffData *pData;
		AttribModBuffData *pBuffData;
		UIStackGroupData *pStackGroup = NULL;

		// TODO(JW): UI: Add code for unknown buffs
		if(!ATTRIBMODNET_VALID(pModNet) || !pModDef)
			continue;

		if (eaTags)
		{
			for (j = iTags-1; j >= 0; j--)
			{
				if (powertags_Check(&pModDef->tags,eaTags[j]))
				{
					eTag = eaTags[j];
					break;
				}
			}
			if (j < 0)
			{
				continue;
			}
		}
		if (gConf.bEntBuffListOnlyShowBestModInStackGroup &&
			pModDef->eStackGroup != kModStackGroup_None &&
			!IsModBestInUIStackGroupArray(eaStackGroups, pModDef, i, &pStackGroup))
		{
			continue;
		}
		if (stashAddressFindElement(stBuffStash, pModDef, &elem))
		{
			pData = (EntityBuffData*)stashElementGetPointer(elem);
			pBuffData = &pData->AttribModData;
			if (pData->iUpdateIndex < s_iUpdateIndex)
			{
				pBuff = eaGetStruct(peaBuffs, parse_UIGenEntityBuff, iCurIndex++);
				ResetUIGenEntityBuff(pBuff,pDef,eTag,pChar,bDev);

				pBuffData->pStackGroup = pStackGroup;
				ea32Clear(&pBuffData->eaModNetIdx);
				ea32Push(&pBuffData->eaModNetIdx, i);
				pData->iUpdateIndex = s_iUpdateIndex;
			}
			else 
			{
				ea32Push(&pBuffData->eaModNetIdx, i);
			}
		}
		else
		{
			pBuff = eaGetStruct(peaBuffs, parse_UIGenEntityBuff, iCurIndex++) ;
			ResetUIGenEntityBuff(pBuff,pDef,eTag,pChar,bDev);

			pData = StructCreate(parse_EntityBuffData);
			pData->iUpdateIndex = s_iUpdateIndex;
			pData->eType = kEntityBuffType_AttribMod;
			pBuff->pData = pData;
			pBuffData = &pData->AttribModData;
			pBuffData->pStackGroup = pStackGroup;
			ea32Push(&pBuffData->eaModNetIdx, i);
			stashAddressAddPointer(stBuffStash, pModDef, pData, false);
		}
	}

	// Clear out anything that wasn't updated this frame (which means it's dead)
	gclEntBuffCleanup(stBuffStash, s_iUpdateIndex, true);

	// Decide the stack and timer values for each remaining UIGenEntityBuff
	for (i = 0; i < iCurIndex; i++)
	{
		UIGenEntityBuff *pBuff = (*peaBuffs)[i];
		AttribModBuffData *pData = &pBuff->pData->AttribModData;
		int s = pBuff->uiStack = ea32Size(&pData->eaModNetIdx);

		for (j = 0; j < s; j++)
		{
			AttribModNet *pModNet = eaGet(&pChar->ppModsNet, pData->eaModNetIdx[j]);
			PowerDef *pDef = pModNet ? GET_REF(pModNet->hPowerDef) : NULL;
			if (!pDef || !ATTRIBMODNET_VALID(pModNet))
				continue;

			if(j<BUFF_AUTODESC_MAX)
			{
				if(pBuff->pchAutoDesc && *pBuff->pchAutoDesc)
					estrAppend2(&pBuff->pchAutoDesc,"<br>");
				modnet_AutoDesc(pChar,pModNet,&pBuff->pchAutoDesc,lang,eDetail);
			}
			else if(j==BUFF_AUTODESC_MAX)
			{
				estrAppend2(&pBuff->pchAutoDesc,"<br>...");
			}

			UpdateUIGenEntityBuffDuration(pChar,pBuff,pModNet);
		}
	}
	eaSetSizeStruct(peaBuffs, parse_UIGenEntityBuff, iCurIndex);
	if (iCurIndex)
	{
		eaQSort_s(*peaBuffs, SortEntBuffs, iTags > 1 ? eaTags : NULL);
	}
	ui_GenSetManagedListSafe(pGen, peaBuffs, UIGenEntityBuff, true);
	return iCurIndex;
}

static StashTable gclGenGetBuffStashTable(SA_PARAM_NN_VALID UIGen* pGen)
{
	static UIGen **eaGenIdentifier = NULL;
	static StashTable *eaBuffStashes = NULL;
	StashTable pBuffStash = NULL;
	int i;

	for (i = 0; i < eaSize(&eaGenIdentifier); i++)
	{
		if (eaGenIdentifier[i] == pGen)
		{
			pBuffStash = eaBuffStashes[i];
			break;
		}
	}
	if (pBuffStash == NULL)
	{
		pBuffStash = stashTableCreateAddress(16);
		eaPush(&eaGenIdentifier, pGen);
		eaPush(&eaBuffStashes, pBuffStash);
	}
	return pBuffStash;
}

static void gclGetPowerTagsFromString(ExprContext *pContext, const char* pchTags, S32** peaTags)
{
	static S32* s_eaTags = NULL;
	char* pchContext;
	char* pchStart;
	char* pchTagsCopy;
	ea32Clear(&s_eaTags);
	if (!pchTags || !pchTags[0])
	{
		(*peaTags) = NULL;
		return;
	}
	strdup_alloca(pchTagsCopy, pchTags);
	pchStart = strtok_r(pchTagsCopy, " ,\t\r\n", &pchContext);
	do
	{
		if (pchStart)
		{
			S32 eTag = StaticDefineIntGetInt(PowerTagsEnum,pchStart);
			if (eTag != -1)
			{
				ea32Push(&s_eaTags, eTag);
			}
			else
			{
				const char* pchBlameFile = exprContextGetBlameFile(pContext);
				ErrorFilenamef(pchBlameFile, "Power Tag %s not recognized", pchStart);
			}
		}
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	(*peaTags) = s_eaTags;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetParentPowerOfBuff");
SA_RET_OP_VALID PowerDef* exprGetParentPowerOfBuff(ExprContext *pContext, SA_PARAM_OP_VALID UIGenEntityBuff* pBuff)
{
	if(pBuff)
		return GET_REF(pBuff->hPowerDef);
	else
		return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBuffsByPowerWithTag");
void exprEntGetBuffsByPowerWithTag(ExprContext *pContext,
								   SA_PARAM_NN_VALID UIGen *pGen,
								   SA_PARAM_OP_VALID Entity *pEntity,
								   ACMD_EXPR_ENUM(PowerTag) const char *pchTag)
{
	S32* eaTags = NULL;
	gclGetPowerTagsFromString(pContext, pchTag, &eaTags);
	EntGetBuffsByPower(pGen,pEntity,eaTags,gclGenGetBuffStashTable(pGen));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntBuffPowerGetDuration");
S32 exprEntBuffPowerGetDuration(SA_PARAM_OP_VALID Entity *pEntity, const char* pchName)
{
	Character *pChar = SAFE_MEMBER(pEntity, pChar);
	S32 i;

	if (!pChar)
		return false;

	for(i=0; i < eaSize(&pChar->ppModsNet); i++)
	{
		AttribModNet *pModNet = pChar->ppModsNet[i];
		PowerDef *pDef = GET_REF(pModNet->hPowerDef);
		if (pDef && strcmp(pDef->pchName, pchName) == 0 && pModNet->uiDurationOriginal > 0)
			return character_ModNetGetPredictedDuration(pChar, pModNet);
	}
	return 0;
}

// returns true if any of the ppModsNet on the character come from the given powerDef
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntHasBuffByPower");
S32 exprEntHasBuffByPower(SA_PARAM_OP_VALID Entity *pEntity, const char* pchName)
{
	Character *pChar = SAFE_MEMBER(pEntity, pChar);
	PowerDef *pFindDef = powerdef_Find(pchName);

	if (!pChar || !pFindDef)
		return false;

	FOR_EACH_IN_EARRAY(pChar->ppModsNet, AttribModNet, pModNet)
	{
		if (ATTRIBMODNET_VALID(pModNet))
		{
			if (pFindDef == GET_REF(pModNet->hPowerDef))
				return true;
		}
	}
	FOR_EACH_END

	return false;
}

// returns true if any of the ppModsNet on the character have the given tag
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntHasBuffByTags");
S32 exprEntHasBuffByTags(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char* pchTags)
{
	Character *pChar = SAFE_MEMBER(pEntity, pChar);
	static S32* s_eaiTags = NULL;

	eaiClear(&s_eaiTags);
	gclGetPowerTagsFromString(pContext, pchTags, &s_eaiTags);

	if (!pChar || !eaiSize(&s_eaiTags))
	{
		return false;
	}

	FOR_EACH_IN_EARRAY(pChar->ppModsNet, AttribModNet, pModNet)
	{
		if (ATTRIBMODNET_VALID(pModNet))
		{
			PowerDef *pDef = GET_REF(pModNet->hPowerDef);
			AttribModDef *pModDef = pDef ? eaGet(&pDef->ppOrderedMods, pModNet->uiDefIdx) : NULL;
			if (pModDef)
			{
				FOR_EACH_IN_EARRAY_INT(s_eaiTags, S32, iTag)
				{
					if (powertags_Check(&pModDef->tags, iTag))
						return true;
				}
				FOR_EACH_END
			}
		}
	}
	FOR_EACH_END

	return false;
}

static int SortAttribModNet(const AttribModNet** ppA, const AttribModNet** ppB)
{
	return (S32)(*ppA)->uiDuration - (S32)(*ppB)->uiDuration;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBuffConcatenatedTooltip");
const char *exprEntGetBuffConcatenatedTooltip(	ExprContext *pContext, 
												SA_PARAM_OP_VALID Entity *pEntity, 
												const char* pchTags, 
												const char *pchMessageKeyFormat)
{
	Character *pChar = SAFE_MEMBER(pEntity, pChar);
	static unsigned char *s_pchOut = NULL;
	static S32* s_eaiTags = NULL;
	static AttribModNet** s_eaList = NULL;
	Language lang = LANGUAGE_DEFAULT;
	
	eaiClear(&s_eaiTags);
	gclGetPowerTagsFromString(pContext, pchTags, &s_eaiTags);

	if (!pChar || !eaiSize(&s_eaiTags))
	{
		return "";
	}
	
	eaClear(&s_eaList);
	estrClear(&s_pchOut);
	lang = langGetCurrent();

	FOR_EACH_IN_EARRAY_FORWARDS(pChar->ppModsNet, AttribModNet, pModNet)
	{
		if (ATTRIBMODNET_VALID(pModNet))
		{
			PowerDef *pPowerDef = GET_REF(pModNet->hPowerDef);
			AttribModDef *pModDef = pPowerDef ? eaGet(&pPowerDef->ppOrderedMods, pModNet->uiDefIdx) : NULL;
			if (pModDef)
			{
				FOR_EACH_IN_EARRAY_INT(s_eaiTags, S32, iTag)
				{
					if (powertags_Check(&pModDef->tags, iTag))
					{
						eaPush(&s_eaList, pModNet);
						break;
					}
				}
				FOR_EACH_END
			}
		}
	}
	FOR_EACH_END
		
	if (eaSize(&s_eaList))
	{
		char *pchString  = NULL;
		estrStackCreate(&pchString);

		eaQSort(s_eaList, SortAttribModNet);
		
		FOR_EACH_IN_EARRAY(s_eaList, AttribModNet, pModNet)
		{
			PowerDef *pPowerDef = GET_REF(pModNet->hPowerDef);
			estrClear(&pchString);

			langFormatGameMessageKey(lang, &pchString, pchMessageKeyFormat,
				STRFMT_STRING("DescShort", NULL_TO_EMPTY(TranslateDisplayMessage(pPowerDef->msgDisplayName))),
				STRFMT_STRING("AutoDesc", NULL_TO_EMPTY(TranslateDisplayMessage(pPowerDef->msgDescriptionLong))),
				STRFMT_END);

			if (FOR_EACH_IDX(-,pModNet) != 0)
			{
				estrConcatf(&s_pchOut, "%s<br><br>", pchString);
			}
			else
			{
				estrConcatf(&s_pchOut, "%s", pchString);
			}
		}
		FOR_EACH_END

		estrDestroy(&pchString);
	}

	return s_pchOut ? exprContextAllocString(pContext, s_pchOut) : "";
}

//adds up all the magnitudes of buffs with the given powertag. Used for stuff like Temporary HP in NNO
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntAddUpBuffHealthByTag");
S32 exprEntAddUpBuffsByTag(ExprContext *pContext,
								   SA_PARAM_OP_VALID Entity *pEntity,
								   ACMD_EXPR_ENUM(PowerTag) const char *pchTag)
{
	S32* eaTags = NULL;
	Character *pChar = SAFE_MEMBER(pEntity, pChar);
	S32 i, j, sum = 0;

	if (!pChar)
		return 0;

	gclGetPowerTagsFromString(pContext, pchTag, &eaTags);

	for(i=0; i < eaSize(&pChar->ppModsNet); i++)
	{
		AttribModNet *pModNet = pChar->ppModsNet[i];
		PowerDef *pDef = GET_REF(pModNet->hPowerDef);
		AttribModDef *pModDef = pDef ? eaGet(&pDef->ppOrderedMods,pModNet->uiDefIdx) : NULL;
		S32 eTag = -1;

		// TODO(JW): UI: Add code for unknown buffs
		if(!ATTRIBMODNET_VALID(pModNet) || !pModDef)
			continue;

		if (eaTags)
		{
			for (j = ea32Size(&eaTags)-1; j >= 0; j--)
			{
				if (powertags_Check(&pModDef->tags,eaTags[j]))
				{
					eTag = eaTags[j];
					break;
				}
			}
			if (j < 0)
			{
				continue;
			}
		}
		sum += pModNet->iHealth;
	}
	return sum;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBuffsByPowerWithTags");
void exprEntGetBuffsByPowerWithTags(ExprContext *pContext,
									SA_PARAM_NN_VALID UIGen* pGen,
									SA_PARAM_OP_VALID Entity* pEntity,
									const char* pchTags)
{
	S32* eaTags = NULL;
	gclGetPowerTagsFromString(pContext, pchTags, &eaTags);
	EntGetBuffsByPower(pGen,pEntity,eaTags,gclGenGetBuffStashTable(pGen));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBuffsByPower");
void exprEntGetBuffsByPower(ExprContext *pContext,
							SA_PARAM_NN_VALID UIGen *pGen,
							SA_PARAM_OP_VALID Entity *pEntity)
{
	exprEntGetBuffsByPowerWithTag(pContext,pGen,pEntity,NULL);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBuffsByAttribModWithTags");
void exprEntGetBuffsByAttribModWithTags(ExprContext *pContext,
									SA_PARAM_NN_VALID UIGen* pGen,
									SA_PARAM_OP_VALID Entity* pEntity,
									const char* pchTags)
{
	S32* eaTags = NULL;
	gclGetPowerTagsFromString(pContext, pchTags, &eaTags);
	EntGetBuffsByAttribMod(pGen,pEntity,eaTags,gclGenGetBuffStashTable(pGen));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBuffsByAttribute");
void exprEntGetBuffsByAttribute(SA_PARAM_NN_VALID UIGen *pGen,
								SA_PARAM_OP_VALID Entity *pEntity)
{
	static UIGenEntityBuff **s_ppBuffs = NULL;
	S32 i;
	eaClear(&s_ppBuffs);	// Gets destroyed by list manager
	if(pEntity && pEntity->pChar)
	{
		char *pchIcon = NULL;
		estrStackCreate(&pchIcon);
		for(i=eaSize(&pEntity->pChar->ppModsNet)-1; i>=0; i--)
		{
			if(ATTRIBMODNET_VALID(pEntity->pChar->ppModsNet[i]))
			{
				PowerDef *pdef = GET_REF(pEntity->pChar->ppModsNet[i]->hPowerDef);
				if(pdef && (int)pEntity->pChar->ppModsNet[i]->uiDefIdx < eaSize(&pdef->ppOrderedMods))
				{
					int j;
					AttribModDef *pmoddef = pdef->ppOrderedMods[pEntity->pChar->ppModsNet[i]->uiDefIdx];
					UIGenEntityBuff *pbuff = NULL;
					const char *pchAttribute = StaticDefineIntRevLookup(AttribTypeEnum,pmoddef->offAttrib);
					
					for(j=eaSize(&s_ppBuffs)-1; j>=0; j--)
					{
						if(s_ppBuffs[j]->pchIcon==pchAttribute)
						{
							pbuff = s_ppBuffs[j];
							break;
						}
					}
					
					if(!pbuff)
					{
						pbuff = StructAlloc(parse_UIGenEntityBuff);
						pbuff->pchIcon = allocAddString(pchAttribute);
						// TODO(JW): UI: This needs to be an AutoDesc message?
						pbuff->pchDescShort = pchAttribute;
						eaPush(&s_ppBuffs,pbuff);
					}

					// TODO(JW): UI: Build a description of all the effects here
					pbuff->pchDescLong = NULL;
				}
				else
				{
					// TODO(JW): UI: Add code here for unknown buffs
				}
			}
		}

		for(i=eaSize(&s_ppBuffs)-1; i>=0; i--)
		{
			estrCopy2(&pchIcon,"DefaultAttributeIcon_");
			estrAppend2(&pchIcon,s_ppBuffs[i]->pchIcon);
			if(texFindAndFlag(pchIcon,false,0))
			{
				s_ppBuffs[i]->pchIcon = allocAddString(pchIcon);
			}
			else
			{
				s_ppBuffs[i]->pchIcon = "Power_Generic";
			}
		}
		estrDestroy(&pchIcon);
	}
	ui_GenSetManagedListSafe(pGen, &s_ppBuffs, UIGenEntityBuff, true);
}

static S32 gclUIStackGroupDataGetModNetList(SA_PARAM_NN_VALID Entity* pEnt,
											SA_PARAM_NN_VALID UIStackGroupData* pStackGroup, 
											bool bExcludeBest,
											AttribModNet*** peaModNet)
{
	if (pEnt->pChar && peaModNet)
	{
		S32 i;
		for (i = 0; i < ea32Size(&pStackGroup->piModNetIndices); i++)
		{
			AttribModNet *pModNet = eaGet(&pEnt->pChar->ppModsNet, pStackGroup->piModNetIndices[i]);
			if (pModNet && (!bExcludeBest || pStackGroup->piModNetIndices[i] != pStackGroup->iModNetIdxBest))
			{
				eaPush(peaModNet, pModNet);
			}
		}
		return eaSize(peaModNet);
	}
	return 0;
}

static S32 gclEntBuffGetStackGroupModList(SA_PARAM_OP_VALID UIGenEntityBuff* pBuff, 
										  bool bExcludeBest,
										  AttribModNet*** peaModNet)
{
	Entity* pEnt = entActivePlayerPtr();
	UIStackGroupData* pStackGroup = NULL;

	if (pEnt && pEnt->pChar && pBuff && pBuff->pData)
	{
		switch (pBuff->pData->eType)
		{
			xcase kEntityBuffType_PowerDef:
			{
				S32 i;
				for (i = 0; i < pBuff->pData->PowerDefData.iSize; i++)
				{
					AttribModStack* pStack = pBuff->pData->PowerDefData.eaModStack[i];
					if (pStack->pStackGroup)
					{
						pStackGroup = pStack->pStackGroup;
						break;
					}
				}
			}
			xcase kEntityBuffType_AttribMod:
			{
				pStackGroup = pBuff->pData->AttribModData.pStackGroup;
			}
			xdefault:
			{
				Errorf("Unknown entity buff type for buff %s", pBuff->pchNameInternal);
			}
		}
	}
	if (pStackGroup)
	{
		if (peaModNet)
		{
			return gclUIStackGroupDataGetModNetList(pEnt, pStackGroup, bExcludeBest, peaModNet);
		}	
		else
		{
			S32 iSize = ea32Size(&pStackGroup->piModNetIndices);
			return bExcludeBest ? MAX(iSize-1, 0) : iSize;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenEntBuffGetStackGroupBuffList");
S32 exprGenEntBuffGetStackGroupBuffList(SA_PARAM_NN_VALID UIGen* pGen,
									    SA_PARAM_OP_VALID UIGenEntityBuff* pBuff, 
										bool bExcludeBest,
									    S32 iMaxSize)
{
	UIGenEntityBuff*** peaBuffs = ui_GenGetManagedListSafe(pGen, UIGenEntityBuff);
	AttribModNet** eaModNet = NULL;
	S32 i, iSize = gclEntBuffGetStackGroupModList(pBuff, bExcludeBest, &eaModNet);
	S32 iCount = 0;
	Entity* pEnt = entActivePlayerPtr();
	S32 bDev = entGetAccessLevel(pEnt) > 0;
	AutoDescDetail eDetail = entGetPowerAutoDescDetail(pEnt,true);
	Language eLang = entGetLanguage(pEnt);
	
	if (!pEnt || !pEnt->pChar)
	{
		return 0;
	}
	iSize = iMaxSize < 0 ? iSize : MIN(iSize, iMaxSize);
	for (i = 0; i < iSize; i++)
	{
		AttribModNet* pModNet = eaModNet[i];
		PowerDef* pPowDef = GET_REF(pModNet->hPowerDef);
		if (pPowDef)
		{
			UIGenEntityBuff* pData = eaGetStruct(peaBuffs, parse_UIGenEntityBuff, iCount++);
			ResetUIGenEntityBuff(pData, pPowDef, pBuff->ePowerTag, pEnt->pChar, bDev); 
			modnet_AutoDesc(pEnt->pChar, pModNet, &pData->pchAutoDesc, eLang, eDetail);
			pData->uiDuration = character_ModNetGetPredictedDuration(pEnt->pChar, pModNet);
			pData->uiDurationOriginal = pModNet->uiDurationOriginal;
		}
	}
	eaSetSizeStruct(peaBuffs, parse_UIGenEntityBuff, iCount);
	if (eaSize(peaBuffs) > 1)
	{
		eaQSort_s(*peaBuffs, SortEntBuffs, NULL);
	}
	ui_GenSetManagedListSafe(pGen, peaBuffs, UIGenEntityBuff, true);
	eaDestroy(&eaModNet);
	return iSize;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntBuffGetStackGroupSize");
S32 exprEntBuffGetStackGroupSize(SA_PARAM_OP_VALID UIGenEntityBuff* pBuff, bool bExcludeBest)
{
	return gclEntBuffGetStackGroupModList(pBuff, bExcludeBest, NULL);
}

/// Callback for EntityCopyDict dictionary changes.
///
/// This is needed in case a pet changes costumes
static void entityCopyDictChanged(
		enumResourceEventType eType, const char *pDictName, const char *pRefData,
		Referent pReferent, void *pUserData )
{
	Entity* pEnt = (Entity*)pReferent;
	
	switch( eType ) {
		case RESEVENT_RESOURCE_MODIFIED: {
			ContainerRef *pRef;
			int i;
			
			for(i=eaSize(&s_eaChangedEntities)-1; i>=0; --i) {
				pRef =s_eaChangedEntities[i];
				if ((pRef->containerID == pEnt->myContainerID) && (pRef->containerType == pEnt->myEntityType)) {
					return;
				}
			}

			pRef = StructCreate(parse_ContainerRef);
			pRef->containerID = pEnt->myContainerID;
			pRef->containerType = pEnt->myEntityType;
			eaPush(&s_eaChangedEntities, pRef);
			break;
		}
		case RESEVENT_RESOURCE_REMOVED: {
			ContainerRef *pRef;
			int i;
			for(i=eaSize(&s_eaChangedEntities)-1; i>=0; --i) {
				pRef = s_eaChangedEntities[i];
				if ((pRef->containerID == pEnt->myContainerID) && (pRef->containerType == pEnt->myEntityType)) {
					StructDestroy(parse_ContainerRef, pRef);
					eaRemove(&s_eaChangedEntities, i);
					break;
				}
			}
		}
	}
}


//DEPRECATED; Use UIGenPaperdoll instead
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetHeadshotFromStyle);
SA_RET_OP_VALID BasicTexture *exprEntGetHeadshotFromStyle(	SA_PARAM_NN_VALID ExprContext *pContext,
														SA_PARAM_OP_VALID Entity *pEntity, 
														SA_PARAM_OP_VALID BasicTexture *pTexture, 
														const char* pchHeadshotStyle,
														const char* pchAnimBits,
														F32 fWidth, F32 fHeight, 
														bool bForceRedraw )
{
	return NULL;
}

//DEPRECATED; Use UIGenPaperdoll instead
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetHeadshotBackgroundFromStyle);
const char* exprGetHeadshotBackgroundFromStyle( SA_PARAM_OP_VALID BasicTexture* pTexture, const char* pchHeadshotStyle )
{
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsHeadshotDoneLoading);
bool exprIsHeadshotDoneLoading( SA_PARAM_OP_VALID BasicTexture* pTexture )
{
	return false;
}

// DEPRECATED; Use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetHeadshot);
SA_RET_OP_VALID BasicTexture *exprEntGetHeadshot(SA_PARAM_NN_VALID ExprContext *pContext,
											  SA_PARAM_OP_VALID Entity *pEntity, 
											  SA_PARAM_OP_VALID BasicTexture *pTexture, 
											  const char* pchBackground, 
											  const char* pchBitString,
											  F32 fWidth, F32 fHeight, 
											  bool bForceRedraw)
{
	return NULL;
}

// DEPRECATED; Use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetTransparentHeadshot);
SA_RET_OP_VALID BasicTexture *exprEntGetTransparentHeadshot(SA_PARAM_NN_VALID ExprContext *pContext,
														 SA_PARAM_OP_VALID Entity *pEntity, 
														 SA_PARAM_OP_VALID BasicTexture *pTexture, 
														 const char* pchBitString,
														 F32 fWidth, F32 fHeight,
														 bool bForceRedraw)
{
	return NULL;
}

// DEPRECATED; Use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetTransparentHeadshotByType);
SA_RET_OP_VALID BasicTexture *exprEntGetTransparentHeadshotByType(SA_PARAM_NN_VALID ExprContext *pContext,
															   SA_PARAM_OP_VALID Entity *pEntity, 
															   SA_PARAM_OP_VALID BasicTexture *pTexture, 
															   const char* pchBitString,
															   F32 fWidth, F32 fHeight,
															   bool bForceRedraw, const char *pchClassType)
{
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPowerEnhDescription");
char *gclGenExprPowerEnhDescription(ExprContext *pContext, const char *pchNode, const char *pchEnh, const char *pchMessage)
{
	PTNodeDef *pNode = RefSystem_ReferentFromString("PowerTreeNodeDef", pchNode);
	PowerDef *pEnh = RefSystem_ReferentFromString("PowerDef",pchEnh);
	static char *s_pchDescription = NULL;
	Entity *e = entActivePlayerPtr();
	Character *pChar = NULL;
	int iLevel = 0;

	if(e && e->pChar)
	{
		pChar = e->pChar;
		if(pChar && pChar->iLevelCombat)
			iLevel = pChar->iLevelCombat;
	}
	else
		return "Invalid Entity";

	estrClear(&s_pchDescription);
	if(pNode && pEnh)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		char *pchTemp = NULL;

		powerdef_AutoDesc(PARTITION_CLIENT,pEnh,&pchTemp,NULL,"<br>","<bsp><bsp>","* ",pChar,NULL,NULL,iLevel,true,entGetPowerAutoDescDetail(e,false),pExtract,NULL);
		FormatMessageKey(&s_pchDescription,pchMessage,STRFMT_STRUCT("Power", pEnh, parse_PowerDef), STRFMT_STRING("PowerAutoDesc",pchTemp), STRFMT_END);
		estrDestroy(&pchTemp);

		return s_pchDescription;
	}
	else
		return "Unknown Node or Enhancement";

	
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPowerDefDescription");
char *gclGenExprPowerDefDescription(ExprContext *pContext, SA_PARAM_OP_VALID PowerDef *pDef) {
	static char *s_pchDescription = NULL;

	estrClear(&s_pchDescription);
	if (pDef) {
		Entity *pEntity = entActivePlayerPtr();
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		Character *pChar = pEntity ? pEntity->pChar : NULL;
		int iLevel = pChar && pChar->iLevelCombat ? pChar->iLevelCombat : 1;
		powerdef_AutoDesc(PARTITION_CLIENT,pDef,&s_pchDescription,NULL,"<br>","<bsp><bsp>","- ",
			pChar,NULL,NULL,iLevel,true,entGetPowerAutoDescDetail(pEntity,false), pExtract, NULL);
	}
	return s_pchDescription;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPowerDefDescriptionForEnt");
char *gclGenExprPowerDefDescriptionForEnt(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID PowerDef *pDef) {
	static char *s_pchDescription = NULL;

	estrClear(&s_pchDescription);
	if (pDef) {
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		Character *pChar = pEntity ? pEntity->pChar : NULL;
		int iLevel = pChar && pChar->iLevelCombat ? pChar->iLevelCombat : 1;
		powerdef_AutoDesc(PARTITION_CLIENT,pDef,&s_pchDescription,NULL,"<br>","<bsp><bsp>","- ",
			pChar,NULL,NULL,iLevel,true,entGetPowerAutoDescDetail(pEntity,false), pExtract, NULL);
	}
	return s_pchDescription;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowerDefGetDescription);
const char *gclGenExprPowerDefGetDescription(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID PowerDef *pDef, const char *pchPowerMessageKey, const char *pchAttribModsMessageKey)
{
	if (pEntity && pDef)
		return gclAutoDescPower(pEntity, NULL, pDef, pchPowerMessageKey, pchAttribModsMessageKey, true);
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPowerDefDescriptionByName");
char *gclGenExprPowerDefDescriptionByName(ExprContext *pContext, const char *pchPowerDefName) {
	PowerDef *pDef = powerdef_Find(pchPowerDefName);
	return gclGenExprPowerDefDescription(pContext, pDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPowerDefDescriptionForEntByName");
char *gclGenExprPowerDefDescriptionForEntByName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchPowerDefName) {
	PowerDef *pDef = powerdef_Find(pchPowerDefName);
	return gclGenExprPowerDefDescriptionForEnt(pContext, pEntity, pDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowerDefGetDescriptionByName);
const char *gclGenExprPowerDefGetDescriptionByName(SA_PARAM_OP_VALID Entity *pEntity, const char *pchPowerDefName, const char *pchPowerMessageKey, const char *pchAttribModsMessageKey)
{
	PowerDef *pDef = powerdef_Find(pchPowerDefName);
	return gclGenExprPowerDefGetDescription(pEntity, pDef, pchPowerMessageKey, pchAttribModsMessageKey);
}

char *gclGenPowerNodeDescriptionInternal(ExprContext *pContext,
										   const char *pchGroup, const char *pchNode, S32 iRank,
										   const char *pchPrologue,
										   const char *pchNodeReq, const char *pchGroupReq, const char *pchLevelReq,
										   const char *pchEpilogue,
										   const char *pchCustomAutoDesc,
										   const char *pchCustomAttribMod)
{
	static char *s_pchDescription = NULL;
	PTNodeDef *pNode = RefSystem_ReferentFromString("PowerTreeNodeDef", pchNode);
	PTGroupDef *pGroup = RefSystem_ReferentFromString("PowerTreeGroupDef", pchGroup);
	estrClear(&s_pchDescription);
	if (pGroup && pNode)
	{
		PTNodeRankDef *pRank = eaGet(&pNode->ppRanks, iRank);
		bool bHasAnyRequirements = false;
		S32 iMaxLevel = 0;
		S32 i;

		PTNodeDef *pNodeReq;
		if(*pchPrologue)
			FormatMessageKey(&s_pchDescription, pchPrologue, STRFMT_STRUCT("Node", pNode, parse_PTNodeDef), STRFMT_END);
		if (pGroup->pRequires)
		{
			MAX1(iMaxLevel, pGroup->pRequires->iTableLevel);
			if (pGroup->pRequires->iGroupRequired && GET_REF(pGroup->pRequires->hGroup))
			{
				PTGroupDef *pGroupReq = GET_REF(pGroup->pRequires->hGroup);
				if(*pchGroupReq)
					FormatMessageKey(&s_pchDescription, pchGroupReq, STRFMT_STRUCT("Group", pGroupReq, parse_PTGroupDef), STRFMT_INT("PointsReq",pGroup->pRequires->iGroupRequired), STRFMT_END);
				bHasAnyRequirements = true;
			}
			//TODO(BH): if(pGroup->pRequires->pExprPurchase)
		}
		if (pRank && pRank->pRequires)
		{
			MAX1(iMaxLevel, pRank->pRequires->iTableLevel);
			if (pRank->pRequires->iGroupRequired && GET_REF(pRank->pRequires->hGroup))
			{
				PTGroupDef *pGroupReq = GET_REF(pRank->pRequires->hGroup);
				if(*pchGroupReq)
					FormatMessageKey(&s_pchDescription, pchGroupReq, STRFMT_STRUCT("Group", pGroupReq, parse_PTGroupDef),STRFMT_INT("PointsReq",pGroup->pRequires->iGroupRequired), STRFMT_END);
				bHasAnyRequirements = true;
			}
			//TODO(BH): if(pRank->pRequires->pExprPurchase)
		}

		pNodeReq = GET_REF(pNode->hNodeRequire);
		if (pNodeReq)
		{
			if(*pchNodeReq)
				FormatMessageKey(&s_pchDescription, pchNodeReq, STRFMT_STRUCT("Node", pNodeReq, parse_PTNodeDef), STRFMT_INT("RankReq",pNode->iRequired), STRFMT_END);
			bHasAnyRequirements = true;
		}

		if (iMaxLevel)
		{
			if(*pchLevelReq)
				FormatMessageKey(&s_pchDescription, pchLevelReq, STRFMT_INT("Level", iMaxLevel), STRFMT_END);
			bHasAnyRequirements = true;
		}

		if (!bHasAnyRequirements)
			estrClear(&s_pchDescription);

		for (i = iRank; i >= 0; i--)
		{
			PowerDef *pDef = NULL;
			while (!pDef && i >= 0)
			{
				pRank = eaGet(&pNode->ppRanks, i);
				if (pRank)
					pDef = GET_REF(pRank->hPowerDef);
				i--;
			}
			if (pDef)
			{
				Entity *e = entActivePlayerPtr();
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
				Character *pChar = NULL;
				int iLevel = 1;
				char *pchTemp = NULL;

				estrStackCreate(&pchTemp);
				if(!e)
				{
					e = exprContextGetVarPointer(pContext,"Player",parse_Entity);
				}

				if(e)
					pChar = e->pChar;
				
				if(pChar && pChar->iLevelCombat)
					iLevel = pChar->iLevelCombat;

				if (pchCustomAutoDesc && *pchCustomAutoDesc)
				{
					AutoDescPower *pAutoDescPower = StructCreate(parse_AutoDescPower);
					powerdef_AutoDesc(PARTITION_CLIENT,pDef,NULL,pAutoDescPower,"<br>","<bsp><bsp>","* ",pChar,NULL,NULL,iLevel,true,entGetPowerAutoDescDetail(e,false),pExtract,NULL);
					powerdef_AutoDescCustom(e, &pchTemp, pDef, pAutoDescPower, pchCustomAutoDesc, pchCustomAttribMod);
					StructDestroy(parse_AutoDescPower, pAutoDescPower);
				}
				else
				{
					powerdef_AutoDesc(PARTITION_CLIENT,pDef,&pchTemp,NULL,"<br>","<bsp><bsp>","* ",pChar,NULL,NULL,iLevel,true,entGetPowerAutoDescDetail(e,false),pExtract,NULL);
				}

				FormatMessageKey(&s_pchDescription, pchEpilogue, STRFMT_STRUCT("Power", pDef, parse_PowerDef), STRFMT_STRING("PowerAutoDesc",pchTemp), STRFMT_END);
				estrDestroy(&pchTemp);
				break;
			}

		}

		return s_pchDescription;
	}
	else
		return "Unknown Group/Node Selected";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPowerNodeDescription");
char *gclGenExprPowerNodeDescription(ExprContext *pContext,
										   const char *pchGroup, const char *pchNode, S32 iRank,
										   const char *pchPrologue,
										   const char *pchNodeReq, const char *pchGroupReq, const char *pchLevelReq,
										   const char *pchEpilogue)
{
	return gclGenPowerNodeDescriptionInternal(pContext, pchGroup, pchNode, iRank, pchPrologue, pchNodeReq, pchGroupReq, pchLevelReq, pchEpilogue, NULL, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPowerNodeDescriptionCustom");
char *gclGenExprPowerNodeDescriptionCustom(ExprContext *pContext,
										   const char *pchGroup, const char *pchNode, S32 iRank,
										   const char *pchPrologue,
										   const char *pchNodeReq, const char *pchGroupReq, const char *pchLevelReq,
										   const char *pchEpilogue,
										   const char *pchCustomAutoDesc,
										   const char *pchCustomAttribMod)
{
	return gclGenPowerNodeDescriptionInternal(pContext, pchGroup, pchNode, iRank, pchPrologue, pchNodeReq, pchGroupReq, pchLevelReq, pchEpilogue, pchCustomAutoDesc, pchCustomAttribMod);
}

//////////////////////////////////////////////////////////////////////////

static bool s_bRememberUIPosition = true;

// Whether to remember UI sizes and positions. On by default.
AUTO_CMD_INT(s_bRememberUIPosition, UIRememberPositions) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC;

bool gclui_GenGetPosition(const char *pchName, UIPosition *pPosition, S32 iVersion, U8 chClone, U8 *pchPriority, const char **ppchContents)
{
	Entity *pEnt = entActivePlayerPtr();
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	char *pchCloneName = NULL;

	if (chClone)
	{
		S32 iLen = (S32)strlen(pchName) + 1 + (chClone < 10 ? 1 : chClone < 100 ? 2 : 3) + 1;
		pchCloneName = alloca(iLen);
		snprintf_s(pchCloneName, iLen, "%s_%d", pchName, (S32)chClone);
	}

	if (pUI && s_bRememberUIPosition)
	{
		UIPersistedPosition *pStored = eaIndexedGetUsingString(&pUI->eaStoredPositions, pchCloneName ? pchCloneName : pchName);
		if (pStored && pStored->iVersion == iVersion && iVersion > 0)
		{
			pPosition->eOffsetFrom = pStored->eOffsetFrom;
			pPosition->iX = pStored->iX;
			pPosition->iY = pStored->iY;
			pPosition->fPercentX = pStored->fPercentX;
			pPosition->fPercentY = pStored->fPercentY;
			pPosition->Width.fMagnitude = pStored->fWidth;
			pPosition->Height.fMagnitude = pStored->fHeight;
			*pchPriority = pStored->chPriority;
			if (ppchContents)
				*ppchContents = allocFindString(pStored->pchContents);
			return true;
		}
	}
	return false;
}

bool gclui_GenSetPosition(const char *pchName, const UIPosition *pPosition, S32 iVersion, U8 chClone, U8 chPriority, const char *pchContents)
{
	char *pchCloneName = NULL;

	if (chClone)
	{
		S32 iLen = (S32)strlen(pchName) + 1 + (chClone < 10 ? 1 : chClone < 100 ? 2 : 3) + 1;
		pchCloneName = alloca(iLen);
		snprintf_s(pchCloneName, iLen, "%s_%d", pchName, (S32)chClone);
	}

	if (iVersion > 0 && s_bRememberUIPosition)
	{
		UIPersistedPosition Stored;
		Stored.pchName = pchCloneName ? pchCloneName : pchName;
		Stored.eOffsetFrom = pPosition->eOffsetFrom;
		Stored.iVersion = iVersion;
		Stored.chPriority = chPriority;
		Stored.iX = pPosition->iX;
		Stored.iY = pPosition->iY;
		Stored.fPercentX = pPosition->fPercentX;
		Stored.fPercentY = pPosition->fPercentY;
		Stored.fWidth = pPosition->Width.fMagnitude;
		Stored.fHeight = pPosition->Height.fMagnitude;
		Stored.pchContents = pchContents;
		ServerCmd_gslCmdUpdateStoredUIPosition(&Stored);
		return true;
	}
	return false;
}

bool gclui_GenForgetPosition(const char *pchName, const UIPosition *pPosition, S32 iVersion, U8 chClone, U8 chPriority, const char *pchContents)
{
	char *pchCloneName = NULL;

	if (chClone)
	{
		S32 iLen = (S32)strlen(pchName) + 1 + (chClone < 10 ? 1 : chClone < 100 ? 2 : 3) + 1;
		pchCloneName = alloca(iLen);
		snprintf_s(pchCloneName, iLen, "%s_%d", pchName, (S32)chClone);
	}

	ServerCmd_gslCmdForgetStoredUIPosition(pchCloneName ? pchCloneName : pchName);
	return true;
}

static bool s_bRememberUILists = true;

// Whether to remember UI List Column placement and width.
AUTO_CMD_INT(s_bRememberUILists, RememberUILists) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC;

void gclui_GenGetListOrder(const char *pchName, UIColumn ***peaCols, S32 iVersion)
{
	Entity *pEnt = entActivePlayerPtr();
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	if (pUI && s_bRememberUILists)
	{
		UIPersistedList *pStored = eaIndexedGetUsingString(&pUI->eaStoredLists, pchName);
		if (pStored && pStored->iVersion == iVersion && iVersion > 0)
		{
			S32 i;
			for(i = 0; i < eaSize(&pStored->eaColumns); i++)
			{
				UIColumn *uiCol = StructCreate(parse_UIColumn);
				uiCol->pchColName = allocAddString(pStored->eaColumns[i]->pchColName);
				uiCol->fPercentWidth = pStored->eaColumns[i]->fPercentWidth;
				eaPush(peaCols, uiCol);
			}
		}
	}
}

void gclui_GenSetListOrder(const char *pchName, UIColumn ***peaCols, S32 iVersion)
{
	if (iVersion > 0 && s_bRememberUILists)
	{
		UIPersistedList *pStored = StructCreate(parse_UIPersistedList);
		S32 i;

		pStored->pchName = StructAllocString(pchName);

		for(i = 0; i < eaSize(peaCols); i++)
		{
			UIPersistedColumn *pCol = StructCreate(parse_UIPersistedColumn);
			pCol->fPercentWidth = (*peaCols)[i]->fPercentWidth;
			pCol->pchColName = StructAllocString((*peaCols)[i]->pchColName);
			eaPush(&pStored->eaColumns, pCol);
		}
		ServerCmd_gslCmdUpdateStoredUIList(pStored);

		StructDestroy(parse_UIPersistedList, pStored);
	}
}

static const char *gclui_GenGetValue(const char *pchKey)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		PlayerUIPair *pPair = eaIndexedGetUsingString(&pEnt->pPlayer->pUI->eaPairs, pchKey);
		if (pPair)
			return pPair->pchValue;
	}
	return NULL;
}

static bool gclui_GenSetValue(const char *pchKey, const char *pchValue)
{
	if (entActivePlayerPtr())
	{
		ServerCmd_gslSetPlayerUIValueString(pchKey, pchValue);
		return true;
	}
	return false;
}

static bool s_bRememberWindows = true;
AUTO_CMD_INT(s_bRememberWindows, RememberWindows) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC;

static bool gclui_GenOpenWindowsGetNames(const char ***ppchNames)
{
	Entity *pEnt = entActivePlayerPtr();
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	if (pUI && s_bRememberWindows)
	{
		S32 i;
		for (i = 0; i < eaSize(&pUI->pLooseUI->eaPersistedWindows); i++)
		{
			UIPersistedWindow *pPersistedWindow = eaGet(&pUI->pLooseUI->eaPersistedWindows, i);
			if (pPersistedWindow)
			{
				eaPush(ppchNames, pPersistedWindow->pchName);
			}
		}
		return true;
	}
	return false;
}

static ContainerID s_iPlayerID;
static UIPersistedWindow **s_eaPredictedPersistedWindows;

static bool gclui_GenOpenWindowsGet(const char *pchName, U32 uMaxOpenWindows, U32 *pOpenWindows)
{
	Entity *pEnt = entActivePlayerPtr();
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	if (!pEnt || s_iPlayerID != entGetContainerID(pEnt))
	{
		eaClearStruct(&s_eaPredictedPersistedWindows, parse_UIPersistedWindow);
		s_iPlayerID = pEnt ? entGetContainerID(pEnt) : 0;
	}
	if (pUI && s_bRememberWindows)
	{
		UIPersistedWindow *pPredictedWindow = eaIndexedGetUsingString(&s_eaPredictedPersistedWindows, pchName);
		UIPersistedWindow *pPersistedWindow = eaIndexedGetUsingString(&pUI->pLooseUI->eaPersistedWindows, pchName);

		if (pPredictedWindow && pPredictedWindow->uiTime == pPersistedWindow->uiTime)
		{
			// If the predicted time is the same as the persisted time, then the predicted data
			// is fresher than the server data.
			pPersistedWindow = pPredictedWindow;
		}

		memset(pOpenWindows, 0, sizeof(U32) * (uMaxOpenWindows + 31) / 32);

		if (pPersistedWindow)
		{
			MIN1(uMaxOpenWindows, sizeof(pPersistedWindow->bfWindows) * 8);
			memcpy(pOpenWindows, pPersistedWindow->bfWindows, sizeof(U32) * (uMaxOpenWindows + 31) / 32);
		}

		return true;
	}
	return false;
}

static bool gclui_GenOpenWindowsSet(const char *pchName, U32 uMaxOpenWindows, U32 *pOpenWindows)
{
	Entity *pEnt = entActivePlayerPtr();
	Player *pPlayer = pEnt ? pEnt->pPlayer : NULL;
	PlayerUI *pUI = pPlayer ? pPlayer->pUI : NULL;
	if (!pEnt || s_iPlayerID != entGetContainerID(pEnt))
	{
		eaClearStruct(&s_eaPredictedPersistedWindows, parse_UIPersistedWindow);
		s_iPlayerID = pEnt ? entGetContainerID(pEnt) : 0;
	}
	if (entActivePlayerPtr())
	{
		UIPersistedWindow *pPredictedWindow = eaIndexedGetUsingString(&s_eaPredictedPersistedWindows, pchName);
		UIPersistedWindow *pPersistedWindow = eaIndexedGetUsingString(&pUI->pLooseUI->eaPersistedWindows, pchName);

		if (!pPredictedWindow)
		{
			if (pPersistedWindow)
			{
				pPredictedWindow = StructClone(parse_UIPersistedWindow, pPersistedWindow);
			}
			else
			{
				pPredictedWindow = StructCreate(parse_UIPersistedWindow);
				pPredictedWindow->pchName = StructAllocString(pchName);
			}
			if (!s_eaPredictedPersistedWindows)
				eaIndexedEnable(&s_eaPredictedPersistedWindows, parse_UIPersistedWindow);
			eaPush(&s_eaPredictedPersistedWindows, pPredictedWindow);
		}

		pPredictedWindow->uiTime = SAFE_MEMBER(pPersistedWindow, uiTime);
		memset(pPredictedWindow->bfWindows, 0, sizeof(pPredictedWindow->bfWindows));
		MIN1(uMaxOpenWindows, sizeof(pPredictedWindow->bfWindows) * 8);
		memcpy(pPredictedWindow->bfWindows, pOpenWindows, (uMaxOpenWindows + 7) / 8);

		ServerCmd_gslSetPlayerPersistedWindow(pPredictedWindow);
	}
	return false;
}

AUTO_RUN;
void gclui_GenRegister(void)
{
	ui_GenRegisterType(kUIGenTypeEntity, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateEntity, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		UI_GEN_NO_TICKEARLY, 
		UI_GEN_NO_TICKLATE, 
		UI_GEN_NO_DRAWEARLY,
		UI_GEN_NO_FITCONTENTSSIZE, 
		UI_GEN_NO_FITPARENTSIZE, 
		UI_GEN_NO_HIDE, 
		UI_GEN_NO_INPUT, 
		ui_GenUpdateContextEntity, 
		UI_GEN_NO_QUEUERESET);
	s_pchGenEntityString = allocAddStaticString("Entity");
	ui_GenInitPointerVar(s_pchGenEntityString, parse_Entity);

	ui_GenRegisterType(kUIGenTypeObject, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateObject, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		UI_GEN_NO_TICKEARLY, 
		UI_GEN_NO_TICKLATE, 
		UI_GEN_NO_DRAWEARLY,
		UI_GEN_NO_FITCONTENTSSIZE, 
		UI_GEN_NO_FITPARENTSIZE, 
		UI_GEN_NO_HIDE, 
		UI_GEN_NO_INPUT, 
		ui_GenUpdateContextObject, 
		UI_GEN_NO_QUEUERESET);
	s_pchGenObjectString = allocAddStaticString("Object");
	ui_GenInitPointerVar(s_pchGenObjectString, parse_WorldInteractionNode);

	ui_GenRegisterType(kUIGenTypeWaypoint, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		UI_GEN_NO_UPDATE, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		UI_GEN_NO_TICKEARLY, 
		UI_GEN_NO_TICKLATE, 
		UI_GEN_NO_DRAWEARLY,
		UI_GEN_NO_FITCONTENTSSIZE, 
		UI_GEN_NO_FITPARENTSIZE, 
		UI_GEN_NO_HIDE, 
		UI_GEN_NO_INPUT, 
		ui_GenUpdateContextWaypoint, 
		UI_GEN_NO_QUEUERESET);
	s_pchGenWaypointString = allocAddStaticString("Waypoint");
	ui_GenInitPointerVar(s_pchGenWaypointString, parse_MinimapWaypoint);

	ui_GenPositionRegisterCallbacks(gclui_GenGetPosition, gclui_GenSetPosition, gclui_GenForgetPosition);
	ui_GenListOrderRegisterCallbacks(gclui_GenGetListOrder, gclui_GenSetListOrder);
	s_pchGenObjEntityString = allocAddStaticString("ObjEntity");
	ui_GenInitPointerVar(s_pchGenObjEntityString, parse_Entity);

	ui_GenSetPersistedValueCallbacks(gclui_GenGetValue, gclui_GenSetValue);
	ui_GenSetOpenWindowsCallbacks(gclui_GenOpenWindowsGetNames, gclui_GenOpenWindowsGet, gclui_GenOpenWindowsSet);

	ui_GenSetSMFNavigateCallback(gclui_SMFNavigate);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetTarget");
void gclui_GenExprSetTarget(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (!pEnt) return;
	entity_SetTarget(entActivePlayerPtr(), entGetRef(pEnt));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetTargetByID");
void gclui_GenExprSetTargetByID(U32 eEntType, U32 iEntID)
{
	Entity *pEnt = entFromContainerIDAnyPartition(eEntType, iEntID);
	if (pEnt) {
		gclui_GenExprSetTarget(pEnt);
	}
}

static void gclGenWorldLoadGridCallback(void *userData)
{
	mapState_ClientResetVisibleChildAllNextFrame();
	CostumeUI_UpdateWorldRegion(false);
}

// Load a world grid by name, returns true if it succeeds.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("WorldLoadGrid");
int gclGenWorldLoadGrid(const char *pchName)
{
	CostumeUI_UpdateWorldRegion(true);
	return worldLoadZoneMapByNameAsync(pchName, gclGenWorldLoadGridCallback, NULL);
// 	int ret;
// 	ret = worldLoadZoneMapByName(pchName);
// 	gclGenWorldLoadGridCallback(NULL);
// 	return ret;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SetLoginCamPosPyr");
void gclGenSetLoginCamPosPyr(F32 pos_x, F32 pos_y, F32 pos_z, F32 pitch, F32 yaw, F32 rotation)
{
	GfxCameraController *camera = costumeCameraUI_GetCamera();
	setVec3(camera->camcenter, pos_x, pos_y, pos_z);
	setVec3(camera->campyr, RAD(pitch), RAD(yaw), RAD(rotation));
	camera->camdist = 10.0;
	camera->inited = true;

	camera = gclLoginGetCameraController();
	setVec3(camera->camcenter, pos_x, pos_y, pos_z);
	setVec3(camera->campyr, RAD(pitch), RAD(yaw), RAD(rotation));
	camera->camdist = 10.0;
	camera->inited = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetLoginCamPosDistance");
F32 gclGenGetLoginCamPosDistance(F32 pos_x, F32 pos_y, F32 pos_z)
{
	F32 fDistance, f;
	GfxCameraController *camera = gclLoginGetCameraController();
	Mat4 xCameraMatrix;
	Vec3 vec;

	createMat3YPR(xCameraMatrix, camera->campyr);

	copyVec3(camera->centeroffset, vec);
	vec[2] += camera->camdist;
	mulVecMat3(vec, xCameraMatrix, xCameraMatrix[3]);
	addVec3(xCameraMatrix[3], camera->camcenter, xCameraMatrix[3]);

	f = xCameraMatrix[3][0] - pos_x;
	fDistance = SQR(f);
	f = xCameraMatrix[3][1] - pos_y;
	fDistance += SQR(f);
	f = xCameraMatrix[3][2] - pos_z;
	fDistance += SQR(f);

	return sqrtf(fDistance);
}

// Return true if an entity has an interaction or pickup prompt.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntHasInteractionPrompt");
bool gclGenEntHasInteractionPrompt(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer && (pEnt->pPlayer->InteractStatus.promptInteraction || pEnt->pPlayer->InteractStatus.promptPickup))
		return true;
	else
		return false;
}

/*---------Saved Pet UI Commands-------------*/

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetGetAllPets");
bool gclSavedPetGetAllPets(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, ACMD_EXPR_ENUM(WorldRegionType) const char *pchRegionType, bool bExcludePuppets)
{	
	int i;
	Entity *pEntPlayer = entActivePlayerPtr();
	static PetRelationship **s_eaRelationshipList;
	WorldRegionType eRegionType = StaticDefineIntGetInt(WorldRegionTypeEnum,pchRegionType);
	RegionRules *pRegionRules = getRegionRulesFromRegionType(eRegionType);

	eaClear(&s_eaRelationshipList);

	if(!pEntPlayer || !pEntPlayer->pSaved)
		return false;

	for(i=0;i<eaSize(&pEntPlayer->pSaved->ppOwnedContainers);i++)
	{	
		Entity *pEntity = SavedPet_GetEntity(PARTITION_CLIENT, pEntPlayer->pSaved->ppOwnedContainers[i]);

		if(bExcludePuppets && SavedPet_IsPetAPuppet(pEntPlayer,pEntPlayer->pSaved->ppOwnedContainers[i]))
			continue;

		if(pEntity)
		{
			if(pRegionRules)
			{
				CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);

				if(!pClass || ea32Find(&pRegionRules->ePetType,pClass->eType) == -1)
					continue;
			}

			eaPush(&s_eaRelationshipList,pEntPlayer->pSaved->ppOwnedContainers[i]);
		}
	}

	ui_GenSetManagedListSafe(pGen, &s_eaRelationshipList, PetRelationship, false);
	return true;
}

static int SortAlwaysPropSlots(const AlwaysPropSlotData** ppA, const AlwaysPropSlotData** ppB)
{
	const AlwaysPropSlotData* pA = (*ppA);
	const AlwaysPropSlotData* pB = (*ppB);
	AlwaysPropSlotClassRestrictDef* pRestrictDefA = pA->pRestrictDef;
	AlwaysPropSlotClassRestrictDef* pRestrictDefB = pB->pRestrictDef;
	AlwaysPropSlotDef* pDefA = GET_REF(pA->hDef);
	AlwaysPropSlotDef* pDefB = GET_REF(pB->hDef);

	if (!pRestrictDefA || !pDefA)
		return 1;
	if (!pRestrictDefB || !pDefB)
		return -1;

	if (pRestrictDefA->eClassRestrictType != pRestrictDefB->eClassRestrictType)
		return pRestrictDefA->eClassRestrictType - pRestrictDefB->eClassRestrictType;

	if (pDefA->iMaxPropPowers != pDefB->iMaxPropPowers)
		return pDefB->iMaxPropPowers - pDefA->iMaxPropPowers;

	return (int)(pA->iSlotID - pB->iSlotID);
}

static int ReverseSortAlwaysPropSlots(const AlwaysPropSlotData** ppA, const AlwaysPropSlotData** ppB)
{
	return SortAlwaysPropSlots(ppA, ppB) * -1;
}

bool gclSavedPetGetAllPowerPropSlotsEx(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, Entity *pEntPlayer, S32 ePropCategory, bool bReverseSort, U32 uiPuppetID)
{
	AlwaysPropSlotData*** peaData = ui_GenGetManagedListSafe(pGen, AlwaysPropSlotData);
	int i, j, k, iCount = 0;
	if (pEntPlayer)
	{
		const char* pchName;

		// Add prop slots from the entity's saved prop slots
		if (pEntPlayer->pSaved)
		{
			for(i=0;i<eaSize(&pEntPlayer->pSaved->ppAlwaysPropSlots);i++)
			{
				AlwaysPropSlot* pPropSlot = pEntPlayer->pSaved->ppAlwaysPropSlots[i];
				AlwaysPropSlotDef* pPropSlotDef = GET_REF(pPropSlot->hDef);
				if (uiPuppetID && pPropSlot->iPuppetID != uiPuppetID)
					continue;
				if (ePropCategory < 0 
					|| (pPropSlotDef 
						&& pPropSlotDef->eCategory == ePropCategory))
				{
					Entity* pEnt = SavedPet_GetEntityFromPetID(pEntPlayer, pPropSlot->iPetID);
					AlwaysPropSlotData* pData = NULL;

					for (j=iCount;j<eaSize(peaData);j++) {
						if ((*peaData)[j]->uiPuppetID == pPropSlot->iPuppetID && (*peaData)[j]->iSlotID == (S32)pPropSlot->iSlotID) {
							eaMove(peaData, iCount, j);
							pData = (*peaData)[iCount++];
							break;
						}
					}

					if (!pData) {
						pData = StructCreate(parse_AlwaysPropSlotData);
						eaInsert(peaData, pData, iCount++);
					}

					COPY_HANDLE(pData->hDef, pPropSlot->hDef);
					pData->eRestrictType = pPropSlotDef->eClassRestrictType;

					pData->uiPuppetID = pPropSlot->iPuppetID;
					pData->iSlotID = pPropSlot->iSlotID;

					pData->uiPetID = pPropSlot->iPetID;
					pData->iID = SAFE_MEMBER(pEnt, myContainerID);

					pchName = pEnt ? entGetLocalName(pEnt) : NULL;
					pchName = EMPTY_TO_NULL(pchName);
					if (strcmp_safe(pData->pchName, pchName)) {
						StructCopyString(&pData->pchName, pchName);
					}
				}
			}
		}

		// Add prop slots from the entity's inventory
		if (pEntPlayer->pInventoryV2)
		{
			char *estrTemp = NULL;
			const char *pchKey;
			UIInventoryKey Key = {0};

			for (i=0;i<eaSize(&pEntPlayer->pInventoryV2->ppInventoryBags);i++)
			{
				InventoryBag *pBag = pEntPlayer->pInventoryV2->ppInventoryBags[i];
				const InvBagDef *pBagDef = invbag_def(pBag);
				if (pBagDef && pBagDef->bFakePropSlots)
				{
					for (j=0;j<eaSize(&pBag->ppIndexedInventorySlots);j++)
					{
						Item *pItem = pBag->ppIndexedInventorySlots[j]->pItem;
						ItemDef *pItemDef = SAFE_GET_REF(pItem, hItem);
						AlwaysPropSlotData* pData = NULL;

						if (!pItemDef)
							continue;

						gclInventoryMakeSlotKey(pEntPlayer, pBag, pBag->ppIndexedInventorySlots[j], &Key);
						pchKey = gclInventoryMakeKeyString(pContext, &Key);

						for (k=iCount;k<eaSize(peaData);k++) {
							if (!stricmp_safe((*peaData)[k]->pchPropItemKey, pchKey)) {
								eaMove(peaData, iCount, k);
								pData = (*peaData)[iCount++];
								break;
							}
						}
						if (!pData) {
							pData = StructCreate(parse_AlwaysPropSlotData);
							eaInsert(peaData, pData, iCount++);
						}

						// TODO: pData->eRestrictType = pItemDef->eClassRestrictType;

						pData->pPropItem = pItem;
						COPY_HANDLE(pData->hPropItemDef, pItem->hItem);

						pchKey = EMPTY_TO_NULL(pchKey);
						if (stricmp_safe(pData->pchPropItemKey, pchKey)) {
							StructCopyString(&pData->pchPropItemKey, pchKey);
						}

						pchName = item_GetName(pItem, pEntPlayer);
						pchName = EMPTY_TO_NULL(pchName);
						if (strcmp_safe(pData->pchName, pchName)) {
							StructCopyString(&pData->pchName, pchName);
						}
					}
				}
			}

			for (i=0;i<eaSize(&pEntPlayer->pInventoryV2->ppLiteBags);i++)
			{
				InventoryBagLite *pBag = pEntPlayer->pInventoryV2->ppLiteBags[i];
				const InvBagDef *pBagDef = invbaglite_def(pBag);
				if (pBagDef && pBagDef->bFakePropSlots)
				{
					for (j=0;j<eaSize(&pBag->ppIndexedLiteSlots);j++)
					{
						ItemDef *pItemDef = GET_REF(pBag->ppIndexedLiteSlots[j]->hItemDef);
						AlwaysPropSlotData* pData = NULL;

						if (!pItemDef)
							continue;

						gclInventoryMakeSlotLiteKey(pEntPlayer, pBag, pBag->ppIndexedLiteSlots[j], &Key);
						pchKey = gclInventoryMakeKeyString(pContext, &Key);

						for (k=iCount;k<eaSize(peaData);k++) {
							if (!stricmp_safe((*peaData)[k]->pchPropItemKey, pchKey)) {
								eaMove(peaData, iCount, k);
								pData = (*peaData)[iCount++];
								break;
							}
						}
						if (!pData) {
							pData = StructCreate(parse_AlwaysPropSlotData);
							eaInsert(peaData, pData, iCount++);
						}

						// TODO: pData->eRestrictType = pItemDef->eClassRestrictType;

						COPY_HANDLE(pData->hPropItemDef, pBag->ppIndexedLiteSlots[j]->hItemDef);

						pchKey = EMPTY_TO_NULL(pchKey);
						if (stricmp_safe(pData->pchPropItemKey, pchKey)) {
							StructCopyString(&pData->pchPropItemKey, pchKey);
						}

						if (!estrTemp)
							estrStackCreate(&estrTemp);
						pchName = itemdef_GetName(&estrTemp, pItemDef, pEntPlayer);
						pchName = EMPTY_TO_NULL(pchName);
						if (strcmp_safe(pData->pchName, pchName)) {
							StructCopyString(&pData->pchName, pchName);
						}
					}
				}
			}

			if (estrTemp)
				estrDestroy(&estrTemp);
		}

		// Post update fixup
		for (i=0;i<iCount;i++)
		{
			AlwaysPropSlotData* pData = (*peaData)[i];

			// Get the AlwaysPropSlotClassRestrictDef
			pData->pRestrictDef = AlwaysPropSlot_GetClassRestrictDef(pData->eRestrictType);

			// Get the AlwaysPropSlotClassRestrictDef display name
			if (pData->pRestrictDef)
				pData->pchClassDisplayName = TranslateDisplayMessage(pData->pRestrictDef->msgDisplayName);
			else
				pData->pchClassDisplayName = NULL;
		}
	}
	eaSetSizeStruct(peaData, parse_AlwaysPropSlotData, iCount);
	eaQSort(*peaData, bReverseSort ? ReverseSortAlwaysPropSlots : SortAlwaysPropSlots);
	ui_GenSetManagedListSafe(pGen, peaData, AlwaysPropSlotData, true);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetGetPowerPropSlotsForActivePuppet");
bool gclSavedPetGetPowerPropSlotsForActivePuppet(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, S32 ePropCategory, bool bReverseSort)
{
	Entity *pEntPlayer = entActivePlayerPtr();
	return gclSavedPetGetAllPowerPropSlotsEx(pContext, pGen, pEntPlayer, ePropCategory, bReverseSort, SAFE_MEMBER3(pEntPlayer, pSaved, pPuppetMaster, curID));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetGetAllPowerPropSlots");
bool gclSavedPetGetAllPowerPropSlots(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, S32 ePropCategory, bool bReverseSort)
{
	Entity *pEntPlayer = entActivePlayerPtr();
	return gclSavedPetGetAllPowerPropSlotsEx(pContext, pGen, pEntPlayer, ePropCategory, bReverseSort, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PropSlotCanSlotType");
bool gclPropSlotCanSlotType(ExprContext *pContext, SA_PARAM_OP_VALID AlwaysPropSlotData* pSlot, const char* pchClassTypes)
{
	AlwaysPropSlotDef* pSlotDef = pSlot ? GET_REF(pSlot->hDef) : NULL;
	AlwaysPropSlotClassRestrictDef* pRestrictDef = pSlotDef ? AlwaysPropSlot_GetClassRestrictDef(pSlotDef->eClassRestrictType) : NULL;

	if (!pchClassTypes || !pchClassTypes[0])
		return false;

	if (pRestrictDef)
	{
		const char* pchClassType;
		char* context = NULL;
		char temp[1024];
		strcpy( temp, pchClassTypes );
		pchClassType = strtok_s(temp, ",", &context);
		while (pchClassType)
		{
			S32 iType = StaticDefineIntGetInt(CharClassTypesEnum, pchClassType);
			
			if (iType < 0 || eaiFind(&pRestrictDef->peClassTypes, iType) == -1)
			{
				return false;
			}

			pchClassType = strtok_s(NULL, ",", &context);
		}

		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PropSlotGetMinPropPowers);
S32 gclPropSlotGetPowerMin(ExprContext *pContext, SA_PARAM_OP_VALID AlwaysPropSlotData* pSlot)
{
	return pSlot && GET_REF(pSlot->hDef) ? GET_REF(pSlot->hDef)->iMinPropPowers : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PropSlotGetMaxPropPowers);
S32 gclPropSlotGetPowerMax(ExprContext *pContext, SA_PARAM_OP_VALID AlwaysPropSlotData* pSlot)
{
	return pSlot && GET_REF(pSlot->hDef) ? GET_REF(pSlot->hDef)->iMaxPropPowers : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PropSlotGetCandidateList", "PropSlotGetCanSlotList");
bool gclPropSlotGetCanSlotList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEntPlayer, U32 iSlotID)
{
	AlwaysPropSlotCandidate*** peaCandidates = ui_GenGetManagedListSafe(pGen, AlwaysPropSlotCandidate);
	AlwaysPropSlot* pSlot = NULL;
	AlwaysPropSlotDef* pSlotDef;
	int i, iCount = 0;

	if(!pEntPlayer || !pEntPlayer->pSaved)
		return false;

	for(i=eaSize(&pEntPlayer->pSaved->ppAlwaysPropSlots)-1;i>=0;--i)
	{
		pSlot = pEntPlayer->pSaved->ppAlwaysPropSlots[i];
		if (pSlot->iSlotID == iSlotID) 
		{
			break;
		}
	}
	if (i >= 0) 
	{
		// Create an empty slot
		AlwaysPropSlotCandidate* pCandidate = eaGetStruct(peaCandidates, parse_AlwaysPropSlotCandidate, iCount++);
		pCandidate->iID = 0;
		pCandidate->uiPetID = 0;
		StructFreeStringSafe(&pCandidate->pchName);
		pCandidate->bIsEmpty = true;

		// Get the list of candidates
		if (pSlotDef = GET_REF(pSlot->hDef))
		{
			for (i = 0; i < eaSize(&pEntPlayer->pSaved->ppOwnedContainers); i++)
			{
				PetRelationship* pPet = pEntPlayer->pSaved->ppOwnedContainers[i];
				Entity* pPetEnt = GET_REF(pPet->hPetRef);
				
				if (!pPetEnt || eaSize(&pPetEnt->pChar->ppTraining) || SavedPet_IsPetAPuppet(pEntPlayer, pPet))
					continue;

				if (SavedPet_AlwaysPropSlotCheckRestrictions(pPetEnt, pPet, pSlotDef))
				{
					pCandidate = eaGetStruct(peaCandidates, parse_AlwaysPropSlotCandidate, iCount++);
					pCandidate->iID = pPet->conID;
					pCandidate->uiPetID = pPet->uiPetID;
					StructCopyString(&pCandidate->pchName, entGetLocalName(pPetEnt));
					pCandidate->bIsEmpty = false;
				}
			}
		}
	}
	eaSetSizeStruct(peaCandidates, parse_AlwaysPropSlotCandidate, iCount);
	ui_GenSetManagedListSafe(pGen, peaCandidates, AlwaysPropSlotCandidate, true);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetGetAllPuppets");
bool gclSavedPetGetAllPuppets(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, ACMD_EXPR_ENUM(WorldRegionType) const char *pchRegionType)
{	
	int i;
	Entity *pEntPlayer = entActivePlayerPtr();
	static PuppetEntity **s_eaPuppetList;
	WorldRegionType eRegionType = StaticDefineIntGetInt(WorldRegionTypeEnum,pchRegionType);
	RegionRules *pRegionRules = getRegionRulesFromRegionType(eRegionType);

	eaClear(&s_eaPuppetList);

	if(!pEntPlayer || !pEntPlayer->pSaved || !pEntPlayer->pSaved->pPuppetMaster)
		return false;

	for(i=0;i<eaSize(&pEntPlayer->pSaved->pPuppetMaster->ppPuppets);i++)
	{	
		Entity *pEntity = SavedPuppet_GetEntity(PARTITION_CLIENT, pEntPlayer->pSaved->pPuppetMaster->ppPuppets[i]);

		if(pEntity)
		{
			if(pRegionRules)
			{
				CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);

				if(pClass && ea32Find(&pRegionRules->peCharClassTypes, pClass->eType) < 0)
					continue;
			}

			eaPush(&s_eaPuppetList,pEntPlayer->pSaved->pPuppetMaster->ppPuppets[i]);
		}
	}

	ui_GenSetManagedListSafe(pGen, &s_eaPuppetList, PuppetEntity, false);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetGetAllPetsWithTeamRequest");
bool gclSavedPetGetPetsWithTeamRequest(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, bool bExcludePuppets)
{
	static PetRelationship **s_eaRelationshipList;
	Entity *pEntPlayer = entActivePlayerPtr();
	int i;

	eaClear(&s_eaRelationshipList);

	if(!pEntPlayer || !pEntPlayer->pSaved)
		return false;

	for(i=0;i<eaSize(&pEntPlayer->pSaved->ppOwnedContainers);i++)
	{
		Entity *pEntity = SavedPet_GetEntity(PARTITION_CLIENT, pEntPlayer->pSaved->ppOwnedContainers[i]);

		if(bExcludePuppets && SavedPet_IsPetAPuppet(pEntPlayer,pEntPlayer->pSaved->ppOwnedContainers[i]))
			continue;

		if(pEntPlayer->pSaved->ppOwnedContainers[i]->bTeamRequest)
			eaPush(&s_eaRelationshipList,pEntPlayer->pSaved->ppOwnedContainers[i]);
	}

	ui_GenSetManagedListSafe(pGen, &s_eaRelationshipList, PetRelationship, false);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetIDAddTeamRequest");
bool gclSavedPetIDAddTeamRequest(ExprContext *pContext, U32 uiPetID, int iSlotID, int ePropCategory)
{
	int i;
	Entity *pPlayerEnt = entActivePlayerPtr();

	if(!pPlayerEnt || !pPlayerEnt->pSaved)
		return false;

	for(i=0;i<eaSize(&pPlayerEnt->pSaved->ppOwnedContainers);i++)
	{
		if(pPlayerEnt->pSaved->ppOwnedContainers[i]->uiPetID == uiPetID)
		{
			if(!pPlayerEnt->pSaved->ppOwnedContainers[i]->bTeamRequest)
			{
				ServerCmd_SavedPet_StatusFlagRequestTeamRequest(uiPetID,iSlotID,ePropCategory,true);
			}
			else
			{
				ServerCmd_SavedPet_StatusFlagSwapRequest(0,uiPetID,iSlotID,uiPetID,false);
			}
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetIDAddAlwaysProp");
bool gclSavedPetIDAddAlwaysProp(ExprContext *pContext, U32 uiPetID, U32 uiPuppetID, int iSlotID, int ePropCategory)
{
	int i;
	Entity *pPlayerEnt = entActivePlayerPtr();

	if(!pPlayerEnt || !pPlayerEnt->pSaved)
		return false;

	for (i = eaSize(&pPlayerEnt->pSaved->ppAlwaysPropSlots)-1; i >= 0; i--)
	{
		AlwaysPropSlot* pPropSlot = pPlayerEnt->pSaved->ppAlwaysPropSlots[i];
		AlwaysPropSlotDef* pPropSlotDef = GET_REF(pPropSlot->hDef);
		if (pPropSlot->iPetID == uiPetID && 
			(ePropCategory < 0 || (pPropSlotDef && pPropSlotDef->eCategory == ePropCategory)))
		{
			ServerCmd_SavedPet_StatusFlagSwapRequest(0,uiPetID,iSlotID,uiPuppetID,false);
			return true;
		}
	}
	if (i < 0)
	{
		ServerCmd_SavedPet_StatusFlagRequestAlwaysPropSlot(uiPetID,uiPuppetID,iSlotID,ePropCategory,true);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetAddAlwaysProp");
bool gclSavedPetAddAlwaysProp(ExprContext *pContext, const char *pchSavedPetID, U32 uiPuppetID, int iSlotID, int ePropCategory)
{
	U32 uiPetID = StringToContainerID(pchSavedPetID);

	return gclSavedPetIDAddAlwaysProp(pContext, uiPetID, uiPuppetID, iSlotID, ePropCategory);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetAddTeamRequest");
bool gclSavedPetAddTeamRequest(ExprContext *pContext, const char *pchSavedPetID, 
						int iSlotID, int ePropCategory)
{
	U32 uiPetID = StringToContainerID(pchSavedPetID);

	return gclSavedPetIDAddTeamRequest(pContext, uiPetID, iSlotID, ePropCategory);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetIDRemoveAlwaysProp");
bool gclSavedPetIDRemoveAlwaysProp(ExprContext *pContext, U32 uiPetID, U32 uiPuppetID, int ePropCategory)
{
	int i;
	Entity *pPlayerEnt = entActivePlayerPtr();

	if(!pPlayerEnt || !pPlayerEnt->pSaved)
		return false;

	for (i = eaSize(&pPlayerEnt->pSaved->ppAlwaysPropSlots)-1; i >= 0; i--)
	{
		AlwaysPropSlot* pPropSlot = pPlayerEnt->pSaved->ppAlwaysPropSlots[i];
		AlwaysPropSlotDef* pPropSlotDef = GET_REF(pPropSlot->hDef);
		if (pPropSlot->iPetID == uiPetID && 
			(ePropCategory < 0 || (pPropSlotDef && pPropSlotDef->eCategory == ePropCategory)))
		{
			ServerCmd_SavedPet_StatusFlagRequestAlwaysPropSlot(uiPetID, uiPuppetID, -1, ePropCategory, false);
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetIDRemoveTeamRequest");
bool gclSavedPetIDRemoveTeamRequest(ExprContext *pContext, U32 uiPetID, int ePropCategory)
{
	int i;
	Entity *pPlayerEnt = entActivePlayerPtr();

	if(!pPlayerEnt || !pPlayerEnt->pSaved)
		return false;

	for(i=0;i<eaSize(&pPlayerEnt->pSaved->ppOwnedContainers);i++)
	{
		if(pPlayerEnt->pSaved->ppOwnedContainers[i]->uiPetID == uiPetID)
		{
			if(pPlayerEnt->pSaved->ppOwnedContainers[i]->bTeamRequest)
			{
				ServerCmd_SavedPet_StatusFlagRequestTeamRequest(uiPetID,-1,ePropCategory,false);
			}
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetIDReplaceAlwaysProp");
bool gclSavedPetIDReplaceAlwaysProp(ExprContext *pContext, U32 uiOldPetID, U32 uiNewPetID, U32 uiPuppetID, int iSlotID)
{
	int i;
	Entity *pPlayerEnt = entActivePlayerPtr();
	bool bFoundOld = false;
	bool bFoundNew = false;


	if(!pPlayerEnt || !pPlayerEnt->pSaved)
		return false;

	if (uiOldPetID == uiNewPetID) 
		return true;

	for(i=eaSize(&pPlayerEnt->pSaved->ppOwnedContainers)-1;i>=0;i--)
	{
		if(pPlayerEnt->pSaved->ppOwnedContainers[i]->uiPetID == uiOldPetID)
			bFoundOld = true;
		if(pPlayerEnt->pSaved->ppOwnedContainers[i]->uiPetID == uiNewPetID)
			bFoundNew = true;
	}

	if (!bFoundOld || !bFoundNew)
		return false;

	ServerCmd_SavedPet_StatusFlagSwapRequest(uiOldPetID,uiNewPetID,iSlotID,uiPuppetID,false);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetIDCheckTeamRequest");
bool gclSavedPetIDCheckTeamRequest(ExprContext *pContext, U32 uiPetID)
{
	int i;
	Entity *pPlayerEnt = entActivePlayerPtr();

	if(!pPlayerEnt || !pPlayerEnt->pSaved)
		return false;

	for(i=0;i<eaSize(&pPlayerEnt->pSaved->ppOwnedContainers);i++)
	{
		if(pPlayerEnt->pSaved->ppOwnedContainers[i]->uiPetID == uiPetID)
		{
			return pPlayerEnt->pSaved->ppOwnedContainers[i]->bTeamRequest;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SavedPetIDCheckAlwaysProp");
bool gclSavedPetIDCheckAlwaysProp(ExprContext *pContext, U32 uiPetID, U32 uiPuppetID)
{
	int i;
	Entity *pPlayerEnt = entActivePlayerPtr();

	if(!pPlayerEnt || !pPlayerEnt->pSaved)
		return false;

	for(i=0;i<eaSize(&pPlayerEnt->pSaved->ppAlwaysPropSlots);i++)
	{
		if(pPlayerEnt->pSaved->ppAlwaysPropSlots[i]->iPuppetID == uiPuppetID && pPlayerEnt->pSaved->ppAlwaysPropSlots[i]->iPetID == uiPetID)
		{
			return true;
		}
	}

	return false;
}

void CreateScreenBoxFromScreenPosition(CBox *pBox, CBox *pScreen, Vec2 vScreenPos, F32 width, F32 height)
{
	pBox->right = max((pScreen->right)*vScreenPos[0], width); // converting it to screen coordinates, preserving the height and width
	pBox->left = pBox->right - width;

	pBox->bottom = max((pScreen->bottom)*vScreenPos[1], height);
	pBox->top = pBox->bottom - height;
}

bool ProjectPointOnScreen(Vec3 vPos, GfxCameraView *pView, CBox *pBox, CBox *pScreen, Vec2 vScreenPos)
{
	bool bOnScreen = true;
	Vec3 vViewPos, vProjPos;

	// Change vPos from world space to screen space
	mulVecMat4(vPos, pView->frustum.viewmat, vViewPos);				
	mulVec3ProjMat44(vViewPos, pView->projection_matrix, vProjPos);
	
	// Glitch occurs around very large values of Z
	if (vProjPos[2] >= 1.0f)		
	{
		// Special case where it needs to be reversed
		vProjPos[0] *= -1.0f;	
		vProjPos[1] *= -1.0f;
	}

	// It's offscreen, or behind you, so we should clamp it.
	if (vProjPos[0] < -1.0f || vProjPos[0] > 1.0f || vProjPos[1] < -1.0f || vProjPos[1] > 1.0f || vViewPos[2] > 0.0f)
	{
		// Project the position to the edge of the screen
		if (fabs(vProjPos[0]) > fabs(vProjPos[1])) 
		{
			if (vProjPos[0])
			{
				vProjPos[1] /= fabs(vProjPos[0]);
				vProjPos[0] /= fabs(vProjPos[0]);
			}
		}
		else
		{	
			if (vProjPos[1])
			{
				vProjPos[0] /= fabs(vProjPos[1]);
				vProjPos[1] /= fabs(vProjPos[1]);
			}
		}
		bOnScreen = false;
	}

	// Transform to screen-space
	vScreenPos[0] = (vProjPos[0]+1.0f)/2.0f; 
	vScreenPos[1] = (vProjPos[1]+1.0f)/2.0f; 
	vScreenPos[1] = 1.0f - vScreenPos[1];
	return bOnScreen;
}

bool ProjectCBoxOnScreen(Vec3 vPos, GfxCameraView *pView, CBox *pBox, CBox *pScreen, F32 width, F32 height)
{
	Vec2 vScreenPos;
	bool bOnScreen = ProjectPointOnScreen(vPos, pView, pBox, pScreen, vScreenPos);
	CreateScreenBoxFromScreenPosition(pBox, pScreen, vScreenPos, width, height);
	return bOnScreen;
}

bool IsScreenPositionValid(Vec2 vScreenPos, S32 iSlop)
{
	S32 w, h;

	if (vScreenPos==NULL)
		return false;

	gfxGetActiveSurfaceSize(&w, &h);

	return (vScreenPos[0] > -iSlop && vScreenPos[0] < w+iSlop && vScreenPos[1] > -iSlop && vScreenPos[1] < h+iSlop);
}

//if pContext is valid, allocate scratch memory
static char *ConvertNameToFit_Internal(SA_PARAM_OP_VALID ExprContext *pContext, SA_PARAM_NN_STR const char* longName, int useLastName, int maxFitLen)
{
	char *temp;
	int i, len;

	if (maxFitLen < 4) maxFitLen = 4;

	len = (int)strlen(longName);

	if (useLastName && len)
	{
		for (i = len-1; i >= 0; --i)
		{
			if (longName[i] != ' ')
			{
				break;
			}
			--len;
		}
	}
	else
	{
		for (i = 0; i < len; ++i)
		{
			if (longName[i] != ' ')
			{
				break;
			}
		}
		len -= i;
		longName += i;
	}

	if (len <= maxFitLen)
	{
		//It already fits
		if (pContext)
		{
			U32 nameLen = (U32)strlen(longName);
			temp = exprContextAllocScratchMemory(pContext, nameLen+1);
			strncpy_s(temp, nameLen+1, longName, nameLen);
			return temp;
		}
		else
		{
			return StructAllocString(longName);
		}
	}

	if (useLastName)
	{
		for (i = len-1; (len-i) <= maxFitLen; --i)
		{
			if (longName[i] == ' ' || longName[i] == '_')
			{
				if (pContext)
				{
					const char* subName = longName + i + 1;
					U32 subLen = (U32)strlen(subName);
					temp = exprContextAllocScratchMemory(pContext, subLen+1);
					strncpy_s(temp, subLen+1, subName, subLen);
					return temp;
				}
				else
				{
					return StructAllocString((longName + i + 1));
				}
			}
		}

		//Smaller name doesn't fit
		for (; i >= 0; --i)
		{
			if (longName[i] == ' ' || longName[i] == '_')
			{
				break;
			}
		}
		if (pContext)
		{
			const char* subName = longName + i + 1;
			temp = exprContextAllocScratchMemory(pContext, maxFitLen+1);
			strncpy_s(temp, maxFitLen+1, subName, maxFitLen);
		}
		else
		{
			temp = StructAllocStringLen((longName + i + 1), maxFitLen);
		}

		temp[maxFitLen-1] = '.';
		temp[maxFitLen-2] = '.';
		temp[maxFitLen-3] = '.';
		return temp;
	}
	else //Use first name
	{
		for (i = 0; i < maxFitLen; ++i)
		{
			if (longName[i] == ' ' || longName[i] == '_')
			{
				if (pContext)
				{
					temp = exprContextAllocScratchMemory(pContext, i+1);
					strncpy_s(temp, i+1, longName, i);
					return temp;
				}
				else
				{
					return StructAllocStringLen(longName, i);
				}
			}
		}

		//Smaller name doesn't fit
		if (pContext)
		{
			temp = exprContextAllocScratchMemory(pContext, maxFitLen+1);
			strncpy_s(temp, maxFitLen+1, longName, maxFitLen);
		}
		else
		{
			temp = StructAllocStringLen(longName, maxFitLen);
		}
		temp[maxFitLen-1] = '.';
		temp[maxFitLen-2] = '.';
		temp[maxFitLen-3] = '.';
		return temp;
	}

	return NULL;
}

char *ConvertNameToFit(const char* longName, int useLastName, int maxFitLen)
{
	return ConvertNameToFit_Internal(NULL, longName, useLastName, maxFitLen);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ConvertNameToFit");
const char* gclGenExprConvertNameToFit(ExprContext *pContext, const char* pchName, int bUseLastName, int iMaxLen)
{
	return ConvertNameToFit_Internal(pContext, pchName, bUseLastName, iMaxLen);
}

char *ConvertTextToFit(const char* longText, int maxFitLen)
{
	char *temp;
	int len;

	if (maxFitLen < 4) maxFitLen = 4;

	len = (longText ? (int)strlen(longText) : 0);

	if (len <= maxFitLen)
	{
		//It already fits
		return StructAllocString(longText);
	}

	temp = StructAllocStringLen(longText, maxFitLen);
	temp[maxFitLen-1] = '.';
	temp[maxFitLen-2] = '.';
	temp[maxFitLen-3] = '.';
	return temp;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntPlayerGetTrayMode");
U32 gclGenExprPlayerGetTrayMode(SA_PARAM_OP_VALID Entity *pEnt)
{
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	return pHUDOptions ? pHUDOptions->uiTrayMode : 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntPlayerGetPowerLevelsMode");
U32 gclGenExprPlayerGetPowerLevelsMode(SA_PARAM_OP_VALID Entity *pEnt)
{
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	return pHUDOptions ? pHUDOptions->uiPowerLevelsMode : 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerGetNotifyAudioMode");
const char* gclExprPlayerGetNotifyAudioMode(SA_PARAM_OP_VALID Entity *pEnt)
{
	const char* pchReturn = NULL;
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	if ( pHUDOptions )
	{
		pchReturn = StaticDefineIntRevLookup(PlayerNotifyAudioModeEnum, pHUDOptions->eNotifyAudioMode);
	}
	return NULL_TO_EMPTY(pchReturn);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntIsPlayingStyle);
bool gclChat_IsPlayingStyleExpr(SA_PARAM_OP_VALID Entity *pEnt, const char *pchType)
{
	char *pchPlayingClassTypes = SAFE_MEMBER4(pEnt, pPlayer, pUI, pLooseUI, pchPlayingStyles);
	char *pchContext = NULL;
	char *pchToken = NULL;

	if (!pchType || !*pchType || !pchPlayingClassTypes || !*pchPlayingClassTypes)
		return false;

	pchPlayingClassTypes = NULL;
	strdup_alloca(pchPlayingClassTypes, pEnt->pPlayer->pUI->pLooseUI->pchPlayingStyles);
	pchToken = strtok_r(pchPlayingClassTypes, ", ", &pchContext);

	do 
	{
		if (pchToken && *pchToken && !stricmp(pchToken, pchType))
		{
			return true;
		}
	}
	while ((pchToken = strtok_r(NULL, ", ", &pchContext)) != NULL);

	return false;
}

static void gclConstructFullName(SA_PRE_NN_OP_VALID char **ppchFullName, SA_PARAM_OP_VALID const char *pchCharacterName, SA_PARAM_OP_VALID const char *pchAccountName, bool bAddQuotesIfNeeded, bool bAddCommaIfNeeded, bool bAddTrailingSpace) {
	bool bAddComma = bAddCommaIfNeeded && pchAccountName && *pchAccountName;
	bool bAddQuotes = bAddQuotesIfNeeded && pchCharacterName && strchr(pchCharacterName, ' ');

	estrClear(ppchFullName);
	if (bAddQuotes && !bAddComma) {
		estrAppend2(ppchFullName, "\"");
	}

	if (pchCharacterName && *pchCharacterName) {
		estrAppend2(ppchFullName, pchCharacterName);
	}

	if (pchAccountName && *pchAccountName) {
		if (*pchAccountName != '@') {
			estrAppend2(ppchFullName, "@");
		}
		estrAppend2(ppchFullName, pchAccountName);

		if (bAddQuotes && !bAddComma) {
			estrAppend2(ppchFullName, "\"");
		} else if (bAddComma) {
			estrAppend2(ppchFullName, ",");
		}

		// Note: We only add trailing spaces to the handle
		// portion because the user might want to type an '@'
		// and restart auto-completion.  This case only happens
		// (at this time) when users to chat auto completion
		// using text that doesn't include a command. (i.e. they
		// are typing a message to a channel using the current channel
		// selection).
		if (bAddTrailingSpace) {
			estrAppend2(ppchFullName, " ");
		}
	} else if (bAddQuotes && !bAddComma) {
		estrAppend2(ppchFullName, "\"");
	}
}

static bool gclAddEntitySuggestion(UIGenTextEntryCompletion ***peaCompletion, const char *pchMatchPrefix, 
								   S32 iPrefixReplaceFrom, S32 iPrefixReplaceTo,
								   const char *pchCharacterName, const char *pchHandle, 
								   SuggestionTestOptions *pTestOptions, SuggestionDisplayOptions *pDisplayOptions,
								   ContainerID **peaiSuggestedEntIds, ContainerID iEntId, S32 *piNumEntries) {
	static char *pchBaseDisplayName = NULL;
	static char *pchColorBegin = NULL;
	const char *pchColorEnd;
	const char *pchSource;
	UIGenTextEntryCompletion *pComp;
	// Should expand minimally should only be true if (1) we want minimal
	// expansion (which means only expand the character or account name, but 
	// not both), and (2) only if the prefix starts with an '@' (meaning it's
	// and account name) or does not contain an '@' (meaning it's a character 
	// name).  If an '@' appears somewhere in the middle of the prefix, then 
	// we assume the user wants both the character and account name and we will
	// therefore do full expansion (both character & account names).
	bool bDoMinimalExpansion = pTestOptions->bDoMinimalExpansion && (*pchMatchPrefix == '@' || !strchr(pchMatchPrefix, '@'));

	// Only add the suggestion if it has not already been added
	if (eaiFind(peaiSuggestedEntIds, iEntId) >= 0) {
		return false;
	}

	// Set up source info
	if (pDisplayOptions->iColor) {
		estrPrintf(&pchColorBegin, "<font color=#%0x>", pDisplayOptions->iColor);
		pchColorEnd = "</font>";
	} else {
		estrClear(&pchColorBegin);
		pchColorEnd = "";
	}

	pchSource = pDisplayOptions->bShowSource ? pDisplayOptions->pchSource : "";

	if (bDoMinimalExpansion) {
		if (*pchMatchPrefix == '@') {
			// Only expand account name
			pchCharacterName = NULL;
		} else if (!strchr(pchMatchPrefix, '@')) {
			// Only expand character name
			pchHandle = NULL;
		}
	}

	// Construct the base display name
	gclConstructFullName(&pchBaseDisplayName, pchCharacterName, pchHandle, false, false, false);

	// Create the complete suggestion entry
	pComp = eaGetStruct(peaCompletion, parse_UIGenTextEntryCompletion, *piNumEntries);
	estrPrintf(&pComp->pchDisplay, "%s<table><tr><td>%s</td><td align=\"right\">%s</td></tr></table>%s", 
		pchColorBegin, pchBaseDisplayName, pchSource, pchColorEnd);
	gclConstructFullName(&pComp->pchSuggestion, pchCharacterName, pchHandle, 
		pDisplayOptions->bAddQuotesIfNeeded, 
		pDisplayOptions->bAddCommaIfNeeded, 
		pDisplayOptions->bAddTrailingSpace);
	pComp->iPrefixReplaceFrom = iPrefixReplaceFrom;
	pComp->iPrefixReplaceTo = iPrefixReplaceTo;

	if (bDebugEntityComplete) {
		printf("Complete Added - Prefix used: '%s'[%d...%d] Suggest: '%s' Display: '%s'\n", pchMatchPrefix,
			pComp->iPrefixReplaceFrom, pComp->iPrefixReplaceTo, pComp->pchSuggestion, pComp->pchDisplay);
	}
	// Keep track of the fact that it was added
	eaiPush(peaiSuggestedEntIds, iEntId);
	(*piNumEntries)++;
	return true;
}

static bool gclTestEntitySuggestion(UIGenTextEntryCompletion ***peaCompletion, const char *pchPrefix, 
										  const char *pchCharacterName, const char *pchHandle, 
										  SuggestionTestOptions *pTestOptions,
										  ContainerID **peaiSuggestedEntIds, ContainerID iEntId) {
	static char *pchFullName = NULL;	
	const char *pchAccountOnly = (*pchPrefix == '@') ? (pchPrefix + 1) : 0;

	// Should expand minimally should only be true if (1) we want minimal
	// expansion (which means only expand the character or account name, but 
	// not both), and (2) only if the prefix starts with an '@' (meaning it's
	// and account name) or does not contain an '@' (meaning it's a character 
	// name).  If an '@' appears somewhere in the middle of the prefix, then 
	// we assume the user wants both the character and account name and we will
	// therefore do full expansion (both character & account names).
	bool bDoMinimalExpansion = pTestOptions->bDoMinimalExpansion && (*pchPrefix == '@' || !strchr(pchPrefix, '@'));

	// Most callers don't include the @, but guild members have the @, so skip it.
	if (pchHandle && pchHandle[0] == '@') {
		pchHandle++;
	}

	if (bDoMinimalExpansion) {
		if (*pchPrefix == '@') {
			// Only expand account name
			pchCharacterName = NULL;
		} else if (!strchr(pchPrefix, '@')) {
			// Only expand character name
			pchHandle = NULL;
		}
	}

	gclConstructFullName(&pchFullName, pchCharacterName, pchHandle, false, false, false);
	if (estrLength(&pchFullName) == 0) {
		return false;
	}

	
	if (strStartsWith(pchFullName, pchPrefix) || 
		(pchAccountOnly && strStartsWith(pchHandle, pchAccountOnly)))	
	{
		if (bDebugEntityComplete) {
			printf("Complete Test(PASS) - Prefix: '%s' Char: '%s' Handle: '%s'\n", pchPrefix, pchCharacterName, pchHandle);
		}
		return true;
	}

	if (bDebugEntityComplete) {
		printf("Complete Test - Prefix: '%s' Char: '%s' Handle: '%s'\n", pchPrefix, pchCharacterName, pchHandle);
	}
	return false;
}

static void gclTestAndAddEntitySuggestions(UIGenTextEntryCompletion ***peaCompletion, const char *pchPrefix,
										   S32 iPrefixReplaceFrom, S32 iPrefixReplaceTo,
										   const char *pchCharacterName, const char *pchHandle, 
										   SuggestionTestOptions *pTestOptions, SuggestionDisplayOptions *pDisplayOptions,
										   ContainerID **peaiSuggestedEntIds, ContainerID iEntId, S32 *piNumEntries) {
	const char *pchTestPrefix = pchPrefix;
	bool bSaveAddCommaIfNeeded = pDisplayOptions->bAddCommaIfNeeded;

	// Don't add the suggestion if it's already been added
	if (eaiFind(peaiSuggestedEntIds, iEntId) >= 0) {
		return;
	}

	while (pchTestPrefix && *pchTestPrefix && strlen(pchTestPrefix) >= MIN_COMPLETE_PREFIX_LENGTH) {
		if (gclTestEntitySuggestion(peaCompletion, pchTestPrefix, pchCharacterName, pchHandle, pTestOptions, peaiSuggestedEntIds, iEntId)) {
			gclAddEntitySuggestion(peaCompletion, pchTestPrefix, iPrefixReplaceFrom, iPrefixReplaceTo, pchCharacterName, pchHandle, pTestOptions, pDisplayOptions, peaiSuggestedEntIds, iEntId, piNumEntries);
		}

		if (pTestOptions->bMayPerformPartialCompletion) {
			U32 iNextWordPos = UTF8CodepointOfNextWordDelim(pchTestPrefix, 0, g_pchCommandWordDelimiters);
			if (!pDisplayOptions->bAddCommaIfNeeded) {
				// If we need a comma, then assume we're going to replace
				// the entire prefix (which works for /tell).  If we don't
				// need a comma, then only replace the part that matches, so
				// bump up iPrefixReplaceFrom.
				iPrefixReplaceFrom += iNextWordPos;

				// If we're not replacing the first word of the prefix, then we never 
				// want to add a comma.  This is primarily for /tell handling.
				pDisplayOptions->bAddCommaIfNeeded = false;
			}
			pchTestPrefix = UTF8GetCodepoint(pchTestPrefix, iNextWordPos);
		} else {
			break;
		}
	}

	pDisplayOptions->bAddCommaIfNeeded = bSaveAddCommaIfNeeded;
}

extern ChatState g_ChatState;
/************************************************************************
 Fill entity name suggestions.  This function searches several buckets of 
 entities to find any that a player may be interested in using in commands.
 These includes team mates, friends, guild mates, recent chat correspondents
 (any channel), local entities, and possibly tell correspondents.

 Arguments are:
 - peaCompletion - The completion model to be filled
 - pchPrefix - The search prefix to use to match against known entity names
 - iPrefixReplaceFrom/To - The positions of text that will be replaced with the suggestion
 - bAllowPartialReplace - If true, each trailing set of word(s) in pchPrefix may be used for matching purposes.
 - bDoMinimalExpansion - If true, then only one of the character or account name name may be expanded,
     if there is no '@' in the middle of the prefix.  (primarily for non-command completion, i.e. 'say')
 - bAddQuotesIfNeeded - The resulting suggestion will be quoted if it is multi-word
 - bAddCommaIfNeeded - The resulting suggestion will have a trailing comma (primarily for /tell)
 - bAddTrailingSpace - The resulting suggestion will have a trailing space
 - bAppendRecentChatReceivers - The suggestion list will have appended to it the
     list of recent /tell correspondents that DO NOT match the prefix.  (primarily for /tell)
************************************************************************/
void gclGenFillEntityNameSuggestions(UIGenTextEntryCompletion ***peaCompletion, const char *pchPrefix, 
									 S32 iPrefixReplaceFrom, S32 iPrefixReplaceTo,
									 bool bAllowPartialReplace, bool bDoMinimalExpansion, 
									 bool bAddQuotesIfNeeded, bool bAddCommaIfNeeded, 
									 bool bAddTrailingSpace, bool bAppendRecentChatReceivers)
{
	static char *pchFullName = NULL;
	static char *pchFullNameQuoted = NULL;
	static const char *pchSource = "";
	static ContainerID *s_eaiID;

	Entity *pRequester = entActivePlayerPtr();
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pRequester);
	Team *pTeam;
	EntityIterator *pIter;
	Entity *pEnt;
	S32 iNumEntries = 0;
	U32 iColor=0xffffffff;
	bool bShouldExpandMinimally = false;
	ChatUserInfo ***peaUserInfos;
	SuggestionDisplayOptions displayOptions = { 0 };
	SuggestionTestOptions testOptions = { 0 };

	eaiClear(&s_eaiID);

	if (strlen(pchPrefix) < MIN_COMPLETE_PREFIX_LENGTH)
	{
		eaSetSizeStruct(peaCompletion, parse_UIGenTextEntryCompletion, 0);
		return;
	}

	// Initialize test options
	//
	testOptions.bDoMinimalExpansion = bDoMinimalExpansion;
	testOptions.bMayPerformPartialCompletion = bAllowPartialReplace;
		
	// Initialize display options
	displayOptions.bAddCommaIfNeeded = bAddCommaIfNeeded;
	displayOptions.bAddQuotesIfNeeded = bAddQuotesIfNeeded;
	displayOptions.bAddTrailingSpace = bAddTrailingSpace;
	displayOptions.iColor = 0;
	displayOptions.bShowSource = pConfig ? pConfig->bAnnotateAutoComplete : false;
	displayOptions.pchSource = NULL;
	estrCreate(&displayOptions.pchSource);

	// Look at teammates
	if (pRequester)
	{
		pTeam = pRequester->pTeam ? GET_REF(pRequester->pTeam->hTeam) : NULL;
		if (pTeam && pRequester->pTeam->eState == TeamState_Member)
		{
			displayOptions.iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Team, NULL, ChatCommon_GetChatConfigSourceForEntity(pRequester));
			ClientChat_GetMessageTypeDisplayNameByType(&displayOptions.pchSource, kChatLogEntryType_Team);

			EARRAY_FOREACH_BEGIN(pTeam->eaMembers, i);
			{
				TeamMember *pMember = pTeam->eaMembers[i];
				if (pMember->iEntID != pRequester->myContainerID) {
					pEnt = GET_REF(pMember->hEnt);
					if (pEnt && pEnt->pPlayer) {
						gclTestAndAddEntitySuggestions(peaCompletion, pchPrefix, 
							iPrefixReplaceFrom, iPrefixReplaceTo,
							entGetLocalName(pEnt), pEnt->pPlayer->publicAccountName, 
							&testOptions, &displayOptions, 
							&s_eaiID, entGetContainerID(pEnt), &iNumEntries);
					}
				}
			}
			EARRAY_FOREACH_END;
		}
	}

	// Look at friends
#ifdef USE_CHATRELAY
	EARRAY_FOREACH_BEGIN(g_ChatState.eaFriends, i);
	{
		ChatPlayerStruct *pFriend = g_ChatState.eaFriends[i];
		displayOptions.iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Friend, NULL, ChatCommon_GetChatConfigSourceForEntity(pRequester));
		ClientChat_GetMessageTypeDisplayNameByType(&displayOptions.pchSource, kChatLogEntryType_Friend);

		gclTestAndAddEntitySuggestions(peaCompletion, pchPrefix, 
			iPrefixReplaceFrom, iPrefixReplaceTo,
			pFriend->pPlayerInfo.onlinePlayerName, pFriend->chatHandle, 
			&testOptions, &displayOptions, 
			&s_eaiID, pFriend->pPlayerInfo.onlineCharacterID, &iNumEntries);

	}
	EARRAY_FOREACH_END;
#else
	EARRAY_FOREACH_BEGIN(pRequester->pPlayer->pUI->pChatState->eaFriends, i);
	{
		ChatPlayerStruct *pFriend = pRequester->pPlayer->pUI->pChatState->eaFriends[i];
		displayOptions.iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Friend, NULL, ChatCommon_GetChatConfigSourceForEntity(pRequester));
		ClientChat_GetMessageTypeDisplayNameByType(&displayOptions.pchSource, kChatLogEntryType_Friend);

		gclTestAndAddEntitySuggestions(peaCompletion, pchPrefix, 
			iPrefixReplaceFrom, iPrefixReplaceTo,
			pFriend->pPlayerInfo.onlinePlayerName, pFriend->chatHandle, 
			&testOptions, &displayOptions, 
			&s_eaiID, pFriend->pPlayerInfo.onlineCharacterID, &iNumEntries);
	}
	EARRAY_FOREACH_END;
#endif

	// Look at guild mates
	if (pRequester && pRequester->pPlayer->pGuild)
	{
		Guild *pGuild = GET_REF(pRequester->pPlayer->pGuild->hGuild);
		if (pGuild)
		{
			displayOptions.iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Guild, NULL, ChatCommon_GetChatConfigSourceForEntity(pRequester));
			ClientChat_GetMessageTypeDisplayNameByType(&displayOptions.pchSource, kChatLogEntryType_Guild);

			EARRAY_FOREACH_BEGIN(pGuild->eaMembers, i);
			{
				GuildMember *pMember = pGuild->eaMembers[i];
				if (pMember->iEntID != pRequester->myContainerID) {
					gclTestAndAddEntitySuggestions(peaCompletion, pchPrefix, 
						iPrefixReplaceFrom, iPrefixReplaceTo,
						pMember->pcName, pMember->pcAccount, 
						&testOptions, &displayOptions, 
						&s_eaiID, pMember->iEntID, &iNumEntries);
				}
			}
			EARRAY_FOREACH_END;
		}
	}

	// Look at recent chat receivers (people you've sent messages to)
	peaUserInfos = ChatLog_GetRecentChatReceivers();
	if (peaUserInfos) {
		displayOptions.iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Private_Sent, NULL, ChatCommon_GetChatConfigSourceForEntity(pRequester));
		estrCopy2(&displayOptions.pchSource, entTranslateMessageKey(pRequester, "AutoComplete_Source_RecentChatReceivers"));

		EARRAY_FOREACH_BEGIN(*peaUserInfos, i);
		{
			ChatUserInfo *pInfo = (*peaUserInfos)[i];
			gclTestAndAddEntitySuggestions(peaCompletion, pchPrefix, 
				iPrefixReplaceFrom, iPrefixReplaceTo,
				pInfo->pchName, pInfo->pchHandle, 
				&testOptions, &displayOptions, 
				&s_eaiID, pInfo->playerID, &iNumEntries);
		}
		EARRAY_FOREACH_END;
	}

	// Look at recent chat senders (people you've received messages from)
	peaUserInfos = ChatLog_GetRecentChatSenders();
	if (peaUserInfos) {
		displayOptions.iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Private, NULL, ChatCommon_GetChatConfigSourceForEntity(pRequester));
		estrCopy2(&displayOptions.pchSource, entTranslateMessageKey(pRequester, "AutoComplete_Source_RecentChatSenders"));

		EARRAY_FOREACH_BEGIN(*peaUserInfos, i);
		{
			ChatUserInfo *pInfo = (*peaUserInfos)[i];
			gclTestAndAddEntitySuggestions(peaCompletion, pchPrefix, 
				iPrefixReplaceFrom, iPrefixReplaceTo,
				pInfo->pchName, pInfo->pchHandle, 
				&testOptions, &displayOptions, 
				&s_eaiID, pInfo->playerID, &iNumEntries);
		}
		EARRAY_FOREACH_END;
	}

	// Look for other local entities
	if (pRequester)
	{
		pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
		displayOptions.iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Local, NULL, ChatCommon_GetChatConfigSourceForEntity(pRequester));
		ClientChat_GetMessageTypeDisplayNameByType(&displayOptions.pchSource, kChatLogEntryType_Local);

		while ((pEnt = EntityIteratorGetNext(pIter)))
		{
			ContainerID id = entGetContainerID(pEnt);
			if (id != pRequester->myContainerID && eaiFind(&s_eaiID, id) < 0)
			{
				gclTestAndAddEntitySuggestions(peaCompletion, pchPrefix, 
					iPrefixReplaceFrom, iPrefixReplaceTo,
					entGetLocalName(pEnt), pEnt->pPlayer->publicAccountName, 
					&testOptions, &displayOptions, 
					&s_eaiID, id, &iNumEntries);
			}
		}
		EntityIteratorRelease(pIter);
	}

	// Add Self - This is generally the most rarely useful suggestion, so it should be last 
	// amongst suggestions that match the prefix
	displayOptions.iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Local, NULL, ChatCommon_GetChatConfigSourceForEntity(pRequester));
	estrCopy2(&displayOptions.pchSource, entTranslateMessageKey(pRequester, "AutoComplete_Source_Self"));

	gclTestAndAddEntitySuggestions(peaCompletion, pchPrefix, 
		iPrefixReplaceFrom, iPrefixReplaceTo,
		pRequester ? entGetLocalName(pRequester) : NULL, gclClientChat_GetAccountDisplayName(), 
		&testOptions, &displayOptions,
		&s_eaiID, pRequester ? entGetContainerID(pRequester): 0, &iNumEntries);

	// Append all recent chat receivers (if desired)
	//
	// NOTE: These are NOT tested whether against the pchPrefix.
	//       They should always appear.  This is due to a feature request
	//       that players be able to easily get a list of recent correspondents
	//       while replying.  As written, this code is more general in that recent
	//       correspondents are always shown, however the number presented is limited
	//       to MAX_RECENT_TELL_SUGGESTIONS for both sent & received tells.
	if (bAppendRecentChatReceivers) {
		S32 iTellSuggestionCount = 0, i;
		displayOptions.iColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Private, NULL, ChatCommon_GetChatConfigSourceForEntity(pRequester));
		estrCopy2(&displayOptions.pchSource, entTranslateMessageKey(pRequester, "AutoComplete_Source_RecentTellCorrespondent"));

		peaUserInfos = ChatLog_GetRecentTellCorrespondents();
		for (i=0; i < eaSize(peaUserInfos) && iTellSuggestionCount < MAX_RECENT_TELL_SUGGESTIONS; i++) {
			ChatUserInfo *pInfo = (*peaUserInfos)[i];
			if (gclAddEntitySuggestion(peaCompletion, pchPrefix, iPrefixReplaceFrom, iPrefixReplaceTo, 
				pInfo->pchName, pInfo->pchHandle, &testOptions, &displayOptions, 
				&s_eaiID, pInfo->playerID, &iNumEntries)) {
					iTellSuggestionCount++;
			}
		}
	}

	// Cleanup
	estrDestroy(&displayOptions.pchSource);

	// Clamp resulting completion array down to iNumEntries
	iNumEntries = MIN(iNumEntries, MAX_NAME_SUGGESTIONS);
	eaSetSizeStruct(peaCompletion, parse_UIGenTextEntryCompletion, iNumEntries);
}

// Suggest an entity name from a prefix.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSuggestEntityName");
void gclGenExprSuggestEntityName(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchPrefix)
{
	UIGenTextEntryCompletion ***peaCompletion = ui_GenGetManagedListSafe(pGen, UIGenTextEntryCompletion);
	gclGenFillEntityNameSuggestions(peaCompletion, pchPrefix, 0, -1, false, false, false, false, false, false);
	ui_GenSetManagedListSafe(pGen, peaCompletion, UIGenTextEntryCompletion, true);
}

// Returns true if the entity is a GM
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsGM");
bool exprEntGetIsGM(SA_PARAM_OP_VALID Entity *pEntitySource)
{
	if(pEntitySource && pEntitySource->pPlayer && pEntitySource->pPlayer->bIsGM)
	{
		return true;
	}	
	return false;
}

// Returns true if the entity is a Dev
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsDev");
bool exprEntGetIsDev(SA_PARAM_OP_VALID Entity *pEntitySource)
{
	if(pEntitySource && pEntitySource->pPlayer && pEntitySource->pPlayer->bIsDev)
	{
		return true;
	}	
	return false;
}

// Returns true if the entity is playing a given FX
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetIsPlayingFx);
bool exprEntGetIsPlayingFx(SA_PARAM_OP_VALID Entity *pEnt, const char *pchFx)
{
	// This is probably the wrong way to do this.
	DynFxManager *pFxManager = pEnt ? dynFxManFromGuid(pEnt->dyn.guidFxMan) : NULL;
	pchFx = pchFx && *pchFx ? allocFindString(pchFx) : pchFx;
	if (pFxManager && pchFx && *pchFx)
	{
		S32 i;
		for (i = 0; i < eaSize(&pFxManager->eaDynFx); i++)
		{
			DynFx *pDynFx = pFxManager->eaDynFx[i];
			if (pDynFx && REF_STRING_FROM_HANDLE(pDynFx->hInfo) == pchFx)
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetLocalTimeAsString);
const char* exprGetLocalTimeAsString(ExprContext *pContext, bool bShowSeconds, bool b24Hour )
{
	return timeGetLocalTimeStringEx(bShowSeconds,b24Hour);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetLocalTime);
U32 exprGetLocalTime()
{
	return timeSecondsSince2000();
}

S32 gclUIGen_GetServerUTCOffset(U32 uTimestamp)
{
	// PST timezone information currently configured for
	//   Start: 2nd week of March at 2:00 AM PST (10:00 AM GMT)
	//   End: 1st week of November at 2:00 AM PDT (9:00 AM GMT)
#define PDST_START_MONTH (3 - 1) // Month: [0,11]
#define PDST_END_MONTH (11 - 1)
#define PDST_START_WEEK (2 - 1) // Week: [0,5]
#define PDST_END_WEEK (1 - 1)
#define PDST_START_DAY 0 // Day: [0,6]
#define PDST_END_DAY 0
#define PDST_START_TIME ((10 - 1) * 3600) // Time of day: [0,86400]
#define PDST_END_TIME ((9 - 1) * 3600)
	int iDayOfFirst, iWeekOfMonth, iSeconds;
	bool bDaylightSavingsTime = false;
	struct tm tmInfo;
	timeMakeTimeStructFromSecondsSince2000(uTimestamp, &tmInfo);
	iDayOfFirst = tmInfo.tm_wday - (tmInfo.tm_mday - 1) % 7;
	iDayOfFirst += iDayOfFirst < 0 ? 7 : 0;
	iWeekOfMonth = (tmInfo.tm_mday - 1 + iDayOfFirst) / 7 + ((iDayOfFirst != 0) ? 1 : 0);
	iSeconds = tmInfo.tm_hour * 3600 + tmInfo.tm_min * 60 + tmInfo.tm_sec;
	if (tmInfo.tm_mon == PDST_START_MONTH)
	{
		if (iWeekOfMonth == PDST_START_WEEK)
		{
			if (tmInfo.tm_wday == PDST_START_DAY)
				bDaylightSavingsTime = iSeconds >= PDST_START_TIME;
			else
				bDaylightSavingsTime = tmInfo.tm_wday > PDST_START_DAY;
		}
		else
			bDaylightSavingsTime = iWeekOfMonth > PDST_START_WEEK;
	}
	else if (tmInfo.tm_mon == PDST_END_MONTH)
	{
		if (iWeekOfMonth == PDST_END_WEEK)
		{
			if (tmInfo.tm_wday == PDST_END_DAY)
				bDaylightSavingsTime = iSeconds < PDST_END_TIME;
			else
				bDaylightSavingsTime = tmInfo.tm_wday < PDST_END_DAY;
		}
		else
			bDaylightSavingsTime = iWeekOfMonth < PDST_END_WEEK;
	}
	else
	{
		bDaylightSavingsTime = PDST_START_MONTH < tmInfo.tm_mon && tmInfo.tm_mon < PDST_END_MONTH;
	}
	return bDaylightSavingsTime ? -7 : -8;
}

// Use a MessageKey to format the provided timestamp, using the timezone of the client.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatLocalTimestamp);
	const char* exprMessageFormatLocalTimestamp(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey, U32 uSeconds)
{
	static char *s_estr = NULL;
	const char *pchReturn = NULL;
	estrClear(&s_estr);
	FormatGameMessageKey(&s_estr, pchFormatMessageKey, STRFMT_DATETIME("Time", uSeconds), STRFMT_DATETIME("Value", uSeconds), STRFMT_END);
	pchReturn = exprContextAllocString(pContext, s_estr);
	return pchReturn;
}

// Use a string to format the provided timestamp, using the timezone of the client.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatLocalTimestamp);
	const char* exprStringFormatLocalTimestamp(ExprContext *pContext, const char* pchFormat, U32 uSeconds)
{
	static char *s_estr = NULL;
	const char *pchReturn = NULL;
	estrClear(&s_estr);
	FormatGameString(&s_estr, pchFormat, STRFMT_DATETIME("Time", uSeconds), STRFMT_DATETIME("Value", uSeconds), STRFMT_END);
	pchReturn = exprContextAllocString(pContext, s_estr);
	return pchReturn;
}

// Use a MessageKey to format the provided timestamp, for the given UTC offset.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatUTCOffsetTimestamp);
	const char* exprMessageFormatUTCOffsetTimestamp(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey, S32 iHourOffset, U32 uSeconds)
{
	static char *s_estr = NULL;
	const char *pchReturn = NULL;
	estrClear(&s_estr);
	// TODO: Handle XBOX: timeLocalOffsetFromUTC() DOES_NOT_WORK_ON_XBOX
#if !_XBOX
	uSeconds = uSeconds - timeDaylightLocalOffsetFromUTC(uSeconds) + iHourOffset * 3600;
#else
	uSeconds = uSeconds - iHourOffset * 3600;
#endif
	FormatGameMessageKey(&s_estr, pchFormatMessageKey, STRFMT_DATETIME("Time", uSeconds), STRFMT_DATETIME("Value", uSeconds), STRFMT_END);
	pchReturn = exprContextAllocString(pContext, s_estr);
	return pchReturn;
}

// Use a string to format the provided timestamp, for the given UTC offset.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatUTCOffsetTimestamp);
	const char* exprStringFormatUTCOffsetTimestamp(ExprContext *pContext, const char* pchFormat, S32 iHourOffset, U32 uSeconds)
{
	static char *s_estr = NULL;
	const char *pchReturn = NULL;
	estrClear(&s_estr);
	// TODO: Handle XBOX: timeLocalOffsetFromUTC() DOES_NOT_WORK_ON_XBOX
#if !_XBOX
	uSeconds = uSeconds - timeDaylightLocalOffsetFromUTC(uSeconds) + iHourOffset * 3600;
#else
	uSeconds = uSeconds - iHourOffset * 3600;
#endif
	FormatGameString(&s_estr, pchFormat, STRFMT_DATETIME("Time", uSeconds), STRFMT_DATETIME("Value", uSeconds), STRFMT_END);
	pchReturn = exprContextAllocString(pContext, s_estr);
	return pchReturn;
}

// Use a MessageKey to format the provided timestamp, using the server's timezone.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatServerTimestamp);
const char* exprMessageFormatServerTimestamp(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey, U32 uSeconds)
{
	return exprMessageFormatUTCOffsetTimestamp(pContext, pchFormatMessageKey, gclUIGen_GetServerUTCOffset(uSeconds), uSeconds);
}

// Use a string to format the provided timestamp, using the server's timezone.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatServerTimestamp);
const char* exprStringFormatServerTimestamp(ExprContext *pContext, const char* pchFormat, U32 uSeconds)
{
	return exprStringFormatUTCOffsetTimestamp(pContext, pchFormat, gclUIGen_GetServerUTCOffset(uSeconds), uSeconds);
}

// Deprecated: Use MessageFormatLocalTime instead
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatLocalTime);
const char* exprMessageFormatLocalTime(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey);

// Format the current client time using a message key in the client's timezone.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatLocalTime);
const char* exprMessageFormatLocalTime(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey)
{
	return exprMessageFormatLocalTimestamp(pContext, pchFormatMessageKey, timeSecondsSince2000());
}

// Format the current client time using a string in the client's timezone.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatLocalTime);
const char* exprStringFormatLocalTime(ExprContext *pContext, const char* pchFormat)
{
	return exprStringFormatLocalTimestamp(pContext, pchFormat, timeSecondsSince2000());
}

// Deprecated: Use MessageFormatUTCOffsetTime
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatUTCOffsetTime);
const char* exprMessageFormatUTCOffsetTime(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey, int iHourOffset);

// Format the current server time using a message key with a given hour offset from UTC.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatUTCOffsetTime);
const char* exprMessageFormatUTCOffsetTime(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey, int iHourOffset)
{
	return exprMessageFormatUTCOffsetTimestamp(pContext, pchFormatMessageKey, iHourOffset, timeServerSecondsSince2000());
}

// Format the current server time using a string with a given hour offset from UTC.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatUTCOffsetTime);
const char* exprStringFormatUTCOffsetTime(ExprContext *pContext, const char* pchFormat, int iHourOffset)
{
	return exprStringFormatUTCOffsetTimestamp(pContext, pchFormat, iHourOffset, timeServerSecondsSince2000());
}

// Deprecated: Use MessageFormatServerTime
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatServerTime);
const char* exprMessageFormatServerTime(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey);

// Format the server's time using a message key in the server's timezone.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatServerTime);
const char* exprMessageFormatServerTime(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey)
{
	U32 uSeconds = timeServerSecondsSince2000();
	return exprMessageFormatUTCOffsetTimestamp(pContext, pchFormatMessageKey, gclUIGen_GetServerUTCOffset(uSeconds), uSeconds);
}

// Format the server's time using a string in the server's timezone.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatServerTime);
const char* exprStringFormatServerTime(ExprContext *pContext, const char* pchFormat)
{
	U32 uSeconds = timeServerSecondsSince2000();
	return exprStringFormatUTCOffsetTimestamp(pContext, pchFormat, gclUIGen_GetServerUTCOffset(uSeconds), uSeconds);
}

// Deprecated name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatTimer);
const char* exprMessageFormatTimer(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey, int iTimeInSeconds);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatTimer);
const char* exprMessageFormatTimer(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey, int iTimeInSeconds)
{
	char *estrResult = NULL;
	const char *pchReturn = NULL;
	estrStackCreate(&estrResult);
	FormatGameMessageKey(&estrResult, pchFormatMessageKey,
		STRFMT_TIMER("Time", iTimeInSeconds),
		STRFMT_TIMER("Value", iTimeInSeconds),
		STRFMT_END);
	pchReturn = exprContextAllocString(pContext, estrResult);
	estrDestroy(&estrResult);
	return pchReturn;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatTimer);
const char* exprStringFormatTimer(ExprContext *pContext, const char* pchFormat, int iTimeInSeconds)
{
	char *estrResult = NULL;
	const char *pchReturn = NULL;
	estrStackCreate(&estrResult);
	FormatGameString(&estrResult, pchFormat,
		STRFMT_TIMER("Time", iTimeInSeconds),
		STRFMT_TIMER("Value", iTimeInSeconds),
		STRFMT_END);
	pchReturn = exprContextAllocString(pContext, estrResult);
	estrDestroy(&estrResult);
	return pchReturn;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatMapWorldTime);
const char* exprFormatMapWorldTime(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char* pchFormatMessageKey)
{
	char *estrResult = NULL;
	const char *pchReturn = NULL;
	int iHours12, iHours24, iMinutes, iSeconds;
	F32 fTime = wlTimeGet();

	iHours24 = (int)fTime;
	iHours12 = (iHours24 == 0 || iHours24 == 12) ? 12 : iHours24 % 12;
	iMinutes = (int)(fTime * 60) % 60;
	iSeconds = (int)(fTime * 3600) % 60;

	estrStackCreate(&estrResult);
	FormatGameMessageKey(&estrResult, pchFormatMessageKey,
		STRFMT_INT("WorldHours", iHours12),
		STRFMT_INT("WorldHours12", iHours12),
		STRFMT_INT("WorldHours24", iHours24),
		STRFMT_INT("WorldMinutes", iMinutes),
		STRFMT_INT("WorldSeconds", iSeconds),
		STRFMT_END);
	pchReturn = exprContextAllocString(pContext, estrResult);
	estrDestroy(&estrResult);
	return pchReturn;
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenOpenWebBrowser");
void exprGenOpenWebBrowser(const char *pchUrl)
{
#if !PLATFORM_CONSOLE
	if (pchUrl && *pchUrl)
	{
		ShellExecute(NULL, "open", pchUrl, NULL, NULL, SW_SHOWNORMAL);
	}
#endif
}



//bShorten will use the first valid H/M/S block - ex: 30m20s would be shorted to 30m
//bHideSeconds, if true, will truncate 's' from the output
const char* gclGetSecondsAsHMS(S32 iSeconds, bool bShorten, bool bHideSeconds)
{
	static char pchTime[20];
	const char* ppchUnits[3] = {"DateTime_HoursOneLetter","DateTime_MinutesOneLetter","DateTime_SecondsOneLetter"};
	S32 i, iSize = 0;
	S32 pHMS[3] = {0, 0, 0};

	timeSecondsGetHoursMinutesSeconds(iSeconds, pHMS, bShorten);

	for (i = 0; i < 3; i++)
	{
		if (pHMS[i] > 0)
		{
			char pchBuffer[12];
			S32 iLen;
			itoa(pHMS[i],pchBuffer,10);
			iLen = (S32)strlen(pchBuffer);
			strcpy_s(pchTime+iSize,iLen+1,pchBuffer);
			if (i < 2 || !bHideSeconds)
			{
				const char* pchUnits = TranslateMessageKey(ppchUnits[i]);
				if (pchUnits && pchUnits[0])
					pchTime[iSize+iLen++] = pchUnits[0]; 
			}
			iSize += iLen;
		}
	}
	pchTime[iSize] = 0;
	return pchTime;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetSecondsAsHMS);
const char* exprGetSecondsAsHMS(S32 iSeconds, bool bShorten)
{
	return gclGetSecondsAsHMS(iSeconds, bShorten, bShorten);
}

// if iSeconds is over iShortenOutputOverSeconds, then the output will be abbreviated
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetSecondsAsHMS2);
const char* exprGetSecondsAsHMSEx(S32 iSeconds, S32 iShortenOutputOverSeconds, bool bHideSeconds)
{
	return gclGetSecondsAsHMS(iSeconds, 
		iShortenOutputOverSeconds > 0 && iSeconds >= iShortenOutputOverSeconds, 
		bHideSeconds);
}

void gclFormatSecondsAsHMS(char** pestrResult, 
						   S32 iSeconds, S32 iShortenOutputOverSeconds,
						   const char* pchFormatMessageKey)
{
	S32 pHMS[3] = {0, 0, 0};
	timeSecondsGetHoursMinutesSeconds(iSeconds, pHMS, 
		iShortenOutputOverSeconds > 0 && iSeconds >= iShortenOutputOverSeconds);
	FormatGameMessageKey(pestrResult, pchFormatMessageKey,
		STRFMT_INT("TimeHours", pHMS[0]),
		STRFMT_INT("TimeMinutes", pHMS[1]),
		STRFMT_INT("TimeSeconds", pHMS[2]),
		STRFMT_END);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatSecondsAsHMS);
const char* exprFormatSecondsAsHMS(ExprContext *pContext,
								   S32 iSeconds, S32 iShortenOutputOverSeconds, 
								   const char* pchFormatMessageKey)
{
	char *estrResult = NULL;
	const char *pchReturn = NULL;
	estrStackCreate(&estrResult);
	gclFormatSecondsAsHMS(&estrResult, iSeconds, iShortenOutputOverSeconds, pchFormatMessageKey);
	pchReturn = exprContextAllocString(pContext, estrResult);
	estrDestroy(&estrResult);
	return pchReturn;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetTotalElapsedTime);
F32 exprGetTotalElapsedTime(void)
{
	return gGCLState.totalElapsedTimeMs / 1000.0;
}

// Check if a particular shard-wide event is active.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ShardWideEventIsActive);
bool gclExprShardWideEventIsActive(const char *pchEvent)
{
	pchEvent = allocFindString(pchEvent);
	return eaFind(&gGCLState.shardWideEvents, pchEvent) >= 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SwitchUISkin);
void gclExprSwitchUISkin(const char *pcSkinName);

AUTO_COMMAND ACMD_NAME(SwitchUISkin);
void gclExprSwitchUISkin(const char *pcSkinName)
{
	static const char *pcPreviousSkin = NULL;
	char buf[256];

	// Quit early if changing skin to current skin
	if (pcPreviousSkin && (stricmp(pcSkinName, pcPreviousSkin) == 0)) {
		return;
	}

	if (ResourceOverlayExists("UITextureAssembly", pcSkinName)) {
		// Clear gen state for previous skin
		// Note that this starts as NULL, so the first time called it won't unset anything
		if (pcPreviousSkin) {
			sprintf(buf, "UISkin%s", pcPreviousSkin);
			ui_GenSetGlobalStateName(buf, false);
		}

		// Save the new skin as previous
		pcPreviousSkin = allocAddString(pcSkinName);

		// Set gen state for new skin
		sprintf(buf, "UISkin%s", pcSkinName);
		ui_GenSetGlobalStateName(buf, true);

		// Load alternate resource overlays for the skin
		ResourceOverlayLoad("UITextureAssembly", pcSkinName);
		ResourceOverlayLoad("UIStyleFont", pcSkinName);
		ResourceOverlayLoad("UIStyleBar", pcSkinName);

		// Inform the sound system about the change
		sndSetUISkinName(pcSkinName);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TruncateString);
const char* exprTruncateString(ExprContext *pContext, const char* pchString, U32 iMaxSize)
{
	if ( iMaxSize < 3 )
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "TruncateString: iMaxSize must be at least 3 to make room for \'...\'");
	}
	else
	{
		// Run to almost the end of pchString
		U32 numCodePointsToCopy = iMaxSize-3; // iMaxSize without the "..." we might add
		const unsigned char* next = pchString;
		while(*next && numCodePointsToCopy)
		{
			numCodePointsToCopy--;
			next += UTF8GetCodepointLength(next);
		}

		if( *next && UTF8GetLength(next)>3) // Check if the remaining text is longer than "..."
		{
			// Something to truncate
			char *pchBuffer = _alloca( (next-pchString) + 3 + 1); // +3 for the periods, +1 for the '\0'

			memmove(pchBuffer, pchString, next-pchString);
			pchBuffer[next-pchString+0] = '.';
			pchBuffer[next-pchString+1] = '.';
			pchBuffer[next-pchString+2] = '.'; // 0x2026 ellipsis character would be great to use, but not all fonts have it.
			pchBuffer[next-pchString+3] = '\0';
			return exprContextAllocString(pContext, pchBuffer);
		}
	}
	return pchString; // return the original string, untruncated
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SMFEntityReplaceString);
const char* exprSMFEntityReplaceString(ExprContext *pContext, const char* pchString)
{
	if( !pchString || !strchr(pchString, '&' )) {
		return pchString;
	} else {
		S32 iLen = strlen(pchString);
		char *pchBuffer = _alloca(iLen + 1);
		strcpy_s(pchBuffer, iLen + 1, pchString);
		smf_EntityReplace(pchBuffer);
		return exprContextAllocString(pContext, pchBuffer);
	}
}

//This is a hack to get Klingon power icons from standard icon names
const char* gclGetBestPowerIcon(const char* pchIcon, const char* pchLastIcon)
{
	if (pchIcon && pchIcon[0] && gConf.pcUISkinTrayPowerIconHack && gConf.pcUISkinTrayPowerIconHack[0])
	{
		static UIGenState s_eState = kUIGenStateNone;
		char pchBuffer[MAX_PATH];
		if (s_eState == kUIGenStateNone)
		{
			sprintf(pchBuffer, "UISkin%s", gConf.pcUISkinTrayPowerIconHack);
			s_eState = StaticDefineIntGetInt(UIGenStateEnum, pchBuffer);
			devassert(s_eState != kUIGenStateNone);
		}
		if (ui_GenInGlobalState(s_eState))
		{
			const char* pchPooledIcon = NULL;
			sprintf(pchBuffer, "%s_%s", pchIcon, gConf.pcUISkinTrayPowerIconHack);
			pchPooledIcon = allocFindString(pchBuffer);
			if ((pchPooledIcon && pchPooledIcon==pchLastIcon) || texFind(pchBuffer, false))
			{
				return pchPooledIcon ? allocAddString(pchPooledIcon) : allocAddString(pchBuffer);
			}
		}
	}
	return allocAddString(pchIcon);
}

const char* gclGetBestIconName(const char* pchIconName, const char* pchDefaultIcon)
{
	static const char* pchLastIcon = NULL;

	if (!pchIconName || !pchIconName[0])
	{
		pchIconName = pchDefaultIcon;
	}
	pchLastIcon = gclGetBestPowerIcon(pchIconName, pchLastIcon);
	return pchLastIcon;
}

// Return the entity's skill type (SkillTypeArms, SkillTypeMysticism, etc.)
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSkillType);
U32 exprEntGetSkillType(SA_PARAM_OP_VALID Entity *pEnt)
{
	return pEnt ? entity_GetSkill(pEnt) : kSkillType_None;
}

// Save a currently open window to later be reopened by GenReopenWindows. You cannot save a hidden window, a child of a window, or a modal window.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSaveWindow);
bool ui_GenExprSaveWindow(SA_PARAM_NN_VALID UIGen *pGen)
{
	int i;
	UIGenReopenInformation *pInfo = NULL;
	bool bJailed = ui_GenInState(pGen, kUIGenStateJailed);

	for (i = 0; i < eaSize(&s_eaSavedWindows); i++)
	{
		if (s_eaSavedWindows[i]->pchName == pGen->pchName
			&& !stricmp(s_eaSavedWindows[i]->displayName, gGCLState.displayName)
			&& !stricmp(s_eaSavedWindows[i]->loginCharacterName, gGCLState.loginCharacterName))
		{
			pInfo = eaRemove(&s_eaSavedWindows, i);
			break;
		}
	}

	if (!UI_GEN_READY(pGen) || pGen->bIsRoot
		|| !bJailed && (!pGen->pParent || !pGen->pParent->bIsRoot || !stricmp(pGen->pParent->pchName, "Modal_Root"))
		|| bJailed && (!pGen->pParent && pGen->pSpriteCache && pGen->pSpriteCache->iAccumulate <= 0))
	{
		if (pInfo)
			StructDestroy(parse_UIGenReopenInformation, pInfo);
		return false;
	}

	if (!pInfo)
		pInfo = StructCreate(parse_UIGenReopenInformation);

	pInfo->bJailed = bJailed;
	pInfo->pchName = pGen->pchName;
	strcpy(pInfo->displayName, gGCLState.displayName);
	strcpy(pInfo->loginCharacterName, gGCLState.loginCharacterName);
	eaPush(&s_eaSavedWindows, pInfo);
	return true;
}

// Reopen all the windows saved by GenSaveWindow.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenReopenWindows);
int ui_GenExprReopenWindows(void)
{
	int iOpened = 0;

	while (eaSize(&s_eaSavedWindows) > 0)
	{
		UIGenReopenInformation *pInfo = eaPop(&s_eaSavedWindows);
		UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pInfo->pchName);
		if (!UI_GEN_READY(pGen) && pGen && !pGen->pParent
			&& !stricmp(pInfo->displayName, gGCLState.displayName)
			&& !stricmp(pInfo->loginCharacterName, gGCLState.loginCharacterName))
		{
			if (pInfo->bJailed)
				ui_GenJailKeeperAdd(NULL, pGen);
			else
				ui_GenAddWindow(pGen);
			iOpened++;
		}
		StructDestroy(parse_UIGenReopenInformation, pInfo);
	}

	return iOpened;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LogoffTime");
F32 gclExpr_GetLogoffTime(void)
{
	Entity *pEnt = entActivePlayerPtr();
	F32 r = 0.f;
	if(pEnt && pEnt->pPlayer)
	{
		r = MAX(0.f, pEnt->pPlayer->fLogoffTime);
	}

	return r;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LogoffCancel");
void gclExpr_LogoffCancel(void)
{
	CancelLogOut();
	ServerCmd_LogoffCancelServer();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ParseTime);
int gclExpr_ParseTime(const char *pchTime)
{
	char achTimeBuf[32] = {0};
	char *pchTimeBuf;
	char chMBuf = '\0';
	int iTimeBuf = 0;
	int iHour = 0, iMin = 0, iSec = 0;
	int iDigis = 0;

	if (!pchTime)
		return 0;

	// Trim leading whitespace
	while (*pchTime && isspace((unsigned char)*pchTime))
		pchTime++;

	// Extract time digits
	pchTimeBuf = achTimeBuf;
	while (*pchTime && (isdigit(*pchTime) || strchr(" \r\n\t:.", *pchTime)) && pchTimeBuf != achTimeBuf+32)
	{
		if (isdigit(*pchTime))
		{
			// Ignore multiple leading zeros
			if (!(pchTimeBuf == achTimeBuf+1 && *achTimeBuf == '0'))
			{
				*pchTimeBuf++ = *pchTime;
				iDigis++;
			}
		}
		else if (*pchTime == ':')
		{
			switch (iDigis)
			{
			case 0:
				*pchTimeBuf++ = '0';
				// add two zeros
			case 1:
				// insert a zero
				*pchTimeBuf = *(pchTimeBuf-1);
				*(pchTimeBuf-1) = '0';
				pchTimeBuf++;
			case 2:
				break;
			default:
				// erase extra digits
				for (; iDigis > 2; iDigis--)
					*pchTimeBuf-- = '\0';
			}
			iDigis = 0;
		}
		pchTime++;
	}

	// Skip whitespace
	while (*pchTime && isspace((unsigned char)*pchTime))
		pchTime++;

	// Check for AM
	if (toupper(*pchTime) == 'A' || toupper(*pchTime) == 'P')
	{
		// Skip . in A.M.
		chMBuf = toupper(*pchTime);
		pchTime++;
		while (*pchTime && strchr(" \r\n\t.", *pchTime))
			pchTime++;
		if (toupper(*pchTime) != 'M')
			chMBuf = '\0';
	}

	pchTimeBuf = achTimeBuf;
	if (!*pchTimeBuf)
	{
		return 0;
	}

	// Start parsing numbers
	switch (*pchTimeBuf)
	{
	case '0':
		if (!*(pchTimeBuf+1))
			return 0;
		iHour = *(pchTimeBuf+1) - '0';
		pchTimeBuf += 2;
		break;
	case '1':
		pchTimeBuf++;
		if (*pchTimeBuf)
		{
			iHour = 10 + *pchTimeBuf - '0';
			pchTimeBuf++;
			if (iHour > 12)
				chMBuf = '\0';
		}
		else
		{
			iHour = 1;
		}
		break;
	case '2':
		pchTimeBuf++;
		if (*pchTimeBuf && *pchTimeBuf < '4')
		{
			iHour = 10 + *pchTimeBuf - '0';
			pchTimeBuf++;
			chMBuf = '\0';
		}
		else
		{
			iHour = 2;
		}
		break;
	default:
		iHour = *pchTimeBuf - '0';
		pchTimeBuf++;
		break;
	}

	if (*pchTimeBuf)
	{
		iMin = *pchTimeBuf - '0';
		pchTimeBuf++;
		if (iMin < 6 && *pchTimeBuf)
		{
			iMin = iMin * 10 + *pchTimeBuf - '0';
			pchTimeBuf++;
		}
	}

	if (*pchTimeBuf)
	{
		iSec = *pchTimeBuf - '0';
		pchTimeBuf++;
		if (iSec < 6 && *pchTimeBuf)
		{
			iSec = iSec * 10 + *pchTimeBuf - '0';
			pchTimeBuf++;
		}
	}

	if (iHour > 0 && iHour <= 12)
	{
		if (iHour == 12)
			iHour = 0;

		if (chMBuf == 'P')
		{
			iHour += 12;
		}
		else if (!chMBuf)
		{
			// Use same AM/PM as current time
			struct tm tmNow;
			timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &tmNow);
			if (tmNow.tm_hour >= 12)
				iHour += 12;
		}
	}

	return iSec + iMin * 60 + iHour * 3600;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsProductionEditMode");
bool exprIsProductionEditMode( void )
{
	return isProductionEditMode();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsDevelopmentMode");
bool exprIsDevelopmentMode( void )
{
	return isDevelopmentMode();
}

//if fDeltaTime is less than zero, then a static headshot is used
//otherwise this will generate an animated headshot
SA_RET_OP_VALID BasicTexture* gclHeadshotFromCostume(	SA_PARAM_OP_VALID const char* pchHeadshotStyleDef,
													SA_PARAM_OP_VALID const PlayerCostume *pHeadshotCostume,	// Costume pointer
													SA_PARAM_OP_VALID const char* pchCostume,				// Name of costume reference (used if costume ptr is NULL)
													F32 fWidth,
													F32 fHeight,
													HeadshotNotifyBytesF notifyBytesF, UserData notifyBytesData )
{
	HeadshotStyleDef* pStyleDef = pchHeadshotStyleDef ? contact_HeadshotStyleDefFromName(pchHeadshotStyleDef) : contact_HeadshotStyleDefFromName("HeadshotStyle_Default");
	PlayerCostume *pCostume = pHeadshotCostume ? pHeadshotCostume : RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostume);
	WLCostume *pWLCostume = NULL;
	BasicTexture* pBackground = NULL; 
	DynBitFieldGroup bfg = { 0 };
	const char* pchSky = NULL;
	const char* pchFrame = NULL;
	const char* pchStance = NULL;
	Color bgColor = ColorTransparent;
	F32 fFOVy = -1;
	const char *pchAnimBits = "HEADSHOT IDLE NOLOD";

	if (pStyleDef)
	{	
		if (pStyleDef->bUseBackgroundOnly)
			return NULL;

		if (pStyleDef->pchBackground)
			pBackground = texLoadBasic(pStyleDef->pchBackground, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);
		if (!pStyleDef->bIgnoreFrame)
			pchFrame = pStyleDef->pchFrame && pStyleDef->pchFrame[0] ? pStyleDef->pchFrame : "Status";
		if (pStyleDef->pchAnimBits && pStyleDef->pchAnimBits[0])
			pchAnimBits = pStyleDef->pchAnimBits;
		pchSky = pStyleDef->pchSky;
		pchStance = pStyleDef->pchStance;
		bgColor = colorFromRGBA(pStyleDef->uiBackgroundColor);
		fFOVy = contact_HeadshotStyleDefGetFOV(pStyleDef, -1);
	}

	dynBitFieldGroupAddBits(&bfg, pchAnimBits, true);

	if (pCostume)
	{
		char buf[260];
		sprintf(buf, "Headshot_%s", pCostume->pcName);

		// Check if override is already in the dictionary
		pWLCostume = RefSystem_ReferentFromString("Costume", buf);

		if (!pWLCostume)
		{
			WLCostume** eaSubCostumes = NULL;

			// Create the world layer costume
			pWLCostume = costumeGenerate_CreateWLCostume(pCostume, NULL, NULL, NULL, 0, NULL, NULL, "Headshot_", 0, 0, false, &eaSubCostumes);
			FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume)
				wlCostumePushSubCostume(pSubCostume, pWLCostume);
			FOR_EACH_END;

			// Put costume into the dictionary once it is complete, else destroy and try again next time
			if (pWLCostume->bComplete)
			{
				pWLCostume->pcName = allocAddString(buf);
				wlCostumeAddToDictionary(pWLCostume, buf);
			}
			else
			{
				StructDestroy(parse_WLCostume, pWLCostume);
				pWLCostume = NULL;
			}
		}
	}

	if (pWLCostume && pWLCostume->bComplete)
	{
		S32 iHeight = round(fHeight);
		S32 iWidth = round(fWidth);
		char textureNameBuffer[512];
		BasicTexture *pTexture;

		sprintf(textureNameBuffer, "%s+%s", pWLCostume->pcName, pchHeadshotStyleDef);

		pTexture = gfxHeadshotCaptureCostume(textureNameBuffer, iWidth, iHeight, pWLCostume, pBackground, pchFrame, bgColor, false, &bfg, NULL, pchStance, fFOVy, pchSky, NULL, notifyBytesF, notifyBytesData);
		gfxHeadshotFlagForUI(pTexture);
		gfxHeadshotRaisePriority(pTexture);
		return pTexture;
	}

	return NULL;
}

#define MAP_NAME_REQUEST_TIMEOUT 60
const char* gclRequestMapDisplayName(const char* pchMapNameMsgKey)
{
	static ClientMapNameRequestInfo** s_eaRequestInfo = NULL;
	S32 i, iFoundIndex = -1;
	U32 uCurrentTime = timeSecondsSince2000();
	const char* pchResult;

	for (i = eaSize(&s_eaRequestInfo)-1; i >= 0; i--)
	{
		ClientMapNameRequestInfo* pRequest = s_eaRequestInfo[i];
		if (iFoundIndex < 0 && stricmp(REF_STRING_FROM_HANDLE(pRequest->hMessage), pchMapNameMsgKey)==0)
		{
			pRequest->uLastRequestTime = uCurrentTime;
			iFoundIndex = i;
		}
		else if (pRequest->uLastRequestTime + MAP_NAME_REQUEST_TIMEOUT < uCurrentTime)
		{
			StructDestroy(parse_ClientMapNameRequestInfo, eaRemove(&s_eaRequestInfo, i));
		}
	}
	if (pchResult = TranslateMessageKey(pchMapNameMsgKey))
	{
		return pchResult;
	}
	if (iFoundIndex < 0)
	{
		ClientMapNameRequestInfo* pInfo = StructCreate(parse_ClientMapNameRequestInfo);
		SET_HANDLE_FROM_STRING("Message", pchMapNameMsgKey, pInfo->hMessage);
		pInfo->uLastRequestTime = uCurrentTime;
		eaPush(&s_eaRequestInfo, pInfo);
		ServerCmd_gslRequestMapDisplayName(pchMapNameMsgKey);
	}
	return NULL;
}

// Gets the type of Ammo loaded for the power, returns -1 if it has no ammo associated with it
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetAmmoType);
S32 gclExprGetAmmoType(SA_PARAM_OP_VALID Entity *pEnt, const char *pchPowerDef)
{
	PowerDef *pDef = powerdef_Find(pchPowerDef);
	if (pEnt && pDef)
	{
		// FIXME: Look at the power to figure out what type of Ammo to use.
		if (strstri(pDef->pchName, "Revolver_Activate"))
		{
			return exprEntGetAttrib(pEnt, "XTypeRevolver");
		}
	}
	return -1;
}

// Gets the amount of the Ammo that the power has, returns -1 if it has no ammo associated with it
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetAmmo);
S32 gclExprGetAmmo(SA_PARAM_OP_VALID Entity *pEnt, const char *pchPowerDef)
{
	PowerDef *pDef = powerdef_Find(pchPowerDef);
	if (pEnt && pDef)
	{
		// FIXME: Look at the power to figure out what type of Ammo to use.
		if (strstri(pDef->pchName, "Revolver_Activate"))
		{
			return exprEntGetAttrib(pEnt, "XGunRevolver");
		}
	}
	return -1;
}

// Gets the maximum amount of Ammo that the power can have, returns -1 if it has no ammo associated with it
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetAmmoMax);
S32 gclExprGetAmmoMax(SA_PARAM_OP_VALID Entity *pEnt, const char *pchPowerDef)
{
	PowerDef *pDef = powerdef_Find(pchPowerDef);
	if (pEnt && pDef)
	{
		// FIXME: Look at the power to figure out what type of Ammo to use.
		if (strstri(pDef->pchName, "Revolver_Activate"))
		{
			return exprEntGetAttrib(pEnt, "XGunRevolverMax");
		}
	}
	return -1;
}

void gclUISave(Entity* pEnt, const char* pchFilename)
{
	if (SAFE_MEMBER2(pEnt, pPlayer, pUI))
	{
		char achFullFilename[CRYPTIC_MAX_PATH];

		if (!pchFilename)
			pchFilename = UI_SETTINGS_DEFAULT_FILENAME;

		strcpy(achFullFilename, pchFilename);
		if (!isFullPath(achFullFilename))
		{
			char dir[CRYPTIC_MAX_PATH];
			if (isDevelopmentMode())
				sprintf(achFullFilename, "%s/%s", fileLocalDataDir(), pchFilename);
			else
				sprintf(achFullFilename, "%s/%s", getExecutableDir(dir), pchFilename);
		}
	
		ParserWriteTextFile(achFullFilename, parse_PlayerUI, pEnt->pPlayer->pUI, 0, TOK_USEROPTIONBIT_2);
	}
}

void gclUILoad(Entity* pEnt, const char* pchFilename)
{
	if (SAFE_MEMBER2(pEnt, pPlayer, pUI))
	{
		char achFullFilename[CRYPTIC_MAX_PATH];
		PlayerUI UI = {0};

		StructInit(parse_PlayerUI, &UI);	// required due to the fact the ALWAYS_ALLOC fields will be created here

		if (!pchFilename)
			pchFilename = UI_SETTINGS_DEFAULT_FILENAME;

		strcpy(achFullFilename, pchFilename);
		if (!isFullPath(achFullFilename))
		{
			char dir[CRYPTIC_MAX_PATH];
			if (isDevelopmentMode())
				sprintf(achFullFilename, "%s/%s", fileLocalDataDir(), pchFilename);
			else
				sprintf(achFullFilename, "%s/%s", getExecutableDir(dir), pchFilename);
		}

		ParserReadTextFile(achFullFilename, parse_PlayerUI, &UI, 0);
		ServerCmd_gslCmdUpdatePlayerUI(&UI);

		StructDeInit(parse_PlayerUI, &UI);

	}
}

// Loads default UI Windows save file
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(ui_load) ACMD_CATEGORY(Standard);
void gclUICmdLoad(Entity* pEnt)
{
	gclUILoad(pEnt, NULL);
}

// Saves UI layout to default UI Window save file
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(ui_save) ACMD_CATEGORY(Standard);
void gclUICmdSave(Entity* pEnt)
{
	gclUISave(pEnt, NULL);
}

// Loads named UI Windows save file
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(ui_load_file) ACMD_CATEGORY(Standard);
void gclUICmdCmdLoadFile(Entity* pEnt, const char* pchFilename)
{
	gclUILoad(pEnt, pchFilename);
}

// Saves UI layout to named UI Window save file
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(ui_save_file) ACMD_CATEGORY(Standard);
void gclUICmdSaveFile(Entity* pEnt, const char* pchFilename)
{
	gclUISave(pEnt, pchFilename);
}

// Resets the layout, used for when the server updates movable window positions
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME(ui_GenLayersReset) ACMD_ACCESSLEVEL(0);
void gclGenCmdLayersReset()
{
	ui_GenLayersReset();
}

// This will find a PowerTreeDef name that is a given tree type name, and is for a given class.
// If there are multiple PowerTreeDefs that satisfy the condition, it will randomly pick one.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowerTreeNameFindWithTypeAndClass);
const char *gclUIExprPowerTreeNameFindWithTypeAndClass(const char *pchType, const char *pchClass)
{
	S32 i;
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("PowerTreeDef");

	if (pchType && !*pchType)
		pchType = NULL;
	if (pchClass && !*pchClass)
		pchClass = NULL;

	for (i = 0; i < eaSize(&pStruct->ppReferents); i++)
	{
		PowerTreeDef *pPowerTree = pStruct->ppReferents[i];
		const char *pchClassName, *pchTypeName;

		if (!pPowerTree)
			continue;

		pchTypeName = REF_STRING_FROM_HANDLE(pPowerTree->hTreeType);
		pchClassName = REF_STRING_FROM_HANDLE(pPowerTree->hClass);

		// Assert that either both names are defined or both undefined.
		if ((!pchTypeName) != (!pchType) || (!pchClassName) != (!pchClass))
			continue;

		if ((pchType && stricmp(pchTypeName, pchType)) || (pchClass && stricmp(pchClassName, pchClass)))
			continue;

		return pPowerTree->pchName;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayEntityFXOnce);
void PlayEntityFXOnce(SA_PARAM_NN_VALID Entity *pEnt,const char * pchEffectName)
{
	if (!dynFxInfoSelfTerminates(pchEffectName))
	{
		Errorf("PlayEntityFXOnce called with non-terminating FX: %s",pchEffectName);
		return;
	}
	dtAddFx(pEnt->dyn.guidFxMan, pchEffectName, NULL, 0, 0, 0, 0, NULL, eDynFxSource_UI, NULL, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetByName);
SA_RET_OP_VALID PetDef *gclGenExpr_PetGetByName(const char *pchName)
{
	return pchName && *pchName ? RefSystem_ReferentFromString("PetDef", pchName) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetCostume);
const char *gclGenExpr_PetGetCostume(SA_PARAM_OP_VALID PetDef *pPetDef)
{
	CritterDef *pCritter = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;
	return pCritter && eaSize(&pCritter->ppCostume) > 0 ? REF_STRING_FROM_HANDLE(pCritter->ppCostume[0]->hCostumeRef) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetSpecies);
const char *gclGenExpr_PetGetSpecies(SA_PARAM_OP_VALID PetDef *pPetDef)
{
	CritterDef *pCritter = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;

	if (!pCritter)
		return "";

	if (IS_HANDLE_ACTIVE(pCritter->hSpecies))
		return REF_STRING_FROM_HANDLE(pCritter->hSpecies);

	if (eaSize(&pCritter->ppCostume) > 0)
	{
		PlayerCostume *pCostume = GET_REF(pCritter->ppCostume[0]->hCostumeRef);
		if (pCostume && IS_HANDLE_ACTIVE(pCostume->hSpecies))
		{
			return REF_STRING_FROM_HANDLE(pCostume->hSpecies);
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetCostumeList);
void gclGenExpr_PetGetCostumeList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID PetDef *pPetDef)
{
	PlayerCostume ***peaCostumes = ui_GenGetManagedListSafe(pGen, PlayerCostume);
	CritterDef *pCritter = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;
	int i;

	eaClearFast(peaCostumes);

	if (pCritter)
	{
		for (i = 0; i < eaSize(&pCritter->ppCostume); i++)
		{
			CritterCostume *pCritterCostume = pCritter->ppCostume[i];
			if (GET_REF(pCritterCostume->hCostumeRef))
			{
				eaPush(peaCostumes, GET_REF(pCritterCostume->hCostumeRef));
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, peaCostumes, PlayerCostume, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetCostumeListSize);
S32 gclGenExpr_PetGetCostumeListSize(SA_PARAM_OP_VALID PetDef *pPetDef)
{
	CritterDef *pCritter = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;
	return pCritter ? eaSize(&pCritter->ppCostume) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetClass);
SA_RET_OP_VALID CharacterClass *gclGenExpr_PetGetClass(SA_PARAM_OP_VALID PetDef *pPetDef)
{
	return pPetDef ? GET_REF(pPetDef->hClass) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetAlwaysPropSlots);
bool gclGenExpr_PetGetAlwaysPropSlots(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID PetDef *pPetDef, S32 ePropCategory)
{
	AlwaysPropSlotData*** peaData = ui_GenGetManagedListSafe(pGen, AlwaysPropSlotData);
	int i, iCount = 0;

	if (pPetDef)
	{
		for (i = 0; i < eaSize(&pPetDef->ppAlwaysPropSlot); i++)
		{
			AlwaysPropSlotDef *pPropSlotDef = GET_REF(pPetDef->ppAlwaysPropSlot[i]->hPropDef);
			if (pPropSlotDef && (ePropCategory < 0 || pPropSlotDef->eCategory == ePropCategory))
			{
				AlwaysPropSlotData* pData = eaGetStruct(peaData, parse_AlwaysPropSlotData, iCount++);
				SET_HANDLE_FROM_REFERENT("AlwaysPropSlotDef", pPropSlotDef, pData->hDef);
				pData->uiPetID = 0;
				pData->iSlotID = 0;
				pData->iID = 0;
				pData->pRestrictDef = NULL;
				pData->pchClassDisplayName = NULL;
				StructFreeStringSafe(&pData->pchName);
			}
		}
	}

	eaSetSizeStruct(peaData, parse_AlwaysPropSlotData, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, AlwaysPropSlotData, true);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetBagSlots);
S32 gclGenExpr_PetGetBagSlots(ExprContext *pContext, SA_PARAM_OP_VALID PetDef *pPetDef, S32 iBagID)
{
	CharacterClass *pClass = pPetDef ? GET_REF(pPetDef->hClass) : NULL;
	DefaultInventory *pDefaultInventory = pClass ? GET_REF(pClass->hInventorySet) : NULL;

	if (pDefaultInventory)
	{
		InvBagDef *pBagDef = eaIndexedGetUsingInt(&pDefaultInventory->InventoryBags, iBagID);
		if (pBagDef)
		{
			return pBagDef->MaxSlots;
		}
	}

	return 0;
}

// Return the exp level of the entity.  If the entity is one of the player's
// puppets, return the level of player.
S32 gclEnt_GetExpLevel(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (entGetType(pEnt) == GLOBALTYPE_ENTITYSAVEDPET)
	{
		static U32 *s_ea32PlayerPets;
		Entity *pPlayer = entActivePlayerPtr();
		if (pPlayer && pPlayer->pSaved && pPlayer->pSaved->pPuppetMaster)
		{
			S32 i;
			for (i = eaSize(&pPlayer->pSaved->pPuppetMaster->ppPuppets) - 1; i >= 0; i--)
			{
				PuppetEntity *pPuppet = pPlayer->pSaved->pPuppetMaster->ppPuppets[i];
				if (pPuppet->curType == entGetType(pEnt) && pPuppet->curID == entGetContainerID(pEnt))
				{
					return entity_GetSavedExpLevel(pPlayer);
				}
			}
		}
	}
	return entity_GetSavedExpLevel(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetClassBasicAttrib);
F32 gclGenExpr_EntGetClassBasicAttrib(SA_PARAM_OP_VALID Entity *pEnt, const char *pchAttrib)
{
	CharacterClass *pClass = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hClass) : NULL;
	AttribType eAttribType = pchAttrib && *pchAttrib ? StaticDefineIntGetInt(AttribTypeEnum, pchAttrib) : -1;
	F32 fResult = 0;

	if (pClass && eAttribType >= 0)
	{
		S32 iLevel = gclEnt_GetExpLevel(pEnt);
		fResult = class_GetAttribBasic(pClass, eAttribType, iLevel - 1);
		if (eAttribType == kAttribType_HitPointsMax)
		{
			S32 iHpLevel = pClass->iBasicFactBonusHitPointsMaxLevel ? pClass->iBasicFactBonusHitPointsMaxLevel : iLevel;

			// TODO: POWERTABLE_BASICFACTBONUSHITPOINTSMAX from Character_mods.c
			fResult *= (1 + class_powertable_Lookup(pClass, "BasicFactBonusHitPointsMax", iHpLevel - 1));
		}
	}
	else if (pClass && eAttribType < 0 && !stricmp(pchAttrib, "BasicFactBonusHitPointsMaxLevel"))
	{
		fResult = pClass->iBasicFactBonusHitPointsMaxLevel;
	}

	return fResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetClassPowerTablePoints);
F32 gclGenExpr_EntGetClassPowerTablePoints(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, const char *pchTable)
{
	CharacterClass *pClass = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hClass) : NULL;
	F32 fResult = 0;

	if (pClass && pchTable && *pchTable)
	{
		S32 iLevel = gclEnt_GetExpLevel(pEnt);
		fResult = class_powertable_Lookup(pClass, pchTable, iLevel - 1);
	}

	return fResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetClassPowerTablePointsEx);
F32 gclGenExpr_EntGetClassPowerTablePointsEx(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, const char *pchTable, S32 iLevel)
{
	CharacterClass *pClass = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hClass) : NULL;
	F32 fResult = 0;

	if (pClass && pchTable && *pchTable)
	{
		MAX1(iLevel, 1);
		fResult = class_powertable_Lookup(pClass, pchTable, iLevel - 1);
	}

	return fResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetClassBasicAttrib);
F32 gclGenExpr_PetGetClassBaseAttrib(ExprContext *pContext, SA_PARAM_OP_VALID PetDef *pPetDef, const char *pchAttrib, S32 iLevel)
{
	CharacterClass *pClass = pPetDef ? GET_REF(pPetDef->hClass) : NULL;
	AttribType eAttribType = pchAttrib && *pchAttrib ? StaticDefineIntGetInt(AttribTypeEnum, pchAttrib) : -1;
	F32 fResult = 0;

	if (pClass && eAttribType >= 0)
	{
		MAX1(iLevel, 1);
		fResult = class_GetAttribBasic(pClass, eAttribType, iLevel - 1);
		if (eAttribType == kAttribType_HitPointsMax)
		{
			S32 iHpLevel = pClass->iBasicFactBonusHitPointsMaxLevel ? pClass->iBasicFactBonusHitPointsMaxLevel : iLevel;

			// TODO: POWERTABLE_BASICFACTBONUSHITPOINTSMAX from Character_mods.c
			fResult *= (1 + class_powertable_Lookup(pClass, "BasicFactBonusHitPointsMax", iHpLevel - 1));
		}
	}
	else if (pClass && eAttribType < 0 && !stricmp(pchAttrib, "BasicFactBonusHitPointsMaxLevel"))
	{
		fResult = pClass->iBasicFactBonusHitPointsMaxLevel;
	}

	return fResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetClassPowerTablePoints);
F32 gclGenExpr_PetGetClassPowerTablePoints(ExprContext *pContext, SA_PARAM_OP_VALID PetDef *pPetDef, const char *pchTable, S32 iLevel)
{
	CharacterClass *pClass = pPetDef ? GET_REF(pPetDef->hClass) : NULL;
	F32 fResult = 0;

	if (pClass && pchTable && *pchTable)
	{
		MAX1(iLevel, 1);
		fResult = class_powertable_Lookup(pClass, pchTable, iLevel - 1);
	}

	return fResult;
}

static bool PlayerGetUnlockedCostumes(PlayerCostume ***peaCostumes, S32 *piVersion)
{
	static PlayerCostumeRef **s_eaCostumeRefs;
	static Entity *s_pEnt;
	static GameAccountData *s_pAccountData;
	static U32 s_iAccountDataCRC;
	static bool s_bFirstTime;
	static S32 s_iCurrentVersion;

	Entity *pEnt = entActivePlayerPtr();
	GameAccountData *pAccountData = entity_GetGameAccount(pEnt);
	S32 i;
	U32 iAccountDataCRC = s_pAccountData ? StructCRC(parse_GameAccountData, pAccountData) : 0;

	if (!s_bFirstTime || s_pEnt != pEnt || s_pAccountData != pAccountData || s_iAccountDataCRC != iAccountDataCRC)
	{
		eaClearStruct(&s_eaCostumeRefs, parse_PlayerCostumeRef);
		if (pEnt)
		{
			costumeEntity_GetUnlockCostumesRef(pEnt->pSaved->costumeData.eaUnlockedCostumeRefs, pAccountData, pEnt, pEnt, &s_eaCostumeRefs);
		}
		else
		{
			costumeEntity_GetUnlockCostumesRef(NULL, pAccountData, NULL, NULL, &s_eaCostumeRefs);
		}

		s_bFirstTime = true;
		s_pEnt = pEnt;
		s_pAccountData = pAccountData;
		s_iAccountDataCRC = iAccountDataCRC;
		s_iCurrentVersion++;
	}

	eaClearFast(peaCostumes);
	for (i = 0; i < eaSize(&s_eaCostumeRefs); i++)
	{
		PlayerCostume *pCostume = GET_REF(s_eaCostumeRefs[i]->hCostume);
		if (pCostume)
		{
			eaPush(peaCostumes, pCostume);
		}
	}

	if (piVersion && *piVersion != s_iCurrentVersion)
	{
		*piVersion = s_iCurrentVersion;
		return true;
	}

	return eaSize(peaCostumes) != eaSize(&s_eaCostumeRefs);
}

static CostumePreset ***SpeciesGetCostumePresets(const char *pchSpecies)
{
	static PlayerCostume **s_eaUnlockedCostumes;
	static S32 s_iVersion;

	static CostumePreset **s_eaCostumePresets;
	static const char *s_pchCachedSpecies;

	static CostumePreset **s_eaNoPresets;

	SpeciesDef *pSpecies = RefSystem_ReferentFromString(g_hSpeciesDict, pchSpecies);
	bool bChanged = PlayerGetUnlockedCostumes(&s_eaUnlockedCostumes, &s_iVersion);
	bChanged = bChanged || pSpecies && s_pchCachedSpecies != pSpecies->pcName;

	if (bChanged && pSpecies)
	{
		NOCONST(PlayerCostume) s_DummyCostume;
		ZeroStruct(&s_DummyCostume);
		s_DummyCostume.eCostumeType = kPCCostumeType_Player;

		costumeTailor_GetValidPresets(&s_DummyCostume, pSpecies, &s_eaCostumePresets, true, s_eaUnlockedCostumes, false);
		s_pchCachedSpecies = pSpecies->pcName;
	}

	return pSpecies && s_pchCachedSpecies == pSpecies->pcName ? &s_eaCostumePresets : &s_eaNoPresets;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetPetCostumePreset);
const char *gclInvExprItemGetPetCostumePreset(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	PetDef *pPet = pItemDef ? GET_REF(pItemDef->hPetDef) : NULL;
	CritterDef *pCritter = pPet ? GET_REF(pPet->hCritterDef) : NULL;
	PlayerCostume *pActiveCostume = pCritter && eaSize(&pCritter->ppCostume) > 0 ? GET_REF(pCritter->ppCostume[0]->hCostumeRef) : NULL;
	SpeciesDef *pSpecies = pCritter && GET_REF(pCritter->hSpecies) ? GET_REF(pCritter->hSpecies)
						: pActiveCostume && GET_REF(pActiveCostume->hSpecies) ? GET_REF(pActiveCostume->hSpecies)
						: NULL;
	CostumePreset ***peaPresets = pSpecies ? SpeciesGetCostumePresets(pSpecies->pcName) : NULL;
	const char *pchCurrent = pCritter && eaSize(&pCritter->ppCostume) > 0 ? REF_STRING_FROM_HANDLE(pCritter->ppCostume[0]->hCostumeRef) : NULL;
	int i;
	if (!peaPresets)
		return pchCurrent;
	for (i = 0; pchCurrent && i < eaSize(peaPresets); i++)
	{
		CostumePreset *pPreset = (*peaPresets)[i];
		PlayerCostume* pCostume = pPreset ? GET_REF(pPreset->hCostume) : NULL;
		if (pCostume && stricmp(pCostume->pcName, pchCurrent) == 0)
			return pchCurrent;
	}
	return eaSize(peaPresets) > 0 ? REF_STRING_FROM_HANDLE((*peaPresets)[0]->hCostume) : pchCurrent;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PetGetCostumePresets);
void gclUIGenExprPetGetCostumePresets(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID PetDef *pPetDef, const char *pchCurrentName)
{
	static CostumePreset s_Current;
	CritterDef *pCritter = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;

	PlayerCostume *pActiveCostume = pCritter && eaSize(&pCritter->ppCostume) > 0 ? GET_REF(pCritter->ppCostume[0]->hCostumeRef) : NULL;
	SpeciesDef *pSpecies = pCritter && GET_REF(pCritter->hSpecies) ? GET_REF(pCritter->hSpecies)
							: pActiveCostume && GET_REF(pActiveCostume->hSpecies) ? GET_REF(pActiveCostume->hSpecies)
							: NULL;
	const char *pchCurrent = pActiveCostume ? pActiveCostume->pcName
							: pCritter && eaSize(&pCritter->ppCostume) > 0 ? REF_STRING_FROM_HANDLE(pCritter->ppCostume[0]->hCostumeRef)
							: NULL;

	CostumePreset ***peaSpeciesPresets = pSpecies ? SpeciesGetCostumePresets(pSpecies->pcName) : NULL;
	CostumePreset ***peaGenPresets = ui_GenGetManagedListSafe(pGen, CostumePreset);
	int i;

	if (peaSpeciesPresets)
		eaCopy(peaGenPresets, peaSpeciesPresets);
	else
		eaClearFast(peaGenPresets);

	if (peaSpeciesPresets && pchCurrent)
	{
		for (i = eaSize(peaSpeciesPresets) - 1; i >= 0 ; i--)
		{
			CostumePreset *pPreset = (*peaSpeciesPresets)[i];
			PlayerCostume* pCostume = pPreset ? GET_REF(pPreset->hCostume) : NULL;
			if (pCostume && stricmp(pCostume->pcName, pchCurrent) == 0)
				break;
		}

		if (i < 0 && pActiveCostume)
		{
			// Add the current costume to the list
			Message *pCurrent = GET_REF(s_Current.displayNameMsg.hMessage);
			if (!pCurrent || stricmp_safe(pCurrent->pcMessageKey, pchCurrentName))
			{
				if (pchCurrentName && *pchCurrentName)
					SET_HANDLE_FROM_STRING("Message", pchCurrentName, s_Current.displayNameMsg.hMessage);
				else if (IS_HANDLE_ACTIVE(s_Current.displayNameMsg.hMessage))
					REMOVE_HANDLE(s_Current.displayNameMsg.hMessage);
			}

			if (GET_REF(s_Current.hCostume) != pActiveCostume)
				SET_HANDLE_FROM_REFERENT("PlayerCostume", pActiveCostume, s_Current.hCostume);

			eaInsert(peaGenPresets, &s_Current, 0);
		}
		else if (IS_HANDLE_ACTIVE(s_Current.hCostume))
			StructReset(parse_CostumePreset, &s_Current);
	}

	ui_GenSetManagedListSafe(pGen, peaGenPresets, CostumePreset, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpeciesGetCostumePresets);
void gclGenExpr_GenSpeciesGetCostumePresets(SA_PARAM_NN_VALID UIGen *pGen, const char *pchSpecies)
{
	CostumePreset ***peaPresets = ui_GenGetManagedListSafe(pGen, CostumePreset);
	eaCopy(peaPresets, SpeciesGetCostumePresets(pchSpecies));
	ui_GenSetManagedListSafe(pGen, peaPresets, CostumePreset, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpeciesGetCostumePresetDisplayName);
const char* gclGenExpr_GenSpeciesGetCostumePresetDisplayName(const char *pchSpecies, const char *pchCostume)
{
	SpeciesDef *pSpecies = RefSystem_ReferentFromString(g_hSpeciesDict, pchSpecies);
	CostumePreset ***peaPresets = SpeciesGetCostumePresets(pchSpecies);
	int i;
	for (i = 0; i < eaSize(peaPresets); i++)
	{
		CostumePreset *pPreset = (*peaPresets)[i];
		PlayerCostume* pCostume = pPreset ? GET_REF(pPreset->hCostume) : NULL;
		if (pCostume && stricmp(pCostume->pcName, pchCostume) == 0)
			return pPreset ? TranslateDisplayMessage(pPreset->displayNameMsg) : "";
	}
	return "";
} 

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SpeciesGetCostumePresetsSize);
S32 gclGenExpr_GenSpeciesGetCostumePresetsSize(const char *pchSpecies)
{
	return eaSize(SpeciesGetCostumePresets(pchSpecies));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenClassGetBasicAttribValueByName);
F32 gclGenExpr_GenClassGetBasicAttribValueByName(SA_PARAM_OP_VALID CharacterClass *pClass, SA_PARAM_NN_VALID const char *pchAttribName, S32 iLevel)
{
	S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,pchAttribName);
	CharacterClassAttrib *attrib = NULL;

	if (!pClass || eAttrib < 0 || !IS_NORMAL_ATTRIB(eAttrib))
		return 0.f;
	
	return class_GetAttribBasic(pClass, eAttrib, iLevel);

	if (attrib && attrib->pfBasic)
	{
		return eafGet(&attrib->pfBasic, iLevel);
	}

	return 0.f;
}

// Given a class, sets the UIGen model with powerDefs from the class that have the given category tags
// the category tags can be a list of delimted strings by " ,\r\n\t"
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenClassGetPowerDefsByCategoryTags);
void gclGenExpr_GenClassGetPowerDefsByCategoryTags(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID CharacterClass *pClass, 
													SA_PARAM_NN_VALID const char *pchCategoryTags, S32 bTagOrdered)
{
	S32 *eaiCategoryTags = NULL;
	PowerDef **s_ppPowerDefList = NULL;

	eaClear(&s_ppPowerDefList);
	if (!pClass)
	{
		ui_GenSetManagedListSafe(pGen, &s_ppPowerDefList, PowerDef, false);
		return;
	}

	// get a list of category tags we'll want to match against
	{
		char *pchTag;
		char *pchContext;
		strdup_alloca(pchContext, pchCategoryTags);
		while ((pchTag = strtok_r(NULL, " ,\r\n\t", &pchContext)) != NULL)
		{
			S32 iCatTag = StaticDefineIntGetInt(PowerCategoriesEnum, pchTag);
			if (iCatTag >= 0)
				eaiPush(&eaiCategoryTags, iCatTag);
		}

		if (eaiSize(&eaiCategoryTags) == 0)
		{
			ui_GenSetManagedListSafe(pGen, &s_ppPowerDefList, PowerDef, false);
			return;
		}
	}

	if (bTagOrdered)
	{	// get the powers in the order of the categories passed in
		S32 s = eaiSize(&eaiCategoryTags), i;
		for (i = 0; i < s; ++i )
		{
			FOR_EACH_IN_EARRAY(pClass->ppPowers, CharacterClassPower, pClassPower)
			{
				PowerDef *pDef = GET_REF(pClassPower->hdef);
				if (pDef && eaiFind(&pDef->piCategories, eaiCategoryTags[i]) >= 0)
				{
					if (eaFind(&s_ppPowerDefList, pDef) == -1)
					{
						eaPush(&s_ppPowerDefList, pDef);
					}
				}
			}
			FOR_EACH_END
		}
	}
	else
	{
		FOR_EACH_IN_EARRAY(pClass->ppPowers, CharacterClassPower, pClassPower)
		{
			PowerDef *pDef = GET_REF(pClassPower->hdef);
			if (pDef && pDef->piCategories)
			{
				S32 i = eaiSize(&eaiCategoryTags) - 1;
				do 
				{
					if (eaiFind(&pDef->piCategories, eaiCategoryTags[i]) >= 0)
					{
						eaPush(&s_ppPowerDefList, pDef);
						break;
					}
				} while (--i >= 0);
			}
		}
		FOR_EACH_END
	}
	
	
	
	ui_GenSetManagedListSafe(pGen, &s_ppPowerDefList, PowerDef, false);
}

static const char PageBreakTag[] = "<pagebr>";

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TipPageCount);
S32 gclGenExpr_TipPageCount(ExprContext *pContext, const char *pchDisplayString, const char *pchLogicalString)
{
	S32 iCount = 1;
	const char *pch = pchDisplayString;
	size_t pageBreakLen = strlen(PageBreakTag);

	while ((pch = strstri(pch, PageBreakTag)))
	{
		iCount++;
		pch += pageBreakLen;
	}

	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TipGetPage);
const char *gclGenExpr_TipGetPage(ExprContext *pContext, S32 iPage, const char *pchDisplayString, const char *pchLogicalString)
{
	S32 iCount = 1;
	char *pch;
	char *pchStart = NULL;
	size_t pageBreakLen = strlen(PageBreakTag);

	strdup_alloca(pchStart, pchDisplayString);
	pch = pchStart;
	ANALYSIS_ASSUME(pch); // cryptic code is defined to crash when alloc fails, so this will succeed

	while (iCount < iPage && (pch = strstri(pch, PageBreakTag)))
	{
		iCount++;
		pch += pageBreakLen;
	}

	pchStart = pch;
	if (pch)
	{
		ANALYSIS_ASSUME(pch);
		if (pch = strstri(pch, PageBreakTag))
		{
			*pch = '\0';
		}
	}

	return exprContextAllocString(pContext, pchStart);
}

typedef struct PlayTimeCache {
	U32 uPlayerID;
	U32 iPlayTimeReference;
	F32 fPlayTime;
} PlayTimeCache;
static PlayTimeCache s_CurrentPlayTime;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclUIGen_SetCurrentPlayTime(U32 uPlayerID, U32 iLastPlayedTime, F32 fTotalPlayTime)
{
	s_CurrentPlayTime.uPlayerID = uPlayerID;
	s_CurrentPlayTime.iPlayTimeReference = iLastPlayedTime;
	s_CurrentPlayTime.fPlayTime = fTotalPlayTime;
}

// Return the estimated total gameplay time for the character.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterTotalGameplayTime");
U32 gclUIGen_CharacterGetTotalGameplayTime(void)
{
	if (s_CurrentPlayTime.uPlayerID && s_CurrentPlayTime.iPlayTimeReference)
	{
		S32 iDelta = timeServerSecondsSince2000() - s_CurrentPlayTime.iPlayTimeReference;
		if (iDelta < 0)
			iDelta = 0;
		return (U32)s_CurrentPlayTime.fPlayTime + iDelta;
	}
	return 0;
}

// Return the estimated total gameplay time for all the characters in the account.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AccountTotalGameplayTime");
U32 gclUIGen_AccountGetTotalGameplayTime(void)
{
	Entity *pEnt = entActivePlayerPtr();
	U32 uTotalTime = 0;

	if (pEnt)
	{
        U32 currentTime = timeServerSecondsSince2000();
        GameAccountData *gameAccountData = entity_GetGameAccount(pEnt);

        // Estimate total played time by adding the total time at login to the time since login.
        uTotalTime = gameAccountData->uTotalPlayedTime_AccountServer + ( currentTime - gameAccountData->uLastRefreshTime );
	}

	return uTotalTime;
}

// Return the amount of time required for the permission to be granted.
// This will return 0 for a permission that should be available, <0 for a permission that doesn't have a time to unlock, otherwise the approximate number of seconds before the permission is granted.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GameTimeUntilPermission");
S32 gclUIGen_TimeUntilPermission(const char *pchPermission)
{
	static const char *s_pchTokenText;
	static GameTokenText *pToken;
	Entity *pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	S32 i, j;
	S32 iTime = -1;
	U32 uNow, uAccountPlayTime;

	if (GamePermission_EntHasToken(pEnt, pchPermission))
	{
		return 0;
	}

	if (s_pchTokenText != pchPermission)
	{
		StructDestroySafe(parse_GameTokenText, &pToken);
		pToken = gamePermission_TokenStructFromString(pchPermission);
		s_pchTokenText = pchPermission;
	}

	if (!pToken)
	{
		return -1;
	}

	uNow = timeServerSecondsSince2000();
	uAccountPlayTime = gclUIGen_AccountGetTotalGameplayTime();

	for (i = eaSize(&g_GamePermissions.eaTimedTokenList) - 1; i >= 0; i--)
	{
		GamePermissionTimed *pTimedPermissions = g_GamePermissions.eaTimedTokenList[i];
		U32 iStartTime = uNow < pTimedPermissions->iStartSeconds ? uNow - pTimedPermissions->iStartSeconds : 0;
		U32 iDaysSubscribed = pTimedPermissions->iDaysSubscribed > 0 && pExtract && pExtract->iDaysSubscribed < pTimedPermissions->iDaysSubscribed ? pTimedPermissions->iDaysSubscribed - pExtract->iDaysSubscribed : 0;
		U32 iSecondsPlayed = pTimedPermissions->iDaysSubscribed == 0 && uAccountPlayTime < HOURS(pTimedPermissions->iHours) ? HOURS(pTimedPermissions->iHours) - uAccountPlayTime : 0;
		S32 iTimeToUnlock = max(iStartTime, max(DAYS(iDaysSubscribed), iSecondsPlayed));

		if (!pExtract && pTimedPermissions->iDaysSubscribed > 0)
		{
			continue;
		}

		for (j = eaSize(&pTimedPermissions->eaPermissions) - 1; j >= 0; j--)
		{
			if (GamePermissions_PermissionGivesToken(pTimedPermissions->eaPermissions[j], pToken))
			{
				if (iTime < 0 || iTimeToUnlock < iTime)
				{
					iTime = iTimeToUnlock;
				}
			}
		}
	}

	return iTime;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayerHasGamePermission");
bool gclUIGen_HasGamePermission(const char *pchPermission)
{
	return GamePermission_EntHasToken(entActivePlayerPtr(), pchPermission);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetAccessLevel");
S32 gclGenExpr_EntGetAccessLevel(SA_PARAM_OP_VALID Entity *pEntity)
{
	return entGetAccessLevel(pEntity);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetAlwaysPropSlot");
S32 gclGenExpr_EntGetAlwaysPropSlot(SA_PARAM_OP_VALID Entity *pSlotOwner, SA_PARAM_OP_VALID Entity *pSlottedEnt)
{
	if (pSlotOwner && pSlotOwner->pSaved && pSlottedEnt && entGetType(pSlottedEnt) == GLOBALTYPE_ENTITYSAVEDPET)
	{
		S32 i;
		U32 uPetID = 0;
		for (i = eaSize(&pSlotOwner->pSaved->ppOwnedContainers) - 1; i >= 0; i--)
		{
			if (pSlotOwner->pSaved->ppOwnedContainers[i]->conID == entGetContainerID(pSlottedEnt))
			{
				uPetID = pSlotOwner->pSaved->ppOwnedContainers[i]->uiPetID;
				break;
			}
		}
		for (i = eaSize(&pSlotOwner->pSaved->ppAlwaysPropSlots) - 1; i >= 0; i--)
		{
			if (pSlotOwner->pSaved->ppAlwaysPropSlots[i]->iPetID == uPetID)
				return i;
		}
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetValidShipNamePrefixes");
void gclEntGetValidShipNamePrefixes(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	AllegianceDef *pDef = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	AllegianceDef *pSubDef = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;
	GameAccountData *pGameAccount = entity_GetGameAccount(pEnt);
	UIGenVarTypeGlob ***peaGlob = ui_GenGetManagedListSafe(pGen, UIGenVarTypeGlob);
	S32 i, iSize = 0;

	if (pDef)
	{
		for (i = 0; i < eaSize(&pDef->eaNamePrefixes); i++)
		{
			if (allegiance_CanUseNamePrefix(pDef->eaNamePrefixes[i], NULL, pGameAccount))
			{
				// Pick the first free prefix
				UIGenVarTypeGlob *pGlob = eaGetStruct(peaGlob, parse_UIGenVarTypeGlob, iSize++);
				estrClear(&pGlob->pchString);
				estrAppend2(&pGlob->pchString, pDef->eaNamePrefixes[i]->pchPrefix);
			}
		}
	}

	if (pSubDef)
	{
		for (i = 0; i < eaSize(&pSubDef->eaNamePrefixes); i++)
		{
			if (allegiance_CanUseNamePrefix(pSubDef->eaNamePrefixes[i], NULL, pGameAccount))
			{
				// Pick the first free prefix
				UIGenVarTypeGlob *pGlob = eaGetStruct(peaGlob, parse_UIGenVarTypeGlob, iSize++);
				estrClear(&pGlob->pchString);
				estrAppend2(&pGlob->pchString, pSubDef->eaNamePrefixes[i]->pchPrefix);
			}
		}	
	}

	eaSetSizeStruct(peaGlob, parse_UIGenVarTypeGlob, iSize);
	ui_GenSetManagedListSafe(pGen, peaGlob, UIGenVarTypeGlob, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetCurrentShipRenamePrefix");
const char *gclEntGetCurrentShipRenamePrefix(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pRenameEnt)
{
	AllegianceDef *pDef = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	AllegianceDef *pSubDef = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;
	GameAccountData *pGameAccount = entity_GetGameAccount(pEnt);
	const char *pchName = pRenameEnt ? entGetLocalName(pRenameEnt) : NULL;
	const char *pchExpectedPrefix = NULL;

	if (pchName)
		allegiance_GetNamePrefix(pDef, pSubDef, pRenameEnt, pGameAccount, pchName, &pchExpectedPrefix);

	return pchExpectedPrefix;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetValidShipRenamePrefixes");
void gclEntGetValidShipRenamePrefixes(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pRenameEnt)
{
	AllegianceDef *pDef = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	AllegianceDef *pSubDef = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;
	GameAccountData *pGameAccount = entity_GetGameAccount(pEnt);
	UIGenVarTypeGlob ***peaGlob = ui_GenGetManagedListSafe(pGen, UIGenVarTypeGlob);
	S32 i, iSize = 0;

	if (pDef)
	{
		for (i = 0; i < eaSize(&pDef->eaNamePrefixes); i++)
		{
			UIGenVarTypeGlob *pGlob;

			if (!allegiance_CanUseNamePrefix(pDef->eaNamePrefixes[i], pRenameEnt, pGameAccount))
				continue;

			pGlob = eaGetStruct(peaGlob, parse_UIGenVarTypeGlob, iSize++);
			estrCopy2(&pGlob->pchString, pDef->eaNamePrefixes[i]->pchPrefix);
		}
	}

	if (pSubDef)
	{
		for (i = 0; i < eaSize(&pSubDef->eaNamePrefixes); i++)
		{
			UIGenVarTypeGlob *pGlob;

			if (!allegiance_CanUseNamePrefix(pSubDef->eaNamePrefixes[i], pRenameEnt, pGameAccount))
				continue;

			pGlob = eaGetStruct(peaGlob, parse_UIGenVarTypeGlob, iSize++);
			estrCopy2(&pGlob->pchString, pSubDef->eaNamePrefixes[i]->pchPrefix);
		}
	}

	eaSetSizeStruct(peaGlob, parse_UIGenVarTypeGlob, iSize);
	ui_GenSetManagedListSafe(pGen, peaGlob, UIGenVarTypeGlob, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetValidShipSubNamePrefixes");
void gclEntGetValidRegistryNamePrefixes(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	AllegianceDef *pDef = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	AllegianceDef *pSubDef = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;
	GameAccountData *pGameAccount = entity_GetGameAccount(pEnt);
	UIGenVarTypeGlob ***peaGlob = ui_GenGetManagedListSafe(pGen, UIGenVarTypeGlob);
	S32 i, iSize = 0;

	if (pDef)
	{
		for (i = 0; i < eaSize(&pDef->eaNamePrefixes); i++)
		{
			if (allegiance_CanUseNamePrefix(pDef->eaNamePrefixes[i], NULL, pGameAccount))
			{
				// Pick the first free prefix
				UIGenVarTypeGlob *pGlob = eaGetStruct(peaGlob, parse_UIGenVarTypeGlob, iSize++);
				estrClear(&pGlob->pchString);
				estrAppend2(&pGlob->pchString, pDef->eaNamePrefixes[i]->pchPrefix);
			}
		}
	}

	if (pSubDef)
	{
		for (i = 0; i < eaSize(&pSubDef->eaNamePrefixes); i++)
		{
			if (allegiance_CanUseNamePrefix(pSubDef->eaNamePrefixes[i], NULL, pGameAccount))
			{
				// Pick the first free prefix
				UIGenVarTypeGlob *pGlob = eaGetStruct(peaGlob, parse_UIGenVarTypeGlob, iSize++);
				estrClear(&pGlob->pchString);
				estrAppend2(&pGlob->pchString, pSubDef->eaNamePrefixes[i]->pchPrefix);
			}
		}	
	}

	eaSetSizeStruct(peaGlob, parse_UIGenVarTypeGlob, iSize);
	ui_GenSetManagedListSafe(pGen, peaGlob, UIGenVarTypeGlob, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetCurrentShipSubRenamePrefix");
const char *gclEntGetCurrentShipSubRenamePrefix(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pRenameEnt)
{
	AllegianceDef *pDef = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	AllegianceDef *pSubDef = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;
	GameAccountData *pGameAccount = entity_GetGameAccount(pEnt);
	const char *pchName = pRenameEnt ? entGetLocalSubName(pRenameEnt) : NULL;
	const char *pchExpectedPrefix = NULL;

	if (pchName)
		allegiance_GetSubNamePrefix(pDef, pSubDef, pRenameEnt, pGameAccount, pchName, &pchExpectedPrefix);

	return pchExpectedPrefix;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetValidShipSubRenamePrefixes");
void gclEntGetValidShipSubRenamePrefixes(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pRenameEnt)
{
	AllegianceDef *pDef = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	AllegianceDef *pSubDef = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;
	GameAccountData *pGameAccount = entity_GetGameAccount(pEnt);
	UIGenVarTypeGlob ***peaGlob = ui_GenGetManagedListSafe(pGen, UIGenVarTypeGlob);
	S32 i, iSize = 0;

	if (pDef)
	{
		for (i = 0; i < eaSize(&pDef->eaSubNamePrefixes); i++)
		{
			UIGenVarTypeGlob *pGlob;

			if (!allegiance_CanUseNamePrefix(pDef->eaSubNamePrefixes[i], pRenameEnt, pGameAccount))
				continue;

			pGlob = eaGetStruct(peaGlob, parse_UIGenVarTypeGlob, iSize++);
			estrCopy2(&pGlob->pchString, pDef->eaSubNamePrefixes[i]->pchPrefix);
		}
	}

	if (pSubDef)
	{
		for (i = 0; i < eaSize(&pSubDef->eaSubNamePrefixes); i++)
		{
			UIGenVarTypeGlob *pGlob;

			if (!allegiance_CanUseNamePrefix(pSubDef->eaSubNamePrefixes[i], pRenameEnt, pGameAccount))
				continue;

			pGlob = eaGetStruct(peaGlob, parse_UIGenVarTypeGlob, iSize++);
			estrCopy2(&pGlob->pchString, pSubDef->eaSubNamePrefixes[i]->pchPrefix);
		}
	}

	eaSetSizeStruct(peaGlob, parse_UIGenVarTypeGlob, iSize);
	ui_GenSetManagedListSafe(pGen, peaGlob, UIGenVarTypeGlob, true);
}

// Get the Gimme version of the game
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetGimmeVersion");
S32 gclExprGetGimmeVersion(void)
{
	static S32 s_iGimmeVersion = -1;
	const char *pchShortName, *pchVersion, *pch;
	S32 iShortNameLen;

	if (s_iGimmeVersion != -1)
		return s_iGimmeVersion;

	pchVersion = GetUsefulVersionString();
	if (!pchVersion)
		return 0;

	pchShortName = GetShortProductName();
	iShortNameLen = (S32)strlen(pchShortName);
	pch = strstr(pchVersion, pchShortName);
	if (pch && (pch[iShortNameLen] == '.' || pch[iShortNameLen] == ' ') && isdigit(pch[iShortNameLen + 1] & 0xff))
	{
		// e.g. "ST.<branch>.<date>.<incr>" or "<svn> (ST <branch>)"
		s_iGimmeVersion = atoi(&pch[iShortNameLen + 1]);
	}

	return MAX(s_iGimmeVersion, 0);
}

// Get the most recent "release" version based on a message key pattern
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetReleaseVersionEx");
S32 gclExprGetReleaseVersionEx(const char *pchMessageKeyFormat)
{
	S32 iVersion = gclExprGetGimmeVersion();
	char *estrMessageKey = NULL;

	estrStackCreate(&estrMessageKey);
	while (iVersion >= 0)
	{
		FormatGameString(&estrMessageKey, pchMessageKeyFormat, STRFMT_INT("Branch", iVersion), STRFMT_END);
		if (RefSystem_ReferentFromString("Message", estrMessageKey))
			break;
		estrClear(&estrMessageKey);
		--iVersion;
	}
	estrDestroy(&estrMessageKey);

	return iVersion;
}

// Get the previous "release" version based on a message key pattern
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPreviousReleaseVersionEx");
S32 gclExprGetPreviousReleaseVersionEx(const char *pchMessageKeyFormat, S32 iVersion)
{
	S32 iGimmeVersion = gclExprGetGimmeVersion();
	char *estrMessageKey = NULL;
	bool bFound = false;

	--iVersion;
	if (iVersion >= iGimmeVersion)
		return gclExprGetReleaseVersionEx(pchMessageKeyFormat);

	estrStackCreate(&estrMessageKey);
	while (iVersion >= 0)
	{
		FormatGameString(&estrMessageKey, pchMessageKeyFormat, STRFMT_INT("Branch", iVersion), STRFMT_END);
		if (RefSystem_ReferentFromString("Message", estrMessageKey))
			break;
		estrClear(&estrMessageKey);
		--iVersion;
	}
	estrDestroy(&estrMessageKey);

	return iVersion >= 0 ? iVersion : -1;
}

// Get the next "release" version based on a message key pattern
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetNextReleaseVersionEx");
S32 gclExprGetNextReleaseVersionEx(const char *pchMessageKeyFormat, S32 iVersion)
{
	S32 iGimmeVersion = gclExprGetGimmeVersion();
	char *estrMessageKey = NULL;

	++iVersion;

	estrStackCreate(&estrMessageKey);
	while (iVersion <= iGimmeVersion)
	{
		FormatGameString(&estrMessageKey, pchMessageKeyFormat, STRFMT_INT("Branch", iVersion), STRFMT_END);
		if (RefSystem_ReferentFromString("Message", estrMessageKey))
			break;
		estrClear(&estrMessageKey);
		++iVersion;
	}
	estrDestroy(&estrMessageKey);

	return iVersion <= iGimmeVersion ? iVersion : -1;
}

// Get the most recent "release" version based on a message key pattern "Game_Version_{Branch}"
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetReleaseVersion");
S32 gclExprGetReleaseVersion(void)
{
	return gclExprGetReleaseVersionEx("Game_Version_{Branch}");
}

// Get the previous "release" version from the given version based on a message key pattern "Game_Version_{Branch}"
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPreviousReleaseVersion");
S32 gclExprGetPreviousReleaseVersion(S32 iVersion)
{
	return gclExprGetPreviousReleaseVersionEx("Game_Version_{Branch}", iVersion);
}

// Get the next "release" version from the given version based on a message key pattern "Game_Version_{Branch}"
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetNextReleaseVersion");
S32 gclExprGetNextReleaseVersion(S32 iVersion)
{
	return gclExprGetNextReleaseVersionEx("Game_Version_{Branch}", iVersion);
}

// Return true if the given texture exists, false otherwise.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSoundExists);
bool gclExprGenSoundExists(ExprContext *pContext, const char *pchName)
{
	return sndEventExists(pchName);
}

#include "gclUIGen_h_ast.c"
#include "Autogen/gclUIGen_c_ast.c"
