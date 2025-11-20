
#include "structInternals.h"

#include "tokenstore.h"

#include "error.h"

#include "../../3rdparty/clearsilver-0.10.5/ClearSilver.h"

#include "structinternals_h_ast.h"

#if PLATFORM_CONSOLE
#elif defined(_WIN64)
#pragma comment(lib, "clearsilverX64")
#elif defined(WIN32)
#pragma comment(lib, "clearsilver")
#endif

typedef struct KVPair {
	char * key;
	char * value;
} KVPair;



//Iterates through the parsetable and calls the appropriate writexmlfile function for each token.
bool InnerWriteHDF(HDF *hdf, ParseTable tpi[], int column, void *struct_mem, int index, char *name_override)
{
	int i;

	//For ignored tokens, keep them around incase we want to use them for enclosing tags.
	ParseTable *pCurrentElement;
	//In this case I'm using a stack just because I'm not familiar with the nature of the token array.
	ParseTable **ppElementStack = NULL;

	FORALL_PARSETABLE(tpi, i)
	{
		//XXXXXX: These if statements came from the writetext implementation. Need to be reviewed and cut.
		if (!tpi[i].name || !tpi[i].name[0]) continue; // unnamed fields shouldn't be parsed or written
		if (tpi[i].type & TOK_REDUNDANTNAME) continue;
		if (TOK_GET_TYPE(tpi[i].type) == TOK_START) continue;
		if (TOK_GET_TYPE(tpi[i].type) == TOK_IGNORE && i) continue;
		if (tpi[i].type & TOK_FLATEMBED) continue;
		//if (TOK_GET_TYPE(tpi[i].type) == TOK_CURRENTFILE_X && !(iWriteTextFlags & WRITETEXTFLAG_FORCEWRITECURRENTFILE)) continue;
		//if (!FlagsMatchAll(tpi[i].type,iOtpionFlagsToMatch)) continue;
		//if (!FlagsMatchNone(tpi[i].type,iOptionFlagsToExclude)) continue;
		//if (inheritanceColumn >= 0 && i != inheritanceColumn) continue;
		//if (pTemplateStruct && TokenCompare(tpi, i, struct_mem, pTemplateStruct, iOptionFlagsToMatch, iOptionFlagsToExclude) == 0)
		//{
		//	continue;
		//}

		//For start tokens, look at the previous token to get the open element name.
		if (!ppElementStack)//(TOK_GET_TYPE(tpi[i].type) == TOK_START)
		{
			pCurrentElement = &tpi[i];

			//Save the token for the closing tag.
			eaPush(&ppElementStack, pCurrentElement);
		}

		//For end tokens, we need to recall what we used for the start token, so pop it off the stack.
		//if ((!TOK_GET_TYPE(tpi[i+1].type) && !(tpi[i+1].type & TOK_FLATEMBED) ) || TOK_GET_TYPE(tpi[i].type) == TOK_END)
		if (TOK_GET_TYPE(tpi[i].type) == TOK_END)
		{
			pCurrentElement = eaPop(&ppElementStack);
			if (pCurrentElement)
			{
			}
			else
			{
				Errorf("Error getting tag element in xml generation.");
			}
			break;
		}

		writehdf_autogen(tpi, i, struct_mem, 0, hdf, name_override);
/*
		//This will call nonarray_writehdf for structs and primitives if they within their own array.
		if (TOKARRAY_INFO(tpi[i].type).writehdf)
		{
			TOKARRAY_INFO(tpi[i].type).writehdf(tpi, i, struct_mem, 0, hdf, name_override);
		}
		//Otherwise handle structs and primitives as they are in this array.
		else if (TYPE_INFO(tpi[i].type).writehdf)
		{
			TYPE_INFO(tpi[i].type).writehdf(tpi, i, struct_mem, 0, hdf, name_override);
		}
		//???
		else
		{
			//quiet
			continue;
		}*/
		//TOKARRAY_INFO(tpi[i].type).writetext(out, tpi, i, struct_mem, 0, 1, ignoreInherited, level,iWriteTextFlags, iOptionFlagsToMatch,iOptionFlagsToExclude);
	}

	if (ppElementStack)
	{
		eaClear(&ppElementStack);
		eaDestroy(&ppElementStack);
	}

	return 1;
}

