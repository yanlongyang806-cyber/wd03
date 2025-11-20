
#pragma once

#include "stdtypes.h"
#include "timing_profiler.h"

typedef struct SUIMainWindow				SUIMainWindow;
typedef struct SUIWindow					SUIWindow;
typedef struct SUIWindowPipe				SUIWindowPipe;
typedef struct SUIWindowProcessingHandle	SUIWindowProcessingHandle;

// --- SUIWindow Functions ------------------------------------------------------------------------

// Structs and enums.

typedef struct SUIDrawContext SUIDrawContext;

typedef enum SUIKey {
	SUI_KEY_HOME,
	SUI_KEY_END,
	SUI_KEY_PAGE_DOWN,
	SUI_KEY_PAGE_UP,
	
	SUI_KEY_UP,
	SUI_KEY_DOWN,
	SUI_KEY_LEFT,
	SUI_KEY_RIGHT,
	SUI_KEY_INSERT,
	SUI_KEY_DELETE,
	
	SUI_KEY_ENTER,
	SUI_KEY_ESCAPE,
	SUI_KEY_BACKSPACE,
	SUI_KEY_TAB,
	SUI_KEY_SPACE,

	SUI_KEY_LSHIFT,
	SUI_KEY_RSHIFT,
	SUI_KEY_LCONTROL,
	SUI_KEY_RCONTROL,
	SUI_KEY_LALT,
	SUI_KEY_RALT,
	
	SUI_KEY_F1,
	SUI_KEY_F2,
	SUI_KEY_F3,
	SUI_KEY_F4,
	SUI_KEY_F5,
	SUI_KEY_F6,
	SUI_KEY_F7,
	SUI_KEY_F8,
	SUI_KEY_F9,
	SUI_KEY_F10,
	SUI_KEY_F11,
	SUI_KEY_F12,
	
	SUI_KEY_A,
	SUI_KEY_B,
	SUI_KEY_C,
	SUI_KEY_D,
	SUI_KEY_E,
	SUI_KEY_F,
	SUI_KEY_G,
	SUI_KEY_H,
	SUI_KEY_I,
	SUI_KEY_J,
	SUI_KEY_K,
	SUI_KEY_L,
	SUI_KEY_M,
	SUI_KEY_N,
	SUI_KEY_O,
	SUI_KEY_P,
	SUI_KEY_Q,
	SUI_KEY_R,
	SUI_KEY_S,
	SUI_KEY_T,
	SUI_KEY_U,
	SUI_KEY_V,
	SUI_KEY_W,
	SUI_KEY_X,
	SUI_KEY_Y,
	SUI_KEY_Z,
	
	SUI_KEY_0,
	SUI_KEY_1,
	SUI_KEY_2,
	SUI_KEY_3,
	SUI_KEY_4,
	SUI_KEY_5,
	SUI_KEY_6,
	SUI_KEY_7,
	SUI_KEY_8,
	SUI_KEY_9,

	SUI_KEY_MINUS,
	SUI_KEY_PLUS,
	SUI_KEY_LEFT_BRACKET,
	SUI_KEY_RIGHT_BRACKET,
} SUIKey;

typedef enum SUIKeyModifier {
	SUI_KEY_MOD_CONTROL 			= BIT(0),
	SUI_KEY_MOD_ALT					= BIT(1),
	SUI_KEY_MOD_SHIFT				= BIT(2),

	SUI_KEY_MOD_CONTROL_ALT			= SUI_KEY_MOD_CONTROL | SUI_KEY_MOD_ALT,
	SUI_KEY_MOD_CONTROL_SHIFT		= SUI_KEY_MOD_CONTROL | SUI_KEY_MOD_SHIFT,
	SUI_KEY_MOD_SHIFT_ALT			= SUI_KEY_MOD_SHIFT | SUI_KEY_MOD_ALT,
	SUI_KEY_MOD_CONTROL_SHIFT_ALT	= SUI_KEY_MOD_CONTROL | SUI_KEY_MOD_SHIFT | SUI_KEY_MOD_ALT,
} SUIKeyModifier;

