
#include "textparserUtils.h"

#include "structInternals.h"
#include "tokenstore.h"
#include "structnet.h"
#include "strings_opt.h"
#include "memorypool.h"
#include "objPath.h"
#include "StringCache.h"
#include "MemoryBudget.h"
#include "ResourceInfo.h"
#include "stringutil.h"
#include "autogen/TextParserUtils_h_ast.h"
#include "zutils.h"
#include "HttpXpathSupport.h"
#include "CmdParse.h"
#include "wininclude.h"
#include "StringTable.h"
#include "serialize.h"
#include "tokenstore_inline.h"
#include "textparserUtils_inline.h"
#include "ThreadSafeMemoryPool.h"
#include "StructInit.h"
#include "structinternals_h_ast.h"
#include "mathUtil.h"
#include "autogen/GlobalTypes_h_ast.h"

void ParserInitIndexedCompareCache(ParseTable *pTable);

extern CRITICAL_SECTION gTextParserGlobalLock;

#define SETERROR(msg, ...) if (resultString && !(iObjPathFlags & OBJPATHFLAG_WRITERESULTIFNOBROADCAST)) estrPrintf(resultString, msg, ##__VA_ARGS__);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping(FAKEFILENAME_PARSETABLEINFOS, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping(FAKEFILENAME_COMPILEDOBJPATHS, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping(FAKEFILENAME_TESTTEXTPARSER, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping(TEMPORARYTPI_MEMORY_NAME, BUDGET_EngineMisc););

bool TokenToSimpleString(ParseTable tpi[], int column, const void* structptr, char* str, int str_size, WriteTextFlags iWriteTextFlags)
{
	char *temp = NULL;
	estrStackCreateSize(&temp,str_size);
	if (writestring_autogen(tpi, column, (void*)structptr, 0, &temp, iWriteTextFlags, 0, 0, __FILE__, __LINE__))
	{
		strcpy_s(str,str_size,temp);
		estrDestroy(&temp);
		return true;
	}
	estrDestroy(&temp);
	return false;
}

bool FieldToSimpleString(ParseTable tpi[], int column, const void* structptr, int index, char* str, int str_size, WriteTextFlags iWriteTextFlags)
{
	char *temp = NULL;
	if (TYPE_INFO(tpi[column].type).writestring)
	{	
		estrStackCreateSize(&temp,str_size);
		if (TYPE_INFO(tpi[column].type).writestring(tpi, column, (void*)structptr, index, &temp, iWriteTextFlags, 0, 0, __FILE__, __LINE__))
		{
			strcpy_s(str,str_size,temp);
			estrDestroy(&temp);
			return true;
		}
		estrDestroy(&temp);
		return false;
	}
	return false;
}

bool TokenWriteText_dbg(ParseTable tpi[], int column, const void* structptr, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	return writestring_autogen(tpi, column, (void*)structptr, 0, estr, iWriteTextFlags, 0, 0, caller_fname, line);	
}

bool FieldWriteText_dbg(ParseTable tpi[], int column, const void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	if (TYPE_INFO(tpi[column].type).writestring)
		return TYPE_INFO(tpi[column].type).writestring(tpi, column, (void*)structptr, index, estr, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, caller_fname, line);
	return false;
}

bool TokenFromSimpleString(ParseTable tpi[], int column, void* structptr, const char* str)
{
	return readstring_autogen(tpi, column, structptr, 0, str, 0);
}

bool FieldFromSimpleString(ParseTable tpi[], int column, void* structptr, int index, const char* str)
{	
	if (TYPE_INFO(tpi[column].type).readstring)
		return TYPE_INFO(tpi[column].type).readstring(tpi, column, structptr, index, str, 0);
	return false;
}

bool FieldToMultiVal(ParseTable tpi[], int column, const void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL)
{
	PERFINFO_AUTO_START_FUNC();
	if (TYPE_INFO(tpi[column].type).tomulti)
	{
		bool answer = TYPE_INFO(tpi[column].type).tomulti(tpi, column, (void*)structptr, index, result, bDuplicateData, dummyOnNULL);
		PERFINFO_AUTO_STOP();
		return answer;
	}
	PERFINFO_AUTO_STOP();
	return false;
}

bool FieldFromMultiVal(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value)
{
	if (TYPE_INFO(tpi[column].type).frommulti)
		return TYPE_INFO(tpi[column].type).frommulti(tpi, column, structptr, index, value);
	return false;
}

bool TokenToMultiVal(ParseTable tpi[], int column, const void* structptr, MultiVal* result)
{
	return tomulti_autogen(tpi, column, (void*)structptr, 0, result, true, false);
}

bool TokenFromMultiVal(ParseTable tpi[], int column, void* structptr, const MultiVal* value)
{
	return frommulti_autogen(tpi, column, structptr, 0, value);
}

void TokenCopy(ParseTable tpi[], int column, void *dstStruct, void *srcStruct, bool bCopyAll)
{
	ParseTable *ptcc = &tpi[column];
	destroystruct_autogen(tpi, ptcc, column, dstStruct, 0);
	if (bCopyAll && TOK_HAS_SUBTABLE(ptcc->type))
	{
		copystruct_autogen(tpi, ptcc, column, dstStruct, srcStruct, 0, NULL, NULL);
	}
	else
	{
		copyfield_autogen(tpi, ptcc, column, dstStruct, srcStruct, 0, NULL, NULL, 0, 0);
	}
}

void TokenClear(ParseTable tpi[], int column, void *dstStruct)
{
	destroystruct_autogen(tpi, &tpi[column], column, dstStruct, 0);
	initstruct_autogen(tpi, column, dstStruct, 0);
}

void FieldCopy(ParseTable tpi[], int column, void* dstStruct, void *srcStruct,int index,  StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	copyfield_autogen(tpi, &tpi[column], column, dstStruct, srcStruct, index, NULL, NULL, iOptionFlagsToMatch, iOptionFlagsToExclude);
}

void FieldClear(ParseTable tpi[], int column, void* dstStruct, int index)
{
	destroystruct_autogen(tpi, &tpi[column], column, dstStruct, index);
	initstruct_autogen(tpi, column, dstStruct, index);
}


// only works for primitive token types, but should work for all object ids
bool FieldIsDefault(ParseTable tpi[], int column, void* structptr, int index)
{
	ParseInfoFieldUsage paramusage = TYPE_INFO(tpi[column].type).interpretfield(tpi, column, ParamField);

	// old switch style, extend if more than just primitive types are needed..
	switch (TOK_GET_TYPE(tpi[column].type))
	{
	xcase TOK_U8_X: 
		return TokenStoreGetU8(tpi, column, structptr, index, NULL) == ((paramusage == DefaultValue)? tpi[column].param: 0);
	xcase TOK_INT16_X:
		return TokenStoreGetInt16_inline(tpi, &tpi[column], column, structptr, index, NULL) == ((paramusage == DefaultValue)? tpi[column].param: 0);
	xcase TOK_INT_X:
		return TokenStoreGetInt_inline(tpi, &tpi[column], column, structptr, index, NULL) == ((paramusage == DefaultValue)? tpi[column].param: 0);
	xcase TOK_INT64_X:
		return TokenStoreGetInt64_inline(tpi, &tpi[column], column, structptr, index, NULL) == ((paramusage == DefaultValue)? tpi[column].param: 0);
	xcase TOK_F32_X:
		return TokenStoreGetF32_inline(tpi, &tpi[column], column, structptr, index, NULL) == ((paramusage == DefaultValue)? GET_FLOAT_FROM_INTPTR(tpi[column].param): 0);
	xcase TOK_BIT:
		{
			U32 iDefValue = 0;

			if (tpi[column].type & TOK_SPECIAL_DEFAULT)
			{
				char *pDefaultString = NULL;
				GetStringFromTPIFormatString(&tpi[column], "SPECIAL_DEFAULT", &pDefaultString);
				iDefValue = atoi(pDefaultString);
			}

			return TokenStoreGetBit_inline(tpi, &tpi[column], column, structptr, index, NULL) == iDefValue;
		}
	xcase TOK_STRING_X:
		{
			if (tpi[column].type & TOK_SPECIAL_DEFAULT)
			{
				char *pDefaultString = NULL;
				const char *string = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, NULL);

				GetStringFromTPIFormatString(&tpi[column], "SPECIAL_DEFAULT", &pDefaultString);

				return stricmp_safe(pDefaultString, string) == 0;
			}
			else
			{
				char* defaultstring = ((paramusage == PointerToDefaultString)? (char*)tpi[column].param: 0);
				const char* string = TokenStoreGetString_inline(tpi, &tpi[column], column, structptr, index, NULL);
				return stricmp_safe(string, defaultstring) == 0;
			}
		}
	}
	return false; // any other token type
}

bool FieldDeterminePolyType(ParseTable tpi[], int column, void* structptr, int index, int* polycol)
{
	ParseTable* polytable = tpi[column].subtable;
	void* substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);

	assert(TOK_GET_TYPE(tpi[column].type) == TOK_POLYMORPH_X);
	if (!substruct) return false;

	return StructDeterminePolyType(polytable, substruct, polycol);
}

bool StructDeterminePolyType(ParseTable* polytable, void* structptr, int* polycol)
{
	int i;

	// look for a tpi whose key field matches the field given here
	FORALL_PARSETABLE(polytable, i)
	{
		ParseTable* subtable = polytable[i].subtable;
		int typecol = -1;
		U32 iType = TOK_GET_TYPE(polytable[i].type);

		if (iType == TOK_IGNORE)
		{
			continue;
		}


		assert(TOK_GET_TYPE(polytable[i].type) == TOK_STRUCT_X);	// poly table must be simple list of structs
		assert((typecol = ParserGetTableObjectTypeColumn(subtable)) >= 0);	
				// every struct must have an object type column

		if (FieldIsDefault(subtable, typecol, structptr, 0))
		{
			*polycol = i;
			return true;
		}
	}
	return false;
}

ParseTable* PolyStructDetermineParseTable(ParseTable* polytable, void* structptr)
{
	int polycol;
	if (StructDeterminePolyType(polytable, structptr, &polycol))
		return polytable[polycol].subtable;
	else
		return NULL;
}

bool PolyTableContains(ParseTable* polytable, ParseTable *table)
{
	S32 i;
	FORALL_PARSETABLE(polytable, i)
	{
		if (devassertmsg(polytable[i].type == TOK_STRUCT_X, "Non-polytable parse table given to PolyTableContains"))
			if (polytable[i].subtable == table)
				return true;
	}
	return false;
}

// get a subtable based on column and index, optionally get the subtable and size fields for the subtable
// - this encapsulates the operations necessary for dealing with a polymorphic subtable
void* StructGetSubtable(ParseTable tpi[], int column, void* structptr, int index, ParseTable** subtable, size_t* subsize)
{
	if (TOK_GET_TYPE(tpi[column].type) == TOK_STRUCT_X)
	{
		if (subtable) *subtable = tpi[column].subtable;
		if (subsize) *subsize = tpi[column].param;

		if (!structptr)
		{
			return NULL;
		}
		else
		{
			return TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
		}
	}
	else if (TOK_GET_TYPE(tpi[column].type) == TOK_POLYMORPH_X)
	{
		void* substruct;
		ParseTable* polytable;
		int polycol = -1;

		if (!structptr)
		{
			if (subtable)
			{
				*subtable = NULL;
			}

			return NULL;
		}

		substruct = TokenStoreGetPointer_inline(tpi, &tpi[column], column, structptr, index, NULL);
		if (!substruct)
		{
			return NULL;
		}
		polytable = tpi[column].subtable;
		assert(StructDeterminePolyType(polytable, substruct, &polycol));
		if (subtable) *subtable = polytable[polycol].subtable;
		if (subsize) *subsize = polytable[polycol].param;
		return substruct;
	}
	else
		assertmsgf(0, "%s may only be called with a struct or polymorph token", __FUNCTION__);
	return NULL;
}

// copy all substructs in srcStruct (in this column) to dstStruct
void TokenAddSubStruct(ParseTable tpi[], int column, void *dstStruct, void *srcStruct, bool copyAll)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, (void*)srcStruct, NULL);
	int destIndex = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, (void*)dstStruct, NULL);

	for (i = 0; i < numelems; i++)
	{
		ParseTable* subtable;
		size_t subsize;
		void* fromstruct = StructGetSubtable(tpi, column, srcStruct, i, &subtable, &subsize);
		void* tostruct = TokenStoreAlloc(tpi, column, dstStruct, destIndex++, (U32)subsize, NULL, NULL, NULL);
		if (copyAll)
			StructCompress(subtable, fromstruct, tostruct, NULL, NULL);
		else
			StructCopyFieldsVoid(subtable, fromstruct, tostruct, 0, 0);
	}
}

// looks at keys to merge lists of substructs.  if key is in src, but not dst, adds to end of list
// if overwriteSame is set, then any structs that exist in both dst & src will be overwritten in dst with copies from src
void TokenSubStructKeyMerge(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, bool overwriteSame, bool copyAll)
{
	char *key = NULL;
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, srcStruct, NULL);

	for (i = 0; i < numelems; i++)
	{
		ParseTable* subtable;
		size_t subsize;
		int target_index = -1;
		void* fromstruct = StructGetSubtable(tpi, column, srcStruct, i, &subtable, &subsize);

		if (!key)
			estrStackCreateSize(&key, MIN_STACK_ESTR);
		estrClear(&key);
		if (!objGetKeyEString(subtable, fromstruct, &key)) continue;

		// if found in dst
		if (ParserResolveKey(key, tpi, column, dstStruct, &target_index, 0, NULL, NULL) && target_index >= 0)
		{
			if (overwriteSame)
			{
				ParseTable* to_subtable;
				size_t to_subsize;
				void* tostruct = StructGetSubtable(tpi, column, dstStruct, target_index, &to_subtable, &to_subsize);

				if (to_subsize == subsize)
				{
					StructDeInitVoid(to_subtable, tostruct);
					if (copyAll)
						StructCompress(subtable, fromstruct, tostruct, NULL, NULL);
					else
						StructCopyFieldsVoid(subtable, fromstruct, tostruct, 0, 0);
					// still same pointer, so earray unaffected
				}
				else // otherwise, guaranteed to be a pointer, not embedded
				{
					StructDestroyVoid(to_subtable, tostruct);
					tostruct = StructAllocVoid(subtable); // should be new size
					if (copyAll)
						StructCompress(subtable, fromstruct, tostruct, NULL, NULL);
					else
						StructCopyFieldsVoid(subtable, fromstruct, tostruct, 0, 0);
					TokenStoreSetPointer_inline(tpi, &tpi[column], column, dstStruct, target_index, tostruct, NULL);
				}
			}
		}
		else // not found in dst
		{
			void ***pea = TokenStoreGetEArray_inline(tpi, &tpi[column], column, dstStruct, NULL);
			void* tostruct = StructCreateVoid(subtable);
			if (copyAll)
				StructCompress(subtable, fromstruct, tostruct, NULL, NULL);
			else
				StructCopyFieldsVoid(subtable, fromstruct, tostruct, 0, 0);
			eaPush(pea, tostruct);
		}
	}
	estrDestroy(&key);
}

// Uses the TPI to copy values from a source struct to a target struct, only copying
// non-defaults, so that you can override a parsed structure with another (to improve reuse)
// If addsubstructs is set, full substructures in the srcStruct may be added to the list of 
// substructs in dstStruct
// If keyMerge is set, then substructs are added or merged based on their key values
// If copyAll is set, StructCopyAll/StructCompress is used, otherwise StructCopyFields is used.
void StructOverride(ParseTable pti[], void *dstStruct, void *srcStruct, int addSubStructs, bool keyMerge, bool copyAll)
{
	int i, foundfield = 0;
	U32* usedBitField = NULL;

	// First, make sure they have the parambitfield token
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_USEDFIELD )
		{
			usedBitField = TokenStoreGetPointer_inline(pti, &pti[i], i, srcStruct, 0, NULL);
			foundfield = 1;
			break;
		}
	}

	if (!foundfield) // it may not be an error for usedBitField = NULL here (no tokens specified)
	{
		assertmsg(0, "Error: attempting to use StructOverride without a TOK_USEDFIELD bitfield");
		return;
	}

	if (!usedBitField)
		return;

	FORALL_PARSETABLE(pti, i)
	{
		// earrays of substructs actually get ADDED to the target
		// but ignore redundant earray names, otherwise we end up copying
		// everything to the dest earray twice.
		if (ParserColumnIsIndexedEArray(pti, i, NULL) && keyMerge && TSTB(usedBitField, i))
		{
			TokenSubStructKeyMerge(pti, i, dstStruct, srcStruct, true, copyAll);
		}
		else if (addSubStructs && TOK_HAS_SUBTABLE(pti[i].type) && pti[i].type & TOK_EARRAY && !(pti[i].type & TOK_REDUNDANTNAME) && TSTB(usedBitField, i))
		{
			TokenAddSubStruct(pti, i, dstStruct, srcStruct, copyAll);
		}
		else if (pti[i].type & TOK_USEDFIELD ||
			TOK_GET_TYPE(pti[i].type) == TOK_CURRENTFILE_X ||
			TOK_GET_TYPE(pti[i].type) == TOK_LINENUM_X ||
			TOK_GET_TYPE(pti[i].type) == TOK_TIMESTAMP_X)
		{
			// Do *not* copy the USEDFIELD bits, so that when we reload the
			//  default structure, we can re-apply it to this structure and
			//  get the same result as the initial application.
			// Also do not copy over auto-generated values (currentfile)!
		}
		else if ( usedBitField && TSTB(usedBitField, i) )
		{
			TokenCopy(pti, i, dstStruct, srcStruct, copyAll);
		}
	}
}

// Any fields not specified in dstStruct will be overridden with defaultStruct
// Can use this to have a data-defined "default" and call this once per struct after load to fill in the defaults
// that were not overridden
// addSubStructs - substructs in defaultStruct will be ADDED to the list of structs in dest
// applySubStructFields - function will recurse so that individual fields in substructures will also use defaults, instead of only applying on a whole-struct level
// If keyMerge is set, then substructs are added or merged based on their key values
void StructApplyDefaults(ParseTable pti[], void *dstStruct, void *defaultStruct, int addSubStructs, int applySubStructFields, bool keyMerge)
{
	int i, foundfield = 0;
	U32* usedBitField = NULL;

	if (!defaultStruct)
		return;

	PERFINFO_AUTO_START_FUNC();

	// First, make sure they have the parambitfield token
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_USEDFIELD )
		{
			usedBitField = TokenStoreGetPointer_inline(pti, &pti[i], i, dstStruct, 0, NULL);
			foundfield = 1;
			break;
		}
	}

	if (!foundfield) // it may not be an error for usedBitField = NULL here (no tokens specified)
	{
		assertmsg(0, "Error: attempting to use ParserReverseOverrideStruct without a TOK_USEDFIELD bitfield");
		PERFINFO_AUTO_STOP();
		return;
	}

	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & (TOK_REDUNDANTNAME  | TOK_IGNORE))
			continue;
		// earrays of substructs actually get ADDED to the target
		else if (addSubStructs && TOK_HAS_SUBTABLE(pti[i].type) && pti[i].type & TOK_EARRAY)
		{
			TokenAddSubStruct(pti, i, dstStruct, defaultStruct, true);
		}
		else if (keyMerge && TOK_HAS_SUBTABLE(pti[i].type) && pti[i].type & TOK_EARRAY)
		{
			TokenSubStructKeyMerge(pti, i, dstStruct, defaultStruct, false, true);
		}
		else if (pti[i].type & TOK_USEDFIELD ||
			TOK_GET_TYPE(pti[i].type) == TOK_CURRENTFILE_X ||
			TOK_GET_TYPE(pti[i].type) == TOK_LINENUM_X ||
			TOK_GET_TYPE(pti[i].type) == TOK_TIMESTAMP_X)
		{
			// Do *not* copy the USEDFIELD bits, so that when we reload the
			//  default structure, we can re-apply it to this structure and
			//  get the same result as the initial application.
			// Also do not copy over auto-generated values (currentfile)!
		}
		else if (usedBitField && !TSTB(usedBitField, i))
		{
			TokenCopy(pti, i, dstStruct, defaultStruct, false);
		}
		else if (TOK_GET_TYPE(pti[i].type) == TOK_STRUCT_X && applySubStructFields) // past copy, so this structure exists in destination
			// (doesn't apply to polymorphic objects because applying their fields to differently typed structs wouldn't make sense)
		{
			int defaultNumElems = TokenStoreGetNumElems_inline(pti, &pti[i], i, defaultStruct, NULL);
			int destNumElems = TokenStoreGetNumElems_inline(pti, &pti[i], i, dstStruct, NULL);
			int j;
			if (destNumElems) // If this is zero, how was the bitfield set that it was used?!  Maybe a fixup func removed it?
			{
				if (defaultNumElems) {
					void *defaultSubStruct = TokenStoreGetPointer_inline(pti, &pti[i], i, defaultStruct, 0, NULL);
					if (defaultNumElems!=1) {
						Errorf("Found default with more than one \"%s\" specified, this will be ignored.", pti[i].name);
					}
					for (j=0; j<destNumElems; j++) {
						void* dstSubStruct = TokenStoreGetPointer_inline(pti, &pti[i], i, dstStruct, j, NULL);
						StructApplyDefaults(pti[i].subtable, dstSubStruct, defaultSubStruct, addSubStructs, applySubStructFields, keyMerge);
					}
				} 
				//else {
				//	Errorf("Default struct does not have a value for \"%s\"", pti[i].name);
				//}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

const char *ParserColumnGetDictionaryName(ParseTable pti[], int i)
{
	int usage = TYPE_INFO(pti[i].type).interpretfield(pti, i, SubtableField);
	if (usage == PointerToDictionaryName)
	{
		return pti[i].subtable;
	}
	return NULL;
}

int ParserGetUsedBitFieldIndex(ParseTable pti[])
{
	int iBitFieldIndex;
	FORALL_PARSETABLE(pti, iBitFieldIndex)
		{
		if ( pti[iBitFieldIndex].type & TOK_USEDFIELD )
		{
			return iBitFieldIndex;
		}
	}

	return -1;
}

// note that the field name case sensitive and is the parseTable name, not necessarily the struct member logical name 
int TokenIsSpecifiedByName(ParseTable pti[], void* srcStruct, const char *pchField)
{
	S32 i;
	int usedFieldIndex = ParserGetUsedBitFieldIndex(pti);
	if (usedFieldIndex == -1)
		return false;
	if (!srcStruct)
		return false;

	FORALL_PARSETABLE(pti, i)
	{
		if(pti[i].type & TOK_PARSETABLE_INFO)
			continue;

		if (!strcmpi(pti[i].name, pchField))
		{
			return TokenIsSpecifiedEx(pti, i, srcStruct, usedFieldIndex, 0);
		}
	}

	return 0;
}


// iBitFieldIndex is the index of the TOK_USEDFIELD, to prevent a linear search every time you use this if you know it...
// if you don't care, pass -1 and it will be ignored
bool TokenIsSpecifiedEx(ParseTable pti[], int column, void* srcStruct, int iBitFieldIndex, bool bReturnThisIfNoUsedFieldExists)
{
	U32* usedBitField = NULL;

	if (!srcStruct)
		return false;

	if(iBitFieldIndex<0)
		iBitFieldIndex = ParserGetUsedBitFieldIndex(pti);

	// First, make sure they have the parambitfield token
	if ( iBitFieldIndex >= 0 )
	{
		if ( !(pti[iBitFieldIndex].type & TOK_USEDFIELD) )
		{
			Errorf("BitFieldIndex specified in TokenIsSpecified is incorrect.");
			return false;
		}
		else
		{
			usedBitField = TokenStoreGetPointer_inline(pti, &pti[iBitFieldIndex], iBitFieldIndex, srcStruct, 0, NULL);
			if ( !usedBitField ) // If the bitfield is null, no tokens specified
				return false;
		}
	}

	if (!usedBitField)
		return bReturnThisIfNoUsedFieldExists;

	return TSTB(usedBitField, column);
}



// 

// iBitFieldIndex is the index of the TOK_USEDFIELD, to prevent a linear search every time you use this if you know it...
// if you don't care, pass -1 and it will be ignored
void TokenSetSpecified(ParseTable tpi[], int column, void* srcStruct, int iBitFieldIndex, bool bOn)
{
	U32* usedBitField = NULL;
	bool found = false;

	// First, make sure they have the parambitfield token
	if (iBitFieldIndex >= 0)
	{
		if ( tpi[iBitFieldIndex].type & TOK_USEDFIELD )
		{
			usedBitField = TokenStoreGetPointer_inline(tpi, &tpi[iBitFieldIndex], iBitFieldIndex, srcStruct, 0, NULL);
			found = true;
		}
	}
	else
	{
		FORALL_PARSETABLE(tpi, iBitFieldIndex)
		{
			if ( tpi[iBitFieldIndex].type & TOK_USEDFIELD )
			{
				usedBitField = TokenStoreGetPointer_inline(tpi, &tpi[iBitFieldIndex], iBitFieldIndex, srcStruct, 0, NULL);
				found = true;
				break;
			}
		}
	}

	assertmsg(found, "TokenSetSpecified used on a parsetable with no USEDFIELD");

	if (!usedBitField && bOn)
	{
		// We need this in case someone made the object with StructCreate instead of actually parsing it.
		TokenStoreAddUsedField(tpi, iBitFieldIndex, srcStruct);
		usedBitField = TokenStoreGetPointer_inline(tpi, &tpi[iBitFieldIndex], iBitFieldIndex, srcStruct, 0, NULL);
		assertmsg(usedBitField, "Didn't allocate a used bitfield even after we tried");
	}

	if (bOn)
		SETB(usedBitField, column);
	else if (usedBitField)
		CLRB(usedBitField, column);
}


void TokenSetSpecifiedIfPossible(ParseTable tpi[], int column, void* srcStruct, bool bOn)
{
	int iUsedFieldColumn = ParserGetUsedBitFieldIndex(tpi);

	if (iUsedFieldColumn >= 0)
	{
		TokenSetSpecified(tpi, column, srcStruct, iUsedFieldColumn, bOn);
	}
}



void TokenInterpolate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, F32 interpParam)
{
	interp_autogen(tpi, column, structA, structB, destStruct, 0, interpParam);
}

void StructInterpolate(ParseTable pti[], void* structA, void* structB, void* destStruct, F32 interpParam)
{
	int i;
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		interp_autogen(pti, i, structA, structB, destStruct, 0, interpParam);
	}
}

void TokenCalcRate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, F32 deltaTime )
{
	calcrate_autogen(tpi, column, structA, structB, destStruct, 0, deltaTime);
}

void StructCalcRate(ParseTable pti[], void* structA, void* structB, void* destStruct, F32 deltaTime )
{
	int i;
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		calcrate_autogen(pti, i, structA, structB, destStruct, 0, deltaTime);
	}
}

void TokenIntegrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, F32 deltaTime )
{
	integrate_autogen(tpi, column, valueStruct, rateStruct, destStruct, 0, deltaTime);
}

void StructIntegrate(ParseTable pti[], void* valueStruct, void* rateStruct, void* destStruct, F32 deltaTime )
{
	int i;
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		integrate_autogen(pti, i, valueStruct, rateStruct, destStruct, 0, deltaTime);
	}
}

void TokenCalcCyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, F32 fStartTime, F32 deltaTime )
{
	calccyclic_autogen(tpi, column, valueStruct, ampStruct, freqStruct, cycleStruct, destStruct, 0, fStartTime, deltaTime);
}

void StructCalcCyclic(ParseTable pti[], void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, F32 fStartTime, F32 deltaTime )
{
	int i;
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		calccyclic_autogen(pti, i, valueStruct, ampStruct, freqStruct, cycleStruct, destStruct, 0, fStartTime, deltaTime);
	}
}

void FieldApplyDynOp(ParseTable tpi[], int column, int index, DynOpType optype, F32* values, U8 uiValuesSpecd, void* dstStruct, void* srcStruct, U32* seed)
{
	applydynop_autogen(tpi, column, dstStruct, srcStruct, index, optype, values, uiValuesSpecd, seed);
}

void StructApplyDynOp(ParseTable pti[], DynOpType optype, const F32* values, U8 uiValuesSpecd, void* dstStruct, void* srcStruct, U32* seed)
{
	int i;
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		applydynop_autogen(pti, i, dstStruct, srcStruct, 0, optype, values, uiValuesSpecd, seed);
	}
}

void SafeCalculateOffset(ParseTable pti[], int iColumn, size_t *size)
{
	size_t iOldOffset = pti[iColumn].storeoffset;
	calcoffset_autogen(pti, iColumn, size);
	assert(iOldOffset == 0 || iOldOffset == pti[iColumn].storeoffset);
}

int CalcOffsetsSubtable(ParseTable pti[], StashTable subSizes)
{
	int i;
	int size = 0;
	size_t fullsize = 0;


	// deal with circular refs & common subrefs
	if (stashAddressFindInt(subSizes, pti, &size)) return size; // we've already done this subtable
	stashAddressAddInt(subSizes, pti, 0, 0); // zero for now, necessary to avoid embedded recursion madness

	// MAK - deleting this weird strategy for redundantnames..  redundant names must be 
	// fixed up outside calcoffsets

	// now deal with all normal fields
	FORALL_PARSETABLE(pti, i)
	{
		// redundant structs may have different parse tables that still need to get evaluated
		if (TOK_HAS_SUBTABLE(pti[i].type))
		{
			int storage = TokenStoreGetStorageType(pti[i].type);
			int subsize = CalcOffsetsSubtable(pti[i].subtable, subSizes);
			if (storage == TOK_STORAGE_DIRECT_SINGLE)
			{
				pti[i].param = subsize;
				if (!subsize)
				{
					devassertmsg(0, "Unknown subtable size in an embedded struct because of recursion.  "
						"This is possibly fixable with more passes, but you may just want to fix the parse definition.");
				}
			}
			else
				pti[i].param = 0;
		}
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
	
		SafeCalculateOffset(pti, i, &fullsize);
	}
	size = RoundUpToGranularity((int)fullsize, sizeof(void*)); 
	//sizes of structs should always be rounded up to nearest multiple of pointer size


	stashAddressAddInt(subSizes, pti, size, 1); // now I finally know my size



	return size;
}

void FixupSubtableSizes(ParseTable pti[], StashTable subSizes, StashTable noRecurseTable)
{
	int i, subsize;

	bool bRoot = false;
	if (!noRecurseTable)
	{
		bRoot = true;
		noRecurseTable = stashTableCreateAddress(16);
	}
	else
	{
		if (stashFindPointer(noRecurseTable, pti, NULL))
		{
			return;
		}
	}

	stashAddPointer(noRecurseTable, pti, NULL, false);


	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_HAS_SUBTABLE(pti[i].type))
		{
			assert(stashAddressFindInt(subSizes, pti[i].subtable, &subsize));
			pti[i].param = subsize;
			if (TOK_SHOULD_RECURSE_PARSEINFO(pti, i))
			{			
				FixupSubtableSizes(pti[i].subtable, subSizes, noRecurseTable);
			}
		}
	}

	if (bRoot)
	{
		stashTableDestroy(noRecurseTable);
	}

}

// does not fixup offsets for redundant names, you need another method for that
int ParseInfoCalcOffsets(ParseTable pti[], bool bNoRecurse)
{

	if (bNoRecurse)
	{
		int i;
		int size;
		size_t fullsize = 0;

		FORALL_PARSETABLE(pti, i)
		{
			if (pti[i].type & TOK_REDUNDANTNAME) continue;
			SafeCalculateOffset(pti, i, &fullsize);
		}
		size = RoundUpToGranularity((int)fullsize, sizeof(void*)); 

		return size;
	}
	else
	{
		StashTable	subSizes = stashTableCreateAddress(10);
		int size = CalcOffsetsSubtable(pti, subSizes);
		FixupSubtableSizes(pti, subSizes, NULL); // need to recurse again to deal with recursive tables
		stashTableDestroy(subSizes);
		return size;
	}
}

ParseTable* ParseInfoAlloc(int numentries);
void ParseInfoFree(ParseTable* pti);

// Tokenizer tables get converted into data format (mostly using FUNCTIONCALL fields) and then serialized

// Used to indicate we have a static define table, but not a valid one
StaticDefineInt NullStaticDefineList[] =
{
	DEFINE_INT
	DEFINE_END
};

StaticDefineInt StaticDefineTypeList[] =
{
	DEFINE_INT
	{ "IntDefine",		DM_INT },
	{ "StringDefine",	DM_STRING },
	DEFINE_END
};

typedef struct ParseInfoColumn
{
	StructFunctionCall** options;
} ParseInfoColumn;

typedef struct ParseInfoTable
{
	char* name;
	ParseInfoColumn** columns;
} ParseInfoTable;

typedef struct StaticDefineEntry
{
	char *name;
	char *value;
} StaticDefineEntry;

typedef struct StaticDefineListInfo
{
	char* name;
	int type;
	StaticDefineEntry **entries;
} StaticDefineListInfo;

typedef struct ParseInfoDescriptor
{
	int schemaVersion;
	StaticDefineListInfo** defines;
	ParseInfoTable** tables;

	// These aren't sent, but are for convenience
	StashTable subDefines;
	StashTable subNames; 
	StashTable subAddresses;
} ParseInfoDescriptor;

ParseTable tpiParseInfoColumn[] = 
{
	{ "",	TOK_STRUCTPARAM | TOK_FUNCTIONCALL(ParseInfoColumn, options), },
	{ "\n", TOK_END },
	{ 0 },
};

ParseTable tpiParseInfoTable[] = 
{
	{ "",	TOK_STRUCTPARAM | TOK_STRING(ParseInfoTable, name, 0)	},
	{ "{",			TOK_START },
	{ "}",			TOK_END },
	{ "Column",		TOK_STRUCT(ParseInfoTable, columns, tpiParseInfoColumn)	},
	{ 0 },
};

ParseTable tpiStaticDefineEntry[] = 
{
	{ "",	TOK_STRUCTPARAM | TOK_STRING(StaticDefineEntry, name, 0)	},
	{ "",	TOK_STRUCTPARAM | TOK_STRING(StaticDefineEntry, value, 0)	},
	{ "\n", TOK_END },
	{ 0 },
};

ParseTable tpiStaticDefineListInfo[] = 
{
	{ "",	TOK_STRUCTPARAM | TOK_STRING(StaticDefineListInfo, name, 0)	},
	{ "{",			TOK_START },
	{ "}",			TOK_END },
	{ "Type",		TOK_INT(StaticDefineListInfo, type, 0), StaticDefineTypeList	},
	{ "Define",		TOK_STRUCT(StaticDefineListInfo, entries, tpiStaticDefineEntry)	},
	{ 0 },
};

