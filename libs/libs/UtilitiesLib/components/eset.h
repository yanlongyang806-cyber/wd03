// eset.h - Provides a simple hash set, with low memory overhead

#ifndef ESET_H
#define ESET_H
GCC_SYSTEM

C_DECLARATIONS_BEGIN

typedef struct ESetImp* ESet;
typedef const struct ESetImp* cESet;
typedef ESet* ESetHandle; 
typedef const cESet* cESetHandle; 

// Creates a set by passing in a handle. 0 means default size
void eSetCreate_dbg(ESetHandle handle, int capacity MEM_DBG_PARMS);
#define eSetCreate(handle,size) eSetCreate_dbg(handle,size MEM_DBG_PARMS_INIT)

// Destroys a set and sets handle to 0
void eSetDestroy(ESetHandle handle);

// Clears all values in set
void eSetClear(ESetHandle handle);

// Returns number of things in set
U32	eSetGetCount(cESetHandle handle);

// Returns number of storage slots
U32	eSetGetMaxSize(cESetHandle handle);

// Returns value at index if valid, or NULL if not. This is the simplest way of iterating
void *eSetGetValueAtIndex(cESetHandle handle, U32 index);

// Adds a value to the set. Return false if it's already there or can't be added
bool eSetAdd_dbg(ESetHandle handle, const void *pSearch MEM_DBG_PARMS);
#define eSetAdd(handle,pSearch) eSetAdd_dbg(handle,pSearch MEM_DBG_PARMS_INIT)

// Removes a value from the set. Returns true if successfully removed
bool eSetRemove(ESetHandle handle, const void *pSearch);

// Returns true if the specified value was found in the set
bool eSetFind(cESetHandle handle, const void *pSearch);

C_DECLARATIONS_END

#endif // ESET_H
