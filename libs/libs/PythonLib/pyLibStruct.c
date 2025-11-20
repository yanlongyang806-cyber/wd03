#include "pyLib.h"
#include "pyLibStruct.h"

#include "earray.h"
#include "textparser.h"
#include "timing_profiler.h"
#include "tokenstore.h"

typedef struct PyLibStructState
{
	PyObject **eaStack;
	void *pStruct;
	bool bExpectChange;
} PyLibStructState;

static bool pyLibStruct_PreCB(ParseTable pti[], void *pStruct, int column, PyLibStructState *pState)
{
	PyObject *pObj = eaGetLast(&pState->eaStack);

	PERFINFO_AUTO_START_FUNC();

	if(TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(pti[column].type)))
	{
		PyObject *pNewObj = NULL;

		if(TOK_HAS_SUBTABLE(pti[column].type) && ParserGetTableKeyColumn(pti[column].subtable) > -1)
			pNewObj = PyDict_New();
		else
			pNewObj = PyList_New(0);

		if(PyDict_SetItemString(pObj, pti[column].name, pNewObj))
			PyErr_Print();

		if(TokenStoreGetNumElems(pti, column, pStruct, NULL) > 0)
			eaPush(&pState->eaStack, pNewObj);

		Py_XDECREF(pNewObj);
	}

	PERFINFO_AUTO_STOP();
	return false;
}

static void pyLibStruct_HandleStructChange(void *pStruct, PyLibStructState *pState)
{
	if(pStruct != pState->pStruct)
	{
		pState->pStruct = pStruct;

		if(pState->bExpectChange)
			pState->bExpectChange = false;
		else
			eaPop(&pState->eaStack);
	}
}

static bool pyLibStruct_PostCB(ParseTable pti[], void *pStruct, int column, PyLibStructState *pState)
{
	PERFINFO_AUTO_START_FUNC();

	pyLibStruct_HandleStructChange(pStruct, pState);

	if(TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(pti[column].type)) && TokenStoreGetNumElems(pti, column, pStruct, NULL) > 0)
		eaPop(&pState->eaStack);

	PERFINFO_AUTO_STOP();
	return false;
}

static bool pyLibStruct_TraverseCB(ParseTable pti[], void *pStruct, int column, int index, PyLibStructState *pState)
{
	PyObject *pObj = NULL;
	int iType = TOK_GET_TYPE(pti[column].type);

	PERFINFO_AUTO_START_FUNC();

	pyLibStruct_HandleStructChange(pStruct, pState);

	pObj = eaGetLast(&pState->eaStack);

	if(iType == TOK_START || iType == TOK_END || iType == TOK_IGNORE)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(TOK_HAS_SUBTABLE(pti[column].type))
	{
		void *pSubStruct = TokenStoreGetPointer(pti, column, pStruct, index, NULL);
		PyObject *pNewObj = PyDict_New();

		if(TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(pti[column].type)))
		{
			ParseTable *pSubTable = pti[column].subtable;
			int key = ParserGetTableKeyColumn(pSubTable);

			if(key == -1)
			{
				if(PyList_Append(pObj, pNewObj))
					PyErr_Print();

				if(pSubStruct)
				{
					pState->bExpectChange = true;
					eaPush(&pState->eaStack, pNewObj);
				}
			}
			else if(TypeIsInt(TOK_GET_TYPE(pSubTable[key].type)))
			{
				// pSubStruct almost has to exist
				int iSubKey = TokenStoreGetInt(pSubTable, key, pSubStruct, 0, NULL);
				PyObject *pKey = Py_BuildValue("i", iSubKey);

				if(PyDict_SetItem(pObj, pKey, pNewObj))
					PyErr_Print();

				Py_XDECREF(pKey);

				pState->bExpectChange = true;
				eaPush(&pState->eaStack, pNewObj);
			}
			else
			{
				// pSubStruct almost has to exist
				const char *pSubKey = TokenStoreGetString(pSubTable, key, pSubStruct, 0, NULL);

				if(PyDict_SetItemString(pObj, pSubKey, pNewObj))
					PyErr_Print();

				pState->bExpectChange = true;
				eaPush(&pState->eaStack, pNewObj);
			}
		}
		else if(pSubStruct)
		{
			if(PyDict_SetItemString(pObj, pti[column].name, pNewObj))
				PyErr_Print();

			pState->bExpectChange = true;
			eaPush(&pState->eaStack, pNewObj);
		}

		Py_XDECREF(pNewObj);
	}
	else
	{
		PyObject *pNewObj = NULL;

		if(TypeIsInt(TOK_GET_TYPE(pti[column].type)))
		{
			S64 iValue = TokenStoreGetIntAuto(pti, column, pStruct, index, NULL);
			pNewObj = Py_BuildValue("i", iValue);
		}
		else if(TOK_GET_TYPE(pti[column].type) & TOK_F32_X)
		{
			F32 fValue = TokenStoreGetF32(pti, column, pStruct, index, NULL);
			pNewObj = Py_BuildValue("f", fValue);
		}
		else if(TOK_GET_TYPE(pti[column].type) & TOK_STRING_X)
		{
			const char *pcValue = TokenStoreGetString(pti, column, pStruct, index, NULL);

			if(pcValue)
				pNewObj = PyString_FromString(pcValue);
			else
				pNewObj = PyLib_IncNone;
		}

		if(TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(pti[column].type)))
		{
			if(PyList_Append(pObj, pNewObj))
				PyErr_Print();
		}
		else
		{
			if(PyDict_SetItemString(pObj, pti[column].name, pNewObj))
				PyErr_Print();
		}

		Py_XDECREF(pNewObj);
	}

	PERFINFO_AUTO_STOP();
	return false;
}

PyObject *pyLibSerializeStructEx(void *pStruct, ParseTable pti[])
{
	PyLibStructState state = {0};
	PyObject *pObject = NULL;

	PERFINFO_AUTO_START_FUNC();
	eaPush(&state.eaStack, PyDict_New());
	state.bExpectChange = true;
	ParserTraverseParseTable(pti, pStruct, 0, TOK_PARSETABLE_INFO, pyLibStruct_TraverseCB, pyLibStruct_PreCB, pyLibStruct_PostCB, &state);
	pObject = eaPop(&state.eaStack);
	eaDestroy(&state.eaStack);
	PERFINFO_AUTO_STOP();

	return pObject;
}