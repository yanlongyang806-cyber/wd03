#include "structInternals.h"
#include "tokenStore.h"
#include "StringCache.h"
#include "serialize.h"
#include "fileCache.h"
#include "timing.h"
#include "StringUtil.h"
#include "textParserCallbacks_inline.h"
#include "FolderCache.h"

#if _PS3
#define u8_writehdf 0
#define int16_writehdf 0
#define int_writehdf 0
#define int64_writehdf 0
#define float_writehdf 0
#define string_writehdf 0
#define struct_writehdf 0
#define nonarray_writehdf 0
#define fixedarray_writehdf 0
#define earray_writehdf 0
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

////////////////////////////////////////////////// sanity checks
void TestParseTable(ParseTable pti[])
{
	int i;
	PERFINFO_AUTO_START_FUNC();
	FORALL_PARSETABLE(pti, i)
	{
		devassert(TYPE_INFO(pti[i].type).type == TOK_GET_TYPE(pti[i].type));
		if (TYPE_INFO(pti[i].type).storage_compatibility)
			devassert(TokenStoreIsCompatible(pti[i].type, TYPE_INFO(pti[i].type).storage_compatibility));
	}
	PERFINFO_AUTO_STOP();
}

/////////////////////////////////////////////////////////////////////// g_tokentable

// this function marks the end of the table - (it helps produce a compiler
// error if you forget one of the functions)
void ENDTABLE(F32 f, int i, void* p) { }

// these are the basic types of storage compatibility
#define TOK_STORAGE_NUMBERS		(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY)	// U8 member, U8 members[3]
#define TOK_STORAGE_NUMBERS_32	(TOK_STORAGE_NUMBERS | TOK_STORAGE_DIRECT_EARRAY)	// also int* - must be 32-bits for earrays
#define TOK_STORAGE_STRINGS		(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY) // char str[128], char* str, char** strs
#define TOK_STORAGE_POLYS		(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE  | TOK_STORAGE_INDIRECT_EARRAY)	// Mystruct s, Mystruct* s, Mystruct** s
#define TOK_STORAGE_STRUCTS		(TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_DIRECT_EARRAY | TOK_STORAGE_INDIRECT_EARRAY)	// Mystruct s, Mystruct* s, MyStruct *s AST(BLOCK_EARRAY),  Mystruct** s

