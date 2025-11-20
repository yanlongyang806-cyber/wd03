#include "BlockEarray.h"
#include "filespecmap.h"
#include "StashTable.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "textparser.h"
#include "timing.h"

#ifndef PLATFORM_CONSOLE
#define PCRE_STATIC
#include "pcre.h"
#else
typedef struct pcre pcre;
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

enum FilespecMapElementType {
	ElementNone = 0,
	ElementExact,
	ElementPrefix,
	ElementSuffix,
	ElementSimple,
	ElementGlob,
	ElementRegex
};

typedef struct FilespecMapElement
{
	char *filespec;
	pcre *re;
	void *data;

	enum FilespecMapElementType type;
	bool isExt;
	int numSlashes;
} FilespecMapElement;

typedef struct FilespecMap
{
	FilespecMapElement *elements;
	StashTable exactElements;
	bool noSorting;
	bool universalSet;					// Hint that there is a filespec that will match everything, if flattened.
	bool emptySet;						// There are no filespecs, if flattened.
} FilespecMap;

FilespecMap *filespecMapCreate(void)
{
	FilespecMap *ret = calloc(1, sizeof(FilespecMap));
	ret->exactElements = stashTableCreateWithStringKeys(0, StashDefault);
	return ret;
}

FilespecMap *filespecMapCreateNoSorting(void)
{
	FilespecMap *ret = filespecMapCreate();
	ret->noSorting = 1;
	return ret;
}

static void filespecMapFree(void *ptr)
{
	free(ptr);
}

void filespecMapDestroy(FilespecMap *handle)
{
	int i;
	for (i=0; i<beaSize(&handle->elements); i++)
		SAFE_FREE(handle->elements[i].filespec);
	stashTableDestroyEx(handle->exactElements, NULL, filespecMapFree);
	beaDestroy(&handle->elements);
	free(handle);
}

// Test if a filespace matches everything.
// Warning: This might return false in case where it should return true, since this is a hard problem in general, although hopefully not in any important cases.
static bool filespecMatchesEverything(const FilespecMapElement *larger)
{
	return larger->type == ElementPrefix && !*larger->filespec;
}

// Test if one filespec covers another.
static bool filespecIsCovered(const FilespecMapElement *larger, const char *smaller)
{
	if (larger->type == ElementPrefix)
		return strStartsWith(larger->filespec, smaller);
	return matchExactSensitive(larger->filespec, smaller);
}

// Create a new FilespecMap from an existing one, flattening it by removing all value information, retaining only information about what is included.
// This allows optimizing filespecMap operations where we only care about inclusion, not the actual value, since overlapping operations can be reduced.
FilespecMap *filespecMapFlatten(const FilespecMap *f, int value)
{
	FilespecMap *ret = filespecMapCreate();
	int i, j;
	bool empty = true;

	ret->noSorting = f->noSorting;
	
	// Copy elements that are not redundant.
	for (i = 0; i < beaSize(&f->elements); i++)
	{
		FilespecMapElement *elem;
		bool covered = false;

		// Check if this is covered by a later filespec.  If so, skip it.
		for (j = i + 1; j < beaSize(&f->elements); j++)
		{
			if (filespecIsCovered(&f->elements[j], f->elements[i].filespec))
			{
				covered = true;
				break;
			}
		}
		if (covered)
			continue;
		
		// If the filespec matches everything, set the hint for this.
		if (filespecMatchesEverything(&f->elements[i]))
			ret->universalSet = true;

		// Copy the filespec, since it is not covered by a later filespec.
		elem = beaPushEmpty(&ret->elements);
		elem->filespec = strdup(f->elements[i].filespec);
		assertmsg(!elem->re, "unsupported");
		elem->data = (void *)value;
		elem->type = f->elements[i].type;
		elem->isExt = f->elements[i].isExt;
		elem->numSlashes = f->elements[i].numSlashes;
		empty = false;
	}

	// Copy exact elements that are not redundant.
	FOR_EACH_IN_STASHTABLE2(f->exactElements, el);
	{
		const char *key = stashElementGetKey(el);
		FilespecMapElement *old = stashElementGetPointer(el);
		bool covered = false;

		// Check if any element covers this exact element.
		for (i = 0; i < beaSize(&ret->elements); i++)
		{
			if (filespecIsCovered(&ret->elements[i], key))
			{
				covered = true;
				break;
			}
		}

		// Copy the exact element only if it isn't covered by a rule we already have.
		if (!covered)
		{
			FilespecMapElement *elem = malloc(sizeof(*elem));
			elem->filespec = strdup(old->filespec);
			assertmsg(!old->re, "unsupported");
			elem->data = (void *)value;
			devassert(old->type == ElementExact);
			elem->type = old->type;
			devassert(old->isExt);
			elem->isExt = old->isExt;
			elem->numSlashes = old->numSlashes;
			stashAddPointer(ret->exactElements, elem->filespec, elem, false);
			empty = false;
		}
	}
	FOR_EACH_END;

	// If there are no filespecs, set the hint for this.
	ret->emptySet = empty;

	return ret;
}

