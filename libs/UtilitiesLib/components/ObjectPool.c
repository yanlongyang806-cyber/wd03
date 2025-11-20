#include "ObjectPool.h"
#include "earray.h"
#include "winInclude.h"
#include "timing_profiler.h"
#include "estring.h"

typedef struct ObjectPool
{
	void **freeArray;
	size_t totalSize;
	size_t maxSize;
	size_t removeSize;
	FreeFunction freeFunction;
	DestroyFunction destroyFunction;
	AllocFunction allocFunction;
	SizeFunction sizeFunction;
	CRITICAL_SECTION cs;
} ObjectPool;

ObjectPool *ObjectPoolCreate(size_t maxSize, size_t removeSize, AllocFunction allocFunction, SizeFunction sizeFunction, FreeFunction freeFunction, DestroyFunction destroyFunction)
{
	ObjectPool *pool;
	PERFINFO_AUTO_START_FUNC();
	assert(freeFunction && allocFunction && sizeFunction && destroyFunction);

	pool = malloc(sizeof(ObjectPool));
	pool->freeFunction = freeFunction;
	pool->allocFunction = allocFunction;
	pool->sizeFunction = sizeFunction;
	pool->destroyFunction = destroyFunction;
	pool->freeArray = NULL;
	pool->totalSize = 0;
	pool->maxSize = maxSize;
	pool->removeSize = removeSize;

	InitializeCriticalSection(&pool->cs);
	PERFINFO_AUTO_STOP();
	return pool;
}

static void *ObjectPoolGetObject(ObjectPool *pool)
{
	void *object = NULL;
	//Find an available entry. If there isn't one, allocate a new one
	EnterCriticalSection(&pool->cs);
	if(eaSize(&pool->freeArray))
	{
		object = eaPop(&pool->freeArray);
		pool->totalSize -= pool->sizeFunction(object);
	}
	LeaveCriticalSection(&pool->cs);

	return object;
}

void *ObjectPoolAlloc(ObjectPool *pool)
{
	void *object = NULL;
	PERFINFO_AUTO_START_FUNC();
	object = ObjectPoolGetObject(pool);

	if(!object)
		object = pool->allocFunction();
	PERFINFO_AUTO_STOP();
	return object;
}

void ObjectPoolFree(ObjectPool *pool, void *object)
{
	PERFINFO_AUTO_START_FUNC();
	pool->freeFunction(object);
	EnterCriticalSection(&pool->cs);
	eaPush(&pool->freeArray, object);
	pool->totalSize += pool->sizeFunction(object);
	LeaveCriticalSection(&pool->cs);
	PERFINFO_AUTO_STOP();
}

void ObjectPoolTick(ObjectPool *pool)
{
	size_t removed = 0;
	void **objectsToDestroy = NULL;
	U32 count = 0;

	if(!pool || !pool->maxSize)
		return;

	EnterCriticalSection(&pool->cs);
	if(!pool->removeSize)
		pool->removeSize = 1;

	while(eaSize(&pool->freeArray) && pool->totalSize > pool->maxSize && removed < pool->removeSize)
	{
		void *object = pool->freeArray[count];
		size_t size = pool->sizeFunction(object);
		pool->totalSize -= size;
		removed += size;
		eaPush(&objectsToDestroy, object);
		count++;
	}
	eaRemoveRange(&pool->freeArray, 0, count);
	LeaveCriticalSection(&pool->cs);

	while(eaSize(&objectsToDestroy))
	{
		void *object = eaPop(&objectsToDestroy);
		pool->destroyFunction(object);
	}

	eaDestroy(&objectsToDestroy);
}

void ObjectPoolSetMaxSize(ObjectPool *pool, size_t maxSize)
{
	if(!pool)
		return;

	EnterCriticalSection(&pool->cs);
	pool->maxSize = maxSize;
	LeaveCriticalSection(&pool->cs);
}

size_t ObjectPoolGetMaxSize(ObjectPool *pool, size_t maxSize)
{
	if(!pool)
		return 0;

	return pool->maxSize;
}

void ObjectPoolSetRemoveSize(ObjectPool *pool, size_t removeSize)
{
	if(!pool)
		return;

	EnterCriticalSection(&pool->cs);
	pool->removeSize = removeSize;
	LeaveCriticalSection(&pool->cs);
}

size_t ObjectPoolGetRemoveSize(ObjectPool *pool, size_t removeSize)
{
	if(!pool)
		return 0;

	return pool->removeSize;
}

size_t ObjectPoolGetNumberOfObjects(ObjectPool *pool)
{
	size_t retval;
	if(!pool)
		return 0;

	EnterCriticalSection(&pool->cs);
	retval = eaSize(&pool->freeArray);
	LeaveCriticalSection(&pool->cs);
	return retval;
}

size_t ObjectPoolGetTotalSize(ObjectPool *pool)
{
	if(!pool)
		return 0;

	return pool->totalSize;
}

char *opEstrCreate(void)
{
	char *str = NULL;
	estrCreate(&str);
	return str;
}

void opEstrFree(char *str)
{
	estrClear(&str);
}

void opEstrDestroy(char *str)
{
	estrDestroy(&str);
}

size_t opEstrSize(const char *str)
{
	return estrGetCapacity(&str);
}

ObjectPool *CreateEstrObjectPool(size_t maxSize, size_t removeSize)
{
	return ObjectPoolCreate(maxSize, removeSize, opEstrCreate, opEstrSize, opEstrFree, opEstrDestroy);
}