//EString file-wrapped wrapper for xml output to an estring.
bool ParserWriteHDF(HDF *hdf, ParseTable *tpi, void *struct_mem)
{
	TextParserAutoFixupCB *pFixupCB = ParserGetTableFixupFunc(tpi);
	int ok = 1;
	if (pFixupCB)
	{
		ok &= (pFixupCB(struct_mem, FIXUPTYPE_PRE_TEXT_WRITE, NULL) == PARSERESULT_SUCCESS);
	}

	return ok && InnerWriteHDF(hdf, tpi, 0, struct_mem, 0, NULL);
}

//This function is just a placeholder. Its implementation should change.
//bool colorcomp_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, HDF *hdf)
//{
//	int i, numelems = tpi[column].param;
//	U8 value;
//	KVPair kv[1] = {{"color",NULL}};
//	KVPair ** ppKVs = NULL;
//	char *buf, *tmp;
//
//	estrStackCreate(&buf);
//	estrStackCreate(&tmp);
//
//	estrPrintf(&buf, "#");
//
//	for (i = 0; i < numelems; i++)
//	{
//		value = TokenStoreGetU8(tpi, column, structptr, i, NULL);
//		estrPrintf(&tmp,"%02X", value);
//		estrAppend(&buf, &tmp);
//	}
//	kv->value = buf;
//	eaPush(&ppKVs, kv);
//
//#if USE_SINGLE_XML_ELEMENTS==1
//	XMLSingleElementWithAttributes(tpi[column].name, ppKVs, level, out);
//#else
//	XMLOpenElementNoAttr(tpi[column].name,level, out);
//	WriteString(out, "", 0,1);
//	WriteString(out, buf, level+1, 1);
//	XMLCloseElement(tpi[column].name, level, out);
//
//#endif
//
//	eaClear(&ppKVs);
//	eaDestroy(&ppKVs);
//
//	estrDestroy(&buf);
//	estrDestroy(&tmp);
//
//
//	return false;
//}

//This implementation was copied mostly from the writetext function. 
// There's probably redundant or irrelevant code in it.
bool array_writehdf(ParseTable tpi[], int column, void* structptr, int index, HDF *hdf, char *name_override)
{
	bool result = true;

#ifdef WIN32
	int numelems = TokenStoreGetNumElems(tpi, column, structptr, NULL);
	//int type = TOK_GET_TYPE(tpi[column].type);

	int i, default_value = 0;
	HDF *node;
	char buf[16];

	if (default_value == numelems)
		return false;


	//if (TOK_GET_FORMAT_OPTIONS(tpi[column].format) == TOK_FORMAT_COLOR)
	//{
	//	colorcomp_writexmlfile(tpi, column, structptr, 0, level, out);
	//}
	////Most arrays go just print their elements like this:
	//else
	//{
	//	XMLOpenElementNoAttr(tpi[column].name,level, out);
	//	WriteString(out, "", 0, 1);
	//	for (i = 0; i < numelems; i++)
	//	{
	//		//TODO: Right now, the child elements all have the same element name. This should be changed.
	//		nonarray_writexmlfile(tpi, column, structptr, i, level+1, out);
	//	}
	//	XMLCloseElement(tpi[column].name,level, out);
	//}

	hdf_get_node(hdf, tpi[column].name, &node);
	for (i = 0; i < numelems; i++)
	{
		sprintf(buf, "%u", i);
		result = result && nonarray_writehdf(tpi, column, structptr, i, node, buf);
	}
#endif

	return result;
}