#define b2i(b) ((b)?1:0)
int sortByNumSlashes(const FilespecMapElement *a, const FilespecMapElement *b)
{
	//if ((a->type == ElementExact) != (b->type == ElementExact)) {
	//	// Explicit specs first!
	//	return b2i(b->type == ElementExact) - b2i(a->type == ElementExact);
	//}
	if (a->isExt != b->isExt) {
		// Those with extensions first!
		return b2i(b->isExt) - b2i(a->isExt);
	}
	if (a->numSlashes != b->numSlashes) {
		// Those with more slashes first!
		return b->numSlashes - a->numSlashes;
	}

	return 0;
}
#undef b2i

static void convertGlobToRegex(const char *glob, char *re_out, U32 re_out_len)
{
	const char *c;
	
	if(!glob)
		glob = "";

	strcpy_s(re_out, re_out_len, "^");
	for(c=glob; *c; c++)
	{
		if(*c == '*')
			strcat_s(re_out, re_out_len, ".*");
		else if(*c == '?')
			strcat_s(re_out, re_out_len, ".");
		else if(*c == '.')
			strcat_s(re_out, re_out_len, "\\.");
		else if(*c == '(')
			strcat_s(re_out, re_out_len, "\\(");
		else if(*c == ')')
			strcat_s(re_out, re_out_len, "\\)");
		else if(*c == '\\') // We tend to go back and forth with slashs, to both are equivalent
			strcat_s(re_out, re_out_len, "[/\\\\]");
		else if(*c == '/')
			strcat_s(re_out, re_out_len, "[/\\\\]");
		else
			strncat_s(re_out, re_out_len, c, 1);
	}
	strcat_s(re_out, re_out_len, "$");
}

// Return true if this glob can be matched with match().
static bool isGlobMatchCandidate(const char *glob)
{
	// Currently, never use PCRE, as it's too slow.  Maybe in the future we'll want to take advantage
	// of one of its features that actually makes the performance hit worthwhile.  Alternately, we may
	// just want to remove PCRE.
	return true;
}

