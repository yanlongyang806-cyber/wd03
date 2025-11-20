
#include "SimpleUI.h"
#include "wininclude.h"
#include "stdtypes.h"
#include "mathutil.h"
#include "assert.h"
#include "MemoryPool.h"
#include "winutil.h"
#include "timing.h"
#include "file.h"
#include "utils.h"
#include "earray.h"
#include "StashTable.h"

//#define DRAW_CRAP_IF_NO_DRAW 1

// Private structures.

typedef struct SUIRootWindow {
	SUIWindow*					w;
	U32							downButtons;
} SUIRootWindow;

typedef struct SUIDrawContext {
	SUIRootWindowDrawInfo*		info;
	S32							origin[2];
	S32							rect[2][2];
	S32							size[2];
} SUIDrawContext;

typedef struct SUIWindowMouseStatus {
	S32							pos[2];
	U32							heldButtons;
	SUIWindow*					mouseOwner;
	SUIWindow*					underMouse;
	U32							mouseOwnerIsChild : 1;
} SUIWindowMouseStatus;

typedef struct SUIWindowIter SUIWindowIter;

typedef struct SUIWindowPipe {
	SUIWindow*					wReader;
	SUIWindow*					wWriter;
} SUIWindowPipe;

typedef struct SUIWindowProcessingHandle {
	SUIWindow*					w;
} SUIWindowProcessingHandle;

typedef struct SUIWindow {
	SUIRootWindow*				rootWindow;
	SUIWindowIter*				iterators;
	SUIWindow*					parent;
	void*						userPointer;
	SUIWindowMsgHandler			msgHandler;
	
	S32							pos[2];
	S32							size[2];
	
	SUIWindowMouseStatus*		mouseStatus;
	SUIWindow*					keyboardOwner;
	
	SUIWindowProcessingHandle**	processingHandles;
	U32							processingChildCount;
	
	struct {
		SUIWindowPipe**			reading;
		SUIWindowPipe**			writing;
	} pipe;
	
	struct {
		SUIWindow*				head;
		SUIWindow*				tail;
	} child;
	
	struct {
		SUIWindow*				next;
		SUIWindow*				prev;
	} sibling;
	
	struct {
		U32						destroyed						: 1;
		U32						destroying						: 1;
		U32						sentDestroyMsg					: 1;
		U32						destroyedProcessingHandles		: 1;

		U32						hidden							: 1;
	} flags;
	
	struct {
		U32						suiInternal;
	} refCount;
} SUIWindow;

MP_DEFINE(SUIWindow);

typedef struct SUIWindowIter {
	SUIWindowIter*				next;
	SUIWindowIter*				prev;
	
	SUIWindow*					parent;
	SUIWindow*					curChild;
	
	SUIWindow*					deletedChild;
} SUIWindowIter;

typedef struct SUIMsgGroup {
	U32							id;
	char*						name;
} SUIMsgGroup;

typedef struct SUIState {
	CRITICAL_SECTION			csSUIWindows;
	CRITICAL_SECTION			csDebug;
	
	SUIMsgGroup**				msgGroups;
} SUIState;

SUIState suiState;

// ---------- SUIWindow Functions ------------------------------------------------------------------

static void suiWindowInternalRefInc(SUIWindow* w);
static void suiWindowInternalRefDec(SUIWindow* w);
static void suiWindowProcessingHandleDestroyInternal(	SUIWindow* w,
														SUIWindowProcessingHandle* ph);
														
// Debug cs.

void suiEnterDebugCS(void){
	ATOMIC_INIT_BEGIN;
		InitializeCriticalSection(&suiState.csDebug);
	ATOMIC_INIT_END;
	
	EnterCriticalSection(&suiState.csDebug);
}

void suiLeaveDebugCS(void){
	LeaveCriticalSection(&suiState.csDebug);
}

//#define SUI_PRINT_COUNTS 1

void suiCountInc_dbg(	const char* name,
						S32* countInOut)
{
	suiEnterDebugCS();
		(*countInOut)++;
		#if SUI_PRINT_COUNTS
			printf("%s++: %d\n", name, *countInOut);
		#endif
	suiLeaveDebugCS();
}

void suiCountDec_dbg(	const char* name,
						S32* countInOut)
{
	suiEnterDebugCS();
		assert(*countInOut);
		(*countInOut)--;
		#if SUI_PRINT_COUNTS
			printf("%s--: %d\n", name, *countInOut);
		#endif
	suiLeaveDebugCS();
}

static void suiEnterCS(void){
	ATOMIC_INIT_BEGIN;
		InitializeCriticalSection(&suiState.csSUIWindows);
	ATOMIC_INIT_END;
	
	EnterCriticalSection(&suiState.csSUIWindows);
}

static void suiLeaveCS(void){
	LeaveCriticalSection(&suiState.csSUIWindows);
}

static void suiWindowIterInit(	SUIWindowIter* it,
								SUIWindow* w,
								S32 startAtTail)
{
	if(	!it ||
		!w)
	{
		return;
	}
	
	it->parent = w;
	it->curChild = startAtTail ? w->child.tail : w->child.head;
	it->deletedChild = NULL;
	
	suiWindowInternalRefInc(it->parent);
	suiWindowInternalRefInc(it->curChild);
	
	assert(w->iterators != it);
	
	it->prev = NULL;
	it->next = w->iterators;

	if(w->iterators){
		w->iterators->prev = it;
	}
	
	w->iterators = it;
}

static void suiWindowIterReleaseDeletedChild(SUIWindowIter* it){
	if(it->deletedChild){
		suiWindowInternalRefDec(it->deletedChild);
		it->deletedChild = NULL;
	}
}

static void suiWindowIterDeInit(SUIWindowIter* it){
	if(!it){
		return;
	}
	
	suiWindowIterReleaseDeletedChild(it);

	if(it->next){
		assert(it->next->parent == it->parent);

		it->next->prev = it->prev;
	}
	
	if(it->prev){
		it->prev->next = it->next;
		
		assert(it->prev->next != it->prev);
	}else{
		assert(it->parent->iterators == it);
		
		it->parent->iterators = it->next;
	}
	
	suiWindowInternalRefDec(it->parent);
	suiWindowInternalRefDec(it->curChild);
	
	assert(	!it->parent->iterators ||
			it->parent->iterators->parent == it->parent);
			
	{
		SUIWindowIter* i;
		
		for(i = it->parent->iterators; i; i = i->next){
			assert(i != it);
		}
	}
}

static void suiWindowIterCreate(SUIWindowIter** itOut,
								SUIWindow* w,
								S32 startAtTail)
{
	SUIWindowIter* it;
	
	if(	!itOut ||
		!w)
	{
		return;
	}
	
	it = callocStruct(SUIWindowIter);
	
	suiWindowIterInit(it, w, startAtTail);
	
	*itOut = it;
}

static void suiWindowIterDestroy(SUIWindowIter** itInOut){
	SUIWindowIter* it = SAFE_DEREF(itInOut);
	
	if(!it){
		return;
	}
	
	suiWindowIterDeInit(it);
	
	SAFE_FREE(*itInOut);
}

static S32 suiWindowIterGetCur(	SUIWindow** child,
								SUIWindowIter* it)
{
	if(	!child ||
		!it)
	{
		return 0;
	}
	
	if(it->deletedChild){
		*child = NULL;
		
		return 1;
	}

	*child = it->curChild;
	
	return !!*child;
}

static void suiWindowIterGotoNext(SUIWindowIter* it){
	SUIWindow* oldChild;
	
	if(!it){
		return;
	}
	
	oldChild = it->curChild;
	
	it->curChild = oldChild ? oldChild->sibling.next : it->parent->child.head;

	suiWindowIterReleaseDeletedChild(it);
			
	suiWindowInternalRefInc(it->curChild);
	suiWindowInternalRefDec(oldChild);
}

static void suiWindowIterGotoPrev(SUIWindowIter* it){
	SUIWindow* oldChild;

	if(!it){
		return;
	}
	
	oldChild = it->curChild;
	
	if(	!it->deletedChild &&
		oldChild)
	{
		it->curChild = oldChild->sibling.prev;
	}

	suiWindowIterReleaseDeletedChild(it);

	suiWindowInternalRefInc(it->curChild);
	suiWindowInternalRefDec(oldChild);
}

static void suiWindowIterHandleRemove(	SUIWindow* parent,
										SUIWindow* removedChild)
{
	SUIWindowIter* it;
	
	if(	!parent ||
		!removedChild)
	{
		return;
	}
	
	for(it = parent->iterators; it; it = it->next){
		assert(it->parent == parent);
		
		if(it->curChild == removedChild){
			SUIWindow* prev = it->curChild->sibling.prev;
			
			if(it->deletedChild){
				assert(it->deletedChild != removedChild);
				
				suiWindowInternalRefDec(it->curChild);
			}else{
				it->deletedChild = it->curChild;
			}

			it->curChild = prev;

			suiWindowInternalRefInc(it->curChild);
		}
	}
}

