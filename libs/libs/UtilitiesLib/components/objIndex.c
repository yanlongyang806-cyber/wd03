/***************************************************************************



***************************************************************************/

//This file provides a means to generate and use a b+tree index on a collection of objects.
// KNN 20081020
// tags: btree b-tree b+tree bplustree search index compiledobjectpaths sorting iteration ranged fast

// THREAD SAFETY NOTES
//
// The objIndex is intended to be accessible from multiple threads. Each ObjectIndex has an
// internal locking mechanism. However, the API does not internally perform any locking or
// unlocking of the index. Managing the lock is your responsibility.
//
// About the locking mechanism:
//
// objIndex uses a read-write lock that permits up to N concurrent readers, or a single
// writer. The lock consists of a CRITICAL_SECTION (write_lock) and a semaphore
// (read_semaphore) with count N. Readers acquire the lock by decrementing the semaphore
// one time. Writers acquire the lock by entering the CRITICAL_SECTION, then decrementing
// the semaphore N times. Because Windows does not provide fairness for semaphore waiters,
// this scheme favors readers proportionally to N.
//
// Regarding safe use:
//
// To acquire the read or write locks, respectively, use:
//		objIndexObtainReadLock(index)
//		objIndexObtainWriteLock(index)
//
// To release the read or write locks, respectively, use:
//		objIndexReleaseReadLock(index)
//		objIndexReleaseWriteLock(index)
//
// The lock does not support upgrading from read to write, nor does it support downgrading
// from write to read. Additionally, you must NEVER recursively acquire the read lock. Doing
// so can cause a deadlock. (If thread X acquires the read lock, read_semaphore decreases
// to N-1. Then thread Y acquires the write lock, decrementing read_semaphore N times. The
// N-th decrement causes thread Y to block with read_semaphore == 0. If thread X now tries
// to recursively acquire the read lock, it will also block. X and Y are now deadlocked.)
//
// It is also very important to note that although the contents of the index may not change
// while the index is locked, the indexed objects may still change. An ObjectIndex does not
// own the objects being indexed, so there is no guarantee that they remain in a consistent
// state while you iterate or otherwise access the index. If you need those objects to
// remain consistent, you must manage their locking via some other mechanism.
//
// Usage example:
//
// A common usage pattern is to get the list of objects matching a given value as an earray.
// This can be accomplished like so:
//
//		ObjectIndexKey key = {0};
//		U32 keyInt = 42;
//		void **objectArray = NULL;
//
//		objIndexInitKey_Int(index, &key, keyInt);
//		objIndexObtainReadLock(index);
//		objIndexCopyEArrayOfKey(index, &objectArray, &key, false);
//		objIndexReleaseReadLock(index);
//		objIndexDeinitKey_Int(index, &key);
//
// Frequently, you will want to modify the object being indexed. If your modification(s)
// affect the specific field being indexed inside the object, you need to remove the object
// from the index during the changes. That looks like this:
//
//		objIndexObtainWriteLock(index);
//		objIndexRemove(index, object);
//		// Only release the lock if you don't care that readers can read
//		// the index while your object is temporarily missing from it
//		objIndexReleaseWriteLock(index);
//
//		// Apply modifications to object here
//
//		objIndexObtainWriteLock(index); // Only if you released above
//		objIndexInsert(index, object);
//		objIndexReleaseWriteLock(index);


#include "objIndex.h"

//For field comparison
#include "objPath.h"
//For field dereferencing
#include "tokenstore.h"
#include "tokenstore_inline.h"
#include "timing.h"
#include "file.h"
#include "Semaphore.h"

#include "cpu_count.h"
#include "earray.h"
#include "Estring.h"
#include "StashTable.h"
#include "textparser.h"
#include "textparserUtils.h"
#include "ThreadSafeMemoryPool.h"

#include "autogen/objIndex_c_ast.h"

char gExtraHeaderDataConfigPath[MAX_PATH] = "defs/config/ExtraHeaderDataConfig.def";
AUTO_CMD_STRING(gExtraHeaderDataConfigPath, ExtraHeaderDataConfigPath);

char gExtraHeaderDataConfigBinPath[MAX_PATH] = "defs/ExtraHeaderDataConfig.bin";
AUTO_CMD_STRING(gExtraHeaderDataConfigBinPath, ExtraHeaderDataConfigBinPath);

AUTO_STRUCT;
typedef struct ExtraHeaderDataConfig
{
	const char* ExtraHeaderData1Path;
	const char* ExtraHeaderData2Path;
	const char* ExtraHeaderData3Path;
	const char* ExtraHeaderData4Path;
	const char* ExtraHeaderData5Path;
} ExtraHeaderDataConfig;

ExtraHeaderDataConfig gExtraHeaderDataConfig;

// Load extra header data. This needs to be an auto startup so that the GameServer can bin it.
AUTO_STARTUP(AS_ExtraHeaderDataConfig);
void LoadExtraHeaderDataConfig(void)
{
	StructInit(parse_ExtraHeaderDataConfig, &gExtraHeaderDataConfig);
	ParserLoadFiles(NULL, gExtraHeaderDataConfigPath, gExtraHeaderDataConfigBinPath, PARSER_OPTIONALFLAG, parse_ExtraHeaderDataConfig, &gExtraHeaderDataConfig);
}

// Check whether the ExtraHeaderData has changed. If so, alert. This only needs to happen on the ObjectDB so it is directly called near DB load.
void LoadAndCheckExtraHeaderDataConfig(void)
{
	char tempFilePath[CRYPTIC_MAX_PATH];
	bool ok;
	ExtraHeaderDataConfig testExtraHeaderDataConfig = {0};
	StructInit(parse_ExtraHeaderDataConfig, &gExtraHeaderDataConfig);
	ParserLoadFiles(NULL, gExtraHeaderDataConfigPath, gExtraHeaderDataConfigBinPath, PARSER_OPTIONALFLAG, parse_ExtraHeaderDataConfig, &gExtraHeaderDataConfig);

	sprintf(tempFilePath, "%s/ExtraHeaderConfigLast.txt", fileLocalDataDir());
	ok = ParserReadTextFile(tempFilePath, parse_ExtraHeaderDataConfig, &testExtraHeaderDataConfig, 0);

	if(ok)
	{
		// Test for equality
		ok = (StructCompare(parse_ExtraHeaderDataConfig, &gExtraHeaderDataConfig, &testExtraHeaderDataConfig, 0, 0, 0) == 0);

		if(!ok)
		{
			ErrorOrCriticalAlert("OBJECTDB.HEADERDATACHANGED", "The game specific header data has changed. Check with the game team to see if this is expected.");
		}
	}

	if(!ok)
		ParserWriteTextFile(tempFilePath, parse_ExtraHeaderDataConfig, &gExtraHeaderDataConfig, 0, 0);

	StructDeInit(parse_ExtraHeaderDataConfig, &testExtraHeaderDataConfig);
}

const char *objIndexGetExtraDataPath(ObjectIndexHeaderField field)
{
	assert(OBJ_HEADER_EXTRA_DATA_1 <= field && field <= OBJ_HEADER_EXTRA_DATA_5);

	switch (field)
	{
		case OBJ_HEADER_EXTRA_DATA_1:
			return gExtraHeaderDataConfig.ExtraHeaderData1Path;
			break;
		case OBJ_HEADER_EXTRA_DATA_2:
			return gExtraHeaderDataConfig.ExtraHeaderData2Path;
			break;
		case OBJ_HEADER_EXTRA_DATA_3:
			return gExtraHeaderDataConfig.ExtraHeaderData3Path;
			break;
		case OBJ_HEADER_EXTRA_DATA_4:
			return gExtraHeaderDataConfig.ExtraHeaderData4Path;
			break;
		case OBJ_HEADER_EXTRA_DATA_5:
			return gExtraHeaderDataConfig.ExtraHeaderData5Path;
			break;
	}

	//Should never get here due to earlier assert
	assert(0);
	return NULL;
}

TSMP_DEFINE(ObjectBTKV);

