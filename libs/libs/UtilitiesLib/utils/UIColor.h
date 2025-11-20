#pragma once

GCC_SYSTEM

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIColor {
	Vec4 color;							AST( STRUCTPARAM )
	F32 fRandomWeight;					AST( STRUCTPARAM )
} UIColor;

AUTO_STRUCT;
typedef struct UIColorSet {
	const char *pcName;					AST( NAME("Name") STRUCTPARAM KEY POOL_STRING )
	const char *pcFilename;				AST( CURRENTFILE )

	UIColor **eaColors;                 AST( NAME("Color") )
	int rowSize;						AST( NAME("RowSize"))
} UIColorSet;

extern ParseTable parse_UIColor[];
#define TYPE_parse_UIColor UIColor
extern ParseTable parse_UIColorSet[];
#define TYPE_parse_UIColorSet UIColorSet

