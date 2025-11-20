/***************************************************************************
 
 
 
 *
 * Module Description:
 *
 * This is a fairly general simple markup parser which can handle HTML-like
 * markup. The majority of the brains is in the caller, which provides a set
 * of callback functions specifying how to handle the various tags found in
 * the text.
 *
 * Nothing is done specially with whitespace here. It is considered part of
 * the regular text. The caller will need to provide further parsing of the
 * text if it wants to split by whitespace as well.
 *
 * Tags are put in angle brackets <like this>. The first word must follow
 * the opening < immediately. This is the tag name which is matched to a
 * callback function. Following the tag can be one or more (up to
 * SM_MAX_PARAMS) values. If there is more than one value, they must be
 * in attrib=value pairs. If there is only one value, it is assumed to be
 * the default attrib.
 *
 * The result of SMParse is a block which contains a tree of other blocks.
 * These blocks can contain others, ad infinitum. The construction and
 * structure of the tree is up to the caller.
 *
 * This code is only generic to a point. It does make some assumptions about
 * what is being parsed (text formatted as above) and that there is a certain
 * kind of hierarchy which it will be parsed into. Each block has position
 * and dimension information attached to it since the basic purpose for this
 * class is for text layout.
 *
 ***************************************************************************/
#include <assert.h>
#include <stdlib.h> // calloc
#include <stdio.h> // printf debugging
#include <string.h>

#include "earray.h"
#include "memorypool.h"

#include "sm_parser.h"
#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(SMBlock);

/**********************************************************************func*
 * sm_CreateBlock
 *
 */
SMBlock *sm_CreateBlock(void)
{
	SMBlock *pnew;

	// Initialize the memory pool (only does anything the first time it's called).
	MP_CREATE(SMBlock, 64);

	pnew = MP_ALLOC(SMBlock);

	return pnew;
}

/**********************************************************************func*
 * sm_AppendBlock
 *
 */
SMBlock *sm_AppendBlock(SMBlock *pBlockParent, SMBlock *pBlock)
{
	assert(pBlockParent->bHasBlocks);

	pBlock->pParent = pBlockParent;
	eaPush(&pBlockParent->ppBlocks, pBlock);

	return pBlock;
}

/**********************************************************************func*
 * sm_CreateContainer
 *
 */
SMBlock *sm_CreateContainer(void)
{
	SMBlock *pnew = sm_CreateBlock();

	pnew->bHasBlocks = true;

	return pnew;
}

/**********************************************************************func*
 * sm_AppendNewBlock
 *
 */
SMBlock *sm_AppendNewBlock(SMBlock *pBlock)
{
	return sm_AppendBlock(pBlock, sm_CreateBlock());
}


/**********************************************************************func*
 * sm_AppendNewBlock
 *
 */
SMBlock *sm_AppendNewBlockAndData(SMBlock *pBlock, int iSize)
{
	SMBlock *pnew = sm_AppendBlock(pBlock, sm_CreateBlock());
	if(pnew!=NULL && iSize>0)
	{
		pnew->pv = calloc(iSize, 1);
		pnew->bFreeOnDestroy = true;
	}
	return pnew;
}

/**********************************************************************func*
 * sm_AppendNewContainer
 *
 */
SMBlock *sm_AppendNewContainer(SMBlock *pBlock)
{
	return sm_AppendBlock(pBlock, sm_CreateContainer());
}

/**********************************************************************func*
 * sm_AppendNewContainer
 *
 */
SMBlock *sm_AppendNewContainerAndData(SMBlock *pBlock, int iSize)
{
	SMBlock *pnew = sm_AppendBlock(pBlock, sm_CreateContainer());
	if(pnew!=NULL && iSize>0)
	{
		pnew->pv = calloc(iSize, 1);
		pnew->bFreeOnDestroy = true;
	}
	return pnew;
}

/**********************************************************************func*
 * indent
 *
 */
static void indent(int i)
{
	while(i>0)
	{
		printf("  ");
		i--;
	}
}

/**********************************************************************func*
 * sm_BlockDump
 *
 */
