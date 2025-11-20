#include "UGCCommon.h"
#include "UGCProjectCommon.h"

#include "EntityLib.h"
#include "Player.h"
#include "timing.h"
#include "GameStringFormat.h"
#include "StringUtil.h"
#include "cmdparse.h"
#include "EditLibUIUtil.h"
#include "UIWindow.h"
#include "UIList.h"
#include "UIMenu.h"
#include "UIButton.h"
#include "UIModalDialog.h"
#include "Prefs.h"

#include "UIBoxSizer.h"

#include "gclUGC_FeaturingUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// In the UGC Featuring UI for CS reps, we are making a copy of all of these fields from UGCProject and its nested containers.
// Originally, I was using objpaths to get the data for drawing, sorting, and filtering. I changed to this approach for 2 reasons:
// 1) the overhead of ParserResolvePath was too great with thousands of rows in the model, and 2) this gives us compile-time errors
// when the containers are refactored, thus preventing us from breaking their UI accidentally.
AUTO_STRUCT;
typedef struct UGCFeaturingUIModel {
	U32 id;								AST(KEY)
	char *IdString;
	U32 CreationTime;
	char *CreationTime_AsString;
	char *ProjectName;					AST(NAME(ProjectName))
	U32 OwnerAccountID;
	char *OwnerAccountName;
	U32 SeriesID;
	char *PreviousShard;
	F32 AverageRating;
	F32 AdjustedRatingUsingConfidence;
	U32 FeaturedStartTimestamp;
	char *FeaturedStartTimestamp_AsString;
	U32 FeaturedEndTimestamp;
	char *FeaturedEndTimestamp_AsString;
	char *FeaturedDetails;
	U32 LastPublishTimestamp;
	char *LastPublishTimestamp_AsString;
	Language eLanguage;
	char *NameSpace;
	U32 AverageDuration;
} UGCFeaturingUIModel;

typedef struct UGCFeaturingUI {
	UIWindow *mainWindow;

	UIList *featuringList;

	UIMenu *commandMenu;

	UGCFeaturingUIModel **eaFeaturingUIModel;

	char **eaEstrColumns;

	char **eaCommandTitles;
	char **eaCommandInstructions;
	char **eaCommands;
} UGCFeaturingUI;

static UGCFeaturingUI s_ugcFeaturingUI = { 0 };

static void ugcFeaturingUI_CreateDateString(char **strResult, U32 iTimestamp)
{
	char *estrTemp = NULL;
	estrPrintf(&estrTemp, "%s utc", timeGetDateStringFromSecondsSince2000(iTimestamp));
	(*strResult) = strdup(estrTemp);
	estrDestroy(&estrTemp);
}

static void ugcFeaturingUI_CreateFeaturedDateString(char **strResult, U32 iTimestamp)
{
	if(0 != iTimestamp)
		ugcFeaturingUI_CreateDateString(strResult, iTimestamp);
	else
		(*strResult) = strdup("<OPEN ENDED>");
}

