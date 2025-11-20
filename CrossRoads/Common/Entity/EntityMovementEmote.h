#pragma once
GCC_SYSTEM

/***************************************************************************
*     Copyright (c) 2005-Present, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

typedef struct MovementManager		MovementManager;
typedef struct MovementRequester	MovementRequester;

extern ParseTable parse_MREmoteFX[];
#define TYPE_parse_MREmoteFX MREmoteFX
extern ParseTable parse_MREmoteSet[];
#define TYPE_parse_MREmoteSet MREmoteSet

AUTO_STRUCT;
typedef struct MREmoteFX {
	const char*		name;				AST(POOL_STRING)
	U32				isMaintained : 1;
} MREmoteFX;

AUTO_STRUCT;
typedef struct MREmoteSetFlags {
	U32				destroyOnMovement	: 1;
	U32				played				: 1;
	U32				destroyed			: 1;	AST(NAME("Destroyed"))
} MREmoteSetFlags;

AUTO_STRUCT;
typedef struct MREmoteSet {
	U32				handle;
	U32				animToStart;
	U32*			stances;					AST(NAME("Stances"))
	U32*			animBitHandles;
	U32*			flashAnimBitHandles;
	MREmoteFX**		fx;
	U32*			fxHandles;
	MREmoteSetFlags	flags;
} MREmoteSet;

// EmoteMovement.

S32		mrEmoteCreate(	MovementManager* mm,
						MovementRequester** mrOut);

S32		mrEmoteSetCreate(	MovementRequester* mr,
							MREmoteSet** setInOut,
							U32* handleInOut);

S32		mrEmoteSetDestroy(	MovementRequester* mr,
							U32* handleInOut);
