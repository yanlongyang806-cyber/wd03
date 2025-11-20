#include "structInternals.h"
#include "url.h"
#include "tokenstore.h"
#include "autogen/textparser_h_ast.h"
#include "autogen/textparserhtml_c_ast.h"
#include "objpath.h"
#include "expression.h"
#include "sock.h"
#include "cmdparse.h"

#include "crypt.h"
#include "structinternals_h_ast.h"
#include "textParserJSON.h"
#include "file.h"
#include "estring.h"
#include "message.h"

// Turning this on makes ParserReadJSON() fail if a member in a JSON object isn't represented in the TPI
// #define JSON_STRICT_PARSER

static __forceinline char MakeCharLowercase(char c)
{
	if (c >= 'A' && c <= 'Z')
	{
		c -= 'A' - 'a';
	}

	return c;
}

static __forceinline void MakeStringLowercase(char *pString)
{
	if (pString)
	{
		while (*pString)
		{
			*pString = MakeCharLowercase(*pString);
			
			pString++;
		}
	}
}

void WriteJsonIntFlags(FILE *out, int iValue, ParseTable *pSubTable)
{
	int i;
	U32 mask = 1;
	int first = 1;

	fwrite("[ ", 2, 1, out);

	for (i = 0; i < 32; i++)
	{
		if (iValue & 1)
		{
			fprintf(out, "%s\"%s\"", first ? "" : ", ", StaticDefine_FastIntToString((StaticDefine*)pSubTable, mask));
			first = 0;
		}
		mask <<= 1;
		iValue >>= 1;
	}

	fwrite(" ]", 2, 1, out);
}

bool bit_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	U32 value = TokenStoreGetBit(tpi, column, structptr, index, NULL);
	const char *rev;
	
	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		WriteJsonIntFlags(out, value, tpi[column].subtable);
		return true;
	}

	if(tpi[column].subtable && (rev = StaticDefineIntRevLookup(tpi[column].subtable, value)))
	{
		fprintf(out, "\"%s\"", rev);
	}
	else
	{
		fprintf(out, "%d", value);
	}
	
	return true;
}


bool u8_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *rev;
	TextParserResult ok = PARSERESULT_SUCCESS;
	int value = TokenStoreGetU8(tpi, column, structptr, index, &ok);

	if(!RESULT_GOOD(ok))
		return false;

	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		WriteJsonIntFlags(out, value, tpi[column].subtable);
		return true;
	}

	if(tpi[column].subtable && (rev = StaticDefineIntRevLookup(tpi[column].subtable, value)))
	{
		fprintf(out, "\"%s\"", rev);
	}
	else
	{
		fprintf(out, "%d", value);
	}
	
	return true;
}

bool int16_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *rev;
	TextParserResult ok = PARSERESULT_SUCCESS;
	int value = TokenStoreGetInt16(tpi, column, structptr, index, &ok);

	if(!RESULT_GOOD(ok))
		return false;

	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_FLAGS)
	{
		WriteJsonIntFlags(out, value, tpi[column].subtable);
		return true;
	}

	if(tpi[column].subtable && (rev = StaticDefineIntRevLookup(tpi[column].subtable, value)))
	{
		fprintf(out, "\"%s\"", rev);
	}
	else
	{
		fprintf(out, "%d", value);
	}
	
	return true;
}

bool int_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *rev;
	TextParserResult ok = PARSERESULT_SUCCESS;
	int value = TokenStoreGetInt(tpi, column, structptr, index, &ok);

	int format = TOK_GET_FORMAT_OPTIONS(tpi[column].format);

	if(!RESULT_GOOD(ok))
		return false;

	if (format == TOK_FORMAT_FLAGS)
	{
		WriteJsonIntFlags(out, value, tpi[column].subtable);
		return true;
	}

	if (format == TOK_FORMAT_IP)
	{
		fprintf(out, "\"%s\"", makeIpStr(value));
		return true;
	}

	if (GetBoolFromTPIFormatString(&tpi[column], "JSON_SECS_TO_RFC822"))
	{
		rev = timeGetRFC822StringFromSecondsSince2000(value);
		fprintf(out, "\"%s\"", rev);
	}
	else if(tpi[column].subtable && (rev = StaticDefineIntRevLookup(tpi[column].subtable, value)))
	{
		fprintf(out, "\"%s\"", rev);
	}
	else
	{
		fprintf(out, "%d", value);
	}

	return true;
}


