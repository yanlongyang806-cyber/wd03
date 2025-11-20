#include "htmlForms.h"
#include "estring.h"
#include "earray.h"

void formAppendStart(char **estr, const char *pAction, const char *pMethod, const char *pName, const char *pExtraAttrs)
{
	estrConcatf(estr, "<form name=\"%s\" %s action=\"%s\" method=%s>\n", pName, (pExtraAttrs)?pExtraAttrs:"", pAction, pMethod);
}

void formAppendStartInline(char **estr, const char *pAction, const char *pMethod, const char *pName)
{
	estrConcatf(estr, "<form style=\"display:inline\" name=\"%s\" action=\"%s\" method=%s>\n", pName, pAction, pMethod);
}

void formAppendEnd(char **estr)
{
	estrConcatf(estr, "</form>\n");
}

void formAppendEnum(char **estr, const char *pVarName, const char *pEnumData, int iCurrentVal)
{
	char *pTempEnumBuffer = NULL;
	char *pToken = NULL;
	char *pContext = NULL;
	int iEnumVal = 0;
	estrAllocaCreate(&pTempEnumBuffer, (int)strlen(pEnumData+1));
	estrCopy2(&pTempEnumBuffer, pEnumData);

	estrConcatf(estr, "<select class=\"formdata\" name=\"%s\">\n", pVarName);

	pToken = strtok_s(pTempEnumBuffer, "|", &pContext);
	while(pToken != NULL)
	{
		estrConcatf(estr, "<option %svalue=\"%d\">%s</option>\n", 
			(iCurrentVal == iEnumVal) ? "SELECTED " : "",
			iEnumVal, 
			pToken);
		iEnumVal++;

		pToken = strtok_s(NULL, "|", &pContext);
	}

	estrConcatf(estr, "</select>\n");
}

void formAppendEnumList(char **estr, const char *pVarName, const char **ppListName, int iCurrentVal, bool bHasNullValue)
{
	int iEnumVal = 0, i;

	estrConcatf(estr, "<select class=\"formdata\" name=\"%s\">\n", pVarName);
	
	if (bHasNullValue)
	{
		iCurrentVal++;
		estrConcatf(estr, "<option %svalue=\"%d\">--</option>\n", 
			(iCurrentVal == iEnumVal) ? "SELECTED " : "", iEnumVal++);
	}

	for (i=0; i<eaSize(&ppListName); i++)
	{
		estrConcatf(estr, "<option %svalue=\"%d\">%s</option>\n", 
			(iCurrentVal == iEnumVal) ? "SELECTED " : "",
			iEnumVal++, ppListName[i]);
	}

	estrConcatf(estr, "</select>\n");
}

void formAppendSelection(char **estr, const char *pVarName, const char **ppListName, int *pListValue, int iCurrentVal, int iCount)
{
	int i;	
	estrConcatf(estr, "<select class=\"formdata\" name=\"%s\">\n", pVarName);
	for (i=0; i<iCount; i++)
	{
		estrConcatf(estr, "<option %svalue=\"%d\">%s</option>\n", 
			(iCurrentVal == pListValue[i]) ? "SELECTED " : "",
			pListValue[i], 
			ppListName[i]);
	}
	estrConcatf(estr, "</select>\n");
}

void formAppendSelectionStringValues(char **estr, const char *pVarName, const char **ppListName, const char **ppListValue, const char *pCurrentVal, int iCount)
{
	int i;	
	estrConcatf(estr, "<select class=\"formdata\" name=\"%s\">\n", pVarName);
	for (i=0; i<iCount; i++)
	{
		estrConcatf(estr, "<option %svalue=\"%s\">%s</option>\n", 
			(!strcmp(pCurrentVal, ppListValue[i])) ? "SELECTED " : "",
			ppListValue[i],
			ppListName[i]);
	}
	estrConcatf(estr, "</select>\n");
}

void formAppendEdit(char **estr, int iEditSize, const char *pVarName, const char *pVal)
{
	char *temp = NULL;
	estrCopyWithHTMLEscapingSafe(&temp, pVal, false);
	estrConcatf(estr, "<input class=\"formdata\" type=\"text\" size=\"%d\" name=\"%s\" value=\"%s\">\n", iEditSize, pVarName, temp);
	estrDestroy(&temp);
}
void formAppendPasswordEdit(char **estr, int iEditSize, const char *pVarName, const char *pVal)
{
	char *temp = NULL;
	estrCopyWithHTMLEscapingSafe(&temp, pVal, false);
	estrConcatf(estr, "<input class=\"formdata\" type=\"password\" size=\"%d\" name=\"%s\" value=\"%s\">\n", iEditSize, pVarName, temp);
	estrDestroy(&temp);
}

void formAppendTextarea(char **estr, int rows, int cols, const char *pVarName, const char *pVal)
{
	char *temp = NULL;
	estrCopyWithHTMLEscapingSafe(&temp, pVal, false);
	estrConcatf(estr, "<textarea class=\"formdata\" rows=\"%d\" cols=\"%d\" name=\"%s\">%s</textarea>\n",
		rows, cols, pVarName, temp);
	estrDestroy(&temp);
}

void formAppendCheckBox(char **estr, const char *pVarName, bool bChecked)
{
	estrConcatf(estr, "<input class=\"formdata\" type=\"checkbox\" name=\"%s\" %svalue=\"1\">\n", pVarName, bChecked ? "CHECKED " : "");
}

void formAppendCheckBoxScripted(char **estr, const char *pVarName, const char *pScriptAction, bool bChecked)
{
	estrConcatf(estr, "<input class=\"formdata\" type=\"checkbox\" name=\"%s\" %s %svalue=\"1\">\n", pVarName, pScriptAction ? pScriptAction : "", bChecked ? "CHECKED " : "");
}

void formAppendEditInt(char **estr, int iEditSize, const char *pVarName, int iVal)
{
	char szTemp[20];
	sprintf(szTemp, "%d", iVal);
	estrConcatf(estr, "<input class=\"formdata\" type=\"text\" size=\"%d\" name=\"%s\" value=\"%s\">\n", iEditSize, pVarName, szTemp);
}

void formAppendHidden(char **estr, const char *pVar, const char *pVal)
{
	char *temp = NULL;
	estrCopyWithHTMLEscapingSafe(&temp, pVal, false);
	estrConcatf(estr, "<input class=\"formdata\" type=\"hidden\" name=\"%s\" value=\"%s\">\n", pVar, temp);
	estrDestroy(&temp);
}

void formAppendHiddenInt(char **estr, const char *pVar, int iVal)
{
	estrConcatf(estr, "<input class=\"formdata\" type=\"hidden\" name=\"%s\" value=\"%d\">\n", pVar, iVal);
}

void formAppendSubmit(char **estr, const char *pText)
{
	estrConcatf(estr, "<input class=\"formdata\" type=\"submit\" value=\"%s\">\n", pText);
}

void formAppendNamedSubmit(char **estr, const char *pName, const char *pText)
{
	estrConcatf(estr, "<input class=\"formdata\" type=\"submit\" name=\"%s\" value=\"%s\">\n", pName, pText);
}

void formAppendReset(char **estr, const char *pText)
{
	estrConcatf(estr, "<input class=\"formdata\" type=\"reset\" value=\"%s\">\n", pText);
}