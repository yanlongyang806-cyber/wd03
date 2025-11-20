#include "gclCommandAlias.h"
#include "cmdparse.h"
#include "NameList.h"
#include "StringCache.h"
#include "StringFormat.h"
#include "StashTable.h"
#include "inputKeyBind.h"
#include "FolderCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#include "Autogen/gclCommandAlias_c_ast.h"

extern CmdList g_AliasList = {0};
extern NameList *pGlobAliasNameList = NULL;

static KeyBindProfile s_AliasProfile = { "Aliases", __FILE__, NULL, true, true, NULL, &g_AliasList,  InputBindPriorityUser };
static StashTable s_stTable;
static bool s_bInit;

static void gclCommandAliasExecute(CMDARGS);
static void gclCommandAliasExecuteReplace(CMDARGS);

// This is the default state of an aliased command. One version for
// a command with replacements, one for command without replacement.

static Cmd s_AliasTemplate = {
	0, "FakeAlias", NULL, 0, NULL, "GameClientLib", " interface ",
	{{0}} /* No args */, 0 /* flags */, NULL /* comment */, gclCommandAliasExecute, {NULL, MULTI_NONE,0,0,0}, NULL
};

static Cmd s_ArgAliasTemplate = {
	0, "FakeArgsAlias", NULL, 0, NULL, "GameClientLib",  " interface ",
	{ {"Args", MULTI_STRING, 0, 0, CMDAF_SENTENCE|CMDAF_ALLOCATED, 0,  NAMELISTTYPE_NONE, NULL }, },
	0 /* flags */, NULL /* comment */, gclCommandAliasExecuteReplace, {NULL, MULTI_NONE,0,0,0}, NULL
};

AUTO_STRUCT;
typedef struct CommandAlias
{
	const char *pchName; AST(POOL_STRING STRUCTPARAM)
	const char *pchToExecute; AST(POOL_STRING STRUCTPARAM)

	// Literal string, and not a message, because it becomes a default value
	// for a command help message elsewhere.
	const char *pchHelp; AST(ADDNAMES(Comment))

	U8 iAccessLevel;
	bool bHidden : 1;
} CommandAlias;

AUTO_STRUCT;
typedef struct CommandAliases
{
	CommandAlias **eaAliases; AST(NAME(Alias))
} CommandAliases;

const char * gclCommandAliasGetToExecute(const char* pchCmdName)
{
	CommandAlias *pAlias;
	if (s_stTable && stashFindPointer(s_stTable, pchCmdName, &pAlias) && pAlias)
	{
		return pAlias->pchToExecute;
	}
	return NULL;
}

static void gclCommandAliasExecute(CMDARGS)
{
	CommandAlias *pAlias;
	if (s_stTable && stashFindPointer(s_stTable, cmd->name, &pAlias) && pAlias)
	{
		globCmdParse(pAlias->pchToExecute);
	}
}

static void gclCommandAliasExecuteReplace(CMDARGS)
{
	CommandAlias *pAlias;
	READARGS1(unsigned char *, pchArg);
	if (s_stTable && stashFindPointer(s_stTable, cmd->name, &pAlias) && pAlias)
	{
		unsigned char *pchCommand = NULL;
		estrStackCreate(&pchCommand);
		strfmt_FromArgs(&pchCommand, pAlias->pchToExecute, STRFMT_STRING("", pchArg ? pchArg : ""), STRFMT_END);
		globCmdParse(pchCommand);
		estrDestroy(&pchCommand);
	}
}

