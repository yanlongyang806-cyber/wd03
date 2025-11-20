#pragma once
#include "cmdparse.h"

AUTO_ENUM;
typedef enum AutoSettingType
{
	ASTYPE_INT,
	ASTYPE_FLOAT,
	ASTYPE_STRING,
} AutoSettingType;

typedef struct Cmd Cmd;
typedef struct Packet Packet;

bool AutoSetting_CommandIsAutoSettingCommand(Cmd *pCmd, char **ppCategory);
void AutoSetting_GetCmdTypeAndValueString(const Cmd *pCmd, AutoSettingType *pOutType, char **ppOutValString);


AUTO_STRUCT;
typedef struct AutoSetting_ForDataFile
{
	const char *pName; AST(STRUCTPARAM POOL_STRING)
	const char *pCategory; AST(POOL_STRING)
	AutoSettingType eType;
	char *pBuiltInVal; AST(ESTRING)
	const char *pComment;
	bool bEarly;
	const char *pFileName; AST(CURRENTFILE)
} AutoSetting_ForDataFile;

AUTO_STRUCT;
typedef struct AutoSetting_ForDataFile_List
{
	AutoSetting_ForDataFile **ppSettings;
} AutoSetting_ForDataFile_List;


AUTO_ENUM;
typedef enum AutoSettingOrigin
{
	ASORIGIN_UNSPEC,

	//the value was NOT read from a file or set via AUTO_CMD at some point, so retains its original
	//int foo = x; static default value
	ASORIGIN_DEFAULT, 

	//the value was written into the file as the default value, and has remained unchange, and still
	//equals the default value
	ASORIGIN_FILE_DEFAULT, 

	//the value was read from the file and was different from the default value, or at least was
	//manually and consciously set
	ASORIGIN_FILE,

	//the value was set via command at some point. How that command was called will be found in
	//eCmdHowCalled
	ASORIGIN_COMMAND,

	//was set through an AUTO_COMMAND in the special "future" category
	ASORIGIN_FUTURE,

} AutoSettingOrigin;


AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Set, RestoreToDefault, Type, Value, Default, Comment");
typedef struct AutoSetting_SingleSetting
{
	const char *pCmdName; AST(POOL_STRING)
	Cmd *pCmd; NO_AST
	int *pServerTypes; AST(SUBTABLE(GlobalTypeEnum))
	bool bEarly; NO_AST
	AutoSettingType eType;
	const char *pCategory; AST(POOL_STRING FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	char *pCurValueString; AST(NAME(Value), ESTRING)
	char *pDefaultValueString; AST(NAME(Def))
	bool bOfficialValueStringSet; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	char *pOfficialValueString; AST(ESTRING FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	const char *pComment; AST(POOL_STRING) //not really pooled. Sometimes a static pointer, sometimes an estring
	AutoSettingOrigin eOrigin ; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))

	char *pCommandStringForApplyingToOtherServres; AST(ESTRING FORMATSTRING(HTML_SKIP_IN_TABLE = 1)) //cleared whenever the setting changes, recalculated at need. Starts with AUTOSETTING_CONSTSTRING_EARLY
		//or AUTOSETTING_CONSTSTRING_NORMAL, then a space, then a superescaped string containing the entire cmdparse string to execute

	char *pNotes; AST(FORMATSTRING(HTML=1))

	enumCmdContextHowCalled eCmdHowCalled; NO_AST
	AST_COMMAND("Set", "SetAutoSetting $FIELD(CmdName) $STRING(New value) $CONFIRM(Really set $FIELD(CmdName)? It will be saved on disk and apply to future runnings of the shard)")
	AST_COMMAND("RestoreToDefault", "RestoreAutoSetting $FIELD(CmdName) $CONFIRM(Really restore $FIELD(CmdName) to its default value?)")
} AutoSetting_SingleSetting;

#define AUTO_SETTING_CATEGORY_PREFIX "__CAS_"

//the strings the controller generates that specify what AutoSettings to set for each other server begin with one these, ie
//"Early myCommand 7" or "Normal otherCommand 3.5". These can be concatted onto a command prefix name so you end up with
//"AutoCommandCmdLineEarly" or "AutoCommandCmdLineNormal", or a space can be inserted so that "Normal" or "Early" ends up as the
//first argument to a command
#define AUTOSETTING_CONSTSTRING_EARLY "Early"
#define AUTOSETTING_CONSTSTRING_NORMAL "Normal"
#define AUTOSETTING_CONSTSTRING_EARLY_SPACE AUTOSETTING_CONSTSTRING_EARLY " "
#define AUTOSETTING_CONSTSTRING_NORMAL_SPACE AUTOSETTING_CONSTSTRING_NORMAL " "

#define AUTOSETTING_CMDLINECOMMAND_PREFIX "AutoSettingCmdLine"
