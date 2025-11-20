/***************************************************************************



***************************************************************************/

#include "textparserinheritance.h"
#include "structInternals.h"
#include "autogen/textparserinheritance_h_ast.h"
#include "autogen/textparserinheritance_c_ast.h"

#include "objpath.h"
#include "estring.h"
#include "MemoryPool.h"
#include "tokenstore.h"
#include "error.h"
#include "stringcache.h"
#include "structinternals_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems);); // Until we can get this memory to trickle down correctly

MP_DEFINE(SingleFieldInheritanceData);
MP_DEFINE(InheritanceData);

AUTO_RUN;
void RegisterInheritanceMemoryPools(void)
{
	MP_CREATE_COMPACT(SingleFieldInheritanceData, 1024, 1024, 0.8);
	MP_CREATE_COMPACT(InheritanceData, 512, 512, 0.8);
}

//returns -1 if not found (or asserts if bAssertOnFail is true)
int StructInherit_GetInheritanceDataColumn(ParseTable *pTPI)
{
	int iInheritanceColumn;

	if (!ParserFindColumnWithFlag(pTPI, TOK_INHERITANCE_STRUCT, &iInheritanceColumn))
	{
		return -1;
	}

	if (TOK_GET_TYPE(pTPI[iInheritanceColumn].type) == TOK_STRUCT_X
		&& TokenStoreGetStorageType(pTPI[iInheritanceColumn].type) == TOK_STORAGE_INDIRECT_SINGLE
		&& pTPI[iInheritanceColumn].subtable == parse_InheritanceData)
	{
		return iInheritanceColumn;
	}

	return -1;
}

//returns true if something was removed
bool RemoveInheritanceDataField(InheritanceData *pInheritanceData, char *pFieldName)
{
	int iNumFields = eaSize(&pInheritanceData->ppFieldData);
	int i;

	for (i=0; i < iNumFields; i++)
	{
		if (stricmp(pInheritanceData->ppFieldData[i]->pFieldName, pFieldName) == 0)
		{
			StructDestroy(parse_SingleFieldInheritanceData, pInheritanceData->ppFieldData[i]);
			eaRemoveFast(&pInheritanceData->ppFieldData, i);
			return true;
		}
	}

	return false;
}

void AddInheritanceDataField(InheritanceData *pInheritanceData, char *pFieldName, enumOverrideType eType, char *pNewValue)
{
	SingleFieldInheritanceData *pData = StructCreate(parse_SingleFieldInheritanceData);
	RemoveInheritanceDataField(pInheritanceData, pFieldName);

	pData->eType = eType;
	pData->pFieldName = strdup(pFieldName);
	pData->pNewValue = strdup(pNewValue);

	eaPush(&pInheritanceData->ppFieldData, pData);
}


bool StructInherit_NormalizeFieldNames(ParseTable *pTPI, void *pStruct)
{
	int i;
	int iNumFields;
	int iInheritanceColumn;


	InheritanceData *pInheritanceData;

	assert(pStruct);
	assert(pTPI);

	iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);

	if (iInheritanceColumn == -1)
	{
		return false;
	}

	pInheritanceData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);

	if (!pInheritanceData)
	{
		return true;
	}

	iNumFields = eaSize(&pInheritanceData->ppFieldData);

	for (i=0; i < iNumFields; i++)
	{
		SingleFieldInheritanceData *pCurField = pInheritanceData->ppFieldData[i];
		char *pNormalizedFieldName = NULL;
		bool bResult;
		char *pResultString = NULL;
		
		estrStackCreate(&pResultString);
		estrStackCreate(&pNormalizedFieldName);

		bResult = ParserResolvePath(pCurField->pFieldName, pTPI, NULL, 
			NULL, NULL, NULL, NULL, &pResultString, &pNormalizedFieldName, 0);

		if (!bResult)
		{
			if (strStartsWith(pResultString, PARSERRESOLVE_TRAVERSE_IGNORE_SHORT))
			{
				estrDestroy(&pResultString);
				estrDestroy(&pNormalizedFieldName);
				continue;
			}

			InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Couldn't normalize objectpath %s",
				pCurField->pFieldName);
			estrDestroy(&pNormalizedFieldName);
			estrDestroy(&pResultString);
			return false;
		}

		StructFreeString(pCurField->pFieldName);
		pCurField->pFieldName = strdup(pNormalizedFieldName);

		estrDestroy(&pNormalizedFieldName);
		estrDestroy(&pResultString);
	}

	return true;
}


