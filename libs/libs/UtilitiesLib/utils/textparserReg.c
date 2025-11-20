#include "textparser.h"

#include <stdio.h>
#include <string.h>
#include "RegistryReader.h"
#include "error.h"
#include "estring.h"


// key should be either a full key "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Coh" or just the post-cryptic bit, "Coh"
int ParserWriteRegistry(const char *key, ParseTable pti[], void *structptr, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int succeeded = 1;
	int i;

	char fullKey[CRYPTIC_MAX_PATH];
	char buf[1024];
	RegReader rr;

	rr = createRegReader();

	if (strStartsWith(key, "HKEY")) {
		strcpy(fullKey, key);
	} else {
		sprintf(fullKey, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\%s", key);
	}
	if (!initRegReader(rr, fullKey))
	{
		return 0;
	}

	// data segment
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) 
		{
			continue; // don't allow TOK_REDUNDANTNAME
		}
		if (!FlagsMatchAll(pti[i].type,iOptionFlagsToMatch))
		{
			continue;
		}

		if (!FlagsMatchNone(pti[i].type,iOptionFlagsToExclude))
		{
			continue;
		}
		if (pti[i].name && pti[i].name[0] && TokenToSimpleString(pti, i, structptr, SAFESTR(buf), 0))
		{
			rrWriteString(rr, pti[i].name, buf);			
		}
	} // each data token
	destroyRegReader(rr);
	return succeeded;
}

// Returns 0 if any field was not read (e.g. a new field was added since last written)
int ParserReadRegistry(const char *key, ParseTable pti[], void *structptr)
{
	int succeeded = 1;
	int i;

	char fullKey[CRYPTIC_MAX_PATH];
	char buf[1024];
	RegReader rr;

	rr = createRegReader();

	if (strStartsWith(key, "HKEY")) {
		strcpy(fullKey, key);
	} else {
		sprintf(fullKey, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\%s", key);
	}
	initRegReader(rr, fullKey);

	// data segment
	FORALL_PARSETABLE(pti, i)
	{
		assert(pti[i].name);
		if (pti[i].type & TOK_REDUNDANTNAME) continue; // don't allow TOK_REDUNDANTNAME

		if (!pti[i].name[0]) continue;
		if (!rrReadString(rr, pti[i].name, SAFESTR(buf)))
		{
			succeeded = 0;
			continue;
		}

		TokenFromSimpleString(pti, i, structptr, buf);
	} // each data token
	destroyRegReader(rr);
	return succeeded;
}


int ParserReadRegistryStringified(const char *key, ParseTable pti[], void *structptr, const char *value_name)
{
	char fullKey[CRYPTIC_MAX_PATH];
	char valueLenName[128];
	RegReader rr;
	int iSize;
	char *pFullString;
	int succeeded;

	rr = createRegReader();

	if (strStartsWith(key, "HKEY")) {
		strcpy(fullKey, key);
	} else {
		sprintf(fullKey, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\%s", key);
	}
	if (!initRegReader(rr, fullKey))
	{
		return 0;
	}

	sprintf(valueLenName, "%s_Length", value_name);
	if (!rrReadInt(rr, valueLenName, &iSize))
	{
		return 0;
	}

	pFullString = malloc(iSize + 2);
	if (!rrReadString(rr, value_name, pFullString, iSize + 1))
	{
		free(pFullString);
		return 0;
	}

	succeeded = ParserReadText(pFullString, pti, structptr, 0);
	free(pFullString);
	return succeeded;
}




int ParserWriteRegistryStringified(const char *key, ParseTable pti[], void *structptr, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *value_name)
{
	char *pFullStructString = NULL;

	char fullKey[CRYPTIC_MAX_PATH];
	char valueLenName[128];
	RegReader rr;
	bool succeeded = true;

	rr = createRegReader();

	if (strStartsWith(key, "HKEY")) {
		strcpy(fullKey, key);
	} else {
		sprintf(fullKey, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\%s", key);
	}
	if (!initRegReader(rr, fullKey))
	{
		return 0;
	}

	ParserWriteText(&pFullStructString, pti, structptr, 0, iOptionFlagsToMatch, iOptionFlagsToExclude);

	if (!rrWriteString(rr, value_name, pFullStructString))
	{
		Errorf("rrWriteString failure");
		succeeded = false;
	}
	
	sprintf(valueLenName, "%s_Length", value_name);
	if (!rrWriteInt(rr, valueLenName, estrLength(&pFullStructString)))
	{
		Errorf("rrWriteInt failure");
		succeeded = false;
	}


	destroyRegReader(rr);
	estrDestroy(&pFullStructString);
	return succeeded;
}

