// textparser.h is the base of our object system. This file contains the basic
// object functions, as well as various parsing functions.
// Other useful utility functions can be found in textparserutils.h

#ifndef __TEXTPARSER_H
#define __TEXTPARSER_H
#pragma once
GCC_SYSTEM

#include "structdefines.h"
#include "structtokenizer.h"
#include "textparserutils.h"
#include "earray.h"
#include "utils.h"

C_DECLARATIONS_BEGIN

typedef struct SimpleBuffer *SimpleBufHandle;
typedef struct StashTableImp *StashTable;
typedef struct SharedMemoryHandle SharedMemoryHandle;
typedef void* TokenizerHandle;
typedef struct DefineContext DefineContext;
typedef struct FileEntry FileEntry;
typedef FileEntry** FileList;
typedef struct DependencyEntry DependencyEntry;
typedef DependencyEntry** DependencyList;
typedef struct ParseTableInfo ParseTableInfo;
typedef struct HogFile HogFile;
typedef struct ResourceDictionaryInfo ResourceDictionaryInfo;
typedef struct _hdf HDF;
typedef enum WriteJsonFlags WriteJsonFlags;

//////////////////////////////////////////////////// Basic Textparser Definitions

#define PARSE_SIG "ParseN"	// should stay at 6 characters

// TOK_XXX Defines - Use these defines when making manual parse tables, you can add options likes
//		TOK_STRUCTPARAM by prefixing the option and OR'ing with the TOK_XXX type
//		These defines help reliability by producing a compile error if the corresponding field 
//		is not the correct size.  They also make the semantics of compound array types like FIXEDSTR
//		clear.
// s - parent structure
// m - member field
// d - default value
// tpi - pointer to subtable ParseTable (for STRUCT)
// dictname - pointer to dictionary (for REFERENCE)
// size - size of field (for RAW)
// c - offset to int member field that has memory allocation size (for POINTER & USEDFIELD)

// primitives - all are compatible with a StaticDefine table used in the subtable field
#define TOK_BOOL(s,m,d)			TOK_BOOL_X, (SIZEOF2(s,m)==sizeof(U8))? offsetof(s,m):(5,5,5,5,5,5,5), d
#define TOK_U8(s,m,d)			TOK_U8_X, (SIZEOF2(s,m)==sizeof(U8))? offsetof(s,m):(5,5,5,5,5,5,5), d
#define TOK_INT16(s,m,d)		TOK_INT16_X, (SIZEOF2(s,m)==sizeof(S16))? offsetof(s,m):(5,5,5,5,5,5,5), d
#define TOK_INT(s,m,d)			TOK_INT_X, (SIZEOF2(s,m)==sizeof(int))? offsetof(s,m):(5,5,5,5,5,5,5), d
#define TOK_INT64(s,m,d)		TOK_INT64_X, (SIZEOF2(s,m)==sizeof(S64))? offsetof(s,m):(5,5,5,5,5,5,5), d
#define TOK_F32(s,m,d)			TOK_F32_X, (SIZEOF2(s,m)==sizeof(F32))? offsetof(s,m):(5,5,5,5,5,5,5), d
#define TOK_STRING(s,m,d)		TOK_INDIRECT | TOK_STRING_X, (SIZEOF2_DEREF(s,m)==sizeof(char))? offsetof(s,m):(5,5,5,5,5,5,5), (intptr_t)d

#define TOK_AUTOINT(s, m, d)		((SIZEOF2(s,m)==sizeof(U8)) ? TOK_U8_X : ((SIZEOF2(s,m)==sizeof(U16)) ? TOK_INT16_X : ((SIZEOF2(s,m)==sizeof(U32)) ? TOK_INT_X : ((SIZEOF2(s,m)==sizeof(U64)) ? TOK_INT64_X : (5,5,5,5,5,5,5))))), offsetof(s,m), d
#define TOK_AUTOINTARRAY(s, m)	((SIZEOF2(s,m[0])==sizeof(U8)) ? TOK_U8_X : ((SIZEOF2(s,m[0])==sizeof(U16)) ? TOK_INT16_X : ((SIZEOF2(s,m[0])==sizeof(U32)) ? TOK_INT_X : ((SIZEOF2(s,m[0])==sizeof(U64)) ? TOK_INT64_X : (5,5,5,5,5,5,5))))), offsetof(s,m), TYPE_ARRAY_SIZE(s,m)

// built-ins - closely related a primitive, but serialize or parse differently
#define TOK_CURRENTFILE(s,m)	TOK_INDIRECT | TOK_CURRENTFILE_X, (SIZEOF2_DEREF(s,m)==sizeof(char))? offsetof(s,m):(5,5,5,5,5,5,5), 0
#define TOK_TIMESTAMP(s,m)		TOK_TIMESTAMP_X, (SIZEOF2(s,m)==sizeof(int))? offsetof(s,m):(5,5,5,5,5,5,5), 0
#define TOK_LINENUM(s,m)		TOK_LINENUM_X, (SIZEOF2(s,m)==sizeof(int))? offsetof(s,m):(5,5,5,5,5,5,5), 0
#define TOK_BOOLFLAG(s,m,d)		TOK_BOOLFLAG_X, (SIZEOF2(s,m)==sizeof(U8))? offsetof(s,m):(5,5,5,5,5,5,5), d
#define TOK_QUATPYR(s,m)		TOK_FIXED_ARRAY | TOK_QUATPYR_X, (SIZEOF2(s,m)==sizeof(F32)*4)? offsetof(s,m):(5,5,5,5,5,5,5), 4
#define TOK_MAT3PYR(s,m)		TOK_FIXED_ARRAY | TOK_MATPYR_X, (SIZEOF2(s,m)==sizeof(F32)*9)? offsetof(s,m):(5,5,5,5,5,5,5), 9
#define TOK_MAT4PYR_ROT(s,m)	TOK_FIXED_ARRAY | TOK_MATPYR_X, (SIZEOF2(s,m)==sizeof(F32)*12)? offsetof(s,m):(5,5,5,5,5,5,5), 9
#define TOK_MAT4PYR_POS(s,m)	TOK_FIXED_ARRAY | TOK_F32_X, (SIZEOF2(s,m)==sizeof(F32)*12)? (offsetof(s,m) + sizeof(F32) * 9):(5,5,5,5,5,5,5), 3
#define TOK_FILENAME(s,m,d)		TOK_INDIRECT | TOK_FILENAME_X, (SIZEOF2_DEREF(s,m)==sizeof(char))? offsetof(s,m):(5,5,5,5,5,5,5), (intptr_t)d
#define TOK_MULTIVAL(s,m)		TOK_MULTIVAL_X, (SIZEOF2(s,m)==sizeof(MultiVal))? offsetof(s,m):(5,5,5,5,5,5,5), 0

// complex types
#define TOK_REFERENCE(s,m,d,dictname) TOK_REFERENCE_X | TOK_INDIRECT, (SIZEOF2(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), (intptr_t)d, (void*)dictname
#define TOK_FUNCTIONCALL(s,m)	TOK_INDIRECT | TOK_EARRAY | TOK_FUNCTIONCALL_X, (SIZEOF2(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), 0
#define TOK_STRUCT(s,m,tpi)		TOK_INDIRECT | TOK_EARRAY | TOK_STRUCT_X, (SIZEOF2(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), SIZEOF2_DEREF2(s,m), tpi
#define TOK_LATEBINDSTRUCT(s,m)		TOK_INDIRECT | TOK_EARRAY | TOK_STRUCT_X, (SIZEOF2(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), 0, NULL
#define TOK_POLYMORPH(s,m,tpi)	TOK_INDIRECT| TOK_EARRAY | TOK_POLYMORPH_X, (SIZEOF2(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), 0, tpi
#define TOK_STASHTABLE(s,m)		TOK_INDIRECT | TOK_STASHTABLE_X, (SIZEOF2(s,m)==sizeof(StashTable))? offsetof(s,m):(5,5,5,5,5,5,5), 0

// predefined array types
#define TOK_RG(s,m)				TOK_FIXED_ARRAY | TOK_U8_X, (SIZEOF2(s,m)==sizeof(U8)*2)? offsetof(s,m):(5,5,5,5,5,5,5), 2
#define TOK_RGB(s,m)			TOK_FIXED_ARRAY | TOK_U8_X, (SIZEOF2(s,m)==sizeof(U8)*3)? offsetof(s,m):(5,5,5,5,5,5,5), 3
#define TOK_RGBA(s,m)			TOK_FIXED_ARRAY | TOK_U8_X, (SIZEOF2(s,m)==sizeof(U8)*4)? offsetof(s,m):(5,5,5,5,5,5,5), 4
#define TOK_VEC2(s,m)			TOK_FIXED_ARRAY | TOK_F32_X, (SIZEOF2(s,m)==sizeof(F32)*2)? offsetof(s,m):(5,5,5,5,5,5,5), 2
#define TOK_VEC3(s,m)			TOK_FIXED_ARRAY | TOK_F32_X, (SIZEOF2(s,m)==sizeof(F32)*3)? offsetof(s,m):(5,5,5,5,5,5,5), 3
#define TOK_VEC4(s,m)			TOK_FIXED_ARRAY | TOK_F32_X, (SIZEOF2(s,m)==sizeof(F32)*4)? offsetof(s,m):(5,5,5,5,5,5,5), 4
#define TOK_IVEC2(s,m)			TOK_FIXED_ARRAY | TOK_INT_X, (SIZEOF2(s,m)==sizeof(int)*2)? offsetof(s,m):(5,5,5,5,5,5,5), 2
#define TOK_IVEC3(s,m)			TOK_FIXED_ARRAY | TOK_INT_X, (SIZEOF2(s,m)==sizeof(int)*3)? offsetof(s,m):(5,5,5,5,5,5,5), 3
#define TOK_IVEC4(s,m)			TOK_FIXED_ARRAY | TOK_INT_X, (SIZEOF2(s,m)==sizeof(int)*4)? offsetof(s,m):(5,5,5,5,5,5,5), 4
#define TOK_FIXEDSTR(s,m)		TOK_STRING_X, (SIZEOF2_DEREF(s,m)==sizeof(char))? offsetof(s,m):(5,5,5,5,5,5,5), SIZEOF2(s,m)
#define TOK_INTARRAY(s,m)		TOK_EARRAY | TOK_INT_X, (SIZEOF2_DEREF(s,m)==sizeof(int))? offsetof(s,m):(5,5,5,5,5,5,5), 0
#define TOK_F32ARRAY(s,m)		TOK_EARRAY | TOK_F32_X, (SIZEOF2_DEREF(s,m)==sizeof(F32))? offsetof(s,m):(5,5,5,5,5,5,5), 0
#define TOK_STRINGARRAY(s,m)	TOK_INDIRECT | TOK_EARRAY | TOK_STRING_X, (SIZEOF2_DEREF(s,m)==sizeof(char*))? offsetof(s,m):(5,5,5,5,5,5,5), 0
#define TOK_FILENAMEARRAY(s,m)	TOK_INDIRECT | TOK_EARRAY | TOK_FILENAME_X, (SIZEOF2_DEREF(s,m)==sizeof(char*))? offsetof(s,m):(5,5,5,5,5,5,5), 0
#define TOK_MULTIARRAY(s,m)		TOK_FIXED_ARRAY | TOK_MULTIVAL_X, (SIZEOF2(s,m)== sizeof(MultiVal)* TYPE_ARRAY_SIZE(s,m))? offsetof(s,m):(5,5,5,5,5,5,5), TYPE_ARRAY_SIZE(s,m)
#define TOK_MULTIEARRAY(s,m)	TOK_EARRAY | TOK_INDIRECT | TOK_MULTIVAL_X, (SIZEOF2_DEREF(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), 0
#define TOK_MULTIBLOCKARRAY(s,m) TOK_EARRAY | TOK_MULTIVAL_X, (SIZEOF2_DEREF(s,m) == sizeof(MultiVal)) ? offsetof(s,m):(5,5,5,5,5,5,5), 0
#define TOK_EMBEDDEDSTRUCT(s,m,tpi)	TOK_STRUCT_X, offsetof(s,m), SIZEOF2(s,m), tpi
#define TOK_EMBEDDEDPOLYMORPH(s,m,tpi)	TOK_POLYMORPH_X, offsetof(s,m), SIZEOF2(s,m), tpi
#define TOK_OPTIONALSTRUCT(s,m,tpi)	TOK_INDIRECT | TOK_STRUCT_X, (SIZEOF2(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), SIZEOF2_DEREF(s,m), tpi
#define TOK_OPTIONALLATEBINDSTRUCT(s,m)	TOK_INDIRECT | TOK_STRUCT_X, (SIZEOF2(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), 0, NULL
#define TOK_OPTIONALPOLYMORPH(s,m,tpi) TOK_INDIRECT | TOK_POLYMORPH_X, (SIZEOF2(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), 0, tpi
#define TOK_STRUCTBLOCKEARRAY(s,m,tpi)	TOK_EARRAY | TOK_STRUCT_X, (SIZEOF2(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), SIZEOF2_DEREF(s,m), tpi
#define TOK_LATEBINDSTRUCTBLOCKEARRAY(s,m)	TOK_EARRAY | TOK_STRUCT_X, (SIZEOF2(s,m)==sizeof(void*))? offsetof(s,m):(5,5,5,5,5,5,5), 0, NULL