static S32 suiWindowMsgSendDirect(	SUIWindow* w,
									SUIWindowMsg* msg)
{
	if(	SAFE_MEMBER(w, msgHandler) &&
		msg)
	{
		S32 ret;
		
		suiWindowInternalRefInc(w);
		
		ret = w->msgHandler(w, w->userPointer, msg);
		
		suiWindowInternalRefDec(w);
		
		return ret;
	}
	
	return 0;
}

static S32 suiWindowMsgSend(SUIWindow* w,
							SUIWindowMsgType msgType,
							const void* msgData)
{
	SUIWindowMsg msg = {0};
	
	msg.msgType = msgType;
	msg.msgData = msgData;
	
	return suiWindowMsgSendDirect(w, &msg);
}

S32 suiWindowCreate(SUIWindow** wOut, 
					SUIWindow* wParent,
					SUIWindowMsgHandler msgHandler,
					const void* createParams)
{
	SUIWindow* w;
	
	if(!wOut){
		return 0;
	}
	
	suiEnterCS();
	{	
		MP_CREATE(SUIWindow, 100);
		
		w = *wOut = MP_ALLOC(SUIWindow);
	}
	suiLeaveCS();
	
	//printfColor(COLOR_BRIGHT|COLOR_GREEN, "Created:  0x%8.8x\n", w);
	
	w->msgHandler = msgHandler;
	
	suiWindowInternalRefInc(w);
	suiWindowInternalRefInc(w);

	suiWindowMsgSend(	w,
						SUI_WM_CREATE,
						createParams);

	suiWindowAddChild(wParent, w);

	suiWindowInternalRefDec(w);

	return 1;
}

MP_DEFINE(SUIWindowMouseStatus);

static SUIWindowMouseStatus* suiWindowGetMouseStatus(SUIWindow* w){
	if(!w->mouseStatus){
		suiEnterCS();
		{	
			MP_CREATE(SUIWindowMouseStatus, 10);
		
			w->mouseStatus = MP_ALLOC(SUIWindowMouseStatus);
		}
		suiLeaveCS();
	}
	
	return w->mouseStatus;
}

static void suiWindowDestroyInternal(SUIWindow* w){
	SUIWindow* wChild;
	
	if(!FALSE_THEN_SET(w->flags.destroying)){
		return;
	}
	
	ASSERT_FALSE_AND_SET(w->flags.sentDestroyMsg);

	suiWindowMsgSend(w, SUI_WM_DESTROY, NULL);

	suiWindowRemoveChild(w);
	
	for(wChild = w->child.head; wChild; wChild = w->child.head){
		suiWindowRemoveChild(wChild);
		suiWindowDestroy(&wChild);
	}
	
	EARRAY_CONST_FOREACH_BEGIN(w->pipe.reading, i, isize);
		SUIWindowPipe* wp = w->pipe.reading[i];
		
		assert(wp->wReader == w);
		wp->wReader = NULL;
		w->pipe.reading[i] = NULL;
	EARRAY_FOREACH_END;
	
	eaDestroy(&w->pipe.reading);
		
	ASSERT_FALSE_AND_SET(w->flags.destroyedProcessingHandles);
		
	while(eaSize(&w->processingHandles)){
		SUIWindowProcessingHandle* ph = w->processingHandles[0];
		
		suiWindowProcessingHandleDestroyInternal(w, ph);
	}
	
	while(eaSize(&w->pipe.writing)){
		SUIWindowPipe* wp = w->pipe.writing[0];
		
		suiWindowPipeDestroy(&wp, w);
	}
	
	//printfColor(COLOR_RED|COLOR_BRIGHT, "Destroy: 0x%8.8x\n", w);
	
	assert(!w->iterators);
	assert(!w->parent);
	assert(!w->child.head && !w->child.tail);
	assert(!w->sibling.next && !w->sibling.prev);
	assert(!w->refCount.suiInternal);

	suiEnterCS();
	{
		//printf("Freeing window 0x%8.8p:0x%8.8p\n", w, w->userPointer);
		MP_FREE(SUIWindow, w);
	}
	suiLeaveCS();
}

static void suiWindowInternalRefInc(SUIWindow* w){
	if(w){
		w->refCount.suiInternal++;
	}
}

static void suiWindowInternalRefDec(SUIWindow* w){
	if(w){
		assert(w->refCount.suiInternal);
		
		if(!--w->refCount.suiInternal){
			suiWindowDestroyInternal(w);
		}
	}
}

static void suiWindowSetRootRecurse(SUIWindow* w,
									SUIRootWindow* rw)
{
	SUIWindowIter	it;
	SUIWindow*		wChild;
	
	w->rootWindow = rw;

	for(suiWindowIterInit(&it, w, 0);
		suiWindowIterGetCur(&wChild, &it);
		suiWindowIterGotoNext(&it))
	{
		suiWindowSetRootRecurse(wChild, rw);
	}
	
	suiWindowIterDeInit(&it);
}

void suiWindowRemoveChild(SUIWindow* w){
	SUIWindow* wParent = SAFE_MEMBER(w, parent);
	
	if(!w){
		return;
	}
	
	if(!wParent){
		assert(	!w->sibling.next &&
				!w->sibling.prev);
				
		return;
	}
	
	// Invalidate me.
	
	suiWindowInvalidate(w);
	
	// Remove from current iterators.
	
	suiWindowIterHandleRemove(wParent, w);
	
	// Remove from the sibling list.
	
	if(w->sibling.prev){
		assert(w->sibling.prev->sibling.next == w);
		
		w->sibling.prev->sibling.next = w->sibling.next;
	}else{
		assert(wParent->child.head == w);
		
		wParent->child.head = w->sibling.next;
	}
	
	if(w->sibling.next){
		assert(w->sibling.next->sibling.prev == w);
		
		w->sibling.next->sibling.prev = w->sibling.prev;
	}else{
		assert(wParent->child.tail == w);
		
		wParent->child.tail = w->sibling.prev;
	}
	
	ZeroStruct(&w->sibling);

	// Remove processing.
	
	if(	eaSize(&w->processingHandles) ||
		w->processingChildCount)
	{
		const U32	count = !!eaSize(&w->processingHandles) +
							w->processingChildCount;
		SUIWindow*	wTemp;
		
		for(wTemp = wParent; wTemp; wTemp = wTemp->parent){
			assert(wTemp->processingChildCount >= count);
			wTemp->processingChildCount -= count;
		}
	}

	// Reset parent pointer.
	
	w->parent = NULL;
	
	// Remove root from all children.
	
	suiWindowSetRootRecurse(w, NULL);
	
	if(wParent){
		// Remove mouse ownership status.
		
		if(SAFE_MEMBER(wParent->mouseStatus, mouseOwner) == w){
			wParent->mouseStatus->mouseOwner = NULL;
		}
		
		if(SAFE_MEMBER(wParent->mouseStatus, underMouse) == w){
			wParent->mouseStatus->underMouse = NULL;
		}
		
		// Remove keyboard ownership status.
		
		if(wParent->keyboardOwner == w){
			wParent->keyboardOwner = NULL;
		}
	}
	
	// Tell parent.
	
	suiWindowMsgSend(wParent, SUI_WM_CHILD_REMOVED, w);
		
	// Decrement internal ref count.

	suiWindowInternalRefDec(w);
}

S32 suiWindowAddChild(	SUIWindow* w,
						SUIWindow* wChild)
{
	if(	!w ||
		!wChild ||
		wChild->parent)
	{
		return 0;
	}
	
	if(w->child.head){
		assert(w->child.tail);
		w->child.tail->sibling.next = wChild;
		wChild->sibling.prev = w->child.tail;
		w->child.tail = wChild;
		assert(!wChild->sibling.next);
	}else{
		assert(!w->child.tail);
		w->child.head = w->child.tail = wChild;
		assert(!wChild->sibling.prev && !wChild->sibling.next);
	}
	
	wChild->parent = w;
	
	suiWindowSetRootRecurse(wChild, w->rootWindow);
	
	suiWindowInternalRefInc(wChild);
	
	if(	eaSize(&wChild->processingHandles) ||
		wChild->processingChildCount)
	{
		const S32	count = !!eaSize(&wChild->processingHandles) +
							wChild->processingChildCount;
		SUIWindow*	wTemp;
		
		for(wTemp = w; wTemp; wTemp = wTemp->parent){
			wTemp->processingChildCount += count;
		}
	}
	
	suiWindowInternalRefInc(w);
	suiWindowInternalRefInc(wChild);
	
	suiWindowMsgSend(w, SUI_WM_CHILD_ADDED, NULL);
	suiWindowMsgSend(wChild, SUI_WM_ADDED_TO_PARENT, NULL);
	
	suiWindowInternalRefDec(wChild);
	suiWindowInternalRefDec(w);

	suiWindowInvalidate(wChild);
	
	return 1;
}

