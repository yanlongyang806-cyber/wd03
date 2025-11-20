/***************************************************************************



***************************************************************************/

#include "objSchema.h"
#include "MemoryPool.h"
#include "fileutil.h"
#include "windefinclude.h"
#include "timing.h"
#include "ResourceInfo.h"
#include "ThreadSafeMemoryPool.h"
#include "MemoryPool.h"
#include "tokenstore.h"
#include "objSchema_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define SCHEMA_DIR "server/objectdb/schemas/"

SchemaDictionary gSchemaDict;

MP_DEFINE(ContainerSchema);

StashTable sThreadSafeMemPoolSizesBySchemaName = NULL;

static bool sbMemPoolAllSchemaStructs = true;
static int siMemPoolAllSchemaStructsPoolSize = 256;

#ifdef _M_X64
// Make this a bit larger than 1MB to ensure that the allocations are large enough to be outside the windows heap segments.
static int siMinBlockSizeForSchemaStructs = ((1024 + 16) * 1024);
#else
static int siMinBlockSizeForSchemaStructs = 0;
#endif

AUTO_CMD_INT(sbMemPoolAllSchemaStructs, MemPoolAllSchemaStructs);
AUTO_CMD_INT(siMemPoolAllSchemaStructsPoolSize, MemPoolAllSchemaStructsPoolSize);
AUTO_CMD_INT(siMinBlockSizeForSchemaStructs, MinBlockSizeForSchemaStructs);

StashTable sSchemaMemoryStatusesByName = NULL;

//for servermonitoring purposes, assembles information about the threadsafe mem pools for each
//schema type
AUTO_STRUCT;
typedef struct SchemaMemoryStatus
{
	char *pName; AST(KEY)
	S64 iTotalSpace;  AST(FORMATSTRING(HTML_BYTES = 1))
	S64 iUsedSpace;  AST(FORMATSTRING(HTML_BYTES = 1))
	S64 iFreeSpace;  AST(FORMATSTRING(HTML_BYTES = 1))

	ThreadSafeMemoryPool **ppMemoryPools;
} SchemaMemoryStatus;

AUTO_FIXUPFUNC;
TextParserResult SchemaMemoryStatuFixupFunc(SchemaMemoryStatus *pStatus, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
			pStatus->iFreeSpace = pStatus->iTotalSpace = pStatus->iUsedSpace = 0;

			FOR_EACH_IN_EARRAY(pStatus->ppMemoryPools, ThreadSafeMemoryPool, pPool)
			{
				ThreadSafeMemoryPoolFixupFunc(pPool, FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED, NULL);
				pStatus->iFreeSpace += pPool->iFreeBytes_ForServerMonitoring;
				pStatus->iUsedSpace += pPool->iUsedBytes_ForServerMonitoring;
				pStatus->iTotalSpace += pPool->iTotalBytes_ForServerMonitoring;
			}
			FOR_EACH_END;

			break;
	}

	return true;
}



void objSetSchemaTypeWillUseThreadSafeMemPool(char *pTypeName, int iPoolSize)
{
	if (!sThreadSafeMemPoolSizesBySchemaName)
	{
		sThreadSafeMemPoolSizesBySchemaName = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	}

	stashAddInt(sThreadSafeMemPoolSizesBySchemaName, pTypeName, iPoolSize, true);
}


// This is for parsing schema definitions using textparser

ContainerSchema *CreateContainerSchema(void)
{
	ContainerSchema *schema;
	MP_CREATE(ContainerSchema, 100);

	schema = MP_ALLOC(ContainerSchema);
	return schema;
}

void DestroyContainerSchema(ContainerSchema *schema)
{
	int i;
	
	if (schema->loadedParseTable)
	{
		Errorf("memory leak... a schema with a loadedParseTable is being destroyed. Talk to Alex");
	}

	for (i = 0; i < eaSize(&schema->commitCallbacks); i++)
	{
		SAFE_FREE(schema->commitCallbacks[i]->matchString);
		SAFE_FREE(schema->commitCallbacks[i]);
	}
	eaDestroy(&schema->commitCallbacks);
	MP_FREE(ContainerSchema,schema);
}



ContainerSchema *BuildContainerSchema(ParseTable *parse, GlobalType containerType, bool nativeTable)
{
	ContainerSchema *newSchema = CreateContainerSchema();

	newSchema->classParse = parse;
	newSchema->createCallback = NULL;
	newSchema->destroyCallback = NULL;
	newSchema->initializeCallback = NULL;
	newSchema->deInitCallback = NULL;
	newSchema->containerType = containerType;
		
	{
		int idcolumn;
		if ((idcolumn = ParserGetTableKeyColumn(newSchema->classParse)) < 0 || TOK_GET_TYPE(newSchema->classParse[idcolumn].type) != TOK_INT_X)
		{			
			assertmsgf(0,"Container class %s MUST have an int key to act as containerID!", GlobalTypeToName(containerType));
		}
	}
	return newSchema;
}

