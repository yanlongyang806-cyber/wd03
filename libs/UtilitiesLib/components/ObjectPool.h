/***************************************************************************



***************************************************************************/

#pragma once

typedef struct ObjectPool ObjectPool;
typedef void (*FreeFunction)(void * object);
typedef void (*DestroyFunction)(void * object);
typedef void *(*AllocFunction)(void);
typedef size_t (*SizeFunction)(void *object);

ObjectPool *ObjectPoolCreate(size_t maxSize, size_t removeSize, AllocFunction allocFunction, SizeFunction sizeFunction, FreeFunction freeFunction, DestroyFunction destroyFunction);
void *ObjectPoolAlloc(ObjectPool *pool);
void ObjectPoolFree(ObjectPool *pool, void *object);
void ObjectPoolTick(ObjectPool *pool);

size_t ObjectPoolGetNumberOfObjects(ObjectPool *pool);
size_t ObjectPoolGetTotalSize(ObjectPool *pool);

void ObjectPoolSetMaxSize(ObjectPool *pool, size_t maxSize);
size_t ObjectPoolGetMaxSize(ObjectPool *pool, size_t maxSize);
void ObjectPoolSetRemoveSize(ObjectPool *pool, size_t removeSize);
size_t ObjectPoolGetRemoveSize(ObjectPool *pool, size_t removeSize);

ObjectPool *CreateEstrObjectPool(size_t maxSize, size_t removeSize);
