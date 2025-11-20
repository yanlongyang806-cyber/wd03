#include "cmdParse.h"
#include "GlobalStateMachine.h"
#include "gclBaseStates.h"
#include "Expression.h"
#include "file.h"

#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "GlobalTypes.h"
#include "inputKeybind.h"

#include "UILib.h"

#include "gclQuickPlay.h"
#include "gclQuickPlay_h_ast.h"

#include "LoginCommon.h"
#include "gcllogin.h"
#include "Login2Common.h"
#include "AutoGen/Login2Common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static char **s_eachScripts;

KeyBindProfile s_OverrideProfile = {"QuickPlay Overrides", __FILE__, NULL, true, true};

static UIList *s_pChoiceList;
static UIButton *s_pBackButton;

static QuickPlayMenu **s_eaChosenMenus;

static QuickPlayMenus s_AllMenus;

static Login2CharacterCreationData *s_quickplayCreationData;

static char *s_pchMapName;

static int giEnableQuickplay;

AUTO_CMD_INT(giEnableQuickplay, EnableQuickplay) ACMD_COMMANDLINE;
 
__forceinline static bool gclQuickplay_IsEnabled(void)
{
	return giEnableQuickplay || isDevelopmentMode() || gConf.bAlwaysQuickplay ||g_iQuickLogin;
}

AUTO_STARTUP(QuickPlay);
void gclQuickPlay_Load(void)
{
	if ( gclQuickplay_IsEnabled() )
	{
		loadstart_printf("Loading QuickPlay data... ");
		StructDeInit(parse_QuickPlayMenus, &s_AllMenus);
		ParserLoadFiles("defs/quickplay/", ".quickplay", "QuickPlay.bin", PARSER_OPTIONALFLAG, parse_QuickPlayMenus, &s_AllMenus);
		loadend_printf("done (%d entries)", eaSize(&s_AllMenus.eaMenus));
	}
}

void gclQuickPlay_PushBinds(void)
{
	keybind_PushProfileEx(&s_OverrideProfile, InputBindPriorityDevelopment);
}

void gclQuickPlay_PopBinds(void)
{
	keybind_PopProfileEx(&s_OverrideProfile, InputBindPriorityDevelopment);
}

void gclQuickPlay_ExecuteScripts(void)
{
	S32 i;
	char achCommand[1024];
	// FIXME JFW - This needs to actually call a function rather than be lame.
	for (i = 0; i < eaSize(&s_eachScripts); i++)
	{
		sprintf(achCommand, "exec %s", s_eachScripts[i]);
		globCmdParse(achCommand);
	}
	eaDestroy(&s_eachScripts);
}

void gclQuickPlay_PushChoice(QuickPlayMenu *pMenu)
{
	eaPushUnique(&s_eaChosenMenus, pMenu);
	if (eaSize(&pMenu->eaSubMenus) > 1)
	{
		ui_ListSetModel(s_pChoiceList, parse_QuickPlayMenu, &pMenu->eaSubMenus);
		ui_SetFocus(s_pChoiceList);
		ui_ListSetSelectedRow(s_pChoiceList, 0);
	}
	else if (eaSize(&pMenu->eaSubMenus) == 1)
		gclQuickPlay_PushChoice(pMenu->eaSubMenus[0]);
	else
		GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOGIN);
}

void gclQuickPlay_PopChoice(void)
{
	QuickPlayMenu *pChosen;
	eaPop(&s_eaChosenMenus);
	pChosen = eaTail(&s_eaChosenMenus);
	while (pChosen && eaSize(&pChosen->eaSubMenus) == 1)
	{
		eaPop(&s_eaChosenMenus);
		pChosen = eaTail(&s_eaChosenMenus);
	}
	if (pChosen)
		ui_ListSetModel(s_pChoiceList, parse_QuickPlayMenu, &pChosen->eaSubMenus);
	else
		ui_ListSetModel(s_pChoiceList, parse_QuickPlayMenu, &s_AllMenus.eaMenus);
	ui_SetFocus(s_pChoiceList);
	ui_ListSetSelectedRow(s_pChoiceList, 0);
}

void gclQuickPlay_QueueCommand(const char *pchCommand)
{
	eaPush(&s_eachScripts, strdup(pchCommand));
}

static void gclQuickPlayer_ListActivated(UIList *pList, UserData pDummy)
{
	QuickPlayMenu *pMenu = ui_ListGetSelectedObject(pList);
	if (pMenu)
		gclQuickPlay_PushChoice(pMenu);
}

