#include "auto_float.h"
#include "stashtable.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

StashTable autoFloatTable = NULL;

AUTO_RUN;
int initAutoFloat(void)
{
	autoFloatTable = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);

	return 0;
}

int RegisterAutoFloat(float *pFloat, char *pFloatName, float fInitValue, bool *pRegisteredVar)
{
	float *pFoundElement;

	if (stashFindPointer(autoFloatTable, pFloatName, &pFoundElement))
	{
		assertmsgf(0, "AUTO_FLOAT name overlap: %s", pFloatName);
	}

	stashAddPointer(autoFloatTable, pFloatName, pFloat, true);

	*pFloat = fInitValue;
	*pRegisteredVar = true;

	return 0;
}

//Sets an AUTO_FLOAT
AUTO_COMMAND ACMD_NAME(setf);
void SetAutoFloat(char *pName, float fValue)
{
	float *pFoundElement;

	if (!stashFindPointer(autoFloatTable, pName, &pFoundElement))
	{
		return;
	}

	*pFoundElement = fValue;
}
