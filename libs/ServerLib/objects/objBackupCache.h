#pragma once

typedef struct Container Container;
typedef enum GlobalType GlobalType;

AUTO_ENUM;
typedef enum BackupCacheType
{
	BACKUPCACHE_STASH = BIT(0), // a simple stash table cache; default
	BACKUPCACHE_LRU = BIT(1), // LRU cache
} BackupCacheType;

void LRUCacheSetMissLogFile (const char *path);
void LRUCache_EnableMissDetails(bool bEnable);
void * BackupCacheGet(SA_PARAM_NN_VALID Container *con);

// Currently only used for printing LRU Cache Misses per Hour
void BackupCacheOncePerFrame(void);

// Register the GlobalType to use the specified BackupCacheType.
// If the type is already registered, then the existing cache is converted to the new type.
// Current conversions supported: LRU to Stash, Stash to LRU (destroys all cache data)
// The meaning of the size parameter varies for different cache types:
//   LRU = cache size, Stash = initial stash table size
void BackupCache_RegisterType(GlobalType eType, BackupCacheType eCacheType, int iSize);