// ALL ENTRIES IN-ORDER
TokenTypeInfo g_tokentable[NUM_TOK_TYPE_TOKENS] = {
	{ TOK_IGNORE,	0,	
		"IGNORE", 0, 0, 0, 0, 0, 
		MULTI_NONE,
		ignore_interpretfield,
		0,	// initstruct
		0,	// destroystruct
		0,	// preparse
		ignore_parse,
		0,	// writetext
		0,	// writetext
		0,	// writebin
		0,	// readbin
		0,  // writehdiff
		0,  // writebdiff
		0,	// senddiff
		0,	// recvdiff
		0,	// bitpack
		0,	// unbitpack
		0,	// writestring
		0,	// readstring
		0,	// tomulti
		0,	// frommulti
		0,	// updatecrc
		0,	// compare
		0,	// memusage
		0,	// calcoffset
		0,	// copystruct
		0,	// copyfield
		0,	// copyfield2tpis
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		0,  // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  //textdiffwithnull
		ENDTABLE
	},
	{ TOK_START,	0,
		"START", 0, 0, 0, 0, 0,	
		MULTI_NONE,
		ignore_interpretfield,
		0,	// initstruct
		0,	// destroystruct
		0,	// preparse
		0,  // parse
		0,  // writetext
		0,	// writebin
		0,	// readbin
		0,  // writehdiff
		0,  // writebdiff
		0,	// senddiff
		0,	// recvdiff
		0,	// bitpack
		0,	// unbitpack
		0,	// writestring
		0,	// readstring
		0,	// tomulti
		0,	// frommulti
		0,	// updatecrc
		0,	// compare
		0,	// memusage
		0,	// calcoffset
		0,	// copystruct
		0,	// copyfield
		0,	// copyfield2tpis
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		0,  // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  // writehdf
		0,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_END,		0,
		"END", 0, 0, 0, 0, 0,	
		MULTI_NONE,
		ignore_interpretfield,
		0,	// initstruct
		0,	// destroystruct
		0,	// preparse
		end_parse,
		0,	// writetext
		0,	// writetext
		0,	// writebin
		0,	// readbin
		0,  // writehdiff
		0,  // writebdiff
		0,	// senddiff
		0,	// recvdiff
		0,	// bitpack
		0,	// unbitpack
		0,	// writestring
		0,	// readstring
		0,	// tomulti
		0,	// frommulti
		0,	// updatecrc
		0,	// compare
		0,	// memusage
		0,	// calcoffset
		0,	// copystruct
		0,	// copyfield
		0,	// copyfield2tpis
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		0,  // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  //textdiffwithnull
		
		ENDTABLE
	},

	// primitives
	{ TOK_U8_X,		TOK_STORAGE_NUMBERS,
		"U8", "U8FIXEDARRAY", 0, 0, 0, 0,	
		MULTI_INT,
		number_interpretfield,
		u8_initstruct,
		0,	// destroystruct
		0,	// preparse
		u8_parse,
		u8_writetext,
		u8_writebin,
		u8_readbin,
		u8_writehdiff,
		u8_writebdiff,
		u8_senddiff,
		u8_recvdiff,
		int_bitpack,
		int_unbitpack,
		u8_writestring,
		u8_readstring,
		u8_tomulti,	
		u8_frommulti,
		u8_updatecrc,
		u8_compare,
		0,	// memusage
		u8_calcoffset,
		0,	// copystruct
		u8_copyfield,
		u8_copyfield2tpis,
		0,	// endianswap
		u8_interp,
		u8_calcrate,
		u8_integrate,
		u8_calccyclic,
		u8_applydynop,
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		u8_writexmlfile,	// writexmlfile
		u8_writejsonfile,	// writejsonfile
		u8_queryCopyStruct,
		0,  // newFieldCopy
		0,	//earrayCopy
		u8_writehdf, 
		u8_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_INT16_X,	TOK_STORAGE_NUMBERS,
		"INT16", "INT16FIXEDARRAY", 0, 0, 0, 0,	
		MULTI_INT,
		number_interpretfield,
		int16_initstruct,
		0,	// destroystruct
		0,	// preparse
		int16_parse,
		int16_writetext,
		int16_writebin,
		int16_readbin,
		int16_writehdiff,
		int16_writebdiff,
		int16_senddiff,
		int16_recvdiff,
		int_bitpack,
		int_unbitpack,
		int16_writestring,
		int16_readstring,
		int16_tomulti,
		int16_frommulti,
		int16_updatecrc,
		int16_compare,
		0,	// memusage
		int16_calcoffset,
		0,	// copystruct
		int16_copyfield,
		int16_copyfield2tpis,
		int16_endianswap,
		int16_interp,
		int16_calcrate,
		int16_integrate,
		int16_calccyclic,
		int16_applydynop,
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		int16_writexmlfile,	// writexmlfile
		int16_writejsonfile,	// writejsonfile
		int16_queryCopyStruct,
		0,  // newFieldCopy
		0,	//earrayCopy
		int16_writehdf,
		int16_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_INT_X,	TOK_STORAGE_NUMBERS_32,
		"INT", "INTFIXEDARRAY", "INTARRAY", 0, 0, 0,	
		MULTI_INT,
		number_interpretfield,
		int_initstruct,
		0,	// destroystruct
		0,	// preparse
		int_parse,
		int_writetext,
		int_writebin,
		int_readbin,
		int_writehdiff,
		int_writebdiff,
		int_senddiff,
		int_recvdiff,
		int_bitpack,
		int_unbitpack,
		int_writestring,
		int_readstring,
		int_tomulti,
		int_frommulti,
		int_updatecrc,
		int_compare,
		0,	// memusage
		int_calcoffset,
		0,	// copystruct
		int_copyfield,
		int_copyfield2tpis,
		int_endianswap,
		int_interp,
		int_calcrate,
		int_integrate,
		int_calccyclic,
		int_applydynop,
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		int_writehtmlfile,  // writehtmlfile
		int_writexmlfile,	// writexmlfile
		int_writejsonfile,
		int_queryCopyStruct,
		0,  // newFieldCopy
		int_earrayCopy,	//earrayCopy
		int_writehdf,
		int_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_INT64_X,	TOK_STORAGE_NUMBERS,
		"INT64", "INT64FIXEDARRAY", 0, 0, 0, 0,	
		MULTI_INT,
		number_interpretfield,
		int64_initstruct,
		0,	// destroystruct
		0,	// preparse
		int64_parse,
		int64_writetext,
		int64_writebin,
		int64_readbin,
		int64_writehdiff,
		int64_writebdiff,
		int64_senddiff,
		int64_recvdiff,
		int64_bitpack,
		int64_unbitpack,
		int64_writestring,
		int64_readstring,
		int64_tomulti,
		int64_frommulti,
		int64_updatecrc,
		int64_compare,
		0,	// memusage
		int64_calcoffset,
		0,	// copystruct
		int64_copyfield,
		int64_copyfield2tpis,
		int64_endianswap,
		int64_interp,
		int64_calcrate,
		int64_integrate,
		int64_calccyclic,
		int64_applydynop,
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		int64_writehtmlfile,  // writehtmlfile
		int64_writexmlfile,	// writexmlfile
		int64_writejsonfile,	// writejsonfile
		int64_queryCopyStruct,
		0,  // newFieldCopy
		0,	//earrayCopy
		int64_writehdf,
		int64_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_F32_X,	TOK_STORAGE_NUMBERS_32,
		"F32", "F32FIXEDARRAY", "F32ARRAY", 0, 0, 0,	
		MULTI_FLOAT,
		number_interpretfield,
		float_initstruct,
		0,	// destroystruct
		0,	// preparse
		float_parse,
		float_writetext,
		float_writebin,
		float_readbin,
		float_writehdiff,
		float_writebdiff,
		float_senddiff,
		float_recvdiff,
		float_bitpack,
		float_unbitpack,
		float_writestring,
		float_readstring,
		float_tomulti,
		float_frommulti,
		float_updatecrc,
		float_compare,
		0,	// memusage
		float_calcoffset,
		0,	// copystruct
		float_copyfield,
		float_copyfield2tpis,
		float_endianswap,
		float_interp,
		float_calcrate,
		float_integrate,
		float_calccyclic,
		float_applydynop,
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		float_writehtmlfile,  // writehtmlfile
		float_writexmlfile,	// writexmlfile
		0,	// writejsonfile
		float_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		float_earrayCopy,	//earrayCopy
		float_writehdf,
		float_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_STRING_X,	TOK_STORAGE_STRINGS,
		"FIXEDSTRING", 0, 0, "STRING", 0, "STRINGARRAY",
		MULTI_STRING,
		string_interpretfield,
		string_initstruct,
		string_destroystruct,
		0,	// preparse
		string_parse,
		string_writetext,
		string_writebin,
		string_readbin,
		string_writehdiff,
		string_writebdiff,
		string_senddiff,
		string_recvdiff,
		string_bitpack,
		string_unbitpack,
		string_writestring,
		string_readstring,
		string_tomulti,
		string_frommulti,
		string_updatecrc,
		string_compare,
		string_memusage,
		string_calcoffset,
		string_copystruct,
		string_copyfield,
		string_copyfield2tpis,
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		string_applydynop,
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		string_checksharedmemory,
		string_writehtmlfile,
		string_writexmlfile,	// writexmlfile
		string_writejsonfile,
		string_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		string_earrayCopy,	//earrayCopy
		string_writehdf,
		string_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	
	

	// built-ins
	{ TOK_CURRENTFILE_X,	TOK_STORAGE_INDIRECT_SINGLE,
		"CURRENTFILE_X", 0, 0, "CURRENTFILE", 0, 0,
		MULTI_STRING,
		string_interpretfield,
		0,	// initstruct
		string_destroystruct,
		currentfile_preparse,
		currentfile_parse,  
		string_writetext,	// writetext
		currentfile_writebin,
		currentfile_readbin,
		string_writehdiff,
		string_writebdiff,
		string_senddiff,
		currentfile_recvdiff,
		string_bitpack,
		string_unbitpack,
		string_writestring,
		filename_readstring,
		string_tomulti,
		string_frommulti,
		string_updatecrc,
		string_compare,
		string_memusage,
		string_calcoffset,
		string_copystruct,
		string_copyfield,
		string_copyfield2tpis,
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		currentfile_reapplypreparse,	// reapplypreparse
		string_checksharedmemory,
		0,  // writehtmlfile
		0,	// writexmlfile
		string_writejsonfile,	// writejsonfile
		string_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		0,  // newFieldCopy
		0,	//earrayCopy
		string_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_TIMESTAMP_X,		TOK_STORAGE_DIRECT_SINGLE,
		"TIMESTAMP", 0, 0, 0, 0, 0,
		MULTI_INT,
		number_interpretfield,
		0,	// initstruct
		0,	// destroystruct
		timestamp_preparse,	
		error_parse,
		0,	// writetext
		int_writebin,
		int_readbin,
		int_writehdiff,
		int_writebdiff,
		int_senddiff,
		int_recvdiff,
		timestamp_bitpack,
		timestamp_unbitpack,
		int_writestring,
		int_readstring,
		int_tomulti,
		int_frommulti,
		timestamp_updatecrc,
		int_compare,
		0,	// memusage
		int_calcoffset,
		0,	// copystruct
		int_copyfield,
		int_copyfield2tpis,
		int_endianswap,
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		timestamp_reapplypreparse,
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		int_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  // writehdf
		int_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_LINENUM_X,		TOK_STORAGE_DIRECT_SINGLE,
		"LINENUM", 0, 0, 0, 0, 0,
		MULTI_INT,
		number_interpretfield,
		0,	// initstruct
		0,	// destroystruct
		linenum_preparse,
		error_parse,
		0,	// writetext
		int_writebin,
		int_readbin,
		int_writehdiff,
		int_writebdiff,
		int_senddiff,
		int_recvdiff,
		int_bitpack,
		int_unbitpack,
		int_writestring,
		int_readstring,
		int_tomulti,
		int_frommulti,
		int_updatecrc,
		int_compare,
		0,	// memusage
		int_calcoffset,
		0,	// copystruct
		int_copyfield,
		int_copyfield2tpis,
		int_endianswap,
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		linenum_reapplypreparse,
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		int_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		0,  // newFieldCopy
		0,	//earrayCopy
		int_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_BOOL_X,	TOK_STORAGE_NUMBERS,
		"BOOL", "BOOLFIXEDARRAY", 0, 0, 0, 0,
		MULTI_INT,
		number_interpretfield,
		u8_initstruct,
		0,	// destroystruct
		0,	// preparse
		bool_parse,
		u8_writetext,
		u8_writebin,
		u8_readbin,
		u8_writehdiff,
		u8_writebdiff,
		u8_senddiff,
		u8_recvdiff,
		bool_bitpack,
		bool_unbitpack,
		u8_writestring,
		u8_readstring,
		u8_tomulti,
		u8_frommulti,
		u8_updatecrc,
		u8_compare,
		0,	// memusage
		u8_calcoffset,
		0,	// copystruct
		u8_copyfield,
		u8_copyfield2tpis,
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		u8_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  // writehdf
		u8_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_UNUSED1_X,	0,
		NULL, NULL, 0, 0, 0, 0,
		0,
		0,
		0,
		0,	// destroystruct
		0,	// preparse
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,	// memusage
		0,
		0,	// copystruct
		0,
		0,
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		0,  // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  // writehdf
		0,  //textdiffwithnull
		
		ENDTABLE
	},


	{ TOK_BOOLFLAG_X,		TOK_STORAGE_DIRECT_SINGLE,
		"BOOLFLAG", 0, 0, 0, 0, 0,
		MULTI_INT,
		number_interpretfield,
		u8_initstruct,
		0,	// destroystruct
		0,	// preparse
		boolflag_parse,
		boolflag_writetext,
		u8_writebin,
		u8_readbin,
		u8_writehdiff,
		u8_writebdiff,
		u8_senddiff,
		u8_recvdiff,
		bool_bitpack,
		bool_unbitpack,
		u8_writestring,
		u8_readstring,
		u8_tomulti,
		u8_frommulti,
		u8_updatecrc,
		u8_compare,
		0,	// memusage
		u8_calcoffset,
		0,	// copystruct
		u8_copyfield,
		u8_copyfield2tpis,
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		boolflag_writexmlfile,	// writexmlfile
		0,	// writejsonfile
		u8_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  // writehdf
		u8_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_QUATPYR_X,		TOK_STORAGE_DIRECT_FIXEDARRAY,
		"QUATPYR_X", "QUATPYR", 0, 0, 0, 0,
		MULTI_FLOAT,
		number_interpretfield,
		0,	// initstruct
		0,	// destroystruct
		0,	// preparse
		quatpyr_parse,
		quatpyr_writetext,
		float_writebin,
		float_readbin,
		float_writehdiff,
		quatpyr_writebdiff,
		float_senddiff,
		float_recvdiff,
		float_bitpack,
		float_unbitpack,
		float_writestring,
		float_readstring,
		float_tomulti,
		float_frommulti,
		float_updatecrc,
		float_compare,
		0,	// memusage
		float_calcoffset,
		0,	// copystruct
		float_copyfield,
		float_copyfield2tpis,
		float_endianswap,
		quatpyr_interp,
		quatpyr_calcrate,
		quatpyr_integrate,
		float_calccyclic,
		quatpyr_applydynop,
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		float_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		0,  // newFieldCopy
		0,	//earrayCopy
		float_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_MATPYR_X,			TOK_STORAGE_DIRECT_FIXEDARRAY,
		"MATPYR_X", "MATPYR", 0, 0, 0, 0,
		MULTI_MAT4,
		number_interpretfield,
		0,	// initstruct
		0,	// destroystruct
		0,	// preparse
		matpyr_parse,
		matpyr_writetext,
		matpyr_writebin,
		matpyr_readbin,
		float_writehdiff,
		matpyr_writebdiff,
		matpyr_senddiff,
		matpyr_recvdiff,
		matpyr_bitpack,
		matpyr_unbitpack,
		float_writestring,
		float_readstring,
		matpyr_tomulti,
		matpyr_frommulti,
		matpyr_updatecrc,
		matpyr_compare,
		0,	// memusage
		float_calcoffset,
		0,	// copystruct
		float_copyfield,
		float_copyfield2tpis,
		float_endianswap,
		matpyr_interp,
		matpyr_calcrate,
		matpyr_integrate,
		float_calccyclic,
		float_applydynop,
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		float_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  // writehdf
		float_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_FILENAME_X,		TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY,
		"FIXEDFILENAME", 0, 0, "FILENAME", 0, 0,
		MULTI_STRING,
		string_interpretfield,
		string_initstruct,
		string_destroystruct,
		0,	// preparse
		filename_parse,
		string_writetext,
		filename_writebin,
		string_readbin,
		string_writehdiff,
		string_writebdiff,
		string_senddiff,
		string_recvdiff,
		string_bitpack,
		string_unbitpack,
		string_writestring,
		filename_readstring,
		string_tomulti,
		string_frommulti,
		string_updatecrc,
		string_compare,
		string_memusage,
		string_calcoffset,
		string_copystruct,
		string_copyfield,
		string_copyfield2tpis,
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		string_applydynop,
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		string_checksharedmemory,
		0,  // writehtmlfile
		0,	// writexmlfile
		string_writejsonfile,	// writejsonfile
		string_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		string_earrayCopy,	//earrayCopy
		0,  // writehdf
		string_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},


	{ TOK_REFERENCE_X,			TOK_STORAGE_INDIRECT_SINGLE | TOK_STORAGE_INDIRECT_EARRAY,
		"REFERENCE_X", 0, 0, "REFERENCE", 0, 0,
		MULTI_STRING,
		reference_interpretfield,
		reference_initstruct,
		reference_destroystruct,
		0,	// preparse
		reference_parse,
		reference_writetext,
		reference_writebin,
		reference_readbin,
		reference_writehdiff,
		reference_writebdiff,
		reference_senddiff,
		reference_recvdiff,
		reference_bitpack,
		reference_unbitpack,
		reference_writestring,
		reference_readstring,
		reference_tomulti,
		reference_frommulti,
		reference_updatecrc,
		reference_compare,
		0,	// memusage
		reference_calcoffset,
		reference_copystruct,
		reference_copyfield,
		reference_copyfield2tpis,
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccylic
		0,	// applydynop
		reference_preparesharedmemoryforfixup,	// preparesharedmemoryforfixup
		reference_fixupsharedmemory,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		reference_checksharedmemory,  // checksharedmemory
		0,  // writehtmlfile
		reference_writexmlfile,	// writexmlfile
		reference_writejsonfile,	// writejsonfile
		reference_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  // writehdf
		reference_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_FUNCTIONCALL_X,	TOK_STORAGE_INDIRECT_EARRAY,
		"FUNCTIONCALL_X", 0, 0, 0, 0, "FUNCTIONCALL",
		MULTI_NONE,
		ignore_interpretfield,
		0,	// initstruct
		functioncall_destroystruct,
		0,	// preparse
		functioncall_parse,
		functioncall_writetext,
		functioncall_writebin,
		functioncall_readbin,
		0,  // writehdiff
		0,  // writebdiff
		functioncall_senddiff,
		functioncall_recvdiff,
		functioncall_bitpack,
		functioncall_unbitpack,
		0,	// writestring
		0,	// readstring
		0,	// tomulti
		0,	// frommulti
		functioncall_updatecrc,
		functioncall_compare,
		functioncall_memusage,
		earray_calcoffset,
		functioncall_copystruct,
		functioncall_copyfield,
		functioncall_copyfield2tpis,
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		functioncall_queryCopyStruct,  
		0,  // newFieldCopy
		functioncall_earrayCopy,
		0,  // writehdf
		0,  //textdiffwithnull
		
		ENDTABLE
	},
	
	{ TOK_STRUCT_X,			TOK_STORAGE_STRUCTS,
		"EMBEDDEDSTRUCT", 0, 0, "OPTIONALSTRUCT", 0, "STRUCT",
		MULTI_NONE,
		struct_interpretfield,
		struct_initstruct,
		struct_destroystruct,
		0,	// preparse
		struct_parse,
		struct_writetext,
		struct_writebin,
		struct_readbin,
		struct_writehdiff,
		struct_writebdiff,
		struct_senddiff,
		struct_recvdiff,
		struct_bitpack,
		struct_unbitpack,
		struct_writestring,
		struct_readstring,
		pointer_tomulti,
		0,	// frommulti
		struct_updatecrc,
		struct_compare,
		struct_memusage,
		struct_calcoffset,
		struct_copystruct,
		struct_copyfield,
		struct_copyfield2tpis,
		struct_endianswap,
		struct_interp,
		struct_calcrate,
		struct_integrate,
		struct_calccyclic,
		struct_applydynop,
		struct_preparesharedmemoryforfixup,	// preparesharedmemoryforfixup
		struct_fixupsharedmemory,	// fixupsharedmemory
		struct_leafFirstFixup,	// leafFirstFixup
		struct_reapplypreparse,
		struct_checksharedmemory,
		struct_writehtmlfile,
		struct_writexmlfile,	// writexmlfile
		struct_writejsonfile,
		struct_queryCopyStruct,
		struct_newCopyField,  // newFieldCopy
		struct_earrayCopy,	//earrayCopy
		struct_writehdf,
		struct_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_POLYMORPH_X,			TOK_STORAGE_POLYS,
		"EMBEDDEDPOLYMORPH", 0, 0, "OPTIONALPOLYMORPH", 0, "POLYMORPH",
		MULTI_NONE,
		poly_interpretfield,
		poly_initstruct,
		poly_destroystruct,
		0,	// preparse
		poly_parse,
		poly_writetext,
		poly_writebin,
		poly_readbin,
		poly_writehdiff,
		0, //poly_writebdiff,
		poly_senddiff,
		poly_recvdiff,
		poly_bitpack,
		poly_unbitpack,
		poly_writestring,
		poly_readstring,
		0,	// tomulti
		0,	// frommulti
		poly_updatecrc,
		poly_compare,
		poly_memusage,
		poly_calcoffset,
		poly_copystruct,
		poly_copyfield,
		poly_copyfield2tpis,
		poly_endianswap,
		poly_interp,
		poly_calcrate,
		poly_integrate,
		poly_calccyclic,
		poly_applydynop,
		poly_preparesharedmemoryforfixup,	// preparesharedmemoryforfixup
		poly_fixupsharedmemory,	// fixupsharedmemory
		poly_leafFirstFixup,	// leafFirstFixup
		poly_reapplypreparse,	// reapplypreparse
		poly_checksharedmemory,
		0,  // writehtmlfile
		0,	// writexmlfile
		poly_writejsonfile,	// writejsonfile
		poly_queryCopyStruct,  // queryCopyStruct
		poly_newCopyField,  // newFieldCopy
		poly_earrayCopy,	//earrayCopy
		0,  // writehdf
		poly_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_STASHTABLE_X,		TOK_STORAGE_INDIRECT_SINGLE,
		"STASHTABLE_X", 0, 0, "STASHTABLE", 0, 0,
		MULTI_NONE,
		ignore_interpretfield,
		0,	// initstruct
		stashtable_destroystruct,
		0,	// preparse
		error_parse,
		0,	// writetext
		0,	// writebin
		0,	// readbin
		0,  // writehdiff
		0,  // writebdiff
		0,	// senddiff
		0,	// recvdiff
		0,	// bitpack
		0,	// unbitpack
		0,	// writestring
		0,	// readstring
		0,	// tomulti
		0,	// frommulti
		0,	// updatecrc
		0,	// compare
		stashtable_memusage,
		stashtable_calcoffset,
		stashtable_copystruct,
		stashtable_copyfield,
		stashtable_copyfield2tpis,
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccylic
		0,  // applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		stashtable_checksharedmemory,
		0,  // writehtmlfile
		0,	// writexmlfile
		0,	// writejsonfile
		stashtable_queryCopyStruct,  // queryCopyStruct
		0,  // newFieldCopy
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  //textdiffwithnull
		
		ENDTABLE
	},	
	{ TOK_BIT,  TOK_STORAGE_DIRECT_SINGLE,    
		"BIT", 0, 0, 0, 0, 0, 
		MULTI_INT,
		bit_interpretfield,
		bit_initstruct,    // initstruct
		0,    // destroystruct
		0,    // preparse
		bit_parse,
		bit_writetext,    // writetext
		bit_writebin,     // writebin
		bit_readbin,      // readbin
		bit_writehdiff,	  // writehdiff
		bit_writebdiff,
		bit_senddiff,     // senddiff
		bit_recvdiff,     // recvdiff
		bit_bitpack,
		bit_unbitpack,
		bit_writestring,     // writestring
		bit_readstring,   // readstring
		bit_tomulti,
		bit_frommulti,
		bit_updatecrc,    // updatecrc
		bit_compare,      // compare
		0,    // memusage
		bit_calcoffset,   // calcoffset
		0,    // copystruct
		bit_copyfield,    // copyfield
		bit_copyfield2tpis, //copyfield2tpis
		0,    // endianswap
		0,    // interp
		0,    // calcrate
		0,    // integrate
		0,    // calccyclic
		0,    // applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		bit_writexmlfile,	// writexmlfile
		bit_writejsonfile,	// writejsonfile
		bit_queryCopyStruct, // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  // writehdf
		bit_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_MULTIVAL_X,		TOK_STORAGE_DIRECT_SINGLE | TOK_STORAGE_DIRECT_FIXEDARRAY | TOK_STORAGE_INDIRECT_EARRAY | TOK_STORAGE_DIRECT_EARRAY,
		"MULTIVAL", "MULTIARRAY", 0, 0, 0, "MULTIEARRAY",	
		MULTI_NONE,
		number_interpretfield, //NMM ???
		MultiVal_initstruct,
		MultiVal_destroystruct,
		0,	// preparse
		MultiVal_parse,
		MultiVal_writetext,
		MultiVal_writebin,
		MultiVal_readbin,
		MultiVal_writehdiff,
		MultiVal_writebdiff,
		MultiVal_senddiff,
		MultiVal_recvdiff,
		MultiVal_bitpack,
		MultiVal_unbitpack,
		MultiVal_writestring,
		MultiVal_readstring,
		MultiVal_tomulti,
		MultiVal_frommulti,
		MultiVal_updatecrc,
		MultiVal_compare,
		MultiVal_memusage,
		MultiVal_calcoffset,
		MultiVal_copystruct,
		MultiVal_copyfield,
		MultiVal_copyfield2tpis,
		MultiVal_endianswap,	
		0,	// interp,
		0,  // calcrate,
		0,  // integrate,
		0,  // calccyclic,
		0,  // applydynop,
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		0,	// writexmlfile
		MultiVal_writejsonfile,	// writejsonfile
		MultiVal_queryCopyStruct,
		0,  // newFieldCopy
		MultiVal_earrayCopy,	//earrayCopy
		0,  // writehdf
		MultiVal_textdiffwithnull,  //textdiffwithnull
		
		ENDTABLE
	},
	{ TOK_COMMAND,	0,	
		"COMMAND", 0, 0, 0, 0, 0, 
		MULTI_NONE,
		command_interpretfield,
		0,	// initstruct
		0,	// destroystruct
		0,	// preparse
		0,  //parse
		0,	// writetext
		0,	// writebin
		0,	// readbin
		0,  // writehdiff
		0,  // writebdiff
		0,	// senddiff
		0,	// recvdiff
		0,	// bitpack
		0,	// unbitpack
		0,	// writestring
		0,	// readstring
		0,	// tomulti
		0,	// frommulti
		0,	// updatecrc
		0,	// compare
		0,	// memusage
		0,	// calcoffset
		0,	// copystruct
		0,	// copyfield
		0,	// copyfield2tpis
		0,	// endianswap
		0,	// interp
		0,	// calcrate
		0,	// integrate
		0,	// calccyclic
		0,	// applydynop
		0,	// preparesharedmemoryforfixup
		0,	// fixupsharedmemory
		0,	// leafFirstFixup
		0,	// reapplypreparse
		0,  // checksharedmemory
		0,  // writehtmlfile
		command_writexmlfile,	// writexmlfile
		0,	// writejsonfile
		0,  // queryCopyStruct
		0,  // newFieldCopy
		0,	//earrayCopy
		0,  // writehdf
		0,  //textdiffwithnull
		
		ENDTABLE
	},};


//////////////////////////////////////////////////////////////////////////////// FileList
// A FileList is a list of filenames and dates.  It is always kept internally sorted

// ahhh, another binary search function - why doesn't bsearch work better?
// internal function, does binary search for file name.
// returns index where file is or should be inserted and whether
// the file was found
static int FileListSearch(FileList* list, const char* path, int* found)
{
	int front = 0;
	int back = eaSize(list) - 1;
	int cmp;
	*found = 0;

	if (back == -1) // no items in list
		return 0;

	// binary search to narrow front and back
	while (back - front > 1)
	{
		int mid = (front + back) / 2;
		cmp = inline_stricmp(path, (*list)[mid]->path);
		if (cmp < 0)
			back = mid;
		else if (cmp > 0)
			front = mid;
		else
		{
			*found = 1;
			return mid;
		}
	}

	// compare to front
	cmp = inline_stricmp(path, (*list)[front]->path);
	if (cmp < 0) // if less than front
		return 0;
	if (cmp == 0) // is front
	{
		*found = 1;
		return 0;
	}

	// compare to back
	cmp = inline_stricmp(path, (*list)[back]->path);
	if (cmp > 0) // greater than back
		return back+1;
	if (cmp == 0) // is back
	{
		*found = 1;
		return back;
	}
	// otherwise, less than back
	return back;
}

// look for the given file in list, we don't
// assume sorted, so just a linear search
FileEntry* FileListFind(FileList* list, const char* path)
{
	char cleanpath[CRYPTIC_MAX_PATH];
	int found;
	int result;
	strcpy(cleanpath, path);
	forwardSlashes(cleanpath);
	result = FileListSearch(list, cleanpath, &found);
	if (!found)
		return NULL;
	return (*list)[result];
}

FileEntry* FileListFindFast(FileList* list, const char* path)
{
	int found;
	int result;
	result = FileListSearch(list, path, &found);
	if (!found)
		return NULL;
	return (*list)[result];
}

int FileListFindIndex(FileList* list, const char* path)
{
	char cleanpath[CRYPTIC_MAX_PATH];
	int found;
	int result;
	strcpy(cleanpath, path);
	forwardSlashes(cleanpath);
	result = FileListSearch(list, cleanpath, &found);
	if (!found)
		return -1;
	return result;
}

const char *FileListGetFromIndex(FileList *list, int iIndex)
{
	int size = eaSize(list);
	if (iIndex >= size || iIndex < 0)
	{
		return NULL;
	}

	return (*list)[iIndex]->path;
}

void FileListInsertInternal(FileList* list, const char* path, U32 date_or_checksum)
{
	char cleanpath[CRYPTIC_MAX_PATH];
	const char *cleanpath_ptr;
	int index;
	int found;
	FileEntry* node;

	// look for an existing element or a good index
	fileRelativePath(path, cleanpath);
	cleanpath_ptr = allocAddFilename(cleanpath);
	if (g_assert_verify_ccase_string_cache)
		assert(StringIsCCase(cleanpath_ptr));
	index = FileListSearch(list, cleanpath_ptr, &found);
	if (found)
		return;

	// otherwise, create a node and insert
	node = calloc(sizeof(FileEntry), 1);
	node->path = cleanpath_ptr;
	if (!date_or_checksum) {
		date_or_checksum = fileLastChanged(path);
		if (date_or_checksum & FILELIST_CHECKSUM_BIT)
		{
			// Happening on some end-users - clock set to 2038?  Disk corruption?
			devassert(!(date_or_checksum & FILELIST_CHECKSUM_BIT));
			date_or_checksum = 0x7fffffff;
		}
	}
	// Date can be 0 at this point, signifying that the file doesn't exist but needs to be checked when
	//  verifying the validity of the .bin file
	node->date = date_or_checksum;
	eaInsert(list, node, index);
}

// add the given file with path and date to list.
// doesn't add if already in list.
// ok to pass 0 for date - if 0, date will be looked up from file name
void FileListInsert(FileList* list, const char* path, __time32_t date)
{
	assert(!(date & FILELIST_CHECKSUM_BIT));
	FileListInsertInternal(list, path, date);
}

void FileListInsertChecksum(FileList* list, const char* path, U32 checksum) // ok to pass 0 for checksum
{
	if (!checksum)
		checksum = fileCachedChecksum(path);
	FileListInsertInternal(list, path, checksum | FILELIST_CHECKSUM_BIT);
}

// returns success
int FileListWrite(FileList* list, SimpleBufHandle file, FILE *pLayoutFile, char *pComment)
{
	long start_loc;
	long size = 0;
	int i, n;

	TagLayoutFileBegin(pLayoutFile, file, "Start of file list %s", pComment);


	if (!SerializeWriteHeader(file, FILELIST_SIG, 0, &start_loc))
		return 0;
	n = list?eaSize(list):0;
	size += SimpleBufWriteU32(n, file);
	for (i = 0; i < n; i++)
	{
		TagLayoutFile(pLayoutFile, file, "File list file: %s", (*list)[i]->path);
		if (g_assert_verify_ccase_string_cache)
		{
			assert(StringIsCCase((*list)[i]->path));
		}
		size += WritePascalString(file, (*list)[i]->path);
	
		TagLayoutFile(pLayoutFile, file, "File list date: %u", (*list)[i]->date);
		size += SimpleBufWriteU32((*list)[i]->date, file);
	}

	TagLayoutFile(pLayoutFile, file, "Before patch headder");
	SerializePatchHeader(file, size, start_loc);
	TagLayoutFileEnd(pLayoutFile, file, "End of file list");

	return 1;
}

int FileListRead(FileList* list, SimpleBufHandle file)
{
	char readsig[20];
	int size;
	int i, n, re;

	PERFINFO_AUTO_START_FUNC();

	if (!list) // if we don't care about reading the file list
	{
		SerializeSkipStruct(file);
		PERFINFO_AUTO_STOP();
		return 1;
	}

	// verify header
	if (!SerializeReadHeader(file, readsig, 20, &size))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}
	if (strcmp(readsig, FILELIST_SIG) || size <= 0)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	// read in entries
	if (!(*list)) FileListCreate(list);
	size -= SimpleBufReadU32(&n, file);
	for (i = 0; i < n; i++)
	{
		char filepath[CRYPTIC_MAX_PATH];
		__time32_t filedate;
		FileEntry* node;

		size -= re = ReadPascalString(file, filepath, CRYPTIC_MAX_PATH);
		if (!re) break;
		size -= re = SimpleBufReadU32((U32*)&filedate, file);
		if (!re) break;
		if (size < 0) break;

		node = calloc(sizeof(FileEntry), 1);
		if (g_ccase_string_cache && !StringIsCCase(filepath))
		{
			verbose_printf("%s in bin file has wrong case\n", filepath);
			PERFINFO_AUTO_STOP();
			return 0;
		}
		node->path = allocAddFilename(filepath);
		node->date = filedate;
		eaPush(list, node);
	}
	PERFINFO_AUTO_STOP();
	if (i != n || size != 0)
		return 0; // didn't get correct amount of information, something's screwed
	return 1; // a-ok
}

