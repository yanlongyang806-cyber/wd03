/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Expression.h"
#include "earray.h"

#include "UIGen.h"

#include "gclLogin.h"
#include "MapDescription.h"
#include "EditorPrefs.h"

#include "StringCache.h"
#include "StringUtil.h"
#include "loginCommon.h"
#include "Login2Common.h"

#include "AutoGen/ServerSelection_c_ast.h"
#include "AutoGen/MapDescription_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static int gTreeMode = 0;
//static int gTreeModeChanged = 1;

AUTO_STRUCT;
typedef struct ServerChoice 
{
	U32 bFolder:1;
	U32 bIsOpen:1;
	U32 bLastMap:1;
	U32 bNewMap:1;
	U32 iIndent;
	ContainerID containerID;
	int mapInstanceIndex;
	MapChoiceType eChoiceType;
	int iNumPlayers;
	char *estrFolderName;						AST(ESTRING NAME("FolderName"))
	char *estrDisplayName;						AST(ESTRING NAME("DisplayName") NAME(MapDescription))
	PossibleMapChoice *pPossibleMapChoice;		AST(UNOWNED)
} ServerChoice;

AUTO_STRUCT;
typedef struct TreeBranch 
{
	char* estrBranch;	AST(ESTRING)
	int iImediateChildCount;
	int iVisibleChildCount;
} TreeBranch;

// Reset Tree Mode.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ResetServerTreeMode);
void gcluiResetServerTreeMode(void)
{
	gTreeMode = EditorPrefGetInt("ServerSelection", "Modes", "TreeMode", 0);
}

// Set Tree Mode.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetTreeMode);
int gcluiGetTreeMode(void)
{
	return gTreeMode;
}

// Set Tree Mode.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetTreeMode);
void gcluiSetTreeMode(int treeMode)
{
	gTreeMode = treeMode;
	EditorPrefStoreInt("ServerSelection", "Modes", "TreeMode", gTreeMode);
}

// Is Tree Branch Open
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsBranchOpen);
int gcluiIsBranchOpen(const char *branch)
{
	return EditorPrefGetInt("ServerSelection", "Folders", branch, 0);
}

// Open a Tree Branch.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OpenBranch);
void gcluiOpenBranch(const char *branch)
{
	int iBranch = EditorPrefGetInt("ServerSelection", "Folders", branch, 0);

	if (!iBranch)
	{
		EditorPrefStoreInt("ServerSelection", "Folders", branch, 1);
	}
}

// Close a Tree Branch.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CloseBranch);
void gcluiCloseBranch(const char *branch)
{
	int iBranch = EditorPrefGetInt("ServerSelection", "Folders", branch, 0);

	if (iBranch)
	{
		EditorPrefStoreInt("ServerSelection", "Folders", branch, 0);
	}
}

static void gcluiAddBranches(const char *base, int offset, TreeBranch ***peaBranches)
{
	int len = offset;
	const char *underscore;

	do
	{
		//New Branch
		TreeBranch *newBranch;

		underscore = base + len;
		while (*underscore)
		{
			if (*underscore == '_') break;
			if (*underscore >= '0' && *underscore <= '9' && underscore != base + len)
			{
				if ((underscore[(-1)] < '0' || underscore[(-1)] > '9') && underscore[(-1)] != '_')
				{
					//Number after a letter
					--underscore;
					break;
				}
			}
			if (((*underscore >= 'a' && *underscore <= 'z') || (*underscore >= 'A' && *underscore <= 'Z')) && underscore != base + len)
			{
				if (underscore[(-1)] >= '0' && underscore[(-1)] <= '9')
				{
					//Letter after a number
					--underscore;
					break;
				}
			}
			++underscore;
		}

		len = underscore - base + 1;

		if (!*underscore) 
			--len;

		if (len - offset > 1)
		{
			newBranch = StructCreate(parse_TreeBranch);
			assert(newBranch);
			newBranch->iImediateChildCount = 1;
			newBranch->iVisibleChildCount = 0;
			estrCopy2(&newBranch->estrBranch, base);
			estrSetSize(&newBranch->estrBranch, len);
			eaInsert(peaBranches, newBranch, 0);
		}

		offset = len;
	} while (*underscore);
}


