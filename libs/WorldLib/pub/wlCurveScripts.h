#pragma once
GCC_SYSTEM

#include "wlCurve.h"

AUTO_STRUCT;
typedef struct CurveScriptParameterValue
{
	char			*value_name; AST(KEY)
	int				value_enum;
} CurveScriptParameterValue;

AUTO_STRUCT;
typedef struct CurveScriptParameterDef
{
	char								*param_name;	AST(KEY)
	char								**param_values;	// List of CurveScriptParameterValue names
} CurveScriptParameterDef;

AUTO_STRUCT;
typedef struct CurveScriptParameter
{
	REF_TO(CurveScriptParameterDef)		param;			AST(REFDICT(CurveScriptParameterDef))
	REF_TO(CurveScriptParameterValue)	param_value;	AST(REFDICT(CurveScriptParameterValue))
} CurveScriptParameter;

typedef struct CurveScriptNodeConnector
{
	struct CurveScriptElement	*parent;
	Vec3						exit_vec;
	F32							spline_index;
	struct CurveScriptSegment	*segments[2]; // pre-node and post-node
	void						*userdata;
} CurveScriptNodeConnector;

typedef struct CurveScriptNode
{
	Vec3 world_position;
	Vec3 world_direction;
	Vec3 world_up;
	struct CurveScriptNodeConnector **connectors;
} CurveScriptNode;

typedef struct CurveScriptSegment
{
	int							index;
	struct CurveScriptElement	*parent;
	CurveScriptNode				*nodes[2];
	CurveScriptNodeConnector	*connectors[2];
} CurveScriptSegment;

AUTO_STRUCT;
typedef struct CurveScriptElement
{
	Spline						spline;

	CurveScriptParameter		**parameters;
	U32					seed;

	bool						dirty;			NO_AST
	bool						was_dirty;		NO_AST	// Set in curveElementsUpdateAll iff dirty bit was set

	CurveScriptNode				**nodes;		NO_AST	// Ordered list
	CurveScriptSegment			**segments;		NO_AST	// length(nodes) + 1
} CurveScriptElement;

extern ParseTable parse_CurveScriptElement[];
#define TYPE_parse_CurveScriptElement CurveScriptElement
extern ParseTable parse_CurveScriptParameterValue[];
#define TYPE_parse_CurveScriptParameterValue CurveScriptParameterValue
extern ParseTable parse_CurveScriptParameterDef[];
#define TYPE_parse_CurveScriptParameterDef CurveScriptParameterDef
extern ParseTable parse_CurveScriptParameter[];
#define TYPE_parse_CurveScriptParameter CurveScriptParameter

void curveElementsUpdateAll(CurveScriptElement **element_list);

void curveScriptDrawDebug(CurveScriptElement *element);

CurveScriptParameterValue *curveScriptGetParameter(CurveScriptSegment *segment, char *param_name);
