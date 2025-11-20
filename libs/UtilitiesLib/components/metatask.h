#pragma once
GCC_SYSTEM

//a metatask is a thing to do which consists of many smaller, named tasks, with dependencies between them.
//When a metatask happens, some subset of its tasks will happen, in an order which depends on the dependencies
//specified

typedef void MetaTaskCB(void);

//creates a metatask and gives it a name. (This actually doesn't have to happen before the metatask is referred to,
//but it has to happen once per task, so that misspellings of task names can be caught)
void MetaTask_Register(const char *pMetaTaskName);

//tasks associated with metatasks can be set to happen every time the metatask occurs,
//or only once ever. 
typedef enum
{
	METATASK_BEHAVIOR_ONLY_ONCE,
	METATASK_BEHAVIOR_EVERY_TIME,
} enumMetaTaskBehavior;

//adds a task to a metatask. Tasks have names that are strings, callback functions, and bool global variables which
//say whether the task is "on" or not to begin with. (Most tasks should start out "off" and be turned on only when
//another task depends on them).
//
//The dependency string is a collection of dependencies separated by commas. Each dependency is
//the word "BEFORE or "AFTER", followed by the name of the other task to depend on, followed optionally by "IFTHERE".
//
//for instance: "AFTER graphics, BEFORE textures IFTHERE, AFTER collision, CANCELLEDBY nographics"
//
//If IFTHERE is found, then the task isn't "dependent" on the other task, it just has an ordering requirement
//if the other task happens to exist. If IFTHERE is not found, then the current task depends on the other task. So,
//if the metatask decides that the current task needs to be run, then it will also have to run the task that it
//depends on
//
//CANCELLEDBY is a somewhat special case. As a first pass, before all the normal dependency tree stuff is done,
//every task that has a CANCELLEDBY checks to see if the task that it is CANCELLEBY is set to start on. If it is,
//then the cancelee asserts that it is not also set to start on, and removes all dependencies that anything has on it.
//Note that this is basically a flat operation with no order dependencies or anything. If you are set to be cancelled by
//something you are cancelled if and only if it is set to start on. It doesn't matter what that task depends on, or what
//depends on it, it only matters if it is set to start on.
void MetaTask_AddTask(const char *pMetaTaskName, const char *pTaskName, MetaTaskCB *pCB, const char *pCBName, bool bOKIfAddedMultiply,
	bool bStartsOutOn, char *pDependencyString, enumMetaTaskBehavior eBehavior);

//adds a dependency string to an already existing task
void MetaTask_AddDependencies(const char *pMetaTaskName, const char *pTaskName, char *pNewDependencies);

//removes all dependencies of all types from one task to another
void MetaTask_RemoveDependencies(const char *pMetaTaskName, const char *pTaskName, const char *pTaskNameItShouldNotDependOn);

//removes all dependences of all tasks on a single task
void MetaTask_RemoveAllDependenciesOn(const char *pMetaTask, const char *pTaskNothingShouldDependOn);

//Sets whether a task starts out on
void MetaTask_SetTaskStartsOn(const char *pMetaTaskName, const char *pTaskName, bool bStartsOutOn);

//actually execute a MetaTask
void MetaTask_DoMetaTask(const char *pMetaTaskName);



//-------------Debugging stuff-------------

//writes a C source file that calls all the metatask callback functions in the order they will be called,
//for debugging purposes
void MetaTask_WriteOutFunctionCalls(const char *pMetaTaskName, char *pOutFileName);

//writes a vizgraph file
void MetaTask_WriteOutGraphFile(const char *pMetaTaskName, char *pOutFileName);