// Basic token type array, which the defines above are built from
#define TOK_TYPE_MASK		((1 << 8)-1)
// YOU MUST ADD AN ENTRY TO g_tokentable IF ADDING A TOKEN TYPE

AUTO_ENUM;
typedef enum StructTokenType
{
	TOK_IGNORE,			// do nothing with this token, ignores remainder of line during parse
	TOK_START,			// not required, but used as the open brace for a structure
	TOK_END,			// terminate the structure described by the parse table

	// primitives - param=default value
	TOK_U8_X,			// U8 (unsigned char)
	TOK_INT16_X,		// 16 bit integer
	TOK_INT_X,			// int
	TOK_INT64_X,		// 64 bit integer
	TOK_F32_X,			// F32 (float), can be initialized with <param> but you only get an integer value
	TOK_STRING_X,		// char*

	// built-ins
	TOK_CURRENTFILE_X,	// stored as char*, filled with filename of currently parsed text file
	TOK_TIMESTAMP_X,	// stored as int, filled with fileLastChanged() of currently parsed text file
	TOK_LINENUM_X,		// stored as int, filled with line number of the currently parsed text file
	TOK_BOOL_X,			// stored as u8, restricted to 0 or 1

	TOK_UNUSED1_X,
//	TOK_FLAGS_X,		// unsigned int, list of integers as parameter, result is the values OR'd together (0, 129, 5 => 133), can't have default value
//DEPRECATED now a format flag on int so it works for u8, u64, etc.
	
	TOK_BOOLFLAG_X,		// int, no parameters in script file, if token exists, field is set to 1
	TOK_QUATPYR_X,		// F32[4], quaternion, read in as a pyr
	TOK_MATPYR_X,		// F32[3][3] in memory turns into F32[3] (PYR) when serialized
	TOK_FILENAME_X,		// same as string, passed through forwardslashes & _strupr

	// complex types
	TOK_REFERENCE_X,	// YourStruct*, subtable is dictionary name
	TOK_FUNCTIONCALL_X,	// StructFunctionCall**, parenthesis in input signals hierarchal organization
	TOK_STRUCT_X,		// YourStruct**, pass size as parameter, use eaSize to get number of items

	TOK_POLYMORPH_X,	// YourStruct**, as TOK_STRUCT, but subtable points to tpi list of possible substructures
	TOK_STASHTABLE_X,	// StashTable
	TOK_BIT,			// A bitfield... only generated by AUTOSTRUCT
	TOK_MULTIVAL_X,		// A variant type used by the expression system

	// a "Command", which never does anything when parsing, but creates a magic button when UI is autogenerated
	TOK_COMMAND,

	NUM_TOK_TYPE_TOKENS,
} StructTokenType;
// YOU MUST ADD AN ENTRY TO g_tokentable IF ADDING A TOKEN TYPE

// Flags that specify additional behavior, in addition to basic types
#define TOK_OPTION_MASK		 (~(U64)((1 << 16)-1)) // would prefer not to hit signed/unsigned bit
// YOU MUST ADD AN ENTRY TO TypeOptionNames IF ADDING A FLAG
// Array Types

//note that in column zero, which is TOK_IGNORE | TOK_PARSETABLEINFO, the numColumns is 10 bits << 8, so it overlaps these two. That shouldn't matter,
//because they are only relevant for strings
#define TOK_POOL_STRING		((U64)1 << 16)    // this field is a pooled string, and should never be freed
#define TOK_ESTRING			((U64)1 << 17)    // this is an estring, and should be allocated/freed using those functions

#define TOK_EARRAY			((U64)1 << 18)	// type is an earray of items
#define TOK_FIXED_ARRAY		((U64)1 << 19)	// type is a fixed array of items (otherwise, single)
#define TOK_INDIRECT		((U64)1 << 20)	// type is reached through a pointer (otherwise, direct)
// Basic textparser bits
#define TOK_OBJECTTYPE		((U64)1 << 21)	// marker for object type field, int or string, required for polymorph objects
#define TOK_REDUNDANTNAME	((U64)1 << 22)	// This is an alias for another textparser field
#define TOK_STRUCTPARAM		((U64)1 << 23)	// This field is parsed as part of the name, as opposed to inside the BEGIN/END block

#define TOK_ALWAYS_ALLOC	((U64)1 << 24)	// this field is only valid for TOK_OPTIONALSTRUCTs. It forces them to be allocated during StructCreate
#define TOK_NO_INDEXED_PREALLOC	((U64)1 << 24)	// this field is only valid for TOK_EARRAYSs that are indexed. It turns off preallocation, requiring the caller to index is before adding the first member.

#define TOK_NON_NULL_REF	((U64)1 << 25)    // Valid only for TOK_REFERENCE. When reading a ref for this field, it must not be NULL (normally, a reference can be "active" and NULL at the same time). 
#define TOK_REQUIRED		((U64)1 << 26)	// if no value is specified for this field when parsing from text, generate an error
#define TOK_NO_WRITE		((U64)1 << 27)    // don't write this field (bin or text or hdiff)
#define TOK_NO_NETSEND		((U64)1 << 28)    // don't put this field into packets
#define TOK_FLATEMBED		((U64)1 << 29)    // this field is the name of a flat embed which follows. This is need by the xpath stuff so it knows how to deal with "entity.ent.foo" as well as "entity.foo"
#define TOK_NO_TEXT_SAVE	((U64)1 << 30)    // this field is not written during normal text file saving (but might be if text writing is used in other contexts)
#define TOK_GLOBAL_NAME		((U64)1 << 31)	// This is the name of a global object. Interpret subtable as global dictionary
#define TOK_UNOWNED			((U64)1 << 32)	//this field is tracked by the parsetable so that it can be Xpathed and servermonitored, but is ignored by all textparser functions that actually "Do things".
#define TOK_USEDFIELD		((U64)1 << 33)	// this field is a fixed size array of U32s and has bits set specifying which fields were loaded

// Product-specific bits that are reserved for miscellaneous use
#define TOK_USEROPTIONBIT_1	((U64)1 << 34)
#define TOK_USEROPTIONBIT_2	((U64)1 << 35)
#define TOK_USEROPTIONBIT_3	((U64)1 << 36)

// This means to pool the string on the DB and nowhere else
#define TOK_POOL_STRING_DB	((U64)1 << 37)

//means "if you read in a token and don't recognize it, parse as one of these instead
#define DEPRECATED_38		((U64)1 << 38)

#define DEPRECATED_39		((U64)1 << 39)
#define TOK_PUPPET_NO_COPY	((U64)1 << 40) // When doing a puppet copy, copy this field to the new puppet.

// Entity sending/data loading bits
#define TOK_SUBSCRIBE		((U64)1 << 41) // this field is sent during container subscription
#define TOK_SERVER_ONLY		((U64)1 << 42) // Ignore this field on the client (never send it to client, never bin on client)
#define TOK_CLIENT_ONLY     ((U64)1 << 43) // Ignore this field on the server (don't bin on server)
#define TOK_SELF_ONLY		((U64)1 << 44) // Only send this field to yourself, when doing updates
#define TOK_SELF_AND_TEAM_ONLY	((U64)1 << 45) // Only send this field to entities with a special relationship (including self, team, etc)
#define TOK_LOGIN_SUBSCRIBE	((U64)1 << 46) // This field should send to client when container subscribed from login server

// Object system bits
#define TOK_KEY				((U64)1 << 47) // Is this field a key field for indexing? This field is immutable, and can't be modified via path commands
#define TOK_PERSIST			((U64)1 << 48) // Save this to the database
#define TOK_NO_TRANSACT		((U64)1 << 49) // Never transact on this field, even if it is persisted
#define TOK_SOMETIMES_TRANSACT ((U64)1 << 50) // This field is weird and may have data loss. Don't use this.
#define TOK_VITAL_REF		((U64)1 << 51) // This reference or global name is important, and is part of the parent object


// Misc Bits
#define TOK_NON_NULL_REF__ERROR_ONLY	((U64)1 << 52) //like TOK_NON_NULL_REF, except that while an error is generated, INVALID isn't returned, which presumably won't cause the parent structure to fail its parsing
#define TOK_NO_LOG			((U64)1 << 53) //when objLogWithStruct or entLogWithStruct is used to log this struct, don't write this field
#define TOK_DIRTY_BIT		((U64)1 << 54) //this field is the "diry bit" for this struct. Note that this does not necessarily mean that it's a TOK_BIT, as the dirty
										 //bit may become an pointer of some sort at some point.
#define TOK_NO_INHERIT		((U64)1 << 55) //When TextParserInheritance happens, instead of the child copying this field from the parent, the child gets the default value
#define TOK_IGNORE_STRUCT	((U64)1 << 56) //this TPI line, which is a TOK_IGNORE, ignores an entire struct (hopefully with matching curly braces)
#define TOK_SPECIAL_DEFAULT ((U64)1 << 57) // this field has a special default value stored in a non-normal place. This is used for fixed size strings (the default is stored as a FORMAT_STRING option, because param is used for size)
#define TOK_PARSETABLE_INFO  ((U64)1 << 58) // this TPI line, which is TOK_IGNORE, is actually info about the entire struct (must be line 0 if present)
#define TOK_INHERITANCE_STRUCT ((U64)1 << 59) // this is the inheritance data for its parent (must be a pointer to InheritanceData or an earray of strings for simple inheritance)

#define TOK_STRUCT_NORECURSE ((U64)1 << 60) // this substruct references its parent and shouldn't be recursed through
	//note that this only applies to things like TPI CRC generation and so forth. Putting it in will not stop you from writing, reading,
	//destroying, etc., a struct of type A which includes a struct of type B which includes a (different) struct of type B

#define TOK_CASE_SENSITIVE	((U64)1 << 61) // when comparing this field, consider it case sensitive
#define TOK_EDIT_ONLY		((U64)1 << 62) // This data is only used in the editor, don't send if we're just using the data
#define TOK_NO_INDEX		((U64)1 << 63) // Don't index this earray

