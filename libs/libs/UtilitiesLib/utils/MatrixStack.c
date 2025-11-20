#include "stdtypes.h"
#include "MatrixStack.h"
#include "EArray.h"
#include "MemoryPool.h"
#include "mathutil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

typedef struct TransformationMatrix
{
	Mat3 matrix;
} TransformationMatrix;

MP_DEFINE(TransformationMatrix);

Mat3 *matrixStackGet(TransformationMatrix ***peaMatrixStack)
{
	TransformationMatrix *pTransform = eaTail(peaMatrixStack);
	return pTransform ? &pTransform->matrix : NULL;
}

void matrixStackPush(TransformationMatrix ***peaMatrixStack)
{
	if (peaMatrixStack)
	{
		TransformationMatrix *pPrev = eaTail(peaMatrixStack);
		TransformationMatrix *pTransform;
		MP_CREATE(TransformationMatrix, 16);

		pTransform = MP_ALLOC(TransformationMatrix);
		copyMat3(pPrev ? pPrev->matrix : unitmat, pTransform->matrix);
		eaPush(peaMatrixStack, pTransform);
		devassertmsg(eaSize(peaMatrixStack) < 128, "Too many transformation matrices pushed, someone is forgetting to pop them.");
	}
}

void matrixStackPop(TransformationMatrix ***peaMatrixStack)
{
	if (peaMatrixStack)
	{
		Mat3 *pMatrix = eaPop(peaMatrixStack);
		if (devassertmsg(pMatrix, "Trying to pop a non-existent transformation matrix."))
			MP_FREE(TransformationMatrix, pMatrix);
	}
}