// Given a string like "foo.bar[wakka].happy[crazy]" returns "foo.bar[wakka].happy" in ppRootFieldName
// and "crazy" in ppAddedStructName (out args are EStrings)
// Strips quotes from crazy if found
void DivideFieldNameForInheritAdd(char *pInFieldName_const, char **ppRootFieldName, char **ppAddedStructName)
{
	char *pInFieldName = strdup(pInFieldName_const);
	int iLen = (int)strlen(pInFieldName);
	char *pLastLeftBracket;

	if (pInFieldName[iLen-1] != ']')
	{
		free(pInFieldName);
		return;
	}

	pLastLeftBracket = strrchr(pInFieldName, '[');

	if (!pLastLeftBracket)
	{
		free(pInFieldName);
		return;
	}

	*pLastLeftBracket = 0;
	estrCopy2(ppRootFieldName, pInFieldName);
	*pLastLeftBracket = '[';

	if (pInFieldName[iLen-2] == '"' && pLastLeftBracket[1] == '"')
	{
		// strip enclosing quotes
		iLen--;
		pLastLeftBracket++;
	}
	pInFieldName[iLen-1] = 0;
	estrCopy2(ppAddedStructName, pLastLeftBracket + 1);

	free(pInFieldName);
}


bool StructInherit_ApplyToStruct(ParseTable *pTPI, void *pStruct, void *pParent)
{
	int i;
	int iNumFields;
	int iInheritanceColumn;
	TextParserAutoFixupCB *pFixupCB;


	InheritanceData *pInheritanceData;
	char *pTempString = NULL;


	assert(pStruct);
	assert(pTPI);

	iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);

	if (iInheritanceColumn == -1)
	{
		return false;
	}

	pInheritanceData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);

	if (!pInheritanceData)
	{
		return true;
	}

	if (pParent)
	{
		char *keyName = NULL;
		int iKeyColumn;
		estrStackCreate(&keyName);
		iKeyColumn = ParserGetTableKeyColumn(pTPI);
		assert(iKeyColumn != -1);
		objGetKeyEString(pTPI, pStruct, &keyName);
		//zero out the pointer in the referent to the inheritance data, will add it back in at the end of the process
		TokenStoreSetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL, NULL);
		StructDeInitVoid(pTPI, pStruct);
		StructCopyFieldsVoid(pTPI, pParent, pStruct, 0, TOK_INHERITANCE_STRUCT | TOK_NO_INHERIT);
		TokenStoreSetPointer(pTPI, iInheritanceColumn, pStruct, 0, pInheritanceData, NULL);
		// Restore name
		TokenStoreSetString(pTPI, iKeyColumn, pStruct, 0, keyName, NULL, NULL, NULL, NULL);
		estrDestroy(&keyName);
	}

	iNumFields = eaSize(&pInheritanceData->ppFieldData);

	for (i=0; i < iNumFields; i++)
	{
		SingleFieldInheritanceData *pCurField = pInheritanceData->ppFieldData[i];
		ParseTable *pTPIForField;
		int iColumnForField;
		void *pStructForField;
		int iIndexForField;
		bool bResult;
		char *pchPathResult = NULL;


		switch (pCurField->eType)
		{
		case OVERRIDE_SET:
			estrStackCreate(&pchPathResult);
			bResult = objPathResolveFieldWithResult(pCurField->pFieldName, pTPI, pStruct, 
				&pTPIForField, &iColumnForField, &pStructForField, &iIndexForField, OBJPATHFLAG_CREATESTRUCTS, &pchPathResult);

			if (!bResult)
			{
				if (strStartsWith(pchPathResult, PARSERRESOLVE_TRAVERSE_IGNORE_SHORT))
				{
					estrDestroy(&pchPathResult);
					continue;
				}

				estrDestroy(&pchPathResult);
				InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Couldn't resolve objectpath %s while applying inheritance data (parent %s)",
					pCurField->pFieldName, pInheritanceData->pParentName);
				return false;
			}
			
			estrDestroy(&pchPathResult);

			TokenSetSpecifiedIfPossible(pTPIForField, iColumnForField, pStructForField, true);

			FieldClear(pTPIForField,iColumnForField,pStructForField,iIndexForField);

			//special case... earrays of ints, floats, strings can be as a single field, and treat | as ,
			if (!TOK_HAS_SUBTABLE(pTPIForField[iColumnForField].type) && TokenStoreStorageTypeIsEArray(TokenStoreGetStorageType(pTPIForField[iColumnForField].type)) && iIndexForField == -1)
			{
				if (pCurField->pNewValue && pCurField->pNewValue[0])
				{
					bResult = readstring_autogen(pTPIForField, iColumnForField, pStructForField, 0, pCurField->pNewValue, READSTRINGFLAG_BARSASCOMMAS);
				}
			}
			else
			{
				bResult = FieldReadText(pTPIForField,iColumnForField, pStructForField, iIndexForField, pCurField->pNewValue);
			}
			
			if (!bResult && pCurField->pNewValue)
			{
				ErrorFilenamef(pInheritanceData->pCurrentFile, "Couldn't read text %s for objectpath %s while applying inheritance data",
					pCurField->pNewValue, pCurField->pFieldName);
			}
			break;
		case OVERRIDE_ARRAY_SET:
			{
				int arraySize;
				int arrayIndex;
				bResult = objPathResolveField(pCurField->pFieldName, pTPI, pStruct, 
					&pTPIForField, &iColumnForField, &pStructForField, &iIndexForField, OBJPATHFLAG_CREATESTRUCTS);				

				if (!bResult)
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Couldn't resolve objectpath %s while applying inheritance data (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					return false;
				}

				if (TokenStoreGetStorageType(pTPIForField[iColumnForField].type) == TOK_STORAGE_DIRECT_FIXEDARRAY
					|| TokenStoreGetStorageType(pTPIForField[iColumnForField].type) == TOK_STORAGE_INDIRECT_FIXEDARRAY)
				{
					arraySize = TokenStoreGetNumElems(pTPIForField, iColumnForField, pStructForField, NULL);
					if (eaSize(&pCurField->pArrayValues) > arraySize)
					{
						InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "objectpath %s points to fixed array with fewer members than specified values, in ARRAY_SET operation(parent %s)",
							pCurField->pFieldName, pInheritanceData->pParentName);
						return false;
					}
				}
				else if (TokenStoreGetStorageType(pTPIForField[iColumnForField].type) != TOK_STORAGE_DIRECT_EARRAY &&
					TokenStoreGetStorageType(pTPIForField[iColumnForField].type) != TOK_STORAGE_INDIRECT_EARRAY)
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "objectpath %s doesn't point to an array, which is invalid for ARRAY_SET (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					return false;
				}

				if (iIndexForField != -1)
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "objectpath %s points to specific array member, which is invalid for ARRAY_SET (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					return false;
				}

				TokenClear(pTPIForField, iColumnForField, pStructForField);
				for (arrayIndex = 0; arrayIndex < eaSize(&pCurField->pArrayValues); arrayIndex++)
				{
					bResult = FieldReadText(pTPIForField, iColumnForField, pStructForField, arrayIndex, pCurField->pArrayValues[arrayIndex]);

					if (!bResult && pCurField->pArrayValues[arrayIndex])
					{
						// Apparently the inheritance parser doesn't trim whitespace itself, and neither do
						//  some of the textparser readtext calls.  There are cases where (for some reason) the
						//  inheritance writer will create strings with whitespace, which are readable by that
						//  field type's readtext calls - but if the field type ever changes, these fields may
						//  no longer be readable (e.g. changing a field from an earray of structparam'd references
						//  to an earray of enum ints).  So to handle this scenario, if the first attempt to read
						//  fails we call it again with whitespace trimmed.
						char *estrTrim = estrStackCreateFromStr(pCurField->pArrayValues[arrayIndex]);
						estrTrimLeadingAndTrailingWhitespace(&estrTrim);
						bResult = FieldReadText(pTPIForField, iColumnForField, pStructForField, arrayIndex, estrTrim);
						estrDestroy(&estrTrim);
					}

					if (!bResult && pCurField->pArrayValues[arrayIndex])
					{
						InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Couldn't read text %s for objectpath %s index %d while applying inheritance data",
							pCurField->pArrayValues[arrayIndex], pCurField->pFieldName, arrayIndex);
					}
				}

				TokenSetSpecifiedIfPossible(pTPIForField, iColumnForField, pStructForField, true);



				break;
			}
		case OVERRIDE_ADD:
			{
				//first, we divide our fieldName (which is of the form .foo.fieldName[addedStructName]) into the root field name
				//and the added struct name
				char *pRootFieldName = NULL;
				char *pAddedStructName = NULL;
				void *pNewStruct;
				void ***pppEArray;
				int iChildKeyColumn;
				char *pAddedStructNameFoundInStruct = NULL;

				DivideFieldNameForInheritAdd(pCurField->pFieldName, &pRootFieldName, &pAddedStructName);

				if (!pRootFieldName)
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "FieldName %s for OVERRIDE_ADD is badly formed", pCurField->pFieldName); 
					return false;
				}

				bResult = objPathResolveField(pRootFieldName, pTPI, pStruct, 
					&pTPIForField, &iColumnForField, &pStructForField, &iIndexForField, 0);

				if (!bResult)
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Couldn't resolve objectpath %s while applying inheritance data (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					return false;
				}

				pNewStruct = StructCreateFromStringEscapedWithFileAndLine(pTPIForField[iColumnForField].subtable, pCurField->pNewValue,
					pCurField->pCurrentFile, pCurField->iLineNum);

				if (!pNewStruct) {
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "During Textparser Inheritance, StructCreate failed to create a %s",
						ParserGetTableName(pTPIForField[iColumnForField].subtable));
					return false;
				}

				pppEArray = TokenStoreGetEArray(pTPIForField, iColumnForField, pStructForField, NULL);

				iChildKeyColumn = ParserGetTableKeyColumn(pTPIForField[iColumnForField].subtable);

				if (iChildKeyColumn < 0 || (pTPIForField[iColumnForField].type & TOK_NO_INDEX))
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Must be indexed by key to do OVERRIDE_ADD");
					return false;
				}

				bResult = FieldWriteText(pTPIForField[iColumnForField].subtable, iChildKeyColumn, pNewStruct, 0, &pAddedStructNameFoundInStruct, 0);

				if (!bResult)
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Couldn't read key column out of newly created struct in OVERRIDE_ADD (struct %s)", pAddedStructName);
					return false;
				}
			
				if (stricmp(pAddedStructName, pAddedStructNameFoundInStruct) != 0)
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Expected name of OVERRIDE_ADD struct %s, found %s", pAddedStructName,
						pAddedStructNameFoundInStruct);
					return false;
				}

				eaIndexedEnableVoid(pppEArray, pTPIForField[iColumnForField].subtable);
				eaPush(pppEArray, pNewStruct);

				estrDestroy(&pRootFieldName);
				estrDestroy(&pAddedStructName);
				estrDestroy(&pAddedStructNameFoundInStruct);
				TokenSetSpecifiedIfPossible(pTPIForField, iColumnForField, pStructForField, true);



			}
			break;

		case OVERRIDE_REMOVE:
			bResult = objPathResolveField(pCurField->pFieldName, pTPI, pStruct, 
				&pTPIForField, &iColumnForField, &pStructForField, &iIndexForField, 0);

			if (!bResult)
			{
				InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Couldn't resolve objectpath %s while applying inheritance data (parent %s)",
					pCurField->pFieldName, pInheritanceData->pParentName);
				return false;
			}

			if ((TOK_GET_TYPE(pTPIForField[iColumnForField].type) != TOK_STRUCT_X && TOK_GET_TYPE(pTPIForField[iColumnForField].type) != TOK_POLYMORPH_X))				
			{
				InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "objectpath %s points to something other than a struct, illegal for OVERRIDE_REMOVE (parent %s)",
					pCurField->pFieldName, pInheritanceData->pParentName);
				return false;
			}
			if (TokenStoreGetStorageType(pTPIForField[iColumnForField].type) == TOK_STORAGE_INDIRECT_SINGLE)
			{
				FieldClear(pTPIForField,iColumnForField,pStructForField,iIndexForField);
			}
			else if (TokenStoreGetStorageType(pTPIForField[iColumnForField].type) == TOK_STORAGE_INDIRECT_SINGLE)
			{
				void ***pppEArray;

				if (iIndexForField < 0)
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Objectpath %s points to array member that does not exist, illegal for OVERRIDE_REMOVE (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					return false;
				}
				pppEArray = TokenStoreGetEArray(pTPIForField, iColumnForField, pStructForField, NULL);

				if (!eaIndexedGetTable(pppEArray))
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Objectpath %s points to array member in non-indexed earray, illegal for OVERRIDE_REMOVE (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					return false;

				}

				eaRemove(pppEArray, iIndexForField);
			}
			else
			{
				InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Objectpath %s points to an embedded struct, illegal for OVERRIDE_REMOVE (parent %s)",
					pCurField->pFieldName, pInheritanceData->pParentName);
				return false;
			}

			TokenSetSpecifiedIfPossible(pTPIForField, iColumnForField, pStructForField, true);



			break;

		default:
			InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Unknown/unsupported inheritance operation");
			return false;
		}
	}


	{
		char *pDupCurFile = strdup(pInheritanceData->pCurrentFile);
		ReApplyPreParseToStruct(pTPI, pStruct, pDupCurFile, pInheritanceData->iTimeStamp, pInheritanceData->iLineNum);
		free(pDupCurFile);
	}

	pFixupCB = ParserGetTableFixupFunc(pTPI);

	if (pFixupCB)
	{
		pFixupCB(pStruct, FIXUPTYPE_POST_INHERITANCE_APPLICATION, NULL);
	}

	return true;
}

