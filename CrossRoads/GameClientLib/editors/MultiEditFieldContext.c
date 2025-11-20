//
// MultiEditField.c
//
#include "MultiEditFieldContext.h"

#include "Color.h"
#include "GfxTexAtlas.h"
#include "GraphicsLib.h"
#include "Message.h"
#include "MultiEditField.h"
#include "ResourceInfo.h"
#include "StringCache.h"
#include "TokenStore.h"
#include "UIButton.h"
#include "UILabel.h"
#include "UISeparator.h"
#include "UISliderTextEntry.h"
#include "UISpinnerEntry.h"
#include "UISprite.h"
#include "stdtypes.h"
#include "textparser.h"

#include "AutoGen/MultiEditFieldContext_h_ast.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct MEFieldContextState
{
	MEFieldContext *pCurrentContext;
	MEFieldContext **ppContexts;
} MEFieldContextState;

MEFieldContextState g_MEFCS = {0};

static void MEContextAddSpriteInternal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, const char *pchTextureName, const char *pchToolTip);
static void MEContextAllocFree(MEFieldContextAlloc *pAlloc);

static void MEContextInit(MEFieldContext* pContext)
{
	if(pContext->id.pParent) {
		MEFieldContext* pParent = pContext->id.pParent;
		MEFieldContext sTemp = {0};
		bool bTempIsExternalContext = pContext->bIsExternalContext;
		memcpy(&sTemp.id, &pContext->id, sizeof(pContext->id));
		StructCopy(parse_MEFieldContext, pContext->id.pParent, pContext, 0, 0, 0);
		memcpy(&pContext->id, &sTemp.id, sizeof(pContext->id));
		pContext->bIsExternalContext = bTempIsExternalContext;
		pContext->id.pParent = pParent;
	} else {
		pContext->iXPos = MEFC_DEFAULT_X;
		pContext->iYPos = MEFC_DEFAULT_Y;
		pContext->iXLabelStart = 0;
		pContext->iYLabelStart = 0;
		pContext->iXDataStart = MEFC_DEFAULT_X_DATA_START;
		pContext->iYDataStart = 0;
		pContext->bLabelPaddingFromData = true;
		pContext->iXIndentStep = MEFC_DEFAULT_X_INDENT_STEP;
		pContext->iWidgetHeight = MEFC_DEFAULT_WIDGET_HEIGHT;
		pContext->iYStep = MEFC_DEFAULT_Y_STEP;
		pContext->iYSpacer = MEFC_DEFAULT_Y_SPACER;
		pContext->fDataWidgetWidth = MEFC_DEFAULT_DATA_WIDGET_WIDTH;
		pContext->iTextAreaHeight = MEFC_DEFAULT_TEXT_AREA_HEIGHT;

		pContext->iErrorIconSpaceWidth = MEFC_DEFAULT_ERROR_ICON_SPACE_WIDTH;
		pContext->bErrorIconOffsetFromRight = true;
		pContext->iActionButtonPad = MEFC_DEFAULT_ACTION_BUTTON_PAD;

		pContext->iLeftPad = MEFC_DEFAULT_LEFT_PAD;
		pContext->iRightPad = MEFC_DEFAULT_RIGHT_PAD;
		pContext->iTopPad = MEFC_DEFAULT_TOP_PAD;
		pContext->iBottomPad = MEFC_DEFAULT_BOTTOM_PAD;
	}
}

MEFieldContext* MEContextPushEA(const char *pchName, void **ppOldData, void **ppNewData, ParseTable *pTable)
{
	int i;
	const char *pchNamePooled = allocAddString(pchName);
	MEFieldContext ***pppChildren;
	MEFieldContext *pContext = NULL;
	
	if(!g_MEFCS.pCurrentContext) {
		pppChildren = &g_MEFCS.ppContexts;
	} else {
		pppChildren = &g_MEFCS.pCurrentContext->id.ppChildren;
	}

	for ( i=0; i < eaSize(pppChildren); i++ ) {
		if((*pppChildren)[i]->id.pchName == pchNamePooled) {
			pContext = (*pppChildren)[i];
		}
	}

	if(!pContext) {
		pContext = StructCreate(parse_MEFieldContext);
		pContext->id.pchName = pchNamePooled;
		pContext->id.pTable = pTable;
		pContext->id.pParent = g_MEFCS.pCurrentContext;
		eaPush(pppChildren, pContext);
	} else {
 		assert(pContext->id.pTable == pTable);
		pContext->id.pParent = g_MEFCS.pCurrentContext;
	}

	MEContextInit( pContext );
	assert(eaSize(&ppOldData) == eaSize(&ppNewData));
	eaCopy(&pContext->id.ppOldData, &ppOldData);
	eaCopy(&pContext->id.ppNewData, &ppNewData);
	
	g_MEFCS.pCurrentContext = pContext;
	return pContext;
}

void MEContextPushEAExternalContext(MEFieldContext* pContext, void **ppOldData, void **ppNewData, ParseTable *pTable)
{
	pContext->id.pTable = pTable;
	pContext->id.pParent = NULL;

	MEContextInit( pContext );
	assert(eaSize(&ppOldData) == eaSize(&ppNewData));
	eaCopy(&pContext->id.ppOldData, &ppOldData);
	eaCopy(&pContext->id.ppNewData, &ppNewData);
	pContext->id.pExternalContextPrevContext = g_MEFCS.pCurrentContext;
	
	g_MEFCS.pCurrentContext = pContext;
}

MEFieldContext* MEContextPush(const char *pchName, void *pOld, void *pNew, ParseTable *pTable)
{
	MEFieldContext *pRet;
	void **ppOldData = NULL;
	void **ppNewData = NULL;
	eaPush(&ppOldData, pOld);
	eaPush(&ppNewData, pNew);
	pRet = MEContextPushEA(pchName, ppOldData, ppNewData, pTable);
	eaDestroy(&ppOldData);
	eaDestroy(&ppNewData);
	return pRet;
}

void MEContextPushExternalContext(MEFieldContext* pContext, void *pOld, void *pNew, ParseTable *pTable)
{
	void **ppOldData = NULL;
	void **ppNewData = NULL;
	eaPush(&ppOldData, pOld);
	eaPush(&ppNewData, pNew);
	MEContextPushEAExternalContext(pContext, ppOldData, ppNewData, pTable);
	eaDestroy(&ppOldData);
	eaDestroy(&ppNewData);
}

static void MEContextRemoveUntouched(MEFieldContext *pContext)
{
	int i;
	for ( i=0; i < eaSize(&pContext->id.ppEntries); i++ ) {
		MEFieldContextEntry *pEntry = pContext->id.ppEntries[i];
		if(!pEntry->bTouched) {
			if(pEntry->pLabel)
				ui_WidgetRemoveFromGroup(UI_WIDGET(pEntry->pLabel));
			if(pEntry->pLabel2)
				ui_WidgetRemoveFromGroup(UI_WIDGET(pEntry->pLabel2));
			if(pEntry->pSprite)
				ui_WidgetRemoveFromGroup(UI_WIDGET(pEntry->pSprite));
			if(pEntry->pButton)
				ui_WidgetRemoveFromGroup(UI_WIDGET(pEntry->pButton));
			if (pEntry->pCheck)
				ui_WidgetRemoveFromGroup(UI_WIDGET(pEntry->pCheck));
			if (pEntry->pSeparator)
				ui_WidgetRemoveFromGroup(UI_WIDGET(pEntry->pSeparator));
			if (pEntry->pRadioButton1)
				ui_WidgetRemoveFromGroup(UI_WIDGET(pEntry->pRadioButton1));
			if (pEntry->pRadioButton2)
				ui_WidgetRemoveFromGroup(UI_WIDGET(pEntry->pRadioButton2));
			if(pEntry->pCustomWidget)
				ui_WidgetRemoveFromGroup(pEntry->pCustomWidget);
			if(pEntry->pWindow)
				ui_WindowHide(pEntry->pWindow);
			if(eaSize(&pEntry->ppFields) > 0)
				ui_WidgetRemoveFromGroup(pEntry->ppFields[0]->pUIWidget);
		}
		pEntry->iActionButtons = 0;
		pEntry->bErrorPaddingSet = false;
		pEntry->bTouched = false;
		
	}

	for ( i=0; i < eaSize(&pContext->id.ppChildren); i++ ) {
		MEContextRemoveUntouched(pContext->id.ppChildren[i]);
	}
}

static void MEContextUpdateFromData(MEFieldContext *pContext)
{
	int i, j;
	assert(	eaSize(&pContext->id.ppOldData) == eaSize(&pContext->id.ppNewData) );
	for ( j=0; j < eaSize(&pContext->id.ppEntries); j++ ) {
		MEFieldContextEntry *pEntry = pContext->id.ppEntries[j];
		if(pEntry->bNeedsDataUpdate) {
			assert( eaSize(&pEntry->ppFields) >= eaSize(&pContext->id.ppNewData) );
			for ( i=0; i < eaSize(&pContext->id.ppNewData); i++ ) {
				MEFieldSetAndRefreshFromData(pEntry->ppFields[i], pContext->id.ppOldData[i], pContext->id.ppNewData[i]);
			}
			pEntry->bNeedsDataUpdate = false;
		}
		if(pEntry->bOverrideActive)	{
			MEField *pField = ENTRY_FIELD(pEntry);
			ui_SetActive(pField->pUIWidget, pEntry->bActive);
		}
	}
}

