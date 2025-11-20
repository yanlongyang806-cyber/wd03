

#ifndef CSVEXPORT_H
#define CSVEXPORT_H

#pragma once
GCC_SYSTEM

// An enum to describe the CSV column to be exported
AUTO_ENUM;
typedef enum CSVColumnType
{
	kCSVColumn_Text,
	// A simple text column

	kCSVColumn_Message,
	// A reference to a message or display message

	kCSVColumn_Flag,
	// A flag combo column

	kCSVColumn_Expression,
	// An expression (will be output with ` instead of ")

	kCSVColumn_Boolean,
	// A boolean value column, TRUE or FALSE

	kCSVColumn_Parent,
	// The parent or inheritance information for the referent
	
	kCSVColumn_StaticText,
	// The column is not in the parse table, it's just static text to be output

} CSVColumnType;

//CSVColumn callback
typedef void (*CSVColumnCB) (char**);

AUTO_STRUCT;
typedef struct CSVColumn
{
	char *pchTitle;
	// Display header for the column

	char *pchObjPath;
	// What column in the ParseTable to output
	// Note: if type is StaticText, this is just static text to output

	char **ppchFixes;
	//Appended or Prepended to each column/token

	CSVColumnType eType;
	// What type of column is being output

	U32 bRemoveWhitespace : 1;
	//Removes internal whitespaces around commas of the field
	U32 bPrefix : 1;
	//if the is prefix.  If false, this is Postfix.
	U32 bTokens : 1;
	//If this column has tokens in it
} CSVColumn;

AUTO_STRUCT;
typedef struct CSVConfig
{
	CSVColumn **eaColumns;
	// Information about the header, and columns to export to the csv

	char **eaRefList;
	//A List of the resource names of the referents to export to CSV

	char *pchStructName;
	//The name of the struct we're trying to export

	char *pchScope;
	// The scope of the referents to export.  "*" means all.

	char *pchScopeColumnName;
	// The name of the column that is the scope of this parse table

	char *pchFileName;
	// The path to the file to output the CSV data

	char *pchDictionary;
	// The name of the dictionary to export from

	bool bMicrotransactionExport;
	// Terrible hack to allow special behavior for microtransaction exports - VAS 091611
} CSVConfig;

void CSV_GetDocumentsDirEx(char *chDocumentsDir, size_t iLength);
#define CSV_GetDocumentsDir(pcDir) CSV_GetDocumentsDirEx(pcDir, ARRAY_SIZE_CHECKED(pcDir))

void referent_CSV(void *pRef, ParseTable *pTPI, CSVColumn **eaColumns, char **estr, bool bMicrotransactionExport);
void referent_CSVHeader(CSVColumn **eaColumns, char **estr, bool bMicrotransactionExport);

#endif //CSVEXPORT_H