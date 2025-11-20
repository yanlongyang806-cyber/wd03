#include "clearsilver.h"
#include "estring.h"
#include "file.h"
#include "MemTrack.h"
#include "sysutil.h"
#include "textparser.h"
#include "timing.h"
#include "fileutil.h"

static char sCustomLoadPath[CRYPTIC_MAX_PATH] = "";

void csSetCustomLoadPath(const char *path)
{
	strcpy(sCustomLoadPath, path);
}

static char **findLoadPaths(char *extra_path)
{
	char **load_paths = NULL;

	PERFINFO_AUTO_START_FUNC();

	eaCreate(&load_paths);

#if !PLATFORM_CONSOLE
	// Add $CWD/templates
	{
		char buf[MAX_PATH];
		char *path = NULL;
		estrPrintf(&path, "%s/templates", fileGetcwd(buf, ARRAY_SIZE_CHECKED(buf)));
		eaPush(&load_paths, path);
	}
#endif

	// Add $EXE/templates
	{
		char buf[MAX_PATH];
		char *path = NULL;
		estrPrintf(&path, "%s/templates", getExecutableDir(buf));
		eaPush(&load_paths, path);
	}

	// Add $extra/templates
	if (extra_path)
	{
		char buf[MAX_PATH];
		char *path = NULL;
		strcpy(buf, extra_path);
		forwardSlashes(buf);
		if(strEndsWith(extra_path, ".c"))
		{
			char *p = strrchr(buf, '/');
			if(p) *p = '\0';
		}
		estrPrintf(&path, "%s/templates", buf);
		eaPush(&load_paths, path);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return load_paths;
}

// Find a file, using the CS template lookup rules.
bool findTemplateFile(char **estrLocation, const char *file, char *extra_path)
{
	char **load_paths = findLoadPaths(extra_path);
	FOR_EACH_IN_EARRAY(load_paths, char, path)
	{
		estrPrintf(estrLocation, "%s/%s", path, file);
		if (fileExists(*estrLocation))
			return true;
	}
	FOR_EACH_END
		eaDestroyEString(&load_paths);
	estrClear(estrLocation);
	return false;
}

typedef struct renderctx {
	char *str;
} renderctx;

static NEOERR* outfunc(renderctx *ctx, char *str)
{
	estrAppend2(&ctx->str, str);
	return STATUS_OK;
}

static void dumpHdf(HDF *hdf, int level, char **out)
{
	int i;
	HDF *node = hdf;
	while(node)
	{
		char *name = hdf_obj_name(node);
		char *value = hdf_obj_value(node);
		HDF *child = hdf_obj_child(node);
		if(!name)
			name = "";

		for(i=0; i<level; i++)
			estrConcatf(out, "  ");
		estrAppend2(out, name);
		
		if(value || !child)
		{
			if(!value) value = "";
			if(!strchr(value, '\n'))
				estrConcatf(out, " = %s", value);
			else
				estrConcatf(out, " = << EOM\n%s\nEOM", value);
		}
		if(child)
		{
			estrConcatf(out, " {\n");
			dumpHdf(child, level+1, out);
			for(i=0; i<level; i++)
				estrConcatf(out, "  ");
			estrConcatf(out, "}\n");
		}
		else
			estrConcatf(out, "\n");
		node = hdf_obj_next(node);
	}
}

static NEOERR *loadClearsilverTemplate(void *ctx, HDF *hdf, const char *filename, char **contents)
{
	NEOERR *err = STATUS_OK;
	char fix_path[CRYPTIC_MAX_PATH];

	if(sCustomLoadPath[0])
	{
		int len;
		sprintf(fix_path, "%s/%s", sCustomLoadPath, filename);
		*contents = fileAlloc(fix_path, &len);

		if(*contents)
		{
			char *p = crt_malloc(len + 1);
			memcpy(p, *contents, len);
			p[len] = 0;
			free(*contents);
			*contents = p;
			return nerr_pass(err);
		}
	}

	// file is not actually a Cryptic path, use the HDF load paths
	err = hdf_search_path(hdf, filename, fix_path);

	if(err != STATUS_OK)
	{
		return nerr_pass(err);
	}

	err = ne_load_file(fix_path, contents);
	return nerr_pass(err);
}

static void freeClearsilverTemplate(void *file)
{
	crt_free(file);
}

char *renderTemplateEx(const char *template_file, ParseTable *tpi, void *struct_mem, bool hdfdump, char *template_path)
{
	renderctx ctx = {0};
	HDF *hdf;
	CSPARSE *parse;
	char **load_paths = findLoadPaths(template_path);
	NEOERR *nerr;
	STRING nerr_str;

	string_init(&nerr_str);

	// Create and populate the HDF
	hdf_init(&hdf);
	ParserWriteHDF(hdf, tpi, struct_mem);
	FOR_EACH_IN_EARRAY(load_paths, char, path)
		hdf_set_value(hdf, STACK_SPRINTF("hdf.loadpaths.%d", FOR_EACH_IDX(load_paths, path)), path);
	FOR_EACH_END

	// Special case for hdfdump
	if(hdfdump)
	{
		char *dump = NULL;
		dumpHdf(hdf_obj_child(hdf), 0, &dump);
		return dump;
	}

	// Load and parse the template
	cs_init(&parse, hdf);
	nerr = cgi_register_strfuncs(parse);
	devassert(nerr == STATUS_OK);
	cs_register_fileload(parse, NULL, loadClearsilverTemplate);
	cs_register_filefree(parse, freeClearsilverTemplate);
	nerr = cs_parse_file(parse, template_file);

	// Render the template or error
	estrCreate(&ctx.str);
	if(nerr == STATUS_OK)
	{
		cs_render(parse, &ctx, outfunc);
	}
	else
	{
		nerr_error_traceback(nerr, &nerr_str);
		estrPrintf(&ctx.str, "Unable to load template file %s: %s", template_file, nerr_str.buf);
	}

	// Cleanup
	eaDestroyEString(&load_paths);
	hdf_destroy(&hdf);
	cs_destroy(&parse);
	string_clear(&nerr_str);

	return ctx.str;
}