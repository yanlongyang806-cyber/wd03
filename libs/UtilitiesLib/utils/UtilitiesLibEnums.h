#pragma once

typedef enum VolumeFaces
{
	VOLFACE_POSX = 1<<0,
	VOLFACE_NEGX = 1<<1,
	VOLFACE_POSY = 1<<2,
	VOLFACE_NEGY = 1<<3,
	VOLFACE_POSZ = 1<<4,
	VOLFACE_NEGZ = 1<<5,
	VOLFACE_ALL = VOLFACE_POSX|VOLFACE_NEGX|VOLFACE_POSY|VOLFACE_NEGY|VOLFACE_POSZ|VOLFACE_NEGZ,
} VolumeFaces;

AUTO_ENUM AEN_NO_PREFIX_STRIPPING;
typedef enum AccessLevel {
	ACCESS_USER = 0,    // regular users, lowest access level
	ACCESS_UGC = 2,		// regular user, but is allowed limited debug access for UGC needs
	ACCESS_GM = 4,      // Regular GM access, this is defined as the first non-user level
	ACCESS_GM_FULL = 7, // GM Lead access
	ACCESS_DEBUG = 9,	// debug should be the highest external access level.
	ACCESS_CRASH = 10,
	ACCESS_INTERNAL = 11,
} AccessLevel;
extern StaticDefineInt AccessLevelEnum[];

typedef enum CmdContextFlag
{
	//if this flag is set, and the PRINTVARS code is hit (ie, someone reads from a variable instead of setting it
	//by executing "varname" instead of "varname val", then treat that as a success, and return the value read, rather
	//than treating it as an error and dumping the value
	CMD_CONTEXT_FLAG_TREAT_PRINTVARS_AS_SUCCESS = 1,

	//if this flag is set, all strings are treated as escaped
	CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED = 1 << 1,

	//if this flag is set, then only commands with CMDF_COMMANDLINE can be executed
	CMD_CONTEXT_FLAG_COMMAND_LINE_CMDS_ONLY = 1 << 2,

	//if this flag is set, then commands with CMDF_EARLYCOMMANDLINE will not be executed (so that commands that are both
	//earlycommandline and global don't get double executed)
	CMD_CONTEXT_FLAG_NO_EARLY_COMMAND_LINE = 1 << 3,

	//if a non-access-level-0 command is executed, log it. (Should be used for all potentially untrustworthy command sources,
	//particularly clients but also server monitored commands).
	CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL = 1 << 4,

	//if true, this command is being requested via server monitoring (which is logged)
	CMD_CONTEXT_FLAG_FROM_SERVER_MONITORING = 1 << 5,

	//if true, this command is being executed on the command line
	CMD_CONTEXT_FLAG_FROM_COMMAND_LINE = 1 << 6,

	// if this flag is set, struct parameters will be textparsered with PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE
	// which will ignore all unknown fields and NOT output any errors
	CMD_CONTEXT_FLAG_IGNORE_UNKNOWN_FIELDS = 1 << 7,
} CmdContextFlag;
