/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "earray.h"
#include "Expression.h"

#include "UIGen.h"
#include "UIGenColorChooser.h"
#include "UIGenList.h"

#include "EntityIterator.h"
#include "EntityLib.h"
#include "EntityResolver.h"
#include "Guild.h"
#include "gclBaseStates.h"
#include "gclEntity.h"
#include "gclUIGen.h"
#include "Guild_h_ast.h"
#include "gclChat.h"
#include "GlobalStateMachine.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "TextFilter.h"
#include "StringUtil.h"
#include "AutoTransDefs.h"
#include "inventoryCommon.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "UIColor.h"
#include "StringCache.h"
#include "GraphicsLib.h"
#include "species_common.h"
#include "WorldGrid.h"
#include "gclFriendsIgnore.h"
#include "chatCommonStructs.h"
#include "EntitySavedData.h"
#include "Character.h"
#include "CharacterClass.h"
#include "guildCommonStructs.h"
#include "pub/gclEntity.h"
#include "contact_common.h"
#include "NotifyCommon.h"
#include "GuildUI.h"
#include "GroupProjectCommon.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/GuildUI_h_ast.h"
#include "AutoGen/GuildUI_c_ast.h"
#include "AutoGen/itemEnums_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

GuildEmblemList g_GuildEmblems;

AUTO_ENUM;
typedef enum
{
	GuildNameTakenStatus_Reserved	= -1,
	GuildNameTakenStatus_Pending	=  0,
	GuildNameTakenStatus_Available	=  1,
} GuildNameTakenStatus;

static GuildNameTakenStatus g_pGuildNameTaken = GuildNameTakenStatus_Pending;
static U32 s_uGuildOwnerID = 0;

AUTO_STRUCT;
typedef struct GuildMapGuest
{
	const char* pchName; AST(UNOWNED)
	ContainerID uEntID;
} GuildMapGuest;

void gclGuild_GameplayEnter(void)
{
	s_uGuildOwnerID = 0;
	if (zmapInfoGetIsGuildOwned(NULL))
	{
		ServerCmd_RequestGuildMapOwner();
	}
}

void gclGuild_GameplayLeave(void)
{
	s_uGuildOwnerID = 0;
}