MP_DEFINE(SchemaSource);

// This is for parsing schema definitions using textparser

SchemaSource *CreateSchemaSource(void)
{
	SchemaSource *source;
	MP_CREATE(SchemaSource, 100);

	source = MP_ALLOC(SchemaSource);
	return source;
}

void DestroySchemaSource(SchemaSource *source)
{
	if (source->parseTables)
	{
		ParseTableFree(&source->parseTables);
	}

	MP_FREE(SchemaSource,source);
}

char *objSchemaFileNameFromName(char *className)
{
	static char filename[FILENAME_MAX];

	//convert to lowercase?

	sprintf(filename,"%s%s.schema",SCHEMA_DIR,className);
	return filename;
}
//Write a given schema out to disk
void SaveSchemaToDisk(SchemaSource *source)
{
	char realPath[MAX_PATH];
	int  ret;
	fileLocateWriteBin(source->fileName,realPath);

	// Force it to be writable
	ret = _chmod(realPath, _S_IREAD | _S_IWRITE);
	// If ret is false, it doesn't exist and we're creating it

	if (source->parseTables)
	{
		ParseTableWriteTextFile(realPath,source->parseTables[0],source->className, PARSETABLESENDFLAG_FOR_SCHEMAS);
	}
	else
	{
		ParseTableWriteTextFile(realPath,source->staticTable,source->className, PARSETABLESENDFLAG_FOR_SCHEMAS);
	}

	//change it back to read-only, so gimme is happy
	_chmod(realPath, _S_IREAD);

}

void objSchemaToText(SchemaSource *source, char **estr)
{
	if (source->parseTables)
	{
		ParseTableWriteText(estr,source->parseTables[0],source->className, PARSETABLESENDFLAG_FOR_SCHEMAS);
	}
	else
	{
		ParseTableWriteText(estr,source->staticTable,source->className, PARSETABLESENDFLAG_FOR_SCHEMAS);
	}
}

//Write all native schemas to disk
void objExportNativeSchemas(void)
{
	int i;
	for (i = 0; i < eaSize(&gSchemaDict.schemaSources); i++)
	{
		if (gSchemaDict.schemaSources[i]->staticTable)
		{		
			SaveSchemaToDisk(gSchemaDict.schemaSources[i]);
		}
	}
}

void objDestroyAllSchemas(void)
{
	int i;
	for (i = 0; i < eaSize(&gSchemaDict.schemaSources); i++)
	{
		DestroySchemaSource(gSchemaDict.schemaSources[i]);
	}

	for (i = 0; i < eaSize(&gSchemaDict.containerSchemas); i++)
	{
		DestroyContainerSchema(gSchemaDict.containerSchemas[i]);
	}
}

static FileScanAction SchemaLoadCallback(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char schemaname[MAX_PATH];
	char *ext;
	FileScanAction retval = FSA_EXPLORE_DIRECTORY;

	// Ignore all directories.
	if(data->attrib & _A_SUBDIR)
		return retval;

	// Ignore all .bak files.
	if(strEndsWith(data->name, ".bak"))
		return retval;

	ext = strstri(data->name,".schema");
	if (!ext)
	{
		return retval;
	}

	// Grab the full filename.

	strncpyt(schemaname,data->name,ext - data->name + 1);

	objLoadGenericSchema(schemaname);

	return retval;
}


void objLoadAllGenericSchemas(void)
{
	// This should be integrated into ParserLoadFiles somehow
	fileScanAllDataDirs(SCHEMA_DIR, SchemaLoadCallback, NULL);
}

SchemaSource *objGetArchivedSchemaSource(GlobalType type, char *schematext)
{
	SchemaSource *source = CreateSchemaSource();
	char *name = GlobalTypeToName(type);
	char *path = objSchemaFileNameFromName(name);

	strcpy(source->className,name);
	strcpy(source->fileName,path);
	source->staticTable = NULL;

	if (ParseTableReadText(schematext,&source->parseTables,&source->baseSize,0, PARSETABLESENDFLAG_FOR_SCHEMAS))
	{
		source->containerSchema = BuildContainerSchema(source->parseTables[0],type,false);
	}
	return source;
}

void objDestroyArchivedSchemaSource(SchemaSource *source)
{
	if (source)
	{
		if (source->containerSchema) 
			DestroyContainerSchema(source->containerSchema);
		DestroySchemaSource(source);
	}
}