ParseTable tpiParseInfoDescriptor[] =
{
	{ "SchemaVersion", TOK_INT(ParseInfoDescriptor, schemaVersion, 0) },
	{ "StaticDefineList",	TOK_STRUCT(ParseInfoDescriptor, defines, tpiStaticDefineListInfo) },
	{ "ParseInfo",	TOK_STRUCT(ParseInfoDescriptor, tables, tpiParseInfoTable) },
	{ 0 },
};
#define TYPE_tpiParseInfoDescriptor ParseInfoDescriptor

AUTO_RUN;
void initParseInfoDescriptor(void)
{
	ParserSetTableInfoRecurse(tpiParseInfoDescriptor, sizeof(ParseInfoDescriptor), "ParseInfoDescriptor", NULL, __FILE__, NULL, SETTABLEINFO_NAME_STATIC | SETTABLEINFO_ALLOW_CRC_CACHING);
}

typedef struct StructTypeOptionName {
	char* name;
	StructTypeField bit;
} StructTypeOptionName;

// type bits are 64-bit, so we can't use the standard StaticDefineInt array
// Try to keep this in the same order as textparser.h, to notice missing entries
static StructTypeOptionName TypeOptionNames[] =
{
	{"EARRAY",				TOK_EARRAY},
	{"FIXED_ARRAY",			TOK_FIXED_ARRAY},
	{"INDIRECT",			TOK_INDIRECT},

	{"POOL_STRING",			TOK_POOL_STRING},
	{"ESTRING",				TOK_ESTRING},
	{"OBJECTTYPE",			TOK_OBJECTTYPE},
//	{"REDUNDANTNAME",		TOK_REDUNDANTNAME}, // special handling, don't uncomment
	{"STRUCTPARAM",			TOK_STRUCTPARAM},
	{"ALWAYS_ALLOC",		TOK_ALWAYS_ALLOC},
	{"NON_NULL_REF",		TOK_NON_NULL_REF},
	{"REQUIRED",			TOK_REQUIRED},
	{"NO_WRITE",			TOK_NO_WRITE},
	{"NO_NETSEND",			TOK_NO_NETSEND},
	{"FLATEMBED",			TOK_FLATEMBED},
	{"NO_TEXT_SAVE",		TOK_NO_TEXT_SAVE},
	{"GLOBAL_NAME",			TOK_GLOBAL_NAME},
	{"USEDFIELD",			TOK_USEDFIELD},

	{"USEROPTIONBIT_1",		TOK_USEROPTIONBIT_1},
	{"USEROPTIONBIT_2",		TOK_USEROPTIONBIT_2},
	{"USEROPTIONBIT_3",		TOK_USEROPTIONBIT_3},
	{"POOL_STRING_DB",		TOK_POOL_STRING_DB},
	{"PUPPET_NO_COPY",		TOK_PUPPET_NO_COPY},

	{"SUBSCRIBE",			TOK_SUBSCRIBE},
	{"SERVER_ONLY",			TOK_SERVER_ONLY},
	{"CLIENT_ONLY",			TOK_CLIENT_ONLY},
	{"SELF_ONLY",			TOK_SELF_ONLY},
	{"SELF_AND_TEAM_ONLY",  TOK_SELF_AND_TEAM_ONLY},
	{"LOGIN_SUBSCRIBE",		TOK_LOGIN_SUBSCRIBE},

	{"KEY",					TOK_KEY},
	{"PERSIST",				TOK_PERSIST},
	{"NO_TRANSACT",			TOK_NO_TRANSACT},
	{"SOMETIMES_TRANSACT",	TOK_SOMETIMES_TRANSACT},
	{"VITAL_REF",			TOK_VITAL_REF},
	{"NON_NULL_REF__ERROR_ONLY",	TOK_NON_NULL_REF__ERROR_ONLY},
	{"DIRTY_BIT",			TOK_DIRTY_BIT},

	{"NO_INHERIT",			TOK_NO_INHERIT },
	{"IGNORE_STRUCT",		TOK_IGNORE_STRUCT },
	{"SPECIAL_DEFAULT",		TOK_SPECIAL_DEFAULT },
	{"PARSETABLE_INFO",		TOK_PARSETABLE_INFO },
	{"INHERITANCE_STRUCT",	TOK_INHERITANCE_STRUCT },
	{"STRUCT_NORECURSE",	TOK_STRUCT_NORECURSE},
	{"CASE_SENSITIVE",		TOK_CASE_SENSITIVE},
	{"EDIT_ONLY",			TOK_EDIT_ONLY},
	{"NO_INDEX",			TOK_NO_INDEX},
	{"NO_LOG",				TOK_NO_LOG},


	{"",	0} // end
};

StaticDefineInt ParseFloatRounding[] =
{
	DEFINE_INT
	{"HUNDREDTHS",	FLOAT_HUNDREDTHS},
	{"TENTHS",		FLOAT_TENTHS},
	{"ONES",		FLOAT_ONES},
	{"FIVES",		FLOAT_FIVES},
	{"TENS",		FLOAT_TENS},
	DEFINE_END
};

StaticDefineInt ParseFormatOptions[] =
{
	DEFINE_INT
	{"IP",				TOK_FORMAT_IP},
	{"UNSIGNED",		TOK_FORMAT_UNSIGNED},
	{"DATESS2000",		TOK_FORMAT_DATESS2000},
	{"PERCENT",			TOK_FORMAT_PERCENT},
	{"HSV",				TOK_FORMAT_HSV},
	{"TEXTURE",			TOK_FORMAT_TEXTURE},
	{"COLOR",			TOK_FORMAT_COLOR},
	{"FRIENDLYDATE",	TOK_FORMAT_FRIENDLYDATE},
	{"FRIENDLYSS2000",	TOK_FORMAT_FRIENDLYSS2000},
	{"KBYTES",			TOK_FORMAT_KBYTES},
	{"FLAGS",			TOK_FORMAT_FLAGS},
	DEFINE_END
};

StaticDefineInt ParseFormatUIOptions[] =
{
	DEFINE_INT
	{"FORMAT_UI_LEFT",				TOK_FORMAT_UI_LEFT},
	{"FORMAT_UI_RIGHT",				TOK_FORMAT_UI_RIGHT},
	{"FORMAT_UI_RESIZABLE",			TOK_FORMAT_UI_RESIZABLE},
	{"FORMAT_UI_NOTRANSLATE_HEADER",TOK_FORMAT_UI_NOTRANSLATE_HEADER},
	{"FORMAT_UI_NOHEADER",			TOK_FORMAT_UI_NOHEADER},
	{"FORMAT_UI_NODISPLAY",			TOK_FORMAT_UI_NODISPLAY},
	DEFINE_END
};

AUTO_RUN;
void MakeTPUEnumsFast(void)
{
	StaticDefineInt_PossiblyAddFastIntLookupCache(ParseFloatRounding);
	StaticDefineInt_PossiblyAddFastIntLookupCache(ParseFormatOptions);
	StaticDefineInt_PossiblyAddFastIntLookupCache(ParseFormatUIOptions);
}


static StructTypeField TypeOptionGetBit(char* option)
{
	int i;
	for (i = 0; TypeOptionNames[i].bit; i++)
	{
		if (0==stricmp(TypeOptionNames[i].name, option))
			return TypeOptionNames[i].bit;
	}
	return 0;
}

void ParseInfoPushOption(ParseInfoColumn* elem, const char* option, const char* param)
{
	StructFunctionCall* call = StructAllocRaw(sizeof(StructFunctionCall));
	call->function = StructAllocString(option);
	eaPush(&elem->options, call);
	if (param && param[0])
	{
		StructFunctionCall* call2 = StructAllocRaw(sizeof(StructFunctionCall));
		call2->function = StructAllocString(param);
		eaPush(&call->params, call2);
	}
}

void ParseInfoPushOptionInt(ParseInfoColumn* elem, const char* option, int param)
{
	char buf[100];
	sprintf(buf, "%i", param);
	ParseInfoPushOption(elem, option, buf);
}

char* ParseInfoGetType(ParseTable tpi[], int column, StructTypeField* modifiedtype)
{
	char* ret = 0;
	int storage = TokenStoreGetStorageType(tpi[column].type);
	
	if (modifiedtype)
	{	
		*modifiedtype = tpi[column].type;
	}

	// first attempt to find a name that combines both the basic type and the storage attribute
	switch (storage) {
	case TOK_STORAGE_DIRECT_FIXEDARRAY:		ret = TYPE_INFO(tpi[column].type).name_direct_fixedarray; break;
	case TOK_STORAGE_DIRECT_EARRAY:			ret = TYPE_INFO(tpi[column].type).name_direct_earray; break;
	case TOK_STORAGE_INDIRECT_SINGLE:		ret = TYPE_INFO(tpi[column].type).name_indirect_single; break;
	case TOK_STORAGE_INDIRECT_FIXEDARRAY:	ret = TYPE_INFO(tpi[column].type).name_indirect_fixedarray; break;
	case TOK_STORAGE_INDIRECT_EARRAY:		ret = TYPE_INFO(tpi[column].type).name_indirect_earray; break;
	}

	if (ret) // clear the storage attribute if we recognized the combined name
	{
		if (modifiedtype)
		{
			*modifiedtype = *modifiedtype & ~TOK_INDIRECT;
			*modifiedtype = *modifiedtype & ~TOK_EARRAY;
			*modifiedtype = *modifiedtype & ~TOK_FIXED_ARRAY;
		}
	}
	else
	{
		ret = TYPE_INFO(tpi[column].type).name_direct_single; // guaranteed to exist
	}
	return ret;
}

bool ParseInfoTypeFromName(char* name, StructTypeField* result)
{
	int i;
	for (i = 0; i < NUM_TOK_TYPE_TOKENS; i++)
	{
#define TRY_FIELD(field, bits) \
	if (TYPE_INFO(i).field && stricmp(TYPE_INFO(i).field, name) == 0) \
	{ \
		*result |= i | bits; \
		return true; \
	}
		TRY_FIELD(name_direct_single, 0);
		TRY_FIELD(name_direct_fixedarray,	TOK_FIXED_ARRAY);
		TRY_FIELD(name_direct_earray,		TOK_EARRAY);
		TRY_FIELD(name_indirect_single,		TOK_INDIRECT);
		TRY_FIELD(name_indirect_fixedarray, TOK_INDIRECT | TOK_FIXED_ARRAY);
		TRY_FIELD(name_indirect_earray,		TOK_INDIRECT | TOK_EARRAY);
#undef TRY_FIELD
	}
	return false;
}

// find a field with the same offset as passed column
int ParseInfoFindAliasedField(ParseTable pti[], int column)
{
	int i;

	FORALL_PARSETABLE(pti, i)
	{
		if (i == column) continue;
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		if (pti[column].storeoffset == pti[i].storeoffset &&
			TOK_GET_TYPE(pti[column].type) == TOK_GET_TYPE(pti[i].type)
			&& (TOK_GET_TYPE(pti[column].type) != TOK_BIT || pti[column].param == pti[i].param))
			return i;
	}
	return -1;
}

bool ParseInfoShouldSkipThisField(ParseTable pti[], int iColumnNum, enumParseTableSendType eSendType)
{
	int iFieldAccessLevel;

	if (eSendType & PARSETABLESENDFLAG_FOR_HTTP_INTERNAL)
	{
		if (TOK_GET_TYPE(pti[iColumnNum].type) == TOK_COMMAND)
		{
			iFieldAccessLevel = cmdGetAccessLevelFromFullString(&gGlobalCmdList, (char*)(pti[iColumnNum].param));

			return iFieldAccessLevel > GetHTMLAccessLevel();
		}


		if (!GetIntFromTPIFormatString(&pti[iColumnNum], "AccessLevel", &iFieldAccessLevel))
		{
			return false;
		}			

		if (iFieldAccessLevel > GetHTMLAccessLevel())
		{
			return true;
		}
	}


	if (eSendType & PARSETABLESENDFLAG_FOR_API)
	{
		if (TOK_GET_TYPE(pti[iColumnNum].type) == TOK_IGNORE) return true;
		if (TOK_GET_TYPE(pti[iColumnNum].type) == TOK_START) return true;
		if (TOK_GET_TYPE(pti[iColumnNum].type) == TOK_END) return true;
		if (pti[iColumnNum].type & TOK_REDUNDANTNAME) return true;
		if (pti[iColumnNum].type & TOK_FLATEMBED) return true;
	}

	if (eSendType & PARSETABLESENDFLAG_FOR_SQL)
	{
		if (TOK_GET_TYPE(pti[iColumnNum].type) == TOK_IGNORE) return true;
		if (TOK_GET_TYPE(pti[iColumnNum].type) == TOK_START) return true;
		if (TOK_GET_TYPE(pti[iColumnNum].type) == TOK_END) return true;
		if (pti[iColumnNum].type & TOK_PERSIST) return false;
		return true;
	}
	return false;
}

static bool TypeIsOKForSchema( StructTypeField type )
{
	if (type & TOK_PERSIST || TOK_GET_TYPE(type) == TOK_COMMAND)
	{
		return true;
	}

	return false;
}


void ParseInfoIdentifySubtables(ParseTable pti[], StashTable subNames, const char* name, enumParseTableSendType eSendType)
{
	int i;
	if (stashAddressFindElement(subNames, pti, NULL)) return; // we've already done this subtable
	stashAddressAddPointer(subNames, pti, strdup(name), 0);
	
	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_HAS_SUBTABLE(pti[i].type) && !ParseInfoShouldSkipThisField(pti, i, eSendType))
		{
			char fullName[1000];
			fullName[0] = 0;
			
			if (eSendType & PARSETABLESENDFLAG_FOR_SCHEMAS)
			{
				if (!TypeIsOKForSchema(pti[i].type))
					continue;
			}

//ABW not totally certain what's going on here, I think that the name is not very relevant
//			devassertmsg(pti[i].name && pti[i].name[0], "Don't have a subtable name to use with ParseInfo functions?");
			if (eSendType & PARSETABLESENDFLAG_FOR_SQL)
			{
				ParseTable *subtable = pti[i].subtable;
				if (subtable[0].type & TOK_PARSETABLE_INFO)
				{
					sprintf(fullName,"%s",subtable[0].name);
				}
				else if (pti[i].name && pti[i].name[0])
				{
					sprintf(fullName,"%s.%s",name,pti[i].name);
				}
				else
				{
					sprintf(fullName, "%s.COL%d", name, i);
				}
			}
			else
			{
				if (pti[i].name && pti[i].name[0])
				{
					sprintf(fullName,"%s.%s",name,pti[i].name);
				}
				else
				{
					sprintf(fullName, "%s.COL%d", name, i);
				}
			}

			ParseInfoIdentifySubtables(pti[i].subtable, subNames, fullName, eSendType);
		}
	}
}



char *ParseInfoAddStaticDefineToDescriptor(ParseInfoDescriptor *pid, void * definePointer, const char *name)
{
	StaticDefineListInfo *info;
	if (stashAddressFindPointer(pid->subDefines,definePointer,&info))
	{
		// Already here, return the name so we're consistent
		return info->name;
	}
	info = StructAllocRaw(sizeof(StaticDefineListInfo));
	info->name = StructAllocString(name);
	if (((StaticDefine *)definePointer)->key == U32_TO_PTR(DM_INT))
	{
		StaticDefineInt *defineList = (StaticDefineInt *)definePointer;
		int i;
		info->type = DM_INT;
		for (i = 1; defineList[i].key != U32_TO_PTR(DM_END); i++)
		{
			char numString[100];
			StaticDefineEntry *defineEntry;
			if (defineList[i].key == U32_TO_PTR(DM_DYNLIST))
			{
				DefineContext* context = *(DefineContext**)defineList[i].value;

				if (context)
				{
					char **ppchKeys = NULL;
					char **ppchVals = NULL;
					int j;

					DefineGetKeysAndVals(context,&ppchKeys,&ppchVals);
				
					for (j=0; j<eaSize(&ppchKeys); j++)
					{
						if (!ppchKeys[j] || !ppchVals[j])
						{
							continue;
						}
						defineEntry = StructAllocRaw(sizeof(StaticDefineEntry));

						defineEntry->name = StructAllocString(ppchKeys[j]);
						defineEntry->value = StructAllocString(ppchVals[j]);
						eaPush(&info->entries,defineEntry);
				
					}
					eaDestroy(&ppchKeys);
					eaDestroy(&ppchVals);
				}

				continue;

			}
			if (defineList[i].key == U32_TO_PTR(DM_TAILLIST))
			{
				defineList = (StaticDefineInt *)defineList[i].value;
				i = 0;
				continue;
			}
			defineEntry = StructAllocRaw(sizeof(StaticDefineEntry));

			sprintf(numString,"%d",defineList[i].value);
			defineEntry->name = StructAllocString(defineList[i].key);
			defineEntry->value = StructAllocString(numString);
			eaPush(&info->entries,defineEntry);
		}	
	}
	else if (((StaticDefine *)definePointer)->key == U32_TO_PTR(DM_STRING))
	{
		StaticDefine *defineList = (StaticDefine *)definePointer;
		int i;
		info->type = DM_STRING;
		for (i = 1; defineList[i].key != U32_TO_PTR(DM_END); i++)
		{
			StaticDefineEntry *defineEntry;
			if (defineList[i].key == U32_TO_PTR(DM_DYNLIST))
			{
				assertmsg(0,"DynLists are not supported in persisted enums!");
				return NULL;
			}
			if (defineList[i].key == U32_TO_PTR(DM_TAILLIST))
			{
				defineList = (StaticDefine *)defineList[i].value;
				i = 0;
				continue;
			}
			defineEntry = StructAllocRaw(sizeof(StaticDefineEntry));

			defineEntry->name = StructAllocString(defineList[i].key);
			defineEntry->value = StructAllocString(defineList[i].value);
			eaPush(&info->entries,defineEntry);
			i++;
		}	
	}
	else
	{
		assertmsgf(0,"Bad Static define table %s!",name);
	}
	eaPush(&pid->defines,info);
	stashAddressAddPointer(pid->subDefines,definePointer,info,0);
	return info->name;
}

ParseTable *GetAndModifyTableCopyForSendingIfNecessary(ParseTable pti[], enumParseTableSendType eSendType)
{
	int iNumColumns;
	int i;
	ParseTable *pCopyTable = NULL;

	if (!(eSendType & PARSETABLESENDFLAG_FOR_HTTP_INTERNAL))
	{
		return NULL;
	}

	iNumColumns = ParserGetTableNumColumns(pti);
	for (i=0; i < iNumColumns; i++)
	{
		if (GetStringFromTPIFormatString(&pti[i], "HTML_CONVERT_OPT_STRUCT_TO_KEY_STRING", NULL))
		{
			U32 storage = TokenStoreGetStorageType(pti[i].type);

			if (storage != TOK_STORAGE_INDIRECT_SINGLE || TOK_GET_TYPE(pti[i].type) != TOK_STRUCT_X)
			{
				assertmsgf(0, "HTML_CONVERT_OPT_STRUCT_TO_KEY_STRING only valid for optional structs");
			}

			if (!pCopyTable)
			{
				pCopyTable = malloc(sizeof(ParseTable) * (iNumColumns + 1));
				memcpy(pCopyTable, pti, sizeof(ParseTable) * (iNumColumns + 1));
			}

			pCopyTable[i].type = TOK_INDIRECT | TOK_STRING_X;
			pCopyTable[i].param = 0;
			pCopyTable[i].subtable = 0;
			pCopyTable[i].format = 0;
		}
	}

	return pCopyTable;
}

ParseInfoTable* ParseInfoToTable(ParseTable pti[], ParseInfoDescriptor *pid, enumParseTableSendType eSendType)
{
	ParseInfoFieldUsage usage;
	StructTypeOptionName* typeoption;
	StaticDefineInt* define;
	int i, precision, format, width;
	const char *option, *param;
	ParseInfoTable* table;
	char* tablename;
	ParseTable *pTempTableCopy;

	// set up the table
	table = StructAllocRaw(sizeof(*table));
    stashAddressFindPointer(pid->subNames, pti, &tablename);
	devassertmsgf(tablename, "Internal parse error in %s", __FUNCTION__); // should be in hash already
	if (tablename)
		table->name = StructAllocString(tablename);

	//special case... some FORMAT_STRING options require that their column be effectively lied about when doing certain kinds of sends.
	//If so, we make a temp copy of the entire parse table, make the change to the appropriate column in the temp copy, do all our work on it,
	//and then free it when we're done
	pTempTableCopy = GetAndModifyTableCopyForSendingIfNecessary(pti, eSendType);
	if (pTempTableCopy)
	{
		pti = pTempTableCopy;
	}


	// then each column
	FORALL_PARSETABLE(pti, i)
	{
		StructTypeField modifiedtype = pti[i].type;
		ParseInfoColumn* elem;

		if (ParseInfoShouldSkipThisField(pti, i, eSendType))
		{
			continue;
		}

		elem = StructAllocRaw(sizeof(ParseInfoColumn));

		// name
		eaPush(&table->columns, elem);
		if (pti[i].name && pti[i].name[0])
			ParseInfoPushOption(elem, "NAME", pti[i].name);

		// basic type
		option = ParseInfoGetType(pti, i, &modifiedtype); // can strip some bits from modifiedtype
		if (eSendType & PARSETABLESENDFLAG_FOR_SCHEMAS)
		{
			// Convert certain things into ignores, but leave them in the info
			if (TOK_HAS_SUBTABLE(pti[i].type) && !TypeIsOKForSchema(pti[i].type))
			{
				ParseInfoPushOption(elem, "IGNORE", NULL);
				ParseInfoPushOption(elem, "IGNORE_STRUCT", NULL);
				continue;
			}
			if (!TypeIsOKForSchema(pti[i].type)
				&& TOK_GET_TYPE(pti[i].type) != TOK_END 
				&& TOK_GET_TYPE(pti[i].type) != TOK_START 
				&& TOK_GET_TYPE(pti[i].type) != TOK_IGNORE)
			{
				ParseInfoPushOption(elem, "IGNORE", NULL);
				continue;
			}			
		}
		ParseInfoPushOption(elem, option, NULL);

		if (pti[i].type & TOK_PARSETABLE_INFO)
		{
			ParseInfoPushOption(elem, "PARSETABLE_INFO", NULL);
			//format string
			if (FormatStringIsSet(&pti[i]))
				ParseInfoPushOption(elem, "FORMATSTRING", GetRawFormatString(&pti[i]));

			continue;
		}

		// type options
		for (typeoption = TypeOptionNames; typeoption->bit; typeoption++)
		{
			if (modifiedtype & typeoption->bit)
				ParseInfoPushOption(elem, typeoption->name, NULL);
		}

		// special handling for redundantname - param is name of aliased field
		if (pti[i].type & TOK_REDUNDANTNAME)
		{
			int alias = ParseInfoFindAliasedField(pti, i);
			devassertmsgf(alias >= 0 && pti[alias].name && pti[alias].name[0], "Couldn't find aliased field for redundant token in %s", __FUNCTION__);
			if (alias >= 0)
			{
				ParseInfoPushOption(elem, "REDUNDANTNAME", pti[alias].name);
			}
		}

		// precision
		param = 0;
		precision = TOK_GET_PRECISION(modifiedtype);
		if (precision)
		{
			// seperate out float rounding if possible
			if (TOK_GET_TYPE(pti[i].type) == TOK_F32_X)
				param = StaticDefineIntRevLookup(ParseFloatRounding, precision);
			if (param)
				ParseInfoPushOption(elem, "FLOAT_ROUNDING", param);
			else
			{
				ParseInfoPushOptionInt(elem, "MINBITS", precision);
			}
		}

		// param
		usage = TYPE_INFO(pti[i].type).interpretfield(pti, i, ParamField);
		switch (usage)
		{
		case SizeOfSubstruct: break; // ignored, we will derive this again when loading
		xcase NumberOfElements:			ParseInfoPushOptionInt(elem, "NUM_ELEMENTS", pti[i].param); break;
		xcase DefaultValue:				if (pti[i].param) ParseInfoPushOptionInt(elem, "DEFAULT", pti[i].param); break;
		xcase EmbeddedStringLength:		ParseInfoPushOptionInt(elem, "STRING_LENGTH", pti[i].param); break;
		xcase PointerToDefaultString:	if (pti[i].param) ParseInfoPushOption(elem, "DEFAULT_STRING", (char*)pti[i].param); break;
		xcase PointerToCommandString:	if (pti[i].param) ParseInfoPushOption(elem, "COMMAND_STRING", (char*)pti[i].param); break;
		xcase OffsetOfSizeField: break; // ignored, we will derive this again when loading
		xcase SizeOfRawField:			ParseInfoPushOptionInt(elem, "SIZE", pti[i].param); break;
		xcase BitOffset:				ParseInfoPushOptionInt(elem, "BIT_OFFSET", pti[i].param); break;
		}

		// subtable
		usage = TYPE_INFO(pti[i].type).interpretfield(pti, i, SubtableField);
		switch (usage)
		{
		xcase PointerToDictionaryName:
			{
				if (pti[i].subtable)
				{
					ParseInfoPushOption(elem, "DICTIONARYNAME", (char*)pti[i].subtable);
				}
			} break;
		xcase StaticDefineList: 
		// We need to indicate this has a StaticDefineList, so plain-text sending works correctly
		// We may need to replace this with correctly serializing the actual StaticDefineList
			{
				if (pti[i].subtable)
				{
					const char *defineName = FindStaticDefineName(pti[i].subtable);
					char tempName[1024];

					if (!defineName || !defineName[0])
					{
						sprintf(tempName,"%s_%s_DefineList",tablename, pti[i].name);
						defineName = tempName;
					}
					defineName = ParseInfoAddStaticDefineToDescriptor(pid,pti[i].subtable, defineName);
					ParseInfoPushOption(elem, "STATICDEFINELIST", defineName);
				}
			} break; 
		
		xcase PointerToSubtable:
			{
				char* subname = 0;
				stashAddressFindPointer(pid->subNames, pti[i].subtable, &subname);
				devassertmsgf(subname, "Internal parse error in %s", __FUNCTION__); // should be in hash already
				if (subname)
					ParseInfoPushOption(elem, "SUBTABLE", subname);
			} break;
		}

		// format options
		format = TOK_GET_FORMAT_OPTIONS(pti[i].format);
		if (format)
		{
			param = StaticDefineIntRevLookup(ParseFormatOptions, format);
			if (param)
				ParseInfoPushOption(elem, "FORMAT", param);
			else // unknown format?
				ParseInfoPushOptionInt(elem, "FORMAT_RAW", format); 
		}

		// list view width
		width = TOK_FORMAT_GET_LVWIDTH(pti[i].format);
		if (width)
			ParseInfoPushOptionInt(elem, "LVWIDTH", width);

		// format ui options
		for (define = &ParseFormatUIOptions[1]; define->key != U32_TO_PTR(DM_END); define++)
		{
			if (pti[i].format & define->value)
				ParseInfoPushOption(elem, define->key, NULL);
		}

		//format string
		if (FormatStringIsSet(&pti[i]))
			ParseInfoPushOption(elem, "FORMATSTRING", GetRawFormatString(&pti[i]));


	} // each column

	if (pTempTableCopy)
	{
		free(pTempTableCopy);
	}
	return table;
}

//Everything this function needs to be OK to call multiple times. Generally, this should only
//be stuff that might have dependencies on other parse tables that are being freed at the same time
void ParseInfoPreFreeCleanup(ParseTable *pti)
{
	ParseTableInfo *pInfo = ParserGetTableInfo(pti);

	if (pInfo->pStructInitInfo)
	{
		PreDestroyStructInitInfo(pInfo->pStructInitInfo, pti);
	}
}


// should keep sync with any memory allocated by ParseInfoFromTable
void ParseInfoFreeTable(ParseTable* pti)
{
	int i;


	ParseInfoPreFreeCleanup(pti);

	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].name && (i!=0 || !TPIHasTPIInfoColumn(pti)))
		{			
			pti[i].name = NULL;
		}
		if (pti[i].param)
		{
			int usage = TYPE_INFO(pti[i].type).interpretfield(pti, i, ParamField);
			if (usage == PointerToDefaultString || usage == PointerToCommandString)
				StructFreeString((char*)pti[i].param);
		}
		if (pti[i].subtable)
		{
			int usage = TYPE_INFO(pti[i].type).interpretfield(pti, i, SubtableField);
			if (usage == PointerToDictionaryName)
				StructFreeString((char*)pti[i].subtable);
		}
		if (FormatStringIsSet(&pti[i]))
		{
			FreeFormatString(&pti[i]);
		}

	}
	ParserClearTableInfo(pti);
	_StructFree_internal(NULL, pti); // the whole parse table was alloced as a big chunk
}



void ParseTableFree(ParseTable*** eapti)
{
	int i;
	
	//we need to do our freeing in two steps... first, go in and do anything which
	//might depend on the other parse tables still being around
	for (i = 0; i < eaSize(eapti); i++)
		ParseInfoPreFreeCleanup((*eapti)[i]);

	for (i = 0; i < eaSize(eapti); i++)
		ParseInfoFreeTable((*eapti)[i]);
	eaDestroy(eapti);
}




ParseTable* ParseInfoFromTable(ParseInfoTable* table, ParseInfoDescriptor *pid, enumParseTableSendType eSendType)
{
	int i, opt;
	ParseTable *pFullTPI, *pWorkingTPI; //pWorkingTPI starts after the info column to avoid lots of off-by-1 issues (now deprecated)
	StructTypeField TypeOptionBit;
	int iEnum;



	pFullTPI = StructAllocRaw(sizeof(ParseTable) * (eaSize(&table->columns)+1)); // last entry null
	pWorkingTPI = pFullTPI;


	for (i = 0; i < eaSize(&table->columns); i++) // each column
	{
		for (opt = 0; opt < eaSize(&table->columns[i]->options); opt++) // each option for the column
		{
			StructFunctionCall* optionstruct = table->columns[i]->options[opt];
			char* option = optionstruct->function;
			char* param = optionstruct->params ? optionstruct->params[0]->function: NULL;
			if (!option)
			{
				Errorf("Invalid format encountered in ParseInfoFromTable");
				continue;
			}
			else if (stricmp(option, "NAME")==0)
			{
				if (!param) 
				{
					Errorf("Name not given parameter in %s", __FUNCTION__);
				}
				else
				{
					pWorkingTPI[i].name = allocAddString(param);
				}
			}
			else if (stricmp(option, "FORMATSTRING")==0)
			{
				if (!param) 
				{
					Errorf("FORMATSTRING not given parameter in %s", __FUNCTION__);
				}
				else
				{
					SetFormatString(&pWorkingTPI[i], param);
				}
			}
			else if (ParseInfoTypeFromName(option, &pWorkingTPI[i].type))
			{
				if (TOK_GET_TYPE(pWorkingTPI[i].type) == TOK_BIT)
				{
					if (eSendType & PARSETABLESENDFLAG_MAINTAIN_BITFIELDS)
					{
						//do nothing, bitfield remains bitfield, will get fixed up later


					}
					else if (eSendType & PARSETABLESENDFLAG_CONVERT_BITFIELDS_TO_APPROPRIATE_SIZED_INTS)
					{
						//do nothing, bitfield will get turned into int when we get the bitdepth argument later this same function
						
					}
					else	
					{
						pWorkingTPI[i].type &= (~TOK_TYPE_MASK);
						pWorkingTPI[i].type |= TOK_U8_X;
					}
				}
			}
			else if ((TypeOptionBit = TypeOptionGetBit(option)))
			{
				pWorkingTPI[i].type |= TypeOptionGetBit(option);
			}
			else if (stricmp(option, "REDUNDANTNAME")==0)
			{
				// putting the name in storeoffset right now, it will be fixed up later in this function
				if (!param)
					Errorf("REDUNDANTNAME not given parameter in %s", __FUNCTION__);
				pWorkingTPI[i].type |= TOK_REDUNDANTNAME;
				pWorkingTPI[i].storeoffset = (size_t)param;
			}
			else if (stricmp(option, "FLOAT_ROUNDING")==0)
			{
				if (!param)
					Errorf("FLOAT_ROUNDING missing parameter in %s", __FUNCTION__);
				else
				{
					int rounding = StaticDefineIntGetInt(ParseFloatRounding, param);
					if (!rounding)
						Errorf("Invalid param %s to FLOAT_ROUNDING in %s", __FUNCTION__, param);
					else
						pWorkingTPI[i].type |= TOK_FLOAT_ROUNDING(rounding);
				}
			}
			else if (stricmp(option, "MINBITS")==0)
			{
				if (!param)
					Errorf("MINBITS missing parameter in %s", __FUNCTION__);
				else
				{
					pWorkingTPI[i].type |= TOK_MINBITS(atoi(param));
				}
			}
			else if (stricmp(option, "BIT_OFFSET")==0)
			{
				if (eSendType & PARSETABLESENDFLAG_MAINTAIN_BITFIELDS)
				{
					U32 iParamVal;
					if (!StringToUint(param, &iParamVal))
					{
						Errorf("Invalid param string %s while trying to receive the param field for a bitfield", param);
					}
					else
					{
						pWorkingTPI[i].param = (iParamVal >> 16) << 16;
					}
				}
				else if (eSendType & PARSETABLESENDFLAG_CONVERT_BITFIELDS_TO_APPROPRIATE_SIZED_INTS )
				{
					U32 iParamVal;
					int iNumBits;
		
					if (!TOK_GET_TYPE(pWorkingTPI[i].type) == TOK_BIT)
					{
						AssertOrAlert("TPI_TYPE_CORRUPTION", "Got BIT_OFFSET for a non-bitfield field");
					}

					if (!StringToUint(param, &iParamVal))
					{
						Errorf("Invalid param string %s while trying to receive the param field for a bitfield", param);
					}
					else
					{
						pWorkingTPI[i].param = (iParamVal >> 16) << 16;
					}

					iNumBits = TextParserBitField_GetBitCount(pWorkingTPI, i);
		
					if (iNumBits <= 8)
					{
						pWorkingTPI[i].type &= (~TOK_TYPE_MASK);
						pWorkingTPI[i].type |= TOK_U8_X;
					}
					else if (iNumBits <= 16)
					{
						pWorkingTPI[i].type &= (~TOK_TYPE_MASK);
						pWorkingTPI[i].type |= TOK_INT16_X;
					}
					else if (iNumBits <= 32)
					{
						pWorkingTPI[i].type &= (~TOK_TYPE_MASK);
						pWorkingTPI[i].type |= TOK_INT_X;
					}

					pWorkingTPI[i].param = 0;
				}
				else
				{
					U32 iParamVal;

					if (!StringToUint(param, &iParamVal))
					{
						Errorf("Invalid param string %s while trying to receive the param field for a bitfield", param);
					}
					else
					{
						int iNumBits = iParamVal >> 16;
						if (iNumBits > 8)
						{
							AssertOrAlert("BITFIELD_TOO_BIG", "Someone is converting a >8bit bitfield into a U8 during ParseTable reading/receiving");
						}
					}
				}
			}
			else if (stricmp(option, "NUM_ELEMENTS")==0 ||
					stricmp(option, "DEFAULT")==0 ||
					stricmp(option, "STRING_LENGTH")==0 ||
					stricmp(option, "SIZE")==0)
			{
				int usage = TYPE_INFO(pWorkingTPI[i].type).interpretfield(pWorkingTPI, i, ParamField);
				if (usage != NumberOfElements && 
					usage != DefaultValue &&
					usage != EmbeddedStringLength &&
					usage != SizeOfRawField &&
					usage != BitOffset)
				{
					Errorf("Invalid option %s passed to %s. You may need to state the correct type first", option, __FUNCTION__);
				}
				else if (!param)
				{
					Errorf("No param given to option %s in %s", option, __FUNCTION__);
				}
				else
				{
					pWorkingTPI[i].param = atoi(param);
				}
			}
			else if (stricmp(option, "DEFAULT_STRING")==0)
			{
				int usage = TYPE_INFO(pWorkingTPI[i].type).interpretfield(pWorkingTPI, i, ParamField);
				if (usage != PointerToDefaultString)
				{
					Errorf("Invalid option %s passed to %s. You may need to state the correct type first", option, __FUNCTION__);
				}
				else if (!param)
				{
					Errorf("No param given to option %s in %s", option, __FUNCTION__);
				}
				else
				{
					pWorkingTPI[i].param = (intptr_t)StructAllocString(param);
				}
			}			
			else if (stricmp(option, "COMMAND_STRING")==0)
			{
				int usage = TYPE_INFO(pWorkingTPI[i].type).interpretfield(pWorkingTPI, i, ParamField);
				if (usage != PointerToCommandString)
				{
					Errorf("Invalid option %s passed to %s. You may need to state the correct type first", option, __FUNCTION__);
				}
				else if (!param)
				{
					Errorf("No param given to option %s in %s", option, __FUNCTION__);
				}
				else
				{
					pWorkingTPI[i].param = (intptr_t)StructAllocString(param);
				}
			}
			else if (stricmp(option, "DICTIONARYNAME")==0)
			{
				int usage = TYPE_INFO(pWorkingTPI[i].type).interpretfield(pWorkingTPI, i, SubtableField);
				if (usage != PointerToDictionaryName)
				{
					Errorf("Invalid option %s passed to %s. You may need to state the correct type first", option, __FUNCTION__);
				}
				else
				{
					pWorkingTPI[i].subtable = (void*)StructAllocString(param);
				}
			}
			else if (stricmp(option, "STATICDEFINELIST")==0)
			{
				int usage = TYPE_INFO(pWorkingTPI[i].type).interpretfield(pWorkingTPI, i, SubtableField);
				if (usage != StaticDefineList)
				{
					Errorf("Invalid option %s passed to %s. You may need to state the correct type first", option, __FUNCTION__);
				}
				else
				{
					void *defineList = NULL;
					if (param && stashFindPointer(pid->subDefines,param,&defineList))
					{
						pWorkingTPI[i].subtable = defineList;
					}
					else
					{
						pWorkingTPI[i].subtable = NullStaticDefineList;
					}
				}
			}
			else if (stricmp(option, "SUBTABLE")==0)
			{
				if (!TOK_HAS_SUBTABLE(pWorkingTPI[i].type))
				{
					Errorf("Option SUBTABLE passed to an invalid field type in %s.  You may need to state the correct type first", __FUNCTION__);
				}
				else
				{
					// putting the name in subtable right now, it will be fixed up later in this function
					if (!param)
						Errorf("SUBTABLE not given parameter in %s", __FUNCTION__);
					pWorkingTPI[i].subtable = param;
				}
			}
			else if (stricmp(option, "FORMAT")==0)
			{
				if (!param)
					Errorf("FORMAT missing parameter in %s", __FUNCTION__);
				else
				{
					int format = StaticDefineIntGetInt(ParseFormatOptions, param);
					if (!format)
						Errorf("Invalid param %s to FORMAT in %s", param, __FUNCTION__);
					else
						pWorkingTPI[i].format |= format;
				}
			}
			else if (stricmp(option, "FORMAT_RAW")==0)
			{
				if (!param)
					Errorf("FORMAT_RAW missing parameter in %s", __FUNCTION__);
				else
				{
					int format = atoi(param);
					if (!format || TOK_GET_FORMAT_OPTIONS(format) != format)
						Errorf("Invalid param %s to FORMAT_RAW in %s", param, __FUNCTION__);
					else
						pWorkingTPI[i].format |= format;
				}
			}
			else if (stricmp(option, "LVWIDTH")==0)
			{
				if (!param)
					Errorf("LVWIDTH missing parameter in %s", __FUNCTION__);
				else
				{
					int width = atoi(param);
					if (!width)
						Errorf("Invalid param %s to LVWIDTH in %s", param, __FUNCTION__);
					else
						pWorkingTPI[i].format |= TOK_FORMAT_LVWIDTH(width);
				}
			}
			else if ((iEnum = StaticDefineInt_FastStringToInt(ParseFormatUIOptions, option, INT_MIN)) != INT_MIN)
			{
				pWorkingTPI[i].format |= iEnum;
			}
		} // per option

		// MAK - get rid of this assumption in other code later?
		// need to have a name field
		if (!pWorkingTPI[i].name) 
			pWorkingTPI[i].name = allocAddString("");
	} // per column

	return pFullTPI;
}