void sm_BlockDump(SMBlock *pBlock, int iLevel, SMTagDef aTagDefs[])
{
	indent(iLevel);

	if(pBlock->sType>=0)
	{
		printf("%-6.6s: ",aTagDefs[pBlock->sType].pchName);
	}
	else if(pBlock->sType==-1)
	{
		printf("block: ");
	}
	else
	{
		printf("%-3d: ", pBlock->sType);
	}

	if(pBlock->pv
		&& pBlock->bFreeOnDestroy
		&& (stricmp(aTagDefs[pBlock->sType].pchName, "text")==0
		|| stricmp(aTagDefs[pBlock->sType].pchName, "img")==0))
	{
		char ach[50];
		sprintf(ach, "\"%.30s\"", (char *)pBlock->pv);
		printf("%-30.30s (0x%08p)", ach, (char *)pBlock->pv);
	}
	else
	{
		//		printf("(null)");
	}

	printf("  (%dx%d)", pBlock->pos.iMinWidth, pBlock->pos.iMinHeight);
	printf("  (%dx%d)", pBlock->pos.iWidth, pBlock->pos.iHeight);
	printf("  (%d, %d)", pBlock->pos.iX, pBlock->pos.iY);

	printf("\n");

	if(pBlock->bHasBlocks)
	{
		int i;
		int iNumBlocks = eaSize(&pBlock->ppBlocks);
		indent(iLevel);
		printf("{\n");
		for(i=0; i<iNumBlocks; i++)
		{
			sm_BlockDump(pBlock->ppBlocks[i], iLevel+1, aTagDefs);
		}
		indent(iLevel);
		printf("}\n");
	}
}


/**********************************************************************func*
 * sm_DestroyBlock
 *
 */
void sm_DestroyBlock(SMBlock *pBlock)
{
	PERFINFO_AUTO_START("sm_DestroyBlock", 1);
	if(pBlock->bHasBlocks)
	{
		int i;
		for(i=eaSize(&pBlock->ppBlocks)-1; i>=0; i--)
		{
			sm_DestroyBlock(pBlock->ppBlocks[i]);
			pBlock->ppBlocks[i] = S32_TO_PTR(0xd00dd00d);
		}

		eaDestroy(&pBlock->ppBlocks);
	}

	if(pBlock->pv && pBlock->bFreeOnDestroy)
	{
		free(pBlock->pv);
		pBlock->pv = S32_TO_PTR(0xd00dd00d);
	}

	MP_FREE(SMBlock, pBlock);
	PERFINFO_AUTO_STOP();
}

/**********************************************************************func*
 * sm_Parse
 *
 */
SMBlock *sm_Parse(const char *pchOrig, SMTagDef aTagDefs[], bool bSafeOnly, SMInitProc pfnInit, SMTextProc pfnText)
{
	SMBlock *pBlock = sm_CreateContainer();

	pBlock->sType = -1;
	pBlock = pfnInit(pBlock);

	return sm_ParseInto(pBlock, pchOrig, aTagDefs, bSafeOnly, pfnText);
}

// Global counter for preventing links with <nolink>
int g_preventLinks;

/**********************************************************************func*
 * sm_ParseInto
 *
 */
