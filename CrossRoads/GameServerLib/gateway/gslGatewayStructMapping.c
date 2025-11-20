/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#include "stdtypes.h"
#include "referencesystem.h"
#include "textparserJSON.h"
#include "Message.h"

#include "gslGatewayStructMapping.h"

#include "gslGatewayStructMapping_c_ast.h"

/////////////////////////////////////////////////////////////////////////////

static StructMapping *FindStructMapping(const char *pchDict)
{
	int i = 0;
	StructMapping *aStructMappings = GetStructMappings();
	if(aStructMappings)
	{
		while(aStructMappings[i].pchDictionaryName)
		{
			if(stricmp(aStructMappings[i].pchDictionaryName, pchDict) == 0)
			{
				return &aStructMappings[i];
			}

			i++;
		}
	}

	return NULL;
}

/////////////////////////////////////////////////////////////////////////////

//
// WriteMappedStructJSON
//
// Maps the given resource into its web form, and writes it to the given EString in JSON.
//
void WriteMappedStructJSON(char **pestr, const char *pchDict, const char *pchKey, GatewaySession *psess)
{
	void *pobj;
	ParseTable *tpi = NULL;
	const char *pchRealDict;

	StructMapping *pmap = FindStructMapping(pchDict);
	if(pmap)
	{
		pchRealDict = pmap->pchRealDictionaryName;
		tpi = pmap->tpiDest;
	}
	else
	{
		pchRealDict = pchDict;
	}

	if(!tpi)
	{
		tpi = RefSystem_GetDictionaryParseTable(pchRealDict);
	}

	if(pmap && pmap->pobjGlobal)
	{
		pobj = pmap->pobjGlobal;
	}
	else
	{
		pobj = RefSystem_ReferentFromString(pchRealDict, pchKey);
	}

	if(tpi && pobj)
	{
		if(pmap && pmap->pfnConvert)
		{
			pobj = pmap->pfnConvert(pmap, pobj, psess);
		}

		ParserWriteJSON(pestr, tpi, pobj, WRITEJSON_DONT_WRITE_EMPTY_OR_DEFAULT_FIELDS, 0, TOK_SERVER_ONLY|TOK_EDIT_ONLY|TOK_NO_NETSEND);
	}
}

//
// WriteEmptyMappedStructJSON
//
// Maps a default version of the given resource into its web form, and writes it to the
// given EString in JSON.
//
void WriteEmptyMappedStructJSON(char **pestr, const char *pchDict)
{
	void *pobj;
	void *pEmpty;
	ParseTable *tpiDest;
	ParseTable *tpiEmpty;
	StructMapping *pmap = FindStructMapping(pchDict);

	if(pmap)
	{
		tpiEmpty = pmap->tpiSource;
		tpiDest = pmap->tpiDest;
	}
	else
	{
		tpiDest = tpiEmpty = RefSystem_GetDictionaryParseTable(pchDict);
	}

	if(tpiEmpty && tpiDest)
	{
		pobj = pEmpty = StructAllocVoid(tpiEmpty);

		if(pmap && pmap->pfnConvert)
		{
			pobj = pmap->pfnConvert(pmap, pEmpty, NULL);
		}

		ParserWriteJSON(pestr, tpiDest, pobj, 0, 0, TOK_SERVER_ONLY|TOK_EDIT_ONLY|TOK_NO_NETSEND);

		StructDestroyVoid(tpiEmpty, pEmpty);
	}
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct MappedMessage
{
	char *str;
} MappedMessage;

void *structmap_Message(StructMapping *pmap, void *pvSrc, GatewaySession *psess)
{
	Message *psrc = (Message *)pvSrc;
	MappedMessage *pdest = (MappedMessage *)pmap->pvScratch;

	StructAllocIfNullVoid(parse_MappedMessage, pmap->pvScratch);
	pdest = (MappedMessage *)pmap->pvScratch;

	estrCopy2(&pdest->str, TranslateMessagePtr((Message *)psrc));

	return pdest;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

//
// GetStructMappings
//
// Override this in your project to return that project's struct mapping array.
//
StructMapping *DEFAULT_LATELINK_GetStructMappings(void)
{
	//
	// Here is a default set of struct mappings. You will want to copy this and
	// add to it for you particular project. You could check STGatewayStructMapping.c
	// for a more complete implementation.
	//
	static StructMapping s_aStructMappings[] =
	{
		STRUCT_MAPPING_STANDARD(Message),
		STRUCT_MAPPING_END
	};

	return s_aStructMappings;
}

/////////////////////////////////////////////////////////////////////////////

#include "gslGatewayStructMapping_c_ast.c"

// End of File
