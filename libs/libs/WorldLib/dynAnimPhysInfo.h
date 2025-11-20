#pragma once
GCC_SYSTEM

#include "referencesystem.h"


AUTO_ENUM;
typedef enum eBouncerType
{
	eBouncerType_Linear, ENAMES(Linear)
	eBouncerType_Linear2, ENAMES(Linear2)
	eBouncerType_Hinge, ENAMES(Hinge)
	eBouncerType_Hinge2, ENAMES(Hinge2)
} eBouncerType;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynBouncerInfo
{
	const char*			pcBoneName;		AST(POOL_STRING STRUCTPARAM)
	eBouncerType		eType;
	F32					fSpring; AST(NAME(spring))
	F32					fDampRate;
	F32					fMaxDist;
	Quat				qRot; AST(NAME(Rotation))
} DynBouncerInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynBouncerGroupInfo
{
	const char*			pcInfoName;		AST(KEY POOL_STRING)
	const char*			pcFileName;		AST(CURRENTFILE)
	DynBouncerInfo**	eaBouncer;		AST(NAME(DynBouncer))
} DynBouncerGroupInfo;

void dynBouncerInfoLoadAll(void);
