#include "TextFilter.h"

#include "StashTable.h"
#include "StringUtil.h"
#include "earray.h"
#include "file.h"
#include "rand.h"
#include "timing.h"

FilterTrieNode* s_ProfanityTrie;
FilterTrieNode* s_RestrictedTrie;
FilterTrieNode* s_DisallowedNameTrie;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
#define TF_STD_DELIMS " -_?.,!'\"\n\t\r"

#define tf_IsDelim(chSrc, pchDelim) ((pchDelim) && strchr((pchDelim), (unsigned char)(chSrc)))

static FilterTrieNode* tf_NodeGetChildInternal( FilterTrieNode* node, unsigned char character )
{
	if( node->numChildren <= TRIE_MODE_B_MAX_NUM_CHILDREN ) {
		int it;
		for( it = 0; it != ARRAY_SIZE( node->b->children ); ++it ) {
			if( node->b->children[ it ].character == character ) {
				return node->b->children[ it ].child;
			}
		}
		return NULL;
	} else {
		return node->a->children[ character ];
	}
}

static FilterTrieNode* tf_NodeGetChild( FilterTrieNode* node, unsigned char character )
{
	return tf_NodeGetChildInternal( node, character );
}

static unsigned char tf_De1337Single( unsigned char c )
{
	switch ( c )
	{
		case '0': return 'o';
		case '1': return 'l';
		case '2': return 'z';
		case '3': return 'e';
		case '4':
		case '@': return 'a';
		case '5':
		case '$': return 's';
		case '7': return 't';
		case '|': return 'i';
		case '6':
		case '9': return 'g';
		case '8': return 'b';
	}

	return c;
}

static unsigned char tf_UnscrambleChar( unsigned char c )
{
	if( TF_SCRAMBLE_TEXT ) {
		if( c != 0xA5 ) {
			return c ^ 0xA5;
		} else {
			return c;
		}
	} else {
		return c;
	}
}

static unsigned char tf_ScrambleChar( unsigned char c )
{
	if( TF_SCRAMBLE_TEXT ) {
		if( c != 0xA5 ) {
			return c ^ 0xA5;
		} else {
			return c;
		}
	} else {
		return c;
	}
}

//replace every non-delimiting character in pchText with a random character in pchReplace
static void tf_ReplaceRandom(	char* pchText,
								const char* pchReplace,
								U32 iLen,
								U32 iReplaceLen,
								const char* pchDelim )
{
	// Using the pointer as the seed allows the replacement to be deterministic for
	// a particular pointer, but otherwise random.
	// This is done in two steps because the compiler complains about directly casting
	// a pointer to a 32-bit integer (even though I don't care about potential information
	// loss.
	// This isn't perfect because if the text being replaced is edited and realloc'd, then
	// the pointer to the text will change.
	U64 iSeed64 = (U64) pchText;
	U32 iSeed32 = (U32) iSeed64;
	U32 i;

	for ( i = 0; i < iLen; i++ )
	{
		if ( !tf_IsDelim( pchText[i], pchDelim ) )
		{
			U32 iR = ( iReplaceLen == 1 ) ? 0 : randomIntRangeSeeded(&iSeed32, RandType_LCG, 0, iReplaceLen - 1 );

			pchText[i] = pchReplace[iR];
		}
	}
}

void tf_InternString(const char* str, char** out_pestr )
{
	char* estrNormalized = NULL;
	char* estrNormalizedAndReduced = NULL;
	estrCreate(&estrNormalized);
	estrCreate(&estrNormalizedAndReduced);
	UTF8NormalizeString( str, &estrNormalized );
	UTF8ReduceString( estrNormalized, &estrNormalizedAndReduced );

	estrClear( out_pestr );
	{
		int it;
		for( it = 0; estrNormalizedAndReduced[ it ]; ++it ) {
			if( !tf_IsDelim( estrNormalizedAndReduced[ it ], TF_STD_DELIMS )) {
				estrConcatChar( out_pestr, tf_ScrambleChar( estrNormalizedAndReduced[ it ]));
			}
		}
	}

	estrDestroy( &estrNormalized );
	estrDestroy( &estrNormalizedAndReduced );
}

