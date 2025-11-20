#pragma once
GCC_SYSTEM

typedef struct WrlDef WrlDef;

typedef enum WrlDefType {
	WRL_DEFAULT = 0,
	WRL_TRANSFORM = WRL_DEFAULT,
}WrlDefType;

AUTO_STRUCT AST_STARTTOK("[") AST_ENDTOK("]");
typedef struct WrlChildren {
	//WrlShape wrlShape;
	WrlDef **wrlDefs;					AST( NAME(Def) POOL_STRING )
} WrlChildren;
extern ParseTable parse_WrlChildren[];
#define TYPE_parse_WrlChildren WrlChildren

AUTO_STRUCT AST_STARTTOK("{") AST_ENDTOK("}");
typedef struct WrlDef {
	const char		*name;				AST( STRUCTPARAM POOL_STRING )
	const char		*defType;			AST( STRUCTPARAM POOL_STRING )
	Vec3			position;			AST( NAME(Translation) )
	Vec3			pivot;				AST( NAME(Pivot) )
	WrlChildren		*wrlChildren;		AST( NAME(Children) )
} WrlDef;
extern ParseTable parse_WrlDef[];
#define TYPE_parse_WrlDef WrlDef

WrlDef* Wrl_CreateDef(WrlDefType wrlDefType);
void Wrl_SetDefType(WrlDef *wrlDef, WrlDefType wrlDefType);

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct WrlScene {
	const char	*filename;				AST( NAME(FN) CURRENTFILE )
	WrlDef		**wrlDefs;				AST( NAME(Def) )
} WrlScene;
extern ParseTable parse_WrlScene[];
#define TYPE_parse_WrlScene WrlScene