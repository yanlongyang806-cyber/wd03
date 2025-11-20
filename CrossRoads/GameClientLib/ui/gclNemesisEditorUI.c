#include "gclCostumeUI.h"
#include "gclCostumeUIState.h"

#include "Player.h"
#include "gclEntity.h"
#include "UIGen.h"
#include "CharacterCreationUI.h"
#include "StringUtil.h"
#include "NotifyCommon.h"
#include "SimpleParser.h"
#include "Expression.h"
#include "WLCostume.h"
#include "CostumeCommonLoad.h"
#include "GfxHeadshot.h"
#include "GraphicsLib.h"
#include "dynSequencer.h"
#include "CostumeCommonGenerate.h"
#include "GameClientLib.h"

#include "AutoGen/nemesis_common_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/gclCostumeUIState_h_ast.h"
#include "AutoGen/gclNemesisEditorUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct NemesisNameData
{
	const char *name;
} NemesisNameData;

extern NemesisPowerSetList g_NemesisPowerSetList;
extern NemesisMinionPowerSetList g_NemesisMinionPowerSetList;
extern NemesisMinionCostumeSetList g_NemesisMinionCostumeSetList;

static char *s_pchPlayerMakeNemesisResult = NULL;
static bool s_bPlayerMakeNemesisSuccessful = false;

