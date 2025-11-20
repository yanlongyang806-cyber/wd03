#include "MultiVal.h"

#include "earray.h"
#include "EString.h"
#include "MemoryPool.h"
#include "mathutil.h"
#include "wininclude.h"
#include "StringCache.h"
#include "file.h"
#include "expression.h"
#include "quat.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define MULTIVAL_PRECHECK_EQUALITY_ON_COPY 1

static CRITICAL_SECTION multiValMemPoolCS = {0};

MP_DEFINE(MultiVal);

AUTO_RUN_FIRST;
void InitMultiValCS(void)
{
	MP_CREATE(MultiVal, 128);
	InitializeCriticalSection(&multiValMemPoolCS);
}

MultiVal* MultiValCreate(void)
{
	MultiVal *pRetVal;

	EnterCriticalSection(&multiValMemPoolCS);
	pRetVal = MP_ALLOC(MultiVal);
	LeaveCriticalSection(&multiValMemPoolCS);

	return pRetVal;
}

void MultiValConstruct(MultiVal* pmv, int type, int count)
{
	MultiInfo info;

	pmv->type	= type;
	pmv->intval	= 0;
	MultiValInfo(pmv, &info);

	
	switch(info.atype)
	{
		case MMA_FIXED:	
			pmv->ptr = calloc(info.size, 1);	
			break;

		case MMA_EARRAY:	
			if (count == 0) 
				break;

			switch(info.width)
			{
				case 4: 
					ea32Create((int **) &pmv->ptr);
					ea32SetSize((int **) &pmv->ptr, count);	
					break;

				case 8: 
					eaCreate((int ***) &pmv->ptr);
					eaSetSize((int ***) &pmv->ptr, count);
					break;

				default:
					devassert(info.width == 0);
					break;
			}
			break;
	}


}

void MultiValClear(MultiVal* src)
{
	if (MULTI_IS_FREE(src->type))
	{
		switch(MULTI_GET_TYPE(src->type))
		{
			case MMT_NONE:
				devassertmsg(0, "Attempting to free type of NONE.");
				break;
				
			case MMT_INTARRAY:
				eaiDestroy((int **) &src->ptr);
				break;

			case MMT_FLOATARRAY:
				eafDestroy((int **) &src->ptr);
				break;

			case MMT_NP_ENTITYARRAY:
				eaDestroy((void ***) &src->ptr);
				break;

			case MMT_VEC3:
			case MMT_VEC4:
			case MMT_MAT4:
			case MMT_QUAT:
			case MMT_NP_POINTER:
			case MMT_STRING:
				free((char *) src->str);
				src->str = NULL;
				break;

			default:
				devassertmsg(0, "Attempting to free unknown type.");
				free((char *) src->str);
				src->str = NULL;
				break;
		}
	}

	memset(src, 0, sizeof(MultiVal));
}

#define TOINDEX(type, i, ptr)	((type *) ptr)[i]
#define DUPINDEX(type, i)		TOINDEX(type, i, pdst) = TOINDEX(type, i, psrc);
void MultiValFill(MultiVal* dst, void *psrc)
{
	void *pdst;
	U32 i;
	MultiInfo info;
	MultiValInfo(dst, &info);
	pdst = info.ptr;

	for (i=0; i < info.count; i++)
	{
		switch(info.dtype)
		{
			case MMT_INT32:		DUPINDEX(S32,i);		break;
			case MMT_INT64:		DUPINDEX(S64,i);		break;
			case MMT_FLOAT32:	DUPINDEX(F32,i);		break;
			case MMT_FLOAT64:	DUPINDEX(F64,i);		break;
			case MMT_STRING:	
			{
				if (dst->type & MULTI_FREE)
					TOINDEX(char*, i, pdst) = strdup(TOINDEX(char*, i, psrc));
				else
					DUPINDEX(char*, i);
				break;
			}
		}
	}
}

void MultiValDestroy(MultiVal* val)
{
	MultiValClear(val);
	EnterCriticalSection(&multiValMemPoolCS);
	MP_FREE(MultiVal, val);
	LeaveCriticalSection(&multiValMemPoolCS);
}

static void *MultiValAlloc(void* cookie, size_t s)
{
	return calloc(s, 1);
}