bool array_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	bool result = true;
	int numelems = TokenStoreGetNumElems(tpi, column, structptr, NULL);
	int i;

	fprintf(out, "[");
	for (i = 0; i < numelems; i++)
	{
		if(i) fprintf(out, ",");
		nonarray_writejsonfile(tpi, column, structptr, i, out, eFlags,
			iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	fprintf(out, "]");

	return result;
}

bool reference_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *buf;
	buf = TokenStoreGetRefString(tpi, column, structptr, index, NULL);

	if (buf)
	{
		fprintf(out, "\"@%s[%s]\"",  (char*)(tpi[column].subtable), buf);
	}
	else
	{
		fprintf(out, "\"@%s[]\"",  (char*)(tpi[column].subtable));
	}

	return true;
}

__forceinline static void *InfoToPointer(MultiInfo* info, U32 cnt)
{
	U8* ptr = (U8*) info->ptr;
	return &ptr[info->width * cnt];
}

bool MultiVal_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE *out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
#define INFO_TO_REF(type, cnt)  *((type *) InfoToPointer(&info, cnt))

	MultiInfo info;
	MultiVal*   pmv = TokenStoreGetMultiVal(tpi, column, structptr, index, NULL);
	U32 i;

	MultiValInfo(pmv, &info);

	//Consume Size from stream if this is an EArray
	if (info.atype == MMA_EARRAY) 
	{
		fprintf(out, "[");
	}

	for (i = 0; i < info.count; i++)
	{
		if(i>0) fprintf(out, ",");
		switch(info.dtype)
		{
			case MMT_INT32:		fprintf(out, "%d", INFO_TO_REF(U32, i)); break;
			case MMT_INT64:		fprintf(out, "%"FORM_LL"d", INFO_TO_REF(U64, i)); break;
			case MMT_FLOAT32:	fprintf(out, "%g", INFO_TO_REF(F32, i)); break;
			case MMT_FLOAT64:	fprintf(out, "%g", (float)INFO_TO_REF(F64, i)); break;
			case MMT_STRING:	fprintf(out, "\"%s\"", (char*)pmv->str); break;

			default:
				assertmsg(0, "This type of MultiVal is unsupported.");
				break;
		}
	}

	if (info.atype == MMA_EARRAY) 
	{
		fprintf(out, "]");
	}

	return true;
}

bool JsonMaybeDoSpecialStructWriting(char **ppOutString, ParseTable tpi[], int column, void* structptr, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	if (tpi[column].subtable == parse_DisplayMessage)
	{
		U32 storage = TokenStoreGetStorageType(tpi[column].type);
		DisplayMessage *pMessage;
		char *pTempName = NULL;
		estrCopy2(&pTempName, tpi[column].name);
		MakeStringLowercase(pTempName);

		if(storage == TOK_STORAGE_INDIRECT_EARRAY)
			return false;

		if(storage != TOK_STORAGE_INDIRECT_SINGLE && storage != TOK_STORAGE_DIRECT_SINGLE)
		{
			devassertmsgf(storage == TOK_STORAGE_INDIRECT_SINGLE || storage == TOK_STORAGE_DIRECT_SINGLE, "While trying to json write %s, came upon some DisplayMessages that were not optional or embedded structs... currently unsupported",
				ParserGetTableName(tpi));
			return false;
		}

		pMessage = TokenStoreGetPointer(tpi, column, structptr, 0, NULL);

		if (!pMessage)
		{
			if (! (eFlags & WRITEJSON_DONT_WRITE_EMPTY_OR_DEFAULT_FIELDS))
			{
				estrPrintf(ppOutString, "\"%s\": \"@Message[]\"", pTempName);
			}
		}
		else
		{
			const char *pRefString = REF_STRING_FROM_HANDLE(pMessage->hMessage);
			if (!pRefString)
			{
				if (! (eFlags & WRITEJSON_DONT_WRITE_EMPTY_OR_DEFAULT_FIELDS))
				{
					estrPrintf(ppOutString, "\"%s\": \"@Message[]\"", pTempName);
				}
			}
			else
			{
				estrPrintf(ppOutString, "\"%s\": \"@Message[%s]\"", pTempName, pRefString);
			}

		}

		estrDestroy(&pTempName);
		return true;
	}

	return false;
}