static void ugcFeaturingUI_CreateAndAddModel(const UGCProject *pUGCProject)
{
	UGCFeaturingUIModel *pUGCFeaturingUIModel = StructCreate(parse_UGCFeaturingUIModel);
	pUGCFeaturingUIModel->id = pUGCProject->id;
	pUGCFeaturingUIModel->IdString = strdup(pUGCProject->pIDString);
	pUGCFeaturingUIModel->CreationTime = pUGCProject->iCreationTime;
	ugcFeaturingUI_CreateDateString(&pUGCFeaturingUIModel->CreationTime_AsString, pUGCFeaturingUIModel->CreationTime);
	pUGCFeaturingUIModel->ProjectName = strdup(pUGCProject->pPublishedVersionName);
	pUGCFeaturingUIModel->OwnerAccountID = pUGCProject->iOwnerAccountID;
	pUGCFeaturingUIModel->OwnerAccountName = strdup(pUGCProject->pOwnerAccountName);
	pUGCFeaturingUIModel->SeriesID = pUGCProject->seriesID;
	pUGCFeaturingUIModel->PreviousShard = strdup(pUGCProject->strPreviousShard);
	pUGCFeaturingUIModel->AverageRating = pUGCProject->ugcReviews.fAverageRating;
	pUGCFeaturingUIModel->AdjustedRatingUsingConfidence = pUGCProject->ugcReviews.fAdjustedRatingUsingConfidence;
	pUGCFeaturingUIModel->FeaturedStartTimestamp = pUGCProject->pFeatured ? pUGCProject->pFeatured->iStartTimestamp : 0;
	if(pUGCProject->pFeatured)
		ugcFeaturingUI_CreateFeaturedDateString(&pUGCFeaturingUIModel->FeaturedStartTimestamp_AsString, pUGCFeaturingUIModel->FeaturedStartTimestamp);
	else
		pUGCFeaturingUIModel->FeaturedStartTimestamp_AsString = strdup("");
	pUGCFeaturingUIModel->FeaturedEndTimestamp = pUGCProject->pFeatured ? pUGCProject->pFeatured->iEndTimestamp : 0;
	if(pUGCProject->pFeatured)
		ugcFeaturingUI_CreateFeaturedDateString(&pUGCFeaturingUIModel->FeaturedEndTimestamp_AsString, pUGCFeaturingUIModel->FeaturedEndTimestamp);
	else
		pUGCFeaturingUIModel->FeaturedEndTimestamp_AsString = strdup("");
	pUGCFeaturingUIModel->FeaturedDetails = strdup(pUGCProject->pFeatured ? pUGCProject->pFeatured->strDetails : "");
	pUGCFeaturingUIModel->LastPublishTimestamp = UGCProject_GetMostRecentPublishedVersion(pUGCProject)->sLastPublishTimeStamp.iTimestamp;
	ugcFeaturingUI_CreateDateString(&pUGCFeaturingUIModel->LastPublishTimestamp_AsString, pUGCFeaturingUIModel->LastPublishTimestamp);
	pUGCFeaturingUIModel->eLanguage = UGCProject_GetMostRecentPublishedVersion(pUGCProject)->eLanguage;
	pUGCFeaturingUIModel->NameSpace = strdup(UGCProject_GetMostRecentPublishedVersion(pUGCProject)->pNameSpace);
	pUGCFeaturingUIModel->AverageDuration = UGCProject_AverageDurationInMinutes(pUGCProject);
	eaIndexedPushUsingIntIfPossible(&s_ugcFeaturingUI.eaFeaturingUIModel, pUGCFeaturingUIModel->id, pUGCFeaturingUIModel);
}

static bool ugcFeaturingUI_CloseCallback(UIAnyWidget *widget, UserData userdata_unused)
{
	S32 i;

	GamePrefStoreFloat("UGCFeaturingUI.x", ui_WidgetGetX((UIWidget*)widget));
	GamePrefStoreFloat("UGCFeaturingUI.y", ui_WidgetGetY((UIWidget*)widget));
	GamePrefStoreFloat("UGCFeaturingUI.w", ui_WidgetGetWidth((UIWidget*)widget));
	GamePrefStoreFloat("UGCFeaturingUI.h", ui_WidgetGetHeight((UIWidget*)widget));

	for(i = 0; i < eaSize(&s_ugcFeaturingUI.featuringList->eaColumns); i++)
	{
		static char *hidden_str = NULL;

		UIListColumn *col = s_ugcFeaturingUI.featuringList->eaColumns[i];
		estrPrintf(&hidden_str, "UGCFeaturingUI.HiddenColumns.%s", s_ugcFeaturingUI.eaEstrColumns[i]);
		GamePrefStoreInt(hidden_str, col->bHidden);

		estrDestroy(&hidden_str);
	}

	ui_ListSetModel(s_ugcFeaturingUI.featuringList, NULL, NULL);

	eaDestroyStruct(&s_ugcFeaturingUI.eaFeaturingUIModel, parse_UGCFeaturingUIModel);

	return 1;
}

