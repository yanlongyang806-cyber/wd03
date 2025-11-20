#include "fileServing.h"
#include "estring.h"
#include "stringutil.h"
#include "fileServing_h_ast.h"

bool DeconstructFileServingName(char *pInName, GlobalType *pOutContainerType, ContainerID *pOutContainerID,
	char **ppTypeString, char **ppInnerName)
{
	char **ppParts = NULL;
	int i;

	DivideString(pInName, "/", &ppParts, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	if (eaSize(&ppParts) < 4)
	{
		eaDestroyEx(&ppParts, NULL);
		return false;
	}

	*pOutContainerType = NameToGlobalType(ppParts[0]);
	if (*pOutContainerType == GLOBALTYPE_NONE)
	{
		eaDestroyEx(&ppParts, NULL);
		return false;
	}

	if (!StringToUint(ppParts[1], pOutContainerID))
	{
		eaDestroyEx(&ppParts, NULL);
		return false;
	}

	estrCopy2(ppTypeString, ppParts[2]);

	estrClear(ppInnerName);

	for (i=3; i < eaSize(&ppParts); i++)
	{
		estrConcatf(ppInnerName, "%s%s", i == 3 ? "" : "/", ppParts[i]);
	}

	eaDestroyEx(&ppParts, NULL);
	return true;
}


#include "fileServing_h_ast.c"
