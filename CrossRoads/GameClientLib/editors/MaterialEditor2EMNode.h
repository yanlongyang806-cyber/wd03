//// A Material Editor Node is the center of all editing operations.
//// Each Material Template is a collection of nodes connected
//// together to create some sort of output.
////
//// You can also have guides, which are stored here, even though they are not nodes
#ifndef _MATERIALEDITOR2EMNODE_H
#define _MATERIALEDITOR2EMNODE_H
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct Mated2EditorDoc Mated2EditorDoc;
typedef struct Mated2Guide Mated2Guide;
typedef struct Mated2Input Mated2Input;
typedef struct Mated2Node Mated2Node;
typedef struct Mated2NodeOption Mated2NodeOption;
typedef struct ShaderGuide ShaderGuide;
typedef struct ShaderInputMapping ShaderInputMapping;
typedef struct ShaderOperation ShaderOperation;
typedef struct ShaderOperationDef ShaderOperationDef;
typedef struct ShaderOperationValues ShaderOperationValues;
typedef struct ShaderOperationSpecificValue ShaderOperationSpecificValue;
typedef struct UICheckButton UICheckButton;
typedef struct UIFlowchart UIFlowchart;
typedef struct UIFlowchartButton UIFlowchartButton;
typedef struct UIFlowchartNode UIFlowchartNode;
typedef struct UITextEntry UITextEntry;

extern ParseTable parse_Mated2NodeOption[];
#define TYPE_parse_Mated2NodeOption Mated2NodeOption

SA_RET_OP_VALID Mated2Node* mated2NewNodeByOpName(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_STR const char* name );
SA_RET_OP_VALID Mated2Node* mated2NewNodeByOpAndName(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_OP_VALID const ShaderOperationDef* op,
		SA_PARAM_NN_STR const char* name );
SA_RET_OP_VALID Mated2Node* mated2NewNodeByOp(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID ShaderOperation* op );
void mated2NodeFree( SA_PRE_NN_VALID SA_POST_P_FREE Mated2Node* node );

void mated2NodeLink( SA_PARAM_NN_VALID Mated2Node* sourceNode, SA_PARAM_NN_STR const char* sourceName,
					 SA_PARAM_NN_VALID Mated2Node* destNode, SA_PARAM_NN_STR const char* destName,
					 SA_PRE_NN_RELEMS(4) const U8 swizzle[ 4 ],
					 bool isCreateLink );

void mated2NodeSetIsPreview( SA_PARAM_NN_VALID Mated2Node* node, bool isPreview );

void mated2NodeUpdatePosition( SA_PARAM_NN_VALID Mated2Node* node );
int mated2NodeCountMaterialSpecificInput( SA_PARAM_NN_VALID Mated2Node* node );
SA_RET_NN_VALID ShaderOperation* mated2NodeShaderOp( SA_PARAM_NN_VALID Mated2Node* node );
SA_RET_NN_VALID __forceinline const ShaderOperation* mated2NodeShaderOpConst( SA_PARAM_NN_VALID const Mated2Node* node ) { return mated2NodeShaderOp( (Mated2Node*)node ); }
SA_RET_NN_VALID Mated2Node* mated2ShaderOpNode( SA_PARAM_NN_VALID ShaderOperation* op );
SA_RET_OP_VALID ShaderOperationValues* mated2NodeShaderOpValues( SA_PARAM_NN_VALID Mated2Node* node );
SA_RET_NN_VALID ShaderOperationSpecificValue* mated2NodeAddOperationSpecificValue(SA_PARAM_NN_VALID Mated2Node* node, SA_PARAM_NN_STR const char *inputName);
SA_RET_OP_VALID ShaderInputMapping* mated2NodeShaderInputMapping( SA_PARAM_NN_VALID Mated2Node* node );
SA_RET_NN_VALID Mated2EditorDoc* mated2NodeDoc( SA_PARAM_NN_VALID Mated2Node* node );
SA_RET_NN_STR const char* mated2NodeName( SA_PARAM_NN_VALID Mated2Node* node );
SA_RET_NN_STR const char* mated2NodeNameWithType(
		SA_PARAM_NN_VALID Mated2Node* node, char* buffer, int buffer_size );
SA_RET_NN_STR const char* mated2NodeGroup( SA_PARAM_NN_VALID const Mated2Node* node );
SA_RET_NN_STR const char* mated2NodeNotes( SA_PARAM_NN_VALID const Mated2Node* node );
SA_RET_OP_VALID Mated2Input* mated2FindNodeInputByName(
		SA_PARAM_OP_VALID Mated2Node* node, SA_PARAM_NN_STR const char* inputName );
SA_RET_NN_VALID Mated2Input** mated2NodeInputs( SA_PARAM_NN_VALID Mated2Node* node );
SA_RET_NN_VALID UIFlowchartNode* mated2NodeUI( SA_PARAM_NN_VALID Mated2Node* node );

void mated2NodeInstancedUpdate( SA_PARAM_NN_VALID Mated2Node* node, bool isInstanced );

// These functions are not the cleanest interface... but they work.
SA_RET_NN_VALID UICheckButton* mated2NodeInputListInheritValuesCheckButton(
		SA_PARAM_NN_VALID Mated2Node* node );
SA_RET_NN_VALID UITextEntry* mated2NodeInputListInheritValuesNameWidget(
		SA_PARAM_NN_VALID Mated2Node* node );
bool mated2NodeInheritValues( SA_PARAM_NN_VALID Mated2Node* node );

SA_ORET_NN_VALID Mated2Guide* mated2NewGuide(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID ShaderGuide* guide );

void mated2NodeOptionDestroy( SA_PRE_NN_VALID SA_POST_P_FREE Mated2NodeOption* nodeOption );

/// Some callbacks the flowchart system needs
bool mated2FlowchartLinkRequest(
		SA_PARAM_NN_VALID UIFlowchart* flowchart, SA_PARAM_NN_VALID UIFlowchartButton* source,
		SA_PARAM_NN_VALID UIFlowchartButton* dest, bool force, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
bool mated2FlowchartUnlinkRequest(
		SA_PARAM_NN_VALID UIFlowchart* flowchart, SA_PARAM_NN_VALID UIFlowchartButton* source,
		SA_PARAM_NN_VALID UIFlowchartButton* dest, bool force, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
bool mated2FlowchartLinkBegin(
		SA_PARAM_NN_VALID UIFlowchart* flow, SA_PARAM_NN_VALID UIFlowchartButton* source,
		SA_PARAM_NN_VALID UIFlowchartButton* dest, bool force,
		SA_PARAM_NN_VALID Mated2EditorDoc* doc );
bool mated2FlowchartLinkEnd(
		SA_PARAM_NN_VALID UIFlowchart* flow, SA_PARAM_NN_VALID UIFlowchartButton* source,
		SA_PARAM_NN_VALID UIFlowchartButton* dest, bool linked,
		SA_PARAM_NN_VALID Mated2EditorDoc* doc );

void mated2NodeAssertValid( SA_PARAM_NN_VALID const Mated2Node* node );

#endif

#endif
