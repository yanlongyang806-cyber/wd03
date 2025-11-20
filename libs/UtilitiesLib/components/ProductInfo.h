#pragma once
GCC_SYSTEM

AUTO_STRUCT;
typedef struct LoadedProductInfo
{
	char productName[32];
	char shortProductName[32];
} LoadedProductInfo;

//this function should currently only be called by the MasterControlProgram. 
//It creates the controller and tells the controller the
//the product, which tells everything else
//
//It is also called by AssetManager, since that is launched
//independant of the MCP.
LoadedProductInfo *GetProductNameFromDataFile(void);
