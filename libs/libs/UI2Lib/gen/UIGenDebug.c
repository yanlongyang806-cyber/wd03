#include "textparser.h"
#include "tokenstore.h"
#include "cmdparse.h"
#include "Expression.h"
#include "ExpressionPrivate.h"
#include "structInternals.h"
#include "TextBuffer.h"
#include "sysutil.h"          // for winCopyToClipboard
#include "ResourceInfo.h"
#include "StringUtil.h"
#include "Message.h"
#include "StringFormat.h"
#include "BlockEarray.h"

#include "GfxSpriteText.h"
#include "GfxConsole.h"

#include "UICore_h_ast.h"
#include "UIList.h"
#include "UILabel.h"
#include "UITabs.h"
#include "UIWindow.h"
#include "UIButton.h"
#include "UITree.h"
#include "UITextureAssembly.h"

#include "UIGen.h"
#include "UIGenPrivate.h"
#include "UIGenButton.h"
#include "UIGenLayoutBox.h"
#include "UIGenList.h"
#include "UIGenSlider.h"
#include "UIGenSMF.h"
#include "UIGenTabGroup.h"
#include "UIGenText.h"
#include "UIGenTextArea.h"
#include "UIGenTextEntry.h"
#include "UIGenWidget.h"

#include "UIGen_h_ast.h"
#include "UIStyle_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct UIGenDebugUpdateItem UIGenDebugUpdateItem;
typedef void (*UIGenDebugUpdateCB)(UIGenDebugUpdateItem *pValues, void *pWidget);

typedef struct UIGenDebugUpdateItem {
	void *pData;
	UIGenDebugUpdateCB cbUpdate;

	void **eaDummy;
	F32 UI_PARENT_VALUES;
} UIGenDebugUpdateItem;

typedef enum UIGenDebugContextVarType {
	kUIGenDebugContextVarType_Int,
	kUIGenDebugContextVarType_Float,
	kUIGenDebugContextVarType_String,
	kUIGenDebugContextVarType_Pointer,
	kUIGenDebugContextVarType_StaticDefine,
} UIGenDebugContextVarType;

typedef struct UIGenDebugContextVar {
	const char *pchName;
	int eType;
	ParseTable *pTable;

	StaticDefineInt *pDefine;
	S32 *eaiValues;
} UIGenDebugContextVar;

#define UI_PARENT_ARGS_FROM_UPDATE(pUIGenDebugUpdateItem) (pUIGenDebugUpdateItem)->pX, (pUIGenDebugUpdateItem)->pY, (pUIGenDebugUpdateItem)->pW, (pUIGenDebugUpdateItem)->pH, (pUIGenDebugUpdateItem)->pScale
#define UI_PARENT_ARGS_TO_UPDATE(pUIGenDebugUpdateItem) (pUIGenDebugUpdateItem)->pX = pX, (pUIGenDebugUpdateItem)->pY = pY, (pUIGenDebugUpdateItem)->pW = pW, (pUIGenDebugUpdateItem)->pH = pH, (pUIGenDebugUpdateItem)->pScale = pScale

static UIGenDebugUpdateItem **s_eaDebugUpdateQueue;
static S32 s_iDebugUpdateCount;
static UIGenDebugContextVar **s_eaContextVars;

static int GenDebugSortContextVars(const UIGenDebugContextVar **ppLeft, const UIGenDebugContextVar **ppRight)
{
	const UIGenDebugContextVar *pLeft = *ppLeft;
	const UIGenDebugContextVar *pRight = *ppRight;

	// Cluster by category
	if (pLeft->eType != pRight->eType)
		return pLeft->eType - pRight->eType;

	// Cluster pointers by (initial) type
	if (pLeft->eType == kUIGenDebugContextVarType_Pointer)
	{
		const char *pchNameLeft = SAFE_MEMBER(pLeft->pTable, name);
		const char *pchNameRight = SAFE_MEMBER(pRight->pTable, name);
		int d = stricmp_safe(pchNameLeft, pchNameRight);
		if (d)
			return d;
	}

	// Cluster defines first by prefix, then by define name
	if (pLeft->eType == kUIGenDebugContextVarType_StaticDefine && stricmp(pLeft->pchName, pRight->pchName) == 0)
	{
		const char *pchNameLeft = FindStaticDefineName(pLeft->pDefine);
		const char *pchNameRight = FindStaticDefineName(pRight->pDefine);
		return stricmp_safe(pchNameLeft, pchNameRight);
	}

	return stricmp(pLeft->pchName, pRight->pchName);
}

void ui_GenDebugRegisterContextInt(const char *pchName)
{
	UIGenDebugContextVar *pVar = NULL;
	S32 i;
	for (i = 0; i < eaSize(&s_eaContextVars) && !pVar; i++)
		if (!stricmp(s_eaContextVars[i]->pchName, pchName))
			pVar = ZeroStruct(s_eaContextVars[i]);
	if (!pVar)
		pVar = calloc(1, sizeof(UIGenDebugContextVar));
	pVar->pchName = allocAddString(pchName);
	pVar->eType = kUIGenDebugContextVarType_Int;
	eaPush(&s_eaContextVars, pVar);
	eaQSort(s_eaContextVars, GenDebugSortContextVars);
}

void ui_GenDebugRegisterContextFloat(const char *pchName)
{
	UIGenDebugContextVar *pVar = NULL;
	S32 i;
	for (i = 0; i < eaSize(&s_eaContextVars) && !pVar; i++)
		if (!stricmp(s_eaContextVars[i]->pchName, pchName))
			pVar = ZeroStruct(s_eaContextVars[i]);
	if (!pVar)
		pVar = calloc(1, sizeof(UIGenDebugContextVar));
	pVar->pchName = allocAddString(pchName);
	pVar->eType = kUIGenDebugContextVarType_Float;
	eaPush(&s_eaContextVars, pVar);
	eaQSort(s_eaContextVars, GenDebugSortContextVars);
}

void ui_GenDebugRegisterContextString(const char *pchName)
{
	UIGenDebugContextVar *pVar = NULL;
	S32 i;
	for (i = 0; i < eaSize(&s_eaContextVars) && !pVar; i++)
		if (!stricmp(s_eaContextVars[i]->pchName, pchName))
			pVar = ZeroStruct(s_eaContextVars[i]);
	if (!pVar)
		pVar = calloc(1, sizeof(UIGenDebugContextVar));
	pVar->pchName = allocAddString(pchName);
	pVar->eType = kUIGenDebugContextVarType_String;
	eaPush(&s_eaContextVars, pVar);
	eaQSort(s_eaContextVars, GenDebugSortContextVars);
}

void ui_GenDebugRegisterContextPointer(const char *pchName, ParseTable *pTable)
{
	UIGenDebugContextVar *pVar = NULL;
	S32 i;
	for (i = 0; i < eaSize(&s_eaContextVars) && !pVar; i++)
		if (!stricmp(s_eaContextVars[i]->pchName, pchName))
			pVar = ZeroStruct(s_eaContextVars[i]);
	if (!pVar)
		pVar = calloc(1, sizeof(UIGenDebugContextVar));
	pVar->pchName = allocAddString(pchName);
	pVar->eType = kUIGenDebugContextVarType_Pointer;
	pVar->pTable = pTable;
	eaPush(&s_eaContextVars, pVar);
	eaQSort(s_eaContextVars, GenDebugSortContextVars);
}

void ui_GenDebugRegisterContextStaticDefine(StaticDefineInt *pDefine, const char *pchPrefix)
{
	UIGenDebugContextVar *pVar = NULL;
	S32 i;
	for (i = 0; i < eaSize(&s_eaContextVars) && !pVar; i++)
		if (!stricmp(s_eaContextVars[i]->pchName, pchPrefix))
			pVar = ZeroStruct(s_eaContextVars[i]);
	if (!pVar)
		pVar = calloc(1, sizeof(UIGenDebugContextVar));
	pVar->pchName = allocAddString(pchPrefix);
	pVar->eType = kUIGenDebugContextVarType_StaticDefine;
	pVar->pDefine = pDefine;
	eaPush(&s_eaContextVars, pVar);
	eaQSort(s_eaContextVars, GenDebugSortContextVars);
}

static UIGenDebugUpdateItem *GenQueueDebugUpdate(void *pData, UIGenDebugUpdateCB cbUpdate)
{
	UIGenDebugUpdateItem *pItem;

	if (!devassertmsgf(s_iDebugUpdateCount >= 0, "s_iDebugUpdateCount is %d?", s_iDebugUpdateCount))
		s_iDebugUpdateCount = 0;

	while (eaSize(&s_eaDebugUpdateQueue) <= s_iDebugUpdateCount)
	{
		pItem = calloc(1, sizeof(UIGenDebugUpdateItem));
		eaPush(&s_eaDebugUpdateQueue, pItem);
	}

	pItem = s_eaDebugUpdateQueue[s_iDebugUpdateCount++];
	pItem->pData = pData;
	pItem->cbUpdate = cbUpdate;
	return pItem;
}

//////////////////////////////////////////////////////////////////////////
// Struct Dumping

static void GenDebugStructTree_FillStruct(UITreeNode *pNode, UserData fillData);
static void GenDebugStructTree_DrawStruct(UITreeNode *pNode, UserData displayData, UI_MY_ARGS, F32 z);
static void GenDebugStructTree_ActivateStruct(UITreeNode *pNode, UserData activateData);
static UITreeNode *GenDebugStructTree_AddStruct(UITreeNode *pParent, S32 iIndex, const char *pchDisplayName, S32 iDisplayIndex, void *pPointer, ParseTable *pTable);
static S32 GenDebugStructTree_AddListStructs(UITreeNode *pParent, S32 iIndex, const char *pchDisplayName, S32 iDisplayIndex, void ***peaList, ParseTable *pTable);

static void GenDebugStructTree_FillParse(UITreeNode *pNode, UserData fillData);
static void GenDebugStructTree_DrawParse(UITreeNode *pNode, UserData displayData, UI_MY_ARGS, F32 z);
static void GenDebugStructTree_ActivateParse(UITreeNode *pNode, UserData activateData);
static UITreeNode *GenDebugStructTree_AddParse(UITreeNode *pParent, S32 iIndex, void *pPointer, ParseTable *pTable, S32 iArrayIndex, S32 iParseIndex);
static S32 GenDebugStructTree_AddParseList(UITreeNode *pParent, S32 iIndex, void *pPointer, ParseTable *pTable, S32 iArrayIndex, StructTypeField iFieldsToInclude, StructTypeField iFieldsToExclude);

static void GenDebugStructTree_FillContextVar(UITreeNode *pNode, UserData fillData);
static void GenDebugStructTree_DrawContextVar(UITreeNode *pNode, UserData displayData, UI_MY_ARGS, F32 z);
static void GenDebugStructTree_ActivateContextVar(UITreeNode *pNode, UserData activateData);
static UITreeNode *GenDebugStructTree_AddContextVar(UITreeNode *pParent, S32 iIndex, ExprContext *pContext, UIGenDebugContextVar *pVar);
static S32 GenDebugStructTree_AddListContextVars(UITreeNode *pParent, S32 iIndex, ExprContext *pContext, UIGenDebugContextVar **eaVars);

static void GenDebugStructTree_FillStruct(UITreeNode *pNode, UserData fillData)
{
	S32 iSize = 0;
	if (pNode->table != NULL && pNode->contents != NULL)
		iSize = GenDebugStructTree_AddParseList(pNode, iSize, pNode->contents, pNode->table, -1, 0, 0);
	ui_TreeNodeTruncateChildren(pNode, iSize);
}

static void GenDebugStructTree_DrawStruct(UITreeNode *pNode, UserData displayData, UI_MY_ARGS, F32 z)
{
	bool bSelected = ui_TreeIsNodeSelected(pNode->tree, pNode);
	void *contents = pNode->contents;
	const char *pchDisplayName = (const char *)displayData;
	S32 iDisplayIndex = pNode->crc;

	gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);

	if (pchDisplayName)
	{
		if (iDisplayIndex < 0)
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s: 0x%.8p; %s", pchDisplayName, contents, SAFE_MEMBER(pNode->table, name));
		else
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s[%d]: 0x%.8p; %s", pchDisplayName, iDisplayIndex, contents, SAFE_MEMBER(pNode->table, name));
	}
	else
	{
		if (iDisplayIndex < 0)
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "0x%.8p; %s", contents, SAFE_MEMBER(pNode->table, name));
		else
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "[%d]: 0x%.8p; %s", iDisplayIndex, contents, SAFE_MEMBER(pNode->table, name));
	}
}

static UITreeNode *GenDebugStructTree_AddStruct(UITreeNode *pParent, S32 iIndex, const char *pchDisplayName, S32 iDisplayIndex, void *pPointer, ParseTable *pTable)
{
	UITreeNode *pNode = eaGet(&pParent->children, iIndex);

	if (!pNode)
	{
		while (eaSize(&pParent->children) <= iIndex)
		{
			pNode = ui_TreeNodeCreate(pParent->tree, 0, NULL, NULL, GenDebugStructTree_FillStruct, NULL, GenDebugStructTree_DrawStruct, NULL, 20);
			ui_TreeNodeAddChild(pParent, pNode);
		}
	}

	pNode->contents = pPointer;
	pNode->table = pTable;
	pNode->fillF = GenDebugStructTree_FillStruct;
	pNode->fillData = NULL;
	pNode->displayF = GenDebugStructTree_DrawStruct;
	pNode->displayData = (void *) pchDisplayName;
	pNode->crc = iDisplayIndex;
	return pNode;
}

static S32 GenDebugStructTree_AddListStructs(UITreeNode *pParent, S32 iIndex, const char *pchDisplayName, S32 iDisplayIndex, void ***peaList, ParseTable *pTable)
{
	S32 i;
	for (i = 0; i < eaSize(peaList); i++)
		GenDebugStructTree_AddStruct(pParent, iIndex++, pchDisplayName, iDisplayIndex < 0 ? iDisplayIndex : iDisplayIndex + i, (*peaList)[i], pTable);
	return iIndex;
}

static S32 GenDebugStructTree_GetArrayStride(void *pDataIndirect, ParseTable *pTable, S32 iParseIndex)
{
	switch (TokenStoreGetStorageType(pTable[iParseIndex].type))
	{
	xcase TOK_STORAGE_DIRECT_FIXEDARRAY:
		switch (TOK_GET_TYPE(pTable[iParseIndex].type))
		{
			xcase TOK_U8_X: return sizeof(U8);
			xcase TOK_INT16_X: return sizeof(S16);
			xcase TOK_INT_X: return sizeof(S32);
			xcase TOK_INT64_X: return sizeof(U64);
			xcase TOK_F32_X: return sizeof(F32);
			xcase TOK_MULTIVAL_X: return sizeof(MultiVal);
		}
	xcase TOK_STORAGE_DIRECT_EARRAY:
		if (TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_STRUCT_X || TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_MULTIVAL_X)
			return beaBlockSize(pDataIndirect);
		else if (TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_INT64_X)
			return sizeof(U64);
		else
			return sizeof(S32);
	xcase TOK_STORAGE_INDIRECT_FIXEDARRAY:
		return sizeof(void *);
	xcase TOK_STORAGE_INDIRECT_EARRAY:
		return sizeof(void *);
	}
	return 0;
}

static S32 GenDebugStructTree_GetArrayLength(void *pDataIndirect, ParseTable *pTable, S32 iParseIndex)
{
	if (pDataIndirect)
	{
		switch (TokenStoreGetStorageType(pTable[iParseIndex].type))
		{
		xcase TOK_STORAGE_DIRECT_FIXEDARRAY:
		xcase TOK_STORAGE_INDIRECT_FIXEDARRAY:
			return pTable[iParseIndex].param;
		xcase TOK_STORAGE_DIRECT_EARRAY:
			if (TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_STRUCT_X || TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_MULTIVAL_X)
				return beaSize(pDataIndirect);
			else if (TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_INT64_X)
				return ea64Size((U64 **)pDataIndirect);
			else
				return ea32Size((U32 **)pDataIndirect);
		xcase TOK_STORAGE_INDIRECT_EARRAY:
			return eaSize((void ***)pDataIndirect);
		}
	}
	return 0;
}

