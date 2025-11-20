#pragma once
GCC_SYSTEM

typedef enum ReadStringFlags
{
	READSTRINGFLAG_BARSASCOMMAS = 1 << 0, //only relevant for earrays... treat | as ,

	READSTRINGFLAG_READINGEMPTYSTRINGALWAYSSUCCEEDS = 1 << 1, //when the last element of an earray you're reading is NULL, don't care that 
		//string_readString fails
} ReadStringFlags;

typedef enum WriteTextFlags
{
	WRITETEXTFLAG_PRETTYPRINT = 1 << 0,
	WRITETEXTFLAG_FORCEWRITECURRENTFILE = 1 << 1,

	// If true, fields with default values will be written if they are marked as used via a USEDFIELD.
	// This slows down writing significantly and so should not be the default.
	WRITETEXTFLAG_WRITEDEFAULTSIFUSED = 1 << 2,

	//if true, will get the current html access level from GetHTMLAccessLevel(). Then, for each field in a struct,
	//if that field has an access level in its format string, won't write it if the field's access level is too high
	WRITETEXTFLAG_USEHTMLACCESSLEVEL = 1 << 3,

	//if true, then check before writing your end token if you've actually written a single character. If not, then don't
	//write the end token either
	WRITETEXTFLAG_DONTWRITEENDTOKENIFNOTHINGELSEWRITTEN = 1 << 4,

	//if true, does a few things differently, such as how UNOWNED fields are treated
	WRITETEXTFLAG_WRITINGFORHTML = 1 << 5,

	//if true, both of the next entries
	WRITETEXTFLAG_WRITINGFORSQL = 1 << 6,

	//if true, then if nothing was written and you're going to write an EOL, only write the EOL if at least one thing was written
	WRITETEXTFLAG_DONTWRITEEOLIFNOTHINGELSEWRITTEN  = 1 << 7, 

	//if true, you are a direct embedded struct, so your rules may be different
	WRITETEXTFLAG_DIRECTEMBED = 1 << 8,

	//Treat it as required, needed for several weird cases involving arrays
	WRITETEXTFLAG_ISREQUIRED = 1 << 9,

	//this write is occuring as part of a struct write, as opposed to an individual field write of some sort
	WRITETEXTFLAG_STRUCT_BEING_WRITTEN = 1 << 10,

	//if you're writing out an enum value, it's OK if it has a space in it
	WRITETEXTFLAG_SPACES_OK_IN_ENUMS = 1 << 11,

	//never escape or quote string tokens
	WRITETEXTFLAG_NO_QUOTING_OR_ESCAPING_STRINGS = 1 << 12,

	//the text that is being written out will never be read back in, so relax some legality checking
	WRITETEXTFLAG_WRITINGFORDISPLAY = 1 << 13,

	//only write fields that are listed in USEDFIELD. If there is no USEDFIELD for this struct, write everything
	WRITETEXTLFAG_ONLY_WRITE_USEDFIELDS = 1 << 14,
} WriteTextFlags;

typedef enum TextParserResult
{
	PARSERESULT_SPECIAL_CALLBACK_RESULT = -2, //used by fixup functions to indicate that certain cases were in fact handled
	PARSERESULT_INVALID = -1, // The data is fatally invalid and should be removed
	PARSERESULT_ERROR = 0, // The data has non-fatal errors, but failed verification, and will not be removed
	PARSERESULT_SUCCESS = 1, // Data is correct
	PARSERESULT_PRUNE = 2, // Data is correct, but should be pruned anyway
} TextParserResult;


