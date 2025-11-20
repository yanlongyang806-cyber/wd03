/***************************************************************************
 
 
 
 ***************************************************************************/
#ifndef SM_PARSER_H__
#define SM_PARSER_H__
#pragma once
GCC_SYSTEM

#define SM_MAX_PARAMS 14

/***************************************************************************/
/***************************************************************************/

typedef struct TupleSN
{
	char *pchName;
	int iVal;
} TupleSN;

typedef struct TupleSS
{
	// Data type which maps a string to another string.
	char *pchName;
	char *pchVal;
} TupleSS;

typedef enum SMAlignment
{
	kAlignment_None,
	kAlignment_Left,
	kAlignment_Right,
	kAlignment_Center,
	kAlignment_Top,
	kAlignment_Bottom,
	kAlignment_Both,
	// Don't let this get larger than 4 bits without changing the packing in SMPosition.
} SMAlignment;

typedef struct SMPosition
{
	S32 iY;
	S32 iHeight;
		// iY and iHeight may get quite large since most SMF is constrained by width.

	S16 iX;
	S16 iWidth;

	S16 iMinWidth;
	S16 iMinHeight;

	U8 iBorder;

	/* SMAlignment */ U32 alignHoriz : 4;
	/* SMAlignment */ U32 alignVert : 4;

} SMPosition;

typedef struct SMBlock SMBlock; // forward decl since it's self-referential

/***************************************************************************/
/***************************************************************************/

typedef struct SMBlock
{
	// Definition of an atomic item in the text stream. Can also contain
	// blocks hierarchically.

	SMPosition pos;
		// Location and dimension info for the block, inherited by children.

	void *pv;
		// holds whatever the caller gives us

	SMBlock **ppBlocks;
		// Managed array holding the blocks this block contains.

	SMBlock *pParent;
		// Points back to the owning block. Can be used for tree traversal.

	S16 sType;
		// An arbitrary type given by the user of the smparser so they can
		// tell what the following void * is.

	bool bFreeOnDestroy : 1;
		// true if pv should be freed on destroy.

	bool bHasBlocks : 1;
		// If this block contains others, this is true

	bool bAlwaysRender : 1;
		// If true, don't exit early on render if it "can't be drawn"

} SMBlock;

/***************************************************************************/
/***************************************************************************/

typedef SMBlock *(*SMTagProc)(SMBlock *pBlock, int iTag, TupleSS[SM_MAX_PARAMS]);
	// The callback function for tags

typedef SMBlock *(*SMTextProc)(SMBlock *pBlock, const char *pchText, int iLen);
	// The callback function for plain text. The given string is NOT
	// terminated by \0. Make sure you use iLen to determine where the string
	// ends.

typedef SMBlock *(*SMInitProc)(SMBlock *pBlock);
	// The callback function for creating the base container block.

typedef struct SMTagDef
{
	char *pchName;
		// tag name

	S16 id;
		// The identity (aka tag-type) of this def. Just make it the same
		//   as the pfn function name as a string with the SMF_TAG macro

	SMTagProc  pfn;
		// function to execute for this tag

	bool bSafe;
		// Allow this tag to be used in "safe" mode

	TupleSS aParams[SM_MAX_PARAMS];
		// Parameter name and default defs. The first one will be filled in
		// if no attrib name is given.

} SMTagDef;

#define SMF_TAG(x) k_##x, x
#define SMF_TAG_NONE(x) 0, x


// SMTagDef aTagDefs[] =
// {
//    { "tag",     SMF_TAG(DoTag),   {{ "param1", "default" }, { "param2", "moo" }, { "param3", "foo" }, { "param4", "bar" }}  },
//    { "tag2",    SMF_TAG(DoTag2),  {{ "param1", "default" }, { "param2", "moo" }, { "param3", "foo" }, { "param4", "bar" }} },
//    { 0 }, // required sentinel for end of list
// }

/***************************************************************************/
/***************************************************************************/

SMBlock *sm_CreateBlock(void);
	// Creates a new block.

void sm_DestroyBlock(SMBlock *pBlock);
	// Rescursively destroys the container block. After this function, all
	// storage will be released, including storage within other blocks.
	// Assumes that all void points which are not null are shallow and can
	// be released with free().

SMBlock *sm_AppendBlock(SMBlock *pBlockParent, SMBlock *pBlock);
	// Appends the given block to the parent's list of blocks.
	// Returns the new block.

SMBlock *sm_AppendNewBlock(SMBlock *pBlock);
	// Creates a new block and adds it to the given parent block. This
	// block cannot contain child blocks. Returns the new block.

SMBlock *sm_AppendNewBlockAndData(SMBlock *pBlock, int iSize);
	// Creates a new block and adds it to the given parent block. This
	// block cannot contain child blocks. Returns the new block. A buffer
	// of the given size is allocated from the heap, cleared, and put in
	// the block's data pointer.

SMBlock *sm_CreateContainer(void);
	// Creates a new block which holds other blocks.

SMBlock *sm_AppendNewContainer(SMBlock *pBlockParent);
	// Creates a new block which holds other blocks, adds it to the given
	// parent block. Returns the new block.

SMBlock *sm_AppendNewContainerAndData(SMBlock *pBlockParent, int iSize);
	// Creates a new block which holds other blocks, adds it to the given
	// parent block. Returns the new block. A buffer of the given size is
	// allocated from the heap, cleared, and put in the block's data pointer.

void sm_BlockDump(SMBlock *pBlock, int iLevel, SMTagDef aTagDefs[]);
	// A debugging function used to dump the given block at the given
	// indenting level. It assumes that the type of the block is equal
	// to the index in aTagDef.

SMBlock *sm_Parse(const char *pchOrig, SMTagDef aTagDefs[], bool bSafeOnly, SMInitProc pfnInit, SMTextProc pfnText);
	// Parses the given string using the standard conventions and the
	// callback functions given. Returns a container block which must
	// eventually be freed with SMDestroyBlock;

SMBlock *sm_ParseInto(SMBlock *pBlock, const char *pchOrig, SMTagDef aTagDefs[], bool bSafeOnly, SMTextProc pfnText);
	// Parses the given string using the standard conventions and the
	// callback functions given. Uses the container block passed in.
	// Use this function if special construction of the container block
	// needs to be done before parsing.

/* sm_util.c */

TupleSN *FindTupleSN(TupleSN *pTuples, char *pch);
TupleSS *FindTupleSS(TupleSS *pTuples, char *pch);

char *sm_GetVal(char *pchAttrib, TupleSS *pTuples);
	// Returns the value for a particular attrib in a parameter list.

int sm_GetAlignment(char *pchAttrib, TupleSS *pTuples);
	// Returns the alignment value for a particular attrib in a param list.

U32 sm_GetColor(char *pchAttrib, TupleSS *pTuples);
	// Returns a 32 bit hex color for a particular attrib. Supports a bunch
	// of named colors as well as HTML-ish #xxxxxx values.

#endif /* #ifndef SM_PARSER_H__ */

/* End of File */