void MultiValCopyWith(MultiVal* dst, CMultiVal* src, CustomMemoryAllocator fpMem, void *cookie, int deepCopy)
{
	int size = 0;
	int specialAlloc;

	//special case... some multivals ignore custom allocators
	if (fpMem != MultiValAlloc && !MULTI_IS_FREE(src->type) && MULTI_GET_TYPE(src->type) == MMT_STRING && allocFindString(src->ptr) == src->ptr)
	{
		fpMem = MultiValAlloc;
	}

	if (fpMem == 0)
		fpMem = MultiValAlloc;

	#if MULTIVAL_PRECHECK_EQUALITY_ON_COPY
	{
		if(	src &&
			fpMem == MultiValAlloc &&
			MULTI_GET_TYPE(src->type) == MULTI_GET_TYPE(dst->type))
		{
			switch(MULTI_GET_TYPE(src->type)){
				xcase MMT_VEC3:
					if(sameVec3((const F32*)src->str, (const F32*)dst->str)){
						return;
					}
				xcase MMT_VEC4:
					if(sameVec4((const F32*)src->str, (const F32*)dst->str)){
						return;
					}
				xcase MMT_MAT4:
					if(sameMat4((const Vec3*)src->str, (const Vec3*)dst->str)){
						return;
					}
				xcase MMT_QUAT:
					if(sameQuat((const Quat*)src->str, (const Quat*)dst->str)){
						return;
					}
				xcase MMT_STRING:{
					if(src->str == dst->str ||  
						(src->str && dst->str && !strcmp(src->str, dst->str))){
						return;
					}
				}
			}
		}
	}
	#endif


	specialAlloc = (fpMem != MultiValAlloc);



	MultiValClear(dst);
	if (src == 0) return;

	


	if (MULTI_IS_FREE(src->type) || specialAlloc || deepCopy)
	{
		switch(MULTI_GET_TYPE(src->type))
		{
			case MMT_STRING:	
				size = (int) strlen(src->ptr) + 1;	
				dst->type = (src->type | (specialAlloc ? 0 : MULTI_FREE));
				break;

			case MMT_VEC3:
				size = sizeof(Vec3);
				dst->type = (src->type | (specialAlloc ? 0 : MULTI_FREE));
				break;

			case MMT_VEC4:
				size = sizeof(Vec4);
				dst->type = (src->type | (specialAlloc ? 0 : MULTI_FREE));
				break;

			case MMT_MAT4:
				size = sizeof(Mat4);
				dst->type = (src->type | (specialAlloc ? 0 : MULTI_FREE));
				break;

			case MMT_QUAT:
				size = sizeof(Quat);
				dst->type = (src->type | (specialAlloc ? 0 : MULTI_FREE));
				break;

			case MMT_INTARRAY:
			case MMT_FLOATARRAY:
				eaiCompress((int **) &dst->ptr, (int **) &src->ptr, fpMem, cookie);
				dst->type = (src->type | (specialAlloc ? 0 : MULTI_FREE));
				return;

			case MMT_NP_ENTITYARRAY:
				eaCompress((void ***) &dst->ptr, (void ***) &src->ptr, fpMem, cookie);
				dst->type = (src->type | (specialAlloc ? 0 : MULTI_FREE));
				return;

			default:
				dst->type	= src->type;
				dst->intval = src->intval;
				break;
		}
		
		// Size will be 0 for any basic types, We do not want to allocate unless it is required
		if (dst->ptr == 0 && size)
		{
			dst->ptr =  fpMem(cookie, size);
			memcpy((void*) dst->ptr, src->ptr, size);
		}
	} else {
		//Copy the full 64 bits
		dst->intval = src->intval;
		dst->type   = src->type;
	}
}

void MultiValCopy(MultiVal* dst, CMultiVal* src)
{
	MultiValCopyWith(dst, src, MultiValAlloc, 0, false);
}

void MultiValDeepCopy(MultiVal* dst, CMultiVal* src)
{
	MultiValCopyWith(dst, src, MultiValAlloc, 0, true);
}


MultiVal *MultiValDup(CMultiVal* src)
{
	MultiVal* out = MultiValCreate();
	MultiValCopy(out, src);
	
	return out;
}

MultiVal *MultiValDupWith(CMultiVal* src, CustomMemoryAllocator fpMem, void* cookie)
{
	MultiVal* dst = fpMem(cookie, sizeof(MultiVal));
	MultiValCopyWith(dst, src, fpMem, cookie, false);

	return dst;
}

MultiVal* MultiValGetDummyOfType(MultiValType type)
{
	MultiVal* out = MultiValCreate();

	MultiValSetDummyType(out, type);

	return out;
}

void MultiValSetDummyType(MultiVal* val, MultiValType type)
{
	switch(type)
	{
	case MULTI_INT:
		val->intval = 1;
		break;
	case MULTI_FLOAT:
		val->floatval = 1.0f;
		break;
	case MULTI_STRING:
		val->str = MULTI_DUMMY_STRING;
		break;
	case MULTI_MAT4:
		val->ptr = unitmat;
		break;
	default:
		val->intval = 0;
	}
	val->type = type;
}

