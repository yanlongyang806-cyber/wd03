

void TestLateLinkArgString(char **ppStoredArgString, char *pNewArgString, char *pFuncName)
{
	if (!(*ppStoredArgString))
	{
		*ppStoredArgString = pNewArgString;
	}
	else
	{
		assertmsgf(strcmp(*ppStoredArgString, pNewArgString) == 0, "Argument mismatch for LATELINK function %s (%s != %s)", pFuncName, *ppStoredArgString, pNewArgString);
	}
}