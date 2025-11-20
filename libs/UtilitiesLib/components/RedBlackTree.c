// reentrant red-black tree

#include <stdlib.h>
#include "RedBlackTree.h"
#include "ThreadSafeMemoryPool.h"
#include "qsortG.h"
#include "textparser.h"
#include "Textparsercallbacks_inline.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

//#define PER_TREE_MEMPOOL

typedef enum NodeColor {
	BLACK,
	RED
} NodeColor;

typedef struct RedBlackTreeNode RedBlackTreeNode;

typedef struct RedBlackTreeNode {
    RedBlackTreeNode *left;       // left child
    RedBlackTreeNode *right;      // right child
    RedBlackTreeNode *parent;     // parent
    NodeColor color;      // node color (BLACK, RED)
    void *key;            // key used for searching
    void *val;            // user data
} RedBlackTreeNode;

typedef struct RedBlackTree {
    RedBlackTreeNode *root;   // root of red-black tree
    RedBlackTreeNode sentinel;
    int (*compare)(void *a, void *b);    // compare keys
	int nodeCount;
	MEM_DBG_STRUCT_PARMS
#ifdef PER_TREE_MEMPOOL
	MemoryPool mpNodes;
#endif
} RedBlackTree;

// all leafs are sentinels
#define SENTINEL &rbt->sentinel

#ifndef PER_TREE_MEMPOOL
TSMP_DEFINE(RedBlackTreeNode);

AUTO_RUN_FIRST;
void rbtInitMemPool(void)
{
	TSMP_CREATE(RedBlackTreeNode, 256);
}
#endif

RedBlackTree *rbtNew_dbg(int(*rbtCompare)(void *a, void *b) MEM_DBG_PARMS) {
    RedBlackTree *rbt;
    
    if ((rbt = (RedBlackTree *)smalloc(sizeof(RedBlackTree))) == NULL) {
        return NULL;
    }

	MEM_DBG_STRUCT_PARMS_INIT(rbt);

#ifdef PER_TREE_MEMPOOL
	rbt->mpNodes = createMemoryPool_dbg(MEM_DBG_PARMS_CALL_VOID);
	initMemoryPool_dbg(rbt->mpNodes, sizeof(RedBlackTreeNode), 128 MEM_DBG_PARMS_CALL);
#endif

    rbt->compare = rbtCompare;
    rbt->root = SENTINEL;
    rbt->sentinel.left = SENTINEL;
    rbt->sentinel.right = SENTINEL;
    rbt->sentinel.parent = NULL;
    rbt->sentinel.color = BLACK;
    rbt->sentinel.key = NULL;
    rbt->sentinel.val = NULL;
	rbt->nodeCount = 0;

    return rbt;
}

static void deleteTree(RedBlackTree *h, RedBlackTreeNode *p, Destructor keyDestructor, Destructor valueDestructor) {
    RedBlackTree *rbt = h;

    // erase nodes depth-first
    if (p == SENTINEL)
		return;
	if (keyDestructor && p->key)
		keyDestructor(p->key);
	if (valueDestructor && p->val)
		valueDestructor(p->val);
    deleteTree(h, p->left, keyDestructor, valueDestructor);
    deleteTree(h, p->right, keyDestructor, valueDestructor);
#ifdef PER_TREE_MEMPOOL
    mpFree_dbg(rbt->mpNodes, p MEM_DBG_STRUCT_PARMS_CALL(rbt));
#else
	TSMP_FREE(RedBlackTreeNode, p);
	//free(p);
#endif
}

void rbtDelete(RedBlackTree *h, Destructor keyDestructor, Destructor valueDestructor) {
    RedBlackTree *rbt = h;

#ifdef PER_TREE_MEMPOOL
	assert(mpGetAllocatedCount(rbt->mpNodes) == (size_t)rbt->nodeCount);
#endif
    deleteTree(h, rbt->root, keyDestructor, valueDestructor);
#ifdef PER_TREE_MEMPOOL
	destroyMemoryPool(rbt->mpNodes);
	rbt->mpNodes = NULL;
#endif
    free(rbt);
}