void MultiValInfo(CMultiVal* val, MultiInfo* info)
{
	switch(MULTI_GET_TYPE(val->type))
	{
		case MMT_INT32:
			info->dtype = MMT_INT32;
			info->atype = MMA_NONE;
			info->ptr   = (void*) &val->int32;
			info->size  = sizeof(val->int32);
			info->count = 1;
			info->width = sizeof(U32);
			break;

		case MMT_INT64:
			info->dtype = MMT_INT64;
			info->atype = MMA_NONE;
			info->ptr   = (void*) &val->intval;
			info->size  = sizeof(val->intval);
			info->count = 1;
			info->width = sizeof(U64);
			break;

		case MMT_FLOAT32:
			info->dtype = MMT_FLOAT32;
			info->atype = MMA_NONE;
			info->ptr	= (void*) &val->float32;
			info->size	= sizeof(val->float32);
			info->count = 1;
			info->width = sizeof(F32);
			break;
		
		case MMT_FLOAT64:
			info->dtype = MMT_FLOAT64;
			info->atype = MMA_NONE;
			info->ptr	= (void*) &val->intval;
			info->size	=sizeof(val->intval);
			info->count = 1;
			info->width = sizeof(F64);
			break;

		case MMT_STRING:
			info->dtype = MMT_STRING;
			info->atype = MMA_NONE;
			//note that info->ptr is a char**, not a char*
			info->ptr	= (void *) &val->ptr;
			info->count = 1;
			if (val->ptr)
				info->size	= (U32) (strlen(val->ptr) + 1);
			else
				info->size	= 0;
			info->width = sizeof(char *);
			break;

		case MMT_INTARRAY:
			info->dtype = MMT_INT32;
			info->atype = MMA_EARRAY;
			info->ptr	= (void *) val->ptr;
			info->count = eaiSize((S32 **) &val->ptr);
			info->width = sizeof(S32);
			if (info->ptr)
				info->size	= info->count * sizeof(S32);
			else
				info->size	= 0;
			break;

		case MMT_FLOATARRAY:
			info->dtype = MMT_FLOAT32;
			info->atype = MMA_EARRAY;
			info->ptr	= (void *) val->ptr;
			info->count = eafSize((F32 **) &val->ptr);
			info->width = sizeof(F32);
			if (info->ptr)
				info->size	= info->count * sizeof(F32);
			else
				info->size	= 0;
			break;

		case MMT_NP_ENTITYARRAY:
		case MMT_MULTIVALARRAY:
			info->dtype = MMT_NP_POINTER;
			info->atype = MMA_EARRAY;
			info->ptr	= (void *) val->ptr;
			info->count = eaSize((MultiVal ***) &val->ptr);	//Can't use VOID*** in eaSize Macro
			info->width = sizeof(void*);
			if (info->ptr)
				info->size	= info->count * sizeof(void*);
			else
				info->size  = 0;
			break;

		case MMT_VEC3:
			info->dtype = MMT_FLOAT32;
			info->atype = MMA_FIXED;
			info->ptr	= (void *) val->ptr;
			info->size  = sizeof(Vec3);
			info->width = sizeof(F32);
			info->count = info->size / sizeof(F32);
			break;
			
		case MMT_VEC4:
			info->dtype = MMT_FLOAT32;
			info->atype = MMA_FIXED;
			info->ptr	= (void *) val->ptr;
			info->size  = sizeof(Vec4);
			info->width = sizeof(F32);
			info->count = info->size / sizeof(F32);
			break;

		case MMT_MAT4:
			info->dtype = MMT_FLOAT32;
			info->atype = MMA_FIXED;
			info->ptr	= (void *) val->ptr;
			info->size  = sizeof(Mat4);
			info->width = sizeof(F32);
			info->count = info->size / sizeof(F32);
			break;

		case MMT_QUAT:
			info->dtype = MMT_FLOAT32;
			info->atype = MMA_FIXED;
			info->ptr	= (void *) val->ptr;
			info->size  = sizeof(Quat);
			info->width = sizeof(F32);
			info->count = info->size / sizeof(F32);
			break;

		case MMT_NP_POINTER:
			info->dtype = MMT_NP_POINTER;
			info->atype = MMT_NONE;
			info->ptr   = (void *) &val->ptr;
			info->size  = sizeof(info->ptr);
			info->width = sizeof(void*);
			info->count = 1;
			break;

		case MMT_NONE:
		default:
			info->dtype = MMT_NONE;
			info->atype = MMT_NONE;
			info->ptr   = 0;
			info->size  = 0;
			info->count = 0;
			info->width = 0;
			break;
	}
}

void MultiValTypeInfo(MultiValType t, MultiInfo* info)
{
	MultiVal mv;

	MultiValConstruct(&mv, t, 0);
	MultiValInfo(&mv, info);
	MultiValClear(&mv);
}



/***************************************************
          Set Functions (Deep Copy)
  
  These functions do deep copies of the data and 
set the 'Free' flag bit as needed.  Allocations
will need to be free'd using MultiValClear or
MultiValDestory to avoid leaks.
***************************************************/
void  MultiValSetInt(MultiVal* dst, S64 val)
{
	MultiValClear(dst);
	dst->type	= MULTI_INT;
	dst->intval	= val;
}

