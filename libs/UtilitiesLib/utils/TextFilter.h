#pragma once
GCC_SYSTEM
#ifndef _TEXT_FILTER_H_
#define _TEXT_FILTER_H_

typedef struct StashTableImp *StashTable;
typedef struct FilterTrieNode FilterTrieNode;

#define TRIE_MODE_B_MAX_NUM_CHILDREN 2

/// Fully expanded mode.  Each child is referenced by index.
typedef struct FilterTrieNodeChildrenA
{
	FilterTrieNode* children[256];
} FilterTrieNodeChildrenA;

/// Compact mode.  Can store up to COMPACT_MODE_NUM_CHILDREN children
/// and does a linear scan.  Consumes a lot less memory.
typedef struct FilterTrieNodeChildrenB
{
	struct {
		unsigned char character;
		FilterTrieNode* child;
	} children[TRIE_MODE_B_MAX_NUM_CHILDREN];
} FilterTrieNodeChildrenB;

/// The FilterTrie looks at profanity as a sequence of bytes.  This
/// allows it to be encoding agnostic and just use a 256-way tree.  It
/// doesn't care about UTF-8, Windows-1252, it's all just bytes.
///
/// Because it is looking at words and phrases, the trie is extremely
/// sparse.  It stores the children in one of two modes, based on the
/// number of children.
typedef struct FilterTrieNode
{
	unsigned isEnd : 1;
	unsigned ignoreSplitTokens : 1; // True if matches with delimiters should skipped
	unsigned ignore1337Coalesce : 1; // True if matches with 1337 matching should be skipped

	U8 numChildren;
	union {
		FilterTrieNodeChildrenA* a;
		FilterTrieNodeChildrenB* b;
	};
} FilterTrieNode;

extern FilterTrieNode* s_ProfanityTrie;
extern FilterTrieNode* s_RestrictedTrie;		// Words that aren't even allowed as substrings. e.g. Batman
extern FilterTrieNode* s_DisallowedNameTrie;	// Exact names that aren't allowed. e.g. Defender

#define TF_CENSOR_RANDOM_SYMBOL 1
#define TF_SCRAMBLE_TEXT true
#define TF_CONCAT_WORD_CHECK true

// Filter Matching functions
// This matches against all substrings and matches exactly, including all whitespace; eg. a filter string of "asdf" will match "qasdfq"
// To be used for Chat Blacklist and for languages with no specific word delimiters (eg. Chinese)
bool tf_IsAnyTokenInTrieExact( FilterTrieNode* trie, const char* text, int** out_eaRanges );

// This matches tokens based on Latin delimiters in TF_STD_DELIMS, and matches based delimited words
// To be used for filtering of Latin languages and any other languages that used the standard delimiters for word spacing
bool tf_IsAnyTokenInTrie( FilterTrieNode* trie, const char* text, int** out_eaRanges, bool bMatchTokenSubstrings );

bool IsAnyProfane( const char* pchText );
bool IsAnyProfaneList( const char* pchText, int** out_peaProfaneList );
bool ReplaceAnyWordProfane( char *pchText );

bool IsAnyRestricted( const char* pchText );

bool IsDisallowed( const char* pchText );

FilterTrieNode* tf_Create( void );
void tf_Free( FilterTrieNode* pTrie );
FilterTrieNode* tf_AddStringAlreadyNormalizedAndReduced( FilterTrieNode* pTrie, const char* str, bool bScrambled );
bool tf_RemoveStringAlreadyNormalizedAndReduced(FilterTrieNode* pTrie, const char* str, bool bScrambled);
void tf_InternString(const char* str, char** out_pestr );
void tf_DebugPrintTrie(FilterTrieNode* pTrie);

#endif