static bool GenDebugStructTree_DereferenceArrayIndex(S32 iIndex, void **ppData, S32 *piIndex, void *pDataIndirect, ParseTable *pTable, S32 iParseIndex)
{
	// Dereference array index
	S32 iStride = GenDebugStructTree_GetArrayStride(pDataIndirect, pTable, iParseIndex);
	S32 iLength = GenDebugStructTree_GetArrayLength(pDataIndirect, pTable, iParseIndex);
	bool bArray = true;
	void *pData;

	if (iIndex >= 0 && pDataIndirect)
	{
		switch (TokenStoreGetStorageType(pTable[iParseIndex].type))
		{
		xcase TOK_STORAGE_DIRECT_FIXEDARRAY:
			pData = (char *)pDataIndirect + iIndex * iStride;
			bArray = false;
		xcase TOK_STORAGE_DIRECT_EARRAY:
			pData = *(char **)pDataIndirect + iIndex * iStride;
			bArray = false;
		xcase TOK_STORAGE_INDIRECT_FIXEDARRAY:
			pData = *(void **)((char *)pDataIndirect + iIndex * iStride);
			bArray = false;
		xcase TOK_STORAGE_INDIRECT_EARRAY:
			pData = *(void **)(*(char **)pDataIndirect + iIndex * iStride);
			bArray = false;
		}
	}
	// A bad index leaves the array flag set, to prevent display from doing weird things

	if (!bArray)
	{
		*ppData = pData;
		*piIndex = iIndex;
	}
	return bArray;
}

static bool GenDebugStructTree_DereferenceArray(UITreeNode *pNode, void **ppData, S32 *piIndex, void *pDataIndirect, ParseTable *pTable, S32 iParseIndex)
{
	S32 iIndex = eaFind(&pNode->parent->children, pNode);
	return GenDebugStructTree_DereferenceArrayIndex(iIndex, ppData, piIndex, pDataIndirect, pTable, iParseIndex);
}

static void GenDebugStructTree_FillParse(UITreeNode *pNode, UserData fillData)
{
	void *pPointer = pNode->contents;
	ParseTable *pTable = pNode->table;
	S32 iParseIndex = pNode->crc;
	void *pDataIndirect = pPointer ? (char *)pPointer + pTable[iParseIndex].storeoffset : NULL;
	void *pData = NULL;
	bool bArray = (pTable[iParseIndex].type & (TOK_EARRAY | TOK_FIXED_ARRAY)) != 0;
	bool bReference = TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_REFERENCE_X;
	bool bStruct = TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_STRUCT_X || TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_POLYMORPH_X;
	S32 iDisplayIndex = -1;
	S32 iSize = 0;

	if (fillData)
	{
		bArray = GenDebugStructTree_DereferenceArray(pNode, &pData, &iDisplayIndex, pDataIndirect, pTable, iParseIndex);
		if (bArray)
		{
			// Array dereference failed
			ui_TreeNodeTruncateChildren(pNode, iSize);
			return;
		}
	}
	else if (pDataIndirect && (pTable[iParseIndex].type & TOK_INDIRECT) != 0 && !bReference)
		pData = *(void **)pDataIndirect;
	else if (pDataIndirect && ((pTable[iParseIndex].type & TOK_INDIRECT) == 0 || bReference) && !bArray)
		pData = pDataIndirect;

	if (bArray)
	{
		// Expand out elements
		S32 iArraySize = pPointer ? TokenStoreGetNumElems(pTable, iParseIndex, pPointer, NULL) : 0;
		for (; iSize < iArraySize; iSize++)
			GenDebugStructTree_AddParse(pNode, iSize, pPointer, pTable, iSize, iParseIndex);
	}
	else
	{
		// Expand out subtable
		ParseTable *pSubTable = NULL;
		void *pSubPointer = NULL;
		switch (TOK_GET_TYPE(pTable[iParseIndex].type))
		{
		xcase TOK_REFERENCE_X:
			{
				REF_TO(void)* value = pData;
				pSubTable = resDictGetParseTable(pTable[iParseIndex].subtable);
				pSubPointer = value ? GET_REF(*value) : NULL;
			}
		xcase TOK_STRUCT_X:
			pSubTable = pTable[iParseIndex].subtable;
			pSubPointer = pData;
		xcase TOK_POLYMORPH_X:
			if (pData && pTable[iParseIndex].subtable)
			{
				ParseTable *pPolyTable = pTable[iParseIndex].subtable;
				S32 iCol;
				if (StructDeterminePolyType(pPolyTable, pData, &iCol))
					pSubTable = pPolyTable[iCol].subtable;
			}
			pSubPointer = pData;
		}
		iSize = GenDebugStructTree_AddParseList(pNode, iSize, pSubPointer, pSubTable, -1, 0, 0);
	}

	ui_TreeNodeTruncateChildren(pNode, iSize);
}

static void GenDebugStructTree_DrawParse(UITreeNode *pNode, UserData displayData, UI_MY_ARGS, F32 z)
{
	static char *s_estrLabel;
	bool bSelected = ui_TreeIsNodeSelected(pNode->tree, pNode);
	void *pPointer = pNode->contents;
	ParseTable *pTable = pNode->table;
	S32 iParseIndex = pNode->crc;
	void *pDataIndirect = pPointer ? (char *)pPointer + pTable[iParseIndex].storeoffset : NULL;
	void *pData = NULL;
	bool bArray = (pTable[iParseIndex].type & (TOK_EARRAY | TOK_FIXED_ARRAY)) != 0;
	bool bReference = TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_REFERENCE_X;
	bool bStruct = TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_STRUCT_X || TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_POLYMORPH_X;
	StaticDefineInt *pSubEnum = NULL;
	S32 iDisplayIndex = -1;
	S32 iArraySize = 0;
	S64 iIntValue = 0;
	bool bIntValue = false;
	const char *pchIntType = NULL;

	gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);

	if (displayData)
		bArray = GenDebugStructTree_DereferenceArray(pNode, &pData, &iDisplayIndex, pDataIndirect, pTable, iParseIndex);
	else if (pDataIndirect && (pTable[iParseIndex].type & TOK_INDIRECT) != 0 && !bArray && !bReference)
		pData = *(void **)pDataIndirect;
	else if (pDataIndirect && ((pTable[iParseIndex].type & TOK_INDIRECT) == 0 || bReference) && !bArray)
		pData = pDataIndirect;

	if (iDisplayIndex >= 0)
		estrPrintf(&s_estrLabel, "%s[%d]: ", pTable[iParseIndex].name, iDisplayIndex);
	else
		estrPrintf(&s_estrLabel, "%s: ", pTable[iParseIndex].name);

	switch (TOK_GET_TYPE(pTable[iParseIndex].type))
	{
	xcase TOK_U8_X: iIntValue = pData ? *(U8 *)pData : 0; bIntValue = true; pchIntType = "byte";
	xcase TOK_INT16_X: iIntValue = pData ? *(S16 *)pData : 0; bIntValue = true; pchIntType = "short";
	xcase TOK_INT_X: iIntValue = pData ? *(S32 *)pData : 0; bIntValue = true; pchIntType = "int";
	xcase TOK_INT64_X: iIntValue = pData ? *(S64 *)pData : 0; bIntValue = true; pchIntType = "long";
	xcase TOK_F32_X:
		if (!bArray)
			estrConcatf(&s_estrLabel, "%f; ", pData ? *(F32 *)pData : 0);
		estrAppend2(&s_estrLabel, "float");
	xcase TOK_STRING_X:
	acase TOK_CURRENTFILE_X:
	acase TOK_FILENAME_X:
		if (!bArray)
		{
			if (pData)
			{
				estrAppend2(&s_estrLabel, "\"");
				estrAppendEscaped(&s_estrLabel, (const char *)pData);
				estrAppend2(&s_estrLabel, "\"; ");
			}
			else
				estrAppend2(&s_estrLabel, "\"\"; ");
		}
		estrAppend2(&s_estrLabel, "string");
	xcase TOK_BOOL_X:
		if (!bArray)
			estrConcatf(&s_estrLabel, "%s; ", pData && *(U8 *)pData ? "true" : "false");
		estrAppend2(&s_estrLabel, "bool");
	xcase TOK_BOOLFLAG_X:
		if (!bArray)
			estrConcatf(&s_estrLabel, "%s; ", pData && *(U8 *)pData ? "true" : "false");
		estrAppend2(&s_estrLabel, "bool");
	xcase TOK_REFERENCE_X:
		if (!bArray)
		{
			REF_TO(void)* handle = pData;
			const char *pchType = NULL;
			bool bPresent = false;
			if (handle && IS_HANDLE_ACTIVE(*handle))
			{
				pchType = REF_STRING_FROM_HANDLE(*handle);
				bPresent = GET_REF(*handle) != NULL;
			}
			if (pchType && !bPresent)
				estrConcatf(&s_estrLabel, "%s (unavailable); ", pchType);
			else
				estrConcatf(&s_estrLabel, "%s; ", pchType ? pchType : "NULL");
		}
		if (pTable[iParseIndex].subtable)
			estrConcatf(&s_estrLabel, "%s ref", (const char *)pTable[iParseIndex].subtable);
		else
			estrAppend2(&s_estrLabel, "ref");
	xcase TOK_STRUCT_X:
		if (!bArray)
		{
			if (pTable[iParseIndex].subtable == parse_UIGen)
			{
				UIGen *pStruct = pData;
				if (pStruct)
					estrConcatf(&s_estrLabel, "%s; ", pStruct->pchName);
				else
					estrAppend2(&s_estrLabel, "NULL; ");
			}
			else if (pTable[iParseIndex].subtable == parse_UIGenChild)
			{
				UIGenChild *pStruct = pData;
				if (pStruct && REF_STRING_FROM_HANDLE(pStruct->hChild))
					estrConcatf(&s_estrLabel, "%s; ", REF_STRING_FROM_HANDLE(pStruct->hChild));
				else
					estrAppend2(&s_estrLabel, "NULL; ");
			}
			else if (pTable[iParseIndex].subtable == parse_UIGenBorrowed)
			{
				UIGenBorrowed *pStruct = pData;
				if (pStruct && REF_STRING_FROM_HANDLE(pStruct->hGen))
					estrConcatf(&s_estrLabel, "%s (%08X); ", REF_STRING_FROM_HANDLE(pStruct->hGen), pStruct->uiComplexStates);
				else
					estrAppend2(&s_estrLabel, "NULL; ");
			}
			else if (pTable[iParseIndex].subtable == parse_UISizeSpec)
			{
				UISizeSpec Default = {0};
				UISizeSpec *pStruct = pData ? pData : &Default;
				if (!pStruct)
					pStruct = &Default;
				estrConcatf(&s_estrLabel, "%f %s; ", pStruct->fMagnitude, StaticDefineIntRevLookupNonNull(UIUnitTypeEnum, pStruct->eUnit));
			}
			else if (pTable[iParseIndex].subtable == parse_UIAngle)
			{
				UIAngle Default = {0};
				UIAngle *pStruct = pData ? pData : &Default;
				estrConcatf(&s_estrLabel, "%f %s; ", pStruct->fAngle, StaticDefineIntRevLookupNonNull(UIAngleUnitTypeEnum, pStruct->eUnit));
			}
			else if (pTable[iParseIndex].subtable == parse_UIGenMessagePacket)
			{
				UIGenMessagePacket *pStruct = pData;
				if (pStruct && REF_STRING_FROM_HANDLE(pStruct->hGen))
					estrConcatf(&s_estrLabel, "%s %s; ", pStruct->pchMessageName, REF_STRING_FROM_HANDLE(pStruct->hGen));
				else if (pStruct)
					estrConcatf(&s_estrLabel, "%s; ", pStruct->pchMessageName);
				else
					estrAppend2(&s_estrLabel, "NULL; ");
			}
			else if (pTable[iParseIndex].subtable == parse_UIGenVarTypeGlob)
			{
				UIGenVarTypeGlob *pStruct = pData;
				if (pStruct)
				{
					estrConcatf(&s_estrLabel, "%s %d %f \"", pStruct->pchName, pStruct->iInt, pStruct->fFloat);
					estrAppendEscaped(&s_estrLabel, pStruct->pchString);
					estrAppend2(&s_estrLabel, "\"; ");
				}
				else
					estrAppend2(&s_estrLabel, "NULL; ");
			}
			else if (pTable[iParseIndex].subtable == parse_UIGenVarTypeGlobAndGen)
			{
				UIGenVarTypeGlobAndGen *pStruct = pData;
				if (pStruct)
				{
					estrConcatf(&s_estrLabel, "%s %d %f \"", pStruct->glob.pchName, pStruct->glob.iInt, pStruct->glob.fFloat);
					estrAppendEscaped(&s_estrLabel, pStruct->glob.pchString);
					if (REF_STRING_FROM_HANDLE(pStruct->hTarget))
						estrConcatf(&s_estrLabel, "\" %s; ", REF_STRING_FROM_HANDLE(pStruct->hTarget));
					else
						estrAppend2(&s_estrLabel, "\"; ");
				}
				else
					estrAppend2(&s_estrLabel, "NULL; ");
			}
			else if (pTable[iParseIndex].subtable == parse_Expression)
			{
				Expression *pStruct = pData;
				static char *s_pchString;
				if (pStruct)
				{
					exprGetCompleteStringEstr(pStruct, &s_pchString);
					estrAppend2(&s_estrLabel, "<& ");
					estrAppendEscaped(&s_estrLabel, s_pchString);
					estrAppend2(&s_estrLabel, " &>; ");
				}
				else
					estrAppend2(&s_estrLabel, "undefined; ");
			}
			else if (pData)
				estrConcatf(&s_estrLabel, "0x%08p; ", pData);
			else
				estrAppend2(&s_estrLabel, "NULL; ");
		}
		estrAppend2(&s_estrLabel, ((ParseTable *) pTable[iParseIndex].subtable)->name);
	xcase TOK_POLYMORPH_X:
		if (!bArray)
		{
			ParseTable *pPolyTable = pTable[iParseIndex].subtable;
			ParseTable *pSubTable = NULL;

			if (pData && pPolyTable)
			{
				S32 iCol;
				if (StructDeterminePolyType(pPolyTable, pData, &iCol))
					pSubTable = pPolyTable[iCol].subtable;
			}

			if (pSubTable)
				estrConcatf(&s_estrLabel, "0x%08p; (%s) ", pData, pSubTable->name);
			else if (pData)
				estrConcatf(&s_estrLabel, "0x%08p; ", pData);
			else
				estrAppend2(&s_estrLabel, "NULL; ");
		}
		estrAppend2(&s_estrLabel, ((ParseTable *) pTable[iParseIndex].subtable)->name);
	xcase TOK_BIT:
		if (!bArray)
			estrConcatf(&s_estrLabel, "%s; ", pPointer && TokenStoreGetBit(pTable, iParseIndex, pPointer, 0, 0) ? "true" : "false");
		estrAppend2(&s_estrLabel, "bool");
	xcase TOK_MULTIVAL_X:
		if (!bArray)
			estrConcatf(&s_estrLabel, "%s; ", MultiValPrint(pData));
		estrAppend2(&s_estrLabel, "multi");
	}

	// Special handling for integers (to display enum values)
	if (bIntValue)
	{
		if (pTable[iParseIndex].subtable && !bArray)
		{
			StaticDefineInt *pEnum = pTable[iParseIndex].subtable;
			const char *pchEnumValue = StaticDefineIntRevLookup(pEnum, (int)iIntValue);
			if (pchEnumValue)
			{
				estrConcatf(&s_estrLabel, "%s; ", pchEnumValue);
				bIntValue = false;
			}
		}

		if (bIntValue && !bArray)
		{
			switch (TOK_GET_TYPE(pTable[iParseIndex].type))
			{
			xcase TOK_U8_X: estrConcatf(&s_estrLabel, "%u; ", (U32)iIntValue);
			xcase TOK_INT16_X: estrConcatf(&s_estrLabel, "%d; ", (S32)iIntValue);
			xcase TOK_INT_X: estrConcatf(&s_estrLabel, "%d; ", (S32)iIntValue);
			xcase TOK_INT64_X: estrConcatf(&s_estrLabel, "%"FORM_LL"d; ", iIntValue);
			}
		}

		estrAppend2(&s_estrLabel, pchIntType);
	}

	// Add array count
	if (!displayData && (pTable[iParseIndex].type & (TOK_EARRAY | TOK_FIXED_ARRAY)))
		estrConcatf(&s_estrLabel, "[%d]", pPointer ? TokenStoreGetNumElems(pTable, iParseIndex, pPointer, NULL) : 0);

	gfxfont_Print(x, y + h/2, z, scale, scale, CENTER_Y, s_estrLabel);
}