bool objLoadArchivedSchema(GlobalType type, char *schematext)
{
	SchemaSource *source = CreateSchemaSource();
	char *name = GlobalTypeToName(type);
	char *path = objSchemaFileNameFromName(name);
	ContainerSchema *topClass;

	strcpy(source->className,name);
	strcpy(source->fileName,path);
	source->staticTable = NULL;

	if (!ParseTableReadText(schematext,&source->parseTables,&source->baseSize,0, PARSETABLESENDFLAG_FOR_SCHEMAS))
	{
		DestroySchemaSource(source);
		return false;
	}

	topClass = BuildContainerSchema(source->parseTables[0],type,false);
	if (topClass)
	{
		eaPush(&gSchemaDict.containerSchemas,topClass);

		source->containerSchema = topClass;
		eaPush(&gSchemaDict.schemaSources, source);

		resRegisterDictionaryForContainerType(type);
		return true;
	}
	else
	{
		DestroySchemaSource(source);
		return false;
	}
}

void objLoadGenericSchema(char *className)
{
	GlobalType containerType = NameToGlobalType(className);
	ContainerSchema *topClass;

	if (containerType == GLOBALTYPE_NONE || objFindContainerSchema(containerType))
	{
		// Invalid or already loaded, ignore it
		return;
	}
	else
	{
		SchemaMemoryStatus *pMemoryStatus = NULL;
		SchemaSource *source = CreateSchemaSource();
		char *path = objSchemaFileNameFromName(className);

		strcpy(source->className,className);
		strcpy(source->fileName,path);
		source->staticTable = NULL;

		ParseTableReadTextFile(path,&source->parseTables,&source->baseSize,0, PARSETABLESENDFLAG_FOR_SCHEMAS);

		assertmsgf(source->parseTables && source->parseTables[0], "Failed to load schema file %s, check for data corruption", path);

		if (sbMemPoolAllSchemaStructs)
		{
			pMemoryStatus = StructCreate(parse_SchemaMemoryStatus);
			pMemoryStatus->pName = strdup(className);

			if (!sSchemaMemoryStatusesByName)
			{
				sSchemaMemoryStatusesByName = stashTableCreateWithStringKeys(16, StashDefault);
				resRegisterDictionaryForStashTable("Schema Memory Status", RESCATEGORY_SYSTEM, 0, sSchemaMemoryStatusesByName, parse_SchemaMemoryStatus);


			}

			stashAddPointer(sSchemaMemoryStatusesByName, pMemoryStatus->pName, pMemoryStatus, true);
		}

		FOR_EACH_IN_EARRAY(source->parseTables, ParseTable, pTable)
		{
			const char *pTableName = ParserGetTableName(pTable);
			int iTableSize = ParserGetTableSize(pTable);
			int iThreadSafeMemPoolSize;
			char nameToUse[1024];
			
			if (pTable == source->parseTables[0])
			{
				sprintf(nameToUse, "%s", className);
			}
			else
			{
				sprintf(nameToUse, "%s__%s", className, pTableName);
			}


			if (sbMemPoolAllSchemaStructs)
			{
				if (!ParserGetTPIThreadSafeMemPool(pTable))
				{
					ThreadSafeMemoryPool *pPool =  aligned_calloc_dbg(sizeof(ThreadSafeMemoryPool), 64, pTableName, LINENUM_FOR_TS_POOLED_STRUCTS);
					int iPoolSize = siMemPoolAllSchemaStructsPoolSize;

					if (siMinBlockSizeForSchemaStructs)
					{
						iPoolSize = siMinBlockSizeForSchemaStructs / ((iTableSize + 15) & ~15);
						if (iPoolSize < siMemPoolAllSchemaStructsPoolSize)
						{
							iPoolSize = siMemPoolAllSchemaStructsPoolSize;
						}
					}	
					
					threadSafeMemoryPoolInit_dbg(pPool, iPoolSize, (iTableSize + 15) & ~15, 0, nameToUse, pTableName, LINENUM_FOR_TS_POOLED_STRUCTS);
					ParserSetTPIUsesThreadSafeMemPool(pTable, pPool);
					eaPush(&pMemoryStatus->ppMemoryPools, pPool);
				}			
			} 
			else if (stashFindInt(sThreadSafeMemPoolSizesBySchemaName, pTableName, &iThreadSafeMemPoolSize))
			{
				if (!ParserGetTPIThreadSafeMemPool(pTable))
				{
					ThreadSafeMemoryPool *pPool =  aligned_calloc_dbg(sizeof(ThreadSafeMemoryPool), 64, pTableName, LINENUM_FOR_TS_POOLED_STRUCTS);
					threadSafeMemoryPoolInit_dbg(pPool, iThreadSafeMemPoolSize, (iTableSize + 15) & ~15, 0, nameToUse,  pTableName, LINENUM_FOR_TS_POOLED_STRUCTS);
					
					ParserSetTPIUsesThreadSafeMemPool(pTable, pPool);
				}
			}
		}
		FOR_EACH_END;

		topClass = BuildContainerSchema(source->parseTables[0],containerType,false);
		if (topClass)
		{
			eaPush(&gSchemaDict.containerSchemas,topClass);

			source->containerSchema = topClass;
			eaPush(&gSchemaDict.schemaSources, source);

			resRegisterDictionaryForContainerType(containerType);
		}
		else
		{
			DestroySchemaSource(source);
		}		
	}
}