void gclQuickPlay_Enter(void)
{
	gclQuickPlay_Reset();
	s_pChoiceList = ui_ListCreate(parse_QuickPlayMenu, NULL, 18);

	ui_ListAppendColumn(s_pChoiceList, ui_ListColumnCreateParseName("Quick Play Menu", "DisplayName", NULL));
	ui_WidgetSetDimensionsEx(UI_WIDGET(s_pChoiceList), 0.7, 0.7, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(s_pChoiceList), 0, 0, 0, 0, UINoDirection);
	ui_ListSetActivatedCallback(s_pChoiceList, gclQuickPlayer_ListActivated, NULL);

	eaDestroy(&s_eachScripts);
	eaDestroyStruct(&s_eaChosenMenus, parse_QuickPlayMenu);

	if (eaSize(&s_AllMenus.eaMenus) == 1)
		gclQuickPlay_PushChoice(s_AllMenus.eaMenus[0]);
	else
		ui_ListSetModel(s_pChoiceList, parse_QuickPlayMenu, &s_AllMenus.eaMenus);
	ui_WidgetAddToDevice(UI_WIDGET(s_pChoiceList), NULL);
	ui_SetFocus(s_pChoiceList);
	ui_ListSetSelectedRow(s_pChoiceList, 0);
}

void gclQuickPlay_OncePerFrame(void)
{
	AtlasTex *pWhite = atlasLoadTexture("white");
	ui_SetFocus(s_pChoiceList);
	display_sprite(pWhite, 0, 0, 1.f, g_ui_State.screenWidth / pWhite->width, g_ui_State.screenHeight / pWhite->height, 0xEEEEEEFF);
}

void gclQuickPlay_QuickPlayFillCharacter(QuickPlayMenu ***peaChosenMenus, Login2CharacterCreationData *characterCreationData)
{
	const char *pchPlayerName = NULL;
	const char *pchMapName = NULL;
	S32 i;
	for (i = 0; i < eaSize(&s_eaChosenMenus); i++)
	{
		S32 j;
		QuickPlayMenu *pMenu = s_eaChosenMenus[i];
		if (pMenu->pInfo)
			StructCopyFields(parse_Login2CharacterCreationData, pMenu->pInfo, characterCreationData, 0, 0);
		eaPushEArray(&s_eachScripts, &pMenu->eachScripts);
		for (j = 0; j < eaSize(&pMenu->eaBinds); j++)
			gclQuickPlay_AddKeyBind(pMenu->eaBinds[j]);
		if (pMenu->pchPlayerName)
			pchPlayerName = pMenu->pchPlayerName;
		if (pMenu->pchMapName)
			pchMapName = pMenu->pchMapName;
	}

	if (pchPlayerName)
    {
		characterCreationData->name = StructAllocString(pchPlayerName);
    }

	s_pchMapName = StructAllocString(pchMapName);
}

void gclQuickPlay_Leave(void)
{
	s_quickplayCreationData = StructCreate(parse_Login2CharacterCreationData);
	gclQuickPlay_QuickPlayFillCharacter(&s_eaChosenMenus, s_quickplayCreationData);
	eaDestroy(&s_eaChosenMenus);
	ui_WidgetDestroy(&s_pBackButton);
	ui_WidgetDestroy(&s_pChoiceList);
}

void gclQuickPlay_FillDefaultCharacter(Login2CharacterCreationData *characterCreationData)
{
	int i;
	eaClear(&s_eaChosenMenus);
	for (i = 0; i < eaSize(&s_AllMenus.eaMenus); i++)
	{
		if (s_AllMenus.eaMenus[i]->bDefaultCharacter)
		{
			eaPush(&s_eaChosenMenus, s_AllMenus.eaMenus[i]);
		}
	}
	gclQuickPlay_QuickPlayFillCharacter(&s_eaChosenMenus, characterCreationData);
}

void gclQuickPlay_AddKeyBind(const KeyBind *pBind)
{
	keybind_BindKeyInProfile(&s_OverrideProfile, pBind->pchKey, pBind->pchCommand);
}

AUTO_RUN;
void gclQuickPlay_Register(void)
{
	GSM_AddGlobalState(GCL_QUICK_PLAY);
	GSM_AddGlobalStateCallbacks(GCL_QUICK_PLAY, gclQuickPlay_Enter, NULL, gclQuickPlay_OncePerFrame, gclQuickPlay_Leave);
}

// Return the number of Quick Play profiles found.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("QuickPlayProfileCount");
S32 gclQuickPlay_CountQuickPlayProfiles(void)
{
	return eaSize(&s_AllMenus.eaMenus);
}

AUTO_COMMAND ACMD_NAME("QuickPlay.Start") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug) ACMD_HIDE;
void gclQuickPlay_Start(void)
{
	if ( gclQuickplay_IsEnabled() )
	{
		GSM_SwitchToState_Complex(GCL_BASE "/" GCL_QUICK_PLAY);
	}
}

void gclQuickPlay_Reset(void)
{
	StructFreeString(s_pchMapName);
	s_pchMapName = NULL;
	StructDestroySafe(parse_Login2CharacterCreationData, &s_quickplayCreationData);
	gclQuickPlay_Load();
}

Login2CharacterCreationData *gclQuickPlay_GetCharacterCreationData(void)
{
	return s_quickplayCreationData;
}

const char *gclQuickPlay_GetMapName(void)
{
	return s_pchMapName;
}


#include "gclQuickPlay_h_ast.c"