void MultiValSetFloat(MultiVal* dst, F64 val)
{
	MultiValClear(dst);
	dst->type		= MULTI_FLOAT;
	dst->floatval	= val;
}

void MultiValSetPointer(MultiVal* dst, const void* ptr, int size)
{
	MultiValClear(dst);
	dst->type		= MULTI_NP_POINTER_F;
	dst->ptr		= calloc(size, 1);

	if (ptr)
		memcpy((void *) dst->ptr, ptr, size);
}

void MultiValSetString(MultiVal* dst, const char* str)
{
	MultiValClear(dst);
	dst->type		= MULTI_STRING_F;
	dst->str		= (const char *) strdup(str);
}

void MultiValSetStringLen(MultiVal* dst, const char* str, int len)
{
	char *tmp		= malloc(len + 1);

	MultiValClear(dst);
	dst->type		= MULTI_STRING_F;
	dst->str		= (const char *) tmp;

	if (str)
		memcpy(tmp, str, len);
	else
		memset(tmp, '*', len);

	tmp[len] = 0;
}

void MultiValSetStringForExpr(ExprContext* pContext, MultiVal* multival, const char* str)
{
	if (!pContext) {
		MultiValSetString(multival, str);
	} else {
		MultiValClear(multival);
		multival->type = MULTI_STRING_F;
		multival->str = exprContextAllocString(pContext, str);
	}
}

void MultiValSetVec3(MultiVal* dst, const Vec3* val)
{
	MultiValClear(dst);
	dst->type		= MULTI_VEC3_F;
	dst->ptr		= calloc(sizeof(Vec3), 1);
	
	if (val)
		memcpy((void *) dst->ptr, val, sizeof(Vec3));
}

void MultiValSetVec4(MultiVal* dst, const Vec4* val)
{
	MultiValClear(dst);
	dst->type		= MULTI_VEC4_F;
	dst->ptr		= calloc(sizeof(Vec4), 1);

	if (val)
		memcpy((void *) dst->ptr, val, sizeof(Vec4));
}

void MultiValSetMat4(MultiVal* dst, const Mat4 val)
{
	MultiValClear(dst);
	dst->type		= MULTI_MAT4_F;
	dst->ptr		= calloc(sizeof(Mat4), 1);
	if (val)
		memcpy((void *) dst->ptr, val, sizeof(Mat4));
}

void MultiValSetQuat(MultiVal* dst, const Quat* val)
{
	MultiValClear(dst);
	dst->type		= MULTI_QUAT_F;
	dst->ptr		= calloc(sizeof(Quat), 1);
	if (val)
		memcpy((void *) dst->ptr, val, sizeof(Quat));
}

void MultiValSetIntArray(MultiVal* dst, int** intArray)
{
	MultiValClear(dst);
	dst->type		= MULTI_INTARRAY_F;
	eaiCopy((int **) &dst->ptr, intArray);
}

void MultiValSetFloatArray(MultiVal* dst, float** floatArray)
{
	MultiValClear(dst);
	dst->type		= MULTI_FLOATARRAY_F;
	eafCopy((int **) &dst->ptr, floatArray);
}



/***************************************************
        Reference Functions (Shallow Copy)
  
  These functions copy the pointers only, without
creating a new version of the data.   This means
modifications to this data will affect the original
data.
***************************************************/
void MultiValReferenceString(MultiVal* dst, const char* str)
{
	MultiValClear(dst);
	dst->type		= MULTI_STRING;
	dst->str		= str;
}

void MultiValReferencePointer(MultiVal* dst, const void* ptr)
{
	MultiValClear(dst);
	dst->type		= MULTI_NP_POINTER;
	dst->ptr		= ptr;
}

void MultiValReferenceVec3(MultiVal* dst, const Vec3* val)
{
	MultiValClear(dst);
	dst->type		= MULTI_VEC3;
	dst->ptr		= (const void *) val;
}

void MultiValReferenceVec4(MultiVal* dst, const Vec4* val)
{
	MultiValClear(dst);
	dst->type		= MULTI_VEC4;
	dst->ptr		= (const void *) val;
}

void MultiValReferenceMat4(MultiVal* dst, const Mat4* val)
{
	MultiValClear(dst);
	dst->type		= MULTI_MAT4;
	dst->ptr		= (const void *) val;
}

void MultiValReferenceQuat(MultiVal* dst, const Quat* val)
{
	MultiValClear(dst);
	dst->type		= MULTI_QUAT;
	dst->ptr		= (const void *) val;
}

void MultiValReferenceIntArray(MultiVal* dst, int** eiArray)
{
	MultiValClear(dst);
	dst->type		= MULTI_INTARRAY;
	dst->ptr		= (const void *) *eiArray;
}

