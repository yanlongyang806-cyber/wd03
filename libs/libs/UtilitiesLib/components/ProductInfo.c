#include "productinfo.h"
#include "autogen/productinfo_h_ast.h"

LoadedProductInfo *GetProductNameFromDataFile(void)
{
	static LoadedProductInfo info = { "" };

	ParserReadTextFile("server/ProductInfo.txt", parse_LoadedProductInfo, &info, 0);

	return &info;
}
#include "autogen/productinfo_h_ast.c"