static void MEContextPopInternal(MEFieldContext* pContext, bool incrementYPos)
{
	if(pContext->id.pParent && pContext->id.pParent->pUIContainer) {
		if(pContext->id.pParent->pUIContainer == pContext->pUIContainer)
			pContext->id.pParent->iYPos = pContext->iYPos;
		else if(incrementYPos && eaFind(&pContext->id.pParent->pUIContainer->children, pContext->pUIContainer) >= 0)
			pContext->id.pParent->iYPos += pContext->iYPos;
	} else if (!pContext->id.pParent) {
		MEContextRemoveUntouched(pContext);
	}
	MEContextUpdateFromData(pContext);
}

void MEContextPop(const char *pchName)
{
	const char *pchNamePooled = allocAddString(pchName);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	
	assert(pContext && pContext->id.pchName == pchNamePooled && !pContext->bIsExternalContext);
	MEContextPopInternal(pContext, true);
	g_MEFCS.pCurrentContext = pContext->id.pParent;
}

void MEContextPopNoIncrementYPos(const char *pchName)
{
	const char *pchNamePooled = allocAddString(pchName);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	
	assert(pContext && pContext->id.pchName == pchNamePooled && !pContext->bIsExternalContext);
	MEContextPopInternal(pContext, false);
	g_MEFCS.pCurrentContext = pContext->id.pParent;
}

void MEContextPopExternalContext(MEFieldContext* pContext)
{
	assert(pContext && g_MEFCS.pCurrentContext == pContext && pContext->bIsExternalContext);
	MEContextPopInternal(pContext, true);
	g_MEFCS.pCurrentContext = pContext->id.pExternalContextPrevContext;
}

void MEContextSetParent(UIWidget *pParent)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);

	pContext->pUIContainer = pParent;
	pContext->iYPos = 0;
}

bool MEContextExists()
{
	return g_MEFCS.pCurrentContext != NULL;
}

MEFieldContext* MEContextGetCurrent(void)
{
	return g_MEFCS.pCurrentContext;
}

static void MEContextDestroy(MEFieldContext *pContext)
{
	int i;

	if(!pContext)
		return;

	// Order here is important!  We want to destroy as close to
	// possible child->parent!

	// 1. Destroy all child contexts first -- they likely contain
	// children of existing widgets
	eaDestroyEx(&pContext->id.ppChildren, MEContextDestroy);

	// 2. Destroy all (non-custom) widgets next.
	for ( i=0; i < eaSize(&pContext->id.ppEntries); i++ ) {
		MEFieldContextEntry *pEntry = pContext->id.ppEntries[i];
		ui_WidgetQueueFree(UI_WIDGET(pEntry->pLabel));
		ui_WidgetQueueFree(UI_WIDGET(pEntry->pLabel2));
		ui_WidgetQueueFree(UI_WIDGET(pEntry->pSprite));
		ui_WidgetQueueFree(UI_WIDGET(pEntry->pButton));
		ui_WidgetQueueFree(UI_WIDGET(pEntry->pCheck));
		ui_WidgetQueueFree(UI_WIDGET(pEntry->pSeparator));
		ui_WidgetQueueFree(UI_WIDGET(pEntry->pRadioButton1));
		ui_WidgetQueueFree(UI_WIDGET(pEntry->pRadioButton2));
		ui_WidgetQueueFree(UI_WIDGET(pEntry->pWindow));
		eaDestroyEx(&pEntry->ppFields, MEFieldDestroy);
	}

	// 3. Destroy all custom widgets, since they could be scroll areas
	// and panes that include child widgets.
	for ( i=0; i < eaSize(&pContext->id.ppEntries); i++ ) {
		MEFieldContextEntry *pEntry = pContext->id.ppEntries[i];
		ui_WidgetForceQueueFree(pEntry->pCustomWidget);
	}
	
	eaDestroyEx(&pContext->id.ppAllocs, MEContextAllocFree);

	eaDestroy(&pContext->id.ppOldData);
	eaDestroy(&pContext->id.ppNewData);

	StructDestroy(parse_MEFieldContext, pContext);
}

void MEContextDestroyByName(const char *pchName)
{
	int i;
	const char *pchNamePooled = allocAddString(pchName);
	MEFieldContext ***pppChildren;
	MEFieldContext *pContext = NULL;
	
	if(!g_MEFCS.pCurrentContext) {
		pppChildren = &g_MEFCS.ppContexts;
	} else {
		pppChildren = &g_MEFCS.pCurrentContext->id.ppChildren;
	}

	for ( i=0; i < eaSize(pppChildren); i++ ) {
		if((*pppChildren)[i]->id.pchName == pchNamePooled) {
			assert(!pContext);
			pContext = (*pppChildren)[i];
		}
	}

	MEContextDestroy(pContext);
	eaFindAndRemoveFast(pppChildren, pContext);
}

MEFieldContext* MEContextCreateExternalContext(const char* pcName)
{
	MEFieldContext* pContext = StructCreate(parse_MEFieldContext);
	pContext->id.pchName = allocAddString(pcName);
	pContext->bIsExternalContext = true;
	return pContext;
}

void MEContextDestroyExternalContext(MEFieldContext* pContext)
{
	assert( pContext->bIsExternalContext && !pContext->id.pParent );
	MEContextDestroy(pContext);
}


//////////////////////////////////////////////////////////////////////////

bool MEContextFieldDiff(const char *pchFieldName)
{
	int i;
	int column;
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	void *pFirst;
	assert(pContext);
	assert(eaSize(&pContext->id.ppNewData) > 0);
	pFirst = pContext->id.ppNewData[0];

	assert(ParserFindColumn(pContext->id.pTable, pchFieldName, &column));

	for ( i=1; i < eaSize(&pContext->id.ppNewData); i++ ) {
		void *pNext = pContext->id.ppNewData[i];
		if(TokenCompare(pContext->id.pTable, column, pFirst, pNext, 0, 0) != 0)
			return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////

void* MEContextGetData(void)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert( pContext && eaSize( &pContext->id.ppNewData ));
	return pContext->id.ppNewData[0];
}

//////////////////////////////////////////////////////////////////////////

ParseTable* MEContextGetParseTable(void)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert( pContext );
	return pContext->id.pTable;
}

//////////////////////////////////////////////////////////////////////////

void MEContextIndentRight()
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->iXPos += pContext->iXIndentStep;
	pContext->iXDataStart -= pContext->iXIndentStep;
}

void MEContextIndentLeft()
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->iXPos -= pContext->iXIndentStep;
	pContext->iXDataStart += pContext->iXIndentStep;
}

void MEContextSetIndent(int indent)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->iXPos = indent;
}

void MEContextStepBackUp()
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->iYPos -= pContext->iYStep;
}

void MEContextStepDown()
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->iYPos += pContext->iYStep;
}

void MEContextAddSpacer()
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->iYPos += pContext->iYSpacer;
}

void MEContextAddCustomSpacer( int space )
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->iYPos += space;
}

//////////////////////////////////////////////////////////////////////////

void MEContextSetEnabled(bool bEnabled)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->bDisabled = !bEnabled;
}

//////////////////////////////////////////////////////////////////////////

void MEContextSetErrorFunction(MEFieldContextErrorFunction pFunction)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->pErrorFunction = pFunction;
}

void MEContextSetErrorContext(void *pErrorContext)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->pErrorContext = pErrorContext;
}

void MEContextSetErrorIcon(const char *pchErrorIcon, int iWidth, int iHeight)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	pContext->pchErrorIcon = pchErrorIcon;
	setVec2(pContext->iErrorIconSize, iWidth, iHeight);
}

static void MEContextAddRightPadding(MEFieldContextEntry *pEntry, int iPadding)
{
	// Add padding to field
	if (pEntry->pButton)
	{
		UI_WIDGET(pEntry->pButton)->rightPad += iPadding;
	}
	else if (eaSize(&pEntry->ppFields) > 0)
	{
		pEntry->ppFields[0]->pUIWidget->rightPad += iPadding;
	}
	else if (pEntry->pLabel2)
	{
		UI_WIDGET(pEntry->pLabel2)->rightPad += iPadding;
	}
	// Not supported for any other types
}