void FileListPrint(FileList* list)
{
	int i, n;
	n = eaSize(list);
	for (i = 0; i < n; i++)
	{
		printf("%s: %i\n", (*list)[i]->path, (*list)[i]->date);
	}
}

int FileListIsBinUpToDate(FileList* binlist, FileList *disklist)
{
	int bin_len, disk_len;
	FileEntry *bin_file, *disk_file;
	int i;

	bin_len = eaSize(binlist);
	disk_len = eaSize(disklist);

	if (bin_len != disk_len)
		return false;

	for (i = 0; i < bin_len; i++)
	{
		bin_file = (*binlist)[i];
		if (g_ccase_string_cache && !StringIsCCase(bin_file->path))
		{
			verbose_printf("%s in bin file has wrong case\n", bin_file->path);
			return false;
		}
		disk_file = FileListFind(disklist, bin_file->path);
		if (!disk_file)
		{
			verbose_printf("%s in bin file no longer exists on disk\n", bin_file->path);
			return false;
		}
		if (bin_file->date != disk_file->date)
		{
			verbose_printf("%s in bin file has different date than one on disk\n", bin_file->path);
			return false;
		}
	}

	return true;
}
void FileListForEach(FileList *list, FileListCallback callback)
{
	int list_len;
	int i;

	list_len = eaSize(list);
	for (i = 0; i < list_len; i++)
	{
		callback((*list)[i]->path, (*list)[i]->date);
	}
}