static void deleteTreeEx(RedBlackTree *h, RedBlackTreeNode *p, DestructorWithUserData keyDestructor, DestructorWithUserData valueDestructor, void *pUserData) {
    RedBlackTree *rbt = h;

    // erase nodes depth-first
    if (p == SENTINEL)
		return;
	if (keyDestructor && p->key)
		keyDestructor(p->key, pUserData);
	if (valueDestructor && p->val)
		valueDestructor(p->val, pUserData);
    deleteTreeEx(h, p->left, keyDestructor, valueDestructor, pUserData);
    deleteTreeEx(h, p->right, keyDestructor, valueDestructor, pUserData);
#ifdef PER_TREE_MEMPOOL
    mpFree_dbg(rbt->mpNodes, p MEM_DBG_STRUCT_PARMS_CALL(rbt));
#else
	TSMP_FREE(RedBlackTreeNode, p);
	//free(p);
#endif
}

void rbtDeleteEx(RedBlackTree *h, DestructorWithUserData keyDestructor, DestructorWithUserData valueDestructor, void *pUserData) {
    RedBlackTree *rbt = h;

#ifdef PER_TREE_MEMPOOL
	assert(mpGetAllocatedCount(rbt->mpNodes) == (size_t)rbt->nodeCount);
#endif
    deleteTreeEx(h, rbt->root, keyDestructor, valueDestructor, pUserData);
#ifdef PER_TREE_MEMPOOL
	destroyMemoryPool(rbt->mpNodes);
	rbt->mpNodes = NULL;
#endif
    free(rbt);
}


static void rotateLeft(RedBlackTree *rbt, RedBlackTreeNode *x) {

    // rotate node x to left

    RedBlackTreeNode *y = x->right;

    // establish x->right link
    x->right = y->left;
    if (y->left != SENTINEL) y->left->parent = x;

    // establish y->parent link
    if (y != SENTINEL) y->parent = x->parent;
    if (x->parent) {
        if (x == x->parent->left)
            x->parent->left = y;
        else
            x->parent->right = y;
    } else {
        rbt->root = y;
    }

    // link x and y
    y->left = x;
    if (x != SENTINEL) x->parent = y;
}

static void rotateRight(RedBlackTree *rbt, RedBlackTreeNode *x) {

    // rotate node x to right

    RedBlackTreeNode *y = x->left;

    // establish x->left link
    x->left = y->right;
    if (y->right != SENTINEL) y->right->parent = x;

    // establish y->parent link
    if (y != SENTINEL) y->parent = x->parent;
    if (x->parent) {
        if (x == x->parent->right)
            x->parent->right = y;
        else
            x->parent->left = y;
    } else {
        rbt->root = y;
    }

    // link x and y
    y->right = x;
    if (x != SENTINEL) x->parent = y;
}

static void insertFixup(RedBlackTree *rbt, RedBlackTreeNode *x) {

    // maintain red-black tree balance after inserting node x

    // check red-black properties
    while (x != rbt->root && x->parent->color == RED) {
        // we have a violation
        if (x->parent == x->parent->parent->left) {
            RedBlackTreeNode *y = x->parent->parent->right;
            if (y->color == RED) {

                // uncle is RED
                x->parent->color = BLACK;
                y->color = BLACK;
                x->parent->parent->color = RED;
                x = x->parent->parent;
            } else {

                // uncle is BLACK
                if (x == x->parent->right) {
                    // make x a left child
                    x = x->parent;
                    rotateLeft(rbt, x);
                }

                // recolor and rotate
                x->parent->color = BLACK;
                x->parent->parent->color = RED;
                rotateRight(rbt, x->parent->parent);
            }
        } else {

            // mirror image of above code
            RedBlackTreeNode *y = x->parent->parent->left;
            if (y->color == RED) {

                // uncle is RED
                x->parent->color = BLACK;
                y->color = BLACK;
                x->parent->parent->color = RED;
                x = x->parent->parent;
            } else {

                // uncle is BLACK
                if (x == x->parent->left) {
                    x = x->parent;
                    rotateRight(rbt, x);
                }
                x->parent->color = BLACK;
                x->parent->parent->color = RED;
                rotateLeft(rbt, x->parent->parent);
            }
        }
    }
    rbt->root->color = BLACK;
}

