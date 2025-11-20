#pragma once

AUTO_STRUCT;
typedef struct EntityAttach
{
	// Attachment to other entities
	EntityRef *eaiAttached;												AST(INT)
		// Other entities attached to this one

	// Child part
	EntityRef erAttachedTo;
		// Entity this entity is attached to, for movement

	const char *pBoneName;												AST(POOL_STRING)
		// Which bone on entity to attach to

	const char *pExtraBit;												AST(POOL_STRING)
		// Anim bit to add to inherited bits

	Vec3 posOffset;
		// Pos offset from attached bone

	Quat rotOffset;
		// Rotation offset

	bool bRiding;
		// Is this a riding attachment, or just a general one?
}EntityAttach;