void MultiValReferenceFloatArray(MultiVal* dst, float** efArray)
{
	MultiValClear(dst);
	dst->type		= MULTI_FLOATARRAY;
	dst->ptr		= (const void *) *efArray;
}


/***************************************************
				Get Functions 

Get functions return the data within the MultiVal.
***************************************************/
S64	MultiValGetInt(CMultiVal* src, bool* tst)
{
	bool result;
	if (tst == 0) tst = &result;

	switch(MULTI_GET_TYPE(src->type))
	{
		case MMT_INT32:
			*tst = true;
			return src->int32;

		case MMT_INT64:		
			*tst = true;  
			return src->intval;
		
		case MMT_FLOAT64:
			*tst = true;  
			return round(src->floatval);

		case MMT_FLOAT32:
			*tst = true;
			return round(src->float32);

		case MMT_STRING:
		{
			char* end = 0;
			S64 l   = (S64) (src->str ? strtol(src->str, &end, 10) : 0);

			if(end > src->str && *end=='\0')
			{
				*tst = true;
				return l;
			}
		}
	}

	*tst = false;
	return 0;
}

F64	MultiValGetFloat(CMultiVal* src, bool* tst)
{
	bool result;
	if (tst == 0) tst = &result;

	switch(MULTI_GET_TYPE(src->type))
	{
		case MMT_INT64:		*tst = true;  return src->intval;
		case MMT_FLOAT64:	*tst = true;  return src->floatval;

		case MMT_INT32:		*tst = true;  return src->int32;
		case MMT_FLOAT32:	*tst = true;  return src->float32;

		case MMT_STRING:
		{
			char* end = 0;
			float f   = (float) (src->str ? strtod(src->str, &end) : 0);

			if(end > src->str && *end=='\0')
			{
				*tst = true;
				return f;
			}
		}
	}

	*tst = false;
	return 0;
}

const char* MultiValGetString(CMultiVal* src, bool* tst)
{
	switch(MULTI_GET_TYPE(src->type))
	{
		case MMT_STRING:
			if (tst) *tst = true;
			return src->str;
	}

	if (tst) *tst = false;
	return 0;
}

Vec3* MultiValGetVec3(CMultiVal* src, bool *tst)
{
	if (MULTI_GET_TYPE(src->type) == MMT_VEC3)
	{
		if (tst) *tst = true;
		return (Vec3 *) src->ptr;
	}

	if (tst) *tst = false;
	return (Vec3 *) NULL;
}

Vec4* MultiValGetVec4(CMultiVal* src, bool *tst)
{
	if (MULTI_GET_TYPE(src->type) == MMT_VEC4)
	{
		if (tst) *tst = true;
		return (Vec4 *) src->ptr;
	}

	if (tst) *tst = false;
	return (Vec4 *) NULL;
}

Mat4* MultiValGetMat4(CMultiVal* src, bool *tst)
{
	if (MULTI_GET_TYPE(src->type) == MMT_MAT4)
	{
		if (tst) *tst = true;
		return (Mat4 *) src->ptr;
	}

	if (tst) *tst = false;
	return (Mat4 *) NULL;
}

Quat* MultiValGetQuat(CMultiVal* src, bool *tst)
{
	if (MULTI_GET_TYPE(src->type) == MMT_QUAT)
	{
		if (tst) *tst = true;
		return (Quat *) src->ptr;
	}

	if (tst) *tst = false;
	return (Quat *) NULL;
}

void* MultiValGetPointer(CMultiVal* src, bool* tst)
{
	if (MULTI_GET_TYPE(src->type) == MMT_NP_POINTER)
	{
		if (tst) *tst = true;
		return (void *)src->ptr;
	}

	if (tst) *tst = false;
	return NULL;
}

int** MultiValGetIntArray(CMultiVal* src, bool* tst)
{
	if (MULTI_GET_TYPE(src->type) == MMT_NP_POINTER)
	{
		if (tst) *tst = true;
		return (int **) &src->ptr;
	}

	if (tst) *tst = false;
	return (int **) NULL;
}

float**	MultiValGetFloatArray(CMultiVal* src, bool *tst)
{
	if (MULTI_GET_TYPE(src->type) == MMT_NP_POINTER)
	{
		if (tst) *tst = true;
		return (float **) &src->ptr;
	}

	if (tst) *tst = false;
	return (float **) NULL;
}

Entity*** MultiValGetEntityArray(CMultiVal* src, bool *tst)
{
	if(MULTI_GET_TYPE(src->type) == MMT_NP_ENTITYARRAY)
	{
		if(tst)
			*tst = true;
		return (Entity***)src->ptr;
	}

	if(tst)
		*tst = false;
	return NULL;
}