S32 suiWindowIsChild(SUIWindow* wParent, SUIWindow* wChild){
	return	wParent &&
			wChild &&
			wChild->parent == wParent;
}

S32 suiWindowDestroy(SUIWindow** wInOut){
	SUIWindow* w = SAFE_DEREF(wInOut);
	
	if(!w){
		return 0;
	}
	
	if(!w->flags.destroyed){
		w->flags.destroyed = 1;
		
		suiWindowRemoveChild(w);

		suiWindowInternalRefDec(w);
	}
	
	*wInOut = NULL;
	
	return 1;
}

S32 suiCustomMsgGroupCreate(U32* idOut,
							const char* name)
{
	SUIMsgGroup* mg;
	
	if(	!idOut ||
		!name)
	{
		return 0;
	}
	
	mg = callocStruct(SUIMsgGroup);
	
	mg->name = strdup(name);
	
	suiEnterCS();
	{
		mg->id = eaPush(&suiState.msgGroups, mg) + 1;
	}
	suiLeaveCS();
	
	*idOut = mg->id;
	
	return 1;
}

static S32 pipeCount;

S32 suiWindowPipeCreate(SUIWindowPipe** wpOut,
						SUIWindow* wReader,
						SUIWindow* wWriter)
{
	SUIWindowPipe* wp;
	
	if(	!wpOut ||
		!wWriter ||
		wWriter->flags.destroying ||
		!wReader ||
		wReader->flags.destroying)
	{
		return 0;
	}
	
	wp = callocStruct(SUIWindowPipe);
	
	wp->wReader = wReader;
	wp->wWriter = wWriter;
	
	eaPush(	&wWriter->pipe.writing,
			wp);
	
	eaPush(	&wReader->pipe.reading,
			wp);
	
	*wpOut = wp;
	
	suiCountInc(pipeCount);
	
	return 1;
}

S32 suiWindowPipeDestroy(	SUIWindowPipe** wpInOut,
							SUIWindow* wWriter)
{
	SUIWindowPipe*	wp = SAFE_DEREF(wpInOut);
	S32				index;
	
	if(	!wp ||
		wp->wWriter != wWriter)
	{
		return 0;
	}

	// Remove from the writer.

	index = eaFindAndRemove(&wWriter->pipe.writing, wp);
	
	if(index < 0){
		return 0;
	}
	
	if(	!index &&
		!eaSize(&wWriter->pipe.writing))
	{
		eaDestroy(&wWriter->pipe.writing);
	}
	
	// Remove from the reader.

	if(wp->wReader){
		index = eaFindAndRemove(&wp->wReader->pipe.reading, wp);
		
		assert(index >= 0);

		if(	!index &&
			!eaSize(&wWriter->pipe.reading))
		{
			eaDestroy(&wWriter->pipe.reading);
		}
	}
	
	// Destroy it.
	
	SAFE_FREE(*wpInOut);

	suiCountDec(pipeCount);

	return 1;
}

S32 suiWindowPipeMsgSend(	SUIWindowPipe* wp,
							SUIWindow* wWriter,
							void* userPointer,
							U32 msgGroupID,
							U32 msgType,
							const void* msgData)
{
	SUIWindowMsg msg = {0};

	if(	!wWriter ||
		!wp ||
		wp->wWriter != wWriter ||
		!wp->wReader ||
		!msgGroupID)
	{
		return 0;
	}

	msg.pipe.wWriter = wWriter;
	msg.pipe.userPointer = userPointer;
	msg.msgGroupID = msgGroupID;
	msg.msgType = msgType;
	msg.msgData = msgData;
	
	suiWindowMsgSendDirect(wp->wReader, &msg);
	
	return 1;
}

S32	suiWindowSetPos(SUIWindow* w, S32 x, S32 y){
	if(!w){
		return 0;
	}
	
	if(	w->pos[0] != x ||
		w->pos[1] != y)
	{
		suiWindowInvalidate(w);
		
		w->pos[0] = x;
		w->pos[1] = y;

		suiWindowInvalidate(w);
	}
	
	return 1;
}

S32 suiWindowSetPosX(SUIWindow* w, S32 x){
	if(!w){
		return 0;
	}
	
	if(w->pos[0] != x){
		suiWindowInvalidate(w);
		
		w->pos[0] = x;

		suiWindowInvalidate(w);
	}
	
	
	return 1;
}

S32 suiWindowSetPosY(SUIWindow* w, S32 y){
	if(!w){
		return 0;
	}
	
	if(w->pos[1] != y){
		suiWindowInvalidate(w);
		
		w->pos[1] = y;

		suiWindowInvalidate(w);
	}
	
	return 1;
}

static void suiWindowSendSizeChanged(SUIWindow* w){
	SUIWindowIter	it;
	SUIWindow*		wChild;
	
	suiWindowMsgSend(w, SUI_WM_SIZE_CHANGED, NULL);
	
	for(suiWindowIterInit(&it, w, 0);
		suiWindowIterGetCur(&wChild, &it);
		suiWindowIterGotoNext(&it))
	{
		suiWindowMsgSend(wChild, SUI_WM_PARENT_SIZE_CHANGED, NULL);
	}
	
	suiWindowIterDeInit(&it);
}

S32	suiWindowSetSize(SUIWindow* w, S32 sx, S32 sy){
	if(!w){
		return 0;
	}
	
	if(	w->size[0] != sx ||
		w->size[1] != sy)
	{
		suiWindowInvalidate(w);
		
		w->size[0] = sx;
		w->size[1] = sy;

		suiWindowSendSizeChanged(w);
		suiWindowInvalidate(w);
	}

	return 1;
}

S32	suiWindowSetSizeX(SUIWindow* w, S32 sx){
	if(!w){
		return 0;
	}
	
	if(w->size[0] != sx){
		suiWindowInvalidate(w);
		
		w->size[0] = sx;
		
		suiWindowSendSizeChanged(w);
		suiWindowInvalidate(w);
	}

	return 1;
}

S32	suiWindowSetSizeY(SUIWindow* w, S32 sy){
	if(!w){
		return 0;
	}
	
	if(w->size[1] != sy){
		suiWindowInvalidate(w);
		
		w->size[1] = sy;
		
		suiWindowSendSizeChanged(w);
		suiWindowInvalidate(w);
	}

	return 1;
}

S32 suiWindowSetPosAndSize(SUIWindow* w, S32 x, S32 y, S32 sx, S32 sy){
	if(!w){
		return 0;
	}
	
	if(	w->pos[0] != x ||
		w->pos[1] != y ||
		w->size[0] != sx ||
		w->size[1] != sy)
	{
		suiWindowInvalidate(w);
		
		w->pos[0] = x;
		w->pos[1] = y;
		
		if(	w->size[0] != sx ||
			w->size[1] != sy)
		{
			w->size[0] = sx;
			w->size[1] = sy;

			suiWindowSendSizeChanged(w);
		}
		
		suiWindowInvalidate(w);
	}

	return 1;
}

S32 suiWindowGetPosX(const SUIWindow* w){
	if(!w){
		return 0;
	}
	
	return w->pos[0];
}

S32 suiWindowGetPosY(const SUIWindow* w){
	if(!w){
		return 0;
	}
	
	return w->pos[1];
}

S32 suiWindowGetPos(const SUIWindow* w,
					S32* xOut,
					S32* yOut)
{
	if(!w){
		return 0;
	}
	
	if(xOut){
		*xOut = w->pos[0];
	}
	
	if(yOut){
		*yOut = w->pos[1];
	}
	
	return 1;
}

S32	suiWindowGetSizeX(const SUIWindow* w){
	if(!w){
		return 0;
	}
	
	return w->size[0];
}

S32	suiWindowGetSizeY(const SUIWindow* w){
	if(!w){
		return 0;
	}
	
	return w->size[1];
}

S32 suiWindowGetSize(	const SUIWindow* w,
						S32* sxOut,
						S32* syOut)
{
	if(!w){
		return 0;
	}
	
	if(sxOut){
		*sxOut = w->size[0];
	}
	
	if(syOut){
		*syOut = w->size[1];
	}
	
	return 1;
}

S32 suiWindowParentGetSize(	const SUIWindow* w,
							S32* sxOut,
							S32* syOut)
{
	if(!SAFE_MEMBER(w, parent)){
		return 0;
	}
	
	if(sxOut){
		*sxOut = w->parent->size[0];
	}
	
	if(syOut){
		*syOut = w->parent->size[1];
	}
	
	return 1;
}

S32 suiWindowGetMouseButtonsHeld(const SUIWindow* w){
	return SAFE_MEMBER2(w, mouseStatus, heldButtons);
}

S32 suiWindowGetMouseHeldOnSelf(const SUIWindow* w){
	return w == SAFE_MEMBER2(w, mouseStatus, mouseOwner);
}

S32 suiWindowIsMouseOverSelf(const SUIWindow* w){
	return w == SAFE_MEMBER2(w, mouseStatus, underMouse);
}

