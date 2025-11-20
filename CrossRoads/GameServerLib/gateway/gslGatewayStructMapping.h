/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#ifndef GSLGATEWAYSTRUCTMAPPING_H__
#define GSLGATEWAYSTRUCTMAPPING_H__
#pragma once
GCC_SYSTEM

typedef struct ParseTable ParseTable;
typedef struct StructMapping StructMapping;
typedef struct PowerDef PowerDef;
typedef struct GatewaySession GatewaySession;

typedef void *(*StructMapperFunc)(StructMapping *pmap, void *pvSrc, GatewaySession *psess);
	// A function that converts a struct.
	//   pmap - The StructMapping structure defining this mapping. (Useful for pvScratch.)
	//   pvSrc - The struct to convert.
	//   psess - pointer to the session requesting the structure. NOTE: This might be NULL.

typedef struct StructMapping
{
	const char *pchDictionaryName;
		// Name of the dictionary being mapped

	const char *pchRealDictionaryName;
		// String name of the real dictionary where the data is contained.

	ParseTable *tpiSource;
		// The ParseTable of the items in that dictionary.
		// Used to generate an empty structure for WriteEmptyMappedStructJSON.

	ParseTable *tpiDest;
		// The ParseTable of the new struct to build.
		// Used to tell textparser what the table of the built object is.

	StructMapperFunc pfnConvert;
		// The function to call to map from a tpiSource to a tpiDest
		// This is responsible for allocating the mapped structure (probably
		//   in pvScratch).

	void *pobjGlobal;
		// If set, a dictionary isn't used for lookup. Instead this object is
		//   returned.

	void *pvScratch;
		// A scratch space for the pfnConvert function to use as it sees fit.
		// This is a shared scratch space.

} StructMapping;

#define STRUCT_MAPPING_STANDARD(name) { #name, #name, parse_##name, parse_Mapped##name, structmap_##name }
#define STRUCT_MAPPING_STANDARD_GLOBAL(name, pobjGlobal) { #name, #name, parse_##name, parse_Mapped##name, structmap_##name, pobjGlobal }
#define STRUCT_MAPPING_END { NULL }


LATELINK;
StructMapping *GetStructMappings(void);
	// Each project should override this function to return an array of
	//   StructMapping structures. This will be used to find the right
	//   conversions between the real structure and the structure exposed
	//   to the web client.
	// See gslGatewayStructmapping.c for boilerplate.

void WriteMappedStructJSON(char **pestr, const char *pchDict, void *pobj, GatewaySession *psess);
	// Maps the given object into its web form, and writes it to the given EString in JSON.
	//    psess is the session making the request. This may be NULL, so plan accordingly.

void WriteEmptyMappedStructJSON(char **pestr, const char *pchDict);
	// Maps a default version of the given resource into its web form, and writes it to the
	//   given EString in JSON.


void *structmap_Message(StructMapping *pmap, void *pvSrc, GatewaySession *psess);


#endif /* #ifndef GSLGATEWAYSTRUCTMAPPING_H__ */

/* End of File */
