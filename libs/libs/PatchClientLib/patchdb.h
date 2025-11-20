#ifndef _PATCHDB_H
#define _PATCHDB_H
GCC_SYSTEM

typedef struct Checkin Checkin;
typedef struct DirEntry DirEntry;
typedef struct PatchFile PatchFile;
typedef struct StashTableImp* StashTable;
typedef struct StringTableImp* StringTable;
typedef struct FilespecMap FilespecMap;
typedef struct PatchProject PatchProject;

#define PATCHDB_VERSION_ORIGINAL			0	// Original
#define PATCHDB_VERSION_REVISIONNUMBERS		1	// Starting using revision numbers instead of timestamps

#define PATCHDB_VERSION_CURRENT PATCHDB_VERSION_REVISIONNUMBERS

// #define PATCHREVISION_NULL	-1
#define PATCHREVISION_NONE		-2
#define PATCHREVISION_NEW		-3

#define PATCHBRANCH_TIP				-1 // server-only, see patchserverdb.h
#define PATCHBRANCH_NONE			-2 // must match GIMME_BRANCH_UNKNOWN

// If you make any additions to these structures, make sure that the PatchServer mirroring code can handle it.
// The PatchServer does not destroy and reload its database when mirroring.

typedef enum PatchDBFlags
{
	PATCHDB_DEFAULT			= 0,
	PATCHDB_POOLED_PATHS	= (1<<0),
	PATCHDB_OMIT_AUTHORS	= (2<<0),	// Omit author information in PatchDB, to save memory.
} PatchDBFlags;

typedef enum DirEntryFlags	// not persisted
{
	DIRENTRY_FROZEN	= (1<<0),	// identical in all branches
} DirEntryFlags;

typedef enum FileVerFlags
{
	// NEVER CHANGE THE ORDER OF THESE
	FILEVERSION_DELETED				= (1<<0),
	FILEVERSION_MATCHED				= (1<<1),
	FILEVERSION_HEADER_MATCHED		= (1<<2),
	FILEVERSION_LINK_BACKWARD_SOLID	= (1<<3),
	FILEVERSION_LINK_FORWARD_BROKEN	= (1<<4),
} FileVerFlags;

AUTO_STRUCT;
typedef struct FileVersion
{
	// IF YOU CHANGE THE ORDER OF THESE, THE DIRENTRY PARSE TABLE NEEDS TO REFER TO FILEVERSIONS BY ANOTHER NAME
AST_PREFIX(STRUCTPARAM)
	int				version;	AST(NAME(Version))
	int				rev;
	U32				checksum;
	U32				size;
	U32				modified;
	U32				header_size;
	U32				header_checksum;
	FileVerFlags	flags; AST(INT)		// INT  maintains compatibility and works better as a param
	U32				expires;	AST(NAME(Expires)) // server-only. greatest expiration of named views that this version is in
AST_PREFIX()
AST_STOP
	U8*				header_data;
	Checkin*		checkin;
	DirEntry*		parent;
	PatchFile*		patch;		// server-only. file data to be served
	U8*				in_memory_data; // client-only. Only used when PCL_IN_MEMORY is enabled

	U32				sizeInHogg;
	U32				foundSizeInHogg : 1;
	U32				foundInHogg		: 1;
} FileVersion;

AUTO_STRUCT;
typedef struct Checkout
{
	char	*author;	AST(NAME(Author))
	U32		time;		AST(NAME(Time))
	char	*sandbox;	AST(NAME(Sandbox))
	int		branch;		AST(NAME(Branch))
} Checkout;

AUTO_STRUCT;
typedef struct DirEntry
{
	char*			name;			AST(STRUCTPARAM)
	FileVersion**	versions;		AST(NAME(Version) REDUNDANT_STRUCT(versions, parse_load_FileVersion))
	DirEntry**		children;		AST(NAME(File, children))
	Checkout**		checkouts;	AST(NAME(Checkout, checkouts))
AST_STOP
	const char		*path;		// string table or pooled
	DirEntry		*parent;
	DirEntryFlags	flags;
	const char		*author;	// client-only. 
	const char		*lockedby;	// client-only.
	PatchProject	**project_cache_allowed; // server-only. earray of projects this file is allowed in.
	time_t			project_cache_time; // server-only. timestamp of the last time the cache array was updated.
} DirEntry;

