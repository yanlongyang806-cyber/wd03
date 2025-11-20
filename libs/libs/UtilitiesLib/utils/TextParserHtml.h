#pragma once

//putting some stuff here so it can be seen from both textparserhtml.c and textparsercallbacks_inline.h


// Max depth handling

// ------------------------------------------------------------------------------------------------------
#define writeHTMLHandleMaxDepthAndCollapsed(offset, tpi, column, structptr, index, out, pContext) \
if(pContext->iMaxDepth && ((pContext->internalState.iDepth+offset) > pContext->iMaxDepth)) \
{ \
	outputTPILink(tpi, column, structptr, index, out, pContext); \
	return true; \
} \
{ \
	int ignoredInteger = 0; \
	bool bForceCollapsed = GetIntFromTPIFormatString(&tpi[column], "collapsed", &ignoredInteger); \
	if((pContext->internalState.iDepth > 1) && bForceCollapsed) \
	{ \
		outputTPILink(tpi, column, structptr, index, out, pContext); \
		return true; \
	} \
}


void outputTPILink(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext);

bool all_arrays_writehtmlfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext);