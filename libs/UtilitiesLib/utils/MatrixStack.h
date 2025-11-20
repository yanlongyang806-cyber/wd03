#ifndef GFXMATRIXSTACK_H
#define GFXMATRIXSTACK_H

typedef Mat3 Mat3;
typedef struct TransformationMatrix TransformationMatrix;

Mat3 *matrixStackGet(TransformationMatrix ***peaMatrixStack);
void matrixStackPush(TransformationMatrix ***peaMatrixStack);
void matrixStackPop(TransformationMatrix ***peaMatrixStack);

#endif