static void MEContextGetRightPadding(MEFieldContextEntry *pEntry, int *iXPos_out, int *iYPos_out, int xSize)
{
	// Add padding to field
	if (pEntry->pButton)
	{
		*iXPos_out = UI_WIDGET(pEntry->pButton)->rightPad - xSize;
		*iYPos_out = UI_WIDGET(pEntry->pButton)->y;
	}
	else if (eaSize(&pEntry->ppFields) > 0)
	{
		*iXPos_out = pEntry->ppFields[0]->pUIWidget->rightPad - xSize;
		*iYPos_out = pEntry->ppFields[0]->pUIWidget->y;
	}
	else if (pEntry->pLabel2)
	{
		*iXPos_out = UI_WIDGET(pEntry->pLabel2)->rightPad - xSize;
		*iYPos_out = UI_WIDGET(pEntry->pLabel2)->y;
	}
	else if (pEntry->pLabel)
	{
		*iXPos_out = UI_WIDGET(pEntry->pLabel)->rightPad;
		*iYPos_out = UI_WIDGET(pEntry->pLabel)->y;
	}
	// Not supported for any other types
}

static void MEContextSetFieldErrorInternal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, bool bHasError, const char *pchToolTip)
{
	AtlasTex* errorTex = atlasFindTexture( pContext->pchErrorIcon );
	int iXPos, iYPos;
	int iErrorIconSpaceWidth;
	if( pContext->iErrorIconSpaceWidth >= 0 ) {
		iErrorIconSpaceWidth = pContext->iErrorIconSpaceWidth;
	} else {
		iErrorIconSpaceWidth = errorTex->width;
	}

	if( !pEntry->bErrorPaddingSet ) {
		MEContextAddRightPadding(pEntry, iErrorIconSpaceWidth+pContext->iActionButtonPad);
		pEntry->bErrorPaddingSet = true;
	}

	if( pContext->bErrorIconOffsetFromRight ) {
		MEContextGetRightPadding(pEntry, &iXPos, &iYPos, iErrorIconSpaceWidth+pContext->iActionButtonPad);
	} else {
		UIWidget* entryLabel = UI_WIDGET( ENTRY_LABEL( pEntry ));
		iXPos = entryLabel->x - pContext->iXLabelStart;
		iYPos = entryLabel->y - pContext->iYLabelStart;
	}
	
	assert(pContext->pchErrorIcon);

	if (bHasError)
	{
		MEContextAddSpriteInternal(pContext, pEntry, pContext->pchErrorIcon, pchToolTip);
	}
	else
	{
		MEContextAddSpriteInternal(pContext, pEntry, "alpha8x8", NULL);
	}

	if( pContext->bErrorIconOffsetFromRight ) {
		// Position sprite on right edge
		ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pSprite),
							   iXPos + pContext->iErrorIconOffset[ 0 ],
							   iYPos + pContext->iErrorIconOffset[ 1 ],
							   1 - pContext->fDataWidgetWidth, 0, UITopRight );
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pSprite),
							   iXPos + pContext->iErrorIconOffset[ 0 ],
							   iYPos + pContext->iErrorIconOffset[ 1 ],
							   0, 0, UITopLeft );
	}

	if (pContext->iErrorIconSize[0] > 0 && pContext->iErrorIconSize[1] > 0)
		ui_WidgetSetDimensions(UI_WIDGET(pEntry->pSprite), pContext->iErrorIconSize[0], pContext->iErrorIconSize[1]);
	else
		ui_WidgetSetDimensions(UI_WIDGET(pEntry->pSprite), errorTex->width, errorTex->height );
}

void MEContextSetEntryError(MEFieldContextEntry *pEntry, bool bHasError, const char *pchToolTip)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);
	MEContextSetFieldErrorInternal(pContext, pEntry, bHasError, pchToolTip);
}

void MEContextSetEntryErrorForField(MEFieldContextEntry *pEntry, const char* pcFieldNameStr)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	char *estrToolTip = NULL;
	bool bHasError;

	assert(pContext);


	bHasError = pContext->pErrorFunction(pContext->pErrorContext, pcFieldNameStr, 0, &estrToolTip);
	MEContextSetEntryError(pEntry, bHasError, estrToolTip);
	estrDestroy(&estrToolTip);
}

void MEContextSetActive(MEFieldContextEntry *pEntry, bool bActive)
{
	pEntry->bOverrideActive = true;
	pEntry->bActive = bActive;
}

static void MEContextUpdateFieldError(MEFieldContext *pContext, MEFieldContextEntry *pEntry)
{
	if (pContext->pErrorFunction)
	{
		bool bHasError;
		char *estrToolTip = NULL;

		// Check for error
		estrCreate(&estrToolTip);
		bHasError = pEntry->pchFieldName && pContext->pErrorFunction(pContext->pErrorContext, pEntry->pchFieldName, pEntry->iFieldIndex, &estrToolTip);
		MEContextSetFieldErrorInternal(pContext, pEntry, bHasError, estrToolTip);
		estrDestroy(&estrToolTip);
	}
}

bool MEContextGenericErrorFunction(MEFieldGenericErrorList *pErrorList, const char *pchFieldName, int iFieldIndex, char **estrToolTip_out)
{
	int count = 0;
	FOR_EACH_IN_EARRAY(pErrorList->eaErrors, MEFieldGenericError, error)
	{
		if (stricmp(pchFieldName, error->pchFieldName) == 0 &&
			iFieldIndex == error->iFieldIndex)
		{
			estrConcatf(estrToolTip_out, "<p>%s</p>", error->estrErrorText);
			count++;
		}
	}
	FOR_EACH_END;

	if (count > 1)
		estrInsertf(estrToolTip_out, 0, "<b><font scale=1.2>%d Errors</font></b>", count);

	return (count > 0);
}

bool MEContextGenericErrorMsgFunction(MEFieldGenericErrorList *pErrorList, const char *pchFieldName, int iFieldIndex, char **estrToolTip_out)
{
	int count = 0;
	FOR_EACH_IN_EARRAY(pErrorList->eaErrors, MEFieldGenericError, error)
	{
		if (stricmp(pchFieldName, error->pchFieldName) == 0 &&
			iFieldIndex == error->iFieldIndex)
		{
			estrConcatf(estrToolTip_out, "<p>%s</p>", TranslateMessageKey( error->estrErrorText ));
			count++;
		}
	}
	FOR_EACH_END;

	if (count > 1)
		estrInsertf(estrToolTip_out, 0, "<b><font scale=1.2>%d Errors</font></b>", count);

	return (count > 0);
}

MEFieldGenericError *MEContextGenericErrorAdd(MEFieldGenericErrorList *pErrorList, const char *pchFieldName, int iFieldIndex, const char *pcErrorFormat, ...)
{
	va_list ap;
	MEFieldGenericError *pError = StructCreate(parse_MEFieldGenericError);
	va_start( ap, pcErrorFormat );
	estrConcatfv(&pError->estrErrorText, pcErrorFormat, ap);
	va_end( ap );
	pError->pchFieldName = StructAllocString(pchFieldName);
	pError->iFieldIndex = iFieldIndex;
	eaPush(&pErrorList->eaErrors, pError);
	return pError;
}

//////////////////////////////////////////////////////////////////////////

static MEFieldContextEntry* MEContextFindEntryEx(const char *pchFieldName, int iIndex, int iButtonIndex)
{
	int i;
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	MEFieldContextEntry *pEntry;
	assert(pContext);

	for ( i=0; i < eaSize(&pContext->id.ppEntries); i++ ) {
		pEntry = pContext->id.ppEntries[i];
		if(pEntry->pchFieldName == pchFieldName &&
			pEntry->iFieldIndex == iIndex &&
			pEntry->iButtonIndex == iButtonIndex) {
			return pEntry;
		}
	}

	pEntry = calloc(1, sizeof(MEFieldContextEntry));
	pEntry->pchFieldName = pchFieldName;
	pEntry->iFieldIndex = iIndex;
	pEntry->iButtonIndex = iButtonIndex;
	eaPush(&pContext->id.ppEntries, pEntry);
	return pEntry;
}
#define MEContextFindEntry(pchFieldName, iIndex) MEContextFindEntryEx(pchFieldName, iIndex, 0)

static void MEContextWidgetAddChild(UIWidget *parent, UIWidget *child)
{
	child->priority = 128;
	ui_WidgetGroupMove(&parent->children, child);
}

