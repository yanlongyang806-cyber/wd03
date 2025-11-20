#ifndef AIJOBS_H
#define AIJOBS_H

GCC_SYSTEM

typedef struct AIJob		AIJob;
typedef struct AIJobDesc	AIJobDesc;
typedef struct Entity		Entity;
typedef struct Expression	Expression;
typedef struct ExprContext	ExprContext;
typedef struct FSMContext	FSMContext;

AUTO_STRUCT;
typedef struct AIJob
{
	const char *filename;  NO_AST
	AIJobDesc* desc;

	EntityRef assignedBE;
	AIJob** subJobs;

	FSMContext* fsmContext;
	ExprContext* exprContext;	NO_AST
}AIJob;

AIJob* aiJobCreate(void);
void aiJobDestroy(AIJob* job);

int aiJobGenerateExpressions(AIJobDesc* desc);
AIJob* aiJobAdd(AIJob*** jobs, AIJobDesc* desc, int iPartitionIdx);

void aiJobAssign(Entity* be, AIJob* job);
void aiJobUnassign(Entity* be, AIJob* job);

void aiClearJobs(Entity* e, AIJobDesc **jobDescs);

const char* aiGetJobName(Entity* be);

#endif
