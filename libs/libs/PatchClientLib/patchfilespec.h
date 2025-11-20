#ifndef _PATCHFILESPEC_H
#define _PATCHFILESPEC_H
GCC_SYSTEM

typedef struct FileSpecMapper FileSpecMapper;
typedef struct HogFile HogFile;
typedef struct FileVersion FileVersion;
typedef U32 HogFileIndex;

AUTO_ENUM;
typedef enum FileSpecFlag
{
	FILESPEC_NOT_REQUIRED			= (1<<0),	ENAMES(NotRequired)
	FILESPEC_MIRRORED_OUTSIDE_HOGGS	= (1<<1),	ENAMES(Mirrored)
	FILESPEC_NOWARN					= (1<<2),	ENAMES(NoWarn)		// Relavent to Gimme projects
	FILESPEC_INCLUDED				= (1<<3),	ENAMES(Included)	// Relavent to Gimme projects
} FileSpecFlag;

AUTO_ENUM;
typedef enum ControlSpecType
{
	CONTROLSPEC_INCLUDED,	ENAMES(Incl)
	CONTROLSPEC_EXCLUDED,	ENAMES(Excl)
	CONTROLSPEC_BINFILES,	ENAMES(Bins)
} ControlSpecType;

AUTO_STRUCT;
typedef struct Spec
{
	char * spec;
} Spec;

AUTO_STRUCT;
typedef struct HogSpec
{
	char * filename;
	char * strip;
	Spec ** filespecs;
	bool mirror_stripped;
} HogSpec;

AUTO_STRUCT;
typedef struct FlagSpecLine
{
	char * spec;
	int exclude; AST(NAME(Exclude))
} FlagSpecLine;

AUTO_STRUCT;
typedef struct FlagSpec
{
	FileSpecFlag flag;
	FlagSpecLine ** filespecs;
} FlagSpec;

AUTO_STRUCT;
typedef struct ControlSpecLine
{
	char *spec;				AST(STRUCTPARAM)
	ControlSpecType type;	AST(STRUCTPARAM)
} ControlSpecLine;

AUTO_STRUCT;
typedef struct ControlSpec
{
	ControlSpecLine **specs; AST(NAME(spec))
} ControlSpec;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct FileSpecs
{
	HogSpec ** hoggspecs;
	FlagSpec ** flagspecs;
	ControlSpec controlspecs;
} FileSpecs;

typedef void (*fileSpecLogFptr)(const char *string, void *loggerdata);

FileSpecMapper * fileSpecLoad(const char * filename, const char *extra_filename, bool createHoggs, const char ** root_folders, U32 *hogg_flags, fileSpecLogFptr logger, void *loggerdata);
FileSpecMapper * fileSpecLoadFromData(const char * data, size_t data_len, bool createHoggs, const char ** root_folders, U32 *hogg_flags, fileSpecLogFptr logger, void *loggerdata);
bool			 fileSpecPreprocessFile(const char * input_filename, const char * output_filename);
void			 fileSpecSetRoot(FileSpecMapper * fsm, const char ** root_folders, U32 *hogg_flags);
void             fileSpecLoadHoggs(FileSpecMapper * fsm);
bool			 fileSpecUnloadHoggs(FileSpecMapper * fsm);
bool			 fileSpecHasHoggsLoaded(FileSpecMapper * fsm);
int              fileSpecHoggCount(FileSpecMapper * fsm);
int				 fileSpecFolderCount(FileSpecMapper * fsm);
void			 fileSpecGetHoggName(FileSpecMapper * fsm, U32 folder_index, U32 hogg_index, char * buf, int buf_size);
const char *	 fileSpecGetHoggNameNoPath(FileSpecMapper * fsm, int index);
#define          fileSpecGetHoggHandle(fsm, index) fileSpecGetHoggHandleEx((fsm), 0, (index))
HogFile *		 fileSpecGetHoggHandleEx(FileSpecMapper * fsm, int folder_index, int hogg_index);
int              fileSpecGetHoggIndexForHandle(FileSpecMapper * fsm, HogFile * hogg);
char *           fileSpecGetStripPath(FileSpecMapper * fsm, int index);
bool             fileSpecGetMirrorStriped(FileSpecMapper * fsm, int index);
char *           fileSpecGetHoggNameForFile(FileSpecMapper * fsm, const char * filename);
int              fileSpecGetHoggIndexForFile(FileSpecMapper * fsm, const char * filename);
HogFile *        fileSpecGetHoggHandleForFile(FileSpecMapper * fsm, const char * filename);
int				 fileSpectGetFolderIndexForFileVersion(FileSpecMapper * fsm, FileVersion * ver, int hogg_index, bool allow_inexact, HogFileIndex * hfi_out);
#define          fileSpecGetHoggHandleForFileVersion(fsm, ver) fileSpecGetHoggHandleForFileVersionEx((fsm), (ver), false, false, NULL, NULL, NULL)
HogFile *        fileSpecGetHoggHandleForFileVersionEx(FileSpecMapper * fsm, FileVersion * ver, bool allow_inexact, bool useFileOverlay, HogFileIndex * hfi_out, char (* full_path)[MAX_PATH], U32 * check_out);
void			 fileSpecFlushHoggs(FileSpecMapper * fsm);
void             fileSpecDestroy(FileSpecMapper ** fsm);
bool             fileSpecIsHogFile(FileSpecMapper * fsm, const char * filename);
bool             fileSpecIsMirrored(FileSpecMapper * fsm, const char * filename);
bool             fileSpecIsNotRequired(FileSpecMapper * fsm, const char * filename);
bool             fileSpecIsNotRequiredDebug(FileSpecMapper * fsm, const char * filename, char ** estrDebug);
void			 fileSpecSetSingleAppMode(FileSpecMapper * fsm, bool singleAppMode);
bool fileSpecIsAHogg(FileSpecMapper *fsm, const char *filename);
bool fileSpecHasNotRequired(FileSpecMapper *fsm);
bool fileSpecIsIncluded(FileSpecMapper *fsm, const char *filename);
bool fileSpecIsUnderSourceControl(FileSpecMapper *fsm, const char *filename);
bool fileSpecIsBin(FileSpecMapper *fsm, const char *filename);
bool fileSpecIsNoWarn(FileSpecMapper *fsm, const char *filename);

#endif