static bool tf_IsTokenInTrie( FilterTrieNode* trie, const char* text )
{
	int it = 0;
	while( text[ it ]) {
		unsigned char cur = tolower( tf_De1337Single( text[ it ]));
		trie = tf_NodeGetChildInternal( trie, cur );
		if( !trie ) {
			return false;
		}

		++it;
	}

	return trie->isEnd;
}

// Utilities for getting cursor start&flag data
#define CSAF_CURSOR_MASK     0x00FFFFFF
#define CSAF_SPLIT_TOKEN     0x01000000
#define CSAF_1337_COALESE    0x02000000

static bool tf_CursorIsMatch( FilterTrieNode *cursor, int cursorFlags )
{
	return (cursor->isEnd &&
		(!cursor->ignoreSplitTokens || !(cursorFlags & CSAF_SPLIT_TOKEN)) &&
		(!cursor->ignore1337Coalesce || !(cursorFlags & CSAF_1337_COALESE)));
}

static const char* tf_ReduceString( const char* str )
{
	static char* estrBuffer = NULL;
	if( !str ) {
		return NULL;
	}

	UTF8ReduceString( str, &estrBuffer );

	// The text filter can return ranges, so it is important that
	// ReduceString reduces characters, and does not adjust positions
	// of any characters.
	if( isDevelopmentMode() ) {
		assert( strlen( str ) == estrLength( &estrBuffer ));
	}

	return estrBuffer;
}

// Matches ignoring all punctuation and delimiters, CSAF_SPLIT_TOKEN has no meaning here
// Eg. "asdf" matches "qasdfq", but does not match "as df" or "as.df"
// and "www.asdf" will match exactly "www.asdf"
bool tf_IsAnyTokenInTrieExact( FilterTrieNode* trie, const char* text, int** out_eaRanges )
{
	FilterTrieNode** cursors = NULL;
	int* cursorStartsAndFlags = NULL;
	int it;
	int initialSize = 0;
	text = tf_ReduceString( text );

	if( !text ) {
		return false;
	}

	eaSetCapacity( &cursors, 64 );
	eaiSetCapacity( &cursorStartsAndFlags, 64 );
	eaPush( &cursors, trie );
	eaiPush( &cursorStartsAndFlags, 0 );

	if( out_eaRanges ) {
		initialSize = eaiSize( out_eaRanges );
	}

	it = 0;
	while( text[ it ]) {
		unsigned char cur = text[ it ];
		unsigned char curNo1337 = tf_De1337Single( text[ it ]);
		int cursorIt;
		FilterTrieNode *match = NULL;
		int flagMatch = 0;

		for( cursorIt = eaSize( &cursors ) - 1; cursorIt >= 0; --cursorIt ) {
			FilterTrieNode* child = tf_NodeGetChildInternal( cursors[ cursorIt], cur );
			FilterTrieNode* childNo1337 = (cur == curNo1337 ? child : tf_NodeGetChildInternal( cursors[ cursorIt ], curNo1337 ));
			int flag = cursorStartsAndFlags[ cursorIt ];

			if (cursorIt == 0)
				flag = it;

			// If there's a de-1337 conversion, flag it
			if( childNo1337 && cur != curNo1337 ) {
				eaPush( &cursors, childNo1337 );
				eaiPush( &cursorStartsAndFlags, flag | CSAF_1337_COALESE );
				if (tf_CursorIsMatch(childNo1337, (flag | CSAF_1337_COALESE)))
				{
					match = childNo1337;
					flagMatch = flag | CSAF_1337_COALESE;
				}
			}

			if( !child ) {
				if (cursorIt != 0)
				{
					eaRemoveFast( &cursors, cursorIt );
					eaiRemoveFast( &cursorStartsAndFlags, cursorIt );
				}
			} else {
				if (cursorIt == 0)
				{
					eaPush(&cursors, child);
					eaiPush(&cursorStartsAndFlags, flag);
				}
				else
					cursors[ cursorIt ] = child;

				if( tf_CursorIsMatch (child, flag) )
				{
					match = child;
					flagMatch = flag;
				}
			}
		}

		++it;
		// Last match always has longest range, since it's the first added
		if (match)
		{
			if( out_eaRanges ) {
				eaiPush( out_eaRanges, flagMatch & CSAF_CURSOR_MASK );
				eaiPush( out_eaRanges, it );
			} else {
				eaDestroy( &cursors );
				eaiDestroy( &cursorStartsAndFlags );
				return true;
			}
		}
	}

	eaDestroy( &cursors );
	eaiDestroy( &cursorStartsAndFlags );
	if( out_eaRanges ) {
		return eaiSize( out_eaRanges ) != initialSize;
	} else {
		return false;
	}
}