bool InnerWriteJSON(ParseTable tpi[], int column, void* structptr, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i;
	bool sawOne = false;
	char *tempName = NULL;
	S64 iTell;

	char **ppOriginalCaseNames = ParserGetOriginalCaseFieldNames(tpi);

	estrStackCreate(&tempName);

	fprintf(out, "{");
	FORALL_PARSETABLE(tpi, i)
	{
		char *pTestStringForSpecialStructs = NULL;
		U32 type = TOK_GET_TYPE(tpi[i].type);
		if (column >= 0 && i != column) continue;
		if (!tpi[i].name || !tpi[i].name[0]) continue; // unnamed fields shouldn't be parsed or written
		if (tpi[i].type & TOK_REDUNDANTNAME) continue;
		if (type == TOK_START) continue;
		if (type == TOK_END) continue;
		if (type == TOK_IGNORE) continue; // also ignoring column 0. this is on purpose.
		if (type == TOK_COMMAND) continue;
		if (tpi[i].type & TOK_FLATEMBED) continue;
		if (!FlagsMatchAll(tpi[i].type,iOptionFlagsToMatch)) continue;
		if (!FlagsMatchNone(tpi[i].type,iOptionFlagsToExclude)) continue;

		//certain child structs have special case struct writing code all their own
		if (tpi[i].subtable)
		{
			if (JsonMaybeDoSpecialStructWriting(&pTestStringForSpecialStructs, 
				tpi, i, structptr, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude))
			{
				if (estrLength(&pTestStringForSpecialStructs))
				{

					if(sawOne)
					{
						if (eFlags & WRITEJSON_DONT_WRITE_NEWLINES)
						{
							fprintf(out, ",");
						}
						else
						{
							fprintf(out, ",\n");
						}
					}
					else
					{
						sawOne = true;
					}
					fprintf(out, "%s", pTestStringForSpecialStructs);

					estrDestroy(&pTestStringForSpecialStructs);
				}

				continue;
			}
		}



		if (eFlags & WRITEJSON_DONT_WRITE_EMPTY_OR_DEFAULT_FIELDS)
		{
			char *pScratchString = NULL;
			FILE *pScratchFile;
			int iAmountWritten;

			estrStackCreate(&pScratchString);
			pScratchFile = fileOpenEString(&pScratchString);



			writetext_autogen(pScratchFile, tpi, i, structptr, 0, 1, 0, 0, 0, 0, 0);

			iAmountWritten = ftell(pScratchFile);

			fclose(pScratchFile);
			estrDestroy(&pScratchString);

			if (!iAmountWritten)
			{
				continue;
			}
		}

		if(sawOne)
		{
			if (eFlags & WRITEJSON_DONT_WRITE_NEWLINES)
			{
				fprintf(out, ",");
			}
			else
			{
				fprintf(out, ",\n");
			}
		}
		else
		{
			sawOne = true;
		}

		if (ppOriginalCaseNames)
		{
			fprintf(out, "\"%s\":", ppOriginalCaseNames[i]);
		}
		else
		{
			estrCopy2(&tempName, tpi[i].name);
			MakeStringLowercase(tempName);

			fprintf(out, "\"%s\":", tempName);
		}


		iTell = ftell(out);
		writejsonfile_autogen(tpi, i, structptr, -1, out,  eFlags, iOptionFlagsToMatch,  iOptionFlagsToExclude);
		if (iTell == ftell(out))
		{
			fwrite("null", 4, 1, out);
		}
	}
	fprintf(out, "}");

	estrDestroy(&tempName);
	return true;
}