typedef enum SUIWindowMsgType {
	// Create/Destroy messages.
	
	SUI_WM_CREATE,
	SUI_WM_DESTROY,
	
	// Process.
	
	SUI_WM_PROCESS,
	
	// Change messages.
	
	SUI_WM_SIZE_CHANGED,
	SUI_WM_PARENT_SIZE_CHANGED,
	SUI_WM_ADDED_TO_PARENT,
	SUI_WM_CHILD_ADDED,
	SUI_WM_CHILD_REMOVED,
	
	// Draw messages.
	
	SUI_WM_DRAW,
	SUI_WM_INVALIDATE_RECT,
	SUI_WM_GET_TEXT_SIZE,
	
	// Mouse messages.
	
	SUI_WM_MOUSE_MOVE,
	SUI_WM_MOUSE_WHEEL,
	SUI_WM_MOUSE_DOWN,
	SUI_WM_MOUSE_DOUBLECLICK,
	SUI_WM_MOUSE_UP,
	SUI_WM_MOUSE_LEAVE,
	
	// Keyboard messages.
	
	SUI_WM_KEY_DOWN,
	SUI_WM_KEY_UP,
	
	// Done.
	
	SUI_WM_COUNT,
} SUIWindowMsgType;

#define SUI_MSG_GROUP_FUNCTION_NAME(prefix) prefix##GetMsgGroupID
#define SUI_MSG_GROUP(prefix) (SUI_MSG_GROUP_FUNCTION_NAME(prefix)())
#define SUI_MSG_GROUP_FUNCTION_DEFINE(prefix, name)					\
	U32 SUI_MSG_GROUP_FUNCTION_NAME(prefix)(void){					\
		static U32 msgGroupID;										\
		ATOMIC_INIT_BEGIN;											\
		{															\
			suiCustomMsgGroupCreate(&msgGroupID, name);				\
		}															\
		ATOMIC_INIT_END;											\
		return msgGroupID;											\
	}

#define SUI_WM_GROUP_HANDLERS_BEGIN(w, msg, userPointerType, userPointerVar){					\
			SUIWindow*			handler_w = w;													\
			S32					returnVal = 0;													\
			userPointerType*	handler_userPointer = userPointerVar;							\
			switch(msg->msgType){void ladjflajdflajdfjdj(void)

#define SUI_WM_GROUP_HANDLERS_END																\
			}return returnVal;}

#define SUI_WM_GROUP_BEGIN(w, msg, getGroupID, userPointerType, userPointerVar){				\
			static U32 storedMsgGroupID;														\
			ATOMIC_INIT_BEGIN;																	\
			{																					\
				U32 SUI_MSG_GROUP_FUNCTION_NAME(getGroupID)(void);								\
				storedMsgGroupID = SUI_MSG_GROUP(getGroupID);									\
			}																					\
			ATOMIC_INIT_END;																	\
			if(msg->msgGroupID == storedMsgGroupID){void ladjflajdflajdfjdj(void)

#define SUI_WM_GROUP_END																		\
			}}((void)0)

#define SUI_WM_HANDLERS_BEGIN_MAIN(w, msg, storedMsgGroupID, userPointerType, userPointerVar)	\
			if(msg->msgGroupID == storedMsgGroupID)											\
				SUI_WM_GROUP_HANDLERS_BEGIN(w, msg, userPointerType, userPointerVar)

#define SUI_WM_HANDLERS_BEGIN(w, msg, getGroupID, userPointerType, userPointerVar){				\
			static U32 storedMsgGroupID;														\
			ATOMIC_INIT_BEGIN;																	\
			{																					\
				U32 SUI_MSG_GROUP_FUNCTION_NAME(getGroupID)(void);								\
				storedMsgGroupID = SUI_MSG_GROUP(getGroupID);									\
			}																					\
			ATOMIC_INIT_END;																	\
			SUI_WM_HANDLERS_BEGIN_MAIN(	w,														\
										msg,													\
										storedMsgGroupID,										\
										userPointerType,										\
										userPointerVar)
			
#define SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, userPointerType, userPointerVar){					\
			SUI_WM_HANDLERS_BEGIN_MAIN(w, msg, 0, userPointerType, userPointerVar)

#define SUI_WM_CASE(type)	xcase (type):

