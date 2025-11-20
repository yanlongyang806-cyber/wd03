
#pragma once

#include "stdtypes.h"

typedef struct SUIWindow			SUIWindow;
typedef struct SUIListEntry			SUIListEntry;
typedef struct SUIListEntryClass	SUIListEntryClass;

enum {
	SUI_LIST_MSG_ENTRY_DRAW,
	SUI_LIST_MSG_ENTRY_MOUSE_DOWN,
	SUI_LIST_MSG_ENTRY_MOUSE_UP,
	SUI_LIST_MSG_ENTRY_MOUSE_ENTER,
	SUI_LIST_MSG_ENTRY_MOUSE_LEAVE,
	SUI_LIST_MSG_ENTRY_MOUSE_MOVE,
	SUI_LIST_MSG_ENTRY_DESTROYED,
};

typedef struct SUIListMsgEntry {
	SUIListEntry*			le;
	void*					userPointer;
} SUIListMsgEntry;

typedef struct SUIListMsgEntryDraw {
	SUIListMsgEntry			le;
	
	const SUIDrawContext*	dc;

	U32						sx;
	U32						sy;
	
	U32						xIndent;
	
	U32						argbDefault;
	
	struct {
		U32					isUnderMouse : 1;
	} flags;
} SUIListMsgEntryDraw;

typedef struct SUIListMsgEntryMouse {
	SUIListMsgEntry			le;
	
	U32						button;
	
	S32						x;
	S32						y;

	S32						xIndent;

	S32						sx;
	S32						sy;
} SUIListMsgEntryMouse;

typedef struct SUIListMsgEntryMouseEnter {
	SUIListMsgEntry			le;
} SUIListMsgEntryMouseEnter;

typedef struct SUIListMsgEntryMouseLeave {
	SUIListMsgEntry			le;
	
	struct {
		U32					enteredEmptySpace : 1;
	} flags;
} SUIListMsgEntryMouseLeave;

typedef struct SUIListMsgEntryDestroyed {
	SUIListMsgEntry			le;
} SUIListMsgEntryDestroyed;

typedef struct SUIListCreateParams {
	void*					userPointer;
	SUIWindow*				wReader;
} SUIListCreateParams;

S32 suiListCreate(	SUIWindow** wOut,
					SUIWindow* wParent,
					const SUIListCreateParams* cp);

S32 suiListCreateBasic(	SUIWindow** wOut,
						SUIWindow* wParent,
						void* userPointer,
						SUIWindow* wReader);
						
S32 suiListSetPosY(	SUIWindow* w,
					S32 y);

S32 suiListSetXIndentPerDepth(	SUIWindow* w,
								S32 xIndent);

S32 suiListGetXIndentPerDepth(	SUIWindow* w,
								S32* xIndentOut);
								
S32 suiListEntryClassCreate(SUIListEntryClass** lecOut,
							SUIWindow* w,
							SUIWindow* wReader);

S32 suiListEntryCreate(	SUIListEntry** leOut,
						SUIListEntry* leParent,
						SUIListEntryClass* lec,
						void* leUserPointer);
						
S32 suiListEntryDestroy(SUIListEntry** leInOut);

S32 suiListEntrySetUserPointer(	SUIListEntry* le,
								void* userPointer);

S32 suiListEntryGetPosY(SUIListEntry* le,
						S32* yOut);
						
S32 suiListEntryGetOpenState(	SUIListEntry* le,
								S32* openOut);

S32 suiListEntrySetOpenState(	SUIListEntry* le,
								S32 open);

S32 suiListEntryGetHiddenState(	SUIListEntry* le,
								S32* hiddenOut);

S32 suiListEntrySetHiddenState(	SUIListEntry* le,
								S32 hidden);

S32 suiListEntryGetHeight(	SUIListEntry* le,
							U32* heightOut);
							
S32 suiListEntrySetHeight(	SUIListEntry* le,
							U32 height);