AUTO_RUN;
void gclGuildUI_SetupUI(void)
{
	ui_GenInitStaticDefineVars(GuildEventReplyTypeEnum, "GuildEventReplyType_");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_NAME(ReceiveGuildMapOwner) ACMD_CATEGORY(Guild);
void gclGuild_ReceiveGuildMapOwner(U32 uGuildID)
{
	if (GSM_IsStateActive(GCL_GAMEPLAY))
	{
		s_uGuildOwnerID = uGuildID;
	}
}

static void gclGuild_ValidateEmblems(void)
{
	int i;

	for(i=eaSize(&g_GuildEmblems.eaEmblems)-1; i>=0; --i) {
		GuildEmblem *pEmblem = g_GuildEmblems.eaEmblems[i];
		if (!GET_REF(pEmblem->hTexture)) {
			if (REF_STRING_FROM_HANDLE(pEmblem->hTexture)) {
				ErrorFilenamef("defs/config/GuildEmblems.def", "A Guild Emblem is refers to non-existent costume texture '%s'.  Note that this texture must be player legal or it won't be present on the client where this is tested.", REF_STRING_FROM_HANDLE(pEmblem->hTexture));
			} else {
				ErrorFilenamef("defs/config/GuildEmblems.def", "A Guild Emblem is missing its costume texture choice");
			}
		}
	}
}

static void gclGuild_ReloadEmblems(const char *pcRelPath, int UNUSED_when)
{
	fileWaitForExclusiveAccess(pcRelPath);
	if (g_GuildEmblems.eaEmblems) {
		int i;
		for (i = eaSize(&g_GuildEmblems.eaEmblems)-1; i >= 0; i--) {
			StructDestroy(parse_GuildEmblem, g_GuildEmblems.eaEmblems[i]);
		}
		eaClear(&g_GuildEmblems.eaEmblems);
	}
	ParserLoadFiles(NULL, "defs/config/GuildEmblems.def", "GuildEmblems.bin", PARSER_OPTIONALFLAG, parse_GuildEmblemList, &g_GuildEmblems);

	gclGuild_ValidateEmblems();
}

AUTO_STARTUP(Guilds) ASTRT_DEPS(EntityCostumes);
void gclGuild_run_InitEmblems(void)
{
	ParserLoadFiles(NULL, "defs/config/GuildEmblems.def", "GuildEmblems.bin", PARSER_OPTIONALFLAG, parse_GuildEmblemList, &g_GuildEmblems);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE | FOLDER_CACHE_CALLBACK_DELETE, "defs/config/GuildEmblems.def", gclGuild_ReloadEmblems);

	gclGuild_ValidateEmblems();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void gclGuild_ccmd_NameTakenReturn(bool bIsTaken)
{
	if ( bIsTaken == false )
	{
		g_pGuildNameTaken = GuildNameTakenStatus_Available;
	}
	else
	{
		g_pGuildNameTaken = GuildNameTakenStatus_Reserved;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Slash Commands
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Interface, Social) ACMD_NAME(guild_nametaken);
void gclGuild_cmd_NameTaken(const ACMD_SENTENCE pcName)
{
	//reset the status
	g_pGuildNameTaken = GuildNameTakenStatus_Pending;

	ServerCmd_gslGuild_NameValid(pcName);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Expression Functions
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_GetCreatedDate);
U32 gclGuild_expr_GetCreatedDate(SA_PARAM_OP_VALID Entity* pEnt)
{
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			return pGuild->iCreatedOn;
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_isnamevalid);
bool gclGuild_expr_IsNameValid(const char* pcNewName)
{
	//don't allow invalid name
	return StringIsValidGuildName( pcNewName );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_resetnametakenstatus);
void gclGuild_expr_ResetNameTakenStatus(void)
{
	g_pGuildNameTaken = GuildNameTakenStatus_Pending;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_isinvitee");
bool gclGuild_expr_IsInvitee( SA_PARAM_OP_VALID Entity* pEnt )
{
	bool invited = false;

	if(SAFE_MEMBER2(pEnt, pPlayer, pGuild))
	{
		Guild *pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		invited = guild_IsInvite(pEnt) && pGuild;

		if ( invited && ( pEnt->pPlayer != NULL ) && ( pEnt->pPlayer->pUI != NULL ) &&
			( pEnt->pPlayer->pUI->pChatState != NULL ) && ( pEnt->pPlayer->pUI->pChatState->eaIgnores != NULL )	)
		{
			int i;
			int n;
			n = eaSize(&pEnt->pPlayer->pUI->pChatState->eaIgnores);
			for ( i = 0; i < n; i++ )
			{
				if ( pEnt->pPlayer->pUI->pChatState->eaIgnores[i]->chatHandle && pEnt->pPlayer->pGuild->pcInviterHandle && !stricmp(pEnt->pPlayer->pUI->pChatState->eaIgnores[i]->chatHandle, pEnt->pPlayer->pGuild->pcInviterHandle) )
				{
					return false;
				}
			}
		}
	}

	return invited;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_create");
void gclGuild_expr_Create(const char* pcName, U32 iColor1, U32 iColor2, const char* pcEmblem)
{
	ServerCmd_Guild_Create(iColor1, iColor2, pcEmblem, "", pcName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_createex");
void gclGuild_expr_CreateEx(const char* pcName, U32 iColor1, U32 iColor2, const char* pcEmblem, const char *pcTheme)
{
	ServerCmd_Guild_CreateEx(iColor1, iColor2, pcEmblem, "", pcTheme, pcName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_createwithdescription");
void gclGuild_expr_CreateWithDescription(const char* pcName, U32 iColor1, U32 iColor2, const char* pcEmblem, const char* pcDescription)
{
	ServerCmd_Guild_Create(iColor1, iColor2, pcEmblem, pcDescription, pcName);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_nametaken");
void gclGuild_expr_NameTaken( const char* pcName )
{
	//reset the status
	g_pGuildNameTaken = GuildNameTakenStatus_Pending;

	gclGuild_cmd_NameTaken(pcName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getnamestatus");
S32 gclGuild_expr_GetNameStatus()
{
	return g_pGuildNameTaken;
}

AUTO_STRUCT;
typedef struct LeagueEmblemData {
	const char*				pcName;		AST(UNOWNED)
	const char*				pcTextureName; AST(UNOWNED)
} LeagueEmblemData;

AUTO_STRUCT;
typedef struct GuildThemeUIElement
{
	// The logical name for the theme
	const char *pchName; AST(POOL_STRING)

	// The display name for the theme
	const char *pchDisplayName; AST(UNOWNED)

	// The description for the theme
	const char *pchDescription; AST(UNOWNED)
} GuildThemeUIElement;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_setemblem");
void gclGuild_expr_SetEmblem( const char* pcEmblemName )
{
	ServerCmd_Guild_SetEmblem(pcEmblemName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_setadvancedemblem");
void gclGuild_expr_SetAdvancedEmblem( const char* pcEmblemName, U32 iEmblemColor0, U32 iEmblemColor1, F32 fEmblemRotation )
{
	ServerCmd_Guild_SetAdvancedEmblem(pcEmblemName, iEmblemColor0, iEmblemColor1, fEmblemRotation, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_setadvancedemblem2");
void gclGuild_expr_SetAdvancedEmblem2( const char* pcEmblem2Name, U32 iEmblem2Color0, U32 iEmblem2Color1, F32 fEmblem2Rotation, F32 fEmblem2X, F32 fEmblem2Y, F32 fEmblem2ScaleX, F32 fEmblem2ScaleY )
{
	ServerCmd_Guild_SetAdvancedEmblem2(pcEmblem2Name, iEmblem2Color0, iEmblem2Color1, fEmblem2Rotation, fEmblem2X, fEmblem2Y, fEmblem2ScaleX, fEmblem2ScaleY);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_setadvancedemblem3");
void gclGuild_expr_SetAdvancedEmblem3( const char* pcEmblem3Name )
{
	ServerCmd_Guild_SetAdvancedEmblem3(pcEmblem3Name, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getemblemname");
const char* gclGuild_expr_GetEmblemName( SA_PARAM_OP_VALID Entity* pEnt )
{
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if (pGuild && pGuild->pcEmblem)
		{
			return pGuild->pcEmblem;
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getemblem2name");
const char* gclGuild_expr_GetEmblem2Name( SA_PARAM_OP_VALID Entity* pEnt )
{
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if (pGuild && pGuild->pcEmblem2)
		{
			return pGuild->pcEmblem2;
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getemblem3name");
const char* gclGuild_expr_GetEmblem3Name( SA_PARAM_OP_VALID Entity* pEnt )
{
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if (pGuild && pGuild->pcEmblem3)
		{
			return pGuild->pcEmblem3;
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getemblemdisplayname");
const char* gclGuild_expr_GetEmblemDisplayName(const char* pcInternalName)
{
	S32 i;
	if (stricmp("None", pcInternalName)==0)
	{
		return TranslateMessageKey("None");
	}
	for (i = eaSize(&g_GuildEmblems.eaEmblems)-1; i >= 0; --i)
	{
		const char* pchName = REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[i]->hTexture);
		if (stricmp(pchName, pcInternalName)==0)
		{
			PCTextureDef* pTexDef = GET_REF(g_GuildEmblems.eaEmblems[i]->hTexture);
			if (pTexDef)
			{
				return TranslateDisplayMessage(pTexDef->displayNameMsg);
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getemblemlist");
void gclGuild_expr_GetEmblemList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static LeagueEmblemData** s_eaEmblemData = NULL;
	S32 i, iCount = 0;

	for (i = 0; i < eaSize(&g_GuildEmblems.eaEmblems); i++)
	{
		PCTextureDef* pTexDef = GET_REF(g_GuildEmblems.eaEmblems[i]->hTexture);
		if (g_GuildEmblems.eaEmblems[i]->bFalse) continue;
		if (pTexDef)
		{
			LeagueEmblemData* pEmblemData = eaGetStruct(&s_eaEmblemData, parse_LeagueEmblemData, iCount++);
			pEmblemData->pcName = TranslateDisplayMessage(pTexDef->displayNameMsg);
			pEmblemData->pcTextureName = pTexDef->pcName;
		}
	}

	eaSetSizeStruct(&s_eaEmblemData, parse_LeagueEmblemData, iCount);

	ui_GenSetList(pGen, &s_eaEmblemData, parse_LeagueEmblemData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getthemelist");
void gclGuild_expr_GetThemeList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static GuildThemeUIElement** s_eaThemes = NULL;
	S32 iCount = 0;

	GuildThemeDef *pGuildThemeDef;

	// The dictionary iterator
	RefDictIterator itGuildThemes;

	// Initialize the iterator
	RefSystem_InitRefDictIterator(g_GuildThemeDictionary, &itGuildThemes);

	// Iterate through all referents in the dictionary
	while(pGuildThemeDef = RefSystem_GetNextReferentFromIterator(&itGuildThemes))
	{
		devassert(pGuildThemeDef);

		if (pGuildThemeDef)
		{
			GuildThemeUIElement *pThemeUIElement = eaGetStruct(&s_eaThemes, parse_GuildThemeUIElement, iCount++);
			pThemeUIElement->pchName = allocAddString(pGuildThemeDef->pchName);
			pThemeUIElement->pchDisplayName = TranslateDisplayMessage(pGuildThemeDef->displayName);
			pThemeUIElement->pchDescription = TranslateDisplayMessage(pGuildThemeDef->description);
		}
	}

	eaSetSizeStruct(&s_eaThemes, parse_GuildThemeUIElement, iCount);

	ui_GenSetList(pGen, &s_eaThemes, parse_GuildThemeUIElement);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getfrontemblemlist");
void gclGuild_expr_GetFrontEmblemList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static LeagueEmblemData** s_eaEmblemData = NULL;
	S32 i, iCount = 0;

	{
		LeagueEmblemData* pEmblemData = eaGetStruct(&s_eaEmblemData, parse_LeagueEmblemData, iCount++);
		pEmblemData->pcName = TranslateMessageKey("None");
		pEmblemData->pcTextureName = NULL;
	}

	for (i = 0; i < eaSize(&g_GuildEmblems.eaEmblems); i++)
	{
		PCTextureDef* pTexDef = GET_REF(g_GuildEmblems.eaEmblems[i]->hTexture);
		if (g_GuildEmblems.eaEmblems[i]->bDetail || g_GuildEmblems.eaEmblems[i]->bBackground || g_GuildEmblems.eaEmblems[i]->bFalse) continue;
		if (pTexDef)
		{
			LeagueEmblemData* pEmblemData = eaGetStruct(&s_eaEmblemData, parse_LeagueEmblemData, iCount++);
			pEmblemData->pcName = TranslateDisplayMessage(pTexDef->displayNameMsg);
			pEmblemData->pcTextureName = pTexDef->pcName;
		}
	}

	eaSetSizeStruct(&s_eaEmblemData, parse_LeagueEmblemData, iCount);

	ui_GenSetList(pGen, &s_eaEmblemData, parse_LeagueEmblemData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getbackemblemlist");
void gclGuild_expr_GetBackEmblemList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static LeagueEmblemData** s_eaEmblemData = NULL;
	S32 i, iCount = 0;

	{
		LeagueEmblemData* pEmblemData = eaGetStruct(&s_eaEmblemData, parse_LeagueEmblemData, iCount++);
		pEmblemData->pcName = TranslateMessageKey("None");
		pEmblemData->pcTextureName = NULL;
	}

	for (i = 0; i < eaSize(&g_GuildEmblems.eaEmblems); i++)
	{
		PCTextureDef* pTexDef = GET_REF(g_GuildEmblems.eaEmblems[i]->hTexture);
		if (g_GuildEmblems.eaEmblems[i]->bDetail || !g_GuildEmblems.eaEmblems[i]->bBackground || g_GuildEmblems.eaEmblems[i]->bFalse) continue;
		if (pTexDef)
		{
			LeagueEmblemData* pEmblemData = eaGetStruct(&s_eaEmblemData, parse_LeagueEmblemData, iCount++);
			pEmblemData->pcName = TranslateDisplayMessage(pTexDef->displayNameMsg);
			pEmblemData->pcTextureName = pTexDef->pcName;
		}
	}

	eaSetSizeStruct(&s_eaEmblemData, parse_LeagueEmblemData, iCount);

	ui_GenSetList(pGen, &s_eaEmblemData, parse_LeagueEmblemData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getdetailemblemlist");
void gclGuild_expr_GetDetailEmblemList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static LeagueEmblemData** s_eaEmblemData = NULL;
	S32 i, iCount = 0;

	for (i = 0; i < eaSize(&g_GuildEmblems.eaEmblems); i++)
	{
		PCTextureDef* pTexDef = GET_REF(g_GuildEmblems.eaEmblems[i]->hTexture);
		if (!g_GuildEmblems.eaEmblems[i]->bDetail || g_GuildEmblems.eaEmblems[i]->bFalse) continue;
		if (pTexDef)
		{
			LeagueEmblemData* pEmblemData = eaGetStruct(&s_eaEmblemData, parse_LeagueEmblemData, iCount++);
			pEmblemData->pcName = TranslateDisplayMessage(pTexDef->displayNameMsg);
			pEmblemData->pcTextureName = pTexDef->pcName;
		}
	}

	eaSetSizeStruct(&s_eaEmblemData, parse_LeagueEmblemData, iCount);

	ui_GenSetList(pGen, &s_eaEmblemData, parse_LeagueEmblemData);
}

static REF_TO(UIColorSet) hGuildColorSet;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getcolorfromindex");
SA_RET_NN_VALID UIColor* gclGuild_expr_GetColorValueFromIndex( S32 iIndex )
{
	static UIColor GuildColor = {0};
	UIColorSet *pColorSet;

	if ( !IS_HANDLE_ACTIVE( hGuildColorSet ) )
	{
		SET_HANDLE_FROM_STRING(g_hCostumeColorsDict, "Guild_Colors", hGuildColorSet);
	}

	pColorSet = GET_REF( hGuildColorSet );

	if (pColorSet)
	{
		return pColorSet->eaColors[iIndex];
	}

	return &GuildColor;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getcolorlist");
void gclGuild_expr_GetColorList(SA_PARAM_NN_VALID UIGen *pGen)
{
	UIColorSet *pColorSet = NULL;

	if ( !IS_HANDLE_ACTIVE( hGuildColorSet ) )
	{
		SET_HANDLE_FROM_STRING(g_hCostumeColorsDict, "Guild_Colors", hGuildColorSet);
	}

	pColorSet = GET_REF( hGuildColorSet );

	if (pColorSet)
	{
		ui_GenSetList(pGen, &pColorSet->eaColors, parse_UIColor);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_getcolormodel");
void gclGuild_expr_GetColorModel(SA_PARAM_NN_VALID UIGen *pGen)
{
	UIColorSet *pColorSet = NULL;

	if ( !IS_HANDLE_ACTIVE( hGuildColorSet ) )
	{
		SET_HANDLE_FROM_STRING(g_hCostumeColorsDict, "Guild_Colors", hGuildColorSet);
	}

	pColorSet = GET_REF( hGuildColorSet );

	if (pColorSet)
	{
		int **peaiColorList = ui_GenGetColorList(pGen);
		int i;
		eaiClear(peaiColorList);
		for (i = 0; i < eaSize(&pColorSet->eaColors); i++)
		{
			UIColor *pColor = pColorSet->eaColors[i];
			U32 uiColor = (((U32)pColor->color[0]) << 24) | (((U32)pColor->color[1]) << 16) | (((U32)pColor->color[2]) << 8) | ((U32)pColor->color[3]);
			eaiPush(peaiColorList, uiColor);
		}
	}
}

//struct
AUTO_STRUCT;
typedef struct GuildColorIndex {
	U32 iColorIndex;
} GuildColorIndex;


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getcolorindex);
SA_RET_NN_VALID GuildColorIndex* gclGuild_expr_GetColorIndex(SA_PARAM_OP_VALID Entity* pEnt, bool bIndex)
{
	static GuildColorIndex *pGuildColor = NULL;

	if ( pGuildColor == NULL )
	{
		pGuildColor = StructAlloc( parse_GuildColorIndex );
	}

	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			if ( bIndex == 0 )
			{
				pGuildColor->iColorIndex = pGuild->iColor1;
			}
			else
			{
				pGuildColor->iColorIndex = pGuild->iColor2;
			}
		}
	}

	return pGuildColor;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_setcolor);
void gclGuild_expr_SetColor(SA_PARAM_OP_VALID Entity* pEnt, bool bIndex, U32 iColor)
{
	if (bIndex == 0) {
		ServerCmd_Guild_SetColor1(iColor);
	} else {
		ServerCmd_Guild_SetColor2(iColor);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_setcolor1);
void gclGuild_expr_SetColor1(SA_PARAM_OP_VALID Entity* pEnt, U32 iColor)
{
	ServerCmd_Guild_SetColor1(iColor);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_setcolor2);
void gclGuild_expr_SetColor2(SA_PARAM_OP_VALID Entity* pEnt, U32 iColor)
{
	ServerCmd_Guild_SetColor2(iColor);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_SetGuildAllegiance");
void gclGuild_expr_SetGuildAllegiance(const char* pcNewAllegiance)
{
	ServerCmd_Guild_SetAllegiance(pcNewAllegiance);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_GetGuildAllegiance");
const char* gclGuild_expr_GetGuildAllegiance( SA_PARAM_OP_VALID Entity* pEnt )
{
	if (guild_IsMember( pEnt ))
	{
		if (pEnt->pPlayer!=NULL && pEnt->pPlayer->pGuild!=NULL)
		{
			Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);
	
			if (pGuild && pGuild->pcAllegiance)
			{
				return pGuild->pcAllegiance;
			}
		}
	}

	return "";
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_cansendguildmail);
bool gclGuild_expr_CanGuildMail(SA_PARAM_OP_VALID Entity* pEnt)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	if (pGuild) {
		if (pEnt->pPlayer->accessLevel < 9 && timeServerSecondsSince2000() - pGuild->iLastGuildMailTime < GUILD_MAIL_TIME) {
			return false;
		}
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_haspermissiontoguildmail);
bool gclGuild_expr_HasPermissionToGuildMail(SA_PARAM_OP_VALID Entity* pEnt)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildMember *pMember = pGuild ? guild_FindMemberInGuild(pEnt, pGuild) : NULL;
	if (pMember) {
		if (!guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildMail)) {
			return false;
		}
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_isplayeronline);
bool gclGuild_expr_IsPlayerOnline(ContainerID iContainerID)
{
	Entity* pPlayerEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iContainerID);

	if (pPlayerEntity != NULL) {
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_setmotd);
void gclGuild_expr_SetMOTD(const char* pcMotD)
{
	ServerCmd_Guild_SetMotD(pcMotD);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_setdescription);
void gclGuild_expr_SetDescription(const char* pcDescription)
{
	ServerCmd_Guild_SetDescription(pcDescription);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_renamerank);
bool gclGuild_expr_RenameRank(SA_PARAM_OP_VALID Entity* pEnt, S32 iRank, const char* pcName)
{
	//make sure we're a member of the guild
	if (guild_IsMember(pEnt)) {
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL ) {
			const char* pcExistingRankName;

			//must be a valid rank
			iRank--;
			if (iRank >= eaSize(&pGuild->eaRanks) || iRank < 0) return false;

			//must be a valid name
			if (gclGuild_expr_IsNameValid(pcName) == false) return false;

			if (pGuild->eaRanks[iRank]->pcDisplayName != 0) {
				//store the user-specified rank name
				pcExistingRankName = pGuild->eaRanks[iRank]->pcDisplayName;
			} else {
				//store the default rank name
				pcExistingRankName = TranslateMessageKey( pGuild->eaRanks[iRank]->pcDefaultNameMsg );
			}

			//make sure we are changing the name of the rank to a *NEW* name
			//and not the same name as before
			if (pcExistingRankName==NULL || strcmp(pcName, pcExistingRankName) != 0) {
				ServerCmd_Guild_RenameRank(iRank+1, pcName);
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getname);
const char* gclGuild_expr_GetName(SA_PARAM_OP_VALID Entity* pEnt)
{
	//make sure we're a member of the guild
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pcGuildName && pEnt->pPlayer->pcGuildName[0]) {
		return pEnt->pPlayer->pcGuildName;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_rename);
bool gclGuild_expr_Rename(SA_PARAM_OP_VALID Entity* pEnt, const char* pcNewName)
{
	if (gclGuild_expr_IsNameValid(pcNewName) == false) {
		return false;
	}

	//make sure we're a member of the guild
	if (guild_IsMember(pEnt)) {
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if (pGuild != NULL) {
			//make sure we are changing the name of the guild to a *NEW* name
			//and not the same name as before
			if (pGuild->pcName == NULL || strcmp(pcNewName, pGuild->pcName) != 0) {
				ServerCmd_Guild_Rename(pcNewName);
			}
		}
	}

	return true;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getmotd);
const char* gclGuild_expr_GetMOTD(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	//make sure we're a member of the guild
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL && pGuild->pcMotD != NULL )
		{
			/////// v Filter profanity in the UIGen v
			//U32 iMotdSize = (U32)strlen( pGuild->pcMotD ) + 1;
			//char* pcMotD = exprContextAllocScratchMemory( pContext, iMotdSize );

			//Entity* pPlayerEnt = entActivePlayerPtr();

			//strcpy_s( pcMotD, iMotdSize, pGuild->pcMotD );

			//// Censor profanity from "MOTD" text if the player wants profanity filtering
			//if (	pPlayerEnt
			//	&&	pPlayerEnt->pPlayer
			//	&&	pPlayerEnt->pPlayer->pUI
			//	&&	pPlayerEnt->pPlayer->pUI->pChatConfig
			//	&&	pPlayerEnt->pPlayer->pUI->pChatConfig->bProfanityFilter )
			//{
			//	ReplaceAnyWordProfane( pcMotD );
			//}

			//retrive the motd
			return pGuild->pcMotD;
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getdescription);
const char* gclGuild_expr_GetDescription(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	//make sure we're a member of the guild
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL && pGuild->pcDescription != NULL )
		{
			/////// v Filter profanity in the UIGen v
			//U32 iDescriptionSize = (U32)strlen( pGuild->pcDescription ) + 1;
			//char* pcDescription = exprContextAllocScratchMemory( pContext, iDescriptionSize );

			//Entity* pPlayerEnt = entActivePlayerPtr();

			//strcpy_s( pcDescription, iDescriptionSize, pGuild->pcDescription );

			//// Censor profanity from "Description" text if the player wants profanity filtering
			//if (	pPlayerEnt
			//	&&	pPlayerEnt->pPlayer
			//	&&	pPlayerEnt->pPlayer->pUI
			//	&&	pPlayerEnt->pPlayer->pUI->pChatConfig
			//	&&	pPlayerEnt->pPlayer->pUI->pChatConfig->bProfanityFilter )
			//{
			//	ReplaceAnyWordProfane( pcDescription );
			//}

			//retrive the Description
			return pGuild->pcDescription;
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_ismember);
bool gclGuild_expr_IsMember(SA_PARAM_OP_VALID Entity* pEnt)
{
	return guild_IsMember(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getmemberrank);
int gclGuild_expr_GetMemberRank(SA_PARAM_OP_VALID Entity* pEnt)
{
	GuildMember* pGuildMember = guild_FindMember( pEnt );
	if (pGuildMember) return pGuildMember->iRank;
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getrankname);
const char* gclGuild_expr_GetRankName(SA_PARAM_OP_VALID Entity* pEnt, S32 iRank)
{
	//make sure we're a member of the guild
	if ( guild_IsMember( pEnt ) )
	{
		const Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			//must be a valid rank
			iRank--;
			if ( iRank >= eaSize( &pGuild->eaRanks ) || iRank < 0 ) return "";

			if ( pGuild->eaRanks[iRank]->pcDisplayName != 0 )
			{
				//return the user-specified rank name
				return pGuild->eaRanks[iRank]->pcDisplayName;
			}
			else
			{
				//return the default rank name
				return TranslateMessageKey( pGuild->eaRanks[iRank]->pcDefaultNameMsg );
			}
		}
	}

	return "";
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_HasPermissionToInvite);
bool gclGuild_expr_HasPermissionToInvite(SA_PARAM_OP_VALID Entity* pEnt)
{
	GuildMember* pGuildMember = guild_FindMember(pEnt);

	if (pGuildMember != NULL) {
		const Guild *pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if (pGuild && guild_HasPermission(pGuildMember->iRank, pGuild, GuildPermission_Invite)) {
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_haspermissiontokick);
bool gclGuild_expr_HasPermissionToKick(S32 iRank, SA_PARAM_OP_VALID Entity* pEnt)
{
	GuildMember* pGuildMember = guild_FindMember( pEnt );

	//make sure we're a member of the guild
	if ( pGuildMember != NULL )
	{
		const Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			//we must be above the other member's rank to kick
			iRank--;
			if ( iRank < pGuildMember->iRank )
			{
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_haspermissiontopromote);
bool gclGuild_expr_HasPermissionToPromote(S32 iRankToPromote, SA_PARAM_OP_VALID Entity* pEnt)
{
	GuildMember* pGuildMember = guild_FindMember( pEnt );

	//make sure we're a member of the guild
	if ( pGuildMember != NULL )
	{
		const Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			//we can't promote if the current rank is the highest rank
			iRankToPromote--;
			if ( iRankToPromote >= eaSize( &pGuild->eaRanks ) - 1 ) return false;

			//you can't promote yourself if you are the only member
			if ( eaSize( &pGuild->eaMembers ) == 1 ) return false;

			//check promotoion conditions/permissions
			if ( iRankToPromote + 1 < pGuildMember->iRank ) //promote below rank
			{
				if ( guild_HasPermission( pGuildMember->iRank, pGuild, GuildPermission_PromoteBelowRank ) )
				{
					return true;
				}
			}
			else if ( iRankToPromote + 1 == pGuildMember->iRank ) //promote to rank
			{
				if ( guild_HasPermission( pGuildMember->iRank, pGuild, GuildPermission_PromoteToRank ) )
				{
					return true;
				}
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_haspermissiontodemote);
bool gclGuild_expr_HasPermissionToDemote(S32 iRankToDemote, SA_PARAM_OP_VALID Entity* pEnt)
{
	GuildMember* pGuildMember = guild_FindMember( pEnt );

	//make sure we're a member of the guild
	if ( pGuildMember != NULL )
	{
		const Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			//can't demote if the current rank is the lowest rank
			iRankToDemote--;
			if ( iRankToDemote <= 0 ) return false;

			//can't demote yourself if you're the only member
			if ( eaSize( &pGuild->eaMembers ) == 1 ) return false;

			//check demotion conditions/permissions
			if ( iRankToDemote < pGuildMember->iRank ) //demote below rank
			{
				if ( guild_HasPermission( pGuildMember->iRank, pGuild, GuildPermission_DemoteBelowRank ) )
				{
					return true;
				}
			}
			else if ( iRankToDemote == pGuildMember->iRank ) //demote at rank
			{
				if ( guild_HasPermission( pGuildMember->iRank, pGuild, GuildPermission_DemoteAtRank ) )
				{
					return true;
				}
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_haspermissiontouseconfig);
bool gclGuild_expr_HasPermissionToUseConfig(SA_PARAM_OP_VALID Entity* pEnt)
{
	GuildMember* pGuildMember = guild_FindMember( pEnt );

	//make sure we're a member of the guild
	if ( pGuildMember != NULL )
	{
		const Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			//check permission to use config
			if (	guild_HasPermission( pGuildMember->iRank, pGuild, GuildPermission_SetPermission )
				||	guild_HasPermission( pGuildMember->iRank, pGuild, GuildPermission_RenameRank )
				||	guild_HasPermission( pGuildMember->iRank, pGuild, GuildPermission_Rename )
				||	guild_HasPermission( pGuildMember->iRank, pGuild, GuildPermission_SetLook ))
			{
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_haspermissiontousebankconfig);
bool gclGuild_expr_HasPermissionToUseBankConfig(SA_PARAM_OP_VALID Entity* pEnt)
{
	GuildMember* pGuildMember = guild_FindMember( pEnt );

	//make sure we're a member of the guild
	if ( pGuildMember != NULL )
	{
		const Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			//check permission to use config
			if (	guild_HasPermission( pGuildMember->iRank, pGuild, GuildPermission_SetBankPermission )
				||	guild_HasPermission( pGuildMember->iRank, pGuild, GuildPermission_RenameBankTab ))
			{
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_haspermissiontochangemotd);
bool gclGuild_expr_HasPermissionToChangeMotD(SA_PARAM_OP_VALID Entity* pEnt)
{
	GuildMember *pGuildMember = guild_FindMember(pEnt);
	Guild *pGuild = guild_GetGuild(pEnt);

	//make sure we're a member of a guild
	if (pGuildMember && pGuild) {
		return guild_HasPermission(pGuildMember->iRank, pGuild, GuildPermission_SetMotD);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_hasexpermission);
bool gclGuild_expr_HasExPermission(SA_PARAM_OP_VALID Entity* pEnt, const char* pcPermission, const char* pcUniform, const char* pcCategory)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildCostume *pUniform = NULL;
	PlayerCostume *pCostume = NULL;
	int i;

	//make sure we're a member of a guild
	if (pGuild) {
		for (i = eaSize(&pGuild->eaUniforms)-1; i >= 0; --i)
		{
			pCostume = pGuild->eaUniforms[i]->pCostume;
			if (!pCostume) continue;
			if (!pCostume->pcName) continue;
			if (!stricmp(pCostume->pcName, pcUniform))
			{
				pUniform = pGuild->eaUniforms[i];
				break;
			}
		}

		if (!pUniform) return false;

		if (!stricmp("SpeciesNotAllowed",pcPermission))
		{
			for (i = eaSize(&pUniform->eaSpeciesNotAllowed)-1; i >= 0; --i)
			{
				SpeciesDef *pSpecies = GET_REF(pUniform->eaSpeciesNotAllowed[i]->hSpeciesRef);
				if (!pSpecies) continue;
				if (!pSpecies->pcName) continue;
				if (!stricmp(pSpecies->pcName, pcCategory)) return false;
			}
		}
		else if (!stricmp("RanksNotAllowed",pcPermission))
		{
			char *e = (char *)pcCategory;
			int cat = strtol(pcCategory, &e, 10);
			if (e == pcCategory) return false;
			for (i = ea32Size(&pUniform->eaRanksNotAllowed)-1; i >= 0; --i)
			{
				int rank = pUniform->eaRanksNotAllowed[i];
				if (rank == cat) return false;
			}
		}
		else if (!stricmp("ClassNotAllowed",pcPermission))
		{
			for (i = eaSize(&pUniform->eaClassNotAllowed)-1; i >= 0; --i)
			{
				const char *pchClass = pUniform->eaClassNotAllowed[i]->pchClass;
				if (!pchClass) continue;
				if (!stricmp(pchClass, pcCategory)) return false;
			}
		}
	}
	else
	{
		return false;
	}

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_haspermissionself);
bool gclGuild_expr_HasPermissionSelf(SA_PARAM_OP_VALID Entity* pEnt, const char* pcPermission)
{
	GuildMember *pGuildMember = guild_FindMember(pEnt);
	Guild *pGuild = guild_GetGuild(pEnt);

	//make sure we're a member of a guild
	if (pGuildMember && pGuild) {
		GuildRankPermissions ePerm = StaticDefineIntGetInt(GuildRankPermissionsEnum, pcPermission);
		return guild_HasPermission(pGuildMember->iRank, pGuild, ePerm);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_haspermission);
bool gclGuild_expr_HasPermission(SA_PARAM_OP_VALID Entity* pEnt, S32 iRank, const char* pcPermission)
{
	Guild *pGuild = guild_GetGuild(pEnt);

	//make sure we're a member of a guild
	if (pGuild) {
		GuildRankPermissions ePerm = StaticDefineIntGetInt(GuildRankPermissionsEnum, pcPermission);
		return guild_HasPermission(iRank-1, pGuild, ePerm);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_CanClaimLeadership);
bool gclGuild_expr_CanClaimLeadership(SA_PARAM_OP_VALID Entity* pEnt)
{
	GuildMember *pGuildMember = guild_FindMember(pEnt);
	Guild *pGuild = guild_GetGuild(pEnt);

	return guild_CanClaimLeadership(pGuildMember, pGuild);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_HasBankTabPermission);
bool gclGuild_expr_HasBankTabPermission(SA_PARAM_OP_VALID Entity* pEnt, S32 iRank, S32 iBagID, const char* pcPermission)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	InventoryBag *pBag = pGuild ? inv_guildbank_GetBag(guild_GetGuildBank(pEnt), iBagID) : NULL;
	InventoryBagLite *pBagLite = !pBag && pGuild ? inv_guildbank_GetLiteBag(guild_GetGuildBank(pEnt), iBagID) : NULL;
	GuildBankPermissions ePerm = StaticDefineIntGetInt(GuildBankPermissionsEnum, pcPermission);

	iRank--;
	if (pBag && pBag->pGuildBankInfo && iRank >= 0 && iRank < eaSize(&pBag->pGuildBankInfo->eaPermissions)) {
		return (pBag->pGuildBankInfo->eaPermissions[iRank]->ePerms & ePerm) != 0;
	}
	if (pBagLite && pBagLite->pGuildBankInfo && iRank >= 0 && iRank < eaSize(&pBagLite->pGuildBankInfo->eaPermissions)) {
		return (pBagLite->pGuildBankInfo->eaPermissions[iRank]->ePerms & ePerm) != 0;
	}

	return false;
}

//has permission to set permission
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_haspermissiontoset);
bool gclGuild_expr_HasPermissionToSet(SA_PARAM_OP_VALID Entity* pEnt, S32 iRank, const char* pcPermission)
{
	GuildMember* pGuildMember = guild_FindMember( pEnt );

	//make sure we're a member of the guild
	if ( pGuildMember != NULL )
	{
		const Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		//lowest rank members cannot change permissions
		if ( pGuildMember->iRank == 0 ) return false;

		if ( pGuild != NULL )
		{
			bool bResult;
			GuildRankPermissions ePerm;

			iRank--;
			if ( iRank == eaSize( &pGuild->eaRanks ) - 1 ) return false;

			//check if the user has access to the permission
			ePerm = StaticDefineIntGetInt(GuildRankPermissionsEnum, pcPermission);

			//we have to have both the GuildPermission_SetPermission flag and the ePerm flag to set this permission
			//in the case of GuildPermission_SetPermission == ePerm, only one flag is tested
			if ( ePerm != GuildPermission_SetPermission )
			{
				ePerm |= GuildPermission_SetPermission;
			}
			else
			{
				if ( iRank == 0 ) return false;
			}

			bResult = guild_HasPermission( pGuildMember->iRank, pGuild, ePerm );

			return bResult;
		}

	}

	return false;
}

//has permission to set permission internal
bool GuildInternal_HasPermissionToSet(SA_PARAM_OP_VALID Guild*		pGuild,
									  SA_PARAM_OP_VALID GuildMember*	pGuildMember,
									  S32						iRank,
									  GuildRankPermissions		ePerm )
{
	//lowest rank members cannot change permissions
	if ( pGuildMember->iRank == 0 ) return false;

	iRank--;
	if ( iRank == eaSize( &pGuild->eaRanks ) - 1 ) return false;

	//we have to have both the GuildPermission_SetPermission flag and the ePerm flag to set this permission
	//in the case of GuildPermission_SetPermission == ePerm, only one flag is tested
	if ( ePerm != GuildPermission_SetPermission )
	{
		ePerm |= GuildPermission_SetPermission;
	}
	else
	{
		if ( iRank == 0 ) return false;
	}

	return guild_HasPermission( pGuildMember->iRank, pGuild, ePerm );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_setpermission);
void gclGuild_expr_SetPermission(SA_PARAM_OP_VALID Entity* pEnt, S32 iRank, const char* pcPermission, bool bOn)
{
	const Guild* pGuild = guild_GetGuild(pEnt);

	if (pGuild) {
		ServerCmd_Guild_SetPermission(iRank, pcPermission, bOn);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_SetBankPermission);
void gclGuild_expr_SetBankPermission(SA_PARAM_OP_VALID Entity* pEnt, S32 iRank, S32 iBagID, const char* pcPermission, bool bOn)
{
	const Guild* pGuild = guild_GetGuild(pEnt);

	if (pGuild) {
		ServerCmd_Guild_SetBankPermission(StaticDefineIntRevLookup(InvBagIDsEnum, iBagID), iRank, pcPermission, bOn);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getuniformlist);
void gclGuild_expr_GetUniformList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	Guild* pGuild;
	GuildMember* pGuildMember;
	static GuildCostume **s_eaLeagueCostumes = NULL;

	int iSize, i;

	iSize = eaSize(&s_eaLeagueCostumes);

	//clear the existing league costume list
	for (i = 0; i < iSize; i++)
	{
		StructDestroy(parse_GuildCostume, s_eaLeagueCostumes[i]);
	}

	eaClear(&s_eaLeagueCostumes);

	pGuildMember = guild_FindMember( pEnt );

	//make sure we're a member of the guild
	if ( pGuildMember != NULL )
	{
		pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		//make sure the guild exists
		if ( pGuild != NULL )
		{
			iSize = eaSize( &pGuild->eaUniforms );

			//for each rank, record relevant guild rank data
			for ( i = 0; i < iSize; i++ )
			{
				GuildCostume* pLeagueCostume = StructClone( parse_GuildCostume, pGuild->eaUniforms[i] );
				eaPush( &s_eaLeagueCostumes, pLeagueCostume );
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, &s_eaLeagueCostumes, GuildCostume, false);
}

AUTO_STRUCT;
typedef struct LeagueRankData {
	const char*				pcName;		AST(UNOWNED)
} LeagueRankData;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getranklist);
void gclGuild_expr_GetRankList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	Guild* pGuild;
	GuildMember* pGuildMember;

	static LeagueRankData **s_eaLeagueRanks = NULL;

	int iSize, i;

	iSize = eaSize(&s_eaLeagueRanks);

	//clear the existing league rank list
	for (i = 0; i < iSize; i++)
	{
		StructDestroy(parse_LeagueRankData, s_eaLeagueRanks[i]);
	}

	eaClear(&s_eaLeagueRanks);

	pGuildMember = guild_FindMember( pEnt );

	//make sure we're a member of the guild
	if ( pGuildMember != NULL )
	{
		pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		//make sure the guild exists
		if ( pGuild != NULL )
		{
			iSize = eaSize( &pGuild->eaRanks );

			//for each rank, record relevant guild rank data
			for ( i = 0; i < iSize; i++ )
			{
				LeagueRankData* pLeagueRankData = StructCreate( parse_LeagueRankData );

				//get the display name of the rank
				if ( pGuild->eaRanks[i]->pcDisplayName != 0 )
				{
					//get the user-specified rank name
					pLeagueRankData->pcName = pGuild->eaRanks[i]->pcDisplayName;
				}
				else
				{
					//get the default rank name
					pLeagueRankData->pcName = TranslateMessageKey( pGuild->eaRanks[i]->pcDefaultNameMsg );
				}

				eaPush( &s_eaLeagueRanks, pLeagueRankData );
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, &s_eaLeagueRanks, LeagueRankData, false);
}

AUTO_STRUCT;
typedef struct GuildMemberData {
	const char*	pcName;		AST(UNOWNED)
	const char*	pcAtName;	AST(UNOWNED)
	const char*	pcStatus;	AST(UNOWNED)
	const char*	pcPublicComment;
	const char*	pcPublicCommentTime;
	const char*	pcOfficerComment;
	const char*	pcOfficerCommentTime;
	const char *pchClassName;	AST(NAME(ClassName) POOL_STRING)
	char *pchClassDispName;		AST(NAME(ClassDispName) ESTRING)
	int			iRank;
	int			iOfficerRank;
	int			iLevel;
	const char*	pcLocation; AST(UNOWNED)
	int			iInstance;
	char*		pcLogout;	AST(ESTRING)
	ContainerID iContainerID;
	bool		bOnline;
	U32			eLFGMode;	AST(NAME(LFGMode))
	U32			uGroupProjectContribution;
} GuildMemberData;

// Get the members of this entity's guild.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getmemberlist);
void gclGuild_expr_GetMemberList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, bool bShowOffline, S32 iStart, S32 iCount)
{
	static StashTable st_GroupProjectCounts;
	static GuildMemberData **eaSortedList = NULL;
	GuildMemberData ***peaGuildMembers = ui_GenGetManagedListSafe(pGen, GuildMemberData);
	Guild* pGuild = guild_GetGuild(pEnt);
	int i, iGuildSize, iOldSize, iCurSize = 0;
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	char temp[1024];

	if (iStart < 0) {
		iStart = 0;
	}

	if (pGuild && pState) {
		PlayerGuild *pPlayerGuildData = pEnt->pPlayer ? pEnt->pPlayer->pGuild : NULL;
		GroupProjectContainer *pContainer = pPlayerGuildData ? GET_REF(pPlayerGuildData->hGroupProjectContainer) : NULL;

		if (pContainer)
		{
			if (!st_GroupProjectCounts)
				st_GroupProjectCounts = stashTableCreateInt(16);
			else
				stashTableClear(st_GroupProjectCounts);

			PERFINFO_AUTO_START("Updating Group Project Leaderboard Counts",1);
			for (i = 0; i < eaSize(&pContainer->projectList); i++)
			{
				GroupProjectState *pGroupProjectState = pContainer->projectList[i];
				int j;
				for (j = eaSize(&pGroupProjectState->donationStats) - 1; j >= 0; j--)
				{
					U32 iValue, iDonor = pGroupProjectState->donationStats[j]->donatorID;

					// Update the value in the table
					if (stashIntFindInt(st_GroupProjectCounts, iDonor, &iValue))
						iValue += pGroupProjectState->donationStats[j]->contribution;
					else
						iValue = pGroupProjectState->donationStats[j]->contribution;

					stashIntAddInt(st_GroupProjectCounts, iDonor, iValue, true);
				}
			}
			PERFINFO_AUTO_STOP();
		}

		PERFINFO_AUTO_START("BuildMemberList",1);
		// Assemble the initial list of guild members to be sorted
		iOldSize = eaSize(&eaSortedList);
		iGuildSize = eaSize(&pGuild->eaMembers);
		iCurSize = 0;
		for (i = 0; i < iGuildSize; i++) {
			if (bShowOffline || pGuild->eaMembers[i]->bOnline) {
				GuildMemberData *pData;

				if (iCurSize >= iOldSize) {
					eaPush(&eaSortedList, StructCreate(parse_GuildMemberData));
				} else {
					StructReset(parse_GuildMemberData, eaGet(&eaSortedList, iCurSize));
				}
				pData = eaGet(&eaSortedList, iCurSize);
				pData->pcName = pGuild->eaMembers[i]->pcName;
				pData->pcAtName = pGuild->eaMembers[i]->pcAccount;
				pData->pchClassName = pGuild->eaMembers[i]->pchClassName;
				if (pData->pchClassName)
				{
					CharacterClass *pClass = RefSystem_ReferentFromString("CharacterClass", pData->pchClassName);
					if (pClass)
					{
						estrCopy2(&pData->pchClassDispName, TranslateDisplayMessage(pClass->msgDisplayName));
					}
				}
				pData->iRank = pGuild->eaMembers[i]->iRank+1;
				pData->iLevel = pGuild->eaMembers[i]->iLevel;
				pData->iOfficerRank = pGuild->eaMembers[i]->iOfficerRank;
				pData->iContainerID = pGuild->eaMembers[i]->iEntID;
				pData->bOnline = pGuild->eaMembers[i]->bOnline;
				pData->eLFGMode = pGuild->eaMembers[i]->eLFGMode;
				if (pData->bOnline) {
					pData->pcStatus = pGuild->eaMembers[i]->pcStatus;
					pData->pcLocation = gclRequestMapDisplayName(pGuild->eaMembers[i]->pcMapMsgKey);
				} else {
					pData->pcStatus = NULL;
					pData->pcLocation = TranslateMessageKey("Player.Offline");
				}
				if (pGuild->eaMembers[i]->pcPublicComment && *pGuild->eaMembers[i]->pcPublicComment)
				{
					*temp = '\0';
					strcat(temp,pGuild->eaMembers[i]->pcPublicComment);
					strcat(temp,"<br><br>");
					strcat(temp,timeGetLocalDateStringFromSecondsSince2000(pGuild->eaMembers[i]->iPublicCommentTime));
					pData->pcPublicCommentTime = StructAllocString(temp);
					pData->pcPublicComment = StructAllocString(pGuild->eaMembers[i]->pcPublicComment);
				}
				if (pPlayerGuildData && eaSize(&pPlayerGuildData->eaOfficerComments))
				{
					int j;
					for (j = eaSize(&pPlayerGuildData->eaOfficerComments)-1; j >= 0; --j)
					{
						if (pGuild->eaMembers[i]->iEntID == pPlayerGuildData->eaOfficerComments[j]->iEntID)
						{
							if (pPlayerGuildData->eaOfficerComments[j]->pcWhoOfficerComment && *pPlayerGuildData->eaOfficerComments[j]->pcWhoOfficerComment)
							{
								*temp = '\0';
								if (pPlayerGuildData->eaOfficerComments[j]->pcOfficerComment && *pPlayerGuildData->eaOfficerComments[j]->pcOfficerComment)
								{
									strcat(temp,pPlayerGuildData->eaOfficerComments[j]->pcOfficerComment);
									strcat(temp,"<br><br>");
									strcat(temp,TranslateMessageKey("GuildServer_PostedBy"));
									pData->pcOfficerComment = StructAllocString(pPlayerGuildData->eaOfficerComments[j]->pcOfficerComment);
								}
								else
								{
									strcat(temp,TranslateMessageKey("GuildServer_RemovedBy"));
								}
								strcat(temp,"<br>");
								strcat(temp,pPlayerGuildData->eaOfficerComments[j]->pcWhoOfficerComment);
								strcat(temp,"<br>");
								strcat(temp,timeGetLocalDateStringFromSecondsSince2000(pPlayerGuildData->eaOfficerComments[j]->iOfficerCommentTime));
								pData->pcOfficerCommentTime = StructAllocString(temp);
							}
							break;
						}
					}
				}
				pData->iInstance = pGuild->eaMembers[i]->iMapInstanceNumber;
				if (!stashIntFindInt(st_GroupProjectCounts, pData->iContainerID, &pData->uGroupProjectContribution))
					pData->uGroupProjectContribution = 0;
				iCurSize++;
			}
		}
		eaSetSizeStruct(&eaSortedList, parse_GuildMemberData, iCurSize);
		PERFINFO_AUTO_STOP();

		// Sort the list
		PERFINFO_AUTO_START("SortMemberList",1);
		{
			UIGen *pColGen = eaGet(&pState->eaCols, pState->iSortCol);
			UIGenListColumn *pCol = pColGen ? UI_GEN_RESULT(pColGen, ListColumn) : NULL;
			UIGenListColumnState *pColState = pCol ? UI_GEN_STATE(pColGen, ListColumn) : NULL;

			if (pColState) {
				if (pCol->pchTPIField && *pCol->pchTPIField && pColState->iTPICol == -1) {
					ParserFindColumn(parse_GuildMemberData, pCol->pchTPIField, &pColState->iTPICol);
				}

				eaStableSortUsingColumn(&eaSortedList, parse_GuildMemberData, pColState->iTPICol);
			}
		}
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("SliceMemberList",1);
		// After sorting, copy the relevant subset into the actual gen list
		iOldSize = eaSize(peaGuildMembers);
		iCurSize = 0;
		if (pState->eSortMode == UISortDescending) {
			int iStartIndex = eaSize(&eaSortedList) - iStart - 1;
			int iEndIndex = iStartIndex - iCount;
			for (i = iStartIndex; i >= 0 && i > iEndIndex; i--) {
				if (iCurSize >= iOldSize) {
					eaPush(peaGuildMembers, StructCreate(parse_GuildMemberData));
				} else {
					StructReset(parse_GuildMemberData, eaGet(peaGuildMembers, iCurSize));
				}
				StructCopy(parse_GuildMemberData, eaSortedList[i], (*peaGuildMembers)[iCurSize], 0, 0, 0);
				if (!((*peaGuildMembers)[iCurSize]->bOnline)) {
					GuildMember *pMember = guild_FindMemberInGuildEntID((*peaGuildMembers)[iCurSize]->iContainerID, pGuild);
					if (pMember->iLogoutTime)
					{
						entFormatGameMessageKey(pEnt, &(*peaGuildMembers)[iCurSize]->pcLogout, "Guild.LastOnline",
							STRFMT_DATETIME("lastOnline", pMember->iLogoutTime),
							STRFMT_END);
					}
					else
					{
						entFormatGameMessageKey(pEnt, &(*peaGuildMembers)[iCurSize]->pcLogout, "Guild.LastOnlineBefore",
							STRFMT_END);
					}
				}
				iCurSize++;
			}
		} else {
			for (i = iStart; i < iStart+iCount && i < eaSize(&eaSortedList); i++) {
				if (iCurSize >= iOldSize) {
					eaPush(peaGuildMembers, StructCreate(parse_GuildMemberData));
				} else {
					StructReset(parse_GuildMemberData, eaGet(peaGuildMembers, iCurSize));
				}
				StructCopy(parse_GuildMemberData, eaSortedList[i], (*peaGuildMembers)[iCurSize], 0, 0, 0);
				if (!((*peaGuildMembers)[iCurSize]->bOnline)) {
					GuildMember *pMember = guild_FindMemberInGuildEntID((*peaGuildMembers)[iCurSize]->iContainerID, pGuild);
					if (pMember->iLogoutTime)
					{
						entFormatGameMessageKey(pEnt, &(*peaGuildMembers)[iCurSize]->pcLogout, "Guild.LastOnline",
							STRFMT_DATETIME("lastOnline", pMember->iLogoutTime),
							STRFMT_END);
					}
					else
					{
						entFormatGameMessageKey(pEnt, &(*peaGuildMembers)[iCurSize]->pcLogout, "Guild.LastOnlineBefore",
							STRFMT_END);
					}
				}
				iCurSize++;
			}
		}
		PERFINFO_AUTO_STOP();
	}

	eaSetSizeStruct(peaGuildMembers, parse_GuildMemberData, iCurSize);
	ui_GenSetManagedListSafe(pGen, peaGuildMembers, GuildMemberData, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_GetSize);
S32 gclGuild_expr_GetSize(SA_PARAM_OP_VALID Entity *pEnt, bool bShowOffline)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	if (pGuild) {
		if (bShowOffline) {
			return eaSize(&pGuild->eaMembers);
		} else {
			S32 i, iCount = 0;
			for (i = 0; i < eaSize(&pGuild->eaMembers); i++) {
				if (pGuild->eaMembers[i]->bOnline) {
					iCount++;
				}
			}
			return iCount;
		}
	}
	return 0;
}

// These need to be separate (guild_GetWithdrawLimit and guild_GetItemWithdrawLimit) as the withdraw limit is used in CO, and the item version
// is used in STO
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_GetWithdrawLimit);
S32 gclGuild_expr_GetWithdrawLimit(SA_PARAM_OP_VALID Entity *pEnt, S32 iRank, S32 iBagID)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	Entity *pGuildBank = guild_GetGuildBank(pEnt);
	NOCONST(GuildBankTabInfo) *pGuildBankInfo = inv_GuildbankGetBankTabInfo(pGuildBank, iBagID);

	iRank--;
	if(pGuildBankInfo && iRank >= 0 && iRank < eaSize(&pGuildBankInfo->eaPermissions))
	{
		return pGuildBankInfo->eaPermissions[iRank]->iWithdrawLimit;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_GetItemWithdrawLimit);
S32 gclGuild_expr_GetItemWithdrawLimit(SA_PARAM_OP_VALID Entity *pEnt, S32 iRank, S32 iBagID)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	Entity *pGuildBank = guild_GetGuildBank(pEnt);
	NOCONST(GuildBankTabInfo) *pGuildBankInfo = inv_GuildbankGetBankTabInfo(pGuildBank, iBagID);

	iRank--;
	if(pGuildBankInfo && iRank >= 0 && iRank < eaSize(&pGuildBankInfo->eaPermissions))
	{
		return pGuildBankInfo->eaPermissions[iRank]->iWithdrawItemCountLimit;
	}

	return 0;
}

// These need to be separate (guild_SetWithdrawLimit and guild_SetItemWithdrawLimit) as the withdraw limit is used in CO, and the item version
// is used in STO
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_SetWithdrawLimit);
void gclGuild_expr_SetWithdrawLimit(SA_PARAM_OP_VALID Entity *pEnt, S32 iRank, S32 iBagID, S32 iWithdrawLimit)
{
	ServerCmd_Guild_SetBankWithdrawLimit(StaticDefineIntRevLookup(InvBagIDsEnum, iBagID), iRank, iWithdrawLimit);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_SetItemWithdrawLimit);
void gclGuild_expr_SetItemWithdrawLimit(SA_PARAM_OP_VALID Entity *pEnt, S32 iRank, S32 iBagID, S32 iWithdrawLimit)
{
	ServerCmd_Guild_SetBankItemWithdrawLimit(StaticDefineIntRevLookup(InvBagIDsEnum, iBagID), iRank, iWithdrawLimit);
}

// Return the value of the given numeric item for the given entity.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(guild_GetBankTabName);
const char *gclGuild_expr_GetBankTabName(SA_PARAM_OP_VALID Entity *pEnt, S32 iBagID)
{
	return guild_GetBankTabName(pEnt, iBagID);
}

// Return the value of the given numeric item for the given entity.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(guild_SetBankTabName);
void gclGuild_expr_SetBankTabName(SA_PARAM_OP_VALID Entity *pEnt, S32 iBagID, const char *pcNewName)
{
	ServerCmd_Guild_RenameBankTab(StaticDefineIntRevLookup(InvBagIDsEnum, iBagID), pcNewName);
}

// Return whether or not the player may invite another player into their guild
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(guild_MayInvite);
bool gclGuild_expr_MayInvite(SA_PARAM_OP_VALID Entity *pEnt, const char *pchOtherAccountName)
{
	Guild *pGuild = guild_GetGuild(pEnt);

	if (pEnt && pGuild && gclGuild_expr_HasPermissionToInvite(pEnt)) {
		Entity *pInvite;
		ContainerID iInviteID = 0;
		ChatPlayerStruct *pStruct;
		ResolveKnownEntityID(pEnt, pchOtherAccountName, NULL, NULL, &iInviteID);

		if (!iInviteID) {
			return false;
		}

		if (guild_FindMemberInGuildEntID(iInviteID, pGuild)) {
			// Other person is already in pEnt's guild.
			return false;
		}

		pInvite = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iInviteID);

		if (pInvite)
		{
			if (gConf.bEnforceGuildGuildMemberAllegianceMatch)
			{
				if (pGuild->pcAllegiance && *pGuild->pcAllegiance)
				{
					if (!REF_STRING_FROM_HANDLE(pInvite->hAllegiance)) return false;
					if (stricmp(REF_STRING_FROM_HANDLE(pInvite->hAllegiance), pGuild->pcAllegiance)) return false;
				}
			}
			return !guild_WithGuild(pInvite);
		}
		pStruct = FindChatPlayerByPlayerID(iInviteID);
		if (pStruct)
		{
			if (gConf.bEnforceGuildGuildMemberAllegianceMatch)
			{
				if (pGuild->pcAllegiance && *pGuild->pcAllegiance)
				{
					if (!pStruct->pPlayerInfo.onlinePlayerAllegiance) return false;
					if (stricmp(pStruct->pPlayerInfo.onlinePlayerAllegiance, pGuild->pcAllegiance)) return false;
				}
			}
			return pStruct->pPlayerInfo.iPlayerGuild == 0;
		}

		return true;
	}

	return false;
}

// Return whether or not the player may kick another player from their guild
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(guild_MayKick);
bool gclGuild_expr_MayKick(SA_PARAM_OP_VALID Entity *pEnt, const char *pchOtherAccountName)
{
	Guild *pGuild = guild_GetGuild(pEnt);

	if (pEnt && pGuild) {
		U32 iEntID = 0;
		GuildMember *pOtherMember;

		ResolveKnownEntityID(pEnt, pchOtherAccountName, NULL, NULL, &iEntID);
		pOtherMember = guild_FindMemberInGuildEntID(iEntID, pGuild);
		if (pOtherMember) {
			return gclGuild_expr_HasPermissionToKick(pOtherMember->iRank+1, pEnt);
		}
	}

	return false;
}

// Return whether or not the player may demote another player in their guild
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(guild_MayDemote);
bool gclGuild_expr_MayDemote(SA_PARAM_OP_VALID Entity *pEnt, const char *pchOtherAccountName)
{
	Guild *pGuild = guild_GetGuild(pEnt);

	if (pEnt && pGuild) {
		U32 iEntID = 0;
		GuildMember *pOtherMember;

		ResolveKnownEntityID(pEnt, pchOtherAccountName, NULL, NULL, &iEntID);
		pOtherMember = guild_FindMemberInGuildEntID(iEntID, pGuild);
		if (pOtherMember) {
			return gclGuild_expr_HasPermissionToDemote(pOtherMember->iRank+1, pEnt);
		}
	}

	return false;
}

// Return whether or not the player may promote another player in their guild
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(guild_MayPromote);
bool gclGuild_expr_MayPromote(SA_PARAM_OP_VALID Entity *pEnt, const char *pchOtherAccountName)
{
	Guild *pGuild = guild_GetGuild(pEnt);

	if (pEnt && pGuild) {
		U32 iEntID = 0;
		GuildMember *pOtherMember;

		ResolveKnownEntityID(pEnt, pchOtherAccountName, NULL, NULL, &iEntID);
		pOtherMember = guild_FindMemberInGuildEntID(iEntID, pGuild);
		if (pOtherMember) {
			return gclGuild_expr_HasPermissionToPromote(pOtherMember->iRank+1, pEnt);
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_IsInMyGuildEnt);
bool gclGuild_expr_IsInMyGuildEnt(SA_PARAM_OP_VALID Entity *pGuildMate)
{
	Entity *pPlayer = entActivePlayerPtr();
	Guild *pGuild = guild_IsMember(pPlayer) ? guild_GetGuild(pPlayer) : NULL;
	return pGuild ? guild_FindMemberInGuild(pGuildMate, pGuild) != NULL : false;
}

////////////////////////////////////////////////////
// Guild Recruit Uniforms
////////////////////////////////////////////////////

static const char *p_SavedUniformName = NULL;
static U32 p_SavedUniformIndex = 0;
static PCSlotType *p_SlotTypeToValidate = NULL;
static PlayerCostume *p_LoadedUniform = NULL;
static bool g_UniformHasErrors = false;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_SetSlotTypeToValidate);
void gclGuild_expr_SetSlotTypeToValidate(SA_PARAM_OP_VALID const char *pchSlotType)
{
	p_SlotTypeToValidate = costumeLoad_GetSlotType(pchSlotType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_ClearLoadedUniform);
void gclGuild_expr_ClearLoadedUniform()
{
	StructDestroySafe(parse_PlayerCostume, &p_LoadedUniform);
	g_UniformHasErrors = false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_UniformHasErrors);
bool gclGuild_expr_UniformHasErrors()
{
	return g_UniformHasErrors;
}

void gclGuild_SetLoadedUniform(PlayerCostume *pCostume, SpeciesDef *pSpecies)
{
	PlayerCostume *pTemp = costumeTailor_MakeCostumeOverlay(pCostume, "Uniforms", true, false);
	PlayerCostume *pUniform = StructClone(parse_PlayerCostume, pCostume);
	Entity *pEnt = entActivePlayerPtr();

	if (pTemp && pUniform)
	{
		costumeTailor_MakeCostumeValid(CONTAINER_NOCONST(PlayerCostume, pUniform), pSpecies, NULL, p_SlotTypeToValidate, false, false, false, guild_GetGuild(pEnt), false, NULL, false, NULL);
		if (p_LoadedUniform) gclGuild_expr_ClearLoadedUniform();
		p_LoadedUniform = costumeTailor_MakeCostumeOverlay(pUniform, "Uniforms", true, false);
		if (p_LoadedUniform)
		{
			if (0 == StructCompare(parse_PlayerCostume, p_LoadedUniform, pTemp, 0, 0, 0))
			{
				g_UniformHasErrors = false;
			}
			else
			{
				g_UniformHasErrors = true;
			}
			if (pCostume->pcName) CONTAINER_NOCONST(PlayerCostume, p_LoadedUniform)->pcName = allocAddString(pCostume->pcName);
			if (pSpecies) SET_HANDLE_FROM_REFERENT("Species",pSpecies,p_LoadedUniform->hSpecies);
		}
		p_SavedUniformName = NULL;
	}

	if (pTemp) StructDestroy(parse_PlayerCostume, pTemp);
	if (pUniform) StructDestroy(parse_PlayerCostume, pUniform);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AnimatedPersonWithUniform");
SA_RET_OP_VALID BasicTexture *gclExpr_AnimatedPersonWithUniform(SA_PARAM_OP_VALID Entity *pPlayer,
																	 const char *pchSpecies, U32 iUniformIndex,
																	 SA_PARAM_OP_VALID BasicTexture *pTexture,
																	 F32 fWidth, F32 fHeight, F32 fDeltaTime)
{
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AnimatedPersonWithUniformRefresh);
void gclExpr_AnimatedPersonWithUniformRefresh()
{
	p_SavedUniformName = NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AnimatedPersonWithUniformRelease);
SA_RET_OP_VALID BasicTexture* gclExpr_AnimatedPersonWithUniformRelease(SA_PARAM_OP_VALID BasicTexture* pTexture, F32 fDeltaTime)
{
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetGuildUniformNameByIndex);
SA_RET_OP_VALID const char *gclExpr_GetGuildUniformNameByIndex(SA_PARAM_OP_VALID Entity *pPlayer, U32 iUniformIndex)
{
	Guild *pGuild = guild_IsMember(pPlayer) ? guild_GetGuild(pPlayer) : NULL;

	if (pGuild && pGuild->eaUniforms &&
		iUniformIndex >= 0 && iUniformIndex < eaUSize(&pGuild->eaUniforms) &&
		pGuild->eaUniforms[iUniformIndex] && pGuild->eaUniforms[iUniformIndex]->pCostume)
	{
		return pGuild->eaUniforms[iUniformIndex]->pCostume->pcName;
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_Expr_AddUniform);
void guild_Expr_AddUniform(SA_PARAM_OP_VALID char *pcName)
{
	if (p_LoadedUniform)
	{
		ServerCmd_Guild_AddLoadedCostume(p_LoadedUniform, pcName);
	}
	else
	{
		ServerCmd_Guild_AddCostume(pcName);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_AddUniformFromCurrentCostume");
void guild_Expr_AddUniformFromCurrentCostume(SA_PARAM_NN_STR char *pcName)
{
	ServerCmd_Guild_AddCostume(pcName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_AddUniformFromCostume");
void guild_Expr_AddUniformFromCostume(SA_PARAM_NN_STR char *pcName, SA_PARAM_OP_VALID PlayerCostume *pCostume)
{
	if (pCostume)
	{
		PlayerCostume *pFixupCostume = costumeTailor_MakeCostumeOverlay(pCostume, "Uniforms", true, false);
		ServerCmd_Guild_AddLoadedCostume(pFixupCostume, pcName);
		StructDestroy(parse_PlayerCostume, pFixupCostume);
	}
}

////////////////////////////////////////////////////
// Time Expressions (Consider moving these to a more appropriate file)
////////////////////////////////////////////////////
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_GetMonthString);
const char *time_GetMonthString(int iMonthNum)
{
	switch (iMonthNum)
	{
	case 0: return TranslateMessageKey("DateTime_January");
	case 1: return TranslateMessageKey("DateTime_February");
	case 2: return TranslateMessageKey("DateTime_March");
	case 3: return TranslateMessageKey("DateTime_April");
	case 4: return TranslateMessageKey("DateTime_May");
	case 5: return TranslateMessageKey("DateTime_June");
	case 6: return TranslateMessageKey("DateTime_July");
	case 7: return TranslateMessageKey("DateTime_August");
	case 8: return TranslateMessageKey("DateTime_September");
	case 9: return TranslateMessageKey("DateTime_October");
	case 10: return TranslateMessageKey("DateTime_November");
	case 11: return TranslateMessageKey("DateTime_December");
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_GetWeekdayString);
const char *time_GetWeekdayString(int iWeekdayNum)
{
	switch (iWeekdayNum)
	{
	case 0: return TranslateMessageKey("DateTime_Sunday");
	case 1: return TranslateMessageKey("DateTime_Monday");
	case 2: return TranslateMessageKey("DateTime_Tuesday");
	case 3: return TranslateMessageKey("DateTime_Wednesday");
	case 4: return TranslateMessageKey("DateTime_Thursday");
	case 5: return TranslateMessageKey("DateTime_Friday");
	case 6: return TranslateMessageKey("DateTime_Saturday");
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_MakeMonthCorrection);
int time_MakeMonthCorrection(int iMonth)
{
	if (iMonth < 0)
	{
		iMonth = -iMonth % 12;
	}
	else if (iMonth >= 12)
	{
		iMonth = iMonth % 12;
	}

	return iMonth;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_MakeYearCorrection);
int time_MakeYearCorrection(int iMonth, int iYear)
{
	if (iMonth < 0)
	{
		iYear -= (11 - iMonth) / 12;
	}
	else if (iMonth >= 12)
	{
		iYear += iMonth / 12;
	}

	return iYear;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_GetWeekdayFromDate);
int time_GetWeekdayFromDate(int iYear, int iMonth, int iDay)
{
	return dayOfWeek(iYear - 1900, iMonth - 1, iDay);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_GetYearFromTime);
int time_GetYearFromTime(int iTimeStamp)
{
	struct tm myTime;
	timeMakeLocalTimeStructFromSecondsSince2000(iTimeStamp, &myTime);
	return myTime.tm_year + 1900;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_GetMonthFromTime);
int time_GetMonthFromTime(int iTimeStamp)
{
	struct tm myTime;
	timeMakeLocalTimeStructFromSecondsSince2000(iTimeStamp, &myTime);
	return myTime.tm_mon;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_GetDayFromTime);
int time_GetDayFromTime(int iTimeStamp)
{
	struct tm myTime;
	timeMakeLocalTimeStructFromSecondsSince2000(iTimeStamp, &myTime);
	return myTime.tm_mday;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_GetHourFromTime);
int time_GetHourFromTime(int iTimeStamp)
{
	struct tm myTime;
	timeMakeLocalTimeStructFromSecondsSince2000(iTimeStamp, &myTime);
	return myTime.tm_hour;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_GetMinuteFromTime);
int time_GetMinuteFromTime(int iTimeStamp)
{
	struct tm myTime;
	timeMakeLocalTimeStructFromSecondsSince2000(iTimeStamp, &myTime);
	return myTime.tm_min;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(time_RoundTimeToMinuteInterval);
int time_RoundTimeToMinuteInterval(int iTimeStamp, int iRoundInterval)
{
	struct tm myTime;
	timeMakeLocalTimeStructFromSecondsSince2000(iTimeStamp, &myTime);

	iTimeStamp += MINUTES(iRoundInterval - myTime.tm_min % iRoundInterval);
	iTimeStamp -= SECONDS(myTime.tm_sec);

	return iTimeStamp;
}

int time_RoundTimeToNearestMinuteInterval(int iTimeStamp, int iRoundInterval)
{
	int iTimeDelta, iRoundIntervalInSeconds;
	struct tm myTime;
	timeMakeLocalTimeStructFromSecondsSince2000(iTimeStamp, &myTime);

	iTimeDelta = MINUTES(myTime.tm_min % iRoundInterval) + SECONDS(myTime.tm_sec);
	iRoundIntervalInSeconds = MINUTES(iRoundInterval);

	if (iTimeDelta >= (iRoundIntervalInSeconds / 2))
	{
		iTimeStamp += iRoundIntervalInSeconds - iTimeDelta;
	}
	else
	{
		iTimeStamp -= iTimeDelta;
	}

	return iTimeStamp;
}

#define GuildEventLocalToServerTime(time) time_RoundTimeToNearestMinuteInterval(timeLocalSecondsToServerSeconds(time), 5)
#define GuildEventServerToLocalTime(time) time_RoundTimeToNearestMinuteInterval(timeServerSecondsToLocalSeconds(time), 5)

////////////////////////////////////////////////////
// Guild Recruit Events
////////////////////////////////////////////////////
static GuildEventData g_GuildEventData;

static bool gclGuildUI_EventSetData(GuildEventData *pData, GuildEvent *pGuildEvent, S32 iOccurrence, S32 iStartTimeOffset)
{
	if (pData)
	{
		if (pGuildEvent)
		{
			if (!pData->pcTitle && pGuildEvent->pcTitle || pData->pcTitle && (!pGuildEvent->pcTitle || strcmp(pData->pcTitle, pGuildEvent->pcTitle)))
				StructCopyString(&pData->pcTitle, pGuildEvent->pcTitle);
			if (!pData->pcDescription && pGuildEvent->pcDescription || pData->pcDescription && (!pGuildEvent->pcDescription || strcmp(pData->pcDescription, pGuildEvent->pcDescription)))
				StructCopyString(&pData->pcDescription, pGuildEvent->pcDescription);

			pData->uiID = pGuildEvent->uiID;
			pData->iStartTimeTime = GuildEventServerToLocalTime(pGuildEvent->iStartTimeTime + DAYS(iOccurrence * pGuildEvent->eRecurType) + iStartTimeOffset);
			pData->iDuration = pGuildEvent->iDuration;
			pData->eRecurType = pGuildEvent->eRecurType;
			pData->iRecurrenceCount = pGuildEvent->iRecurrenceCount - iOccurrence;
			pData->iMinGuildRank = pGuildEvent->iMinGuildRank;
			pData->iMinGuildEditRank = pGuildEvent->iMinGuildEditRank;
			pData->iMinLevel = pGuildEvent->iMinLevel;
			pData->iMaxLevel = pGuildEvent->iMaxLevel;
			pData->iMinAccepts = pGuildEvent->iMinAccepts;
			pData->iMaxAccepts = pGuildEvent->iMaxAccepts;

			// TODO: add more details, such as
			//  - accept count
			//  - maybe count
			//  - refuse count
			//  - reply count
			//  - my reply
			//  - canceled
			//  - editable
			//  - owner
			//  - last update time
			//  - occurrence
			//  - end time
			//  - full (accept count >= max accepts)
			//  - can accept (satisfies level and guild rank reqs)
			//  - status (in progress, ended)

			return true;
		}
		else
		{
			StructReset(parse_GuildEventData, pData);
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_LoadGuildEvent);
bool guildevent_Expr_LoadGuildEvent(SA_PARAM_OP_VALID Entity *pEnt, U32 uiID)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	return gclGuildUI_EventSetData(&g_GuildEventData, pGuild ? eaIndexedGetUsingInt(&pGuild->eaEvents, uiID) : NULL, 0, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_UnloadGuildEvent);
void guildevent_Expr_UnloadGuildEvent()
{
	gclGuildUI_EventSetData(&g_GuildEventData, NULL, 0, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_GetGuildEvent);
SA_RET_NN_VALID GuildEventData *guildevent_Expr_GetGuildEvent(void)
{
	return &g_GuildEventData;
}

// Get the edited event being modified
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetMutableGuildEvent);
SA_RET_NN_VALID GuildEventData *gclGuildExprGenGetMutableGuildEvent(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetPointer(pGen, &g_GuildEventData, parse_GuildEventData);
	return &g_GuildEventData;
}

// Get the event data for a specific occurrence
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetGuildEventOccurrence);
SA_RET_OP_VALID GuildEventData *gclGuildExprGenGetGuildEventOccurrence(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, U32 uiID, S32 iOccurence)
{
	GuildEventData *pData = ui_GenGetManagedPointer(pGen, parse_GuildEventData);
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildEvent *pEvent = pGuild ? eaIndexedGetUsingInt(&pGuild->eaEvents, uiID) : NULL;
	bool bValid = gclGuildUI_EventSetData(pData, pEvent, iOccurence, 0);
	ui_GenSetManagedPointer(pGen, pData, parse_GuildEventData, true);
	return bValid ? pData : NULL;
}

// Get the event data for an event
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetGuildEvent);
SA_RET_OP_VALID GuildEventData *gclGuildExprGenGetGuildEvent(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, U32 uiID)
{
	GuildEventData *pData = ui_GenGetManagedPointer(pGen, parse_GuildEventData);
	Guild *pGuild = guild_GetGuild(pEnt);
	bool bValid = gclGuildUI_EventSetData(pData, pGuild ? eaIndexedGetUsingInt(&pGuild->eaEvents, uiID) : NULL, 0, 0);
	ui_GenSetManagedPointer(pGen, pData, parse_GuildEventData, true);
	return bValid ? pData : NULL;
}

// Get the event data for a specific starting time
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetGuildEventByTime);
SA_RET_OP_VALID GuildEventData *gclGuildExprGenGetGuildEventByTime(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, U32 uiID, S32 iStartTime)
{
	GuildEventData *pData = ui_GenGetManagedPointer(pGen, parse_GuildEventData);
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildEvent *pEvent = pGuild ? eaIndexedGetUsingInt(&pGuild->eaEvents, uiID) : NULL;
	S32 iOccurence = pEvent && pEvent->eRecurType != GuildEventRecurType_Once ? (iStartTime - pEvent->iStartTimeTime) / DAYS(pEvent->eRecurType) : 0;
	S32 iStartTimeOffset = pEvent && pEvent->eRecurType != GuildEventRecurType_Once ? DAYS(iOccurence * pEvent->eRecurType) : 0;
	bool bValid = gclGuildUI_EventSetData(pData, pEvent, iOccurence, iStartTimeOffset);
	ui_GenSetManagedPointer(pGen, pData, parse_GuildEventData, true);
	return bValid ? pData : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_GetReplyCount);
int guildevent_Expr_GetReplyCount(SA_PARAM_OP_VALID Entity *pEnt, U32 uiID, U32 iStartTime, U32 eReplyType)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildEvent *pGuildEvent = NULL;
	int i, iCount = 0;

	if (!pGuild)
	{
		return 0;
	}

	i = eaIndexedFindUsingInt(&pGuild->eaEvents, uiID);
	if (i < 0)
	{
		return 0;
	}

	pGuildEvent = pGuild->eaEvents[i];

	return guildevent_GetReplyCount(pGuild, pGuildEvent, GuildEventLocalToServerTime(iStartTime), eReplyType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_IsCanceled);
bool guildevent_Expr_IsCanceled(SA_PARAM_OP_VALID Entity *pEnt, U32 uiID)
{
	int i;
	Guild *pGuild = guild_GetGuild(pEnt);
	if (!pGuild)
	{
		return false;
	}

	i = eaIndexedFindUsingInt(&pGuild->eaEvents, uiID);
	if (i < 0)
	{
		return false;
	}

	return pGuild->eaEvents[i]->bCanceled;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetTitle);
void guildevent_Expr_SetTitle(SA_PARAM_OP_VALID char *pcTitle)
{
	StructFreeStringSafe(&g_GuildEventData.pcTitle);

	if (pcTitle)
	{
		g_GuildEventData.pcTitle = StructAllocString(pcTitle);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetDescription);
void guildevent_Expr_SetDescription(SA_PARAM_OP_VALID char *pcDescription)
{
	StructFreeStringSafe(&g_GuildEventData.pcDescription);

	if (pcDescription)
	{
		g_GuildEventData.pcDescription = StructAllocString(pcDescription);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetStartTime);
void guildevent_Expr_SetStartTime(U32 iStartTime)
{
	g_GuildEventData.iStartTimeTime = iStartTime;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetDuration);
void guildevent_Expr_SetDuration(U32 iDuration)
{
	g_GuildEventData.iDuration = iDuration;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetReccurenceType);
void guildevent_Expr_SetRecurrenceType(U32 eRecurType)
{
	g_GuildEventData.eRecurType = eRecurType;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetReccurenceCount);
void guildevent_Expr_SetRecurrenceCount(S32 iRecurrenceCount)
{
	g_GuildEventData.iRecurrenceCount = iRecurrenceCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetMinGuildRank);
void guildevent_Expr_SetMinGuildRank(U32 iMinGuildRank)
{
	g_GuildEventData.iMinGuildRank = iMinGuildRank;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetMinGuildEditRank);
void guildevent_Expr_SetMinGuildEditRank(U32 iMinGuildEditRank)
{
	g_GuildEventData.iMinGuildEditRank = iMinGuildEditRank;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetMinLevel);
void guildevent_Expr_SetMinLevel(U32 iMinLevel)
{
	g_GuildEventData.iMinLevel = iMinLevel;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetMaxLevel);
void guildevent_Expr_SetMaxLevel(U32 iMaxLevel)
{
	g_GuildEventData.iMaxLevel = iMaxLevel;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetLevelRange);
void guildevent_Expr_SetLevelRange(U32 iMinLevel, U32 iMaxLevel)
{
	g_GuildEventData.iMinLevel = iMinLevel;
	g_GuildEventData.iMaxLevel = iMaxLevel;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetMinAccepts);
void guildevent_Expr_SetMinAccepts(U32 iMinAccepts)
{
	g_GuildEventData.iMinAccepts = iMinAccepts;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetMaxAccepts);
void guildevent_Expr_SetMaxAccepts(U32 iMaxAccepts)
{
	g_GuildEventData.iMaxAccepts = iMaxAccepts;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SetAcceptRange);
void guildevent_Expr_SetAcceptRange(U32 iMinAccepts, U32 iMaxAccepts)
{
	g_GuildEventData.iMinAccepts = iMinAccepts;
	g_GuildEventData.iMaxAccepts = iMaxAccepts;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_GuildMaxEvents);
bool guildevent_Expr_GuildMaxEvents(SA_PARAM_OP_VALID Entity *pEnt)
{
	Guild* pGuild = guild_GetGuild(pEnt);
	if (!pGuild || eaSize(&pGuild->eaEvents) >= MAX_GUILD_EVENTS)
	{
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SubmitNew);
void guildevent_Expr_SubmitNew()
{
	g_GuildEventData.iStartTimeTime = GuildEventLocalToServerTime(g_GuildEventData.iStartTimeTime);
	ServerCmd_Guild_NewEvent(&g_GuildEventData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_SubmitEdit);
void guildevent_Expr_SubmitEdit()
{
	g_GuildEventData.iStartTimeTime = GuildEventLocalToServerTime(g_GuildEventData.iStartTimeTime);
	ServerCmd_Guild_EditEvent(&g_GuildEventData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_Cancel);
void guildevent_Expr_Cancel(U32 uiID)
{
	ServerCmd_Guild_CancelEvent(uiID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_Remove);
void guildevent_Expr_Remove(U32 uiID)
{
	ServerCmd_Guild_RemoveEvent(uiID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guildevent_Reply);
void guildevent_Expr_Reply(U32 uiID, U32 iStartTime, int eReplyType, SA_PARAM_OP_VALID char *pchMessage)
{
	ServerCmd_Guild_ReplyEvent(uiID, GuildEventLocalToServerTime(iStartTime), eReplyType, pchMessage);
}

static int gclGuildEventSorter(void *pContext, const GuildEventData **ppLeft, const GuildEventData **ppRight)
{
	if ((*ppLeft)->iStartTimeTime != (*ppRight)->iStartTimeTime)
		return (*ppLeft)->iStartTimeTime - (*ppRight)->iStartTimeTime;
	return stricmp((*ppLeft)->pcTitle, (*ppRight)->pcTitle);
}

// Get the events of this entity's guild.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_geteventlist);
void gclGuild_expr_GetEventList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	GuildEventData ***peaEvents = ui_GenGetManagedListSafe(pGen, GuildEventData);
	Guild* pGuild = guild_GetGuild(pEnt);
	GuildMember *pMember = guild_FindMember(pEnt);
	int iCount = 0;

	if (pGuild && pMember)
	{
		int i, iEventsCount;
		U32 iCurrentTime = timeServerSecondsSince2000();

		PERFINFO_AUTO_START("BuildMemberList",1);

		iEventsCount = eaSize(&pGuild->eaEvents);
		for (i = 0; i < iEventsCount; i++)
		{
			GuildEvent *pGuildEvent = pGuild->eaEvents[i];
			if (pGuildEvent->iMinGuildRank <= pMember->iRank &&
				pGuildEvent->iStartTimeTime + pGuildEvent->iDuration + MIN_GUILD_EVENT_TIME_PAST_REMOVE >= iCurrentTime)
			{
				// TODO: Let players see future events we have not made recurrences for yet
				gclGuildUI_EventSetData(eaGetStruct(peaEvents, parse_GuildEventData, iCount++), pGuildEvent, 0, 0);
			}
		}

		PERFINFO_AUTO_STOP();
	}

	eaSetSizeStruct(peaEvents, parse_GuildEventData, iCount);
	eaQSort_s((*peaEvents), gclGuildEventSorter, NULL);
	ui_GenSetManagedListSafe(pGen, peaEvents, GuildEventData, true);
}

// Get the replies to an event of this entity's guild.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_geteventreplylist);
void gclGuild_expr_GetEventReplyList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, U32 uiID, U32 iStartTime)
{
	GuildEventReply ***peaReplies = ui_GenGetManagedListSafe(pGen, GuildEventReply);
	Guild* pGuild = guild_GetGuild(pEnt);
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	GuildEvent *pGuildEvent = NULL;
	GuildEventReply *pGuildEventReply;
	NOCONST(GuildEventReply) *pGuildEventReplyCopy;
	GuildMember *pMember;
	int i, iCurSize = 0;

	if (pGuild && pState)
	{
		i = eaIndexedFindUsingInt(&pGuild->eaEvents, uiID);
		if (i >= 0)
		{
			pGuildEvent = pGuild->eaEvents[i];
		}
	}

	if (!pGuildEvent)
	{
		eaSetSizeStruct(peaReplies, parse_GuildEventReply, iCurSize);
		ui_GenSetManagedListSafe(pGen, peaReplies, GuildEventReply, true);
		return;
	}

	for (i = eaSize(&pGuildEvent->eaReplies) - 1; i >= 0; i--)
	{
		pGuildEventReply = pGuildEvent->eaReplies[i];
		if ((U32)GuildEventServerToLocalTime(pGuildEventReply->iStartTime) != iStartTime ||
			pGuildEventReply->eGuildEventReplyType == GuildEventReplyType_NoReply)
		{
			continue;
		}

		pMember = guild_FindMemberInGuildEntID(pGuildEventReply->iMemberID, pGuild);
		if (!pMember)
		{
			continue;
		}

		pGuildEventReplyCopy = eaGetStruct(peaReplies, parse_GuildEventReply, iCurSize++);
		StructCopyAllNoConst(parse_GuildEventReply, CONTAINER_NOCONST(GuildEventReply, pGuildEventReply), pGuildEventReplyCopy);
		pGuildEventReplyCopy->iStartTime = GuildEventServerToLocalTime(pGuildEventReplyCopy->iStartTime);
	}
	eaSetSizeStruct(peaReplies, parse_GuildEventReply, iCurSize);

	// Sort the list
	PERFINFO_AUTO_START("SortMemberList",1);
	{
		UIGen *pColGen = eaGet(&pState->eaCols, pState->iSortCol);
		UIGenListColumn *pCol = pColGen ? UI_GEN_RESULT(pColGen, ListColumn) : NULL;
		UIGenListColumnState *pColState = pCol ? UI_GEN_STATE(pColGen, ListColumn) : NULL;

		if (pColState)
		{
			if (pCol->pchTPIField && *pCol->pchTPIField && pColState->iTPICol == -1)
			{
				ParserFindColumn(parse_GuildEventReply, pCol->pchTPIField, &pColState->iTPICol);
			}

			eaStableSortUsingColumn(peaReplies, parse_GuildEventReply, pColState->iTPICol);
		}
	}
	PERFINFO_AUTO_STOP();

	ui_GenSetManagedListSafe(pGen, peaReplies, GuildEventReply, true);
}

AUTO_STRUCT;
typedef struct GuildDayRow
{
	int iDay; AST(NAME(Day))
	int iWeekDay; AST(NAME(WeekDay))
} GuildDayRow;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("guild_GetDayListFromMonth");
void gclGuild_expr_GetDayListFromMonth(SA_PARAM_OP_VALID ExprContext *pContext, int iMonth, int iYear)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (pGen)
	{
		GuildDayRow ***peaModelRow = ui_GenGetManagedListSafe(pGen, GuildDayRow);
		int iDay, iWeekDay;
		int iDaysInMonth = timeDaysInMonth(iMonth, iYear);
		U32 iTimeStamp = timeSecondsSince2000();
		struct tm myTime;

		ANALYSIS_ASSUME(pGen != NULL);
		if (!peaModelRow)
		{
			ui_GenSetListSafe(pGen, NULL, GuildDayRow);
			return;
		}

		timeMakeLocalTimeStructFromSecondsSince2000(iTimeStamp, &myTime);
		if (myTime.tm_year + 1900 == iYear && myTime.tm_mon == iMonth)
		{
			iDay = myTime.tm_mday;
			iWeekDay = myTime.tm_wday;
		}
		else
		{
			iDay = 1;
			iWeekDay = time_GetWeekdayFromDate(iYear, iMonth, iDay);
		}

		eaClear(peaModelRow);
		for (; iDay <= iDaysInMonth; iDay++)
		{
			GuildDayRow *pRow = StructCreate(parse_GuildDayRow);
			pRow->iDay = iDay;
			pRow->iWeekDay = iWeekDay;
			iWeekDay = (iWeekDay + 1) % 7;
			eaPush(peaModelRow, pRow);
		}

		ui_GenSetListSafe(pGen, peaModelRow, GuildDayRow);
	}
}

////////////////////////////////////////////////////
// Guild Recruit Search
////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct GuildRecruitCatData {
	int bThisIsACategory;
	int bMyGuildInThisTag;
	const char *pcName;				AST(POOL_STRING NAME("Name"))
	const char *displayNameMsg;
	F32 fOrder;
} GuildRecruitCatData;

static GuildRecruitInfoList *s_pGuildRecruitInfoList = NULL; // Search results
static U32 s_uiGuildRecruteCooldown = 0;
static bool s_bGuildRecruteWaitingOnSearch = false;
static GuildRecruitSearchRequest *s_pGuildRecruitSearchRequest = NULL;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildRecruitGetSearchWaiting);
bool exprGuildRecruitGetSearchWaiting(void)
{
	U32 now = timeSecondsSince2000();
	if (s_bGuildRecruteWaitingOnSearch && now > s_uiGuildRecruteCooldown && now - s_uiGuildRecruteCooldown > 10)
	{
		s_bGuildRecruteWaitingOnSearch = false;
	}
	return s_bGuildRecruteWaitingOnSearch;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildRecruitGetSearchCooldown);
U32 exprGuildRecruitGetSearchCooldown(void)
{
	U32 now = timeSecondsSince2000();
	return (s_uiGuildRecruteCooldown >= now) ? 0 : now - s_uiGuildRecruteCooldown;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Guild);
void gclGuildSetRecruitInfoList(GuildRecruitInfoList *pGuildRecruitInfoList)
{
	if (pGuildRecruitInfoList) {
		if (!s_pGuildRecruitInfoList) {
			s_pGuildRecruitInfoList = StructClone(parse_GuildRecruitInfoList, pGuildRecruitInfoList);
		} else {
			StructCopy(parse_GuildRecruitInfoList, pGuildRecruitInfoList, s_pGuildRecruitInfoList, 0, 0, 0);
		}
		printf("Time Taken: %f\n", pGuildRecruitInfoList->timeTaken);
		s_uiGuildRecruteCooldown = timeSecondsSince2000();
	}
	s_bGuildRecruteWaitingOnSearch = false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildRecruitRequestSortedSearch);
void exprGuildRecruitRequestSortedSearch(SA_PARAM_OP_VALID Entity *pEnt, const char *searchText)
{
	if (!s_pGuildRecruitSearchRequest) {
		s_pGuildRecruitSearchRequest = StructCreate(parse_GuildRecruitSearchRequest);
	}

	SAFE_FREE(s_pGuildRecruitSearchRequest->stringSearch);
	s_pGuildRecruitSearchRequest->stringSearch = StructAllocString(searchText);

	ServerCmd_gslGuild_GetGuildsForSearch(s_pGuildRecruitSearchRequest);
	s_bGuildRecruteWaitingOnSearch = true;
	s_uiGuildRecruteCooldown = timeSecondsSince2000();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildClearCatsForSearchRequest);
void exprGuildClearCatsForSearchRequest(void)
{
	if (s_pGuildRecruitSearchRequest) {
		eaClearStruct(&s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat, parse_GuildRecruitSearchCat);
		eaClearStruct(&s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat, parse_GuildRecruitSearchCat);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildSetCatForSearchRequest);
void exprGuildSetCatForSearchRequest(SA_PARAM_OP_VALID Entity *pEnt, const char *category, int bInclude, int bExclude)
{
	int i;
	const char *temp = allocAddString(category);

	if (!s_pGuildRecruitSearchRequest) {
		s_pGuildRecruitSearchRequest = StructCreate(parse_GuildRecruitSearchRequest);
	}

	if (bInclude && bExclude)
	{
		//Toggle to the opposite of what it was previously
		for (i = eaSize(&s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat)-1; i >= 0; --i)
		{
			if (s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat[i]->pcName == temp) break;
		}
		if (i >= 0)
		{
			bInclude = false;
		}
		else
		{
			bExclude = false;
		}
	}

	for (i = eaSize(&s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat)-1; i >= 0; --i)
	{
		if (s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat[i]->pcName == temp)
		{
			if (!bInclude)
			{
				StructDestroy(parse_GuildRecruitSearchCat, s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat[i]);
				eaRemove(&s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat, i);
			}
			break;
		}
	}
	if (i < 0 && bInclude)
	{
		GuildRecruitSearchCat *grsc = StructCreate(parse_GuildRecruitSearchCat);
		grsc->pcName = allocAddString(category);
		eaPush(&s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat, grsc);
	}

	for (i = eaSize(&s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat)-1; i >= 0; --i)
	{
		if (s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat[i]->pcName == temp)
		{
			if (!bExclude)
			{
				StructDestroy(parse_GuildRecruitSearchCat, s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat[i]);
				eaRemove(&s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat, i);
			}
			break;
		}
	}
	if (i < 0 && bExclude)
	{
		GuildRecruitSearchCat *grsc = StructCreate(parse_GuildRecruitSearchCat);
		grsc->pcName = allocAddString(category);
		eaPush(&s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat, grsc);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildIsCatForSearchIncluded);
int exprGuildIsCatForSearchIncluded(SA_PARAM_OP_VALID Entity *pEnt, const char *category)
{
	int i;
	const char *temp = allocAddString(category);

	if (!s_pGuildRecruitSearchRequest) {
		s_pGuildRecruitSearchRequest = StructCreate(parse_GuildRecruitSearchRequest);
	}

	for (i = eaSize(&s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat)-1; i >= 0; --i)
	{
		if (s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat[i]->pcName == temp)
		{
			return 1;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildIsCatForSearchExcluded);
int exprGuildIsCatForSearchExcluded(SA_PARAM_OP_VALID Entity *pEnt, const char *category)
{
	int i;
	const char *temp = allocAddString(category);

	if (!s_pGuildRecruitSearchRequest) {
		s_pGuildRecruitSearchRequest = StructCreate(parse_GuildRecruitSearchRequest);
	}

	for (i = eaSize(&s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat)-1; i >= 0; --i)
	{
		if (s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat[i]->pcName == temp)
		{
			return 1;
		}
	}
	return 0;
}

static const char *gclGuild_GetCatTagDispName(const char *pcName)
{
	int k, m = 0;
	GuildRecruitParam *grp = Guild_GetGuildRecruitParams();
	GuildRecruitCatDef *grcd = NULL;

	for (k = eaSize(&grp->eaGuildRecruitCatDef)-1; k >= 0; --k)
	{
		grcd = grp->eaGuildRecruitCatDef[k];
		for (m = eaSize(&grcd->eaGuildRecruitTagDef)-1; m >= 0; --m)
		{
			if (pcName == grcd->eaGuildRecruitTagDef[m]->pcName) break;
		}
		if (m >= 0) break;
	}
	if (k >= 0 && grcd)
	{
		return TranslateMessageRef(grcd->eaGuildRecruitTagDef[m]->displayNameMsg);
	}
	return NULL;
}

// Get the recruit list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getcatforsearchlist);
const char *gclGuild_expr_GetCatForSearchList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntWithGuild, int bFromGuild, int bExcludeList)
{
#define MAX_CATLISTSIZE 4096
	int i, m;
	char *pcList = exprContextAllocScratchMemory( pContext, MAX_CATLISTSIZE ), *pos;
	const char *pTempText;
	int len;

	if (pcList)
	{
		*pcList = '\0';
		pos = pcList;
		if (bFromGuild)
		{
			Guild* pGuild = guild_GetGuild(pEntWithGuild);
			if (pGuild)
			{
				for (i = eaSize(&pGuild->eaRecruitCat)-1; i >= 0; --i)
				{
					if (!pGuild->eaRecruitCat[i]->pcName) continue;
					pTempText = gclGuild_GetCatTagDispName(pGuild->eaRecruitCat[i]->pcName);
					len = pTempText ? (int)strlen(pTempText) : 0;
					if (pTempText && (pos - pcList) < MAX_CATLISTSIZE - len - 3)
					{
						strcpy_s(pos, MAX_CATLISTSIZE - (pos - pcList) - 3, pTempText);
						pos += len;
						strcpy_s(pos, MAX_CATLISTSIZE - (pos - pcList) - 1, ", ");
						pos += 2;
					}
				}
			}
		}
		else
		{
			if (!s_pGuildRecruitSearchRequest) {
				s_pGuildRecruitSearchRequest = StructCreate(parse_GuildRecruitSearchRequest);
			}

			if (bExcludeList)
			{
				for (i = eaSize(&s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat)-1; i >= 0; --i)
				{
					if (!s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat[i]->pcName) continue;
					pTempText = gclGuild_GetCatTagDispName(s_pGuildRecruitSearchRequest->eaGuildExcludeSearchCat[i]->pcName);
					len = pTempText ? (int)strlen(pTempText) : 0;
					if (pTempText && (pos - pcList) < MAX_CATLISTSIZE - len - 3)
					{
						strcpy_s(pos, MAX_CATLISTSIZE - (pos - pcList) - 3, pTempText);
						pos += len;
						strcpy_s(pos, MAX_CATLISTSIZE - (pos - pcList) - 1, ", ");
						pos += 2;
					}
				}
			}
			else
			{
				for (i = eaSize(&s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat)-1; i >= 0; --i)
				{
					if (!s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat[i]->pcName) continue;
					pTempText = gclGuild_GetCatTagDispName(s_pGuildRecruitSearchRequest->eaGuildIncludeSearchCat[i]->pcName);
					len = pTempText ? (int)strlen(pTempText) : 0;
					if (pTempText && (pos - pcList) < MAX_CATLISTSIZE - len - 3)
					{
						strcpy_s(pos, MAX_CATLISTSIZE - (pos - pcList) - 3, pTempText);
						pos += len;
						strcpy_s(pos, MAX_CATLISTSIZE - (pos - pcList) - 1, ", ");
						pos += 2;
					}
				}
			}
		}
		m = (int)strlen(pcList);
		if (m>1&&pcList[m-1]==' '&&pcList[m-2]==',')
		{
			pcList[m-2] = '\0';
		}
	}

	return pcList;
}

// Get guilds recruiting members
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getRecruitingMembers);
void gclGuild_expr_GetRecruitingMembers(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, ContainerID id)
{
	GuildRecruitMember ***peaMembers = ui_GenGetManagedListSafe(pGen, GuildRecruitMember);
	GuildRecruitInfo *pGuildRecruitInfo;
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	GuildEvent *pEvent = NULL;
	int i, iReplyCount, iOldSize, iCurSize = 0;

	if (pState)
	{
		if (!s_pGuildRecruitSearchRequest) {
			s_pGuildRecruitSearchRequest = StructCreate(parse_GuildRecruitSearchRequest);
		}

		pGuildRecruitInfo = NULL;
		for (i = eaSize(&s_pGuildRecruitInfoList->eaGuilds)-1; i >= 0; --i)
		{
			if (s_pGuildRecruitInfoList->eaGuilds[i]->iContainerID == id)
			{
				pGuildRecruitInfo = s_pGuildRecruitInfoList->eaGuilds[i];
				break;
			}
		}

		if (pGuildRecruitInfo)
		{
			iOldSize = eaSize(peaMembers);
			iReplyCount = eaSize(&pGuildRecruitInfo->eaMembers);
			iCurSize = 0;
			for (i = 0; i < iReplyCount; i++)
			{
				GuildRecruitMember *pGuildRecruitMember = pGuildRecruitInfo->eaMembers[i];
				GuildRecruitMember *pData;

				if (iCurSize >= iOldSize) {
					eaPush(peaMembers, StructCreate(parse_GuildRecruitMember));
				} else {
					StructReset(parse_GuildRecruitMember, eaGet(peaMembers, iCurSize));
				}
				pData = eaGet(peaMembers, iCurSize);
				StructCopy(parse_GuildRecruitMember, pGuildRecruitMember, pData, 0, 0, 0);

				++iCurSize;
			}
			eaSetSizeStruct(peaMembers, parse_GuildRecruitMember, iCurSize);

			// Sort the list
			PERFINFO_AUTO_START("SortMemberList",1);
			{
				UIGen *pColGen = eaGet(&pState->eaCols, pState->iSortCol);
				UIGenListColumn *pCol = pColGen ? UI_GEN_RESULT(pColGen, ListColumn) : NULL;
				UIGenListColumnState *pColState = pCol ? UI_GEN_STATE(pColGen, ListColumn) : NULL;

				if (pColState) {
					if (pCol->pchTPIField && *pCol->pchTPIField && pColState->iTPICol == -1) {
						ParserFindColumn(parse_GuildRecruitMember, pCol->pchTPIField, &pColState->iTPICol);
					}

					eaStableSortUsingColumn(peaMembers, parse_GuildRecruitMember, pColState->iTPICol);
				}
			}
			PERFINFO_AUTO_STOP();
		}
	}

	eaSetSizeStruct(peaMembers, parse_GuildRecruitMember, iCurSize);
	ui_GenSetManagedListSafe(pGen, peaMembers, GuildRecruitMember, true);
}

void gclGuild_expr_GetCategoryList_Internal(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, char *pcCategory, bool bGetEverything)
{
	GuildRecruitCatData ***peaCatData = ui_GenGetManagedListSafe(pGen, GuildRecruitCatData);
	GuildRecruitCatData *pData = NULL, *pTemp;
	GuildRecruitParam *grp = Guild_GetGuildRecruitParams();
	Guild *pGuild = guild_GetGuild(pEnt);
	int i, j, k, iCatCount, iOldSize, iCurSize = 0, iCatPos, iPrevCatPos, iTagListSize = 0;

	{
		PERFINFO_AUTO_START("BuildRecruitList",1);
		// Assemble the list of guild events
		iOldSize = eaSize(peaCatData);
		iCatCount = eaSize(&grp->eaGuildRecruitCatDef);
		iCurSize = 0;
		// Fill and sort categories first
		for (i = 0; i < iCatCount; i++)
		{
			GuildRecruitCatDef *pGuildCat = grp->eaGuildRecruitCatDef[i];

			if ((!bGetEverything) && pcCategory && stricmp(pcCategory,pGuildCat->pcName)) continue;

			if (bGetEverything || !pcCategory)
			{
				// Init category
				if (iCurSize >= iOldSize) {
					eaPush(peaCatData, StructCreate(parse_GuildRecruitCatData));
				} else {
					StructReset(parse_GuildRecruitCatData, eaGet(peaCatData, iCurSize));
				}
				pData = eaGet(peaCatData, iCurSize);
				++iCurSize;
			}
			iCatPos = iCurSize - 1;

			if (bGetEverything || pcCategory)
			{
				// Init room for tags
				iTagListSize = eaSize(&pGuildCat->eaGuildRecruitTagDef);
				for (j = iTagListSize - 1; j >= 0; --j)
				{
					if (iCurSize >= iOldSize) {
						eaPush(peaCatData, StructCreate(parse_GuildRecruitCatData));
					} else {
						StructReset(parse_GuildRecruitCatData, eaGet(peaCatData, iCurSize));
					}
					++iCurSize;
				}
			}

			if (bGetEverything || !pcCategory)
			{
				// Copy category
				pData->bThisIsACategory = 1;
				pData->bMyGuildInThisTag = 0;
				pData->pcName = allocAddString(pGuildCat->pcName);
				pData->displayNameMsg = StructAllocString(TranslateMessageRef(pGuildCat->displayNameMsg));
				pData->fOrder = pGuildCat->fOrder;

				// Sort categories
				iPrevCatPos = iCatPos - 1;
				while (iPrevCatPos >= 0)
				{
					//Find
					pTemp = eaGet(peaCatData, iPrevCatPos);
					if (!pTemp->bThisIsACategory)
					{
						--iPrevCatPos;
						continue;
					}

					//Compare
					if (pData->fOrder > pTemp->fOrder)
					{
						break;
					}
					if (pData->fOrder == pTemp->fOrder)
					{
						if (stricmp(pData->displayNameMsg,pTemp->displayNameMsg) > 0)
						{
							break;
						}
					}

					//Swap
					for (j = iCatPos; j > iPrevCatPos; --j)
					{
						pTemp = eaGet(peaCatData, j+iTagListSize);
						eaSet(peaCatData, eaGet(peaCatData, j-1), j+iTagListSize);
						eaSet(peaCatData, pTemp, j-1);
						if (pTemp == pData) iCatPos = j - 1;
					}
					if (iCatPos != j)
					{
						eaSet(peaCatData, eaGet(peaCatData, j), iCatPos);
						eaSet(peaCatData, pData, j);
					}

					//Next
					iCatPos = iPrevCatPos;
					iPrevCatPos = iCatPos - 1;
				}
			}

			if (bGetEverything || pcCategory)
			{
				// Add tags
				for (j = 0; j < iTagListSize; ++j)
				{
					GuildRecruitTagDef *pGuildTag = pGuildCat->eaGuildRecruitTagDef[j];
					pData = eaGet(peaCatData, iCatPos + j + 1);

					// Copy tag
					pData->bThisIsACategory = 0;
					pData->bMyGuildInThisTag = 0;
					pData->pcName = allocAddString(pGuildTag->pcName);
					pData->displayNameMsg = StructAllocString(TranslateMessageRef(pGuildTag->displayNameMsg));
					pData->fOrder = pGuildTag->fOrder;
					if (pGuild)
					{
						for (k = eaSize(&pGuild->eaRecruitCat)-1; k >= 0; --k)
						{
							if (pGuild->eaRecruitCat[k]->pcName == pGuildTag->pcName) break;
						}
						if (k >= 0)
						{
							pData->bMyGuildInThisTag = 1;
						}
					}

					//Sort Tags
					k = iCatPos + j + 1;
					while (k > iCatPos + 1)
					{
						//Find
						pTemp = eaGet(peaCatData, k-1);

						//Compare
						if (pData->fOrder > pTemp->fOrder)
						{
							break;
						}
						if (pData->fOrder == pTemp->fOrder)
						{
							if (stricmp(pData->displayNameMsg,pTemp->displayNameMsg) > 0)
							{
								break;
							}
						}

						//Swap
						eaSet(peaCatData, pTemp, k);
						eaSet(peaCatData, pData, k-1);

						//Next
						--k;
					}
				}
			}
		}
		PERFINFO_AUTO_STOP();
	}

	eaSetSizeStruct(peaCatData, parse_GuildRecruitCatData, iCurSize);
	ui_GenSetManagedListSafe(pGen, peaCatData, GuildRecruitCatData, true);
}

// Get the category list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getcategorylist);
void gclGuild_expr_GetCategoryList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	gclGuild_expr_GetCategoryList_Internal(pGen, pEnt, NULL, true);
}

// Get the category list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getcategorytaglist);
void gclGuild_expr_GetCategoryTagList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, char *pcCategory)
{
	gclGuild_expr_GetCategoryList_Internal(pGen, pEnt, pcCategory && *pcCategory ? pcCategory : NULL, false);
}

// Get the recruit list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getrecruitlist);
void gclGuild_expr_GetRecruitList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	GuildRecruitData ***peaRecruitData = ui_GenGetManagedListSafe(pGen, GuildRecruitData);
	GuildRecruitData *pData;
	GuildRecruitParam *grp = Guild_GetGuildRecruitParams();
	int i, j, m, iRecruitCount, iOldSize, iCurSize = 0;
	char text[2048];
	const char *pTempText;

	if (!s_pGuildRecruitInfoList) {
		s_pGuildRecruitInfoList = StructCreate(parse_GuildRecruitInfoList);
	}

	{
		PERFINFO_AUTO_START("BuildRecruitList",1);
		// Assemble the list of guild events
		iOldSize = eaSize(peaRecruitData);
		iRecruitCount = eaSize(&s_pGuildRecruitInfoList->eaGuilds);
		iCurSize = 0;
		for (i = 0; i < iRecruitCount; i++)
		{
			GuildRecruitInfo *pGuildInfo = s_pGuildRecruitInfoList->eaGuilds[i];

			if (iCurSize >= iOldSize) {
				eaPush(peaRecruitData, StructCreate(parse_GuildRecruitData));
			} else {
				StructReset(parse_GuildRecruitData, eaGet(peaRecruitData, iCurSize));
			}
			pData = eaGet(peaRecruitData, iCurSize);

			pData->iContainerID = pGuildInfo->iContainerID;
			pData->pcName = StructAllocString(pGuildInfo->pcName);
			pData->pcRecruitMessage = StructAllocString(pGuildInfo->pcRecruitMessage);
			pData->pcWebSite = StructAllocString(pGuildInfo->pcWebSite);
			pData->pcEmblem = StructAllocString(pGuildInfo->pcEmblem);
			pData->iEmblemColor0 = pGuildInfo->iEmblemColor0;
			pData->iEmblemColor1 = pGuildInfo->iEmblemColor1;
			pData->fEmblemRotation = pGuildInfo->fEmblemRotation;
			if (pGuildInfo->pcEmblem2) pData->pcEmblem2 = allocAddString(pGuildInfo->pcEmblem2);
			pData->iEmblem2Color0 = pGuildInfo->iEmblem2Color0;
			pData->iEmblem2Color1 = pGuildInfo->iEmblem2Color1;
			pData->fEmblem2Rotation = pGuildInfo->fEmblem2Rotation;
			pData->fEmblem2X = pGuildInfo->fEmblem2X;
			pData->fEmblem2Y = pGuildInfo->fEmblem2Y;
			pData->fEmblem2ScaleX = pGuildInfo->fEmblem2ScaleX;
			pData->fEmblem2ScaleY = pGuildInfo->fEmblem2ScaleY;
			if (pGuildInfo->pcEmblem3) pData->pcEmblem3 = allocAddString(pGuildInfo->pcEmblem3);
			pData->iColor1 = pGuildInfo->iColor1;
			pData->iColor2 = pGuildInfo->iColor2;
			pData->iMinLevelRecruit = pGuildInfo->iMinLevelRecruit;
			pData->bHasMambers = eaSize(&pGuildInfo->eaMembers) ? 1 : 0;
			pData->pcGuildAllegiance = allocAddString(pGuildInfo->pcGuildAllegiance);
			if (text)
			{
				GuildRecruitCatDef *grcd = NULL;
				*text = '\0';
				for (j = eaSize(&pGuildInfo->eaRecruitCat)-1; j >= 0; --j)
				{
					grcd = NULL;
					pTempText = gclGuild_GetCatTagDispName(pGuildInfo->eaRecruitCat[j]->pcName);
					if (pTempText)
					{
						strcat(text, pTempText);
						strcat(text, ", ");
					}
				}
				m = (int)strlen(text);
				if (m>1&&text[m-1]==' '&&text[m-2]==',')
				{
					text[m-2] = '\0';
				}
				pData->pcRecruitCategories = StructAllocString(text);
			}

			++iCurSize;
		}
		PERFINFO_AUTO_STOP();
	}

	eaSetSizeStruct(peaRecruitData, parse_GuildRecruitData, iCurSize);
	ui_GenSetManagedListSafe(pGen, peaRecruitData, GuildRecruitData, true);
}

static GuildRecruitData s_StaticRecruitData;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Guild_SetStaticRecruitData);
void gclGuild_expr_SetStaticRecruitData(SA_PARAM_OP_VALID GuildRecruitData *pRecruitData)
{
	if (pRecruitData)
	{
		StructCopyAll(parse_GuildRecruitData, pRecruitData, &s_StaticRecruitData);
	}
	else
	{
		StructReset(parse_GuildRecruitData, &s_StaticRecruitData);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Guild_GetStaticRecruitData);
SA_RET_NN_VALID GuildRecruitData *gclGuild_expr_GetStaticRecruitData(void)
{
	return &s_StaticRecruitData;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_setRecruitMessage);
void gclGuild_expr_SetRecruitMessage(const char* pcRecruitMessage)
{
	ServerCmd_Guild_SetRecruitMessage(pcRecruitMessage);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_setWebSite);
void gclGuild_expr_SetWebSite(const char* pcWebSite)
{
	ServerCmd_Guild_SetWebSite(pcWebSite);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getRecruitMessage);
const char* gclGuild_expr_GetRecruitMessage(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	//make sure we're a member of the guild
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL && pGuild->pcRecruitMessage != NULL )
		{
			//retrive the Recruit Message
			return pGuild->pcRecruitMessage;
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getWebSite);
const char* gclGuild_expr_GetWebSite(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	//make sure we're a member of the guild
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL && pGuild->pcWebSite != NULL )
		{
			//retrive the Web Site
			return pGuild->pcWebSite;
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_getMinLevelToRecruit);
int gclGuild_expr_GetMinLevelToRecruit(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	//make sure we're a member of the guild
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			return pGuild->iMinLevelRecruit;
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_isRecruitHidden);
int gclGuild_expr_IsRecruitHidden(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	//make sure we're a member of the guild
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			return pGuild->bHideRecruitMessage ? 1 : 0;
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(guild_isRecruitMembersHidden);
int gclGuild_expr_IsRecruitMembersHidden(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	//make sure we're a member of the guild
	if (guild_IsMember( pEnt ))
	{
		Guild* pGuild = GET_REF(pEnt->pPlayer->pGuild->hGuild);

		if ( pGuild != NULL )
		{
			return pGuild->bHideMembers ? 1 : 0;
		}
	}

	return 0;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gclGuild_SendEventReminder(char *messageKey, char *title, int starttime)
{
	Entity *pEnt = entActivePlayerPtr();
	char *estrTemp = NULL;

	entFormatGameMessageKey(pEnt, &estrTemp, messageKey,
		STRFMT_STRING("title", title),
		STRFMT_DATETIME("Time", starttime),
		STRFMT_END);
	notify_NotifySend(pEnt, kNotifyType_GuildMotD, estrTemp, NULL, NULL);
	estrDestroy(&estrTemp);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Guild_GetInviter);
const char* gclGuild_GetInviter(SA_PARAM_OP_VALID Entity* pEnt)
{
	return SAFE_MEMBER3(pEnt, pPlayer, pGuild, pcInviterName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Guild_GetInviterGuildName);
const char* gclGuild_GetInviterGuildName(SA_PARAM_OP_VALID Entity* pEnt)
{
	PlayerGuild *pPlayerGuild = SAFE_MEMBER2(pEnt, pPlayer, pGuild);
	Guild *pGuild = pPlayerGuild ? GET_REF(pPlayerGuild->hGuild) : NULL;
	return pGuild ? pGuild->pcName : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Guild_IsSystemGuild);
bool gclGuild_IsSystemGuild(SA_PARAM_OP_VALID Entity* pEnt)
{
	PlayerGuild *pPlayerGuild = SAFE_MEMBER2(pEnt, pPlayer, pGuild);
	Guild *pGuild = pPlayerGuild ? GET_REF(pPlayerGuild->hGuild) : NULL;
	return pGuild ? pGuild->bIsOwnedBySystem : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Guild_GetGuildMapGuests);
void gclGuild_GetGuildMapGuests(SA_PARAM_NN_VALID UIGen *pGen)
{
	GuildMapGuest*** peaData = ui_GenGetManagedListSafe(pGen, GuildMapGuest);
	Entity* pPlayerEnt = entActivePlayerPtr();
	Guild* pGuild = guild_IsMember(pPlayerEnt) ? guild_GetGuild(pPlayerEnt) : NULL;
	int iCount = 0;

	if (s_uGuildOwnerID && pGuild && pGuild->iContainerID == s_uGuildOwnerID)
	{
		EntityIterator* pIter = entGetIteratorSingleType(PARTITION_CLIENT, 0, 0, GLOBALTYPE_ENTITYPLAYER);
		Entity* pEnt;
		
		while ((pEnt = EntityIteratorGetNext(pIter)))
		{
			if (!guild_FindMemberInGuild(pEnt, pGuild))
			{
				GuildMapGuest* pGuest = eaGetStruct(peaData, parse_GuildMapGuest, iCount++);
				pGuest->pchName = entGetLocalName(pEnt);
				pGuest->uEntID = entGetContainerID(pEnt);
			}
		}
		EntityIteratorRelease(pIter);
	}

	eaSetSizeStruct(peaData, parse_GuildMapGuest, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, GuildMapGuest, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Guild_IsGuildMapForMember);
bool gclGuild_IsGuildMapForMember(SA_PARAM_OP_VALID Entity* pEnt)
{
	ContainerID uGuildID = guild_IsMember(pEnt) ? guild_GetGuildID(pEnt) : 0;
	if (uGuildID && uGuildID == s_uGuildOwnerID)
	{
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Guild_GetGuildIDForCurrentMapInvite);
U32 gclGuild_GetGuildIDForCurrentMapInvite(SA_PARAM_OP_VALID Entity* pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		GuildMapInvite* pInvite = eaGet(&pEnt->pPlayer->eaGuildMapInvites, 0);
		if (pInvite)
		{
			return pInvite->uGuildID;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Guild_GetGuildNameForCurrentMapInvite);
const char* gclGuild_GetGuildNameForCurrentMapInvite(SA_PARAM_OP_VALID Entity* pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		GuildMapInvite* pInvite = eaGet(&pEnt->pPlayer->eaGuildMapInvites, 0);
		if (pInvite)
		{
			return pInvite->pchGuildName;
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildMemberGetFullName);
const char *gclGuildExprMemberGetFullName(ExprContext *pContext, S32 iMemberID)
{
	Entity *pEnt = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pEnt);
	if (pGuild)
	{
		GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iMemberID);
		if (pMember)
		{
			const char *pcName = pMember->pcName ? pMember->pcName : "";
			const char *pcAccount = pMember->pcAccount ? pMember->pcAccount : "";
			S32 pchBuffer_size = (S32)(strlen(pcName) + strlen(pcAccount) + 2);
			char *pchBuffer = (char *)alloca(pchBuffer_size);
			strcpy_s(SAFESTR2(pchBuffer), pcName);
			strcpy_s(SAFESTR2(pchBuffer), "@");
			strcpy_s(SAFESTR2(pchBuffer), pcAccount);
			return exprContextAllocString(pContext, pchBuffer);
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildMemberGetCharacterName);
const char *gclGuildExprMemberGetCharacterName(ExprContext *pContext, S32 iMemberID)
{
	Entity *pEnt = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pEnt);
	if (pGuild)
	{
		GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iMemberID);
		if (pMember)
			return exprContextAllocString(pContext, pMember->pcName);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildMemberGetAccountName);
const char *gclGuildExprMemberGetAccountName(ExprContext *pContext, S32 iMemberID)
{
	Entity *pEnt = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pEnt);
	if (pGuild)
	{
		GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iMemberID);
		if (pMember)
			return exprContextAllocString(pContext, pMember->pcAccount);
	}
	return "";
}

#define GUILD_NO_WITHDRAW_LIMIT 2000000000

// Get the total items that can still be withdrawn for the day from this guild tab (bag)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildGetItemWithdrawLeft);
S32 gclGuildGetItemWithdrawLeft(ExprContext *pContext, S32 iBagID)
{
	Entity *pEnt = entActivePlayerPtr();
	Entity *pGuildBank = guild_GetGuildBank(pEnt);
	GuildMember* pGuildMember =	guild_FindMember(pEnt);

	NOCONST(GuildBankTabInfo) *pGuildBankInfo = inv_GuildbankGetBankTabInfo(pGuildBank, iBagID);

	if(pGuildBankInfo && pGuildMember)
	{
		U32 iTimestamp = timeServerSecondsSince2000();
		S32 iRank = pGuildMember->iRank;
		S32 iWithdrawTotal;
		S32 iWithdrawLimit;
		NOCONST(GuildWithdrawLimit) *pWithdrawLimit = CONTAINER_NOCONST(GuildWithdrawLimit, eaIndexedGetUsingInt(&pGuildMember->eaWithdrawLimits, iBagID));
		
		if(iRank < 0 || iRank >= eaSize(&pGuildBankInfo->eaPermissions))
		{
			return 0;
		}

		iWithdrawLimit = pGuildBankInfo->eaPermissions[iRank]->iWithdrawItemCountLimit;

		if((pGuildBankInfo->eaPermissions[iRank]->ePerms & GuildPermission_Withdraw) == 0)
		{
			return 0;
		}

		if(iWithdrawLimit <=0)
		{
			// no limit
			return GUILD_NO_WITHDRAW_LIMIT;
		}

		if(!gConf.bEnableGuildItemWithdrawLimit)
		{
			// no limit
			return GUILD_NO_WITHDRAW_LIMIT;
		}

		iWithdrawTotal = (NONNULL(pWithdrawLimit) && (iTimestamp - pWithdrawLimit->iTimestamp) < WITHDRAW_LIMIT_TIME) ? pWithdrawLimit->iItemsWithdrawn : 0;

		return iWithdrawLimit - iWithdrawTotal;

	}

	return 0;
}

// Is there a withdraw limit for items for this character and bag?
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildNoItemWithdrawLimit);
bool gclGuildNoItemWithdrawLimit(ExprContext *pContext, S32 iBagID)
{
	S32 iLimit = gclGuildGetItemWithdrawLeft(pContext, iBagID);

	if(iLimit >= GUILD_NO_WITHDRAW_LIMIT)
	{
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen);
void Guild_SetAllowInvites(bool bAllowInvites)
{
	ServerCmd_gslGuild_AllowInvites(bAllowInvites);
}

AUTO_EXPR_FUNC(UIGen);
bool Guild_GetAllowInvites()
{
	Entity *pEnt = entActivePlayerPtr();
	return !SAFE_MEMBER3(pEnt, pPlayer, pUI, bDisallowGuildInvites);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetGuildBankTabPurchaseNumericName);
const char *GuildBank_GetGuildBankTabPurchaseNumericName(void)
{
    return guildBankConfig_GetBankTabPurchaseNumericName();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetGuildBankTabUnlockNumericName);
const char *GuildBank_GetGuildBankTabUnlockNumericName(void)
{
    return guildBankConfig_GetBankTabUnlockNumericName();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetGuildBankTabPurchaseCost);
U32 GuildBank_GetGuildBankTabPurchaseCost(U32 tabIndex)
{
    return guildBankConfig_GetBankTabPurchaseCost(tabIndex);
}

#include "AutoGen/GuildUI_h_ast.c"
#include "AutoGen/GuildUI_c_ast.c"