S32 suiWindowIsMouseOverChild(const SUIWindow* w){
	return	SAFE_MEMBER2(w, mouseStatus, underMouse) &&
			w != w->mouseStatus->underMouse;
}

S32 suiWindowGetMousePos(const SUIWindow* w, S32* posOut){
	if(	posOut &&
		w &&
		w == SAFE_MEMBER2(w, mouseStatus, underMouse))
	{
		copyVec2(w->mouseStatus->pos, posOut);
		
		return 1;
	}
	
	return 0;
}

S32 suiWindowSetKeyboardExclusive(	SUIWindow* w,
									S32 enabled)
{
	if(!w){
		return 0;
	}
	
	if(enabled){
		SUIWindow* wParent;
		SUIWindow* wChild;

		if(w->keyboardOwner == w){
			// Already the keyboard owner.
			
			return 1;
		}
		
		for(wChild = w, wParent = w->parent;
			wParent;
			wChild = wParent, wParent = wParent->parent)
		{
			if(wParent->keyboardOwner){
				SUIWindow* wKeyboardOwner = wParent;
				
				assert(	wKeyboardOwner == wChild ||
						wKeyboardOwner == wParent);
				
				while(wKeyboardOwner){
					SUIWindow* wNext = wKeyboardOwner->keyboardOwner;
					
					wKeyboardOwner->keyboardOwner = NULL;

					if(wNext == wKeyboardOwner){
						// Send a message that ownership has been taken away.
						
						break;
					}

					wKeyboardOwner = wNext;
				}
				
				break;
			}
			
			wParent->keyboardOwner = wChild;
		}
		
		w->keyboardOwner = w;
	}else{
		SUIWindow* wParent;

		if(w->keyboardOwner != w){
			return 1;
		}
		
		for(wParent = w;
			wParent;
			wParent = wParent->parent)
		{
			assert(wParent->keyboardOwner);
			
			wParent->keyboardOwner = NULL;
		}
	}
	
	return 1;
}

S32 suiWindowIsProcessing(const SUIWindow* w){
	return w ? !!eaSize(&w->processingHandles) : 0;
}

S32 suiWindowProcessingHandleCreate(SUIWindow* w,
									SUIWindowProcessingHandle** phOut)
{
	SUIWindowProcessingHandle* ph;
	
	if(	!w ||
		!phOut ||
		*phOut ||
		w->flags.sentDestroyMsg)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	ph = callocStruct(SUIWindowProcessingHandle);
	
	ph->w = w;
	
	if(!eaPush(&w->processingHandles, ph)){
		SUIWindow* wTemp;
		
		for(wTemp = w->parent; wTemp; wTemp = wTemp->parent){
			wTemp->processingChildCount++;
		}
	}
	
	*phOut = ph;
	
	PERFINFO_AUTO_STOP();

	return 1;
}

static void suiWindowProcessingHandleDestroyInternal(	SUIWindow* w,
														SUIWindowProcessingHandle* ph)
{
	assert(ph->w == w);
	
	if(eaFindAndRemoveFast(&w->processingHandles, ph) < 0){
		assert(0);
	}
	
	ph->w = NULL;
	
	if(!eaSize(&w->processingHandles)){
		SUIWindow* wTemp;
		
		eaDestroy(&w->processingHandles);
		
		for(wTemp = w->parent; wTemp; wTemp = wTemp->parent){
			assert(wTemp->processingChildCount);
			wTemp->processingChildCount--;
		}
	}

	SAFE_FREE(ph);
}

S32 suiWindowProcessingHandleDestroy(	SUIWindow* w,
										SUIWindowProcessingHandle** phInOut)
{
	SUIWindowProcessingHandle* ph = SAFE_DEREF(phInOut);
	
	if(	!w ||
		!ph)
	{
		return 0;
	}
	
	assert(!w->flags.destroyedProcessingHandles);

	suiWindowProcessingHandleDestroyInternal(w, ph);

	*phInOut = NULL;

	return 1;
}

void suiWindowSetUserPointer(	SUIWindow* w,
								SUIWindowMsgHandler msgHandler,
								void* userPointer)
{
	if(!w){
		return;
	}
	
	w->userPointer = userPointer;
}

S32 suiWindowGetUserPointer(const SUIWindow* w,
							SUIWindowMsgHandler msgHandler,
							void** userPointerOut)
{
	if( userPointerOut &&
		SAFE_MEMBER(w, msgHandler) == msgHandler)
	{
		*userPointerOut = SAFE_MEMBER(w, userPointer);
		return 1;
	}else{
		return 0;
	}
}

void suiWindowInvalidateRect(SUIWindow* w, S32 x, S32 y, S32 sx, S32 sy){
	RECT rect;
	
	if(!w){
		return;
	}

	rect.left = x;
	rect.top = y;
	rect.right = x + sx;
	rect.bottom = y + sy;
	
	if(rect.left <= 0){
		rect.left = 0;
	}

	while(w){
		if(w->flags.hidden){
			break;
		}
		
		if(rect.left < 0){
			rect.left = 0;
		}
		
		if(rect.top < 0){
			rect.top = 0;
		}
		
		if(rect.right > w->size[0]){
			rect.right = w->size[0];
		}
		
		if(rect.bottom > w->size[1]){
			rect.bottom = w->size[1];
		}
		
		rect.left += w->pos[0];
		rect.top += w->pos[1];
		rect.right += w->pos[0];
		rect.bottom += w->pos[1];
		
		if(!w->parent){
			break;
		}

		w = w->parent;
	}
	
	if(!w->flags.hidden){
		suiWindowMsgSend(w, SUI_WM_INVALIDATE_RECT, &rect);
	}
}

void suiWindowInvalidate(SUIWindow* w){
	if(!w){
		return;
	}
	
	suiWindowInvalidateRect(w, 0, 0, w->size[0], w->size[1]);
}

S32 suiWindowGetTextSize(	SUIWindow* w,
							const char* text,
							U32 height,
							S32* sxOut,
							S32* syOut)
{
	SUIRootWindow* rw = SAFE_MEMBER(w, rootWindow);
	
	if(!rw){
		return 0;
	}else{
		SUIWindowMsgGetTextSize		md = {0};
		SUIWindowMsgGetTextSizeOut	out = {0};
		
		md.out = &out;
		md.text = text;
		md.height = height;
		
		suiWindowMsgSend(rw->w, SUI_WM_GET_TEXT_SIZE, &md);
		
		if(!out.flags.gotSize){
			return 0;
		}

		if(sxOut){
			*sxOut = out.size[0];
		}
		
		if(syOut){
			*syOut = out.size[1];
		}

		return 1;
	}
}

static S32 suiWindowCheckClipRect(	SUIRootWindowDrawInfo* info,
									S32 x,
									S32 y,
									S32 sx,
									S32 sy)
{
	if(info->clipRect.isSet){
		return 1;
	}

	if(sx < 0){
		x += sx;
		sx = -sx;
	}
	
	if(sy < 0){
		y += sy;
		sy = -sy;
	}
	
	if(	x + sx > info->clipRect.queued.x &&
		x < info->clipRect.queued.x2 &&
		y + sy > info->clipRect.queued.y &&
		y < info->clipRect.queued.y2)
	{
		if(!info->clipRect.isSet){
			info->clipRect.isSet = 1;
			info->clipRect.set = info->clipRect.queued;
			
			info->funcs.setClipRect(info->userPointer,
									info->clipRect.set.x,
									info->clipRect.set.y,
									info->clipRect.set.sx,
									info->clipRect.set.sy);
		}
		
		return 1;
	}
	
	return 0;
}

void suiDrawLine(	const SUIDrawContext* dc,
					S32 x0,
					S32 y0,
					S32 x1,
					S32 y1,
					U32 colorARGB)
{
	SUIRootWindowDrawInfo* info = dc->info;
	
	if(info->funcs.drawFilledRect){
		S32 xo = dc->origin[0];
		S32 yo = dc->origin[1];
		
		if(suiWindowCheckClipRect(info, xo + x0, yo + y0, x1 - x0, y1 - y0)){
			info->funcs.drawLine(info->userPointer, xo + x0, yo + y0, xo + x1, yo + y1, colorARGB);
		}
	}
}

void suiDrawFilledRect(	const SUIDrawContext* dc,
						S32 x,
						S32 y,
						S32 sx,
						S32 sy,
						U32 colorARGB)
{
	SUIRootWindowDrawInfo* info = dc->info;
	
	if(	info->funcs.drawFilledRect &&
		sx > 0 &&
		sy > 0)
	{
		PERFINFO_AUTO_START_FUNC();
		{
			S32 xo = dc->origin[0];
			S32 yo = dc->origin[1];
			
			if(suiWindowCheckClipRect(info, xo + x, yo + y, sx, sy)){
				info->funcs.drawFilledRect(	info->userPointer,
											xo + x,
											yo + y,
											sx,
											sy,
											colorARGB);
			}
		}
		PERFINFO_AUTO_STOP();
	}
}