void gclCommandAliasAddEx(const char *pchAlias, const ACMD_SENTENCE pchCommand, S32 iAccessLevel, bool bHidden, const char *pchHelp)
{
	Cmd *pCommand;
	CommandAlias *pAlias;
	char achHelp[2048];
	bool bNew = false;

	if (!s_bInit)
	{
		keybind_PushProfile(&s_AliasProfile);
		s_bInit = true;
		s_stTable = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);
	}

	if (!(pCommand = cmdListFind(&g_AliasList, pchAlias)))
	{
		pCommand = calloc(1, sizeof(*pCommand));
		bNew = true;
	}
	if (strstr(pchCommand, "{}"))
		*pCommand = s_ArgAliasTemplate;
	else
		*pCommand = s_AliasTemplate;
	cmdInit(pCommand);

	if (!(stashFindPointer(s_stTable, pchAlias, &pAlias) && pAlias))
		pAlias = StructCreate(parse_CommandAlias);

	if (!pchHelp)
	{
		sprintf(achHelp, "Alias for %s", pchCommand);
		pchHelp = achHelp;
	}

	pAlias->pchName = allocAddString(pchAlias);
	pAlias->pchToExecute = allocAddString(pchCommand);
	pAlias->iAccessLevel = iAccessLevel;
	pAlias->bHidden = bHidden;
	if (pAlias->pchHelp != pchHelp)
	{
		SAFE_FREE(pAlias->pchHelp);
		pAlias->pchHelp = StructAllocString(pchHelp);
	}
	pCommand->name = pAlias->pchName;
	pCommand->access_level = pAlias->iAccessLevel;
	pCommand->flags = pAlias->bHidden ? CMDF_HIDEPRINT : 0;
	pCommand->comment = pAlias->pchHelp;
	if (bNew)
		cmdAddSingleCmdToList(&g_AliasList, pCommand);
	stashAddPointer(s_stTable, pAlias->pchName, pAlias, true);
}

// Create a more convenient alias for a longer command. You can embed "{}" in the command
// to replace any arguments to the alias in the aliased command.
AUTO_COMMAND ACMD_NAME("alias") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void gclCommandAliasAdd(const char *pchAlias, const ACMD_SENTENCE pchCommand)
{
	gclCommandAliasAddEx(pchAlias, pchCommand, 0, false, NULL);
}

void gclCommandAliasReload(const char *pchPath, S32 iWhen)
{
	S32 i;
	CommandAliases Aliases = {0};

	loadstart_printf("Loading standard command aliases... ");
	ParserLoadFiles("ui", ".cmdalias", "CommandAliases.bin", PARSER_OPTIONALFLAG, parse_CommandAliases, &Aliases);
	for (i = 0; i < eaSize(&Aliases.eaAliases); i++)
	{
		CommandAlias *pAlias = Aliases.eaAliases[i];
		gclCommandAliasAddEx(pAlias->pchName, pAlias->pchToExecute, pAlias->iAccessLevel, pAlias->bHidden, pAlias->pchHelp);
	}
	loadend_printf("done. (%d)", i);
	StructDeInit(parse_CommandAliases, &Aliases);

	loadstart_printf("Loading user command aliases... ");
	ParserLoadFiles(NULL, "CommandAliases.txt", NULL, PARSER_OPTIONALFLAG, parse_CommandAliases, &Aliases);
	for (i = 0; i < eaSize(&Aliases.eaAliases); i++)
	{
		CommandAlias *pAlias = Aliases.eaAliases[i];
		gclCommandAliasAddEx(pAlias->pchName, pAlias->pchToExecute, pAlias->iAccessLevel, pAlias->bHidden, pAlias->pchHelp);
	}
	loadend_printf("done. (%d)", i);
	StructDeInit(parse_CommandAliases, &Aliases);
}

void gclCommandAliasLoad(void)
{
	pGlobAliasNameList = CreateNameList_CmdList(&g_AliasList, 9);
	NameList_MultiList_AddList(pAllCmdNamesForAutoComplete, pGlobAliasNameList);

	gclCommandAliasReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/*.cmdalias", gclCommandAliasReload);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "CommandAliases.txt", gclCommandAliasReload);
}

#include "AutoGen/gclCommandAlias_c_ast.c"
