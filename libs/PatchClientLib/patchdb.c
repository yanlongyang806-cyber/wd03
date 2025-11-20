#include "EString.h"
#include "file.h"
#include "FilespecMap.h"
#include "patchcommonutils.h"
#include "patchdb.h"
#include "patchdb_h_ast.h"
#include "patchdb_opt.h"
#include "patchxfer.h"
#include "pcl_client_internal.h"
#include "StashTable.h"
#include "StringCache.h"
#include "StringTable.h"
#include "StringUtil.h"
#include "trivia.h"
#include "timing.h"
#include "wininclude.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

static void initDir(DirEntry *dir, PatchDB *db);

// COMPATIBILITY PARSETABLES /////////////////////////////////////////////////

StaticDefineInt parse_load_FileVerFlags[3] =
{
	DEFINE_INT
	{ "Deleted",		FILEVERSION_DELETED },
	DEFINE_END
};

ParseTable parse_load_FileVersion[] =
{
	{ "FileVersion", 		TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(FileVersion), 0, NULL, 0 },
	{ "{",					TOK_START, 0 },
	{ "version",			TOK_AUTOINT(FileVersion, version, 0), NULL },
	{ "checksum",			TOK_AUTOINT(FileVersion, checksum, 0), NULL },
	{ "size",				TOK_AUTOINT(FileVersion, size, 0), NULL },
	{ "modified",			TOK_AUTOINT(FileVersion, modified, 0), NULL },
	{ "checkin_idx",		TOK_AUTOINT(FileVersion, rev, 0), NULL },
	{ "flags",				TOK_AUTOINT(FileVersion, flags, 0), parse_load_FileVerFlags },
	{ "header_size",		TOK_AUTOINT(FileVersion, header_size, 0), NULL },
	{ "header_checksum",	TOK_AUTOINT(FileVersion, header_checksum, 0), NULL },
	{ "}",					TOK_END, 0 },
	{ "", 0, 0 }
};

AUTO_RUN;
void initLoadFileVersionParse(void)
{
	ParserSetTableInfo(parse_load_FileVersion, sizeof(FileVersion), "load_FileVersion", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);	
}

//////////////////////////////////////////////////////////////////////////////

static void releaseFileVersionMemory(DirEntry *dir, void *user_data)
{
	int i;
	for(i = 0; i < eaSize(&dir->versions); i++)
	{
		FileVersion *ver = dir->versions[i];
		SAFE_FREE(ver->header_data);
		SAFE_FREE(ver->in_memory_data);
		assertmsg(ver->patch == NULL,"Leaking PatchFiles!");
	}
}

void patchDbDestroy(PatchDB **pdb)
{
	if(pdb && *pdb)
	{
		int i;
		PatchDB *db = *pdb;
		forEachDirEntry(&db->root, releaseFileVersionMemory, NULL);
		for(i = 0; i < eaSize(&db->checkins); i++)
			if(db->checkins[i])
				eaDestroy(&(db->checkins[i]->versions));
		stashTableDestroy(db->sandbox_stash);
		stashTableDestroy(db->view_stash);
		stashTableDestroy(db->dir_lookup);
		stashTableDestroy(db->author_stash);
		if(db->author_strings)
			destroyStringTable(db->author_strings);
		if(db->lookup_strings)
			destroyStringTable(db->lookup_strings);
		StructDeInit(parse_PatchDB, db);
		free(db);
		*pdb = NULL;
	}
}

NamedView* patchAddNamedView(	PatchDB* db,
								const char* view_name,
								int branch,
								const char* sandbox,
								int rev,
								const char* comment,
								U32 expires,
								char* err_msg,
								int err_msg_size)
{
	NamedView *named_view;
	assert(view_name);

	if(stashFindPointer(db->view_stash, view_name, &named_view))
	{
		bool changed = false;
		if(err_msg != NULL)
			sprintf_s(SAFESTR2(err_msg), "The name %s has already been used.", view_name);
		// Check that we aren't trying to change the target of the view.
		// NOTE: I would be willing to bet Bruce will want this removed one day. Probably safe. <NPK 2009-05-26>
		if(named_view->branch != branch)
		{
			changed = true;
			if(err_msg)
				strcatf_s(err_msg, err_msg_size, " Trying to re-add a view with a different branch (%d vs %d).", named_view->branch, branch);
			
		}
		if(named_view->rev != rev)
		{
			changed = true;
			if(err_msg)
				strcatf_s(err_msg, err_msg_size, " Trying to re-add a view with a different rev (%d vs %d)", named_view->rev, rev);
		}
		if(stricmp(named_view->sandbox, sandbox)!=0)
		{
			changed = true;
			if(err_msg)
				strcatf_s(err_msg, err_msg_size, " Trying to re-add a view with a different sandbox (%s vs %s)", named_view->sandbox, sandbox);
		}

		if(!changed)
		{
			// Allow propagating changes to the comment or expiry (needed for view expiration) from a master server
			if(stricmp(named_view->comment, comment)!=0)
			{
				StructFreeString(named_view->comment);
				named_view->comment = StructAllocString(comment);
			}
			named_view->expires = expires;
		}
		return NULL;
	}

	named_view = StructAlloc(parse_NamedView);
	named_view->branch = branch;
	named_view->name = StructAllocString(view_name);
	named_view->sandbox = StructAllocString(sandbox);
	named_view->rev = rev;
	named_view->comment = StructAllocString(comment);
	named_view->expires = expires;
	named_view->dirty = true;

	eaPush(&db->namedviews, named_view);
	if(!db->view_stash)
		db->view_stash = stashTableCreateWithStringKeys(10, StashDefault);
	assert(stashAddPointer(db->view_stash, named_view->name, named_view, false));

	return named_view;
}