bool MultiValToBool(SA_PARAM_NN_VALID MultiVal* src)
{
	switch (MULTI_GET_TYPE(src->type))
	{
	case MMT_INT32:
		return !!src->int32;
	case MMT_INT64:
		return !!src->intval;
	case MMT_FLOAT32:
		return !!src->float32;
	case MMT_FLOAT64:
		return !!src->floatval;
	case MMT_STRING:
		return (src->str && src->str[0]);
	case MMT_NP_POINTER:
		return !!src->ptr;
	case MMT_NONE:
	case MMT_INVALID:
		return 0;
	default:
		return !!src->intval;
	}
}


/***************************************************
				Test Functions 

   These functions will test the type of data
contained within the MultiVal.
***************************************************/
bool MultiValIsEmpty(CMultiVal* src)
{
	return src->type == MULTI_NONE;
}

bool MultiValIsString(CMultiVal* src)
{
	switch(MULTI_GET_TYPE(src->type))
	{
		case MMT_STRING:
			return true;
	}
	return false;
}

bool MultiValIsNumber(CMultiVal* src)
{
	switch(MULTI_GET_TYPE(src->type))
	{

		case MMT_FLOAT32:
		case MMT_INT32:
		case MMT_FLOAT64:
		case MMT_INT64:
			return true;
	}
	return false;
}

bool MultiValIsOp(CMultiVal* src)
{
	return (src->type & MULTI_OPER_MASK);
}

bool MultiValIsNull(SA_PARAM_NN_VALID CMultiVal *src)
{
	switch(MULTI_GET_TYPE(src->type))
	{
		case MMT_STRING:
			return !(src->str && src->str[0]);
		default:
			return !(src->ptr);
	}
	return true;
}


// Returns 4 byte (3 char + NULL) representation of type
const char*  MultiValTypeToString(MultiValType t)
{
	//Uninitialized multival on stack
	if (t == 0xCCCCCCCC)
	{
		return "INV";
	}

	//We do not support GAME types
	if(MULTI_GET_GAME(t))
	{
		devassertmsg(0, "Game MultiVal types not currently supported");
		return "INV";
	}

	//If not, return name based on type
	switch(MULTI_FLAGLESS_TYPE(t)) 
	{
		case MULTI_INVALID:				return "INV";
		case MULTI_NONE:				return "NON";
		case MULTI_INT:					return "INT";
		case MULTI_FLOAT:				return "FLT";
		case MULTI_STRING:				return "STR";
		case MULTI_NP_POINTER:			return "PTR";

		case MULTI_INTARRAY:			return "INS";
		case MULTI_FLOATARRAY:			return "FLS";
		case MULTI_NP_ENTITYARRAY:		return "ENT";
		case MULTI_MULTIVALARRAY:		return "FIL";

		case MULTI_VEC3:				return "VEC";
		case MULTI_VEC4:				return "VC4";
		case MULTI_MAT4:				return "MAT";
		case MULTI_QUAT:				return "QAT";
		case MULTIOP_ADD:				return "ADD";
		case MULTIOP_SUBTRACT:			return "SUB";
		case MULTIOP_NEGATE:			return "NEG";
		case MULTIOP_MULTIPLY:			return "MUL";
		case MULTIOP_DIVIDE:			return "DIV";
		case MULTIOP_EXPONENT:			return "EXP";
		case MULTIOP_BIT_AND:			return "BAN";
		case MULTIOP_BIT_OR:			return "BOR";
		case MULTIOP_BIT_NOT:			return "BNT";
		case MULTIOP_BIT_XOR:			return "BXR";
		case MULTIOP_PAREN_OPEN:		return "O_P";
		case MULTIOP_PAREN_CLOSE:		return "C_P";
		case MULTIOP_BRACE_OPEN:		return "O_B";
		case MULTIOP_BRACE_CLOSE:		return "C_B";
		case MULTIOP_EQUALITY:			return "EQU";
		case MULTIOP_LESSTHAN:			return "LES";
		case MULTIOP_LESSTHANEQUALS:	return "NGR";
		case MULTIOP_GREATERTHAN:		return "GRE";
		case MULTIOP_GREATERTHANEQUALS: return "NLE";

		case MULTIOP_FUNCTIONCALL:		return "FUN";
		case MULTIOP_NP_STACKPTR:		return "S_I";
		case MULTIOP_IDENTIFIER:		return "IDS";
		case MULTIOP_STATICVAR:			return "S_V";
		case MULTIOP_COMMA:				return "COM";

		case MULTIOP_AND:				return "AND";
		case MULTIOP_OR:				return "ORR";
		case MULTIOP_NOT:				return "NOT";

		case MULTIOP_IF:				return "IF_";
		case MULTIOP_ELSE:				return "ELS";
		case MULTIOP_ELIF:				return "ELF";
		case MULTIOP_ENDIF:				return "EIF";

		case MULTIOP_RETURN:			return "RET";
		case MULTIOP_RETURNIFZERO:		return "RZ_";
		case MULTIOP_JUMP:				return "J__";
		case MULTIOP_JUMPIFZERO:		return "JZ_";

		case MULTIOP_CONTINUATION:		return "CON";
		case MULTIOP_STATEMENT_BREAK:	return "STM";
		case MULTIOP_OBJECT_PATH:		return "OBJ";
		case MULTIOP_ROOT_PATH:			return "RP_";

		case MULTIOP_LOC_STRING:		return "L_S";
		case MULTIOP_LOC_MAT4:			return "L_M";
	}
	devassertmsg(0, "Type not found");
	return "INV";
}


