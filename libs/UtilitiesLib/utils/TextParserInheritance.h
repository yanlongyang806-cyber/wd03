#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#include "textparser.h"

// This header file contains definitions for the textparser object inheritance scheme

AUTO_ENUM;
typedef enum enumOverrideType
{
	OVERRIDE_NONE = -1, // Field does not inherit
	OVERRIDE_SET = 0, // Setting a value for a specific simple field
	OVERRIDE_ADD, // Adding an element to the end of an earray
	OVERRIDE_REMOVE, // NULLs out an optional struct or optional polymorph, if present
					// in the parent, or removes from an indexed earray
	OVERRIDE_ARRAY_SET, // Overrides all members of an array or earray

} enumOverrideType;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct SingleFieldInheritanceData
{
	char *pFieldName; // object path to the field we are overwriting
		// note that for OVERRIDE_ADD, this name contains the name of the new field. So
		// if you are adding a new field named "fireball" to an earray called ".character.powers", 
		// pFieldName would be ".character.powers[fireball]"

	enumOverrideType eType; //what we are doing to that field
	char *pNewValue; //stringed version of values to set for that field
	char **pArrayValues; //stringed version of array values

	const char *pCurrentFile; AST(CURRENTFILE)
	int iLineNum; AST(LINENUM)
} SingleFieldInheritanceData;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL; 
typedef struct InheritanceData
{
	char *pParentName;
	SingleFieldInheritanceData **ppFieldData;

	// inheritancedata has CURRENTFILE, TIMESTAMP and LINENUM, so that after the inheriting object
	// has all the inheritance stuff applied to it, it can then get at least quasi-reasonable values
	// for those three things, namely, the ones from the inheritancedata
	const char *pCurrentFile; AST(CURRENTFILE)
	int iTimeStamp; AST(TIMESTAMP)
	int iLineNum; AST(LINENUM)

} InheritanceData;


// For a struct which has inheritance data, normalize all override data field names - this means changing
//  redundant names / aliases into root names.
//
// Returns false on error, true on success (even if nothing is done)
bool StructInherit_NormalizeFieldNames(ParseTable *pTPI, void *pStruct);

// For a struct which inherits, reflect all changes that have been made to it (presumably due to editing)
// back into its inheritance data
//
// Returns false on error, true on success (even if nothing is done)
bool StructInherit_UpdateFromStruct(ParseTable *pTPI, void *pStruct, bool bIncludeNoTextSave);

// For a struct which has inheritance data, reflect all override data in its inheritance data into the
// struct itself, after copying from a parent structure
//
// Returns false on error, true on success (even if nothing is done)
bool StructInherit_ApplyToStruct(ParseTable *pTPI, void *pStruct, void *pParent);

// Turns an independent struct into one that inherits. Will create inheritance data and set the parent
// If filename is passed in, sets the filename of the inheritance data
void StructInherit_StartInheriting(ParseTable *pTPI, void *pStruct, const char *pParentName, const char *pFileName);

// Turn an inheriting struct into a fully independent struct. Will destroy inheritance data if it exists
void StructInherit_StopInheriting(ParseTable *pTPI, void *pStruct);

// Returns true if this parse table allows inheriting
bool StructInherit_IsSupported(ParseTable *pTPI);

// Returns true if this structure is currently inheriting; false if the
// struct does not support inheritance or is not currently inheriting.
bool StructInherit_IsInheriting(ParseTable *pTPI, void *pStruct);

// Returns column of the inheritance data for this table
int StructInherit_GetInheritanceDataColumn(ParseTable *pTPI);

// Returns the name of the structure inherited from, or NULL if it is not inheriting
char *StructInherit_GetParentName(ParseTable *pTPI, void *pStruct);

// Changes the filename of an inherited struct (needed for editing). Doesn't modify the struct itself
void StructInherit_SetFileName(ParseTable *pTPI, void *pStruct, const char *pFileName);

// Returns the type of override used by the specified field, or NONE if it is not an override
#define StructInherit_GetOverrideType(pTPI, pStruct, pXPathOfField) StructInherit_GetOverrideTypeEx(pTPI, pStruct, pXPathOfField, false)
enumOverrideType StructInherit_GetOverrideTypeEx(ParseTable *pTPI, void *pStruct, char *pXPathOfField, bool bCheckParentPaths);

// for an inheriting struct, add a new override. Does nothing if the override already exists.
// Note that it is not necessary to set any data in the new field. Just set the field in the struct itself.
void StructInherit_CreateFieldOverride(ParseTable *pTPI, void *pStruct, char *pXPathOfField);

// Add a new structure to an indexed earray in the parent. Pass in the entire path to the new object
void StructInherit_CreateAddStructOverride(ParseTable *pTPI, void *pStruct, char *pXPathOfNewStructure);

// Removes a structure from an indexed earray in the parent, or remove an optional struct
void StructInherit_CreateRemoveStructOverride(ParseTable *pTPI, void *pStruct, char *pXPathOfNewStructure);

// Entirely Replaces an array or earray with a set of values
void StructInherit_CreateArrayOverride(ParseTable *pTPI, void *pStruct, char *pobjectpathOfField);

//  any overrides set on the structure for the passed in path (add, set, or remove)
void StructInherit_DestroyOverride(ParseTable *pTPI, void *pStruct, char *pXPathOfField);

// Compare two structures, that may be inherited, and return (-1,0,1) depending
// inherited structs will always sort after non-inherited structs
int StructInherit_CompareStructs(ParseTable *pTPI, void *pStruct1, void *pStruct2);