static void GenDebugStructTree_Activate(UITreeNode *pNode, UserData activateData)
{
	void *pPointer = pNode->contents;
	ParseTable *pTable = pNode->table;
	S32 iParseIndex = pNode->crc;
	void *pDataIndirect = pPointer ? (char *)pPointer + pTable[iParseIndex].storeoffset : NULL;
	void *pData = NULL;
	bool bArray = (pTable[iParseIndex].type & (TOK_EARRAY | TOK_FIXED_ARRAY)) != 0;
	bool bReference = TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_REFERENCE_X;
	StaticDefineInt *pSubEnum = NULL;
	S32 iDisplayIndex = -1;
	S32 i;

	// Default to opening the current struct
	ParseTable *pFileTable = pTable;
	void *pFilePointer = pPointer;

	if (activateData)
		bArray = GenDebugStructTree_DereferenceArray(pNode, &pData, &iDisplayIndex, pDataIndirect, pTable, iParseIndex);
	else if (pDataIndirect && (pTable[iParseIndex].type & TOK_INDIRECT) != 0 && !bArray && !bReference)
		pData = *(void **)pDataIndirect;
	else if (pDataIndirect && ((pTable[iParseIndex].type & TOK_INDIRECT) == 0 || bReference) && !bArray)
		pData = pDataIndirect;

	switch (TOK_GET_TYPE(pTable[iParseIndex].type))
	{
	xcase TOK_REFERENCE_X:
		if (!bArray)
		{
			REF_TO(void)* handle = pData;
			ParseTable *pSubTable = pTable[iParseIndex].subtable ? resDictGetParseTable((const char *)pTable[iParseIndex].subtable) : NULL;
			if (pSubTable && handle)
			{
				pFileTable = pSubTable;
				pFilePointer = GET_REF(*handle);
			}
		}
	xcase TOK_STRUCT_X:
		if (!bArray)
		{
			ParseTable *pSubTable = pTable[iParseIndex].subtable;
			for (i = 0; pSubTable && pData && pSubTable[i].name; i++)
			{
				if (TOK_GET_TYPE(pSubTable[i].type) == TOK_CURRENTFILE_X || TOK_GET_TYPE(pSubTable[i].type) == TOK_FILENAME_X)
				{
					pFileTable = pSubTable;
					pFilePointer = pData;
					break;
				}
			}
		}
	xcase TOK_POLYMORPH_X:
		if (!bArray)
		{
			ParseTable *pPolyTable = pTable[iParseIndex].subtable;
			ParseTable *pSubTable = NULL;

			if (pData && pPolyTable)
			{
				S32 iCol;
				if (StructDeterminePolyType(pPolyTable, pData, &iCol))
					pSubTable = pPolyTable[iCol].subtable;
			}

			for (i = 0; pSubTable && pData && pSubTable[i].name; i++)
			{
				if (TOK_GET_TYPE(pSubTable[i].type) == TOK_CURRENTFILE_X || TOK_GET_TYPE(pSubTable[i].type) == TOK_FILENAME_X)
				{
					pFileTable = pSubTable;
					pFilePointer = pData;
					break;
				}
			}
		}
	}

	for (i = 0; pFilePointer && pFileTable[i].name; i++)
	{
		if (TOK_GET_TYPE(pFileTable[i].type) == TOK_CURRENTFILE_X || TOK_GET_TYPE(pFileTable[i].type) == TOK_FILENAME_X)
			break;
	}
	if (!pFileTable[i].name && pFileTable != pTable)
	{
		for (i = 0; pTable[i].name; i++)
		{
			if (TOK_GET_TYPE(pTable[i].type) == TOK_CURRENTFILE_X || TOK_GET_TYPE(pTable[i].type) == TOK_FILENAME_X)
			{
				pFileTable = pTable;
				pFilePointer = pPointer;
				break;
			}
		}
	}

	if (pFileTable && pFileTable[i].name
		&& (TOK_GET_TYPE(pFileTable[i].type) == TOK_CURRENTFILE_X || TOK_GET_TYPE(pFileTable[i].type) == TOK_FILENAME_X)
		&& pFilePointer)
	{
		const char **ppchFilename = (const char **)pFilePointer + pFileTable[iParseIndex].storeoffset;
		if (ppchFilename && *ppchFilename && **ppchFilename)
		{
			char achResolved[CRYPTIC_MAX_PATH];
			fileLocateWrite(*ppchFilename, achResolved);
			fileOpenWithEditor(achResolved);
		}
	}
}

static UITreeNode *GenDebugStructTree_AddParse(UITreeNode *pParent, S32 iIndex, void *pPointer, ParseTable *pTable, S32 iArrayIndex, S32 iParseIndex)
{
	UITreeNode *pNode = eaGet(&pParent->children, iIndex);
	bool bRecurse = false;

	if (!pNode)
	{
		while (eaSize(&pParent->children) <= iIndex)
		{
			pNode = ui_TreeNodeCreate(pParent->tree, 0, NULL, NULL, GenDebugStructTree_FillParse, NULL, GenDebugStructTree_DrawParse, NULL, 20);
			ui_TreeNodeAddChild(pParent, pNode);
		}
	}
	ANALYSIS_ASSUME(pNode);

	if (pPointer)
	{
		if (iArrayIndex < 0 && (pTable[iParseIndex].type & (TOK_FIXED_ARRAY | TOK_EARRAY)))
			bRecurse = TokenStoreGetNumElems(pTable, iParseIndex, pPointer, NULL) > 0;
		else if (TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_STRUCT_X)
			bRecurse = TokenStoreGetPointer(pTable, iParseIndex, pPointer, MAX(iArrayIndex, 0), NULL) != NULL;
		else if (TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_POLYMORPH_X && (pTable[iParseIndex].type & TOK_INDIRECT) != 0)
		{
			ParseTable *pPolyTable = pTable[iParseIndex].subtable;
			ParseTable *pSubTable = NULL;
			void *pDataIndirect = pPointer ? (char *)pPointer + pTable[iParseIndex].storeoffset : NULL;
			void *pData = NULL;
			S32 iCol;

			if ((pTable[iParseIndex].type & (TOK_EARRAY | TOK_FIXED_ARRAY)) != 0)
			{
				switch (TokenStoreGetStorageType(pTable[iParseIndex].type))
				{
				xcase TOK_STORAGE_INDIRECT_FIXEDARRAY:
					if (iArrayIndex < pTable[iParseIndex].param)
						pData = *(void **)((char *)pDataIndirect + iIndex * sizeof(void *));
				xcase TOK_STORAGE_INDIRECT_EARRAY:
					if (iArrayIndex < eaSize((void ***)pDataIndirect))
						pData = *(void **)(*(char **)pDataIndirect + iIndex * sizeof(void *));
				}
			}
			else if (pDataIndirect)
				pData = *(void **)pDataIndirect;

			if (pData && pPolyTable && StructDeterminePolyType(pPolyTable, pData, &iCol))
				bRecurse = true;
		}
		else if (TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_REFERENCE_X)
		{
			void *phandle = TokenStoreGetRefHandlePointer(pTable, iParseIndex, pPointer, MAX(iArrayIndex, 0), NULL);
			REF_TO(void)* handle = phandle;
			bRecurse = handle && IS_HANDLE_ACTIVE(*handle) ? GET_REF(*handle) != NULL : false;
		}

		// Don't expand these structs
		if (iArrayIndex >= 0 || (pTable[iParseIndex].type & (TOK_FIXED_ARRAY | TOK_EARRAY)) == 0)
		{
			ParseTable *pSubTable = pTable[iParseIndex].subtable;
			if (TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_REFERENCE_X)
				pSubTable = resDictGetParseTable(pTable[iParseIndex].subtable);
			bRecurse = bRecurse && pSubTable != parse_UIGen;
			bRecurse = bRecurse && pSubTable != parse_UIGenChild;
			bRecurse = bRecurse && pSubTable != parse_UIGenBorrowed;
			bRecurse = bRecurse && pSubTable != parse_UISizeSpec;
			bRecurse = bRecurse && pSubTable != parse_UIAngle;
			bRecurse = bRecurse && pSubTable != parse_UIGenMessagePacket;
			bRecurse = bRecurse && pSubTable != parse_UIGenVarTypeGlob;
			bRecurse = bRecurse && pSubTable != parse_UIGenVarTypeGlobAndGen;
			bRecurse = bRecurse && pSubTable != parse_Expression;
		}
	}

	pNode->contents = pPointer;
	pNode->table = pTable;
	pNode->fillF = bRecurse ? GenDebugStructTree_FillParse : NULL;
	pNode->fillData = iArrayIndex < 0 ? NULL : pNode;
	pNode->displayF = GenDebugStructTree_DrawParse;
	pNode->displayData = iArrayIndex < 0 ? NULL : pNode;
	pNode->crc = iParseIndex;

	if (!bRecurse && pNode->open)
		ui_TreeNodeCollapse(pNode);

	return pNode;
}

static S32 GenDebugStructTree_AddParseList(UITreeNode *pParent, S32 iIndex, void *pPointer, ParseTable *pTable, S32 iArrayIndex, StructTypeField iFieldsToInclude, StructTypeField iFieldsToExclude)
{
	S32 i;

	// Don't expand NULL pointers
	if (!pTable || !pPointer)
		return iIndex;

	FORALL_PARSETABLE(pTable, i)
	{
		if (pTable[i].type & TOK_REDUNDANTNAME)
			continue;
		if (iFieldsToInclude && (pTable[i].type & iFieldsToInclude) == 0)
			continue;
		if (iFieldsToExclude && (pTable[i].type & iFieldsToExclude) != 0)
			continue;

		switch (TOK_GET_TYPE(pTable[i].type))
		{
		xcase TOK_U8_X:
		xcase TOK_INT16_X:
		xcase TOK_INT_X:
		xcase TOK_INT64_X:
		xcase TOK_F32_X:
		xcase TOK_STRING_X:
		acase TOK_CURRENTFILE_X:
		acase TOK_FILENAME_X:
		xcase TOK_BOOL_X:
		xcase TOK_BOOLFLAG_X:
		xcase TOK_REFERENCE_X:
		xcase TOK_STRUCT_X:
		xcase TOK_POLYMORPH_X:
		xcase TOK_BIT:
		xcase TOK_MULTIVAL_X:
		xdefault:
			// unsupported type
			continue;
		}

		GenDebugStructTree_AddParse(pParent, iIndex++, pPointer, pTable, iArrayIndex, i);
	}

	return iIndex;
}

static void GenDebugStructTree_Tick(UITreeNode *pNode)
{
	S32 i;

	if (!pNode->contents || !pNode->fillF)
	{
		ui_TreeNodeCollapse(pNode);
		return;
	}

	if (pNode->open)
	{
		// Update children
		pNode->fillF(pNode, pNode->fillData);

		// Tick the children
		for (i = 0; i < eaSize(&pNode->children); i++)
			GenDebugStructTree_Tick(pNode->children[i]);
	}
	else
		ui_TreeNodeTruncateChildren(pNode, 0);
}

static int GenDebugSortStrings(const char **ppLeft, const char **ppRight)
{
	return stricmp(*ppLeft, *ppRight);
}

static void GenDebugStructTree_FillContextVar(UITreeNode *pNode, UserData fillData)
{
	ExprContext *pContext = fillData;
	UIGenDebugContextVar *pVar = pNode->contents;
	S32 iSize = 0;

	switch (pVar->eType)
	{
	xcase kUIGenDebugContextVarType_Pointer:
		{
			ParseTable *pTable;
			void *pPointer = exprContextGetVarPointerAndType(pContext, pVar->pchName, &pTable);
			iSize = GenDebugStructTree_AddParseList(pNode, iSize, pPointer, pTable, -1, 0, 0);
		}
	xcase kUIGenDebugContextVarType_StaticDefine:
		{
			S32 i;

			// Initialize the value cache
			if (!eaiSize(&pVar->eaiValues))
			{
				const char **eaKeys = NULL;
				DefineFillAllKeysAndValues(pVar->pDefine, &eaKeys, NULL);
				eaQSort(eaKeys, GenDebugSortStrings);
				for (i = 0; i < eaSize(&eaKeys); i++)
					eaiPush(&pVar->eaiValues, StaticDefineIntGetInt(pVar->pDefine, eaKeys[i]));
				eaDestroy(&eaKeys);
			}

			for (i = 0; i < eaiSize(&pVar->eaiValues); i++)
				GenDebugStructTree_AddContextVar(pNode, iSize++, NULL, pVar)->crc = pVar->eaiValues[i];
		}
	}

	ui_TreeNodeTruncateChildren(pNode, iSize);
}

static void GenDebugStructTree_DrawContextVar(UITreeNode *pNode, UserData displayData, UI_MY_ARGS, F32 z)
{
	bool bSelected = ui_TreeIsNodeSelected(pNode->tree, pNode);
	UIGen *pGen = displayData;
	ExprContext *pContext = ui_GenGetContext(pGen);
	UIGenDebugContextVar *pVar = pNode->contents;
	MultiVal *mv;

	gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);

	switch (pVar->eType)
	{
	xcase kUIGenDebugContextVarType_Int:
		mv = exprContextGetSimpleVar(pContext, pVar->pchName);
		if (mv)
		{
			ANALYSIS_ASSUME(mv);
			if (MultiValIsNumber(mv))
			{
				gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s: %"FORM_LL"d; int", pVar->pchName, MultiValGetInt(mv, NULL));
				break;
			}
		}
		
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s: int", pVar->pchName);

	xcase kUIGenDebugContextVarType_Float:
		mv = exprContextGetSimpleVar(pContext, pVar->pchName);
		if (mv)
		{
			ANALYSIS_ASSUME(mv);
			if (MultiValIsNumber(mv))
			{
				gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s: %f; float", pVar->pchName, MultiValGetFloat(mv, NULL));
				break;
			}
		}
		
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s: float", pVar->pchName);

	xcase kUIGenDebugContextVarType_String:
		mv = exprContextGetSimpleVar(pContext, pVar->pchName);
		if (mv)
		{
			ANALYSIS_ASSUME(mv);
			if (MultiValIsNumber(mv))
			{
				gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s: %s; string", pVar->pchName, MultiValGetString(mv, NULL));
				break;
			}
		}

		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s: string", pVar->pchName);

	xcase kUIGenDebugContextVarType_Pointer:
		{
			ParseTable *pTable;
			void *pPointer = exprContextGetVarPointerAndType(pContext, pVar->pchName, &pTable);
			if (pPointer && pTable)
				gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s: 0x%08p; %s", pVar->pchName, pPointer, pTable[0].name);
			else if (pVar->pTable)
				gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s: %s", pVar->pchName, pVar->pTable[0].name);
			else
				gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s: uninitialized", pVar->pchName);
		}

	xcase kUIGenDebugContextVarType_StaticDefine:
		{
			if (pNode->fillData)
				gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s*: %s; enum", pVar->pchName, FindStaticDefineName(pVar->pDefine));
			else
				gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s%s", pVar->pchName, StaticDefineIntRevLookup(pVar->pDefine, pNode->crc));
		}
	}
}