void ParseInfoToTableIter(ParseTable pti[], ParseInfoDescriptor* pid, enumParseTableSendType eSendType)
{
	ParseInfoTable* table;
	int i;
	if (stashAddressFindElement(pid->subAddresses, pti, NULL)) return; // we've already done this subtable
	stashAddressAddPointer(pid->subAddresses, pti, 0, 0);
	table = ParseInfoToTable(pti, pid, eSendType);

	if (!(eSendType & PARSETABLESENDFLAG_FOR_SQL))
	{
		if (table) eaPush(&pid->tables, table);
	}

	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_HAS_SUBTABLE(pti[i].type) && !ParseInfoShouldSkipThisField(pti, i, eSendType))
		{
			if (eSendType & PARSETABLESENDFLAG_FOR_SCHEMAS)
			{
				if (!TypeIsOKForSchema(pti[i].type))
					continue;
			}
			ParseInfoToTableIter(pti[i].subtable, pid, eSendType);
		}
	}

	if (eSendType & PARSETABLESENDFLAG_FOR_SQL)
	{
		if (table) eaPush(&pid->tables, table);
	}
}

static void freeFunc(char *str)
{
	free(str);
}


bool ParseInfoToDescriptor(ParseTable pti[], ParseInfoDescriptor* pid, const char* name, enumParseTableSendType eSendType)
{
	int count;
	pid->schemaVersion = GetSchemaVersion();
	pid->subAddresses = stashTableCreateAddress(10);
	pid->subNames = stashTableCreateAddress(10);
	pid->subDefines = stashTableCreateAddress(10);

	if (eSendType & PARSETABLESENDFLAG_FOR_SQL)
	{	//find the real table name
		if (pti[0].type & TOK_PARSETABLE_INFO) name = pti[0].name;
		else return false;
	}

	ParseInfoIdentifySubtables(pti, pid->subNames, name, eSendType);
	// I could just iterate over subNames here, but I'd prefer to have the subtables in order
	ParseInfoToTableIter(pti, pid, eSendType);
	count = stashGetCount(pid->subNames);
	stashTableDestroyEx(pid->subNames,NULL,freeFunc);
	stashTableDestroy(pid->subAddresses);
	stashTableDestroy(pid->subDefines);
	pid->subNames = 0;
	return count == eaSize(&pid->tables);
}

bool ParseInfoFixupSubtablePointers(ParseTable pti[], ParseInfoDescriptor *pid)
{
	void* subtable;
	bool ret = true;
	int i;
	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_HAS_SUBTABLE(pti[i].type))
		{
			if (!pti[i].subtable)
			{
				Errorf("Missing subtable from TOK_STRUCT in %s", __FUNCTION__);
				ret = false;
			}
			else if (!stashFindPointer(pid->subAddresses, pti[i].subtable, &subtable)) // subtable is a string right now
			{
				Errorf("Invalid subtable reference %s. Missing target table", (char*)pti[i].subtable);
				ret = false;
			}
			else // lookup worked
			{
				pti[i].subtable = subtable;
			}
		}
	}
	return ret;
}

bool ParseInfoFixupRedundantNames(ParseTable pti[], enumParseTableSendType eSendType)
{
	bool ret = true;
	int i, j;

	U32 iSize = ParserGetTableSize(pti);

	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME)
		{
			bool found = false;
			char* parentfield = (char*)pti[i].storeoffset;
			assertmsgf (((uintptr_t)parentfield) > iSize, "Table corruption detected during ParseInfoFixupRedundantNames... field %s being double-fixed up", pti[i].name);

			FORALL_PARSETABLE(pti, j)
			{
				if (i == j) continue;
				if (pti[j].type & TOK_REDUNDANTNAME) continue;
				if (pti[j].name && stricmp(pti[j].name, parentfield)==0)
				{
					assertmsgf(!found, "Found two different non-redundant fields named %s while doing redundant field fixup",
						parentfield);
					found = true;
					pti[i].storeoffset = pti[j].storeoffset;
				}
			}
			if (!found)
			{
				Errorf("REDUNDANTNAME given name of invalid field %s.  This doesn't not match another field in the struct.", parentfield);
				ret = false;
				pti[i].storeoffset = 0; 
			}
		}
	}
	return ret;
}

//appends a given string to a TPI's FormatString, creating it if it doesn't exist.
//
//Note that this generally only should be done to TPIs that have been created by parse table send/receive stuff,
//as it's assuming the formatstring is locally owned
void AppendFormatString(ParseTable *tpi, char *pString)
{
	if (FormatStringIsSet(tpi))
	{
		const char *pCurrentString;
		char *pNewString = NULL;
		pCurrentString = GetRawFormatString(tpi);
		FreeFormatString(tpi);
		estrStackCreate(&pNewString);
		estrPrintf(&pNewString, "%s , %s", pCurrentString, pString);
		SetFormatString(tpi, pNewString);
		estrDestroy(&pNewString);
	}
	else
	{
		SetFormatString(tpi, pString);
	}
}


// change references and static define lists that we don't understand in this process
int ParseInfoFixupMissingReferences(ParseTable parse[], enumParseTableSendType eSendType)
{
	int i;
	FORALL_PARSETABLE(parse, i)
	{
		ParseTable* column = parse+i;

		// Make sure to only do string bit fixups on string fields, as those bits are used for
		//  other things in other contexts, particularly in column 0 where they overlap with
		//  the number of columns.
		if(TOK_GET_TYPE(column->type) == TOK_STRING_X)
		{
			if (eSendType & PARSETABLESENDFLAG_FOR_SCHEMAS && column->type & TOK_ESTRING)
			{
				// Strip estring on db because of pointless memory overhead
				column->type &= ~TOK_ESTRING;
			}

			if (eSendType & PARSETABLESENDFLAG_FOR_SCHEMAS && column->type & TOK_POOL_STRING_DB)
			{
				// Turn on string pooling
				column->type |= TOK_POOL_STRING;
			}
		}

		if (TOK_GET_TYPE(column->type) == TOK_CURRENTFILE_X)
		{
			if (eSendType & PARSETABLESENDFLAG_FOR_HTTP_INTERNAL)
			{
				column->type &= ~TOK_CURRENTFILE_X;
				column->type |= TOK_STRING_X;
				column->type |= TOK_POOL_STRING;
			}
		}

		if (TOK_GET_TYPE(column->type) == TOK_REFERENCE_X)
		{
			if (eSendType & PARSETABLESENDFLAG_FOR_HTTP_INTERNAL)
			{
				//convert to a string, but leave the dictionary name in the subtable, add
				// a special flag telling the HTTP code that this is a former reference that
				//needs to become a link
				char *pNewFormatString = 0;
				
				estrPrintf(&pNewFormatString, " FORMER_REFERENCE = 1 , FORMER_DICTIONARY = \"%s\" ", (char*)column->subtable);

				AppendFormatString(column, pNewFormatString);

				column->type &= ~TOK_REFERENCE_X;
				column->type |= TOK_STRING_X;
				column->type |= TOK_POOL_STRING;
				StructFreeString((char *)column->subtable);
				column->subtable = NULL;

				
			}
			else if (eSendType & PARSETABLESENDFLAG_FOR_SCHEMAS)
			{
				//Always convert refs to strings for objectdb schemas.
				column->type &= ~TOK_REFERENCE_X;
				column->type |= TOK_STRING_X;
				column->type |= TOK_POOL_STRING;

				StructFreeString((char *)column->subtable);
				column->subtable = NULL;
			}
			else
			{
				if (!RefSystem_DoesDictionaryExist((char*)column->subtable))
				{
					// convert it to a string
					column->type &= ~TOK_REFERENCE_X;
					column->type |= TOK_STRING_X;
					column->type |= TOK_POOL_STRING;

					StructFreeString((char *)column->subtable);
					column->subtable = NULL;

				}
			}
		}
		if (TYPE_INFO(column->type).interpretfield(parse, i, SubtableField) == StaticDefineList)
		{
			// If there is a static define, set this to a normal (pooled) string
			// Eventually, replace this with looking things up in a dictionary of enums
			if (column->subtable == NullStaticDefineList)
			{
				// convert it to a string
				column->type &= ~TOK_TYPE_MASK;
				column->type |= TOK_STRING_X;
				column->type |= TOK_INDIRECT;
				column->type |= TOK_POOL_STRING;

				column->subtable = NULL;

				if (TYPE_INFO(column->type).interpretfield(parse, i, ParamField) == DefaultValue
					&& column->param)
				{
					//if all we're doing with this TPI is doing HTTP-viewing of preexisting
					//data, we can safely ignore default values
					if (eSendType & PARSETABLESENDFLAG_FOR_HTTP_INTERNAL)
					{
						column->param = 0;
					}
					else
					{
						Errorf("during tpi sending or receiving, trying to convert an enum (or something) to a string when it has a default value... this will cause a crash when an integer is treated as a string pointer");
					}
				}

			}
		}		
	}
	return 1;
}

void *StaticDefineFromDescriptor(StaticDefineListInfo *info, void ***pppAllocations, enumParseTableSendType eSendType)
{
	int size = eaSize(&info->entries);
	if (info->type == DM_STRING)
	{
		int i;
		StaticDefine *newDefines = calloc(sizeof(StaticDefine),size+2);
		eaPush(pppAllocations, newDefines);
		newDefines[0].key = U32_TO_PTR(DM_STRING);
		for (i = 0; i < size; i++)
		{
			newDefines[i+1].key = allocAddString(info->entries[i]->name);
			newDefines[i+1].value = allocAddString(info->entries[i]->value);
		}
		newDefines[size+1].key = U32_TO_PTR(DM_END);
		return newDefines;
	}
	else if (info->type == DM_INT)
	{
		int i;
		StaticDefineInt *newDefines = calloc(sizeof(StaticDefineInt),size+2);
		eaPush(pppAllocations, newDefines);
		newDefines[0].key = U32_TO_PTR(DM_INT);
		for (i = 0; i < size; i++)
		{
			newDefines[i+1].key = allocAddString(info->entries[i]->name);
			newDefines[i+1].value = atoi(info->entries[i]->value);
		}
		newDefines[size+1].key = U32_TO_PTR(DM_END);

		//schemas are never freed, so can attach a fast cache to all staticdefines without worrying about memory leaks
		if (eSendType == PARSETABLESENDFLAG_FOR_SCHEMAS)
		{
			StaticDefineInt_PossiblyAddFastIntLookupCache(newDefines);
		}

		return newDefines;
	}
	else
	{
		assertmsg(0,"Invalid StaticDefine type was serialized!");
		return NULL;
	}
}

ParseTable *RecursivelyFindNamedChildTable(ParseTable pti[], char *pChildName)
{
	int i;

	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_HAS_SUBTABLE(pti[i].type))
		{
			if (stricmp(ParserGetTableName(pti[i].subtable), pChildName) == 0)
			{
				return pti[i].subtable;
			}

			if (TOK_SHOULD_RECURSE_PARSEINFO(pti, i))
			{                 
				ParseTable *pFound = RecursivelyFindNamedChildTable(pti[i].subtable, pChildName);
				if (pFound)
				{
					return pFound;
				}
			}
		}
	}

	return NULL;
}


bool ParseInfoFromDescriptor(ParseTable*** eapti, int* size, const char** name, ParseInfoDescriptor* pid, enumParseTableSendType eSendType)
{
	bool ret = true;
	int i;
	int rootsize;
	char* rootname;
	void **ppExtraAllocations = NULL;

	pid->subAddresses = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
	pid->subDefines = stashTableCreateWithStringKeys(10, StashDefault);

	for (i = 0; i < eaSize(&pid->defines); i++)
	{
		StaticDefineListInfo *define = pid->defines[i];
		if (define)
		{
			void *newTable = StaticDefineFromDescriptor(define, &ppExtraAllocations, eSendType);
			stashAddPointer(pid->subDefines, pid->defines[i]->name, newTable, 0);
			if (eSendType & PARSETABLESENDFLAG_FOR_SCHEMAS)
			{
				RegisterNamedDefineForSchema(newTable, pid->defines[i]->name);
			}
		}
		else
		{
			ret = false;
		}
	}

	// first create the parse info tables
	for (i = 0; i < eaSize(&pid->tables); i++)
	{
		ParseTable* table = ParseInfoFromTable(pid->tables[i],pid, eSendType);
		if (table)
		{
			stashAddPointer(pid->subAddresses, pid->tables[i]->name, table, 0);
			eaPush(eapti, table);
		}
		else 
		{
			ret = false;
		}
	}
	
	// the root table is written as the first table, this is required by the following

	// now do various fixups in order
	for (i = 0; i < eaSizeSlow(eapti); i++)
	{
		ret = ret && ParseInfoFixupSubtablePointers((*eapti)[i], pid); 
	}
	if (ret)
	{
		assert(pid->tables);

		rootsize = ParseInfoCalcOffsets((*eapti)[0], false); 
		rootname = pid->tables[0]->name;
		ParserSetTableInfoRecurse((*eapti)[0], rootsize, rootname, 0, NULL, NULL, (!(eSendType & PARSETABLESENDFLAG_FOR_SCHEMAS)) ? SETTABLEINFO_TABLE_IS_TEMPORARY : 0);
	}
	for (i = 0; i < eaSizeSlow(eapti); i++)
	{
		ret = ret && ParseInfoFixupRedundantNames((*eapti)[i], eSendType);
		ret = ret && ParseInfoFixupMissingReferences((*eapti)[i], eSendType);
		ParserInitIndexedCompareCache((*eapti)[i]);
	}

	// return size & name if desired, calling TableInfo here so we get a stable name address
	
	if (size)
	{
		*size = ParserGetTableSize((*eapti)[0]);
	}

	if (name)
	{
		*name = ParserGetTableName((*eapti)[0]);
	}

	// finally done
	stashTableDestroy(pid->subAddresses);
	stashTableDestroy(pid->subDefines);
	if (!ret) 
	{
		eaDestroyEx(&ppExtraAllocations, NULL);
		ParseTableFree(eapti);
	}
	else
	{
		ParseTableInfo *pInfo = ParserGetTableInfo((*eapti)[0]);
		pInfo->ppExtraAllocations = ppExtraAllocations;
		
	}
	return ret;
}

bool ParseTableWriteTextFile(const char* filename, ParseTable pti[], const char* name, enumParseTableSendType eSendType)
{
	bool ret;
	ParseInfoDescriptor pid = {0};
	ret = ParseInfoToDescriptor(pti, &pid, name, eSendType);
	if (ret) ParserWriteTextFile(filename, tpiParseInfoDescriptor, &pid, 0, 0);
	StructDeInit(tpiParseInfoDescriptor, &pid);
	return ret;
}

bool ParseTableReadTextFile(const char* filename, ParseTable*** eapti, int* size, const char** name, enumParseTableSendType eSendType)
{
	bool ret = true;
	ParseInfoDescriptor pid = {0};
	*eapti = 0;

	ret = ParserReadTextFile(filename, tpiParseInfoDescriptor, &pid, 0);
	if (ret) 
	{
		// 0 means unversioned
		if ((pid.schemaVersion != 0) && (GetSchemaVersion() != 0) && 
			(pid.schemaVersion != GetSchemaVersion()))
		{
			devassertmsg(0,"Your schema files are out of date. Get latest data, or regenerate them by running a GameServer");
			ret = 0;
		}
		if (ret) ret = ret && ParseInfoFromDescriptor(eapti, size, name, &pid, eSendType);
	}
	StructDeInit(tpiParseInfoDescriptor, &pid);
	return ret;
}

bool ParseTableWriteText_dbg(char **ppEString, ParseTable pti[], const char *name, enumParseTableSendType eSendType, const char *caller_fname, int line)
{
	bool ret;
	ParseInfoDescriptor pid = {0};
	PERFINFO_AUTO_START_FUNC();
	ret = ParseInfoToDescriptor(pti, &pid, name, eSendType);
	if (ret)
		ParserWriteText_dbg(ppEString, tpiParseInfoDescriptor, &pid, 0, 0, 0, caller_fname, line);
	StructDeInit(tpiParseInfoDescriptor, &pid);
	PERFINFO_AUTO_STOP();
	return ret;
}
bool ParseTableReadText(const char *pText, ParseTable*** eapti, int* size, const char** name, enumParseTableSendType eSendType)
{
	bool ret;
	ParseInfoDescriptor pid = {0};
	*eapti = 0;

	ret = ParserReadText(pText, tpiParseInfoDescriptor, &pid, 0);
	if (ret) ret = ret && ParseInfoFromDescriptor(eapti, size, name, &pid, eSendType);
	StructDeInit(tpiParseInfoDescriptor, &pid);

	return ret;
}

