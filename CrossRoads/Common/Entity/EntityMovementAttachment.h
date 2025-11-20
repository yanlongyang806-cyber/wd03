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

typedef struct MovementRequesterMsg		MovementRequesterMsg;
typedef U32								EntityRef;

AUTO_STRUCT;
typedef struct MMRAttachmentConstant {
	EntityRef								erParent;
	Vec3									pyrOffset;
	Vec3									vecOffset;
} MMRAttachmentConstant;

AUTO_STRUCT;
typedef struct MMRAttachmentConstantNP {
	U32 unused;
} MMRAttachmentConstantNP;

S32 mrAttachmentCreateBG(	const MovementRequesterMsg* msg,
							const MMRAttachmentConstant* constant,
							U32* handleOut);

S32 mrAttachmentDestroyBG(	const MovementRequesterMsg* msg,
							U32* handleInOut);