bool FileListAllFilesUpToDate(FileList *list)
{
	int list_len;
	int i;

	list_len = eaSize(list);
	for (i = 0; i < list_len; i++)
	{
		FileEntry *pEntry = (*list)[i];
		U32 fileDate = fileLastChanged(pEntry->path);
		if (g_ccase_string_cache && !StringIsCCase(pEntry->path))
		{
			if (g_assert_verify_ccase_string_cache)
				assert(0); // How'd it get here?
			return false;
		}
		if (fileDate != (U32)pEntry->date)
			return false;
	}
	return true;
}

U32 FileListGetMostRecentTimeSS2000(FileList *list)
{
	int list_len;
	int i;
	__time32_t iMostRecent;
	int iMostRecentIndex;


	list_len = eaSize(list);

	if (!list_len)
	{
		return 0;
	}

	iMostRecent = (*list)[0]->date;
	iMostRecentIndex = 0;

	for (i=1; i < list_len; i++)
	{
		if ((*list)[i]->date > iMostRecent)
		{
			iMostRecent = (*list)[i]->date;
			iMostRecentIndex = i;
		}
	}
	return timeGetSecondsSince2000FromWindowsTime32(iMostRecent);
}

void FileListForceReloadAll(FileList *list)
{

	int list_len;
	int i;

	list_len = eaSize(list);

	if (!list_len)
	{
		return;
	}


	for (i=0; i < list_len; i++)
	{
		FolderCacheForceUpdateCallbacksForFile((*list)[i]->path);		
	}
}