#define SUI_WM_HANDLER(type, handler)															\
				SUI_WM_CASE(type) returnVal = handler(handler_w, handler_userPointer, msg)
			
#define SUI_WM_HANDLERS_END																		\
			}																					\
			return returnVal;}}((void)0)

typedef enum SUIMouseButton {
	SUI_MBUTTON_LEFT	= 1 << 0,
	SUI_MBUTTON_MIDDLE	= 1 << 1,
	SUI_MBUTTON_RIGHT	= 1 << 2,
} SUIMouseButton;

typedef struct SUIWindowMsg {
	struct {
		SUIWindow*						wWriter;
		void*							userPointer;
	} pipe;
	
	U32									msgGroupID;
	U32									msgType;
	
	const void*							msgData;
} SUIWindowMsg;

typedef struct SUIWindowMsgInvalidateRect {
	S32									pos[2];
	S32									size[2];
} SUIWindowMsgInvalidateRect;

typedef struct SUIWindowMsgGetTextSizeOut {
	S32									size[2];
	
	struct {
		U32								gotSize : 1;
	} flags;
} SUIWindowMsgGetTextSizeOut;

typedef struct SUIWindowMsgGetTextSize {
	const char*							text;
	U32									height;
	SUIWindowMsgGetTextSizeOut*			out;
} SUIWindowMsgGetTextSize;

typedef struct SUIWindowMsgMouseButton {
	S32									x;
	S32									y;
	U32									button;
	U32									inRect : 1;
} SUIWindowMsgMouseButton;

typedef struct SUIWindowMsgMouseMove {
	S32									x;
	S32									y;
	U32									button;
	U32									inRect : 1;
} SUIWindowMsgMouseMove;

typedef struct SUIWindowMsgMouseWheel {
	S32									x;
	S32									y;
	S32									clickThousandths;
	U32									button;
	U32									inRect : 1;
} SUIWindowMsgMouseWheel;

typedef struct SUIWindowMsgMouseLeave {
	S32									x;
	S32									y;
} SUIWindowMsgMouseLeave;

typedef struct SUIWindowMsgKey {
	SUIKey								key;
	U32									character;
	U32									modBits;
	
	struct {
		U32								isOwned : 1;
	} flags;
} SUIWindowMsgKey;

typedef S32 (*SUIWindowMsgHandler)(	SUIWindow* w,
									void* userPointer,
									const SUIWindowMsg* msg);

typedef struct SUIDrawContextPrivate SUIDrawContextPrivate;

// Debug.

void	suiEnterDebugCS(void);
void	suiLeaveDebugCS(void);

void	suiCountInc_dbg(const char* name, S32* countInOut);
#define suiCountInc(x) suiCountInc_dbg(#x, &x)
void	suiCountDec_dbg(const char* name, S32* countInOut);
#define suiCountDec(x) suiCountDec_dbg(#x, &x)

// Create/Destroy.

S32		suiWindowCreate(SUIWindow** wOut,
						SUIWindow* wParent,
						SUIWindowMsgHandler msgHandler,
						const void* createParams);

S32		suiWindowDestroy(SUIWindow** wInOut);

// Custom messages.

S32		suiCustomMsgGroupCreate(U32* idOut,
								const char* name);

// Pipes.

S32		suiWindowPipeCreate(SUIWindowPipe** wpOut,
							SUIWindow* wReader,
							SUIWindow* wWriter);

S32		suiWindowPipeDestroy(	SUIWindowPipe** wpInOut,
								SUIWindow* wWriter);

S32		suiWindowPipeMsgSend(	SUIWindowPipe* wp,
								SUIWindow* wWriter,
								void* userPointer,
								U32 msgGroupID,
								U32 msgType,
								const void* msgData);

// Parent/Child.

void	suiWindowRemoveChild(SUIWindow* wChild);
S32		suiWindowAddChild(SUIWindow* wParent, SUIWindow* wChild);
S32		suiWindowIsChild(SUIWindow* wParent, SUIWindow* wChild);

// Window position/dimensions.

S32		suiWindowSetPos(SUIWindow* w, S32 x, S32 y);
S32		suiWindowSetPosX(SUIWindow* w, S32 x);
S32		suiWindowSetPosY(SUIWindow* w, S32 y);