//Returns a type based on a 3 character compare 
// NOTE:  The src string does not need to be null terminated
#define MVTYPECHK(t,s)	if (!strnicmp(str, s, 3)) return t
MultiValType MultiValTypeFromString(const char* str)
{
	MVTYPECHK(MULTI_INVALID,			"INV");
	MVTYPECHK(MULTI_NONE,				"NON");
	MVTYPECHK(MULTI_INT,				"INT");
	MVTYPECHK(MULTI_FLOAT,				"FLT");
	MVTYPECHK(MULTI_STRING,				"STR");
	MVTYPECHK(MULTI_NP_POINTER,			"PTR");

	MVTYPECHK(MULTI_INTARRAY,			"INS");
	MVTYPECHK(MULTI_FLOATARRAY,			"FLS");
	MVTYPECHK(MULTI_NP_ENTITYARRAY,		"ENT");
	MVTYPECHK(MULTI_MULTIVALARRAY,		"FIL");

	MVTYPECHK(MULTI_VEC3,				"VEC");
	MVTYPECHK(MULTI_VEC4,				"VC4");
	MVTYPECHK(MULTI_MAT4,				"MAT");
	MVTYPECHK(MULTI_QUAT,				"QAT");

	// operators produced by tokenizer
	MVTYPECHK(MULTIOP_ADD,				"ADD");
	MVTYPECHK(MULTIOP_SUBTRACT,			"SUB");
	MVTYPECHK(MULTIOP_NEGATE,			"NEG");
	MVTYPECHK(MULTIOP_MULTIPLY,			"MUL");
	MVTYPECHK(MULTIOP_DIVIDE,			"DIV");
	MVTYPECHK(MULTIOP_EXPONENT,			"EXP");
	MVTYPECHK(MULTIOP_BIT_AND,			"BAN");
	MVTYPECHK(MULTIOP_BIT_OR,			"BOR");
	MVTYPECHK(MULTIOP_BIT_NOT,			"BNT");
	MVTYPECHK(MULTIOP_BIT_XOR,			"BXR");
	MVTYPECHK(MULTIOP_PAREN_OPEN,		"O_P");
	MVTYPECHK(MULTIOP_PAREN_CLOSE,		"C_P");
	MVTYPECHK(MULTIOP_BRACE_OPEN,		"O_B");
	MVTYPECHK(MULTIOP_BRACE_CLOSE,		"C_B");
	MVTYPECHK(MULTIOP_EQUALITY,			"EQU");
	MVTYPECHK(MULTIOP_LESSTHAN,			"LES");
	MVTYPECHK(MULTIOP_LESSTHANEQUALS,	"NGR");
	MVTYPECHK(MULTIOP_GREATERTHAN,		"GRE");
	MVTYPECHK(MULTIOP_GREATERTHANEQUALS,"NLE");

	MVTYPECHK(MULTIOP_FUNCTIONCALL,		"FUN");
	MVTYPECHK(MULTIOP_IDENTIFIER,		"IDS");
	MVTYPECHK(MULTIOP_STATICVAR,		"S_V");
	MVTYPECHK(MULTIOP_COMMA,			"COM");

	MVTYPECHK(MULTIOP_AND,				"AND");
	MVTYPECHK(MULTIOP_OR,				"ORR");
	MVTYPECHK(MULTIOP_NOT,				"NOT");

	MVTYPECHK(MULTIOP_IF,				"IF_");
	MVTYPECHK(MULTIOP_ELSE,				"ELS");
	MVTYPECHK(MULTIOP_ELIF,				"ELF");
	MVTYPECHK(MULTIOP_ENDIF,			"EIF");

	MVTYPECHK(MULTIOP_RETURN,			"RET");
	MVTYPECHK(MULTIOP_RETURNIFZERO,		"RZ_");
	MVTYPECHK(MULTIOP_JUMP,				"J__");
	MVTYPECHK(MULTIOP_JUMPIFZERO,		"JZ_");

	MVTYPECHK(MULTIOP_CONTINUATION,		"CON");
	MVTYPECHK(MULTIOP_STATEMENT_BREAK,	"STM");
	MVTYPECHK(MULTIOP_OBJECT_PATH,		"OBJ");
	MVTYPECHK(MULTIOP_ROOT_PATH,		"RP_");

	MVTYPECHK(MULTIOP_LOC_MAT4,			"L_M");
	MVTYPECHK(MULTIOP_LOC_STRING,		"L_S");

	return MULTI_INVALID;
}