static int CompareDependencyEntry(const DependencyEntry** a, const DependencyEntry** b)
{
	int result = (*a)->type - (*b)->type;	
	if (result != 0) return result;

	return stricmp((*a)->name, (*b)->name);
}

static bool depPush(DependencyList *list, DependencyEntry *pRequestToSearch)
{
	int idx = (int)eaBFind(*list, CompareDependencyEntry, pRequestToSearch);

	if (*list && idx != eaSize(list) && CompareDependencyEntry(&pRequestToSearch, &((*list)[idx])) == 0)
	{
		return false; // identical thing is already here
	}
	else
	{
		eaInsert(list, pRequestToSearch, idx);
		return true;
	}
}

static DependencyEntry* depFind(DependencyList *list, DependencyEntry *pRequestToSearch)
{
	int idx = (int)eaBFind(*list, CompareDependencyEntry, pRequestToSearch);

	if (*list && idx != eaSize(list) && CompareDependencyEntry(&pRequestToSearch, &((*list)[idx])) == 0)
	{
		return (*list)[idx];
	}
	else
	{
		return NULL;
	}
}

void DependencyListInsert(DependencyList* list, DependencyType type, const char* path, U32 value)
{
	DependencyEntry *newEntry = calloc(sizeof(DependencyEntry), 1);
	newEntry->type = type;
	newEntry->name = allocAddString(path);
	newEntry->value = value;

	if (!depPush(list, newEntry))
	{
		SAFE_FREE(newEntry);
	}
}

