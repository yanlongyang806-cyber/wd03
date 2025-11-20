#ifndef NO_EDITORS

#include "estring.h"
#include "objpath.h"
#include "wininclude.h"

#include "EditLib.h"
#include "EditLibUndo.h"

#include "EditorObject.h"
#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef enum EditUndoCommand
{
	EditUndoSelect = 1,
	EditUndoTransform,
	EditUndoStruct,
	EditUndoCustom,
    EditUndoTransient,
} EditUndoCommand;

typedef struct EditUndoOperation
{
	int						id;
	EditUndoCommand			command;
	void*					data;
	bool 					linked_next;
	bool					linked_prev;
} EditUndoOperation;

typedef struct EditUndoOperationLog
{
	int id;
	EditUndoCommand command;
	char* data_estr;
	char* log;
	unsigned linked_next : 1;
	unsigned linked_prev : 1;
} EditUndoOperationLog;

typedef struct EditUndoStack
{
	EditUndoOperation**		op_stack;
	int 					current_index;
	EditUndoOperationLog**	op_debug_log;

	void*					context;
	EditUndoSelectFn		select_fn;
	EditUndoTransformFn		transform_fn;

	int						start_linked;
	int						link_depth;
	bool					doing;

    int						current_id;
} EditUndoStack;

typedef struct EditUndoInternalSelectionList
{
	EditorObject** selected_list;
	EditorObject** deselected_list;
} EditUndoInternalSelectionList;

typedef struct EditUndoCustomCommand
{
	EditUndoCustomFn undo_fn;
	EditUndoCustomFn redo_fn;
	EditUndoCustomFreeFn free_fn;
	void *data;
} EditUndoCustomCommand;

// Temporary object for diffing
typedef struct EditUndoStructOp
{
	EditorObject *op_object;
	void *pre_op;
	void *post_op;
	EditUndoChangedFn changed_func;
	ParseTable *tpi;
} EditUndoStructOp;

// The actual undo entry struct
typedef struct EditUndoStructCommand
{
	EditorObject *editor_obj;
	void *obj;
	ParseTable *tpi;
	char *pre_to_post;
	char *post_to_pre;
	EditUndoChangedFn changed_func;
} EditUndoStructCommand;

static CRITICAL_SECTION undo_critical_section;

static void EditDebugLogOp( EditUndoStack *stack, EditUndoOperation* op, const char* log );
static void EditDebugLogOpFinished( EditUndoStack *stack, EditUndoOperation* op );
static void EditUndoOpDestroyLog( EditUndoOperationLog* op );

#endif
AUTO_RUN;
void InitEditLibUndo(void)
{
#ifndef NO_EDITORS
	InitializeCriticalSection(&undo_critical_section);
#endif
}
#ifndef NO_EDITORS

EditUndoStack* EditUndoStackCreate()
{
	EditUndoStack *ret = (EditUndoStack *)calloc(1, sizeof(EditUndoStack));
	ret->current_index = -1;
	ret->start_linked = -1;
    ret->current_id = 1;
	return ret;
}

void EditUndoStackDestroy(EditUndoStack* stack)
{
	if(stack)
	{
		EditUndoStackClear(stack);
		free(stack);
	}
}

void EditUndoSetContext(EditUndoStack *stack, void *context)
{
	stack->context = context;
}

void EditUndoSetSelectFn(EditUndoStack *stack, EditUndoSelectFn fn)
{
	stack->select_fn = fn;
}

void EditUndoSetTransformFn(EditUndoStack *stack, EditUndoTransformFn fn)
{
	stack->transform_fn = fn;
}

