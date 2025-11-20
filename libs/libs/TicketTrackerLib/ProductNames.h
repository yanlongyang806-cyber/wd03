#ifndef PRODUCTNAMES_H
#define PRODUCTNAMES_H

AUTO_STRUCT;
typedef struct ProductNamesList
{
	char ** ppProductNames; AST(POOL_STRING)
} ProductNamesList;

const char *** productNamesGetEArray(void);
int productNameGetIndex(const char *pProductName);
const char * productNameGetString(int index);

const char *productNameGetDisplayName(const char *pProductName);
const char *productNameGetShortDisplayName(const char *pProductName);

#endif