bool struct_writehdf(ParseTable tpi[], int column, void *structptr, int index, HDF *hdf, char *name_override)
{
	void* substruct = TokenStoreGetPointer(tpi, column, structptr, index, NULL);
	bool result = false;
	if (substruct)
	{
		//XXXXXX: this next comment is kind of backwards.
		//for structs in arrays, they will be in a table,
		//if (TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(tpi[column].type)))
		//{
		//	return InnerWriteHDF(hdf, tpi[column].subtable, 0, substruct, 0);
		//}
		//else
		//{
		//	XMLOpenElementNoAttr(tpi[column].name,level, out);
		//	WriteString(out, "", 0,1);
		//	result = result || InnerWriteXML(out, tpi[column].subtable, 0, substruct, 0, level+1, false);
		//	XMLCloseElement(tpi[column].name, level, out);
		//	return result;
		//}
#ifdef WIN32
		HDF *node;
		hdf_get_node(hdf, name_override ? name_override : tpi[column].name, &node);
		return InnerWriteHDF(node, tpi[column].subtable, 0, substruct, 0, NULL);
#endif
	}
	return false;
}

//Output a string to the hdf
bool string_writehdf(ParseTable tpi[], int column, void *structptr, int index, HDF *hdf, char *name_override)
{
#ifdef WIN32
	const char* str = TokenStoreGetString(tpi, column, structptr, index, NULL);
	hdf_set_value(hdf, name_override ? name_override : tpi[column].name, str);
#endif
	return true;
}

//Output an unsigned byte to the hdf
bool u8_writehdf(ParseTable tpi[], int column, void *structptr, int index, HDF *hdf, char *name_override)
{
#ifdef WIN32
	U8 value = TokenStoreGetU8(tpi, column, structptr, index, NULL);
	hdf_set_int_value(hdf, name_override ? name_override : tpi[column].name, value);
#endif
	return true;
}

//Output an int to the hdf
bool int_writehdf(ParseTable tpi[], int column, void *structptr, int index, HDF *hdf, char *name_override)
{
#ifdef WIN32
	int value = TokenStoreGetInt(tpi, column, structptr, index, NULL);
	hdf_set_int_value(hdf, name_override ? name_override : tpi[column].name, value);
#endif
	return true;
}

//Output an int16 to the hdf
bool int16_writehdf(ParseTable tpi[], int column, void *structptr, int index, HDF *hdf, char *name_override)
{
#ifdef WIN32
	S16 value = TokenStoreGetInt16(tpi, column, structptr, index, NULL);
	hdf_set_int_value(hdf, name_override ? name_override : tpi[column].name, value);
#endif
	return true;
}

//Output an int64 to the hdf
bool int64_writehdf(ParseTable tpi[], int column, void *structptr, int index, HDF *hdf, char *name_override)
{
#ifdef WIN32
	S64 value = TokenStoreGetInt64(tpi, column, structptr, index, NULL);
	hdf_set_value(hdf, name_override ? name_override : tpi[column].name, STACK_SPRINTF("%"FORM_LL"d", value));
#endif
	return true;
}

//Output a float to the hdf
bool float_writehdf(ParseTable tpi[], int column, void *structptr, int index, HDF *hdf, char *name_override)
{
#ifdef WIN32
	F32 value = TokenStoreGetF32(tpi, column, structptr, index, NULL);
	hdf_set_value(hdf, name_override ? name_override : tpi[column].name, STACK_SPRINTF("%f", value));
#endif
	return true;
}



//Output a bit to the hdf
bool bit_writehdf(ParseTable tpi[], int column, void *structptr, int index, HDF *hdf, char *name_override)
{
#ifdef WIN32
	U8 value = TokenStoreGetBit(tpi, column, structptr, index, NULL);
	hdf_set_int_value(hdf, name_override ? name_override : tpi[column].name, value);
#endif
	return true;
}



//TODO: add implementations for other handled data types? bools, etc.