void EditUndoPrintToBuffer(const EditUndoStack *stack, char* buffer, int buffer_size)
{
	char* estr = NULL;
	int it;

	estrConcatf( &estr, "Most recent -> Least recent:\n");
	for( it = eaSize( &stack->op_debug_log ) - 1; it >= 0 && (int)estrLength( &estr ) < buffer_size; --it ) {
		EditUndoOperationLog* op = stack->op_debug_log[ it ];
		
		estrConcatf( &estr, "ID: %d%s%s%s%s\n", op->id,
					 (op->linked_next ? ", LinkedNext" : ""),
					 (op->linked_prev ? ", LinkedPrev" : ""),
					 (op->log && op->log[0] ? ", " : ""), (op->log && op->log[0] ? op->log : ""));
		switch( op->command ) {
			xcase EditUndoStruct:		estrConcatf( &estr, "EditUndoStruct     " );
			xcase EditUndoSelect:		estrConcatf( &estr, "EditUndoSelect     " );
			xcase EditUndoTransform:	estrConcatf( &estr, "EditUndoTransform  " );
			xcase EditUndoCustom:		estrConcatf( &estr, "EditUndoCustom     " );
			xcase EditUndoTransient:	estrConcatf( &estr, "EditUndoTransient	" );
			xdefault:					estrConcatf( &estr, "UNKNOWN TYPE: %d	", op->command );
		}
		estrConcatf( &estr, "%s\n", op->data_estr );
	}

	if( (int)estrLength( &estr ) + 1 > buffer_size ) {
		estrSetSize( &estr, buffer_size - 1 );
	}
	strcpy_s( SAFESTR2( buffer ), estr );
	estrDestroy( &estr );
}

static void undo_clear_stack(EditUndoStack *stack, int offset)
{
	int i;
	EnterCriticalSection(&undo_critical_section);
	for (i = offset; i < eaSize(&stack->op_stack);)
	{
		assert(stack->op_stack); // stupid warning
		switch (stack->op_stack[i]->command)
		{
		case EditUndoSelect:
			{
				int j;
				EditUndoInternalSelectionList* list = (EditUndoInternalSelectionList*)stack->op_stack[i]->data;
				for (j = 0; j < eaSize(&list->selected_list); j++)
					if (list->selected_list[j]) editorObjectDeref(list->selected_list[j]);
				for (j = 0; j < eaSize(&list->deselected_list); j++)
					if (list->deselected_list[j]) editorObjectDeref(list->deselected_list[j]);
				SAFE_FREE(list);
			}
            break;
		case EditUndoTransform:
			SAFE_FREE(stack->op_stack[i]->data);
            break;
		case EditUndoCustom:
        case EditUndoTransient:
			{
				EditUndoCustomCommand *custom = (EditUndoCustomCommand*)stack->op_stack[i]->data;
				if (custom->free_fn)
					custom->free_fn(stack->context, custom->data);
				SAFE_FREE(custom);
			}
        	break;
		case EditUndoStruct:
			{
				EditUndoStructCommand *structcmd = (EditUndoStructCommand*)stack->op_stack[i]->data;
				estrDestroy(&structcmd->post_to_pre);
				estrDestroy(&structcmd->pre_to_post);
				if (structcmd->editor_obj)
					editorObjectDeref(structcmd->editor_obj);
				SAFE_FREE(structcmd);
			}
            break;
		};
		eaRemove(&stack->op_stack, i);
	}
	LeaveCriticalSection(&undo_critical_section);
}

void EditUndoBeginGroup(EditUndoStack *stack)
{
	if (!stack || stack->doing)
		return;
	EnterCriticalSection(&undo_critical_section);
	if (stack->start_linked == -1)
	{
		undo_clear_stack(stack, stack->current_index+1);
		stack->current_index = eaSize(&stack->op_stack)-1;

		stack->start_linked = stack->current_index+1;
		stack->link_depth = 1;
	}
	else stack->link_depth++;
	LeaveCriticalSection(&undo_critical_section);
}

void EditUndoEndGroup(EditUndoStack *stack)
{
	if (!stack || stack->doing)
		return;
	EnterCriticalSection(&undo_critical_section);
	if (--stack->link_depth <= 0)
		stack->start_linked = -1;
	LeaveCriticalSection(&undo_critical_section);
}