static int MEContextEntryRefreshPart1(MEFieldContext *pContext, MEFieldContextEntry *pEntry)
{
	int i, iRetHeight, iArraySize;
	pEntry->iFieldCount = eaSize(&pContext->id.ppNewData);
	assert(pEntry->iFieldCount > 0);
	eaClear(&pEntry->ppFields[0]->eaSiblings);

	if (pEntry->ppFields[0]->eType == kMEFieldType_MultiSpinner)
		iArraySize = TokenStoreGetFixedArraySize(pEntry->ppFields[0]->pTable, pEntry->ppFields[0]->column);
	else
		iArraySize = 0;

	pEntry->ppFields[0]->arraySize = iArraySize;
	for ( i=1; i < pEntry->iFieldCount; i++ ) {
		eaPush(&pEntry->ppFields[0]->eaSiblings, pEntry->ppFields[i]);
		pEntry->ppFields[i]->arraySize = iArraySize;
	}
	if(!pEntry->bInited) {
		pEntry->bInited = true;
		pEntry->ppFields[0]->bDontSortEnums = pContext->bDontSortComboEnums;
		
		MEFieldAddToParentPriority(pEntry->ppFields[0], pContext->pUIContainer, 0, 0, 128);
	}

	for ( i=0; i < pEntry->iFieldCount; i++ ) {
		MEFieldSetPreChangeCallback(pEntry->ppFields[i], pContext->cbPreChanged, pContext->pPreChangedData);
		MEFieldSetChangeCallbackEX(pEntry->ppFields[i], pContext->cbChanged, pContext->pChangedData, pContext->bSkipSiblingChangedCallbacks);
	}

	ui_WidgetSetPositionEx(pEntry->ppFields[0]->pUIWidget, 0, pContext->iYPos + pContext->iYDataStart, pContext->fXPosPercentage, 0, UITopLeft);
	ui_WidgetSetWidthEx(pEntry->ppFields[0]->pUIWidget, pContext->fDataWidgetWidth - pContext->fXPosPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pEntry->ppFields[0]->pUIWidget, pContext->iLeftPad + pContext->iXPos + pContext->iXDataStart, pContext->iRightPad, pContext->iTopPad, pContext->iBottomPad);
	ui_WidgetSetHeight(pEntry->ppFields[0]->pUIWidget, pContext->iWidgetHeight);
	ui_SetActive(pEntry->ppFields[0]->pUIWidget, !pContext->bDisabled);
	iRetHeight = pContext->iWidgetHeight;
	if (pContext->iTextAreaHeight &&
		pEntry->ppFields[0]->eType == kMEFieldType_TextArea)
	{
		ui_WidgetSetHeight(pEntry->ppFields[0]->pUIWidget, pContext->iWidgetHeight * pContext->iTextAreaHeight);
		iRetHeight = pContext->iWidgetHeight * pContext->iTextAreaHeight;
	}

	if (pContext->iEditableMaxLength &&
		(pEntry->ppFields[0]->eType == kMEFieldType_TextEntry || pEntry->ppFields[0]->eType == kMEFieldType_TextArea))
	{
		ui_EditableSetMaxLength(pEntry->ppFields[0]->pUIEditable, pContext->iEditableMaxLength);
	}

	if (pContext->bTextEntryTrimWhitespace &&
		pEntry->ppFields[0]->eType == kMEFieldType_TextEntry)
	{
		pEntry->ppFields[0]->pUIText->trimWhitespace = true;
	}

	if( pContext->astrOverrideSkinName ) {
		SET_HANDLE_FROM_STRING( g_hUISkinDict, pContext->astrOverrideSkinName, pEntry->ppFields[0]->pUIWidget->hOverrideSkin );
	} else {
		REMOVE_HANDLE( pEntry->ppFields[0]->pUIWidget->hOverrideSkin );
	}
	


	if (pEntry->ppFields[0]->pUIWidget->group != &pContext->pUIContainer->children) {
		MEContextWidgetAddChild(pContext->pUIContainer, pEntry->ppFields[0]->pUIWidget);
	}

	MEContextUpdateFieldError(pContext, pEntry);

	pEntry->bNeedsDataUpdate = true;

	if(pContext->cbFieldAdded)
		pContext->cbFieldAdded(pEntry, pContext->pFieldAddedData);

	return iRetHeight;
}

static void MEContextAddLabelInternal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, const char *pchText, const char *pchToolTip, bool bIsOnlyLabel)
{
	if (!pEntry->pLabel) {
		pEntry->pLabel = ui_LabelCreate(NULL, pContext->iXPos, pContext->iYPos);
		ui_LabelEnableTooltips(pEntry->pLabel);
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pLabel));
	} else {
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pLabel));
	}
	if( bIsOnlyLabel || !pContext->bLabelPaddingFromData ) {
		ui_LabelSetWidthNoAutosize(pEntry->pLabel, 1, UIUnitPercentage);
	} else {
		ui_LabelSetWidthNoAutosize(pEntry->pLabel, pContext->iXDataStart, UIUnitFixed);
	}
	ui_LabelSetText(pEntry->pLabel, pchText);
	ui_WidgetSetClickThrough(UI_WIDGET(pEntry->pLabel), true);
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pLabel), pContext->iXPos + pContext->iXLabelStart, pContext->iYPos + pContext->iYLabelStart, pContext->fXPosPercentage, 0, UITopLeft);
	ui_WidgetSetTooltipString(UI_WIDGET(pEntry->pLabel), pchToolTip);
	if( pContext->astrOverrideSkinName ) {
		SET_HANDLE_FROM_STRING( g_hUISkinDict, pContext->astrOverrideSkinName, UI_WIDGET( pEntry->pLabel )->hOverrideSkin );
	} else {
		REMOVE_HANDLE( UI_WIDGET( pEntry->pLabel )->hOverrideSkin );
	}
	ui_SetActive(UI_WIDGET(pEntry->pLabel), !pContext->bLabelsDisabled);
}

static void MEContextAddLabelMsgInternal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, const char *pchText, const char *pchToolTip, bool bIsOnlyLabel)
{
	MEContextAddLabelInternal(pContext, pEntry, NULL, NULL, bIsOnlyLabel);
	ui_LabelSetMessage(pEntry->pLabel, pchText);
	ui_WidgetSetTooltipMessage(UI_WIDGET(pEntry->pLabel), pchToolTip);
}

static void MEContextAddLabel2Internal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, const char *pchText, const char *pchToolTip)
{
	if (!pEntry->pLabel2) {
		pEntry->pLabel2 = ui_LabelCreate(NULL, pContext->iXPos, pContext->iYPos);
		ui_LabelEnableTooltips(pEntry->pLabel2);
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pLabel2));
	} else {
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pLabel2));
	}
	ui_LabelSetText(pEntry->pLabel2, pchText);
	ui_WidgetSetClickThrough(UI_WIDGET(pEntry->pLabel2), true);
	ui_LabelSetWidthNoAutosize(pEntry->pLabel2, 1, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pLabel2), pContext->iXPos + pContext->iXDataStart, pContext->iYPos + pContext->iYDataStart, pContext->fXPosPercentage, 0, UITopLeft);
	ui_WidgetSetTooltipString(UI_WIDGET(pEntry->pLabel2), pchToolTip);
	if( pContext->astrOverrideSkinName ) {
		SET_HANDLE_FROM_STRING( g_hUISkinDict, pContext->astrOverrideSkinName, UI_WIDGET( pEntry->pLabel2 )->hOverrideSkin );
	} else {
		REMOVE_HANDLE( UI_WIDGET( pEntry->pLabel2 )->hOverrideSkin );
	}
	UI_WIDGET(pEntry->pLabel2)->rightPad = pContext->iRightPad;
}

static void MEContextAddLabel2MsgInternal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, const char *pchText, const char *pchToolTip)
{
	MEContextAddLabel2Internal( pContext, pEntry, NULL, NULL );
	ui_LabelSetMessage(pEntry->pLabel2, pchText);
	ui_WidgetSetTooltipMessage(UI_WIDGET(pEntry->pLabel2), pchToolTip);
}

static void MEContextAddButtonInternal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, bool bSetWidth, const char *pchButtonText, const char *pchButtonIcon, const char *pchToolTip, UIActivationFunc clickedF, UserData clickedData)
{
	if( !pEntry->pButton ) {
		pEntry->pButton = ui_ButtonCreate( NULL, 0, 0, NULL, NULL );
	}

	ui_ButtonSetText( pEntry->pButton, pchButtonText );
	if( pchButtonIcon ) {
		ui_ButtonSetImage( pEntry->pButton, pchButtonIcon );
	} else {
		ui_ButtonClearImage( pEntry->pButton );
	}
	ui_ButtonSetCallback( pEntry->pButton, clickedF, clickedData );
	ui_ButtonResize( pEntry->pButton );
	
	MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pButton));

	if(bSetWidth)
		ui_WidgetSetWidthEx(UI_WIDGET(pEntry->pButton), pContext->fDataWidgetWidth, UIUnitPercentage);
	ui_WidgetSetHeight(UI_WIDGET(pEntry->pButton), pContext->iWidgetHeight);
	ui_WidgetSetPaddingEx(UI_WIDGET(pEntry->pButton), pContext->iLeftPad, pContext->iRightPad, pContext->iTopPad, pContext->iBottomPad);
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pButton), pContext->iXPos + pContext->iXDataStart, pContext->iYPos + pContext->iYDataStart, pContext->fXPosPercentage, 0, UITopLeft);
	ui_WidgetSetTooltipString(UI_WIDGET(pEntry->pButton), pchToolTip);
	UI_WIDGET(pEntry->pButton)->rightPad = pContext->iRightPad;
	if( pContext->astrOverrideSkinName ) {
		SET_HANDLE_FROM_STRING( g_hUISkinDict, pContext->astrOverrideSkinName, UI_WIDGET( pEntry->pButton )->hOverrideSkin );
	} else {
		REMOVE_HANDLE( UI_WIDGET( pEntry->pButton )->hOverrideSkin );
	}
	ui_SetActive(UI_WIDGET(pEntry->pButton), !pContext->bDisabled);
}