AUTO_STRUCT;
typedef struct Checkin
// Checkins to sandboxes obscure other files only for views in that sandbox
// Newer checkins in the trunk (not any sandbox) will not obscure files checked in to a sandbox
// Either everything in a sandbox is an incremental from the same origin (incr_from)
//     or everything is not an incremental (incr_from == PATCHREVISION_NONE)
// This system doesn't truly allow forking incrementals
// If we cram any more functionality into sandboxes, we should make them a proper structure
{
	int		rev;		AST(STRUCTPARAM)
	int		branch;		AST(NAME(Branch))
	U32		time;		AST(NAME(Time))
	char	*sandbox;	AST(NAME(Sandbox))
	int		incr_from;	AST(NAME(IncrementalFrom) DEFAULT(PATCHREVISION_NONE))
	char	*author;	AST(NAME(Author))
	char	*comment;	AST(NAME(Comment))
AST_STOP
	FileVersion	**versions;
	unsigned update_duration;	// On the server, the amount of time it took to update this checkin
	bool updated;				// true, if this checkin has been updated
} Checkin;

AUTO_STRUCT;
typedef struct NamedView
{
	char*	name;		AST(STRUCTPARAM)
	int		branch;		AST(NAME(Branch))
	int		rev;		AST(NAME(Revision, Time))
	char*	comment;	AST(NAME(Comment))
	char*	sandbox;	AST(NAME(Sandbox))	// incremental info should be inferred from sandbox
	U32		expires;	AST(NAME(Expires))	// server-only
	U32		viewed;		AST(NAME(Viewed))	// server-only
	U32		viewed_external; AST(NAME(ViewedExternal)) // server-only
	bool	dirty;		AST(NAME(Dirty)) // server-only, set when changing the expiry time
} NamedView;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("") AST_IGNORE(patcher_version);
typedef struct PatchDB
{
	int			version;			AST(NAME(DBVersion))
	DirEntry	root;				AST(NAME(Root))
	Checkin		**checkins;			AST(NAME(Checkin, checkins))
	NamedView	**namedviews;		AST(NAME(View, namedviews))
	int			*branch_valid_since;AST(NAME(BranchValidity)) // earliest non-pruned revs
	int			latest_rev;			AST(NAME(LatestRev) DEFAULT(-1)) // Copy of PatchServerDb.latest_rev used during server-to-server sync
AST_STOP
	PatchDBFlags flags;
	StashTable	sandbox_stash;		// last checkin for a given sandbox
	StashTable	sandboxes_included; // List of sandbox names explicitly included by the mirror config. Only used during sync.
	StashTable	view_stash;
	StashTable	dir_lookup;
	StashTable	author_stash;		// client-only.
	StringTable author_strings;
	StringTable	lookup_strings;
	U32			*checkout_time;		// the last time checkouts changed, indexed by branch (sandboxes ignored)
	FilespecMap	*frozen_filemap;	// SHALLOW COPY! currently server-only, determines which new files will be frozen
} PatchDB;

// Structure sent to the client when asking for history about a file
AUTO_STRUCT;
typedef struct PatcherFileHistory
{
	DirEntry *dir_entry;
	DirEntryFlags flags;	AST(INT)
	Checkin **checkins; // Just the relevant ones, parallel to dir_entry->versions
	char **batch_info; // Info on other files involved in each checkin; parallel to the above
} PatcherFileHistory;
extern ParseTable parse_PatcherFileHistory[];
#define TYPE_parse_PatcherFileHistory PatcherFileHistory

PatchDB* patchCreateDb(PatchDBFlags flags, FilespecMap *frozen_filemap);
PatchDB* patchLoadDb(const char *manifest_name, PatchDBFlags flags, FilespecMap *frozen_filemap);
PatchDB *patchLinkDb(PatchDB *db, bool incremental);
void patchDbWrite(const char *fileName, char **estr, PatchDB *db); // write to file, estring, or both. serializes once
void patchDbDestroy(PatchDB **pdb);