static UITreeNode *GenDebugStructTree_AddContextVar(UITreeNode *pParent, S32 iIndex, ExprContext *pContext, UIGenDebugContextVar *pVar)
{
	UITreeNode *pNode = eaGet(&pParent->children, iIndex);
	bool bRecurse = false;

	if (!pNode)
	{
		while (eaSize(&pParent->children) <= iIndex)
		{
			pNode = ui_TreeNodeCreate(pParent->tree, 0, NULL, NULL, GenDebugStructTree_FillContextVar, NULL, GenDebugStructTree_DrawContextVar, NULL, 20);
			ui_TreeNodeAddChild(pParent, pNode);
		}
	}

	ANALYSIS_ASSUME(pNode != NULL); // I believe this is correct

	switch (pVar->eType)
	{
	xcase kUIGenDebugContextVarType_Pointer:
		if (pContext)
		{
			ParseTable *pTable;
			bRecurse = exprContextGetVarPointerAndType(pContext, pVar->pchName, &pTable) != NULL;
		}
	xcase kUIGenDebugContextVarType_StaticDefine:
		if (pContext)
			bRecurse = true;
	}

	pNode->contents = pVar;
	pNode->table = NULL;
	pNode->fillF = bRecurse ? GenDebugStructTree_FillContextVar : NULL;
	pNode->fillData = pContext;
	pNode->displayF = GenDebugStructTree_DrawContextVar;
	pNode->displayData = pContext ? exprContextGetUserPtr(pContext, parse_UIGen) : NULL;

	if (!bRecurse && pNode->open)
		ui_TreeNodeCollapse(pNode);

	return pNode;
}

static S32 GenDebugStructTree_AddListContextVars(UITreeNode *pParent, S32 iIndex, ExprContext *pContext, UIGenDebugContextVar **eaVars)
{
	S32 i;
	for (i = 0; i < eaSize(&eaVars); i++)
		GenDebugStructTree_AddContextVar(pParent, iIndex++, pContext, eaVars[i]);
	return iIndex;
}

//////////////////////////////////////////////////////////////////////////

static void GenInspectLabelTick(UILabel *pLabel, UI_PARENT_ARGS)
{
	char achString[1000];
	UIGen *pGen;
	S32 iValid = 0;
	S32 iReady = 0;
	F32 fFramesRun = ui_GenGetRunRateCounter(g_GenState.aiFrameRunRate);
	F32 fExpressionsRun = ui_GenGetRunRateCounter(g_GenState.aiExpressionsRunRate);
	F32 fCSDExpressionsRun = ui_GenGetRunRateCounter(g_GenState.aiCSDExpressionsRunRate);
	F32 fActionsRun = ui_GenGetRunRateCounter(g_GenState.aiActionsRunRate);
	RefDictIterator iter;
	RefSystem_InitRefDictIterator(UI_GEN_DICTIONARY, &iter);
	while (pGen = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if (UI_GEN_NON_NULL(pGen))
			iValid++;
		if (UI_GEN_READY(pGen))
			iReady++;
	}
	MAX1(fFramesRun, 1);
	sprintf(achString, "%d referenced gens, %d valid, %d ready, %.2f act/fr (%d peak), %.2f expr/fr (%d peak)), %.2f CSDexpr/fr (%d peak)",
		RefSystem_GetDictionaryNumberOfReferentInfos(UI_GEN_DICTIONARY),
		iValid, iReady,
		fActionsRun / fFramesRun, ui_GenGetRunRateCounter(g_GenState.aiActionsPeakRate),
		fExpressionsRun / fFramesRun, ui_GenGetRunRateCounter(g_GenState.aiExpressionsPeakRate),
		fCSDExpressionsRun / fFramesRun, ui_GenGetRunRateCounter(g_GenState.aiCSDExpressionsPeakRate));
	ui_LabelSetText(pLabel, achString);
}

static void GenTypeListDraw(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **ppchOutput)
{
	const char *pchType = eaGet(pList->peaModel, iRow);
	estrPrintf(ppchOutput, "%02d - %s", StaticDefineInt_FastStringToInt(UIGenTypeEnum, pchType, INT_MIN), pchType);
}

static void GenGlobalStateListDraw(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **ppchOutput)
{
	const char *pchState = eaGet(pList->peaModel, iRow);
	S32 iState = StaticDefineInt_FastStringToInt(UIGenStateEnum, pchState, INT_MIN);
	U32 uiColor = ui_ListIsSelected(pList, pColumn, iRow) ? 0xFFFFFFFF : 0xFF;
	estrPrintf(ppchOutput, "%02d - %s", iState, pchState);
	// Bold state if it's on.
	gfxfont_SetFontEx(g_font_Active, false, ui_GenInGlobalState(iState), false, false, uiColor, uiColor);
}

static void GenFocusedLabelTick(UILabel *pLabel, UI_PARENT_ARGS)
{
	UIGen *pGen = ui_GenGetFocus();
	char *pch = NULL;
	estrStackCreate(&pch);
	if (pGen)
		estrPrintf(&pch, "Focus: %s (%sin dictionary).", pGen->pchName, (pGen == ui_GenFind(pGen->pchName, kUIGenTypeNone) ? "" : "not "));
	else
		estrPrintf(&pch, "Focus: No gen is currenly focused.");
	ui_LabelSetText(pLabel, pch);
	estrDestroy(&pch);
}

static UITab *GenGetGlobalInspectTab(void)
{
	static char **s_FakeTypeArray;
	static char **s_FakeStateArray;
	UITab *pTab = ui_TabCreate("Global");
	UILabel *pLabel = ui_LabelCreate("", 0, 0);
	UILabel *pFocused = ui_LabelCreate("", 0, 14);
	UIList *pTypeList = ui_ListCreate(NULL, &s_FakeTypeArray, 32);
	UIList *pGlobalStateList = ui_ListCreate(NULL, &s_FakeStateArray, 32);
	UI_WIDGET(pLabel)->tickF = GenInspectLabelTick;
	UI_WIDGET(pFocused)->tickF = GenFocusedLabelTick;
	eaClear(&s_FakeStateArray);
	eaClear(&s_FakeTypeArray);
	DefineFillAllKeysAndValues(UIGenTypeEnum, &s_FakeTypeArray, NULL);
	DefineFillAllKeysAndValues(UIGenStateEnum, &s_FakeStateArray, NULL);
	ui_ListAppendColumn(pTypeList, ui_ListColumnCreateText("Types", GenTypeListDraw, NULL));
	ui_ListAppendColumn(pGlobalStateList, ui_ListColumnCreateText("Global States", GenGlobalStateListDraw, NULL));
	ui_TabAddChild(pTab, pLabel);
	ui_TabAddChild(pTab, pFocused);
	ui_TabAddChild(pTab, pTypeList);
	ui_TabAddChild(pTab, pGlobalStateList);
	ui_WidgetSetPositionEx(UI_WIDGET(pTypeList), 0, UI_WIDGET(pLabel)->height + UI_STEP, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pTypeList), 0.5, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(pGlobalStateList), 0, UI_WIDGET(pLabel)->height + UI_STEP, 0, 0, UITopRight);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pGlobalStateList), 0.5, 1.f, UIUnitPercentage, UIUnitPercentage);
	return pTab;
}

static void GenStateListDraw(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **ppchOutput)
{
	UIGen *pGen = ui_GenGetHighlighted();
	if (pGen)
	{
		if (TSTB(pGen->bfStates, iRow))
			estrPrintf(ppchOutput, "%02d - 1: %s", iRow, StaticDefineIntRevLookup(UIGenStateEnum, iRow));
		else
			estrPrintf(ppchOutput, "%02d - 0: %s", iRow, StaticDefineIntRevLookup(UIGenStateEnum, iRow));
	}
	else
		estrPrintf(ppchOutput, "Waiting...");
}

static void GenStateListTick(UIList *pList, UI_PARENT_ARGS)
{
	UIGen *pGen = ui_GenGetHighlighted();
	if (pGen)
	{
		if (!pList->peaModel)
			pList->peaModel = calloc(1, sizeof(void **));
		eaSetSize(pList->peaModel, g_GenState.iMaxStates);
	}
	else if (pList->peaModel) {
		eaDestroy(pList->peaModel);
		free(pList->peaModel);
		pList->peaModel = NULL;
	}
	ui_ListTick(pList, UI_PARENT_VALUES);
}

static void GenStateListFree(UIList *pList)
{
	if (pList->peaModel)
	{
		eaDestroy(pList->peaModel);
		free(pList->peaModel);
		pList->peaModel = NULL;
	}
}

static void GenStateDefListDraw(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **ppchOutput)
{
	UIGen *pGen = ui_GenGetHighlighted();
	UIGenStateDef *pOverride = pGen ? eaGet(&pGen->eaStates, iRow) : NULL;
	if (pOverride)
	{
		U32 uiColor = ui_ListIsSelected(pList, pColumn, iRow) ? 0xFFFFFFFF : 0xFF;
		estrPrintf(ppchOutput, "%02d - %s", pOverride->eState, StaticDefineIntRevLookup(UIGenStateEnum, pOverride->eState));
		// Bold state if it's on.
		gfxfont_SetFontEx(g_font_Active, false, ui_GenInState(pGen, pOverride->eState), false, false, uiColor, uiColor);
	}
	else
		estrPrintf(ppchOutput, "Waiting...");
}

static void GenStateDefListUpdate(UIGenDebugUpdateItem *pValues, UIList *pList)
{
	UIGen *pGen = ui_GenGetHighlighted();
	pList->peaModel = pGen ? &pGen->eaStates : NULL;
}

static void GenStateDefListTick(UIList *pList, UI_PARENT_ARGS)
{
	UIGenDebugUpdateItem *pValues = GenQueueDebugUpdate(pList, GenStateDefListUpdate);
	UIGen *pGen = ui_GenGetHighlighted();
	if (pGen)
	{
		eaSetSize(&pValues->eaDummy, eaSize(&pGen->eaStates));
	}
	else
	{
		eaDestroy(&pValues->eaDummy);
	}
	pList->peaModel = &pValues->eaDummy;
	ui_ListTick(pList, UI_PARENT_VALUES);
}

static void GenComplexStateDefListDraw(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **ppchOutput)
{
	UIGen *pGen = ui_GenGetHighlighted();
	if (pGen && pGen->eaComplexStates)
	{
		UIGenComplexStateDef *pOverride = pGen ? eaGet(&pGen->eaComplexStates, iRow) : NULL;
		if (pOverride)
		{
			U32 uiColor = ui_ListIsSelected(pList, pColumn, iRow) ? 0xFFFFFFFF : 0xFF;
			estrPrintf(ppchOutput, "%s", pOverride->pCondition ? exprGetCompleteString(pOverride->pCondition) : "No Expression");
			// Bold state if it's on.
			gfxfont_SetFontEx(g_font_Active, false, (pGen->uiComplexStates & ((U32)1 << iRow)), false, false, uiColor, uiColor);
		}
		else
			estrPrintf(ppchOutput, "Waiting...");
	}
	else
		estrPrintf(ppchOutput, "Waiting...");
}

static void GenComplexStateDefListUpdate(UIGenDebugUpdateItem *pValues, UIList *pList)
{
	UIGen *pGen = ui_GenGetHighlighted();
	pList->peaModel = pGen ? &pGen->eaComplexStates: NULL;
}

static void GenComplexStateDefListTick(UIList *pList, UI_PARENT_ARGS)
{
	UIGenDebugUpdateItem *pValues = GenQueueDebugUpdate(pList, GenComplexStateDefListUpdate);
	UIGen *pGen = ui_GenGetHighlighted();
	if (pGen)
	{
		eaSetSize(&pValues->eaDummy, eaSize(&pGen->eaComplexStates));
	}
	else
	{
		eaDestroy(&pValues->eaDummy);
	}
	pList->peaModel = &pValues->eaDummy;
	ui_ListTick(pList, UI_PARENT_VALUES);
}

static void GenVarListUpdate(UIGenDebugUpdateItem *pValues, UIList *pList)
{
	UIGen *pGen = ui_GenGetHighlighted();
	pList->peaModel = pGen ? &pGen->eaVars: NULL;
}

static void GenVarListTick(UIList *pList, UI_PARENT_ARGS)
{
	UIGenDebugUpdateItem *pValues = GenQueueDebugUpdate(pList, GenVarListUpdate);
	UIGen *pGen = ui_GenGetHighlighted();
	if (pGen)
	{
		eaSetSize(&pValues->eaDummy, eaSize(&pGen->eaVars));
	}
	else
	{
		eaDestroy(&pValues->eaDummy);
	}
	pList->peaModel = &pValues->eaDummy;
	ui_ListTick(pList, UI_PARENT_VALUES);
}

static void GenVarListDrawName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **ppchOutput)
{
	UIGen *pGen = ui_GenGetHighlighted();
	UIGenVarTypeGlob *pVar = pGen ? eaGet(&pGen->eaVars, iRow) : NULL;
	if (pGen && pVar)
		estrPrintf(ppchOutput, "%s", pVar->pchName);
	else
		estrPrintf(ppchOutput, "No Var");
}

static void GenVarListDrawInt(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **ppchOutput)
{
	UIGen *pGen = ui_GenGetHighlighted();
	UIGenVarTypeGlob *pVar = pGen ? eaGet(&pGen->eaVars, iRow) : NULL;
	if (pGen && pVar)
		estrPrintf(ppchOutput, "%d", pVar->iInt);
	else
		estrPrintf(ppchOutput, "No Var");
}

static void GenVarListDrawFloat(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **ppchOutput)
{
	UIGen *pGen = ui_GenGetHighlighted();
	UIGenVarTypeGlob *pVar = pGen ? eaGet(&pGen->eaVars, iRow) : NULL;
	if (pGen && pVar)
		estrPrintf(ppchOutput, "%g", pVar->fFloat);
	else
		estrPrintf(ppchOutput, "No Var");
}

static void GenVarListDrawString(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **ppchOutput)
{
	UIGen *pGen = ui_GenGetHighlighted();
	UIGenVarTypeGlob *pVar = pGen ? eaGet(&pGen->eaVars, iRow) : NULL;
	if (pGen && pVar)
		estrPrintf(ppchOutput, "%s", pVar->pchString);
	else
		estrPrintf(ppchOutput, "No Var");
}