static void MEContextAddButtonMsgInternal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, bool bSetWidth, const char *pchButtonText, const char *pchButtonIcon, const char *pchToolTip, UIActivationFunc clickedF, UserData clickedData)
{
	MEContextAddButtonInternal( pContext, pEntry, bSetWidth, NULL, pchButtonIcon, NULL, clickedF, clickedData );
	ui_ButtonSetMessage( pEntry->pButton, pchButtonText );
	ui_WidgetSetTooltipMessage( UI_WIDGET( pEntry->pButton ), pchToolTip );
}

static void MEContextAddCheckInternal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, const char *pchToolTip, UIActivationFunc clickedF, UserData clickedData, bool bCurrentState)
{
	if (!pEntry->pCheck) {
		pEntry->pCheck = ui_CheckButtonCreate(0, 0, "", 0);
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pCheck));
	} else {
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pCheck));
	}
	ui_CheckButtonSetToggledCallback(pEntry->pCheck, clickedF, clickedData);
	ui_CheckButtonSetState(pEntry->pCheck, bCurrentState);
	ui_WidgetSetWidthEx(UI_WIDGET(pEntry->pCheck), pContext->fDataWidgetWidth, UIUnitPercentage);
	ui_WidgetSetHeight(UI_WIDGET(pEntry->pCheck), pContext->iWidgetHeight);
	ui_WidgetSetPaddingEx(UI_WIDGET(pEntry->pCheck), pContext->iLeftPad, pContext->iRightPad, pContext->iTopPad, pContext->iBottomPad);
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pCheck), pContext->iXPos + pContext->iXDataStart, pContext->iYPos + pContext->iYDataStart, pContext->fXPosPercentage, 0, UITopLeft);
	ui_WidgetSetTooltipString(UI_WIDGET(pEntry->pCheck), pchToolTip);
	if( pContext->astrOverrideSkinName ) {
		SET_HANDLE_FROM_STRING( g_hUISkinDict, pContext->astrOverrideSkinName, UI_WIDGET( pEntry->pCheck )->hOverrideSkin );
	} else {
		REMOVE_HANDLE( UI_WIDGET( pEntry->pCheck )->hOverrideSkin );
	}
}

static void MEContextAddSeparatorInternal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, int iThickness)
{
	if (!pEntry->pSeparator) {
		pEntry->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pEntry->pSeparator), 0, 0);
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pSeparator));
	} else {
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pSeparator));
	}
	ui_WidgetSetWidthEx(UI_WIDGET(pEntry->pSeparator), pContext->fDataWidgetWidth, UIUnitPercentage);
	if( iThickness > 0 ) {
		ui_WidgetSetHeight(UI_WIDGET(pEntry->pSeparator), iThickness);
	}
	ui_WidgetSetPaddingEx(UI_WIDGET(pEntry->pSeparator), pContext->iLeftPad, pContext->iRightPad, pContext->iTopPad, pContext->iBottomPad);
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pSeparator), 0, pContext->iYPos, pContext->fXPosPercentage, 0, UITopLeft);
	if( pContext->astrOverrideSkinName ) {
		SET_HANDLE_FROM_STRING( g_hUISkinDict, pContext->astrOverrideSkinName, UI_WIDGET( pEntry->pSeparator )->hOverrideSkin );
	} else {
		REMOVE_HANDLE( UI_WIDGET( pEntry->pSeparator )->hOverrideSkin );
	}
}

static void MEContextAddSpriteInternal(MEFieldContext *pContext, MEFieldContextEntry *pEntry, const char *pchTextureName, const char *pchToolTip)
{
	if (!pEntry->pSprite) {
		pEntry->pSprite = ui_SpriteCreate(pContext->iXPos, pContext->iYPos, -1, -1, pchTextureName);
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pSprite));
	} else {
		ui_SpriteSetTexture(pEntry->pSprite, pchTextureName);
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pSprite));
	}
	ui_SpriteResize( pEntry->pSprite );
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pSprite), pContext->iXPos + pContext->iXDataStart, pContext->iYPos + pContext->iYDataStart, pContext->fXPosPercentage, 0, UITopLeft);
	ui_WidgetSetTooltipString(UI_WIDGET(pEntry->pSprite), pchToolTip);
	UI_WIDGET(pEntry->pSprite)->rightPad = pContext->iRightPad;
	if( pContext->astrOverrideSkinName ) {
		SET_HANDLE_FROM_STRING( g_hUISkinDict, pContext->astrOverrideSkinName, UI_WIDGET( pEntry->pSprite )->hOverrideSkin );
	} else {
		REMOVE_HANDLE( UI_WIDGET( pEntry->pSprite )->hOverrideSkin );
	}
}

MEFieldContextEntry* MEContextAddLabel(const char *pchUID, const char *pchLabelText, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, -1);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddLabelInternal(pContext, pEntry, pchLabelText, pchToolTip, true);
	pContext->iYPos += pContext->iYStep;
	return pEntry;
}

MEFieldContextEntry* MEContextAddLabelMsg(const char *pchUID, const char *pchLabelText, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, -1);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddLabelMsgInternal(pContext, pEntry, pchLabelText, pchToolTip, true);
	pContext->iYPos += pContext->iYStep;
	return pEntry;
}

MEFieldContextEntry* MEContextAddLabelIndex(const char *pchUID, int index, const char *pchLabelText, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, index);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddLabelInternal(pContext, pEntry, pchLabelText, pchToolTip, true);
	pContext->iYPos += pContext->iYStep;
	return pEntry;
}

MEFieldContextEntry* MEContextAddLabelIndexMsg(const char *pchUID, int index, const char *pchLabelText, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, index);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddLabelMsgInternal(pContext, pEntry, pchLabelText, pchToolTip, true);
	pContext->iYPos += pContext->iYStep;
	return pEntry;
}

MEFieldContextEntry* MEContextAddTwoLabels(const char *pchUID, const char *pchLabel1Text, const char *pchLabel2Text, const char *pchToolTip)
{
	return MEContextAddTwoLabelsIndex(pchUID, -1, pchLabel1Text, pchLabel2Text, pchToolTip);
}

MEFieldContextEntry* MEContextAddTwoLabelsMsg(const char *pchUID, const char *pchLabel1Text, const char *pchLabel2Text, const char *pchToolTip)
{
	return MEContextAddTwoLabelsIndexMsg(pchUID, -1, pchLabel1Text, pchLabel2Text, pchToolTip);
}

MEFieldContextEntry* MEContextAddTwoLabelsIndex(const char *pchUID, int index, const char *pchLabel1Text, const char *pchLabel2Text, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, index);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddLabelInternal(pContext, pEntry, pchLabel1Text, pchToolTip, false);
	MEContextAddLabel2Internal(pContext, pEntry, pchLabel2Text, NULL);
	MEContextUpdateFieldError(pContext, pEntry);
	pContext->iYPos += pContext->iYStep;
	return pEntry;
}

MEFieldContextEntry* MEContextAddTwoLabelsIndexMsg(const char *pchUID, int index, const char *pchLabel1Text, const char *pchLabel2Text, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, index);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddLabelMsgInternal(pContext, pEntry, pchLabel1Text, pchToolTip, false);
	MEContextAddLabel2MsgInternal(pContext, pEntry, pchLabel2Text, NULL);
	MEContextUpdateFieldError(pContext, pEntry);
	pContext->iYPos += pContext->iYStep;
	return pEntry;
}

MEFieldContextEntry* MEContextAddButton(const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, const char *pchUID, const char *pchLabelText, const char *pchToolTip)
{
	return MEContextAddButtonIndex( pchButtonText, pchButtonIcon, clickedF, clickedData, pchUID, 0, pchLabelText, pchToolTip );
}

MEFieldContextEntry* MEContextAddButtonMsg(const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, const char *pchUID, const char *pchLabelText, const char *pchToolTip)
{
	return MEContextAddButtonIndexMsg( pchButtonText, pchButtonIcon, clickedF, clickedData, pchUID, 0, pchLabelText, pchToolTip );
}

MEFieldContextEntry* MEContextAddButtonIndex(const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, const char *pchUID, int index, const char *pchLabelText, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, index);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddLabelInternal(pContext, pEntry, pchLabelText, pchToolTip, false);
	MEContextAddButtonInternal(pContext, pEntry, true, pchButtonText, pchButtonIcon, pchToolTip, clickedF, clickedData);
	MEContextUpdateFieldError(pContext, pEntry);
	pContext->iYPos += pContext->iYStep;
	return pEntry;
}

MEFieldContextEntry* MEContextAddButtonIndexMsg(const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, const char *pchUID, int index, const char *pchLabelText, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, index);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddLabelMsgInternal(pContext, pEntry, pchLabelText, pchToolTip, false);
	MEContextAddButtonMsgInternal(pContext, pEntry, true, pchButtonText, pchButtonIcon, pchToolTip, clickedF, clickedData);
	MEContextUpdateFieldError(pContext, pEntry);
	pContext->iYPos += pContext->iYStep;
	return pEntry;
}

