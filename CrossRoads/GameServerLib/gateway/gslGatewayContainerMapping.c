/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 * Helper functions for entity mapping.
 *
 ***************************************************************************/

#include "objSchema.h"
#include "error.h"
#include "textparserJSON.h"
#include "timing.h"

#include "gslGatewaySession.h"
#include "gslGatewayContainerMapping.h"

/////////////////////////////////////////////////////////////////////////////

void DEFAULT_LATELINK_ContainerMappingInit(void)
{
	// Does nothing by default.
	// Override in your project if you have any one-time set up to do.
}

/////////////////////////////////////////////////////////////////////////////

ContainerMapping *FindContainerMappingForName(const char *pchName)
{
	int i = 0;
	ContainerMapping *aContainerMappings = GetContainerMappings();
	if(aContainerMappings)
	{
		while(aContainerMappings[i].pchName)
		{
			if(stricmp(aContainerMappings[i].pchName, pchName) == 0)
			{
				return &aContainerMappings[i];
			}

			i++;
		}
	}

	return NULL;
}

ContainerMapping *FindContainerMapping(GatewayGlobalType type)
{
	int i = 0;
	ContainerMapping *aContainerMappings = GetContainerMappings();
	if(aContainerMappings)
	{
		while(aContainerMappings[i].gatewaytype)
		{
			if(aContainerMappings[i].gatewaytype == type)
			{
				return &aContainerMappings[i];
			}

			i++;
		}
	}

	return NULL;
}

//
// MakeMappedContainer
//
// Fills in tracker->pmapped
//
void MakeMappedContainer(GatewaySession *psess, ContainerTracker *ptracker)
{
	void *pmapped = NULL;
	ContainerMapping *pcmap = ptracker->pMapping;

	PERFINFO_AUTO_START_FUNC();

	if(pcmap)
	{
		if(pcmap->pfnCreate)
		{
			pmapped = pcmap->pfnCreate(psess, ptracker, NULL);
		}
		else
		{
			ContainerSchema *pschema = objFindContainerSchema(ptracker->pMapping->globaltype);
			if(pschema && pschema->classParse == pcmap->tpiDest)
			{
				pmapped = StructCloneVoid(pcmap->tpiDest, GET_REF(*ptracker->phRef));
			}
			else
			{
				verbose_printf("ERROR in ContainerMappings for type %d\n", ptracker->gatewaytype);
			}
		}

 		if(pmapped && ptracker->pMapped && ptracker->pMapped != pmapped && pcmap->tpiDest)
 		{
 			StructWriteTextDiff(&ptracker->estrDiff, pcmap->tpiDest,
				ptracker->pMapped, pmapped,
				NULL, 0, 0, TEXTDIFFFLAG_ANNOTATE_REFERENCES | TEXTDIFFFLAG_JSON_SECS_TO_RFC822);
 		}

		if(ptracker->pMapped && ptracker->pMapped != pmapped)
		{
			if(pcmap->pfnDestroy)
			{
				pcmap->pfnDestroy(psess, ptracker, ptracker->pMapped);
			}
			else
			{
				StructDestroyVoid(pcmap->tpiDest, ptracker->pMapped);
			}
		}

		ptracker->pMapped = pmapped;
	}

	PERFINFO_AUTO_STOP();
}

//
// FreeMappedContainer
//
// Frees in tracker->pmapped
//
void FreeMappedContainer(GatewaySession *psess, ContainerTracker *ptracker)
{
	PERFINFO_AUTO_START_FUNC();
	if(ptracker->pMapping && ptracker->pMapped)
	{
		if(ptracker->pMapping->pfnDestroy)
		{
			ptracker->pMapping->pfnDestroy(psess, ptracker, ptracker->pMapped);
		}
		else
		{
			StructDestroyVoid(ptracker->pMapping->tpiDest, ptracker->pMapped);
		}

		ptracker->pMapped = NULL;
	}
	PERFINFO_AUTO_STOP();
}


//
// WriteContainerJSON
//
// Maps the given container into its web form, and writes it to the given
// EString in JSON.
//
void WriteContainerJSON(char **pestr, GatewaySession *psess, ContainerTracker *ptracker)
{
	ContainerMapping *cmap = ptracker->pMapping;
 
	PERFINFO_AUTO_START_FUNC();

	MakeMappedContainer(psess, ptracker);

	if(ptracker->pMapped && cmap->tpiDest)
	{
		int iExtraExcludes = (ptracker->idOwnerAccount != psess->idAccount ? TOK_SELF_ONLY : 0);

		PERFINFO_AUTO_START("ParserWriteJSON", 1);
		ParserWriteJSON(pestr, cmap->tpiDest, ptracker->pMapped,
			0, 0, TOK_SERVER_ONLY|TOK_EDIT_ONLY|TOK_NO_NETSEND|iExtraExcludes);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

//
// GetContainerMappings
//
// Override this in your project to return that project's container mapping array.
//
ContainerMapping *DEFAULT_LATELINK_GetContainerMappings(void)
{
	//
	// Here is a default set of struct mappings. You will want to copy this and
	// add to it for you particular project. You could check STGatewayContainerMapping.c
	// for a more complete implementation.
	//
	static ContainerMapping s_aContainerMappings[] =
	{
		CONTAINER_MAPPING_END
	};

	return s_aContainerMappings;
}

/////////////////////////////////////////////////////////////////////////////

//#include "gslGateway_c_ast.c"

// End of File