bool ParseTableSend(Packet* pak, ParseTable pti[], const char* name, enumParseTableSendType eSendType)
{
	bool ret;
	ParseInfoDescriptor pid = {0};
	ret = ParseInfoToDescriptor(pti, &pid, name, eSendType);
	if (ret) ParserSend(tpiParseInfoDescriptor, pak, NULL, &pid, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
	StructDeInit(tpiParseInfoDescriptor, &pid);
	return ret;
}

bool ParseTableRecv(Packet* pak, ParseTable*** eapti, int* size, const char** name, enumParseTableSendType eSendType)
{
	bool ret = true;
	ParseInfoDescriptor pid = {0};
	*eapti = 0;

	ParserRecv(tpiParseInfoDescriptor, pak, &pid, 0);
	ret = ParseInfoFromDescriptor(eapti, size, name, &pid, eSendType);
	StructDeInit(tpiParseInfoDescriptor, &pid);
	return ret;
}

char* SQLReservedWords[] = 
	{ 
		"ACCESSIBLE", "ADD", "ALL", "ALTER", "ANALYZE", "AND", "AS", "ASC", "ASENSITIVE", "BEFORE", "BETWEEN",
		"BIGINT", "BINARY", "BLOB", "BOTH", "BY", "CALL", "CASCADE", "CASE", "CHANGE", "CHAR", "CHARACTER",
		"CHECK", "COLLATE", "COLUMN", "CONDITION", "CONNECTION", "CONSTRAINT", "CONTINUE", "CONVERT", "CREATE",
		"CROSS", "CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIMESTAMP", "CURRENT_USER", "CURSOR", "DATABASE",
		"DATABASES", "DAY_HOUR", "DAY_MICROSECOND", "DAY_MINUTE", "DAY_SECOND", "DEC", "DECIMAL", "DECLARE",
		"DEFAULT", "DELAYED", "DELETE", "DESC", "DESCRIBE",	"DETERMINISTIC", "DISTINCT", "DISTINCTROW", "DIV",
		"DOUBLE", "DROP", "DUAL", "EACH", "ELSE", "ELSEIF", "ENCLOSED", "ESCAPED", "EXISTS", "EXIT", "EXPLAIN",
		"FALSE", "FETCH", "FLOAT", "FLOAT4", "FLOAT8", "FOR", "FORCE", "FOREIGN", "FROM", "FULLTEXT", "GOTO",
		"GRANT", "GROUP", "HAVING", "HIGH_PRIORITY", "HOUR_MICROSECOND", "HOUR_MINUTE", "HOUR_SECOND", "IF",
		"IGNORE", "IN", "INDEX", "INFILE", "INNER", "INOUT", "INSENSITIVE", "INSERT", "INT", "INT1", "INT2",
		"INT3", "INT4", "INT8", "INTEGER", "INTERVAL", "INTO", "IS", "ITERATE",	"JOIN", "KEY", "KEYS", "KILL",
		"LABEL", "LEADING", "LEAVE", "LEFT", "LIKE", "LIMIT", "LINEAR",	"LINES", "LOAD", "LOCALTIME", "LOCALTIMESTAMP",
		"LOCK", "LONG", "LONGBLOB", "LONGTEXT", "LOOP", "LOW_PRIORITY", "MASTER_SSL_VERIFY_SERVER_CERT", "MATCH",
		"MEDIUMBLOB", "MEDIUMINT", "MEDIUMTEXT", "MIDDLEINT", "MINUTE_MICROSECOND", "MINUTE_SECOND", "MOD", 
		"MODIFIES", "NATURAL", "NOT", "NO_WRITE_TO_BINLOG", "NULL", "NUMERIC", "ON", "OPTIMIZE", "OPTION",
		"OPTIONALLY", "OR", "ORDER", "OUT", "OUTER", "OUTFILE",	"PRECISION", "PRIMARY", "PROCEDURE", "PURGE",
		"RANGE", "READ", "READS", "READ_ONLY", "READ_WRITE", "REAL", "REFERENCES", "REGEXP", "RELEASE", 
		"RENAME", "REPEAT", "REPLACE", "REQUIRE", "RESTRICT", "RETURN", "REVOKE", "RIGHT", "RLIKE", "SCHEMA", 
		"SCHEMAS", "SECOND_MICROSECOND", "SELECT", "SENSITIVE",	"SEPARATOR", "SET", "SHOW", "SMALLINT", "SPATIAL", 
		"SPECIFIC", "SQL", "SQLEXCEPTION", "SQLSTATE", "SQLWARNING", "SQL_BIG_RESULT", "SQL_CALC_FOUND_ROWS",
		"SQL_SMALL_RESULT", "SSL", "STARTING", "STRAIGHT_JOIN", "TABLE", "TERMINATED", "THEN", "TINYBLOB", 
		"TINYINT", "TINYTEXT", "TO", "TRAILING", "TRIGGER", "TRUE",	"UNDO", "UNION", "UNIQUE", "UNLOCK",
		"UNSIGNED", "UPDATE", "UPGRADE", "USAGE", "USE", "USING", "UTC_DATE", "UTC_TIME", "UTC_TIMESTAMP",
		"VALUES", "VARBINARY", "VARCHAR", "VARCHARACTER", "VARYING", "WHEN", "WHERE", "WHILE", "WITH", "WRITE",
		"XOR", "YEAR_MONTH", "ZEROFILL" 
	};

static StashTable sqlwords;
static size_t sqlwordsize = 0;
static bool isSQLReservedWord(const char *word)
{
	char *buf = NULL;
	if (!sqlwords)
	{
		int i;
		size_t len;
		sqlwords = stashTableCreate(ARRAY_SIZE(SQLReservedWords) *2, StashDefault, StashKeyTypeStrings, sizeof(void *));
		for (i = 0; i < ARRAY_SIZE(SQLReservedWords); i++)
		{
			stashAddPointer(sqlwords, SQLReservedWords[i], SQLReservedWords[i], false);
			len = strlen(SQLReservedWords[i]);
			if (len > sqlwordsize) sqlwordsize = len;
		}
	}
	if (strlen(word) > sqlwordsize) return false;
	estrStackCreate(&buf);
	estrPrintf(&buf, "%s", word);
	string_toupper(buf);
	if (stashFindPointer(sqlwords, buf, NULL))
	{
		estrDestroy(&buf);
		return true;
	}
	else
	{
		estrDestroy(&buf);
		return false;
	}
}

#define SQL_STATIC_DEFINE_STRING_SIZE 767

StructTypeField ParseInfoColumnsWriteSQL(char **estrOut, ParseInfoDescriptor *pid, ParseInfoColumn *pic)
{
	StructTypeField type = 0;
	bool isnumber = false;
	char *name = NULL;
	int fieldsize = 0;
	//int floatrounding;
	char *default_string = NULL;
	char *ref_table_name = NULL;
	const char *ref_table_prefix = NULL;
	char *ref_foreign_keyname = NULL;
	bool ref_cascade = false;
	bool iskey = false;
	EARRAY_FOREACH_BEGIN(pic->options, opt);
	{
		StructFunctionCall* optionstruct = pic->options[opt];
		char* option = optionstruct->function;
		char* param = optionstruct->params ? optionstruct->params[0]->function: NULL;
		if (!option)
		{
			Errorf("Invalid format encountered in ParseInfoFromTable");
			continue;
		}
		else if (stricmp(option, "IGNORE") == 0)
		{
			return 0;
		}
		else if (stricmp(option, "NAME")==0)
		{
			if (!param) Errorf("Name not given parameter in %s", __FUNCTION__);
			else name = param;
		}
		else if (ParseInfoTypeFromName(option, &type))
		{
			if (TOK_GET_TYPE(type) == TOK_BIT)
			{
				type &= (~TOK_TYPE_MASK);
				type |= TOK_U8_X;
				// Convert bits to U8					
			}
		}
		else if (stricmp(option, "KEY") == 0)
		{
			type |= TOK_KEY;
			iskey = true;
		}
		else if (stricmp(option, "REDUNDANTNAME")==0)
		{
			return 0;
		}
		else if (stricmp(option, "FLOAT_ROUNDING")==0)
		{
			if (!param) Errorf("FLOAT_ROUNDING missing parameter in %s", __FUNCTION__);
			else
			{
				int rounding = StaticDefineIntGetInt(ParseFloatRounding, param);
				if (!rounding)
					Errorf("Invalid param %s to FLOAT_ROUNDING in %s", __FUNCTION__, param);
				//else
				//	pWorkingTPI[i].type |= TOK_FLOAT_ROUNDING(rounding);

//TODO: float rounding
			}
		}
		else if (stricmp(option, "MINBITS")==0) continue;
		//{
		//	if (!param)
		//		Errorf("MINBITS missing parameter in %s", __FUNCTION__);
		//	else
		//	{
		//		pWorkingTPI[i].type |= TOK_MINBITS(atoi(param));
		//	}
		//}
		else if (stricmp(option, "NUM_ELEMENTS")==0 ||
			stricmp(option, "STRING_LENGTH")==0 ||
			stricmp(option, "SIZE")==0)
		{
			//int usage = TYPE_INFO(type).interpretfield(pWorkingTPI, i, ParamField);
			//if (usage != NumberOfElements && 
			//	usage != DefaultValue &&
			//	usage != EmbeddedStringLength &&
			//	usage != SizeOfRawField &&
			//	usage != BitOffset)
			//{
			//	Errorf("Invalid option %s passed to %s. You may need to state the correct type first", option, __FUNCTION__);
			//}
			//else 
			if (!param) Errorf("No param given to option %s in %s", option, __FUNCTION__);
			else fieldsize = atoi(param);
			
		}
		else if (stricmp(option, "DEFAULT")==0)
		{
			if (!param) Errorf("No param given to option %s in %s", option, __FUNCTION__);
			else default_string = param;
		}
		else if (stricmp(option, "DEFAULT_STRING")==0)
		{
			if (!param) Errorf("No param given to option %s in %s", option, __FUNCTION__);
			else default_string = param;
		}			
		else if (stricmp(option, "DICTIONARYNAME")==0)
		{
//TODO: dictionary references
			if (!param) Errorf("No param given to option %s in %s", option, __FUNCTION__);
			else 
			{
				ref_table_name = param;
				ref_foreign_keyname = "dict_id";
				ref_table_prefix = "dc_";
			}
		}
		else if (stricmp(option, "STATICDEFINELIST")==0)
		{
//TODO: define list reference
			if (!param) Errorf("No param given to option %s in %s", option, __FUNCTION__);
			else 
			{
				ref_table_name = param;
				ref_foreign_keyname = "def_key";
				ref_table_prefix = "sd_";
			}
		}
		else if (stricmp(option, "SUBTABLE")==0)
		{
			if (!TOK_HAS_SUBTABLE(type)) Errorf("Option SUBTABLE passed to an invalid field type in %s.  You may need to state the correct type first", __FUNCTION__);
			else if (!param) Errorf("No param given to option %s in %s", option, __FUNCTION__);
			else 
			{
				ref_table_name = param;
				ref_cascade = true;
				ref_foreign_keyname = "struct_id";
				ref_table_prefix = "pt_";
			}
		}
		else if (stricmp(option, "FORMAT")==0) continue;
		//{
		//	if (!param)
		//		Errorf("FORMAT missing parameter in %s", __FUNCTION__);
		//	else
		//	{
		//		int format = StaticDefineIntGetInt(ParseFormatOptions, param);
		//		if (!format)
		//			Errorf("Invalid param %s to FORMAT in %s", param, __FUNCTION__);
		//		else
		//			pWorkingTPI[i].format |= format;
		//	}
		//}
		else if (stricmp(option, "FORMAT_RAW")==0) continue;
		//{
		//	if (!param)
		//		Errorf("FORMAT_RAW missing parameter in %s", __FUNCTION__);
		//	else
		//	{
		//		int format = atoi(param);
		//		if (!format || TOK_GET_FORMAT_OPTIONS(format) != format)
		//			Errorf("Invalid param %s to FORMAT_RAW in %s", param, __FUNCTION__);
		//		else
		//			pWorkingTPI[i].format |= format;
		//	}
		//}
		else if (stricmp(option, "LVWIDTH")==0) continue;
		//{
		//	if (!param)
		//		Errorf("LVWIDTH missing parameter in %s", __FUNCTION__);
		//	else
		//	{
		//		int width = atoi(param);
		//		if (!width)
		//			Errorf("Invalid param %s to LVWIDTH in %s", param, __FUNCTION__);
		//		else
		//			pWorkingTPI[i].format |= TOK_FORMAT_LVWIDTH(width);
		//	}
		//}
		else if (StaticDefineInt_FastStringToInt(ParseFormatUIOptions, option, INT_MIN) != INT_MIN) continue;
		//{
		//	pWorkingTPI[i].format |= StaticDefineIntGetInt(ParseFormatUIOptions, option);
		//}
	}
	EARRAY_FOREACH_END;

	estrPrintf(estrOut, "    ");

	if (type & TOK_EARRAY || TOK_GET_TYPE(type) == TOK_STRUCT_X)
		estrConcatf(estrOut, "-- EArray ");

	if (iskey)
	{
		estrConcatf(estrOut, "struct_id ");
		if (!fieldsize) fieldsize = 255;
	}
	else if (isSQLReservedWord(name))
	{
		estrConcatf(estrOut, "tp_%s ", name);
	}
	else
	{
		estrConcatf(estrOut, "%s ", name);
	}

	switch (TOK_GET_TYPE(type))
	{
				// primitives - param=default value
	case TOK_U8_X:			// U8 (unsigned char)
		estrConcatf(estrOut, "TINYINT UNSIGNED "); isnumber = true; break;
	case TOK_INT16_X:		// 16 bit integer
		estrConcatf(estrOut, "SMALLINT ");  isnumber = true; break;
	case TOK_INT_X:			// int
		estrConcatf(estrOut, "INT "); isnumber = true; break;
	case TOK_INT64_X:		// 64 bit integer
		estrConcatf(estrOut, "BIGINT ");  isnumber = true; break;
	case TOK_F32_X:			// F32 (float), can be initialized with <param> but you only get an integer value
		estrConcatf(estrOut, "FLOAT ");  isnumber = true; break;
	case TOK_STRING_X:		// char*
		estrConcatf(estrOut, "VARCHAR(%d) CHARACTER SET utf8 ", (fieldsize?fieldsize:SQL_STATIC_DEFINE_STRING_SIZE)); break;

			// built-ins
	case TOK_CURRENTFILE_X:	// stored as char*, filled with filename of currently parsed text file
		//TODO
		estrConcatf(estrOut, "VARCHAR(%d) ", MAX_PATH); break;
	case TOK_TIMESTAMP_X:	// stored as int, filled with fileLastChanged() of currently parsed text file
		estrConcatf(estrOut, "TIMESTAMP "); break;
	case TOK_LINENUM_X:		// stored as int, filled with line number of the currently parsed text file
		estrConcatf(estrOut, "INT "); isnumber = true; break;
	case TOK_BOOL_X:			// stored as u8, restricted to 0 or 1
		estrConcatf(estrOut, "BIT(1) "); isnumber = true; break;
	case TOK_BOOLFLAG_X:		// int, no parameters in script file, if token exists, field is set to 1
		estrConcatf(estrOut, "SMALLINT "); isnumber = true; break;
	case TOK_QUATPYR_X:		// F32[4], quaternion, read in as a pyr
		//TODO
		estrDestroy(estrOut); return false;
	case TOK_MATPYR_X:		// F32[3][3] in memory turns into F32[3] (PYR) when serialized
		//TODO
		 estrDestroy(estrOut); return false;
	case TOK_FILENAME_X:		// same as string, passed through forwardslashes & _strupr
		estrConcatf(estrOut, "VARCHAR(%d) ", MAX_PATH); break;

			// complex types
	case TOK_REFERENCE_X:	// YourStruct*, subtable is dictionary name
		estrConcatf(estrOut, "INT "); break;
	case TOK_FUNCTIONCALL_X:	// StructFunctionCall**, parenthesis in input signals hierarchal organization
		estrDestroy(estrOut); return false;
	case TOK_STRUCT_X:		// YourStruct**, pass size as parameter, use eaSize to get number of items
		estrConcatf(estrOut, "INT "); break;

	case TOK_POLYMORPH_X:	// YourStruct**, as TOK_STRUCT, but subtable points to tpi list of possible substructures
		//Not currently supported in the ObjectDB
		estrDestroy(estrOut); return false;
	case TOK_STASHTABLE_X:	// StashTable
		//TODO
		estrDestroy(estrOut); return false;
	case TOK_BIT:			// A bitfield... only generated by AUTOSTRUCT
		estrConcatf(estrOut, "BIT(%d) ", (fieldsize?fieldsize:32)); break;
	case TOK_MULTIVAL_X:		// A variant type used by the expression system
		estrConcatf(estrOut, "VARCHAR(%d) CHARACTER SET utf8 ", (fieldsize?fieldsize:SQL_STATIC_DEFINE_STRING_SIZE)); break;

	default:
		Errorf("SQL Dump could not identify type field: %"FORM_LL"d for field %s", type, name); 
		printf("SQL Dump could not identify type field: %"FORM_LL"d for field %s", type, name);
		return false;
	}

	if (default_string)
	{
		if (isnumber) estrConcatf(estrOut, "DEFAULT %s ", default_string);
		else estrConcatf(estrOut, "DEFAULT '%s' ", default_string);
	}

	if (iskey) estrConcatf(estrOut, "PRIMARY KEY ");
	

	if (ref_table_name && ref_table_name[0])
	{
		char *tblname = NULL;
		const char *dot;
		estrStackCreate(&tblname);
		estrPrintf(&tblname, "%s", ref_table_name);
		dot = strrchr(tblname, '.');
		if (dot)
		{
			tblname[dot-tblname] = '_';
			estrRemoveUpToLastOccurrence(&tblname, '.');
		}
		estrInsert(&tblname, 0, ref_table_prefix, (int)strlen(ref_table_prefix));

		//if (type & TOK_EARRAY || 
		//	TOK_GET_TYPE(type) == TOK_STRUCT_X ||
		//	TOK_GET_TYPE(type) == TOK_FLAGS_X)
			estrConcatf(estrOut, "\n    --");
		//else
		//	estrConcatf(estrOut, "\n    ");
		estrConcatf(estrOut, "  FOREIGN KEY (%s) REFERENCES %s (%s) ", name, tblname, ref_foreign_keyname);
		estrDestroy(&tblname);
	}

	return type;
}

char * ParserWriteSQL_dbg(char **ppEString, ParseTable pti[], void *strptr, U64 *ids, U32 opts, const char *caller_fname, int line)
{
	U64 myID = 0;
	char *buf = NULL;
	char *val = NULL;
	char *key = NULL;
	FILE *fpBuff = NULL;
	int columns = 0;
	int i;

	if (!strptr || !pti || ! ppEString)
		return 0;

	estrStackCreate(&val);
	estrStackCreate(&buf);
	estrPrintf(&buf, "INSERT INTO pt_%s SET\n", pti[0].name);

	if (opts & PARSER_WRITE_SQL_SUBSTRUCT)
	{
		myID = ++(*ids);
		estrPrintf(&key, " %"FORM_LL"u", myID);
		estrConcatf(&buf, "  struct_id=%s", key);
		columns++;
	}
	else
	{
		FORALL_PARSETABLE(pti, i)
		{
			if (~pti[i].type & TOK_KEY) continue;

			estrCreate(&key);
			fpBuff = fileOpenEString_dbg(&key, caller_fname, line);
			writetext_autogen(fpBuff, pti, i, strptr, -1, false, true, 0, WRITETEXTFLAG_WRITINGFORSQL, 0, 0);
			fileClose(fpBuff);

			if (TOK_GET_TYPE(pti[i].type) == TOK_STRING_X) myID = -1;
			else if (!(myID = atoi64(key))) 
			{
				myID = ++(*ids);
				estrPrintf(&key, " %"FORM_LL"u", myID);
			}
			if (key[0])
			{
				estrConcatf(&buf, "  struct_id=%s", key);
				columns++;
			}
			break;
		}
		if (!myID)
		{
			myID = ++(*ids);
			estrPrintf(&key, " %"FORM_LL"u", myID);
			estrConcatf(&buf, "  struct_id=%s", key);
			columns++;
		}
	}

	FORALL_PARSETABLE(pti, i)
	{
		StructTypeField type = pti[i].type;
		U32 storage = TokenStoreGetStorageType(type);
		const char *name = pti[i].name;

		if (type == TOK_IGNORE) continue;
		if (~type & TOK_PERSIST) continue;
		if (type & TOK_REDUNDANTNAME) continue;
		if (type & TOK_USEDFIELD) continue;
		if (type & TOK_UNOWNED) continue;
		if (type & TOK_KEY) continue;

		if (TokenStoreStorageTypeIsAnArray(storage))
		{
			if (TOK_GET_TYPE(type) == TOK_STRUCT_X && pti[i].type & TOK_EARRAY)
			{
				void **earr = *TokenStoreGetEArray_inline(pti, &pti[i], i, strptr, NULL);
				if (earr)
				{
					ParseTable *subtable = pti[i].subtable;
					EARRAY_FOREACH_BEGIN(earr, ei);
					{
						char * subkey = ParserWriteSQL(ppEString, subtable, earr[ei], ids, PARSER_WRITE_SQL_SUBSTRUCT);
						if (subkey && myID > 0 && atoi64(subkey))
						{
							estrConcatf(ppEString, "INSERT INTO PivotTable VALUES (%s, %s, 'pt_%s', 'pt_%s', '%s');\n",
								key, subkey, pti[0].name, subtable[0].name, name);
						}
						if (subkey) estrDestroy(&subkey);
					}
					EARRAY_FOREACH_END;
				}
			}
			continue;
		}
	
		

		if (TOK_HAS_SUBTABLE(type))
		{
			if (type & TOK_REFERENCE_X)
			{	//handle individual structs
				void *substruct = TokenStoreGetPointer_inline(pti, &pti[i], i, strptr, -1, NULL);

				if (substruct)
				{
					ParseTable *subtable = pti[i].subtable;
					char * subkey = ParserWriteSQL(ppEString, subtable, substruct, ids, PARSER_WRITE_SQL_SUBSTRUCT);
					if (subkey && myID > 0 && atoi64(subkey))
					{
						estrConcatf(ppEString, "INSERT INTO PivotTable VALUES (%s, %s, 'pt_%s', 'pt_%s', '%s');\n",
							key, subkey, pti[0].name, subtable[0].name, name);
					}
					if (subkey) estrDestroy(&subkey);
				}
				continue;
			}
		}

		estrClear(&val);
		fpBuff = fileOpenEString_dbg(&val, caller_fname, line);
		writetext_autogen(fpBuff, pti, i, strptr, -1, false, true, 0, WRITETEXTFLAG_WRITINGFORSQL, 0, 0);
		fileClose(fpBuff);


		if (val[0])
		{
			bool isquoted = false;
			estrTrimLeadingAndTrailingWhitespace(&val);
			if (val[0] == '"' && val[estrLength(&val)-1] == '"') 
			{
				isquoted = true;
				val[0] = '\b';
				val[estrLength(&val)-1] = '\b';
			}
			estrReplaceOccurrences(&val, "\\", "\\\\");
			estrReplaceOccurrences(&val, "\"", "\\\"");
			if (isquoted)
			{
				val[0] = '"';
				val[estrLength(&val)-1] = '"';
			}
			if (columns) estrConcatf(&buf, ",\n");
			if (isSQLReservedWord(name))
				estrConcatf(&buf, "  tp_%s=%s", name, val);
			else
				estrConcatf(&buf, "  %s=%s", name, val);

			columns++;
		}
	}

	if (columns)
	{
		estrConcatf(ppEString, "%s;\n\n", buf);
	}
	else
	{
		estrDestroy(&key);
		key = NULL;
	}

	estrDestroy(&buf);
	estrDestroy(&val);

	return key;
}

bool ParseTableWriteSQL_dbg(char **ppEString, ParseTable pti[], const char* name, const char *caller_fname, int line)
{
	bool ret;
	char *buf = NULL;
	char *rowbuf = NULL;
	ParseInfoDescriptor pid = {0};
	ret = ParseInfoToDescriptor(pti, &pid, name, PARSETABLESENDFLAG_FOR_SQL);
	if (!ret)
	{
		StructDeInit(tpiParseInfoDescriptor, &pid);
		return ret;
	}

	estrStackCreate(&buf);
	estrStackCreate(&rowbuf);
	
	//Handle the static defines first
	EARRAY_FOREACH_BEGIN(pid.defines, i);
	{
		StashTable keys = stashTableCreate(8, StashDefault, StashKeyTypeStrings, sizeof(void*));
		estrPrintf(&buf, "sd_%s", pid.defines[i]->name);
		estrReplaceOccurrences(&buf, ".", "_");
		estrConcatf(ppEString, 
			"DROP TABLE IF EXISTS `%s`;\nCREATE TABLE `%s` (\n",
			buf, buf);

		switch (pid.defines[i]->type) {
			case DM_INT: estrConcatf(ppEString, "    def_key INT,\n"); break;
			case DM_STRING: estrConcatf(ppEString, "    def_key VARCHAR(%d) KEY,\n",SQL_STATIC_DEFINE_STRING_SIZE); break;
		}

		estrConcatf(ppEString, "    def_val VARCHAR(%d),\nPRIMARY KEY(def_key)\n); \n\n", SQL_STATIC_DEFINE_STRING_SIZE);

		estrConcatf(ppEString, "  INSERT INTO %s (def_key, def_val)\n    VALUES ", buf);

		EARRAY_FOREACH_BEGIN(pid.defines[i]->entries, j);
		{
			if (stashAddPointer(keys,pid.defines[i]->entries[j]->value, pid.defines[i]->entries[j]->name, false))
			{
				estrConcatf(ppEString, "%s\n      (%s, \'%s')",
					(j?",":""),
					pid.defines[i]->entries[j]->value,
					pid.defines[i]->entries[j]->name
					);
			}
		}
		EARRAY_FOREACH_END;

		stashTableDestroy(keys);

		estrConcatf(ppEString, ";\n\n");
	}
	EARRAY_FOREACH_END;

	estrConcatf(ppEString, 
		"DROP TABLE IF EXISTS PivotTable;\n"
		"CREATE TABLE IF NOT EXISTS PivotTable (\n"
		"  parent_id BIGINT,\n"
		"  child_id BIGINT,\n"
		"  parent_table VARCHAR(256),\n"
		"  child_table VARCHAR(256),\n"
		"  child_field VARCHAR(256)\n"
		") ENGINE MyISAM;\n\n"
		);


	//Then the regular ParseTables
	EARRAY_FOREACH_BEGIN(pid.tables, i);
	{
		bool hasKey = false;
		bool fieldcount = 0;
		estrClear(&buf);
		EARRAY_FOREACH_REVERSE_BEGIN(pid.tables[i]->columns, j);
		{
			StructTypeField type = 0;
			estrClear(&rowbuf);
			if (type = ParseInfoColumnsWriteSQL(&rowbuf, &pid, pid.tables[i]->columns[j]))
			{
				estrConcatf(&rowbuf, "\n");
				
				if (~type & TOK_EARRAY && TOK_GET_TYPE(type) != TOK_STRUCT_X)
				{
					if (fieldcount) estrReplaceOccurrences(&rowbuf, "\n", ",\n");
					fieldcount++;
				}

				if (type & TOK_KEY)
					hasKey = true;

				estrInsert(&buf, 0, rowbuf, estrLength(&rowbuf));
			}
		}
		EARRAY_FOREACH_END;

		if (!hasKey)
		{
			estrPrintf(&rowbuf, "    struct_id BIGINT PRIMARY KEY%s\n", (fieldcount?",":""));
			estrInsert(&buf, 0, rowbuf, estrLength(&rowbuf) );
		}

		estrPrintf(&rowbuf, "%s", pid.tables[i]->name);
		estrInsert(&rowbuf, 0, "pt_", 3);
		estrConcatf(ppEString, 
			"DROP TABLE IF EXISTS `%s`;\nCREATE TABLE `%s` (\n%s) ENGINE MyISAM;\n\n",
			rowbuf, rowbuf, buf);

	}
	EARRAY_FOREACH_END;

	estrDestroy(&rowbuf);
	estrDestroy(&buf);

	StructDeInit(tpiParseInfoDescriptor, &pid);
	return ret;

}

/////////////////////////////////////////////////// textparser path functions

#define MAX_ROOT_PATH_FUNC 5
static RootPathLookupFunc g_rootpathtable[MAX_ROOT_PATH_FUNC] = {0};

void ParserRegisterRootPathFunc(RootPathLookupFunc function)
{
	int i;
	for (i = 0; i < MAX_ROOT_PATH_FUNC; i++)
		if (!g_rootpathtable[i])
		{
			g_rootpathtable[i] = function;
			return;
		}
	assert(0 && "Increase MAX_ROOT_PATH_FUNC as necessary");
}

AUTO_RUN;
int RegisterDefaultRootPathFunc(void)
{
	ParserRegisterRootPathFunc(RefSystem_RootPathLookup);
	ParserRegisterRootPathFunc(objContainerRootPathLookup);
	return 0;
}


bool ParserRootPathLookup(const char* name, const char *key, ParseTable** table, void** structptr, int* column)
{
	int i;
	for (i = 0; i < MAX_ROOT_PATH_FUNC; i++)
	{
		RootPathLookupResult result;
		if (!g_rootpathtable[i]) 
		{
			break;
		}
		result = g_rootpathtable[i](name, key, table, structptr, column);
		if (result == ROOTPATH_FOUND)
		{
			return true;
		}
		else if (result == ROOTPATH_NOTFOUND)
		{
			return false;
		}
	}
	return false;
}


// do a key lookup on this struct and return the resulting index
// - table[column] is required to be an earray of structs
bool ParserResolveKey(const char* key, ParseTable table[], int column, void* structptr, int* index, U32 iObjPathFlags, char **resultString, ObjectPathSegment *top)
{
	ParseTable* subtable = NULL;
	int keyfield = -1;
	int i, numelems;
	char buf[MAX_TOKEN_LENGTH];
	char *endptr;
	int keyInt;
	bool keyIsInt;

	PERFINFO_AUTO_START_FUNC_L2();
	
	if (ParserColumnIsIndexedEArray(table, column, &keyfield))
	{
		if (top) {
			top->key = allocAddString(key);
		}

		if (structptr)
		{
			void ***earray = TokenStoreGetEArray_inline(table, &table[column], column, structptr, NULL);
			int iIntKey = 0;
			StructGetSubtable(table, column, NULL, 0, &subtable, 0);


			// If the key field is an enumerated type, we need to translate the string into its real value...
			if (TYPE_INFO(subtable[keyfield].type).interpretfield(subtable, keyfield, SubtableField) == StaticDefineList)
			{
				if (subtable[keyfield].subtable)
				{
					if (TOK_GET_TYPE(subtable[keyfield].type) == TOK_INT_X)
					{
						iIntKey = StaticDefineInt_FastStringToInt(subtable[keyfield].subtable, key, 0);
					}
					else
					{
						const char *possibleKey = StaticDefineLookup(subtable[keyfield].subtable, key);
						if (possibleKey)
							key = possibleKey;
					}
				}
			}

			if (iIntKey)
			{
				*index = eaIndexedFindUsingInt(earray, iIntKey);
			}
			else
			{
				*index = eaIndexedFindUsingString(earray, key);
			}

			if (*index < 0)
			{
				if ((iObjPathFlags & OBJPATHFLAG_CREATESTRUCTS))
				{				
					void *subStruct = StructCreateVoid(table[column].subtable);

					FieldReadText(table[column].subtable,keyfield,subStruct,0,key);

					if (!*earray)
					{						
						const char *pTPIName = ParserGetTableName(table);							
						eaIndexedEnable_dbg(earray,table[column].subtable, pTPIName?pTPIName:__FILE__, pTPIName?LINENUM_FOR_EARRAYS:__LINE__);
					}

					if (!eaIndexedAdd(earray,subStruct))
					{
						StructDestroyVoid(table[column].subtable,subStruct);
					}
					*index = eaIndexedFindUsingString(earray, key);
					if (&index >= 0)
					{
						PERFINFO_AUTO_STOP_L2();
						return true;
					}
				}
				if (iObjPathFlags & OBJPATHFLAG_GETFINALKEY)
				{
					// Okay for this to be invalid
					PERFINFO_AUTO_STOP_L2();
					return true;
				}
				SETERROR(PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_FULL, key);
				PERFINFO_AUTO_STOP_L2();
				return false;
			}
			else
			{
				PERFINFO_AUTO_STOP_L2();
				return true;
			}
		}
		else
		{
			*index = -1;
			PERFINFO_AUTO_STOP_L2();
			return true;
		}

	}

	if (structptr)
	{
		numelems = TokenStoreGetNumElems_inline(table, &table[column], column, structptr, NULL);
	}
	else
	{
		numelems = 0;
	}
	keyInt = strtol(key, &endptr, 10);
	keyIsInt = !(endptr && *endptr);

	// If the key is an integer, it is the index, and it just needs to be less than the element count.
	// However, indexed earrays *cannot* be indexed by number.
	if (keyIsInt)
	{
		*index = keyInt;
		if (top)
		{
			top->index = keyInt;
		}
		if (!structptr)
		{
			*index = -1;
			PERFINFO_AUTO_STOP_L2();
			return true;
		}
		if (keyInt >= numelems)
		{
			if (structptr && (iObjPathFlags & OBJPATHFLAG_CREATESTRUCTS))
			{
				void ***earray = TokenStoreGetEArray_inline(table, &table[column], column, structptr, NULL);

				while (keyInt >= eaSize(earray))
				{
					void *subStruct = StructCreateVoid(table[column].subtable);
					eaPush(earray, subStruct);
				}
				PERFINFO_AUTO_STOP_L2();
				return true;
			}
			
			if (iObjPathFlags & OBJPATHFLAG_GETFINALKEY)
			{
				// Okay for this to be invalid
				*index = -1;
				PERFINFO_AUTO_STOP_L2();
				return true;
			}
			SETERROR(PARSERRESOLVE_INDEX_BEYOND_BOUNDS_FULL, keyInt, numelems);
			PERFINFO_AUTO_STOP_L2();
			return false;
		}
		else
		{
			PERFINFO_AUTO_STOP_L2();
			return true;
		}
	}

	if (top) {
		top->key = allocAddString(key);
	}

	if (!(iObjPathFlags & OBJPATHFLAG_SEARCHNONINDEXED) || !TOK_HAS_SUBTABLE(table[column].type) )
	{
		// Not an int, and we aren't allowed to search non indexed arrays, or this isn't a struct array
		SETERROR(PARSERRESOLVE_NO_KEY_OR_INDEX);
		PERFINFO_AUTO_STOP_L2();
		return false;
	}

	if (!structptr)
	{
		*index = -1;
		PERFINFO_AUTO_STOP_L2();
		return true;
	}

	// Otherwise, we need to iterate over the whole array.
	for (i = 0; i < numelems; i++)
	{
		void* substruct = StructGetSubtable(table, column, structptr, i, &subtable, NULL);
		if (!substruct)
			continue;
		keyfield = ParserGetTableKeyColumn(subtable);
		assertmsg(keyfield >= 0, "Some polymorph types of have a key field, but some do not?? BAD");
		if (TokenToSimpleString(subtable, keyfield, substruct, SAFESTR(buf), false) && !stricmp(buf, key))
		{			
			*index = i;
			PERFINFO_AUTO_STOP_L2();
			return true;
		}
	}
	SETERROR(PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_FULL, key);
	PERFINFO_AUTO_STOP_L2();
	return false;
}	

bool ParserAppendPath(char **estrPath, ParseTable tpi[], int column, SA_PARAM_NN_VALID void* structptr, int index)
{
	int keyColumn;
	if (!tpi || !column || !estrPath)
	{
		return false;
	}
	if (!(tpi[column].type & TOK_EARRAY || tpi[column].type & TOK_FIXED_ARRAY))
	{
		estrConcatf(estrPath, ".%s", tpi[column].name);
		return true;
	}
	else if (tpi[column].type & TOK_FIXED_ARRAY)
	{
		estrConcatf(estrPath, ".%s[%d]", tpi[column].name, index);
		return true;
	}
	if (ParserColumnIsIndexedEArray(tpi, column, &keyColumn))
	{
		ParseTable *subtable;
		void *subObject = StructGetSubtable(tpi, column, structptr, index, &subtable, NULL);	
		estrConcatf(estrPath, ".%s[\"", tpi[column].name);
		if (!objGetEscapedKeyEString(subtable, subObject, estrPath))
		{
			return false;
		}
		estrConcatf(estrPath, "\"]");
		return true;
	}
	estrConcatf(estrPath, ".%s[%d]", tpi[column].name, index);
	return true;
}


bool ParserFindColumnWithFlag(ParseTable table[], StructTypeField flag, int *column)
{
	int i;
	
	FORALL_PARSETABLE(table, i)
	{
		if (table[i].type & flag)
		{
			if (column)
			{
				*column = i;
			}
			return true;
		}
	}
	return false;
}

bool ParserFindColumnWithType(ParseTable table[], StructTypeField type, int *column)
{
	int i;

	FORALL_PARSETABLE(table, i)
	{
		if (TOK_GET_TYPE(table[i].type) == type)
		{
			if (column)
			{
				*column = i;
			}
			return true;
		}
	}
	return false;
}


bool ParserFindColumn(ParseTable table[], const char* colname, int* column)
{
	int i;
	const char *cachedName = allocFindString(colname);
	if (!cachedName)
	{
		return false; // Not in cache, can't exist
	}
	FORALL_PARSETABLE(table, i)
	{
		if ((TOK_GET_TYPE(table[i].type) != TOK_IGNORE) && table[i].name == cachedName)
		{
			if (column) *column = i;
			return true;
		}
	}
	return false;
}

bool ParserFindIgnoreColumn(ParseTable table[], const char *colname)
{
	int i;
	const char *cachedName = allocFindString(colname);
	if (!cachedName)
	{
		return false; // Not in cache, can't exist
	}
	FORALL_PARSETABLE(table, i)
	{
		if ((TOK_GET_TYPE(table[i].type) == TOK_IGNORE) && table[i].name == cachedName)
		{
			return true;
		}
	}
	return false;
}


int ParserFindColumnFromOffset(ParseTable table[], size_t offset)
{
	int i;
	FORALL_PARSETABLE(table, i)
	{
		if ((TOK_GET_TYPE(table[i].type) != TOK_IGNORE) && table[i].storeoffset == offset)
		{
			return i;
		}
	}
	return -1;
}

const char *ParserGetFilename(ParseTable *pti, void *structptr)
{
	int i;
	if (!structptr)
		return NULL;
	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_GET_TYPE(pti[i].type) == TOK_CURRENTFILE_X)
			return TokenStoreGetString_inline(pti, &pti[i], i, structptr, 0, NULL);
	}	
	return NULL;
}

void ParserSetFilename(ParseTable *pti, void *structptr, const char* filename)
{
	int i;
	if (!structptr)
		return;
	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_GET_TYPE(pti[i].type) == TOK_CURRENTFILE_X) {
			TokenStoreSetString(pti, i, structptr, 0, allocAddString(filename), NULL, NULL, NULL, NULL );
			return;
		}
	}
}

#define COMPAREINT(a,b) ((a) < (b)? -1 : ((a) == (b) ? 0 : 1))

static int CompareStringField(const ParserSortData *context, const void ** a, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	const char *aKey = TokenStoreGetString_inline(tpi,&tpi[column],column,(void *)*a,0, NULL);
	const char *bKey = TokenStoreGetString_inline(tpi,&tpi[column],column,(void *)*b,0, NULL);
	return inline_stricmp(aKey?aKey:"",bKey?bKey:"");
}

static int StringSearchStringField(ParserSortData *context, const char * aKey, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	const char *bKey = TokenStoreGetString_inline(tpi,&tpi[column],column,(void *)*b,0, NULL);
	return inline_stricmp(aKey?aKey:"",bKey?bKey:"");
}

static int IntSearchStringField(ParserSortData *context, const S64 *intPtr, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	char aKey[32];
	const char *bKey = TokenStoreGetString_inline(tpi,&tpi[column],column,(void *)*b,0, NULL);
	sprintf(aKey,"%"FORM_LL"d",*intPtr);
	return stricmp(aKey?aKey:"",bKey?bKey:"");
}

static int CompareIntField(const ParserSortData *context, const void ** a, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	int aKey = TokenStoreGetInt_inline(tpi,&tpi[column],column,(void *)*a,0, NULL);
	int bKey = TokenStoreGetInt_inline(tpi,&tpi[column],column,(void *)*b,0, NULL);
	return COMPAREINT(aKey, bKey);
}

static int CompareIntFieldAuto(const ParserSortData *context, const void ** a, const void** b)
{
	S64 aKey = TokenStoreGetIntAuto(context->tpi,context->column,(void *)*a,0, NULL);
	S64 bKey = TokenStoreGetIntAuto(context->tpi,context->column,(void *)*b,0, NULL);
	return COMPAREINT(aKey, bKey);
}



static int StringSearchIntField(ParserSortData *context, const char * aString, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	int aKey;
	int bKey = TokenStoreGetInt_inline(tpi,&tpi[column],column,(void *)*b,0, NULL);
	StaticDefine *list = ParserColumnGetStaticDefineList(tpi, column);
	if (list)
	{
		const char *pNewString = StaticDefineLookup(list, aString);
		if (pNewString) 
			aString = pNewString;
	}
	aKey = atoi(aString);
	
	return COMPAREINT(aKey, bKey);
}

static int IntSearchIntField(ParserSortData *context, const S64 *intPtr, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	S64 aKey = *intPtr;
	S64 bKey = (S64)TokenStoreGetInt_inline(tpi,&tpi[column],column,(void *)*b,0, NULL);
	return COMPAREINT(aKey, bKey);
}

static int CompareInt64Field(const ParserSortData *context, const void ** a, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	S64 aKey = TokenStoreGetInt64_inline(tpi, &tpi[column], column, (void *)*a, 0, NULL);
	S64 bKey = TokenStoreGetInt64_inline(tpi, &tpi[column], column, (void *)*b, 0, NULL);
	return COMPAREINT(aKey, bKey);
}

static int StringSearchInt64Field(ParserSortData *context, const char * aString, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	S64 aKey;
	S64 bKey = TokenStoreGetInt64_inline(tpi,&tpi[column],column,(void *)*b,0, NULL);
	StaticDefine *list = ParserColumnGetStaticDefineList(tpi, column);
	if (list)
	{
		const char *pNewString = StaticDefineLookup(list, aString);
		if (pNewString) 
			aString = pNewString;
	}
	aKey = atoi64(aString);

	return COMPAREINT(aKey, bKey);
}

static int IntSearchInt64Field(ParserSortData *context, const S64 *intPtr, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	S64 aKey = *intPtr;
	S64 bKey = TokenStoreGetInt64_inline(tpi,&tpi[column],column,(void *)*b,0, NULL);
	return COMPAREINT(aKey, bKey);
}

static int CompareRefField(const ParserSortData *context, const void ** a, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	const char *aKey, *bKey;
	aKey = TokenStoreGetRefString_inline(tpi, &tpi[column], column, (void *)*a, 0, NULL);
	bKey = TokenStoreGetRefString_inline(tpi, &tpi[column], column, (void *)*b, 0, NULL);
	if (!aKey)
	{
		if (!bKey)
			return 0;
		return -1;
	}
	else if (!bKey)
	{
		return 1;
	}

	return stricmp(aKey,bKey);
}

static int StringSearchRefField(ParserSortData *context, const char * aKey, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	const char *bKey = TokenStoreGetRefString_inline(tpi, &tpi[column], column, (void *)*b, 0, NULL);
	if (!aKey)
	{
		if (!bKey)
			return 0;
		return -1;
	}
	else if (!bKey)
	{
		return 1;
	}

	return stricmp(aKey,bKey);
}

static int IntSearchRefField(ParserSortData *context, const S64 *intPtr, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	char aKey[32];
	const char *bKey = TokenStoreGetRefString_inline(tpi, &tpi[column], column, (void *)*b, 0, NULL);
	sprintf(aKey,"%"FORM_LL"d",*intPtr);
	if (!aKey)
	{
		if (!bKey)
			return 0;
		return -1;
	}
	else if (!bKey)
	{
		return 1;
	}

	return stricmp(aKey,bKey);
}


static int CompareStructField(const ParserSortData *context, const void ** a, const void** b)
{
	ParseTable *tpi = context->tpi;
	int column = context->column;
	ParseTable *ptcc = &context->tpi[column];
	const void *aKey = TokenStoreGetPointer_inline(tpi,ptcc,column,(void *)*a,0, NULL);
	const void *bKey = TokenStoreGetPointer_inline(tpi,ptcc,column,(void *)*b,0, NULL);
	return StructCompare(ptcc->subtable, aKey, bKey, 0, 0, 0);
}

static int StringSearchStructField(ParserSortData *context, const char * aKey, const void** b)
{
	int result;
	char *bKey = NULL;
	ParseTable *tpi = context->tpi;
	int column = context->column;
	ParseTable *ptcc = &context->tpi[column];
	void *bObject = TokenStoreGetPointer_inline(tpi,ptcc,column,(void *)*b,0, NULL);
	estrStackCreate(&bKey);
	ParserWriteText(&bKey, ptcc->subtable, bObject, 0, 0, 0);
	result = stricmp(aKey?aKey:"",bKey?bKey:"");
	estrDestroy(&bKey);
	return result;
}

ParserCompareFieldFunction ParserGetCompareFunction(ParseTable tpi[], int column)
{
	if (column < 0)
	{
		return NULL;
	}
	switch (TOK_GET_TYPE(tpi[column].type))
	{
	case TOK_STRING_X:
	case TOK_CURRENTFILE_X:
	case TOK_FILENAME_X:
		return CompareStringField;
	case TOK_U8_X:
	case TOK_INT16_X:
	case TOK_INT_X:
	case TOK_INT64_X:
		return CompareIntFieldAuto;
	case TOK_REFERENCE_X:
		return CompareRefField;
	case TOK_STRUCT_X:
		return CompareStructField;
	default:
		return NULL;
	}
}

ParserSearchStringFunction ParserGetStringSearchFunction(ParseTable tpi[], int column)
{
	if (column < 0)
	{
		return NULL;
	}
	switch (TOK_GET_TYPE(tpi[column].type))
	{
	case TOK_STRING_X:
	case TOK_CURRENTFILE_X:
	case TOK_FILENAME_X:
		return StringSearchStringField;
	case TOK_INT_X:
		return StringSearchIntField;
	case TOK_INT64_X:
		return StringSearchInt64Field;
	case TOK_REFERENCE_X:
		return StringSearchRefField;
	case TOK_STRUCT_X:
		return StringSearchStructField;
	default:
		return NULL;
	}
}

ParserSearchIntFunction ParserGetIntSearchFunction(ParseTable tpi[], int column)
{
	if (column < 0)
	{
		return NULL;
	}
	switch (TOK_GET_TYPE(tpi[column].type))
	{
	case TOK_STRING_X:
	case TOK_CURRENTFILE_X:
	case TOK_FILENAME_X:
		return IntSearchStringField;
	case TOK_INT_X:
		return IntSearchIntField;
	case TOK_INT64_X:
		return IntSearchInt64Field;
	case TOK_REFERENCE_X:
		return IntSearchRefField;
	default:
		return NULL;
	}
}