static void GenInspectHighlightLabelTick(UILabel *pLabel, UI_PARENT_ARGS)
{
	UIGen *pGen = ui_GenGetHighlighted();
	UIGen *pFocusedGen = ui_GenGetFocus();
	if (UI_GEN_READY(pGen))
	{
		char ach[4000];
		S32 iPriority = pGen->chPriority;
		sprintf(ach,
			"Name: %s\n"
			"Filename: %s\n"
			"Memory Usage: %.2fkB\n"
			"Padded Box: <%.1f, %.1f, %.1f, %.1f> (%.1f x %.1f)\n"
			"Unpadded Box: <%.1f, %.1f, %.1f, %.1f> (%.1f x %.1f)\n"
			"Scale: %g\n"
			"Priority: %d\n"
			"Animating: %.2g/%g seconds (%s, %d%%)\n"
			"Background: %s (%s)\n"
			"Fx: %s\n"
			"Focus: %s\n"
			"Window Id: %d\n",
			pGen->pchName, pGen->pchFilename, (StructGetMemoryUsage(parse_UIGen, pGen, true) + sizeof(UIGen)) / 1024.f,
			pGen->ScreenBox.lx, pGen->ScreenBox.ly, pGen->ScreenBox.hx, pGen->ScreenBox.hy,
			CBoxWidth(&pGen->ScreenBox), CBoxHeight(&pGen->ScreenBox),
			pGen->UnpaddedScreenBox.lx, pGen->UnpaddedScreenBox.ly, pGen->UnpaddedScreenBox.hx, pGen->UnpaddedScreenBox.hy,
			CBoxWidth(&pGen->UnpaddedScreenBox), CBoxHeight(&pGen->UnpaddedScreenBox),
			pGen->fScale,
			iPriority,
			pGen->pTweenState ? pGen->pTweenState->fElapsedTime : 0.f,
			pGen->pTweenState ? pGen->pTweenState->pInfo->fTotalTime : 0.f,
			pGen->pTweenState ? StaticDefineIntRevLookup(UITweenTypeEnum, pGen->pTweenState->pInfo->eType) : "None",
			(int)(100 * (pGen->pTweenState ? pGen->pTweenState->fElapsedTime / pGen->pTweenState->pInfo->fTotalTime : 0.f)),
			pGen->pResult->pBackground ? pGen->pResult->pBackground->pchImage : "None",
			pGen->pResult->pBackground ? StaticDefineIntRevLookup(UITextureModeEnum, pGen->pResult->pBackground->eType) : "None",
			"None",
			pFocusedGen ? (pFocusedGen->pchName ? pFocusedGen->pchName : "(no name)") : "None",
			pGen->chClone);
		ui_LabelSetText(pLabel, ach);
	}
	else
		ui_LabelSetText(pLabel, "No gen is highlighted. Run \"GenHighlight 1\" and hold down shift.");
}

static void GenParentButtonClicked(UIButton *button, void *unused)
{
	UIGen *pGen = ui_GenGetHighlighted();
	ui_GenSetHighlighted(pGen && pGen->pParent ? pGen->pParent : pGen);
}

static void GenCopyFilenameClicked(UIButton *button, void *unused)
{
	UIGen *pGen = ui_GenGetHighlighted();
	if(pGen && pGen->pchFilename)
	{
		char achResolved[CRYPTIC_MAX_PATH];
		fileLocateWrite(pGen->pchFilename, achResolved);
		winCopyToClipboard(achResolved);
	}
}

static UITab *GenGetHighlightInspectTab(void)
{
	UITab *pTab = ui_TabCreate("Highlighted");
	UILabel *pLabel = ui_LabelCreate("", 0, 0);
	UIList *pStateList = ui_ListCreate(NULL, NULL, 14);
	UIList *pStateDefList = ui_ListCreate(NULL, NULL, 14);
	UIList *pComplexStateDefList = ui_ListCreate(NULL, NULL, 14);
	UIList *pVarList = ui_ListCreate(NULL, NULL, 14);
	UIButton *pParentButton = ui_ButtonCreate("Parent", 0, 0, GenParentButtonClicked, NULL);
	UIButton *pFilenameButton = ui_ButtonCreate("Copy Filename", 0, 0, GenCopyFilenameClicked, NULL);
	ui_ListAppendColumn(pStateList, ui_ListColumnCreateText("States", GenStateListDraw, NULL));
	ui_ListColumnSetWidth(pStateList->eaColumns[0], true, 1.f);
	ui_ListAppendColumn(pStateDefList, ui_ListColumnCreateText("StateDefs", GenStateDefListDraw, NULL));
	ui_ListAppendColumn(pComplexStateDefList, ui_ListColumnCreateText("ComplexStateDefs", GenComplexStateDefListDraw, NULL));
	ui_ListColumnSetWidth(pStateList->eaColumns[0], true, 1.f);
	ui_ListColumnSetWidth(pStateDefList->eaColumns[0], true, 1.f);
	ui_ListColumnSetWidth(pComplexStateDefList->eaColumns[0], true, 1.f);
	UI_WIDGET(pLabel)->tickF = GenInspectHighlightLabelTick;
	UI_WIDGET(pStateList)->tickF = GenStateListTick;
	UI_WIDGET(pStateDefList)->tickF = GenStateDefListTick;
	UI_WIDGET(pComplexStateDefList)->tickF = GenComplexStateDefListTick;
	ui_WidgetSetFreeCallback(UI_WIDGET(pStateList), GenStateListFree);

	UI_WIDGET(pVarList)->tickF = GenVarListTick;
	ui_ListAppendColumn(pVarList, ui_ListColumnCreateText("Var Name", GenVarListDrawName, NULL));
	ui_ListColumnSetWidth(pVarList->eaColumns[0], true, 1.f);
	ui_ListAppendColumn(pVarList, ui_ListColumnCreateText("Int", GenVarListDrawInt, NULL));
	ui_ListColumnSetWidth(pVarList->eaColumns[1], true, 1.f);
	ui_ListAppendColumn(pVarList, ui_ListColumnCreateText("Float", GenVarListDrawFloat, NULL));
	ui_ListColumnSetWidth(pVarList->eaColumns[2], true, 1.f);
	ui_ListAppendColumn(pVarList, ui_ListColumnCreateText("String", GenVarListDrawString, NULL));
	ui_ListColumnSetWidth(pVarList->eaColumns[3], true, 1.f);

	ui_LabelSetWordWrap(pLabel, true);
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), UI_STEP, 0, 0.4f, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 0.6f, 0.5f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(pStateList), 0, 0, 0.0, 0.00f, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pStateList), 0.4f, 0.5f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(pStateDefList), 0, 0, 0.0, 0.5f, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pStateDefList), 0.4f, 0.25f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(pComplexStateDefList), 0, 0, 0.0, 0.75f, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pComplexStateDefList), 0.4f, 0.25f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(pVarList), UI_STEP, 0, 0.4f, 0.6f, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pVarList), 0.6f, 0.4f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(pParentButton), UI_STEP, 0, 0.4f, 0.51f, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pParentButton), 0.3f, 0.08f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(pFilenameButton), UI_STEP, 0, 0.7f, 0.51f, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pFilenameButton), 0.3f, 0.08f, UIUnitPercentage, UIUnitPercentage);

	ui_TabAddChild(pTab, pLabel);
	ui_TabAddChild(pTab, pStateDefList);
	ui_TabAddChild(pTab, pStateList);
	ui_TabAddChild(pTab, pComplexStateDefList);
	ui_TabAddChild(pTab, pVarList);
	ui_TabAddChild(pTab, pParentButton);
	ui_TabAddChild(pTab, pFilenameButton);
	return pTab;
}

static void GenDataViewerLabelTick(UILabel *pLabel, UI_PARENT_ARGS)
{
	UIGen *pGen = ui_GenGetHighlighted();
	ParseTable *pParseTable = NULL;
	if (!UI_GEN_READY(pGen))
		return;

	pParseTable = SAFE_MEMBER2(pGen, pCode, pListTable);

	if (pParseTable)
	{
		char ach[512];
		sprintf(ach, 
			"Name: %s\n"
			"Filename: %s\n" 
			"DataType: %s\n", 
			pGen->pchName, pGen->pchFilename, pParseTable->name);
		ui_LabelSetText(pLabel, ach);
	}
}

static void GenDataViewerTreeUpdate(UIGenDebugUpdateItem *pValues, UITree *pTree)
{
	UIGen *pGen = ui_GenGetHighlighted();
	ParseTable *pParseTable = SAFE_MEMBER2(pGen, pCode, pListTable);
	void*** peaList = pParseTable ? ui_GenGetManagedList(pGen, pParseTable) : NULL;
	ParseTable *pPointerTable = NULL;
	void *pPointer = pGen ? ui_GenGetPointer(pGen, NULL, &pPointerTable) : NULL;
	S32 i, iSize = 0;

	if (UI_GEN_READY(pGen) && pPointerTable)
		GenDebugStructTree_AddStruct(&pTree->root, iSize++, "GenData", -1, pPointer, pPointerTable);

	if (UI_GEN_READY(pGen) && peaList)
	{
		const char *pchTypeName = StaticDefineIntRevLookup(UIGenTypeEnum, pGen->eType);
		switch (pGen->eType)
		{
		xcase kUIGenTypeList: pchTypeName = "RowData";
		xcase kUIGenTypeLayoutBox: pchTypeName = "GenInstanceData";
		xcase kUIGenTypeTabGroup: pchTypeName = "TabData";
		}
		iSize = GenDebugStructTree_AddListStructs(&pTree->root, iSize, pchTypeName, 0, peaList, pParseTable);
	}

	ui_TreeNodeTruncateChildren(&pTree->root, iSize);

	// Tick the children
	for (i = 0; i < eaSize(&pTree->root.children); i++)
		GenDebugStructTree_Tick(pTree->root.children[i]);
}

static void GenDataViewerTreeTick(UITree *pTree, UI_PARENT_ARGS)
{
	UIGenDebugUpdateItem *pValues = GenQueueDebugUpdate(pTree, GenDataViewerTreeUpdate);
	ui_TreeTick(pTree, UI_PARENT_VALUES);
}

static void GenDataViewerTreeFill(UITreeNode *pNode, UserData fillData)
{
	// This is handled in GenDataViewerTreeUpdate()
}

static UITab *GenGetDataViewInspectTab()
{
	UITab *pTab = ui_TabCreate("Data View");
	UILabel *pLabel = ui_LabelCreate("", 0, 0);
	UITree *pTree = ui_TreeCreate(0, 70, 500, 50);

	ui_LabelSetWordWrap(pLabel, true);
	UI_WIDGET(pTree)->name = "DataTree";
	UI_WIDGET(pTree)->tickF = GenDataViewerTreeTick;
	UI_WIDGET(pLabel)->tickF = GenDataViewerLabelTick;

	ui_TreeNodeSetFillCallback(&pTree->root, GenDataViewerTreeFill, NULL);
	ui_TreeNodeExpand(&pTree->root);
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), UI_STEP, 0, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 1.f, 70, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx(UI_WIDGET(pTree), 0, 0, 0, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pTree), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);

	ui_TabAddChild(pTab, pLabel);
	ui_TabAddChild(pTab, pTree);

	return pTab;
}

static void GenResultViewerLabelTick(UILabel *pLabel, UI_PARENT_ARGS)
{
	UIGen *pGen = ui_GenGetHighlighted();
	char ach[512];

	if (!UI_GEN_READY(pGen))
		return;

	sprintf(ach, 
		"Name: %s\n"
		"Filename: %s\n" 
		"Type: %s\n", 
		pGen->pchName, pGen->pchFilename, StaticDefineIntRevLookup(UIGenTypeEnum, pGen->eType));
	ui_LabelSetText(pLabel, ach);
}

static void GenResultViewerTreeUpdate(UIGenDebugUpdateItem *pValues, UITree *pTree)
{
	UIGen *pGen = ui_GenGetHighlighted();
	S32 i, iSize = 0;

	if (UI_GEN_READY(pGen))
	{
		UIGenInternal FakeInternal = {pGen->eType};
		UIGenPerTypeState FakeState = {pGen->eType};
		ParseTable *pResultTable = PolyStructDetermineParseTable(polyTable_UIGenInternal, &FakeInternal);
		ParseTable *pStateTable = PolyStructDetermineParseTable(polyTable_UIGenPerTypeState, &FakeState);
		ParseTable *pCodeOverrideTable = pGen->pCodeOverrideEarly ? PolyStructDetermineParseTable(polyTable_UIGenInternal, pGen->pCodeOverrideEarly) : NULL;

		if (pResultTable)
			GenDebugStructTree_AddStruct(&pTree->root, iSize++, "Result", -1, pGen->pResult, pResultTable);
		if (pStateTable)
			GenDebugStructTree_AddStruct(&pTree->root, iSize++, "State", -1, pGen->pState, pStateTable);
		if (pCodeOverrideTable)
			GenDebugStructTree_AddStruct(&pTree->root, iSize++, "CodeOverrideEarly", -1, pGen->pCodeOverrideEarly, pCodeOverrideTable);
	}

	ui_TreeNodeTruncateChildren(&pTree->root, iSize);

	// Tick the children
	for (i = 0; i < eaSize(&pTree->root.children); i++)
		GenDebugStructTree_Tick(pTree->root.children[i]);
}

static void GenResultViewerTreeTick(UITree *pTree, UI_PARENT_ARGS)
{
	UIGenDebugUpdateItem *pValues = GenQueueDebugUpdate(pTree, GenResultViewerTreeUpdate);
	ui_TreeTick(pTree, UI_PARENT_VALUES);
}

static void GenResultViewerTreeFill(UITreeNode *pNode, UserData fillData)
{
	// This is handled in GenResultViewerTreeUpdate()
}

static UITab *GenGetResultViewInspectTab()
{
	UITab *pTab = ui_TabCreate("Gen View");
	UILabel *pLabel = ui_LabelCreate("", 0, 0);
	UITree *pTree = ui_TreeCreate(0, 70, 500, 50);

	ui_LabelSetWordWrap(pLabel, true);
	UI_WIDGET(pTree)->name = "DataTree";
	UI_WIDGET(pTree)->tickF = GenResultViewerTreeTick;
	UI_WIDGET(pLabel)->tickF = GenResultViewerLabelTick;

	ui_TreeNodeSetFillCallback(&pTree->root, GenResultViewerTreeFill, NULL);
	ui_TreeNodeExpand(&pTree->root);
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), UI_STEP, 0, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 1.f, 70, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx(UI_WIDGET(pTree), 0, 0, 0, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pTree), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);

	ui_TabAddChild(pTab, pLabel);
	ui_TabAddChild(pTab, pTree);

	return pTab;
}

static void GenContextViewerLabelTick(UILabel *pLabel, UI_PARENT_ARGS)
{
	UIGen *pGen = ui_GenGetHighlighted();
	char ach[512];

	if (!UI_GEN_READY(pGen))
		return;

	sprintf(ach, 
		"Name: %s\n"
		"Filename: %s\n" 
		"Type: %s\n", 
		pGen->pchName, pGen->pchFilename, StaticDefineIntRevLookup(UIGenTypeEnum, pGen->eType));
	ui_LabelSetText(pLabel, ach);
}

static void GenContextViewerTreeUpdate(UIGenDebugUpdateItem *pValues, UITree *pTree)
{
	UIGen *pGen = ui_GenGetHighlighted();
	S32 i, iSize = 0;

	if (UI_GEN_READY(pGen))
	{
		ExprContext *pContext = ui_GenGetContext(pGen);
		if (pContext)
			iSize = GenDebugStructTree_AddListContextVars(&pTree->root, iSize, pContext, s_eaContextVars);
	}

	ui_TreeNodeTruncateChildren(&pTree->root, iSize);

	// Tick the children
	for (i = 0; i < eaSize(&pTree->root.children); i++)
		GenDebugStructTree_Tick(pTree->root.children[i]);
}

static void GenContextViewerTreeTick(UITree *pTree, UI_PARENT_ARGS)
{
	UIGenDebugUpdateItem *pValues = GenQueueDebugUpdate(pTree, GenContextViewerTreeUpdate);
	ui_TreeTick(pTree, UI_PARENT_VALUES);
}

static void GenContextViewerTreeFill(UITreeNode *pNode, UserData fillData)
{
	// This is handled in GenContextViewerTreeUpdate()
}