void suiDrawRect(	const SUIDrawContext* dc,
					S32 x,
					S32 y,
					S32 sx,
					S32 sy,
					S32 borderWidth,
					U32 colorARGB)
{
	SUIRootWindowDrawInfo* info = dc->info;
	
	if(	info->funcs.drawRect &&
		sx > 0 &&
		sy > 0 &&
		borderWidth > 0)
	{
		S32 xo = dc->origin[0];
		S32 yo = dc->origin[1];
		
		if(suiWindowCheckClipRect(info, xo + x, yo + y, sx, sy)){
			if(	borderWidth >= sx / 2 ||
				borderWidth >= sy / 2)
			{
				suiDrawFilledRect(dc, x, y, sx, sy, colorARGB);
			}else{
				info->funcs.drawRect(	info->userPointer,
										xo + x,
										yo + y,
										sx,
										sy,
										borderWidth,
										colorARGB);
			}
		}
	}
}

void suiDrawFilledTriangle(	const SUIDrawContext* dc,
							S32 x0,
							S32 y0,
							S32 x1,
							S32 y1,
							S32 x2,
							S32 y2,
							U32 colorARGB)
{
	SUIRootWindowDrawInfo* info = dc->info;
	
	if(info->funcs.drawFilledTriangle){
		S32 xo = dc->origin[0];
		S32 yo = dc->origin[1];
		S32 minx;
		S32 maxx;
		S32 miny;
		S32 maxy;

		minx = min(x0, x1);
		minx = min(minx, x2);
		maxx = max(x0, x1);
		maxx = max(maxx, x2);
		
		miny = min(y0, y1);
		miny = min(miny, y2);
		maxy = max(y0, y1);
		maxy = max(maxy, y2);
		
		if(suiWindowCheckClipRect(info, xo + minx, yo + miny, maxx - minx, maxy - miny)){
			info->funcs.drawFilledTriangle(	info->userPointer,
											xo + x0,
											yo + y0,
											xo + x1,
											yo + y1,
											xo + x2,
											yo + y2,
											colorARGB);
		}
	}
}

void suiPrintText(	const SUIDrawContext* dc,
					S32 x,
					S32 y,
					const char* textData,
					S32 textLen,
					U32 height,
					U32 colorARGB)
{
	SUIRootWindowDrawInfo* info = dc->info;
	
	if( info->funcs.printText &&
		textData &&
		textLen)
	{
		S32 xo = dc->origin[0];
		S32 yo = dc->origin[1];
		
		if(suiWindowCheckClipRect(info, xo + x, yo + y, 100000, 40)){
			info->funcs.printText(	info->userPointer,
									xo + x,
									yo + y,
									textData,
									textLen,
									height,
									colorARGB);
		}
	}
}

static void suiWindowDraw(	SUIWindow* w,
							SUIDrawContext* dc)
{
	//if(0){
	//	S32 a = 0x40;//0x60 + (rand() & 0x7f);
	//	S32 b = 0x40;//0x60 + (rand() & 0x7f);
	//	S32 c = (c = 0x40) + (rand() & (0xff - c));
	//	//U32 color = ((a / 1) << 16) | ((b / 1) << 8) | ((c / 1) << 0);
	//	U32 color;
	//	S32 sx = suiWindowGetSizeX(w);
	//	S32 sy = suiWindowGetSizeY(w);
	//	
	//	if(w->parent){
	//		color =	((c / 1) << 16) | ((c / 4) << 8) | ((c / 3) << 0);
	//	}else{
	//		color =	((c / 1) << 16) | ((c / 2) << 8) | ((c / 5) << 0);
	//	}
	//	
	//	suiDrawFilledRect(dc, 0, 0, w->size[0], w->size[1], color);
	//	
	//	if(!w->parent){
	//		suiDrawFilledRect(dc, 15, 15, w->size[0] - 30, w->size[1] - 30, 0xff000000);
	//		suiDrawFilledRect(dc, 17, 17, w->size[0] - 34, w->size[1] - 34, 0xff111122);
	//	}
	//	
	//	if(1){
	//		// Draw a 2 pixel black border.
	//		
	//		suiDrawFilledRect(dc, 0, 0, 2, w->size[1], 0xff000000);
	//		suiDrawFilledRect(dc, 2, 0, w->size[0] - 4, 2, 0xff000000);
	//		suiDrawFilledRect(dc, 2, w->size[1] - 2, w->size[0] - 4, 2, 0xff000000);
	//		suiDrawFilledRect(dc, w->size[0] - 2, 0, 2, w->size[1], 0xff000000);
	//	}
	//	
	//	if(0){
	//		char textData[100];
	//		
	//		sprintf_s(SAFESTR(textData), "Window 0x%8.8x", (intptr_t)w);
	//		
	//		suiPrintText(dc, 11, 11, textData, -1, 0x000000);
	//		suiPrintText(dc, 10, 10, textData, -1, 0xaaffaa);
	//	}
	//}
	
	S32 didDraw;
	
	PERFINFO_AUTO_START("suiMsg:SUI_WM_DRAW", 1);
		didDraw = suiWindowMsgSend(w, SUI_WM_DRAW, dc);
	PERFINFO_AUTO_STOP();
	
	#if DRAW_CRAP_IF_NO_DRAW
		if(!didDraw){
			PERFINFO_AUTO_START("suiWindowDraw", 1);
				suiDrawFilledRect(	dc, 
									0,
									0,
									w->size[0],
									w->size[1],
									0xffffaa22);
									
				suiDrawRect(dc, 3, 3, w->size[0] - 6, suiWindowGetSizeY(w) - 6, 3, 0xff332233);
				suiDrawRect(dc, 0, 0, w->size[1], suiWindowGetSizeY(w), 3, 0xff000000);
			PERFINFO_AUTO_STOP();
		}
	#endif
}

S32 suiClipDrawContext(	const SUIDrawContext* dc,
						S32 x,
						S32 y,
						S32 sx,
						S32 sy,
						SUIClipDrawContextCallback callback,
						void* userPointer)
{
	SUIDrawContext	dcNew = {0};
	S32				pos[2] = {x, y};
	S32				size[2] = {sx, sy};
	
	if(	!dc ||
		!callback)
	{
		return 0;
	}
	
	dcNew.info = dc->info;
	
	// Get the new "origin" which is the upper left corner of this child window in DC coords.
	
	ARRAY_FOREACH_BEGIN(dc->rect, i);
		S32 cornerCoord;
		
		dcNew.origin[i] =	dc->origin[i] +
							pos[i];
		
		dcNew.rect[i][0] = MAX(	dcNew.origin[i],
								dc->rect[i][0]);

		cornerCoord =	dcNew.origin[i] +
						size[i];
						
		dcNew.rect[i][1] = MIN(	cornerCoord,
								dc->rect[i][1]);

		dcNew.size[i] = dcNew.rect[i][1] -
						dcNew.rect[i][0];
		
		if(dcNew.size[i] <= 0){
			return 0;
		}
	ARRAY_FOREACH_END;
	
	suiClearClipRect(&dcNew);

	callback(	&dcNew,
				userPointer);
				
	suiClearClipRect(dc);

	return 1;
}

static void suiSetClipRectHelper(const SUIDrawContext* dc, S32 x, S32 y, S32 sx, S32 sy){
	SUIRootWindowDrawInfo* info = dc->info;
	
	if(	x != info->clipRect.set.x ||
		y != info->clipRect.set.y ||
		sx != info->clipRect.set.sx ||
		sy != info->clipRect.set.sy)
	{
		info->clipRect.queued.x = x;
		info->clipRect.queued.y = y;
		info->clipRect.queued.x2 = x + sx;
		info->clipRect.queued.y2 = y + sy;
		info->clipRect.queued.sx = sx;
		info->clipRect.queued.sy = sy;
		info->clipRect.isSet = 0;
	}else{
		info->clipRect.isSet = 1;
	}
}

void suiSetClipRect(const SUIDrawContext* dc, S32 x, S32 y, S32 sx, S32 sy){
	S32 x2;
	S32 y2;
	
	x += dc->origin[0];
	y += dc->origin[1];
	
	MAX1(sx, 0);
	MAX1(sy, 0);
	
	x2 = x + sx;
	y2 = y + sy;
	
	MAX1(x, dc->rect[0][0]);
	MAX1(y, dc->rect[1][0]);
	MIN1(x2, dc->rect[0][1]);
	MIN1(y2, dc->rect[1][1]);
	
	suiSetClipRectHelper(dc, x, y, x2 - x, y2 - y);
}

void suiClearClipRect(const SUIDrawContext* dc){
	suiSetClipRectHelper(	dc,
							dc->rect[0][0],
							dc->rect[1][0],
							dc->size[0],
							dc->size[1]);
}