// Eats a token, unescaping if it's in quotes
//
// counts colons as legal identifier tokens, as they are frequently used in
// field names.
enumReadObjectPathIdentifierResult ReadObjectPathIdentifier(char **estrOut, const char** path, bool *quoted)
{
	int read;
	bool found = false;
	const char* src = *path;
	const char* end;

	if (!src || !src[0]) return READOBJPATHIDENT_PARSEFAIL;
	while (src[0] == ' ') src++;
	if (src[0] == '[')
	{
		bool bQuoted = false;
		const char *keyend;
		src++;
		if (src[0] == '"') 
		{
			if (quoted) *quoted = 1;
			bQuoted = true;
			src++;
			keyend = strchr_fast(src, '"');			
			if (!keyend) return READOBJPATHIDENT_PARSEFAIL;
		}
		else
		{
			while (src[0] == ' ') src++;
			keyend = src;
		}

		end = strchr_fast(keyend, ']');
		if (!end) return READOBJPATHIDENT_PARSEFAIL;

		if (!bQuoted)
		{	
			keyend = end;
		
			while (keyend >= src && (keyend[-1] == ' ' || keyend[-1] == '"')) keyend--;
		}

		read = estrAppendUnescapedCount(estrOut, src, keyend - src);

		*path = end + 1;

		if (read == 0)
		{
			return READOBJPATHIDENT_EMPTYKEY;
		}
		else
		{
			return READOBJPATHIDENT_OK;
		}
	}
	else if (src[0] == '"')
	{	
		src++;
		end = strchr_fast(src, '"');
		if (!end)
		{
			return READOBJPATHIDENT_PARSEFAIL;
		}

		read = estrAppendUnescapedCount(estrOut, src, end - src);

		*path = end + 1;

		if (quoted)
			*quoted = 1;

		if (read == 0)
		{
			return READOBJPATHIDENT_EMPTYKEY;
		}
		else
		{
			return READOBJPATHIDENT_OK;
		}
	}
	else
	{
		end = src;
		while (end[0] && (isalnum(end[0]) || end[0] == '_' || end[0] == ':'))
		{
			end++;
		}
		if (src == end)
		{
			return READOBJPATHIDENT_EMPTYKEY;
		}
		estrConcat(estrOut, src, end - src);

		*path = end;
		if (quoted)
			*quoted = 0;
		return READOBJPATHIDENT_OK;
	}
}

// helper for ParserResolvePath - field is being used, so traverse down to next logical level.  
// Just deals with structs now, but should be expanded to references
// A -1 in column indicates this refers to a base structure, and not a field
__forceinline static bool TraverseObject(ParseTable** table, int* column, void** structptr, int* index, 
						   const char* path_in, size_t path_len, char **resultString, char **createString, 
						   U32 iObjPathFlags)
{
	ParseTable* subtable;
	void* substruct;

	PERFINFO_AUTO_START_FUNC_L3();

	if (((*table)[*column].type) & TOK_FLATEMBED)
	{
		*column = INVALID_COLUMN;
		*index = INVALID_COLUMN;
		PERFINFO_AUTO_STOP_L3();
		return true;
	}

	if (TOK_GET_TYPE((*table)[*column].type) == TOK_REFERENCE_X)
	{
		DictionaryHandle dictHandle;		
		if (!(iObjPathFlags & OBJPATHFLAG_TRAVERSEUNOWNED))
		{
			SETERROR("Attempted to traverse reference, when not allowed to: %s", path_in);
			PERFINFO_AUTO_STOP_L3();
			return false;
		}
		dictHandle = RefSystem_GetDictionaryHandleFromNameOrHandle((*table)[*column].subtable);
		if (!dictHandle || ! (subtable = RefSystem_GetDictionaryParseTable(dictHandle)))
		{
			SETERROR("Attempted to traverse invalid reference dictionary: %s", path_in);
			PERFINFO_AUTO_STOP_L3();
			return false;
		}

		if (*structptr)
		{
			ReferenceHandle *pHandle = TokenStoreGetRefHandlePointer(*table, *column, *structptr, *index, NULL);

			if (RefSystem_IsHandleActive(pHandle))
			{
				substruct = RefSystem_ReferentFromHandle(pHandle);
				if (!substruct)
				{
					SETERROR("Attempted to traverse null reference in %s", path_in);
					PERFINFO_AUTO_STOP_L3();
					return false;
				}			
				*structptr = substruct;
			}
			else
			{
				SETERROR("Attempted to traverse inactive reference in %s", path_in);
				PERFINFO_AUTO_STOP_L3();
				return false;
			}			
		}
		else
		{
			*structptr = NULL;
		}

		*table = subtable;
		*column = INVALID_COLUMN;
		*index = INVALID_COLUMN;
		PERFINFO_AUTO_STOP_L3();
		return true;
	}

	if ((((*table)[*column].type) & TOK_UNOWNED) && !(iObjPathFlags & OBJPATHFLAG_TRAVERSEUNOWNED))
	{
		SETERROR("Attempted to traverse unowned struct/poly, when not allowed to: %s", path_in);
		PERFINFO_AUTO_STOP_L3();
		return false;
	}

	if (!TOK_HAS_SUBTABLE((*table)[*column].type))
	{
		SETERROR("attempted to obtain field of non-struct: %s", path_in);
		PERFINFO_AUTO_STOP_L3();
		return false;
	}

	if (*structptr)
	{
		substruct = TokenStoreGetPointer(*table, *column, *structptr, *index, NULL);
		if (!substruct)
		{
			if ((iObjPathFlags & OBJPATHFLAG_CREATESTRUCTS) && (TOK_GET_TYPE((*table)[*column].type) != TOK_POLYMORPH_X))
			{
				StructGetSubtable(*table, *column, *structptr, *index, &subtable, NULL);
				substruct = StructCreateVoid(subtable);
				TokenStoreSetPointer(*table, *column, *structptr, *index, substruct, NULL);
			} 
			else 
			{
				SETERROR(PARSERRESOLVE_TRAVERSE_NULL_FULL, path_in);
				PERFINFO_AUTO_STOP_L3();
				return false;
			}
		}
	}
	else
	{
		substruct = NULL;
	}
	if (((*table)[*column].type & TOK_INDIRECT) && createString)
	{
		estrConcatf(createString,"create ");
		estrConcat(createString,path_in,(unsigned int)path_len);
		estrConcatf(createString,"\n");
	}

	StructGetSubtable(*table, *column, *structptr, *index, &subtable, NULL);
	if (!subtable)
	{
		if (iObjPathFlags & OBJPATHFLAG_REPORTWHENENCOUNTERINGNULLPOLY
			&& !(*structptr) && TOK_GET_TYPE((*table)[*column].type) == TOK_POLYMORPH_X)
		{
			SETERROR(SPECIAL_NULL_POLY_ERROR_STRING);
			PERFINFO_AUTO_STOP_L3();
			return false;
		}
	}
	


	*table = subtable;
	*column = INVALID_COLUMN;
	*structptr = substruct;
	*index = INVALID_COLUMN;
	PERFINFO_AUTO_STOP_L3();
	return true;
}

bool ParserResolveRootPath(const char* path_in, char** path_out, ParseTable** table_out,
	   int* column_out, void** structptr_out, char** resultString, U32 iObjPathFlags)
{
	bool ok = true;
	char *buf = NULL, *keybuf = NULL;
	const char* path = path_in;
	enumReadObjectPathIdentifierResult eIdentResult;

	estrStackCreate(&buf);
	estrStackCreate(&keybuf);
	// get from global table first, unless we're using . to signify local reference
	while (path && path[0] == ' ') path++;
	if (path && path[0] != '.' && path[0] != '[')
	{
		estrClear(&buf);
		eIdentResult = ReadObjectPathIdentifier(&buf, &path, NULL);
		if (eIdentResult != READOBJPATHIDENT_OK) 
		{
			if (eIdentResult == READOBJPATHIDENT_EMPTYKEY)
			{
				SETERROR(PARSERRESOLVE_EMPTY_KEY);
			}
			else
			{
				SETERROR("failed parsing path %s", path_in);
			}
			ok = false;
		}
		else
		{
			while (path[0] == ' ') path++;
			if (path[0] == '[')
			{
				estrClear(&keybuf);
				eIdentResult = ReadObjectPathIdentifier(&keybuf, &path, NULL);

				if (eIdentResult != READOBJPATHIDENT_OK) 
				{
					if (eIdentResult == READOBJPATHIDENT_EMPTYKEY)
					{
						SETERROR(PARSERRESOLVE_EMPTY_KEY);
					}
					else
					{
						SETERROR("failed parsing path %s", path_in);
					}
					ok = false;
				}
				else
				{
					while (path[0] == ' ') path++;
					//if (path[0] == ']') path++; //should already be done by ReadObjectPathIdentifier


					ok = ParserRootPathLookup(buf, keybuf, table_out, structptr_out, column_out);
					if (!ok) 
					{
						SETERROR("failed global textparser table lookup of %s", buf);
					}
				}
			}
			else
			{			
				ok = ParserRootPathLookup(buf, NULL, table_out, structptr_out, column_out);
				if (!ok) 
				{
					SETERROR("failed global textparser table lookup of %s", buf);
				}
			}
		}
	}

	if(path_out)
		*path_out = (char*)path;

	estrDestroy(&keybuf);
	estrDestroy(&buf);	

	return ok;
}

#define COMPILED_OBJECT_PATH_CACHE_SIZE_SERVER 30000
#define COMPILED_OBJECT_PATH_CACHE_SIZE_CLIENT 2000

#ifdef _M_X64
#define COMPILED_OBJECT_PATH_CACHE_SIZE_OBJECTDB 5000000
#else
#define COMPILED_OBJECT_PATH_CACHE_SIZE_OBJECTDB 100000
#endif

//XXXXXX: Default objectpath cache to true.
bool objectPathCacheEnable = true;
bool cacheVerifyOnly = false;
static U32 globalCompiledObjectPathCacheSize = 0;

static U32 objPathCacheQueries;
static U32 objPathCacheHits;

static U32 objPathCacheHitsRecent;

StashTable globalCompiledObjectPaths;
static CRITICAL_SECTION globalCompiledPathCS = {0};
static char *objectPathCacheInfo = 0;

static bool enableTLSCompiledObjectPaths = false;
static bool enableTLSCompiledObjectPathsSetByCommandLine = false;

AUTO_CMD_INT(enableTLSCompiledObjectPaths, EnableTLSCompiledObjectPaths) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE ACMD_CALLBACK(ChangeTLSCompiledObjectPaths);

void ChangeTLSCompiledObjectPaths(CMDARGS)
{
	enableTLSCompiledObjectPathsSetByCommandLine = true;
}

void EnableTLSCompiledObjectPaths(bool enable)
{
	if(enableTLSCompiledObjectPathsSetByCommandLine)
		return;

	enableTLSCompiledObjectPaths = enable;
}

static void EnterCompiledObjectPathCS(void)
{
	if(!enableTLSCompiledObjectPaths)
		EnterCriticalSection(&globalCompiledPathCS);
}

static void LeaveCompiledObjectPathCS(void)
{
	if(!enableTLSCompiledObjectPaths)
		LeaveCriticalSection(&globalCompiledPathCS);
}

AUTO_CMD_INT(objectPathCacheEnable, EnableObjectPathCache) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE; //Turning this off causes the ObjectDB to crash
AUTO_CMD_INT(cacheVerifyOnly, CacheVerifyOnly) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;
AUTO_CMD_INT(globalCompiledObjectPathCacheSize, CompiledObjectPathCacheSize) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

typedef struct ObjectPathCacheThreadData
{
	StashTable compiledObjectPaths;
} ObjectPathCacheThreadData;

ObjectPathCacheThreadData *GetObjectPathCacheThreadData(void)
{
	ObjectPathCacheThreadData *threadData;
	STATIC_THREAD_ALLOC(threadData);
	return threadData;
}

StashTable InitCompiledObjectPathCacheInternal(void)
{
	StashTable compiledObjectPaths = NULL;
	if (objectPathCacheEnable)
	{
		ATOMIC_INIT_BEGIN;
		if (!globalCompiledObjectPathCacheSize)
		{
			switch (gAppGlobalType)
			{
				case GLOBALTYPE_CLIENT:
					globalCompiledObjectPathCacheSize = COMPILED_OBJECT_PATH_CACHE_SIZE_CLIENT;
				xcase GLOBALTYPE_GIMMEDLL:
					globalCompiledObjectPathCacheSize = 8; 
				xcase GLOBALTYPE_OBJECTDB:
					globalCompiledObjectPathCacheSize = COMPILED_OBJECT_PATH_CACHE_SIZE_OBJECTDB;
                xcase GLOBALTYPE_CLONEOBJECTDB:
                    globalCompiledObjectPathCacheSize = COMPILED_OBJECT_PATH_CACHE_SIZE_OBJECTDB;
               xcase GLOBALTYPE_CLONEOFCLONE:
                    globalCompiledObjectPathCacheSize = COMPILED_OBJECT_PATH_CACHE_SIZE_OBJECTDB;
				xcase GLOBALTYPE_ACCOUNTSERVER:
					globalCompiledObjectPathCacheSize = COMPILED_OBJECT_PATH_CACHE_SIZE_OBJECTDB;
				xdefault:
					globalCompiledObjectPathCacheSize = COMPILED_OBJECT_PATH_CACHE_SIZE_SERVER;
			}
		}
		ATOMIC_INIT_END;
		if(globalCompiledObjectPathCacheSize)
		{
			compiledObjectPaths = stashTableCreateWithStringKeysEx(globalCompiledObjectPathCacheSize,StashDefault,FAKEFILENAME_COMPILEDOBJPATHS, __LINE__);
		}
		else
			objectPathCacheEnable = false;
	}
	return compiledObjectPaths;
}

void InitCompiledObjectPathCache(void)
{
	ObjectPathCacheThreadData *threadData = GetObjectPathCacheThreadData();
	threadData->compiledObjectPaths = InitCompiledObjectPathCacheInternal();
}

StashTable GetCompiledObjectPathCache(void)
{
	if(enableTLSCompiledObjectPaths)
	{
		ObjectPathCacheThreadData *threadData = GetObjectPathCacheThreadData();
		if(!threadData->compiledObjectPaths)
			InitCompiledObjectPathCache();

		return threadData->compiledObjectPaths;
	}
	else
	{
		return globalCompiledObjectPaths;
	}
}

int StashProcGetSizeOfObjectPath(void* userData, StashElement element) 
{
	int i;
	size_t *size_in = (size_t*)userData;
	size_t size = 0;
	ObjectPath *path = stashElementGetPointer(element);
	
	//size += sizeof(element); //calculated as part of stashTable's mem usage
	size += sizeof(path);
	size += sizeof(path->key);
	size += sizeof(path->key->lookupString);
	size += sizeof(EArray) + eaSize(&path->ppPath) * sizeof(path->ppPath[0]);//ppPath's earray
	for (i = 0; i < eaSize(&path->ppPath); i++)
	{
		ObjectPathSegment *seg = path->ppPath[i];
		size += sizeof(ObjectPathSegment);
		if (seg->key)
			size += sizeof(seg->key);					//key
	}
	*size_in += size;
	return 1;
}

int StashProcAddToEarray(void *userData, StashElement element)
{
	ObjectPath *path = stashElementGetPointer(element);
	ObjectPath ***earray = userData;
	
	eaPush(earray, path);
	return 1;
}

size_t GetSizeOfObjectPathCache()
{
	StashTable compiledObjectPaths = GetCompiledObjectPathCache();
	size_t size = stashGetMemoryUsage(compiledObjectPaths);
	stashForEachElementEx(compiledObjectPaths, StashProcGetSizeOfObjectPath, &size);
	return size;
}

int objPathCacheStatsDetailed;
AUTO_CMD_INT(objPathCacheStatsDetailed, objPathCacheStatsDetailed) ACMD_CMDLINE;

AUTO_COMMAND ACMD_CATEGORY(Debug);
void ShowObjectPathCacheStats()
{
	if (objectPathCacheEnable)
	{
		ObjectPath **earray = NULL;
		int i, j;
		StashTable compiledObjectPaths = GetCompiledObjectPathCache();

		printf("ObjectPaths Cached:%u (%u bytes) Hitrate %.2f%% (%u hits, %u queries)\n", stashGetCount(compiledObjectPaths), 
			GetSizeOfObjectPathCache(), 100.f * objPathCacheHits/objPathCacheQueries, objPathCacheHits, objPathCacheQueries);

		eaCreate(&earray);
		stashForEachElementEx(compiledObjectPaths, StashProcAddToEarray, &earray);
		eaStableSortUsingColumn(&earray, parse_ObjectPath, PARSE_OBJECTPATH_HITCOUNT_INDEX);

		i = eaSize(&earray);

		if(objPathCacheStatsDetailed)
			j = 0;
		else
		{
			j = i - 10;
			if (j < 0) j = 0;
		}

		for (;i > j; i--)
		{
			ObjectPath *path = earray[i-1];
			printf("  %u: %s\n", path->hitcount, path->key->lookupString);
		}

		eaDestroy(&earray);
		
		if (cacheVerifyOnly) 
			printf("\nCache is in verify mode only.");
	}
	else
	{
		printf("ObjectPath Cache disabled.");
	}
}

int ObjectPathCacheCleaner(U32* counter, StashElement element)
{
	//every 4th element
	if ((*counter)++ % 4 == 0)
	{
		ObjectPath *path = stashElementGetPointer(element);
		if (path->refcount == 1) // only referenced by the stashtable
			ObjectPathDestroy(path);
	}
	return 1;
}

//This function just removes every 4th entry from the compiled object path cache if the load factor is >=.95
void cleanCompiledObjectPaths(void)
{
	U32 counter = 0;
	StashTable compiledObjectPaths = GetCompiledObjectPathCache();
	U32 load = stashGetCount(compiledObjectPaths);

	if (load * 100 < 95 * globalCompiledObjectPathCacheSize)
        return;

	PERFINFO_AUTO_START_FUNC();
	verbose_printf("CompiledObjectPath cache over 95%% load, shrinking...\n");

	EnterCompiledObjectPathCS();
	stashForEachElementEx(compiledObjectPaths, ObjectPathCacheCleaner, &counter);
	LeaveCompiledObjectPathCS();
	PERFINFO_AUTO_STOP();
}

TSMP_DEFINE(ObjectPath);
TSMP_DEFINE(ObjectPathKey);
TSMP_DEFINE(ObjectPathSegment);