bool StructInherit_UpdateFromStruct(ParseTable *pTPI, void *pStruct, bool bIncludeNoTextSave)
{
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);
	InheritanceData *pInheritanceData;
	int iNumInheritanceFields;
	int i;
	char *pTempString = NULL;
	int iExcludeFlags = TOK_NO_WRITE | TOK_NO_TEXT_SAVE;

	//!!!! temp hack by cg to avoid crash
	if (iInheritanceColumn < 0 )
		return true;

	assertmsg(iInheritanceColumn >= 0, "Attempted to apply inheritance to table without inheritance data");

	pInheritanceData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);

	if (!pInheritanceData)
	{
		return true;
	}

	if (!StructInherit_NormalizeFieldNames(pTPI, pStruct))
	{
		return false;
	}

	if (bIncludeNoTextSave)
	{
		iExcludeFlags = TOK_NO_WRITE;
	}

	estrStackCreate(&pTempString);
		
	iNumInheritanceFields = eaSize(&pInheritanceData->ppFieldData);

	for (i=0; i < iNumInheritanceFields; i++)
	{
		ParseTable *pTPIForField;
		int iColumnForField;
		void *pStructForField;
		int iIndexForField;
		bool bResult;
		const char *pchFileName = ParserGetFilename(pTPI,pStruct);
		char *pObjPathResult = NULL;

		SingleFieldInheritanceData *pCurField = pInheritanceData->ppFieldData[i];

		switch (pCurField->eType)
		{
		case OVERRIDE_SET:
			estrStackCreate(&pObjPathResult);
			bResult = objPathResolveFieldWithResult(pCurField->pFieldName, pTPI, pStruct, 
				&pTPIForField, &iColumnForField, &pStructForField, &iIndexForField, OBJPATHFLAG_CREATESTRUCTS, &pObjPathResult);

			if (!bResult)
			{
				if (strStartsWith(pObjPathResult, PARSERRESOLVE_TRAVERSE_IGNORE_SHORT))
				{
					estrDestroy(&pObjPathResult);
					continue;
				}

				estrDestroy(&pObjPathResult);
				ErrorFilenamef(pchFileName, "Couldn't resolve obj path during StructInherit_UpdateFromStruct (field %s)", pCurField->pFieldName);
				estrDestroy(&pTempString);
				return false;
			}
			estrDestroy(&pObjPathResult);

			if ((TokenStoreGetStorageType(pTPIForField[iColumnForField].type) == TOK_STORAGE_DIRECT_EARRAY ||
				TokenStoreGetStorageType(pTPIForField[iColumnForField].type) == TOK_STORAGE_INDIRECT_EARRAY)
				&& iIndexForField == -1)
			{
//				ErrorFilenamef(pchFileName, "objectpath %s points to an array, which is invalid for SET (parent %s)",
//					pCurField->pFieldName, pInheritanceData->pParentName);

				// Instead of generating an error, if we find a SET for an array, change it to an ARRAY_SET and repeat the process
				pCurField->eType = OVERRIDE_ARRAY_SET;
				i--;
				estrClear(&pTempString);
				continue;
			}
			
			bResult = FieldWriteTextWFlags(pTPIForField,iColumnForField, pStructForField, iIndexForField, &pTempString, 0, 0, iExcludeFlags);

			if (!bResult)
			{
				ErrorFilenamef(pchFileName, "Couldn't FieldWriteText during StructInherit_UpdateFromStruct (field %s)", pCurField->pFieldName);
				estrDestroy(&pTempString);
				return false;
			}

			SAFE_FREE(pCurField->pNewValue);
			pCurField->pNewValue = strdup(pTempString);

			estrClear(&pTempString);
			break;


		case OVERRIDE_ARRAY_SET:
			{			
				int arraySize;
				int arrayIndex;
				bResult = objPathResolveField(pCurField->pFieldName, pTPI, pStruct, 
					&pTPIForField, &iColumnForField, &pStructForField, &iIndexForField, OBJPATHFLAG_CREATESTRUCTS);

				if (!bResult)
				{
					ErrorFilenamef(pchFileName, "Couldn't resolve objectpath %s while applying inheritance data (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					estrDestroy(&pTempString);
					return false;
				}

				if (TokenStoreGetStorageType(pTPIForField[iColumnForField].type) == TOK_STORAGE_DIRECT_SINGLE ||
					TokenStoreGetStorageType(pTPIForField[iColumnForField].type) == TOK_STORAGE_INDIRECT_SINGLE)
				{
					ErrorFilenamef(pchFileName, "objectpath %s doesn't point to an array, which is invalid for ARRAY_SET (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					estrDestroy(&pTempString);
					return false;
				}

				if (iIndexForField != -1)
				{
					ErrorFilenamef(pchFileName, "objectpath %s points to specific array member, which is invalid for ARRAY_SET (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					estrDestroy(&pTempString);
					return false;
				}

				arraySize = TokenStoreGetNumElems(pTPIForField, iColumnForField, pStructForField, NULL);

				eaDestroyEx(&pCurField->pArrayValues, NULL);

				for (arrayIndex = 0; arrayIndex < arraySize; arrayIndex++)
				{
					bResult = FieldWriteTextWFlags(pTPIForField,iColumnForField, pStructForField, arrayIndex, &pTempString, 0, 0, iExcludeFlags);

					if (!bResult)
					{
						ErrorFilenamef(pchFileName, "Couldn't FieldWriteText during StructInherit_UpdateFromStruct (field %s)", pCurField->pFieldName);
						estrDestroy(&pTempString);
						return false;
					}
					eaPush(&pCurField->pArrayValues, strdup(pTempString));

					estrClear(&pTempString);
				}

				break;
			}

		case OVERRIDE_ADD:
			{
				char *pEscapedString = NULL;
				bResult = objPathResolveField(pCurField->pFieldName, pTPI, pStruct, 
					&pTPIForField, &iColumnForField, &pStructForField, &iIndexForField, OBJPATHFLAG_CREATESTRUCTS);

				if (!bResult)
				{
					ErrorFilenamef(pchFileName, "Couldn't resolve obj path during StructInherit_UpdateFromStruct (field %s)", pCurField->pFieldName);
					estrDestroy(&pTempString);
					return false;
				}

				bResult = FieldWriteTextWFlags(pTPIForField,iColumnForField, pStructForField, iIndexForField, &pTempString, 0, 0, iExcludeFlags);
				
				if (!bResult)
				{
					ErrorFilenamef(pchFileName, "Couldn't FieldWriteText during StructInherit_UpdateFromStruct (field %s)", pCurField->pFieldName);
					estrDestroy(&pTempString);
					return false;
				}

				pEscapedString = malloc(estrLength(&pTempString) * 2 + 2);
				TokenizerEscape(pEscapedString, pTempString);

				free(pCurField->pNewValue);
				pCurField->pNewValue = strdup(pEscapedString);
				
				free(pEscapedString);

				estrClear(&pTempString);
			}
			break;	

		case OVERRIDE_REMOVE:
			bResult = objPathResolveField(pCurField->pFieldName, pTPI, pStruct, 
				&pTPIForField, &iColumnForField, &pStructForField, &iIndexForField, 0);

			if (!bResult)
			{
				InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Couldn't resolve objectpath %s while applying inheritance data (parent %s)",
					pCurField->pFieldName, pInheritanceData->pParentName);
				estrDestroy(&pTempString);
				return false;
			}

			if ((TOK_GET_TYPE(pTPIForField[iColumnForField].type) != TOK_STRUCT_X && TOK_GET_TYPE(pTPIForField[iColumnForField].type) != TOK_POLYMORPH_X))				
			{
				InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "objectpath %s points to something other than a struct, illegal for OVERRIDE_REMOVE (parent %s)",
					pCurField->pFieldName, pInheritanceData->pParentName);
				estrDestroy(&pTempString);
				return false;
			}
			if (TokenStoreGetStorageType(pTPIForField[iColumnForField].type) == TOK_STORAGE_INDIRECT_SINGLE)
			{
				FieldClear(pTPIForField,iColumnForField,pStructForField,iIndexForField);
			}
			else if (TokenStoreGetStorageType(pTPIForField[iColumnForField].type) == TOK_STORAGE_INDIRECT_SINGLE)
			{
				void ***pppEArray;

				if (iIndexForField < 0)
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Objectpath %s points to array member that does not exist, illegal for OVERRIDE_REMOVE (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					estrDestroy(&pTempString);
					return false;
				}
				pppEArray = TokenStoreGetEArray(pTPIForField, iColumnForField, pStructForField, NULL);

				if (!eaIndexedGetTable(pppEArray))
				{
					InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Objectpath %s points to array member in non-indexed earray, illegal for OVERRIDE_REMOVE (parent %s)",
						pCurField->pFieldName, pInheritanceData->pParentName);
					estrDestroy(&pTempString);
					return false;

				}

				eaRemove(pppEArray, iIndexForField);
			}
			else
			{
				InvalidDataErrorFilenamef(pInheritanceData->pCurrentFile, "Objectpath %s points to an embedded struct, illegal for OVERRIDE_REMOVE (parent %s)",
					pCurField->pFieldName, pInheritanceData->pParentName);
				estrDestroy(&pTempString);
				return false;
			}
			break;



		default:
			ErrorFilenamef(pchFileName, "Unknown/unsupported inheritance operation");
			estrDestroy(&pTempString);
			return false;
		}
	}

	estrDestroy(&pTempString);
	return true;
}