ObjectBTKV *AllocObjectBTKV()
{
	ObjectBTKV *kv;

	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(ObjectBTKV, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	kv = TSMP_CALLOC(ObjectBTKV);
	return kv;
}

void FreeObjectBTKV(ObjectBTKV *kv)
{
	TSMP_FREE(ObjectBTKV, kv);
}

TSMP_DEFINE(ObjectBTNode);

ObjectBTNode *AllocObjectBTNode()
{
	ObjectBTNode *node;

	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(ObjectBTNode, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	node = TSMP_CALLOC(ObjectBTNode);
	return node;
}

void FreeObjectBTNode(ObjectBTNode *node)
{
	TSMP_FREE(ObjectBTNode, node);
}

static __inline bool nodeIsLeaf(ObjectBTNode *node)
{
	return (eaSize(&node->kvs) == 0 || //empty root
		!node->kvs[0] ||		//all non-root nodes have at least 2 elements.
		!node->kvs[0]->child);
}

//Recursively clean up ObjectBTNodes.
static void nodeDestroy(ObjectBTNode *node)
{
	if (!nodeIsLeaf(node))
	{
		while (eaSize(&node->kvs) > 0)
		{
			ObjectBTKV *kv = eaPop(&node->kvs);
			nodeDestroy(kv->child);
			FreeObjectBTKV(kv);
		}
	}
	eaDestroy(&node->kvs);
	FreeObjectBTNode(node);
}

//Recalculate the number of elements contained in node.
static void nodeRecount(ObjectBTNode *node)
{
	if (nodeIsLeaf(node))
	{
		node->count = eaSize(&node->kvs);
	}
	else
	{
		int i;
		node->count = 0;
		for (i = 0; i < eaSize(&node->kvs); i++)
			node->count += node->kvs[i]->child->count;
	}
	assert(node->count);
}

//Create an object index.
ObjectIndex* objIndexCreate(S16 order, U32 options, ObjectPath **paths, const char **origPaths, StashTable headerPathTable)
{
	ObjectIndex *oi = NULL;
	ObjectBTNode *root = NULL;
	int i;
	char *result = 0;

	assertmsg(eaSize(&paths) > 0, "Tried to create an index with no sort columns.\n");
	
	oi = (ObjectIndex*)calloc(1, sizeof(ObjectIndex));
	assert(oi);

	oi->useHeaders = !!headerPathTable;
	oi->headerPathTable = headerPathTable;

	oi->columns = 0;
	eaCreate(&oi->columns);

	estrStackCreate(&result);
	for (i = 0; i < eaSize(&paths); i++)
	{
		ObjectSortCol *col = (ObjectSortCol*)calloc(1, sizeof(ObjectSortCol));
		col->colPath = paths[i];
		col->context.column = -1;
		col->context.tpi = NULL;

		estrClear(&result);
		if (!ParserResolvePathComp(col->colPath, NULL, &col->context.tpi, &col->context.column, NULL, NULL, &result, 0))
		{
			char *error = 0;
			estrStackCreate(&error);
			estrPrintf(&error, "Could not resolve path for ObjectIndex creation. result: %s\n", result);
			assertmsg(false, error);
			estrDestroy(&error);
		}

		if(oi->useHeaders)
		{
			int headerField;
			int found;
			const char *pathstr = col->colPath->key->pathString;

			found = stashFindInt(headerPathTable, pathstr, &headerField);
			assertmsgf(found, "%s does not resolve to a valid header field for a header based index", pathstr);
			col->headerField = headerField;

		}
		else
		{
			col->comp = ParserGetCompareFunction(col->context.tpi, col->context.column);
			if (!col->comp)
			{
				char *error = 0;
				estrStackCreate(&error);
				estrPrintf(&error, "Failed to get comparison function for compiled objectpath: %s\n", col->colPath->key->pathString);
				assertmsg(false, error);
				estrDestroy(&error);
			}
		}

		eaPush(&oi->columns, col);
	}

	assertmsg(eaSize(&oi->columns), "ObjectIndex must have at least 1 valid sort column.\n");

	oi->opts = options;

	oi->count = 0;
	oi->depth = 1;

	if (order < 2) order = OBJINDEX_DEFAULT_ORDER;
	oi->order = order;

	root = AllocObjectBTNode();
	assert(root);

	root->prev = NULL;
	root->next = NULL;
	root->kvs = NULL;
	eaCreate(&root->kvs);
	eaSetCapacity(&root->kvs, oi->order * 2 + 1);
	root->count = 0;
	oi->root = root;
	oi->first = root;
	oi->last = root;

#if _PS3
    oi->read_count = 2;
#elif _XBOX
	oi->read_count = 6;
#else
	oi->read_count = getNumVirtualCpus() * 8;
#endif

	semaphoreInit(&oi->read_semaphore, oi->read_count, 0);
	assertmsg(oi->read_semaphore, "Could not create read semaphore for object index.\n");

	InitializeCriticalSection(&oi->write_lock);

	estrDestroy(&result);

	return oi;
}

//this function creates an ObjectPath, then calls objIndexCreate.
ObjectIndex* objIndexCreateWithStringPaths(S16 order, U32 options, ParseTable *tpi, char *paths, ...)
{
	ObjectPath **objectpaths = NULL;
	const char **origpaths = NULL;
	char *result = 0;
	char *path = paths;
	bool ok = true;
	va_list vl;
	ObjectIndex *oi;

	estrStackCreate(&result);
	
	va_start(vl, paths);
	while (ok && path)
	{
		ObjectPath *objectpath;
		//create the ObjectPath by resolving it.
		ok = ParserResolvePathEx(path, tpi, NULL, NULL, NULL, NULL, NULL, &objectpath, &result, NULL, NULL, OBJPATHFLAG_DONTLOOKUPROOTPATH | OBJPATHFLAG_INCREASEREFCOUNT);
		if (!ok)
		{
			char * error = 0;
			EARRAY_FOREACH_BEGIN(objectpaths, i);
			{
				InterlockedDecrement(&objectpaths[i]->refcount);
			}
			EARRAY_FOREACH_END;
			eaDestroy(&objectpaths);

			estrStackCreate(&error);
			estrPrintf(&error, "Could not resolve path: %s for ObjectIndex creation. result:%s\n", path, result);
			assertmsg(false, error);
			estrDestroy(&error);
		}
		eaPush(&objectpaths, objectpath);
		eaPush(&origpaths, path);
		path = va_arg(vl, char *);
	}
	va_end(vl);
	
	estrDestroy(&result);
	oi = objIndexCreate(order, options, objectpaths, origpaths, NULL);

	eaDestroy(&objectpaths);
	eaDestroy(&origpaths);

	return oi;
}

//Destroy an object index. Free all parts.
void objIndexDestroy(ObjectIndex **oi)
{
	ObjectIndex *index = *oi;
	*oi = NULL;

	if (!index) return;

	nodeDestroy(index->root);

	while (eaSize(&index->columns))
	{
		ObjectSortCol *col = eaPop(&index->columns);
		ObjectPathDestroy(col->colPath);
		free(col);
	}
	eaDestroy(&index->columns);

	semaphoreDestroy(&index->read_semaphore);
	DeleteCriticalSection(&index->write_lock);

	ZeroStruct(index);

	free(index);
}

bool objIndexPathAffected(ObjectIndex *oi, ObjectPath *path)
{
	if (!oi || !path) return false;

	PERFINFO_AUTO_START_FUNC_L3();

	EARRAY_FOREACH_BEGIN(oi->columns, i);
	{
		if (ObjectPathIsDescendant(oi->columns[i]->colPath, path))
		{
			PERFINFO_AUTO_STOP_L3();
			return true;
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_L3();
	return false;
}

bool objIndexPathsAffected(ObjectIndex *oi, EARRAY_OF(ObjectPath) paths)
{
	if (!oi || !paths) return false;

	PERFINFO_AUTO_START_FUNC_L2();

	EARRAY_FOREACH_BEGIN(paths, i);
	{
		if (objIndexPathAffected(oi, paths[i]))
		{
			PERFINFO_AUTO_STOP_L2();
			return true;
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_L2();
	return false;
}

bool objIndexObtainReadLock(ObjectIndex *oi)
{
	PERFINFO_AUTO_START_FUNC_L2();
	semaphoreWait(oi->read_semaphore);
	PERFINFO_AUTO_STOP_L2();
	return true;
}

void* objIndexReleaseReadLock(ObjectIndex *oi)
{
	PERFINFO_AUTO_START_FUNC_L2();
	semaphoreSignal(oi->read_semaphore);
	PERFINFO_AUTO_STOP_L2();
	return NULL;
}

bool objIndexObtainWriteLock(ObjectIndex *oi)
{
	int lockCount = 0;
	PERFINFO_AUTO_START_FUNC_L2();
	EnterCriticalSection(&oi->write_lock);
	while (lockCount < oi->read_count)
	{	//Obtain all the read locks
		semaphoreWait(oi->read_semaphore);
		lockCount++;
	}
	LeaveCriticalSection(&oi->write_lock);
	PERFINFO_AUTO_STOP_L2();
	return true;
}

void* objIndexReleaseWriteLock(ObjectIndex *oi)
{
	PERFINFO_AUTO_START_FUNC_L2();
	semaphoreSignalMulti(oi->read_semaphore, oi->read_count);
	PERFINFO_AUTO_STOP_L2();
	return NULL;
}

MultiValType objGetHeaderFieldType(ObjectIndexHeaderField field)
{
	switch(field)
	{
	case OBJ_HEADER_PUB_ACCOUNTNAME:
	case OBJ_HEADER_PRIV_ACCOUNTNAME:
	case OBJ_HEADER_SAVEDNAME:
	case OBJ_HEADER_EXTRA_DATA_1:
	case OBJ_HEADER_EXTRA_DATA_2:
	case OBJ_HEADER_EXTRA_DATA_3:
	case OBJ_HEADER_EXTRA_DATA_4:
	case OBJ_HEADER_EXTRA_DATA_5:
		return MULTI_STRING;
	case OBJ_HEADER_ACCOUNTID:
	case OBJ_HEADER_CONTAINERID:
	case OBJ_HEADER_CREATEDTIME:
	case OBJ_HEADER_LEVEL:
	case OBJ_HEADER_FIXUP_VERSION:
	case OBJ_HEADER_LAST_PLAYED_TIME:
	case OBJ_HEADER_VIRTUAL_SHARD_ID:
		return MULTI_INT;
	default:
		assertmsg(field, "Unspecified headerfield type");
		return MULTI_NONE;
	}
}

const char *objGetHeaderFieldStringVal(const ObjectIndexHeader *header, ObjectIndexHeaderField headerField)
{
	switch(headerField)
	{
	xcase OBJ_HEADER_PUB_ACCOUNTNAME:
		return header->pubAccountName;
	xcase OBJ_HEADER_PRIV_ACCOUNTNAME:
		return header->privAccountName;
	xcase OBJ_HEADER_SAVEDNAME:
		return header->savedName;
	xcase OBJ_HEADER_EXTRA_DATA_1:
		return header->extraData1;
	xcase OBJ_HEADER_EXTRA_DATA_2:
		return header->extraData2;
	xcase OBJ_HEADER_EXTRA_DATA_3:
		return header->extraData3;
	xcase OBJ_HEADER_EXTRA_DATA_4:
		return header->extraData4;
	xcase OBJ_HEADER_EXTRA_DATA_5:
		return header->extraData5;
	xdefault:
		assertmsg(0, "unsupported field for header string access");
	}
}

void objSetHeaderFieldStringVal(ObjectIndexHeader *header, ObjectIndexHeaderField headerField, const char *newStr)
{
	ObjectIndexHeader_NoConst *header_noConst = (ObjectIndexHeader_NoConst *)header;
	switch(headerField)
	{
	xcase OBJ_HEADER_PUB_ACCOUNTNAME:
		SAFE_FREE(header_noConst->pubAccountName);
		header->pubAccountName = strdup(newStr);
	xcase OBJ_HEADER_PRIV_ACCOUNTNAME:
		SAFE_FREE(header_noConst->privAccountName);
		header->privAccountName = strdup(newStr);
	xcase OBJ_HEADER_SAVEDNAME:
		SAFE_FREE(header_noConst->savedName);
		header->savedName = strdup(newStr);
	xcase OBJ_HEADER_EXTRA_DATA_1:
		SAFE_FREE(header_noConst->extraData1);
		header->extraData1 = strdup(newStr);
	xcase OBJ_HEADER_EXTRA_DATA_2:
		SAFE_FREE(header_noConst->extraData2);
		header->extraData2 = strdup(newStr);
	xcase OBJ_HEADER_EXTRA_DATA_3:
		SAFE_FREE(header_noConst->extraData3);
		header->extraData3 = strdup(newStr);
	xcase OBJ_HEADER_EXTRA_DATA_4:
		SAFE_FREE(header_noConst->extraData4);
		header->extraData4 = strdup(newStr);
	xcase OBJ_HEADER_EXTRA_DATA_5:
		SAFE_FREE(header_noConst->extraData5);
		header->extraData5 = strdup(newStr);
	xdefault:
		assertmsg(0, "unsupported field for header string access");
	}
}

U32 objGetHeaderFieldIntVal(const ObjectIndexHeader *header, ObjectIndexHeaderField headerField)
{
	switch(headerField)
	{
	xcase OBJ_HEADER_CONTAINERID:
		return header->containerId;
	xcase OBJ_HEADER_ACCOUNTID:
		return header->accountId;
	xcase OBJ_HEADER_CREATEDTIME:
		return header->createdTime;
	xcase OBJ_HEADER_LEVEL:
		return header->level;
	xcase OBJ_HEADER_FIXUP_VERSION:
		return header->fixupVersion;
	xcase OBJ_HEADER_LAST_PLAYED_TIME:
		return header->lastPlayedTime;
	xcase OBJ_HEADER_VIRTUAL_SHARD_ID:
		return header->virtualShardId;
	xdefault:
		assertmsg(0, "unsupported field for header int access");
	}
}

void objSetHeaderFieldIntVal(ObjectIndexHeader *header, ObjectIndexHeaderField headerField, int newInt)
{
	switch(headerField)
	{
	xcase OBJ_HEADER_CONTAINERID:
		header->containerId = newInt;
	xcase OBJ_HEADER_ACCOUNTID:
		header->accountId = newInt;
	xcase OBJ_HEADER_CREATEDTIME:
		header->createdTime = newInt;
	xcase OBJ_HEADER_LEVEL:
		header->level = newInt;
	xcase OBJ_HEADER_FIXUP_VERSION:
		header->fixupVersion = newInt;
	xcase OBJ_HEADER_LAST_PLAYED_TIME:
		header->lastPlayedTime = newInt;
	xcase OBJ_HEADER_VIRTUAL_SHARD_ID:
		header->virtualShardId = newInt;
	xdefault:
		assertmsg(0, "unsupported field for header int access");
	}
}

ObjectIndexKey *objIndexCreateKey(void)
{
	return calloc(1, sizeof(ObjectIndexKey));
}

void objIndexInitKey_F32(ObjectIndex *oi, ObjectIndexKey *key, F32 value)
{
	ParseTable *tpi = oi->columns[0]->context.tpi;
	int col = oi->columns[0]->context.column;
	ParseTable *ppt = &tpi[col];
	U32 storage = TokenStoreGetStorageType(ppt->type);
	devassertmsg(!oi->useHeaders, "F32 keys for header based indexed are currently not supported");
	key->val.type = MULTI_FLOAT;
	assertmsg(storage == TOK_STORAGE_DIRECT_SINGLE, "Trying to create an int key for a non-int index.\n");

	key->val.ptr = StructCreateVoid(tpi);
	TokenStoreSetF32_inline(tpi, &tpi[col], col, key->val.ptr_noconst, -1, value, NULL, NULL);
}

void objIndexInitKey_Int(ObjectIndex *oi, ObjectIndexKey *key, int value)
{
	ObjectSortCol *col0 = oi->columns[0];
	key->val.type = MULTI_INT;
	if(oi->useHeaders)
	{
		assertmsg(objGetHeaderFieldType(col0->headerField) == MULTI_INT,
			"Can't create int key from non-int index");
		key->val.int32 = value;
	}
	else
	{
		ParseTable *tpi = oi->columns[0]->context.tpi;
		int col = oi->columns[0]->context.column;
		ParseTable *ppt = &tpi[col];
		U32 storage = TokenStoreGetStorageType(ppt->type);
		assertmsg(storage == TOK_STORAGE_DIRECT_SINGLE, "Trying to create an int key for a non-int index.\n");

		key->val.ptr_noconst = StructCreateVoid(tpi);
		TokenStoreSetInt_inline(tpi, &tpi[col], col, key->val.ptr_noconst, -1, value, NULL, NULL);
	}
}

void objIndexInitKey_Int64(ObjectIndex *oi, ObjectIndexKey *key, S64 value)
{
	ObjectSortCol *col0 = oi->columns[0];
	key->val.type = MULTI_INT;
	if(oi->useHeaders)
	{
		assertmsg(objGetHeaderFieldType(col0->headerField) == MULTI_INT,
			"Can't create int key from non-int index");
		key->val.intval = value;
	}
	else
	{
		ParseTable *tpi = oi->columns[0]->context.tpi;
		int col = oi->columns[0]->context.column;
		ParseTable *ppt = &tpi[col];
		U32 storage = TokenStoreGetStorageType(ppt->type);
		assertmsg(storage == TOK_STORAGE_DIRECT_SINGLE, "Trying to create an int key for a non-int index.\n");

		key->val.ptr_noconst = StructCreateVoid(tpi);
		TokenStoreSetInt64_inline(tpi, &tpi[col], col, key->val.ptr_noconst, -1, value, NULL, NULL);
	}
}

void objIndexInitKey_String(ObjectIndex *oi, ObjectIndexKey *key, const char *value)
{
	ObjectSortCol *col0 = oi->columns[0];
	key->val.type = MULTI_STRING;

	if (oi->useHeaders)
	{
		assertmsg(objGetHeaderFieldType(col0->headerField) == MULTI_STRING,
			"Can't create string key from non-string index");
		key->val.str = value;
	}
	else
	{
		ParseTable *tpi = col0->context.tpi;
		int col = col0->context.column;
		ParseTable *ppt = &tpi[col];
		U32 storage = TokenStoreGetStorageType(ppt->type);
		
		switch (storage) {
			case TOK_STORAGE_DIRECT_SINGLE:
			case TOK_STORAGE_INDIRECT_SINGLE:
				{
					key->val.ptr_noconst = StructCreateVoid(tpi);
					TokenStoreSetString(tpi, col, key->val.ptr_noconst, -1, value, NULL, NULL, NULL, NULL);
				}
				break;
			default:
				assertmsg(false, "Trying to create a string key for a non-string index.\n");
		}
	}
}

void objIndexInitKey_U8(ObjectIndex *oi, ObjectIndexKey *key, U8 value)
{
	ParseTable *tpi = oi->columns[0]->context.tpi;
	int col = oi->columns[0]->context.column;
	ParseTable *ppt = &tpi[col];
	U32 storage = TokenStoreGetStorageType(ppt->type);
	key->val.type = MULTI_INT;
	devassertmsg(!oi->useHeaders, "U8 keys for header based indexed are currently not supported");
	assertmsg(storage == TOK_STORAGE_DIRECT_SINGLE, "Trying to create an int key for a non-int index.\n");

	key->val.ptr_noconst = StructCreateVoid(tpi);
	TokenStoreSetU8_inline(tpi, &tpi[col], col, key->val.ptr_noconst, -1, value, NULL, NULL);
}

void objIndexInitKey_Template(ObjectIndex *oi, ObjectIndexKey *key, ...)
{
	bool success = true;
	va_list vl;
	ObjectIndexHeader *threadheader;
	STATIC_THREAD_ALLOC(threadheader);

	PERFINFO_AUTO_START_FUNC_L2();
	key->val.type = MULTI_NP_POINTER;

	if(oi->useHeaders)
	{
		DeinitObjectIndexHeader(threadheader);
		key->val.ptr = threadheader;
		key->str = threadheader;
	}
	else
		key->str = StructCreateVoid(oi->columns[0]->colPath->key->rootTpi);

	va_start(vl, key);
	EARRAY_FOREACH_BEGIN(oi->columns, i);
	{
		if(oi->useHeaders)
		{
			MultiValType fieldType = objGetHeaderFieldType(oi->columns[i]->headerField);

			switch(fieldType)
			{
			xcase MULTI_INT:
				objSetHeaderFieldIntVal(threadheader, oi->columns[i]->headerField, va_arg(vl, int));
			xcase MULTI_STRING:
				objSetHeaderFieldStringVal(threadheader, oi->columns[i]->headerField, va_arg(vl, char*));
			xdefault:
				devassertmsgf(0, "Can't figure out type of header field %d", oi->columns[i]->headerField);
			}
		}
		else
		{
			void *strptr = NULL;
			int col = -1;
			int ind = -1;
			ParseTable *tpi = NULL;

			success = ParserResolvePathComp(oi->columns[i]->colPath, key->str, &tpi, &col, &strptr, &ind, NULL, OBJPATHFLAG_CREATESTRUCTS);
			if (!success) break;

			//first column fast lookup
			if (!i) key->val.ptr_noconst = strptr;

			switch (TOK_GET_TYPE(tpi[col].type))
			{
			case TOK_U8_X:			// U8 (unsigned char)
				if (TokenStoreGetStorageType(tpi[col].type) != TOK_STORAGE_DIRECT_SINGLE)
				{
					success = false;
					break;
				}
				TokenStoreSetU8_inline(tpi, &tpi[col], col, strptr, ind, va_arg(vl, int), NULL, NULL);
				break;
			case TOK_INT16_X:		// 16 bit integer
			case TOK_INT_X:			// int
			case TOK_INT64_X:		// 64 bit integer
				if (TokenStoreGetStorageType(tpi[col].type) != TOK_STORAGE_DIRECT_SINGLE)
				{
					success = false;
					break;
				}
				TokenStoreSetInt_inline(tpi, &tpi[col], col, strptr, ind, va_arg(vl, int), NULL, NULL);
				break;
			case TOK_F32_X:			// F32 (float), can be initialized with <param> but you only get an integer value
				if (TokenStoreGetStorageType(tpi[col].type) != TOK_STORAGE_DIRECT_SINGLE)
				{
					success = false;
					break;
				}
				TokenStoreSetF32_inline(tpi, &tpi[col], col, strptr, ind, va_arg(vl, F32), NULL, NULL);
				break;
			case TOK_STRING_X:		// char*
				{
					switch (TokenStoreGetStorageType(tpi[col].type) )
					{
					case TOK_STORAGE_DIRECT_SINGLE:
					case TOK_STORAGE_INDIRECT_SINGLE: TokenStoreSetString(tpi, col, strptr, ind, va_arg(vl, char*), NULL, NULL, NULL, NULL); break;
					default: success = false;
					}
				} break;
			default:
				success = false;
			}
			if (!success) break;
		}
	}
	EARRAY_FOREACH_END;
	va_end(vl);

	if (!success)
	{
		StructDestroyVoid(oi->columns[0]->colPath->key->rootTpi, key->str);
		key->str = NULL;
		key->val.type = MULTI_NONE;
	}
	PERFINFO_AUTO_STOP_L2();
}

void objIndexInitKey_Struct(ObjectIndex *oi, ObjectIndexKey *key, void *strptr)
{
	bool ok = true;
	assert(strptr);

	key->val.type = MULTI_NP_POINTER;

	if(oi->useHeaders)
	{
		key->val.ptr = strptr;
		key->str = strptr;
	}
	else
	{
		key->str = strptr;
		ok = ParserResolvePathComp(oi->columns[0]->colPath, strptr, NULL, NULL, &key->val.ptr_noconst, NULL, NULL, 0);
		if (!ok)
		{
			key->val.type = MULTI_NONE;
			key->str = key->str = NULL;
		}
	}
}

void objIndexDeinitKey(ObjectIndex *oi, ObjectIndexKey *key, bool destroyData)
{
	if (!key)
		return;

	PERFINFO_AUTO_START_FUNC_L2();
	if(!oi->useHeaders && destroyData)
	{
		switch(key->val.type)
		{
		case MULTI_INT:
		case MULTI_FLOAT:
		case MULTI_STRING:
			StructDestroyVoid(oi->columns[0]->context.tpi, key->val.ptr_noconst);
			break;
		case MULTI_NP_POINTER:
			StructDestroyVoid(oi->columns[0]->colPath->key->rootTpi, key->str);
			break;
		}
	}
	PERFINFO_AUTO_STOP_L2();
}

void objIndexDestroyKey(ObjectIndex *oi, ObjectIndexKey **key, bool destroyData)
{
	if(!key || !*key)
		return;

	PERFINFO_AUTO_START_FUNC_L2();
	objIndexDeinitKey(oi, *key, destroyData);
	SAFE_FREE(*key);
	PERFINFO_AUTO_STOP_L2();
}

MultiValType objIndexGetKeyType(ObjectIndex *oi)
{
	ParseTable *tpi = NULL;
	int col;
	int index;
	if (!oi)
		return MULTI_INVALID;

	if (!ParserResolvePathComp(oi->columns[0]->colPath, NULL, &tpi, &col, NULL, &index, NULL, 0))
		return MULTI_NONE;
	
	switch (TOK_GET_TYPE(tpi[col].type))
	{
	case TOK_IGNORE:			// do nothing with this token: ignores remainder of line during parse
		return MULTI_NONE;
	case TOK_START:			// not required: but used as the open brace for a structure
	case TOK_END:			// terminate the structure described by the parse table
		return MULTI_INVALID;

	case TOK_QUATPYR_X:		// F32[4]: quaternion: read in as a pyr
		return MULTI_QUAT_F;
	case TOK_MATPYR_X:		// F32[3][3] in memory turns into F32[3] (PYR) when serialized
		return MULTI_MAT4_F;

	case TOK_REFERENCE_X:	// YourStruct*: subtable is dictionary name
		return MULTI_NP_POINTER_F;

	case TOK_TIMESTAMP_X:	// stored as int: filled with fileLastChanged() of currently parsed text file
	case TOK_BOOL_X:			// stored as u8: restricted to 0 or 1
	case TOK_BOOLFLAG_X:		// int: no parameters in script file: if token exists: field is set to 1
	case TOK_U8_X:			// U8 (unsigned char)
	case TOK_INT16_X:		// 16 bit integer
	case TOK_INT_X:			// int
	case TOK_INT64_X:		// 64 bit integer
	case TOK_BIT:			// A bitfield... only generated by AUTOSTRUCT
		return MULTI_INT;
	case TOK_F32_X:			// F32 (float): can be initialized with <param> but you only get an integer value
		return MULTI_FLOAT;
	case TOK_STRING_X:		// char*
	case TOK_FILENAME_X:		// same as string: passed through forwardslashes & _strupr
		return MULTI_STRING;

	case TOK_STRUCT_X:		// YourStruct**: pass size as parameter: use eaSize to get number of items
		return MULTI_NP_POINTER;

	//	// built-ins
	//case TOK_CURRENTFILE_X:	// stored as char*: filled with filename of currently parsed text file
	//case TOK_LINENUM_X:		// stored as int: filled with line number of the currently parsed text file
	//case TOK_FLAGS_X:		// unsigned int: list of integers as parameter: result is the values OR'd together (0: 129: 5 => 133): can't have default value
	//case TOK_FUNCTIONCALL_X:	// StructFunctionCall**: parenthesis in input signals hierarchal organization
	//case TOK_POLYMORPH_X:	// YourStruct**: as TOK_STRUCT: but subtable points to tpi list of possible substructures
	//case TOK_STASHTABLE_X:	// StashTable
	//case TOK_MULTIVAL_X:		// A variant type used by the expression system
	//	// a "Command": which never does anything when parsing: but creates a magic button when UI is autogenerated
	//case TOK_COMMAND:
	default:
		return MULTI_NONE;
	}
}

//Get the nth item in the index.
static ObjectBTNode* objIndexDirectGet(ObjectIndex *oi, S64 n)
{
	ObjectBTNode *node = oi->root;
	ObjectBTNode *v = NULL;
	S64 ci = 0;
	int i;

	//Early bailout if we are beyond the end of the index.
	if (n >= oi->count || n < 0)
		return NULL;

	while (!nodeIsLeaf(node))
	{	//descend and count
		for (i = 0; i < eaSize(&node->kvs); i++)
		{
			v = node->kvs[i]->child;
			if (ci + v->count <= n)
			{
				ci += v->count;
			}
			else
			{
				break;
			}
		}
		if (!v)
			return NULL;

		node = v;
	}
	n -= ci;
	oi->currentIndex = n;

	//We found a node
	if (n < eaSize(&node->kvs))
	{
		return node;
	}
	else
		return NULL;
}

__forceinline static int nodeCompareHeaderElement(ObjectSortCol *col, ObjectIndexKey *key, ObjectIndexHeader *a, ObjectIndexHeader *b)
{
	int set = false;
	const char *lStr = NULL;
	U32 lInt = 0;

	const ObjectIndexHeader *lPtr = a;

	MultiValType headerFieldType = objGetHeaderFieldType(col->headerField);

	if(key)
	{
		switch(key->val.type)
		{
		xcase MULTI_STRING:
			lStr = key->val.str;
			set = true;
		xcase MULTI_INT:
			lInt = key->val.int32;
			set = true;
		xcase MULTI_NP_POINTER:
			lPtr = key->val.ptr;
		xdefault:	
			assertmsg(0, "unsupported key type");
		}
	}

	if(!set)
	{
		switch(headerFieldType)
		{
		xcase MULTI_STRING:
			lStr = objGetHeaderFieldStringVal(lPtr, col->headerField);
		xcase MULTI_INT:
			lInt = objGetHeaderFieldIntVal(lPtr, col->headerField);
		xdefault:	
			assertmsg(0, "unsupported header field type");
		}
	}

	switch(headerFieldType)
	{
	xcase MULTI_STRING:
	{
		const char *rStr = objGetHeaderFieldStringVal(b, col->headerField);
		return rStr ? stricmp(lStr, rStr) : 1;
	}
	xcase MULTI_INT:
		return lInt - objGetHeaderFieldIntVal(b, col->headerField);
	xdefault:	
		assertmsg(0, "unsupported header field type");
	}
}

__forceinline static int nodeCompareElement(ObjectIndex *oi, ObjectIndexKey *key, void *strptr, void *otherstr)
{
	ObjectIndexKey btkeystruct = {0};
	ObjectSortCol **cols = oi->columns;
	bool ok;
	int c;
	int i = 1;

	assert(key);

	if(oi->useHeaders)
	{
		c = nodeCompareHeaderElement(cols[0], key, strptr, otherstr);
	}
	else
	{
		if (!key->val.type)
		{
			assertmsg(strptr, "Element comparison cannot compare a NULL element (no key, no strptr).\n");
			key->str = strptr;
			key->val.type = MULTI_NP_POINTER;
			ok = ParserResolvePathComp(cols[0]->colPath, strptr, NULL, NULL, &key->val.ptr_noconst, NULL, NULL, 0);
			assertmsgf(ok, "Failed to resolve %s in index key comparison.\n", cols[0]->colPath->key->pathString);
		}

		ok = ParserResolvePathComp(cols[0]->colPath, otherstr, NULL, NULL, &btkeystruct.val.ptr_noconst, NULL, NULL, 0);
		assertmsgf(ok, "Failed to resolve %s in index node comparison.\n", cols[0]->colPath->key->pathString);

		c = cols[0]->comp(&cols[0]->context, &key->val.ptr, &btkeystruct.val.ptr);
	}

	//If we have inequality or there is no data to compare, just return the result.
	if (c != 0 || !(key->str && key->val.type == MULTI_NP_POINTER))
		return c;
	
	//Otherwise look at the secondary columns. (This is somewhat more expensive because we don't keep the key around.)
	while (c == 0 && i < eaSize(&cols))
	{
		ObjectSortCol *col = cols[i++];
		if(oi->useHeaders)
		{
			c = nodeCompareHeaderElement(col, key, strptr, otherstr);
		}
		else
		{
			void *kk = NULL;
			//left side
			ok = ParserResolvePathComp(col->colPath, key->str, NULL, NULL, &kk, NULL, NULL, 0);
			assertmsgf(ok, "Failed to resolve %s in index key comparison.\n", col->colPath->key->pathString);
			//right side
			ok = ParserResolvePathComp(col->colPath, otherstr, NULL, NULL, &btkeystruct.val.ptr_noconst, NULL, NULL, 0);
			assertmsgf(ok, "Failed to resolve %s in index node comparison.\n", col->colPath->key->pathString);
			//compare
			c = col->comp(&col->context, &kk, &btkeystruct.val.ptr);
		}
	}
	return c;
}


__forceinline static int nodeCompareKeys(ObjectSortCol **cols, ObjectIndexKey *key, ObjectIndexKey *otherkey)
{
	assert(key && otherkey && key->val.type && otherkey->val.type && key->val.type == otherkey->val.type);
	return cols[0]->comp(&cols[0]->context, &key->val.ptr, &otherkey->val.ptr);
}


//Descend internal nodes.
static ObjectBTNode* nodeFindBucket(ObjectIndex *oi, ObjectBTNode *node, ObjectIndexKey *key, void *strptr)
{
	assert(node->kvs);
	if (nodeIsLeaf(node))
	{
		if (strptr)
		{
			int c = 0;
			int i = 0;
			ObjectBTKV *kv = AllocObjectBTKV();
			kv->child = NULL;
			kv->key = strptr;

			if (oi->order > 4)
			{	//binary search
				int min = 0;
				int max = eaSize(&node->kvs);
				if (max)
				{
					i = max/2;
					while (max - min > 1)
					{
						c = nodeCompareElement(oi, key, strptr, node->kvs[i]->key);
						if (c > 0)
						{
							min = i;
							i = min + (max - min)/2;
						}
						else
						{
							max = i;
							i = min + (max - min)/2;
						}
					}
					i = min;
					c = nodeCompareElement(oi, key, strptr, node->kvs[i]->key);
					if (c > 0) i = max;
				}
			}
			else
			{
				for (i = 0; i < eaSize(&node->kvs); i++)
				{
					c = nodeCompareElement(oi, key, strptr, node->kvs[i]->key);
					if (c <= 0)
						break;
				}
			}
			oi->currentIndex += i;

			eaInsert(&node->kvs, kv, i);
			oi->lastinsert = i;
			node->count++;
		}
		return node;
	}
	else
	{
		int i = 0;
		int c = 0;
		ObjectBTNode *child;
		ObjectBTNode *found;

		if (oi->order > 4)
		{	//binary search
			int min = 0;
			int max = eaSize(&node->kvs);
			if (max)
			{
				i = max/2;
				while (max - min > 1)
				{
					c = nodeCompareElement(oi, key, strptr, node->kvs[i]->key);
					if (c > 0)
					{
						min = i;
						i = min + (max - min)/2;
					}
					else
					{
						max = i;
						i = min + (max - min)/2;
					}
				}
				i = min;
				if (max != eaSize(&node->kvs))
				{
					c = nodeCompareElement(oi, key, strptr, node->kvs[i]->key);
					if (c > 0) i = max;
				}
			}			
		}
		else
		{
			for (i = 0; i < eaSize(&node->kvs) - 1; i++)
			{
				c = nodeCompareElement(oi, key, strptr, node->kvs[i]->key);
				if (c <= 0)
					break;
			}
		}
		c = i;
		while (c--)
		{
			oi->currentIndex += node->kvs[c]->child->count;
		}

		child = node->kvs[i]->child;
		found = nodeFindBucket(oi, child, key, strptr);
		if (strptr)
		{
			ObjectBTKV *lastkv = NULL;

			if (eaSize(&child->kvs) > oi->order * 2)
			{	//split
				int insert = oi->order -1;
				ObjectBTNode *newnode = AllocObjectBTNode();
				ObjectBTKV *newkv = AllocObjectBTKV();

				newnode->next = child->next;	// nn-> n
				if (newnode->next)
					newnode->next->prev = newnode;	// nn <-n
				child->next = newnode;			// c-> nn
				newnode->prev = child;			// c <- nn
				newnode->kvs = 0;
				eaCreate(&newnode->kvs);
				eaSetCapacity(&newnode->kvs, oi->order * 2 + 1);
				eaSetSize(&newnode->kvs, oi->order);
				memmove(newnode->kvs, child->kvs + (oi->order + 1), sizeof(ObjectBTKV*)*oi->order);
				eaSetSize(&child->kvs, oi->order + 1);
				
				nodeRecount(newnode);
				nodeRecount(child);
				assert(newnode->count);
				assert(child->count);

				lastkv = eaTail(&newnode->kvs);
				assert(lastkv);
				newkv->key = lastkv->key;
				newkv->child = newnode;

				if (child == oi->last)
					oi->last = newnode;

				eaInsert(&node->kvs, newkv, i+1);

				lastkv = eaTail(&child->kvs);
				assert(lastkv);
				node->kvs[i]->key = lastkv->key;		
			}
			//if the child was the last in the current node, re-key the node
			else if (i+1 == eaSize(&node->kvs))
			{
				lastkv = eaTail(&child->kvs);
				assert(lastkv);
				node->kvs[i]->key = lastkv->key;
			}
			node->count++;
		}
		return found;
	}
}

//Find the bucket a specific element is in.
static ObjectBTNode* nodeSearchBucket(ObjectIndex *oi, ObjectBTNode *node, ObjectIndexKey *key, void *strptr, U32 options)
{
	bool bucketStart = !(options & SEARCHBUCKET_FINDNEXT);

	if (!node)
	{
		node = oi->root;
		oi->currentIndex = 0;
	}
	assert(node->kvs);
	if (nodeIsLeaf(node))
	{
		int i;
		int c = 0;
		//TODO: make a binary search mode
		for (i = 0; i < eaSize(&node->kvs); i++)
		{
			c = nodeCompareElement(oi, key, strptr, node->kvs[i]->key);
			if (c <= 0 && bucketStart)
				break;
			else if (c < 0 && !bucketStart)
				break;
			oi->currentIndex++;
		}

		if (bucketStart)
		{
			oi->lastinsert = i;
			if (i >= eaSize(&node->kvs))
				return NULL;
		}
		else
		{
			if (i == 0)
			{
				node = node->prev;
				if (node)
					oi->lastinsert = eaSize(&node->kvs);
			}
			else
			{
				oi->lastinsert = i;
			}
			oi->currentIndex--;
		}

		return node;
	}
	else
	{
		int i;
		int c = 0;
		//TODO: make a binary search mode
		for (i = 0; i < eaSize(&node->kvs) - 1; i++)
		{
			c = nodeCompareElement(oi, key, strptr, node->kvs[i]->key);
			if (c <= 0 && bucketStart)
				break;
			else if (c < 0 && !bucketStart)
				break;
			oi->currentIndex += node->kvs[i]->child->count;
		}
		return nodeSearchBucket(oi, node->kvs[i]->child, key, strptr, options);
	}
}

typedef enum {
	OIMATCH_CONTINUE = -1,
	OIMATCH_NOTFOUND = 0,
	OIMATCH_FOUND = 1,
	OIMATCH_REMOVED = 2
} OIExactMatch;

//Find an exact pointer match, return true if found.
// *if remove, remove it and zip up the tree.
static OIExactMatch nodeFindExactMatch(ObjectIndex *oi, ObjectBTNode *node, ObjectIndexKey *key, void *strptr, bool remove)
{
	if (nodeIsLeaf(node))
	{
		int i;
		int c = 0;
		//TODO: make a binary search mode
		for (i = 0; i < eaSize(&node->kvs); i++)
		{
			c = nodeCompareElement(oi, key, strptr, node->kvs[i]->key);

			if (c == 0)
			{	//found potential match
				if (strptr && key->val.type)
				{	//We need to check for exact match.
					if (strptr == node->kvs[i]->key)
					{
						if (remove)
						{
							ObjectBTKV *kv = eaRemove(&node->kvs, i);
							FreeObjectBTKV(kv);
							node->count--;
							return OIMATCH_REMOVED;
						}
						else
							return OIMATCH_FOUND;
					}
					//else, continue.
				}
				else
				{	//We've found the first (key or template) match.
					if (remove)
					{
						ObjectBTKV *kv = eaRemove(&node->kvs, i);
						FreeObjectBTKV(kv);
						node->count--;
						return OIMATCH_REMOVED;
					}
					else
					{
						return OIMATCH_FOUND;
					}
				}
			}
			else if (c < 0)
			{
				return OIMATCH_NOTFOUND;
			}
		}
		return OIMATCH_CONTINUE;
	}
	else
	{
		int i;
		int c = 0;
		ObjectBTNode *child = NULL;
		OIExactMatch found = OIMATCH_CONTINUE;
		//TODO: make a binary search mode
		for (i = 0; i < eaSize(&node->kvs); i++)
		{
			c = nodeCompareElement(oi, key, strptr, node->kvs[i]->key);
			if (c > 0)
			{
				continue;
			}
			else if (c <= 0 && found == OIMATCH_CONTINUE)
			{	//child may contain a match.
				child = node->kvs[i]->child;
				found = nodeFindExactMatch(oi, child, key, strptr, remove);

				//Child failed comparison
				if (found == OIMATCH_NOTFOUND) 
					return OIMATCH_NOTFOUND;
				//Child found match
				else if (found != OIMATCH_CONTINUE)
					break;
			}
			else
			{	//we've passed all possible indices.
				return OIMATCH_NOTFOUND;
			}
		}
		if (found == OIMATCH_CONTINUE)
			return found;

		if (remove)
		{
			//Fix up the child key, it may have changed.
			ObjectBTKV *childTailKV = eaTail(&child->kvs);
			node->kvs[i]->key = childTailKV->key;
			node->count--;
			if (child && found == OIMATCH_REMOVED)
			{

				if (eaSize(&child->kvs) < oi->order)
				{	//merge child nodes
					ObjectBTKV *left = (i > 0?node->kvs[i-1]:NULL);
					ObjectBTKV *right = (i < eaSize(&node->kvs) - 1?node->kvs[i+1]:NULL);
					U16 lcount = (left?eaSize(&left->child->kvs):0);
					U16 rcount = (right?eaSize(&right->child->kvs):0);

					if (lcount > rcount && lcount > oi->order)
					{	//borrow left
						ObjectBTKV *kv = eaPop(&left->child->kvs);
						U16 borrowsize = (kv->child?kv->child->count:1);
						eaInsert(&child->kvs, kv, 0);
						//reset left key;
						kv = eaTail(&left->child->kvs);
						assert(kv);
						left->key = kv->key;
						child->count += borrowsize;
						left->child->count -= borrowsize;
						found = OIMATCH_FOUND;
					}
					else if (rcount >= lcount && rcount > oi->order)
					{	//borrow from the right
						ObjectBTKV *kv = eaRemove(&right->child->kvs, 0);
						U16 borrowsize = (kv->child?kv->child->count:1);
						assert(kv);
						eaPush(&child->kvs, kv);
						//reset child key;
						node->kvs[i]->key = kv->key;
						child->count += borrowsize;
						right->child->count -= borrowsize;
						found = OIMATCH_FOUND;
					}
					else if (lcount > 0)
					{	//merge left (lcount should be oi->order)
						ObjectBTKV *kv;
						eaInsertEArray(&left->child->kvs, &child->kvs, eaSize(&left->child->kvs));
						left->child->count += child->count;
						if (oi->last == child)
							oi->last = child->prev;
						if (child->next)
							child->next->prev = child->prev;
						left->child->next = child->next;
						left->key = node->kvs[i]->key;
						kv = eaRemove(&node->kvs, i);
						assert(kv);
						eaDestroy(&child->kvs);
						FreeObjectBTNode(kv->child);
						FreeObjectBTKV(kv);
						found = OIMATCH_REMOVED;
					}
					else if (rcount > 0)
					{	//merge right (in the event that lcount == 0)
						ObjectBTKV *kv;
						eaInsertEArray(&right->child->kvs, &child->kvs, 0);
						right->child->count += child->count;
						if (oi->first == child)
							oi->first = child->next;
						if (child->prev)
							child->prev->next = child->next;
						right->child->prev = child->prev;
						//keys don't need to move because right absorbs children.
						kv = eaRemove(&node->kvs, i);
						assert(kv);
						eaDestroy(&child->kvs);
						FreeObjectBTNode(kv->child);
						FreeObjectBTKV(kv);
						found = OIMATCH_REMOVED;
					}
					else
					{
						assertmsg(false, "ObjectIndex: b+tree node underflow, something is fucked up.");
					}
				}
				else
				{	//Child removed, but we don't need to merge any nodes.
					found = OIMATCH_FOUND;
				}
			}
		}
		return found;
	}
}

//Descend and conditionally split the root node.
// if strptr is not NULL, insert it.
static ObjectBTNode* objIndexFindBucket(ObjectIndex *oi, ObjectIndexKey *key, void *strptr)
{
	bool ok = true;
	ObjectBTNode *found;
	ObjectBTNode *root = oi->root;
	oi->lastinsert = -1;
	oi->currentIndex = 0;
	found = nodeFindBucket(oi, oi->root, key, strptr);

	if (strptr)
	{
		//split the root if it grows too large.
		if (eaSize(&root->kvs) > oi->order * 2)
		{	//split root
			int insert = oi->order -1;
			ObjectBTNode *newnode = AllocObjectBTNode();
			ObjectBTKV *newkv = AllocObjectBTKV();
			ObjectBTKV *rootkv = AllocObjectBTKV();
			ObjectBTNode *newroot = AllocObjectBTNode();
			ObjectBTKV *lastkv;

			newnode->prev = root;
			newnode->next = root->next;
			root->next = newnode;
			newnode->kvs = 0;
			eaCreate(&newnode->kvs);
			eaSetCapacity(&newnode->kvs, oi->order * 2 + 1);
			eaSetSize(&newnode->kvs, oi->order);
			memmove(newnode->kvs, root->kvs + (oi->order + 1), sizeof(ObjectBTKV*)*oi->order);
			eaSetSize(&root->kvs, oi->order + 1);

			nodeRecount(root);
			nodeRecount(newnode);

			lastkv = eaTail(&newnode->kvs);
			assert(lastkv);
			newkv->key = lastkv->key;
			newkv->child = newnode;
			
			lastkv = eaTail(&root->kvs);
			assert(lastkv);
			rootkv->key = lastkv->key;
			rootkv->child = root;

			newroot->prev = newroot->next = NULL;
			newroot->kvs = 0;
			eaCreate(&newroot->kvs);
			eaSetCapacity(&newroot->kvs, oi->order * 2 + 1);
			eaPush(&newroot->kvs, rootkv);
			eaPush(&newroot->kvs, newkv);

			nodeRecount(newroot);

			if (oi->last == root)
				oi->last = newnode;
			
			oi->root = newroot;

			oi->depth++;
		}
		oi->count++;
	}

	return found;
}

bool objIndexFindExactMatch(ObjectIndex *oi, ObjectIndexKey *key, void *strptr, bool remove)
{
	OIExactMatch found;

	if (!oi->count) 
		return false;

	//We need key or strptr to find an element.
	assert( key && key->val.type || strptr );

	found = nodeFindExactMatch(oi, oi->root, key, strptr, remove);
	
	if (found == OIMATCH_CONTINUE || found == OIMATCH_NOTFOUND)
		return false;
	
	if (remove)
	{
		if (found == OIMATCH_REMOVED)
		{
			if (eaSize(&oi->root->kvs) == 1 && oi->depth > 1)
			{
				ObjectBTNode *root = oi->root;
				ObjectBTKV *kv = eaPop(&root->kvs);
				oi->root = kv->child;
				FreeObjectBTKV(kv);
				eaDestroy(&root->kvs);
				FreeObjectBTNode(root);
				oi->depth--;
			}
		}
		oi->count--;
	}
	return true;
}


//ObjectIndexes do not own indexed data they must be updated when objects are destroyed.
// *Insertion ASSUMES that the parsetable for this index is correct for strptr
bool objIndexInsert(ObjectIndex *oi, void *strptr)
{
	ObjectIndexKey key = {0};
	bool success = false;
	
	objIndexInitKey_Struct(oi, &key, strptr);
	if (!key.val.type)
	{
		objIndexDeinitKey_Struct(oi, &key);
		return success;
	}

	success = !!objIndexFindBucket(oi, &key, strptr);
	objIndexDeinitKey_Struct(oi, &key);
	return success;
}


//Searches for the object and, if found, removes it from the index.
// *Returns true if an object was removed.
// *If you need to UPDATE an object's KEY FIELD, you MUST remove it from the index first!
bool objIndexFind(ObjectIndex *oi, void *strptr, bool remove)
{
	ObjectIndexKey key = {0};
	bool success = false;
	if (!oi->count)
		return success;
	objIndexInitKey_Struct(oi, &key, strptr);
	if (key.val.type)
	{
		success = objIndexFindExactMatch(oi, &key, strptr, remove);
	}

	objIndexDeinitKey_Struct(oi, &key);
	return success;
}

//Get an object by indexed order.
// *if key is null, get the n'th element in the index.
// *if key is not null, get the n'th match.
// *returns true if a match is found and passed back in strfound.
bool objIndexGet(ObjectIndex *oi, ObjectIndexKey *key, S64 n, ObjectHeaderOrData **strfound)
{
	ObjectBTNode *node;
	bool ok = true;
	S64 start = 0;
	S64 end;
	int c, i;

	*strfound = NULL;

	if (key->val.type)
	{
		node = objIndexFindBucket(oi, key, NULL);
		start = oi->currentIndex;
		for (i = 0; i < eaSize(&node->kvs) - 1; i++)
		{
			c = nodeCompareElement(oi, key, NULL, node->kvs[i]->key);
			if (c == 0)
			{	//The 0th match.
				*strfound = node->kvs[i]->key;
				if (n == 0)
					return true;
			}
			else if (c < 0)
			{
				break;
			}
			start++;
		}
	}
	end = start + n;
	
	//get the nth item;
	node = objIndexDirectGet(oi, end);

	if (node)
	{
		assert(node->kvs);
		assert(node->kvs[oi->currentIndex]);
		*strfound = node->kvs[oi->currentIndex]->key;
		if (!*strfound)
			return false;
	}
	else
		return false;

	if (key->val.type)
	{	//see if an nth item with matching key exists.
		c = nodeCompareElement(oi, key, NULL, *strfound);
		return (c == 0);
	}
	else
		return true;
}

//Get the element with the minimum key.
ObjectHeaderOrData *objIndexGetFirst(ObjectIndex *oi)
{
	if (!oi->count)
		return NULL;
	return oi->first->kvs[0]->key;
}

//Get the element with the maximum key.
ObjectHeaderOrData *objIndexGetLast(ObjectIndex *oi)
{
	ObjectBTKV *kv;
	if (!oi->count)
		return NULL;
	kv = eaTail(&oi->last->kvs);
	assert(kv);
	return kv->key;
}

S64 objIndexCount(ObjectIndex *oi)
{
    if ( oi == NULL )
    {
        return 0;
    }
	//Locking here would introduce deadlock in the writes.
	return oi->count;
}


bool objIndexGetIterator(ObjectIndex *oi, ObjectIndexIterator *iter, IteratorDirection dir)
{
	if (!oi->count)
		return false;

	iter->oi = oi;
	iter->dir = dir;
	if (dir == ITERATE_FORWARD)
	{
		iter->leafNode = oi->first;
		iter->nodeIndex = 0;
	}
	else
	{
		iter->leafNode = oi->last;
		iter->nodeIndex = oi->last->count - 1;
	}

	return true;
}

//Start the iterator from a key offset.
// *node searching rules apply the same as objIndexGet.
// *ITERATE_REVERSE will cause the iterator to go to the last element with the key.
bool objIndexGetIteratorFrom(ObjectIndex *oi, ObjectIndexIterator *iter, IteratorDirection dir, ObjectIndexKey *key, S64 n)
{
	ObjectBTNode *node;
	S64 start = 0;
	S64 end;

	assert(key);

	PERFINFO_AUTO_START_FUNC();

	iter->dir = dir;
	iter->oi = oi;

	if (dir == ITERATE_FORWARD)
	{
		if (key->val.type)
		{
			node = nodeSearchBucket(oi, NULL, key, NULL, 0);
			start = oi->currentIndex;
		}
		end = start + n;
	}
	else	//ITERATE_REVERSE
	{
		if (key->val.type)
		{
			node = nodeSearchBucket(oi, NULL, key, NULL, SEARCHBUCKET_FINDNEXT);
			start = oi->currentIndex;
		}
		else
		{
			start = oi->count - 1;
		}
		end = start - n;
	}
	iter->leafNode = objIndexDirectGet(oi, end);
	iter->nodeIndex = oi->currentIndex;

	PERFINFO_AUTO_STOP();

	if (!iter->leafNode)
		return false;
	else
		return true;
}

bool objIndexFindLimit(ObjectIndex *oi, IteratorDirection dir, ObjectIndexKey *key, ObjectHeaderOrData **strfound)
{
	ObjectBTNode *node;
	int index;
	node = nodeSearchBucket(oi, NULL, key, NULL, dir);
	index = oi->lastinsert;

	if (dir == ITERATE_REVERSE) index--;

	if (index < 0 && node)
	{
		node = node->prev;
		if (node)
			index = eaSize(&node->kvs) - 1;
	}
	if (node)
	{
		*strfound = node->kvs[index]->key;
		return true;
	}
	else
		return false;
}

bool objIndexGetIteratorPast(ObjectIndex *oi, ObjectIndexIterator *iter, IteratorDirection dir, ObjectIndexKey *key, S64 n)
{
	assert(key);
	n++;
	//TODO: use objIndexFindLimit() to skip past keys.
	if (!objIndexGetIteratorFrom(oi, iter, (dir==ITERATE_FORWARD?ITERATE_REVERSE:ITERATE_FORWARD), key, 0))
		return false;
	while (n < 0)
	{
		if (!objIndexGetNext(iter))
			return false;
		n++;
	}
	objIndexReverseIterator(iter);
	while (n > 0)
	{
		if (!objIndexGetNext(iter))
			return false;
		n--;
	}
	return true;
}

//Start the iterator at a specific node.
// *strptr will be treated as a template (pointer equality need not match).
// *Returns true if a matching element was found.
bool objIndexGetIteratorAt(ObjectIndex *oi, ObjectIndexIterator *iter, IteratorDirection dir, void *strptr)
{
	ObjectIndexKey key = {0};
	
	objIndexInitKey_Struct(oi, &key, strptr);
	iter->dir = dir;
	iter->oi = oi;
	iter->leafNode = nodeSearchBucket(oi, NULL, &key, strptr, 0);
	iter->nodeIndex = oi->currentIndex;

	objIndexDeinitKey_Struct(oi, &key);
	if (!iter->leafNode)
		return false;
	else
		return true;
}

__forceinline void objIndexReverseIterator(ObjectIndexIterator *iter)
{
	iter->dir = (iter->dir == ITERATE_FORWARD?ITERATE_REVERSE:ITERATE_FORWARD);
}

ObjectHeaderOrData *objIndexGetNext(ObjectIndexIterator *iter)
{
	void *element = NULL;

	if (!iter->leafNode)
		return NULL;

	PERFINFO_AUTO_START_FUNC_L3();

	if (iter->dir == ITERATE_FORWARD)
	{
		while (iter->nodeIndex >= iter->leafNode->count)
		{
			iter->nodeIndex -= iter->leafNode->count;
			iter->leafNode = iter->leafNode->next;
			if (!iter->leafNode)
			{
				PERFINFO_AUTO_STOP_L3();
				return NULL;
			}
		}
		element = iter->leafNode->kvs[iter->nodeIndex]->key;
		iter->nodeIndex++;
	}
	else
	{
		while (iter->nodeIndex < 0)
		{
			iter->leafNode = iter->leafNode->prev;
			if (!iter->leafNode)
			{
				PERFINFO_AUTO_STOP_L3();
				return NULL;
			}
			iter->nodeIndex += iter->leafNode->count;
		}
		element = iter->leafNode->kvs[iter->nodeIndex]->key;
		iter->nodeIndex--;
	}
	PERFINFO_AUTO_STOP_L3();
	return element;
}

ObjectHeaderOrData *objIndexPeekNext(ObjectIndexIterator *iter)
{
	ObjectIndexIterator local_iter;

	memcpy(&local_iter, iter, sizeof(ObjectIndexIterator));

	return objIndexGetNext(&local_iter);
}

ObjectHeaderOrData *objIndexGetNextMatch(ObjectIndexIterator *iter, ObjectIndexKey *key, ObjectIndexMatch match)
{
	ObjectHeaderOrData *el = objIndexGetNext(iter);
	int c;
	if (el)
	{
		c = nodeCompareElement(iter->oi, key, NULL, el);
		switch (match)
		{	//Because we're comparing the next element to the key, these are backwards.
		case OIM_LTE:
			if (c >= 0) return el;
			return NULL;
		case OIM_LT:
			if (c > 0) return el;
			return NULL;
		case OIM_EQ:
			if (c == 0) return el;
			return NULL;
		case OIM_GT:
			if (c < 0) return el;
			return NULL;
		case OIM_GTE:
			if (c <= 0) return el;
			return NULL;
		}
	}
	return NULL;
}

//Copies elements into the earray while < key.
// *Does not include key.
S64 objIndexGetEArrayToKey(ObjectIndex *oi_UNUSED_, ObjectIndexIterator *iter, cEArrayHandle * handle, ObjectIndexKey *key)
{
	int c;
	void *el;
	ObjectHeaderOrData *limit;
	S64 resultCount = 0;

	assert(handle && key);

	el = objIndexGetNext(iter);
	c = nodeCompareElement(iter->oi, key, NULL, el);
	if (iter->dir == ITERATE_FORWARD && c <= 0)
		return 0;
	else if (iter->dir == ITERATE_REVERSE && c >= 0)
		return 0;

	if (!objIndexFindLimit(iter->oi, iter->dir, key, &limit))
		limit = 0;

	do
	{
		if (el == limit) break;
		eaPush(handle, el);
		resultCount++;
	}
	while (el = objIndexGetNext(iter));

	return resultCount;
}

//Copies elements into the earray while == key.
S64 objIndexGetEArrayWhileKey(ObjectIndex *oi_UNUSED_, ObjectIndexIterator *iter, cEArrayHandle * handle, ObjectIndexKey *key)
{
	//int c;
	void *el;
	ObjectHeaderOrData *limit;
	IteratorDirection dir = (iter->dir==ITERATE_FORWARD?ITERATE_REVERSE:ITERATE_FORWARD);
	S64 resultCount = 0;

	assert(handle && key);

	el = objIndexGetNext(iter);

	if (nodeCompareElement(iter->oi, key, NULL, el))
		return 0;

	if (!objIndexFindLimit(iter->oi, dir, key, &limit))
		return 0;

	do
	{
		eaPush(handle, el);
		resultCount++;
		if (el == limit) break;
	} while (el = objIndexGetNext(iter));

	return resultCount;
}

//Returns the number of elements matching the given key.
S64 objIndexCountKey(ObjectIndex *oi, ObjectIndexKey *key)
{
	ObjectBTNode *node;
	S64 start, end;

	node = nodeSearchBucket(oi, NULL, key, NULL, ITERATE_FORWARD);
	if (!node)
	{
		return 0;
	}
	start = oi->currentIndex;

	node = nodeSearchBucket(oi, NULL, key, NULL, ITERATE_REVERSE);
	end = oi->currentIndex + 1;

	return end - start;
}

//Returns the number of elements between the keys.
S64 objIndexCountRange(ObjectIndex *oi, ObjectIndexKey *from, ObjectIndexKey *to)
{
	ObjectBTNode *node;
	S64 start;
	S64 end;

	assert(oi && from && to);
	start = 0;
	end = oi->count;

	if (!from->val.ptr && !to->val.ptr)
		return 0;

	if (from->val.ptr)
	{
		node = nodeSearchBucket(oi, NULL, from, NULL, ITERATE_FORWARD);
		if (!node)
			start = -1;
		else
			start = oi->currentIndex;
	}


	if (to->val.ptr)
	{
		node = nodeSearchBucket(oi, NULL, to, NULL, ITERATE_REVERSE);
		if (node) 
		{
			end = oi->currentIndex + 1;
			start = 0;
		}
		else if (start == -1)
			end = -1;
	}

	return end - start;
}

//Verifies the order of the elements in the index.
bool objIndexVerifyOrder(ObjectIndex *oi)
{
	ObjectIndexIterator iter;
	S64 resultCount = 0;
	ObjectIndexHeader *element = NULL;
	ObjectIndexHeader *lastelement = NULL;

	if(!oi->useHeaders)
		return true;

	if(!objIndexGetIterator(oi, &iter, ITERATE_FORWARD))
		return false;

	while(element = (ObjectIndexHeader*)objIndexGetNext(&iter))
	{
		if(lastelement)
		{
			int i;
			int c = 0;
			for (i = 0; i < eaSize(&oi->columns) && c == 0; ++i)
			{
				c = nodeCompareHeaderElement(oi->columns[i], NULL, lastelement, element);
			}

			if(c > 0)
			{
				AssertOrAlert("OBJECTINDEXVERIFY", "Index out of order.");
				return false;
			}
		}

		lastelement = element;
	}

	return true;
}

//Copies count elements into the earray from the iterator.
S64 objIndexGetEArrayCount(ObjectIndex *oi_UNUSED_, ObjectIndexIterator *iter, cEArrayHandle * handle, S64 count)
{
	S64 resultCount = 0;
	void *el;

	if (count <= 0) count = _I64_MAX;

	while (resultCount < count && (el = objIndexGetNext(iter)))
	{
		eaPush(handle, el);
		resultCount++;
	}

	return resultCount;
}
//Copies elements into the earray
S64 objIndexCopyEArrayRange(ObjectIndex *oi, cEArrayHandle * handle, S64 start, S64 count)
{
	ObjectIndexIterator iter;
	ObjectIndexKey *key = NULL;
	S64 resultCount = 0;
	void *el;
	bool ok = true;

	assert(handle);

	if (!count) count = _I64_MAX;

	if (start == 0)
		ok = objIndexGetIterator(oi, &iter, ITERATE_FORWARD);
	else
		ok = objIndexGetIteratorFrom(oi, &iter, ITERATE_FORWARD, key, start);
	if (!ok)
		return 0;
		
	while (resultCount < count && (el = objIndexGetNext(&iter)))
	{
		eaPush(handle, el);
		resultCount++;
	}

	return resultCount;
}

S64 objIndexCopyEArrayRangeFrom(ObjectIndex *oi, cEArrayHandle *handle, ObjectIndexKey *key, S64 n, IteratorDirection dir, S64 count)
{
	ObjectIndexIterator iter;
	S64 resultCount = 0;
	void *el;
	bool ok = true;

	assert(handle && key);
	
	if (!count) count = _I64_MAX;

	if (!objIndexGetIteratorFrom(oi, &iter, dir, key, n))
		return 0;

	while (resultCount < count && (el = objIndexGetNext(&iter)))
	{
		eaPush(handle, el);
		resultCount++;
	}

	return resultCount;
}

S64 objIndexCopyEArrayRangeFromTo(ObjectIndex *oi, cEArrayHandle *handle, ObjectIndexKey *key, S64 n, ObjectIndexKey *end)
{
	ObjectIndexIterator iter;
	S64 resultCount = 0;
	int c;
	IteratorDirection dir = ITERATE_FORWARD;

	assert(handle && key);
	
	PERFINFO_AUTO_START_FUNC();

	c = nodeCompareKeys(oi->columns, key, end);

	if (c > 0) 
		dir = ITERATE_REVERSE;
	else if (c == 0)
		assertmsg(false, "Cannot Range from identical keys.\n");
	
	if (!objIndexGetIteratorFrom(oi, &iter, dir, key, n))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	resultCount = objIndexGetEArrayToKey(oi, &iter, handle, end);
	
	PERFINFO_AUTO_STOP();

	return resultCount;
}

// We must lock the index outside of this function, since returns pointers to data inside the index.
// In a container store at least, these pointers can potentially be freed while the earray exists.
S64 objIndexCopyEArrayOfKey(ObjectIndex *oi, cEArrayHandle *handle, ObjectIndexKey *key, bool reverse)
{
	ObjectIndexIterator iter = {0};
	S64 resultCount = 0;
	IteratorDirection dir = (reverse?ITERATE_REVERSE:ITERATE_FORWARD);

	PERFINFO_AUTO_START_FUNC();

	assert(handle && key);
	
	if (!objIndexGetIteratorFrom(oi, &iter, dir, key,0))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	resultCount = objIndexGetEArrayWhileKey(NULL, &iter, handle, key);

	PERFINFO_AUTO_STOP();
	return resultCount;
}

// ObjectIndex Tests

void printObjectIndexKey(ObjectIndex *oi, void *strptr, char **estr)
{
	int i;
	char buf[1024];
	for (i = 0; i < eaSize(&oi->columns); i++)
	{
		if (i) estrConcatf(estr, "<");

		if(oi->useHeaders)
		{
			MultiValType eType = objGetHeaderFieldType(oi->columns[i]->headerField);
			switch(eType)
			{
			case MULTI_INT:
				estrConcatf(estr, "%d", objGetHeaderFieldIntVal(strptr, oi->columns[i]->headerField));
				break;
			case MULTI_STRING:
				estrConcatf(estr, "%s", objGetHeaderFieldStringVal(strptr, oi->columns[i]->headerField));
				break;
			}
		}
		else
		{
			objPathGetString(oi->columns[i]->colPath->key->pathString, oi->columns[i]->colPath->key->rootTpi, strptr, buf, 1024);
			estrConcatf(estr,"%s", buf);
		}

		if (i) estrConcatf(estr, ">");
	}
}

void printObjectIndexNode(ObjectIndex *oi, ObjectBTNode *node, int depth, char **estr)
{
	ObjectBTKV *kv;
	int i;
	if (depth == 0)
	{
		estrConcatf(estr, " %"FORM_LL"d", node->count);
		if (nodeIsLeaf(node))
			estrConcatf(estr, "(");
		else
			estrConcatf(estr, "[");
		if (node == oi->first)
			estrConcatf(estr, "!");
	}
	for (i = 0; i < eaSize(&node->kvs); i++)
	{
		kv = node->kvs[i];
		if (depth == 0)
		{
			if (i) estrConcatf(estr, " ");
			printObjectIndexKey(oi, kv->key, estr);
		}
		else
		{
			printObjectIndexNode(oi, kv->child, depth - 1, estr);
		}
	}
	if (depth == 0)
	{
		if (node == oi->last)
			estrConcatf(estr, "!");
		if (nodeIsLeaf(node))
			estrConcatf(estr, ")");
		else
			estrConcatf(estr, "]");
	}
}

void printObjectIndexStructure(ObjectIndex *oi, char **estr)
{
	int i;
	for (i = 0; i < oi->depth; i++)
	{
		printObjectIndexNode(oi, oi->root, i, estr);	
		estrConcatf(estr, "\n");
	}
	estrConcatf(estr, "first:");
	printObjectIndexNode(oi, oi->first, 0, estr);
	
	estrConcatf(estr, "\nlast:");
	printObjectIndexNode(oi, oi->last, 0, estr);
	estrConcatf(estr, "\n");
}

#ifdef TEST_OBJECT_INDEX


char* printObjectIndexElement(ObjectIndex *oi, S64 n)
{
	static char buf[1024];
	ObjectIndexKey key = {0};
	void *str;
	if (objIndexGet(oi, &key, n, &str))
	{
		char *estr = 0;
		estrStackCreate(&estr);
		printObjectIndexKey(oi, str, &estr);
		sprintf(buf, "%s", estr);
		estrDestroy(&estr);
	}
	else
	{
		sprintf(buf, "Could not get element %d.", n);
	}
	return buf;
}


#endif

AUTO_STRUCT;
typedef struct OITestSubElement {
	char *pName; AST(ESTRING)
	int i; AST(KEY)
} OITestSubElement;


AUTO_STRUCT;
typedef struct OITestElement {
	char *pName; AST(ESTRING)
	int id; AST(KEY)
	int classnum;
	OITestSubElement *sub;
} OITestElement;

#ifdef TEST_OBJECT_INDEX

#include "autogen/objIndex_c_ast.h"
#include "rand.h"

#endif


AUTO_RUN;
void testObjectIndexDeletion(void)
#ifdef TEST_OBJECT_INDEX
{
	ObjectIndex *oi = objIndexCreateWithStringPaths(0, 0, parse_OITestElement, ".classnum", ".id", NULL);
	U32 flip = 0;
	int nextid = 0;
	int i;

	for (i = 0; i < 65535; i++)
	{
		OITestElement *el = StructCreate(parse_OITestElement);
		el->id = ++nextid;
		el->classnum = randomIntRange(0,1023);
		objIndexInsert(oi, el);
	}

	printf("Testing objIndex deletion.\n");

	while (true)
	{
		ObjectIndexIterator iter = {0};
		OITestElement *el = NULL;
		i = 0;

		if (flip++ % 2)
		{
			ObjectIndexKey key = {0};
			objIndexGet(oi, &key, randomIntRange(0,oi->count - 1), &el);
			printf(" Removing id: %u   ", el->id);
			assert (el);
			objIndexRemove(oi, el);
		}
		else
		{
			el = StructCreate(parse_OITestElement);
			el->id = ++nextid;
			el->classnum = randomIntRange(0,1023);
			objIndexInsert(oi, el);
		}

		objIndexGetIterator(oi, &iter, ITERATE_FORWARD);

		el = NULL;
		while (el = objIndexGetNext(&iter))
		{
			if (el->classnum != i)
			{
				OITestElement **arry = NULL;
				ObjectIndexKey key = {0};

				i = el->classnum;
				objIndexInitKey_Int(oi, &key, i);
				objIndexCopyEArrayOfKey(oi, &arry, &key, false);
				objIndexDeinitKey_Int(oi, &key);

				assert(eaSize(&arry));
				assert(el == arry[0]);

				eaDestroy(&arry);
			}
		}

		printf("\rflip = %u", flip);
	}
}
#else
{}
#endif

AUTO_RUN;
void testObjectIndex(void)
#ifdef TEST_OBJECT_INDEX
{	
	ObjectIndex *oi = objIndexCreateWithStringPaths(0, 0, parse_OITestElement, ".name", ".id", NULL);
	ObjectIndex *oi2 = objIndexCreateWithStringPath(0, 0, parse_OITestElement, ".sub.i");
	ObjectIndex *oi3 = objIndexCreateWithStringPath(0, 0, parse_OITestElement, ".sub.name");
	int i;
	int max = 128;
	OITestElement *el;
	char *estr = 0;
	estrStackCreate(&estr);

	{
		OITestElement *found;
		ObjectIndexKey key = {0};

		objIndexInitKey_String(oi, &key, "3");
		if (objIndexGet(oi, &key, 0, &found))
		{
			printf("Get is failing to fail.\n");
		}

		objIndexDeinitKey_String(oi, &key);
	}

	for (i = 0; i < max; i++)
	{
		int v = randomIntRange(0,9);
		el = StructCreate(parse_OITestElement);
		estrPrintf(&el->pName, "%d", v);
		el->id = i;
		
		el->sub = StructCreate(parse_OITestSubElement);
		el->sub->i = max - i;
		estrPrintf(&el->sub->pName, "%x", max - i);

		objIndexInsert(oi, el);

		estrClear(&estr);
		printObjectIndexStructure(oi, &estr);
		printf("oi\n%s\n\n", estr);


		objIndexInsert(oi2, el);
		objIndexInsert(oi3, el);
	}
	printf("Indexes built.\n");

	printf("Smoke test:\n");
	{
		estrClear(&estr);
		printObjectIndexStructure(oi, &estr);
		printf("oi\n%s\n\n", estr);
		if (oi->count != max || oi->count != oi->root->count);

		estrClear(&estr);
		printObjectIndexStructure(oi2, &estr);
		printf("oi2\n%s\n\n", estr);
		if (oi2->count != max || oi2->count != oi2->root->count);

		estrClear(&estr);
		printObjectIndexStructure(oi3, &estr);
		printf("oi3\n%s\n\n", estr);
		if (oi3->count != max || oi3->count != oi3->root->count);
	}

	printf("Testing Iteration.\n");
	{
		ObjectIndexIterator iter;
		ObjectIndexKey key = {0};
		OITestElement *prev = NULL;
		int c;
		char buf[1024];
		int count = 0;

		estrClear(&estr);

		objIndexGetIterator(oi2, &iter, ITERATE_FORWARD);
		while (el = objIndexGetNext(&iter))
		{
			if (prev)
			{
				c = oi2->columns[0]->comp(&oi2->columns[0]->context, (const void**)&el, (const void**)&prev);
				assertmsg(c <= 0, "Index order failed during iteration!");
			}
			objPathGetString(oi2->columns[0]->colPath->key->pathString, oi2->columns[0]->colPath->key->rootTpi, el, buf, 1024);
			estrConcatf(&estr, "%s ", buf);
			prev = el;
			count++;
		}
		if (count == max)
			printf("Forward Iterate pass: %s\n\n", estr);
		else
			printf("Forward Iterate fail: %s\n\n", estr);
		estrClear(&estr);

		count = 0;
		prev = NULL;
		objIndexGetIterator(oi2, &iter, ITERATE_REVERSE);
		while (el = objIndexGetNext(&iter))
		{
			if (prev)
			{
				c = oi2->columns[0]->comp(&oi2->columns[0]->context, (const void**)&el, (const void**)&prev);
				assertmsg(c >= 0, "Index order failed during iteration!");
			}
			objPathGetString(oi2->columns[0]->colPath->key->pathString, oi2->columns[0]->colPath->key->rootTpi, el, buf, 1024);
			estrConcatf(&estr, "%s ", buf);
			prev = el;
			count++;
		}

		if (count == max)
			printf("Reverse Iterate pass: %s\n\n", estr);
		else
			printf("Reverse Iterate fail: %s\n\n", estr);
		estrClear(&estr);


		printf("\n\nTesting offset iteration.");
		count = 0;
		for (i = 0; i < 10; i++)
		{
			char buf2[2];
			char *estr2 = 0;
			sprintf(buf2, "%d", i);
			printf("\n\n\nIterating %d's:\n", i);
			
			estrStackCreate(&estr2);
			//Get the iterator.
			objIndexInitKey_String(oi, &key, buf2);
			objIndexGetIteratorFrom(oi, &iter, ITERATE_FORWARD, &key, 0);
			objIndexDeinitKey_String(oi, &key);

			//Iterate
			while (el = objIndexGetNext(&iter))
			{
				estrClear(&estr2);
				printObjectIndexKey(oi, el, &estr2);
				if (estr2[0] != buf2[0])
					break;
				estrConcatf(&estr, "%s ", estr2);
				count++;
			}

			estrDestroy(&estr2);
			printf("%s", estr);
			estrClear(&estr);
		}
		assert(count == max);

		count = 0;
		for (i = 9; i >= 0; i--)
		{
			char buf2[2];
			char *estr2 = 0;
			sprintf(buf2, "%d", i);
			printf("\n\n\nIterating %d's:\n", i);
			
			estrStackCreate(&estr2);
			//Get the iterator.
			objIndexInitKey_String(oi, &key, buf2);
			objIndexGetIteratorFrom(oi, &iter, ITERATE_REVERSE, &key, 0);
			objIndexDeinitKey_String(oi, &key);

			//Iterate
			while (el = objIndexGetNext(&iter))
			{
				estrClear(&estr2);
				printObjectIndexKey(oi, el, &estr2);
				if (estr2[0] != buf2[0])
					break;
				estrConcatf(&estr, "%s ", estr2);
				count++;
			}

			estrDestroy(&estr2);
			printf("%s", estr);
			estrClear(&estr);
		}
		assert(count == max);
	}

	printf("\n\nTesting index search.\n");
	{
		OITestElement *found;
		ObjectIndexKey key = {0};
		
		objIndexInitKey_String(oi, &key, "3");
		if (objIndexGet(oi, &key, 1, &found))
		{
			printObjectIndexKey(oi, found, &estr);
			printf("found element: %s\n", estr);
		}
		else
		{
			printf("failed to find element.\n");
		}
		objIndexDeinitKey_String(oi, &key);

		estrClear(&estr);
		objIndexInitKey_String(oi, &key, "3+");
		if (objIndexGet(oi, &key, 1, &found))
		{
			printf("failed to not find element.\n");
		}
		else
		{
			printObjectIndexKey(oi, found, &estr);
			printf("next element: %s\n", estr);
		}
		objIndexDeinitKey_String(oi, &key);

	}

	printf("\n\nTesting EArray copies.\n");
	{
		OITestElement **ppEls = 0;
		ObjectIndexKey start = {0}, end = {0};

		eaCreate(&ppEls);
		objIndexCopyEArrayRange(oi2, &ppEls, max/4, max/2);
		
		for (i = 0; i < eaSize(&ppEls); i++)
		{
			estrClear(&estr);
			printObjectIndexKey(oi2, ppEls[i], &estr);
			printf("Copied element:%s\n", estr);
		}
		if (!i)
			printf("Earray copy failed.\n\n");

		eaClear(&ppEls);

		//forward
		printf("\n\nRange FromTo: [%s,%s)\n", "2", "5");
		objIndexInitKey_String(oi, &start, "2");
		objIndexInitKey_String(oi, &end, "5");
		objIndexCopyEArrayRangeFromTo(oi, &ppEls, &start, 0, &end);
		objIndexDeinitKey_String(oi, &start);
		objIndexDeinitKey_String(oi, &end);

		for (i = 0; i < eaSize(&ppEls); i++)
		{
			estrClear(&estr);
			printObjectIndexKey(oi, ppEls[i], &estr);
			printf("Copied element:%s\n", estr);
		}
		if (!i)
			printf("Earray copy failed.\n\n");

		eaClear(&ppEls);

		//reverse
		printf("\n\nRange FromTo: [%s,%s)\n", "5", "2");
		objIndexInitKey_String(oi, &start, "5");
		objIndexInitKey_String(oi, &end, "2");
		objIndexCopyEArrayRangeFromTo(oi, &ppEls, &start, 0, &end);
		objIndexDeinitKey_String(oi, &start);
		objIndexDeinitKey_String(oi, &end);

		for (i = 0; i < eaSize(&ppEls); i++)
		{
			estrClear(&estr);
			printObjectIndexKey(oi, ppEls[i], &estr);
			printf("Copied element:%s\n", estr);
		}
		if (!i)
			printf("Earray copy failed.\n\n");

		eaDestroy(&ppEls);
	}


	printf("Testing deletion.\n");
	{
		ObjectIndexIterator iter;
		OITestElement **ppDeletions = 0;
		eaCreate(&ppDeletions);

		objIndexGetIterator(oi, &iter, ITERATE_FORWARD);
		i = 0;
		while (el = objIndexGetNext(&iter))
		{
			if (i++ % 3 == 0) eaPush(&ppDeletions, el);
		}
		
		while (eaSize(&ppDeletions) > 0)
		{
			S64 count = objIndexCount(oi);

			el = eaPop(&ppDeletions);
			objIndexRemove(oi, el);
			
			assert(count == objIndexCount(oi) + 1);
			
			//estrClear(&estr);
			//printObjectIndexStructure(oi, &estr);
			//printf("Deleted %s new state:\n%s\n\n", el->pName, estr);

			StructDestroy(parse_OITestElement, el);
		}

		eaDestroy(&ppDeletions);

		while (objIndexCount(oi))
		{
			el = objIndexGetFirst(oi);
			objIndexRemove(oi, el);

			if (objIndexCount(oi) < oi->order * 2 + 1)
			{
				estrClear(&estr);
				printObjectIndexStructure(oi, &estr);
				printf("Deleted %s new state:\n%s\n", el->pName, estr);
			}

			StructDestroy (parse_OITestElement, el);
		}
		estrClear(&estr);
		printObjectIndexStructure(oi, &estr);
		printf("oi should be empty. state:\n%s\n\n", estr);
	}
	
	estrDestroy(&estr);
}
#else
{}
#endif

void DeinitObjectIndexHeader(ObjectIndexHeader *header)
{
	if (header)
	{
		ObjectIndexHeader_NoConst *header_NoConst = (ObjectIndexHeader_NoConst *)header;
		SAFE_FREE(header_NoConst->privAccountName);
		SAFE_FREE(header_NoConst->pubAccountName);
		SAFE_FREE(header_NoConst->savedName);
		SAFE_FREE(header_NoConst->extraData1);
		SAFE_FREE(header_NoConst->extraData2);
		SAFE_FREE(header_NoConst->extraData3);
		SAFE_FREE(header_NoConst->extraData4);
		SAFE_FREE(header_NoConst->extraData5);
		ZeroStruct(header);
	}
}

void DestroyObjectIndexHeader(ObjectIndexHeader **header)
{
	if (header && *header)
	{
		ObjectIndexHeader_NoConst *header_NoConst = (ObjectIndexHeader_NoConst *)*header;
		SAFE_FREE(header_NoConst->privAccountName);
		SAFE_FREE(header_NoConst->pubAccountName);
		SAFE_FREE(header_NoConst->savedName);
		SAFE_FREE(header_NoConst->extraData1);
		SAFE_FREE(header_NoConst->extraData2);
		SAFE_FREE(header_NoConst->extraData3);
		SAFE_FREE(header_NoConst->extraData4);
		SAFE_FREE(header_NoConst->extraData5);
		SAFE_FREE(*header);
	}
}

#include "autogen/objIndex_c_ast.c"