static int gcluiRemoveBranch(TreeBranch ***peaBranches, ServerChoice ***peaChoices, int i)
{
	char buffer[2048];
	int j, l, count;

	count = 0;
	if ((*peaBranches)[0]->iImediateChildCount > 1)
	{
		//Insert Folder
		ServerChoice *tempPMC = StructCreate(parse_ServerChoice);
		tempPMC->bFolder = true;
		tempPMC->bIsOpen = EditorPrefGetInt("ServerSelection", "Folders", (*peaBranches)[0]->estrBranch, 0);;

		count = (*peaBranches)[0]->iImediateChildCount + (*peaBranches)[0]->iVisibleChildCount;
		j = i - count;
		sprintf(buffer, "[%c] %s*", (tempPMC->bIsOpen ? '-' : '+'), (*peaBranches)[0]->estrBranch);
		estrCopy2(&tempPMC->estrDisplayName, buffer);
		string_toupper(tempPMC->estrDisplayName);
		estrCopy2(&tempPMC->estrFolderName, (*peaBranches)[0]->estrBranch);
		eaInsert(peaChoices, tempPMC, j);
		++i;
		++j;

		if (!tempPMC->bIsOpen)
		{
			//Closed Branch
			for (; count > 0; count--)
			{
				ServerChoice *pChoice = (*peaChoices)[j];
				StructDestroy(parse_ServerChoice, pChoice);
				eaRemove(peaChoices, j);
				--i;
			}
			count = 0;
		}
		else
		{
			//Open Branch
			for (l = j; l < j + count; ++l)
			{
				ServerChoice *pChoice = (*peaChoices)[l];
				sprintf(buffer, ".....%s", pChoice->estrDisplayName);
				estrCopy2(&pChoice->estrDisplayName, buffer);
			}
		}
	}
	else
	{
		count = (*peaBranches)[0]->iVisibleChildCount;
	}

	StructDestroy(parse_TreeBranch, eaRemoveVoid(peaBranches, 0));

	if (eaSize(peaBranches))
	{
		(*peaBranches)[0]->iVisibleChildCount += count;
	}

	return i;
}

// Get a list of valid game servers to log into.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetServerList);
void gcluiGetServerList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, g_pGameServerChoices ? &g_pGameServerChoices->ppChoices : NULL, parse_PossibleMapChoice);
}


static char s_ServerNameFilter[128] = "";
static int s_ServerNameFilterLen = 0;

// Sets the filter on the server name list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetServerListFilter);
void gcluiSetServerListFilter(ExprContext *pContext, const char* pchFilter)
{
	int i = 0;
	bool bNewToken = false;

	while (*pchFilter && i < ARRAY_SIZE_CHECKED(s_ServerNameFilter) - 1)
	{
		while (*pchFilter == ' ' || *pchFilter == ',' || *pchFilter == '\t' || *pchFilter == '\r' || *pchFilter == '\n')
		{
			bNewToken = true;
			pchFilter++;
		}

		if (!*pchFilter)
		{
			break;
		}

		if (bNewToken)
		{
			if (i < ARRAY_SIZE_CHECKED(s_ServerNameFilter) - 2)
			{
				s_ServerNameFilter[i++] = '\0';
			}
			else
			{
				// can't hold any more tokens
				break;
			}

			bNewToken = false;
		}

		s_ServerNameFilter[i++] = *pchFilter;
		pchFilter++;
	}

	s_ServerNameFilter[i] = '\0';
	s_ServerNameFilterLen = i;

	//gTreeModeChanged = 1;
}

static bool gcluiUnfilteredMap(const char *pchMapDescription)
{
	char *pcCur;
	char *pcLast;

	if (s_ServerNameFilterLen == 0)
	{
		return true;
	}

	pcCur = s_ServerNameFilter;
	pcLast = s_ServerNameFilter + s_ServerNameFilterLen;

	while (pcCur < pcLast)
	{
		if (*pcCur)
		{
			if (!strstri(pchMapDescription, pcCur))
			{
				return false;
			}

			// Skip ahead to the next token
			while (*pcCur && pcCur < pcLast)
			{
				pcCur++;
			}
		}

		pcCur++;
	}

	return true;
}

static __forceinline bool isletter(char c)
{
	char cUpper = toupper(c);
	return (cUpper >= 'A' && cUpper <= 'Z');
}

static __forceinline bool isnumber(char c)
{
	return (c >= '0' && c <= '9');
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenServerListGetIndexFromLetter);
int gcluiGenServerListGetIndexFromLetter(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, char *pcLetter)
{
	ServerChoice ***peaChoices = ui_GenGetManagedListSafe(pGen, ServerChoice);
	int i, j;
	char cLetter;
	const char *pcText;

	if (!pcLetter || !peaChoices) 
		return 0;

	cLetter = *pcLetter;
	cLetter = toupper(cLetter);

	if (!isletter(cLetter) && !isnumber(cLetter)) 
		return 0;

	for (i = 0; i < eaSize(peaChoices); ++i)
	{
		ServerChoice *pServerChoice = (*peaChoices)[i];
		char c;

		if (pServerChoice->bLastMap) 
			continue;

		if (pServerChoice->bFolder)
		{
			pcText = pServerChoice->estrFolderName;
		}
		else
		{
			if (!pServerChoice->bNewMap) 
				continue;
			pcText = pServerChoice->estrDisplayName;
		}
		
		//Find first letter or number, skipping all the ...'s
		j = 0;
		while (pcText[j] && (pcText[j] == '.'))
		{
			++j;
		}

		if (!pcText[j]) 
			continue;

		c = toupper(pcText[j]);

		if (c == cLetter) 
			return i;
	}
	
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ServerListGetIndexFromLetter);
int gcluiServerListGetIndexFromLetter(ExprContext *pContext, char *pcLetter)
{
	// I want to deprecate this, but it's too much effort to make the change in every release branch...
	return gcluiGenServerListGetIndexFromLetter(pContext, exprContextGetUserPtr(pContext, parse_UIGen), pcLetter);
}