//turn an inheriting struct into a fully independent struct
void StructInherit_StopInheriting(ParseTable *pTPI, void *pStruct)
{
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);
	InheritanceData *pData;

	assertmsg(iInheritanceColumn >= 0, "Attempted to apply inheritance to table without inheritance data");

	pData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);

	if (pData)
	{
		StructDestroy(parse_InheritanceData, pData);

		TokenStoreSetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL, NULL);
	}
}

bool StructInherit_IsInheriting(ParseTable *pTPI, void *pStruct)
{
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);
	if (iInheritanceColumn < 0)
		return false;
	else if (TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL))
		return true;
	else
		return false;
}

void StructInherit_StartInheriting(ParseTable *pTPI, void *pStruct, const char *name, const char *pFileName)
{
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);
	InheritanceData *pData;

	assertmsg(iInheritanceColumn >= 0, "Attempted to apply inheritance to table without inheritance data");

	pData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);

	if (pData)
	{
		StructInherit_StopInheriting(pTPI, pStruct);
	}

	pData = StructCreate(parse_InheritanceData);
	pData->pParentName = StructAllocString(name);
	pData->pCurrentFile = allocAddString(pFileName);
	TokenStoreSetPointer(pTPI, iInheritanceColumn, pStruct, 0, pData, NULL);
}