EditUndoOperation *create_undo_command(EditUndoStack *stack, bool kill_transients)
{
    int stack_end;
	EditUndoOperation *new_command = (EditUndoOperation*)calloc(1, sizeof(EditUndoOperation));

    // Clear the stack of undone operations and transients
    stack_end = stack->current_index;
    if (kill_transients)
        while (stack_end >= 0 &&
               stack->op_stack[stack_end]->command == EditUndoTransient)
            stack_end--;
    
	undo_clear_stack(stack, stack_end+1);
	stack->current_index = eaSize(&stack->op_stack);

    new_command->id = stack->current_id++;

	if (stack->start_linked > -1 && eaSize(&stack->op_stack) > stack->start_linked)
	{
		stack->op_stack[eaSize(&stack->op_stack)-1]->linked_next = true;
		new_command->linked_prev = true;
//		printf("Linked Items #%d and #%d\n", stack->op_stack[eaSize(&stack->op_stack)-1]->id, new_command->id);
	}

	eaPush(&stack->op_stack, new_command);

	return new_command;
}

int EditCreateUndoSelectWithLog(EditUndoStack *stack, EditorObject** const selected_list, EditorObject** const deselected_list, const char* log)
{
    int ret = -1;
	EnterCriticalSection(&undo_critical_section);
	if (!stack->doing)
	{
		int i;
		EditUndoOperation *new_command = create_undo_command(stack, true);

		EditUndoInternalSelectionList *new_list = (EditUndoInternalSelectionList*)calloc(1, sizeof(EditUndoInternalSelectionList));
		for (i = 0; i < eaSize(&selected_list); i++)
			if (selected_list[i])
			{
				editorObjectRef(selected_list[i]);
				eaPush(&new_list->selected_list, selected_list[i]);
			}
		for (i = 0; i < eaSize(&deselected_list); i++)
			if (deselected_list[i])
			{
				editorObjectRef(deselected_list[i]);
				eaPush(&new_list->deselected_list, deselected_list[i]);
			}

		new_command->command = EditUndoSelect;
		new_command->data = new_list;
        
        ret = new_command->id;

//		printf("Added Select Undo Item #%d\n", new_command->id);
		EditDebugLogOp( stack, new_command, log );
	}
	LeaveCriticalSection(&undo_critical_section);
    return ret;
}

int EditCreateUndoTransformWithLog(EditUndoStack *stack, const Mat4 before_matrix, const Mat4 after_matrix, const char* log)
{
    int ret = -1;
	EnterCriticalSection(&undo_critical_section);
	if (!stack->doing)
	{
		Mat4 inv_matrix, delta_matrix;
		EditUndoOperation *new_command = create_undo_command(stack, true);

		invertMat4Copy(after_matrix, inv_matrix);
		mulMat4(before_matrix, inv_matrix, delta_matrix);

		new_command->command = EditUndoTransform;
		new_command->data = calloc(1, sizeof(Mat4));
		copyMat4(delta_matrix, *((Mat4*)new_command->data));

        ret = new_command->id;
        
//		printf("Added Transform Undo Item #%d\n", new_command->id);
		EditDebugLogOp( stack, new_command, log );
	}
	LeaveCriticalSection(&undo_critical_section);
    return ret;
}

int EditCreateUndoCustomWithLog(EditUndoStack *stack, EditUndoCustomFn undo_fn, EditUndoCustomFn redo_fn, EditUndoCustomFreeFn free_fn, void *data, const char* log)
{
    int ret = -1;
	EnterCriticalSection(&undo_critical_section);
	if (!stack->doing)
	{
		EditUndoOperation *new_command = create_undo_command(stack, true);
		EditUndoCustomCommand *new_custom = (EditUndoCustomCommand*)calloc(1, sizeof(EditUndoCustomCommand));
		new_custom->undo_fn = undo_fn;
		new_custom->redo_fn = redo_fn;
		new_custom->free_fn = free_fn;
		new_custom->data = data;

		new_command->command = EditUndoCustom;
		new_command->data = new_custom;

        ret = new_command->id;
        
//		printf("Added Custom Undo Item #%d\n", new_command->id);
		EditDebugLogOp( stack, new_command, log );
	}
	else
	{
		if (free_fn)
			free_fn(stack->context, data);
	}
	LeaveCriticalSection(&undo_critical_section);
    return ret;
}