NamedView* patchFindNamedView(PatchDB *db, const char *view_name)
{
	NamedView *view = NULL;
	if(view_name)
		stashFindPointer(db->view_stash, view_name, &view);
	return view;
}

DirEntry *patchFindPath(PatchDB * db, const char *dir_p, int add)
{
	char dir_buf[MAX_PATH];
	char *s, *dir, *names[100];
	int i;
	DirEntry *curr;
	size_t size;

	PERFINFO_AUTO_START_FUNC();

	if(!db)
	{
		assert(!add);
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	if(dir_p[0] == '/') // opening slash optional
		dir_p++;
	if (strEndsWith(dir_p, "/")) {
		// trailing slash on directories
		strcpy(dir_buf, dir_p);
		dir_buf[strlen(dir_buf)-1] = '\0';
		dir_p = dir_buf;
	}

	// FIXME: this is For legacy databases, remove it
	if(db->root.name)
	{
		// TODO: this could cause some confusion with something like FightClub/FightClub
		size_t len = strlen(db->root.name);
		if( !strnicmp(db->root.name, dir_p, len) && (!dir_p[len] || dir_p[len] == '/') )
		{
			dir_p += strlen(db->root.name);
			if(dir_p[0] == '/') // opening slash optional
				dir_p++;
		}
	}
	// EMXIF

	if(stashFindPointer(db->dir_lookup, dir_p, &curr))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return curr;
	}
	else if(!add)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	size = strlen(dir_p) + 1;
	dir = _alloca(size);
	strcpy_s(dir, size, dir_p);

	// parse up the path, looking for one we know (curr)
	s = dir+size-2; // skip the \0
	for(i = 0; ; ++i)
	{
		while(s >= dir && *s != '/')
			--s;

		names[i] = s+1;
		if(s >= dir)
		{
			*(s--) = '\0';
			if(stashFindPointer(db->dir_lookup, dir, &curr))
				break;
		}
		else
		{
			curr = &db->root;
			break;
		}
	}
	assert(i <= ARRAY_SIZE(names));

	// walk back down the path, building direntries along the way
	for(; i >= 0; --i)
	{
		DirEntry *child = StructAlloc(parse_DirEntry);
		child->name = StructAllocString(names[i]);
		child->parent = curr;
		initDir(child, db);
		eaPush(&curr->children,child);
		curr = child;
	}

	PERFINFO_AUTO_STOP_FUNC();

	return curr;
}

static void clearFileVersion(PatchDB *db, FileVersion *ver);
static void clearDirEntry(PatchDB *db, DirEntry *dir)
{
	int i;
	for(i = 0; i < eaSize(&dir->children); i++)
		clearDirEntry(db, dir->children[i]);
	eaDestroy(&dir->children);
	for(i = 0; i < eaSize(&dir->versions); i++)
		clearFileVersion(db, dir->versions[i]);
	eaDestroy(&dir->versions);
	eaDestroyStruct(&dir->checkouts, parse_Checkout);
	if(dir->parent) // not root
	{
		assert(stashRemovePointer(db->dir_lookup, dir->path, NULL)); // note: root's path is always ""
		if(!(db->flags & PATCHDB_POOLED_PATHS))
			strTableLogRemovalRequest(db->lookup_strings, dir->path);
		StructDestroy(parse_DirEntry, dir);
	}
}

void direntryRemoveAndDestroy(PatchDB *db, DirEntry *dir)
{
	direntryRemoveAndDestroyEx(db, dir, true);
}

void direntryRemoveAndDestroyEx(PatchDB *db, DirEntry *dir, bool check_parents)
{
	DirEntry *parent = dir->parent;
	clearDirEntry(db, dir); // clear children, destroys dir
	if(parent) // not root
	{
		// clear parents
		eaFindAndRemove(&parent->children, dir);

		// Remove empty parents.
		if(	check_parents &&
			!eaSize(&parent->versions) &&
			!eaSize(&parent->children))
		{
			direntryRemoveAndDestroyEx(db, parent, check_parents);
		}
	}
}

static int cmpFileVersion(const FileVersion **a, const FileVersion **b)
{
	int				t;
	const Checkin	*ca,*cb;

	ca = (*a)->checkin;
	cb = (*b)->checkin;
	t = ca->branch - cb->branch;
	if (t)
		return t;
	t = !!SAFE_DEREF(ca->sandbox) - !!SAFE_DEREF(cb->sandbox);
	if(t)
		return t;
	return (int)(ca->rev - cb->rev);
}