bool StructInherit_IsSupported(ParseTable *pTPI)
{
	return StructInherit_GetInheritanceDataColumn(pTPI) != -1;
}

char *StructInherit_GetParentName(ParseTable *pTPI, void *pStruct)
{
	InheritanceData *data;
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);

	if (iInheritanceColumn < 0 || !pStruct)
	{
		return NULL;
	}

	data = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);

	if (data)
	{
		return data->pParentName;
	}
	return NULL;
}

void StructInherit_SetFileName(ParseTable *pTPI, void *pStruct, const char *pFileName)
{
	InheritanceData *data;
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);

	if (iInheritanceColumn < 0)
	{
		return;
	}

	data = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);

	if (data)
	{
		data->pCurrentFile = allocAddString(pFileName);		
	}	
}


void StructInherit_CreateAddStructOverride(ParseTable *pTPI, void *pStruct, char *pObjectPath)
{
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);
	InheritanceData *pData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);
	int iCurNumFields = eaSize(&pData->ppFieldData);
	int i;
	SingleFieldInheritanceData *pCurField;

	for (i=0; i < iCurNumFields; i++)
	{
		if (stricmp(pData->ppFieldData[i]->pFieldName, pObjectPath) == 0)
		{
			if (pData->ppFieldData[i]->eType != OVERRIDE_ADD)
			{
				ErrorFilenamef(ParserGetFilename(pTPI,pStruct), "Trying to add a new OVERRIDE_ADD where an incompatible overriding field already exists");
			}
			return;
		}
	}

	pCurField = StructCreate(parse_SingleFieldInheritanceData);
	pCurField->eType = OVERRIDE_ADD;
	pCurField->pFieldName = strdup(pObjectPath);
	pCurField->pNewValue = NULL;

	eaPush(&pData->ppFieldData, pCurField);
}