MEFieldContextEntry* MEContextAddCheck(UIActivationFunc clickedF, UserData clickedData, bool bCurrentState, const char *pchUID, const char *pchLabelText, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, 0);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddLabelInternal(pContext, pEntry, pchLabelText, pchToolTip, false);
	MEContextAddCheckInternal(pContext, pEntry, pchToolTip, clickedF, clickedData, bCurrentState);
	pContext->iYPos += pContext->iYStep;
	return pEntry;
}

MEFieldContextEntry* MEContextAddSeparator(const char *pchUID)
{
	return MEContextAddSeparatorIndex( pchUID, -1 );
}

MEFieldContextEntry* MEContextAddSeparatorIndex(const char *pchUID, int index)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, index);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddSeparatorInternal(pContext, pEntry, -1);
	pContext->iYPos += pEntry->pSeparator->widget.height;
	return pEntry;
}

MEFieldContextEntry* MEContextAddColoredSeparator( U32 color, const char* pchUID, int height )
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, 0);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	if (!pEntry->pSprite) {
		pEntry->pSprite = ui_SpriteCreate(0, 0, -1, -1, "white" );
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pSprite));
	} else {
		MEContextWidgetAddChild(pContext->pUIContainer, UI_WIDGET(pEntry->pSprite));
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pSprite), pContext->iXPos, 2 + pContext->iYPos, pContext->fXPosPercentage, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pEntry->pSprite), 1, height, UIUnitPercentage, UIUnitFixed);
	pEntry->pSprite->tint = colorFromRGBA( color );
	
	pContext->iYPos += 2 + height + 2;
	return pEntry;
}

MEFieldContextEntry* MEContextAddSprite(const char *pchTextureName, const char *pchUID, const char *pchLabelText, const char *pchToolTip)
{
	return MEContextAddSpriteIndex( pchTextureName, pchUID, -1, pchLabelText, pchToolTip );
}

MEFieldContextEntry* MEContextAddSpriteIndex(const char *pchTextureName, const char *pchUID, int index, const char *pchLabelText, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, index);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddLabelInternal(pContext, pEntry, pchLabelText, pchToolTip, false);
	MEContextAddSpriteInternal(pContext, pEntry, pchTextureName, pchToolTip);
	pContext->iYPos += pContext->iYStep;
	return pEntry;
}

MEFieldContextEntry* MEContextAddCustom(const char *pchUID)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, 0);
	pEntry->bTouched = true;
	return pEntry;
}

MEFieldContextEntry* MEContextAddCustomIndex(const char *pchUID, int index)
{
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, index);
	pEntry->bTouched = true;
	return pEntry;
}

MEFieldContextEntry* MEContextAddSimple(MEFieldType eType, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	return MEContextAddIndex(eType, pchFieldName, -1, pchDisplayName, pchToolTip);
}

MEFieldContextEntry* MEContextAddSimpleMsg(MEFieldType eType, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	return MEContextAddIndexMsg(eType, pchFieldName, -1, pchDisplayName, pchToolTip);
}

MEFieldContextEntry* MEContextAddIndex(MEFieldType eType, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, iIndex);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(eType, pContext->id.ppOldData[i], pContext->id.ppNewData[i], pContext->id.pTable, pchFieldName, 
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, 
			NULL, NULL, NULL, NULL, iIndex, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		eaPush(&pEntry->ppFields, pField);
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);
	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry* MEContextAddIndexMsg(MEFieldType eType, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, iIndex);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelMsgInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(eType, pContext->id.ppOldData[i], pContext->id.ppNewData[i], pContext->id.pTable, pchFieldName, 
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, 
			NULL, NULL, NULL, NULL, iIndex, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		eaPush(&pEntry->ppFields, pField);
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);
	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry* MEContextAddText(bool bIsMultiline, const char* strDefaultText, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	MEFieldContextEntry* entry = MEContextAddIndex( bIsMultiline ? kMEFieldType_TextArea : kMEFieldType_TextEntry,
													pchFieldName, -1, pchDisplayName, pchToolTip );
	ui_EditableSetDefaultString( ENTRY_FIELD( entry )->pUIEditable, strDefaultText );
	return entry;
}

MEFieldContextEntry* MEContextAddTextMsg(bool bIsMultiline, const char* strDefaultText, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	MEFieldContextEntry* entry = MEContextAddIndexMsg( bIsMultiline ? kMEFieldType_TextArea : kMEFieldType_TextEntry,
													   pchFieldName, -1, pchDisplayName, pchToolTip );
	ui_EditableSetDefaultMessage( ENTRY_FIELD( entry )->pUIEditable, strDefaultText );
	return entry;
}

MEFieldContextEntry* MEContextAddTextIndex(bool bIsMultiline, const char* strDefaultText, const char *pchFieldName, int index, const char *pchDisplayName, const char *pchToolTip)
{
	MEFieldContextEntry* entry = MEContextAddIndex( bIsMultiline ? kMEFieldType_TextArea : kMEFieldType_TextEntry,
													pchFieldName, index, pchDisplayName, pchToolTip );
	ui_EditableSetDefaultString( ENTRY_FIELD( entry )->pUIEditable, strDefaultText );
	return entry;
}

MEFieldContextEntry* MEContextAddTextIndexMsg(bool bIsMultiline, const char* strDefaultText, const char *pchFieldName, int index, const char *pchDisplayName, const char *pchToolTip)
{
	MEFieldContextEntry* entry = MEContextAddIndexMsg( bIsMultiline ? kMEFieldType_TextArea : kMEFieldType_TextEntry,
													   pchFieldName, index, pchDisplayName, pchToolTip );
	ui_EditableSetDefaultMessage( ENTRY_FIELD( entry )->pUIEditable, strDefaultText );
	return entry;
}

MEFieldContextEntry* MEContextAddMinMax(MEFieldType eType, F32 min, F32 max, F32 step, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, 0);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(eType, pContext->id.ppOldData[i], pContext->id.ppNewData[i], pContext->id.pTable, pchFieldName, 
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, 
			NULL, NULL, NULL, NULL, -1, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		eaPush(&pEntry->ppFields, pField);
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);
	switch(pEntry->ppFields[0]->eType) {
	case kMEFieldType_Spinner:
		ui_SpinnerEntrySetBounds(pEntry->ppFields[0]->pUISpinner, min, max, step);
		break;
	case kMEFieldType_MultiSpinner:
		ui_MultiSpinnerEntrySetBounds(pEntry->ppFields[0]->pUIMultiSpinner, min, max, step);
		break;
	case kMEFieldType_SliderText:
		ui_SliderTextEntrySetRange(pEntry->ppFields[0]->pUISliderText, min, max, step);
		break;
	case kMEFieldType_Slider:
		ui_SliderSetRange(pEntry->ppFields[0]->pUISlider, min, max, step);
		break;
	default:
		assert(false);
	}	
	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry* MEContextAddMinMaxIndex(MEFieldType eType, F32 min, F32 max, F32 step, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, iIndex);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(eType, pContext->id.ppOldData[i], pContext->id.ppNewData[i], pContext->id.pTable, pchFieldName, 
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, 
			NULL, NULL, NULL, NULL, iIndex, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		eaPush(&pEntry->ppFields, pField);
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);
	switch(pEntry->ppFields[0]->eType) {
	case kMEFieldType_Spinner:
		ui_SpinnerEntrySetBounds(pEntry->ppFields[0]->pUISpinner, min, max, step);
		break;
	case kMEFieldType_MultiSpinner:
		ui_MultiSpinnerEntrySetBounds(pEntry->ppFields[0]->pUIMultiSpinner, min, max, step);
		break;
	case kMEFieldType_SliderText:
		ui_SliderTextEntrySetRange(pEntry->ppFields[0]->pUISliderText, min, max, step);
		break;
	case kMEFieldType_Slider:
		ui_SliderSetRange(pEntry->ppFields[0]->pUISlider, min, max, step);
		break;
	default:
		assert(false);
	}	
	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry* MEContextAddDict(MEFieldType eType, const char *pchDictionary, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	return MEContextAddDictIdx(eType, pchDictionary, pchFieldName, -1, pchDisplayName, pchToolTip);
}

MEFieldContextEntry* MEContextAddDictIdx(MEFieldType eType, const char *pchDictionary, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, iIndex);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(eType, pContext->id.ppOldData[i], pContext->id.ppNewData[i], 
			pContext->id.pTable, pchFieldName, NULL, NULL, NULL, NULL, NULL, NULL, parse_ResourceInfo,
			"ResourceName", NULL, pchDictionary, NULL, false, NULL, NULL, NULL, NULL, iIndex, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		eaPush(&pEntry->ppFields, pField);
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);
	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry* MEContextAddList(MEFieldType eType, const char ***ppchList, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	return MEContextAddListIdx(eType, ppchList, pchFieldName, -1, pchDisplayName, pchToolTip);
}

