/***************************************************************************
 
 
 
 ***************************************************************************/
#include <assert.h>
#include <string.h>
#include <stdio.h> // for sscanf


#include "sm_parser.h"

#include "Color.h"

U32 (*g_sm_GetColorMapping)(U32);

/*************************************************************************/

TupleSN s_aAlignments[] =
{
	{ "none",   kAlignment_None   },
	{ "left",   kAlignment_Left   },
	{ "right",  kAlignment_Right  },
	{ "center", kAlignment_Center },
	{ "middle", kAlignment_Center },
	{ "top",    kAlignment_Top    },
	{ "bottom", kAlignment_Bottom },
	{ "both",   kAlignment_Both   },
	{ 0 },
};

/**********************************************************************func*
 * FindTupleSN
 *
 */
TupleSN *FindTupleSN(TupleSN *pTuples, char *pch)
{
	while(pTuples->pchName!=NULL)
	{
		if(stricmp(pTuples->pchName, pch)==0)
		{
			return pTuples;
		}
		pTuples++;
	}

	return NULL;
}

/**********************************************************************func*
 * FindTupleSS
 *
 */
TupleSS *FindTupleSS(TupleSS *pTuples, char *pch)
{
	while(pTuples->pchName!=NULL)
	{
		if(stricmp(pTuples->pchName, pch)==0)
		{
			return pTuples;
		}
		pTuples++;
	}

	return NULL;
}

/**********************************************************************func*
 * sm_GetVal
 *
 */
char *sm_GetVal(char *pchAttrib, TupleSS *pTuples)
{
	TupleSS *tup = FindTupleSS(pTuples, pchAttrib);
	if(tup)
	{
		return tup->pchVal;
	}

	return NULL;
}

/**********************************************************************func*
 * sm_GetAlignment
 *
 */
int sm_GetAlignment(char *pchAttrib, TupleSS *pTuples)
{
	TupleSS *tup = FindTupleSS(pTuples, pchAttrib);
	if(tup)
	{
		TupleSN *tup2 = FindTupleSN(s_aAlignments, tup->pchVal);
		if(tup2!=NULL)
		{
			return tup2->iVal;
		}
	}

	return kAlignment_None;
}

/**********************************************************************func*
 * sm_GetColor
 *
 */
U32 sm_GetColor(char *pchAttrib, TupleSS *pTuples)
{
	U32 uiRet = 0x000000ff;

	TupleSS *tup = FindTupleSS(pTuples, pchAttrib);
	if(tup)
	{
		if(tup->pchVal[0]=='#')
		{
			sscanf(tup->pchVal+1, "%x", &uiRet);

			if(strlen(&tup->pchVal[1])<=6)
			{
				uiRet <<= 8;
				uiRet |= 0xff;
			}
		}
		else
		{
			return g_sm_GetColorMapping ? g_sm_GetColorMapping(ColorRGBAFromName(tup->pchVal)) : ColorRGBAFromName(tup->pchVal);
		}
	}

	return uiRet;
}


/* End of File */