//never actually found in a "normal" parse table, just set during some crazy-ass fancy maneuvering when we want
//to get special fixups called for AST_IGNORE lines. If this is set on an AST_IGNOREd parsetable line, then
//immediately after the POST_TEXT_READ callback (and ONLY then, not during bin reading or anything else)
//if any IGNORE fields were read in, the strigns they contained will be passed into 
#define TOK_WANT_FIXUP_WITH_IGNORED_FIELD TOK_NO_INDEX

// YOU MUST ADD AN ENTRY TO TypeOptionNames IF ADDING A FLAG

#define TOK_SHOULD_RECURSE_PARSEINFO(tpi, i) \
	(TOK_HAS_SUBTABLE(tpi[i].type) && tpi[i].subtable != tpi && !(tpi[i].type & TOK_STRUCT_NORECURSE))

// token precision field - can mean different things to different types of tokens
#define TOK_PRECISION_MASK	(((1 << 16)-1) & ~((1 << 8)-1))
#define TOK_PRECISION(x)	((x > 0 && x <= 255)? (x << 8): (5,5,5,5,5,5,5))
#define TOK_GET_PRECISION(type)	(((type) & TOK_PRECISION_MASK) >> 8)
#define TOK_GET_PRECISION_DEF(type, def) (TOK_GET_PRECISION(type)? TOK_GET_PRECISION(type): def)

// min bits are used for most integer fields
#define TOK_MINBITS(x)		TOK_PRECISION(x)
#define TOK_GET_MINBITS(x)	TOK_GET_PRECISION(x)
#define TOK_GET_MINBITS_DEF(x, def) TOK_GET_PRECISION_DEF(x, def)

// right now float accuracy as defined below is the only precision type for F32's
// - this may be extended later to different types of network strategies
#define TOK_FLOAT_ROUNDING(x) TOK_PRECISION(x)
#define TOK_GET_FLOAT_ROUNDING(type) TOK_GET_PRECISION(type)

typedef enum {
	FLOAT_HUNDREDTHS = 1,
	FLOAT_TENTHS,
	FLOAT_ONES,
	FLOAT_FIVES,
	FLOAT_TENS,
} FloatAccuracy;


// options for different parsing & printing formats
#define TOK_FORMAT_OPTIONS_MASK	((1 << 8)-1)
typedef enum
{
	// These formats are reversible, or are not actually used for reading/writing
	TOK_FORMAT_IP = 1,			// int, parse & print as an IP address
	TOK_FORMAT_UNSIGNED,		// int, this field should be printed unsigned
	TOK_FORMAT_DATESS2000,		// 11-07-2006 14:04:40 type format from seconds since 2k, in UTC
	TOK_FORMAT_PERCENT,			// ints or floats, display as a percent
	TOK_FORMAT_HSV,				// Vec3s, not actually for formatting, but for interpolation
	TOK_FORMAT_HSV_OFFSET,		// Vec3s, not actually for formatting, but for interpolation
	TOK_FORMAT_TEXTURE,			// This field represents a texture
	TOK_FORMAT_COLOR,			// int array, this field was defined by TOK_FORMAT_RGB or 
								// TOK_FORMAT_RGBA and should be displayed by a color picker
								// in menus

	// These formats only work for writing, and should not be used for persisted data
	TOK_FORMAT_UNREADABLE, 
	TOK_FORMAT_FRIENDLYDATE = TOK_FORMAT_UNREADABLE,	// int, print as a friendly date (e.g. "Yesterday, 12:00pm")
	TOK_FORMAT_FRIENDLYSS2000,	// int, print a seconds since 2000 count as a friendly date (e.g. "Yesterday, 12:00pm")
	TOK_FORMAT_KBYTES,			// int, print as bytes/KB/MB/etc

	TOK_FORMAT_FLAGS,			//use parse table to print out OR'd together bools

} StructFormatOptions;
#define TOK_GET_FORMAT_OPTIONS(format) ((format) & TOK_FORMAT_OPTIONS_MASK)

// listview width field
#define TOK_FORMAT_LVWIDTH_MASK	(((1 << 16)-1) & ~((1 << 8)-1))
#define TOK_FORMAT_LVWIDTH(x)	((x > 0 && x <= 255)? (x << 8): (5,5,5,5,5,5,5))
#define TOK_FORMAT_GET_LVWIDTH(format)	(((format) & TOK_FORMAT_LVWIDTH_MASK) >> 8)
#define TOK_FORMAT_GET_LVWIDTH_DEF(format, def)	(TOK_FORMAT_GET_LVWIDTH(format)? TOK_FORMAT_GET_LVWIDTH(format): def)

// ui options field
#define TOK_FORMAT_UI_LEFT		(1 << 16)	// justify left
#define TOK_FORMAT_UI_RIGHT		(1 << 17)	// justify right
#define TOK_FORMAT_UI_RESIZABLE	(1 << 18)	// can be resized
#define TOK_FORMAT_UI_NOTRANSLATE_HEADER	(1 << 19)	// the name of this field should not be translated
#define TOK_FORMAT_UI_NOHEADER	(1 << 20)	// don't show a header
#define TOK_FORMAT_UI_NODISPLAY	(1 << 21)	// don't make a column for this field


// Structures and typedefs used by the textparser system

typedef struct StructFunctionCall
{
	char* function;
	struct StructFunctionCall** params;
} StructFunctionCall;

typedef U64 StructTypeField;
typedef U32 StructFormatField;

// MAK 5/4/6 - OK, this is the way parse infos are going to be rearranged for future expansion
// and compatibility with all struct stuff:
//
//		name			char* (pointer-size)					when <name> is hit in a text file, parse this token
//		type			U64										divided into 8 8-bit fields-
//						primitive type:8						int, string, struct, etc.
//						precision:8								precision of binary storage/network for numbers
//						storage:8								array, single item, fixed array, any options for allocation
//						options:40								options for parsing like REDUNDANT_NAME, STRUCT_PARAM
//		offset			size_t (pointer-size)					offset into parent struct to store this token
//		param			intptr_t (pointer-size)					for structs: size of struct
//																for fixed arrays: number of elements
//																for numbers: default value (only integer default allowed)
//																for direct, single string: embedded string length
//																for other strings: pointer to default value
//																-- use interpretfield to find out what is held here
//		subtable		void* (pointer-size)					for complex data types: pointer to subtable or other definition
//																for primitives: pointer to StaticDefine list for string substitution
//																-- use interpretfield to find out what is held here
//		format			U32, expandable to 64					probably divided into 8-bit fields, only 2 fields right now-
//						pretty print:8							flags for how to print dates nicely, etc
//						listview width:8						width when using listview or the ui to display table
//						format options:8						bits for different options
//
//		formatString	char*									may be NULL, contains name=value pairs for formatting options
//																where flexibility is more important than performance, particularly
//																HTML-y things
// in stdtypes.h for GCC compatibility:
// typedef struct ParseTable
// {
// 	const char* name;
// 	StructTypeField type;
// 	size_t storeoffset;
// 	intptr_t param;			// default to ints, but pointers must fit here
// 	void* subtable;
// 	StructFormatField format;
// 	char *formatString;
// } ParseTable;


//----------------------stuff relating to format strings
//both of these functions return true and set a value if that named value is there, false otherwise
bool GetIntFromTPIFormatString(ParseTable *pTable, char *pFieldName, int *pOutVal);

//ppOutString is NOT AN ESTRING!!!!!!!!!!!!!!!!!!!!!
bool GetStringFromTPIFormatString(ParseTable *pTable, char *pFieldName, const char **ppOutString);
//returns true if the given int is present and non-zero (still must be an int internally)
__forceinline static bool GetBoolFromTPIFormatString(ParseTable *pTable, char *pFieldName)
{
	int iVal;
	
	return GetIntFromTPIFormatString(pTable, pFieldName, &iVal) && iVal;
}

//returns the actual string, for purposes of CRCs and so forth. Note that in the actual structure the string will have
//gotten mangled for purposes of fast lookups and so forth
const char *GetRawFormatString(ParseTable *pTable);

//true if there is a format string there
bool FormatStringIsSet(ParseTable *pTable);

//can only be called if the string was allocated in the first place
void FreeFormatString(ParseTable *pTable);

void SetFormatString(ParseTable *pTable, const char *pInString);

////////////////////////stuff relating to text parser bitfields
//
//if you change this code, change the code in GetSimpleByteCompareSizeFromType as well, please
__forceinline static int TextParserBitField_GetWordNum(ParseTable tpi[], int column) { return (int)(tpi[column].storeoffset) / 4;}
__forceinline static int TextParserBitField_GetBitNum(ParseTable tpi[], int column) { return (int)(tpi[column].param & 0xffff);}
__forceinline static int TextParserBitField_GetBitCount(ParseTable tpi[], int column) { return (int)(tpi[column].param >> 16);}
//if you change this code, change the code in GetSimpleByteCompareSizeFromType as well, please





//this callback function is used by some of the sending/packing functions that take bitsToInclude and bitsToExclude. It 
//calculates them on a struct-by-struct basis.
//The current bits are passed in as both the _ToUse and _ToRecurseWith bits, either or both of which can be modified.
typedef void StructGenerateCustomIncludeExcludeFlagsCB(ParseTable pti[], const void *pStruct, StructTypeField *pFlagsToMatch_ToUse, StructTypeField *pFlagsToExclude_ToUse,
													   StructTypeField *pFlagsToMatch_ToRecurseWith, StructTypeField *pFlagsToExclude_ToRecurseWith);
// Useful TextParser Macros

// Use this to iterate through a parse table
#define FORALL_PARSETABLE(pti, i) for (i = 0; pti[i].type || (pti[i].name && pti[i].name[0]); i++)
#define TOK_GET_TYPE(type)	(((U32)(type)) & TOK_TYPE_MASK)
#define TOK_HAS_SUBTABLE(type) (TOK_GET_TYPE(type) == TOK_STRUCT_X || TOK_GET_TYPE(type) == TOK_POLYMORPH_X)

//////////////////////////////////////////////////// Struct memory handling
// All structured data must be alloced or dealloced with these functions.
// Memory is handled a bit differently than malloc

#define STRUCT_TYPE_PTR_FROM_PTI(pti)						(TYPEOF_PARSETABLE(pti) *)
#define STRUCT_NOCONST_TYPE_PTR_FROM_PTI(pti)				(NOCONST(TYPEOF_PARSETABLE(pti))*)
#define STRUCT_TYPESAFE_PTR(pti,ptr)						(0?((TYPEOF_PARSETABLE(pti)*)0):(ptr))
#define STRUCT_NOCONST_TYPESAFE_PTR(pti,ptr)				(0?(NOCONST(TYPEOF_PARSETABLE(pti))*)0:(ptr))
#define STRUCT_TYPESAFE_PTR_PTR(pti,ptrptr)					(0?((TYPEOF_PARSETABLE(pti)**)0):(ptrptr))
#define STRUCT_NOCONST_TYPESAFE_PTR_PTR(pti,ptrptr)			(0?((NOCONST(TYPEOF_PARSETABLE(pti))**)0):(ptrptr))
#define STRUCT_TYPESAFE_PTR_PTR_PTR(pti,ptrptrptr)			(0?((TYPEOF_PARSETABLE(pti)***)0):(ptrptrptr))
#define STRUCT_NOCONST_TYPESAFE_PTR_PTR_PTR(pti,ptrptrptr)	(0?((NOCONST(TYPEOF_PARSETABLE(pti))***)0):(ptrptrptr))
#define STRUCT_DECONST_FROM_PTI(pti,ptr)					CONTAINER_NOCONST(TYPEOF_PARSETABLE(pti), STRUCT_TYPESAFE_PTR(pti,ptr))
#define STRUCT_RECONST_FROM_PTI(pti,ptr)					CONTAINER_RECONST(TYPEOF_PARSETABLE(pti), STRUCT_NOCONST_TYPESAFE_PTR(pti,ptr))

