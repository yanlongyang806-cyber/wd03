#include "earray.h"
#include "patchdb.h"
#include "patchdb_opt.h"

void forEachDirEntry(DirEntry *dir_entry, ForEachDirEntryCallback callback, void *userdata)
{
	callback(dir_entry, userdata);
	EARRAY_CONST_FOREACH_BEGIN(dir_entry->children, i, isize); // this is unsafe for deletion
	forEachDirEntry(dir_entry->children[i], callback, userdata);
	EARRAY_FOREACH_END;
}

void forEachDirEntryReverse(DirEntry *dir_entry, ForEachDirEntryCallback callback, void *userdata)
{
	EARRAY_FOREACH_REVERSE_BEGIN(dir_entry->children, i);
	forEachDirEntryReverse(dir_entry->children[i], callback, userdata);
	EARRAY_FOREACH_END;
	callback(dir_entry, userdata);
}

FileVersion* patchFindVersionInDir(	DirEntry *de,
									int branch,
									const char *sandbox,
									int rev,
									int incr_from)
{
	S32 i;
	S32 useIncrFrom = incr_from != PATCHREVISION_NONE;
	S32 isFrozenEntry;
	S32 maxRev = max(incr_from, rev);
	S32 startVer;

	if(!de)
	{
		return NULL;
	}

	isFrozenEntry = de->flags & DIRENTRY_FROZEN;
	
	startVer = eaSize(&de->versions) - 1;
		
	for(i = startVer; i >= 0; i--)
	{
		const FileVersion *ver = de->versions[i];
		const Checkin *checkin = ver->checkin;
		bool in_sandbox = false;
		
		if(SAFE_DEREF(checkin->sandbox))
		{
			// if we're using a sandbox, the checkin must match it or be in the trunk
			if(	!SAFE_DEREF(sandbox) ||
				stricmp(checkin->sandbox, sandbox))
			{
				continue;
			}

			in_sandbox = true;
		}
		// else, the checkin was in the trunk, it matches any sandbox, but might not be included in incrementals

		if( (	isFrozenEntry ||
				checkin->branch <= branch)
			&&
			checkin->rev <= (useIncrFrom && !in_sandbox ? incr_from : rev))
		{
			return de->versions[i];
		}
	}

	return NULL;
}