const char* MultiValTypeToReadableString(MultiValType t)
{
	//We do not support GAME types
	if(MULTI_GET_GAME(t))
		return MultiValTypeToString(t);

	//If not, return name based on type
	switch(MULTI_FLAGLESS_TYPE(t)) 
	{
		case MULTI_INVALID:				return "inv";
		case MULTI_NONE:				return "none";
		case MULTI_INT:					return "int";
		case MULTI_FLOAT:				return "flt";
		case MULTI_STRING:				return "str";
		case MULTI_NP_POINTER:			return "ptr";

		case MULTI_INTARRAY:			return "intarray";
		case MULTI_FLOATARRAY:			return "floatarray";
		case MULTI_NP_ENTITYARRAY:		return "entityarray";
		case MULTI_MULTIVALARRAY:		return "multivalarray";

		case MULTI_VEC3:				return "Vec3";
		case MULTI_VEC4:				return "Vec4";
		case MULTI_MAT4:				return "Mat4";
		case MULTI_QUAT:				return "Quat";
		case MULTIOP_ADD:				return "+";
		case MULTIOP_SUBTRACT:			return "minus";
		case MULTIOP_NEGATE:			return "neg";
		case MULTIOP_MULTIPLY:			return "*";
		case MULTIOP_DIVIDE:			return "/";
		case MULTIOP_EXPONENT:			return "^";
		case MULTIOP_BIT_AND:			return "&";
		case MULTIOP_BIT_OR:			return "|";
		case MULTIOP_BIT_NOT:			return "~";
		case MULTIOP_BIT_XOR:			return "BXR";
		case MULTIOP_PAREN_OPEN:		return "(";
		case MULTIOP_PAREN_CLOSE:		return ")";
		case MULTIOP_BRACE_OPEN:		return "{";
		case MULTIOP_BRACE_CLOSE:		return "}";
		case MULTIOP_EQUALITY:			return "=";
		case MULTIOP_LESSTHAN:			return "<";
		case MULTIOP_LESSTHANEQUALS:	return "<=";
		case MULTIOP_GREATERTHAN:		return ">";
		case MULTIOP_GREATERTHANEQUALS: return ">=";

		case MULTIOP_FUNCTIONCALL:		return "func";
		case MULTIOP_IDENTIFIER:		return "ident";
		case MULTIOP_STATICVAR:			return "staticvar";
		case MULTIOP_COMMA:				return ",";

		case MULTIOP_AND:				return "and";
		case MULTIOP_OR:				return "or";
		case MULTIOP_NOT:				return "not";

		case MULTIOP_IF:				return "if";
		case MULTIOP_ELSE:				return "else";
		case MULTIOP_ELIF:				return "elif";
		case MULTIOP_ENDIF:				return "endif";

		case MULTIOP_RETURN:			return "return";
		case MULTIOP_RETURNIFZERO:		return "retifzero";
		case MULTIOP_JUMP:				return "j";
		case MULTIOP_JUMPIFZERO:		return "jz";
			
		case MULTIOP_CONTINUATION:		return "continuation";
		case MULTIOP_STATEMENT_BREAK:	return ";";
		case MULTIOP_OBJECT_PATH:		return "objpath";
		case MULTIOP_ROOT_PATH:			return "rootpath";

		case MULTIOP_LOC_MAT4:			return "loc";
		case MULTIOP_LOC_STRING:		return "loc";
	}
	return MultiValTypeToString(t);
}

char* MultiValPrint(CMultiVal* val)
{
	static char* estr = NULL;

	estrPrintf(&estr, "(%s", MultiValTypeToReadableString(val->type));
	switch(MULTI_GET_TYPE(val->type))
	{
	xcase MULTI_INT:
		estrConcatf(&estr, ":%"FORM_LL"d", val->intval);
	xcase MULTI_FLOAT:
		estrConcatf(&estr, ":%.3f", val->floatval);
	xcase MULTI_STRING:
		estrConcatf(&estr, ":\"%s\"", val->str);
	//xdefault:
		//estrConcatf(&estr, "%s", MultiValTypeToString(val->type));
	}
	estrConcatf(&estr, ")");

	return estr;
}

void MultiValToEString(SA_PARAM_NN_VALID CMultiVal* val, char **estr)
{
	switch (MULTI_GET_TYPE(val->type))
	{
	case MULTI_INT:
		estrConcatf(estr, "%"FORM_LL"d", val->intval);
	xcase MULTI_FLOAT:
		estrConcatf(estr, "%g", val->floatval);
	xcase MULTI_STRING:
		estrConcatf(estr, "%s", val->str);
	xdefault:
		if (isDevelopmentMode())
			estrConcatf(estr, "[Unsupported MultiVal Type]");
	}
}