void filespecMapAdd(FilespecMap *handle, const char *spec_in, void *data)
{
	char spec[CRYPTIC_MAX_PATH];
	int size;
	FilespecMapElement *elem;
	char *star;
	char *slash;
	int i;

	PERFINFO_AUTO_START_FUNC();

	// Clean up input
	if (spec_in[0]=='/' || spec_in[0]=='\\')
		spec_in++;
	Strncpyt(spec, spec_in);
	forwardSlashes(spec);
	string_toupper(spec);

	// Search for duplicates
	size = beaSize(&handle->elements);
	for (i=0; i<size; i++) {
		FilespecMapElement *elem2 = handle->elements + i;
		if (stricmp(elem2->filespec, spec)==0) {
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
	}

	// Get sort parameters.
	elem = beaPushEmpty(&handle->elements);
	slash = spec-1;
	while (slash = strchr(slash+1, '/'))
		elem->numSlashes++;
	if (!strEndsWith(spec, "*")) {
		elem->isExt = true;
	}

	// Add element.
	// This might modify the filespec string.
	star = strrchr(spec, '*');
	if (star == NULL) {
		elem->type = ElementExact;
	} else if(star == spec + strlen(spec) - 1 && star == strchr(spec, '*')) {
		*star = '\0';
		elem->type = ElementPrefix;
	} else if(spec[0] == '*' && !strchr(spec + 1, '*')) {
		elem->type = ElementSuffix;
	} else if(strchr(spec, '*') == star) {
		elem->type = ElementSimple;
	} else if(isGlobMatchCandidate(spec)) {
		elem->type = ElementGlob;
	} else {
		// Compile a regex version of the glob
		char regex_str[MAX_PATH];
		char *errorstr;
		int erroroff;
		elem->type = ElementRegex;
		convertGlobToRegex(spec, SAFESTR(regex_str));
#ifndef PLATFORM_CONSOLE
		elem->re = pcre_compile(regex_str, PCRE_CASELESS, &errorstr, &erroroff, NULL);
#endif // !PLATFORM_CONSOLE
		assertmsgf(elem->re, "Error compiling regex \"%s\": %s", regex_str, errorstr);
	}
	elem->filespec = strdup(spec);
	elem->data = data;

	// Put exact elements into a StashTable.
	if (elem->type == ElementExact)
	{
		FilespecMapElement *elem_copy = malloc(sizeof(*elem_copy));
		memcpy(elem_copy, elem, sizeof(*elem_copy));
		beaRemove(&handle->elements, elem - handle->elements);
		stashAddPointer(handle->exactElements, elem_copy->filespec, elem_copy, false);
	}

	// Sort elements.
	else if(!handle->noSorting)
		qsort(handle->elements, beaSize(&handle->elements), sizeof(*handle->elements), sortByNumSlashes);

	PERFINFO_AUTO_STOP_FUNC();
}

static bool filespecMapGetEx(FilespecMap *handle, const char *filename, void **result, size_t *count, const char **match)
{
	int i;
	int size, total_size;
	FilespecMapElement *elem;
	int filename_length;
	char *upper_filename;
	U32 exact_element_count;

// If count is non-null, count matches instead of returning.
#define RETURN_TRUE						\
	do {								\
		if (match)						\
			*match = elem->filespec;	\
		if (!count)						\
		{								\
			PERFINFO_AUTO_STOP_L2();	\
			return true;				\
		}								\
		++*count;						\
	} while (0)

	// Return if the filespec is empty.
	size = beaSize(&handle->elements);
	exact_element_count = stashGetCount(handle->exactElements);
	total_size = size + exact_element_count;
	if (!total_size)
	{
		*result = NULL;
		return false;
	}

	// Check for an exact match first.
	if (exact_element_count && stashFindPointer(handle->exactElements, filename, &elem))
	{
		*result = elem->data;
		if (match)
			*match = elem->filespec;
		if (!count)
			return true;
		++*count;
	}

	// Canonicalize the filename into upper case, for comparison performance.
	filename_length = (int)strlen(filename);
	upper_filename = alloca(filename_length + 1);
	for (i = 0; i != filename_length + 1; ++i)
		upper_filename[i] = toupper(filename[i]);
	
	// This slowed down the function somewhat; we now rely on the caller to do this.
	//forwardSlashes(upper_filename);

	for (i=0; i<size; i++) {
		int rv;

		elem = handle->elements + i;
		
		switch (elem->type)
		{

			//case ElementExact:
			//	PERFINFO_AUTO_START_L2("filespecMapGet:exact", 1);
			//	if(strcmp(elem->filespec, upper_filename)==0)
			//	{
			//		// It matched!
			//		*result = elem->data;
			//		RETURN_TRUE;
			//	}
			//	PERFINFO_AUTO_STOP_L2();
			//	break;

			case ElementPrefix:

				PERFINFO_AUTO_START_L2("filespecMapGet:prefix", 1);
				if(strStartsWith(upper_filename, elem->filespec))
				{
					// It matched!
					*result = elem->data;
					RETURN_TRUE;
				}
				PERFINFO_AUTO_STOP_L2();
				break;

			case ElementSuffix:

				PERFINFO_AUTO_START_L2("filespecMapGet:suffix", 1);
				if(strEndsWithSensitive(upper_filename, elem->filespec + 1))
				{
					// It matched!
					*result = elem->data;
					RETURN_TRUE;
				}
				PERFINFO_AUTO_STOP_L2();
				break;

			case ElementSimple:

				PERFINFO_AUTO_START_L2("filespecMapGet:simple", 1);
				if(simpleMatchExactSensitiveFast(elem->filespec, upper_filename))
				{
					// It matched!
					*result = elem->data;
					RETURN_TRUE;
				}
				PERFINFO_AUTO_STOP_L2();
				break;

			case ElementGlob:

				PERFINFO_AUTO_START_L2("filespecMapGet:glob", 1);
				if(matchExactSensitive(elem->filespec, upper_filename))
				{
					// It matched!
					*result = elem->data;
					RETURN_TRUE;
				}
				PERFINFO_AUTO_STOP_L2();
				break;
 
			case ElementRegex:

				PERFINFO_AUTO_START_L2("filespecMapGet:regex", 1);
				rv = pcre_exec(elem->re, NULL, filename, (int)strlen(filename), 0, 0, NULL, 0);
				if(rv >= 0)
				{
					// It matched!
					*result = elem->data;
					RETURN_TRUE;
				}
				PERFINFO_AUTO_STOP_L2();
				break;

			default:
				devassert(0);
		}
	}
	*result = NULL;

#undef RETURN_TRUE

	return false;
}

// Check for a match, without getting the actual value.
bool filespecMapCheckFlat(FilespecMap *handle, const char *filename)
{
	void *dummy;
	if (handle->universalSet)
		return true;
	if (handle->emptySet)
		return false;
	return filespecMapGetEx(handle, filename, &dummy, NULL, NULL);
}

bool filespecMapGet(FilespecMap *handle, const char *filename, void **result)
{
	return filespecMapGetEx(handle, filename, result, NULL, NULL);
}

size_t filespecMapGetCount(FilespecMap *handle, const char *filename)
{
	void *dummy;
	size_t result = 0;
	filespecMapGetEx(handle, filename, &dummy, &result, NULL);
	return result;
}

const char *filespecMapGetMatchFilespec(FilespecMap *handle, const char *filename)
{
	void *dummy;
	const char *result = NULL;
	filespecMapGetEx(handle, filename, &dummy, NULL, &result);
	return result;
}

bool filespecMapGetExact(FilespecMap *handle, const char *spec, void **result)
{
	int i;
	for (i=0; i<beaSize(&handle->elements); i++) {
		FilespecMapElement *elem = handle->elements + i;
		if (stricmp(elem->filespec, spec)==0) {
			*result = elem->data;
			return true;
		}
	}
	*result = NULL;
	return false;
}


void filespecMapAddInt(FilespecMap *handle, const char *spec, int data)
{
	filespecMapAdd(handle, spec, (void*)(uintptr_t)data);
}

bool filespecMapGetInt(FilespecMap *handle, const char *filename, int *result)
{
	uintptr_t res;
	bool ret = filespecMapGet(handle, filename, (void**)&res);
	if(result)
	{
		*result = (int)res;
	}
	return ret;
}

bool filespecMapGetExactInt(FilespecMap *handle, const char *spec, int *result)
{
	return filespecMapGetExact(handle, spec, (void**)result);
}

int filespecMapGetNumElements(FilespecMap *handle)
{
	return beaSize(&handle->elements);
}



ParseTable parse_filespec_include[] = {
	{ "doInclude",	TOK_BOOL(SimpleFileSpecEntry,doInclude, 1)},
	{ "filespec",	TOK_STRUCTPARAM|TOK_POOL_STRING|TOK_FILENAME(SimpleFileSpecEntry,filespec,0)},
	{ "\n",			TOK_END,			0},
	{ "", 0, 0 }
};
ParseTable parse_filespec_exclude[] = {
	{ "doInclude",	TOK_BOOL(SimpleFileSpecEntry,doInclude, 0)},
	{ "filespec",	TOK_STRUCTPARAM|TOK_POOL_STRING|TOK_FILENAME(SimpleFileSpecEntry,filespec,0)},
	{ "\n",			TOK_END,			0},
	{ "", 0, 0 }
};

static ParseTable parse_filespec_list[] = {
	{ "Include:",	TOK_STRUCT_X | TOK_EARRAY | TOK_INDIRECT,	0,	sizeof(SimpleFileSpecEntry),		parse_filespec_include},
	{ "Exclude:",	TOK_REDUNDANTNAME | TOK_STRUCT_X | TOK_EARRAY | TOK_INDIRECT,	0,	sizeof(SimpleFileSpecEntry),		parse_filespec_exclude },
	{ "", 0, 0 }
};

static ParseTable parse_filespectree[] = {
	{ "filespec",	TOK_STRUCTPARAM|TOK_POOL_STRING|TOK_STRING(FileSpecTree,filespec,0)},
	{ "Folder",	TOK_STRUCT(FileSpecTree, folders, parse_filespectree)},
	{ "Incl",	TOK_STRUCT(FileSpecTree, entries, parse_filespec_include)},
	{ "Excl",	TOK_REDUNDANTNAME | TOK_STRUCT(FileSpecTree, entries, parse_filespec_exclude) },
	{ "EndFolder",	TOK_END,			0},
	{ "", 0, 0 }
};


AUTO_RUN;
void initFilespecTPIs(void)
{
	ParserSetTableInfo(parse_filespec_list, sizeof(SimpleFileSpecEntry**), "parse_filespec_list", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_filespec_exclude, sizeof(SimpleFileSpecEntry), "parse_filespec_exclude", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_filespec_include, sizeof(SimpleFileSpecEntry), "parse_filespec_include", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_filespectree, sizeof(FileSpecTree), "parse_filespectree", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}


//static bool filespec_all_simple_exclude;
SimpleFileSpec *simpleFileSpecLoad(const char *path, const char *prefixToStrip)
{
	SimpleFileSpecEntry **ret=NULL;
	int i;
	ParserLoadFiles(NULL, path, NULL, 0, parse_filespec_list, &ret);
	if (eaSize(&ret))
	{
//		filespec_all_simple_exclude = true;
		// Only care about things that start with our prefix
		for (i=eaSize(&ret)-1; i>=0; i--) 
		{
			if (strchr(ret[i]->filespec, '\\'))
			{
				char buf[MAX_PATH];
				strcpy(buf, ret[i]->filespec);
				forwardSlashes(buf);
				ret[i]->filespec = allocAddFilename(buf);
			}
			if (prefixToStrip && strStartsWith(ret[i]->filespec, prefixToStrip))
			{
				const char *news = allocAddFilename(ret[i]->filespec +strlen(prefixToStrip));
				ret[i]->filespec = news;
// 				if (!strEndsWith(news, "*"))
// 					filespec_all_simple_exclude = false;
			} else if (strStartsWith(ret[i]->filespec, "*"))
			{
// 				filespec_all_simple_exclude = false;
			} else if (prefixToStrip) {
				// Starts with a non-wildcard other than our prefix, ignore it for our purposes
				StructDestroyVoid(parse_filespec_include, ret[i]);
				eaRemove(&ret, i);
			}
		}

		if (ret[eaSize(&ret)-1]->doInclude && stricmp(ret[eaSize(&ret)-1]->filespec, "*")==0)
		{
			SimpleFileSpecEntry *elem = eaPop(&ret);
			StructDestroyVoid(parse_filespec_include, elem);
		}
// 		for (i=0; i<eaSize(&ret); i++) 
// 		{
// 			if (ret[i]->doInclude)
// 				filespec_all_simple_exclude = false;
// 			//printf("  %s %s\n", ret[i]->doInclude?"Include":"Exclude", ret[i]->filespec);
// 		}
// 		if (filespec_all_simple_exclude)
// 		{
// 			// Could sort for optimalness so that we only need to check one or two entries below
// 		}
	}
	if (ret)
	{
		SimpleFileSpec *spec;
		spec = callocStruct(SimpleFileSpec);
		spec->entries = ret;
		return spec;
	}
	return NULL;
}

bool simpleFileSpecExcludesFile(const char *path, SimpleFileSpec *filespec)
{
	int i;
	if (!filespec)
		return false;
	for (i=0; i<eaSize(&filespec->entries); i++)
	{
		if (simpleMatchExact(filespec->entries[i]->filespec, path))
		{
			return !filespec->entries[i]->doInclude;
		}
	}
	return false;
}

bool simpleFileSpecIncludesFile(const char *path, SimpleFileSpec *filespec)
{
	int i;
	if (!filespec)
		return false;
	for (i=0; i<eaSize(&filespec->entries); i++)
	{
		if (simpleMatchExact(filespec->entries[i]->filespec, path))
		{
			return filespec->entries[i]->doInclude;
		}
	}
	return false;
}

void simpleFileSpecDestroy(SimpleFileSpec *filespec)
{
	if (filespec)
	{
		StructDeInitVoid(parse_filespec_list, &filespec->entries);
		free(filespec);
	}
}


FileSpecTree *fileSpecTreeLoad(const char *path)
{
	FileSpecTree ret = {0};
	ParserLoadFiles(NULL, path, NULL, 0, parse_filespectree, &ret);
	if (eaSize(&ret.entries) || eaSize(&ret.folders))
	{
		FileSpecTree *spec;
		spec = callocStruct(FileSpecTree);
		*spec = ret;
		return spec;
	} else {
		StructDeInitVoid(parse_filespectree, &ret);
		return NULL;
	}
}

FileSpecTree *fileSpecTreeGetAction(const FileSpecTree *fstree, const char *path) // path should not have any '/'s in it.
{
	int i;
	if (!fstree)
		return FST_NOT_SPECIFIED;
	if (fstree == FST_EXCLUDED)
		return FST_EXCLUDED;
	for (i=0; i<eaSize(&fstree->folders); i++)
	{
		if (simpleMatchExact(fstree->folders[i]->filespec, path))
		{
			return fstree->folders[i];
		}
	}
	for (i=0; i<eaSize(&fstree->entries); i++)
	{
		if (simpleMatchExact(fstree->entries[i]->filespec, path))
		{
			return fstree->entries[i]->doInclude?FST_INCLUDED:FST_EXCLUDED;
		}
	}
	return FST_NOT_SPECIFIED;
}
