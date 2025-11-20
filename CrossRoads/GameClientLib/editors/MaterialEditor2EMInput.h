//// Each node in the Material Editor can have many different of
//// inputs.
#ifndef _MATERIALEDITOR2EMINPUT_H
#define _MATERIALEDITOR2EMINPUT_H
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct Mated2Input Mated2Input;
typedef struct Mated2Node Mated2Node;
typedef struct UIWidget UIWidget;
typedef struct Mated2EditorDoc Mated2EditorDoc;

SA_RET_NN_VALID Mated2Input* mated2ColorPickerCreate(
		SA_PARAM_NN_VALID Mated2Node* node, SA_PARAM_OP_STR const char* op_name, SA_PARAM_NN_STR const char* name,
		SA_PARAM_OP_STR const char* tooltip, bool allowLock,
		bool allowAlpha, Vec2 floatRange );
SA_RET_NN_VALID Mated2Input* mated2TexturePickerCreate(
		SA_PARAM_NN_VALID Mated2Node* node, SA_PARAM_NN_VALID const char* name,
		SA_PARAM_OP_STR const char* tooltip, bool allowLock,
		SA_PARAM_OP_STR const char* default_string);
SA_RET_NN_VALID Mated2Input* mated2FloatPickerCreate(
		SA_PARAM_NN_VALID Mated2Node* node, SA_PARAM_NN_STR const char* name,
		SA_PARAM_OP_STR const char* tooltip, bool allowLock,
		int numFloats, Vec2 floatRange );
SA_RET_NN_VALID Mated2Input* mated2UnsupportedInputCreate(
		SA_PARAM_NN_VALID Mated2Node* node, SA_PARAM_NN_STR const char* name );
void mated2InputFree(SA_PRE_NN_VALID SA_POST_NN_FREE Mated2Input* input);

bool mated2InputIsLocked( SA_PARAM_NN_VALID const Mated2Input* input );
SA_RET_NN_VALID UIWidget* mated2InputNodeViewWidget( SA_PARAM_NN_VALID const Mated2Input* input );
SA_RET_NN_VALID UIWidget* mated2InputInputListWidget( SA_PARAM_NN_VALID const Mated2Input* input );
SA_RET_NN_STR const char* mated2InputName( SA_PARAM_NN_VALID const Mated2Input* input );
SA_RET_NN_VALID Mated2EditorDoc* mated2InputDoc( SA_PARAM_NN_VALID const Mated2Input* input );

void mated2InputUpdateIsActive( SA_PARAM_NN_VALID Mated2Input* input, bool isTemplateEditable );
void mated2InputSetValues(
		SA_PARAM_NN_VALID Mated2Input* input, SA_PRE_OP_RELEMS(4) const F32* values,
		SA_PRE_OP_RELEMS(4) const char** svalues, bool isLocked );
void mated2InputSetValuesToDefault( SA_PARAM_NN_VALID Mated2Input* input );
void mated2InputHidden( SA_PARAM_NN_VALID Mated2Input* input );

void mated2InputAssertValid( SA_PARAM_NN_VALID const Mated2Input* input );

#endif

#endif
