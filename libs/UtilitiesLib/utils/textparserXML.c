
#include "structInternals.h"

#include "estring.h"
#include "tokenstore.h"
#include "Message.h"

#include "timing.h"
#include "error.h"
#include "textParserXML.h"
#include "structInternals_h_ast.h"

#define TPXML_WRITE_ENCLOSING_ELEMENT (1 << 8)
#define TPXML_WRITE_MEMBER_WRAPPERS (1 << 9)
#define TPXML_WRITE_CLOSING_NEWLINE (1 << 10)
#define TPXML_DECODE_RPC_STRUCT (1 << 11)
#define TPXML_NO_CASE_STRIPPING (1 << 12)
#define TPXML_ENCODE_BASE64 (1 << 13)

typedef struct KVPair {
	char * key;
	char * value;
} KVPair;

//Creates an xml open tag. No santitization, just adds < and >.
void XMLOpenElementNoAttrEx(const char *name, int level, StructFormatField iOptions, FILE* out)
{
	char *estr = NULL;
	int len = (int)strlen(name);
	int i;

	estrStackCreate(&estr);
	
	estrPrintf(&estr, "<%s>", name);
	if (!(iOptions & TPXML_NO_CASE_STRIPPING))
	for (i = 1; i < (1+len); i++) estr[i] = tolower(estr[i]);

	WriteString(out, estr, (iOptions & TPXML_NO_PRETTY) ? 0 : level, iOptions & TPXML_WRITE_CLOSING_NEWLINE);

	estrDestroy(&estr);
}

//Creates an xml close tag. No santitization, just adds </ and >.
void XMLCloseElementEx(const char *name, int level, StructFormatField iOptions, FILE* out)
{
	char *estr = NULL;
	int len = (int)strlen(name);
	int i;

	estrStackCreate(&estr);

	estrPrintf(&estr, "</%s>", name);
	if (!(iOptions & TPXML_NO_CASE_STRIPPING))
		for (i = 2; i < (2+len); i++) estr[i] = tolower(estr[i]);

	WriteString(out, estr, (iOptions & TPXML_NO_PRETTY) ? 0 : level, iOptions & TPXML_WRITE_CLOSING_NEWLINE);

	estrDestroy(&estr);
}

void XMLSingleElementWithAttributes(const char *name, KVPair **ppKVs , int level, StructFormatField iOptions, FILE* out)
{
	char *buf = NULL;
	char *tmp = NULL;
	KVPair *kv;
	int i;

	estrStackCreate(&buf);
	estrStackCreate(&tmp);

	estrPrintf(&tmp, "<%s", name);
	estrAppend(&buf, &tmp);

	for(i = 0; i < eaSize(&ppKVs); i++)
	{
		kv = (KVPair*)eaGet(&ppKVs, i);
		estrPrintf(&tmp, " %s", kv->key);
		estrAppend(&buf, &tmp);
		if (kv->value != NULL)
		{
			estrPrintf(&tmp, "=\"%s\"", kv->value);
			estrAppend(&buf, &tmp);
		}
	}
	//pad the key value pairs if we wrote them.
	if (i > 0)
	{
		estrPrintf(&tmp, " ");
		estrAppend(&buf, &tmp);
	}

	estrPrintf(&tmp, "/>");
	estrAppend(&buf, &tmp);

	WriteString(out, buf, (iOptions & TPXML_NO_PRETTY) ? 0 : level, 1);

	estrDestroy(&buf);
	estrDestroy(&tmp);
}

// Wraps an estring in "<![CDATA["..."]]>" unless the incoming string is empty.
// *** This will truncate the incoming string at first 
//     occurrence of "]]>" to conform to the CDATA section ***
void XMLWrapInCdata(char **estr)
{
	char *tmpstr = NULL;
	estrStackCreate(&tmpstr);
	estrPrintf(&tmpstr, "");

	//create it if necessary.
	estrAppend(estr, &tmpstr);

	//move to tmpstr so we can kill the ]]> tokens.
	estrPrintf(&tmpstr, "%s", *estr);

	//If you've got "]]>" in your xml string you've got other issues you need to address.
	estrReplaceOccurrences(&tmpstr, "]]>", "\003");
	estrTruncateAtFirstOccurrence(&tmpstr, '\003');
	
	if ( estrLength(&tmpstr) )
	{
		estrPrintf(estr, "<![CDATA[%s]]>", tmpstr);
	}
	estrDestroy(&tmpstr);
}