// Get a list of valid game servers to log into.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetServerTreeList);
void gcluiGetServerTreeList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	ServerChoice ***peaChoices = ui_GenGetManagedListSafe(pGen, ServerChoice);
	if (g_pGameServerChoices)
	{
		int i;
		ServerChoice *pServerChoice;
		int iRowIndex = 0;
			
		for (i = 0; i < eaSize(&g_pGameServerChoices->ppChoices); ++i)
		{
			if (!g_pGameServerChoices->ppChoices[i] 
				|| !g_pGameServerChoices->ppChoices[i]->baseMapDescription.mapDescription)
			{
				continue;
			}

			if (g_pGameServerChoices->ppChoices[i]->bNewMap 
				&& !g_pGameServerChoices->ppChoices[i]->bLastMap 
				&& !gcluiUnfilteredMap(g_pGameServerChoices->ppChoices[i]->baseMapDescription.mapDescription))
			{
				continue;
			}
			pServerChoice = eaGetStruct(peaChoices, parse_ServerChoice, iRowIndex++);
			pServerChoice->pPossibleMapChoice = g_pGameServerChoices->ppChoices[i];
			estrCopy2(&pServerChoice->estrDisplayName, pServerChoice->pPossibleMapChoice->baseMapDescription.mapDescription);
			estrCopy2(&pServerChoice->estrFolderName, pServerChoice->pPossibleMapChoice->baseMapDescription.mapDescription);
			pServerChoice->bLastMap = pServerChoice->pPossibleMapChoice->bLastMap;
			pServerChoice->bNewMap = pServerChoice->pPossibleMapChoice->bNewMap;
			pServerChoice->containerID = pServerChoice->pPossibleMapChoice->baseMapDescription.containerID;
			pServerChoice->mapInstanceIndex = pServerChoice->pPossibleMapChoice->baseMapDescription.mapInstanceIndex;
			pServerChoice->eChoiceType = pServerChoice->pPossibleMapChoice->eChoiceType;
			pServerChoice->iNumPlayers = pServerChoice->pPossibleMapChoice->iNumPlayers;
			pServerChoice->bFolder = false;
		}

		eaSetSizeStruct(peaChoices, parse_ServerChoice, iRowIndex);

		if (gTreeMode)
		{
			TreeBranch **eaBranches = NULL;

			for (i = 0; i < eaSize(peaChoices); ++i)
			{
				pServerChoice = (*peaChoices)[i];
				if (pServerChoice->bLastMap || !pServerChoice->bNewMap) 
					continue;

				if (eaSize(&eaBranches))
				{
					if (!strnicmp(eaBranches[0]->estrBranch, pServerChoice->estrFolderName, estrLength(&eaBranches[0]->estrBranch)))
					{
						eaBranches[0]->iImediateChildCount++;
						gcluiAddBranches(pServerChoice->estrFolderName, estrLength(&eaBranches[0]->estrBranch), &eaBranches);
					}
					else
					{
						do
						{
							i = gcluiRemoveBranch(&eaBranches, peaChoices, i);

							if (!eaSize(&eaBranches))
							{
								gcluiAddBranches(pServerChoice->estrFolderName, 0, &eaBranches);
								break;
							}

							if (!strnicmp(eaBranches[0]->estrBranch, pServerChoice->estrFolderName, estrLength(&eaBranches[0]->estrBranch)))
							{
								eaBranches[0]->iImediateChildCount++;
								gcluiAddBranches(pServerChoice->estrFolderName, estrLength(&eaBranches[0]->estrBranch), &eaBranches);
								break;
							}
						} while (1);
					}
				}
				else
				{
					gcluiAddBranches(pServerChoice->estrFolderName, 0, &eaBranches);
				}
			}

			while (eaSize(&eaBranches))
			{
				i = gcluiRemoveBranch(&eaBranches, peaChoices, i);
			}

			eaDestroy(&eaBranches);
		}
	}
	else
	{
		eaClearStruct(peaChoices, parse_ServerChoice);
	}
	ui_GenSetManagedListSafe(pGen, peaChoices, ServerChoice, true);
}

// Choose the given map.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoginChooseMap_Safe);
void gcluiChooseMap_Safe(SA_PARAM_NN_VALID PossibleMapChoice *pChoice, int index)
{
	gclLoginChooseMap(pChoice);
}

// Choose the given map.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LoginChooseMap);
void gcluiChooseMap(SA_PARAM_NN_VALID PossibleMapChoice *pChoice)
{
	gclLoginChooseMap(pChoice);
}


bool gbTestServerSelectionUGC = 0;

AUTO_CMD_INT(gbTestServerSelectionUGC, TestServerSelectionUGC);

// Is UGC editing allowed for this account?
AUTO_EXPR_FUNC(UIGen);
bool ServerSelection_IsUGCAllowed(void)
{
    return gclLoginGetChosenCharacterUGCEditAllowed();
}

AUTO_RUN;
void ServerSelection_StartUp(void)
{
	ui_GenInitStaticDefineVars(MapChoiceTypeEnum, "MapChoiceType_");
}

#include "AutoGen/ServerSelection_c_ast.c"