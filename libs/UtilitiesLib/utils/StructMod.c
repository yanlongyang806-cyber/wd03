#include "StructMod.h"

#include "EString.h"
#include "MemoryPool.h"
#include "objPath.h"

#include "StructMod_h_ast.h"

MP_DEFINE(StructMod);

StructMod* structModCreate()
{
	MP_CREATE(StructMod, 16);
	return MP_ALLOC(StructMod);
}

void structModDestroy(StructMod* mod)
{
	MP_FREE(StructMod, mod);
}

int structModResolvePath(void* ptr, ParseTable *tableIn, const char* relObjPath, StructMod *mod)
{
	StructMod local = {0};

	if(!mod)
		mod = &local;

	if(relObjPath[0] == '.')
	{
		if(!objPathResolveField(relObjPath, tableIn, ptr, &mod->table, &mod->column, &mod->ptr, &mod->idx, 0))
			return 0;
	}
	else
	{
		char* objPathStr = NULL;
		int retval;
		estrStackCreate(&objPathStr);
		estrPrintf(&objPathStr, ".%s", relObjPath);
		retval = objPathResolveField(objPathStr, tableIn, ptr, &mod->table, &mod->column, &mod->ptr, &mod->idx, 0);
		estrDestroy(&objPathStr);

		if(!retval)
			return 0;
	}

	return 1;
}

void structModApply(StructMod *mod)
{
	FieldFromSimpleString(mod->table, mod->column, mod->ptr, mod->idx, mod->val);
}

#include "StructMod_h_ast.c"