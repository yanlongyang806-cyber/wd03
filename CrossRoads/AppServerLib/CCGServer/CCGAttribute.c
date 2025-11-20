/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CCGAttribute.h"
#include "earray.h"
#include <stdlib.h>
#include "StringCache.h"
#include "EString.h"

#include "AutoGen/CCGAttribute_h_ast.h"

bool
CCG_GetAttributeInt(CCGAttribute ***attrHandle, const char *name, int *attrOut)
{
	CCGAttribute *attr = eaIndexedGetUsingString(attrHandle, name);

	if ( attr == NULL )
	{
		return false;
	}
	*attrOut = strtol(attr->value, NULL, 10);

	return true;
}

bool
CCG_GetAttributeU32(CCGAttribute ***attrHandle, const char *name, U32 *attrOut)
{
	CCGAttribute *attr = eaIndexedGetUsingString(attrHandle, name);

	if ( attr == NULL )
	{
		return false;
	}
	*attrOut = strtoul(attr->value, NULL, 10);

	return true;
}

const char *
CCG_GetAttributeString(CCGAttribute ***attrHandle, const char *name)
{
	CCGAttribute *attr = eaIndexedGetUsingString(attrHandle, name);

	if ( attr == NULL )
	{
		return NULL;
	}

	return attr->value;
}

bool
CCG_AttributeExists(CCGAttribute * const * const *attrHandle, const char *name)
{
	int attrIndex;
	attrIndex = eaIndexedFindUsingString(attrHandle, name);

	return ( attrIndex >= 0 );
}

AUTO_TRANS_HELPER;
bool
CCG_trh_GetAttributeInt(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name, int *attrOut)
{
	NOCONST(CCGAttribute) *attr = eaIndexedGetUsingString(attrHandle, name);

	if ( attr == NULL )
	{
		return false;
	}
	*attrOut = strtol(attr->value, NULL, 10);

	return true;
}

AUTO_TRANS_HELPER;
bool
CCG_trh_GetAttributeU32(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name, U32 *attrOut)
{
	NOCONST(CCGAttribute) *attr = eaIndexedGetUsingString(attrHandle, name);

	if ( attr == NULL )
	{
		return false;
	}
	*attrOut = strtoul(attr->value, NULL, 10);

	return true;
}

AUTO_TRANS_HELPER;
char *
CCG_trh_GetAttributeString(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name)
{
	NOCONST(CCGAttribute) *attr = eaIndexedGetUsingString(attrHandle, name);
	if ( attr == NULL )
	{
		return NULL;
	}

	return attr->value;
}

AUTO_TRANS_HELPER;
void
CCG_trh_SetAttributeString(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name, const char * val)
{
	NOCONST(CCGAttribute) *attr = eaIndexedGetUsingString(attrHandle, name);

	if ( attr == NULL )
	{
		// attribute doesn't already exist, so create a new one
		attr = StructCreate(parse_CCGAttribute);
		attr->name = allocAddString(name);
		attr->value = strdup(val);

		eaPush(attrHandle, attr);
	}
	else
	{
		// attribute already exists, so free the old value (if it exists)
		//  and replace it with the new value
		if ( attr->value != NULL )
		{
			free(attr->value);
		}
		attr->value = strdup(val);
	}
}

AUTO_TRANS_HELPER;
void
CCG_trh_SetAttributeInt(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name, int val)
{
	char *tmpEStr = NULL;

	estrStackCreate(&tmpEStr);
	estrPrintf(&tmpEStr, "%d", val);

	CCG_trh_SetAttributeString(attrHandle, name, tmpEStr);

	estrDestroy(&tmpEStr);
}

AUTO_TRANS_HELPER;
void
CCG_trh_SetAttributeU32(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name, U32 val)
{
	char *tmpEStr = NULL;

	estrStackCreate(&tmpEStr);
	estrPrintf(&tmpEStr, "%u", val);

	CCG_trh_SetAttributeString(attrHandle, name, tmpEStr);

	estrDestroy(&tmpEStr);
}

AUTO_TRANS_HELPER;
bool
CCG_trh_AttributeExists(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name)
{
	int attrIndex;
	attrIndex = eaIndexedFindUsingString(attrHandle, name);

	return ( attrIndex >= 0 );
}

AUTO_TRANS_HELPER;
void
CCG_trh_RemoveAttribute(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name)
{
	int attrIndex;
	attrIndex = eaIndexedFindUsingString(attrHandle, name);
	if ( attrIndex >= 0 )
	{
		eaRemove(attrHandle, attrIndex);
	}
}

#include "CCGAttribute_h_ast.c"