static void ugcFeaturingUI_CreateAndAddColumn(int langID, const char *pchTitleMsgKey, const char *pchFieldName)
{
	static char *hidden_str = NULL;

	UIListColumn *pUIListColumn = ui_ListColumnCreateParseName(langTranslateMessageKey(langID, pchTitleMsgKey), pchFieldName, NULL);
	ui_ListColumnSetWidth(pUIListColumn, true, 1.0f);
	estrPrintf(&hidden_str, "UGCFeaturingUI.HiddenColumns.%s", pchTitleMsgKey);
	ui_ListColumnSetHidden(pUIListColumn, GamePrefGetInt(hidden_str, 0));
	ui_ListColumnSetSortable(pUIListColumn, true);

	ui_ListAppendColumn(s_ugcFeaturingUI.featuringList, pUIListColumn);

	eaPush(&s_ugcFeaturingUI.eaEstrColumns, strdup(pchTitleMsgKey));

	estrDestroy(&hidden_str);
}

static void ugcFeaturingUI_CreateAndAddCommand(int langID, const char *pchTitleMsgKey, const char *pchInstructionsMsgKey, const char *pchCommand)
{
	eaPush(&s_ugcFeaturingUI.eaCommandTitles, strdup(pchTitleMsgKey));
	eaPush(&s_ugcFeaturingUI.eaCommandInstructions, strdup(pchInstructionsMsgKey));
	eaPush(&s_ugcFeaturingUI.eaCommands, strdup(pchCommand));
}

static void ugcFeaturingUI_CreateAndAddCommandSeparator()
{
	eaPush(&s_ugcFeaturingUI.eaCommandTitles, "");
	eaPush(&s_ugcFeaturingUI.eaCommandInstructions, "");
	eaPush(&s_ugcFeaturingUI.eaCommands, "");
}

static UIDialogButtons s_ugcFeaturedUI_CommandReturn;

bool ugcFeaturedUI_CommandDialogCloseCB(UIWindow* ignored, UserData ignored2)
{
	ui_ModalDialogLoopExit();

	s_ugcFeaturedUI_CommandReturn = UICancel;

	return true;
}

static void ugcFeaturedUI_CommandDialogCB(UIWindow* ignored, UserData rawDialogButton)
{
	ui_ModalDialogLoopExit();

	s_ugcFeaturedUI_CommandReturn = (UIDialogButtons)rawDialogButton;
}

UIDialogButtons ugcFeaturedUI_CommandDialog(const char* title, const char* instructions, const char *projectName, char **eaEstrParams, char ***peaValues)
{
	UIGlobalState oldState = {0};

	UIWindow *window = NULL;
	UILabel *label = NULL;
	UITextEntry **eaTextEntries = NULL;
	UIBoxSizer *mainVerticalSizer = ui_BoxSizerCreate(UIVertical);

	ui_ModalDialogBeforeWidgetAdd(&oldState);

	window = ui_WindowCreate("", 0, 0, 0, 0);
	ui_WidgetSetTextMessage(UI_WIDGET(window), title);
	ui_WindowSetCloseCallback(window, ugcFeaturedUI_CommandDialogCloseCB, NULL);
	ui_WidgetSetSizer(UI_WIDGET(window), UI_SIZER(mainVerticalSizer));

	label = ui_LabelCreateWithMessage(instructions, 0, 0);
	ui_LabelSetWordWrap(label, true);
	ui_WidgetSetDimensions(UI_WIDGET(label), 500, 150);
	ui_BoxSizerAddWidget(mainVerticalSizer, UI_WIDGET(label), 0, UIWidth, 0);

	ui_BoxSizerAddSpacer(mainVerticalSizer, 16);

	label = ui_LabelCreate(projectName, 0, 0);
	ui_BoxSizerAddWidget(mainVerticalSizer, UI_WIDGET(label), 0, UIWidth, 0);

	ui_BoxSizerAddSpacer(mainVerticalSizer, 16);

	// Make text entry params
	{
		FOR_EACH_IN_EARRAY_FORWARDS(eaEstrParams, char, estrParam)
		{
			UITextEntry *text = NULL;

			label = ui_LabelCreateWithMessage(estrParam, 0, 0);
			ui_BoxSizerAddWidget(mainVerticalSizer, UI_WIDGET(label), 0, UIWidth, 2);

			text = ui_TextEntryCreate("", 0, 0);
			ui_BoxSizerAddWidget(mainVerticalSizer, UI_WIDGET(text), 0, UIWidth, 2);

			eaPush(&eaTextEntries, text);
		}
		FOR_EACH_END;
	}

	// Make buttons
	{
		struct {
			UIDialogButtons flag;
			const char* messageKey;
		} list[] = {
			{ UICancel, "UGC.Cancel" },
			{ UIOk, "UGC.Ok" }
		};

		UIBoxSizer *buttonHorizontalSizer = ui_BoxSizerCreate(UIHorizontal);
		int it;
		for(it = 0; it < ARRAY_SIZE(list); it++)
		{
			UIButton *button = ui_ButtonCreate("", 0, 0, ugcFeaturedUI_CommandDialogCB, (UserData)list[it].flag);
			ui_ButtonSetMessage(button, list[it].messageKey);
			ui_WidgetSetDimensions(UI_WIDGET(button), 80, 22);

			ui_BoxSizerAddWidget(buttonHorizontalSizer, UI_WIDGET(button), 0, UINoDirection, 5);
		}

		ui_BoxSizerAddSizer(mainVerticalSizer, UI_SIZER(buttonHorizontalSizer), 0, UIRight, 0);
	}

	// Set window initial and minimum size based on sizer
	{
		Vec2 minSize;
		ui_SizerGetMinSize(UI_SIZER(mainVerticalSizer), minSize);

		ui_WindowSetDimensions(window, minSize[0], minSize[1], minSize[0], minSize[1]);
	}

	elUICenterWindow(window);
	ui_WindowShow(window);

	ui_ModalDialogLoop();

	if(UIOk == s_ugcFeaturedUI_CommandReturn)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(eaTextEntries, UITextEntry, text)
		{
			eaPush(peaValues, strdup(ui_TextEntryGetText(text)));
		}
		FOR_EACH_END;
	}
	eaDestroy(&eaTextEntries);

	ui_WindowFreeInternal(window);

	ui_ModalDialogAfterWidgetDestroy(&oldState);

	return s_ugcFeaturedUI_CommandReturn;
}

