#pragma once

typedef enum AttribType AttribType;
typedef enum AttribAspect AttribAspect;

AUTO_STRUCT;
typedef struct AttribBonusUIElement
{
	// Affected attribute
	AttribType eAttrib;

	// The aspect of the attribute affected
	AttribAspect eAspect;

	// Translated display name of the affected attribute
	char *pchDisplayName;

	// Bonus amount
	F32 fBonus;
} AttribBonusUIElement;

#define TYPE_parse_AttribBonusUIElement AttribBonusUIElement