static UITab *GenGetContextViewInspectTab()
{
	UITab *pTab = ui_TabCreate("Gen Context");
	UILabel *pLabel = ui_LabelCreate("", 0, 0);
	UITree *pTree = ui_TreeCreate(0, 70, 500, 50);

	ui_LabelSetWordWrap(pLabel, true);
	UI_WIDGET(pTree)->name = "DataTree";
	UI_WIDGET(pTree)->tickF = GenContextViewerTreeTick;
	UI_WIDGET(pLabel)->tickF = GenContextViewerLabelTick;

	ui_TreeNodeSetFillCallback(&pTree->root, GenContextViewerTreeFill, NULL);
	ui_TreeNodeExpand(&pTree->root);
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), UI_STEP, 0, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 1.f, 70, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx(UI_WIDGET(pTree), 0, 0, 0, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pTree), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);

	ui_TabAddChild(pTab, pLabel);
	ui_TabAddChild(pTab, pTree);

	return pTab;
}

typedef enum {
	kHierarchyResult = 1,
	kHierarchyGenArray = 2,
	kHierarchyGenPointer = 4,
	kHierarchyPlaceholder = 8,
	kHierarchyParentArray = 16,
} HierarchyFlag;

typedef struct {
	const char *pchParseName;
	const char *pchDisplayName;
	HierarchyFlag eFlags;
} HierarcyInfo;

HierarcyInfo s_HierarcyFields[] = {
	{ NULL,                 NULL,                   kHierarchyPlaceholder | kHierarchyGenArray },
	{ NULL,                 "{Value}",              kHierarchyPlaceholder | kHierarchyGenPointer | kHierarchyParentArray },
	{ "FlatBorrowFrom",     "BorrowFroms",          kHierarchyGenArray },
	{ "Child",              "Children",             kHierarchyResult | kHierarchyGenArray },
	{ "InlineChild",        "InlineChildren",       kHierarchyResult | kHierarchyGenArray },
	{ "TemplateChild",      "TemplateChildren",     kHierarchyResult | kHierarchyGenArray },
	{ "Assembly",           "Assembly: {Value}",    kHierarchyResult },
	{ "Text",               "Text: {Value}",        kHierarchyResult },
	{ "Bar",                "Bar: {Value}",         kHierarchyResult },
	{ "Template",           "Template: {Value}",    kHierarchyResult | kHierarchyGenPointer },
	{ "RowTemplate",        "Template: {Value}",    kHierarchyResult | kHierarchyGenPointer },
	{ "CellTemplate",       "Template: {Value}",    kHierarchyResult | kHierarchyGenPointer },
	{ "TabTemplate",        "Template: {Value}",    kHierarchyResult | kHierarchyGenPointer },
};

static bool GenHierarchyTreeNode_GetParseInfo(UIGen *pGen, S32 iValue, ParseTable **ppTable, S32 *piParseIndex, void **ppPointer)
{
	S32 iSubType = iValue % 256;

	if (iSubType >= 0 && iSubType < ARRAY_SIZE(s_HierarcyFields))
	{
		HierarcyInfo *pInfo = &s_HierarcyFields[iSubType];
		ParseTable *pTable = parse_UIGen;
		void *pPointer = pGen;
		S32 i = 0;

		if (pInfo->eFlags & kHierarchyResult)
		{
			pTable = ui_GenGetType(pGen);
			pPointer = pGen->pResult;
		}

		if (!pInfo->pchParseName)
		{
			if (ppTable)
				*ppTable = pTable;
			if (piParseIndex)
				*piParseIndex = i;
			if (ppPointer)
				*ppPointer = pPointer;
			return true;
		}

		for (i = 0; pTable[i].name; i++)
		{
			if (!stricmp(pTable[i].name, pInfo->pchParseName))
			{
				if (ppTable)
					*ppTable = pTable;
				if (piParseIndex)
					*piParseIndex = i;
				if (ppPointer)
					*ppPointer = pPointer;
				return true;
			}
		}
	}

	return false;
}

static void GenHierarchyTreeNode_Fill(UITreeNode *pNode, UserData fillData);
static void GenHierarchyTreeNode_Draw(UITreeNode *pNode, UserData displayData, UI_MY_ARGS, F32 z);

static UITreeNode *GenHierarchyTreeNode_AddChild(UITreeNode *pParent, S32 iIndex, UIGen *pGen, S32 iDisplayIndex, S32 iType)
{
	UITreeNode *pNode = eaGet(&pParent->children, iIndex);

	if (!pNode)
	{
		while (eaSize(&pParent->children) <= iIndex)
		{
			pNode = ui_TreeNodeCreate(pParent->tree, 0, NULL, NULL, GenHierarchyTreeNode_Fill, NULL, GenHierarchyTreeNode_Draw, NULL, 20);
			ui_TreeNodeAddChild(pParent, pNode);
		}
	}

	pNode->contents = pGen;
	pNode->fillF = s_HierarcyFields[iType % 256].eFlags & (kHierarchyGenPointer | kHierarchyGenArray) ? GenHierarchyTreeNode_Fill : NULL;
	pNode->fillData = NULL;
	pNode->displayF = GenHierarchyTreeNode_Draw;
	pNode->displayData = NULL;
	pNode->crc = (iDisplayIndex * 256) + iType;
	return pNode;
}

static void GenHierarchyTreeNode_Fill(UITreeNode *pNode, UserData fillData)
{
	UIGen *pGen = pNode->contents;
	S32 iNodeIndex = pNode->crc / 256;
	S32 iNodeType = pNode->crc % 256;
	HierarcyInfo *pInfo = iNodeType >= 0 && iNodeType < ARRAY_SIZE(s_HierarcyFields) ? &s_HierarcyFields[iNodeType] : NULL;
	ParseTable *pTable;
	void *pPointer;
	S32 iParseIndex, iCount = 0;

	if (!pNode->open || !pInfo || (pInfo->eFlags & (kHierarchyGenPointer | kHierarchyGenArray)) == 0)
	{
		// No recursion
		ui_TreeNodeTruncateChildren(pNode, 0);
		return;
	}

	if (!GenHierarchyTreeNode_GetParseInfo(pGen, iNodeType, &pTable, &iParseIndex, &pPointer))
	{
		// Abort recursion
		ui_TreeNodeTruncateChildren(pNode, 0);
		return;
	}

	if (pInfo->eFlags & kHierarchyGenArray)
	{
		void *pDataIndirect = pPointer ? (char *)pPointer + pTable[iParseIndex].storeoffset : NULL;
		S32 i, iLength = GenDebugStructTree_GetArrayLength(pDataIndirect, pTable, iParseIndex);
		S32 iSubType = 0;

		// Make sure this code can handle the type
		if (TOK_GET_TYPE(pTable[iParseIndex].type) != TOK_STRUCT_X)
			iLength = 0;

		// Find the placeholder GenPointer that is indexed by array
		while (iSubType < ARRAY_SIZE(s_HierarcyFields))
		{
			if ((s_HierarcyFields[iSubType].eFlags & kHierarchyPlaceholder)
				&& (s_HierarcyFields[iSubType].eFlags & kHierarchyGenPointer)
				&& (s_HierarcyFields[iSubType].eFlags & kHierarchyParentArray))
			{
				break;
			}
		}

		// Expand array
		if (iSubType < ARRAY_SIZE(s_HierarcyFields))
		{
			for (i = 0; i < iLength && i < 0x7FFFFF; i++)
				GenHierarchyTreeNode_AddChild(pNode, iCount++, pGen, i, iSubType);
		}
	}

	if (pInfo->eFlags & kHierarchyGenPointer)
	{
		UIGen *pSubGen = pGen;
		S32 i, iFieldCount = ARRAY_SIZE(s_HierarcyFields);

		if (pInfo->eFlags & kHierarchyParentArray)
		{
			// pGen is actually the parent gen, and iNodeIndex is the index into an array
			void *pDataIndirect = pPointer ? (char *)pPointer + pTable[iParseIndex].storeoffset : NULL;
			void *pData;
			S32 iDisplayIndex;

			if (GenDebugStructTree_DereferenceArrayIndex(iNodeIndex, &pData, &iDisplayIndex, pDataIndirect, pTable, iParseIndex)
				&& pData)
			{
				if (pTable[iParseIndex].subtable == parse_UIGen)
					pSubGen = pData;
				else if (pTable[iParseIndex].subtable == parse_UIGenChild)
					pSubGen = GET_REF(((UIGenChild *)pData)->hChild);
				else if (pTable[iParseIndex].subtable == parse_UIGenBorrowed)
					pSubGen = GET_REF(((UIGenBorrowed *)pData)->hGen);
				else
					iFieldCount = 0;
			}
			else
				iFieldCount = 0;
		}

		// Expand fields
		for (i = 0; i < iFieldCount; i++)
		{
			ParseTable *pSubTable;
			void *pSubPointer;
			S32 iSubIndex;

			if (s_HierarcyFields[i].eFlags & kHierarchyPlaceholder)
				continue;

			if (!GenHierarchyTreeNode_GetParseInfo(pSubGen, i, &pSubTable, &iSubIndex, &pSubPointer))
				continue;

			GenHierarchyTreeNode_AddChild(pNode, iCount++, pSubGen, 0, i);
		}
	}

	ui_TreeNodeTruncateChildren(pNode, iCount);
}

static void GenHierarchyTreeNode_Draw(UITreeNode *pNode, UserData displayData, UI_MY_ARGS, F32 z)
{
	static char *s_estrLabel;
	UIGen *pGen = pNode->contents;
	S32 iNodeIndex = pNode->crc / 256;
	S32 iNodeType = pNode->crc % 256;
	HierarcyInfo *pInfo = iNodeType >= 0 && iNodeType < ARRAY_SIZE(s_HierarcyFields) ? &s_HierarcyFields[iNodeType] : NULL;
	ParseTable *pTable, *pSubTable = NULL, *pPolyTable = NULL;
	void *pDataIndirect, *pData;
	S32 iParseIndex, iDisplayIndex;

	const char *pchDisplayValue = NULL;
	bool bBadRef = false;

	if (!pInfo || !pInfo->pchDisplayName)
	{
		// Don't know how to draw
		return;
	}

	if (!GenHierarchyTreeNode_GetParseInfo(pGen, iNodeType, &pTable, &iParseIndex, &pDataIndirect))
	{
		// Unable to draw
		return;
	}

	// Get the parse type
	switch (TOK_GET_TYPE(pTable[iParseIndex].type))
	{
	xcase TOK_REFERENCE_X:
		pSubTable = pTable[iParseIndex].subtable ? resDictGetParseTable((const char *)pTable[iParseIndex].subtable) : NULL;
	xcase TOK_POLYMORPH_X:
		pPolyTable = pTable[iParseIndex].subtable;
	xcase TOK_STRUCT_X:
		pSubTable = pTable[iParseIndex].subtable;
	}

	// Get the pointer
	if (pTable[iParseIndex].type & (TOK_FIXED_ARRAY | TOK_EARRAY))
	{
		if (!GenDebugStructTree_DereferenceArrayIndex(iNodeIndex, &pData, &iDisplayIndex, pDataIndirect, pTable, iParseIndex))
		{
			pData = NULL;
			iDisplayIndex = -1;
		}
	}
	else if (TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_REFERENCE_X)
	{
		REF_TO(void) *handle = pDataIndirect ? *(void **)pDataIndirect : NULL;
		pData = handle ? GET_REF(*handle) : NULL;
	}
	else if ((pTable[iParseIndex].type & TOK_INDIRECT) != 0)
	{
		pData = pDataIndirect ? *(void **)pDataIndirect : NULL;
	}
	else
	{
		pData = pDataIndirect;
	}

	// Resolve poly type
	if (pPolyTable && pData)
	{
		S32 iCol;
		if (StructDeterminePolyType(pPolyTable, pData, &iCol))
			pSubTable = pPolyTable[iCol].subtable;
	}

	// Handle types
	if (TOK_GET_TYPE(pTable[iParseIndex].type) == TOK_REFERENCE_X)
	{
		REF_TO(UIGen) *handle = pDataIndirect;
		pchDisplayValue = handle ? REF_STRING_FROM_HANDLE(*handle) : NULL;
		bBadRef = handle && IS_HANDLE_ACTIVE(*handle) && !GET_REF(*handle);
	}
	else if (pSubTable == parse_UIGen)
	{
		UIGen *pSubStruct = pData;
		pchDisplayValue = pSubStruct ? pSubStruct->pchName : NULL;
		bBadRef = false;
	}
	else if (pSubTable == parse_UIGenChild)
	{
		UIGenChild *pSubStruct = pData;
		pchDisplayValue = pSubStruct ? REF_STRING_FROM_HANDLE(pSubStruct->hChild) : NULL;
		bBadRef = pSubStruct != NULL && IS_HANDLE_ACTIVE(pSubStruct->hChild) && !GET_REF(pSubStruct->hChild);
	}
	else if (pSubTable == parse_UIGenBorrowed)
	{
		UIGenBorrowed *pSubStruct = pData;
		pchDisplayValue = pSubStruct ? REF_STRING_FROM_HANDLE(pSubStruct->hGen) : NULL;
		bBadRef = pSubStruct != NULL && IS_HANDLE_ACTIVE(pSubStruct->hGen) && !GET_REF(pSubStruct->hGen);
	}
	else if (pSubTable == parse_UIGenTextureAssembly)
	{
		UIGenTextureAssembly *pSubStruct = pData;
		pchDisplayValue = pSubStruct ? REF_STRING_FROM_HANDLE(pSubStruct->hAssembly) : NULL;
		bBadRef = pSubStruct != NULL && IS_HANDLE_ACTIVE(pSubStruct->hAssembly) && !GET_REF(pSubStruct->hAssembly);
	}
	else if (pSubTable == parse_UIStyleBar)
	{
		UIStyleBar *pSubStruct = pData;
		pchDisplayValue = pSubStruct ? pSubStruct->pchName : NULL;
		bBadRef = false;
	}
	else if (pSubTable == parse_Message)
	{
		Message *pSubStruct = pData;
		pchDisplayValue = pSubStruct ? pSubStruct->pcMessageKey : NULL;
		bBadRef = false;
	}

	estrClear(&s_estrLabel);
	strfmt_FromArgs(&s_estrLabel, pInfo->pchDisplayName, STRFMT_STRING("Value", pchDisplayValue ? pchDisplayValue : "(null)"), STRFMT_END);
	if (bBadRef)
		estrAppend2(&s_estrLabel, " (missing)");

	gfxfont_Print(x, y + h/2, z, scale, scale, CENTER_Y, s_estrLabel);
}

static void GenHierarchyTreeNode_Activate(UITreeNode *pNode, UserData activateData)
{
	//
}

static void GenHierarchyGenNameDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z);
static void GenHierarchyAssemblyDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z);
static void GenHierarchyBarDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z);
static void GenHierarchyTemplateDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z);
static void GenHierarchyBundleTextDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z);
static void GenHierarchySMFDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z);
static void GenHierarchyGenTreeNodeTick(UITreeNode *pParent, UIGen ***peaParentStack, bool bForceOpen);
static void GenHierarchySelect(UITree *pTree, UserData unused)
{
	UITreeNode *pSelectedNode = ui_TreeGetSelected(pTree);
	UIGen *pGen = pSelectedNode ? (UIGen*)pSelectedNode->contents : NULL;
	if (UI_GEN_READY(pGen))
	{
		ui_GenSetHighlighted(pGen);
	}
}

