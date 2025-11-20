#include "LoginCommon.h"

#include "error.h"
#include "GameAccountData\GameAccountData.h"
#include "AccountStub.h"
#include "GlobalTypes.h"
#include "Microtransactions.h"
#include "objPath.h"
#include "StringCache.h"

#include "AutoGen/GameAccountData_h_ast.h"
#include "AutoGen/LoginCommon_h_ast.h"

DefineContext *g_pUnlockedAllegianceFlags = NULL;
DefineContext *g_pUnlockedCreateFlags = NULL;

char *GetLoginBeganKeyName(const char *pCharName)
{
	static char *pRet = NULL;

	estrPrintf(&pRet, "LoginBegan_%s", pCharName);

	return pRet;
}

//
// Add character slots, with or without restrictions, to the AvailableCharSlots struct.
//
void
CharSlots_AddSlots(AvailableCharSlots *availableSlots, int numSlots, CharSlotRestrictFlag flags, U32 virtualShardID, char *allegianceName)
{
    const char *pooledAllegianceName = allocAddString(allegianceName);
    int i;
    bool found = false;
    CharSlotRestriction *newRestriction;

    // If simplified multi-shard character slot rules are in place, only virtual shard restricted slots are allowed.
    if ( gConf.bUseSimplifiedMultiShardCharacterSlotRules && ( ( flags & (~CharSlotRestrictFlag_VirtualShard) ) != 0 ) )
    {
        Errorf("Trying to use character slot restriction other than virtual shard with simplified multi-shard character slot rules.  This is not allowed.");
    }

    availableSlots->numTotalSlots += numSlots;

    // handle unrestricted slots
    if ( flags == 0 )
    {
        availableSlots->numUnrestrictedSlots += numSlots;
        return;
    }

    // find an existing restriction that matches
    for ( i = 0; i < eaSize(&availableSlots->eaSlotRestrictions); i++ )
    {
        CharSlotRestriction *restriction = availableSlots->eaSlotRestrictions[i];
        if ( restriction->flags == flags &&
             ( (flags & CharSlotRestrictFlag_VirtualShard) == 0 || restriction->virtualShardID == virtualShardID ) &&
             ( (flags & CharSlotRestrictFlag_Allegiance) == 0 || restriction->allegianceName == pooledAllegianceName ) )
        {
            restriction->numSlots += numSlots;
            return;
        }
    }

    // no matching restrictions found, so create a new restriction
    if ( (flags & CharSlotRestrictFlag_VirtualShard) == 0 )
    {
        virtualShardID = 0;
    }

    if ( (flags & CharSlotRestrictFlag_Allegiance) == 0 )
    {
        pooledAllegianceName = NULL;
    }

    newRestriction = StructCreate(parse_CharSlotRestriction);
    newRestriction->numSlots = numSlots;
    newRestriction->flags = flags;
    newRestriction->virtualShardID = virtualShardID;
    newRestriction->allegianceName = pooledAllegianceName;

    eaPush(&availableSlots->eaSlotRestrictions, newRestriction);

    return;
}

// return true if an exact match for the given restrictions exists, then optionally remove the slot.
static bool
FindMatchingRestriction(AvailableCharSlots *availableSlots, CharSlotRestrictFlag flags, U32 virtualShardID, const char *allegianceName, bool remove)
{
    int i;
    for ( i = 0; i < eaSize(&availableSlots->eaSlotRestrictions); i++ )
    {
        CharSlotRestriction *restriction = availableSlots->eaSlotRestrictions[i];

        if ( restriction->flags == flags && 
             restriction->virtualShardID == virtualShardID &&
             restriction->allegianceName == allegianceName )
        {
            // optionally remove the slot
            if ( remove )
            {
                restriction->numSlots--;
                if ( restriction->numSlots == 0 )
                {
                    // remove the restriction if it doesn't have any slots left
                    eaRemove(&availableSlots->eaSlotRestrictions, i);
                }
                availableSlots->numTotalSlots--;
            }
            return true;
        }
    }

    return false;
}

// Match the most restrictive available slot for the given virtual shard and allegiance
// If a match is found optionally remove the slot from availableSlots.
bool
CharSlots_MatchSlot(AvailableCharSlots *availableSlots, U32 virtualShardID, const char *allegianceName, bool remove)
{
    const char *pooledAllegianceName = allocAddString(allegianceName);

    if (availableSlots->numTotalSlots == 0)
    {
        return false;
    }

    if (FindMatchingRestriction(availableSlots, CharSlotRestrictFlag_VirtualShard | CharSlotRestrictFlag_Allegiance, virtualShardID, pooledAllegianceName, remove))
    {
        // found a slot restricted to both virtual shard and allegiance
        return true;
    }

    // NOTE - it is important to match against virtual shard restrictions before allegiance restrictions
    if (FindMatchingRestriction(availableSlots, CharSlotRestrictFlag_VirtualShard, virtualShardID, NULL, remove))
    {
        // found a slot restricted to just virtual shard
        return true;
    }

    if (FindMatchingRestriction(availableSlots, CharSlotRestrictFlag_Allegiance, 0, pooledAllegianceName, remove))
    {
        // found a slot restricted to just allegiance
        return true;
    }

    // use an unrestricted slot if available
	if( virtualShardID == 0 || !gConf.bVirtualShardsOnlyUseRestrictedCharacterSlots )
	{
		if (availableSlots->numUnrestrictedSlots > 0)
		{
			if ( remove )
			{
				availableSlots->numUnrestrictedSlots--;
				availableSlots->numTotalSlots--;
			}

			return true;
		}
	}

    return false;
}

#include "AutoGen/LoginCommon_h_ast.c"