AUTO_RUN;
void initCompiledObjectPaths(void)
{
	TSMP_SMART_CREATE(ObjectPath, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	TSMP_SMART_CREATE(ObjectPathKey, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	TSMP_SMART_CREATE(ObjectPathSegment, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);

	ParserSetTPIUsesThreadSafeMemPool(parse_ObjectPath, &TSMP_NAME(ObjectPath));
	ParserSetTPIUsesThreadSafeMemPool(parse_ObjectPathKey, &TSMP_NAME(ObjectPathKey));
	ParserSetTPIUsesThreadSafeMemPool(parse_ObjectPathSegment, &TSMP_NAME(ObjectPathSegment));

	if(!enableTLSCompiledObjectPaths)
	{
		InitializeCriticalSection(&globalCompiledPathCS);
		globalCompiledObjectPaths = InitCompiledObjectPathCacheInternal();
	}
}

static void pointerToString(char * pszBuff,intptr_t pointer)
{
	int i;
	for (i=sizeof(intptr_t)*2-1;i>=0;i--)
	{
		pszBuff[i] = (char)(pointer & 0xf);
		if (pszBuff[i] < 0xa)
			pszBuff[i] += 48;
		else
			pszBuff[i] += 87;  //0xa+87 = 'a'
		pointer = pointer >> 4;
	}
}

U32 objPathMakeLookupString(char** lookupString, ParseTable* pti, const char* pathString)
{
	const int iPtrSize = sizeof(pti)*2; // in alphanumeric chars
	const U32 prefixLen = iPtrSize+2;

	// this can't use the parsetable name until (at least) the objectdb doesn't make
	// separate schemas for each type of entity based container it stores. (We'd still
	// also need to make sure that there can be no duplicate names in general though)
	estrClear(lookupString);
	estrForceSize(lookupString,2+iPtrSize);

	(*lookupString)[0] = '0';
	(*lookupString)[1] = 'x';
	pointerToString(&((*lookupString)[2]),(intptr_t)pti);
	estrConcat(lookupString, pathString, (U32)strlen(pathString));

	return prefixLen;
}

void objPathKeyInitialize(ObjectPath* path, ObjectPathKey* key, ParseTable* pti, const char* pathString)
{
	char* lookupString = NULL;
	U32 prefixLen;

	estrStackCreate(&lookupString);

	// RvP: not all things set a parsetable because the object path building code that
	// XMLRPC uses recreates the key many times for each object path
	if(pti)
		key->rootTpi = pti;

	prefixLen = objPathMakeLookupString(&lookupString, pti, pathString);

	if(key->lookupString)
	{
		// Only the object path building functions are allowed to realloc lookup string,
		// anything actually using the compiled object path cache would fail horribly
		devassertmsg(!path->refcount, "Can't change the string for this object path because it's referenced somewhere");
		estrDestroy(&key->lookupString);
	}

	// These were previously put in an ever growing string table, but now have to actually
	// be freed properly
	estrBufferCreate(&key->lookupString, SAFESTR(key->lookupStringBuffer));
	estrCopy(&key->lookupString, &lookupString);
	key->pathString = key->lookupString + prefixLen;

	estrDestroy(&lookupString);
}

ObjectPath *ObjectPathCopy(ObjectPath* path)
{
	if (!path)
		return NULL;

	if (eaSize(&path->ppPath) == 0)
		return NULL;
	else
	{
		int i;
		ObjectPath *newPath = StructCreate(parse_ObjectPath);
		newPath->key = StructCreate(parse_ObjectPathKey);

		objPathKeyInitialize(newPath, newPath->key, path->key->rootTpi, path->key->pathString);

		//Copy the segments
		for (i = 0; i < eaSize(&path->ppPath); i++)
		{
			ObjectPathSegment *seg = eaGet(&path->ppPath, i);
			ObjectPathSegment *newSeg = StructCreate(parse_ObjectPathSegment);

			newSeg->column = seg->column;
			newSeg->descend = seg->descend;
			newSeg->fieldOffset = seg->fieldOffset;
			newSeg->fieldSize = seg->fieldSize;
			newSeg->index = seg->index;
			newSeg->isPoly = seg->isPoly;
			newSeg->key = seg->key;
			newSeg->offset = seg->offset;

			eaPush_dbg(&newPath->ppPath, newSeg, FAKEFILENAME_COMPILEDOBJPATHS, __LINE__);
		}
		return newPath;
	}
}

ObjectPath *ObjectPathCreate(ParseTable *tpi, const char *field, int column, int index, const char *key)
{
	ObjectPath *path;
	ObjectPathSegment *seg;

	int numcolumns = 0;
	char newPathName[MAX_OBJECT_PATH];
	
	assert(field);
	assert(tpi);
	
	if (TPIHasTPIInfoColumn(tpi))
	{
		ParseTableInfo* tpiInfo = ParserGetTableInfo(tpi);
		numcolumns = tpiInfo->numcolumns;
	}

	path = StructCreate(parse_ObjectPath);
	path->key = StructCreate(parse_ObjectPathKey);

	sprintf(newPathName, ".%s", field);
	objPathKeyInitialize(path, path->key, tpi, newPathName);

	seg = StructCreate(parse_ObjectPathSegment);
	seg->fieldOffset = 0;
	seg->fieldSize = (int)strlen(newPathName);
	seg->index = index;
	if (key) seg->key = allocAddString(key);
	if (column < 0)
	{
		FORALL_PARSETABLE(tpi, column)
		{
			if (strcmpi(tpi[column].name, field) == 0)
			{
				numcolumns = column + 1;	//Just set to larger than column to pass the devassert.
				break;
			}
			if (tpi[column].type == TOK_END)
				numcolumns = column;
		}
	}
	devassertmsg(column < numcolumns, "Could not resolve field in compiled objectpath creation.");
	seg->column = column;
	
	seg->isPoly = (TOK_GET_TYPE(tpi[column].type) == TOK_POLYMORPH_X);
	seg->offset = tpi[column].storeoffset;

	eaPush(&path->ppPath, seg);

	return path;
}

ObjectPath *ObjectPathCopyAndAppend(ObjectPath* path, const char *field, int column, int index, const char *key)
{
	if (!path)
		return NULL;

	if (eaSize(&path->ppPath) == 0)
		return NULL;
	else
	{
		int i;
		ParseTable *tpi;
		//TODO: This should be swapped out for an estring.
		char newPathName[MAX_OBJECT_PATH];
		ObjectPath *newPath = NULL;
		ObjectPathSegment *top = NULL;

		if (!ParserResolvePathComp(path, NULL, &tpi, NULL, NULL, NULL, NULL, 0))
		{
			Errorf("Could not resolve parsetable for path:%s.\n", path->key->pathString);
			return NULL;
		}

		newPath = StructCreate(parse_ObjectPath);
		newPath->key = StructCreate(parse_ObjectPathKey);

		sprintf(newPathName, "%s.%s", path->key->pathString, field);
		objPathKeyInitialize(newPath, newPath->key, path->key->rootTpi, newPathName);

		//Copy the segments
		for (i = 0; i < eaSize(&path->ppPath); i++)
		{
			ObjectPathSegment *seg = eaGet(&path->ppPath, i);
			top = StructCreate(parse_ObjectPathSegment);

			top->column = seg->column;
			top->descend = seg->descend;
			top->fieldOffset = seg->fieldOffset;
			top->fieldSize = seg->fieldSize;
			top->index = seg->index;
			top->isPoly = seg->isPoly;
			top->key = seg->key;
			top->offset = seg->offset;

			eaPush_dbg(&newPath->ppPath, top, FAKEFILENAME_COMPILEDOBJPATHS, __LINE__);
		}
		assert(top); //this should never assert.

		//Make the path traverse below this last element.
		top->descend = true;

		//Create and append a new element.
		top = StructCreate(parse_ObjectPathSegment);
		top->fieldOffset = (int)strlen(path->key->pathString);
		top->fieldSize = (int)strlen(field) + 1; // +1 to include the dot.
		top->descend = false;

		top->index = index;
		if (key) top->key = allocAddString(key);

		if (column == LOOKUP_COLUMN)
		{
			FORALL_PARSETABLE(tpi, column)
			{
				if (strcmpi(tpi[column].name, field) == 0)
					break;
				if (tpi[column].type == TOK_END)
					column = INVALID_COLUMN;
			}
		}
		top->column = column;
		if (column >= 0)
		{
			top->offset = tpi[column].storeoffset;
			top->isPoly = (TOK_GET_TYPE(tpi[column].type) == TOK_POLYMORPH_X);
		}

		eaPush(&newPath->ppPath, top);

		return newPath;
	}
}

ObjectPath* ObjectPathAppend(ObjectPath* path, const char *field, int column, int index, const char *key)
{
	if (!path)
		return NULL;

	if (eaSize(&path->ppPath) == 0)
		return NULL;
	else
	{
		ParseTable *tpi;
		//TODO: This should be swapped out for an estring.
		char newPathName[MAX_OBJECT_PATH];
		ObjectPathSegment *top = ObjectPathTail(path);

		//Make the path traverse below this last element.
		top->descend = true;

		if (!ParserResolvePathComp(path, NULL, &tpi, NULL, NULL, NULL, NULL, 0))
		{
			Errorf("Could not resolve parsetable for path:%s.\n", path->key->pathString);
			return NULL;
		}

		//Create and append a new element.
		top = StructCreate(parse_ObjectPathSegment);
		top->fieldOffset = (int)strlen(path->key->pathString);
		top->fieldSize = (int)strlen(field) + 1; // +1 to include the dot.
		top->descend = true;
		
		sprintf(newPathName, "%s.%s", path->key->pathString, field);
		objPathKeyInitialize(path, path->key, NULL, newPathName);

		top->index = index;
		if (key) top->key = allocAddString(key);

		if (column == LOOKUP_COLUMN)
		{
			FORALL_PARSETABLE(tpi, column)
			{
				if (strcmpi(tpi[column].name, field) == 0)
					break;
				if (tpi[column].type == TOK_END)
				{
					column = INVALID_COLUMN;
					break;
				}
			}
		}
		top->column = column;
		if (column >= 0)
		{
			top->offset = tpi[column].storeoffset;
			top->isPoly = (TOK_GET_TYPE(tpi[column].type) == TOK_POLYMORPH_X);
		}

		eaPush(&path->ppPath, top);

		return path;
	}
}

ObjectPath *ObjectPathChopSegment(ObjectPath *path)
{
	if (!path)
		return NULL;

	if (eaSize(&path->ppPath) < 2)
		return NULL;
	else
	{
		ObjectPathSegment *seg = eaPop(&path->ppPath);
		char *newkey = NULL;
		estrStackCreate(&newkey);
		
		estrPrintf(&newkey, "%s", path->key->pathString);
		estrSetSize(&newkey, seg->fieldOffset);
		objPathKeyInitialize(path, path->key, NULL, newkey);
		
		StructDestroy(parse_ObjectPathSegment, seg);
		seg = ObjectPathTail(path);

		estrDestroy(&newkey);
		return path;
	}
}

ObjectPath *ObjectPathChopSegmentIndex(ObjectPath *path)
{
	if (!path)
		return NULL;

	if (eaSize(&path->ppPath) < 1)
		return NULL;
	else
	{
		ObjectPathSegment *seg = ObjectPathTail(path);
		char *key = NULL;
		size_t ilen;
		estrStackCreate(&key);
		
		if (seg->key) estrPrintf(&key, "[%s]", seg->key);
		else estrPrintf(&key, "[%d]", seg->index);
		ilen = strlen(key);
		
		estrPrintf(&key, "%s", path->key->pathString);
		seg->fieldSize -= (U16)ilen;
		estrSetSize(&key, seg->fieldOffset + seg->fieldSize);
		
		objPathKeyInitialize(path, path->key, NULL, key);
		
		seg->index = -1;
		seg->key = NULL;

		estrDestroy(&key);
		return path;
	}
}


ObjectPath *ObjectPathCopyAndAppendIndex(ObjectPath* path, int index, const char *key)
{
	if (!path)
		return NULL;
	if (eaSize(&path->ppPath) == 0)
		return NULL; 

	else
	{
		int i;
		ParseTable *tpi;
		//TODO: This should be swapped out for an estring.
		char newPathName[MAX_OBJECT_PATH];
		char *keystring = newPathName + strlen(path->key->pathString); 
		int keystringsize;
		ObjectPath *newPath = NULL;
		ObjectPathSegment *top = NULL;

		top = eaTail(&path->ppPath);
		assert(top);
		if (top->index != -1 || top->key)
			return NULL;

		if (!ParserResolvePathComp(path, NULL, &tpi, NULL, NULL, NULL, NULL, 0))
		{
			Errorf("Could not resolve parsetable for path:%s.\n", path->key->pathString);
			return NULL;
		}

		newPath = StructCreate(parse_ObjectPath);
		newPath->key = StructCreate(parse_ObjectPathKey);

		//The pathString should be in the objectpath strings cache.
		if (key) sprintf(newPathName, "%s[%s]", path->key->pathString, key);
		else sprintf(newPathName, "%s[%d]", path->key->pathString, index);
		keystringsize = (int)strlen(keystring);
		
		objPathKeyInitialize(newPath, newPath->key, path->key->rootTpi, newPathName);

		//Copy the segments
		for (i = 0; i < eaSize(&path->ppPath); i++)
		{
			ObjectPathSegment *seg = eaGet(&path->ppPath, i);
			top = StructCreate(parse_ObjectPathSegment);

			top->column = seg->column;
			top->descend = seg->descend;
			top->fieldOffset = seg->fieldOffset;
			top->fieldSize = seg->fieldSize;
			top->index = seg->index;
			top->isPoly = seg->isPoly;
			top->key = seg->key;
			top->offset = seg->offset;

			eaPush_dbg(&newPath->ppPath, top, FAKEFILENAME_COMPILEDOBJPATHS, __LINE__);
		}
		assert(top); //this should never assert.

		//Make the path traverse below this last element.
		if (key) top->key = allocAddString(key);
		else top->index = index;
		top->fieldSize += keystringsize;

		return newPath;
	}
}

ObjectPath *ObjectPathAppendIndex(ObjectPath* path, int index, const char *key)
{
	if (!path)
		return NULL;
	if (eaSize(&path->ppPath) == 0)
		return NULL; 

	else
	{
		ParseTable *tpi;
		//TODO: This should be swapped out for an estring.
		char newPathName[MAX_OBJECT_PATH];
		char *keystring = newPathName + strlen(path->key->pathString); 
		int keystringsize;
		
		ObjectPathSegment *top = ObjectPathTail(path);

		//check to see that this element is an array.
		if (top->index != -1 || top->key)
			return NULL;

		if (!ParserResolvePathComp(path, NULL, &tpi, NULL, NULL, NULL, NULL, 0))
		{
			Errorf("Could not resolve parsetable for path:%s.\n", path->key->pathString);
			return NULL;
		}

		//The pathString should be in the objectpath strings cache.
		if (key) sprintf(newPathName, "%s[%s]", path->key->pathString, key);
		else sprintf(newPathName, "%s[%d]", path->key->pathString, index);
		keystringsize = (int)strlen(keystring);
		
		objPathKeyInitialize(path, path->key, NULL, newPathName);

		//Make the path traverse below this last element.
		if (key) top->key = allocAddString(key);
		else top->index = index;
		top->fieldSize += keystringsize;

		return path;
	}
}

void ObjectPathSetTailDescend(ObjectPath *path, bool flag)
{
	ObjectPathSegment *seg = ObjectPathTail(path);
	if (seg)
		seg->descend = flag;
}

ObjectPathSegment* ObjectPathTail(ObjectPath *path)
{
	ObjectPathSegment *seg;
	assert(path);
	assert(eaSize(&path->ppPath) > 0);
	
	seg = eaTail(&path->ppPath);
	assert(seg);
	return seg;
}

const char * ObjectPathTailString(ObjectPath *path)
{
	ObjectPathSegment *seg = ObjectPathTail(path);
	return path->key->pathString + seg->fieldOffset;
}

void ObjectPathDestroy(ObjectPath *path)
{
	ObjectPath *stashedPath = NULL;
	ObjectPathKey *key = path->key;
	S32 doDestroyPath = 0;
	assert(path);
	assert(key);
	
	PERFINFO_AUTO_START_FUNC_L2();
	EnterCompiledObjectPathCS();
	if (--path->refcount <= 0)
	{
		StashTable compiledObjectPaths = GetCompiledObjectPathCache();
		// Remove from the StashTable if appropriate
		if (stashFindPointer(compiledObjectPaths, key->lookupString, &stashedPath) && stashedPath == path)
			stashRemovePointer(compiledObjectPaths, key->lookupString, NULL);
		LeaveCompiledObjectPathCS();

		//The ParseTable is not owned.
		path->key->rootTpi = NULL;
		StructDestroy(parse_ObjectPath, path);
	}
	else
	{
		LeaveCompiledObjectPathCS();
	}
	PERFINFO_AUTO_STOP_L2();
}

void ObjectPathCleanTPI(ParseTable *tpi)
{
	StashTableIterator iter;
	StashElement elem;
	StashTable compiledObjectPaths = GetCompiledObjectPathCache();
	
	PERFINFO_AUTO_START_FUNC();

	EnterCompiledObjectPathCS();
	stashGetIterator(compiledObjectPaths, &iter);

	while (stashGetNextElement(&iter, &elem))
	{
		ObjectPath *path = stashElementGetPointer(elem);
		if (path->key->rootTpi == tpi)
			ObjectPathDestroy(path);
	}
	LeaveCompiledObjectPathCS();
	
	PERFINFO_AUTO_STOP();
}


bool ParserResolvePathEx(const char* path_in, ParseTable table_in[], void* structptr_in, 
	   ParseTable** table_out, int* column_out, void** structptr_out, int* index_out, ObjectPath **objpath_out,
	   char **resultString, char **correctedString, char **createString, U32 iObjPathFlags)
{
	ParseTable* table = table_in;
	int column = INVALID_COLUMN;
	void* structptr = structptr_in;
	int index = INVALID_COLUMN;
	char* path;
	const char *keyPath = path_in;
	ParseTable* keyTable;
	bool ok = true;
	char *buf = NULL;

	void *strptr;

	ObjectPath *cachedPath = NULL;
	ObjectPath *objPath = NULL;
	ObjectPathSegment *top = NULL;
	char* path_start;
	char* field_start;
	char* objPathLookupString = NULL;
	enumReadObjectPathIdentifierResult eIdentResult;

	if (path_in == NULL) return false;

	PERFINFO_AUTO_START_FUNC_L2();

	if (! (iObjPathFlags & OBJPATHFLAG_DONTLOOKUPROOTPATH))
		ok = ParserResolveRootPath(path_in, &path, &table, &column, &structptr, resultString, iObjPathFlags);
	else
	{
		path = (char*)path_in;
		table = table_in;
		structptr = structptr_in;
	}
	
	strptr = structptr;
	keyTable = table;

	if (ok && !table) 
	{ 
		ok = false; 
		SETERROR("usage error: local path specified, but table_in parameter was not provided"); 
	}
	if (ok && correctedString)
	{
		estrConcat(correctedString,path,path_in - path);
	}

	if (!ok)
	{
		PERFINFO_AUTO_STOP_L2();
		return ok;
	}

	//Check cache for compiled object path if there's no create string, misspell guessing, or correctedString.
	if (objectPathCacheEnable && !ParserTableIsTemporary(table_in) && !(iObjPathFlags & OBJPATHFLAG_TOLERATEMISSPELLEDFIELDS) && !createString && !correctedString) {
		EnterCompiledObjectPathCS();
		if (path)
		{
			U32 numQueries;
			StashTable compiledObjectPaths = GetCompiledObjectPathCache();
			estrStackCreate(&objPathLookupString);
			objPathMakeLookupString(&objPathLookupString, table, path);

			numQueries = InterlockedIncrement(&objPathCacheQueries);

			if (stashFindPointer(compiledObjectPaths, objPathLookupString, &objPath))
			{
				InterlockedIncrement(&objPathCacheHits);
				InterlockedIncrement(&objPathCacheHitsRecent);
				if (cacheVerifyOnly)
				{
					cachedPath = objPath;
					objPath = NULL;
				}
				else
				{
					ok = ParserResolvePathComp(objPath, structptr, table_out, column_out, structptr_out, index_out, resultString, iObjPathFlags);
					if (objpath_out) {
						if (iObjPathFlags & OBJPATHFLAG_INCREASEREFCOUNT)
							objPath->refcount++;
						*objpath_out = objPath;
					}
					LeaveCompiledObjectPathCS();
					estrDestroy(&objPathLookupString);

					if(!(numQueries % 1000))
					{
						//verbose_printf("Object path cache hit rate for past 1000 queries is %.2f%% (all time %.2f%%)\n", 100.f * objPathCacheHitsRecent / 1000, 100.f * objPathCacheHits/objPathCacheQueries);
						InterlockedExchange(&objPathCacheHitsRecent, 0);
					}
					PERFINFO_AUTO_STOP_L2();
					return ok;
				}
			}

			if(!(numQueries % 1000))
			{
				//verbose_printf("Object path cache hit rate for past 1000 queries is %.2f%% (all time %.2f%%)\n", 100.f * objPathCacheHitsRecent / 1000, 100.f * objPathCacheHits/objPathCacheQueries);
				InterlockedExchange(&objPathCacheHitsRecent, 0);
			}
		}
		//shrink the cache if necessary.
		cleanCompiledObjectPaths();
		LeaveCompiledObjectPathCS();

		if (!cachedPath)
		{
			//Set objPath so we compile as we parse.
			objPath = StructCreate(parse_ObjectPath);

			//defer allocAddString until compilation succeeds.
			keyPath = path;
		}
	}

	PERFINFO_AUTO_START_L2("Cache miss", 1);

	estrStackCreate(&buf);
	path_start = path;
	// main loop for iterating through path
	while (ok && path && path[0]) 
	{
		while (path[0] == ' ') path++;
		if (path[0] == '[') // indexing
		{
			bool bLastThing = false;
			estrClear(&buf);
			eIdentResult = ReadObjectPathIdentifier(&buf, &path, NULL);
			if (top) top->index = -1;
			if (eIdentResult == READOBJPATHIDENT_OK) // eat trailing bracket immediately
			{
				while (path[0] == ' ') path++;
				//if (path[0] == ']') path++; //should already be done by ReadObjectPathIdentifier
				if (!path[0])
				{
					bLastThing = true;
				}
			}

			if (eIdentResult != READOBJPATHIDENT_OK) 
			{
				if (eIdentResult == READOBJPATHIDENT_EMPTYKEY)
				{
					SETERROR(PARSERRESOLVE_EMPTY_KEY);
				}
				else
				{
					SETERROR("failed parsing path %s", path_in);
				}

				ok = false;
			}
			else if (column < 0) 
			{ 
				ok = false; 
				SETERROR("attempted indexing of non-field: %s", path_in);
			}
			else if (index >= 0)
			{
				ok = false; 
				SETERROR("attempted to index field twice: %s", path_in); 
			}
			else if (!(table[column].type & TOK_EARRAY || table[column].type & TOK_FIXED_ARRAY))
			{
				ok = false; 
				SETERROR("attempted index of single field: %s", path_in); 
			}
			else if (TOK_HAS_SUBTABLE(table[column].type))
			{
				//when structptr is NULL, use index -1 so that we can get types from parsetables without actual data
				ok = ParserResolveKey(buf, table, column, structptr, &index, iObjPathFlags, resultString, top);

				if (ok && index >= 0 && table[column].type & TOK_INDIRECT && createString)
				{
					estrConcatf(createString,"create ");
					estrConcat(createString,path_in,path - path_in);
					estrConcatf(createString,"\n");
				}			
				
				if (correctedString && ok)
				{
					estrConcatf(correctedString,"[\"%s\"]",buf);
				}
			}
			else // non-struct array
			{
				int numelems;	
				ok = sscanf(buf, "%i", &index) == 1;
				if (!ok) 
				{
					SETERROR("expected numeric index but received %s", buf);
				}
				if (correctedString && ok)
				{
					estrConcatf(correctedString,"[\"%s\"]",buf);
				}
				if (top) top->index = index;
				if (!structptr)
				{
					index = -1;
				}
				else if (index >= (numelems = TokenStoreGetNumElems_inline(table, &table[column], column, structptr, NULL)))
				{					
					if (!(iObjPathFlags & OBJPATHFLAG_GETFINALKEY))
					{
						ok = 0;
						SETERROR(PARSERRESOLVE_INDEX_BEYOND_BOUNDS_FULL, index, numelems);
					}
					index = -1;
				}				
			}
		}
		else if (path[0] == '.') // field specifier
		{
			// if we already have a field, then we need to traverse the previous object
			if (column >= 0)
			{
				bool bNullStruct = (structptr == NULL);
				if (top) top->descend = true;
				ok = TraverseObject(&table, &column, &structptr, &index, 
					path_in, path-path_in, resultString, createString, iObjPathFlags); 
					// traverse does it's own error printing
				
				if (bNullStruct)
				{
					structptr = NULL;
				}
			}
			if (ok)
			{
				bool bFound = false;
				path++;
				field_start = path;
				estrClear(&buf);
				eIdentResult = ReadObjectPathIdentifier(&buf, &path, NULL);
				if (eIdentResult != READOBJPATHIDENT_OK)
				{
					ok = false;
				}
				
				bFound = ParserFindColumn(table, buf, &column);

				if (!bFound)
				{
					if (ParserFindIgnoreColumn(table, buf))
					{
						if(iObjPathFlags & OBJPATHFLAG_TREATIGNOREASSUCCESS)
							ok = true;
						else
						{
							ok = false;
							SETERROR(PARSERRESOLVE_TRAVERSE_IGNORE_FULL, buf);
						}
					}
					else if (iObjPathFlags & OBJPATHFLAG_TOLERATEMISSPELLEDFIELDS)
					{
						int iBestDif = 100000000;
						int iBestColumn = -1;
						int i;

						FORALL_PARSETABLE(table, i)
						{
							if (table[i].name && table[i].name[0])
							{
								int iCurDif = levenshtein_distance(table[i].name, buf);

								if (iCurDif < iBestDif)
								{
									iBestDif = iCurDif;
									iBestColumn = i;
								}
							}
						}

						bFound = true;
						column = iBestColumn;
					}
					else
					{
						ok = false; 
						SETERROR("couldn't find field %s", buf); 
					}
				}

				if (bFound)
				{
					if (iObjPathFlags & OBJPATHFLAG_WRITERESULTIFNOBROADCAST && resultString)
					{
						if (!FlagsMatchAll(table[column].type, FLAGS_TO_INCLUDE_BROADCAST))
						{
							estrPrintf(resultString,"0");
						}
					}
					//compile the objectpath
					if (objPath) {
						top = StructCreate(parse_ObjectPathSegment);
						top->fieldOffset = field_start - path_start - 1; //-1 to include the dot
						top->fieldSize = path - field_start + 1;		//+1 to include the dot
						top->column = column;
						top->descend = false;
						top->offset = table[column].storeoffset;
						top->index = index;
						top->key = 0;
						top->isPoly = (TOK_GET_TYPE(table[column].type) == TOK_POLYMORPH_X);
						eaPush_dbg(&objPath->ppPath, top, FAKEFILENAME_COMPILEDOBJPATHS, __LINE__);
					}
					if (correctedString)
					{
						if (table[column].type & TOK_FLATEMBED)
						{
							// write nothing to correctedString
						}
						else if (table[column].type & TOK_REDUNDANTNAME)
						{
							int realcolumn = ParseInfoFindAliasedField(table,column);
							if (realcolumn < 0)
							{
								ok = false;
								SETERROR("no real field for redundant field %s", buf); 
							}
							else
							{
								estrConcatf(correctedString,".%s",table[realcolumn].name);
								if (top) top->column = realcolumn;
							}
						}
						else
						{
							//usually table[column].name will be the same as buf, but it will be different
							//in the misspelled case
							estrConcatf(correctedString,".%s",table[column].name);
						}
					}
				}
			}
		}
		else // not [ or .
		{
			ok = false;
			SETERROR("couldn't parse path %s", path_in);
		}
	} // iterate through path


	if (objPath)
	{
		if (objPath->ppPath == 0 || eaSize(&objPath->ppPath) == 0) 
		{
			StructDestroy(parse_ObjectPath, objPath);
			objPath = NULL;
		}
	}
	// results
	if (ok)
	{
		if (objectPathCacheEnable && objPath)
		{	//compiled case
			ObjectPath *existingPath;
			StashTable compiledObjectPaths = GetCompiledObjectPathCache();
			EnterCompiledObjectPathCS();

			//Check the cache again just in case a compiled path was added while we were compiling.
			if (stashFindPointer(compiledObjectPaths, objPathLookupString, &existingPath))
			{
				StructDestroy(parse_ObjectPath, objPath);
				objPath = existingPath;
			}
			else
			{
				objPath->key = StructCreate(parse_ObjectPathKey);
				objPathKeyInitialize(objPath, objPath->key, keyTable, keyPath);
				stashAddPointer(compiledObjectPaths, objPath->key->lookupString, objPath, false);
				objPath->refcount++; // add refcount for being in the stashtable
			}
			if (objpath_out) {
				if (iObjPathFlags & OBJPATHFLAG_INCREASEREFCOUNT)
					objPath->refcount++;
				*objpath_out = objPath;
			}
			if (!cacheVerifyOnly) {
				objPath->refcount++;
			}
			LeaveCompiledObjectPathCS();

			if  (cacheVerifyOnly)
			{
				ParseTable *ctable;
				int ccol;
				int cind;
				void *cstruct;

				if (table_out) *table_out = table;
				if (column_out) *column_out = column;
				if (structptr_out) *structptr_out = structptr;
				if (index_out) *index_out = index;

				ok = ParserResolvePathComp(objPath, strptr, &ctable, &ccol, &cstruct, &cind, resultString, iObjPathFlags);
				//check cache result
				if (!((table_out?ctable == table:1) &&
					(column_out?ccol == column:1) &&
					(structptr_out?cstruct == structptr:1) &&
					(index_out?cind == index:1) &&
					ok) )
				{
					Errorf("Compiled Objectpath did not return the correct output for path:%s\n", objPath->key->pathString);
				}
			}
			else
			{
				int ccol, cind;
				//used the compiled path to generate output.
				ok = ParserResolvePathComp(objPath, strptr, table_out, &ccol, structptr_out, &cind, resultString, iObjPathFlags);
				if (column_out) *column_out = ccol;
				if (index_out) *index_out = cind;

				//sanity check
				if (!((table_out?table == *table_out:1) &&
					(column_out?column == ccol:1) &&
					(structptr_out?structptr == *structptr_out:1) &&
					(index_out?index == cind:1) &&
					ok) )
				{
					ok = ParserResolvePathComp(objPath, strptr, table_out, column_out, structptr_out, index_out, resultString, iObjPathFlags);
					Errorf("Compiled Objectpath did not return the correct output for path:%s\n", objPath->key->pathString);
				}
				InterlockedDecrement(&objPath->refcount);
			}
		}
		else
		{	//non-compiled case
			if (table_out) *table_out = table;
			if (column_out) *column_out = column;
			if (structptr_out) *structptr_out = structptr;
			if (index_out) *index_out = index;


			if (cacheVerifyOnly) 
			{
				if (cachedPath)
				{
					ParseTable *ctable;
					int ccol;
					int cind;
					void *cstruct;
					ok = ParserResolvePathComp(cachedPath, strptr, &ctable, &ccol, &cstruct, &cind, resultString, iObjPathFlags);
					//check cache result
					if (!((table_out?ctable == table:1) &&
						(column_out?ccol == column:1) &&
						(structptr_out?cstruct == structptr:1) &&
						(index_out?cind == index:1) &&
						ok) )
					{
						Errorf("Compiled Objectpath did not return the correct output for path:%s\n", cachedPath->key->pathString);
					}

					if (objpath_out) *objpath_out = cachedPath;
				}
			}
		}
	} 
	else
	{
		if (cacheVerifyOnly)
		{
			if (cachedPath)
			{
				ok = ParserResolvePathComp(cachedPath, strptr, NULL, NULL, NULL, NULL, resultString, iObjPathFlags);
				//check cache result
				if (ok)
				{
					Errorf("Compiled Objectpath did not return the correct output for path:%s\n", cachedPath->key->pathString);
				}
			}
		}

		if (objPath) StructDestroy(parse_ObjectPath, objPath);
	}

	estrDestroy(&buf);
	estrDestroy(&objPathLookupString);
	PERFINFO_AUTO_STOP_L2();
	PERFINFO_AUTO_STOP_L2();
	return ok;
}

bool ParserResolvePathSegment(ObjectPathSegment *pSeg, ParseTable *tpi, const void *structPtr, ParseTable **tpiOut, void **struct_out, int *column_out,  int *index_out, char **resultString, U32 iObjPathFlags)
{
	ParseTable *leafTpi = tpi;
	void *walker = (void*)structPtr;
	bool walkerIsNull = (walker == NULL);
	int column = pSeg->column;
	int index = pSeg->index;
	bool ok = true;

	do {
		if (pSeg->key != 0)
		{
			if (walkerIsNull)
			{
				index = -1;
			}
			else if (!ParserResolveKey(pSeg->key, leafTpi, column, walker, &index, iObjPathFlags, resultString, NULL))
			{
				ok = false;
				break;
			}
		}
		else
		{
			index = pSeg->index;

			if (walkerIsNull)
			{
				index = -1;
			}

			if (index >= 0)
			{
				int numelems = TokenStoreGetNumElems_inline(leafTpi, &leafTpi[column], column, walker, NULL); 
				if (index >= numelems)
				{
					if (!(iObjPathFlags & OBJPATHFLAG_GETFINALKEY))
					{					
						SETERROR(PARSERRESOLVE_INDEX_BEYOND_BOUNDS_FULL, index, numelems);
						ok = false;
						index = -1;
						break;
					}
					// Out of bounds
					index = -1;
				}
			}
		}

		if(pSeg->descend) {			
			if (pSeg->isPoly)
			{
				SETERROR("Cannot descend polymorphic path without a full objectpath.");
				ok = false;
				break;
			}
			else
			{
				//descend an array or a struct
				if (!TraverseObject(&leafTpi, &column, &walker, &index, "Compiled ObjectPathSegment", 0, resultString, NULL, iObjPathFlags))
				{
					if (walkerIsNull) walker = NULL;
					if (tpiOut) *tpiOut = leafTpi;
					if (struct_out) *struct_out = walker;
					if (index_out) *index_out = index;
					if (column_out) *column_out = column;
					ok = false;
					break;
				}
			}
		}
		else
		{
			break;
		}
		if (walkerIsNull) walker = NULL;
	} while (false);
	if (ok)
	{
		if (tpiOut) *tpiOut = leafTpi;
		if (struct_out) *struct_out = walker;
		if (index_out) *index_out = index;
		if (column_out) *column_out = column;
	}
	return ok;
}

bool ParserResolvePathComp(ObjectPath *objPath, const void *structPtr, ParseTable **tpiOut, int *column_out, void **struct_out, int *index_out, char **resultString, U32 iObjPathFlags)
{
	void* walker = (void*)structPtr;
	ParseTable *leafTpi = (ParseTable*)objPath->key->rootTpi;
	bool ok = true;
	int i;
	int column = -1;
	int index = -1;
	int pathSize = eaSize(&objPath->ppPath);

	PERFINFO_AUTO_START_FUNC_L2();
	objPath->hitcount++;

	devassertmsgf(pathSize, "Compiled ObjectPath had no segments: %s\n", objPath->key->pathString);

	for (i = 0; i < pathSize && leafTpi; i++)
	{
		ObjectPathSegment *pSeg = objPath->ppPath[i];
		bool walkerIsNull = (walker == NULL);
		char *subpath = (char *)(objPath->key->pathString + pSeg->fieldOffset);
		column = pSeg->column;
		index = pSeg->index;

		if (iObjPathFlags & OBJPATHFLAG_WRITERESULTIFNOBROADCAST && resultString)
		{
			if (!FlagsMatchAll(leafTpi[column].type, FLAGS_TO_INCLUDE_BROADCAST))
			{
				estrPrintf(resultString,"0");
			}
		}

		if (pSeg->key != 0)
		{
			if (walkerIsNull)
			{
				index = -1;
			}
			else if (!ParserResolveKey(pSeg->key, leafTpi, column, walker, &index, iObjPathFlags, resultString, NULL))
			{
				ok = false;
				break;
			}
		}
		else
		{
			index = pSeg->index;

			if (walkerIsNull)
			{
				index = -1;
			}

			if (index >= 0)
			{
				int numelems = TokenStoreGetNumElems_inline(leafTpi, &leafTpi[column], column, walker, NULL); 
				if (index >= numelems)
				{
					if (walker && (iObjPathFlags & OBJPATHFLAG_CREATESTRUCTS))
					{
						void ***earray = TokenStoreGetEArray_inline(leafTpi, &leafTpi[column], column, walker, NULL);

						while (index >= eaSize(earray))
						{
							void *subStruct = StructCreateVoid(leafTpi[column].subtable);
							eaPush(earray, subStruct);
						}
					}
					else if (!(iObjPathFlags & OBJPATHFLAG_GETFINALKEY))
					{					
						SETERROR(PARSERRESOLVE_INDEX_BEYOND_BOUNDS_FULL, index, numelems);
						ok = false;
						index = -1;
						break;
					}
					// Out of bounds
					index = -1;
				}
			}
		}

		if(pSeg->descend) {			
			if (pSeg->isPoly)
			{
				//descend a polymorphic struct, this recursively calls ParserResolvePath in order to resolve the path after the poly.
				ParseTable *subtable = NULL;
				size_t subsize;

				if ((leafTpi[column].type & TOK_UNOWNED) && !(iObjPathFlags & OBJPATHFLAG_TRAVERSEUNOWNED))
				{
					SETERROR("Attempted to traverse unowned struct/poly, when not allowed to: %s (%s)", objPath->key->pathString, subpath);
					ok = false;
					break;
				}

				if (!TOK_HAS_SUBTABLE(leafTpi[column].type))
				{
					SETERROR("attempted to obtain field of non-struct: %s (%s)", objPath->key->pathString, subpath);
					ok = false;
					break;
				}

				if (walkerIsNull)
				{
					if (iObjPathFlags & OBJPATHFLAG_REPORTWHENENCOUNTERINGNULLPOLY)
					{
						SETERROR(SPECIAL_NULL_POLY_ERROR_STRING);
					}
					else
					{
						SETERROR("attempted to traverse null substruct in %s (%s)", objPath->key->pathString, subpath);
					}
					ok = false;
					break;
				}

				walker = StructGetSubtable(leafTpi, column, walker, index, &subtable, &subsize);


				if (!walker)
				{
					SETERROR("non-null polymorphic structure has an unknown type");
					ok = false;
					break;
				}

				leafTpi = subtable;
				subpath = (char*)(objPath->key->pathString + objPath->ppPath[i+1]->fieldOffset);

				ok = ParserResolvePathEx(subpath, leafTpi, walker, &subtable, &column, &walker, &index, NULL,  resultString, NULL, NULL, iObjPathFlags);
				leafTpi = subtable;
				if (walkerIsNull) walker = NULL;
				break;
			}
			else
			{
				//descend an array or a struct
				if (!TraverseObject(&leafTpi, &column, &walker, &index, objPath->key->pathString, 0, resultString, NULL, iObjPathFlags))
				{
					if (walkerIsNull) walker = NULL;
					if (tpiOut) *tpiOut = leafTpi;
					if (struct_out) *struct_out = walker;
					if (index_out) *index_out = index;
					if (column_out) *column_out = column;
					ok = false;
					break;
				}
			}
		}
		else
		{
			break;
		}
		if (walkerIsNull) walker = NULL;
	}
	
	if (ok)
	{
		if (tpiOut) *tpiOut = leafTpi;
		if (struct_out) *struct_out = walker;
		if (index_out) *index_out = index;
		if (column_out) *column_out = column;
	}
	PERFINFO_AUTO_STOP_L2();
	return ok;
}


bool ObjectPathGetParseTables(ObjectPath *objPath, const void *structPtr, ParseTable ***pppTables, char **resultString, U32 iObjPathFlags)
{
	void* walker = (void*)structPtr;
	ParseTable *leafTpi = (ParseTable*)objPath->key->rootTpi;
	bool ok = true;
	int i;
	int column = -1;
	int index = -1;
	int pathSize = eaSize(&objPath->ppPath);

	devassertmsgf(pathSize, "Compiled ObjectPath had no segments: %s\n", objPath->key->pathString);

	for (i = 0; i < pathSize && leafTpi; i++)
	{
		ObjectPathSegment *pSeg = objPath->ppPath[i];
		bool walkerIsNull = (walker == NULL);
		char *subpath = (char *)(objPath->key->pathString + pSeg->fieldOffset);
		column = pSeg->column;
		index = pSeg->index;

		//Push the parsetable.
		eaPush(pppTables, leafTpi);

		if (pSeg->key != 0)
		{
			if (walkerIsNull)
			{
				index = -1;
			}
			else if (!ParserResolveKey(pSeg->key, leafTpi, column, walker, &index, iObjPathFlags, NULL, NULL))
			{
				ok = false;
				break;
			}
		}
		else
		{
			index = pSeg->index;

			if (walkerIsNull)
			{
				index = -1;
			}

			if (index >= 0)
			{
				int numelems = TokenStoreGetNumElems_inline(leafTpi, &leafTpi[column], column, walker, NULL); 
				if (index >= numelems)
				{
					if (!(iObjPathFlags & OBJPATHFLAG_GETFINALKEY))
					{					
						SETERROR(PARSERRESOLVE_INDEX_BEYOND_BOUNDS_FULL, index, numelems);
						ok = false;
						index = -1;
						break;
					}
					// Out of bounds
					index = -1;
				}
			}
		}

		if(pSeg->descend) {			
			if (pSeg->isPoly)
			{
				//descend a polymorphic struct, this recursively calls ParserResolvePath in order to resolve the path after the poly.
				ParseTable *subtable = NULL;
				ObjectPath *subobjectpath;
				size_t subsize;

				if ((leafTpi[column].type & TOK_UNOWNED) && !(iObjPathFlags & OBJPATHFLAG_TRAVERSEUNOWNED))
				{
					SETERROR("Attempted to traverse unowned struct/poly, when not allowed to: %s (%s)", objPath->key->pathString, subpath);
					ok = false;
					break;
				}

				if (!TOK_HAS_SUBTABLE(leafTpi[column].type))
				{
					SETERROR("attempted to obtain field of non-struct: %s (%s)", objPath->key->pathString, subpath);
					ok = false;
					break;
				}

				if (walkerIsNull)
				{
					if (iObjPathFlags & OBJPATHFLAG_REPORTWHENENCOUNTERINGNULLPOLY)
					{
						SETERROR(SPECIAL_NULL_POLY_ERROR_STRING);
					}
					else
					{
						SETERROR("attempted to traverse null substruct in %s (%s)", objPath->key->pathString, subpath);
					}
					ok = false;
					break;
				}

				walker = StructGetSubtable(leafTpi, column, walker, index, &subtable, &subsize);


				if (!walker)
				{
					SETERROR("non-null polymorphic structure has an unknown type");
					ok = false;
					break;
				}

				leafTpi = subtable;
				subpath = (char*)(objPath->key->pathString + objPath->ppPath[i+1]->fieldOffset);

				ok = ParserResolvePathEx(subpath, leafTpi, walker, &subtable, &column, NULL, &index, &subobjectpath,  NULL, NULL, NULL, iObjPathFlags);
				if (!subobjectpath)
				{
					ok = false;
					break;
				}

				ok = ObjectPathGetParseTables(subobjectpath, walker, pppTables, resultString, iObjPathFlags);
				
				break;
			}
			else
			{
				//descend an array or a struct
				if (!TraverseObject(&leafTpi, &column, &walker, &index, objPath->key->pathString, 0, NULL, NULL, iObjPathFlags))
				{
					SETERROR("Failed to traverse struct.");
					ok = false;
					break;
				}
			}
		}
		else
		{
			break;
		}
		if (walkerIsNull) walker = NULL;
	}
	
	return ok;
	
}

//Returns true if the beginning segments of the longpath are equivalent to the shortpath.
// *Use this to figure out if longpath is a descendant of shortpath.
// *This function assumes the ObjectPaths have the same rootTPI.
bool ObjectPathIsDescendant(ObjectPath *longpath, ObjectPath *shortpath)
{
	if (!longpath || !shortpath)
		return false;

	if (longpath == shortpath)
		return true;

	if (eaSize(&longpath->ppPath) < eaSize(&shortpath->ppPath))
		return false;
	
	EARRAY_FOREACH_BEGIN(shortpath->ppPath, i);
	{	//compare each segment
		if (shortpath->ppPath[i]->column != longpath->ppPath[i]->column) return false;
		if (shortpath->ppPath[i]->index != longpath->ppPath[i]->index) return false;
		if (shortpath->ppPath[i]->key != longpath->ppPath[i]->key) return false;
	}
	EARRAY_FOREACH_END;
	return true;
}


MP_DEFINE(ParseTableInfo);
StashTable g_parsetableinfos = NULL; // indexed by parse table address
StashTable gParseTablesByName = NULL; //indexed by name

// empty parse table that marks memory that isn't attached to any other parse table
ParseTable PARSER_RAW_MEMORY[] = { { 0 } }; 


//Functions to set the info that is stored directly in the TPI itself

void SetTPISizeInTPIInfoColumn(ParseTable table[], int size)
{
	if (TPIHasTPIInfoColumn(table))
	{
		table[0].storeoffset = size;
	}
}


int ParserGetTableSize(ParseTable table[])
{
	ParseTableInfo *pTPIInfo;

	if (TPIHasTPIInfoColumn(table))
	{
		return (int)table[0].storeoffset;
	}
	
	pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo && pTPIInfo->size)
	{
		return pTPIInfo->size;
	}

	return 0;
}


// Only takes a static string (either from allocAddString or static in the process image)
void SetTPINameInTPIInfoColumn(ParseTable table[], const char *pName)
{
	if (TPIHasTPIInfoColumn(table))
	{
		//if (table[0].name && 0!=stricmp(table[0].name, pName))
		//	printf("Table with two names: %s and %s\n", table[0].name, pName);
		if (!table[0].name) // At least with standard parser stuff, we want the original name, as the second name is likely a TOK_REDUNDANT_NAME
			table[0].name = (char*)pName;
	}
}

const char *ParserGetTableName(ParseTable table[])
{
	ParseTableInfo *pTPIInfo;
	if (!table)
		return NULL;

	if (TPIHasTPIInfoColumn(table))
	{
		return table[0].name;
	}

	pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo && pTPIInfo->name)
	{
		return pTPIInfo->name;
	}

	return NULL;
}

const char *ParserGetTableSourceFileName(ParseTable table[])
{
	ParseTableInfo *pTPIInfo;


	pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo && pTPIInfo->pSourceFileName)
	{
		return pTPIInfo->pSourceFileName;
	}

	return NULL;
}

void ParserSetTPIUsesSingleThreadedMemPool(ParseTable table[], MemoryPool *pPool)
{
	ParseTableInfo *pTPIInfo = ParserGetTableInfo(table);
	pTPIInfo->pSingleThreadedMemPool = pPool;
}



void ParserSetTPIUsesThreadSafeMemPool(ParseTable table[], ThreadSafeMemoryPool *pPool)
{
	ParseTableInfo *pTPIInfo = ParserGetTableInfo(table);
	pTPIInfo->pThreadSafeMemPool = pPool;
}

void ParserSetTPINoMemTracking(ParseTable table[])
{
	ParseTableInfo *pTPIInfo = ParserGetTableInfo(table);
	pTPIInfo->bNoMemoryTracking = true;
}



MemoryPool ParserGetTPISingleThreadedMemPool(ParseTable *pTable)
{ 
	ParseTableInfo *pInfo = ParserGetTableInfo(pTable);
	if (pInfo)
	{
		return pInfo->pSingleThreadedMemPool ? *(pInfo->pSingleThreadedMemPool) : NULL;
	}
	return NULL;
}


ThreadSafeMemoryPool *ParserGetTPIThreadSafeMemPool(ParseTable *pTable)
{ 
	ParseTableInfo *pInfo = ParserGetTableInfo(pTable);
	if (pInfo)
	{
		return pInfo->pThreadSafeMemPool ? pInfo->pThreadSafeMemPool : NULL;
	}
	return NULL;
}

void SetFixupCBInTPIInfoColumn(ParseTable table[], TextParserAutoFixupCB *pFixupCB)
{
	if (TPIHasTPIInfoColumn(table))
	{
		table[0].param = (intptr_t)(pFixupCB);
	}
}

TextParserAutoFixupCB * ParserGetTableFixupFunc(ParseTable table[])
{
	ParseTableInfo *pTPIInfo;
	
	if (TPIHasTPIInfoColumn(table))
	{
		return (TextParserAutoFixupCB*)table[0].param;
	}

	pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo && pTPIInfo->pFixupCB)
	{
		return pTPIInfo->pFixupCB;
	}


	return NULL;
}


void SetKeyColumnInTPIInfoColumn(ParseTable table[], int iKeyColumn)
{
	if (TPIHasTPIInfoColumn(table))
	{
		table[0].format &= 0xffff0000;
		table[0].format |= iKeyColumn == -1 ? 0xffff : iKeyColumn;
	}
}

int ParserGetTableKeyColumn(ParseTable table[])
{
	ParseTableInfo *pTPIInfo;

	if (TPIHasTPIInfoColumn(table))
	{
		int retVal = table[0].format & 0xffff;

		return retVal == 0xffff ? -1 : retVal;
	}

	pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo && pTPIInfo->keycolumn != -1)
	{
		return pTPIInfo->keycolumn;
	}

	return -1;
}

bool ParserColumnIsIndexedEArray(ParseTable tpi[], int column, int *keyColumn)
{
	int foundKey;

	if (!tpi[column].subtable)
	{
		//only possible if the earray is LATEBINDed.
		//TODO: make that case work somehow, or at least complain at run time
		return false;
	}

	if (tpi[column].type & TOK_EARRAY &&
		TOK_GET_TYPE(tpi[column].type) == TOK_STRUCT_X &&
		!(tpi[column].type & TOK_NO_INDEX))
	{
		foundKey = ParserGetTableKeyColumn(tpi[column].subtable);
		if (keyColumn)
		{
			*keyColumn = foundKey;
		}
		if (foundKey >= 0)
		{
			return true;	
		}
	}
	else if (keyColumn)
	{
		*keyColumn = -1;
	}
	return false;
}


void SetTypeColumnInTPIInfoColumn(ParseTable table[], int iTypeColumn)
{
	if (TPIHasTPIInfoColumn(table))
	{
		table[0].format &= 0x8000ffff;
		table[0].format |= (iTypeColumn == -1 ? (0x7fff0000) : (iTypeColumn << 16));
	}
}
int ParserGetTableObjectTypeColumn(ParseTable table[])
{
	ParseTableInfo *pTPIInfo;

	if (TPIHasTPIInfoColumn(table))
	{
		int retVal = (table[0].format>>16) & 0x7fff;

		return retVal == 0x7fff ? -1 : retVal;
	}

	pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo && pTPIInfo->typecolumn != -1)
	{
		return pTPIInfo->typecolumn;
	}

	return -1;
}

int ParserCanTpiBeCopiedFast(ParseTable table[])
{
	if (TPIHasTPIInfoColumn(table))
	{
		
		if ((table[0].format & (0x80000000)) != 0)
		{
			return (int)table[0].storeoffset;
		}
	}

	return 0;
}