//Iterates through the parsetable and calls the appropriate writexmlfile function for each token.
bool InnerWriteXML(FILE *out, ParseTable tpi[], int column, void *struct_mem, int index, int level, StructFormatField iOptions )
{
	int i;

	//For ignored tokens, keep them around incase we want to use them for enclosing tags.
	ParseTable *pCurrentElement;
	//In this case I'm using a stack just because I'm not familiar with the nature of the token array.
	ParseTable **ppElementStack = NULL;
	int unwrapcol = -1;
	int decodecol = -1;

	//if we're xmlrpc decoding an array
	bool decodeThisStruct = false;
	int keycolumn = -1;
	int valcolumn = -1;
	
	if (iOptions & TPXML_DECODE_RPC_STRUCT)
	{
		iOptions = iOptions & ~TPXML_DECODE_RPC_STRUCT;
		if (column < 0)
			decodeThisStruct = true;
	}

	if (iOptions & TPXML_FORMAT_XMLRPC && column < 0) 
	{
		//magic array unwrapping
		FORALL_PARSETABLE(tpi, i)
		{
			if (TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(tpi[i].type)))
			{
				int decodekey = 0;
				GetIntFromTPIFormatString(&tpi[i], "XML_DECODE_KEY", &decodekey);
				if (decodecol < 0 && decodekey == 1 && TOK_GET_TYPE(tpi[i].type) == TOK_STRUCT_X) 
				{
					decodecol = i;
					break;
				}
				else if (unwrapcol < 0 && GetBoolFromTPIFormatString(&tpi[i], "XML_UNWRAP_ARRAY")) 
				{
					unwrapcol = i;
					break;
				}
			}
		}
		if (decodeThisStruct)
		{
			//must have a key column to decode
			if ((keycolumn = ParserGetTableKeyColumn(tpi)) < 0)
			{
				Errorf("Cannot decode struct lacking an AST(KEY) column.");
				return false;
			}
			
			//can't use non-primitives as a key
			if (TOK_GET_TYPE(tpi[keycolumn].type) == TOK_STRUCT_X)
			{
				Errorf("Cannot decode struct using a non-primitive as an AST(KEY) column.");
				return false;
			}
			//find the first structparam column.
			FORALL_PARSETABLE(tpi, i)
			{
				U32 type = TOK_GET_TYPE(tpi[i].type);
				if (type == TOK_START) continue;
				if (type == TOK_IGNORE && i) continue;
				if (tpi[i].type & TOK_FLATEMBED) continue;

				if (tpi[i].type & TOK_STRUCTPARAM)
				{
					valcolumn = i;
					break;
				}
			}

			//substruct must have a struct param column
			if (valcolumn < 0)
			{
				Errorf("Cannot decode a struct lacking an AST(STRUCTPARAM) value column.");
				return false;
			}

			WriteString(out, "", 0, 1);
			XMLOpenElementNoAttrEx("member", level++, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
		}
		else if (unwrapcol < 0)
		{
			WriteString(out, "", 0, 1);
			XMLOpenElementNoAttrEx("struct", level++, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
		}
	}

	FORALL_PARSETABLE(tpi, i)
	{
		U32 type = TOK_GET_TYPE(tpi[i].type);
		if (column >= 0 && i != column) continue;
		//XXXXXX: These if statements came from the writetext implementation. Need to be reviewed and cut.
		if (!tpi[i].name || !tpi[i].name[0]) continue; // unnamed fields shouldn't be parsed or written
		if (tpi[i].type & TOK_REDUNDANTNAME) continue;
		if (type == TOK_START) continue;
		if (type == TOK_IGNORE && i) continue;
		if (tpi[i].type & TOK_FLATEMBED) continue;
		if (unwrapcol >= 0 && i != unwrapcol) continue;
		if (decodecol >= 0 && i != decodecol) continue;
		//if (TOK_GET_TYPE(tpi[i].type) == TOK_CURRENTFILE_X && !(iWriteTextFlags & WRITETEXTFLAG_FORCEWRITECURRENTFILE)) continue;
		//if (!FlagsMatchAll(tpi[i].type,iOtpionFlagsToMatch)) continue;
		//if (!FlagsMatchNone(tpi[i].type,iOptionFlagsToExclude)) continue;
		//if (inheritanceColumn >= 0 && i != inheritanceColumn) continue;
		//if (pTemplateStruct && TokenCompare(tpi, i, struct_mem, pTemplateStruct, iOptionFlagsToMatch, iOptionFlagsToExclude) == 0)
		//{
		//	continue;
		//}

		if (iOptions & TPXML_FORMAT_XMLRPC)
		{
			if (type == TOK_END) continue;
			if (type == TOK_IGNORE) continue;

			if (decodeThisStruct) 
				if (i != keycolumn && i != valcolumn) continue;
		}
		else
		{
			//For start tokens, look at the previous token to get the open element name.
			if (!ppElementStack)//(TOK_GET_TYPE(tpi[i].type) == TOK_START)
			{
				pCurrentElement = &tpi[i];

				//Save the token for the closing tag.
				eaPush(&ppElementStack, pCurrentElement);
				if (iOptions & TPXML_WRITE_ENCLOSING_ELEMENT) 
				{
					XMLOpenElementNoAttrEx(pCurrentElement->name, level++, iOptions, out);
					WriteString(out, "", 0, 1);
				}
			}

			//For end tokens, we need to recall what we used for the start token, so pop it off the stack.
			//if ((!TOK_GET_TYPE(tpi[i+1].type) && !(tpi[i+1].type & TOK_FLATEMBED) ) || TOK_GET_TYPE(tpi[i].type) == TOK_END)
			if (TOK_GET_TYPE(tpi[i].type) == TOK_END)
			{
				pCurrentElement = eaPop(&ppElementStack);
				if (pCurrentElement)
				{
					if (iOptions & TPXML_WRITE_ENCLOSING_ELEMENT)
					{
						XMLCloseElementEx(pCurrentElement->name, --level, iOptions, out);
					}
				}
				else
				{
					Errorf("Error getting tag element in xml generation.");
				}
				break;
			}
		}
		

		{
			char *buf = NULL;
			bool success = false;
			FILE *fpBuff = NULL;
			StructFormatField opts = iOptions;
			int decodekey = 0;
			int encodebase64 = 0;
			GetIntFromTPIFormatString(&tpi[i], "XML_DECODE_KEY", &decodekey);
			GetIntFromTPIFormatString(&tpi[i], "XML_ENCODE_BASE64", &encodebase64);

			estrStackCreate(&buf);
			if (decodecol >= 0)
			{
				opts |= TPXML_DECODE_RPC_STRUCT;
			}
			if (decodeThisStruct && i == keycolumn)
			{	//strip type tags for names
				opts |= TPXML_FORMAT_NONE;
			}

			fpBuff = fileOpenEString(&buf);
			if (decodekey == 2)
			{
				opts |= TPXML_DECODE_RPC_STRUCT;
				XMLOpenElementNoAttrEx("struct", level+1, iOptions, fpBuff); 
			}
			if (encodebase64)
			{
				opts |= TPXML_ENCODE_BASE64;
			}

			success = writexmlfile_autogen(tpi, i, struct_mem, index,
				((iOptions & TPXML_FORMAT_XMLRPC)?level+2:level), fpBuff, opts);

			if (decodekey == 2)
			{
				XMLCloseElementEx("struct",0, iOptions | TPXML_WRITE_CLOSING_NEWLINE, fpBuff);
			}

			fileClose(fpBuff);

			if (success)
			{
				if (iOptions & TPXML_FORMAT_XMLRPC)
				{
					if (decodeThisStruct)
					{
						if (i == keycolumn)
						{
							XMLOpenElementNoAttrEx("name", level, iOptions, out); 
							if (decodekey == 1) 
								writetext_autogen(out, tpi, i, struct_mem, index, 0, 0, level+2, 0, 0, 0);
							else
								WriteString(out, buf, 0, 0); 
							XMLCloseElementEx("name",0, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
						}
						if (i == valcolumn)
						{
							XMLOpenElementNoAttrEx("value", level, iOptions, out); 
							WriteString(out, buf, 0, 0);
							XMLCloseElementEx("value",0, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
						}
					}
					else if(unwrapcol < 0 && decodecol < 0 && !(iOptions & TPXML_FORMAT_NONE) && column < 0)
					{
						XMLOpenElementNoAttrEx("member", level++, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
						XMLOpenElementNoAttrEx("name", level, iOptions, out); 

						if (decodekey == 1)
							writetext_autogen(out, tpi, i, struct_mem, -1, 0, 0, level+2, 0, 0, 0);
						else
							WriteString(out, tpi[i].name, 0, 0); 

						XMLCloseElementEx("name", 0, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
						XMLOpenElementNoAttrEx("value", level, iOptions, out);
						
						WriteString(out, buf, 0, 0);
						
						XMLCloseElementEx("value",0, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
						XMLCloseElementEx("member", --level, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
					}
					else
					{
						WriteString(out, buf, 0, 0);
					}
				}
				else
				{
					WriteString(out, buf, 0, 0);
				}
			}
			estrDestroy(&buf);
		}


	//this case never happens... Kelvin misunderstood how the array callbacks work
/*
		//Otherwise handle structs and primitives as they are in this array.
		else if (TYPE_INFO(tpi[i].type).writexmlfile)
		{
			if (iOptions & TPXML_FORMAT_XMLRPC && column < 0)
			{
				XMLOpenElementNoAttrEx("member", level++, iOptions, out); WriteString(out, "", 0,1);
				XMLOpenElementNoAttrEx("name", level, iOptions, out); WriteString(out, tpi[i].name, 0, 0); XMLCloseElementEx("name", 0, iOptions, out);
				XMLOpenElementNoAttrEx("value", level++, iOptions, out); WriteString(out, "", 0,1);
			}
			TYPE_INFO(tpi[i].type).writexmlfile(tpi, i, struct_mem, index, level+1, out, iOptions & ~TPXML_WRITE_CLOSING_NEWLINE);
			if (iOptions & TPXML_FORMAT_XMLRPC && column < 0)
			{
				XMLCloseElementEx("value", --level, iOptions, out);
				XMLCloseElementEx("member", --level, iOptions, out);
			}
		}
		//???
		else
		{
			//quiet
			continue;
		}*/
		//TOKARRAY_INFO(tpi[i].type).writetext(out, tpi, i, struct_mem, 0, 1, ignoreInherited, level,iWriteTextFlags, iOptionFlagsToMatch,iOptionFlagsToExclude);
	}

	if (iOptions & TPXML_FORMAT_XMLRPC && column < 0)
	{
		if (decodeThisStruct)
		{
			XMLCloseElementEx("member", --level, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
			WriteString(out, "", (iOptions & TPXML_NO_PRETTY) ? 0 : level-1 , 0);
		}
		else if (unwrapcol < 0)
		{
			XMLCloseElementEx("struct", --level, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
			WriteString(out, "", (iOptions & TPXML_NO_PRETTY) ? 0 : level-1, 0);
		}
	}
	else
	{
		if (ppElementStack)
		{
			eaClear(&ppElementStack);
			eaDestroy(&ppElementStack);
		}
	}

	return 1;
}

//TODO: What do these return values mean? Oops, they don't mean anything.
bool ParseWriteXMLFile(FILE *out, ParseTable *tpi, void *struct_mem, StructFormatField iOptions)
{
	TextParserAutoFixupCB *pFixupCB = ParserGetTableFixupFunc(tpi);
	int ok = 1;
	if (pFixupCB)
	{
		ok &= (pFixupCB(struct_mem, FIXUPTYPE_PRE_TEXT_WRITE, NULL) == PARSERESULT_SUCCESS);
	}

	//output the xml declaration
	if (TPXML_FORMAT(iOptions) == TPXML_FORMAT_DEFAULT)
		WriteString(out, XML_DECLARATION, 0, 1);

	if (iOptions & TPXML_OUTPUT_DTD)
	{
		//TODO: add DTD output here if people want it.
	}

	return ok && InnerWriteXML(out, tpi, -1, struct_mem, -1, 0, iOptions | TPXML_WRITE_ENCLOSING_ELEMENT);
}

//EString file-wrapped wrapper for xml output to an estring.
void ParserWriteXMLEx(char **estr, ParseTable *tpi, void *struct_mem, StructFormatField iOptions)
{
	FILE *fpBuff;

	if (!estr)
		return;

	PERFINFO_AUTO_START_FUNC();
	fpBuff = fileOpenEString(estr);
	ParseWriteXMLFile(fpBuff, tpi, struct_mem, iOptions);
	fileClose(fpBuff);
	PERFINFO_AUTO_STOP();
}

void ParserWriteXMLField(char **estr, ParseTable *tpi, int col, void *struct_mem, int index, StructFormatField iOptions)
{
	FILE *fpBuff;
	TextParserAutoFixupCB *pFixupCB = ParserGetTableFixupFunc(tpi);

	if (!estr)
		return;

	fpBuff = fileOpenEString(estr);

	if (pFixupCB)
	{
		if (pFixupCB(struct_mem, FIXUPTYPE_PRE_TEXT_WRITE, NULL) == PARSERESULT_SUCCESS)
			return;
	}


	InnerWriteXML(fpBuff, tpi, col, struct_mem, index, 0, iOptions);

	fileClose(fpBuff);
}

//This function is just a placeholder. Its implementation should change.
bool colorcomp_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	int i, numelems = tpi[column].param;
	U8 value;
	KVPair kv[1] = {{"color",NULL}};
	KVPair ** ppKVs = NULL;
	char *buf = NULL, *tmp = NULL;

	estrStackCreate(&buf);
	estrStackCreate(&tmp);

	estrPrintf(&buf, "#");

	for (i = 0; i < numelems; i++)
	{
		value = TokenStoreGetU8(tpi, column, structptr, i, NULL);
		estrPrintf(&tmp,"%02X", value);
		estrAppend(&buf, &tmp);
	}
	kv->value = buf;
	eaPush(&ppKVs, kv);

	if (iOptions & TPXML_FORMAT_XMLRPC)
	{
		if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("string",level, iOptions, out); 
		WriteString(out, buf, 0, 1); 
		if (!(iOptions & TPXML_FORMAT_NONE))XMLCloseElementEx("string", 0, iOptions, out);
	}
	else if (iOptions & TPXML_USE_SINGLE_ELEMENTS)
	{
		XMLSingleElementWithAttributes(tpi[column].name, ppKVs, level, iOptions, out);
	}
	else
	{
		XMLOpenElementNoAttrEx(tpi[column].name,level, iOptions, out);
		WriteString(out, "", 0,1);
		WriteString(out, buf, (iOptions & TPXML_NO_PRETTY) ? 0 : level+1, 1);
		XMLCloseElementEx(tpi[column].name, level, iOptions, out);
	}

	eaClear(&ppKVs);
	eaDestroy(&ppKVs);

	estrDestroy(&buf);
	estrDestroy(&tmp);


	return true;
}

//This implementation was copied mostly from the writetext function. 
// There's probably redundant or irrelevant code in it.
bool array_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	bool result = true;

	int numelems = TokenStoreGetNumElems(tpi, column, structptr, NULL);
	//int type = TOK_GET_TYPE(tpi[column].type);

	int i, default_value = 0;

	if (default_value == numelems)
		return false;

	if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_COLOR)
	{
		result = colorcomp_writexmlfile(tpi, column, structptr, 0, level, out, iOptions);
	}
	//Most arrays go just print their elements like this:
	else
	{
		if (iOptions & TPXML_FORMAT_XMLRPC)
		{
			if (iOptions & TPXML_DECODE_RPC_STRUCT)
			{
				for (i = 0; i < numelems; i++) 
				{
					nonarray_writexmlfile(tpi, column, structptr, i, level, out, iOptions);
				}
			}
			else
			{
				WriteString(out, "", 0, 1);
				XMLOpenElementNoAttrEx("array",level, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
				XMLOpenElementNoAttrEx("data",level+1, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out); 
				for (i = 0; i < numelems; i++) 
				{
					XMLOpenElementNoAttrEx("value",level+2, iOptions, out);
					nonarray_writexmlfile(tpi, column, structptr, i, level+3, out, iOptions);
					XMLCloseElementEx("value", 0, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
				}
				XMLCloseElementEx("data", level+1, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out); 
				XMLCloseElementEx("array", level, iOptions | TPXML_WRITE_CLOSING_NEWLINE, out);
				WriteString(out, "", (iOptions & TPXML_NO_PRETTY) ? 0 : level-1, 0);
			}
		}
		else
		{
			XMLOpenElementNoAttrEx(tpi[column].name,level, iOptions, out);
			WriteString(out, "", 0, 1);
			for (i = 0; i < numelems; i++)
			{
				//TODO: Right now, the child elements all have the same element name. This should be changed.
				nonarray_writexmlfile(tpi, column, structptr, i, level+1, out, iOptions);
			}
			XMLCloseElementEx(tpi[column].name,level, iOptions, out);
		}
	}

	return result;
}





bool command_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	//XXXXXX: probably want to figure out how to handle this case.
	//most other functions don't handle commands either though...
	return false;
}



bool struct_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	void* substruct = TokenStoreGetPointer(tpi, column, structptr, index, NULL);
	bool result = false;
	if (substruct)
	{
		//XXXXXX: this next comment is kind of backwards.
		//for structs in arrays, they will be in a table,
		if (TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(tpi[column].type)))
		{
			return InnerWriteXML(out, tpi[column].subtable, -1, substruct, -1, level, iOptions | TPXML_WRITE_ENCLOSING_ELEMENT);
		}
		else
		{
			if (iOptions & TPXML_FORMAT_XMLRPC)
			{
				return InnerWriteXML(out, tpi[column].subtable, -1, substruct, -1, level, iOptions );
			}
			else
			{
				XMLOpenElementNoAttrEx(tpi[column].name,level, iOptions, out);
				WriteString(out, "", 0,1);
				result = result || InnerWriteXML(out, tpi[column].subtable, -1, substruct, -1, level+1, iOptions &~ TPXML_WRITE_ENCLOSING_ELEMENT);
				XMLCloseElementEx(tpi[column].name, level, iOptions, out);
				return result;
			}
		}
	}
	else
	{
		return false;
	}
}

//Output a string xml element
bool string_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	const char* str = TokenStoreGetString(tpi, column, structptr, index, NULL);
	char *estr = NULL;
	int inlineblob = 0;
	GetIntFromTPIFormatString(&tpi[column], "XML_INLINE_BLOB", &inlineblob);

	estrStackCreate(&estr);

	if (str)
	{
		estrPrintf(&estr, "%s", str);
	}
	else
	{
		estrPrintf(&estr, "");
	}

	if (iOptions & TPXML_FORMAT_XMLRPC)
	{
		if (inlineblob != 0)
		{
			WriteString(out, estr, 0, 0);
		}
		else if (str)
		{
			ANALYSIS_ASSUME(str);
			if (iOptions & TPXML_ENCODE_BASE64)
			{
				if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("base64",0, iOptions, out);

				estrClear(&estr);
				estrBase64Encode(&estr, str, ((tpi[column].type & TOK_ESTRING)?estrLength(&str):(int)strlen(str)));
				WriteString(out, estr, 0, 0);

				if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("base64", 0, iOptions, out);
			}
			else
			{
				if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("string",0, iOptions, out);
				estrCopyValidXMLOnly(&estr, str);
				XMLWrapInCdata(&estr);
				WriteString(out, estr, 0, 0);
				if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("string", 0, iOptions, out);
			}
		}
	}
	else
	{
		XMLWrapInCdata(&estr);
		XMLOpenElementNoAttrEx(tpi[column].name, level, iOptions, out);
		WriteString(out, estr, 0, 0);
		XMLCloseElementEx(tpi[column].name, 0, iOptions, out);
	}

	estrDestroy(&estr);
	return true;
}

bool reference_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	const char* str = TokenStoreGetRefString(tpi, column, structptr, index, NULL);
	char *estr = NULL;

	estrStackCreate(&estr);

	if (langFieldToSimpleEString(LANGUAGE_DEFAULT, tpi, column, structptr, index, &estr, 0))
	{
		//already written
	}
	else if (str)
	{
		FILE *fpBuff = fileOpenEString(&estr);
		estrClear(&estr);
		WriteQuotedString(fpBuff, str, 0, true);
		fileClose(fpBuff);
	}
	else
	{
		estrClear(&estr);
	}

	if (iOptions & TPXML_FORMAT_XMLRPC)
	{
		if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("string",0, iOptions, out);
		XMLWrapInCdata(&estr);
		WriteString(out, estr, 0, 0);
		if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("string", 0, iOptions, out);
	}
	else
	{
		XMLWrapInCdata(&estr);
		XMLOpenElementNoAttrEx(tpi[column].name, level, iOptions, out);
		WriteString(out, estr, 0, 0);
		XMLCloseElementEx(tpi[column].name, 0, iOptions, out);
	}

	estrDestroy(&estr);
	return true;
}

//Output an unsigned byte xml element
bool u8_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	U8 value = TokenStoreGetU8(tpi, column, structptr, index, NULL);
	if (iOptions & TPXML_FORMAT_XMLRPC)
	{
		const char *define = NULL;
		char str[20];
		bool bWriteDefine = false;
		sprintf(str, "%i", value);

		if (tpi[column].subtable) 
		{
			define = StaticDefineRevLookup((StaticDefine*)tpi[column].subtable, str);
		}

		if (define)
		{
			ANALYSIS_ASSUME(define);
			bWriteDefine = strcmp(define, str);
		}

		if (bWriteDefine)
		{
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("string",0, iOptions, out);
			WriteString(out, define, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("string", 0, iOptions, out);
		}
		else
		{
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("i4",0, iOptions, out);
			WriteString(out, str, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("i4", 0, iOptions, out);
		}
	}
	else
	{
		XMLOpenElementNoAttrEx(tpi[column].name, level, iOptions, out);
		WriteInt(out, value, 0, false, tpi[column].subtable);
		XMLCloseElementEx(tpi[column].name, 0, iOptions, out);
	}
	return true;
}

//Output an boolean xml element
bool boolflag_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	U8 value = TokenStoreGetU8(tpi, column, structptr, index, NULL);
	if (iOptions & TPXML_FORMAT_XMLRPC)
	{
		if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("boolean",0, iOptions, out); 
		WriteString(out, (value?"1":"0"), 0, 0);
		if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("boolean", 0, iOptions, out);
	}
	else
	{
		XMLOpenElementNoAttrEx(tpi[column].name, level, iOptions,  out);
		//TODO: Maybe the options should dictate this output...
		WriteString(out, (value?"<true/>":"<false/>"), 0, 0);
		XMLCloseElementEx(tpi[column].name, 0, iOptions, out);
	}
	return true;
}

//Output an int xml element
bool int_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	int value = TokenStoreGetInt(tpi, column, structptr, index, NULL);
	if (iOptions & TPXML_FORMAT_XMLRPC)
	{
		const char *define = NULL;
		char str[20];
		bool bWriteDefine = false;
		sprintf(str, "%i", value);

		if (tpi[column].subtable) 
		{
			define = StaticDefineRevLookup((StaticDefine*)tpi[column].subtable, str);
		}
		if (define)
		{
			ANALYSIS_ASSUME(define);
			bWriteDefine = strcmp(define, str);
		}

		if (bWriteDefine)
		{
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("string",0, iOptions, out);
			WriteString(out, define, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("string", 0, iOptions, out);
		}
		else if (GetBoolFromTPIFormatString(&tpi[column], "HTML_SECS") || GetBoolFromTPIFormatString(&tpi[column], "HTML_SECS_AGO"))
		{
			timeMakeLocalIso8601StringFromSecondsSince2000(str, value);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("dateTime.iso8601",0,TPXML_NO_CASE_STRIPPING, out);
			WriteString(out, str, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("dateTime.iso8601", 0,TPXML_NO_CASE_STRIPPING, out);
		}
		else
		{
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("i4",0, iOptions, out);
			WriteString(out, str, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("i4", 0, iOptions, out);
		}
	}
	else
	{
		char *str = NULL;
		estrStackCreate(&str);
		XMLOpenElementNoAttrEx(tpi[column].name, level, iOptions, out);
		int_writestring(tpi, column, structptr, index, &str, 0, 0, 0, "will_destroy_immediately", __LINE__);
		WriteString(out, str, 0, 0);
		estrDestroy(&str);
		XMLCloseElementEx(tpi[column].name, 0, iOptions, out);
	}
	return true;
}

//Output an int64 xml element
bool int16_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	S16 value = TokenStoreGetInt16(tpi, column, structptr, index, NULL);
	if (iOptions & TPXML_FORMAT_XMLRPC)
	{
		const char *define = NULL;
		char str[20];
		bool bWriteDefine = false;
		sprintf(str, "%i", value);

		if (tpi[column].subtable) 
		{
			define = StaticDefineRevLookup((StaticDefine*)tpi[column].subtable, str);
		}
		if (define)
		{
			ANALYSIS_ASSUME(define);
			bWriteDefine = strcmp(define, str);
		}
		if (bWriteDefine)
		{
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("string",0, iOptions, out);
			WriteString(out, define, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("string", 0, iOptions, out);
		}
		else
		{
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("i4",0, iOptions, out);
			WriteString(out, str, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("i4", 0, iOptions, out);
		}
	}
	else
	{
		XMLOpenElementNoAttrEx(tpi[column].name, level, iOptions, out);
		WriteInt(out, value, 0, false, tpi[column].subtable);
		XMLCloseElementEx(tpi[column].name, 0, iOptions, out);
	}
	return true;
}

//Output an int64 xml element
bool int64_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	S64 value = TokenStoreGetInt64(tpi, column, structptr, index, NULL);
	if (iOptions & TPXML_FORMAT_XMLRPC)
	{
		const char *define = NULL;
		char str[20];
		bool bWriteDefine = false;
		sprintf(str, "%"FORM_LL"d", value);

		if (tpi[column].subtable) 
		{
			define = StaticDefineRevLookup((StaticDefine*)tpi[column].subtable, str);
		}
		if (define)
		{
			ANALYSIS_ASSUME(define);
			bWriteDefine = strcmp(define, str);
		}
		if (bWriteDefine)
		{
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("string",0, iOptions, out);
			WriteString(out, define, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("string", 0, iOptions, out);
		}
		else
		{
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("i4",0, iOptions, out);
			WriteString(out, str, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("i4", 0, iOptions, out);
		}
	}
	else
	{
		XMLOpenElementNoAttrEx(tpi[column].name, level, iOptions, out);
		WriteInt64(out, value, 0, false, tpi[column].subtable);
		XMLCloseElementEx(tpi[column].name, 0, iOptions, out);
	}
	return true;
}

//Output a float xml element
bool float_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	F32 value = TokenStoreGetF32(tpi, column, structptr, index, NULL);
	if (iOptions & TPXML_FORMAT_XMLRPC)
	{
		const char *define = NULL;
		char str[20];
		bool bWriteDefine = FALSE;
		float af = ABS(value);
		if (af < 10000.f && af >= 1.f)
			sprintf(str, "%.6f", value);
		else
			sprintf(str, "%.6g", value);

		if (tpi[column].subtable) 
		{
			define = StaticDefineRevLookup((StaticDefine*)tpi[column].subtable, str);
		}
		if (define)
		{
			ANALYSIS_ASSUME(define);
			bWriteDefine = strcmp(define, str);
		}
		if (bWriteDefine)
		{
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("string",0, iOptions, out);
			WriteString(out, define, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("string", 0, iOptions, out);
		}
		else
		{
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLOpenElementNoAttrEx("double",0, iOptions, out);
			WriteString(out, str, 0, false);
			if (!(iOptions & TPXML_FORMAT_NONE)) XMLCloseElementEx("double", 0, iOptions, out);
		}
	}
	else
	{
		XMLOpenElementNoAttrEx(tpi[column].name, level, iOptions, out);
		WriteFloat(out, value, 0, false, tpi[column].subtable);
		XMLCloseElementEx(tpi[column].name, 0, iOptions, out);
	}
	return true;
}



//Output a bit xml element
bool bit_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions)
{
	U8 value = TokenStoreGetBit(tpi, column, structptr, index, NULL);
	if (iOptions & TPXML_FORMAT_XMLRPC)
	{
		XMLOpenElementNoAttrEx("int",level, iOptions, out); WriteInt(out, value, 0, false, tpi[column].subtable); XMLCloseElementEx("int", 0, iOptions, out);
	}	
	else if (iOptions & TPXML_USE_SINGLE_ELEMENTS)
	{
		KVPair pKV[1] = {{"value", "off"}};
		KVPair ** ppKVs = NULL;
		eaCreate(&ppKVs);
		eaPush(&ppKVs, pKV);
		if (value) pKV->value = "on";

		XMLSingleElementWithAttributes(tpi[column].name, ppKVs, level, iOptions, out);

		eaClear(&ppKVs);
		eaDestroy(&ppKVs);
	}	
	else
	{
		XMLOpenElementNoAttrEx(tpi[column].name, level, iOptions, out);
		WriteInt(out, value, 0, false, tpi[column].subtable);
		XMLCloseElementEx(tpi[column].name, 0, iOptions, out);
	}
	return true;
}



//TODO: add implementations for other handled data types? bools, etc.