RbtStatus rbtInsertEx(RedBlackTree *h, void *key, void *val, bool bOverWrite, RedBlackTreeNode **ppNode, void **ppOverwrittenValue) {
    RedBlackTreeNode *current, *parent, *x;
    RedBlackTree *rbt = h;

    // allocate node for data and insert in tree

    // find future parent
    current = rbt->root;
    parent = 0;
    while (current != SENTINEL) {
        int rc = rbt->compare(key, current->key);
        if (rc == 0)
		{
			if (bOverWrite)
			{
				if (ppOverwrittenValue)
				{
					*ppOverwrittenValue = current->val;
				}

				current->val = val;

				if (ppNode)
				{
					*ppNode = current;
				}

				return RBT_STATUS_OVERWROTE;
			}

            return RBT_STATUS_DUPLICATE_KEY;
		}
        parent = current;
        current = (rc < 0) ? current->left : current->right;
    }

    // setup new node
#ifdef PER_TREE_MEMPOOL
	x = mpAlloc_dbg(rbt->mpNodes, 0 MEM_DBG_STRUCT_PARMS_CALL(rbt));
#else
	//x = stmalloc(sizeof(*x), rbt);
	x = TSMP_ALLOC(RedBlackTreeNode);
#endif
    x->parent = parent;
    x->left = SENTINEL;
    x->right = SENTINEL;
    x->color = RED;
    x->key = key;
    x->val = val;

    // insert node in tree
    if(parent) {
        if(rbt->compare(key, parent->key) < 0)
            parent->left = x;
        else
            parent->right = x;
    } else {
        rbt->root = x;
    }

    insertFixup(rbt, x);

	rbt->nodeCount++;

	if (ppNode)
	{
		*ppNode = x;
	}

    return RBT_STATUS_OK;
}

