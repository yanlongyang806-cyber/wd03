#ifndef HTML_FORMS_H
#define HTML_FORMS_H

void formAppendStart(char **estr, const char *pAction, const char *pMethod, const char *pName, const char *pExtraAttrs);
void formAppendStartInline(char **estr, const char *pAction, const char *pMethod, const char *pName);
void formAppendEnd(char **estr);

void formAppendEnum(char **estr, const char *pVarName, const char *pEnumData, int iCurrentVal);
void formAppendEnumList(char **estr, const char *pVarName, const char **ppListName, int iCurrentVal, bool bHasNullValue);
void formAppendSelection(char **estr, const char *pVarName, const char **ppListName, int *pListValue, int iCurrentVal, int iCount);
void formAppendSelectionStringValues(char **estr, const char *pVarName, const char **ppListName, const char **ppListValue, const char *pCurrentVal, int iCount);

void formAppendEdit(char **estr, int iEditSize, const char *pVarName, const char *pVal);
void formAppendPasswordEdit(char **estr, int iEditSize, const char *pVarName, const char *pVal);

void formAppendTextarea(char **estr, int rows, int cols, const char *pVarName, const char *pVal);
void formAppendCheckBox(char **estr, const char *pVarName, bool bChecked);
void formAppendCheckBoxScripted(char **estr, const char *pVarName, const char *pScriptAction, bool bChecked);
void formAppendEditInt(char **estr, int iEditSize, const char *pVarName, int iVal);
void formAppendHidden(char **estr, const char *pVar, const char *pVal);

void formAppendHiddenInt(char **estr, const char *pVar, int iVal);

void formAppendSubmit(char **estr, const char *pText);
void formAppendNamedSubmit(char **estr, const char *pName, const char *pText);

void formAppendReset(char **estr, const char *pText);

#endif