#define UI_GEN_DEBUG_GET_BUNDLETEXT(type) \
	if (pResult && UI_GEN_IS_TYPE(pGen, kUIGenType##type##)) pText = &((UIGen##type##*)pResult)->TextBundle

static void GenHierarchyActivate(UITree *pTree, UserData unused)
{
	UITreeNode *pSelectedNode = ui_TreeGetSelected(pTree);
	if (pSelectedNode && pSelectedNode->contents)
	{
		UIGen *pGen = (UIGen*)pSelectedNode->contents;
		char achResolved[CRYPTIC_MAX_PATH];
		const char *pchFileName = NULL;
		// Cheap hack to figure out what type of row we're looking at
		if (pSelectedNode->displayF == GenHierarchyGenNameDispFunc
			|| pSelectedNode->displayF == GenHierarchyTemplateDispFunc)
		{
			pchFileName = pGen->pchFilename;
		}
		else if (pSelectedNode->displayF == GenHierarchyAssemblyDispFunc)
		{
			UIGenTextureAssembly *pGenTexAs = SAFE_MEMBER2(pGen, pResult, pAssembly);
			UITextureAssembly *pTexAs = ui_GenTextureAssemblyGetAssembly(pGen, pGenTexAs);
			pchFileName = pTexAs ? pTexAs->pchFilename : NULL;
		}
		else if (pSelectedNode->displayF == GenHierarchyBarDispFunc)
		{
			UIGenSlider *pSlider = UI_GEN_RESULT(pGen, Slider);
			UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
			UIStyleBar *pBar = GET_REF(pState->hStyleBar);
			pchFileName = pBar ? pBar->pchFilename : NULL;
		}
		else if (pSelectedNode->displayF == GenHierarchyBundleTextDispFunc)
		{
			UIGenInternal *pResult = pGen->pResult;
			UIGenBundleText *pText = NULL;
			Message *pMessage;
			UI_GEN_DEBUG_GET_BUNDLETEXT(Text);
			UI_GEN_DEBUG_GET_BUNDLETEXT(TextArea);
			UI_GEN_DEBUG_GET_BUNDLETEXT(TextEntry);
			UI_GEN_DEBUG_GET_BUNDLETEXT(Button);

			if (pText)
			{
				if (pMessage = GET_REF(pText->hText))
					pchFileName = pMessage->pcFilename;
				else if (pText->pTextExpr)
					pchFileName = pGen->pchFilename;
			}
		}
		else if (pSelectedNode->displayF == GenHierarchySMFDispFunc)
		{
			UIGenSMF *pSMF = UI_GEN_RESULT(pGen, SMF);
			Message *pMessage;
			if (pSMF)
			{
				if (pMessage = GET_REF(pSMF->hText))
					pchFileName = pMessage->pcFilename;
				else if (pSMF->pTextExpr)
					pchFileName = pGen->pchFilename;
			}
		}

		if (pchFileName)
		{
			fileLocateWrite(pchFileName, achResolved);
			fileOpenWithEditor(achResolved);
		}
	}
}

static void GenHierarchyGenNameDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z)
{
	UIGen *pGen = (UIGen*)node->contents;
	if (pGen && pGen->pchName)
	{
		bool bSelected = (node == ui_TreeGetSelected(node->tree));
		gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pGen->pchName);
	}
}

static void GenHierarchyAssemblyDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z)
{
	UIGen *pGen = (UIGen*)node->contents;
	UIGenTextureAssembly *pGenTexAs = SAFE_MEMBER2(pGen, pResult, pAssembly);
	UITextureAssembly *pTexAs = ui_GenTextureAssemblyGetAssembly(pGen, pGenTexAs);
	const char *pchTexAsName = pGenTexAs ? REF_STRING_FROM_HANDLE(pGenTexAs->hAssembly) : NULL;
	bool bSelected = (node == ui_TreeGetSelected(node->tree));
	gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);
	if (pchTexAsName && *pchTexAsName)
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "Assembly: %s%s", pchTexAsName, pTexAs ? "" : " (missing)");
	else
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "Assembly: %s", "[No Assembly]");
}

static void GenHierarchyBarDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z)
{
	UIGen *pGen = (UIGen*)node->contents;
	if (pGen)
	{
		UIGenSlider *pSlider = UI_GEN_RESULT(pGen, Slider);
		UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
		UIStyleBar *pBar = GET_REF(pState->hStyleBar);
		bool bSelected = (node == ui_TreeGetSelected(node->tree));
		gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "Bar: %s", pSlider->pInlineBar ? "[Inline UIStyleBar]" : (pBar ? pBar->pchName : "[No UIStyleBar]"));
	}
}

static void GenHierarchyTemplateDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z)
{
	UIGen *pGen = (UIGen*)node->contents;
	bool bSelected = (node == ui_TreeGetSelected(node->tree));
	gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "Template: %s", pGen->pchName);
}

static void GenHierarchyChildrenRowDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z)
{
	bool bSelected = (node == ui_TreeGetSelected(node->tree));
	gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "Children");
}

static void GenHierarchyInlineChildrenRowDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z)
{
	bool bSelected = (node == ui_TreeGetSelected(node->tree));
	gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "InlineChildren");
}

static void GenHierarchyBorrowFromRowDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z)
{
	bool bSelected = (node == ui_TreeGetSelected(node->tree));
	gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "BorrowFroms");
}

static void GenHierarchyTextDisplay(Message *pMessage, bool bTextExpr, UI_MY_ARGS, F32 z)
{
	if (pMessage)
	{
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "MessageKey: %s", pMessage->pcMessageKey);
	}
	else if (bTextExpr)
	{
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "MessageKey: N/A - Generated from expression");
	}
	else 
	{
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "MessageKey: [No Text]");
	}
}

static void GenHierarchyBundleTextDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z)
{
	UIGen *pGen = (UIGen*)node->contents;
	UIGenInternal *pResult = pGen->pResult;
	UIGenBundleText *pText = NULL;
	Message *pMessage = NULL;
	bool bSelected = (node == ui_TreeGetSelected(node->tree));
	UI_GEN_DEBUG_GET_BUNDLETEXT(Text);
	UI_GEN_DEBUG_GET_BUNDLETEXT(TextArea);
	UI_GEN_DEBUG_GET_BUNDLETEXT(TextEntry);
	UI_GEN_DEBUG_GET_BUNDLETEXT(Button);
	devassertmsg(pText, "BundleText should have been found by now. Are you trying to display text on the wrong gen?");

	gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);
	if(pText)
		GenHierarchyTextDisplay(GET_REF(pText->hText), !!pText->pTextExpr, UI_MY_VALUES, z);
}
#undef UI_GEN_DEBUG_GET_BUNDLETEXT

static void GenHierarchySMFDispFunc(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z)
{
	UIGen *pGen = (UIGen*)node->contents;
	UIGenSMF *pSMF = UI_GEN_RESULT(pGen, SMF);
	bool bSelected = (node == ui_TreeGetSelected(node->tree));
	gfxfont_SetColorRGBA(bSelected ? 0xFFFFFFFF : 0xFF, bSelected ? 0xFFFFFFFF : 0xFF);
	if(pSMF)
		GenHierarchyTextDisplay(GET_REF(pSMF->hText), !!pSMF->pTextExpr, UI_MY_VALUES, z);
}

static void GenHierarchyDummyFillFunc(UITreeNode *pParent, UserData unused)
{
	/* This space intentionally left blank */ 
	// Since gens can reflow and shift around at any time, the 
	// filling is done in the tick function below, which will adjust
	// the tree on the fly. 
}

static void GenHierarchyBorrowFillFunc(UITreeNode *pParent, UserData unused)
{ 
	UIGen *pGen = (UIGen*)pParent->contents;
	int i;
	for (i = 0; i < eaSize(&pGen->eaBorrowTree); i++)
	{
		UIGenBorrowed *pBorrowed = pGen->eaBorrowTree[i];
		UIGen *pBorrowedGen = pBorrowed ? GET_REF(pBorrowed->hGen) : NULL;
		UITreeNode *pBorrowFromNode = ui_TreeNodeCreate(pParent->tree, 0, parse_UIGen, pBorrowedGen, eaSize(&pBorrowedGen->eaBorrowTree) ? GenHierarchyBorrowFillFunc : NULL, NULL, GenHierarchyGenNameDispFunc, NULL, 20);
		ui_TreeNodeAddChild(pParent, pBorrowFromNode);
	}
}

static void GenHierarchyPruneNodes(UITreeNode *pParent, UIGen *pGen, void** eaList)
{
	int size;
	if (!UI_GEN_READY(pGen) || !eaList || !pParent->open)
		size = 0;
	else 
		size = eaSize(&eaList);

	while (eaSize(&pParent->children) > size)
	{
		UITreeNode *pTemp = eaTail(&pParent->children);
		if (pTemp)
			ui_TreeNodeRemoveChild(pParent, pTemp);
	}
}

void GenHierarchyGenTreeNodeTick_Borrows(UITreeNode *pParent, UIGen *pGen, int iRowIndex)
{
	// Borrows don't change, so they don't need to be updated recursively like children do. 
	UITreeNode *pBorrowFromNode = NULL;
	if ((pBorrowFromNode = eaGet(&pParent->children, iRowIndex)) == NULL)
	{
		pBorrowFromNode = ui_TreeNodeCreate(pParent->tree, 0, parse_UIGen, pGen, GenHierarchyBorrowFillFunc, NULL, GenHierarchyBorrowFromRowDispFunc, NULL, 20);
		ui_TreeNodeAddChild(pParent, pBorrowFromNode);
	}
}


void GenHierarchyGenTreeNodeTick_Children(UITreeNode *pParent, UIGen *pGen, UIGen ***peaParentStack, bool bForceOpen, int iRowIndex)
{
	UITreeNode *pChildNode = NULL;

	if ((pChildNode = eaGet(&pParent->children, iRowIndex++)) == NULL)
	{
		pChildNode = ui_TreeNodeCreate(pParent->tree, 0, parse_UIGen, pGen, GenHierarchyDummyFillFunc, NULL, GenHierarchyChildrenRowDispFunc, NULL, 20);
		ui_TreeNodeAddChild(pParent, pChildNode);
	}
	
	pChildNode->open |= bForceOpen;

	if (pChildNode->open)
	{
		int i;
		// Update current nodes
		/*for (i = 0; i < eaSize(&pChildNode->children) && i < eaSize(&pGen->pResult->eaChildren); i++)
		{
			UITreeNode *pNode = pChildNode->children[i];
			GenHierarchyGenTreeNodeTick(pChildNode->children[i], peaParentStack, false);
		}*/

		// Add new nodes if necessary
		for  (i = eaSize(&pChildNode->children) ; i < eaSize(&pGen->pResult->eaChildren); i++)
		{
			UIGenChild *pChild = pGen->pResult->eaChildren[i];
			UIGen *pChildGen = pChild ? GET_REF(pChild->hChild) : NULL;
			if (UI_GEN_READY(pChildGen))
			{
				UITreeNode *pNode = ui_TreeNodeCreate(pChildNode->tree, 0, parse_UIGen, pChildGen, GenHierarchyDummyFillFunc, NULL, GenHierarchyGenNameDispFunc, NULL, 20);
				ui_TreeNodeAddChild(pChildNode, pNode);
				
				
			}
		}

		if(bForceOpen)
		{
			for(i=0;i<eaSize(&pChildNode->children);i++)
			{
				UIGenChild *pChild = pGen->pResult->eaChildren[i];
				UIGen *pChildGen = pChild ? GET_REF(pChild->hChild) : NULL;
				UITreeNode *pNode = pChildNode->children[i];

				if (pChildGen == eaTail(peaParentStack))
				{
					bool bFoundGen;
					eaPop(peaParentStack);
					bFoundGen = (eaSize(peaParentStack) == 0);
					pNode->open = true;
					GenHierarchyGenTreeNodeTick(pNode, peaParentStack, !bFoundGen);
					if (bFoundGen)
					{
						pNode->tree->selected = pNode;
						pNode->tree->scrollToSelected = true;
					}
				}
				else
				{
					GenHierarchyGenTreeNodeTick(pNode, peaParentStack, false);
				}
			}
		}
		
	}
	GenHierarchyPruneNodes(pChildNode, pGen, SAFE_MEMBER2(pGen, pResult, eaChildren));
}

void GenHierarchyGenTreeNodeTick_InlineChildren(UITreeNode *pParent, UIGen *pGen, UIGen ***peaParentStack, bool bForceOpen, int iRowIndex)
{
	UITreeNode *pInlineChildNode = NULL;

	if ((pInlineChildNode = eaGet(&pParent->children, iRowIndex++)) == NULL)
	{
		pInlineChildNode = ui_TreeNodeCreate(pParent->tree, 0, parse_UIGen, pGen, GenHierarchyDummyFillFunc, NULL, GenHierarchyInlineChildrenRowDispFunc, NULL, 20);
		ui_TreeNodeAddChild(pParent, pInlineChildNode);
	}

	pInlineChildNode->open |= bForceOpen;

	if (pInlineChildNode->open)
	{
		int i;
		// Update current nodes
		/*
		for (i = 0; i < eaSize(&pInlineChildNode->children) && i < eaSize(&pGen->pResult->eaInlineChildren); i++)
		{
			UITreeNode *pNode = pInlineChildNode->children[i];
			GenHierarchyGenTreeNodeTick(pNode, peaParentStack, pNode->contents == eaTail(peaParentStack));
		}*/

		// Add new nodes if necessary
		for  (i=eaSize(&pInlineChildNode->children) ; i < eaSize(&pGen->pResult->eaInlineChildren); i++)
		{
			if (UI_GEN_READY(pGen->pResult->eaInlineChildren[i]))
			{
				UITreeNode *pNode = ui_TreeNodeCreate(pInlineChildNode->tree, 0, parse_UIGen, pGen->pResult->eaInlineChildren[i], GenHierarchyDummyFillFunc, NULL, GenHierarchyGenNameDispFunc, NULL, 20);
				ui_TreeNodeAddChild(pInlineChildNode, pNode);
			}
		}

		for(i=0;i<eaSize(&pInlineChildNode->children);i++)
		{
			UITreeNode *pNode = pInlineChildNode->children[i];

			if (pGen->pResult->eaInlineChildren[i] == eaTail(peaParentStack))
			{
				bool bFoundGen;
				eaPop(peaParentStack);
				bFoundGen = (eaSize(peaParentStack) == 0);
				pNode->open = true;
				GenHierarchyGenTreeNodeTick(pNode, peaParentStack, !bFoundGen);
				if (bFoundGen)
				{
					pNode->tree->selected = pNode;
					pNode->tree->scrollToSelected = true;
				}
			}
			else
			{
				GenHierarchyGenTreeNodeTick(pNode, peaParentStack, false);
			}
		}
	}
	GenHierarchyPruneNodes(pInlineChildNode, pGen, SAFE_MEMBER2(pGen, pResult, eaInlineChildren));
}


GenHierarchyGenTreeNodeTick_Other(UITreeNode *pParent, UIGen *pGen, int iRowIndex, UITreeDisplayFunc func, bool bShowBorrows, bool bForceOpen)
{
	UITreeNode *pNode = NULL;
	if ((pNode = eaGet(&pParent->children, iRowIndex)) == NULL)
	{
		pNode = ui_TreeNodeCreate(pParent->tree, 0, parse_UIGen, pGen, bShowBorrows ? GenHierarchyBorrowFillFunc : NULL, NULL, func, NULL, 20);
		ui_TreeNodeAddChild(pParent, pNode);
	}

	if(bForceOpen)
	{
		pNode->tree->selected = pNode;
		pNode->tree->scrollToSelected = true;
	}
}

void GenHierarchyGenTreeNodeTick_BundleText(UITreeNode *pParent, UIGen *pGen, int iRowIndex)
{
	UITreeNode *pBundleTextNode = NULL;
	if ((pBundleTextNode = eaGet(&pParent->children, iRowIndex)) == NULL)
	{
		pBundleTextNode = ui_TreeNodeCreate(pParent->tree, 0, parse_UIGen, pGen, NULL, NULL, GenHierarchyBundleTextDispFunc, NULL, 20);
		ui_TreeNodeAddChild(pParent, pBundleTextNode);
	}
}

