/***************************************************************************



***************************************************************************/

#ifndef OBJSCHEMA_H_
#define OBJSCHEMA_H_
GCC_SYSTEM

// The data types that are used to represent the schema

typedef struct ContainerSchema ContainerSchema;

#include "GlobalTypeEnum.h"
#include "GlobalTypes.h"
#include "textparser.h"

typedef void *(*CreateContainerCallback)(ContainerSchema * sc);
typedef void (*DestroyContainerCallback)(ContainerSchema * sc, void * obj, const char* file, int line);
typedef void (*InitializeContainerCallback)(ContainerSchema * sc, void * obj);
typedef void (*DeInitializeContainerCallback)(ContainerSchema * sc, void * obj);
typedef void (*RegisterPreexistingContainerCallback)(ContainerSchema *sc, void *obj);

typedef struct CommitCallbackStruct CommitCallbackStruct;

// Structure defining information about a particular container type
typedef struct ContainerSchema
{
	ParseTable *classParse; // Parse table associated with this class

	//some servers have a native schema but also want a copy of the loaded-from-disk schema
	//for conversion purposes
	ParseTable *loadedParseTable;

	GlobalType containerType;

	CreateContainerCallback createCallback;
	DestroyContainerCallback destroyCallback;
	InitializeContainerCallback initializeCallback;
	DeInitializeContainerCallback deInitCallback;
	RegisterPreexistingContainerCallback registerCallback;

	CommitCallbackStruct **commitCallbacks;

	bool bIsNativeSchema;

} ContainerSchema;

// Describes where a given ContainerSchema was loaded from
typedef struct SchemaSource
{
	ParseTable *staticTable;
	ParseTable **parseTables;
	int baseSize;
	char className[GLOBALTYPE_MAXSCHEMALEN];
	char fileName[FILENAME_MAX];
	ContainerSchema *containerSchema;
} SchemaSource;

typedef struct SchemaDictionary
{
	ContainerSchema **containerSchemas; // Top level Container Schemas
	SchemaSource **schemaSources; // Where we load schemas from
} SchemaDictionary;

// Loading and saving schemas

//Load ParseTables, and then set up the ContainerSchema referring to their top-level class
void objLoadGenericSchema(char *className);

//Load ParseTables for a ContainerSchema from archived text. Do not register the schema.
SchemaSource *objGetArchivedSchemaSource(GlobalType type, char *schematext);
//Destroy a loaded archive ContainerSchema
void objDestroyArchivedSchemaSource(SchemaSource *source);

//Load ParseTables and set up the ContainerSchema from archived text.
bool objLoadArchivedSchema(GlobalType type, char *schematext);

//Load all schemas in the directory
void objLoadAllGenericSchemas(void);

//Destroys all loaded schemas and schema sources;
void objDestroyAllSchemas(void);

//Write all schemas to disk
void objExportNativeSchemas(void);

// Write the schema for the specified container type only
void objExportContainerSchema(GlobalType type);

// Returns the path to a schema file, given a name
char *objSchemaFileNameFromName(char *className);

// ContainerSchemas, which are used for character data

//Create a ContainerSchema from an existing ParseTable
void objRegisterNativeSchema(GlobalType containerType, ParseTable pt[],
	CreateContainerCallback createCB, DestroyContainerCallback destroyCB, 
	InitializeContainerCallback initializeCB, DeInitializeContainerCallback deInitCB,
	RegisterPreexistingContainerCallback registerCB);
 
// Find a schema with given container type
ContainerSchema *objFindContainerSchema(GlobalType type);

//Find a schemasource with a given container type
SchemaSource *objFindContainerSchemaSource(GlobalType type);

void objSchemaToText(SchemaSource *source, char **estr);

// Returns total number of container schemas
int objNumberOfContainerSchemas(void);

// Can be used (along with objNumberOfContainerSchemas()) to find all registered schema GlobalTypes
GlobalType objGetContainerSchemaGlobalTypeByIndex(int index);

//if you already have a native schema, but want to load the schema from disk as well
bool objForceLoadSchemaFilesFromDisk(ContainerSchema *pSchema);


//call this before loading generic schemas... sets that the named type will alloc/free from a threadsafe memory pool
void objSetSchemaTypeWillUseThreadSafeMemPool(char *pTypeName, int iPoolSize);
#endif