// Does not support filter strings with delimiter characters in them, eg. "www.website.com"
bool tf_IsAnyTokenInTrie( FilterTrieNode* trie, const char* text, int** out_eaRanges, bool bMatchTokenSubstrings )
{
	FilterTrieNode** cursors = NULL;
	int* cursorStartsAndFlags = NULL;
	int it;
	int initialSize = 0;
	text = tf_ReduceString( text );

	if( !text ) {
		return false;
	}

	eaSetCapacity( &cursors, 64 );
	eaiSetCapacity( &cursorStartsAndFlags, 64 );
	eaPush( &cursors, trie );
	eaiPush( &cursorStartsAndFlags, 0 );

	if( out_eaRanges ) {
		initialSize = eaiSize( out_eaRanges );
	}

	it = 0;
	while( text[ it ]) {
		if( tf_IsDelim( text[ it ], TF_STD_DELIMS )) {
			int cursorIt;
			for( cursorIt = eaSize( &cursors ) - 1; cursorIt >= 0; --cursorIt ) {
				FilterTrieNode* cursor = cursors[ cursorIt ];
				int cursorStartAndFlag = cursorStartsAndFlags[ cursorIt ];
				if( tf_CursorIsMatch (cursor, cursorStartAndFlag) ) {
					if( out_eaRanges ) {
						eaiPush( out_eaRanges, cursorStartAndFlag & CSAF_CURSOR_MASK );
						eaiPush( out_eaRanges, it );
						eaClear( &cursors );
						eaiClear( &cursorStartsAndFlags );
						break;
					} else {
						eaDestroy( &cursors );
						eaiDestroy( &cursorStartsAndFlags );
						return true;
					}
				} else {
					cursorStartsAndFlags[ cursorIt ] |= CSAF_SPLIT_TOKEN;
				}
			}

			// advance past other delims
			do {
				++it;
			} while( text[ it ] && tf_IsDelim( text[ it ], TF_STD_DELIMS ));
			eaPush( &cursors, trie );
			eaiPush( &cursorStartsAndFlags, it );
		} else {
			unsigned char cur = text[ it ];
			unsigned char curNo1337 = tf_De1337Single( text[ it ]);
			int cursorIt;

			for( cursorIt = eaSize( &cursors ) - 1; cursorIt >= 0; --cursorIt ) {
				FilterTrieNode* child = tf_NodeGetChildInternal( cursors[ cursorIt ], cur );
				FilterTrieNode* childNo1337 = (cur == curNo1337 ? child : tf_NodeGetChildInternal( cursors[ cursorIt ], curNo1337 ));

				// If there's a de-1337 conversion, flag it
				if( childNo1337 && cur != curNo1337 ) {
					eaPush( &cursors, childNo1337 );
					eaiPush( &cursorStartsAndFlags, cursorStartsAndFlags[ cursorIt ] | CSAF_1337_COALESE );
				}

				if( !child ) {
					eaRemoveFast( &cursors, cursorIt );
					eaiRemoveFast( &cursorStartsAndFlags, cursorIt );
				} else {
					cursors[ cursorIt ] = child;

					// If this could be the start of a sequence of profanity, check for that too.
					if( TF_CONCAT_WORD_CHECK && cursors[ cursorIt ]->isEnd ) {
						eaPush( &cursors, trie );
						eaiPush( &cursorStartsAndFlags, cursorStartsAndFlags[ cursorIt ] & ~CSAF_SPLIT_TOKEN );
					}
				}
			}

			++it;

			if ( bMatchTokenSubstrings && text[ it ] && !tf_IsDelim( text[ it ], TF_STD_DELIMS ) )
			{
				eaPush( &cursors, trie );
				eaiPush( &cursorStartsAndFlags, it );
			}
		}
	}

	{
		int cursorIt;
		for( cursorIt = eaSize( &cursors ) - 1; cursorIt >= 0; --cursorIt ) {
			FilterTrieNode* cursor = cursors[ cursorIt ];
			int cursorStartAndFlag = cursorStartsAndFlags[ cursorIt ];
			if( cursor->isEnd && (!cursor->ignoreSplitTokens || !(cursorStartAndFlag & CSAF_SPLIT_TOKEN)) ) {
				if( out_eaRanges ) {
					eaiPush( out_eaRanges, cursorStartAndFlag & CSAF_CURSOR_MASK );
					eaiPush( out_eaRanges, it );
					eaClear( &cursors );
					eaiClear( &cursorStartsAndFlags );
					break;
				} else {
					eaDestroy( &cursors );
					eaiDestroy( &cursorStartsAndFlags );
					return true;
				}
			} else {
				cursorStartsAndFlags[ cursorIt ] |= CSAF_SPLIT_TOKEN;
			}
		}
	}

	eaDestroy( &cursors );
	eaiDestroy( &cursorStartsAndFlags );
	if( out_eaRanges ) {
		return eaiSize( out_eaRanges ) != initialSize;
	} else {
		return false;
	}
}

