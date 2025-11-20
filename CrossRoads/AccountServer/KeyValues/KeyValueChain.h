#pragma once

/************************************************************************/
/* Types                                                                */
/************************************************************************/

AUTO_STRUCT AST_CONTAINER;
typedef struct KeyValueChainElement
{
	CONST_STRING_MODIFIABLE pKey;	AST(PERSIST)
	const float fCoefficient;		AST(PERSIST)
} KeyValueChainElement;

AUTO_STRUCT AST_CONTAINER;
typedef struct KeyValueChain
{
	const U32 uContainerID;								AST(PERSIST KEY)
	CONST_STRING_MODIFIABLE pAlias;						AST(PERSIST)
	CONST_EARRAY_OF(KeyValueChainElement) eaElements;	AST(PERSIST)
} KeyValueChain;

// Initialize keyvalue chains
void initializeKeyValueChains(void);