// Get the cost of the current costume change and Nemesis respec
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetCost");
S32 NemesisEditor_GetCost(void)
{
	Entity *pEnt = entActivePlayerPtr();
	S32 cost = g_CostumeEditState.currentCost;
	if (g_CostumeEditState.pStartNemesis && pEnt){
		cost += nemesis_trh_GetCostToChange((const NOCONST(Nemesis)*)g_CostumeEditState.pStartNemesis, entity_GetSavedExpLevel(pEnt), g_CostumeEditState.personality, g_CostumeEditState.pchNemesisPowerSet, g_CostumeEditState.pchMinionPowerSet, g_CostumeEditState.pchMinionCostumeSet, g_CostumeEditState.fNemesisPowerHue, g_CostumeEditState.fMinionPowerHue);
	}
	return cost;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetNonPrimaryNemesisList");
void NemesisEditor_GetNonPrimaryNemesisList(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity *pEnt = entActivePlayerPtr();
	U32 iPrimaryNemesisID = player_GetPrimaryNemesisID(pEnt);
	static Entity **eaNonActiveNemeses = NULL;
	eaClear(&eaNonActiveNemeses);

	if (pEnt && pEnt->pPlayer) {
		int i;
		for (i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; i--) {
			Entity *pPet = GET_REF(pEnt->pSaved->ppOwnedContainers[i]->hPetRef);
			if (pPet && pPet->pNemesis && pPet->myContainerID != iPrimaryNemesisID){
				eaPush(&eaNonActiveNemeses, pPet);
			}
		}
	}
	ui_GenSetList(pGen, (void***)&eaNonActiveNemeses, parse_Entity);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetNumNonPrimaryNemeses");
U32 NemesisEditor_GetNumNonPrimaryNemeses(void)
{
	Entity *pEnt = entActivePlayerPtr();
	U32 iPrimaryNemesisID = player_GetPrimaryNemesisID(pEnt);
	U32 numNemeses = 0;

	if (pEnt && pEnt->pPlayer) {
		int i;
		for (i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; i--) {
			Entity *pPet = GET_REF(pEnt->pSaved->ppOwnedContainers[i]->hPetRef);
			if (pPet && pPet->pNemesis && pPet->myContainerID != iPrimaryNemesisID){
				numNemeses++;
			}
		}
	}

	return numNemeses;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetPrimaryNemesisList");
void NemesisEditor_GetPrimaryNemesisList(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity *pEnt = entActivePlayerPtr();
	static Entity **eaNemeses = NULL;
	eaClear(&eaNemeses);

	if (pEnt && pEnt->pPlayer) {
		Entity *pNemesis = player_GetPrimaryNemesis(pEnt);
		if (pNemesis){
			eaPush(&eaNemeses, pNemesis);
		}
	}

	ui_GenSetList(pGen, (void***)&eaNemeses, parse_Entity);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetNumPrimaryNemeses");
U32 NemesisEditor_GetNumPrimaryNemeses(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if (player_GetPrimaryNemesis(pEnt)){
		return 1;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_CanCreateNemesis");
bool NemesisEditor_CanCreateNemesis(SA_PARAM_NN_VALID Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer && !player_GetPrimaryNemesisID(pPlayerEnt) && eaSize(&pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates) < MAX_NEMESIS_COUNT){
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_CanReactivateNemesis");
bool NemesisEditor_CanReactivateNemesis(SA_PARAM_NN_VALID Entity *pPlayerEnt, ContainerID uNemesisID)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer && !player_GetPrimaryNemesisID(pPlayerEnt) && uNemesisID){
		PlayerNemesisState *pNemesisState = player_GetNemesisStateByID(pPlayerEnt, uNemesisID);
		if (pNemesisState && (pNemesisState->eState == NemesisState_InJail || pNemesisState->eState == NemesisState_AtLarge)){
			return true;
		} else {
			return false;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_CanEditName");
bool NemesisEditor_CanEditName(SA_PARAM_NN_VALID Entity *pPlayerEnt)
{
	if (g_CostumeEditState.uNemesisID){
		// Editing a Nemesis
		if (pPlayerEnt)
		{
			Entity *pNemesisEnt = player_GetNemesisByID(pPlayerEnt, g_CostumeEditState.uNemesisID);
			if (pNemesisEnt && pNemesisEnt->pSaved && pNemesisEnt->pSaved->bBadName){
				return true;
			}
		}
		return false;
	} else {
		// Creating a Nemesis
		return true;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_IsDuplicateName");
bool NemesisEditor_IsDuplicateName(SA_PARAM_NN_VALID Entity *pPlayerEnt, const char *pchName)
{
	ContainerID id = nemesis_FindIDFromName(pPlayerEnt, pchName);
	if (id && id != g_CostumeEditState.uNemesisID){
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetPowerSetList");
void NemesisEditor_GetPowerSetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &g_NemesisPowerSetList.sets, parse_NemesisPowerSet);
	if (!g_NemesisPowerSetList.sets) {
		ServerCmd_PopulateNemesisPowerSetList();
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetPowerSet");
void NemesisEditor_SetPowerSet(const char *pchPowerSet)
{
	int i;
	for (i = eaSize(&g_NemesisPowerSetList.sets)-1; i >= 0; i--) {
		if (stricmp(g_NemesisPowerSetList.sets[i]->pcName, pchPowerSet) == 0){
			g_CostumeEditState.pchNemesisPowerSet = g_NemesisPowerSetList.sets[i]->pcName;
			break;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetPowerSetInfo");
SA_RET_OP_VALID NemesisPowerSet *NemesisEditor_GetPowerSetInfo(const char *pchPowerSet)
{
	static NemesisPowerSet *s_pLast = NULL;
	static U32 s_iLastFrame = 0;
	int i;

	if (s_iLastFrame == gGCLState.totalElapsedTimeMs && s_pLast && stricmp(s_pLast->pcName, pchPowerSet) == 0)
	{
		return s_pLast;
	}

	for (i = eaSize(&g_NemesisPowerSetList.sets)-1; i >= 0; i--) {
		if (stricmp(g_NemesisPowerSetList.sets[i]->pcName, pchPowerSet) == 0){
			s_pLast = g_NemesisPowerSetList.sets[i];
			s_iLastFrame = gGCLState.totalElapsedTimeMs;
			return s_pLast;
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetPowerHue");
void NemesisEditor_SetPowerHue(F32 fHue)
{
	g_CostumeEditState.fNemesisPowerHue = fHue;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetPowerSetDisplayName");
const char *NemesisEditor_GetPowerSetDisplayName(void)
{
	int i;
	for (i = eaSize(&g_NemesisPowerSetList.sets)-1; i >= 0; i--) {
		if (g_CostumeEditState.pchNemesisPowerSet && g_CostumeEditState.pchNemesisPowerSet == g_NemesisPowerSetList.sets[i]->pcName){
			Message *pMessage = GET_REF(g_NemesisPowerSetList.sets[i]->msgDisplayName.hMessage);
			if (pMessage) {
				return langTranslateMessage(locGetLanguage(getCurrentLocale()), pMessage);
			}
		}
	}

	return langTranslateMessageKey(locGetLanguage(getCurrentLocale()), "UI.Loading");	
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetMotivationList");
void NemesisEditor_GetMotivationList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static NemesisNameData **eaList = NULL;

	if (!eaList) {
		int i;
		char **eaKeys = NULL;
		U32 *eaiValues = NULL;

		DefineFillAllKeysAndValues(NemesisMotivationEnum, &eaKeys, &eaiValues);

		for (i = eaSize(&eaKeys)-1; i >= 0; i--) {
			NemesisNameData *motivation = malloc(sizeof(NemesisNameData));
			motivation->name = eaKeys[i];
			eaPush(&eaList, motivation);
		}

		eaClear(&eaKeys);
		eaiClear(&eaiValues);
	}

	ui_GenSetList(pGen, &eaList, parse_NemesisNameData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetMotivation");
const char *NemesisEditor_GetMotivation(void)
{
	return StaticDefineIntRevLookup(NemesisMotivationEnum, g_CostumeEditState.motivation);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetMotivation");
void NemesisEditor_SetMotivation(const char *motivation)
{
	g_CostumeEditState.motivation = StaticDefineIntGetInt(NemesisMotivationEnum, motivation);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetMotivationInt");
void NemesisEditor_SetMotivationInt(int motivation)
{
	g_CostumeEditState.motivation = motivation;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetPersonalityList");
void NemesisEditor_GetPersonalityList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static NemesisNameData **eaList = NULL;

	if (!eaList) {
		int i;
		char **eaKeys = NULL;
		U32 *eaiValues = NULL;

		DefineFillAllKeysAndValues(NemesisPersonalityEnum, &eaKeys, &eaiValues);

		for (i = eaSize(&eaKeys)-1; i >= 0; i--) {
			NemesisNameData *personality = malloc(sizeof(NemesisNameData));
			personality->name = eaKeys[i];
			eaPush(&eaList, personality);
		}

		eaClear(&eaKeys);
		eaiClear(&eaiValues);
	}

	ui_GenSetList(pGen, &eaList, parse_NemesisNameData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetPersonality");
const char *NemesisEditor_GetPersonality(void)
{
	return StaticDefineIntRevLookup(NemesisPersonalityEnum, g_CostumeEditState.personality);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetPersonality");
void NemesisEditor_SetPersonality(const char *personality)
{
	g_CostumeEditState.personality = StaticDefineIntGetInt(NemesisPersonalityEnum, personality);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetPersonalityInt");
void NemesisEditor_SetPersonalityInt(int personality)
{
	g_CostumeEditState.personality = personality;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetMinionPowerSetList");
void NemesisEditor_GetMinionPowerSetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &g_NemesisMinionPowerSetList.sets, parse_NemesisMinionPowerSet);
	if (!g_NemesisMinionPowerSetList.sets) {
		ServerCmd_PopulateNemesisMinionPowerSetList();
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetMinionPowerSet");
void NemesisEditor_SetMinionPowerSet(const char *pchMinionPowerSet)
{
	int i;
	for (i = eaSize(&g_NemesisMinionPowerSetList.sets)-1; i >= 0; i--) {
		if (stricmp(g_NemesisMinionPowerSetList.sets[i]->pcName, pchMinionPowerSet) == 0) {
			g_CostumeEditState.pchMinionPowerSet = g_NemesisMinionPowerSetList.sets[i]->pcName;
			return;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetMinionPowerHue");
void NemesisEditor_SetMinionPowerHue(F32 fHue)
{
	g_CostumeEditState.fMinionPowerHue = fHue;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetMinionPowerSet");
const char *NemesisEditor_GetMinionPowerSet(void)
{
	return g_CostumeEditState.pchMinionPowerSet;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetMinionPowerSetDisplayName");
const char *NemesisEditor_GetMinionPowerSetDisplayName(void)
{
	const char *pchMinionPowerSet = g_CostumeEditState.pchMinionPowerSet;
	int i;
	for (i = eaSize(&g_NemesisMinionPowerSetList.sets)-1; i >= 0; i--) {
		if (pchMinionPowerSet && stricmp(g_NemesisMinionPowerSetList.sets[i]->pcName, pchMinionPowerSet) == 0) {
			Message *pMessage = GET_REF(g_NemesisMinionPowerSetList.sets[i]->msgDisplayName.hMessage);
			if (pMessage) {
				return langTranslateMessage(locGetLanguage(getCurrentLocale()), pMessage);
			}
		}
	}
	return langTranslateMessageKey(locGetLanguage(getCurrentLocale()), "UI.Loading");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetMinionCostumeSetList");
void NemesisEditor_GetMinionCostumeSetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &g_NemesisMinionCostumeSetList.sets, parse_NemesisMinionCostumeSet);
	if (!g_NemesisMinionCostumeSetList.sets) {
		ServerCmd_PopulateNemesisMinionCostumeSetList();
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetMinionCostumeSet");
void NemesisEditor_SetMinionCostumeSet(const char *pchCostumeSet)
{
	int i;
	for (i = eaSize(&g_NemesisMinionCostumeSetList.sets)-1; i >= 0; i--) {
		if (stricmp(g_NemesisMinionCostumeSetList.sets[i]->pcName, pchCostumeSet) == 0) {
			g_CostumeEditState.pchMinionCostumeSet = g_NemesisMinionCostumeSetList.sets[i]->pcName;
			return;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetMinionCostumeSet");
const char *NemesisEditor_GetMinionCostumeSet(void)
{
	return g_CostumeEditState.pchMinionCostumeSet;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetMinionCostumeSetDisplayName");
const char *NemesisEditor_GetMinionCostumeSetDisplayName(void)
{
	const char *pchMinionCostumeSet = g_CostumeEditState.pchMinionCostumeSet;
	int i;
	for (i = eaSize(&g_NemesisMinionCostumeSetList.sets)-1; i >= 0; i--) {
		if (pchMinionCostumeSet && stricmp(g_NemesisMinionCostumeSetList.sets[i]->pcName, pchMinionCostumeSet) == 0) {
			Message *pMessage = GET_REF(g_NemesisMinionCostumeSetList.sets[i]->msgDisplayName.hMessage);
			if (pMessage) {
				return langTranslateMessage(locGetLanguage(getCurrentLocale()), pMessage);
			}
		}
	}
	return langTranslateMessageKey(locGetLanguage(getCurrentLocale()), "UI.Loading");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetState");
const char *NemesisEditor_GetState(void)
{
	return StaticDefineIntRevLookup(NemesisStateEnum, g_CostumeEditState.state);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetNemesisState");
const char *NemesisEditor_GetNemesisState(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pNemesisEnt)
{
	// Find Nemesis state on Player
	if (pPlayerEnt && pNemesisEnt && pPlayerEnt->pPlayer && pNemesisEnt->pNemesis) {
		int i;
		for(i=eaSize(&pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates)-1; i>=0; --i){
			if (pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->iNemesisID == pNemesisEnt->myContainerID){
				return StaticDefineIntRevLookup(NemesisStateEnum, pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->eState);
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetState");
void NemesisEditor_SetState(const char *state)
{
	g_CostumeEditState.state = StaticDefineIntGetInt(NemesisStateEnum, state);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetStateInt");
void NemesisEditor_SetStateInt(int state)
{
	g_CostumeEditState.state = state;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetName");
const char *NemesisEditor_GetName(void)
{
	return g_CostumeEditState.pcNemesisName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetDescription");
const char *NemesisEditor_GetDescription(void)
{
	return g_CostumeEditState.pcNemesisDescription;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetName");
void NemesisEditor_SetName(const char *name)
{
	char *estrWorking = NULL;
	estrStackCreate(&estrWorking);
	estrAppend2(&estrWorking, name);
	estrTrimLeadingAndTrailingWhitespace(&estrWorking);
	if (g_CostumeEditState.pcNemesisName) {
		StructFreeString(g_CostumeEditState.pcNemesisName);
	}
	g_CostumeEditState.pcNemesisName = StructAllocString(estrWorking);
	estrDestroy(&estrWorking);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetDescription");
void NemesisEditor_SetDescription(const char *description)
{
	if (g_CostumeEditState.pcNemesisDescription) {
		StructFreeString(g_CostumeEditState.pcNemesisDescription);
	}
	g_CostumeEditState.pcNemesisDescription = StructAllocString(description);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_MakeNemesis");
bool NemesisEditor_MakeNemesis(void)
{
	int strerr;
	Entity *pEnt = entActivePlayerPtr();
	PCMood *pMood;

	if (!pEnt)
	{
		pEnt = (Entity *)g_pFakePlayer;
	}

	strerr = StringIsInvalidCharacterName( NULL_TO_EMPTY(g_CostumeEditState.pcNemesisName), entGetAccessLevel(pEnt) );

	if ( strerr > 0 )
	{
		char* pcError = NULL;
		estrStackCreate( &pcError );
		StringCreateNameError( &pcError, strerr );
		notify_NotifySend(NULL, kNotifyType_NameInvalid, pcError, NULL, NULL);
		estrDestroy(&pcError);
		return false;
	}

	//trim the trailing whitespace
	removeTrailingWhiteSpaces( g_CostumeEditState.pcNemesisName );
	if (g_CostumeEditState.pcNemesisDescription)
		removeTrailingWhiteSpaces( g_CostumeEditState.pcNemesisDescription );

	pMood = GET_REF(g_CostumeEditState.hMood);

	SAFE_FREE(s_pchPlayerMakeNemesisResult);
	ServerCmd_playerMakeNemesis(
		g_CostumeEditState.pcNemesisName, g_CostumeEditState.pcNemesisDescription,
		g_CostumeEditState.pConstCostume, pMood ? pMood->pcName : NULL,
		g_CostumeEditState.motivation, g_CostumeEditState.personality,
		g_CostumeEditState.pchNemesisPowerSet, g_CostumeEditState.pchMinionPowerSet, g_CostumeEditState.pchMinionCostumeSet,
		g_CostumeEditState.fNemesisPowerHue, g_CostumeEditState.fMinionPowerHue);

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_EditNemesis");
bool NemesisEditor_EditNemesis(void)
{
	int strerr;
	Entity *pEnt = entActivePlayerPtr();
	PCMood *pMood;

	if (!pEnt)
	{
		pEnt = (Entity *)g_pFakePlayer;
	}

	strerr = StringIsInvalidCharacterName( g_CostumeEditState.pcNemesisName, entGetAccessLevel(pEnt) );

	if ( strerr > 0 )
	{
		char* pcError = NULL;
		estrStackCreate( &pcError );
		StringCreateNameError( &pcError, strerr );
		notify_NotifySend(NULL, kNotifyType_NameInvalid, pcError, NULL, NULL);
		estrDestroy(&pcError);
		return false;
	}

	//trim the trailing whitespace
	removeTrailingWhiteSpaces( g_CostumeEditState.pcNemesisName );
	if (g_CostumeEditState.pcNemesisDescription)
		removeTrailingWhiteSpaces( g_CostumeEditState.pcNemesisDescription );

	SAFE_FREE(s_pchPlayerMakeNemesisResult);
	pMood = GET_REF(g_CostumeEditState.hMood);
	ServerCmd_playerEditNemesis(
		g_CostumeEditState.uNemesisID,
		g_CostumeEditState.pcNemesisName, g_CostumeEditState.pcNemesisDescription,
		g_CostumeEditState.pConstCostume, pMood ? pMood->pcName : NULL,
		/* g_CostumeEditState.motivation, */ g_CostumeEditState.personality,
		g_CostumeEditState.pchNemesisPowerSet, g_CostumeEditState.pchMinionPowerSet, g_CostumeEditState.pchMinionCostumeSet,
		g_CostumeEditState.fNemesisPowerHue, g_CostumeEditState.fMinionPowerHue);

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_GetMakeNemesisResult");
const char *NemesisEditor_GetMakeNemesisResult(ExprContext *pContext)
{
	const char *pchResult = s_pchPlayerMakeNemesisResult ? TranslateMessageKey(s_pchPlayerMakeNemesisResult) : "(null)";

	if (!pchResult)
	{
		if (s_pchPlayerMakeNemesisResult)
		{
			char *pchTemp = NULL;
			estrStackCreate(&pchTemp);
			estrConcatf(&pchTemp, "[UNTRANSLATED: %s]", s_pchPlayerMakeNemesisResult);
			pchResult = exprContextAllocString(pContext, pchTemp);
			estrDestroy(&pchTemp);
		}
		else
		{
			pchResult = "[null]";
		}
	}

	return pchResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_NemesisMade);
bool CostumeCreator_NemesisMade(void)
{
	return s_bPlayerMakeNemesisSuccessful;
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void CostumeCreator_SetPlayerMakeNemesisResult(bool bSuccessful, const char *pcResultKey)
{
	s_bPlayerMakeNemesisSuccessful = bSuccessful;
	s_pchPlayerMakeNemesisResult = pcResultKey ? strdup(pcResultKey) : NULL;
}

// Preload a list of nemesis minion costumes
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_InitNemesisMinionCostumes");
void NemesisEditor_InitNemesisMinionCostumes(const char *pchNames)
{
	gclCostumeEditListExpr_LoadCostumeList(COSTUME_LIST_MINIONCOSTUMES, pchNames);
}

// Unload the preloaded list of nemesis minion costumes
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_DeinitNemesisMinionCostumes");
void NemesisEditor_DeinitNemesisMinionCostumes(void)
{
	CostumeEditList_ClearCostumeSourceList(COSTUME_LIST_MINIONCOSTUMES, true);
}

// Get a headshot of a preloaded nemesis minion costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_MinionHeadshot");
SA_RET_OP_VALID BasicTexture *NemesisEditor_MinionHeadshot(const char *pchName, SA_PARAM_OP_VALID BasicTexture *pTexture, const char* chBackground, const char* chBitString, const char* chPose, const char* chMood, bool bBodyShot, float fWidth, float fHeight, bool bForceRedraw, bool bTransparent)
{
	return NULL;
}

// Free a headshot of a nemesis minion costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_FreeMinionHeadshot");
SA_RET_OP_VALID BasicTexture *NemesisEditor_FreeMinionHeadshot(SA_PARAM_OP_VALID BasicTexture *pTexture)
{
	return NULL;
}

// Prepare a specific minion costume for display
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetNemesisMinionCostume");
void NemesisEditor_SetNemesisMinionCostume(const char *pchName)
{
	char *estrName = NULL;

	estrStackCreate(&estrName);
	estrAppend2(&estrName, "Nemesisminion_");
	estrAppend2(&estrName, pchName);

	gclCostumeEditListExpr_LoadCostume(COSTUME_LIST_MINIONCOSTUMES, "Minion", pchName);

	estrDestroy(&estrName);
}

// Show the minion costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_ShowNemesisMinionCostume");
bool NemesisEditor_ShowNemesisMinionCostume(void)
{
	return gclCostumeEditListExpr_SelectNamedCostume(COSTUME_LIST_MINIONCOSTUMES, "Minion");
}

// Clean up a displayed minion costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_HideNemesisMinionCostume");
void NemesisEditor_HideNemesisMinionCostume(void)
{
	CostumeUI_RegenCostume(true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_SetNemesisData");
void NemesisEditor_SetNemesisData(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pNemesisEnt)
{
	int i, iPetNum = 0;
	if (pPlayerEnt && pPlayerEnt->pPlayer && pNemesisEnt && pNemesisEnt->pNemesis) {
		g_CostumeEditState.uNemesisID = pNemesisEnt->myContainerID;
		NemesisEditor_SetName(pNemesisEnt->pSaved->savedName);

		// Copy costume
		CostumeCreator_CopyCostumeFromContainer(kPCCostumeStorageType_Nemesis, g_CostumeEditState.uNemesisID, 0);

		StructFreeString(g_CostumeEditState.pcNemesisDescription);
		g_CostumeEditState.pcNemesisDescription = StructAllocString(pNemesisEnt->pSaved->savedDescription);
		g_CostumeEditState.motivation = pNemesisEnt->pNemesis->motivation;
		g_CostumeEditState.personality = pNemesisEnt->pNemesis->personality;

		g_CostumeEditState.pchNemesisPowerSet = pNemesisEnt->pNemesis->pchPowerSet;
		g_CostumeEditState.pchMinionPowerSet = pNemesisEnt->pNemesis->pchMinionPowerSet;
		g_CostumeEditState.pchMinionCostumeSet = pNemesisEnt->pNemesis->pchMinionCostumeSet;

		g_CostumeEditState.fNemesisPowerHue = pNemesisEnt->pNemesis->fPowerHue;
		g_CostumeEditState.fMinionPowerHue = pNemesisEnt->pNemesis->fMinionPowerHue;

		StructDestroySafe(parse_Nemesis, &g_CostumeEditState.pStartNemesis);
		g_CostumeEditState.pStartNemesis = StructClone(parse_Nemesis, pNemesisEnt->pNemesis);

		// Find Nemesis state on Player
		for(i=eaSize(&pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates)-1; i>=0; --i){
			if (pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->iNemesisID == pNemesisEnt->myContainerID){
				g_CostumeEditState.state = pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->eState;
				break;
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NemesisEditor_ResetNemesisData");
void NemesisEditor_ResetNemesisData(void)
{
	g_CostumeEditState.uNemesisID = 0;
	g_CostumeEditState.fNemesisPowerHue = 0.f;
	g_CostumeEditState.fMinionPowerHue = 0.f;
	g_CostumeEditState.motivation = 0;
	g_CostumeEditState.personality = 0;
	g_CostumeEditState.pchNemesisPowerSet = NULL;
	g_CostumeEditState.pchMinionPowerSet = NULL;
	g_CostumeEditState.pchMinionCostumeSet = NULL;
	StructFreeStringSafe(&g_CostumeEditState.pcNemesisDescription);
	StructFreeStringSafe(&g_CostumeEditState.pcNemesisName);
	StructDestroySafe(parse_Nemesis, &g_CostumeEditState.pStartNemesis);
}

#include "gclNemesisEditorUI_c_ast.c"