void StructInherit_CreateRemoveStructOverride(ParseTable *pTPI, void *pStruct, char *pObjectPath)
{
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);
	InheritanceData *pData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);
	int iCurNumFields = eaSize(&pData->ppFieldData);
	int i;
	SingleFieldInheritanceData *pCurField;

	for (i=0; i < iCurNumFields; i++)
	{
		if (stricmp(pData->ppFieldData[i]->pFieldName, pObjectPath) == 0)
		{
			if (pData->ppFieldData[i]->eType != OVERRIDE_REMOVE)
			{
				ErrorFilenamef(ParserGetFilename(pTPI,pStruct), "Trying to add a new OVERRIDE_REMOVE where an incompatible overriding field already exists");
			}
			return;
		}
	}

	pCurField = StructCreate(parse_SingleFieldInheritanceData);
	pCurField->eType = OVERRIDE_REMOVE;
	pCurField->pFieldName = strdup(pObjectPath);
	pCurField->pNewValue = NULL;

	eaPush(&pData->ppFieldData, pCurField);
}


void StructInherit_CreateFieldOverride(ParseTable *pTPI, void *pStruct, char *pObjectPath)
{
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);
	InheritanceData *pData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);
	int iCurNumFields = eaSize(&pData->ppFieldData);
	int i;
	SingleFieldInheritanceData *pCurField;

	for (i=0; i < iCurNumFields; i++)
	{
		if (stricmp(pData->ppFieldData[i]->pFieldName, pObjectPath) == 0)
		{
			if (pData->ppFieldData[i]->eType != OVERRIDE_SET)
			{
				ErrorFilenamef(ParserGetFilename(pTPI,pStruct), "Trying to add a new OVERRIDE_SET where an incompatible overriding field already exists");
			}
			return;
		}
	}

	pCurField = StructCreate(parse_SingleFieldInheritanceData);
	pCurField->eType = OVERRIDE_SET;
	pCurField->pFieldName = strdup(pObjectPath);

	eaPush(&pData->ppFieldData, pCurField);

}


