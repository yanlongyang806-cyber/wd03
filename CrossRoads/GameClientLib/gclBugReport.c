#include "uidialog.h"
#include "ticketnet.h"
#include "GameClientLib.h"
#include "utilitiesLib.h"
#include "trivia.h"
#include "file.h"
#include "gclEntity.h"
#include "TimedCallback.h"
#include "GameAccountDataCommon.h"
#include "GfxCommandParse.h"
#include "gclSendToServer.h"
#include "GameStringFormat.h"
#include "Category.h"
#include "GfxConsole.h"
#include "StringUtil.h"
#include "EntitySavedData.h"
#include "Player.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "entCritter.h"
#include "SavedPetCommon.h"
#include "tradeCommon.h"
#include "tradeCommon_h_ast.h"

#include "UIGen.h"
#include "Expression.h"
#include "gclcommandparse.h"
#include "gclLogin.h"
#include "NotifyCommon.h"
#include "gclNotify.h"
#include "MemReport.h"
#include "MemoryBudget.h"
#include "UGCBugReport.h"

#include "gclBugReport.h"
#include "Autogen/trivia_h_ast.h"
#include "Autogen/ticketnet_h_ast.h"
#include "Autogen/ticketenums_h_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/gclBugReport_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ------------------------------------------------------------------------------------------------
typedef struct UserDataCBStruct
{
	char *category;
	CategoryCustomDataFunc cb;
	bool bIsDelayed; // Marks if the callback may not be able to be completed in this frame
} UserDataCBStruct;

AUTO_ENUM;
typedef enum ClientTicketStatus
{
	TicketStatus_SearchInput = 0, // Waiting for user input on search
	TicketStatus_SubmissionInput, // Waiting for user input on submission
	TicketStatus_Search, // Submitting a search request
	TicketStatus_Back, // Going back to search from submission
	TicketStatus_Submit, // Submitting Ticket
	TicketStatus_Error, // Failed
} ClientTicketStatus;

typedef struct CategoryCBStruct
{
	char *category;

	// For sending server data
	CategoryComboBoxTextFunc cbText;
	// For labeling tickets
	CategoryComboBoxTextFunc cbLabel;

	// For UIGen
	CategoryComboBoxSelectedFunc cbSelected; // for getting the selected model element in the combo box
	char *pGenName;
} CategoryCBStruct;

static UserDataCBStruct ** seaUserDataCBList = NULL;
static UserDataCBStruct ** sppDelayedFuncs = NULL;
static StashTable sCategoryStash = NULL;
// The current funcs in use by the selected category
static CategoryCBStruct * sCurrentCategory = NULL;

AUTO_RUN_EARLY;
void initCategoryCBStash(void)
{
	sCategoryStash = stashTableCreateWithStringKeys(10, StashDefault);
}

CategoryCBStruct* cBug_GetCategoryChoiceCallbacks(const char *category)
{
	CategoryCBStruct * cbstruct = NULL;
	if (stashFindPointer(sCategoryStash, category, &cbstruct))
		return cbstruct;
	return NULL;
}

// Replaces any existing callbacks for the function
void cBug_AddCategoryChoiceCallbacks(const char *category, CategoryComboBoxTextFunc textf, 
									 CategoryComboBoxTextFunc labelf, CategoryComboBoxSelectedFunc selectedf, 
									 const char *pGenName)
{
	CategoryCBStruct * cbstruct = cBug_GetCategoryChoiceCallbacks(category);
	if (!cbstruct)
	{
		cbstruct = calloc(1, sizeof(CategoryCBStruct));
		stashAddPointer(sCategoryStash, category, cbstruct, true);
	}
	else
	{
		if (cbstruct->category)
			free(cbstruct->category);
		if (cbstruct->pGenName)
			free(cbstruct->pGenName);

	}
	cbstruct->category = strdup(category);
	cbstruct->cbText = textf;
	cbstruct->cbLabel = labelf;
	cbstruct->cbSelected = selectedf;
	cbstruct->pGenName = pGenName ? strdup(pGenName) : strdup("");
}

bool cBugAddCustomDataCallback (const char *category, CategoryCustomDataFunc callback)
{
	UserDataCBStruct *cb = calloc(1, sizeof(UserDataCBStruct));

	cb->category = strdup(category);
	cb->cb = callback;
	
	eaPush(&seaUserDataCBList, cb);
	return true;
}

bool cBugRemoveCustomDataCallback (const char *category, CategoryCustomDataFunc callback)
{
	int i;
	for (i=eaSize(&seaUserDataCBList)-1; i>=0; i--)
	{
		if (stricmp(seaUserDataCBList[i]->category, category) == 0 && seaUserDataCBList[i]->cb == callback)
		{
			eaRemove(&seaUserDataCBList, i);
			return true;
		}
	}
	return false;
}

void cBug_RunCustomDataCallbacks(TicketData *ticket, const char *category)
{
	int i;
	for (i=eaSize(&seaUserDataCBList)-1; i>=0; i--)
	{
		if (stricmp(seaUserDataCBList[i]->category, category) == 0 || 
			stricmp(seaUserDataCBList[i]->category, TICKETDATA_ALL_CATEGORY_STRING) == 0)
		{
			if (seaUserDataCBList[i]->bIsDelayed)
			{
				eaPush(&sppDelayedFuncs, seaUserDataCBList[i]);
			}
			else
			{
				void *pStruct = NULL;
				ParseTable *pti = NULL;
				seaUserDataCBList[i]->cb(&pStruct, &pti, &ticket->pTicketLabel);

				if (pStruct)
				{
					assertmsg(pti, "No Parse Table set.");
					putUserDataIntoTicket(ticket, pStruct, pti);
					StructDestroyVoid(pti, pStruct);
				}
			}
		}
	}
}

extern ParseTable parse_Category[];
#define TYPE_parse_Category Category
extern char *g_errorTriviaString;
extern TriviaData **g_ppTrivia;

static const char *szScreenshotsDir = "screenshots";
static const char *szScreenshotsFile = "tempss.jpg";
static const char *szCategoryDataFile = "";

AUTO_STARTUP(BugReport) ASTRT_DEPS(Category);
void cBugStartup(void)
{
}

// ------------------------------------------------------------------------------------------------

static bool s_bInSend = false;
static bool bReceivedResponse = false;
static int siResponseResult = 0;
static char *spResponseKey = NULL;
static DWORD start_tick = 0;

static U32 suLastTicketID = 0;

#define WAIT_FOR_RESPONSE_TIMEOUT 45000

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void ClientShowTrackerResponse(int result, const char *pResponseKey, U32 uTicketID)
{
	bReceivedResponse = true;
	siResponseResult = result;
	suLastTicketID = uTicketID;

	estrClear(&spResponseKey);
	if (pResponseKey && *pResponseKey)
		estrCopy2(&spResponseKey, pResponseKey);
}