static int cmpFileVersionFrozen(const FileVersion **a, const FileVersion **b)
{
	int				t;
	const Checkin	*ca,*cb;

	ca = (*a)->checkin;
	cb = (*b)->checkin;
	t = !!SAFE_DEREF(ca->sandbox) - !!SAFE_DEREF(cb->sandbox);
	if(t)
		return t;
	return (int)(ca->rev - cb->rev);
}

static void sortVersions(DirEntry *dir)
{
	eaQSort(dir->versions, dir->flags & DIRENTRY_FROZEN ? cmpFileVersionFrozen : cmpFileVersion);
}

void linkCheckins(DirEntry *dir,PatchDB *db)
{
	int i;
	for(i=0;i<eaSize(&dir->versions);i++)
	{
		FileVersion	*ver = dir->versions[i];
		ver->parent = dir;
		if(ver->rev >= 0 && ver->rev < eaSize(&db->checkins))
		{
			ver->checkin = db->checkins[ver->rev];
			eaPush(&ver->checkin->versions, ver);
		}
		else
			assert(!ver->rev); // 0 used to mean none
	}
	sortVersions(dir);

	for(i=0;i<eaSize(&dir->children);i++)
	{
		dir->children[i]->parent = dir;
		linkCheckins(dir->children[i],db);
	}
}

static void freezeDir(DirEntry *dir, PatchDB *db)
{
	int freeze;
	bool changed;

	if(	db->frozen_filemap &&
		filespecMapGetInt(db->frozen_filemap, dir->path, &freeze) &&
		freeze >= 0)
	{
		changed = !(dir->flags & DIRENTRY_FROZEN);
		dir->flags |= DIRENTRY_FROZEN;
	}
	else
	{
		changed = dir->flags & DIRENTRY_FROZEN;
		dir->flags &= ~DIRENTRY_FROZEN;
	}

	if(changed)
		sortVersions(dir);
}

static struct {
	struct {
		U32 minTimeStamp;
		U32 maxTimeStamp;
		U64 totalSize;
	} dupedVersions;
} initDirStats;

static void initDir(DirEntry *dir_entry, PatchDB *db)
{
	char buf[MAX_PATH], *path = buf+MAX_PATH-1;
	DirEntry	*dir;

	*path = '\0';
	for(dir = dir_entry; dir->parent; dir = dir->parent) // stop before adding root's name
	{
		int len = (int)strlen(dir->name);
		if(dir != dir_entry)
			*--path = '/';
		path -= len;
		assert(path >= buf);
		memcpy(path, dir->name, len);
	}
	
	dir_entry->path = db->flags & PATCHDB_POOLED_PATHS ? allocAddFilename(path) : strTableAddString(db->lookup_strings, path);
	//printf("Adding dir: %s\n", dir_entry->path);
	assert(stashAddPointer(db->dir_lookup, dir_entry->path, dir_entry, false));
	freezeDir(dir_entry, db);

	// Check for duped version numbers.

	#if 0
		EARRAY_CONST_FOREACH_BEGIN(dir_entry->versions, i, isize);
			FileVersion* v1 = dir_entry->versions[i];
			
			EARRAY_CONST_FOREACH_BEGIN_FROM(dir_entry->versions, j, jsize, i + 1);
				FileVersion* v2 = dir_entry->versions[j];
				
				if(v1->version == v2->version){
					MIN1(initDirStats.dupedVersions.minTimeStamp, v1->checkin->time);
					MIN1(initDirStats.dupedVersions.minTimeStamp, v2->checkin->time);
					MAX1(initDirStats.dupedVersions.maxTimeStamp, v1->checkin->time);
					MAX1(initDirStats.dupedVersions.maxTimeStamp, v2->checkin->time);
					
					printfColor(COLOR_BRIGHT|COLOR_RED,
								"WARNING: Duplicate version number (%d, revs %d & %d) in \"%s\"\n",
								v1->version,
								v1->rev,
								v2->rev,
								dir_entry->path);
				}
			EARRAY_FOREACH_END;
		EARRAY_FOREACH_END;
	#endif
}

static void fixupForDbVersion(DirEntry *dir, int *ver)
{
// 	if(*ver < PATCHDB_VERSION_DELETEDFILESTATS)
// 	{
// 		int i;
// 		for(i = eaSize(&dir->versions)-1; i >= 0; --i)
// 		{
// 			FileVersion *ver = dir->versions[i];
// 			if(ver->flags & FILEVERSION_DELETED)
// 			{
// 				FileVersion *prev = patchPreviousVersion(ver);
// 				if(prev)
// 				{
// 					assert(!(prev->flags & FILEVERSION_DELETED));
// 					ver->modified = prev->modified;
// 					ver->size = prev->size;
// 					ver->checksum = prev->checksum;
// 				}
// 			}
// 		}
// 
// 	}
}

PatchDB* patchCreateDb(PatchDBFlags flags, FilespecMap *frozen_filemap)
{
	PatchDB *db = StructCreate(parse_PatchDB);
	db->version = PATCHDB_VERSION_CURRENT;
	db->flags = flags;
	db->dir_lookup = stashTableCreateWithStringKeys(2500, StashDefault);
	if(!(db->flags & PATCHDB_POOLED_PATHS))
		db->lookup_strings = strTableCreate(StrTableDefault, 1024);
	db->frozen_filemap = frozen_filemap;
	initDir(&db->root, db);
	return db;
}