DependencyEntry* DependencyListFind(DependencyList* list, DependencyType type, const char* path)
{
	DependencyEntry searchEntry = {0};
	searchEntry.type = type;
	searchEntry.name = allocAddString(path);

	return depFind(list, &searchEntry);
}

int DependencyListRead(DependencyList* list, SimpleBufHandle file)
{
	char readsig[20];
	int size;
	int i, n, re;

	PERFINFO_AUTO_START_FUNC();

	if (!list) // if we don't care about reading the file list
	{
		SerializeSkipStruct(file);
		PERFINFO_AUTO_STOP();
		return 1;
	}

	// verify header
	if (!SerializeReadHeader(file, readsig, 20, &size))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}
	if (strcmp(readsig, DEPENDENCYLIST_SIG) || size <= 0)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	// read in entries
	if (!(*list)) DependencyListCreate(list);
	size -= SimpleBufReadU32(&n, file);
	for (i = 0; i < n; i++)
	{
		DependencyType type;
		char name[CRYPTIC_MAX_PATH];
		U32 value;
		DependencyEntry* node;

		size -= re = SimpleBufReadU32((U32*)&type, file);
		if (!re) break;
		size -= re = ReadPascalString(file, name, CRYPTIC_MAX_PATH);
		if (!re) break;
		size -= re = SimpleBufReadU32((U32*)&value, file);
		if (!re) break;
		if (size < 0) break;

		node = calloc(sizeof(DependencyEntry), 1);
		node->type = type;
		node->name = allocAddString(name);
		node->value = value;
		eaPush(list, node);
	}

	PERFINFO_AUTO_STOP();

	if (i != n || size != 0)
		return 0; // didn't get correct amount of information, something's screwed
	return 1; // a-ok
}