SMBlock *sm_ParseInto(SMBlock *pBlock, const char *pchOrig, SMTagDef aTagDefs[], bool bSafeOnly, SMTextProc pfnText)
{
	int i = 0;
	int k;
	SMBlock *pBlockTop = pBlock;

	int len = (int)strlen(pchOrig);
	g_preventLinks = 0;
	while(i<len)
	{
		k = i;
		while(pchOrig[i]!='<' && i<len)
		{
			i++;
		}

		pfnText(pBlock, pchOrig+k, i-k);

		if(pchOrig[i]=='<')
		{
			SMTagDef *tag = NULL;
			int iTag;

			// We've got a command
			k = i;
			while(pchOrig[i]!='>' && pchOrig[i]!=' ' && pchOrig[i]!='\r' && pchOrig[i]!='\n' && pchOrig[i]!='\t' && i<len)
			{
				i++;
			}

			// We're at the end of the string or command
			for(iTag = 0; aTagDefs[iTag].pchName!=NULL; iTag++)
			{
				if(strlen(aTagDefs[iTag].pchName) == (size_t)(i-k-1)
					&& strnicmp(&pchOrig[k+1], aTagDefs[iTag].pchName, i-k-1)==0)
				{
					tag = &aTagDefs[iTag];
					break;
				}
			}

			if(tag==NULL || i-k-1<1 || (bSafeOnly && tag && !tag->bSafe))
			{
				while(pchOrig[i]!='>' && i<len)
				{
					i++;
				}

				// OK, just send this out (maybe make an emote?)
				pfnText(pBlock, pchOrig+k, i-k+1);

				i++; // Move to next character after failed tag
			}
			else
			{
				bool bDone = false;
				char aBufs[SM_MAX_PARAMS][128];
				TupleSS aParams[SM_MAX_PARAMS];

				memcpy(aParams, tag->aParams, sizeof(TupleSS)*SM_MAX_PARAMS);

				while(!bDone)
				{
					// Eat up the leading whitespace
					while((pchOrig[i]==' ' || pchOrig[i]=='\r' || pchOrig[i]=='\n' || pchOrig[i]=='\t') && i<len)
					{
						i++;
					}
					k = i;
					while(pchOrig[i]!='>' && pchOrig[i]!='=' && i<len)
					{
						i++;
					}
					if(pchOrig[i]!='=')
					{
						if(i!=k)
						{
							// If there was something afterwards, assume
							// it's the default (first) parameter.
							strncpy_s(aBufs[0], i-k+1, &pchOrig[k], _TRUNCATE);
							aParams[0].pchVal = aBufs[0];
						}
						// suck anything remaining as a form of error handling.
						while(pchOrig[i]!='>' && i<len)
						{
							i++;
						}
						pBlock = tag->pfn(pBlock, iTag, aParams);
						if(pBlock==NULL) pBlock=pBlockTop;
						bDone = true;
						break;
					}
					else
					{
						int l;
						int iIdxParam = -1;

						for(l=0;
							l<SM_MAX_PARAMS && aParams[l].pchName!=NULL && aParams[l].pchName[0]!=0;
							l++)
						{
							if(strlen(aParams[l].pchName) == (size_t)(i-k)
								&& strnicmp(&pchOrig[k], aParams[l].pchName, i-k)==0)
							{
								iIdxParam = l;
							}
						}

						i++; // Skip over the =

						k = i;

						if(pchOrig[i]=='"')
						{
							k++; // skip over the "
							i++;
							while(pchOrig[i]!='"' && i<len)
							{
								i++;
							}
						}
						else if(pchOrig[i]=='\'')
						{
							k++; // skip over the '
							i++;
							while(pchOrig[i]!='\'' && i<len)
							{
								i++;
							}
						}
						else
						{
							while(pchOrig[i]!='>' && pchOrig[i]!=' ' && pchOrig[i]!='\r' && pchOrig[i]!='\n' && pchOrig[i]!='\t' && i<len)
							{
								i++;
							}
						}

						// If this is a parameter that I know about, copy it.
						// If I don't know about it, just ignore it.
						if(iIdxParam >= 0)
						{
							if (i-k+1 <= 127)
							{
								strncpy_s(aBufs[iIdxParam], i-k+1, &pchOrig[k], _TRUNCATE);
							}
							else
							{
								strncpy_s(aBufs[iIdxParam], 127, &pchOrig[k], _TRUNCATE);
							}
							aParams[iIdxParam].pchVal = aBufs[iIdxParam];
						}

						if(pchOrig[i]=='"' && i<len)
						{
							i++;
						}
						else if(pchOrig[i]=='\'' && i<len)
						{
							i++;
						}

						if(pchOrig[i]!=' ' && pchOrig[i]!='\r' && pchOrig[i]!='\n' && pchOrig[i]!='\t')
						{
							while(pchOrig[i]!='>' && pchOrig[i]!=' ' && pchOrig[i]!='\r' && pchOrig[i]!='\n' && pchOrig[i]!='\t' && i<len)
							{
								i++;
							}
							pBlock = tag->pfn(pBlock, iTag, aParams);
							if(pBlock==NULL) pBlock=pBlockTop;
							bDone = true;
						}
					}
				}

				i++; // move to the next character after the command
			}
		}
		else
		{
			i++; // skip over the crap
		}
	}

	return pBlockTop;
}

/* End of File */