bool string_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char* str = TokenStoreGetString(tpi, column, structptr, index, NULL);
	if (str)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		estrCopyWithJSONEscaping(&estr, str);
		WriteString(out, "\"", 0, 0);
		WriteString(out, estr, 0, 0);
		WriteString(out, "\"", 0, 0);
		estrDestroy(&estr);
	}
	else
	{
		WriteString(out, "\"\"", 0, 0);
	}

	return true;
}

bool struct_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	void* substruct = TokenStoreGetPointer(tpi, column, structptr, index, NULL);
	bool result = false;
	if (substruct)
	{
		return InnerWriteJSON(tpi[column].subtable, -1, substruct, out, eFlags,
			iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	return false;
}

bool poly_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i, numelems = TokenStoreGetNumElems_inline(tpi, &tpi[column], column, structptr, NULL);

	if (!numelems)
	{
		return false;
	}

	for (i = 0; i < numelems; i++)
	{
		ParseTable* polytable = tpi[column].subtable;
		int polycol = -1;
		void* substruct = TokenStoreGetPointer(tpi, column, structptr, i, NULL);

		if (!substruct)
		{
			//this is only legal if it's an optional struct
			U32 storage = TokenStoreGetStorageType(tpi[column].type);
		
			if (storage == TOK_STORAGE_INDIRECT_SINGLE)
			{
				return false;
			}
		}

		if (substruct && StructDeterminePolyType(polytable, substruct, &polycol))
		{
			fprintf(out, "{ \"%s\": ", polytable[polycol].name);
			InnerWriteJSON(polytable[polycol].subtable, -1, substruct, out, eFlags,
				iOptionFlagsToMatch, iOptionFlagsToExclude);
			fprintf(out, "}");
		}
		else
			devassertmsgf(0, "%s couldn't determine poly type!", __FUNCTION__);
	}

	return true;
}



