#include "ResourceCommands.h"

#include "error.h"

#include "gimmeDLLWrapper.h"
#include "structInternals.h"
#include "TextParserInheritance.h"
#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_ENUM;
typedef enum ObjectCommandType {
	ObjectCommandType_DictInfo,
	ObjectCommandType_ObjInfo,
	ObjectCommandType_DataCopy,
} ObjectCommandType;

AUTO_STRUCT;
typedef struct PendingObjectCommand
{
	ObjectCommandType type;
	char *pDictName;
	char *objName;
	char *fileName;
} PendingObjectCommand;

PendingObjectCommand **ppPendingCommands;

#include "AutoGen/ResourceCommands_c_ast.c"

bool resAnyCommandsPending(void)
{
	if (eaSize (&ppPendingCommands))
	{
		return true;
	}
	return false;
}

int resExecuteAllPendingCommands(void)
{
	int result = 1;
	int i;
	
	if(eaSize(&ppPendingCommands))
	{
		PERFINFO_AUTO_START_FUNC();
		
		for (i = 0; i < eaSize(&ppPendingCommands); i++) 
		{
			PendingObjectCommand *pCommand = ppPendingCommands[i];
			switch (pCommand->type)
			{
				xcase ObjectCommandType_DictInfo:
					result &= resOutputDictionaryDescription(pCommand->pDictName, pCommand->fileName);
				xcase ObjectCommandType_ObjInfo:
					result &= resOutputObjectDescription(pCommand->pDictName, pCommand->objName, pCommand->fileName);
				xcase ObjectCommandType_DataCopy:
					result &= resPerformDataCopy(pCommand->fileName);
			}
		}

		eaClearStruct(&ppPendingCommands, parse_PendingObjectCommand);
		
		PERFINFO_AUTO_STOP();
	}

	return result;
}

static void AddPendingCommand(ObjectCommandType type, const char *pDictName, const char *objName, const char *fileName)
{
	PendingObjectCommand *newCommand = StructCreate(parse_PendingObjectCommand);
	newCommand->type = type;
	newCommand->pDictName = strdup(pDictName);
	newCommand->objName = strdup(objName);
	newCommand->fileName = strdup(fileName);
	eaPush(&ppPendingCommands, newCommand);
}

AUTO_COMMAND ACMD_COMMANDLINE;
void OutputDictionaryInfo(char *dictionaryName, char *fileName)
{
	AddPendingCommand(ObjectCommandType_DictInfo, dictionaryName, NULL, fileName);
}

AUTO_COMMAND ACMD_COMMANDLINE;
void OutputObjectInfo(char *dictionaryName, char *resourceName, char *fileName)
{
	AddPendingCommand(ObjectCommandType_ObjInfo, dictionaryName, resourceName, fileName);
}

AUTO_COMMAND ACMD_COMMANDLINE;
void PerformDataCopy(char *fileName)
{
	AddPendingCommand(ObjectCommandType_DataCopy, NULL, NULL, fileName);
}


int resOutputDictionaryDescription(const char *dictionaryName, const char *fileName)
{
	int result = 0;
	bool bFakeDictInfo = false;
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(dictionaryName);

	if (!pDictInfo || eaSize(&pDictInfo->ppInfos) == 0)
	{
		printf("Failed to output description for dictionary %s: Does not exist\n", dictionaryName);
		return 0;
	}

/*	if (!pDictInfo->ppInfos || eaSize(&pDictInfo->ppInfos) == 0)
	{
		char *objName;
		ResourceIterator objIterator = {0};

		bFakeDictInfo = true;
		pDictInfo = StructClone(parse_ResourceDictionaryInfo, pDictInfo);
		assert(pDictInfo);
		resInitIterator(dictionaryName, &objIterator);

		while (resIteratorGetNext(&objIterator, &objName, NULL))
		{
			const char *tempFileName;
			char fileNameBuf[MAX_PATH];

			ResourceInfo *newInfo = StructCreate(parse_ResourceInfo);
			newInfo->resourceDict = allocAddString(dictionaryName);
			newInfo->resourceName = allocAddString(objName);
			tempFileName = resGetLocation(dictionaryName, objName);
			if (tempFileName)
			{
				fileLocateWrite(tempFileName, fileNameBuf);
				newInfo->resourceLocation = allocAddFilename(fileNameBuf);
			}					
			eaPush(&pDictInfo->ppInfos, newInfo);
		}
		resFreeIterator(&objIterator);
	}*/

	result = ParserWriteTextFile(fileName, parse_ResourceDictionaryInfo, pDictInfo, 0, 0);

	if (bFakeDictInfo)
	{
		StructDestroy(parse_ResourceDictionaryInfo, pDictInfo);
	}
	
	if (!result)
	{
		printf("Failed to output description for dictionary %s: Failed to write %s\n", dictionaryName, fileName);
		return 0;
	}

	printf("Wrote dictionary description %s to %s\n", dictionaryName, fileName);
	return 1;
}