static int sendTicketWithScreenshot(U32 uType, const char *pData, const char *pExtraData, const char *pFilename)
{
	FILE *pFile = fopen(pFilename, "rb");
	U32 uImageSize = 0;
	char *pTempData = NULL;
	int retval = 0;

	if (!pFile)
	{
		// Send the ticket even if the screenshot file fails to be read
		return gclServerSendTicketAndScreenshot(uType, pData, pExtraData, NULL, 0);
	}

	uImageSize = (U32) fileGetSize(pFile);
	pTempData = malloc(uImageSize);
	fread(pTempData, sizeof(char), uImageSize, pFile);
	fclose(pFile);

	retval = gclServerSendTicketAndScreenshot(uType, pData, pExtraData, pTempData, uImageSize);

	free(pTempData);
	return retval;
}

int sendToTicketTracker (TicketData *ticket, const char *pScreenshotFilename)
{
	int retval = 0;
	if (!pScreenshotFilename || !*pScreenshotFilename)
	{
		retval = ticketTrackerSendTicket(ticket);
	}
	else
	{
		FILE *pFile = fopen(pScreenshotFilename, "rb");
		U32 uImageSize = 0;
		char *pTempData = NULL;

		if (!pFile)
			return ticketTrackerSendTicket(ticket);

		uImageSize = (U32) fileGetSize(pFile);
		pTempData = malloc(uImageSize);
		fread(pTempData, sizeof(char), uImageSize, pFile);
		fclose(pFile);
		ticket->uImageSize = uImageSize;

		retval = ticketTrackerSendTicketPlusScreenshot(ticket, pTempData);
		free(pTempData);
	}
	if (retval)
		suLastTicketID = ticketTrackerGetLastID();
	return retval;
}

// ------------------------------------------------------------------------------------------------
static void ticketPopulateAndSend(const char *pMainCategory, const char *pCategory, const char *pSummary, const char *pDescription, 
								  const char *imagepath, int iMergeID, TicketVisibility eVisibility, const char *pLabel);

static bool sbInternalBugReport = false;
static ClientTicketStatus sTicketStatus = TicketStatus_SearchInput;
static ClientTicketStatus sTicketActiveWindow = TicketStatus_SearchInput;