MEFieldContextEntry* MEContextAddListIdx(MEFieldType eType, const char ***ppchList, const char *pchFieldName, int iIdx, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, iIdx);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(eType, pContext->id.ppOldData[i], pContext->id.ppNewData[i], 
			pContext->id.pTable, pchFieldName, NULL, NULL, NULL, NULL, NULL, NULL, parse_ResourceInfo,
			"ResourceName", NULL, NULL, NULL, false, ppchList, NULL, NULL, NULL, iIdx, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		eaPush(&pEntry->ppFields, pField);
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);
	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry* MEContextAddExpr(ExprContext *pExprContext, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, 0);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(kMEFieldTypeEx_Expression, pContext->id.ppOldData[i], pContext->id.ppNewData[i], 
			pContext->id.pTable, pchFieldName, NULL, NULL, NULL, NULL, NULL, NULL, parse_ResourceInfo,
			"ResourceName", NULL, NULL, NULL, false, NULL, NULL, NULL, pExprContext, -1, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		eaPush(&pEntry->ppFields, pField);
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);
	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}


MEFieldContextEntry* MEContextAddPicker(const char *pchDictionary, const char *pchPickerName, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, 0);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(kMEFieldType_EMPicker, pContext->id.ppOldData[i], pContext->id.ppNewData[i], 
			pContext->id.pTable, pchFieldName, NULL, NULL, NULL, NULL, NULL, NULL, parse_ResourceInfo,
			"ResourceName", NULL, pchDictionary, NULL, false, NULL, NULL, NULL, NULL, -1, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		pField->pchEMPickerName = pchPickerName;
		eaPush(&pEntry->ppFields, pField);
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);
	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry* MEContextAddEnum(MEFieldType eType, StaticDefineInt *pEnum, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	return MEContextAddEnumIndex( eType, pEnum, pchFieldName, -1, pchDisplayName, pchToolTip );
}

MEFieldContextEntry* MEContextAddEnumIndex(MEFieldType eType, StaticDefineInt *pEnum, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, iIndex);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(eType, pContext->id.ppOldData[i], pContext->id.ppNewData[i], 
			pContext->id.pTable, pchFieldName, NULL, NULL, NULL, NULL, NULL, NULL, parse_ResourceInfo,
			NULL, NULL, NULL, NULL, false, NULL, NULL, pEnum, NULL, iIndex, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		eaPush(&pEntry->ppFields, pField);
	}
	for( i = 0; i < eaSize( &pEntry->ppFields ); ++i ) {
		MEFieldSetEnum(pEntry->ppFields[i], pEnum);
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);
	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry* MEContextAddEnumMsg(MEFieldType eType, StaticDefineInt *pEnum, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	return MEContextAddEnumIndexMsg( eType, pEnum, pchFieldName, -1, pchDisplayName, pchToolTip );
}

MEFieldContextEntry* MEContextAddEnumIndexMsg(MEFieldType eType, StaticDefineInt *pEnum, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, iIndex);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelMsgInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(eType, pContext->id.ppOldData[i], pContext->id.ppNewData[i], 
			pContext->id.pTable, pchFieldName, NULL, NULL, NULL, NULL, NULL, NULL, parse_ResourceInfo,
			NULL, NULL, NULL, NULL, false, NULL, NULL, pEnum, NULL, iIndex, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		eaPush(&pEntry->ppFields, pField);
	}
	for( i = 0; i < eaSize( &pEntry->ppFields ); ++i ) {
		MEField* pField = pEntry->ppFields[i];
		MEFieldSetEnum(pField, pEnum);
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);

	for( i = 0; i < eaSize( &pEntry->ppFields ); ++i ) {
		MEField* pField = pEntry->ppFields[i];
		pField->pUICombo->bStringAsMessageKey = true;
	}

	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry* MEContextAddDataProvided(MEFieldType eType, ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	return MEContextAddDataProvidedIndex( eType, pComboParseTable, peaComboModel, pchComboField, pchFieldName, -1, pchDisplayName, pchToolTip );
}

MEFieldContextEntry* MEContextAddDataProvidedIndex(MEFieldType eType, ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip)
{
	int i, height;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchFieldName, iIndex);
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddLabelInternal(pContext, pEntry, pchDisplayName, pchToolTip, false);

	for ( i=eaSize(&pEntry->ppFields); eaSize(&pEntry->ppFields) < eaSize(&pContext->id.ppNewData); i++ ) {
		MEField *pField = MEFieldCreate(eType, pContext->id.ppOldData[i], pContext->id.ppNewData[i], 
			pContext->id.pTable, pchFieldName, NULL, NULL, NULL, NULL, NULL, NULL, pComboParseTable,
			pchComboField, NULL, NULL, NULL, false, NULL, peaComboModel, NULL, NULL, -1, 0, 0, 0, NULL);
		assertmsg(pField, "Field could not be created.  Did you pass the correct field name for the current parse table?");
		eaPush(&pEntry->ppFields, pField);
	}
	for( i = 0; i < eaSize( &pEntry->ppFields ); i++ ) {
		MEFieldSetComboModel( pEntry->ppFields[i], peaComboModel );
	}

	height = MEContextEntryRefreshPart1(pContext, pEntry);
	pContext->iYPos += pContext->iYStep + (height - pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry* MEContextAddDataProvidedMsg(MEFieldType eType, ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip)
{
	MEFieldContextEntry* pEntry = MEContextAddDataProvided( eType, pComboParseTable, peaComboModel, pchComboField, pchFieldName, "", "" );
	ui_WidgetSetTextMessage( UI_WIDGET( pEntry->pLabel ), pchDisplayName );
	ui_WidgetSetTooltipMessage( UI_WIDGET( pEntry->pLabel ), pchToolTip );
	return pEntry;
}

MEFieldContextEntry* MEContextAddDataProvidedIndexMsg(MEFieldType eType, ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip)
{
	MEFieldContextEntry* pEntry = MEContextAddDataProvidedIndex( eType, pComboParseTable, peaComboModel, pchComboField, pchFieldName, iIndex, "", "" );
	ui_WidgetSetTextMessage( UI_WIDGET( pEntry->pLabel ), pchDisplayName );
	ui_WidgetSetTooltipMessage( UI_WIDGET( pEntry->pLabel ), pchToolTip );
	return pEntry;
}

MEFieldContextEntry *MEContextEntryAddActionButton(MEFieldContextEntry *pParentEntry, const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, int iSize, const char *pchToolTip)
{
	int iXPos, iYPos;
	MEFieldContextEntry *pEntry = MEContextFindEntryEx(pParentEntry->pchFieldName, pParentEntry->iFieldIndex, (++pParentEntry->iActionButtons));
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddButtonInternal(pContext, pEntry, false, pchButtonText, pchButtonIcon, pchToolTip, clickedF, clickedData);
	if(iSize == 0)
		iSize = pContext->iWidgetHeight;
	if(iSize < 0)
		iSize = UI_WIDGET(pEntry->pButton)->width;
	MEContextAddRightPadding(pParentEntry, iSize + pContext->iActionButtonPad);
	MEContextGetRightPadding(pParentEntry, &iXPos, &iYPos, iSize + pContext->iActionButtonPad);
	ui_WidgetSetPaddingEx(UI_WIDGET(pEntry->pButton), 0, 0, 0, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pButton), iXPos, iYPos, 0, 0, UITopRight);
	ui_WidgetSetDimensions(UI_WIDGET(pEntry->pButton), iSize, pContext->iWidgetHeight);
	return pEntry;
}

MEFieldContextEntry *MEContextEntryAddActionButtonMsg(MEFieldContextEntry *pParentEntry, const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, int iSize, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextEntryAddActionButton(pParentEntry, TranslateMessageKey( pchButtonText ), pchButtonIcon, clickedF, clickedData, iSize, NULL);
	ui_WidgetSetTextMessage( UI_WIDGET( pEntry->pButton ), pchButtonText );
	ui_WidgetSetTooltipMessage( UI_WIDGET( pEntry->pButton ), pchToolTip );
	return pEntry;
}

MEFieldContextEntry *MEContextEntryAddIsSpecifiedCheck(MEFieldContextEntry *pParentEntry, UIActivationFunc clickedF, UserData clickedData, bool bCurrentlySpecified, const char *pchToolTip)
{
	MEFieldContextEntry *pEntry = MEContextFindEntryEx(pParentEntry->pchFieldName, pParentEntry->iFieldIndex, (++pParentEntry->iActionButtons));
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;
	MEContextAddCheckInternal(pContext, pEntry, pchToolTip, clickedF, clickedData, bCurrentlySpecified);
	assert(pParentEntry->pLabel);//Currently only supports being added to things with labels.
	UI_WIDGET(pEntry->pCheck)->x = UI_WIDGET(pParentEntry->pLabel)->x;
	UI_WIDGET(pEntry->pCheck)->y = UI_WIDGET(pParentEntry->pLabel)->y;
	UI_WIDGET(pParentEntry->pLabel)->x += pContext->iXIndentStep;
	ui_WidgetSetPaddingEx(UI_WIDGET(pEntry->pCheck), 0, 0, 0, 0);
	return pEntry;
}