static void removeBadCheckouts(DirEntry* d){
	EARRAY_CONST_FOREACH_BEGIN(d->checkouts, i, isize);
		Checkout* c = d->checkouts[i];
		
		if(SAFE_DEREF(c->author)){
			removeLeadingAndFollowingSpaces(c->author);
		}
		
		if(!SAFE_DEREF(c->author)){
			printfColor(COLOR_BRIGHT|COLOR_RED,
						"  Removing checkout for missing author (%s: b%d%s%s: t%d)\n",
						d->path,
						c->branch,
						c->sandbox ? "-s" : "",
						NULL_TO_EMPTY(c->sandbox),
						c->time);
					
			eaRemove(&d->checkouts, i);
			i--;
			isize--;
			
			StructDestroySafe(	parse_Checkout,
								&c);
		}
	EARRAY_FOREACH_END;
		
	EARRAY_CONST_FOREACH_BEGIN(d->children, i, isize);
		removeBadCheckouts(d->children[i]);
	EARRAY_FOREACH_END;
}

void patchDbRemoveBadCheckouts(PatchDB* db)
{
	//printf("Removing bad checkouts: %s\n", db->root.path);
	loadstart_printf("Removing bad checkouts: %s", db->root.path);
	removeBadCheckouts(&db->root);
	loadend_printf("");
	//printf("Done removing bad checkouts: %s\n", db->root.path);
}

PatchDB* patchLoadDb(const char *manifest_name, PatchDBFlags flags, FilespecMap *frozen_filemap)
{
	char adjusted_path[MAX_PATH];
	PatchDB *db = patchCreateDb(flags, frozen_filemap);
	db->version = 0;

	//assertHeapValidateAll();
	machinePath(adjusted_path,manifest_name);
	if(fileExists(adjusted_path))
	{
		int result = ParserReadTextFile(adjusted_path, parse_PatchDB, db, 0);
		if (!result)
		{
			patchDbDestroy(&db);
			return NULL;
		}
		db->root.path = ""; // kind of a hack... text parser does a memset 0 on root
		freezeDir(&db->root, db);
	}
	
	return db;
}

PatchDB *patchLinkDb(PatchDB *db, bool incremental)
{
	int i, count;

	linkCheckins(&db->root, db);
	
	// Some checking to make sure that all checkins are listed in order
	// and that checkins to sandboxes all have the same incremental status (see patchVerifyIncremental())
	// and that checkins outside of sandboxes aren't incrementals
	count = eaSize(&db->checkins);
	if(count)
	{
		U32 checkin_time = 0;
		db->sandbox_stash = stashTableCreateWithStringKeys(96, StashDeepCopyKeys_NeverRelease);
		for(i = 0; i < count; i++)
		{
			Checkin *checkin = db->checkins[i];
			if(!checkin) continue;
			assertmsg(checkin_time <= checkin->time, "out of order checkins!!");
			assertmsg(checkin->rev == i, "Checkin out of place");
			checkin_time = checkin->time;
			if(checkin->sandbox && checkin->sandbox[0])
			{
				Checkin *last;
				if(stashFindPointer(db->sandbox_stash, checkin->sandbox, &last))
					assertmsg(checkin->incr_from == last->incr_from, "inconsistent incremental information");
				assert(stashAddPointer(db->sandbox_stash, checkin->sandbox, checkin, true));
			}
			else
			{
				assertmsg(checkin->incr_from == PATCHREVISION_NONE, "incremental outside a sandbox!!");
			}
			if(checkin->incr_from != PATCHREVISION_NONE)
			{
				if(db->version < PATCHDB_VERSION_REVISIONNUMBERS) // previously stored as timestamps
				{
					int rev = patchFindRevByTime(db, checkin->incr_from, checkin->branch, checkin->sandbox, INT_MAX);
					assertmsg(eaGet(&db->checkins, rev), "old incremental time doesn't refer to any checkin");
					checkin->incr_from = rev;
				}
				else if(!incremental)
				{
					// Don't run this test for incr manifests, since the old checkins won't exist.
					assertmsg(eaGet(&db->checkins, checkin->incr_from), "checkin refers to a checkin that doesn't exist");
				}
			}
		}
	}

	// TODO: don't sort versions twice
	
	{
		ZeroStruct(&initDirStats);
		initDirStats.dupedVersions.minTimeStamp = U32_MAX;

		EARRAY_CONST_FOREACH_BEGIN(db->root.children, j, jsize);
			forEachDirEntry(db->root.children[j], initDir, db);
		EARRAY_FOREACH_END;
		
		#if 0
			if(initDirStats.dupedVersions.minTimeStamp != U32_MAX)
			{
				char buffer[100];
				
				#define TIMESTR(x) timeMakeLocalDateStringFromSecondsSince2000(	buffer,					\
																				patchFileTimeToSS2000(x))

				printf(	"Min duped file version timestamp: %s\n",
						TIMESTR(initDirStats.dupedVersions.minTimeStamp));
						
				printf(	"Max duped file version timestamp: %s\n",
						TIMESTR(initDirStats.dupedVersions.maxTimeStamp));

				#undef TIMESTR
			}
		#endif
	}

	count = eaSize(&db->namedviews);
	if(count)
	{
		db->view_stash = stashTableCreateWithStringKeys(count + (count>>1), StashDefault);
		for(i = 0; i < count; i++)
		{
			NamedView *named = db->namedviews[i];
			assertmsg(stashAddPointer(db->view_stash, named->name, named, false), "namedview collision!!");
			if(db->version < PATCHDB_VERSION_REVISIONNUMBERS) // previously stored as timestamps
			{
				int rev = patchFindRevByTime(db, named->rev, named->branch, named->sandbox, INT_MAX);
				assertmsg(eaGet(&db->checkins, rev), "old named view doesn't refer to any checkin");
				named->rev = rev;
			}
			else
				assertmsg(eaGet(&db->checkins, named->rev), "namedview refers to a checkin that doesn't exist!");
		}
	}

	if(db->version < PATCHDB_VERSION_CURRENT)
		forEachDirEntry(&db->root, fixupForDbVersion, &db->version);

	db->version = PATCHDB_VERSION_CURRENT;
	return db;
}

