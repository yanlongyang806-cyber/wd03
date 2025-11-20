/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct JobEditor JobEditor;
typedef struct AIJobDesc AIJobDesc;
typedef struct UIWindow UIWindow;

typedef void (*JobEditorChangeFunc)(JobEditor *editor, AIJobDesc *origJob, AIJobDesc *newJob, void *userData);

JobEditor *jobeditor_Create(const AIJobDesc *job, JobEditorChangeFunc changeFunc, void* userData);
void jobeditor_Destroy(JobEditor* editor);
void jobeditor_DestroyForJob(const AIJobDesc *job); // destroys all editors bound to this Job

UIWindow* jobeditor_GetWindow(JobEditor *editor);

#endif