#ifndef __EDITLIBUNDO_H__
#define __EDITLIBUNDO_H__
GCC_SYSTEM

#ifndef NO_EDITORS

typedef enum EditorObjectTypeEnum EditorObjectTypeEnum;
typedef struct EditorObject EditorObject;

typedef void (*EditUndoSelectFn)(EditorObject** const selected_list, EditorObject** const deselected_list);
typedef void (*EditUndoTransformFn)(void *context, const Mat4 matrix);
typedef void (*EditUndoCustomFn)(void *context, void *data);
typedef void (*EditUndoCustomFreeFn)(void *context, void *data);
typedef void (*EditUndoChangedFn)(EditorObject*, void *);

typedef struct EditUndoStack EditUndoStack;
typedef struct EditUndoStructOp EditUndoStructOp;

EditUndoStack* EditUndoStackCreate();
void EditUndoStackDestroy(EditUndoStack* stack);

void EditUndoSetContext(EditUndoStack *stack, void *context);
void EditUndoSetSelectFn(EditUndoStack *stack, EditUndoSelectFn fn);
void EditUndoSetTransformFn(EditUndoStack *stack, EditUndoTransformFn fn);

void EditUndoPrintToBuffer(const EditUndoStack *stack, char* buffer, int buffer_size);

// Signal the begin and end of a linked set of operations.
// Can be nested - be sure to call EndGroup once for each BeginGroup!
void EditUndoBeginGroup(SA_PARAM_OP_VALID EditUndoStack *stack); 
void EditUndoEndGroup(SA_PARAM_OP_VALID EditUndoStack *stack);

#define EditCreateUndoSelect(stack,selected_list,deselected_list) EditCreateUndoSelectWithLog(stack,selected_list,deselected_list,"Unsupported Editor - "__FILE__)
#define EditCreateUndoTransform(stack,before_matrix,after_matrix) EditCreateUndoTransformWithLog(stack,before_matrix,after_matrix,"Unsupported Editor - "__FILE__)
#define EditCreateUndoCustom(stack,undo_fn,redo_fn,free_fn,data) EditCreateUndoCustomWithLog(stack,undo_fn,redo_fn,free_fn,data,"Unsupported Editor - "__FILE__)
#define EditCreateUndoTransient(stack,undo_fn,redo_fn,free_fn,data) EditCreateUndoTransientWithLog(stack,undo_fn,redo_fn,free_fn,data,"Unsupported Editor - "__FILE__)

int EditCreateUndoSelectWithLog(EditUndoStack *stack, EditorObject** const selected_list, EditorObject** const deselected_list, const char* log);
int EditCreateUndoTransformWithLog(EditUndoStack *stack, const Mat4 before_matrix, const Mat4 after_matrix, const char* log);
int EditCreateUndoCustomWithLog(EditUndoStack *stack, EditUndoCustomFn undo_fn, EditUndoCustomFn redo_fn, EditUndoCustomFreeFn free_fn, void *data, const char* log);
int EditCreateUndoTransientWithLog(EditUndoStack *stack, EditUndoCustomFn undo_fn, EditUndoCustomFn redo_fn, EditUndoCustomFreeFn free_fn, void *data, const char* log);

// Mark a transient operation as "completed", changing it into a Custom Op.
void EditUndoCompleteTransient(EditUndoStack *stack, int handle);

// Example usage 1: (Undo/Redo will happen on mystruct)
// EditUndoStructOp *op = EditCreateUndoStructBegin(stack, mystruct, parse_MyStruct);
// mystruct->obj->foo = bar;
// EditCreateUndoStructEnd(stack, op, NULL);

// Example usage 2: (Undo/Redo will happen on mystruct_new)
// EditUndoStructOp *op = EditCreateUndoStructBegin(stack, mystruct_old, parse_MyStruct);
// EditCreateUndoStructEnd(stack, op, mystruct_new);

int EditCreateUndoStruct(EditUndoStack *stack, EditorObject *op_object, void *pre_op, void *post_op, ParseTable *tpi, EditUndoChangedFn changed_func);
EditUndoStructOp *EditCreateUndoStructBegin(EditUndoStack *stack, EditorObject *op_object, void *operand, ParseTable *tpi, EditUndoChangedFn changed_fn);
void EditCreateUndoStructEnd(EditUndoStack *stack, EditUndoStructOp *undo_op, void *new_object);
void EditCreateUndoStructCancel(EditUndoStack *stack, EditUndoStructOp *undo_op); // Free undo_op and don't create an operation

// Wrapper for editor objects
EditUndoStructOp *EditCreateUndoStructBeginEO(EditUndoStack *stack, EditorObject *operand, ParseTable *tpi);

bool EditCanUndoLast(EditUndoStack *stack);
bool EditCanRedoLast(EditUndoStack *stack);

void EditUndoLast(EditUndoStack *stack);
void EditRedoLast(EditUndoStack *stack);

void EditUndoStackClear(EditUndoStack *stack);

#endif // NO_EDITORS

#endif // __EDITLIBUNDO_H__