static void suiWindowDrawRecurse(	SUIWindow* w,
									SUIDrawContext* dc)
{
	SUIWindow*				wChild;
	SUIDrawContext			dcOrig = *dc;
	
	assert(	dc->size[0] >= 0 &&
			dc->size[1] >= 0);
	
	suiClearClipRect(dc);
	
	suiWindowDraw(w, dc);

	for(wChild = w->child.head;
		wChild;
		wChild = wChild->sibling.next)
	{
		S32 childAreaIsEmpty = 0;
		
		// Get the new "origin" which is the upper left corner of this child window in DC coords.
		
		ARRAY_FOREACH_BEGIN(dc->rect, i);
			S32 cornerCoord;
			
			dc->origin[i] = dcOrig.origin[i] +
							wChild->pos[i];
			
			dc->rect[i][0] = MAX(dc->origin[i], dcOrig.rect[i][0]);

			cornerCoord = dc->origin[i] + wChild->size[i];
			dc->rect[i][1] = MIN(cornerCoord, dcOrig.rect[i][1]);

			dc->size[i] = dc->rect[i][1] - dc->rect[i][0];
			
			if(dc->size[i] <= 0){
				childAreaIsEmpty = 1;
				break;
			}
		ARRAY_FOREACH_END;
		
		if(childAreaIsEmpty){
			continue;
		}
		
		suiWindowDrawRecurse(wChild, dc);
	}

	*dc = dcOrig;

	#if 0
	{
		char textData[100];
		suiClearClipRect(dc);
		sprintf_s(	SAFESTR(textData),
					"refs: %d%s",
					w->refCount.suiInternal,
					w->flags.destroyed ? " (destroyed)" : "");
		suiPrintText(dc, 6, 6, textData, -1, 0);
		suiPrintText(dc, 5, 5, textData, -1, 0xff99ff99);
	}
	#endif

	//drawClipBox(dc->hdc, &clipRect);
	
}

static void suiWindowProcess(SUIWindow* w){
	suiWindowInternalRefInc(w);

	if(w->processingHandles){
		PERFINFO_AUTO_START("process", 1);
			suiWindowMsgSend(w, SUI_WM_PROCESS, NULL);
		PERFINFO_AUTO_STOP();
	}
	
	if(w->processingChildCount){
		SUIWindowIter	it;
		SUIWindow*		wChild;
		
		for(suiWindowIterInit(&it, w, 1);
			suiWindowIterGetCur(&wChild, &it);
			suiWindowIterGotoPrev(&it))
		{
			suiWindowProcess(wChild);
		}
		
		suiWindowIterDeInit(&it);
	}

	suiWindowInternalRefDec(w);
}

static S32 suiWindowMouseLeaveSendToChild(	SUIWindow* w,
											SUIWindowMsg* msg,
											SUIWindowMsgMouseLeave* md)
{
	static S32 suiWindowMouseLeaveRecurse(	SUIWindow* w,
											SUIWindowMsg* msg,
											SUIWindowMsgMouseLeave*	md);
	
	SUIWindowMsgMouseLeave	mdOrig = *md;
	S32						ret;
	
	//md->x -= child->pos[0];
	//md->y -= child->pos[1];
	
	ret = suiWindowMouseLeaveRecurse(w, msg, md);

	*md = mdOrig;
	
	return ret;
}

static S32 suiWindowMouseLeaveRecurse(	SUIWindow* w,
										SUIWindowMsg* msg,
										SUIWindowMsgMouseLeave*	md)
{
	SUIWindowMouseStatus* ms = w->mouseStatus;
	
	if(SAFE_MEMBER(ms, underMouse)){
		S32 ret;
		
		if(w == ms->underMouse){
			suiWindowMsgSendDirect(w, msg);
			
			ret = 1;
		}else{
			//printf("Child owns: 0x%8.8x\n", w->children.mouseOwner);
			
			ret = suiWindowMouseLeaveSendToChild(ms->underMouse, msg, md);
		}
		
		ms->underMouse = NULL;
		
		return ret;
	}
	
	return 0;
}

static S32 suiWindowMouseLeaveSend(SUIWindow* w){
	if(w){
		SUIWindowMsg			msg = {0};
		SUIWindowMsgMouseLeave	md = {0};
		SUIWindowMouseStatus*	ms = SAFE_MEMBER(w, mouseStatus);
		
		msg.msgType = SUI_WM_MOUSE_LEAVE;
		msg.msgData = &md;
		
		return suiWindowMouseLeaveRecurse(w, &msg, &md);
	}
	
	return 0;
}

static S32 suiWindowMouseNoOwnershipChangeSendToChild(	SUIWindow* w,
														SUIWindowMsg* msg,
														S32 x,
														S32 y,
														S32 inRect,
														void* mdVoid)
{
	S32 xOrig = x;
	S32 yOrig = y;
	S32 ret;
	
	x -= w->pos[0];
	y -= w->pos[1];
	
	if(msg->msgType == SUI_WM_MOUSE_MOVE){
		SUIWindowMsgMouseMove* md = mdVoid;
		
		md->inRect = inRect;
		md->x = x;
		md->y = y;
	}
	else if(msg->msgType == SUI_WM_MOUSE_WHEEL){
		SUIWindowMsgMouseWheel* md = mdVoid;
		
		md->inRect = inRect;
		md->x = x;
		md->y = y;
	}
	
	{
		static S32 suiWindowMouseNoOwnershipChangeRecurse(	SUIWindow* w,
															SUIWindowMsg* msg,
															S32 x,
															S32 y,
															void* mdVoid);

		ret = suiWindowMouseNoOwnershipChangeRecurse(w, msg, x, y, mdVoid);
	}

	if(msg->msgType == SUI_WM_MOUSE_MOVE){
		SUIWindowMsgMouseMove* md = mdVoid;
		
		md->x = xOrig;
		md->y = yOrig;
	}
	else if(msg->msgType == SUI_WM_MOUSE_WHEEL){
		SUIWindowMsgMouseWheel* md = mdVoid;
		
		md->x = xOrig;
		md->y = yOrig;
	}

	return ret;
}

static S32 suiWindowPointInRect(SUIWindow* w,
								S32 x,
								S32 y)
{
	x -= w->pos[0];
	
	if(x >= 0 && x < w->size[0]){
		y -= w->pos[1];
		
		if(y >= 0 && y < w->size[1]){
			return 1;
		}
	}
	
	return 0;
}

static S32 suiWindowMouseNoOwnershipChangeRecurse(	SUIWindow* w,
													SUIWindowMsg* msg,
													S32 x,
													S32 y,
													void* mdVoid)
{
	SUIWindowMouseStatus* ms = w->mouseStatus;
	
	if(SAFE_MEMBER(ms, mouseOwnerIsChild)){
		S32 ret;

		if(ms->mouseOwner){
			//printf("Child owns: 0x%8.8x\n", w->children.mouseOwner);
			
			S32 inRect = suiWindowPointInRect(ms->mouseOwner, x, y);

			ret = suiWindowMouseNoOwnershipChangeSendToChild(	ms->mouseOwner,
																msg,
																x,
																y,
																inRect,
																mdVoid);
		}else{
			ret = 1;
		}
		
		return ret;
	}
	else if(w != SAFE_MEMBER(ms, mouseOwner)){
		SUIWindowIter	it;
		SUIWindow*		wChild;
		S32				ret = 0;

		// Go through the children backwards.
		
		for(suiWindowIterInit(&it, w, 1);
			suiWindowIterGetCur(&wChild, &it);
			suiWindowIterGotoPrev(&it))
		{
			if(!suiWindowPointInRect(wChild, x, y)){
				continue;
			}
			
			ret = suiWindowMouseNoOwnershipChangeSendToChild(	wChild,
																msg,
																x,
																y,
																1,
																mdVoid);
			
			if(ret){
				//printf("Child used: 0x%8.8x\n", wChild);
				
				if(!ms){
					ms = suiWindowGetMouseStatus(w);
				}
				
				if(	ms->underMouse &&
					ms->underMouse != wChild)
				{
					suiWindowMouseLeaveSend(ms->underMouse);
				}
				
				ms->underMouse = wChild;

				break;
			}
		}
		
		suiWindowIterDeInit(&it);
		
		if(ret){
			return ret;
		}
	}
		
	if(suiWindowMsgSendDirect(w, msg)){
		if(!ms){
			ms = suiWindowGetMouseStatus(w);
		}
		
		if(	ms->underMouse &&
			ms->underMouse != w)
		{
			suiWindowMouseLeaveSend(ms->underMouse);
		}
		
		ms->pos[0] = x;
		ms->pos[1] = y;
		
		ms->underMouse = w;
		
		return 1;
	}
	
	return 0;
}

static S32 suiWindowMouseDownSendToChild(	SUIWindow* w,
											SUIWindowMsg* msg,
											SUIWindowMsgMouseButton* md)
{
	static S32 suiWindowMouseDownRecurse(	SUIWindow* w,
											SUIWindowMsg* msg,
											SUIWindowMsgMouseButton* md);
	
	SUIWindowMsgMouseButton mdOrig;
	S32 ret;

	mdOrig = *md;

	md->x -= w->pos[0];
	md->y -= w->pos[1];
	
	ret = suiWindowMouseDownRecurse(w, msg, md);
	
	*md = mdOrig;
	
	return ret;
}