S32		suiWindowSetSize(	SUIWindow* w,
							S32 sx,
							S32 sy);
							
S32		suiWindowSetSizeX(	SUIWindow* w,
							S32 sx);
							
S32		suiWindowSetSizeY(	SUIWindow* w,
							S32 sy);

S32		suiWindowSetPosAndSize(	SUIWindow* w,
								S32 x,
								S32 y,
								S32 sx,
								S32 sy);

S32		suiWindowGetPosX(const SUIWindow* w);

S32		suiWindowGetPosY(const SUIWindow* w);

S32		suiWindowGetPos(const SUIWindow* w,
						S32* xOut,
						S32* yOut);

S32		suiWindowGetSizeX(const SUIWindow* w);

S32		suiWindowGetSizeY(const SUIWindow* w);

S32		suiWindowGetSize(	const SUIWindow* w,
							S32* sxOut,
							S32* syOut);

S32		suiWindowParentGetSize(	const SUIWindow* w,
								S32* sxOut,
								S32* syOut);

// Mouse.

S32		suiWindowGetMouseButtonsHeld(const SUIWindow* w);
S32		suiWindowGetMouseHeldOnSelf(const SUIWindow* w);
S32		suiWindowIsMouseOverSelf(const SUIWindow* w);
S32		suiWindowIsMouseOverChild(const SUIWindow* w);
S32		suiWindowGetMousePos(const SUIWindow* w, S32 posOut[2]);

// Keyboard.

S32		suiWindowSetKeyboardExclusive(	SUIWindow* w,
										S32 enabled);

// Processing.

S32		suiWindowIsProcessing(const SUIWindow* w);

S32		suiWindowProcessingHandleCreate(SUIWindow* w,
										SUIWindowProcessingHandle** phOut);

S32		suiWindowProcessingHandleDestroy(	SUIWindow* w,
											SUIWindowProcessingHandle** phInOut);
											
// User pointers.

void	suiWindowSetUserPointer(SUIWindow* w,
								SUIWindowMsgHandler msgHandler,
								void* userPointer);
								
S32		suiWindowGetUserPointer(const SUIWindow* w,
								SUIWindowMsgHandler msgHandler,
								void** userPointerOut);

// Clipping.

typedef void (*SUIClipDrawContextCallback)(	const SUIDrawContext* dc,
											void* userPointer);

S32		suiClipDrawContext(	const SUIDrawContext* dc,
							S32 x,
							S32 y,
							S32 sx,
							S32 sy,
							SUIClipDrawContextCallback callback,
							void* userPointer);

void	suiSetClipRect(	const SUIDrawContext* dc,
						S32 x,
						S32 y,
						S32 sx,
						S32 sy);

void	suiClearClipRect(const SUIDrawContext* dc);

// Drawing.

void	suiDrawLine(const SUIDrawContext* dc,
					S32 x0,
					S32 y0,
					S32 x1,
					S32 y1,
					U32 colorARGB);

void	suiDrawFilledRect(	const SUIDrawContext* dc,
							S32 x,
							S32 y,
							S32 sx,
							S32 sy,
							U32 colorARGB);

void	suiDrawRect(const SUIDrawContext* dc,
					S32 x,
					S32 y,
					S32 sx,
					S32 sy,
					S32 borderWidth,
					U32 colorARGB);

void	suiDrawFilledTriangle(	const SUIDrawContext* dc,
								S32 x0,
								S32 y0,
								S32 x1,
								S32 y1,
								S32 x2,
								S32 y2,
								U32 colorARGB);

void	suiPrintText(	const SUIDrawContext* dc,
						S32 x,
						S32 y,
						const char* textData,
						S32 textLen,
						U32 height,
						U32 colorARGB);

// Invalidate.
							
void	suiWindowInvalidate(SUIWindow* w);

// Draw queries.

S32		suiWindowGetTextSize(	SUIWindow* w,
								const char* text,
								U32 height,
								S32* sxOut,
								S32* syOut);

// --- SUIRootWindow Functions --------------------------------------------------------------------

typedef void (*SUIFuncSetClipRect)(	void* userPointer,
									S32 x,
									S32 y,
									S32 sx,
									S32 sy);
									