void objRegisterNativeSchema(GlobalType containerType, ParseTable pt[], 
	CreateContainerCallback createCB, DestroyContainerCallback destroyCB, 
	InitializeContainerCallback initializeCB, DeInitializeContainerCallback clearCB, RegisterPreexistingContainerCallback registerCB)
{
	// Do something to allow reloading, eventually
	ContainerSchema *topClass;
	SchemaSource *source = CreateSchemaSource();
	char *className = GlobalTypeToName(containerType);
	char *path = objSchemaFileNameFromName(className);

	assertmsg(containerType,"Invalid Container type passed in to objRegisterNativeSchema!");
	assertmsg(!objFindContainerSchema(containerType),"The same container type was registered as a native schema twice!. You must register all native schemas before loading generic schemas.");
	assertmsg((source->baseSize = ParserGetTableSize(pt)) > 0,"ParseTable passed that has not yet been initialized. AutoStruct it!");
	
	strcpy(source->className,className);
	strcpy(source->fileName,path);
	source->staticTable = pt;
	source->parseTables = NULL;

	topClass = BuildContainerSchema(source->staticTable,containerType,true);

	if (topClass)
	{
		topClass->createCallback = createCB;
		topClass->destroyCallback = destroyCB;
		topClass->initializeCallback = initializeCB;
		topClass->deInitCallback = clearCB;
		topClass->registerCallback = registerCB;
		topClass->bIsNativeSchema = true;
		eaPush(&gSchemaDict.containerSchemas,topClass);

		source->containerSchema = topClass;
		eaPush(&gSchemaDict.schemaSources, source);

		resRegisterDictionaryForContainerType(containerType);
	}
	else
	{
		DestroySchemaSource(source);
	}
}

ContainerSchema *objFindContainerSchema(GlobalType type)
{
	int i;
	PERFINFO_AUTO_START("objFindContainerSchema",1);
	for (i = 0; i < eaSize(&gSchemaDict.containerSchemas); i++)
	{
		if (gSchemaDict.containerSchemas[i]->containerType == type)
		{
			PERFINFO_AUTO_STOP();
			return gSchemaDict.containerSchemas[i];
		}
	}
	PERFINFO_AUTO_STOP();
	return NULL;
}

SchemaSource *objFindContainerSchemaSource(GlobalType type)
{
	int i;
	PERFINFO_AUTO_START("objFindContainerSchema",1);
	for (i = 0; i < eaSize(&gSchemaDict.containerSchemas); i++)
	{
		if (gSchemaDict.schemaSources[i]->containerSchema->containerType == type)
		{
			PERFINFO_AUTO_STOP();
			return gSchemaDict.schemaSources[i];
		}
	}
	PERFINFO_AUTO_STOP();
	return NULL;
}

int objNumberOfContainerSchemas(void)
{
	return eaSize(&gSchemaDict.containerSchemas);
}

GlobalType objGetContainerSchemaGlobalTypeByIndex(int index)
{
	if(index >=0 && index < eaSize(&gSchemaDict.containerSchemas))
	{
		return gSchemaDict.schemaSources[index]->containerSchema->containerType;
	}

	return GLOBALTYPE_NONE;
}

bool objForceLoadSchemaFilesFromDisk(ContainerSchema *pSchema)
{
	char *path;
	ParseTable **ppTables = NULL;
	int iBaseSize;

	if (pSchema->loadedParseTable)
	{
		return true;
	}
	
	path = objSchemaFileNameFromName(GlobalTypeToName(pSchema->containerType));
	ParseTableReadTextFile(path,&ppTables,&iBaseSize,0, PARSETABLESENDFLAG_FOR_SCHEMAS);

	if (ppTables[0])
	{
		pSchema->loadedParseTable = ppTables[0];

		return true;
	}

	return false;
}

void
objExportContainerSchema(GlobalType type)
{
	SchemaSource *source = objFindContainerSchemaSource(type);
	if ( source != NULL )
	{
		SaveSchemaToDisk(source);
	}
}


#include "objSchema_c_ast.c"