void StructInherit_CreateArrayOverride(ParseTable *pTPI, void *pStruct, char *pObjectPath)
{
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);
	InheritanceData *pData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);
	int iCurNumFields = eaSize(&pData->ppFieldData);
	int i;
	SingleFieldInheritanceData *pCurField;

	for (i=0; i < iCurNumFields; i++)
	{
		if (stricmp(pData->ppFieldData[i]->pFieldName, pObjectPath) == 0)
		{
			if (pData->ppFieldData[i]->eType != OVERRIDE_ARRAY_SET)
			{
				ErrorFilenamef(ParserGetFilename(pTPI,pStruct), "Trying to add a new OVERRIDE_SET where an incompatible overriding field already exists");
			}
			return;
		}
	}

	pCurField = StructCreate(parse_SingleFieldInheritanceData);
	pCurField->eType = OVERRIDE_ARRAY_SET;
	pCurField->pFieldName = strdup(pObjectPath);

	eaPush(&pData->ppFieldData, pCurField);

}

enumOverrideType StructInherit_GetOverrideTypeEx(ParseTable *pTPI, void *pStruct, char *pobjectpathOfField, bool bCheckParentPaths)
{
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);
	InheritanceData *pData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);
	int iCurNumFields = eaSize(&pData->ppFieldData);
	int i;
	int iMaxMatchLen = 0;
	int iInputLen = bCheckParentPaths?(int)strlen(pobjectpathOfField):0;
	enumOverrideType parentType = OVERRIDE_NONE;

	for (i=0; i < iCurNumFields; i++)
	{
		if (stricmp(pData->ppFieldData[i]->pFieldName, pobjectpathOfField) == 0)
		{
			return pData->ppFieldData[i]->eType;
		}
		else if (bCheckParentPaths)
		{
			// Do a substring match to see if this override is a parent of the object path input
			int n = (int)strlen(pData->ppFieldData[i]->pFieldName);
			if (strnicmp(pData->ppFieldData[i]->pFieldName, pobjectpathOfField, n) == 0)
			{
				// Check that the substring represents a complete portion of the object path
				if (iInputLen >= n && (pobjectpathOfField[n] == '.' || pobjectpathOfField[n] == '\0' || pobjectpathOfField[n] == '[')){
					// Save the longest (most specific) match
					if (n > iMaxMatchLen){
						iMaxMatchLen = n;
						parentType = pData->ppFieldData[i]->eType;
					}
				}
			}
		}
	}

	if (bCheckParentPaths)
		return parentType;
	return OVERRIDE_NONE;
}