// StructAlloc handles basic memory allocation, but not initialization. Do NOT use this for 
// complicated structures managed by the object system, or using default values
// It is safe to StructAlloc something, and immediately copy over it
SA_RET_NN_VALID void*	StructAlloc_dbg(ParseTable pti[], ParseTableInfo *optPtr, const char* callerName, int line);
#define StructAllocVoid(pti) StructAlloc_dbg(pti, NULL MEM_DBG_PARMS_INIT)
#define StructAlloc(pti) STRUCT_TYPE_PTR_FROM_PTI(pti)StructAllocVoid(pti)
#define StructAllocNoConst(pti) STRUCT_NOCONST_TYPE_PTR_FROM_PTI(pti)StructAllocVoid(pti)
#define StructAllocIfNullVoid(pti,s) if(!s) s = StructAlloc_dbg(pti, NULL MEM_DBG_PARMS_INIT)
#define StructAllocIfNull(pti,s) if(!s) s = StructAlloc(pti)
#define StructAllocIfNullNoConst(pti,s) if(!s) s = StructAllocNoConst(pti)

// StructInit initializes the members of a structure safely. This is called by StructCreate
void	StructInit_dbg(ParseTable pti[],void *structptr, const char *pCreationComment MEM_DBG_PARMS);
#define StructInitWithCommentVoid(pti, structptr, comment) StructInit_dbg(pti, structptr, comment MEM_DBG_PARMS_INIT)
#define StructInitWithComment(pti, structptr, comment) StructInitWithCommentVoid(pti, STRUCT_TYPESAFE_PTR(pti,structptr), comment)
#define StructInitVoid(pti, structptr) StructInitWithCommentVoid(pti, structptr, NULL)
#define StructInit(pti, structptr) StructInitVoid(pti, STRUCT_TYPESAFE_PTR(pti,structptr))
#define StructInitNoConst(pti, structptr) StructInitVoid(pti, STRUCT_NOCONST_TYPESAFE_PTR(pti,structptr))
void	StructInitFields(ParseTable pti[], void* structptr); // as above but doesn't do initial memset to zero

// StructCreate allocates the memory for a structure, and initializes it.
// By default, use this by default to create textparser structures.
SA_RET_NN_VALID void*   StructCreate_dbg(ParseTable pti[], const char *pCreationComment MEM_DBG_PARMS);
#define StructCreateVoid(pti) StructCreate_dbg(pti, NULL MEM_DBG_PARMS_INIT)
#define StructCreate(pti) STRUCT_TYPE_PTR_FROM_PTI(pti)StructCreateVoid(pti)
#define StructCreateNoConst(pti) STRUCT_NOCONST_TYPE_PTR_FROM_PTI(pti)StructCreateVoid(pti)
#define StructCreateWithComment(pti, pComment) StructCreate_dbg(pti, pComment MEM_DBG_PARMS_INIT)

// Creation utility functions
#define StructCreateDefault(StructName)	StructCreate(parse_##StructName)
SA_RET_OP_VALID void*   StructCreateFromString_dbg(ParseTable pti[], char *str MEM_DBG_PARMS); // Create a structure, and read in data from the given string. Calls ParserReadText
#define StructCreateFromString(pti,str) StructCreateFromString_dbg(pti,str MEM_DBG_PARMS_INIT)

//the string being read came from a data file, so we will report all errors as being associated with a particular file/line
SA_RET_OP_VALID void*   StructCreateFromStringEscapedWithFileAndLine_dbg(ParseTable pti[], char *str, const char *dataFile, int iDataFileLineNum MEM_DBG_PARMS);//like above, but escaped string. Calls ParserReadTextEscaped
#define StructCreateFromStringEscapedWithFileAndLine(pti,str, dataFile, dataFileLineNum) StructCreateFromStringEscapedWithFileAndLine_dbg(pti,str, dataFile, dataFileLineNum MEM_DBG_PARMS_INIT)

#define StructCreateFromStringEscaped(pti,str) StructCreateFromStringEscapedWithFileAndLine_dbg(pti,str, NULL, 0 MEM_DBG_PARMS_INIT)


SA_RET_OP_VALID void*	StructClone_dbg(ParseTable pti[], const void *source, const char *pComment MEM_DBG_PARMS); // Create a new structure, and then StructCopyAll from source
#define StructCloneVoid(pti, source) StructClone_dbg(pti, source, NULL MEM_DBG_PARMS_INIT)
#define StructClone(pti, source) STRUCT_TYPE_PTR_FROM_PTI(pti)StructCloneVoid(pti, STRUCT_TYPESAFE_PTR(pti,source))
#define StructCloneFromNoConst(pti, source) STRUCT_TYPE_PTR_FROM_PTI(pti)StructCloneVoid(pti, STRUCT_NOCONST_TYPESAFE_PTR(pti,source))
#define StructCloneNoConst(pti, source) STRUCT_NOCONST_TYPE_PTR_FROM_PTI(pti)StructCloneVoid(pti, STRUCT_NOCONST_TYPESAFE_PTR(pti,source))
#define StructCloneDeConst(pti, source) StructCloneNoConst(pti, STRUCT_DECONST_FROM_PTI(pti,source))
#define StructCloneReConst(pti, source) StructClone(pti, STRUCT_RECONST_FROM_PTI(pti,source))
#define StructCloneWithCommentVoid(pti, source, comment) StructClone_dbg(pti, source, comment MEM_DBG_PARMS_INIT)
#define StructCloneWithComment(pti, source, comment) STRUCT_TYPE_PTR_FROM_PTI(pti)StructCloneWithCommentVoid(pti, STRUCT_TYPESAFE_PTR(pti,source), comment)
#define StructCloneWithCommentNoConst(pti, source, comment) STRUCT_NOCONST_TYPE_PTR_FROM_PTI(pti)StructCloneWithCommentVoid(pti, STRUCT_NOCONST_TYPESAFE_PTR(pti,source), comment)
#define StructCloneWithCommentDeConst(pti, source, comment) StructCloneWithCommentNoConst(pti, STRUCT_DECONST_FROM_PTI(pti, source), comment)

SA_RET_OP_VALID void*	StructConvert_dbg(ParseTable sourcepti[], void *source, ParseTable destpti[] MEM_DBG_PARMS); // Create a new structure, and then StructCopyAll from source
#define StructConvert(sourcepti, source, destpti) StructConvert_dbg(sourcepti, source, destpti MEM_DBG_PARMS_INIT)


// StructDeInit frees the submembers of a structure, in preparation for freeing the structure
// If you wish to clear to the default state, use StructReset.
// Calling DeInit and then StructCopyFields over top is safe.
void	StructDeInitVoid(ParseTable pti[], void* structptr);
#define StructDeInit(pti, structptr) StructDeInitVoid(pti, STRUCT_TYPESAFE_PTR(pti,structptr))
#define StructDeInitNoConst(pti, structptr) StructDeInitVoid(pti, STRUCT_NOCONST_TYPESAFE_PTR(pti,structptr))

// StructReset DeInits and then Inits a structure, safely reseting it to the default state
void StructResetVoid(ParseTable pti[], void *structptr);
#define StructReset(pti, structptr) StructResetVoid(pti, STRUCT_TYPESAFE_PTR(pti,structptr))
#define StructResetNoConst(pti, structptr) StructResetVoid(pti, STRUCT_NOCONST_TYPESAFE_PTR(pti,structptr))

// StructDestroy is how you should normally destroy textparser structs. It calls DeInit, and then frees it
void	StructDestroyVoid(ParseTable pti[], SA_PRE_OP_VALID SA_POST_FREE void* structptr);
#define StructDestroy(pti, ptr) StructDestroyVoid(pti, STRUCT_TYPESAFE_PTR(pti,ptr))
#define StructDestroyNoConst(pti, ptr) StructDestroyVoid(pti, STRUCT_NOCONST_TYPESAFE_PTR(pti,ptr))
__forceinline static void	StructDestroySafeVoid(ParseTable pti[], SA_PRE_OP_VALID SA_POST_OP_NULL void** structptr) // Destroy and null pointer
{ if(structptr && *structptr){StructDestroyVoid(pti, *structptr); *structptr = NULL;} }
#define StructDestroySafe(pti, ptr) StructDestroySafeVoid(pti, STRUCT_TYPESAFE_PTR_PTR(pti,ptr))
#define StructDestroyNoConstSafe(pti, ptr) StructDestroySafeVoid(pti, STRUCT_NOCONST_TYPESAFE_PTR_PTR(pti,ptr))

// Parser String alloc/free. This is basically a wrapper for malloc
#define StructAllocString(str) StructAllocStringLen_dbg(str, ((str)?((int)strlen(str)):0) MEM_DBG_PARMS_INIT)
#define StructAllocString_dbg(str, caller_fname, line) StructAllocStringLen_dbg(str, ((str)?((int)strlen(str)):0), caller_fname, line)
#define StructAllocStringLen(string,len) StructAllocStringLen_dbg(string,len MEM_DBG_PARMS_INIT)
SA_RET_OP_STR char*	StructAllocStringLen_dbg(SA_PARAM_OP_STR const char* string, int len MEM_DBG_PARMS);	// allocate memory for a string and copy parameter in
void	StructFreeString(SA_PRE_OP_STR SA_POST_P_FREE char* string);		// release memory for a string
__forceinline static void	StructFreeStringSafe(SA_PRE_OP_OP_STR SA_POST_OP_NULL char** stringptr) // Destroy and null pointer
{ if(stringptr && *stringptr){StructFreeString(*stringptr); *stringptr = NULL;} }
#define StructCopyString(dest, src) StructCopyString_dbg(dest, src MEM_DBG_PARMS_INIT)
void	StructCopyString_dbg(SA_PRE_GOOD SA_POST_NN_OP_STR char** dest, SA_PARAM_OP_STR const char* src MEM_DBG_PARMS);

/////////////////////////////////////////////////// Struct utils
// General functions for copying, comparing, crcing, etc. structs

// Get a CRC of the structure and children
U32		StructCRC(ParseTable pti[], void* structptr); 