int EditCreateUndoTransientWithLog(EditUndoStack *stack, EditUndoCustomFn undo_fn, EditUndoCustomFn redo_fn, EditUndoCustomFreeFn free_fn, void *data, const char* log)
{
    int ret = -1;
	EnterCriticalSection(&undo_critical_section);
	if (!stack->doing)
	{
		EditUndoOperation *new_command = create_undo_command(stack, false);
		EditUndoCustomCommand *new_custom = (EditUndoCustomCommand*)calloc(1, sizeof(EditUndoCustomCommand));
		new_custom->undo_fn = undo_fn;
		new_custom->redo_fn = redo_fn;
		new_custom->free_fn = free_fn;
		new_custom->data = data;

		new_command->command = EditUndoTransient;
		new_command->data = new_custom;

        ret = new_command->id;

//		printf("Added Transient Undo Item #%d\n", new_command->id);
		EditDebugLogOp( stack, new_command, log );
	}
	else
		free_fn(stack->context, data);
	LeaveCriticalSection(&undo_critical_section);
    return ret;
}

void EditUndoCompleteTransient(EditUndoStack *stack, int handle)
{
    EnterCriticalSection(&undo_critical_section);
    if (!stack->doing)
    {
        int i;
        bool found_transient = false;
        for (i = 0; i < eaSize(&stack->op_stack); i++)
        {
            if (stack->op_stack[i]->id == handle)
            {
                assert(!found_transient); // Transients completed out of order! Bad caller!
                if (stack->op_stack[i]->command == EditUndoTransient)
				{
					stack->op_stack[i]->command = EditUndoCustom;
					EditDebugLogOpFinished( stack, stack->op_stack[i] );
				}
                break;
            }
            else if (stack->op_stack[i]->command == EditUndoTransient)
            {
                found_transient = true;
            }
        }
    }
    LeaveCriticalSection(&undo_critical_section);
}

int EditCreateUndoStruct(EditUndoStack *stack, EditorObject *op_object, void *pre_op, void *post_op, ParseTable *tpi, EditUndoChangedFn changed_func)
{
    int ret = -1;
	EnterCriticalSection(&undo_critical_section);
	if (!stack->doing)
	{
		char *pre_to_post = NULL,  *post_to_pre = NULL;
		EditUndoOperation *new_command = create_undo_command(stack, true);
		EditUndoStructCommand *new_struct = (EditUndoStructCommand*)calloc(1, sizeof(EditUndoStructCommand));

		assert(post_op);

		estrCreate(&pre_to_post);
		StructWriteTextDiff(&pre_to_post, tpi, pre_op, post_op, 0, 0, 0, 0);
		estrCreate(&post_to_pre);
		StructWriteTextDiff(&post_to_pre, tpi, post_op, pre_op, 0, 0, 0, 0);

		if (op_object)
            editorObjectRef(op_object);

		new_struct->editor_obj = op_object;
		new_struct->obj = post_op;
		new_struct->tpi = tpi;
		new_struct->pre_to_post = pre_to_post;
		new_struct->post_to_pre = post_to_pre;
		new_struct->changed_func = changed_func;

		new_command->command = EditUndoStruct;
		new_command->data = new_struct;

        ret = new_command->id;
        
//		printf("Added Custom Struct Item #%d\n", new_command->id);
		EditDebugLogOp( stack, new_command, "" );
	}
	LeaveCriticalSection(&undo_critical_section);
    return ret;
}

EditUndoStructOp *EditCreateUndoStructBeginEO(EditUndoStack *stack, EditorObject *operand, ParseTable *tpi)
{
	return EditCreateUndoStructBegin(stack, operand, operand->obj, tpi, operand->type->changedFunc);
}

EditUndoStructOp *EditCreateUndoStructBegin(EditUndoStack *stack, EditorObject *op_object, void *operand, ParseTable *tpi, EditUndoChangedFn changed_fn)
{
	EditUndoStructOp *undo_op = calloc(1, sizeof(EditUndoStructOp));
	
	undo_op->op_object = op_object;
	undo_op->tpi = tpi;
	undo_op->changed_func = changed_fn;
	undo_op->pre_op = StructAllocVoid(tpi);
	StructCopyFieldsVoid(tpi, operand, undo_op->pre_op, 0, 0);
	undo_op->post_op = operand;

	if (op_object) editorObjectRef(op_object);

	return undo_op;
}	