int resOutputObjectDescription(const char *dictionaryName, const char *resourceName, const char *fileName)
{
	ResourceInfoHolder *pInfos = StructCreate(parse_ResourceInfoHolder);

	if (resFindDependencies(dictionaryName, resourceName, pInfos))
	{	
		if (!ParserWriteTextFile(fileName, parse_ResourceInfoHolder, pInfos, 0, 0))
		{
			StructDestroy(parse_ResourceInfoHolder, pInfos);
			printf("Failed to output description for object %s %s: Failed to write %s\n", dictionaryName, resourceName, fileName);
			return 0;
		}
	}
	else
	{
		StructDestroy(parse_ResourceInfoHolder, pInfos);
		printf("Failed to output description for object %s %s: Failed to get dependencies\n", dictionaryName, resourceName);
		return 0;
	}
	StructDestroy(parse_ResourceInfoHolder, pInfos);
	printf("Wrote object description %s %s to %s\n", dictionaryName, resourceName, fileName);
	return 1;
}

int resPerformDataCopy(const char *fileName)
{
	//bool bSuccess = true;
	int i;
	ResourceInfoHolder *pInfos = StructCreate(parse_ResourceInfoHolder);

	if (!ParserReadTextFile(fileName, parse_ResourceInfoHolder, pInfos, 0))
	{
		StructDestroy(parse_ResourceInfoHolder, pInfos);
		printf("Failed to perform data copy: Can't read %s\n", fileName);
		return 0;
	}

	for (i = 0; i < eaSize(&pInfos->ppInfos); i++)
	{
		ResourceInfo *pInfo = pInfos->ppInfos[i];
		ParseTable *pTable = resDictGetParseTable(pInfo->resourceDict);
		void *pObject = resGetObject(pInfo->resourceDict, pInfo->resourceName);
		ResourceLoaderStruct *pLoaderStruct;
		void *pNewObject;
		int oldIndex;
		int j;

		if (!pObject || !pTable)
		{
			printf("Can't find source object for %s %s\n", pInfo->resourceDict, pInfo->resourceName);
			continue;
		}

		pNewObject = StructCloneVoid(pTable, pObject);

		if (StructInherit_IsInheriting(pTable, pNewObject))
		{
			StructInherit_StopInheriting(pTable, pNewObject);
		}

		for (j = 0; j < eaSize(&pInfo->ppReferences); j++)
		{
			ResourceReference *pRef = pInfo->ppReferences[j];
			if (!objPathSetString(pRef->referencePath, pTable, pNewObject, pRef->resourceName))
			{
				printf("Can't modify path %s for %s %s\n", pRef->referencePath, pInfo->resourceDict, pInfo->resourceName);
			}
		}

		SetUpResourceLoaderParse(pInfo->resourceDict,ParserGetTableSize(pTable),pTable,NULL);

		pLoaderStruct = StructCreate(parse_ResourceLoaderStruct);

		if (fileExists(pInfo->resourceLocation))
		{
			if (!ParserReadTextFile(pInfo->resourceLocation, parse_ResourceLoaderStruct, pLoaderStruct, 0))
			{
				printf("Can't read output file for %s %s\n", pInfo->resourceDict, pInfo->resourceName);
				continue;
			}
		}

		oldIndex = eaIndexedFindUsingString(&pLoaderStruct->earrayOfStructs, pInfo->resourceName);
		if (oldIndex >= 0)
		{
			StructDestroyVoid(pTable, pLoaderStruct->earrayOfStructs[oldIndex]);
			pLoaderStruct->earrayOfStructs[oldIndex] = pNewObject;
		}
		else
		{
			eaPush(&pLoaderStruct->earrayOfStructs, pNewObject);
		}

		if (!ParserWriteTextFile(pInfo->resourceLocation, parse_ResourceLoaderStruct, pLoaderStruct, 0, 0))
		{
			printf("Can't write output file for %s %s\n", pInfo->resourceDict, pInfo->resourceName);
			continue;
		}

		StructDestroy(parse_ResourceLoaderStruct, pLoaderStruct);

		parse_ResourceLoaderStruct[0].name = NULL;
		
	}

	StructDestroy(parse_ResourceInfoHolder, pInfos);
	printf("Data copy specified by %s succeeded\n", fileName);
	return 1;

}