int DependencyListWrite(DependencyList* list, SimpleBufHandle file, FILE *pLayoutFile)
{
	long start_loc;
	long size = 0;
	int i, n;

	TagLayoutFileBegin(pLayoutFile, file, "Beginning of Dependency List");

	if (!SerializeWriteHeader(file, DEPENDENCYLIST_SIG, 0, &start_loc))
		return 0;
	n = list?eaSize(list):0;
	size += SimpleBufWriteU32(n, file);
	for (i = 0; i < n; i++)
	{
		TagLayoutFile(pLayoutFile, file, "Dependency file %d(%s)", i, (*list)[i]->name);

		size += SimpleBufWriteU32((*list)[i]->type, file);
		size += WritePascalString(file, (*list)[i]->name);
		size += SimpleBufWriteU32((*list)[i]->value, file);
	}

	TagLayoutFile(pLayoutFile, file, "Before patch header");
	SerializePatchHeader(file, size, start_loc);
	TagLayoutFileEnd(pLayoutFile, file, "End of Dependency List");
	return 1;
}


bool DependencyListIsSuperSetOfOtherList(DependencyList *pLarger, DependencyList *pSmaller)
{
	DependencyEntry **ppEntries = *pSmaller;
	
	FOR_EACH_IN_EARRAY(ppEntries, DependencyEntry, pEntry)
	{
		if (!DependencyListFind(pLarger, pEntry->type,pEntry->name))
		{
			return false;
		}
	}
	FOR_EACH_END;

	return true;
}

#include "structInternals_h_ast.c"
