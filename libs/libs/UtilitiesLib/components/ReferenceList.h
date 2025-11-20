#ifndef _REFERENCELIST_H
#define _REFERENCELIST_H
#pragma once
GCC_SYSTEM

typedef struct ReferenceListImp ReferenceListImp;
typedef ReferenceListImp *ReferenceList;
typedef unsigned long Reference;

ReferenceList createReferenceList(void);
void destroyReferenceList(__deref ReferenceList list);

Reference referenceListAddElement(__deref ReferenceList list, void *data);
void *referenceListFindByRef(__deref ReferenceList list, Reference ref);
void referenceListRemoveElement(__deref ReferenceList list, Reference ref);
void referenceListMoveElement(__deref ReferenceList list, Reference rdst, Reference rsrc);

#endif