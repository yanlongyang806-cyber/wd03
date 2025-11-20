#ifndef UI_GEN_DND_H
#define UI_GEN_DND_H
GCC_SYSTEM

typedef struct UIGen UIGen;
typedef struct UIGenDragDropAction UIGenDragDropAction;
extern StaticDefineInt MouseButtonEnum[];

AUTO_STRUCT;
typedef struct UIGenDragDropTarget
{
	UIGen *pGen; AST(UNOWNED)
	UIGenAction QueuedAction;
} UIGenDragDropTarget;

AUTO_STRUCT;
typedef struct UIGenDragDrop
{
	UIGen *pSource; AST(UNOWNED)
	const char *pchType; AST(POOL_STRING)
	char *pchTextPayload;
	S32 iIntPayload;
	F32 fFloatPayload;
	void *pPointerPayload; NO_AST
	ParseTable *pPointerType; NO_AST

	const char *pchCursor; AST(POOL_STRING)
	MouseButton eButton; AST(SUBTABLE(MouseButtonEnum))
} UIGenDragDrop;

void ui_GenDragDropStart(UIGen *pSource, const char *pchType, const char *pchPayload, S32 iIntPayload, F32 fFloatPayload,
						 void *pPointerPayload, ParseTable *pPointerType, const char *pchCursor, MouseButton eButton, int hotX, int hotY);

UIGenDragDropAction *ui_GenDragDropWillAccept(UIGen *pTarget);
bool ui_GenDragDropAccept(UIGen *pTarget);

void ui_GenDragDropOncePerFrame(void);
void ui_GenDragDropCancel(void);
bool ui_GenIsDragging(void);
bool ui_GenDragWasDropped(CBox *pBox);
UIGen *ui_GenDragDropGetSource(void);
const char *ui_GenDragDropGetType(void);
void ui_GenDragDropUpdate(UIGen *pGen);
const char *ui_GenDragDropGetStringPayload(void);

#endif