static S32 suiWindowMouseDownRecurse(	SUIWindow* w,
										SUIWindowMsg* msg,
										SUIWindowMsgMouseButton* md)
{
	SUIWindowMouseStatus*	ms = suiWindowGetMouseStatus(w);
	S32						ret;
	
	if(SAFE_MEMBER(ms, mouseOwnerIsChild)){
		// If a child owns the mouse then it gets the message.
		
		if(ms->mouseOwner){
			md->inRect = suiWindowPointInRect(ms->mouseOwner, md->x, md->y);
			
			ret = suiWindowMouseDownSendToChild(ms->mouseOwner, msg, md);
		}else{
			ret = 1;
		}
		
		if(ret){
			ms->heldButtons |= md->button;
		}
		
		return ret;
	}
	else if(w != SAFE_MEMBER(ms, mouseOwner)){
		SUIWindowIter	it;
		SUIWindow*		wChild;
		
		ret = 0;

		// Go through the children backwards.
		
		for(suiWindowIterInit(&it, w, 1);
			suiWindowIterGetCur(&wChild, &it);
			suiWindowIterGotoPrev(&it))
		{
			if(!suiWindowPointInRect(wChild, md->x, md->y)){
				continue;
			}

			md->inRect = 1;
			
			ret = suiWindowMouseDownSendToChild(wChild, msg, md);

			if(ret){
				if(ms){
					ms->mouseOwnerIsChild = 1;
				}
				
				if(wChild->parent == w){
					ms->mouseOwner = wChild;
				}
				
				//printf("Child now owns: 0x%8.8x\n", wChild);
				
				break;
			}
		}
		
		suiWindowIterDeInit(&it);
		
		if(ret){
			if(ms){
				ms->heldButtons |= md->button;
			}
	
			return ret;
		}
	}
	
	// If no children want it, then send to myself.
		
	if(suiWindowMsgSendDirect(w, msg)){
		if(ms){
			ms->mouseOwner = w;
			
			ms->heldButtons |= md->button;
		}
	
		return 1;
	}
	
	return 0;
}

static S32 suiWindowMouseUpSendToChild(	SUIWindow* w,
										SUIWindowMsg* msg,
										SUIWindowMsgMouseButton* md)
{
	static S32 suiWindowMouseUpRecurse(	SUIWindow* w,
										SUIWindowMsg* msg,
										SUIWindowMsgMouseButton* md);
	
	SUIWindowMsgMouseButton mdOrig;
	S32 ret;

	mdOrig = *md;

	md->x -= w->pos[0];
	md->y -= w->pos[1];
	
	ret = suiWindowMouseUpRecurse(w, msg, md);
	
	*md = mdOrig;
	
	return ret;
}

static S32 suiWindowMouseUpRecurse(	SUIWindow* w,
									SUIWindowMsg* msg,
									SUIWindowMsgMouseButton* md)
{
	SUIWindowMouseStatus* ms = suiWindowGetMouseStatus(w);
	
	ms->heldButtons &= ~md->button;
	
	if(ms->mouseOwnerIsChild){
		S32 ret;
		
		// If a child owns the mouse then it gets the message.
		
		if(ms->mouseOwner){
			md->inRect = suiWindowPointInRect(ms->mouseOwner, md->x, md->y);
			
			ret = suiWindowMouseUpSendToChild(ms->mouseOwner, msg, md);
		}else{
			ret = 1;
		}
		
		if(!ms->heldButtons){
			ms->mouseOwnerIsChild = 0;
			ms->mouseOwner = NULL;
		}
		
		return ret;
	}
	else if(w != ms->mouseOwner){
		SUIWindowIter	it;
		SUIWindow*		wChild;
		S32				ret;

		// Go through the children backwards.
		
		for(suiWindowIterInit(&it, w, 1);
			suiWindowIterGetCur(&wChild, &it);
			suiWindowIterGotoPrev(&it))
		{
			if(!suiWindowPointInRect(wChild, md->x, md->y)){
				continue;
			}
			
			md->inRect = 1;
			
			ret = suiWindowMouseUpSendToChild(wChild, msg, md);

			if(ret){
				break;
			}
		}
		
		suiWindowIterDeInit(&it);
		
		if(ret){
			return ret;
		}
	}
		
	suiWindowMsgSendDirect(w, msg);
	
	if(!ms->heldButtons){
		ms->mouseOwner = NULL;
	}
	
	return 1;
}

// ---------- SUIRootWindow Functions --------------------------------------------------------------

S32	suiRootWindowCreate(SUIRootWindow** rwOut,
						SUIWindowMsgHandler msgHandler,
						const void* createParams)
{
	SUIRootWindow* rw;
	
	rw = callocStruct(SUIRootWindow);
	
	if(!suiWindowCreate(&rw->w, NULL, msgHandler, createParams)){
		SAFE_FREE(rw);
		return 0;
	}
	
	suiWindowSetRootRecurse(rw->w, rw);
	
	*rwOut = rw;
	
	return 1;
}

S32	suiRootWindowDestroy(SUIRootWindow** rwInOut){
	SUIRootWindow* rw = SAFE_DEREF(rwInOut);
	
	if(!rw){
		return 0;
	}
	
	suiWindowDestroy(&rw->w);
	
	SAFE_FREE(*rwInOut);
	
	return 1;
}

S32 suiRootWindowNeedsProcess(SUIRootWindow* rw){
	return	SAFE_MEMBER(rw, w)
			&&
			(	rw->w->processingHandles ||
				rw->w->processingChildCount);
}

void suiRootWindowProcess(SUIRootWindow* rw){
	PERFINFO_AUTO_START_FUNC();
		#if 0
		{
			U64 a;
			U64 b;
			
			{__asm	rdtsc						}
			{__asm	mov		ebx,			eax	}
			{__asm	mov		ecx,			edx	}
			{__asm	rdtsc						}
			{__asm	mov		dword ptr[a],	ebx	}
			{__asm	mov		dword ptr[a]+4,	ecx	}
			{__asm	mov		dword ptr[b],	eax	}
			{__asm	mov		dword ptr[b]+4,	edx	}
			
			//GET_CPU_TICKS_64(a);
			//GET_CPU_TICKS_64(b);
			
			printf("min: %I64u\n", b - a);
		}
		#endif
		
		#if 0
		{
			static StashTable st;
			ATOMIC_INIT_BEGIN;
			{
				st = stashTableCreateWithStringKeys(100, StashDefault);
				stashAddPointer(st, "", NULL, false);
				stashAddPointer(st, "blah", NULL, false);
			}
			ATOMIC_INIT_END;
			
			PERFINFO_AUTO_START("stashFindPointer", 1);
			stashFindPointer(st, "", NULL);
			PERFINFO_AUTO_STOP();

			PERFINFO_AUTO_START("stashFindPointer x100", 1);
				FOR_BEGIN(i, 1000);
					PERFINFO_AUTO_START("stashFindPointer", 1);
					stashFindPointer(st, "", NULL);
					PERFINFO_AUTO_STOP();
				FOR_END;
			PERFINFO_AUTO_STOP();
		}
		#endif
		
		#if 0
		{
			PERFINFO_AUTO_START("test1", 1);
				PERFINFO_AUTO_START("nothing", 1);
				PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();

			PERFINFO_AUTO_START("test2", 1);
				FOR_BEGIN(i, 100);
					PERFINFO_AUTO_START("something", 1);
						PERFINFO_AUTO_START("nothing", 1);
						PERFINFO_AUTO_STOP();
					PERFINFO_AUTO_STOP();
				FOR_END;
			PERFINFO_AUTO_STOP();
		}
		#endif
				
		suiWindowProcess(rw->w);
	PERFINFO_AUTO_STOP();
}

void suiRootWindowDraw(	SUIRootWindow* rw,
						SUIRootWindowDrawInfo* info,
						S32 origin_x,
						S32 origin_y,
						S32 x,
						S32 y,
						S32 sx,
						S32 sy,
						S32 drawBorderAroundUpdateArea)
{
	SUIDrawContext dc = {0};
	
	if(	!rw ||
		!info)
	{
		return;
	}
	
	dc.info = info;
	dc.origin[0] = origin_x;
	dc.origin[1] = origin_y;
	dc.rect[0][0] = x;
	dc.rect[1][0] = y;
	dc.rect[0][1] = x + sx;
	dc.rect[1][1] = y + sy;
	dc.size[0] = sx;
	dc.size[1] = sy;
	
	//printf(	"drawing %d,%d-%d,%d, origin %d,%d\n",
	//		x,
	//		y,
	//		x + sx,
	//		y + sy,
	//		origin_x,
	//		origin_y);
	
	assert(info->funcs.setClipRect);
	
	suiWindowDrawRecurse(rw->w, &dc);
	
	if(drawBorderAroundUpdateArea){
		// Draw a colored box around the area that was updated.
		
		static U32 value;
		value = (value + 8) % 24;
		suiClearClipRect(&dc);
		suiDrawRect(&dc, x, y, sx, sy, 1, 0xff000000 | (0xff << value));
	}
}