bool ParseWriteJSONFile(FILE *out, ParseTable *tpi, void *struct_mem, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserAutoFixupCB *pFixupCB = ParserGetTableFixupFunc(tpi);
	int ok = 1;
	if (pFixupCB)
	{
		ok &= (pFixupCB(struct_mem, FIXUPTYPE_PRE_TEXT_WRITE, NULL) == PARSERESULT_SUCCESS);
	}

	return ok && InnerWriteJSON(tpi, -1, struct_mem, out, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
}

void ParserWriteJSON(char **estr, ParseTable *tpi, void *struct_mem, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	FILE *fpBuff;

	if (!estr)
		return;

	fpBuff = fileOpenEString(estr);
	ParseWriteJSONFile(fpBuff, tpi, struct_mem, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	fileClose(fpBuff);
}

void ParserWriteJSONEx(char **estr, ParseTable *tpi, int column, void *structptr, int index, WriteJsonFlags eFlags)
{
	FILE *fpBuff;
	U32 storageType = TokenStoreGetStorageType(tpi[column].type);

	if (!estr)
		return;

	fpBuff = fileOpenEString(estr);

	if(index == -1 && TokenStoreStorageTypeIsAnArray(storageType))
	{
		writejsonfile_autogen(tpi, column, structptr, -1, fpBuff,  eFlags, 0,  0);
	}
	else
	{
		nonarray_writejsonfile(tpi, column, structptr, index, fpBuff, eFlags, 0, 0);
	}

	fileClose(fpBuff);
}


#include "cJSON.h"

void *json_malloc(size_t size)
{
	return malloc(size);
}

void json_free(void *p)
{
	free(p);
}

AUTO_RUN;
void InitJSONHooks(void)
{
	static cJSON_Hooks hooks = {
		json_malloc,
		json_free
	};
	cJSON_InitHooks(&hooks);
};

static bool json_convert_array(cJSON *a, ParseTable tpi[], int col, void *ptr, char *fieldname, char **resultString);

static bool json_convert_nonarray(cJSON *v, ParseTable tpi[], int col, void *ptr, int index, char *fieldname, char **resultString)
{
	if (TOK_HAS_SUBTABLE(tpi[col].type))
	{ 
		//structs
		if (v->type == cJSON_Object)
		{
			void *subptr = json_convert_struct(v, tpi[col].subtable, resultString);
			if (subptr)
			{
				TokenStoreSetPointer(tpi, col, ptr, index, subptr, NULL);
				return true;
			}
		}
		else if(v->type == cJSON_NULL)
		{
			TokenStoreSetPointer(tpi, col, ptr, index, NULL, NULL);
			return true;
		}
		if (resultString) estrPrintf(resultString, "Could not convert struct value for field: %s", fieldname);
		return false;
	}

	if (v->type == cJSON_String && tpi[col].subtable && TYPE_INFO(tpi[col].type).interpretfield(tpi, col, SubtableField) == StaticDefineList)
	{ //StaticDefines
		const char *reinterpret = StaticDefineLookup(tpi[col].subtable, v->valuestring);
		if (reinterpret)
		{
			free(v->valuestring);
			v->valuestring = strdup(reinterpret);
		}
	}

	//primitives
	switch (TOK_GET_TYPE(tpi[col].type))
	{
	case TOK_U8_X:
		{
			if (v->type == cJSON_Number)
			{
				if (v->valueint > U8_MAX || v->valueint < 0)
				{
					if (resultString) estrPrintf(resultString, "Could not convert value (%"FORM_LL"d) to U8 for field: %s", v->valueint, fieldname);
					return false;
				}
				TokenStoreSetU8(tpi, col, ptr, index, v->valueint, NULL, NULL);
				return true;
			}
			else if (v->type == cJSON_True)
			{
				TokenStoreSetU8(tpi, col, ptr, index, 1, NULL, NULL);
				return true;
			}
			else if (v->type == cJSON_False)
			{
				TokenStoreSetU8(tpi, col, ptr, index, 0, NULL, NULL);
				return true;
			}
			else if (v->type == cJSON_String)
			{
				U8 strint;
				char *end = NULL;
				strint = (U8)strtol(v->valuestring, &end, 0);
				if (end != v->valuestring)
				{
					TokenStoreSetU8(tpi, col, ptr, index, strint, NULL, NULL);
					return true;
				}
			}
		} break;
	case TOK_INT16_X:		// 16 bit integer
		{
			if (v->type == cJSON_Number)
			{
				if (v->valueint > SHRT_MAX || v->valueint < SHRT_MIN)
				{
					if (resultString) estrPrintf(resultString, "Could not convert value (%"FORM_LL"d) to Int16 for field: %s", v->valueint, fieldname);
					return false;
				}					
				TokenStoreSetInt16(tpi, col, ptr, index, v->valueint, NULL, NULL);
				return true;
			}
			else if (v->type == cJSON_True)
			{
				TokenStoreSetInt16(tpi, col, ptr, index, 1, NULL, NULL);
				return true;
			}
			else if (v->type == cJSON_False)
			{
				TokenStoreSetInt16(tpi, col, ptr, index, 0, NULL, NULL);
				return true;
			}
			else if (v->type == cJSON_String)
			{
				S16 strint;
				char *end = NULL;
				strint = strtol(v->valuestring, &end, 0);
				if (end != v->valuestring)
				{
					TokenStoreSetInt(tpi, col, ptr, index, strint, NULL, NULL);
					return true;
				}
			}
		} break;
	case TOK_INT64_X:
	case TOK_INT_X:
		{
			if (v->type == cJSON_Number)
			{
				TokenStoreSetIntAuto(tpi, col, ptr, index, v->valueint, NULL, NULL);
				return true;
			}
			else if (v->type == cJSON_True)
			{
				TokenStoreSetInt(tpi, col, ptr, index, 1, NULL, NULL);
				return true;
			}
			else if (v->type == cJSON_False)
			{
				TokenStoreSetInt(tpi, col, ptr, index, 0, NULL, NULL);
				return true;
			}
			else if (v->type == cJSON_String)
			{
				int strint;
				char *end = NULL;
				strint = strtol(v->valuestring, &end, 0);
				if (end != v->valuestring)
				{
					TokenStoreSetInt(tpi, col, ptr, index, strint, NULL, NULL);
					return true;
				}
			}
		} break;
	case TOK_F32_X:
		{
			if (v->type == cJSON_Number)
			{
				TokenStoreSetF32(tpi, col, ptr, index, v->valuedouble, NULL, NULL);
				return true;
			}
		} break;
	case TOK_STRING_X:
		{
			if (v->type == cJSON_String)
			{
				TokenStoreSetString(tpi, col, ptr, index, v->valuestring, NULL, NULL, NULL, NULL);
				return true;
			}
			else
			if (v->type == cJSON_NULL)
			{
				// Leave the pointer NULL
				return true;
			}
		} break;
	}

	if (resultString) estrPrintf(resultString, "Could not convert struct value for field: %s", fieldname);
	return false;
}

static bool json_convert_array(cJSON *a, ParseTable tpi[], int col, void *ptr, char *fieldname, char **resultString)
{
	bool success = true;
	if (TokenStoreStorageTypeIsFixedArray(TOK_GET_TYPE(tpi[col].type)))
	{
		if (resultString) estrPrintf(resultString, "Cannot pass fixed array fields as parameters: %s", fieldname);
		return false;
	}
	if(a->type != cJSON_Array)
	{
		if (resultString) estrPrintf(resultString, "Expecting array: %s", fieldname);
		return false;
	}
	if (TOK_HAS_SUBTABLE(tpi[col].type))
	{ //structs
		int i = 0;
		cJSON *child = a->child;
		while(child)
		{
			void *subptr = NULL;
			if (child->type != cJSON_Object)
			{
				if (resultString) estrPrintf(resultString, "Cannot convert struct parameter in array: %s", fieldname);
				success = false;
				break;
			}
			subptr = json_convert_struct(child, tpi[col].subtable, resultString);
			if (!subptr)
			{
				success = false;
				break;
			}
			TokenStoreSetPointer(tpi, col, ptr, i, subptr, NULL);
			child = child->next;
			i++;
		}
	}
	else
	{
		int i = 0;
		cJSON *child = a->child;
		while(child)
		{
			success = json_convert_nonarray(child, tpi, col, ptr, i, fieldname, resultString);
			if(!success)
				break;

			child = child->next;
			i++;
		}
	}
	return success;
}

void* json_convert_struct(cJSON *json, ParseTable tpi[], char **resultString)
{
	void *ptr = StructCreateVoid(tpi);
	bool success = true;

	if(json->type == cJSON_Object)
	{
		cJSON *child = json->child;
		while(child)
		{
			int col;

			if (!child->string)
			{
				if (resultString) estrPrintf(resultString, "Invalid or incomplete struct member.");
				success = false;
				break;
			}
			if (!ParserFindColumn(tpi, child->string, &col))
			{
#ifdef JSON_STRICT_PARSER
				if (resultString) estrPrintf(resultString, "Could not find struct parameter field: %s", child->string);
				success = false;
				break;
#else
				child = child->next;
				continue;
#endif
			}
			if (TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(tpi[col].type)))
			{
				if (child->type != cJSON_Array)
				{
					if (resultString) estrPrintf(resultString, "Expected an array for field: %s", child->string);
					success = false;
					break;
				}
				success = json_convert_array(child, tpi, col, ptr, child->string, resultString);
			}
			else
			{	//non-arrays
				success = json_convert_nonarray(child, tpi, col, ptr, -1, child->string, resultString);
			}
			if (!success)
			{
				if (resultString && !**resultString) estrPrintf(resultString, "Could not convert value for field: %s", child->string);
				break;
			}

			child = child->next;
		}
	}
	else
	{
		estrPrintf(resultString, "not a struct");
		success = false;
	}

	if (!success)
	{
		StructDestroyVoid(tpi, ptr);
		ptr = NULL;
	}

	return ptr;
}

void * ParserReadJSON(const char *json_string, ParseTable *tpi, char **estrResult)
{
	void * ret = NULL;
	cJSON *json = cJSON_Parse(json_string);
	if(!json)
	{
		estrPrintf(estrResult, "Invalid JSON");
		return NULL;
	}

	ret = json_convert_struct(json, tpi, estrResult);

	cJSON_Delete(json);
	return ret;
}