bool IsAnyProfane( const char* pchText )
{
	bool bIsProfane = false;
	PERFINFO_AUTO_START_FUNC();
	if( s_ProfanityTrie ) {
		bIsProfane = tf_IsAnyTokenInTrie( s_ProfanityTrie, pchText, NULL, false );
	} else {
		Errorf( "IsAnyProfane() called without initializing the profanity trie." );
	}
	PERFINFO_AUTO_STOP();
	return bIsProfane;
}

bool IsAnyProfaneList( const char* pchText, int** out_peaProfaneList )
{
	bool bIsProfane = false;
	PERFINFO_AUTO_START_FUNC();
	if( s_ProfanityTrie ) {
		bIsProfane = tf_IsAnyTokenInTrie( s_ProfanityTrie, pchText, out_peaProfaneList, false );
	} else {
		Errorf( "IsAnyProfane() called without initializing the profanity trie." );
	}
	PERFINFO_AUTO_STOP();
	return bIsProfane;
}

bool IsAnyRestricted( const char* pchText )
{
	bool bIsRestricted = false;
	PERFINFO_AUTO_START_FUNC();
	if( s_RestrictedTrie ) {
		bIsRestricted = tf_IsAnyTokenInTrie( s_RestrictedTrie, pchText, NULL, false );
	} else {
		Errorf( "IsAnyRestricted() called without initializing the restricted trie." );
	}
	PERFINFO_AUTO_STOP_FUNC();
	return bIsRestricted;
}

bool ReplaceAnyWordProfane( char *pchText )
{
	const char* pchReplace = "!@#$%^&*()";
	int* replaceRanges = NULL;
	PERFINFO_AUTO_START_FUNC();
	if( s_ProfanityTrie ) {
		tf_IsAnyTokenInTrie( s_ProfanityTrie, pchText, &replaceRanges, false );
	} else {
		Errorf( "ReplaceAnyWordProfane() called without initializing the profanity trie." );
	}
	PERFINFO_AUTO_STOP_FUNC();

	if( eaiSize( &replaceRanges )) {
		int it;
		for( it = 0; it < eaiSize( &replaceRanges ); it += 2 ) {
			int start = replaceRanges[ it + 0 ];
			int end = replaceRanges[ it + 1 ];

			tf_ReplaceRandom( pchText + start, pchReplace, end - start, (U32)strlen( pchReplace ), TF_STD_DELIMS );
		}

		eaiDestroy( &replaceRanges );
		return true;
	} else {
		eaiDestroy( &replaceRanges );
		return false;
	}
}

bool IsDisallowed( const char* pchText )
{
	if( s_DisallowedNameTrie ) {
		return tf_IsTokenInTrie( s_DisallowedNameTrie, pchText );
	} else {
		Errorf( "IsDisallowed() called without initializing the disallowed trie." );
		return false;
	}
}

FilterTrieNode* tf_Create( void )
{
	FilterTrieNode* accum = calloc( 1, sizeof( *accum ));
	accum->b = calloc( 1, sizeof( *accum->b ));
	return accum;
}