bool ParserFigureOutIfTpiCanBeCopiedFast(ParseTable table[])
{
	int i;

	FORALL_PARSETABLE(table, i)
	{
		StructTypeField eType = TOK_GET_TYPE(table[i].type);
		

		if (eType == TOK_STRUCT_X)
		{
			if (TokenStoreGetStorageType(table[i].type) == TOK_STORAGE_DIRECT_SINGLE)
			{
				if (!ParserFigureOutIfTpiCanBeCopiedFast(table[i].subtable))
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		else if (eType == TOK_STRING_X)
		{
			switch(TokenStoreGetStorageType(table[i].type))
			{
			case TOK_STORAGE_DIRECT_SINGLE:
				break;

			case TOK_STORAGE_INDIRECT_SINGLE:
				if (!(table[i].type & TOK_POOL_STRING))
				{
					return false;
				}

			default:
				return false;
			}
		}
		else if (TokenStoreGetStorageType(table[i].type) != TOK_STORAGE_DIRECT_SINGLE && TokenStoreGetStorageType(table[i].type) != TOK_STORAGE_DIRECT_FIXEDARRAY)
		{
			return false;
		}
		else if (TYPE_INFO(table[i].type).copystruct_func)
		{
			return false;
		}
	}

	return true;
}


#define TPI_NUM_COLUMNS_BITSIZE 10
#define TPI_NUM_COLUMNS_SHIFT 8
#define TPI_NUM_COLUMNS_MASK ((1 << TPI_NUM_COLUMNS_BITSIZE) - 1)

void SetNumColumnsInTPIInfoColumn(ParseTable table[], int iNumColumns)
{
	assertmsgf(iNumColumns < (1 << TPI_NUM_COLUMNS_BITSIZE), "Too many columns in a parse table... max is %u", (1 << TPI_NUM_COLUMNS_BITSIZE));
	if (TPIHasTPIInfoColumn(table))
	{
		table[0].type &= ~(TPI_NUM_COLUMNS_MASK << TPI_NUM_COLUMNS_SHIFT);
		table[0].type |= ((StructTypeField)iNumColumns) << TPI_NUM_COLUMNS_SHIFT;
	}
}

int ParserGetTableNumColumns(ParseTable table[])
{
	ParseTableInfo *pTPIInfo;

	if (TPIHasTPIInfoColumn(table))
	{
		return (table[0].type >> TPI_NUM_COLUMNS_SHIFT) & (TPI_NUM_COLUMNS_MASK);
	}

	pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo)
	{
		return pTPIInfo->numcolumns;
	}

	return 0;
}

#define TPI_CREATION_COMMENT_OFFSET_BITSIZE 8
#define TPI_CREATION_COMMENT_OFFSET_SHIFT 18
#define TPI_CREATION_COMMENT_OFFSET_MASK ((1 << TPI_CREATION_COMMENT_OFFSET_BITSIZE) - 1)



void SetCreationCommentOffsetInTPIInfoColumn(ParseTable table[], int iOffset)
{
	assertmsgf(TPIHasTPIInfoColumn(table), "can't set creation column Offset for %s... no tpi info column", ParserGetTableName(table));
	assertmsgf(iOffset < (1 << TPI_CREATION_COMMENT_OFFSET_BITSIZE), "Creation column Offset out of range, max is %u", (1 << TPI_CREATION_COMMENT_OFFSET_BITSIZE));

	table[0].type &= ~(TPI_CREATION_COMMENT_OFFSET_MASK << TPI_CREATION_COMMENT_OFFSET_SHIFT);
	table[0].type |= ((StructTypeField)iOffset) << TPI_CREATION_COMMENT_OFFSET_SHIFT;
}

int ParserGetCreationCommentOffset(ParseTable table[])
{
	if (TPIHasTPIInfoColumn(table))
	{
		return (table[0].type >> TPI_CREATION_COMMENT_OFFSET_SHIFT) & (TPI_CREATION_COMMENT_OFFSET_MASK);
	}

	return 0;
}




int TableGetDirtyBitOffset(ParseTable table[])
{
	ParseTableInfo *pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo)
	{
		return pTPIInfo->iDirtyBitOffset;
	}

	return 0;
}







void SetTPIInfoPointerInTPIInfoColumn(ParseTable table[], ParseTableInfo *pInfo)
{
	if (TPIHasTPIInfoColumn(table))
	{
		table[0].subtable = pInfo;
	}
}

bool ParserHasTableFixupFunc(ParseTable table[])
{
	ParseTableInfo* info = ParserGetTableInfo(table);
	return !!(info && info->pFixupCB);
}

//tries to find the dirty bit for a parsetable... asserts if it finds multiples. Returns the byte
//offset, or -1 if none
int FindParseTableDirtyBitOffset(ParseTable table[], const char *pName)
{
	int iRetVal = -1;
	int i;

	FORALL_PARSETABLE(table, i)
	{
		if (table[i].type & TOK_DIRTY_BIT)
		{
			assertmsgf(iRetVal == -1, "Table %s seems to have multiple dirty bits, perhaps due to flat embedding. This is bad.",
				pName ? pName : "(UNNAMED)");
			iRetVal = (int)table[i].storeoffset;
		}
	}

	return iRetVal;
}


bool ParserTableHasDirtyBit(ParseTable table[])
{
	ParseTableInfo *pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo)
	{
		return pTPIInfo->bHasDirtyBit;
	}

	return false;
}

extern int do_struct_send_logging;

StashTable dirtyBitLogTable;
CRITICAL_SECTION dirtyBitLogCS;

typedef struct DirtyBitLogInfo{
	const char* structName;
	const char* file;
	int line;
} DirtyBitLogInfo;

typedef struct DirtyBitLogEntry{
	DirtyBitLogInfo info;
	int count;
} DirtyBitLogEntry;

void dirtyBitLogInit(void)
{
	dirtyBitLogTable = stashTableCreateFixedSize(128, sizeof(DirtyBitLogInfo));
	InitializeCriticalSection(&dirtyBitLogCS);
}

int tpsDirtyBitsPushEntries(DirtyBitLogEntry*** entries, StashElement elem)
{
	DirtyBitLogEntry* entry = stashElementGetPointer(elem);

	eaPush(entries, entry);
	return 1;
}

int cmpDirtyBitLogEntry(const DirtyBitLogEntry** lhs, const DirtyBitLogEntry** rhs)
{
	return (*lhs)->count - (*rhs)->count;
}

AUTO_COMMAND;
void tpsDirtyBits(void)
{
	int i;
	DirtyBitLogEntry** entries = NULL;
	
	stashForEachElementEx(dirtyBitLogTable, tpsDirtyBitsPushEntries, &entries);
	eaQSort(entries, cmpDirtyBitLogEntry);

	for(i = eaSize(&entries)-1; i >= 0; i--)
	{
		DirtyBitLogInfo* info = &entries[i]->info;
		printf("%6d Struct %s (file: %s, line: %d)\n", entries[i]->count,
			info->structName, info->file, info->line);
	}
	eaDestroy(&entries);
}

int ParserSetDirtyBit_sekret(ParseTable table[], void *pStruct, bool bSubStructsChanged, MEM_DBG_PARMS_VOID)
{
	ParseTableInfo *pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo)
	{
		if (pTPIInfo->bHasDirtyBit)
		{
			if(do_struct_send_logging)
			{
				DirtyBitLogInfo info;
				DirtyBitLogEntry* entry = NULL;

				info.structName = pTPIInfo->name;
				info.file = caller_fname;
				info.line = line;

				EnterCriticalSection(&dirtyBitLogCS);
				if(!stashFindPointer(dirtyBitLogTable, &info, &entry))
				{
					entry = calloc(1, sizeof(*entry));
					memcpy(&entry->info, &info, sizeof(info));
					stashAddPointer(dirtyBitLogTable, &entry->info, entry, false);
				}
				entry->count++;
				LeaveCriticalSection(&dirtyBitLogCS);
			}
			(((U8*)pStruct)[pTPIInfo->iDirtyBitOffset]) = 1;
			return true;
		}
		else
		{
			devassertmsgf(0, "Failed to set the dirty bit on parse table %s because it doesn't have one. Call entity_setDirtyBit() on a parent struct that has a dirty bit instead.", pTPIInfo->name);
		}
	}
	return false;
}

void ParserClearDirtyBit(ParseTable table[], void *pStruct)
{
	ParseTableInfo *pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo)
	{
		if (pTPIInfo->bHasDirtyBit)
		{
			(((U8*)pStruct)[pTPIInfo->iDirtyBitOffset]) = 0;
		}
	}
}

void RecursivelySetAllYourParentsToHaveDirtyBitChildren(ParseTableGraphHandle *pHandle)
{
	int i;

	if (pHandle->iUserData)
	{
		return;
	}
	pHandle->iUserData = 1;

	for (i=0 ; i < eaSize(&pHandle->ppParents); i++)
	{
		ParseTableInfo *pTPIInfo = ParserGetTableInfo(pHandle->ppParents[i]->pTable);
		pTPIInfo->eChildrenHaveDirtyBit = TABLEHASDIRTYBIT_YES;
		RecursivelySetAllYourParentsToHaveDirtyBitChildren(pHandle->ppParents[i]);
	}
}



void CalculateRecursiveDirtyBitHaving(ParseTable table[])
{
	ParseTableGraph *pGraph = MakeParseTableGraph(table, 0, 0);
	int i;

	for (i=0; i < eaSize(&pGraph->ppHandles); i++)
	{
		if (ParserTableHasDirtyBit(pGraph->ppHandles[i]->pTable))
		{
			RecursivelySetAllYourParentsToHaveDirtyBitChildren(pGraph->ppHandles[i]);
		}
	}

	for (i=0; i < eaSize(&pGraph->ppHandles); i++)
	{
		ParseTableInfo *pTPIInfo = ParserGetTableInfo(pGraph->ppHandles[i]->pTable);

		if (pTPIInfo->eChildrenHaveDirtyBit == TABLEHASDIRTYBIT_UNKNOWN)
		{
			pTPIInfo->eChildrenHaveDirtyBit = TABLEHASDIRTYBIT_NO;
		}
	}

	DestroyParseTableGraph(pGraph);
}

bool ParserChildrenHaveDirtyBit(ParseTable table[])
{
	ParseTableInfo *pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo)
	{
		if (pTPIInfo->eChildrenHaveDirtyBit == TABLEHASDIRTYBIT_UNKNOWN)
		{
			CalculateRecursiveDirtyBitHaving(table);
		}

		return (pTPIInfo->eChildrenHaveDirtyBit == TABLEHASDIRTYBIT_YES);
	}

	return false;
}

void VerifyParseTable(ParseTable tpi[], const char *pName)
{
	int i, j;
	FORALL_PARSETABLE(tpi, i)
	{
		if (tpi[i].type & (TOK_REDUNDANTNAME | TOK_PARSETABLE_INFO))
		{
			continue;
		}

		if (TOK_GET_TYPE(tpi[i].type) == TOK_IGNORE)
		{
			continue;
		}


		FORALL_PARSETABLE(tpi, j)
		{
			if (j > i)
			{
				if (tpi[j].type & (TOK_REDUNDANTNAME | TOK_PARSETABLE_INFO))
				{
					continue;
				}

				if (TOK_GET_TYPE(tpi[j].type) == TOK_IGNORE)
				{
					continue;
				}

				if (tpi[i].name && tpi[i].name[0] && tpi[j].name && stricmp(tpi[i].name, tpi[j].name) == 0)
				{
					assertmsgf(0, "ParseTable %s has two non-redundant fields both named %s", pName, tpi[i].name);
				}
			}
		}
	}
}

void ParserSetTableFixupFunc(ParseTable pti[], TextParserAutoFixupCB *pFixupCB)
{
	ParseTableInfo* info = ParserGetTableInfo(pti);

	if (pFixupCB)
	{
		if (info->pFixupCB)
		{
			assertmsg(pFixupCB == info->pFixupCB, "Attempting to set fixup CB again with different CB");
		}
		else
		{
			info->pFixupCB = pFixupCB;
			SetFixupCBInTPIInfoColumn(pti, pFixupCB);
		}
	}
}

// must set size here, name can be NULL (presumably to be filled in later)
bool ParserSetTableInfoEx(ParseTable table[], int size, const char* name, TextParserAutoFixupCB *pFixupCB, const char *pSourceFileName, SetTableInfoFlags eFlags)
{
	int i;
	ParseTableInfo* info = ParserGetTableInfo(table);
	bool presumablyDidMapping = info->name && info->pSourceFileName;
	int iDirtyBitOffset = FindParseTableDirtyBitOffset(table, name);

	//this is a good time to do some basic verification
	VerifyParseTable(table, name);
		
	info->bHasDirtyBit = (iDirtyBitOffset != -1);
	info->iDirtyBitOffset = iDirtyBitOffset;
	info->bTPIIsTemporary = !!(eFlags & SETTABLEINFO_TABLE_IS_TEMPORARY);
	info->bAllowCRCCaching = !!(eFlags & SETTABLEINFO_ALLOW_CRC_CACHING);

	if (!gParseTablesByName)
	{
		gParseTablesByName = stashTableCreateWithStringKeysEx(1024, 0, FAKEFILENAME_PARSETABLEINFOS, __LINE__);
	}

	// we are attempting to infer the size & name through normal textparser calls
	// this means the name may not be correct, but we're ok with that.  If the size 
	// changes, this is probably a more serious problem though
	if (size)
	{
		if (info->size && info->size != size)
		{
			assertmsgf(0, "%s: attempting to set table info again with different size!", __FUNCTION__);
			return false;
		}
		info->size = size;

		SetTPISizeInTPIInfoColumn(table, size);
	}

	if (eFlags & SETTABLEINFO_TABLE_IS_TEMPORARY)
	{
		info->name = allocAddString(TEMPORARYTPI_MEMORY_NAME);
	}
	else if (name)
	{
		if (info->name)
		{
			//the new name may be incorrect, so only use it if we don't already have a name, which, 
			//having been set by autorun-time fixup stuff, is presumably correct. Only use the other
			//name as a backup
		}
		else
		{
			info->name = (eFlags & SETTABLEINFO_NAME_STATIC)?name:allocAddString(name);

			SetTPINameInTPIInfoColumn(table, info->name);

			if (!(eFlags & SETTABLEINFO_TABLE_IS_TEMPORARY))
			{
				stashAddPointer(gParseTablesByName, info->name, table, 0);
			}
		}
	}


	if (pFixupCB)
	{
		if (info->pFixupCB)
		{
			assertmsg(pFixupCB == info->pFixupCB, "Attempting to set fixup CB again with different CB");
		}
		else
		{
			info->pFixupCB = pFixupCB;
			SetFixupCBInTPIInfoColumn(table, pFixupCB);
		}
	}

	if (pSourceFileName)
	{
		info->pSourceFileName = pSourceFileName;
	}

	if (!presumablyDidMapping && info->name && info->pSourceFileName)
	{
		// Have the name and filename for the first time
		memBudgetAddStructMapping(info->name, info->pSourceFileName);
	}

	FORALL_PARSETABLE(table, i)
	{
		if (eFlags & SETTABLEINFO_SAVE_ORIGINAL_CASE_FIELD_NAMES)
		{
			eaSet(&info->ppOriginalCaseFieldNames, strdup(table[i].name), i);
		}

		if (GetAppGlobalType() == GLOBALTYPE_CLIENT && !g_disallow_static_strings)
		{
			table[i].name = allocAddStaticString(table[i].name);
		}
		else
		{
			table[i].name = allocAddString(table[i].name);
		}
	}

	return true;
}

bool ParserAddParent(ParseTable table[], ParseTable* parent)
{
	int i;
	ParseTableInfo* info = ParserGetTableInfo(table);

	if (!parent)
	{
		return false;
	}

	for (i = 0; i < eaSize(&info->tableinfo_parents); i++)
	{
		if (info->tableinfo_parents[i] == parent) return true;
	}
	eaPush_dbg(&info->tableinfo_parents, parent, FAKEFILENAME_PARSETABLEINFOS, __LINE__);
	return true;
}

bool ParserSetTableInfoRecurse(ParseTable table[], int size, const char* name, ParseTable* parent, const char *pSourceFileName, StashTable noRecurseTable, SetTableInfoFlags eFlags)
{
	bool ret = true;
	int i;
	bool bRoot = false;

	ParseTableInfo* parentInfo;

	if (!noRecurseTable)
	{
		bRoot = true;
		noRecurseTable = stashTableCreateAddress(16);
	}
	else
	{
		if (stashFindPointer(noRecurseTable, table, NULL))
		{
			return ret;
		}
	}

	stashAddPointer(noRecurseTable, table, NULL, false);

	parentInfo = parent?ParserGetTableInfo(parent):NULL;
	ret = ParserSetTableInfoEx(table, size, name, NULL, pSourceFileName?pSourceFileName:parentInfo?parentInfo->pSourceFileName:NULL, eFlags);
	ParserAddParent(table, parent);

	FORALL_PARSETABLE(table, i)
	{
		// HACK - only recursing through named tables here.
		// makes sense to me because it avoids hitting placeholder tables, but
		// does rule out having tables with no name at all
		if (TOK_SHOULD_RECURSE_PARSEINFO(table, i) &&
			table[i].name && table[i].name[0])
		{
			ret = ret && ParserSetTableInfoRecurse(table[i].subtable, table[i].param, table[i].name, table, pSourceFileName, noRecurseTable, eFlags & (~SETTABLEINFO_NAME_STATIC));
		}
	}


	if (bRoot)
	{
		stashTableDestroy(noRecurseTable);
	}


	return ret;

}

static void CalcFieldNameLengths(ParseTable table[], ParseTableInfo* info)
{
	int i;

	FORALL_PARSETABLE(table, i)
	{
		int iLen = table[i].name ? (int)strlen(table[i].name) : 0;

		ea32Push(&info->piColumnNameLengths, iLen);

		if (iLen > info->iLongestColumnNameLength)
		{
			info->iLongestColumnNameLength = iLen;
		}
	}

}

void InitIndexedCompareCache(ParseTableInfo *pInfo, ParseTable tpi[], int iKeyColumn)
{
	int iType = TOK_GET_TYPE(tpi[iKeyColumn].type);
	int iStorageType = TokenStoreGetStorageType(tpi[iKeyColumn].type);

	switch (iType)
	{
	xcase TOK_INT_X:
		if (iStorageType == TOK_STORAGE_DIRECT_SINGLE)
		{
			pInfo->IndexedCompareCache.eCompareType = INDEXCOMPARETYPE_INT32;
			pInfo->IndexedCompareCache.storeoffset = (int)(tpi[iKeyColumn].storeoffset);
		}
	xcase TOK_U8_X:
		if (iStorageType == TOK_STORAGE_DIRECT_SINGLE)
		{
			pInfo->IndexedCompareCache.eCompareType = INDEXCOMPARETYPE_INT8;
			pInfo->IndexedCompareCache.storeoffset = (int)(tpi[iKeyColumn].storeoffset);
		}
	xcase TOK_INT16_X:
		if (iStorageType == TOK_STORAGE_DIRECT_SINGLE)
		{
			pInfo->IndexedCompareCache.eCompareType = INDEXCOMPARETYPE_INT16;
			pInfo->IndexedCompareCache.storeoffset = (int)(tpi[iKeyColumn].storeoffset);
		}
	xcase TOK_INT64_X:
		if (iStorageType == TOK_STORAGE_DIRECT_SINGLE)
		{
			pInfo->IndexedCompareCache.eCompareType = INDEXCOMPARETYPE_INT64;
			pInfo->IndexedCompareCache.storeoffset = (int)(tpi[iKeyColumn].storeoffset);
		}

	xcase TOK_STRING_X:
	case TOK_CURRENTFILE_X:
	case TOK_FILENAME_X:
		if (iStorageType == TOK_STORAGE_DIRECT_SINGLE)
		{
			pInfo->IndexedCompareCache.eCompareType = INDEXCOMPARETYPE_STRING_EMBEDDED;
			pInfo->IndexedCompareCache.storeoffset = (int)(tpi[iKeyColumn].storeoffset);
		}
		else if (iStorageType == TOK_STORAGE_INDIRECT_SINGLE)
		{
			pInfo->IndexedCompareCache.eCompareType = INDEXCOMPARETYPE_STRING_POINTER;
			pInfo->IndexedCompareCache.storeoffset = (int)(tpi[iKeyColumn].storeoffset);
		}
	
	xcase TOK_REFERENCE_X:
		pInfo->IndexedCompareCache.eCompareType = INDEXCOMPARETYPE_REF;
		pInfo->IndexedCompareCache.storeoffset = (int)(tpi[iKeyColumn].storeoffset);

	xcase TOK_STRUCT_X:
		if (iStorageType == TOK_STORAGE_DIRECT_SINGLE)
		{
			//special case... if the struct is a containerRef, treat it as an int64
			if (tpi[iKeyColumn].subtable == parse_ContainerRef)
			{
				pInfo->IndexedCompareCache.eCompareType = INDEXCOMPARETYPE_INT64;
				pInfo->IndexedCompareCache.storeoffset = (int)(tpi[iKeyColumn].storeoffset);
			}
			else
			{
				pInfo->IndexedCompareCache.eCompareType = INDEXCOMPARETYPE_STRUCT_EMBEDDED;
				pInfo->IndexedCompareCache.storeoffset = (int)(tpi[iKeyColumn].storeoffset);
				pInfo->IndexedCompareCache.pTPIForStructComparing = tpi[iKeyColumn].subtable;
			}
		}
		else if (iStorageType == TOK_STORAGE_INDIRECT_SINGLE)
		{
			pInfo->IndexedCompareCache.eCompareType = INDEXCOMPARETYPE_STRUCT_POINTER;
			pInfo->IndexedCompareCache.storeoffset = (int)(tpi[iKeyColumn].storeoffset);
			pInfo->IndexedCompareCache.pTPIForStructComparing = tpi[iKeyColumn].subtable;
		}
	
	}

	//now check for fast key generation
	if (pInfo->IndexedCompareCache.eCompareType)
	{
		switch (iType)
		{
		case TOK_INT_X:
		case TOK_U8_X:
		case TOK_INT16_X:
		case TOK_INT64_X:
			//INTs can be written out fast as long as there's no weird formatting involved
			{
				int format = TOK_GET_FORMAT_OPTIONS(tpi[iKeyColumn].format);
				if (format)
				{
					return;
				}

				pInfo->IndexedCompareCache.bCanGetStringKeysFast = true;
				pInfo->IndexedCompareCache.pStaticDefineForEnums = tpi[iKeyColumn].subtable;
			}
			break;

			//string types of all sorts all work fine
		case TOK_STRING_X:
		case TOK_CURRENTFILE_X:
		case TOK_FILENAME_X:
		case TOK_REFERENCE_X:
			pInfo->IndexedCompareCache.bCanGetStringKeysFast = true;
			break;
		}
	}
}

//called specifically by schema loading, because during schema loading sometimes the indexedCompareCache gets set up
//before the offsets in the struct are all set up right, so then we need to redo it
void ParserInitIndexedCompareCache(ParseTable *pTable)
{
	ParseTableInfo *pInfo = ParserGetTableInfo(pTable);
	if (pInfo->keycolumn != -1)
	{
		InitIndexedCompareCache(pInfo, pTable, pInfo->keycolumn);
	}
}

ParseTableInfo *ParserGetTableInfoInternal(ParseTable table[])
{
	int i = 0;
	ParseTableInfo* info = 0;

	PERFINFO_AUTO_START_FUNC();

	// in macro now
	/*
	if (TPIHasTPIInfoColumn(table))
	{
		if (table[0].subtable)
		{
			PERFINFO_AUTO_STOP();
			return table[0].subtable;
		}
	}
	*/

	

	if (!g_parsetableinfos)	
	{
		EnterCriticalSection(&gTextParserGlobalLock);
		if (!g_parsetableinfos)
			g_parsetableinfos = stashTableCreateAddressEx(1024, FAKEFILENAME_PARSETABLEINFOS, __LINE__);
		LeaveCriticalSection(&gTextParserGlobalLock);

	}
	//This is slow, but shouldn't matter most of the time.
	EnterCriticalSection(&gTextParserGlobalLock);
	stashAddressFindPointer(g_parsetableinfos, table, &info);
	LeaveCriticalSection(&gTextParserGlobalLock);
	if (!info) // create an info record with anything that can be calculated
	{
		EnterCriticalSection(&gTextParserGlobalLock);
		stashAddressFindPointer(g_parsetableinfos, table, &info);
		if (!info)
		{
			PERFINFO_AUTO_START("Create new PTI", 1);

			TEST_PARSE_TABLE(table);

			MP_CREATE_DBG(ParseTableInfo, 100, FAKEFILENAME_PARSETABLEINFOS, __LINE__);
			info = MP_ALLOC(ParseTableInfo);
			memset(info, 0, sizeof(ParseTableInfo));
			info->table = table;
			info->name = 0;
			info->size = 0;
			info->keycolumn = -1;
			info->typecolumn = -1;
			info->pFixupCB = NULL;

			SetKeyColumnInTPIInfoColumn(table, -1);
			SetTypeColumnInTPIInfoColumn(table, -1);

			// figure out id & key columns
			if (table)
			{
				FORALL_PARSETABLE(table, i)
				{
					if (table[i].type & TOK_KEY)
					{
						if (TOK_GET_TYPE(table[i].type) == TOK_STRUCT_X)
						{
							ParseTable *subtable = table[i].subtable;
							int j;
							FORALL_PARSETABLE(subtable, j)
							{
								if (TOK_GET_TYPE(subtable[j].type) == TOK_IGNORE ||
									TOK_GET_TYPE(subtable[j].type) == TOK_START ||
									TOK_GET_TYPE(subtable[j].type) == TOK_END)
								{
									continue;
								}
								assertmsgf(subtable[j].type & TOK_STRUCTPARAM,
									"Can't use non-structparam only structure for key field %s in %s!", table[i].name, info->name);
							}
						}					
						else
						{					
							assertmsgf(TOK_GET_TYPE(table[i].type) == TOK_INT_X || TOK_GET_TYPE(table[i].type) == TOK_U8_X 
								|| TOK_GET_TYPE(table[i].type) == TOK_INT16_X || TOK_GET_TYPE(table[i].type) == TOK_INT64_X 
								|| TOK_GET_TYPE(table[i].type) == TOK_STRING_X || TOK_GET_TYPE(table[i].type) == TOK_REFERENCE_X
								|| TOK_GET_TYPE(table[i].type) == TOK_CURRENTFILE_X || TOK_GET_TYPE(table[i].type) == TOK_FILENAME_X,
								"Unsupported type for a key field %s in %s!", table[i].name, info->name);
						}
						assertmsg(info->keycolumn == -1, "Multiple keys defined in single parse table!");
						info->keycolumn = i;

						SetKeyColumnInTPIInfoColumn( table, i );

						InitIndexedCompareCache(info, table, i);



					}
					if (table[i].type & TOK_OBJECTTYPE)
					{
						assertmsg(TOK_GET_TYPE(table[i].type) == TOK_INT_X || TOK_GET_TYPE(table[i].type) == TOK_INT64_X
							|| TOK_GET_TYPE(table[i].type) == TOK_STRING_X,
							"Unsupported type for an object id field!");
						assertmsg(info->typecolumn == -1, "Multiple object type tags defined in a single parse table!");
						info->typecolumn = i;

						SetTypeColumnInTPIInfoColumn( table, i );
					}
				}
				info->numcolumns = i;
				SetNumColumnsInTPIInfoColumn(table, i);
				SetTPIInfoPointerInTPIInfoColumn(table, info);
			}
			stashAddressAddPointer(g_parsetableinfos, table, info, 0);

			if (TPIHasTPIInfoColumn(table))
			{
				if (ParserFigureOutIfTpiCanBeCopiedFast(table))
				{
					table[0].format |= 0x80000000;
				}
			}

			CalcFieldNameLengths(table, info);

			PERFINFO_AUTO_STOP();
		}
		LeaveCriticalSection(&gTextParserGlobalLock);
	}

	PERFINFO_AUTO_STOP();
	return info;
}




ParseTable *ParserGetTableFromStructName(const char *pName)
{
	ParseTable *pPointer;

	if (!gParseTablesByName)
	{
		return NULL;
	}
	
	if (stashFindPointer(gParseTablesByName, pName, &pPointer))
	{
		return pPointer;
	}
	
	return NULL;
}


bool ParserClearTableInfo(ParseTable table[])
{
	ParseTableInfo* info = 0;
	
	if (!ParserTableIsTemporary(table))
	{
		ObjectPathCleanTPI(table);
	}

	eaIndexedTemplateDestroy(table);

	if (stashAddressRemovePointer(g_parsetableinfos, table, &info) && info)
	{
		if (info->name && !info->bTPIIsTemporary)
		{
			stashRemovePointer(gParseTablesByName, info->name, NULL);
		}
		
		eaDestroyEx(&info->ppOriginalCaseFieldNames, NULL);

		eaDestroy(&info->tableinfo_parents);

		DestroyStructCopyInformation(info->pCopyWithNOASTs);
		DestroyStructCopyInformation(info->pCopyWithoutNOASTs);
		if (info->copyWithFlagsTable)
		{
			stashTableDestroyEx(info->copyWithFlagsTable, freeFunc, DestroyStructCopyInformation);
		}
		
		eaDestroyEx(&info->ppExtraAllocations, NULL);

		if (info->pStructInitInfo)
		{
			DestroyStructInitInfo(info->pStructInitInfo, table);
		}


		if (info->pStructDeInitInfo)
		{
			DestroyStructInitInfo(info->pStructDeInitInfo, table);
		}

		stashTableDestroy(info->sCachedCRCsByFlags);

		ea32Destroy(&info->piColumnNameLengths);

		MP_FREE(ParseTableInfo, info);
		return true;
	}
	return false;
}





StaticDefine* ParserColumnGetStaticDefineList(ParseTable *tpi, int column)
{
	if (TYPE_INFO(tpi[column].type).interpretfield(tpi, column, SubtableField) == StaticDefineList)
	{
		return (StaticDefine *)tpi[column].subtable;
	}
	return NULL;
}



//returns SIMPLECHILD if the table is a child of only one parent, and if that parent is also a simplechild
enumParseTableChildType GetTableTypeRecurse(ParseTable *pTable, int iDepth)
{
	ParseTableInfo* info;


	if (!stashAddressFindPointer(g_parsetableinfos, pTable, &info))
	{
		return TABLECHILDTYPE_UNKNOWN;
	}

	if (info->eMyChildType != TABLECHILDTYPE_UNKNOWN)
	{
		return info->eMyChildType;
	}

	if (iDepth > 32)
	{
		return info->eMyChildType = TABLECHILDTYPE_COMPLEXCHILD;
	}
	

	switch (eaSize(&info->tableinfo_parents))
	{
	case 0:
		return info->eMyChildType = TABLECHILDTYPE_PARENT;

	case 1:
		{
			enumParseTableChildType eMyParentType = GetTableTypeRecurse(info->tableinfo_parents[0], iDepth + 1);

			if (eMyParentType == TABLECHILDTYPE_PARENT || eMyParentType == TABLECHILDTYPE_SIMPLECHILD)
			{
				return info->eMyChildType = TABLECHILDTYPE_SIMPLECHILD;
			}
			else
			{
				return info->eMyChildType = TABLECHILDTYPE_COMPLEXCHILD;
			}
		}

	default:
		return info->eMyChildType = TABLECHILDTYPE_COMPLEXCHILD;
	}
}


bool ParserFindDependencies(ParseTable table[], void *structptr, ResourceInfo *parentObject, char *pathPrefix, bool bOnlyImportantRefs, bool bIncludePaths)
{
	char *pathString = NULL;
	int i;
	if (!structptr || !parentObject || !table)
		return false;

	if (bIncludePaths)
		estrStackCreate(&pathString);

	FORALL_PARSETABLE(table, i)
	{
		if (!table[i].name || !table[i].name[0])
		{
			continue;
		}
		if (table[i].type & TOK_REDUNDANTNAME)
		{
			continue;
		}
		if (TOK_GET_TYPE(table[i].type) == TOK_STRUCT_X || TOK_GET_TYPE(table[i].type) == TOK_POLYMORPH_X)
		{

			int index, size = TokenStoreGetNumElems_inline(table, &table[i], i, structptr, NULL);
			for (index = 0; index < size; index++)
			{				
				ParseTable *pSubTable;
				void *pStruct = StructGetSubtable(table, i, structptr, index, &pSubTable, NULL);

				if (pStruct)
				{				
					if (bIncludePaths)
					{
						if (pathPrefix)
						{
							estrCopy2(&pathString, pathPrefix);
						}
						else
						{
							estrClear(&pathString);
						}
					}

					if (!bIncludePaths || ParserAppendPath(&pathString, table, i, structptr, index))
					{									
						ParserFindDependencies(pSubTable, pStruct, parentObject, bIncludePaths ? pathString : NULL, bOnlyImportantRefs, bIncludePaths);
					}
				}				
			}
		}
		else if (TOK_GET_TYPE(table[i].type) == TOK_REFERENCE_X || TOK_GET_TYPE(table[i].type) == TOK_STRING_X)
		{
			const char *pDictionaryName = NULL;
			if (TOK_GET_TYPE(table[i].type) == TOK_REFERENCE_X)
			{
				pDictionaryName = table[i].subtable;
			}
			else if (stricmp(ParserGetTableName(table),"InheritanceData") == 0 &&
				stricmp(table[i].name, "ParentName") == 0)
			{
				// Special case for parent
				pDictionaryName = parentObject->resourceDict;
			}
			else
			{
				pDictionaryName = ParserColumnGetDictionaryName(table, i);
			}
			if (pDictionaryName)
			{
				int index, size = TokenStoreGetNumElems_inline(table, &table[i], i, structptr, NULL);
				for (index = 0; index < size; index++)
				{
					ResourceReference *newRef;
					const char *pReferentName;
					if (!(table[i].type & TOK_VITAL_REF) && bOnlyImportantRefs)
					{
						continue;
					}
					if (TOK_GET_TYPE(table[i].type) == TOK_REFERENCE_X)
					{
						pReferentName = TokenStoreGetRefString_inline(table, &table[i], i, structptr, index, NULL);
					}
					else
					{
						pReferentName = TokenStoreGetString_inline(table, &table[i], i, structptr, index, NULL);
					}
				
					if (pReferentName)
					{

						if (bIncludePaths)
						{						
							if (pathPrefix)
							{
								estrCopy2(&pathString, pathPrefix);
							}
							else
							{
								estrClear(&pathString);
							}
						}
						if (!bIncludePaths || ParserAppendPath(&pathString, table, i, structptr, index))
						{																					
							if (table[i].type & TOK_VITAL_REF)
							{
								newRef = resInfoGetOrCreateReference(parentObject, pDictionaryName, pReferentName, bIncludePaths?pathString:NULL, REFTYPE_CONTAINS, NULL);
							}							
							else
							{	
								newRef = resInfoGetOrCreateReference(parentObject, pDictionaryName, pReferentName, bIncludePaths?pathString:NULL, REFTYPE_REFERENCE_TO, NULL);
							}
						}
					}
				}
			}
		}
		else if (TOK_GET_TYPE(table[i].type) == TOK_FILENAME_X)
		{
			ResourceReference *newRef;
			int index, size = TokenStoreGetNumElems_inline(table, &table[i], i, structptr, NULL);
			for (index = 0; index < size; index++)
			{
				const char *tempFileName = TokenStoreGetString_inline(table, &table[i], i, structptr, index, NULL);
				char fileName[MAX_PATH];

				if (!(table[i].type & TOK_VITAL_REF) && bOnlyImportantRefs)
				{
					continue;
				}

				if (!tempFileName || !tempFileName[0])
				{
					continue;
				}
				fileLocateWrite(tempFileName, fileName);

				if (bIncludePaths)
				{				
					if (pathPrefix)
					{
						estrCopy2(&pathString, pathPrefix);
					}
					else
					{
						estrClear(&pathString);
					}
				}
				if (!bIncludePaths || ParserAppendPath(&pathString, table, i, structptr, index))
				{
					newRef = resInfoGetOrCreateReference(parentObject, "Filename", fileName, bIncludePaths ? pathString : NULL, REFTYPE_REFERENCE_TO, NULL);
				}
			}
		}
	}	
	if (bIncludePaths)
		estrDestroy(&pathString);
	return true;
}