typedef enum enumTextParserFixupType
{
	FIXUPTYPE_NONE,

	//the first few fixup types are built-in fixups, which do NOT call the per-struct-type fixup funcs, but which
	//share the same basic recursing behavior
	FIXUPTYPE__BUILTIN__CLEAR_DIRTY_BITS,

	//REMEMBER TO UPDATE THIS IF YOU ADD BUILTINS AFTER THAT LAST BUILTIN
	FIXUPTYPE__BUILTIN__LAST = FIXUPTYPE__BUILTIN__CLEAR_DIRTY_BITS,

	//immediately after a text read has happened on the individual struct level
	//pExtraData is *pTextParserState
	FIXUPTYPE_POST_TEXT_READ,


	//immediately before a text read happens on the individual struct level
	//pExtraData is NULL
	FIXUPTYPE_PRE_TEXT_WRITE,

	//immediately after a bin read has happened on the individual struct level
	//pExtraData is NULL
	FIXUPTYPE_POST_BIN_READ,

	//immediately before a bin write happens on the individual struct level
	//pExtraData is NULL
	FIXUPTYPE_PRE_BIN_WRITE,

	//when StructDeInit is called (which is called by StructDestroy). Note that this recurses root-node-first,
	//so it will be called on the parent with still-intact children, then called on all children
	//pExtraData is NULL
	FIXUPTYPE_DESTRUCTOR,

	//when StructInit is called (which is called by Create)
	//pExtraData is NULL
	FIXUPTYPE_CONSTRUCTOR,

	//when something is gotten out of a resource dictionary (useful for doing time-specific fixup)
	FIXUPTYPE_GOTTEN_FROM_RES_DICT,

	//you are about to be viewed in servermonitoring... so fix up any fields such as 
	//ints that represent the size of an earray
	FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED,

	//the template copy of this struct type that is used to structCopy from is being created... do slow things
	//that modify struct memory directly, will then be memcpy onto each newly created struct. Good for adding
	//default values to vec3s, etc
	FIXUPTYPE_TEMPLATE_CONSTRUCTOR,

	//TOK_WANT_FIXUP_WITH_IGNORED_FIELD was set on an AST_IGNORE field during the TEXT_READ that just 
	//occurred
	FIXUPTYPE_HERE_IS_IGNORED_FIELD,

/*There are two fixuptypes for Structcopy. FIXUPTYPE_PRE_STRUCTCOPY is called on the guy who is about to be
copied FROM, right before the copy happens. FIXUPTYPE_POST_STRUCTCOPY is called on the guy who is being
copied TO, right after the copy happens. Both of these are tricky macros so that the structcopy code can
check whether the callbacks are necessary but you should never use the
internal flags directly

newly added: FIXUPTYPE_POST_STRUCTCOPY_SOURCE and FIXUPTYPE_PRE_STRUCTCOPY_DEST, which complete the set

*/
#define FIXUPTYPE_POST_STRUCTCOPY FIXUPTYPE_POSTSC_INTERNAL_CHECKFOREXISTENCE : return PARSERESULT_SPECIAL_CALLBACK_RESULT; case FIXUPTYPE_POSTSC_INTERNAL_ACTUAL
#define FIXUPTYPE_PRE_STRUCTCOPY FIXUPTYPE_PRESC_INTERNAL_CHECKFOREXISTENCE : return PARSERESULT_SPECIAL_CALLBACK_RESULT; case FIXUPTYPE_PRESC_INTERNAL_ACTUAL
#define FIXUPTYPE_POST_STRUCTCOPY_SOURCE FIXUPTYPE_POSTSC_SRC_INTERNAL_CHECKFOREXISTENCE : return PARSERESULT_SPECIAL_CALLBACK_RESULT; case FIXUPTYPE_POSTSC_SRC_INTERNAL_ACTUAL
#define FIXUPTYPE_PRE_STRUCTCOPY_DEST FIXUPTYPE_PRESC_DEST_INTERNAL_CHECKFOREXISTENCE : return PARSERESULT_SPECIAL_CALLBACK_RESULT; case FIXUPTYPE_PRESC_DEST_INTERNAL_ACTUAL
	
	FIXUPTYPE_POSTSC_INTERNAL_CHECKFOREXISTENCE,
	FIXUPTYPE_POSTSC_INTERNAL_ACTUAL,
	FIXUPTYPE_PRESC_INTERNAL_CHECKFOREXISTENCE,
	FIXUPTYPE_PRESC_INTERNAL_ACTUAL,

	FIXUPTYPE_POSTSC_SRC_INTERNAL_CHECKFOREXISTENCE,
	FIXUPTYPE_POSTSC_SRC_INTERNAL_ACTUAL,
	FIXUPTYPE_PRESC_DEST_INTERNAL_CHECKFOREXISTENCE,
	FIXUPTYPE_PRESC_DEST_INTERNAL_ACTUAL,



	//-----------------DEPRECATED----------------
	// The rest of these callbacks are deprecated in favor of RESVALIDATE callbacks

	//-----------------NOTE: All the fixups with "DURING_LOADFILES" in their names happen ONLY during 
	//calls to variations of ParserLoadFiles, not during (for instance) ParserReadTextFile.

	//during parserLoadFiles, after all text reading is complete, also after inheritance has been
	//applied
	//pExtraData is NULL
	FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES,

	//during parserloadfiles, after all loading has happened, BEFORE moving to shared memory (if happening)
	// (used to be postProcess)
	//pExtraData is NULL
	FIXUPTYPE_POST_BINNING_DURING_LOADFILES,

	//during parserloadfiles, after all loading has happened, AFTER being put in its final location (ie, after
	//being moved to shared memory, or after we decided not to move it to shared memory)
	// (used to be pointerPostProcess)
	//pExtraData is NULL
	FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION,

	//during a reload, after all reloading has occurred and the newly created objects have been put into their
	//reference dictionary (if any)
	//pExtraData is NULL
	FIXUPTYPE_POST_RELOAD,

	//This object inherits, and the inheriting fields from its InheritanceData were just applied. Presumably, the
	//whole object was also recopied from the Parent first, so any and all fixup stuff might need to happen
	//
	//Note that this is NOT called recursively. It's only called on the actual object which has inheritance data
	//pExtraData is NULL
	FIXUPTYPE_POST_INHERITANCE_APPLICATION,

	

	//a fixup that happens
} enumTextParserFixupType;

//used by textparser, also by lower level things like serialize.c
typedef enum enumBinaryReadFlags
{
	BINARYREADFLAG_IGNORE_CRC = 1 << 0,
	BINARYREADFLAG_NONMATCHING_SIGNATURE_NON_FATAL = 1 << 1,

	//only successfuly open the file if BINARYHEADERFLAG_NO_DATA_ERRORS is set in the header
	BINARYREADFLAG_REQUIRE_NO_ERRORS_FLAG = 1 << 2, 
} enumBinaryReadFlags;


//flags that are actually written into .bin files in the header, then read, and used to make decisions about
//whether to read this bin file, etc.
typedef enum enumBinaryFileHeaderFlags
{
	BINARYHEADERFLAG_NO_DATA_ERRORS = 1 << 0, //there were no data errors encountered
		//while reading the text files that made up this data file
} enumBinaryFileHeaderFlags;