void GenHierarchyGenTreeNodeTick_SMF(UITreeNode *pParent, UIGen *pGen, int iRowIndex)
{
	UITreeNode *pBundleTextNode = NULL;
	if ((pBundleTextNode = eaGet(&pParent->children, iRowIndex)) == NULL)
	{
		pBundleTextNode = ui_TreeNodeCreate(pParent->tree, 0, parse_UIGen, pGen, NULL, NULL, GenHierarchySMFDispFunc, NULL, 20);
		ui_TreeNodeAddChild(pParent, pBundleTextNode);
	}
}

// This is all done in a tick function in case the stuff changes on the fly
// which would result in dangling pointers and other bad things if it were 
// event driven
static void GenHierarchyGenTreeNodeTick(UITreeNode *pParent, UIGen ***peaParentStack, bool bForceOpen)
{
	UIGen *pGen = (UIGen*)pParent->contents;

	if (UI_GEN_READY(pGen) && pParent->open)
	{
		int iRowIndex = 0;
		if (eaSize(&pGen->eaBorrowTree))
		{
			GenHierarchyGenTreeNodeTick_Borrows(pParent, pGen, iRowIndex++);
		}
		if (eaSize(&pGen->pResult->eaChildren))
		{
			GenHierarchyGenTreeNodeTick_Children(pParent, pGen, peaParentStack, bForceOpen, iRowIndex++);
		}
		if (eaSize(&pGen->pResult->eaInlineChildren))
		{
			GenHierarchyGenTreeNodeTick_InlineChildren(pParent, pGen, peaParentStack, bForceOpen, iRowIndex++);
		}
		if (pGen->pResult->pAssembly)
		{
			GenHierarchyGenTreeNodeTick_Other(pParent, pGen, iRowIndex++, GenHierarchyAssemblyDispFunc, false, false);
		}
		// Anything that has a text bundle on it
		if (pGen->eType == kUIGenTypeText
			|| pGen->eType == kUIGenTypeTextArea
			|| pGen->eType == kUIGenTypeTextEntry
			|| pGen->eType == kUIGenTypeButton)
		{
			GenHierarchyGenTreeNodeTick_BundleText(pParent, pGen, iRowIndex++);
		}
		if (pGen->eType == kUIGenTypeSMF)
		{
			GenHierarchyGenTreeNodeTick_SMF(pParent, pGen, iRowIndex++);
		}
		if (pGen->eType == kUIGenTypeSlider)
		{
			GenHierarchyGenTreeNodeTick_Other(pParent, pGen, iRowIndex++, GenHierarchyBarDispFunc, false, false);
		}
		if (pGen->eType == kUIGenTypeList)
		{
			UIGenList *pList = UI_GEN_RESULT(pGen, List);
			UIGen *pTemplate = pList ? GET_REF(pList->hRowTemplate) : NULL;
			if (pTemplate)
				GenHierarchyGenTreeNodeTick_Other(pParent, pTemplate, iRowIndex++, GenHierarchyTemplateDispFunc, true, bForceOpen);
		}
		if (pGen->eType == kUIGenTypeLayoutBox)
		{
			UIGenLayoutBox *pLayout = UI_GEN_RESULT(pGen, LayoutBox);
			UIGen *pTemplate = pLayout ? GET_REF(pLayout->hTemplate) : NULL;
			if (pTemplate)
				GenHierarchyGenTreeNodeTick_Other(pParent, pTemplate, iRowIndex++, GenHierarchyTemplateDispFunc, true, bForceOpen);
		}
		if (pGen->eType == kUIGenTypeTabGroup)
		{
			UIGenTabGroup *pTabGroup = UI_GEN_RESULT(pGen, TabGroup);
			UIGen *pTemplate = pTabGroup ? GET_REF(pTabGroup->hTabTemplate) : NULL;
			if (pTemplate)
				GenHierarchyGenTreeNodeTick_Other(pParent, pTemplate, iRowIndex++, GenHierarchyTemplateDispFunc, true, bForceOpen);
		}

		// If this is not one of the above gen types, there should not be 
		// an extra row at the end, if there is remove it. 
		while (eaSize(&pParent->children) > iRowIndex)
		{
			UITreeNode *pTemp = eaTail(&pParent->children);
			if (pTemp)
				ui_TreeNodeRemoveChild(pParent, pTemp);
		}
	}
}

static void GenHierarchyTreeUpdate(UIGenDebugUpdateItem *pValues, UITree *pTree)
{
	// Not necessarily the "Root" gen, just the root of this particular tree
	static UIGen *pHighlight = NULL;
	UIGen *pHighlightPrev = NULL;
	UIGen *pRoot = NULL;
	UIGen **eaParentStack = NULL;

	pHighlightPrev = pHighlight;
	pRoot = pHighlight = ui_GenGetHighlighted();

	while (pRoot && !pRoot->bIsRoot && !pRoot->pchJailCell)
	{
		if (pRoot && pHighlight != pHighlightPrev)
			eaPush(&eaParentStack, pRoot);
		pRoot = pRoot->pParent;
	}

	if (pRoot)
	{
		UITreeNode *pNode = NULL;
		if ((pNode = eaGet(&pTree->root.children, 0)) == NULL)
		{
			pNode = ui_TreeNodeCreate(pTree, 0, parse_UIGen, pRoot, GenHierarchyDummyFillFunc, NULL, GenHierarchyGenNameDispFunc, NULL, 20);
			ui_TreeNodeAddChild(&pTree->root, pNode);
			pNode->open = true;
		}
		pTree->root.contents = pRoot;

		if (pNode)
			GenHierarchyGenTreeNodeTick(pNode, &eaParentStack, pHighlight != pHighlightPrev);

	}
	else
	{
		UITreeNode *pNode = eaGet(&pTree->root.children, 0);
		if (pNode)
			ui_TreeNodeRemoveChild(&pTree->root, pNode);
	}

	eaClearFast(&eaParentStack);
}

static void GenHierarchyTreeTick(UITree *pTree, UI_PARENT_ARGS)
{
	UIGenDebugUpdateItem *pValues = GenQueueDebugUpdate(pTree, GenHierarchyTreeUpdate);
	ui_TreeTick(pTree, UI_PARENT_VALUES);
}

static void GenHierarchyLabelTick(UILabel *pLabel, UI_PARENT_ARGS)
{
	UIGen *pGen = ui_GenGetHighlighted();
	if (UI_GEN_READY(pGen))
	{
		char ach[512];
		sprintf(ach, 
			"Name: %s\n"
			"Filename: %s\n",
			pGen->pchName, pGen->pchFilename);
		ui_LabelSetText(pLabel, ach);
	}
}

UITab *GenGetHierarchyInspectTab()
{
	UITab *pTab = ui_TabCreate("Hierarchy");
	UILabel *pLabel = ui_LabelCreate("", 0, 0);
	UITree *pTree = ui_TreeCreate(0, 30, 500, 50);

	ui_TreeSetSelectedCallback(pTree, GenHierarchySelect, NULL);
	ui_TreeSetActivatedCallback(pTree, GenHierarchyActivate, NULL);

	ui_LabelSetWordWrap(pLabel, true);
	UI_WIDGET(pTree)->tickF = GenHierarchyTreeTick;
	UI_WIDGET(pLabel)->tickF = GenHierarchyLabelTick;

	//ui_TreeNodeSetFillCallback(&pTree->root, GenDataViewerRootFill, NULL); 
	ui_TreeNodeExpand(&pTree->root);
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), UI_STEP, 0, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 1.f, 50, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx(UI_WIDGET(pTree), 0, 0, 0, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pTree), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);

	ui_TabAddChild(pTab, pLabel);
	ui_TabAddChild(pTab, pTree);

	return pTab;
}

// Performance

static void GenPerformanceLabelTick(UILabel *pLabel, UI_PARENT_ARGS)
{
	char ach[2000];
	ui_GenPrintTimingResults(ach);
	ui_LabelSetText(pLabel, ach);
}

UITab *GenGetPerformanceInspectTab()
{
	UITab *pTab = ui_TabCreate("Performance");
	UILabel *pLabel = ui_LabelCreate("", 0, 0);
	ui_LabelSetWordWrap(pLabel, true);

	UI_WIDGET(pLabel)->tickF = GenPerformanceLabelTick;

	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), UI_STEP, 0, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 1.f, 50, UIUnitPercentage, UIUnitFixed);

	ui_TabAddChild(pTab, pLabel);

	return pTab;
}

// This updates the models/data pointers after the gen system runs it's update.
void ui_GenDebugUpdate(void)
{
	S32 i;
	for (i = 0; i < s_iDebugUpdateCount; i++)
	{
		UIGenDebugUpdateItem *pItem = s_eaDebugUpdateQueue[i];
		(pItem->cbUpdate)(pItem, pItem->pData);
	}
	s_iDebugUpdateCount = 0;
}

// Inspect the currently highlighted gen, and also show some global information.
AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Debug) ACMD_NAME(GenInspect, ui_GenInspect);
void ui_GenInspect(void)
{
	UITabGroup *pGroup = ui_TabGroupCreate(0, 0, 0, 0);
	UIWindow *pWindow = ui_WindowCreate("Inspect Gens", 0, 0, 520, 300);
	ui_WindowAddChild(pWindow, pGroup);
	ui_TabGroupAddTab(pGroup, GenGetGlobalInspectTab());
	ui_TabGroupAddTab(pGroup, GenGetHighlightInspectTab());
	ui_TabGroupAddTab(pGroup, GenGetResultViewInspectTab());
	ui_TabGroupAddTab(pGroup, GenGetDataViewInspectTab());
	ui_TabGroupAddTab(pGroup, GenGetContextViewInspectTab());
	ui_TabGroupAddTab(pGroup, GenGetHierarchyInspectTab());
	ui_TabGroupAddTab(pGroup, GenGetPerformanceInspectTab());
	ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowShow(pWindow);
	ui_WindowSetCloseCallback(pWindow, ui_WindowFreeOnClose, NULL);
	ui_WindowSetCycleBetweenDisplays(pWindow, true);
	s_iDebugUpdateCount = 0;
	globCmdParse("GenHighlight 1");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Debug) ACMD_NAME(GenHighlightParent, ui_GenHighlightParent);
void ui_GenHighlightParent()
{
	GenParentButtonClicked(NULL, NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Debug) ACMD_NAME(GenOpen, ui_GenOpen);
bool ui_GenOpen(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen)
{
	UIGen *pGen;
	if (pGen = RefSystem_ReferentFromString(g_GenState.hGenDict, pchGen))
	{
		char achResolved[CRYPTIC_MAX_PATH];
		fileLocateWrite(pGen->pchFilename, achResolved);
		fileOpenWithEditor(achResolved);
		return true;
	}
	return false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Debug) ACMD_NAME(MessageOpen);
bool ui_MessageOpen(ACMD_NAMELIST("Message", REFDICTIONARY) const char *pchMessage)
{
	Message *pMessage;
	if (pMessage = RefSystem_ReferentFromString(gMessageDict, pchMessage))
	{
		char achResolved[CRYPTIC_MAX_PATH];
		fileLocateWrite(pMessage->pcFilename, achResolved);
		fileOpenWithEditor(achResolved);
		return true;
	}
	return false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Debug) ACMD_NAME(GenOpenCSDUpdateCandidates, ui_GenOpenCSDUpdateCandidates);
int ui_GenOpenCSDUpdateCandidates()
{
	int iCount = 0;
	UIGen *pGen;
	RefDictIterator iter;
	RefSystem_InitRefDictIterator(g_GenState.hGenDict, &iter);
	while (pGen = RefSystem_GetNextReferentFromIterator(&iter))
	{
		int i;
		bool bFoundCSD = false;
		char achResolved[CRYPTIC_MAX_PATH];
		for (i = eaSize(&pGen->eaComplexStates)-1; i >= 0; --i)
		{
			UIGenComplexStateDef *pCSD = pGen->eaComplexStates[i];
			Expression *expr = pCSD->pCondition;
			if (expr)
			{
				int j;
				for(j = 0; j < beaSize(&expr->postfixEArray); j++)
				{
					MultiVal *val = &expr->postfixEArray[j];
					if(val->type==MULTIOP_FUNCTIONCALL)
					{
						char *estrFunctionCall = NULL;
						MultiValToEString(val, &estrFunctionCall);
						if (stricmp("InState", estrFunctionCall) != 0
							&& stricmp("GenInState", estrFunctionCall) != 0
							&& stricmp("GenIntVar", estrFunctionCall) != 0
							&& stricmp("GenFloatVar", estrFunctionCall) != 0
							&& stricmp("GenStringVar", estrFunctionCall) != 0
							&& stricmp("GenNameIntVar", estrFunctionCall) != 0
							&& stricmp("GenNameFloatVar", estrFunctionCall) != 0
							&& stricmp("GenNameStringVar", estrFunctionCall) != 0)
						{
							estrDestroy(&estrFunctionCall);
							goto next;
						}
						else
						{
							bFoundCSD = true;
						}
						estrDestroy(&estrFunctionCall);
					}
				}
			}
		}
		if (bFoundCSD)
		{
			iCount++;
			conPrintf("Candidate for CSD update: %s", pGen->pchName);
			fileLocateWrite(pGen->pchFilename, achResolved);
			fileOpenWithEditor(achResolved);
		}
		next: ;
	}
	return iCount;
};

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Debug) ACMD_NAME(ui_GenOpenCSDUpdateCandidate);
void ui_GenOpenCSDUpdateCandidate(int count)
{
	UIGen *pGen;
	RefDictIterator iter;
	RefSystem_InitRefDictIterator(g_GenState.hGenDict, &iter);
	while (pGen = RefSystem_GetNextReferentFromIterator(&iter))
	{
		int i;
		bool bFoundCSD = false;
		char achResolved[CRYPTIC_MAX_PATH];
		for (i = eaSize(&pGen->eaComplexStates)-1; i >= 0; --i)
		{
			UIGenComplexStateDef *pCSD = pGen->eaComplexStates[i];
			Expression *expr = pCSD->pCondition;
			if (expr)
			{
				int j;
				for(j = 0; j < beaSize(&expr->postfixEArray); j++)
				{
					MultiVal *val = &expr->postfixEArray[j];
					if(val->type==MULTIOP_FUNCTIONCALL)
					{
						char *estrFunctionCall = NULL;
						MultiValToEString(val, &estrFunctionCall);
						if (stricmp("InState", estrFunctionCall) != 0
							&& stricmp("GenInState", estrFunctionCall) != 0
							&& stricmp("GenIntVar", estrFunctionCall) != 0
							&& stricmp("GenFloatVar", estrFunctionCall) != 0
							&& stricmp("GenStringVar", estrFunctionCall) != 0
							&& stricmp("GenNameIntVar", estrFunctionCall) != 0
							&& stricmp("GenNameFloatVar", estrFunctionCall) != 0
							&& stricmp("GenNameStringVar", estrFunctionCall) != 0)
						{
							estrDestroy(&estrFunctionCall);
							goto next;
						}
						else
						{
							bFoundCSD = true;
						}
						estrDestroy(&estrFunctionCall);
					}
				}
			}
		}
		if (bFoundCSD && (--count == 0))
		{
			conPrintf("Candidate for CSD update: %s", pGen->pchName);
			fileLocateWrite(pGen->pchFilename, achResolved);
			fileOpenWithEditor(achResolved);
			return;
		}
		next: ;
	}
};

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Debug) ACMD_NAME(DumpComplexStateDefConditions);
void ui_DumpComplexStateDefConditions(const char *pchFilename)
{
	g_GenState.pchDumpCSDExpressions = allocAddString(pchFilename);
};

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Debug) ACMD_NAME(DumpExpressions);
void ui_DumpExpressions(const char *pchFilename)
{
	g_GenState.pchDumpExpressions = allocAddString(pchFilename);
};