//--------------TextParserBinaryBlock stuff


AUTO_RUN_LATE;
void TextParserBinaryBlock_Verify(void)
{
	int iSize;
	if (!GetIntFromTPIFormatString(parse_TextParserBinaryBlock + 2, "MAX_ARRAY_SIZE", &iSize) || iSize !=  TEXTPARSERBINARYBLOCK_MAXSIZE_WORDS)
	{
		assertmsgf(0, "TextParserBinaryBlock pInts must have FORMATSTRING MAX_ARRAY_SIZE that equals %d",  TEXTPARSERBINARYBLOCK_MAXSIZE_WORDS);
	}
}

int TextParserBinaryBlock_GetSize(TextParserBinaryBlock *pBlock)
{
	if (pBlock->iSizeBeforeZipping)
	{
		return pBlock->iSizeBeforeZipping;
	}

	return pBlock->iActualByteSize;
}


TextParserBinaryBlock *TextParserBinaryBlock_CreateFromMemory_Internal(char *pBuf, int iBufSize, const char *caller_fname, int line)
{
	TextParserBinaryBlock *pBlock = StructCreate(parse_TextParserBinaryBlock);
	int iWordCount;

	if (!iBufSize)
	{
		return pBlock;
	}

	assert(iBufSize <= TEXTPARSERBINARYBLOCK_MAXSIZE_BYTES);

	iWordCount = (iBufSize + 3) / 4;

	ea32SetSize(&pBlock->pInts, iWordCount);
	memcpy(pBlock->pInts, pBuf, iBufSize);
	pBlock->iActualByteSize = iBufSize;

	return pBlock;
}

TextParserBinaryBlock *TextParserBinaryBlock_CreateFromMemory_dbg(char *pBuf, int iBufSize, bool bZip, const char *caller_fname, int line)
{
	if (bZip)
	{
		char *pZippedBuffer;
		int iZipSize;
		TextParserBinaryBlock *pBlock;
	

		pZippedBuffer = zipData(pBuf, iBufSize, &iZipSize);


		pBlock = TextParserBinaryBlock_CreateFromMemory_Internal(pZippedBuffer, iZipSize, caller_fname, line);
		free(pZippedBuffer);
		pBlock->iSizeBeforeZipping = iBufSize;
		return pBlock;
	}
	else
	{
		return TextParserBinaryBlock_CreateFromMemory_Internal(pBuf, iBufSize, caller_fname, line);
	}
}




static void *TextParserBinaryBlock_PutIntoMallocedBuffer_Internal(TextParserBinaryBlock *pBlock, int *piOutSize, const char *caller_fname, int line, bool bNullTerminate)
{
	int iSize = pBlock->iActualByteSize;
	char *pOutBuf;

	if (piOutSize)
	{
		*piOutSize = iSize;
	}

	if (!iSize)
	{
		return NULL;
	}

	if (bNullTerminate)
	{
		pOutBuf = smalloc(iSize + 1);
		pOutBuf[iSize] = 0;
	}
	else
	{
		pOutBuf = smalloc(iSize);
	}
	memcpy(pOutBuf, pBlock->pInts, iSize);

	return pOutBuf;
}


void *TextParserBinaryBlock_PutIntoMallocedBuffer_dbg(TextParserBinaryBlock *pBlock, int *piOutSize, const char *caller_fname, int line)
{
	if (pBlock->iSizeBeforeZipping)
	{
		char *pZippedBuffer;
		int iZippedSize;
		char *pUnZippedBuffer;
		U32 iOutSize;

		if (piOutSize)
		{
			*piOutSize = pBlock->iSizeBeforeZipping;
		}

		pZippedBuffer = TextParserBinaryBlock_PutIntoMallocedBuffer_Internal(pBlock, &iZippedSize, caller_fname, line, false);
		pUnZippedBuffer = smalloc(pBlock->iSizeBeforeZipping + 1);
		pUnZippedBuffer[pBlock->iSizeBeforeZipping] = 0;
		iOutSize = pBlock->iSizeBeforeZipping;
		unzipData(pUnZippedBuffer, &iOutSize, pZippedBuffer, iZippedSize);
		assert(iOutSize == pBlock->iSizeBeforeZipping);
		free(pZippedBuffer);
		return pUnZippedBuffer;
	}
	else
	{
		return TextParserBinaryBlock_PutIntoMallocedBuffer_Internal(pBlock, piOutSize, caller_fname, line, true);
	}
}


TextParserBinaryBlock *TextParserBinaryBlock_CreateFromFile_dbg(char *pFileName, bool bZip, const char *caller_fname, int line)
{
	int iSize;
	char *pBuf = fileAlloc(pFileName, &iSize);
	TextParserBinaryBlock *pBlock;

	if (!pBuf)
	{
		return NULL;
	}

	pBlock = TextParserBinaryBlock_CreateFromMemory_dbg(pBuf, iSize, bZip, caller_fname, line);

	free(pBuf);

	return pBlock;
}

bool TextParserBinaryBlock_PutIntoFile(TextParserBinaryBlock *pBlock, char *pOutFileName)
{
	void *pBuf;
	int iSize;

	pBuf = TextParserBinaryBlock_PutIntoMallocedBuffer(pBlock, &iSize);

	if (iSize)
	{
		FILE *pOutFile = fopen(pOutFileName, "wb");

		if (!pOutFile)
		{
			free(pBuf);
			return false;
		}

		fwrite(pBuf, iSize, 1, pOutFile);

		fclose(pOutFile);
		free(pBuf);
		return true;
	}

	return false;
}

TextParserBinaryBlock *TextParserBinaryBlock_AssignMemory(TextParserBinaryBlock *pBlock, const U8 *pBuf, int iBufSize, bool bZip)
{
	int iWordCount;
	if (!iBufSize)
	{
		ea32Destroy(&pBlock->pInts);
		return pBlock;
	}

	assert(iBufSize <= TEXTPARSERBINARYBLOCK_MAXSIZE_BYTES);

	iWordCount = RoundUpToGranularity(iBufSize, sizeof(int)) / sizeof(int);

	ea32SetSize(&pBlock->pInts, iWordCount);
	memcpy(pBlock->pInts, pBuf, iBufSize);
	pBlock->iActualByteSize = iBufSize;

	return pBlock;
}





/*
AUTO_RUN_LATE;
void TPBB_Test(void)
{
	int iCount;
	int i;
	int iSize;
	char *pInBuf;
	char *pOutBuf;
	int iOutSize;

	TextParserBinaryBlock *pBlock;

	for (iCount = 1; iCount < 1000; iCount++)
	{
		char fileName[MAX_PATH];
		iSize = iCount * iCount;
	
		pInBuf = malloc(iSize);

		for (i=0; i < iSize; i++)
		{
			pInBuf[i] = (char)randomIntRange(-128, 127);
		}

		pBlock = TextParserBinaryBlock_CreateFromMemory(pInBuf, iSize, iCount % 2);

		sprintf(fileName, "c:\\temp\\TPBB_test_%d.txt", iCount);

		ParserWriteTextFile(fileName, parse_TextParserBinaryBlock, pBlock, 0, 0);

		StructDestroy(parse_TextParserBinaryBlock, pBlock);

		pBlock = StructCreate(parse_TextParserBinaryBlock);
		ParserReadTextFile(fileName, parse_TextParserBinaryBlock, pBlock, 0);


		pOutBuf = TextParserBinaryBlock_PutIntoMallocedBuffer(pBlock, &iOutSize);

		assert(pOutBuf && (iOutSize == iSize));

		for (i=0; i < iSize; i++)
		{
			assert(pInBuf[i] == pOutBuf[i]);
		}

		free(pInBuf);
		free(pOutBuf);
		StructDestroy(parse_TextParserBinaryBlock, pBlock);
	}
}
*/










////////////////////////////////////////////////////////



ParseTableGraph *MakeParseTableGraph(ParseTable *pRoot, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	ParseTableGraph *pOutGraph = calloc(sizeof(ParseTableGraph), 1);
	ParseTableGraphHandle **ppHandlesToProcess = NULL;
	ParseTableGraphHandle *pRootHandle = calloc(sizeof(ParseTableGraphHandle), 1);

	pOutGraph->handleTable = stashTableCreateAddress(16);

	pRootHandle->pTable = pRoot;
	stashAddPointer(pOutGraph->handleTable, pRoot, pRootHandle, false);

	eaPush(&ppHandlesToProcess, pRootHandle);

	while (eaSize(&ppHandlesToProcess))
	{
		ParseTableGraphHandle *pCurHandle = ppHandlesToProcess[0];
		ParseTable *pCurTable = pCurHandle->pTable;
		int i;
		ParseTableGraphHandle *pSubHandle;

		eaPush(&pOutGraph->ppHandles, pCurHandle);
		eaRemove(&ppHandlesToProcess, 0);

		FORALL_PARSETABLE(pCurTable, i)
		{
			if (!pCurHandle->bIsPolyTable)
			{
				if (pCurTable[i].type & (TOK_REDUNDANTNAME | TOK_IGNORE | TOK_UNOWNED)) 
				{
					continue;
				}

				if (!FlagsMatchAll(pCurTable[i].type,iOptionFlagsToMatch))
				{
					continue;
				}

				if (!FlagsMatchNone(pCurTable[i].type,iOptionFlagsToExclude))
				{
					continue;
				}

				if (!TOK_HAS_SUBTABLE(pCurTable[i].type))
				{
					continue;
				}
			}

			if (!pCurTable[i].subtable)
			{
				continue;
			}

			if (!stashFindPointer(pOutGraph->handleTable, pCurTable[i].subtable, &pSubHandle))
			{
				pSubHandle = calloc(sizeof(ParseTableGraphHandle), 1);
				pSubHandle->pTable = pCurTable[i].subtable;
				stashAddPointer(pOutGraph->handleTable, pCurTable[i].subtable, pSubHandle, false);
				eaPush(&ppHandlesToProcess, pSubHandle);
				pSubHandle->bIsPolyTable = (TOK_GET_TYPE(pCurTable[i].type) == TOK_POLYMORPH_X);
			}

			eaPushUnique(&pSubHandle->ppParents, pCurHandle);
			eaPushUnique(&pCurHandle->ppChildren, pSubHandle);
		}
	}

	return pOutGraph;
}

void DestroyParseTableGraph(ParseTableGraph *pGraph)
{
	int i;

	for (i=0; i < eaSize(&pGraph->ppHandles); i++)
	{
		ParseTableGraphHandle *pCurHandle = pGraph->ppHandles[i];

		eaDestroy(&pCurHandle->ppParents);
		eaDestroy(&pCurHandle->ppChildren);
		free(pCurHandle);
	}

	eaDestroy(&pGraph->ppHandles);
	stashTableDestroy(pGraph->handleTable);
	free(pGraph);
}


//////////////////////////////////////////////////////////////////////

static bool ParseTableHasSomeFieldsForOptimization_NoRecurse(ParseTable pti[], StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, 
	enumParseTableOptimizationFlags eOptionFlags)
{
	int i;

	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME 
			|| TOK_GET_TYPE(pti[i].type) == TOK_IGNORE
			|| TOK_GET_TYPE(pti[i].type) == TOK_START 
			|| TOK_GET_TYPE(pti[i].type) == TOK_END
			|| !FlagsMatchAll(pti[i].type,iOptionFlagsToMatch)
			|| !FlagsMatchNone(pti[i].type,iOptionFlagsToExclude)
			|| TOK_HAS_SUBTABLE(pti[i].type))
		{
			continue;
		}

		return true;
	}

	return false;
}

static void SetParseTableGraphUserDataRecurseParents(ParseTableGraphHandle *pHandle)
{
	int i;
	pHandle->iUserData = 1;

	for (i=0; i < eaSize(&pHandle->ppParents); i++)
	{
		if (!pHandle->ppParents[i]->iUserData)
		{
			SetParseTableGraphUserDataRecurseParents(pHandle->ppParents[i]);
		}
	}
}

ParseTable *ParserMakeFlatOptimizedParseTable(ParseTable pti[], StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, 
	enumParseTableOptimizationFlags eOptionFlags, bool bIsPolyTable, ParseTableGraph *pGraph)
{
	int i;
	ParseTable **ppColumns = NULL;

	ParseTable *pOut;

	FORALL_PARSETABLE(pti, i)
	{
		if (!bIsPolyTable)
		{
			if (pti[i].type & TOK_REDUNDANTNAME 
				|| TOK_GET_TYPE(pti[i].type) == TOK_IGNORE
				|| !FlagsMatchAll(pti[i].type,iOptionFlagsToMatch)
				|| !FlagsMatchNone(pti[i].type,iOptionFlagsToExclude))
			{
				continue;
			}

			if (eOptionFlags & PTOPT_FLAG_NO_START_AND_END)
			{
				if (TOK_GET_TYPE(pti[i].type) == TOK_START 
					|| TOK_GET_TYPE(pti[i].type) == TOK_END)
				{
					continue;
				}
			}
		}

		if (bIsPolyTable || TOK_HAS_SUBTABLE(pti[i].type))
		{
			ParseTableGraphHandle *pHandle;

			if (pti[i].subtable == NULL)
			{
				continue;
			}

			if (!stashFindPointer(pGraph->handleTable, pti[i].subtable, &pHandle))
			{
				assertmsg(0, "Something went wrong while making an optimized parse table");
			}

			if (pHandle->iUserData == 0)
			{
				continue;
			}
		}

		eaPush(&ppColumns, pti + i);
	}
	
	assertmsg(eaSize(&ppColumns), "Something went wrong while making an optimized parse table");

	pOut = calloc(sizeof(ParseTable) * (eaSize(&ppColumns) + 1), 1);

	for (i=0; i < eaSize(&ppColumns); i++)
	{
		memcpy(pOut + i, ppColumns[i], sizeof(ParseTable));
	}

	eaDestroy(&ppColumns);

	return pOut;
}

void ParserOptimized_FixupSubtables(ParseTable pti[], ParseTableGraph *pGraph)
{
	int i;

	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_HAS_SUBTABLE(pti[i].type))
		{
			ParseTableGraphHandle *pHandle;
			if (!stashFindPointer(pGraph->handleTable, pti[i].subtable, &pHandle))
			{
				assertmsg(0, "Something went wrong while making an optimized parse table");
			}

			pti[i].subtable = pHandle->pUserData;
		}
	}
}


ParseTable *ParserMakeOptimizedParseTable(ParseTable pti[], StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, 
	enumParseTableOptimizationFlags eOptionFlags)
{
	int i;
	ParseTable *pRetVal;

	//first, we construct the parse table graph
	ParseTableGraph *pGraph = MakeParseTableGraph(pti, iOptionFlagsToMatch, iOptionFlagsToExclude);

	assert(pGraph->ppHandles);

	//then we go through our graph and figure out which tables we actually need to make optimized tables for;
	for (i=0; i < eaSize(&pGraph->ppHandles); i++)
	{
		if (pGraph->ppHandles[i]->iUserData)
		{
			continue;
		}

		if (ParseTableHasSomeFieldsForOptimization_NoRecurse(pGraph->ppHandles[i]->pTable, iOptionFlagsToMatch, iOptionFlagsToExclude, eOptionFlags))
		{
			SetParseTableGraphUserDataRecurseParents(pGraph->ppHandles[i]);
		}
	}

	if (!pGraph->ppHandles[0]->iUserData)
	{
		DestroyParseTableGraph(pGraph);
		return NULL;
	}
	

	for (i=0; i < eaSize(&pGraph->ppHandles); i++)
	{
		if (pGraph->ppHandles[i]->iUserData)
		{
			pGraph->ppHandles[i]->pUserData = ParserMakeFlatOptimizedParseTable(pGraph->ppHandles[i]->pTable, iOptionFlagsToMatch, 
				iOptionFlagsToExclude, eOptionFlags, pGraph->ppHandles[i]->bIsPolyTable, pGraph);
		}
	}

	for (i=0; i < eaSize(&pGraph->ppHandles); i++)
	{
		if (pGraph->ppHandles[i]->pUserData)
		{
			ParserOptimized_FixupSubtables(pGraph->ppHandles[i]->pUserData, pGraph);
		}
	}

	pRetVal = pGraph->ppHandles[0]->pUserData;
	DestroyParseTableGraph(pGraph);

	return pRetVal;
	
}

// Recursively scans for all structs matching pTargetTable
static bool ParserScanForSubstructInternal(ParseTable pti[], void *pStruct, ParseTable *pRootTable, void *pRootStruct, ParseTable *pTargetTable, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *pcPathString, ParserScanForSubstructCB callback, void *pCBData)
{
	int i;
	bool result = false;

	if (pti == pTargetTable) {
		if (callback)
			result |= callback(pStruct, pRootStruct, pcPathString, pCBData);
		else
			return true;
	}

	FORALL_PARSETABLE(pti, i) {
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		if (pti[i].type & TOK_UNOWNED) continue;
		if (!FlagsMatchAll(pti[i].type,iOptionFlagsToMatch)) continue;
		if (!FlagsMatchNone(pti[i].type,iOptionFlagsToExclude)) continue;

		if (TOK_GET_TYPE(pti[i].type) == TOK_STRUCT_X || TOK_GET_TYPE(pti[i].type) == TOK_POLYMORPH_X) {
			if (pti[i].subtable) {
				ParseTable *pSubtable = (ParseTable*)pti[i].subtable;
				ParseTable *pPolyTable = (ParseTable*)pti[i].subtable;
				char path[1024];
				if (pti[i].type & TOK_EARRAY) {
					// Recurse into earray of structs
					void ***peaStructs = TokenStoreGetEArray_inline(pti, &pti[i], i, pStruct, NULL);
					int iKeyCol = ParserGetTableKeyColumn(pSubtable);
					int j;
					for(j=0; j<eaSize(peaStructs); ++j) {
						if (TOK_GET_TYPE(pti[i].type) == TOK_POLYMORPH_X) {
							if ((*peaStructs)[j])
								pSubtable = PolyStructDetermineParseTable(pPolyTable, (*peaStructs)[j]);
							else
								continue;
						}
						if (pRootStruct) {
							if (iKeyCol >= 0) {
								if (TOK_GET_TYPE(pSubtable[iKeyCol].type) == TOK_STRING_X ||
									TOK_GET_TYPE(pSubtable[iKeyCol].type) == TOK_CURRENTFILE_X)
									sprintf(path, "%s.%s[\"%s\"]", pcPathString, pti[i].name, TokenStoreGetString_inline(pSubtable, &pSubtable[iKeyCol], iKeyCol, (*peaStructs)[j], 0, NULL));
								else
									sprintf(path, "%s.%s[\"%d\"]", pcPathString, pti[i].name, TokenStoreGetInt_inline(pSubtable, &pSubtable[iKeyCol], iKeyCol, (*peaStructs)[j], 0, NULL));
							} else {
								sprintf(path, "%s.%s[%d]", pcPathString, pti[i].name, j);
							}
						} else {
							path[0] = '\0';
						}
						result |= ParserScanForSubstructInternal(pSubtable, (*peaStructs)[j], pRootTable, pRootStruct, pTargetTable, iOptionFlagsToMatch, iOptionFlagsToExclude, path, callback, pCBData);
					}
				} else if (pSubtable == pTargetTable) {
					// This is the structure we're looking for
					void *pSubstruct = TokenStoreGetPointer_inline(pti, &pti[i], i, pStruct, 0, NULL);
					if(pSubstruct)
					{
						sprintf(path, "%s.%s", pcPathString, pti[i].name);
						if (callback)
							result |= callback(pSubstruct, pRootStruct, path, pCBData);
						else
							return true;
					}
				} else {
					// Recurse into non-array struct
					void *pSubstruct = TokenStoreGetPointer_inline(pti, &pti[i], i, pStruct, 0, NULL);
					if (pSubstruct) {
						if (TOK_GET_TYPE(pti[i].type) == TOK_POLYMORPH_X) {
							pSubtable = PolyStructDetermineParseTable(pSubtable, pSubstruct);
						}
						if (pRootStruct) {
							sprintf(path, "%s.%s", pcPathString, pti[i].name);
						} else {
							path[0] = '\0';
						}
						result |= ParserScanForSubstructInternal(pSubtable, pSubstruct, pRootTable, pRootStruct, pTargetTable, iOptionFlagsToMatch, iOptionFlagsToExclude, path, callback, pCBData);
					}
				}
			}
		}
	}
	return result;
}

// Runs a callback for every substruct of type pTargetTable
// If a callback is provided, returns TRUE if at least one callback was executed and returned TRUE.
// Otherwise, returns TRUE if at least one matching substruct was found.
bool ParserScanForSubstruct(ParseTable pti[], void *pStruct, ParseTable *pTargetTable, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, ParserScanForSubstructCB callback, void *pCBData)
{
	if (pStruct && pTargetTable)
	{
		return ParserScanForSubstructInternal(pti, pStruct, pti, pStruct, pTargetTable, iOptionFlagsToMatch, iOptionFlagsToExclude, "", callback, pCBData);
	}
	return false;
}

// Walks a struct based on its ParseTable and calls a callback function on each column
// If the callback ever returns true, stop all scanning and return true.
static bool ParserTraverseParseTableInternal(ParseTable pti[], void *pStruct, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, ParserTraverseParseTableCB callback, ParserTraverseParseTableColumnCB preColumnCallback, ParserTraverseParseTableColumnCB postColumnCallback, void *pCBData)
{
	int i;

	PERFINFO_AUTO_START_FUNC();
	FORALL_PARSETABLE(pti, i) {
		int j;
		if (!FlagsMatchAll(pti[i].type,iOptionFlagsToMatch)) continue;
		if (!FlagsMatchNone(pti[i].type,iOptionFlagsToExclude)) continue;

		if(preColumnCallback && preColumnCallback(pti, pStruct, i, pCBData))
			return true;

		for(j = 0; j < TokenStoreGetNumElems_inline(pti, &pti[i], i, pStruct, NULL); ++j)
		{
			if(callback(pti, pStruct, i, j, pCBData))
				return true;

			if(TOK_HAS_SUBTABLE(pti[i].type) && pti[i].subtable)
			{
				ParseTable *pSubtable = (ParseTable*)pti[i].subtable;
				ParseTable *pPolyTable = (ParseTable*)pti[i].subtable;
				// Recurse into struct
				void *pSubstruct = TokenStoreGetPointer_inline(pti, &pti[i], i, pStruct, j, NULL);
				if (pSubstruct) 
				{
					if (TOK_GET_TYPE(pti[i].type) == TOK_POLYMORPH_X) 
					{
						pSubtable = PolyStructDetermineParseTable(pSubtable, pSubstruct);
					}
					if(ParserTraverseParseTableInternal(pSubtable, pSubstruct, iOptionFlagsToMatch, iOptionFlagsToExclude, callback, preColumnCallback, postColumnCallback, pCBData))
						return true;
				}
			}
		}

		if(postColumnCallback && postColumnCallback(pti, pStruct, i, pCBData))
			return true;
	}
	PERFINFO_AUTO_STOP();
	return false;
}

// returns true iff we called ParserTraverseParseTableInternal and it returned true.
// This means that a call to the callback returned true and we exited early.
// Can also take callback pointers to be before and after walking the contents of each column in the ParseTable
// The pre- and post-column callbacks ARE OPTIONAL
// Returns false immediately if you don't pass a main callback
bool ParserTraverseParseTable(ParseTable pti[], void *pStruct, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, ParserTraverseParseTableCB callback, ParserTraverseParseTableColumnCB preColumnCallback, ParserTraverseParseTableColumnCB postColumnCallback, void *pCBData)
{
	if(!callback)
	{
		return false;
	}

	if (pStruct && pti)
	{
		return ParserTraverseParseTableInternal(pti, pStruct, iOptionFlagsToMatch, iOptionFlagsToExclude, callback, preColumnCallback, postColumnCallback, pCBData);
	}

	return false;
}

char *spTagIndentString = NULL;

void LayoutFile_BeginBlock(FILE *pLayoutFile)
{
	estrConcatf(&spTagIndentString, "     ");
}

void LayoutFile_EndBlock(FILE *pLayoutFile)
{
	if (estrLength(&spTagIndentString))
	{
		estrRemove(&spTagIndentString, 0, 5);
	}
}

void TagLayoutFileOffset_internal(FILE *pLayoutFile, SimpleBufHandle outFile, int iOffset, char *fmt, ...)
{
char *pTemp = NULL;

	va_list ap;

	estrStackCreate(&pTemp);

	va_start(ap, fmt);
	if (fmt)
	{
		estrConcatfv(&pTemp, fmt, ap);

		fprintf(pLayoutFile, "<%6x>:%s%s", iOffset, spTagIndentString, pTemp);
		if (!strEndsWith(pTemp, "\n"))
		{
			fprintf(pLayoutFile, "\n");
		}
	}
	va_end(ap);

	estrDestroy(&pTemp);
}


void TagLayoutFile_internal(FILE *pLayoutFile, SimpleBufHandle outFile, char *fmt, ...)
{
	char *pTemp = NULL;

	va_list ap;

	estrStackCreate(&pTemp);

	va_start(ap, fmt);
	if (fmt)
	{
		estrConcatfv(&pTemp, fmt, ap);

		fprintf(pLayoutFile, "<%6x>:%s%s", SimpleBufTell(outFile), spTagIndentString, pTemp);
		if (!strEndsWith(pTemp, "\n"))
		{
			fprintf(pLayoutFile, "\n");
		}
	}
	va_end(ap);

	estrDestroy(&pTemp);
}

const char *ParserGetStructName(ParseTable table[], void *pStruct)
{
	static char outName[256] = "unknown";
	int key = ParserGetTableKeyColumn(table);
	if (key >= 0)
	{
		char *pTemp = NULL;
			
		estrStackCreate(&pTemp);
		FieldWriteText(table,key,pStruct,0,&pTemp,0);

		if (pTemp && pTemp[0])
		{
			strcpy_trunc(outName, pTemp);
		}
		estrDestroy(&pTemp);
	}

	return outName;
}



void ParserSetTableNotLegalForWriting(ParseTable table[], char *pErrorString)
{
	ParseTableInfo *pInfo = ParserGetTableInfo(table);
	pInfo->pNoStringWritingError = pErrorString;
}

//used during text writing to check the above flag and maybe generate an error
void CheckForIllegalTableWriting_internal(ParseTable parentTable[], const char *pNameInParent, ParseTable childTable[])
{
	ParseTableInfo *pInfo = ParserGetTableInfo(childTable);

	if (pInfo->pNoStringWritingError)
	{
		if (parentTable && pNameInParent)
		{
			Errorf("Illegal tpi \"%s\" (field \"%s\" in parent tpi \"%s\") for text writing. Error String \"%s\"", pInfo->name,
				pNameInParent, ParserGetTableName(parentTable), pInfo->pNoStringWritingError);
		}
		else
		{
			Errorf("Illegal tpi \"%s\" for text writing. Error string: \"%s\"", pInfo->name, pInfo->pNoStringWritingError);
		}
	}
}

void ParserSetReformattingCallback(ParseTable table[], TextParserReformattingCallback *pReformattingCB)
{
	ParseTableInfo *pTPIInfo;

	pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo)
	{
		pTPIInfo->pReformattingCB = pReformattingCB;
	}

}

TextParserReformattingCallback *ParserGetReformattingCallback(ParseTable *tpi)
{
	ParseTableInfo *pTPIInfo;

	if (!tpi)
	{
		return NULL;
	}
	
	pTPIInfo = ParserGetTableInfo(tpi);

	if (!pTPIInfo)
	{
		return NULL;
	}

	return pTPIInfo->pReformattingCB;
}

bool ParseTableContainsRefs(ParseTable pti[], char*** eaFieldNames)
{
	int i;
	bool retval = false;

	FORALL_PARSETABLE(pti, i) 
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		if (pti[i].type & TOK_UNOWNED) continue;

		if (TOK_HAS_SUBTABLE(pti[i].type))
		{
			//calling this function on parsetable that have polystructs in them is not reliable
			assert(TOK_GET_TYPE(pti[i].type) != TOK_POLYMORPH_X);

			if (pti[i].subtable) 
			{
				ParseTable *pSubtable = (ParseTable*)pti[i].subtable;
				if (ParseTableContainsRefs(pSubtable, eaFieldNames))
					retval = true;
			}
		}
		else
		{
			if (TOK_GET_TYPE(pti[i].type) == TOK_REFERENCE_X)
			{
				if (eaFieldNames)
				{
					char* debugName;
					estrCreate(&debugName);
					estrConcatf(&debugName, "%s::%s", pti[0].name, pti[i].name);
					eaPush(eaFieldNames, debugName);
				}
				retval = true;
			}
		}
	}

	return retval;
}

void ParserClearAndDestroyRefs(ParseTable pti[], void* pStruct)
{
	int i;

	//LDM: it's bad to duplicate this code from ParserTraverseParseTableInternal, but the callback needs to be inlined
	//and the only way that will happen through the compiler is if both it and ParserTraverseParseTableInternal are forceinline
	//which is impossible since ParserTraverseParseTableInternal is recursive.
	PERFINFO_AUTO_START_FUNC();
	FORALL_PARSETABLE(pti, i) 
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		if (pti[i].type & TOK_UNOWNED) continue;

		if (TOK_HAS_SUBTABLE(pti[i].type))
		{
			if (pti[i].subtable) 
			{
				ParseTable *pSubtable = (ParseTable*)pti[i].subtable;
				ParseTable *pPolyTable = (ParseTable*)pti[i].subtable;
				if (pti[i].type & TOK_EARRAY) 
				{
					// Recurse into earray of structs
					void ***peaStructs = TokenStoreGetEArray_inline(pti, &pti[i], i, pStruct, NULL);
					int iKeyCol = ParserGetTableKeyColumn(pSubtable);
					int j;
					for(j=0; j<eaSize(peaStructs); ++j) 
					{
						if (TOK_GET_TYPE(pti[i].type) == TOK_POLYMORPH_X) 
						{
							if ((*peaStructs)[j])
								pSubtable = PolyStructDetermineParseTable(pPolyTable, (*peaStructs)[j]);
							else
								continue;
						}
						ParserClearAndDestroyRefs(pSubtable, (*peaStructs)[j]);
					}
				} 
				else 
				{
					// Recurse into non-array struct
					void *pSubstruct = TokenStoreGetPointer_inline(pti, &pti[i], i, pStruct, 0, NULL);
					if (pSubstruct) 
					{
						if (TOK_GET_TYPE(pti[i].type) == TOK_POLYMORPH_X) 
						{
							pSubtable = PolyStructDetermineParseTable(pSubtable, pSubstruct);
						}
						ParserClearAndDestroyRefs(pSubtable, pSubstruct);
					}
				}
			}
		}
		else if (TOK_GET_TYPE(pti[i].type) == TOK_REFERENCE_X)
		{
			TokenStoreClearRef(pti, i, pStruct, 0, true, NULL); //we want to destroy the refs here so that they wont be re-cleared from the background thread
		}
	}
	PERFINFO_AUTO_STOP();
}



void ParserSetWantSpecialIgnoredFieldCallbacks(ParseTable pti[], const char *pFieldName)
{
	int i;
	const char *cachedName = allocFindString(pFieldName);
	if (!cachedName)
	{
		return; // Not in cache, can't exist
	}

	FORALL_PARSETABLE(pti, i)
	{
		if ((TOK_GET_TYPE(pti[i].type) == TOK_IGNORE) && pti[i].name == cachedName)
		{
			pti[i].type |= TOK_WANT_FIXUP_WITH_IGNORED_FIELD;
		}
	}
}


int ParserGetDefaultFieldColumn(ParseTable tpi[])
{
	ParseTableInfo *pInfo = ParserGetTableInfo(tpi);
	int i;

	if (!pInfo->iDefaultFieldColumn)
	{
		FORALL_PARSETABLE(tpi, i)
		{
			if (GetBoolFromTPIFormatString(&tpi[i], "DEFAULT_FIELD"))
			{
				pInfo->iDefaultFieldColumn = i;
				break;
			}
		}

		if (!pInfo->iDefaultFieldColumn)
		{
			pInfo->iDefaultFieldColumn = -1;
		}
	}

	return pInfo->iDefaultFieldColumn;
}

//returns true if this is a temporary table, ie one created for server monitoring or as a hybrid object or somethign
bool ParserTableIsTemporary(ParseTable *tpi)
{
	ParseTableInfo *pInfo = NULL;

	if (!tpi)
		return false;

	pInfo = ParserGetTableInfo(tpi);

	if (!pInfo)
		return false;

	return pInfo->bTPIIsTemporary;
}


//gets the key of a struct as a string, appends it onto the end of an estring. Will usually run in "fast" mode,
//unless your key is something oddball like a struct or a formatted string
bool ParserGetKeyStringDbg(ParseTable table[], void *pStruct, char **ppOutEString, const char *caller_fname, int iLine)
{
	return ParserGetKeyStringDbg_inline(table, pStruct, ppOutEString, false, caller_fname, iLine);
}


const char *DEFAULT_LATELINK_ParserGetTableName_WithEntityFixup(ParseTable *table, void *pObject)
{
	return ParserGetTableName(table);
}

char **ParserGetOriginalCaseFieldNames(ParseTable tpi[])
{
	ParseTableInfo *pInfo = ParserGetTableInfo(tpi);

	if (pInfo && pInfo->ppOriginalCaseFieldNames)
	{
		return pInfo->ppOriginalCaseFieldNames;
	}

	return NULL;
}


#include "autogen/TextParserUtils_h_ast.c"