// TODO: move this to patchserverdb.c/h
void patchDbWrite(const char *fileName, char **estr, PatchDB *db)
{
	// TODO: error checking
	if(estr)
		ParserWriteText(estr, parse_PatchDB, db, 0, 0, 0);

	if(fileName)
	{
		makeDirectoriesForFile(fileName);

		if(fileExists(fileName))
		{
			char history_filename[MAX_PATH], fname_root[MAX_PATH], fname_ext[MAX_PATH], * found;

			found = strrchr(fileName, '/');
			if(found)
				strcpy(fname_root, found + 1);
			else
				strcpy(fname_root, fileName);
			found = strrchr(fname_root, '.');
			if(found)
			{
				found[0] = '\0';
				strcpy(fname_ext, found + 1);
			}
			else
			{
				fname_ext[0] = '\0';
			}
			sprintf(history_filename, "./history/%s.%u.%s", fname_root, getCurrentFileTime(), fname_ext);
			while(fileExists(history_filename))
			{
				Sleep(500);
				sprintf(history_filename, "./history/%s.%u.%s", fname_root, getCurrentFileTime(), fname_ext);
			}

			makeDirectoriesForFile(history_filename);
			assert(rename(fileName, history_filename)==0);
			assert(fileGzip(history_filename));
		}

		if(estr)
		{
			FILE *file = fopen(fileName,"wb");
			fwrite(*estr, 1, estrLength(estr), file);
			fclose(file);
		}
		else
		{
			ParserWriteTextFile(fileName, parse_PatchDB, db, 0, 0);
		}
	}
}

static bool parseString(char **line, char **str)
{
	if(*line == NULL)
		return false;
	*str = *line;
	*line = strchr(*line, '\t');
	if(*line)
		*((*line)++) = '\0';
	return true;
}

static bool parseInt(char **line, int *i)
{
	if(*line == NULL || (!isdigit(**line) && **line != '-'))
		return false;
	*i = atoi(*line);
	*line = strchr(*line, '\t');
	if(*line)
		++*line;
	return true;
}

static const char* getAuthorString(PatchDB *db, const char *author)
{
	char *ret;

	if(!author || author[0] == '\0')
		return NULL;

	if(!stashFindPointer(db->author_stash, author, &ret))
	{
		ret = strTableAddString(db->author_strings, author);
		stashAddPointer(db->author_stash, ret, ret, false);
	}
	return ret;
}

PatchDB* patchLoadDbClient(const char *manifest_name, PatchDBFlags flags,
	patchDbShouldTrimCallback should_trim, void *trimdata)
{
	// Manifest generated by patchprojectFindOrAddView() 
	char adjusted_path[MAX_PATH];
	PatchDB *db;
	char *manifest_str;

	//printf("Loading manifest: \"%s\"...", manifest_name);

	machinePath(adjusted_path, manifest_name);
	
	{
		TriviaMutex mutex = triviaAcquireDumbMutex(adjusted_path);
		manifest_str = fileAlloc(adjusted_path, NULL);
		triviaReleaseDumbMutex(mutex);
	}
	
	if(!manifest_str)
	{
		printf("Couldn't load manifest \"%s\"\n", manifest_name);
		return NULL;
	}
	
	pclMSpf("loading manifest: %s...",
			adjusted_path);

	db = patchLoadDbClientFromData(manifest_str, strlen(manifest_str), flags, should_trim, trimdata);

	pclMSpf("done loading manifest: %s\n",
			adjusted_path);
	
	//printf("done.\n");

	free(manifest_str);
	return db;
}