void deleteFixup(RedBlackTree *rbt, RedBlackTreeNode *x) {

    // maintain red-black tree balance after deleting node x

    while (x != rbt->root && x->color == BLACK) {
        if (x == x->parent->left) {
            RedBlackTreeNode *w = x->parent->right;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                rotateLeft (rbt, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == BLACK && w->right->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->right->color == BLACK) {
                    w->left->color = BLACK;
                    w->color = RED;
                    rotateRight (rbt, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->right->color = BLACK;
                rotateLeft (rbt, x->parent);
                x = rbt->root;
            }
        } else {
            RedBlackTreeNode *w = x->parent->left;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                rotateRight (rbt, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == BLACK && w->left->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->left->color == BLACK) {
                    w->right->color = BLACK;
                    w->color = RED;
                    rotateLeft (rbt, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->left->color = BLACK;
                rotateRight (rbt, x->parent);
                x = rbt->root;
            }
        }
    }
    x->color = BLACK;
}

RbtStatus rbtErase(RedBlackTree *h, RbtIterator i) {
    RedBlackTreeNode *x, *y;
    RedBlackTree *rbt = h;
    RedBlackTreeNode *z = i;

    if (z->left == SENTINEL || z->right == SENTINEL) {
        // y has a SENTINEL node as a child
        y = z;
    } else {
        // find tree successor with a SENTINEL node as a child
        y = z->right;
        while (y->left != SENTINEL) y = y->left;
    }

    // x is y's only child
    if (y->left != SENTINEL)
        x = y->left;
    else
        x = y->right;

    // remove y from the parent chain
    x->parent = y->parent;
    if (y->parent)
        if (y == y->parent->left)
            y->parent->left = x;
        else
            y->parent->right = x;
    else
        rbt->root = x;

    if (y != z) {
        z->key = y->key;
        z->val = y->val;
    }


    if (y->color == BLACK)
        deleteFixup (rbt, x);

#ifdef PER_TREE_MEMPOOL
	mpFree_dbg(rbt->mpNodes, y MEM_DBG_STRUCT_PARMS_CALL(rbt));
#else
	TSMP_FREE(RedBlackTreeNode, y);
	//free(y);
#endif

	rbt->nodeCount--;

    return RBT_STATUS_OK;
}

RbtIterator rbtNext(RedBlackTree *h, RbtIterator it) {
    RedBlackTree *rbt = h;
    RedBlackTreeNode *i = it;

    if (i->right != SENTINEL) {
        // go right 1, then left to the end
        for (i = i->right; i->left != SENTINEL; i = i->left);
    } else {
        // while you're the right child, chain up parent link
        RedBlackTreeNode *p = i->parent;
        while (p && i == p->right) {
            i = p;
            p = p->parent;
        }

        // return the "inorder" node
        i = p;
    }
    return i != SENTINEL ? i : NULL;
}

RbtIterator rbtPrev(RedBlackTree *h, RbtIterator it) {
	RedBlackTree *rbt = h;
	RedBlackTreeNode *i = it;

	if (it == NULL) // End
	{
		for (i = h->root; i->right != SENTINEL; i = i->right);
	} else {
		if (i->left != SENTINEL) {
			// go left 1, then rightto the end
			for (i = i->left; i->right != SENTINEL; i = i->right);
		} else {
			// while you're the left child, chain up parent link
			RedBlackTreeNode *p = i->parent;
			while (p && i == p->left) {
				i = p;
				p = p->parent;
			}

			// return the "inorder" node
			i = p;
		}
	}
	return i != SENTINEL ? i : NULL;
}

RbtIterator rbtBegin(RedBlackTree *h) {
    RedBlackTree *rbt = h;

    // return pointer to first value
    RedBlackTreeNode *i;
    for (i = rbt->root; i->left != SENTINEL; i = i->left);
    return i != SENTINEL ? i : NULL;
}

RbtIterator rbtEnd(RedBlackTree *h) {
   // return pointer to one past last value
   return NULL;
}

void rbtKeyValue(RedBlackTree *h, RbtIterator it, void **key, void **val) {
    RedBlackTreeNode *i = it;

	if (key)
		*key = i->key;
	if (val)
		*val = i->val;
}


RbtIterator rbtFind(RedBlackTree *h, void *key) {
    RedBlackTree *rbt = h;

    RedBlackTreeNode *current;
    current = rbt->root;
    while(current != SENTINEL) {
        int rc = rbt->compare(key, current->key);
        if (rc == 0) return current;
        current = (rc < 0) ? current->left : current->right;
    }
    return NULL;
}

RbtIterator rbtFindGTE(RedBlackTree *h, void *key) {
	int rc;
	RedBlackTree *rbt = h;
	RedBlackTreeNode *current;
	RedBlackTreeNode *best=NULL;
	current = rbt->root;
	while(current != SENTINEL) {
		best = current;
		rc = rbt->compare(key, current->key);
		if (rc == 0)
			return current;
		current = (rc < 0) ? current->left : current->right;
	}
	if (best) {
		rc = rbt->compare(key, best->key);
		if (rc < 0)
			return best;
		else {
			best = rbtNext(h, best);
			assert(!best || rbt->compare(key, best->key) < 0);
			return best;
		}
	}
	return NULL;
}





static void freeFunc(void *pData)
{
	free(pData);
}



int intCompare(void *a, void *b)
{
	int intA = INT_FROM_KEY(a);
	int intB = INT_FROM_KEY(b);
	
	if (intB > intA)
	{
		return -1;
	}

	if (intB < intA)
	{
		return 1;
	}

	return 0;
}

int U32Compare(void *a, void *b)
{
	U32 intA = U32_FROM_KEY(a);
	U32 intB = U32_FROM_KEY(b);
	
	if (intB > intA)
	{
		return -1;
	}

	if (intB < intA)
	{
		return 1;
	}

	return 0;
}

int stringCompare(char *pStr1, char *pStr2)
{
	return stricmp(pStr1, pStr2);
}


#define ADDPOINTER_RETURN { switch (eStatus) { case RBT_STATUS_OK: pTree->iCount++; return true; case RBT_STATUS_OVERWROTE: return true; } return false; }


CrypticRedBlackTree *CrypticRedBlackTree_Create_IntKeys(void)
{
	CrypticRedBlackTree *pCrypticTree = calloc(sizeof(CrypticRedBlackTree), 1);
	pCrypticTree->eType = REDBLACKTREE_INT;
	pCrypticTree->pTree = rbtNew(intCompare);

	return pCrypticTree;
}


bool CrypticRedBlackTree_Int_AddPointer(CrypticRedBlackTree *pTree, int iKey, void *pValue, bool bOverwrite)
{
	RbtStatus eStatus;
	void *pKey = KEY_FROM_INT(iKey);

	assert(pTree && pTree->eType == REDBLACKTREE_INT);

	eStatus = rbtInsertEx(pTree->pTree, pKey, pValue, bOverwrite, NULL, NULL);

	ADDPOINTER_RETURN;
}

bool CrypticRedBlackTree_Int_FindPointer(CrypticRedBlackTree *pTree, int iKey, void **ppFound)
{
	void *pKey = KEY_FROM_INT(iKey);
	RbtIterator pFound;

	if (!pTree)
	{
		return false;
	}

	assert(pTree->eType == REDBLACKTREE_INT);

	pFound = rbtFind(pTree->pTree, pKey);

	if (!pFound)
	{
		return false;
	}

	if (ppFound)
	{
		*ppFound = ((RedBlackTreeNode*)pFound)->val;
	}

	return true;
}

bool CrypticRedBlackTree_Int_RemovePointer(CrypticRedBlackTree *pTree, int iKey, void **ppFound)
{
	void *pKey = KEY_FROM_INT(iKey);
	RbtIterator pFound;
		
	if (!pTree)
	{
		return false;
	}

	assert(pTree->eType == REDBLACKTREE_INT);

	pFound = rbtFind(pTree->pTree, pKey);

	if (!pFound)
	{
		return false;
	}

	if (ppFound)
	{
		*ppFound = ((RedBlackTreeNode*)pFound)->val;
	}

	pTree->iCount--;
	rbtErase(pTree->pTree, pFound);

	return true;
}

void CrypticRedBlackTree_Int_Destroy(CrypticRedBlackTree **ppTree)
{
	if (!ppTree || !(*ppTree))
	{
		return;
	}

	assert((*ppTree)->eType == REDBLACKTREE_INT);

	rbtDelete((*ppTree)->pTree, NULL, NULL);
	free(*ppTree);
	*ppTree = NULL;
}

void CrypticRedBlackTree_Int_DestroyEx(CrypticRedBlackTree **ppTree, Destructor valDestructor)
{
	if (!ppTree || !(*ppTree))
	{
		return;
	}
	assert((*ppTree)->eType == REDBLACKTREE_INT);
	rbtDelete((*ppTree)->pTree, NULL, valDestructor ? valDestructor : freeFunc);
	free(*ppTree);
	*ppTree = NULL;

}





CrypticRedBlackTree *CrypticRedBlackTree_Create_U32Keys(void)
{
	CrypticRedBlackTree *pCrypticTree = calloc(sizeof(CrypticRedBlackTree), 1);
	pCrypticTree->eType = REDBLACKTREE_U32;
	pCrypticTree->pTree = rbtNew(U32Compare);

	return pCrypticTree;
}


bool CrypticRedBlackTree_U32_AddPointer(CrypticRedBlackTree *pTree, U32 iKey, void *pValue, bool bOverwrite)
{
	RbtStatus eStatus;
	void *pKey = KEY_FROM_U32(iKey);

	assert(pTree && pTree->eType == REDBLACKTREE_U32);

	eStatus = rbtInsertEx(pTree->pTree, pKey, pValue, bOverwrite, NULL, NULL);

	ADDPOINTER_RETURN;
}

bool CrypticRedBlackTree_U32_FindPointer(CrypticRedBlackTree *pTree, U32 iKey, void **ppFound)
{
	void *pKey = KEY_FROM_U32(iKey);
	RbtIterator pFound;

	if (!pTree)
	{
		return false;
	}

	assert(pTree->eType == REDBLACKTREE_U32);

	pFound = rbtFind(pTree->pTree, pKey);

	if (!pFound)
	{
		return false;
	}

	if (ppFound)
	{
		*ppFound = ((RedBlackTreeNode*)pFound)->val;
	}

	return true;
}

bool CrypticRedBlackTree_U32_RemovePointer(CrypticRedBlackTree *pTree, U32 iKey, void **ppFound)
{
	void *pKey = KEY_FROM_U32(iKey);
	RbtIterator pFound;
		
	if (!pTree)
	{
		return false;
	}

	assert(pTree->eType == REDBLACKTREE_U32);
	pFound = rbtFind(pTree->pTree, pKey);

	if (!pFound)
	{
		return false;
	}

	if (ppFound)
	{
		*ppFound = ((RedBlackTreeNode*)pFound)->val;
	}

	pTree->iCount--;
	rbtErase(pTree->pTree, pFound);

	return true;
}

void CrypticRedBlackTree_U32_Destroy(CrypticRedBlackTree **ppTree)
{
	if (!ppTree || !(*ppTree))
	{
		return;
	}
	assert((*ppTree)->eType == REDBLACKTREE_U32);
	rbtDelete((*ppTree)->pTree, NULL, NULL);
	free(*ppTree);
	*ppTree = NULL;
}

void CrypticRedBlackTree_U32_DestroyEx(CrypticRedBlackTree **ppTree, Destructor valDestructor)
{
	if (!ppTree || !(*ppTree))
	{
		return;
	}
	assert((*ppTree)->eType == REDBLACKTREE_U32);
	rbtDelete((*ppTree)->pTree, NULL, valDestructor ? valDestructor : freeFunc);
	free(*ppTree);
	*ppTree = NULL;

}






CrypticRedBlackTree *CrypticRedBlackTree_Create_StringKeys(bool bInternallyCopyKeys)
{
	CrypticRedBlackTree *pCrypticTree = calloc(sizeof(CrypticRedBlackTree), 1);
	pCrypticTree->eType = REDBLACKTREE_STRING;
	pCrypticTree->bCopyStringKeys = bInternallyCopyKeys;
	pCrypticTree->pTree = rbtNew(stringCompare);

	return pCrypticTree;
}


bool CrypticRedBlackTree_String_AddPointer(CrypticRedBlackTree *pTree, char *pKey, void *pValue, bool bOverwrite)
{
	RbtStatus eStatus;
	RedBlackTreeNode *pNode;

	assert(pTree && pTree->eType == REDBLACKTREE_STRING && pKey);

	eStatus = rbtInsertEx(pTree->pTree, pKey, pValue, bOverwrite, &pNode, NULL);

	switch (eStatus)
	{
	case RBT_STATUS_OK:
		//added a new node
		if (pTree->bCopyStringKeys)
		{
			pNode->key = strdup(pNode->key);
		}

		pTree->iCount++;
		return true;

	case RBT_STATUS_OVERWROTE:
		//node already existed... whether in dup keys case or not, we don't need to do anything, because the key in that node
		//either was or was not already duped
		return true;
	}

	return false;
}

bool CrypticRedBlackTree_String_FindPointer(CrypticRedBlackTree *pTree, char *pKey, void **ppFound)
{
	RbtIterator pFound;

	if (!pTree)
	{
		return false;
	}

	assert(pTree->eType == REDBLACKTREE_STRING && pKey);

	pFound = rbtFind(pTree->pTree, pKey);

	if (!pFound)
	{
		return false;
	}

	if (ppFound)
	{
		*ppFound = ((RedBlackTreeNode*)pFound)->val;
	}

	return true;
}

bool CrypticRedBlackTree_String_RemovePointer(CrypticRedBlackTree *pTree, char *pKey, void **ppFound)
{
	RbtIterator pFound;
	char *pFoundKey;
		
	if (!pTree)
	{
		return false;
	}

	assert(pTree->eType == REDBLACKTREE_STRING && pKey);
	pFound = rbtFind(pTree->pTree, pKey);

	if (!pFound)
	{
		return false;
	}

	if (ppFound)
	{
		*ppFound = ((RedBlackTreeNode*)pFound)->val;
	}

	pTree->iCount--;

	pFoundKey = pFound->key;
	rbtErase(pTree->pTree, pFound);

	if (pTree->bCopyStringKeys)
	{
		free(pFoundKey);
	}

	return true;
}

void CrypticRedBlackTree_String_Destroy(CrypticRedBlackTree **ppTree)
{
	if (!ppTree || !(*ppTree))
	{
		return;
	}
	assert((*ppTree)->eType == REDBLACKTREE_STRING);
	rbtDelete((*ppTree)->pTree, ((*ppTree)->bCopyStringKeys) ? freeFunc : NULL, NULL);
	free(*ppTree);
	*ppTree = NULL;
}

void CrypticRedBlackTree_String_DestroyEx(CrypticRedBlackTree **ppTree, Destructor valDestructor)
{
	if (!ppTree || !(*ppTree))
	{
		return;
	}
	assert((*ppTree)->eType == REDBLACKTREE_STRING);
	rbtDelete((*ppTree)->pTree, ((*ppTree)->bCopyStringKeys) ? freeFunc : NULL, valDestructor ? valDestructor : freeFunc);
	free(*ppTree);
	*ppTree = NULL;

}






CrypticRedBlackTree *CrypticRedBlackTree_Create_StringKeyedStructs(ParseTable *pTPI)
{
	CrypticRedBlackTree *pCrypticTree = calloc(sizeof(CrypticRedBlackTree), 1);
	pCrypticTree->eType = REDBLACKTREE_STRINGKEYEDSTRUCT;
	pCrypticTree->pTPI = pTPI;
	pCrypticTree->pTree = rbtNew(stringCompare);

	return pCrypticTree;
}


bool CrypticRedBlackTree_StringKeyedStruct_AddStruct(CrypticRedBlackTree *pTree, void *pStruct, bool bOverwrite, bool bDestroyOnOverwrite)
{
	RbtStatus eStatus;
	void *pOldStruct;
	char *pKey;
	RedBlackTreeNode *pNode;

	assert(pTree && pTree->eType == REDBLACKTREE_STRINGKEYEDSTRUCT && pStruct);

	pKey = ParserGetInPlaceKeyString_inline(pTree->pTPI, pStruct);

	assert(pKey);

	eStatus = rbtInsertEx(pTree->pTree, pKey, pStruct, bOverwrite, &pNode, &pOldStruct);

	switch (eStatus)
	{
	case RBT_STATUS_OK:
		pTree->iCount++;

		return true;

	case RBT_STATUS_OVERWROTE:

		//replace the pointer to the old struct's key with a pointer to the new struct's key... they're the same string, but
		//might be different pointers
		pNode->key = pKey;

		if (bDestroyOnOverwrite)
		{
			StructDestroyVoid(pTree->pTPI, pOldStruct);
		}
			
		return true;
	}

	return false;
}

bool CrypticRedBlackTree_StringKeyedStruct_FindStruct(CrypticRedBlackTree *pTree, char *pKey, void **ppFound)
{
	RbtIterator pFound;

	if (!pTree)
	{
		return false;
	}

	assert(pTree->eType == REDBLACKTREE_STRINGKEYEDSTRUCT && pKey);

	pFound = rbtFind(pTree->pTree, pKey);

	if (!pFound)
	{
		return false;
	}

	if (ppFound)
	{
		*ppFound = ((RedBlackTreeNode*)pFound)->val;
	}

	return true;
}

bool CrypticRedBlackTree_StringKeyedStruct_RemoveStruct(CrypticRedBlackTree *pTree, char *pKey, void **ppFound)
{
	RbtIterator pFound;
	char *pFoundKey;
		
	if (!pTree)
	{
		return false;
	}

	assert(pTree->eType == REDBLACKTREE_STRINGKEYEDSTRUCT && pKey);
	pFound = rbtFind(pTree->pTree, pKey);

	if (!pFound)
	{
		return false;
	}

	if (ppFound)
	{
		*ppFound = ((RedBlackTreeNode*)pFound)->val;
	}

	pTree->iCount--;

	pFoundKey = pFound->key;
	rbtErase(pTree->pTree, pFound);

	return true;
}

bool CrypticRedBlackTree_StringKeyedStruct_RemoveAndDestroyStruct(CrypticRedBlackTree *pTree, char *pKey)
{
	void *pStruct;

	if (CrypticRedBlackTree_StringKeyedStruct_RemoveStruct(pTree, pKey, &pStruct))
	{
		StructDestroyVoid(pTree->pTPI, pStruct);
		return true;
	}

	return false;
}

static void StructDestroyCB(void *pData, ParseTable *pTPI)
{
	StructDestroyVoid(pTPI, pData);
}

void CrypticRedBlackTree_StringKeyedStruct_Destroy(CrypticRedBlackTree **ppTree, bool bDestroyStructs)
{
	if (!ppTree || !(*ppTree))
	{
		return;
	}
	assert((*ppTree)->eType == REDBLACKTREE_STRINGKEYEDSTRUCT);

	if (bDestroyStructs)
	{
		rbtDeleteEx((*ppTree)->pTree, NULL, StructDestroyCB, (*ppTree)->pTPI);
	}
	else
	{
		rbtDelete((*ppTree)->pTree, NULL, NULL);

	}
	free(*ppTree);
	*ppTree = NULL;
}






typedef struct rbtTestStruct
{
	char key[32];
	int i;
} rbtTestStruct;

rbtTestStruct *makeTestStruct(char *pKey, int i)
{
	rbtTestStruct *pStruct = calloc(sizeof(rbtTestStruct), 1);
	strcpy(pStruct->key, pKey);
	pStruct->i = i;

	return pStruct;
}


#if 0

#include "rand.h"
#include "redBlackTree_c_Ast.h"

AUTO_STRUCT;
typedef struct keyedTestStruct
{
	char key[32]; AST(KEY)
	int iTest;
} keyedTestStruct;

static int iCount = 0;

AUTO_FIXUPFUNC;
TextParserResult fixupkeyedTestStruct(keyedTestStruct* pStruct, enumTextParserFixupType eType, void *pExtraData)
{
	switch(eType)
	{
		xcase FIXUPTYPE_DESTRUCTOR: 
			iCount--;

		xcase FIXUPTYPE_CONSTRUCTOR:
			iCount++;


			sprintf(pStruct->key, "%c%c%c", randomIntRange('a', 'z'), randomIntRange('a','z'), randomIntRange('a', 'z'));
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void RBT_Test(void)
{
	int i;

	while (1)
	{
		CrypticRedBlackTree *pTree = CrypticRedBlackTree_Create_StringKeyedStructs(parse_keyedTestStruct);

		for (i = 0; i < 100000; i++)
		{
			CrypticRedBlackTree_StringKeyedStruct_AddStruct(pTree, StructCreate(parse_keyedTestStruct), true, true);

			if (randomIntRange(0,100) < 3)
			{
				char tempKey[10];
				sprintf(tempKey, "%c%c%c",  randomIntRange('a', 'z'), randomIntRange('a','z'), randomIntRange('a', 'z'));
				CrypticRedBlackTree_StringKeyedStruct_RemoveAndDestroyStruct(pTree, tempKey);
			}
		}

		FOR_EACH_IN_CRYPTICREDBLACKTREE_STRING(pTree, pKey, keyedTestStruct, pStruct)
		{
			assert(pKey == pStruct->key);
			printf("%s\n", pKey);
		}
		FOR_EACH_END;

		CrypticRedBlackTree_StringKeyedStruct_Destroy(&pTree, true);

		assert(iCount == 0);
	}
}
#include "redBlackTree_c_Ast.c"
#endif

