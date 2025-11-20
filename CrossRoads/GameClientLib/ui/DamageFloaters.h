/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#ifndef DAMAGEFLOATERS_H__
#define DAMAGEFLOATERS_H__

GCC_SYSTEM

typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct CombatTrackerNet CombatTrackerNet;
typedef struct DamageFloat DamageFloat;

AUTO_ENUM;
typedef enum DamageFloatLayout
{
	kDamageFloatLayout_Linear,
	kDamageFloatLayout_Splatter,
	kDamageFloatLayout_ZigZag,
	kDamageFloatLayout_PrioritySplatter,
	kDamageFloatLayout_Arc,

	kDamageFloatLayout_Count, EIGNORE
} DamageFloatLayout;

AUTO_ENUM;
typedef enum DamageFloatMotion
{
	kDamageFloatMotion_Accelerate,
	kDamageFloatMotion_Linear,
	kDamageFloatMotion_Sticky,

	kDamageFloatMotion_Count, EIGNORE
} DamageFloatMotion;

AUTO_STRUCT;
typedef struct DamageFloatTargetBoxScalar
{
	F32 fHeightPercentThreshold;
	F32 fScaleMin;

} DamageFloatTargetBoxScalar;

AUTO_STRUCT;
typedef struct DamageFloatTemplate
{
	Expression *pColor; AST(NAME(ColorBlock) REDUNDANT_STRUCT(Color, parse_Expression_StructParam) LATEBIND REQUIRED)
	Expression *pScale; AST(NAME(ScaleBlock) REDUNDANT_STRUCT(Scale, parse_Expression_StructParam) LATEBIND REQUIRED)
	Expression *pVelocityX; AST(NAME(VelocityXBlock) REDUNDANT_STRUCT(VelocityX, parse_Expression_StructParam) LATEBIND)
	Expression *pVelocityY; AST(NAME(VelocityYBlock) REDUNDANT_STRUCT(VelocityY, parse_Expression_StructParam) LATEBIND REQUIRED)
	Expression *pLifetime; AST(NAME(LifetimeBlock) REDUNDANT_STRUCT(Lifetime, parse_Expression_StructParam) LATEBIND REQUIRED)
	Expression *pFont; AST(NAME(FontBlock) REDUNDANT_STRUCT(Font, parse_Expression_StructParam) LATEBIND REQUIRED)
	Expression *pText; AST(NAME(TextBlock) REDUNDANT_STRUCT(Text, parse_Expression_StructParam) LATEBIND REQUIRED)
	Expression *pX; AST(NAME(XBlock) REDUNDANT_STRUCT(X, parse_Expression_StructParam) LATEBIND REQUIRED)
	Expression *pY; AST(NAME(YBlock) REDUNDANT_STRUCT(Y, parse_Expression_StructParam) LATEBIND REQUIRED)
	Expression *pOffsetFrom; AST(NAME(OffsetFromBlock) REDUNDANT_STRUCT(OffsetFrom, parse_Expression_StructParam) LATEBIND REQUIRED)
	Expression *pGroup; AST(NAME(GroupBlock) REDUNDANT_STRUCT(Group, parse_Expression_StructParam) LATEBIND REQUIRED)
	Expression *pBottomColor; AST(NAME(BottomColorBlock) REDUNDANT_STRUCT(BottomColor, parse_Expression_StructParam) LATEBIND )
	Expression *pPriority; AST(NAME(PriorityBlock) REDUNDANT_STRUCT(Priority, parse_Expression_StructParam) LATEBIND )
	Expression *pPopout; AST(NAME(PopoutBlock) REDUNDANT_STRUCT(Popout, parse_Expression_StructParam) LATEBIND )
	Expression *pIconName; AST(NAME(IconBlock) REDUNDANT_STRUCT(IconName, parse_Expression_StructParam) LATEBIND )
	Expression *pIconOffsetFrom; AST(NAME(IconOffsetBlock) REDUNDANT_STRUCT(IconOffset, parse_Expression_StructParam) LATEBIND )
	Expression *pIconScale; AST(NAME(IconScaleBlock) REDUNDANT_STRUCT(IconScale, parse_Expression_StructParam) LATEBIND )
	Expression *pIconX; AST(NAME(IconXBlock) REDUNDANT_STRUCT(IconX, parse_Expression_StructParam) LATEBIND )
	Expression *pIconY; AST(NAME(IconYBlock) REDUNDANT_STRUCT(IconY, parse_Expression_StructParam) LATEBIND )
	Expression *pIconColor; AST(NAME(IconColorBlock) REDUNDANT_STRUCT(IconColor, parse_Expression_StructParam) LATEBIND )
	Expression *pMinArc; AST(NAME(MinArcBlock) REDUNDANT_STRUCT(MinArc, parse_Expression_StructParam) LATEBIND )
	Expression *pMaxArc; AST(NAME(MaxArcBlock) REDUNDANT_STRUCT(MaxArc, parse_Expression_StructParam) LATEBIND )
	Expression *pArcRadius; AST(NAME(RadiusBlock) REDUNDANT_STRUCT(Radius, parse_Expression_StructParam) LATEBIND )

	DamageFloatTargetBoxScalar *pTargetBoxScalar;

	bool bCombineSimilar;
	bool bDontAnchorToEntity;	AST(DEFAULT(0))

} DamageFloatTemplate;

AUTO_STRUCT;
typedef struct DamageFloatGroupTemplate
{
	DamageFloatLayout eLayout; AST(NAME(Layout))
	DamageFloatMotion eMotion; AST(NAME(Motion))

} DamageFloatGroupTemplate;

AUTO_STRUCT;
typedef struct DamageFloatGroupTemplates
{
	DamageFloatGroupTemplate **ppGroups; AST(NAME(Group))
} DamageFloatGroupTemplates;

extern DamageFloatTemplate g_DamageFloatTemplate;

DamageFloat *gclDamageFloatCreate(DamageFloatTemplate *pTemplate, CombatTrackerNet *pNet, Entity *pTarget, F32 fDelay, F32 fDamagePct);

void gclDamageFloatLayout(DamageFloatTemplate *pTemplate, Entity *pTarget);
void gclDamageFloatTick(DamageFloatTemplate *pTemplate, Entity *pTarget, F32 fElapsedTime);

void gclDrawDamageFloaters(Entity *pEnt, F32 fScale);

LATELINK;
void gclDamageFloat_Gamespecific_Preprocess(CombatTrackerNet *pNet, Entity *pTarget, F32 fDelay);

#endif /* #ifndef DAMAGEFLOATERS_H__ */

/* End of File */