void EditCreateUndoStructEnd(EditUndoStack *stack, EditUndoStructOp *undo_op, void *post_operand)
{
	if (post_operand)
		undo_op->post_op = post_operand;
	EditCreateUndoStruct(stack, undo_op->op_object, undo_op->pre_op, undo_op->post_op, undo_op->tpi, undo_op->changed_func);
	StructDestroyVoid(undo_op->tpi, undo_op->pre_op);
	if (undo_op->op_object)
        editorObjectDeref(undo_op->op_object);
	SAFE_FREE(undo_op);
}

void EditCreateUndoStructCancel(EditUndoStack *stack, EditUndoStructOp *undo_op)
{
	StructDestroyVoid(undo_op->tpi, undo_op->pre_op);
	if (undo_op->op_object)
		editorObjectDeref(undo_op->op_object);
	SAFE_FREE(undo_op);
}

bool EditCanUndoLast(EditUndoStack *stack)
{
	return (stack->current_index >= 0);
}

void EditUndoLast(EditUndoStack *stack)
{
	EditUndoOperation *op;
	EditorObject **selectionList = NULL, **deselectionList = NULL;
	
	if (stack->current_index < 0) return;

	EnterCriticalSection(&undo_critical_section);
    
	stack->start_linked = -1;
	stack->doing = true;

	do {
		op = stack->op_stack[stack->current_index];
		stack->current_index--;

//		printf("Undoing command #%d\n", op->id);
		EditDebugLogOp( stack, op, "Undo" );

		switch (op->command)
		{
		xcase EditUndoSelect:
			{
				EditUndoInternalSelectionList* list = (EditUndoInternalSelectionList*)op->data;
				//Pushing these all into arrays to make one call instead of many saves the headache of refreshing the selection n times. Refresh has n! time and is the biggest slowdown in the selection pipeline.
				if (stack->select_fn)
				{
					eaPushEArray(&selectionList, &list->selected_list);
					eaPushEArray(&deselectionList, &list->deselected_list);
				}
			}
		xcase EditUndoTransform:
			{
				Mat4 copy;
				copyMat4(*((Mat4*)op->data), copy);
				if (stack->transform_fn)
					stack->transform_fn(stack->context, copy);
			}
		xcase EditUndoStruct:
			{
				EditUndoStructCommand *struct_cmd = (EditUndoStructCommand*)op->data;
				objPathParseAndApplyOperations(struct_cmd->tpi, struct_cmd->obj, struct_cmd->post_to_pre);
				if (struct_cmd->changed_func) struct_cmd->changed_func(struct_cmd->editor_obj, struct_cmd->obj);
			}
		xcase EditUndoCustom:
			{
				EditUndoCustomCommand *custom = (EditUndoCustomCommand*)op->data;
				if (custom->undo_fn) custom->undo_fn(stack->context, custom->data);
			}
        xcase EditUndoTransient:
            {
				EditUndoCustomCommand *custom = (EditUndoCustomCommand*)op->data;
				if (custom->undo_fn) custom->undo_fn(stack->context, custom->data);
                undo_clear_stack(stack, stack->current_index+1);
            }
		};
	} while (stack->current_index >= 0 && op->linked_prev);

	if (deselectionList || selectionList)
	{
		stack->select_fn(deselectionList, selectionList);
		eaDestroy(&deselectionList);
		eaDestroy(&selectionList);
	}

	stack->doing = false;
    
	LeaveCriticalSection(&undo_critical_section);
}

bool EditCanRedoLast(EditUndoStack *stack)
{
	return (stack->current_index < eaSize(&stack->op_stack)-1);
}