void suiRootWindowMouseMove(SUIRootWindow* rw, S32 x, S32 y){
	SUIWindowMsg			msg = {0};
	SUIWindowMsgMouseMove	md = {0};
	SUIWindowMouseStatus*	ms = SAFE_MEMBER(rw->w, mouseStatus);
	
	md.x = x;
	md.y = y;
	md.button = SAFE_MEMBER(ms, heldButtons);
	
	msg.msgType = SUI_WM_MOUSE_MOVE;
	msg.msgData = &md;
	
	suiWindowMouseNoOwnershipChangeRecurse(rw->w, &msg, x, y, &md);
}

void suiRootWindowMouseWheel(	SUIRootWindow* rw,
								S32 x,
								S32 y,
								S32 clickThousandths)
{
	SUIWindowMsg			msg = {0};
	SUIWindowMsgMouseWheel	md = {0};
	SUIWindowMouseStatus*	ms = SAFE_MEMBER(rw->w, mouseStatus);
	
	md.x = x;
	md.y = y;
	md.button = SAFE_MEMBER(ms, heldButtons);
	md.clickThousandths = clickThousandths;
	
	msg.msgType = SUI_WM_MOUSE_WHEEL;
	msg.msgData = &md;
	
	suiWindowMouseNoOwnershipChangeRecurse(rw->w, &msg, x, y, &md);
	
	suiRootWindowMouseMove(rw, x, y);
}

void suiRootWindowMouseDown(SUIRootWindow* rw, S32 x, S32 y, U32 button){
	SUIWindowMsg			msg = {0};
	SUIWindowMsgMouseButton	md = {0};
	
	rw->downButtons |= button;
	
	md.x = x;
	md.y = y;
	md.button = button;
	
	msg.msgType = SUI_WM_MOUSE_DOWN;
	msg.msgData = &md;
	
	suiWindowMouseDownRecurse(rw->w, &msg, &md);
}

void suiRootWindowMouseDoubleClick(SUIRootWindow* rw, S32 x, S32 y, U32 button){
	SUIWindowMsg			msg = {0};
	SUIWindowMsgMouseButton	md = {0};
	
	rw->downButtons |= button;

	md.x = x;
	md.y = y;
	md.button = button;
	
	msg.msgType = SUI_WM_MOUSE_DOUBLECLICK;
	msg.msgData = &md;
	
	suiWindowMouseDownRecurse(rw->w, &msg, &md);
}

void suiRootWindowMouseUp(SUIRootWindow* rw, S32 x, S32 y, U32 button){
	SUIWindowMsg			msg = {0};
	SUIWindowMsgMouseButton	md = {0};
	
	rw->downButtons &= ~button;

	md.x = x;
	md.y = y;
	md.button = button;
	
	msg.msgType = SUI_WM_MOUSE_UP;
	msg.msgData = &md;
	
	suiWindowMouseUpRecurse(rw->w, &msg, &md);
}

static S32 suiWindowKeyRecurse(	SUIWindow* w,
								SUIWindowMsg* msg)
{
	S32 ret;
	
	suiWindowInternalRefInc(w);

	ret = suiWindowMsgSendDirect(w, msg);
	
	if(!ret){
		SUIWindowIter	it;
		SUIWindow*		wChild;
		
		for(suiWindowIterInit(&it, w, 1);
			suiWindowIterGetCur(&wChild, &it);
			suiWindowIterGotoPrev(&it))
		{
			ret = suiWindowKeyRecurse(wChild, msg);
			
			if(ret){
				break;
			}
		}
		
		suiWindowIterDeInit(&it);
	}

	suiWindowInternalRefDec(w);
	
	return ret;
}

S32 suiRootWindowKey(	SUIRootWindow* rw,
						SUIKey key,
						U32 character,
						S32 isDown,
						U32 modBits)
{
	SUIWindowMsg	msg = {0};
	SUIWindowMsgKey	md = {0};
	
	md.key = key;
	md.character = character;
	md.modBits = modBits;
	
	msg.msgType = isDown ? SUI_WM_KEY_DOWN : SUI_WM_KEY_UP;
	msg.msgData = &md;
	
	if(isDown){
		if(rw->w->keyboardOwner){
			SUIWindow* w = rw->w->keyboardOwner;
			
			while(w){
				if(w->keyboardOwner == w){
					break;
				}
				
				w = w->keyboardOwner;
			}
			
			md.flags.isOwned = 1;
			
			if(suiWindowMsgSendDirect(w, &msg)){
				return 1;
			}

			md.flags.isOwned = 0;
		}
	}

	return suiWindowKeyRecurse(rw->w, &msg);
}

void suiRootWindowMouseLeave(SUIRootWindow* rw){
	suiWindowMouseLeaveSend(rw->w);
}

void suiRootWindowLoseFocus(SUIRootWindow* rw){
	if(!rw){
		return;
	}
	
	if(rw->downButtons){
		FOR_BEGIN(i, 32);
			if(rw->downButtons & (1 << i)){
				suiRootWindowMouseUp(rw, 0, 0, 1 << i);
			}
		FOR_END;
	}
}

void suiRootWindowSetSize(	SUIRootWindow* rw,
							S32 sx,
							S32 sy)
{
	suiWindowSetSize(rw->w, sx, sy);
}

S32 suiRootWindowAddChild(	SUIRootWindow* rw,
							SUIWindow* w)
{
	return suiWindowAddChild(	rw->w,
								w);
}

// --- SUI Helper Functions ------------------------------------------------------------------------

#define ARGB_A_SHIFT	24
#define ARGB_R_SHIFT	16
#define ARGB_G_SHIFT	8
#define ARGB_B_SHIFT	0
#define ARGB_A_MASK		(0xff << ARGB_A_SHIFT)
#define ARGB_R_MASK		(0xff << ARGB_R_SHIFT)
#define ARGB_G_MASK		(0xff << ARGB_G_SHIFT)
#define ARGB_B_MASK		(0xff << ARGB_B_SHIFT)
#define ARGB_A(c) 		(((c) >> ARGB_A_SHIFT) & 0xff)
#define ARGB_R(c) 		(((c) >> ARGB_R_SHIFT) & 0xff)
#define ARGB_G(c) 		(((c) >> ARGB_G_SHIFT) & 0xff)
#define ARGB_B(c) 		(((c) >> ARGB_B_SHIFT) & 0xff)

U32 suiColorInterpAllRGB(U32 a, U32 rgbFrom, U32 rgbTo, U32 scale){
	scale &= 0xff;
	a &= 0xff;
	
	return	(a << ARGB_A_SHIFT) |
			((ARGB_R(rgbFrom) + (ARGB_R(rgbTo) - ARGB_R(rgbFrom)) * scale / 0xff) << ARGB_R_SHIFT) |
			((ARGB_G(rgbFrom) + (ARGB_G(rgbTo) - ARGB_G(rgbFrom)) * scale / 0xff) << ARGB_G_SHIFT) |
			((ARGB_B(rgbFrom) + (ARGB_B(rgbTo) - ARGB_B(rgbFrom)) * scale / 0xff) << ARGB_B_SHIFT);
}

U32 suiColorInterpSeparateRGB(U32 a, U32 rgbFrom, U32 rgbTo, U32 scaleR, U32 scaleG, U32 scaleB){
	scaleR &= 0xff;
	scaleG &= 0xff;
	scaleB &= 0xff;
	a &= 0xff;
	
	return	(a << ARGB_A_SHIFT) |
			((ARGB_R(rgbFrom) + (ARGB_R(rgbTo) - ARGB_R(rgbFrom)) * scaleR / 0xff) << ARGB_R_SHIFT) |
			((ARGB_G(rgbFrom) + (ARGB_G(rgbTo) - ARGB_G(rgbFrom)) * scaleG / 0xff) << ARGB_G_SHIFT) |
			((ARGB_B(rgbFrom) + (ARGB_B(rgbTo) - ARGB_B(rgbFrom)) * scaleB / 0xff) << ARGB_B_SHIFT);
}

U32 suiColorSetA(U32 argb, U32 newA){
	return (argb & ~ARGB_A_MASK) | ((newA & 0xff) << ARGB_A_SHIFT);
}

U32 suiColorSetR(U32 argb, U32 newR){
	return (argb & ~ARGB_R_MASK) | ((newR & 0xff) << ARGB_R_SHIFT);
}

U32 suiColorSetG(U32 argb, U32 newG){
	return (argb & ~ARGB_G_MASK) | ((newG & 0xff) << ARGB_G_SHIFT);
}

U32 suiColorSetB(U32 argb, U32 newB){
	return (argb & ~ARGB_B_MASK) | ((newB & 0xff) << ARGB_B_SHIFT);
}
