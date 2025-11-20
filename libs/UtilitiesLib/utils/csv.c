#include "csv.h"
#include "earray.h"
#include "error.h"
#include "file.h"

void csvWriteToFile (SA_PARAM_NN_STR const char *fileName, void ** ppData, ParseTable *pti, 
					 CsvHeaderFileWrite_CB header_cb, CsvFileWrite_CB element_cb)
{
	FileWrapper *file = fopen(fileName, "w");
	int i, size;

	if (!file)
		return;

	if (header_cb)
		header_cb(file, pti);

	if (!element_cb)
		return;

	size = eaSize(&ppData);
	for (i=0; i<size; i++)
	{
		element_cb(file, ppData[i]);
	}
	fclose(file);
}

void csvWriteToEstr (char **estr, void ** ppData, ParseTable *pti, 
					 CsvHeaderEstrWrite_CB header_cb, CsvEstrWrite_CB element_cb)
{
	int i, size;
	if (header_cb)
		header_cb(estr, pti);

	if (!element_cb)
		return;

	size = eaSize(&ppData);
	for (i=0; i<size; i++)
	{
		element_cb(estr, ppData[i]);
	}
}

// Input must be an open file
bool csvReadLine(FileWrapper *file, STRING_EARRAY *eaFields, int iMaxLength)
{
	char *buffer;
	char *cur, *start, *end;
	bool bQuoted; // Whether a value is enclosed in quotes
	char cEOF; // Character that ends value string, either ',' or '"' (depends on bQuoted)
	char cEndChar; // Temporary storage for end character

	if (!eaFields || feof(file->fptr))
		return false;
	buffer = malloc(iMaxLength);
	if (!fgets(buffer, iMaxLength, file))
	{
		free(buffer);
		return false;
	}
	cur = buffer;
	while (cur && *cur && (*cur != '\n'))
	{
		char *pCopy = NULL;
		bQuoted = false;
		cEOF = ',';
		
		if (*cur == '"')
		{
			bQuoted = true;
			cEOF = '"';
			cur++;
		}
		start = cur;

		while (*cur && (*cur != '\n'))
		{
			if (*cur == cEOF)
			{
				if (!bQuoted || *(cur+1) != '"')
					break;
				if (bQuoted)
					cur++; // Advance to second double-quote
			}
			cur++; // Advance past character
		}
		end = cur;
		cEndChar = *end;
		*end = 0;
		estrCopy2(&pCopy, start);
		estrReplaceOccurrences(&pCopy, "\"\"", "\"");
		eaPush(eaFields, pCopy);
		*end = cEndChar;
		cur++;
		if (bQuoted)
		{
			if (*cur == ',')
				cur++; // Advance past comma after ending double-quote
			else if (*cur && *cur != '\n')
			{
				Errorf("Error parsing line: %s", buffer);
					return false;
			}
		}
	}
	free(buffer);
	return true;
}