static void ugcFeaturingUI_CommandCB(UIMenuItem *pMenuItem, UserData rawCommandIndex)
{
	int commandIndex = (int)(intptr_t)rawCommandIndex;

	char *strCommandTitle = s_ugcFeaturingUI.eaCommandTitles[commandIndex];
	char *strCommandInstructions = s_ugcFeaturingUI.eaCommandInstructions[commandIndex];
	char *strCommand = s_ugcFeaturingUI.eaCommands[commandIndex];

	const UGCFeaturingUIModel *pUGCFeaturingUIModel = (UGCFeaturingUIModel *)pMenuItem->data.voidPtr;

	char **eaEstrParams = NULL;
	char **eaValues = NULL;
	char *pStr = strstr(strCommand, "$(");
	while(pStr)
	{
		char *pStrStart = &pStr[2];
		char *pStrEnd = strstr(pStr, ")");
		if(!strStartsWith(pStrStart, "Model."))
		{
			char *estrParam = NULL;
			estrConcat(&estrParam, pStrStart, pStrEnd - pStrStart);
			eaPush(&eaEstrParams, estrParam);
		}
		pStr = strstr(pStrEnd, "$(");
	}

	if(UIOk == ugcFeaturedUI_CommandDialog(strCommandTitle, strCommandInstructions, pUGCFeaturingUIModel->ProjectName, eaEstrParams, &eaValues))
	{
		char *pCommand = estrCreateFromStr(strCommand);
		char *pCommandEnd = strchr(pCommand, ' ');
		if(pCommandEnd)
		{
			estrSetSize(&pCommand, pCommandEnd - pCommand);
			pStr = strstr(strCommand, "$(");
			while(pStr)
			{
				char *pStrStart = &pStr[2];
				char *pStrEnd = strstr(pStr, ")");
				if(!strStartsWith(pStrStart, "Model."))
				{
					char *value = eaRemove(&eaValues, 0);
					// TODO: escape double quotes in estrValue?
					estrConcatf(&pCommand, " \"%s\"", value);
					free(value);
				}
				else
				{
					char *estrParam = NULL;
					estrConcat(&estrParam, pStrStart, pStrEnd - pStrStart);
					if(0 == strcmpi(estrParam, "Model.id")) // TODO: support other model members?
						estrConcatf(&pCommand, " %u", pUGCFeaturingUIModel->id);
					estrDestroy(&estrParam);
				}
				pStr = strstr(pStrEnd, "$(");
			}

			globCmdParse(pCommand);
		}

		eaDestroyEString(&eaEstrParams);
		eaDestroyEString(&eaValues);
		estrDestroy(&pCommand);
	}
}