typedef void (*SUIFuncDrawLine)(void* userPointer,
								S32 x0,
								S32 y0,
								S32 x1,
								S32 y1,
								U32 colorARGB);
								
typedef void (*SUIFuncDrawFilledRect)(	void* userPointer,
										S32 x,
										S32 y,
										S32 sx,
										S32 sy,
										U32 colorARGB);

typedef void (*SUIFuncDrawRect)(void* userPointer,
								S32 x,
								S32 y,
								S32 sx,
								S32 sy,
								S32 borderWidth,
								U32 colorARGB);

typedef void (*SUIFuncDrawFilledTriangle)(	void* userPointer,
											S32 x0,
											S32 y0,
											S32 x1, 
											S32 y1,
											S32 x2, 
											S32 y2,
											U32 colorARGB);

typedef void (*SUIFuncPrintText)(	void* userPointer,
									S32 x,
									S32 y,
									const char* textData,
									S32 textLen,
									U32 height,
									U32 colorARGB);

typedef struct SUIRootWindowDrawInfo {
	// All fields are defined by the owner of the root window.
	
	void*							userPointer;
	
	struct {
		struct {
			S32						x;
			S32						y;
			S32						x2;
			S32						y2;
			S32						sx;
			S32						sy;
		} set, queued;
		
		S32							isSet;
	} clipRect;
	
	struct {
		SUIFuncSetClipRect			setClipRect;
		SUIFuncDrawLine				drawLine;
		SUIFuncDrawFilledRect		drawFilledRect;
		SUIFuncDrawRect				drawRect;
		SUIFuncDrawFilledTriangle	drawFilledTriangle;
		SUIFuncPrintText			printText;
	} funcs;
} SUIRootWindowDrawInfo;

typedef struct SUIRootWindow SUIRootWindow;

S32		suiRootWindowCreate(SUIRootWindow** rwOut,
							SUIWindowMsgHandler msgHandler,
							const void* createParams);

S32		suiRootWindowDestroy(SUIRootWindow** rwInOut);

S32		suiRootWindowNeedsProcess(SUIRootWindow* rw);

void	suiRootWindowProcess(SUIRootWindow* rw);

void	suiRootWindowDraw(	SUIRootWindow* rw,
							SUIRootWindowDrawInfo* info,
							S32 origin_x,
							S32 origin_y,
							S32 x,
							S32 y,
							S32 sx,
							S32 sy,
							S32 drawBorderAroundUpdateArea);
							
void 	suiRootWindowMouseMove(	SUIRootWindow* rw,
								S32 x,
								S32 y);

void	suiRootWindowMouseWheel(SUIRootWindow* rw,
								S32 x,
								S32 y,
								S32 clickThousandths);

void 	suiRootWindowMouseDown(	SUIRootWindow* rw,
								S32 x,
								S32 y,
								U32 button);

void 	suiRootWindowMouseDoubleClick(	SUIRootWindow* rw,
										S32 x,
										S32 y,
										U32 button);

void 	suiRootWindowMouseUp(	SUIRootWindow* rw,
								S32 x,
								S32 y,
								U32 button);

void 	suiRootWindowMouseLeave(SUIRootWindow* rw);

S32		suiRootWindowKey(	SUIRootWindow* rw,
							SUIKey key,
							U32 character,
							S32 isDown,
							U32 modBits);

void	suiRootWindowLoseFocus(SUIRootWindow* rw);

void	suiRootWindowSetSize(	SUIRootWindow* rw,
								S32 sx,
								S32 sy);

S32 	suiRootWindowAddChild(	SUIRootWindow* rw,
								SUIWindow* w);

// --- SUI Helper Functions --------------------------------------------------------------------

U32		suiColorInterpAllRGB(U32 a, U32 rgbFrom, U32 rgbTo, U32 scale);
U32		suiColorInterpSeparateRGB(U32 a, U32 rgbFrom, U32 rgbTo, U32 scaleR, U32 scaleG, U32 scaleB);
U32		suiColorSetA(U32 argb, U32 newA);
U32		suiColorSetR(U32 argb, U32 newR);
U32		suiColorSetG(U32 argb, U32 newG);
U32		suiColorSetB(U32 argb, U32 newB);