// FIXME: This function should be rewritten with a proper parser that does not need to modify manifest_datamanifest_data.
PatchDB* patchLoadDbClientFromData(char *manifest_data, size_t manifest_length, PatchDBFlags flags,
	patchDbShouldTrimCallback should_trim, void *trimdata)
{
	// Manifest generated by patchprojectFindOrAddView() 
	PatchDB *db;
	char *line, *nextline;
	bool no_authors;
	unsigned initial_size;

	PERFINFO_AUTO_START_FUNC();

	// Check if authors should be included.
	no_authors = flags & PATCHDB_OMIT_AUTHORS;
	initial_size = no_authors ? 8 : 512;

	db = patchCreateDb(flags, NULL);
	db->author_stash = stashTableCreateWithStringKeys(initial_size, StashDefault);
	db->author_strings = strTableCreate(StrTableDefault, initial_size);
	for(line = manifest_data; line; line = nextline)
	{
		char *path, *author = "", *lockedby = "";
		DirEntry *dir;
		FileVersion *ver;
		int verflags = 0;

		if (manifest_length == (size_t)(line - manifest_data))
		{
			nextline = NULL;
			continue;
		}
		nextline = strnchr(line, manifest_length - (line - manifest_data), '\n');
		if(!nextline)
		{
			patchDbDestroy(&db);
			break;
		}
		*(nextline++) = '\0';

		// Skip comments.
		if(*line == '#')
			continue;

		// Parse the DirEntry path.
		if(!parseString(&line, &path))
		{
			patchDbDestroy(&db);
			break;
		}

		// Parse the FileVersion information.
		ver = calloc(1, sizeof(*ver));
		if( !parseInt(&line, &ver->modified) ||
			!parseInt(&line, &ver->size) ||
			!parseInt(&line, &ver->checksum) ||
			!parseInt(&line, &ver->header_size) ||
			!parseInt(&line, &ver->header_checksum) ||
			(	parseInt(&line, &verflags) && 
			(	!parseString(&line, &author) ||
			!parseString(&line, &lockedby) ) ) )
		{
			free(ver);
			patchDbDestroy(&db);
			break;
		}

		// If we're trimming, skip trimmed paths.
		if (should_trim && should_trim(path, ver, trimdata))
		{
			free(ver);
			continue;
		}

		// Add the FileVersion to a DirEntry in the DB.
		dir = patchFindPath(db, path, 1);
		ver->parent = dir;
		ver->flags = verflags;
		eaPush(&dir->versions, ver);
		dir->author = no_authors ? NULL : getAuthorString(db, author);
		dir->lockedby = no_authors ? NULL : getAuthorString(db, lockedby);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return db;
}

void patchSetAuthorClient(PatchDB *db, DirEntry *dir, const char *author)
{
	if(dir)
		dir->author = getAuthorString(db, author);
}

void patchSetLockedbyClient(PatchDB *db, DirEntry *dir, const char *author)
{
	if(dir)
		dir->lockedby = getAuthorString(db, author);
}

void patchForEachDirEntry(PatchDB *db, ForEachDirEntryCallback callback, void *userdata)
{
	forEachDirEntry(&db->root, callback, userdata);
}

void patchForEachDirEntryReverse(PatchDB *db, ForEachDirEntryCallback callback, void *userdata)
{
	forEachDirEntryReverse(&db->root, callback, userdata);
}

void patchForEachDirEntryPrefix(PatchDB *db, const char *prefix, ForEachDirEntryCallback callback, void *userdata)
{
	DirEntry *root = &db->root;

	// If a prefix is set, jump to that node in the DB
	if(prefix)
	{
		root = patchFindPath(db, prefix, false);
		if(!root)
			return; // Invalid prefix, so we can bail out now
	}

	forEachDirEntry(root, callback, userdata);
}

static int cmpTimeline(const U32 *time, const Checkin **pcheckin)
{
	if(!(*pcheckin))
		return 1;
	return *time < (*pcheckin)->time ? -1 : *time > (*pcheckin)->time ? 1 : 0;
}

int patchFindRev(PatchDB *db, int rev_start, int branch, const char *sandbox)
{
	int rev;

	PERFINFO_AUTO_START_FUNC();

	if(sandbox && !sandbox[0])
		sandbox = NULL;

	for(rev = MIN(rev_start, eaSize(&db->checkins)-1); rev >= 0; --rev)
	{
		Checkin *checkin = db->checkins[rev];
		if( checkin->branch <= branch &&
			(!checkin->sandbox || !checkin->sandbox[0] || (sandbox && stricmp(checkin->sandbox, sandbox) == 0 )) )
		{
			PERFINFO_AUTO_STOP_FUNC();
			return rev;
		}
	}
	PERFINFO_AUTO_STOP_FUNC();
	return -1;
}

int patchFindRevByTime(PatchDB *db, U32 time, int branch, const char *sandbox, int latest_rev)
{
	U32 time_temp;
	int found;
	int result;

	PERFINFO_AUTO_START_FUNC();

	time_temp = time < U32_MAX ? time+1 : time;
	found = (int)eaBFind(db->checkins, cmpTimeline, time_temp) - 1;

	ANALYSIS_ASSUME(found < eaSize(&db->checkins));
	MIN1(found, latest_rev);
	if(found >= 0)
	{
		if(!db->checkins[found])
		{
			PERFINFO_AUTO_STOP_FUNC();
			return -1;
		}
		assert(db->checkins[found]->time <= time_temp);
	}
	result = patchFindRev(db, found, branch, sandbox);

	PERFINFO_AUTO_STOP_FUNC();
	return result;
}

FileVersion* patchPreviousVersion(const FileVersion *ver)
{
	// Note ver->rev - 1
	return ver && ver->rev
		? patchFindVersionInDir(ver->parent, ver->checkin->branch, ver->checkin->sandbox,
								ver->rev - 1, ver->checkin->incr_from)
		: NULL;
}

void patchSetFrozenFiles(PatchDB *db, FilespecMap *frozen_filemap)
{
	db->frozen_filemap = frozen_filemap;
	forEachDirEntry(&db->root, freezeDir, db);
}

Checkout* patchFindCheckoutInDir(const DirEntry *dir, int branch, const char *sandbox)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	if(!dir)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	if(!sandbox)
		sandbox = "";

	for(i = 0; i < eaSize(&dir->checkouts); i++)
	{
		Checkout *checkout = dir->checkouts[i];
		if( ((dir->flags & DIRENTRY_FROZEN) || checkout->branch == branch) &&
			stricmp(NULL_TO_EMPTY(checkout->sandbox), sandbox) == 0)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return checkout;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

Checkout* patchFindCheckout(PatchDB *db, const char *dir_name, int branch, const char *sandbox, DirEntry **dir_p)
{
	DirEntry *dir = patchFindPath(db, dir_name, 0);
	if(dir_p)
		*dir_p = dir;
	return patchFindCheckoutInDir(dir, branch, sandbox);
}

bool fileVersionsEqual(FileVersion *lhs, FileVersion *rhs)
{
	return !StructCompare(parse_FileVersion, lhs, rhs, 0, 0, 0);
}

void fileVersionDestroy(FileVersion *file)
{
	if(file)
	{
		SAFE_FREE(file->header_data);
		SAFE_FREE(file->in_memory_data);
		assertmsg(file->patch == NULL,"Leaking PatchFiles!");
		StructDestroy(parse_FileVersion, file);
	}
}

static void clearFileVersion(PatchDB *db, FileVersion *ver)
{
	if(ver->checkin)
	{
		assert(ver->checkin->branch >= 0); // or /analyze gets upset
		if(eaiSize(&db->branch_valid_since) < ver->checkin->branch+1)
			eaiSetSize(&db->branch_valid_since, ver->checkin->branch+1);
		MAX1(db->branch_valid_since[ver->checkin->branch], ver->checkin->rev+1);
		eaFindAndRemove(&ver->checkin->versions, ver);
	}
	fileVersionDestroy(ver);
}

// Remove a FileVersion from a DirEntry.
// Return true if pruning this FileVersion resulted in the removal of the parent DirEntry.
bool fileVersionRemoveAndDestroy(PatchDB *db, FileVersion *ver)
{
	// GGFIXME: move the handling for branch_valid_since into PatchServerDb,
	DirEntry *parent;
	bool removed_parent = false;

	PERFINFO_AUTO_START_FUNC();

	parent = ver->parent;
	clearFileVersion(db, ver);

	if(parent)
	{
		// clear parents
		eaFindAndRemove(&parent->versions, ver);
		
		if(	!eaSize(&parent->versions) &&
			!eaSize(&parent->children))
		{
			direntryRemoveAndDestroy(db, parent);
			removed_parent = true;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return removed_parent;
}

void fileVersionRemoveByRevision(PatchDB *db, const char *filename, int rev)
{
	DirEntry *dir_entry = patchFindPath(db, filename, 0);
	int i;

	if(!dir_entry)
		return;

	for(i = eaSize(&dir_entry->versions)-1; i >= 0; i--)
		if(dir_entry->versions[i]->rev == rev)
		{
			fileVersionRemoveAndDestroy(db, dir_entry->versions[i]);
			return;
		}
}

void fileVersionRemoveById(PatchDB *db, const char *filename, int version)
{
	DirEntry *dir_entry = patchFindPath(db, filename, 0);
	int i;

	if(!dir_entry)
		return;

	for(i = eaSize(&dir_entry->versions)-1; i >= 0; i--)
		if(dir_entry->versions[i]->version == version)
		{
			fileVersionRemoveAndDestroy(db, dir_entry->versions[i]);
			return;
		}
}

int getMaxVersion(FileVersion **versions)
{
	int		i,maxver=0;

	for(i=0;i<eaSize(&versions);i++)
	{
		maxver = MAX(maxver,versions[i]->version);
	}
	return maxver;
}

FileVersion * patchAddVersion(	PatchDB *db,
								Checkin * checkin,
								U32 checksum,
								bool deleted,
								const char *path,
								U32 modified,
								U32 size,
								U32 header_size,
								U32 header_checksum,
								U8 *header_data,
								U32 expires)
{
	FileVersion * ver = StructCreate(parse_FileVersion), *prev = NULL;
	DirEntry * dir_entry = patchFindPath(db, path, 1);

	// these are needed for patchPreviousVersion
	ver->checkin = checkin;
	ver->parent = dir_entry;
	ver->rev = checkin ? checkin->rev : 0;

	if(deleted)
	{
		prev = patchPreviousVersion(ver);
		ver->flags |= FILEVERSION_DELETED;
	}
	ver->modified = prev ? prev->modified : modified;
	ver->size = size; // prev ? prev->size : size;
	ver->checksum = checksum; // prev ? prev->checksum : checksum;
	ver->header_size = header_size;
	ver->header_checksum = header_checksum;
	ver->header_data = header_data;
	ver->version = getMaxVersion(dir_entry->versions) + 1;
	ver->expires = expires;

	// Note that 'forcein' will remove other authors checkouts
	if(checkin)
		patchRemoveCheckout(db, dir_entry, checkin->branch, checkin->sandbox);

	eaPush(&dir_entry->versions, ver);
	if(checkin)
		eaPush(&checkin->versions, ver);
	sortVersions(dir_entry);

	return ver;
}

Checkin* patchAddCheckin(PatchDB *db, int branch, char *sandbox, char *author, char *comment, U32 filetime, int incr_from)
{
	Checkin	*checkin;

	checkin = StructCreate(parse_Checkin);
	checkin->author		= StructAllocString(author);
	checkin->branch		= branch;
	checkin->comment	= StructAllocString(comment);
	checkin->rev		= eaSize(&db->checkins);
	checkin->sandbox	= StructAllocString(sandbox);
	checkin->time		= filetime;
	checkin->incr_from = incr_from;
	eaPush(&db->checkins,checkin);

	if(checkin->rev)
		assert(db->checkins[checkin->rev-1]->time <= checkin->time);

	if(sandbox && sandbox[0])
	{
		// TODO: If two people try to checkin incrementals to the same sandbox, but with different incr_from values, this'll assert.
		Checkin *previous;
		if(!db->sandbox_stash)
			db->sandbox_stash = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
		else if(stashFindPointer(db->sandbox_stash, checkin->sandbox, &previous))
			assert(previous->incr_from == checkin->incr_from);
		else
			assert(checkin->incr_from < checkin->rev);
		assert(stashAddPointer(db->sandbox_stash, checkin->sandbox, checkin, true));
	}

	return checkin;
}

void checkinFree(Checkin ** checkin)
{
	if(checkin && *checkin)
	{
		eaDestroy(&(*checkin)->versions);
		StructDestroy(parse_Checkin, *checkin);
		*checkin = NULL;
	}
}

Checkin* patchGetSandboxCheckin(PatchDB *db, const char *sandbox)
{
	Checkin *checkin;
	assert(SAFE_DEREF(sandbox));	// it may be appropriate to get the latest non-sandbox checkin
									// but I think that could cause confusion
	if(stashFindPointer(db->sandbox_stash, sandbox, &checkin))
		return checkin;
	return NULL;
}

FileVersion* patchFindVersion(PatchDB *db, const char *dir_name, int branch, char *sandbox, int rev, int incr_from)
{
	DirEntry *dir = patchFindPath(db, dir_name, 0);
	if(dir)
		return patchFindVersionInDir(dir, branch, sandbox, rev, incr_from);
	else
		return NULL;
}

void patchUpdateCheckoutTime(PatchDB* db, U32 branch)
{
	if(eaiUSize(&db->checkout_time) < branch+1)
		eaiSetSize(&db->checkout_time, branch+1);
	assert(db->checkout_time);
	db->checkout_time[branch] = getCurrentFileTime();
}

Checkout * patchAddCheckout(PatchDB *db, DirEntry *dir_entry, const char *author, int branch, const char *sandbox, U32 time)
{
	Checkout *checkout = patchFindCheckoutInDir(dir_entry, branch, sandbox);
	char authorTrimmed[MAX_PATH];
	assertmsgf(checkout == NULL, "%s is already checked out in branch %d", dir_entry->path, branch);
	
	strcpy(authorTrimmed, author);
	removeLeadingAndFollowingSpaces(authorTrimmed);
	author = authorTrimmed;
	
	if(!author[0]){
		printfColor(COLOR_BRIGHT|COLOR_RED,
					"Trying to add checkout to DirEntry with blank author: %s\n",
					dir_entry->path);
		
		return NULL;
	}

	checkout = StructCreate(parse_Checkout);
	checkout->author = StructAllocString(author);
	checkout->branch = branch;
	checkout->sandbox = StructAllocString(sandbox);
	checkout->time = time;
	eaPush(&dir_entry->checkouts, checkout);

	patchUpdateCheckoutTime(db, branch);

	return checkout;
}

bool patchRemoveCheckout(PatchDB *db, DirEntry *dir_entry, int branch, const char *sandbox)
{
	Checkout *checkout = patchFindCheckoutInDir(dir_entry, branch, sandbox);
	if(checkout)
	{
		eaFindAndRemove(&dir_entry->checkouts, checkout);
		StructDestroy(parse_Checkout, checkout);

		{	// FIXME: this is pointless on the client
			assert(branch >= 0); // or /analyze gets upset
			if(ea32Size(&db->checkout_time) < branch+1)
				ea32SetSize(&db->checkout_time, branch+1);
			db->checkout_time[branch] = getCurrentFileTime();
		}

		return true;
	}
	return false;
}

#include "patchdb_h_ast.c"