static void ugcFeaturingUI_ListCellContextCB(UIList *pUnused, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData unused)
{
	if(iRow >= 0 && iRow == ui_ListGetSelectedRow(s_ugcFeaturingUI.featuringList))
	{
		UGCFeaturingUIModel *pUGCFeaturingUIModel = eaGet(s_ugcFeaturingUI.featuringList->peaModel, iRow);
		UIMenuItem *item = NULL;

		if(s_ugcFeaturingUI.commandMenu)
			ui_MenuClearAndFreeItems(s_ugcFeaturingUI.commandMenu);
		else
			s_ugcFeaturingUI.commandMenu = ui_MenuCreate("");

		FOR_EACH_IN_EARRAY_FORWARDS(s_ugcFeaturingUI.eaCommandTitles, char, strCommandTitle)
		{
			int i = FOR_EACH_IDX(s_ugcFeaturingUI.eaCommandTitles, strCommandTitle);
			char *strCommandInstructions = s_ugcFeaturingUI.eaCommandInstructions[i];
			char *strCommand = s_ugcFeaturingUI.eaCommands[i];

			if(nullStr(strCommandTitle) || nullStr(strCommand))
				ui_MenuAppendItem(s_ugcFeaturingUI.commandMenu, ui_MenuItemCreateSeparator());
			else
			{
				item = ui_MenuItemCreateMessage(strCommandTitle, UIMenuCallback, ugcFeaturingUI_CommandCB, (UserData)(intptr_t)i, pUGCFeaturingUIModel);
				ui_MenuAppendItem(s_ugcFeaturingUI.commandMenu, item);
			}
		}
		FOR_EACH_END;

		ui_MenuPopupAtCursor(s_ugcFeaturingUI.commandMenu);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(4);
void gclUGC_FeaturedShowAuthorAllowsFeaturedList(Entity *pEntity, UGCProjectList* pList)
{
	UIListColumn *pUIListColumn = NULL;

	// Create the main window
	F32 x = GamePrefGetFloat("UGCFeaturingUI.x", 5);
	F32 y = GamePrefGetFloat("UGCFeaturingUI.y", 5);
	F32 w = GamePrefGetFloat("UGCFeaturingUI.w", 600);
	F32 h = GamePrefGetFloat("UGCFeaturingUI.h", 600);

	eaDestroyStruct(&s_ugcFeaturingUI.eaFeaturingUIModel, parse_UGCFeaturingUIModel);
	eaIndexedEnable(&s_ugcFeaturingUI.eaFeaturingUIModel, parse_UGCFeaturingUIModel);
	FOR_EACH_IN_EARRAY_FORWARDS(pList->eaProjects, UGCProject, pUGCProject)
	{
		ugcFeaturingUI_CreateAndAddModel(pUGCProject);
	}
	FOR_EACH_END;

	if(s_ugcFeaturingUI.mainWindow)
	{
		ui_ListSetModel(s_ugcFeaturingUI.featuringList, parse_UGCFeaturingUIModel, &s_ugcFeaturingUI.eaFeaturingUIModel);
	}
	else
	{
		s_ugcFeaturingUI.mainWindow = ui_WindowCreate(entTranslateMessageKey(pEntity, "UGC.Featuring.UGCFeaturedContent"), x, y, w, h);
		ui_WindowSetDimensions(s_ugcFeaturingUI.mainWindow, w, h, 500, 200);
		ui_WindowSetCloseCallback(s_ugcFeaturingUI.mainWindow, ugcFeaturingUI_CloseCallback, NULL);

		s_ugcFeaturingUI.featuringList = ui_ListCreate(parse_UGCFeaturingUIModel, &s_ugcFeaturingUI.eaFeaturingUIModel, /*fRowHeight=*/17);
		ui_ListSetAutoColumnContextMenu(s_ugcFeaturingUI.featuringList, true);
		ui_ListSetCellContextCallback(s_ugcFeaturingUI.featuringList, ugcFeaturingUI_ListCellContextCB, NULL);
		ui_ListSetSortOnlyOnUIChange(s_ugcFeaturingUI.featuringList, true);

		ugcFeaturingUI_CreateAndAddCommand(entGetLanguage(pEntity), "UGC.Featuring.CopyProject", "UGC.Featuring.CopyProject.Instructions",
			"ugcFeatured_CopyProject $(Model.ID) $(UGC.Featuring.FeaturedDetails) $(UGC.Featuring.FeaturedStart) $(UGC.Featuring.FeaturedEnd)");

		ugcFeaturingUI_CreateAndAddCommandSeparator();

		ugcFeaturingUI_CreateAndAddCommand(entGetLanguage(pEntity), "UGC.Featuring.ArchiveProject", "UGC.Featuring.ArchiveProject.Instructions",
			"ugcFeatured_ArchiveProject $(Model.ID) $(UGC.Featuring.FeaturedEnd)");

		ugcFeaturingUI_CreateAndAddCommand(entGetLanguage(pEntity), "UGC.Featuring.RemoveProject", "UGC.Featuring.RemoveProject.Instructions",
			"ugcFeatured_RemoveProject $(Model.ID)");

		ugcFeaturingUI_CreateAndAddCommand(entGetLanguage(pEntity), "UGC.Featuring.FeatureProject", "UGC.Featuring.FeatureProject.Instructions",
			"ugcFeatured_AddProject $(Model.ID) $(UGC.Featuring.FeaturedDetails) $(UGC.Featuring.FeaturedStart) $(UGC.Featuring.FeaturedEnd)");

		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.ID", "id");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.IdString", "IdString");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.CreationTime", "CreationTime_AsString");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.OwnerAccountID", "OwnerAccountID");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.OwnerAccountName", "OwnerAccountName");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.SeriesID", "SeriesID");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.ImportedFromShard", "PreviousShard");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.AverageRating", "AverageRating");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.AdjustedRating", "AdjustedRatingUsingConfidence");

		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.FeaturedStart", "FeaturedStartTimestamp_AsString");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.FeaturedEnd", "FeaturedEndTimestamp_AsString");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.FeaturedDetails", "FeaturedDetails");

		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.ProjectName", "ProjectName");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.LastPublishTimeStamp", "LastPublishTimestamp_AsString");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.ProjectLanguage", "Language");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.Namespace", "NameSpace");
		ugcFeaturingUI_CreateAndAddColumn(entGetLanguage(pEntity), "UGC.Featuring.AverageDuration", "AverageDuration");

		// LAYOUT
		{
			UILabel *label = NULL;
			UIBoxSizer *mainVerticalSizer = ui_BoxSizerCreate(UIVertical);
			ui_WidgetSetSizer(UI_WIDGET(s_ugcFeaturingUI.mainWindow), UI_SIZER(mainVerticalSizer));

			label = ui_LabelCreateWithMessage("UGC.Featuring.Instructions.Header", 0, 0);
			ui_BoxSizerAddWidget(mainVerticalSizer, UI_WIDGET(label), 0, UIWidth, 0);

			ui_BoxSizerAddSpacer(mainVerticalSizer, 16);

			label = ui_LabelCreateWithMessage("UGC.Featuring.Instructions.Sort", 0, 0);
			ui_BoxSizerAddWidget(mainVerticalSizer, UI_WIDGET(label), 0, UIWidth, 0);

			label = ui_LabelCreateWithMessage("UGC.Featuring.Instructions.ShowHide", 0, 0);
			ui_BoxSizerAddWidget(mainVerticalSizer, UI_WIDGET(label), 0, UIWidth, 0);

			label = ui_LabelCreateWithMessage("UGC.Featuring.Instructions.Commands", 0, 0);
			ui_BoxSizerAddWidget(mainVerticalSizer, UI_WIDGET(label), 0, UIWidth, 0);

			ui_BoxSizerAddWidget(mainVerticalSizer, UI_WIDGET(s_ugcFeaturingUI.featuringList), 1, UIWidth, 5);
		}
	}

	ui_WindowShow(s_ugcFeaturingUI.mainWindow);
}

#include "gclUGC_FeaturingUI_c_ast.c"