void StructInherit_DestroyOverride(ParseTable *pTPI, void *pStruct, char *pObjectPath)
{
	int iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);
	InheritanceData *pData = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct, 0, NULL);
	int iCurNumFields = eaSize(&pData->ppFieldData);
	int i;

	for (i=0; i < iCurNumFields; i++)
	{
		if (stricmp(pData->ppFieldData[i]->pFieldName, pObjectPath) == 0)
		{
			StructDestroy(parse_SingleFieldInheritanceData, pData->ppFieldData[i]);
			eaRemoveFast(&pData->ppFieldData, i);
			return;
		}
	}

	ErrorFilenamef(ParserGetFilename(pTPI,pStruct), "Didn't find overriding field %s to stop overriding", pObjectPath);
}

int StructInherit_CompareStructs(ParseTable *pTPI, void *pStruct1, void *pStruct2)
{
	int diff;
	int iInheritanceColumn;
	InheritanceData *pData1, *pData2;
	if (!StructInherit_IsSupported(pTPI))
	{
		return StructCompare(pTPI, pStruct1, pStruct2, 0, 0, 0);
	}
	iInheritanceColumn = StructInherit_GetInheritanceDataColumn(pTPI);

	assertmsg(iInheritanceColumn >= 0, "Attempted to apply inheritance to table without inheritance data");

	pData1 = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct1, 0, NULL);
	pData2 = TokenStoreGetPointer(pTPI, iInheritanceColumn, pStruct2, 0, NULL);
	if (pData1)
	{
		int column;
		if (!pData2)
		{
			return 1;
		}
		diff = stricmp(pData1->pParentName, pData2->pParentName);
		if (diff != 0) return diff;
		assert(ParserFindColumn(parse_InheritanceData, "FieldData", &column));

		return TokenCompare(parse_InheritanceData, column, pData1, pData2, 0, 0);
	}
	else
	{
		if (pData1)
		{
			return -1;
		}
		return StructCompare(pTPI, pStruct1, pStruct2, 0, 0, 0);
	}
}




AUTO_STRUCT;
typedef struct inhTestSubStruct
{
	int foo;
	int bar;
	char *pName; AST(KEY)
} inhTestSubStruct;


AUTO_STRUCT;
typedef struct inhTestStruct
{
	inhTestSubStruct **ppSubStructs;
	InheritanceData *pInheritance; 
	inhTestSubStruct *pOptionalStruct;
} inhTestStruct;


//AUTO_RUN;
void inhTest(void)
{
	SingleFieldInheritanceData inhSingleFieldData[] = 
	{
		{
			".SubStructs[sub1]", OVERRIDE_ADD, NULL,
		},
		{
			".OptionalStruct", OVERRIDE_REMOVE, NULL,
		},
	};

	InheritanceData inhData = { "dummy", NULL };

	inhTestStruct testStruct = { NULL, NULL, NULL };

	testStruct.pOptionalStruct = StructCreate(parse_inhTestSubStruct);


	inhSingleFieldData[0].pNewValue = strdup("<&{\nName \"sub1\"\nfoo 3\nbar 5\n}\n&>");

	testStruct.pInheritance = &inhData;

	eaPush(&inhData.ppFieldData, &inhSingleFieldData[0]);
	eaPush(&inhData.ppFieldData, &inhSingleFieldData[1]);

	StructInherit_ApplyToStruct(parse_inhTestStruct, &testStruct, NULL);

	testStruct.ppSubStructs[0]->bar = 30;

	StructInherit_UpdateFromStruct(parse_inhTestStruct, &testStruct, false);

	{
		int iBrk = 0;
	}
}


#include "autogen/textparserinheritance_c_ast.c"
#include "autogen/textparserinheritance_h_ast.c"