void patchDbRemoveBadCheckouts(PatchDB* db);

typedef bool (*patchDbShouldTrimCallback)(const char *path, FileVersion *ver, void *userdata);

PatchDB* patchLoadDbClient(const char *manifest_name, PatchDBFlags flags,
	patchDbShouldTrimCallback should_trim, void *trimdata);
PatchDB* patchLoadDbClientFromData(char *manifest_data, size_t manifest_length, PatchDBFlags flags,
	patchDbShouldTrimCallback should_trim, void *trimdata);
void patchSetAuthorClient(PatchDB *db, DirEntry *dir, const char *author);
void patchSetLockedbyClient(PatchDB *db, DirEntry *dir, const char *author);

void checkinFree(Checkin ** checkin);
bool fileVersionsEqual(FileVersion *lhs, FileVersion *rhs);
void fileVersionDestroy(FileVersion *file);
bool fileVersionRemoveAndDestroy(PatchDB *db, FileVersion *file);
void fileVersionRemoveByRevision(PatchDB *db, const char *filename, int rev);
void fileVersionRemoveById(PatchDB *db, const char *filename, int version);

DirEntry * patchFindPath(PatchDB * db, const char * dir_p, int add);
FileVersion* patchFindVersion(PatchDB *db, const char *dir_name, int branch, char *sandbox, int rev, int incr_from);
void direntryRemoveAndDestroy(PatchDB *db, DirEntry *dir); // path probably won't be freed
void direntryRemoveAndDestroyEx(PatchDB *db, DirEntry *dir, bool check_parents); // path probably won't be freed

void patchSetFrozenFiles(PatchDB *db, FilespecMap *frozen_filemap);
FileVersion* patchPreviousVersion(const FileVersion *ver);
int patchFindRev(PatchDB *db, int rev_start, int branch, const char *sandbox);
int patchFindRevByTime(PatchDB *db, U32 time, int branch, const char *sandbox, int rev_max);

typedef void (*ForEachDirEntryCallback)(DirEntry*,void*);
void patchForEachDirEntry(PatchDB *db, ForEachDirEntryCallback callback, void *userdata);
void patchForEachDirEntryReverse(PatchDB *db, ForEachDirEntryCallback callback, void *userdata);
void patchForEachDirEntryPrefix(PatchDB *db, const char *prefix, ForEachDirEntryCallback callback, void *userdata);
FileVersion* patchFindVersionInDir(	DirEntry *dir_entry,
									int branch,
									const char *sandbox,
									int rev,
									int incr_from);
Checkout* patchFindCheckoutInDir(const DirEntry *dir, int branch, const char *sandbox);
Checkout* patchFindCheckout(PatchDB *db, const char *dir_name, int branch, const char *sandbox, DirEntry **dir_p);

NamedView* patchAddNamedView(	PatchDB* db,
								const char* view_name,
								int branch,
								const char* sandbox,
								int rev,
								const char* comment,
								U32 expires,
								char* err_msg,
								int err_msg_size);
								
NamedView* patchFindNamedView(PatchDB *db, const char *view_name);
FileVersion * patchAddVersion(PatchDB *db, Checkin * checkin, U32 checksum, bool deleted, const char *path,
							  U32 modified, U32 size, U32 header_size, U32 header_checksum, U8 *header_data, U32 expires);
Checkin* patchAddCheckin(PatchDB *db, int branch, char *sandbox, char *author,char *comment, U32 filetime, int incr_from);
Checkin* patchGetSandboxCheckin(PatchDB *db, const char *sandbox);
void patchUpdateCheckoutTime(PatchDB* db, U32 branch);
Checkout * patchAddCheckout(PatchDB *db, DirEntry *dir_entry, const char *author, int branch, const char *sandbox, U32 time);
bool patchRemoveCheckout(PatchDB *db, DirEntry *dir_entry, int branch, const char *sandbox);

#endif