void tf_Free( FilterTrieNode* pTrie )
{
	if( !pTrie ) {
		return;
	}

	if( pTrie->numChildren <= TRIE_MODE_B_MAX_NUM_CHILDREN ) {
		int it;
		for( it = 0; it != ARRAY_SIZE( pTrie->b->children ); ++it ) {
			tf_Free( pTrie->b->children[ it ].child );
		}
		free( pTrie->b );
	} else {
		int it;
		for( it = 0; it != ARRAY_SIZE( pTrie->a->children ); ++it ) {
			tf_Free( pTrie->a->children[ it ]);
		}
		free( pTrie->a );
	}

	free( pTrie );
}

FilterTrieNode* tf_AddStringAlreadyNormalizedAndReduced( FilterTrieNode* pTrie, const char* str, bool bScrambled )
{
	unsigned char cur;
	FilterTrieNode* child;

	if( !str ) {
		return NULL;
	}

	cur = *str;
	if( cur == '\0' ) {
		pTrie->isEnd = true;
		return pTrie;
	}

	if (bScrambled)
		cur = tf_UnscrambleChar( cur );
	cur = tf_De1337Single( cur );

	child = tf_NodeGetChildInternal( pTrie, cur );
	if( !child ) {
		child = tf_Create();

		if( pTrie->numChildren == TRIE_MODE_B_MAX_NUM_CHILDREN ) {
			// About to transition between storage modes (B->A) by
			// adding a new child.
			FilterTrieNodeChildrenA* a = calloc( 1, sizeof( *a ));
			FilterTrieNodeChildrenB* b = pTrie->b;
			int it;
			for( it = 0; it != TRIE_MODE_B_MAX_NUM_CHILDREN; ++it ) {
				a->children[ b->children[ it ].character ] = b->children[ it ].child;
			}
			free( pTrie->b );
			pTrie->a = a;

			pTrie->a->children[ cur ] = child;
		} else if( pTrie->numChildren < TRIE_MODE_B_MAX_NUM_CHILDREN ) {
			// ModeB storage mode
			int it;
			for( it = 0; it != TRIE_MODE_B_MAX_NUM_CHILDREN; ++it ) {
				if( !pTrie->b->children[ it ].child ) {
					break;
				}
			}
			devassert( it < TRIE_MODE_B_MAX_NUM_CHILDREN );
			if( it < TRIE_MODE_B_MAX_NUM_CHILDREN ) {
				pTrie->b->children[ it ].character = cur;
				pTrie->b->children[ it ].child = child;
			}
		} else {
			// ModeA storage mode
			pTrie->a->children[ cur ] = child;
		}

		++pTrie->numChildren;
	}

	return tf_AddStringAlreadyNormalizedAndReduced( child, str + 1, bScrambled );
}

// Returns whether or not the node is safe to remove from its parent
bool tf_RemoveStringAlreadyNormalizedAndReduced(FilterTrieNode* pTrie, const char* str, bool bScrambled)
{
	unsigned char cur;
	bool bSafeToRemove;
	FilterTrieNode* child;

	if (!str)
		return false;
	cur = *str;
	if(cur == '\0')
	{
		if (!pTrie->isEnd)
			return false;
		pTrie->isEnd = false;
		return pTrie->numChildren == 0;
	}

	if (bScrambled)
		cur = tf_UnscrambleChar( cur );
	cur = tf_De1337Single( cur );

	child = tf_NodeGetChildInternal( pTrie, cur );
	if( !child ) {
		return false;
	}
	bSafeToRemove = tf_RemoveStringAlreadyNormalizedAndReduced( child, str+1, bScrambled );
	if (bSafeToRemove)
	{
		FilterTrieNode* toRemove = NULL;

		if( pTrie->numChildren == TRIE_MODE_B_MAX_NUM_CHILDREN + 1 ) {
			// About to transition between storage modes (A->B) by
			// removing a new child.
			FilterTrieNodeChildrenA* a = pTrie->a;
			FilterTrieNodeChildrenB* b = calloc( 1, sizeof( *b ));
			int it;
			int bPos;

			toRemove = a->children[ cur ];
			a->children[ cur ] = NULL;

			bPos = 0;
			for( it = 0; it != 256; ++it ) {
				if( a->children[ it ]) {
					if( bPos < TRIE_MODE_B_MAX_NUM_CHILDREN ) {
						b->children[ bPos ].character = it;
						b->children[ bPos ].child = a->children[ it ];
					}
					++bPos;
				}
			}
			devassert( bPos == TRIE_MODE_B_MAX_NUM_CHILDREN );
		} else if( pTrie->numChildren <= TRIE_MODE_B_MAX_NUM_CHILDREN ) {
			// ModeB storage mode
			int it;
			for( it = 0; it != TRIE_MODE_B_MAX_NUM_CHILDREN; ++it ) {
				if( pTrie->b->children[ it ].character == cur ) {
					toRemove = pTrie->b->children[ it ].child;
				}
			}
		} else {
			// ModeA storage mode
			toRemove = pTrie->a->children[ cur ];
			pTrie->a->children[ cur ] = NULL;
		}

		free( toRemove );
		--pTrie->numChildren;
		return pTrie->numChildren == 0;
	}
	else
		return false;
}