MEFieldContextEntry *MEContextEntryMakeHighlighted(MEFieldContextEntry *pParentEntry, int iRGBA)
{
	int iXPos=0, iYpos=0;
	MEFieldContextEntry *pEntry = MEContextFindEntryEx(pParentEntry->pchFieldName, pParentEntry->iFieldIndex, (++pParentEntry->iActionButtons));
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	pEntry->bTouched = true;

	MEContextAddSpriteInternal(pContext, pEntry, "white", NULL);
	MEContextGetRightPadding(pParentEntry, &iXPos, &iYpos, 0);

	iYpos -= (pContext->iYStep - pContext->iWidgetHeight)/2;

	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pSprite), 0, iYpos, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pEntry->pSprite), 1, pContext->iYStep, UIUnitPercentage, UIUnitFixed);
	setColorFromRGBA(&pEntry->pSprite->tint, iRGBA);

	UI_WIDGET(pEntry->pSprite)->priority = 10;
	ui_WidgetGroupMove(&pContext->pUIContainer->children, UI_WIDGET(pEntry->pSprite));

	return pEntry;
}

MEFieldContextEntry *MEContextCreateWindowParent(const char *pchTitle, int iWidth, int iHeight, bool bModal, const char *pchUID)
{
	int w, h;
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, 0);
	pEntry->bTouched = true;

	if (!pEntry->pWindow)
		pEntry->pWindow = ui_WindowCreate(pchTitle, 0, 0, iWidth, iHeight);
	ui_WindowSetModal(pEntry->pWindow, bModal);
	if (!ui_WindowIsVisible(pEntry->pWindow))
		ui_WindowShow(pEntry->pWindow);

	gfxGetActiveDeviceSize(&w, &h);
	ui_WidgetSetPosition(UI_WIDGET(pEntry->pWindow), (w / g_ui_State.scale - iWidth) / 2, (h / g_ui_State.scale - iHeight) / 2);

	assert(pContext);
	pContext->pUIContainer = UI_WIDGET(pEntry->pWindow);
	pContext->iYPos = 0;

	return pEntry;
}

MEFieldContextEntry *MEContextCreateScrollAreaParent(int iHeight, const char *pchUID)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, 0);
	pEntry->bTouched = true;

	assert(pContext);

	if (!pEntry->pCustomWidget)
		pEntry->pCustomWidget = (UIWidget*)ui_ScrollAreaCreate(0, pContext->iYPos, 0, 0, 0, 0, false, true);

	((UIScrollArea*)pEntry->pCustomWidget)->autosize = true;
	ui_WidgetSetDimensionsEx(pEntry->pCustomWidget, 1, iHeight, UIUnitPercentage, UIUnitFixed);
	MEContextWidgetAddChild(pContext->pUIContainer, pEntry->pCustomWidget);
	ui_WidgetSetPosition(pEntry->pCustomWidget, pContext->iXPos, pContext->iYPos);
	if( pContext->astrOverrideSkinName ) {
		SET_HANDLE_FROM_STRING( g_hUISkinDict, pContext->astrOverrideSkinName, pEntry->pCustomWidget->hOverrideSkin );
	} else {
		REMOVE_HANDLE( pEntry->pCustomWidget->hOverrideSkin );
	}

	pContext->iYPos += iHeight;

	pContext->pUIContainer = pEntry->pCustomWidget;
	pContext->iYPos = 0;

	return pEntry;
}

MEFieldContextEntry *MEContextCreatePaneParent(int iHeight, const char *pchUID)
{
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	MEFieldContextEntry *pEntry = MEContextFindEntry(pchUID, 0);
	pEntry->bTouched = true;

	assert(pContext);

	if (!pEntry->pCustomWidget)
		pEntry->pCustomWidget = (UIWidget*)ui_PaneCreate(0, pContext->iYPos, 1, 1, UIUnitFixed, UIUnitFixed, false);

	ui_WidgetSetDimensionsEx(pEntry->pCustomWidget, 1, iHeight, UIUnitPercentage, UIUnitFixed);
	MEContextWidgetAddChild(pContext->pUIContainer, pEntry->pCustomWidget);
	ui_WidgetSetPosition(pEntry->pCustomWidget, pContext->iXPos, pContext->iYPos);
	if( pContext->astrOverrideSkinName ) {
		SET_HANDLE_FROM_STRING( g_hUISkinDict, pContext->astrOverrideSkinName, pEntry->pCustomWidget->hOverrideSkin );
	} else {
		REMOVE_HANDLE( pEntry->pCustomWidget->hOverrideSkin );
	}

	pContext->iYPos += iHeight;

	pContext->pUIContainer = pEntry->pCustomWidget;
	pContext->iYPos = 0;

	return pEntry;
}

UIScrollArea *MEContextPushScrollAreaParent(const char *pchUID)
{
	MEFieldContextEntry* entry;
	MEContextPushEA( pchUID, g_MEFCS.pCurrentContext->id.ppOldData, g_MEFCS.pCurrentContext->id.ppNewData, g_MEFCS.pCurrentContext->id.pTable );
	entry = MEContextCreateScrollAreaParent( 1, pchUID );
	return (UIScrollArea*)ENTRY_WIDGET( entry );
}

UIPane *MEContextPushPaneParent(const char *pchUID)
{
	MEFieldContextEntry* entry;
	MEContextPushEA( pchUID, g_MEFCS.pCurrentContext->id.ppOldData, g_MEFCS.pCurrentContext->id.ppNewData, g_MEFCS.pCurrentContext->id.pTable );
	entry = MEContextCreatePaneParent( 1, pchUID );
	return (UIPane*)ENTRY_WIDGET( entry );
}

static void MEContextAllocFree(MEFieldContextAlloc *pAlloc)
{
	if (pAlloc->pParseTable)
		StructDestroySafeVoid(pAlloc->pParseTable, &pAlloc->pPtr);
	else
	{
		if (pAlloc->pfResetFunction)
			pAlloc->pfResetFunction(pAlloc->pPtr);
		SAFE_FREE(pAlloc->pPtr);
	}
	SAFE_FREE(pAlloc);
}

static void *MEContextAllocMemInternal(const char *pchUID, int index, size_t iSize, MEFieldContextAllocResetFn pfResetFunction, ParseTable *pParseTable, bool bResetNow)
{
	MEFieldContextAlloc *pAlloc = NULL;
	MEFieldContext *pContext = g_MEFCS.pCurrentContext;
	assert(pContext);

	// Find an existing alloc
	FOR_EACH_IN_EARRAY(pContext->id.ppAllocs, MEFieldContextAlloc, alloc)
	{
		if (alloc->pchFieldName == pchUID && alloc->iIndex == index)
		{
			pAlloc = alloc;
			break;
		}
	}
	FOR_EACH_END;

	// Does the alloc match? If not, delete the existing block.
	if (pAlloc && 
		(pAlloc->pParseTable != pParseTable || pAlloc->iSize != iSize || pAlloc->pfResetFunction != pfResetFunction))
	{
		eaFindAndRemoveFast(&pContext->id.ppAllocs, pAlloc);
		MEContextAllocFree(pAlloc);
		pAlloc = NULL;
	}

	// Allocate a new memory block
	if (!pAlloc)
	{
		pAlloc = calloc(1, sizeof(MEFieldContextAlloc));
		pAlloc->pchFieldName = pchUID;
		pAlloc->iIndex = index;
		if (pParseTable)
		{
			pAlloc->pPtr = StructCreateVoid(pParseTable);
			pAlloc->pParseTable = pParseTable;
		}
		else
		{
			pAlloc->pPtr = calloc(1, iSize);
			pAlloc->iSize = iSize;
		}
		eaPush(&pContext->id.ppAllocs, pAlloc);
	}

	if (bResetNow)
	{
		// Reset the struct
		if (pParseTable)
		{
			StructResetVoid(pParseTable, pAlloc->pPtr);
		}
		else
		{
			pAlloc->pfResetFunction = pfResetFunction;
			if (pAlloc->pfResetFunction)
				pAlloc->pfResetFunction(pAlloc->pPtr);
		}
	}

	return pAlloc->pPtr;
}

void *MEContextAllocMem(const char *pchUID, size_t iSize, MEFieldContextAllocResetFn pfResetFunction, bool bResetNow)
{
	return MEContextAllocMemInternal(pchUID, -1, iSize, pfResetFunction, NULL, bResetNow);
}

void *MEContextAllocMemIndex(const char *pchUID, int index, size_t iSize, MEFieldContextAllocResetFn pfResetFunction, bool bResetNow)
{
	return MEContextAllocMemInternal(pchUID, index, iSize, pfResetFunction, NULL, bResetNow);
}

void *MEContextAllocStruct(const char *pchUID, ParseTable *pParseTable, bool bResetNow)
{
	return MEContextAllocMemInternal(pchUID, -1, 0, NULL, pParseTable, bResetNow);
}

void *MEContextAllocStructIndex(const char *pchUID, int index, ParseTable *pParseTable, bool bResetNow)
{
	return MEContextAllocMemInternal(pchUID, index, 0, NULL, pParseTable, bResetNow);
}

//
// Include the auto-generated code so it gets compiled
//
#include "MultiEditFieldContext.h"
#include "AutoGen/MultiEditFieldContext_h_ast.c"