// Compares two structures, returning -1, 0, or 1
typedef enum enumCompareFlags
{
	COMPAREFLAG_NULLISDEFAULT = 1,
	COMPAREFLAG_USEDIRTYBITS = 1 << 1,
	COMPAREFLAG_COMPARE_FLOATS_APPROXIMATELY = 1 << 2,
	COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT = 1 << 3,
	COMPAREFLAG_EMPTY_STRINGS_MATCH_NULL_STRINGS = 1 << 4,
	
} enumCompareFlags;
int		StructCompare(ParseTable pti[], const void *structptr1, const void *structptr2, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
// Compares a single element element on two structures
int		TokenCompare(ParseTable tpi[], int column, const void *structptr1, const void *structptr2, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);




//a set of option flags describing how structs can be copied
typedef enum StructCopyFlags
{
	STRUCTCOPYFLAG_DONT_COPY_NO_ASTS = 1 << 0, //if true, then "all the rest" of the struct is NOT copied

	STRUCTCOPYFLAG_ALWAYS_RECURSE = 1 << 1, //ignore all require/exclude flags when looking at substructs (but not for leaf fields inside those structs)

	STRUCTCOPYFLAG_LAST
} StructCopyFlags;

#define USE_NEW_STRUCTCOPY 1 // Causes errors when copying/deleting multivals

int StructCopyVoid(ParseTable *pTPI, const void *source, void *dest, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
#define StructCopy(tpi, source, dest, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyVoid(tpi, STRUCT_TYPESAFE_PTR(tpi,source), STRUCT_TYPESAFE_PTR(tpi,dest), eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude)
#define StructCopyNoConst(tpi, source, dest, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyVoid(tpi, STRUCT_NOCONST_TYPESAFE_PTR(tpi,source), STRUCT_NOCONST_TYPESAFE_PTR(tpi,dest), eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude)
#define StructCopyDeConst(tpi, source, dest, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyVoid(tpi, STRUCT_TYPESAFE_PTR(tpi,source), STRUCT_NOCONST_TYPESAFE_PTR(tpi,dest), eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude)
#define StructCopyReConst(tpi, source, dest, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyVoid(tpi, STRUCT_NOCONST_TYPESAFE_PTR(tpi,source), STRUCT_TYPESAFE_PTR(tpi,dest), eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude)

//copy a single field or substruct.
int StructCopyFieldVoid(ParseTable *pTPI, const void *source, void *dest, const char *field, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
#define StructCopyField(tpi, source, dest, field, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyFieldVoid(tpi, STRUCT_TYPESAFE_PTR(tpi,source), STRUCT_TYPESAFE_PTR(tpi,dest), field, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude)
#define StructCopyFieldNoConst(tpi, source, dest, field, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyFieldVoid(tpi, STRUCT_NOCONST_TYPESAFE_PTR(tpi,source), STRUCT_NOCONST_TYPESAFE_PTR(tpi,dest), field, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude)
#define StructCopyFieldDeConst(tpi, source, dest, field, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyFieldVoid(tpi, STRUCT_TYPESAFE_PTR(tpi,source), STRUCT_NOCONST_TYPESAFE_PTR(tpi,dest), field, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude)
#define StructCopyFieldReConst(tpi, source, dest, field, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyFieldVoid(tpi, STRUCT_NOCONST_TYPESAFE_PTR(tpi,source), STRUCT_TYPESAFE_PTR(tpi,dest), field, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude)

#if USE_NEW_STRUCTCOPY

//the new StructCopy, which is hopefully faster and better and more flexible and easier to understand than the old
//StructCopyAll/StructCopyFields dichotomy. See the wiki page for more info.
#define StructCopyAllVoid(tpi, source, dest) StructCopyVoid(tpi, source, dest, 0, 0, 0)
#define StructCopyAll(tpi, source, dest) StructCopyAllVoid(tpi, STRUCT_TYPESAFE_PTR(tpi,source), STRUCT_TYPESAFE_PTR(tpi,dest))
#define StructCopyAllNoConst(tpi, source, dest) StructCopyAllVoid(tpi, STRUCT_NOCONST_TYPESAFE_PTR(tpi,source), STRUCT_NOCONST_TYPESAFE_PTR(tpi,dest))
#define StructCopyAllDeConst(tpi, source, dest) StructCopyAllVoid(tpi, STRUCT_TYPESAFE_PTR(tpi,source), STRUCT_NOCONST_TYPESAFE_PTR(tpi,dest))
#define StructCopyAllReConst(tpi, source, dest) StructCopyAllVoid(tpi, STRUCT_NOCONST_TYPESAFE_PTR(tpi,source), STRUCT_TYPESAFE_PTR(tpi,dest))

#define StructCopyFieldsVoid(tpi, source, dest, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyVoid(tpi, source, dest, STRUCTCOPYFLAG_DONT_COPY_NO_ASTS, iOptionFlagsToMatch, iOptionFlagsToExclude)
#define StructCopyFields(tpi, source, dest, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyFieldsVoid(tpi, STRUCT_TYPESAFE_PTR(tpi,source), STRUCT_TYPESAFE_PTR(tpi,dest), iOptionFlagsToMatch, iOptionFlagsToExclude)
#define StructCopyFieldsNoConst(tpi, source, dest, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyFieldsVoid(tpi, STRUCT_NOCONST_TYPESAFE_PTR(tpi,source), STRUCT_NOCONST_TYPESAFE_PTR(tpi,dest), iOptionFlagsToMatch, iOptionFlagsToExclude)
#define StructCopyFieldsDeConst(tpi, source, dest, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyFieldsVoid(tpi, STRUCT_TYPESAFE_PTR(tpi,source), STRUCT_NOCONST_TYPESAFE_PTR(tpi,dest), iOptionFlagsToMatch, iOptionFlagsToExclude)
#define StructCopyFieldsReConst(tpi, source, dest, iOptionFlagsToMatch, iOptionFlagsToExclude) StructCopyFieldsVoid(tpi, STRUCT_NOCONST_TYPESAFE_PTR(tpi,source), STRUCT_TYPESAFE_PTR(tpi,dest), iOptionFlagsToMatch, iOptionFlagsToExclude)

#else

// StructCopyAll copies the entire contents of the source object over top of the dest object,
// INCLUDING data that is not allocated via TextParser. It does this recursively for any children
// This is faster than CopyFields, and should be used unless you wish to exclude some data
int		StructCopyAll(ParseTable pti[], const void* source, void* dest);

// StructCopyFields copies from source to dest all fields that textparser knows about.
// If flags to match or exclude are specified, only the specified fields will be copied
// This is slower than CopyAll, but will not copy any non-textparser data.
int		StructCopyFields(ParseTable pti[], const void* source, void* dest, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

#endif


//flags used by sendDiff and ParserSend. Put here because it's a good generally included file
typedef enum enumSendDiffFlags
{
	SENDDIFF_FLAG_FORCEPACKALL = 1 << 0,
	SENDDIFF_FLAG_ALLOWDIFFS = 1 << 1,

	//this flag must be set on send and on receive. It says to use the faster compare algorithm
	SENDDIFF_FLAG_COMPAREBEFORESENDING  = 1 << 2,
} enumSendDiffFlags;

typedef enum enumRecvDiffFlags
{
	RECVDIFF_FLAG_ABS_VALUES = 1 << 0,
	RECVDIFF_FLAG_UNUSED = 1 << 1,

	//must be set if and only if SENDDIFF_FLAG_COMPAREBEFORESENDING was set
	RECVDIFF_FLAG_COMPAREBEFORESENDING = 1 << 2,

	RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE = 1 << 3, //presumably the source is a production mode game client or patcher
		//if something goes wrong, don't assert, just fail.

		//if you're going to structInit a struct, get the global variable creation comment set in 
		//textparser.h (obviously not even remotely threadsafe)
	RECVDIFF_FLAG_GET_GLOBAL_CREATION_COMMENT = 1 << 4,

} enumRecvDiffFlags;


//copies from one struct described by one tpi to another struct described by another TPI, attempting to copy all fields
//that are "The same" between the two structs.
//

//enumerated result type for copyfield2tpis_f and StructCopyFields2tpis
typedef enum enumCopy2TpiResult
{
	COPY2TPIRESULT_SUCCESS,
	COPY2TPIRESULT_UNKNOWN_FIELDS,
	COPY2TPIRESULT_FAILED_FIELDS,
} enumCopy2TpiResult;

enumCopy2TpiResult StructCopyFields2tpis(ParseTable src_tpi[], const void* source, ParseTable dest_tpi[], void* dest, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString);

//given two TPIs and a (non-redundant-named) column number in one, try to find a non-redundant-named column in the other with the
//same name, but respecting redundant names as appropriate. Returns -1 on failure
int FindMatchingColumnInOtherTpi(ParseTable inTpi[], int iInColumn, ParseTable outTpi[]);



// Like StructClone, but uses StructCopyFields internally instead of StructCopyAll.
// so it will only clone fields that textparser knows about.
SA_RET_OP_VALID void*	StructCloneFields_dbg(ParseTable pti[], const void *source, const char *pCreationComment MEM_DBG_PARMS); // Create a new structure, and then StructCopyFields from source
#define StructCloneFieldsWithCommentVoid(pti, source, pComment) StructCloneFields_dbg(pti, source, pComment MEM_DBG_PARMS_INIT)
#define StructCloneFieldsWithComment(pti, source, pComment) STRUCT_TYPE_PTR_FROM_PTI(pti)StructCloneFieldsWithCommentVoid(pti, source, pComment)
#define StructCloneFieldsVoid(pti, source) StructCloneFieldsWithCommentVoid(pti, source, NULL)
#define StructCloneFields(pti, source) STRUCT_TYPE_PTR_FROM_PTI(pti)StructCloneFieldsVoid(pti, source)

// Makes the minimal-size copy possible, EArrays as small as possible.
// Returns the new structure, or NULL if copying failed (ran out of memory?).  
// Pass in NULL for pDestStruct in order to have the destination allocated automatically
typedef void* (*CustomMemoryAllocator)(void* data, size_t size);
void *	StructCompress(ParseTable pti[], const void* pSrcStruct, void *pDestStruct, CustomMemoryAllocator memAllocator, void *customData); 

///////////////////////////////////////////////////// Parser serialization
// Serializing to/from text, network, file, etc.

// Text I/O

// Read in data from a given text file
//usage note: if you call this on a nonexistant file, it will return zero, BUT it will not error or assert in any way
//
//flags are the same as for ParserLoadFiles, but only certain ones apply
int ParserReadTextFile(const char* filename, ParseTable *pti, void* structptr, int flags); 

//same as the above, but sticks parse errors into an EString, rather than errorFing.
//Note that this changes the errorf handler function temporarily, so has threading issues
int ParserReadTextFile_CaptureErrors(const char* filename, ParseTable *pti, void* structptr, int flags, char **ppErrors); 


// Writes out data to a text file. If you specify flags, only the fields that match will be written out
//prefixString will get written at the beginning of the file, if specified. SuffixString will get written at the end
int ParserWriteTextFileEx(const char* filename, ParseTable pti[], void* structptr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude,
	const char *pPrefixString, const char *pSuffixString);
#define ParserWriteTextFile(filename, pti, structptr, iOptionFlagsToMatch, iOptionFlagsToExclude) ParserWriteTextFileEx(filename, pti, structptr, 0, iOptionFlagsToMatch, iOptionFlagsToExclude, NULL, NULL)
int ParserWriteTextFileAppend(const char* filename, ParseTable pti[], void* structptr, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);



//writes out a .gz file containing the text file
int ParserWriteZippedTextFile(const char* filename, ParseTable pti[], void* structptr, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

//reads the above
int ParserReadZippedTextFile(const char* filename, ParseTable *pti, void* structptr, int flags); 


// String reading/writing
//
//flags are the same as for ParserLoadFiles, but only certain ones apply
int ParserReadTextWithCommentEx(const char *str,ParseTable *tpi,void *struct_mem, int iFlags, const char *pComment, const char *pFunction, int iLine);
#define ParserReadTextWithComment(str, tpi, struct_mem, iFlags, pComment) ParserReadTextWithCommentEx(str, tpi, struct_mem, iFlags, pComment, __FUNCTION__,  __LINE__ )
#define ParserReadText(str, tpi, struct_mem, iFlags) ParserReadTextWithCommentEx(str, tpi, struct_mem, iFlags, NULL, __FUNCTION__,  __LINE__ )

// Load from text, but pretend we're loading from a file
int ParserReadTextForFile(const char *str,const char *filename, ParseTable *tpi,void *struct_mem, int iFlags);

//these write a struct. To write an individual field, use TokenWriteText (no index) or FieldWriteText (which accepts an index)
int ParserWriteText_dbg(char **estr,ParseTable *tpi,void *struct_mem, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, SA_PARAM_NN_STR const char *caller_fname, int line);
#define ParserWriteText(estr, tpi, struct_mem, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) ParserWriteText_dbg(estr, tpi, struct_mem, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, __FILE__, __LINE__)

int ParserWriteTextSafe(char **estrStruct, char **estrTPI, SA_PARAM_NN_VALID U32 *uCRC, ParseTable *tpi,void *struct_mem, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
int ParserReadTextSafeWithCommentEx(const char *strStruct, const char *strTPI, U32 iOtherCRC, ParseTable *tpi,void *struct_mem, int iFlags, const char *pComment, const char *pFunction, int iLine);
#define ParserReadTextSafe(strStruct, strTPI, iOtherCRC, tpi, struct_mem, iFlags) ParserReadTextSafeWithCommentEx(strStruct, strTPI, iOtherCRC, tpi, struct_mem, iFlags, NULL, __FUNCTION__, __LINE__)
#define ParserReadTextSafeWithComment(strStruct, strTPI, iOtherCRC, tpi, struct_mem, iFlags, pComment) ParserReadTextSafeWithCommentEx(strStruct, strTPI, iOtherCRC, tpi, struct_mem, iFlags, pComment, __FUNCTION__, __LINE__)

int ParserWriteTextFileToHogg(
	const char *filename,
	ParseTable pti[], void* structptr,
	HogFile *pHogFile);

int ParserReadTextFileFromHogg(
	const char *filename,
	ParseTable pti[], void* structptr,
	HogFile *pHogFile);

// Escaped string reading/writing
//
//flags are the same as for ParserLoadFiles, but only certain ones apply
//
//NOTE NOTE NOTE NOTE **str is NOT an estring, but it gets updated to point after the read text. Do NOT NOT NOT
//pass in an estring and assume that anything useful will happen
int ParserReadTextEscapedWithCommentOrFileAndLineEx(const char **str, ParseTable *tpi, void *struct_mem, int iFlags, const char *pComment, const char *dataFileName, int iDataFileLineNum, const char *pFunction, int iLine);
#define ParserReadTextEscaped(str, tpi, struct_mem, iFlags) ParserReadTextEscapedWithCommentOrFileAndLineEx(str, tpi, struct_mem, iFlags, NULL, NULL, 0, __FUNCTION__, __LINE__)
#define ParserReadTextEscapedWithComment(str, tpi, struct_mem, iFlags, pComment) ParserReadTextEscapedWithCommentOrFileAndLineEx(str, tpi, struct_mem, iFlags, pComment, NULL, 0, __FUNCTION__, __LINE__)
#define ParserReadTextEscapedWithFileAndLine(str, tpi, struct_mem, iFlags, dataFileName, iDataLineNum) ParserReadTextEscapedWithCommentOrFileAndLineEx(str, tpi, struct_mem, iFlags, NULL, dataFileName, iDataLineNum, __FUNCTION__, __LINE__)
int ParserWriteTextEscaped(char **estr, ParseTable *tpi, const void *struct_mem, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

//Support for Binary diffs
typedef void * MultiValPointer;

//operator
AUTO_ENUM;
typedef enum StructDiffOperator {
	STRUCTDIFF_DESTROY	= -1,
	STRUCTDIFF_INVALID  = 0,
	STRUCTDIFF_SET		= 1,
	STRUCTDIFF_CREATE	= 2
} StructDiffOperator;

//operation
AUTO_STRUCT;
typedef struct StructDiffOp {
	ObjectPath *pField;
	StructDiffOperator op;
	MultiValPointer pOperand; AST(INT) //will work fine as long as this struct is never read/written
} StructDiffOp;

AUTO_STRUCT;
typedef struct StructDiff {
	StructDiffOp **ppOps;
} StructDiff;

StructDiffOp *StructMakeDiffOp(ObjectPath *path, void *structptr, StructDiffOperator sdop);
StructDiffOp *StructMakeAndAppendDiffOp(StructDiff *diff, ObjectPath* path, void *structptr, StructDiffOperator sdop);

bool StructDiffIsValid(StructDiff *diff);

//Dump a StructDiff to an estring. !!NOT compatible with text diffs!! Just for debugging.
void StructWriteTextDiffFromBDiff(char **estr, StructDiff *diff);
// This function is ONLY FOR DEBUGGING and will give you a memory report of the given structure in a completely non-thread-safe way
const char *StructWriteMemoryReport(ParseTable tpi[], void *newp);

void StructDestroyDiffOp(StructDiffOp **op);

//what does invertExcludeFlags do you might wonder? Look for TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT
//and see the comment there
StructDiff *StructMakeDiff_dbg(ParseTable tpi[], void *old_struct, void *new_struct, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, bool forceAbsoluteDiff, const char *caller_fname, int line);
#define StructMakeDiff(tpi, old_struct, new_struct, iOptionFlagsToMatch, iOptionFlagsToExclude, invertExcludeFlags, forceAbsoluteDiff) StructMakeDiff_dbg(tpi, old_struct, new_struct, iOptionFlagsToMatch, iOptionFlagsToExclude, invertExcludeFlags, forceAbsoluteDiff, __FILE__, __LINE__)

void StructDestroyDiff(StructDiff **diff);

void StructMakeDiffInternal(StructDiff *diff, ParseTable tpi[], void *oldp, void *newp, ObjectPath* parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, bool forceAbsoluteDiff, const char *caller_fname, int line);


void StructDestroyDiff(StructDiff **diff);

typedef enum
{
	TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT = 1 << 0,
	/*Far and away the most confusing flag, for which we daily curse the memory of Ben Ziegler. What does it do? Well, look at one of
the calls to StructWriteTextDiff in gslSendEntityToDatabase:
	StructWriteTextDiff(&diffString,parse_Entity,bEnt->pSaved->pEntityBackup,ent,NULL,TOK_PERSIST,TOK_NO_TRANSACT,TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT);
Basically, that call looks like it's including TOK_PERSIST and excluding TOK_NO_TRANSACT. Actually, it's requiring BOTH fields to be true
before it diffs anything. BUT, while recursing the hierarchy of the structure, when it comes to a child struct, if that
child has TOK_PERSIST set but not TOK_NO_TRANSACT, it recurses into that child struct. If the child struct has both
TOK_PERSIST and TOK_NO_TRANSACT set, it recurses in, but does so back in normal mode with the crazy flag cleared and only TOK_PERSIST set.

So, if we restrict ourselves to only thinking about fields with TOK_PERSIST set, it's basically recursing through the struct. When it comes to
a normal field (an int or a string or something), if that field has TOK_NO_TRANSACT set, it diffs it, otherwise it doesn't. When it comes to
a struct, if that field has TOK_NO_TRANSACT set it switches to a normal diff from then on which will diff all TOK_PERSIST fields inside that
struct, and forgets about TOK_NO_TRANSACT entirely. If that struct does not have TOK_NO_TRANSACT set, then we recurse in but maintain
our initial behavior.

This behavior used to be called InvertExcludeFlags, which is probably still floating around some places*/


//if traversing into an unowned struct, add TOK_KEY to the flagsToMatch field. This prevents infinite recursion in some
//cases
	TEXTDIFFFLAG_ONLYKEYSFROMUNOWNED = 1 << 1,

//if set, then don't write the create string, THEN CLEAR THIS FLAG
	TEXTDIFFFLAG_DONTWRITECREATE_NONRECURSING = 1 << 2,



//skip over all polymorphic children (if this is not set, then if a polymorphic child actually exists and needs to be written, 
//it will assert)
	TEXTDIFFFLAG_SKIP_POLYMORPHIC_CHILDREN = 1 << 3,

//Write out references as "@DictName[Key]" rather than "Key". Used for Gateway/JSON-based users
	TEXTDIFFFLAG_ANNOTATE_REFERENCES = 1 << 4,

//Write out RFC822 time string if the field calls for it
	TEXTDIFFFLAG_JSON_SECS_TO_RFC822 = 1 << 5,

} TextDiffFlags;


// Does a text diff of two structures and writes to the estring
void StructWriteTextDiff_dbg(char **estr, ParseTable tpi[], void *old_struct, void *new_struct, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS);
#define StructWriteTextDiff(estr, tpi, old_struct, new_struct, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags) StructWriteTextDiff_dbg(estr, tpi, old_struct, new_struct, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, __FILE__, __LINE__)

//uses the new internal WriteTextDiff calls instead of WriteHDiff... should be identical to calling StructWriteTextDiff with NULL as the old_struct,
//but does not support all the TEXTDIFFFLAGs. Talk to Alex W.
int StructTextDiffWithNull_dbg(char **estr, ParseTable tpi[], void *new_struct, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS);
#define StructTextDiffWithNull(estr, tpi, new_struct, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags) StructTextDiffWithNull_dbg(estr, tpi, new_struct, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, __FILE__, __LINE__)

//should only exist temporarily... calls both StructTextDiffWithNull and StructWriteTextDiff, asserts that they produce the same output
int StructTextDiffWithNull_Verify_dbg(char **estr, ParseTable tpi[], void *new_struct, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS);
#define StructTextDiffWithNull_Verify(estr, tpi, new_struct, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags) StructTextDiffWithNull_Verify_dbg(estr, tpi, new_struct, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, __FILE__, __LINE__)

//same as above, but starts anywhere in a struct hierarchy
void ParserTextDiffWithNull_dbg(char **estr, ParseTable tpi[], int column, int index, void *newp, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS);
#define ParserTextDiffWithNull(estr, tpi, column, index, new_struct, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags) ParserTextDiffWithNull_dbg(estr, tpi, column, index, new_struct, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, __FILE__, __LINE__)

//should only exist temporarily... calls both ParserTextDiffWithNull and ParserWriteTextDiff, asserts that they produce the same output
void ParserTextDiffWithNull_Verify_dbg(char **estr, ParseTable tpi[], int column, int index, void *newp, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS);
#define ParserTextDiffWithNull_Verify(estr, tpi, column, index, new_struct, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags) ParserTextDiffWithNull_Verify_dbg(estr, tpi, column, index, new_struct, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, __FILE__, __LINE__)


// Gets diff, starting with specific column/index as if it was a base object
void FieldWriteTextDiff_dbg(char **estr, ParseTable tpi[], int column, void *old_struct, void *new_struct, int index, char * prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS);
#define FieldWriteTextDiff(estr, tpi, column, old_struct, new_struct, index, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags) FieldWriteTextDiff_dbg(estr, tpi, column, old_struct, new_struct, index, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags MEM_DBG_PARMS_INIT)
 
// Gets diff, starting with specific column, including array indexes if necessary
void TokenWriteTextDiff_dbg(char **estr, ParseTable tpi[], int column, void *old_struct, void *new_struct, char * prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS);
#define TokenWriteTextDiff(estr, tpi, column, old_struct, new_struct, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags) TokenWriteTextDiff_dbg(estr, tpi, column, old_struct, new_struct, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags MEM_DBG_PARMS_INIT)

void ParserWriteTextDiff_dbg(char **estr, ParseTable tpi[], int column, int oldindex, int newindex,
							 void *oldp, void *newp, char *prefix,
							 StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude,
							 TextDiffFlags eFlags MEM_DBG_PARMS);
#define ParserWriteTextDiff(estr, tpi, column, old_index, new_index, old_struct, new_struct, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags) ParserWriteTextDiff_dbg(estr, tpi, column, old_index, new_index, old_struct, new_struct, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags MEM_DBG_PARMS_INIT)

// Binary I/O

typedef enum enumParserWriteBinaryFlags
{
	PARSERWRITE_IGNORECRC = 1 << 0,
	PARSERWRITE_ZEROTIMESTAMP = 1 << 1,
	PARSERWRITE_HUGEBUFFER = 1 << 2,
} enumParserWriteBinaryFlags;


//THESE ARE THE TWO FUNCTIONS THAT YOU SHOULD GENERALLY USE FOR READING/WRITING BINARY FILES
//(although you should only do that if you really know what you're doing. Normally, parserLoadFiles does it for you)

int ParserOpenReadBinaryFile(HogFile *hog_file, const char *filename, ParseTable pti[], void* structptr, FileList* filelist, FileList *filesWithErrorsList, DependencyList *deplist, DefineContext* defines, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, enumBinaryReadFlags eFlags);
int ParserWriteBinaryFile(const char* filename, const char *layoutFilename, ParseTable pti[], void* structptr, FileList* filelist, FileList *filesWithErrorsList, DependencyList *deplist, DefineContext* defines, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, HogFile *pHogFile, enumParserWriteBinaryFlags eFlags, enumBinaryFileHeaderFlags eHeaderFlagsToWrite); // returns success

//these functions are generally for internal/expert use only
SimpleBufHandle ParserOpenBinaryFile(HogFile *hog_file, const char *filename, ParseTable pti[], enumBinaryReadFlags eFlags, DefineContext* defines);
int ParserReadBinaryFile(SimpleBufHandle binfile, ParseTable pti[], void* structptr, FileList* filelist, FileList *filesWithErrorsList, DependencyList *deplist, DefineContext* defines, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool bCloseFile);	// returns success
//these functions are generally for internal/expert use only


// Registry I/O
//not fully compatible, just does toSimple and fromSimple
// Deprecated: you probably want to call GamePrefStoreStruct()
//int ParserReadRegistry(const char *key, ParseTable pti[], void *structptr);
//int ParserWriteRegistry(const char *key, ParseTable pti[], void *structptr, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

//fully compatible, but not space-efficient (stringifies the entire struct, then writes it, etc.)
int ParserReadRegistryStringified(const char *key, ParseTable pti[], void *structptr, const char *value_name);
int ParserWriteRegistryStringified(const char *key, ParseTable pti[], void *structptr, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *value_name);



///////////////////////////////////////////////////// ParserLoadFiles
// ParserLoadFiles is used to hide binary and text file representations.
// The most current representation is automatically used.  ParserLoadFiles
// will scan through directories looking for all text files matching an extension
// to load at one time.  Changes in either the source text files or the
// data definitions (ParseTable) will be automatically detected and
// a new binary file written.

// flags for ParserLoadFiles
//NOTE: correctness of this enum is checked by a call to VerifyFlagEnums in resourcemanager.c
AUTO_ENUM;
typedef enum enumParserLoadFlags
{
	PARSER_INCLUDEHIDDEN = (1 << 1),			// include hidden (_) directories
	PARSER_FORCEREBUILD	= (1 << 2),				// force a rebuild of .bin file
	PARSER_INTERNALLOAD	= (1 << 3),				// This is an internal load (during reload, etc), don't call certain callbacks.
	PARSER_DONTFREE	= (1 << 4),					// Do not free when copying to shared memory
	PARSER_OPTIONALFLAG	= (1 << 5),				// It is not an error if no files are loaded, or files are empty
	PARSER_SERVERSIDE = (1 << 7),				// We're on the server
	PARSER_CLIENTSIDE = (1 << 8),				// We're on the client
	PARSER_BINS_ARE_SHARED = (1 << 9),			// regardless of whether we're client or server, put .bin files in .bin, not server/bin
	PARSER_NOERRORFSONPARSE = (1 << 10),		// don't generate errorfs during parsing of text files.... useful for checking for validity
	PARSER_PARSECURRENTFILE = (1 << 11),		// parse current file, instead of getting it from the tokenizer
	PARSER_DONTSETEDITMODE = (1 << 12),			// Set this if the reload you're doing is part of normal loading, and shouldn't kick off the editor reload changes
	PARSER_USE_CRCS	= (1 << 13),				// Use CRCs instead of timestamps (slow, but not messed up by SVN dates)
	PARSER_IGNORE_ALL_UNKNOWN = (1 << 14),		// any time you see an unknown field name, skip the line, and the entire struct if it's a struct
	PARSER_NOINCLUDES = (1 << 15),				// don't treat INCLUDE at the beginning of a line as an include, it conflicts with something or other
	PARSER_NOERROR_ONIGNORE	= (1 << 16),		// do not trigger Errorf's on PARSER_IGNORE_ALL_UNKNOWN
	PARSER_DONTREBUILD = (1 << 17),				// Load persist file if available, but do not rebuild it, even if it is out-of-date.
	PARSER_HUGEBUFFERFORBINARYFILE = (1 << 18), // the binary file we write will be HUGE, allocate a bunch of RAM at once rather than resizing
												// (the size used will be HUGE_INITIAL_BUF_SIZE from serialize.c)
	PARSER_DEVMODE = (1 << 19),					// open this file as if in development mode, even if not in development mode
	PARSER_IGNORE_EXTENSIONS = (1 << 20),		// Ignore file extensions when reloading files to dictionary
	PARSER_NO_RELOAD = (1 << 21),				// Don't add a reload callback

	PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING = (1 << 22), //new mode for faster development, only active in dev mode. Basically,
												//when errors are found in text files, we still build the bin file, then we
												//record what text files had errors, write that into the bin file, and
												//then force a reload of those text files. Only legal for systems
												//which support reloading

	PARSER_ONLY_LOAD_BIN_FILE_IF_IT_HAD_NO_ERRORS = (1 << 23), //when loading in a bin file, only continue loading if
												// the flag BINARYHEADERFLAG_NO_DATA_ERRORS is set in its header

	//DO NOT ADD ANYTHING AFTER THIS
	PARSER_LASTPLUSONE,
	//DO NOT ADD ANYTHING AFTER THIS
} enumParserLoadFlags;

// preprocessor function format for ParserLoadFiles
typedef int (*ParserLoadPreprocessFunc)(ParseTable pti[], void* structptr);
	// if this function returns 0 for failure, the .bin file will NOT be created

bool ParserLoadFiles(const char* dir, const char* filemask, const char* persistfile, int flags, ParseTable pti[], void* structptr);
	// Loading multiple files is identical to taking all those files, appending them together, and loading them
	// as a single file. The typical usage is to have a parent TPI with an EArray of structs, and have one
	// or more struct per file
	//
	// When loading, first look for a persist (binary .bin) file. Compare its internal timestamps against the timestamps of the
	// actual source files. If it's newer, load directly from the bin file. Otherwise, create a new persist file
	// automatically.
	//
	// flags is a combination of PARSER_XXX flags
	// if dir: look for all files in dir and subdirs matching filemask
	// if !dir: just use single text file specified in filemask
	// okay to pass NULL for pli.  If pli is used, more data on status is returned
	// if a preprocessor function is used, it is called ONLY when creating a .bin file.  The .bin file
	// will be created with data already run through the preprocessor.
	// returns success

// Make an attempt to load a file and return false if it needs to be binned.
bool ParserCheckLoadFiles(const char* dir, const char* filemask, const char* persistfile, int flags, ParseTable pti[]);

// Loading files to/from Shared Memory

// Generate standard filenames
void MakeSharedMemoryName(const char *pchBinFilename, char **ppOutEString);

// Convert dir and filename to filespecs. Creates earray of strings
void MakeFileSpecFromDirFilename(const char *dir, const char *filename, char ***pppFileSpecs);

bool ParserLoadFilesShared_dbg(const char* sharedMemoryName, const char* dir, const char* filemask, const char* persistfile, int flags, 
	ParseTable pti[], void* structptr MEM_DBG_PARMS);
	// Attempts to acquire the data from shared memory, and if not, then load it from disk and puts it in shared
	// memory so the next caller will find it there.
	// Preprocessor: same as ParserLoadFiles
	// PostProcessor: This is after loading from bin, before copying to shared memory. Any resizing of data members
	//   that's required on live data must be done here.
	// PointerPostProcessor: After loading to shared memory, before locking that memory. Any backpointers may be 
	// set here, as long as it doesn't change the memory size.

#define ParserLoadFilesShared(sharedMemoryName,dir,filemask,persistfile,flags,pti,structptr) \
	ParserLoadFilesShared_dbg(sharedMemoryName,dir,filemask,persistfile,flags,pti,structptr MEM_DBG_PARMS_INIT)


int ParserLoadFromShared(const char* sharedMemoryName, int flags, ParseTable pti[], void* structptr, SharedMemoryHandle **shared_memory, char **pFixupString, const char *pDictName, ResourceDictionaryInfo *pMetadata, bool bLock);
	// Attempts to load the data from shared memory. Returns 1 on success, 0 on failure
	// shared_memory is modified to point to the handle returned
int ParserMoveToShared(SharedMemoryHandle *shared_memory, int flags, ParseTable pti[], void* structptr, ResourceDictionaryInfo *pMetadata);
	// Attempts to move the data to the shared memory handle that is passed in. We assume we have write access

// Loading files directly to/from Dictionaries

// Works the same as ParserLoadFiles, but directly in to a dictionary
bool ParserLoadFilesToDictionaryEx(const char* dirs, const char* filemask, const char* persistfile, int flags, const void * dictHandle,
	DependencyList extraBinFileDependencies);
#define ParserLoadFilesToDictionary(dirs, filemask, persistfile, flags, dicthandle) ParserLoadFilesToDictionaryEx(dirs, filemask, persistfile, flags, dicthandle, NULL)

// Wrapper around ParserLoadFilesShared
bool ParserLoadFilesSharedToDictionary_dbg(const char *sharedMemoryName, const char* dirs, const char* filemask, const char* persistfile, int flags, const void * dictHandle MEM_DBG_PARMS);
#define ParserLoadFilesSharedToDictionary(sharedMemoryName, dirs, filemask, persistfile, flags, dictHandle) ParserLoadFilesSharedToDictionary_dbg(sharedMemoryName, dirs, filemask, persistfile, flags, dictHandle MEM_DBG_PARMS_INIT)

// Load files into a dictionary and set a FolderCache callback; requires FolderCache.h.
#define ParserLoadFilesAndSetCallback(pchDir, pchExt, pchBin, hDict, eFlags, cbReload) \
	ParserLoadFilesToDictionary(pchDir, pchExt, pchBin, eFlags, hDict); \
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, pchDir "/*" pchExt, cbReload);

// Reads a single dictionary structure from a text file
bool ParserLoadSingleDictionaryStruct(const char* filename, const void *dictHandle, void *structptr, int flags);

// Same as ParserLoadSingleDictionaryStruct, but takes the parse name and parse table (Useful for fixing up files from deleted dictionaries)
bool ParserLoadSingleParseTableStruct(const char* filename, const char *pParseName, const char *pDeprecatedName, ParseTable *childTable, void *structptr, int flags);

// Writes out all members of the given dictionary that have filename set as their currentfile
// For all members, disregard the edit copy
int ParserWriteTextFileFromDictionary(const char* filename, const void *dictHandle, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

// Writes out a specific referent to disk. This will correctly save all contents of the 
// file the edit copy says it is in. Additionally, it will correctly save the file the referent
// used to be in, if it has changed.
int ParserWriteReferentFromDictionary(const void *dictHandle, const char *referentName, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

// Writes out a single structure to a text file, as if it was in the dictionary (deprecated in favor of WriteReferent)
int ParserWriteTextFileFromSingleDictionaryStruct(const char* filename, const void *dictHandle, void* structPtr, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

// Reloads a file, which involves removing and re-adding all elements defined in that file
bool ParserReloadFileToDictionaryWithFlags(const char *relpath, const void *dictHandle, int flags);
#define ParserReloadFileToDictionary(relpath, dictHandle) ParserReloadFileToDictionaryWithFlags((relpath), (dictHandle), 0)

// Call once per frame to validate all dictionaries that have been queued for validation
void ParserValidateQueuedDicts(void);

// Scans a given directory, which speeds up loading in dev mode
void ParserScanFiles(const char* dirs, const char* filemask);

// Bin dependencies

// Use these functions to modify the dependencies of a bin file. These are safe to call from POST_TEXT_READ or RESVALIDATE_POST_TEXT_READING

typedef enum DependencyType
{
	DEPTYPE_PARSETABLE,
	DEPTYPE_EXPR_FUNC,
	DEPTYPE_VALUE,

	//this bin file depends on a file that is NOT part of the normal file list system, but which, if it changes, should
	//force rebinning of this bin file. The data for the dependency is the timestamp of the file, with 0 meaning "does not exist"
	DEPTYPE_FILE,
} DependencyType;

// Add dependency on given file, with current time stamp
void ParserBinAddFileDep(TextParserState *tps, const char *filename);

// Add a dependency on CRC of a parse table
void ParserBinAddParseTableDep(TextParserState *tps, ParseTable *pTable);

// Add a dependency on CRC of a parse table
void ParserBinAddParseTableNameDep(TextParserState *tps, const char *pchTable);

// Add dependency on expression function definition
void ParserBinAddExprFuncDep(TextParserState *tps, const char *pchFuncName);

// Add dependency on arbitrary key/value pair, that must be registered below
void ParserBinAddValueDep(TextParserState *tps, const char *pchValueName);

// Say that the bin file being loaded has errors, and should never be considered "up to date"
void ParserBinHadErrors(TextParserState *tps);

// Register a new key/value pair for dependency checking
// Must be called BEFORE binning, probably in an AUTO_RUN
void ParserBinRegisterDepValue(const char *pchValueName, U32 val);

// Returns the value associated with a named dependency, for checking against one in bin file
U32 ParserBinGetValueForDep(DependencyType type, const char *pchName);


// Parser Reloading

// Using these functions to reload will attempt to reload the passed in structure in place
// If your data is in a dictionary, using ParserReloadFileToDictionary is required instead.
typedef enum eParseReloadCallbackType
{
	eParseReloadCallbackType_Add,
	eParseReloadCallbackType_Delete,
	eParseReloadCallbackType_Update,
} eParseReloadCallbackType;

// ParserReloadCallback second parameter is a copy of the old structure if the structure was updated, NULL otherwise
typedef int (*ParserReloadCallback)(void* structptr, void* oldStructCopy, ParseTable *, eParseReloadCallbackType);
bool ParserReloadFile(const char *relpath, ParseTable pti[], void *pOldStruct, ParserReloadCallback subStructCallback, int flags);
	// Reloads a file and replaces the affected elements in the original structure.  See .c file for full comments.

///////////////////////////////////////////////////// TextParser System Utilities


// if bin creating is forced, ParserLoadFiles will always create a .bin file
void ParserForceBinCreation(int set);	

// Run an internal test of the textparser and print to console
void TestTextParser(void);	

//FIXME make this a flag passed to each individual string-reading call
//if true, then don't cause errors on TOK_NON_NULL_REF (should only be used for entire servers where that concept
//is meaningless, like the logparser, as a global flag is very non-thread-safe)
void SetGloballyIgnoreNonNullRefs(bool bSet);

// If set, don't create indexed earrays until something is added to them
void SetLateCreateIndexedEArrays(bool bSet);

typedef struct UrlArgumentList UrlArgumentList;

AUTO_STRUCT;
typedef struct WriteHTMLContextInternalState
{
	int iDepth;
	int *iGeneratingTableStack;
	char **ppXPathSegments;

	//when writing URLs for things such as commands, we want to use the HTML_LINKOVERRIDE link rather
	//than the one we get from the xpath segments, but then need to tack xpath segments onto the end of it to
	//whatever depth is necessary
	char *pOverrideLink; AST(ESTRING)
	int iXpathDepthOfOverrideLink;

} WriteHTMLContextInternalState;

AUTO_STRUCT;
typedef struct WriteHTMLContext
{
	// Used internally by WriteHTML calls ... do not touch!
	WriteHTMLContextInternalState internalState;

	const char *pViewURL;           NO_AST
	const char *pCommandFormURL;    NO_AST
	const char *pCommandProcessURL; NO_AST

	UrlArgumentList *pUrlArgs;
	int iMaxDepth;
	bool bArrayContext : 1; // The top-level item passed in should be treated as an array (ignore index)
	bool bNeedToDoAccessLevelChecks : 1; //whether or not access level checks have already been done

} WriteHTMLContext;

LATELINK;
U32 TextParserHTML_GetTime(void);

void initWriteHTMLContext(WriteHTMLContext *pContext, 
						  bool bArrayContext,
						  UrlArgumentList *pUrlArgs, 
						  int iMaxDepth, 
						  const char *pCurrentXPath,
						  const char *pViewURL,
						  const char *pFormURL,
						  const char *pProcessURL,
						  bool bNeedsToDoAccessLevelChecks);
void shutdownWriteHTMLContext(WriteHTMLContext *pContext);

void ParserWriteHTML(char **estr, ParseTable *tpi, void *struct_mem, WriteHTMLContext *pContext);
void ParserWriteHTMLEx(char **estr, ParseTable *tpi, int column, void *struct_mem, int index, WriteHTMLContext *pContext);

void ParserWriteJSON(char **estr, ParseTable *tpi, void *struct_mem, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
bool ParseWriteJSONFile(FILE *out, ParseTable *tpi, void *struct_mem, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

void ParserWriteJSONEx(char **estr, ParseTable *tpi, int column, void *structptr, int index, WriteJsonFlags eFlags);

void * ParserReadJSON(const char *json_string, ParseTable *tpi, char **estrResult);

//Somethings to help our xml validate:
//The xml declaration tag. 
#define XML_DECLARATION "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"

void ParserWriteXMLEx(char **estr, ParseTable *pti, void *struct_mem, StructFormatField iOptions);
#define ParserWriteXML(estr, pti, struct_mem) ParserWriteXMLEx(estr, pti, struct_mem, 0);

//For writing out a single field.
void ParserWriteXMLField(char **estr, ParseTable *tpi, int col, void *struct_mem, int index, StructFormatField iOptions);

bool ParserWriteHDF(HDF *hdf, ParseTable *tpi, void *struct_mem);

void ParserWriteConfirmationForm(char **estr,
								 const char *cmd,
								 ParseTable *pParseTable,
								 int iCurrentColumn,
								 void *pCurrentObject,
								 int iCurrentIndex,
								 bool bIsRootStruct,
								 WriteHTMLContext *pContext);

void ParserGenerateCommand(char **estr,
						   const char *cmd,
						   ParseTable *pParseTable,
						   int iCurrentColumn,
						   void *pCurrentObject,
						   int iCurrentIndex,
						   bool bIsRootStruct,
						   WriteHTMLContext *pContext);

void ResolveSimpleFieldMacrosIntoEstring(char **estr,
						   const char *pInString,
						   ParseTable *pParseTable,
						   int iCurrentColumn,
						   void *pCurrentObject,
						   int iCurrentIndex,
						   bool bIsRootStruct,
						   WriteHTMLContext *pContext);

//CRC a single column of a TPI
#define TPICRCFLAG_IGNORE_NAME (1 << 1) // Affects root TPI only
#define TPICRCFLAG_IGNORE_SUBTABLE (1 << 2)
#define TPICRCFLAG_CRC_SUBSTRUCT_SIZE (1 << 3) // Passed recursively

#define TPICRCFLAG_ALWAYS_DESCEND_ONE_LEVEL (1 << 4) //always recurse one level of structs, even if you normally wouldn't. 
	//this flag is NOT passed recursively

void UpdateCRCFromParseInfoColumn(ParseTable pti[], int iColumn, U32 iFlags);
U32 GetCRCFromParseInfoColumn(ParseTable tpi[], int iColumn, U32 iFlags);

//memory debug allocation name used for structs with no name (uncommon)
//#define TRACK_NONAME
#ifdef TRACK_NONAME
#define MISC_TEXTPARSER_ALLOC_NAME (ignorableAssertmsg(0, "Textparser alloc with no name"),"textParserMisc")
#else
#define MISC_TEXTPARSER_ALLOC_NAME "textParserMisc"
#endif

//the "state" of text parser during parserLoadFiles and similar things. Passed around for threadsafety, no longer global.
typedef struct TextParserState {
	const char *parser_relpath;

	DependencyList preExistingDependencies; //usually NULL, if set, then these are external dependencies imposed
		//by code

	FileList parselist; // if non-zero, tokenizer will keep track of every file parsed
	DependencyList deplist; // list of dependencies of current bin file

	char *old_filemask;
	char *old_dirs;
	// globals to pass information to callbacks
	// lf_ == LoadFiles_xx, not Leonard
	char** lf_filemasks;
	char** lf_dirs;
	ParseTable* lf_pti;
	void* lf_structptr;
	int lf_loadedok;
	int lf_ignoreempty;
	int lf_loadedonefile;
	int lf_forcebincreate;
	int lf_forceBinCreateDoesntForceTextFileReading;
	int lf_noErrorfsOnRead;
	int lf_include_hidden;
	FileList lf_binfilelist;
	int lf_filesmissingfrombin;
	FileList FilesWithErrors;
	int flags;
} TextParserState;


//make sure to call TOK_GET_TYPE first
static __forceinline bool TypeIsInt(U32 eType)
{
	switch (eType)
	{
	case TOK_U8_X:			
	case TOK_INT16_X:	
	case TOK_INT_X:			
	case TOK_INT64_X:
	case TOK_BIT:
		return true;
	}

	return false;
}


//returns how much memory a struct takes up (recursively)
size_t StructGetMemoryUsage(ParseTable pti[], const void* structptr, bool bAbsoluteUsage);

void TextParserPreAutoRunInit(void);

//don't call this directly unless you know what you're doing. It's usually done for you by ParserLoadFiles
void StructSortIndexedArrays(ParseTable pti[], void* structptr);

//for functions which call structcreate_dbg but are internal to textparser, generates a reasonable
//creation comment to pass in (does no work if the tpi has no creation comment column
const char *GetCreationCommentFromFileAndLine_dbg(ParseTable pti[], const char *pFunction, const char *pCallerName, int line);
#define MaybeGetCreationCommentFromFileAndLine(pti, pCallerName, line) (ParserGetCreationCommentOffset(pti) ? GetCreationCommentFromFileAndLine_dbg(pti, __FUNCTION__, pCallerName, line) : NULL)

//the internal string writing function that does TP-compatible quoting and escaping. Don't use this directly unless you know what you're doing
void WriteQuotedString(FILE* out, const char* str, int tabs, int eol);

//not thread-safe, be very careful using this
bool FieldFailsHTMLAccessLevelCheck(ParseTable tpi[], int column, void *pStruct);

extern char *gpGlobalCreationComment;

static __forceinline void TextParser_SetGlobalStructCreationComment(char *pComment)
{
	estrCopy2(&gpGlobalCreationComment, pComment);
}

static __forceinline char *TextParser_GetGlobalStructCreationComment(void)
{
	return gpGlobalCreationComment;
}

//whenever this happens (should only be during startup/loading) something has changed which might
//change stored parse table CRCs, so invalidate them all
void Parser_InvalidateParseTableCRCs(void);

// textparser.h
typedef U64 StructTypeField;
typedef U32 StructFormatField;

//defined in AutoStructSupport.c, this is what LATEBINDs point to when they're not linked
extern ParseTable parse_NullStruct[];

extern bool gbProductionModeBins;
extern bool gbForceReadBinFilesForMultiplexedMakebins;


//stuff for debugging StructCRC... first uncomment DEBUG_CRCS and recompile, then 
//set the variable giDebugCRCs to true before calling STructCRC

//#define DEBUG_CRCS 1

#ifdef DEBUG_CRCS

extern bool giDebugCRCs;
extern char *pDebugCRCPrefix;

#define IF_DEBUG_CRCS(foo) if (giDebugCRCs) { foo }
#else
#define IF_DEBUG_CRCS(foo)
#endif



C_DECLARATIONS_END


#endif // __TEXTPARSER_H