void EditRedoLast(EditUndoStack *stack)
{
	EditUndoOperation *op;
	EditorObject **selectionList = NULL, **deselectionList = NULL;
	
	if (stack->current_index >= eaSize(&stack->op_stack)-1) return;
	assert(stack->op_stack); // stupid warning

	stack->start_linked = -1;
	stack->doing = true;

	EnterCriticalSection(&undo_critical_section);
    
	do {
		stack->current_index++;
		op = stack->op_stack[stack->current_index];

//		printf("Redoing command #%d\n", op->id);
		EditDebugLogOp( stack, op, "Redo" );

		switch (op->command)
		{
		xcase EditUndoSelect:
			{
				EditUndoInternalSelectionList* list = (EditUndoInternalSelectionList*)op->data;
				//Pushing these all into arrays to make one call instead of many saves the headache of refreshing the selection n times. Refresh has n! time and is the biggest slowdown in the selection pipeline.
				if (stack->select_fn)
				{
					eaPushEArray(&selectionList, &list->selected_list);
					eaPushEArray(&deselectionList, &list->deselected_list);
				}
			}
		xcase EditUndoTransform:
			{
				Mat4 inv_mat;
				invertMat4Copy(*((Mat4*)op->data), inv_mat);
				if (stack->transform_fn) stack->transform_fn(stack->context, inv_mat);
			}
		xcase EditUndoStruct:
			{
				EditUndoStructCommand *struct_cmd = (EditUndoStructCommand*)op->data;
				objPathParseAndApplyOperations(struct_cmd->tpi, struct_cmd->obj, struct_cmd->pre_to_post);
				if (struct_cmd->changed_func) struct_cmd->changed_func(struct_cmd->editor_obj, struct_cmd->obj);
			}
		xcase EditUndoCustom:
			{
				EditUndoCustomCommand *custom = (EditUndoCustomCommand*)op->data;
				if (custom->redo_fn) custom->redo_fn(stack->context, custom->data);
			}
        xcase EditUndoTransient:
            // This just won't happen
			break;
		};
	} while (stack->current_index < eaSize(&stack->op_stack)-1 && op->linked_next);

	if (deselectionList || selectionList)
	{
		stack->select_fn(selectionList, deselectionList);
		eaDestroy(&deselectionList);
		eaDestroy(&selectionList);
	}

	stack->doing = false;

	LeaveCriticalSection(&undo_critical_section);
}

void EditUndoStackClear(EditUndoStack *stack)
{
	EnterCriticalSection(&undo_critical_section);
	undo_clear_stack(stack, 0);
	eaDestroyEx(&stack->op_debug_log, EditUndoOpDestroyLog);
	stack->current_index = -1;
	stack->start_linked = -1;
	LeaveCriticalSection(&undo_critical_section);
}


void EditDebugLogOp( EditUndoStack *stack, EditUndoOperation* op, const char* log )
{
	EditUndoOperationLog* opLog = calloc( 1, sizeof( *opLog ));

	opLog->id = op->id;
	opLog->command = op->command;
	opLog->linked_next = op->linked_next;
	opLog->linked_prev = op->linked_prev;
	opLog->log = (log ? strdup( log ) : NULL);
	
	switch( op->command ) {
		xcase EditUndoStruct: {
			EditUndoStructCommand* opData = op->data;
			estrConcatf( &opLog->data_estr, "%s\n", ParserGetTableName( opData->tpi ));
			estrConcatf( &opLog->data_estr, "%s", opData->pre_to_post );
		}

		xdefault:
			estrConcatf( &opLog->data_estr, "Logging not yet implemented" );
	}


	eaPush( &stack->op_debug_log, opLog );
	while( eaSize( &stack->op_debug_log ) > 100 ) {
		EditUndoOpDestroyLog( stack->op_debug_log[ 0 ]);
		eaRemove( &stack->op_debug_log, 0 );
	}
}

void EditDebugLogOpFinished( EditUndoStack *stack, EditUndoOperation* op )
{
	EditUndoOperationLog* opLog = calloc( 1, sizeof( *opLog ));
	opLog->id = op->id;
	opLog->command = EditUndoCustom;
	opLog->linked_next = op->linked_next;
	opLog->linked_prev = op->linked_prev;
	estrConcatf( &opLog->data_estr, "(Complete)" );

	eaPush( &stack->op_debug_log, opLog );
	while( eaSize( &stack->op_debug_log ) > 100 ) {
		EditUndoOpDestroyLog( stack->op_debug_log[ 0 ]);
		eaRemove( &stack->op_debug_log, 0 );
	}
}

void EditUndoOpDestroyLog( EditUndoOperationLog* opLog )
{
	free( opLog->log );
	estrDestroy( &opLog->data_estr );
	free( opLog );
}

#endif