// Return a string with the current ticket error message, or "" if there is no error [yet]
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_GetError");
const char *gclTicketExpr_GetError(ExprContext *pContext)
{
	static char *sTicketError = NULL;
	if (spResponseKey && siResponseResult != TICKETFLAGS_SUCCESS)
	{
		estrClear(&sTicketError);
		FormatGameMessageKey(&sTicketError, spResponseKey, STRFMT_END);
		return sTicketError;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_DisableCategory");
bool Ticket_DisableCategoryControls(void)
{
	return (sTicketStatus != TicketStatus_SearchInput && sTicketStatus != TicketStatus_SubmissionInput);
}

void Ticket_ChangeStatus(ClientTicketStatus eNewStatus);
void Ticket_ClearErrorStatus(TimedCallback *callback, F32 timeSinceLastCallback, void *data)
{
	Ticket_ChangeStatus(sTicketActiveWindow);
}

void Ticket_ChangeStatus(ClientTicketStatus eNewStatus)
{
	switch (eNewStatus)
	{
	case TicketStatus_SearchInput:
		{
			UIGen *main = ui_GenFind("TicketWindow_Main", kUIGenTypeBox);
			estrClear(&spResponseKey);
			ui_GenState(main, kUIGenStateDisabled, false);
		}
	xcase TicketStatus_SubmissionInput:
		{
			UIGen *main = ui_GenFind("TicketCreate_Main", kUIGenTypeBox);
			estrClear(&spResponseKey);
			ui_GenState(main, kUIGenStateDisabled, false);
		}
	xcase TicketStatus_Search:
		{
			UIGen *main = ui_GenFind("TicketWindow_Main", kUIGenTypeBox);
			estrCopy2(&spResponseKey, "Ticket.Refresh.Text");
			ui_GenState(main, kUIGenStateDisabled, true);
		}
	xcase TicketStatus_Back:
		{
			UIGen *main = ui_GenFind("TicketCreate_Main", kUIGenTypeBox);
			estrClear(&spResponseKey);
			ui_GenState(main, kUIGenStateDisabled, true);
		}
	xcase TicketStatus_Submit:
		{
			UIGen *main = ui_GenFind("TicketCreate_Main", kUIGenTypeBox);
			estrCopy2(&spResponseKey, "Ticket.Submit.Text");
			ui_GenState(main, kUIGenStateDisabled, true);
		}
	xcase TicketStatus_Error:
		{
			if (sTicketActiveWindow == TicketStatus_SearchInput)
			{
				UIGen *main = ui_GenFind("TicketWindow_Main", kUIGenTypeBox);
				ui_GenState(main, kUIGenStateDisabled, true);
			}
			else
			{
				UIGen *main = ui_GenFind("TicketCreate_Main", kUIGenTypeBox);
				ui_GenState(main, kUIGenStateDisabled, true);
			}
			notify_NotifySend(NULL, kNotifyType_TicketError, gclTicketExpr_GetError(NULL), NULL, NULL);
			TimedCallback_Run(Ticket_ClearErrorStatus, NULL, 3);
		}
		break;
	}
	sTicketStatus = eNewStatus;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketGen_GameServerConnected");
bool Ticket_GameServerConnected(void)
{
	return gclServerIsConnected();
}

void Ticket_SetSearchString(SA_PARAM_OP_STR const char *string);
void cTicketLabel_ScreenshotCB(char *fileName, void *userData)
{
	sTicketActiveWindow = sTicketStatus = TicketStatus_SearchInput;
	Ticket_SetSearchString("");
	if (!gclNotifySendIfHandled(kNotifyType_BugReport, szScreenshotsFile, "bug", ""))
	{
		ui_GenAddWindow(ui_GenFind("TicketWindow_Root", kUIGenTypeMovableBox));
	}
	Category_SendTicketUpdateRequest();
}

void cTicketLabel(void)
{
	ServerCmd_gslGrabEntityTarget();
	gfxConsoleEnable(0); // Hide the console if it's shown
	gfxSaveJPGScreenshotWithUIOverrideCallback(szScreenshotsFile, 60, cTicketLabel_ScreenshotCB, NULL);
}

// Report a problem with the game.
AUTO_COMMAND ACMD_NAME(bug) ACMD_ACCESSLEVEL(0);
void cBugExternal(void)
{
	sbInternalBugReport = false;
	cTicketLabel();
}

/////////////////////////////////
// UIGen Stuff

static char *sMessageString = NULL;
static Category *sSelectedMainCategory = NULL;
static Category *sSelectedCategory = NULL;
static TicketRequestResponse *sSelectedTicket = NULL;
static TicketRequestResponse **sTicketLabelList = NULL;

#define TICKET_MAX_SEARCH_LEN 256
static char sTicketSearchBuffer[TICKET_MAX_SEARCH_LEN] = "";
// sCurrentCategory

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_SetSearchString");
void Ticket_SetSearchString(SA_PARAM_OP_STR const char *string)
{
	// TODO error if too long
	strcpy(sTicketSearchBuffer, string);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_GetSearchString");
const char *Ticket_GetSearchString(void)
{
	return sTicketSearchBuffer;
}

void TicketUI_DisableButton(const char *genName, int disable)
{
	UIGen *button = ui_GenFind(genName, kUIGenTypeButton);

	if (button)
	{
		ui_GenState(button, kUIGenStateDisabled, disable);
	}
}

static void TicketUI_ForceTicketListReload(void)
{
	UIGen *pGen = ui_GenFind("Ticket_MainList", kUIGenTypeList);
	if (pGen)
	{
		ui_GenSetList(pGen, &sTicketLabelList, parse_TicketRequestResponse);
		ui_GenMarkDirty(pGen);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketGen_SelectedCreateNew");
bool Ticket_SelectedCreateNew(void)
{
	if (sSelectedTicket && sSelectedTicket->uID == 0)
	{
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketGen_SelectTicket");
void Ticket_Select(SA_PARAM_OP_VALID TicketRequestResponse * ticket)
{
	sSelectedTicket = ticket;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketGen_ClearSelection");
void Ticket_SelectClear(void)
{
	sSelectedTicket = NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Category_GetList");
void Category_GetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, getCategories(), parse_Category);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Category_GetSubcategoryList");
void Category_GetSubcategoryList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static Category **emptyCategory = NULL;
	if (sSelectedMainCategory)
		ui_GenSetList(pGen, &sSelectedMainCategory->ppSubCategories, parse_Category);
	else
		ui_GenSetList(pGen, &emptyCategory, parse_Category);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Category_SetMainCategory");
void Category_SetMainCategory(const char *pMainCategoryKey)
{
	Category *category = getMainCategory(pMainCategoryKey);
	if (category)
	{
		sSelectedMainCategory = category;
	}
	else
	{
		Errorf("Couldn't find Main Category Key: '%s'", pMainCategoryKey);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Gen_SetCategoryList");
void Category_SetCategoryList(SA_PARAM_NN_VALID UIGen *pGen, const char *pMainCategoryKey)
{
	Category *category = getMainCategory(pMainCategoryKey);
	if (category)
	{
		ui_GenSetList(pGen, &category->ppSubCategories, parse_Category);
	}
	else
	{
		Errorf("Couldn't find Main Category Key: '%s'", pMainCategoryKey);
	}
}

void Category_SendTicketUpdateRequest(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketCategory_SetSelected");
bool TicketCategory_SetSelected(SA_PARAM_OP_VALID Category *pchCategory)
{
	sSelectedMainCategory = pchCategory;
	sSelectedCategory = NULL;
	sCurrentCategory = NULL;
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketCategory_SetSelectedByName");
void TicketCategory_SetSelectedByName(const char *pchMainCategoryName, const char *pchSubcategoryName)
{
	Category *maincat = getMainCategory(pchMainCategoryName);
	Category *subcat;

	if (!maincat)
	{
		Errorf("Invalid main category: %s", pchMainCategoryName);
		return;
	}
	subcat = getCategoryFromMain(maincat, pchSubcategoryName);
	if (!subcat)
	{
		Errorf("Invalid subcategory: %s", pchSubcategoryName);
		return;
	}
	
	sSelectedMainCategory = maincat;
	sSelectedCategory = subcat;
	sCurrentCategory = cBug_GetCategoryChoiceCallbacks(REF_STRING_FROM_HANDLE(sSelectedCategory->hDisplayName));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketCategory_SetSelectedSub");
bool TicketCategory_SetSelectedSub(SA_PARAM_OP_VALID Category *pchCategory)
{
	if (pchCategory)
	{
		sSelectedCategory = pchCategory;
		sCurrentCategory = cBug_GetCategoryChoiceCallbacks(REF_STRING_FROM_HANDLE(sSelectedCategory->hDisplayName));
		return true;
	}
	else
	{
		sCurrentCategory = NULL;
		return false;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketCategory_GetSelectedSubSpecialGen");
SA_RET_OP_VALID UIGen *TicketCategory_GetSelectedSubSpecialGen(ExprContext *pContext)
{
	if (sCurrentCategory)
	{
		UIGen *pGen = ui_GenFind(sCurrentCategory->pGenName, kUIGenTypeBox);
		if (pGen)
		{
			return pGen;
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketMainCategory_IsSelected");
bool TicketMainCategory_IsSelected(void)
{
	return (bool) sSelectedMainCategory;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketMainCategory_IsGMSelected");
bool TicketMainCategory_IsGMSelected(void)
{
	return (sSelectedMainCategory && stricmp(REF_STRING_FROM_HANDLE(sSelectedMainCategory->hDisplayName), "CBug.CategoryMain.GM") == 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketMainCategory_IsGameSupportSelected");
bool TicketMainCategory_IsGameSupportSelected(void)
{
	return (sSelectedMainCategory && stricmp(REF_STRING_FROM_HANDLE(sSelectedMainCategory->hDisplayName), "CBug.CategoryMain.GameSupport") == 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketSubcategory_IsSelected");
bool TicketSubcategory_IsSelected(void)
{
	return (bool) sSelectedCategory;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketCategory_GetSelectedSub");
SA_RET_OP_STR const char * Category_GetSelectedSub(void)
{
	if (sSelectedCategory)
	{
		estrClear(&sMessageString);
		FormatGameMessage(&sMessageString, GET_REF(sSelectedCategory->hDisplayName), STRFMT_END);
		return sMessageString;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Category_GetText");
SA_RET_OP_STR const char * Category_GetRowDisplayText(SA_PARAM_NN_VALID Category *category)
{
	estrClear(&sMessageString);
	FormatGameMessage(&sMessageString, GET_REF(category->hDisplayName), STRFMT_END);
	return sMessageString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Category_GetSubText");
SA_RET_OP_STR const char * Category_GetSubcategoryRowDisplayText(SA_PARAM_NN_VALID Category *category)
{
	estrClear(&sMessageString);
	FormatGameMessage(&sMessageString, GET_REF(category->hDisplayName), STRFMT_END);
	return sMessageString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Category_GetSpecialName");
SA_RET_NN_STR const char *Category_GetSpecialName(void)
{
	if (sCurrentCategory)
		return sCurrentCategory->pGenName;
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Category_Reset");
void Category_Reset(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateUser1))
	{
		// only destroy the ticket list
		sSelectedTicket = NULL;
		eaDestroyStruct(&sTicketLabelList, parse_TicketRequestResponse);
		return;
	}
	if (sCurrentCategory)
	{
		UIGen *pCurrentGen = ui_GenFind(sCurrentCategory->pGenName, kUIGenTypeBox);
		if (pCurrentGen)
		{
			ui_GenSendMessage(pCurrentGen, "CategoryReset");
			ui_GenReset(pCurrentGen);
			ui_GenRemoveChild(pGen, pCurrentGen, true);
		}
	}

	// Leave the current categories selected
	sSelectedMainCategory = NULL;
	sSelectedCategory = NULL;
	sCurrentCategory = NULL;
	sSelectedTicket = NULL;
	eaDestroyStruct(&sTicketLabelList, parse_TicketRequestResponse);
}

static void TicketList_AppendCreateNew(void)
{
	TicketRequestResponse *pResponse = StructCreate(parse_TicketRequestResponse);
	char *defaultNewText = NULL;

	estrStackCreate(&defaultNewText);
	FormatGameMessageKey(&defaultNewText, "Ticket.CreateNew.Text", STRFMT_END);
	pResponse->pSummary = StructAllocString(defaultNewText);
	estrDestroy(&defaultNewText);
	eaPush(&sTicketLabelList, pResponse);
}

static void TicketList_AppendHiddenCategory(SA_PARAM_NN_VALID Category *category)
{
	TicketRequestResponse *pResponse = StructCreate(parse_TicketRequestResponse);
	char *categoryString = NULL;
	static char *pSummary = NULL;

	estrStackCreate(&categoryString);
	FormatGameMessage(&categoryString, GET_REF(category->hDisplayName), STRFMT_END);

	estrCopy2(&pSummary, "");
	FormatGameMessageKey(&pSummary, "Ticket.Hidden.Text", STRFMT_STRING("Category", categoryString), STRFMT_END);
	pResponse->pSummary = StructAllocString(pSummary);
	eaPush(&sTicketLabelList, pResponse);
	estrDestroy(&categoryString);
}

void Category_UpdateTicketList(const char *ticketResponse);
void Category_UpdateTicketListCallback(void *userData, const char *ticketResponse)
{
	Category_UpdateTicketList(ticketResponse);
}

static void Category_SendTicketUpdateRequestHelper(const char *mainCategory, const char *category, const char *label)
{
	TicketRequestData ticketData = {0};
	char *pTicketSearchStart = &sTicketSearchBuffer[0];
	char *pTicketSearchEnd = &sTicketSearchBuffer[0] + strlen(sTicketSearchBuffer) - 1;
	Entity * ent = entActivePlayerPtr();

	ticketData.pMainCategory = mainCategory;
	ticketData.pCategory = category;
	ticketData.pLabel = label;
	Ticket_ChangeStatus(TicketStatus_Search);

	if (ent && ent->pPlayer)
	{
		ticketData.pAccountName = StructAllocString(ent->pPlayer->publicAccountName);
		ticketData.accessLevel = ent->pPlayer->accessLevel;
		if (gGCLState.pwAccountName[0])
			ticketData.pPWAccountName = strdup(gGCLState.pwAccountName);
	}
	else
		ticketData.pAccountName = StructAllocString(LoginGetAccountName());

	while (pTicketSearchStart && IS_WHITESPACE(*pTicketSearchStart))
		pTicketSearchStart++; // trim leading
	while (pTicketSearchEnd > pTicketSearchStart && IS_WHITESPACE(*pTicketSearchEnd))
	{
		*pTicketSearchEnd = 0;
		pTicketSearchEnd--;
	}
	ticketData.pKeyword = pTicketSearchStart ? pTicketSearchStart : NULL;

	if (gclServerIsConnected())
	{
		ticketData.pDebugPosString = gclGetDebugPosString();
		ServerCmd_sendTicketLabelRequest(&ticketData);
	}
	else
	{
		ticketData.pProduct = GetProductName();
		ticketTrackerSendLabelRequest(&ticketData, Category_UpdateTicketListCallback, NULL);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Category_TicketUpdate");
void Category_SendTicketUpdateRequest(void)
{
	/*if (sSelectedMainCategory && sSelectedMainCategory->bHidden)
	{
		sSelectedTicket = NULL;
		eaDestroyStruct(&sTicketLabelList, parse_TicketRequestResponse);
		TicketUI_ForceTicketListReload();
	} else */
	if (sSelectedMainCategory && sCurrentCategory && sCurrentCategory->cbSelected && sCurrentCategory->cbText)
	{
		char *pLabel = NULL;
		sCurrentCategory->cbLabel(&pLabel, sCurrentCategory->cbSelected());
		Category_SendTicketUpdateRequestHelper(REF_STRING_FROM_HANDLE(sSelectedMainCategory->hDisplayName), 
			sCurrentCategory->category, pLabel);
	}
	else if (sSelectedMainCategory && sSelectedCategory)
	{
		Category_SendTicketUpdateRequestHelper(REF_STRING_FROM_HANDLE(sSelectedMainCategory->hDisplayName), 
			REF_STRING_FROM_HANDLE(sSelectedCategory->hDisplayName), NULL);
	}
	else
	{
		sSelectedTicket = NULL; 
		eaDestroyStruct(&sTicketLabelList, parse_TicketRequestResponse);
		TicketUI_ForceTicketListReload();
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void Category_UpdateTicketList(const char *ticketResponse)
{
	TicketRequestResponseWrapper *pWrapper = StructCreate(parse_TicketRequestResponseWrapper);
	TicketRequestResponseList *pList = StructCreate(parse_TicketRequestResponseList);
	UIGen *pGen = ui_GenFind("Ticket_MainList", kUIGenTypeList);
	int i, size;

	ugc_ReportBugSearchResult( ticketResponse );

	Ticket_ChangeStatus(TicketStatus_SearchInput);
	if (ticketResponse && *ticketResponse)
	{
		if (*ticketResponse != '\r' && *ticketResponse != '\n' && *ticketResponse != '{')
		{
			// failed
			estrCopy2(&spResponseKey, ticketResponse);
			Ticket_ChangeStatus(TicketStatus_Error);
		}
		else if (ParserReadText(ticketResponse, parse_TicketRequestResponseWrapper, pWrapper, 0))
		{
			if (!pWrapper->pListString || !pWrapper->pTPIString ||
				!ParserReadTextSafe(pWrapper->pListString, pWrapper->pTPIString, pWrapper->uCRC, parse_TicketRequestResponseList, pList, 0))
			{
				// failed
			}
		}
		else 
		{
			// failed
			estrCopy2(&spResponseKey, ticketResponse);
			Ticket_ChangeStatus(TicketStatus_Error);
		}
	}
	else
	{
		// failed
		estrCopy2(&spResponseKey, "CTicket.Failure");
		Ticket_ChangeStatus(TicketStatus_Error);
	}

	sSelectedTicket = NULL;
	eaDestroyStruct(&sTicketLabelList, parse_TicketRequestResponse);
	eaCopyStructs(&pList->ppTickets, &sTicketLabelList, parse_TicketRequestResponse);

	size = eaSize(&sTicketLabelList);
	for (i=0; i<size; i++)
	{
		if (sTicketLabelList[i]->pDescription)
		{
			estrCopy2(&sTicketLabelList[i]->pDescriptionBreak, sTicketLabelList[i]->pDescription);
			estrReplaceOccurrences(&sTicketLabelList[i]->pDescriptionBreak, "\n", "<br>");
		}
		if (sTicketLabelList[i]->pResponse)
		{
			estrCopy2(&sTicketLabelList[i]->pResponseBreak, sTicketLabelList[i]->pResponse);
			estrReplaceOccurrences(&sTicketLabelList[i]->pResponseBreak, "\n", "<br>");
		}
	}

	StructDestroy(parse_TicketRequestResponseWrapper, pWrapper);
	StructDestroy(parse_TicketRequestResponseList, pList);

	// create dummy and append it to end
	TicketList_AppendCreateNew();
	TicketUI_DisableButton("Ticket_Submit", 1);
	
	ui_GenSendMessage(pGen, "Category_HasTickets");
	TicketUI_ForceTicketListReload();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_ConvertTime");
SA_RET_NN_STR char * Ticket_FiledTime(SA_PARAM_NN_VALID TicketRequestResponse *ticket)
{
	static char datetime[256];
	timeMakeLocalDateStringFromSecondsSince2000(datetime, ticket->uFiledTime);
	return datetime;
}

void FormatTimeElapsedString(char **estr, U32 diffTime)
{
	if (diffTime >= SECONDS_PER_DAY)
	{
		U32 time = diffTime / SECONDS_PER_DAY;
		FormatGameMessageKey(estr, "Ticket.Time.Days", STRFMT_INT("Count", time), STRFMT_END);
	}
	else if (diffTime >= SECONDS_PER_HOUR)
	{
		U32 time = diffTime / SECONDS_PER_HOUR;
		FormatGameMessageKey(estr, "Ticket.Time.Hours", STRFMT_INT("Count", time), STRFMT_END);
	}
	else if (diffTime >= 60)
	{
		U32 time = diffTime / 60;
		FormatGameMessageKey(estr, "Ticket.Time.MinutesShort", STRFMT_INT("Count", time), STRFMT_END);
	}
	else
	{
		FormatGameMessageKey(estr, "Ticket.Time.SecondsShort", STRFMT_INT("Count", diffTime), STRFMT_INT("Count", diffTime), STRFMT_END);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_LastTime");
SA_RET_NN_STR char * Ticket_LastTime(SA_PARAM_NN_VALID TicketRequestResponse *ticket)
{
	static char *datetime = NULL;
	U32 curTime = timeSecondsSince2000();
	U32 diffTime = curTime < ticket->uLastTime ? 0 : curTime - ticket->uLastTime;

	if (ticket->uID == 0)
		return "";
	estrClear(&datetime);
	FormatTimeElapsedString(&datetime, diffTime);
	return datetime;
}

// UIGen

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_GetList");
void Ticket_GetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &sTicketLabelList, parse_TicketRequestResponse);
}
// Create a new ticket.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cTicketGen(void)
{
	sTicketActiveWindow = sTicketStatus = TicketStatus_SubmissionInput;
	ui_GenAddWindow(ui_GenFind("TicketCreate_Root", kUIGenTypeMovableBox));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_SearchBack");
void TicketGen_BackToSearch(void)
{
	UIGen *pGen = ui_GenFind("TicketCreate_Main", kUIGenTypeBox);
	sTicketActiveWindow = sTicketStatus = TicketStatus_SearchInput;
	if (pGen)
		ui_GenSetState(pGen, kUIGenStateUser1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_KillMe");
void TickGen_KillMe(void)
{
	ServerCmd_player_killme();
}

static void TicketGen_CloseAll(void)
{
	UIGen *pGen = ui_GenFind("TicketCreate_Root", kUIGenTypeMovableBox);
	if (pGen)
	{
		ui_GenUnsetState(pGen, kUIGenStateUser);
	}
	pGen = ui_GenFind("TicketWindow_Root", kUIGenTypeMovableBox);
	if (pGen)
	{
		ui_GenUnsetState(pGen, kUIGenStateUser);
	}
}

/////////////////////////////////////////
// Ticket Sending Code
/////////////////////////////////////////

static void ticket_Cleanup(TicketData *pTicketData)
{
	if (pTicketData->pTriviaList)
	{
		free(pTicketData->pTriviaList);
		pTicketData->pTriviaList = NULL;
	}
	if (pTicketData->imagePath)
	{
		if (fileExists(pTicketData->imagePath))
			fileForceRemove(pTicketData->imagePath);
		free(pTicketData->imagePath);
		pTicketData->imagePath = NULL;
	}
	StructDestroy(parse_TicketData, pTicketData);
}

void WaitForTicketResponse(TimedCallback *callback, F32 timeSinceLastCallback, void *pData)
{
	TicketData *pTicketData = (TicketData*) pData;
	if (!s_bInSend)
	{
		ticket_Cleanup(pTicketData);
		return;
	}
	if (!bReceivedResponse)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_RESPONSE_TIMEOUT))
		{
			char *dialogTitle = NULL, *dialogMsg = NULL;
			estrStackCreate(&dialogTitle);
			estrStackCreate(&dialogMsg);
			FormatGameMessageKey(&dialogTitle, "CTicket.Title", STRFMT_END);
			FormatGameMessageKey(&dialogMsg, "CBug.Errors.ServerTimeout", STRFMT_END);
			ui_DialogPopup(dialogTitle,dialogMsg);
			estrDestroy(&dialogTitle);
			estrDestroy(&dialogMsg);
			ticket_Cleanup(pTicketData);
			s_bInSend = false;
			return;
		}
		TimedCallback_Run(WaitForTicketResponse, pTicketData, 1);
		return;
	}

	// "Create Ticket Entry (Cryptic Internal)"
	if (spResponseKey && spResponseKey[0])
	{
		if (suLastTicketID && siResponseResult == TICKETFLAGS_SUCCESS)
		{
			char *successString = NULL;
			if (stricmp(spResponseKey, "CTicket.Success") == 0)
				FormatGameMessageKey(&successString, "CTicket.Success", STRFMT_INT("id", suLastTicketID), STRFMT_END);
			else
				FormatGameMessageKey(&successString, spResponseKey, STRFMT_END);
			notify_NotifySend(NULL, kNotifyType_TicketCreated, successString, NULL, NULL);
			TicketGen_CloseAll();
			estrDestroy(&successString);
		}
		else
		{
			char * msg = NULL;
			FormatGameMessageKey(&msg, spResponseKey, STRFMT_END);
			Ticket_ChangeStatus(TicketStatus_Error);
			estrDestroy(&msg);
		}
	}
	else
	{
		// Default assumes success
		char *successString = NULL;
		FormatGameMessageKey(&successString, "CTicket.Success", STRFMT_INT("id", suLastTicketID), STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_TicketCreated, successString, NULL, NULL);
		TicketGen_CloseAll();
		estrDestroy(&successString);
	}
	/*else
	{
		if (sendToTicketTracker(pTicketData, pTicketData->imagePath))
		{
			char *successString = NULL;
			FormatGameMessageKey(&successString, "CTicket.Success", STRFMT_INT("id", suLastTicketID), STRFMT_END);
			notify_NotifySend(NULL, kNotifyType_TicketCreated, successString, NULL, NULL);
			TicketGen_CloseAll();
			estrDestroy(&successString);
		}
		else
		{
			Ticket_ChangeStatus(TicketStatus_Error);
		}
	}*/
	ticket_Cleanup(pTicketData);
	s_bInSend = false;
}

static void ticketSendingStep2(TicketData *pTicketData)
{
	char *pUserDataString = NULL;
	if (sCurrentCategory && sCurrentCategory->cbSelected)
	{
		const void *selected = sCurrentCategory->cbSelected();
		if (selected)
		{
			if (sCurrentCategory->cbText)
				sCurrentCategory->cbText(&pUserDataString, selected);
			if (sCurrentCategory->cbLabel)
				sCurrentCategory->cbLabel(&pTicketData->pTicketLabel, selected);
		}
	}

	if (gclServerIsConnected())
	{
		bReceivedResponse = false;
		siResponseResult = 0;
		start_tick = GetTickCount();
		if (pTicketData->imagePath && *pTicketData->imagePath)
		{
			char *pTicketString = NULL;
			ParserWriteText(&pTicketString, parse_TicketData, pTicketData, 0, 0, 0);
	
			sendTicketWithScreenshot(1, pTicketString, pUserDataString, pTicketData->imagePath);
			estrDestroy(&pTicketString);
		}
		else
		{
			ServerCmd_sendTicket(pTicketData, pUserDataString);
		}
		TimedCallback_Run(WaitForTicketResponse, pTicketData, 1);
	}
	else
	{
		if (sendToTicketTracker(pTicketData, pTicketData->imagePath))
		{
			char *successString = NULL;
			FormatGameMessageKey(&successString, "CTicket.Success", STRFMT_INT("id", suLastTicketID), STRFMT_END);
			notify_NotifySend(NULL, kNotifyType_TicketCreated, successString, NULL, NULL);
			TicketGen_CloseAll();
			estrDestroy(&successString);
		}
		else
		{
			char *dialogTitle = NULL, *dialogMsg = NULL;
			estrStackCreate(&dialogTitle);
			estrStackCreate(&dialogMsg);
			FormatGameMessageKey(&dialogTitle, "CTicket.Title", STRFMT_END);
			FormatGameMessageKey(&dialogMsg, "CTicket.Failure", STRFMT_END);
			ui_DialogPopup(dialogTitle,dialogMsg);
			estrDestroy(&dialogTitle);
			estrDestroy(&dialogMsg);
		}
		if (pTicketData->pTriviaList)
		{
			free(pTicketData->pTriviaList);
			pTicketData->pTriviaList = NULL;
		}
		if (pTicketData->imagePath)
		{
			free(pTicketData->imagePath);
			pTicketData->imagePath = NULL;
		}
		StructDestroy(parse_TicketData, pTicketData);
		s_bInSend = false;
	}
	estrDestroy (&pUserDataString);
}

void ticket_DelayedSend(TimedCallback *callback, F32 timeSinceLastCallback, TicketData *ticket)
{
	int i, size = eaSize(&sppDelayedFuncs);

	for (i=size-1; i>=0; i--)
	{
		void *pStruct = NULL;
		ParseTable *pti = NULL;
		
		sppDelayedFuncs[i]->cb(&pStruct, &pti, &ticket->pTicketLabel);

		if (pStruct)
		{
			assertmsg(pti, "No Parse Table set.");
			putUserDataIntoTicket(ticket, pStruct, pti);
			StructDestroyVoid(pti, pStruct);
			eaRemoveFast(&sppDelayedFuncs, i);
			size--;
		}
	}

	if (size > 0)
	{
		TimedCallback_Run(ticket_DelayedSend, ticket, 0.5f);
	}
	else
	{
		ticketSendingStep2(ticket);
	}
}

static void ticketPopulateAndSend(const char *pMainCategory, const char *pCategory, const char *pSummary, const char *pDescription, 
								  const char *imagepath, int iMergeID, TicketVisibility eVisibility, const char *pLabel)
{
	Entity *pEnt = entActivePlayerPtr();
	TicketData *pTicketData = StructCreate(parse_TicketData);
	NOCONST(TriviaList) *ptList = calloc(1, sizeof(TriviaList));

	if (pCategory && strstri(pCategory, "MemLeak"))
	{
		char *memory_dump=NULL;
		memMonitorDisplayStatsInternal(estrConcatHandler, &memory_dump, 50);
		triviaPrintf("MemoryDump", "%s", memory_dump);
		estrDestroy(&memory_dump);
	}

	pTicketData->pPlatformName = strdup(PLATFORM_NAME);
	pTicketData->pProductName = strdup(GetProductName());
	pTicketData->pVersionString = strdup(GetUsefulVersionString());

	if (pEnt && pEnt->pPlayer)
	{
		pTicketData->uAccountID = entGetAccountID(pEnt);
		pTicketData->pAccountName = strdup(pEnt->pPlayer->privateAccountName);
		pTicketData->pDisplayName = StructAllocString(pEnt->pPlayer->publicAccountName);
		if (gGCLState.pwAccountName[0])
			pTicketData->pPWAccountName = strdup(gGCLState.pwAccountName);
		pTicketData->uCharacterID = pEnt->myContainerID;
		pTicketData->pCharacterName = strdup(pEnt->pSaved->savedName);
		pTicketData->uIsInternal = (pEnt->pPlayer->accessLevel >= ACCESS_GM);
	}
	else
	{
		pTicketData->pAccountName = StructAllocString(LoginGetAccountName());
		pTicketData->pDisplayName = StructAllocString(LoginGetDisplayName());
		pTicketData->pShardInfoString = StructAllocString(LoginGetShardInfo());
	}

	estrCopy2(&pTicketData->pMainCategory, pMainCategory);
	estrCopy2(&pTicketData->pCategory, pCategory);
	pTicketData->pSummary = pSummary ? strdup(pSummary) : NULL;
	pTicketData->pUserDescription = pDescription ? strdup(pDescription) : NULL;
	if (pLabel)
		estrCopy2(&pTicketData->pTicketLabel, pLabel);

	pTicketData->iProductionMode = isProductionMode();
	ptList->triviaDatas = (NOCONST(TriviaData)**) g_ppTrivia;
	pTicketData->pTriviaList = (TriviaList*) ptList;
	pTicketData->iMergeID = iMergeID;
	pTicketData->imagePath = imagepath ? strdup(imagepath) : NULL;

	pTicketData->eLanguage = entGetLanguage(pEnt);
	pTicketData->eVisibility = eVisibility;
	
	eaDestroy(&sppDelayedFuncs);
	cBug_RunCustomDataCallbacks(pTicketData, pCategory);

	if (sppDelayedFuncs)
		ticket_DelayedSend(NULL, 0, pTicketData); // call it immediately
	else
		ticketSendingStep2(pTicketData);
	
	Ticket_ChangeStatus(TicketStatus_Submit);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_SendClose");
bool Ticket_CloseTicket(SA_PARAM_OP_VALID TicketRequestResponse *ticket)
{
	if (!ticket || ticket->bVisible)
		return false;
	// TODO figure out how to show feedback
	if (gclServerIsConnected())
	{
		TicketData *pTicketData = StructCreate(parse_TicketData);
		Entity *ent = entActivePlayerPtr();
		if (ent && ent->pPlayer)
			pTicketData->pAccountName = StructAllocString(ent->pPlayer->privateAccountName);
		pTicketData->iMergeID = ticket->uID;
	
		bReceivedResponse = false;
		siResponseResult = 0;
		start_tick = GetTickCount();

		ServerCmd_sendTicketClose(pTicketData);
		TimedCallback_Run(WaitForTicketResponse, pTicketData, 1);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_IsEditable");
bool Ticket_IsEditable(SA_PARAM_OP_VALID TicketRequestResponse *ticket)
{
	if (!ticket)
		return false;
	// Not Editable if the ticket is:
	//   1) Visible
	//   2) Closed or Resolved
	//   3) Has more than one subscriber
	if (ticket->bVisible || ticket->uSubscribedAccounts > 1)
		return false;
	if (strstri(ticket->pStatus, "Closed") || strstri(ticket->pStatus, "Resolved"))
		return false;
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_SendEdit");
bool Ticket_SendEdit(SA_PARAM_OP_VALID TicketRequestResponse *ticket, SA_PARAM_NN_STR const char *summary, SA_PARAM_NN_STR const char *description)
{
	if (!ticket || ticket->bVisible)
		return false;
	if (gclServerIsConnected())
	{
		TicketData *pTicketData = StructCreate(parse_TicketData);
		Entity *ent = entActivePlayerPtr();
		if (ent && ent->pPlayer)
			pTicketData->pAccountName = StructAllocString(ent->pPlayer->privateAccountName);
		pTicketData->iMergeID = ticket->uID;
		pTicketData->pSummary = StructAllocString(summary);
		pTicketData->pUserDescription = StructAllocString(description);
	
		bReceivedResponse = false;
		siResponseResult = 0;
		start_tick = GetTickCount();

		ServerCmd_sendTicketEdit(pTicketData);
		TimedCallback_Run(WaitForTicketResponse, pTicketData, 1);
		return true;
	}
	return false;
}

AUTO_STRUCT;
typedef struct PendingTicket
{
	char *pMainCategory;
	char *pCategory;
	char *pSummary;
	char *pDescription;
	char *imagepath;
	int iMergeID;
	TicketVisibility eVisibility;
	char *pLabel;
} PendingTicket;

static void cTicketGen_ScreenshotCB(char *fileName, PendingTicket *ticket)
{
	ticketPopulateAndSend(ticket->pMainCategory, ticket->pCategory, ticket->pSummary, ticket->pDescription, ticket->imagepath,
		ticket->iMergeID, ticket->eVisibility, ticket->pLabel);
	StructDestroy(parse_PendingTicket, ticket);
}
static void WaitForTicketWindowClose(TimedCallback *callback, F32 timeSinceLastCallback, PendingTicket *ticket)
{
	gfxSaveJPGScreenshotWithUIOverrideCallback(szScreenshotsFile, 60, cTicketGen_ScreenshotCB, ticket);
}
static void ticket_GetScreenshotAndPopulate(const char *pMainCategory, const char *pCategory, const char *pSummary, const char *pDescription, 
								  const char *imagepath, int iMergeID, TicketVisibility eVisibility, const char *pLabel)
{
	PendingTicket *ticket = StructCreate(parse_PendingTicket);
	gfxConsoleEnable(0); // Hide the console if it's shown
	ticket->pMainCategory = StructAllocString(pMainCategory);
	ticket->pCategory = StructAllocString(pCategory);
	ticket->pSummary = StructAllocString(pSummary);
	ticket->pDescription = StructAllocString(pDescription);
	ticket->imagepath = StructAllocString(imagepath);
	ticket->iMergeID = iMergeID;
	ticket->eVisibility = eVisibility;
	ticket->pLabel = StructAllocString(pLabel);
	TimedCallback_Run(WaitForTicketWindowClose, ticket, 0.1f);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_CreateNew");
void Ticket_CreateNew(SA_PARAM_NN_STR const char *summary, SA_PARAM_NN_STR const char *description, ACMD_FORCETYPE(int) TicketVisibility eVisibility)
{
	char imagepath[MAX_PATH];
	int iError;
	char *estrSummary = NULL;
	char *estrDescription = NULL;

	if (!sSelectedMainCategory)
		return;

	if (s_bInSend)
		return;

	iError = StringIsInvalidDescription(summary);
	if (iError == STRINGERR_MAX_LENGTH)
	{
		char *errorString = NULL;
		langFormatMessageKey(langGetCurrent(), &errorString, "TicketSummaryFormat_MaxLengthError", 
			STRFMT_INT("value", ASCII_DESCRIPTION_MAX_LENGTH),STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_TicketError, errorString, "Summary", NULL);
		estrDestroy(&errorString);
		return;
	}
	else if (iError == STRINGERR_PROFANITY)
	{
		char *errorString = NULL;
		langFormatMessageKey(langGetCurrent(), &errorString, "TicketSummaryFormat_Profanity", STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_TicketError, errorString, "Summary", NULL);
		estrDestroy(&errorString);
		return;
	}

	iError = StringIsInvalidDescription(description);
	if (iError == STRINGERR_PROFANITY)
	{
		char *errorString = NULL;
		langFormatMessageKey(langGetCurrent(), &errorString, "TicketDescriptionFormat_Profanity", STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_TicketError, errorString, "Description", NULL);
		estrDestroy(&errorString);
		return;
	}

	s_bInSend = true;

	fileSpecialDir(szScreenshotsDir, SAFESTR(imagepath));
	strcat(imagepath, "/");
	strcat(imagepath, szScreenshotsFile);
	
	estrCopyWithHTMLEscaping(&estrSummary, summary, false);
	estrCopyWithHTMLEscaping(&estrDescription, description, false);
	ticket_GetScreenshotAndPopulate(REF_STRING_FROM_HANDLE(sSelectedMainCategory->hDisplayName), 
		sSelectedCategory ? REF_STRING_FROM_HANDLE(sSelectedCategory->hDisplayName) : NULL, 
		estrSummary && estrSummary[0] ? estrSummary : "<No Summary>", 
		estrDescription && estrDescription[0] ? estrDescription: "<No Description>", 
		imagepath, 0, eVisibility, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_CreateNewWithLabel");
void Ticket_CreateNewWithLabel(SA_PARAM_NN_STR const char *summary, SA_PARAM_NN_STR const char *description, ACMD_FORCETYPE(int) TicketVisibility eVisibility, SA_PARAM_NN_STR const char *label)
{
	char imagepath[MAX_PATH];
	int iError;
	char *estrSummary = NULL;
	char *estrDescription = NULL;

	if (!sSelectedMainCategory)
		return;

	if (s_bInSend)
		return;

	iError = StringIsInvalidSummaryInternal(summary);
	if (iError == STRINGERR_MAX_LENGTH)
	{
		char *errorString = NULL;
		langFormatMessageKey(langGetCurrent(), &errorString, "TicketSummaryFormat_MaxLengthError", 
			STRFMT_INT("value", ASCII_DESCRIPTION_MAX_LENGTH),STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_TicketError, errorString, "Summary", NULL);
		estrDestroy(&errorString);
		return;
	}
	else if (iError == STRINGERR_PROFANITY)
	{
		char *errorString = NULL;
		langFormatMessageKey(langGetCurrent(), &errorString, "TicketSummaryFormat_Profanity", STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_TicketError, errorString, "Summary", NULL);
		estrDestroy(&errorString);
		return;
	}

	iError = StringIsInvalidDescription(description);
	if (iError == STRINGERR_PROFANITY)
	{
		char *errorString = NULL;
		langFormatMessageKey(langGetCurrent(), &errorString, "TicketDescriptionFormat_Profanity", STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_TicketError, errorString, "Description", NULL);
		estrDestroy(&errorString);
		return;
	}

	s_bInSend = true;

	fileSpecialDir(szScreenshotsDir, SAFESTR(imagepath));
	strcat(imagepath, "/");
	strcat(imagepath, szScreenshotsFile);
	
	estrCopyWithHTMLEscaping(&estrSummary, summary, false);
	estrCopyWithHTMLEscaping(&estrDescription, description, false);
	ticket_GetScreenshotAndPopulate(REF_STRING_FROM_HANDLE(sSelectedMainCategory->hDisplayName), 
		sSelectedCategory ? REF_STRING_FROM_HANDLE(sSelectedCategory->hDisplayName) : NULL, 
		estrSummary && estrSummary[0] ? estrSummary : "<No Summary>", 
		estrDescription && estrDescription[0] ? estrDescription: "<No Description>", 
		imagepath, 0, eVisibility, label);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Ticket_CreateNewWithItem");
void Ticket_CreateNewWithItem(SA_PARAM_NN_STR const char *summary, SA_PARAM_NN_STR const char *description, ACMD_FORCETYPE(int) TicketVisibility eVisibility,
							  SA_PARAM_NN_STR const char *pItemName, SA_PARAM_NN_VALID Entity* pEnt, S32 iBagIndex, S32 iSlotIndex)
{
	char *estrLabel = NULL;

	if (pEnt == NULL)
		return;

	if (pItemName == NULL || !pItemName[0])
	{
		Ticket_CreateNewWithLabel(summary, description, eVisibility, "");
		return;
	}

	// Create the label
	estrCreate(&estrLabel);

	if (iBagIndex == -1)
	{
		estrPrintf(&estrLabel, "Item name: %s.", pItemName);
	}
	else
	{
		// Get the inventory slot
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventorySlot *pSlot = inv_ent_GetSlotPtr(pEnt, iBagIndex, iSlotIndex, pExtract);

		// Item selection is still valid (Item is not removed/replaced after the drag-drop)
		if (pSlot && pSlot->pItem)
		{
			ParserWriteText(&estrLabel, parse_Item, pSlot->pItem, 0, 0, 0);
		}
		// Item selection is no longer valid, however we still can display the item name
		else
		{
			estrPrintf(&estrLabel, "Item name: %s. (Item details could not be retrieved because the item has been moved from its position in the inventory after being dropped into the ticket creation window.)", pItemName);
		}
	}

	// Create the ticket
	Ticket_CreateNewWithLabel(summary, description, eVisibility, estrLabel);

	// Destroy the label
	estrDestroy(&estrLabel);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketGen_SubmitTicket");
void Ticket_Submit(void)
{
	if (sSelectedTicket && sSelectedTicket->uID == 0)
	{
		//ui_GenSetState(ui_GenFind("TicketWindow_Main", kUIGenTypeBox), kUIGenStateUser1);
		cTicketGen();
	}
	else if (sSelectedMainCategory && sSelectedTicket)
	{
		{
			if (s_bInSend)
				return;
			s_bInSend = true;

			ticketPopulateAndSend(REF_STRING_FROM_HANDLE(sSelectedMainCategory->hDisplayName), 
				sSelectedCategory ? REF_STRING_FROM_HANDLE(sSelectedCategory->hDisplayName) : NULL, 
				sSelectedTicket->pSummary, sSelectedTicket->pDescription, NULL, sSelectedTicket->uID, 
				TICKETVISIBLE_UNKNOWN, NULL);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketGen_EnableSubmitButton");
void Ticket_EnableSubmitButton(SA_PARAM_OP_STR const char *summary, SA_PARAM_OP_STR const char *description, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (summary && summary[0] && description && description[0])
	{
		ui_GenUnsetState(pGen, kUIGenStateDisabled);
	}
	else
		ui_GenSetState(pGen, kUIGenStateDisabled);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TicketSelected_GetData");
const char *TicketGen_GetAndFormatData(ExprContext *pContext, const char *pchDescription)
{
	static char *s_pchDescription = NULL;
	estrClear(&s_pchDescription);
	if (!sSelectedTicket)
		FormatGameMessageKey(&s_pchDescription, "Ticket.NoTicketSelected", STRFMT_END);
	else
		FormatGameMessageKey(&s_pchDescription, pchDescription, STRFMT_STRUCT("TicketData", sSelectedTicket, parse_TicketRequestResponse), STRFMT_END);
	return s_pchDescription;
}

///////////////////////////////////////////
// Customer Service Stuff
#include "LoginCommon.h"
#include "LoginCommon_h_ast.h"

static AccountFlagUpdate sBanStruct = {0};

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclCSR_BanReturn (AccountFlagUpdate *banStruct)
{
	StructCopy(parse_AccountFlagUpdate, banStruct, &sBanStruct, 0, 0, 0);
	
	ui_GenAddWindow(ui_GenFind("ConfirmBan_Root", kUIGenTypeMovableBox));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetBanEndtime);
char *gclCSR_BanDuration (void)
{
	static char *pText = NULL;
	estrClear(&pText);
	if (sBanStruct.uCurrentExpiration)
		FormatGameMessageKey(&pText, "Bans.Current", STRFMT_STRING("User", sBanStruct.displayName),
			STRFMT_DATETIME("Time", sBanStruct.uCurrentExpiration), STRFMT_END);
	else
		FormatGameMessageKey(&pText, "Bans.CurrentIndefinite", STRFMT_STRING("User", sBanStruct.displayName), STRFMT_END);
	return pText;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OverrideBan);
void gclExprCSR_OverrideBan(void)
{
//	ServerCmd_suspend_override(sBanStruct.displayName, sBanStruct.uDuration / 3600.f);
}

// Returns all items in the inventory that can be reported as a bug or used in messages to the GMs
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetAllReportableItems");
void gclExprCSR_GetAllReportableItems(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt)
{

}

#include "AutoGen/gclBugReport_c_ast.c"