#ifndef NO_EDITORS

#include "net/net.h"
#include "StashTable.h"
#include "WorldEditorClientMain.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct EditAsyncOp
{
	U32 req_id;
	EditAsyncOpFunction callback;
	void *userdata;
} EditAsyncOp;

void editLibCreateAsyncOp(U32 req_id, EditAsyncOpFunction callback, void *userdata)
{
	EditAsyncOp *new_op = calloc(1, sizeof(EditAsyncOp));
	new_op->req_id = req_id;
	new_op->callback = callback;
	new_op->userdata = userdata;
	if (!editState.asyncOpTable)
		editState.asyncOpTable = stashTableCreateInt(16);
	assert(stashIntAddPointer(editState.asyncOpTable, req_id, new_op, false));
}

void editLibHandleServerReply(Packet* pak)
{
	EditAsyncOp *async_op = NULL;
	U32 req_id = pktGetBitsAuto(pak);
	U32 ret = pktGetBitsAuto(pak);
	U32 step = pktGetBitsAuto(pak);
	U32 total = pktGetBitsAuto(pak);
	if (stashIntFindPointer(editState.asyncOpTable, req_id, &async_op))
	{
		if (async_op->callback(ret, async_op->userdata, step, total))
		{
			stashIntRemovePointer(editState.asyncOpTable, async_op->req_id, NULL);
			SAFE_FREE(async_op);
		}
	}
}

#endif // NO_EDITORS