/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

AUTO_STRUCT AST_CONTAINER;
typedef struct CCGAttribute
{
	CONST_STRING_POOLED name;		AST(PERSIST, KEY, POOL_STRING)
	CONST_STRING_MODIFIABLE value;	AST(PERSIST)
} CCGAttribute;

typedef struct NOCONST(CCGAttribute) NOCONST(CCGAttribute);

const char *CCG_GetAttributeString(CCGAttribute ***attrHandle, const char *name);
bool CCG_GetAttributeInt(CCGAttribute ***attrHandle, const char *name, int *attrOut);
bool CCG_GetAttributeU32(CCGAttribute ***attrHandle, const char *name, U32 *attrOut);
bool CCG_AttributeExists(CCGAttribute * const * const *attrHandle, const char *name);

char *CCG_trh_GetAttributeString(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name);
bool CCG_trh_GetAttributeInt(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name, int *attrOut);
bool CCG_trh_GetAttributeU32(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name, U32 *attrOut);

void CCG_trh_SetAttributeString(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name, const char * val);
void CCG_trh_SetAttributeInt(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name, int val);
void CCG_trh_SetAttributeU32(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name, U32 val);

void CCG_trh_RemoveAttribute(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name);
bool CCG_trh_AttributeExists(ATH_ARG NOCONST(CCGAttribute) ***attrHandle, const char *name);