static void tf_DebugPrintTrie_Helper(FilterTrieNode* pTrie, int idx, char *buffer, int buffer_size)
{
	int i;
	bool buffer_resized = false;
	int next_idx = idx+1;
	if (pTrie->isEnd)
		printf("%s\n", buffer);

	if (idx >= buffer_size-1)
	{
		char *bufferCopy;
		buffer_resized = true;
		buffer_size *= 2;
		bufferCopy = malloc(buffer_size);
		strcpy_s(bufferCopy, buffer_size, buffer);
		buffer = bufferCopy;
	}
	for (i=0; i<256; i++) {
		FilterTrieNode* child = tf_NodeGetChildInternal( pTrie, i );
		if( child ) {
			buffer[idx] = (char) i;
			buffer[next_idx] = '\0';
			tf_DebugPrintTrie_Helper(child, next_idx, buffer, buffer_size);
		}
	}
	if (buffer_resized)
		free(buffer);
}

void tf_DebugPrintTrie(FilterTrieNode* pTrie)
{
	int i;
	char *buffer;

	if (!pTrie)
		return;
	buffer = malloc(256);
	buffer[1] = '\0';
	for (i=0; i<256; i++) {
		FilterTrieNode* child = tf_NodeGetChildInternal( pTrie, i );
		if( child ) {
			buffer[0] = (char) i;
			buffer[1] = '\0';
			tf_DebugPrintTrie_Helper(child, 1, buffer, 256);
		}
	}
	free(buffer);
}

static void tf_DebugPrintStats1( FilterTrieNode* pTrie, FILE* file, int depth )
{
	int numChildrenAccum = 0;
	int numSpansAccum = 0;
	bool prevIsOccupied;
	int it;

	if( !pTrie ) {
		return;
	}

	prevIsOccupied = false;
	for( it = 0; it != 256; ++it ) {
		FilterTrieNode* child = tf_NodeGetChildInternal( pTrie, it );
		if( child ) {
			++numChildrenAccum;
			if( !prevIsOccupied ) {
				++numSpansAccum;
			}
		}
		prevIsOccupied = (child != NULL);
	}

	fprintf( file, "%d,%d,%d\n", depth, numChildrenAccum, numSpansAccum );

	for( it = 0; it != 256; ++it ) {
		FilterTrieNode* child = tf_NodeGetChildInternal( pTrie, it );
		tf_DebugPrintStats1( child, file, depth + 1 );
	}
}

void tf_DebugPrintStats( FilterTrieNode* pTrie, FILE* file )
{
	fprintf( file, "Depth,Num Children,Num Spans\n" );
	tf_DebugPrintStats1( pTrie, file, 0 );
}

#if 0

AUTO_COMMAND;
void Profane_DebugPrintStats( void )
{
	FILE* out = fopen( "C:/profane_stats.csv", "w" );
	if( !out ) {
		return;
	}

	tf_DebugPrintStats( s_ProfanityTrie, out );
	fclose( out );
}

AUTO_COMMAND;
void Restricted_DebugPrintStats( void )
{
	FILE* out = fopen( "C:\restricted_stats.csv", "w" );
	if( !out ) {
		return;
	}

	tf_DebugPrintStats( s_ProfanityTrie, out );
	fclose( out );
}

#endif
